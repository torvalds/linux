/* Handle the hair of processing (but not expanding) inline functions.
   Also manage function and variable name overloading.
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


/* Handle method declarations.  */
#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "tree.h"
#include "cp-tree.h"
#include "rtl.h"
#include "expr.h"
#include "output.h"
#include "flags.h"
#include "toplev.h"
#include "tm_p.h"
#include "target.h"
#include "tree-pass.h"
#include "diagnostic.h"

/* Various flags to control the mangling process.  */

enum mangling_flags
{
  /* No flags.  */
  mf_none = 0,
  /* The thing we are presently mangling is part of a template type,
     rather than a fully instantiated type.  Therefore, we may see
     complex expressions where we would normally expect to see a
     simple integer constant.  */
  mf_maybe_uninstantiated = 1,
  /* When mangling a numeric value, use the form `_XX_' (instead of
     just `XX') if the value has more than one digit.  */
  mf_use_underscores_around_value = 2
};

typedef enum mangling_flags mangling_flags;

static tree thunk_adjust (tree, bool, HOST_WIDE_INT, tree);
static void do_build_assign_ref (tree);
static void do_build_copy_constructor (tree);
static tree synthesize_exception_spec (tree, tree (*) (tree, void *), void *);
static tree locate_dtor (tree, void *);
static tree locate_ctor (tree, void *);
static tree locate_copy (tree, void *);
static tree make_alias_for_thunk (tree);

/* Called once to initialize method.c.  */

void
init_method (void)
{
  init_mangle ();
}

/* Return a this or result adjusting thunk to FUNCTION.  THIS_ADJUSTING
   indicates whether it is a this or result adjusting thunk.
   FIXED_OFFSET and VIRTUAL_OFFSET indicate how to do the adjustment
   (see thunk_adjust).  VIRTUAL_OFFSET can be NULL, but FIXED_OFFSET
   never is.  VIRTUAL_OFFSET is the /index/ into the vtable for this
   adjusting thunks, we scale it to a byte offset. For covariant
   thunks VIRTUAL_OFFSET is the virtual binfo.  You must post process
   the returned thunk with finish_thunk.  */

tree
make_thunk (tree function, bool this_adjusting,
	    tree fixed_offset, tree virtual_offset)
{
  HOST_WIDE_INT d;
  tree thunk;

  gcc_assert (TREE_CODE (function) == FUNCTION_DECL);
  /* We can have this thunks to covariant thunks, but not vice versa.  */
  gcc_assert (!DECL_THIS_THUNK_P (function));
  gcc_assert (!DECL_RESULT_THUNK_P (function) || this_adjusting);

  /* Scale the VIRTUAL_OFFSET to be in terms of bytes.  */
  if (this_adjusting && virtual_offset)
    virtual_offset
      = size_binop (MULT_EXPR,
		    virtual_offset,
		    convert (ssizetype,
			     TYPE_SIZE_UNIT (vtable_entry_type)));

  d = tree_low_cst (fixed_offset, 0);

  /* See if we already have the thunk in question.  For this_adjusting
     thunks VIRTUAL_OFFSET will be an INTEGER_CST, for covariant thunks it
     will be a BINFO.  */
  for (thunk = DECL_THUNKS (function); thunk; thunk = TREE_CHAIN (thunk))
    if (DECL_THIS_THUNK_P (thunk) == this_adjusting
	&& THUNK_FIXED_OFFSET (thunk) == d
	&& !virtual_offset == !THUNK_VIRTUAL_OFFSET (thunk)
	&& (!virtual_offset
	    || (this_adjusting
		? tree_int_cst_equal (THUNK_VIRTUAL_OFFSET (thunk),
				      virtual_offset)
		: THUNK_VIRTUAL_OFFSET (thunk) == virtual_offset)))
      return thunk;

  /* All thunks must be created before FUNCTION is actually emitted;
     the ABI requires that all thunks be emitted together with the
     function to which they transfer control.  */
  gcc_assert (!TREE_ASM_WRITTEN (function));
  /* Likewise, we can only be adding thunks to a function declared in
     the class currently being laid out.  */
  gcc_assert (TYPE_SIZE (DECL_CONTEXT (function))
	      && TYPE_BEING_DEFINED (DECL_CONTEXT (function)));

  thunk = build_decl (FUNCTION_DECL, NULL_TREE, TREE_TYPE (function));
  DECL_LANG_SPECIFIC (thunk) = DECL_LANG_SPECIFIC (function);
  cxx_dup_lang_specific_decl (thunk);
  DECL_THUNKS (thunk) = NULL_TREE;

  DECL_CONTEXT (thunk) = DECL_CONTEXT (function);
  TREE_READONLY (thunk) = TREE_READONLY (function);
  TREE_THIS_VOLATILE (thunk) = TREE_THIS_VOLATILE (function);
  TREE_PUBLIC (thunk) = TREE_PUBLIC (function);
  SET_DECL_THUNK_P (thunk, this_adjusting);
  THUNK_TARGET (thunk) = function;
  THUNK_FIXED_OFFSET (thunk) = d;
  THUNK_VIRTUAL_OFFSET (thunk) = virtual_offset;
  THUNK_ALIAS (thunk) = NULL_TREE;

  /* The thunk itself is not a constructor or destructor, even if
     the thing it is thunking to is.  */
  DECL_INTERFACE_KNOWN (thunk) = 1;
  DECL_NOT_REALLY_EXTERN (thunk) = 1;
  DECL_SAVED_FUNCTION_DATA (thunk) = NULL;
  DECL_DESTRUCTOR_P (thunk) = 0;
  DECL_CONSTRUCTOR_P (thunk) = 0;
  DECL_EXTERNAL (thunk) = 1;
  DECL_ARTIFICIAL (thunk) = 1;
  /* Even if this thunk is a member of a local class, we don't
     need a static chain.  */
  DECL_NO_STATIC_CHAIN (thunk) = 1;
  /* The THUNK is not a pending inline, even if the FUNCTION is.  */
  DECL_PENDING_INLINE_P (thunk) = 0;
  DECL_INLINE (thunk) = 0;
  DECL_DECLARED_INLINE_P (thunk) = 0;
  /* Nor has it been deferred.  */
  DECL_DEFERRED_FN (thunk) = 0;
  /* Nor is it a template instantiation.  */
  DECL_USE_TEMPLATE (thunk) = 0;
  DECL_TEMPLATE_INFO (thunk) = NULL;

  /* Add it to the list of thunks associated with FUNCTION.  */
  TREE_CHAIN (thunk) = DECL_THUNKS (function);
  DECL_THUNKS (function) = thunk;

  return thunk;
}

/* Finish THUNK, a thunk decl.  */

void
finish_thunk (tree thunk)
{
  tree function, name;
  tree fixed_offset = ssize_int (THUNK_FIXED_OFFSET (thunk));
  tree virtual_offset = THUNK_VIRTUAL_OFFSET (thunk);

  gcc_assert (!DECL_NAME (thunk) && DECL_THUNK_P (thunk));
  if (virtual_offset && DECL_RESULT_THUNK_P (thunk))
    virtual_offset = BINFO_VPTR_FIELD (virtual_offset);
  function = THUNK_TARGET (thunk);
  name = mangle_thunk (function, DECL_THIS_THUNK_P (thunk),
		       fixed_offset, virtual_offset);

  /* We can end up with declarations of (logically) different
     covariant thunks, that do identical adjustments.  The two thunks
     will be adjusting between within different hierarchies, which
     happen to have the same layout.  We must nullify one of them to
     refer to the other.  */
  if (DECL_RESULT_THUNK_P (thunk))
    {
      tree cov_probe;

      for (cov_probe = DECL_THUNKS (function);
	   cov_probe; cov_probe = TREE_CHAIN (cov_probe))
	if (DECL_NAME (cov_probe) == name)
	  {
	    gcc_assert (!DECL_THUNKS (thunk));
	    THUNK_ALIAS (thunk) = (THUNK_ALIAS (cov_probe)
				   ? THUNK_ALIAS (cov_probe) : cov_probe);
	    break;
	  }
    }

  DECL_NAME (thunk) = name;
  SET_DECL_ASSEMBLER_NAME (thunk, name);
}

/* Adjust PTR by the constant FIXED_OFFSET, and by the vtable
   offset indicated by VIRTUAL_OFFSET, if that is
   non-null. THIS_ADJUSTING is nonzero for a this adjusting thunk and
   zero for a result adjusting thunk.  */

static tree
thunk_adjust (tree ptr, bool this_adjusting,
	      HOST_WIDE_INT fixed_offset, tree virtual_offset)
{
  if (this_adjusting)
    /* Adjust the pointer by the constant.  */
    ptr = fold_build2 (PLUS_EXPR, TREE_TYPE (ptr), ptr,
		       ssize_int (fixed_offset));

  /* If there's a virtual offset, look up that value in the vtable and
     adjust the pointer again.  */
  if (virtual_offset)
    {
      tree vtable;

      ptr = save_expr (ptr);
      /* The vptr is always at offset zero in the object.  */
      vtable = build1 (NOP_EXPR,
		       build_pointer_type (build_pointer_type
					   (vtable_entry_type)),
		       ptr);
      /* Form the vtable address.  */
      vtable = build1 (INDIRECT_REF, TREE_TYPE (TREE_TYPE (vtable)), vtable);
      /* Find the entry with the vcall offset.  */
      vtable = build2 (PLUS_EXPR, TREE_TYPE (vtable), vtable, virtual_offset);
      /* Get the offset itself.  */
      vtable = build1 (INDIRECT_REF, TREE_TYPE (TREE_TYPE (vtable)), vtable);
      /* Adjust the `this' pointer.  */
      ptr = fold_build2 (PLUS_EXPR, TREE_TYPE (ptr), ptr, vtable);
    }

  if (!this_adjusting)
    /* Adjust the pointer by the constant.  */
    ptr = fold_build2 (PLUS_EXPR, TREE_TYPE (ptr), ptr,
		       ssize_int (fixed_offset));

  return ptr;
}

static GTY (()) int thunk_labelno;

/* Create a static alias to function.  */

tree
make_alias_for (tree function, tree newid)
{
  tree alias = build_decl (FUNCTION_DECL, newid, TREE_TYPE (function));
  DECL_LANG_SPECIFIC (alias) = DECL_LANG_SPECIFIC (function);
  cxx_dup_lang_specific_decl (alias);
  DECL_CONTEXT (alias) = NULL;
  TREE_READONLY (alias) = TREE_READONLY (function);
  TREE_THIS_VOLATILE (alias) = TREE_THIS_VOLATILE (function);
  TREE_PUBLIC (alias) = 0;
  DECL_INTERFACE_KNOWN (alias) = 1;
  DECL_NOT_REALLY_EXTERN (alias) = 1;
  DECL_THIS_STATIC (alias) = 1;
  DECL_SAVED_FUNCTION_DATA (alias) = NULL;
  DECL_DESTRUCTOR_P (alias) = 0;
  DECL_CONSTRUCTOR_P (alias) = 0;
  DECL_CLONED_FUNCTION (alias) = NULL_TREE;
  DECL_EXTERNAL (alias) = 0;
  DECL_ARTIFICIAL (alias) = 1;
  DECL_NO_STATIC_CHAIN (alias) = 1;
  DECL_PENDING_INLINE_P (alias) = 0;
  DECL_INLINE (alias) = 0;
  DECL_DECLARED_INLINE_P (alias) = 0;
  DECL_DEFERRED_FN (alias) = 0;
  DECL_USE_TEMPLATE (alias) = 0;
  DECL_TEMPLATE_INSTANTIATED (alias) = 0;
  DECL_TEMPLATE_INFO (alias) = NULL;
  DECL_INITIAL (alias) = error_mark_node;
  TREE_ADDRESSABLE (alias) = 1;
  TREE_USED (alias) = 1;
  SET_DECL_ASSEMBLER_NAME (alias, DECL_NAME (alias));
  TREE_SYMBOL_REFERENCED (DECL_ASSEMBLER_NAME (alias)) = 1;
  return alias;
}

static tree
make_alias_for_thunk (tree function)
{
  tree alias;
  char buf[256];

  ASM_GENERATE_INTERNAL_LABEL (buf, "LTHUNK", thunk_labelno);
  thunk_labelno++;

  alias = make_alias_for (function, get_identifier (buf));

  if (!flag_syntax_only)
    assemble_alias (alias, DECL_ASSEMBLER_NAME (function));

  return alias;
}

/* Emit the definition of a C++ multiple inheritance or covariant
   return vtable thunk.  If EMIT_P is nonzero, the thunk is emitted
   immediately.  */

void
use_thunk (tree thunk_fndecl, bool emit_p)
{
  tree a, t, function, alias;
  tree virtual_offset;
  HOST_WIDE_INT fixed_offset, virtual_value;
  bool this_adjusting = DECL_THIS_THUNK_P (thunk_fndecl);

  /* We should have called finish_thunk to give it a name.  */
  gcc_assert (DECL_NAME (thunk_fndecl));

  /* We should never be using an alias, always refer to the
     aliased thunk.  */
  gcc_assert (!THUNK_ALIAS (thunk_fndecl));

  if (TREE_ASM_WRITTEN (thunk_fndecl))
    return;

  function = THUNK_TARGET (thunk_fndecl);
  if (DECL_RESULT (thunk_fndecl))
    /* We already turned this thunk into an ordinary function.
       There's no need to process this thunk again.  */
    return;

  if (DECL_THUNK_P (function))
    /* The target is itself a thunk, process it now.  */
    use_thunk (function, emit_p);

  /* Thunks are always addressable; they only appear in vtables.  */
  TREE_ADDRESSABLE (thunk_fndecl) = 1;

  /* Figure out what function is being thunked to.  It's referenced in
     this translation unit.  */
  TREE_ADDRESSABLE (function) = 1;
  mark_used (function);
  if (!emit_p)
    return;

  if (TARGET_USE_LOCAL_THUNK_ALIAS_P (function))
   alias = make_alias_for_thunk (function);
  else
   alias = function;

  fixed_offset = THUNK_FIXED_OFFSET (thunk_fndecl);
  virtual_offset = THUNK_VIRTUAL_OFFSET (thunk_fndecl);

  if (virtual_offset)
    {
      if (!this_adjusting)
	virtual_offset = BINFO_VPTR_FIELD (virtual_offset);
      virtual_value = tree_low_cst (virtual_offset, /*pos=*/0);
      gcc_assert (virtual_value);
    }
  else
    virtual_value = 0;

  /* And, if we need to emit the thunk, it's used.  */
  mark_used (thunk_fndecl);
  /* This thunk is actually defined.  */
  DECL_EXTERNAL (thunk_fndecl) = 0;
  /* The linkage of the function may have changed.  FIXME in linkage
     rewrite.  */
  TREE_PUBLIC (thunk_fndecl) = TREE_PUBLIC (function);
  DECL_VISIBILITY (thunk_fndecl) = DECL_VISIBILITY (function);
  DECL_VISIBILITY_SPECIFIED (thunk_fndecl)
    = DECL_VISIBILITY_SPECIFIED (function);
  if (DECL_ONE_ONLY (function))
    make_decl_one_only (thunk_fndecl);

  if (flag_syntax_only)
    {
      TREE_ASM_WRITTEN (thunk_fndecl) = 1;
      return;
    }

  push_to_top_level ();

  if (TARGET_USE_LOCAL_THUNK_ALIAS_P (function)
      && targetm.have_named_sections)
    {
      resolve_unique_section (function, 0, flag_function_sections);

      if (DECL_SECTION_NAME (function) != NULL && DECL_ONE_ONLY (function))
	{
	  resolve_unique_section (thunk_fndecl, 0, flag_function_sections);

	  /* Output the thunk into the same section as function.  */
	  DECL_SECTION_NAME (thunk_fndecl) = DECL_SECTION_NAME (function);
	}
    }

  /* Set up cloned argument trees for the thunk.  */
  t = NULL_TREE;
  for (a = DECL_ARGUMENTS (function); a; a = TREE_CHAIN (a))
    {
      tree x = copy_node (a);
      TREE_CHAIN (x) = t;
      DECL_CONTEXT (x) = thunk_fndecl;
      SET_DECL_RTL (x, NULL_RTX);
      DECL_HAS_VALUE_EXPR_P (x) = 0;
      t = x;
    }
  a = nreverse (t);
  DECL_ARGUMENTS (thunk_fndecl) = a;

  if (this_adjusting
      && targetm.asm_out.can_output_mi_thunk (thunk_fndecl, fixed_offset,
					      virtual_value, alias))
    {
      const char *fnname;
      tree fn_block;
      
      current_function_decl = thunk_fndecl;
      DECL_RESULT (thunk_fndecl)
	= build_decl (RESULT_DECL, 0, integer_type_node);
      fnname = XSTR (XEXP (DECL_RTL (thunk_fndecl), 0), 0);
      /* The back-end expects DECL_INITIAL to contain a BLOCK, so we
	 create one.  */
      fn_block = make_node (BLOCK);
      BLOCK_VARS (fn_block) = a;
      DECL_INITIAL (thunk_fndecl) = fn_block;
      init_function_start (thunk_fndecl);
      current_function_is_thunk = 1;
      assemble_start_function (thunk_fndecl, fnname);

      targetm.asm_out.output_mi_thunk (asm_out_file, thunk_fndecl,
				       fixed_offset, virtual_value, alias);

      assemble_end_function (thunk_fndecl, fnname);
      init_insn_lengths ();
      current_function_decl = 0;
      cfun = 0;
      TREE_ASM_WRITTEN (thunk_fndecl) = 1;
    }
  else
    {
      /* If this is a covariant thunk, or we don't have the necessary
	 code for efficient thunks, generate a thunk function that
	 just makes a call to the real function.  Unfortunately, this
	 doesn't work for varargs.  */

      if (varargs_function_p (function))
	error ("generic thunk code fails for method %q#D which uses %<...%>",
	       function);

      DECL_RESULT (thunk_fndecl) = NULL_TREE;

      start_preparsed_function (thunk_fndecl, NULL_TREE, SF_PRE_PARSED);
      /* We don't bother with a body block for thunks.  */

      /* There's no need to check accessibility inside the thunk body.  */
      push_deferring_access_checks (dk_no_check);

      t = a;
      if (this_adjusting)
	t = thunk_adjust (t, /*this_adjusting=*/1,
			  fixed_offset, virtual_offset);

      /* Build up the call to the real function.  */
      t = tree_cons (NULL_TREE, t, NULL_TREE);
      for (a = TREE_CHAIN (a); a; a = TREE_CHAIN (a))
	t = tree_cons (NULL_TREE, a, t);
      t = nreverse (t);
      t = build_call (alias, t);
      CALL_FROM_THUNK_P (t) = 1;

      if (VOID_TYPE_P (TREE_TYPE (t)))
	finish_expr_stmt (t);
      else
	{
	  if (!this_adjusting)
	    {
	      tree cond = NULL_TREE;

	      if (TREE_CODE (TREE_TYPE (t)) == POINTER_TYPE)
		{
		  /* If the return type is a pointer, we need to
		     protect against NULL.  We know there will be an
		     adjustment, because that's why we're emitting a
		     thunk.  */
		  t = save_expr (t);
		  cond = cp_convert (boolean_type_node, t);
		}

	      t = thunk_adjust (t, /*this_adjusting=*/0,
				fixed_offset, virtual_offset);
	      if (cond)
		t = build3 (COND_EXPR, TREE_TYPE (t), cond, t,
			    cp_convert (TREE_TYPE (t), integer_zero_node));
	    }
	  if (IS_AGGR_TYPE (TREE_TYPE (t)))
	    t = build_cplus_new (TREE_TYPE (t), t);
	  finish_return_stmt (t);
	}

      /* Since we want to emit the thunk, we explicitly mark its name as
	 referenced.  */
      mark_decl_referenced (thunk_fndecl);

      /* But we don't want debugging information about it.  */
      DECL_IGNORED_P (thunk_fndecl) = 1;

      /* Re-enable access control.  */
      pop_deferring_access_checks ();

      thunk_fndecl = finish_function (0);
      tree_lowering_passes (thunk_fndecl);
      expand_body (thunk_fndecl);
    }

  pop_from_top_level ();
}

/* Code for synthesizing methods which have default semantics defined.  */

/* Generate code for default X(X&) constructor.  */

static void
do_build_copy_constructor (tree fndecl)
{
  tree parm = FUNCTION_FIRST_USER_PARM (fndecl);

  parm = convert_from_reference (parm);

  if (TYPE_HAS_TRIVIAL_INIT_REF (current_class_type)
      && is_empty_class (current_class_type))
    /* Don't copy the padding byte; it might not have been allocated
       if *this is a base subobject.  */;
  else if (TYPE_HAS_TRIVIAL_INIT_REF (current_class_type))
    {
      tree t = build2 (INIT_EXPR, void_type_node, current_class_ref, parm);
      finish_expr_stmt (t);
    }
  else
    {
      tree fields = TYPE_FIELDS (current_class_type);
      tree member_init_list = NULL_TREE;
      int cvquals = cp_type_quals (TREE_TYPE (parm));
      int i;
      tree binfo, base_binfo;
      VEC(tree,gc) *vbases;

      /* Initialize all the base-classes with the parameter converted
	 to their type so that we get their copy constructor and not
	 another constructor that takes current_class_type.  We must
	 deal with the binfo's directly as a direct base might be
	 inaccessible due to ambiguity.  */
      for (vbases = CLASSTYPE_VBASECLASSES (current_class_type), i = 0;
	   VEC_iterate (tree, vbases, i, binfo); i++)
	{
	  member_init_list
	    = tree_cons (binfo,
			 build_tree_list (NULL_TREE,
					  build_base_path (PLUS_EXPR, parm,
							   binfo, 1)),
			 member_init_list);
	}

      for (binfo = TYPE_BINFO (current_class_type), i = 0;
	   BINFO_BASE_ITERATE (binfo, i, base_binfo); i++)
	{
	  if (BINFO_VIRTUAL_P (base_binfo))
	    continue;

	  member_init_list
	    = tree_cons (base_binfo,
			 build_tree_list (NULL_TREE,
					  build_base_path (PLUS_EXPR, parm,
							   base_binfo, 1)),
			 member_init_list);
	}

      for (; fields; fields = TREE_CHAIN (fields))
	{
	  tree init = parm;
	  tree field = fields;
	  tree expr_type;

	  if (TREE_CODE (field) != FIELD_DECL)
	    continue;

	  expr_type = TREE_TYPE (field);
	  if (DECL_NAME (field))
	    {
	      if (VFIELD_NAME_P (DECL_NAME (field)))
		continue;
	    }
	  else if (ANON_AGGR_TYPE_P (expr_type) && TYPE_FIELDS (expr_type))
	    /* Just use the field; anonymous types can't have
	       nontrivial copy ctors or assignment ops.  */;
	  else
	    continue;

	  /* Compute the type of "init->field".  If the copy-constructor
	     parameter is, for example, "const S&", and the type of
	     the field is "T", then the type will usually be "const
	     T".  (There are no cv-qualified variants of reference
	     types.)  */
	  if (TREE_CODE (expr_type) != REFERENCE_TYPE)
	    {
	      int quals = cvquals;

	      if (DECL_MUTABLE_P (field))
		quals &= ~TYPE_QUAL_CONST;
	      expr_type = cp_build_qualified_type (expr_type, quals);
	    }

	  init = build3 (COMPONENT_REF, expr_type, init, field, NULL_TREE);
	  init = build_tree_list (NULL_TREE, init);

	  member_init_list = tree_cons (field, init, member_init_list);
	}
      finish_mem_initializers (member_init_list);
    }
}

static void
do_build_assign_ref (tree fndecl)
{
  tree parm = TREE_CHAIN (DECL_ARGUMENTS (fndecl));
  tree compound_stmt;

  compound_stmt = begin_compound_stmt (0);
  parm = convert_from_reference (parm);

  if (TYPE_HAS_TRIVIAL_ASSIGN_REF (current_class_type)
      && is_empty_class (current_class_type))
    /* Don't copy the padding byte; it might not have been allocated
       if *this is a base subobject.  */;
  else if (TYPE_HAS_TRIVIAL_ASSIGN_REF (current_class_type))
    {
      tree t = build2 (MODIFY_EXPR, void_type_node, current_class_ref, parm);
      finish_expr_stmt (t);
    }
  else
    {
      tree fields;
      int cvquals = cp_type_quals (TREE_TYPE (parm));
      int i;
      tree binfo, base_binfo;

      /* Assign to each of the direct base classes.  */
      for (binfo = TYPE_BINFO (current_class_type), i = 0;
	   BINFO_BASE_ITERATE (binfo, i, base_binfo); i++)
	{
	  tree converted_parm;

	  /* We must convert PARM directly to the base class
	     explicitly since the base class may be ambiguous.  */
	  converted_parm = build_base_path (PLUS_EXPR, parm, base_binfo, 1);
	  /* Call the base class assignment operator.  */
	  finish_expr_stmt
	    (build_special_member_call (current_class_ref,
					ansi_assopname (NOP_EXPR),
					build_tree_list (NULL_TREE,
							 converted_parm),
					base_binfo,
					LOOKUP_NORMAL | LOOKUP_NONVIRTUAL));
	}

      /* Assign to each of the non-static data members.  */
      for (fields = TYPE_FIELDS (current_class_type);
	   fields;
	   fields = TREE_CHAIN (fields))
	{
	  tree comp = current_class_ref;
	  tree init = parm;
	  tree field = fields;
	  tree expr_type;
	  int quals;

	  if (TREE_CODE (field) != FIELD_DECL || DECL_ARTIFICIAL (field))
	    continue;

	  expr_type = TREE_TYPE (field);

	  if (CP_TYPE_CONST_P (expr_type))
	    {
	      error ("non-static const member %q#D, can't use default "
		     "assignment operator", field);
	      continue;
	    }
	  else if (TREE_CODE (expr_type) == REFERENCE_TYPE)
	    {
	      error ("non-static reference member %q#D, can't use "
		     "default assignment operator", field);
	      continue;
	    }

	  if (DECL_NAME (field))
	    {
	      if (VFIELD_NAME_P (DECL_NAME (field)))
		continue;
	    }
	  else if (ANON_AGGR_TYPE_P (expr_type)
		   && TYPE_FIELDS (expr_type) != NULL_TREE)
	    /* Just use the field; anonymous types can't have
	       nontrivial copy ctors or assignment ops.  */;
	  else
	    continue;

	  comp = build3 (COMPONENT_REF, expr_type, comp, field, NULL_TREE);

	  /* Compute the type of init->field  */
	  quals = cvquals;
	  if (DECL_MUTABLE_P (field))
	    quals &= ~TYPE_QUAL_CONST;
	  expr_type = cp_build_qualified_type (expr_type, quals);

	  init = build3 (COMPONENT_REF, expr_type, init, field, NULL_TREE);

	  if (DECL_NAME (field))
	    init = build_modify_expr (comp, NOP_EXPR, init);
	  else
	    init = build2 (MODIFY_EXPR, TREE_TYPE (comp), comp, init);
	  finish_expr_stmt (init);
	}
    }
  finish_return_stmt (current_class_ref);
  finish_compound_stmt (compound_stmt);
}

/* Synthesize FNDECL, a non-static member function.   */

void
synthesize_method (tree fndecl)
{
  bool nested = (current_function_decl != NULL_TREE);
  tree context = decl_function_context (fndecl);
  bool need_body = true;
  tree stmt;
  location_t save_input_location = input_location;
  int error_count = errorcount;
  int warning_count = warningcount;

  /* Reset the source location, we might have been previously
     deferred, and thus have saved where we were first needed.  */
  DECL_SOURCE_LOCATION (fndecl)
    = DECL_SOURCE_LOCATION (TYPE_NAME (DECL_CONTEXT (fndecl)));

  /* If we've been asked to synthesize a clone, just synthesize the
     cloned function instead.  Doing so will automatically fill in the
     body for the clone.  */
  if (DECL_CLONED_FUNCTION_P (fndecl))
    fndecl = DECL_CLONED_FUNCTION (fndecl);

  /* We may be in the middle of deferred access check.  Disable
     it now.  */
  push_deferring_access_checks (dk_no_deferred);

  if (! context)
    push_to_top_level ();
  else if (nested)
    push_function_context_to (context);

  input_location = DECL_SOURCE_LOCATION (fndecl);

  start_preparsed_function (fndecl, NULL_TREE, SF_DEFAULT | SF_PRE_PARSED);
  stmt = begin_function_body ();

  if (DECL_OVERLOADED_OPERATOR_P (fndecl) == NOP_EXPR)
    {
      do_build_assign_ref (fndecl);
      need_body = false;
    }
  else if (DECL_CONSTRUCTOR_P (fndecl))
    {
      tree arg_chain = FUNCTION_FIRST_USER_PARMTYPE (fndecl);
      if (arg_chain != void_list_node)
	do_build_copy_constructor (fndecl);
      else
	finish_mem_initializers (NULL_TREE);
    }

  /* If we haven't yet generated the body of the function, just
     generate an empty compound statement.  */
  if (need_body)
    {
      tree compound_stmt;
      compound_stmt = begin_compound_stmt (BCS_FN_BODY);
      finish_compound_stmt (compound_stmt);
    }

  finish_function_body (stmt);
  expand_or_defer_fn (finish_function (0));

  input_location = save_input_location;

  if (! context)
    pop_from_top_level ();
  else if (nested)
    pop_function_context_from (context);

  pop_deferring_access_checks ();

  if (error_count != errorcount || warning_count != warningcount)
    inform ("%Hsynthesized method %qD first required here ",
	    &input_location, fndecl);
}

/* Use EXTRACTOR to locate the relevant function called for each base &
   class field of TYPE. CLIENT allows additional information to be passed
   to EXTRACTOR.  Generates the union of all exceptions generated by those
   functions.  Note that we haven't updated TYPE_FIELDS and such of any
   variants yet, so we need to look at the main one.  */

static tree
synthesize_exception_spec (tree type, tree (*extractor) (tree, void*),
			   void *client)
{
  tree raises = empty_except_spec;
  tree fields = TYPE_FIELDS (type);
  tree binfo, base_binfo;
  int i;

  for (binfo = TYPE_BINFO (type), i = 0;
       BINFO_BASE_ITERATE (binfo, i, base_binfo); i++)
    {
      tree fn = (*extractor) (BINFO_TYPE (base_binfo), client);
      if (fn)
	{
	  tree fn_raises = TYPE_RAISES_EXCEPTIONS (TREE_TYPE (fn));

	  raises = merge_exception_specifiers (raises, fn_raises);
	}
    }
  for (; fields; fields = TREE_CHAIN (fields))
    {
      tree type = TREE_TYPE (fields);
      tree fn;

      if (TREE_CODE (fields) != FIELD_DECL || DECL_ARTIFICIAL (fields))
	continue;
      while (TREE_CODE (type) == ARRAY_TYPE)
	type = TREE_TYPE (type);
      if (!CLASS_TYPE_P (type))
	continue;

      fn = (*extractor) (type, client);
      if (fn)
	{
	  tree fn_raises = TYPE_RAISES_EXCEPTIONS (TREE_TYPE (fn));

	  raises = merge_exception_specifiers (raises, fn_raises);
	}
    }
  return raises;
}

/* Locate the dtor of TYPE.  */

static tree
locate_dtor (tree type, void *client ATTRIBUTE_UNUSED)
{
  return CLASSTYPE_DESTRUCTORS (type);
}

/* Locate the default ctor of TYPE.  */

static tree
locate_ctor (tree type, void *client ATTRIBUTE_UNUSED)
{
  tree fns;

  if (!TYPE_HAS_DEFAULT_CONSTRUCTOR (type))
    return NULL_TREE;

  /* Call lookup_fnfields_1 to create the constructor declarations, if
     necessary.  */
  if (CLASSTYPE_LAZY_DEFAULT_CTOR (type))
    return lazily_declare_fn (sfk_constructor, type);

  for (fns = CLASSTYPE_CONSTRUCTORS (type); fns; fns = OVL_NEXT (fns))
    {
      tree fn = OVL_CURRENT (fns);
      tree parms = TYPE_ARG_TYPES (TREE_TYPE (fn));

      parms = skip_artificial_parms_for (fn, parms);

      if (sufficient_parms_p (parms))
	return fn;
    }
  gcc_unreachable ();
}

struct copy_data
{
  tree name;
  int quals;
};

/* Locate the copy ctor or copy assignment of TYPE. CLIENT_
   points to a COPY_DATA holding the name (NULL for the ctor)
   and desired qualifiers of the source operand.  */

static tree
locate_copy (tree type, void *client_)
{
  struct copy_data *client = (struct copy_data *)client_;
  tree fns;
  tree best = NULL_TREE;
  bool excess_p = false;

  if (client->name)
    {
      int ix;
      ix = lookup_fnfields_1 (type, client->name);
      if (ix < 0)
	return NULL_TREE;
      fns = VEC_index (tree, CLASSTYPE_METHOD_VEC (type), ix);
    }
  else if (TYPE_HAS_INIT_REF (type))
    {
      /* If construction of the copy constructor was postponed, create
	 it now.  */
      if (CLASSTYPE_LAZY_COPY_CTOR (type))
	lazily_declare_fn (sfk_copy_constructor, type);
      fns = CLASSTYPE_CONSTRUCTORS (type);
    }
  else
    return NULL_TREE;
  for (; fns; fns = OVL_NEXT (fns))
    {
      tree fn = OVL_CURRENT (fns);
      tree parms = TYPE_ARG_TYPES (TREE_TYPE (fn));
      tree src_type;
      int excess;
      int quals;

      parms = skip_artificial_parms_for (fn, parms);
      if (!parms)
	continue;
      src_type = non_reference (TREE_VALUE (parms));

      if (src_type == error_mark_node)
        return NULL_TREE;

      if (!same_type_ignoring_top_level_qualifiers_p (src_type, type))
	continue;
      if (!sufficient_parms_p (TREE_CHAIN (parms)))
	continue;
      quals = cp_type_quals (src_type);
      if (client->quals & ~quals)
	continue;
      excess = quals & ~client->quals;
      if (!best || (excess_p && !excess))
	{
	  best = fn;
	  excess_p = excess;
	}
      else
	/* Ambiguous */
	return NULL_TREE;
    }
  return best;
}

/* Implicitly declare the special function indicated by KIND, as a
   member of TYPE.  For copy constructors and assignment operators,
   CONST_P indicates whether these functions should take a const
   reference argument or a non-const reference.  Returns the
   FUNCTION_DECL for the implicitly declared function.  */

static tree
implicitly_declare_fn (special_function_kind kind, tree type, bool const_p)
{
  tree fn;
  tree parameter_types = void_list_node;
  tree return_type;
  tree fn_type;
  tree raises = empty_except_spec;
  tree rhs_parm_type = NULL_TREE;
  tree this_parm;
  tree name;
  HOST_WIDE_INT saved_processing_template_decl;

  /* Because we create declarations for implicitly declared functions
     lazily, we may be creating the declaration for a member of TYPE
     while in some completely different context.  However, TYPE will
     never be a dependent class (because we never want to do lookups
     for implicitly defined functions in a dependent class).
     Furthermore, we must set PROCESSING_TEMPLATE_DECL to zero here
     because we only create clones for constructors and destructors
     when not in a template.  */
  gcc_assert (!dependent_type_p (type));
  saved_processing_template_decl = processing_template_decl;
  processing_template_decl = 0;

  type = TYPE_MAIN_VARIANT (type);

  if (targetm.cxx.cdtor_returns_this () && !TYPE_FOR_JAVA (type))
    {
      if (kind == sfk_destructor)
	/* See comment in check_special_function_return_type.  */
	return_type = build_pointer_type (void_type_node);
      else
	return_type = build_pointer_type (type);
    }
  else
    return_type = void_type_node;

  switch (kind)
    {
    case sfk_destructor:
      /* Destructor.  */
      name = constructor_name (type);
      raises = synthesize_exception_spec (type, &locate_dtor, 0);
      break;

    case sfk_constructor:
      /* Default constructor.  */
      name = constructor_name (type);
      raises = synthesize_exception_spec (type, &locate_ctor, 0);
      break;

    case sfk_copy_constructor:
    case sfk_assignment_operator:
    {
      struct copy_data data;

      data.name = NULL;
      data.quals = 0;
      if (kind == sfk_assignment_operator)
	{
	  return_type = build_reference_type (type);
	  name = ansi_assopname (NOP_EXPR);
	  data.name = name;
	}
      else
	name = constructor_name (type);

      if (const_p)
	{
	  data.quals = TYPE_QUAL_CONST;
	  rhs_parm_type = build_qualified_type (type, TYPE_QUAL_CONST);
	}
      else
	rhs_parm_type = type;
      rhs_parm_type = build_reference_type (rhs_parm_type);
      parameter_types = tree_cons (NULL_TREE, rhs_parm_type, parameter_types);
      raises = synthesize_exception_spec (type, &locate_copy, &data);
      break;
    }
    default:
      gcc_unreachable ();
    }

  /* Create the function.  */
  fn_type = build_method_type_directly (type, return_type, parameter_types);
  if (raises)
    fn_type = build_exception_variant (fn_type, raises);
  fn = build_lang_decl (FUNCTION_DECL, name, fn_type);
  DECL_SOURCE_LOCATION (fn) = DECL_SOURCE_LOCATION (TYPE_NAME (type));
  if (kind == sfk_constructor || kind == sfk_copy_constructor)
    DECL_CONSTRUCTOR_P (fn) = 1;
  else if (kind == sfk_destructor)
    DECL_DESTRUCTOR_P (fn) = 1;
  else
    {
      DECL_ASSIGNMENT_OPERATOR_P (fn) = 1;
      SET_OVERLOADED_OPERATOR_CODE (fn, NOP_EXPR);
    }
  /* Create the explicit arguments.  */
  if (rhs_parm_type)
    {
      /* Note that this parameter is *not* marked DECL_ARTIFICIAL; we
	 want its type to be included in the mangled function
	 name.  */
      DECL_ARGUMENTS (fn) = cp_build_parm_decl (NULL_TREE, rhs_parm_type);
      TREE_READONLY (DECL_ARGUMENTS (fn)) = 1;
    }
  /* Add the "this" parameter.  */
  this_parm = build_this_parm (fn_type, TYPE_UNQUALIFIED);
  TREE_CHAIN (this_parm) = DECL_ARGUMENTS (fn);
  DECL_ARGUMENTS (fn) = this_parm;

  grokclassfn (type, fn, kind == sfk_destructor ? DTOR_FLAG : NO_SPECIAL);
  set_linkage_according_to_type (type, fn);
  rest_of_decl_compilation (fn, toplevel_bindings_p (), at_eof);
  DECL_IN_AGGR_P (fn) = 1;
  DECL_ARTIFICIAL (fn) = 1;
  DECL_NOT_REALLY_EXTERN (fn) = 1;
  DECL_DECLARED_INLINE_P (fn) = 1;
  DECL_INLINE (fn) = 1;
  gcc_assert (!TREE_USED (fn));

  /* Restore PROCESSING_TEMPLATE_DECL.  */
  processing_template_decl = saved_processing_template_decl;

  return fn;
}

/* Add an implicit declaration to TYPE for the kind of function
   indicated by SFK.  Return the FUNCTION_DECL for the new implicit
   declaration.  */

tree
lazily_declare_fn (special_function_kind sfk, tree type)
{
  tree fn;
  bool const_p;

  /* Figure out whether or not the argument has a const reference
     type.  */
  if (sfk == sfk_copy_constructor)
    const_p = TYPE_HAS_CONST_INIT_REF (type);
  else if (sfk == sfk_assignment_operator)
    const_p = TYPE_HAS_CONST_ASSIGN_REF (type);
  else
    /* In this case, CONST_P will be ignored.  */
    const_p = false;
  /* Declare the function.  */
  fn = implicitly_declare_fn (sfk, type, const_p);
  /* A destructor may be virtual.  */
  if (sfk == sfk_destructor)
    check_for_override (fn, type);
  /* Add it to CLASSTYPE_METHOD_VEC.  */
  add_method (type, fn, NULL_TREE);
  /* Add it to TYPE_METHODS.  */
  if (sfk == sfk_destructor
      && DECL_VIRTUAL_P (fn)
      && abi_version_at_least (2))
    /* The ABI requires that a virtual destructor go at the end of the
       vtable.  */
    TYPE_METHODS (type) = chainon (TYPE_METHODS (type), fn);
  else
    {
      /* G++ 3.2 put the implicit destructor at the *beginning* of the
	 TYPE_METHODS list, which cause the destructor to be emitted
	 in an incorrect location in the vtable.  */
      if (warn_abi && DECL_VIRTUAL_P (fn))
	warning (OPT_Wabi, "vtable layout for class %qT may not be ABI-compliant"
		 "and may change in a future version of GCC due to "
		 "implicit virtual destructor",
		 type);
      TREE_CHAIN (fn) = TYPE_METHODS (type);
      TYPE_METHODS (type) = fn;
    }
  maybe_add_class_template_decl_list (type, fn, /*friend_p=*/0);
  if (sfk == sfk_assignment_operator)
    CLASSTYPE_LAZY_ASSIGNMENT_OP (type) = 0;
  else
    {
      /* Remember that the function has been created.  */
      if (sfk == sfk_constructor)
	CLASSTYPE_LAZY_DEFAULT_CTOR (type) = 0;
      else if (sfk == sfk_copy_constructor)
	CLASSTYPE_LAZY_COPY_CTOR (type) = 0;
      else if (sfk == sfk_destructor)
	CLASSTYPE_LAZY_DESTRUCTOR (type) = 0;
      /* Create appropriate clones.  */
      clone_function_decl (fn, /*update_method_vec=*/true);
    }

  return fn;
}

/* Given a FUNCTION_DECL FN and a chain LIST, skip as many elements of LIST
   as there are artificial parms in FN.  */

tree
skip_artificial_parms_for (tree fn, tree list)
{
  if (DECL_NONSTATIC_MEMBER_FUNCTION_P (fn))
    list = TREE_CHAIN (list);
  else
    return list;

  if (DECL_HAS_IN_CHARGE_PARM_P (fn))
    list = TREE_CHAIN (list);
  if (DECL_HAS_VTT_PARM_P (fn))
    list = TREE_CHAIN (list);
  return list;
}

#include "gt-cp-method.h"
