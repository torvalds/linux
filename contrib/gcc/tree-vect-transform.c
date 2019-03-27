/* Transformation Utilities for Loop Vectorization.
   Copyright (C) 2003,2004,2005,2006 Free Software Foundation, Inc.
   Contributed by Dorit Naishlos <dorit@il.ibm.com>

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

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "ggc.h"
#include "tree.h"
#include "target.h"
#include "rtl.h"
#include "basic-block.h"
#include "diagnostic.h"
#include "tree-flow.h"
#include "tree-dump.h"
#include "timevar.h"
#include "cfgloop.h"
#include "expr.h"
#include "optabs.h"
#include "recog.h"
#include "tree-data-ref.h"
#include "tree-chrec.h"
#include "tree-scalar-evolution.h"
#include "tree-vectorizer.h"
#include "langhooks.h"
#include "tree-pass.h"
#include "toplev.h"
#include "real.h"

/* Utility functions for the code transformation.  */
static bool vect_transform_stmt (tree, block_stmt_iterator *);
static void vect_align_data_ref (tree);
static tree vect_create_destination_var (tree, tree);
static tree vect_create_data_ref_ptr 
  (tree, block_stmt_iterator *, tree, tree *, bool); 
static tree vect_create_addr_base_for_vector_ref (tree, tree *, tree);
static tree vect_get_new_vect_var (tree, enum vect_var_kind, const char *);
static tree vect_get_vec_def_for_operand (tree, tree, tree *);
static tree vect_init_vector (tree, tree);
static void vect_finish_stmt_generation 
  (tree stmt, tree vec_stmt, block_stmt_iterator *bsi);
static bool vect_is_simple_cond (tree, loop_vec_info); 
static void update_vuses_to_preheader (tree, struct loop*);
static void vect_create_epilog_for_reduction (tree, tree, enum tree_code, tree);
static tree get_initial_def_for_reduction (tree, tree, tree *);

/* Utility function dealing with loop peeling (not peeling itself).  */
static void vect_generate_tmps_on_preheader 
  (loop_vec_info, tree *, tree *, tree *);
static tree vect_build_loop_niters (loop_vec_info);
static void vect_update_ivs_after_vectorizer (loop_vec_info, tree, edge); 
static tree vect_gen_niters_for_prolog_loop (loop_vec_info, tree);
static void vect_update_init_of_dr (struct data_reference *, tree niters);
static void vect_update_inits_of_drs (loop_vec_info, tree);
static void vect_do_peeling_for_alignment (loop_vec_info, struct loops *);
static void vect_do_peeling_for_loop_bound 
  (loop_vec_info, tree *, struct loops *);
static int vect_min_worthwhile_factor (enum tree_code);


/* Function vect_get_new_vect_var.

   Returns a name for a new variable. The current naming scheme appends the 
   prefix "vect_" or "vect_p" (depending on the value of VAR_KIND) to 
   the name of vectorizer generated variables, and appends that to NAME if 
   provided.  */

static tree
vect_get_new_vect_var (tree type, enum vect_var_kind var_kind, const char *name)
{
  const char *prefix;
  tree new_vect_var;

  switch (var_kind)
  {
  case vect_simple_var:
    prefix = "vect_";
    break;
  case vect_scalar_var:
    prefix = "stmp_";
    break;
  case vect_pointer_var:
    prefix = "vect_p";
    break;
  default:
    gcc_unreachable ();
  }

  if (name)
    new_vect_var = create_tmp_var (type, concat (prefix, name, NULL));
  else
    new_vect_var = create_tmp_var (type, prefix);

  return new_vect_var;
}


/* Function vect_create_addr_base_for_vector_ref.

   Create an expression that computes the address of the first memory location
   that will be accessed for a data reference.

   Input:
   STMT: The statement containing the data reference.
   NEW_STMT_LIST: Must be initialized to NULL_TREE or a statement list.
   OFFSET: Optional. If supplied, it is be added to the initial address.

   Output:
   1. Return an SSA_NAME whose value is the address of the memory location of 
      the first vector of the data reference.
   2. If new_stmt_list is not NULL_TREE after return then the caller must insert
      these statement(s) which define the returned SSA_NAME.

   FORNOW: We are only handling array accesses with step 1.  */

static tree
vect_create_addr_base_for_vector_ref (tree stmt,
                                      tree *new_stmt_list,
				      tree offset)
{
  stmt_vec_info stmt_info = vinfo_for_stmt (stmt);
  struct data_reference *dr = STMT_VINFO_DATA_REF (stmt_info);
  tree data_ref_base = unshare_expr (DR_BASE_ADDRESS (dr));
  tree base_name = build_fold_indirect_ref (data_ref_base);
  tree ref = DR_REF (dr);
  tree scalar_type = TREE_TYPE (ref);
  tree scalar_ptr_type = build_pointer_type (scalar_type);
  tree vec_stmt;
  tree new_temp;
  tree addr_base, addr_expr;
  tree dest, new_stmt;
  tree base_offset = unshare_expr (DR_OFFSET (dr));
  tree init = unshare_expr (DR_INIT (dr));

  /* Create base_offset */
  base_offset = size_binop (PLUS_EXPR, base_offset, init);
  dest = create_tmp_var (TREE_TYPE (base_offset), "base_off");
  add_referenced_var (dest);
  base_offset = force_gimple_operand (base_offset, &new_stmt, false, dest);  
  append_to_statement_list_force (new_stmt, new_stmt_list);

  if (offset)
    {
      tree tmp = create_tmp_var (TREE_TYPE (base_offset), "offset");
      add_referenced_var (tmp);
      offset = fold_build2 (MULT_EXPR, TREE_TYPE (offset), offset,
			    DR_STEP (dr));
      base_offset = fold_build2 (PLUS_EXPR, TREE_TYPE (base_offset),
				 base_offset, offset);
      base_offset = force_gimple_operand (base_offset, &new_stmt, false, tmp);  
      append_to_statement_list_force (new_stmt, new_stmt_list);
    }
  
  /* base + base_offset */
  addr_base = fold_build2 (PLUS_EXPR, TREE_TYPE (data_ref_base), data_ref_base,
			   base_offset);

  /* addr_expr = addr_base */
  addr_expr = vect_get_new_vect_var (scalar_ptr_type, vect_pointer_var,
                                     get_name (base_name));
  add_referenced_var (addr_expr);
  vec_stmt = build2 (MODIFY_EXPR, void_type_node, addr_expr, addr_base);
  new_temp = make_ssa_name (addr_expr, vec_stmt);
  TREE_OPERAND (vec_stmt, 0) = new_temp;
  append_to_statement_list_force (vec_stmt, new_stmt_list);

  if (vect_print_dump_info (REPORT_DETAILS))
    {
      fprintf (vect_dump, "created ");
      print_generic_expr (vect_dump, vec_stmt, TDF_SLIM);
    }
  return new_temp;
}


/* Function vect_align_data_ref.

   Handle misalignment of a memory accesses.

   FORNOW: Can't handle misaligned accesses. 
   Make sure that the dataref is aligned.  */

static void
vect_align_data_ref (tree stmt)
{
  stmt_vec_info stmt_info = vinfo_for_stmt (stmt);
  struct data_reference *dr = STMT_VINFO_DATA_REF (stmt_info);

  /* FORNOW: can't handle misaligned accesses; 
             all accesses expected to be aligned.  */
  gcc_assert (aligned_access_p (dr));
}


/* Function vect_create_data_ref_ptr.

   Create a memory reference expression for vector access, to be used in a
   vector load/store stmt. The reference is based on a new pointer to vector
   type (vp).

   Input:
   1. STMT: a stmt that references memory. Expected to be of the form
         MODIFY_EXPR <name, data-ref> or MODIFY_EXPR <data-ref, name>.
   2. BSI: block_stmt_iterator where new stmts can be added.
   3. OFFSET (optional): an offset to be added to the initial address accessed
        by the data-ref in STMT.
   4. ONLY_INIT: indicate if vp is to be updated in the loop, or remain
        pointing to the initial address.

   Output:
   1. Declare a new ptr to vector_type, and have it point to the base of the
      data reference (initial addressed accessed by the data reference).
      For example, for vector of type V8HI, the following code is generated:

      v8hi *vp;
      vp = (v8hi *)initial_address;

      if OFFSET is not supplied:
         initial_address = &a[init];
      if OFFSET is supplied:
         initial_address = &a[init + OFFSET];

      Return the initial_address in INITIAL_ADDRESS.

   2. If ONLY_INIT is true, return the initial pointer.  Otherwise, create
      a data-reference in the loop based on the new vector pointer vp.  This
      new data reference will by some means be updated each iteration of
      the loop.  Return the pointer vp'.

   FORNOW: handle only aligned and consecutive accesses.  */

static tree
vect_create_data_ref_ptr (tree stmt,
			  block_stmt_iterator *bsi ATTRIBUTE_UNUSED,
			  tree offset, tree *initial_address, bool only_init)
{
  tree base_name;
  stmt_vec_info stmt_info = vinfo_for_stmt (stmt);
  loop_vec_info loop_vinfo = STMT_VINFO_LOOP_VINFO (stmt_info);
  struct loop *loop = LOOP_VINFO_LOOP (loop_vinfo);
  tree vectype = STMT_VINFO_VECTYPE (stmt_info);
  tree vect_ptr_type;
  tree vect_ptr;
  tree tag;
  tree new_temp;
  tree vec_stmt;
  tree new_stmt_list = NULL_TREE;
  edge pe = loop_preheader_edge (loop);
  basic_block new_bb;
  tree vect_ptr_init;
  struct data_reference *dr = STMT_VINFO_DATA_REF (stmt_info);

  base_name =  build_fold_indirect_ref (unshare_expr (DR_BASE_ADDRESS (dr)));

  if (vect_print_dump_info (REPORT_DETAILS))
    {
      tree data_ref_base = base_name;
      fprintf (vect_dump, "create vector-pointer variable to type: ");
      print_generic_expr (vect_dump, vectype, TDF_SLIM);
      if (TREE_CODE (data_ref_base) == VAR_DECL)
        fprintf (vect_dump, "  vectorizing a one dimensional array ref: ");
      else if (TREE_CODE (data_ref_base) == ARRAY_REF)
        fprintf (vect_dump, "  vectorizing a multidimensional array ref: ");
      else if (TREE_CODE (data_ref_base) == COMPONENT_REF)
        fprintf (vect_dump, "  vectorizing a record based array ref: ");
      else if (TREE_CODE (data_ref_base) == SSA_NAME)
        fprintf (vect_dump, "  vectorizing a pointer ref: ");
      print_generic_expr (vect_dump, base_name, TDF_SLIM);
    }

  /** (1) Create the new vector-pointer variable:  **/

  vect_ptr_type = build_pointer_type (vectype);
  vect_ptr = vect_get_new_vect_var (vect_ptr_type, vect_pointer_var,
                                    get_name (base_name));
  add_referenced_var (vect_ptr);
  
  
  /** (2) Add aliasing information to the new vector-pointer:
          (The points-to info (DR_PTR_INFO) may be defined later.)  **/
  
  tag = DR_MEMTAG (dr);
  gcc_assert (tag);

  /* If tag is a variable (and NOT_A_TAG) than a new symbol memory
     tag must be created with tag added to its may alias list.  */
  if (!MTAG_P (tag))
    new_type_alias (vect_ptr, tag, DR_REF (dr));
  else
    var_ann (vect_ptr)->symbol_mem_tag = tag;

  var_ann (vect_ptr)->subvars = DR_SUBVARS (dr);

  /** (3) Calculate the initial address the vector-pointer, and set
          the vector-pointer to point to it before the loop:  **/

  /* Create: (&(base[init_val+offset]) in the loop preheader.  */
  new_temp = vect_create_addr_base_for_vector_ref (stmt, &new_stmt_list,
                                                   offset);
  pe = loop_preheader_edge (loop);
  new_bb = bsi_insert_on_edge_immediate (pe, new_stmt_list);
  gcc_assert (!new_bb);
  *initial_address = new_temp;

  /* Create: p = (vectype *) initial_base  */
  vec_stmt = fold_convert (vect_ptr_type, new_temp);
  vec_stmt = build2 (MODIFY_EXPR, void_type_node, vect_ptr, vec_stmt);
  vect_ptr_init = make_ssa_name (vect_ptr, vec_stmt);
  TREE_OPERAND (vec_stmt, 0) = vect_ptr_init;
  new_bb = bsi_insert_on_edge_immediate (pe, vec_stmt);
  gcc_assert (!new_bb);


  /** (4) Handle the updating of the vector-pointer inside the loop: **/

  if (only_init) /* No update in loop is required.  */
    {
      /* Copy the points-to information if it exists. */
      if (DR_PTR_INFO (dr))
        duplicate_ssa_name_ptr_info (vect_ptr_init, DR_PTR_INFO (dr));
      return vect_ptr_init;
    }
  else
    {
      block_stmt_iterator incr_bsi;
      bool insert_after;
      tree indx_before_incr, indx_after_incr;
      tree incr;

      standard_iv_increment_position (loop, &incr_bsi, &insert_after);
      create_iv (vect_ptr_init,
		 fold_convert (vect_ptr_type, TYPE_SIZE_UNIT (vectype)),
		 NULL_TREE, loop, &incr_bsi, insert_after,
		 &indx_before_incr, &indx_after_incr);
      incr = bsi_stmt (incr_bsi);
      set_stmt_info (stmt_ann (incr),
		     new_stmt_vec_info (incr, loop_vinfo));

      /* Copy the points-to information if it exists. */
      if (DR_PTR_INFO (dr))
	{
	  duplicate_ssa_name_ptr_info (indx_before_incr, DR_PTR_INFO (dr));
	  duplicate_ssa_name_ptr_info (indx_after_incr, DR_PTR_INFO (dr));
	}
      merge_alias_info (vect_ptr_init, indx_before_incr);
      merge_alias_info (vect_ptr_init, indx_after_incr);

      return indx_before_incr;
    }
}


/* Function vect_create_destination_var.

   Create a new temporary of type VECTYPE.  */

static tree
vect_create_destination_var (tree scalar_dest, tree vectype)
{
  tree vec_dest;
  const char *new_name;
  tree type;
  enum vect_var_kind kind;

  kind = vectype ? vect_simple_var : vect_scalar_var;
  type = vectype ? vectype : TREE_TYPE (scalar_dest);

  gcc_assert (TREE_CODE (scalar_dest) == SSA_NAME);

  new_name = get_name (scalar_dest);
  if (!new_name)
    new_name = "var_";
  vec_dest = vect_get_new_vect_var (type, vect_simple_var, new_name);
  add_referenced_var (vec_dest);

  return vec_dest;
}


/* Function vect_init_vector.

   Insert a new stmt (INIT_STMT) that initializes a new vector variable with
   the vector elements of VECTOR_VAR. Return the DEF of INIT_STMT. It will be
   used in the vectorization of STMT.  */

static tree
vect_init_vector (tree stmt, tree vector_var)
{
  stmt_vec_info stmt_vinfo = vinfo_for_stmt (stmt);
  loop_vec_info loop_vinfo = STMT_VINFO_LOOP_VINFO (stmt_vinfo);
  struct loop *loop = LOOP_VINFO_LOOP (loop_vinfo);
  tree new_var;
  tree init_stmt;
  tree vectype = STMT_VINFO_VECTYPE (stmt_vinfo); 
  tree vec_oprnd;
  edge pe;
  tree new_temp;
  basic_block new_bb;
 
  new_var = vect_get_new_vect_var (vectype, vect_simple_var, "cst_");
  add_referenced_var (new_var); 
 
  init_stmt = build2 (MODIFY_EXPR, vectype, new_var, vector_var);
  new_temp = make_ssa_name (new_var, init_stmt);
  TREE_OPERAND (init_stmt, 0) = new_temp;

  pe = loop_preheader_edge (loop);
  new_bb = bsi_insert_on_edge_immediate (pe, init_stmt);
  gcc_assert (!new_bb);

  if (vect_print_dump_info (REPORT_DETAILS))
    {
      fprintf (vect_dump, "created new init_stmt: ");
      print_generic_expr (vect_dump, init_stmt, TDF_SLIM);
    }

  vec_oprnd = TREE_OPERAND (init_stmt, 0);
  return vec_oprnd;
}


/* Function vect_get_vec_def_for_operand.

   OP is an operand in STMT. This function returns a (vector) def that will be
   used in the vectorized stmt for STMT.

   In the case that OP is an SSA_NAME which is defined in the loop, then
   STMT_VINFO_VEC_STMT of the defining stmt holds the relevant def.

   In case OP is an invariant or constant, a new stmt that creates a vector def
   needs to be introduced.  */

static tree
vect_get_vec_def_for_operand (tree op, tree stmt, tree *scalar_def)
{
  tree vec_oprnd;
  tree vec_stmt;
  tree def_stmt;
  stmt_vec_info def_stmt_info = NULL;
  stmt_vec_info stmt_vinfo = vinfo_for_stmt (stmt);
  tree vectype = STMT_VINFO_VECTYPE (stmt_vinfo);
  int nunits = TYPE_VECTOR_SUBPARTS (vectype);
  loop_vec_info loop_vinfo = STMT_VINFO_LOOP_VINFO (stmt_vinfo);
  struct loop *loop = LOOP_VINFO_LOOP (loop_vinfo);
  tree vec_inv;
  tree vec_cst;
  tree t = NULL_TREE;
  tree def;
  int i;
  enum vect_def_type dt;
  bool is_simple_use;

  if (vect_print_dump_info (REPORT_DETAILS))
    {
      fprintf (vect_dump, "vect_get_vec_def_for_operand: ");
      print_generic_expr (vect_dump, op, TDF_SLIM);
    }

  is_simple_use = vect_is_simple_use (op, loop_vinfo, &def_stmt, &def, &dt);
  gcc_assert (is_simple_use);
  if (vect_print_dump_info (REPORT_DETAILS))
    {
      if (def)
        {
          fprintf (vect_dump, "def =  ");
          print_generic_expr (vect_dump, def, TDF_SLIM);
        }
      if (def_stmt)
        {
          fprintf (vect_dump, "  def_stmt =  ");
          print_generic_expr (vect_dump, def_stmt, TDF_SLIM);
        }
    }

  switch (dt)
    {
    /* Case 1: operand is a constant.  */
    case vect_constant_def:
      {
	if (scalar_def) 
	  *scalar_def = op;

        /* Create 'vect_cst_ = {cst,cst,...,cst}'  */
        if (vect_print_dump_info (REPORT_DETAILS))
          fprintf (vect_dump, "Create vector_cst. nunits = %d", nunits);

        for (i = nunits - 1; i >= 0; --i)
          {
            t = tree_cons (NULL_TREE, op, t);
          }
        vec_cst = build_vector (vectype, t);
        return vect_init_vector (stmt, vec_cst);
      }

    /* Case 2: operand is defined outside the loop - loop invariant.  */
    case vect_invariant_def:
      {
	if (scalar_def) 
	  *scalar_def = def;

        /* Create 'vec_inv = {inv,inv,..,inv}'  */
        if (vect_print_dump_info (REPORT_DETAILS))
          fprintf (vect_dump, "Create vector_inv.");

        for (i = nunits - 1; i >= 0; --i)
          {
            t = tree_cons (NULL_TREE, def, t);
          }

	/* FIXME: use build_constructor directly.  */
        vec_inv = build_constructor_from_list (vectype, t);
        return vect_init_vector (stmt, vec_inv);
      }

    /* Case 3: operand is defined inside the loop.  */
    case vect_loop_def:
      {
	if (scalar_def) 
	  *scalar_def = def_stmt;

        /* Get the def from the vectorized stmt.  */
        def_stmt_info = vinfo_for_stmt (def_stmt);
        vec_stmt = STMT_VINFO_VEC_STMT (def_stmt_info);
        gcc_assert (vec_stmt);
        vec_oprnd = TREE_OPERAND (vec_stmt, 0);
        return vec_oprnd;
      }

    /* Case 4: operand is defined by a loop header phi - reduction  */
    case vect_reduction_def:
      {
        gcc_assert (TREE_CODE (def_stmt) == PHI_NODE);

        /* Get the def before the loop  */
        op = PHI_ARG_DEF_FROM_EDGE (def_stmt, loop_preheader_edge (loop));
        return get_initial_def_for_reduction (stmt, op, scalar_def);
     }

    /* Case 5: operand is defined by loop-header phi - induction.  */
    case vect_induction_def:
      {
        if (vect_print_dump_info (REPORT_DETAILS))
          fprintf (vect_dump, "induction - unsupported.");
        internal_error ("no support for induction"); /* FORNOW */
      }

    default:
      gcc_unreachable ();
    }
}


/* Function vect_finish_stmt_generation.

   Insert a new stmt.  */

static void
vect_finish_stmt_generation (tree stmt, tree vec_stmt, block_stmt_iterator *bsi)
{
  bsi_insert_before (bsi, vec_stmt, BSI_SAME_STMT);

  if (vect_print_dump_info (REPORT_DETAILS))
    {
      fprintf (vect_dump, "add new stmt: ");
      print_generic_expr (vect_dump, vec_stmt, TDF_SLIM);
    }

  /* Make sure bsi points to the stmt that is being vectorized.  */
  gcc_assert (stmt == bsi_stmt (*bsi));

#ifdef USE_MAPPED_LOCATION
  SET_EXPR_LOCATION (vec_stmt, EXPR_LOCATION (stmt));
#else
  SET_EXPR_LOCUS (vec_stmt, EXPR_LOCUS (stmt));
#endif
}


#define ADJUST_IN_EPILOG 1

/* Function get_initial_def_for_reduction

   Input:
   STMT - a stmt that performs a reduction operation in the loop.
   INIT_VAL - the initial value of the reduction variable

   Output:
   SCALAR_DEF - a tree that holds a value to be added to the final result
	of the reduction (used for "ADJUST_IN_EPILOG" - see below).
   Return a vector variable, initialized according to the operation that STMT
	performs. This vector will be used as the initial value of the
	vector of partial results.

   Option1 ("ADJUST_IN_EPILOG"): Initialize the vector as follows:
     add:         [0,0,...,0,0]
     mult:        [1,1,...,1,1]
     min/max:     [init_val,init_val,..,init_val,init_val]
     bit and/or:  [init_val,init_val,..,init_val,init_val]
   and when necessary (e.g. add/mult case) let the caller know 
   that it needs to adjust the result by init_val.

   Option2: Initialize the vector as follows:
     add:         [0,0,...,0,init_val]
     mult:        [1,1,...,1,init_val]
     min/max:     [init_val,init_val,...,init_val]
     bit and/or:  [init_val,init_val,...,init_val]
   and no adjustments are needed.

   For example, for the following code:

   s = init_val;
   for (i=0;i<n;i++)
     s = s + a[i];

   STMT is 's = s + a[i]', and the reduction variable is 's'.
   For a vector of 4 units, we want to return either [0,0,0,init_val],
   or [0,0,0,0] and let the caller know that it needs to adjust
   the result at the end by 'init_val'.

   FORNOW: We use the "ADJUST_IN_EPILOG" scheme.
   TODO: Use some cost-model to estimate which scheme is more profitable.
*/

static tree
get_initial_def_for_reduction (tree stmt, tree init_val, tree *scalar_def)
{
  stmt_vec_info stmt_vinfo = vinfo_for_stmt (stmt);
  tree vectype = STMT_VINFO_VECTYPE (stmt_vinfo);
  int nunits = GET_MODE_NUNITS (TYPE_MODE (vectype));
  int nelements;
  enum tree_code code = TREE_CODE (TREE_OPERAND (stmt, 1));
  tree type = TREE_TYPE (init_val);
  tree def;
  tree vec, t = NULL_TREE;
  bool need_epilog_adjust;
  int i;

  gcc_assert (INTEGRAL_TYPE_P (type) || SCALAR_FLOAT_TYPE_P (type));

  switch (code)
  {
  case WIDEN_SUM_EXPR:
  case DOT_PROD_EXPR:
  case PLUS_EXPR:
    if (INTEGRAL_TYPE_P (type))
      def = build_int_cst (type, 0);
    else
      def = build_real (type, dconst0);

#ifdef ADJUST_IN_EPILOG
    /* All the 'nunits' elements are set to 0. The final result will be
       adjusted by 'init_val' at the loop epilog.  */
    nelements = nunits;
    need_epilog_adjust = true;
#else
    /* 'nunits - 1' elements are set to 0; The last element is set to 
        'init_val'.  No further adjustments at the epilog are needed.  */
    nelements = nunits - 1;
    need_epilog_adjust = false;
#endif
    break;

  case MIN_EXPR:
  case MAX_EXPR:
    def = init_val;
    nelements = nunits;
    need_epilog_adjust = false;
    break;

  default:
    gcc_unreachable ();
  }

  for (i = nelements - 1; i >= 0; --i)
    t = tree_cons (NULL_TREE, def, t);

  if (nelements == nunits - 1)
    {
      /* Set the last element of the vector.  */
      t = tree_cons (NULL_TREE, init_val, t);
      nelements += 1;
    }
  gcc_assert (nelements == nunits);
  
  if (TREE_CODE (init_val) == INTEGER_CST || TREE_CODE (init_val) == REAL_CST)
    vec = build_vector (vectype, t);
  else
    vec = build_constructor_from_list (vectype, t);
    
  if (!need_epilog_adjust)
    *scalar_def = NULL_TREE;
  else
    *scalar_def = init_val;

  return vect_init_vector (stmt, vec);
}


/* Function vect_create_epilog_for_reduction
    
   Create code at the loop-epilog to finalize the result of a reduction
   computation. 
  
   VECT_DEF is a vector of partial results. 
   REDUC_CODE is the tree-code for the epilog reduction.
   STMT is the scalar reduction stmt that is being vectorized.
   REDUCTION_PHI is the phi-node that carries the reduction computation.

   This function:
   1. Creates the reduction def-use cycle: sets the the arguments for 
      REDUCTION_PHI:
      The loop-entry argument is the vectorized initial-value of the reduction.
      The loop-latch argument is VECT_DEF - the vector of partial sums.
   2. "Reduces" the vector of partial results VECT_DEF into a single result,
      by applying the operation specified by REDUC_CODE if available, or by 
      other means (whole-vector shifts or a scalar loop).
      The function also creates a new phi node at the loop exit to preserve 
      loop-closed form, as illustrated below.
  
     The flow at the entry to this function:
    
        loop:
          vec_def = phi <null, null>            # REDUCTION_PHI
          VECT_DEF = vector_stmt                # vectorized form of STMT       
          s_loop = scalar_stmt                  # (scalar) STMT
        loop_exit:
          s_out0 = phi <s_loop>                 # (scalar) EXIT_PHI
          use <s_out0>
          use <s_out0>

     The above is transformed by this function into:

        loop:
          vec_def = phi <vec_init, VECT_DEF>    # REDUCTION_PHI
          VECT_DEF = vector_stmt                # vectorized form of STMT
          s_loop = scalar_stmt                  # (scalar) STMT 
        loop_exit:
          s_out0 = phi <s_loop>                 # (scalar) EXIT_PHI
          v_out1 = phi <VECT_DEF>               # NEW_EXIT_PHI
          v_out2 = reduce <v_out1>
          s_out3 = extract_field <v_out2, 0>
          s_out4 = adjust_result <s_out3>
          use <s_out4>
          use <s_out4>
*/

static void
vect_create_epilog_for_reduction (tree vect_def, tree stmt,
                                  enum tree_code reduc_code, tree reduction_phi)
{
  stmt_vec_info stmt_info = vinfo_for_stmt (stmt);
  tree vectype;
  enum machine_mode mode;
  loop_vec_info loop_vinfo = STMT_VINFO_LOOP_VINFO (stmt_info);
  struct loop *loop = LOOP_VINFO_LOOP (loop_vinfo);
  basic_block exit_bb;
  tree scalar_dest;
  tree scalar_type;
  tree new_phi;
  block_stmt_iterator exit_bsi;
  tree vec_dest;
  tree new_temp;
  tree new_name;
  tree epilog_stmt;
  tree new_scalar_dest, exit_phi;
  tree bitsize, bitpos, bytesize; 
  enum tree_code code = TREE_CODE (TREE_OPERAND (stmt, 1));
  tree scalar_initial_def;
  tree vec_initial_def;
  tree orig_name;
  imm_use_iterator imm_iter;
  use_operand_p use_p;
  bool extract_scalar_result;
  tree reduction_op;
  tree orig_stmt;
  tree use_stmt;
  tree operation = TREE_OPERAND (stmt, 1);
  int op_type;
  
  op_type = TREE_CODE_LENGTH (TREE_CODE (operation));
  reduction_op = TREE_OPERAND (operation, op_type-1);
  vectype = get_vectype_for_scalar_type (TREE_TYPE (reduction_op));
  mode = TYPE_MODE (vectype);

  /*** 1. Create the reduction def-use cycle  ***/
  
  /* 1.1 set the loop-entry arg of the reduction-phi:  */
  /* For the case of reduction, vect_get_vec_def_for_operand returns
     the scalar def before the loop, that defines the initial value
     of the reduction variable.  */
  vec_initial_def = vect_get_vec_def_for_operand (reduction_op, stmt,
						  &scalar_initial_def);
  add_phi_arg (reduction_phi, vec_initial_def, loop_preheader_edge (loop));

  /* 1.2 set the loop-latch arg for the reduction-phi:  */
  add_phi_arg (reduction_phi, vect_def, loop_latch_edge (loop));

  if (vect_print_dump_info (REPORT_DETAILS))
    {
      fprintf (vect_dump, "transform reduction: created def-use cycle:");
      print_generic_expr (vect_dump, reduction_phi, TDF_SLIM);
      fprintf (vect_dump, "\n");
      print_generic_expr (vect_dump, SSA_NAME_DEF_STMT (vect_def), TDF_SLIM);
    }


  /*** 2. Create epilog code
	  The reduction epilog code operates across the elements of the vector
          of partial results computed by the vectorized loop.
          The reduction epilog code consists of:
          step 1: compute the scalar result in a vector (v_out2)
          step 2: extract the scalar result (s_out3) from the vector (v_out2)
          step 3: adjust the scalar result (s_out3) if needed.

          Step 1 can be accomplished using one the following three schemes:
          (scheme 1) using reduc_code, if available.
          (scheme 2) using whole-vector shifts, if available.
          (scheme 3) using a scalar loop. In this case steps 1+2 above are 
                     combined.
                
          The overall epilog code looks like this:

          s_out0 = phi <s_loop>         # original EXIT_PHI
          v_out1 = phi <VECT_DEF>       # NEW_EXIT_PHI
          v_out2 = reduce <v_out1>              # step 1
          s_out3 = extract_field <v_out2, 0>    # step 2
          s_out4 = adjust_result <s_out3>       # step 3

          (step 3 is optional, and step2 1 and 2 may be combined).
          Lastly, the uses of s_out0 are replaced by s_out4.

	  ***/

  /* 2.1 Create new loop-exit-phi to preserve loop-closed form:
        v_out1 = phi <v_loop>  */

  exit_bb = loop->single_exit->dest;
  new_phi = create_phi_node (SSA_NAME_VAR (vect_def), exit_bb);
  SET_PHI_ARG_DEF (new_phi, loop->single_exit->dest_idx, vect_def);
  exit_bsi = bsi_start (exit_bb);

  /* 2.2 Get the relevant tree-code to use in the epilog for schemes 2,3 
         (i.e. when reduc_code is not available) and in the final adjustment code
         (if needed).  Also get the original scalar reduction variable as
         defined in the loop.  In case STMT is a "pattern-stmt" (i.e. - it 
         represents a reduction pattern), the tree-code and scalar-def are 
         taken from the original stmt that the pattern-stmt (STMT) replaces.  
         Otherwise (it is a regular reduction) - the tree-code and scalar-def
         are taken from STMT.  */ 

  orig_stmt = STMT_VINFO_RELATED_STMT (stmt_info);
  if (!orig_stmt)
    {
      /* Regular reduction  */
      orig_stmt = stmt;
    }
  else
    {
      /* Reduction pattern  */
      stmt_vec_info stmt_vinfo = vinfo_for_stmt (orig_stmt);
      gcc_assert (STMT_VINFO_IN_PATTERN_P (stmt_vinfo));
      gcc_assert (STMT_VINFO_RELATED_STMT (stmt_vinfo) == stmt);
    }
  code = TREE_CODE (TREE_OPERAND (orig_stmt, 1));
  scalar_dest = TREE_OPERAND (orig_stmt, 0);
  scalar_type = TREE_TYPE (scalar_dest);
  new_scalar_dest = vect_create_destination_var (scalar_dest, NULL);
  bitsize = TYPE_SIZE (scalar_type);
  bytesize = TYPE_SIZE_UNIT (scalar_type);

  /* 2.3 Create the reduction code, using one of the three schemes described
         above.  */

  if (reduc_code < NUM_TREE_CODES)
    {
      /*** Case 1:  Create:
	   v_out2 = reduc_expr <v_out1>  */

      if (vect_print_dump_info (REPORT_DETAILS))
	fprintf (vect_dump, "Reduce using direct vector reduction.");

      vec_dest = vect_create_destination_var (scalar_dest, vectype);
      epilog_stmt = build2 (MODIFY_EXPR, vectype, vec_dest,
			build1 (reduc_code, vectype,  PHI_RESULT (new_phi)));
      new_temp = make_ssa_name (vec_dest, epilog_stmt);
      TREE_OPERAND (epilog_stmt, 0) = new_temp;
      bsi_insert_after (&exit_bsi, epilog_stmt, BSI_NEW_STMT);

      extract_scalar_result = true;
    }
  else
    {
      enum tree_code shift_code = 0;
      bool have_whole_vector_shift = true;
      int bit_offset;
      int element_bitsize = tree_low_cst (bitsize, 1);
      int vec_size_in_bits = tree_low_cst (TYPE_SIZE (vectype), 1);
      tree vec_temp;

      if (vec_shr_optab->handlers[mode].insn_code != CODE_FOR_nothing)
	shift_code = VEC_RSHIFT_EXPR;
      else
	have_whole_vector_shift = false;

      /* Regardless of whether we have a whole vector shift, if we're
	 emulating the operation via tree-vect-generic, we don't want
	 to use it.  Only the first round of the reduction is likely
	 to still be profitable via emulation.  */
      /* ??? It might be better to emit a reduction tree code here, so that
	 tree-vect-generic can expand the first round via bit tricks.  */
      if (!VECTOR_MODE_P (mode))
	have_whole_vector_shift = false;
      else
	{
	  optab optab = optab_for_tree_code (code, vectype);
	  if (optab->handlers[mode].insn_code == CODE_FOR_nothing)
	    have_whole_vector_shift = false;
	}

      if (have_whole_vector_shift)
        {
	  /*** Case 2: Create:
	     for (offset = VS/2; offset >= element_size; offset/=2)
	        {
	          Create:  va' = vec_shift <va, offset>
	          Create:  va = vop <va, va'>
	        }  */

	  if (vect_print_dump_info (REPORT_DETAILS))
	    fprintf (vect_dump, "Reduce using vector shifts");

	  vec_dest = vect_create_destination_var (scalar_dest, vectype);
	  new_temp = PHI_RESULT (new_phi);

	  for (bit_offset = vec_size_in_bits/2;
	       bit_offset >= element_bitsize;
	       bit_offset /= 2)
	    {
	      tree bitpos = size_int (bit_offset);

	      epilog_stmt = build2 (MODIFY_EXPR, vectype, vec_dest,
	      build2 (shift_code, vectype, new_temp, bitpos));
	      new_name = make_ssa_name (vec_dest, epilog_stmt);
	      TREE_OPERAND (epilog_stmt, 0) = new_name;
	      bsi_insert_after (&exit_bsi, epilog_stmt, BSI_NEW_STMT);

	      epilog_stmt = build2 (MODIFY_EXPR, vectype, vec_dest,
	      build2 (code, vectype, new_name, new_temp));
	      new_temp = make_ssa_name (vec_dest, epilog_stmt);
	      TREE_OPERAND (epilog_stmt, 0) = new_temp;
	      bsi_insert_after (&exit_bsi, epilog_stmt, BSI_NEW_STMT);
	    }

	  extract_scalar_result = true;
	}
      else
        {
	  tree rhs;

	  /*** Case 3: Create:  
	     s = extract_field <v_out2, 0>
	     for (offset = element_size; 
		  offset < vector_size; 
		  offset += element_size;)
	       {
	         Create:  s' = extract_field <v_out2, offset>
	         Create:  s = op <s, s'>
	       }  */

	  if (vect_print_dump_info (REPORT_DETAILS))
	    fprintf (vect_dump, "Reduce using scalar code. ");

	  vec_temp = PHI_RESULT (new_phi);
	  vec_size_in_bits = tree_low_cst (TYPE_SIZE (vectype), 1);
	  rhs = build3 (BIT_FIELD_REF, scalar_type, vec_temp, bitsize,
			 bitsize_zero_node);
	  BIT_FIELD_REF_UNSIGNED (rhs) = TYPE_UNSIGNED (scalar_type);
	  epilog_stmt = build2 (MODIFY_EXPR, scalar_type, new_scalar_dest, rhs);
	  new_temp = make_ssa_name (new_scalar_dest, epilog_stmt);
	  TREE_OPERAND (epilog_stmt, 0) = new_temp;
	  bsi_insert_after (&exit_bsi, epilog_stmt, BSI_NEW_STMT);
	      
	  for (bit_offset = element_bitsize;
	       bit_offset < vec_size_in_bits;
	       bit_offset += element_bitsize)
	    { 
	      tree bitpos = bitsize_int (bit_offset);
	      tree rhs = build3 (BIT_FIELD_REF, scalar_type, vec_temp, bitsize,
				 bitpos);
		
	      BIT_FIELD_REF_UNSIGNED (rhs) = TYPE_UNSIGNED (scalar_type);
	      epilog_stmt = build2 (MODIFY_EXPR, scalar_type, new_scalar_dest,
				    rhs);	
	      new_name = make_ssa_name (new_scalar_dest, epilog_stmt);
	      TREE_OPERAND (epilog_stmt, 0) = new_name;
	      bsi_insert_after (&exit_bsi, epilog_stmt, BSI_NEW_STMT);

	      epilog_stmt = build2 (MODIFY_EXPR, scalar_type, new_scalar_dest,
				build2 (code, scalar_type, new_name, new_temp));
	      new_temp = make_ssa_name (new_scalar_dest, epilog_stmt);
	      TREE_OPERAND (epilog_stmt, 0) = new_temp;
	      bsi_insert_after (&exit_bsi, epilog_stmt, BSI_NEW_STMT);
	    }

	  extract_scalar_result = false;
	}
    }

  /* 2.4  Extract the final scalar result.  Create:
         s_out3 = extract_field <v_out2, bitpos>  */
  
  if (extract_scalar_result)
    {
      tree rhs;

      if (vect_print_dump_info (REPORT_DETAILS))
	fprintf (vect_dump, "extract scalar result");

      if (BYTES_BIG_ENDIAN)
	bitpos = size_binop (MULT_EXPR,
		       bitsize_int (TYPE_VECTOR_SUBPARTS (vectype) - 1),
		       TYPE_SIZE (scalar_type));
      else
	bitpos = bitsize_zero_node;

      rhs = build3 (BIT_FIELD_REF, scalar_type, new_temp, bitsize, bitpos);
      BIT_FIELD_REF_UNSIGNED (rhs) = TYPE_UNSIGNED (scalar_type);
      epilog_stmt = build2 (MODIFY_EXPR, scalar_type, new_scalar_dest, rhs);
      new_temp = make_ssa_name (new_scalar_dest, epilog_stmt);
      TREE_OPERAND (epilog_stmt, 0) = new_temp; 
      bsi_insert_after (&exit_bsi, epilog_stmt, BSI_NEW_STMT);
    }

  /* 2.4 Adjust the final result by the initial value of the reduction
	 variable. (When such adjustment is not needed, then
	 'scalar_initial_def' is zero).

	 Create: 
	 s_out4 = scalar_expr <s_out3, scalar_initial_def>  */
  
  if (scalar_initial_def)
    {
      epilog_stmt = build2 (MODIFY_EXPR, scalar_type, new_scalar_dest,
                      build2 (code, scalar_type, new_temp, scalar_initial_def));
      new_temp = make_ssa_name (new_scalar_dest, epilog_stmt);
      TREE_OPERAND (epilog_stmt, 0) = new_temp;
      bsi_insert_after (&exit_bsi, epilog_stmt, BSI_NEW_STMT);
    }

  /* 2.6 Replace uses of s_out0 with uses of s_out3  */

  /* Find the loop-closed-use at the loop exit of the original scalar result.  
     (The reduction result is expected to have two immediate uses - one at the 
     latch block, and one at the loop exit).  */
  exit_phi = NULL;
  FOR_EACH_IMM_USE_FAST (use_p, imm_iter, scalar_dest)
    {
      if (!flow_bb_inside_loop_p (loop, bb_for_stmt (USE_STMT (use_p))))
	{
	  exit_phi = USE_STMT (use_p);
	  break;
	}
    }
  /* We expect to have found an exit_phi because of loop-closed-ssa form.  */
  gcc_assert (exit_phi);
  /* Replace the uses:  */
  orig_name = PHI_RESULT (exit_phi);
  FOR_EACH_IMM_USE_STMT (use_stmt, imm_iter, orig_name)
    FOR_EACH_IMM_USE_ON_STMT (use_p, imm_iter)
      SET_USE (use_p, new_temp);
} 


/* Function vectorizable_reduction.

   Check if STMT performs a reduction operation that can be vectorized.
   If VEC_STMT is also passed, vectorize the STMT: create a vectorized
   stmt to replace it, put it in VEC_STMT, and insert it at BSI.
   Return FALSE if not a vectorizable STMT, TRUE otherwise.

   This function also handles reduction idioms (patterns) that have been 
   recognized in advance during vect_pattern_recog. In this case, STMT may be
   of this form:
     X = pattern_expr (arg0, arg1, ..., X)
   and it's STMT_VINFO_RELATED_STMT points to the last stmt in the original
   sequence that had been detected and replaced by the pattern-stmt (STMT).
  
   In some cases of reduction patterns, the type of the reduction variable X is 
   different than the type of the other arguments of STMT.
   In such cases, the vectype that is used when transforming STMT into a vector
   stmt is different than the vectype that is used to determine the 
   vectorization factor, because it consists of a different number of elements 
   than the actual number of elements that are being operated upon in parallel.

   For example, consider an accumulation of shorts into an int accumulator. 
   On some targets it's possible to vectorize this pattern operating on 8
   shorts at a time (hence, the vectype for purposes of determining the
   vectorization factor should be V8HI); on the other hand, the vectype that
   is used to create the vector form is actually V4SI (the type of the result). 

   Upon entry to this function, STMT_VINFO_VECTYPE records the vectype that 
   indicates what is the actual level of parallelism (V8HI in the example), so 
   that the right vectorization factor would be derived. This vectype 
   corresponds to the type of arguments to the reduction stmt, and should *NOT* 
   be used to create the vectorized stmt. The right vectype for the vectorized
   stmt is obtained from the type of the result X: 
        get_vectype_for_scalar_type (TREE_TYPE (X))

   This means that, contrary to "regular" reductions (or "regular" stmts in 
   general), the following equation:
      STMT_VINFO_VECTYPE == get_vectype_for_scalar_type (TREE_TYPE (X))
   does *NOT* necessarily hold for reduction patterns.  */

bool
vectorizable_reduction (tree stmt, block_stmt_iterator *bsi, tree *vec_stmt)
{
  tree vec_dest;
  tree scalar_dest;
  tree op;
  tree loop_vec_def0, loop_vec_def1;
  stmt_vec_info stmt_info = vinfo_for_stmt (stmt);
  tree vectype = STMT_VINFO_VECTYPE (stmt_info);
  loop_vec_info loop_vinfo = STMT_VINFO_LOOP_VINFO (stmt_info);
  struct loop *loop = LOOP_VINFO_LOOP (loop_vinfo);
  tree operation;
  enum tree_code code, orig_code, epilog_reduc_code = 0;
  enum machine_mode vec_mode;
  int op_type;
  optab optab, reduc_optab;
  tree new_temp;
  tree def, def_stmt;
  enum vect_def_type dt;
  tree new_phi;
  tree scalar_type;
  bool is_simple_use;
  tree orig_stmt;
  stmt_vec_info orig_stmt_info;
  tree expr = NULL_TREE;
  int i;

  /* 1. Is vectorizable reduction?  */

  /* Not supportable if the reduction variable is used in the loop.  */
  if (STMT_VINFO_RELEVANT_P (stmt_info))
    return false;

  if (!STMT_VINFO_LIVE_P (stmt_info))
    return false;

  /* Make sure it was already recognized as a reduction computation.  */
  if (STMT_VINFO_DEF_TYPE (stmt_info) != vect_reduction_def)
    return false;

  /* 2. Has this been recognized as a reduction pattern? 

     Check if STMT represents a pattern that has been recognized
     in earlier analysis stages.  For stmts that represent a pattern,
     the STMT_VINFO_RELATED_STMT field records the last stmt in
     the original sequence that constitutes the pattern.  */

  orig_stmt = STMT_VINFO_RELATED_STMT (stmt_info);
  if (orig_stmt)
    {
      orig_stmt_info = vinfo_for_stmt (orig_stmt);
      gcc_assert (STMT_VINFO_RELATED_STMT (orig_stmt_info) == stmt);
      gcc_assert (STMT_VINFO_IN_PATTERN_P (orig_stmt_info));
      gcc_assert (!STMT_VINFO_IN_PATTERN_P (stmt_info));
    }
 
  /* 3. Check the operands of the operation. The first operands are defined
        inside the loop body. The last operand is the reduction variable,
        which is defined by the loop-header-phi.  */

  gcc_assert (TREE_CODE (stmt) == MODIFY_EXPR);

  operation = TREE_OPERAND (stmt, 1);
  code = TREE_CODE (operation);
  op_type = TREE_CODE_LENGTH (code);

  if (op_type != binary_op && op_type != ternary_op)
    return false;
  scalar_dest = TREE_OPERAND (stmt, 0);
  scalar_type = TREE_TYPE (scalar_dest);

  /* All uses but the last are expected to be defined in the loop.
     The last use is the reduction variable.  */
  for (i = 0; i < op_type-1; i++)
    {
      op = TREE_OPERAND (operation, i);
      is_simple_use = vect_is_simple_use (op, loop_vinfo, &def_stmt, &def, &dt);
      gcc_assert (is_simple_use);
      gcc_assert (dt == vect_loop_def || dt == vect_invariant_def ||
                  dt == vect_constant_def);
    }

  op = TREE_OPERAND (operation, i);
  is_simple_use = vect_is_simple_use (op, loop_vinfo, &def_stmt, &def, &dt);
  gcc_assert (is_simple_use);
  gcc_assert (dt == vect_reduction_def);
  gcc_assert (TREE_CODE (def_stmt) == PHI_NODE);
  if (orig_stmt) 
    gcc_assert (orig_stmt == vect_is_simple_reduction (loop, def_stmt));
  else
    gcc_assert (stmt == vect_is_simple_reduction (loop, def_stmt));
  
  if (STMT_VINFO_LIVE_P (vinfo_for_stmt (def_stmt)))
    return false;

  /* 4. Supportable by target?  */

  /* 4.1. check support for the operation in the loop  */
  optab = optab_for_tree_code (code, vectype);
  if (!optab)
    {
      if (vect_print_dump_info (REPORT_DETAILS))
        fprintf (vect_dump, "no optab.");
      return false;
    }
  vec_mode = TYPE_MODE (vectype);
  if (optab->handlers[(int) vec_mode].insn_code == CODE_FOR_nothing)
    {
      if (vect_print_dump_info (REPORT_DETAILS))
        fprintf (vect_dump, "op not supported by target.");
      if (GET_MODE_SIZE (vec_mode) != UNITS_PER_WORD
          || LOOP_VINFO_VECT_FACTOR (loop_vinfo)
	     < vect_min_worthwhile_factor (code))
        return false;
      if (vect_print_dump_info (REPORT_DETAILS))
	fprintf (vect_dump, "proceeding using word mode.");
    }

  /* Worthwhile without SIMD support?  */
  if (!VECTOR_MODE_P (TYPE_MODE (vectype))
      && LOOP_VINFO_VECT_FACTOR (loop_vinfo)
	 < vect_min_worthwhile_factor (code))
    {
      if (vect_print_dump_info (REPORT_DETAILS))
	fprintf (vect_dump, "not worthwhile without SIMD support.");
      return false;
    }

  /* 4.2. Check support for the epilog operation.

          If STMT represents a reduction pattern, then the type of the
          reduction variable may be different than the type of the rest
          of the arguments.  For example, consider the case of accumulation
          of shorts into an int accumulator; The original code:
                        S1: int_a = (int) short_a;
          orig_stmt->   S2: int_acc = plus <int_a ,int_acc>;

          was replaced with:
                        STMT: int_acc = widen_sum <short_a, int_acc>

          This means that:
          1. The tree-code that is used to create the vector operation in the 
             epilog code (that reduces the partial results) is not the 
             tree-code of STMT, but is rather the tree-code of the original 
             stmt from the pattern that STMT is replacing. I.e, in the example 
             above we want to use 'widen_sum' in the loop, but 'plus' in the 
             epilog.
          2. The type (mode) we use to check available target support
             for the vector operation to be created in the *epilog*, is 
             determined by the type of the reduction variable (in the example 
             above we'd check this: plus_optab[vect_int_mode]).
             However the type (mode) we use to check available target support
             for the vector operation to be created *inside the loop*, is
             determined by the type of the other arguments to STMT (in the
             example we'd check this: widen_sum_optab[vect_short_mode]).
  
          This is contrary to "regular" reductions, in which the types of all 
          the arguments are the same as the type of the reduction variable. 
          For "regular" reductions we can therefore use the same vector type 
          (and also the same tree-code) when generating the epilog code and
          when generating the code inside the loop.  */

  if (orig_stmt)
    {
      /* This is a reduction pattern: get the vectype from the type of the
         reduction variable, and get the tree-code from orig_stmt.  */
      orig_code = TREE_CODE (TREE_OPERAND (orig_stmt, 1));
      vectype = get_vectype_for_scalar_type (TREE_TYPE (def));
      vec_mode = TYPE_MODE (vectype);
    }
  else
    {
      /* Regular reduction: use the same vectype and tree-code as used for
         the vector code inside the loop can be used for the epilog code. */
      orig_code = code;
    }

  if (!reduction_code_for_scalar_code (orig_code, &epilog_reduc_code))
    return false;
  reduc_optab = optab_for_tree_code (epilog_reduc_code, vectype);
  if (!reduc_optab)
    {
      if (vect_print_dump_info (REPORT_DETAILS))
        fprintf (vect_dump, "no optab for reduction.");
      epilog_reduc_code = NUM_TREE_CODES;
    }
  if (reduc_optab->handlers[(int) vec_mode].insn_code == CODE_FOR_nothing)
    {
      if (vect_print_dump_info (REPORT_DETAILS))
        fprintf (vect_dump, "reduc op not supported by target.");
      epilog_reduc_code = NUM_TREE_CODES;
    }
 
  if (!vec_stmt) /* transformation not required.  */
    {
      STMT_VINFO_TYPE (stmt_info) = reduc_vec_info_type;
      return true;
    }

  /** Transform.  **/

  if (vect_print_dump_info (REPORT_DETAILS))
    fprintf (vect_dump, "transform reduction.");

  /* Create the destination vector  */
  vec_dest = vect_create_destination_var (scalar_dest, vectype);

  /* Create the reduction-phi that defines the reduction-operand.  */
  new_phi = create_phi_node (vec_dest, loop->header);

  /* Prepare the operand that is defined inside the loop body  */
  op = TREE_OPERAND (operation, 0);
  loop_vec_def0 = vect_get_vec_def_for_operand (op, stmt, NULL);
  if (op_type == binary_op)
    expr = build2 (code, vectype, loop_vec_def0, PHI_RESULT (new_phi));
  else if (op_type == ternary_op)
    {
      op = TREE_OPERAND (operation, 1);
      loop_vec_def1 = vect_get_vec_def_for_operand (op, stmt, NULL);
      expr = build3 (code, vectype, loop_vec_def0, loop_vec_def1, 
		     PHI_RESULT (new_phi));
    }

  /* Create the vectorized operation that computes the partial results  */
  *vec_stmt = build2 (MODIFY_EXPR, vectype, vec_dest, expr);
  new_temp = make_ssa_name (vec_dest, *vec_stmt);
  TREE_OPERAND (*vec_stmt, 0) = new_temp;
  vect_finish_stmt_generation (stmt, *vec_stmt, bsi);

  /* Finalize the reduction-phi (set it's arguments) and create the
     epilog reduction code.  */
  vect_create_epilog_for_reduction (new_temp, stmt, epilog_reduc_code, new_phi);
  return true;
}


/* Function vectorizable_assignment.

   Check if STMT performs an assignment (copy) that can be vectorized. 
   If VEC_STMT is also passed, vectorize the STMT: create a vectorized 
   stmt to replace it, put it in VEC_STMT, and insert it at BSI.
   Return FALSE if not a vectorizable STMT, TRUE otherwise.  */

bool
vectorizable_assignment (tree stmt, block_stmt_iterator *bsi, tree *vec_stmt)
{
  tree vec_dest;
  tree scalar_dest;
  tree op;
  tree vec_oprnd;
  stmt_vec_info stmt_info = vinfo_for_stmt (stmt);
  tree vectype = STMT_VINFO_VECTYPE (stmt_info);
  loop_vec_info loop_vinfo = STMT_VINFO_LOOP_VINFO (stmt_info);
  tree new_temp;
  tree def, def_stmt;
  enum vect_def_type dt;

  /* Is vectorizable assignment?  */
  if (!STMT_VINFO_RELEVANT_P (stmt_info))
    return false;

  gcc_assert (STMT_VINFO_DEF_TYPE (stmt_info) == vect_loop_def);

  if (TREE_CODE (stmt) != MODIFY_EXPR)
    return false;

  scalar_dest = TREE_OPERAND (stmt, 0);
  if (TREE_CODE (scalar_dest) != SSA_NAME)
    return false;

  op = TREE_OPERAND (stmt, 1);
  if (!vect_is_simple_use (op, loop_vinfo, &def_stmt, &def, &dt))
    {
      if (vect_print_dump_info (REPORT_DETAILS))
        fprintf (vect_dump, "use not simple.");
      return false;
    }

  if (!vec_stmt) /* transformation not required.  */
    {
      STMT_VINFO_TYPE (stmt_info) = assignment_vec_info_type;
      return true;
    }

  /** Transform.  **/
  if (vect_print_dump_info (REPORT_DETAILS))
    fprintf (vect_dump, "transform assignment.");

  /* Handle def.  */
  vec_dest = vect_create_destination_var (scalar_dest, vectype);

  /* Handle use.  */
  op = TREE_OPERAND (stmt, 1);
  vec_oprnd = vect_get_vec_def_for_operand (op, stmt, NULL);

  /* Arguments are ready. create the new vector stmt.  */
  *vec_stmt = build2 (MODIFY_EXPR, vectype, vec_dest, vec_oprnd);
  new_temp = make_ssa_name (vec_dest, *vec_stmt);
  TREE_OPERAND (*vec_stmt, 0) = new_temp;
  vect_finish_stmt_generation (stmt, *vec_stmt, bsi);
  
  return true;
}


/* Function vect_min_worthwhile_factor.

   For a loop where we could vectorize the operation indicated by CODE,
   return the minimum vectorization factor that makes it worthwhile
   to use generic vectors.  */
static int
vect_min_worthwhile_factor (enum tree_code code)
{
  switch (code)
    {
    case PLUS_EXPR:
    case MINUS_EXPR:
    case NEGATE_EXPR:
      return 4;

    case BIT_AND_EXPR:
    case BIT_IOR_EXPR:
    case BIT_XOR_EXPR:
    case BIT_NOT_EXPR:
      return 2;

    default:
      return INT_MAX;
    }
}


/* Function vectorizable_operation.

   Check if STMT performs a binary or unary operation that can be vectorized. 
   If VEC_STMT is also passed, vectorize the STMT: create a vectorized 
   stmt to replace it, put it in VEC_STMT, and insert it at BSI.
   Return FALSE if not a vectorizable STMT, TRUE otherwise.  */

bool
vectorizable_operation (tree stmt, block_stmt_iterator *bsi, tree *vec_stmt)
{
  tree vec_dest;
  tree scalar_dest;
  tree operation;
  tree op0, op1 = NULL;
  tree vec_oprnd0, vec_oprnd1=NULL;
  stmt_vec_info stmt_info = vinfo_for_stmt (stmt);
  tree vectype = STMT_VINFO_VECTYPE (stmt_info);
  loop_vec_info loop_vinfo = STMT_VINFO_LOOP_VINFO (stmt_info);
  int i;
  enum tree_code code;
  enum machine_mode vec_mode;
  tree new_temp;
  int op_type;
  tree op;
  optab optab;
  int icode;
  enum machine_mode optab_op2_mode;
  tree def, def_stmt;
  enum vect_def_type dt;

  /* Is STMT a vectorizable binary/unary operation?   */
  if (!STMT_VINFO_RELEVANT_P (stmt_info))
    return false;

  gcc_assert (STMT_VINFO_DEF_TYPE (stmt_info) == vect_loop_def);

  if (STMT_VINFO_LIVE_P (stmt_info))
    {
      /* FORNOW: not yet supported.  */
      if (vect_print_dump_info (REPORT_DETAILS))
        fprintf (vect_dump, "value used after loop.");
      return false;
    }

  if (TREE_CODE (stmt) != MODIFY_EXPR)
    return false;

  if (TREE_CODE (TREE_OPERAND (stmt, 0)) != SSA_NAME)
    return false;

  operation = TREE_OPERAND (stmt, 1);
  code = TREE_CODE (operation);
  optab = optab_for_tree_code (code, vectype);

  /* Support only unary or binary operations.  */
  op_type = TREE_CODE_LENGTH (code);
  if (op_type != unary_op && op_type != binary_op)
    {
      if (vect_print_dump_info (REPORT_DETAILS))
	fprintf (vect_dump, "num. args = %d (not unary/binary op).", op_type);
      return false;
    }

  for (i = 0; i < op_type; i++)
    {
      op = TREE_OPERAND (operation, i);
      if (!vect_is_simple_use (op, loop_vinfo, &def_stmt, &def, &dt))
	{
	  if (vect_print_dump_info (REPORT_DETAILS))
	    fprintf (vect_dump, "use not simple.");
	  return false;
	}	
    } 

  /* Supportable by target?  */
  if (!optab)
    {
      if (vect_print_dump_info (REPORT_DETAILS))
	fprintf (vect_dump, "no optab.");
      return false;
    }
  vec_mode = TYPE_MODE (vectype);
  icode = (int) optab->handlers[(int) vec_mode].insn_code;
  if (icode == CODE_FOR_nothing)
    {
      if (vect_print_dump_info (REPORT_DETAILS))
	fprintf (vect_dump, "op not supported by target.");
      if (GET_MODE_SIZE (vec_mode) != UNITS_PER_WORD
          || LOOP_VINFO_VECT_FACTOR (loop_vinfo)
	     < vect_min_worthwhile_factor (code))
        return false;
      if (vect_print_dump_info (REPORT_DETAILS))
	fprintf (vect_dump, "proceeding using word mode.");
    }

  /* Worthwhile without SIMD support?  */
  if (!VECTOR_MODE_P (TYPE_MODE (vectype))
      && LOOP_VINFO_VECT_FACTOR (loop_vinfo)
	 < vect_min_worthwhile_factor (code))
    {
      if (vect_print_dump_info (REPORT_DETAILS))
	fprintf (vect_dump, "not worthwhile without SIMD support.");
      return false;
    }

  if (code == LSHIFT_EXPR || code == RSHIFT_EXPR)
    {
      /* FORNOW: not yet supported.  */
      if (!VECTOR_MODE_P (vec_mode))
	return false;

      /* Invariant argument is needed for a vector shift
	 by a scalar shift operand.  */
      optab_op2_mode = insn_data[icode].operand[2].mode;
      if (! (VECTOR_MODE_P (optab_op2_mode)
	     || dt == vect_constant_def
	     || dt == vect_invariant_def))
	{
	  if (vect_print_dump_info (REPORT_DETAILS))
	    fprintf (vect_dump, "operand mode requires invariant argument.");
	  return false;
	}
    }

  if (!vec_stmt) /* transformation not required.  */
    {
      STMT_VINFO_TYPE (stmt_info) = op_vec_info_type;
      return true;
    }

  /** Transform.  **/

  if (vect_print_dump_info (REPORT_DETAILS))
    fprintf (vect_dump, "transform binary/unary operation.");

  /* Handle def.  */
  scalar_dest = TREE_OPERAND (stmt, 0);
  vec_dest = vect_create_destination_var (scalar_dest, vectype);

  /* Handle uses.  */
  op0 = TREE_OPERAND (operation, 0);
  vec_oprnd0 = vect_get_vec_def_for_operand (op0, stmt, NULL);

  if (op_type == binary_op)
    {
      op1 = TREE_OPERAND (operation, 1);

      if (code == LSHIFT_EXPR || code == RSHIFT_EXPR)
	{
	  /* Vector shl and shr insn patterns can be defined with
	     scalar operand 2 (shift operand).  In this case, use
	     constant or loop invariant op1 directly, without
	     extending it to vector mode first.  */

	  optab_op2_mode = insn_data[icode].operand[2].mode;
	  if (!VECTOR_MODE_P (optab_op2_mode))
	    {
	      if (vect_print_dump_info (REPORT_DETAILS))
		fprintf (vect_dump, "operand 1 using scalar mode.");
	      vec_oprnd1 = op1;
	    }
	}

      if (!vec_oprnd1)
	vec_oprnd1 = vect_get_vec_def_for_operand (op1, stmt, NULL); 
    }

  /* Arguments are ready. create the new vector stmt.  */

  if (op_type == binary_op)
    *vec_stmt = build2 (MODIFY_EXPR, vectype, vec_dest,
		build2 (code, vectype, vec_oprnd0, vec_oprnd1));
  else
    *vec_stmt = build2 (MODIFY_EXPR, vectype, vec_dest,
		build1 (code, vectype, vec_oprnd0));
  new_temp = make_ssa_name (vec_dest, *vec_stmt);
  TREE_OPERAND (*vec_stmt, 0) = new_temp;
  vect_finish_stmt_generation (stmt, *vec_stmt, bsi);

  return true;
}


/* Function vectorizable_store.

   Check if STMT defines a non scalar data-ref (array/pointer/structure) that 
   can be vectorized. 
   If VEC_STMT is also passed, vectorize the STMT: create a vectorized 
   stmt to replace it, put it in VEC_STMT, and insert it at BSI.
   Return FALSE if not a vectorizable STMT, TRUE otherwise.  */

bool
vectorizable_store (tree stmt, block_stmt_iterator *bsi, tree *vec_stmt)
{
  tree scalar_dest;
  tree data_ref;
  tree op;
  tree vec_oprnd1;
  stmt_vec_info stmt_info = vinfo_for_stmt (stmt);
  struct data_reference *dr = STMT_VINFO_DATA_REF (stmt_info);
  tree vectype = STMT_VINFO_VECTYPE (stmt_info);
  loop_vec_info loop_vinfo = STMT_VINFO_LOOP_VINFO (stmt_info);
  enum machine_mode vec_mode;
  tree dummy;
  enum dr_alignment_support alignment_support_cheme;
  ssa_op_iter iter;
  tree def, def_stmt;
  enum vect_def_type dt;

  /* Is vectorizable store? */

  if (TREE_CODE (stmt) != MODIFY_EXPR)
    return false;

  scalar_dest = TREE_OPERAND (stmt, 0);
  if (TREE_CODE (scalar_dest) != ARRAY_REF
      && TREE_CODE (scalar_dest) != INDIRECT_REF)
    return false;

  op = TREE_OPERAND (stmt, 1);
  if (!vect_is_simple_use (op, loop_vinfo, &def_stmt, &def, &dt))
    {
      if (vect_print_dump_info (REPORT_DETAILS))
        fprintf (vect_dump, "use not simple.");
      return false;
    }

  vec_mode = TYPE_MODE (vectype);
  /* FORNOW. In some cases can vectorize even if data-type not supported
     (e.g. - array initialization with 0).  */
  if (mov_optab->handlers[(int)vec_mode].insn_code == CODE_FOR_nothing)
    return false;

  if (!STMT_VINFO_DATA_REF (stmt_info))
    return false;


  if (!vec_stmt) /* transformation not required.  */
    {
      STMT_VINFO_TYPE (stmt_info) = store_vec_info_type;
      return true;
    }

  /** Transform.  **/

  if (vect_print_dump_info (REPORT_DETAILS))
    fprintf (vect_dump, "transform store");

  alignment_support_cheme = vect_supportable_dr_alignment (dr);
  gcc_assert (alignment_support_cheme);
  gcc_assert (alignment_support_cheme == dr_aligned);  /* FORNOW */

  /* Handle use - get the vectorized def from the defining stmt.  */
  vec_oprnd1 = vect_get_vec_def_for_operand (op, stmt, NULL);

  /* Handle def.  */
  /* FORNOW: make sure the data reference is aligned.  */
  vect_align_data_ref (stmt);
  data_ref = vect_create_data_ref_ptr (stmt, bsi, NULL_TREE, &dummy, false);
  data_ref = build_fold_indirect_ref (data_ref);

  /* Arguments are ready. create the new vector stmt.  */
  *vec_stmt = build2 (MODIFY_EXPR, vectype, data_ref, vec_oprnd1);
  vect_finish_stmt_generation (stmt, *vec_stmt, bsi);

  /* Copy the V_MAY_DEFS representing the aliasing of the original array
     element's definition to the vector's definition then update the
     defining statement.  The original is being deleted so the same
     SSA_NAMEs can be used.  */
  copy_virtual_operands (*vec_stmt, stmt);

  FOR_EACH_SSA_TREE_OPERAND (def, stmt, iter, SSA_OP_VMAYDEF)
    {
      SSA_NAME_DEF_STMT (def) = *vec_stmt;

      /* If this virtual def has a use outside the loop and a loop peel is 
	 performed then the def may be renamed by the peel.  Mark it for 
	 renaming so the later use will also be renamed.  */
      mark_sym_for_renaming (SSA_NAME_VAR (def));
    }

  return true;
}


/* vectorizable_load.

   Check if STMT reads a non scalar data-ref (array/pointer/structure) that 
   can be vectorized. 
   If VEC_STMT is also passed, vectorize the STMT: create a vectorized 
   stmt to replace it, put it in VEC_STMT, and insert it at BSI.
   Return FALSE if not a vectorizable STMT, TRUE otherwise.  */

bool
vectorizable_load (tree stmt, block_stmt_iterator *bsi, tree *vec_stmt)
{
  tree scalar_dest;
  tree vec_dest = NULL;
  tree data_ref = NULL;
  tree op;
  stmt_vec_info stmt_info = vinfo_for_stmt (stmt);
  struct data_reference *dr = STMT_VINFO_DATA_REF (stmt_info);
  tree vectype = STMT_VINFO_VECTYPE (stmt_info);
  tree new_temp;
  int mode;
  tree init_addr;
  tree new_stmt;
  tree dummy;
  basic_block new_bb;
  loop_vec_info loop_vinfo = STMT_VINFO_LOOP_VINFO (stmt_info);
  struct loop *loop = LOOP_VINFO_LOOP (loop_vinfo);
  edge pe = loop_preheader_edge (loop);
  enum dr_alignment_support alignment_support_cheme;

  /* Is vectorizable load? */
  if (!STMT_VINFO_RELEVANT_P (stmt_info))
    return false;

  gcc_assert (STMT_VINFO_DEF_TYPE (stmt_info) == vect_loop_def);

  if (STMT_VINFO_LIVE_P (stmt_info))
    {
      /* FORNOW: not yet supported.  */
      if (vect_print_dump_info (REPORT_DETAILS))
        fprintf (vect_dump, "value used after loop.");
      return false;
    }

  if (TREE_CODE (stmt) != MODIFY_EXPR)
    return false;

  scalar_dest = TREE_OPERAND (stmt, 0);
  if (TREE_CODE (scalar_dest) != SSA_NAME)
    return false;

  op = TREE_OPERAND (stmt, 1);
  if (TREE_CODE (op) != ARRAY_REF && TREE_CODE (op) != INDIRECT_REF)
    return false;

  if (!STMT_VINFO_DATA_REF (stmt_info))
    return false;

  mode = (int) TYPE_MODE (vectype);

  /* FORNOW. In some cases can vectorize even if data-type not supported
    (e.g. - data copies).  */
  if (mov_optab->handlers[mode].insn_code == CODE_FOR_nothing)
    {
      if (vect_print_dump_info (REPORT_DETAILS))
	fprintf (vect_dump, "Aligned load, but unsupported type.");
      return false;
    }

  if (!vec_stmt) /* transformation not required.  */
    {
      STMT_VINFO_TYPE (stmt_info) = load_vec_info_type;
      return true;
    }

  /** Transform.  **/

  if (vect_print_dump_info (REPORT_DETAILS))
    fprintf (vect_dump, "transform load.");

  alignment_support_cheme = vect_supportable_dr_alignment (dr);
  gcc_assert (alignment_support_cheme);

  if (alignment_support_cheme == dr_aligned
      || alignment_support_cheme == dr_unaligned_supported)
    {
      /* Create:
         p = initial_addr;
         indx = 0;
         loop {
           vec_dest = *(p);
           indx = indx + 1;
         }
      */

      vec_dest = vect_create_destination_var (scalar_dest, vectype);
      data_ref = vect_create_data_ref_ptr (stmt, bsi, NULL_TREE, &dummy, false);
      if (aligned_access_p (dr))
        data_ref = build_fold_indirect_ref (data_ref);
      else
	{
	  int mis = DR_MISALIGNMENT (dr);
	  tree tmis = (mis == -1 ? size_zero_node : size_int (mis));
	  tmis = size_binop (MULT_EXPR, tmis, size_int(BITS_PER_UNIT));
	  data_ref = build2 (MISALIGNED_INDIRECT_REF, vectype, data_ref, tmis);
	}
      new_stmt = build2 (MODIFY_EXPR, vectype, vec_dest, data_ref);
      new_temp = make_ssa_name (vec_dest, new_stmt);
      TREE_OPERAND (new_stmt, 0) = new_temp;
      vect_finish_stmt_generation (stmt, new_stmt, bsi);
      copy_virtual_operands (new_stmt, stmt);
    }
  else if (alignment_support_cheme == dr_unaligned_software_pipeline)
    {
      /* Create:
	 p1 = initial_addr;
	 msq_init = *(floor(p1))
	 p2 = initial_addr + VS - 1;
	 magic = have_builtin ? builtin_result : initial_address;
	 indx = 0;
	 loop {
	   p2' = p2 + indx * vectype_size
	   lsq = *(floor(p2'))
	   vec_dest = realign_load (msq, lsq, magic)
	   indx = indx + 1;
	   msq = lsq;
	 }
      */

      tree offset;
      tree magic;
      tree phi_stmt;
      tree msq_init;
      tree msq, lsq;
      tree dataref_ptr;
      tree params;

      /* <1> Create msq_init = *(floor(p1)) in the loop preheader  */
      vec_dest = vect_create_destination_var (scalar_dest, vectype);
      data_ref = vect_create_data_ref_ptr (stmt, bsi, NULL_TREE, 
					   &init_addr, true);
      data_ref = build1 (ALIGN_INDIRECT_REF, vectype, data_ref);
      new_stmt = build2 (MODIFY_EXPR, vectype, vec_dest, data_ref);
      new_temp = make_ssa_name (vec_dest, new_stmt);
      TREE_OPERAND (new_stmt, 0) = new_temp;
      new_bb = bsi_insert_on_edge_immediate (pe, new_stmt);
      gcc_assert (!new_bb);
      msq_init = TREE_OPERAND (new_stmt, 0);
      copy_virtual_operands (new_stmt, stmt);
      update_vuses_to_preheader (new_stmt, loop);


      /* <2> Create lsq = *(floor(p2')) in the loop  */ 
      offset = size_int (TYPE_VECTOR_SUBPARTS (vectype) - 1);
      vec_dest = vect_create_destination_var (scalar_dest, vectype);
      dataref_ptr = vect_create_data_ref_ptr (stmt, bsi, offset, &dummy, false);
      data_ref = build1 (ALIGN_INDIRECT_REF, vectype, dataref_ptr);
      new_stmt = build2 (MODIFY_EXPR, vectype, vec_dest, data_ref);
      new_temp = make_ssa_name (vec_dest, new_stmt);
      TREE_OPERAND (new_stmt, 0) = new_temp;
      vect_finish_stmt_generation (stmt, new_stmt, bsi);
      lsq = TREE_OPERAND (new_stmt, 0);
      copy_virtual_operands (new_stmt, stmt);


      /* <3> */
      if (targetm.vectorize.builtin_mask_for_load)
	{
	  /* Create permutation mask, if required, in loop preheader.  */
	  tree builtin_decl;
	  params = build_tree_list (NULL_TREE, init_addr);
	  vec_dest = vect_create_destination_var (scalar_dest, vectype);
	  builtin_decl = targetm.vectorize.builtin_mask_for_load ();
	  new_stmt = build_function_call_expr (builtin_decl, params);
	  new_stmt = build2 (MODIFY_EXPR, vectype, vec_dest, new_stmt);
	  new_temp = make_ssa_name (vec_dest, new_stmt);
	  TREE_OPERAND (new_stmt, 0) = new_temp;
	  new_bb = bsi_insert_on_edge_immediate (pe, new_stmt);
	  gcc_assert (!new_bb);
	  magic = TREE_OPERAND (new_stmt, 0);

	  /* The result of the CALL_EXPR to this builtin is determined from
	     the value of the parameter and no global variables are touched
	     which makes the builtin a "const" function.  Requiring the
	     builtin to have the "const" attribute makes it unnecessary
	     to call mark_call_clobbered.  */
	  gcc_assert (TREE_READONLY (builtin_decl));
	}
      else
	{
	  /* Use current address instead of init_addr for reduced reg pressure.
	   */
	  magic = dataref_ptr;
	}


      /* <4> Create msq = phi <msq_init, lsq> in loop  */ 
      vec_dest = vect_create_destination_var (scalar_dest, vectype);
      msq = make_ssa_name (vec_dest, NULL_TREE);
      phi_stmt = create_phi_node (msq, loop->header); /* CHECKME */
      SSA_NAME_DEF_STMT (msq) = phi_stmt;
      add_phi_arg (phi_stmt, msq_init, loop_preheader_edge (loop));
      add_phi_arg (phi_stmt, lsq, loop_latch_edge (loop));


      /* <5> Create <vec_dest = realign_load (msq, lsq, magic)> in loop  */
      vec_dest = vect_create_destination_var (scalar_dest, vectype);
      new_stmt = build3 (REALIGN_LOAD_EXPR, vectype, msq, lsq, magic);
      new_stmt = build2 (MODIFY_EXPR, vectype, vec_dest, new_stmt);
      new_temp = make_ssa_name (vec_dest, new_stmt); 
      TREE_OPERAND (new_stmt, 0) = new_temp;
      vect_finish_stmt_generation (stmt, new_stmt, bsi);
    }
  else
    gcc_unreachable ();

  *vec_stmt = new_stmt;
  return true;
}


/* Function vectorizable_live_operation.

   STMT computes a value that is used outside the loop. Check if 
   it can be supported.  */

bool
vectorizable_live_operation (tree stmt,
                             block_stmt_iterator *bsi ATTRIBUTE_UNUSED,
                             tree *vec_stmt ATTRIBUTE_UNUSED)
{
  tree operation;
  stmt_vec_info stmt_info = vinfo_for_stmt (stmt);
  loop_vec_info loop_vinfo = STMT_VINFO_LOOP_VINFO (stmt_info);
  int i;
  enum tree_code code;
  int op_type;
  tree op;
  tree def, def_stmt;
  enum vect_def_type dt; 

  if (!STMT_VINFO_LIVE_P (stmt_info))
    return false;

  if (TREE_CODE (stmt) != MODIFY_EXPR)
    return false;

  if (TREE_CODE (TREE_OPERAND (stmt, 0)) != SSA_NAME)
    return false;

  operation = TREE_OPERAND (stmt, 1);
  code = TREE_CODE (operation);

  op_type = TREE_CODE_LENGTH (code);

  /* FORNOW: support only if all uses are invariant. This means
     that the scalar operations can remain in place, unvectorized.
     The original last scalar value that they compute will be used.  */

  for (i = 0; i < op_type; i++)
    {
      op = TREE_OPERAND (operation, i);
      if (!vect_is_simple_use (op, loop_vinfo, &def_stmt, &def, &dt))
        {
          if (vect_print_dump_info (REPORT_DETAILS))
            fprintf (vect_dump, "use not simple.");
          return false;
        }

      if (dt != vect_invariant_def && dt != vect_constant_def)
        return false;
    }

  /* No transformation is required for the cases we currently support.  */
  return true;
}


/* Function vect_is_simple_cond.
  
   Input:
   LOOP - the loop that is being vectorized.
   COND - Condition that is checked for simple use.

   Returns whether a COND can be vectorized.  Checks whether
   condition operands are supportable using vec_is_simple_use.  */

static bool
vect_is_simple_cond (tree cond, loop_vec_info loop_vinfo)
{
  tree lhs, rhs;
  tree def;
  enum vect_def_type dt;

  if (!COMPARISON_CLASS_P (cond))
    return false;

  lhs = TREE_OPERAND (cond, 0);
  rhs = TREE_OPERAND (cond, 1);

  if (TREE_CODE (lhs) == SSA_NAME)
    {
      tree lhs_def_stmt = SSA_NAME_DEF_STMT (lhs);
      if (!vect_is_simple_use (lhs, loop_vinfo, &lhs_def_stmt, &def, &dt))
	return false;
    }
  else if (TREE_CODE (lhs) != INTEGER_CST && TREE_CODE (lhs) != REAL_CST)
    return false;

  if (TREE_CODE (rhs) == SSA_NAME)
    {
      tree rhs_def_stmt = SSA_NAME_DEF_STMT (rhs);
      if (!vect_is_simple_use (rhs, loop_vinfo, &rhs_def_stmt, &def, &dt))
	return false;
    }
  else if (TREE_CODE (rhs) != INTEGER_CST  && TREE_CODE (rhs) != REAL_CST)
    return false;

  return true;
}

/* vectorizable_condition.

   Check if STMT is conditional modify expression that can be vectorized. 
   If VEC_STMT is also passed, vectorize the STMT: create a vectorized 
   stmt using VEC_COND_EXPR  to replace it, put it in VEC_STMT, and insert it 
   at BSI.

   Return FALSE if not a vectorizable STMT, TRUE otherwise.  */

bool
vectorizable_condition (tree stmt, block_stmt_iterator *bsi, tree *vec_stmt)
{
  tree scalar_dest = NULL_TREE;
  tree vec_dest = NULL_TREE;
  tree op = NULL_TREE;
  tree cond_expr, then_clause, else_clause;
  stmt_vec_info stmt_info = vinfo_for_stmt (stmt);
  tree vectype = STMT_VINFO_VECTYPE (stmt_info);
  tree vec_cond_lhs, vec_cond_rhs, vec_then_clause, vec_else_clause;
  tree vec_compare, vec_cond_expr;
  tree new_temp;
  loop_vec_info loop_vinfo = STMT_VINFO_LOOP_VINFO (stmt_info);
  enum machine_mode vec_mode;
  tree def;
  enum vect_def_type dt;

  if (!STMT_VINFO_RELEVANT_P (stmt_info))
    return false;

  gcc_assert (STMT_VINFO_DEF_TYPE (stmt_info) == vect_loop_def);

  if (STMT_VINFO_LIVE_P (stmt_info))
    {
      /* FORNOW: not yet supported.  */
      if (vect_print_dump_info (REPORT_DETAILS))
        fprintf (vect_dump, "value used after loop.");
      return false;
    }

  if (TREE_CODE (stmt) != MODIFY_EXPR)
    return false;

  op = TREE_OPERAND (stmt, 1);

  if (TREE_CODE (op) != COND_EXPR)
    return false;

  cond_expr = TREE_OPERAND (op, 0);
  then_clause = TREE_OPERAND (op, 1);
  else_clause = TREE_OPERAND (op, 2);

  if (!vect_is_simple_cond (cond_expr, loop_vinfo))
    return false;

  /* We do not handle two different vector types for the condition
     and the values.  */
  if (TREE_TYPE (TREE_OPERAND (cond_expr, 0)) != TREE_TYPE (vectype))
    return false;

  if (TREE_CODE (then_clause) == SSA_NAME)
    {
      tree then_def_stmt = SSA_NAME_DEF_STMT (then_clause);
      if (!vect_is_simple_use (then_clause, loop_vinfo, 
			       &then_def_stmt, &def, &dt))
	return false;
    }
  else if (TREE_CODE (then_clause) != INTEGER_CST 
	   && TREE_CODE (then_clause) != REAL_CST)
    return false;

  if (TREE_CODE (else_clause) == SSA_NAME)
    {
      tree else_def_stmt = SSA_NAME_DEF_STMT (else_clause);
      if (!vect_is_simple_use (else_clause, loop_vinfo, 
			       &else_def_stmt, &def, &dt))
	return false;
    }
  else if (TREE_CODE (else_clause) != INTEGER_CST 
	   && TREE_CODE (else_clause) != REAL_CST)
    return false;


  vec_mode = TYPE_MODE (vectype);

  if (!vec_stmt) 
    {
      STMT_VINFO_TYPE (stmt_info) = condition_vec_info_type;
      return expand_vec_cond_expr_p (op, vec_mode);
    }

  /* Transform */

  /* Handle def.  */
  scalar_dest = TREE_OPERAND (stmt, 0);
  vec_dest = vect_create_destination_var (scalar_dest, vectype);

  /* Handle cond expr.  */
  vec_cond_lhs = 
    vect_get_vec_def_for_operand (TREE_OPERAND (cond_expr, 0), stmt, NULL);
  vec_cond_rhs = 
    vect_get_vec_def_for_operand (TREE_OPERAND (cond_expr, 1), stmt, NULL);
  vec_then_clause = vect_get_vec_def_for_operand (then_clause, stmt, NULL);
  vec_else_clause = vect_get_vec_def_for_operand (else_clause, stmt, NULL);

  /* Arguments are ready. create the new vector stmt.  */
  vec_compare = build2 (TREE_CODE (cond_expr), vectype, 
			vec_cond_lhs, vec_cond_rhs);
  vec_cond_expr = build3 (VEC_COND_EXPR, vectype, 
			  vec_compare, vec_then_clause, vec_else_clause);

  *vec_stmt = build2 (MODIFY_EXPR, vectype, vec_dest, vec_cond_expr);
  new_temp = make_ssa_name (vec_dest, *vec_stmt);
  TREE_OPERAND (*vec_stmt, 0) = new_temp;
  vect_finish_stmt_generation (stmt, *vec_stmt, bsi);
  
  return true;
}

/* Function vect_transform_stmt.

   Create a vectorized stmt to replace STMT, and insert it at BSI.  */

bool
vect_transform_stmt (tree stmt, block_stmt_iterator *bsi)
{
  bool is_store = false;
  tree vec_stmt = NULL_TREE;
  stmt_vec_info stmt_info = vinfo_for_stmt (stmt);
  tree orig_stmt_in_pattern;
  bool done;

  if (STMT_VINFO_RELEVANT_P (stmt_info))
    {
      switch (STMT_VINFO_TYPE (stmt_info))
      {
      case op_vec_info_type:
	done = vectorizable_operation (stmt, bsi, &vec_stmt);
	gcc_assert (done);
	break;

      case assignment_vec_info_type:
	done = vectorizable_assignment (stmt, bsi, &vec_stmt);
	gcc_assert (done);
	break;

      case load_vec_info_type:
	done = vectorizable_load (stmt, bsi, &vec_stmt);
	gcc_assert (done);
	break;

      case store_vec_info_type:
	done = vectorizable_store (stmt, bsi, &vec_stmt);
	gcc_assert (done);
	is_store = true;
	break;

      case condition_vec_info_type:
	done = vectorizable_condition (stmt, bsi, &vec_stmt);
	gcc_assert (done);
	break;

      default:
	if (vect_print_dump_info (REPORT_DETAILS))
	  fprintf (vect_dump, "stmt not supported.");
	gcc_unreachable ();
      }

      gcc_assert (vec_stmt);
      STMT_VINFO_VEC_STMT (stmt_info) = vec_stmt;
      orig_stmt_in_pattern = STMT_VINFO_RELATED_STMT (stmt_info);
      if (orig_stmt_in_pattern)
        {
          stmt_vec_info stmt_vinfo = vinfo_for_stmt (orig_stmt_in_pattern);
          if (STMT_VINFO_IN_PATTERN_P (stmt_vinfo))
            {
              gcc_assert (STMT_VINFO_RELATED_STMT (stmt_vinfo) == stmt);

              /* STMT was inserted by the vectorizer to replace a computation 
                 idiom.  ORIG_STMT_IN_PATTERN is a stmt in the original
                 sequence that computed this idiom.  We need to record a pointer
                 to VEC_STMT in the stmt_info of ORIG_STMT_IN_PATTERN.  See more
                 detail in the documentation of vect_pattern_recog.  */

              STMT_VINFO_VEC_STMT (stmt_vinfo) = vec_stmt;
            }
        }
    }

  if (STMT_VINFO_LIVE_P (stmt_info))
    {
      switch (STMT_VINFO_TYPE (stmt_info))
      {
      case reduc_vec_info_type:
        done = vectorizable_reduction (stmt, bsi, &vec_stmt);
        gcc_assert (done);
        break;

      default:
        done = vectorizable_live_operation (stmt, bsi, &vec_stmt);
        gcc_assert (done);
      }

      if (vec_stmt)
        {
          gcc_assert (!STMT_VINFO_VEC_STMT (stmt_info));
          STMT_VINFO_VEC_STMT (stmt_info) = vec_stmt;
        }
    }

  return is_store; 
}


/* This function builds ni_name = number of iterations loop executes
   on the loop preheader.  */

static tree
vect_build_loop_niters (loop_vec_info loop_vinfo)
{
  tree ni_name, stmt, var;
  edge pe;
  struct loop *loop = LOOP_VINFO_LOOP (loop_vinfo);
  tree ni = unshare_expr (LOOP_VINFO_NITERS (loop_vinfo));

  var = create_tmp_var (TREE_TYPE (ni), "niters");
  add_referenced_var (var);
  ni_name = force_gimple_operand (ni, &stmt, false, var);

  pe = loop_preheader_edge (loop);
  if (stmt)
    {
      basic_block new_bb = bsi_insert_on_edge_immediate (pe, stmt);
      gcc_assert (!new_bb);
    }
      
  return ni_name;
}


/* This function generates the following statements:

 ni_name = number of iterations loop executes
 ratio = ni_name / vf
 ratio_mult_vf_name = ratio * vf

 and places them at the loop preheader edge.  */

static void 
vect_generate_tmps_on_preheader (loop_vec_info loop_vinfo, 
				 tree *ni_name_ptr,
				 tree *ratio_mult_vf_name_ptr, 
				 tree *ratio_name_ptr)
{

  edge pe;
  basic_block new_bb;
  tree stmt, ni_name;
  tree var;
  tree ratio_name;
  tree ratio_mult_vf_name;
  struct loop *loop = LOOP_VINFO_LOOP (loop_vinfo);
  tree ni = LOOP_VINFO_NITERS (loop_vinfo);
  int vf = LOOP_VINFO_VECT_FACTOR (loop_vinfo);
  tree log_vf;

  pe = loop_preheader_edge (loop);

  /* Generate temporary variable that contains 
     number of iterations loop executes.  */

  ni_name = vect_build_loop_niters (loop_vinfo);
  log_vf = build_int_cst (TREE_TYPE (ni), exact_log2 (vf));

  /* Create: ratio = ni >> log2(vf) */

  var = create_tmp_var (TREE_TYPE (ni), "bnd");
  add_referenced_var (var);
  ratio_name = make_ssa_name (var, NULL_TREE);
  stmt = build2 (MODIFY_EXPR, void_type_node, ratio_name,
	   build2 (RSHIFT_EXPR, TREE_TYPE (ni_name), ni_name, log_vf));
  SSA_NAME_DEF_STMT (ratio_name) = stmt;

  pe = loop_preheader_edge (loop);
  new_bb = bsi_insert_on_edge_immediate (pe, stmt);
  gcc_assert (!new_bb);
       
  /* Create: ratio_mult_vf = ratio << log2 (vf).  */

  var = create_tmp_var (TREE_TYPE (ni), "ratio_mult_vf");
  add_referenced_var (var);
  ratio_mult_vf_name = make_ssa_name (var, NULL_TREE);
  stmt = build2 (MODIFY_EXPR, void_type_node, ratio_mult_vf_name,
	   build2 (LSHIFT_EXPR, TREE_TYPE (ratio_name), ratio_name, log_vf));
  SSA_NAME_DEF_STMT (ratio_mult_vf_name) = stmt;

  pe = loop_preheader_edge (loop);
  new_bb = bsi_insert_on_edge_immediate (pe, stmt);
  gcc_assert (!new_bb);

  *ni_name_ptr = ni_name;
  *ratio_mult_vf_name_ptr = ratio_mult_vf_name;
  *ratio_name_ptr = ratio_name;
    
  return;  
}


/* Function update_vuses_to_preheader.

   Input:
   STMT - a statement with potential VUSEs.
   LOOP - the loop whose preheader will contain STMT.

   It's possible to vectorize a loop even though an SSA_NAME from a VUSE
   appears to be defined in a V_MAY_DEF in another statement in a loop.
   One such case is when the VUSE is at the dereference of a __restricted__
   pointer in a load and the V_MAY_DEF is at the dereference of a different
   __restricted__ pointer in a store.  Vectorization may result in
   copy_virtual_uses being called to copy the problematic VUSE to a new
   statement that is being inserted in the loop preheader.  This procedure
   is called to change the SSA_NAME in the new statement's VUSE from the
   SSA_NAME updated in the loop to the related SSA_NAME available on the
   path entering the loop.

   When this function is called, we have the following situation:

        # vuse <name1>
        S1: vload
    do {
        # name1 = phi < name0 , name2>

        # vuse <name1>
        S2: vload

        # name2 = vdef <name1>
        S3: vstore

    }while...

   Stmt S1 was created in the loop preheader block as part of misaligned-load
   handling. This function fixes the name of the vuse of S1 from 'name1' to
   'name0'.  */

static void
update_vuses_to_preheader (tree stmt, struct loop *loop)
{
  basic_block header_bb = loop->header;
  edge preheader_e = loop_preheader_edge (loop);
  ssa_op_iter iter;
  use_operand_p use_p;

  FOR_EACH_SSA_USE_OPERAND (use_p, stmt, iter, SSA_OP_VUSE)
    {
      tree ssa_name = USE_FROM_PTR (use_p);
      tree def_stmt = SSA_NAME_DEF_STMT (ssa_name);
      tree name_var = SSA_NAME_VAR (ssa_name);
      basic_block bb = bb_for_stmt (def_stmt);

      /* For a use before any definitions, def_stmt is a NOP_EXPR.  */
      if (!IS_EMPTY_STMT (def_stmt)
	  && flow_bb_inside_loop_p (loop, bb))
        {
          /* If the block containing the statement defining the SSA_NAME
             is in the loop then it's necessary to find the definition
             outside the loop using the PHI nodes of the header.  */
	  tree phi;
	  bool updated = false;

	  for (phi = phi_nodes (header_bb); phi; phi = TREE_CHAIN (phi))
	    {
	      if (SSA_NAME_VAR (PHI_RESULT (phi)) == name_var)
		{
		  SET_USE (use_p, PHI_ARG_DEF (phi, preheader_e->dest_idx));
		  updated = true;
		  break;
		}
	    }
	  gcc_assert (updated);
	}
    }
}


/*   Function vect_update_ivs_after_vectorizer.

     "Advance" the induction variables of LOOP to the value they should take
     after the execution of LOOP.  This is currently necessary because the
     vectorizer does not handle induction variables that are used after the
     loop.  Such a situation occurs when the last iterations of LOOP are
     peeled, because:
     1. We introduced new uses after LOOP for IVs that were not originally used
        after LOOP: the IVs of LOOP are now used by an epilog loop.
     2. LOOP is going to be vectorized; this means that it will iterate N/VF
        times, whereas the loop IVs should be bumped N times.

     Input:
     - LOOP - a loop that is going to be vectorized. The last few iterations
              of LOOP were peeled.
     - NITERS - the number of iterations that LOOP executes (before it is
                vectorized). i.e, the number of times the ivs should be bumped.
     - UPDATE_E - a successor edge of LOOP->exit that is on the (only) path
                  coming out from LOOP on which there are uses of the LOOP ivs
		  (this is the path from LOOP->exit to epilog_loop->preheader).

                  The new definitions of the ivs are placed in LOOP->exit.
                  The phi args associated with the edge UPDATE_E in the bb
                  UPDATE_E->dest are updated accordingly.

     Assumption 1: Like the rest of the vectorizer, this function assumes
     a single loop exit that has a single predecessor.

     Assumption 2: The phi nodes in the LOOP header and in update_bb are
     organized in the same order.

     Assumption 3: The access function of the ivs is simple enough (see
     vect_can_advance_ivs_p).  This assumption will be relaxed in the future.

     Assumption 4: Exactly one of the successors of LOOP exit-bb is on a path
     coming out of LOOP on which the ivs of LOOP are used (this is the path 
     that leads to the epilog loop; other paths skip the epilog loop).  This
     path starts with the edge UPDATE_E, and its destination (denoted update_bb)
     needs to have its phis updated.
 */

static void
vect_update_ivs_after_vectorizer (loop_vec_info loop_vinfo, tree niters, 
				  edge update_e)
{
  struct loop *loop = LOOP_VINFO_LOOP (loop_vinfo);
  basic_block exit_bb = loop->single_exit->dest;
  tree phi, phi1;
  basic_block update_bb = update_e->dest;

  /* gcc_assert (vect_can_advance_ivs_p (loop_vinfo)); */

  /* Make sure there exists a single-predecessor exit bb:  */
  gcc_assert (single_pred_p (exit_bb));

  for (phi = phi_nodes (loop->header), phi1 = phi_nodes (update_bb); 
       phi && phi1; 
       phi = PHI_CHAIN (phi), phi1 = PHI_CHAIN (phi1))
    {
      tree access_fn = NULL;
      tree evolution_part;
      tree init_expr;
      tree step_expr;
      tree var, stmt, ni, ni_name;
      block_stmt_iterator last_bsi;

      if (vect_print_dump_info (REPORT_DETAILS))
        {
          fprintf (vect_dump, "vect_update_ivs_after_vectorizer: phi: ");
          print_generic_expr (vect_dump, phi, TDF_SLIM);
        }

      /* Skip virtual phi's.  */
      if (!is_gimple_reg (SSA_NAME_VAR (PHI_RESULT (phi))))
	{
	  if (vect_print_dump_info (REPORT_DETAILS))
	    fprintf (vect_dump, "virtual phi. skip.");
	  continue;
	}

      /* Skip reduction phis.  */
      if (STMT_VINFO_DEF_TYPE (vinfo_for_stmt (phi)) == vect_reduction_def)
        { 
          if (vect_print_dump_info (REPORT_DETAILS))
            fprintf (vect_dump, "reduc phi. skip.");
          continue;
        } 

      access_fn = analyze_scalar_evolution (loop, PHI_RESULT (phi)); 
      gcc_assert (access_fn);
      evolution_part =
	 unshare_expr (evolution_part_in_loop_num (access_fn, loop->num));
      gcc_assert (evolution_part != NULL_TREE);
      
      /* FORNOW: We do not support IVs whose evolution function is a polynomial
         of degree >= 2 or exponential.  */
      gcc_assert (!tree_is_chrec (evolution_part));

      step_expr = evolution_part;
      init_expr = unshare_expr (initial_condition_in_loop_num (access_fn, 
							       loop->num));

      ni = build2 (PLUS_EXPR, TREE_TYPE (init_expr),
		  build2 (MULT_EXPR, TREE_TYPE (niters),
		       niters, step_expr), init_expr);

      var = create_tmp_var (TREE_TYPE (init_expr), "tmp");
      add_referenced_var (var);

      ni_name = force_gimple_operand (ni, &stmt, false, var);
      
      /* Insert stmt into exit_bb.  */
      last_bsi = bsi_last (exit_bb);
      if (stmt)
        bsi_insert_before (&last_bsi, stmt, BSI_SAME_STMT);   

      /* Fix phi expressions in the successor bb.  */
      SET_PHI_ARG_DEF (phi1, update_e->dest_idx, ni_name);
    }
}


/* Function vect_do_peeling_for_loop_bound

   Peel the last iterations of the loop represented by LOOP_VINFO.
   The peeled iterations form a new epilog loop.  Given that the loop now 
   iterates NITERS times, the new epilog loop iterates
   NITERS % VECTORIZATION_FACTOR times.
   
   The original loop will later be made to iterate 
   NITERS / VECTORIZATION_FACTOR times (this value is placed into RATIO).  */

static void 
vect_do_peeling_for_loop_bound (loop_vec_info loop_vinfo, tree *ratio,
				struct loops *loops)
{
  tree ni_name, ratio_mult_vf_name;
  struct loop *loop = LOOP_VINFO_LOOP (loop_vinfo);
  struct loop *new_loop;
  edge update_e;
  basic_block preheader;
  int loop_num;

  if (vect_print_dump_info (REPORT_DETAILS))
    fprintf (vect_dump, "=== vect_do_peeling_for_loop_bound ===");

  initialize_original_copy_tables ();

  /* Generate the following variables on the preheader of original loop:
	 
     ni_name = number of iteration the original loop executes
     ratio = ni_name / vf
     ratio_mult_vf_name = ratio * vf  */
  vect_generate_tmps_on_preheader (loop_vinfo, &ni_name,
				   &ratio_mult_vf_name, ratio);

  loop_num  = loop->num; 
  new_loop = slpeel_tree_peel_loop_to_edge (loop, loops, loop->single_exit,
					    ratio_mult_vf_name, ni_name, false);
  gcc_assert (new_loop);
  gcc_assert (loop_num == loop->num);
#ifdef ENABLE_CHECKING
  slpeel_verify_cfg_after_peeling (loop, new_loop);
#endif

  /* A guard that controls whether the new_loop is to be executed or skipped
     is placed in LOOP->exit.  LOOP->exit therefore has two successors - one
     is the preheader of NEW_LOOP, where the IVs from LOOP are used.  The other
     is a bb after NEW_LOOP, where these IVs are not used.  Find the edge that
     is on the path where the LOOP IVs are used and need to be updated.  */

  preheader = loop_preheader_edge (new_loop)->src;
  if (EDGE_PRED (preheader, 0)->src == loop->single_exit->dest)
    update_e = EDGE_PRED (preheader, 0);
  else
    update_e = EDGE_PRED (preheader, 1);

  /* Update IVs of original loop as if they were advanced 
     by ratio_mult_vf_name steps.  */
  vect_update_ivs_after_vectorizer (loop_vinfo, ratio_mult_vf_name, update_e); 

  /* After peeling we have to reset scalar evolution analyzer.  */
  scev_reset ();

  free_original_copy_tables ();
}


/* Function vect_gen_niters_for_prolog_loop

   Set the number of iterations for the loop represented by LOOP_VINFO
   to the minimum between LOOP_NITERS (the original iteration count of the loop)
   and the misalignment of DR - the data reference recorded in
   LOOP_VINFO_UNALIGNED_DR (LOOP_VINFO).  As a result, after the execution of 
   this loop, the data reference DR will refer to an aligned location.

   The following computation is generated:

   If the misalignment of DR is known at compile time:
     addr_mis = int mis = DR_MISALIGNMENT (dr);
   Else, compute address misalignment in bytes:
     addr_mis = addr & (vectype_size - 1)

   prolog_niters = min ( LOOP_NITERS , (VF - addr_mis/elem_size)&(VF-1) )
   
   (elem_size = element type size; an element is the scalar element 
	whose type is the inner type of the vectype)  */

static tree 
vect_gen_niters_for_prolog_loop (loop_vec_info loop_vinfo, tree loop_niters)
{
  struct data_reference *dr = LOOP_VINFO_UNALIGNED_DR (loop_vinfo);
  int vf = LOOP_VINFO_VECT_FACTOR (loop_vinfo);
  struct loop *loop = LOOP_VINFO_LOOP (loop_vinfo);
  tree var, stmt;
  tree iters, iters_name;
  edge pe;
  basic_block new_bb;
  tree dr_stmt = DR_STMT (dr);
  stmt_vec_info stmt_info = vinfo_for_stmt (dr_stmt);
  tree vectype = STMT_VINFO_VECTYPE (stmt_info);
  int vectype_align = TYPE_ALIGN (vectype) / BITS_PER_UNIT;
  tree niters_type = TREE_TYPE (loop_niters);

  pe = loop_preheader_edge (loop); 

  if (LOOP_PEELING_FOR_ALIGNMENT (loop_vinfo) > 0)
    {
      int byte_misalign = LOOP_PEELING_FOR_ALIGNMENT (loop_vinfo);
      int element_size = vectype_align/vf;
      int elem_misalign = byte_misalign / element_size;

      if (vect_print_dump_info (REPORT_DETAILS))
        fprintf (vect_dump, "known alignment = %d.", byte_misalign);
      iters = build_int_cst (niters_type, (vf - elem_misalign)&(vf-1));
    }
  else
    {
      tree new_stmts = NULL_TREE;
      tree start_addr =
        vect_create_addr_base_for_vector_ref (dr_stmt, &new_stmts, NULL_TREE);
      tree ptr_type = TREE_TYPE (start_addr);
      tree size = TYPE_SIZE (ptr_type);
      tree type = lang_hooks.types.type_for_size (tree_low_cst (size, 1), 1);
      tree vectype_size_minus_1 = build_int_cst (type, vectype_align - 1);
      tree elem_size_log =
        build_int_cst (type, exact_log2 (vectype_align/vf));
      tree vf_minus_1 = build_int_cst (type, vf - 1);
      tree vf_tree = build_int_cst (type, vf);
      tree byte_misalign;
      tree elem_misalign;

      new_bb = bsi_insert_on_edge_immediate (pe, new_stmts);
      gcc_assert (!new_bb);
  
      /* Create:  byte_misalign = addr & (vectype_size - 1)  */
      byte_misalign = 
        build2 (BIT_AND_EXPR, type, start_addr, vectype_size_minus_1);
  
      /* Create:  elem_misalign = byte_misalign / element_size  */
      elem_misalign =
        build2 (RSHIFT_EXPR, type, byte_misalign, elem_size_log);

      /* Create:  (niters_type) (VF - elem_misalign)&(VF - 1)  */
      iters = build2 (MINUS_EXPR, type, vf_tree, elem_misalign);
      iters = build2 (BIT_AND_EXPR, type, iters, vf_minus_1);
      iters = fold_convert (niters_type, iters);
    }

  /* Create:  prolog_loop_niters = min (iters, loop_niters) */
  /* If the loop bound is known at compile time we already verified that it is
     greater than vf; since the misalignment ('iters') is at most vf, there's
     no need to generate the MIN_EXPR in this case.  */
  if (TREE_CODE (loop_niters) != INTEGER_CST)
    iters = build2 (MIN_EXPR, niters_type, iters, loop_niters);

  if (vect_print_dump_info (REPORT_DETAILS))
    {
      fprintf (vect_dump, "niters for prolog loop: ");
      print_generic_expr (vect_dump, iters, TDF_SLIM);
    }

  var = create_tmp_var (niters_type, "prolog_loop_niters");
  add_referenced_var (var);
  iters_name = force_gimple_operand (iters, &stmt, false, var);

  /* Insert stmt on loop preheader edge.  */
  if (stmt)
    {
      basic_block new_bb = bsi_insert_on_edge_immediate (pe, stmt);
      gcc_assert (!new_bb);
    }

  return iters_name; 
}


/* Function vect_update_init_of_dr

   NITERS iterations were peeled from LOOP.  DR represents a data reference
   in LOOP.  This function updates the information recorded in DR to
   account for the fact that the first NITERS iterations had already been 
   executed.  Specifically, it updates the OFFSET field of DR.  */

static void
vect_update_init_of_dr (struct data_reference *dr, tree niters)
{
  tree offset = DR_OFFSET (dr);
      
  niters = fold_build2 (MULT_EXPR, TREE_TYPE (niters), niters, DR_STEP (dr));
  offset = fold_build2 (PLUS_EXPR, TREE_TYPE (offset), offset, niters);
  DR_OFFSET (dr) = offset;
}


/* Function vect_update_inits_of_drs

   NITERS iterations were peeled from the loop represented by LOOP_VINFO.  
   This function updates the information recorded for the data references in 
   the loop to account for the fact that the first NITERS iterations had 
   already been executed.  Specifically, it updates the initial_condition of the
   access_function of all the data_references in the loop.  */

static void
vect_update_inits_of_drs (loop_vec_info loop_vinfo, tree niters)
{
  unsigned int i;
  VEC (data_reference_p, heap) *datarefs = LOOP_VINFO_DATAREFS (loop_vinfo);
  struct data_reference *dr;

  if (vect_dump && (dump_flags & TDF_DETAILS))
    fprintf (vect_dump, "=== vect_update_inits_of_dr ===");

  for (i = 0; VEC_iterate (data_reference_p, datarefs, i, dr); i++)
    vect_update_init_of_dr (dr, niters);
}


/* Function vect_do_peeling_for_alignment

   Peel the first 'niters' iterations of the loop represented by LOOP_VINFO.
   'niters' is set to the misalignment of one of the data references in the
   loop, thereby forcing it to refer to an aligned location at the beginning
   of the execution of this loop.  The data reference for which we are
   peeling is recorded in LOOP_VINFO_UNALIGNED_DR.  */

static void
vect_do_peeling_for_alignment (loop_vec_info loop_vinfo, struct loops *loops)
{
  struct loop *loop = LOOP_VINFO_LOOP (loop_vinfo);
  tree niters_of_prolog_loop, ni_name;
  tree n_iters;
  struct loop *new_loop;

  if (vect_print_dump_info (REPORT_DETAILS))
    fprintf (vect_dump, "=== vect_do_peeling_for_alignment ===");

  initialize_original_copy_tables ();

  ni_name = vect_build_loop_niters (loop_vinfo);
  niters_of_prolog_loop = vect_gen_niters_for_prolog_loop (loop_vinfo, ni_name);
  
  /* Peel the prolog loop and iterate it niters_of_prolog_loop.  */
  new_loop = 
	slpeel_tree_peel_loop_to_edge (loop, loops, loop_preheader_edge (loop), 
				       niters_of_prolog_loop, ni_name, true); 
  gcc_assert (new_loop);
#ifdef ENABLE_CHECKING
  slpeel_verify_cfg_after_peeling (new_loop, loop);
#endif

  /* Update number of times loop executes.  */
  n_iters = LOOP_VINFO_NITERS (loop_vinfo);
  LOOP_VINFO_NITERS (loop_vinfo) = fold_build2 (MINUS_EXPR,
		TREE_TYPE (n_iters), n_iters, niters_of_prolog_loop);

  /* Update the init conditions of the access functions of all data refs.  */
  vect_update_inits_of_drs (loop_vinfo, niters_of_prolog_loop);

  /* After peeling we have to reset scalar evolution analyzer.  */
  scev_reset ();

  free_original_copy_tables ();
}


/* Function vect_create_cond_for_align_checks.

   Create a conditional expression that represents the alignment checks for
   all of data references (array element references) whose alignment must be
   checked at runtime.

   Input:
   LOOP_VINFO - two fields of the loop information are used.
                LOOP_VINFO_PTR_MASK is the mask used to check the alignment.
                LOOP_VINFO_MAY_MISALIGN_STMTS contains the refs to be checked.

   Output:
   COND_EXPR_STMT_LIST - statements needed to construct the conditional
                         expression.
   The returned value is the conditional expression to be used in the if
   statement that controls which version of the loop gets executed at runtime.

   The algorithm makes two assumptions:
     1) The number of bytes "n" in a vector is a power of 2.
     2) An address "a" is aligned if a%n is zero and that this
        test can be done as a&(n-1) == 0.  For example, for 16
        byte vectors the test is a&0xf == 0.  */

static tree
vect_create_cond_for_align_checks (loop_vec_info loop_vinfo,
                                   tree *cond_expr_stmt_list)
{
  VEC(tree,heap) *may_misalign_stmts
    = LOOP_VINFO_MAY_MISALIGN_STMTS (loop_vinfo);
  tree ref_stmt;
  int mask = LOOP_VINFO_PTR_MASK (loop_vinfo);
  tree mask_cst;
  unsigned int i;
  tree psize;
  tree int_ptrsize_type;
  char tmp_name[20];
  tree or_tmp_name = NULL_TREE;
  tree and_tmp, and_tmp_name, and_stmt;
  tree ptrsize_zero;

  /* Check that mask is one less than a power of 2, i.e., mask is
     all zeros followed by all ones.  */
  gcc_assert ((mask != 0) && ((mask & (mask+1)) == 0));

  /* CHECKME: what is the best integer or unsigned type to use to hold a
     cast from a pointer value?  */
  psize = TYPE_SIZE (ptr_type_node);
  int_ptrsize_type
    = lang_hooks.types.type_for_size (tree_low_cst (psize, 1), 0);

  /* Create expression (mask & (dr_1 || ... || dr_n)) where dr_i is the address
     of the first vector of the i'th data reference. */

  for (i = 0; VEC_iterate (tree, may_misalign_stmts, i, ref_stmt); i++)
    {
      tree new_stmt_list = NULL_TREE;   
      tree addr_base;
      tree addr_tmp, addr_tmp_name, addr_stmt;
      tree or_tmp, new_or_tmp_name, or_stmt;

      /* create: addr_tmp = (int)(address_of_first_vector) */
      addr_base = vect_create_addr_base_for_vector_ref (ref_stmt, 
							&new_stmt_list, 
							NULL_TREE);

      if (new_stmt_list != NULL_TREE)
        append_to_statement_list_force (new_stmt_list, cond_expr_stmt_list);

      sprintf (tmp_name, "%s%d", "addr2int", i);
      addr_tmp = create_tmp_var (int_ptrsize_type, tmp_name);
      add_referenced_var (addr_tmp);
      addr_tmp_name = make_ssa_name (addr_tmp, NULL_TREE);
      addr_stmt = fold_convert (int_ptrsize_type, addr_base);
      addr_stmt = build2 (MODIFY_EXPR, void_type_node,
                          addr_tmp_name, addr_stmt);
      SSA_NAME_DEF_STMT (addr_tmp_name) = addr_stmt;
      append_to_statement_list_force (addr_stmt, cond_expr_stmt_list);

      /* The addresses are OR together.  */

      if (or_tmp_name != NULL_TREE)
        {
          /* create: or_tmp = or_tmp | addr_tmp */
          sprintf (tmp_name, "%s%d", "orptrs", i);
          or_tmp = create_tmp_var (int_ptrsize_type, tmp_name);
          add_referenced_var (or_tmp);
          new_or_tmp_name = make_ssa_name (or_tmp, NULL_TREE);
          or_stmt = build2 (MODIFY_EXPR, void_type_node, new_or_tmp_name,
                            build2 (BIT_IOR_EXPR, int_ptrsize_type,
	                            or_tmp_name,
                                    addr_tmp_name));
          SSA_NAME_DEF_STMT (new_or_tmp_name) = or_stmt;
          append_to_statement_list_force (or_stmt, cond_expr_stmt_list);
          or_tmp_name = new_or_tmp_name;
        }
      else
        or_tmp_name = addr_tmp_name;

    } /* end for i */

  mask_cst = build_int_cst (int_ptrsize_type, mask);

  /* create: and_tmp = or_tmp & mask  */
  and_tmp = create_tmp_var (int_ptrsize_type, "andmask" );
  add_referenced_var (and_tmp);
  and_tmp_name = make_ssa_name (and_tmp, NULL_TREE);

  and_stmt = build2 (MODIFY_EXPR, void_type_node,
                     and_tmp_name,
                     build2 (BIT_AND_EXPR, int_ptrsize_type,
                             or_tmp_name, mask_cst));
  SSA_NAME_DEF_STMT (and_tmp_name) = and_stmt;
  append_to_statement_list_force (and_stmt, cond_expr_stmt_list);

  /* Make and_tmp the left operand of the conditional test against zero.
     if and_tmp has a nonzero bit then some address is unaligned.  */
  ptrsize_zero = build_int_cst (int_ptrsize_type, 0);
  return build2 (EQ_EXPR, boolean_type_node,
                 and_tmp_name, ptrsize_zero);
}


/* Function vect_transform_loop.

   The analysis phase has determined that the loop is vectorizable.
   Vectorize the loop - created vectorized stmts to replace the scalar
   stmts in the loop, and update the loop exit condition.  */

void
vect_transform_loop (loop_vec_info loop_vinfo, 
		     struct loops *loops ATTRIBUTE_UNUSED)
{
  struct loop *loop = LOOP_VINFO_LOOP (loop_vinfo);
  basic_block *bbs = LOOP_VINFO_BBS (loop_vinfo);
  int nbbs = loop->num_nodes;
  block_stmt_iterator si;
  int i;
  tree ratio = NULL;
  int vectorization_factor = LOOP_VINFO_VECT_FACTOR (loop_vinfo);
  bitmap_iterator bi;
  unsigned int j;

  if (vect_print_dump_info (REPORT_DETAILS))
    fprintf (vect_dump, "=== vec_transform_loop ===");

  /* If the loop has data references that may or may not be aligned then
     two versions of the loop need to be generated, one which is vectorized
     and one which isn't.  A test is then generated to control which of the
     loops is executed.  The test checks for the alignment of all of the
     data references that may or may not be aligned. */

  if (VEC_length (tree, LOOP_VINFO_MAY_MISALIGN_STMTS (loop_vinfo)))
    {
      struct loop *nloop;
      tree cond_expr;
      tree cond_expr_stmt_list = NULL_TREE;
      basic_block condition_bb;
      block_stmt_iterator cond_exp_bsi;
      basic_block merge_bb;
      basic_block new_exit_bb;
      edge new_exit_e, e;
      tree orig_phi, new_phi, arg;

      cond_expr = vect_create_cond_for_align_checks (loop_vinfo,
                                                     &cond_expr_stmt_list);
      initialize_original_copy_tables ();
      nloop = loop_version (loops, loop, cond_expr, &condition_bb, true);
      free_original_copy_tables();

      /** Loop versioning violates an assumption we try to maintain during 
	 vectorization - that the loop exit block has a single predecessor.
	 After versioning, the exit block of both loop versions is the same
	 basic block (i.e. it has two predecessors). Just in order to simplify
	 following transformations in the vectorizer, we fix this situation
	 here by adding a new (empty) block on the exit-edge of the loop,
	 with the proper loop-exit phis to maintain loop-closed-form.  **/
      
      merge_bb = loop->single_exit->dest;
      gcc_assert (EDGE_COUNT (merge_bb->preds) == 2);
      new_exit_bb = split_edge (loop->single_exit);
      add_bb_to_loop (new_exit_bb, loop->outer);
      new_exit_e = loop->single_exit;
      e = EDGE_SUCC (new_exit_bb, 0);

      for (orig_phi = phi_nodes (merge_bb); orig_phi; 
	   orig_phi = PHI_CHAIN (orig_phi))
	{
          new_phi = create_phi_node (SSA_NAME_VAR (PHI_RESULT (orig_phi)),
				     new_exit_bb);
          arg = PHI_ARG_DEF_FROM_EDGE (orig_phi, e);
          add_phi_arg (new_phi, arg, new_exit_e);
	  SET_PHI_ARG_DEF (orig_phi, e->dest_idx, PHI_RESULT (new_phi));
	} 

      /** end loop-exit-fixes after versioning  **/

      update_ssa (TODO_update_ssa);
      cond_exp_bsi = bsi_last (condition_bb);
      bsi_insert_before (&cond_exp_bsi, cond_expr_stmt_list, BSI_SAME_STMT);
    }

  /* CHECKME: we wouldn't need this if we called update_ssa once
     for all loops.  */
  bitmap_zero (vect_vnames_to_rename);

  /* Peel the loop if there are data refs with unknown alignment.
     Only one data ref with unknown store is allowed.  */

  if (LOOP_PEELING_FOR_ALIGNMENT (loop_vinfo))
    vect_do_peeling_for_alignment (loop_vinfo, loops);
  
  /* If the loop has a symbolic number of iterations 'n' (i.e. it's not a
     compile time constant), or it is a constant that doesn't divide by the
     vectorization factor, then an epilog loop needs to be created.
     We therefore duplicate the loop: the original loop will be vectorized,
     and will compute the first (n/VF) iterations. The second copy of the loop
     will remain scalar and will compute the remaining (n%VF) iterations.
     (VF is the vectorization factor).  */

  if (!LOOP_VINFO_NITERS_KNOWN_P (loop_vinfo)
      || (LOOP_VINFO_NITERS_KNOWN_P (loop_vinfo)
          && LOOP_VINFO_INT_NITERS (loop_vinfo) % vectorization_factor != 0))
    vect_do_peeling_for_loop_bound (loop_vinfo, &ratio, loops);
  else
    ratio = build_int_cst (TREE_TYPE (LOOP_VINFO_NITERS (loop_vinfo)),
		LOOP_VINFO_INT_NITERS (loop_vinfo) / vectorization_factor);

  /* 1) Make sure the loop header has exactly two entries
     2) Make sure we have a preheader basic block.  */

  gcc_assert (EDGE_COUNT (loop->header->preds) == 2);

  loop_split_edge_with (loop_preheader_edge (loop), NULL);


  /* FORNOW: the vectorizer supports only loops which body consist
     of one basic block (header + empty latch). When the vectorizer will 
     support more involved loop forms, the order by which the BBs are 
     traversed need to be reconsidered.  */

  for (i = 0; i < nbbs; i++)
    {
      basic_block bb = bbs[i];

      for (si = bsi_start (bb); !bsi_end_p (si);)
	{
	  tree stmt = bsi_stmt (si);
	  stmt_vec_info stmt_info;
	  bool is_store;

	  if (vect_print_dump_info (REPORT_DETAILS))
	    {
	      fprintf (vect_dump, "------>vectorizing statement: ");
	      print_generic_expr (vect_dump, stmt, TDF_SLIM);
	    }	
	  stmt_info = vinfo_for_stmt (stmt);
	  gcc_assert (stmt_info);
	  if (!STMT_VINFO_RELEVANT_P (stmt_info)
	      && !STMT_VINFO_LIVE_P (stmt_info))
	    {
	      bsi_next (&si);
	      continue;
	    }
	  /* FORNOW: Verify that all stmts operate on the same number of
	             units and no inner unrolling is necessary.  */
	  gcc_assert 
		(TYPE_VECTOR_SUBPARTS (STMT_VINFO_VECTYPE (stmt_info))
		 == (unsigned HOST_WIDE_INT) vectorization_factor);

	  /* -------- vectorize statement ------------ */
	  if (vect_print_dump_info (REPORT_DETAILS))
	    fprintf (vect_dump, "transform statement.");

	  is_store = vect_transform_stmt (stmt, &si);
	  if (is_store)
	    {
	      /* Free the attached stmt_vec_info and remove the stmt.  */
	      stmt_ann_t ann = stmt_ann (stmt);
	      free (stmt_info);
	      set_stmt_info (ann, NULL);
	      bsi_remove (&si, true);
	      continue;
	    }

	  bsi_next (&si);
	}		        /* stmts in BB */
    }				/* BBs in loop */

  slpeel_make_loop_iterate_ntimes (loop, ratio);

  EXECUTE_IF_SET_IN_BITMAP (vect_vnames_to_rename, 0, j, bi)
    mark_sym_for_renaming (SSA_NAME_VAR (ssa_name (j)));

  /* The memory tags and pointers in vectorized statements need to
     have their SSA forms updated.  FIXME, why can't this be delayed
     until all the loops have been transformed?  */
  update_ssa (TODO_update_ssa);

  if (vect_print_dump_info (REPORT_VECTORIZED_LOOPS))
    fprintf (vect_dump, "LOOP VECTORIZED.");
}
