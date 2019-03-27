/* RunTime Type Identification
   Copyright (C) 1995, 1996, 1997, 1998, 1999, 2000, 2001, 2002, 2003, 2004,
   2005, 2006
   Free Software Foundation, Inc.
   Mostly written by Jason Merrill (jason@cygnus.com).

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

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "tree.h"
#include "cp-tree.h"
#include "flags.h"
#include "output.h"
#include "assert.h"
#include "toplev.h"
#include "convert.h"
#include "target.h"

/* C++ returns type information to the user in struct type_info
   objects. We also use type information to implement dynamic_cast and
   exception handlers. Type information for a particular type is
   indicated with an ABI defined structure derived from type_info.
   This would all be very straight forward, but for the fact that the
   runtime library provides the definitions of the type_info structure
   and the ABI defined derived classes. We cannot build declarations
   of them directly in the compiler, but we need to layout objects of
   their type.  Somewhere we have to lie.

   We define layout compatible POD-structs with compiler-defined names
   and generate the appropriate initializations for them (complete
   with explicit mention of their vtable). When we have to provide a
   type_info to the user we reinterpret_cast the internal compiler
   type to type_info.  A well formed program can only explicitly refer
   to the type_infos of complete types (& cv void).  However, we chain
   pointer type_infos to the pointed-to-type, and that can be
   incomplete.  We only need the addresses of such incomplete
   type_info objects for static initialization.

   The type information VAR_DECL of a type is held on the
   IDENTIFIER_GLOBAL_VALUE of the type's mangled name. That VAR_DECL
   will be the internal type.  It will usually have the correct
   internal type reflecting the kind of type it represents (pointer,
   array, function, class, inherited class, etc).  When the type it
   represents is incomplete, it will have the internal type
   corresponding to type_info.  That will only happen at the end of
   translation, when we are emitting the type info objects.  */

/* Auxiliary data we hold for each type_info derived object we need.  */
typedef struct tinfo_s GTY (())
{
  tree type;  /* The RECORD_TYPE for this type_info object */

  tree vtable; /* The VAR_DECL of the vtable.  Only filled at end of
		  translation.  */

  tree name;  /* IDENTIFIER_NODE for the ABI specified name of
		 the type_info derived type.  */
} tinfo_s;

DEF_VEC_O(tinfo_s);
DEF_VEC_ALLOC_O(tinfo_s,gc);

typedef enum tinfo_kind
{
  TK_TYPE_INFO_TYPE,    /* std::type_info */
  TK_BASE_TYPE,		/* abi::__base_class_type_info */
  TK_BUILTIN_TYPE,	/* abi::__fundamental_type_info */
  TK_ARRAY_TYPE,	/* abi::__array_type_info */
  TK_FUNCTION_TYPE,	/* abi::__function_type_info */
  TK_ENUMERAL_TYPE,	/* abi::__enum_type_info */
  TK_POINTER_TYPE,	/* abi::__pointer_type_info */
  TK_POINTER_MEMBER_TYPE, /* abi::__pointer_to_member_type_info */
  TK_CLASS_TYPE,	/* abi::__class_type_info */
  TK_SI_CLASS_TYPE,	/* abi::__si_class_type_info */
  TK_FIXED		/* end of fixed descriptors. */
  /* ...		   abi::__vmi_type_info<I> */
} tinfo_kind;

/* A vector of all tinfo decls that haven't yet been emitted.  */
VEC(tree,gc) *unemitted_tinfo_decls;

/* A vector of all type_info derived types we need.  The first few are
   fixed and created early. The remainder are for multiple inheritance
   and are generated as needed. */
static GTY (()) VEC(tinfo_s,gc) *tinfo_descs;

static tree build_headof (tree);
static tree ifnonnull (tree, tree);
static tree tinfo_name (tree);
static tree build_dynamic_cast_1 (tree, tree);
static tree throw_bad_cast (void);
static tree throw_bad_typeid (void);
static tree get_tinfo_decl_dynamic (tree);
static tree get_tinfo_ptr (tree);
static bool typeid_ok_p (void);
static int qualifier_flags (tree);
static bool target_incomplete_p (tree);
static tree tinfo_base_init (tinfo_s *, tree);
static tree generic_initializer (tinfo_s *, tree);
static tree ptr_initializer (tinfo_s *, tree);
static tree ptm_initializer (tinfo_s *, tree);
static tree class_initializer (tinfo_s *, tree, tree);
static void create_pseudo_type_info (int, const char *, ...);
static tree get_pseudo_ti_init (tree, unsigned);
static unsigned get_pseudo_ti_index (tree);
static void create_tinfo_types (void);
static bool typeinfo_in_lib_p (tree);

static int doing_runtime = 0;


/* Declare language defined type_info type and a pointer to const
   type_info.  This is incomplete here, and will be completed when
   the user #includes <typeinfo>.  There are language defined
   restrictions on what can be done until that is included.  Create
   the internal versions of the ABI types.  */

void
init_rtti_processing (void)
{
  tree type_info_type;

  push_namespace (std_identifier);
  type_info_type = xref_tag (class_type, get_identifier ("type_info"),
			     /*tag_scope=*/ts_current, false);
  pop_namespace ();
  const_type_info_type_node
    = build_qualified_type (type_info_type, TYPE_QUAL_CONST);
  type_info_ptr_type = build_pointer_type (const_type_info_type_node);

  unemitted_tinfo_decls = VEC_alloc (tree, gc, 124);

  create_tinfo_types ();
}

/* Given the expression EXP of type `class *', return the head of the
   object pointed to by EXP with type cv void*, if the class has any
   virtual functions (TYPE_POLYMORPHIC_P), else just return the
   expression.  */

static tree
build_headof (tree exp)
{
  tree type = TREE_TYPE (exp);
  tree offset;
  tree index;

  gcc_assert (TREE_CODE (type) == POINTER_TYPE);
  type = TREE_TYPE (type);

  if (!TYPE_POLYMORPHIC_P (type))
    return exp;

  /* We use this a couple of times below, protect it.  */
  exp = save_expr (exp);

  /* The offset-to-top field is at index -2 from the vptr.  */
  index = build_int_cst (NULL_TREE,
			 -2 * TARGET_VTABLE_DATA_ENTRY_DISTANCE);

  offset = build_vtbl_ref (build_indirect_ref (exp, NULL), index);

  type = build_qualified_type (ptr_type_node,
			       cp_type_quals (TREE_TYPE (exp)));
  return build2 (PLUS_EXPR, type, exp,
		 convert_to_integer (ptrdiff_type_node, offset));
}

/* Get a bad_cast node for the program to throw...

   See libstdc++/exception.cc for __throw_bad_cast */

static tree
throw_bad_cast (void)
{
  tree fn = get_identifier ("__cxa_bad_cast");
  if (!get_global_value_if_present (fn, &fn))
    fn = push_throw_library_fn (fn, build_function_type (ptr_type_node,
							 void_list_node));

  return build_cxx_call (fn, NULL_TREE);
}

/* Return an expression for "__cxa_bad_typeid()".  The expression
   returned is an lvalue of type "const std::type_info".  */

static tree
throw_bad_typeid (void)
{
  tree fn = get_identifier ("__cxa_bad_typeid");
  if (!get_global_value_if_present (fn, &fn))
    {
      tree t;

      t = build_reference_type (const_type_info_type_node);
      t = build_function_type (t, void_list_node);
      fn = push_throw_library_fn (fn, t);
    }

  return build_cxx_call (fn, NULL_TREE);
}

/* Return an lvalue expression whose type is "const std::type_info"
   and whose value indicates the type of the expression EXP.  If EXP
   is a reference to a polymorphic class, return the dynamic type;
   otherwise return the static type of the expression.  */

static tree
get_tinfo_decl_dynamic (tree exp)
{
  tree type;
  tree t;

  if (error_operand_p (exp))
    return error_mark_node;

  /* peel back references, so they match.  */
  type = non_reference (TREE_TYPE (exp));

  /* Peel off cv qualifiers.  */
  type = TYPE_MAIN_VARIANT (type);

  if (CLASS_TYPE_P (type))
    type = complete_type_or_else (type, exp);

  if (!type)
    return error_mark_node;

  /* If exp is a reference to polymorphic type, get the real type_info.  */
  if (TYPE_POLYMORPHIC_P (type) && ! resolves_to_fixed_type_p (exp, 0))
    {
      /* build reference to type_info from vtable.  */
      tree index;

      /* The RTTI information is at index -1.  */
      index = build_int_cst (NULL_TREE,
			     -1 * TARGET_VTABLE_DATA_ENTRY_DISTANCE);
      t = build_vtbl_ref (exp, index);
      t = convert (type_info_ptr_type, t);
    }
  else
    /* Otherwise return the type_info for the static type of the expr.  */
    t = get_tinfo_ptr (TYPE_MAIN_VARIANT (type));

  return build_indirect_ref (t, NULL);
}

static bool
typeid_ok_p (void)
{
  if (! flag_rtti)
    {
      error ("cannot use typeid with -fno-rtti");
      return false;
    }

  if (!COMPLETE_TYPE_P (const_type_info_type_node))
    {
      error ("must #include <typeinfo> before using typeid");
      return false;
    }

  return true;
}

/* Return an expression for "typeid(EXP)".  The expression returned is
   an lvalue of type "const std::type_info".  */

tree
build_typeid (tree exp)
{
  tree cond = NULL_TREE;
  int nonnull = 0;

  if (exp == error_mark_node || !typeid_ok_p ())
    return error_mark_node;

  if (processing_template_decl)
    return build_min (TYPEID_EXPR, const_type_info_type_node, exp);

  if (TREE_CODE (exp) == INDIRECT_REF
      && TREE_CODE (TREE_TYPE (TREE_OPERAND (exp, 0))) == POINTER_TYPE
      && TYPE_POLYMORPHIC_P (TREE_TYPE (exp))
      && ! resolves_to_fixed_type_p (exp, &nonnull)
      && ! nonnull)
    {
      exp = stabilize_reference (exp);
      cond = cp_convert (boolean_type_node, TREE_OPERAND (exp, 0));
    }

  exp = get_tinfo_decl_dynamic (exp);

  if (exp == error_mark_node)
    return error_mark_node;

  if (cond)
    {
      tree bad = throw_bad_typeid ();

      exp = build3 (COND_EXPR, TREE_TYPE (exp), cond, exp, bad);
    }

  return exp;
}

/* Generate the NTBS name of a type.  */
static tree
tinfo_name (tree type)
{
  const char *name;
  tree name_string;

  name = mangle_type_string (type);
  name_string = fix_string_type (build_string (strlen (name) + 1, name));
  return name_string;
}

/* Return a VAR_DECL for the internal ABI defined type_info object for
   TYPE. You must arrange that the decl is mark_used, if actually use
   it --- decls in vtables are only used if the vtable is output.  */

tree
get_tinfo_decl (tree type)
{
  tree name;
  tree d;

  if (variably_modified_type_p (type, /*fn=*/NULL_TREE))
    {
      error ("cannot create type information for type %qT because "
	     "it involves types of variable size",
	     type);
      return error_mark_node;
    }

  if (TREE_CODE (type) == METHOD_TYPE)
    type = build_function_type (TREE_TYPE (type),
				TREE_CHAIN (TYPE_ARG_TYPES (type)));

  /* For a class type, the variable is cached in the type node
     itself.  */
  if (CLASS_TYPE_P (type))
    {
      d = CLASSTYPE_TYPEINFO_VAR (TYPE_MAIN_VARIANT (type));
      if (d)
	return d;
    }

  name = mangle_typeinfo_for_type (type);

  d = IDENTIFIER_GLOBAL_VALUE (name);
  if (!d)
    {
      int ix = get_pseudo_ti_index (type);
      tinfo_s *ti = VEC_index (tinfo_s, tinfo_descs, ix);

      d = build_lang_decl (VAR_DECL, name, ti->type);
      SET_DECL_ASSEMBLER_NAME (d, name);
      /* Remember the type it is for.  */
      TREE_TYPE (name) = type;
      DECL_TINFO_P (d) = 1;
      DECL_ARTIFICIAL (d) = 1;
      DECL_IGNORED_P (d) = 1;
      TREE_READONLY (d) = 1;
      TREE_STATIC (d) = 1;
      /* Mark the variable as undefined -- but remember that we can
	 define it later if we need to do so.  */
      DECL_EXTERNAL (d) = 1;
      DECL_NOT_REALLY_EXTERN (d) = 1;
      if (CLASS_TYPE_P (type))
	CLASSTYPE_TYPEINFO_VAR (TYPE_MAIN_VARIANT (type)) = d;
      set_linkage_according_to_type (type, d);
      pushdecl_top_level_and_finish (d, NULL_TREE);

      /* Add decl to the global array of tinfo decls.  */
      VEC_safe_push (tree, gc, unemitted_tinfo_decls, d);
    }

  return d;
}

/* Return a pointer to a type_info object describing TYPE, suitably
   cast to the language defined type.  */

static tree
get_tinfo_ptr (tree type)
{
  tree decl = get_tinfo_decl (type);

  mark_used (decl);
  return build_nop (type_info_ptr_type,
		    build_address (decl));
}

/* Return the type_info object for TYPE.  */

tree
get_typeid (tree type)
{
  if (type == error_mark_node || !typeid_ok_p ())
    return error_mark_node;

  if (processing_template_decl)
    return build_min (TYPEID_EXPR, const_type_info_type_node, type);

  /* If the type of the type-id is a reference type, the result of the
     typeid expression refers to a type_info object representing the
     referenced type.  */
  type = non_reference (type);

  /* The top-level cv-qualifiers of the lvalue expression or the type-id
     that is the operand of typeid are always ignored.  */
  type = TYPE_MAIN_VARIANT (type);

  if (CLASS_TYPE_P (type))
    type = complete_type_or_else (type, NULL_TREE);

  if (!type)
    return error_mark_node;

  return build_indirect_ref (get_tinfo_ptr (type), NULL);
}

/* Check whether TEST is null before returning RESULT.  If TEST is used in
   RESULT, it must have previously had a save_expr applied to it.  */

static tree
ifnonnull (tree test, tree result)
{
  return build3 (COND_EXPR, TREE_TYPE (result),
		 build2 (EQ_EXPR, boolean_type_node, test,
			 cp_convert (TREE_TYPE (test), integer_zero_node)),
		 cp_convert (TREE_TYPE (result), integer_zero_node),
		 result);
}

/* Execute a dynamic cast, as described in section 5.2.6 of the 9/93 working
   paper.  */

static tree
build_dynamic_cast_1 (tree type, tree expr)
{
  enum tree_code tc = TREE_CODE (type);
  tree exprtype = TREE_TYPE (expr);
  tree dcast_fn;
  tree old_expr = expr;
  const char *errstr = NULL;

  /* Save casted types in the function's used types hash table.  */
  used_types_insert (type);

  /* T shall be a pointer or reference to a complete class type, or
     `pointer to cv void''.  */
  switch (tc)
    {
    case POINTER_TYPE:
      if (TREE_CODE (TREE_TYPE (type)) == VOID_TYPE)
	break;
      /* Fall through.  */
    case REFERENCE_TYPE:
      if (! IS_AGGR_TYPE (TREE_TYPE (type)))
	{
	  errstr = "target is not pointer or reference to class";
	  goto fail;
	}
      if (!COMPLETE_TYPE_P (complete_type (TREE_TYPE (type))))
	{
	  errstr = "target is not pointer or reference to complete type";
	  goto fail;
	}
      break;

    default:
      errstr = "target is not pointer or reference";
      goto fail;
    }

  if (tc == POINTER_TYPE)
    {
      /* If T is a pointer type, v shall be an rvalue of a pointer to
	 complete class type, and the result is an rvalue of type T.  */

      if (TREE_CODE (exprtype) != POINTER_TYPE)
	{
	  errstr = "source is not a pointer";
	  goto fail;
	}
      if (! IS_AGGR_TYPE (TREE_TYPE (exprtype)))
	{
	  errstr = "source is not a pointer to class";
	  goto fail;
	}
      if (!COMPLETE_TYPE_P (complete_type (TREE_TYPE (exprtype))))
	{
	  errstr = "source is a pointer to incomplete type";
	  goto fail;
	}
    }
  else
    {
      exprtype = build_reference_type (exprtype);

      /* T is a reference type, v shall be an lvalue of a complete class
	 type, and the result is an lvalue of the type referred to by T.  */

      if (! IS_AGGR_TYPE (TREE_TYPE (exprtype)))
	{
	  errstr = "source is not of class type";
	  goto fail;
	}
      if (!COMPLETE_TYPE_P (complete_type (TREE_TYPE (exprtype))))
	{
	  errstr = "source is of incomplete class type";
	  goto fail;
	}

      /* Apply trivial conversion T -> T& for dereferenced ptrs.  */
      expr = convert_to_reference (exprtype, expr, CONV_IMPLICIT,
				   LOOKUP_NORMAL, NULL_TREE);
    }

  /* The dynamic_cast operator shall not cast away constness.  */
  if (!at_least_as_qualified_p (TREE_TYPE (type),
				TREE_TYPE (exprtype)))
    {
      errstr = "conversion casts away constness";
      goto fail;
    }

  /* If *type is an unambiguous accessible base class of *exprtype,
     convert statically.  */
  {
    tree binfo;

    binfo = lookup_base (TREE_TYPE (exprtype), TREE_TYPE (type),
			 ba_check, NULL);

    if (binfo)
      {
	expr = build_base_path (PLUS_EXPR, convert_from_reference (expr),
				binfo, 0);
	if (TREE_CODE (exprtype) == POINTER_TYPE)
	  expr = rvalue (expr);
	return expr;
      }
  }

  /* Otherwise *exprtype must be a polymorphic class (have a vtbl).  */
  if (TYPE_POLYMORPHIC_P (TREE_TYPE (exprtype)))
    {
      tree expr1;
      /* if TYPE is `void *', return pointer to complete object.  */
      if (tc == POINTER_TYPE && VOID_TYPE_P (TREE_TYPE (type)))
	{
	  /* if b is an object, dynamic_cast<void *>(&b) == (void *)&b.  */
	  if (TREE_CODE (expr) == ADDR_EXPR
	      && TREE_CODE (TREE_OPERAND (expr, 0)) == VAR_DECL
	      && TREE_CODE (TREE_TYPE (TREE_OPERAND (expr, 0))) == RECORD_TYPE)
	    return build1 (NOP_EXPR, type, expr);

	  /* Since expr is used twice below, save it.  */
	  expr = save_expr (expr);

	  expr1 = build_headof (expr);
	  if (TREE_TYPE (expr1) != type)
	    expr1 = build1 (NOP_EXPR, type, expr1);
	  return ifnonnull (expr, expr1);
	}
      else
	{
	  tree retval;
	  tree result, td2, td3, elems;
	  tree static_type, target_type, boff;

	  /* If we got here, we can't convert statically.  Therefore,
	     dynamic_cast<D&>(b) (b an object) cannot succeed.  */
	  if (tc == REFERENCE_TYPE)
	    {
	      if (TREE_CODE (old_expr) == VAR_DECL
		  && TREE_CODE (TREE_TYPE (old_expr)) == RECORD_TYPE)
		{
		  tree expr = throw_bad_cast ();
		  warning (0, "dynamic_cast of %q#D to %q#T can never succeed",
			   old_expr, type);
		  /* Bash it to the expected type.  */
		  TREE_TYPE (expr) = type;
		  return expr;
		}
	    }
	  /* Ditto for dynamic_cast<D*>(&b).  */
	  else if (TREE_CODE (expr) == ADDR_EXPR)
	    {
	      tree op = TREE_OPERAND (expr, 0);
	      if (TREE_CODE (op) == VAR_DECL
		  && TREE_CODE (TREE_TYPE (op)) == RECORD_TYPE)
		{
		  warning (0, "dynamic_cast of %q#D to %q#T can never succeed",
			   op, type);
		  retval = build_int_cst (type, 0);
		  return retval;
		}
	    }

	  /* Use of dynamic_cast when -fno-rtti is prohibited.  */
	  if (!flag_rtti)
	    {
	      error ("%<dynamic_cast%> not permitted with -fno-rtti");
	      return error_mark_node;
	    }

	  target_type = TYPE_MAIN_VARIANT (TREE_TYPE (type));
	  static_type = TYPE_MAIN_VARIANT (TREE_TYPE (exprtype));
	  td2 = get_tinfo_decl (target_type);
	  mark_used (td2);
	  td2 = build_unary_op (ADDR_EXPR, td2, 0);
	  td3 = get_tinfo_decl (static_type);
	  mark_used (td3);
	  td3 = build_unary_op (ADDR_EXPR, td3, 0);

	  /* Determine how T and V are related.  */
	  boff = dcast_base_hint (static_type, target_type);

	  /* Since expr is used twice below, save it.  */
	  expr = save_expr (expr);

	  expr1 = expr;
	  if (tc == REFERENCE_TYPE)
	    expr1 = build_unary_op (ADDR_EXPR, expr1, 0);

	  elems = tree_cons
	    (NULL_TREE, expr1, tree_cons
	     (NULL_TREE, td3, tree_cons
	      (NULL_TREE, td2, tree_cons
	       (NULL_TREE, boff, NULL_TREE))));

	  dcast_fn = dynamic_cast_node;
	  if (!dcast_fn)
	    {
	      tree tmp;
	      tree tinfo_ptr;
	      tree ns = abi_node;
	      const char *name;

	      push_nested_namespace (ns);
	      tinfo_ptr = xref_tag (class_type,
				    get_identifier ("__class_type_info"),
				    /*tag_scope=*/ts_current, false);

	      tinfo_ptr = build_pointer_type
		(build_qualified_type
		 (tinfo_ptr, TYPE_QUAL_CONST));
	      name = "__dynamic_cast";
	      tmp = tree_cons
		(NULL_TREE, const_ptr_type_node, tree_cons
		 (NULL_TREE, tinfo_ptr, tree_cons
		  (NULL_TREE, tinfo_ptr, tree_cons
		   (NULL_TREE, ptrdiff_type_node, void_list_node))));
	      tmp = build_function_type (ptr_type_node, tmp);
	      dcast_fn = build_library_fn_ptr (name, tmp);
	      DECL_IS_PURE (dcast_fn) = 1;
	      pop_nested_namespace (ns);
	      dynamic_cast_node = dcast_fn;
	    }
	  result = build_cxx_call (dcast_fn, elems);

	  if (tc == REFERENCE_TYPE)
	    {
	      tree bad = throw_bad_cast ();
	      tree neq;

	      result = save_expr (result);
	      neq = c_common_truthvalue_conversion (result);
	      return build3 (COND_EXPR, type, neq, result, bad);
	    }

	  /* Now back to the type we want from a void*.  */
	  result = cp_convert (type, result);
	  return ifnonnull (expr, result);
	}
    }
  else
    errstr = "source type is not polymorphic";

 fail:
  error ("cannot dynamic_cast %qE (of type %q#T) to type %q#T (%s)",
	 expr, exprtype, type, errstr);
  return error_mark_node;
}

tree
build_dynamic_cast (tree type, tree expr)
{
  if (type == error_mark_node || expr == error_mark_node)
    return error_mark_node;

  if (processing_template_decl)
    {
      expr = build_min (DYNAMIC_CAST_EXPR, type, expr);
      TREE_SIDE_EFFECTS (expr) = 1;

      return expr;
    }

  return convert_from_reference (build_dynamic_cast_1 (type, expr));
}

/* Return the runtime bit mask encoding the qualifiers of TYPE.  */

static int
qualifier_flags (tree type)
{
  int flags = 0;
  int quals = cp_type_quals (type);

  if (quals & TYPE_QUAL_CONST)
    flags |= 1;
  if (quals & TYPE_QUAL_VOLATILE)
    flags |= 2;
  if (quals & TYPE_QUAL_RESTRICT)
    flags |= 4;
  return flags;
}

/* Return true, if the pointer chain TYPE ends at an incomplete type, or
   contains a pointer to member of an incomplete class.  */

static bool
target_incomplete_p (tree type)
{
  while (true)
    if (TYPE_PTRMEM_P (type))
      {
	if (!COMPLETE_TYPE_P (TYPE_PTRMEM_CLASS_TYPE (type)))
	  return true;
	type = TYPE_PTRMEM_POINTED_TO_TYPE (type);
      }
    else if (TREE_CODE (type) == POINTER_TYPE)
      type = TREE_TYPE (type);
    else
      return !COMPLETE_OR_VOID_TYPE_P (type);
}

/* Returns true if TYPE involves an incomplete class type; in that
   case, typeinfo variables for TYPE should be emitted with internal
   linkage.  */

static bool
involves_incomplete_p (tree type)
{
  switch (TREE_CODE (type))
    {
    case POINTER_TYPE:
      return target_incomplete_p (TREE_TYPE (type));

    case OFFSET_TYPE:
    ptrmem:
      return
	(target_incomplete_p (TYPE_PTRMEM_POINTED_TO_TYPE (type))
	 || !COMPLETE_TYPE_P (TYPE_PTRMEM_CLASS_TYPE (type)));

    case RECORD_TYPE:
      if (TYPE_PTRMEMFUNC_P (type))
	goto ptrmem;
      /* Fall through.  */
    case UNION_TYPE:
      if (!COMPLETE_TYPE_P (type))
	return true;

    default:
      /* All other types do not involve incomplete class types.  */
      return false;
    }
}

/* Return a CONSTRUCTOR for the common part of the type_info objects. This
   is the vtable pointer and NTBS name.  The NTBS name is emitted as a
   comdat const char array, so it becomes a unique key for the type. Generate
   and emit that VAR_DECL here.  (We can't always emit the type_info itself
   as comdat, because of pointers to incomplete.) */

static tree
tinfo_base_init (tinfo_s *ti, tree target)
{
  tree init = NULL_TREE;
  tree name_decl;
  tree vtable_ptr;

  {
    tree name_name;

    /* Generate the NTBS array variable.  */
    tree name_type = build_cplus_array_type
		     (build_qualified_type (char_type_node, TYPE_QUAL_CONST),
		     NULL_TREE);
    tree name_string = tinfo_name (target);

    /* Determine the name of the variable -- and remember with which
       type it is associated.  */
    name_name = mangle_typeinfo_string_for_type (target);
    TREE_TYPE (name_name) = target;

    name_decl = build_lang_decl (VAR_DECL, name_name, name_type);
    SET_DECL_ASSEMBLER_NAME (name_decl, name_name);
    DECL_ARTIFICIAL (name_decl) = 1;
    DECL_IGNORED_P (name_decl) = 1;
    TREE_READONLY (name_decl) = 1;
    TREE_STATIC (name_decl) = 1;
    DECL_EXTERNAL (name_decl) = 0;
    DECL_TINFO_P (name_decl) = 1;
    set_linkage_according_to_type (target, name_decl);
    import_export_decl (name_decl);
    DECL_INITIAL (name_decl) = name_string;
    mark_used (name_decl);
    pushdecl_top_level_and_finish (name_decl, name_string);
  }

  vtable_ptr = ti->vtable;
  if (!vtable_ptr)
    {
      tree real_type;
      push_nested_namespace (abi_node);
      real_type = xref_tag (class_type, ti->name,
			    /*tag_scope=*/ts_current, false);
      pop_nested_namespace (abi_node);

      if (!COMPLETE_TYPE_P (real_type))
	{
	  /* We never saw a definition of this type, so we need to
	     tell the compiler that this is an exported class, as
	     indeed all of the __*_type_info classes are.  */
	  SET_CLASSTYPE_INTERFACE_KNOWN (real_type);
	  CLASSTYPE_INTERFACE_ONLY (real_type) = 1;
	}

      vtable_ptr = get_vtable_decl (real_type, /*complete=*/1);
      vtable_ptr = build_unary_op (ADDR_EXPR, vtable_ptr, 0);

      /* We need to point into the middle of the vtable.  */
      vtable_ptr = build2
	(PLUS_EXPR, TREE_TYPE (vtable_ptr), vtable_ptr,
	 size_binop (MULT_EXPR,
		     size_int (2 * TARGET_VTABLE_DATA_ENTRY_DISTANCE),
		     TYPE_SIZE_UNIT (vtable_entry_type)));

      ti->vtable = vtable_ptr;
    }

  init = tree_cons (NULL_TREE, vtable_ptr, init);

  init = tree_cons (NULL_TREE, decay_conversion (name_decl), init);

  init = build_constructor_from_list (NULL_TREE, nreverse (init));
  TREE_CONSTANT (init) = 1;
  TREE_INVARIANT (init) = 1;
  TREE_STATIC (init) = 1;
  init = tree_cons (NULL_TREE, init, NULL_TREE);

  return init;
}

/* Return the CONSTRUCTOR expr for a type_info of TYPE. TI provides the
   information about the particular type_info derivation, which adds no
   additional fields to the type_info base.  */

static tree
generic_initializer (tinfo_s *ti, tree target)
{
  tree init = tinfo_base_init (ti, target);

  init = build_constructor_from_list (NULL_TREE, init);
  TREE_CONSTANT (init) = 1;
  TREE_INVARIANT (init) = 1;
  TREE_STATIC (init) = 1;
  return init;
}

/* Return the CONSTRUCTOR expr for a type_info of pointer TYPE.
   TI provides information about the particular type_info derivation,
   which adds target type and qualifier flags members to the type_info base.  */

static tree
ptr_initializer (tinfo_s *ti, tree target)
{
  tree init = tinfo_base_init (ti, target);
  tree to = TREE_TYPE (target);
  int flags = qualifier_flags (to);
  bool incomplete = target_incomplete_p (to);

  if (incomplete)
    flags |= 8;
  init = tree_cons (NULL_TREE, build_int_cst (NULL_TREE, flags), init);
  init = tree_cons (NULL_TREE,
		    get_tinfo_ptr (TYPE_MAIN_VARIANT (to)),
		    init);

  init = build_constructor_from_list (NULL_TREE, nreverse (init));
  TREE_CONSTANT (init) = 1;
  TREE_INVARIANT (init) = 1;
  TREE_STATIC (init) = 1;
  return init;
}

/* Return the CONSTRUCTOR expr for a type_info of pointer to member data TYPE.
   TI provides information about the particular type_info derivation,
   which adds class, target type and qualifier flags members to the type_info
   base.  */

static tree
ptm_initializer (tinfo_s *ti, tree target)
{
  tree init = tinfo_base_init (ti, target);
  tree to = TYPE_PTRMEM_POINTED_TO_TYPE (target);
  tree klass = TYPE_PTRMEM_CLASS_TYPE (target);
  int flags = qualifier_flags (to);
  bool incomplete = target_incomplete_p (to);

  if (incomplete)
    flags |= 0x8;
  if (!COMPLETE_TYPE_P (klass))
    flags |= 0x10;
  init = tree_cons (NULL_TREE, build_int_cst (NULL_TREE, flags), init);
  init = tree_cons (NULL_TREE,
		    get_tinfo_ptr (TYPE_MAIN_VARIANT (to)),
		    init);
  init = tree_cons (NULL_TREE,
		    get_tinfo_ptr (klass),
		    init);

  init = build_constructor_from_list (NULL_TREE, nreverse (init));
  TREE_CONSTANT (init) = 1;
  TREE_INVARIANT (init) = 1;
  TREE_STATIC (init) = 1;
  return init;
}

/* Return the CONSTRUCTOR expr for a type_info of class TYPE.
   TI provides information about the particular __class_type_info derivation,
   which adds hint flags and TRAIL initializers to the type_info base.  */

static tree
class_initializer (tinfo_s *ti, tree target, tree trail)
{
  tree init = tinfo_base_init (ti, target);

  TREE_CHAIN (init) = trail;
  init = build_constructor_from_list (NULL_TREE, init);
  TREE_CONSTANT (init) = 1;
  TREE_INVARIANT (init) = 1;
  TREE_STATIC (init) = 1;
  return init;
}

/* Returns true if the typeinfo for type should be placed in
   the runtime library.  */

static bool
typeinfo_in_lib_p (tree type)
{
  /* The typeinfo objects for `T*' and `const T*' are in the runtime
     library for simple types T.  */
  if (TREE_CODE (type) == POINTER_TYPE
      && (cp_type_quals (TREE_TYPE (type)) == TYPE_QUAL_CONST
	  || cp_type_quals (TREE_TYPE (type)) == TYPE_UNQUALIFIED))
    type = TREE_TYPE (type);

  switch (TREE_CODE (type))
    {
    case INTEGER_TYPE:
    case BOOLEAN_TYPE:
    case REAL_TYPE:
    case VOID_TYPE:
      return true;

    default:
      return false;
    }
}

/* Generate the initializer for the type info describing TYPE.  TK_INDEX is
   the index of the descriptor in the tinfo_desc vector. */

static tree
get_pseudo_ti_init (tree type, unsigned tk_index)
{
  tinfo_s *ti = VEC_index (tinfo_s, tinfo_descs, tk_index);

  gcc_assert (at_eof);
  switch (tk_index)
    {
    case TK_POINTER_MEMBER_TYPE:
      return ptm_initializer (ti, type);

    case TK_POINTER_TYPE:
      return ptr_initializer (ti, type);

    case TK_BUILTIN_TYPE:
    case TK_ENUMERAL_TYPE:
    case TK_FUNCTION_TYPE:
    case TK_ARRAY_TYPE:
      return generic_initializer (ti, type);

    case TK_CLASS_TYPE:
      return class_initializer (ti, type, NULL_TREE);

    case TK_SI_CLASS_TYPE:
      {
	tree base_binfo = BINFO_BASE_BINFO (TYPE_BINFO (type), 0);
	tree tinfo = get_tinfo_ptr (BINFO_TYPE (base_binfo));
	tree base_inits = tree_cons (NULL_TREE, tinfo, NULL_TREE);

	/* get_tinfo_ptr might have reallocated the tinfo_descs vector.  */
	ti = VEC_index (tinfo_s, tinfo_descs, tk_index);
	return class_initializer (ti, type, base_inits);
      }

    default:
      {
	int hint = ((CLASSTYPE_REPEATED_BASE_P (type) << 0)
		    | (CLASSTYPE_DIAMOND_SHAPED_P (type) << 1));
	tree binfo = TYPE_BINFO (type);
	int nbases = BINFO_N_BASE_BINFOS (binfo);
	VEC(tree,gc) *base_accesses = BINFO_BASE_ACCESSES (binfo);
	tree base_inits = NULL_TREE;
	int ix;

	gcc_assert (tk_index >= TK_FIXED);

	/* Generate the base information initializer.  */
	for (ix = nbases; ix--;)
	  {
	    tree base_binfo = BINFO_BASE_BINFO (binfo, ix);
	    tree base_init = NULL_TREE;
	    int flags = 0;
	    tree tinfo;
	    tree offset;

	    if (VEC_index (tree, base_accesses, ix) == access_public_node)
	      flags |= 2;
	    tinfo = get_tinfo_ptr (BINFO_TYPE (base_binfo));
	    if (BINFO_VIRTUAL_P (base_binfo))
	      {
		/* We store the vtable offset at which the virtual
		   base offset can be found.  */
		offset = BINFO_VPTR_FIELD (base_binfo);
		offset = convert (sizetype, offset);
		flags |= 1;
	      }
	    else
	      offset = BINFO_OFFSET (base_binfo);

	    /* Combine offset and flags into one field.  */
	    offset = cp_build_binary_op (LSHIFT_EXPR, offset,
					 build_int_cst (NULL_TREE, 8));
	    offset = cp_build_binary_op (BIT_IOR_EXPR, offset,
					 build_int_cst (NULL_TREE, flags));
	    base_init = tree_cons (NULL_TREE, offset, base_init);
	    base_init = tree_cons (NULL_TREE, tinfo, base_init);
	    base_init = build_constructor_from_list (NULL_TREE, base_init);
	    base_inits = tree_cons (NULL_TREE, base_init, base_inits);
	  }
	base_inits = build_constructor_from_list (NULL_TREE, base_inits);
	base_inits = tree_cons (NULL_TREE, base_inits, NULL_TREE);
	/* Prepend the number of bases.  */
	base_inits = tree_cons (NULL_TREE,
				build_int_cst (NULL_TREE, nbases),
				base_inits);
	/* Prepend the hint flags.  */
	base_inits = tree_cons (NULL_TREE,
				build_int_cst (NULL_TREE, hint),
				base_inits);

	/* get_tinfo_ptr might have reallocated the tinfo_descs vector.  */
	ti = VEC_index (tinfo_s, tinfo_descs, tk_index);
	return class_initializer (ti, type, base_inits);
      }
    }
}

/* Generate the RECORD_TYPE containing the data layout of a type_info
   derivative as used by the runtime. This layout must be consistent with
   that defined in the runtime support. Also generate the VAR_DECL for the
   type's vtable. We explicitly manage the vtable member, and name it for
   real type as used in the runtime. The RECORD type has a different name,
   to avoid collisions.  Return a TREE_LIST who's TINFO_PSEUDO_TYPE
   is the generated type and TINFO_VTABLE_NAME is the name of the
   vtable.  We have to delay generating the VAR_DECL of the vtable
   until the end of the translation, when we'll have seen the library
   definition, if there was one.

   REAL_NAME is the runtime's name of the type. Trailing arguments are
   additional FIELD_DECL's for the structure. The final argument must be
   NULL.  */

static void
create_pseudo_type_info (int tk, const char *real_name, ...)
{
  tinfo_s *ti;
  tree pseudo_type;
  char *pseudo_name;
  tree fields;
  tree field_decl;
  va_list ap;

  va_start (ap, real_name);

  /* Generate the pseudo type name.  */
  pseudo_name = (char *) alloca (strlen (real_name) + 30);
  strcpy (pseudo_name, real_name);
  strcat (pseudo_name, "_pseudo");
  if (tk >= TK_FIXED)
    sprintf (pseudo_name + strlen (pseudo_name), "%d", tk - TK_FIXED);

  /* First field is the pseudo type_info base class.  */
  fields = build_decl (FIELD_DECL, NULL_TREE,
		       VEC_index (tinfo_s, tinfo_descs,
				  TK_TYPE_INFO_TYPE)->type);

  /* Now add the derived fields.  */
  while ((field_decl = va_arg (ap, tree)))
    {
      TREE_CHAIN (field_decl) = fields;
      fields = field_decl;
    }

  /* Create the pseudo type.  */
  pseudo_type = make_aggr_type (RECORD_TYPE);
  finish_builtin_struct (pseudo_type, pseudo_name, fields, NULL_TREE);
  CLASSTYPE_AS_BASE (pseudo_type) = pseudo_type;

  ti = VEC_index (tinfo_s, tinfo_descs, tk);
  ti->type = cp_build_qualified_type (pseudo_type, TYPE_QUAL_CONST);
  ti->name = get_identifier (real_name);
  ti->vtable = NULL_TREE;

  /* Pretend this is public so determine_visibility doesn't give vtables
     internal linkage.  */
  TREE_PUBLIC (TYPE_MAIN_DECL (ti->type)) = 1;

  va_end (ap);
}

/* Return the index of a pseudo type info type node used to describe
   TYPE.  TYPE must be a complete type (or cv void), except at the end
   of the translation unit.  */

static unsigned
get_pseudo_ti_index (tree type)
{
  unsigned ix;

  switch (TREE_CODE (type))
    {
    case OFFSET_TYPE:
      ix = TK_POINTER_MEMBER_TYPE;
      break;

    case POINTER_TYPE:
      ix = TK_POINTER_TYPE;
      break;

    case ENUMERAL_TYPE:
      ix = TK_ENUMERAL_TYPE;
      break;

    case FUNCTION_TYPE:
      ix = TK_FUNCTION_TYPE;
      break;

    case ARRAY_TYPE:
      ix = TK_ARRAY_TYPE;
      break;

    case UNION_TYPE:
    case RECORD_TYPE:
      if (TYPE_PTRMEMFUNC_P (type))
	{
	  ix = TK_POINTER_MEMBER_TYPE;
	  break;
	}
      else if (!COMPLETE_TYPE_P (type))
	{
	  if (!at_eof)
	    cxx_incomplete_type_error (NULL_TREE, type);
	  ix = TK_CLASS_TYPE;
	  break;
	}
      else if (!BINFO_N_BASE_BINFOS (TYPE_BINFO (type)))
	{
	  ix = TK_CLASS_TYPE;
	  break;
	}
      else
	{
	  tree binfo = TYPE_BINFO (type);
	  VEC(tree,gc) *base_accesses = BINFO_BASE_ACCESSES (binfo);
	  tree base_binfo = BINFO_BASE_BINFO (binfo, 0);
	  int num_bases = BINFO_N_BASE_BINFOS (binfo);

	  if (num_bases == 1
	      && VEC_index (tree, base_accesses, 0) == access_public_node
	      && !BINFO_VIRTUAL_P (base_binfo)
	      && integer_zerop (BINFO_OFFSET (base_binfo)))
	    {
	      /* single non-virtual public.  */
	      ix = TK_SI_CLASS_TYPE;
	      break;
	    }
	  else
	    {
	      tinfo_s *ti;
	      tree array_domain, base_array;

	      ix = TK_FIXED + num_bases;
	      if (VEC_length (tinfo_s, tinfo_descs) <= ix)
		{
		  /* too short, extend.  */
		  unsigned len = VEC_length (tinfo_s, tinfo_descs);

		  VEC_safe_grow (tinfo_s, gc, tinfo_descs, ix + 1);
		  while (VEC_iterate (tinfo_s, tinfo_descs, len++, ti))
		    ti->type = ti->vtable = ti->name = NULL_TREE;
		}
	      else if (VEC_index (tinfo_s, tinfo_descs, ix)->type)
		/* already created.  */
		break;

	      /* Create the array of __base_class_type_info entries.
		 G++ 3.2 allocated an array that had one too many
		 entries, and then filled that extra entries with
		 zeros.  */
	      if (abi_version_at_least (2))
		array_domain = build_index_type (size_int (num_bases - 1));
	      else
		array_domain = build_index_type (size_int (num_bases));
	      base_array =
		build_array_type (VEC_index (tinfo_s, tinfo_descs,
					     TK_BASE_TYPE)->type,
				  array_domain);

	      push_nested_namespace (abi_node);
	      create_pseudo_type_info
		(ix, "__vmi_class_type_info",
		 build_decl (FIELD_DECL, NULL_TREE, integer_type_node),
		 build_decl (FIELD_DECL, NULL_TREE, integer_type_node),
		 build_decl (FIELD_DECL, NULL_TREE, base_array),
		 NULL);
	      pop_nested_namespace (abi_node);
	      break;
	    }
	}
    default:
      ix = TK_BUILTIN_TYPE;
      break;
    }
  return ix;
}

/* Make sure the required builtin types exist for generating the type_info
   variable definitions.  */

static void
create_tinfo_types (void)
{
  tinfo_s *ti;

  gcc_assert (!tinfo_descs);

  VEC_safe_grow (tinfo_s, gc, tinfo_descs, TK_FIXED);

  push_nested_namespace (abi_node);

  /* Create the internal type_info structure. This is used as a base for
     the other structures.  */
  {
    tree field, fields;

    field = build_decl (FIELD_DECL, NULL_TREE, const_ptr_type_node);
    fields = field;

    field = build_decl (FIELD_DECL, NULL_TREE, const_string_type_node);
    TREE_CHAIN (field) = fields;
    fields = field;

    ti = VEC_index (tinfo_s, tinfo_descs, TK_TYPE_INFO_TYPE);
    ti->type = make_aggr_type (RECORD_TYPE);
    ti->vtable = NULL_TREE;
    ti->name = NULL_TREE;
    finish_builtin_struct (ti->type, "__type_info_pseudo",
			   fields, NULL_TREE);
    TYPE_HAS_CONSTRUCTOR (ti->type) = 1;
  }

  /* Fundamental type_info */
  create_pseudo_type_info (TK_BUILTIN_TYPE, "__fundamental_type_info", NULL);

  /* Array, function and enum type_info. No additional fields.  */
  create_pseudo_type_info (TK_ARRAY_TYPE, "__array_type_info", NULL);
  create_pseudo_type_info (TK_FUNCTION_TYPE, "__function_type_info", NULL);
  create_pseudo_type_info (TK_ENUMERAL_TYPE, "__enum_type_info", NULL);

  /* Class type_info.  No additional fields.  */
  create_pseudo_type_info (TK_CLASS_TYPE, "__class_type_info", NULL);

  /* Single public non-virtual base class. Add pointer to base class.
     This is really a descendant of __class_type_info.  */
  create_pseudo_type_info (TK_SI_CLASS_TYPE, "__si_class_type_info",
	    build_decl (FIELD_DECL, NULL_TREE, type_info_ptr_type),
	    NULL);

  /* Base class internal helper. Pointer to base type, offset to base,
     flags.  */
  {
    tree field, fields;

    field = build_decl (FIELD_DECL, NULL_TREE, type_info_ptr_type);
    fields = field;

    field = build_decl (FIELD_DECL, NULL_TREE, integer_types[itk_long]);
    TREE_CHAIN (field) = fields;
    fields = field;

    ti = VEC_index (tinfo_s, tinfo_descs, TK_BASE_TYPE);

    ti->type = make_aggr_type (RECORD_TYPE);
    ti->vtable = NULL_TREE;
    ti->name = NULL_TREE;
    finish_builtin_struct (ti->type, "__base_class_type_info_pseudo",
			   fields, NULL_TREE);
    TYPE_HAS_CONSTRUCTOR (ti->type) = 1;
  }

  /* Pointer type_info. Adds two fields, qualification mask
     and pointer to the pointed to type.  This is really a descendant of
     __pbase_type_info.  */
  create_pseudo_type_info (TK_POINTER_TYPE, "__pointer_type_info",
       build_decl (FIELD_DECL, NULL_TREE, integer_type_node),
       build_decl (FIELD_DECL, NULL_TREE, type_info_ptr_type),
       NULL);

  /* Pointer to member data type_info.  Add qualifications flags,
     pointer to the member's type info and pointer to the class.
     This is really a descendant of __pbase_type_info.  */
  create_pseudo_type_info (TK_POINTER_MEMBER_TYPE,
       "__pointer_to_member_type_info",
	build_decl (FIELD_DECL, NULL_TREE, integer_type_node),
	build_decl (FIELD_DECL, NULL_TREE, type_info_ptr_type),
	build_decl (FIELD_DECL, NULL_TREE, type_info_ptr_type),
	NULL);

  pop_nested_namespace (abi_node);
}

/* Emit the type_info descriptors which are guaranteed to be in the runtime
   support.  Generating them here guarantees consistency with the other
   structures.  We use the following heuristic to determine when the runtime
   is being generated.  If std::__fundamental_type_info is defined, and its
   destructor is defined, then the runtime is being built.  */

void
emit_support_tinfos (void)
{
  static tree *const fundamentals[] =
  {
    &void_type_node,
    &boolean_type_node,
    &wchar_type_node,
    &char_type_node, &signed_char_type_node, &unsigned_char_type_node,
    &short_integer_type_node, &short_unsigned_type_node,
    &integer_type_node, &unsigned_type_node,
    &long_integer_type_node, &long_unsigned_type_node,
    &long_long_integer_type_node, &long_long_unsigned_type_node,
    &float_type_node, &double_type_node, &long_double_type_node,
    0
  };
  int ix;
  tree bltn_type, dtor;

  push_nested_namespace (abi_node);
  bltn_type = xref_tag (class_type,
			get_identifier ("__fundamental_type_info"),
			/*tag_scope=*/ts_current, false);
  pop_nested_namespace (abi_node);
  if (!COMPLETE_TYPE_P (bltn_type))
    return;
  dtor = CLASSTYPE_DESTRUCTORS (bltn_type);
  if (!dtor || DECL_EXTERNAL (dtor))
    return;
  doing_runtime = 1;
  for (ix = 0; fundamentals[ix]; ix++)
    {
      tree bltn = *fundamentals[ix];
      tree types[3];
      int i;

      types[0] = bltn;
      types[1] = build_pointer_type (bltn);
      types[2] = build_pointer_type (build_qualified_type (bltn,
							   TYPE_QUAL_CONST));

      for (i = 0; i < 3; ++i)
	{
	  tree tinfo;

	  tinfo = get_tinfo_decl (types[i]);
	  TREE_USED (tinfo) = 1;
	  mark_needed (tinfo);
	  /* The C++ ABI requires that these objects be COMDAT.  But,
	     On systems without weak symbols, initialized COMDAT
	     objects are emitted with internal linkage.  (See
	     comdat_linkage for details.)  Since we want these objects
	     to have external linkage so that copies do not have to be
	     emitted in code outside the runtime library, we make them
	     non-COMDAT here.  

	     It might also not be necessary to follow this detail of the
	     ABI.  */
	  if (!flag_weak || ! targetm.cxx.library_rtti_comdat ())
	    {
	      gcc_assert (TREE_PUBLIC (tinfo) && !DECL_COMDAT (tinfo));
	      DECL_INTERFACE_KNOWN (tinfo) = 1;
	    }
	}
    }
}

/* Finish a type info decl. DECL_PTR is a pointer to an unemitted
   tinfo decl.  Determine whether it needs emitting, and if so
   generate the initializer.  */

bool
emit_tinfo_decl (tree decl)
{
  tree type = TREE_TYPE (DECL_NAME (decl));
  int in_library = typeinfo_in_lib_p (type);

  gcc_assert (DECL_TINFO_P (decl));

  if (in_library)
    {
      if (doing_runtime)
	DECL_EXTERNAL (decl) = 0;
      else
	{
	  /* If we're not in the runtime, then DECL (which is already
	     DECL_EXTERNAL) will not be defined here.  */
	  DECL_INTERFACE_KNOWN (decl) = 1;
	  return false;
	}
    }
  else if (involves_incomplete_p (type))
    {
      if (!decl_needed_p (decl))
	return false;
      /* If TYPE involves an incomplete class type, then the typeinfo
	 object will be emitted with internal linkage.  There is no
	 way to know whether or not types are incomplete until the end
	 of the compilation, so this determination must be deferred
	 until this point.  */
      TREE_PUBLIC (decl) = 0;
      DECL_EXTERNAL (decl) = 0;
      DECL_INTERFACE_KNOWN (decl) = 1;
    }

  import_export_decl (decl);
  if (DECL_NOT_REALLY_EXTERN (decl) && decl_needed_p (decl))
    {
      tree init;

      DECL_EXTERNAL (decl) = 0;
      init = get_pseudo_ti_init (type, get_pseudo_ti_index (type));
      DECL_INITIAL (decl) = init;
      mark_used (decl);
      finish_decl (decl, init, NULL_TREE);
      return true;
    }
  else
    return false;
}

#include "gt-cp-rtti.h"
