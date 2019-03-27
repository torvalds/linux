
/* Data references and dependences detectors.
   Copyright (C) 2003, 2004, 2005, 2006 Free Software Foundation, Inc.
   Contributed by Sebastian Pop <pop@cri.ensmp.fr>

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free
Software Foundation; either version 2, or (at your option) any later
version.

GCC is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING.  If not, write to the Free
Software Foundation, 51 Franklin Street, Fifth Floor, Boston, MA
02110-1301, USA.  */

/* This pass walks a given loop structure searching for array
   references.  The information about the array accesses is recorded
   in DATA_REFERENCE structures. 
   
   The basic test for determining the dependences is: 
   given two access functions chrec1 and chrec2 to a same array, and 
   x and y two vectors from the iteration domain, the same element of 
   the array is accessed twice at iterations x and y if and only if:
   |             chrec1 (x) == chrec2 (y).
   
   The goals of this analysis are:
   
   - to determine the independence: the relation between two
     independent accesses is qualified with the chrec_known (this
     information allows a loop parallelization),
     
   - when two data references access the same data, to qualify the
     dependence relation with classic dependence representations:
     
       - distance vectors
       - direction vectors
       - loop carried level dependence
       - polyhedron dependence
     or with the chains of recurrences based representation,
     
   - to define a knowledge base for storing the data dependence 
     information,
     
   - to define an interface to access this data.
   
   
   Definitions:
   
   - subscript: given two array accesses a subscript is the tuple
   composed of the access functions for a given dimension.  Example:
   Given A[f1][f2][f3] and B[g1][g2][g3], there are three subscripts:
   (f1, g1), (f2, g2), (f3, g3).

   - Diophantine equation: an equation whose coefficients and
   solutions are integer constants, for example the equation 
   |   3*x + 2*y = 1
   has an integer solution x = 1 and y = -1.
     
   References:
   
   - "Advanced Compilation for High Performance Computing" by Randy
   Allen and Ken Kennedy.
   http://citeseer.ist.psu.edu/goff91practical.html 
   
   - "Loop Transformations for Restructuring Compilers - The Foundations" 
   by Utpal Banerjee.

   
*/

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "ggc.h"
#include "tree.h"

/* These RTL headers are needed for basic-block.h.  */
#include "rtl.h"
#include "basic-block.h"
#include "diagnostic.h"
#include "tree-flow.h"
#include "tree-dump.h"
#include "timevar.h"
#include "cfgloop.h"
#include "tree-chrec.h"
#include "tree-data-ref.h"
#include "tree-scalar-evolution.h"
#include "tree-pass.h"

static struct datadep_stats
{
  int num_dependence_tests;
  int num_dependence_dependent;
  int num_dependence_independent;
  int num_dependence_undetermined;

  int num_subscript_tests;
  int num_subscript_undetermined;
  int num_same_subscript_function;

  int num_ziv;
  int num_ziv_independent;
  int num_ziv_dependent;
  int num_ziv_unimplemented;

  int num_siv;
  int num_siv_independent;
  int num_siv_dependent;
  int num_siv_unimplemented;

  int num_miv;
  int num_miv_independent;
  int num_miv_dependent;
  int num_miv_unimplemented;
} dependence_stats;

static tree object_analysis (tree, tree, bool, struct data_reference **, 
			     tree *, tree *, tree *, tree *, tree *,
			     struct ptr_info_def **, subvar_t *);
static struct data_reference * init_data_ref (tree, tree, tree, tree, bool, 
					      tree, tree, tree, tree, tree, 
					      struct ptr_info_def *,
					      enum  data_ref_type);
static bool subscript_dependence_tester_1 (struct data_dependence_relation *,
					   struct data_reference *,
					   struct data_reference *);

/* Determine if PTR and DECL may alias, the result is put in ALIASED.
   Return FALSE if there is no symbol memory tag for PTR.  */

static bool
ptr_decl_may_alias_p (tree ptr, tree decl, 
		      struct data_reference *ptr_dr, 
		      bool *aliased)
{
  tree tag = NULL_TREE;
  struct ptr_info_def *pi = DR_PTR_INFO (ptr_dr);  

  gcc_assert (TREE_CODE (ptr) == SSA_NAME && DECL_P (decl));

  if (pi)
    tag = pi->name_mem_tag;
  if (!tag)
    tag = get_var_ann (SSA_NAME_VAR (ptr))->symbol_mem_tag;
  if (!tag)
    tag = DR_MEMTAG (ptr_dr);
  if (!tag)
    return false;
   
  *aliased = is_aliased_with (tag, decl);      
  return true;
}


/* Determine if two pointers may alias, the result is put in ALIASED.
   Return FALSE if there is no symbol memory tag for one of the pointers.  */

static bool
ptr_ptr_may_alias_p (tree ptr_a, tree ptr_b, 
		     struct data_reference *dra, 
		     struct data_reference *drb, 
		     bool *aliased)
{  
  tree tag_a = NULL_TREE, tag_b = NULL_TREE;
  struct ptr_info_def *pi_a = DR_PTR_INFO (dra);  
  struct ptr_info_def *pi_b = DR_PTR_INFO (drb);  

  if (pi_a && pi_a->name_mem_tag && pi_b && pi_b->name_mem_tag)
    {
      tag_a = pi_a->name_mem_tag;
      tag_b = pi_b->name_mem_tag;
    }
  else
    {
      tag_a = get_var_ann (SSA_NAME_VAR (ptr_a))->symbol_mem_tag;
      if (!tag_a)
	tag_a = DR_MEMTAG (dra);
      if (!tag_a)
	return false;
      
      tag_b = get_var_ann (SSA_NAME_VAR (ptr_b))->symbol_mem_tag;
      if (!tag_b)
	tag_b = DR_MEMTAG (drb);
      if (!tag_b)
	return false;
    }
  
  if (tag_a == tag_b)
    *aliased = true;
  else
    *aliased = may_aliases_intersect (tag_a, tag_b);
  
  return true;
}


/* Determine if BASE_A and BASE_B may alias, the result is put in ALIASED.
   Return FALSE if there is no symbol memory tag for one of the symbols.  */

static bool
may_alias_p (tree base_a, tree base_b,
	     struct data_reference *dra,
	     struct data_reference *drb,
	     bool *aliased)
{
  if (TREE_CODE (base_a) == ADDR_EXPR || TREE_CODE (base_b) == ADDR_EXPR)
    {
      if (TREE_CODE (base_a) == ADDR_EXPR && TREE_CODE (base_b) == ADDR_EXPR)
	{
	 *aliased = (TREE_OPERAND (base_a, 0) == TREE_OPERAND (base_b, 0));
	 return true;
	}
      if (TREE_CODE (base_a) == ADDR_EXPR)
	return ptr_decl_may_alias_p (base_b, TREE_OPERAND (base_a, 0), drb, 
				     aliased);
      else
	return ptr_decl_may_alias_p (base_a, TREE_OPERAND (base_b, 0), dra, 
				     aliased);
    }

  return ptr_ptr_may_alias_p (base_a, base_b, dra, drb, aliased);
}


/* Determine if a pointer (BASE_A) and a record/union access (BASE_B)
   are not aliased. Return TRUE if they differ.  */
static bool
record_ptr_differ_p (struct data_reference *dra,
		     struct data_reference *drb)
{
  bool aliased;
  tree base_a = DR_BASE_OBJECT (dra);
  tree base_b = DR_BASE_OBJECT (drb);

  if (TREE_CODE (base_b) != COMPONENT_REF)
    return false;

  /* Peel COMPONENT_REFs to get to the base. Do not peel INDIRECT_REFs.
     For a.b.c.d[i] we will get a, and for a.b->c.d[i] we will get a.b.  
     Probably will be unnecessary with struct alias analysis.  */
  while (TREE_CODE (base_b) == COMPONENT_REF)
     base_b = TREE_OPERAND (base_b, 0);
  /* Compare a record/union access (b.c[i] or p->c[i]) and a pointer
     ((*q)[i]).  */
  if (TREE_CODE (base_a) == INDIRECT_REF
      && ((TREE_CODE (base_b) == VAR_DECL
	   && (ptr_decl_may_alias_p (TREE_OPERAND (base_a, 0), base_b, dra, 
				     &aliased)
	       && !aliased))
	  || (TREE_CODE (base_b) == INDIRECT_REF
	      && (ptr_ptr_may_alias_p (TREE_OPERAND (base_a, 0), 
				       TREE_OPERAND (base_b, 0), dra, drb, 
				       &aliased)
		  && !aliased))))
    return true;
  else
    return false;
}

/* Determine if two record/union accesses are aliased. Return TRUE if they 
   differ.  */
static bool
record_record_differ_p (struct data_reference *dra,
			struct data_reference *drb)
{
  bool aliased;
  tree base_a = DR_BASE_OBJECT (dra);
  tree base_b = DR_BASE_OBJECT (drb);

  if (TREE_CODE (base_b) != COMPONENT_REF 
      || TREE_CODE (base_a) != COMPONENT_REF)
    return false;

  /* Peel COMPONENT_REFs to get to the base. Do not peel INDIRECT_REFs.
     For a.b.c.d[i] we will get a, and for a.b->c.d[i] we will get a.b.  
     Probably will be unnecessary with struct alias analysis.  */
  while (TREE_CODE (base_b) == COMPONENT_REF)
    base_b = TREE_OPERAND (base_b, 0);
  while (TREE_CODE (base_a) == COMPONENT_REF)
    base_a = TREE_OPERAND (base_a, 0);

  if (TREE_CODE (base_a) == INDIRECT_REF
      && TREE_CODE (base_b) == INDIRECT_REF
      && ptr_ptr_may_alias_p (TREE_OPERAND (base_a, 0), 
			      TREE_OPERAND (base_b, 0), 
			      dra, drb, &aliased)
      && !aliased)
    return true;
  else
    return false;
}
    
/* Determine if an array access (BASE_A) and a record/union access (BASE_B)
   are not aliased. Return TRUE if they differ.  */
static bool
record_array_differ_p (struct data_reference *dra,
		       struct data_reference *drb)
{  
  bool aliased;
  tree base_a = DR_BASE_OBJECT (dra);
  tree base_b = DR_BASE_OBJECT (drb);

  if (TREE_CODE (base_b) != COMPONENT_REF)
    return false;

  /* Peel COMPONENT_REFs to get to the base. Do not peel INDIRECT_REFs.
     For a.b.c.d[i] we will get a, and for a.b->c.d[i] we will get a.b.  
     Probably will be unnecessary with struct alias analysis.  */
  while (TREE_CODE (base_b) == COMPONENT_REF)
     base_b = TREE_OPERAND (base_b, 0);

  /* Compare a record/union access (b.c[i] or p->c[i]) and an array access 
     (a[i]). In case of p->c[i] use alias analysis to verify that p is not
     pointing to a.  */
  if (TREE_CODE (base_a) == VAR_DECL
      && (TREE_CODE (base_b) == VAR_DECL
	  || (TREE_CODE (base_b) == INDIRECT_REF
	      && (ptr_decl_may_alias_p (TREE_OPERAND (base_b, 0), base_a, drb, 
					&aliased)
		  && !aliased))))
    return true;
  else
    return false;
}


/* Determine if an array access (BASE_A) and a pointer (BASE_B)
   are not aliased. Return TRUE if they differ.  */
static bool
array_ptr_differ_p (tree base_a, tree base_b, 	     
		    struct data_reference *drb)
{  
  bool aliased;

  /* In case one of the bases is a pointer (a[i] and (*p)[i]), we check with the
     help of alias analysis that p is not pointing to a.  */
  if (TREE_CODE (base_a) == VAR_DECL && TREE_CODE (base_b) == INDIRECT_REF 
      && (ptr_decl_may_alias_p (TREE_OPERAND (base_b, 0), base_a, drb, &aliased)
	  && !aliased))
    return true;
  else
    return false;
}


/* This is the simplest data dependence test: determines whether the
   data references A and B access the same array/region.  Returns
   false when the property is not computable at compile time.
   Otherwise return true, and DIFFER_P will record the result. This
   utility will not be necessary when alias_sets_conflict_p will be
   less conservative.  */

static bool
base_object_differ_p (struct data_reference *a,
		      struct data_reference *b,
		      bool *differ_p)
{
  tree base_a = DR_BASE_OBJECT (a);
  tree base_b = DR_BASE_OBJECT (b);
  bool aliased;

  if (!base_a || !base_b)
    return false;

  /* Determine if same base.  Example: for the array accesses
     a[i], b[i] or pointer accesses *a, *b, bases are a, b.  */
  if (base_a == base_b)
    {
      *differ_p = false;
      return true;
    }

  /* For pointer based accesses, (*p)[i], (*q)[j], the bases are (*p)
     and (*q)  */
  if (TREE_CODE (base_a) == INDIRECT_REF && TREE_CODE (base_b) == INDIRECT_REF
      && TREE_OPERAND (base_a, 0) == TREE_OPERAND (base_b, 0))
    {
      *differ_p = false;
      return true;
    }

  /* Record/union based accesses - s.a[i], t.b[j]. bases are s.a,t.b.  */ 
  if (TREE_CODE (base_a) == COMPONENT_REF && TREE_CODE (base_b) == COMPONENT_REF
      && TREE_OPERAND (base_a, 0) == TREE_OPERAND (base_b, 0)
      && TREE_OPERAND (base_a, 1) == TREE_OPERAND (base_b, 1))
    {
      *differ_p = false;
      return true;
    }


  /* Determine if different bases.  */

  /* At this point we know that base_a != base_b.  However, pointer
     accesses of the form x=(*p) and y=(*q), whose bases are p and q,
     may still be pointing to the same base. In SSAed GIMPLE p and q will
     be SSA_NAMES in this case.  Therefore, here we check if they are
     really two different declarations.  */
  if (TREE_CODE (base_a) == VAR_DECL && TREE_CODE (base_b) == VAR_DECL)
    {
      *differ_p = true;
      return true;
    }

  /* In case one of the bases is a pointer (a[i] and (*p)[i]), we check with the
     help of alias analysis that p is not pointing to a.  */
  if (array_ptr_differ_p (base_a, base_b, b) 
      || array_ptr_differ_p (base_b, base_a, a))
    {
      *differ_p = true;
      return true;
    }

  /* If the bases are pointers ((*q)[i] and (*p)[i]), we check with the
     help of alias analysis they don't point to the same bases.  */
  if (TREE_CODE (base_a) == INDIRECT_REF && TREE_CODE (base_b) == INDIRECT_REF 
      && (may_alias_p (TREE_OPERAND (base_a, 0), TREE_OPERAND (base_b, 0), a, b, 
		       &aliased)
	  && !aliased))
    {
      *differ_p = true;
      return true;
    }

  /* Compare two record/union bases s.a and t.b: s != t or (a != b and
     s and t are not unions).  */
  if (TREE_CODE (base_a) == COMPONENT_REF && TREE_CODE (base_b) == COMPONENT_REF
      && ((TREE_CODE (TREE_OPERAND (base_a, 0)) == VAR_DECL
           && TREE_CODE (TREE_OPERAND (base_b, 0)) == VAR_DECL
           && TREE_OPERAND (base_a, 0) != TREE_OPERAND (base_b, 0))
          || (TREE_CODE (TREE_TYPE (TREE_OPERAND (base_a, 0))) == RECORD_TYPE 
              && TREE_CODE (TREE_TYPE (TREE_OPERAND (base_b, 0))) == RECORD_TYPE
              && TREE_OPERAND (base_a, 1) != TREE_OPERAND (base_b, 1))))
    {
      *differ_p = true;
      return true;
    }

  /* Compare a record/union access (b.c[i] or p->c[i]) and a pointer
     ((*q)[i]).  */
  if (record_ptr_differ_p (a, b) || record_ptr_differ_p (b, a))
    {
      *differ_p = true;
      return true;
    }

  /* Compare a record/union access (b.c[i] or p->c[i]) and an array access 
     (a[i]). In case of p->c[i] use alias analysis to verify that p is not
     pointing to a.  */
  if (record_array_differ_p (a, b) || record_array_differ_p (b, a))
    {
      *differ_p = true;
      return true;
    }

  /* Compare two record/union accesses (b.c[i] or p->c[i]).  */
  if (record_record_differ_p (a, b))
    {
      *differ_p = true;
      return true;
    }

  return false;
}

/* Function base_addr_differ_p.

   This is the simplest data dependence test: determines whether the
   data references DRA and DRB access the same array/region.  Returns
   false when the property is not computable at compile time.
   Otherwise return true, and DIFFER_P will record the result.

   The algorithm:   
   1. if (both DRA and DRB are represented as arrays)
          compare DRA.BASE_OBJECT and DRB.BASE_OBJECT
   2. else if (both DRA and DRB are represented as pointers)
          try to prove that DRA.FIRST_LOCATION == DRB.FIRST_LOCATION
   3. else if (DRA and DRB are represented differently or 2. fails)
          only try to prove that the bases are surely different
*/

static bool
base_addr_differ_p (struct data_reference *dra,
		    struct data_reference *drb,
		    bool *differ_p)
{
  tree addr_a = DR_BASE_ADDRESS (dra);
  tree addr_b = DR_BASE_ADDRESS (drb);
  tree type_a, type_b;
  bool aliased;

  if (!addr_a || !addr_b)
    return false;

  type_a = TREE_TYPE (addr_a);
  type_b = TREE_TYPE (addr_b);

  gcc_assert (POINTER_TYPE_P (type_a) &&  POINTER_TYPE_P (type_b));

  /* 1. if (both DRA and DRB are represented as arrays)
            compare DRA.BASE_OBJECT and DRB.BASE_OBJECT.  */
  if (DR_TYPE (dra) == ARRAY_REF_TYPE && DR_TYPE (drb) == ARRAY_REF_TYPE)
    return base_object_differ_p (dra, drb, differ_p);

  /* 2. else if (both DRA and DRB are represented as pointers)
	    try to prove that DRA.FIRST_LOCATION == DRB.FIRST_LOCATION.  */
  /* If base addresses are the same, we check the offsets, since the access of 
     the data-ref is described by {base addr + offset} and its access function,
     i.e., in order to decide whether the bases of data-refs are the same we 
     compare both base addresses and offsets.  */
  if (DR_TYPE (dra) == POINTER_REF_TYPE && DR_TYPE (drb) == POINTER_REF_TYPE
      && (addr_a == addr_b 
	  || (TREE_CODE (addr_a) == ADDR_EXPR && TREE_CODE (addr_b) == ADDR_EXPR
	      && TREE_OPERAND (addr_a, 0) == TREE_OPERAND (addr_b, 0))))
    {
      /* Compare offsets.  */
      tree offset_a = DR_OFFSET (dra); 
      tree offset_b = DR_OFFSET (drb);
      
      STRIP_NOPS (offset_a);
      STRIP_NOPS (offset_b);

      /* FORNOW: we only compare offsets that are MULT_EXPR, i.e., we don't handle
	 PLUS_EXPR.  */
      if (offset_a == offset_b
	  || (TREE_CODE (offset_a) == MULT_EXPR 
	      && TREE_CODE (offset_b) == MULT_EXPR
	      && TREE_OPERAND (offset_a, 0) == TREE_OPERAND (offset_b, 0)
	      && TREE_OPERAND (offset_a, 1) == TREE_OPERAND (offset_b, 1)))
	{
	  *differ_p = false;
	  return true;
	}
    }

  /*  3. else if (DRA and DRB are represented differently or 2. fails) 
              only try to prove that the bases are surely different.  */

  /* Apply alias analysis.  */
  if (may_alias_p (addr_a, addr_b, dra, drb, &aliased) && !aliased)
    {
      *differ_p = true;
      return true;
    }
  
  /* An instruction writing through a restricted pointer is "independent" of any 
     instruction reading or writing through a different pointer, in the same 
     block/scope.  */
  else if ((TYPE_RESTRICT (type_a) && !DR_IS_READ (dra))
      || (TYPE_RESTRICT (type_b) && !DR_IS_READ (drb)))
    {
      *differ_p = true;
      return true;
    }
  return false;
}

/* Returns true iff A divides B.  */

static inline bool 
tree_fold_divides_p (tree a, 
		     tree b)
{
  /* Determines whether (A == gcd (A, B)).  */
  return tree_int_cst_equal (a, tree_fold_gcd (a, b));
}

/* Returns true iff A divides B.  */

static inline bool 
int_divides_p (int a, int b)
{
  return ((b % a) == 0);
}



/* Dump into FILE all the data references from DATAREFS.  */ 

void 
dump_data_references (FILE *file, VEC (data_reference_p, heap) *datarefs)
{
  unsigned int i;
  struct data_reference *dr;

  for (i = 0; VEC_iterate (data_reference_p, datarefs, i, dr); i++)
    dump_data_reference (file, dr);
}

/* Dump into FILE all the dependence relations from DDRS.  */ 

void 
dump_data_dependence_relations (FILE *file, 
				VEC (ddr_p, heap) *ddrs)
{
  unsigned int i;
  struct data_dependence_relation *ddr;

  for (i = 0; VEC_iterate (ddr_p, ddrs, i, ddr); i++)
    dump_data_dependence_relation (file, ddr);
}

/* Dump function for a DATA_REFERENCE structure.  */

void 
dump_data_reference (FILE *outf, 
		     struct data_reference *dr)
{
  unsigned int i;
  
  fprintf (outf, "(Data Ref: \n  stmt: ");
  print_generic_stmt (outf, DR_STMT (dr), 0);
  fprintf (outf, "  ref: ");
  print_generic_stmt (outf, DR_REF (dr), 0);
  fprintf (outf, "  base_object: ");
  print_generic_stmt (outf, DR_BASE_OBJECT (dr), 0);
  
  for (i = 0; i < DR_NUM_DIMENSIONS (dr); i++)
    {
      fprintf (outf, "  Access function %d: ", i);
      print_generic_stmt (outf, DR_ACCESS_FN (dr, i), 0);
    }
  fprintf (outf, ")\n");
}

/* Dump function for a SUBSCRIPT structure.  */

void 
dump_subscript (FILE *outf, struct subscript *subscript)
{
  tree chrec = SUB_CONFLICTS_IN_A (subscript);

  fprintf (outf, "\n (subscript \n");
  fprintf (outf, "  iterations_that_access_an_element_twice_in_A: ");
  print_generic_stmt (outf, chrec, 0);
  if (chrec == chrec_known)
    fprintf (outf, "    (no dependence)\n");
  else if (chrec_contains_undetermined (chrec))
    fprintf (outf, "    (don't know)\n");
  else
    {
      tree last_iteration = SUB_LAST_CONFLICT (subscript);
      fprintf (outf, "  last_conflict: ");
      print_generic_stmt (outf, last_iteration, 0);
    }
	  
  chrec = SUB_CONFLICTS_IN_B (subscript);
  fprintf (outf, "  iterations_that_access_an_element_twice_in_B: ");
  print_generic_stmt (outf, chrec, 0);
  if (chrec == chrec_known)
    fprintf (outf, "    (no dependence)\n");
  else if (chrec_contains_undetermined (chrec))
    fprintf (outf, "    (don't know)\n");
  else
    {
      tree last_iteration = SUB_LAST_CONFLICT (subscript);
      fprintf (outf, "  last_conflict: ");
      print_generic_stmt (outf, last_iteration, 0);
    }

  fprintf (outf, "  (Subscript distance: ");
  print_generic_stmt (outf, SUB_DISTANCE (subscript), 0);
  fprintf (outf, "  )\n");
  fprintf (outf, " )\n");
}

/* Print the classic direction vector DIRV to OUTF.  */

void
print_direction_vector (FILE *outf,
			lambda_vector dirv,
			int length)
{
  int eq;

  for (eq = 0; eq < length; eq++)
    {
      enum data_dependence_direction dir = dirv[eq];

      switch (dir)
	{
	case dir_positive:
	  fprintf (outf, "    +");
	  break;
	case dir_negative:
	  fprintf (outf, "    -");
	  break;
	case dir_equal:
	  fprintf (outf, "    =");
	  break;
	case dir_positive_or_equal:
	  fprintf (outf, "   +=");
	  break;
	case dir_positive_or_negative:
	  fprintf (outf, "   +-");
	  break;
	case dir_negative_or_equal:
	  fprintf (outf, "   -=");
	  break;
	case dir_star:
	  fprintf (outf, "    *");
	  break;
	default:
	  fprintf (outf, "indep");
	  break;
	}
    }
  fprintf (outf, "\n");
}

/* Print a vector of direction vectors.  */

void
print_dir_vectors (FILE *outf, VEC (lambda_vector, heap) *dir_vects,
		   int length)
{
  unsigned j;
  lambda_vector v;

  for (j = 0; VEC_iterate (lambda_vector, dir_vects, j, v); j++)
    print_direction_vector (outf, v, length);
}

/* Print a vector of distance vectors.  */

void
print_dist_vectors  (FILE *outf, VEC (lambda_vector, heap) *dist_vects,
		     int length)
{
  unsigned j;
  lambda_vector v;

  for (j = 0; VEC_iterate (lambda_vector, dist_vects, j, v); j++)
    print_lambda_vector (outf, v, length);
}

/* Debug version.  */

void 
debug_data_dependence_relation (struct data_dependence_relation *ddr)
{
  dump_data_dependence_relation (stderr, ddr);
}

/* Dump function for a DATA_DEPENDENCE_RELATION structure.  */

void 
dump_data_dependence_relation (FILE *outf, 
			       struct data_dependence_relation *ddr)
{
  struct data_reference *dra, *drb;

  dra = DDR_A (ddr);
  drb = DDR_B (ddr);
  fprintf (outf, "(Data Dep: \n");
  if (DDR_ARE_DEPENDENT (ddr) == chrec_dont_know)
    fprintf (outf, "    (don't know)\n");
  
  else if (DDR_ARE_DEPENDENT (ddr) == chrec_known)
    fprintf (outf, "    (no dependence)\n");
  
  else if (DDR_ARE_DEPENDENT (ddr) == NULL_TREE)
    {
      unsigned int i;
      struct loop *loopi;

      for (i = 0; i < DDR_NUM_SUBSCRIPTS (ddr); i++)
	{
	  fprintf (outf, "  access_fn_A: ");
	  print_generic_stmt (outf, DR_ACCESS_FN (dra, i), 0);
	  fprintf (outf, "  access_fn_B: ");
	  print_generic_stmt (outf, DR_ACCESS_FN (drb, i), 0);
	  dump_subscript (outf, DDR_SUBSCRIPT (ddr, i));
	}

      fprintf (outf, "  loop nest: (");
      for (i = 0; VEC_iterate (loop_p, DDR_LOOP_NEST (ddr), i, loopi); i++)
	fprintf (outf, "%d ", loopi->num);
      fprintf (outf, ")\n");

      for (i = 0; i < DDR_NUM_DIST_VECTS (ddr); i++)
	{
	  fprintf (outf, "  distance_vector: ");
	  print_lambda_vector (outf, DDR_DIST_VECT (ddr, i),
			       DDR_NB_LOOPS (ddr));
	}

      for (i = 0; i < DDR_NUM_DIR_VECTS (ddr); i++)
	{
	  fprintf (outf, "  direction_vector: ");
	  print_direction_vector (outf, DDR_DIR_VECT (ddr, i),
				  DDR_NB_LOOPS (ddr));
	}
    }

  fprintf (outf, ")\n");
}

/* Dump function for a DATA_DEPENDENCE_DIRECTION structure.  */

void
dump_data_dependence_direction (FILE *file, 
				enum data_dependence_direction dir)
{
  switch (dir)
    {
    case dir_positive: 
      fprintf (file, "+");
      break;
      
    case dir_negative:
      fprintf (file, "-");
      break;
      
    case dir_equal:
      fprintf (file, "=");
      break;
      
    case dir_positive_or_negative:
      fprintf (file, "+-");
      break;
      
    case dir_positive_or_equal: 
      fprintf (file, "+=");
      break;
      
    case dir_negative_or_equal: 
      fprintf (file, "-=");
      break;
      
    case dir_star: 
      fprintf (file, "*"); 
      break;
      
    default: 
      break;
    }
}

/* Dumps the distance and direction vectors in FILE.  DDRS contains
   the dependence relations, and VECT_SIZE is the size of the
   dependence vectors, or in other words the number of loops in the
   considered nest.  */

void 
dump_dist_dir_vectors (FILE *file, VEC (ddr_p, heap) *ddrs)
{
  unsigned int i, j;
  struct data_dependence_relation *ddr;
  lambda_vector v;

  for (i = 0; VEC_iterate (ddr_p, ddrs, i, ddr); i++)
    if (DDR_ARE_DEPENDENT (ddr) == NULL_TREE && DDR_AFFINE_P (ddr))
      {
	for (j = 0; VEC_iterate (lambda_vector, DDR_DIST_VECTS (ddr), j, v); j++)
	  {
	    fprintf (file, "DISTANCE_V (");
	    print_lambda_vector (file, v, DDR_NB_LOOPS (ddr));
	    fprintf (file, ")\n");
	  }

	for (j = 0; VEC_iterate (lambda_vector, DDR_DIR_VECTS (ddr), j, v); j++)
	  {
	    fprintf (file, "DIRECTION_V (");
	    print_direction_vector (file, v, DDR_NB_LOOPS (ddr));
	    fprintf (file, ")\n");
	  }
      }

  fprintf (file, "\n\n");
}

/* Dumps the data dependence relations DDRS in FILE.  */

void 
dump_ddrs (FILE *file, VEC (ddr_p, heap) *ddrs)
{
  unsigned int i;
  struct data_dependence_relation *ddr;

  for (i = 0; VEC_iterate (ddr_p, ddrs, i, ddr); i++)
    dump_data_dependence_relation (file, ddr);

  fprintf (file, "\n\n");
}



/* Estimate the number of iterations from the size of the data and the
   access functions.  */

static void
estimate_niter_from_size_of_data (struct loop *loop, 
				  tree opnd0, 
				  tree access_fn, 
				  tree stmt)
{
  tree estimation = NULL_TREE;
  tree array_size, data_size, element_size;
  tree init, step;

  init = initial_condition (access_fn);
  step = evolution_part_in_loop_num (access_fn, loop->num);

  array_size = TYPE_SIZE (TREE_TYPE (opnd0));
  element_size = TYPE_SIZE (TREE_TYPE (TREE_TYPE (opnd0)));
  if (array_size == NULL_TREE 
      || TREE_CODE (array_size) != INTEGER_CST
      || TREE_CODE (element_size) != INTEGER_CST)
    return;

  data_size = fold_build2 (EXACT_DIV_EXPR, integer_type_node,
			   array_size, element_size);

  if (init != NULL_TREE
      && step != NULL_TREE
      && TREE_CODE (init) == INTEGER_CST
      && TREE_CODE (step) == INTEGER_CST)
    {
      tree i_plus_s = fold_build2 (PLUS_EXPR, integer_type_node, init, step);
      tree sign = fold_binary (GT_EXPR, boolean_type_node, i_plus_s, init);

      if (sign == boolean_true_node)
	estimation = fold_build2 (CEIL_DIV_EXPR, integer_type_node,
				  fold_build2 (MINUS_EXPR, integer_type_node,
					       data_size, init), step);

      /* When the step is negative, as in PR23386: (init = 3, step =
	 0ffffffff, data_size = 100), we have to compute the
	 estimation as ceil_div (init, 0 - step) + 1.  */
      else if (sign == boolean_false_node)
	estimation = 
	  fold_build2 (PLUS_EXPR, integer_type_node,
		       fold_build2 (CEIL_DIV_EXPR, integer_type_node,
				    init,
				    fold_build2 (MINUS_EXPR, unsigned_type_node,
						 integer_zero_node, step)),
		       integer_one_node);

      if (estimation)
	record_estimate (loop, estimation, boolean_true_node, stmt);
    }
}

/* Given an ARRAY_REF node REF, records its access functions.
   Example: given A[i][3], record in ACCESS_FNS the opnd1 function,
   i.e. the constant "3", then recursively call the function on opnd0,
   i.e. the ARRAY_REF "A[i]".  
   If ESTIMATE_ONLY is true, we just set the estimated number of loop
   iterations, we don't store the access function.
   The function returns the base name: "A".  */

static tree
analyze_array_indexes (struct loop *loop,
		       VEC(tree,heap) **access_fns, 
		       tree ref, tree stmt,
		       bool estimate_only)
{
  tree opnd0, opnd1;
  tree access_fn;

  opnd0 = TREE_OPERAND (ref, 0);
  opnd1 = TREE_OPERAND (ref, 1);

  /* The detection of the evolution function for this data access is
     postponed until the dependence test.  This lazy strategy avoids
     the computation of access functions that are of no interest for
     the optimizers.  */
  access_fn = instantiate_parameters
    (loop, analyze_scalar_evolution (loop, opnd1));

  if (estimate_only 
      && chrec_contains_undetermined (loop->estimated_nb_iterations))
    estimate_niter_from_size_of_data (loop, opnd0, access_fn, stmt);

  if (!estimate_only)
    VEC_safe_push (tree, heap, *access_fns, access_fn);
  
  /* Recursively record other array access functions.  */
  if (TREE_CODE (opnd0) == ARRAY_REF)
    return analyze_array_indexes (loop, access_fns, opnd0, stmt, estimate_only);

  /* Return the base name of the data access.  */
  else
    return opnd0;
}

/* For an array reference REF contained in STMT, attempt to bound the
   number of iterations in the loop containing STMT  */

void 
estimate_iters_using_array (tree stmt, tree ref)
{
  analyze_array_indexes (loop_containing_stmt (stmt), NULL, ref, stmt, 
			 true);
}
  
/* For a data reference REF contained in the statement STMT, initialize
   a DATA_REFERENCE structure, and return it.  IS_READ flag has to be
   set to true when REF is in the right hand side of an
   assignment.  */

struct data_reference *
analyze_array (tree stmt, tree ref, bool is_read)
{
  struct data_reference *res;
  VEC(tree,heap) *acc_fns;

  if (dump_file && (dump_flags & TDF_DETAILS))
    {
      fprintf (dump_file, "(analyze_array \n");
      fprintf (dump_file, "  (ref = ");
      print_generic_stmt (dump_file, ref, 0);
      fprintf (dump_file, ")\n");
    }

  res = XNEW (struct data_reference);

  DR_STMT (res) = stmt;
  DR_REF (res) = ref;
  acc_fns = VEC_alloc (tree, heap, 3);
  DR_BASE_OBJECT (res) = analyze_array_indexes
    (loop_containing_stmt (stmt), &acc_fns, ref, stmt, false);
  DR_TYPE (res) = ARRAY_REF_TYPE;
  DR_SET_ACCESS_FNS (res, acc_fns);
  DR_IS_READ (res) = is_read;
  DR_BASE_ADDRESS (res) = NULL_TREE;
  DR_OFFSET (res) = NULL_TREE;
  DR_INIT (res) = NULL_TREE;
  DR_STEP (res) = NULL_TREE;
  DR_OFFSET_MISALIGNMENT (res) = NULL_TREE;
  DR_MEMTAG (res) = NULL_TREE;
  DR_PTR_INFO (res) = NULL;

  if (dump_file && (dump_flags & TDF_DETAILS))
    fprintf (dump_file, ")\n");

  return res;
}

/* Analyze an indirect memory reference, REF, that comes from STMT.
   IS_READ is true if this is an indirect load, and false if it is
   an indirect store.
   Return a new data reference structure representing the indirect_ref, or
   NULL if we cannot describe the access function.  */

static struct data_reference *
analyze_indirect_ref (tree stmt, tree ref, bool is_read)
{
  struct loop *loop = loop_containing_stmt (stmt);
  tree ptr_ref = TREE_OPERAND (ref, 0);
  tree access_fn = analyze_scalar_evolution (loop, ptr_ref);
  tree init = initial_condition_in_loop_num (access_fn, loop->num);
  tree base_address = NULL_TREE, evolution, step = NULL_TREE;
  struct ptr_info_def *ptr_info = NULL;

  if (TREE_CODE (ptr_ref) == SSA_NAME)
    ptr_info = SSA_NAME_PTR_INFO (ptr_ref);

  STRIP_NOPS (init);
  if (access_fn == chrec_dont_know || !init || init == chrec_dont_know)
    {
      if (dump_file && (dump_flags & TDF_DETAILS))
	{
	  fprintf (dump_file, "\nBad access function of ptr: ");
	  print_generic_expr (dump_file, ref, TDF_SLIM);
	  fprintf (dump_file, "\n");
	}
      return NULL;
    }

  if (dump_file && (dump_flags & TDF_DETAILS))
    {
      fprintf (dump_file, "\nAccess function of ptr: ");
      print_generic_expr (dump_file, access_fn, TDF_SLIM);
      fprintf (dump_file, "\n");
    }

  if (!expr_invariant_in_loop_p (loop, init))
    {
    if (dump_file && (dump_flags & TDF_DETAILS))
	fprintf (dump_file, "\ninitial condition is not loop invariant.\n");	
    }
  else
    {
      base_address = init;
      evolution = evolution_part_in_loop_num (access_fn, loop->num);
      if (evolution != chrec_dont_know)
	{       
	  if (!evolution)
	    step = ssize_int (0);
	  else  
	    {
	      if (TREE_CODE (evolution) == INTEGER_CST)
		step = fold_convert (ssizetype, evolution);
	      else
		if (dump_file && (dump_flags & TDF_DETAILS))
		  fprintf (dump_file, "\nnon constant step for ptr access.\n");	
	    }
	}
      else
	if (dump_file && (dump_flags & TDF_DETAILS))
	  fprintf (dump_file, "\nunknown evolution of ptr.\n");	
    }
  return init_data_ref (stmt, ref, NULL_TREE, access_fn, is_read, base_address, 
			NULL_TREE, step, NULL_TREE, NULL_TREE, 
			ptr_info, POINTER_REF_TYPE);
}

/* For a data reference REF contained in the statement STMT, initialize
   a DATA_REFERENCE structure, and return it.  */

struct data_reference *
init_data_ref (tree stmt, 
	       tree ref,
	       tree base,
	       tree access_fn,
	       bool is_read,
	       tree base_address,
	       tree init_offset,
	       tree step,
	       tree misalign,
	       tree memtag,
               struct ptr_info_def *ptr_info,
	       enum data_ref_type type)
{
  struct data_reference *res;
  VEC(tree,heap) *acc_fns;

  if (dump_file && (dump_flags & TDF_DETAILS))
    {
      fprintf (dump_file, "(init_data_ref \n");
      fprintf (dump_file, "  (ref = ");
      print_generic_stmt (dump_file, ref, 0);
      fprintf (dump_file, ")\n");
    }

  res = XNEW (struct data_reference);

  DR_STMT (res) = stmt;
  DR_REF (res) = ref;
  DR_BASE_OBJECT (res) = base;
  DR_TYPE (res) = type;
  acc_fns = VEC_alloc (tree, heap, 3);
  DR_SET_ACCESS_FNS (res, acc_fns);
  VEC_quick_push (tree, DR_ACCESS_FNS (res), access_fn);
  DR_IS_READ (res) = is_read;
  DR_BASE_ADDRESS (res) = base_address;
  DR_OFFSET (res) = init_offset;
  DR_INIT (res) = NULL_TREE;
  DR_STEP (res) = step;
  DR_OFFSET_MISALIGNMENT (res) = misalign;
  DR_MEMTAG (res) = memtag;
  DR_PTR_INFO (res) = ptr_info;

  if (dump_file && (dump_flags & TDF_DETAILS))
    fprintf (dump_file, ")\n");

  return res;
}

/* Function strip_conversions

   Strip conversions that don't narrow the mode.  */

static tree 
strip_conversion (tree expr)
{
  tree to, ti, oprnd0;
  
  while (TREE_CODE (expr) == NOP_EXPR || TREE_CODE (expr) == CONVERT_EXPR)
    {
      to = TREE_TYPE (expr);
      oprnd0 = TREE_OPERAND (expr, 0);
      ti = TREE_TYPE (oprnd0);
 
      if (!INTEGRAL_TYPE_P (to) || !INTEGRAL_TYPE_P (ti))
	return NULL_TREE;
      if (GET_MODE_SIZE (TYPE_MODE (to)) < GET_MODE_SIZE (TYPE_MODE (ti)))
	return NULL_TREE;
      
      expr = oprnd0;
    }
  return expr; 
}


/* Function analyze_offset_expr

   Given an offset expression EXPR received from get_inner_reference, analyze
   it and create an expression for INITIAL_OFFSET by substituting the variables 
   of EXPR with initial_condition of the corresponding access_fn in the loop. 
   E.g., 
      for i
         for (j = 3; j < N; j++)
            a[j].b[i][j] = 0;
	 
   For a[j].b[i][j], EXPR will be 'i * C_i + j * C_j + C'. 'i' cannot be 
   substituted, since its access_fn in the inner loop is i. 'j' will be 
   substituted with 3. An INITIAL_OFFSET will be 'i * C_i + C`', where
   C` =  3 * C_j + C.

   Compute MISALIGN (the misalignment of the data reference initial access from
   its base). Misalignment can be calculated only if all the variables can be 
   substituted with constants, otherwise, we record maximum possible alignment
   in ALIGNED_TO. In the above example, since 'i' cannot be substituted, MISALIGN 
   will be NULL_TREE, and the biggest divider of C_i (a power of 2) will be 
   recorded in ALIGNED_TO.

   STEP is an evolution of the data reference in this loop in bytes.
   In the above example, STEP is C_j.

   Return FALSE, if the analysis fails, e.g., there is no access_fn for a 
   variable. In this case, all the outputs (INITIAL_OFFSET, MISALIGN, ALIGNED_TO
   and STEP) are NULL_TREEs. Otherwise, return TRUE.

*/

static bool
analyze_offset_expr (tree expr, 
		     struct loop *loop, 
		     tree *initial_offset,
		     tree *misalign,
		     tree *aligned_to,
		     tree *step)
{
  tree oprnd0;
  tree oprnd1;
  tree left_offset = ssize_int (0);
  tree right_offset = ssize_int (0);
  tree left_misalign = ssize_int (0);
  tree right_misalign = ssize_int (0);
  tree left_step = ssize_int (0);
  tree right_step = ssize_int (0);
  enum tree_code code;
  tree init, evolution;
  tree left_aligned_to = NULL_TREE, right_aligned_to = NULL_TREE;

  *step = NULL_TREE;
  *misalign = NULL_TREE;
  *aligned_to = NULL_TREE;  
  *initial_offset = NULL_TREE;

  /* Strip conversions that don't narrow the mode.  */
  expr = strip_conversion (expr);
  if (!expr)
    return false;

  /* Stop conditions:
     1. Constant.  */
  if (TREE_CODE (expr) == INTEGER_CST)
    {
      *initial_offset = fold_convert (ssizetype, expr);
      *misalign = fold_convert (ssizetype, expr);      
      *step = ssize_int (0);
      return true;
    }

  /* 2. Variable. Try to substitute with initial_condition of the corresponding
     access_fn in the current loop.  */
  if (SSA_VAR_P (expr))
    {
      tree access_fn = analyze_scalar_evolution (loop, expr);

      if (access_fn == chrec_dont_know)
	/* No access_fn.  */
	return false;

      init = initial_condition_in_loop_num (access_fn, loop->num);
      if (!expr_invariant_in_loop_p (loop, init))
	/* Not enough information: may be not loop invariant.  
	   E.g., for a[b[i]], we get a[D], where D=b[i]. EXPR is D, its 
	   initial_condition is D, but it depends on i - loop's induction
	   variable.  */	  
	return false;

      evolution = evolution_part_in_loop_num (access_fn, loop->num);
      if (evolution && TREE_CODE (evolution) != INTEGER_CST)
	/* Evolution is not constant.  */
	return false;

      if (TREE_CODE (init) == INTEGER_CST)
	*misalign = fold_convert (ssizetype, init);
      else
	/* Not constant, misalignment cannot be calculated.  */
	*misalign = NULL_TREE;

      *initial_offset = fold_convert (ssizetype, init); 

      *step = evolution ? fold_convert (ssizetype, evolution) : ssize_int (0);
      return true;      
    }

  /* Recursive computation.  */
  if (!BINARY_CLASS_P (expr))
    {
      /* We expect to get binary expressions (PLUS/MINUS and MULT).  */
      if (dump_file && (dump_flags & TDF_DETAILS))
        {
	  fprintf (dump_file, "\nNot binary expression ");
          print_generic_expr (dump_file, expr, TDF_SLIM);
	  fprintf (dump_file, "\n");
	}
      return false;
    }
  oprnd0 = TREE_OPERAND (expr, 0);
  oprnd1 = TREE_OPERAND (expr, 1);

  if (!analyze_offset_expr (oprnd0, loop, &left_offset, &left_misalign, 
			    &left_aligned_to, &left_step)
      || !analyze_offset_expr (oprnd1, loop, &right_offset, &right_misalign, 
			       &right_aligned_to, &right_step))
    return false;

  /* The type of the operation: plus, minus or mult.  */
  code = TREE_CODE (expr);
  switch (code)
    {
    case MULT_EXPR:
      if (TREE_CODE (right_offset) != INTEGER_CST)
	/* RIGHT_OFFSET can be not constant. For example, for arrays of variable 
	   sized types. 
	   FORNOW: We don't support such cases.  */
	return false;

      /* Strip conversions that don't narrow the mode.  */
      left_offset = strip_conversion (left_offset);      
      if (!left_offset)
	return false;      
      /* Misalignment computation.  */
      if (SSA_VAR_P (left_offset))
	{
	  /* If the left side contains variables that can't be substituted with 
	     constants, the misalignment is unknown. However, if the right side 
	     is a multiple of some alignment, we know that the expression is
	     aligned to it. Therefore, we record such maximum possible value.
	   */
	  *misalign = NULL_TREE;
	  *aligned_to = ssize_int (highest_pow2_factor (right_offset));
	}
      else 
	{
	  /* The left operand was successfully substituted with constant.  */	  
	  if (left_misalign)
	    {
	      /* In case of EXPR '(i * C1 + j) * C2', LEFT_MISALIGN is 
		 NULL_TREE.  */
	      *misalign  = size_binop (code, left_misalign, right_misalign);
	      if (left_aligned_to && right_aligned_to)
		*aligned_to = size_binop (MIN_EXPR, left_aligned_to, 
					  right_aligned_to);
	      else 
		*aligned_to = left_aligned_to ? 
		  left_aligned_to : right_aligned_to;
	    }
	  else
	    *misalign = NULL_TREE; 
	}

      /* Step calculation.  */
      /* Multiply the step by the right operand.  */
      *step  = size_binop (MULT_EXPR, left_step, right_offset);
      break;
   
    case PLUS_EXPR:
    case MINUS_EXPR:
      /* Combine the recursive calculations for step and misalignment.  */
      *step = size_binop (code, left_step, right_step);

      /* Unknown alignment.  */
      if ((!left_misalign && !left_aligned_to)
	  || (!right_misalign && !right_aligned_to))
	{
	  *misalign = NULL_TREE;
	  *aligned_to = NULL_TREE;
	  break;
	}

      if (left_misalign && right_misalign)
	*misalign = size_binop (code, left_misalign, right_misalign);
      else
	*misalign = left_misalign ? left_misalign : right_misalign;

      if (left_aligned_to && right_aligned_to)
	*aligned_to = size_binop (MIN_EXPR, left_aligned_to, right_aligned_to);
      else 
	*aligned_to = left_aligned_to ? left_aligned_to : right_aligned_to;

      break;

    default:
      gcc_unreachable ();
    }

  /* Compute offset.  */
  *initial_offset = fold_convert (ssizetype, 
				  fold_build2 (code, TREE_TYPE (left_offset), 
					       left_offset, 
					       right_offset));
  return true;
}

/* Function address_analysis

   Return the BASE of the address expression EXPR.
   Also compute the OFFSET from BASE, MISALIGN and STEP.

   Input:
   EXPR - the address expression that is being analyzed
   STMT - the statement that contains EXPR or its original memory reference
   IS_READ - TRUE if STMT reads from EXPR, FALSE if writes to EXPR
   DR - data_reference struct for the original memory reference

   Output:
   BASE (returned value) - the base of the data reference EXPR.
   INITIAL_OFFSET - initial offset of EXPR from BASE (an expression)
   MISALIGN - offset of EXPR from BASE in bytes (a constant) or NULL_TREE if the
              computation is impossible 
   ALIGNED_TO - maximum alignment of EXPR or NULL_TREE if MISALIGN can be 
                calculated (doesn't depend on variables)
   STEP - evolution of EXPR in the loop
 
   If something unexpected is encountered (an unsupported form of data-ref),
   then NULL_TREE is returned.  
 */

static tree
address_analysis (tree expr, tree stmt, bool is_read, struct data_reference *dr, 
		  tree *offset, tree *misalign, tree *aligned_to, tree *step)
{
  tree oprnd0, oprnd1, base_address, offset_expr, base_addr0, base_addr1;
  tree address_offset = ssize_int (0), address_misalign = ssize_int (0);
  tree dummy, address_aligned_to = NULL_TREE;
  struct ptr_info_def *dummy1;
  subvar_t dummy2;

  switch (TREE_CODE (expr))
    {
    case PLUS_EXPR:
    case MINUS_EXPR:
      /* EXPR is of form {base +/- offset} (or {offset +/- base}).  */
      oprnd0 = TREE_OPERAND (expr, 0);
      oprnd1 = TREE_OPERAND (expr, 1);

      STRIP_NOPS (oprnd0);
      STRIP_NOPS (oprnd1);
      
      /* Recursively try to find the base of the address contained in EXPR.
	 For offset, the returned base will be NULL.  */
      base_addr0 = address_analysis (oprnd0, stmt, is_read, dr, &address_offset, 
				     &address_misalign, &address_aligned_to, 
				     step);

      base_addr1 = address_analysis (oprnd1, stmt, is_read,  dr, &address_offset, 
				     &address_misalign, &address_aligned_to, 
				     step);

      /* We support cases where only one of the operands contains an 
	 address.  */
      if ((base_addr0 && base_addr1) || (!base_addr0 && !base_addr1))
	{
	  if (dump_file && (dump_flags & TDF_DETAILS))
	    {
	      fprintf (dump_file, 
		    "\neither more than one address or no addresses in expr ");
	      print_generic_expr (dump_file, expr, TDF_SLIM);
	      fprintf (dump_file, "\n");
	    }	
	  return NULL_TREE;
	}

      /* To revert STRIP_NOPS.  */
      oprnd0 = TREE_OPERAND (expr, 0);
      oprnd1 = TREE_OPERAND (expr, 1);
      
      offset_expr = base_addr0 ? 
	fold_convert (ssizetype, oprnd1) : fold_convert (ssizetype, oprnd0);

      /* EXPR is of form {base +/- offset} (or {offset +/- base}). If offset is 
	 a number, we can add it to the misalignment value calculated for base,
	 otherwise, misalignment is NULL.  */
      if (TREE_CODE (offset_expr) == INTEGER_CST && address_misalign)
	{
	  *misalign = size_binop (TREE_CODE (expr), address_misalign, 
				  offset_expr);
	  *aligned_to = address_aligned_to;
	}
      else
	{
	  *misalign = NULL_TREE;
	  *aligned_to = NULL_TREE;
	}

      /* Combine offset (from EXPR {base + offset}) with the offset calculated
	 for base.  */
      *offset = size_binop (TREE_CODE (expr), address_offset, offset_expr);
      return base_addr0 ? base_addr0 : base_addr1;

    case ADDR_EXPR:
      base_address = object_analysis (TREE_OPERAND (expr, 0), stmt, is_read, 
				      &dr, offset, misalign, aligned_to, step, 
				      &dummy, &dummy1, &dummy2);
      return base_address;

    case SSA_NAME:
      if (!POINTER_TYPE_P (TREE_TYPE (expr)))
	{
	  if (dump_file && (dump_flags & TDF_DETAILS))
	    {
	      fprintf (dump_file, "\nnot pointer SSA_NAME ");
	      print_generic_expr (dump_file, expr, TDF_SLIM);
	      fprintf (dump_file, "\n");
	    }	
	  return NULL_TREE;
	}
      *aligned_to = ssize_int (TYPE_ALIGN_UNIT (TREE_TYPE (TREE_TYPE (expr))));
      *misalign = ssize_int (0);
      *offset = ssize_int (0);
      *step = ssize_int (0);
      return expr;
      
    default:
      return NULL_TREE;
    }
}


/* Function object_analysis

   Create a data-reference structure DR for MEMREF.
   Return the BASE of the data reference MEMREF if the analysis is possible.
   Also compute the INITIAL_OFFSET from BASE, MISALIGN and STEP.
   E.g., for EXPR a.b[i] + 4B, BASE is a, and OFFSET is the overall offset  
   'a.b[i] + 4B' from a (can be an expression), MISALIGN is an OFFSET 
   instantiated with initial_conditions of access_functions of variables, 
   and STEP is the evolution of the DR_REF in this loop.
   
   Function get_inner_reference is used for the above in case of ARRAY_REF and
   COMPONENT_REF.

   The structure of the function is as follows:
   Part 1:
   Case 1. For handled_component_p refs 
          1.1 build data-reference structure for MEMREF
          1.2 call get_inner_reference
            1.2.1 analyze offset expr received from get_inner_reference
          (fall through with BASE)
   Case 2. For declarations 
          2.1 set MEMTAG
   Case 3. For INDIRECT_REFs 
          3.1 build data-reference structure for MEMREF
	  3.2 analyze evolution and initial condition of MEMREF
	  3.3 set data-reference structure for MEMREF
          3.4 call address_analysis to analyze INIT of the access function
	  3.5 extract memory tag

   Part 2:
   Combine the results of object and address analysis to calculate 
   INITIAL_OFFSET, STEP and misalignment info.   

   Input:
   MEMREF - the memory reference that is being analyzed
   STMT - the statement that contains MEMREF
   IS_READ - TRUE if STMT reads from MEMREF, FALSE if writes to MEMREF
   
   Output:
   BASE_ADDRESS (returned value) - the base address of the data reference MEMREF
                                   E.g, if MEMREF is a.b[k].c[i][j] the returned
			           base is &a.
   DR - data_reference struct for MEMREF
   INITIAL_OFFSET - initial offset of MEMREF from BASE (an expression)
   MISALIGN - offset of MEMREF from BASE in bytes (a constant) modulo alignment of 
              ALIGNMENT or NULL_TREE if the computation is impossible
   ALIGNED_TO - maximum alignment of EXPR or NULL_TREE if MISALIGN can be 
                calculated (doesn't depend on variables)
   STEP - evolution of the DR_REF in the loop
   MEMTAG - memory tag for aliasing purposes
   PTR_INFO - NULL or points-to aliasing info from a pointer SSA_NAME
   SUBVARS - Sub-variables of the variable

   If the analysis of MEMREF evolution in the loop fails, NULL_TREE is returned, 
   but DR can be created anyway.
   
*/
 
static tree
object_analysis (tree memref, tree stmt, bool is_read, 
		 struct data_reference **dr, tree *offset, tree *misalign, 
		 tree *aligned_to, tree *step, tree *memtag,
		 struct ptr_info_def **ptr_info, subvar_t *subvars)
{
  tree base = NULL_TREE, base_address = NULL_TREE;
  tree object_offset = ssize_int (0), object_misalign = ssize_int (0);
  tree object_step = ssize_int (0), address_step = ssize_int (0);
  tree address_offset = ssize_int (0), address_misalign = ssize_int (0);
  HOST_WIDE_INT pbitsize, pbitpos;
  tree poffset, bit_pos_in_bytes;
  enum machine_mode pmode;
  int punsignedp, pvolatilep;
  tree ptr_step = ssize_int (0), ptr_init = NULL_TREE;
  struct loop *loop = loop_containing_stmt (stmt);
  struct data_reference *ptr_dr = NULL;
  tree object_aligned_to = NULL_TREE, address_aligned_to = NULL_TREE;
  tree comp_ref = NULL_TREE;

 *ptr_info = NULL;

  /* Part 1:  */
  /* Case 1. handled_component_p refs.  */
  if (handled_component_p (memref))
    {
      /* 1.1 build data-reference structure for MEMREF.  */
      if (!(*dr))
	{ 
	  if (TREE_CODE (memref) == ARRAY_REF)
	    *dr = analyze_array (stmt, memref, is_read);	  
	  else if (TREE_CODE (memref) == COMPONENT_REF)
	    comp_ref = memref;
	  else  
	    {
	      if (dump_file && (dump_flags & TDF_DETAILS))
		{
		  fprintf (dump_file, "\ndata-ref of unsupported type ");
		  print_generic_expr (dump_file, memref, TDF_SLIM);
		  fprintf (dump_file, "\n");
		}
	      return NULL_TREE;
	    }
	}

      /* 1.2 call get_inner_reference.  */
      /* Find the base and the offset from it.  */
      base = get_inner_reference (memref, &pbitsize, &pbitpos, &poffset,
				  &pmode, &punsignedp, &pvolatilep, false);
      if (!base)
	{
	  if (dump_file && (dump_flags & TDF_DETAILS))
	    {
	      fprintf (dump_file, "\nfailed to get inner ref for ");
	      print_generic_expr (dump_file, memref, TDF_SLIM);
	      fprintf (dump_file, "\n");
	    }	  
	  return NULL_TREE;
	}

      /* 1.2.1 analyze offset expr received from get_inner_reference.  */
      if (poffset 
	  && !analyze_offset_expr (poffset, loop, &object_offset, 
				   &object_misalign, &object_aligned_to,
				   &object_step))
	{
	  if (dump_file && (dump_flags & TDF_DETAILS))
	    {
	      fprintf (dump_file, "\nfailed to compute offset or step for ");
	      print_generic_expr (dump_file, memref, TDF_SLIM);
	      fprintf (dump_file, "\n");
	    }
	  return NULL_TREE;
	}

      /* Add bit position to OFFSET and MISALIGN.  */

      bit_pos_in_bytes = ssize_int (pbitpos/BITS_PER_UNIT);
      /* Check that there is no remainder in bits.  */
      if (pbitpos%BITS_PER_UNIT)
	{
	  if (dump_file && (dump_flags & TDF_DETAILS))
	    fprintf (dump_file, "\nbit offset alignment.\n");
	  return NULL_TREE;
	}
      object_offset = size_binop (PLUS_EXPR, bit_pos_in_bytes, object_offset);     
      if (object_misalign) 
	object_misalign = size_binop (PLUS_EXPR, object_misalign, 
				      bit_pos_in_bytes); 
      
      memref = base; /* To continue analysis of BASE.  */
      /* fall through  */
    }
  
  /*  Part 1: Case 2. Declarations.  */ 
  if (DECL_P (memref))
    {
      /* We expect to get a decl only if we already have a DR, or with 
	 COMPONENT_REFs of type 'a[i].b'.  */
      if (!(*dr))
	{
	  if (comp_ref && TREE_CODE (TREE_OPERAND (comp_ref, 0)) == ARRAY_REF)
	    {
	      *dr = analyze_array (stmt, TREE_OPERAND (comp_ref, 0), is_read);	      	      
	      if (DR_NUM_DIMENSIONS (*dr) != 1)
		{
		  if (dump_file && (dump_flags & TDF_DETAILS))
		    {
		      fprintf (dump_file, "\n multidimensional component ref ");
		      print_generic_expr (dump_file, comp_ref, TDF_SLIM);
		      fprintf (dump_file, "\n");
		    }
		  return NULL_TREE;
		}
	    }
	  else 
	    {
	      if (dump_file && (dump_flags & TDF_DETAILS))
		{
		  fprintf (dump_file, "\nunhandled decl ");
		  print_generic_expr (dump_file, memref, TDF_SLIM);
		  fprintf (dump_file, "\n");
		}
	      return NULL_TREE;
	    }
	}

      /* TODO: if during the analysis of INDIRECT_REF we get to an object, put 
	 the object in BASE_OBJECT field if we can prove that this is O.K., 
	 i.e., the data-ref access is bounded by the bounds of the BASE_OBJECT.
	 (e.g., if the object is an array base 'a', where 'a[N]', we must prove
	 that every access with 'p' (the original INDIRECT_REF based on '&a')
	 in the loop is within the array boundaries - from a[0] to a[N-1]).
	 Otherwise, our alias analysis can be incorrect.
	 Even if an access function based on BASE_OBJECT can't be build, update
	 BASE_OBJECT field to enable us to prove that two data-refs are 
	 different (without access function, distance analysis is impossible).
      */
     if (SSA_VAR_P (memref) && var_can_have_subvars (memref))	
	*subvars = get_subvars_for_var (memref);
      base_address = build_fold_addr_expr (memref);
      /* 2.1 set MEMTAG.  */
      *memtag = memref;
    }

  /* Part 1:  Case 3. INDIRECT_REFs.  */
  else if (TREE_CODE (memref) == INDIRECT_REF)
    {
      tree ptr_ref = TREE_OPERAND (memref, 0);
      if (TREE_CODE (ptr_ref) == SSA_NAME)
        *ptr_info = SSA_NAME_PTR_INFO (ptr_ref);

      /* 3.1 build data-reference structure for MEMREF.  */
      ptr_dr = analyze_indirect_ref (stmt, memref, is_read);
      if (!ptr_dr)
	{
	  if (dump_file && (dump_flags & TDF_DETAILS))
	    {
	      fprintf (dump_file, "\nfailed to create dr for ");
	      print_generic_expr (dump_file, memref, TDF_SLIM);
	      fprintf (dump_file, "\n");
	    }	
	  return NULL_TREE;      
	}

      /* 3.2 analyze evolution and initial condition of MEMREF.  */
      ptr_step = DR_STEP (ptr_dr);
      ptr_init = DR_BASE_ADDRESS (ptr_dr);
      if (!ptr_init || !ptr_step || !POINTER_TYPE_P (TREE_TYPE (ptr_init)))
	{
	  *dr = (*dr) ? *dr : ptr_dr;
	  if (dump_file && (dump_flags & TDF_DETAILS))
	    {
	      fprintf (dump_file, "\nbad pointer access ");
	      print_generic_expr (dump_file, memref, TDF_SLIM);
	      fprintf (dump_file, "\n");
	    }
	  return NULL_TREE;
	}

      if (integer_zerop (ptr_step) && !(*dr))
	{
	  if (dump_file && (dump_flags & TDF_DETAILS)) 
	    fprintf (dump_file, "\nptr is loop invariant.\n");	
	  *dr = ptr_dr;
	  return NULL_TREE;
	
	  /* If there exists DR for MEMREF, we are analyzing the base of
	     handled component (PTR_INIT), which not necessary has evolution in 
	     the loop.  */
	}
      object_step = size_binop (PLUS_EXPR, object_step, ptr_step);

      /* 3.3 set data-reference structure for MEMREF.  */
      if (!*dr)
        *dr = ptr_dr;

      /* 3.4 call address_analysis to analyze INIT of the access 
	 function.  */
      base_address = address_analysis (ptr_init, stmt, is_read, *dr, 
				       &address_offset, &address_misalign, 
				       &address_aligned_to, &address_step);
      if (!base_address)
	{
	  if (dump_file && (dump_flags & TDF_DETAILS))
	    {
	      fprintf (dump_file, "\nfailed to analyze address ");
	      print_generic_expr (dump_file, ptr_init, TDF_SLIM);
	      fprintf (dump_file, "\n");
	    }
	  return NULL_TREE;
	}

      /* 3.5 extract memory tag.  */
      switch (TREE_CODE (base_address))
	{
	case SSA_NAME:
	  *memtag = get_var_ann (SSA_NAME_VAR (base_address))->symbol_mem_tag;
	  if (!(*memtag) && TREE_CODE (TREE_OPERAND (memref, 0)) == SSA_NAME)
	    *memtag = get_var_ann (
		      SSA_NAME_VAR (TREE_OPERAND (memref, 0)))->symbol_mem_tag;
	  break;
	case ADDR_EXPR:
	  *memtag = TREE_OPERAND (base_address, 0);
	  break;
	default:
	  if (dump_file && (dump_flags & TDF_DETAILS))
	    {
	      fprintf (dump_file, "\nno memtag for "); 
	      print_generic_expr (dump_file, memref, TDF_SLIM);
	      fprintf (dump_file, "\n");
	    }
	  *memtag = NULL_TREE;
	  break;
	}
    }      
    
  if (!base_address)
    {
      /* MEMREF cannot be analyzed.  */
      if (dump_file && (dump_flags & TDF_DETAILS))
	{
	  fprintf (dump_file, "\ndata-ref of unsupported type ");
	  print_generic_expr (dump_file, memref, TDF_SLIM);
	  fprintf (dump_file, "\n");
	}
      return NULL_TREE;
    }

  if (comp_ref)
    DR_REF (*dr) = comp_ref;

  if (SSA_VAR_P (*memtag) && var_can_have_subvars (*memtag))
    *subvars = get_subvars_for_var (*memtag);
	
  /* Part 2: Combine the results of object and address analysis to calculate 
     INITIAL_OFFSET, STEP and misalignment info.  */
  *offset = size_binop (PLUS_EXPR, object_offset, address_offset);

  if ((!object_misalign && !object_aligned_to)
      || (!address_misalign && !address_aligned_to))
    {
      *misalign = NULL_TREE;
      *aligned_to = NULL_TREE;
    }  
  else 
    {
      if (object_misalign && address_misalign)
	*misalign = size_binop (PLUS_EXPR, object_misalign, address_misalign);
      else
	*misalign = object_misalign ? object_misalign : address_misalign;
      if (object_aligned_to && address_aligned_to)
	*aligned_to = size_binop (MIN_EXPR, object_aligned_to, 
				  address_aligned_to);
      else
	*aligned_to = object_aligned_to ? 
	  object_aligned_to : address_aligned_to; 
    }
  *step = size_binop (PLUS_EXPR, object_step, address_step); 

  return base_address;
}

/* Function analyze_offset.
   
   Extract INVARIANT and CONSTANT parts from OFFSET. 

*/
static bool 
analyze_offset (tree offset, tree *invariant, tree *constant)
{
  tree op0, op1, constant_0, constant_1, invariant_0, invariant_1;
  enum tree_code code = TREE_CODE (offset);

  *invariant = NULL_TREE;
  *constant = NULL_TREE;

  /* Not PLUS/MINUS expression - recursion stop condition.  */
  if (code != PLUS_EXPR && code != MINUS_EXPR)
    {
      if (TREE_CODE (offset) == INTEGER_CST)
	*constant = offset;
      else
	*invariant = offset;
      return true;
    }

  op0 = TREE_OPERAND (offset, 0);
  op1 = TREE_OPERAND (offset, 1);

  /* Recursive call with the operands.  */
  if (!analyze_offset (op0, &invariant_0, &constant_0)
      || !analyze_offset (op1, &invariant_1, &constant_1))
    return false;

  /* Combine the results. Add negation to the subtrahend in case of
     subtraction.  */
  if (constant_0 && constant_1)
    return false;
  *constant = constant_0 ? constant_0 : constant_1;
  if (code == MINUS_EXPR && constant_1)
    *constant = fold_build1 (NEGATE_EXPR, TREE_TYPE (*constant), *constant);

  if (invariant_0 && invariant_1)
    *invariant = 
      fold_build2 (code, TREE_TYPE (invariant_0), invariant_0, invariant_1);
  else
    {
      *invariant = invariant_0 ? invariant_0 : invariant_1;
      if (code == MINUS_EXPR && invariant_1)
        *invariant = 
           fold_build1 (NEGATE_EXPR, TREE_TYPE (*invariant), *invariant);
    }
  return true;
}

/* Free the memory used by the data reference DR.  */

static void
free_data_ref (data_reference_p dr)
{
  DR_FREE_ACCESS_FNS (dr);
  free (dr);
}

/* Function create_data_ref.
   
   Create a data-reference structure for MEMREF. Set its DR_BASE_ADDRESS,
   DR_OFFSET, DR_INIT, DR_STEP, DR_OFFSET_MISALIGNMENT, DR_ALIGNED_TO,
   DR_MEMTAG, and DR_POINTSTO_INFO fields. 

   Input:
   MEMREF - the memory reference that is being analyzed
   STMT - the statement that contains MEMREF
   IS_READ - TRUE if STMT reads from MEMREF, FALSE if writes to MEMREF

   Output:
   DR (returned value) - data_reference struct for MEMREF
*/

static struct data_reference *
create_data_ref (tree memref, tree stmt, bool is_read)
{
  struct data_reference *dr = NULL;
  tree base_address, offset, step, misalign, memtag;
  struct loop *loop = loop_containing_stmt (stmt);
  tree invariant = NULL_TREE, constant = NULL_TREE;
  tree type_size, init_cond;
  struct ptr_info_def *ptr_info;
  subvar_t subvars = NULL;
  tree aligned_to, type = NULL_TREE, orig_offset;

  if (!memref)
    return NULL;

  base_address = object_analysis (memref, stmt, is_read, &dr, &offset, 
				  &misalign, &aligned_to, &step, &memtag, 
				  &ptr_info, &subvars);
  if (!dr || !base_address)
    {
      if (dump_file && (dump_flags & TDF_DETAILS))
	{
	  fprintf (dump_file, "\ncreate_data_ref: failed to create a dr for ");
	  print_generic_expr (dump_file, memref, TDF_SLIM);
	  fprintf (dump_file, "\n");
	}
      return NULL;
    }

  DR_BASE_ADDRESS (dr) = base_address;
  DR_OFFSET (dr) = offset;
  DR_INIT (dr) = ssize_int (0);
  DR_STEP (dr) = step;
  DR_OFFSET_MISALIGNMENT (dr) = misalign;
  DR_ALIGNED_TO (dr) = aligned_to;
  DR_MEMTAG (dr) = memtag;
  DR_PTR_INFO (dr) = ptr_info;
  DR_SUBVARS (dr) = subvars;
  
  type_size = fold_convert (ssizetype, TYPE_SIZE_UNIT (TREE_TYPE (DR_REF (dr))));

  /* Extract CONSTANT and INVARIANT from OFFSET.  */
  /* Remove cast from OFFSET and restore it for INVARIANT part.  */
  orig_offset = offset;
  STRIP_NOPS (offset);
  if (offset != orig_offset)
    type = TREE_TYPE (orig_offset);
  if (!analyze_offset (offset, &invariant, &constant))
    {
      if (dump_file && (dump_flags & TDF_DETAILS))
        {
          fprintf (dump_file, "\ncreate_data_ref: failed to analyze dr's");
          fprintf (dump_file, " offset for ");
          print_generic_expr (dump_file, memref, TDF_SLIM);
          fprintf (dump_file, "\n");
        }
      return NULL;
    }
  if (type && invariant)
    invariant = fold_convert (type, invariant);

  /* Put CONSTANT part of OFFSET in DR_INIT and INVARIANT in DR_OFFSET field
     of DR.  */
  if (constant)
    {
      DR_INIT (dr) = fold_convert (ssizetype, constant);
      init_cond = fold_build2 (TRUNC_DIV_EXPR, TREE_TYPE (constant), 
			       constant, type_size);
    }
  else
    DR_INIT (dr) = init_cond = ssize_int (0);

  if (invariant)
    DR_OFFSET (dr) = invariant;
  else
    DR_OFFSET (dr) = ssize_int (0);

  /* Change the access function for INIDIRECT_REFs, according to 
     DR_BASE_ADDRESS.  Analyze OFFSET calculated in object_analysis. OFFSET is 
     an expression that can contain loop invariant expressions and constants.
     We put the constant part in the initial condition of the access function
     (for data dependence tests), and in DR_INIT of the data-ref. The loop
     invariant part is put in DR_OFFSET. 
     The evolution part of the access function is STEP calculated in
     object_analysis divided by the size of data type.
  */
  if (!DR_BASE_OBJECT (dr)
      || (TREE_CODE (memref) == COMPONENT_REF && DR_NUM_DIMENSIONS (dr) == 1))
    {
      tree access_fn;
      tree new_step;

      /* Update access function.  */
      access_fn = DR_ACCESS_FN (dr, 0);
      if (automatically_generated_chrec_p (access_fn))
	{
	  free_data_ref (dr);
	  return NULL;
	}

      new_step = size_binop (TRUNC_DIV_EXPR,  
			     fold_convert (ssizetype, step), type_size);

      init_cond = chrec_convert (chrec_type (access_fn), init_cond, stmt);
      new_step = chrec_convert (chrec_type (access_fn), new_step, stmt);
      if (automatically_generated_chrec_p (init_cond)
	  || automatically_generated_chrec_p (new_step))
	{
	  free_data_ref (dr);
	  return NULL;
	}
      access_fn = chrec_replace_initial_condition (access_fn, init_cond);
      access_fn = reset_evolution_in_loop (loop->num, access_fn, new_step);

      VEC_replace (tree, DR_ACCESS_FNS (dr), 0, access_fn);
    }

  if (dump_file && (dump_flags & TDF_DETAILS))
    {
      struct ptr_info_def *pi = DR_PTR_INFO (dr);

      fprintf (dump_file, "\nCreated dr for ");
      print_generic_expr (dump_file, memref, TDF_SLIM);
      fprintf (dump_file, "\n\tbase_address: ");
      print_generic_expr (dump_file, DR_BASE_ADDRESS (dr), TDF_SLIM);
      fprintf (dump_file, "\n\toffset from base address: ");
      print_generic_expr (dump_file, DR_OFFSET (dr), TDF_SLIM);
      fprintf (dump_file, "\n\tconstant offset from base address: ");
      print_generic_expr (dump_file, DR_INIT (dr), TDF_SLIM);
      fprintf (dump_file, "\n\tbase_object: ");
      print_generic_expr (dump_file, DR_BASE_OBJECT (dr), TDF_SLIM);
      fprintf (dump_file, "\n\tstep: ");
      print_generic_expr (dump_file, DR_STEP (dr), TDF_SLIM);
      fprintf (dump_file, "B\n\tmisalignment from base: ");
      print_generic_expr (dump_file, DR_OFFSET_MISALIGNMENT (dr), TDF_SLIM);
      if (DR_OFFSET_MISALIGNMENT (dr))
	fprintf (dump_file, "B");
      if (DR_ALIGNED_TO (dr))
	{
	  fprintf (dump_file, "\n\taligned to: ");
	  print_generic_expr (dump_file, DR_ALIGNED_TO (dr), TDF_SLIM);
	}
      fprintf (dump_file, "\n\tmemtag: ");
      print_generic_expr (dump_file, DR_MEMTAG (dr), TDF_SLIM);
      fprintf (dump_file, "\n");
      if (pi && pi->name_mem_tag)
        {
          fprintf (dump_file, "\n\tnametag: ");
          print_generic_expr (dump_file, pi->name_mem_tag, TDF_SLIM);
          fprintf (dump_file, "\n");
        }
    }  
  return dr;  
}


/* Returns true when all the functions of a tree_vec CHREC are the
   same.  */

static bool 
all_chrecs_equal_p (tree chrec)
{
  int j;

  for (j = 0; j < TREE_VEC_LENGTH (chrec) - 1; j++)
    if (!eq_evolutions_p (TREE_VEC_ELT (chrec, j),
			  TREE_VEC_ELT (chrec, j + 1)))
      return false;

  return true;
}

/* Determine for each subscript in the data dependence relation DDR
   the distance.  */

static void
compute_subscript_distance (struct data_dependence_relation *ddr)
{
  if (DDR_ARE_DEPENDENT (ddr) == NULL_TREE)
    {
      unsigned int i;
      
      for (i = 0; i < DDR_NUM_SUBSCRIPTS (ddr); i++)
 	{
 	  tree conflicts_a, conflicts_b, difference;
 	  struct subscript *subscript;
 	  
 	  subscript = DDR_SUBSCRIPT (ddr, i);
 	  conflicts_a = SUB_CONFLICTS_IN_A (subscript);
 	  conflicts_b = SUB_CONFLICTS_IN_B (subscript);

	  if (TREE_CODE (conflicts_a) == TREE_VEC)
	    {
	      if (!all_chrecs_equal_p (conflicts_a))
		{
		  SUB_DISTANCE (subscript) = chrec_dont_know;
		  return;
		}
	      else
		conflicts_a = TREE_VEC_ELT (conflicts_a, 0);
	    }

	  if (TREE_CODE (conflicts_b) == TREE_VEC)
	    {
	      if (!all_chrecs_equal_p (conflicts_b))
		{
		  SUB_DISTANCE (subscript) = chrec_dont_know;
		  return;
		}
	      else
		conflicts_b = TREE_VEC_ELT (conflicts_b, 0);
	    }

	  conflicts_b = chrec_convert (integer_type_node, conflicts_b,
				       NULL_TREE);
	  conflicts_a = chrec_convert (integer_type_node, conflicts_a,
				       NULL_TREE);
	  difference = chrec_fold_minus 
	    (integer_type_node, conflicts_b, conflicts_a);
 	  
 	  if (evolution_function_is_constant_p (difference))
 	    SUB_DISTANCE (subscript) = difference;
 	  
 	  else
 	    SUB_DISTANCE (subscript) = chrec_dont_know;
 	}
    }
}

/* Initialize a data dependence relation between data accesses A and
   B.  NB_LOOPS is the number of loops surrounding the references: the
   size of the classic distance/direction vectors.  */

static struct data_dependence_relation *
initialize_data_dependence_relation (struct data_reference *a, 
				     struct data_reference *b,
 				     VEC (loop_p, heap) *loop_nest)
{
  struct data_dependence_relation *res;
  bool differ_p, known_dependence;
  unsigned int i;
  
  res = XNEW (struct data_dependence_relation);
  DDR_A (res) = a;
  DDR_B (res) = b;
  DDR_LOOP_NEST (res) = NULL;

  if (a == NULL || b == NULL)
    {
      DDR_ARE_DEPENDENT (res) = chrec_dont_know;    
      return res;
    }   

  /* When A and B are arrays and their dimensions differ, we directly
     initialize the relation to "there is no dependence": chrec_known.  */
  if (DR_BASE_OBJECT (a) && DR_BASE_OBJECT (b)
      && DR_NUM_DIMENSIONS (a) != DR_NUM_DIMENSIONS (b))
    {
      DDR_ARE_DEPENDENT (res) = chrec_known;
      return res;
    }

  if (DR_BASE_ADDRESS (a) && DR_BASE_ADDRESS (b))
    known_dependence = base_addr_differ_p (a, b, &differ_p);
  else 
    known_dependence = base_object_differ_p (a, b, &differ_p);

  if (!known_dependence)
    {
      /* Can't determine whether the data-refs access the same memory 
	 region.  */
      DDR_ARE_DEPENDENT (res) = chrec_dont_know;    
      return res;
    }

  if (differ_p)
    {
      DDR_ARE_DEPENDENT (res) = chrec_known;    
      return res;
    }
    
  DDR_AFFINE_P (res) = true;
  DDR_ARE_DEPENDENT (res) = NULL_TREE;
  DDR_SUBSCRIPTS (res) = VEC_alloc (subscript_p, heap, DR_NUM_DIMENSIONS (a));
  DDR_LOOP_NEST (res) = loop_nest;
  DDR_DIR_VECTS (res) = NULL;
  DDR_DIST_VECTS (res) = NULL;

  for (i = 0; i < DR_NUM_DIMENSIONS (a); i++)
    {
      struct subscript *subscript;
	  
      subscript = XNEW (struct subscript);
      SUB_CONFLICTS_IN_A (subscript) = chrec_dont_know;
      SUB_CONFLICTS_IN_B (subscript) = chrec_dont_know;
      SUB_LAST_CONFLICT (subscript) = chrec_dont_know;
      SUB_DISTANCE (subscript) = chrec_dont_know;
      VEC_safe_push (subscript_p, heap, DDR_SUBSCRIPTS (res), subscript);
    }

  return res;
}

/* Set DDR_ARE_DEPENDENT to CHREC and finalize the subscript overlap
   description.  */

static inline void
finalize_ddr_dependent (struct data_dependence_relation *ddr, 
			tree chrec)
{
  if (dump_file && (dump_flags & TDF_DETAILS))
    {
      fprintf (dump_file, "(dependence classified: ");
      print_generic_expr (dump_file, chrec, 0);
      fprintf (dump_file, ")\n");
    }

  DDR_ARE_DEPENDENT (ddr) = chrec;  
  VEC_free (subscript_p, heap, DDR_SUBSCRIPTS (ddr));
}

/* The dependence relation DDR cannot be represented by a distance
   vector.  */

static inline void
non_affine_dependence_relation (struct data_dependence_relation *ddr)
{
  if (dump_file && (dump_flags & TDF_DETAILS))
    fprintf (dump_file, "(Dependence relation cannot be represented by distance vector.) \n");

  DDR_AFFINE_P (ddr) = false;
}



/* This section contains the classic Banerjee tests.  */

/* Returns true iff CHREC_A and CHREC_B are not dependent on any index
   variables, i.e., if the ZIV (Zero Index Variable) test is true.  */

static inline bool
ziv_subscript_p (tree chrec_a, 
		 tree chrec_b)
{
  return (evolution_function_is_constant_p (chrec_a)
	  && evolution_function_is_constant_p (chrec_b));
}

/* Returns true iff CHREC_A and CHREC_B are dependent on an index
   variable, i.e., if the SIV (Single Index Variable) test is true.  */

static bool
siv_subscript_p (tree chrec_a,
		 tree chrec_b)
{
  if ((evolution_function_is_constant_p (chrec_a)
       && evolution_function_is_univariate_p (chrec_b))
      || (evolution_function_is_constant_p (chrec_b)
	  && evolution_function_is_univariate_p (chrec_a)))
    return true;
  
  if (evolution_function_is_univariate_p (chrec_a)
      && evolution_function_is_univariate_p (chrec_b))
    {
      switch (TREE_CODE (chrec_a))
	{
	case POLYNOMIAL_CHREC:
	  switch (TREE_CODE (chrec_b))
	    {
	    case POLYNOMIAL_CHREC:
	      if (CHREC_VARIABLE (chrec_a) != CHREC_VARIABLE (chrec_b))
		return false;
	      
	    default:
	      return true;
	    }
	  
	default:
	  return true;
	}
    }
  
  return false;
}

/* Analyze a ZIV (Zero Index Variable) subscript.  *OVERLAPS_A and
   *OVERLAPS_B are initialized to the functions that describe the
   relation between the elements accessed twice by CHREC_A and
   CHREC_B.  For k >= 0, the following property is verified:

   CHREC_A (*OVERLAPS_A (k)) = CHREC_B (*OVERLAPS_B (k)).  */

static void 
analyze_ziv_subscript (tree chrec_a, 
		       tree chrec_b, 
		       tree *overlaps_a,
		       tree *overlaps_b, 
		       tree *last_conflicts)
{
  tree difference;
  dependence_stats.num_ziv++;
  
  if (dump_file && (dump_flags & TDF_DETAILS))
    fprintf (dump_file, "(analyze_ziv_subscript \n");
  
  chrec_a = chrec_convert (integer_type_node, chrec_a, NULL_TREE);
  chrec_b = chrec_convert (integer_type_node, chrec_b, NULL_TREE);
  difference = chrec_fold_minus (integer_type_node, chrec_a, chrec_b);
  
  switch (TREE_CODE (difference))
    {
    case INTEGER_CST:
      if (integer_zerop (difference))
	{
	  /* The difference is equal to zero: the accessed index
	     overlaps for each iteration in the loop.  */
	  *overlaps_a = integer_zero_node;
	  *overlaps_b = integer_zero_node;
	  *last_conflicts = chrec_dont_know;
	  dependence_stats.num_ziv_dependent++;
	}
      else
	{
	  /* The accesses do not overlap.  */
	  *overlaps_a = chrec_known;
	  *overlaps_b = chrec_known;
	  *last_conflicts = integer_zero_node;
	  dependence_stats.num_ziv_independent++;
	}
      break;
      
    default:
      /* We're not sure whether the indexes overlap.  For the moment, 
	 conservatively answer "don't know".  */
      if (dump_file && (dump_flags & TDF_DETAILS))
	fprintf (dump_file, "ziv test failed: difference is non-integer.\n");

      *overlaps_a = chrec_dont_know;
      *overlaps_b = chrec_dont_know;
      *last_conflicts = chrec_dont_know;
      dependence_stats.num_ziv_unimplemented++;
      break;
    }
  
  if (dump_file && (dump_flags & TDF_DETAILS))
    fprintf (dump_file, ")\n");
}

/* Get the real or estimated number of iterations for LOOPNUM, whichever is
   available. Return the number of iterations as a tree, or NULL_TREE if
   we don't know.  */

static tree
get_number_of_iters_for_loop (int loopnum)
{
  tree numiter = number_of_iterations_in_loop (current_loops->parray[loopnum]);

  if (TREE_CODE (numiter) != INTEGER_CST)
    numiter = current_loops->parray[loopnum]->estimated_nb_iterations;
  if (chrec_contains_undetermined (numiter))
    return NULL_TREE;
  return numiter;
}
    
/* Analyze a SIV (Single Index Variable) subscript where CHREC_A is a
   constant, and CHREC_B is an affine function.  *OVERLAPS_A and
   *OVERLAPS_B are initialized to the functions that describe the
   relation between the elements accessed twice by CHREC_A and
   CHREC_B.  For k >= 0, the following property is verified:

   CHREC_A (*OVERLAPS_A (k)) = CHREC_B (*OVERLAPS_B (k)).  */

static void
analyze_siv_subscript_cst_affine (tree chrec_a, 
				  tree chrec_b,
				  tree *overlaps_a, 
				  tree *overlaps_b, 
				  tree *last_conflicts)
{
  bool value0, value1, value2;
  tree difference;

  chrec_a = chrec_convert (integer_type_node, chrec_a, NULL_TREE);
  chrec_b = chrec_convert (integer_type_node, chrec_b, NULL_TREE);
  difference = chrec_fold_minus 
    (integer_type_node, initial_condition (chrec_b), chrec_a);
  
  if (!chrec_is_positive (initial_condition (difference), &value0))
    {
      if (dump_file && (dump_flags & TDF_DETAILS))
	fprintf (dump_file, "siv test failed: chrec is not positive.\n"); 

      dependence_stats.num_siv_unimplemented++;
      *overlaps_a = chrec_dont_know;
      *overlaps_b = chrec_dont_know;
      *last_conflicts = chrec_dont_know;
      return;
    }
  else
    {
      if (value0 == false)
	{
	  if (!chrec_is_positive (CHREC_RIGHT (chrec_b), &value1))
	    {
	      if (dump_file && (dump_flags & TDF_DETAILS))
		fprintf (dump_file, "siv test failed: chrec not positive.\n");

	      *overlaps_a = chrec_dont_know;
	      *overlaps_b = chrec_dont_know;      
	      *last_conflicts = chrec_dont_know;
	      dependence_stats.num_siv_unimplemented++;
	      return;
	    }
	  else
	    {
	      if (value1 == true)
		{
		  /* Example:  
		     chrec_a = 12
		     chrec_b = {10, +, 1}
		  */
		  
		  if (tree_fold_divides_p (CHREC_RIGHT (chrec_b), difference))
		    {
		      tree numiter;
		      int loopnum = CHREC_VARIABLE (chrec_b);

		      *overlaps_a = integer_zero_node;
		      *overlaps_b = fold_build2 (EXACT_DIV_EXPR, integer_type_node,
						 fold_build1 (ABS_EXPR,
							      integer_type_node,
							      difference),
						 CHREC_RIGHT (chrec_b));
		      *last_conflicts = integer_one_node;
		      

		      /* Perform weak-zero siv test to see if overlap is
			 outside the loop bounds.  */
		      numiter = get_number_of_iters_for_loop (loopnum);

		      if (numiter != NULL_TREE
			  && TREE_CODE (*overlaps_b) == INTEGER_CST
			  && tree_int_cst_lt (numiter, *overlaps_b))
			{
			  *overlaps_a = chrec_known;
			  *overlaps_b = chrec_known;
			  *last_conflicts = integer_zero_node;
			  dependence_stats.num_siv_independent++;
			  return;
			}		
		      dependence_stats.num_siv_dependent++;
		      return;
		    }
		  
		  /* When the step does not divide the difference, there are
		     no overlaps.  */
		  else
		    {
		      *overlaps_a = chrec_known;
		      *overlaps_b = chrec_known;      
		      *last_conflicts = integer_zero_node;
		      dependence_stats.num_siv_independent++;
		      return;
		    }
		}
	      
	      else
		{
		  /* Example:  
		     chrec_a = 12
		     chrec_b = {10, +, -1}
		     
		     In this case, chrec_a will not overlap with chrec_b.  */
		  *overlaps_a = chrec_known;
		  *overlaps_b = chrec_known;
		  *last_conflicts = integer_zero_node;
		  dependence_stats.num_siv_independent++;
		  return;
		}
	    }
	}
      else 
	{
	  if (!chrec_is_positive (CHREC_RIGHT (chrec_b), &value2))
	    {
	      if (dump_file && (dump_flags & TDF_DETAILS))
		fprintf (dump_file, "siv test failed: chrec not positive.\n");

	      *overlaps_a = chrec_dont_know;
	      *overlaps_b = chrec_dont_know;      
	      *last_conflicts = chrec_dont_know;
	      dependence_stats.num_siv_unimplemented++;
	      return;
	    }
	  else
	    {
	      if (value2 == false)
		{
		  /* Example:  
		     chrec_a = 3
		     chrec_b = {10, +, -1}
		  */
		  if (tree_fold_divides_p (CHREC_RIGHT (chrec_b), difference))
		    {
		      tree numiter;
		      int loopnum = CHREC_VARIABLE (chrec_b);

		      *overlaps_a = integer_zero_node;
		      *overlaps_b = fold_build2 (EXACT_DIV_EXPR,
				      		 integer_type_node, difference, 
						 CHREC_RIGHT (chrec_b));
		      *last_conflicts = integer_one_node;

		      /* Perform weak-zero siv test to see if overlap is
			 outside the loop bounds.  */
		      numiter = get_number_of_iters_for_loop (loopnum);

		      if (numiter != NULL_TREE
			  && TREE_CODE (*overlaps_b) == INTEGER_CST
			  && tree_int_cst_lt (numiter, *overlaps_b))
			{
			  *overlaps_a = chrec_known;
			  *overlaps_b = chrec_known;
			  *last_conflicts = integer_zero_node;
			  dependence_stats.num_siv_independent++;
			  return;
			}	
		      dependence_stats.num_siv_dependent++;
		      return;
		    }
		  
		  /* When the step does not divide the difference, there
		     are no overlaps.  */
		  else
		    {
		      *overlaps_a = chrec_known;
		      *overlaps_b = chrec_known;      
		      *last_conflicts = integer_zero_node;
		      dependence_stats.num_siv_independent++;
		      return;
		    }
		}
	      else
		{
		  /* Example:  
		     chrec_a = 3  
		     chrec_b = {4, +, 1}
		 
		     In this case, chrec_a will not overlap with chrec_b.  */
		  *overlaps_a = chrec_known;
		  *overlaps_b = chrec_known;
		  *last_conflicts = integer_zero_node;
		  dependence_stats.num_siv_independent++;
		  return;
		}
	    }
	}
    }
}

/* Helper recursive function for initializing the matrix A.  Returns
   the initial value of CHREC.  */

static int
initialize_matrix_A (lambda_matrix A, tree chrec, unsigned index, int mult)
{
  gcc_assert (chrec);

  if (TREE_CODE (chrec) != POLYNOMIAL_CHREC)
    return int_cst_value (chrec);

  A[index][0] = mult * int_cst_value (CHREC_RIGHT (chrec));
  return initialize_matrix_A (A, CHREC_LEFT (chrec), index + 1, mult);
}

#define FLOOR_DIV(x,y) ((x) / (y))

/* Solves the special case of the Diophantine equation: 
   | {0, +, STEP_A}_x (OVERLAPS_A) = {0, +, STEP_B}_y (OVERLAPS_B)

   Computes the descriptions OVERLAPS_A and OVERLAPS_B.  NITER is the
   number of iterations that loops X and Y run.  The overlaps will be
   constructed as evolutions in dimension DIM.  */

static void
compute_overlap_steps_for_affine_univar (int niter, int step_a, int step_b, 
					 tree *overlaps_a, tree *overlaps_b, 
					 tree *last_conflicts, int dim)
{
  if (((step_a > 0 && step_b > 0)
       || (step_a < 0 && step_b < 0)))
    {
      int step_overlaps_a, step_overlaps_b;
      int gcd_steps_a_b, last_conflict, tau2;

      gcd_steps_a_b = gcd (step_a, step_b);
      step_overlaps_a = step_b / gcd_steps_a_b;
      step_overlaps_b = step_a / gcd_steps_a_b;

      tau2 = FLOOR_DIV (niter, step_overlaps_a);
      tau2 = MIN (tau2, FLOOR_DIV (niter, step_overlaps_b));
      last_conflict = tau2;

      *overlaps_a = build_polynomial_chrec
	(dim, integer_zero_node,
	 build_int_cst (NULL_TREE, step_overlaps_a));
      *overlaps_b = build_polynomial_chrec
	(dim, integer_zero_node,
	 build_int_cst (NULL_TREE, step_overlaps_b));
      *last_conflicts = build_int_cst (NULL_TREE, last_conflict);
    }

  else
    {
      *overlaps_a = integer_zero_node;
      *overlaps_b = integer_zero_node;
      *last_conflicts = integer_zero_node;
    }
}


/* Solves the special case of a Diophantine equation where CHREC_A is
   an affine bivariate function, and CHREC_B is an affine univariate
   function.  For example, 

   | {{0, +, 1}_x, +, 1335}_y = {0, +, 1336}_z
   
   has the following overlapping functions: 

   | x (t, u, v) = {{0, +, 1336}_t, +, 1}_v
   | y (t, u, v) = {{0, +, 1336}_u, +, 1}_v
   | z (t, u, v) = {{{0, +, 1}_t, +, 1335}_u, +, 1}_v

   FORNOW: This is a specialized implementation for a case occurring in
   a common benchmark.  Implement the general algorithm.  */

static void
compute_overlap_steps_for_affine_1_2 (tree chrec_a, tree chrec_b, 
				      tree *overlaps_a, tree *overlaps_b, 
				      tree *last_conflicts)
{
  bool xz_p, yz_p, xyz_p;
  int step_x, step_y, step_z;
  int niter_x, niter_y, niter_z, niter;
  tree numiter_x, numiter_y, numiter_z;
  tree overlaps_a_xz, overlaps_b_xz, last_conflicts_xz;
  tree overlaps_a_yz, overlaps_b_yz, last_conflicts_yz;
  tree overlaps_a_xyz, overlaps_b_xyz, last_conflicts_xyz;

  step_x = int_cst_value (CHREC_RIGHT (CHREC_LEFT (chrec_a)));
  step_y = int_cst_value (CHREC_RIGHT (chrec_a));
  step_z = int_cst_value (CHREC_RIGHT (chrec_b));

  numiter_x = get_number_of_iters_for_loop (CHREC_VARIABLE (CHREC_LEFT (chrec_a)));
  numiter_y = get_number_of_iters_for_loop (CHREC_VARIABLE (chrec_a));
  numiter_z = get_number_of_iters_for_loop (CHREC_VARIABLE (chrec_b));
  
  if (numiter_x == NULL_TREE || numiter_y == NULL_TREE 
      || numiter_z == NULL_TREE)
    {
      if (dump_file && (dump_flags & TDF_DETAILS))
	fprintf (dump_file, "overlap steps test failed: no iteration counts.\n");
	   
      *overlaps_a = chrec_dont_know;
      *overlaps_b = chrec_dont_know;
      *last_conflicts = chrec_dont_know;
      return;
    }

  niter_x = int_cst_value (numiter_x);
  niter_y = int_cst_value (numiter_y);
  niter_z = int_cst_value (numiter_z);

  niter = MIN (niter_x, niter_z);
  compute_overlap_steps_for_affine_univar (niter, step_x, step_z,
					   &overlaps_a_xz,
					   &overlaps_b_xz,
					   &last_conflicts_xz, 1);
  niter = MIN (niter_y, niter_z);
  compute_overlap_steps_for_affine_univar (niter, step_y, step_z,
					   &overlaps_a_yz,
					   &overlaps_b_yz,
					   &last_conflicts_yz, 2);
  niter = MIN (niter_x, niter_z);
  niter = MIN (niter_y, niter);
  compute_overlap_steps_for_affine_univar (niter, step_x + step_y, step_z,
					   &overlaps_a_xyz,
					   &overlaps_b_xyz,
					   &last_conflicts_xyz, 3);

  xz_p = !integer_zerop (last_conflicts_xz);
  yz_p = !integer_zerop (last_conflicts_yz);
  xyz_p = !integer_zerop (last_conflicts_xyz);

  if (xz_p || yz_p || xyz_p)
    {
      *overlaps_a = make_tree_vec (2);
      TREE_VEC_ELT (*overlaps_a, 0) = integer_zero_node;
      TREE_VEC_ELT (*overlaps_a, 1) = integer_zero_node;
      *overlaps_b = integer_zero_node;
      if (xz_p)
	{
	  tree t0 = chrec_convert (integer_type_node, 
				   TREE_VEC_ELT (*overlaps_a, 0), NULL_TREE);
	  tree t1 = chrec_convert (integer_type_node, overlaps_a_xz,
				   NULL_TREE);
	  tree t2 = chrec_convert (integer_type_node, *overlaps_b,
				   NULL_TREE);
	  tree t3 = chrec_convert (integer_type_node, overlaps_b_xz,
				   NULL_TREE);

	  TREE_VEC_ELT (*overlaps_a, 0) = chrec_fold_plus (integer_type_node,
							   t0, t1);
	  *overlaps_b = chrec_fold_plus (integer_type_node, t2, t3);
	  *last_conflicts = last_conflicts_xz;
	}
      if (yz_p)
	{
	  tree t0 = chrec_convert (integer_type_node,
				   TREE_VEC_ELT (*overlaps_a, 1), NULL_TREE);
	  tree t1 = chrec_convert (integer_type_node, overlaps_a_yz, NULL_TREE);
	  tree t2 = chrec_convert (integer_type_node, *overlaps_b, NULL_TREE);
	  tree t3 = chrec_convert (integer_type_node, overlaps_b_yz, NULL_TREE);

	  TREE_VEC_ELT (*overlaps_a, 1) = chrec_fold_plus (integer_type_node,
							   t0, t1);
	  *overlaps_b = chrec_fold_plus (integer_type_node, t2, t3);
	  *last_conflicts = last_conflicts_yz;
	}
      if (xyz_p)
	{
	  tree t0 = chrec_convert (integer_type_node,
				   TREE_VEC_ELT (*overlaps_a, 0), NULL_TREE);
	  tree t1 = chrec_convert (integer_type_node, overlaps_a_xyz,
				   NULL_TREE);
	  tree t2 = chrec_convert (integer_type_node,
				   TREE_VEC_ELT (*overlaps_a, 1), NULL_TREE);
	  tree t3 = chrec_convert (integer_type_node, overlaps_a_xyz,
				   NULL_TREE);
	  tree t4 = chrec_convert (integer_type_node, *overlaps_b, NULL_TREE);
	  tree t5 = chrec_convert (integer_type_node, overlaps_b_xyz,
				   NULL_TREE);

	  TREE_VEC_ELT (*overlaps_a, 0) = chrec_fold_plus (integer_type_node,
							   t0, t1);
	  TREE_VEC_ELT (*overlaps_a, 1) = chrec_fold_plus (integer_type_node,
							   t2, t3);
	  *overlaps_b = chrec_fold_plus (integer_type_node, t4, t5);
	  *last_conflicts = last_conflicts_xyz;
	}
    }
  else
    {
      *overlaps_a = integer_zero_node;
      *overlaps_b = integer_zero_node;
      *last_conflicts = integer_zero_node;
    }
}

/* Determines the overlapping elements due to accesses CHREC_A and
   CHREC_B, that are affine functions.  This function cannot handle
   symbolic evolution functions, ie. when initial conditions are
   parameters, because it uses lambda matrices of integers.  */

static void
analyze_subscript_affine_affine (tree chrec_a, 
				 tree chrec_b,
				 tree *overlaps_a, 
				 tree *overlaps_b, 
				 tree *last_conflicts)
{
  unsigned nb_vars_a, nb_vars_b, dim;
  int init_a, init_b, gamma, gcd_alpha_beta;
  int tau1, tau2;
  lambda_matrix A, U, S;

  if (eq_evolutions_p (chrec_a, chrec_b))
    {
      /* The accessed index overlaps for each iteration in the
	 loop.  */
      *overlaps_a = integer_zero_node;
      *overlaps_b = integer_zero_node;
      *last_conflicts = chrec_dont_know;
      return;
    }
  if (dump_file && (dump_flags & TDF_DETAILS))
    fprintf (dump_file, "(analyze_subscript_affine_affine \n");
  
  /* For determining the initial intersection, we have to solve a
     Diophantine equation.  This is the most time consuming part.
     
     For answering to the question: "Is there a dependence?" we have
     to prove that there exists a solution to the Diophantine
     equation, and that the solution is in the iteration domain,
     i.e. the solution is positive or zero, and that the solution
     happens before the upper bound loop.nb_iterations.  Otherwise
     there is no dependence.  This function outputs a description of
     the iterations that hold the intersections.  */

  nb_vars_a = nb_vars_in_chrec (chrec_a);
  nb_vars_b = nb_vars_in_chrec (chrec_b);

  dim = nb_vars_a + nb_vars_b;
  U = lambda_matrix_new (dim, dim);
  A = lambda_matrix_new (dim, 1);
  S = lambda_matrix_new (dim, 1);

  init_a = initialize_matrix_A (A, chrec_a, 0, 1);
  init_b = initialize_matrix_A (A, chrec_b, nb_vars_a, -1);
  gamma = init_b - init_a;

  /* Don't do all the hard work of solving the Diophantine equation
     when we already know the solution: for example, 
     | {3, +, 1}_1
     | {3, +, 4}_2
     | gamma = 3 - 3 = 0.
     Then the first overlap occurs during the first iterations: 
     | {3, +, 1}_1 ({0, +, 4}_x) = {3, +, 4}_2 ({0, +, 1}_x)
  */
  if (gamma == 0)
    {
      if (nb_vars_a == 1 && nb_vars_b == 1)
	{
	  int step_a, step_b;
	  int niter, niter_a, niter_b;
	  tree numiter_a, numiter_b;

	  numiter_a = get_number_of_iters_for_loop (CHREC_VARIABLE (chrec_a));
	  numiter_b = get_number_of_iters_for_loop (CHREC_VARIABLE (chrec_b));
	  if (numiter_a == NULL_TREE || numiter_b == NULL_TREE)
	    {
	      if (dump_file && (dump_flags & TDF_DETAILS))
		fprintf (dump_file, "affine-affine test failed: missing iteration counts.\n");
	      *overlaps_a = chrec_dont_know;
	      *overlaps_b = chrec_dont_know;
	      *last_conflicts = chrec_dont_know;
	      goto end_analyze_subs_aa;
	    }

	  niter_a = int_cst_value (numiter_a);
	  niter_b = int_cst_value (numiter_b);
	  niter = MIN (niter_a, niter_b);

	  step_a = int_cst_value (CHREC_RIGHT (chrec_a));
	  step_b = int_cst_value (CHREC_RIGHT (chrec_b));

	  compute_overlap_steps_for_affine_univar (niter, step_a, step_b, 
						   overlaps_a, overlaps_b, 
						   last_conflicts, 1);
	}

      else if (nb_vars_a == 2 && nb_vars_b == 1)
	compute_overlap_steps_for_affine_1_2
	  (chrec_a, chrec_b, overlaps_a, overlaps_b, last_conflicts);

      else if (nb_vars_a == 1 && nb_vars_b == 2)
	compute_overlap_steps_for_affine_1_2
	  (chrec_b, chrec_a, overlaps_b, overlaps_a, last_conflicts);

      else
	{
	  if (dump_file && (dump_flags & TDF_DETAILS))
	    fprintf (dump_file, "affine-affine test failed: too many variables.\n");
	  *overlaps_a = chrec_dont_know;
	  *overlaps_b = chrec_dont_know;
	  *last_conflicts = chrec_dont_know;
	}
      goto end_analyze_subs_aa;
    }

  /* U.A = S */
  lambda_matrix_right_hermite (A, dim, 1, S, U);

  if (S[0][0] < 0)
    {
      S[0][0] *= -1;
      lambda_matrix_row_negate (U, dim, 0);
    }
  gcd_alpha_beta = S[0][0];

  /* Something went wrong: for example in {1, +, 0}_5 vs. {0, +, 0}_5,
     but that is a quite strange case.  Instead of ICEing, answer
     don't know.  */
  if (gcd_alpha_beta == 0)
    {
      *overlaps_a = chrec_dont_know;
      *overlaps_b = chrec_dont_know;
      *last_conflicts = chrec_dont_know;
      goto end_analyze_subs_aa;
    }

  /* The classic "gcd-test".  */
  if (!int_divides_p (gcd_alpha_beta, gamma))
    {
      /* The "gcd-test" has determined that there is no integer
	 solution, i.e. there is no dependence.  */
      *overlaps_a = chrec_known;
      *overlaps_b = chrec_known;
      *last_conflicts = integer_zero_node;
    }

  /* Both access functions are univariate.  This includes SIV and MIV cases.  */
  else if (nb_vars_a == 1 && nb_vars_b == 1)
    {
      /* Both functions should have the same evolution sign.  */
      if (((A[0][0] > 0 && -A[1][0] > 0)
	   || (A[0][0] < 0 && -A[1][0] < 0)))
	{
	  /* The solutions are given by:
	     | 
	     | [GAMMA/GCD_ALPHA_BETA  t].[u11 u12]  = [x0]
	     |                           [u21 u22]    [y0]
	 
	     For a given integer t.  Using the following variables,
	 
	     | i0 = u11 * gamma / gcd_alpha_beta
	     | j0 = u12 * gamma / gcd_alpha_beta
	     | i1 = u21
	     | j1 = u22
	 
	     the solutions are:
	 
	     | x0 = i0 + i1 * t, 
	     | y0 = j0 + j1 * t.  */
      
	  int i0, j0, i1, j1;

	  /* X0 and Y0 are the first iterations for which there is a
	     dependence.  X0, Y0 are two solutions of the Diophantine
	     equation: chrec_a (X0) = chrec_b (Y0).  */
	  int x0, y0;
	  int niter, niter_a, niter_b;
	  tree numiter_a, numiter_b;

	  numiter_a = get_number_of_iters_for_loop (CHREC_VARIABLE (chrec_a));
	  numiter_b = get_number_of_iters_for_loop (CHREC_VARIABLE (chrec_b));

	  if (numiter_a == NULL_TREE || numiter_b == NULL_TREE)
	    {
	      if (dump_file && (dump_flags & TDF_DETAILS))
		fprintf (dump_file, "affine-affine test failed: missing iteration counts.\n");
	      *overlaps_a = chrec_dont_know;
	      *overlaps_b = chrec_dont_know;
	      *last_conflicts = chrec_dont_know;
	      goto end_analyze_subs_aa;
	    }

	  niter_a = int_cst_value (numiter_a);
	  niter_b = int_cst_value (numiter_b);
	  niter = MIN (niter_a, niter_b);

	  i0 = U[0][0] * gamma / gcd_alpha_beta;
	  j0 = U[0][1] * gamma / gcd_alpha_beta;
	  i1 = U[1][0];
	  j1 = U[1][1];

	  if ((i1 == 0 && i0 < 0)
	      || (j1 == 0 && j0 < 0))
	    {
	      /* There is no solution.  
		 FIXME: The case "i0 > nb_iterations, j0 > nb_iterations" 
		 falls in here, but for the moment we don't look at the 
		 upper bound of the iteration domain.  */
	      *overlaps_a = chrec_known;
	      *overlaps_b = chrec_known;
	      *last_conflicts = integer_zero_node;
	    }

	  else 
	    {
	      if (i1 > 0)
		{
		  tau1 = CEIL (-i0, i1);
		  tau2 = FLOOR_DIV (niter - i0, i1);

		  if (j1 > 0)
		    {
		      int last_conflict, min_multiple;
		      tau1 = MAX (tau1, CEIL (-j0, j1));
		      tau2 = MIN (tau2, FLOOR_DIV (niter - j0, j1));

		      x0 = i1 * tau1 + i0;
		      y0 = j1 * tau1 + j0;

		      /* At this point (x0, y0) is one of the
			 solutions to the Diophantine equation.  The
			 next step has to compute the smallest
			 positive solution: the first conflicts.  */
		      min_multiple = MIN (x0 / i1, y0 / j1);
		      x0 -= i1 * min_multiple;
		      y0 -= j1 * min_multiple;

		      tau1 = (x0 - i0)/i1;
		      last_conflict = tau2 - tau1;

		      /* If the overlap occurs outside of the bounds of the
			 loop, there is no dependence.  */
		      if (x0 > niter || y0  > niter)
			{
			  *overlaps_a = chrec_known;
			  *overlaps_b = chrec_known;
			  *last_conflicts = integer_zero_node;
			}
		      else
			{
			  *overlaps_a = build_polynomial_chrec
			    (1,
			     build_int_cst (NULL_TREE, x0),
			     build_int_cst (NULL_TREE, i1));
			  *overlaps_b = build_polynomial_chrec
			    (1,
			     build_int_cst (NULL_TREE, y0),
			     build_int_cst (NULL_TREE, j1));
			  *last_conflicts = build_int_cst (NULL_TREE, last_conflict);
			}
		    }
		  else
		    {
		      /* FIXME: For the moment, the upper bound of the
			 iteration domain for j is not checked.  */
		      if (dump_file && (dump_flags & TDF_DETAILS))
			fprintf (dump_file, "affine-affine test failed: unimplemented.\n");
		      *overlaps_a = chrec_dont_know;
		      *overlaps_b = chrec_dont_know;
		      *last_conflicts = chrec_dont_know;
		    }
		}
	  
	      else
		{
		  /* FIXME: For the moment, the upper bound of the
		     iteration domain for i is not checked.  */
		  if (dump_file && (dump_flags & TDF_DETAILS))
		    fprintf (dump_file, "affine-affine test failed: unimplemented.\n");
		  *overlaps_a = chrec_dont_know;
		  *overlaps_b = chrec_dont_know;
		  *last_conflicts = chrec_dont_know;
		}
	    }
	}
      else
	{
	  if (dump_file && (dump_flags & TDF_DETAILS))
	    fprintf (dump_file, "affine-affine test failed: unimplemented.\n");
	  *overlaps_a = chrec_dont_know;
	  *overlaps_b = chrec_dont_know;
	  *last_conflicts = chrec_dont_know;
	}
    }

  else
    {
      if (dump_file && (dump_flags & TDF_DETAILS))
	fprintf (dump_file, "affine-affine test failed: unimplemented.\n");
      *overlaps_a = chrec_dont_know;
      *overlaps_b = chrec_dont_know;
      *last_conflicts = chrec_dont_know;
    }

end_analyze_subs_aa:  
  if (dump_file && (dump_flags & TDF_DETAILS))
    {
      fprintf (dump_file, "  (overlaps_a = ");
      print_generic_expr (dump_file, *overlaps_a, 0);
      fprintf (dump_file, ")\n  (overlaps_b = ");
      print_generic_expr (dump_file, *overlaps_b, 0);
      fprintf (dump_file, ")\n");
      fprintf (dump_file, ")\n");
    }
}

/* Returns true when analyze_subscript_affine_affine can be used for
   determining the dependence relation between chrec_a and chrec_b,
   that contain symbols.  This function modifies chrec_a and chrec_b
   such that the analysis result is the same, and such that they don't
   contain symbols, and then can safely be passed to the analyzer.  

   Example: The analysis of the following tuples of evolutions produce
   the same results: {x+1, +, 1}_1 vs. {x+3, +, 1}_1, and {-2, +, 1}_1
   vs. {0, +, 1}_1
   
   {x+1, +, 1}_1 ({2, +, 1}_1) = {x+3, +, 1}_1 ({0, +, 1}_1)
   {-2, +, 1}_1 ({2, +, 1}_1) = {0, +, 1}_1 ({0, +, 1}_1)
*/

static bool
can_use_analyze_subscript_affine_affine (tree *chrec_a, tree *chrec_b)
{
  tree diff, type, left_a, left_b, right_b;

  if (chrec_contains_symbols (CHREC_RIGHT (*chrec_a))
      || chrec_contains_symbols (CHREC_RIGHT (*chrec_b)))
    /* FIXME: For the moment not handled.  Might be refined later.  */
    return false;

  type = chrec_type (*chrec_a);
  left_a = CHREC_LEFT (*chrec_a);
  left_b = chrec_convert (type, CHREC_LEFT (*chrec_b), NULL_TREE);
  diff = chrec_fold_minus (type, left_a, left_b);

  if (!evolution_function_is_constant_p (diff))
    return false;

  if (dump_file && (dump_flags & TDF_DETAILS))
    fprintf (dump_file, "can_use_subscript_aff_aff_for_symbolic \n");

  *chrec_a = build_polynomial_chrec (CHREC_VARIABLE (*chrec_a), 
				     diff, CHREC_RIGHT (*chrec_a));
  right_b = chrec_convert (type, CHREC_RIGHT (*chrec_b), NULL_TREE);
  *chrec_b = build_polynomial_chrec (CHREC_VARIABLE (*chrec_b),
				     build_int_cst (type, 0),
				     right_b);
  return true;
}

/* Analyze a SIV (Single Index Variable) subscript.  *OVERLAPS_A and
   *OVERLAPS_B are initialized to the functions that describe the
   relation between the elements accessed twice by CHREC_A and
   CHREC_B.  For k >= 0, the following property is verified:

   CHREC_A (*OVERLAPS_A (k)) = CHREC_B (*OVERLAPS_B (k)).  */

static void
analyze_siv_subscript (tree chrec_a, 
		       tree chrec_b,
		       tree *overlaps_a, 
		       tree *overlaps_b, 
		       tree *last_conflicts)
{
  dependence_stats.num_siv++;
  
  if (dump_file && (dump_flags & TDF_DETAILS))
    fprintf (dump_file, "(analyze_siv_subscript \n");
  
  if (evolution_function_is_constant_p (chrec_a)
      && evolution_function_is_affine_p (chrec_b))
    analyze_siv_subscript_cst_affine (chrec_a, chrec_b, 
				      overlaps_a, overlaps_b, last_conflicts);
  
  else if (evolution_function_is_affine_p (chrec_a)
	   && evolution_function_is_constant_p (chrec_b))
    analyze_siv_subscript_cst_affine (chrec_b, chrec_a, 
				      overlaps_b, overlaps_a, last_conflicts);
  
  else if (evolution_function_is_affine_p (chrec_a)
	   && evolution_function_is_affine_p (chrec_b))
    {
      if (!chrec_contains_symbols (chrec_a)
	  && !chrec_contains_symbols (chrec_b))
	{
	  analyze_subscript_affine_affine (chrec_a, chrec_b, 
					   overlaps_a, overlaps_b, 
					   last_conflicts);

	  if (*overlaps_a == chrec_dont_know
	      || *overlaps_b == chrec_dont_know)
	    dependence_stats.num_siv_unimplemented++;
	  else if (*overlaps_a == chrec_known
		   || *overlaps_b == chrec_known)
	    dependence_stats.num_siv_independent++;
	  else
	    dependence_stats.num_siv_dependent++;
	}
      else if (can_use_analyze_subscript_affine_affine (&chrec_a, 
							&chrec_b))
	{
	  analyze_subscript_affine_affine (chrec_a, chrec_b, 
					   overlaps_a, overlaps_b, 
					   last_conflicts);
	  /* FIXME: The number of iterations is a symbolic expression.
	     Compute it properly.  */
	  *last_conflicts = chrec_dont_know;

	  if (*overlaps_a == chrec_dont_know
	      || *overlaps_b == chrec_dont_know)
	    dependence_stats.num_siv_unimplemented++;
	  else if (*overlaps_a == chrec_known
		   || *overlaps_b == chrec_known)
	    dependence_stats.num_siv_independent++;
	  else
	    dependence_stats.num_siv_dependent++;
	}
      else
	goto siv_subscript_dontknow;
    }

  else
    {
    siv_subscript_dontknow:;
      if (dump_file && (dump_flags & TDF_DETAILS))
	fprintf (dump_file, "siv test failed: unimplemented.\n");
      *overlaps_a = chrec_dont_know;
      *overlaps_b = chrec_dont_know;
      *last_conflicts = chrec_dont_know;
      dependence_stats.num_siv_unimplemented++;
    }
  
  if (dump_file && (dump_flags & TDF_DETAILS))
    fprintf (dump_file, ")\n");
}

/* Return true when the property can be computed.  RES should contain
   true when calling the first time this function, then it is set to
   false when one of the evolution steps of an affine CHREC does not
   divide the constant CST.  */

static bool
chrec_steps_divide_constant_p (tree chrec, 
			       tree cst, 
			       bool *res)
{
  switch (TREE_CODE (chrec))
    {
    case POLYNOMIAL_CHREC:
      if (evolution_function_is_constant_p (CHREC_RIGHT (chrec)))
	{
	  if (tree_fold_divides_p (CHREC_RIGHT (chrec), cst))
	    /* Keep RES to true, and iterate on other dimensions.  */
	    return chrec_steps_divide_constant_p (CHREC_LEFT (chrec), cst, res);
	  
	  *res = false;
	  return true;
	}
      else
	/* When the step is a parameter the result is undetermined.  */
	return false;

    default:
      /* On the initial condition, return true.  */
      return true;
    }
}

/* Analyze a MIV (Multiple Index Variable) subscript.  *OVERLAPS_A and
   *OVERLAPS_B are initialized to the functions that describe the
   relation between the elements accessed twice by CHREC_A and
   CHREC_B.  For k >= 0, the following property is verified:

   CHREC_A (*OVERLAPS_A (k)) = CHREC_B (*OVERLAPS_B (k)).  */

static void
analyze_miv_subscript (tree chrec_a, 
		       tree chrec_b, 
		       tree *overlaps_a, 
		       tree *overlaps_b, 
		       tree *last_conflicts)
{
  /* FIXME:  This is a MIV subscript, not yet handled.
     Example: (A[{1, +, 1}_1] vs. A[{1, +, 1}_2]) that comes from 
     (A[i] vs. A[j]).  
     
     In the SIV test we had to solve a Diophantine equation with two
     variables.  In the MIV case we have to solve a Diophantine
     equation with 2*n variables (if the subscript uses n IVs).
  */
  bool divide_p = true;
  tree difference;
  dependence_stats.num_miv++;
  if (dump_file && (dump_flags & TDF_DETAILS))
    fprintf (dump_file, "(analyze_miv_subscript \n");

  chrec_a = chrec_convert (integer_type_node, chrec_a, NULL_TREE);
  chrec_b = chrec_convert (integer_type_node, chrec_b, NULL_TREE);
  difference = chrec_fold_minus (integer_type_node, chrec_a, chrec_b);
  
  if (eq_evolutions_p (chrec_a, chrec_b))
    {
      /* Access functions are the same: all the elements are accessed
	 in the same order.  */
      *overlaps_a = integer_zero_node;
      *overlaps_b = integer_zero_node;
      *last_conflicts = get_number_of_iters_for_loop (CHREC_VARIABLE (chrec_a));
      dependence_stats.num_miv_dependent++;
    }
  
  else if (evolution_function_is_constant_p (difference)
	   /* For the moment, the following is verified:
	      evolution_function_is_affine_multivariate_p (chrec_a) */
	   && chrec_steps_divide_constant_p (chrec_a, difference, &divide_p)
	   && !divide_p)
    {
      /* testsuite/.../ssa-chrec-33.c
	 {{21, +, 2}_1, +, -2}_2  vs.  {{20, +, 2}_1, +, -2}_2 
	 
	 The difference is 1, and the evolution steps are equal to 2,
	 consequently there are no overlapping elements.  */
      *overlaps_a = chrec_known;
      *overlaps_b = chrec_known;
      *last_conflicts = integer_zero_node;
      dependence_stats.num_miv_independent++;
    }
  
  else if (evolution_function_is_affine_multivariate_p (chrec_a)
	   && !chrec_contains_symbols (chrec_a)
	   && evolution_function_is_affine_multivariate_p (chrec_b)
	   && !chrec_contains_symbols (chrec_b))
    {
      /* testsuite/.../ssa-chrec-35.c
	 {0, +, 1}_2  vs.  {0, +, 1}_3
	 the overlapping elements are respectively located at iterations:
	 {0, +, 1}_x and {0, +, 1}_x, 
	 in other words, we have the equality: 
	 {0, +, 1}_2 ({0, +, 1}_x) = {0, +, 1}_3 ({0, +, 1}_x)
	 
	 Other examples: 
	 {{0, +, 1}_1, +, 2}_2 ({0, +, 1}_x, {0, +, 1}_y) = 
	 {0, +, 1}_1 ({{0, +, 1}_x, +, 2}_y)

	 {{0, +, 2}_1, +, 3}_2 ({0, +, 1}_y, {0, +, 1}_x) = 
	 {{0, +, 3}_1, +, 2}_2 ({0, +, 1}_x, {0, +, 1}_y)
      */
      analyze_subscript_affine_affine (chrec_a, chrec_b, 
				       overlaps_a, overlaps_b, last_conflicts);

      if (*overlaps_a == chrec_dont_know
	  || *overlaps_b == chrec_dont_know)
	dependence_stats.num_miv_unimplemented++;
      else if (*overlaps_a == chrec_known
	       || *overlaps_b == chrec_known)
	dependence_stats.num_miv_independent++;
      else
	dependence_stats.num_miv_dependent++;
    }
  
  else
    {
      /* When the analysis is too difficult, answer "don't know".  */
      if (dump_file && (dump_flags & TDF_DETAILS))
	fprintf (dump_file, "analyze_miv_subscript test failed: unimplemented.\n");

      *overlaps_a = chrec_dont_know;
      *overlaps_b = chrec_dont_know;
      *last_conflicts = chrec_dont_know;
      dependence_stats.num_miv_unimplemented++;
    }
  
  if (dump_file && (dump_flags & TDF_DETAILS))
    fprintf (dump_file, ")\n");
}

/* Determines the iterations for which CHREC_A is equal to CHREC_B.
   OVERLAP_ITERATIONS_A and OVERLAP_ITERATIONS_B are initialized with
   two functions that describe the iterations that contain conflicting
   elements.
   
   Remark: For an integer k >= 0, the following equality is true:
   
   CHREC_A (OVERLAP_ITERATIONS_A (k)) == CHREC_B (OVERLAP_ITERATIONS_B (k)).
*/

static void 
analyze_overlapping_iterations (tree chrec_a, 
				tree chrec_b, 
				tree *overlap_iterations_a, 
				tree *overlap_iterations_b, 
				tree *last_conflicts)
{
  dependence_stats.num_subscript_tests++;
  
  if (dump_file && (dump_flags & TDF_DETAILS))
    {
      fprintf (dump_file, "(analyze_overlapping_iterations \n");
      fprintf (dump_file, "  (chrec_a = ");
      print_generic_expr (dump_file, chrec_a, 0);
      fprintf (dump_file, ")\n  (chrec_b = ");
      print_generic_expr (dump_file, chrec_b, 0);
      fprintf (dump_file, ")\n");
    }

  if (chrec_a == NULL_TREE
      || chrec_b == NULL_TREE
      || chrec_contains_undetermined (chrec_a)
      || chrec_contains_undetermined (chrec_b))
    {
      dependence_stats.num_subscript_undetermined++;
      
      *overlap_iterations_a = chrec_dont_know;
      *overlap_iterations_b = chrec_dont_know;
    }

  /* If they are the same chrec, and are affine, they overlap 
     on every iteration.  */
  else if (eq_evolutions_p (chrec_a, chrec_b)
	   && evolution_function_is_affine_multivariate_p (chrec_a))
    {
      dependence_stats.num_same_subscript_function++;
      *overlap_iterations_a = integer_zero_node;
      *overlap_iterations_b = integer_zero_node;
      *last_conflicts = chrec_dont_know;
    }

  /* If they aren't the same, and aren't affine, we can't do anything
     yet. */
  else if ((chrec_contains_symbols (chrec_a) 
	    || chrec_contains_symbols (chrec_b))
	   && (!evolution_function_is_affine_multivariate_p (chrec_a)
	       || !evolution_function_is_affine_multivariate_p (chrec_b)))
    {
      dependence_stats.num_subscript_undetermined++;
      *overlap_iterations_a = chrec_dont_know;
      *overlap_iterations_b = chrec_dont_know;
    }

  else if (ziv_subscript_p (chrec_a, chrec_b))
    analyze_ziv_subscript (chrec_a, chrec_b, 
			   overlap_iterations_a, overlap_iterations_b,
			   last_conflicts);
  
  else if (siv_subscript_p (chrec_a, chrec_b))
    analyze_siv_subscript (chrec_a, chrec_b, 
			   overlap_iterations_a, overlap_iterations_b, 
			   last_conflicts);
  
  else
    analyze_miv_subscript (chrec_a, chrec_b, 
			   overlap_iterations_a, overlap_iterations_b,
			   last_conflicts);
  
  if (dump_file && (dump_flags & TDF_DETAILS))
    {
      fprintf (dump_file, "  (overlap_iterations_a = ");
      print_generic_expr (dump_file, *overlap_iterations_a, 0);
      fprintf (dump_file, ")\n  (overlap_iterations_b = ");
      print_generic_expr (dump_file, *overlap_iterations_b, 0);
      fprintf (dump_file, ")\n");
      fprintf (dump_file, ")\n");
    }
}

/* Helper function for uniquely inserting distance vectors.  */

static void
save_dist_v (struct data_dependence_relation *ddr, lambda_vector dist_v)
{
  unsigned i;
  lambda_vector v;

  for (i = 0; VEC_iterate (lambda_vector, DDR_DIST_VECTS (ddr), i, v); i++)
    if (lambda_vector_equal (v, dist_v, DDR_NB_LOOPS (ddr)))
      return;

  VEC_safe_push (lambda_vector, heap, DDR_DIST_VECTS (ddr), dist_v);
}

/* Helper function for uniquely inserting direction vectors.  */

static void
save_dir_v (struct data_dependence_relation *ddr, lambda_vector dir_v)
{
  unsigned i;
  lambda_vector v;

  for (i = 0; VEC_iterate (lambda_vector, DDR_DIR_VECTS (ddr), i, v); i++)
    if (lambda_vector_equal (v, dir_v, DDR_NB_LOOPS (ddr)))
      return;

  VEC_safe_push (lambda_vector, heap, DDR_DIR_VECTS (ddr), dir_v);
}

/* Add a distance of 1 on all the loops outer than INDEX.  If we
   haven't yet determined a distance for this outer loop, push a new
   distance vector composed of the previous distance, and a distance
   of 1 for this outer loop.  Example:

   | loop_1
   |   loop_2
   |     A[10]
   |   endloop_2
   | endloop_1

   Saved vectors are of the form (dist_in_1, dist_in_2).  First, we
   save (0, 1), then we have to save (1, 0).  */

static void
add_outer_distances (struct data_dependence_relation *ddr,
		     lambda_vector dist_v, int index)
{
  /* For each outer loop where init_v is not set, the accesses are
     in dependence of distance 1 in the loop.  */
  while (--index >= 0)
    {
      lambda_vector save_v = lambda_vector_new (DDR_NB_LOOPS (ddr));
      lambda_vector_copy (dist_v, save_v, DDR_NB_LOOPS (ddr));
      save_v[index] = 1;
      save_dist_v (ddr, save_v);
    }
}

/* Return false when fail to represent the data dependence as a
   distance vector.  INIT_B is set to true when a component has been
   added to the distance vector DIST_V.  INDEX_CARRY is then set to
   the index in DIST_V that carries the dependence.  */

static bool
build_classic_dist_vector_1 (struct data_dependence_relation *ddr,
			     struct data_reference *ddr_a,
			     struct data_reference *ddr_b,
			     lambda_vector dist_v, bool *init_b,
			     int *index_carry)
{
  unsigned i;
  lambda_vector init_v = lambda_vector_new (DDR_NB_LOOPS (ddr));

  for (i = 0; i < DDR_NUM_SUBSCRIPTS (ddr); i++)
    {
      tree access_fn_a, access_fn_b;
      struct subscript *subscript = DDR_SUBSCRIPT (ddr, i);

      if (chrec_contains_undetermined (SUB_DISTANCE (subscript)))
	{
	  non_affine_dependence_relation (ddr);
	  return false;
	}

      access_fn_a = DR_ACCESS_FN (ddr_a, i);
      access_fn_b = DR_ACCESS_FN (ddr_b, i);

      if (TREE_CODE (access_fn_a) == POLYNOMIAL_CHREC 
	  && TREE_CODE (access_fn_b) == POLYNOMIAL_CHREC)
	{
	  int dist, index;
	  int index_a = index_in_loop_nest (CHREC_VARIABLE (access_fn_a),
					    DDR_LOOP_NEST (ddr));
	  int index_b = index_in_loop_nest (CHREC_VARIABLE (access_fn_b),
					    DDR_LOOP_NEST (ddr));

	  /* The dependence is carried by the outermost loop.  Example:
	     | loop_1
	     |   A[{4, +, 1}_1]
	     |   loop_2
	     |     A[{5, +, 1}_2]
	     |   endloop_2
	     | endloop_1
	     In this case, the dependence is carried by loop_1.  */
	  index = index_a < index_b ? index_a : index_b;
	  *index_carry = MIN (index, *index_carry);

	  if (chrec_contains_undetermined (SUB_DISTANCE (subscript)))
	    {
	      non_affine_dependence_relation (ddr);
	      return false;
	    }
	  
	  dist = int_cst_value (SUB_DISTANCE (subscript));

	  /* This is the subscript coupling test.  If we have already
	     recorded a distance for this loop (a distance coming from
	     another subscript), it should be the same.  For example,
	     in the following code, there is no dependence:

	     | loop i = 0, N, 1
	     |   T[i+1][i] = ...
	     |   ... = T[i][i]
	     | endloop
	  */
	  if (init_v[index] != 0 && dist_v[index] != dist)
	    {
	      finalize_ddr_dependent (ddr, chrec_known);
	      return false;
	    }

	  dist_v[index] = dist;
	  init_v[index] = 1;
	  *init_b = true;
	}
      else
	{
	  /* This can be for example an affine vs. constant dependence
	     (T[i] vs. T[3]) that is not an affine dependence and is
	     not representable as a distance vector.  */
	  non_affine_dependence_relation (ddr);
	  return false;
	}
    }

  return true;
}

/* Return true when the DDR contains two data references that have the
   same access functions.  */

static bool
same_access_functions (struct data_dependence_relation *ddr)
{
  unsigned i;

  for (i = 0; i < DDR_NUM_SUBSCRIPTS (ddr); i++)
    if (!eq_evolutions_p (DR_ACCESS_FN (DDR_A (ddr), i),
			  DR_ACCESS_FN (DDR_B (ddr), i)))
      return false;

  return true;
}

/* Helper function for the case where DDR_A and DDR_B are the same
   multivariate access function.  */

static void
add_multivariate_self_dist (struct data_dependence_relation *ddr, tree c_2)
{
  int x_1, x_2;
  tree c_1 = CHREC_LEFT (c_2);
  tree c_0 = CHREC_LEFT (c_1);
  lambda_vector dist_v;

  /* Polynomials with more than 2 variables are not handled yet.  */
  if (TREE_CODE (c_0) != INTEGER_CST)
    {
      DDR_ARE_DEPENDENT (ddr) = chrec_dont_know;
      return;
    }

  x_2 = index_in_loop_nest (CHREC_VARIABLE (c_2), DDR_LOOP_NEST (ddr));
  x_1 = index_in_loop_nest (CHREC_VARIABLE (c_1), DDR_LOOP_NEST (ddr));

  /* For "{{0, +, 2}_1, +, 3}_2" the distance vector is (3, -2).  */
  dist_v = lambda_vector_new (DDR_NB_LOOPS (ddr));
  dist_v[x_1] = int_cst_value (CHREC_RIGHT (c_2));
  dist_v[x_2] = -int_cst_value (CHREC_RIGHT (c_1));
  save_dist_v (ddr, dist_v);

  add_outer_distances (ddr, dist_v, x_1);
}

/* Helper function for the case where DDR_A and DDR_B are the same
   access functions.  */

static void
add_other_self_distances (struct data_dependence_relation *ddr)
{
  lambda_vector dist_v;
  unsigned i;
  int index_carry = DDR_NB_LOOPS (ddr);

  for (i = 0; i < DDR_NUM_SUBSCRIPTS (ddr); i++)
    {
      tree access_fun = DR_ACCESS_FN (DDR_A (ddr), i);

      if (TREE_CODE (access_fun) == POLYNOMIAL_CHREC)
	{
	  if (!evolution_function_is_univariate_p (access_fun))
	    {
	      if (DDR_NUM_SUBSCRIPTS (ddr) != 1)
		{
		  DDR_ARE_DEPENDENT (ddr) = chrec_dont_know;
		  return;
		}

	      add_multivariate_self_dist (ddr, DR_ACCESS_FN (DDR_A (ddr), 0));
	      return;
	    }

	  index_carry = MIN (index_carry,
			     index_in_loop_nest (CHREC_VARIABLE (access_fun),
						 DDR_LOOP_NEST (ddr)));
	}
    }

  dist_v = lambda_vector_new (DDR_NB_LOOPS (ddr));
  add_outer_distances (ddr, dist_v, index_carry);
}

/* Compute the classic per loop distance vector.  DDR is the data
   dependence relation to build a vector from.  Return false when fail
   to represent the data dependence as a distance vector.  */

static bool
build_classic_dist_vector (struct data_dependence_relation *ddr)
{
  bool init_b = false;
  int index_carry = DDR_NB_LOOPS (ddr);
  lambda_vector dist_v;

  if (DDR_ARE_DEPENDENT (ddr) != NULL_TREE)
    return true;

  if (same_access_functions (ddr))
    {
      /* Save the 0 vector.  */
      dist_v = lambda_vector_new (DDR_NB_LOOPS (ddr));
      save_dist_v (ddr, dist_v);

      if (DDR_NB_LOOPS (ddr) > 1)
	add_other_self_distances (ddr);

      return true;
    }

  dist_v = lambda_vector_new (DDR_NB_LOOPS (ddr));
  if (!build_classic_dist_vector_1 (ddr, DDR_A (ddr), DDR_B (ddr),
				    dist_v, &init_b, &index_carry))
    return false;

  /* Save the distance vector if we initialized one.  */
  if (init_b)
    {
      /* Verify a basic constraint: classic distance vectors should
	 always be lexicographically positive.

	 Data references are collected in the order of execution of
	 the program, thus for the following loop

	 | for (i = 1; i < 100; i++)
	 |   for (j = 1; j < 100; j++)
	 |     {
	 |       t = T[j+1][i-1];  // A
	 |       T[j][i] = t + 2;  // B
	 |     }

	 references are collected following the direction of the wind:
	 A then B.  The data dependence tests are performed also
	 following this order, such that we're looking at the distance
	 separating the elements accessed by A from the elements later
	 accessed by B.  But in this example, the distance returned by
	 test_dep (A, B) is lexicographically negative (-1, 1), that
	 means that the access A occurs later than B with respect to
	 the outer loop, ie. we're actually looking upwind.  In this
	 case we solve test_dep (B, A) looking downwind to the
	 lexicographically positive solution, that returns the
	 distance vector (1, -1).  */
      if (!lambda_vector_lexico_pos (dist_v, DDR_NB_LOOPS (ddr)))
	{
	  lambda_vector save_v = lambda_vector_new (DDR_NB_LOOPS (ddr));
	  subscript_dependence_tester_1 (ddr, DDR_B (ddr), DDR_A (ddr));
	  compute_subscript_distance (ddr);
	  build_classic_dist_vector_1 (ddr, DDR_B (ddr), DDR_A (ddr),
				       save_v, &init_b, &index_carry);
	  save_dist_v (ddr, save_v);

	  /* In this case there is a dependence forward for all the
	     outer loops:

	     | for (k = 1; k < 100; k++)
	     |  for (i = 1; i < 100; i++)
	     |   for (j = 1; j < 100; j++)
	     |     {
	     |       t = T[j+1][i-1];  // A
	     |       T[j][i] = t + 2;  // B
	     |     }

	     the vectors are: 
	     (0,  1, -1)
	     (1,  1, -1)
	     (1, -1,  1)
	  */
	  if (DDR_NB_LOOPS (ddr) > 1)
	    {
 	      add_outer_distances (ddr, save_v, index_carry);
	      add_outer_distances (ddr, dist_v, index_carry);
	    }
	}
      else
	{
	  lambda_vector save_v = lambda_vector_new (DDR_NB_LOOPS (ddr));
	  lambda_vector_copy (dist_v, save_v, DDR_NB_LOOPS (ddr));
	  save_dist_v (ddr, save_v);

	  if (DDR_NB_LOOPS (ddr) > 1)
	    {
	      lambda_vector opposite_v = lambda_vector_new (DDR_NB_LOOPS (ddr));

	      subscript_dependence_tester_1 (ddr, DDR_B (ddr), DDR_A (ddr));
	      compute_subscript_distance (ddr);
	      build_classic_dist_vector_1 (ddr, DDR_B (ddr), DDR_A (ddr),
					   opposite_v, &init_b, &index_carry);

	      add_outer_distances (ddr, dist_v, index_carry);
	      add_outer_distances (ddr, opposite_v, index_carry);
	    }
	}
    }
  else
    {
      /* There is a distance of 1 on all the outer loops: Example:
	 there is a dependence of distance 1 on loop_1 for the array A.

	 | loop_1
	 |   A[5] = ...
	 | endloop
      */
      add_outer_distances (ddr, dist_v,
			   lambda_vector_first_nz (dist_v,
						   DDR_NB_LOOPS (ddr), 0));
    }

  if (dump_file && (dump_flags & TDF_DETAILS))
    {
      unsigned i;

      fprintf (dump_file, "(build_classic_dist_vector\n");
      for (i = 0; i < DDR_NUM_DIST_VECTS (ddr); i++)
	{
	  fprintf (dump_file, "  dist_vector = (");
	  print_lambda_vector (dump_file, DDR_DIST_VECT (ddr, i),
			       DDR_NB_LOOPS (ddr));
	  fprintf (dump_file, "  )\n");
	}
      fprintf (dump_file, ")\n");
    }

  return true;
}

/* Return the direction for a given distance.
   FIXME: Computing dir this way is suboptimal, since dir can catch
   cases that dist is unable to represent.  */

static inline enum data_dependence_direction
dir_from_dist (int dist)
{
  if (dist > 0)
    return dir_positive;
  else if (dist < 0)
    return dir_negative;
  else
    return dir_equal;
}

/* Compute the classic per loop direction vector.  DDR is the data
   dependence relation to build a vector from.  */

static void
build_classic_dir_vector (struct data_dependence_relation *ddr)
{
  unsigned i, j;
  lambda_vector dist_v;

  for (i = 0; VEC_iterate (lambda_vector, DDR_DIST_VECTS (ddr), i, dist_v); i++)
    {
      lambda_vector dir_v = lambda_vector_new (DDR_NB_LOOPS (ddr));

      for (j = 0; j < DDR_NB_LOOPS (ddr); j++)
	dir_v[j] = dir_from_dist (dist_v[j]);

      save_dir_v (ddr, dir_v);
    }
}

/* Helper function.  Returns true when there is a dependence between
   data references DRA and DRB.  */

static bool
subscript_dependence_tester_1 (struct data_dependence_relation *ddr,
			       struct data_reference *dra,
			       struct data_reference *drb)
{
  unsigned int i;
  tree last_conflicts;
  struct subscript *subscript;

  for (i = 0; VEC_iterate (subscript_p, DDR_SUBSCRIPTS (ddr), i, subscript);
       i++)
    {
      tree overlaps_a, overlaps_b;

      analyze_overlapping_iterations (DR_ACCESS_FN (dra, i), 
				      DR_ACCESS_FN (drb, i),
				      &overlaps_a, &overlaps_b, 
				      &last_conflicts);

      if (chrec_contains_undetermined (overlaps_a)
 	  || chrec_contains_undetermined (overlaps_b))
 	{
 	  finalize_ddr_dependent (ddr, chrec_dont_know);
	  dependence_stats.num_dependence_undetermined++;
	  return false;
 	}

      else if (overlaps_a == chrec_known
 	       || overlaps_b == chrec_known)
 	{
 	  finalize_ddr_dependent (ddr, chrec_known);
	  dependence_stats.num_dependence_independent++;
	  return false;
 	}

      else
 	{
 	  SUB_CONFLICTS_IN_A (subscript) = overlaps_a;
 	  SUB_CONFLICTS_IN_B (subscript) = overlaps_b;
	  SUB_LAST_CONFLICT (subscript) = last_conflicts;
 	}
    }

  return true;
}

/* Computes the conflicting iterations, and initialize DDR.  */

static void
subscript_dependence_tester (struct data_dependence_relation *ddr)
{
  
  if (dump_file && (dump_flags & TDF_DETAILS))
    fprintf (dump_file, "(subscript_dependence_tester \n");
  
  if (subscript_dependence_tester_1 (ddr, DDR_A (ddr), DDR_B (ddr)))
    dependence_stats.num_dependence_dependent++;

  compute_subscript_distance (ddr);
  if (build_classic_dist_vector (ddr))
    build_classic_dir_vector (ddr);

  if (dump_file && (dump_flags & TDF_DETAILS))
    fprintf (dump_file, ")\n");
}

/* Returns true when all the access functions of A are affine or
   constant.  */

static bool 
access_functions_are_affine_or_constant_p (struct data_reference *a)
{
  unsigned int i;
  VEC(tree,heap) **fns = DR_ACCESS_FNS_ADDR (a);
  tree t;
  
  for (i = 0; VEC_iterate (tree, *fns, i, t); i++)
    if (!evolution_function_is_constant_p (t)
	&& !evolution_function_is_affine_multivariate_p (t))
      return false;
  
  return true;
}

/* This computes the affine dependence relation between A and B.
   CHREC_KNOWN is used for representing the independence between two
   accesses, while CHREC_DONT_KNOW is used for representing the unknown
   relation.
   
   Note that it is possible to stop the computation of the dependence
   relation the first time we detect a CHREC_KNOWN element for a given
   subscript.  */

static void
compute_affine_dependence (struct data_dependence_relation *ddr)
{
  struct data_reference *dra = DDR_A (ddr);
  struct data_reference *drb = DDR_B (ddr);
  
  if (dump_file && (dump_flags & TDF_DETAILS))
    {
      fprintf (dump_file, "(compute_affine_dependence\n");
      fprintf (dump_file, "  (stmt_a = \n");
      print_generic_expr (dump_file, DR_STMT (dra), 0);
      fprintf (dump_file, ")\n  (stmt_b = \n");
      print_generic_expr (dump_file, DR_STMT (drb), 0);
      fprintf (dump_file, ")\n");
    }

  /* Analyze only when the dependence relation is not yet known.  */
  if (DDR_ARE_DEPENDENT (ddr) == NULL_TREE)
    {
      dependence_stats.num_dependence_tests++;

      if (access_functions_are_affine_or_constant_p (dra)
	  && access_functions_are_affine_or_constant_p (drb))
	subscript_dependence_tester (ddr);
      
      /* As a last case, if the dependence cannot be determined, or if
	 the dependence is considered too difficult to determine, answer
	 "don't know".  */
      else
	{
	  dependence_stats.num_dependence_undetermined++;

	  if (dump_file && (dump_flags & TDF_DETAILS))
	    {
	      fprintf (dump_file, "Data ref a:\n");
	      dump_data_reference (dump_file, dra);
	      fprintf (dump_file, "Data ref b:\n");
	      dump_data_reference (dump_file, drb);
	      fprintf (dump_file, "affine dependence test not usable: access function not affine or constant.\n");
	    }
	  finalize_ddr_dependent (ddr, chrec_dont_know);
	}
    }
  
  if (dump_file && (dump_flags & TDF_DETAILS))
    fprintf (dump_file, ")\n");
}

/* This computes the dependence relation for the same data
   reference into DDR.  */

static void
compute_self_dependence (struct data_dependence_relation *ddr)
{
  unsigned int i;
  struct subscript *subscript;

  for (i = 0; VEC_iterate (subscript_p, DDR_SUBSCRIPTS (ddr), i, subscript);
       i++)
    {
      /* The accessed index overlaps for each iteration.  */
      SUB_CONFLICTS_IN_A (subscript) = integer_zero_node;
      SUB_CONFLICTS_IN_B (subscript) = integer_zero_node;
      SUB_LAST_CONFLICT (subscript) = chrec_dont_know;
    }

  /* The distance vector is the zero vector.  */
  save_dist_v (ddr, lambda_vector_new (DDR_NB_LOOPS (ddr)));
  save_dir_v (ddr, lambda_vector_new (DDR_NB_LOOPS (ddr)));
}

/* Compute in DEPENDENCE_RELATIONS the data dependence graph for all
   the data references in DATAREFS, in the LOOP_NEST.  When
   COMPUTE_SELF_AND_RR is FALSE, don't compute read-read and self
   relations.  */

static void 
compute_all_dependences (VEC (data_reference_p, heap) *datarefs,
			 VEC (ddr_p, heap) **dependence_relations,
			 VEC (loop_p, heap) *loop_nest,
			 bool compute_self_and_rr)
{
  struct data_dependence_relation *ddr;
  struct data_reference *a, *b;
  unsigned int i, j;

  for (i = 0; VEC_iterate (data_reference_p, datarefs, i, a); i++)
    for (j = i + 1; VEC_iterate (data_reference_p, datarefs, j, b); j++)
      if (!DR_IS_READ (a) || !DR_IS_READ (b) || compute_self_and_rr)
	{
	  ddr = initialize_data_dependence_relation (a, b, loop_nest);
	  VEC_safe_push (ddr_p, heap, *dependence_relations, ddr);
	  compute_affine_dependence (ddr);
	}

  if (compute_self_and_rr)
    for (i = 0; VEC_iterate (data_reference_p, datarefs, i, a); i++)
      {
	ddr = initialize_data_dependence_relation (a, a, loop_nest);
	VEC_safe_push (ddr_p, heap, *dependence_relations, ddr);
	compute_self_dependence (ddr);
      }
}

/* Search the data references in LOOP, and record the information into
   DATAREFS.  Returns chrec_dont_know when failing to analyze a
   difficult case, returns NULL_TREE otherwise.
   
   TODO: This function should be made smarter so that it can handle address
   arithmetic as if they were array accesses, etc.  */

tree 
find_data_references_in_loop (struct loop *loop,
			      VEC (data_reference_p, heap) **datarefs)
{
  basic_block bb, *bbs;
  unsigned int i;
  block_stmt_iterator bsi;
  struct data_reference *dr;

  bbs = get_loop_body (loop);

  for (i = 0; i < loop->num_nodes; i++)
    {
      bb = bbs[i];

      for (bsi = bsi_start (bb); !bsi_end_p (bsi); bsi_next (&bsi))
        {
	  tree stmt = bsi_stmt (bsi);

	  /* ASM_EXPR and CALL_EXPR may embed arbitrary side effects.
	     Calls have side-effects, except those to const or pure
	     functions.  */
	  if ((TREE_CODE (stmt) == CALL_EXPR
	       && !(call_expr_flags (stmt) & (ECF_CONST | ECF_PURE)))
	      || (TREE_CODE (stmt) == ASM_EXPR
		  && ASM_VOLATILE_P (stmt)))
	    goto insert_dont_know_node;

	  if (ZERO_SSA_OPERANDS (stmt, SSA_OP_ALL_VIRTUALS))
	    continue;

	  switch (TREE_CODE (stmt))
	    {
	    case MODIFY_EXPR:
	      {
		bool one_inserted = false;
		tree opnd0 = TREE_OPERAND (stmt, 0);
		tree opnd1 = TREE_OPERAND (stmt, 1);
		
		if (TREE_CODE (opnd0) == ARRAY_REF 
		    || TREE_CODE (opnd0) == INDIRECT_REF
                    || TREE_CODE (opnd0) == COMPONENT_REF)
		  {
		    dr = create_data_ref (opnd0, stmt, false);
		    if (dr) 
		      {
			VEC_safe_push (data_reference_p, heap, *datarefs, dr);
			one_inserted = true;
		      }
		  }

		if (TREE_CODE (opnd1) == ARRAY_REF 
		    || TREE_CODE (opnd1) == INDIRECT_REF
		    || TREE_CODE (opnd1) == COMPONENT_REF)
		  {
		    dr = create_data_ref (opnd1, stmt, true);
		    if (dr) 
		      {
			VEC_safe_push (data_reference_p, heap, *datarefs, dr);
			one_inserted = true;
		      }
		  }

		if (!one_inserted)
		  goto insert_dont_know_node;

		break;
	      }

	    case CALL_EXPR:
	      {
		tree args;
		bool one_inserted = false;

		for (args = TREE_OPERAND (stmt, 1); args; 
		     args = TREE_CHAIN (args))
		  if (TREE_CODE (TREE_VALUE (args)) == ARRAY_REF
		      || TREE_CODE (TREE_VALUE (args)) == INDIRECT_REF
		      || TREE_CODE (TREE_VALUE (args)) == COMPONENT_REF)
		    {
		      dr = create_data_ref (TREE_VALUE (args), stmt, true);
		      if (dr)
			{
			  VEC_safe_push (data_reference_p, heap, *datarefs, dr);
			  one_inserted = true;
			}
		    }

		if (!one_inserted)
		  goto insert_dont_know_node;

		break;
	      }

	    default:
		{
		  struct data_reference *res;

		insert_dont_know_node:;
		  res = XNEW (struct data_reference);
		  DR_STMT (res) = NULL_TREE;
		  DR_REF (res) = NULL_TREE;
		  DR_BASE_OBJECT (res) = NULL;
		  DR_TYPE (res) = ARRAY_REF_TYPE;
		  DR_SET_ACCESS_FNS (res, NULL);
		  DR_BASE_OBJECT (res) = NULL;
		  DR_IS_READ (res) = false;
		  DR_BASE_ADDRESS (res) = NULL_TREE;
		  DR_OFFSET (res) = NULL_TREE;
		  DR_INIT (res) = NULL_TREE;
		  DR_STEP (res) = NULL_TREE;
		  DR_OFFSET_MISALIGNMENT (res) = NULL_TREE;
		  DR_MEMTAG (res) = NULL_TREE;
		  DR_PTR_INFO (res) = NULL;
		  VEC_safe_push (data_reference_p, heap, *datarefs, res);

		  free (bbs);
		  return chrec_dont_know;
		}
	    }

	  /* When there are no defs in the loop, the loop is parallel.  */
	  if (!ZERO_SSA_OPERANDS (stmt, SSA_OP_VIRTUAL_DEFS))
	    loop->parallel_p = false;
	}
    }

  free (bbs);

  return NULL_TREE;
}

/* Recursive helper function.  */

static bool
find_loop_nest_1 (struct loop *loop, VEC (loop_p, heap) **loop_nest)
{
  /* Inner loops of the nest should not contain siblings.  Example:
     when there are two consecutive loops,

     | loop_0
     |   loop_1
     |     A[{0, +, 1}_1]
     |   endloop_1
     |   loop_2
     |     A[{0, +, 1}_2]
     |   endloop_2
     | endloop_0

     the dependence relation cannot be captured by the distance
     abstraction.  */
  if (loop->next)
    return false;

  VEC_safe_push (loop_p, heap, *loop_nest, loop);
  if (loop->inner)
    return find_loop_nest_1 (loop->inner, loop_nest);
  return true;
}

/* Return false when the LOOP is not well nested.  Otherwise return
   true and insert in LOOP_NEST the loops of the nest.  LOOP_NEST will
   contain the loops from the outermost to the innermost, as they will
   appear in the classic distance vector.  */

static bool
find_loop_nest (struct loop *loop, VEC (loop_p, heap) **loop_nest)
{
  VEC_safe_push (loop_p, heap, *loop_nest, loop);
  if (loop->inner)
    return find_loop_nest_1 (loop->inner, loop_nest);
  return true;
}

/* Given a loop nest LOOP, the following vectors are returned:
   DATAREFS is initialized to all the array elements contained in this loop, 
   DEPENDENCE_RELATIONS contains the relations between the data references.  
   Compute read-read and self relations if 
   COMPUTE_SELF_AND_READ_READ_DEPENDENCES is TRUE.  */

void
compute_data_dependences_for_loop (struct loop *loop, 
				   bool compute_self_and_read_read_dependences,
				   VEC (data_reference_p, heap) **datarefs,
				   VEC (ddr_p, heap) **dependence_relations)
{
  struct loop *loop_nest = loop;
  VEC (loop_p, heap) *vloops = VEC_alloc (loop_p, heap, 3);

  memset (&dependence_stats, 0, sizeof (dependence_stats));

  /* If the loop nest is not well formed, or one of the data references 
     is not computable, give up without spending time to compute other
     dependences.  */
  if (!loop_nest
      || !find_loop_nest (loop_nest, &vloops)
      || find_data_references_in_loop (loop, datarefs) == chrec_dont_know)
    {
      struct data_dependence_relation *ddr;

      /* Insert a single relation into dependence_relations:
	 chrec_dont_know.  */
      ddr = initialize_data_dependence_relation (NULL, NULL, vloops);
      VEC_safe_push (ddr_p, heap, *dependence_relations, ddr);
    }
  else
    compute_all_dependences (*datarefs, dependence_relations, vloops,
			     compute_self_and_read_read_dependences);

  if (dump_file && (dump_flags & TDF_STATS))
    {
      fprintf (dump_file, "Dependence tester statistics:\n");

      fprintf (dump_file, "Number of dependence tests: %d\n", 
	       dependence_stats.num_dependence_tests);
      fprintf (dump_file, "Number of dependence tests classified dependent: %d\n", 
	       dependence_stats.num_dependence_dependent);
      fprintf (dump_file, "Number of dependence tests classified independent: %d\n", 
	       dependence_stats.num_dependence_independent);
      fprintf (dump_file, "Number of undetermined dependence tests: %d\n", 
	       dependence_stats.num_dependence_undetermined);

      fprintf (dump_file, "Number of subscript tests: %d\n", 
	       dependence_stats.num_subscript_tests);
      fprintf (dump_file, "Number of undetermined subscript tests: %d\n", 
	       dependence_stats.num_subscript_undetermined);
      fprintf (dump_file, "Number of same subscript function: %d\n", 
	       dependence_stats.num_same_subscript_function);

      fprintf (dump_file, "Number of ziv tests: %d\n",
	       dependence_stats.num_ziv);
      fprintf (dump_file, "Number of ziv tests returning dependent: %d\n",
	       dependence_stats.num_ziv_dependent);
      fprintf (dump_file, "Number of ziv tests returning independent: %d\n",
	       dependence_stats.num_ziv_independent);
      fprintf (dump_file, "Number of ziv tests unimplemented: %d\n",
	       dependence_stats.num_ziv_unimplemented);      

      fprintf (dump_file, "Number of siv tests: %d\n", 
	       dependence_stats.num_siv);
      fprintf (dump_file, "Number of siv tests returning dependent: %d\n",
	       dependence_stats.num_siv_dependent);
      fprintf (dump_file, "Number of siv tests returning independent: %d\n",
	       dependence_stats.num_siv_independent);
      fprintf (dump_file, "Number of siv tests unimplemented: %d\n",
	       dependence_stats.num_siv_unimplemented);

      fprintf (dump_file, "Number of miv tests: %d\n", 
	       dependence_stats.num_miv);
      fprintf (dump_file, "Number of miv tests returning dependent: %d\n",
	       dependence_stats.num_miv_dependent);
      fprintf (dump_file, "Number of miv tests returning independent: %d\n",
	       dependence_stats.num_miv_independent);
      fprintf (dump_file, "Number of miv tests unimplemented: %d\n",
	       dependence_stats.num_miv_unimplemented);
    }    
}

/* Entry point (for testing only).  Analyze all the data references
   and the dependence relations.

   The data references are computed first.  
   
   A relation on these nodes is represented by a complete graph.  Some
   of the relations could be of no interest, thus the relations can be
   computed on demand.
   
   In the following function we compute all the relations.  This is
   just a first implementation that is here for:
   - for showing how to ask for the dependence relations, 
   - for the debugging the whole dependence graph,
   - for the dejagnu testcases and maintenance.
   
   It is possible to ask only for a part of the graph, avoiding to
   compute the whole dependence graph.  The computed dependences are
   stored in a knowledge base (KB) such that later queries don't
   recompute the same information.  The implementation of this KB is
   transparent to the optimizer, and thus the KB can be changed with a
   more efficient implementation, or the KB could be disabled.  */
#if 0
static void 
analyze_all_data_dependences (struct loops *loops)
{
  unsigned int i;
  int nb_data_refs = 10;
  VEC (data_reference_p, heap) *datarefs = 
    VEC_alloc (data_reference_p, heap, nb_data_refs);
  VEC (ddr_p, heap) *dependence_relations = 
    VEC_alloc (ddr_p, heap, nb_data_refs * nb_data_refs);

  /* Compute DDs on the whole function.  */
  compute_data_dependences_for_loop (loops->parray[0], false,
				     &datarefs, &dependence_relations);

  if (dump_file)
    {
      dump_data_dependence_relations (dump_file, dependence_relations);
      fprintf (dump_file, "\n\n");

      if (dump_flags & TDF_DETAILS)
	dump_dist_dir_vectors (dump_file, dependence_relations);

      if (dump_flags & TDF_STATS)
	{
	  unsigned nb_top_relations = 0;
	  unsigned nb_bot_relations = 0;
	  unsigned nb_basename_differ = 0;
	  unsigned nb_chrec_relations = 0;
	  struct data_dependence_relation *ddr;

	  for (i = 0; VEC_iterate (ddr_p, dependence_relations, i, ddr); i++)
	    {
	      if (chrec_contains_undetermined (DDR_ARE_DEPENDENT (ddr)))
		nb_top_relations++;
	  
	      else if (DDR_ARE_DEPENDENT (ddr) == chrec_known)
		{
		  struct data_reference *a = DDR_A (ddr);
		  struct data_reference *b = DDR_B (ddr);
		  bool differ_p;	
	      
		  if ((DR_BASE_OBJECT (a) && DR_BASE_OBJECT (b)
		       && DR_NUM_DIMENSIONS (a) != DR_NUM_DIMENSIONS (b))
		      || (base_object_differ_p (a, b, &differ_p) 
			  && differ_p))
		    nb_basename_differ++;
		  else
		    nb_bot_relations++;
		}
	  
	      else 
		nb_chrec_relations++;
	    }
      
	  gather_stats_on_scev_database ();
	}
    }

  free_dependence_relations (dependence_relations);
  free_data_refs (datarefs);
}
#endif

/* Free the memory used by a data dependence relation DDR.  */

void
free_dependence_relation (struct data_dependence_relation *ddr)
{
  if (ddr == NULL)
    return;

  if (DDR_ARE_DEPENDENT (ddr) == NULL_TREE && DDR_SUBSCRIPTS (ddr))
    VEC_free (subscript_p, heap, DDR_SUBSCRIPTS (ddr));

  free (ddr);
}

/* Free the memory used by the data dependence relations from
   DEPENDENCE_RELATIONS.  */

void 
free_dependence_relations (VEC (ddr_p, heap) *dependence_relations)
{
  unsigned int i;
  struct data_dependence_relation *ddr;
  VEC (loop_p, heap) *loop_nest = NULL;

  for (i = 0; VEC_iterate (ddr_p, dependence_relations, i, ddr); i++)
    {
      if (ddr == NULL)
	continue;
      if (loop_nest == NULL)
	loop_nest = DDR_LOOP_NEST (ddr);
      else
	gcc_assert (DDR_LOOP_NEST (ddr) == NULL
		    || DDR_LOOP_NEST (ddr) == loop_nest);
      free_dependence_relation (ddr);
    }

  if (loop_nest)
    VEC_free (loop_p, heap, loop_nest);
  VEC_free (ddr_p, heap, dependence_relations);
}

/* Free the memory used by the data references from DATAREFS.  */

void
free_data_refs (VEC (data_reference_p, heap) *datarefs)
{
  unsigned int i;
  struct data_reference *dr;

  for (i = 0; VEC_iterate (data_reference_p, datarefs, i, dr); i++)
    free_data_ref (dr);
  VEC_free (data_reference_p, heap, datarefs);
}

