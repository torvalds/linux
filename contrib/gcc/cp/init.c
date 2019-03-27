/* Handle initialization things in C++.
   Copyright (C) 1987, 1989, 1992, 1993, 1994, 1995, 1996, 1997, 1998,
   1999, 2000, 2001, 2002, 2003, 2004, 2005 Free Software Foundation, Inc.
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
#include "rtl.h"
#include "expr.h"
#include "cp-tree.h"
#include "flags.h"
#include "output.h"
#include "except.h"
#include "toplev.h"
#include "target.h"

static bool begin_init_stmts (tree *, tree *);
static tree finish_init_stmts (bool, tree, tree);
static void construct_virtual_base (tree, tree);
static void expand_aggr_init_1 (tree, tree, tree, tree, int);
static void expand_default_init (tree, tree, tree, tree, int);
static tree build_vec_delete_1 (tree, tree, tree, special_function_kind, int);
static void perform_member_init (tree, tree);
static tree build_builtin_delete_call (tree);
static int member_init_ok_or_else (tree, tree, tree);
static void expand_virtual_init (tree, tree);
static tree sort_mem_initializers (tree, tree);
static tree initializing_context (tree);
static void expand_cleanup_for_base (tree, tree);
static tree get_temp_regvar (tree, tree);
static tree dfs_initialize_vtbl_ptrs (tree, void *);
static tree build_default_init (tree, tree);
static tree build_dtor_call (tree, special_function_kind, int);
static tree build_field_list (tree, tree, int *);
static tree build_vtbl_address (tree);

/* We are about to generate some complex initialization code.
   Conceptually, it is all a single expression.  However, we may want
   to include conditionals, loops, and other such statement-level
   constructs.  Therefore, we build the initialization code inside a
   statement-expression.  This function starts such an expression.
   STMT_EXPR_P and COMPOUND_STMT_P are filled in by this function;
   pass them back to finish_init_stmts when the expression is
   complete.  */

static bool
begin_init_stmts (tree *stmt_expr_p, tree *compound_stmt_p)
{
  bool is_global = !building_stmt_tree ();

  *stmt_expr_p = begin_stmt_expr ();
  *compound_stmt_p = begin_compound_stmt (BCS_NO_SCOPE);

  return is_global;
}

/* Finish out the statement-expression begun by the previous call to
   begin_init_stmts.  Returns the statement-expression itself.  */

static tree
finish_init_stmts (bool is_global, tree stmt_expr, tree compound_stmt)
{
  finish_compound_stmt (compound_stmt);

  stmt_expr = finish_stmt_expr (stmt_expr, true);

  gcc_assert (!building_stmt_tree () == is_global);

  return stmt_expr;
}

/* Constructors */

/* Called from initialize_vtbl_ptrs via dfs_walk.  BINFO is the base
   which we want to initialize the vtable pointer for, DATA is
   TREE_LIST whose TREE_VALUE is the this ptr expression.  */

static tree
dfs_initialize_vtbl_ptrs (tree binfo, void *data)
{
  if (!TYPE_CONTAINS_VPTR_P (BINFO_TYPE (binfo)))
    return dfs_skip_bases;

  if (!BINFO_PRIMARY_P (binfo) || BINFO_VIRTUAL_P (binfo))
    {
      tree base_ptr = TREE_VALUE ((tree) data);

      base_ptr = build_base_path (PLUS_EXPR, base_ptr, binfo, /*nonnull=*/1);

      expand_virtual_init (binfo, base_ptr);
    }

  return NULL_TREE;
}

/* Initialize all the vtable pointers in the object pointed to by
   ADDR.  */

void
initialize_vtbl_ptrs (tree addr)
{
  tree list;
  tree type;

  type = TREE_TYPE (TREE_TYPE (addr));
  list = build_tree_list (type, addr);

  /* Walk through the hierarchy, initializing the vptr in each base
     class.  We do these in pre-order because we can't find the virtual
     bases for a class until we've initialized the vtbl for that
     class.  */
  dfs_walk_once (TYPE_BINFO (type), dfs_initialize_vtbl_ptrs, NULL, list);
}

/* Return an expression for the zero-initialization of an object with
   type T.  This expression will either be a constant (in the case
   that T is a scalar), or a CONSTRUCTOR (in the case that T is an
   aggregate).  In either case, the value can be used as DECL_INITIAL
   for a decl of the indicated TYPE; it is a valid static initializer.
   If NELTS is non-NULL, and TYPE is an ARRAY_TYPE, NELTS is the
   number of elements in the array.  If STATIC_STORAGE_P is TRUE,
   initializers are only generated for entities for which
   zero-initialization does not simply mean filling the storage with
   zero bytes.  */

tree
build_zero_init (tree type, tree nelts, bool static_storage_p)
{
  tree init = NULL_TREE;

  /* [dcl.init]

     To zero-initialization storage for an object of type T means:

     -- if T is a scalar type, the storage is set to the value of zero
	converted to T.

     -- if T is a non-union class type, the storage for each nonstatic
	data member and each base-class subobject is zero-initialized.

     -- if T is a union type, the storage for its first data member is
	zero-initialized.

     -- if T is an array type, the storage for each element is
	zero-initialized.

     -- if T is a reference type, no initialization is performed.  */

  gcc_assert (nelts == NULL_TREE || TREE_CODE (nelts) == INTEGER_CST);

  if (type == error_mark_node)
    ;
  else if (static_storage_p && zero_init_p (type))
    /* In order to save space, we do not explicitly build initializers
       for items that do not need them.  GCC's semantics are that
       items with static storage duration that are not otherwise
       initialized are initialized to zero.  */
    ;
  else if (SCALAR_TYPE_P (type))
    init = convert (type, integer_zero_node);
  else if (CLASS_TYPE_P (type))
    {
      tree field;
      VEC(constructor_elt,gc) *v = NULL;

      /* Iterate over the fields, building initializations.  */
      for (field = TYPE_FIELDS (type); field; field = TREE_CHAIN (field))
	{
	  if (TREE_CODE (field) != FIELD_DECL)
	    continue;

	  /* Note that for class types there will be FIELD_DECLs
	     corresponding to base classes as well.  Thus, iterating
	     over TYPE_FIELDs will result in correct initialization of
	     all of the subobjects.  */
	  if (!static_storage_p || !zero_init_p (TREE_TYPE (field)))
	    {
	      tree value = build_zero_init (TREE_TYPE (field),
					    /*nelts=*/NULL_TREE,
					    static_storage_p);
	      CONSTRUCTOR_APPEND_ELT(v, field, value);
	    }

	  /* For unions, only the first field is initialized.  */
	  if (TREE_CODE (type) == UNION_TYPE)
	    break;
	}

	/* Build a constructor to contain the initializations.  */
	init = build_constructor (type, v);
    }
  else if (TREE_CODE (type) == ARRAY_TYPE)
    {
      tree max_index;
      VEC(constructor_elt,gc) *v = NULL;

      /* Iterate over the array elements, building initializations.  */
      if (nelts)
	max_index = fold_build2 (MINUS_EXPR, TREE_TYPE (nelts),
				 nelts, integer_one_node);
      else
	max_index = array_type_nelts (type);

      /* If we have an error_mark here, we should just return error mark
	 as we don't know the size of the array yet.  */
      if (max_index == error_mark_node)
	return error_mark_node;
      gcc_assert (TREE_CODE (max_index) == INTEGER_CST);

      /* A zero-sized array, which is accepted as an extension, will
	 have an upper bound of -1.  */
      if (!tree_int_cst_equal (max_index, integer_minus_one_node))
	{
	  constructor_elt *ce;

	  v = VEC_alloc (constructor_elt, gc, 1);
	  ce = VEC_quick_push (constructor_elt, v, NULL);

	  /* If this is a one element array, we just use a regular init.  */
	  if (tree_int_cst_equal (size_zero_node, max_index))
	    ce->index = size_zero_node;
	  else
	    ce->index = build2 (RANGE_EXPR, sizetype, size_zero_node,
				max_index);

	  ce->value = build_zero_init (TREE_TYPE (type),
				       /*nelts=*/NULL_TREE,
				       static_storage_p);
	}

      /* Build a constructor to contain the initializations.  */
      init = build_constructor (type, v);
    }
  else if (TREE_CODE (type) == VECTOR_TYPE)
    init = fold_convert (type, integer_zero_node);
  else
    gcc_assert (TREE_CODE (type) == REFERENCE_TYPE);

  /* In all cases, the initializer is a constant.  */
  if (init)
    {
      TREE_CONSTANT (init) = 1;
      TREE_INVARIANT (init) = 1;
    }

  return init;
}

/* Build an expression for the default-initialization of an object of
   the indicated TYPE.  If NELTS is non-NULL, and TYPE is an
   ARRAY_TYPE, NELTS is the number of elements in the array.  If
   initialization of TYPE requires calling constructors, this function
   returns NULL_TREE; the caller is responsible for arranging for the
   constructors to be called.  */

static tree
build_default_init (tree type, tree nelts)
{
  /* [dcl.init]:

    To default-initialize an object of type T means:

    --if T is a non-POD class type (clause _class_), the default construc-
      tor  for  T is called (and the initialization is ill-formed if T has
      no accessible default constructor);

    --if T is an array type, each element is default-initialized;

    --otherwise, the storage for the object is zero-initialized.

    A program that calls for default-initialization of an entity of refer-
    ence type is ill-formed.  */

  /* If TYPE_NEEDS_CONSTRUCTING is true, the caller is responsible for
     performing the initialization.  This is confusing in that some
     non-PODs do not have TYPE_NEEDS_CONSTRUCTING set.  (For example,
     a class with a pointer-to-data member as a non-static data member
     does not have TYPE_NEEDS_CONSTRUCTING set.)  Therefore, we end up
     passing non-PODs to build_zero_init below, which is contrary to
     the semantics quoted above from [dcl.init].

     It happens, however, that the behavior of the constructor the
     standard says we should have generated would be precisely the
     same as that obtained by calling build_zero_init below, so things
     work out OK.  */
  if (TYPE_NEEDS_CONSTRUCTING (type)
      || (nelts && TREE_CODE (nelts) != INTEGER_CST))
    return NULL_TREE;

  /* At this point, TYPE is either a POD class type, an array of POD
     classes, or something even more innocuous.  */
  return build_zero_init (type, nelts, /*static_storage_p=*/false);
}

/* Initialize MEMBER, a FIELD_DECL, with INIT, a TREE_LIST of
   arguments.  If TREE_LIST is void_type_node, an empty initializer
   list was given; if NULL_TREE no initializer was given.  */

static void
perform_member_init (tree member, tree init)
{
  tree decl;
  tree type = TREE_TYPE (member);
  bool explicit;

  explicit = (init != NULL_TREE);

  /* Effective C++ rule 12 requires that all data members be
     initialized.  */
  if (warn_ecpp && !explicit && TREE_CODE (type) != ARRAY_TYPE)
    warning (OPT_Weffc__, "%J%qD should be initialized in the member initialization "
	     "list", current_function_decl, member);

  if (init == void_type_node)
    init = NULL_TREE;

  /* Get an lvalue for the data member.  */
  decl = build_class_member_access_expr (current_class_ref, member,
					 /*access_path=*/NULL_TREE,
					 /*preserve_reference=*/true);
  if (decl == error_mark_node)
    return;

  /* Deal with this here, as we will get confused if we try to call the
     assignment op for an anonymous union.  This can happen in a
     synthesized copy constructor.  */
  if (ANON_AGGR_TYPE_P (type))
    {
      if (init)
	{
	  init = build2 (INIT_EXPR, type, decl, TREE_VALUE (init));
	  finish_expr_stmt (init);
	}
    }
  else if (TYPE_NEEDS_CONSTRUCTING (type))
    {
      if (explicit
	  && TREE_CODE (type) == ARRAY_TYPE
	  && init != NULL_TREE
	  && TREE_CHAIN (init) == NULL_TREE
	  && TREE_CODE (TREE_TYPE (TREE_VALUE (init))) == ARRAY_TYPE)
	{
	  /* Initialization of one array from another.  */
	  finish_expr_stmt (build_vec_init (decl, NULL_TREE, TREE_VALUE (init),
					    /*explicit_default_init_p=*/false,
					    /* from_array=*/1));
	}
      else
	finish_expr_stmt (build_aggr_init (decl, init, 0));
    }
  else
    {
      if (init == NULL_TREE)
	{
	  if (explicit)
	    {
	      init = build_default_init (type, /*nelts=*/NULL_TREE);
	      if (TREE_CODE (type) == REFERENCE_TYPE)
		warning (0, "%Jdefault-initialization of %q#D, "
			 "which has reference type",
			 current_function_decl, member);
	    }
	  /* member traversal: note it leaves init NULL */
	  else if (TREE_CODE (type) == REFERENCE_TYPE)
	    pedwarn ("%Juninitialized reference member %qD",
		     current_function_decl, member);
	  else if (CP_TYPE_CONST_P (type))
	    pedwarn ("%Juninitialized member %qD with %<const%> type %qT",
		     current_function_decl, member, type);
	}
      else if (TREE_CODE (init) == TREE_LIST)
	/* There was an explicit member initialization.  Do some work
	   in that case.  */
	init = build_x_compound_expr_from_list (init, "member initializer");

      if (init)
	finish_expr_stmt (build_modify_expr (decl, INIT_EXPR, init));
    }

  if (TYPE_HAS_NONTRIVIAL_DESTRUCTOR (type))
    {
      tree expr;

      expr = build_class_member_access_expr (current_class_ref, member,
					     /*access_path=*/NULL_TREE,
					     /*preserve_reference=*/false);
      expr = build_delete (type, expr, sfk_complete_destructor,
			   LOOKUP_NONVIRTUAL|LOOKUP_DESTRUCTOR, 0);

      if (expr != error_mark_node)
	finish_eh_cleanup (expr);
    }
}

/* Returns a TREE_LIST containing (as the TREE_PURPOSE of each node) all
   the FIELD_DECLs on the TYPE_FIELDS list for T, in reverse order.  */

static tree
build_field_list (tree t, tree list, int *uses_unions_p)
{
  tree fields;

  *uses_unions_p = 0;

  /* Note whether or not T is a union.  */
  if (TREE_CODE (t) == UNION_TYPE)
    *uses_unions_p = 1;

  for (fields = TYPE_FIELDS (t); fields; fields = TREE_CHAIN (fields))
    {
      /* Skip CONST_DECLs for enumeration constants and so forth.  */
      if (TREE_CODE (fields) != FIELD_DECL || DECL_ARTIFICIAL (fields))
	continue;

      /* Keep track of whether or not any fields are unions.  */
      if (TREE_CODE (TREE_TYPE (fields)) == UNION_TYPE)
	*uses_unions_p = 1;

      /* For an anonymous struct or union, we must recursively
	 consider the fields of the anonymous type.  They can be
	 directly initialized from the constructor.  */
      if (ANON_AGGR_TYPE_P (TREE_TYPE (fields)))
	{
	  /* Add this field itself.  Synthesized copy constructors
	     initialize the entire aggregate.  */
	  list = tree_cons (fields, NULL_TREE, list);
	  /* And now add the fields in the anonymous aggregate.  */
	  list = build_field_list (TREE_TYPE (fields), list,
				   uses_unions_p);
	}
      /* Add this field.  */
      else if (DECL_NAME (fields))
	list = tree_cons (fields, NULL_TREE, list);
    }

  return list;
}

/* The MEM_INITS are a TREE_LIST.  The TREE_PURPOSE of each list gives
   a FIELD_DECL or BINFO in T that needs initialization.  The
   TREE_VALUE gives the initializer, or list of initializer arguments.

   Return a TREE_LIST containing all of the initializations required
   for T, in the order in which they should be performed.  The output
   list has the same format as the input.  */

static tree
sort_mem_initializers (tree t, tree mem_inits)
{
  tree init;
  tree base, binfo, base_binfo;
  tree sorted_inits;
  tree next_subobject;
  VEC(tree,gc) *vbases;
  int i;
  int uses_unions_p;

  /* Build up a list of initializations.  The TREE_PURPOSE of entry
     will be the subobject (a FIELD_DECL or BINFO) to initialize.  The
     TREE_VALUE will be the constructor arguments, or NULL if no
     explicit initialization was provided.  */
  sorted_inits = NULL_TREE;

  /* Process the virtual bases.  */
  for (vbases = CLASSTYPE_VBASECLASSES (t), i = 0;
       VEC_iterate (tree, vbases, i, base); i++)
    sorted_inits = tree_cons (base, NULL_TREE, sorted_inits);

  /* Process the direct bases.  */
  for (binfo = TYPE_BINFO (t), i = 0;
       BINFO_BASE_ITERATE (binfo, i, base_binfo); ++i)
    if (!BINFO_VIRTUAL_P (base_binfo))
      sorted_inits = tree_cons (base_binfo, NULL_TREE, sorted_inits);

  /* Process the non-static data members.  */
  sorted_inits = build_field_list (t, sorted_inits, &uses_unions_p);
  /* Reverse the entire list of initializations, so that they are in
     the order that they will actually be performed.  */
  sorted_inits = nreverse (sorted_inits);

  /* If the user presented the initializers in an order different from
     that in which they will actually occur, we issue a warning.  Keep
     track of the next subobject which can be explicitly initialized
     without issuing a warning.  */
  next_subobject = sorted_inits;

  /* Go through the explicit initializers, filling in TREE_PURPOSE in
     the SORTED_INITS.  */
  for (init = mem_inits; init; init = TREE_CHAIN (init))
    {
      tree subobject;
      tree subobject_init;

      subobject = TREE_PURPOSE (init);

      /* If the explicit initializers are in sorted order, then
	 SUBOBJECT will be NEXT_SUBOBJECT, or something following
	 it.  */
      for (subobject_init = next_subobject;
	   subobject_init;
	   subobject_init = TREE_CHAIN (subobject_init))
	if (TREE_PURPOSE (subobject_init) == subobject)
	  break;

      /* Issue a warning if the explicit initializer order does not
	 match that which will actually occur.
	 ??? Are all these on the correct lines?  */
      if (warn_reorder && !subobject_init)
	{
	  if (TREE_CODE (TREE_PURPOSE (next_subobject)) == FIELD_DECL)
	    warning (OPT_Wreorder, "%q+D will be initialized after",
		     TREE_PURPOSE (next_subobject));
	  else
	    warning (OPT_Wreorder, "base %qT will be initialized after",
		     TREE_PURPOSE (next_subobject));
	  if (TREE_CODE (subobject) == FIELD_DECL)
	    warning (OPT_Wreorder, "  %q+#D", subobject);
	  else
	    warning (OPT_Wreorder, "  base %qT", subobject);
	  warning (OPT_Wreorder, "%J  when initialized here", current_function_decl);
	}

      /* Look again, from the beginning of the list.  */
      if (!subobject_init)
	{
	  subobject_init = sorted_inits;
	  while (TREE_PURPOSE (subobject_init) != subobject)
	    subobject_init = TREE_CHAIN (subobject_init);
	}

      /* It is invalid to initialize the same subobject more than
	 once.  */
      if (TREE_VALUE (subobject_init))
	{
	  if (TREE_CODE (subobject) == FIELD_DECL)
	    error ("%Jmultiple initializations given for %qD",
		   current_function_decl, subobject);
	  else
	    error ("%Jmultiple initializations given for base %qT",
		   current_function_decl, subobject);
	}

      /* Record the initialization.  */
      TREE_VALUE (subobject_init) = TREE_VALUE (init);
      next_subobject = subobject_init;
    }

  /* [class.base.init]

     If a ctor-initializer specifies more than one mem-initializer for
     multiple members of the same union (including members of
     anonymous unions), the ctor-initializer is ill-formed.  */
  if (uses_unions_p)
    {
      tree last_field = NULL_TREE;
      for (init = sorted_inits; init; init = TREE_CHAIN (init))
	{
	  tree field;
	  tree field_type;
	  int done;

	  /* Skip uninitialized members and base classes.  */
	  if (!TREE_VALUE (init)
	      || TREE_CODE (TREE_PURPOSE (init)) != FIELD_DECL)
	    continue;
	  /* See if this field is a member of a union, or a member of a
	     structure contained in a union, etc.  */
	  field = TREE_PURPOSE (init);
	  for (field_type = DECL_CONTEXT (field);
	       !same_type_p (field_type, t);
	       field_type = TYPE_CONTEXT (field_type))
	    if (TREE_CODE (field_type) == UNION_TYPE)
	      break;
	  /* If this field is not a member of a union, skip it.  */
	  if (TREE_CODE (field_type) != UNION_TYPE)
	    continue;

	  /* It's only an error if we have two initializers for the same
	     union type.  */
	  if (!last_field)
	    {
	      last_field = field;
	      continue;
	    }

	  /* See if LAST_FIELD and the field initialized by INIT are
	     members of the same union.  If so, there's a problem,
	     unless they're actually members of the same structure
	     which is itself a member of a union.  For example, given:

	       union { struct { int i; int j; }; };

	     initializing both `i' and `j' makes sense.  */
	  field_type = DECL_CONTEXT (field);
	  done = 0;
	  do
	    {
	      tree last_field_type;

	      last_field_type = DECL_CONTEXT (last_field);
	      while (1)
		{
		  if (same_type_p (last_field_type, field_type))
		    {
		      if (TREE_CODE (field_type) == UNION_TYPE)
			error ("%Jinitializations for multiple members of %qT",
			       current_function_decl, last_field_type);
		      done = 1;
		      break;
		    }

		  if (same_type_p (last_field_type, t))
		    break;

		  last_field_type = TYPE_CONTEXT (last_field_type);
		}

	      /* If we've reached the outermost class, then we're
		 done.  */
	      if (same_type_p (field_type, t))
		break;

	      field_type = TYPE_CONTEXT (field_type);
	    }
	  while (!done);

	  last_field = field;
	}
    }

  return sorted_inits;
}

/* Initialize all bases and members of CURRENT_CLASS_TYPE.  MEM_INITS
   is a TREE_LIST giving the explicit mem-initializer-list for the
   constructor.  The TREE_PURPOSE of each entry is a subobject (a
   FIELD_DECL or a BINFO) of the CURRENT_CLASS_TYPE.  The TREE_VALUE
   is a TREE_LIST giving the arguments to the constructor or
   void_type_node for an empty list of arguments.  */

void
emit_mem_initializers (tree mem_inits)
{
  /* We will already have issued an error message about the fact that
     the type is incomplete.  */
  if (!COMPLETE_TYPE_P (current_class_type))
    return;

  /* Sort the mem-initializers into the order in which the
     initializations should be performed.  */
  mem_inits = sort_mem_initializers (current_class_type, mem_inits);

  in_base_initializer = 1;

  /* Initialize base classes.  */
  while (mem_inits
	 && TREE_CODE (TREE_PURPOSE (mem_inits)) != FIELD_DECL)
    {
      tree subobject = TREE_PURPOSE (mem_inits);
      tree arguments = TREE_VALUE (mem_inits);

      /* If these initializations are taking place in a copy
	 constructor, the base class should probably be explicitly
	 initialized.  */
      if (extra_warnings && !arguments
	  && DECL_COPY_CONSTRUCTOR_P (current_function_decl)
	  && TYPE_NEEDS_CONSTRUCTING (BINFO_TYPE (subobject)))
	warning (OPT_Wextra, "%Jbase class %q#T should be explicitly initialized in the "
		 "copy constructor",
		 current_function_decl, BINFO_TYPE (subobject));

      /* If an explicit -- but empty -- initializer list was present,
	 treat it just like default initialization at this point.  */
      if (arguments == void_type_node)
	arguments = NULL_TREE;

      /* Initialize the base.  */
      if (BINFO_VIRTUAL_P (subobject))
	construct_virtual_base (subobject, arguments);
      else
	{
	  tree base_addr;

	  base_addr = build_base_path (PLUS_EXPR, current_class_ptr,
				       subobject, 1);
	  expand_aggr_init_1 (subobject, NULL_TREE,
			      build_indirect_ref (base_addr, NULL),
			      arguments,
			      LOOKUP_NORMAL);
	  expand_cleanup_for_base (subobject, NULL_TREE);
	}

      mem_inits = TREE_CHAIN (mem_inits);
    }
  in_base_initializer = 0;

  /* Initialize the vptrs.  */
  initialize_vtbl_ptrs (current_class_ptr);

  /* Initialize the data members.  */
  while (mem_inits)
    {
      perform_member_init (TREE_PURPOSE (mem_inits),
			   TREE_VALUE (mem_inits));
      mem_inits = TREE_CHAIN (mem_inits);
    }
}

/* Returns the address of the vtable (i.e., the value that should be
   assigned to the vptr) for BINFO.  */

static tree
build_vtbl_address (tree binfo)
{
  tree binfo_for = binfo;
  tree vtbl;

  if (BINFO_VPTR_INDEX (binfo) && BINFO_VIRTUAL_P (binfo))
    /* If this is a virtual primary base, then the vtable we want to store
       is that for the base this is being used as the primary base of.  We
       can't simply skip the initialization, because we may be expanding the
       inits of a subobject constructor where the virtual base layout
       can be different.  */
    while (BINFO_PRIMARY_P (binfo_for))
      binfo_for = BINFO_INHERITANCE_CHAIN (binfo_for);

  /* Figure out what vtable BINFO's vtable is based on, and mark it as
     used.  */
  vtbl = get_vtbl_decl_for_binfo (binfo_for);
  assemble_external (vtbl);
  TREE_USED (vtbl) = 1;

  /* Now compute the address to use when initializing the vptr.  */
  vtbl = unshare_expr (BINFO_VTABLE (binfo_for));
  if (TREE_CODE (vtbl) == VAR_DECL)
    vtbl = build1 (ADDR_EXPR, build_pointer_type (TREE_TYPE (vtbl)), vtbl);

  return vtbl;
}

/* This code sets up the virtual function tables appropriate for
   the pointer DECL.  It is a one-ply initialization.

   BINFO is the exact type that DECL is supposed to be.  In
   multiple inheritance, this might mean "C's A" if C : A, B.  */

static void
expand_virtual_init (tree binfo, tree decl)
{
  tree vtbl, vtbl_ptr;
  tree vtt_index;

  /* Compute the initializer for vptr.  */
  vtbl = build_vtbl_address (binfo);

  /* We may get this vptr from a VTT, if this is a subobject
     constructor or subobject destructor.  */
  vtt_index = BINFO_VPTR_INDEX (binfo);
  if (vtt_index)
    {
      tree vtbl2;
      tree vtt_parm;

      /* Compute the value to use, when there's a VTT.  */
      vtt_parm = current_vtt_parm;
      vtbl2 = build2 (PLUS_EXPR,
		      TREE_TYPE (vtt_parm),
		      vtt_parm,
		      vtt_index);
      vtbl2 = build_indirect_ref (vtbl2, NULL);
      vtbl2 = convert (TREE_TYPE (vtbl), vtbl2);

      /* The actual initializer is the VTT value only in the subobject
	 constructor.  In maybe_clone_body we'll substitute NULL for
	 the vtt_parm in the case of the non-subobject constructor.  */
      vtbl = build3 (COND_EXPR,
		     TREE_TYPE (vtbl),
		     build2 (EQ_EXPR, boolean_type_node,
			     current_in_charge_parm, integer_zero_node),
		     vtbl2,
		     vtbl);
    }

  /* Compute the location of the vtpr.  */
  vtbl_ptr = build_vfield_ref (build_indirect_ref (decl, NULL),
			       TREE_TYPE (binfo));
  gcc_assert (vtbl_ptr != error_mark_node);

  /* Assign the vtable to the vptr.  */
  vtbl = convert_force (TREE_TYPE (vtbl_ptr), vtbl, 0);
  finish_expr_stmt (build_modify_expr (vtbl_ptr, NOP_EXPR, vtbl));
}

/* If an exception is thrown in a constructor, those base classes already
   constructed must be destroyed.  This function creates the cleanup
   for BINFO, which has just been constructed.  If FLAG is non-NULL,
   it is a DECL which is nonzero when this base needs to be
   destroyed.  */

static void
expand_cleanup_for_base (tree binfo, tree flag)
{
  tree expr;

  if (TYPE_HAS_TRIVIAL_DESTRUCTOR (BINFO_TYPE (binfo)))
    return;

  /* Call the destructor.  */
  expr = build_special_member_call (current_class_ref,
				    base_dtor_identifier,
				    NULL_TREE,
				    binfo,
				    LOOKUP_NORMAL | LOOKUP_NONVIRTUAL);
  if (flag)
    expr = fold_build3 (COND_EXPR, void_type_node,
			c_common_truthvalue_conversion (flag),
			expr, integer_zero_node);

  finish_eh_cleanup (expr);
}

/* Construct the virtual base-class VBASE passing the ARGUMENTS to its
   constructor.  */

static void
construct_virtual_base (tree vbase, tree arguments)
{
  tree inner_if_stmt;
  tree exp;
  tree flag;

  /* If there are virtual base classes with destructors, we need to
     emit cleanups to destroy them if an exception is thrown during
     the construction process.  These exception regions (i.e., the
     period during which the cleanups must occur) begin from the time
     the construction is complete to the end of the function.  If we
     create a conditional block in which to initialize the
     base-classes, then the cleanup region for the virtual base begins
     inside a block, and ends outside of that block.  This situation
     confuses the sjlj exception-handling code.  Therefore, we do not
     create a single conditional block, but one for each
     initialization.  (That way the cleanup regions always begin
     in the outer block.)  We trust the back-end to figure out
     that the FLAG will not change across initializations, and
     avoid doing multiple tests.  */
  flag = TREE_CHAIN (DECL_ARGUMENTS (current_function_decl));
  inner_if_stmt = begin_if_stmt ();
  finish_if_stmt_cond (flag, inner_if_stmt);

  /* Compute the location of the virtual base.  If we're
     constructing virtual bases, then we must be the most derived
     class.  Therefore, we don't have to look up the virtual base;
     we already know where it is.  */
  exp = convert_to_base_statically (current_class_ref, vbase);

  expand_aggr_init_1 (vbase, current_class_ref, exp, arguments,
		      LOOKUP_COMPLAIN);
  finish_then_clause (inner_if_stmt);
  finish_if_stmt (inner_if_stmt);

  expand_cleanup_for_base (vbase, flag);
}

/* Find the context in which this FIELD can be initialized.  */

static tree
initializing_context (tree field)
{
  tree t = DECL_CONTEXT (field);

  /* Anonymous union members can be initialized in the first enclosing
     non-anonymous union context.  */
  while (t && ANON_AGGR_TYPE_P (t))
    t = TYPE_CONTEXT (t);
  return t;
}

/* Function to give error message if member initialization specification
   is erroneous.  FIELD is the member we decided to initialize.
   TYPE is the type for which the initialization is being performed.
   FIELD must be a member of TYPE.

   MEMBER_NAME is the name of the member.  */

static int
member_init_ok_or_else (tree field, tree type, tree member_name)
{
  if (field == error_mark_node)
    return 0;
  if (!field)
    {
      error ("class %qT does not have any field named %qD", type,
	     member_name);
      return 0;
    }
  if (TREE_CODE (field) == VAR_DECL)
    {
      error ("%q#D is a static data member; it can only be "
	     "initialized at its definition",
	     field);
      return 0;
    }
  if (TREE_CODE (field) != FIELD_DECL)
    {
      error ("%q#D is not a non-static data member of %qT",
	     field, type);
      return 0;
    }
  if (initializing_context (field) != type)
    {
      error ("class %qT does not have any field named %qD", type,
		member_name);
      return 0;
    }

  return 1;
}

/* NAME is a FIELD_DECL, an IDENTIFIER_NODE which names a field, or it
   is a _TYPE node or TYPE_DECL which names a base for that type.
   Check the validity of NAME, and return either the base _TYPE, base
   binfo, or the FIELD_DECL of the member.  If NAME is invalid, return
   NULL_TREE and issue a diagnostic.

   An old style unnamed direct single base construction is permitted,
   where NAME is NULL.  */

tree
expand_member_init (tree name)
{
  tree basetype;
  tree field;

  if (!current_class_ref)
    return NULL_TREE;

  if (!name)
    {
      /* This is an obsolete unnamed base class initializer.  The
	 parser will already have warned about its use.  */
      switch (BINFO_N_BASE_BINFOS (TYPE_BINFO (current_class_type)))
	{
	case 0:
	  error ("unnamed initializer for %qT, which has no base classes",
		 current_class_type);
	  return NULL_TREE;
	case 1:
	  basetype = BINFO_TYPE
	    (BINFO_BASE_BINFO (TYPE_BINFO (current_class_type), 0));
	  break;
	default:
	  error ("unnamed initializer for %qT, which uses multiple inheritance",
		 current_class_type);
	  return NULL_TREE;
      }
    }
  else if (TYPE_P (name))
    {
      basetype = TYPE_MAIN_VARIANT (name);
      name = TYPE_NAME (name);
    }
  else if (TREE_CODE (name) == TYPE_DECL)
    basetype = TYPE_MAIN_VARIANT (TREE_TYPE (name));
  else
    basetype = NULL_TREE;

  if (basetype)
    {
      tree class_binfo;
      tree direct_binfo;
      tree virtual_binfo;
      int i;

      if (current_template_parms)
	return basetype;

      class_binfo = TYPE_BINFO (current_class_type);
      direct_binfo = NULL_TREE;
      virtual_binfo = NULL_TREE;

      /* Look for a direct base.  */
      for (i = 0; BINFO_BASE_ITERATE (class_binfo, i, direct_binfo); ++i)
	if (SAME_BINFO_TYPE_P (BINFO_TYPE (direct_binfo), basetype))
	  break;

      /* Look for a virtual base -- unless the direct base is itself
	 virtual.  */
      if (!direct_binfo || !BINFO_VIRTUAL_P (direct_binfo))
	virtual_binfo = binfo_for_vbase (basetype, current_class_type);

      /* [class.base.init]

	 If a mem-initializer-id is ambiguous because it designates
	 both a direct non-virtual base class and an inherited virtual
	 base class, the mem-initializer is ill-formed.  */
      if (direct_binfo && virtual_binfo)
	{
	  error ("%qD is both a direct base and an indirect virtual base",
		 basetype);
	  return NULL_TREE;
	}

      if (!direct_binfo && !virtual_binfo)
	{
	  if (CLASSTYPE_VBASECLASSES (current_class_type))
	    error ("type %qT is not a direct or virtual base of %qT",
		   basetype, current_class_type);
	  else
	    error ("type %qT is not a direct base of %qT",
		   basetype, current_class_type);
	  return NULL_TREE;
	}

      return direct_binfo ? direct_binfo : virtual_binfo;
    }
  else
    {
      if (TREE_CODE (name) == IDENTIFIER_NODE)
	field = lookup_field (current_class_type, name, 1, false);
      else
	field = name;

      if (member_init_ok_or_else (field, current_class_type, name))
	return field;
    }

  return NULL_TREE;
}

/* This is like `expand_member_init', only it stores one aggregate
   value into another.

   INIT comes in two flavors: it is either a value which
   is to be stored in EXP, or it is a parameter list
   to go to a constructor, which will operate on EXP.
   If INIT is not a parameter list for a constructor, then set
   LOOKUP_ONLYCONVERTING.
   If FLAGS is LOOKUP_ONLYCONVERTING then it is the = init form of
   the initializer, if FLAGS is 0, then it is the (init) form.
   If `init' is a CONSTRUCTOR, then we emit a warning message,
   explaining that such initializations are invalid.

   If INIT resolves to a CALL_EXPR which happens to return
   something of the type we are looking for, then we know
   that we can safely use that call to perform the
   initialization.

   The virtual function table pointer cannot be set up here, because
   we do not really know its type.

   This never calls operator=().

   When initializing, nothing is CONST.

   A default copy constructor may have to be used to perform the
   initialization.

   A constructor or a conversion operator may have to be used to
   perform the initialization, but not both, as it would be ambiguous.  */

tree
build_aggr_init (tree exp, tree init, int flags)
{
  tree stmt_expr;
  tree compound_stmt;
  int destroy_temps;
  tree type = TREE_TYPE (exp);
  int was_const = TREE_READONLY (exp);
  int was_volatile = TREE_THIS_VOLATILE (exp);
  int is_global;

  if (init == error_mark_node)
    return error_mark_node;

  TREE_READONLY (exp) = 0;
  TREE_THIS_VOLATILE (exp) = 0;

  if (init && TREE_CODE (init) != TREE_LIST)
    flags |= LOOKUP_ONLYCONVERTING;

  if (TREE_CODE (type) == ARRAY_TYPE)
    {
      tree itype;

      /* An array may not be initialized use the parenthesized
	 initialization form -- unless the initializer is "()".  */
      if (init && TREE_CODE (init) == TREE_LIST)
	{
	  error ("bad array initializer");
	  return error_mark_node;
	}
      /* Must arrange to initialize each element of EXP
	 from elements of INIT.  */
      itype = init ? TREE_TYPE (init) : NULL_TREE;
      if (cp_type_quals (type) != TYPE_UNQUALIFIED)
	TREE_TYPE (exp) = TYPE_MAIN_VARIANT (type);
      if (itype && cp_type_quals (itype) != TYPE_UNQUALIFIED)
	itype = TREE_TYPE (init) = TYPE_MAIN_VARIANT (itype);
      stmt_expr = build_vec_init (exp, NULL_TREE, init,
				  /*explicit_default_init_p=*/false,
				  itype && same_type_p (itype,
							TREE_TYPE (exp)));
      TREE_READONLY (exp) = was_const;
      TREE_THIS_VOLATILE (exp) = was_volatile;
      TREE_TYPE (exp) = type;
      if (init)
	TREE_TYPE (init) = itype;
      return stmt_expr;
    }

  if (TREE_CODE (exp) == VAR_DECL || TREE_CODE (exp) == PARM_DECL)
    /* Just know that we've seen something for this node.  */
    TREE_USED (exp) = 1;

  TREE_TYPE (exp) = TYPE_MAIN_VARIANT (type);
  is_global = begin_init_stmts (&stmt_expr, &compound_stmt);
  destroy_temps = stmts_are_full_exprs_p ();
  current_stmt_tree ()->stmts_are_full_exprs_p = 0;
  expand_aggr_init_1 (TYPE_BINFO (type), exp, exp,
		      init, LOOKUP_NORMAL|flags);
  stmt_expr = finish_init_stmts (is_global, stmt_expr, compound_stmt);
  current_stmt_tree ()->stmts_are_full_exprs_p = destroy_temps;
  TREE_TYPE (exp) = type;
  TREE_READONLY (exp) = was_const;
  TREE_THIS_VOLATILE (exp) = was_volatile;

  return stmt_expr;
}

static void
expand_default_init (tree binfo, tree true_exp, tree exp, tree init, int flags)
{
  tree type = TREE_TYPE (exp);
  tree ctor_name;

  /* It fails because there may not be a constructor which takes
     its own type as the first (or only parameter), but which does
     take other types via a conversion.  So, if the thing initializing
     the expression is a unit element of type X, first try X(X&),
     followed by initialization by X.  If neither of these work
     out, then look hard.  */
  tree rval;
  tree parms;

  if (init && TREE_CODE (init) != TREE_LIST
      && (flags & LOOKUP_ONLYCONVERTING))
    {
      /* Base subobjects should only get direct-initialization.  */
      gcc_assert (true_exp == exp);

      if (flags & DIRECT_BIND)
	/* Do nothing.  We hit this in two cases:  Reference initialization,
	   where we aren't initializing a real variable, so we don't want
	   to run a new constructor; and catching an exception, where we
	   have already built up the constructor call so we could wrap it
	   in an exception region.  */;
      else if (BRACE_ENCLOSED_INITIALIZER_P (init))
	{
	  /* A brace-enclosed initializer for an aggregate.  */
	  gcc_assert (CP_AGGREGATE_TYPE_P (type));
	  init = digest_init (type, init);
	}
      else
	init = ocp_convert (type, init, CONV_IMPLICIT|CONV_FORCE_TEMP, flags);

      if (TREE_CODE (init) == MUST_NOT_THROW_EXPR)
	/* We need to protect the initialization of a catch parm with a
	   call to terminate(), which shows up as a MUST_NOT_THROW_EXPR
	   around the TARGET_EXPR for the copy constructor.  See
	   initialize_handler_parm.  */
	{
	  TREE_OPERAND (init, 0) = build2 (INIT_EXPR, TREE_TYPE (exp), exp,
					   TREE_OPERAND (init, 0));
	  TREE_TYPE (init) = void_type_node;
	}
      else
	init = build2 (INIT_EXPR, TREE_TYPE (exp), exp, init);
      TREE_SIDE_EFFECTS (init) = 1;
      finish_expr_stmt (init);
      return;
    }

  if (init == NULL_TREE
      || (TREE_CODE (init) == TREE_LIST && ! TREE_TYPE (init)))
    {
      parms = init;
      if (parms)
	init = TREE_VALUE (parms);
    }
  else
    parms = build_tree_list (NULL_TREE, init);

  if (true_exp == exp)
    ctor_name = complete_ctor_identifier;
  else
    ctor_name = base_ctor_identifier;

  rval = build_special_member_call (exp, ctor_name, parms, binfo, flags);
  if (TREE_SIDE_EFFECTS (rval))
    finish_expr_stmt (convert_to_void (rval, NULL));
}

/* This function is responsible for initializing EXP with INIT
   (if any).

   BINFO is the binfo of the type for who we are performing the
   initialization.  For example, if W is a virtual base class of A and B,
   and C : A, B.
   If we are initializing B, then W must contain B's W vtable, whereas
   were we initializing C, W must contain C's W vtable.

   TRUE_EXP is nonzero if it is the true expression being initialized.
   In this case, it may be EXP, or may just contain EXP.  The reason we
   need this is because if EXP is a base element of TRUE_EXP, we
   don't necessarily know by looking at EXP where its virtual
   baseclass fields should really be pointing.  But we do know
   from TRUE_EXP.  In constructors, we don't know anything about
   the value being initialized.

   FLAGS is just passed to `build_new_method_call'.  See that function
   for its description.  */

static void
expand_aggr_init_1 (tree binfo, tree true_exp, tree exp, tree init, int flags)
{
  tree type = TREE_TYPE (exp);

  gcc_assert (init != error_mark_node && type != error_mark_node);
  gcc_assert (building_stmt_tree ());

  /* Use a function returning the desired type to initialize EXP for us.
     If the function is a constructor, and its first argument is
     NULL_TREE, know that it was meant for us--just slide exp on
     in and expand the constructor.  Constructors now come
     as TARGET_EXPRs.  */

  if (init && TREE_CODE (exp) == VAR_DECL
      && COMPOUND_LITERAL_P (init))
    {
      /* If store_init_value returns NULL_TREE, the INIT has been
	 recorded as the DECL_INITIAL for EXP.  That means there's
	 nothing more we have to do.  */
      init = store_init_value (exp, init);
      if (init)
	finish_expr_stmt (init);
      return;
    }

  /* We know that expand_default_init can handle everything we want
     at this point.  */
  expand_default_init (binfo, true_exp, exp, init, flags);
}

/* Report an error if TYPE is not a user-defined, aggregate type.  If
   OR_ELSE is nonzero, give an error message.  */

int
is_aggr_type (tree type, int or_else)
{
  if (type == error_mark_node)
    return 0;

  if (! IS_AGGR_TYPE (type)
      && TREE_CODE (type) != TEMPLATE_TYPE_PARM
      && TREE_CODE (type) != BOUND_TEMPLATE_TEMPLATE_PARM)
    {
      if (or_else)
	error ("%qT is not an aggregate type", type);
      return 0;
    }
  return 1;
}

tree
get_type_value (tree name)
{
  if (name == error_mark_node)
    return NULL_TREE;

  if (IDENTIFIER_HAS_TYPE_VALUE (name))
    return IDENTIFIER_TYPE_VALUE (name);
  else
    return NULL_TREE;
}

/* Build a reference to a member of an aggregate.  This is not a C++
   `&', but really something which can have its address taken, and
   then act as a pointer to member, for example TYPE :: FIELD can have
   its address taken by saying & TYPE :: FIELD.  ADDRESS_P is true if
   this expression is the operand of "&".

   @@ Prints out lousy diagnostics for operator <typename>
   @@ fields.

   @@ This function should be rewritten and placed in search.c.  */

tree
build_offset_ref (tree type, tree member, bool address_p)
{
  tree decl;
  tree basebinfo = NULL_TREE;

  /* class templates can come in as TEMPLATE_DECLs here.  */
  if (TREE_CODE (member) == TEMPLATE_DECL)
    return member;

  if (dependent_type_p (type) || type_dependent_expression_p (member))
    return build_qualified_name (NULL_TREE, type, member,
				 /*template_p=*/false);

  gcc_assert (TYPE_P (type));
  if (! is_aggr_type (type, 1))
    return error_mark_node;

  gcc_assert (DECL_P (member) || BASELINK_P (member));
  /* Callers should call mark_used before this point.  */
  gcc_assert (!DECL_P (member) || TREE_USED (member));

  if (!COMPLETE_TYPE_P (complete_type (type))
      && !TYPE_BEING_DEFINED (type))
    {
      error ("incomplete type %qT does not have member %qD", type, member);
      return error_mark_node;
    }

  /* Entities other than non-static members need no further
     processing.  */
  if (TREE_CODE (member) == TYPE_DECL)
    return member;
  if (TREE_CODE (member) == VAR_DECL || TREE_CODE (member) == CONST_DECL)
    return convert_from_reference (member);

  if (TREE_CODE (member) == FIELD_DECL && DECL_C_BIT_FIELD (member))
    {
      error ("invalid pointer to bit-field %qD", member);
      return error_mark_node;
    }

  /* Set up BASEBINFO for member lookup.  */
  decl = maybe_dummy_object (type, &basebinfo);

  /* A lot of this logic is now handled in lookup_member.  */
  if (BASELINK_P (member))
    {
      /* Go from the TREE_BASELINK to the member function info.  */
      tree t = BASELINK_FUNCTIONS (member);

      if (TREE_CODE (t) != TEMPLATE_ID_EXPR && !really_overloaded_fn (t))
	{
	  /* Get rid of a potential OVERLOAD around it.  */
	  t = OVL_CURRENT (t);

	  /* Unique functions are handled easily.  */

	  /* For non-static member of base class, we need a special rule
	     for access checking [class.protected]:

	       If the access is to form a pointer to member, the
	       nested-name-specifier shall name the derived class
	       (or any class derived from that class).  */
	  if (address_p && DECL_P (t)
	      && DECL_NONSTATIC_MEMBER_P (t))
	    perform_or_defer_access_check (TYPE_BINFO (type), t, t);
	  else
	    perform_or_defer_access_check (basebinfo, t, t);

	  if (DECL_STATIC_FUNCTION_P (t))
	    return t;
	  member = t;
	}
      else
	TREE_TYPE (member) = unknown_type_node;
    }
  else if (address_p && TREE_CODE (member) == FIELD_DECL)
    /* We need additional test besides the one in
       check_accessibility_of_qualified_id in case it is
       a pointer to non-static member.  */
    perform_or_defer_access_check (TYPE_BINFO (type), member, member);

  if (!address_p)
    {
      /* If MEMBER is non-static, then the program has fallen afoul of
	 [expr.prim]:

	   An id-expression that denotes a nonstatic data member or
	   nonstatic member function of a class can only be used:

	   -- as part of a class member access (_expr.ref_) in which the
	   object-expression refers to the member's class or a class
	   derived from that class, or

	   -- to form a pointer to member (_expr.unary.op_), or

	   -- in the body of a nonstatic member function of that class or
	   of a class derived from that class (_class.mfct.nonstatic_), or

	   -- in a mem-initializer for a constructor for that class or for
	   a class derived from that class (_class.base.init_).  */
      if (DECL_NONSTATIC_MEMBER_FUNCTION_P (member))
	{
	  /* Build a representation of a the qualified name suitable
	     for use as the operand to "&" -- even though the "&" is
	     not actually present.  */
	  member = build2 (OFFSET_REF, TREE_TYPE (member), decl, member);
	  /* In Microsoft mode, treat a non-static member function as if
	     it were a pointer-to-member.  */
	  if (flag_ms_extensions)
	    {
	      PTRMEM_OK_P (member) = 1;
	      return build_unary_op (ADDR_EXPR, member, 0);
	    }
	  error ("invalid use of non-static member function %qD",
		 TREE_OPERAND (member, 1));
	  return error_mark_node;
	}
      else if (TREE_CODE (member) == FIELD_DECL)
	{
	  error ("invalid use of non-static data member %qD", member);
	  return error_mark_node;
	}
      return member;
    }

  member = build2 (OFFSET_REF, TREE_TYPE (member), decl, member);
  PTRMEM_OK_P (member) = 1;
  return member;
}

/* If DECL is a scalar enumeration constant or variable with a
   constant initializer, return the initializer (or, its initializers,
   recursively); otherwise, return DECL.  If INTEGRAL_P, the
   initializer is only returned if DECL is an integral
   constant-expression.  */

static tree
constant_value_1 (tree decl, bool integral_p)
{
  while (TREE_CODE (decl) == CONST_DECL
	 || (integral_p
	     ? DECL_INTEGRAL_CONSTANT_VAR_P (decl)
	     : (TREE_CODE (decl) == VAR_DECL
		&& CP_TYPE_CONST_NON_VOLATILE_P (TREE_TYPE (decl)))))
    {
      tree init;
      /* Static data members in template classes may have
	 non-dependent initializers.  References to such non-static
	 data members are not value-dependent, so we must retrieve the
	 initializer here.  The DECL_INITIAL will have the right type,
	 but will not have been folded because that would prevent us
	 from performing all appropriate semantic checks at
	 instantiation time.  */
      if (DECL_CLASS_SCOPE_P (decl)
	  && CLASSTYPE_TEMPLATE_INFO (DECL_CONTEXT (decl))
	  && uses_template_parms (CLASSTYPE_TI_ARGS
				  (DECL_CONTEXT (decl))))
	{
	  ++processing_template_decl;
	  init = fold_non_dependent_expr (DECL_INITIAL (decl));
	  --processing_template_decl;
	}
      else
	{
	  /* If DECL is a static data member in a template
	     specialization, we must instantiate it here.  The
	     initializer for the static data member is not processed
	     until needed; we need it now.  */
	  mark_used (decl);
	  init = DECL_INITIAL (decl);
	}
      if (init == error_mark_node)
	return decl;
      if (!init
	  || !TREE_TYPE (init)
	  || (integral_p
	      ? !INTEGRAL_OR_ENUMERATION_TYPE_P (TREE_TYPE (init))
	      : (!TREE_CONSTANT (init)
		 /* Do not return an aggregate constant (of which
		    string literals are a special case), as we do not
		    want to make inadvertent copies of such entities,
		    and we must be sure that their addresses are the
		    same everywhere.  */
		 || TREE_CODE (init) == CONSTRUCTOR
		 || TREE_CODE (init) == STRING_CST)))
	break;
      decl = unshare_expr (init);
    }
  return decl;
}

/* If DECL is a CONST_DECL, or a constant VAR_DECL initialized by
   constant of integral or enumeration type, then return that value.
   These are those variables permitted in constant expressions by
   [5.19/1].  */

tree
integral_constant_value (tree decl)
{
  return constant_value_1 (decl, /*integral_p=*/true);
}

/* A more relaxed version of integral_constant_value, used by the
   common C/C++ code and by the C++ front-end for optimization
   purposes.  */

tree
decl_constant_value (tree decl)
{
  return constant_value_1 (decl,
			   /*integral_p=*/processing_template_decl);
}

/* Common subroutines of build_new and build_vec_delete.  */

/* Call the global __builtin_delete to delete ADDR.  */

static tree
build_builtin_delete_call (tree addr)
{
  mark_used (global_delete_fndecl);
  return build_call (global_delete_fndecl, build_tree_list (NULL_TREE, addr));
}

/* Build and return a NEW_EXPR.  If NELTS is non-NULL, TYPE[NELTS] is
   the type of the object being allocated; otherwise, it's just TYPE.
   INIT is the initializer, if any.  USE_GLOBAL_NEW is true if the
   user explicitly wrote "::operator new".  PLACEMENT, if non-NULL, is
   the TREE_LIST of arguments to be provided as arguments to a
   placement new operator.  This routine performs no semantic checks;
   it just creates and returns a NEW_EXPR.  */

static tree
build_raw_new_expr (tree placement, tree type, tree nelts, tree init,
		    int use_global_new)
{
  tree new_expr;

  new_expr = build4 (NEW_EXPR, build_pointer_type (type), placement, type,
		     nelts, init);
  NEW_EXPR_USE_GLOBAL (new_expr) = use_global_new;
  TREE_SIDE_EFFECTS (new_expr) = 1;

  return new_expr;
}

/* Generate code for a new-expression, including calling the "operator
   new" function, initializing the object, and, if an exception occurs
   during construction, cleaning up.  The arguments are as for
   build_raw_new_expr.  */

static tree
build_new_1 (tree placement, tree type, tree nelts, tree init,
	     bool globally_qualified_p)
{
  tree size, rval;
  /* True iff this is a call to "operator new[]" instead of just
     "operator new".  */
  bool array_p = false;
  /* True iff ARRAY_P is true and the bound of the array type is
     not necessarily a compile time constant.  For example, VLA_P is
     true for "new int[f()]".  */
  bool vla_p = false;
  /* The type being allocated.  If ARRAY_P is true, this will be an
     ARRAY_TYPE.  */
  tree full_type;
  /* If ARRAY_P is true, the element type of the array.  This is an
     never ARRAY_TYPE; for something like "new int[3][4]", the
     ELT_TYPE is "int".  If ARRAY_P is false, this is the same type as
     FULL_TYPE.  */
  tree elt_type;
  /* The type of the new-expression.  (This type is always a pointer
     type.)  */
  tree pointer_type;
  /* A pointer type pointing to the FULL_TYPE.  */
  tree full_pointer_type;
  tree outer_nelts = NULL_TREE;
  tree alloc_call, alloc_expr;
  /* The address returned by the call to "operator new".  This node is
     a VAR_DECL and is therefore reusable.  */
  tree alloc_node;
  tree alloc_fn;
  tree cookie_expr, init_expr;
  int nothrow, check_new;
  int use_java_new = 0;
  /* If non-NULL, the number of extra bytes to allocate at the
     beginning of the storage allocated for an array-new expression in
     order to store the number of elements.  */
  tree cookie_size = NULL_TREE;
  /* True if the function we are calling is a placement allocation
     function.  */
  bool placement_allocation_fn_p;
  tree args = NULL_TREE;
  /* True if the storage must be initialized, either by a constructor
     or due to an explicit new-initializer.  */
  bool is_initialized;
  /* The address of the thing allocated, not including any cookie.  In
     particular, if an array cookie is in use, DATA_ADDR is the
     address of the first array element.  This node is a VAR_DECL, and
     is therefore reusable.  */
  tree data_addr;
  tree init_preeval_expr = NULL_TREE;

  if (nelts)
    {
      tree index;

      outer_nelts = nelts;
      array_p = true;

      /* ??? The middle-end will error on us for building a VLA outside a
	 function context.  Methinks that's not it's purvey.  So we'll do
	 our own VLA layout later.  */
      vla_p = true;
      index = convert (sizetype, nelts);
      index = size_binop (MINUS_EXPR, index, size_one_node);
      index = build_index_type (index);
      full_type = build_cplus_array_type (type, NULL_TREE);
      /* We need a copy of the type as build_array_type will return a shared copy
         of the incomplete array type.  */
      full_type = build_distinct_type_copy (full_type);
      TYPE_DOMAIN (full_type) = index;
    }
  else
    {
      full_type = type;
      if (TREE_CODE (type) == ARRAY_TYPE)
	{
	  array_p = true;
	  nelts = array_type_nelts_top (type);
	  outer_nelts = nelts;
	  type = TREE_TYPE (type);
	}
    }

  if (!complete_type_or_else (type, NULL_TREE))
    return error_mark_node;

  /* If our base type is an array, then make sure we know how many elements
     it has.  */
  for (elt_type = type;
       TREE_CODE (elt_type) == ARRAY_TYPE;
       elt_type = TREE_TYPE (elt_type))
    nelts = cp_build_binary_op (MULT_EXPR, nelts,
				array_type_nelts_top (elt_type));

  if (TREE_CODE (elt_type) == VOID_TYPE)
    {
      error ("invalid type %<void%> for new");
      return error_mark_node;
    }

  if (abstract_virtuals_error (NULL_TREE, elt_type))
    return error_mark_node;

  is_initialized = (TYPE_NEEDS_CONSTRUCTING (elt_type) || init);
  if (CP_TYPE_CONST_P (elt_type) && !is_initialized)
    {
      error ("uninitialized const in %<new%> of %q#T", elt_type);
      return error_mark_node;
    }

  size = size_in_bytes (elt_type);
  if (array_p)
    {
      size = size_binop (MULT_EXPR, size, convert (sizetype, nelts));
      if (vla_p)
	{
	  tree n, bitsize;

	  /* Do our own VLA layout.  Setting TYPE_SIZE/_UNIT is
	     necessary in order for the <INIT_EXPR <*foo> <CONSTRUCTOR
	     ...>> to be valid.  */
	  TYPE_SIZE_UNIT (full_type) = size;
	  n = convert (bitsizetype, nelts);
	  bitsize = size_binop (MULT_EXPR, TYPE_SIZE (elt_type), n);
	  TYPE_SIZE (full_type) = bitsize;
	}
    }

  alloc_fn = NULL_TREE;

  /* Allocate the object.  */
  if (! placement && TYPE_FOR_JAVA (elt_type))
    {
      tree class_addr;
      tree class_decl = build_java_class_ref (elt_type);
      static const char alloc_name[] = "_Jv_AllocObject";

      if (class_decl == error_mark_node)
	return error_mark_node;

      use_java_new = 1;
      if (!get_global_value_if_present (get_identifier (alloc_name),
					&alloc_fn))
	{
	  error ("call to Java constructor with %qs undefined", alloc_name);
	  return error_mark_node;
	}
      else if (really_overloaded_fn (alloc_fn))
	{
	  error ("%qD should never be overloaded", alloc_fn);
	  return error_mark_node;
	}
      alloc_fn = OVL_CURRENT (alloc_fn);
      class_addr = build1 (ADDR_EXPR, jclass_node, class_decl);
      alloc_call = (build_function_call
		    (alloc_fn,
		     build_tree_list (NULL_TREE, class_addr)));
    }
  else
    {
      tree fnname;
      tree fns;

      fnname = ansi_opname (array_p ? VEC_NEW_EXPR : NEW_EXPR);

      if (!globally_qualified_p
	  && CLASS_TYPE_P (elt_type)
	  && (array_p
	      ? TYPE_HAS_ARRAY_NEW_OPERATOR (elt_type)
	      : TYPE_HAS_NEW_OPERATOR (elt_type)))
	{
	  /* Use a class-specific operator new.  */
	  /* If a cookie is required, add some extra space.  */
	  if (array_p && TYPE_VEC_NEW_USES_COOKIE (elt_type))
	    {
	      cookie_size = targetm.cxx.get_cookie_size (elt_type);
	      size = size_binop (PLUS_EXPR, size, cookie_size);
	    }
	  /* Create the argument list.  */
	  args = tree_cons (NULL_TREE, size, placement);
	  /* Do name-lookup to find the appropriate operator.  */
	  fns = lookup_fnfields (elt_type, fnname, /*protect=*/2);
	  if (fns == NULL_TREE)
	    {
	      error ("no suitable %qD found in class %qT", fnname, elt_type);
	      return error_mark_node;
	    }
	  if (TREE_CODE (fns) == TREE_LIST)
	    {
	      error ("request for member %qD is ambiguous", fnname);
	      print_candidates (fns);
	      return error_mark_node;
	    }
	  alloc_call = build_new_method_call (build_dummy_object (elt_type),
					      fns, args,
					      /*conversion_path=*/NULL_TREE,
					      LOOKUP_NORMAL,
					      &alloc_fn);
	}
      else
	{
	  /* Use a global operator new.  */
	  /* See if a cookie might be required.  */
	  if (array_p && TYPE_VEC_NEW_USES_COOKIE (elt_type))
	    cookie_size = targetm.cxx.get_cookie_size (elt_type);
	  else
	    cookie_size = NULL_TREE;

	  alloc_call = build_operator_new_call (fnname, placement,
						&size, &cookie_size,
						&alloc_fn);
	}
    }

  if (alloc_call == error_mark_node)
    return error_mark_node;

  gcc_assert (alloc_fn != NULL_TREE);

  /* In the simple case, we can stop now.  */
  pointer_type = build_pointer_type (type);
  if (!cookie_size && !is_initialized)
    return build_nop (pointer_type, alloc_call);

  /* While we're working, use a pointer to the type we've actually
     allocated. Store the result of the call in a variable so that we
     can use it more than once.  */
  full_pointer_type = build_pointer_type (full_type);
  alloc_expr = get_target_expr (build_nop (full_pointer_type, alloc_call));
  alloc_node = TARGET_EXPR_SLOT (alloc_expr);

  /* Strip any COMPOUND_EXPRs from ALLOC_CALL.  */
  while (TREE_CODE (alloc_call) == COMPOUND_EXPR)
    alloc_call = TREE_OPERAND (alloc_call, 1);

  /* Now, check to see if this function is actually a placement
     allocation function.  This can happen even when PLACEMENT is NULL
     because we might have something like:

       struct S { void* operator new (size_t, int i = 0); };

     A call to `new S' will get this allocation function, even though
     there is no explicit placement argument.  If there is more than
     one argument, or there are variable arguments, then this is a
     placement allocation function.  */
  placement_allocation_fn_p
    = (type_num_arguments (TREE_TYPE (alloc_fn)) > 1
       || varargs_function_p (alloc_fn));

  /* Preevaluate the placement args so that we don't reevaluate them for a
     placement delete.  */
  if (placement_allocation_fn_p)
    {
      tree inits;
      stabilize_call (alloc_call, &inits);
      if (inits)
	alloc_expr = build2 (COMPOUND_EXPR, TREE_TYPE (alloc_expr), inits,
			     alloc_expr);
    }

  /*        unless an allocation function is declared with an empty  excep-
     tion-specification  (_except.spec_),  throw(), it indicates failure to
     allocate storage by throwing a bad_alloc exception  (clause  _except_,
     _lib.bad.alloc_); it returns a non-null pointer otherwise If the allo-
     cation function is declared  with  an  empty  exception-specification,
     throw(), it returns null to indicate failure to allocate storage and a
     non-null pointer otherwise.

     So check for a null exception spec on the op new we just called.  */

  nothrow = TYPE_NOTHROW_P (TREE_TYPE (alloc_fn));
  check_new = (flag_check_new || nothrow) && ! use_java_new;

  if (cookie_size)
    {
      tree cookie;
      tree cookie_ptr;

      /* Adjust so we're pointing to the start of the object.  */
      data_addr = get_target_expr (build2 (PLUS_EXPR, full_pointer_type,
					   alloc_node, cookie_size));

      /* Store the number of bytes allocated so that we can know how
	 many elements to destroy later.  We use the last sizeof
	 (size_t) bytes to store the number of elements.  */
      cookie_ptr = build2 (MINUS_EXPR, build_pointer_type (sizetype),
			   data_addr, size_in_bytes (sizetype));
      cookie = build_indirect_ref (cookie_ptr, NULL);

      cookie_expr = build2 (MODIFY_EXPR, sizetype, cookie, nelts);

      if (targetm.cxx.cookie_has_size ())
	{
	  /* Also store the element size.  */
	  cookie_ptr = build2 (MINUS_EXPR, build_pointer_type (sizetype),
			       cookie_ptr, size_in_bytes (sizetype));
	  cookie = build_indirect_ref (cookie_ptr, NULL);
	  cookie = build2 (MODIFY_EXPR, sizetype, cookie,
			   size_in_bytes(elt_type));
	  cookie_expr = build2 (COMPOUND_EXPR, TREE_TYPE (cookie_expr),
				cookie, cookie_expr);
	}
      data_addr = TARGET_EXPR_SLOT (data_addr);
    }
  else
    {
      cookie_expr = NULL_TREE;
      data_addr = alloc_node;
    }

  /* Now initialize the allocated object.  Note that we preevaluate the
     initialization expression, apart from the actual constructor call or
     assignment--we do this because we want to delay the allocation as long
     as possible in order to minimize the size of the exception region for
     placement delete.  */
  if (is_initialized)
    {
      bool stable;

      init_expr = build_indirect_ref (data_addr, NULL);

      if (array_p)
	{
	  bool explicit_default_init_p = false;

	  if (init == void_zero_node)
	    {
	      init = NULL_TREE;
	      explicit_default_init_p = true;
	    }
	  else if (init)
	    pedwarn ("ISO C++ forbids initialization in array new");

	  init_expr
	    = build_vec_init (init_expr,
			      cp_build_binary_op (MINUS_EXPR, outer_nelts,
						  integer_one_node),
			      init,
			      explicit_default_init_p,
			      /*from_array=*/0);

	  /* An array initialization is stable because the initialization
	     of each element is a full-expression, so the temporaries don't
	     leak out.  */
	  stable = true;
	}
      else
	{
	  if (init == void_zero_node)
	    init = build_default_init (full_type, nelts);

	  if (TYPE_NEEDS_CONSTRUCTING (type))
	    {
	      init_expr = build_special_member_call (init_expr,
						     complete_ctor_identifier,
						     init, elt_type,
						     LOOKUP_NORMAL);
	      stable = stabilize_init (init_expr, &init_preeval_expr);
	    }
	  else
	    {
	      /* We are processing something like `new int (10)', which
		 means allocate an int, and initialize it with 10.  */

	      if (TREE_CODE (init) == TREE_LIST)
		init = build_x_compound_expr_from_list (init,
							"new initializer");
	      else
		gcc_assert (TREE_CODE (init) != CONSTRUCTOR
			    || TREE_TYPE (init) != NULL_TREE);

	      init_expr = build_modify_expr (init_expr, INIT_EXPR, init);
	      stable = stabilize_init (init_expr, &init_preeval_expr);
	    }
	}

      if (init_expr == error_mark_node)
	return error_mark_node;

      /* If any part of the object initialization terminates by throwing an
	 exception and a suitable deallocation function can be found, the
	 deallocation function is called to free the memory in which the
	 object was being constructed, after which the exception continues
	 to propagate in the context of the new-expression. If no
	 unambiguous matching deallocation function can be found,
	 propagating the exception does not cause the object's memory to be
	 freed.  */
      if (flag_exceptions && ! use_java_new)
	{
	  enum tree_code dcode = array_p ? VEC_DELETE_EXPR : DELETE_EXPR;
	  tree cleanup;

	  /* The Standard is unclear here, but the right thing to do
	     is to use the same method for finding deallocation
	     functions that we use for finding allocation functions.  */
	  cleanup = build_op_delete_call (dcode, alloc_node, size,
					  globally_qualified_p,
					  (placement_allocation_fn_p
					   ? alloc_call : NULL_TREE),
					  alloc_fn);

	  if (!cleanup)
	    /* We're done.  */;
	  else if (stable)
	    /* This is much simpler if we were able to preevaluate all of
	       the arguments to the constructor call.  */
	    init_expr = build2 (TRY_CATCH_EXPR, void_type_node,
				init_expr, cleanup);
	  else
	    /* Ack!  First we allocate the memory.  Then we set our sentry
	       variable to true, and expand a cleanup that deletes the
	       memory if sentry is true.  Then we run the constructor, and
	       finally clear the sentry.

	       We need to do this because we allocate the space first, so
	       if there are any temporaries with cleanups in the
	       constructor args and we weren't able to preevaluate them, we
	       need this EH region to extend until end of full-expression
	       to preserve nesting.  */
	    {
	      tree end, sentry, begin;

	      begin = get_target_expr (boolean_true_node);
	      CLEANUP_EH_ONLY (begin) = 1;

	      sentry = TARGET_EXPR_SLOT (begin);

	      TARGET_EXPR_CLEANUP (begin)
		= build3 (COND_EXPR, void_type_node, sentry,
			  cleanup, void_zero_node);

	      end = build2 (MODIFY_EXPR, TREE_TYPE (sentry),
			    sentry, boolean_false_node);

	      init_expr
		= build2 (COMPOUND_EXPR, void_type_node, begin,
			  build2 (COMPOUND_EXPR, void_type_node, init_expr,
				  end));
	    }

	}
    }
  else
    init_expr = NULL_TREE;

  /* Now build up the return value in reverse order.  */

  rval = data_addr;

  if (init_expr)
    rval = build2 (COMPOUND_EXPR, TREE_TYPE (rval), init_expr, rval);
  if (cookie_expr)
    rval = build2 (COMPOUND_EXPR, TREE_TYPE (rval), cookie_expr, rval);

  if (rval == alloc_node)
    /* If we don't have an initializer or a cookie, strip the TARGET_EXPR
       and return the call (which doesn't need to be adjusted).  */
    rval = TARGET_EXPR_INITIAL (alloc_expr);
  else
    {
      if (check_new)
	{
	  tree ifexp = cp_build_binary_op (NE_EXPR, alloc_node,
					   integer_zero_node);
	  rval = build_conditional_expr (ifexp, rval, alloc_node);
	}

      /* Perform the allocation before anything else, so that ALLOC_NODE
	 has been initialized before we start using it.  */
      rval = build2 (COMPOUND_EXPR, TREE_TYPE (rval), alloc_expr, rval);
    }

  if (init_preeval_expr)
    rval = build2 (COMPOUND_EXPR, TREE_TYPE (rval), init_preeval_expr, rval);

  /* Convert to the final type.  */
  rval = build_nop (pointer_type, rval);

  /* A new-expression is never an lvalue.  */
  gcc_assert (!lvalue_p (rval));

  return rval;
}

/* Generate a representation for a C++ "new" expression.  PLACEMENT is
   a TREE_LIST of placement-new arguments (or NULL_TREE if none).  If
   NELTS is NULL, TYPE is the type of the storage to be allocated.  If
   NELTS is not NULL, then this is an array-new allocation; TYPE is
   the type of the elements in the array and NELTS is the number of
   elements in the array.  INIT, if non-NULL, is the initializer for
   the new object, or void_zero_node to indicate an initializer of
   "()".  If USE_GLOBAL_NEW is true, then the user explicitly wrote
   "::new" rather than just "new".  */

tree
build_new (tree placement, tree type, tree nelts, tree init,
	   int use_global_new)
{
  tree rval;
  tree orig_placement;
  tree orig_nelts;
  tree orig_init;

  if (placement == error_mark_node || type == error_mark_node
      || init == error_mark_node)
    return error_mark_node;

  orig_placement = placement;
  orig_nelts = nelts;
  orig_init = init;

  if (processing_template_decl)
    {
      if (dependent_type_p (type)
	  || any_type_dependent_arguments_p (placement)
	  || (nelts && type_dependent_expression_p (nelts))
	  || (init != void_zero_node
	      && any_type_dependent_arguments_p (init)))
	return build_raw_new_expr (placement, type, nelts, init,
				   use_global_new);
      placement = build_non_dependent_args (placement);
      if (nelts)
	nelts = build_non_dependent_expr (nelts);
      if (init != void_zero_node)
	init = build_non_dependent_args (init);
    }

  if (nelts)
    {
      if (!build_expr_type_conversion (WANT_INT | WANT_ENUM, nelts, false))
	pedwarn ("size in array new must have integral type");
      nelts = cp_save_expr (cp_convert (sizetype, nelts));
      /* It is valid to allocate a zero-element array:

	   [expr.new]

	   When the value of the expression in a direct-new-declarator
	   is zero, the allocation function is called to allocate an
	   array with no elements.  The pointer returned by the
	   new-expression is non-null.  [Note: If the library allocation
	   function is called, the pointer returned is distinct from the
	   pointer to any other object.]

	 However, that is not generally useful, so we issue a
	 warning.  */
      if (integer_zerop (nelts))
	warning (0, "allocating zero-element array");
    }

  /* ``A reference cannot be created by the new operator.  A reference
     is not an object (8.2.2, 8.4.3), so a pointer to it could not be
     returned by new.'' ARM 5.3.3 */
  if (TREE_CODE (type) == REFERENCE_TYPE)
    {
      error ("new cannot be applied to a reference type");
      type = TREE_TYPE (type);
    }

  if (TREE_CODE (type) == FUNCTION_TYPE)
    {
      error ("new cannot be applied to a function type");
      return error_mark_node;
    }

  rval = build_new_1 (placement, type, nelts, init, use_global_new);
  if (rval == error_mark_node)
    return error_mark_node;

  if (processing_template_decl)
    return build_raw_new_expr (orig_placement, type, orig_nelts, orig_init,
			       use_global_new);

  /* Wrap it in a NOP_EXPR so warn_if_unused_value doesn't complain.  */
  rval = build1 (NOP_EXPR, TREE_TYPE (rval), rval);
  TREE_NO_WARNING (rval) = 1;

  return rval;
}

/* Given a Java class, return a decl for the corresponding java.lang.Class.  */

tree
build_java_class_ref (tree type)
{
  tree name = NULL_TREE, class_decl;
  static tree CL_suffix = NULL_TREE;
  if (CL_suffix == NULL_TREE)
    CL_suffix = get_identifier("class$");
  if (jclass_node == NULL_TREE)
    {
      jclass_node = IDENTIFIER_GLOBAL_VALUE (get_identifier ("jclass"));
      if (jclass_node == NULL_TREE)
	{
	  error ("call to Java constructor, while %<jclass%> undefined");
	  return error_mark_node;
	}
      jclass_node = TREE_TYPE (jclass_node);
    }

  /* Mangle the class$ field.  */
  {
    tree field;
    for (field = TYPE_FIELDS (type); field; field = TREE_CHAIN (field))
      if (DECL_NAME (field) == CL_suffix)
	{
	  mangle_decl (field);
	  name = DECL_ASSEMBLER_NAME (field);
	  break;
	}
    if (!field)
      {
	error ("can't find %<class$%> in %qT", type);
	return error_mark_node;
      }
  }

  class_decl = IDENTIFIER_GLOBAL_VALUE (name);
  if (class_decl == NULL_TREE)
    {
      class_decl = build_decl (VAR_DECL, name, TREE_TYPE (jclass_node));
      TREE_STATIC (class_decl) = 1;
      DECL_EXTERNAL (class_decl) = 1;
      TREE_PUBLIC (class_decl) = 1;
      DECL_ARTIFICIAL (class_decl) = 1;
      DECL_IGNORED_P (class_decl) = 1;
      pushdecl_top_level (class_decl);
      make_decl_rtl (class_decl);
    }
  return class_decl;
}

static tree
build_vec_delete_1 (tree base, tree maxindex, tree type,
    special_function_kind auto_delete_vec, int use_global_delete)
{
  tree virtual_size;
  tree ptype = build_pointer_type (type = complete_type (type));
  tree size_exp = size_in_bytes (type);

  /* Temporary variables used by the loop.  */
  tree tbase, tbase_init;

  /* This is the body of the loop that implements the deletion of a
     single element, and moves temp variables to next elements.  */
  tree body;

  /* This is the LOOP_EXPR that governs the deletion of the elements.  */
  tree loop = 0;

  /* This is the thing that governs what to do after the loop has run.  */
  tree deallocate_expr = 0;

  /* This is the BIND_EXPR which holds the outermost iterator of the
     loop.  It is convenient to set this variable up and test it before
     executing any other code in the loop.
     This is also the containing expression returned by this function.  */
  tree controller = NULL_TREE;

  /* We should only have 1-D arrays here.  */
  gcc_assert (TREE_CODE (type) != ARRAY_TYPE);

  if (! IS_AGGR_TYPE (type) || TYPE_HAS_TRIVIAL_DESTRUCTOR (type))
    goto no_destructor;

  /* The below is short by the cookie size.  */
  virtual_size = size_binop (MULT_EXPR, size_exp,
			     convert (sizetype, maxindex));

  tbase = create_temporary_var (ptype);
  tbase_init = build_modify_expr (tbase, NOP_EXPR,
				  fold_build2 (PLUS_EXPR, ptype,
					       base,
					       virtual_size));
  DECL_REGISTER (tbase) = 1;
  controller = build3 (BIND_EXPR, void_type_node, tbase,
		       NULL_TREE, NULL_TREE);
  TREE_SIDE_EFFECTS (controller) = 1;

  body = build1 (EXIT_EXPR, void_type_node,
		 build2 (EQ_EXPR, boolean_type_node, tbase,
			 fold_convert (ptype, base)));
  body = build_compound_expr
    (body, build_modify_expr (tbase, NOP_EXPR,
			      build2 (MINUS_EXPR, ptype, tbase, size_exp)));
  body = build_compound_expr
    (body, build_delete (ptype, tbase, sfk_complete_destructor,
			 LOOKUP_NORMAL|LOOKUP_DESTRUCTOR, 1));

  loop = build1 (LOOP_EXPR, void_type_node, body);
  loop = build_compound_expr (tbase_init, loop);

 no_destructor:
  /* If the delete flag is one, or anything else with the low bit set,
     delete the storage.  */
  if (auto_delete_vec != sfk_base_destructor)
    {
      tree base_tbd;

      /* The below is short by the cookie size.  */
      virtual_size = size_binop (MULT_EXPR, size_exp,
				 convert (sizetype, maxindex));

      if (! TYPE_VEC_NEW_USES_COOKIE (type))
	/* no header */
	base_tbd = base;
      else
	{
	  tree cookie_size;

	  cookie_size = targetm.cxx.get_cookie_size (type);
	  base_tbd
	    = cp_convert (ptype,
			  cp_build_binary_op (MINUS_EXPR,
					      cp_convert (string_type_node,
							  base),
					      cookie_size));
	  /* True size with header.  */
	  virtual_size = size_binop (PLUS_EXPR, virtual_size, cookie_size);
	}

      if (auto_delete_vec == sfk_deleting_destructor)
	deallocate_expr = build_op_delete_call (VEC_DELETE_EXPR,
						base_tbd, virtual_size,
						use_global_delete & 1,
						/*placement=*/NULL_TREE,
						/*alloc_fn=*/NULL_TREE);
    }

  body = loop;
  if (!deallocate_expr)
    ;
  else if (!body)
    body = deallocate_expr;
  else
    body = build_compound_expr (body, deallocate_expr);

  if (!body)
    body = integer_zero_node;

  /* Outermost wrapper: If pointer is null, punt.  */
  body = fold_build3 (COND_EXPR, void_type_node,
		      fold_build2 (NE_EXPR, boolean_type_node, base,
				   convert (TREE_TYPE (base),
					    integer_zero_node)),
		      body, integer_zero_node);
  body = build1 (NOP_EXPR, void_type_node, body);

  if (controller)
    {
      TREE_OPERAND (controller, 1) = body;
      body = controller;
    }

  if (TREE_CODE (base) == SAVE_EXPR)
    /* Pre-evaluate the SAVE_EXPR outside of the BIND_EXPR.  */
    body = build2 (COMPOUND_EXPR, void_type_node, base, body);

  return convert_to_void (body, /*implicit=*/NULL);
}

/* Create an unnamed variable of the indicated TYPE.  */

tree
create_temporary_var (tree type)
{
  tree decl;

  decl = build_decl (VAR_DECL, NULL_TREE, type);
  TREE_USED (decl) = 1;
  DECL_ARTIFICIAL (decl) = 1;
  DECL_IGNORED_P (decl) = 1;
  DECL_SOURCE_LOCATION (decl) = input_location;
  DECL_CONTEXT (decl) = current_function_decl;

  return decl;
}

/* Create a new temporary variable of the indicated TYPE, initialized
   to INIT.

   It is not entered into current_binding_level, because that breaks
   things when it comes time to do final cleanups (which take place
   "outside" the binding contour of the function).  */

static tree
get_temp_regvar (tree type, tree init)
{
  tree decl;

  decl = create_temporary_var (type);
  add_decl_expr (decl);

  finish_expr_stmt (build_modify_expr (decl, INIT_EXPR, init));

  return decl;
}

/* `build_vec_init' returns tree structure that performs
   initialization of a vector of aggregate types.

   BASE is a reference to the vector, of ARRAY_TYPE.
   MAXINDEX is the maximum index of the array (one less than the
     number of elements).  It is only used if
     TYPE_DOMAIN (TREE_TYPE (BASE)) == NULL_TREE.

   INIT is the (possibly NULL) initializer.

   If EXPLICIT_DEFAULT_INIT_P is true, then INIT must be NULL.  All
   elements in the array are default-initialized.

   FROM_ARRAY is 0 if we should init everything with INIT
   (i.e., every element initialized from INIT).
   FROM_ARRAY is 1 if we should index into INIT in parallel
   with initialization of DECL.
   FROM_ARRAY is 2 if we should index into INIT in parallel,
   but use assignment instead of initialization.  */

tree
build_vec_init (tree base, tree maxindex, tree init,
		bool explicit_default_init_p,
		int from_array)
{
  tree rval;
  tree base2 = NULL_TREE;
  tree size;
  tree itype = NULL_TREE;
  tree iterator;
  /* The type of the array.  */
  tree atype = TREE_TYPE (base);
  /* The type of an element in the array.  */
  tree type = TREE_TYPE (atype);
  /* The element type reached after removing all outer array
     types.  */
  tree inner_elt_type;
  /* The type of a pointer to an element in the array.  */
  tree ptype;
  tree stmt_expr;
  tree compound_stmt;
  int destroy_temps;
  tree try_block = NULL_TREE;
  int num_initialized_elts = 0;
  bool is_global;

  if (TYPE_DOMAIN (atype))
    maxindex = array_type_nelts (atype);

  if (maxindex == NULL_TREE || maxindex == error_mark_node)
    return error_mark_node;

  if (explicit_default_init_p)
    gcc_assert (!init);

  inner_elt_type = strip_array_types (atype);
  if (init
      && (from_array == 2
	  ? (!CLASS_TYPE_P (inner_elt_type)
	     || !TYPE_HAS_COMPLEX_ASSIGN_REF (inner_elt_type))
	  : !TYPE_NEEDS_CONSTRUCTING (type))
      && ((TREE_CODE (init) == CONSTRUCTOR
	   /* Don't do this if the CONSTRUCTOR might contain something
	      that might throw and require us to clean up.  */
	   && (VEC_empty (constructor_elt, CONSTRUCTOR_ELTS (init))
	       || ! TYPE_HAS_NONTRIVIAL_DESTRUCTOR (inner_elt_type)))
	  || from_array))
    {
      /* Do non-default initialization of POD arrays resulting from
	 brace-enclosed initializers.  In this case, digest_init and
	 store_constructor will handle the semantics for us.  */

      stmt_expr = build2 (INIT_EXPR, atype, base, init);
      return stmt_expr;
    }

  maxindex = cp_convert (ptrdiff_type_node, maxindex);
  ptype = build_pointer_type (type);
  size = size_in_bytes (type);
  if (TREE_CODE (TREE_TYPE (base)) == ARRAY_TYPE)
    base = cp_convert (ptype, decay_conversion (base));

  /* The code we are generating looks like:
     ({
       T* t1 = (T*) base;
       T* rval = t1;
       ptrdiff_t iterator = maxindex;
       try {
	 for (; iterator != -1; --iterator) {
	   ... initialize *t1 ...
	   ++t1;
	 }
       } catch (...) {
	 ... destroy elements that were constructed ...
       }
       rval;
     })

     We can omit the try and catch blocks if we know that the
     initialization will never throw an exception, or if the array
     elements do not have destructors.  We can omit the loop completely if
     the elements of the array do not have constructors.

     We actually wrap the entire body of the above in a STMT_EXPR, for
     tidiness.

     When copying from array to another, when the array elements have
     only trivial copy constructors, we should use __builtin_memcpy
     rather than generating a loop.  That way, we could take advantage
     of whatever cleverness the back-end has for dealing with copies
     of blocks of memory.  */

  is_global = begin_init_stmts (&stmt_expr, &compound_stmt);
  destroy_temps = stmts_are_full_exprs_p ();
  current_stmt_tree ()->stmts_are_full_exprs_p = 0;
  rval = get_temp_regvar (ptype, base);
  base = get_temp_regvar (ptype, rval);
  iterator = get_temp_regvar (ptrdiff_type_node, maxindex);

  /* Protect the entire array initialization so that we can destroy
     the partially constructed array if an exception is thrown.
     But don't do this if we're assigning.  */
  if (flag_exceptions && TYPE_HAS_NONTRIVIAL_DESTRUCTOR (type)
      && from_array != 2)
    {
      try_block = begin_try_block ();
    }

  if (init != NULL_TREE && TREE_CODE (init) == CONSTRUCTOR)
    {
      /* Do non-default initialization of non-POD arrays resulting from
	 brace-enclosed initializers.  */
      unsigned HOST_WIDE_INT idx;
      tree elt;
      from_array = 0;

      FOR_EACH_CONSTRUCTOR_VALUE (CONSTRUCTOR_ELTS (init), idx, elt)
	{
	  tree baseref = build1 (INDIRECT_REF, type, base);

	  num_initialized_elts++;

	  current_stmt_tree ()->stmts_are_full_exprs_p = 1;
	  if (IS_AGGR_TYPE (type) || TREE_CODE (type) == ARRAY_TYPE)
	    finish_expr_stmt (build_aggr_init (baseref, elt, 0));
	  else
	    finish_expr_stmt (build_modify_expr (baseref, NOP_EXPR,
						 elt));
	  current_stmt_tree ()->stmts_are_full_exprs_p = 0;

	  finish_expr_stmt (build_unary_op (PREINCREMENT_EXPR, base, 0));
	  finish_expr_stmt (build_unary_op (PREDECREMENT_EXPR, iterator, 0));
	}

      /* Clear out INIT so that we don't get confused below.  */
      init = NULL_TREE;
    }
  else if (from_array)
    {
      /* If initializing one array from another, initialize element by
	 element.  We rely upon the below calls the do argument
	 checking.  */
      if (init)
	{
	  base2 = decay_conversion (init);
	  itype = TREE_TYPE (base2);
	  base2 = get_temp_regvar (itype, base2);
	  itype = TREE_TYPE (itype);
	}
      else if (TYPE_LANG_SPECIFIC (type)
	       && TYPE_NEEDS_CONSTRUCTING (type)
	       && ! TYPE_HAS_DEFAULT_CONSTRUCTOR (type))
	{
	  error ("initializer ends prematurely");
	  return error_mark_node;
	}
    }

  /* Now, default-initialize any remaining elements.  We don't need to
     do that if a) the type does not need constructing, or b) we've
     already initialized all the elements.

     We do need to keep going if we're copying an array.  */

  if (from_array
      || ((TYPE_NEEDS_CONSTRUCTING (type) || explicit_default_init_p)
	  && ! (host_integerp (maxindex, 0)
		&& (num_initialized_elts
		    == tree_low_cst (maxindex, 0) + 1))))
    {
      /* If the ITERATOR is equal to -1, then we don't have to loop;
	 we've already initialized all the elements.  */
      tree for_stmt;
      tree elt_init;
      tree to;

/* APPLE LOCAL begin for-fsf-4_4 3274130 5295549 */ \
      for_stmt = begin_for_stmt (NULL_TREE);
/* APPLE LOCAL end for-fsf-4_4 3274130 5295549 */ \
      finish_for_init_stmt (for_stmt);
      finish_for_cond (build2 (NE_EXPR, boolean_type_node, iterator,
			       build_int_cst (TREE_TYPE (iterator), -1)),
		       for_stmt);
      finish_for_expr (build_unary_op (PREDECREMENT_EXPR, iterator, 0),
		       for_stmt);

      to = build1 (INDIRECT_REF, type, base);

      if (from_array)
	{
	  tree from;

	  if (base2)
	    from = build1 (INDIRECT_REF, itype, base2);
	  else
	    from = NULL_TREE;

	  if (from_array == 2)
	    elt_init = build_modify_expr (to, NOP_EXPR, from);
	  else if (TYPE_NEEDS_CONSTRUCTING (type))
	    elt_init = build_aggr_init (to, from, 0);
	  else if (from)
	    elt_init = build_modify_expr (to, NOP_EXPR, from);
	  else
	    gcc_unreachable ();
	}
      else if (TREE_CODE (type) == ARRAY_TYPE)
	{
	  if (init != 0)
	    sorry
	      ("cannot initialize multi-dimensional array with initializer");
	  elt_init = build_vec_init (build1 (INDIRECT_REF, type, base),
				     0, 0,
				     /*explicit_default_init_p=*/false,
				     0);
	}
      else if (!TYPE_NEEDS_CONSTRUCTING (type))
	elt_init = (build_modify_expr
		    (to, INIT_EXPR,
		     build_zero_init (type, size_one_node,
				      /*static_storage_p=*/false)));
      else
	elt_init = build_aggr_init (to, init, 0);

      current_stmt_tree ()->stmts_are_full_exprs_p = 1;
      finish_expr_stmt (elt_init);
      current_stmt_tree ()->stmts_are_full_exprs_p = 0;

      finish_expr_stmt (build_unary_op (PREINCREMENT_EXPR, base, 0));
      if (base2)
	finish_expr_stmt (build_unary_op (PREINCREMENT_EXPR, base2, 0));

      finish_for_stmt (for_stmt);
    }

  /* Make sure to cleanup any partially constructed elements.  */
  if (flag_exceptions && TYPE_HAS_NONTRIVIAL_DESTRUCTOR (type)
      && from_array != 2)
    {
      tree e;
      tree m = cp_build_binary_op (MINUS_EXPR, maxindex, iterator);

      /* Flatten multi-dimensional array since build_vec_delete only
	 expects one-dimensional array.  */
      if (TREE_CODE (type) == ARRAY_TYPE)
	m = cp_build_binary_op (MULT_EXPR, m,
				array_type_nelts_total (type));

      finish_cleanup_try_block (try_block);
      e = build_vec_delete_1 (rval, m,
			      inner_elt_type, sfk_base_destructor,
			      /*use_global_delete=*/0);
      finish_cleanup (e, try_block);
    }

  /* The value of the array initialization is the array itself, RVAL
     is a pointer to the first element.  */
  finish_stmt_expr_expr (rval, stmt_expr);

  stmt_expr = finish_init_stmts (is_global, stmt_expr, compound_stmt);

  /* Now convert make the result have the correct type.  */
  atype = build_pointer_type (atype);
  stmt_expr = build1 (NOP_EXPR, atype, stmt_expr);
  stmt_expr = build_indirect_ref (stmt_expr, NULL);

  current_stmt_tree ()->stmts_are_full_exprs_p = destroy_temps;
  return stmt_expr;
}

/* Call the DTOR_KIND destructor for EXP.  FLAGS are as for
   build_delete.  */

static tree
build_dtor_call (tree exp, special_function_kind dtor_kind, int flags)
{
  tree name;
  tree fn;
  switch (dtor_kind)
    {
    case sfk_complete_destructor:
      name = complete_dtor_identifier;
      break;

    case sfk_base_destructor:
      name = base_dtor_identifier;
      break;

    case sfk_deleting_destructor:
      name = deleting_dtor_identifier;
      break;

    default:
      gcc_unreachable ();
    }
  fn = lookup_fnfields (TREE_TYPE (exp), name, /*protect=*/2);
  return build_new_method_call (exp, fn,
				/*args=*/NULL_TREE,
				/*conversion_path=*/NULL_TREE,
				flags,
				/*fn_p=*/NULL);
}

/* Generate a call to a destructor. TYPE is the type to cast ADDR to.
   ADDR is an expression which yields the store to be destroyed.
   AUTO_DELETE is the name of the destructor to call, i.e., either
   sfk_complete_destructor, sfk_base_destructor, or
   sfk_deleting_destructor.

   FLAGS is the logical disjunction of zero or more LOOKUP_
   flags.  See cp-tree.h for more info.  */

tree
build_delete (tree type, tree addr, special_function_kind auto_delete,
    int flags, int use_global_delete)
{
  tree expr;

  if (addr == error_mark_node)
    return error_mark_node;

  /* Can happen when CURRENT_EXCEPTION_OBJECT gets its type
     set to `error_mark_node' before it gets properly cleaned up.  */
  if (type == error_mark_node)
    return error_mark_node;

  type = TYPE_MAIN_VARIANT (type);

  if (TREE_CODE (type) == POINTER_TYPE)
    {
      bool complete_p = true;

      type = TYPE_MAIN_VARIANT (TREE_TYPE (type));
      if (TREE_CODE (type) == ARRAY_TYPE)
	goto handle_array;

      /* We don't want to warn about delete of void*, only other
	  incomplete types.  Deleting other incomplete types
	  invokes undefined behavior, but it is not ill-formed, so
	  compile to something that would even do The Right Thing
	  (TM) should the type have a trivial dtor and no delete
	  operator.  */
      if (!VOID_TYPE_P (type))
	{
	  complete_type (type);
	  if (!COMPLETE_TYPE_P (type))
	    {
	      warning (0, "possible problem detected in invocation of "
		       "delete operator:");
	      cxx_incomplete_type_diagnostic (addr, type, 1);
	      inform ("neither the destructor nor the class-specific "
		      "operator delete will be called, even if they are "
		      "declared when the class is defined.");
	      complete_p = false;
	    }
	}
      if (VOID_TYPE_P (type) || !complete_p || !IS_AGGR_TYPE (type))
	/* Call the builtin operator delete.  */
	return build_builtin_delete_call (addr);
      if (TREE_SIDE_EFFECTS (addr))
	addr = save_expr (addr);

      /* Throw away const and volatile on target type of addr.  */
      addr = convert_force (build_pointer_type (type), addr, 0);
    }
  else if (TREE_CODE (type) == ARRAY_TYPE)
    {
    handle_array:

      if (TYPE_DOMAIN (type) == NULL_TREE)
	{
	  error ("unknown array size in delete");
	  return error_mark_node;
	}
      return build_vec_delete (addr, array_type_nelts (type),
			       auto_delete, use_global_delete);
    }
  else
    {
      /* Don't check PROTECT here; leave that decision to the
	 destructor.  If the destructor is accessible, call it,
	 else report error.  */
      addr = build_unary_op (ADDR_EXPR, addr, 0);
      if (TREE_SIDE_EFFECTS (addr))
	addr = save_expr (addr);

      addr = convert_force (build_pointer_type (type), addr, 0);
    }

  gcc_assert (IS_AGGR_TYPE (type));

  if (TYPE_HAS_TRIVIAL_DESTRUCTOR (type))
    {
      if (auto_delete != sfk_deleting_destructor)
	return void_zero_node;

      return build_op_delete_call (DELETE_EXPR, addr,
				   cxx_sizeof_nowarn (type),
				   use_global_delete,
				   /*placement=*/NULL_TREE,
				   /*alloc_fn=*/NULL_TREE);
    }
  else
    {
      tree do_delete = NULL_TREE;
      tree ifexp;

      if (CLASSTYPE_LAZY_DESTRUCTOR (type))
	lazily_declare_fn (sfk_destructor, type);

      /* For `::delete x', we must not use the deleting destructor
	 since then we would not be sure to get the global `operator
	 delete'.  */
      if (use_global_delete && auto_delete == sfk_deleting_destructor)
	{
	  /* We will use ADDR multiple times so we must save it.  */
	  addr = save_expr (addr);
	  /* Delete the object.  */
	  do_delete = build_builtin_delete_call (addr);
	  /* Otherwise, treat this like a complete object destructor
	     call.  */
	  auto_delete = sfk_complete_destructor;
	}
      /* If the destructor is non-virtual, there is no deleting
	 variant.  Instead, we must explicitly call the appropriate
	 `operator delete' here.  */
      else if (!DECL_VIRTUAL_P (CLASSTYPE_DESTRUCTORS (type))
	       && auto_delete == sfk_deleting_destructor)
	{
	  /* We will use ADDR multiple times so we must save it.  */
	  addr = save_expr (addr);
	  /* Build the call.  */
	  do_delete = build_op_delete_call (DELETE_EXPR,
					    addr,
					    cxx_sizeof_nowarn (type),
					    /*global_p=*/false,
					    /*placement=*/NULL_TREE,
					    /*alloc_fn=*/NULL_TREE);
	  /* Call the complete object destructor.  */
	  auto_delete = sfk_complete_destructor;
	}
      else if (auto_delete == sfk_deleting_destructor
	       && TYPE_GETS_REG_DELETE (type))
	{
	  /* Make sure we have access to the member op delete, even though
	     we'll actually be calling it from the destructor.  */
	  build_op_delete_call (DELETE_EXPR, addr, cxx_sizeof_nowarn (type),
				/*global_p=*/false,
				/*placement=*/NULL_TREE,
				/*alloc_fn=*/NULL_TREE);
	}

      expr = build_dtor_call (build_indirect_ref (addr, NULL),
			      auto_delete, flags);
      if (do_delete)
	expr = build2 (COMPOUND_EXPR, void_type_node, expr, do_delete);

      if (flags & LOOKUP_DESTRUCTOR)
	/* Explicit destructor call; don't check for null pointer.  */
	ifexp = integer_one_node;
      else
	/* Handle deleting a null pointer.  */
	ifexp = fold (cp_build_binary_op (NE_EXPR, addr, integer_zero_node));

      if (ifexp != integer_one_node)
	expr = build3 (COND_EXPR, void_type_node,
		       ifexp, expr, void_zero_node);

      return expr;
    }
}

/* At the beginning of a destructor, push cleanups that will call the
   destructors for our base classes and members.

   Called from begin_destructor_body.  */

void
push_base_cleanups (void)
{
  tree binfo, base_binfo;
  int i;
  tree member;
  tree expr;
  VEC(tree,gc) *vbases;

  /* Run destructors for all virtual baseclasses.  */
  if (CLASSTYPE_VBASECLASSES (current_class_type))
    {
      tree cond = (condition_conversion
		   (build2 (BIT_AND_EXPR, integer_type_node,
			    current_in_charge_parm,
			    integer_two_node)));

      /* The CLASSTYPE_VBASECLASSES vector is in initialization
	 order, which is also the right order for pushing cleanups.  */
      for (vbases = CLASSTYPE_VBASECLASSES (current_class_type), i = 0;
	   VEC_iterate (tree, vbases, i, base_binfo); i++)
	{
	  if (TYPE_HAS_NONTRIVIAL_DESTRUCTOR (BINFO_TYPE (base_binfo)))
	    {
	      expr = build_special_member_call (current_class_ref,
						base_dtor_identifier,
						NULL_TREE,
						base_binfo,
						(LOOKUP_NORMAL
						 | LOOKUP_NONVIRTUAL));
	      expr = build3 (COND_EXPR, void_type_node, cond,
			     expr, void_zero_node);
	      finish_decl_cleanup (NULL_TREE, expr);
	    }
	}
    }

  /* Take care of the remaining baseclasses.  */
  for (binfo = TYPE_BINFO (current_class_type), i = 0;
       BINFO_BASE_ITERATE (binfo, i, base_binfo); i++)
    {
      /* APPLE LOCAL begin omit calls to empty destructors 5559195 */
      tree dtor = CLASSTYPE_DESTRUCTORS (BINFO_TYPE (base_binfo));

      if ((!CLASSTYPE_DESTRUCTOR_NONTRIVIAL_BECAUSE_OF_BASE (BINFO_TYPE (base_binfo))
	   && !CLASSTYPE_HAS_NONTRIVIAL_DESTRUCTOR_BODY (BINFO_TYPE (base_binfo))
	   && !(dtor && (TREE_PRIVATE (dtor))))
      /* APPLE LOCAL end omit calls to empty destructors 5559195 */
	  || BINFO_VIRTUAL_P (base_binfo))
	continue;

      expr = build_special_member_call (current_class_ref,
					base_dtor_identifier,
					NULL_TREE, base_binfo,
					LOOKUP_NORMAL | LOOKUP_NONVIRTUAL);
      finish_decl_cleanup (NULL_TREE, expr);
    }

  for (member = TYPE_FIELDS (current_class_type); member;
       member = TREE_CHAIN (member))
    {
      if (TREE_TYPE (member) == error_mark_node
	  || TREE_CODE (member) != FIELD_DECL
	  || DECL_ARTIFICIAL (member))
	continue;
      if (TYPE_HAS_NONTRIVIAL_DESTRUCTOR (TREE_TYPE (member)))
	{
	  tree this_member = (build_class_member_access_expr
			      (current_class_ref, member,
			       /*access_path=*/NULL_TREE,
			       /*preserve_reference=*/false));
	  tree this_type = TREE_TYPE (member);
	  expr = build_delete (this_type, this_member,
			       sfk_complete_destructor,
			       LOOKUP_NONVIRTUAL|LOOKUP_DESTRUCTOR|LOOKUP_NORMAL,
			       0);
	  finish_decl_cleanup (NULL_TREE, expr);

	  /* APPLE LOCAL begin omit calls to empty destructors 5559195 */
	  /* Even if body of current class's destructor was found to be empty,
	     it must now be called because it must delete its members. */
	  CLASSTYPE_DESTRUCTOR_NONTRIVIAL_BECAUSE_OF_BASE (current_class_type) = 1;
	  /* APPLE LOCAL end omit calls to empty destructors 5559195 */
	}
    }
}

/* Build a C++ vector delete expression.
   MAXINDEX is the number of elements to be deleted.
   ELT_SIZE is the nominal size of each element in the vector.
   BASE is the expression that should yield the store to be deleted.
   This function expands (or synthesizes) these calls itself.
   AUTO_DELETE_VEC says whether the container (vector) should be deallocated.

   This also calls delete for virtual baseclasses of elements of the vector.

   Update: MAXINDEX is no longer needed.  The size can be extracted from the
   start of the vector for pointers, and from the type for arrays.  We still
   use MAXINDEX for arrays because it happens to already have one of the
   values we'd have to extract.  (We could use MAXINDEX with pointers to
   confirm the size, and trap if the numbers differ; not clear that it'd
   be worth bothering.)  */

tree
build_vec_delete (tree base, tree maxindex,
    special_function_kind auto_delete_vec, int use_global_delete)
{
  tree type;
  tree rval;
  tree base_init = NULL_TREE;

  type = TREE_TYPE (base);

  if (TREE_CODE (type) == POINTER_TYPE)
    {
      /* Step back one from start of vector, and read dimension.  */
      tree cookie_addr;

      if (TREE_SIDE_EFFECTS (base))
	{
	  base_init = get_target_expr (base);
	  base = TARGET_EXPR_SLOT (base_init);
	}
      type = strip_array_types (TREE_TYPE (type));
      cookie_addr = build2 (MINUS_EXPR,
			    build_pointer_type (sizetype),
			    base,
			    TYPE_SIZE_UNIT (sizetype));
      maxindex = build_indirect_ref (cookie_addr, NULL);
    }
  else if (TREE_CODE (type) == ARRAY_TYPE)
    {
      /* Get the total number of things in the array, maxindex is a
	 bad name.  */
      maxindex = array_type_nelts_total (type);
      type = strip_array_types (type);
      base = build_unary_op (ADDR_EXPR, base, 1);
      if (TREE_SIDE_EFFECTS (base))
	{
	  base_init = get_target_expr (base);
	  base = TARGET_EXPR_SLOT (base_init);
	}
    }
  else
    {
      if (base != error_mark_node)
	error ("type to vector delete is neither pointer or array type");
      return error_mark_node;
    }

  rval = build_vec_delete_1 (base, maxindex, type, auto_delete_vec,
			     use_global_delete);
  if (base_init)
    rval = build2 (COMPOUND_EXPR, TREE_TYPE (rval), base_init, rval);

  return rval;
}
