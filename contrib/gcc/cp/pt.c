/* Handle parameterized types (templates) for GNU C++.
   Copyright (C) 1992, 1993, 1994, 1995, 1996, 1997, 1998, 1999, 2000,
   2001, 2002, 2003, 2004, 2005  Free Software Foundation, Inc.
   Written by Ken Raeburn (raeburn@cygnus.com) while at Watchmaker Computing.
   Rewritten by Jason Merrill (jason@cygnus.com).

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

/* Known bugs or deficiencies include:

     all methods must be provided in header files; can't use a source
     file that contains only the method templates and "just win".  */

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "obstack.h"
#include "tree.h"
#include "pointer-set.h"
#include "flags.h"
#include "c-common.h"
#include "cp-tree.h"
#include "cp-objcp-common.h"
#include "tree-inline.h"
#include "decl.h"
#include "output.h"
#include "except.h"
#include "toplev.h"
#include "rtl.h"
#include "timevar.h"
#include "tree-iterator.h"
#include "vecprim.h"

/* The type of functions taking a tree, and some additional data, and
   returning an int.  */
typedef int (*tree_fn_t) (tree, void*);

/* The PENDING_TEMPLATES is a TREE_LIST of templates whose
   instantiations have been deferred, either because their definitions
   were not yet available, or because we were putting off doing the work.
   The TREE_PURPOSE of each entry is either a DECL (for a function or
   static data member), or a TYPE (for a class) indicating what we are
   hoping to instantiate.  The TREE_VALUE is not used.  */
static GTY(()) tree pending_templates;
static GTY(()) tree last_pending_template;

int processing_template_parmlist;
static int template_header_count;

static GTY(()) tree saved_trees;
static VEC(int,heap) *inline_parm_levels;

static GTY(()) tree current_tinst_level;

static GTY(()) tree saved_access_scope;

/* Live only within one (recursive) call to tsubst_expr.  We use
   this to pass the statement expression node from the STMT_EXPR
   to the EXPR_STMT that is its result.  */
static tree cur_stmt_expr;

/* A map from local variable declarations in the body of the template
   presently being instantiated to the corresponding instantiated
   local variables.  */
static htab_t local_specializations;

#define UNIFY_ALLOW_NONE 0
#define UNIFY_ALLOW_MORE_CV_QUAL 1
#define UNIFY_ALLOW_LESS_CV_QUAL 2
#define UNIFY_ALLOW_DERIVED 4
#define UNIFY_ALLOW_INTEGER 8
#define UNIFY_ALLOW_OUTER_LEVEL 16
#define UNIFY_ALLOW_OUTER_MORE_CV_QUAL 32
#define UNIFY_ALLOW_OUTER_LESS_CV_QUAL 64

static void push_access_scope (tree);
static void pop_access_scope (tree);
static bool resolve_overloaded_unification (tree, tree, tree, tree,
					    unification_kind_t, int);
static int try_one_overload (tree, tree, tree, tree, tree,
			     unification_kind_t, int, bool);
static int unify (tree, tree, tree, tree, int);
static void add_pending_template (tree);
static int push_tinst_level (tree);
static void pop_tinst_level (void);
static void reopen_tinst_level (tree);
static tree classtype_mangled_name (tree);
static char* mangle_class_name_for_template (const char *, tree, tree);
static tree tsubst_initializer_list (tree, tree);
static tree get_class_bindings (tree, tree, tree);
static tree coerce_template_parms (tree, tree, tree, tsubst_flags_t,
				   bool, bool);
static void tsubst_enum	(tree, tree, tree);
static tree add_to_template_args (tree, tree);
static tree add_outermost_template_args (tree, tree);
static bool check_instantiated_args (tree, tree, tsubst_flags_t);
static int maybe_adjust_types_for_deduction (unification_kind_t, tree*, tree*);
static int  type_unification_real (tree, tree, tree, tree,
				   int, unification_kind_t, int);
static void note_template_header (int);
static tree convert_nontype_argument_function (tree, tree);
static tree convert_nontype_argument (tree, tree);
static tree convert_template_argument (tree, tree, tree,
				       tsubst_flags_t, int, tree);
static int for_each_template_parm (tree, tree_fn_t, void*,
				   struct pointer_set_t*);
static tree build_template_parm_index (int, int, int, tree, tree);
static int inline_needs_template_parms (tree);
static void push_inline_template_parms_recursive (tree, int);
static tree retrieve_local_specialization (tree);
static void register_local_specialization (tree, tree);
static tree reduce_template_parm_level (tree, tree, int);
static int mark_template_parm (tree, void *);
static int template_parm_this_level_p (tree, void *);
static tree tsubst_friend_function (tree, tree);
static tree tsubst_friend_class (tree, tree);
static int can_complete_type_without_circularity (tree);
static tree get_bindings (tree, tree, tree, bool);
static int template_decl_level (tree);
static int check_cv_quals_for_unify (int, tree, tree);
static tree tsubst_template_arg (tree, tree, tsubst_flags_t, tree);
static tree tsubst_template_args (tree, tree, tsubst_flags_t, tree);
static tree tsubst_template_parms (tree, tree, tsubst_flags_t);
static void regenerate_decl_from_template (tree, tree);
static tree most_specialized_class (tree, tree);
static tree tsubst_aggr_type (tree, tree, tsubst_flags_t, tree, int);
static tree tsubst_arg_types (tree, tree, tsubst_flags_t, tree);
static tree tsubst_function_type (tree, tree, tsubst_flags_t, tree);
static bool check_specialization_scope (void);
static tree process_partial_specialization (tree);
static void set_current_access_from_decl (tree);
static void check_default_tmpl_args (tree, tree, int, int);
static tree get_template_base (tree, tree, tree, tree);
static tree try_class_unification (tree, tree, tree, tree);
static int coerce_template_template_parms (tree, tree, tsubst_flags_t,
					   tree, tree);
static int template_args_equal (tree, tree);
static void tsubst_default_arguments (tree);
static tree for_each_template_parm_r (tree *, int *, void *);
static tree copy_default_args_to_explicit_spec_1 (tree, tree);
static void copy_default_args_to_explicit_spec (tree);
static int invalid_nontype_parm_type_p (tree, tsubst_flags_t);
static int eq_local_specializations (const void *, const void *);
static bool dependent_type_p_r (tree);
static tree tsubst (tree, tree, tsubst_flags_t, tree);
static tree tsubst_expr	(tree, tree, tsubst_flags_t, tree, bool);
static tree tsubst_copy	(tree, tree, tsubst_flags_t, tree);

/* Make the current scope suitable for access checking when we are
   processing T.  T can be FUNCTION_DECL for instantiated function
   template, or VAR_DECL for static member variable (need by
   instantiate_decl).  */

static void
push_access_scope (tree t)
{
  gcc_assert (TREE_CODE (t) == FUNCTION_DECL
	      || TREE_CODE (t) == VAR_DECL);

  if (DECL_FRIEND_CONTEXT (t))
    push_nested_class (DECL_FRIEND_CONTEXT (t));
  else if (DECL_CLASS_SCOPE_P (t))
    push_nested_class (DECL_CONTEXT (t));
  else
    push_to_top_level ();

  if (TREE_CODE (t) == FUNCTION_DECL)
    {
      saved_access_scope = tree_cons
	(NULL_TREE, current_function_decl, saved_access_scope);
      current_function_decl = t;
    }
}

/* Restore the scope set up by push_access_scope.  T is the node we
   are processing.  */

static void
pop_access_scope (tree t)
{
  if (TREE_CODE (t) == FUNCTION_DECL)
    {
      current_function_decl = TREE_VALUE (saved_access_scope);
      saved_access_scope = TREE_CHAIN (saved_access_scope);
    }

  if (DECL_FRIEND_CONTEXT (t) || DECL_CLASS_SCOPE_P (t))
    pop_nested_class ();
  else
    pop_from_top_level ();
}

/* Do any processing required when DECL (a member template
   declaration) is finished.  Returns the TEMPLATE_DECL corresponding
   to DECL, unless it is a specialization, in which case the DECL
   itself is returned.  */

tree
finish_member_template_decl (tree decl)
{
  if (decl == error_mark_node)
    return error_mark_node;

  gcc_assert (DECL_P (decl));

  if (TREE_CODE (decl) == TYPE_DECL)
    {
      tree type;

      type = TREE_TYPE (decl);
      if (IS_AGGR_TYPE (type)
	  && CLASSTYPE_TEMPLATE_INFO (type)
	  && !CLASSTYPE_TEMPLATE_SPECIALIZATION (type))
	{
	  tree tmpl = CLASSTYPE_TI_TEMPLATE (type);
	  check_member_template (tmpl);
	  return tmpl;
	}
      return NULL_TREE;
    }
  else if (TREE_CODE (decl) == FIELD_DECL)
    error ("data member %qD cannot be a member template", decl);
  else if (DECL_TEMPLATE_INFO (decl))
    {
      if (!DECL_TEMPLATE_SPECIALIZATION (decl))
	{
	  check_member_template (DECL_TI_TEMPLATE (decl));
	  return DECL_TI_TEMPLATE (decl);
	}
      else
	return decl;
    }
  else
    error ("invalid member template declaration %qD", decl);

  return error_mark_node;
}

/* Returns the template nesting level of the indicated class TYPE.

   For example, in:
     template <class T>
     struct A
     {
       template <class U>
       struct B {};
     };

   A<T>::B<U> has depth two, while A<T> has depth one.
   Both A<T>::B<int> and A<int>::B<U> have depth one, if
   they are instantiations, not specializations.

   This function is guaranteed to return 0 if passed NULL_TREE so
   that, for example, `template_class_depth (current_class_type)' is
   always safe.  */

int
template_class_depth (tree type)
{
  int depth;

  for (depth = 0;
       type && TREE_CODE (type) != NAMESPACE_DECL;
       type = (TREE_CODE (type) == FUNCTION_DECL)
	 ? CP_DECL_CONTEXT (type) : TYPE_CONTEXT (type))
    {
      if (TREE_CODE (type) != FUNCTION_DECL)
	{
	  if (CLASSTYPE_TEMPLATE_INFO (type)
	      && PRIMARY_TEMPLATE_P (CLASSTYPE_TI_TEMPLATE (type))
	      && uses_template_parms (CLASSTYPE_TI_ARGS (type)))
	    ++depth;
	}
      else
	{
	  if (DECL_TEMPLATE_INFO (type)
	      && PRIMARY_TEMPLATE_P (DECL_TI_TEMPLATE (type))
	      && uses_template_parms (DECL_TI_ARGS (type)))
	    ++depth;
	}
    }

  return depth;
}

/* Returns 1 if processing DECL as part of do_pending_inlines
   needs us to push template parms.  */

static int
inline_needs_template_parms (tree decl)
{
  if (! DECL_TEMPLATE_INFO (decl))
    return 0;

  return (TMPL_PARMS_DEPTH (DECL_TEMPLATE_PARMS (most_general_template (decl)))
	  > (processing_template_decl + DECL_TEMPLATE_SPECIALIZATION (decl)));
}

/* Subroutine of maybe_begin_member_template_processing.
   Push the template parms in PARMS, starting from LEVELS steps into the
   chain, and ending at the beginning, since template parms are listed
   innermost first.  */

static void
push_inline_template_parms_recursive (tree parmlist, int levels)
{
  tree parms = TREE_VALUE (parmlist);
  int i;

  if (levels > 1)
    push_inline_template_parms_recursive (TREE_CHAIN (parmlist), levels - 1);

  ++processing_template_decl;
  current_template_parms
    = tree_cons (size_int (processing_template_decl),
		 parms, current_template_parms);
  TEMPLATE_PARMS_FOR_INLINE (current_template_parms) = 1;

  begin_scope (TREE_VEC_LENGTH (parms) ? sk_template_parms : sk_template_spec,
	       NULL);
  for (i = 0; i < TREE_VEC_LENGTH (parms); ++i)
    {
      tree parm = TREE_VALUE (TREE_VEC_ELT (parms, i));

      if (parm == error_mark_node)
	continue;

      gcc_assert (DECL_P (parm));

      switch (TREE_CODE (parm))
	{
	case TYPE_DECL:
	case TEMPLATE_DECL:
	  pushdecl (parm);
	  break;

	case PARM_DECL:
	  {
	    /* Make a CONST_DECL as is done in process_template_parm.
	       It is ugly that we recreate this here; the original
	       version built in process_template_parm is no longer
	       available.  */
	    tree decl = build_decl (CONST_DECL, DECL_NAME (parm),
				    TREE_TYPE (parm));
	    DECL_ARTIFICIAL (decl) = 1;
	    TREE_CONSTANT (decl) = 1;
	    TREE_INVARIANT (decl) = 1;
	    TREE_READONLY (decl) = 1;
	    DECL_INITIAL (decl) = DECL_INITIAL (parm);
	    SET_DECL_TEMPLATE_PARM_P (decl);
	    pushdecl (decl);
	  }
	  break;

	default:
	  gcc_unreachable ();
	}
    }
}

/* Restore the template parameter context for a member template or
   a friend template defined in a class definition.  */

void
maybe_begin_member_template_processing (tree decl)
{
  tree parms;
  int levels = 0;

  if (inline_needs_template_parms (decl))
    {
      parms = DECL_TEMPLATE_PARMS (most_general_template (decl));
      levels = TMPL_PARMS_DEPTH (parms) - processing_template_decl;

      if (DECL_TEMPLATE_SPECIALIZATION (decl))
	{
	  --levels;
	  parms = TREE_CHAIN (parms);
	}

      push_inline_template_parms_recursive (parms, levels);
    }

  /* Remember how many levels of template parameters we pushed so that
     we can pop them later.  */
  VEC_safe_push (int, heap, inline_parm_levels, levels);
}

/* Undo the effects of maybe_begin_member_template_processing.  */

void
maybe_end_member_template_processing (void)
{
  int i;
  int last;

  if (VEC_length (int, inline_parm_levels) == 0)
    return;

  last = VEC_pop (int, inline_parm_levels);
  for (i = 0; i < last; ++i)
    {
      --processing_template_decl;
      current_template_parms = TREE_CHAIN (current_template_parms);
      poplevel (0, 0, 0);
    }
}

/* Return a new template argument vector which contains all of ARGS,
   but has as its innermost set of arguments the EXTRA_ARGS.  */

static tree
add_to_template_args (tree args, tree extra_args)
{
  tree new_args;
  int extra_depth;
  int i;
  int j;

  extra_depth = TMPL_ARGS_DEPTH (extra_args);
  new_args = make_tree_vec (TMPL_ARGS_DEPTH (args) + extra_depth);

  for (i = 1; i <= TMPL_ARGS_DEPTH (args); ++i)
    SET_TMPL_ARGS_LEVEL (new_args, i, TMPL_ARGS_LEVEL (args, i));

  for (j = 1; j <= extra_depth; ++j, ++i)
    SET_TMPL_ARGS_LEVEL (new_args, i, TMPL_ARGS_LEVEL (extra_args, j));

  return new_args;
}

/* Like add_to_template_args, but only the outermost ARGS are added to
   the EXTRA_ARGS.  In particular, all but TMPL_ARGS_DEPTH
   (EXTRA_ARGS) levels are added.  This function is used to combine
   the template arguments from a partial instantiation with the
   template arguments used to attain the full instantiation from the
   partial instantiation.  */

static tree
add_outermost_template_args (tree args, tree extra_args)
{
  tree new_args;

  /* If there are more levels of EXTRA_ARGS than there are ARGS,
     something very fishy is going on.  */
  gcc_assert (TMPL_ARGS_DEPTH (args) >= TMPL_ARGS_DEPTH (extra_args));

  /* If *all* the new arguments will be the EXTRA_ARGS, just return
     them.  */
  if (TMPL_ARGS_DEPTH (args) == TMPL_ARGS_DEPTH (extra_args))
    return extra_args;

  /* For the moment, we make ARGS look like it contains fewer levels.  */
  TREE_VEC_LENGTH (args) -= TMPL_ARGS_DEPTH (extra_args);

  new_args = add_to_template_args (args, extra_args);

  /* Now, we restore ARGS to its full dimensions.  */
  TREE_VEC_LENGTH (args) += TMPL_ARGS_DEPTH (extra_args);

  return new_args;
}

/* Return the N levels of innermost template arguments from the ARGS.  */

tree
get_innermost_template_args (tree args, int n)
{
  tree new_args;
  int extra_levels;
  int i;

  gcc_assert (n >= 0);

  /* If N is 1, just return the innermost set of template arguments.  */
  if (n == 1)
    return TMPL_ARGS_LEVEL (args, TMPL_ARGS_DEPTH (args));

  /* If we're not removing anything, just return the arguments we were
     given.  */
  extra_levels = TMPL_ARGS_DEPTH (args) - n;
  gcc_assert (extra_levels >= 0);
  if (extra_levels == 0)
    return args;

  /* Make a new set of arguments, not containing the outer arguments.  */
  new_args = make_tree_vec (n);
  for (i = 1; i <= n; ++i)
    SET_TMPL_ARGS_LEVEL (new_args, i,
			 TMPL_ARGS_LEVEL (args, i + extra_levels));

  return new_args;
}

/* We've got a template header coming up; push to a new level for storing
   the parms.  */

void
begin_template_parm_list (void)
{
  /* We use a non-tag-transparent scope here, which causes pushtag to
     put tags in this scope, rather than in the enclosing class or
     namespace scope.  This is the right thing, since we want
     TEMPLATE_DECLS, and not TYPE_DECLS for template classes.  For a
     global template class, push_template_decl handles putting the
     TEMPLATE_DECL into top-level scope.  For a nested template class,
     e.g.:

       template <class T> struct S1 {
	 template <class T> struct S2 {};
       };

     pushtag contains special code to call pushdecl_with_scope on the
     TEMPLATE_DECL for S2.  */
  begin_scope (sk_template_parms, NULL);
  ++processing_template_decl;
  ++processing_template_parmlist;
  note_template_header (0);
}

/* This routine is called when a specialization is declared.  If it is
   invalid to declare a specialization here, an error is reported and
   false is returned, otherwise this routine will return true.  */

static bool
check_specialization_scope (void)
{
  tree scope = current_scope ();

  /* [temp.expl.spec]

     An explicit specialization shall be declared in the namespace of
     which the template is a member, or, for member templates, in the
     namespace of which the enclosing class or enclosing class
     template is a member.  An explicit specialization of a member
     function, member class or static data member of a class template
     shall be declared in the namespace of which the class template
     is a member.  */
  if (scope && TREE_CODE (scope) != NAMESPACE_DECL)
    {
      error ("explicit specialization in non-namespace scope %qD", scope);
      return false;
    }

  /* [temp.expl.spec]

     In an explicit specialization declaration for a member of a class
     template or a member template that appears in namespace scope,
     the member template and some of its enclosing class templates may
     remain unspecialized, except that the declaration shall not
     explicitly specialize a class member template if its enclosing
     class templates are not explicitly specialized as well.  */
  if (current_template_parms)
    {
      error ("enclosing class templates are not explicitly specialized");
      return false;
    }

  return true;
}

/* We've just seen template <>.  */

bool
begin_specialization (void)
{
  begin_scope (sk_template_spec, NULL);
  note_template_header (1);
  return check_specialization_scope ();
}

/* Called at then end of processing a declaration preceded by
   template<>.  */

void
end_specialization (void)
{
  finish_scope ();
  reset_specialization ();
}

/* Any template <>'s that we have seen thus far are not referring to a
   function specialization.  */

void
reset_specialization (void)
{
  processing_specialization = 0;
  template_header_count = 0;
}

/* We've just seen a template header.  If SPECIALIZATION is nonzero,
   it was of the form template <>.  */

static void
note_template_header (int specialization)
{
  processing_specialization = specialization;
  template_header_count++;
}

/* We're beginning an explicit instantiation.  */

void
begin_explicit_instantiation (void)
{
  gcc_assert (!processing_explicit_instantiation);
  processing_explicit_instantiation = true;
}


void
end_explicit_instantiation (void)
{
  gcc_assert (processing_explicit_instantiation);
  processing_explicit_instantiation = false;
}

/* An explicit specialization or partial specialization TMPL is being
   declared.  Check that the namespace in which the specialization is
   occurring is permissible.  Returns false iff it is invalid to
   specialize TMPL in the current namespace.  */

static bool
check_specialization_namespace (tree tmpl)
{
  tree tpl_ns = decl_namespace_context (tmpl);

  /* [tmpl.expl.spec]

     An explicit specialization shall be declared in the namespace of
     which the template is a member, or, for member templates, in the
     namespace of which the enclosing class or enclosing class
     template is a member.  An explicit specialization of a member
     function, member class or static data member of a class template
     shall be declared in the namespace of which the class template is
     a member.  */
  if (is_associated_namespace (current_namespace, tpl_ns))
    /* Same or super-using namespace.  */
    return true;
  else
    {
      pedwarn ("specialization of %qD in different namespace", tmpl);
      pedwarn ("  from definition of %q+#D", tmpl);
      return false;
    }
}

/* SPEC is an explicit instantiation.  Check that it is valid to
   perform this explicit instantiation in the current namespace.  */

static void
check_explicit_instantiation_namespace (tree spec)
{
  tree ns;

  /* DR 275: An explicit instantiation shall appear in an enclosing
     namespace of its template.  */
  ns = decl_namespace_context (spec);
  if (!is_ancestor (current_namespace, ns))
    pedwarn ("explicit instantiation of %qD in namespace %qD "
	     "(which does not enclose namespace %qD)",
	     spec, current_namespace, ns);
}

/* The TYPE is being declared.  If it is a template type, that means it
   is a partial specialization.  Do appropriate error-checking.  */

tree
maybe_process_partial_specialization (tree type)
{
  tree context;

  if (type == error_mark_node)
    return error_mark_node;

  if (TREE_CODE (type) == BOUND_TEMPLATE_TEMPLATE_PARM)
    {
      error ("name of class shadows template template parameter %qD",
	     TYPE_NAME (type));
      return error_mark_node;
    }

  context = TYPE_CONTEXT (type);

  if (CLASS_TYPE_P (type) && CLASSTYPE_USE_TEMPLATE (type))
    {
      /* This is for ordinary explicit specialization and partial
	 specialization of a template class such as:

	   template <> class C<int>;

	 or:

	   template <class T> class C<T*>;

	 Make sure that `C<int>' and `C<T*>' are implicit instantiations.  */

      if (CLASSTYPE_IMPLICIT_INSTANTIATION (type)
	  && !COMPLETE_TYPE_P (type))
	{
	  check_specialization_namespace (CLASSTYPE_TI_TEMPLATE (type));
	  SET_CLASSTYPE_TEMPLATE_SPECIALIZATION (type);
	  if (processing_template_decl)
	    push_template_decl (TYPE_MAIN_DECL (type));
	}
      else if (CLASSTYPE_TEMPLATE_INSTANTIATION (type))
	error ("specialization of %qT after instantiation", type);
    }
  else if (CLASS_TYPE_P (type)
	   && !CLASSTYPE_USE_TEMPLATE (type)
	   && CLASSTYPE_TEMPLATE_INFO (type)
	   && context && CLASS_TYPE_P (context)
	   && CLASSTYPE_TEMPLATE_INFO (context))
    {
      /* This is for an explicit specialization of member class
	 template according to [temp.expl.spec/18]:

	   template <> template <class U> class C<int>::D;

	 The context `C<int>' must be an implicit instantiation.
	 Otherwise this is just a member class template declared
	 earlier like:

	   template <> class C<int> { template <class U> class D; };
	   template <> template <class U> class C<int>::D;

	 In the first case, `C<int>::D' is a specialization of `C<T>::D'
	 while in the second case, `C<int>::D' is a primary template
	 and `C<T>::D' may not exist.  */

      if (CLASSTYPE_IMPLICIT_INSTANTIATION (context)
	  && !COMPLETE_TYPE_P (type))
	{
	  tree t;

	  if (current_namespace
	      != decl_namespace_context (CLASSTYPE_TI_TEMPLATE (type)))
	    {
	      pedwarn ("specializing %q#T in different namespace", type);
	      pedwarn ("  from definition of %q+#D",
		       CLASSTYPE_TI_TEMPLATE (type));
	    }

	  /* Check for invalid specialization after instantiation:

	       template <> template <> class C<int>::D<int>;
	       template <> template <class U> class C<int>::D;  */

	  for (t = DECL_TEMPLATE_INSTANTIATIONS
		 (most_general_template (CLASSTYPE_TI_TEMPLATE (type)));
	       t; t = TREE_CHAIN (t))
	    if (TREE_VALUE (t) != type
		&& TYPE_CONTEXT (TREE_VALUE (t)) == context)
	      error ("specialization %qT after instantiation %qT",
		     type, TREE_VALUE (t));

	  /* Mark TYPE as a specialization.  And as a result, we only
	     have one level of template argument for the innermost
	     class template.  */
	  SET_CLASSTYPE_TEMPLATE_SPECIALIZATION (type);
	  CLASSTYPE_TI_ARGS (type)
	    = INNERMOST_TEMPLATE_ARGS (CLASSTYPE_TI_ARGS (type));
	}
    }
  else if (processing_specialization)
    {
      error ("explicit specialization of non-template %qT", type);
      return error_mark_node;
    }

  return type;
}

/* Returns nonzero if we can optimize the retrieval of specializations
   for TMPL, a TEMPLATE_DECL.  In particular, for such a template, we
   do not use DECL_TEMPLATE_SPECIALIZATIONS at all.  */

static inline bool
optimize_specialization_lookup_p (tree tmpl)
{
  return (DECL_FUNCTION_TEMPLATE_P (tmpl)
	  && DECL_CLASS_SCOPE_P (tmpl)
	  /* DECL_CLASS_SCOPE_P holds of T::f even if T is a template
	     parameter.  */
	  && CLASS_TYPE_P (DECL_CONTEXT (tmpl))
	  /* The optimized lookup depends on the fact that the
	     template arguments for the member function template apply
	     purely to the containing class, which is not true if the
	     containing class is an explicit or partial
	     specialization.  */
	  && !CLASSTYPE_TEMPLATE_SPECIALIZATION (DECL_CONTEXT (tmpl))
	  && !DECL_MEMBER_TEMPLATE_P (tmpl)
	  && !DECL_CONV_FN_P (tmpl)
	  /* It is possible to have a template that is not a member
	     template and is not a member of a template class:

	     template <typename T>
	     struct S { friend A::f(); };

	     Here, the friend function is a template, but the context does
	     not have template information.  The optimized lookup relies
	     on having ARGS be the template arguments for both the class
	     and the function template.  */
	  && !DECL_FRIEND_P (DECL_TEMPLATE_RESULT (tmpl)));
}

/* Retrieve the specialization (in the sense of [temp.spec] - a
   specialization is either an instantiation or an explicit
   specialization) of TMPL for the given template ARGS.  If there is
   no such specialization, return NULL_TREE.  The ARGS are a vector of
   arguments, or a vector of vectors of arguments, in the case of
   templates with more than one level of parameters.

   If TMPL is a type template and CLASS_SPECIALIZATIONS_P is true,
   then we search for a partial specialization matching ARGS.  This
   parameter is ignored if TMPL is not a class template.  */

static tree
retrieve_specialization (tree tmpl, tree args,
			 bool class_specializations_p)
{
  if (args == error_mark_node)
    return NULL_TREE;

  gcc_assert (TREE_CODE (tmpl) == TEMPLATE_DECL);

  /* There should be as many levels of arguments as there are
     levels of parameters.  */
  gcc_assert (TMPL_ARGS_DEPTH (args)
	      == TMPL_PARMS_DEPTH (DECL_TEMPLATE_PARMS (tmpl)));

  if (optimize_specialization_lookup_p (tmpl))
    {
      tree class_template;
      tree class_specialization;
      VEC(tree,gc) *methods;
      tree fns;
      int idx;

      /* The template arguments actually apply to the containing
	 class.  Find the class specialization with those
	 arguments.  */
      class_template = CLASSTYPE_TI_TEMPLATE (DECL_CONTEXT (tmpl));
      class_specialization
	= retrieve_specialization (class_template, args,
				   /*class_specializations_p=*/false);
      if (!class_specialization)
	return NULL_TREE;
      /* Now, find the appropriate entry in the CLASSTYPE_METHOD_VEC
	 for the specialization.  */
      idx = class_method_index_for_fn (class_specialization, tmpl);
      if (idx == -1)
	return NULL_TREE;
      /* Iterate through the methods with the indicated name, looking
	 for the one that has an instance of TMPL.  */
      methods = CLASSTYPE_METHOD_VEC (class_specialization);
      for (fns = VEC_index (tree, methods, idx); fns; fns = OVL_NEXT (fns))
	{
	  tree fn = OVL_CURRENT (fns);
	  if (DECL_TEMPLATE_INFO (fn) && DECL_TI_TEMPLATE (fn) == tmpl)
	    return fn;
	}
      return NULL_TREE;
    }
  else
    {
      tree *sp;
      tree *head;

      /* Class templates store their instantiations on the
	 DECL_TEMPLATE_INSTANTIATIONS list; other templates use the
	 DECL_TEMPLATE_SPECIALIZATIONS list.  */
      if (!class_specializations_p
	  && TREE_CODE (DECL_TEMPLATE_RESULT (tmpl)) == TYPE_DECL)
	sp = &DECL_TEMPLATE_INSTANTIATIONS (tmpl);
      else
	sp = &DECL_TEMPLATE_SPECIALIZATIONS (tmpl);
      head = sp;
      /* Iterate through the list until we find a matching template.  */
      while (*sp != NULL_TREE)
	{
	  tree spec = *sp;

	  if (comp_template_args (TREE_PURPOSE (spec), args))
	    {
	      /* Use the move-to-front heuristic to speed up future
		 searches.  */
	      if (spec != *head)
		{
		  *sp = TREE_CHAIN (*sp);
		  TREE_CHAIN (spec) = *head;
		  *head = spec;
		}
	      return TREE_VALUE (spec);
	    }
	  sp = &TREE_CHAIN (spec);
	}
    }

  return NULL_TREE;
}

/* Like retrieve_specialization, but for local declarations.  */

static tree
retrieve_local_specialization (tree tmpl)
{
  tree spec = (tree) htab_find_with_hash (local_specializations, tmpl,
					  htab_hash_pointer (tmpl));
  return spec ? TREE_PURPOSE (spec) : NULL_TREE;
}

/* Returns nonzero iff DECL is a specialization of TMPL.  */

int
is_specialization_of (tree decl, tree tmpl)
{
  tree t;

  if (TREE_CODE (decl) == FUNCTION_DECL)
    {
      for (t = decl;
	   t != NULL_TREE;
	   t = DECL_TEMPLATE_INFO (t) ? DECL_TI_TEMPLATE (t) : NULL_TREE)
	if (t == tmpl)
	  return 1;
    }
  else
    {
      gcc_assert (TREE_CODE (decl) == TYPE_DECL);

      for (t = TREE_TYPE (decl);
	   t != NULL_TREE;
	   t = CLASSTYPE_USE_TEMPLATE (t)
	     ? TREE_TYPE (CLASSTYPE_TI_TEMPLATE (t)) : NULL_TREE)
	if (same_type_ignoring_top_level_qualifiers_p (t, TREE_TYPE (tmpl)))
	  return 1;
    }

  return 0;
}

/* Returns nonzero iff DECL is a specialization of friend declaration
   FRIEND according to [temp.friend].  */

bool
is_specialization_of_friend (tree decl, tree friend)
{
  bool need_template = true;
  int template_depth;

  gcc_assert (TREE_CODE (decl) == FUNCTION_DECL
	      || TREE_CODE (decl) == TYPE_DECL);

  /* For [temp.friend/6] when FRIEND is an ordinary member function
     of a template class, we want to check if DECL is a specialization
     if this.  */
  if (TREE_CODE (friend) == FUNCTION_DECL
      && DECL_TEMPLATE_INFO (friend)
      && !DECL_USE_TEMPLATE (friend))
    {
      /* We want a TEMPLATE_DECL for `is_specialization_of'.  */
      friend = DECL_TI_TEMPLATE (friend);
      need_template = false;
    }
  else if (TREE_CODE (friend) == TEMPLATE_DECL
	   && !PRIMARY_TEMPLATE_P (friend))
    need_template = false;

  /* There is nothing to do if this is not a template friend.  */
  if (TREE_CODE (friend) != TEMPLATE_DECL)
    return false;

  if (is_specialization_of (decl, friend))
    return true;

  /* [temp.friend/6]
     A member of a class template may be declared to be a friend of a
     non-template class.  In this case, the corresponding member of
     every specialization of the class template is a friend of the
     class granting friendship.

     For example, given a template friend declaration

       template <class T> friend void A<T>::f();

     the member function below is considered a friend

       template <> struct A<int> {
	 void f();
       };

     For this type of template friend, TEMPLATE_DEPTH below will be
     nonzero.  To determine if DECL is a friend of FRIEND, we first
     check if the enclosing class is a specialization of another.  */

  template_depth = template_class_depth (DECL_CONTEXT (friend));
  if (template_depth
      && DECL_CLASS_SCOPE_P (decl)
      && is_specialization_of (TYPE_NAME (DECL_CONTEXT (decl)),
			       CLASSTYPE_TI_TEMPLATE (DECL_CONTEXT (friend))))
    {
      /* Next, we check the members themselves.  In order to handle
	 a few tricky cases, such as when FRIEND's are

	   template <class T> friend void A<T>::g(T t);
	   template <class T> template <T t> friend void A<T>::h();

	 and DECL's are

	   void A<int>::g(int);
	   template <int> void A<int>::h();

	 we need to figure out ARGS, the template arguments from
	 the context of DECL.  This is required for template substitution
	 of `T' in the function parameter of `g' and template parameter
	 of `h' in the above examples.  Here ARGS corresponds to `int'.  */

      tree context = DECL_CONTEXT (decl);
      tree args = NULL_TREE;
      int current_depth = 0;

      while (current_depth < template_depth)
	{
	  if (CLASSTYPE_TEMPLATE_INFO (context))
	    {
	      if (current_depth == 0)
		args = TYPE_TI_ARGS (context);
	      else
		args = add_to_template_args (TYPE_TI_ARGS (context), args);
	      current_depth++;
	    }
	  context = TYPE_CONTEXT (context);
	}

      if (TREE_CODE (decl) == FUNCTION_DECL)
	{
	  bool is_template;
	  tree friend_type;
	  tree decl_type;
	  tree friend_args_type;
	  tree decl_args_type;

	  /* Make sure that both DECL and FRIEND are templates or
	     non-templates.  */
	  is_template = DECL_TEMPLATE_INFO (decl)
			&& PRIMARY_TEMPLATE_P (DECL_TI_TEMPLATE (decl));
	  if (need_template ^ is_template)
	    return false;
	  else if (is_template)
	    {
	      /* If both are templates, check template parameter list.  */
	      tree friend_parms
		= tsubst_template_parms (DECL_TEMPLATE_PARMS (friend),
					 args, tf_none);
	      if (!comp_template_parms
		     (DECL_TEMPLATE_PARMS (DECL_TI_TEMPLATE (decl)),
		      friend_parms))
		return false;

	      decl_type = TREE_TYPE (DECL_TI_TEMPLATE (decl));
	    }
	  else
	    decl_type = TREE_TYPE (decl);

	  friend_type = tsubst_function_type (TREE_TYPE (friend), args,
					      tf_none, NULL_TREE);
	  if (friend_type == error_mark_node)
	    return false;

	  /* Check if return types match.  */
	  if (!same_type_p (TREE_TYPE (decl_type), TREE_TYPE (friend_type)))
	    return false;

	  /* Check if function parameter types match, ignoring the
	     `this' parameter.  */
	  friend_args_type = TYPE_ARG_TYPES (friend_type);
	  decl_args_type = TYPE_ARG_TYPES (decl_type);
	  if (DECL_NONSTATIC_MEMBER_FUNCTION_P (friend))
	    friend_args_type = TREE_CHAIN (friend_args_type);
	  if (DECL_NONSTATIC_MEMBER_FUNCTION_P (decl))
	    decl_args_type = TREE_CHAIN (decl_args_type);

	  return compparms (decl_args_type, friend_args_type);
	}
      else
	{
	  /* DECL is a TYPE_DECL */
	  bool is_template;
	  tree decl_type = TREE_TYPE (decl);

	  /* Make sure that both DECL and FRIEND are templates or
	     non-templates.  */
	  is_template
	    = CLASSTYPE_TEMPLATE_INFO (decl_type)
	      && PRIMARY_TEMPLATE_P (CLASSTYPE_TI_TEMPLATE (decl_type));

	  if (need_template ^ is_template)
	    return false;
	  else if (is_template)
	    {
	      tree friend_parms;
	      /* If both are templates, check the name of the two
		 TEMPLATE_DECL's first because is_friend didn't.  */
	      if (DECL_NAME (CLASSTYPE_TI_TEMPLATE (decl_type))
		  != DECL_NAME (friend))
		return false;

	      /* Now check template parameter list.  */
	      friend_parms
		= tsubst_template_parms (DECL_TEMPLATE_PARMS (friend),
					 args, tf_none);
	      return comp_template_parms
		(DECL_TEMPLATE_PARMS (CLASSTYPE_TI_TEMPLATE (decl_type)),
		 friend_parms);
	    }
	  else
	    return (DECL_NAME (decl)
		    == DECL_NAME (friend));
	}
    }
  return false;
}

/* Register the specialization SPEC as a specialization of TMPL with
   the indicated ARGS.  IS_FRIEND indicates whether the specialization
   is actually just a friend declaration.  Returns SPEC, or an
   equivalent prior declaration, if available.  */

static tree
register_specialization (tree spec, tree tmpl, tree args, bool is_friend)
{
  tree fn;

  gcc_assert (TREE_CODE (tmpl) == TEMPLATE_DECL);

  if (TREE_CODE (spec) == FUNCTION_DECL
      && uses_template_parms (DECL_TI_ARGS (spec)))
    /* This is the FUNCTION_DECL for a partial instantiation.  Don't
       register it; we want the corresponding TEMPLATE_DECL instead.
       We use `uses_template_parms (DECL_TI_ARGS (spec))' rather than
       the more obvious `uses_template_parms (spec)' to avoid problems
       with default function arguments.  In particular, given
       something like this:

	  template <class T> void f(T t1, T t = T())

       the default argument expression is not substituted for in an
       instantiation unless and until it is actually needed.  */
    return spec;

  fn = retrieve_specialization (tmpl, args,
				/*class_specializations_p=*/false);
  /* We can sometimes try to re-register a specialization that we've
     already got.  In particular, regenerate_decl_from_template calls
     duplicate_decls which will update the specialization list.  But,
     we'll still get called again here anyhow.  It's more convenient
     to simply allow this than to try to prevent it.  */
  if (fn == spec)
    return spec;
  else if (fn && DECL_TEMPLATE_SPECIALIZATION (spec))
    {
      if (DECL_TEMPLATE_INSTANTIATION (fn))
	{
	  if (TREE_USED (fn)
	      || DECL_EXPLICIT_INSTANTIATION (fn))
	    {
	      error ("specialization of %qD after instantiation",
		     fn);
	      return error_mark_node;
	    }
	  else
	    {
	      tree clone;
	      /* This situation should occur only if the first
		 specialization is an implicit instantiation, the
		 second is an explicit specialization, and the
		 implicit instantiation has not yet been used.  That
		 situation can occur if we have implicitly
		 instantiated a member function and then specialized
		 it later.

		 We can also wind up here if a friend declaration that
		 looked like an instantiation turns out to be a
		 specialization:

		   template <class T> void foo(T);
		   class S { friend void foo<>(int) };
		   template <> void foo(int);

		 We transform the existing DECL in place so that any
		 pointers to it become pointers to the updated
		 declaration.

		 If there was a definition for the template, but not
		 for the specialization, we want this to look as if
		 there were no definition, and vice versa.  */
	      DECL_INITIAL (fn) = NULL_TREE;
	      duplicate_decls (spec, fn, is_friend);
	      /* The call to duplicate_decls will have applied
		 [temp.expl.spec]:

		   An explicit specialization of a function template
		   is inline only if it is explicitly declared to be,
		   and independently of whether its function template
		   is.

		to the primary function; now copy the inline bits to
		the various clones.  */
	      FOR_EACH_CLONE (clone, fn)
		{
		  DECL_DECLARED_INLINE_P (clone)
		    = DECL_DECLARED_INLINE_P (fn);
		  DECL_INLINE (clone)
		    = DECL_INLINE (fn);
		}
	      check_specialization_namespace (fn);

	      return fn;
	    }
	}
      else if (DECL_TEMPLATE_SPECIALIZATION (fn))
	{
	  if (!duplicate_decls (spec, fn, is_friend) && DECL_INITIAL (spec))
	    /* Dup decl failed, but this is a new definition. Set the
	       line number so any errors match this new
	       definition.  */
	    DECL_SOURCE_LOCATION (fn) = DECL_SOURCE_LOCATION (spec);

	  return fn;
	}
    }

  /* A specialization must be declared in the same namespace as the
     template it is specializing.  */
  if (DECL_TEMPLATE_SPECIALIZATION (spec)
      && !check_specialization_namespace (tmpl))
    DECL_CONTEXT (spec) = FROB_CONTEXT (decl_namespace_context (tmpl));

  if (!optimize_specialization_lookup_p (tmpl))
    DECL_TEMPLATE_SPECIALIZATIONS (tmpl)
      = tree_cons (args, spec, DECL_TEMPLATE_SPECIALIZATIONS (tmpl));

  return spec;
}

/* Unregister the specialization SPEC as a specialization of TMPL.
   Replace it with NEW_SPEC, if NEW_SPEC is non-NULL.  Returns true
   if the SPEC was listed as a specialization of TMPL.  */

bool
reregister_specialization (tree spec, tree tmpl, tree new_spec)
{
  tree* s;

  for (s = &DECL_TEMPLATE_SPECIALIZATIONS (tmpl);
       *s != NULL_TREE;
       s = &TREE_CHAIN (*s))
    if (TREE_VALUE (*s) == spec)
      {
	if (!new_spec)
	  *s = TREE_CHAIN (*s);
	else
	  TREE_VALUE (*s) = new_spec;
	return 1;
      }

  return 0;
}

/* Compare an entry in the local specializations hash table P1 (which
   is really a pointer to a TREE_LIST) with P2 (which is really a
   DECL).  */

static int
eq_local_specializations (const void *p1, const void *p2)
{
  return TREE_VALUE ((tree) p1) == (tree) p2;
}

/* Hash P1, an entry in the local specializations table.  */

static hashval_t
hash_local_specialization (const void* p1)
{
  return htab_hash_pointer (TREE_VALUE ((tree) p1));
}

/* Like register_specialization, but for local declarations.  We are
   registering SPEC, an instantiation of TMPL.  */

static void
register_local_specialization (tree spec, tree tmpl)
{
  void **slot;

  slot = htab_find_slot_with_hash (local_specializations, tmpl,
				   htab_hash_pointer (tmpl), INSERT);
  *slot = build_tree_list (spec, tmpl);
}

/* TYPE is a class type.  Returns true if TYPE is an explicitly
   specialized class.  */

bool
explicit_class_specialization_p (tree type)
{
  if (!CLASSTYPE_TEMPLATE_SPECIALIZATION (type))
    return false;
  return !uses_template_parms (CLASSTYPE_TI_ARGS (type));
}

/* Print the list of candidate FNS in an error message.  */

void
print_candidates (tree fns)
{
  tree fn;

  const char *str = "candidates are:";

  for (fn = fns; fn != NULL_TREE; fn = TREE_CHAIN (fn))
    {
      tree f;

      for (f = TREE_VALUE (fn); f; f = OVL_NEXT (f))
	error ("%s %+#D", str, OVL_CURRENT (f));
      str = "               ";
    }
}

/* Returns the template (one of the functions given by TEMPLATE_ID)
   which can be specialized to match the indicated DECL with the
   explicit template args given in TEMPLATE_ID.  The DECL may be
   NULL_TREE if none is available.  In that case, the functions in
   TEMPLATE_ID are non-members.

   If NEED_MEMBER_TEMPLATE is nonzero the function is known to be a
   specialization of a member template.

   The TEMPLATE_COUNT is the number of references to qualifying
   template classes that appeared in the name of the function. See
   check_explicit_specialization for a more accurate description.

   TSK indicates what kind of template declaration (if any) is being
   declared.  TSK_TEMPLATE indicates that the declaration given by
   DECL, though a FUNCTION_DECL, has template parameters, and is
   therefore a template function.

   The template args (those explicitly specified and those deduced)
   are output in a newly created vector *TARGS_OUT.

   If it is impossible to determine the result, an error message is
   issued.  The error_mark_node is returned to indicate failure.  */

static tree
determine_specialization (tree template_id,
			  tree decl,
			  tree* targs_out,
			  int need_member_template,
			  int template_count,
			  tmpl_spec_kind tsk)
{
  tree fns;
  tree targs;
  tree explicit_targs;
  tree candidates = NULL_TREE;
  /* A TREE_LIST of templates of which DECL may be a specialization.
     The TREE_VALUE of each node is a TEMPLATE_DECL.  The
     corresponding TREE_PURPOSE is the set of template arguments that,
     when used to instantiate the template, would produce a function
     with the signature of DECL.  */
  tree templates = NULL_TREE;
  int header_count;
  struct cp_binding_level *b;

  *targs_out = NULL_TREE;

  if (template_id == error_mark_node || decl == error_mark_node)
    return error_mark_node;

  fns = TREE_OPERAND (template_id, 0);
  explicit_targs = TREE_OPERAND (template_id, 1);

  if (fns == error_mark_node)
    return error_mark_node;

  /* Check for baselinks.  */
  if (BASELINK_P (fns))
    fns = BASELINK_FUNCTIONS (fns);

  if (!is_overloaded_fn (fns))
    {
      error ("%qD is not a function template", fns);
      return error_mark_node;
    }

  /* Count the number of template headers specified for this
     specialization.  */
  header_count = 0;
  for (b = current_binding_level;
       b->kind == sk_template_parms;
       b = b->level_chain)
    ++header_count;

  for (; fns; fns = OVL_NEXT (fns))
    {
      tree fn = OVL_CURRENT (fns);

      if (TREE_CODE (fn) == TEMPLATE_DECL)
	{
	  tree decl_arg_types;
	  tree fn_arg_types;

	  /* In case of explicit specialization, we need to check if
	     the number of template headers appearing in the specialization
	     is correct. This is usually done in check_explicit_specialization,
	     but the check done there cannot be exhaustive when specializing
	     member functions. Consider the following code:

	     template <> void A<int>::f(int);
	     template <> template <> void A<int>::f(int);

	     Assuming that A<int> is not itself an explicit specialization
	     already, the first line specializes "f" which is a non-template
	     member function, whilst the second line specializes "f" which
	     is a template member function. So both lines are syntactically
	     correct, and check_explicit_specialization does not reject
	     them.

	     Here, we can do better, as we are matching the specialization
	     against the declarations. We count the number of template
	     headers, and we check if they match TEMPLATE_COUNT + 1
	     (TEMPLATE_COUNT is the number of qualifying template classes,
	     plus there must be another header for the member template
	     itself).

	     Notice that if header_count is zero, this is not a
	     specialization but rather a template instantiation, so there
	     is no check we can perform here.  */
	  if (header_count && header_count != template_count + 1)
	    continue;

	  /* Check that the number of template arguments at the
	     innermost level for DECL is the same as for FN.  */
	  if (current_binding_level->kind == sk_template_parms
	      && !current_binding_level->explicit_spec_p
	      && (TREE_VEC_LENGTH (DECL_INNERMOST_TEMPLATE_PARMS (fn))
		  != TREE_VEC_LENGTH (INNERMOST_TEMPLATE_PARMS
				      (current_template_parms))))
	    continue;

	  /* DECL might be a specialization of FN.  */
	  decl_arg_types = TYPE_ARG_TYPES (TREE_TYPE (decl));
	  fn_arg_types = TYPE_ARG_TYPES (TREE_TYPE (fn));

	  /* For a non-static member function, we need to make sure
	     that the const qualification is the same.  Since
	     get_bindings does not try to merge the "this" parameter,
	     we must do the comparison explicitly.  */
	  if (DECL_NONSTATIC_MEMBER_FUNCTION_P (fn)
	      && !same_type_p (TREE_VALUE (fn_arg_types),
			       TREE_VALUE (decl_arg_types)))
	    continue;

	  /* Skip the "this" parameter and, for constructors of
	     classes with virtual bases, the VTT parameter.  A
	     full specialization of a constructor will have a VTT
	     parameter, but a template never will.  */ 
	  decl_arg_types 
	    = skip_artificial_parms_for (decl, decl_arg_types);
	  fn_arg_types 
	    = skip_artificial_parms_for (fn, fn_arg_types);

	  /* Check that the number of function parameters matches.
	     For example,
	       template <class T> void f(int i = 0);
	       template <> void f<int>();
	     The specialization f<int> is invalid but is not caught
	     by get_bindings below.  */
	  if (list_length (fn_arg_types) != list_length (decl_arg_types))
	    continue;

	  /* Function templates cannot be specializations; there are
	     no partial specializations of functions.  Therefore, if
	     the type of DECL does not match FN, there is no
	     match.  */
	  if (tsk == tsk_template)
	    {
	      if (compparms (fn_arg_types, decl_arg_types))
		candidates = tree_cons (NULL_TREE, fn, candidates);
	      continue;
	    }

	  /* See whether this function might be a specialization of this
	     template.  */
	  targs = get_bindings (fn, decl, explicit_targs, /*check_ret=*/true);

	  if (!targs)
	    /* We cannot deduce template arguments that when used to
	       specialize TMPL will produce DECL.  */
	    continue;

	  /* Save this template, and the arguments deduced.  */
	  templates = tree_cons (targs, fn, templates);
	}
      else if (need_member_template)
	/* FN is an ordinary member function, and we need a
	   specialization of a member template.  */
	;
      else if (TREE_CODE (fn) != FUNCTION_DECL)
	/* We can get IDENTIFIER_NODEs here in certain erroneous
	   cases.  */
	;
      else if (!DECL_FUNCTION_MEMBER_P (fn))
	/* This is just an ordinary non-member function.  Nothing can
	   be a specialization of that.  */
	;
      else if (DECL_ARTIFICIAL (fn))
	/* Cannot specialize functions that are created implicitly.  */
	;
      else
	{
	  tree decl_arg_types;

	  /* This is an ordinary member function.  However, since
	     we're here, we can assume it's enclosing class is a
	     template class.  For example,

	       template <typename T> struct S { void f(); };
	       template <> void S<int>::f() {}

	     Here, S<int>::f is a non-template, but S<int> is a
	     template class.  If FN has the same type as DECL, we
	     might be in business.  */

	  if (!DECL_TEMPLATE_INFO (fn))
	    /* Its enclosing class is an explicit specialization
	       of a template class.  This is not a candidate.  */
	    continue;

	  if (!same_type_p (TREE_TYPE (TREE_TYPE (decl)),
			    TREE_TYPE (TREE_TYPE (fn))))
	    /* The return types differ.  */
	    continue;

	  /* Adjust the type of DECL in case FN is a static member.  */
	  decl_arg_types = TYPE_ARG_TYPES (TREE_TYPE (decl));
	  if (DECL_STATIC_FUNCTION_P (fn)
	      && DECL_NONSTATIC_MEMBER_FUNCTION_P (decl))
	    decl_arg_types = TREE_CHAIN (decl_arg_types);

	  if (compparms (TYPE_ARG_TYPES (TREE_TYPE (fn)),
			 decl_arg_types))
	    /* They match!  */
	    candidates = tree_cons (NULL_TREE, fn, candidates);
	}
    }

  if (templates && TREE_CHAIN (templates))
    {
      /* We have:

	   [temp.expl.spec]

	   It is possible for a specialization with a given function
	   signature to be instantiated from more than one function
	   template.  In such cases, explicit specification of the
	   template arguments must be used to uniquely identify the
	   function template specialization being specialized.

	 Note that here, there's no suggestion that we're supposed to
	 determine which of the candidate templates is most
	 specialized.  However, we, also have:

	   [temp.func.order]

	   Partial ordering of overloaded function template
	   declarations is used in the following contexts to select
	   the function template to which a function template
	   specialization refers:

	   -- when an explicit specialization refers to a function
	      template.

	 So, we do use the partial ordering rules, at least for now.
	 This extension can only serve to make invalid programs valid,
	 so it's safe.  And, there is strong anecdotal evidence that
	 the committee intended the partial ordering rules to apply;
	 the EDG front-end has that behavior, and John Spicer claims
	 that the committee simply forgot to delete the wording in
	 [temp.expl.spec].  */
      tree tmpl = most_specialized_instantiation (templates);
      if (tmpl != error_mark_node)
	{
	  templates = tmpl;
	  TREE_CHAIN (templates) = NULL_TREE;
	}
    }

  if (templates == NULL_TREE && candidates == NULL_TREE)
    {
      error ("template-id %qD for %q+D does not match any template "
	     "declaration", template_id, decl);
      return error_mark_node;
    }
  else if ((templates && TREE_CHAIN (templates))
	   || (candidates && TREE_CHAIN (candidates))
	   || (templates && candidates))
    {
      error ("ambiguous template specialization %qD for %q+D",
	     template_id, decl);
      chainon (candidates, templates);
      print_candidates (candidates);
      return error_mark_node;
    }

  /* We have one, and exactly one, match.  */
  if (candidates)
    {
      tree fn = TREE_VALUE (candidates);
      /* DECL is a re-declaration of a template function.  */
      if (TREE_CODE (fn) == TEMPLATE_DECL)
	return fn;
      /* It was a specialization of an ordinary member function in a
	 template class.  */
      *targs_out = copy_node (DECL_TI_ARGS (fn));
      return DECL_TI_TEMPLATE (fn);
    }

  /* It was a specialization of a template.  */
  targs = DECL_TI_ARGS (DECL_TEMPLATE_RESULT (TREE_VALUE (templates)));
  if (TMPL_ARGS_HAVE_MULTIPLE_LEVELS (targs))
    {
      *targs_out = copy_node (targs);
      SET_TMPL_ARGS_LEVEL (*targs_out,
			   TMPL_ARGS_DEPTH (*targs_out),
			   TREE_PURPOSE (templates));
    }
  else
    *targs_out = TREE_PURPOSE (templates);
  return TREE_VALUE (templates);
}

/* Returns a chain of parameter types, exactly like the SPEC_TYPES,
   but with the default argument values filled in from those in the
   TMPL_TYPES.  */

static tree
copy_default_args_to_explicit_spec_1 (tree spec_types,
				      tree tmpl_types)
{
  tree new_spec_types;

  if (!spec_types)
    return NULL_TREE;

  if (spec_types == void_list_node)
    return void_list_node;

  /* Substitute into the rest of the list.  */
  new_spec_types =
    copy_default_args_to_explicit_spec_1 (TREE_CHAIN (spec_types),
					  TREE_CHAIN (tmpl_types));

  /* Add the default argument for this parameter.  */
  return hash_tree_cons (TREE_PURPOSE (tmpl_types),
			 TREE_VALUE (spec_types),
			 new_spec_types);
}

/* DECL is an explicit specialization.  Replicate default arguments
   from the template it specializes.  (That way, code like:

     template <class T> void f(T = 3);
     template <> void f(double);
     void g () { f (); }

   works, as required.)  An alternative approach would be to look up
   the correct default arguments at the call-site, but this approach
   is consistent with how implicit instantiations are handled.  */

static void
copy_default_args_to_explicit_spec (tree decl)
{
  tree tmpl;
  tree spec_types;
  tree tmpl_types;
  tree new_spec_types;
  tree old_type;
  tree new_type;
  tree t;
  tree object_type = NULL_TREE;
  tree in_charge = NULL_TREE;
  tree vtt = NULL_TREE;

  /* See if there's anything we need to do.  */
  tmpl = DECL_TI_TEMPLATE (decl);
  tmpl_types = TYPE_ARG_TYPES (TREE_TYPE (DECL_TEMPLATE_RESULT (tmpl)));
  for (t = tmpl_types; t; t = TREE_CHAIN (t))
    if (TREE_PURPOSE (t))
      break;
  if (!t)
    return;

  old_type = TREE_TYPE (decl);
  spec_types = TYPE_ARG_TYPES (old_type);

  if (DECL_NONSTATIC_MEMBER_FUNCTION_P (decl))
    {
      /* Remove the this pointer, but remember the object's type for
	 CV quals.  */
      object_type = TREE_TYPE (TREE_VALUE (spec_types));
      spec_types = TREE_CHAIN (spec_types);
      tmpl_types = TREE_CHAIN (tmpl_types);

      if (DECL_HAS_IN_CHARGE_PARM_P (decl))
	{
	  /* DECL may contain more parameters than TMPL due to the extra
	     in-charge parameter in constructors and destructors.  */
	  in_charge = spec_types;
	  spec_types = TREE_CHAIN (spec_types);
	}
      if (DECL_HAS_VTT_PARM_P (decl))
	{
	  vtt = spec_types;
	  spec_types = TREE_CHAIN (spec_types);
	}
    }

  /* Compute the merged default arguments.  */
  new_spec_types =
    copy_default_args_to_explicit_spec_1 (spec_types, tmpl_types);

  /* Compute the new FUNCTION_TYPE.  */
  if (object_type)
    {
      if (vtt)
	new_spec_types = hash_tree_cons (TREE_PURPOSE (vtt),
					 TREE_VALUE (vtt),
					 new_spec_types);

      if (in_charge)
	/* Put the in-charge parameter back.  */
	new_spec_types = hash_tree_cons (TREE_PURPOSE (in_charge),
					 TREE_VALUE (in_charge),
					 new_spec_types);

      new_type = build_method_type_directly (object_type,
					     TREE_TYPE (old_type),
					     new_spec_types);
    }
  else
    new_type = build_function_type (TREE_TYPE (old_type),
				    new_spec_types);
  new_type = cp_build_type_attribute_variant (new_type,
					      TYPE_ATTRIBUTES (old_type));
  new_type = build_exception_variant (new_type,
				      TYPE_RAISES_EXCEPTIONS (old_type));
  TREE_TYPE (decl) = new_type;
}

/* Check to see if the function just declared, as indicated in
   DECLARATOR, and in DECL, is a specialization of a function
   template.  We may also discover that the declaration is an explicit
   instantiation at this point.

   Returns DECL, or an equivalent declaration that should be used
   instead if all goes well.  Issues an error message if something is
   amiss.  Returns error_mark_node if the error is not easily
   recoverable.

   FLAGS is a bitmask consisting of the following flags:

   2: The function has a definition.
   4: The function is a friend.

   The TEMPLATE_COUNT is the number of references to qualifying
   template classes that appeared in the name of the function.  For
   example, in

     template <class T> struct S { void f(); };
     void S<int>::f();

   the TEMPLATE_COUNT would be 1.  However, explicitly specialized
   classes are not counted in the TEMPLATE_COUNT, so that in

     template <class T> struct S {};
     template <> struct S<int> { void f(); }
     template <> void S<int>::f();

   the TEMPLATE_COUNT would be 0.  (Note that this declaration is
   invalid; there should be no template <>.)

   If the function is a specialization, it is marked as such via
   DECL_TEMPLATE_SPECIALIZATION.  Furthermore, its DECL_TEMPLATE_INFO
   is set up correctly, and it is added to the list of specializations
   for that template.  */

tree
check_explicit_specialization (tree declarator,
			       tree decl,
			       int template_count,
			       int flags)
{
  int have_def = flags & 2;
  int is_friend = flags & 4;
  int specialization = 0;
  int explicit_instantiation = 0;
  int member_specialization = 0;
  tree ctype = DECL_CLASS_CONTEXT (decl);
  tree dname = DECL_NAME (decl);
  tmpl_spec_kind tsk;

  if (is_friend)
    {
      if (!processing_specialization)
	tsk = tsk_none;
      else
	tsk = tsk_excessive_parms;
    }
  else
    tsk = current_tmpl_spec_kind (template_count);

  switch (tsk)
    {
    case tsk_none:
      if (processing_specialization)
	{
	  specialization = 1;
	  SET_DECL_TEMPLATE_SPECIALIZATION (decl);
	}
      else if (TREE_CODE (declarator) == TEMPLATE_ID_EXPR)
	{
	  if (is_friend)
	    /* This could be something like:

	       template <class T> void f(T);
	       class S { friend void f<>(int); }  */
	    specialization = 1;
	  else
	    {
	      /* This case handles bogus declarations like template <>
		 template <class T> void f<int>(); */

	      error ("template-id %qD in declaration of primary template",
		     declarator);
	      return decl;
	    }
	}
      break;

    case tsk_invalid_member_spec:
      /* The error has already been reported in
	 check_specialization_scope.  */
      return error_mark_node;

    case tsk_invalid_expl_inst:
      error ("template parameter list used in explicit instantiation");

      /* Fall through.  */

    case tsk_expl_inst:
      if (have_def)
	error ("definition provided for explicit instantiation");

      explicit_instantiation = 1;
      break;

    case tsk_excessive_parms:
    case tsk_insufficient_parms:
      if (tsk == tsk_excessive_parms)
	error ("too many template parameter lists in declaration of %qD",
	       decl);
      else if (template_header_count)
	error("too few template parameter lists in declaration of %qD", decl);
      else
	error("explicit specialization of %qD must be introduced by "
	      "%<template <>%>", decl);

      /* Fall through.  */
    case tsk_expl_spec:
      SET_DECL_TEMPLATE_SPECIALIZATION (decl);
      if (ctype)
	member_specialization = 1;
      else
	specialization = 1;
      break;

    case tsk_template:
      if (TREE_CODE (declarator) == TEMPLATE_ID_EXPR)
	{
	  /* This case handles bogus declarations like template <>
	     template <class T> void f<int>(); */

	  if (uses_template_parms (declarator))
	    error ("function template partial specialization %qD "
		   "is not allowed", declarator);
	  else
	    error ("template-id %qD in declaration of primary template",
		   declarator);
	  return decl;
	}

      if (ctype && CLASSTYPE_TEMPLATE_INSTANTIATION (ctype))
	/* This is a specialization of a member template, without
	   specialization the containing class.  Something like:

	     template <class T> struct S {
	       template <class U> void f (U);
	     };
	     template <> template <class U> void S<int>::f(U) {}

	   That's a specialization -- but of the entire template.  */
	specialization = 1;
      break;

    default:
      gcc_unreachable ();
    }

  if (specialization || member_specialization)
    {
      tree t = TYPE_ARG_TYPES (TREE_TYPE (decl));
      for (; t; t = TREE_CHAIN (t))
	if (TREE_PURPOSE (t))
	  {
	    pedwarn
	      ("default argument specified in explicit specialization");
	    break;
	  }
    }

  if (specialization || member_specialization || explicit_instantiation)
    {
      tree tmpl = NULL_TREE;
      tree targs = NULL_TREE;

      /* Make sure that the declarator is a TEMPLATE_ID_EXPR.  */
      if (TREE_CODE (declarator) != TEMPLATE_ID_EXPR)
	{
	  tree fns;

	  gcc_assert (TREE_CODE (declarator) == IDENTIFIER_NODE);
	  if (ctype)
	    fns = dname;
	  else
	    {
	      /* If there is no class context, the explicit instantiation
		 must be at namespace scope.  */
	      gcc_assert (DECL_NAMESPACE_SCOPE_P (decl));

	      /* Find the namespace binding, using the declaration
		 context.  */
	      fns = lookup_qualified_name (CP_DECL_CONTEXT (decl), dname,
					   false, true);
	      if (fns == error_mark_node || !is_overloaded_fn (fns))
		{
		  error ("%qD is not a template function", dname);
		  fns = error_mark_node;
		}
	      else
		{
		  tree fn = OVL_CURRENT (fns);
		  if (!is_associated_namespace (CP_DECL_CONTEXT (decl),
						CP_DECL_CONTEXT (fn)))
		    error ("%qD is not declared in %qD",
			   decl, current_namespace);
		}
	    }

	  declarator = lookup_template_function (fns, NULL_TREE);
	}

      if (declarator == error_mark_node)
	return error_mark_node;

      if (ctype != NULL_TREE && TYPE_BEING_DEFINED (ctype))
	{
	  if (!explicit_instantiation)
	    /* A specialization in class scope.  This is invalid,
	       but the error will already have been flagged by
	       check_specialization_scope.  */
	    return error_mark_node;
	  else
	    {
	      /* It's not valid to write an explicit instantiation in
		 class scope, e.g.:

		   class C { template void f(); }

		   This case is caught by the parser.  However, on
		   something like:

		   template class C { void f(); };

		   (which is invalid) we can get here.  The error will be
		   issued later.  */
	      ;
	    }

	  return decl;
	}
      else if (ctype != NULL_TREE
	       && (TREE_CODE (TREE_OPERAND (declarator, 0)) ==
		   IDENTIFIER_NODE))
	{
	  /* Find the list of functions in ctype that have the same
	     name as the declared function.  */
	  tree name = TREE_OPERAND (declarator, 0);
	  tree fns = NULL_TREE;
	  int idx;

	  if (constructor_name_p (name, ctype))
	    {
	      int is_constructor = DECL_CONSTRUCTOR_P (decl);

	      if (is_constructor ? !TYPE_HAS_CONSTRUCTOR (ctype)
		  : !CLASSTYPE_DESTRUCTORS (ctype))
		{
		  /* From [temp.expl.spec]:

		     If such an explicit specialization for the member
		     of a class template names an implicitly-declared
		     special member function (clause _special_), the
		     program is ill-formed.

		     Similar language is found in [temp.explicit].  */
		  error ("specialization of implicitly-declared special member function");
		  return error_mark_node;
		}

	      name = is_constructor ? ctor_identifier : dtor_identifier;
	    }

	  if (!DECL_CONV_FN_P (decl))
	    {
	      idx = lookup_fnfields_1 (ctype, name);
	      if (idx >= 0)
		fns = VEC_index (tree, CLASSTYPE_METHOD_VEC (ctype), idx);
	    }
	  else
	    {
	      VEC(tree,gc) *methods;
	      tree ovl;

	      /* For a type-conversion operator, we cannot do a
		 name-based lookup.  We might be looking for `operator
		 int' which will be a specialization of `operator T'.
		 So, we find *all* the conversion operators, and then
		 select from them.  */
	      fns = NULL_TREE;

	      methods = CLASSTYPE_METHOD_VEC (ctype);
	      if (methods)
		for (idx = CLASSTYPE_FIRST_CONVERSION_SLOT;
		     VEC_iterate (tree, methods, idx, ovl);
		     ++idx)
		  {
		    if (!DECL_CONV_FN_P (OVL_CURRENT (ovl)))
		      /* There are no more conversion functions.  */
		      break;

		    /* Glue all these conversion functions together
		       with those we already have.  */
		    for (; ovl; ovl = OVL_NEXT (ovl))
		      fns = ovl_cons (OVL_CURRENT (ovl), fns);
		  }
	    }

	  if (fns == NULL_TREE)
	    {
	      error ("no member function %qD declared in %qT", name, ctype);
	      return error_mark_node;
	    }
	  else
	    TREE_OPERAND (declarator, 0) = fns;
	}

      /* Figure out what exactly is being specialized at this point.
	 Note that for an explicit instantiation, even one for a
	 member function, we cannot tell apriori whether the
	 instantiation is for a member template, or just a member
	 function of a template class.  Even if a member template is
	 being instantiated, the member template arguments may be
	 elided if they can be deduced from the rest of the
	 declaration.  */
      tmpl = determine_specialization (declarator, decl,
				       &targs,
				       member_specialization,
				       template_count,
				       tsk);

      if (!tmpl || tmpl == error_mark_node)
	/* We couldn't figure out what this declaration was
	   specializing.  */
	return error_mark_node;
      else
	{
	  tree gen_tmpl = most_general_template (tmpl);

	  if (explicit_instantiation)
	    {
	      /* We don't set DECL_EXPLICIT_INSTANTIATION here; that
		 is done by do_decl_instantiation later.  */

	      int arg_depth = TMPL_ARGS_DEPTH (targs);
	      int parm_depth = TMPL_PARMS_DEPTH (DECL_TEMPLATE_PARMS (tmpl));

	      if (arg_depth > parm_depth)
		{
		  /* If TMPL is not the most general template (for
		     example, if TMPL is a friend template that is
		     injected into namespace scope), then there will
		     be too many levels of TARGS.  Remove some of them
		     here.  */
		  int i;
		  tree new_targs;

		  new_targs = make_tree_vec (parm_depth);
		  for (i = arg_depth - parm_depth; i < arg_depth; ++i)
		    TREE_VEC_ELT (new_targs, i - (arg_depth - parm_depth))
		      = TREE_VEC_ELT (targs, i);
		  targs = new_targs;
		}

	      return instantiate_template (tmpl, targs, tf_error);
	    }

	  /* If we thought that the DECL was a member function, but it
	     turns out to be specializing a static member function,
	     make DECL a static member function as well.  */
	  if (DECL_STATIC_FUNCTION_P (tmpl)
	      && DECL_NONSTATIC_MEMBER_FUNCTION_P (decl))
	    revert_static_member_fn (decl);

	  /* If this is a specialization of a member template of a
	     template class, we want to return the TEMPLATE_DECL, not
	     the specialization of it.  */
	  if (tsk == tsk_template)
	    {
	      SET_DECL_TEMPLATE_SPECIALIZATION (tmpl);
	      DECL_INITIAL (DECL_TEMPLATE_RESULT (tmpl)) = NULL_TREE;
	      if (have_def)
		{
		  DECL_SOURCE_LOCATION (tmpl) = DECL_SOURCE_LOCATION (decl);
		  DECL_SOURCE_LOCATION (DECL_TEMPLATE_RESULT (tmpl))
		    = DECL_SOURCE_LOCATION (decl);
		  /* We want to use the argument list specified in the
		     definition, not in the original declaration.  */
		  DECL_ARGUMENTS (DECL_TEMPLATE_RESULT (tmpl))
		    = DECL_ARGUMENTS (decl);
		}
	      return tmpl;
	    }

	  /* Set up the DECL_TEMPLATE_INFO for DECL.  */
	  DECL_TEMPLATE_INFO (decl) = tree_cons (tmpl, targs, NULL_TREE);

	  /* Inherit default function arguments from the template
	     DECL is specializing.  */
	  copy_default_args_to_explicit_spec (decl);

	  /* This specialization has the same protection as the
	     template it specializes.  */
	  TREE_PRIVATE (decl) = TREE_PRIVATE (gen_tmpl);
	  TREE_PROTECTED (decl) = TREE_PROTECTED (gen_tmpl);

	  /* If DECL is a friend declaration, declared using an
	     unqualified name, the namespace associated with DECL may
	     have been set incorrectly.  For example, in:

	       template <typename T> void f(T);
	       namespace N {
		 struct S { friend void f<int>(int); }
	       }

	     we will have set the DECL_CONTEXT for the friend
	     declaration to N, rather than to the global namespace.  */
	  if (DECL_NAMESPACE_SCOPE_P (decl))
	    DECL_CONTEXT (decl) = DECL_CONTEXT (tmpl);

	  if (is_friend && !have_def)
	    /* This is not really a declaration of a specialization.
	       It's just the name of an instantiation.  But, it's not
	       a request for an instantiation, either.  */
	    SET_DECL_IMPLICIT_INSTANTIATION (decl);
	  else if (DECL_CONSTRUCTOR_P (decl) || DECL_DESTRUCTOR_P (decl))
	    /* This is indeed a specialization.  In case of constructors
	       and destructors, we need in-charge and not-in-charge
	       versions in V3 ABI.  */
	    clone_function_decl (decl, /*update_method_vec_p=*/0);

	  /* Register this specialization so that we can find it
	     again.  */
	  decl = register_specialization (decl, gen_tmpl, targs, is_friend);
	}
    }

  return decl;
}

/* Returns 1 iff PARMS1 and PARMS2 are identical sets of template
   parameters.  These are represented in the same format used for
   DECL_TEMPLATE_PARMS.  */

int
comp_template_parms (tree parms1, tree parms2)
{
  tree p1;
  tree p2;

  if (parms1 == parms2)
    return 1;

  for (p1 = parms1, p2 = parms2;
       p1 != NULL_TREE && p2 != NULL_TREE;
       p1 = TREE_CHAIN (p1), p2 = TREE_CHAIN (p2))
    {
      tree t1 = TREE_VALUE (p1);
      tree t2 = TREE_VALUE (p2);
      int i;

      gcc_assert (TREE_CODE (t1) == TREE_VEC);
      gcc_assert (TREE_CODE (t2) == TREE_VEC);

      if (TREE_VEC_LENGTH (t1) != TREE_VEC_LENGTH (t2))
	return 0;

      for (i = 0; i < TREE_VEC_LENGTH (t2); ++i)
	{
          tree parm1 = TREE_VALUE (TREE_VEC_ELT (t1, i));
          tree parm2 = TREE_VALUE (TREE_VEC_ELT (t2, i));

          /* If either of the template parameters are invalid, assume
             they match for the sake of error recovery. */
          if (parm1 == error_mark_node || parm2 == error_mark_node)
            return 1;

	  if (TREE_CODE (parm1) != TREE_CODE (parm2))
	    return 0;

	  if (TREE_CODE (parm1) == TEMPLATE_TYPE_PARM)
	    continue;
	  else if (!same_type_p (TREE_TYPE (parm1), TREE_TYPE (parm2)))
	    return 0;
	}
    }

  if ((p1 != NULL_TREE) != (p2 != NULL_TREE))
    /* One set of parameters has more parameters lists than the
       other.  */
    return 0;

  return 1;
}

/* Complain if DECL shadows a template parameter.

   [temp.local]: A template-parameter shall not be redeclared within its
   scope (including nested scopes).  */

void
check_template_shadow (tree decl)
{
  tree olddecl;

  /* If we're not in a template, we can't possibly shadow a template
     parameter.  */
  if (!current_template_parms)
    return;

  /* Figure out what we're shadowing.  */
  if (TREE_CODE (decl) == OVERLOAD)
    decl = OVL_CURRENT (decl);
  olddecl = innermost_non_namespace_value (DECL_NAME (decl));

  /* If there's no previous binding for this name, we're not shadowing
     anything, let alone a template parameter.  */
  if (!olddecl)
    return;

  /* If we're not shadowing a template parameter, we're done.  Note
     that OLDDECL might be an OVERLOAD (or perhaps even an
     ERROR_MARK), so we can't just blithely assume it to be a _DECL
     node.  */
  if (!DECL_P (olddecl) || !DECL_TEMPLATE_PARM_P (olddecl))
    return;

  /* We check for decl != olddecl to avoid bogus errors for using a
     name inside a class.  We check TPFI to avoid duplicate errors for
     inline member templates.  */
  if (decl == olddecl
      || TEMPLATE_PARMS_FOR_INLINE (current_template_parms))
    return;

  error ("declaration of %q+#D", decl);
  error (" shadows template parm %q+#D", olddecl);
}

/* Return a new TEMPLATE_PARM_INDEX with the indicated INDEX, LEVEL,
   ORIG_LEVEL, DECL, and TYPE.  */

static tree
build_template_parm_index (int index,
			   int level,
			   int orig_level,
			   tree decl,
			   tree type)
{
  tree t = make_node (TEMPLATE_PARM_INDEX);
  TEMPLATE_PARM_IDX (t) = index;
  TEMPLATE_PARM_LEVEL (t) = level;
  TEMPLATE_PARM_ORIG_LEVEL (t) = orig_level;
  TEMPLATE_PARM_DECL (t) = decl;
  TREE_TYPE (t) = type;
  TREE_CONSTANT (t) = TREE_CONSTANT (decl);
  TREE_INVARIANT (t) = TREE_INVARIANT (decl);
  TREE_READONLY (t) = TREE_READONLY (decl);

  return t;
}

/* Return a TEMPLATE_PARM_INDEX, similar to INDEX, but whose
   TEMPLATE_PARM_LEVEL has been decreased by LEVELS.  If such a
   TEMPLATE_PARM_INDEX already exists, it is returned; otherwise, a
   new one is created.  */

static tree
reduce_template_parm_level (tree index, tree type, int levels)
{
  if (TEMPLATE_PARM_DESCENDANTS (index) == NULL_TREE
      || (TEMPLATE_PARM_LEVEL (TEMPLATE_PARM_DESCENDANTS (index))
	  != TEMPLATE_PARM_LEVEL (index) - levels))
    {
      tree orig_decl = TEMPLATE_PARM_DECL (index);
      tree decl, t;

      decl = build_decl (TREE_CODE (orig_decl), DECL_NAME (orig_decl), type);
      TREE_CONSTANT (decl) = TREE_CONSTANT (orig_decl);
      TREE_INVARIANT (decl) = TREE_INVARIANT (orig_decl);
      TREE_READONLY (decl) = TREE_READONLY (orig_decl);
      DECL_ARTIFICIAL (decl) = 1;
      SET_DECL_TEMPLATE_PARM_P (decl);

      t = build_template_parm_index (TEMPLATE_PARM_IDX (index),
				     TEMPLATE_PARM_LEVEL (index) - levels,
				     TEMPLATE_PARM_ORIG_LEVEL (index),
				     decl, type);
      TEMPLATE_PARM_DESCENDANTS (index) = t;

	/* Template template parameters need this.  */
      if (TREE_CODE (decl) != CONST_DECL)
	DECL_TEMPLATE_PARMS (decl)
	  = DECL_TEMPLATE_PARMS (TEMPLATE_PARM_DECL (index));
    }

  return TEMPLATE_PARM_DESCENDANTS (index);
}

/* Process information from new template parameter PARM and append it to the
   LIST being built.  This new parameter is a non-type parameter iff
   IS_NON_TYPE is true.  */

tree
process_template_parm (tree list, tree parm, bool is_non_type)
{
  tree decl = 0;
  tree defval;
  tree err_parm_list;
  int idx = 0;

  gcc_assert (TREE_CODE (parm) == TREE_LIST);
  defval = TREE_PURPOSE (parm);

  if (list)
    {
      tree p = tree_last (list);

      if (p && TREE_VALUE (p) != error_mark_node)
        {
          p = TREE_VALUE (p);
          if (TREE_CODE (p) == TYPE_DECL || TREE_CODE (p) == TEMPLATE_DECL)
            idx = TEMPLATE_TYPE_IDX (TREE_TYPE (p));
          else
            idx = TEMPLATE_PARM_IDX (DECL_INITIAL (p));
        }

      ++idx;
    }
  else
    idx = 0;

  if (is_non_type)
    {
      parm = TREE_VALUE (parm);

      SET_DECL_TEMPLATE_PARM_P (parm);

      if (TREE_TYPE (parm) == error_mark_node)
        {
          err_parm_list = build_tree_list (defval, parm);
          TREE_VALUE (err_parm_list) = error_mark_node;
	   return chainon (list, err_parm_list);
        }
      else
      {
	/* [temp.param]

	   The top-level cv-qualifiers on the template-parameter are
	   ignored when determining its type.  */
	TREE_TYPE (parm) = TYPE_MAIN_VARIANT (TREE_TYPE (parm));
	if (invalid_nontype_parm_type_p (TREE_TYPE (parm), 1))
          {
            err_parm_list = build_tree_list (defval, parm);
            TREE_VALUE (err_parm_list) = error_mark_node;
	     return chainon (list, err_parm_list);
          }
      }

      /* A template parameter is not modifiable.  */
      TREE_CONSTANT (parm) = 1;
      TREE_INVARIANT (parm) = 1;
      TREE_READONLY (parm) = 1;
      decl = build_decl (CONST_DECL, DECL_NAME (parm), TREE_TYPE (parm));
      TREE_CONSTANT (decl) = 1;
      TREE_INVARIANT (decl) = 1;
      TREE_READONLY (decl) = 1;
      DECL_INITIAL (parm) = DECL_INITIAL (decl)
	= build_template_parm_index (idx, processing_template_decl,
				     processing_template_decl,
				     decl, TREE_TYPE (parm));
    }
  else
    {
      tree t;
      parm = TREE_VALUE (TREE_VALUE (parm));

      if (parm && TREE_CODE (parm) == TEMPLATE_DECL)
	{
	  t = make_aggr_type (TEMPLATE_TEMPLATE_PARM);
	  /* This is for distinguishing between real templates and template
	     template parameters */
	  TREE_TYPE (parm) = t;
	  TREE_TYPE (DECL_TEMPLATE_RESULT (parm)) = t;
	  decl = parm;
	}
      else
	{
	  t = make_aggr_type (TEMPLATE_TYPE_PARM);
	  /* parm is either IDENTIFIER_NODE or NULL_TREE.  */
	  decl = build_decl (TYPE_DECL, parm, t);
	}

      TYPE_NAME (t) = decl;
      TYPE_STUB_DECL (t) = decl;
      parm = decl;
      TEMPLATE_TYPE_PARM_INDEX (t)
	= build_template_parm_index (idx, processing_template_decl,
				     processing_template_decl,
				     decl, TREE_TYPE (parm));
    }
  DECL_ARTIFICIAL (decl) = 1;
  SET_DECL_TEMPLATE_PARM_P (decl);
  pushdecl (decl);
  parm = build_tree_list (defval, parm);
  return chainon (list, parm);
}

/* The end of a template parameter list has been reached.  Process the
   tree list into a parameter vector, converting each parameter into a more
   useful form.	 Type parameters are saved as IDENTIFIER_NODEs, and others
   as PARM_DECLs.  */

tree
end_template_parm_list (tree parms)
{
  int nparms;
  tree parm, next;
  tree saved_parmlist = make_tree_vec (list_length (parms));

  current_template_parms
    = tree_cons (size_int (processing_template_decl),
		 saved_parmlist, current_template_parms);

  for (parm = parms, nparms = 0; parm; parm = next, nparms++)
    {
      next = TREE_CHAIN (parm);
      TREE_VEC_ELT (saved_parmlist, nparms) = parm;
      TREE_CHAIN (parm) = NULL_TREE;
    }

  --processing_template_parmlist;

  return saved_parmlist;
}

/* end_template_decl is called after a template declaration is seen.  */

void
end_template_decl (void)
{
  reset_specialization ();

  if (! processing_template_decl)
    return;

  /* This matches the pushlevel in begin_template_parm_list.  */
  finish_scope ();

  --processing_template_decl;
  current_template_parms = TREE_CHAIN (current_template_parms);
}

/* Given a template argument vector containing the template PARMS.
   The innermost PARMS are given first.  */

static tree
current_template_args (void)
{
  tree header;
  tree args = NULL_TREE;
  int length = TMPL_PARMS_DEPTH (current_template_parms);
  int l = length;

  /* If there is only one level of template parameters, we do not
     create a TREE_VEC of TREE_VECs.  Instead, we return a single
     TREE_VEC containing the arguments.  */
  if (length > 1)
    args = make_tree_vec (length);

  for (header = current_template_parms; header; header = TREE_CHAIN (header))
    {
      tree a = copy_node (TREE_VALUE (header));
      int i;

      TREE_TYPE (a) = NULL_TREE;
      for (i = TREE_VEC_LENGTH (a) - 1; i >= 0; --i)
	{
	  tree t = TREE_VEC_ELT (a, i);

	  /* T will be a list if we are called from within a
	     begin/end_template_parm_list pair, but a vector directly
	     if within a begin/end_member_template_processing pair.  */
	  if (TREE_CODE (t) == TREE_LIST)
	    {
	      t = TREE_VALUE (t);

	      if (t != error_mark_node)
		{
		  if (TREE_CODE (t) == TYPE_DECL
		      || TREE_CODE (t) == TEMPLATE_DECL)
		    t = TREE_TYPE (t);
		  else
		    t = DECL_INITIAL (t);
		}

	      TREE_VEC_ELT (a, i) = t;
	    }
	}

      if (length > 1)
	TREE_VEC_ELT (args, --l) = a;
      else
	args = a;
    }

  return args;
}

/* Return a TEMPLATE_DECL corresponding to DECL, using the indicated
   template PARMS.  If MEMBER_TEMPLATE_P is true, the new template is
   a member template.  Used by push_template_decl below.  */

static tree
build_template_decl (tree decl, tree parms, bool member_template_p)
{
  tree tmpl = build_lang_decl (TEMPLATE_DECL, DECL_NAME (decl), NULL_TREE);
  DECL_TEMPLATE_PARMS (tmpl) = parms;
  DECL_CONTEXT (tmpl) = DECL_CONTEXT (decl);
  DECL_MEMBER_TEMPLATE_P (tmpl) = member_template_p;
  if (DECL_LANG_SPECIFIC (decl))
    {
      DECL_STATIC_FUNCTION_P (tmpl) = DECL_STATIC_FUNCTION_P (decl);
      DECL_CONSTRUCTOR_P (tmpl) = DECL_CONSTRUCTOR_P (decl);
      DECL_DESTRUCTOR_P (tmpl) = DECL_DESTRUCTOR_P (decl);
      DECL_NONCONVERTING_P (tmpl) = DECL_NONCONVERTING_P (decl);
      DECL_ASSIGNMENT_OPERATOR_P (tmpl) = DECL_ASSIGNMENT_OPERATOR_P (decl);
      if (DECL_OVERLOADED_OPERATOR_P (decl))
	SET_OVERLOADED_OPERATOR_CODE (tmpl,
				      DECL_OVERLOADED_OPERATOR_P (decl));
    }

  return tmpl;
}

struct template_parm_data
{
  /* The level of the template parameters we are currently
     processing.  */
  int level;

  /* The index of the specialization argument we are currently
     processing.  */
  int current_arg;

  /* An array whose size is the number of template parameters.  The
     elements are nonzero if the parameter has been used in any one
     of the arguments processed so far.  */
  int* parms;

  /* An array whose size is the number of template arguments.  The
     elements are nonzero if the argument makes use of template
     parameters of this level.  */
  int* arg_uses_template_parms;
};

/* Subroutine of push_template_decl used to see if each template
   parameter in a partial specialization is used in the explicit
   argument list.  If T is of the LEVEL given in DATA (which is
   treated as a template_parm_data*), then DATA->PARMS is marked
   appropriately.  */

static int
mark_template_parm (tree t, void* data)
{
  int level;
  int idx;
  struct template_parm_data* tpd = (struct template_parm_data*) data;

  if (TREE_CODE (t) == TEMPLATE_PARM_INDEX)
    {
      level = TEMPLATE_PARM_LEVEL (t);
      idx = TEMPLATE_PARM_IDX (t);
    }
  else
    {
      level = TEMPLATE_TYPE_LEVEL (t);
      idx = TEMPLATE_TYPE_IDX (t);
    }

  if (level == tpd->level)
    {
      tpd->parms[idx] = 1;
      tpd->arg_uses_template_parms[tpd->current_arg] = 1;
    }

  /* Return zero so that for_each_template_parm will continue the
     traversal of the tree; we want to mark *every* template parm.  */
  return 0;
}

/* Process the partial specialization DECL.  */

static tree
process_partial_specialization (tree decl)
{
  tree type = TREE_TYPE (decl);
  tree maintmpl = CLASSTYPE_TI_TEMPLATE (type);
  tree specargs = CLASSTYPE_TI_ARGS (type);
  tree inner_args = INNERMOST_TEMPLATE_ARGS (specargs);
  tree inner_parms = INNERMOST_TEMPLATE_PARMS (current_template_parms);
  tree main_inner_parms = DECL_INNERMOST_TEMPLATE_PARMS (maintmpl);
  int nargs = TREE_VEC_LENGTH (inner_args);
  int ntparms = TREE_VEC_LENGTH (inner_parms);
  int  i;
  int did_error_intro = 0;
  struct template_parm_data tpd;
  struct template_parm_data tpd2;

  /* We check that each of the template parameters given in the
     partial specialization is used in the argument list to the
     specialization.  For example:

       template <class T> struct S;
       template <class T> struct S<T*>;

     The second declaration is OK because `T*' uses the template
     parameter T, whereas

       template <class T> struct S<int>;

     is no good.  Even trickier is:

       template <class T>
       struct S1
       {
	  template <class U>
	  struct S2;
	  template <class U>
	  struct S2<T>;
       };

     The S2<T> declaration is actually invalid; it is a
     full-specialization.  Of course,

	  template <class U>
	  struct S2<T (*)(U)>;

     or some such would have been OK.  */
  tpd.level = TMPL_PARMS_DEPTH (current_template_parms);
  tpd.parms = (int *) alloca (sizeof (int) * ntparms);
  memset (tpd.parms, 0, sizeof (int) * ntparms);

  tpd.arg_uses_template_parms = (int *) alloca (sizeof (int) * nargs);
  memset (tpd.arg_uses_template_parms, 0, sizeof (int) * nargs);
  for (i = 0; i < nargs; ++i)
    {
      tpd.current_arg = i;
      for_each_template_parm (TREE_VEC_ELT (inner_args, i),
			      &mark_template_parm,
			      &tpd,
			      NULL);
    }
  for (i = 0; i < ntparms; ++i)
    if (tpd.parms[i] == 0)
      {
	/* One of the template parms was not used in the
	   specialization.  */
	if (!did_error_intro)
	  {
	    error ("template parameters not used in partial specialization:");
	    did_error_intro = 1;
	  }

	error ("        %qD", TREE_VALUE (TREE_VEC_ELT (inner_parms, i)));
      }

  /* [temp.class.spec]

     The argument list of the specialization shall not be identical to
     the implicit argument list of the primary template.  */
  if (comp_template_args
      (inner_args,
       INNERMOST_TEMPLATE_ARGS (CLASSTYPE_TI_ARGS (TREE_TYPE
						   (maintmpl)))))
    error ("partial specialization %qT does not specialize any template arguments", type);

  /* [temp.class.spec]

     A partially specialized non-type argument expression shall not
     involve template parameters of the partial specialization except
     when the argument expression is a simple identifier.

     The type of a template parameter corresponding to a specialized
     non-type argument shall not be dependent on a parameter of the
     specialization.  */
  gcc_assert (nargs == DECL_NTPARMS (maintmpl));
  tpd2.parms = 0;
  for (i = 0; i < nargs; ++i)
    {
      tree arg = TREE_VEC_ELT (inner_args, i);
      if (/* These first two lines are the `non-type' bit.  */
	  !TYPE_P (arg)
	  && TREE_CODE (arg) != TEMPLATE_DECL
	  /* This next line is the `argument expression is not just a
	     simple identifier' condition and also the `specialized
	     non-type argument' bit.  */
	  && TREE_CODE (arg) != TEMPLATE_PARM_INDEX)
	{
	  if (tpd.arg_uses_template_parms[i])
	    error ("template argument %qE involves template parameter(s)", arg);
	  else
	    {
	      /* Look at the corresponding template parameter,
		 marking which template parameters its type depends
		 upon.  */
	      tree type =
		TREE_TYPE (TREE_VALUE (TREE_VEC_ELT (main_inner_parms,
						     i)));

	      if (!tpd2.parms)
		{
		  /* We haven't yet initialized TPD2.  Do so now.  */
		  tpd2.arg_uses_template_parms
		    = (int *) alloca (sizeof (int) * nargs);
		  /* The number of parameters here is the number in the
		     main template, which, as checked in the assertion
		     above, is NARGS.  */
		  tpd2.parms = (int *) alloca (sizeof (int) * nargs);
		  tpd2.level =
		    TMPL_PARMS_DEPTH (DECL_TEMPLATE_PARMS (maintmpl));
		}

	      /* Mark the template parameters.  But this time, we're
		 looking for the template parameters of the main
		 template, not in the specialization.  */
	      tpd2.current_arg = i;
	      tpd2.arg_uses_template_parms[i] = 0;
	      memset (tpd2.parms, 0, sizeof (int) * nargs);
	      for_each_template_parm (type,
				      &mark_template_parm,
				      &tpd2,
				      NULL);

	      if (tpd2.arg_uses_template_parms [i])
		{
		  /* The type depended on some template parameters.
		     If they are fully specialized in the
		     specialization, that's OK.  */
		  int j;
		  for (j = 0; j < nargs; ++j)
		    if (tpd2.parms[j] != 0
			&& tpd.arg_uses_template_parms [j])
		      {
			error ("type %qT of template argument %qE depends "
			       "on template parameter(s)",
			       type,
			       arg);
			break;
		      }
		}
	    }
	}
    }

  if (retrieve_specialization (maintmpl, specargs,
			       /*class_specializations_p=*/true))
    /* We've already got this specialization.  */
    return decl;

  DECL_TEMPLATE_SPECIALIZATIONS (maintmpl)
    = tree_cons (specargs, inner_parms,
		 DECL_TEMPLATE_SPECIALIZATIONS (maintmpl));
  TREE_TYPE (DECL_TEMPLATE_SPECIALIZATIONS (maintmpl)) = type;
  return decl;
}

/* Check that a template declaration's use of default arguments is not
   invalid.  Here, PARMS are the template parameters.  IS_PRIMARY is
   nonzero if DECL is the thing declared by a primary template.
   IS_PARTIAL is nonzero if DECL is a partial specialization.  */

static void
check_default_tmpl_args (tree decl, tree parms, int is_primary, int is_partial)
{
  const char *msg;
  int last_level_to_check;
  tree parm_level;

  /* [temp.param]

     A default template-argument shall not be specified in a
     function template declaration or a function template definition, nor
     in the template-parameter-list of the definition of a member of a
     class template.  */

  if (TREE_CODE (CP_DECL_CONTEXT (decl)) == FUNCTION_DECL)
    /* You can't have a function template declaration in a local
       scope, nor you can you define a member of a class template in a
       local scope.  */
    return;

  if (current_class_type
      && !TYPE_BEING_DEFINED (current_class_type)
      && DECL_LANG_SPECIFIC (decl)
      /* If this is either a friend defined in the scope of the class
	 or a member function.  */
      && (DECL_FUNCTION_MEMBER_P (decl)
	  ? same_type_p (DECL_CONTEXT (decl), current_class_type)
	  : DECL_FRIEND_CONTEXT (decl)
	  ? same_type_p (DECL_FRIEND_CONTEXT (decl), current_class_type)
	  : false)
      /* And, if it was a member function, it really was defined in
	 the scope of the class.  */
      && (!DECL_FUNCTION_MEMBER_P (decl)
	  || DECL_INITIALIZED_IN_CLASS_P (decl)))
    /* We already checked these parameters when the template was
       declared, so there's no need to do it again now.  This function
       was defined in class scope, but we're processing it's body now
       that the class is complete.  */
    return;

  /* [temp.param]

     If a template-parameter has a default template-argument, all
     subsequent template-parameters shall have a default
     template-argument supplied.  */
  for (parm_level = parms; parm_level; parm_level = TREE_CHAIN (parm_level))
    {
      tree inner_parms = TREE_VALUE (parm_level);
      int ntparms = TREE_VEC_LENGTH (inner_parms);
      int seen_def_arg_p = 0;
      int i;

      for (i = 0; i < ntparms; ++i)
	{
	  tree parm = TREE_VEC_ELT (inner_parms, i);

          if (parm == error_mark_node)
            continue;

	  if (TREE_PURPOSE (parm))
	    seen_def_arg_p = 1;
	  else if (seen_def_arg_p)
	    {
	      error ("no default argument for %qD", TREE_VALUE (parm));
	      /* For better subsequent error-recovery, we indicate that
		 there should have been a default argument.  */
	      TREE_PURPOSE (parm) = error_mark_node;
	    }
	}
    }

  if (TREE_CODE (decl) != TYPE_DECL || is_partial || !is_primary)
    /* For an ordinary class template, default template arguments are
       allowed at the innermost level, e.g.:
	 template <class T = int>
	 struct S {};
       but, in a partial specialization, they're not allowed even
       there, as we have in [temp.class.spec]:

	 The template parameter list of a specialization shall not
	 contain default template argument values.

       So, for a partial specialization, or for a function template,
       we look at all of them.  */
    ;
  else
    /* But, for a primary class template that is not a partial
       specialization we look at all template parameters except the
       innermost ones.  */
    parms = TREE_CHAIN (parms);

  /* Figure out what error message to issue.  */
  if (TREE_CODE (decl) == FUNCTION_DECL)
    msg = "default template arguments may not be used in function templates";
  else if (is_partial)
    msg = "default template arguments may not be used in partial specializations";
  else
    msg = "default argument for template parameter for class enclosing %qD";

  if (current_class_type && TYPE_BEING_DEFINED (current_class_type))
    /* If we're inside a class definition, there's no need to
       examine the parameters to the class itself.  On the one
       hand, they will be checked when the class is defined, and,
       on the other, default arguments are valid in things like:
	 template <class T = double>
	 struct S { template <class U> void f(U); };
       Here the default argument for `S' has no bearing on the
       declaration of `f'.  */
    last_level_to_check = template_class_depth (current_class_type) + 1;
  else
    /* Check everything.  */
    last_level_to_check = 0;

  for (parm_level = parms;
       parm_level && TMPL_PARMS_DEPTH (parm_level) >= last_level_to_check;
       parm_level = TREE_CHAIN (parm_level))
    {
      tree inner_parms = TREE_VALUE (parm_level);
      int i;
      int ntparms;

      ntparms = TREE_VEC_LENGTH (inner_parms);
      for (i = 0; i < ntparms; ++i)
        {
          if (TREE_VEC_ELT (inner_parms, i) == error_mark_node)
            continue;

	  if (TREE_PURPOSE (TREE_VEC_ELT (inner_parms, i)))
	    {
	      if (msg)
	        {
		  error (msg, decl);
		  msg = 0;
	        }

	      /* Clear out the default argument so that we are not
	         confused later.  */
	      TREE_PURPOSE (TREE_VEC_ELT (inner_parms, i)) = NULL_TREE;
	    }
        }

      /* At this point, if we're still interested in issuing messages,
	 they must apply to classes surrounding the object declared.  */
      if (msg)
	msg = "default argument for template parameter for class enclosing %qD";
    }
}

/* Worker for push_template_decl_real, called via
   for_each_template_parm.  DATA is really an int, indicating the
   level of the parameters we are interested in.  If T is a template
   parameter of that level, return nonzero.  */

static int
template_parm_this_level_p (tree t, void* data)
{
  int this_level = *(int *)data;
  int level;

  if (TREE_CODE (t) == TEMPLATE_PARM_INDEX)
    level = TEMPLATE_PARM_LEVEL (t);
  else
    level = TEMPLATE_TYPE_LEVEL (t);
  return level == this_level;
}

/* Creates a TEMPLATE_DECL for the indicated DECL using the template
   parameters given by current_template_args, or reuses a
   previously existing one, if appropriate.  Returns the DECL, or an
   equivalent one, if it is replaced via a call to duplicate_decls.

   If IS_FRIEND is true, DECL is a friend declaration.  */

tree
push_template_decl_real (tree decl, bool is_friend)
{
  tree tmpl;
  tree args;
  tree info;
  tree ctx;
  int primary;
  int is_partial;
  int new_template_p = 0;
  /* True if the template is a member template, in the sense of
     [temp.mem].  */
  bool member_template_p = false;

  if (decl == error_mark_node)
    return decl;

  /* See if this is a partial specialization.  */
  is_partial = (DECL_IMPLICIT_TYPEDEF_P (decl)
		&& TREE_CODE (TREE_TYPE (decl)) != ENUMERAL_TYPE
		&& CLASSTYPE_TEMPLATE_SPECIALIZATION (TREE_TYPE (decl)));

  if (TREE_CODE (decl) == FUNCTION_DECL && DECL_FRIEND_P (decl))
    is_friend = true;

  if (is_friend)
    /* For a friend, we want the context of the friend function, not
       the type of which it is a friend.  */
    ctx = DECL_CONTEXT (decl);
  else if (CP_DECL_CONTEXT (decl)
	   && TREE_CODE (CP_DECL_CONTEXT (decl)) != NAMESPACE_DECL)
    /* In the case of a virtual function, we want the class in which
       it is defined.  */
    ctx = CP_DECL_CONTEXT (decl);
  else
    /* Otherwise, if we're currently defining some class, the DECL
       is assumed to be a member of the class.  */
    ctx = current_scope ();

  if (ctx && TREE_CODE (ctx) == NAMESPACE_DECL)
    ctx = NULL_TREE;

  if (!DECL_CONTEXT (decl))
    DECL_CONTEXT (decl) = FROB_CONTEXT (current_namespace);

  /* See if this is a primary template.  */
  if (is_friend && ctx)
    /* A friend template that specifies a class context, i.e.
         template <typename T> friend void A<T>::f();
       is not primary.  */
    primary = 0;
  else
    primary = template_parm_scope_p ();

  if (primary)
    {
      if (DECL_CLASS_SCOPE_P (decl))
	member_template_p = true;
      if (TREE_CODE (decl) == TYPE_DECL
	  && ANON_AGGRNAME_P (DECL_NAME (decl)))
	error ("template class without a name");
      else if (TREE_CODE (decl) == FUNCTION_DECL)
	{
	  if (DECL_DESTRUCTOR_P (decl))
	    {
	      /* [temp.mem]

		 A destructor shall not be a member template.  */
	      error ("destructor %qD declared as member template", decl);
	      return error_mark_node;
	    }
	  if (NEW_DELETE_OPNAME_P (DECL_NAME (decl))
	      && (!TYPE_ARG_TYPES (TREE_TYPE (decl))
		  || TYPE_ARG_TYPES (TREE_TYPE (decl)) == void_list_node
		  || !TREE_CHAIN (TYPE_ARG_TYPES (TREE_TYPE (decl)))
		  || (TREE_CHAIN (TYPE_ARG_TYPES ((TREE_TYPE (decl))))
		      == void_list_node)))
	    {
	      /* [basic.stc.dynamic.allocation]

		 An allocation function can be a function
		 template. ... Template allocation functions shall
		 have two or more parameters.  */
	      error ("invalid template declaration of %qD", decl);
	      return error_mark_node;
	    }
	}
      else if (DECL_IMPLICIT_TYPEDEF_P (decl)
	       && CLASS_TYPE_P (TREE_TYPE (decl)))
	/* OK */;
      else
	{
	  error ("template declaration of %q#D", decl);
	  return error_mark_node;
	}
    }

  /* Check to see that the rules regarding the use of default
     arguments are not being violated.  */
  check_default_tmpl_args (decl, current_template_parms,
			   primary, is_partial);

  if (is_partial)
    return process_partial_specialization (decl);

  args = current_template_args ();

  if (!ctx
      || TREE_CODE (ctx) == FUNCTION_DECL
      || (CLASS_TYPE_P (ctx) && TYPE_BEING_DEFINED (ctx))
      || (is_friend && !DECL_TEMPLATE_INFO (decl)))
    {
      if (DECL_LANG_SPECIFIC (decl)
	  && DECL_TEMPLATE_INFO (decl)
	  && DECL_TI_TEMPLATE (decl))
	tmpl = DECL_TI_TEMPLATE (decl);
      /* If DECL is a TYPE_DECL for a class-template, then there won't
	 be DECL_LANG_SPECIFIC.  The information equivalent to
	 DECL_TEMPLATE_INFO is found in TYPE_TEMPLATE_INFO instead.  */
      else if (DECL_IMPLICIT_TYPEDEF_P (decl)
	       && TYPE_TEMPLATE_INFO (TREE_TYPE (decl))
	       && TYPE_TI_TEMPLATE (TREE_TYPE (decl)))
	{
	  /* Since a template declaration already existed for this
	     class-type, we must be redeclaring it here.  Make sure
	     that the redeclaration is valid.  */
	  redeclare_class_template (TREE_TYPE (decl),
				    current_template_parms);
	  /* We don't need to create a new TEMPLATE_DECL; just use the
	     one we already had.  */
	  tmpl = TYPE_TI_TEMPLATE (TREE_TYPE (decl));
	}
      else
	{
	  tmpl = build_template_decl (decl, current_template_parms,
				      member_template_p);
	  new_template_p = 1;

	  if (DECL_LANG_SPECIFIC (decl)
	      && DECL_TEMPLATE_SPECIALIZATION (decl))
	    {
	      /* A specialization of a member template of a template
		 class.  */
	      SET_DECL_TEMPLATE_SPECIALIZATION (tmpl);
	      DECL_TEMPLATE_INFO (tmpl) = DECL_TEMPLATE_INFO (decl);
	      DECL_TEMPLATE_INFO (decl) = NULL_TREE;
	    }
	}
    }
  else
    {
      tree a, t, current, parms;
      int i;

      if (TREE_CODE (decl) == TYPE_DECL)
	{
	  if ((IS_AGGR_TYPE_CODE (TREE_CODE (TREE_TYPE (decl)))
	       || TREE_CODE (TREE_TYPE (decl)) == ENUMERAL_TYPE)
	      && TYPE_TEMPLATE_INFO (TREE_TYPE (decl))
	      && TYPE_TI_TEMPLATE (TREE_TYPE (decl)))
	    tmpl = TYPE_TI_TEMPLATE (TREE_TYPE (decl));
	  else
	    {
	      error ("%qD does not declare a template type", decl);
	      return decl;
	    }
	}
      else if (!DECL_LANG_SPECIFIC (decl) || !DECL_TEMPLATE_INFO (decl))
	{
	  error ("template definition of non-template %q#D", decl);
	  return decl;
	}
      else
	tmpl = DECL_TI_TEMPLATE (decl);

      if (DECL_FUNCTION_TEMPLATE_P (tmpl)
	  && DECL_TEMPLATE_INFO (decl) && DECL_TI_ARGS (decl)
	  && DECL_TEMPLATE_SPECIALIZATION (decl)
	  && DECL_MEMBER_TEMPLATE_P (tmpl))
	{
	  tree new_tmpl;

	  /* The declaration is a specialization of a member
	     template, declared outside the class.  Therefore, the
	     innermost template arguments will be NULL, so we
	     replace them with the arguments determined by the
	     earlier call to check_explicit_specialization.  */
	  args = DECL_TI_ARGS (decl);

	  new_tmpl
	    = build_template_decl (decl, current_template_parms,
				   member_template_p);
	  DECL_TEMPLATE_RESULT (new_tmpl) = decl;
	  TREE_TYPE (new_tmpl) = TREE_TYPE (decl);
	  DECL_TI_TEMPLATE (decl) = new_tmpl;
	  SET_DECL_TEMPLATE_SPECIALIZATION (new_tmpl);
	  DECL_TEMPLATE_INFO (new_tmpl)
	    = tree_cons (tmpl, args, NULL_TREE);

	  register_specialization (new_tmpl,
				   most_general_template (tmpl),
				   args,
				   is_friend);
	  return decl;
	}

      /* Make sure the template headers we got make sense.  */

      parms = DECL_TEMPLATE_PARMS (tmpl);
      i = TMPL_PARMS_DEPTH (parms);
      if (TMPL_ARGS_DEPTH (args) != i)
	{
	  error ("expected %d levels of template parms for %q#D, got %d",
		 i, decl, TMPL_ARGS_DEPTH (args));
	}
      else
	for (current = decl; i > 0; --i, parms = TREE_CHAIN (parms))
	  {
	    a = TMPL_ARGS_LEVEL (args, i);
	    t = INNERMOST_TEMPLATE_PARMS (parms);

	    if (TREE_VEC_LENGTH (t) != TREE_VEC_LENGTH (a))
	      {
		if (current == decl)
		  error ("got %d template parameters for %q#D",
			 TREE_VEC_LENGTH (a), decl);
		else
		  error ("got %d template parameters for %q#T",
			 TREE_VEC_LENGTH (a), current);
		error ("  but %d required", TREE_VEC_LENGTH (t));
		return error_mark_node;
	      }

	    /* Perhaps we should also check that the parms are used in the
	       appropriate qualifying scopes in the declarator?  */

	    if (current == decl)
	      current = ctx;
	    else
	      current = TYPE_CONTEXT (current);
	  }
    }

  DECL_TEMPLATE_RESULT (tmpl) = decl;
  TREE_TYPE (tmpl) = TREE_TYPE (decl);

  /* Push template declarations for global functions and types.  Note
     that we do not try to push a global template friend declared in a
     template class; such a thing may well depend on the template
     parameters of the class.  */
  if (new_template_p && !ctx
      && !(is_friend && template_class_depth (current_class_type) > 0))
    {
      tmpl = pushdecl_namespace_level (tmpl, is_friend);
      if (tmpl == error_mark_node)
	return error_mark_node;

      /* Hide template friend classes that haven't been declared yet.  */
      if (is_friend && TREE_CODE (decl) == TYPE_DECL)
	{
	  DECL_ANTICIPATED (tmpl) = 1;
	  DECL_FRIEND_P (tmpl) = 1;
	}
    }

  if (primary)
    {
      DECL_PRIMARY_TEMPLATE (tmpl) = tmpl;
      if (DECL_CONV_FN_P (tmpl))
	{
	  int depth = TMPL_PARMS_DEPTH (DECL_TEMPLATE_PARMS (tmpl));

	  /* It is a conversion operator. See if the type converted to
	     depends on innermost template operands.  */

	  if (uses_template_parms_level (TREE_TYPE (TREE_TYPE (tmpl)),
					 depth))
	    DECL_TEMPLATE_CONV_FN_P (tmpl) = 1;
	}
    }

  /* The DECL_TI_ARGS of DECL contains full set of arguments referring
     back to its most general template.  If TMPL is a specialization,
     ARGS may only have the innermost set of arguments.  Add the missing
     argument levels if necessary.  */
  if (DECL_TEMPLATE_INFO (tmpl))
    args = add_outermost_template_args (DECL_TI_ARGS (tmpl), args);

  info = tree_cons (tmpl, args, NULL_TREE);

  if (DECL_IMPLICIT_TYPEDEF_P (decl))
    {
      SET_TYPE_TEMPLATE_INFO (TREE_TYPE (tmpl), info);
      if ((!ctx || TREE_CODE (ctx) != FUNCTION_DECL)
	  && TREE_CODE (TREE_TYPE (decl)) != ENUMERAL_TYPE
	  /* Don't change the name if we've already set it up.  */
	  && !IDENTIFIER_TEMPLATE (DECL_NAME (decl)))
	DECL_NAME (decl) = classtype_mangled_name (TREE_TYPE (decl));
    }
  else if (DECL_LANG_SPECIFIC (decl))
    DECL_TEMPLATE_INFO (decl) = info;

  return DECL_TEMPLATE_RESULT (tmpl);
}

tree
push_template_decl (tree decl)
{
  return push_template_decl_real (decl, false);
}

/* Called when a class template TYPE is redeclared with the indicated
   template PARMS, e.g.:

     template <class T> struct S;
     template <class T> struct S {};  */

bool
redeclare_class_template (tree type, tree parms)
{
  tree tmpl;
  tree tmpl_parms;
  int i;

  if (!TYPE_TEMPLATE_INFO (type))
    {
      error ("%qT is not a template type", type);
      return false;
    }

  tmpl = TYPE_TI_TEMPLATE (type);
  if (!PRIMARY_TEMPLATE_P (tmpl))
    /* The type is nested in some template class.  Nothing to worry
       about here; there are no new template parameters for the nested
       type.  */
    return true;

  if (!parms)
    {
      error ("template specifiers not specified in declaration of %qD",
	     tmpl);
      return false;
    }

  parms = INNERMOST_TEMPLATE_PARMS (parms);
  tmpl_parms = DECL_INNERMOST_TEMPLATE_PARMS (tmpl);

  if (TREE_VEC_LENGTH (parms) != TREE_VEC_LENGTH (tmpl_parms))
    {
      error ("previous declaration %q+D", tmpl);
      error ("used %d template parameter(s) instead of %d",
	     TREE_VEC_LENGTH (tmpl_parms),
	     TREE_VEC_LENGTH (parms));
      return false;
    }

  for (i = 0; i < TREE_VEC_LENGTH (tmpl_parms); ++i)
    {
      tree tmpl_parm;
      tree parm;
      tree tmpl_default;
      tree parm_default;

      if (TREE_VEC_ELT (tmpl_parms, i) == error_mark_node
          || TREE_VEC_ELT (parms, i) == error_mark_node)
        continue;

      tmpl_parm = TREE_VALUE (TREE_VEC_ELT (tmpl_parms, i));
      parm = TREE_VALUE (TREE_VEC_ELT (parms, i));
      tmpl_default = TREE_PURPOSE (TREE_VEC_ELT (tmpl_parms, i));
      parm_default = TREE_PURPOSE (TREE_VEC_ELT (parms, i));

      /* TMPL_PARM and PARM can be either TYPE_DECL, PARM_DECL, or
	 TEMPLATE_DECL.  */
      if (tmpl_parm != error_mark_node
	   && (TREE_CODE (tmpl_parm) != TREE_CODE (parm)
	   || (TREE_CODE (tmpl_parm) != TYPE_DECL
	       && !same_type_p (TREE_TYPE (tmpl_parm), TREE_TYPE (parm)))))
	{
	  error ("template parameter %q+#D", tmpl_parm);
	  error ("redeclared here as %q#D", parm);
	  return false;
	}

      if (tmpl_default != NULL_TREE && parm_default != NULL_TREE)
	{
	  /* We have in [temp.param]:

	     A template-parameter may not be given default arguments
	     by two different declarations in the same scope.  */
	  error ("redefinition of default argument for %q#D", parm);
	  error ("%J  original definition appeared here", tmpl_parm);
	  return false;
	}

      if (parm_default != NULL_TREE)
	/* Update the previous template parameters (which are the ones
	   that will really count) with the new default value.  */
	TREE_PURPOSE (TREE_VEC_ELT (tmpl_parms, i)) = parm_default;
      else if (tmpl_default != NULL_TREE)
	/* Update the new parameters, too; they'll be used as the
	   parameters for any members.  */
	TREE_PURPOSE (TREE_VEC_ELT (parms, i)) = tmpl_default;
    }

    return true;
}

/* Simplify EXPR if it is a non-dependent expression.  Returns the
   (possibly simplified) expression.  */

tree
fold_non_dependent_expr (tree expr)
{
  if (expr == NULL_TREE)
    return NULL_TREE;

  /* If we're in a template, but EXPR isn't value dependent, simplify
     it.  We're supposed to treat:

       template <typename T> void f(T[1 + 1]);
       template <typename T> void f(T[2]);

     as two declarations of the same function, for example.  */
  if (processing_template_decl
      && !type_dependent_expression_p (expr)
      && !value_dependent_expression_p (expr))
    {
      HOST_WIDE_INT saved_processing_template_decl;

      saved_processing_template_decl = processing_template_decl;
      processing_template_decl = 0;
      expr = tsubst_copy_and_build (expr,
				    /*args=*/NULL_TREE,
				    tf_error,
				    /*in_decl=*/NULL_TREE,
				    /*function_p=*/false,
				    /*integral_constant_expression_p=*/true);
      processing_template_decl = saved_processing_template_decl;
    }
  return expr;
}

/* EXPR is an expression which is used in a constant-expression context.
   For instance, it could be a VAR_DECL with a constant initializer.
   Extract the innest constant expression.

   This is basically a more powerful version of
   integral_constant_value, which can be used also in templates where
   initializers can maintain a syntactic rather than semantic form
   (even if they are non-dependent, for access-checking purposes).  */

static tree
fold_decl_constant_value (tree expr)
{
  tree const_expr = expr;
  do
    {
      expr = fold_non_dependent_expr (const_expr);
      const_expr = integral_constant_value (expr);
    }
  while (expr != const_expr);

  return expr;
}

/* Subroutine of convert_nontype_argument. Converts EXPR to TYPE, which
   must be a function or a pointer-to-function type, as specified
   in [temp.arg.nontype]: disambiguate EXPR if it is an overload set,
   and check that the resulting function has external linkage.  */

static tree
convert_nontype_argument_function (tree type, tree expr)
{
  tree fns = expr;
  tree fn, fn_no_ptr;

  fn = instantiate_type (type, fns, tf_none);
  if (fn == error_mark_node)
    return error_mark_node;

  fn_no_ptr = fn;
  if (TREE_CODE (fn_no_ptr) == ADDR_EXPR)
    fn_no_ptr = TREE_OPERAND (fn_no_ptr, 0);
  if (TREE_CODE (fn_no_ptr) == BASELINK)
    fn_no_ptr = BASELINK_FUNCTIONS (fn_no_ptr);
 
  /* [temp.arg.nontype]/1

     A template-argument for a non-type, non-template template-parameter
     shall be one of:
     [...]
     -- the address of an object or function with external linkage.  */
  if (!DECL_EXTERNAL_LINKAGE_P (fn_no_ptr))
    {
      error ("%qE is not a valid template argument for type %qT "
	     "because function %qD has not external linkage",
	     expr, type, fn_no_ptr);
      return NULL_TREE;
    }

  return fn;
}

/* Attempt to convert the non-type template parameter EXPR to the
   indicated TYPE.  If the conversion is successful, return the
   converted value.  If the conversion is unsuccessful, return
   NULL_TREE if we issued an error message, or error_mark_node if we
   did not.  We issue error messages for out-and-out bad template
   parameters, but not simply because the conversion failed, since we
   might be just trying to do argument deduction.  Both TYPE and EXPR
   must be non-dependent.

   The conversion follows the special rules described in
   [temp.arg.nontype], and it is much more strict than an implicit
   conversion.

   This function is called twice for each template argument (see
   lookup_template_class for a more accurate description of this
   problem). This means that we need to handle expressions which
   are not valid in a C++ source, but can be created from the
   first call (for instance, casts to perform conversions). These
   hacks can go away after we fix the double coercion problem.  */

static tree
convert_nontype_argument (tree type, tree expr)
{
  tree expr_type;

  /* Detect immediately string literals as invalid non-type argument.
     This special-case is not needed for correctness (we would easily
     catch this later), but only to provide better diagnostic for this
     common user mistake. As suggested by DR 100, we do not mention
     linkage issues in the diagnostic as this is not the point.  */
  if (TREE_CODE (expr) == STRING_CST)
    {
      error ("%qE is not a valid template argument for type %qT "
	     "because string literals can never be used in this context",
	     expr, type);
      return NULL_TREE;
    }

  /* If we are in a template, EXPR may be non-dependent, but still
     have a syntactic, rather than semantic, form.  For example, EXPR
     might be a SCOPE_REF, rather than the VAR_DECL to which the
     SCOPE_REF refers.  Preserving the qualifying scope is necessary
     so that access checking can be performed when the template is
     instantiated -- but here we need the resolved form so that we can
     convert the argument.  */
  expr = fold_non_dependent_expr (expr);
  if (error_operand_p (expr))
    return error_mark_node;
  expr_type = TREE_TYPE (expr);

  /* HACK: Due to double coercion, we can get a
     NOP_EXPR<REFERENCE_TYPE>(ADDR_EXPR<POINTER_TYPE> (arg)) here,
     which is the tree that we built on the first call (see
     below when coercing to reference to object or to reference to
     function). We just strip everything and get to the arg.
     See g++.old-deja/g++.oliva/template4.C and g++.dg/template/nontype9.C
     for examples.  */
  if (TREE_CODE (expr) == NOP_EXPR)
    {
      if (TYPE_REF_OBJ_P (type) || TYPE_REFFN_P (type))
	{
	  /* ??? Maybe we could use convert_from_reference here, but we
	     would need to relax its constraints because the NOP_EXPR
	     could actually change the type to something more cv-qualified,
	     and this is not folded by convert_from_reference.  */
	  tree addr = TREE_OPERAND (expr, 0);
	  gcc_assert (TREE_CODE (expr_type) == REFERENCE_TYPE);
	  gcc_assert (TREE_CODE (addr) == ADDR_EXPR);
	  gcc_assert (TREE_CODE (TREE_TYPE (addr)) == POINTER_TYPE);
	  gcc_assert (same_type_ignoring_top_level_qualifiers_p
		      (TREE_TYPE (expr_type),
		       TREE_TYPE (TREE_TYPE (addr))));

	  expr = TREE_OPERAND (addr, 0);
	  expr_type = TREE_TYPE (expr);
	}

      /* We could also generate a NOP_EXPR(ADDR_EXPR()) when the
	 parameter is a pointer to object, through decay and
	 qualification conversion. Let's strip everything.  */
      else if (TYPE_PTROBV_P (type))
	{
	  STRIP_NOPS (expr);
	  gcc_assert (TREE_CODE (expr) == ADDR_EXPR);
	  gcc_assert (TREE_CODE (TREE_TYPE (expr)) == POINTER_TYPE);
	  /* Skip the ADDR_EXPR only if it is part of the decay for
	     an array. Otherwise, it is part of the original argument
	     in the source code.  */
	  if (TREE_CODE (TREE_TYPE (TREE_OPERAND (expr, 0))) == ARRAY_TYPE)
	    expr = TREE_OPERAND (expr, 0);
	  expr_type = TREE_TYPE (expr);
	}
    }

  /* [temp.arg.nontype]/5, bullet 1

     For a non-type template-parameter of integral or enumeration type,
     integral promotions (_conv.prom_) and integral conversions
     (_conv.integral_) are applied.  */
  if (INTEGRAL_TYPE_P (type))
    {
      if (!INTEGRAL_TYPE_P (expr_type))
	return error_mark_node;

      expr = fold_decl_constant_value (expr);
      /* Notice that there are constant expressions like '4 % 0' which
	 do not fold into integer constants.  */
      if (TREE_CODE (expr) != INTEGER_CST)
	{
	  error ("%qE is not a valid template argument for type %qT "
		 "because it is a non-constant expression", expr, type);
	  return NULL_TREE;
	}

      /* At this point, an implicit conversion does what we want,
	 because we already know that the expression is of integral
	 type.  */
      expr = ocp_convert (type, expr, CONV_IMPLICIT, LOOKUP_PROTECT);
      if (expr == error_mark_node)
	return error_mark_node;

      /* Conversion was allowed: fold it to a bare integer constant.  */
      expr = fold (expr);
    }
  /* [temp.arg.nontype]/5, bullet 2

     For a non-type template-parameter of type pointer to object,
     qualification conversions (_conv.qual_) and the array-to-pointer
     conversion (_conv.array_) are applied.  */
  else if (TYPE_PTROBV_P (type))
    {
      /* [temp.arg.nontype]/1  (TC1 version, DR 49):

	 A template-argument for a non-type, non-template template-parameter
	 shall be one of: [...]

	 -- the name of a non-type template-parameter;
	 -- the address of an object or function with external linkage, [...]
	    expressed as "& id-expression" where the & is optional if the name
	    refers to a function or array, or if the corresponding
	    template-parameter is a reference.

	Here, we do not care about functions, as they are invalid anyway
	for a parameter of type pointer-to-object.  */

      if (DECL_P (expr) && DECL_TEMPLATE_PARM_P (expr))
	/* Non-type template parameters are OK.  */
	;
      else if (TREE_CODE (expr) != ADDR_EXPR
	       && TREE_CODE (expr_type) != ARRAY_TYPE)
	{
	  if (TREE_CODE (expr) == VAR_DECL)
	    {
	      error ("%qD is not a valid template argument "
		     "because %qD is a variable, not the address of "
		     "a variable",
		     expr, expr);
	      return NULL_TREE;
	    }
	  /* Other values, like integer constants, might be valid
	     non-type arguments of some other type.  */
	  return error_mark_node;
	}
      else
	{
	  tree decl;

	  decl = ((TREE_CODE (expr) == ADDR_EXPR)
		  ? TREE_OPERAND (expr, 0) : expr);
	  if (TREE_CODE (decl) != VAR_DECL)
	    {
	      error ("%qE is not a valid template argument of type %qT "
		     "because %qE is not a variable",
		     expr, type, decl);
	      return NULL_TREE;
	    }
	  else if (!DECL_EXTERNAL_LINKAGE_P (decl))
	    {
	      error ("%qE is not a valid template argument of type %qT "
		     "because %qD does not have external linkage",
		     expr, type, decl);
	      return NULL_TREE;
	    }
	}

      expr = decay_conversion (expr);
      if (expr == error_mark_node)
	return error_mark_node;

      expr = perform_qualification_conversions (type, expr);
      if (expr == error_mark_node)
	return error_mark_node;
    }
  /* [temp.arg.nontype]/5, bullet 3

     For a non-type template-parameter of type reference to object, no
     conversions apply. The type referred to by the reference may be more
     cv-qualified than the (otherwise identical) type of the
     template-argument. The template-parameter is bound directly to the
     template-argument, which must be an lvalue.  */
  else if (TYPE_REF_OBJ_P (type))
    {
      if (!same_type_ignoring_top_level_qualifiers_p (TREE_TYPE (type),
						      expr_type))
	return error_mark_node;

      if (!at_least_as_qualified_p (TREE_TYPE (type), expr_type))
	{
	  error ("%qE is not a valid template argument for type %qT "
		 "because of conflicts in cv-qualification", expr, type);
	  return NULL_TREE;
	}

      if (!real_lvalue_p (expr))
	{
	  error ("%qE is not a valid template argument for type %qT "
		 "because it is not an lvalue", expr, type);
	  return NULL_TREE;
	}

      /* [temp.arg.nontype]/1

	 A template-argument for a non-type, non-template template-parameter
	 shall be one of: [...]

	 -- the address of an object or function with external linkage.  */
      if (!DECL_EXTERNAL_LINKAGE_P (expr))
	{
	  error ("%qE is not a valid template argument for type %qT "
		 "because object %qD has not external linkage",
		 expr, type, expr);
	  return NULL_TREE;
	}

      expr = build_nop (type, build_address (expr));
    }
  /* [temp.arg.nontype]/5, bullet 4

     For a non-type template-parameter of type pointer to function, only
     the function-to-pointer conversion (_conv.func_) is applied. If the
     template-argument represents a set of overloaded functions (or a
     pointer to such), the matching function is selected from the set
     (_over.over_).  */
  else if (TYPE_PTRFN_P (type))
    {
      /* If the argument is a template-id, we might not have enough
	 context information to decay the pointer.  */
      if (!type_unknown_p (expr_type))
	{
	  expr = decay_conversion (expr);
	  if (expr == error_mark_node)
	    return error_mark_node;
	}

      expr = convert_nontype_argument_function (type, expr);
      if (!expr || expr == error_mark_node)
	return expr;
    }
  /* [temp.arg.nontype]/5, bullet 5

     For a non-type template-parameter of type reference to function, no
     conversions apply. If the template-argument represents a set of
     overloaded functions, the matching function is selected from the set
     (_over.over_).  */
  else if (TYPE_REFFN_P (type))
    {
      if (TREE_CODE (expr) == ADDR_EXPR)
	{
	  error ("%qE is not a valid template argument for type %qT "
		 "because it is a pointer", expr, type);
	  inform ("try using %qE instead", TREE_OPERAND (expr, 0));
	  return NULL_TREE;
	}

      expr = convert_nontype_argument_function (TREE_TYPE (type), expr);
      if (!expr || expr == error_mark_node)
	return expr;

      expr = build_nop (type, build_address (expr));
    }
  /* [temp.arg.nontype]/5, bullet 6

     For a non-type template-parameter of type pointer to member function,
     no conversions apply. If the template-argument represents a set of
     overloaded member functions, the matching member function is selected
     from the set (_over.over_).  */
  else if (TYPE_PTRMEMFUNC_P (type))
    {
      expr = instantiate_type (type, expr, tf_none);
      if (expr == error_mark_node)
	return error_mark_node;

      /* There is no way to disable standard conversions in
	 resolve_address_of_overloaded_function (called by
	 instantiate_type). It is possible that the call succeeded by
	 converting &B::I to &D::I (where B is a base of D), so we need
	 to reject this conversion here.

	 Actually, even if there was a way to disable standard conversions,
	 it would still be better to reject them here so that we can
	 provide a superior diagnostic.  */
      if (!same_type_p (TREE_TYPE (expr), type))
	{
	  /* Make sure we are just one standard conversion off.  */
	  gcc_assert (can_convert (type, TREE_TYPE (expr)));
	  error ("%qE is not a valid template argument for type %qT "
		 "because it is of type %qT", expr, type,
		 TREE_TYPE (expr));
	  inform ("standard conversions are not allowed in this context");
	  return NULL_TREE;
	}
    }
  /* [temp.arg.nontype]/5, bullet 7

     For a non-type template-parameter of type pointer to data member,
     qualification conversions (_conv.qual_) are applied.  */
  else if (TYPE_PTRMEM_P (type))
    {
      expr = perform_qualification_conversions (type, expr);
      if (expr == error_mark_node)
	return expr;
    }
  /* A template non-type parameter must be one of the above.  */
  else
    gcc_unreachable ();

  /* Sanity check: did we actually convert the argument to the
     right type?  */
  gcc_assert (same_type_p (type, TREE_TYPE (expr)));
  return expr;
}


/* Return 1 if PARM_PARMS and ARG_PARMS matches using rule for
   template template parameters.  Both PARM_PARMS and ARG_PARMS are
   vectors of TREE_LIST nodes containing TYPE_DECL, TEMPLATE_DECL
   or PARM_DECL.

   Consider the example:
     template <class T> class A;
     template<template <class U> class TT> class B;

   For B<A>, PARM_PARMS are the parameters to TT, while ARG_PARMS are
   the parameters to A, and OUTER_ARGS contains A.  */

static int
coerce_template_template_parms (tree parm_parms,
				tree arg_parms,
				tsubst_flags_t complain,
				tree in_decl,
				tree outer_args)
{
  int nparms, nargs, i;
  tree parm, arg;

  gcc_assert (TREE_CODE (parm_parms) == TREE_VEC);
  gcc_assert (TREE_CODE (arg_parms) == TREE_VEC);

  nparms = TREE_VEC_LENGTH (parm_parms);
  nargs = TREE_VEC_LENGTH (arg_parms);

  if (nargs != nparms)
    return 0;

  for (i = 0; i < nparms; ++i)
    {
      if (TREE_VEC_ELT (parm_parms, i) == error_mark_node
          || TREE_VEC_ELT (arg_parms, i) == error_mark_node)
        continue;

      parm = TREE_VALUE (TREE_VEC_ELT (parm_parms, i));
      arg = TREE_VALUE (TREE_VEC_ELT (arg_parms, i));

      if (arg == NULL_TREE || arg == error_mark_node
	  || parm == NULL_TREE || parm == error_mark_node)
	return 0;

      if (TREE_CODE (arg) != TREE_CODE (parm))
	return 0;

      switch (TREE_CODE (parm))
	{
	case TYPE_DECL:
	  break;

	case TEMPLATE_DECL:
	  /* We encounter instantiations of templates like
	       template <template <template <class> class> class TT>
	       class C;  */
	  {
	    tree parmparm = DECL_INNERMOST_TEMPLATE_PARMS (parm);
	    tree argparm = DECL_INNERMOST_TEMPLATE_PARMS (arg);

	    if (!coerce_template_template_parms
		(parmparm, argparm, complain, in_decl, outer_args))
	      return 0;
	  }
	  break;

	case PARM_DECL:
	  /* The tsubst call is used to handle cases such as

	       template <int> class C {};
	       template <class T, template <T> class TT> class D {};
	       D<int, C> d;

	     i.e. the parameter list of TT depends on earlier parameters.  */
	  if (!dependent_type_p (TREE_TYPE (arg))
	      && !same_type_p
		    (tsubst (TREE_TYPE (parm), outer_args, complain, in_decl),
			     TREE_TYPE (arg)))
	    return 0;
	  break;

	default:
	  gcc_unreachable ();
	}
    }
  return 1;
}

/* Convert the indicated template ARG as necessary to match the
   indicated template PARM.  Returns the converted ARG, or
   error_mark_node if the conversion was unsuccessful.  Error and
   warning messages are issued under control of COMPLAIN.  This
   conversion is for the Ith parameter in the parameter list.  ARGS is
   the full set of template arguments deduced so far.  */

static tree
convert_template_argument (tree parm,
			   tree arg,
			   tree args,
			   tsubst_flags_t complain,
			   int i,
			   tree in_decl)
{
  tree val;
  int is_type, requires_type, is_tmpl_type, requires_tmpl_type;

  if (TREE_CODE (arg) == TREE_LIST
      && TREE_CODE (TREE_VALUE (arg)) == OFFSET_REF)
    {
      /* The template argument was the name of some
	 member function.  That's usually
	 invalid, but static members are OK.  In any
	 case, grab the underlying fields/functions
	 and issue an error later if required.  */
      arg = TREE_VALUE (arg);
      TREE_TYPE (arg) = unknown_type_node;
    }

  requires_tmpl_type = TREE_CODE (parm) == TEMPLATE_DECL;
  requires_type = (TREE_CODE (parm) == TYPE_DECL
		   || requires_tmpl_type);

  is_tmpl_type = ((TREE_CODE (arg) == TEMPLATE_DECL
		   && TREE_CODE (DECL_TEMPLATE_RESULT (arg)) == TYPE_DECL)
		  || TREE_CODE (arg) == TEMPLATE_TEMPLATE_PARM
		  || TREE_CODE (arg) == UNBOUND_CLASS_TEMPLATE);

  if (is_tmpl_type
      && (TREE_CODE (arg) == TEMPLATE_TEMPLATE_PARM
	  || TREE_CODE (arg) == UNBOUND_CLASS_TEMPLATE))
    arg = TYPE_STUB_DECL (arg);

  is_type = TYPE_P (arg) || is_tmpl_type;

  if (requires_type && ! is_type && TREE_CODE (arg) == SCOPE_REF
      && TREE_CODE (TREE_OPERAND (arg, 0)) == TEMPLATE_TYPE_PARM)
    {
      pedwarn ("to refer to a type member of a template parameter, "
	       "use %<typename %E%>", arg);

      arg = make_typename_type (TREE_OPERAND (arg, 0),
				TREE_OPERAND (arg, 1),
				typename_type,
				complain & tf_error);
      is_type = 1;
    }
  if (is_type != requires_type)
    {
      if (in_decl)
	{
	  if (complain & tf_error)
	    {
	      error ("type/value mismatch at argument %d in template "
		     "parameter list for %qD",
		     i + 1, in_decl);
	      if (is_type)
		error ("  expected a constant of type %qT, got %qT",
		       TREE_TYPE (parm),
		       (is_tmpl_type ? DECL_NAME (arg) : arg));
	      else if (requires_tmpl_type)
		error ("  expected a class template, got %qE", arg);
	      else
		error ("  expected a type, got %qE", arg);
	    }
	}
      return error_mark_node;
    }
  if (is_tmpl_type ^ requires_tmpl_type)
    {
      if (in_decl && (complain & tf_error))
	{
	  error ("type/value mismatch at argument %d in template "
		 "parameter list for %qD",
		 i + 1, in_decl);
	  if (is_tmpl_type)
	    error ("  expected a type, got %qT", DECL_NAME (arg));
	  else
	    error ("  expected a class template, got %qT", arg);
	}
      return error_mark_node;
    }

  if (is_type)
    {
      if (requires_tmpl_type)
	{
	  if (TREE_CODE (TREE_TYPE (arg)) == UNBOUND_CLASS_TEMPLATE)
	    /* The number of argument required is not known yet.
	       Just accept it for now.  */
	    val = TREE_TYPE (arg);
	  else
	    {
	      tree parmparm = DECL_INNERMOST_TEMPLATE_PARMS (parm);
	      tree argparm = DECL_INNERMOST_TEMPLATE_PARMS (arg);

	      if (coerce_template_template_parms (parmparm, argparm,
						  complain, in_decl,
						  args))
		{
		  val = arg;

		  /* TEMPLATE_TEMPLATE_PARM node is preferred over
		     TEMPLATE_DECL.  */
		  if (val != error_mark_node
		      && DECL_TEMPLATE_TEMPLATE_PARM_P (val))
		    val = TREE_TYPE (val);
		}
	      else
		{
		  if (in_decl && (complain & tf_error))
		    {
		      error ("type/value mismatch at argument %d in "
			     "template parameter list for %qD",
			     i + 1, in_decl);
		      error ("  expected a template of type %qD, got %qD",
			     parm, arg);
		    }

		  val = error_mark_node;
		}
	    }
	}
      else
	val = arg;
      /* We only form one instance of each template specialization.
	 Therefore, if we use a non-canonical variant (i.e., a
	 typedef), any future messages referring to the type will use
	 the typedef, which is confusing if those future uses do not
	 themselves also use the typedef.  */
      if (TYPE_P (val))
	val = canonical_type_variant (val);
    }
  else
    {
      tree t = tsubst (TREE_TYPE (parm), args, complain, in_decl);

      if (invalid_nontype_parm_type_p (t, complain))
	return error_mark_node;

      if (!uses_template_parms (arg) && !uses_template_parms (t))
	/* We used to call digest_init here.  However, digest_init
	   will report errors, which we don't want when complain
	   is zero.  More importantly, digest_init will try too
	   hard to convert things: for example, `0' should not be
	   converted to pointer type at this point according to
	   the standard.  Accepting this is not merely an
	   extension, since deciding whether or not these
	   conversions can occur is part of determining which
	   function template to call, or whether a given explicit
	   argument specification is valid.  */
	val = convert_nontype_argument (t, arg);
      else
	val = arg;

      if (val == NULL_TREE)
	val = error_mark_node;
      else if (val == error_mark_node && (complain & tf_error))
	error ("could not convert template argument %qE to %qT",  arg, t);
    }

  return val;
}

/* Convert all template arguments to their appropriate types, and
   return a vector containing the innermost resulting template
   arguments.  If any error occurs, return error_mark_node. Error and
   warning messages are issued under control of COMPLAIN.

   If REQUIRE_ALL_ARGS is false, argument deduction will be performed
   for arguments not specified in ARGS.  Otherwise, if
   USE_DEFAULT_ARGS is true, default arguments will be used to fill in
   unspecified arguments.  If REQUIRE_ALL_ARGS is true, but
   USE_DEFAULT_ARGS is false, then all arguments must be specified in
   ARGS.  */

static tree
coerce_template_parms (tree parms,
		       tree args,
		       tree in_decl,
		       tsubst_flags_t complain,
		       bool require_all_args,
		       bool use_default_args)
{
  int nparms, nargs, i, lost = 0;
  tree inner_args;
  tree new_args;
  tree new_inner_args;
  bool saved_skip_evaluation;

  inner_args = INNERMOST_TEMPLATE_ARGS (args);
  nargs = inner_args ? NUM_TMPL_ARGS (inner_args) : 0;
  nparms = TREE_VEC_LENGTH (parms);

  if (nargs > nparms
      || (nargs < nparms
	  && require_all_args
	  && (!use_default_args
	      || (TREE_VEC_ELT (parms, nargs) != error_mark_node
                  && !TREE_PURPOSE (TREE_VEC_ELT (parms, nargs))))))
    {
      if (complain & tf_error)
	{
	  error ("wrong number of template arguments (%d, should be %d)",
		 nargs, nparms);

	  if (in_decl)
	    error ("provided for %q+D", in_decl);
	}

      return error_mark_node;
    }

  /* We need to evaluate the template arguments, even though this
     template-id may be nested within a "sizeof".  */
  saved_skip_evaluation = skip_evaluation;
  skip_evaluation = false;
  new_inner_args = make_tree_vec (nparms);
  new_args = add_outermost_template_args (args, new_inner_args);
  for (i = 0; i < nparms; i++)
    {
      tree arg;
      tree parm;

      /* Get the Ith template parameter.  */
      parm = TREE_VEC_ELT (parms, i);
 
      if (parm == error_mark_node)
      {
        TREE_VEC_ELT (new_inner_args, i) = error_mark_node;
        continue;
      }

      /* Calculate the Ith argument.  */
      if (i < nargs)
	arg = TREE_VEC_ELT (inner_args, i);
      else if (require_all_args)
	/* There must be a default arg in this case.  */
	arg = tsubst_template_arg (TREE_PURPOSE (parm), new_args,
				   complain, in_decl);
      else
	break;

      gcc_assert (arg);
      if (arg == error_mark_node)
	{
	  if (complain & tf_error)
	    error ("template argument %d is invalid", i + 1);
	}
      else
	arg = convert_template_argument (TREE_VALUE (parm),
					 arg, new_args, complain, i,
					 in_decl);

      if (arg == error_mark_node)
	lost++;
      TREE_VEC_ELT (new_inner_args, i) = arg;
    }
  skip_evaluation = saved_skip_evaluation;

  if (lost)
    return error_mark_node;

  return new_inner_args;
}

/* Returns 1 if template args OT and NT are equivalent.  */

static int
template_args_equal (tree ot, tree nt)
{
  if (nt == ot)
    return 1;

  if (TREE_CODE (nt) == TREE_VEC)
    /* For member templates */
    return TREE_CODE (ot) == TREE_VEC && comp_template_args (ot, nt);
  else if (TYPE_P (nt))
    return TYPE_P (ot) && same_type_p (ot, nt);
  else if (TREE_CODE (ot) == TREE_VEC || TYPE_P (ot))
    return 0;
  else
    return cp_tree_equal (ot, nt);
}

/* Returns 1 iff the OLDARGS and NEWARGS are in fact identical sets
   of template arguments.  Returns 0 otherwise.  */

int
comp_template_args (tree oldargs, tree newargs)
{
  int i;

  if (TREE_VEC_LENGTH (oldargs) != TREE_VEC_LENGTH (newargs))
    return 0;

  for (i = 0; i < TREE_VEC_LENGTH (oldargs); ++i)
    {
      tree nt = TREE_VEC_ELT (newargs, i);
      tree ot = TREE_VEC_ELT (oldargs, i);

      if (! template_args_equal (ot, nt))
	return 0;
    }
  return 1;
}

/* Given class template name and parameter list, produce a user-friendly name
   for the instantiation.  */

static char *
mangle_class_name_for_template (const char* name, tree parms, tree arglist)
{
  static struct obstack scratch_obstack;
  static char *scratch_firstobj;
  int i, nparms;

  if (!scratch_firstobj)
    gcc_obstack_init (&scratch_obstack);
  else
    obstack_free (&scratch_obstack, scratch_firstobj);
  scratch_firstobj = (char *) obstack_alloc (&scratch_obstack, 1);

#define ccat(C)	obstack_1grow (&scratch_obstack, (C));
#define cat(S)	obstack_grow (&scratch_obstack, (S), strlen (S))

  cat (name);
  ccat ('<');
  nparms = TREE_VEC_LENGTH (parms);
  arglist = INNERMOST_TEMPLATE_ARGS (arglist);
  gcc_assert (nparms == TREE_VEC_LENGTH (arglist));
  for (i = 0; i < nparms; i++)
    {
      tree parm;
      tree arg;

      parm = TREE_VALUE (TREE_VEC_ELT (parms, i));
      arg = TREE_VEC_ELT (arglist, i);

      if (parm == error_mark_node)
	continue;

      if (i)
	ccat (',');

      if (TREE_CODE (parm) == TYPE_DECL)
	{
	  cat (type_as_string (arg, TFF_CHASE_TYPEDEF));
	  continue;
	}
      else if (TREE_CODE (parm) == TEMPLATE_DECL)
	{
	  if (TREE_CODE (arg) == TEMPLATE_DECL)
	    {
	      /* Already substituted with real template.  Just output
		 the template name here */
	      tree context = DECL_CONTEXT (arg);
	      if (context)
		{
		  /* The template may be defined in a namespace, or
		     may be a member template.  */
		  gcc_assert (TREE_CODE (context) == NAMESPACE_DECL
			      || CLASS_TYPE_P (context));
		  cat (decl_as_string (DECL_CONTEXT (arg),
				      TFF_PLAIN_IDENTIFIER));
		  cat ("::");
		}
	      cat (IDENTIFIER_POINTER (DECL_NAME (arg)));
	    }
	  else
	    /* Output the parameter declaration.  */
	    cat (type_as_string (arg, TFF_CHASE_TYPEDEF));
	  continue;
	}
      else
	gcc_assert (TREE_CODE (parm) == PARM_DECL);

      /* No need to check arglist against parmlist here; we did that
	 in coerce_template_parms, called from lookup_template_class.  */
      cat (expr_as_string (arg, TFF_PLAIN_IDENTIFIER));
    }
  {
    char *bufp = obstack_next_free (&scratch_obstack);
    int offset = 0;
    while (bufp[offset - 1] == ' ')
      offset--;
    obstack_blank_fast (&scratch_obstack, offset);

    /* B<C<char> >, not B<C<char>> */
    if (bufp[offset - 1] == '>')
      ccat (' ');
  }
  ccat ('>');
  ccat ('\0');
  return (char *) obstack_base (&scratch_obstack);
}

static tree
classtype_mangled_name (tree t)
{
  if (CLASSTYPE_TEMPLATE_INFO (t)
      /* Specializations have already had their names set up in
	 lookup_template_class.  */
      && !CLASSTYPE_TEMPLATE_SPECIALIZATION (t))
    {
      tree tmpl = most_general_template (CLASSTYPE_TI_TEMPLATE (t));

      /* For non-primary templates, the template parameters are
	 implicit from their surrounding context.  */
      if (PRIMARY_TEMPLATE_P (tmpl))
	{
	  tree name = DECL_NAME (tmpl);
	  char *mangled_name = mangle_class_name_for_template
	    (IDENTIFIER_POINTER (name),
	     DECL_INNERMOST_TEMPLATE_PARMS (tmpl),
	     CLASSTYPE_TI_ARGS (t));
	  tree id = get_identifier (mangled_name);
	  IDENTIFIER_TEMPLATE (id) = name;
	  return id;
	}
    }

  return TYPE_IDENTIFIER (t);
}

static void
add_pending_template (tree d)
{
  tree ti = (TYPE_P (d)
	     ? CLASSTYPE_TEMPLATE_INFO (d)
	     : DECL_TEMPLATE_INFO (d));
  tree pt;
  int level;

  if (TI_PENDING_TEMPLATE_FLAG (ti))
    return;

  /* We are called both from instantiate_decl, where we've already had a
     tinst_level pushed, and instantiate_template, where we haven't.
     Compensate.  */
  level = !(current_tinst_level && TINST_DECL (current_tinst_level) == d);

  if (level)
    push_tinst_level (d);

  pt = tree_cons (current_tinst_level, d, NULL_TREE);
  if (last_pending_template)
    TREE_CHAIN (last_pending_template) = pt;
  else
    pending_templates = pt;

  last_pending_template = pt;

  TI_PENDING_TEMPLATE_FLAG (ti) = 1;

  if (level)
    pop_tinst_level ();
}


/* Return a TEMPLATE_ID_EXPR corresponding to the indicated FNS and
   ARGLIST.  Valid choices for FNS are given in the cp-tree.def
   documentation for TEMPLATE_ID_EXPR.  */

tree
lookup_template_function (tree fns, tree arglist)
{
  tree type;

  if (fns == error_mark_node || arglist == error_mark_node)
    return error_mark_node;

  gcc_assert (!arglist || TREE_CODE (arglist) == TREE_VEC);
  gcc_assert (fns && (is_overloaded_fn (fns)
		      || TREE_CODE (fns) == IDENTIFIER_NODE));

  if (BASELINK_P (fns))
    {
      BASELINK_FUNCTIONS (fns) = build2 (TEMPLATE_ID_EXPR,
					 unknown_type_node,
					 BASELINK_FUNCTIONS (fns),
					 arglist);
      return fns;
    }

  type = TREE_TYPE (fns);
  if (TREE_CODE (fns) == OVERLOAD || !type)
    type = unknown_type_node;

  return build2 (TEMPLATE_ID_EXPR, type, fns, arglist);
}

/* Within the scope of a template class S<T>, the name S gets bound
   (in build_self_reference) to a TYPE_DECL for the class, not a
   TEMPLATE_DECL.  If DECL is a TYPE_DECL for current_class_type,
   or one of its enclosing classes, and that type is a template,
   return the associated TEMPLATE_DECL.  Otherwise, the original
   DECL is returned.  */

tree
maybe_get_template_decl_from_type_decl (tree decl)
{
  return (decl != NULL_TREE
	  && TREE_CODE (decl) == TYPE_DECL
	  && DECL_ARTIFICIAL (decl)
	  && CLASS_TYPE_P (TREE_TYPE (decl))
	  && CLASSTYPE_TEMPLATE_INFO (TREE_TYPE (decl)))
    ? CLASSTYPE_TI_TEMPLATE (TREE_TYPE (decl)) : decl;
}

/* Given an IDENTIFIER_NODE (type TEMPLATE_DECL) and a chain of
   parameters, find the desired type.

   D1 is the PTYPENAME terminal, and ARGLIST is the list of arguments.

   IN_DECL, if non-NULL, is the template declaration we are trying to
   instantiate.

   If ENTERING_SCOPE is nonzero, we are about to enter the scope of
   the class we are looking up.

   Issue error and warning messages under control of COMPLAIN.

   If the template class is really a local class in a template
   function, then the FUNCTION_CONTEXT is the function in which it is
   being instantiated.

   ??? Note that this function is currently called *twice* for each
   template-id: the first time from the parser, while creating the
   incomplete type (finish_template_type), and the second type during the
   real instantiation (instantiate_template_class). This is surely something
   that we want to avoid. It also causes some problems with argument
   coercion (see convert_nontype_argument for more information on this).  */

tree
lookup_template_class (tree d1,
		       tree arglist,
		       tree in_decl,
		       tree context,
		       int entering_scope,
		       tsubst_flags_t complain)
{
  tree template = NULL_TREE, parmlist;
  tree t;

  timevar_push (TV_NAME_LOOKUP);

  if (TREE_CODE (d1) == IDENTIFIER_NODE)
    {
      tree value = innermost_non_namespace_value (d1);
      if (value && DECL_TEMPLATE_TEMPLATE_PARM_P (value))
	template = value;
      else
	{
	  if (context)
	    push_decl_namespace (context);
	  template = lookup_name (d1);
	  template = maybe_get_template_decl_from_type_decl (template);
	  if (context)
	    pop_decl_namespace ();
	}
      if (template)
	context = DECL_CONTEXT (template);
    }
  else if (TREE_CODE (d1) == TYPE_DECL && IS_AGGR_TYPE (TREE_TYPE (d1)))
    {
      tree type = TREE_TYPE (d1);

      /* If we are declaring a constructor, say A<T>::A<T>, we will get
	 an implicit typename for the second A.  Deal with it.  */
      if (TREE_CODE (type) == TYPENAME_TYPE && TREE_TYPE (type))
	type = TREE_TYPE (type);

      if (CLASSTYPE_TEMPLATE_INFO (type))
	{
	  template = CLASSTYPE_TI_TEMPLATE (type);
	  d1 = DECL_NAME (template);
	}
    }
  else if (TREE_CODE (d1) == ENUMERAL_TYPE
	   || (TYPE_P (d1) && IS_AGGR_TYPE (d1)))
    {
      template = TYPE_TI_TEMPLATE (d1);
      d1 = DECL_NAME (template);
    }
  else if (TREE_CODE (d1) == TEMPLATE_DECL
	   && TREE_CODE (DECL_TEMPLATE_RESULT (d1)) == TYPE_DECL)
    {
      template = d1;
      d1 = DECL_NAME (template);
      context = DECL_CONTEXT (template);
    }

  /* Issue an error message if we didn't find a template.  */
  if (! template)
    {
      if (complain & tf_error)
	error ("%qT is not a template", d1);
      POP_TIMEVAR_AND_RETURN (TV_NAME_LOOKUP, error_mark_node);
    }

  if (TREE_CODE (template) != TEMPLATE_DECL
	 /* Make sure it's a user visible template, if it was named by
	    the user.  */
      || ((complain & tf_user) && !DECL_TEMPLATE_PARM_P (template)
	  && !PRIMARY_TEMPLATE_P (template)))
    {
      if (complain & tf_error)
	{
	  error ("non-template type %qT used as a template", d1);
	  if (in_decl)
	    error ("for template declaration %q+D", in_decl);
	}
      POP_TIMEVAR_AND_RETURN (TV_NAME_LOOKUP, error_mark_node);
    }

  complain &= ~tf_user;

  if (DECL_TEMPLATE_TEMPLATE_PARM_P (template))
    {
      /* Create a new TEMPLATE_DECL and TEMPLATE_TEMPLATE_PARM node to store
	 template arguments */

      tree parm;
      tree arglist2;

      parmlist = DECL_INNERMOST_TEMPLATE_PARMS (template);

      /* Consider an example where a template template parameter declared as

	   template <class T, class U = std::allocator<T> > class TT

	 The template parameter level of T and U are one level larger than
	 of TT.  To proper process the default argument of U, say when an
	 instantiation `TT<int>' is seen, we need to build the full
	 arguments containing {int} as the innermost level.  Outer levels,
	 available when not appearing as default template argument, can be
	 obtained from `current_template_args ()'.

	 Suppose that TT is later substituted with std::vector.  The above
	 instantiation is `TT<int, std::allocator<T> >' with TT at
	 level 1, and T at level 2, while the template arguments at level 1
	 becomes {std::vector} and the inner level 2 is {int}.  */

      if (current_template_parms)
	arglist = add_to_template_args (current_template_args (), arglist);

      arglist2 = coerce_template_parms (parmlist, arglist, template,
					complain,
					/*require_all_args=*/true,
					/*use_default_args=*/true);
      if (arglist2 == error_mark_node
	  || (!uses_template_parms (arglist2)
	      && check_instantiated_args (template, arglist2, complain)))
	POP_TIMEVAR_AND_RETURN (TV_NAME_LOOKUP, error_mark_node);

      parm = bind_template_template_parm (TREE_TYPE (template), arglist2);
      POP_TIMEVAR_AND_RETURN (TV_NAME_LOOKUP, parm);
    }
  else
    {
      tree template_type = TREE_TYPE (template);
      tree gen_tmpl;
      tree type_decl;
      tree found = NULL_TREE;
      int arg_depth;
      int parm_depth;
      int is_partial_instantiation;

      gen_tmpl = most_general_template (template);
      parmlist = DECL_TEMPLATE_PARMS (gen_tmpl);
      parm_depth = TMPL_PARMS_DEPTH (parmlist);
      arg_depth = TMPL_ARGS_DEPTH (arglist);

      if (arg_depth == 1 && parm_depth > 1)
	{
	  /* We've been given an incomplete set of template arguments.
	     For example, given:

	       template <class T> struct S1 {
		 template <class U> struct S2 {};
		 template <class U> struct S2<U*> {};
		};

	     we will be called with an ARGLIST of `U*', but the
	     TEMPLATE will be `template <class T> template
	     <class U> struct S1<T>::S2'.  We must fill in the missing
	     arguments.  */
	  arglist
	    = add_outermost_template_args (TYPE_TI_ARGS (TREE_TYPE (template)),
					   arglist);
	  arg_depth = TMPL_ARGS_DEPTH (arglist);
	}

      /* Now we should have enough arguments.  */
      gcc_assert (parm_depth == arg_depth);

      /* From here on, we're only interested in the most general
	 template.  */
      template = gen_tmpl;

      /* Calculate the BOUND_ARGS.  These will be the args that are
	 actually tsubst'd into the definition to create the
	 instantiation.  */
      if (parm_depth > 1)
	{
	  /* We have multiple levels of arguments to coerce, at once.  */
	  int i;
	  int saved_depth = TMPL_ARGS_DEPTH (arglist);

	  tree bound_args = make_tree_vec (parm_depth);

	  for (i = saved_depth,
		 t = DECL_TEMPLATE_PARMS (template);
	       i > 0 && t != NULL_TREE;
	       --i, t = TREE_CHAIN (t))
	    {
	      tree a = coerce_template_parms (TREE_VALUE (t),
					      arglist, template,
					      complain,
					      /*require_all_args=*/true,
					      /*use_default_args=*/true);

	      /* Don't process further if one of the levels fails.  */
	      if (a == error_mark_node)
		{
		  /* Restore the ARGLIST to its full size.  */
		  TREE_VEC_LENGTH (arglist) = saved_depth;
		  POP_TIMEVAR_AND_RETURN (TV_NAME_LOOKUP, error_mark_node);
		}

	      SET_TMPL_ARGS_LEVEL (bound_args, i, a);

	      /* We temporarily reduce the length of the ARGLIST so
		 that coerce_template_parms will see only the arguments
		 corresponding to the template parameters it is
		 examining.  */
	      TREE_VEC_LENGTH (arglist)--;
	    }

	  /* Restore the ARGLIST to its full size.  */
	  TREE_VEC_LENGTH (arglist) = saved_depth;

	  arglist = bound_args;
	}
      else
	arglist
	  = coerce_template_parms (INNERMOST_TEMPLATE_PARMS (parmlist),
				   INNERMOST_TEMPLATE_ARGS (arglist),
				   template,
				   complain,
				   /*require_all_args=*/true,
				   /*use_default_args=*/true);

      if (arglist == error_mark_node)
	/* We were unable to bind the arguments.  */
	POP_TIMEVAR_AND_RETURN (TV_NAME_LOOKUP, error_mark_node);

      /* In the scope of a template class, explicit references to the
	 template class refer to the type of the template, not any
	 instantiation of it.  For example, in:

	   template <class T> class C { void f(C<T>); }

	 the `C<T>' is just the same as `C'.  Outside of the
	 class, however, such a reference is an instantiation.  */
      if (comp_template_args (TYPE_TI_ARGS (template_type),
			      arglist))
	{
	  found = template_type;

	  if (!entering_scope && PRIMARY_TEMPLATE_P (template))
	    {
	      tree ctx;

	      for (ctx = current_class_type;
		   ctx && TREE_CODE (ctx) != NAMESPACE_DECL;
		   ctx = (TYPE_P (ctx)
			  ? TYPE_CONTEXT (ctx)
			  : DECL_CONTEXT (ctx)))
		if (TYPE_P (ctx) && same_type_p (ctx, template_type))
		  goto found_ctx;

	      /* We're not in the scope of the class, so the
		 TEMPLATE_TYPE is not the type we want after all.  */
	      found = NULL_TREE;
	    found_ctx:;
	    }
	}
      if (found)
	POP_TIMEVAR_AND_RETURN (TV_NAME_LOOKUP, found);

      /* If we already have this specialization, return it.  */
      found = retrieve_specialization (template, arglist,
				       /*class_specializations_p=*/false);
      if (found)
	POP_TIMEVAR_AND_RETURN (TV_NAME_LOOKUP, found);

      /* This type is a "partial instantiation" if any of the template
	 arguments still involve template parameters.  Note that we set
	 IS_PARTIAL_INSTANTIATION for partial specializations as
	 well.  */
      is_partial_instantiation = uses_template_parms (arglist);

      /* If the deduced arguments are invalid, then the binding
	 failed.  */
      if (!is_partial_instantiation
	  && check_instantiated_args (template,
				      INNERMOST_TEMPLATE_ARGS (arglist),
				      complain))
	POP_TIMEVAR_AND_RETURN (TV_NAME_LOOKUP, error_mark_node);

      if (!is_partial_instantiation
	  && !PRIMARY_TEMPLATE_P (template)
	  && TREE_CODE (CP_DECL_CONTEXT (template)) == NAMESPACE_DECL)
	{
	  found = xref_tag_from_type (TREE_TYPE (template),
				      DECL_NAME (template),
				      /*tag_scope=*/ts_global);
	  POP_TIMEVAR_AND_RETURN (TV_NAME_LOOKUP, found);
	}

      context = tsubst (DECL_CONTEXT (template), arglist,
			complain, in_decl);
      if (!context)
	context = global_namespace;

      /* Create the type.  */
      if (TREE_CODE (template_type) == ENUMERAL_TYPE)
	{
	  if (!is_partial_instantiation)
	    {
	      set_current_access_from_decl (TYPE_NAME (template_type));
	      t = start_enum (TYPE_IDENTIFIER (template_type));
	    }
	  else
	    /* We don't want to call start_enum for this type, since
	       the values for the enumeration constants may involve
	       template parameters.  And, no one should be interested
	       in the enumeration constants for such a type.  */
	    t = make_node (ENUMERAL_TYPE);
	}
      else
	{
	  t = make_aggr_type (TREE_CODE (template_type));
	  CLASSTYPE_DECLARED_CLASS (t)
	    = CLASSTYPE_DECLARED_CLASS (template_type);
	  SET_CLASSTYPE_IMPLICIT_INSTANTIATION (t);
	  TYPE_FOR_JAVA (t) = TYPE_FOR_JAVA (template_type);

	  /* A local class.  Make sure the decl gets registered properly.  */
	  if (context == current_function_decl)
	    pushtag (DECL_NAME (template), t, /*tag_scope=*/ts_current);
	}

      /* If we called start_enum or pushtag above, this information
	 will already be set up.  */
      if (!TYPE_NAME (t))
	{
	  TYPE_CONTEXT (t) = FROB_CONTEXT (context);

	  type_decl = create_implicit_typedef (DECL_NAME (template), t);
	  DECL_CONTEXT (type_decl) = TYPE_CONTEXT (t);
	  TYPE_STUB_DECL (t) = type_decl;
	  DECL_SOURCE_LOCATION (type_decl)
	    = DECL_SOURCE_LOCATION (TYPE_STUB_DECL (template_type));
	}
      else
	type_decl = TYPE_NAME (t);

      TREE_PRIVATE (type_decl)
	= TREE_PRIVATE (TYPE_STUB_DECL (template_type));
      TREE_PROTECTED (type_decl)
	= TREE_PROTECTED (TYPE_STUB_DECL (template_type));
      DECL_IN_SYSTEM_HEADER (type_decl)
	= DECL_IN_SYSTEM_HEADER (template);
      if (CLASSTYPE_VISIBILITY_SPECIFIED (template_type))
	{
	  DECL_VISIBILITY_SPECIFIED (type_decl) = 1;
	  DECL_VISIBILITY (type_decl) = CLASSTYPE_VISIBILITY (template_type);
	}

      /* Set up the template information.  We have to figure out which
	 template is the immediate parent if this is a full
	 instantiation.  */
      if (parm_depth == 1 || is_partial_instantiation
	  || !PRIMARY_TEMPLATE_P (template))
	/* This case is easy; there are no member templates involved.  */
	found = template;
      else
	{
	  /* This is a full instantiation of a member template.  Look
	     for a partial instantiation of which this is an instance.  */

	  for (found = DECL_TEMPLATE_INSTANTIATIONS (template);
	       found; found = TREE_CHAIN (found))
	    {
	      int success;
	      tree tmpl = CLASSTYPE_TI_TEMPLATE (TREE_VALUE (found));

	      /* We only want partial instantiations, here, not
		 specializations or full instantiations.  */
	      if (CLASSTYPE_TEMPLATE_SPECIALIZATION (TREE_VALUE (found))
		  || !uses_template_parms (TREE_VALUE (found)))
		continue;

	      /* Temporarily reduce by one the number of levels in the
		 ARGLIST and in FOUND so as to avoid comparing the
		 last set of arguments.  */
	      TREE_VEC_LENGTH (arglist)--;
	      TREE_VEC_LENGTH (TREE_PURPOSE (found)) --;

	      /* See if the arguments match.  If they do, then TMPL is
		 the partial instantiation we want.  */
	      success = comp_template_args (TREE_PURPOSE (found), arglist);

	      /* Restore the argument vectors to their full size.  */
	      TREE_VEC_LENGTH (arglist)++;
	      TREE_VEC_LENGTH (TREE_PURPOSE (found))++;

	      if (success)
		{
		  found = tmpl;
		  break;
		}
	    }

	  if (!found)
	    {
	      /* There was no partial instantiation. This happens
		 where C<T> is a member template of A<T> and it's used
		 in something like

		  template <typename T> struct B { A<T>::C<int> m; };
		  B<float>;

		 Create the partial instantiation.
	       */
	      TREE_VEC_LENGTH (arglist)--;
	      found = tsubst (template, arglist, complain, NULL_TREE);
	      TREE_VEC_LENGTH (arglist)++;
	    }
	}

      SET_TYPE_TEMPLATE_INFO (t, tree_cons (found, arglist, NULL_TREE));
      DECL_TEMPLATE_INSTANTIATIONS (template)
	= tree_cons (arglist, t,
		     DECL_TEMPLATE_INSTANTIATIONS (template));

      if (TREE_CODE (t) == ENUMERAL_TYPE
	  && !is_partial_instantiation)
	/* Now that the type has been registered on the instantiations
	   list, we set up the enumerators.  Because the enumeration
	   constants may involve the enumeration type itself, we make
	   sure to register the type first, and then create the
	   constants.  That way, doing tsubst_expr for the enumeration
	   constants won't result in recursive calls here; we'll find
	   the instantiation and exit above.  */
	tsubst_enum (template_type, t, arglist);

      /* Reset the name of the type, now that CLASSTYPE_TEMPLATE_INFO
	 is set up.  */
      if (TREE_CODE (t) != ENUMERAL_TYPE)
	DECL_NAME (type_decl) = classtype_mangled_name (t);
      if (is_partial_instantiation)
	/* If the type makes use of template parameters, the
	   code that generates debugging information will crash.  */
	DECL_IGNORED_P (TYPE_STUB_DECL (t)) = 1;

      /* Possibly limit visibility based on template args.  */
      TREE_PUBLIC (type_decl) = 1;
      determine_visibility (type_decl);

      POP_TIMEVAR_AND_RETURN (TV_NAME_LOOKUP, t);
    }
  timevar_pop (TV_NAME_LOOKUP);
}

struct pair_fn_data
{
  tree_fn_t fn;
  void *data;
  struct pointer_set_t *visited;
};

/* Called from for_each_template_parm via walk_tree.  */

static tree
for_each_template_parm_r (tree *tp, int *walk_subtrees, void *d)
{
  tree t = *tp;
  struct pair_fn_data *pfd = (struct pair_fn_data *) d;
  tree_fn_t fn = pfd->fn;
  void *data = pfd->data;

  if (TYPE_P (t)
      && for_each_template_parm (TYPE_CONTEXT (t), fn, data, pfd->visited))
    return error_mark_node;

  switch (TREE_CODE (t))
    {
    case RECORD_TYPE:
      if (TYPE_PTRMEMFUNC_P (t))
	break;
      /* Fall through.  */

    case UNION_TYPE:
    case ENUMERAL_TYPE:
      if (!TYPE_TEMPLATE_INFO (t))
	*walk_subtrees = 0;
      else if (for_each_template_parm (TREE_VALUE (TYPE_TEMPLATE_INFO (t)),
				       fn, data, pfd->visited))
	return error_mark_node;
      break;

    case METHOD_TYPE:
      /* Since we're not going to walk subtrees, we have to do this
	 explicitly here.  */
      if (for_each_template_parm (TYPE_METHOD_BASETYPE (t), fn, data,
				  pfd->visited))
	return error_mark_node;
      /* Fall through.  */

    case FUNCTION_TYPE:
      /* Check the return type.  */
      if (for_each_template_parm (TREE_TYPE (t), fn, data, pfd->visited))
	return error_mark_node;

      /* Check the parameter types.  Since default arguments are not
	 instantiated until they are needed, the TYPE_ARG_TYPES may
	 contain expressions that involve template parameters.  But,
	 no-one should be looking at them yet.  And, once they're
	 instantiated, they don't contain template parameters, so
	 there's no point in looking at them then, either.  */
      {
	tree parm;

	for (parm = TYPE_ARG_TYPES (t); parm; parm = TREE_CHAIN (parm))
	  if (for_each_template_parm (TREE_VALUE (parm), fn, data,
				      pfd->visited))
	    return error_mark_node;

	/* Since we've already handled the TYPE_ARG_TYPES, we don't
	   want walk_tree walking into them itself.  */
	*walk_subtrees = 0;
      }
      break;

    case TYPEOF_TYPE:
      if (for_each_template_parm (TYPE_FIELDS (t), fn, data,
				  pfd->visited))
	return error_mark_node;
      break;

    case FUNCTION_DECL:
    case VAR_DECL:
      if (DECL_LANG_SPECIFIC (t) && DECL_TEMPLATE_INFO (t)
	  && for_each_template_parm (DECL_TI_ARGS (t), fn, data,
				     pfd->visited))
	return error_mark_node;
      /* Fall through.  */

    case PARM_DECL:
    case CONST_DECL:
      if (TREE_CODE (t) == CONST_DECL && DECL_TEMPLATE_PARM_P (t)
	  && for_each_template_parm (DECL_INITIAL (t), fn, data,
				     pfd->visited))
	return error_mark_node;
      if (DECL_CONTEXT (t)
	  && for_each_template_parm (DECL_CONTEXT (t), fn, data,
				     pfd->visited))
	return error_mark_node;
      break;

    case BOUND_TEMPLATE_TEMPLATE_PARM:
      /* Record template parameters such as `T' inside `TT<T>'.  */
      if (for_each_template_parm (TYPE_TI_ARGS (t), fn, data, pfd->visited))
	return error_mark_node;
      /* Fall through.  */

    case TEMPLATE_TEMPLATE_PARM:
    case TEMPLATE_TYPE_PARM:
    case TEMPLATE_PARM_INDEX:
      if (fn && (*fn)(t, data))
	return error_mark_node;
      else if (!fn)
	return error_mark_node;
      break;

    case TEMPLATE_DECL:
      /* A template template parameter is encountered.  */
      if (DECL_TEMPLATE_TEMPLATE_PARM_P (t)
	  && for_each_template_parm (TREE_TYPE (t), fn, data, pfd->visited))
	return error_mark_node;

      /* Already substituted template template parameter */
      *walk_subtrees = 0;
      break;

    case TYPENAME_TYPE:
      if (!fn
	  || for_each_template_parm (TYPENAME_TYPE_FULLNAME (t), fn,
				     data, pfd->visited))
	return error_mark_node;
      break;

    case CONSTRUCTOR:
      if (TREE_TYPE (t) && TYPE_PTRMEMFUNC_P (TREE_TYPE (t))
	  && for_each_template_parm (TYPE_PTRMEMFUNC_FN_TYPE
				     (TREE_TYPE (t)), fn, data,
				     pfd->visited))
	return error_mark_node;
      break;

    case INDIRECT_REF:
    case COMPONENT_REF:
      /* If there's no type, then this thing must be some expression
	 involving template parameters.  */
      if (!fn && !TREE_TYPE (t))
	return error_mark_node;
      break;

    case MODOP_EXPR:
    case CAST_EXPR:
    case REINTERPRET_CAST_EXPR:
    case CONST_CAST_EXPR:
    case STATIC_CAST_EXPR:
    case DYNAMIC_CAST_EXPR:
    case ARROW_EXPR:
    case DOTSTAR_EXPR:
    case TYPEID_EXPR:
    case PSEUDO_DTOR_EXPR:
      if (!fn)
	return error_mark_node;
      break;

    case BASELINK:
      /* If we do not handle this case specially, we end up walking
	 the BINFO hierarchy, which is circular, and therefore
	 confuses walk_tree.  */
      *walk_subtrees = 0;
      if (for_each_template_parm (BASELINK_FUNCTIONS (*tp), fn, data,
				  pfd->visited))
	return error_mark_node;
      break;

    default:
      break;
    }

  /* We didn't find any template parameters we liked.  */
  return NULL_TREE;
}

/* For each TEMPLATE_TYPE_PARM, TEMPLATE_TEMPLATE_PARM,
   BOUND_TEMPLATE_TEMPLATE_PARM or TEMPLATE_PARM_INDEX in T,
   call FN with the parameter and the DATA.
   If FN returns nonzero, the iteration is terminated, and
   for_each_template_parm returns 1.  Otherwise, the iteration
   continues.  If FN never returns a nonzero value, the value
   returned by for_each_template_parm is 0.  If FN is NULL, it is
   considered to be the function which always returns 1.  */

static int
for_each_template_parm (tree t, tree_fn_t fn, void* data,
			struct pointer_set_t *visited)
{
  struct pair_fn_data pfd;
  int result;

  /* Set up.  */
  pfd.fn = fn;
  pfd.data = data;

  /* Walk the tree.  (Conceptually, we would like to walk without
     duplicates, but for_each_template_parm_r recursively calls
     for_each_template_parm, so we would need to reorganize a fair
     bit to use walk_tree_without_duplicates, so we keep our own
     visited list.)  */
  if (visited)
    pfd.visited = visited;
  else
    pfd.visited = pointer_set_create ();
  result = walk_tree (&t,
		      for_each_template_parm_r,
		      &pfd,
		      pfd.visited) != NULL_TREE;

  /* Clean up.  */
  if (!visited)
    {
      pointer_set_destroy (pfd.visited);
      pfd.visited = 0;
    }

  return result;
}

/* Returns true if T depends on any template parameter.  */

int
uses_template_parms (tree t)
{
  bool dependent_p;
  int saved_processing_template_decl;

  saved_processing_template_decl = processing_template_decl;
  if (!saved_processing_template_decl)
    processing_template_decl = 1;
  if (TYPE_P (t))
    dependent_p = dependent_type_p (t);
  else if (TREE_CODE (t) == TREE_VEC)
    dependent_p = any_dependent_template_arguments_p (t);
  else if (TREE_CODE (t) == TREE_LIST)
    dependent_p = (uses_template_parms (TREE_VALUE (t))
		   || uses_template_parms (TREE_CHAIN (t)));
  else if (TREE_CODE (t) == TYPE_DECL)
    dependent_p = dependent_type_p (TREE_TYPE (t));
  else if (DECL_P (t)
	   || EXPR_P (t)
	   || TREE_CODE (t) == TEMPLATE_PARM_INDEX
	   || TREE_CODE (t) == OVERLOAD
	   || TREE_CODE (t) == BASELINK
	   || TREE_CODE (t) == IDENTIFIER_NODE
	   || CONSTANT_CLASS_P (t))
    dependent_p = (type_dependent_expression_p (t)
		   || value_dependent_expression_p (t));
  else
    {
      gcc_assert (t == error_mark_node);
      dependent_p = false;
    }

  processing_template_decl = saved_processing_template_decl;

  return dependent_p;
}

/* Returns true if T depends on any template parameter with level LEVEL.  */

int
uses_template_parms_level (tree t, int level)
{
  return for_each_template_parm (t, template_parm_this_level_p, &level, NULL);
}

static int tinst_depth;
extern int max_tinst_depth;
#ifdef GATHER_STATISTICS
int depth_reached;
#endif
static int tinst_level_tick;
static int last_template_error_tick;

/* We're starting to instantiate D; record the template instantiation context
   for diagnostics and to restore it later.  */

static int
push_tinst_level (tree d)
{
  tree new;

  if (tinst_depth >= max_tinst_depth)
    {
      /* If the instantiation in question still has unbound template parms,
	 we don't really care if we can't instantiate it, so just return.
	 This happens with base instantiation for implicit `typename'.  */
      if (uses_template_parms (d))
	return 0;

      last_template_error_tick = tinst_level_tick;
      error ("template instantiation depth exceeds maximum of %d (use "
	     "-ftemplate-depth-NN to increase the maximum) instantiating %qD",
	     max_tinst_depth, d);

      print_instantiation_context ();

      return 0;
    }

  new = make_node (TINST_LEVEL);
  TINST_DECL (new) = d;
  TINST_LOCATION (new) = input_location;
  TINST_IN_SYSTEM_HEADER_P (new) = in_system_header;
  TREE_CHAIN (new) = current_tinst_level;
  current_tinst_level = new;

  ++tinst_depth;
#ifdef GATHER_STATISTICS
  if (tinst_depth > depth_reached)
    depth_reached = tinst_depth;
#endif

  ++tinst_level_tick;
  return 1;
}

/* We're done instantiating this template; return to the instantiation
   context.  */

static void
pop_tinst_level (void)
{
  tree old = current_tinst_level;

  /* Restore the filename and line number stashed away when we started
     this instantiation.  */
  input_location = TINST_LOCATION (old);
  in_system_header = TINST_IN_SYSTEM_HEADER_P (old);
  current_tinst_level = TREE_CHAIN (old);
  --tinst_depth;
  ++tinst_level_tick;
}

/* We're instantiating a deferred template; restore the template
   instantiation context in which the instantiation was requested, which
   is one step out from LEVEL.  */

static void
reopen_tinst_level (tree level)
{
  tree t;

  tinst_depth = 0;
  for (t = level; t; t = TREE_CHAIN (t))
    ++tinst_depth;

  current_tinst_level = level;
  pop_tinst_level ();
}

/* Returns the TINST_LEVEL which gives the original instantiation
   context.  */

tree
outermost_tinst_level (void)
{
  return tree_last (current_tinst_level);
}

/* DECL is a friend FUNCTION_DECL or TEMPLATE_DECL.  ARGS is the
   vector of template arguments, as for tsubst.

   Returns an appropriate tsubst'd friend declaration.  */

static tree
tsubst_friend_function (tree decl, tree args)
{
  tree new_friend;

  if (TREE_CODE (decl) == FUNCTION_DECL
      && DECL_TEMPLATE_INSTANTIATION (decl)
      && TREE_CODE (DECL_TI_TEMPLATE (decl)) != TEMPLATE_DECL)
    /* This was a friend declared with an explicit template
       argument list, e.g.:

       friend void f<>(T);

       to indicate that f was a template instantiation, not a new
       function declaration.  Now, we have to figure out what
       instantiation of what template.  */
    {
      tree template_id, arglist, fns;
      tree new_args;
      tree tmpl;
      tree ns = decl_namespace_context (TYPE_MAIN_DECL (current_class_type));

      /* Friend functions are looked up in the containing namespace scope.
	 We must enter that scope, to avoid finding member functions of the
	 current cless with same name.  */
      push_nested_namespace (ns);
      fns = tsubst_expr (DECL_TI_TEMPLATE (decl), args,
			 tf_warning_or_error, NULL_TREE,
			 /*integral_constant_expression_p=*/false);
      pop_nested_namespace (ns);
      arglist = tsubst (DECL_TI_ARGS (decl), args,
			tf_warning_or_error, NULL_TREE);
      template_id = lookup_template_function (fns, arglist);

      new_friend = tsubst (decl, args, tf_warning_or_error, NULL_TREE);
      tmpl = determine_specialization (template_id, new_friend,
				       &new_args,
				       /*need_member_template=*/0,
				       TREE_VEC_LENGTH (args),
				       tsk_none);
      return instantiate_template (tmpl, new_args, tf_error);
    }

  new_friend = tsubst (decl, args, tf_warning_or_error, NULL_TREE);

  /* The NEW_FRIEND will look like an instantiation, to the
     compiler, but is not an instantiation from the point of view of
     the language.  For example, we might have had:

     template <class T> struct S {
       template <class U> friend void f(T, U);
     };

     Then, in S<int>, template <class U> void f(int, U) is not an
     instantiation of anything.  */
  if (new_friend == error_mark_node)
    return error_mark_node;

  DECL_USE_TEMPLATE (new_friend) = 0;
  if (TREE_CODE (decl) == TEMPLATE_DECL)
    {
      DECL_USE_TEMPLATE (DECL_TEMPLATE_RESULT (new_friend)) = 0;
      DECL_SAVED_TREE (DECL_TEMPLATE_RESULT (new_friend))
	= DECL_SAVED_TREE (DECL_TEMPLATE_RESULT (decl));
    }

  /* The mangled name for the NEW_FRIEND is incorrect.  The function
     is not a template instantiation and should not be mangled like
     one.  Therefore, we forget the mangling here; we'll recompute it
     later if we need it.  */
  if (TREE_CODE (new_friend) != TEMPLATE_DECL)
    {
      SET_DECL_RTL (new_friend, NULL_RTX);
      SET_DECL_ASSEMBLER_NAME (new_friend, NULL_TREE);
    }

  if (DECL_NAMESPACE_SCOPE_P (new_friend))
    {
      tree old_decl;
      tree new_friend_template_info;
      tree new_friend_result_template_info;
      tree ns;
      int  new_friend_is_defn;

      /* We must save some information from NEW_FRIEND before calling
	 duplicate decls since that function will free NEW_FRIEND if
	 possible.  */
      new_friend_template_info = DECL_TEMPLATE_INFO (new_friend);
      new_friend_is_defn =
	    (DECL_INITIAL (DECL_TEMPLATE_RESULT
			   (template_for_substitution (new_friend)))
	     != NULL_TREE);
      if (TREE_CODE (new_friend) == TEMPLATE_DECL)
	{
	  /* This declaration is a `primary' template.  */
	  DECL_PRIMARY_TEMPLATE (new_friend) = new_friend;

	  new_friend_result_template_info
	    = DECL_TEMPLATE_INFO (DECL_TEMPLATE_RESULT (new_friend));
	}
      else
	new_friend_result_template_info = NULL_TREE;

      /* Make the init_value nonzero so pushdecl knows this is a defn.  */
      if (new_friend_is_defn)
	DECL_INITIAL (new_friend) = error_mark_node;

      /* Inside pushdecl_namespace_level, we will push into the
	 current namespace. However, the friend function should go
	 into the namespace of the template.  */
      ns = decl_namespace_context (new_friend);
      push_nested_namespace (ns);
      old_decl = pushdecl_namespace_level (new_friend, /*is_friend=*/true);
      pop_nested_namespace (ns);

      if (old_decl == error_mark_node)
	return error_mark_node;

      if (old_decl != new_friend)
	{
	  /* This new friend declaration matched an existing
	     declaration.  For example, given:

	       template <class T> void f(T);
	       template <class U> class C {
		 template <class T> friend void f(T) {}
	       };

	     the friend declaration actually provides the definition
	     of `f', once C has been instantiated for some type.  So,
	     old_decl will be the out-of-class template declaration,
	     while new_friend is the in-class definition.

	     But, if `f' was called before this point, the
	     instantiation of `f' will have DECL_TI_ARGS corresponding
	     to `T' but not to `U', references to which might appear
	     in the definition of `f'.  Previously, the most general
	     template for an instantiation of `f' was the out-of-class
	     version; now it is the in-class version.  Therefore, we
	     run through all specialization of `f', adding to their
	     DECL_TI_ARGS appropriately.  In particular, they need a
	     new set of outer arguments, corresponding to the
	     arguments for this class instantiation.

	     The same situation can arise with something like this:

	       friend void f(int);
	       template <class T> class C {
		 friend void f(T) {}
	       };

	     when `C<int>' is instantiated.  Now, `f(int)' is defined
	     in the class.  */

	  if (!new_friend_is_defn)
	    /* On the other hand, if the in-class declaration does
	       *not* provide a definition, then we don't want to alter
	       existing definitions.  We can just leave everything
	       alone.  */
	    ;
	  else
	    {
	      /* Overwrite whatever template info was there before, if
		 any, with the new template information pertaining to
		 the declaration.  */
	      DECL_TEMPLATE_INFO (old_decl) = new_friend_template_info;

	      if (TREE_CODE (old_decl) != TEMPLATE_DECL)
		reregister_specialization (new_friend,
					   most_general_template (old_decl),
					   old_decl);
	      else
		{
		  tree t;
		  tree new_friend_args;

		  DECL_TEMPLATE_INFO (DECL_TEMPLATE_RESULT (old_decl))
		    = new_friend_result_template_info;

		  new_friend_args = TI_ARGS (new_friend_template_info);
		  for (t = DECL_TEMPLATE_SPECIALIZATIONS (old_decl);
		       t != NULL_TREE;
		       t = TREE_CHAIN (t))
		    {
		      tree spec = TREE_VALUE (t);

		      DECL_TI_ARGS (spec)
			= add_outermost_template_args (new_friend_args,
						       DECL_TI_ARGS (spec));
		    }

		  /* Now, since specializations are always supposed to
		     hang off of the most general template, we must move
		     them.  */
		  t = most_general_template (old_decl);
		  if (t != old_decl)
		    {
		      DECL_TEMPLATE_SPECIALIZATIONS (t)
			= chainon (DECL_TEMPLATE_SPECIALIZATIONS (t),
				   DECL_TEMPLATE_SPECIALIZATIONS (old_decl));
		      DECL_TEMPLATE_SPECIALIZATIONS (old_decl) = NULL_TREE;
		    }
		}
	    }

	  /* The information from NEW_FRIEND has been merged into OLD_DECL
	     by duplicate_decls.  */
	  new_friend = old_decl;
	}
    }
  else
    {
      tree context = DECL_CONTEXT (new_friend);
      bool dependent_p;

      /* In the code
	   template <class T> class C {
	     template <class U> friend void C1<U>::f (); // case 1
	     friend void C2<T>::f ();			 // case 2
	   };
	 we only need to make sure CONTEXT is a complete type for
	 case 2.  To distinguish between the two cases, we note that
	 CONTEXT of case 1 remains dependent type after tsubst while
	 this isn't true for case 2.  */
      ++processing_template_decl;
      dependent_p = dependent_type_p (context);
      --processing_template_decl;

      if (!dependent_p
	  && !complete_type_or_else (context, NULL_TREE))
	return error_mark_node;

      if (COMPLETE_TYPE_P (context))
	{
	  /* Check to see that the declaration is really present, and,
	     possibly obtain an improved declaration.  */
	  tree fn = check_classfn (context,
				   new_friend, NULL_TREE);

	  if (fn)
	    new_friend = fn;
	}
    }

  return new_friend;
}

/* FRIEND_TMPL is a friend TEMPLATE_DECL.  ARGS is the vector of
   template arguments, as for tsubst.

   Returns an appropriate tsubst'd friend type or error_mark_node on
   failure.  */

static tree
tsubst_friend_class (tree friend_tmpl, tree args)
{
  tree friend_type;
  tree tmpl;
  tree context;

  context = DECL_CONTEXT (friend_tmpl);

  if (context)
    {
      if (TREE_CODE (context) == NAMESPACE_DECL)
	push_nested_namespace (context);
      else
	push_nested_class (tsubst (context, args, tf_none, NULL_TREE));
    }

  /* Look for a class template declaration.  We look for hidden names
     because two friend declarations of the same template are the
     same.  For example, in:

       struct A { 
         template <typename> friend class F;
       };
       template <typename> struct B { 
         template <typename> friend class F;
       };

     both F templates are the same.  */
  tmpl = lookup_name_real (DECL_NAME (friend_tmpl), 0, 0,
			   /*block_p=*/true, 0, 
			   LOOKUP_COMPLAIN | LOOKUP_HIDDEN);

  /* But, if we don't find one, it might be because we're in a
     situation like this:

       template <class T>
       struct S {
	 template <class U>
	 friend struct S;
       };

     Here, in the scope of (say) S<int>, `S' is bound to a TYPE_DECL
     for `S<int>', not the TEMPLATE_DECL.  */
  if (!tmpl || !DECL_CLASS_TEMPLATE_P (tmpl))
    {
      tmpl = lookup_name_prefer_type (DECL_NAME (friend_tmpl), 1);
      tmpl = maybe_get_template_decl_from_type_decl (tmpl);
    }

  if (tmpl && DECL_CLASS_TEMPLATE_P (tmpl))
    {
      /* The friend template has already been declared.  Just
	 check to see that the declarations match, and install any new
	 default parameters.  We must tsubst the default parameters,
	 of course.  We only need the innermost template parameters
	 because that is all that redeclare_class_template will look
	 at.  */
      if (TMPL_PARMS_DEPTH (DECL_TEMPLATE_PARMS (friend_tmpl))
	  > TMPL_ARGS_DEPTH (args))
	{
	  tree parms;
	  parms = tsubst_template_parms (DECL_TEMPLATE_PARMS (friend_tmpl),
					 args, tf_warning_or_error);
	  redeclare_class_template (TREE_TYPE (tmpl), parms);
	}

      friend_type = TREE_TYPE (tmpl);
    }
  else
    {
      /* The friend template has not already been declared.  In this
	 case, the instantiation of the template class will cause the
	 injection of this template into the global scope.  */
      tmpl = tsubst (friend_tmpl, args, tf_warning_or_error, NULL_TREE);
      if (tmpl == error_mark_node)
	return error_mark_node;

      /* The new TMPL is not an instantiation of anything, so we
	 forget its origins.  We don't reset CLASSTYPE_TI_TEMPLATE for
	 the new type because that is supposed to be the corresponding
	 template decl, i.e., TMPL.  */
      DECL_USE_TEMPLATE (tmpl) = 0;
      DECL_TEMPLATE_INFO (tmpl) = NULL_TREE;
      CLASSTYPE_USE_TEMPLATE (TREE_TYPE (tmpl)) = 0;
      CLASSTYPE_TI_ARGS (TREE_TYPE (tmpl))
	= INNERMOST_TEMPLATE_ARGS (CLASSTYPE_TI_ARGS (TREE_TYPE (tmpl)));

      /* Inject this template into the global scope.  */
      friend_type = TREE_TYPE (pushdecl_top_level_maybe_friend (tmpl, true));
    }

  if (context)
    {
      if (TREE_CODE (context) == NAMESPACE_DECL)
	pop_nested_namespace (context);
      else
	pop_nested_class ();
    }

  return friend_type;
}

/* Returns zero if TYPE cannot be completed later due to circularity.
   Otherwise returns one.  */

static int
can_complete_type_without_circularity (tree type)
{
  if (type == NULL_TREE || type == error_mark_node)
    return 0;
  else if (COMPLETE_TYPE_P (type))
    return 1;
  else if (TREE_CODE (type) == ARRAY_TYPE && TYPE_DOMAIN (type))
    return can_complete_type_without_circularity (TREE_TYPE (type));
  else if (CLASS_TYPE_P (type)
	   && TYPE_BEING_DEFINED (TYPE_MAIN_VARIANT (type)))
    return 0;
  else
    return 1;
}

tree
instantiate_class_template (tree type)
{
  tree template, args, pattern, t, member;
  tree typedecl;
  tree pbinfo;
  tree base_list;

  if (type == error_mark_node)
    return error_mark_node;

  if (TYPE_BEING_DEFINED (type)
      || COMPLETE_TYPE_P (type)
      || dependent_type_p (type))
    return type;

  /* Figure out which template is being instantiated.  */
  template = most_general_template (CLASSTYPE_TI_TEMPLATE (type));
  gcc_assert (TREE_CODE (template) == TEMPLATE_DECL);

  /* Determine what specialization of the original template to
     instantiate.  */
  t = most_specialized_class (type, template);
  if (t == error_mark_node)
    {
      TYPE_BEING_DEFINED (type) = 1;
      return error_mark_node;
    }
  else if (t)
    {
      /* This TYPE is actually an instantiation of a partial
	 specialization.  We replace the innermost set of ARGS with
	 the arguments appropriate for substitution.  For example,
	 given:

	   template <class T> struct S {};
	   template <class T> struct S<T*> {};

	 and supposing that we are instantiating S<int*>, ARGS will
	 presently be {int*} -- but we need {int}.  */
      pattern = TREE_TYPE (t);
      args = TREE_PURPOSE (t);
    }
  else
    {
      pattern = TREE_TYPE (template);
      args = CLASSTYPE_TI_ARGS (type);
    }

  /* If the template we're instantiating is incomplete, then clearly
     there's nothing we can do.  */
  if (!COMPLETE_TYPE_P (pattern))
    return type;

  /* If we've recursively instantiated too many templates, stop.  */
  if (! push_tinst_level (type))
    return type;

  /* Now we're really doing the instantiation.  Mark the type as in
     the process of being defined.  */
  TYPE_BEING_DEFINED (type) = 1;

  /* We may be in the middle of deferred access check.  Disable
     it now.  */
  push_deferring_access_checks (dk_no_deferred);

  push_to_top_level ();

  SET_CLASSTYPE_INTERFACE_UNKNOWN (type);

  /* Set the input location to the template definition. This is needed
     if tsubsting causes an error.  */
  typedecl = TYPE_MAIN_DECL (type);
  input_location = DECL_SOURCE_LOCATION (typedecl);
  in_system_header = DECL_IN_SYSTEM_HEADER (typedecl);

  TYPE_HAS_CONSTRUCTOR (type) = TYPE_HAS_CONSTRUCTOR (pattern);
  TYPE_HAS_NEW_OPERATOR (type) = TYPE_HAS_NEW_OPERATOR (pattern);
  TYPE_HAS_ARRAY_NEW_OPERATOR (type) = TYPE_HAS_ARRAY_NEW_OPERATOR (pattern);
  TYPE_GETS_DELETE (type) = TYPE_GETS_DELETE (pattern);
  TYPE_HAS_ASSIGN_REF (type) = TYPE_HAS_ASSIGN_REF (pattern);
  TYPE_HAS_CONST_ASSIGN_REF (type) = TYPE_HAS_CONST_ASSIGN_REF (pattern);
  TYPE_HAS_INIT_REF (type) = TYPE_HAS_INIT_REF (pattern);
  TYPE_HAS_CONST_INIT_REF (type) = TYPE_HAS_CONST_INIT_REF (pattern);
  TYPE_HAS_DEFAULT_CONSTRUCTOR (type) = TYPE_HAS_DEFAULT_CONSTRUCTOR (pattern);
  TYPE_HAS_CONVERSION (type) = TYPE_HAS_CONVERSION (pattern);
  TYPE_PACKED (type) = TYPE_PACKED (pattern);
  TYPE_ALIGN (type) = TYPE_ALIGN (pattern);
  TYPE_USER_ALIGN (type) = TYPE_USER_ALIGN (pattern);
  TYPE_FOR_JAVA (type) = TYPE_FOR_JAVA (pattern); /* For libjava's JArray<T> */
  if (ANON_AGGR_TYPE_P (pattern))
    SET_ANON_AGGR_TYPE_P (type);
  if (CLASSTYPE_VISIBILITY_SPECIFIED (pattern))
    {
      CLASSTYPE_VISIBILITY_SPECIFIED (type) = 1;
      CLASSTYPE_VISIBILITY (type) = CLASSTYPE_VISIBILITY (pattern);
    }

  pbinfo = TYPE_BINFO (pattern);

  /* We should never instantiate a nested class before its enclosing
     class; we need to look up the nested class by name before we can
     instantiate it, and that lookup should instantiate the enclosing
     class.  */
  gcc_assert (!DECL_CLASS_SCOPE_P (TYPE_MAIN_DECL (pattern))
	      || COMPLETE_TYPE_P (TYPE_CONTEXT (type))
	      || TYPE_BEING_DEFINED (TYPE_CONTEXT (type)));

  base_list = NULL_TREE;
  if (BINFO_N_BASE_BINFOS (pbinfo))
    {
      tree pbase_binfo;
      tree context = TYPE_CONTEXT (type);
      tree pushed_scope;
      int i;

      /* We must enter the scope containing the type, as that is where
	 the accessibility of types named in dependent bases are
	 looked up from.  */
      pushed_scope = push_scope (context ? context : global_namespace);

      /* Substitute into each of the bases to determine the actual
	 basetypes.  */
      for (i = 0; BINFO_BASE_ITERATE (pbinfo, i, pbase_binfo); i++)
	{
	  tree base;
	  tree access = BINFO_BASE_ACCESS (pbinfo, i);

	  /* Substitute to figure out the base class.  */
	  base = tsubst (BINFO_TYPE (pbase_binfo), args, tf_error, NULL_TREE);
	  if (base == error_mark_node)
	    continue;

	  base_list = tree_cons (access, base, base_list);
	  if (BINFO_VIRTUAL_P (pbase_binfo))
	    TREE_TYPE (base_list) = integer_type_node;
	}

      /* The list is now in reverse order; correct that.  */
      base_list = nreverse (base_list);

      if (pushed_scope)
	pop_scope (pushed_scope);
    }
  /* Now call xref_basetypes to set up all the base-class
     information.  */
  xref_basetypes (type, base_list);


  /* Now that our base classes are set up, enter the scope of the
     class, so that name lookups into base classes, etc. will work
     correctly.  This is precisely analogous to what we do in
     begin_class_definition when defining an ordinary non-template
     class.  */
  pushclass (type);

  /* Now members are processed in the order of declaration.  */
  for (member = CLASSTYPE_DECL_LIST (pattern);
       member; member = TREE_CHAIN (member))
    {
      tree t = TREE_VALUE (member);

      if (TREE_PURPOSE (member))
	{
	  if (TYPE_P (t))
	    {
	      /* Build new CLASSTYPE_NESTED_UTDS.  */

	      tree newtag;
	      bool class_template_p;

	      class_template_p = (TREE_CODE (t) != ENUMERAL_TYPE
				  && TYPE_LANG_SPECIFIC (t)
				  && CLASSTYPE_IS_TEMPLATE (t));
	      /* If the member is a class template, then -- even after
		 substitution -- there may be dependent types in the
		 template argument list for the class.  We increment
		 PROCESSING_TEMPLATE_DECL so that dependent_type_p, as
		 that function will assume that no types are dependent
		 when outside of a template.  */
	      if (class_template_p)
		++processing_template_decl;
	      newtag = tsubst (t, args, tf_error, NULL_TREE);
	      if (class_template_p)
		--processing_template_decl;
	      if (newtag == error_mark_node)
		continue;

	      if (TREE_CODE (newtag) != ENUMERAL_TYPE)
		{
		  tree name = TYPE_IDENTIFIER (t);

		  if (class_template_p)
		    /* Unfortunately, lookup_template_class sets
		       CLASSTYPE_IMPLICIT_INSTANTIATION for a partial
		       instantiation (i.e., for the type of a member
		       template class nested within a template class.)
		       This behavior is required for
		       maybe_process_partial_specialization to work
		       correctly, but is not accurate in this case;
		       the TAG is not an instantiation of anything.
		       (The corresponding TEMPLATE_DECL is an
		       instantiation, but the TYPE is not.) */
		    CLASSTYPE_USE_TEMPLATE (newtag) = 0;

		  /* Now, we call pushtag to put this NEWTAG into the scope of
		     TYPE.  We first set up the IDENTIFIER_TYPE_VALUE to avoid
		     pushtag calling push_template_decl.  We don't have to do
		     this for enums because it will already have been done in
		     tsubst_enum.  */
		  if (name)
		    SET_IDENTIFIER_TYPE_VALUE (name, newtag);
		  pushtag (name, newtag, /*tag_scope=*/ts_current);
		}
	    }
	  else if (TREE_CODE (t) == FUNCTION_DECL
		   || DECL_FUNCTION_TEMPLATE_P (t))
	    {
	      /* Build new TYPE_METHODS.  */
	      tree r;

	      if (TREE_CODE (t) == TEMPLATE_DECL)
		++processing_template_decl;
	      r = tsubst (t, args, tf_error, NULL_TREE);
	      if (TREE_CODE (t) == TEMPLATE_DECL)
		--processing_template_decl;
	      set_current_access_from_decl (r);
	      finish_member_declaration (r);
	    }
	  else
	    {
	      /* Build new TYPE_FIELDS.  */

	      if (TREE_CODE (t) != CONST_DECL)
		{
		  tree r;

		  /* The the file and line for this declaration, to
		     assist in error message reporting.  Since we
		     called push_tinst_level above, we don't need to
		     restore these.  */
		  input_location = DECL_SOURCE_LOCATION (t);

		  if (TREE_CODE (t) == TEMPLATE_DECL)
		    ++processing_template_decl;
		  r = tsubst (t, args, tf_warning_or_error, NULL_TREE);
		  if (TREE_CODE (t) == TEMPLATE_DECL)
		    --processing_template_decl;
		  if (TREE_CODE (r) == VAR_DECL)
		    {
		      /* In [temp.inst]:

			   [t]he initialization (and any associated
			   side-effects) of a static data member does
			   not occur unless the static data member is
			   itself used in a way that requires the
			   definition of the static data member to
			   exist.

			 Therefore, we do not substitute into the
			 initialized for the static data member here.  */
		      finish_static_data_member_decl
			(r,
			 /*init=*/NULL_TREE,
			 /*init_const_expr_p=*/false,
			 /*asmspec_tree=*/NULL_TREE,
			 /*flags=*/0);
		      if (DECL_INITIALIZED_IN_CLASS_P (r))
			check_static_variable_definition (r, TREE_TYPE (r));
		    }
		  else if (TREE_CODE (r) == FIELD_DECL)
		    {
		      /* Determine whether R has a valid type and can be
			 completed later.  If R is invalid, then it is
			 replaced by error_mark_node so that it will not be
			 added to TYPE_FIELDS.  */
		      tree rtype = TREE_TYPE (r);
		      if (can_complete_type_without_circularity (rtype))
			complete_type (rtype);

		      if (!COMPLETE_TYPE_P (rtype))
			{
			  cxx_incomplete_type_error (r, rtype);
			  r = error_mark_node;
			}
		    }

		  /* If it is a TYPE_DECL for a class-scoped ENUMERAL_TYPE,
		     such a thing will already have been added to the field
		     list by tsubst_enum in finish_member_declaration in the
		     CLASSTYPE_NESTED_UTDS case above.  */
		  if (!(TREE_CODE (r) == TYPE_DECL
			&& TREE_CODE (TREE_TYPE (r)) == ENUMERAL_TYPE
			&& DECL_ARTIFICIAL (r)))
		    {
		      set_current_access_from_decl (r);
		      finish_member_declaration (r);
		    }
		}
	    }
	}
      else
	{
	  if (TYPE_P (t) || DECL_CLASS_TEMPLATE_P (t))
	    {
	      /* Build new CLASSTYPE_FRIEND_CLASSES.  */

	      tree friend_type = t;
	      bool adjust_processing_template_decl = false;

	      if (TREE_CODE (friend_type) == TEMPLATE_DECL)
		{
		  /* template <class T> friend class C;  */
		  friend_type = tsubst_friend_class (friend_type, args);
		  adjust_processing_template_decl = true;
		}
	      else if (TREE_CODE (friend_type) == UNBOUND_CLASS_TEMPLATE)
		{
		  /* template <class T> friend class C::D;  */
		  friend_type = tsubst (friend_type, args,
					tf_warning_or_error, NULL_TREE);
		  if (TREE_CODE (friend_type) == TEMPLATE_DECL)
		    friend_type = TREE_TYPE (friend_type);
		  adjust_processing_template_decl = true;
		}
	      else if (TREE_CODE (friend_type) == TYPENAME_TYPE)
		{
		  /* This could be either

		       friend class T::C;

		     when dependent_type_p is false or

		       template <class U> friend class T::C;

		     otherwise.  */
		  friend_type = tsubst (friend_type, args,
					tf_warning_or_error, NULL_TREE);
		  /* Bump processing_template_decl for correct
		     dependent_type_p calculation.  */
		  ++processing_template_decl;
		  if (dependent_type_p (friend_type))
		    adjust_processing_template_decl = true;
		  --processing_template_decl;
		}
	      else if (!CLASSTYPE_USE_TEMPLATE (friend_type)
		       && hidden_name_p (TYPE_NAME (friend_type)))
		{
		  /* friend class C;

		     where C hasn't been declared yet.  Let's lookup name
		     from namespace scope directly, bypassing any name that
		     come from dependent base class.  */
		  tree ns = decl_namespace_context (TYPE_MAIN_DECL (friend_type));

		  /* The call to xref_tag_from_type does injection for friend
		     classes.  */
		  push_nested_namespace (ns);
		  friend_type =
		    xref_tag_from_type (friend_type, NULL_TREE,
					/*tag_scope=*/ts_current);
		  pop_nested_namespace (ns);
		}
	      else if (uses_template_parms (friend_type))
		/* friend class C<T>;  */
		friend_type = tsubst (friend_type, args,
				      tf_warning_or_error, NULL_TREE);
	      /* Otherwise it's

		   friend class C;

		 where C is already declared or

		   friend class C<int>;

		 We don't have to do anything in these cases.  */

	      if (adjust_processing_template_decl)
		/* Trick make_friend_class into realizing that the friend
		   we're adding is a template, not an ordinary class.  It's
		   important that we use make_friend_class since it will
		   perform some error-checking and output cross-reference
		   information.  */
		++processing_template_decl;

	      if (friend_type != error_mark_node)
		make_friend_class (type, friend_type, /*complain=*/false);

	      if (adjust_processing_template_decl)
		--processing_template_decl;
	    }
	  else
	    {
	      /* Build new DECL_FRIENDLIST.  */
	      tree r;

	      /* The the file and line for this declaration, to
		 assist in error message reporting.  Since we
		 called push_tinst_level above, we don't need to
		 restore these.  */
	      input_location = DECL_SOURCE_LOCATION (t);

	      if (TREE_CODE (t) == TEMPLATE_DECL)
		{
		  ++processing_template_decl;
		  push_deferring_access_checks (dk_no_check);
		}

	      r = tsubst_friend_function (t, args);
	      add_friend (type, r, /*complain=*/false);
	      if (TREE_CODE (t) == TEMPLATE_DECL)
		{
		  pop_deferring_access_checks ();
		  --processing_template_decl;
		}
	    }
	}
    }

  /* Set the file and line number information to whatever is given for
     the class itself.  This puts error messages involving generated
     implicit functions at a predictable point, and the same point
     that would be used for non-template classes.  */
  input_location = DECL_SOURCE_LOCATION (typedecl);

  unreverse_member_declarations (type);
  finish_struct_1 (type);
  TYPE_BEING_DEFINED (type) = 0;

  /* Now that the class is complete, instantiate default arguments for
     any member functions.  We don't do this earlier because the
     default arguments may reference members of the class.  */
  if (!PRIMARY_TEMPLATE_P (template))
    for (t = TYPE_METHODS (type); t; t = TREE_CHAIN (t))
      if (TREE_CODE (t) == FUNCTION_DECL
	  /* Implicitly generated member functions will not have template
	     information; they are not instantiations, but instead are
	     created "fresh" for each instantiation.  */
	  && DECL_TEMPLATE_INFO (t))
	tsubst_default_arguments (t);

  popclass ();
  pop_from_top_level ();
  pop_deferring_access_checks ();
  pop_tinst_level ();

  /* The vtable for a template class can be emitted in any translation
     unit in which the class is instantiated.  When there is no key
     method, however, finish_struct_1 will already have added TYPE to
     the keyed_classes list.  */
  if (TYPE_CONTAINS_VPTR_P (type) && CLASSTYPE_KEY_METHOD (type))
    keyed_classes = tree_cons (NULL_TREE, type, keyed_classes);

  return type;
}

static tree
tsubst_template_arg (tree t, tree args, tsubst_flags_t complain, tree in_decl)
{
  tree r;

  if (!t)
    r = t;
  else if (TYPE_P (t))
    r = tsubst (t, args, complain, in_decl);
  else
    {
      r = tsubst_expr (t, args, complain, in_decl,
		       /*integral_constant_expression_p=*/true);
      r = fold_non_dependent_expr (r);
    }
  return r;
}

/* Substitute ARGS into the vector or list of template arguments T.  */

static tree
tsubst_template_args (tree t, tree args, tsubst_flags_t complain, tree in_decl)
{
  int len = TREE_VEC_LENGTH (t);
  int need_new = 0, i;
  tree *elts = (tree *) alloca (len * sizeof (tree));

  for (i = 0; i < len; i++)
    {
      tree orig_arg = TREE_VEC_ELT (t, i);
      tree new_arg;

      if (TREE_CODE (orig_arg) == TREE_VEC)
	new_arg = tsubst_template_args (orig_arg, args, complain, in_decl);
      else
	new_arg = tsubst_template_arg (orig_arg, args, complain, in_decl);

      if (new_arg == error_mark_node)
	return error_mark_node;

      elts[i] = new_arg;
      if (new_arg != orig_arg)
	need_new = 1;
    }

  if (!need_new)
    return t;

  t = make_tree_vec (len);
  for (i = 0; i < len; i++)
    TREE_VEC_ELT (t, i) = elts[i];

  return t;
}

/* Return the result of substituting ARGS into the template parameters
   given by PARMS.  If there are m levels of ARGS and m + n levels of
   PARMS, then the result will contain n levels of PARMS.  For
   example, if PARMS is `template <class T> template <class U>
   template <T*, U, class V>' and ARGS is {{int}, {double}} then the
   result will be `template <int*, double, class V>'.  */

static tree
tsubst_template_parms (tree parms, tree args, tsubst_flags_t complain)
{
  tree r = NULL_TREE;
  tree* new_parms;

  /* When substituting into a template, we must set
     PROCESSING_TEMPLATE_DECL as the template parameters may be
     dependent if they are based on one-another, and the dependency
     predicates are short-circuit outside of templates.  */
  ++processing_template_decl;

  for (new_parms = &r;
       TMPL_PARMS_DEPTH (parms) > TMPL_ARGS_DEPTH (args);
       new_parms = &(TREE_CHAIN (*new_parms)),
	 parms = TREE_CHAIN (parms))
    {
      tree new_vec =
	make_tree_vec (TREE_VEC_LENGTH (TREE_VALUE (parms)));
      int i;

      for (i = 0; i < TREE_VEC_LENGTH (new_vec); ++i)
	{
          tree tuple;
          tree default_value;
          tree parm_decl;

          if (parms == error_mark_node)
            continue;

          tuple = TREE_VEC_ELT (TREE_VALUE (parms), i);

          if (tuple == error_mark_node)
            continue;

          default_value = TREE_PURPOSE (tuple);
          parm_decl = TREE_VALUE (tuple);

	  parm_decl = tsubst (parm_decl, args, complain, NULL_TREE);
	  if (TREE_CODE (parm_decl) == PARM_DECL
	      && invalid_nontype_parm_type_p (TREE_TYPE (parm_decl), complain))
	    parm_decl = error_mark_node;
	  default_value = tsubst_template_arg (default_value, args,
					       complain, NULL_TREE);

	  tuple = build_tree_list (default_value, parm_decl);
	  TREE_VEC_ELT (new_vec, i) = tuple;
	}

      *new_parms =
	tree_cons (size_int (TMPL_PARMS_DEPTH (parms)
			     - TMPL_ARGS_DEPTH (args)),
		   new_vec, NULL_TREE);
    }

  --processing_template_decl;

  return r;
}

/* Substitute the ARGS into the indicated aggregate (or enumeration)
   type T.  If T is not an aggregate or enumeration type, it is
   handled as if by tsubst.  IN_DECL is as for tsubst.  If
   ENTERING_SCOPE is nonzero, T is the context for a template which
   we are presently tsubst'ing.  Return the substituted value.  */

static tree
tsubst_aggr_type (tree t,
		  tree args,
		  tsubst_flags_t complain,
		  tree in_decl,
		  int entering_scope)
{
  if (t == NULL_TREE)
    return NULL_TREE;

  switch (TREE_CODE (t))
    {
    case RECORD_TYPE:
      if (TYPE_PTRMEMFUNC_P (t))
	return tsubst (TYPE_PTRMEMFUNC_FN_TYPE (t), args, complain, in_decl);

      /* Else fall through.  */
    case ENUMERAL_TYPE:
    case UNION_TYPE:
      if (TYPE_TEMPLATE_INFO (t))
	{
	  tree argvec;
	  tree context;
	  tree r;
	  bool saved_skip_evaluation;

	  /* In "sizeof(X<I>)" we need to evaluate "I".  */
	  saved_skip_evaluation = skip_evaluation;
	  skip_evaluation = false;

	  /* First, determine the context for the type we are looking
	     up.  */
	  context = TYPE_CONTEXT (t);
	  if (context)
	    context = tsubst_aggr_type (context, args, complain,
					in_decl, /*entering_scope=*/1);

	  /* Then, figure out what arguments are appropriate for the
	     type we are trying to find.  For example, given:

	       template <class T> struct S;
	       template <class T, class U> void f(T, U) { S<U> su; }

	     and supposing that we are instantiating f<int, double>,
	     then our ARGS will be {int, double}, but, when looking up
	     S we only want {double}.  */
	  argvec = tsubst_template_args (TYPE_TI_ARGS (t), args,
					 complain, in_decl);
	  if (argvec == error_mark_node)
	    r = error_mark_node;
	  else
	    {
	      r = lookup_template_class (t, argvec, in_decl, context,
					 entering_scope, complain);
	      r = cp_build_qualified_type_real (r, TYPE_QUALS (t), complain);
	    }

	  skip_evaluation = saved_skip_evaluation;

	  return r;
	}
      else
	/* This is not a template type, so there's nothing to do.  */
	return t;

    default:
      return tsubst (t, args, complain, in_decl);
    }
}

/* Substitute into the default argument ARG (a default argument for
   FN), which has the indicated TYPE.  */

tree
tsubst_default_argument (tree fn, tree type, tree arg)
{
  tree saved_class_ptr = NULL_TREE;
  tree saved_class_ref = NULL_TREE;

  /* This default argument came from a template.  Instantiate the
     default argument here, not in tsubst.  In the case of
     something like:

       template <class T>
       struct S {
	 static T t();
	 void f(T = t());
       };

     we must be careful to do name lookup in the scope of S<T>,
     rather than in the current class.  */
  push_access_scope (fn);
  /* The "this" pointer is not valid in a default argument.  */
  if (cfun)
    {
      saved_class_ptr = current_class_ptr;
      cp_function_chain->x_current_class_ptr = NULL_TREE;
      saved_class_ref = current_class_ref;
      cp_function_chain->x_current_class_ref = NULL_TREE;
    }

  push_deferring_access_checks(dk_no_deferred);
  /* The default argument expression may cause implicitly defined
     member functions to be synthesized, which will result in garbage
     collection.  We must treat this situation as if we were within
     the body of function so as to avoid collecting live data on the
     stack.  */
  ++function_depth;
  arg = tsubst_expr (arg, DECL_TI_ARGS (fn),
		     tf_warning_or_error, NULL_TREE,
		     /*integral_constant_expression_p=*/false);
  --function_depth;
  pop_deferring_access_checks();

  /* Restore the "this" pointer.  */
  if (cfun)
    {
      cp_function_chain->x_current_class_ptr = saved_class_ptr;
      cp_function_chain->x_current_class_ref = saved_class_ref;
    }

  pop_access_scope (fn);

  /* Make sure the default argument is reasonable.  */
  arg = check_default_argument (type, arg);

  return arg;
}

/* Substitute into all the default arguments for FN.  */

static void
tsubst_default_arguments (tree fn)
{
  tree arg;
  tree tmpl_args;

  tmpl_args = DECL_TI_ARGS (fn);

  /* If this function is not yet instantiated, we certainly don't need
     its default arguments.  */
  if (uses_template_parms (tmpl_args))
    return;

  for (arg = TYPE_ARG_TYPES (TREE_TYPE (fn));
       arg;
       arg = TREE_CHAIN (arg))
    if (TREE_PURPOSE (arg))
      TREE_PURPOSE (arg) = tsubst_default_argument (fn,
						    TREE_VALUE (arg),
						    TREE_PURPOSE (arg));
}

/* Substitute the ARGS into the T, which is a _DECL.  Return the
   result of the substitution.  Issue error and warning messages under
   control of COMPLAIN.  */

static tree
tsubst_decl (tree t, tree args, tsubst_flags_t complain)
{
  location_t saved_loc;
  tree r = NULL_TREE;
  tree in_decl = t;

  /* Set the filename and linenumber to improve error-reporting.  */
  saved_loc = input_location;
  input_location = DECL_SOURCE_LOCATION (t);

  switch (TREE_CODE (t))
    {
    case TEMPLATE_DECL:
      {
	/* We can get here when processing a member function template,
	   member class template, and template template parameter of
	   a template class.  */
	tree decl = DECL_TEMPLATE_RESULT (t);
	tree spec;
	tree tmpl_args;
	tree full_args;

	if (DECL_TEMPLATE_TEMPLATE_PARM_P (t))
	  {
	    /* Template template parameter is treated here.  */
	    tree new_type = tsubst (TREE_TYPE (t), args, complain, in_decl);
	    if (new_type == error_mark_node)
	      return error_mark_node;

	    r = copy_decl (t);
	    TREE_CHAIN (r) = NULL_TREE;
	    TREE_TYPE (r) = new_type;
	    DECL_TEMPLATE_RESULT (r)
	      = build_decl (TYPE_DECL, DECL_NAME (decl), new_type);
	    DECL_TEMPLATE_PARMS (r)
	      = tsubst_template_parms (DECL_TEMPLATE_PARMS (t), args,
				       complain);
	    TYPE_NAME (new_type) = r;
	    break;
	  }

	/* We might already have an instance of this template.
	   The ARGS are for the surrounding class type, so the
	   full args contain the tsubst'd args for the context,
	   plus the innermost args from the template decl.  */
	tmpl_args = DECL_CLASS_TEMPLATE_P (t)
	  ? CLASSTYPE_TI_ARGS (TREE_TYPE (t))
	  : DECL_TI_ARGS (DECL_TEMPLATE_RESULT (t));
	/* Because this is a template, the arguments will still be
	   dependent, even after substitution.  If
	   PROCESSING_TEMPLATE_DECL is not set, the dependency
	   predicates will short-circuit.  */
	++processing_template_decl;
	full_args = tsubst_template_args (tmpl_args, args,
					  complain, in_decl);
	--processing_template_decl;
	if (full_args == error_mark_node)
	  return error_mark_node;

	/* tsubst_template_args doesn't copy the vector if
	   nothing changed.  But, *something* should have
	   changed.  */
	gcc_assert (full_args != tmpl_args);

	spec = retrieve_specialization (t, full_args,
					/*class_specializations_p=*/true);
	if (spec != NULL_TREE)
	  {
	    r = spec;
	    break;
	  }

	/* Make a new template decl.  It will be similar to the
	   original, but will record the current template arguments.
	   We also create a new function declaration, which is just
	   like the old one, but points to this new template, rather
	   than the old one.  */
	r = copy_decl (t);
	gcc_assert (DECL_LANG_SPECIFIC (r) != 0);
	TREE_CHAIN (r) = NULL_TREE;

	DECL_TEMPLATE_INFO (r) = build_tree_list (t, args);

	if (TREE_CODE (decl) == TYPE_DECL)
	  {
	    tree new_type;
	    ++processing_template_decl;
	    new_type = tsubst (TREE_TYPE (t), args, complain, in_decl);
	    --processing_template_decl;
	    if (new_type == error_mark_node)
	      return error_mark_node;

	    TREE_TYPE (r) = new_type;
	    CLASSTYPE_TI_TEMPLATE (new_type) = r;
	    DECL_TEMPLATE_RESULT (r) = TYPE_MAIN_DECL (new_type);
	    DECL_TI_ARGS (r) = CLASSTYPE_TI_ARGS (new_type);
	    DECL_CONTEXT (r) = TYPE_CONTEXT (new_type);
	  }
	else
	  {
	    tree new_decl;
	    ++processing_template_decl;
	    new_decl = tsubst (decl, args, complain, in_decl);
	    --processing_template_decl;
	    if (new_decl == error_mark_node)
	      return error_mark_node;

	    DECL_TEMPLATE_RESULT (r) = new_decl;
	    DECL_TI_TEMPLATE (new_decl) = r;
	    TREE_TYPE (r) = TREE_TYPE (new_decl);
	    DECL_TI_ARGS (r) = DECL_TI_ARGS (new_decl);
	    DECL_CONTEXT (r) = DECL_CONTEXT (new_decl);
	  }

	SET_DECL_IMPLICIT_INSTANTIATION (r);
	DECL_TEMPLATE_INSTANTIATIONS (r) = NULL_TREE;
	DECL_TEMPLATE_SPECIALIZATIONS (r) = NULL_TREE;

	/* The template parameters for this new template are all the
	   template parameters for the old template, except the
	   outermost level of parameters.  */
	DECL_TEMPLATE_PARMS (r)
	  = tsubst_template_parms (DECL_TEMPLATE_PARMS (t), args,
				   complain);

	if (PRIMARY_TEMPLATE_P (t))
	  DECL_PRIMARY_TEMPLATE (r) = r;

	if (TREE_CODE (decl) != TYPE_DECL)
	  /* Record this non-type partial instantiation.  */
	  register_specialization (r, t,
				   DECL_TI_ARGS (DECL_TEMPLATE_RESULT (r)),
				   false);
      }
      break;

    case FUNCTION_DECL:
      {
	tree ctx;
	tree argvec = NULL_TREE;
	tree *friends;
	tree gen_tmpl;
	tree type;
	int member;
	int args_depth;
	int parms_depth;

	/* Nobody should be tsubst'ing into non-template functions.  */
	gcc_assert (DECL_TEMPLATE_INFO (t) != NULL_TREE);

	if (TREE_CODE (DECL_TI_TEMPLATE (t)) == TEMPLATE_DECL)
	  {
	    tree spec;
	    bool dependent_p;

	    /* If T is not dependent, just return it.  We have to
	       increment PROCESSING_TEMPLATE_DECL because
	       value_dependent_expression_p assumes that nothing is
	       dependent when PROCESSING_TEMPLATE_DECL is zero.  */
	    ++processing_template_decl;
	    dependent_p = value_dependent_expression_p (t);
	    --processing_template_decl;
	    if (!dependent_p)
	      return t;

	    /* Calculate the most general template of which R is a
	       specialization, and the complete set of arguments used to
	       specialize R.  */
	    gen_tmpl = most_general_template (DECL_TI_TEMPLATE (t));
	    argvec = tsubst_template_args (DECL_TI_ARGS
					   (DECL_TEMPLATE_RESULT (gen_tmpl)),
					   args, complain, in_decl);

	    /* Check to see if we already have this specialization.  */
	    spec = retrieve_specialization (gen_tmpl, argvec,
					    /*class_specializations_p=*/false);

	    if (spec)
	      {
		r = spec;
		break;
	      }

	    /* We can see more levels of arguments than parameters if
	       there was a specialization of a member template, like
	       this:

		 template <class T> struct S { template <class U> void f(); }
		 template <> template <class U> void S<int>::f(U);

	       Here, we'll be substituting into the specialization,
	       because that's where we can find the code we actually
	       want to generate, but we'll have enough arguments for
	       the most general template.

	       We also deal with the peculiar case:

		 template <class T> struct S {
		   template <class U> friend void f();
		 };
		 template <class U> void f() {}
		 template S<int>;
		 template void f<double>();

	       Here, the ARGS for the instantiation of will be {int,
	       double}.  But, we only need as many ARGS as there are
	       levels of template parameters in CODE_PATTERN.  We are
	       careful not to get fooled into reducing the ARGS in
	       situations like:

		 template <class T> struct S { template <class U> void f(U); }
		 template <class T> template <> void S<T>::f(int) {}

	       which we can spot because the pattern will be a
	       specialization in this case.  */
	    args_depth = TMPL_ARGS_DEPTH (args);
	    parms_depth =
	      TMPL_PARMS_DEPTH (DECL_TEMPLATE_PARMS (DECL_TI_TEMPLATE (t)));
	    if (args_depth > parms_depth
		&& !DECL_TEMPLATE_SPECIALIZATION (t))
	      args = get_innermost_template_args (args, parms_depth);
	  }
	else
	  {
	    /* This special case arises when we have something like this:

		 template <class T> struct S {
		   friend void f<int>(int, double);
		 };

	       Here, the DECL_TI_TEMPLATE for the friend declaration
	       will be an IDENTIFIER_NODE.  We are being called from
	       tsubst_friend_function, and we want only to create a
	       new decl (R) with appropriate types so that we can call
	       determine_specialization.  */
	    gen_tmpl = NULL_TREE;
	  }

	if (DECL_CLASS_SCOPE_P (t))
	  {
	    if (DECL_NAME (t) == constructor_name (DECL_CONTEXT (t)))
	      member = 2;
	    else
	      member = 1;
	    ctx = tsubst_aggr_type (DECL_CONTEXT (t), args,
				    complain, t, /*entering_scope=*/1);
	  }
	else
	  {
	    member = 0;
	    ctx = DECL_CONTEXT (t);
	  }
	type = tsubst (TREE_TYPE (t), args, complain, in_decl);
	if (type == error_mark_node)
	  return error_mark_node;

	/* We do NOT check for matching decls pushed separately at this
	   point, as they may not represent instantiations of this
	   template, and in any case are considered separate under the
	   discrete model.  */
	r = copy_decl (t);
	DECL_USE_TEMPLATE (r) = 0;
	TREE_TYPE (r) = type;
	/* Clear out the mangled name and RTL for the instantiation.  */
	SET_DECL_ASSEMBLER_NAME (r, NULL_TREE);
	SET_DECL_RTL (r, NULL_RTX);
	DECL_INITIAL (r) = NULL_TREE;
	DECL_CONTEXT (r) = ctx;

	if (member && DECL_CONV_FN_P (r))
	  /* Type-conversion operator.  Reconstruct the name, in
	     case it's the name of one of the template's parameters.  */
	  DECL_NAME (r) = mangle_conv_op_name_for_type (TREE_TYPE (type));

	DECL_ARGUMENTS (r) = tsubst (DECL_ARGUMENTS (t), args,
				     complain, t);
	DECL_RESULT (r) = NULL_TREE;

	TREE_STATIC (r) = 0;
	TREE_PUBLIC (r) = TREE_PUBLIC (t);
	DECL_EXTERNAL (r) = 1;
	/* If this is an instantiation of a function with internal
	   linkage, we already know what object file linkage will be
	   assigned to the instantiation.  */
	DECL_INTERFACE_KNOWN (r) = !TREE_PUBLIC (r);
	DECL_DEFER_OUTPUT (r) = 0;
	TREE_CHAIN (r) = NULL_TREE;
	DECL_PENDING_INLINE_INFO (r) = 0;
	DECL_PENDING_INLINE_P (r) = 0;
	DECL_SAVED_TREE (r) = NULL_TREE;
	TREE_USED (r) = 0;
	if (DECL_CLONED_FUNCTION (r))
	  {
	    DECL_CLONED_FUNCTION (r) = tsubst (DECL_CLONED_FUNCTION (t),
					       args, complain, t);
	    TREE_CHAIN (r) = TREE_CHAIN (DECL_CLONED_FUNCTION (r));
	    TREE_CHAIN (DECL_CLONED_FUNCTION (r)) = r;
	  }

	/* Set up the DECL_TEMPLATE_INFO for R.  There's no need to do
	   this in the special friend case mentioned above where
	   GEN_TMPL is NULL.  */
	if (gen_tmpl)
	  {
	    DECL_TEMPLATE_INFO (r)
	      = tree_cons (gen_tmpl, argvec, NULL_TREE);
	    SET_DECL_IMPLICIT_INSTANTIATION (r);
	    register_specialization (r, gen_tmpl, argvec, false);

	    /* We're not supposed to instantiate default arguments
	       until they are called, for a template.  But, for a
	       declaration like:

		 template <class T> void f ()
		 { extern void g(int i = T()); }

	       we should do the substitution when the template is
	       instantiated.  We handle the member function case in
	       instantiate_class_template since the default arguments
	       might refer to other members of the class.  */
	    if (!member
		&& !PRIMARY_TEMPLATE_P (gen_tmpl)
		&& !uses_template_parms (argvec))
	      tsubst_default_arguments (r);
	  }
	else
	  DECL_TEMPLATE_INFO (r) = NULL_TREE;

	/* Copy the list of befriending classes.  */
	for (friends = &DECL_BEFRIENDING_CLASSES (r);
	     *friends;
	     friends = &TREE_CHAIN (*friends))
	  {
	    *friends = copy_node (*friends);
	    TREE_VALUE (*friends) = tsubst (TREE_VALUE (*friends),
					    args, complain,
					    in_decl);
	  }

	if (DECL_CONSTRUCTOR_P (r) || DECL_DESTRUCTOR_P (r))
	  {
	    maybe_retrofit_in_chrg (r);
	    if (DECL_CONSTRUCTOR_P (r))
	      grok_ctor_properties (ctx, r);
	    /* If this is an instantiation of a member template, clone it.
	       If it isn't, that'll be handled by
	       clone_constructors_and_destructors.  */
	    if (PRIMARY_TEMPLATE_P (gen_tmpl))
	      clone_function_decl (r, /*update_method_vec_p=*/0);
	  }
	else if (IDENTIFIER_OPNAME_P (DECL_NAME (r))
		 && !grok_op_properties (r, (complain & tf_error) != 0))
	  return error_mark_node;

	if (DECL_FRIEND_P (t) && DECL_FRIEND_CONTEXT (t))
	  SET_DECL_FRIEND_CONTEXT (r,
				   tsubst (DECL_FRIEND_CONTEXT (t),
					    args, complain, in_decl));

	/* Possibly limit visibility based on template args.  */
	DECL_VISIBILITY (r) = VISIBILITY_DEFAULT;
	if (DECL_VISIBILITY_SPECIFIED (t))
	  {
	    DECL_VISIBILITY_SPECIFIED (r) = 0;
	    DECL_ATTRIBUTES (r)
	      = remove_attribute ("visibility", DECL_ATTRIBUTES (r));
	  }
	determine_visibility (r);
      }
      break;

    case PARM_DECL:
      {
	tree type;

	r = copy_node (t);
	if (DECL_TEMPLATE_PARM_P (t))
	  SET_DECL_TEMPLATE_PARM_P (r);

	type = tsubst (TREE_TYPE (t), args, complain, in_decl);
	type = type_decays_to (type);
	TREE_TYPE (r) = type;
	cp_apply_type_quals_to_decl (cp_type_quals (type), r);

	if (DECL_INITIAL (r))
	  {
	    if (TREE_CODE (DECL_INITIAL (r)) != TEMPLATE_PARM_INDEX)
	      DECL_INITIAL (r) = TREE_TYPE (r);
	    else
	      DECL_INITIAL (r) = tsubst (DECL_INITIAL (r), args,
					 complain, in_decl);
	  }

	DECL_CONTEXT (r) = NULL_TREE;

	if (!DECL_TEMPLATE_PARM_P (r))
	  DECL_ARG_TYPE (r) = type_passed_as (type);
	if (TREE_CHAIN (t))
	  TREE_CHAIN (r) = tsubst (TREE_CHAIN (t), args,
				   complain, TREE_CHAIN (t));
      }
      break;

    case FIELD_DECL:
      {
	tree type;

	r = copy_decl (t);
	type = tsubst (TREE_TYPE (t), args, complain, in_decl);
	if (type == error_mark_node)
	  return error_mark_node;
	TREE_TYPE (r) = type;
	cp_apply_type_quals_to_decl (cp_type_quals (type), r);

	/* DECL_INITIAL gives the number of bits in a bit-field.  */
	DECL_INITIAL (r)
	  = tsubst_expr (DECL_INITIAL (t), args,
			 complain, in_decl,
			 /*integral_constant_expression_p=*/true);
	/* We don't have to set DECL_CONTEXT here; it is set by
	   finish_member_declaration.  */
	TREE_CHAIN (r) = NULL_TREE;
	if (VOID_TYPE_P (type))
	  error ("instantiation of %q+D as type %qT", r, type);
      }
      break;

    case USING_DECL:
      /* We reach here only for member using decls.  */
      if (DECL_DEPENDENT_P (t))
	{
	  r = do_class_using_decl
	    (tsubst_copy (USING_DECL_SCOPE (t), args, complain, in_decl),
	     tsubst_copy (DECL_NAME (t), args, complain, in_decl));
	  if (!r)
	    r = error_mark_node;
	}
      else
	{
	  r = copy_node (t);
	  TREE_CHAIN (r) = NULL_TREE;
	}
      break;

    case TYPE_DECL:
    case VAR_DECL:
      {
	tree argvec = NULL_TREE;
	tree gen_tmpl = NULL_TREE;
	tree spec;
	tree tmpl = NULL_TREE;
	tree ctx;
	tree type = NULL_TREE;
	bool local_p;

	if (TREE_CODE (t) == TYPE_DECL)
	  {
	    type = tsubst (TREE_TYPE (t), args, complain, in_decl);
	    if (TREE_CODE (type) == TEMPLATE_TEMPLATE_PARM
		|| t == TYPE_MAIN_DECL (TREE_TYPE (t)))
	      {
		/* If this is the canonical decl, we don't have to
		   mess with instantiations, and often we can't (for
		   typename, template type parms and such).  Note that
		   TYPE_NAME is not correct for the above test if
		   we've copied the type for a typedef.  */
		r = TYPE_NAME (type);
		break;
	      }
	  }

	/* Check to see if we already have the specialization we
	   need.  */
	spec = NULL_TREE;
	if (DECL_CLASS_SCOPE_P (t) || DECL_NAMESPACE_SCOPE_P (t))
	  {
	    /* T is a static data member or namespace-scope entity.
	       We have to substitute into namespace-scope variables
	       (even though such entities are never templates) because
	       of cases like:
	       
	         template <class T> void f() { extern T t; }

	       where the entity referenced is not known until
	       instantiation time.  */
	    local_p = false;
	    ctx = DECL_CONTEXT (t);
	    if (DECL_CLASS_SCOPE_P (t))
	      {
		ctx = tsubst_aggr_type (ctx, args,
					complain,
					in_decl, /*entering_scope=*/1);
		/* If CTX is unchanged, then T is in fact the
		   specialization we want.  That situation occurs when
		   referencing a static data member within in its own
		   class.  We can use pointer equality, rather than
		   same_type_p, because DECL_CONTEXT is always
		   canonical.  */
		if (ctx == DECL_CONTEXT (t))
		  spec = t;
	      }

	    if (!spec)
	      {
		tmpl = DECL_TI_TEMPLATE (t);
		gen_tmpl = most_general_template (tmpl);
		argvec = tsubst (DECL_TI_ARGS (t), args, complain, in_decl);
		spec = (retrieve_specialization 
			(gen_tmpl, argvec,
			 /*class_specializations_p=*/false));
	      }
	  }
	else
	  {
	    /* A local variable.  */
	    local_p = true;
	    /* Subsequent calls to pushdecl will fill this in.  */
	    ctx = NULL_TREE;
	    spec = retrieve_local_specialization (t);
	  }
	/* If we already have the specialization we need, there is
	   nothing more to do.  */ 
	if (spec)
	  {
	    r = spec;
	    break;
	  }

	/* Create a new node for the specialization we need.  */
	r = copy_decl (t);
	if (TREE_CODE (r) == VAR_DECL)
	  {
	    /* Even if the original location is out of scope, the
	       newly substituted one is not.  */
	    DECL_DEAD_FOR_LOCAL (r) = 0;
	    DECL_INITIALIZED_P (r) = 0;
	    DECL_TEMPLATE_INSTANTIATED (r) = 0;
	    type = tsubst (TREE_TYPE (t), args, complain, in_decl);
	    if (type == error_mark_node)
	      return error_mark_node;
	    if (TREE_CODE (type) == FUNCTION_TYPE)
	      {
		/* It may seem that this case cannot occur, since:

		     typedef void f();
		     void g() { f x; }

		   declares a function, not a variable.  However:
      
		     typedef void f();
		     template <typename T> void g() { T t; }
		     template void g<f>();

		   is an attempt to declare a variable with function
		   type.  */
		error ("variable %qD has function type",
		       /* R is not yet sufficiently initialized, so we
			  just use its name.  */
		       DECL_NAME (r));
		return error_mark_node;
	      }
	    type = complete_type (type);
	    DECL_INITIALIZED_BY_CONSTANT_EXPRESSION_P (r)
	      = DECL_INITIALIZED_BY_CONSTANT_EXPRESSION_P (t);
	    type = check_var_type (DECL_NAME (r), type);

	    if (DECL_HAS_VALUE_EXPR_P (t))
	      {
		tree ve = DECL_VALUE_EXPR (t);
		ve = tsubst_expr (ve, args, complain, in_decl,
				  /*constant_expression_p=*/false);
		SET_DECL_VALUE_EXPR (r, ve);
	      }
	  }
	else if (DECL_SELF_REFERENCE_P (t))
	  SET_DECL_SELF_REFERENCE_P (r);
	TREE_TYPE (r) = type;
	cp_apply_type_quals_to_decl (cp_type_quals (type), r);
	DECL_CONTEXT (r) = ctx;
	/* Clear out the mangled name and RTL for the instantiation.  */
	SET_DECL_ASSEMBLER_NAME (r, NULL_TREE);
	if (CODE_CONTAINS_STRUCT (TREE_CODE (t), TS_DECL_WRTL))
	  SET_DECL_RTL (r, NULL_RTX);
	/* The initializer must not be expanded until it is required;
	   see [temp.inst].  */
	DECL_INITIAL (r) = NULL_TREE;
	if (CODE_CONTAINS_STRUCT (TREE_CODE (t), TS_DECL_WRTL))
	  SET_DECL_RTL (r, NULL_RTX);
	DECL_SIZE (r) = DECL_SIZE_UNIT (r) = 0;
	if (TREE_CODE (r) == VAR_DECL)
	  {
	    /* Possibly limit visibility based on template args.  */
	    DECL_VISIBILITY (r) = VISIBILITY_DEFAULT;
	    if (DECL_VISIBILITY_SPECIFIED (t))
	      {
		DECL_VISIBILITY_SPECIFIED (r) = 0;
		DECL_ATTRIBUTES (r)
		  = remove_attribute ("visibility", DECL_ATTRIBUTES (r));
	      }
	    determine_visibility (r);
	  }

	if (!local_p)
	  {
	    /* A static data member declaration is always marked
	       external when it is declared in-class, even if an
	       initializer is present.  We mimic the non-template
	       processing here.  */
	    DECL_EXTERNAL (r) = 1;

	    register_specialization (r, gen_tmpl, argvec, false);
	    DECL_TEMPLATE_INFO (r) = tree_cons (tmpl, argvec, NULL_TREE);
	    SET_DECL_IMPLICIT_INSTANTIATION (r);
	  }
	else
	  register_local_specialization (r, t);

	TREE_CHAIN (r) = NULL_TREE;
	layout_decl (r, 0);
      }
      break;

    default:
      gcc_unreachable ();
    }

  /* Restore the file and line information.  */
  input_location = saved_loc;

  return r;
}

/* Substitute into the ARG_TYPES of a function type.  */

static tree
tsubst_arg_types (tree arg_types,
		  tree args,
		  tsubst_flags_t complain,
		  tree in_decl)
{
  tree remaining_arg_types;
  tree type;
  tree default_arg;
  tree result = NULL_TREE;

  if (!arg_types || arg_types == void_list_node)
    return arg_types;

  remaining_arg_types = tsubst_arg_types (TREE_CHAIN (arg_types),
					  args, complain, in_decl);
  if (remaining_arg_types == error_mark_node)
    return error_mark_node;

  type = tsubst (TREE_VALUE (arg_types), args, complain, in_decl);
  if (type == error_mark_node)
    return error_mark_node;
  if (VOID_TYPE_P (type))
    {
      if (complain & tf_error)
	{
	  error ("invalid parameter type %qT", type);
	  if (in_decl)
	    error ("in declaration %q+D", in_decl);
	}
      return error_mark_node;
    }

  /* Do array-to-pointer, function-to-pointer conversion, and ignore
     top-level qualifiers as required.  */
  type = TYPE_MAIN_VARIANT (type_decays_to (type));

  /* We do not substitute into default arguments here.  The standard
     mandates that they be instantiated only when needed, which is
     done in build_over_call.  */
  default_arg = TREE_PURPOSE (arg_types);

  if (default_arg && TREE_CODE (default_arg) == DEFAULT_ARG)
    {
      /* We've instantiated a template before its default arguments
	 have been parsed.  This can happen for a nested template
	 class, and is not an error unless we require the default
	 argument in a call of this function.  */
      result = tree_cons (default_arg, type, remaining_arg_types);
      VEC_safe_push (tree, gc, DEFARG_INSTANTIATIONS (default_arg), result);
    }
  else
    result = hash_tree_cons (default_arg, type, remaining_arg_types);

  return result;
}

/* Substitute into a FUNCTION_TYPE or METHOD_TYPE.  This routine does
   *not* handle the exception-specification for FNTYPE, because the
   initial substitution of explicitly provided template parameters
   during argument deduction forbids substitution into the
   exception-specification:

     [temp.deduct]

     All references in the function type of the function template to  the
     corresponding template parameters are replaced by the specified tem-
     plate argument values.  If a substitution in a template parameter or
     in  the function type of the function template results in an invalid
     type, type deduction fails.  [Note: The equivalent  substitution  in
     exception specifications is done only when the function is instanti-
     ated, at which point a program is  ill-formed  if  the  substitution
     results in an invalid type.]  */

static tree
tsubst_function_type (tree t,
		      tree args,
		      tsubst_flags_t complain,
		      tree in_decl)
{
  tree return_type;
  tree arg_types;
  tree fntype;

  /* The TYPE_CONTEXT is not used for function/method types.  */
  gcc_assert (TYPE_CONTEXT (t) == NULL_TREE);

  /* Substitute the return type.  */
  return_type = tsubst (TREE_TYPE (t), args, complain, in_decl);
  if (return_type == error_mark_node)
    return error_mark_node;
  /* The standard does not presently indicate that creation of a
     function type with an invalid return type is a deduction failure.
     However, that is clearly analogous to creating an array of "void"
     or a reference to a reference.  This is core issue #486.  */
  if (TREE_CODE (return_type) == ARRAY_TYPE
      || TREE_CODE (return_type) == FUNCTION_TYPE)
    {
      if (complain & tf_error)
	{
	  if (TREE_CODE (return_type) == ARRAY_TYPE)
	    error ("function returning an array");
	  else
	    error ("function returning a function");
	}
      return error_mark_node;
    }

  /* Substitute the argument types.  */
  arg_types = tsubst_arg_types (TYPE_ARG_TYPES (t), args,
				complain, in_decl);
  if (arg_types == error_mark_node)
    return error_mark_node;

  /* Construct a new type node and return it.  */
  if (TREE_CODE (t) == FUNCTION_TYPE)
    fntype = build_function_type (return_type, arg_types);
  else
    {
      tree r = TREE_TYPE (TREE_VALUE (arg_types));
      if (! IS_AGGR_TYPE (r))
	{
	  /* [temp.deduct]

	     Type deduction may fail for any of the following
	     reasons:

	     -- Attempting to create "pointer to member of T" when T
	     is not a class type.  */
	  if (complain & tf_error)
	    error ("creating pointer to member function of non-class type %qT",
		      r);
	  return error_mark_node;
	}

      fntype = build_method_type_directly (r, return_type,
					   TREE_CHAIN (arg_types));
    }
  fntype = cp_build_qualified_type_real (fntype, TYPE_QUALS (t), complain);
  fntype = cp_build_type_attribute_variant (fntype, TYPE_ATTRIBUTES (t));

  return fntype;
}

/* FNTYPE is a FUNCTION_TYPE or METHOD_TYPE.  Substitute the template
   ARGS into that specification, and return the substituted
   specification.  If there is no specification, return NULL_TREE.  */

static tree
tsubst_exception_specification (tree fntype,
				tree args,
				tsubst_flags_t complain,
				tree in_decl)
{
  tree specs;
  tree new_specs;

  specs = TYPE_RAISES_EXCEPTIONS (fntype);
  new_specs = NULL_TREE;
  if (specs)
    {
      if (! TREE_VALUE (specs))
	new_specs = specs;
      else
	while (specs)
	  {
	    tree spec;
	    spec = tsubst (TREE_VALUE (specs), args, complain, in_decl);
	    if (spec == error_mark_node)
	      return spec;
	    new_specs = add_exception_specifier (new_specs, spec, complain);
	    specs = TREE_CHAIN (specs);
	  }
    }
  return new_specs;
}

/* Take the tree structure T and replace template parameters used
   therein with the argument vector ARGS.  IN_DECL is an associated
   decl for diagnostics.  If an error occurs, returns ERROR_MARK_NODE.
   Issue error and warning messages under control of COMPLAIN.  Note
   that we must be relatively non-tolerant of extensions here, in
   order to preserve conformance; if we allow substitutions that
   should not be allowed, we may allow argument deductions that should
   not succeed, and therefore report ambiguous overload situations
   where there are none.  In theory, we could allow the substitution,
   but indicate that it should have failed, and allow our caller to
   make sure that the right thing happens, but we don't try to do this
   yet.

   This function is used for dealing with types, decls and the like;
   for expressions, use tsubst_expr or tsubst_copy.  */

static tree
tsubst (tree t, tree args, tsubst_flags_t complain, tree in_decl)
{
  tree type, r;

  if (t == NULL_TREE || t == error_mark_node
      || t == integer_type_node
      || t == void_type_node
      || t == char_type_node
      || t == unknown_type_node
      || TREE_CODE (t) == NAMESPACE_DECL)
    return t;

  if (DECL_P (t))
    return tsubst_decl (t, args, complain);

  if (TREE_CODE (t) == IDENTIFIER_NODE)
    type = IDENTIFIER_TYPE_VALUE (t);
  else
    type = TREE_TYPE (t);

  gcc_assert (type != unknown_type_node);

  if (type
      && TREE_CODE (t) != TYPENAME_TYPE
      && TREE_CODE (t) != IDENTIFIER_NODE
      && TREE_CODE (t) != FUNCTION_TYPE
      && TREE_CODE (t) != METHOD_TYPE)
    type = tsubst (type, args, complain, in_decl);
  if (type == error_mark_node)
    return error_mark_node;

  switch (TREE_CODE (t))
    {
    case RECORD_TYPE:
    case UNION_TYPE:
    case ENUMERAL_TYPE:
      return tsubst_aggr_type (t, args, complain, in_decl,
			       /*entering_scope=*/0);

    case ERROR_MARK:
    case IDENTIFIER_NODE:
    case VOID_TYPE:
    case REAL_TYPE:
    case COMPLEX_TYPE:
    case VECTOR_TYPE:
    case BOOLEAN_TYPE:
    case INTEGER_CST:
    case REAL_CST:
    case STRING_CST:
      return t;

    case INTEGER_TYPE:
      if (t == integer_type_node)
	return t;

      if (TREE_CODE (TYPE_MIN_VALUE (t)) == INTEGER_CST
	  && TREE_CODE (TYPE_MAX_VALUE (t)) == INTEGER_CST)
	return t;

      {
	tree max, omax = TREE_OPERAND (TYPE_MAX_VALUE (t), 0);

	max = tsubst_expr (omax, args, complain, in_decl,
			   /*integral_constant_expression_p=*/false);
	max = fold_decl_constant_value (max);

	if (TREE_CODE (max) != INTEGER_CST 
	    && TREE_CODE (max) != TEMPLATE_PARM_INDEX
	    && !at_function_scope_p ())
	  {
	    if (complain & tf_error)
	      error ("array bound is not an integer constant");
	    return error_mark_node;
	  }

	/* [temp.deduct]

	   Type deduction may fail for any of the following
	   reasons:

	     Attempting to create an array with a size that is
	     zero or negative.  */
	if (integer_zerop (max) && !(complain & tf_error))
	  /* We must fail if performing argument deduction (as
	     indicated by the state of complain), so that
	     another substitution can be found.  */
	  return error_mark_node;
	else if (TREE_CODE (max) == INTEGER_CST
		 && INT_CST_LT (max, integer_zero_node))
	  {
	    if (complain & tf_error)
	      error ("creating array with negative size (%qE)", max);

	    return error_mark_node;
	  }

	return compute_array_index_type (NULL_TREE, max);
      }

    case TEMPLATE_TYPE_PARM:
    case TEMPLATE_TEMPLATE_PARM:
    case BOUND_TEMPLATE_TEMPLATE_PARM:
    case TEMPLATE_PARM_INDEX:
      {
	int idx;
	int level;
	int levels;
	tree arg = NULL_TREE;

	r = NULL_TREE;

	gcc_assert (TREE_VEC_LENGTH (args) > 0);
	if (TREE_CODE (t) == TEMPLATE_TYPE_PARM
	    || TREE_CODE (t) == TEMPLATE_TEMPLATE_PARM
	    || TREE_CODE (t) == BOUND_TEMPLATE_TEMPLATE_PARM)
	  {
	    idx = TEMPLATE_TYPE_IDX (t);
	    level = TEMPLATE_TYPE_LEVEL (t);
	  }
	else
	  {
	    idx = TEMPLATE_PARM_IDX (t);
	    level = TEMPLATE_PARM_LEVEL (t);
	  }

	levels = TMPL_ARGS_DEPTH (args);
	if (level <= levels)
	  arg = TMPL_ARG (args, level, idx);

	if (arg == error_mark_node)
	  return error_mark_node;
	else if (arg != NULL_TREE)
	  {
	    if (TREE_CODE (t) == TEMPLATE_TYPE_PARM)
	      {
		int quals;
		gcc_assert (TYPE_P (arg));

		/* cv-quals from the template are discarded when
		   substituting in a function or reference type.  */
		if (TREE_CODE (arg) == FUNCTION_TYPE
		    || TREE_CODE (arg) == METHOD_TYPE
		    || TREE_CODE (arg) == REFERENCE_TYPE)
		  quals = cp_type_quals (arg);
		else
		  quals = cp_type_quals (arg) | cp_type_quals (t);
		  
		return cp_build_qualified_type_real
		  (arg, quals, complain | tf_ignore_bad_quals);
	      }
	    else if (TREE_CODE (t) == BOUND_TEMPLATE_TEMPLATE_PARM)
	      {
		/* We are processing a type constructed from a
		   template template parameter.  */
		tree argvec = tsubst (TYPE_TI_ARGS (t),
				      args, complain, in_decl);
		if (argvec == error_mark_node)
		  return error_mark_node;

		/* We can get a TEMPLATE_TEMPLATE_PARM here when we
		   are resolving nested-types in the signature of a
		   member function templates.  Otherwise ARG is a
		   TEMPLATE_DECL and is the real template to be
		   instantiated.  */
		if (TREE_CODE (arg) == TEMPLATE_TEMPLATE_PARM)
		  arg = TYPE_NAME (arg);

		r = lookup_template_class (arg,
					   argvec, in_decl,
					   DECL_CONTEXT (arg),
					    /*entering_scope=*/0,
					   complain);
		return cp_build_qualified_type_real
		  (r, TYPE_QUALS (t), complain);
	      }
	    else
	      /* TEMPLATE_TEMPLATE_PARM or TEMPLATE_PARM_INDEX.  */
	      return arg;
	  }

	if (level == 1)
	  /* This can happen during the attempted tsubst'ing in
	     unify.  This means that we don't yet have any information
	     about the template parameter in question.  */
	  return t;

	/* If we get here, we must have been looking at a parm for a
	   more deeply nested template.  Make a new version of this
	   template parameter, but with a lower level.  */
	switch (TREE_CODE (t))
	  {
	  case TEMPLATE_TYPE_PARM:
	  case TEMPLATE_TEMPLATE_PARM:
	  case BOUND_TEMPLATE_TEMPLATE_PARM:
	    if (cp_type_quals (t))
	      {
		r = tsubst (TYPE_MAIN_VARIANT (t), args, complain, in_decl);
		r = cp_build_qualified_type_real
		  (r, cp_type_quals (t),
		   complain | (TREE_CODE (t) == TEMPLATE_TYPE_PARM
			       ? tf_ignore_bad_quals : 0));
	      }
	    else
	      {
		r = copy_type (t);
		TEMPLATE_TYPE_PARM_INDEX (r)
		  = reduce_template_parm_level (TEMPLATE_TYPE_PARM_INDEX (t),
						r, levels);
		TYPE_STUB_DECL (r) = TYPE_NAME (r) = TEMPLATE_TYPE_DECL (r);
		TYPE_MAIN_VARIANT (r) = r;
		TYPE_POINTER_TO (r) = NULL_TREE;
		TYPE_REFERENCE_TO (r) = NULL_TREE;

		if (TREE_CODE (t) == BOUND_TEMPLATE_TEMPLATE_PARM)
		  {
		    tree argvec = tsubst (TYPE_TI_ARGS (t), args,
					  complain, in_decl);
		    if (argvec == error_mark_node)
		      return error_mark_node;

		    TEMPLATE_TEMPLATE_PARM_TEMPLATE_INFO (r)
		      = tree_cons (TYPE_TI_TEMPLATE (t), argvec, NULL_TREE);
		  }
	      }
	    break;

	  case TEMPLATE_PARM_INDEX:
	    r = reduce_template_parm_level (t, type, levels);
	    break;

	  default:
	    gcc_unreachable ();
	  }

	return r;
      }

    case TREE_LIST:
      {
	tree purpose, value, chain;

	if (t == void_list_node)
	  return t;

	purpose = TREE_PURPOSE (t);
	if (purpose)
	  {
	    purpose = tsubst (purpose, args, complain, in_decl);
	    if (purpose == error_mark_node)
	      return error_mark_node;
	  }
	value = TREE_VALUE (t);
	if (value)
	  {
	    value = tsubst (value, args, complain, in_decl);
	    if (value == error_mark_node)
	      return error_mark_node;
	  }
	chain = TREE_CHAIN (t);
	if (chain && chain != void_type_node)
	  {
	    chain = tsubst (chain, args, complain, in_decl);
	    if (chain == error_mark_node)
	      return error_mark_node;
	  }
	if (purpose == TREE_PURPOSE (t)
	    && value == TREE_VALUE (t)
	    && chain == TREE_CHAIN (t))
	  return t;
	return hash_tree_cons (purpose, value, chain);
      }

    case TREE_BINFO:
      /* We should never be tsubsting a binfo.  */
      gcc_unreachable ();

    case TREE_VEC:
      /* A vector of template arguments.  */
      gcc_assert (!type);
      return tsubst_template_args (t, args, complain, in_decl);

    case POINTER_TYPE:
    case REFERENCE_TYPE:
      {
	enum tree_code code;

	if (type == TREE_TYPE (t) && TREE_CODE (type) != METHOD_TYPE)
	  return t;

	code = TREE_CODE (t);


	/* [temp.deduct]

	   Type deduction may fail for any of the following
	   reasons:

	   -- Attempting to create a pointer to reference type.
	   -- Attempting to create a reference to a reference type or
	      a reference to void.  */
	if (TREE_CODE (type) == REFERENCE_TYPE
	    || (code == REFERENCE_TYPE && TREE_CODE (type) == VOID_TYPE))
	  {
	    static location_t last_loc;

	    /* We keep track of the last time we issued this error
	       message to avoid spewing a ton of messages during a
	       single bad template instantiation.  */
	    if (complain & tf_error
#ifdef USE_MAPPED_LOCATION
		&& last_loc != input_location
#else
		&& (last_loc.line != input_line
		    || last_loc.file != input_filename)
#endif
		  )
	      {
		if (TREE_CODE (type) == VOID_TYPE)
		  error ("forming reference to void");
		else
		  error ("forming %s to reference type %qT",
			 (code == POINTER_TYPE) ? "pointer" : "reference",
			 type);
		last_loc = input_location;
	      }

	    return error_mark_node;
	  }
	else if (code == POINTER_TYPE)
	  {
	    r = build_pointer_type (type);
	    if (TREE_CODE (type) == METHOD_TYPE)
	      r = build_ptrmemfunc_type (r);
	  }
	else
	  r = build_reference_type (type);
	r = cp_build_qualified_type_real (r, TYPE_QUALS (t), complain);

	if (r != error_mark_node)
	  /* Will this ever be needed for TYPE_..._TO values?  */
	  layout_type (r);

	return r;
      }
    case OFFSET_TYPE:
      {
	r = tsubst (TYPE_OFFSET_BASETYPE (t), args, complain, in_decl);
	if (r == error_mark_node || !IS_AGGR_TYPE (r))
	  {
	    /* [temp.deduct]

	       Type deduction may fail for any of the following
	       reasons:

	       -- Attempting to create "pointer to member of T" when T
		  is not a class type.  */
	    if (complain & tf_error)
	      error ("creating pointer to member of non-class type %qT", r);
	    return error_mark_node;
	  }
	if (TREE_CODE (type) == REFERENCE_TYPE)
	  {
	    if (complain & tf_error)
	      error ("creating pointer to member reference type %qT", type);
	    return error_mark_node;
	  }
	if (TREE_CODE (type) == VOID_TYPE)
	  {
	    if (complain & tf_error)
	      error ("creating pointer to member of type void");
	    return error_mark_node;
	  }
	gcc_assert (TREE_CODE (type) != METHOD_TYPE);
	if (TREE_CODE (type) == FUNCTION_TYPE)
	  {
	    /* The type of the implicit object parameter gets its
	       cv-qualifiers from the FUNCTION_TYPE. */
	    tree method_type;
	    tree this_type = cp_build_qualified_type (TYPE_MAIN_VARIANT (r),
						      cp_type_quals (type));
	    tree memptr;
	    method_type = build_method_type_directly (this_type,
						      TREE_TYPE (type),
						      TYPE_ARG_TYPES (type));
	    memptr = build_ptrmemfunc_type (build_pointer_type (method_type));
	    return cp_build_qualified_type_real (memptr, cp_type_quals (t),
						 complain);
	  }
	else
	  return cp_build_qualified_type_real (build_ptrmem_type (r, type),
					       TYPE_QUALS (t),
					       complain);
      }
    case FUNCTION_TYPE:
    case METHOD_TYPE:
      {
	tree fntype;
	tree specs;
	fntype = tsubst_function_type (t, args, complain, in_decl);
	if (fntype == error_mark_node)
	  return error_mark_node;

	/* Substitute the exception specification.  */
	specs = tsubst_exception_specification (t, args, complain,
						in_decl);
	if (specs == error_mark_node)
	  return error_mark_node;
	if (specs)
	  fntype = build_exception_variant (fntype, specs);
	return fntype;
      }
    case ARRAY_TYPE:
      {
	tree domain = tsubst (TYPE_DOMAIN (t), args, complain, in_decl);
	if (domain == error_mark_node)
	  return error_mark_node;

	/* As an optimization, we avoid regenerating the array type if
	   it will obviously be the same as T.  */
	if (type == TREE_TYPE (t) && domain == TYPE_DOMAIN (t))
	  return t;

	/* These checks should match the ones in grokdeclarator.

	   [temp.deduct]

	   The deduction may fail for any of the following reasons:

	   -- Attempting to create an array with an element type that
	      is void, a function type, or a reference type, or [DR337]
	      an abstract class type.  */
	if (TREE_CODE (type) == VOID_TYPE
	    || TREE_CODE (type) == FUNCTION_TYPE
	    || TREE_CODE (type) == REFERENCE_TYPE)
	  {
	    if (complain & tf_error)
	      error ("creating array of %qT", type);
	    return error_mark_node;
	  }
	if (CLASS_TYPE_P (type) && CLASSTYPE_PURE_VIRTUALS (type))
	  {
	    if (complain & tf_error)
	      error ("creating array of %qT, which is an abstract class type",
		     type);
	    return error_mark_node;
	  }

	r = build_cplus_array_type (type, domain);
	return r;
      }

    case PLUS_EXPR:
    case MINUS_EXPR:
      {
	tree e1 = tsubst (TREE_OPERAND (t, 0), args, complain, in_decl);
	tree e2 = tsubst (TREE_OPERAND (t, 1), args, complain, in_decl);

	if (e1 == error_mark_node || e2 == error_mark_node)
	  return error_mark_node;

	return fold_build2 (TREE_CODE (t), TREE_TYPE (t), e1, e2);
      }

    case NEGATE_EXPR:
    case NOP_EXPR:
      {
	tree e = tsubst (TREE_OPERAND (t, 0), args, complain, in_decl);
	if (e == error_mark_node)
	  return error_mark_node;

	return fold_build1 (TREE_CODE (t), TREE_TYPE (t), e);
      }

    case TYPENAME_TYPE:
      {
	tree ctx = tsubst_aggr_type (TYPE_CONTEXT (t), args, complain,
				     in_decl, /*entering_scope=*/1);
	tree f = tsubst_copy (TYPENAME_TYPE_FULLNAME (t), args,
			      complain, in_decl);

	if (ctx == error_mark_node || f == error_mark_node)
	  return error_mark_node;

	if (!IS_AGGR_TYPE (ctx))
	  {
	    if (complain & tf_error)
	      error ("%qT is not a class, struct, or union type", ctx);
	    return error_mark_node;
	  }
	else if (!uses_template_parms (ctx) && !TYPE_BEING_DEFINED (ctx))
	  {
	    /* Normally, make_typename_type does not require that the CTX
	       have complete type in order to allow things like:

		 template <class T> struct S { typename S<T>::X Y; };

	       But, such constructs have already been resolved by this
	       point, so here CTX really should have complete type, unless
	       it's a partial instantiation.  */
	    ctx = complete_type (ctx);
	    if (!COMPLETE_TYPE_P (ctx))
	      {
		if (complain & tf_error)
		  cxx_incomplete_type_error (NULL_TREE, ctx);
		return error_mark_node;
	      }
	  }

	f = make_typename_type (ctx, f, typename_type,
				(complain & tf_error) | tf_keep_type_decl);
	if (f == error_mark_node)
	  return f;
	if (TREE_CODE (f) == TYPE_DECL)
	  {
	    complain |= tf_ignore_bad_quals;
	    f = TREE_TYPE (f);
	  }

	if (TREE_CODE (f) != TYPENAME_TYPE)
	  {
	    if (TYPENAME_IS_ENUM_P (t) && TREE_CODE (f) != ENUMERAL_TYPE)
	      error ("%qT resolves to %qT, which is not an enumeration type",
		     t, f);
	    else if (TYPENAME_IS_CLASS_P (t) && !CLASS_TYPE_P (f))
	      error ("%qT resolves to %qT, which is is not a class type",
		     t, f);
	  }

	return cp_build_qualified_type_real
	  (f, cp_type_quals (f) | cp_type_quals (t), complain);
      }

    case UNBOUND_CLASS_TEMPLATE:
      {
	tree ctx = tsubst_aggr_type (TYPE_CONTEXT (t), args, complain,
				     in_decl, /*entering_scope=*/1);
	tree name = TYPE_IDENTIFIER (t);
	tree parm_list = DECL_TEMPLATE_PARMS (TYPE_NAME (t));

	if (ctx == error_mark_node || name == error_mark_node)
	  return error_mark_node;

	if (parm_list)
	  parm_list = tsubst_template_parms (parm_list, args, complain);
	return make_unbound_class_template (ctx, name, parm_list, complain);
      }

    case INDIRECT_REF:
    case ADDR_EXPR:
    case CALL_EXPR:
      gcc_unreachable ();

    case ARRAY_REF:
      {
	tree e1 = tsubst (TREE_OPERAND (t, 0), args, complain, in_decl);
	tree e2 = tsubst_expr (TREE_OPERAND (t, 1), args, complain, in_decl,
			       /*integral_constant_expression_p=*/false);
	if (e1 == error_mark_node || e2 == error_mark_node)
	  return error_mark_node;

	return build_nt (ARRAY_REF, e1, e2, NULL_TREE, NULL_TREE);
      }

    case SCOPE_REF:
      {
	tree e1 = tsubst (TREE_OPERAND (t, 0), args, complain, in_decl);
	tree e2 = tsubst (TREE_OPERAND (t, 1), args, complain, in_decl);
	if (e1 == error_mark_node || e2 == error_mark_node)
	  return error_mark_node;

	return build_qualified_name (/*type=*/NULL_TREE,
				     e1, e2, QUALIFIED_NAME_IS_TEMPLATE (t));
      }

    case TYPEOF_TYPE:
      {
	tree type;

	type = finish_typeof (tsubst_expr 
			      (TYPEOF_TYPE_EXPR (t), args,
			       complain, in_decl,
			       /*integral_constant_expression_p=*/false));
	return cp_build_qualified_type_real (type,
					     cp_type_quals (t)
					     | cp_type_quals (type),
					     complain);
      }

      /* APPLE LOCAL begin blocks 6204446 */
    case BLOCK_POINTER_TYPE:
      return t;
      /* APPLE LOCAL end blocks 6204446 */

    default:
      sorry ("use of %qs in template",
	     tree_code_name [(int) TREE_CODE (t)]);
      return error_mark_node;
    }
}

/* Like tsubst_expr for a BASELINK.  OBJECT_TYPE, if non-NULL, is the
   type of the expression on the left-hand side of the "." or "->"
   operator.  */

static tree
tsubst_baselink (tree baselink, tree object_type,
		 tree args, tsubst_flags_t complain, tree in_decl)
{
    tree name;
    tree qualifying_scope;
    tree fns;
    tree optype;
    tree template_args = 0;
    bool template_id_p = false;

    /* A baselink indicates a function from a base class.  Both the
       BASELINK_ACCESS_BINFO and the base class referenced may
       indicate bases of the template class, rather than the
       instantiated class.  In addition, lookups that were not
       ambiguous before may be ambiguous now.  Therefore, we perform
       the lookup again.  */
    qualifying_scope = BINFO_TYPE (BASELINK_ACCESS_BINFO (baselink));
    qualifying_scope = tsubst (qualifying_scope, args,
			       complain, in_decl);
    fns = BASELINK_FUNCTIONS (baselink);
    optype = BASELINK_OPTYPE (baselink);
    if (TREE_CODE (fns) == TEMPLATE_ID_EXPR)
      {
	template_id_p = true;
	template_args = TREE_OPERAND (fns, 1);
	fns = TREE_OPERAND (fns, 0);
	if (template_args)
	  template_args = tsubst_template_args (template_args, args,
						complain, in_decl);
      }
    name = DECL_NAME (get_first_fn (fns));
    baselink = lookup_fnfields (qualifying_scope, name, /*protect=*/1);

    /* If lookup found a single function, mark it as used at this
       point.  (If it lookup found multiple functions the one selected
       later by overload resolution will be marked as used at that
       point.)  */
    if (BASELINK_P (baselink))
      fns = BASELINK_FUNCTIONS (baselink);
    if (!template_id_p && !really_overloaded_fn (fns))
      mark_used (OVL_CURRENT (fns));

    /* Add back the template arguments, if present.  */
    if (BASELINK_P (baselink) && template_id_p)
      BASELINK_FUNCTIONS (baselink)
	= build_nt (TEMPLATE_ID_EXPR,
		    BASELINK_FUNCTIONS (baselink),
		    template_args);
    /* Update the conversion operator type.  */
    BASELINK_OPTYPE (baselink) 
      = tsubst (optype, args, complain, in_decl);

    if (!object_type)
      object_type = current_class_type;
    return adjust_result_of_qualified_name_lookup (baselink,
						   qualifying_scope,
						   object_type);
}

/* Like tsubst_expr for a SCOPE_REF, given by QUALIFIED_ID.  DONE is
   true if the qualified-id will be a postfix-expression in-and-of
   itself; false if more of the postfix-expression follows the
   QUALIFIED_ID.  ADDRESS_P is true if the qualified-id is the operand
   of "&".  */

static tree
tsubst_qualified_id (tree qualified_id, tree args,
		     tsubst_flags_t complain, tree in_decl,
		     bool done, bool address_p)
{
  tree expr;
  tree scope;
  tree name;
  bool is_template;
  tree template_args;

  gcc_assert (TREE_CODE (qualified_id) == SCOPE_REF);

  /* Figure out what name to look up.  */
  name = TREE_OPERAND (qualified_id, 1);
  if (TREE_CODE (name) == TEMPLATE_ID_EXPR)
    {
      is_template = true;
      template_args = TREE_OPERAND (name, 1);
      if (template_args)
	template_args = tsubst_template_args (template_args, args,
					      complain, in_decl);
      name = TREE_OPERAND (name, 0);
    }
  else
    {
      is_template = false;
      template_args = NULL_TREE;
    }

  /* Substitute into the qualifying scope.  When there are no ARGS, we
     are just trying to simplify a non-dependent expression.  In that
     case the qualifying scope may be dependent, and, in any case,
     substituting will not help.  */
  scope = TREE_OPERAND (qualified_id, 0);
  if (args)
    {
      scope = tsubst (scope, args, complain, in_decl);
      expr = tsubst_copy (name, args, complain, in_decl);
    }
  else
    expr = name;

  if (dependent_type_p (scope))
    return build_qualified_name (/*type=*/NULL_TREE,
				 scope, expr,
				 QUALIFIED_NAME_IS_TEMPLATE (qualified_id));

  if (!BASELINK_P (name) && !DECL_P (expr))
    {
      if (TREE_CODE (expr) == BIT_NOT_EXPR)
	/* If this were actually a destructor call, it would have been
	   parsed as such by the parser.  */
	expr = error_mark_node;
      else
	expr = lookup_qualified_name (scope, expr, /*is_type_p=*/0, false);
      if (TREE_CODE (TREE_CODE (expr) == TEMPLATE_DECL
		     ? DECL_TEMPLATE_RESULT (expr) : expr) == TYPE_DECL)
	{
	  if (complain & tf_error)
	    {
	      error ("dependent-name %qE is parsed as a non-type, but "
		     "instantiation yields a type", qualified_id);
	      inform ("say %<typename %E%> if a type is meant", qualified_id);
	    }
	  return error_mark_node;
	}
    }

  if (DECL_P (expr))
    {
      check_accessibility_of_qualified_id (expr, /*object_type=*/NULL_TREE,
					   scope);
      /* Remember that there was a reference to this entity.  */
      mark_used (expr);
    }

  if (expr == error_mark_node || TREE_CODE (expr) == TREE_LIST)
    {
      if (complain & tf_error)
	qualified_name_lookup_error (scope,
				     TREE_OPERAND (qualified_id, 1),
				     expr);
      return error_mark_node;
    }

  if (is_template)
    expr = lookup_template_function (expr, template_args);

  if (expr == error_mark_node && complain & tf_error)
    qualified_name_lookup_error (scope, TREE_OPERAND (qualified_id, 1),
				 expr);
  else if (TYPE_P (scope))
    {
      expr = (adjust_result_of_qualified_name_lookup
	      (expr, scope, current_class_type));
      expr = (finish_qualified_id_expr
	      (scope, expr, done, address_p,
	       QUALIFIED_NAME_IS_TEMPLATE (qualified_id),
	       /*template_arg_p=*/false));
    }

  /* Expressions do not generally have reference type.  */
  if (TREE_CODE (expr) != SCOPE_REF
      /* However, if we're about to form a pointer-to-member, we just
	 want the referenced member referenced.  */
      && TREE_CODE (expr) != OFFSET_REF)
    expr = convert_from_reference (expr);

  return expr;
}

/* Like tsubst, but deals with expressions.  This function just replaces
   template parms; to finish processing the resultant expression, use
   tsubst_expr.  */

static tree
tsubst_copy (tree t, tree args, tsubst_flags_t complain, tree in_decl)
{
  enum tree_code code;
  tree r;

  if (t == NULL_TREE || t == error_mark_node)
    return t;

  code = TREE_CODE (t);

  switch (code)
    {
    case PARM_DECL:
      r = retrieve_local_specialization (t);
      gcc_assert (r != NULL);
      mark_used (r);
      return r;

    case CONST_DECL:
      {
	tree enum_type;
	tree v;

	if (DECL_TEMPLATE_PARM_P (t))
	  return tsubst_copy (DECL_INITIAL (t), args, complain, in_decl);
	/* There is no need to substitute into namespace-scope
	   enumerators.  */
	if (DECL_NAMESPACE_SCOPE_P (t))
	  return t;
	/* If ARGS is NULL, then T is known to be non-dependent.  */
	if (args == NULL_TREE)
	  return integral_constant_value (t);

	/* Unfortunately, we cannot just call lookup_name here.
	   Consider:

	     template <int I> int f() {
	     enum E { a = I };
	     struct S { void g() { E e = a; } };
	     };

	   When we instantiate f<7>::S::g(), say, lookup_name is not
	   clever enough to find f<7>::a.  */
	enum_type
	  = tsubst_aggr_type (TREE_TYPE (t), args, complain, in_decl,
			      /*entering_scope=*/0);

	for (v = TYPE_VALUES (enum_type);
	     v != NULL_TREE;
	     v = TREE_CHAIN (v))
	  if (TREE_PURPOSE (v) == DECL_NAME (t))
	    return TREE_VALUE (v);

	  /* We didn't find the name.  That should never happen; if
	     name-lookup found it during preliminary parsing, we
	     should find it again here during instantiation.  */
	gcc_unreachable ();
      }
      return t;

    case FIELD_DECL:
      if (DECL_CONTEXT (t))
	{
	  tree ctx;

	  ctx = tsubst_aggr_type (DECL_CONTEXT (t), args, complain, in_decl,
				  /*entering_scope=*/1);
	  if (ctx != DECL_CONTEXT (t))
	    {
	      tree r = lookup_field (ctx, DECL_NAME (t), 0, false);
	      if (!r)
		{
		  if (complain & tf_error)
		    error ("using invalid field %qD", t);
		  return error_mark_node;
		}
	      return r;
	    }
	}

      return t;

    case VAR_DECL:
    case FUNCTION_DECL:
      if ((DECL_LANG_SPECIFIC (t) && DECL_TEMPLATE_INFO (t))
	  || local_variable_p (t))
	t = tsubst (t, args, complain, in_decl);
      mark_used (t);
      return t;

    case BASELINK:
      return tsubst_baselink (t, current_class_type, args, complain, in_decl);

    case TEMPLATE_DECL:
      if (DECL_TEMPLATE_TEMPLATE_PARM_P (t))
	return tsubst (TREE_TYPE (DECL_TEMPLATE_RESULT (t)),
		       args, complain, in_decl);
      else if (DECL_FUNCTION_TEMPLATE_P (t) && DECL_MEMBER_TEMPLATE_P (t))
	return tsubst (t, args, complain, in_decl);
      else if (DECL_CLASS_SCOPE_P (t)
	       && uses_template_parms (DECL_CONTEXT (t)))
	{
	  /* Template template argument like the following example need
	     special treatment:

	       template <template <class> class TT> struct C {};
	       template <class T> struct D {
		 template <class U> struct E {};
		 C<E> c;				// #1
	       };
	       D<int> d;				// #2

	     We are processing the template argument `E' in #1 for
	     the template instantiation #2.  Originally, `E' is a
	     TEMPLATE_DECL with `D<T>' as its DECL_CONTEXT.  Now we
	     have to substitute this with one having context `D<int>'.  */

	  tree context = tsubst (DECL_CONTEXT (t), args, complain, in_decl);
	  return lookup_field (context, DECL_NAME(t), 0, false);
	}
      else
	/* Ordinary template template argument.  */
	return t;

    case CAST_EXPR:
    case REINTERPRET_CAST_EXPR:
    case CONST_CAST_EXPR:
    case STATIC_CAST_EXPR:
    case DYNAMIC_CAST_EXPR:
    case NOP_EXPR:
      return build1
	(code, tsubst (TREE_TYPE (t), args, complain, in_decl),
	 tsubst_copy (TREE_OPERAND (t, 0), args, complain, in_decl));

    case INDIRECT_REF:
    case NEGATE_EXPR:
    case TRUTH_NOT_EXPR:
    case BIT_NOT_EXPR:
    case ADDR_EXPR:
    case UNARY_PLUS_EXPR:      /* Unary + */
    case SIZEOF_EXPR:
    case ALIGNOF_EXPR:
    case ARROW_EXPR:
    case THROW_EXPR:
    case TYPEID_EXPR:
    case REALPART_EXPR:
    case IMAGPART_EXPR:
      return build1
	(code, tsubst (TREE_TYPE (t), args, complain, in_decl),
	 tsubst_copy (TREE_OPERAND (t, 0), args, complain, in_decl));

    case COMPONENT_REF:
      {
	tree object;
	tree name;

	object = tsubst_copy (TREE_OPERAND (t, 0), args, complain, in_decl);
	name = TREE_OPERAND (t, 1);
	if (TREE_CODE (name) == BIT_NOT_EXPR)
	  {
	    name = tsubst_copy (TREE_OPERAND (name, 0), args,
				complain, in_decl);
	    name = build1 (BIT_NOT_EXPR, NULL_TREE, name);
	  }
	else if (TREE_CODE (name) == SCOPE_REF
		 && TREE_CODE (TREE_OPERAND (name, 1)) == BIT_NOT_EXPR)
	  {
	    tree base = tsubst_copy (TREE_OPERAND (name, 0), args,
				     complain, in_decl);
	    name = TREE_OPERAND (name, 1);
	    name = tsubst_copy (TREE_OPERAND (name, 0), args,
				complain, in_decl);
	    name = build1 (BIT_NOT_EXPR, NULL_TREE, name);
	    name = build_qualified_name (/*type=*/NULL_TREE,
					 base, name,
					 /*template_p=*/false);
	  }
	else if (TREE_CODE (name) == BASELINK)
	  name = tsubst_baselink (name,
				  non_reference (TREE_TYPE (object)),
				  args, complain,
				  in_decl);
	else
	  name = tsubst_copy (name, args, complain, in_decl);
	return build_nt (COMPONENT_REF, object, name, NULL_TREE);
      }

    case PLUS_EXPR:
    case MINUS_EXPR:
    case MULT_EXPR:
    case TRUNC_DIV_EXPR:
    case CEIL_DIV_EXPR:
    case FLOOR_DIV_EXPR:
    case ROUND_DIV_EXPR:
    case EXACT_DIV_EXPR:
    case BIT_AND_EXPR:
    case BIT_IOR_EXPR:
    case BIT_XOR_EXPR:
    case TRUNC_MOD_EXPR:
    case FLOOR_MOD_EXPR:
    case TRUTH_ANDIF_EXPR:
    case TRUTH_ORIF_EXPR:
    case TRUTH_AND_EXPR:
    case TRUTH_OR_EXPR:
    case RSHIFT_EXPR:
    case LSHIFT_EXPR:
    case RROTATE_EXPR:
    case LROTATE_EXPR:
    case EQ_EXPR:
    case NE_EXPR:
    case MAX_EXPR:
    case MIN_EXPR:
    case LE_EXPR:
    case GE_EXPR:
    case LT_EXPR:
    case GT_EXPR:
    case COMPOUND_EXPR:
    case DOTSTAR_EXPR:
    case MEMBER_REF:
    case PREDECREMENT_EXPR:
    case PREINCREMENT_EXPR:
    case POSTDECREMENT_EXPR:
    case POSTINCREMENT_EXPR:
      return build_nt
	(code, tsubst_copy (TREE_OPERAND (t, 0), args, complain, in_decl),
	 tsubst_copy (TREE_OPERAND (t, 1), args, complain, in_decl));

    case SCOPE_REF:
      return build_qualified_name (/*type=*/NULL_TREE,
				   tsubst_copy (TREE_OPERAND (t, 0),
						args, complain, in_decl),
				   tsubst_copy (TREE_OPERAND (t, 1),
						args, complain, in_decl),
				   QUALIFIED_NAME_IS_TEMPLATE (t));

    case ARRAY_REF:
      return build_nt
	(ARRAY_REF,
	 tsubst_copy (TREE_OPERAND (t, 0), args, complain, in_decl),
	 tsubst_copy (TREE_OPERAND (t, 1), args, complain, in_decl),
	 NULL_TREE, NULL_TREE);

    case CALL_EXPR:
      return build_nt (code,
		       tsubst_copy (TREE_OPERAND (t, 0), args,
				    complain, in_decl),
		       tsubst_copy (TREE_OPERAND (t, 1), args, complain,
				    in_decl),
		       NULL_TREE);

    case COND_EXPR:
    case MODOP_EXPR:
    case PSEUDO_DTOR_EXPR:
      {
	r = build_nt
	  (code, tsubst_copy (TREE_OPERAND (t, 0), args, complain, in_decl),
	   tsubst_copy (TREE_OPERAND (t, 1), args, complain, in_decl),
	   tsubst_copy (TREE_OPERAND (t, 2), args, complain, in_decl));
	TREE_NO_WARNING (r) = TREE_NO_WARNING (t);
	return r;
      }

    case NEW_EXPR:
      {
	r = build_nt
	(code, tsubst_copy (TREE_OPERAND (t, 0), args, complain, in_decl),
	 tsubst_copy (TREE_OPERAND (t, 1), args, complain, in_decl),
	 tsubst_copy (TREE_OPERAND (t, 2), args, complain, in_decl));
	NEW_EXPR_USE_GLOBAL (r) = NEW_EXPR_USE_GLOBAL (t);
	return r;
      }

    case DELETE_EXPR:
      {
	r = build_nt
	(code, tsubst_copy (TREE_OPERAND (t, 0), args, complain, in_decl),
	 tsubst_copy (TREE_OPERAND (t, 1), args, complain, in_decl));
	DELETE_EXPR_USE_GLOBAL (r) = DELETE_EXPR_USE_GLOBAL (t);
	DELETE_EXPR_USE_VEC (r) = DELETE_EXPR_USE_VEC (t);
	return r;
      }

    case TEMPLATE_ID_EXPR:
      {
	/* Substituted template arguments */
	tree fn = TREE_OPERAND (t, 0);
	tree targs = TREE_OPERAND (t, 1);

	fn = tsubst_copy (fn, args, complain, in_decl);
	if (targs)
	  targs = tsubst_template_args (targs, args, complain, in_decl);

	return lookup_template_function (fn, targs);
      }

    case TREE_LIST:
      {
	tree purpose, value, chain;

	if (t == void_list_node)
	  return t;

	purpose = TREE_PURPOSE (t);
	if (purpose)
	  purpose = tsubst_copy (purpose, args, complain, in_decl);
	value = TREE_VALUE (t);
	if (value)
	  value = tsubst_copy (value, args, complain, in_decl);
	chain = TREE_CHAIN (t);
	if (chain && chain != void_type_node)
	  chain = tsubst_copy (chain, args, complain, in_decl);
	if (purpose == TREE_PURPOSE (t)
	    && value == TREE_VALUE (t)
	    && chain == TREE_CHAIN (t))
	  return t;
	return tree_cons (purpose, value, chain);
      }

    case RECORD_TYPE:
    case UNION_TYPE:
    case ENUMERAL_TYPE:
    case INTEGER_TYPE:
    case TEMPLATE_TYPE_PARM:
    case TEMPLATE_TEMPLATE_PARM:
    case BOUND_TEMPLATE_TEMPLATE_PARM:
    case TEMPLATE_PARM_INDEX:
    case POINTER_TYPE:
    case REFERENCE_TYPE:
    case OFFSET_TYPE:
    case FUNCTION_TYPE:
    case METHOD_TYPE:
    case ARRAY_TYPE:
    case TYPENAME_TYPE:
    case UNBOUND_CLASS_TEMPLATE:
    case TYPEOF_TYPE:
    case TYPE_DECL:
      return tsubst (t, args, complain, in_decl);

    case IDENTIFIER_NODE:
      if (IDENTIFIER_TYPENAME_P (t))
	{
	  tree new_type = tsubst (TREE_TYPE (t), args, complain, in_decl);
	  return mangle_conv_op_name_for_type (new_type);
	}
      else
	return t;

    case CONSTRUCTOR:
      /* This is handled by tsubst_copy_and_build.  */
      gcc_unreachable ();

    case VA_ARG_EXPR:
      return build_x_va_arg (tsubst_copy (TREE_OPERAND (t, 0), args, complain,
					  in_decl),
			     tsubst (TREE_TYPE (t), args, complain, in_decl));

    case CLEANUP_POINT_EXPR:
      /* We shouldn't have built any of these during initial template
	 generation.  Instead, they should be built during instantiation
	 in response to the saved STMT_IS_FULL_EXPR_P setting.  */
      gcc_unreachable ();

    case OFFSET_REF:
      mark_used (TREE_OPERAND (t, 1));
      return t;

    default:
      return t;
    }
}

/* Like tsubst_copy, but specifically for OpenMP clauses.  */

static tree
tsubst_omp_clauses (tree clauses, tree args, tsubst_flags_t complain,
		    tree in_decl)
{
  tree new_clauses = NULL, nc, oc;

  for (oc = clauses; oc ; oc = OMP_CLAUSE_CHAIN (oc))
    {
      nc = copy_node (oc);
      OMP_CLAUSE_CHAIN (nc) = new_clauses;
      new_clauses = nc;

      switch (OMP_CLAUSE_CODE (nc))
	{
	case OMP_CLAUSE_PRIVATE:
	case OMP_CLAUSE_SHARED:
	case OMP_CLAUSE_FIRSTPRIVATE:
	case OMP_CLAUSE_LASTPRIVATE:
	case OMP_CLAUSE_REDUCTION:
	case OMP_CLAUSE_COPYIN:
	case OMP_CLAUSE_COPYPRIVATE:
	case OMP_CLAUSE_IF:
	case OMP_CLAUSE_NUM_THREADS:
	case OMP_CLAUSE_SCHEDULE:
	  OMP_CLAUSE_OPERAND (nc, 0)
	    = tsubst_expr (OMP_CLAUSE_OPERAND (oc, 0), args, complain, 
			   in_decl, /*integral_constant_expression_p=*/false);
	  break;
	case OMP_CLAUSE_NOWAIT:
	case OMP_CLAUSE_ORDERED:
	case OMP_CLAUSE_DEFAULT:
	  break;
	default:
	  gcc_unreachable ();
	}
    }

  return finish_omp_clauses (nreverse (new_clauses));
}

/* Like tsubst_copy_and_build, but unshare TREE_LIST nodes.  */

static tree
tsubst_copy_asm_operands (tree t, tree args, tsubst_flags_t complain,
			  tree in_decl)
{
#define RECUR(t) tsubst_copy_asm_operands (t, args, complain, in_decl)

  tree purpose, value, chain;

  if (t == NULL)
    return t;

  if (TREE_CODE (t) != TREE_LIST)
    return tsubst_copy_and_build (t, args, complain, in_decl,
				  /*function_p=*/false,
				  /*integral_constant_expression_p=*/false);

  if (t == void_list_node)
    return t;

  purpose = TREE_PURPOSE (t);
  if (purpose)
    purpose = RECUR (purpose);
  value = TREE_VALUE (t);
  if (value)
    value = RECUR (value);
  chain = TREE_CHAIN (t);
  if (chain && chain != void_type_node)
    chain = RECUR (chain);
  return tree_cons (purpose, value, chain);
#undef RECUR
}

/* Like tsubst_copy for expressions, etc. but also does semantic
   processing.  */

static tree
tsubst_expr (tree t, tree args, tsubst_flags_t complain, tree in_decl,
	     bool integral_constant_expression_p)
{
#define RECUR(NODE)				\
  tsubst_expr ((NODE), args, complain, in_decl,	\
	       integral_constant_expression_p)

  tree stmt, tmp;

  if (t == NULL_TREE || t == error_mark_node)
    return t;

  if (EXPR_HAS_LOCATION (t))
    input_location = EXPR_LOCATION (t);
  if (STATEMENT_CODE_P (TREE_CODE (t)))
    current_stmt_tree ()->stmts_are_full_exprs_p = STMT_IS_FULL_EXPR_P (t);

  switch (TREE_CODE (t))
    {
    case STATEMENT_LIST:
      {
	tree_stmt_iterator i;
	for (i = tsi_start (t); !tsi_end_p (i); tsi_next (&i))
	  RECUR (tsi_stmt (i));
	break;
      }

    case CTOR_INITIALIZER:
      finish_mem_initializers (tsubst_initializer_list
			       (TREE_OPERAND (t, 0), args));
      break;

    case RETURN_EXPR:
      finish_return_stmt (RECUR (TREE_OPERAND (t, 0)));
      break;

    case EXPR_STMT:
      tmp = RECUR (EXPR_STMT_EXPR (t));
      if (EXPR_STMT_STMT_EXPR_RESULT (t))
	finish_stmt_expr_expr (tmp, cur_stmt_expr);
      else
	finish_expr_stmt (tmp);
      break;

    case USING_STMT:
      do_using_directive (RECUR (USING_STMT_NAMESPACE (t)));
      break;

    case DECL_EXPR:
      {
	tree decl;
	tree init;

	decl = DECL_EXPR_DECL (t);
	if (TREE_CODE (decl) == LABEL_DECL)
	  finish_label_decl (DECL_NAME (decl));
	else if (TREE_CODE (decl) == USING_DECL)
	  {
	    tree scope = USING_DECL_SCOPE (decl);
	    tree name = DECL_NAME (decl);
	    tree decl;

	    scope = RECUR (scope);
	    decl = lookup_qualified_name (scope, name,
					  /*is_type_p=*/false,
					  /*complain=*/false);
	    if (decl == error_mark_node || TREE_CODE (decl) == TREE_LIST)
	      qualified_name_lookup_error (scope, name, decl);
	    else
	      do_local_using_decl (decl, scope, name);
	  }
	else
	  {
	    init = DECL_INITIAL (decl);
	    decl = tsubst (decl, args, complain, in_decl);
	    if (decl != error_mark_node)
	      {
		/* By marking the declaration as instantiated, we avoid
		   trying to instantiate it.  Since instantiate_decl can't
		   handle local variables, and since we've already done
		   all that needs to be done, that's the right thing to
		   do.  */
		if (TREE_CODE (decl) == VAR_DECL)
		  DECL_TEMPLATE_INSTANTIATED (decl) = 1;
		if (TREE_CODE (decl) == VAR_DECL
		    && ANON_AGGR_TYPE_P (TREE_TYPE (decl)))
		  /* Anonymous aggregates are a special case.  */
		  finish_anon_union (decl);
		else
		  {
		    maybe_push_decl (decl);
		    if (TREE_CODE (decl) == VAR_DECL
			&& DECL_PRETTY_FUNCTION_P (decl))
		      {
			/* For __PRETTY_FUNCTION__ we have to adjust the
			   initializer.  */
			const char *const name
			  = cxx_printable_name (current_function_decl, 2);
			init = cp_fname_init (name, &TREE_TYPE (decl));
		      }
		    else
		      init = RECUR (init);
		    finish_decl (decl, init, NULL_TREE);
		  }
	      }
	  }

	/* A DECL_EXPR can also be used as an expression, in the condition
	   clause of an if/for/while construct.  */
	return decl;
      }

    case FOR_STMT:
/* APPLE LOCAL begin for-fsf-4_4 3274130 5295549 */ \
      tmp = RECUR (FOR_ATTRIBUTES (t));
      stmt = begin_for_stmt (tmp);
      RECUR (FOR_INIT_STMT (t));
/* APPLE LOCAL end for-fsf-4_4 3274130 5295549 */ \
      finish_for_init_stmt (stmt);
      tmp = RECUR (FOR_COND (t));
      finish_for_cond (tmp, stmt);
      tmp = RECUR (FOR_EXPR (t));
      finish_for_expr (tmp, stmt);
      RECUR (FOR_BODY (t));
      finish_for_stmt (stmt);
      break;

    case WHILE_STMT:
/* APPLE LOCAL begin for-fsf-4_4 3274130 5295549 */ \
      tmp = RECUR (WHILE_ATTRIBUTES (t));
      stmt = begin_while_stmt (tmp);
/* APPLE LOCAL end for-fsf-4_4 3274130 5295549 */ \
      tmp = RECUR (WHILE_COND (t));
      finish_while_stmt_cond (tmp, stmt);
      RECUR (WHILE_BODY (t));
      finish_while_stmt (stmt);
      break;

    case DO_STMT:
/* APPLE LOCAL begin for-fsf-4_4 3274130 5295549 */ \
      tmp = RECUR (DO_ATTRIBUTES (t));
      stmt = begin_do_stmt (tmp);
/* APPLE LOCAL end for-fsf-4_4 3274130 5295549 */ \
      RECUR (DO_BODY (t));
      finish_do_body (stmt);
      tmp = RECUR (DO_COND (t));
      finish_do_stmt (tmp, stmt);
      break;

    case IF_STMT:
      stmt = begin_if_stmt ();
      tmp = RECUR (IF_COND (t));
      finish_if_stmt_cond (tmp, stmt);
      RECUR (THEN_CLAUSE (t));
      finish_then_clause (stmt);

      if (ELSE_CLAUSE (t))
	{
	  begin_else_clause (stmt);
	  RECUR (ELSE_CLAUSE (t));
	  finish_else_clause (stmt);
	}

      finish_if_stmt (stmt);
      break;

    case BIND_EXPR:
      if (BIND_EXPR_BODY_BLOCK (t))
	stmt = begin_function_body ();
      else
	stmt = begin_compound_stmt (BIND_EXPR_TRY_BLOCK (t)
				    ? BCS_TRY_BLOCK : 0);

      RECUR (BIND_EXPR_BODY (t));

      if (BIND_EXPR_BODY_BLOCK (t))
	finish_function_body (stmt);
      else
	finish_compound_stmt (stmt);
      break;

    case BREAK_STMT:
      finish_break_stmt ();
      break;

    case CONTINUE_STMT:
      finish_continue_stmt ();
      break;

    case SWITCH_STMT:
      stmt = begin_switch_stmt ();
      tmp = RECUR (SWITCH_STMT_COND (t));
      finish_switch_cond (tmp, stmt);
      RECUR (SWITCH_STMT_BODY (t));
      finish_switch_stmt (stmt);
      break;

    case CASE_LABEL_EXPR:
      finish_case_label (RECUR (CASE_LOW (t)),
			 RECUR (CASE_HIGH (t)));
      break;

    case LABEL_EXPR:
      finish_label_stmt (DECL_NAME (LABEL_EXPR_LABEL (t)));
      break;

    case GOTO_EXPR:
      tmp = GOTO_DESTINATION (t);
      if (TREE_CODE (tmp) != LABEL_DECL)
	/* Computed goto's must be tsubst'd into.  On the other hand,
	   non-computed gotos must not be; the identifier in question
	   will have no binding.  */
	tmp = RECUR (tmp);
      else
	tmp = DECL_NAME (tmp);
      finish_goto_stmt (tmp);
      break;

    case ASM_EXPR:
      tmp = finish_asm_stmt
	(ASM_VOLATILE_P (t),
	 RECUR (ASM_STRING (t)),
	 tsubst_copy_asm_operands (ASM_OUTPUTS (t), args, complain, in_decl),
	 tsubst_copy_asm_operands (ASM_INPUTS (t), args, complain, in_decl),
	 tsubst_copy_asm_operands (ASM_CLOBBERS (t), args, complain, in_decl));
      {
	tree asm_expr = tmp;
	if (TREE_CODE (asm_expr) == CLEANUP_POINT_EXPR)
	  asm_expr = TREE_OPERAND (asm_expr, 0);
	ASM_INPUT_P (asm_expr) = ASM_INPUT_P (t);
      }
      break;

    case TRY_BLOCK:
      if (CLEANUP_P (t))
	{
	  stmt = begin_try_block ();
	  RECUR (TRY_STMTS (t));
	  finish_cleanup_try_block (stmt);
	  finish_cleanup (RECUR (TRY_HANDLERS (t)), stmt);
	}
      else
	{
	  tree compound_stmt = NULL_TREE;

	  if (FN_TRY_BLOCK_P (t))
	    stmt = begin_function_try_block (&compound_stmt);
	  else
	    stmt = begin_try_block ();

	  RECUR (TRY_STMTS (t));

	  if (FN_TRY_BLOCK_P (t))
	    finish_function_try_block (stmt);
	  else
	    finish_try_block (stmt);

	  RECUR (TRY_HANDLERS (t));
	  if (FN_TRY_BLOCK_P (t))
	    finish_function_handler_sequence (stmt, compound_stmt);
	  else
	    finish_handler_sequence (stmt);
	}
      break;

    case HANDLER:
      {
	tree decl = HANDLER_PARMS (t);

	if (decl)
	  {
	    decl = tsubst (decl, args, complain, in_decl);
	    /* Prevent instantiate_decl from trying to instantiate
	       this variable.  We've already done all that needs to be
	       done.  */
	    if (decl != error_mark_node)
	      DECL_TEMPLATE_INSTANTIATED (decl) = 1;
	  }
	stmt = begin_handler ();
	finish_handler_parms (decl, stmt);
	RECUR (HANDLER_BODY (t));
	finish_handler (stmt);
      }
      break;

    case TAG_DEFN:
      tsubst (TREE_TYPE (t), args, complain, NULL_TREE);
      break;

    case OMP_PARALLEL:
      tmp = tsubst_omp_clauses (OMP_PARALLEL_CLAUSES (t),
				args, complain, in_decl);
      stmt = begin_omp_parallel ();
      RECUR (OMP_PARALLEL_BODY (t));
      OMP_PARALLEL_COMBINED (finish_omp_parallel (tmp, stmt))
	= OMP_PARALLEL_COMBINED (t);
      break;

    case OMP_FOR:
      {
	tree clauses, decl, init, cond, incr, body, pre_body;

	clauses = tsubst_omp_clauses (OMP_FOR_CLAUSES (t),
				      args, complain, in_decl);
	init = OMP_FOR_INIT (t);
	gcc_assert (TREE_CODE (init) == MODIFY_EXPR);
	decl = RECUR (TREE_OPERAND (init, 0));
	init = RECUR (TREE_OPERAND (init, 1));
	cond = RECUR (OMP_FOR_COND (t));
	incr = RECUR (OMP_FOR_INCR (t));

	stmt = begin_omp_structured_block ();

	pre_body = push_stmt_list ();
	RECUR (OMP_FOR_PRE_BODY (t));
	pre_body = pop_stmt_list (pre_body);

	body = push_stmt_list ();
	RECUR (OMP_FOR_BODY (t));
	body = pop_stmt_list (body);

	t = finish_omp_for (EXPR_LOCATION (t), decl, init, cond, incr, body,
			    pre_body);
	if (t)
	  OMP_FOR_CLAUSES (t) = clauses;

	add_stmt (finish_omp_structured_block (stmt));
      }
      break;

    case OMP_SECTIONS:
    case OMP_SINGLE:
      tmp = tsubst_omp_clauses (OMP_CLAUSES (t), args, complain, in_decl);
      stmt = push_stmt_list ();
      RECUR (OMP_BODY (t));
      stmt = pop_stmt_list (stmt);

      t = copy_node (t);
      OMP_BODY (t) = stmt;
      OMP_CLAUSES (t) = tmp;
      add_stmt (t);
      break;

    case OMP_SECTION:
    case OMP_CRITICAL:
    case OMP_MASTER:
    case OMP_ORDERED:
      stmt = push_stmt_list ();
      RECUR (OMP_BODY (t));
      stmt = pop_stmt_list (stmt);

      t = copy_node (t);
      OMP_BODY (t) = stmt;
      add_stmt (t);
      break;

    case OMP_ATOMIC:
      {
	tree op0, op1;
	op0 = RECUR (TREE_OPERAND (t, 0));
	op1 = RECUR (TREE_OPERAND (t, 1));
	finish_omp_atomic (OMP_ATOMIC_CODE (t), op0, op1);
      }
      break;

    default:
      gcc_assert (!STATEMENT_CODE_P (TREE_CODE (t)));

      return tsubst_copy_and_build (t, args, complain, in_decl,
				    /*function_p=*/false,
				    integral_constant_expression_p);
    }

  return NULL_TREE;
#undef RECUR
}

/* T is a postfix-expression that is not being used in a function
   call.  Return the substituted version of T.  */

static tree
tsubst_non_call_postfix_expression (tree t, tree args,
				    tsubst_flags_t complain,
				    tree in_decl)
{
  if (TREE_CODE (t) == SCOPE_REF)
    t = tsubst_qualified_id (t, args, complain, in_decl,
			     /*done=*/false, /*address_p=*/false);
  else
    t = tsubst_copy_and_build (t, args, complain, in_decl,
			       /*function_p=*/false,
			       /*integral_constant_expression_p=*/false);

  return t;
}

/* Like tsubst but deals with expressions and performs semantic
   analysis.  FUNCTION_P is true if T is the "F" in "F (ARGS)".  */

tree
tsubst_copy_and_build (tree t,
		       tree args,
		       tsubst_flags_t complain,
		       tree in_decl,
		       bool function_p,
		       bool integral_constant_expression_p)
{
#define RECUR(NODE)						\
  tsubst_copy_and_build (NODE, args, complain, in_decl, 	\
			 /*function_p=*/false,			\
			 integral_constant_expression_p)

  tree op1;

  if (t == NULL_TREE || t == error_mark_node)
    return t;

  switch (TREE_CODE (t))
    {
    case USING_DECL:
      t = DECL_NAME (t);
      /* Fall through.  */
    case IDENTIFIER_NODE:
      {
	tree decl;
	cp_id_kind idk;
	bool non_integral_constant_expression_p;
	const char *error_msg;

	if (IDENTIFIER_TYPENAME_P (t))
	  {
	    tree new_type = tsubst (TREE_TYPE (t), args, complain, in_decl);
	    t = mangle_conv_op_name_for_type (new_type);
	  }

	/* Look up the name.  */
	decl = lookup_name (t);

	/* By convention, expressions use ERROR_MARK_NODE to indicate
	   failure, not NULL_TREE.  */
	if (decl == NULL_TREE)
	  decl = error_mark_node;

	decl = finish_id_expression (t, decl, NULL_TREE,
				     &idk,
				     integral_constant_expression_p,
				     /*allow_non_integral_constant_expression_p=*/false,
				     &non_integral_constant_expression_p,
				     /*template_p=*/false,
				     /*done=*/true,
				     /*address_p=*/false,
				     /*template_arg_p=*/false,
				     &error_msg);
	if (error_msg)
	  error ("%s", error_msg);
	if (!function_p && TREE_CODE (decl) == IDENTIFIER_NODE)
	  decl = unqualified_name_lookup_error (decl);
	return decl;
      }

    case TEMPLATE_ID_EXPR:
      {
	tree object;
	tree template = RECUR (TREE_OPERAND (t, 0));
	tree targs = TREE_OPERAND (t, 1);

	if (targs)
	  targs = tsubst_template_args (targs, args, complain, in_decl);

	if (TREE_CODE (template) == COMPONENT_REF)
	  {
	    object = TREE_OPERAND (template, 0);
	    template = TREE_OPERAND (template, 1);
	  }
	else
	  object = NULL_TREE;
	template = lookup_template_function (template, targs);

	if (object)
	  return build3 (COMPONENT_REF, TREE_TYPE (template),
			 object, template, NULL_TREE);
	else
	  return baselink_for_fns (template);
      }

    case INDIRECT_REF:
      {
	tree r = RECUR (TREE_OPERAND (t, 0));

	if (REFERENCE_REF_P (t))
	  {
	    /* A type conversion to reference type will be enclosed in
	       such an indirect ref, but the substitution of the cast
	       will have also added such an indirect ref.  */
	    if (TREE_CODE (TREE_TYPE (r)) == REFERENCE_TYPE)
	      r = convert_from_reference (r);
	  }
	else
	  r = build_x_indirect_ref (r, "unary *");
	return r;
      }

    case NOP_EXPR:
      return build_nop
	(tsubst (TREE_TYPE (t), args, complain, in_decl),
	 RECUR (TREE_OPERAND (t, 0)));

    case CAST_EXPR:
    case REINTERPRET_CAST_EXPR:
    case CONST_CAST_EXPR:
    case DYNAMIC_CAST_EXPR:
    case STATIC_CAST_EXPR:
      {
	tree type;
	tree op;

	type = tsubst (TREE_TYPE (t), args, complain, in_decl);
	if (integral_constant_expression_p
	    && !cast_valid_in_integral_constant_expression_p (type))
	  {
	    error ("a cast to a type other than an integral or "
		   "enumeration type cannot appear in a constant-expression");
	    return error_mark_node; 
	  }

	op = RECUR (TREE_OPERAND (t, 0));

	switch (TREE_CODE (t))
	  {
	  case CAST_EXPR:
	    return build_functional_cast (type, op);
	  case REINTERPRET_CAST_EXPR:
	    return build_reinterpret_cast (type, op);
	  case CONST_CAST_EXPR:
	    return build_const_cast (type, op);
	  case DYNAMIC_CAST_EXPR:
	    return build_dynamic_cast (type, op);
	  case STATIC_CAST_EXPR:
	    return build_static_cast (type, op);
	  default:
	    gcc_unreachable ();
	  }
      }

    case POSTDECREMENT_EXPR:
    case POSTINCREMENT_EXPR:
      op1 = tsubst_non_call_postfix_expression (TREE_OPERAND (t, 0),
						args, complain, in_decl);
      return build_x_unary_op (TREE_CODE (t), op1);

    case PREDECREMENT_EXPR:
    case PREINCREMENT_EXPR:
    case NEGATE_EXPR:
    case BIT_NOT_EXPR:
    case ABS_EXPR:
    case TRUTH_NOT_EXPR:
    case UNARY_PLUS_EXPR:  /* Unary + */
    case REALPART_EXPR:
    case IMAGPART_EXPR:
      return build_x_unary_op (TREE_CODE (t), RECUR (TREE_OPERAND (t, 0)));

    case ADDR_EXPR:
      op1 = TREE_OPERAND (t, 0);
      if (TREE_CODE (op1) == SCOPE_REF)
	op1 = tsubst_qualified_id (op1, args, complain, in_decl,
				   /*done=*/true, /*address_p=*/true);
      else
	op1 = tsubst_non_call_postfix_expression (op1, args, complain,
						  in_decl);
      if (TREE_CODE (op1) == LABEL_DECL)
	return finish_label_address_expr (DECL_NAME (op1));
      return build_x_unary_op (ADDR_EXPR, op1);

    case PLUS_EXPR:
    case MINUS_EXPR:
    case MULT_EXPR:
    case TRUNC_DIV_EXPR:
    case CEIL_DIV_EXPR:
    case FLOOR_DIV_EXPR:
    case ROUND_DIV_EXPR:
    case EXACT_DIV_EXPR:
    case BIT_AND_EXPR:
    case BIT_IOR_EXPR:
    case BIT_XOR_EXPR:
    case TRUNC_MOD_EXPR:
    case FLOOR_MOD_EXPR:
    case TRUTH_ANDIF_EXPR:
    case TRUTH_ORIF_EXPR:
    case TRUTH_AND_EXPR:
    case TRUTH_OR_EXPR:
    case RSHIFT_EXPR:
    case LSHIFT_EXPR:
    case RROTATE_EXPR:
    case LROTATE_EXPR:
    case EQ_EXPR:
    case NE_EXPR:
    case MAX_EXPR:
    case MIN_EXPR:
    case LE_EXPR:
    case GE_EXPR:
    case LT_EXPR:
    case GT_EXPR:
    case MEMBER_REF:
    case DOTSTAR_EXPR:
      return build_x_binary_op
	(TREE_CODE (t),
	 RECUR (TREE_OPERAND (t, 0)),
	 (TREE_NO_WARNING (TREE_OPERAND (t, 0))
	  ? ERROR_MARK
	  : TREE_CODE (TREE_OPERAND (t, 0))),
	 RECUR (TREE_OPERAND (t, 1)),
	 (TREE_NO_WARNING (TREE_OPERAND (t, 1))
	  ? ERROR_MARK
	  : TREE_CODE (TREE_OPERAND (t, 1))),
	 /*overloaded_p=*/NULL);

    case SCOPE_REF:
      return tsubst_qualified_id (t, args, complain, in_decl, /*done=*/true,
				  /*address_p=*/false);
    case ARRAY_REF:
      op1 = tsubst_non_call_postfix_expression (TREE_OPERAND (t, 0),
						args, complain, in_decl);
      return build_x_binary_op (ARRAY_REF, op1,
				(TREE_NO_WARNING (TREE_OPERAND (t, 0))
				 ? ERROR_MARK
				 : TREE_CODE (TREE_OPERAND (t, 0))),
				RECUR (TREE_OPERAND (t, 1)),
				(TREE_NO_WARNING (TREE_OPERAND (t, 1))
				 ? ERROR_MARK
				 : TREE_CODE (TREE_OPERAND (t, 1))),
				/*overloaded_p=*/NULL);

    case SIZEOF_EXPR:
    case ALIGNOF_EXPR:
      op1 = TREE_OPERAND (t, 0);
      if (!args)
	{
	  /* When there are no ARGS, we are trying to evaluate a
	     non-dependent expression from the parser.  Trying to do
	     the substitutions may not work.  */
	  if (!TYPE_P (op1))
	    op1 = TREE_TYPE (op1);
	}
      else
	{
	  ++skip_evaluation;
	  op1 = tsubst_copy_and_build (op1, args, complain, in_decl,
				       /*function_p=*/false,
				       /*integral_constant_expression_p=*/false);
	  --skip_evaluation;
	}
      if (TYPE_P (op1))
	return cxx_sizeof_or_alignof_type (op1, TREE_CODE (t), true);
      else
	return cxx_sizeof_or_alignof_expr (op1, TREE_CODE (t));

    case MODOP_EXPR:
      {
	tree r = build_x_modify_expr
	  (RECUR (TREE_OPERAND (t, 0)),
	   TREE_CODE (TREE_OPERAND (t, 1)),
	   RECUR (TREE_OPERAND (t, 2)));
	/* TREE_NO_WARNING must be set if either the expression was
	   parenthesized or it uses an operator such as >>= rather
	   than plain assignment.  In the former case, it was already
	   set and must be copied.  In the latter case,
	   build_x_modify_expr sets it and it must not be reset
	   here.  */
	if (TREE_NO_WARNING (t))
	  TREE_NO_WARNING (r) = TREE_NO_WARNING (t);
	return r;
      }

    case ARROW_EXPR:
      op1 = tsubst_non_call_postfix_expression (TREE_OPERAND (t, 0),
						args, complain, in_decl);
      /* Remember that there was a reference to this entity.  */
      if (DECL_P (op1))
	mark_used (op1);
      return build_x_arrow (op1);

    case NEW_EXPR:
      return build_new
	(RECUR (TREE_OPERAND (t, 0)),
	 RECUR (TREE_OPERAND (t, 1)),
	 RECUR (TREE_OPERAND (t, 2)),
	 RECUR (TREE_OPERAND (t, 3)),
	 NEW_EXPR_USE_GLOBAL (t));

    case DELETE_EXPR:
     return delete_sanity
       (RECUR (TREE_OPERAND (t, 0)),
	RECUR (TREE_OPERAND (t, 1)),
	DELETE_EXPR_USE_VEC (t),
	DELETE_EXPR_USE_GLOBAL (t));

    case COMPOUND_EXPR:
      return build_x_compound_expr (RECUR (TREE_OPERAND (t, 0)),
				    RECUR (TREE_OPERAND (t, 1)));

    case CALL_EXPR:
      {
	tree function;
	tree call_args;
	bool qualified_p;
	bool koenig_p;

	function = TREE_OPERAND (t, 0);
	/* When we parsed the expression,  we determined whether or
	   not Koenig lookup should be performed.  */
	koenig_p = KOENIG_LOOKUP_P (t);
	if (TREE_CODE (function) == SCOPE_REF)
	  {
	    qualified_p = true;
	    function = tsubst_qualified_id (function, args, complain, in_decl,
					    /*done=*/false,
					    /*address_p=*/false);
	  }
	else
	  {
	    if (TREE_CODE (function) == COMPONENT_REF)
	      {
		tree op = TREE_OPERAND (function, 1);

		qualified_p = (TREE_CODE (op) == SCOPE_REF
			       || (BASELINK_P (op)
				   && BASELINK_QUALIFIED_P (op)));
	      }
	    else
	      qualified_p = false;

	    function = tsubst_copy_and_build (function, args, complain,
					      in_decl,
					      !qualified_p,
					      integral_constant_expression_p);

	    if (BASELINK_P (function))
	      qualified_p = true;
	  }

	call_args = RECUR (TREE_OPERAND (t, 1));

	/* We do not perform argument-dependent lookup if normal
	   lookup finds a non-function, in accordance with the
	   expected resolution of DR 218.  */
	if (koenig_p
	    && ((is_overloaded_fn (function)
		 /* If lookup found a member function, the Koenig lookup is
		    not appropriate, even if an unqualified-name was used
		    to denote the function.  */
		 && !DECL_FUNCTION_MEMBER_P (get_first_fn (function)))
		|| TREE_CODE (function) == IDENTIFIER_NODE))
	  function = perform_koenig_lookup (function, call_args);

	if (TREE_CODE (function) == IDENTIFIER_NODE)
	  {
	    unqualified_name_lookup_error (function);
	    return error_mark_node;
	  }

	/* Remember that there was a reference to this entity.  */
	if (DECL_P (function))
	  mark_used (function);

	if (TREE_CODE (function) == OFFSET_REF)
	  return build_offset_ref_call_from_tree (function, call_args);
	if (TREE_CODE (function) == COMPONENT_REF)
	  {
	    if (!BASELINK_P (TREE_OPERAND (function, 1)))
	      return finish_call_expr (function, call_args,
				       /*disallow_virtual=*/false,
				       /*koenig_p=*/false);
	    else
	      return (build_new_method_call
		      (TREE_OPERAND (function, 0),
		       TREE_OPERAND (function, 1),
		       call_args, NULL_TREE,
		       qualified_p ? LOOKUP_NONVIRTUAL : LOOKUP_NORMAL,
		       /*fn_p=*/NULL));
	  }
	return finish_call_expr (function, call_args,
				 /*disallow_virtual=*/qualified_p,
				 koenig_p);
      }

    case COND_EXPR:
      return build_x_conditional_expr
	(RECUR (TREE_OPERAND (t, 0)),
	 RECUR (TREE_OPERAND (t, 1)),
	 RECUR (TREE_OPERAND (t, 2)));

    case PSEUDO_DTOR_EXPR:
      return finish_pseudo_destructor_expr
	(RECUR (TREE_OPERAND (t, 0)),
	 RECUR (TREE_OPERAND (t, 1)),
	 RECUR (TREE_OPERAND (t, 2)));

    case TREE_LIST:
      {
	tree purpose, value, chain;

	if (t == void_list_node)
	  return t;

	purpose = TREE_PURPOSE (t);
	if (purpose)
	  purpose = RECUR (purpose);
	value = TREE_VALUE (t);
	if (value)
	  value = RECUR (value);
	chain = TREE_CHAIN (t);
	if (chain && chain != void_type_node)
	  chain = RECUR (chain);
	if (purpose == TREE_PURPOSE (t)
	    && value == TREE_VALUE (t)
	    && chain == TREE_CHAIN (t))
	  return t;
	return tree_cons (purpose, value, chain);
      }

    case COMPONENT_REF:
      {
	tree object;
	tree object_type;
	tree member;

	object = tsubst_non_call_postfix_expression (TREE_OPERAND (t, 0),
						     args, complain, in_decl);
	/* Remember that there was a reference to this entity.  */
	if (DECL_P (object))
	  mark_used (object);
	object_type = TREE_TYPE (object);

	member = TREE_OPERAND (t, 1);
	if (BASELINK_P (member))
	  member = tsubst_baselink (member,
				    non_reference (TREE_TYPE (object)),
				    args, complain, in_decl);
	else
	  member = tsubst_copy (member, args, complain, in_decl);
	if (member == error_mark_node)
	  return error_mark_node;

	if (object_type && !CLASS_TYPE_P (object_type))
	  {
	    if (TREE_CODE (member) == BIT_NOT_EXPR)
	      return finish_pseudo_destructor_expr (object,
						    NULL_TREE,
						    object_type);
	    else if (TREE_CODE (member) == SCOPE_REF
		     && (TREE_CODE (TREE_OPERAND (member, 1)) == BIT_NOT_EXPR))
	      return finish_pseudo_destructor_expr (object,
						    object,
						    object_type);
	  }
	else if (TREE_CODE (member) == SCOPE_REF
		 && TREE_CODE (TREE_OPERAND (member, 1)) == TEMPLATE_ID_EXPR)
	  {
	    tree tmpl;
	    tree args;

	    /* Lookup the template functions now that we know what the
	       scope is.  */
	    tmpl = TREE_OPERAND (TREE_OPERAND (member, 1), 0);
	    args = TREE_OPERAND (TREE_OPERAND (member, 1), 1);
	    member = lookup_qualified_name (TREE_OPERAND (member, 0), tmpl,
					    /*is_type_p=*/false,
					    /*complain=*/false);
	    if (BASELINK_P (member))
	      {
		BASELINK_FUNCTIONS (member)
		  = build_nt (TEMPLATE_ID_EXPR, BASELINK_FUNCTIONS (member),
			      args);
		member = (adjust_result_of_qualified_name_lookup
			  (member, BINFO_TYPE (BASELINK_BINFO (member)),
			   object_type));
	      }
	    else
	      {
		qualified_name_lookup_error (object_type, tmpl, member);
		return error_mark_node;
	      }
	  }
	else if (TREE_CODE (member) == SCOPE_REF
		 && !CLASS_TYPE_P (TREE_OPERAND (member, 0))
		 && TREE_CODE (TREE_OPERAND (member, 0)) != NAMESPACE_DECL)
	  {
	    if (complain & tf_error)
	      {
		if (TYPE_P (TREE_OPERAND (member, 0)))
		  error ("%qT is not a class or namespace",
			 TREE_OPERAND (member, 0));
		else
		  error ("%qD is not a class or namespace",
			 TREE_OPERAND (member, 0));
	      }
	    return error_mark_node;
	  }
	else if (TREE_CODE (member) == FIELD_DECL)
	  return finish_non_static_data_member (member, object, NULL_TREE);

	return finish_class_member_access_expr (object, member,
						/*template_p=*/false);
      }

    case THROW_EXPR:
      return build_throw
	(RECUR (TREE_OPERAND (t, 0)));

    case CONSTRUCTOR:
      {
	VEC(constructor_elt,gc) *n;
	constructor_elt *ce;
	unsigned HOST_WIDE_INT idx;
	tree type = tsubst (TREE_TYPE (t), args, complain, in_decl);
	bool process_index_p;

	if (type == error_mark_node)
	  return error_mark_node;

	/* digest_init will do the wrong thing if we let it.  */
	if (type && TYPE_PTRMEMFUNC_P (type))
	  return t;

	/* We do not want to process the index of aggregate
	   initializers as they are identifier nodes which will be
	   looked up by digest_init.  */
	process_index_p = !(type && IS_AGGR_TYPE (type));

	n = VEC_copy (constructor_elt, gc, CONSTRUCTOR_ELTS (t));
	for (idx = 0; VEC_iterate (constructor_elt, n, idx, ce); idx++)
	  {
	    if (ce->index && process_index_p)
	      ce->index = RECUR (ce->index);
	    ce->value = RECUR (ce->value);
	  }

	if (TREE_HAS_CONSTRUCTOR (t))
	  return finish_compound_literal (type, n);

	return build_constructor (NULL_TREE, n);
      }

    case TYPEID_EXPR:
      {
	tree operand_0 = RECUR (TREE_OPERAND (t, 0));
	if (TYPE_P (operand_0))
	  return get_typeid (operand_0);
	return build_typeid (operand_0);
      }

    case VAR_DECL:
      if (!args)
	return t;
      /* Fall through */

    case PARM_DECL:
      {
	tree r = tsubst_copy (t, args, complain, in_decl);

	if (TREE_CODE (TREE_TYPE (t)) != REFERENCE_TYPE)
	  /* If the original type was a reference, we'll be wrapped in
	     the appropriate INDIRECT_REF.  */
	  r = convert_from_reference (r);
	return r;
      }

    case VA_ARG_EXPR:
      return build_x_va_arg (RECUR (TREE_OPERAND (t, 0)),
			     tsubst_copy (TREE_TYPE (t), args, complain,
					  in_decl));

    case OFFSETOF_EXPR:
      return finish_offsetof (RECUR (TREE_OPERAND (t, 0)));

    case STMT_EXPR:
      {
	tree old_stmt_expr = cur_stmt_expr;
	tree stmt_expr = begin_stmt_expr ();

	cur_stmt_expr = stmt_expr;
	tsubst_expr (STMT_EXPR_STMT (t), args, complain, in_decl,
		     integral_constant_expression_p);
	stmt_expr = finish_stmt_expr (stmt_expr, false);
	cur_stmt_expr = old_stmt_expr;

	return stmt_expr;
      }

    case CONST_DECL:
      t = tsubst_copy (t, args, complain, in_decl);
      /* As in finish_id_expression, we resolve enumeration constants
	 to their underlying values.  */
      if (TREE_CODE (t) == CONST_DECL)
	{
	  used_types_insert (TREE_TYPE (t));
	  return DECL_INITIAL (t);
	}
      return t;

    default:
      /* Handle Objective-C++ constructs, if appropriate.  */
      {
	tree subst
	  = objcp_tsubst_copy_and_build (t, args, complain,
					 in_decl, /*function_p=*/false);
	if (subst)
	  return subst;
      }
      return tsubst_copy (t, args, complain, in_decl);
    }

#undef RECUR
}

/* Verify that the instantiated ARGS are valid. For type arguments,
   make sure that the type's linkage is ok. For non-type arguments,
   make sure they are constants if they are integral or enumerations.
   Emit an error under control of COMPLAIN, and return TRUE on error.  */

static bool
check_instantiated_args (tree tmpl, tree args, tsubst_flags_t complain)
{
  int ix, len = DECL_NTPARMS (tmpl);
  bool result = false;

  for (ix = 0; ix != len; ix++)
    {
      tree t = TREE_VEC_ELT (args, ix);

      if (TYPE_P (t))
	{
	  /* [basic.link]: A name with no linkage (notably, the name
	     of a class or enumeration declared in a local scope)
	     shall not be used to declare an entity with linkage.
	     This implies that names with no linkage cannot be used as
	     template arguments.  */
	  tree nt = no_linkage_check (t, /*relaxed_p=*/false);

	  if (nt)
	    {
	      /* DR 488 makes use of a type with no linkage cause
		 type deduction to fail.  */
	      if (complain & tf_error)
		{
		  if (TYPE_ANONYMOUS_P (nt))
		    error ("%qT is/uses anonymous type", t);
		  else
		    error ("template argument for %qD uses local type %qT",
			   tmpl, t);
		}
	      result = true;
	    }
	  /* In order to avoid all sorts of complications, we do not
	     allow variably-modified types as template arguments.  */
	  else if (variably_modified_type_p (t, NULL_TREE))
	    {
	      if (complain & tf_error)
		error ("%qT is a variably modified type", t);
	      result = true;
	    }
	}
      /* A non-type argument of integral or enumerated type must be a
	 constant.  */
      else if (TREE_TYPE (t)
	       && INTEGRAL_OR_ENUMERATION_TYPE_P (TREE_TYPE (t))
	       && !TREE_CONSTANT (t))
	{
	  if (complain & tf_error)
	    error ("integral expression %qE is not constant", t);
	  result = true;
	}
    }
  if (result && (complain & tf_error))
    error ("  trying to instantiate %qD", tmpl);
  return result;
}

/* Instantiate the indicated variable or function template TMPL with
   the template arguments in TARG_PTR.  */

tree
instantiate_template (tree tmpl, tree targ_ptr, tsubst_flags_t complain)
{
  tree fndecl;
  tree gen_tmpl;
  tree spec;
  HOST_WIDE_INT saved_processing_template_decl;

  if (tmpl == error_mark_node)
    return error_mark_node;

  gcc_assert (TREE_CODE (tmpl) == TEMPLATE_DECL);

  /* If this function is a clone, handle it specially.  */
  if (DECL_CLONED_FUNCTION_P (tmpl))
    {
      tree spec;
      tree clone;

      spec = instantiate_template (DECL_CLONED_FUNCTION (tmpl), targ_ptr,
				   complain);
      if (spec == error_mark_node)
	return error_mark_node;

      /* Look for the clone.  */
      FOR_EACH_CLONE (clone, spec)
	if (DECL_NAME (clone) == DECL_NAME (tmpl))
	  return clone;
      /* We should always have found the clone by now.  */
      gcc_unreachable ();
      return NULL_TREE;
    }

  /* Check to see if we already have this specialization.  */
  spec = retrieve_specialization (tmpl, targ_ptr,
				  /*class_specializations_p=*/false);
  if (spec != NULL_TREE)
    return spec;

  gen_tmpl = most_general_template (tmpl);
  if (tmpl != gen_tmpl)
    {
      /* The TMPL is a partial instantiation.  To get a full set of
	 arguments we must add the arguments used to perform the
	 partial instantiation.  */
      targ_ptr = add_outermost_template_args (DECL_TI_ARGS (tmpl),
					      targ_ptr);

      /* Check to see if we already have this specialization.  */
      spec = retrieve_specialization (gen_tmpl, targ_ptr,
				      /*class_specializations_p=*/false);
      if (spec != NULL_TREE)
	return spec;
    }

  if (check_instantiated_args (gen_tmpl, INNERMOST_TEMPLATE_ARGS (targ_ptr),
			       complain))
    return error_mark_node;

  /* We are building a FUNCTION_DECL, during which the access of its
     parameters and return types have to be checked.  However this
     FUNCTION_DECL which is the desired context for access checking
     is not built yet.  We solve this chicken-and-egg problem by
     deferring all checks until we have the FUNCTION_DECL.  */
  push_deferring_access_checks (dk_deferred);

  /* Although PROCESSING_TEMPLATE_DECL may be true at this point
     (because, for example, we have encountered a non-dependent
     function call in the body of a template function and must now
     determine which of several overloaded functions will be called),
     within the instantiation itself we are not processing a
     template.  */  
  saved_processing_template_decl = processing_template_decl;
  processing_template_decl = 0;
  /* Substitute template parameters to obtain the specialization.  */
  fndecl = tsubst (DECL_TEMPLATE_RESULT (gen_tmpl),
		   targ_ptr, complain, gen_tmpl);
  processing_template_decl = saved_processing_template_decl;
  if (fndecl == error_mark_node)
    return error_mark_node;

  /* Now we know the specialization, compute access previously
     deferred.  */
  push_access_scope (fndecl);
  perform_deferred_access_checks ();
  pop_access_scope (fndecl);
  pop_deferring_access_checks ();

  /* The DECL_TI_TEMPLATE should always be the immediate parent
     template, not the most general template.  */
  DECL_TI_TEMPLATE (fndecl) = tmpl;

  /* If we've just instantiated the main entry point for a function,
     instantiate all the alternate entry points as well.  We do this
     by cloning the instantiation of the main entry point, not by
     instantiating the template clones.  */
  if (TREE_CHAIN (gen_tmpl) && DECL_CLONED_FUNCTION_P (TREE_CHAIN (gen_tmpl)))
    clone_function_decl (fndecl, /*update_method_vec_p=*/0);

  return fndecl;
}

/* The FN is a TEMPLATE_DECL for a function.  The ARGS are the
   arguments that are being used when calling it.  TARGS is a vector
   into which the deduced template arguments are placed.

   Return zero for success, 2 for an incomplete match that doesn't resolve
   all the types, and 1 for complete failure.  An error message will be
   printed only for an incomplete match.

   If FN is a conversion operator, or we are trying to produce a specific
   specialization, RETURN_TYPE is the return type desired.

   The EXPLICIT_TARGS are explicit template arguments provided via a
   template-id.

   The parameter STRICT is one of:

   DEDUCE_CALL:
     We are deducing arguments for a function call, as in
     [temp.deduct.call].

   DEDUCE_CONV:
     We are deducing arguments for a conversion function, as in
     [temp.deduct.conv].

   DEDUCE_EXACT:
     We are deducing arguments when doing an explicit instantiation
     as in [temp.explicit], when determining an explicit specialization
     as in [temp.expl.spec], or when taking the address of a function
     template, as in [temp.deduct.funcaddr].  */

int
fn_type_unification (tree fn,
		     tree explicit_targs,
		     tree targs,
		     tree args,
		     tree return_type,
		     unification_kind_t strict,
		     int flags)
{
  tree parms;
  tree fntype;
  int result;

  gcc_assert (TREE_CODE (fn) == TEMPLATE_DECL);

  fntype = TREE_TYPE (fn);
  if (explicit_targs)
    {
      /* [temp.deduct]

	 The specified template arguments must match the template
	 parameters in kind (i.e., type, nontype, template), and there
	 must not be more arguments than there are parameters;
	 otherwise type deduction fails.

	 Nontype arguments must match the types of the corresponding
	 nontype template parameters, or must be convertible to the
	 types of the corresponding nontype parameters as specified in
	 _temp.arg.nontype_, otherwise type deduction fails.

	 All references in the function type of the function template
	 to the corresponding template parameters are replaced by the
	 specified template argument values.  If a substitution in a
	 template parameter or in the function type of the function
	 template results in an invalid type, type deduction fails.  */
      int i;
      tree converted_args;
      bool incomplete;

      if (explicit_targs == error_mark_node)
	return 1;

      converted_args
	= (coerce_template_parms (DECL_INNERMOST_TEMPLATE_PARMS (fn),
				  explicit_targs, NULL_TREE, tf_none,
				  /*require_all_args=*/false,
				  /*use_default_args=*/false));
      if (converted_args == error_mark_node)
	return 1;

      /* Substitute the explicit args into the function type.  This is
	 necessary so that, for instance, explicitly declared function
	 arguments can match null pointed constants.  If we were given
	 an incomplete set of explicit args, we must not do semantic
	 processing during substitution as we could create partial
	 instantiations.  */
      incomplete = NUM_TMPL_ARGS (explicit_targs) != NUM_TMPL_ARGS (targs);
      processing_template_decl += incomplete;
      fntype = tsubst (fntype, converted_args, tf_none, NULL_TREE);
      processing_template_decl -= incomplete;

      if (fntype == error_mark_node)
	return 1;

      /* Place the explicitly specified arguments in TARGS.  */
      for (i = NUM_TMPL_ARGS (converted_args); i--;)
	TREE_VEC_ELT (targs, i) = TREE_VEC_ELT (converted_args, i);
    }

  /* Never do unification on the 'this' parameter.  */
  parms = skip_artificial_parms_for (fn, TYPE_ARG_TYPES (fntype));

  if (return_type)
    {
      parms = tree_cons (NULL_TREE, TREE_TYPE (fntype), parms);
      args = tree_cons (NULL_TREE, return_type, args);
    }

  /* We allow incomplete unification without an error message here
     because the standard doesn't seem to explicitly prohibit it.  Our
     callers must be ready to deal with unification failures in any
     event.  */
  result = type_unification_real (DECL_INNERMOST_TEMPLATE_PARMS (fn),
				  targs, parms, args, /*subr=*/0,
				  strict, flags);

  if (result == 0)
    /* All is well so far.  Now, check:

       [temp.deduct]

       When all template arguments have been deduced, all uses of
       template parameters in nondeduced contexts are replaced with
       the corresponding deduced argument values.  If the
       substitution results in an invalid type, as described above,
       type deduction fails.  */
    if (tsubst (TREE_TYPE (fn), targs, tf_none, NULL_TREE)
	== error_mark_node)
      return 1;

  return result;
}

/* Adjust types before performing type deduction, as described in
   [temp.deduct.call] and [temp.deduct.conv].  The rules in these two
   sections are symmetric.  PARM is the type of a function parameter
   or the return type of the conversion function.  ARG is the type of
   the argument passed to the call, or the type of the value
   initialized with the result of the conversion function.  */

static int
maybe_adjust_types_for_deduction (unification_kind_t strict,
				  tree* parm,
				  tree* arg)
{
  int result = 0;

  switch (strict)
    {
    case DEDUCE_CALL:
      break;

    case DEDUCE_CONV:
      {
	/* Swap PARM and ARG throughout the remainder of this
	   function; the handling is precisely symmetric since PARM
	   will initialize ARG rather than vice versa.  */
	tree* temp = parm;
	parm = arg;
	arg = temp;
	break;
      }

    case DEDUCE_EXACT:
      /* There is nothing to do in this case.  */
      return 0;

    default:
      gcc_unreachable ();
    }

  if (TREE_CODE (*parm) != REFERENCE_TYPE)
    {
      /* [temp.deduct.call]

	 If P is not a reference type:

	 --If A is an array type, the pointer type produced by the
	 array-to-pointer standard conversion (_conv.array_) is
	 used in place of A for type deduction; otherwise,

	 --If A is a function type, the pointer type produced by
	 the function-to-pointer standard conversion
	 (_conv.func_) is used in place of A for type deduction;
	 otherwise,

	 --If A is a cv-qualified type, the top level
	 cv-qualifiers of A's type are ignored for type
	 deduction.  */
      if (TREE_CODE (*arg) == ARRAY_TYPE)
	*arg = build_pointer_type (TREE_TYPE (*arg));
      else if (TREE_CODE (*arg) == FUNCTION_TYPE)
	*arg = build_pointer_type (*arg);
      else
	*arg = TYPE_MAIN_VARIANT (*arg);
    }

  /* [temp.deduct.call]

     If P is a cv-qualified type, the top level cv-qualifiers
     of P's type are ignored for type deduction.  If P is a
     reference type, the type referred to by P is used for
     type deduction.  */
  *parm = TYPE_MAIN_VARIANT (*parm);
  if (TREE_CODE (*parm) == REFERENCE_TYPE)
    {
      *parm = TREE_TYPE (*parm);
      result |= UNIFY_ALLOW_OUTER_MORE_CV_QUAL;
    }

  /* DR 322. For conversion deduction, remove a reference type on parm
     too (which has been swapped into ARG).  */
  if (strict == DEDUCE_CONV && TREE_CODE (*arg) == REFERENCE_TYPE)
    *arg = TREE_TYPE (*arg);

  return result;
}

/* Most parms like fn_type_unification.

   If SUBR is 1, we're being called recursively (to unify the
   arguments of a function or method parameter of a function
   template). */

static int
type_unification_real (tree tparms,
		       tree targs,
		       tree xparms,
		       tree xargs,
		       int subr,
		       unification_kind_t strict,
		       int flags)
{
  tree parm, arg;
  int i;
  int ntparms = TREE_VEC_LENGTH (tparms);
  int sub_strict;
  int saw_undeduced = 0;
  tree parms, args;

  gcc_assert (TREE_CODE (tparms) == TREE_VEC);
  gcc_assert (xparms == NULL_TREE || TREE_CODE (xparms) == TREE_LIST);
  gcc_assert (!xargs || TREE_CODE (xargs) == TREE_LIST);
  gcc_assert (ntparms > 0);

  switch (strict)
    {
    case DEDUCE_CALL:
      sub_strict = (UNIFY_ALLOW_OUTER_LEVEL | UNIFY_ALLOW_MORE_CV_QUAL
		    | UNIFY_ALLOW_DERIVED);
      break;

    case DEDUCE_CONV:
      sub_strict = UNIFY_ALLOW_LESS_CV_QUAL;
      break;

    case DEDUCE_EXACT:
      sub_strict = UNIFY_ALLOW_NONE;
      break;

    default:
      gcc_unreachable ();
    }

 again:
  parms = xparms;
  args = xargs;

  while (parms && parms != void_list_node
	 && args && args != void_list_node)
    {
      parm = TREE_VALUE (parms);
      parms = TREE_CHAIN (parms);
      arg = TREE_VALUE (args);
      args = TREE_CHAIN (args);

      if (arg == error_mark_node)
	return 1;
      if (arg == unknown_type_node)
	/* We can't deduce anything from this, but we might get all the
	   template args from other function args.  */
	continue;

      /* Conversions will be performed on a function argument that
	 corresponds with a function parameter that contains only
	 non-deducible template parameters and explicitly specified
	 template parameters.  */
      if (!uses_template_parms (parm))
	{
	  tree type;

	  if (!TYPE_P (arg))
	    type = TREE_TYPE (arg);
	  else
	    type = arg;

	  if (same_type_p (parm, type))
	    continue;
	  if (strict != DEDUCE_EXACT
	      && can_convert_arg (parm, type, TYPE_P (arg) ? NULL_TREE : arg,
				  flags))
	    continue;

	  return 1;
	}

      if (!TYPE_P (arg))
	{
	  gcc_assert (TREE_TYPE (arg) != NULL_TREE);
	  if (type_unknown_p (arg))
	    {
	      /* [temp.deduct.type] 

	         A template-argument can be deduced from a pointer to
		 function or pointer to member function argument if
		 the set of overloaded functions does not contain
		 function templates and at most one of a set of
		 overloaded functions provides a unique match.  */
	      if (resolve_overloaded_unification
		  (tparms, targs, parm, arg, strict, sub_strict))
		continue;

	      return 1;
	    }
	  arg = unlowered_expr_type (arg);
	  if (arg == error_mark_node)
	    return 1;
	}

      {
	int arg_strict = sub_strict;

	if (!subr)
	  arg_strict |= maybe_adjust_types_for_deduction (strict, &parm, &arg);

	if (unify (tparms, targs, parm, arg, arg_strict))
	  return 1;
      }
    }

  /* Fail if we've reached the end of the parm list, and more args
     are present, and the parm list isn't variadic.  */
  if (args && args != void_list_node && parms == void_list_node)
    return 1;
  /* Fail if parms are left and they don't have default values.  */
  if (parms && parms != void_list_node
      && TREE_PURPOSE (parms) == NULL_TREE)
    return 1;

  if (!subr)
    for (i = 0; i < ntparms; i++)
      if (!TREE_VEC_ELT (targs, i))
	{
	  tree tparm;

          if (TREE_VEC_ELT (tparms, i) == error_mark_node)
            continue;

          tparm = TREE_VALUE (TREE_VEC_ELT (tparms, i));

	  /* If this is an undeduced nontype parameter that depends on
	     a type parameter, try another pass; its type may have been
	     deduced from a later argument than the one from which
	     this parameter can be deduced.  */
	  if (TREE_CODE (tparm) == PARM_DECL
	      && uses_template_parms (TREE_TYPE (tparm))
	      && !saw_undeduced++)
	    goto again;

	  return 2;
	}

  return 0;
}

/* Subroutine of type_unification_real.  Args are like the variables
   at the call site.  ARG is an overloaded function (or template-id);
   we try deducing template args from each of the overloads, and if
   only one succeeds, we go with that.  Modifies TARGS and returns
   true on success.  */

static bool
resolve_overloaded_unification (tree tparms,
				tree targs,
				tree parm,
				tree arg,
				unification_kind_t strict,
				int sub_strict)
{
  tree tempargs = copy_node (targs);
  int good = 0;
  bool addr_p;

  if (TREE_CODE (arg) == ADDR_EXPR)
    {
      arg = TREE_OPERAND (arg, 0);
      addr_p = true;
    }
  else
    addr_p = false;

  if (TREE_CODE (arg) == COMPONENT_REF)
    /* Handle `&x' where `x' is some static or non-static member
       function name.  */
    arg = TREE_OPERAND (arg, 1);

  if (TREE_CODE (arg) == OFFSET_REF)
    arg = TREE_OPERAND (arg, 1);

  /* Strip baselink information.  */
  if (BASELINK_P (arg))
    arg = BASELINK_FUNCTIONS (arg);

  if (TREE_CODE (arg) == TEMPLATE_ID_EXPR)
    {
      /* If we got some explicit template args, we need to plug them into
	 the affected templates before we try to unify, in case the
	 explicit args will completely resolve the templates in question.  */

      tree expl_subargs = TREE_OPERAND (arg, 1);
      arg = TREE_OPERAND (arg, 0);

      for (; arg; arg = OVL_NEXT (arg))
	{
	  tree fn = OVL_CURRENT (arg);
	  tree subargs, elem;

	  if (TREE_CODE (fn) != TEMPLATE_DECL)
	    continue;

	  subargs = get_bindings (fn, DECL_TEMPLATE_RESULT (fn),
				  expl_subargs, /*check_ret=*/false);
	  if (subargs)
	    {
	      elem = tsubst (TREE_TYPE (fn), subargs, tf_none, NULL_TREE);
	      good += try_one_overload (tparms, targs, tempargs, parm,
					elem, strict, sub_strict, addr_p);
	    }
	}
    }
  else if (TREE_CODE (arg) != OVERLOAD
	   && TREE_CODE (arg) != FUNCTION_DECL)
    /* If ARG is, for example, "(0, &f)" then its type will be unknown
       -- but the deduction does not succeed because the expression is
       not just the function on its own.  */
    return false;
  else
    for (; arg; arg = OVL_NEXT (arg))
      good += try_one_overload (tparms, targs, tempargs, parm,
				TREE_TYPE (OVL_CURRENT (arg)),
				strict, sub_strict, addr_p);

  /* [temp.deduct.type] A template-argument can be deduced from a pointer
     to function or pointer to member function argument if the set of
     overloaded functions does not contain function templates and at most
     one of a set of overloaded functions provides a unique match.

     So if we found multiple possibilities, we return success but don't
     deduce anything.  */

  if (good == 1)
    {
      int i = TREE_VEC_LENGTH (targs);
      for (; i--; )
	if (TREE_VEC_ELT (tempargs, i))
	  TREE_VEC_ELT (targs, i) = TREE_VEC_ELT (tempargs, i);
    }
  if (good)
    return true;

  return false;
}

/* Subroutine of resolve_overloaded_unification; does deduction for a single
   overload.  Fills TARGS with any deduced arguments, or error_mark_node if
   different overloads deduce different arguments for a given parm.
   ADDR_P is true if the expression for which deduction is being
   performed was of the form "& fn" rather than simply "fn".

   Returns 1 on success.  */

static int
try_one_overload (tree tparms,
		  tree orig_targs,
		  tree targs,
		  tree parm,
		  tree arg,
		  unification_kind_t strict,
		  int sub_strict,
		  bool addr_p)
{
  int nargs;
  tree tempargs;
  int i;

  /* [temp.deduct.type] A template-argument can be deduced from a pointer
     to function or pointer to member function argument if the set of
     overloaded functions does not contain function templates and at most
     one of a set of overloaded functions provides a unique match.

     So if this is a template, just return success.  */

  if (uses_template_parms (arg))
    return 1;

  if (TREE_CODE (arg) == METHOD_TYPE)
    arg = build_ptrmemfunc_type (build_pointer_type (arg));
  else if (addr_p)
    arg = build_pointer_type (arg);

  sub_strict |= maybe_adjust_types_for_deduction (strict, &parm, &arg);

  /* We don't copy orig_targs for this because if we have already deduced
     some template args from previous args, unify would complain when we
     try to deduce a template parameter for the same argument, even though
     there isn't really a conflict.  */
  nargs = TREE_VEC_LENGTH (targs);
  tempargs = make_tree_vec (nargs);

  if (unify (tparms, tempargs, parm, arg, sub_strict) != 0)
    return 0;

  /* First make sure we didn't deduce anything that conflicts with
     explicitly specified args.  */
  for (i = nargs; i--; )
    {
      tree elt = TREE_VEC_ELT (tempargs, i);
      tree oldelt = TREE_VEC_ELT (orig_targs, i);

      if (!elt)
	/*NOP*/;
      else if (uses_template_parms (elt))
	/* Since we're unifying against ourselves, we will fill in
	   template args used in the function parm list with our own
	   template parms.  Discard them.  */
	TREE_VEC_ELT (tempargs, i) = NULL_TREE;
      else if (oldelt && !template_args_equal (oldelt, elt))
	return 0;
    }

  for (i = nargs; i--; )
    {
      tree elt = TREE_VEC_ELT (tempargs, i);

      if (elt)
	TREE_VEC_ELT (targs, i) = elt;
    }

  return 1;
}

/* PARM is a template class (perhaps with unbound template
   parameters).  ARG is a fully instantiated type.  If ARG can be
   bound to PARM, return ARG, otherwise return NULL_TREE.  TPARMS and
   TARGS are as for unify.  */

static tree
try_class_unification (tree tparms, tree targs, tree parm, tree arg)
{
  tree copy_of_targs;

  if (!CLASSTYPE_TEMPLATE_INFO (arg)
      || (most_general_template (CLASSTYPE_TI_TEMPLATE (arg))
	  != most_general_template (CLASSTYPE_TI_TEMPLATE (parm))))
    return NULL_TREE;

  /* We need to make a new template argument vector for the call to
     unify.  If we used TARGS, we'd clutter it up with the result of
     the attempted unification, even if this class didn't work out.
     We also don't want to commit ourselves to all the unifications
     we've already done, since unification is supposed to be done on
     an argument-by-argument basis.  In other words, consider the
     following pathological case:

       template <int I, int J, int K>
       struct S {};

       template <int I, int J>
       struct S<I, J, 2> : public S<I, I, I>, S<J, J, J> {};

       template <int I, int J, int K>
       void f(S<I, J, K>, S<I, I, I>);

       void g() {
	 S<0, 0, 0> s0;
	 S<0, 1, 2> s2;

	 f(s0, s2);
       }

     Now, by the time we consider the unification involving `s2', we
     already know that we must have `f<0, 0, 0>'.  But, even though
     `S<0, 1, 2>' is derived from `S<0, 0, 0>', the code is invalid
     because there are two ways to unify base classes of S<0, 1, 2>
     with S<I, I, I>.  If we kept the already deduced knowledge, we
     would reject the possibility I=1.  */
  copy_of_targs = make_tree_vec (TREE_VEC_LENGTH (targs));

  /* If unification failed, we're done.  */
  if (unify (tparms, copy_of_targs, CLASSTYPE_TI_ARGS (parm),
	     CLASSTYPE_TI_ARGS (arg), UNIFY_ALLOW_NONE))
    return NULL_TREE;

  return arg;
}

/* Given a template type PARM and a class type ARG, find the unique
   base type in ARG that is an instance of PARM.  We do not examine
   ARG itself; only its base-classes.  If there is not exactly one
   appropriate base class, return NULL_TREE.  PARM may be the type of
   a partial specialization, as well as a plain template type.  Used
   by unify.  */

static tree
get_template_base (tree tparms, tree targs, tree parm, tree arg)
{
  tree rval = NULL_TREE;
  tree binfo;

  gcc_assert (IS_AGGR_TYPE_CODE (TREE_CODE (arg)));

  binfo = TYPE_BINFO (complete_type (arg));
  if (!binfo)
    /* The type could not be completed.  */
    return NULL_TREE;

  /* Walk in inheritance graph order.  The search order is not
     important, and this avoids multiple walks of virtual bases.  */
  for (binfo = TREE_CHAIN (binfo); binfo; binfo = TREE_CHAIN (binfo))
    {
      tree r = try_class_unification (tparms, targs, parm, BINFO_TYPE (binfo));

      if (r)
	{
	  /* If there is more than one satisfactory baseclass, then:

	       [temp.deduct.call]

	      If they yield more than one possible deduced A, the type
	      deduction fails.

	     applies.  */
	  if (rval && !same_type_p (r, rval))
	    return NULL_TREE;

	  rval = r;
	}
    }

  return rval;
}

/* Returns the level of DECL, which declares a template parameter.  */

static int
template_decl_level (tree decl)
{
  switch (TREE_CODE (decl))
    {
    case TYPE_DECL:
    case TEMPLATE_DECL:
      return TEMPLATE_TYPE_LEVEL (TREE_TYPE (decl));

    case PARM_DECL:
      return TEMPLATE_PARM_LEVEL (DECL_INITIAL (decl));

    default:
      gcc_unreachable ();
    }
  return 0;
}

/* Decide whether ARG can be unified with PARM, considering only the
   cv-qualifiers of each type, given STRICT as documented for unify.
   Returns nonzero iff the unification is OK on that basis.  */

static int
check_cv_quals_for_unify (int strict, tree arg, tree parm)
{
  int arg_quals = cp_type_quals (arg);
  int parm_quals = cp_type_quals (parm);

  if (TREE_CODE (parm) == TEMPLATE_TYPE_PARM
      && !(strict & UNIFY_ALLOW_OUTER_MORE_CV_QUAL))
    {
      /*  Although a CVR qualifier is ignored when being applied to a
	  substituted template parameter ([8.3.2]/1 for example), that
	  does not apply during deduction [14.8.2.4]/1, (even though
	  that is not explicitly mentioned, [14.8.2.4]/9 indicates
	  this).  Except when we're allowing additional CV qualifiers
	  at the outer level [14.8.2.1]/3,1st bullet.  */
      if ((TREE_CODE (arg) == REFERENCE_TYPE
	   || TREE_CODE (arg) == FUNCTION_TYPE
	   || TREE_CODE (arg) == METHOD_TYPE)
	  && (parm_quals & (TYPE_QUAL_CONST | TYPE_QUAL_VOLATILE)))
	return 0;

      if ((!POINTER_TYPE_P (arg) && TREE_CODE (arg) != TEMPLATE_TYPE_PARM)
	  && (parm_quals & TYPE_QUAL_RESTRICT))
	return 0;
    }

  if (!(strict & (UNIFY_ALLOW_MORE_CV_QUAL | UNIFY_ALLOW_OUTER_MORE_CV_QUAL))
      && (arg_quals & parm_quals) != parm_quals)
    return 0;

  if (!(strict & (UNIFY_ALLOW_LESS_CV_QUAL | UNIFY_ALLOW_OUTER_LESS_CV_QUAL))
      && (parm_quals & arg_quals) != arg_quals)
    return 0;

  return 1;
}

/* Deduce the value of template parameters.  TPARMS is the (innermost)
   set of template parameters to a template.  TARGS is the bindings
   for those template parameters, as determined thus far; TARGS may
   include template arguments for outer levels of template parameters
   as well.  PARM is a parameter to a template function, or a
   subcomponent of that parameter; ARG is the corresponding argument.
   This function attempts to match PARM with ARG in a manner
   consistent with the existing assignments in TARGS.  If more values
   are deduced, then TARGS is updated.

   Returns 0 if the type deduction succeeds, 1 otherwise.  The
   parameter STRICT is a bitwise or of the following flags:

     UNIFY_ALLOW_NONE:
       Require an exact match between PARM and ARG.
     UNIFY_ALLOW_MORE_CV_QUAL:
       Allow the deduced ARG to be more cv-qualified (by qualification
       conversion) than ARG.
     UNIFY_ALLOW_LESS_CV_QUAL:
       Allow the deduced ARG to be less cv-qualified than ARG.
     UNIFY_ALLOW_DERIVED:
       Allow the deduced ARG to be a template base class of ARG,
       or a pointer to a template base class of the type pointed to by
       ARG.
     UNIFY_ALLOW_INTEGER:
       Allow any integral type to be deduced.  See the TEMPLATE_PARM_INDEX
       case for more information.
     UNIFY_ALLOW_OUTER_LEVEL:
       This is the outermost level of a deduction. Used to determine validity
       of qualification conversions. A valid qualification conversion must
       have const qualified pointers leading up to the inner type which
       requires additional CV quals, except at the outer level, where const
       is not required [conv.qual]. It would be normal to set this flag in
       addition to setting UNIFY_ALLOW_MORE_CV_QUAL.
     UNIFY_ALLOW_OUTER_MORE_CV_QUAL:
       This is the outermost level of a deduction, and PARM can be more CV
       qualified at this point.
     UNIFY_ALLOW_OUTER_LESS_CV_QUAL:
       This is the outermost level of a deduction, and PARM can be less CV
       qualified at this point.  */

static int
unify (tree tparms, tree targs, tree parm, tree arg, int strict)
{
  int idx;
  tree targ;
  tree tparm;
  int strict_in = strict;

  /* I don't think this will do the right thing with respect to types.
     But the only case I've seen it in so far has been array bounds, where
     signedness is the only information lost, and I think that will be
     okay.  */
  while (TREE_CODE (parm) == NOP_EXPR)
    parm = TREE_OPERAND (parm, 0);

  if (arg == error_mark_node)
    return 1;
  if (arg == unknown_type_node)
    /* We can't deduce anything from this, but we might get all the
       template args from other function args.  */
    return 0;

  /* If PARM uses template parameters, then we can't bail out here,
     even if ARG == PARM, since we won't record unifications for the
     template parameters.  We might need them if we're trying to
     figure out which of two things is more specialized.  */
  if (arg == parm && !uses_template_parms (parm))
    return 0;

  /* Immediately reject some pairs that won't unify because of
     cv-qualification mismatches.  */
  if (TREE_CODE (arg) == TREE_CODE (parm)
      && TYPE_P (arg)
      /* It is the elements of the array which hold the cv quals of an array
	 type, and the elements might be template type parms. We'll check
	 when we recurse.  */
      && TREE_CODE (arg) != ARRAY_TYPE
      /* We check the cv-qualifiers when unifying with template type
	 parameters below.  We want to allow ARG `const T' to unify with
	 PARM `T' for example, when computing which of two templates
	 is more specialized, for example.  */
      && TREE_CODE (arg) != TEMPLATE_TYPE_PARM
      && !check_cv_quals_for_unify (strict_in, arg, parm))
    return 1;

  if (!(strict & UNIFY_ALLOW_OUTER_LEVEL)
      && TYPE_P (parm) && !CP_TYPE_CONST_P (parm))
    strict &= ~UNIFY_ALLOW_MORE_CV_QUAL;
  strict &= ~UNIFY_ALLOW_OUTER_LEVEL;
  strict &= ~UNIFY_ALLOW_DERIVED;
  strict &= ~UNIFY_ALLOW_OUTER_MORE_CV_QUAL;
  strict &= ~UNIFY_ALLOW_OUTER_LESS_CV_QUAL;

  switch (TREE_CODE (parm))
    {
    case TYPENAME_TYPE:
    case SCOPE_REF:
    case UNBOUND_CLASS_TEMPLATE:
      /* In a type which contains a nested-name-specifier, template
	 argument values cannot be deduced for template parameters used
	 within the nested-name-specifier.  */
      return 0;

    case TEMPLATE_TYPE_PARM:
    case TEMPLATE_TEMPLATE_PARM:
    case BOUND_TEMPLATE_TEMPLATE_PARM:
      tparm = TREE_VALUE (TREE_VEC_ELT (tparms, 0));
      if (tparm == error_mark_node)
	return 1;

      if (TEMPLATE_TYPE_LEVEL (parm)
	  != template_decl_level (tparm))
	/* The PARM is not one we're trying to unify.  Just check
	   to see if it matches ARG.  */
	return (TREE_CODE (arg) == TREE_CODE (parm)
		&& same_type_p (parm, arg)) ? 0 : 1;
      idx = TEMPLATE_TYPE_IDX (parm);
      targ = TREE_VEC_ELT (INNERMOST_TEMPLATE_ARGS (targs), idx);
      tparm = TREE_VALUE (TREE_VEC_ELT (tparms, idx));

      /* Check for mixed types and values.  */
      if ((TREE_CODE (parm) == TEMPLATE_TYPE_PARM
	   && TREE_CODE (tparm) != TYPE_DECL)
	  || (TREE_CODE (parm) == TEMPLATE_TEMPLATE_PARM
	      && TREE_CODE (tparm) != TEMPLATE_DECL))
	return 1;

      if (TREE_CODE (parm) == BOUND_TEMPLATE_TEMPLATE_PARM)
	{
	  /* ARG must be constructed from a template class or a template
	     template parameter.  */
	  if (TREE_CODE (arg) != BOUND_TEMPLATE_TEMPLATE_PARM
	      && !CLASSTYPE_SPECIALIZATION_OF_PRIMARY_TEMPLATE_P (arg))
	    return 1;

	  {
	    tree parmvec = TYPE_TI_ARGS (parm);
	    tree argvec = INNERMOST_TEMPLATE_ARGS (TYPE_TI_ARGS (arg));
	    tree argtmplvec
	      = DECL_INNERMOST_TEMPLATE_PARMS (TYPE_TI_TEMPLATE (arg));
	    int i;

	    /* The resolution to DR150 makes clear that default
	       arguments for an N-argument may not be used to bind T
	       to a template template parameter with fewer than N
	       parameters.  It is not safe to permit the binding of
	       default arguments as an extension, as that may change
	       the meaning of a conforming program.  Consider:

		  struct Dense { static const unsigned int dim = 1; };

		  template <template <typename> class View,
			    typename Block>
		  void operator+(float, View<Block> const&);

		  template <typename Block,
			    unsigned int Dim = Block::dim>
		  struct Lvalue_proxy { operator float() const; };

		  void
		  test_1d (void) {
		    Lvalue_proxy<Dense> p;
		    float b;
		    b + p;
		  }

	      Here, if Lvalue_proxy is permitted to bind to View, then
	      the global operator+ will be used; if they are not, the
	      Lvalue_proxy will be converted to float.  */
	    if (coerce_template_parms (argtmplvec, parmvec,
				       TYPE_TI_TEMPLATE (parm),
				       tf_none,
				       /*require_all_args=*/true,
				       /*use_default_args=*/false)
		== error_mark_node)
	      return 1;

	    /* Deduce arguments T, i from TT<T> or TT<i>.
	       We check each element of PARMVEC and ARGVEC individually
	       rather than the whole TREE_VEC since they can have
	       different number of elements.  */

	    for (i = 0; i < TREE_VEC_LENGTH (parmvec); ++i)
	      {
		if (unify (tparms, targs,
			   TREE_VEC_ELT (parmvec, i),
			   TREE_VEC_ELT (argvec, i),
			   UNIFY_ALLOW_NONE))
		  return 1;
	      }
	  }
	  arg = TYPE_TI_TEMPLATE (arg);

	  /* Fall through to deduce template name.  */
	}

      if (TREE_CODE (parm) == TEMPLATE_TEMPLATE_PARM
	  || TREE_CODE (parm) == BOUND_TEMPLATE_TEMPLATE_PARM)
	{
	  /* Deduce template name TT from TT, TT<>, TT<T> and TT<i>.  */

	  /* Simple cases: Value already set, does match or doesn't.  */
	  if (targ != NULL_TREE && template_args_equal (targ, arg))
	    return 0;
	  else if (targ)
	    return 1;
	}
      else
	{
	  /* If PARM is `const T' and ARG is only `int', we don't have
	     a match unless we are allowing additional qualification.
	     If ARG is `const int' and PARM is just `T' that's OK;
	     that binds `const int' to `T'.  */
	  if (!check_cv_quals_for_unify (strict_in | UNIFY_ALLOW_LESS_CV_QUAL,
					 arg, parm))
	    return 1;

	  /* Consider the case where ARG is `const volatile int' and
	     PARM is `const T'.  Then, T should be `volatile int'.  */
	  arg = cp_build_qualified_type_real
	    (arg, cp_type_quals (arg) & ~cp_type_quals (parm), tf_none);
	  if (arg == error_mark_node)
	    return 1;

	  /* Simple cases: Value already set, does match or doesn't.  */
	  if (targ != NULL_TREE && same_type_p (targ, arg))
	    return 0;
	  else if (targ)
	    return 1;

	  /* Make sure that ARG is not a variable-sized array.  (Note
	     that were talking about variable-sized arrays (like
	     `int[n]'), rather than arrays of unknown size (like
	     `int[]').)  We'll get very confused by such a type since
	     the bound of the array will not be computable in an
	     instantiation.  Besides, such types are not allowed in
	     ISO C++, so we can do as we please here.  */
	  if (variably_modified_type_p (arg, NULL_TREE))
	    return 1;
	}

      TREE_VEC_ELT (INNERMOST_TEMPLATE_ARGS (targs), idx) = arg;
      return 0;

    case TEMPLATE_PARM_INDEX:
      tparm = TREE_VALUE (TREE_VEC_ELT (tparms, 0));
      if (tparm == error_mark_node)
	return 1;

      if (TEMPLATE_PARM_LEVEL (parm)
	  != template_decl_level (tparm))
	/* The PARM is not one we're trying to unify.  Just check
	   to see if it matches ARG.  */
	return !(TREE_CODE (arg) == TREE_CODE (parm)
		 && cp_tree_equal (parm, arg));

      idx = TEMPLATE_PARM_IDX (parm);
      targ = TREE_VEC_ELT (INNERMOST_TEMPLATE_ARGS (targs), idx);

      if (targ)
	return !cp_tree_equal (targ, arg);

      /* [temp.deduct.type] If, in the declaration of a function template
	 with a non-type template-parameter, the non-type
	 template-parameter is used in an expression in the function
	 parameter-list and, if the corresponding template-argument is
	 deduced, the template-argument type shall match the type of the
	 template-parameter exactly, except that a template-argument
	 deduced from an array bound may be of any integral type.
	 The non-type parameter might use already deduced type parameters.  */
      tparm = tsubst (TREE_TYPE (parm), targs, 0, NULL_TREE);
      if (!TREE_TYPE (arg))
	/* Template-parameter dependent expression.  Just accept it for now.
	   It will later be processed in convert_template_argument.  */
	;
      else if (same_type_p (TREE_TYPE (arg), tparm))
	/* OK */;
      else if ((strict & UNIFY_ALLOW_INTEGER)
	       && (TREE_CODE (tparm) == INTEGER_TYPE
		   || TREE_CODE (tparm) == BOOLEAN_TYPE))
	/* Convert the ARG to the type of PARM; the deduced non-type
	   template argument must exactly match the types of the
	   corresponding parameter.  */
	arg = fold (build_nop (TREE_TYPE (parm), arg));
      else if (uses_template_parms (tparm))
	/* We haven't deduced the type of this parameter yet.  Try again
	   later.  */
	return 0;
      else
	return 1;

      TREE_VEC_ELT (INNERMOST_TEMPLATE_ARGS (targs), idx) = arg;
      return 0;

    case PTRMEM_CST:
     {
	/* A pointer-to-member constant can be unified only with
	 another constant.  */
      if (TREE_CODE (arg) != PTRMEM_CST)
	return 1;

      /* Just unify the class member. It would be useless (and possibly
	 wrong, depending on the strict flags) to unify also
	 PTRMEM_CST_CLASS, because we want to be sure that both parm and
	 arg refer to the same variable, even if through different
	 classes. For instance:

	 struct A { int x; };
	 struct B : A { };

	 Unification of &A::x and &B::x must succeed.  */
      return unify (tparms, targs, PTRMEM_CST_MEMBER (parm),
		    PTRMEM_CST_MEMBER (arg), strict);
     }

    case POINTER_TYPE:
      {
	if (TREE_CODE (arg) != POINTER_TYPE)
	  return 1;

	/* [temp.deduct.call]

	   A can be another pointer or pointer to member type that can
	   be converted to the deduced A via a qualification
	   conversion (_conv.qual_).

	   We pass down STRICT here rather than UNIFY_ALLOW_NONE.
	   This will allow for additional cv-qualification of the
	   pointed-to types if appropriate.  */

	if (TREE_CODE (TREE_TYPE (arg)) == RECORD_TYPE)
	  /* The derived-to-base conversion only persists through one
	     level of pointers.  */
	  strict |= (strict_in & UNIFY_ALLOW_DERIVED);

	return unify (tparms, targs, TREE_TYPE (parm),
		      TREE_TYPE (arg), strict);
      }

    case REFERENCE_TYPE:
      if (TREE_CODE (arg) != REFERENCE_TYPE)
	return 1;
      return unify (tparms, targs, TREE_TYPE (parm), TREE_TYPE (arg),
		    strict & UNIFY_ALLOW_MORE_CV_QUAL);

    case ARRAY_TYPE:
      if (TREE_CODE (arg) != ARRAY_TYPE)
	return 1;
      if ((TYPE_DOMAIN (parm) == NULL_TREE)
	  != (TYPE_DOMAIN (arg) == NULL_TREE))
	return 1;
      if (TYPE_DOMAIN (parm) != NULL_TREE)
	{
	  tree parm_max;
	  tree arg_max;
	  bool parm_cst;
	  bool arg_cst;

	  /* Our representation of array types uses "N - 1" as the
	     TYPE_MAX_VALUE for an array with "N" elements, if "N" is
	     not an integer constant.  We cannot unify arbitrarily
	     complex expressions, so we eliminate the MINUS_EXPRs
	     here.  */
	  parm_max = TYPE_MAX_VALUE (TYPE_DOMAIN (parm));
	  parm_cst = TREE_CODE (parm_max) == INTEGER_CST;
	  if (!parm_cst)
	    {
	      gcc_assert (TREE_CODE (parm_max) == MINUS_EXPR);
	      parm_max = TREE_OPERAND (parm_max, 0);
	    }
	  arg_max = TYPE_MAX_VALUE (TYPE_DOMAIN (arg));
	  arg_cst = TREE_CODE (arg_max) == INTEGER_CST;
	  if (!arg_cst)
	    {
	      /* The ARG_MAX may not be a simple MINUS_EXPR, if we are
		 trying to unify the type of a variable with the type
		 of a template parameter.  For example:

                   template <unsigned int N>
		   void f (char (&) [N]);
		   int g(); 
		   void h(int i) {
                     char a[g(i)];
		     f(a); 
                   }

                Here, the type of the ARG will be "int [g(i)]", and
                may be a SAVE_EXPR, etc.  */
	      if (TREE_CODE (arg_max) != MINUS_EXPR)
		return 1;
	      arg_max = TREE_OPERAND (arg_max, 0);
	    }

	  /* If only one of the bounds used a MINUS_EXPR, compensate
	     by adding one to the other bound.  */
	  if (parm_cst && !arg_cst)
	    parm_max = fold_build2 (PLUS_EXPR,
				    integer_type_node,
				    parm_max,
				    integer_one_node);
	  else if (arg_cst && !parm_cst)
	    arg_max = fold_build2 (PLUS_EXPR,
				   integer_type_node,
				   arg_max,
				   integer_one_node);

	  if (unify (tparms, targs, parm_max, arg_max, UNIFY_ALLOW_INTEGER))
	    return 1;
	}
      return unify (tparms, targs, TREE_TYPE (parm), TREE_TYPE (arg),
		    strict & UNIFY_ALLOW_MORE_CV_QUAL);

    case REAL_TYPE:
    case COMPLEX_TYPE:
    case VECTOR_TYPE:
    case INTEGER_TYPE:
    case BOOLEAN_TYPE:
    case ENUMERAL_TYPE:
    case VOID_TYPE:
      if (TREE_CODE (arg) != TREE_CODE (parm))
	return 1;

      /* We have already checked cv-qualification at the top of the
	 function.  */
      if (!same_type_ignoring_top_level_qualifiers_p (arg, parm))
	return 1;

      /* As far as unification is concerned, this wins.	 Later checks
	 will invalidate it if necessary.  */
      return 0;

      /* Types INTEGER_CST and MINUS_EXPR can come from array bounds.  */
      /* Type INTEGER_CST can come from ordinary constant template args.  */
    case INTEGER_CST:
      while (TREE_CODE (arg) == NOP_EXPR)
	arg = TREE_OPERAND (arg, 0);

      if (TREE_CODE (arg) != INTEGER_CST)
	return 1;
      return !tree_int_cst_equal (parm, arg);

    case TREE_VEC:
      {
	int i;
	if (TREE_CODE (arg) != TREE_VEC)
	  return 1;
	if (TREE_VEC_LENGTH (parm) != TREE_VEC_LENGTH (arg))
	  return 1;
	for (i = 0; i < TREE_VEC_LENGTH (parm); ++i)
	  if (unify (tparms, targs,
		     TREE_VEC_ELT (parm, i), TREE_VEC_ELT (arg, i),
		     UNIFY_ALLOW_NONE))
	    return 1;
	return 0;
      }

    case RECORD_TYPE:
    case UNION_TYPE:
      if (TREE_CODE (arg) != TREE_CODE (parm))
	return 1;

      if (TYPE_PTRMEMFUNC_P (parm))
	{
	  if (!TYPE_PTRMEMFUNC_P (arg))
	    return 1;

	  return unify (tparms, targs,
			TYPE_PTRMEMFUNC_FN_TYPE (parm),
			TYPE_PTRMEMFUNC_FN_TYPE (arg),
			strict);
	}

      if (CLASSTYPE_TEMPLATE_INFO (parm))
	{
	  tree t = NULL_TREE;

	  if (strict_in & UNIFY_ALLOW_DERIVED)
	    {
	      /* First, we try to unify the PARM and ARG directly.  */
	      t = try_class_unification (tparms, targs,
					 parm, arg);

	      if (!t)
		{
		  /* Fallback to the special case allowed in
		     [temp.deduct.call]:

		       If P is a class, and P has the form
		       template-id, then A can be a derived class of
		       the deduced A.  Likewise, if P is a pointer to
		       a class of the form template-id, A can be a
		       pointer to a derived class pointed to by the
		       deduced A.  */
		  t = get_template_base (tparms, targs, parm, arg);

		  if (!t)
		    return 1;
		}
	    }
	  else if (CLASSTYPE_TEMPLATE_INFO (arg)
		   && (CLASSTYPE_TI_TEMPLATE (parm)
		       == CLASSTYPE_TI_TEMPLATE (arg)))
	    /* Perhaps PARM is something like S<U> and ARG is S<int>.
	       Then, we should unify `int' and `U'.  */
	    t = arg;
	  else
	    /* There's no chance of unification succeeding.  */
	    return 1;

	  return unify (tparms, targs, CLASSTYPE_TI_ARGS (parm),
			CLASSTYPE_TI_ARGS (t), UNIFY_ALLOW_NONE);
	}
      else if (!same_type_ignoring_top_level_qualifiers_p (parm, arg))
	return 1;
      return 0;

    case METHOD_TYPE:
    case FUNCTION_TYPE:
      if (TREE_CODE (arg) != TREE_CODE (parm))
	return 1;

      /* CV qualifications for methods can never be deduced, they must
	 match exactly.  We need to check them explicitly here,
	 because type_unification_real treats them as any other
	 cvqualified parameter.  */
      if (TREE_CODE (parm) == METHOD_TYPE
	  && (!check_cv_quals_for_unify
	      (UNIFY_ALLOW_NONE,
	       TREE_TYPE (TREE_VALUE (TYPE_ARG_TYPES (arg))),
	       TREE_TYPE (TREE_VALUE (TYPE_ARG_TYPES (parm))))))
	return 1;

      if (unify (tparms, targs, TREE_TYPE (parm),
		 TREE_TYPE (arg), UNIFY_ALLOW_NONE))
	return 1;
      return type_unification_real (tparms, targs, TYPE_ARG_TYPES (parm),
				    TYPE_ARG_TYPES (arg), 1, DEDUCE_EXACT,
				    LOOKUP_NORMAL);

    case OFFSET_TYPE:
      /* Unify a pointer to member with a pointer to member function, which
	 deduces the type of the member as a function type. */
      if (TYPE_PTRMEMFUNC_P (arg))
	{
	  tree method_type;
	  tree fntype;
	  cp_cv_quals cv_quals;

	  /* Check top-level cv qualifiers */
	  if (!check_cv_quals_for_unify (UNIFY_ALLOW_NONE, arg, parm))
	    return 1;

	  if (unify (tparms, targs, TYPE_OFFSET_BASETYPE (parm),
		     TYPE_PTRMEMFUNC_OBJECT_TYPE (arg), UNIFY_ALLOW_NONE))
	    return 1;

	  /* Determine the type of the function we are unifying against. */
	  method_type = TREE_TYPE (TYPE_PTRMEMFUNC_FN_TYPE (arg));
	  fntype =
	    build_function_type (TREE_TYPE (method_type),
				 TREE_CHAIN (TYPE_ARG_TYPES (method_type)));

	  /* Extract the cv-qualifiers of the member function from the
	     implicit object parameter and place them on the function
	     type to be restored later. */
	  cv_quals =
	    cp_type_quals(TREE_TYPE (TREE_VALUE (TYPE_ARG_TYPES (method_type))));
	  fntype = build_qualified_type (fntype, cv_quals);
	  return unify (tparms, targs, TREE_TYPE (parm), fntype, strict);
	}

      if (TREE_CODE (arg) != OFFSET_TYPE)
	return 1;
      if (unify (tparms, targs, TYPE_OFFSET_BASETYPE (parm),
		 TYPE_OFFSET_BASETYPE (arg), UNIFY_ALLOW_NONE))
	return 1;
      return unify (tparms, targs, TREE_TYPE (parm), TREE_TYPE (arg),
		    strict);

    case CONST_DECL:
      if (DECL_TEMPLATE_PARM_P (parm))
	return unify (tparms, targs, DECL_INITIAL (parm), arg, strict);
      if (arg != integral_constant_value (parm))
	return 1;
      return 0;

    case FIELD_DECL:
    case TEMPLATE_DECL:
      /* Matched cases are handled by the ARG == PARM test above.  */
      return 1;

    default:
      gcc_assert (EXPR_P (parm));

      /* We must be looking at an expression.  This can happen with
	 something like:

	   template <int I>
	   void foo(S<I>, S<I + 2>);

	 This is a "nondeduced context":

	   [deduct.type]

	   The nondeduced contexts are:

	   --A type that is a template-id in which one or more of
	     the template-arguments is an expression that references
	     a template-parameter.

	 In these cases, we assume deduction succeeded, but don't
	 actually infer any unifications.  */

      if (!uses_template_parms (parm)
	  && !template_args_equal (parm, arg))
	return 1;
      else
	return 0;
    }
}

/* Note that DECL can be defined in this translation unit, if
   required.  */

static void
mark_definable (tree decl)
{
  tree clone;
  DECL_NOT_REALLY_EXTERN (decl) = 1;
  FOR_EACH_CLONE (clone, decl)
    DECL_NOT_REALLY_EXTERN (clone) = 1;
}

/* Called if RESULT is explicitly instantiated, or is a member of an
   explicitly instantiated class.  */

void
mark_decl_instantiated (tree result, int extern_p)
{
  SET_DECL_EXPLICIT_INSTANTIATION (result);

  /* If this entity has already been written out, it's too late to
     make any modifications.  */
  if (TREE_ASM_WRITTEN (result))
    return;

  if (TREE_CODE (result) != FUNCTION_DECL)
    /* The TREE_PUBLIC flag for function declarations will have been
       set correctly by tsubst.  */
    TREE_PUBLIC (result) = 1;

  /* This might have been set by an earlier implicit instantiation.  */
  DECL_COMDAT (result) = 0;

  if (extern_p)
    DECL_NOT_REALLY_EXTERN (result) = 0;
  else
    {
      mark_definable (result);
      /* Always make artificials weak.  */
      if (DECL_ARTIFICIAL (result) && flag_weak)
	comdat_linkage (result);
      /* For WIN32 we also want to put explicit instantiations in
	 linkonce sections.  */
      else if (TREE_PUBLIC (result))
	maybe_make_one_only (result);
    }

  /* If EXTERN_P, then this function will not be emitted -- unless
     followed by an explicit instantiation, at which point its linkage
     will be adjusted.  If !EXTERN_P, then this function will be
     emitted here.  In neither circumstance do we want
     import_export_decl to adjust the linkage.  */
  DECL_INTERFACE_KNOWN (result) = 1;
}

/* Given two function templates PAT1 and PAT2, return:

   1 if PAT1 is more specialized than PAT2 as described in [temp.func.order].
   -1 if PAT2 is more specialized than PAT1.
   0 if neither is more specialized.

   LEN indicates the number of parameters we should consider
   (defaulted parameters should not be considered).

   The 1998 std underspecified function template partial ordering, and
   DR214 addresses the issue.  We take pairs of arguments, one from
   each of the templates, and deduce them against each other.  One of
   the templates will be more specialized if all the *other*
   template's arguments deduce against its arguments and at least one
   of its arguments *does* *not* deduce against the other template's
   corresponding argument.  Deduction is done as for class templates.
   The arguments used in deduction have reference and top level cv
   qualifiers removed.  Iff both arguments were originally reference
   types *and* deduction succeeds in both directions, the template
   with the more cv-qualified argument wins for that pairing (if
   neither is more cv-qualified, they both are equal).  Unlike regular
   deduction, after all the arguments have been deduced in this way,
   we do *not* verify the deduced template argument values can be
   substituted into non-deduced contexts, nor do we have to verify
   that all template arguments have been deduced.  */

int
more_specialized_fn (tree pat1, tree pat2, int len)
{
  tree decl1 = DECL_TEMPLATE_RESULT (pat1);
  tree decl2 = DECL_TEMPLATE_RESULT (pat2);
  tree targs1 = make_tree_vec (DECL_NTPARMS (pat1));
  tree targs2 = make_tree_vec (DECL_NTPARMS (pat2));
  tree tparms1 = DECL_INNERMOST_TEMPLATE_PARMS (pat1);
  tree tparms2 = DECL_INNERMOST_TEMPLATE_PARMS (pat2);
  tree args1 = TYPE_ARG_TYPES (TREE_TYPE (decl1));
  tree args2 = TYPE_ARG_TYPES (TREE_TYPE (decl2));
  int better1 = 0;
  int better2 = 0;

  /* Remove the this parameter from non-static member functions.  If
     one is a non-static member function and the other is not a static
     member function, remove the first parameter from that function
     also.  This situation occurs for operator functions where we
     locate both a member function (with this pointer) and non-member
     operator (with explicit first operand).  */
  if (DECL_NONSTATIC_MEMBER_FUNCTION_P (decl1))
    {
      len--; /* LEN is the number of significant arguments for DECL1 */
      args1 = TREE_CHAIN (args1);
      if (!DECL_STATIC_FUNCTION_P (decl2))
	args2 = TREE_CHAIN (args2);
    }
  else if (DECL_NONSTATIC_MEMBER_FUNCTION_P (decl2))
    {
      args2 = TREE_CHAIN (args2);
      if (!DECL_STATIC_FUNCTION_P (decl1))
	{
	  len--;
	  args1 = TREE_CHAIN (args1);
	}
    }

  /* If only one is a conversion operator, they are unordered.  */
  if (DECL_CONV_FN_P (decl1) != DECL_CONV_FN_P (decl2))
    return 0;

  /* Consider the return type for a conversion function */
  if (DECL_CONV_FN_P (decl1))
    {
      args1 = tree_cons (NULL_TREE, TREE_TYPE (TREE_TYPE (decl1)), args1);
      args2 = tree_cons (NULL_TREE, TREE_TYPE (TREE_TYPE (decl2)), args2);
      len++;
    }

  processing_template_decl++;

  while (len--)
    {
      tree arg1 = TREE_VALUE (args1);
      tree arg2 = TREE_VALUE (args2);
      int deduce1, deduce2;
      int quals1 = -1;
      int quals2 = -1;

      if (TREE_CODE (arg1) == REFERENCE_TYPE)
	{
	  arg1 = TREE_TYPE (arg1);
	  quals1 = cp_type_quals (arg1);
	}

      if (TREE_CODE (arg2) == REFERENCE_TYPE)
	{
	  arg2 = TREE_TYPE (arg2);
	  quals2 = cp_type_quals (arg2);
	}

      if ((quals1 < 0) != (quals2 < 0))
	{
	  /* Only of the args is a reference, see if we should apply
	     array/function pointer decay to it.  This is not part of
	     DR214, but is, IMHO, consistent with the deduction rules
	     for the function call itself, and with our earlier
	     implementation of the underspecified partial ordering
	     rules.  (nathan).  */
	  if (quals1 >= 0)
	    {
	      switch (TREE_CODE (arg1))
		{
		case ARRAY_TYPE:
		  arg1 = TREE_TYPE (arg1);
		  /* FALLTHROUGH. */
		case FUNCTION_TYPE:
		  arg1 = build_pointer_type (arg1);
		  break;

		default:
		  break;
		}
	    }
	  else
	    {
	      switch (TREE_CODE (arg2))
		{
		case ARRAY_TYPE:
		  arg2 = TREE_TYPE (arg2);
		  /* FALLTHROUGH. */
		case FUNCTION_TYPE:
		  arg2 = build_pointer_type (arg2);
		  break;

		default:
		  break;
		}
	    }
	}

      arg1 = TYPE_MAIN_VARIANT (arg1);
      arg2 = TYPE_MAIN_VARIANT (arg2);

      deduce1 = !unify (tparms1, targs1, arg1, arg2, UNIFY_ALLOW_NONE);
      deduce2 = !unify (tparms2, targs2, arg2, arg1, UNIFY_ALLOW_NONE);

      if (!deduce1)
	better2 = -1;
      if (!deduce2)
	better1 = -1;
      if (better1 < 0 && better2 < 0)
	/* We've failed to deduce something in either direction.
	   These must be unordered.  */
	break;

      if (deduce1 && deduce2 && quals1 >= 0 && quals2 >= 0)
	{
	  /* Deduces in both directions, see if quals can
	     disambiguate.  Pretend the worse one failed to deduce. */
	  if ((quals1 & quals2) == quals2)
	    deduce1 = 0;
	  if ((quals1 & quals2) == quals1)
	    deduce2 = 0;
	}
      if (deduce1 && !deduce2 && !better2)
	better2 = 1;
      if (deduce2 && !deduce1 && !better1)
	better1 = 1;

      args1 = TREE_CHAIN (args1);
      args2 = TREE_CHAIN (args2);
    }

  processing_template_decl--;

  return (better1 > 0) - (better2 > 0);
}

/* Determine which of two partial specializations is more specialized.

   PAT1 is a TREE_LIST whose TREE_TYPE is the _TYPE node corresponding
   to the first partial specialization.  The TREE_VALUE is the
   innermost set of template parameters for the partial
   specialization.  PAT2 is similar, but for the second template.

   Return 1 if the first partial specialization is more specialized;
   -1 if the second is more specialized; 0 if neither is more
   specialized.

   See [temp.class.order] for information about determining which of
   two templates is more specialized.  */

static int
more_specialized_class (tree pat1, tree pat2)
{
  tree targs;
  tree tmpl1, tmpl2;
  int winner = 0;

  tmpl1 = TREE_TYPE (pat1);
  tmpl2 = TREE_TYPE (pat2);

  /* Just like what happens for functions, if we are ordering between
     different class template specializations, we may encounter dependent
     types in the arguments, and we need our dependency check functions
     to behave correctly.  */
  ++processing_template_decl;
  targs = get_class_bindings (TREE_VALUE (pat1),
			      CLASSTYPE_TI_ARGS (tmpl1),
			      CLASSTYPE_TI_ARGS (tmpl2));
  if (targs)
    --winner;

  targs = get_class_bindings (TREE_VALUE (pat2),
			      CLASSTYPE_TI_ARGS (tmpl2),
			      CLASSTYPE_TI_ARGS (tmpl1));
  if (targs)
    ++winner;
  --processing_template_decl;

  return winner;
}

/* Return the template arguments that will produce the function signature
   DECL from the function template FN, with the explicit template
   arguments EXPLICIT_ARGS.  If CHECK_RETTYPE is true, the return type must
   also match.  Return NULL_TREE if no satisfactory arguments could be
   found.  */

static tree
get_bindings (tree fn, tree decl, tree explicit_args, bool check_rettype)
{
  int ntparms = DECL_NTPARMS (fn);
  tree targs = make_tree_vec (ntparms);
  tree decl_type;
  tree decl_arg_types;

  /* Substitute the explicit template arguments into the type of DECL.
     The call to fn_type_unification will handle substitution into the
     FN.  */
  decl_type = TREE_TYPE (decl);
  if (explicit_args && uses_template_parms (decl_type))
    {
      tree tmpl;
      tree converted_args;

      if (DECL_TEMPLATE_INFO (decl))
	tmpl = DECL_TI_TEMPLATE (decl);
      else
	/* We can get here for some invalid specializations.  */
	return NULL_TREE;

      converted_args
	= coerce_template_parms (DECL_INNERMOST_TEMPLATE_PARMS (tmpl),
				 explicit_args, NULL_TREE,
				 tf_none,
				 /*require_all_args=*/false,
				 /*use_default_args=*/false);
      if (converted_args == error_mark_node)
	return NULL_TREE;

      decl_type = tsubst (decl_type, converted_args, tf_none, NULL_TREE);
      if (decl_type == error_mark_node)
	return NULL_TREE;
    }

  /* Never do unification on the 'this' parameter.  */
  decl_arg_types = skip_artificial_parms_for (decl, 
					      TYPE_ARG_TYPES (decl_type));

  if (fn_type_unification (fn, explicit_args, targs,
			   decl_arg_types,
			   (check_rettype || DECL_CONV_FN_P (fn)
			    ? TREE_TYPE (decl_type) : NULL_TREE),
			   DEDUCE_EXACT, LOOKUP_NORMAL))
    return NULL_TREE;

  return targs;
}

/* Return the innermost template arguments that, when applied to a
   template specialization whose innermost template parameters are
   TPARMS, and whose specialization arguments are PARMS, yield the
   ARGS.

   For example, suppose we have:

     template <class T, class U> struct S {};
     template <class T> struct S<T*, int> {};

   Then, suppose we want to get `S<double*, int>'.  The TPARMS will be
   {T}, the SPEC_ARGS will be {T*, int} and the ARGS will be {double*,
   int}.  The resulting vector will be {double}, indicating that `T'
   is bound to `double'.  */

static tree
get_class_bindings (tree tparms, tree spec_args, tree args)
{
  int i, ntparms = TREE_VEC_LENGTH (tparms);
  tree deduced_args;
  tree innermost_deduced_args;

  innermost_deduced_args = make_tree_vec (ntparms);
  if (TMPL_ARGS_HAVE_MULTIPLE_LEVELS (args))
    {
      deduced_args = copy_node (args);
      SET_TMPL_ARGS_LEVEL (deduced_args,
			   TMPL_ARGS_DEPTH (deduced_args),
			   innermost_deduced_args);
    }
  else
    deduced_args = innermost_deduced_args;

  if (unify (tparms, deduced_args,
	     INNERMOST_TEMPLATE_ARGS (spec_args),
	     INNERMOST_TEMPLATE_ARGS (args),
	     UNIFY_ALLOW_NONE))
    return NULL_TREE;

  for (i =  0; i < ntparms; ++i)
    if (! TREE_VEC_ELT (innermost_deduced_args, i))
      return NULL_TREE;

  /* Verify that nondeduced template arguments agree with the type
     obtained from argument deduction.

     For example:

       struct A { typedef int X; };
       template <class T, class U> struct C {};
       template <class T> struct C<T, typename T::X> {};

     Then with the instantiation `C<A, int>', we can deduce that
     `T' is `A' but unify () does not check whether `typename T::X'
     is `int'.  */
  spec_args = tsubst (spec_args, deduced_args, tf_none, NULL_TREE);
  if (spec_args == error_mark_node
      /* We only need to check the innermost arguments; the other
	 arguments will always agree.  */
      || !comp_template_args (INNERMOST_TEMPLATE_ARGS (spec_args),
			      INNERMOST_TEMPLATE_ARGS (args)))
    return NULL_TREE;

  return deduced_args;
}

/* TEMPLATES is a TREE_LIST.  Each TREE_VALUE is a TEMPLATE_DECL.
   Return the TREE_LIST node with the most specialized template, if
   any.  If there is no most specialized template, the error_mark_node
   is returned.

   Note that this function does not look at, or modify, the
   TREE_PURPOSE or TREE_TYPE of any of the nodes.  Since the node
   returned is one of the elements of INSTANTIATIONS, callers may
   store information in the TREE_PURPOSE or TREE_TYPE of the nodes,
   and retrieve it from the value returned.  */

tree
most_specialized_instantiation (tree templates)
{
  tree fn, champ;

  ++processing_template_decl;

  champ = templates;
  for (fn = TREE_CHAIN (templates); fn; fn = TREE_CHAIN (fn))
    {
      int fate = 0;

      if (get_bindings (TREE_VALUE (champ),
			DECL_TEMPLATE_RESULT (TREE_VALUE (fn)),
			NULL_TREE, /*check_ret=*/false))
	fate--;

      if (get_bindings (TREE_VALUE (fn),
			DECL_TEMPLATE_RESULT (TREE_VALUE (champ)),
			NULL_TREE, /*check_ret=*/false))
	fate++;

      if (fate == -1)
	champ = fn;
      else if (!fate)
	{
	  /* Equally specialized, move to next function.  If there
	     is no next function, nothing's most specialized.  */
	  fn = TREE_CHAIN (fn);
	  champ = fn;
	  if (!fn)
	    break;
	}
    }

  if (champ)
    /* Now verify that champ is better than everything earlier in the
       instantiation list.  */
    for (fn = templates; fn != champ; fn = TREE_CHAIN (fn))
      if (get_bindings (TREE_VALUE (champ),
			DECL_TEMPLATE_RESULT (TREE_VALUE (fn)),
			NULL_TREE, /*check_ret=*/false)
	  || !get_bindings (TREE_VALUE (fn),
			    DECL_TEMPLATE_RESULT (TREE_VALUE (champ)),
			    NULL_TREE, /*check_ret=*/false))
	{
	  champ = NULL_TREE;
	  break;
	}

  processing_template_decl--;

  if (!champ)
    return error_mark_node;

  return champ;
}

/* If DECL is a specialization of some template, return the most
   general such template.  Otherwise, returns NULL_TREE.

   For example, given:

     template <class T> struct S { template <class U> void f(U); };

   if TMPL is `template <class U> void S<int>::f(U)' this will return
   the full template.  This function will not trace past partial
   specializations, however.  For example, given in addition:

     template <class T> struct S<T*> { template <class U> void f(U); };

   if TMPL is `template <class U> void S<int*>::f(U)' this will return
   `template <class T> template <class U> S<T*>::f(U)'.  */

tree
most_general_template (tree decl)
{
  /* If DECL is a FUNCTION_DECL, find the TEMPLATE_DECL of which it is
     an immediate specialization.  */
  if (TREE_CODE (decl) == FUNCTION_DECL)
    {
      if (DECL_TEMPLATE_INFO (decl)) {
	decl = DECL_TI_TEMPLATE (decl);

	/* The DECL_TI_TEMPLATE can be an IDENTIFIER_NODE for a
	   template friend.  */
	if (TREE_CODE (decl) != TEMPLATE_DECL)
	  return NULL_TREE;
      } else
	return NULL_TREE;
    }

  /* Look for more and more general templates.  */
  while (DECL_TEMPLATE_INFO (decl))
    {
      /* The DECL_TI_TEMPLATE can be an IDENTIFIER_NODE in some cases.
	 (See cp-tree.h for details.)  */
      if (TREE_CODE (DECL_TI_TEMPLATE (decl)) != TEMPLATE_DECL)
	break;

      if (CLASS_TYPE_P (TREE_TYPE (decl))
	  && CLASSTYPE_TEMPLATE_SPECIALIZATION (TREE_TYPE (decl)))
	break;

      /* Stop if we run into an explicitly specialized class template.  */
      if (!DECL_NAMESPACE_SCOPE_P (decl)
	  && DECL_CONTEXT (decl)
	  && CLASSTYPE_TEMPLATE_SPECIALIZATION (DECL_CONTEXT (decl)))
	break;

      decl = DECL_TI_TEMPLATE (decl);
    }

  return decl;
}

/* Return the most specialized of the class template partial
   specializations of TMPL which can produce TYPE, a specialization of
   TMPL.  The value returned is actually a TREE_LIST; the TREE_TYPE is
   a _TYPE node corresponding to the partial specialization, while the
   TREE_PURPOSE is the set of template arguments that must be
   substituted into the TREE_TYPE in order to generate TYPE.

   If the choice of partial specialization is ambiguous, a diagnostic
   is issued, and the error_mark_node is returned.  If there are no
   partial specializations of TMPL matching TYPE, then NULL_TREE is
   returned.  */

static tree
most_specialized_class (tree type, tree tmpl)
{
  tree list = NULL_TREE;
  tree t;
  tree champ;
  int fate;
  bool ambiguous_p;
  tree args;

  tmpl = most_general_template (tmpl);
  args = CLASSTYPE_TI_ARGS (type);
  for (t = DECL_TEMPLATE_SPECIALIZATIONS (tmpl); t; t = TREE_CHAIN (t))
    {
      tree partial_spec_args;
      tree spec_args;

      partial_spec_args = CLASSTYPE_TI_ARGS (TREE_TYPE (t));
      spec_args = get_class_bindings (TREE_VALUE (t),
				      partial_spec_args,
				      args);
      if (spec_args)
	{
	  list = tree_cons (spec_args, TREE_VALUE (t), list);
	  TREE_TYPE (list) = TREE_TYPE (t);
	}
    }

  if (! list)
    return NULL_TREE;

  ambiguous_p = false;
  t = list;
  champ = t;
  t = TREE_CHAIN (t);
  for (; t; t = TREE_CHAIN (t))
    {
      fate = more_specialized_class (champ, t);
      if (fate == 1)
	;
      else
	{
	  if (fate == 0)
	    {
	      t = TREE_CHAIN (t);
	      if (! t)
		{
		  ambiguous_p = true;
		  break;
		}
	    }
	  champ = t;
	}
    }

  if (!ambiguous_p)
    for (t = list; t && t != champ; t = TREE_CHAIN (t))
      {
	fate = more_specialized_class (champ, t);
	if (fate != 1)
	  {
	    ambiguous_p = true;
	    break;
	  }
      }

  if (ambiguous_p)
    {
      const char *str = "candidates are:";
      error ("ambiguous class template instantiation for %q#T", type);
      for (t = list; t; t = TREE_CHAIN (t))
	{
	  error ("%s %+#T", str, TREE_TYPE (t));
	  str = "               ";
	}
      return error_mark_node;
    }

  return champ;
}

/* Explicitly instantiate DECL.  */

void
do_decl_instantiation (tree decl, tree storage)
{
  tree result = NULL_TREE;
  int extern_p = 0;

  if (!decl || decl == error_mark_node)
    /* An error occurred, for which grokdeclarator has already issued
       an appropriate message.  */
    return;
  else if (! DECL_LANG_SPECIFIC (decl))
    {
      error ("explicit instantiation of non-template %q#D", decl);
      return;
    }
  else if (TREE_CODE (decl) == VAR_DECL)
    {
      /* There is an asymmetry here in the way VAR_DECLs and
	 FUNCTION_DECLs are handled by grokdeclarator.  In the case of
	 the latter, the DECL we get back will be marked as a
	 template instantiation, and the appropriate
	 DECL_TEMPLATE_INFO will be set up.  This does not happen for
	 VAR_DECLs so we do the lookup here.  Probably, grokdeclarator
	 should handle VAR_DECLs as it currently handles
	 FUNCTION_DECLs.  */
      result = lookup_field (DECL_CONTEXT (decl), DECL_NAME (decl), 0, false);
      if (!result || TREE_CODE (result) != VAR_DECL)
	{
	  error ("no matching template for %qD found", decl);
	  return;
	}
    }
  else if (TREE_CODE (decl) != FUNCTION_DECL)
    {
      error ("explicit instantiation of %q#D", decl);
      return;
    }
  else
    result = decl;

  /* Check for various error cases.  Note that if the explicit
     instantiation is valid the RESULT will currently be marked as an
     *implicit* instantiation; DECL_EXPLICIT_INSTANTIATION is not set
     until we get here.  */

  if (DECL_TEMPLATE_SPECIALIZATION (result))
    {
      /* DR 259 [temp.spec].

	 Both an explicit instantiation and a declaration of an explicit
	 specialization shall not appear in a program unless the explicit
	 instantiation follows a declaration of the explicit specialization.

	 For a given set of template parameters, if an explicit
	 instantiation of a template appears after a declaration of an
	 explicit specialization for that template, the explicit
	 instantiation has no effect.  */
      return;
    }
  else if (DECL_EXPLICIT_INSTANTIATION (result))
    {
      /* [temp.spec]

	 No program shall explicitly instantiate any template more
	 than once.

	 We check DECL_NOT_REALLY_EXTERN so as not to complain when
	 the first instantiation was `extern' and the second is not,
	 and EXTERN_P for the opposite case.  */
      if (DECL_NOT_REALLY_EXTERN (result) && !extern_p)
	pedwarn ("duplicate explicit instantiation of %q#D", result);
      /* If an "extern" explicit instantiation follows an ordinary
	 explicit instantiation, the template is instantiated.  */
      if (extern_p)
	return;
    }
  else if (!DECL_IMPLICIT_INSTANTIATION (result))
    {
      error ("no matching template for %qD found", result);
      return;
    }
  else if (!DECL_TEMPLATE_INFO (result))
    {
      pedwarn ("explicit instantiation of non-template %q#D", result);
      return;
    }

  if (storage == NULL_TREE)
    ;
  else if (storage == ridpointers[(int) RID_EXTERN])
    {
      if (pedantic && !in_system_header)
	pedwarn ("ISO C++ forbids the use of %<extern%> on explicit "
		 "instantiations");
      extern_p = 1;
    }
  else
    error ("storage class %qD applied to template instantiation", storage);

  check_explicit_instantiation_namespace (result);
  mark_decl_instantiated (result, extern_p);
  if (! extern_p)
    instantiate_decl (result, /*defer_ok=*/1,
		      /*expl_inst_class_mem_p=*/false);
}

static void
mark_class_instantiated (tree t, int extern_p)
{
  SET_CLASSTYPE_EXPLICIT_INSTANTIATION (t);
  SET_CLASSTYPE_INTERFACE_KNOWN (t);
  CLASSTYPE_INTERFACE_ONLY (t) = extern_p;
  TYPE_DECL_SUPPRESS_DEBUG (TYPE_NAME (t)) = extern_p;
  if (! extern_p)
    {
      CLASSTYPE_DEBUG_REQUESTED (t) = 1;
      rest_of_type_compilation (t, 1);
    }
}

/* Called from do_type_instantiation through binding_table_foreach to
   do recursive instantiation for the type bound in ENTRY.  */
static void
bt_instantiate_type_proc (binding_entry entry, void *data)
{
  tree storage = *(tree *) data;

  if (IS_AGGR_TYPE (entry->type)
      && !uses_template_parms (CLASSTYPE_TI_ARGS (entry->type)))
    do_type_instantiation (TYPE_MAIN_DECL (entry->type), storage, 0);
}

/* Called from do_type_instantiation to instantiate a member
   (a member function or a static member variable) of an
   explicitly instantiated class template.  */
static void
instantiate_class_member (tree decl, int extern_p)
{
  mark_decl_instantiated (decl, extern_p);
  if (! extern_p)
    instantiate_decl (decl, /*defer_ok=*/1,
		      /*expl_inst_class_mem_p=*/true);
}

/* Perform an explicit instantiation of template class T.  STORAGE, if
   non-null, is the RID for extern, inline or static.  COMPLAIN is
   nonzero if this is called from the parser, zero if called recursively,
   since the standard is unclear (as detailed below).  */

void
do_type_instantiation (tree t, tree storage, tsubst_flags_t complain)
{
  int extern_p = 0;
  int nomem_p = 0;
  int static_p = 0;
  int previous_instantiation_extern_p = 0;

  if (TREE_CODE (t) == TYPE_DECL)
    t = TREE_TYPE (t);

  if (! CLASS_TYPE_P (t) || ! CLASSTYPE_TEMPLATE_INFO (t))
    {
      error ("explicit instantiation of non-template type %qT", t);
      return;
    }

  complete_type (t);

  if (!COMPLETE_TYPE_P (t))
    {
      if (complain & tf_error)
	error ("explicit instantiation of %q#T before definition of template",
	       t);
      return;
    }

  if (storage != NULL_TREE)
    {
      if (pedantic && !in_system_header)
	pedwarn("ISO C++ forbids the use of %qE on explicit instantiations",
		storage);

      if (storage == ridpointers[(int) RID_INLINE])
	nomem_p = 1;
      else if (storage == ridpointers[(int) RID_EXTERN])
	extern_p = 1;
      else if (storage == ridpointers[(int) RID_STATIC])
	static_p = 1;
      else
	{
	  error ("storage class %qD applied to template instantiation",
		 storage);
	  extern_p = 0;
	}
    }

  if (CLASSTYPE_TEMPLATE_SPECIALIZATION (t))
    {
      /* DR 259 [temp.spec].

	 Both an explicit instantiation and a declaration of an explicit
	 specialization shall not appear in a program unless the explicit
	 instantiation follows a declaration of the explicit specialization.

	 For a given set of template parameters, if an explicit
	 instantiation of a template appears after a declaration of an
	 explicit specialization for that template, the explicit
	 instantiation has no effect.  */
      return;
    }
  else if (CLASSTYPE_EXPLICIT_INSTANTIATION (t))
    {
      /* [temp.spec]

	 No program shall explicitly instantiate any template more
	 than once.

	 If PREVIOUS_INSTANTIATION_EXTERN_P, then the first explicit
	 instantiation was `extern'.  If EXTERN_P then the second is.
	 These cases are OK.  */
      previous_instantiation_extern_p = CLASSTYPE_INTERFACE_ONLY (t);

      if (!previous_instantiation_extern_p && !extern_p
	  && (complain & tf_error))
	pedwarn ("duplicate explicit instantiation of %q#T", t);

      /* If we've already instantiated the template, just return now.  */
      if (!CLASSTYPE_INTERFACE_ONLY (t))
	return;
    }

  check_explicit_instantiation_namespace (TYPE_NAME (t));
  mark_class_instantiated (t, extern_p);

  if (nomem_p)
    return;

  {
    tree tmp;

    /* In contrast to implicit instantiation, where only the
       declarations, and not the definitions, of members are
       instantiated, we have here:

	 [temp.explicit]

	 The explicit instantiation of a class template specialization
	 implies the instantiation of all of its members not
	 previously explicitly specialized in the translation unit
	 containing the explicit instantiation.

       Of course, we can't instantiate member template classes, since
       we don't have any arguments for them.  Note that the standard
       is unclear on whether the instantiation of the members are
       *explicit* instantiations or not.  However, the most natural
       interpretation is that it should be an explicit instantiation.  */

    if (! static_p)
      for (tmp = TYPE_METHODS (t); tmp; tmp = TREE_CHAIN (tmp))
	if (TREE_CODE (tmp) == FUNCTION_DECL
	    && DECL_TEMPLATE_INSTANTIATION (tmp))
	  instantiate_class_member (tmp, extern_p);

    for (tmp = TYPE_FIELDS (t); tmp; tmp = TREE_CHAIN (tmp))
      if (TREE_CODE (tmp) == VAR_DECL && DECL_TEMPLATE_INSTANTIATION (tmp))
	instantiate_class_member (tmp, extern_p);

    if (CLASSTYPE_NESTED_UTDS (t))
      binding_table_foreach (CLASSTYPE_NESTED_UTDS (t),
			     bt_instantiate_type_proc, &storage);
  }
}

/* Given a function DECL, which is a specialization of TMPL, modify
   DECL to be a re-instantiation of TMPL with the same template
   arguments.  TMPL should be the template into which tsubst'ing
   should occur for DECL, not the most general template.

   One reason for doing this is a scenario like this:

     template <class T>
     void f(const T&, int i);

     void g() { f(3, 7); }

     template <class T>
     void f(const T& t, const int i) { }

   Note that when the template is first instantiated, with
   instantiate_template, the resulting DECL will have no name for the
   first parameter, and the wrong type for the second.  So, when we go
   to instantiate the DECL, we regenerate it.  */

static void
regenerate_decl_from_template (tree decl, tree tmpl)
{
  /* The arguments used to instantiate DECL, from the most general
     template.  */
  tree args;
  tree code_pattern;

  args = DECL_TI_ARGS (decl);
  code_pattern = DECL_TEMPLATE_RESULT (tmpl);

  /* Make sure that we can see identifiers, and compute access
     correctly.  */
  push_access_scope (decl);

  if (TREE_CODE (decl) == FUNCTION_DECL)
    {
      tree decl_parm;
      tree pattern_parm;
      tree specs;
      int args_depth;
      int parms_depth;

      args_depth = TMPL_ARGS_DEPTH (args);
      parms_depth = TMPL_PARMS_DEPTH (DECL_TEMPLATE_PARMS (tmpl));
      if (args_depth > parms_depth)
	args = get_innermost_template_args (args, parms_depth);

      specs = tsubst_exception_specification (TREE_TYPE (code_pattern),
					      args, tf_error, NULL_TREE);
      if (specs)
	TREE_TYPE (decl) = build_exception_variant (TREE_TYPE (decl),
						    specs);

      /* Merge parameter declarations.  */
      decl_parm = skip_artificial_parms_for (decl,
					     DECL_ARGUMENTS (decl));
      pattern_parm
	= skip_artificial_parms_for (code_pattern,
				     DECL_ARGUMENTS (code_pattern));
      while (decl_parm)
	{
	  tree parm_type;
	  tree attributes;

	  if (DECL_NAME (decl_parm) != DECL_NAME (pattern_parm))
	    DECL_NAME (decl_parm) = DECL_NAME (pattern_parm);
	  parm_type = tsubst (TREE_TYPE (pattern_parm), args, tf_error,
			      NULL_TREE);
	  parm_type = type_decays_to (parm_type);
	  if (!same_type_p (TREE_TYPE (decl_parm), parm_type))
	    TREE_TYPE (decl_parm) = parm_type;
	  attributes = DECL_ATTRIBUTES (pattern_parm);
	  if (DECL_ATTRIBUTES (decl_parm) != attributes)
	    {
	      DECL_ATTRIBUTES (decl_parm) = attributes;
	      cplus_decl_attributes (&decl_parm, attributes, /*flags=*/0);
	    }
	  decl_parm = TREE_CHAIN (decl_parm);
	  pattern_parm = TREE_CHAIN (pattern_parm);
	}

      /* Merge additional specifiers from the CODE_PATTERN.  */
      if (DECL_DECLARED_INLINE_P (code_pattern)
	  && !DECL_DECLARED_INLINE_P (decl))
	DECL_DECLARED_INLINE_P (decl) = 1;
      if (DECL_INLINE (code_pattern) && !DECL_INLINE (decl))
	DECL_INLINE (decl) = 1;
    }
  else if (TREE_CODE (decl) == VAR_DECL)
    DECL_INITIAL (decl) =
      tsubst_expr (DECL_INITIAL (code_pattern), args,
		   tf_error, DECL_TI_TEMPLATE (decl),
		   /*integral_constant_expression_p=*/false);
  else
    gcc_unreachable ();

  pop_access_scope (decl);
}

/* Return the TEMPLATE_DECL into which DECL_TI_ARGS(DECL) should be
   substituted to get DECL.  */

tree
template_for_substitution (tree decl)
{
  tree tmpl = DECL_TI_TEMPLATE (decl);

  /* Set TMPL to the template whose DECL_TEMPLATE_RESULT is the pattern
     for the instantiation.  This is not always the most general
     template.  Consider, for example:

	template <class T>
	struct S { template <class U> void f();
		   template <> void f<int>(); };

     and an instantiation of S<double>::f<int>.  We want TD to be the
     specialization S<T>::f<int>, not the more general S<T>::f<U>.  */
  while (/* An instantiation cannot have a definition, so we need a
	    more general template.  */
	 DECL_TEMPLATE_INSTANTIATION (tmpl)
	   /* We must also deal with friend templates.  Given:

		template <class T> struct S {
		  template <class U> friend void f() {};
		};

	      S<int>::f<U> say, is not an instantiation of S<T>::f<U>,
	      so far as the language is concerned, but that's still
	      where we get the pattern for the instantiation from.  On
	      other hand, if the definition comes outside the class, say:

		template <class T> struct S {
		  template <class U> friend void f();
		};
		template <class U> friend void f() {}

	      we don't need to look any further.  That's what the check for
	      DECL_INITIAL is for.  */
	  || (TREE_CODE (decl) == FUNCTION_DECL
	      && DECL_FRIEND_PSEUDO_TEMPLATE_INSTANTIATION (tmpl)
	      && !DECL_INITIAL (DECL_TEMPLATE_RESULT (tmpl))))
    {
      /* The present template, TD, should not be a definition.  If it
	 were a definition, we should be using it!  Note that we
	 cannot restructure the loop to just keep going until we find
	 a template with a definition, since that might go too far if
	 a specialization was declared, but not defined.  */
      gcc_assert (TREE_CODE (decl) != VAR_DECL
		  || DECL_IN_AGGR_P (DECL_TEMPLATE_RESULT (tmpl)));

      /* Fetch the more general template.  */
      tmpl = DECL_TI_TEMPLATE (tmpl);
    }

  return tmpl;
}

/* Produce the definition of D, a _DECL generated from a template.  If
   DEFER_OK is nonzero, then we don't have to actually do the
   instantiation now; we just have to do it sometime.  Normally it is
   an error if this is an explicit instantiation but D is undefined.
   EXPL_INST_CLASS_MEM_P is true iff D is a member of an
   explicitly instantiated class template.  */

tree
instantiate_decl (tree d, int defer_ok,
		  bool expl_inst_class_mem_p)
{
  tree tmpl = DECL_TI_TEMPLATE (d);
  tree gen_args;
  tree args;
  tree td;
  tree code_pattern;
  tree spec;
  tree gen_tmpl;
  bool pattern_defined;
  int need_push;
  location_t saved_loc = input_location;
  int saved_in_system_header = in_system_header;
  bool external_p;

  /* This function should only be used to instantiate templates for
     functions and static member variables.  */
  gcc_assert (TREE_CODE (d) == FUNCTION_DECL
	      || TREE_CODE (d) == VAR_DECL);

  /* Variables are never deferred; if instantiation is required, they
     are instantiated right away.  That allows for better code in the
     case that an expression refers to the value of the variable --
     if the variable has a constant value the referring expression can
     take advantage of that fact.  */
  if (TREE_CODE (d) == VAR_DECL)
    defer_ok = 0;

  /* Don't instantiate cloned functions.  Instead, instantiate the
     functions they cloned.  */
  if (TREE_CODE (d) == FUNCTION_DECL && DECL_CLONED_FUNCTION_P (d))
    d = DECL_CLONED_FUNCTION (d);

  if (DECL_TEMPLATE_INSTANTIATED (d))
    /* D has already been instantiated.  It might seem reasonable to
       check whether or not D is an explicit instantiation, and, if so,
       stop here.  But when an explicit instantiation is deferred
       until the end of the compilation, DECL_EXPLICIT_INSTANTIATION
       is set, even though we still need to do the instantiation.  */
    return d;

  /* If we already have a specialization of this declaration, then
     there's no reason to instantiate it.  Note that
     retrieve_specialization gives us both instantiations and
     specializations, so we must explicitly check
     DECL_TEMPLATE_SPECIALIZATION.  */
  gen_tmpl = most_general_template (tmpl);
  gen_args = DECL_TI_ARGS (d);
  spec = retrieve_specialization (gen_tmpl, gen_args,
				  /*class_specializations_p=*/false);
  if (spec != NULL_TREE && DECL_TEMPLATE_SPECIALIZATION (spec))
    return spec;

  /* This needs to happen before any tsubsting.  */
  if (! push_tinst_level (d))
    return d;

  timevar_push (TV_PARSE);

  /* Set TD to the template whose DECL_TEMPLATE_RESULT is the pattern
     for the instantiation.  */
  td = template_for_substitution (d);
  code_pattern = DECL_TEMPLATE_RESULT (td);

  /* We should never be trying to instantiate a member of a class
     template or partial specialization.  */
  gcc_assert (d != code_pattern);

  if ((DECL_NAMESPACE_SCOPE_P (d) && !DECL_INITIALIZED_IN_CLASS_P (d))
      || DECL_TEMPLATE_SPECIALIZATION (td))
    /* In the case of a friend template whose definition is provided
       outside the class, we may have too many arguments.  Drop the
       ones we don't need.  The same is true for specializations.  */
    args = get_innermost_template_args
      (gen_args, TMPL_PARMS_DEPTH  (DECL_TEMPLATE_PARMS (td)));
  else
    args = gen_args;

  if (TREE_CODE (d) == FUNCTION_DECL)
    pattern_defined = (DECL_SAVED_TREE (code_pattern) != NULL_TREE);
  else
    pattern_defined = ! DECL_IN_AGGR_P (code_pattern);

  /* We may be in the middle of deferred access check.  Disable it now.  */
  push_deferring_access_checks (dk_no_deferred);

  /* Unless an explicit instantiation directive has already determined
     the linkage of D, remember that a definition is available for
     this entity.  */
  if (pattern_defined
      && !DECL_INTERFACE_KNOWN (d)
      && !DECL_NOT_REALLY_EXTERN (d))
    mark_definable (d);

  input_location = DECL_SOURCE_LOCATION (d);
  in_system_header = DECL_IN_SYSTEM_HEADER (d);

  /* If D is a member of an explicitly instantiated class template,
     and no definition is available, treat it like an implicit
     instantiation.  */
  if (!pattern_defined && expl_inst_class_mem_p
      && DECL_EXPLICIT_INSTANTIATION (d))
    {
      DECL_NOT_REALLY_EXTERN (d) = 0;
      DECL_INTERFACE_KNOWN (d) = 0;
      SET_DECL_IMPLICIT_INSTANTIATION (d);
    }

  if (!defer_ok)
    {
      /* Recheck the substitutions to obtain any warning messages
	 about ignoring cv qualifiers.  */
      tree gen = DECL_TEMPLATE_RESULT (gen_tmpl);
      tree type = TREE_TYPE (gen);

      /* Make sure that we can see identifiers, and compute access
	 correctly.  D is already the target FUNCTION_DECL with the
	 right context.  */
      push_access_scope (d);

      if (TREE_CODE (gen) == FUNCTION_DECL)
	{
	  tsubst (DECL_ARGUMENTS (gen), gen_args, tf_warning_or_error, d);
	  tsubst (TYPE_RAISES_EXCEPTIONS (type), gen_args,
		  tf_warning_or_error, d);
	  /* Don't simply tsubst the function type, as that will give
	     duplicate warnings about poor parameter qualifications.
	     The function arguments are the same as the decl_arguments
	     without the top level cv qualifiers.  */
	  type = TREE_TYPE (type);
	}
      tsubst (type, gen_args, tf_warning_or_error, d);

      pop_access_scope (d);
    }

  /* Check to see whether we know that this template will be
     instantiated in some other file, as with "extern template"
     extension.  */
  external_p = (DECL_INTERFACE_KNOWN (d) && DECL_REALLY_EXTERN (d));
  /* In general, we do not instantiate such templates...  */
  if (external_p
      /* ... but we instantiate inline functions so that we can inline
	 them and ... */
      && ! (TREE_CODE (d) == FUNCTION_DECL && DECL_INLINE (d))
      /* ... we instantiate static data members whose values are
	 needed in integral constant expressions.  */
      && ! (TREE_CODE (d) == VAR_DECL
	    && DECL_INITIALIZED_BY_CONSTANT_EXPRESSION_P (d)))
    goto out;
  /* Defer all other templates, unless we have been explicitly
     forbidden from doing so.  */
  if (/* If there is no definition, we cannot instantiate the
	 template.  */
      ! pattern_defined
      /* If it's OK to postpone instantiation, do so.  */
      || defer_ok
      /* If this is a static data member that will be defined
	 elsewhere, we don't want to instantiate the entire data
	 member, but we do want to instantiate the initializer so that
	 we can substitute that elsewhere.  */
      || (external_p && TREE_CODE (d) == VAR_DECL))
    {
      /* The definition of the static data member is now required so
	 we must substitute the initializer.  */
      if (TREE_CODE (d) == VAR_DECL
	  && !DECL_INITIAL (d)
	  && DECL_INITIAL (code_pattern))
	{
	  tree ns;
	  tree init;

	  ns = decl_namespace_context (d);
	  push_nested_namespace (ns);
	  push_nested_class (DECL_CONTEXT (d));
	  init = tsubst_expr (DECL_INITIAL (code_pattern),
			      args,
			      tf_warning_or_error, NULL_TREE,
			      /*integral_constant_expression_p=*/false);
	  cp_finish_decl (d, init, /*init_const_expr_p=*/false,
			  /*asmspec_tree=*/NULL_TREE,
			  LOOKUP_ONLYCONVERTING);
	  pop_nested_class ();
	  pop_nested_namespace (ns);
	}

      /* We restore the source position here because it's used by
	 add_pending_template.  */
      input_location = saved_loc;

      if (at_eof && !pattern_defined
	  && DECL_EXPLICIT_INSTANTIATION (d))
	/* [temp.explicit]

	   The definition of a non-exported function template, a
	   non-exported member function template, or a non-exported
	   member function or static data member of a class template
	   shall be present in every translation unit in which it is
	   explicitly instantiated.  */
	pedwarn
	  ("explicit instantiation of %qD but no definition available", d);

      /* ??? Historically, we have instantiated inline functions, even
	 when marked as "extern template".  */
      if (!(external_p && TREE_CODE (d) == VAR_DECL))
	add_pending_template (d);
      goto out;
    }
  /* Tell the repository that D is available in this translation unit
     -- and see if it is supposed to be instantiated here.  */
  if (TREE_PUBLIC (d) && !DECL_REALLY_EXTERN (d) && !repo_emit_p (d))
    {
      /* In a PCH file, despite the fact that the repository hasn't
	 requested instantiation in the PCH it is still possible that
	 an instantiation will be required in a file that includes the
	 PCH.  */
      if (pch_file)
	add_pending_template (d);
      /* Instantiate inline functions so that the inliner can do its
	 job, even though we'll not be emitting a copy of this
	 function.  */
      if (!(TREE_CODE (d) == FUNCTION_DECL
	    && flag_inline_trees
	    && DECL_DECLARED_INLINE_P (d)))
	goto out;
    }

  need_push = !cfun || !global_bindings_p ();
  if (need_push)
    push_to_top_level ();

  /* Mark D as instantiated so that recursive calls to
     instantiate_decl do not try to instantiate it again.  */
  DECL_TEMPLATE_INSTANTIATED (d) = 1;

  /* Regenerate the declaration in case the template has been modified
     by a subsequent redeclaration.  */
  regenerate_decl_from_template (d, td);

  /* We already set the file and line above.  Reset them now in case
     they changed as a result of calling regenerate_decl_from_template.  */
  input_location = DECL_SOURCE_LOCATION (d);

  if (TREE_CODE (d) == VAR_DECL)
    {
      tree init;

      /* Clear out DECL_RTL; whatever was there before may not be right
	 since we've reset the type of the declaration.  */
      SET_DECL_RTL (d, NULL_RTX);
      DECL_IN_AGGR_P (d) = 0;

      /* The initializer is placed in DECL_INITIAL by
	 regenerate_decl_from_template.  Pull it out so that
	 finish_decl can process it.  */
      init = DECL_INITIAL (d);
      DECL_INITIAL (d) = NULL_TREE;
      DECL_INITIALIZED_P (d) = 0;

      /* Clear DECL_EXTERNAL so that cp_finish_decl will process the
	 initializer.  That function will defer actual emission until
	 we have a chance to determine linkage.  */
      DECL_EXTERNAL (d) = 0;

      /* Enter the scope of D so that access-checking works correctly.  */
      push_nested_class (DECL_CONTEXT (d));
      finish_decl (d, init, NULL_TREE);
      pop_nested_class ();
    }
  else if (TREE_CODE (d) == FUNCTION_DECL)
    {
      htab_t saved_local_specializations;
      tree subst_decl;
      tree tmpl_parm;
      tree spec_parm;

      /* Save away the current list, in case we are instantiating one
	 template from within the body of another.  */
      saved_local_specializations = local_specializations;

      /* Set up the list of local specializations.  */
      local_specializations = htab_create (37,
					   hash_local_specialization,
					   eq_local_specializations,
					   NULL);

      /* Set up context.  */
      start_preparsed_function (d, NULL_TREE, SF_PRE_PARSED);

      /* Create substitution entries for the parameters.  */
      subst_decl = DECL_TEMPLATE_RESULT (template_for_substitution (d));
      tmpl_parm = DECL_ARGUMENTS (subst_decl);
      spec_parm = DECL_ARGUMENTS (d);
      if (DECL_NONSTATIC_MEMBER_FUNCTION_P (d))
	{
	  register_local_specialization (spec_parm, tmpl_parm);
	  spec_parm = skip_artificial_parms_for (d, spec_parm);
	  tmpl_parm = skip_artificial_parms_for (subst_decl, tmpl_parm);
	}
      while (tmpl_parm)
	{
	  register_local_specialization (spec_parm, tmpl_parm);
	  tmpl_parm = TREE_CHAIN (tmpl_parm);
	  spec_parm = TREE_CHAIN (spec_parm);
	}
      gcc_assert (!spec_parm);

      /* Substitute into the body of the function.  */
      tsubst_expr (DECL_SAVED_TREE (code_pattern), args,
		   tf_warning_or_error, tmpl,
		   /*integral_constant_expression_p=*/false);

      /* We don't need the local specializations any more.  */
      htab_delete (local_specializations);
      local_specializations = saved_local_specializations;

      /* Finish the function.  */
      d = finish_function (0);
      expand_or_defer_fn (d);
    }

  /* We're not deferring instantiation any more.  */
  TI_PENDING_TEMPLATE_FLAG (DECL_TEMPLATE_INFO (d)) = 0;

  if (need_push)
    pop_from_top_level ();

out:
  input_location = saved_loc;
  in_system_header = saved_in_system_header;
  pop_deferring_access_checks ();
  pop_tinst_level ();

  timevar_pop (TV_PARSE);

  return d;
}

/* Run through the list of templates that we wish we could
   instantiate, and instantiate any we can.  RETRIES is the
   number of times we retry pending template instantiation.  */

void
instantiate_pending_templates (int retries)
{
  tree *t;
  tree last = NULL_TREE;
  int reconsider;
  location_t saved_loc = input_location;
  int saved_in_system_header = in_system_header;

  /* Instantiating templates may trigger vtable generation.  This in turn
     may require further template instantiations.  We place a limit here
     to avoid infinite loop.  */
  if (pending_templates && retries >= max_tinst_depth)
    {
      tree decl = TREE_VALUE (pending_templates);

      error ("template instantiation depth exceeds maximum of %d"
	     " instantiating %q+D, possibly from virtual table generation"
	     " (use -ftemplate-depth-NN to increase the maximum)",
	     max_tinst_depth, decl);
      if (TREE_CODE (decl) == FUNCTION_DECL)
	/* Pretend that we defined it.  */
	DECL_INITIAL (decl) = error_mark_node;
      return;
    }

  do
    {
      reconsider = 0;

      t = &pending_templates;
      while (*t)
	{
	  tree instantiation = TREE_VALUE (*t);

	  reopen_tinst_level (TREE_PURPOSE (*t));

	  if (TYPE_P (instantiation))
	    {
	      tree fn;

	      if (!COMPLETE_TYPE_P (instantiation))
		{
		  instantiate_class_template (instantiation);
		  if (CLASSTYPE_TEMPLATE_INSTANTIATION (instantiation))
		    for (fn = TYPE_METHODS (instantiation);
			 fn;
			 fn = TREE_CHAIN (fn))
		      if (! DECL_ARTIFICIAL (fn))
			instantiate_decl (fn,
					  /*defer_ok=*/0,
					  /*expl_inst_class_mem_p=*/false);
		  if (COMPLETE_TYPE_P (instantiation))
		    reconsider = 1;
		}

	      if (COMPLETE_TYPE_P (instantiation))
		/* If INSTANTIATION has been instantiated, then we don't
		   need to consider it again in the future.  */
		*t = TREE_CHAIN (*t);
	      else
		{
		  last = *t;
		  t = &TREE_CHAIN (*t);
		}
	    }
	  else
	    {
	      if (!DECL_TEMPLATE_SPECIALIZATION (instantiation)
		  && !DECL_TEMPLATE_INSTANTIATED (instantiation))
		{
		  instantiation
		    = instantiate_decl (instantiation,
					/*defer_ok=*/0,
					/*expl_inst_class_mem_p=*/false);
		  if (DECL_TEMPLATE_INSTANTIATED (instantiation))
		    reconsider = 1;
		}

	      if (DECL_TEMPLATE_SPECIALIZATION (instantiation)
		  || DECL_TEMPLATE_INSTANTIATED (instantiation))
		/* If INSTANTIATION has been instantiated, then we don't
		   need to consider it again in the future.  */
		*t = TREE_CHAIN (*t);
	      else
		{
		  last = *t;
		  t = &TREE_CHAIN (*t);
		}
	    }
	  tinst_depth = 0;
	  current_tinst_level = NULL_TREE;
	}
      last_pending_template = last;
    }
  while (reconsider);

  input_location = saved_loc;
  in_system_header = saved_in_system_header;
}

/* Substitute ARGVEC into T, which is a list of initializers for
   either base class or a non-static data member.  The TREE_PURPOSEs
   are DECLs, and the TREE_VALUEs are the initializer values.  Used by
   instantiate_decl.  */

static tree
tsubst_initializer_list (tree t, tree argvec)
{
  tree inits = NULL_TREE;

  for (; t; t = TREE_CHAIN (t))
    {
      tree decl;
      tree init;

      decl = tsubst_copy (TREE_PURPOSE (t), argvec, tf_warning_or_error,
			  NULL_TREE);
      decl = expand_member_init (decl);
      if (decl && !DECL_P (decl))
	in_base_initializer = 1;

      init = tsubst_expr (TREE_VALUE (t), argvec, tf_warning_or_error,
			  NULL_TREE, 
			  /*integral_constant_expression_p=*/false);
      in_base_initializer = 0;

      if (decl)
	{
	  init = build_tree_list (decl, init);
	  TREE_CHAIN (init) = inits;
	  inits = init;
	}
    }
  return inits;
}

/* Set CURRENT_ACCESS_SPECIFIER based on the protection of DECL.  */

static void
set_current_access_from_decl (tree decl)
{
  if (TREE_PRIVATE (decl))
    current_access_specifier = access_private_node;
  else if (TREE_PROTECTED (decl))
    current_access_specifier = access_protected_node;
  else
    current_access_specifier = access_public_node;
}

/* Instantiate an enumerated type.  TAG is the template type, NEWTAG
   is the instantiation (which should have been created with
   start_enum) and ARGS are the template arguments to use.  */

static void
tsubst_enum (tree tag, tree newtag, tree args)
{
  tree e;

  for (e = TYPE_VALUES (tag); e; e = TREE_CHAIN (e))
    {
      tree value;
      tree decl;

      decl = TREE_VALUE (e);
      /* Note that in a template enum, the TREE_VALUE is the
	 CONST_DECL, not the corresponding INTEGER_CST.  */
      value = tsubst_expr (DECL_INITIAL (decl),
			   args, tf_warning_or_error, NULL_TREE,
			   /*integral_constant_expression_p=*/true);

      /* Give this enumeration constant the correct access.  */
      set_current_access_from_decl (decl);

      /* Actually build the enumerator itself.  */
      build_enumerator (DECL_NAME (decl), value, newtag);
    }

  finish_enum (newtag);
  DECL_SOURCE_LOCATION (TYPE_NAME (newtag))
    = DECL_SOURCE_LOCATION (TYPE_NAME (tag));
}

/* DECL is a FUNCTION_DECL that is a template specialization.  Return
   its type -- but without substituting the innermost set of template
   arguments.  So, innermost set of template parameters will appear in
   the type.  */

tree
get_mostly_instantiated_function_type (tree decl)
{
  tree fn_type;
  tree tmpl;
  tree targs;
  tree tparms;
  int parm_depth;

  tmpl = most_general_template (DECL_TI_TEMPLATE (decl));
  targs = DECL_TI_ARGS (decl);
  tparms = DECL_TEMPLATE_PARMS (tmpl);
  parm_depth = TMPL_PARMS_DEPTH (tparms);

  /* There should be as many levels of arguments as there are levels
     of parameters.  */
  gcc_assert (parm_depth == TMPL_ARGS_DEPTH (targs));

  fn_type = TREE_TYPE (tmpl);

  if (parm_depth == 1)
    /* No substitution is necessary.  */
    ;
  else
    {
      int i, save_access_control;
      tree partial_args;

      /* Replace the innermost level of the TARGS with NULL_TREEs to
	 let tsubst know not to substitute for those parameters.  */
      partial_args = make_tree_vec (TREE_VEC_LENGTH (targs));
      for (i = 1; i < TMPL_ARGS_DEPTH (targs); ++i)
	SET_TMPL_ARGS_LEVEL (partial_args, i,
			     TMPL_ARGS_LEVEL (targs, i));
      SET_TMPL_ARGS_LEVEL (partial_args,
			   TMPL_ARGS_DEPTH (targs),
			   make_tree_vec (DECL_NTPARMS (tmpl)));

      /* Disable access control as this function is used only during
	 name-mangling.  */
      save_access_control = flag_access_control;
      flag_access_control = 0;

      ++processing_template_decl;
      /* Now, do the (partial) substitution to figure out the
	 appropriate function type.  */
      fn_type = tsubst (fn_type, partial_args, tf_error, NULL_TREE);
      --processing_template_decl;

      /* Substitute into the template parameters to obtain the real
	 innermost set of parameters.  This step is important if the
	 innermost set of template parameters contains value
	 parameters whose types depend on outer template parameters.  */
      TREE_VEC_LENGTH (partial_args)--;
      tparms = tsubst_template_parms (tparms, partial_args, tf_error);

      flag_access_control = save_access_control;
    }

  return fn_type;
}

/* Return truthvalue if we're processing a template different from
   the last one involved in diagnostics.  */
int
problematic_instantiation_changed (void)
{
  return last_template_error_tick != tinst_level_tick;
}

/* Remember current template involved in diagnostics.  */
void
record_last_problematic_instantiation (void)
{
  last_template_error_tick = tinst_level_tick;
}

tree
current_instantiation (void)
{
  return current_tinst_level;
}

/* [temp.param] Check that template non-type parm TYPE is of an allowable
   type. Return zero for ok, nonzero for disallowed. Issue error and
   warning messages under control of COMPLAIN.  */

static int
invalid_nontype_parm_type_p (tree type, tsubst_flags_t complain)
{
  if (INTEGRAL_TYPE_P (type))
    return 0;
  else if (POINTER_TYPE_P (type))
    return 0;
  else if (TYPE_PTR_TO_MEMBER_P (type))
    return 0;
  else if (TREE_CODE (type) == TEMPLATE_TYPE_PARM)
    return 0;
  else if (TREE_CODE (type) == TYPENAME_TYPE)
    return 0;

  if (complain & tf_error)
    error ("%q#T is not a valid type for a template constant parameter", type);
  return 1;
}

/* Returns TRUE if TYPE is dependent, in the sense of [temp.dep.type].
   Assumes that TYPE really is a type, and not the ERROR_MARK_NODE.*/

static bool
dependent_type_p_r (tree type)
{
  tree scope;

  /* [temp.dep.type]

     A type is dependent if it is:

     -- a template parameter. Template template parameters are types
	for us (since TYPE_P holds true for them) so we handle
	them here.  */
  if (TREE_CODE (type) == TEMPLATE_TYPE_PARM
      || TREE_CODE (type) == TEMPLATE_TEMPLATE_PARM)
    return true;
  /* -- a qualified-id with a nested-name-specifier which contains a
	class-name that names a dependent type or whose unqualified-id
	names a dependent type.  */
  if (TREE_CODE (type) == TYPENAME_TYPE)
    return true;
  /* -- a cv-qualified type where the cv-unqualified type is
	dependent.  */
  type = TYPE_MAIN_VARIANT (type);
  /* -- a compound type constructed from any dependent type.  */
  if (TYPE_PTR_TO_MEMBER_P (type))
    return (dependent_type_p (TYPE_PTRMEM_CLASS_TYPE (type))
	    || dependent_type_p (TYPE_PTRMEM_POINTED_TO_TYPE
					   (type)));
  else if (TREE_CODE (type) == POINTER_TYPE
	   || TREE_CODE (type) == REFERENCE_TYPE)
    return dependent_type_p (TREE_TYPE (type));
  else if (TREE_CODE (type) == FUNCTION_TYPE
	   || TREE_CODE (type) == METHOD_TYPE)
    {
      tree arg_type;

      if (dependent_type_p (TREE_TYPE (type)))
	return true;
      for (arg_type = TYPE_ARG_TYPES (type);
	   arg_type;
	   arg_type = TREE_CHAIN (arg_type))
	if (dependent_type_p (TREE_VALUE (arg_type)))
	  return true;
      return false;
    }
  /* -- an array type constructed from any dependent type or whose
	size is specified by a constant expression that is
	value-dependent.  */
  if (TREE_CODE (type) == ARRAY_TYPE)
    {
      if (TYPE_DOMAIN (type)
	  && ((value_dependent_expression_p
	       (TYPE_MAX_VALUE (TYPE_DOMAIN (type))))
	      || (type_dependent_expression_p
		  (TYPE_MAX_VALUE (TYPE_DOMAIN (type))))))
	return true;
      return dependent_type_p (TREE_TYPE (type));
    }

  /* -- a template-id in which either the template name is a template
     parameter ...  */
  if (TREE_CODE (type) == BOUND_TEMPLATE_TEMPLATE_PARM)
    return true;
  /* ... or any of the template arguments is a dependent type or
	an expression that is type-dependent or value-dependent.  */
  else if (CLASS_TYPE_P (type) && CLASSTYPE_TEMPLATE_INFO (type)
	   && (any_dependent_template_arguments_p
	       (INNERMOST_TEMPLATE_ARGS (CLASSTYPE_TI_ARGS (type)))))
    return true;

  /* All TYPEOF_TYPEs are dependent; if the argument of the `typeof'
     expression is not type-dependent, then it should already been
     have resolved.  */
  if (TREE_CODE (type) == TYPEOF_TYPE)
    return true;

  /* The standard does not specifically mention types that are local
     to template functions or local classes, but they should be
     considered dependent too.  For example:

       template <int I> void f() {
	 enum E { a = I };
	 S<sizeof (E)> s;
       }

     The size of `E' cannot be known until the value of `I' has been
     determined.  Therefore, `E' must be considered dependent.  */
  scope = TYPE_CONTEXT (type);
  if (scope && TYPE_P (scope))
    return dependent_type_p (scope);
  else if (scope && TREE_CODE (scope) == FUNCTION_DECL)
    return type_dependent_expression_p (scope);

  /* Other types are non-dependent.  */
  return false;
}

/* Returns TRUE if TYPE is dependent, in the sense of
   [temp.dep.type].  */

bool
dependent_type_p (tree type)
{
  /* If there are no template parameters in scope, then there can't be
     any dependent types.  */
  if (!processing_template_decl)
    {
      /* If we are not processing a template, then nobody should be
	 providing us with a dependent type.  */
      gcc_assert (type);
      gcc_assert (TREE_CODE (type) != TEMPLATE_TYPE_PARM);
      return false;
    }

  /* If the type is NULL, we have not computed a type for the entity
     in question; in that case, the type is dependent.  */
  if (!type)
    return true;

  /* Erroneous types can be considered non-dependent.  */
  if (type == error_mark_node)
    return false;

  /* If we have not already computed the appropriate value for TYPE,
     do so now.  */
  if (!TYPE_DEPENDENT_P_VALID (type))
    {
      TYPE_DEPENDENT_P (type) = dependent_type_p_r (type);
      TYPE_DEPENDENT_P_VALID (type) = 1;
    }

  return TYPE_DEPENDENT_P (type);
}

/* Returns TRUE if EXPRESSION is dependent, according to CRITERION.  */

static bool
dependent_scope_ref_p (tree expression, bool criterion (tree))
{
  tree scope;
  tree name;

  gcc_assert (TREE_CODE (expression) == SCOPE_REF);

  if (!TYPE_P (TREE_OPERAND (expression, 0)))
    return true;

  scope = TREE_OPERAND (expression, 0);
  name = TREE_OPERAND (expression, 1);

  /* [temp.dep.expr]

     An id-expression is type-dependent if it contains a
     nested-name-specifier that contains a class-name that names a
     dependent type.  */
  /* The suggested resolution to Core Issue 2 implies that if the
     qualifying type is the current class, then we must peek
     inside it.  */
  if (DECL_P (name)
      && currently_open_class (scope)
      && !criterion (name))
    return false;
  if (dependent_type_p (scope))
    return true;

  return false;
}

/* Returns TRUE if the EXPRESSION is value-dependent, in the sense of
   [temp.dep.constexpr].  EXPRESSION is already known to be a constant
   expression.  */

bool
value_dependent_expression_p (tree expression)
{
  if (!processing_template_decl)
    return false;

  /* A name declared with a dependent type.  */
  if (DECL_P (expression) && type_dependent_expression_p (expression))
    return true;

  switch (TREE_CODE (expression))
    {
    case IDENTIFIER_NODE:
      /* A name that has not been looked up -- must be dependent.  */
      return true;

    case TEMPLATE_PARM_INDEX:
      /* A non-type template parm.  */
      return true;

    case CONST_DECL:
      /* A non-type template parm.  */
      if (DECL_TEMPLATE_PARM_P (expression))
	return true;
      return false;

    case VAR_DECL:
       /* A constant with integral or enumeration type and is initialized
	  with an expression that is value-dependent.  */
      if (DECL_INITIAL (expression)
	  && INTEGRAL_OR_ENUMERATION_TYPE_P (TREE_TYPE (expression))
	  && value_dependent_expression_p (DECL_INITIAL (expression)))
	return true;
      return false;

    case DYNAMIC_CAST_EXPR:
    case STATIC_CAST_EXPR:
    case CONST_CAST_EXPR:
    case REINTERPRET_CAST_EXPR:
    case CAST_EXPR:
      /* These expressions are value-dependent if the type to which
	 the cast occurs is dependent or the expression being casted
	 is value-dependent.  */
      {
	tree type = TREE_TYPE (expression);

	if (dependent_type_p (type))
	  return true;

	/* A functional cast has a list of operands.  */
	expression = TREE_OPERAND (expression, 0);
	if (!expression)
	  {
	    /* If there are no operands, it must be an expression such
	       as "int()". This should not happen for aggregate types
	       because it would form non-constant expressions.  */
	    gcc_assert (INTEGRAL_OR_ENUMERATION_TYPE_P (type));

	    return false;
	  }

	if (TREE_CODE (expression) == TREE_LIST)
	  return any_value_dependent_elements_p (expression);

	return value_dependent_expression_p (expression);
      }

    case SIZEOF_EXPR:
    case ALIGNOF_EXPR:
      /* A `sizeof' expression is value-dependent if the operand is
	 type-dependent.  */
      expression = TREE_OPERAND (expression, 0);
      if (TYPE_P (expression))
	return dependent_type_p (expression);
      return type_dependent_expression_p (expression);

    case SCOPE_REF:
      return dependent_scope_ref_p (expression, value_dependent_expression_p);

    case COMPONENT_REF:
      return (value_dependent_expression_p (TREE_OPERAND (expression, 0))
	      || value_dependent_expression_p (TREE_OPERAND (expression, 1)));

    case CALL_EXPR:
      /* A CALL_EXPR may appear in a constant expression if it is a
	 call to a builtin function, e.g., __builtin_constant_p.  All
	 such calls are value-dependent.  */
      return true;

    case MODOP_EXPR:
      return ((value_dependent_expression_p (TREE_OPERAND (expression, 0)))
	      || (value_dependent_expression_p (TREE_OPERAND (expression, 2))));

    default:
      /* A constant expression is value-dependent if any subexpression is
	 value-dependent.  */
      switch (TREE_CODE_CLASS (TREE_CODE (expression)))
	{
	case tcc_reference:
	case tcc_unary:
	  return (value_dependent_expression_p
		  (TREE_OPERAND (expression, 0)));

	case tcc_comparison:
	case tcc_binary:
	  return ((value_dependent_expression_p
		   (TREE_OPERAND (expression, 0)))
		  || (value_dependent_expression_p
		      (TREE_OPERAND (expression, 1))));

	case tcc_expression:
	  {
	    int i;
	    for (i = 0; i < TREE_CODE_LENGTH (TREE_CODE (expression)); ++i)
	      /* In some cases, some of the operands may be missing.
		 (For example, in the case of PREDECREMENT_EXPR, the
		 amount to increment by may be missing.)  That doesn't
		 make the expression dependent.  */
	      if (TREE_OPERAND (expression, i)
		  && (value_dependent_expression_p
		      (TREE_OPERAND (expression, i))))
		return true;
	    return false;
	  }

	default:
	  break;
	}
    }

  /* The expression is not value-dependent.  */
  return false;
}

/* Returns TRUE if the EXPRESSION is type-dependent, in the sense of
   [temp.dep.expr].  */

bool
type_dependent_expression_p (tree expression)
{
  if (!processing_template_decl)
    return false;

  if (expression == error_mark_node)
    return false;

  /* An unresolved name is always dependent.  */
  if (TREE_CODE (expression) == IDENTIFIER_NODE
      || TREE_CODE (expression) == USING_DECL)
    return true;

  /* Some expression forms are never type-dependent.  */
  if (TREE_CODE (expression) == PSEUDO_DTOR_EXPR
      || TREE_CODE (expression) == SIZEOF_EXPR
      || TREE_CODE (expression) == ALIGNOF_EXPR
      || TREE_CODE (expression) == TYPEID_EXPR
      || TREE_CODE (expression) == DELETE_EXPR
      || TREE_CODE (expression) == VEC_DELETE_EXPR
      || TREE_CODE (expression) == THROW_EXPR)
    return false;

  /* The types of these expressions depends only on the type to which
     the cast occurs.  */
  if (TREE_CODE (expression) == DYNAMIC_CAST_EXPR
      || TREE_CODE (expression) == STATIC_CAST_EXPR
      || TREE_CODE (expression) == CONST_CAST_EXPR
      || TREE_CODE (expression) == REINTERPRET_CAST_EXPR
      || TREE_CODE (expression) == CAST_EXPR)
    return dependent_type_p (TREE_TYPE (expression));

  /* The types of these expressions depends only on the type created
     by the expression.  */
  if (TREE_CODE (expression) == NEW_EXPR
      || TREE_CODE (expression) == VEC_NEW_EXPR)
    {
      /* For NEW_EXPR tree nodes created inside a template, either
	 the object type itself or a TREE_LIST may appear as the
	 operand 1.  */
      tree type = TREE_OPERAND (expression, 1);
      if (TREE_CODE (type) == TREE_LIST)
	/* This is an array type.  We need to check array dimensions
	   as well.  */
	return dependent_type_p (TREE_VALUE (TREE_PURPOSE (type)))
	       || value_dependent_expression_p
		    (TREE_OPERAND (TREE_VALUE (type), 1));
      else
	return dependent_type_p (type);
    }

  if (TREE_CODE (expression) == SCOPE_REF
      && dependent_scope_ref_p (expression,
				type_dependent_expression_p))
    return true;

  if (TREE_CODE (expression) == FUNCTION_DECL
      && DECL_LANG_SPECIFIC (expression)
      && DECL_TEMPLATE_INFO (expression)
      && (any_dependent_template_arguments_p
	  (INNERMOST_TEMPLATE_ARGS (DECL_TI_ARGS (expression)))))
    return true;

  if (TREE_CODE (expression) == TEMPLATE_DECL
      && !DECL_TEMPLATE_TEMPLATE_PARM_P (expression))
    return false;

  if (TREE_TYPE (expression) == unknown_type_node)
    {
      if (TREE_CODE (expression) == ADDR_EXPR)
	return type_dependent_expression_p (TREE_OPERAND (expression, 0));
      if (TREE_CODE (expression) == COMPONENT_REF
	  || TREE_CODE (expression) == OFFSET_REF)
	{
	  if (type_dependent_expression_p (TREE_OPERAND (expression, 0)))
	    return true;
	  expression = TREE_OPERAND (expression, 1);
	  if (TREE_CODE (expression) == IDENTIFIER_NODE)
	    return false;
	}
      /* SCOPE_REF with non-null TREE_TYPE is always non-dependent.  */
      if (TREE_CODE (expression) == SCOPE_REF)
	return false;

      if (TREE_CODE (expression) == BASELINK)
	expression = BASELINK_FUNCTIONS (expression);

      if (TREE_CODE (expression) == TEMPLATE_ID_EXPR)
	{
	  if (any_dependent_template_arguments_p
	      (TREE_OPERAND (expression, 1)))
	    return true;
	  expression = TREE_OPERAND (expression, 0);
	}
      gcc_assert (TREE_CODE (expression) == OVERLOAD
		  || TREE_CODE (expression) == FUNCTION_DECL);

      while (expression)
	{
	  if (type_dependent_expression_p (OVL_CURRENT (expression)))
	    return true;
	  expression = OVL_NEXT (expression);
	}
      return false;
    }

  gcc_assert (TREE_CODE (expression) != TYPE_DECL);

  return (dependent_type_p (TREE_TYPE (expression)));
}

/* Returns TRUE if ARGS (a TREE_LIST of arguments to a function call)
   contains a type-dependent expression.  */

bool
any_type_dependent_arguments_p (tree args)
{
  while (args)
    {
      tree arg = TREE_VALUE (args);

      if (type_dependent_expression_p (arg))
	return true;
      args = TREE_CHAIN (args);
    }
  return false;
}

/* Returns TRUE if LIST (a TREE_LIST whose TREE_VALUEs are
   expressions) contains any value-dependent expressions.  */

bool
any_value_dependent_elements_p (tree list)
{
  for (; list; list = TREE_CHAIN (list))
    if (value_dependent_expression_p (TREE_VALUE (list)))
      return true;

  return false;
}

/* Returns TRUE if the ARG (a template argument) is dependent.  */

static bool
dependent_template_arg_p (tree arg)
{
  if (!processing_template_decl)
    return false;

  if (TREE_CODE (arg) == TEMPLATE_DECL
      || TREE_CODE (arg) == TEMPLATE_TEMPLATE_PARM)
    return dependent_template_p (arg);
  else if (TYPE_P (arg))
    return dependent_type_p (arg);
  else
    return (type_dependent_expression_p (arg)
	    || value_dependent_expression_p (arg));
}

/* Returns true if ARGS (a collection of template arguments) contains
   any dependent arguments.  */

bool
any_dependent_template_arguments_p (tree args)
{
  int i;
  int j;

  if (!args)
    return false;
  if (args == error_mark_node)
    return true;

  for (i = 0; i < TMPL_ARGS_DEPTH (args); ++i)
    {
      tree level = TMPL_ARGS_LEVEL (args, i + 1);
      for (j = 0; j < TREE_VEC_LENGTH (level); ++j)
	if (dependent_template_arg_p (TREE_VEC_ELT (level, j)))
	  return true;
    }

  return false;
}

/* Returns TRUE if the template TMPL is dependent.  */

bool
dependent_template_p (tree tmpl)
{
  if (TREE_CODE (tmpl) == OVERLOAD)
    {
      while (tmpl)
	{
	  if (dependent_template_p (OVL_FUNCTION (tmpl)))
	    return true;
	  tmpl = OVL_CHAIN (tmpl);
	}
      return false;
    }

  /* Template template parameters are dependent.  */
  if (DECL_TEMPLATE_TEMPLATE_PARM_P (tmpl)
      || TREE_CODE (tmpl) == TEMPLATE_TEMPLATE_PARM)
    return true;
  /* So are names that have not been looked up.  */
  if (TREE_CODE (tmpl) == SCOPE_REF
      || TREE_CODE (tmpl) == IDENTIFIER_NODE)
    return true;
  /* So are member templates of dependent classes.  */
  if (TYPE_P (CP_DECL_CONTEXT (tmpl)))
    return dependent_type_p (DECL_CONTEXT (tmpl));
  return false;
}

/* Returns TRUE if the specialization TMPL<ARGS> is dependent.  */

bool
dependent_template_id_p (tree tmpl, tree args)
{
  return (dependent_template_p (tmpl)
	  || any_dependent_template_arguments_p (args));
}

/* TYPE is a TYPENAME_TYPE.  Returns the ordinary TYPE to which the
   TYPENAME_TYPE corresponds.  Returns ERROR_MARK_NODE if no such TYPE
   can be found.  Note that this function peers inside uninstantiated
   templates and therefore should be used only in extremely limited
   situations.  ONLY_CURRENT_P restricts this peering to the currently
   open classes hierarchy (which is required when comparing types).  */

tree
resolve_typename_type (tree type, bool only_current_p)
{
  tree scope;
  tree name;
  tree decl;
  int quals;
  tree pushed_scope;

  gcc_assert (TREE_CODE (type) == TYPENAME_TYPE);

  scope = TYPE_CONTEXT (type);
  name = TYPE_IDENTIFIER (type);

  /* If the SCOPE is itself a TYPENAME_TYPE, then we need to resolve
     it first before we can figure out what NAME refers to.  */
  if (TREE_CODE (scope) == TYPENAME_TYPE)
    scope = resolve_typename_type (scope, only_current_p);
  /* If we don't know what SCOPE refers to, then we cannot resolve the
     TYPENAME_TYPE.  */
  if (scope == error_mark_node || TREE_CODE (scope) == TYPENAME_TYPE)
    return error_mark_node;
  /* If the SCOPE is a template type parameter, we have no way of
     resolving the name.  */
  if (TREE_CODE (scope) == TEMPLATE_TYPE_PARM)
    return type;
  /* If the SCOPE is not the current instantiation, there's no reason
     to look inside it.  */
  if (only_current_p && !currently_open_class (scope))
    return error_mark_node;
  /* If SCOPE is a partial instantiation, it will not have a valid
     TYPE_FIELDS list, so use the original template.  */
  scope = CLASSTYPE_PRIMARY_TEMPLATE_TYPE (scope);
  /* Enter the SCOPE so that name lookup will be resolved as if we
     were in the class definition.  In particular, SCOPE will no
     longer be considered a dependent type.  */
  pushed_scope = push_scope (scope);
  /* Look up the declaration.  */
  decl = lookup_member (scope, name, /*protect=*/0, /*want_type=*/true);
  /* Obtain the set of qualifiers applied to the TYPE.  */
  quals = cp_type_quals (type);
  /* For a TYPENAME_TYPE like "typename X::template Y<T>", we want to
     find a TEMPLATE_DECL.  Otherwise, we want to find a TYPE_DECL.  */
  if (!decl)
    type = error_mark_node;
  else if (TREE_CODE (TYPENAME_TYPE_FULLNAME (type)) == IDENTIFIER_NODE
	   && TREE_CODE (decl) == TYPE_DECL)
    type = TREE_TYPE (decl);
  else if (TREE_CODE (TYPENAME_TYPE_FULLNAME (type)) == TEMPLATE_ID_EXPR
	   && DECL_CLASS_TEMPLATE_P (decl))
    {
      tree tmpl;
      tree args;
      /* Obtain the template and the arguments.  */
      tmpl = TREE_OPERAND (TYPENAME_TYPE_FULLNAME (type), 0);
      args = TREE_OPERAND (TYPENAME_TYPE_FULLNAME (type), 1);
      /* Instantiate the template.  */
      type = lookup_template_class (tmpl, args, NULL_TREE, NULL_TREE,
				    /*entering_scope=*/0, tf_error | tf_user);
    }
  else
    type = error_mark_node;
  /* Qualify the resulting type.  */
  if (type != error_mark_node && quals)
    type = cp_build_qualified_type (type, quals);
  /* Leave the SCOPE.  */
  if (pushed_scope)
    pop_scope (pushed_scope);

  return type;
}

/* EXPR is an expression which is not type-dependent.  Return a proxy
   for EXPR that can be used to compute the types of larger
   expressions containing EXPR.  */

tree
build_non_dependent_expr (tree expr)
{
  tree inner_expr;

  /* Preserve null pointer constants so that the type of things like
     "p == 0" where "p" is a pointer can be determined.  */
  if (null_ptr_cst_p (expr))
    return expr;
  /* Preserve OVERLOADs; the functions must be available to resolve
     types.  */
  inner_expr = expr;
  if (TREE_CODE (inner_expr) == ADDR_EXPR)
    inner_expr = TREE_OPERAND (inner_expr, 0);
  if (TREE_CODE (inner_expr) == COMPONENT_REF)
    inner_expr = TREE_OPERAND (inner_expr, 1);
  if (is_overloaded_fn (inner_expr)
      || TREE_CODE (inner_expr) == OFFSET_REF)
    return expr;
  /* There is no need to return a proxy for a variable.  */
  if (TREE_CODE (expr) == VAR_DECL)
    return expr;
  /* Preserve string constants; conversions from string constants to
     "char *" are allowed, even though normally a "const char *"
     cannot be used to initialize a "char *".  */
  if (TREE_CODE (expr) == STRING_CST)
    return expr;
  /* Preserve arithmetic constants, as an optimization -- there is no
     reason to create a new node.  */
  if (TREE_CODE (expr) == INTEGER_CST || TREE_CODE (expr) == REAL_CST)
    return expr;
  /* Preserve THROW_EXPRs -- all throw-expressions have type "void".
     There is at least one place where we want to know that a
     particular expression is a throw-expression: when checking a ?:
     expression, there are special rules if the second or third
     argument is a throw-expression.  */
  if (TREE_CODE (expr) == THROW_EXPR)
    return expr;

  if (TREE_CODE (expr) == COND_EXPR)
    return build3 (COND_EXPR,
		   TREE_TYPE (expr),
		   TREE_OPERAND (expr, 0),
		   (TREE_OPERAND (expr, 1)
		    ? build_non_dependent_expr (TREE_OPERAND (expr, 1))
		    : build_non_dependent_expr (TREE_OPERAND (expr, 0))),
		   build_non_dependent_expr (TREE_OPERAND (expr, 2)));
  if (TREE_CODE (expr) == COMPOUND_EXPR
      && !COMPOUND_EXPR_OVERLOADED (expr))
    return build2 (COMPOUND_EXPR,
		   TREE_TYPE (expr),
		   TREE_OPERAND (expr, 0),
		   build_non_dependent_expr (TREE_OPERAND (expr, 1)));

  /* If the type is unknown, it can't really be non-dependent */
  gcc_assert (TREE_TYPE (expr) != unknown_type_node);

  /* Otherwise, build a NON_DEPENDENT_EXPR.

     REFERENCE_TYPEs are not stripped for expressions in templates
     because doing so would play havoc with mangling.  Consider, for
     example:

       template <typename T> void f<T& g>() { g(); }

     In the body of "f", the expression for "g" will have
     REFERENCE_TYPE, even though the standard says that it should
     not.  The reason is that we must preserve the syntactic form of
     the expression so that mangling (say) "f<g>" inside the body of
     "f" works out correctly.  Therefore, the REFERENCE_TYPE is
     stripped here.  */
  return build1 (NON_DEPENDENT_EXPR, non_reference (TREE_TYPE (expr)), expr);
}

/* ARGS is a TREE_LIST of expressions as arguments to a function call.
   Return a new TREE_LIST with the various arguments replaced with
   equivalent non-dependent expressions.  */

tree
build_non_dependent_args (tree args)
{
  tree a;
  tree new_args;

  new_args = NULL_TREE;
  for (a = args; a; a = TREE_CHAIN (a))
    new_args = tree_cons (NULL_TREE,
			  build_non_dependent_expr (TREE_VALUE (a)),
			  new_args);
  return nreverse (new_args);
}

#include "gt-cp-pt.h"
