/*
  Last changed Time-stamp: <2007-07-10 18:51:44 xtof>
  $Id: findpath.c,v 1.3 2007/10/21 21:01:35 Kinwalker Exp $
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#include "findpath.h"
#include "fold.h"
#include "fold_vars.h"
#include "utils.h"
#include "pair_mat.h"

#define  MAX(A,B)    (A)>(B)?(A):(B)

static char rcsid[] = "$Id: findpath.c,v 1.3 2007/10/21 21:01:35 Kinwalker Exp $";

extern int
energy_of_struct_pt (char *string, short * ptable, short *s, short *s1);


typedef struct move {
  int i;  /* i,j>0 insert; i,j<0 delete */
  int j;
  int when;  /* 0 if still available, else resulting distance from start */
  int E;
} move_t;

typedef struct intermediate {
  short *pt;     /* pair table */
  int Sen;       /* saddle energy so far */
  int curr_en;   /* current energy */
  move_t *moves; /* remaining moves to target */
} intermediate_t;

static int *pair_table_to_loop_index (short *pt);
static move_t* copy_moves(move_t *mvs);
static int compare_ptable(const void *A, const void *B);
static int compare_energy(const void *A, const void *B);
static int compare_moves_when(const void *A, const void *B);
static void free_intermediate(intermediate_t *i);


static char *seq;
static short *S, *S1;
static int BP_dist;
static move_t *path;
static int path_fwd; /* 1: struc1->struc2, else struc2 -> struc1 */

static int try_moves(intermediate_t c, int maxE,
		     intermediate_t *next, int dist) {
  int *loopidx, len, num_next=0, en, oldE;
  move_t *mv;
  short *pt;

  len = c.pt[0];
  loopidx = pair_table_to_loop_index(c.pt);
  oldE = c.Sen;
  for (mv=c.moves; mv->i!=0; mv++) {
    int i,j;
    if (mv->when>0) continue;
    i = mv->i; j = mv->j;
    pt = (short *) space(sizeof(short)*(len+1));
    memcpy(pt, c.pt,(len+1)*sizeof(short));
    if (j<0) { /*it's a delete move */
      pt[-i]=0;
      pt[-j]=0;
    } else { /* insert move */
      if ((loopidx[i] == loopidx[j]) && /* i and j belong to same loop */
	  (pt[i] == 0) && (pt[j]==0)     /* ... and are unpaired */
	  ) {
	pt[i]=j;
	pt[j]=i;
      } else {
	free(pt);
	continue; /* llegal move, try next; */
      }
    }
    en = energy_of_struct_pt(seq, pt, S, S1);
    if (en<maxE) {
      next[num_next].Sen = (en>oldE)?en:oldE;
      next[num_next].curr_en = en;
      next[num_next].pt = pt;
      mv->when=dist;
      mv->E = en;
      next[num_next++].moves = copy_moves(c.moves);
      mv->when=0;
    }
    else free(pt);
  }
  free(loopidx);
  return num_next;
}

int find_path_once(char *struc1, char *struc2, int maxE, int maxl) {
  short *pt1, *pt2;
  move_t *mlist;
  int i, len, d, dist=0, result;
  intermediate_t *current, *next;

  pt1 = make_pair_table(struc1);
  pt2 = make_pair_table(struc2);
  len = (int) strlen(struc1);

  mlist = (move_t *) space(sizeof(move_t)*len); /* bp_dist < n */

  for (i=1; i<=len; i++) {
    if (pt1[i] != pt2[i]) {
      if (i<pt1[i]) { /* need to delete this pair */
	mlist[dist].i = -i;
	mlist[dist].j = -pt1[i];
	mlist[dist++].when = 0;
      }
      if (i<pt2[i]) { /* need to insert this pair */
	mlist[dist].i = i;
	mlist[dist].j = pt2[i];
	mlist[dist++].when = 0;
      }
    }
  }
  free(pt2);
  BP_dist = dist;
  current = (intermediate_t *) space(sizeof(intermediate_t)*(maxl+1));
  current[0].pt = pt1;
  current[0].Sen = energy_of_struct_pt(seq, pt1, S, S1);
  current[0].moves = mlist;
  next = (intermediate_t *) space(sizeof(intermediate_t)*(dist*maxl+1));

  for (d=1; d<=dist; d++) { /* go through the distance classes */
    int c, u, num_next=0;
    intermediate_t *cc;

    for (c=0; current[c].pt != NULL; c++) {
      num_next += try_moves(current[c], maxE, next+num_next, d);
    }
    if (num_next==0) {
      for (cc=current; cc->pt != NULL; cc++) free_intermediate(cc);
      current[0].Sen=INT_MAX;
      break;
    }
    /* remove duplicats via sort|uniq  
       if this becomes a bottleneck we can use a hash instead */
    qsort(next, num_next, sizeof(intermediate_t),compare_ptable);
    for (u=0,c=1; c<num_next; c++) {
      if (memcmp(next[u].pt,next[c].pt,sizeof(short)*len)!=0) {
	next[++u] = next[c];
      } else {
	free_intermediate(next+c);
      }
    }
    num_next = u+1;
    qsort(next, num_next, sizeof(intermediate_t),compare_energy);
    /* free the old stuff */
    for (cc=current; cc->pt != NULL; cc++) free_intermediate(cc);
    for (u=0; u<maxl && u<num_next; u++) {
      current[u] = next[u];
    }
    for (; u<num_next; u++)
      free_intermediate(next+u);
    num_next=0;
  }
  free(next);
  path = current[0].moves;
  result = current[0].Sen;
  free(current[0].pt); free(current);
  return(result);
}


int find_saddle(char *sequence, char *struc1, char *struc2, int max) {
  int maxl, maxE, i;
  char *tmp;
  move_t *bestpath=NULL;
  int dir;

  maxE = INT_MAX - 1;
  seq = sequence;

  update_fold_params();
  make_pair_matrix();

  /* nummerically encode sequence */
  S = (short *) space(sizeof(short)*(strlen(seq)+1));
  S1 = (short *) space(sizeof(short)*(strlen(seq)+1));
  S[0] = S1[0] = (short) strlen(seq);
  for (i=0; i< strlen(seq); i++) {
    S[i+1] = encode_char(seq[i]);
    S1[i+1] = alias[S[i+1]];
  }

  maxl=1;
  do {
    int saddleE;
    path_fwd = !path_fwd;
    if (maxl>max) maxl=max;
    saddleE  = find_path_once(struc1, struc2, maxE, maxl);
    if (saddleE<maxE) {
      maxE = saddleE;
      if (bestpath) free(bestpath);
      bestpath = path;
      dir = path_fwd;
    } else
      free(path);
    tmp=struc1; struc1=struc2; struc2=tmp; 
    maxl *=2;
  } while (maxl<2*max);

  free(S); free(S1);
  path=bestpath;
  path_fwd = dir;
  return maxE;
}

void print_path(char *seq, char *struc) {
  int d;
  char *s;
  s = (char*)space((strlen(struc)+1)*sizeof(char));
  strcpy(s, struc);
  printf("%s\n%s %6.2f\n", seq, s, energy_of_struct(seq,s));
  qsort(path, BP_dist, sizeof(move_t), compare_moves_when);
  for (d=0; d<BP_dist; d++) {
    int i,j;
    i = path[d].i; j=path[d].j;
    if (i<0) { /* delete */
      s[(-i)-1] = s[(-j)-1] = '.';
    } else {
      s[i-1] = '('; s[j-1] = ')';
    }
    printf("%s %6.2f - %6.2f\n", s, energy_of_struct(seq,s), path[d].E/100.0);
  }
  free(s);
}

path_t* get_path(char *seq, char *s1, char* s2,
		 int maxkeep, int *num_entry) {
  int E, d;
  path_t *route;

  E = find_saddle(seq, s1, s2, maxkeep);

  *num_entry = BP_dist+1;
  route = (path_t *)space((BP_dist+1)*sizeof(path_t));

  qsort(path, BP_dist, sizeof(move_t), compare_moves_when);

  if (path_fwd) {
    /* memorize start of path */
    route[0].s  = (char*)space((strlen(s1)+1)*sizeof(char));
    strcpy(route[0].s, s1);
    route[0].en = energy_of_struct(seq, s1);

    for (d=0; d<BP_dist; d++) {
      int i,j;
      route[d+1].s = (char*)space((strlen(route[d].s)+1)*sizeof(char));
      strcpy(route[d+1].s, route[d].s);
      i = path[d].i; j=path[d].j;
      if (i<0) { /* delete */
	route[d+1].s[(-i)-1] = route[d+1].s[(-j)-1] = '.';
      } else {
	route[d+1].s[i-1] = '('; route[d+1].s[j-1] = ')';
      }
      route[d+1].en = path[d].E/100.0;
    }
  }
  else {
    /* memorize start of path */

    route[BP_dist].s  = (char*)space((strlen(s2)+1)*sizeof(char));
    strcpy(route[BP_dist].s, s2);
    route[BP_dist].en = energy_of_struct(seq, s2);
    
    for (d=0; d<BP_dist; d++) {
      int i,j;
      route[BP_dist-d-1].s
	= (char*)space((strlen(route[BP_dist-d].s)+1)*sizeof(char));
      strcpy(route[BP_dist-d-1].s, route[BP_dist-d].s);
	i = path[d].i; j=path[d].j;
      if (i<0) { /* delete */
	route[BP_dist-d-1].s[(-i)-1] = route[BP_dist-d-1].s[(-j)-1] = '.';
      } else {
	route[BP_dist-d-1].s[i-1] = '('; route[BP_dist-d-1].s[j-1] = ')';
      }
      route[BP_dist-d-1].en = path[d].E/100.0;
    }
  }

#if _DEBUG_FINDPATH_
  fprintf(stderr, "\n%s\n%s\n%s\n\n", seq, s1, s2);
  for (d=0; d<=BP_dist; d++)
    fprintf(stderr, "%s %6.2f\n", route[d].s, route[d].en);
  fprintf(stderr, "%d\n", *num_entry);
#endif

  free(path);
  path_fwd = 0;
  return (route);
}

static int *pair_table_to_loop_index (short *pt)
{
  /* number each position by which loop it belongs to (positions start
     at 1) */
  int i,hx,l,nl;
  int length;
  int *stack = NULL;
  int *loop = NULL;

  length = pt[0];
  stack  = (int *) space(sizeof(int)*(length+1));
  loop   = (int *) space(sizeof(int)*(length+2));
  hx=l=nl=0;

  for (i=1; i<=length; i++) {
    if ((pt[i] != 0) && (i < pt[i])) { /* ( */
      nl++; l=nl;
      stack[hx++]=i;
    }
    loop[i]=l;

    if ((pt[i] != 0) && (i > pt[i])) { /* ) */
      --hx;
      if (hx>0)
	l = loop[stack[hx-1]];  /* index of enclosing loop   */
      else l=0;                 /* external loop has index 0 */
      if (hx<0) {
	nrerror("unbalanced brackets in make_pair_table");
      }
    }
  }
  loop[0] = nl;
  free(stack);

#ifdef _DEBUG_LOOPIDX
  fprintf(stderr,"begin loop index\n");
  fprintf(stderr,
	  "....,....1....,....2....,....3....,....4"
	  "....,....5....,....6....,....7....,....8\n");
  print_structure(pt, loop[0]);
  for (i=1; i<=length; i++)
    fprintf(stderr,"%2d ", loop[i]);
  fprintf(stderr,"\n");
  fprintf(stderr, "end loop index\n");
  fflush(stderr);
#endif

  return (loop);
}

static void free_intermediate(intermediate_t *i) {
   free(i->pt);
   free(i->moves);
   i->pt = NULL;
   i->moves = NULL;
   i->Sen = INT_MAX;
 }

static int compare_ptable(const void *A, const void *B) {
  intermediate_t *a, *b;
  int c;
  a = (intermediate_t *) A;
  b = (intermediate_t *) B;

  c = memcmp(a->pt, b->pt, a->pt[0]*sizeof(short));
  if (c!=0) return c;
  if ((a->Sen - b->Sen) != 0) return (a->Sen - b->Sen);
  return (a->curr_en - b->curr_en);
}

static int compare_energy(const void *A, const void *B) {
  intermediate_t *a, *b;
  a = (intermediate_t *) A;
  b = (intermediate_t *) B;

  if ((a->Sen - b->Sen) != 0) return (a->Sen - b->Sen);
  return (a->curr_en - b->curr_en);
}

static int compare_moves_when(const void *A, const void *B) {
  move_t *a, *b;
  a = (move_t *) A;
  b = (move_t *) B;

  return(a->when - b->when);
}

static move_t* copy_moves(move_t *mvs) {
  move_t *new;
  new = (move_t *) space(sizeof(move_t)*(BP_dist+1));
  memcpy(new,mvs,sizeof(move_t)*(BP_dist+1));
  return new;
}

#ifdef _TEST_FINDPATH_

int main(int argc, char *argv[]) {
  char *seq, *s1, *s2;
  int E, maxkeep=10;
  int p_len = -1;
  path_t *route;

  if (argc>1)
    if (strcmp(argv[1],"-m")==0)
      sscanf(argv[2], "%d", &maxkeep);

  seq = get_line(stdin);
  s1 = get_line(stdin);
  s2 = get_line(stdin);
  //  dangles=2;//;//OptS->dangle;
  dangles=0;
#if 1//0
  E = find_saddle(seq, s1, s2, maxkeep);
  printf("saddle_energy = %6.2f\n", E/100.);
  if (path_fwd) 
    print_path(seq,s1);
  else
    print_path(seq,s2);
#else
  route = get_path(seq, s1, s2, maxkeep, &p_len);
#endif
  
  free(seq); free(s1); free(s2);
  return(EXIT_SUCCESS);
}

#endif
