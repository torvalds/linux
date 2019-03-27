/* Functions related to building classes and their related objects.
   Copyright (C) 1987, 1992, 1993, 1994, 1995, 1996, 1997, 1998,
   1999, 2000, 2001, 2002, 2003, 2004, 2005  Free Software Foundation, Inc.
   Contributed by Michael Tiemann (tiemann@cygnus.com)

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2, or (at your option)
any later version.

GCC is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING.  If not, write to
the Free Software Foundation, 51 Franklin Street, Fifth Floor,
Boston, MA 02110-1301, USA.  */


/* High-level class interface.  */

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "tree.h"
#include "cp-tree.h"
#include "flags.h"
#include "rtl.h"
#include "output.h"
#include "toplev.h"
#include "target.h"
#include "convert.h"
#include "cgraph.h"
#include "tree-dump.h"

/* The number of nested classes being processed.  If we are not in the
   scope of any class, this is zero.  */

int current_class_depth;

/* In order to deal with nested classes, we keep a stack of classes.
   The topmost entry is the innermost class, and is the entry at index
   CURRENT_CLASS_DEPTH  */

typedef struct class_stack_node {
  /* The name of the class.  */
  tree name;

  /* The _TYPE node for the class.  */
  tree type;

  /* The access specifier pending for new declarations in the scope of
     this class.  */
  tree access;

  /* If were defining TYPE, the names used in this class.  */
  splay_tree names_used;

  /* Nonzero if this class is no longer open, because of a call to
     push_to_top_level.  */
  size_t hidden;
}* class_stack_node_t;

typedef struct vtbl_init_data_s
{
  /* The base for which we're building initializers.  */
  tree binfo;
  /* The type of the most-derived type.  */
  tree derived;
  /* The binfo for the dynamic type. This will be TYPE_BINFO (derived),
     unless ctor_vtbl_p is true.  */
  tree rtti_binfo;
  /* The negative-index vtable initializers built up so far.  These
     are in order from least negative index to most negative index.  */
  tree inits;
  /* The last (i.e., most negative) entry in INITS.  */
  tree* last_init;
  /* The binfo for the virtual base for which we're building
     vcall offset initializers.  */
  tree vbase;
  /* The functions in vbase for which we have already provided vcall
     offsets.  */
  VEC(tree,gc) *fns;
  /* The vtable index of the next vcall or vbase offset.  */
  tree index;
  /* Nonzero if we are building the initializer for the primary
     vtable.  */
  int primary_vtbl_p;
  /* Nonzero if we are building the initializer for a construction
     vtable.  */
  int ctor_vtbl_p;
  /* True when adding vcall offset entries to the vtable.  False when
     merely computing the indices.  */
  bool generate_vcall_entries;
} vtbl_init_data;

/* The type of a function passed to walk_subobject_offsets.  */
typedef int (*subobject_offset_fn) (tree, tree, splay_tree);

/* The stack itself.  This is a dynamically resized array.  The
   number of elements allocated is CURRENT_CLASS_STACK_SIZE.  */
static int current_class_stack_size;
static class_stack_node_t current_class_stack;

/* The size of the largest empty class seen in this translation unit.  */
static GTY (()) tree sizeof_biggest_empty_class;

/* An array of all local classes present in this translation unit, in
   declaration order.  */
VEC(tree,gc) *local_classes;

static tree get_vfield_name (tree);
static void finish_struct_anon (tree);
static tree get_vtable_name (tree);
static tree get_basefndecls (tree, tree);
static int build_primary_vtable (tree, tree);
static int build_secondary_vtable (tree);
static void finish_vtbls (tree);
static void modify_vtable_entry (tree, tree, tree, tree, tree *);
static void finish_struct_bits (tree);
static int alter_access (tree, tree, tree);
static void handle_using_decl (tree, tree);
static tree dfs_modify_vtables (tree, void *);
static tree modify_all_vtables (tree, tree);
static void determine_primary_bases (tree);
static void finish_struct_methods (tree);
static void maybe_warn_about_overly_private_class (tree);
static int method_name_cmp (const void *, const void *);
static int resort_method_name_cmp (const void *, const void *);
static void add_implicitly_declared_members (tree, int, int);
static tree fixed_type_or_null (tree, int *, int *);
static tree build_simple_base_path (tree expr, tree binfo);
static tree build_vtbl_ref_1 (tree, tree);
static tree build_vtbl_initializer (tree, tree, tree, tree, int *);
static int count_fields (tree);
static int add_fields_to_record_type (tree, struct sorted_fields_type*, int);
static void check_bitfield_decl (tree);
static void check_field_decl (tree, tree, int *, int *, int *);
static void check_field_decls (tree, tree *, int *, int *);
static tree *build_base_field (record_layout_info, tree, splay_tree, tree *);
static void build_base_fields (record_layout_info, splay_tree, tree *);
static void check_methods (tree);
static void remove_zero_width_bit_fields (tree);
static void check_bases (tree, int *, int *);
static void check_bases_and_members (tree);
static tree create_vtable_ptr (tree, tree *);
static void include_empty_classes (record_layout_info);
static void layout_class_type (tree, tree *);
static void fixup_pending_inline (tree);
static void fixup_inline_methods (tree);
static void propagate_binfo_offsets (tree, tree);
static void layout_virtual_bases (record_layout_info, splay_tree);
static void build_vbase_offset_vtbl_entries (tree, vtbl_init_data *);
static void add_vcall_offset_vtbl_entries_r (tree, vtbl_init_data *);
static void add_vcall_offset_vtbl_entries_1 (tree, vtbl_init_data *);
static void build_vcall_offset_vtbl_entries (tree, vtbl_init_data *);
static void add_vcall_offset (tree, tree, vtbl_init_data *);
static void layout_vtable_decl (tree, int);
static tree dfs_find_final_overrider_pre (tree, void *);
static tree dfs_find_final_overrider_post (tree, void *);
static tree find_final_overrider (tree, tree, tree);
static int make_new_vtable (tree, tree);
static tree get_primary_binfo (tree);
static int maybe_indent_hierarchy (FILE *, int, int);
static tree dump_class_hierarchy_r (FILE *, int, tree, tree, int);
static void dump_class_hierarchy (tree);
static void dump_class_hierarchy_1 (FILE *, int, tree);
static void dump_array (FILE *, tree);
static void dump_vtable (tree, tree, tree);
static void dump_vtt (tree, tree);
static void dump_thunk (FILE *, int, tree);
static tree build_vtable (tree, tree, tree);
static void initialize_vtable (tree, tree);
static void layout_nonempty_base_or_field (record_layout_info,
					   tree, tree, splay_tree);
static tree end_of_class (tree, int);
static bool layout_empty_base (tree, tree, splay_tree);
static void accumulate_vtbl_inits (tree, tree, tree, tree, tree);
static tree dfs_accumulate_vtbl_inits (tree, tree, tree, tree,
					       tree);
static void build_rtti_vtbl_entries (tree, vtbl_init_data *);
static void build_vcall_and_vbase_vtbl_entries (tree, vtbl_init_data *);
static void clone_constructors_and_destructors (tree);
static tree build_clone (tree, tree);
static void update_vtable_entry_for_fn (tree, tree, tree, tree *, unsigned);
static void build_ctor_vtbl_group (tree, tree);
static void build_vtt (tree);
static tree binfo_ctor_vtable (tree);
static tree *build_vtt_inits (tree, tree, tree *, tree *);
static tree dfs_build_secondary_vptr_vtt_inits (tree, void *);
static tree dfs_fixup_binfo_vtbls (tree, void *);
static int record_subobject_offset (tree, tree, splay_tree);
static int check_subobject_offset (tree, tree, splay_tree);
static int walk_subobject_offsets (tree, subobject_offset_fn,
				   tree, splay_tree, tree, int);
static void record_subobject_offsets (tree, tree, splay_tree, bool);
static int layout_conflict_p (tree, tree, splay_tree, int);
static int splay_tree_compare_integer_csts (splay_tree_key k1,
					    splay_tree_key k2);
static void warn_about_ambiguous_bases (tree);
static bool type_requires_array_cookie (tree);
static bool contains_empty_class_p (tree);
static bool base_derived_from (tree, tree);
static int empty_base_at_nonzero_offset_p (tree, tree, splay_tree);
static tree end_of_base (tree);
static tree get_vcall_index (tree, tree);

/* Variables shared between class.c and call.c.  */

#ifdef GATHER_STATISTICS
int n_vtables = 0;
int n_vtable_entries = 0;
int n_vtable_searches = 0;
int n_vtable_elems = 0;
int n_convert_harshness = 0;
int n_compute_conversion_costs = 0;
int n_inner_fields_searched = 0;
#endif

/* Convert to or from a base subobject.  EXPR is an expression of type
   `A' or `A*', an expression of type `B' or `B*' is returned.  To
   convert A to a base B, CODE is PLUS_EXPR and BINFO is the binfo for
   the B base instance within A.  To convert base A to derived B, CODE
   is MINUS_EXPR and BINFO is the binfo for the A instance within B.
   In this latter case, A must not be a morally virtual base of B.
   NONNULL is true if EXPR is known to be non-NULL (this is only
   needed when EXPR is of pointer type).  CV qualifiers are preserved
   from EXPR.  */

tree
build_base_path (enum tree_code code,
		 tree expr,
		 tree binfo,
		 int nonnull)
{
  tree v_binfo = NULL_TREE;
  tree d_binfo = NULL_TREE;
  tree probe;
  tree offset;
  tree target_type;
  tree null_test = NULL;
  tree ptr_target_type;
  int fixed_type_p;
  int want_pointer = TREE_CODE (TREE_TYPE (expr)) == POINTER_TYPE;
  bool has_empty = false;
  bool virtual_access;

  if (expr == error_mark_node || binfo == error_mark_node || !binfo)
    return error_mark_node;

  for (probe = binfo; probe; probe = BINFO_INHERITANCE_CHAIN (probe))
    {
      d_binfo = probe;
      if (is_empty_class (BINFO_TYPE (probe)))
	has_empty = true;
      if (!v_binfo && BINFO_VIRTUAL_P (probe))
	v_binfo = probe;
    }

  probe = TYPE_MAIN_VARIANT (TREE_TYPE (expr));
  if (want_pointer)
    probe = TYPE_MAIN_VARIANT (TREE_TYPE (probe));

  gcc_assert ((code == MINUS_EXPR
	       && SAME_BINFO_TYPE_P (BINFO_TYPE (binfo), probe))
	      || (code == PLUS_EXPR
		  && SAME_BINFO_TYPE_P (BINFO_TYPE (d_binfo), probe)));

  if (binfo == d_binfo)
    /* Nothing to do.  */
    return expr;

  if (code == MINUS_EXPR && v_binfo)
    {
      error ("cannot convert from base %qT to derived type %qT via virtual base %qT",
	     BINFO_TYPE (binfo), BINFO_TYPE (d_binfo), BINFO_TYPE (v_binfo));
      return error_mark_node;
    }

  if (!want_pointer)
    /* This must happen before the call to save_expr.  */
    expr = build_unary_op (ADDR_EXPR, expr, 0);

  offset = BINFO_OFFSET (binfo);
  fixed_type_p = resolves_to_fixed_type_p (expr, &nonnull);
  target_type = code == PLUS_EXPR ? BINFO_TYPE (binfo) : BINFO_TYPE (d_binfo);

  /* Do we need to look in the vtable for the real offset?  */
  virtual_access = (v_binfo && fixed_type_p <= 0);

  /* Do we need to check for a null pointer?  */
  if (want_pointer && !nonnull)
    {
      /* If we know the conversion will not actually change the value
	 of EXPR, then we can avoid testing the expression for NULL.
	 We have to avoid generating a COMPONENT_REF for a base class
	 field, because other parts of the compiler know that such
	 expressions are always non-NULL.  */
      if (!virtual_access && integer_zerop (offset))
	{
	  tree class_type;
	  /* TARGET_TYPE has been extracted from BINFO, and, is
	     therefore always cv-unqualified.  Extract the
	     cv-qualifiers from EXPR so that the expression returned
	     matches the input.  */
	  class_type = TREE_TYPE (TREE_TYPE (expr));
	  target_type
	    = cp_build_qualified_type (target_type,
				       cp_type_quals (class_type));
	  return build_nop (build_pointer_type (target_type), expr);
	}
      null_test = error_mark_node;
    }

  /* Protect against multiple evaluation if necessary.  */
  if (TREE_SIDE_EFFECTS (expr) && (null_test || virtual_access))
    expr = save_expr (expr);

  /* Now that we've saved expr, build the real null test.  */
  if (null_test)
    {
      tree zero = cp_convert (TREE_TYPE (expr), integer_zero_node);
      null_test = fold_build2 (NE_EXPR, boolean_type_node,
			       expr, zero);
    }

  /* If this is a simple base reference, express it as a COMPONENT_REF.  */
  if (code == PLUS_EXPR && !virtual_access
      /* We don't build base fields for empty bases, and they aren't very
	 interesting to the optimizers anyway.  */
      && !has_empty)
    {
      expr = build_indirect_ref (expr, NULL);
      expr = build_simple_base_path (expr, binfo);
      if (want_pointer)
	expr = build_address (expr);
      target_type = TREE_TYPE (expr);
      goto out;
    }

  if (virtual_access)
    {
      /* Going via virtual base V_BINFO.  We need the static offset
	 from V_BINFO to BINFO, and the dynamic offset from D_BINFO to
	 V_BINFO.  That offset is an entry in D_BINFO's vtable.  */
      tree v_offset;

      if (fixed_type_p < 0 && in_base_initializer)
	{
	  /* In a base member initializer, we cannot rely on the
	     vtable being set up.  We have to indirect via the
	     vtt_parm.  */
	  tree t;

	  t = TREE_TYPE (TYPE_VFIELD (current_class_type));
	  t = build_pointer_type (t);
	  v_offset = convert (t, current_vtt_parm);
	  v_offset = build_indirect_ref (v_offset, NULL);
	}
      else
	v_offset = build_vfield_ref (build_indirect_ref (expr, NULL),
				     TREE_TYPE (TREE_TYPE (expr)));

      v_offset = build2 (PLUS_EXPR, TREE_TYPE (v_offset),
			 v_offset,  BINFO_VPTR_FIELD (v_binfo));
      v_offset = build1 (NOP_EXPR,
			 build_pointer_type (ptrdiff_type_node),
			 v_offset);
      v_offset = build_indirect_ref (v_offset, NULL);
      TREE_CONSTANT (v_offset) = 1;
      TREE_INVARIANT (v_offset) = 1;

      offset = convert_to_integer (ptrdiff_type_node,
				   size_diffop (offset,
						BINFO_OFFSET (v_binfo)));

      if (!integer_zerop (offset))
	v_offset = build2 (code, ptrdiff_type_node, v_offset, offset);

      if (fixed_type_p < 0)
	/* Negative fixed_type_p means this is a constructor or destructor;
	   virtual base layout is fixed in in-charge [cd]tors, but not in
	   base [cd]tors.  */
	offset = build3 (COND_EXPR, ptrdiff_type_node,
			 build2 (EQ_EXPR, boolean_type_node,
				 current_in_charge_parm, integer_zero_node),
			 v_offset,
			 convert_to_integer (ptrdiff_type_node,
					     BINFO_OFFSET (binfo)));
      else
	offset = v_offset;
    }

  target_type = cp_build_qualified_type
    (target_type, cp_type_quals (TREE_TYPE (TREE_TYPE (expr))));
  ptr_target_type = build_pointer_type (target_type);
  if (want_pointer)
    target_type = ptr_target_type;

  expr = build1 (NOP_EXPR, ptr_target_type, expr);

  if (!integer_zerop (offset))
    expr = build2 (code, ptr_target_type, expr, offset);
  else
    null_test = NULL;

  if (!want_pointer)
    expr = build_indirect_ref (expr, NULL);

 out:
  if (null_test)
    expr = fold_build3 (COND_EXPR, target_type, null_test, expr,
			fold_build1 (NOP_EXPR, target_type,
				     integer_zero_node));

  return expr;
}

/* Subroutine of build_base_path; EXPR and BINFO are as in that function.
   Perform a derived-to-base conversion by recursively building up a
   sequence of COMPONENT_REFs to the appropriate base fields.  */

static tree
build_simple_base_path (tree expr, tree binfo)
{
  tree type = BINFO_TYPE (binfo);
  tree d_binfo = BINFO_INHERITANCE_CHAIN (binfo);
  tree field;

  if (d_binfo == NULL_TREE)
    {
      tree temp;

      gcc_assert (TYPE_MAIN_VARIANT (TREE_TYPE (expr)) == type);

      /* Transform `(a, b).x' into `(*(a, &b)).x', `(a ? b : c).x'
	 into `(*(a ?  &b : &c)).x', and so on.  A COND_EXPR is only
	 an lvalue in the frontend; only _DECLs and _REFs are lvalues
	 in the backend.  */
      temp = unary_complex_lvalue (ADDR_EXPR, expr);
      if (temp)
	expr = build_indirect_ref (temp, NULL);

      return expr;
    }

  /* Recurse.  */
  expr = build_simple_base_path (expr, d_binfo);

  for (field = TYPE_FIELDS (BINFO_TYPE (d_binfo));
       field; field = TREE_CHAIN (field))
    /* Is this the base field created by build_base_field?  */
    if (TREE_CODE (field) == FIELD_DECL
	&& DECL_FIELD_IS_BASE (field)
	&& TREE_TYPE (field) == type)
      {
	/* We don't use build_class_member_access_expr here, as that
	   has unnecessary checks, and more importantly results in
	   recursive calls to dfs_walk_once.  */
	int type_quals = cp_type_quals (TREE_TYPE (expr));

	expr = build3 (COMPONENT_REF,
		       cp_build_qualified_type (type, type_quals),
		       expr, field, NULL_TREE);
	expr = fold_if_not_in_template (expr);

	/* Mark the expression const or volatile, as appropriate.
	   Even though we've dealt with the type above, we still have
	   to mark the expression itself.  */
	if (type_quals & TYPE_QUAL_CONST)
	  TREE_READONLY (expr) = 1;
	if (type_quals & TYPE_QUAL_VOLATILE)
	  TREE_THIS_VOLATILE (expr) = 1;

	return expr;
      }

  /* Didn't find the base field?!?  */
  gcc_unreachable ();
}

/* Convert OBJECT to the base TYPE.  OBJECT is an expression whose
   type is a class type or a pointer to a class type.  In the former
   case, TYPE is also a class type; in the latter it is another
   pointer type.  If CHECK_ACCESS is true, an error message is emitted
   if TYPE is inaccessible.  If OBJECT has pointer type, the value is
   assumed to be non-NULL.  */

tree
convert_to_base (tree object, tree type, bool check_access, bool nonnull)
{
  tree binfo;
  tree object_type;

  if (TYPE_PTR_P (TREE_TYPE (object)))
    {
      object_type = TREE_TYPE (TREE_TYPE (object));
      type = TREE_TYPE (type);
    }
  else
    object_type = TREE_TYPE (object);

  binfo = lookup_base (object_type, type,
		       check_access ? ba_check : ba_unique,
		       NULL);
  if (!binfo || binfo == error_mark_node)
    return error_mark_node;

  return build_base_path (PLUS_EXPR, object, binfo, nonnull);
}

/* EXPR is an expression with unqualified class type.  BASE is a base
   binfo of that class type.  Returns EXPR, converted to the BASE
   type.  This function assumes that EXPR is the most derived class;
   therefore virtual bases can be found at their static offsets.  */

tree
convert_to_base_statically (tree expr, tree base)
{
  tree expr_type;

  expr_type = TREE_TYPE (expr);
  if (!SAME_BINFO_TYPE_P (BINFO_TYPE (base), expr_type))
    {
      tree pointer_type;

      pointer_type = build_pointer_type (expr_type);
      expr = build_unary_op (ADDR_EXPR, expr, /*noconvert=*/1);
      if (!integer_zerop (BINFO_OFFSET (base)))
	  expr = build2 (PLUS_EXPR, pointer_type, expr,
			 build_nop (pointer_type, BINFO_OFFSET (base)));
      expr = build_nop (build_pointer_type (BINFO_TYPE (base)), expr);
      expr = build1 (INDIRECT_REF, BINFO_TYPE (base), expr);
    }

  return expr;
}


tree
build_vfield_ref (tree datum, tree type)
{
  tree vfield, vcontext;

  if (datum == error_mark_node)
    return error_mark_node;

  /* First, convert to the requested type.  */
  if (!same_type_ignoring_top_level_qualifiers_p (TREE_TYPE (datum), type))
    datum = convert_to_base (datum, type, /*check_access=*/false,
			     /*nonnull=*/true);

  /* Second, the requested type may not be the owner of its own vptr.
     If not, convert to the base class that owns it.  We cannot use
     convert_to_base here, because VCONTEXT may appear more than once
     in the inheritance hierarchy of TYPE, and thus direct conversion
     between the types may be ambiguous.  Following the path back up
     one step at a time via primary bases avoids the problem.  */
  vfield = TYPE_VFIELD (type);
  vcontext = DECL_CONTEXT (vfield);
  while (!same_type_ignoring_top_level_qualifiers_p (vcontext, type))
    {
      datum = build_simple_base_path (datum, CLASSTYPE_PRIMARY_BINFO (type));
      type = TREE_TYPE (datum);
    }

  return build3 (COMPONENT_REF, TREE_TYPE (vfield), datum, vfield, NULL_TREE);
}

/* Given an object INSTANCE, return an expression which yields the
   vtable element corresponding to INDEX.  There are many special
   cases for INSTANCE which we take care of here, mainly to avoid
   creating extra tree nodes when we don't have to.  */

static tree
build_vtbl_ref_1 (tree instance, tree idx)
{
  tree aref;
  tree vtbl = NULL_TREE;

  /* Try to figure out what a reference refers to, and
     access its virtual function table directly.  */

  int cdtorp = 0;
  tree fixed_type = fixed_type_or_null (instance, NULL, &cdtorp);

  tree basetype = non_reference (TREE_TYPE (instance));

  if (fixed_type && !cdtorp)
    {
      tree binfo = lookup_base (fixed_type, basetype,
				ba_unique | ba_quiet, NULL);
      if (binfo)
	vtbl = unshare_expr (BINFO_VTABLE (binfo));
    }

  if (!vtbl)
    vtbl = build_vfield_ref (instance, basetype);

  assemble_external (vtbl);

  aref = build_array_ref (vtbl, idx);
  TREE_CONSTANT (aref) |= TREE_CONSTANT (vtbl) && TREE_CONSTANT (idx);
  TREE_INVARIANT (aref) = TREE_CONSTANT (aref);

  return aref;
}

tree
build_vtbl_ref (tree instance, tree idx)
{
  tree aref = build_vtbl_ref_1 (instance, idx);

  return aref;
}

/* Given a stable object pointer INSTANCE_PTR, return an expression which
   yields a function pointer corresponding to vtable element INDEX.  */

tree
build_vfn_ref (tree instance_ptr, tree idx)
{
  tree aref;

  aref = build_vtbl_ref_1 (build_indirect_ref (instance_ptr, 0), idx);

  /* When using function descriptors, the address of the
     vtable entry is treated as a function pointer.  */
  if (TARGET_VTABLE_USES_DESCRIPTORS)
    aref = build1 (NOP_EXPR, TREE_TYPE (aref),
		   build_unary_op (ADDR_EXPR, aref, /*noconvert=*/1));

  /* Remember this as a method reference, for later devirtualization.  */
  aref = build3 (OBJ_TYPE_REF, TREE_TYPE (aref), aref, instance_ptr, idx);

  return aref;
}

/* Return the name of the virtual function table (as an IDENTIFIER_NODE)
   for the given TYPE.  */

static tree
get_vtable_name (tree type)
{
  return mangle_vtbl_for_type (type);
}

/* DECL is an entity associated with TYPE, like a virtual table or an
   implicitly generated constructor.  Determine whether or not DECL
   should have external or internal linkage at the object file
   level.  This routine does not deal with COMDAT linkage and other
   similar complexities; it simply sets TREE_PUBLIC if it possible for
   entities in other translation units to contain copies of DECL, in
   the abstract.  */

void
set_linkage_according_to_type (tree type, tree decl)
{
  /* If TYPE involves a local class in a function with internal
     linkage, then DECL should have internal linkage too.  Other local
     classes have no linkage -- but if their containing functions
     have external linkage, it makes sense for DECL to have external
     linkage too.  That will allow template definitions to be merged,
     for example.  */
  if (no_linkage_check (type, /*relaxed_p=*/true))
    {
      TREE_PUBLIC (decl) = 0;
      DECL_INTERFACE_KNOWN (decl) = 1;
    }
  else
    TREE_PUBLIC (decl) = 1;
}

/* Create a VAR_DECL for a primary or secondary vtable for CLASS_TYPE.
   (For a secondary vtable for B-in-D, CLASS_TYPE should be D, not B.)
   Use NAME for the name of the vtable, and VTABLE_TYPE for its type.  */

static tree
build_vtable (tree class_type, tree name, tree vtable_type)
{
  tree decl;

  decl = build_lang_decl (VAR_DECL, name, vtable_type);
  /* vtable names are already mangled; give them their DECL_ASSEMBLER_NAME
     now to avoid confusion in mangle_decl.  */
  SET_DECL_ASSEMBLER_NAME (decl, name);
  DECL_CONTEXT (decl) = class_type;
  DECL_ARTIFICIAL (decl) = 1;
  TREE_STATIC (decl) = 1;
  TREE_READONLY (decl) = 1;
  DECL_VIRTUAL_P (decl) = 1;
  DECL_ALIGN (decl) = TARGET_VTABLE_ENTRY_ALIGN;
  DECL_VTABLE_OR_VTT_P (decl) = 1;
  /* At one time the vtable info was grabbed 2 words at a time.  This
     fails on sparc unless you have 8-byte alignment.  (tiemann) */
  DECL_ALIGN (decl) = MAX (TYPE_ALIGN (double_type_node),
			   DECL_ALIGN (decl));
  set_linkage_according_to_type (class_type, decl);
  /* The vtable has not been defined -- yet.  */
  DECL_EXTERNAL (decl) = 1;
  DECL_NOT_REALLY_EXTERN (decl) = 1;

  /* Mark the VAR_DECL node representing the vtable itself as a
     "gratuitous" one, thereby forcing dwarfout.c to ignore it.  It
     is rather important that such things be ignored because any
     effort to actually generate DWARF for them will run into
     trouble when/if we encounter code like:

     #pragma interface
     struct S { virtual void member (); };

     because the artificial declaration of the vtable itself (as
     manufactured by the g++ front end) will say that the vtable is
     a static member of `S' but only *after* the debug output for
     the definition of `S' has already been output.  This causes
     grief because the DWARF entry for the definition of the vtable
     will try to refer back to an earlier *declaration* of the
     vtable as a static member of `S' and there won't be one.  We
     might be able to arrange to have the "vtable static member"
     attached to the member list for `S' before the debug info for
     `S' get written (which would solve the problem) but that would
     require more intrusive changes to the g++ front end.  */
  DECL_IGNORED_P (decl) = 1;

  return decl;
}

/* Get the VAR_DECL of the vtable for TYPE. TYPE need not be polymorphic,
   or even complete.  If this does not exist, create it.  If COMPLETE is
   nonzero, then complete the definition of it -- that will render it
   impossible to actually build the vtable, but is useful to get at those
   which are known to exist in the runtime.  */

tree
get_vtable_decl (tree type, int complete)
{
  tree decl;

  if (CLASSTYPE_VTABLES (type))
    return CLASSTYPE_VTABLES (type);

  decl = build_vtable (type, get_vtable_name (type), vtbl_type_node);
  CLASSTYPE_VTABLES (type) = decl;

  if (complete)
    {
      DECL_EXTERNAL (decl) = 1;
      finish_decl (decl, NULL_TREE, NULL_TREE);
    }

  return decl;
}

/* Build the primary virtual function table for TYPE.  If BINFO is
   non-NULL, build the vtable starting with the initial approximation
   that it is the same as the one which is the head of the association
   list.  Returns a nonzero value if a new vtable is actually
   created.  */

static int
build_primary_vtable (tree binfo, tree type)
{
  tree decl;
  tree virtuals;

  decl = get_vtable_decl (type, /*complete=*/0);

  if (binfo)
    {
      if (BINFO_NEW_VTABLE_MARKED (binfo))
	/* We have already created a vtable for this base, so there's
	   no need to do it again.  */
	return 0;

      virtuals = copy_list (BINFO_VIRTUALS (binfo));
      TREE_TYPE (decl) = TREE_TYPE (get_vtbl_decl_for_binfo (binfo));
      DECL_SIZE (decl) = TYPE_SIZE (TREE_TYPE (decl));
      DECL_SIZE_UNIT (decl) = TYPE_SIZE_UNIT (TREE_TYPE (decl));
    }
  else
    {
      gcc_assert (TREE_TYPE (decl) == vtbl_type_node);
      virtuals = NULL_TREE;
    }

#ifdef GATHER_STATISTICS
  n_vtables += 1;
  n_vtable_elems += list_length (virtuals);
#endif

  /* Initialize the association list for this type, based
     on our first approximation.  */
  BINFO_VTABLE (TYPE_BINFO (type)) = decl;
  BINFO_VIRTUALS (TYPE_BINFO (type)) = virtuals;
  SET_BINFO_NEW_VTABLE_MARKED (TYPE_BINFO (type));
  return 1;
}

/* Give BINFO a new virtual function table which is initialized
   with a skeleton-copy of its original initialization.  The only
   entry that changes is the `delta' entry, so we can really
   share a lot of structure.

   FOR_TYPE is the most derived type which caused this table to
   be needed.

   Returns nonzero if we haven't met BINFO before.

   The order in which vtables are built (by calling this function) for
   an object must remain the same, otherwise a binary incompatibility
   can result.  */

static int
build_secondary_vtable (tree binfo)
{
  if (BINFO_NEW_VTABLE_MARKED (binfo))
    /* We already created a vtable for this base.  There's no need to
       do it again.  */
    return 0;

  /* Remember that we've created a vtable for this BINFO, so that we
     don't try to do so again.  */
  SET_BINFO_NEW_VTABLE_MARKED (binfo);

  /* Make fresh virtual list, so we can smash it later.  */
  BINFO_VIRTUALS (binfo) = copy_list (BINFO_VIRTUALS (binfo));

  /* Secondary vtables are laid out as part of the same structure as
     the primary vtable.  */
  BINFO_VTABLE (binfo) = NULL_TREE;
  return 1;
}

/* Create a new vtable for BINFO which is the hierarchy dominated by
   T. Return nonzero if we actually created a new vtable.  */

static int
make_new_vtable (tree t, tree binfo)
{
  if (binfo == TYPE_BINFO (t))
    /* In this case, it is *type*'s vtable we are modifying.  We start
       with the approximation that its vtable is that of the
       immediate base class.  */
    return build_primary_vtable (binfo, t);
  else
    /* This is our very own copy of `basetype' to play with.  Later,
       we will fill in all the virtual functions that override the
       virtual functions in these base classes which are not defined
       by the current type.  */
    return build_secondary_vtable (binfo);
}

/* Make *VIRTUALS, an entry on the BINFO_VIRTUALS list for BINFO
   (which is in the hierarchy dominated by T) list FNDECL as its
   BV_FN.  DELTA is the required constant adjustment from the `this'
   pointer where the vtable entry appears to the `this' required when
   the function is actually called.  */

static void
modify_vtable_entry (tree t,
		     tree binfo,
		     tree fndecl,
		     tree delta,
		     tree *virtuals)
{
  tree v;

  v = *virtuals;

  if (fndecl != BV_FN (v)
      || !tree_int_cst_equal (delta, BV_DELTA (v)))
    {
      /* We need a new vtable for BINFO.  */
      if (make_new_vtable (t, binfo))
	{
	  /* If we really did make a new vtable, we also made a copy
	     of the BINFO_VIRTUALS list.  Now, we have to find the
	     corresponding entry in that list.  */
	  *virtuals = BINFO_VIRTUALS (binfo);
	  while (BV_FN (*virtuals) != BV_FN (v))
	    *virtuals = TREE_CHAIN (*virtuals);
	  v = *virtuals;
	}

      BV_DELTA (v) = delta;
      BV_VCALL_INDEX (v) = NULL_TREE;
      BV_FN (v) = fndecl;
    }
}


/* Add method METHOD to class TYPE.  If USING_DECL is non-null, it is
   the USING_DECL naming METHOD.  Returns true if the method could be
   added to the method vec.  */

bool
add_method (tree type, tree method, tree using_decl)
{
  unsigned slot;
  tree overload;
  bool template_conv_p = false;
  bool conv_p;
  VEC(tree,gc) *method_vec;
  bool complete_p;
  bool insert_p = false;
  tree current_fns;

  if (method == error_mark_node)
    return false;

  complete_p = COMPLETE_TYPE_P (type);
  conv_p = DECL_CONV_FN_P (method);
  if (conv_p)
    template_conv_p = (TREE_CODE (method) == TEMPLATE_DECL
		       && DECL_TEMPLATE_CONV_FN_P (method));

  method_vec = CLASSTYPE_METHOD_VEC (type);
  if (!method_vec)
    {
      /* Make a new method vector.  We start with 8 entries.  We must
	 allocate at least two (for constructors and destructors), and
	 we're going to end up with an assignment operator at some
	 point as well.  */
      method_vec = VEC_alloc (tree, gc, 8);
      /* Create slots for constructors and destructors.  */
      VEC_quick_push (tree, method_vec, NULL_TREE);
      VEC_quick_push (tree, method_vec, NULL_TREE);
      CLASSTYPE_METHOD_VEC (type) = method_vec;
    }

  /* Maintain TYPE_HAS_CONSTRUCTOR, etc.  */
  grok_special_member_properties (method);

  /* Constructors and destructors go in special slots.  */
  if (DECL_MAYBE_IN_CHARGE_CONSTRUCTOR_P (method))
    slot = CLASSTYPE_CONSTRUCTOR_SLOT;
  else if (DECL_MAYBE_IN_CHARGE_DESTRUCTOR_P (method))
    {
      slot = CLASSTYPE_DESTRUCTOR_SLOT;

      if (TYPE_FOR_JAVA (type))
	{
	  if (!DECL_ARTIFICIAL (method))
	    error ("Java class %qT cannot have a destructor", type);
	  else if (TYPE_HAS_NONTRIVIAL_DESTRUCTOR (type))
	    error ("Java class %qT cannot have an implicit non-trivial "
		   "destructor",
		   type);
	}
    }
  else
    {
      tree m;

      insert_p = true;
      /* See if we already have an entry with this name.  */
      for (slot = CLASSTYPE_FIRST_CONVERSION_SLOT;
	   VEC_iterate (tree, method_vec, slot, m);
	   ++slot)
	{
	  m = OVL_CURRENT (m);
	  if (template_conv_p)
	    {
	      if (TREE_CODE (m) == TEMPLATE_DECL
		  && DECL_TEMPLATE_CONV_FN_P (m))
		insert_p = false;
	      break;
	    }
	  if (conv_p && !DECL_CONV_FN_P (m))
	    break;
	  if (DECL_NAME (m) == DECL_NAME (method))
	    {
	      insert_p = false;
	      break;
	    }
	  if (complete_p
	      && !DECL_CONV_FN_P (m)
	      && DECL_NAME (m) > DECL_NAME (method))
	    break;
	}
    }
  current_fns = insert_p ? NULL_TREE : VEC_index (tree, method_vec, slot);

  if (processing_template_decl)
    /* TYPE is a template class.  Don't issue any errors now; wait
       until instantiation time to complain.  */
    ;
  else
    {
      tree fns;

      /* Check to see if we've already got this method.  */
      for (fns = current_fns; fns; fns = OVL_NEXT (fns))
	{
	  tree fn = OVL_CURRENT (fns);
	  tree fn_type;
	  tree method_type;
	  tree parms1;
	  tree parms2;

	  if (TREE_CODE (fn) != TREE_CODE (method))
	    continue;

	  /* [over.load] Member function declarations with the
	     same name and the same parameter types cannot be
	     overloaded if any of them is a static member
	     function declaration.

	     [namespace.udecl] When a using-declaration brings names
	     from a base class into a derived class scope, member
	     functions in the derived class override and/or hide member
	     functions with the same name and parameter types in a base
	     class (rather than conflicting).  */
	  fn_type = TREE_TYPE (fn);
	  method_type = TREE_TYPE (method);
	  parms1 = TYPE_ARG_TYPES (fn_type);
	  parms2 = TYPE_ARG_TYPES (method_type);

	  /* Compare the quals on the 'this' parm.  Don't compare
	     the whole types, as used functions are treated as
	     coming from the using class in overload resolution.  */
	  if (! DECL_STATIC_FUNCTION_P (fn)
	      && ! DECL_STATIC_FUNCTION_P (method)
	      && (TYPE_QUALS (TREE_TYPE (TREE_VALUE (parms1)))
		  != TYPE_QUALS (TREE_TYPE (TREE_VALUE (parms2)))))
	    continue;

	  /* For templates, the return type and template parameters
	     must be identical.  */
	  if (TREE_CODE (fn) == TEMPLATE_DECL
	      && (!same_type_p (TREE_TYPE (fn_type),
				TREE_TYPE (method_type))
		  || !comp_template_parms (DECL_TEMPLATE_PARMS (fn),
					   DECL_TEMPLATE_PARMS (method))))
	    continue;

	  if (! DECL_STATIC_FUNCTION_P (fn))
	    parms1 = TREE_CHAIN (parms1);
	  if (! DECL_STATIC_FUNCTION_P (method))
	    parms2 = TREE_CHAIN (parms2);

	  if (compparms (parms1, parms2)
	      && (!DECL_CONV_FN_P (fn)
		  || same_type_p (TREE_TYPE (fn_type),
				  TREE_TYPE (method_type))))
	    {
	      if (using_decl)
		{
		  if (DECL_CONTEXT (fn) == type)
		    /* Defer to the local function.  */
		    return false;
		  if (DECL_CONTEXT (fn) == DECL_CONTEXT (method))
		    error ("repeated using declaration %q+D", using_decl);
		  else
		    error ("using declaration %q+D conflicts with a previous using declaration",
			   using_decl);
		}
	      else
		{
		  error ("%q+#D cannot be overloaded", method);
		  error ("with %q+#D", fn);
		}

	      /* We don't call duplicate_decls here to merge the
		 declarations because that will confuse things if the
		 methods have inline definitions.  In particular, we
		 will crash while processing the definitions.  */
	      return false;
	    }
	}
    }

  /* A class should never have more than one destructor.  */
  if (current_fns && DECL_MAYBE_IN_CHARGE_DESTRUCTOR_P (method))
    return false;

  /* Add the new binding.  */
  overload = build_overload (method, current_fns);

  if (conv_p)
    TYPE_HAS_CONVERSION (type) = 1;
  else if (slot >= CLASSTYPE_FIRST_CONVERSION_SLOT && !complete_p)
    push_class_level_binding (DECL_NAME (method), overload);

  if (insert_p)
    {
      bool reallocated;

      /* We only expect to add few methods in the COMPLETE_P case, so
	 just make room for one more method in that case.  */
      if (complete_p)
	reallocated = VEC_reserve_exact (tree, gc, method_vec, 1);
      else
	reallocated = VEC_reserve (tree, gc, method_vec, 1);
      if (reallocated)
	CLASSTYPE_METHOD_VEC (type) = method_vec;
      if (slot == VEC_length (tree, method_vec))
	VEC_quick_push (tree, method_vec, overload);
      else
	VEC_quick_insert (tree, method_vec, slot, overload);
    }
  else
    /* Replace the current slot.  */
    VEC_replace (tree, method_vec, slot, overload);
  return true;
}

/* Subroutines of finish_struct.  */

/* Change the access of FDECL to ACCESS in T.  Return 1 if change was
   legit, otherwise return 0.  */

static int
alter_access (tree t, tree fdecl, tree access)
{
  tree elem;

  if (!DECL_LANG_SPECIFIC (fdecl))
    retrofit_lang_decl (fdecl);

  gcc_assert (!DECL_DISCRIMINATOR_P (fdecl));

  elem = purpose_member (t, DECL_ACCESS (fdecl));
  if (elem)
    {
      if (TREE_VALUE (elem) != access)
	{
	  if (TREE_CODE (TREE_TYPE (fdecl)) == FUNCTION_DECL)
	    error ("conflicting access specifications for method"
		   " %q+D, ignored", TREE_TYPE (fdecl));
	  else
	    error ("conflicting access specifications for field %qE, ignored",
		   DECL_NAME (fdecl));
	}
      else
	{
	  /* They're changing the access to the same thing they changed
	     it to before.  That's OK.  */
	  ;
	}
    }
  else
    {
      perform_or_defer_access_check (TYPE_BINFO (t), fdecl, fdecl);
      DECL_ACCESS (fdecl) = tree_cons (t, access, DECL_ACCESS (fdecl));
      return 1;
    }
  return 0;
}

/* Process the USING_DECL, which is a member of T.  */

static void
handle_using_decl (tree using_decl, tree t)
{
  tree decl = USING_DECL_DECLS (using_decl);
  tree name = DECL_NAME (using_decl);
  tree access
    = TREE_PRIVATE (using_decl) ? access_private_node
    : TREE_PROTECTED (using_decl) ? access_protected_node
    : access_public_node;
  tree flist = NULL_TREE;
  tree old_value;

  gcc_assert (!processing_template_decl && decl);

  old_value = lookup_member (t, name, /*protect=*/0, /*want_type=*/false);
  if (old_value)
    {
      if (is_overloaded_fn (old_value))
	old_value = OVL_CURRENT (old_value);

      if (DECL_P (old_value) && DECL_CONTEXT (old_value) == t)
	/* OK */;
      else
	old_value = NULL_TREE;
    }

  cp_emit_debug_info_for_using (decl, USING_DECL_SCOPE (using_decl));

  if (is_overloaded_fn (decl))
    flist = decl;

  if (! old_value)
    ;
  else if (is_overloaded_fn (old_value))
    {
      if (flist)
	/* It's OK to use functions from a base when there are functions with
	   the same name already present in the current class.  */;
      else
	{
	  error ("%q+D invalid in %q#T", using_decl, t);
	  error ("  because of local method %q+#D with same name",
		 OVL_CURRENT (old_value));
	  return;
	}
    }
  else if (!DECL_ARTIFICIAL (old_value))
    {
      error ("%q+D invalid in %q#T", using_decl, t);
      error ("  because of local member %q+#D with same name", old_value);
      return;
    }

  /* Make type T see field decl FDECL with access ACCESS.  */
  if (flist)
    for (; flist; flist = OVL_NEXT (flist))
      {
	add_method (t, OVL_CURRENT (flist), using_decl);
	alter_access (t, OVL_CURRENT (flist), access);
      }
  else
    alter_access (t, decl, access);
}

/* Run through the base classes of T, updating CANT_HAVE_CONST_CTOR_P,
   and NO_CONST_ASN_REF_P.  Also set flag bits in T based on
   properties of the bases.  */

static void
check_bases (tree t,
	     int* cant_have_const_ctor_p,
	     int* no_const_asn_ref_p)
{
  int i;
  int seen_non_virtual_nearly_empty_base_p;
  tree base_binfo;
  tree binfo;

  seen_non_virtual_nearly_empty_base_p = 0;

  for (binfo = TYPE_BINFO (t), i = 0;
       BINFO_BASE_ITERATE (binfo, i, base_binfo); i++)
    {
      tree basetype = TREE_TYPE (base_binfo);

      gcc_assert (COMPLETE_TYPE_P (basetype));

      /* Effective C++ rule 14.  We only need to check TYPE_POLYMORPHIC_P
	 here because the case of virtual functions but non-virtual
	 dtor is handled in finish_struct_1.  */
      if (!TYPE_POLYMORPHIC_P (basetype))
	warning (OPT_Weffc__,
		 "base class %q#T has a non-virtual destructor", basetype);

      /* If the base class doesn't have copy constructors or
	 assignment operators that take const references, then the
	 derived class cannot have such a member automatically
	 generated.  */
      if (! TYPE_HAS_CONST_INIT_REF (basetype))
	*cant_have_const_ctor_p = 1;
      if (TYPE_HAS_ASSIGN_REF (basetype)
	  && !TYPE_HAS_CONST_ASSIGN_REF (basetype))
	*no_const_asn_ref_p = 1;

      if (BINFO_VIRTUAL_P (base_binfo))
	/* A virtual base does not effect nearly emptiness.  */
	;
      else if (CLASSTYPE_NEARLY_EMPTY_P (basetype))
	{
	  if (seen_non_virtual_nearly_empty_base_p)
	    /* And if there is more than one nearly empty base, then the
	       derived class is not nearly empty either.  */
	    CLASSTYPE_NEARLY_EMPTY_P (t) = 0;
	  else
	    /* Remember we've seen one.  */
	    seen_non_virtual_nearly_empty_base_p = 1;
	}
      else if (!is_empty_class (basetype))
	/* If the base class is not empty or nearly empty, then this
	   class cannot be nearly empty.  */
	CLASSTYPE_NEARLY_EMPTY_P (t) = 0;

      /* A lot of properties from the bases also apply to the derived
	 class.  */
      TYPE_NEEDS_CONSTRUCTING (t) |= TYPE_NEEDS_CONSTRUCTING (basetype);
      TYPE_HAS_NONTRIVIAL_DESTRUCTOR (t)
	|= TYPE_HAS_NONTRIVIAL_DESTRUCTOR (basetype);
      /* APPLE LOCAL begin omit calls to empty destructors 5559195 */
      if (CLASSTYPE_HAS_NONTRIVIAL_DESTRUCTOR_BODY (basetype)
	  || CLASSTYPE_DESTRUCTOR_NONTRIVIAL_BECAUSE_OF_BASE (basetype))
	CLASSTYPE_DESTRUCTOR_NONTRIVIAL_BECAUSE_OF_BASE (t) = 1;
      /* APPLE LOCAL end omit calls to empty destructors 5559195 */

      TYPE_HAS_COMPLEX_ASSIGN_REF (t)
	|= TYPE_HAS_COMPLEX_ASSIGN_REF (basetype);
      TYPE_HAS_COMPLEX_INIT_REF (t) |= TYPE_HAS_COMPLEX_INIT_REF (basetype);
      TYPE_POLYMORPHIC_P (t) |= TYPE_POLYMORPHIC_P (basetype);
      CLASSTYPE_CONTAINS_EMPTY_CLASS_P (t)
	|= CLASSTYPE_CONTAINS_EMPTY_CLASS_P (basetype);
    }
}

/* Determine all the primary bases within T.  Sets BINFO_PRIMARY_BASE_P for
   those that are primaries.  Sets BINFO_LOST_PRIMARY_P for those
   that have had a nearly-empty virtual primary base stolen by some
   other base in the hierarchy.  Determines CLASSTYPE_PRIMARY_BASE for
   T.  */

static void
determine_primary_bases (tree t)
{
  unsigned i;
  tree primary = NULL_TREE;
  tree type_binfo = TYPE_BINFO (t);
  tree base_binfo;

  /* Determine the primary bases of our bases.  */
  for (base_binfo = TREE_CHAIN (type_binfo); base_binfo;
       base_binfo = TREE_CHAIN (base_binfo))
    {
      tree primary = CLASSTYPE_PRIMARY_BINFO (BINFO_TYPE (base_binfo));

      /* See if we're the non-virtual primary of our inheritance
	 chain.  */
      if (!BINFO_VIRTUAL_P (base_binfo))
	{
	  tree parent = BINFO_INHERITANCE_CHAIN (base_binfo);
	  tree parent_primary = CLASSTYPE_PRIMARY_BINFO (BINFO_TYPE (parent));

	  if (parent_primary
	      && SAME_BINFO_TYPE_P (BINFO_TYPE (base_binfo),
				    BINFO_TYPE (parent_primary)))
	    /* We are the primary binfo.  */
	    BINFO_PRIMARY_P (base_binfo) = 1;
	}
      /* Determine if we have a virtual primary base, and mark it so.
       */
      if (primary && BINFO_VIRTUAL_P (primary))
	{
	  tree this_primary = copied_binfo (primary, base_binfo);

	  if (BINFO_PRIMARY_P (this_primary))
	    /* Someone already claimed this base.  */
	    BINFO_LOST_PRIMARY_P (base_binfo) = 1;
	  else
	    {
	      tree delta;

	      BINFO_PRIMARY_P (this_primary) = 1;
	      BINFO_INHERITANCE_CHAIN (this_primary) = base_binfo;

	      /* A virtual binfo might have been copied from within
		 another hierarchy. As we're about to use it as a
		 primary base, make sure the offsets match.  */
	      delta = size_diffop (convert (ssizetype,
					    BINFO_OFFSET (base_binfo)),
				   convert (ssizetype,
					    BINFO_OFFSET (this_primary)));

	      propagate_binfo_offsets (this_primary, delta);
	    }
	}
    }

  /* First look for a dynamic direct non-virtual base.  */
  for (i = 0; BINFO_BASE_ITERATE (type_binfo, i, base_binfo); i++)
    {
      tree basetype = BINFO_TYPE (base_binfo);

      if (TYPE_CONTAINS_VPTR_P (basetype) && !BINFO_VIRTUAL_P (base_binfo))
	{
	  primary = base_binfo;
	  goto found;
	}
    }

  /* A "nearly-empty" virtual base class can be the primary base
     class, if no non-virtual polymorphic base can be found.  Look for
     a nearly-empty virtual dynamic base that is not already a primary
     base of something in the hierarchy.  If there is no such base,
     just pick the first nearly-empty virtual base.  */

  for (base_binfo = TREE_CHAIN (type_binfo); base_binfo;
       base_binfo = TREE_CHAIN (base_binfo))
    if (BINFO_VIRTUAL_P (base_binfo)
	&& CLASSTYPE_NEARLY_EMPTY_P (BINFO_TYPE (base_binfo)))
      {
	if (!BINFO_PRIMARY_P (base_binfo))
	  {
	    /* Found one that is not primary.  */
	    primary = base_binfo;
	    goto found;
	  }
	else if (!primary)
	  /* Remember the first candidate.  */
	  primary = base_binfo;
      }

 found:
  /* If we've got a primary base, use it.  */
  if (primary)
    {
      tree basetype = BINFO_TYPE (primary);

      CLASSTYPE_PRIMARY_BINFO (t) = primary;
      if (BINFO_PRIMARY_P (primary))
	/* We are stealing a primary base.  */
	BINFO_LOST_PRIMARY_P (BINFO_INHERITANCE_CHAIN (primary)) = 1;
      BINFO_PRIMARY_P (primary) = 1;
      if (BINFO_VIRTUAL_P (primary))
	{
	  tree delta;

	  BINFO_INHERITANCE_CHAIN (primary) = type_binfo;
	  /* A virtual binfo might have been copied from within
	     another hierarchy. As we're about to use it as a primary
	     base, make sure the offsets match.  */
	  delta = size_diffop (ssize_int (0),
			       convert (ssizetype, BINFO_OFFSET (primary)));

	  propagate_binfo_offsets (primary, delta);
	}

      primary = TYPE_BINFO (basetype);

      TYPE_VFIELD (t) = TYPE_VFIELD (basetype);
      BINFO_VTABLE (type_binfo) = BINFO_VTABLE (primary);
      BINFO_VIRTUALS (type_binfo) = BINFO_VIRTUALS (primary);
    }
}

/* Set memoizing fields and bits of T (and its variants) for later
   use.  */

static void
finish_struct_bits (tree t)
{
  tree variants;

  /* Fix up variants (if any).  */
  for (variants = TYPE_NEXT_VARIANT (t);
       variants;
       variants = TYPE_NEXT_VARIANT (variants))
    {
      /* These fields are in the _TYPE part of the node, not in
	 the TYPE_LANG_SPECIFIC component, so they are not shared.  */
      TYPE_HAS_CONSTRUCTOR (variants) = TYPE_HAS_CONSTRUCTOR (t);
      TYPE_NEEDS_CONSTRUCTING (variants) = TYPE_NEEDS_CONSTRUCTING (t);
      TYPE_HAS_NONTRIVIAL_DESTRUCTOR (variants)
	= TYPE_HAS_NONTRIVIAL_DESTRUCTOR (t);

      /* APPLE LOCAL begin omit calls to empty destructors 5559195 */
      CLASSTYPE_HAS_NONTRIVIAL_DESTRUCTOR_BODY (variants) =
	CLASSTYPE_HAS_NONTRIVIAL_DESTRUCTOR_BODY (t);
      CLASSTYPE_DESTRUCTOR_NONTRIVIAL_BECAUSE_OF_BASE (variants) =
	CLASSTYPE_DESTRUCTOR_NONTRIVIAL_BECAUSE_OF_BASE (t);
      /* APPLE LOCAL end omit calls to empty destructors 5559195 */

      TYPE_POLYMORPHIC_P (variants) = TYPE_POLYMORPHIC_P (t);

      TYPE_BINFO (variants) = TYPE_BINFO (t);

      /* Copy whatever these are holding today.  */
      TYPE_VFIELD (variants) = TYPE_VFIELD (t);
      TYPE_METHODS (variants) = TYPE_METHODS (t);
      TYPE_FIELDS (variants) = TYPE_FIELDS (t);
    }

  if (BINFO_N_BASE_BINFOS (TYPE_BINFO (t)) && TYPE_POLYMORPHIC_P (t))
    /* For a class w/o baseclasses, 'finish_struct' has set
       CLASSTYPE_PURE_VIRTUALS correctly (by definition).
       Similarly for a class whose base classes do not have vtables.
       When neither of these is true, we might have removed abstract
       virtuals (by providing a definition), added some (by declaring
       new ones), or redeclared ones from a base class.  We need to
       recalculate what's really an abstract virtual at this point (by
       looking in the vtables).  */
    get_pure_virtuals (t);

  /* If this type has a copy constructor or a destructor, force its
     mode to be BLKmode, and force its TREE_ADDRESSABLE bit to be
     nonzero.  This will cause it to be passed by invisible reference
     and prevent it from being returned in a register.  */
  if (! TYPE_HAS_TRIVIAL_INIT_REF (t) || TYPE_HAS_NONTRIVIAL_DESTRUCTOR (t))
    {
      tree variants;
      DECL_MODE (TYPE_MAIN_DECL (t)) = BLKmode;
      for (variants = t; variants; variants = TYPE_NEXT_VARIANT (variants))
	{
	  TYPE_MODE (variants) = BLKmode;
	  TREE_ADDRESSABLE (variants) = 1;
	}
    }
}

/* Issue warnings about T having private constructors, but no friends,
   and so forth.

   HAS_NONPRIVATE_METHOD is nonzero if T has any non-private methods or
   static members.  HAS_NONPRIVATE_STATIC_FN is nonzero if T has any
   non-private static member functions.  */

static void
maybe_warn_about_overly_private_class (tree t)
{
  int has_member_fn = 0;
  int has_nonprivate_method = 0;
  tree fn;

  if (!warn_ctor_dtor_privacy
      /* If the class has friends, those entities might create and
	 access instances, so we should not warn.  */
      || (CLASSTYPE_FRIEND_CLASSES (t)
	  || DECL_FRIENDLIST (TYPE_MAIN_DECL (t)))
      /* We will have warned when the template was declared; there's
	 no need to warn on every instantiation.  */
      || CLASSTYPE_TEMPLATE_INSTANTIATION (t))
    /* There's no reason to even consider warning about this
       class.  */
    return;

  /* We only issue one warning, if more than one applies, because
     otherwise, on code like:

     class A {
       // Oops - forgot `public:'
       A();
       A(const A&);
       ~A();
     };

     we warn several times about essentially the same problem.  */

  /* Check to see if all (non-constructor, non-destructor) member
     functions are private.  (Since there are no friends or
     non-private statics, we can't ever call any of the private member
     functions.)  */
  for (fn = TYPE_METHODS (t); fn; fn = TREE_CHAIN (fn))
    /* We're not interested in compiler-generated methods; they don't
       provide any way to call private members.  */
    if (!DECL_ARTIFICIAL (fn))
      {
	if (!TREE_PRIVATE (fn))
	  {
	    if (DECL_STATIC_FUNCTION_P (fn))
	      /* A non-private static member function is just like a
		 friend; it can create and invoke private member
		 functions, and be accessed without a class
		 instance.  */
	      return;

	    has_nonprivate_method = 1;
	    /* Keep searching for a static member function.  */
	  }
	else if (!DECL_CONSTRUCTOR_P (fn) && !DECL_DESTRUCTOR_P (fn))
	  has_member_fn = 1;
      }

  if (!has_nonprivate_method && has_member_fn)
    {
      /* There are no non-private methods, and there's at least one
	 private member function that isn't a constructor or
	 destructor.  (If all the private members are
	 constructors/destructors we want to use the code below that
	 issues error messages specifically referring to
	 constructors/destructors.)  */
      unsigned i;
      tree binfo = TYPE_BINFO (t);

      for (i = 0; i != BINFO_N_BASE_BINFOS (binfo); i++)
	if (BINFO_BASE_ACCESS (binfo, i) != access_private_node)
	  {
	    has_nonprivate_method = 1;
	    break;
	  }
      if (!has_nonprivate_method)
	{
	  warning (OPT_Wctor_dtor_privacy,
		   "all member functions in class %qT are private", t);
	  return;
	}
    }

  /* Even if some of the member functions are non-private, the class
     won't be useful for much if all the constructors or destructors
     are private: such an object can never be created or destroyed.  */
  fn = CLASSTYPE_DESTRUCTORS (t);
  if (fn && TREE_PRIVATE (fn))
    {
      warning (OPT_Wctor_dtor_privacy,
	       "%q#T only defines a private destructor and has no friends",
	       t);
      return;
    }

  if (TYPE_HAS_CONSTRUCTOR (t)
      /* Implicitly generated constructors are always public.  */
      && (!CLASSTYPE_LAZY_DEFAULT_CTOR (t)
	  || !CLASSTYPE_LAZY_COPY_CTOR (t)))
    {
      int nonprivate_ctor = 0;

      /* If a non-template class does not define a copy
	 constructor, one is defined for it, enabling it to avoid
	 this warning.  For a template class, this does not
	 happen, and so we would normally get a warning on:

	   template <class T> class C { private: C(); };

	 To avoid this asymmetry, we check TYPE_HAS_INIT_REF.  All
	 complete non-template or fully instantiated classes have this
	 flag set.  */
      if (!TYPE_HAS_INIT_REF (t))
	nonprivate_ctor = 1;
      else
	for (fn = CLASSTYPE_CONSTRUCTORS (t); fn; fn = OVL_NEXT (fn))
	  {
	    tree ctor = OVL_CURRENT (fn);
	    /* Ideally, we wouldn't count copy constructors (or, in
	       fact, any constructor that takes an argument of the
	       class type as a parameter) because such things cannot
	       be used to construct an instance of the class unless
	       you already have one.  But, for now at least, we're
	       more generous.  */
	    if (! TREE_PRIVATE (ctor))
	      {
		nonprivate_ctor = 1;
		break;
	      }
	  }

      if (nonprivate_ctor == 0)
	{
	  warning (OPT_Wctor_dtor_privacy,
		   "%q#T only defines private constructors and has no friends",
		   t);
	  return;
	}
    }
}

static struct {
  gt_pointer_operator new_value;
  void *cookie;
} resort_data;

/* Comparison function to compare two TYPE_METHOD_VEC entries by name.  */

static int
method_name_cmp (const void* m1_p, const void* m2_p)
{
  const tree *const m1 = (const tree *) m1_p;
  const tree *const m2 = (const tree *) m2_p;

  if (*m1 == NULL_TREE && *m2 == NULL_TREE)
    return 0;
  if (*m1 == NULL_TREE)
    return -1;
  if (*m2 == NULL_TREE)
    return 1;
  if (DECL_NAME (OVL_CURRENT (*m1)) < DECL_NAME (OVL_CURRENT (*m2)))
    return -1;
  return 1;
}

/* This routine compares two fields like method_name_cmp but using the
   pointer operator in resort_field_decl_data.  */

static int
resort_method_name_cmp (const void* m1_p, const void* m2_p)
{
  const tree *const m1 = (const tree *) m1_p;
  const tree *const m2 = (const tree *) m2_p;
  if (*m1 == NULL_TREE && *m2 == NULL_TREE)
    return 0;
  if (*m1 == NULL_TREE)
    return -1;
  if (*m2 == NULL_TREE)
    return 1;
  {
    tree d1 = DECL_NAME (OVL_CURRENT (*m1));
    tree d2 = DECL_NAME (OVL_CURRENT (*m2));
    resort_data.new_value (&d1, resort_data.cookie);
    resort_data.new_value (&d2, resort_data.cookie);
    if (d1 < d2)
      return -1;
  }
  return 1;
}

/* Resort TYPE_METHOD_VEC because pointers have been reordered.  */

void
resort_type_method_vec (void* obj,
			void* orig_obj ATTRIBUTE_UNUSED ,
			gt_pointer_operator new_value,
			void* cookie)
{
  VEC(tree,gc) *method_vec = (VEC(tree,gc) *) obj;
  int len = VEC_length (tree, method_vec);
  size_t slot;
  tree fn;

  /* The type conversion ops have to live at the front of the vec, so we
     can't sort them.  */
  for (slot = CLASSTYPE_FIRST_CONVERSION_SLOT;
       VEC_iterate (tree, method_vec, slot, fn);
       ++slot)
    if (!DECL_CONV_FN_P (OVL_CURRENT (fn)))
      break;

  if (len - slot > 1)
    {
      resort_data.new_value = new_value;
      resort_data.cookie = cookie;
      qsort (VEC_address (tree, method_vec) + slot, len - slot, sizeof (tree),
	     resort_method_name_cmp);
    }
}

/* Warn about duplicate methods in fn_fields.

   Sort methods that are not special (i.e., constructors, destructors,
   and type conversion operators) so that we can find them faster in
   search.  */

static void
finish_struct_methods (tree t)
{
  tree fn_fields;
  VEC(tree,gc) *method_vec;
  int slot, len;

  method_vec = CLASSTYPE_METHOD_VEC (t);
  if (!method_vec)
    return;

  len = VEC_length (tree, method_vec);

  /* Clear DECL_IN_AGGR_P for all functions.  */
  for (fn_fields = TYPE_METHODS (t); fn_fields;
       fn_fields = TREE_CHAIN (fn_fields))
    DECL_IN_AGGR_P (fn_fields) = 0;

  /* Issue warnings about private constructors and such.  If there are
     no methods, then some public defaults are generated.  */
  maybe_warn_about_overly_private_class (t);

  /* The type conversion ops have to live at the front of the vec, so we
     can't sort them.  */
  for (slot = CLASSTYPE_FIRST_CONVERSION_SLOT;
       VEC_iterate (tree, method_vec, slot, fn_fields);
       ++slot)
    if (!DECL_CONV_FN_P (OVL_CURRENT (fn_fields)))
      break;
  if (len - slot > 1)
    qsort (VEC_address (tree, method_vec) + slot,
	   len-slot, sizeof (tree), method_name_cmp);
}

/* Make BINFO's vtable have N entries, including RTTI entries,
   vbase and vcall offsets, etc.  Set its type and call the backend
   to lay it out.  */

static void
layout_vtable_decl (tree binfo, int n)
{
  tree atype;
  tree vtable;

  atype = build_cplus_array_type (vtable_entry_type,
				  build_index_type (size_int (n - 1)));
  layout_type (atype);

  /* We may have to grow the vtable.  */
  vtable = get_vtbl_decl_for_binfo (binfo);
  if (!same_type_p (TREE_TYPE (vtable), atype))
    {
      TREE_TYPE (vtable) = atype;
      DECL_SIZE (vtable) = DECL_SIZE_UNIT (vtable) = NULL_TREE;
      layout_decl (vtable, 0);
    }
}

/* True iff FNDECL and BASE_FNDECL (both non-static member functions)
   have the same signature.  */

int
same_signature_p (tree fndecl, tree base_fndecl)
{
  /* One destructor overrides another if they are the same kind of
     destructor.  */
  if (DECL_DESTRUCTOR_P (base_fndecl) && DECL_DESTRUCTOR_P (fndecl)
      && special_function_p (base_fndecl) == special_function_p (fndecl))
    return 1;
  /* But a non-destructor never overrides a destructor, nor vice
     versa, nor do different kinds of destructors override
     one-another.  For example, a complete object destructor does not
     override a deleting destructor.  */
  if (DECL_DESTRUCTOR_P (base_fndecl) || DECL_DESTRUCTOR_P (fndecl))
    return 0;

  if (DECL_NAME (fndecl) == DECL_NAME (base_fndecl)
      || (DECL_CONV_FN_P (fndecl)
	  && DECL_CONV_FN_P (base_fndecl)
	  && same_type_p (DECL_CONV_FN_TYPE (fndecl),
			  DECL_CONV_FN_TYPE (base_fndecl))))
    {
      tree types, base_types;
      types = TYPE_ARG_TYPES (TREE_TYPE (fndecl));
      base_types = TYPE_ARG_TYPES (TREE_TYPE (base_fndecl));
      if ((TYPE_QUALS (TREE_TYPE (TREE_VALUE (base_types)))
	   == TYPE_QUALS (TREE_TYPE (TREE_VALUE (types))))
	  && compparms (TREE_CHAIN (base_types), TREE_CHAIN (types)))
	return 1;
    }
  return 0;
}

/* Returns TRUE if DERIVED is a binfo containing the binfo BASE as a
   subobject.  */

static bool
base_derived_from (tree derived, tree base)
{
  tree probe;

  for (probe = base; probe; probe = BINFO_INHERITANCE_CHAIN (probe))
    {
      if (probe == derived)
	return true;
      else if (BINFO_VIRTUAL_P (probe))
	/* If we meet a virtual base, we can't follow the inheritance
	   any more.  See if the complete type of DERIVED contains
	   such a virtual base.  */
	return (binfo_for_vbase (BINFO_TYPE (probe), BINFO_TYPE (derived))
		!= NULL_TREE);
    }
  return false;
}

typedef struct find_final_overrider_data_s {
  /* The function for which we are trying to find a final overrider.  */
  tree fn;
  /* The base class in which the function was declared.  */
  tree declaring_base;
  /* The candidate overriders.  */
  tree candidates;
  /* Path to most derived.  */
  VEC(tree,heap) *path;
} find_final_overrider_data;

/* Add the overrider along the current path to FFOD->CANDIDATES.
   Returns true if an overrider was found; false otherwise.  */

static bool
dfs_find_final_overrider_1 (tree binfo,
			    find_final_overrider_data *ffod,
			    unsigned depth)
{
  tree method;

  /* If BINFO is not the most derived type, try a more derived class.
     A definition there will overrider a definition here.  */
  if (depth)
    {
      depth--;
      if (dfs_find_final_overrider_1
	  (VEC_index (tree, ffod->path, depth), ffod, depth))
	return true;
    }

  method = look_for_overrides_here (BINFO_TYPE (binfo), ffod->fn);
  if (method)
    {
      tree *candidate = &ffod->candidates;

      /* Remove any candidates overridden by this new function.  */
      while (*candidate)
	{
	  /* If *CANDIDATE overrides METHOD, then METHOD
	     cannot override anything else on the list.  */
	  if (base_derived_from (TREE_VALUE (*candidate), binfo))
	    return true;
	  /* If METHOD overrides *CANDIDATE, remove *CANDIDATE.  */
	  if (base_derived_from (binfo, TREE_VALUE (*candidate)))
	    *candidate = TREE_CHAIN (*candidate);
	  else
	    candidate = &TREE_CHAIN (*candidate);
	}

      /* Add the new function.  */
      ffod->candidates = tree_cons (method, binfo, ffod->candidates);
      return true;
    }

  return false;
}

/* Called from find_final_overrider via dfs_walk.  */

static tree
dfs_find_final_overrider_pre (tree binfo, void *data)
{
  find_final_overrider_data *ffod = (find_final_overrider_data *) data;

  if (binfo == ffod->declaring_base)
    dfs_find_final_overrider_1 (binfo, ffod, VEC_length (tree, ffod->path));
  VEC_safe_push (tree, heap, ffod->path, binfo);

  return NULL_TREE;
}

static tree
dfs_find_final_overrider_post (tree binfo ATTRIBUTE_UNUSED, void *data)
{
  find_final_overrider_data *ffod = (find_final_overrider_data *) data;
  VEC_pop (tree, ffod->path);

  return NULL_TREE;
}

/* Returns a TREE_LIST whose TREE_PURPOSE is the final overrider for
   FN and whose TREE_VALUE is the binfo for the base where the
   overriding occurs.  BINFO (in the hierarchy dominated by the binfo
   DERIVED) is the base object in which FN is declared.  */

static tree
find_final_overrider (tree derived, tree binfo, tree fn)
{
  find_final_overrider_data ffod;

  /* Getting this right is a little tricky.  This is valid:

       struct S { virtual void f (); };
       struct T { virtual void f (); };
       struct U : public S, public T { };

     even though calling `f' in `U' is ambiguous.  But,

       struct R { virtual void f(); };
       struct S : virtual public R { virtual void f (); };
       struct T : virtual public R { virtual void f (); };
       struct U : public S, public T { };

     is not -- there's no way to decide whether to put `S::f' or
     `T::f' in the vtable for `R'.

     The solution is to look at all paths to BINFO.  If we find
     different overriders along any two, then there is a problem.  */
  if (DECL_THUNK_P (fn))
    fn = THUNK_TARGET (fn);

  /* Determine the depth of the hierarchy.  */
  ffod.fn = fn;
  ffod.declaring_base = binfo;
  ffod.candidates = NULL_TREE;
  ffod.path = VEC_alloc (tree, heap, 30);

  dfs_walk_all (derived, dfs_find_final_overrider_pre,
		dfs_find_final_overrider_post, &ffod);

  VEC_free (tree, heap, ffod.path);

  /* If there was no winner, issue an error message.  */
  if (!ffod.candidates || TREE_CHAIN (ffod.candidates))
    return error_mark_node;

  return ffod.candidates;
}

/* Return the index of the vcall offset for FN when TYPE is used as a
   virtual base.  */

static tree
get_vcall_index (tree fn, tree type)
{
  VEC(tree_pair_s,gc) *indices = CLASSTYPE_VCALL_INDICES (type);
  tree_pair_p p;
  unsigned ix;

  for (ix = 0; VEC_iterate (tree_pair_s, indices, ix, p); ix++)
    if ((DECL_DESTRUCTOR_P (fn) && DECL_DESTRUCTOR_P (p->purpose))
	|| same_signature_p (fn, p->purpose))
      return p->value;

  /* There should always be an appropriate index.  */
  gcc_unreachable ();
}

/* Update an entry in the vtable for BINFO, which is in the hierarchy
   dominated by T.  FN has been overridden in BINFO; VIRTUALS points to the
   corresponding position in the BINFO_VIRTUALS list.  */

static void
update_vtable_entry_for_fn (tree t, tree binfo, tree fn, tree* virtuals,
			    unsigned ix)
{
  tree b;
  tree overrider;
  tree delta;
  tree virtual_base;
  tree first_defn;
  tree overrider_fn, overrider_target;
  tree target_fn = DECL_THUNK_P (fn) ? THUNK_TARGET (fn) : fn;
  tree over_return, base_return;
  bool lost = false;

  /* Find the nearest primary base (possibly binfo itself) which defines
     this function; this is the class the caller will convert to when
     calling FN through BINFO.  */
  for (b = binfo; ; b = get_primary_binfo (b))
    {
      gcc_assert (b);
      if (look_for_overrides_here (BINFO_TYPE (b), target_fn))
	break;

      /* The nearest definition is from a lost primary.  */
      if (BINFO_LOST_PRIMARY_P (b))
	lost = true;
    }
  first_defn = b;

  /* Find the final overrider.  */
  overrider = find_final_overrider (TYPE_BINFO (t), b, target_fn);
  if (overrider == error_mark_node)
    {
      error ("no unique final overrider for %qD in %qT", target_fn, t);
      return;
    }
  overrider_target = overrider_fn = TREE_PURPOSE (overrider);

  /* Check for adjusting covariant return types.  */
  over_return = TREE_TYPE (TREE_TYPE (overrider_target));
  base_return = TREE_TYPE (TREE_TYPE (target_fn));

  if (POINTER_TYPE_P (over_return)
      && TREE_CODE (over_return) == TREE_CODE (base_return)
      && CLASS_TYPE_P (TREE_TYPE (over_return))
      && CLASS_TYPE_P (TREE_TYPE (base_return))
      /* If the overrider is invalid, don't even try.  */
      && !DECL_INVALID_OVERRIDER_P (overrider_target))
    {
      /* If FN is a covariant thunk, we must figure out the adjustment
	 to the final base FN was converting to. As OVERRIDER_TARGET might
	 also be converting to the return type of FN, we have to
	 combine the two conversions here.  */
      tree fixed_offset, virtual_offset;

      over_return = TREE_TYPE (over_return);
      base_return = TREE_TYPE (base_return);

      if (DECL_THUNK_P (fn))
	{
	  gcc_assert (DECL_RESULT_THUNK_P (fn));
	  fixed_offset = ssize_int (THUNK_FIXED_OFFSET (fn));
	  virtual_offset = THUNK_VIRTUAL_OFFSET (fn);
	}
      else
	fixed_offset = virtual_offset = NULL_TREE;

      if (virtual_offset)
	/* Find the equivalent binfo within the return type of the
	   overriding function. We will want the vbase offset from
	   there.  */
	virtual_offset = binfo_for_vbase (BINFO_TYPE (virtual_offset),
					  over_return);
      else if (!same_type_ignoring_top_level_qualifiers_p
	       (over_return, base_return))
	{
	  /* There was no existing virtual thunk (which takes
	     precedence).  So find the binfo of the base function's
	     return type within the overriding function's return type.
	     We cannot call lookup base here, because we're inside a
	     dfs_walk, and will therefore clobber the BINFO_MARKED
	     flags.  Fortunately we know the covariancy is valid (it
	     has already been checked), so we can just iterate along
	     the binfos, which have been chained in inheritance graph
	     order.  Of course it is lame that we have to repeat the
	     search here anyway -- we should really be caching pieces
	     of the vtable and avoiding this repeated work.  */
	  tree thunk_binfo, base_binfo;

	  /* Find the base binfo within the overriding function's
	     return type.  We will always find a thunk_binfo, except
	     when the covariancy is invalid (which we will have
	     already diagnosed).  */
	  for (base_binfo = TYPE_BINFO (base_return),
	       thunk_binfo = TYPE_BINFO (over_return);
	       thunk_binfo;
	       thunk_binfo = TREE_CHAIN (thunk_binfo))
	    if (SAME_BINFO_TYPE_P (BINFO_TYPE (thunk_binfo),
				   BINFO_TYPE (base_binfo)))
	      break;

	  /* See if virtual inheritance is involved.  */
	  for (virtual_offset = thunk_binfo;
	       virtual_offset;
	       virtual_offset = BINFO_INHERITANCE_CHAIN (virtual_offset))
	    if (BINFO_VIRTUAL_P (virtual_offset))
	      break;

	  if (virtual_offset
	      || (thunk_binfo && !BINFO_OFFSET_ZEROP (thunk_binfo)))
	    {
	      tree offset = convert (ssizetype, BINFO_OFFSET (thunk_binfo));

	      if (virtual_offset)
		{
		  /* We convert via virtual base.  Adjust the fixed
		     offset to be from there.  */
		  offset = size_diffop
		    (offset, convert
		     (ssizetype, BINFO_OFFSET (virtual_offset)));
		}
	      if (fixed_offset)
		/* There was an existing fixed offset, this must be
		   from the base just converted to, and the base the
		   FN was thunking to.  */
		fixed_offset = size_binop (PLUS_EXPR, fixed_offset, offset);
	      else
		fixed_offset = offset;
	    }
	}

      if (fixed_offset || virtual_offset)
	/* Replace the overriding function with a covariant thunk.  We
	   will emit the overriding function in its own slot as
	   well.  */
	overrider_fn = make_thunk (overrider_target, /*this_adjusting=*/0,
				   fixed_offset, virtual_offset);
    }
  else
    gcc_assert (!DECL_THUNK_P (fn));

  /* Assume that we will produce a thunk that convert all the way to
     the final overrider, and not to an intermediate virtual base.  */
  virtual_base = NULL_TREE;

  /* See if we can convert to an intermediate virtual base first, and then
     use the vcall offset located there to finish the conversion.  */
  for (; b; b = BINFO_INHERITANCE_CHAIN (b))
    {
      /* If we find the final overrider, then we can stop
	 walking.  */
      if (SAME_BINFO_TYPE_P (BINFO_TYPE (b),
			     BINFO_TYPE (TREE_VALUE (overrider))))
	break;

      /* If we find a virtual base, and we haven't yet found the
	 overrider, then there is a virtual base between the
	 declaring base (first_defn) and the final overrider.  */
      if (BINFO_VIRTUAL_P (b))
	{
	  virtual_base = b;
	  break;
	}
    }

  if (overrider_fn != overrider_target && !virtual_base)
    {
      /* The ABI specifies that a covariant thunk includes a mangling
	 for a this pointer adjustment.  This-adjusting thunks that
	 override a function from a virtual base have a vcall
	 adjustment.  When the virtual base in question is a primary
	 virtual base, we know the adjustments are zero, (and in the
	 non-covariant case, we would not use the thunk).
	 Unfortunately we didn't notice this could happen, when
	 designing the ABI and so never mandated that such a covariant
	 thunk should be emitted.  Because we must use the ABI mandated
	 name, we must continue searching from the binfo where we
	 found the most recent definition of the function, towards the
	 primary binfo which first introduced the function into the
	 vtable.  If that enters a virtual base, we must use a vcall
	 this-adjusting thunk.  Bleah! */
      tree probe = first_defn;

      while ((probe = get_primary_binfo (probe))
	     && (unsigned) list_length (BINFO_VIRTUALS (probe)) > ix)
	if (BINFO_VIRTUAL_P (probe))
	  virtual_base = probe;

      if (virtual_base)
	/* Even if we find a virtual base, the correct delta is
	   between the overrider and the binfo we're building a vtable
	   for.  */
	goto virtual_covariant;
    }

  /* Compute the constant adjustment to the `this' pointer.  The
     `this' pointer, when this function is called, will point at BINFO
     (or one of its primary bases, which are at the same offset).  */
  if (virtual_base)
    /* The `this' pointer needs to be adjusted from the declaration to
       the nearest virtual base.  */
    delta = size_diffop (convert (ssizetype, BINFO_OFFSET (virtual_base)),
			 convert (ssizetype, BINFO_OFFSET (first_defn)));
  else if (lost)
    /* If the nearest definition is in a lost primary, we don't need an
       entry in our vtable.  Except possibly in a constructor vtable,
       if we happen to get our primary back.  In that case, the offset
       will be zero, as it will be a primary base.  */
    delta = size_zero_node;
  else
    /* The `this' pointer needs to be adjusted from pointing to
       BINFO to pointing at the base where the final overrider
       appears.  */
    virtual_covariant:
    delta = size_diffop (convert (ssizetype,
				  BINFO_OFFSET (TREE_VALUE (overrider))),
			 convert (ssizetype, BINFO_OFFSET (binfo)));

  modify_vtable_entry (t, binfo, overrider_fn, delta, virtuals);

  if (virtual_base)
    BV_VCALL_INDEX (*virtuals)
      = get_vcall_index (overrider_target, BINFO_TYPE (virtual_base));
  else
    BV_VCALL_INDEX (*virtuals) = NULL_TREE;
}

/* Called from modify_all_vtables via dfs_walk.  */

static tree
dfs_modify_vtables (tree binfo, void* data)
{
  tree t = (tree) data;
  tree virtuals;
  tree old_virtuals;
  unsigned ix;

  if (!TYPE_CONTAINS_VPTR_P (BINFO_TYPE (binfo)))
    /* A base without a vtable needs no modification, and its bases
       are uninteresting.  */
    return dfs_skip_bases;

  if (SAME_BINFO_TYPE_P (BINFO_TYPE (binfo), t)
      && !CLASSTYPE_HAS_PRIMARY_BASE_P (t))
    /* Don't do the primary vtable, if it's new.  */
    return NULL_TREE;

  if (BINFO_PRIMARY_P (binfo) && !BINFO_VIRTUAL_P (binfo))
    /* There's no need to modify the vtable for a non-virtual primary
       base; we're not going to use that vtable anyhow.  We do still
       need to do this for virtual primary bases, as they could become
       non-primary in a construction vtable.  */
    return NULL_TREE;

  make_new_vtable (t, binfo);

  /* Now, go through each of the virtual functions in the virtual
     function table for BINFO.  Find the final overrider, and update
     the BINFO_VIRTUALS list appropriately.  */
  for (ix = 0, virtuals = BINFO_VIRTUALS (binfo),
	 old_virtuals = BINFO_VIRTUALS (TYPE_BINFO (BINFO_TYPE (binfo)));
       virtuals;
       ix++, virtuals = TREE_CHAIN (virtuals),
	 old_virtuals = TREE_CHAIN (old_virtuals))
    update_vtable_entry_for_fn (t,
				binfo,
				BV_FN (old_virtuals),
				&virtuals, ix);

  return NULL_TREE;
}

/* Update all of the primary and secondary vtables for T.  Create new
   vtables as required, and initialize their RTTI information.  Each
   of the functions in VIRTUALS is declared in T and may override a
   virtual function from a base class; find and modify the appropriate
   entries to point to the overriding functions.  Returns a list, in
   declaration order, of the virtual functions that are declared in T,
   but do not appear in the primary base class vtable, and which
   should therefore be appended to the end of the vtable for T.  */

static tree
modify_all_vtables (tree t, tree virtuals)
{
  tree binfo = TYPE_BINFO (t);
  tree *fnsp;

  /* Update all of the vtables.  */
  dfs_walk_once (binfo, dfs_modify_vtables, NULL, t);

  /* Add virtual functions not already in our primary vtable. These
     will be both those introduced by this class, and those overridden
     from secondary bases.  It does not include virtuals merely
     inherited from secondary bases.  */
  for (fnsp = &virtuals; *fnsp; )
    {
      tree fn = TREE_VALUE (*fnsp);

      if (!value_member (fn, BINFO_VIRTUALS (binfo))
	  || DECL_VINDEX (fn) == error_mark_node)
	{
	  /* We don't need to adjust the `this' pointer when
	     calling this function.  */
	  BV_DELTA (*fnsp) = integer_zero_node;
	  BV_VCALL_INDEX (*fnsp) = NULL_TREE;

	  /* This is a function not already in our vtable.  Keep it.  */
	  fnsp = &TREE_CHAIN (*fnsp);
	}
      else
	/* We've already got an entry for this function.  Skip it.  */
	*fnsp = TREE_CHAIN (*fnsp);
    }

  return virtuals;
}

/* Get the base virtual function declarations in T that have the
   indicated NAME.  */

static tree
get_basefndecls (tree name, tree t)
{
  tree methods;
  tree base_fndecls = NULL_TREE;
  int n_baseclasses = BINFO_N_BASE_BINFOS (TYPE_BINFO (t));
  int i;

  /* Find virtual functions in T with the indicated NAME.  */
  i = lookup_fnfields_1 (t, name);
  if (i != -1)
    for (methods = VEC_index (tree, CLASSTYPE_METHOD_VEC (t), i);
	 methods;
	 methods = OVL_NEXT (methods))
      {
	tree method = OVL_CURRENT (methods);

	if (TREE_CODE (method) == FUNCTION_DECL
	    && DECL_VINDEX (method))
	  base_fndecls = tree_cons (NULL_TREE, method, base_fndecls);
      }

  if (base_fndecls)
    return base_fndecls;

  for (i = 0; i < n_baseclasses; i++)
    {
      tree basetype = BINFO_TYPE (BINFO_BASE_BINFO (TYPE_BINFO (t), i));
      base_fndecls = chainon (get_basefndecls (name, basetype),
			      base_fndecls);
    }

  return base_fndecls;
}

/* If this declaration supersedes the declaration of
   a method declared virtual in the base class, then
   mark this field as being virtual as well.  */

void
check_for_override (tree decl, tree ctype)
{
  if (TREE_CODE (decl) == TEMPLATE_DECL)
    /* In [temp.mem] we have:

	 A specialization of a member function template does not
	 override a virtual function from a base class.  */
    return;
  if ((DECL_DESTRUCTOR_P (decl)
       || IDENTIFIER_VIRTUAL_P (DECL_NAME (decl))
       || DECL_CONV_FN_P (decl))
      && look_for_overrides (ctype, decl)
      && !DECL_STATIC_FUNCTION_P (decl))
    /* Set DECL_VINDEX to a value that is neither an INTEGER_CST nor
       the error_mark_node so that we know it is an overriding
       function.  */
    DECL_VINDEX (decl) = decl;

  if (DECL_VIRTUAL_P (decl))
    {
      if (!DECL_VINDEX (decl))
	DECL_VINDEX (decl) = error_mark_node;
      IDENTIFIER_VIRTUAL_P (DECL_NAME (decl)) = 1;
      if (DECL_DLLIMPORT_P (decl))
	{
	  /* When we handled the dllimport attribute we may not have known
	     that this function is virtual   We can't use dllimport
	     semantics for a virtual method because we need to initialize
	     the vtable entry with a constant address.  */
	  DECL_DLLIMPORT_P (decl) = 0;
	  DECL_ATTRIBUTES (decl)
	    = remove_attribute ("dllimport", DECL_ATTRIBUTES (decl));
	}
    }
}

/* Warn about hidden virtual functions that are not overridden in t.
   We know that constructors and destructors don't apply.  */

static void
warn_hidden (tree t)
{
  VEC(tree,gc) *method_vec = CLASSTYPE_METHOD_VEC (t);
  tree fns;
  size_t i;

  /* We go through each separately named virtual function.  */
  for (i = CLASSTYPE_FIRST_CONVERSION_SLOT;
       VEC_iterate (tree, method_vec, i, fns);
       ++i)
    {
      tree fn;
      tree name;
      tree fndecl;
      tree base_fndecls;
      tree base_binfo;
      tree binfo;
      int j;

      /* All functions in this slot in the CLASSTYPE_METHOD_VEC will
	 have the same name.  Figure out what name that is.  */
      name = DECL_NAME (OVL_CURRENT (fns));
      /* There are no possibly hidden functions yet.  */
      base_fndecls = NULL_TREE;
      /* Iterate through all of the base classes looking for possibly
	 hidden functions.  */
      for (binfo = TYPE_BINFO (t), j = 0;
	   BINFO_BASE_ITERATE (binfo, j, base_binfo); j++)
	{
	  tree basetype = BINFO_TYPE (base_binfo);
	  base_fndecls = chainon (get_basefndecls (name, basetype),
				  base_fndecls);
	}

      /* If there are no functions to hide, continue.  */
      if (!base_fndecls)
	continue;

      /* Remove any overridden functions.  */
      for (fn = fns; fn; fn = OVL_NEXT (fn))
	{
	  fndecl = OVL_CURRENT (fn);
	  if (DECL_VINDEX (fndecl))
	    {
	      tree *prev = &base_fndecls;

	      while (*prev)
		/* If the method from the base class has the same
		   signature as the method from the derived class, it
		   has been overridden.  */
		if (same_signature_p (fndecl, TREE_VALUE (*prev)))
		  *prev = TREE_CHAIN (*prev);
		else
		  prev = &TREE_CHAIN (*prev);
	    }
	}

      /* Now give a warning for all base functions without overriders,
	 as they are hidden.  */
      while (base_fndecls)
	{
	  /* Here we know it is a hider, and no overrider exists.  */
	  warning (0, "%q+D was hidden", TREE_VALUE (base_fndecls));
	  warning (0, "  by %q+D", fns);
	  base_fndecls = TREE_CHAIN (base_fndecls);
	}
    }
}

/* Check for things that are invalid.  There are probably plenty of other
   things we should check for also.  */

static void
finish_struct_anon (tree t)
{
  tree field;

  for (field = TYPE_FIELDS (t); field; field = TREE_CHAIN (field))
    {
      if (TREE_STATIC (field))
	continue;
      if (TREE_CODE (field) != FIELD_DECL)
	continue;

      if (DECL_NAME (field) == NULL_TREE
	  && ANON_AGGR_TYPE_P (TREE_TYPE (field)))
	{
	  tree elt = TYPE_FIELDS (TREE_TYPE (field));
	  for (; elt; elt = TREE_CHAIN (elt))
	    {
	      /* We're generally only interested in entities the user
		 declared, but we also find nested classes by noticing
		 the TYPE_DECL that we create implicitly.  You're
		 allowed to put one anonymous union inside another,
		 though, so we explicitly tolerate that.  We use
		 TYPE_ANONYMOUS_P rather than ANON_AGGR_TYPE_P so that
		 we also allow unnamed types used for defining fields.  */
	      if (DECL_ARTIFICIAL (elt)
		  && (!DECL_IMPLICIT_TYPEDEF_P (elt)
		      || TYPE_ANONYMOUS_P (TREE_TYPE (elt))))
		continue;

	      if (TREE_CODE (elt) != FIELD_DECL)
		{
		  pedwarn ("%q+#D invalid; an anonymous union can "
			   "only have non-static data members", elt);
		  continue;
		}

	      if (TREE_PRIVATE (elt))
		pedwarn ("private member %q+#D in anonymous union", elt);
	      else if (TREE_PROTECTED (elt))
		pedwarn ("protected member %q+#D in anonymous union", elt);

	      TREE_PRIVATE (elt) = TREE_PRIVATE (field);
	      TREE_PROTECTED (elt) = TREE_PROTECTED (field);
	    }
	}
    }
}

/* Add T to CLASSTYPE_DECL_LIST of current_class_type which
   will be used later during class template instantiation.
   When FRIEND_P is zero, T can be a static member data (VAR_DECL),
   a non-static member data (FIELD_DECL), a member function
   (FUNCTION_DECL), a nested type (RECORD_TYPE, ENUM_TYPE),
   a typedef (TYPE_DECL) or a member class template (TEMPLATE_DECL)
   When FRIEND_P is nonzero, T is either a friend class
   (RECORD_TYPE, TEMPLATE_DECL) or a friend function
   (FUNCTION_DECL, TEMPLATE_DECL).  */

void
maybe_add_class_template_decl_list (tree type, tree t, int friend_p)
{
  /* Save some memory by not creating TREE_LIST if TYPE is not template.  */
  if (CLASSTYPE_TEMPLATE_INFO (type))
    CLASSTYPE_DECL_LIST (type)
      = tree_cons (friend_p ? NULL_TREE : type,
		   t, CLASSTYPE_DECL_LIST (type));
}

/* Create default constructors, assignment operators, and so forth for
   the type indicated by T, if they are needed.  CANT_HAVE_CONST_CTOR,
   and CANT_HAVE_CONST_ASSIGNMENT are nonzero if, for whatever reason,
   the class cannot have a default constructor, copy constructor
   taking a const reference argument, or an assignment operator taking
   a const reference, respectively.  */

static void
add_implicitly_declared_members (tree t,
				 int cant_have_const_cctor,
				 int cant_have_const_assignment)
{
  /* Destructor.  */
  if (!CLASSTYPE_DESTRUCTORS (t))
    {
      /* In general, we create destructors lazily.  */
      CLASSTYPE_LAZY_DESTRUCTOR (t) = 1;
      /* However, if the implicit destructor is non-trivial
	 destructor, we sometimes have to create it at this point.  */
      if (TYPE_HAS_NONTRIVIAL_DESTRUCTOR (t))
	{
	  bool lazy_p = true;

	  /* APPLE LOCAL begin omit calls to empty destructors 5559195 */
	  /* Since this is an empty destructor, it can only be nontrivial
	     because one of its base classes has a destructor that must be
	     called. */
	  CLASSTYPE_DESTRUCTOR_NONTRIVIAL_BECAUSE_OF_BASE (t) = 1;
	  /* APPLE LOCAL end omit calls to empty destructors 5559195 */

	  if (TYPE_FOR_JAVA (t))
	    /* If this a Java class, any non-trivial destructor is
	       invalid, even if compiler-generated.  Therefore, if the
	       destructor is non-trivial we create it now.  */
	    lazy_p = false;
	  else
	    {
	      tree binfo;
	      tree base_binfo;
	      int ix;

	      /* If the implicit destructor will be virtual, then we must
		 generate it now because (unfortunately) we do not
		 generate virtual tables lazily.  */
	      binfo = TYPE_BINFO (t);
	      for (ix = 0; BINFO_BASE_ITERATE (binfo, ix, base_binfo); ix++)
		{
		  tree base_type;
		  tree dtor;

		  base_type = BINFO_TYPE (base_binfo);
		  dtor = CLASSTYPE_DESTRUCTORS (base_type);
		  if (dtor && DECL_VIRTUAL_P (dtor))
		    {
		      lazy_p = false;
		      break;
		    }
		}
	    }

	  /* If we can't get away with being lazy, generate the destructor
	     now.  */
	  if (!lazy_p)
	    lazily_declare_fn (sfk_destructor, t);
	}
    }

  /* Default constructor.  */
  if (! TYPE_HAS_CONSTRUCTOR (t))
    {
      TYPE_HAS_DEFAULT_CONSTRUCTOR (t) = 1;
      CLASSTYPE_LAZY_DEFAULT_CTOR (t) = 1;
    }

  /* Copy constructor.  */
  if (! TYPE_HAS_INIT_REF (t) && ! TYPE_FOR_JAVA (t))
    {
      TYPE_HAS_INIT_REF (t) = 1;
      TYPE_HAS_CONST_INIT_REF (t) = !cant_have_const_cctor;
      CLASSTYPE_LAZY_COPY_CTOR (t) = 1;
      TYPE_HAS_CONSTRUCTOR (t) = 1;
    }

  /* If there is no assignment operator, one will be created if and
     when it is needed.  For now, just record whether or not the type
     of the parameter to the assignment operator will be a const or
     non-const reference.  */
  if (!TYPE_HAS_ASSIGN_REF (t) && !TYPE_FOR_JAVA (t))
    {
      TYPE_HAS_ASSIGN_REF (t) = 1;
      TYPE_HAS_CONST_ASSIGN_REF (t) = !cant_have_const_assignment;
      CLASSTYPE_LAZY_ASSIGNMENT_OP (t) = 1;
    }
}

/* Subroutine of finish_struct_1.  Recursively count the number of fields
   in TYPE, including anonymous union members.  */

static int
count_fields (tree fields)
{
  tree x;
  int n_fields = 0;
  for (x = fields; x; x = TREE_CHAIN (x))
    {
      if (TREE_CODE (x) == FIELD_DECL && ANON_AGGR_TYPE_P (TREE_TYPE (x)))
	n_fields += count_fields (TYPE_FIELDS (TREE_TYPE (x)));
      else
	n_fields += 1;
    }
  return n_fields;
}

/* Subroutine of finish_struct_1.  Recursively add all the fields in the
   TREE_LIST FIELDS to the SORTED_FIELDS_TYPE elts, starting at offset IDX.  */

static int
add_fields_to_record_type (tree fields, struct sorted_fields_type *field_vec, int idx)
{
  tree x;
  for (x = fields; x; x = TREE_CHAIN (x))
    {
      if (TREE_CODE (x) == FIELD_DECL && ANON_AGGR_TYPE_P (TREE_TYPE (x)))
	idx = add_fields_to_record_type (TYPE_FIELDS (TREE_TYPE (x)), field_vec, idx);
      else
	field_vec->elts[idx++] = x;
    }
  return idx;
}

/* FIELD is a bit-field.  We are finishing the processing for its
   enclosing type.  Issue any appropriate messages and set appropriate
   flags.  */

static void
check_bitfield_decl (tree field)
{
  tree type = TREE_TYPE (field);
  tree w;

  /* Extract the declared width of the bitfield, which has been
     temporarily stashed in DECL_INITIAL.  */
  w = DECL_INITIAL (field);
  gcc_assert (w != NULL_TREE);
  /* Remove the bit-field width indicator so that the rest of the
     compiler does not treat that value as an initializer.  */
  DECL_INITIAL (field) = NULL_TREE;

  /* Detect invalid bit-field type.  */
  if (!INTEGRAL_TYPE_P (type))
    {
      error ("bit-field %q+#D with non-integral type", field);
      TREE_TYPE (field) = error_mark_node;
      w = error_mark_node;
    }
  else
    {
      /* Avoid the non_lvalue wrapper added by fold for PLUS_EXPRs.  */
      STRIP_NOPS (w);

      /* detect invalid field size.  */
      w = integral_constant_value (w);

      if (TREE_CODE (w) != INTEGER_CST)
	{
	  error ("bit-field %q+D width not an integer constant", field);
	  w = error_mark_node;
	}
      else if (tree_int_cst_sgn (w) < 0)
	{
	  error ("negative width in bit-field %q+D", field);
	  w = error_mark_node;
	}
      else if (integer_zerop (w) && DECL_NAME (field) != 0)
	{
	  error ("zero width for bit-field %q+D", field);
	  w = error_mark_node;
	}
      else if (compare_tree_int (w, TYPE_PRECISION (type)) > 0
	       && TREE_CODE (type) != ENUMERAL_TYPE
	       && TREE_CODE (type) != BOOLEAN_TYPE)
	warning (0, "width of %q+D exceeds its type", field);
      else if (TREE_CODE (type) == ENUMERAL_TYPE
	       && (0 > compare_tree_int (w,
					 min_precision (TYPE_MIN_VALUE (type),
							TYPE_UNSIGNED (type)))
		   ||  0 > compare_tree_int (w,
					     min_precision
					     (TYPE_MAX_VALUE (type),
					      TYPE_UNSIGNED (type)))))
	warning (0, "%q+D is too small to hold all values of %q#T", field, type);
    }

  if (w != error_mark_node)
    {
      DECL_SIZE (field) = convert (bitsizetype, w);
      DECL_BIT_FIELD (field) = 1;
    }
  else
    {
      /* Non-bit-fields are aligned for their type.  */
      DECL_BIT_FIELD (field) = 0;
      CLEAR_DECL_C_BIT_FIELD (field);
    }
}

/* FIELD is a non bit-field.  We are finishing the processing for its
   enclosing type T.  Issue any appropriate messages and set appropriate
   flags.  */

static void
check_field_decl (tree field,
		  tree t,
		  int* cant_have_const_ctor,
		  int* no_const_asn_ref,
		  int* any_default_members)
{
  tree type = strip_array_types (TREE_TYPE (field));

  /* An anonymous union cannot contain any fields which would change
     the settings of CANT_HAVE_CONST_CTOR and friends.  */
  if (ANON_UNION_TYPE_P (type))
    ;
  /* And, we don't set TYPE_HAS_CONST_INIT_REF, etc., for anonymous
     structs.  So, we recurse through their fields here.  */
  else if (ANON_AGGR_TYPE_P (type))
    {
      tree fields;

      for (fields = TYPE_FIELDS (type); fields; fields = TREE_CHAIN (fields))
	if (TREE_CODE (fields) == FIELD_DECL && !DECL_C_BIT_FIELD (field))
	  check_field_decl (fields, t, cant_have_const_ctor,
			    no_const_asn_ref, any_default_members);
    }
  /* Check members with class type for constructors, destructors,
     etc.  */
  else if (CLASS_TYPE_P (type))
    {
      /* Never let anything with uninheritable virtuals
	 make it through without complaint.  */
      abstract_virtuals_error (field, type);

      if (TREE_CODE (t) == UNION_TYPE)
	{
	  if (TYPE_NEEDS_CONSTRUCTING (type))
	    error ("member %q+#D with constructor not allowed in union",
		   field);
	  if (TYPE_HAS_NONTRIVIAL_DESTRUCTOR (type))
	    error ("member %q+#D with destructor not allowed in union", field);
	  if (TYPE_HAS_COMPLEX_ASSIGN_REF (type))
	    error ("member %q+#D with copy assignment operator not allowed in union",
		   field);
	}
      else
	{
	  TYPE_NEEDS_CONSTRUCTING (t) |= TYPE_NEEDS_CONSTRUCTING (type);
	  TYPE_HAS_NONTRIVIAL_DESTRUCTOR (t)
	    |= TYPE_HAS_NONTRIVIAL_DESTRUCTOR (type);
	  TYPE_HAS_COMPLEX_ASSIGN_REF (t) |= TYPE_HAS_COMPLEX_ASSIGN_REF (type);
	  TYPE_HAS_COMPLEX_INIT_REF (t) |= TYPE_HAS_COMPLEX_INIT_REF (type);
	}

      if (!TYPE_HAS_CONST_INIT_REF (type))
	*cant_have_const_ctor = 1;

      if (!TYPE_HAS_CONST_ASSIGN_REF (type))
	*no_const_asn_ref = 1;
    }
  if (DECL_INITIAL (field) != NULL_TREE)
    {
      /* `build_class_init_list' does not recognize
	 non-FIELD_DECLs.  */
      if (TREE_CODE (t) == UNION_TYPE && any_default_members != 0)
	error ("multiple fields in union %qT initialized", t);
      *any_default_members = 1;
    }
}

/* Check the data members (both static and non-static), class-scoped
   typedefs, etc., appearing in the declaration of T.  Issue
   appropriate diagnostics.  Sets ACCESS_DECLS to a list (in
   declaration order) of access declarations; each TREE_VALUE in this
   list is a USING_DECL.

   In addition, set the following flags:

     EMPTY_P
       The class is empty, i.e., contains no non-static data members.

     CANT_HAVE_CONST_CTOR_P
       This class cannot have an implicitly generated copy constructor
       taking a const reference.

     CANT_HAVE_CONST_ASN_REF
       This class cannot have an implicitly generated assignment
       operator taking a const reference.

   All of these flags should be initialized before calling this
   function.

   Returns a pointer to the end of the TYPE_FIELDs chain; additional
   fields can be added by adding to this chain.  */

static void
check_field_decls (tree t, tree *access_decls,
		   int *cant_have_const_ctor_p,
		   int *no_const_asn_ref_p)
{
  tree *field;
  tree *next;
  bool has_pointers;
  int any_default_members;
  int cant_pack = 0;

  /* Assume there are no access declarations.  */
  *access_decls = NULL_TREE;
  /* Assume this class has no pointer members.  */
  has_pointers = false;
  /* Assume none of the members of this class have default
     initializations.  */
  any_default_members = 0;

  for (field = &TYPE_FIELDS (t); *field; field = next)
    {
      tree x = *field;
      tree type = TREE_TYPE (x);

      next = &TREE_CHAIN (x);

      if (TREE_CODE (x) == USING_DECL)
	{
	  /* Prune the access declaration from the list of fields.  */
	  *field = TREE_CHAIN (x);

	  /* Save the access declarations for our caller.  */
	  *access_decls = tree_cons (NULL_TREE, x, *access_decls);

	  /* Since we've reset *FIELD there's no reason to skip to the
	     next field.  */
	  next = field;
	  continue;
	}

      if (TREE_CODE (x) == TYPE_DECL
	  || TREE_CODE (x) == TEMPLATE_DECL)
	continue;

      /* If we've gotten this far, it's a data member, possibly static,
	 or an enumerator.  */
      DECL_CONTEXT (x) = t;

      /* When this goes into scope, it will be a non-local reference.  */
      DECL_NONLOCAL (x) = 1;

      if (TREE_CODE (t) == UNION_TYPE)
	{
	  /* [class.union]

	     If a union contains a static data member, or a member of
	     reference type, the program is ill-formed.  */
	  if (TREE_CODE (x) == VAR_DECL)
	    {
	      error ("%q+D may not be static because it is a member of a union", x);
	      continue;
	    }
	  if (TREE_CODE (type) == REFERENCE_TYPE)
	    {
	      error ("%q+D may not have reference type %qT because"
		     " it is a member of a union",
		     x, type);
	      continue;
	    }
	}

      /* Perform error checking that did not get done in
	 grokdeclarator.  */
      if (TREE_CODE (type) == FUNCTION_TYPE)
	{
	  error ("field %q+D invalidly declared function type", x);
	  type = build_pointer_type (type);
	  TREE_TYPE (x) = type;
	}
      else if (TREE_CODE (type) == METHOD_TYPE)
	{
	  error ("field %q+D invalidly declared method type", x);
	  type = build_pointer_type (type);
	  TREE_TYPE (x) = type;
	}

      if (type == error_mark_node)
	continue;

      if (TREE_CODE (x) == CONST_DECL || TREE_CODE (x) == VAR_DECL)
	continue;

      /* Now it can only be a FIELD_DECL.  */

      if (TREE_PRIVATE (x) || TREE_PROTECTED (x))
	CLASSTYPE_NON_AGGREGATE (t) = 1;

      /* If this is of reference type, check if it needs an init.
	 Also do a little ANSI jig if necessary.  */
      if (TREE_CODE (type) == REFERENCE_TYPE)
	{
	  CLASSTYPE_NON_POD_P (t) = 1;
	  if (DECL_INITIAL (x) == NULL_TREE)
	    SET_CLASSTYPE_REF_FIELDS_NEED_INIT (t, 1);

	  /* ARM $12.6.2: [A member initializer list] (or, for an
	     aggregate, initialization by a brace-enclosed list) is the
	     only way to initialize nonstatic const and reference
	     members.  */
	  TYPE_HAS_COMPLEX_ASSIGN_REF (t) = 1;

	  if (! TYPE_HAS_CONSTRUCTOR (t) && CLASSTYPE_NON_AGGREGATE (t)
	      && extra_warnings)
	    warning (OPT_Wextra, "non-static reference %q+#D in class without a constructor", x);
	}

      type = strip_array_types (type);

      if (TYPE_PACKED (t))
	{
	  if (!pod_type_p (type) && !TYPE_PACKED (type))
	    {
	      warning
		(0,
		 "ignoring packed attribute because of unpacked non-POD field %q+#D",
		 x);
	      cant_pack = 1;
	    }
	  else if (TYPE_ALIGN (TREE_TYPE (x)) > BITS_PER_UNIT)
	    DECL_PACKED (x) = 1;
	}

      if (DECL_C_BIT_FIELD (x) && integer_zerop (DECL_INITIAL (x)))
	/* We don't treat zero-width bitfields as making a class
	   non-empty.  */
	;
      else
	{
	  /* The class is non-empty.  */
	  CLASSTYPE_EMPTY_P (t) = 0;
	  /* The class is not even nearly empty.  */
	  CLASSTYPE_NEARLY_EMPTY_P (t) = 0;
	  /* If one of the data members contains an empty class,
	     so does T.  */
	  if (CLASS_TYPE_P (type)
	      && CLASSTYPE_CONTAINS_EMPTY_CLASS_P (type))
	    CLASSTYPE_CONTAINS_EMPTY_CLASS_P (t) = 1;
	}

      /* This is used by -Weffc++ (see below). Warn only for pointers
	 to members which might hold dynamic memory. So do not warn
	 for pointers to functions or pointers to members.  */
      if (TYPE_PTR_P (type)
	  && !TYPE_PTRFN_P (type)
	  && !TYPE_PTR_TO_MEMBER_P (type))
	has_pointers = true;

      if (CLASS_TYPE_P (type))
	{
	  if (CLASSTYPE_REF_FIELDS_NEED_INIT (type))
	    SET_CLASSTYPE_REF_FIELDS_NEED_INIT (t, 1);
	  if (CLASSTYPE_READONLY_FIELDS_NEED_INIT (type))
	    SET_CLASSTYPE_READONLY_FIELDS_NEED_INIT (t, 1);
	}

      if (DECL_MUTABLE_P (x) || TYPE_HAS_MUTABLE_P (type))
	CLASSTYPE_HAS_MUTABLE (t) = 1;

      if (! pod_type_p (type))
	/* DR 148 now allows pointers to members (which are POD themselves),
	   to be allowed in POD structs.  */
	CLASSTYPE_NON_POD_P (t) = 1;

      if (! zero_init_p (type))
	CLASSTYPE_NON_ZERO_INIT_P (t) = 1;

      /* If any field is const, the structure type is pseudo-const.  */
      if (CP_TYPE_CONST_P (type))
	{
	  C_TYPE_FIELDS_READONLY (t) = 1;
	  if (DECL_INITIAL (x) == NULL_TREE)
	    SET_CLASSTYPE_READONLY_FIELDS_NEED_INIT (t, 1);

	  /* ARM $12.6.2: [A member initializer list] (or, for an
	     aggregate, initialization by a brace-enclosed list) is the
	     only way to initialize nonstatic const and reference
	     members.  */
	  TYPE_HAS_COMPLEX_ASSIGN_REF (t) = 1;

	  if (! TYPE_HAS_CONSTRUCTOR (t) && CLASSTYPE_NON_AGGREGATE (t)
	      && extra_warnings)
	    warning (OPT_Wextra, "non-static const member %q+#D in class without a constructor", x);
	}
      /* A field that is pseudo-const makes the structure likewise.  */
      else if (CLASS_TYPE_P (type))
	{
	  C_TYPE_FIELDS_READONLY (t) |= C_TYPE_FIELDS_READONLY (type);
	  SET_CLASSTYPE_READONLY_FIELDS_NEED_INIT (t,
	    CLASSTYPE_READONLY_FIELDS_NEED_INIT (t)
	    | CLASSTYPE_READONLY_FIELDS_NEED_INIT (type));
	}

      /* Core issue 80: A nonstatic data member is required to have a
	 different name from the class iff the class has a
	 user-defined constructor.  */
      if (constructor_name_p (DECL_NAME (x), t) && TYPE_HAS_CONSTRUCTOR (t))
	pedwarn ("field %q+#D with same name as class", x);

      /* We set DECL_C_BIT_FIELD in grokbitfield.
	 If the type and width are valid, we'll also set DECL_BIT_FIELD.  */
      if (DECL_C_BIT_FIELD (x))
	check_bitfield_decl (x);
      else
	check_field_decl (x, t,
			  cant_have_const_ctor_p,
			  no_const_asn_ref_p,
			  &any_default_members);
    }

  /* Effective C++ rule 11: if a class has dynamic memory held by pointers,
     it should also define a copy constructor and an assignment operator to
     implement the correct copy semantic (deep vs shallow, etc.). As it is
     not feasible to check whether the constructors do allocate dynamic memory
     and store it within members, we approximate the warning like this:

     -- Warn only if there are members which are pointers
     -- Warn only if there is a non-trivial constructor (otherwise,
	there cannot be memory allocated).
     -- Warn only if there is a non-trivial destructor. We assume that the
	user at least implemented the cleanup correctly, and a destructor
	is needed to free dynamic memory.

     This seems enough for practical purposes.  */
  if (warn_ecpp
      && has_pointers
      && TYPE_HAS_CONSTRUCTOR (t)
      && TYPE_HAS_NONTRIVIAL_DESTRUCTOR (t)
      && !(TYPE_HAS_INIT_REF (t) && TYPE_HAS_ASSIGN_REF (t)))
    {
      warning (OPT_Weffc__, "%q#T has pointer data members", t);

      if (! TYPE_HAS_INIT_REF (t))
	{
	  warning (OPT_Weffc__,
		   "  but does not override %<%T(const %T&)%>", t, t);
	  if (!TYPE_HAS_ASSIGN_REF (t))
	    warning (OPT_Weffc__, "  or %<operator=(const %T&)%>", t);
	}
      else if (! TYPE_HAS_ASSIGN_REF (t))
	warning (OPT_Weffc__,
		 "  but does not override %<operator=(const %T&)%>", t);
    }

  /* If any of the fields couldn't be packed, unset TYPE_PACKED.  */
  if (cant_pack)
    TYPE_PACKED (t) = 0;

  /* Check anonymous struct/anonymous union fields.  */
  finish_struct_anon (t);

  /* We've built up the list of access declarations in reverse order.
     Fix that now.  */
  *access_decls = nreverse (*access_decls);
}

/* If TYPE is an empty class type, records its OFFSET in the table of
   OFFSETS.  */

static int
record_subobject_offset (tree type, tree offset, splay_tree offsets)
{
  splay_tree_node n;

  if (!is_empty_class (type))
    return 0;

  /* Record the location of this empty object in OFFSETS.  */
  n = splay_tree_lookup (offsets, (splay_tree_key) offset);
  if (!n)
    n = splay_tree_insert (offsets,
			   (splay_tree_key) offset,
			   (splay_tree_value) NULL_TREE);
  n->value = ((splay_tree_value)
	      tree_cons (NULL_TREE,
			 type,
			 (tree) n->value));

  return 0;
}

/* Returns nonzero if TYPE is an empty class type and there is
   already an entry in OFFSETS for the same TYPE as the same OFFSET.  */

static int
check_subobject_offset (tree type, tree offset, splay_tree offsets)
{
  splay_tree_node n;
  tree t;

  if (!is_empty_class (type))
    return 0;

  /* Record the location of this empty object in OFFSETS.  */
  n = splay_tree_lookup (offsets, (splay_tree_key) offset);
  if (!n)
    return 0;

  for (t = (tree) n->value; t; t = TREE_CHAIN (t))
    if (same_type_p (TREE_VALUE (t), type))
      return 1;

  return 0;
}

/* Walk through all the subobjects of TYPE (located at OFFSET).  Call
   F for every subobject, passing it the type, offset, and table of
   OFFSETS.  If VBASES_P is one, then virtual non-primary bases should
   be traversed.

   If MAX_OFFSET is non-NULL, then subobjects with an offset greater
   than MAX_OFFSET will not be walked.

   If F returns a nonzero value, the traversal ceases, and that value
   is returned.  Otherwise, returns zero.  */

static int
walk_subobject_offsets (tree type,
			subobject_offset_fn f,
			tree offset,
			splay_tree offsets,
			tree max_offset,
			int vbases_p)
{
  int r = 0;
  tree type_binfo = NULL_TREE;

  /* If this OFFSET is bigger than the MAX_OFFSET, then we should
     stop.  */
  if (max_offset && INT_CST_LT (max_offset, offset))
    return 0;

  if (type == error_mark_node)
    return 0;

  if (!TYPE_P (type))
    {
      if (abi_version_at_least (2))
	type_binfo = type;
      type = BINFO_TYPE (type);
    }

  if (CLASS_TYPE_P (type))
    {
      tree field;
      tree binfo;
      int i;

      /* Avoid recursing into objects that are not interesting.  */
      if (!CLASSTYPE_CONTAINS_EMPTY_CLASS_P (type))
	return 0;

      /* Record the location of TYPE.  */
      r = (*f) (type, offset, offsets);
      if (r)
	return r;

      /* Iterate through the direct base classes of TYPE.  */
      if (!type_binfo)
	type_binfo = TYPE_BINFO (type);
      for (i = 0; BINFO_BASE_ITERATE (type_binfo, i, binfo); i++)
	{
	  tree binfo_offset;

	  if (abi_version_at_least (2)
	      && BINFO_VIRTUAL_P (binfo))
	    continue;

	  if (!vbases_p
	      && BINFO_VIRTUAL_P (binfo)
	      && !BINFO_PRIMARY_P (binfo))
	    continue;

	  if (!abi_version_at_least (2))
	    binfo_offset = size_binop (PLUS_EXPR,
				       offset,
				       BINFO_OFFSET (binfo));
	  else
	    {
	      tree orig_binfo;
	      /* We cannot rely on BINFO_OFFSET being set for the base
		 class yet, but the offsets for direct non-virtual
		 bases can be calculated by going back to the TYPE.  */
	      orig_binfo = BINFO_BASE_BINFO (TYPE_BINFO (type), i);
	      binfo_offset = size_binop (PLUS_EXPR,
					 offset,
					 BINFO_OFFSET (orig_binfo));
	    }

	  r = walk_subobject_offsets (binfo,
				      f,
				      binfo_offset,
				      offsets,
				      max_offset,
				      (abi_version_at_least (2)
				       ? /*vbases_p=*/0 : vbases_p));
	  if (r)
	    return r;
	}

      if (abi_version_at_least (2) && CLASSTYPE_VBASECLASSES (type))
	{
	  unsigned ix;
	  VEC(tree,gc) *vbases;

	  /* Iterate through the virtual base classes of TYPE.  In G++
	     3.2, we included virtual bases in the direct base class
	     loop above, which results in incorrect results; the
	     correct offsets for virtual bases are only known when
	     working with the most derived type.  */
	  if (vbases_p)
	    for (vbases = CLASSTYPE_VBASECLASSES (type), ix = 0;
		 VEC_iterate (tree, vbases, ix, binfo); ix++)
	      {
		r = walk_subobject_offsets (binfo,
					    f,
					    size_binop (PLUS_EXPR,
							offset,
							BINFO_OFFSET (binfo)),
					    offsets,
					    max_offset,
					    /*vbases_p=*/0);
		if (r)
		  return r;
	      }
	  else
	    {
	      /* We still have to walk the primary base, if it is
		 virtual.  (If it is non-virtual, then it was walked
		 above.)  */
	      tree vbase = get_primary_binfo (type_binfo);

	      if (vbase && BINFO_VIRTUAL_P (vbase)
		  && BINFO_PRIMARY_P (vbase)
		  && BINFO_INHERITANCE_CHAIN (vbase) == type_binfo)
		{
		  r = (walk_subobject_offsets
		       (vbase, f, offset,
			offsets, max_offset, /*vbases_p=*/0));
		  if (r)
		    return r;
		}
	    }
	}

      /* Iterate through the fields of TYPE.  */
      for (field = TYPE_FIELDS (type); field; field = TREE_CHAIN (field))
	if (TREE_CODE (field) == FIELD_DECL && !DECL_ARTIFICIAL (field))
	  {
	    tree field_offset;

	    if (abi_version_at_least (2))
	      field_offset = byte_position (field);
	    else
	      /* In G++ 3.2, DECL_FIELD_OFFSET was used.  */
	      field_offset = DECL_FIELD_OFFSET (field);

	    r = walk_subobject_offsets (TREE_TYPE (field),
					f,
					size_binop (PLUS_EXPR,
						    offset,
						    field_offset),
					offsets,
					max_offset,
					/*vbases_p=*/1);
	    if (r)
	      return r;
	  }
    }
  else if (TREE_CODE (type) == ARRAY_TYPE)
    {
      tree element_type = strip_array_types (type);
      tree domain = TYPE_DOMAIN (type);
      tree index;

      /* Avoid recursing into objects that are not interesting.  */
      if (!CLASS_TYPE_P (element_type)
	  || !CLASSTYPE_CONTAINS_EMPTY_CLASS_P (element_type))
	return 0;

      /* Step through each of the elements in the array.  */
      for (index = size_zero_node;
	   /* G++ 3.2 had an off-by-one error here.  */
	   (abi_version_at_least (2)
	    ? !INT_CST_LT (TYPE_MAX_VALUE (domain), index)
	    : INT_CST_LT (index, TYPE_MAX_VALUE (domain)));
	   index = size_binop (PLUS_EXPR, index, size_one_node))
	{
	  r = walk_subobject_offsets (TREE_TYPE (type),
				      f,
				      offset,
				      offsets,
				      max_offset,
				      /*vbases_p=*/1);
	  if (r)
	    return r;
	  offset = size_binop (PLUS_EXPR, offset,
			       TYPE_SIZE_UNIT (TREE_TYPE (type)));
	  /* If this new OFFSET is bigger than the MAX_OFFSET, then
	     there's no point in iterating through the remaining
	     elements of the array.  */
	  if (max_offset && INT_CST_LT (max_offset, offset))
	    break;
	}
    }

  return 0;
}

/* Record all of the empty subobjects of TYPE (either a type or a
   binfo).  If IS_DATA_MEMBER is true, then a non-static data member
   is being placed at OFFSET; otherwise, it is a base class that is
   being placed at OFFSET.  */

static void
record_subobject_offsets (tree type,
			  tree offset,
			  splay_tree offsets,
			  bool is_data_member)
{
  tree max_offset;
  /* If recording subobjects for a non-static data member or a
     non-empty base class , we do not need to record offsets beyond
     the size of the biggest empty class.  Additional data members
     will go at the end of the class.  Additional base classes will go
     either at offset zero (if empty, in which case they cannot
     overlap with offsets past the size of the biggest empty class) or
     at the end of the class.

     However, if we are placing an empty base class, then we must record
     all offsets, as either the empty class is at offset zero (where
     other empty classes might later be placed) or at the end of the
     class (where other objects might then be placed, so other empty
     subobjects might later overlap).  */
  if (is_data_member
      || !is_empty_class (BINFO_TYPE (type)))
    max_offset = sizeof_biggest_empty_class;
  else
    max_offset = NULL_TREE;
  walk_subobject_offsets (type, record_subobject_offset, offset,
			  offsets, max_offset, is_data_member);
}

/* Returns nonzero if any of the empty subobjects of TYPE (located at
   OFFSET) conflict with entries in OFFSETS.  If VBASES_P is nonzero,
   virtual bases of TYPE are examined.  */

static int
layout_conflict_p (tree type,
		   tree offset,
		   splay_tree offsets,
		   int vbases_p)
{
  splay_tree_node max_node;

  /* Get the node in OFFSETS that indicates the maximum offset where
     an empty subobject is located.  */
  max_node = splay_tree_max (offsets);
  /* If there aren't any empty subobjects, then there's no point in
     performing this check.  */
  if (!max_node)
    return 0;

  return walk_subobject_offsets (type, check_subobject_offset, offset,
				 offsets, (tree) (max_node->key),
				 vbases_p);
}

/* DECL is a FIELD_DECL corresponding either to a base subobject of a
   non-static data member of the type indicated by RLI.  BINFO is the
   binfo corresponding to the base subobject, OFFSETS maps offsets to
   types already located at those offsets.  This function determines
   the position of the DECL.  */

static void
layout_nonempty_base_or_field (record_layout_info rli,
			       tree decl,
			       tree binfo,
			       splay_tree offsets)
{
  tree offset = NULL_TREE;
  bool field_p;
  tree type;

  if (binfo)
    {
      /* For the purposes of determining layout conflicts, we want to
	 use the class type of BINFO; TREE_TYPE (DECL) will be the
	 CLASSTYPE_AS_BASE version, which does not contain entries for
	 zero-sized bases.  */
      type = TREE_TYPE (binfo);
      field_p = false;
    }
  else
    {
      type = TREE_TYPE (decl);
      field_p = true;
    }

  /* Try to place the field.  It may take more than one try if we have
     a hard time placing the field without putting two objects of the
     same type at the same address.  */
  while (1)
    {
      struct record_layout_info_s old_rli = *rli;

      /* Place this field.  */
      place_field (rli, decl);
      offset = byte_position (decl);

      /* We have to check to see whether or not there is already
	 something of the same type at the offset we're about to use.
	 For example, consider:

	   struct S {};
	   struct T : public S { int i; };
	   struct U : public S, public T {};

	 Here, we put S at offset zero in U.  Then, we can't put T at
	 offset zero -- its S component would be at the same address
	 as the S we already allocated.  So, we have to skip ahead.
	 Since all data members, including those whose type is an
	 empty class, have nonzero size, any overlap can happen only
	 with a direct or indirect base-class -- it can't happen with
	 a data member.  */
      /* In a union, overlap is permitted; all members are placed at
	 offset zero.  */
      if (TREE_CODE (rli->t) == UNION_TYPE)
	break;
      /* G++ 3.2 did not check for overlaps when placing a non-empty
	 virtual base.  */
      if (!abi_version_at_least (2) && binfo && BINFO_VIRTUAL_P (binfo))
	break;
      if (layout_conflict_p (field_p ? type : binfo, offset,
			     offsets, field_p))
	{
	  /* Strip off the size allocated to this field.  That puts us
	     at the first place we could have put the field with
	     proper alignment.  */
	  *rli = old_rli;

	  /* Bump up by the alignment required for the type.  */
	  rli->bitpos
	    = size_binop (PLUS_EXPR, rli->bitpos,
			  bitsize_int (binfo
				       ? CLASSTYPE_ALIGN (type)
				       : TYPE_ALIGN (type)));
	  normalize_rli (rli);
	}
      else
	/* There was no conflict.  We're done laying out this field.  */
	break;
    }

  /* Now that we know where it will be placed, update its
     BINFO_OFFSET.  */
  if (binfo && CLASS_TYPE_P (BINFO_TYPE (binfo)))
    /* Indirect virtual bases may have a nonzero BINFO_OFFSET at
       this point because their BINFO_OFFSET is copied from another
       hierarchy.  Therefore, we may not need to add the entire
       OFFSET.  */
    propagate_binfo_offsets (binfo,
			     size_diffop (convert (ssizetype, offset),
					  convert (ssizetype,
						   BINFO_OFFSET (binfo))));
}

/* Returns true if TYPE is empty and OFFSET is nonzero.  */

static int
empty_base_at_nonzero_offset_p (tree type,
				tree offset,
				splay_tree offsets ATTRIBUTE_UNUSED)
{
  return is_empty_class (type) && !integer_zerop (offset);
}

/* Layout the empty base BINFO.  EOC indicates the byte currently just
   past the end of the class, and should be correctly aligned for a
   class of the type indicated by BINFO; OFFSETS gives the offsets of
   the empty bases allocated so far. T is the most derived
   type.  Return nonzero iff we added it at the end.  */

static bool
layout_empty_base (tree binfo, tree eoc, splay_tree offsets)
{
  tree alignment;
  tree basetype = BINFO_TYPE (binfo);
  bool atend = false;

  /* This routine should only be used for empty classes.  */
  gcc_assert (is_empty_class (basetype));
  alignment = ssize_int (CLASSTYPE_ALIGN_UNIT (basetype));

  if (!integer_zerop (BINFO_OFFSET (binfo)))
    {
      if (abi_version_at_least (2))
	propagate_binfo_offsets
	  (binfo, size_diffop (size_zero_node, BINFO_OFFSET (binfo)));
      else
	warning (OPT_Wabi,
		 "offset of empty base %qT may not be ABI-compliant and may"
		 "change in a future version of GCC",
		 BINFO_TYPE (binfo));
    }

  /* This is an empty base class.  We first try to put it at offset
     zero.  */
  if (layout_conflict_p (binfo,
			 BINFO_OFFSET (binfo),
			 offsets,
			 /*vbases_p=*/0))
    {
      /* That didn't work.  Now, we move forward from the next
	 available spot in the class.  */
      atend = true;
      propagate_binfo_offsets (binfo, convert (ssizetype, eoc));
      while (1)
	{
	  if (!layout_conflict_p (binfo,
				  BINFO_OFFSET (binfo),
				  offsets,
				  /*vbases_p=*/0))
	    /* We finally found a spot where there's no overlap.  */
	    break;

	  /* There's overlap here, too.  Bump along to the next spot.  */
	  propagate_binfo_offsets (binfo, alignment);
	}
    }
  return atend;
}

/* Layout the base given by BINFO in the class indicated by RLI.
   *BASE_ALIGN is a running maximum of the alignments of
   any base class.  OFFSETS gives the location of empty base
   subobjects.  T is the most derived type.  Return nonzero if the new
   object cannot be nearly-empty.  A new FIELD_DECL is inserted at
   *NEXT_FIELD, unless BINFO is for an empty base class.

   Returns the location at which the next field should be inserted.  */

static tree *
build_base_field (record_layout_info rli, tree binfo,
		  splay_tree offsets, tree *next_field)
{
  tree t = rli->t;
  tree basetype = BINFO_TYPE (binfo);

  if (!COMPLETE_TYPE_P (basetype))
    /* This error is now reported in xref_tag, thus giving better
       location information.  */
    return next_field;

  /* Place the base class.  */
  if (!is_empty_class (basetype))
    {
      tree decl;

      /* The containing class is non-empty because it has a non-empty
	 base class.  */
      CLASSTYPE_EMPTY_P (t) = 0;

      /* Create the FIELD_DECL.  */
      decl = build_decl (FIELD_DECL, NULL_TREE, CLASSTYPE_AS_BASE (basetype));
      DECL_ARTIFICIAL (decl) = 1;
      DECL_IGNORED_P (decl) = 1;
      DECL_FIELD_CONTEXT (decl) = t;
      DECL_SIZE (decl) = CLASSTYPE_SIZE (basetype);
      DECL_SIZE_UNIT (decl) = CLASSTYPE_SIZE_UNIT (basetype);
      DECL_ALIGN (decl) = CLASSTYPE_ALIGN (basetype);
      DECL_USER_ALIGN (decl) = CLASSTYPE_USER_ALIGN (basetype);
      DECL_MODE (decl) = TYPE_MODE (basetype);
      DECL_FIELD_IS_BASE (decl) = 1;

      /* Try to place the field.  It may take more than one try if we
	 have a hard time placing the field without putting two
	 objects of the same type at the same address.  */
      layout_nonempty_base_or_field (rli, decl, binfo, offsets);
      /* Add the new FIELD_DECL to the list of fields for T.  */
      TREE_CHAIN (decl) = *next_field;
      *next_field = decl;
      next_field = &TREE_CHAIN (decl);
    }
  else
    {
      tree eoc;
      bool atend;

      /* On some platforms (ARM), even empty classes will not be
	 byte-aligned.  */
      eoc = round_up (rli_size_unit_so_far (rli),
		      CLASSTYPE_ALIGN_UNIT (basetype));
      atend = layout_empty_base (binfo, eoc, offsets);
      /* A nearly-empty class "has no proper base class that is empty,
	 not morally virtual, and at an offset other than zero."  */
      if (!BINFO_VIRTUAL_P (binfo) && CLASSTYPE_NEARLY_EMPTY_P (t))
	{
	  if (atend)
	    CLASSTYPE_NEARLY_EMPTY_P (t) = 0;
	  /* The check above (used in G++ 3.2) is insufficient because
	     an empty class placed at offset zero might itself have an
	     empty base at a nonzero offset.  */
	  else if (walk_subobject_offsets (basetype,
					   empty_base_at_nonzero_offset_p,
					   size_zero_node,
					   /*offsets=*/NULL,
					   /*max_offset=*/NULL_TREE,
					   /*vbases_p=*/true))
	    {
	      if (abi_version_at_least (2))
		CLASSTYPE_NEARLY_EMPTY_P (t) = 0;
	      else
		warning (OPT_Wabi,
			 "class %qT will be considered nearly empty in a "
			 "future version of GCC", t);
	    }
	}

      /* We do not create a FIELD_DECL for empty base classes because
	 it might overlap some other field.  We want to be able to
	 create CONSTRUCTORs for the class by iterating over the
	 FIELD_DECLs, and the back end does not handle overlapping
	 FIELD_DECLs.  */

      /* An empty virtual base causes a class to be non-empty
	 -- but in that case we do not need to clear CLASSTYPE_EMPTY_P
	 here because that was already done when the virtual table
	 pointer was created.  */
    }

  /* Record the offsets of BINFO and its base subobjects.  */
  record_subobject_offsets (binfo,
			    BINFO_OFFSET (binfo),
			    offsets,
			    /*is_data_member=*/false);

  return next_field;
}

/* Layout all of the non-virtual base classes.  Record empty
   subobjects in OFFSETS.  T is the most derived type.  Return nonzero
   if the type cannot be nearly empty.  The fields created
   corresponding to the base classes will be inserted at
   *NEXT_FIELD.  */

static void
build_base_fields (record_layout_info rli,
		   splay_tree offsets, tree *next_field)
{
  /* Chain to hold all the new FIELD_DECLs which stand in for base class
     subobjects.  */
  tree t = rli->t;
  int n_baseclasses = BINFO_N_BASE_BINFOS (TYPE_BINFO (t));
  int i;

  /* The primary base class is always allocated first.  */
  if (CLASSTYPE_HAS_PRIMARY_BASE_P (t))
    next_field = build_base_field (rli, CLASSTYPE_PRIMARY_BINFO (t),
				   offsets, next_field);

  /* Now allocate the rest of the bases.  */
  for (i = 0; i < n_baseclasses; ++i)
    {
      tree base_binfo;

      base_binfo = BINFO_BASE_BINFO (TYPE_BINFO (t), i);

      /* The primary base was already allocated above, so we don't
	 need to allocate it again here.  */
      if (base_binfo == CLASSTYPE_PRIMARY_BINFO (t))
	continue;

      /* Virtual bases are added at the end (a primary virtual base
	 will have already been added).  */
      if (BINFO_VIRTUAL_P (base_binfo))
	continue;

      next_field = build_base_field (rli, base_binfo,
				     offsets, next_field);
    }
}

/* Go through the TYPE_METHODS of T issuing any appropriate
   diagnostics, figuring out which methods override which other
   methods, and so forth.  */

static void
check_methods (tree t)
{
  tree x;

  for (x = TYPE_METHODS (t); x; x = TREE_CHAIN (x))
    {
      check_for_override (x, t);
      if (DECL_PURE_VIRTUAL_P (x) && ! DECL_VINDEX (x))
	error ("initializer specified for non-virtual method %q+D", x);
      /* The name of the field is the original field name
	 Save this in auxiliary field for later overloading.  */
      if (DECL_VINDEX (x))
	{
	  TYPE_POLYMORPHIC_P (t) = 1;
	  if (DECL_PURE_VIRTUAL_P (x))
	    VEC_safe_push (tree, gc, CLASSTYPE_PURE_VIRTUALS (t), x);
	}
      /* All user-declared destructors are non-trivial.  */
      if (DECL_DESTRUCTOR_P (x))
	/* APPLE LOCAL begin omit calls to empty destructors 5559195 */
	{
	  TYPE_HAS_NONTRIVIAL_DESTRUCTOR (t) = 1;

	  /* Conservatively assume that destructor body is nontrivial.  Will
	     be unmarked during parsing of function body if it happens to be
	     trivial. */
	  CLASSTYPE_HAS_NONTRIVIAL_DESTRUCTOR_BODY (t) = 1;
	}
	/* APPLE LOCAL end omit calls to empty destructors 5559195 */
    }
}

/* FN is a constructor or destructor.  Clone the declaration to create
   a specialized in-charge or not-in-charge version, as indicated by
   NAME.  */

static tree
build_clone (tree fn, tree name)
{
  tree parms;
  tree clone;

  /* Copy the function.  */
  clone = copy_decl (fn);
  /* Remember where this function came from.  */
  DECL_CLONED_FUNCTION (clone) = fn;
  DECL_ABSTRACT_ORIGIN (clone) = fn;
  /* Reset the function name.  */
  DECL_NAME (clone) = name;
  SET_DECL_ASSEMBLER_NAME (clone, NULL_TREE);
  /* There's no pending inline data for this function.  */
  DECL_PENDING_INLINE_INFO (clone) = NULL;
  DECL_PENDING_INLINE_P (clone) = 0;
  /* And it hasn't yet been deferred.  */
  DECL_DEFERRED_FN (clone) = 0;

  /* The base-class destructor is not virtual.  */
  if (name == base_dtor_identifier)
    {
      DECL_VIRTUAL_P (clone) = 0;
      if (TREE_CODE (clone) != TEMPLATE_DECL)
	DECL_VINDEX (clone) = NULL_TREE;
    }

  /* If there was an in-charge parameter, drop it from the function
     type.  */
  if (DECL_HAS_IN_CHARGE_PARM_P (clone))
    {
      tree basetype;
      tree parmtypes;
      tree exceptions;

      exceptions = TYPE_RAISES_EXCEPTIONS (TREE_TYPE (clone));
      basetype = TYPE_METHOD_BASETYPE (TREE_TYPE (clone));
      parmtypes = TYPE_ARG_TYPES (TREE_TYPE (clone));
      /* Skip the `this' parameter.  */
      parmtypes = TREE_CHAIN (parmtypes);
      /* Skip the in-charge parameter.  */
      parmtypes = TREE_CHAIN (parmtypes);
      /* And the VTT parm, in a complete [cd]tor.  */
      if (DECL_HAS_VTT_PARM_P (fn)
	  && ! DECL_NEEDS_VTT_PARM_P (clone))
	parmtypes = TREE_CHAIN (parmtypes);
       /* If this is subobject constructor or destructor, add the vtt
	 parameter.  */
      TREE_TYPE (clone)
	= build_method_type_directly (basetype,
				      TREE_TYPE (TREE_TYPE (clone)),
				      parmtypes);
      if (exceptions)
	TREE_TYPE (clone) = build_exception_variant (TREE_TYPE (clone),
						     exceptions);
      TREE_TYPE (clone)
	= cp_build_type_attribute_variant (TREE_TYPE (clone),
					   TYPE_ATTRIBUTES (TREE_TYPE (fn)));
    }

  /* Copy the function parameters.  But, DECL_ARGUMENTS on a TEMPLATE_DECL
     aren't function parameters; those are the template parameters.  */
  if (TREE_CODE (clone) != TEMPLATE_DECL)
    {
      DECL_ARGUMENTS (clone) = copy_list (DECL_ARGUMENTS (clone));
      /* Remove the in-charge parameter.  */
      if (DECL_HAS_IN_CHARGE_PARM_P (clone))
	{
	  TREE_CHAIN (DECL_ARGUMENTS (clone))
	    = TREE_CHAIN (TREE_CHAIN (DECL_ARGUMENTS (clone)));
	  DECL_HAS_IN_CHARGE_PARM_P (clone) = 0;
	}
      /* And the VTT parm, in a complete [cd]tor.  */
      if (DECL_HAS_VTT_PARM_P (fn))
	{
	  if (DECL_NEEDS_VTT_PARM_P (clone))
	    DECL_HAS_VTT_PARM_P (clone) = 1;
	  else
	    {
	      TREE_CHAIN (DECL_ARGUMENTS (clone))
		= TREE_CHAIN (TREE_CHAIN (DECL_ARGUMENTS (clone)));
	      DECL_HAS_VTT_PARM_P (clone) = 0;
	    }
	}

      for (parms = DECL_ARGUMENTS (clone); parms; parms = TREE_CHAIN (parms))
	{
	  DECL_CONTEXT (parms) = clone;
	  cxx_dup_lang_specific_decl (parms);
	}
    }

  /* Create the RTL for this function.  */
  SET_DECL_RTL (clone, NULL_RTX);
  rest_of_decl_compilation (clone, /*top_level=*/1, at_eof);

  /* Make it easy to find the CLONE given the FN.  */
  TREE_CHAIN (clone) = TREE_CHAIN (fn);
  TREE_CHAIN (fn) = clone;

  /* If this is a template, handle the DECL_TEMPLATE_RESULT as well.  */
  if (TREE_CODE (clone) == TEMPLATE_DECL)
    {
      tree result;

      DECL_TEMPLATE_RESULT (clone)
	= build_clone (DECL_TEMPLATE_RESULT (clone), name);
      result = DECL_TEMPLATE_RESULT (clone);
      DECL_TEMPLATE_INFO (result) = copy_node (DECL_TEMPLATE_INFO (result));
      DECL_TI_TEMPLATE (result) = clone;
    }
  else if (pch_file)
    note_decl_for_pch (clone);

  return clone;
}

/* Produce declarations for all appropriate clones of FN.  If
   UPDATE_METHOD_VEC_P is nonzero, the clones are added to the
   CLASTYPE_METHOD_VEC as well.  */

void
clone_function_decl (tree fn, int update_method_vec_p)
{
  tree clone;

  /* Avoid inappropriate cloning.  */
  if (TREE_CHAIN (fn)
      && DECL_CLONED_FUNCTION (TREE_CHAIN (fn)))
    return;

  if (DECL_MAYBE_IN_CHARGE_CONSTRUCTOR_P (fn))
    {
      /* For each constructor, we need two variants: an in-charge version
	 and a not-in-charge version.  */
      clone = build_clone (fn, complete_ctor_identifier);
      if (update_method_vec_p)
	add_method (DECL_CONTEXT (clone), clone, NULL_TREE);
      clone = build_clone (fn, base_ctor_identifier);
      if (update_method_vec_p)
	add_method (DECL_CONTEXT (clone), clone, NULL_TREE);
    }
  else
    {
      gcc_assert (DECL_MAYBE_IN_CHARGE_DESTRUCTOR_P (fn));

      /* For each destructor, we need three variants: an in-charge
	 version, a not-in-charge version, and an in-charge deleting
	 version.  We clone the deleting version first because that
	 means it will go second on the TYPE_METHODS list -- and that
	 corresponds to the correct layout order in the virtual
	 function table.

	 For a non-virtual destructor, we do not build a deleting
	 destructor.  */
      if (DECL_VIRTUAL_P (fn))
	{
	  clone = build_clone (fn, deleting_dtor_identifier);
	  if (update_method_vec_p)
	    add_method (DECL_CONTEXT (clone), clone, NULL_TREE);
	}
      clone = build_clone (fn, complete_dtor_identifier);
      if (update_method_vec_p)
	add_method (DECL_CONTEXT (clone), clone, NULL_TREE);
      clone = build_clone (fn, base_dtor_identifier);
      if (update_method_vec_p)
	add_method (DECL_CONTEXT (clone), clone, NULL_TREE);
    }

  /* Note that this is an abstract function that is never emitted.  */
  DECL_ABSTRACT (fn) = 1;
}

/* DECL is an in charge constructor, which is being defined. This will
   have had an in class declaration, from whence clones were
   declared. An out-of-class definition can specify additional default
   arguments. As it is the clones that are involved in overload
   resolution, we must propagate the information from the DECL to its
   clones.  */

void
adjust_clone_args (tree decl)
{
  tree clone;

  for (clone = TREE_CHAIN (decl); clone && DECL_CLONED_FUNCTION (clone);
       clone = TREE_CHAIN (clone))
    {
      tree orig_clone_parms = TYPE_ARG_TYPES (TREE_TYPE (clone));
      tree orig_decl_parms = TYPE_ARG_TYPES (TREE_TYPE (decl));
      tree decl_parms, clone_parms;

      clone_parms = orig_clone_parms;

      /* Skip the 'this' parameter.  */
      orig_clone_parms = TREE_CHAIN (orig_clone_parms);
      orig_decl_parms = TREE_CHAIN (orig_decl_parms);

      if (DECL_HAS_IN_CHARGE_PARM_P (decl))
	orig_decl_parms = TREE_CHAIN (orig_decl_parms);
      if (DECL_HAS_VTT_PARM_P (decl))
	orig_decl_parms = TREE_CHAIN (orig_decl_parms);

      clone_parms = orig_clone_parms;
      if (DECL_HAS_VTT_PARM_P (clone))
	clone_parms = TREE_CHAIN (clone_parms);

      for (decl_parms = orig_decl_parms; decl_parms;
	   decl_parms = TREE_CHAIN (decl_parms),
	     clone_parms = TREE_CHAIN (clone_parms))
	{
	  gcc_assert (same_type_p (TREE_TYPE (decl_parms),
				   TREE_TYPE (clone_parms)));

	  if (TREE_PURPOSE (decl_parms) && !TREE_PURPOSE (clone_parms))
	    {
	      /* A default parameter has been added. Adjust the
		 clone's parameters.  */
	      tree exceptions = TYPE_RAISES_EXCEPTIONS (TREE_TYPE (clone));
	      tree basetype = TYPE_METHOD_BASETYPE (TREE_TYPE (clone));
	      tree type;

	      clone_parms = orig_decl_parms;

	      if (DECL_HAS_VTT_PARM_P (clone))
		{
		  clone_parms = tree_cons (TREE_PURPOSE (orig_clone_parms),
					   TREE_VALUE (orig_clone_parms),
					   clone_parms);
		  TREE_TYPE (clone_parms) = TREE_TYPE (orig_clone_parms);
		}
	      type = build_method_type_directly (basetype,
						 TREE_TYPE (TREE_TYPE (clone)),
						 clone_parms);
	      if (exceptions)
		type = build_exception_variant (type, exceptions);
	      TREE_TYPE (clone) = type;

	      clone_parms = NULL_TREE;
	      break;
	    }
	}
      gcc_assert (!clone_parms);
    }
}

/* For each of the constructors and destructors in T, create an
   in-charge and not-in-charge variant.  */

static void
clone_constructors_and_destructors (tree t)
{
  tree fns;

  /* If for some reason we don't have a CLASSTYPE_METHOD_VEC, we bail
     out now.  */
  if (!CLASSTYPE_METHOD_VEC (t))
    return;

  for (fns = CLASSTYPE_CONSTRUCTORS (t); fns; fns = OVL_NEXT (fns))
    clone_function_decl (OVL_CURRENT (fns), /*update_method_vec_p=*/1);
  for (fns = CLASSTYPE_DESTRUCTORS (t); fns; fns = OVL_NEXT (fns))
    clone_function_decl (OVL_CURRENT (fns), /*update_method_vec_p=*/1);
}

/* Remove all zero-width bit-fields from T.  */

static void
remove_zero_width_bit_fields (tree t)
{
  tree *fieldsp;

  fieldsp = &TYPE_FIELDS (t);
  while (*fieldsp)
    {
      if (TREE_CODE (*fieldsp) == FIELD_DECL
	  && DECL_C_BIT_FIELD (*fieldsp)
	  && DECL_INITIAL (*fieldsp))
	*fieldsp = TREE_CHAIN (*fieldsp);
      else
	fieldsp = &TREE_CHAIN (*fieldsp);
    }
}

/* Returns TRUE iff we need a cookie when dynamically allocating an
   array whose elements have the indicated class TYPE.  */

static bool
type_requires_array_cookie (tree type)
{
  tree fns;
  bool has_two_argument_delete_p = false;

  gcc_assert (CLASS_TYPE_P (type));

  /* If there's a non-trivial destructor, we need a cookie.  In order
     to iterate through the array calling the destructor for each
     element, we'll have to know how many elements there are.  */
  if (TYPE_HAS_NONTRIVIAL_DESTRUCTOR (type))
    return true;

  /* If the usual deallocation function is a two-argument whose second
     argument is of type `size_t', then we have to pass the size of
     the array to the deallocation function, so we will need to store
     a cookie.  */
  fns = lookup_fnfields (TYPE_BINFO (type),
			 ansi_opname (VEC_DELETE_EXPR),
			 /*protect=*/0);
  /* If there are no `operator []' members, or the lookup is
     ambiguous, then we don't need a cookie.  */
  if (!fns || fns == error_mark_node)
    return false;
  /* Loop through all of the functions.  */
  for (fns = BASELINK_FUNCTIONS (fns); fns; fns = OVL_NEXT (fns))
    {
      tree fn;
      tree second_parm;

      /* Select the current function.  */
      fn = OVL_CURRENT (fns);
      /* See if this function is a one-argument delete function.  If
	 it is, then it will be the usual deallocation function.  */
      second_parm = TREE_CHAIN (TYPE_ARG_TYPES (TREE_TYPE (fn)));
      if (second_parm == void_list_node)
	return false;
      /* Otherwise, if we have a two-argument function and the second
	 argument is `size_t', it will be the usual deallocation
	 function -- unless there is one-argument function, too.  */
      if (TREE_CHAIN (second_parm) == void_list_node
	  && same_type_p (TREE_VALUE (second_parm), sizetype))
	has_two_argument_delete_p = true;
    }

  return has_two_argument_delete_p;
}

/* Check the validity of the bases and members declared in T.  Add any
   implicitly-generated functions (like copy-constructors and
   assignment operators).  Compute various flag bits (like
   CLASSTYPE_NON_POD_T) for T.  This routine works purely at the C++
   level: i.e., independently of the ABI in use.  */

static void
check_bases_and_members (tree t)
{
  /* Nonzero if the implicitly generated copy constructor should take
     a non-const reference argument.  */
  int cant_have_const_ctor;
  /* Nonzero if the implicitly generated assignment operator
     should take a non-const reference argument.  */
  int no_const_asn_ref;
  tree access_decls;

  /* By default, we use const reference arguments and generate default
     constructors.  */
  cant_have_const_ctor = 0;
  no_const_asn_ref = 0;

  /* Check all the base-classes.  */
  check_bases (t, &cant_have_const_ctor,
	       &no_const_asn_ref);

  /* Check all the method declarations.  */
  check_methods (t);

  /* Check all the data member declarations.  We cannot call
     check_field_decls until we have called check_bases check_methods,
     as check_field_decls depends on TYPE_HAS_NONTRIVIAL_DESTRUCTOR
     being set appropriately.  */
  check_field_decls (t, &access_decls,
		     &cant_have_const_ctor,
		     &no_const_asn_ref);

  /* A nearly-empty class has to be vptr-containing; a nearly empty
     class contains just a vptr.  */
  if (!TYPE_CONTAINS_VPTR_P (t))
    CLASSTYPE_NEARLY_EMPTY_P (t) = 0;

  /* Do some bookkeeping that will guide the generation of implicitly
     declared member functions.  */
  TYPE_HAS_COMPLEX_INIT_REF (t)
    |= (TYPE_HAS_INIT_REF (t) || TYPE_CONTAINS_VPTR_P (t));
  TYPE_NEEDS_CONSTRUCTING (t)
    |= (TYPE_HAS_CONSTRUCTOR (t) || TYPE_CONTAINS_VPTR_P (t));
  CLASSTYPE_NON_AGGREGATE (t)
    |= (TYPE_HAS_CONSTRUCTOR (t) || TYPE_POLYMORPHIC_P (t));
  CLASSTYPE_NON_POD_P (t)
    |= (CLASSTYPE_NON_AGGREGATE (t)
	|| TYPE_HAS_NONTRIVIAL_DESTRUCTOR (t)
	|| TYPE_HAS_ASSIGN_REF (t));
  TYPE_HAS_COMPLEX_ASSIGN_REF (t)
    |= TYPE_HAS_ASSIGN_REF (t) || TYPE_CONTAINS_VPTR_P (t);

  /* Synthesize any needed methods.  */
  add_implicitly_declared_members (t,
				   cant_have_const_ctor,
				   no_const_asn_ref);

  /* Create the in-charge and not-in-charge variants of constructors
     and destructors.  */
  clone_constructors_and_destructors (t);

  /* Process the using-declarations.  */
  for (; access_decls; access_decls = TREE_CHAIN (access_decls))
    handle_using_decl (TREE_VALUE (access_decls), t);

  /* Build and sort the CLASSTYPE_METHOD_VEC.  */
  finish_struct_methods (t);

  /* Figure out whether or not we will need a cookie when dynamically
     allocating an array of this type.  */
  TYPE_LANG_SPECIFIC (t)->u.c.vec_new_uses_cookie
    = type_requires_array_cookie (t);
}

/* If T needs a pointer to its virtual function table, set TYPE_VFIELD
   accordingly.  If a new vfield was created (because T doesn't have a
   primary base class), then the newly created field is returned.  It
   is not added to the TYPE_FIELDS list; it is the caller's
   responsibility to do that.  Accumulate declared virtual functions
   on VIRTUALS_P.  */

static tree
create_vtable_ptr (tree t, tree* virtuals_p)
{
  tree fn;

  /* Collect the virtual functions declared in T.  */
  for (fn = TYPE_METHODS (t); fn; fn = TREE_CHAIN (fn))
    if (DECL_VINDEX (fn) && !DECL_MAYBE_IN_CHARGE_DESTRUCTOR_P (fn)
	&& TREE_CODE (DECL_VINDEX (fn)) != INTEGER_CST)
      {
	tree new_virtual = make_node (TREE_LIST);

	BV_FN (new_virtual) = fn;
	BV_DELTA (new_virtual) = integer_zero_node;
	BV_VCALL_INDEX (new_virtual) = NULL_TREE;

	TREE_CHAIN (new_virtual) = *virtuals_p;
	*virtuals_p = new_virtual;
      }

  /* If we couldn't find an appropriate base class, create a new field
     here.  Even if there weren't any new virtual functions, we might need a
     new virtual function table if we're supposed to include vptrs in
     all classes that need them.  */
  if (!TYPE_VFIELD (t) && (*virtuals_p || TYPE_CONTAINS_VPTR_P (t)))
    {
      /* We build this decl with vtbl_ptr_type_node, which is a
	 `vtable_entry_type*'.  It might seem more precise to use
	 `vtable_entry_type (*)[N]' where N is the number of virtual
	 functions.  However, that would require the vtable pointer in
	 base classes to have a different type than the vtable pointer
	 in derived classes.  We could make that happen, but that
	 still wouldn't solve all the problems.  In particular, the
	 type-based alias analysis code would decide that assignments
	 to the base class vtable pointer can't alias assignments to
	 the derived class vtable pointer, since they have different
	 types.  Thus, in a derived class destructor, where the base
	 class constructor was inlined, we could generate bad code for
	 setting up the vtable pointer.

	 Therefore, we use one type for all vtable pointers.  We still
	 use a type-correct type; it's just doesn't indicate the array
	 bounds.  That's better than using `void*' or some such; it's
	 cleaner, and it let's the alias analysis code know that these
	 stores cannot alias stores to void*!  */
      tree field;

      field = build_decl (FIELD_DECL, get_vfield_name (t), vtbl_ptr_type_node);
      DECL_VIRTUAL_P (field) = 1;
      DECL_ARTIFICIAL (field) = 1;
      DECL_FIELD_CONTEXT (field) = t;
      DECL_FCONTEXT (field) = t;

      TYPE_VFIELD (t) = field;

      /* This class is non-empty.  */
      CLASSTYPE_EMPTY_P (t) = 0;

      return field;
    }

  return NULL_TREE;
}

/* Fixup the inline function given by INFO now that the class is
   complete.  */

static void
fixup_pending_inline (tree fn)
{
  if (DECL_PENDING_INLINE_INFO (fn))
    {
      tree args = DECL_ARGUMENTS (fn);
      while (args)
	{
	  DECL_CONTEXT (args) = fn;
	  args = TREE_CHAIN (args);
	}
    }
}

/* Fixup the inline methods and friends in TYPE now that TYPE is
   complete.  */

static void
fixup_inline_methods (tree type)
{
  tree method = TYPE_METHODS (type);
  VEC(tree,gc) *friends;
  unsigned ix;

  if (method && TREE_CODE (method) == TREE_VEC)
    {
      if (TREE_VEC_ELT (method, 1))
	method = TREE_VEC_ELT (method, 1);
      else if (TREE_VEC_ELT (method, 0))
	method = TREE_VEC_ELT (method, 0);
      else
	method = TREE_VEC_ELT (method, 2);
    }

  /* Do inline member functions.  */
  for (; method; method = TREE_CHAIN (method))
    fixup_pending_inline (method);

  /* Do friends.  */
  for (friends = CLASSTYPE_INLINE_FRIENDS (type), ix = 0;
       VEC_iterate (tree, friends, ix, method); ix++)
    fixup_pending_inline (method);
  CLASSTYPE_INLINE_FRIENDS (type) = NULL;
}

/* Add OFFSET to all base types of BINFO which is a base in the
   hierarchy dominated by T.

   OFFSET, which is a type offset, is number of bytes.  */

static void
propagate_binfo_offsets (tree binfo, tree offset)
{
  int i;
  tree primary_binfo;
  tree base_binfo;

  /* Update BINFO's offset.  */
  BINFO_OFFSET (binfo)
    = convert (sizetype,
	       size_binop (PLUS_EXPR,
			   convert (ssizetype, BINFO_OFFSET (binfo)),
			   offset));

  /* Find the primary base class.  */
  primary_binfo = get_primary_binfo (binfo);

  if (primary_binfo && BINFO_INHERITANCE_CHAIN (primary_binfo) == binfo)
    propagate_binfo_offsets (primary_binfo, offset);

  /* Scan all of the bases, pushing the BINFO_OFFSET adjust
     downwards.  */
  for (i = 0; BINFO_BASE_ITERATE (binfo, i, base_binfo); ++i)
    {
      /* Don't do the primary base twice.  */
      if (base_binfo == primary_binfo)
	continue;

      if (BINFO_VIRTUAL_P (base_binfo))
	continue;

      propagate_binfo_offsets (base_binfo, offset);
    }
}

/* Set BINFO_OFFSET for all of the virtual bases for RLI->T.  Update
   TYPE_ALIGN and TYPE_SIZE for T.  OFFSETS gives the location of
   empty subobjects of T.  */

static void
layout_virtual_bases (record_layout_info rli, splay_tree offsets)
{
  tree vbase;
  tree t = rli->t;
  bool first_vbase = true;
  tree *next_field;

  if (BINFO_N_BASE_BINFOS (TYPE_BINFO (t)) == 0)
    return;

  if (!abi_version_at_least(2))
    {
      /* In G++ 3.2, we incorrectly rounded the size before laying out
	 the virtual bases.  */
      finish_record_layout (rli, /*free_p=*/false);
#ifdef STRUCTURE_SIZE_BOUNDARY
      /* Packed structures don't need to have minimum size.  */
      if (! TYPE_PACKED (t))
	TYPE_ALIGN (t) = MAX (TYPE_ALIGN (t), (unsigned) STRUCTURE_SIZE_BOUNDARY);
#endif
      rli->offset = TYPE_SIZE_UNIT (t);
      rli->bitpos = bitsize_zero_node;
      rli->record_align = TYPE_ALIGN (t);
    }

  /* Find the last field.  The artificial fields created for virtual
     bases will go after the last extant field to date.  */
  next_field = &TYPE_FIELDS (t);
  while (*next_field)
    next_field = &TREE_CHAIN (*next_field);

  /* Go through the virtual bases, allocating space for each virtual
     base that is not already a primary base class.  These are
     allocated in inheritance graph order.  */
  for (vbase = TYPE_BINFO (t); vbase; vbase = TREE_CHAIN (vbase))
    {
      if (!BINFO_VIRTUAL_P (vbase))
	continue;

      if (!BINFO_PRIMARY_P (vbase))
	{
	  tree basetype = TREE_TYPE (vbase);

	  /* This virtual base is not a primary base of any class in the
	     hierarchy, so we have to add space for it.  */
	  next_field = build_base_field (rli, vbase,
					 offsets, next_field);

	  /* If the first virtual base might have been placed at a
	     lower address, had we started from CLASSTYPE_SIZE, rather
	     than TYPE_SIZE, issue a warning.  There can be both false
	     positives and false negatives from this warning in rare
	     cases; to deal with all the possibilities would probably
	     require performing both layout algorithms and comparing
	     the results which is not particularly tractable.  */
	  if (warn_abi
	      && first_vbase
	      && (tree_int_cst_lt
		  (size_binop (CEIL_DIV_EXPR,
			       round_up (CLASSTYPE_SIZE (t),
					 CLASSTYPE_ALIGN (basetype)),
			       bitsize_unit_node),
		   BINFO_OFFSET (vbase))))
	    warning (OPT_Wabi,
		     "offset of virtual base %qT is not ABI-compliant and "
		     "may change in a future version of GCC",
		     basetype);

	  first_vbase = false;
	}
    }
}

/* Returns the offset of the byte just past the end of the base class
   BINFO.  */

static tree
end_of_base (tree binfo)
{
  tree size;

  if (is_empty_class (BINFO_TYPE (binfo)))
    /* An empty class has zero CLASSTYPE_SIZE_UNIT, but we need to
       allocate some space for it. It cannot have virtual bases, so
       TYPE_SIZE_UNIT is fine.  */
    size = TYPE_SIZE_UNIT (BINFO_TYPE (binfo));
  else
    size = CLASSTYPE_SIZE_UNIT (BINFO_TYPE (binfo));

  return size_binop (PLUS_EXPR, BINFO_OFFSET (binfo), size);
}

/* Returns the offset of the byte just past the end of the base class
   with the highest offset in T.  If INCLUDE_VIRTUALS_P is zero, then
   only non-virtual bases are included.  */

static tree
end_of_class (tree t, int include_virtuals_p)
{
  tree result = size_zero_node;
  VEC(tree,gc) *vbases;
  tree binfo;
  tree base_binfo;
  tree offset;
  int i;

  for (binfo = TYPE_BINFO (t), i = 0;
       BINFO_BASE_ITERATE (binfo, i, base_binfo); ++i)
    {
      if (!include_virtuals_p
	  && BINFO_VIRTUAL_P (base_binfo)
	  && (!BINFO_PRIMARY_P (base_binfo)
	      || BINFO_INHERITANCE_CHAIN (base_binfo) != TYPE_BINFO (t)))
	continue;

      offset = end_of_base (base_binfo);
      if (INT_CST_LT_UNSIGNED (result, offset))
	result = offset;
    }

  /* G++ 3.2 did not check indirect virtual bases.  */
  if (abi_version_at_least (2) && include_virtuals_p)
    for (vbases = CLASSTYPE_VBASECLASSES (t), i = 0;
	 VEC_iterate (tree, vbases, i, base_binfo); i++)
      {
	offset = end_of_base (base_binfo);
	if (INT_CST_LT_UNSIGNED (result, offset))
	  result = offset;
      }

  return result;
}

/* Warn about bases of T that are inaccessible because they are
   ambiguous.  For example:

     struct S {};
     struct T : public S {};
     struct U : public S, public T {};

   Here, `(S*) new U' is not allowed because there are two `S'
   subobjects of U.  */

static void
warn_about_ambiguous_bases (tree t)
{
  int i;
  VEC(tree,gc) *vbases;
  tree basetype;
  tree binfo;
  tree base_binfo;

  /* If there are no repeated bases, nothing can be ambiguous.  */
  if (!CLASSTYPE_REPEATED_BASE_P (t))
    return;

  /* Check direct bases.  */
  for (binfo = TYPE_BINFO (t), i = 0;
       BINFO_BASE_ITERATE (binfo, i, base_binfo); ++i)
    {
      basetype = BINFO_TYPE (base_binfo);

      if (!lookup_base (t, basetype, ba_unique | ba_quiet, NULL))
	warning (0, "direct base %qT inaccessible in %qT due to ambiguity",
		 basetype, t);
    }

  /* Check for ambiguous virtual bases.  */
  if (extra_warnings)
    for (vbases = CLASSTYPE_VBASECLASSES (t), i = 0;
	 VEC_iterate (tree, vbases, i, binfo); i++)
      {
	basetype = BINFO_TYPE (binfo);

	if (!lookup_base (t, basetype, ba_unique | ba_quiet, NULL))
	  warning (OPT_Wextra, "virtual base %qT inaccessible in %qT due to ambiguity",
		   basetype, t);
      }
}

/* Compare two INTEGER_CSTs K1 and K2.  */

static int
splay_tree_compare_integer_csts (splay_tree_key k1, splay_tree_key k2)
{
  return tree_int_cst_compare ((tree) k1, (tree) k2);
}

/* Increase the size indicated in RLI to account for empty classes
   that are "off the end" of the class.  */

static void
include_empty_classes (record_layout_info rli)
{
  tree eoc;
  tree rli_size;

  /* It might be the case that we grew the class to allocate a
     zero-sized base class.  That won't be reflected in RLI, yet,
     because we are willing to overlay multiple bases at the same
     offset.  However, now we need to make sure that RLI is big enough
     to reflect the entire class.  */
  eoc = end_of_class (rli->t,
		      CLASSTYPE_AS_BASE (rli->t) != NULL_TREE);
  rli_size = rli_size_unit_so_far (rli);
  if (TREE_CODE (rli_size) == INTEGER_CST
      && INT_CST_LT_UNSIGNED (rli_size, eoc))
    {
      if (!abi_version_at_least (2))
	/* In version 1 of the ABI, the size of a class that ends with
	   a bitfield was not rounded up to a whole multiple of a
	   byte.  Because rli_size_unit_so_far returns only the number
	   of fully allocated bytes, any extra bits were not included
	   in the size.  */
	rli->bitpos = round_down (rli->bitpos, BITS_PER_UNIT);
      else
	/* The size should have been rounded to a whole byte.  */
	gcc_assert (tree_int_cst_equal
		    (rli->bitpos, round_down (rli->bitpos, BITS_PER_UNIT)));
      rli->bitpos
	= size_binop (PLUS_EXPR,
		      rli->bitpos,
		      size_binop (MULT_EXPR,
				  convert (bitsizetype,
					   size_binop (MINUS_EXPR,
						       eoc, rli_size)),
				  bitsize_int (BITS_PER_UNIT)));
      normalize_rli (rli);
    }
}

/* Calculate the TYPE_SIZE, TYPE_ALIGN, etc for T.  Calculate
   BINFO_OFFSETs for all of the base-classes.  Position the vtable
   pointer.  Accumulate declared virtual functions on VIRTUALS_P.  */

static void
layout_class_type (tree t, tree *virtuals_p)
{
  tree non_static_data_members;
  tree field;
  tree vptr;
  record_layout_info rli;
  /* Maps offsets (represented as INTEGER_CSTs) to a TREE_LIST of
     types that appear at that offset.  */
  splay_tree empty_base_offsets;
  /* True if the last field layed out was a bit-field.  */
  bool last_field_was_bitfield = false;
  /* The location at which the next field should be inserted.  */
  tree *next_field;
  /* T, as a base class.  */
  tree base_t;

  /* Keep track of the first non-static data member.  */
  non_static_data_members = TYPE_FIELDS (t);

  /* Start laying out the record.  */
  rli = start_record_layout (t);

  /* Mark all the primary bases in the hierarchy.  */
  determine_primary_bases (t);

  /* Create a pointer to our virtual function table.  */
  vptr = create_vtable_ptr (t, virtuals_p);

  /* The vptr is always the first thing in the class.  */
  if (vptr)
    {
      TREE_CHAIN (vptr) = TYPE_FIELDS (t);
      TYPE_FIELDS (t) = vptr;
      next_field = &TREE_CHAIN (vptr);
      place_field (rli, vptr);
    }
  else
    next_field = &TYPE_FIELDS (t);

  /* Build FIELD_DECLs for all of the non-virtual base-types.  */
  empty_base_offsets = splay_tree_new (splay_tree_compare_integer_csts,
				       NULL, NULL);
  build_base_fields (rli, empty_base_offsets, next_field);

  /* Layout the non-static data members.  */
  for (field = non_static_data_members; field; field = TREE_CHAIN (field))
    {
      tree type;
      tree padding;

      /* We still pass things that aren't non-static data members to
	 the back-end, in case it wants to do something with them.  */
      if (TREE_CODE (field) != FIELD_DECL)
	{
	  place_field (rli, field);
	  /* If the static data member has incomplete type, keep track
	     of it so that it can be completed later.  (The handling
	     of pending statics in finish_record_layout is
	     insufficient; consider:

	       struct S1;
	       struct S2 { static S1 s1; };

	     At this point, finish_record_layout will be called, but
	     S1 is still incomplete.)  */
	  if (TREE_CODE (field) == VAR_DECL)
	    {
	      maybe_register_incomplete_var (field);
	      /* The visibility of static data members is determined
		 at their point of declaration, not their point of
		 definition.  */
	      determine_visibility (field);
	    }
	  continue;
	}

      type = TREE_TYPE (field);
      if (type == error_mark_node)
	continue;

      padding = NULL_TREE;

      /* If this field is a bit-field whose width is greater than its
	 type, then there are some special rules for allocating
	 it.  */
      if (DECL_C_BIT_FIELD (field)
	  && INT_CST_LT (TYPE_SIZE (type), DECL_SIZE (field)))
	{
	  integer_type_kind itk;
	  tree integer_type;
	  bool was_unnamed_p = false;
	  /* We must allocate the bits as if suitably aligned for the
	     longest integer type that fits in this many bits.  type
	     of the field.  Then, we are supposed to use the left over
	     bits as additional padding.  */
	  for (itk = itk_char; itk != itk_none; ++itk)
	    if (INT_CST_LT (DECL_SIZE (field),
			    TYPE_SIZE (integer_types[itk])))
	      break;

	  /* ITK now indicates a type that is too large for the
	     field.  We have to back up by one to find the largest
	     type that fits.  */
	  integer_type = integer_types[itk - 1];

	  /* Figure out how much additional padding is required.  GCC
	     3.2 always created a padding field, even if it had zero
	     width.  */
	  if (!abi_version_at_least (2)
	      || INT_CST_LT (TYPE_SIZE (integer_type), DECL_SIZE (field)))
	    {
	      if (abi_version_at_least (2) && TREE_CODE (t) == UNION_TYPE)
		/* In a union, the padding field must have the full width
		   of the bit-field; all fields start at offset zero.  */
		padding = DECL_SIZE (field);
	      else
		{
		  if (TREE_CODE (t) == UNION_TYPE)
		    warning (OPT_Wabi, "size assigned to %qT may not be "
			     "ABI-compliant and may change in a future "
			     "version of GCC",
			     t);
		  padding = size_binop (MINUS_EXPR, DECL_SIZE (field),
					TYPE_SIZE (integer_type));
		}
	    }
#ifdef PCC_BITFIELD_TYPE_MATTERS
	  /* An unnamed bitfield does not normally affect the
	     alignment of the containing class on a target where
	     PCC_BITFIELD_TYPE_MATTERS.  But, the C++ ABI does not
	     make any exceptions for unnamed bitfields when the
	     bitfields are longer than their types.  Therefore, we
	     temporarily give the field a name.  */
	  if (PCC_BITFIELD_TYPE_MATTERS && !DECL_NAME (field))
	    {
	      was_unnamed_p = true;
	      DECL_NAME (field) = make_anon_name ();
	    }
#endif
	  DECL_SIZE (field) = TYPE_SIZE (integer_type);
	  DECL_ALIGN (field) = TYPE_ALIGN (integer_type);
	  DECL_USER_ALIGN (field) = TYPE_USER_ALIGN (integer_type);
	  layout_nonempty_base_or_field (rli, field, NULL_TREE,
					 empty_base_offsets);
	  if (was_unnamed_p)
	    DECL_NAME (field) = NULL_TREE;
	  /* Now that layout has been performed, set the size of the
	     field to the size of its declared type; the rest of the
	     field is effectively invisible.  */
	  DECL_SIZE (field) = TYPE_SIZE (type);
	  /* We must also reset the DECL_MODE of the field.  */
	  if (abi_version_at_least (2))
	    DECL_MODE (field) = TYPE_MODE (type);
	  else if (warn_abi
		   && DECL_MODE (field) != TYPE_MODE (type))
	    /* Versions of G++ before G++ 3.4 did not reset the
	       DECL_MODE.  */
	    warning (OPT_Wabi,
		     "the offset of %qD may not be ABI-compliant and may "
		     "change in a future version of GCC", field);
	}
      else
	layout_nonempty_base_or_field (rli, field, NULL_TREE,
				       empty_base_offsets);

      /* Remember the location of any empty classes in FIELD.  */
      if (abi_version_at_least (2))
	record_subobject_offsets (TREE_TYPE (field),
				  byte_position(field),
				  empty_base_offsets,
				  /*is_data_member=*/true);

      /* If a bit-field does not immediately follow another bit-field,
	 and yet it starts in the middle of a byte, we have failed to
	 comply with the ABI.  */
      if (warn_abi
	  && DECL_C_BIT_FIELD (field)
	  /* The TREE_NO_WARNING flag gets set by Objective-C when
	     laying out an Objective-C class.  The ObjC ABI differs
	     from the C++ ABI, and so we do not want a warning
	     here.  */
	  && !TREE_NO_WARNING (field)
	  && !last_field_was_bitfield
	  && !integer_zerop (size_binop (TRUNC_MOD_EXPR,
					 DECL_FIELD_BIT_OFFSET (field),
					 bitsize_unit_node)))
	warning (OPT_Wabi, "offset of %q+D is not ABI-compliant and may "
		 "change in a future version of GCC", field);

      /* G++ used to use DECL_FIELD_OFFSET as if it were the byte
	 offset of the field.  */
      if (warn_abi
	  && !tree_int_cst_equal (DECL_FIELD_OFFSET (field),
				  byte_position (field))
	  && contains_empty_class_p (TREE_TYPE (field)))
	warning (OPT_Wabi, "%q+D contains empty classes which may cause base "
		 "classes to be placed at different locations in a "
		 "future version of GCC", field);

      /* The middle end uses the type of expressions to determine the
	 possible range of expression values.  In order to optimize
	 "x.i > 7" to "false" for a 2-bit bitfield "i", the middle end
	 must be made aware of the width of "i", via its type.

	 Because C++ does not have integer types of arbitrary width,
	 we must (for the purposes of the front end) convert from the
	 type assigned here to the declared type of the bitfield
	 whenever a bitfield expression is used as an rvalue.
	 Similarly, when assigning a value to a bitfield, the value
	 must be converted to the type given the bitfield here.  */
      if (DECL_C_BIT_FIELD (field))
	{
	  tree ftype;
	  unsigned HOST_WIDE_INT width;
	  ftype = TREE_TYPE (field);
	  width = tree_low_cst (DECL_SIZE (field), /*unsignedp=*/1);
	  if (width != TYPE_PRECISION (ftype))
	    TREE_TYPE (field)
	      = c_build_bitfield_integer_type (width,
					       TYPE_UNSIGNED (ftype));
	}

      /* If we needed additional padding after this field, add it
	 now.  */
      if (padding)
	{
	  tree padding_field;

	  padding_field = build_decl (FIELD_DECL,
				      NULL_TREE,
				      char_type_node);
	  DECL_BIT_FIELD (padding_field) = 1;
	  DECL_SIZE (padding_field) = padding;
	  DECL_CONTEXT (padding_field) = t;
	  DECL_ARTIFICIAL (padding_field) = 1;
	  DECL_IGNORED_P (padding_field) = 1;
	  layout_nonempty_base_or_field (rli, padding_field,
					 NULL_TREE,
					 empty_base_offsets);
	}

      last_field_was_bitfield = DECL_C_BIT_FIELD (field);
    }

  if (abi_version_at_least (2) && !integer_zerop (rli->bitpos))
    {
      /* Make sure that we are on a byte boundary so that the size of
	 the class without virtual bases will always be a round number
	 of bytes.  */
      rli->bitpos = round_up (rli->bitpos, BITS_PER_UNIT);
      normalize_rli (rli);
    }

  /* G++ 3.2 does not allow virtual bases to be overlaid with tail
     padding.  */
  if (!abi_version_at_least (2))
    include_empty_classes(rli);

  /* Delete all zero-width bit-fields from the list of fields.  Now
     that the type is laid out they are no longer important.  */
  remove_zero_width_bit_fields (t);

  /* Create the version of T used for virtual bases.  We do not use
     make_aggr_type for this version; this is an artificial type.  For
     a POD type, we just reuse T.  */
  if (CLASSTYPE_NON_POD_P (t) || CLASSTYPE_EMPTY_P (t))
    {
      base_t = make_node (TREE_CODE (t));

      /* Set the size and alignment for the new type.  In G++ 3.2, all
	 empty classes were considered to have size zero when used as
	 base classes.  */
      if (!abi_version_at_least (2) && CLASSTYPE_EMPTY_P (t))
	{
	  TYPE_SIZE (base_t) = bitsize_zero_node;
	  TYPE_SIZE_UNIT (base_t) = size_zero_node;
	  if (warn_abi && !integer_zerop (rli_size_unit_so_far (rli)))
	    warning (OPT_Wabi,
		     "layout of classes derived from empty class %qT "
		     "may change in a future version of GCC",
		     t);
	}
      else
	{
	  tree eoc;

	  /* If the ABI version is not at least two, and the last
	     field was a bit-field, RLI may not be on a byte
	     boundary.  In particular, rli_size_unit_so_far might
	     indicate the last complete byte, while rli_size_so_far
	     indicates the total number of bits used.  Therefore,
	     rli_size_so_far, rather than rli_size_unit_so_far, is
	     used to compute TYPE_SIZE_UNIT.  */
	  eoc = end_of_class (t, /*include_virtuals_p=*/0);
	  TYPE_SIZE_UNIT (base_t)
	    = size_binop (MAX_EXPR,
			  convert (sizetype,
				   size_binop (CEIL_DIV_EXPR,
					       rli_size_so_far (rli),
					       bitsize_int (BITS_PER_UNIT))),
			  eoc);
	  TYPE_SIZE (base_t)
	    = size_binop (MAX_EXPR,
			  rli_size_so_far (rli),
			  size_binop (MULT_EXPR,
				      convert (bitsizetype, eoc),
				      bitsize_int (BITS_PER_UNIT)));
	}
      TYPE_ALIGN (base_t) = rli->record_align;
      TYPE_USER_ALIGN (base_t) = TYPE_USER_ALIGN (t);

      /* Copy the fields from T.  */
      next_field = &TYPE_FIELDS (base_t);
      for (field = TYPE_FIELDS (t); field; field = TREE_CHAIN (field))
	if (TREE_CODE (field) == FIELD_DECL)
	  {
	    *next_field = build_decl (FIELD_DECL,
				      DECL_NAME (field),
				      TREE_TYPE (field));
	    DECL_CONTEXT (*next_field) = base_t;
	    DECL_FIELD_OFFSET (*next_field) = DECL_FIELD_OFFSET (field);
	    DECL_FIELD_BIT_OFFSET (*next_field)
	      = DECL_FIELD_BIT_OFFSET (field);
	    DECL_SIZE (*next_field) = DECL_SIZE (field);
	    DECL_MODE (*next_field) = DECL_MODE (field);
	    next_field = &TREE_CHAIN (*next_field);
	  }

      /* Record the base version of the type.  */
      CLASSTYPE_AS_BASE (t) = base_t;
      TYPE_CONTEXT (base_t) = t;
    }
  else
    CLASSTYPE_AS_BASE (t) = t;

  /* Every empty class contains an empty class.  */
  if (CLASSTYPE_EMPTY_P (t))
    CLASSTYPE_CONTAINS_EMPTY_CLASS_P (t) = 1;

  /* Set the TYPE_DECL for this type to contain the right
     value for DECL_OFFSET, so that we can use it as part
     of a COMPONENT_REF for multiple inheritance.  */
  layout_decl (TYPE_MAIN_DECL (t), 0);

  /* Now fix up any virtual base class types that we left lying
     around.  We must get these done before we try to lay out the
     virtual function table.  As a side-effect, this will remove the
     base subobject fields.  */
  layout_virtual_bases (rli, empty_base_offsets);

  /* Make sure that empty classes are reflected in RLI at this
     point.  */
  include_empty_classes(rli);

  /* Make sure not to create any structures with zero size.  */
  if (integer_zerop (rli_size_unit_so_far (rli)) && CLASSTYPE_EMPTY_P (t))
    place_field (rli,
		 build_decl (FIELD_DECL, NULL_TREE, char_type_node));

  /* Let the back-end lay out the type.  */
  finish_record_layout (rli, /*free_p=*/true);

  /* Warn about bases that can't be talked about due to ambiguity.  */
  warn_about_ambiguous_bases (t);

  /* Now that we're done with layout, give the base fields the real types.  */
  for (field = TYPE_FIELDS (t); field; field = TREE_CHAIN (field))
    if (DECL_ARTIFICIAL (field) && IS_FAKE_BASE_TYPE (TREE_TYPE (field)))
      TREE_TYPE (field) = TYPE_CONTEXT (TREE_TYPE (field));

  /* Clean up.  */
  splay_tree_delete (empty_base_offsets);

  if (CLASSTYPE_EMPTY_P (t)
      && tree_int_cst_lt (sizeof_biggest_empty_class,
			  TYPE_SIZE_UNIT (t)))
    sizeof_biggest_empty_class = TYPE_SIZE_UNIT (t);
}

/* Determine the "key method" for the class type indicated by TYPE,
   and set CLASSTYPE_KEY_METHOD accordingly.  */

void
determine_key_method (tree type)
{
  tree method;

  if (TYPE_FOR_JAVA (type)
      || processing_template_decl
      || CLASSTYPE_TEMPLATE_INSTANTIATION (type)
      || CLASSTYPE_INTERFACE_KNOWN (type))
    return;

  /* The key method is the first non-pure virtual function that is not
     inline at the point of class definition.  On some targets the
     key function may not be inline; those targets should not call
     this function until the end of the translation unit.  */
  for (method = TYPE_METHODS (type); method != NULL_TREE;
       method = TREE_CHAIN (method))
    if (DECL_VINDEX (method) != NULL_TREE
	&& ! DECL_DECLARED_INLINE_P (method)
	&& ! DECL_PURE_VIRTUAL_P (method))
      {
	CLASSTYPE_KEY_METHOD (type) = method;
	break;
      }

  return;
}

/* Perform processing required when the definition of T (a class type)
   is complete.  */

void
finish_struct_1 (tree t)
{
  tree x;
  /* A TREE_LIST.  The TREE_VALUE of each node is a FUNCTION_DECL.  */
  tree virtuals = NULL_TREE;
  int n_fields = 0;

  if (COMPLETE_TYPE_P (t))
    {
      gcc_assert (IS_AGGR_TYPE (t));
      error ("redefinition of %q#T", t);
      popclass ();
      return;
    }

  /* If this type was previously laid out as a forward reference,
     make sure we lay it out again.  */
  TYPE_SIZE (t) = NULL_TREE;
  CLASSTYPE_PRIMARY_BINFO (t) = NULL_TREE;

  fixup_inline_methods (t);

  /* Make assumptions about the class; we'll reset the flags if
     necessary.  */
  CLASSTYPE_EMPTY_P (t) = 1;
  CLASSTYPE_NEARLY_EMPTY_P (t) = 1;
  CLASSTYPE_CONTAINS_EMPTY_CLASS_P (t) = 0;

  /* Do end-of-class semantic processing: checking the validity of the
     bases and members and add implicitly generated methods.  */
  check_bases_and_members (t);

  /* Find the key method.  */
  if (TYPE_CONTAINS_VPTR_P (t))
    {
      /* The Itanium C++ ABI permits the key method to be chosen when
	 the class is defined -- even though the key method so
	 selected may later turn out to be an inline function.  On
	 some systems (such as ARM Symbian OS) the key method cannot
	 be determined until the end of the translation unit.  On such
	 systems, we leave CLASSTYPE_KEY_METHOD set to NULL, which
	 will cause the class to be added to KEYED_CLASSES.  Then, in
	 finish_file we will determine the key method.  */
      if (targetm.cxx.key_method_may_be_inline ())
	determine_key_method (t);

      /* If a polymorphic class has no key method, we may emit the vtable
	 in every translation unit where the class definition appears.  */
      if (CLASSTYPE_KEY_METHOD (t) == NULL_TREE)
	keyed_classes = tree_cons (NULL_TREE, t, keyed_classes);
    }

  /* Layout the class itself.  */
  layout_class_type (t, &virtuals);
  if (CLASSTYPE_AS_BASE (t) != t)
    /* We use the base type for trivial assignments, and hence it
       needs a mode.  */
    compute_record_mode (CLASSTYPE_AS_BASE (t));

  virtuals = modify_all_vtables (t, nreverse (virtuals));

  /* If necessary, create the primary vtable for this class.  */
  if (virtuals || TYPE_CONTAINS_VPTR_P (t))
    {
      /* We must enter these virtuals into the table.  */
      if (!CLASSTYPE_HAS_PRIMARY_BASE_P (t))
	build_primary_vtable (NULL_TREE, t);
      else if (! BINFO_NEW_VTABLE_MARKED (TYPE_BINFO (t)))
	/* Here we know enough to change the type of our virtual
	   function table, but we will wait until later this function.  */
	build_primary_vtable (CLASSTYPE_PRIMARY_BINFO (t), t);
    }

  if (TYPE_CONTAINS_VPTR_P (t))
    {
      int vindex;
      tree fn;

      if (BINFO_VTABLE (TYPE_BINFO (t)))
	gcc_assert (DECL_VIRTUAL_P (BINFO_VTABLE (TYPE_BINFO (t))));
      if (!CLASSTYPE_HAS_PRIMARY_BASE_P (t))
	gcc_assert (BINFO_VIRTUALS (TYPE_BINFO (t)) == NULL_TREE);

      /* Add entries for virtual functions introduced by this class.  */
      BINFO_VIRTUALS (TYPE_BINFO (t))
	= chainon (BINFO_VIRTUALS (TYPE_BINFO (t)), virtuals);

      /* Set DECL_VINDEX for all functions declared in this class.  */
      for (vindex = 0, fn = BINFO_VIRTUALS (TYPE_BINFO (t));
	   fn;
	   fn = TREE_CHAIN (fn),
	     vindex += (TARGET_VTABLE_USES_DESCRIPTORS
			? TARGET_VTABLE_USES_DESCRIPTORS : 1))
	{
	  tree fndecl = BV_FN (fn);

	  if (DECL_THUNK_P (fndecl))
	    /* A thunk. We should never be calling this entry directly
	       from this vtable -- we'd use the entry for the non
	       thunk base function.  */
	    DECL_VINDEX (fndecl) = NULL_TREE;
	  else if (TREE_CODE (DECL_VINDEX (fndecl)) != INTEGER_CST)
	    DECL_VINDEX (fndecl) = build_int_cst (NULL_TREE, vindex);
	}
    }

  finish_struct_bits (t);

  /* Complete the rtl for any static member objects of the type we're
     working on.  */
  for (x = TYPE_FIELDS (t); x; x = TREE_CHAIN (x))
    if (TREE_CODE (x) == VAR_DECL && TREE_STATIC (x)
        && TREE_TYPE (x) != error_mark_node
	&& same_type_p (TYPE_MAIN_VARIANT (TREE_TYPE (x)), t))
      DECL_MODE (x) = TYPE_MODE (t);

  /* Done with FIELDS...now decide whether to sort these for
     faster lookups later.

     We use a small number because most searches fail (succeeding
     ultimately as the search bores through the inheritance
     hierarchy), and we want this failure to occur quickly.  */

  n_fields = count_fields (TYPE_FIELDS (t));
  if (n_fields > 7)
    {
      struct sorted_fields_type *field_vec = GGC_NEWVAR
	 (struct sorted_fields_type,
	  sizeof (struct sorted_fields_type) + n_fields * sizeof (tree));
      field_vec->len = n_fields;
      add_fields_to_record_type (TYPE_FIELDS (t), field_vec, 0);
      qsort (field_vec->elts, n_fields, sizeof (tree),
	     field_decl_cmp);
      if (! DECL_LANG_SPECIFIC (TYPE_MAIN_DECL (t)))
	retrofit_lang_decl (TYPE_MAIN_DECL (t));
      DECL_SORTED_FIELDS (TYPE_MAIN_DECL (t)) = field_vec;
    }

  /* Complain if one of the field types requires lower visibility.  */
  constrain_class_visibility (t);

  /* Make the rtl for any new vtables we have created, and unmark
     the base types we marked.  */
  finish_vtbls (t);

  /* Build the VTT for T.  */
  build_vtt (t);

  /* This warning does not make sense for Java classes, since they
     cannot have destructors.  */
  if (!TYPE_FOR_JAVA (t) && warn_nonvdtor && TYPE_POLYMORPHIC_P (t))
    {
      tree dtor;

      dtor = CLASSTYPE_DESTRUCTORS (t);
      /* Warn only if the dtor is non-private or the class has
	 friends.  */
      if (/* An implicitly declared destructor is always public.  And,
	     if it were virtual, we would have created it by now.  */
	  !dtor
	  || (!DECL_VINDEX (dtor)
	      && (!TREE_PRIVATE (dtor)
		  || CLASSTYPE_FRIEND_CLASSES (t)
		  || DECL_FRIENDLIST (TYPE_MAIN_DECL (t)))))
	warning (0, "%q#T has virtual functions but non-virtual destructor",
		 t);
    }

  complete_vars (t);

  if (warn_overloaded_virtual)
    warn_hidden (t);

  /* Class layout, assignment of virtual table slots, etc., is now
     complete.  Give the back end a chance to tweak the visibility of
     the class or perform any other required target modifications.  */
  targetm.cxx.adjust_class_at_definition (t);

  maybe_suppress_debug_info (t);

  dump_class_hierarchy (t);

  /* Finish debugging output for this type.  */
  rest_of_type_compilation (t, ! LOCAL_CLASS_P (t));
}

/* When T was built up, the member declarations were added in reverse
   order.  Rearrange them to declaration order.  */

void
unreverse_member_declarations (tree t)
{
  tree next;
  tree prev;
  tree x;

  /* The following lists are all in reverse order.  Put them in
     declaration order now.  */
  TYPE_METHODS (t) = nreverse (TYPE_METHODS (t));
  CLASSTYPE_DECL_LIST (t) = nreverse (CLASSTYPE_DECL_LIST (t));

  /* Actually, for the TYPE_FIELDS, only the non TYPE_DECLs are in
     reverse order, so we can't just use nreverse.  */
  prev = NULL_TREE;
  for (x = TYPE_FIELDS (t);
       x && TREE_CODE (x) != TYPE_DECL;
       x = next)
    {
      next = TREE_CHAIN (x);
      TREE_CHAIN (x) = prev;
      prev = x;
    }
  if (prev)
    {
      TREE_CHAIN (TYPE_FIELDS (t)) = x;
      if (prev)
	TYPE_FIELDS (t) = prev;
    }
}

tree
finish_struct (tree t, tree attributes)
{
  location_t saved_loc = input_location;

  /* Now that we've got all the field declarations, reverse everything
     as necessary.  */
  unreverse_member_declarations (t);

  cplus_decl_attributes (&t, attributes, (int) ATTR_FLAG_TYPE_IN_PLACE);

  /* Nadger the current location so that diagnostics point to the start of
     the struct, not the end.  */
  input_location = DECL_SOURCE_LOCATION (TYPE_NAME (t));

  if (processing_template_decl)
    {
      tree x;

      finish_struct_methods (t);
      TYPE_SIZE (t) = bitsize_zero_node;
      TYPE_SIZE_UNIT (t) = size_zero_node;

      /* We need to emit an error message if this type was used as a parameter
	 and it is an abstract type, even if it is a template. We construct
	 a simple CLASSTYPE_PURE_VIRTUALS list without taking bases into
	 account and we call complete_vars with this type, which will check
	 the PARM_DECLS. Note that while the type is being defined,
	 CLASSTYPE_PURE_VIRTUALS contains the list of the inline friends
	 (see CLASSTYPE_INLINE_FRIENDS) so we need to clear it.  */
      CLASSTYPE_PURE_VIRTUALS (t) = NULL;
      for (x = TYPE_METHODS (t); x; x = TREE_CHAIN (x))
	if (DECL_PURE_VIRTUAL_P (x))
	  VEC_safe_push (tree, gc, CLASSTYPE_PURE_VIRTUALS (t), x);
      complete_vars (t);
    }
  else
    finish_struct_1 (t);

  input_location = saved_loc;

  TYPE_BEING_DEFINED (t) = 0;

  if (current_class_type)
    popclass ();
  else
    error ("trying to finish struct, but kicked out due to previous parse errors");

  if (processing_template_decl && at_function_scope_p ())
    add_stmt (build_min (TAG_DEFN, t));

  return t;
}

/* Return the dynamic type of INSTANCE, if known.
   Used to determine whether the virtual function table is needed
   or not.

   *NONNULL is set iff INSTANCE can be known to be nonnull, regardless
   of our knowledge of its type.  *NONNULL should be initialized
   before this function is called.  */

static tree
fixed_type_or_null (tree instance, int* nonnull, int* cdtorp)
{
  switch (TREE_CODE (instance))
    {
    case INDIRECT_REF:
      if (POINTER_TYPE_P (TREE_TYPE (instance)))
	return NULL_TREE;
      else
	return fixed_type_or_null (TREE_OPERAND (instance, 0),
				   nonnull, cdtorp);

    case CALL_EXPR:
      /* This is a call to a constructor, hence it's never zero.  */
      if (TREE_HAS_CONSTRUCTOR (instance))
	{
	  if (nonnull)
	    *nonnull = 1;
	  return TREE_TYPE (instance);
	}
      return NULL_TREE;

    case SAVE_EXPR:
      /* This is a call to a constructor, hence it's never zero.  */
      if (TREE_HAS_CONSTRUCTOR (instance))
	{
	  if (nonnull)
	    *nonnull = 1;
	  return TREE_TYPE (instance);
	}
      return fixed_type_or_null (TREE_OPERAND (instance, 0), nonnull, cdtorp);

    case PLUS_EXPR:
    case MINUS_EXPR:
      if (TREE_CODE (TREE_OPERAND (instance, 0)) == ADDR_EXPR)
	return fixed_type_or_null (TREE_OPERAND (instance, 0), nonnull, cdtorp);
      if (TREE_CODE (TREE_OPERAND (instance, 1)) == INTEGER_CST)
	/* Propagate nonnull.  */
	return fixed_type_or_null (TREE_OPERAND (instance, 0), nonnull, cdtorp);
      return NULL_TREE;

    case NOP_EXPR:
    case CONVERT_EXPR:
      return fixed_type_or_null (TREE_OPERAND (instance, 0), nonnull, cdtorp);

    case ADDR_EXPR:
      instance = TREE_OPERAND (instance, 0);
      if (nonnull)
	{
	  /* Just because we see an ADDR_EXPR doesn't mean we're dealing
	     with a real object -- given &p->f, p can still be null.  */
	  tree t = get_base_address (instance);
	  /* ??? Probably should check DECL_WEAK here.  */
	  if (t && DECL_P (t))
	    *nonnull = 1;
	}
      return fixed_type_or_null (instance, nonnull, cdtorp);

    case COMPONENT_REF:
      /* If this component is really a base class reference, then the field
	 itself isn't definitive.  */
      if (DECL_FIELD_IS_BASE (TREE_OPERAND (instance, 1)))
	return fixed_type_or_null (TREE_OPERAND (instance, 0), nonnull, cdtorp);
      return fixed_type_or_null (TREE_OPERAND (instance, 1), nonnull, cdtorp);

    case VAR_DECL:
    case FIELD_DECL:
      if (TREE_CODE (TREE_TYPE (instance)) == ARRAY_TYPE
	  && IS_AGGR_TYPE (TREE_TYPE (TREE_TYPE (instance))))
	{
	  if (nonnull)
	    *nonnull = 1;
	  return TREE_TYPE (TREE_TYPE (instance));
	}
      /* fall through...  */
    case TARGET_EXPR:
    case PARM_DECL:
    case RESULT_DECL:
      if (IS_AGGR_TYPE (TREE_TYPE (instance)))
	{
	  if (nonnull)
	    *nonnull = 1;
	  return TREE_TYPE (instance);
	}
      else if (instance == current_class_ptr)
	{
	  if (nonnull)
	    *nonnull = 1;

	  /* if we're in a ctor or dtor, we know our type.  */
	  if (DECL_LANG_SPECIFIC (current_function_decl)
	      && (DECL_CONSTRUCTOR_P (current_function_decl)
		  || DECL_DESTRUCTOR_P (current_function_decl)))
	    {
	      if (cdtorp)
		*cdtorp = 1;
	      return TREE_TYPE (TREE_TYPE (instance));
	    }
	}
      else if (TREE_CODE (TREE_TYPE (instance)) == REFERENCE_TYPE)
	{
	  /* We only need one hash table because it is always left empty.  */
	  static htab_t ht;
	  if (!ht)
	    ht = htab_create (37, 
			      htab_hash_pointer,
			      htab_eq_pointer,
			      /*htab_del=*/NULL);

	  /* Reference variables should be references to objects.  */
	  if (nonnull)
	    *nonnull = 1;

	  /* Enter the INSTANCE in a table to prevent recursion; a
	     variable's initializer may refer to the variable
	     itself.  */
	  if (TREE_CODE (instance) == VAR_DECL
	      && DECL_INITIAL (instance)
	      && !htab_find (ht, instance))
	    {
	      tree type;
	      void **slot;

	      slot = htab_find_slot (ht, instance, INSERT);
	      *slot = instance;
	      type = fixed_type_or_null (DECL_INITIAL (instance),
					 nonnull, cdtorp);
	      htab_remove_elt (ht, instance);

	      return type;
	    }
	}
      return NULL_TREE;

    default:
      return NULL_TREE;
    }
}

/* Return nonzero if the dynamic type of INSTANCE is known, and
   equivalent to the static type.  We also handle the case where
   INSTANCE is really a pointer. Return negative if this is a
   ctor/dtor. There the dynamic type is known, but this might not be
   the most derived base of the original object, and hence virtual
   bases may not be layed out according to this type.

   Used to determine whether the virtual function table is needed
   or not.

   *NONNULL is set iff INSTANCE can be known to be nonnull, regardless
   of our knowledge of its type.  *NONNULL should be initialized
   before this function is called.  */

int
resolves_to_fixed_type_p (tree instance, int* nonnull)
{
  tree t = TREE_TYPE (instance);
  int cdtorp = 0;

  tree fixed = fixed_type_or_null (instance, nonnull, &cdtorp);
  if (fixed == NULL_TREE)
    return 0;
  if (POINTER_TYPE_P (t))
    t = TREE_TYPE (t);
  if (!same_type_ignoring_top_level_qualifiers_p (t, fixed))
    return 0;
  return cdtorp ? -1 : 1;
}


void
init_class_processing (void)
{
  current_class_depth = 0;
  current_class_stack_size = 10;
  current_class_stack
    = XNEWVEC (struct class_stack_node, current_class_stack_size);
  local_classes = VEC_alloc (tree, gc, 8);
  sizeof_biggest_empty_class = size_zero_node;

  ridpointers[(int) RID_PUBLIC] = access_public_node;
  ridpointers[(int) RID_PRIVATE] = access_private_node;
  ridpointers[(int) RID_PROTECTED] = access_protected_node;
}

/* Restore the cached PREVIOUS_CLASS_LEVEL.  */

static void
restore_class_cache (void)
{
  tree type;

  /* We are re-entering the same class we just left, so we don't
     have to search the whole inheritance matrix to find all the
     decls to bind again.  Instead, we install the cached
     class_shadowed list and walk through it binding names.  */
  push_binding_level (previous_class_level);
  class_binding_level = previous_class_level;
  /* Restore IDENTIFIER_TYPE_VALUE.  */
  for (type = class_binding_level->type_shadowed;
       type;
       type = TREE_CHAIN (type))
    SET_IDENTIFIER_TYPE_VALUE (TREE_PURPOSE (type), TREE_TYPE (type));
}

/* Set global variables CURRENT_CLASS_NAME and CURRENT_CLASS_TYPE as
   appropriate for TYPE.

   So that we may avoid calls to lookup_name, we cache the _TYPE
   nodes of local TYPE_DECLs in the TREE_TYPE field of the name.

   For multiple inheritance, we perform a two-pass depth-first search
   of the type lattice.  */

void
pushclass (tree type)
{
  class_stack_node_t csn;

  type = TYPE_MAIN_VARIANT (type);

  /* Make sure there is enough room for the new entry on the stack.  */
  if (current_class_depth + 1 >= current_class_stack_size)
    {
      current_class_stack_size *= 2;
      current_class_stack
	= XRESIZEVEC (struct class_stack_node, current_class_stack,
		      current_class_stack_size);
    }

  /* Insert a new entry on the class stack.  */
  csn = current_class_stack + current_class_depth;
  csn->name = current_class_name;
  csn->type = current_class_type;
  csn->access = current_access_specifier;
  csn->names_used = 0;
  csn->hidden = 0;
  current_class_depth++;

  /* Now set up the new type.  */
  current_class_name = TYPE_NAME (type);
  if (TREE_CODE (current_class_name) == TYPE_DECL)
    current_class_name = DECL_NAME (current_class_name);
  current_class_type = type;

  /* By default, things in classes are private, while things in
     structures or unions are public.  */
  current_access_specifier = (CLASSTYPE_DECLARED_CLASS (type)
			      ? access_private_node
			      : access_public_node);

  if (previous_class_level
      && type != previous_class_level->this_entity
      && current_class_depth == 1)
    {
      /* Forcibly remove any old class remnants.  */
      invalidate_class_lookup_cache ();
    }

  if (!previous_class_level
      || type != previous_class_level->this_entity
      || current_class_depth > 1)
    pushlevel_class ();
  else
    restore_class_cache ();
}

/* When we exit a toplevel class scope, we save its binding level so
   that we can restore it quickly.  Here, we've entered some other
   class, so we must invalidate our cache.  */

void
invalidate_class_lookup_cache (void)
{
  previous_class_level = NULL;
}

/* Get out of the current class scope. If we were in a class scope
   previously, that is the one popped to.  */

void
popclass (void)
{
  poplevel_class ();

  current_class_depth--;
  current_class_name = current_class_stack[current_class_depth].name;
  current_class_type = current_class_stack[current_class_depth].type;
  current_access_specifier = current_class_stack[current_class_depth].access;
  if (current_class_stack[current_class_depth].names_used)
    splay_tree_delete (current_class_stack[current_class_depth].names_used);
}

/* Mark the top of the class stack as hidden.  */

void
push_class_stack (void)
{
  if (current_class_depth)
    ++current_class_stack[current_class_depth - 1].hidden;
}

/* Mark the top of the class stack as un-hidden.  */

void
pop_class_stack (void)
{
  if (current_class_depth)
    --current_class_stack[current_class_depth - 1].hidden;
}

/* Returns 1 if the class type currently being defined is either T or
   a nested type of T.  */

bool
currently_open_class (tree t)
{
  int i;

  /* We start looking from 1 because entry 0 is from global scope,
     and has no type.  */
  for (i = current_class_depth; i > 0; --i)
    {
      tree c;
      if (i == current_class_depth)
	c = current_class_type;
      else
	{
	  if (current_class_stack[i].hidden)
	    break;
	  c = current_class_stack[i].type;
	}
      if (!c)
	continue;
      if (same_type_p (c, t))
	return true;
    }
  return false;
}

/* If either current_class_type or one of its enclosing classes are derived
   from T, return the appropriate type.  Used to determine how we found
   something via unqualified lookup.  */

tree
currently_open_derived_class (tree t)
{
  int i;

  /* The bases of a dependent type are unknown.  */
  if (dependent_type_p (t))
    return NULL_TREE;

  if (!current_class_type)
    return NULL_TREE;

  if (DERIVED_FROM_P (t, current_class_type))
    return current_class_type;

  for (i = current_class_depth - 1; i > 0; --i)
    {
      if (current_class_stack[i].hidden)
	break;
      if (DERIVED_FROM_P (t, current_class_stack[i].type))
	return current_class_stack[i].type;
    }

  return NULL_TREE;
}

/* When entering a class scope, all enclosing class scopes' names with
   static meaning (static variables, static functions, types and
   enumerators) have to be visible.  This recursive function calls
   pushclass for all enclosing class contexts until global or a local
   scope is reached.  TYPE is the enclosed class.  */

void
push_nested_class (tree type)
{
  tree context;

  /* A namespace might be passed in error cases, like A::B:C.  */
  if (type == NULL_TREE
      || type == error_mark_node
      || TREE_CODE (type) == NAMESPACE_DECL
      || ! IS_AGGR_TYPE (type)
      || TREE_CODE (type) == TEMPLATE_TYPE_PARM
      || TREE_CODE (type) == BOUND_TEMPLATE_TEMPLATE_PARM)
    return;

  context = DECL_CONTEXT (TYPE_MAIN_DECL (type));

  if (context && CLASS_TYPE_P (context))
    push_nested_class (context);
  pushclass (type);
}

/* Undoes a push_nested_class call.  */

void
pop_nested_class (void)
{
  tree context = DECL_CONTEXT (TYPE_MAIN_DECL (current_class_type));

  popclass ();
  if (context && CLASS_TYPE_P (context))
    pop_nested_class ();
}

/* Returns the number of extern "LANG" blocks we are nested within.  */

int
current_lang_depth (void)
{
  return VEC_length (tree, current_lang_base);
}

/* Set global variables CURRENT_LANG_NAME to appropriate value
   so that behavior of name-mangling machinery is correct.  */

void
push_lang_context (tree name)
{
  VEC_safe_push (tree, gc, current_lang_base, current_lang_name);

  if (name == lang_name_cplusplus)
    {
      current_lang_name = name;
    }
  else if (name == lang_name_java)
    {
      current_lang_name = name;
      /* DECL_IGNORED_P is initially set for these types, to avoid clutter.
	 (See record_builtin_java_type in decl.c.)  However, that causes
	 incorrect debug entries if these types are actually used.
	 So we re-enable debug output after extern "Java".  */
      DECL_IGNORED_P (TYPE_NAME (java_byte_type_node)) = 0;
      DECL_IGNORED_P (TYPE_NAME (java_short_type_node)) = 0;
      DECL_IGNORED_P (TYPE_NAME (java_int_type_node)) = 0;
      DECL_IGNORED_P (TYPE_NAME (java_long_type_node)) = 0;
      DECL_IGNORED_P (TYPE_NAME (java_float_type_node)) = 0;
      DECL_IGNORED_P (TYPE_NAME (java_double_type_node)) = 0;
      DECL_IGNORED_P (TYPE_NAME (java_char_type_node)) = 0;
      DECL_IGNORED_P (TYPE_NAME (java_boolean_type_node)) = 0;
    }
  else if (name == lang_name_c)
    {
      current_lang_name = name;
    }
  else
    error ("language string %<\"%E\"%> not recognized", name);
}

/* Get out of the current language scope.  */

void
pop_lang_context (void)
{
  current_lang_name = VEC_pop (tree, current_lang_base);
}

/* Type instantiation routines.  */

/* Given an OVERLOAD and a TARGET_TYPE, return the function that
   matches the TARGET_TYPE.  If there is no satisfactory match, return
   error_mark_node, and issue an error & warning messages under
   control of FLAGS.  Permit pointers to member function if FLAGS
   permits.  If TEMPLATE_ONLY, the name of the overloaded function was
   a template-id, and EXPLICIT_TARGS are the explicitly provided
   template arguments.  If OVERLOAD is for one or more member
   functions, then ACCESS_PATH is the base path used to reference
   those member functions.  */

static tree
resolve_address_of_overloaded_function (tree target_type,
					tree overload,
					tsubst_flags_t flags,
					bool template_only,
					tree explicit_targs,
					tree access_path)
{
  /* Here's what the standard says:

       [over.over]

       If the name is a function template, template argument deduction
       is done, and if the argument deduction succeeds, the deduced
       arguments are used to generate a single template function, which
       is added to the set of overloaded functions considered.

       Non-member functions and static member functions match targets of
       type "pointer-to-function" or "reference-to-function."  Nonstatic
       member functions match targets of type "pointer-to-member
       function;" the function type of the pointer to member is used to
       select the member function from the set of overloaded member
       functions.  If a nonstatic member function is selected, the
       reference to the overloaded function name is required to have the
       form of a pointer to member as described in 5.3.1.

       If more than one function is selected, any template functions in
       the set are eliminated if the set also contains a non-template
       function, and any given template function is eliminated if the
       set contains a second template function that is more specialized
       than the first according to the partial ordering rules 14.5.5.2.
       After such eliminations, if any, there shall remain exactly one
       selected function.  */

  int is_ptrmem = 0;
  int is_reference = 0;
  /* We store the matches in a TREE_LIST rooted here.  The functions
     are the TREE_PURPOSE, not the TREE_VALUE, in this list, for easy
     interoperability with most_specialized_instantiation.  */
  tree matches = NULL_TREE;
  tree fn;

  /* By the time we get here, we should be seeing only real
     pointer-to-member types, not the internal POINTER_TYPE to
     METHOD_TYPE representation.  */
  gcc_assert (TREE_CODE (target_type) != POINTER_TYPE
	      || TREE_CODE (TREE_TYPE (target_type)) != METHOD_TYPE);

  gcc_assert (is_overloaded_fn (overload));

  /* Check that the TARGET_TYPE is reasonable.  */
  if (TYPE_PTRFN_P (target_type))
    /* This is OK.  */;
  else if (TYPE_PTRMEMFUNC_P (target_type))
    /* This is OK, too.  */
    is_ptrmem = 1;
  else if (TREE_CODE (target_type) == FUNCTION_TYPE)
    {
      /* This is OK, too.  This comes from a conversion to reference
	 type.  */
      target_type = build_reference_type (target_type);
      is_reference = 1;
    }
  else
    {
      if (flags & tf_error)
	error ("cannot resolve overloaded function %qD based on"
	       " conversion to type %qT",
	       DECL_NAME (OVL_FUNCTION (overload)), target_type);
      return error_mark_node;
    }

  /* If we can find a non-template function that matches, we can just
     use it.  There's no point in generating template instantiations
     if we're just going to throw them out anyhow.  But, of course, we
     can only do this when we don't *need* a template function.  */
  if (!template_only)
    {
      tree fns;

      for (fns = overload; fns; fns = OVL_NEXT (fns))
	{
	  tree fn = OVL_CURRENT (fns);
	  tree fntype;

	  if (TREE_CODE (fn) == TEMPLATE_DECL)
	    /* We're not looking for templates just yet.  */
	    continue;

	  if ((TREE_CODE (TREE_TYPE (fn)) == METHOD_TYPE)
	      != is_ptrmem)
	    /* We're looking for a non-static member, and this isn't
	       one, or vice versa.  */
	    continue;

	  /* Ignore functions which haven't been explicitly
	     declared.  */
	  if (DECL_ANTICIPATED (fn))
	    continue;

	  /* See if there's a match.  */
	  fntype = TREE_TYPE (fn);
	  if (is_ptrmem)
	    fntype = build_ptrmemfunc_type (build_pointer_type (fntype));
	  else if (!is_reference)
	    fntype = build_pointer_type (fntype);

	  if (can_convert_arg (target_type, fntype, fn, LOOKUP_NORMAL))
	    matches = tree_cons (fn, NULL_TREE, matches);
	}
    }

  /* Now, if we've already got a match (or matches), there's no need
     to proceed to the template functions.  But, if we don't have a
     match we need to look at them, too.  */
  if (!matches)
    {
      tree target_fn_type;
      tree target_arg_types;
      tree target_ret_type;
      tree fns;

      if (is_ptrmem)
	target_fn_type
	  = TREE_TYPE (TYPE_PTRMEMFUNC_FN_TYPE (target_type));
      else
	target_fn_type = TREE_TYPE (target_type);
      target_arg_types = TYPE_ARG_TYPES (target_fn_type);
      target_ret_type = TREE_TYPE (target_fn_type);

      /* Never do unification on the 'this' parameter.  */
      if (TREE_CODE (target_fn_type) == METHOD_TYPE)
	target_arg_types = TREE_CHAIN (target_arg_types);

      for (fns = overload; fns; fns = OVL_NEXT (fns))
	{
	  tree fn = OVL_CURRENT (fns);
	  tree instantiation;
	  tree instantiation_type;
	  tree targs;

	  if (TREE_CODE (fn) != TEMPLATE_DECL)
	    /* We're only looking for templates.  */
	    continue;

	  if ((TREE_CODE (TREE_TYPE (fn)) == METHOD_TYPE)
	      != is_ptrmem)
	    /* We're not looking for a non-static member, and this is
	       one, or vice versa.  */
	    continue;

	  /* Try to do argument deduction.  */
	  targs = make_tree_vec (DECL_NTPARMS (fn));
	  if (fn_type_unification (fn, explicit_targs, targs,
				   target_arg_types, target_ret_type,
				   DEDUCE_EXACT, LOOKUP_NORMAL))
	    /* Argument deduction failed.  */
	    continue;

	  /* Instantiate the template.  */
	  instantiation = instantiate_template (fn, targs, flags);
	  if (instantiation == error_mark_node)
	    /* Instantiation failed.  */
	    continue;

	  /* See if there's a match.  */
	  instantiation_type = TREE_TYPE (instantiation);
	  if (is_ptrmem)
	    instantiation_type =
	      build_ptrmemfunc_type (build_pointer_type (instantiation_type));
	  else if (!is_reference)
	    instantiation_type = build_pointer_type (instantiation_type);
	  if (can_convert_arg (target_type, instantiation_type, instantiation,
			       LOOKUP_NORMAL))
	    matches = tree_cons (instantiation, fn, matches);
	}

      /* Now, remove all but the most specialized of the matches.  */
      if (matches)
	{
	  tree match = most_specialized_instantiation (matches);

	  if (match != error_mark_node)
	    matches = tree_cons (TREE_PURPOSE (match),
				 NULL_TREE,
				 NULL_TREE);
	}
    }

  /* Now we should have exactly one function in MATCHES.  */
  if (matches == NULL_TREE)
    {
      /* There were *no* matches.  */
      if (flags & tf_error)
	{
	  error ("no matches converting function %qD to type %q#T",
		 DECL_NAME (OVL_FUNCTION (overload)),
		 target_type);

	  /* print_candidates expects a chain with the functions in
	     TREE_VALUE slots, so we cons one up here (we're losing anyway,
	     so why be clever?).  */
	  for (; overload; overload = OVL_NEXT (overload))
	    matches = tree_cons (NULL_TREE, OVL_CURRENT (overload),
				 matches);

	  print_candidates (matches);
	}
      return error_mark_node;
    }
  else if (TREE_CHAIN (matches))
    {
      /* There were too many matches.  */

      if (flags & tf_error)
	{
	  tree match;

	  error ("converting overloaded function %qD to type %q#T is ambiguous",
		    DECL_NAME (OVL_FUNCTION (overload)),
		    target_type);

	  /* Since print_candidates expects the functions in the
	     TREE_VALUE slot, we flip them here.  */
	  for (match = matches; match; match = TREE_CHAIN (match))
	    TREE_VALUE (match) = TREE_PURPOSE (match);

	  print_candidates (matches);
	}

      return error_mark_node;
    }

  /* Good, exactly one match.  Now, convert it to the correct type.  */
  fn = TREE_PURPOSE (matches);

  if (DECL_NONSTATIC_MEMBER_FUNCTION_P (fn)
      && !(flags & tf_ptrmem_ok) && !flag_ms_extensions)
    {
      static int explained;

      if (!(flags & tf_error))
	return error_mark_node;

      pedwarn ("assuming pointer to member %qD", fn);
      if (!explained)
	{
	  pedwarn ("(a pointer to member can only be formed with %<&%E%>)", fn);
	  explained = 1;
	}
    }

  /* If we're doing overload resolution purely for the purpose of
     determining conversion sequences, we should not consider the
     function used.  If this conversion sequence is selected, the
     function will be marked as used at this point.  */
  if (!(flags & tf_conv))
    {
      mark_used (fn);
      /* We could not check access when this expression was originally
	 created since we did not know at that time to which function
	 the expression referred.  */
      if (DECL_FUNCTION_MEMBER_P (fn))
	{
	  gcc_assert (access_path);
	  perform_or_defer_access_check (access_path, fn, fn);
	}
    }

  if (TYPE_PTRFN_P (target_type) || TYPE_PTRMEMFUNC_P (target_type))
    return build_unary_op (ADDR_EXPR, fn, 0);
  else
    {
      /* The target must be a REFERENCE_TYPE.  Above, build_unary_op
	 will mark the function as addressed, but here we must do it
	 explicitly.  */
      cxx_mark_addressable (fn);

      return fn;
    }
}

/* This function will instantiate the type of the expression given in
   RHS to match the type of LHSTYPE.  If errors exist, then return
   error_mark_node. FLAGS is a bit mask.  If TF_ERROR is set, then
   we complain on errors.  If we are not complaining, never modify rhs,
   as overload resolution wants to try many possible instantiations, in
   the hope that at least one will work.

   For non-recursive calls, LHSTYPE should be a function, pointer to
   function, or a pointer to member function.  */

tree
instantiate_type (tree lhstype, tree rhs, tsubst_flags_t flags)
{
  tsubst_flags_t flags_in = flags;
  tree access_path = NULL_TREE;

  flags &= ~tf_ptrmem_ok;

  if (TREE_CODE (lhstype) == UNKNOWN_TYPE)
    {
      if (flags & tf_error)
	error ("not enough type information");
      return error_mark_node;
    }

  if (TREE_TYPE (rhs) != NULL_TREE && ! (type_unknown_p (rhs)))
    {
      if (same_type_p (lhstype, TREE_TYPE (rhs)))
	return rhs;
      if (flag_ms_extensions
	  && TYPE_PTRMEMFUNC_P (lhstype)
	  && !TYPE_PTRMEMFUNC_P (TREE_TYPE (rhs)))
	/* Microsoft allows `A::f' to be resolved to a
	   pointer-to-member.  */
	;
      else
	{
	  if (flags & tf_error)
	    error ("argument of type %qT does not match %qT",
		   TREE_TYPE (rhs), lhstype);
	  return error_mark_node;
	}
    }

  if (TREE_CODE (rhs) == BASELINK)
    {
      access_path = BASELINK_ACCESS_BINFO (rhs);
      rhs = BASELINK_FUNCTIONS (rhs);
    }

  /* If we are in a template, and have a NON_DEPENDENT_EXPR, we cannot
     deduce any type information.  */
  if (TREE_CODE (rhs) == NON_DEPENDENT_EXPR)
    {
      if (flags & tf_error)
	error ("not enough type information");
      return error_mark_node;
    }

  /* There only a few kinds of expressions that may have a type
     dependent on overload resolution.  */
  gcc_assert (TREE_CODE (rhs) == ADDR_EXPR
	      || TREE_CODE (rhs) == COMPONENT_REF
	      || TREE_CODE (rhs) == COMPOUND_EXPR
	      || really_overloaded_fn (rhs));

  /* We don't overwrite rhs if it is an overloaded function.
     Copying it would destroy the tree link.  */
  if (TREE_CODE (rhs) != OVERLOAD)
    rhs = copy_node (rhs);

  /* This should really only be used when attempting to distinguish
     what sort of a pointer to function we have.  For now, any
     arithmetic operation which is not supported on pointers
     is rejected as an error.  */

  switch (TREE_CODE (rhs))
    {
    case COMPONENT_REF:
      {
	tree member = TREE_OPERAND (rhs, 1);

	member = instantiate_type (lhstype, member, flags);
	if (member != error_mark_node
	    && TREE_SIDE_EFFECTS (TREE_OPERAND (rhs, 0)))
	  /* Do not lose object's side effects.  */
	  return build2 (COMPOUND_EXPR, TREE_TYPE (member),
			 TREE_OPERAND (rhs, 0), member);
	return member;
      }

    case OFFSET_REF:
      rhs = TREE_OPERAND (rhs, 1);
      if (BASELINK_P (rhs))
	return instantiate_type (lhstype, rhs, flags_in);

      /* This can happen if we are forming a pointer-to-member for a
	 member template.  */
      gcc_assert (TREE_CODE (rhs) == TEMPLATE_ID_EXPR);

      /* Fall through.  */

    case TEMPLATE_ID_EXPR:
      {
	tree fns = TREE_OPERAND (rhs, 0);
	tree args = TREE_OPERAND (rhs, 1);

	return
	  resolve_address_of_overloaded_function (lhstype, fns, flags_in,
						  /*template_only=*/true,
						  args, access_path);
      }

    case OVERLOAD:
    case FUNCTION_DECL:
      return
	resolve_address_of_overloaded_function (lhstype, rhs, flags_in,
						/*template_only=*/false,
						/*explicit_targs=*/NULL_TREE,
						access_path);

    case COMPOUND_EXPR:
      TREE_OPERAND (rhs, 0)
	= instantiate_type (lhstype, TREE_OPERAND (rhs, 0), flags);
      if (TREE_OPERAND (rhs, 0) == error_mark_node)
	return error_mark_node;
      TREE_OPERAND (rhs, 1)
	= instantiate_type (lhstype, TREE_OPERAND (rhs, 1), flags);
      if (TREE_OPERAND (rhs, 1) == error_mark_node)
	return error_mark_node;

      TREE_TYPE (rhs) = lhstype;
      return rhs;

    case ADDR_EXPR:
    {
      if (PTRMEM_OK_P (rhs))
	flags |= tf_ptrmem_ok;

      return instantiate_type (lhstype, TREE_OPERAND (rhs, 0), flags);
    }

    case ERROR_MARK:
      return error_mark_node;

    default:
      gcc_unreachable ();
    }
  return error_mark_node;
}

/* Return the name of the virtual function pointer field
   (as an IDENTIFIER_NODE) for the given TYPE.  Note that
   this may have to look back through base types to find the
   ultimate field name.  (For single inheritance, these could
   all be the same name.  Who knows for multiple inheritance).  */

static tree
get_vfield_name (tree type)
{
  tree binfo, base_binfo;
  char *buf;

  for (binfo = TYPE_BINFO (type);
       BINFO_N_BASE_BINFOS (binfo);
       binfo = base_binfo)
    {
      base_binfo = BINFO_BASE_BINFO (binfo, 0);

      if (BINFO_VIRTUAL_P (base_binfo)
	  || !TYPE_CONTAINS_VPTR_P (BINFO_TYPE (base_binfo)))
	break;
    }

  type = BINFO_TYPE (binfo);
  buf = (char *) alloca (sizeof (VFIELD_NAME_FORMAT)
			 + TYPE_NAME_LENGTH (type) + 2);
  sprintf (buf, VFIELD_NAME_FORMAT,
	   IDENTIFIER_POINTER (constructor_name (type)));
  return get_identifier (buf);
}

void
print_class_statistics (void)
{
#ifdef GATHER_STATISTICS
  fprintf (stderr, "convert_harshness = %d\n", n_convert_harshness);
  fprintf (stderr, "compute_conversion_costs = %d\n", n_compute_conversion_costs);
  if (n_vtables)
    {
      fprintf (stderr, "vtables = %d; vtable searches = %d\n",
	       n_vtables, n_vtable_searches);
      fprintf (stderr, "vtable entries = %d; vtable elems = %d\n",
	       n_vtable_entries, n_vtable_elems);
    }
#endif
}

/* Build a dummy reference to ourselves so Derived::Base (and A::A) works,
   according to [class]:
					  The class-name is also inserted
   into  the scope of the class itself.  For purposes of access checking,
   the inserted class name is treated as if it were a public member name.  */

void
build_self_reference (void)
{
  tree name = constructor_name (current_class_type);
  tree value = build_lang_decl (TYPE_DECL, name, current_class_type);
  tree saved_cas;

  DECL_NONLOCAL (value) = 1;
  DECL_CONTEXT (value) = current_class_type;
  DECL_ARTIFICIAL (value) = 1;
  SET_DECL_SELF_REFERENCE_P (value);

  if (processing_template_decl)
    value = push_template_decl (value);

  saved_cas = current_access_specifier;
  current_access_specifier = access_public_node;
  finish_member_declaration (value);
  current_access_specifier = saved_cas;
}

/* Returns 1 if TYPE contains only padding bytes.  */

int
is_empty_class (tree type)
{
  if (type == error_mark_node)
    return 0;

  if (! IS_AGGR_TYPE (type))
    return 0;

  /* In G++ 3.2, whether or not a class was empty was determined by
     looking at its size.  */
  if (abi_version_at_least (2))
    return CLASSTYPE_EMPTY_P (type);
  else
    return integer_zerop (CLASSTYPE_SIZE (type));
}

/* Returns true if TYPE contains an empty class.  */

static bool
contains_empty_class_p (tree type)
{
  if (is_empty_class (type))
    return true;
  if (CLASS_TYPE_P (type))
    {
      tree field;
      tree binfo;
      tree base_binfo;
      int i;

      for (binfo = TYPE_BINFO (type), i = 0;
	   BINFO_BASE_ITERATE (binfo, i, base_binfo); ++i)
	if (contains_empty_class_p (BINFO_TYPE (base_binfo)))
	  return true;
      for (field = TYPE_FIELDS (type); field; field = TREE_CHAIN (field))
	if (TREE_CODE (field) == FIELD_DECL
	    && !DECL_ARTIFICIAL (field)
	    && is_empty_class (TREE_TYPE (field)))
	  return true;
    }
  else if (TREE_CODE (type) == ARRAY_TYPE)
    return contains_empty_class_p (TREE_TYPE (type));
  return false;
}

/* Note that NAME was looked up while the current class was being
   defined and that the result of that lookup was DECL.  */

void
maybe_note_name_used_in_class (tree name, tree decl)
{
  splay_tree names_used;

  /* If we're not defining a class, there's nothing to do.  */
  if (!(innermost_scope_kind() == sk_class
	&& TYPE_BEING_DEFINED (current_class_type)))
    return;

  /* If there's already a binding for this NAME, then we don't have
     anything to worry about.  */
  if (lookup_member (current_class_type, name,
		     /*protect=*/0, /*want_type=*/false))
    return;

  if (!current_class_stack[current_class_depth - 1].names_used)
    current_class_stack[current_class_depth - 1].names_used
      = splay_tree_new (splay_tree_compare_pointers, 0, 0);
  names_used = current_class_stack[current_class_depth - 1].names_used;

  splay_tree_insert (names_used,
		     (splay_tree_key) name,
		     (splay_tree_value) decl);
}

/* Note that NAME was declared (as DECL) in the current class.  Check
   to see that the declaration is valid.  */

void
note_name_declared_in_class (tree name, tree decl)
{
  splay_tree names_used;
  splay_tree_node n;

  /* Look to see if we ever used this name.  */
  names_used
    = current_class_stack[current_class_depth - 1].names_used;
  if (!names_used)
    return;

  n = splay_tree_lookup (names_used, (splay_tree_key) name);
  if (n)
    {
      /* [basic.scope.class]

	 A name N used in a class S shall refer to the same declaration
	 in its context and when re-evaluated in the completed scope of
	 S.  */
      error ("declaration of %q#D", decl);
      error ("changes meaning of %qD from %q+#D",
	     DECL_NAME (OVL_CURRENT (decl)), (tree) n->value);
    }
}

/* Returns the VAR_DECL for the complete vtable associated with BINFO.
   Secondary vtables are merged with primary vtables; this function
   will return the VAR_DECL for the primary vtable.  */

tree
get_vtbl_decl_for_binfo (tree binfo)
{
  tree decl;

  decl = BINFO_VTABLE (binfo);
  if (decl && TREE_CODE (decl) == PLUS_EXPR)
    {
      gcc_assert (TREE_CODE (TREE_OPERAND (decl, 0)) == ADDR_EXPR);
      decl = TREE_OPERAND (TREE_OPERAND (decl, 0), 0);
    }
  if (decl)
    gcc_assert (TREE_CODE (decl) == VAR_DECL);
  return decl;
}


/* Returns the binfo for the primary base of BINFO.  If the resulting
   BINFO is a virtual base, and it is inherited elsewhere in the
   hierarchy, then the returned binfo might not be the primary base of
   BINFO in the complete object.  Check BINFO_PRIMARY_P or
   BINFO_LOST_PRIMARY_P to be sure.  */

static tree
get_primary_binfo (tree binfo)
{
  tree primary_base;

  primary_base = CLASSTYPE_PRIMARY_BINFO (BINFO_TYPE (binfo));
  if (!primary_base)
    return NULL_TREE;

  return copied_binfo (primary_base, binfo);
}

/* If INDENTED_P is zero, indent to INDENT. Return nonzero.  */

static int
maybe_indent_hierarchy (FILE * stream, int indent, int indented_p)
{
  if (!indented_p)
    fprintf (stream, "%*s", indent, "");
  return 1;
}

/* Dump the offsets of all the bases rooted at BINFO to STREAM.
   INDENT should be zero when called from the top level; it is
   incremented recursively.  IGO indicates the next expected BINFO in
   inheritance graph ordering.  */

static tree
dump_class_hierarchy_r (FILE *stream,
			int flags,
			tree binfo,
			tree igo,
			int indent)
{
  int indented = 0;
  tree base_binfo;
  int i;

  indented = maybe_indent_hierarchy (stream, indent, 0);
  fprintf (stream, "%s (0x%lx) ",
	   type_as_string (BINFO_TYPE (binfo), TFF_PLAIN_IDENTIFIER),
	   (unsigned long) binfo);
  if (binfo != igo)
    {
      fprintf (stream, "alternative-path\n");
      return igo;
    }
  igo = TREE_CHAIN (binfo);

  fprintf (stream, HOST_WIDE_INT_PRINT_DEC,
	   tree_low_cst (BINFO_OFFSET (binfo), 0));
  if (is_empty_class (BINFO_TYPE (binfo)))
    fprintf (stream, " empty");
  else if (CLASSTYPE_NEARLY_EMPTY_P (BINFO_TYPE (binfo)))
    fprintf (stream, " nearly-empty");
  if (BINFO_VIRTUAL_P (binfo))
    fprintf (stream, " virtual");
  fprintf (stream, "\n");

  indented = 0;
  if (BINFO_PRIMARY_P (binfo))
    {
      indented = maybe_indent_hierarchy (stream, indent + 3, indented);
      fprintf (stream, " primary-for %s (0x%lx)",
	       type_as_string (BINFO_TYPE (BINFO_INHERITANCE_CHAIN (binfo)),
			       TFF_PLAIN_IDENTIFIER),
	       (unsigned long)BINFO_INHERITANCE_CHAIN (binfo));
    }
  if (BINFO_LOST_PRIMARY_P (binfo))
    {
      indented = maybe_indent_hierarchy (stream, indent + 3, indented);
      fprintf (stream, " lost-primary");
    }
  if (indented)
    fprintf (stream, "\n");

  if (!(flags & TDF_SLIM))
    {
      int indented = 0;

      if (BINFO_SUBVTT_INDEX (binfo))
	{
	  indented = maybe_indent_hierarchy (stream, indent + 3, indented);
	  fprintf (stream, " subvttidx=%s",
		   expr_as_string (BINFO_SUBVTT_INDEX (binfo),
				   TFF_PLAIN_IDENTIFIER));
	}
      if (BINFO_VPTR_INDEX (binfo))
	{
	  indented = maybe_indent_hierarchy (stream, indent + 3, indented);
	  fprintf (stream, " vptridx=%s",
		   expr_as_string (BINFO_VPTR_INDEX (binfo),
				   TFF_PLAIN_IDENTIFIER));
	}
      if (BINFO_VPTR_FIELD (binfo))
	{
	  indented = maybe_indent_hierarchy (stream, indent + 3, indented);
	  fprintf (stream, " vbaseoffset=%s",
		   expr_as_string (BINFO_VPTR_FIELD (binfo),
				   TFF_PLAIN_IDENTIFIER));
	}
      if (BINFO_VTABLE (binfo))
	{
	  indented = maybe_indent_hierarchy (stream, indent + 3, indented);
	  fprintf (stream, " vptr=%s",
		   expr_as_string (BINFO_VTABLE (binfo),
				   TFF_PLAIN_IDENTIFIER));
	}

      if (indented)
	fprintf (stream, "\n");
    }

  for (i = 0; BINFO_BASE_ITERATE (binfo, i, base_binfo); i++)
    igo = dump_class_hierarchy_r (stream, flags, base_binfo, igo, indent + 2);

  return igo;
}

/* Dump the BINFO hierarchy for T.  */

static void
dump_class_hierarchy_1 (FILE *stream, int flags, tree t)
{
  fprintf (stream, "Class %s\n", type_as_string (t, TFF_PLAIN_IDENTIFIER));
  fprintf (stream, "   size=%lu align=%lu\n",
	   (unsigned long)(tree_low_cst (TYPE_SIZE (t), 0) / BITS_PER_UNIT),
	   (unsigned long)(TYPE_ALIGN (t) / BITS_PER_UNIT));
  fprintf (stream, "   base size=%lu base align=%lu\n",
	   (unsigned long)(tree_low_cst (TYPE_SIZE (CLASSTYPE_AS_BASE (t)), 0)
			   / BITS_PER_UNIT),
	   (unsigned long)(TYPE_ALIGN (CLASSTYPE_AS_BASE (t))
			   / BITS_PER_UNIT));
  dump_class_hierarchy_r (stream, flags, TYPE_BINFO (t), TYPE_BINFO (t), 0);
  fprintf (stream, "\n");
}

/* Debug interface to hierarchy dumping.  */

void
debug_class (tree t)
{
  dump_class_hierarchy_1 (stderr, TDF_SLIM, t);
}

static void
dump_class_hierarchy (tree t)
{
  int flags;
  FILE *stream = dump_begin (TDI_class, &flags);

  if (stream)
    {
      dump_class_hierarchy_1 (stream, flags, t);
      dump_end (TDI_class, stream);
    }
}

static void
dump_array (FILE * stream, tree decl)
{
  tree value;
  unsigned HOST_WIDE_INT ix;
  HOST_WIDE_INT elt;
  tree size = TYPE_MAX_VALUE (TYPE_DOMAIN (TREE_TYPE (decl)));

  elt = (tree_low_cst (TYPE_SIZE (TREE_TYPE (TREE_TYPE (decl))), 0)
	 / BITS_PER_UNIT);
  fprintf (stream, "%s:", decl_as_string (decl, TFF_PLAIN_IDENTIFIER));
  fprintf (stream, " %s entries",
	   expr_as_string (size_binop (PLUS_EXPR, size, size_one_node),
			   TFF_PLAIN_IDENTIFIER));
  fprintf (stream, "\n");

  FOR_EACH_CONSTRUCTOR_VALUE (CONSTRUCTOR_ELTS (DECL_INITIAL (decl)),
			      ix, value)
    fprintf (stream, "%-4ld  %s\n", (long)(ix * elt),
	     expr_as_string (value, TFF_PLAIN_IDENTIFIER));
}

static void
dump_vtable (tree t, tree binfo, tree vtable)
{
  int flags;
  FILE *stream = dump_begin (TDI_class, &flags);

  if (!stream)
    return;

  if (!(flags & TDF_SLIM))
    {
      int ctor_vtbl_p = TYPE_BINFO (t) != binfo;

      fprintf (stream, "%s for %s",
	       ctor_vtbl_p ? "Construction vtable" : "Vtable",
	       type_as_string (BINFO_TYPE (binfo), TFF_PLAIN_IDENTIFIER));
      if (ctor_vtbl_p)
	{
	  if (!BINFO_VIRTUAL_P (binfo))
	    fprintf (stream, " (0x%lx instance)", (unsigned long)binfo);
	  fprintf (stream, " in %s", type_as_string (t, TFF_PLAIN_IDENTIFIER));
	}
      fprintf (stream, "\n");
      dump_array (stream, vtable);
      fprintf (stream, "\n");
    }

  dump_end (TDI_class, stream);
}

static void
dump_vtt (tree t, tree vtt)
{
  int flags;
  FILE *stream = dump_begin (TDI_class, &flags);

  if (!stream)
    return;

  if (!(flags & TDF_SLIM))
    {
      fprintf (stream, "VTT for %s\n",
	       type_as_string (t, TFF_PLAIN_IDENTIFIER));
      dump_array (stream, vtt);
      fprintf (stream, "\n");
    }

  dump_end (TDI_class, stream);
}

/* Dump a function or thunk and its thunkees.  */

static void
dump_thunk (FILE *stream, int indent, tree thunk)
{
  static const char spaces[] = "        ";
  tree name = DECL_NAME (thunk);
  tree thunks;

  fprintf (stream, "%.*s%p %s %s", indent, spaces,
	   (void *)thunk,
	   !DECL_THUNK_P (thunk) ? "function"
	   : DECL_THIS_THUNK_P (thunk) ? "this-thunk" : "covariant-thunk",
	   name ? IDENTIFIER_POINTER (name) : "<unset>");
  if (DECL_THUNK_P (thunk))
    {
      HOST_WIDE_INT fixed_adjust = THUNK_FIXED_OFFSET (thunk);
      tree virtual_adjust = THUNK_VIRTUAL_OFFSET (thunk);

      fprintf (stream, " fixed=" HOST_WIDE_INT_PRINT_DEC, fixed_adjust);
      if (!virtual_adjust)
	/*NOP*/;
      else if (DECL_THIS_THUNK_P (thunk))
	fprintf (stream, " vcall="  HOST_WIDE_INT_PRINT_DEC,
		 tree_low_cst (virtual_adjust, 0));
      else
	fprintf (stream, " vbase=" HOST_WIDE_INT_PRINT_DEC "(%s)",
		 tree_low_cst (BINFO_VPTR_FIELD (virtual_adjust), 0),
		 type_as_string (BINFO_TYPE (virtual_adjust), TFF_SCOPE));
      if (THUNK_ALIAS (thunk))
	fprintf (stream, " alias to %p", (void *)THUNK_ALIAS (thunk));
    }
  fprintf (stream, "\n");
  for (thunks = DECL_THUNKS (thunk); thunks; thunks = TREE_CHAIN (thunks))
    dump_thunk (stream, indent + 2, thunks);
}

/* Dump the thunks for FN.  */

void
debug_thunks (tree fn)
{
  dump_thunk (stderr, 0, fn);
}

/* Virtual function table initialization.  */

/* Create all the necessary vtables for T and its base classes.  */

static void
finish_vtbls (tree t)
{
  tree list;
  tree vbase;

  /* We lay out the primary and secondary vtables in one contiguous
     vtable.  The primary vtable is first, followed by the non-virtual
     secondary vtables in inheritance graph order.  */
  list = build_tree_list (BINFO_VTABLE (TYPE_BINFO (t)), NULL_TREE);
  accumulate_vtbl_inits (TYPE_BINFO (t), TYPE_BINFO (t),
			 TYPE_BINFO (t), t, list);

  /* Then come the virtual bases, also in inheritance graph order.  */
  for (vbase = TYPE_BINFO (t); vbase; vbase = TREE_CHAIN (vbase))
    {
      if (!BINFO_VIRTUAL_P (vbase))
	continue;
      accumulate_vtbl_inits (vbase, vbase, TYPE_BINFO (t), t, list);
    }

  if (BINFO_VTABLE (TYPE_BINFO (t)))
    initialize_vtable (TYPE_BINFO (t), TREE_VALUE (list));
}

/* Initialize the vtable for BINFO with the INITS.  */

static void
initialize_vtable (tree binfo, tree inits)
{
  tree decl;

  layout_vtable_decl (binfo, list_length (inits));
  decl = get_vtbl_decl_for_binfo (binfo);
  initialize_artificial_var (decl, inits);
  dump_vtable (BINFO_TYPE (binfo), binfo, decl);
}

/* Build the VTT (virtual table table) for T.
   A class requires a VTT if it has virtual bases.

   This holds
   1 - primary virtual pointer for complete object T
   2 - secondary VTTs for each direct non-virtual base of T which requires a
       VTT
   3 - secondary virtual pointers for each direct or indirect base of T which
       has virtual bases or is reachable via a virtual path from T.
   4 - secondary VTTs for each direct or indirect virtual base of T.

   Secondary VTTs look like complete object VTTs without part 4.  */

static void
build_vtt (tree t)
{
  tree inits;
  tree type;
  tree vtt;
  tree index;

  /* Build up the initializers for the VTT.  */
  inits = NULL_TREE;
  index = size_zero_node;
  build_vtt_inits (TYPE_BINFO (t), t, &inits, &index);

  /* If we didn't need a VTT, we're done.  */
  if (!inits)
    return;

  /* Figure out the type of the VTT.  */
  type = build_index_type (size_int (list_length (inits) - 1));
  type = build_cplus_array_type (const_ptr_type_node, type);

  /* Now, build the VTT object itself.  */
  vtt = build_vtable (t, mangle_vtt_for_type (t), type);
  initialize_artificial_var (vtt, inits);
  /* Add the VTT to the vtables list.  */
  TREE_CHAIN (vtt) = TREE_CHAIN (CLASSTYPE_VTABLES (t));
  TREE_CHAIN (CLASSTYPE_VTABLES (t)) = vtt;

  dump_vtt (t, vtt);
}

/* When building a secondary VTT, BINFO_VTABLE is set to a TREE_LIST with
   PURPOSE the RTTI_BINFO, VALUE the real vtable pointer for this binfo,
   and CHAIN the vtable pointer for this binfo after construction is
   complete.  VALUE can also be another BINFO, in which case we recurse.  */

static tree
binfo_ctor_vtable (tree binfo)
{
  tree vt;

  while (1)
    {
      vt = BINFO_VTABLE (binfo);
      if (TREE_CODE (vt) == TREE_LIST)
	vt = TREE_VALUE (vt);
      if (TREE_CODE (vt) == TREE_BINFO)
	binfo = vt;
      else
	break;
    }

  return vt;
}

/* Data for secondary VTT initialization.  */
typedef struct secondary_vptr_vtt_init_data_s
{
  /* Is this the primary VTT? */
  bool top_level_p;

  /* Current index into the VTT.  */
  tree index;

  /* TREE_LIST of initializers built up.  */
  tree inits;

  /* The type being constructed by this secondary VTT.  */
  tree type_being_constructed;
} secondary_vptr_vtt_init_data;

/* Recursively build the VTT-initializer for BINFO (which is in the
   hierarchy dominated by T).  INITS points to the end of the initializer
   list to date.  INDEX is the VTT index where the next element will be
   replaced.  Iff BINFO is the binfo for T, this is the top level VTT (i.e.
   not a subvtt for some base of T).  When that is so, we emit the sub-VTTs
   for virtual bases of T. When it is not so, we build the constructor
   vtables for the BINFO-in-T variant.  */

static tree *
build_vtt_inits (tree binfo, tree t, tree *inits, tree *index)
{
  int i;
  tree b;
  tree init;
  tree secondary_vptrs;
  secondary_vptr_vtt_init_data data;
  int top_level_p = SAME_BINFO_TYPE_P (BINFO_TYPE (binfo), t);

  /* We only need VTTs for subobjects with virtual bases.  */
  if (!CLASSTYPE_VBASECLASSES (BINFO_TYPE (binfo)))
    return inits;

  /* We need to use a construction vtable if this is not the primary
     VTT.  */
  if (!top_level_p)
    {
      build_ctor_vtbl_group (binfo, t);

      /* Record the offset in the VTT where this sub-VTT can be found.  */
      BINFO_SUBVTT_INDEX (binfo) = *index;
    }

  /* Add the address of the primary vtable for the complete object.  */
  init = binfo_ctor_vtable (binfo);
  *inits = build_tree_list (NULL_TREE, init);
  inits = &TREE_CHAIN (*inits);
  if (top_level_p)
    {
      gcc_assert (!BINFO_VPTR_INDEX (binfo));
      BINFO_VPTR_INDEX (binfo) = *index;
    }
  *index = size_binop (PLUS_EXPR, *index, TYPE_SIZE_UNIT (ptr_type_node));

  /* Recursively add the secondary VTTs for non-virtual bases.  */
  for (i = 0; BINFO_BASE_ITERATE (binfo, i, b); ++i)
    if (!BINFO_VIRTUAL_P (b))
      inits = build_vtt_inits (b, t, inits, index);

  /* Add secondary virtual pointers for all subobjects of BINFO with
     either virtual bases or reachable along a virtual path, except
     subobjects that are non-virtual primary bases.  */
  data.top_level_p = top_level_p;
  data.index = *index;
  data.inits = NULL;
  data.type_being_constructed = BINFO_TYPE (binfo);

  dfs_walk_once (binfo, dfs_build_secondary_vptr_vtt_inits, NULL, &data);

  *index = data.index;

  /* The secondary vptrs come back in reverse order.  After we reverse
     them, and add the INITS, the last init will be the first element
     of the chain.  */
  secondary_vptrs = data.inits;
  if (secondary_vptrs)
    {
      *inits = nreverse (secondary_vptrs);
      inits = &TREE_CHAIN (secondary_vptrs);
      gcc_assert (*inits == NULL_TREE);
    }

  if (top_level_p)
    /* Add the secondary VTTs for virtual bases in inheritance graph
       order.  */
    for (b = TYPE_BINFO (BINFO_TYPE (binfo)); b; b = TREE_CHAIN (b))
      {
	if (!BINFO_VIRTUAL_P (b))
	  continue;

	inits = build_vtt_inits (b, t, inits, index);
      }
  else
    /* Remove the ctor vtables we created.  */
    dfs_walk_all (binfo, dfs_fixup_binfo_vtbls, NULL, binfo);

  return inits;
}

/* Called from build_vtt_inits via dfs_walk.  BINFO is the binfo for the base
   in most derived. DATA is a SECONDARY_VPTR_VTT_INIT_DATA structure.  */

static tree
dfs_build_secondary_vptr_vtt_inits (tree binfo, void *data_)
{
  secondary_vptr_vtt_init_data *data = (secondary_vptr_vtt_init_data *)data_;

  /* We don't care about bases that don't have vtables.  */
  if (!TYPE_VFIELD (BINFO_TYPE (binfo)))
    return dfs_skip_bases;

  /* We're only interested in proper subobjects of the type being
     constructed.  */
  if (SAME_BINFO_TYPE_P (BINFO_TYPE (binfo), data->type_being_constructed))
    return NULL_TREE;

  /* We're only interested in bases with virtual bases or reachable
     via a virtual path from the type being constructed.  */
  if (!(CLASSTYPE_VBASECLASSES (BINFO_TYPE (binfo))
	|| binfo_via_virtual (binfo, data->type_being_constructed)))
    return dfs_skip_bases;

  /* We're not interested in non-virtual primary bases.  */
  if (!BINFO_VIRTUAL_P (binfo) && BINFO_PRIMARY_P (binfo))
    return NULL_TREE;

  /* Record the index where this secondary vptr can be found.  */
  if (data->top_level_p)
    {
      gcc_assert (!BINFO_VPTR_INDEX (binfo));
      BINFO_VPTR_INDEX (binfo) = data->index;

      if (BINFO_VIRTUAL_P (binfo))
	{
	  /* It's a primary virtual base, and this is not a
	     construction vtable.  Find the base this is primary of in
	     the inheritance graph, and use that base's vtable
	     now.  */
	  while (BINFO_PRIMARY_P (binfo))
	    binfo = BINFO_INHERITANCE_CHAIN (binfo);
	}
    }

  /* Add the initializer for the secondary vptr itself.  */
  data->inits = tree_cons (NULL_TREE, binfo_ctor_vtable (binfo), data->inits);

  /* Advance the vtt index.  */
  data->index = size_binop (PLUS_EXPR, data->index,
			    TYPE_SIZE_UNIT (ptr_type_node));

  return NULL_TREE;
}

/* Called from build_vtt_inits via dfs_walk. After building
   constructor vtables and generating the sub-vtt from them, we need
   to restore the BINFO_VTABLES that were scribbled on.  DATA is the
   binfo of the base whose sub vtt was generated.  */

static tree
dfs_fixup_binfo_vtbls (tree binfo, void* data)
{
  tree vtable = BINFO_VTABLE (binfo);

  if (!TYPE_CONTAINS_VPTR_P (BINFO_TYPE (binfo)))
    /* If this class has no vtable, none of its bases do.  */
    return dfs_skip_bases;

  if (!vtable)
    /* This might be a primary base, so have no vtable in this
       hierarchy.  */
    return NULL_TREE;

  /* If we scribbled the construction vtable vptr into BINFO, clear it
     out now.  */
  if (TREE_CODE (vtable) == TREE_LIST
      && (TREE_PURPOSE (vtable) == (tree) data))
    BINFO_VTABLE (binfo) = TREE_CHAIN (vtable);

  return NULL_TREE;
}

/* Build the construction vtable group for BINFO which is in the
   hierarchy dominated by T.  */

static void
build_ctor_vtbl_group (tree binfo, tree t)
{
  tree list;
  tree type;
  tree vtbl;
  tree inits;
  tree id;
  tree vbase;

  /* See if we've already created this construction vtable group.  */
  id = mangle_ctor_vtbl_for_type (t, binfo);
  if (IDENTIFIER_GLOBAL_VALUE (id))
    return;

  gcc_assert (!SAME_BINFO_TYPE_P (BINFO_TYPE (binfo), t));
  /* Build a version of VTBL (with the wrong type) for use in
     constructing the addresses of secondary vtables in the
     construction vtable group.  */
  vtbl = build_vtable (t, id, ptr_type_node);
  DECL_CONSTRUCTION_VTABLE_P (vtbl) = 1;
  list = build_tree_list (vtbl, NULL_TREE);
  accumulate_vtbl_inits (binfo, TYPE_BINFO (TREE_TYPE (binfo)),
			 binfo, t, list);

  /* Add the vtables for each of our virtual bases using the vbase in T
     binfo.  */
  for (vbase = TYPE_BINFO (BINFO_TYPE (binfo));
       vbase;
       vbase = TREE_CHAIN (vbase))
    {
      tree b;

      if (!BINFO_VIRTUAL_P (vbase))
	continue;
      b = copied_binfo (vbase, binfo);

      accumulate_vtbl_inits (b, vbase, binfo, t, list);
    }
  inits = TREE_VALUE (list);

  /* Figure out the type of the construction vtable.  */
  type = build_index_type (size_int (list_length (inits) - 1));
  type = build_cplus_array_type (vtable_entry_type, type);
  TREE_TYPE (vtbl) = type;

  /* Initialize the construction vtable.  */
  CLASSTYPE_VTABLES (t) = chainon (CLASSTYPE_VTABLES (t), vtbl);
  initialize_artificial_var (vtbl, inits);
  dump_vtable (t, binfo, vtbl);
}

/* Add the vtbl initializers for BINFO (and its bases other than
   non-virtual primaries) to the list of INITS.  BINFO is in the
   hierarchy dominated by T.  RTTI_BINFO is the binfo within T of
   the constructor the vtbl inits should be accumulated for. (If this
   is the complete object vtbl then RTTI_BINFO will be TYPE_BINFO (T).)
   ORIG_BINFO is the binfo for this object within BINFO_TYPE (RTTI_BINFO).
   BINFO is the active base equivalent of ORIG_BINFO in the inheritance
   graph of T. Both BINFO and ORIG_BINFO will have the same BINFO_TYPE,
   but are not necessarily the same in terms of layout.  */

static void
accumulate_vtbl_inits (tree binfo,
		       tree orig_binfo,
		       tree rtti_binfo,
		       tree t,
		       tree inits)
{
  int i;
  tree base_binfo;
  int ctor_vtbl_p = !SAME_BINFO_TYPE_P (BINFO_TYPE (rtti_binfo), t);

  gcc_assert (SAME_BINFO_TYPE_P (BINFO_TYPE (binfo), BINFO_TYPE (orig_binfo)));

  /* If it doesn't have a vptr, we don't do anything.  */
  if (!TYPE_CONTAINS_VPTR_P (BINFO_TYPE (binfo)))
    return;

  /* If we're building a construction vtable, we're not interested in
     subobjects that don't require construction vtables.  */
  if (ctor_vtbl_p
      && !CLASSTYPE_VBASECLASSES (BINFO_TYPE (binfo))
      && !binfo_via_virtual (orig_binfo, BINFO_TYPE (rtti_binfo)))
    return;

  /* Build the initializers for the BINFO-in-T vtable.  */
  TREE_VALUE (inits)
    = chainon (TREE_VALUE (inits),
	       dfs_accumulate_vtbl_inits (binfo, orig_binfo,
					  rtti_binfo, t, inits));

  /* Walk the BINFO and its bases.  We walk in preorder so that as we
     initialize each vtable we can figure out at what offset the
     secondary vtable lies from the primary vtable.  We can't use
     dfs_walk here because we need to iterate through bases of BINFO
     and RTTI_BINFO simultaneously.  */
  for (i = 0; BINFO_BASE_ITERATE (binfo, i, base_binfo); ++i)
    {
      /* Skip virtual bases.  */
      if (BINFO_VIRTUAL_P (base_binfo))
	continue;
      accumulate_vtbl_inits (base_binfo,
			     BINFO_BASE_BINFO (orig_binfo, i),
			     rtti_binfo, t,
			     inits);
    }
}

/* Called from accumulate_vtbl_inits.  Returns the initializers for
   the BINFO vtable.  */

static tree
dfs_accumulate_vtbl_inits (tree binfo,
			   tree orig_binfo,
			   tree rtti_binfo,
			   tree t,
			   tree l)
{
  tree inits = NULL_TREE;
  tree vtbl = NULL_TREE;
  int ctor_vtbl_p = !SAME_BINFO_TYPE_P (BINFO_TYPE (rtti_binfo), t);

  if (ctor_vtbl_p
      && BINFO_VIRTUAL_P (orig_binfo) && BINFO_PRIMARY_P (orig_binfo))
    {
      /* In the hierarchy of BINFO_TYPE (RTTI_BINFO), this is a
	 primary virtual base.  If it is not the same primary in
	 the hierarchy of T, we'll need to generate a ctor vtable
	 for it, to place at its location in T.  If it is the same
	 primary, we still need a VTT entry for the vtable, but it
	 should point to the ctor vtable for the base it is a
	 primary for within the sub-hierarchy of RTTI_BINFO.

	 There are three possible cases:

	 1) We are in the same place.
	 2) We are a primary base within a lost primary virtual base of
	 RTTI_BINFO.
	 3) We are primary to something not a base of RTTI_BINFO.  */

      tree b;
      tree last = NULL_TREE;

      /* First, look through the bases we are primary to for RTTI_BINFO
	 or a virtual base.  */
      b = binfo;
      while (BINFO_PRIMARY_P (b))
	{
	  b = BINFO_INHERITANCE_CHAIN (b);
	  last = b;
	  if (BINFO_VIRTUAL_P (b) || b == rtti_binfo)
	    goto found;
	}
      /* If we run out of primary links, keep looking down our
	 inheritance chain; we might be an indirect primary.  */
      for (b = last; b; b = BINFO_INHERITANCE_CHAIN (b))
	if (BINFO_VIRTUAL_P (b) || b == rtti_binfo)
	  break;
    found:

      /* If we found RTTI_BINFO, this is case 1.  If we found a virtual
	 base B and it is a base of RTTI_BINFO, this is case 2.  In
	 either case, we share our vtable with LAST, i.e. the
	 derived-most base within B of which we are a primary.  */
      if (b == rtti_binfo
	  || (b && binfo_for_vbase (BINFO_TYPE (b), BINFO_TYPE (rtti_binfo))))
	/* Just set our BINFO_VTABLE to point to LAST, as we may not have
	   set LAST's BINFO_VTABLE yet.  We'll extract the actual vptr in
	   binfo_ctor_vtable after everything's been set up.  */
	vtbl = last;

      /* Otherwise, this is case 3 and we get our own.  */
    }
  else if (!BINFO_NEW_VTABLE_MARKED (orig_binfo))
    return inits;

  if (!vtbl)
    {
      tree index;
      int non_fn_entries;

      /* Compute the initializer for this vtable.  */
      inits = build_vtbl_initializer (binfo, orig_binfo, t, rtti_binfo,
				      &non_fn_entries);

      /* Figure out the position to which the VPTR should point.  */
      vtbl = TREE_PURPOSE (l);
      vtbl = build_address (vtbl);
      /* ??? We should call fold_convert to convert the address to
	 vtbl_ptr_type_node, which is the type of elements in the
	 vtable.  However, the resulting NOP_EXPRs confuse other parts
	 of the C++ front end.  */
      gcc_assert (TREE_CODE (vtbl) == ADDR_EXPR);
      TREE_TYPE (vtbl) = vtbl_ptr_type_node;
      index = size_binop (PLUS_EXPR,
			  size_int (non_fn_entries),
			  size_int (list_length (TREE_VALUE (l))));
      index = size_binop (MULT_EXPR,
			  TYPE_SIZE_UNIT (vtable_entry_type),
			  index);
      vtbl = build2 (PLUS_EXPR, TREE_TYPE (vtbl), vtbl, index);
    }

  if (ctor_vtbl_p)
    /* For a construction vtable, we can't overwrite BINFO_VTABLE.
       So, we make a TREE_LIST.  Later, dfs_fixup_binfo_vtbls will
       straighten this out.  */
    BINFO_VTABLE (binfo) = tree_cons (rtti_binfo, vtbl, BINFO_VTABLE (binfo));
  else if (BINFO_PRIMARY_P (binfo) && BINFO_VIRTUAL_P (binfo))
    inits = NULL_TREE;
  else
     /* For an ordinary vtable, set BINFO_VTABLE.  */
    BINFO_VTABLE (binfo) = vtbl;

  return inits;
}

static GTY(()) tree abort_fndecl_addr;

/* Construct the initializer for BINFO's virtual function table.  BINFO
   is part of the hierarchy dominated by T.  If we're building a
   construction vtable, the ORIG_BINFO is the binfo we should use to
   find the actual function pointers to put in the vtable - but they
   can be overridden on the path to most-derived in the graph that
   ORIG_BINFO belongs.  Otherwise,
   ORIG_BINFO should be the same as BINFO.  The RTTI_BINFO is the
   BINFO that should be indicated by the RTTI information in the
   vtable; it will be a base class of T, rather than T itself, if we
   are building a construction vtable.

   The value returned is a TREE_LIST suitable for wrapping in a
   CONSTRUCTOR to use as the DECL_INITIAL for a vtable.  If
   NON_FN_ENTRIES_P is not NULL, *NON_FN_ENTRIES_P is set to the
   number of non-function entries in the vtable.

   It might seem that this function should never be called with a
   BINFO for which BINFO_PRIMARY_P holds, the vtable for such a
   base is always subsumed by a derived class vtable.  However, when
   we are building construction vtables, we do build vtables for
   primary bases; we need these while the primary base is being
   constructed.  */

static tree
build_vtbl_initializer (tree binfo,
			tree orig_binfo,
			tree t,
			tree rtti_binfo,
			int* non_fn_entries_p)
{
  tree v, b;
  tree vfun_inits;
  vtbl_init_data vid;
  unsigned ix;
  tree vbinfo;
  VEC(tree,gc) *vbases;

  /* Initialize VID.  */
  memset (&vid, 0, sizeof (vid));
  vid.binfo = binfo;
  vid.derived = t;
  vid.rtti_binfo = rtti_binfo;
  vid.last_init = &vid.inits;
  vid.primary_vtbl_p = SAME_BINFO_TYPE_P (BINFO_TYPE (binfo), t);
  vid.ctor_vtbl_p = !SAME_BINFO_TYPE_P (BINFO_TYPE (rtti_binfo), t);
  vid.generate_vcall_entries = true;
  /* The first vbase or vcall offset is at index -3 in the vtable.  */
  vid.index = ssize_int(-3 * TARGET_VTABLE_DATA_ENTRY_DISTANCE);

  /* Add entries to the vtable for RTTI.  */
  build_rtti_vtbl_entries (binfo, &vid);

  /* Create an array for keeping track of the functions we've
     processed.  When we see multiple functions with the same
     signature, we share the vcall offsets.  */
  vid.fns = VEC_alloc (tree, gc, 32);
  /* Add the vcall and vbase offset entries.  */
  build_vcall_and_vbase_vtbl_entries (binfo, &vid);

  /* Clear BINFO_VTABLE_PATH_MARKED; it's set by
     build_vbase_offset_vtbl_entries.  */
  for (vbases = CLASSTYPE_VBASECLASSES (t), ix = 0;
       VEC_iterate (tree, vbases, ix, vbinfo); ix++)
    BINFO_VTABLE_PATH_MARKED (vbinfo) = 0;

  /* If the target requires padding between data entries, add that now.  */
  if (TARGET_VTABLE_DATA_ENTRY_DISTANCE > 1)
    {
      tree cur, *prev;

      for (prev = &vid.inits; (cur = *prev); prev = &TREE_CHAIN (cur))
	{
	  tree add = cur;
	  int i;

	  for (i = 1; i < TARGET_VTABLE_DATA_ENTRY_DISTANCE; ++i)
	    add = tree_cons (NULL_TREE,
			     build1 (NOP_EXPR, vtable_entry_type,
				     null_pointer_node),
			     add);
	  *prev = add;
	}
    }

  if (non_fn_entries_p)
    *non_fn_entries_p = list_length (vid.inits);

  /* Go through all the ordinary virtual functions, building up
     initializers.  */
  vfun_inits = NULL_TREE;
  for (v = BINFO_VIRTUALS (orig_binfo); v; v = TREE_CHAIN (v))
    {
      tree delta;
      tree vcall_index;
      tree fn, fn_original;
      tree init = NULL_TREE;

      fn = BV_FN (v);
      fn_original = fn;
      if (DECL_THUNK_P (fn))
	{
	  if (!DECL_NAME (fn))
	    finish_thunk (fn);
	  if (THUNK_ALIAS (fn))
	    {
	      fn = THUNK_ALIAS (fn);
	      BV_FN (v) = fn;
	    }
	  fn_original = THUNK_TARGET (fn);
	}

      /* If the only definition of this function signature along our
	 primary base chain is from a lost primary, this vtable slot will
	 never be used, so just zero it out.  This is important to avoid
	 requiring extra thunks which cannot be generated with the function.

	 We first check this in update_vtable_entry_for_fn, so we handle
	 restored primary bases properly; we also need to do it here so we
	 zero out unused slots in ctor vtables, rather than filling themff
	 with erroneous values (though harmless, apart from relocation
	 costs).  */
      for (b = binfo; ; b = get_primary_binfo (b))
	{
	  /* We found a defn before a lost primary; go ahead as normal.  */
	  if (look_for_overrides_here (BINFO_TYPE (b), fn_original))
	    break;

	  /* The nearest definition is from a lost primary; clear the
	     slot.  */
	  if (BINFO_LOST_PRIMARY_P (b))
	    {
	      init = size_zero_node;
	      break;
	    }
	}

      if (! init)
	{
	  /* Pull the offset for `this', and the function to call, out of
	     the list.  */
	  delta = BV_DELTA (v);
	  vcall_index = BV_VCALL_INDEX (v);

	  gcc_assert (TREE_CODE (delta) == INTEGER_CST);
	  gcc_assert (TREE_CODE (fn) == FUNCTION_DECL);

	  /* You can't call an abstract virtual function; it's abstract.
	     So, we replace these functions with __pure_virtual.  */
	  if (DECL_PURE_VIRTUAL_P (fn_original))
	    {
	      fn = abort_fndecl;
	      if (abort_fndecl_addr == NULL)
		abort_fndecl_addr = build1 (ADDR_EXPR, vfunc_ptr_type_node, fn);
	      init = abort_fndecl_addr;
	    }
	  else
	    {
	      if (!integer_zerop (delta) || vcall_index)
		{
		  fn = make_thunk (fn, /*this_adjusting=*/1, delta, vcall_index);
		  if (!DECL_NAME (fn))
		    finish_thunk (fn);
		}
	      /* Take the address of the function, considering it to be of an
		 appropriate generic type.  */
	      init = build1 (ADDR_EXPR, vfunc_ptr_type_node, fn);
	    }
	}

      /* And add it to the chain of initializers.  */
      if (TARGET_VTABLE_USES_DESCRIPTORS)
	{
	  int i;
	  if (init == size_zero_node)
	    for (i = 0; i < TARGET_VTABLE_USES_DESCRIPTORS; ++i)
	      vfun_inits = tree_cons (NULL_TREE, init, vfun_inits);
	  else
	    for (i = 0; i < TARGET_VTABLE_USES_DESCRIPTORS; ++i)
	      {
		tree fdesc = build2 (FDESC_EXPR, vfunc_ptr_type_node,
				     TREE_OPERAND (init, 0),
				     build_int_cst (NULL_TREE, i));
		TREE_CONSTANT (fdesc) = 1;
		TREE_INVARIANT (fdesc) = 1;

		vfun_inits = tree_cons (NULL_TREE, fdesc, vfun_inits);
	      }
	}
      else
	vfun_inits = tree_cons (NULL_TREE, init, vfun_inits);
    }

  /* The initializers for virtual functions were built up in reverse
     order; straighten them out now.  */
  vfun_inits = nreverse (vfun_inits);

  /* The negative offset initializers are also in reverse order.  */
  vid.inits = nreverse (vid.inits);

  /* Chain the two together.  */
  return chainon (vid.inits, vfun_inits);
}

/* Adds to vid->inits the initializers for the vbase and vcall
   offsets in BINFO, which is in the hierarchy dominated by T.  */

static void
build_vcall_and_vbase_vtbl_entries (tree binfo, vtbl_init_data* vid)
{
  tree b;

  /* If this is a derived class, we must first create entries
     corresponding to the primary base class.  */
  b = get_primary_binfo (binfo);
  if (b)
    build_vcall_and_vbase_vtbl_entries (b, vid);

  /* Add the vbase entries for this base.  */
  build_vbase_offset_vtbl_entries (binfo, vid);
  /* Add the vcall entries for this base.  */
  build_vcall_offset_vtbl_entries (binfo, vid);
}

/* Returns the initializers for the vbase offset entries in the vtable
   for BINFO (which is part of the class hierarchy dominated by T), in
   reverse order.  VBASE_OFFSET_INDEX gives the vtable index
   where the next vbase offset will go.  */

static void
build_vbase_offset_vtbl_entries (tree binfo, vtbl_init_data* vid)
{
  tree vbase;
  tree t;
  tree non_primary_binfo;

  /* If there are no virtual baseclasses, then there is nothing to
     do.  */
  if (!CLASSTYPE_VBASECLASSES (BINFO_TYPE (binfo)))
    return;

  t = vid->derived;

  /* We might be a primary base class.  Go up the inheritance hierarchy
     until we find the most derived class of which we are a primary base:
     it is the offset of that which we need to use.  */
  non_primary_binfo = binfo;
  while (BINFO_INHERITANCE_CHAIN (non_primary_binfo))
    {
      tree b;

      /* If we have reached a virtual base, then it must be a primary
	 base (possibly multi-level) of vid->binfo, or we wouldn't
	 have called build_vcall_and_vbase_vtbl_entries for it.  But it
	 might be a lost primary, so just skip down to vid->binfo.  */
      if (BINFO_VIRTUAL_P (non_primary_binfo))
	{
	  non_primary_binfo = vid->binfo;
	  break;
	}

      b = BINFO_INHERITANCE_CHAIN (non_primary_binfo);
      if (get_primary_binfo (b) != non_primary_binfo)
	break;
      non_primary_binfo = b;
    }

  /* Go through the virtual bases, adding the offsets.  */
  for (vbase = TYPE_BINFO (BINFO_TYPE (binfo));
       vbase;
       vbase = TREE_CHAIN (vbase))
    {
      tree b;
      tree delta;

      if (!BINFO_VIRTUAL_P (vbase))
	continue;

      /* Find the instance of this virtual base in the complete
	 object.  */
      b = copied_binfo (vbase, binfo);

      /* If we've already got an offset for this virtual base, we
	 don't need another one.  */
      if (BINFO_VTABLE_PATH_MARKED (b))
	continue;
      BINFO_VTABLE_PATH_MARKED (b) = 1;

      /* Figure out where we can find this vbase offset.  */
      delta = size_binop (MULT_EXPR,
			  vid->index,
			  convert (ssizetype,
				   TYPE_SIZE_UNIT (vtable_entry_type)));
      if (vid->primary_vtbl_p)
	BINFO_VPTR_FIELD (b) = delta;

      if (binfo != TYPE_BINFO (t))
	/* The vbase offset had better be the same.  */
	gcc_assert (tree_int_cst_equal (delta, BINFO_VPTR_FIELD (vbase)));

      /* The next vbase will come at a more negative offset.  */
      vid->index = size_binop (MINUS_EXPR, vid->index,
			       ssize_int (TARGET_VTABLE_DATA_ENTRY_DISTANCE));

      /* The initializer is the delta from BINFO to this virtual base.
	 The vbase offsets go in reverse inheritance-graph order, and
	 we are walking in inheritance graph order so these end up in
	 the right order.  */
      delta = size_diffop (BINFO_OFFSET (b), BINFO_OFFSET (non_primary_binfo));

      *vid->last_init
	= build_tree_list (NULL_TREE,
			   fold_build1 (NOP_EXPR,
					vtable_entry_type,
					delta));
      vid->last_init = &TREE_CHAIN (*vid->last_init);
    }
}

/* Adds the initializers for the vcall offset entries in the vtable
   for BINFO (which is part of the class hierarchy dominated by VID->DERIVED)
   to VID->INITS.  */

static void
build_vcall_offset_vtbl_entries (tree binfo, vtbl_init_data* vid)
{
  /* We only need these entries if this base is a virtual base.  We
     compute the indices -- but do not add to the vtable -- when
     building the main vtable for a class.  */
  if (BINFO_VIRTUAL_P (binfo) || binfo == TYPE_BINFO (vid->derived))
    {
      /* We need a vcall offset for each of the virtual functions in this
	 vtable.  For example:

	   class A { virtual void f (); };
	   class B1 : virtual public A { virtual void f (); };
	   class B2 : virtual public A { virtual void f (); };
	   class C: public B1, public B2 { virtual void f (); };

	 A C object has a primary base of B1, which has a primary base of A.  A
	 C also has a secondary base of B2, which no longer has a primary base
	 of A.  So the B2-in-C construction vtable needs a secondary vtable for
	 A, which will adjust the A* to a B2* to call f.  We have no way of
	 knowing what (or even whether) this offset will be when we define B2,
	 so we store this "vcall offset" in the A sub-vtable and look it up in
	 a "virtual thunk" for B2::f.

	 We need entries for all the functions in our primary vtable and
	 in our non-virtual bases' secondary vtables.  */
      vid->vbase = binfo;
      /* If we are just computing the vcall indices -- but do not need
	 the actual entries -- not that.  */
      if (!BINFO_VIRTUAL_P (binfo))
	vid->generate_vcall_entries = false;
      /* Now, walk through the non-virtual bases, adding vcall offsets.  */
      add_vcall_offset_vtbl_entries_r (binfo, vid);
    }
}

/* Build vcall offsets, starting with those for BINFO.  */

static void
add_vcall_offset_vtbl_entries_r (tree binfo, vtbl_init_data* vid)
{
  int i;
  tree primary_binfo;
  tree base_binfo;

  /* Don't walk into virtual bases -- except, of course, for the
     virtual base for which we are building vcall offsets.  Any
     primary virtual base will have already had its offsets generated
     through the recursion in build_vcall_and_vbase_vtbl_entries.  */
  if (BINFO_VIRTUAL_P (binfo) && vid->vbase != binfo)
    return;

  /* If BINFO has a primary base, process it first.  */
  primary_binfo = get_primary_binfo (binfo);
  if (primary_binfo)
    add_vcall_offset_vtbl_entries_r (primary_binfo, vid);

  /* Add BINFO itself to the list.  */
  add_vcall_offset_vtbl_entries_1 (binfo, vid);

  /* Scan the non-primary bases of BINFO.  */
  for (i = 0; BINFO_BASE_ITERATE (binfo, i, base_binfo); ++i)
    if (base_binfo != primary_binfo)
      add_vcall_offset_vtbl_entries_r (base_binfo, vid);
}

/* Called from build_vcall_offset_vtbl_entries_r.  */

static void
add_vcall_offset_vtbl_entries_1 (tree binfo, vtbl_init_data* vid)
{
  /* Make entries for the rest of the virtuals.  */
  if (abi_version_at_least (2))
    {
      tree orig_fn;

      /* The ABI requires that the methods be processed in declaration
	 order.  G++ 3.2 used the order in the vtable.  */
      for (orig_fn = TYPE_METHODS (BINFO_TYPE (binfo));
	   orig_fn;
	   orig_fn = TREE_CHAIN (orig_fn))
	if (DECL_VINDEX (orig_fn))
	  add_vcall_offset (orig_fn, binfo, vid);
    }
  else
    {
      tree derived_virtuals;
      tree base_virtuals;
      tree orig_virtuals;
      /* If BINFO is a primary base, the most derived class which has
	 BINFO as a primary base; otherwise, just BINFO.  */
      tree non_primary_binfo;

      /* We might be a primary base class.  Go up the inheritance hierarchy
	 until we find the most derived class of which we are a primary base:
	 it is the BINFO_VIRTUALS there that we need to consider.  */
      non_primary_binfo = binfo;
      while (BINFO_INHERITANCE_CHAIN (non_primary_binfo))
	{
	  tree b;

	  /* If we have reached a virtual base, then it must be vid->vbase,
	     because we ignore other virtual bases in
	     add_vcall_offset_vtbl_entries_r.  In turn, it must be a primary
	     base (possibly multi-level) of vid->binfo, or we wouldn't
	     have called build_vcall_and_vbase_vtbl_entries for it.  But it
	     might be a lost primary, so just skip down to vid->binfo.  */
	  if (BINFO_VIRTUAL_P (non_primary_binfo))
	    {
	      gcc_assert (non_primary_binfo == vid->vbase);
	      non_primary_binfo = vid->binfo;
	      break;
	    }

	  b = BINFO_INHERITANCE_CHAIN (non_primary_binfo);
	  if (get_primary_binfo (b) != non_primary_binfo)
	    break;
	  non_primary_binfo = b;
	}

      if (vid->ctor_vtbl_p)
	/* For a ctor vtable we need the equivalent binfo within the hierarchy
	   where rtti_binfo is the most derived type.  */
	non_primary_binfo
	  = original_binfo (non_primary_binfo, vid->rtti_binfo);

      for (base_virtuals = BINFO_VIRTUALS (binfo),
	     derived_virtuals = BINFO_VIRTUALS (non_primary_binfo),
	     orig_virtuals = BINFO_VIRTUALS (TYPE_BINFO (BINFO_TYPE (binfo)));
	   base_virtuals;
	   base_virtuals = TREE_CHAIN (base_virtuals),
	     derived_virtuals = TREE_CHAIN (derived_virtuals),
	     orig_virtuals = TREE_CHAIN (orig_virtuals))
	{
	  tree orig_fn;

	  /* Find the declaration that originally caused this function to
	     be present in BINFO_TYPE (binfo).  */
	  orig_fn = BV_FN (orig_virtuals);

	  /* When processing BINFO, we only want to generate vcall slots for
	     function slots introduced in BINFO.  So don't try to generate
	     one if the function isn't even defined in BINFO.  */
	  if (!SAME_BINFO_TYPE_P (BINFO_TYPE (binfo), DECL_CONTEXT (orig_fn)))
	    continue;

	  add_vcall_offset (orig_fn, binfo, vid);
	}
    }
}

/* Add a vcall offset entry for ORIG_FN to the vtable.  */

static void
add_vcall_offset (tree orig_fn, tree binfo, vtbl_init_data *vid)
{
  size_t i;
  tree vcall_offset;
  tree derived_entry;

  /* If there is already an entry for a function with the same
     signature as FN, then we do not need a second vcall offset.
     Check the list of functions already present in the derived
     class vtable.  */
  for (i = 0; VEC_iterate (tree, vid->fns, i, derived_entry); ++i)
    {
      if (same_signature_p (derived_entry, orig_fn)
	  /* We only use one vcall offset for virtual destructors,
	     even though there are two virtual table entries.  */
	  || (DECL_DESTRUCTOR_P (derived_entry)
	      && DECL_DESTRUCTOR_P (orig_fn)))
	return;
    }

  /* If we are building these vcall offsets as part of building
     the vtable for the most derived class, remember the vcall
     offset.  */
  if (vid->binfo == TYPE_BINFO (vid->derived))
    {
      tree_pair_p elt = VEC_safe_push (tree_pair_s, gc,
				       CLASSTYPE_VCALL_INDICES (vid->derived),
				       NULL);
      elt->purpose = orig_fn;
      elt->value = vid->index;
    }

  /* The next vcall offset will be found at a more negative
     offset.  */
  vid->index = size_binop (MINUS_EXPR, vid->index,
			   ssize_int (TARGET_VTABLE_DATA_ENTRY_DISTANCE));

  /* Keep track of this function.  */
  VEC_safe_push (tree, gc, vid->fns, orig_fn);

  if (vid->generate_vcall_entries)
    {
      tree base;
      tree fn;

      /* Find the overriding function.  */
      fn = find_final_overrider (vid->rtti_binfo, binfo, orig_fn);
      if (fn == error_mark_node)
	vcall_offset = build1 (NOP_EXPR, vtable_entry_type,
			       integer_zero_node);
      else
	{
	  base = TREE_VALUE (fn);

	  /* The vbase we're working on is a primary base of
	     vid->binfo.  But it might be a lost primary, so its
	     BINFO_OFFSET might be wrong, so we just use the
	     BINFO_OFFSET from vid->binfo.  */
	  vcall_offset = size_diffop (BINFO_OFFSET (base),
				      BINFO_OFFSET (vid->binfo));
	  vcall_offset = fold_build1 (NOP_EXPR, vtable_entry_type,
				      vcall_offset);
	}
      /* Add the initializer to the vtable.  */
      *vid->last_init = build_tree_list (NULL_TREE, vcall_offset);
      vid->last_init = &TREE_CHAIN (*vid->last_init);
    }
}

/* Return vtbl initializers for the RTTI entries corresponding to the
   BINFO's vtable.  The RTTI entries should indicate the object given
   by VID->rtti_binfo.  */

static void
build_rtti_vtbl_entries (tree binfo, vtbl_init_data* vid)
{
  tree b;
  tree t;
  tree basetype;
  tree offset;
  tree decl;
  tree init;

  basetype = BINFO_TYPE (binfo);
  t = BINFO_TYPE (vid->rtti_binfo);

  /* To find the complete object, we will first convert to our most
     primary base, and then add the offset in the vtbl to that value.  */
  b = binfo;
  while (CLASSTYPE_HAS_PRIMARY_BASE_P (BINFO_TYPE (b))
	 && !BINFO_LOST_PRIMARY_P (b))
    {
      tree primary_base;

      primary_base = get_primary_binfo (b);
      gcc_assert (BINFO_PRIMARY_P (primary_base)
		  && BINFO_INHERITANCE_CHAIN (primary_base) == b);
      b = primary_base;
    }
  offset = size_diffop (BINFO_OFFSET (vid->rtti_binfo), BINFO_OFFSET (b));

  /* The second entry is the address of the typeinfo object.  */
  if (flag_rtti)
    decl = build_address (get_tinfo_decl (t));
  else
    decl = integer_zero_node;

  /* Convert the declaration to a type that can be stored in the
     vtable.  */
  init = build_nop (vfunc_ptr_type_node, decl);
  *vid->last_init = build_tree_list (NULL_TREE, init);
  vid->last_init = &TREE_CHAIN (*vid->last_init);

  /* Add the offset-to-top entry.  It comes earlier in the vtable than
     the typeinfo entry.  Convert the offset to look like a
     function pointer, so that we can put it in the vtable.  */
  init = build_nop (vfunc_ptr_type_node, offset);
  *vid->last_init = build_tree_list (NULL_TREE, init);
  vid->last_init = &TREE_CHAIN (*vid->last_init);
}

/* Fold a OBJ_TYPE_REF expression to the address of a function.
   KNOWN_TYPE carries the true type of OBJ_TYPE_REF_OBJECT(REF).  */

tree
cp_fold_obj_type_ref (tree ref, tree known_type)
{
  HOST_WIDE_INT index = tree_low_cst (OBJ_TYPE_REF_TOKEN (ref), 1);
  HOST_WIDE_INT i = 0;
  tree v = BINFO_VIRTUALS (TYPE_BINFO (known_type));
  tree fndecl;

  while (i != index)
    {
      i += (TARGET_VTABLE_USES_DESCRIPTORS
	    ? TARGET_VTABLE_USES_DESCRIPTORS : 1);
      v = TREE_CHAIN (v);
    }

  fndecl = BV_FN (v);

#ifdef ENABLE_CHECKING
  gcc_assert (tree_int_cst_equal (OBJ_TYPE_REF_TOKEN (ref),
				  DECL_VINDEX (fndecl)));
#endif

  cgraph_node (fndecl)->local.vtable_method = true;

  return build_address (fndecl);
}

#include "gt-cp-class.h"
