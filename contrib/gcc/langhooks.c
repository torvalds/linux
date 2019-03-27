/* Default language-specific hooks.
   Copyright 2001, 2002, 2003, 2004, 2005, 2006 Free Software Foundation, Inc.
   Contributed by Alexandre Oliva  <aoliva@redhat.com>

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
#include "intl.h"
#include "tm.h"
#include "toplev.h"
#include "tree.h"
#include "tree-inline.h"
#include "tree-gimple.h"
#include "rtl.h"
#include "insn-config.h"
#include "integrate.h"
#include "flags.h"
#include "langhooks.h"
#include "langhooks-def.h"
#include "ggc.h"
#include "diagnostic.h"

/* Do nothing; in many cases the default hook.  */

void
lhd_do_nothing (void)
{
}

/* Do nothing (tree).  */

void
lhd_do_nothing_t (tree ARG_UNUSED (t))
{
}

/* Do nothing (int).  */

void
lhd_do_nothing_i (int ARG_UNUSED (i))
{
}

/* Do nothing (int, int, int).  Return NULL_TREE.  */

tree
lhd_do_nothing_iii_return_null_tree (int ARG_UNUSED (i),
				     int ARG_UNUSED (j),
				     int ARG_UNUSED (k))
{
  return NULL_TREE;
}

/* Do nothing (function).  */

void
lhd_do_nothing_f (struct function * ARG_UNUSED (f))
{
}

/* Do nothing (return the tree node passed).  */

tree
lhd_return_tree (tree t)
{
  return t;
}

/* Do nothing (return NULL_TREE).  */

tree
lhd_return_null_tree_v (void)
{
  return NULL_TREE;
}

/* Do nothing (return NULL_TREE).  */

tree
lhd_return_null_tree (tree ARG_UNUSED (t))
{
  return NULL_TREE;
}

/* The default post options hook.  */

bool
lhd_post_options (const char ** ARG_UNUSED (pfilename))
{
  return false;
}

/* Called from by print-tree.c.  */

void
lhd_print_tree_nothing (FILE * ARG_UNUSED (file),
			tree ARG_UNUSED (node),
			int ARG_UNUSED (indent))
{
}

/* Called from safe_from_p.  */

int
lhd_safe_from_p (rtx ARG_UNUSED (x), tree ARG_UNUSED (exp))
{
  return 1;
}

/* Called from staticp.  */

tree
lhd_staticp (tree ARG_UNUSED (exp))
{
  return NULL;
}

/* Called from check_global_declarations.  */

bool
lhd_warn_unused_global_decl (tree decl)
{
  /* This is what used to exist in check_global_declarations.  Probably
     not many of these actually apply to non-C languages.  */

  if (TREE_CODE (decl) == FUNCTION_DECL && DECL_INLINE (decl))
    return false;
  if (TREE_CODE (decl) == VAR_DECL && TREE_READONLY (decl))
    return false;
  if (DECL_IN_SYSTEM_HEADER (decl))
    return false;

  return true;
}

/* Set the DECL_ASSEMBLER_NAME for DECL.  */
void
lhd_set_decl_assembler_name (tree decl)
{
  /* The language-independent code should never use the
     DECL_ASSEMBLER_NAME for lots of DECLs.  Only FUNCTION_DECLs and
     VAR_DECLs for variables with static storage duration need a real
     DECL_ASSEMBLER_NAME.  */
  gcc_assert (TREE_CODE (decl) == FUNCTION_DECL
	      || (TREE_CODE (decl) == VAR_DECL
		  && (TREE_STATIC (decl)
		      || DECL_EXTERNAL (decl)
		      || TREE_PUBLIC (decl))));
  
  /* By default, assume the name to use in assembly code is the same
     as that used in the source language.  (That's correct for C, and
     GCC used to set DECL_ASSEMBLER_NAME to the same value as
     DECL_NAME in build_decl, so this choice provides backwards
     compatibility with existing front-ends.
      
     Can't use just the variable's own name for a variable whose scope
     is less than the whole compilation.  Concatenate a distinguishing
     number - we use the DECL_UID.  */
  if (TREE_PUBLIC (decl) || DECL_CONTEXT (decl) == NULL_TREE)
    SET_DECL_ASSEMBLER_NAME (decl, DECL_NAME (decl));
  else
    {
      const char *name = IDENTIFIER_POINTER (DECL_NAME (decl));
      char *label;
      
      ASM_FORMAT_PRIVATE_NAME (label, name, DECL_UID (decl));
      SET_DECL_ASSEMBLER_NAME (decl, get_identifier (label));
    }
}

/* By default we always allow bit-field based optimizations.  */
bool
lhd_can_use_bit_fields_p (void)
{
  return true;
}

/* Type promotion for variable arguments.  */
tree
lhd_type_promotes_to (tree ARG_UNUSED (type))
{
  gcc_unreachable ();
}

/* Registration of machine- or os-specific builtin types.  */
void
lhd_register_builtin_type (tree ARG_UNUSED (type),
			   const char * ARG_UNUSED (name))
{
}

/* Invalid use of an incomplete type.  */
void
lhd_incomplete_type_error (tree ARG_UNUSED (value), tree type)
{
  gcc_assert (TREE_CODE (type) == ERROR_MARK);
  return;
}

/* Provide a default routine for alias sets that always returns -1.  This
   is used by languages that don't need to do anything special.  */

HOST_WIDE_INT
lhd_get_alias_set (tree ARG_UNUSED (t))
{
  return -1;
}

/* Provide a hook routine for alias sets that always returns 0.  This is
   used by languages that haven't deal with alias sets yet.  */

HOST_WIDE_INT
hook_get_alias_set_0 (tree ARG_UNUSED (t))
{
  return 0;
}

/* This is the default expand_expr function.  */

rtx
lhd_expand_expr (tree ARG_UNUSED (t), rtx ARG_UNUSED (r),
		 enum machine_mode ARG_UNUSED (mm),
		 int ARG_UNUSED (em),
		 rtx * ARG_UNUSED (a))
{
  gcc_unreachable ();
}

/* The default language-specific function for expanding a decl.  After
   the language-independent cases are handled, this function will be
   called.  If this function is not defined, it is assumed that
   declarations other than those for variables and labels do not require
   any RTL generation.  */

int
lhd_expand_decl (tree ARG_UNUSED (t))
{
  return 0;
}

/* This is the default decl_printable_name function.  */

const char *
lhd_decl_printable_name (tree decl, int ARG_UNUSED (verbosity))
{
  gcc_assert (decl && DECL_NAME (decl));
  return IDENTIFIER_POINTER (DECL_NAME (decl));
}

/* This is the default dwarf_name function.  */

const char *
lhd_dwarf_name (tree t, int verbosity)
{
  gcc_assert (DECL_P (t));

  return lang_hooks.decl_printable_name (t, verbosity);
}

/* This compares two types for equivalence ("compatible" in C-based languages).
   This routine should only return 1 if it is sure.  It should not be used
   in contexts where erroneously returning 0 causes problems.  */

int
lhd_types_compatible_p (tree x, tree y)
{
  return TYPE_MAIN_VARIANT (x) == TYPE_MAIN_VARIANT (y);
}

/* lang_hooks.tree_inlining.walk_subtrees is called by walk_tree()
   after handling common cases, but before walking code-specific
   sub-trees.  If this hook is overridden for a language, it should
   handle language-specific tree codes, as well as language-specific
   information associated to common tree codes.  If a tree node is
   completely handled within this function, it should set *SUBTREES to
   0, so that generic handling isn't attempted.  The generic handling
   cannot deal with language-specific tree codes, so make sure it is
   set properly.  Both SUBTREES and *SUBTREES is guaranteed to be
   nonzero when the function is called.  */

tree
lhd_tree_inlining_walk_subtrees (tree *tp ATTRIBUTE_UNUSED,
				 int *subtrees ATTRIBUTE_UNUSED,
				 walk_tree_fn func ATTRIBUTE_UNUSED,
				 void *data ATTRIBUTE_UNUSED,
				 struct pointer_set_t *pset ATTRIBUTE_UNUSED)
{
  return NULL_TREE;
}

/* lang_hooks.tree_inlining.cannot_inline_tree_fn is called to
   determine whether there are language-specific reasons for not
   inlining a given function.  */

int
lhd_tree_inlining_cannot_inline_tree_fn (tree *fnp)
{
  if (flag_really_no_inline
      && lookup_attribute ("always_inline", DECL_ATTRIBUTES (*fnp)) == NULL)
    return 1;

  return 0;
}

/* lang_hooks.tree_inlining.disregard_inline_limits is called to
   determine whether a function should be considered for inlining even
   if it would exceed inlining limits.  */

int
lhd_tree_inlining_disregard_inline_limits (tree fn)
{
  if (lookup_attribute ("always_inline", DECL_ATTRIBUTES (fn)) != NULL)
    return 1;

  return 0;
}

/* lang_hooks.tree_inlining.add_pending_fn_decls is called before
   starting to inline a function, to push any language-specific
   functions that should not be inlined into the current function,
   into VAFNP.  PFN is the top of varray, and should be returned if no
   functions are pushed into VAFNP.  The top of the varray should be
   returned.  */

tree
lhd_tree_inlining_add_pending_fn_decls (void *vafnp ATTRIBUTE_UNUSED, tree pfn)
{
  return pfn;
}

/* lang_hooks.tree_inlining.auto_var_in_fn_p is called to determine
   whether VT is an automatic variable defined in function FT.  */

int
lhd_tree_inlining_auto_var_in_fn_p (tree var, tree fn)
{
  return (DECL_P (var) && DECL_CONTEXT (var) == fn
	  && (((TREE_CODE (var) == VAR_DECL || TREE_CODE (var) == PARM_DECL)
	       && ! TREE_STATIC (var))
	      || TREE_CODE (var) == LABEL_DECL
	      || TREE_CODE (var) == RESULT_DECL));
}

/* lang_hooks.tree_inlining.anon_aggr_type_p determines whether T is a
   type node representing an anonymous aggregate (union, struct, etc),
   i.e., one whose members are in the same scope as the union itself.  */

int
lhd_tree_inlining_anon_aggr_type_p (tree t ATTRIBUTE_UNUSED)
{
  return 0;
}

/* lang_hooks.tree_inlining.start_inlining and end_inlining perform any
   language-specific bookkeeping necessary for processing
   FN. start_inlining returns nonzero if inlining should proceed, zero if
   not.

   For instance, the C++ version keeps track of template instantiations to
   avoid infinite recursion.  */

int
lhd_tree_inlining_start_inlining (tree fn ATTRIBUTE_UNUSED)
{
  return 1;
}

void
lhd_tree_inlining_end_inlining (tree fn ATTRIBUTE_UNUSED)
{
}

/* lang_hooks.tree_inlining.convert_parm_for_inlining performs any
   language-specific conversion before assigning VALUE to PARM.  */

tree
lhd_tree_inlining_convert_parm_for_inlining (tree parm ATTRIBUTE_UNUSED,
					     tree value,
					     tree fndecl ATTRIBUTE_UNUSED,
					     int argnum ATTRIBUTE_UNUSED)
{
  return value;
}

/* lang_hooks.tree_dump.dump_tree:  Dump language-specific parts of tree
   nodes.  Returns nonzero if it does not want the usual dumping of the
   second argument.  */

bool
lhd_tree_dump_dump_tree (void *di ATTRIBUTE_UNUSED, tree t ATTRIBUTE_UNUSED)
{
  return false;
}

/* lang_hooks.tree_dump.type_qual:  Determine type qualifiers in a
   language-specific way.  */

int
lhd_tree_dump_type_quals (tree t)
{
  return TYPE_QUALS (t);
}

/* lang_hooks.expr_size: Determine the size of the value of an expression T
   in a language-specific way.  Returns a tree for the size in bytes.  */

tree
lhd_expr_size (tree exp)
{
  if (DECL_P (exp)
      && DECL_SIZE_UNIT (exp) != 0)
    return DECL_SIZE_UNIT (exp);
  else
    return size_in_bytes (TREE_TYPE (exp));
}

/* lang_hooks.gimplify_expr re-writes *EXPR_P into GIMPLE form.  */

int
lhd_gimplify_expr (tree *expr_p ATTRIBUTE_UNUSED, tree *pre_p ATTRIBUTE_UNUSED,
		   tree *post_p ATTRIBUTE_UNUSED)
{
  return GS_UNHANDLED;
}

/* lang_hooks.tree_size: Determine the size of a tree with code C,
   which is a language-specific tree code in category tcc_constant or
   tcc_exceptional.  The default expects never to be called.  */
size_t
lhd_tree_size (enum tree_code c ATTRIBUTE_UNUSED)
{
  gcc_unreachable ();
}

/* Return true if decl, which is a function decl, may be called by a
   sibcall.  */

bool
lhd_decl_ok_for_sibcall (tree decl ATTRIBUTE_UNUSED)
{
  return true;
}

/* Return the COMDAT group into which DECL should be placed.  */

const char *
lhd_comdat_group (tree decl)
{
  return IDENTIFIER_POINTER (DECL_ASSEMBLER_NAME (decl));
}

/* lang_hooks.decls.final_write_globals: perform final processing on
   global variables.  */
void
write_global_declarations (void)
{
  /* Really define vars that have had only a tentative definition.
     Really output inline functions that must actually be callable
     and have not been output so far.  */

  tree globals = lang_hooks.decls.getdecls ();
  int len = list_length (globals);
  tree *vec = XNEWVEC (tree, len);
  int i;
  tree decl;

  /* Process the decls in reverse order--earliest first.
     Put them into VEC from back to front, then take out from front.  */

  for (i = 0, decl = globals; i < len; i++, decl = TREE_CHAIN (decl))
    vec[len - i - 1] = decl;

  wrapup_global_declarations (vec, len);
  check_global_declarations (vec, len);
  emit_debug_global_declarations (vec, len);

  /* Clean up.  */
  free (vec);
}

/* Called to perform language-specific initialization of CTX.  */
void
lhd_initialize_diagnostics (struct diagnostic_context *ctx ATTRIBUTE_UNUSED)
{
}

/* The default function to print out name of current function that caused
   an error.  */
void
lhd_print_error_function (diagnostic_context *context, const char *file)
{
  if (diagnostic_last_function_changed (context))
    {
      const char *old_prefix = context->printer->prefix;
      char *new_prefix = file ? file_name_as_prefix (file) : NULL;

      pp_set_prefix (context->printer, new_prefix);

      if (current_function_decl == NULL)
	pp_printf (context->printer, _("At top level:"));
      else
	{
	  if (TREE_CODE (TREE_TYPE (current_function_decl)) == METHOD_TYPE)
	    pp_printf
	      (context->printer, _("In member function %qs:"),
	       lang_hooks.decl_printable_name (current_function_decl, 2));
	  else
	    pp_printf
	      (context->printer, _("In function %qs:"),
	       lang_hooks.decl_printable_name (current_function_decl, 2));
	}

      diagnostic_set_last_function (context);
      pp_flush (context->printer);
      context->printer->prefix = old_prefix;
      free ((char*) new_prefix);
    }
}

tree
lhd_callgraph_analyze_expr (tree *tp ATTRIBUTE_UNUSED,
			    int *walk_subtrees ATTRIBUTE_UNUSED,
			    tree decl ATTRIBUTE_UNUSED)
{
  return NULL;
}

tree
lhd_make_node (enum tree_code code)
{
  return make_node (code);
}

HOST_WIDE_INT
lhd_to_target_charset (HOST_WIDE_INT c)
{
  return c;
}

tree
lhd_expr_to_decl (tree expr, bool *tc ATTRIBUTE_UNUSED,
		  bool *ti ATTRIBUTE_UNUSED, bool *se ATTRIBUTE_UNUSED)
{
  return expr;
}

/* Return sharing kind if OpenMP sharing attribute of DECL is
   predetermined, OMP_CLAUSE_DEFAULT_UNSPECIFIED otherwise.  */

enum omp_clause_default_kind
lhd_omp_predetermined_sharing (tree decl ATTRIBUTE_UNUSED)
{
  if (DECL_ARTIFICIAL (decl))
    return OMP_CLAUSE_DEFAULT_SHARED;
  return OMP_CLAUSE_DEFAULT_UNSPECIFIED;
}

/* Generate code to copy SRC to DST.  */

tree
lhd_omp_assignment (tree clause ATTRIBUTE_UNUSED, tree dst, tree src)
{
  return build2 (MODIFY_EXPR, void_type_node, dst, src);
}

/* Register language specific type size variables as potentially OpenMP
   firstprivate variables.  */

void
lhd_omp_firstprivatize_type_sizes (struct gimplify_omp_ctx *c ATTRIBUTE_UNUSED,
				   tree t ATTRIBUTE_UNUSED)
{
}

/* APPLE LOCAL begin radar 6353006  */
tree 
lhd_build_generic_block_struct_type (void)
{
  return NULL_TREE;
}
/* APPLE LOCAL end radar 6353006  */

/* APPLE LOCAL begin radar 6386976  */
bool
lhd_is_runtime_specific_type (tree type ATTRIBUTE_UNUSED)
{
  return false;
}
/* APPLE LOCAL end radar 6386976  */
