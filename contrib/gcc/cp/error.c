/* Call-backs for C++ error reporting.
   This code is non-reentrant.
   Copyright (C) 1993, 1994, 1995, 1996, 1997, 1998, 1999, 2000, 2002,
   2003, 2004, 2005 Free Software Foundation, Inc.
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
#include "real.h"
#include "toplev.h"
#include "flags.h"
#include "diagnostic.h"
#include "langhooks-def.h"
#include "cxx-pretty-print.h"

#define pp_separate_with_comma(PP) pp_cxx_separate_with (PP, ',')

/* The global buffer where we dump everything.  It is there only for
   transitional purpose.  It is expected, in the near future, to be
   completely removed.  */
static cxx_pretty_printer scratch_pretty_printer;
#define cxx_pp (&scratch_pretty_printer)

# define NEXT_CODE(T) (TREE_CODE (TREE_TYPE (T)))

static const char *args_to_string (tree, int);
static const char *assop_to_string (enum tree_code);
static const char *code_to_string (enum tree_code);
static const char *cv_to_string (tree, int);
static const char *decl_to_string (tree, int);
static const char *expr_to_string (tree);
static const char *fndecl_to_string (tree, int);
static const char *op_to_string	(enum tree_code);
static const char *parm_to_string (int);
static const char *type_to_string (tree, int);

static void dump_type (tree, int);
static void dump_typename (tree, int);
static void dump_simple_decl (tree, tree, int);
static void dump_decl (tree, int);
static void dump_template_decl (tree, int);
static void dump_function_decl (tree, int);
static void dump_expr (tree, int);
static void dump_unary_op (const char *, tree, int);
static void dump_binary_op (const char *, tree, int);
static void dump_aggr_type (tree, int);
static void dump_type_prefix (tree, int);
static void dump_type_suffix (tree, int);
static void dump_function_name (tree, int);
static void dump_expr_list (tree, int);
static void dump_global_iord (tree);
static void dump_parameters (tree, int);
static void dump_exception_spec (tree, int);
static void dump_template_argument (tree, int);
static void dump_template_argument_list (tree, int);
static void dump_template_parameter (tree, int);
static void dump_template_bindings (tree, tree);
static void dump_scope (tree, int);
static void dump_template_parms (tree, int, int);

static const char *function_category (tree);
static void maybe_print_instantiation_context (diagnostic_context *);
static void print_instantiation_full_context (diagnostic_context *);
static void print_instantiation_partial_context (diagnostic_context *,
						 tree, location_t);
static void cp_diagnostic_starter (diagnostic_context *, diagnostic_info *);
static void cp_diagnostic_finalizer (diagnostic_context *, diagnostic_info *);
static void cp_print_error_function (diagnostic_context *, diagnostic_info *);

static bool cp_printer (pretty_printer *, text_info *, const char *,
			int, bool, bool, bool);
static location_t location_of (tree);

void
init_error (void)
{
  diagnostic_starter (global_dc) = cp_diagnostic_starter;
  diagnostic_finalizer (global_dc) = cp_diagnostic_finalizer;
  diagnostic_format_decoder (global_dc) = cp_printer;

  pp_construct (pp_base (cxx_pp), NULL, 0);
  pp_cxx_pretty_printer_init (cxx_pp);
}

/* Dump a scope, if deemed necessary.  */

static void
dump_scope (tree scope, int flags)
{
  int f = ~TFF_RETURN_TYPE & (flags & (TFF_SCOPE | TFF_CHASE_TYPEDEF));

  if (scope == NULL_TREE)
    return;

  if (TREE_CODE (scope) == NAMESPACE_DECL)
    {
      if (scope != global_namespace)
	{
	  dump_decl (scope, f);
	  pp_cxx_colon_colon (cxx_pp);
	}
    }
  else if (AGGREGATE_TYPE_P (scope))
    {
      dump_type (scope, f);
      pp_cxx_colon_colon (cxx_pp);
    }
  else if ((flags & TFF_SCOPE) && TREE_CODE (scope) == FUNCTION_DECL)
    {
      dump_function_decl (scope, f);
      pp_cxx_colon_colon (cxx_pp);
    }
}

/* Dump the template ARGument under control of FLAGS.  */

static void
dump_template_argument (tree arg, int flags)
{
  if (TYPE_P (arg) || TREE_CODE (arg) == TEMPLATE_DECL)
    dump_type (arg, flags & ~TFF_CLASS_KEY_OR_ENUM);
  else
    dump_expr (arg, (flags | TFF_EXPR_IN_PARENS) & ~TFF_CLASS_KEY_OR_ENUM);
}

/* Dump a template-argument-list ARGS (always a TREE_VEC) under control
   of FLAGS.  */

static void
dump_template_argument_list (tree args, int flags)
{
  int n = TREE_VEC_LENGTH (args);
  int need_comma = 0;
  int i;

  for (i = 0; i< n; ++i)
    {
      if (need_comma)
	pp_separate_with_comma (cxx_pp);
      dump_template_argument (TREE_VEC_ELT (args, i), flags);
      need_comma = 1;
    }
}

/* Dump a template parameter PARM (a TREE_LIST) under control of FLAGS.  */

static void
dump_template_parameter (tree parm, int flags)
{
  tree p;
  tree a;

  if (parm == error_mark_node)
   return;

  p = TREE_VALUE (parm);
  a = TREE_PURPOSE (parm);

  if (TREE_CODE (p) == TYPE_DECL)
    {
      if (flags & TFF_DECL_SPECIFIERS)
	{
	  pp_cxx_identifier (cxx_pp, "class");
	  if (DECL_NAME (p))
	    pp_cxx_tree_identifier (cxx_pp, DECL_NAME (p));
	}
      else if (DECL_NAME (p))
	pp_cxx_tree_identifier (cxx_pp, DECL_NAME (p));
      else
	pp_cxx_canonical_template_parameter (cxx_pp, TREE_TYPE (p));
    }
  else
    dump_decl (p, flags | TFF_DECL_SPECIFIERS);

  if ((flags & TFF_FUNCTION_DEFAULT_ARGUMENTS) && a != NULL_TREE)
    {
      pp_cxx_whitespace (cxx_pp);
      pp_equal (cxx_pp);
      pp_cxx_whitespace (cxx_pp);
      if (TREE_CODE (p) == TYPE_DECL || TREE_CODE (p) == TEMPLATE_DECL)
	dump_type (a, flags & ~TFF_CHASE_TYPEDEF);
      else
	dump_expr (a, flags | TFF_EXPR_IN_PARENS);
    }
}

/* Dump, under control of FLAGS, a template-parameter-list binding.
   PARMS is a TREE_LIST of TREE_VEC of TREE_LIST and ARGS is a
   TREE_VEC.  */

static void
dump_template_bindings (tree parms, tree args)
{
  int need_comma = 0;

  while (parms)
    {
      tree p = TREE_VALUE (parms);
      int lvl = TMPL_PARMS_DEPTH (parms);
      int arg_idx = 0;
      int i;

      for (i = 0; i < TREE_VEC_LENGTH (p); ++i)
	{
	  tree arg = NULL_TREE;

	  /* Don't crash if we had an invalid argument list.  */
	  if (TMPL_ARGS_DEPTH (args) >= lvl)
	    {
	      tree lvl_args = TMPL_ARGS_LEVEL (args, lvl);
	      if (NUM_TMPL_ARGS (lvl_args) > arg_idx)
		arg = TREE_VEC_ELT (lvl_args, arg_idx);
	    }

	  if (need_comma)
	    pp_separate_with_comma (cxx_pp);
	  dump_template_parameter (TREE_VEC_ELT (p, i), TFF_PLAIN_IDENTIFIER);
	  pp_cxx_whitespace (cxx_pp);
	  pp_equal (cxx_pp);
	  pp_cxx_whitespace (cxx_pp);
	  if (arg)
	    dump_template_argument (arg, TFF_PLAIN_IDENTIFIER);
	  else
	    pp_identifier (cxx_pp, "<missing>");

	  ++arg_idx;
	  need_comma = 1;
	}

      parms = TREE_CHAIN (parms);
    }
}

/* Dump a human-readable equivalent of TYPE.  FLAGS controls the
   format.  */

static void
dump_type (tree t, int flags)
{
  if (t == NULL_TREE)
    return;

  if (TYPE_PTRMEMFUNC_P (t))
    goto offset_type;

  switch (TREE_CODE (t))
    {
    case UNKNOWN_TYPE:
      pp_identifier (cxx_pp, "<unresolved overloaded function type>");
      break;

    case TREE_LIST:
      /* A list of function parms.  */
      dump_parameters (t, flags);
      break;

    case IDENTIFIER_NODE:
      pp_cxx_tree_identifier (cxx_pp, t);
      break;

    case TREE_BINFO:
      dump_type (BINFO_TYPE (t), flags);
      break;

    case RECORD_TYPE:
    case UNION_TYPE:
    case ENUMERAL_TYPE:
      dump_aggr_type (t, flags);
      break;

    case TYPE_DECL:
      if (flags & TFF_CHASE_TYPEDEF)
	{
	  dump_type (DECL_ORIGINAL_TYPE (t)
		     ? DECL_ORIGINAL_TYPE (t) : TREE_TYPE (t), flags);
	  break;
	}
      /* Else fall through.  */

    case TEMPLATE_DECL:
    case NAMESPACE_DECL:
      dump_decl (t, flags & ~TFF_DECL_SPECIFIERS);
      break;

    case INTEGER_TYPE:
    case REAL_TYPE:
    case VOID_TYPE:
    case BOOLEAN_TYPE:
    case COMPLEX_TYPE:
    case VECTOR_TYPE:
      pp_type_specifier_seq (cxx_pp, t);
      break;

    case TEMPLATE_TEMPLATE_PARM:
      /* For parameters inside template signature.  */
      if (TYPE_IDENTIFIER (t))
	pp_cxx_tree_identifier (cxx_pp, TYPE_IDENTIFIER (t));
      else
	pp_cxx_canonical_template_parameter (cxx_pp, t);
      break;

    case BOUND_TEMPLATE_TEMPLATE_PARM:
      {
	tree args = TYPE_TI_ARGS (t);
	pp_cxx_cv_qualifier_seq (cxx_pp, t);
	pp_cxx_tree_identifier (cxx_pp, TYPE_IDENTIFIER (t));
	pp_cxx_begin_template_argument_list (cxx_pp);
	dump_template_argument_list (args, flags);
	pp_cxx_end_template_argument_list (cxx_pp);
      }
      break;

    case TEMPLATE_TYPE_PARM:
      pp_cxx_cv_qualifier_seq (cxx_pp, t);
      if (TYPE_IDENTIFIER (t))
	pp_cxx_tree_identifier (cxx_pp, TYPE_IDENTIFIER (t));
      else
	pp_cxx_canonical_template_parameter
	  (cxx_pp, TEMPLATE_TYPE_PARM_INDEX (t));
      break;

      /* This is not always necessary for pointers and such, but doing this
	 reduces code size.  */
    case ARRAY_TYPE:
    case POINTER_TYPE:
      /* APPLE LOCAL blocks 6040305 */
    case BLOCK_POINTER_TYPE:
    case REFERENCE_TYPE:
    case OFFSET_TYPE:
    offset_type:
    case FUNCTION_TYPE:
    case METHOD_TYPE:
    {
      dump_type_prefix (t, flags);
      dump_type_suffix (t, flags);
      break;
    }
    case TYPENAME_TYPE:
      pp_cxx_cv_qualifier_seq (cxx_pp, t);
      pp_cxx_identifier (cxx_pp,
			 TYPENAME_IS_ENUM_P (t) ? "enum"
			 : TYPENAME_IS_CLASS_P (t) ? "class"
			 : "typename");
      dump_typename (t, flags);
      break;

    case UNBOUND_CLASS_TEMPLATE:
      dump_type (TYPE_CONTEXT (t), flags);
      pp_cxx_colon_colon (cxx_pp);
      pp_cxx_identifier (cxx_pp, "template");
      dump_type (DECL_NAME (TYPE_NAME (t)), flags);
      break;

    case TYPEOF_TYPE:
      pp_cxx_identifier (cxx_pp, "__typeof__");
      pp_cxx_whitespace (cxx_pp);
      pp_cxx_left_paren (cxx_pp);
      dump_expr (TYPEOF_TYPE_EXPR (t), flags & ~TFF_EXPR_IN_PARENS);
      pp_cxx_right_paren (cxx_pp);
      break;

    default:
      pp_unsupported_tree (cxx_pp, t);
      /* Fall through to error.  */

    case ERROR_MARK:
      pp_identifier (cxx_pp, "<type error>");
      break;
    }
}

/* Dump a TYPENAME_TYPE. We need to notice when the context is itself
   a TYPENAME_TYPE.  */

static void
dump_typename (tree t, int flags)
{
  tree ctx = TYPE_CONTEXT (t);

  if (TREE_CODE (ctx) == TYPENAME_TYPE)
    dump_typename (ctx, flags);
  else
    dump_type (ctx, flags & ~TFF_CLASS_KEY_OR_ENUM);
  pp_cxx_colon_colon (cxx_pp);
  dump_decl (TYPENAME_TYPE_FULLNAME (t), flags);
}

/* Return the name of the supplied aggregate, or enumeral type.  */

const char *
class_key_or_enum_as_string (tree t)
{
  if (TREE_CODE (t) == ENUMERAL_TYPE)
    return "enum";
  else if (TREE_CODE (t) == UNION_TYPE)
    return "union";
  else if (TYPE_LANG_SPECIFIC (t) && CLASSTYPE_DECLARED_CLASS (t))
    return "class";
  else
    return "struct";
}

/* Print out a class declaration T under the control of FLAGS,
   in the form `class foo'.  */

static void
dump_aggr_type (tree t, int flags)
{
  tree name;
  const char *variety = class_key_or_enum_as_string (t);
  int typdef = 0;
  int tmplate = 0;

  pp_cxx_cv_qualifier_seq (cxx_pp, t);

  if (flags & TFF_CLASS_KEY_OR_ENUM)
    pp_cxx_identifier (cxx_pp, variety);

  if (flags & TFF_CHASE_TYPEDEF)
    t = TYPE_MAIN_VARIANT (t);

  name = TYPE_NAME (t);

  if (name)
    {
      typdef = !DECL_ARTIFICIAL (name);
      tmplate = !typdef && TREE_CODE (t) != ENUMERAL_TYPE
		&& TYPE_LANG_SPECIFIC (t) && CLASSTYPE_TEMPLATE_INFO (t)
		&& (TREE_CODE (CLASSTYPE_TI_TEMPLATE (t)) != TEMPLATE_DECL
		    || PRIMARY_TEMPLATE_P (CLASSTYPE_TI_TEMPLATE (t)));
      dump_scope (CP_DECL_CONTEXT (name), flags | TFF_SCOPE);
      if (tmplate)
	{
	  /* Because the template names are mangled, we have to locate
	     the most general template, and use that name.  */
	  tree tpl = CLASSTYPE_TI_TEMPLATE (t);

	  while (DECL_TEMPLATE_INFO (tpl))
	    tpl = DECL_TI_TEMPLATE (tpl);
	  name = tpl;
	}
      name = DECL_NAME (name);
    }

  if (name == 0 || ANON_AGGRNAME_P (name))
    {
      if (flags & TFF_CLASS_KEY_OR_ENUM)
	pp_identifier (cxx_pp, "<anonymous>");
      else
	pp_printf (pp_base (cxx_pp), "<anonymous %s>", variety);
    }
  else
    pp_cxx_tree_identifier (cxx_pp, name);
  if (tmplate)
    dump_template_parms (TYPE_TEMPLATE_INFO (t),
			 !CLASSTYPE_USE_TEMPLATE (t),
			 flags & ~TFF_TEMPLATE_HEADER);
}

/* Dump into the obstack the initial part of the output for a given type.
   This is necessary when dealing with things like functions returning
   functions.  Examples:

   return type of `int (* fee ())()': pointer -> function -> int.  Both
   pointer (and reference and offset) and function (and member) types must
   deal with prefix and suffix.

   Arrays must also do this for DECL nodes, like int a[], and for things like
   int *[]&.  */

static void
dump_type_prefix (tree t, int flags)
{
  if (TYPE_PTRMEMFUNC_P (t))
    {
      t = TYPE_PTRMEMFUNC_FN_TYPE (t);
      goto offset_type;
    }

  switch (TREE_CODE (t))
    {
    case POINTER_TYPE:
      /* APPLE LOCAL blocks 6040305 */
    case BLOCK_POINTER_TYPE:
    case REFERENCE_TYPE:
      {
	tree sub = TREE_TYPE (t);

	dump_type_prefix (sub, flags);
	if (TREE_CODE (sub) == ARRAY_TYPE)
	  {
	    pp_cxx_whitespace (cxx_pp);
	    pp_cxx_left_paren (cxx_pp);
	  }
	/* APPLE LOCAL begin blocks 6040305 */
	pp_character (cxx_pp, "&*^"[(TREE_CODE (t) == POINTER_TYPE)
				    + (TREE_CODE (t) == BLOCK_POINTER_TYPE)*2]);
	/* APPLE LOCAL end blocks 6040305 */
	pp_base (cxx_pp)->padding = pp_before;
	pp_cxx_cv_qualifier_seq (cxx_pp, t);
      }
      break;

    case OFFSET_TYPE:
    offset_type:
      dump_type_prefix (TREE_TYPE (t), flags);
      if (TREE_CODE (t) == OFFSET_TYPE)	/* pmfs deal with this in d_t_p */
	{
	  pp_maybe_space (cxx_pp);
	  if (TREE_CODE (TREE_TYPE (t)) == ARRAY_TYPE)
	     pp_cxx_left_paren (cxx_pp);
	  dump_type (TYPE_OFFSET_BASETYPE (t), flags);
	  pp_cxx_colon_colon (cxx_pp);
	}
      pp_cxx_star (cxx_pp);
      pp_cxx_cv_qualifier_seq (cxx_pp, t);
      pp_base (cxx_pp)->padding = pp_before;
      break;

      /* Can only be reached through function pointer -- this would not be
	 correct if FUNCTION_DECLs used it.  */
    case FUNCTION_TYPE:
      dump_type_prefix (TREE_TYPE (t), flags);
      pp_maybe_space (cxx_pp);
      pp_cxx_left_paren (cxx_pp);
      break;

    case METHOD_TYPE:
      dump_type_prefix (TREE_TYPE (t), flags);
      pp_maybe_space (cxx_pp);
      pp_cxx_left_paren (cxx_pp);
      dump_aggr_type (TYPE_METHOD_BASETYPE (t), flags);
      pp_cxx_colon_colon (cxx_pp);
      break;

    case ARRAY_TYPE:
      dump_type_prefix (TREE_TYPE (t), flags);
      break;

    case ENUMERAL_TYPE:
    case IDENTIFIER_NODE:
    case INTEGER_TYPE:
    case BOOLEAN_TYPE:
    case REAL_TYPE:
    case RECORD_TYPE:
    case TEMPLATE_TYPE_PARM:
    case TEMPLATE_TEMPLATE_PARM:
    case BOUND_TEMPLATE_TEMPLATE_PARM:
    case TREE_LIST:
    case TYPE_DECL:
    case TREE_VEC:
    case UNION_TYPE:
    case UNKNOWN_TYPE:
    case VOID_TYPE:
    case TYPENAME_TYPE:
    case COMPLEX_TYPE:
    case VECTOR_TYPE:
    case TYPEOF_TYPE:
      dump_type (t, flags);
      pp_base (cxx_pp)->padding = pp_before;
      break;

    default:
      pp_unsupported_tree (cxx_pp, t);
      /* fall through.  */
    case ERROR_MARK:
      pp_identifier (cxx_pp, "<typeprefixerror>");
      break;
    }
}

/* Dump the suffix of type T, under control of FLAGS.  This is the part
   which appears after the identifier (or function parms).  */

static void
dump_type_suffix (tree t, int flags)
{
  if (TYPE_PTRMEMFUNC_P (t))
    t = TYPE_PTRMEMFUNC_FN_TYPE (t);

  switch (TREE_CODE (t))
    {
    case POINTER_TYPE:
      /* APPLE LOCAL blocks 6040305 */
    case BLOCK_POINTER_TYPE:
    case REFERENCE_TYPE:
    case OFFSET_TYPE:
      if (TREE_CODE (TREE_TYPE (t)) == ARRAY_TYPE)
	pp_cxx_right_paren (cxx_pp);
      dump_type_suffix (TREE_TYPE (t), flags);
      break;

      /* Can only be reached through function pointer.  */
    case FUNCTION_TYPE:
    case METHOD_TYPE:
      {
	tree arg;
	pp_cxx_right_paren (cxx_pp);
	arg = TYPE_ARG_TYPES (t);
	if (TREE_CODE (t) == METHOD_TYPE)
	  arg = TREE_CHAIN (arg);

	/* Function pointers don't have default args.  Not in standard C++,
	   anyway; they may in g++, but we'll just pretend otherwise.  */
	dump_parameters (arg, flags & ~TFF_FUNCTION_DEFAULT_ARGUMENTS);

	if (TREE_CODE (t) == METHOD_TYPE)
	  pp_cxx_cv_qualifier_seq
	    (cxx_pp, TREE_TYPE (TREE_VALUE (TYPE_ARG_TYPES (t))));
	else
	  pp_cxx_cv_qualifier_seq(cxx_pp, t);
	dump_exception_spec (TYPE_RAISES_EXCEPTIONS (t), flags);
	dump_type_suffix (TREE_TYPE (t), flags);
	break;
      }

    case ARRAY_TYPE:
      pp_maybe_space (cxx_pp);
      pp_cxx_left_bracket (cxx_pp);
      if (TYPE_DOMAIN (t))
	{
	  if (host_integerp (TYPE_MAX_VALUE (TYPE_DOMAIN (t)), 0))
	    pp_wide_integer
	      (cxx_pp, tree_low_cst (TYPE_MAX_VALUE (TYPE_DOMAIN (t)), 0) + 1);
	  else if (TREE_CODE (TYPE_MAX_VALUE (TYPE_DOMAIN (t))) == MINUS_EXPR)
	    dump_expr (TREE_OPERAND (TYPE_MAX_VALUE (TYPE_DOMAIN (t)), 0),
		       flags & ~TFF_EXPR_IN_PARENS);
	  else
	    dump_expr (fold (cp_build_binary_op
			     (PLUS_EXPR, TYPE_MAX_VALUE (TYPE_DOMAIN (t)),
			      integer_one_node)),
		       flags & ~TFF_EXPR_IN_PARENS);
	}
      pp_cxx_right_bracket (cxx_pp);
      dump_type_suffix (TREE_TYPE (t), flags);
      break;

    case ENUMERAL_TYPE:
    case IDENTIFIER_NODE:
    case INTEGER_TYPE:
    case BOOLEAN_TYPE:
    case REAL_TYPE:
    case RECORD_TYPE:
    case TEMPLATE_TYPE_PARM:
    case TEMPLATE_TEMPLATE_PARM:
    case BOUND_TEMPLATE_TEMPLATE_PARM:
    case TREE_LIST:
    case TYPE_DECL:
    case TREE_VEC:
    case UNION_TYPE:
    case UNKNOWN_TYPE:
    case VOID_TYPE:
    case TYPENAME_TYPE:
    case COMPLEX_TYPE:
    case VECTOR_TYPE:
    case TYPEOF_TYPE:
      break;

    default:
      pp_unsupported_tree (cxx_pp, t);
    case ERROR_MARK:
      /* Don't mark it here, we should have already done in
	 dump_type_prefix.  */
      break;
    }
}

static void
dump_global_iord (tree t)
{
  const char *p = NULL;

  if (DECL_GLOBAL_CTOR_P (t))
    p = "initializers";
  else if (DECL_GLOBAL_DTOR_P (t))
    p = "destructors";
  else
    gcc_unreachable ();

  pp_printf (pp_base (cxx_pp), "(static %s for %s)", p, input_filename);
}

static void
dump_simple_decl (tree t, tree type, int flags)
{
  if (flags & TFF_DECL_SPECIFIERS)
    {
      dump_type_prefix (type, flags);
      pp_maybe_space (cxx_pp);
    }
  if (!DECL_INITIAL (t) || TREE_CODE (DECL_INITIAL (t)) != TEMPLATE_PARM_INDEX)
    dump_scope (CP_DECL_CONTEXT (t), flags);
  if (DECL_NAME (t))
    dump_decl (DECL_NAME (t), flags);
  else
    pp_identifier (cxx_pp, "<anonymous>");
  if (flags & TFF_DECL_SPECIFIERS)
    dump_type_suffix (type, flags);
}

/* Dump a human readable string for the decl T under control of FLAGS.  */

static void
dump_decl (tree t, int flags)
{
  if (t == NULL_TREE)
    return;

  switch (TREE_CODE (t))
    {
    case TYPE_DECL:
      /* Don't say 'typedef class A' */
      if (DECL_ARTIFICIAL (t))
	{
	  if ((flags & TFF_DECL_SPECIFIERS)
	      && TREE_CODE (TREE_TYPE (t)) == TEMPLATE_TYPE_PARM)
	    /* Say `class T' not just `T'.  */
	    pp_cxx_identifier (cxx_pp, "class");

	  dump_type (TREE_TYPE (t), flags);
	  break;
	}
      if (flags & TFF_DECL_SPECIFIERS)
	pp_cxx_identifier (cxx_pp, "typedef");
      dump_simple_decl (t, DECL_ORIGINAL_TYPE (t)
			? DECL_ORIGINAL_TYPE (t) : TREE_TYPE (t),
			flags);
      break;

    case VAR_DECL:
      if (DECL_NAME (t) && VTABLE_NAME_P (DECL_NAME (t)))
	{
	  pp_string (cxx_pp, "vtable for ");
	  gcc_assert (TYPE_P (DECL_CONTEXT (t)));
	  dump_type (DECL_CONTEXT (t), flags);
	  break;
	}
      /* Else fall through.  */
    case FIELD_DECL:
    case PARM_DECL:
      dump_simple_decl (t, TREE_TYPE (t), flags);
      break;

    case RESULT_DECL:
      pp_string (cxx_pp, "<return value> ");
      dump_simple_decl (t, TREE_TYPE (t), flags);
      break;

    case NAMESPACE_DECL:
      if (flags & TFF_DECL_SPECIFIERS)
	pp_cxx_declaration (cxx_pp, t);
      else
	{
	  dump_scope (CP_DECL_CONTEXT (t), flags);
	  if (DECL_NAME (t) == NULL_TREE)
	    pp_identifier (cxx_pp, "<unnamed>");
	  else
	    pp_cxx_tree_identifier (cxx_pp, DECL_NAME (t));
	}
      break;

    case SCOPE_REF:
      pp_expression (cxx_pp, t);
      break;

    case ARRAY_REF:
      dump_decl (TREE_OPERAND (t, 0), flags);
      pp_cxx_left_bracket (cxx_pp);
      dump_decl (TREE_OPERAND (t, 1), flags);
      pp_cxx_right_bracket (cxx_pp);
      break;

      /* So that we can do dump_decl on an aggr type.  */
    case RECORD_TYPE:
    case UNION_TYPE:
    case ENUMERAL_TYPE:
      dump_type (t, flags);
      break;

    case BIT_NOT_EXPR:
      /* This is a pseudo destructor call which has not been folded into
	 a PSEUDO_DTOR_EXPR yet.  */
      pp_cxx_complement (cxx_pp);
      dump_type (TREE_OPERAND (t, 0), flags);
      break;

    case TYPE_EXPR:
      gcc_unreachable ();
      break;

      /* These special cases are duplicated here so that other functions
	 can feed identifiers to error and get them demangled properly.  */
    case IDENTIFIER_NODE:
      if (IDENTIFIER_TYPENAME_P (t))
	{
	  pp_cxx_identifier (cxx_pp, "operator");
	  /* Not exactly IDENTIFIER_TYPE_VALUE.  */
	  dump_type (TREE_TYPE (t), flags);
	  break;
	}
      else
	pp_cxx_tree_identifier (cxx_pp, t);
      break;

    case OVERLOAD:
      if (OVL_CHAIN (t))
	{
	  t = OVL_CURRENT (t);
	  if (DECL_CLASS_SCOPE_P (t))
	    {
	      dump_type (DECL_CONTEXT (t), flags);
	      pp_cxx_colon_colon (cxx_pp);
	    }
	  else if (DECL_CONTEXT (t))
	    {
	      dump_decl (DECL_CONTEXT (t), flags);
	      pp_cxx_colon_colon (cxx_pp);
	    }
	  dump_decl (DECL_NAME (t), flags);
	  break;
	}

      /* If there's only one function, just treat it like an ordinary
	 FUNCTION_DECL.  */
      t = OVL_CURRENT (t);
      /* Fall through.  */

    case FUNCTION_DECL:
      if (! DECL_LANG_SPECIFIC (t))
	pp_identifier (cxx_pp, "<built-in>");
      else if (DECL_GLOBAL_CTOR_P (t) || DECL_GLOBAL_DTOR_P (t))
	dump_global_iord (t);
      else
	dump_function_decl (t, flags);
      break;

    case TEMPLATE_DECL:
      dump_template_decl (t, flags);
      break;

    case TEMPLATE_ID_EXPR:
      {
	tree name = TREE_OPERAND (t, 0);

	if (is_overloaded_fn (name))
	  name = DECL_NAME (get_first_fn (name));
	dump_decl (name, flags);
	pp_cxx_begin_template_argument_list (cxx_pp);
	if (TREE_OPERAND (t, 1))
	  dump_template_argument_list (TREE_OPERAND (t, 1), flags);
	pp_cxx_end_template_argument_list (cxx_pp);
      }
      break;

    case LABEL_DECL:
      pp_cxx_tree_identifier (cxx_pp, DECL_NAME (t));
      break;

    case CONST_DECL:
      if ((TREE_TYPE (t) != NULL_TREE && NEXT_CODE (t) == ENUMERAL_TYPE)
	  || (DECL_INITIAL (t) &&
	      TREE_CODE (DECL_INITIAL (t)) == TEMPLATE_PARM_INDEX))
	dump_simple_decl (t, TREE_TYPE (t), flags);
      else if (DECL_NAME (t))
	dump_decl (DECL_NAME (t), flags);
      else if (DECL_INITIAL (t))
	dump_expr (DECL_INITIAL (t), flags | TFF_EXPR_IN_PARENS);
      else
	pp_identifier (cxx_pp, "<enumerator>");
      break;

    case USING_DECL:
      pp_cxx_identifier (cxx_pp, "using");
      dump_type (USING_DECL_SCOPE (t), flags);
      pp_cxx_colon_colon (cxx_pp);
      dump_decl (DECL_NAME (t), flags);
      break;

    case BASELINK:
      dump_decl (BASELINK_FUNCTIONS (t), flags);
      break;

    case NON_DEPENDENT_EXPR:
      dump_expr (t, flags);
      break;

    case TEMPLATE_TYPE_PARM:
      if (flags & TFF_DECL_SPECIFIERS)
	pp_cxx_declaration (cxx_pp, t);
      else
	pp_type_id (cxx_pp, t);
      break;

    case UNBOUND_CLASS_TEMPLATE:
      dump_type (t, flags);
      break;

    default:
      pp_unsupported_tree (cxx_pp, t);
      /* Fall through to error.  */

    case ERROR_MARK:
      pp_identifier (cxx_pp, "<declaration error>");
      break;
    }
}

/* Dump a template declaration T under control of FLAGS. This means the
   'template <...> leaders plus the 'class X' or 'void fn(...)' part.  */

static void
dump_template_decl (tree t, int flags)
{
  tree orig_parms = DECL_TEMPLATE_PARMS (t);
  tree parms;
  int i;

  if (flags & TFF_TEMPLATE_HEADER)
    {
      for (parms = orig_parms = nreverse (orig_parms);
	   parms;
	   parms = TREE_CHAIN (parms))
	{
	  tree inner_parms = INNERMOST_TEMPLATE_PARMS (parms);
	  int len = TREE_VEC_LENGTH (inner_parms);

	  pp_cxx_identifier (cxx_pp, "template");
	  pp_cxx_begin_template_argument_list (cxx_pp);

	  /* If we've shown the template prefix, we'd better show the
	     parameters' and decl's type too.  */
	    flags |= TFF_DECL_SPECIFIERS;

	  for (i = 0; i < len; i++)
	    {
	      if (i)
		pp_separate_with_comma (cxx_pp);
	      dump_template_parameter (TREE_VEC_ELT (inner_parms, i), flags);
	    }
	  pp_cxx_end_template_argument_list (cxx_pp);
	  pp_cxx_whitespace (cxx_pp);
	}
      nreverse(orig_parms);

      if (DECL_TEMPLATE_TEMPLATE_PARM_P (t))
	/* Say `template<arg> class TT' not just `template<arg> TT'.  */
	pp_cxx_identifier (cxx_pp, "class");
    }

  if (TREE_CODE (DECL_TEMPLATE_RESULT (t)) == TYPE_DECL)
    dump_type (TREE_TYPE (t),
	       ((flags & ~TFF_CLASS_KEY_OR_ENUM) | TFF_TEMPLATE_NAME
		| (flags & TFF_DECL_SPECIFIERS ? TFF_CLASS_KEY_OR_ENUM : 0)));
  else if (TREE_CODE (DECL_TEMPLATE_RESULT (t)) == VAR_DECL)
    dump_decl (DECL_TEMPLATE_RESULT (t), flags | TFF_TEMPLATE_NAME);
  else
    {
      gcc_assert (TREE_TYPE (t));
      switch (NEXT_CODE (t))
	{
	case METHOD_TYPE:
	case FUNCTION_TYPE:
	  dump_function_decl (t, flags | TFF_TEMPLATE_NAME);
	  break;
	default:
	  /* This case can occur with some invalid code.  */
	  dump_type (TREE_TYPE (t),
		     (flags & ~TFF_CLASS_KEY_OR_ENUM) | TFF_TEMPLATE_NAME
		     | (flags & TFF_DECL_SPECIFIERS
			? TFF_CLASS_KEY_OR_ENUM : 0));
	}
    }
}

/* Pretty print a function decl. There are several ways we want to print a
   function declaration. The TFF_ bits in FLAGS tells us how to behave.
   As error can only apply the '#' flag once to give 0 and 1 for V, there
   is %D which doesn't print the throw specs, and %F which does.  */

static void
dump_function_decl (tree t, int flags)
{
  tree fntype;
  tree parmtypes;
  tree cname = NULL_TREE;
  tree template_args = NULL_TREE;
  tree template_parms = NULL_TREE;
  int show_return = flags & TFF_RETURN_TYPE || flags & TFF_DECL_SPECIFIERS;

  if (TREE_CODE (t) == TEMPLATE_DECL)
    t = DECL_TEMPLATE_RESULT (t);

  /* Pretty print template instantiations only.  */
  if (DECL_USE_TEMPLATE (t) && DECL_TEMPLATE_INFO (t))
    {
      tree tmpl;

      template_args = DECL_TI_ARGS (t);
      tmpl = most_general_template (t);
      if (tmpl && TREE_CODE (tmpl) == TEMPLATE_DECL)
	{
	  template_parms = DECL_TEMPLATE_PARMS (tmpl);
	  t = tmpl;
	}
    }

  fntype = TREE_TYPE (t);
  parmtypes = FUNCTION_FIRST_USER_PARMTYPE (t);

  if (DECL_CLASS_SCOPE_P (t))
    cname = DECL_CONTEXT (t);
  /* This is for partially instantiated template methods.  */
  else if (TREE_CODE (fntype) == METHOD_TYPE)
    cname = TREE_TYPE (TREE_VALUE (parmtypes));

  if (!(flags & TFF_DECL_SPECIFIERS))
    /* OK */;
  else if (DECL_STATIC_FUNCTION_P (t))
    pp_cxx_identifier (cxx_pp, "static");
  else if (DECL_VIRTUAL_P (t))
    pp_cxx_identifier (cxx_pp, "virtual");

  /* Print the return type?  */
  if (show_return)
    show_return = !DECL_CONV_FN_P (t)  && !DECL_CONSTRUCTOR_P (t)
		  && !DECL_DESTRUCTOR_P (t);
  if (show_return)
    dump_type_prefix (TREE_TYPE (fntype), flags);

  /* Print the function name.  */
  if (cname)
    {
      dump_type (cname, flags);
      pp_cxx_colon_colon (cxx_pp);
    }
  else
    dump_scope (CP_DECL_CONTEXT (t), flags);

  dump_function_name (t, flags);

  if (!(flags & TFF_NO_FUNCTION_ARGUMENTS))
    {
      dump_parameters (parmtypes, flags);

      if (TREE_CODE (fntype) == METHOD_TYPE)
	{
	  pp_base (cxx_pp)->padding = pp_before;
	  pp_cxx_cv_qualifier_seq
	    (cxx_pp, TREE_TYPE (TREE_VALUE (TYPE_ARG_TYPES (fntype))));
	}

      if (flags & TFF_EXCEPTION_SPECIFICATION)
	{
	  pp_base (cxx_pp)->padding = pp_before;
	  dump_exception_spec (TYPE_RAISES_EXCEPTIONS (fntype), flags);
	}

      if (show_return)
	dump_type_suffix (TREE_TYPE (fntype), flags);
    }

  /* If T is a template instantiation, dump the parameter binding.  */
  if (template_parms != NULL_TREE && template_args != NULL_TREE)
    {
      pp_cxx_whitespace (cxx_pp);
      pp_cxx_left_bracket (cxx_pp);
      pp_cxx_identifier (cxx_pp, "with");
      pp_cxx_whitespace (cxx_pp);
      dump_template_bindings (template_parms, template_args);
      pp_cxx_right_bracket (cxx_pp);
    }
}

/* Print a parameter list. If this is for a member function, the
   member object ptr (and any other hidden args) should have
   already been removed.  */

static void
dump_parameters (tree parmtypes, int flags)
{
  int first;

  pp_cxx_left_paren (cxx_pp);

  for (first = 1; parmtypes != void_list_node;
       parmtypes = TREE_CHAIN (parmtypes))
    {
      if (!first)
	pp_separate_with_comma (cxx_pp);
      first = 0;
      if (!parmtypes)
	{
	  pp_cxx_identifier (cxx_pp, "...");
	  break;
	}
      dump_type (TREE_VALUE (parmtypes), flags);

      if ((flags & TFF_FUNCTION_DEFAULT_ARGUMENTS) && TREE_PURPOSE (parmtypes))
	{
	  pp_cxx_whitespace (cxx_pp);
	  pp_equal (cxx_pp);
	  pp_cxx_whitespace (cxx_pp);
	  dump_expr (TREE_PURPOSE (parmtypes), flags | TFF_EXPR_IN_PARENS);
	}
    }

  pp_cxx_right_paren (cxx_pp);
}

/* Print an exception specification. T is the exception specification.  */

static void
dump_exception_spec (tree t, int flags)
{
  if (t)
    {
      pp_cxx_identifier (cxx_pp, "throw");
      pp_cxx_whitespace (cxx_pp);
      pp_cxx_left_paren (cxx_pp);
      if (TREE_VALUE (t) != NULL_TREE)
	while (1)
	  {
	    dump_type (TREE_VALUE (t), flags);
	    t = TREE_CHAIN (t);
	    if (!t)
	      break;
	    pp_separate_with_comma (cxx_pp);
	  }
      pp_cxx_right_paren (cxx_pp);
    }
}

/* Handle the function name for a FUNCTION_DECL node, grokking operators
   and destructors properly.  */

static void
dump_function_name (tree t, int flags)
{
  tree name = DECL_NAME (t);

  /* We can get here with a decl that was synthesized by language-
     independent machinery (e.g. coverage.c) in which case it won't
     have a lang_specific structure attached and DECL_CONSTRUCTOR_P
     will crash.  In this case it is safe just to print out the
     literal name.  */
  if (!DECL_LANG_SPECIFIC (t))
    {
      pp_cxx_tree_identifier (cxx_pp, name);
      return;
    }

  if (TREE_CODE (t) == TEMPLATE_DECL)
    t = DECL_TEMPLATE_RESULT (t);

  /* Don't let the user see __comp_ctor et al.  */
  if (DECL_CONSTRUCTOR_P (t)
      || DECL_DESTRUCTOR_P (t))
    name = constructor_name (DECL_CONTEXT (t));

  if (DECL_DESTRUCTOR_P (t))
    {
      pp_cxx_complement (cxx_pp);
      dump_decl (name, TFF_PLAIN_IDENTIFIER);
    }
  else if (DECL_CONV_FN_P (t))
    {
      /* This cannot use the hack that the operator's return
	 type is stashed off of its name because it may be
	 used for error reporting.  In the case of conflicting
	 declarations, both will have the same name, yet
	 the types will be different, hence the TREE_TYPE field
	 of the first name will be clobbered by the second.  */
      pp_cxx_identifier (cxx_pp, "operator");
      dump_type (TREE_TYPE (TREE_TYPE (t)), flags);
    }
  else if (IDENTIFIER_OPNAME_P (name))
    pp_cxx_tree_identifier (cxx_pp, name);
  else
    dump_decl (name, flags);

  if (DECL_TEMPLATE_INFO (t)
      && !DECL_FRIEND_PSEUDO_TEMPLATE_INSTANTIATION (t)
      && (TREE_CODE (DECL_TI_TEMPLATE (t)) != TEMPLATE_DECL
	  || PRIMARY_TEMPLATE_P (DECL_TI_TEMPLATE (t))))
    dump_template_parms (DECL_TEMPLATE_INFO (t), !DECL_USE_TEMPLATE (t), flags);
}

/* Dump the template parameters from the template info INFO under control of
   FLAGS. PRIMARY indicates whether this is a primary template decl, or
   specialization (partial or complete). For partial specializations we show
   the specialized parameter values. For a primary template we show no
   decoration.  */

static void
dump_template_parms (tree info, int primary, int flags)
{
  tree args = info ? TI_ARGS (info) : NULL_TREE;

  if (primary && flags & TFF_TEMPLATE_NAME)
    return;
  flags &= ~(TFF_CLASS_KEY_OR_ENUM | TFF_TEMPLATE_NAME);
  pp_cxx_begin_template_argument_list (cxx_pp);

  /* Be careful only to print things when we have them, so as not
	 to crash producing error messages.  */
  if (args && !primary)
    {
      int len, ix;

      if (TMPL_ARGS_HAVE_MULTIPLE_LEVELS (args))
	args = TREE_VEC_ELT (args, TREE_VEC_LENGTH (args) - 1);

      len = TREE_VEC_LENGTH (args);

      for (ix = 0; ix != len; ix++)
	{
	  tree arg = TREE_VEC_ELT (args, ix);

	  if (ix)
	    pp_separate_with_comma (cxx_pp);

	  if (!arg)
	    pp_identifier (cxx_pp, "<template parameter error>");
	  else
	    dump_template_argument (arg, flags);
	}
    }
  else if (primary)
    {
      tree tpl = TI_TEMPLATE (info);
      tree parms = DECL_TEMPLATE_PARMS (tpl);
      int len, ix;

      parms = TREE_CODE (parms) == TREE_LIST ? TREE_VALUE (parms) : NULL_TREE;
      len = parms ? TREE_VEC_LENGTH (parms) : 0;

      for (ix = 0; ix != len; ix++)
	{
	  tree parm;

          if (TREE_VEC_ELT (parms, ix) == error_mark_node)
            {
              pp_identifier (cxx_pp, "<template parameter error>");
              continue;
            }

          parm = TREE_VALUE (TREE_VEC_ELT (parms, ix));

	  if (ix)
	    pp_separate_with_comma (cxx_pp);

	  dump_decl (parm, flags & ~TFF_DECL_SPECIFIERS);
	}
    }
  pp_cxx_end_template_argument_list (cxx_pp);
}

/* Print out a list of initializers (subr of dump_expr).  */

static void
dump_expr_list (tree l, int flags)
{
  while (l)
    {
      dump_expr (TREE_VALUE (l), flags | TFF_EXPR_IN_PARENS);
      l = TREE_CHAIN (l);
      if (l)
	pp_separate_with_comma (cxx_pp);
    }
}

/* Print out a vector of initializers (subr of dump_expr).  */

static void
dump_expr_init_vec (VEC(constructor_elt,gc) *v, int flags)
{
  unsigned HOST_WIDE_INT idx;
  tree value;

  FOR_EACH_CONSTRUCTOR_VALUE (v, idx, value)
    {
      dump_expr (value, flags | TFF_EXPR_IN_PARENS);
      if (idx != VEC_length (constructor_elt, v) - 1)
	pp_separate_with_comma (cxx_pp);
    }
}


/* We've gotten an indirect REFERENCE (an OBJ_TYPE_REF) to a virtual
   function.  Resolve it to a close relative -- in the sense of static
   type -- variant being overridden.  That is close to what was written in
   the source code.  Subroutine of dump_expr.  */

static tree
resolve_virtual_fun_from_obj_type_ref (tree ref)
{
  tree obj_type = TREE_TYPE (OBJ_TYPE_REF_OBJECT (ref));
  HOST_WIDE_INT index = tree_low_cst (OBJ_TYPE_REF_TOKEN (ref), 1);
  tree fun = BINFO_VIRTUALS (TYPE_BINFO (TREE_TYPE (obj_type)));
  while (index)
    {
      fun = TREE_CHAIN (fun);
      index -= (TARGET_VTABLE_USES_DESCRIPTORS
		? TARGET_VTABLE_USES_DESCRIPTORS : 1);
    }

  return BV_FN (fun);
}

/* Print out an expression E under control of FLAGS.  */

static void
dump_expr (tree t, int flags)
{
  if (t == 0)
    return;

  switch (TREE_CODE (t))
    {
    case VAR_DECL:
    case PARM_DECL:
    case FIELD_DECL:
    case CONST_DECL:
    case FUNCTION_DECL:
    case TEMPLATE_DECL:
    case NAMESPACE_DECL:
    case LABEL_DECL:
    case OVERLOAD:
    case IDENTIFIER_NODE:
      dump_decl (t, (flags & ~TFF_DECL_SPECIFIERS) | TFF_NO_FUNCTION_ARGUMENTS);
      break;

    case INTEGER_CST:
    case REAL_CST:
    case STRING_CST:
      pp_constant (cxx_pp, t);
      break;

    case THROW_EXPR:
      pp_cxx_identifier (cxx_pp, "throw");
      dump_expr (TREE_OPERAND (t, 0), flags);
      break;

    case PTRMEM_CST:
      pp_ampersand (cxx_pp);
      dump_type (PTRMEM_CST_CLASS (t), flags);
      pp_cxx_colon_colon (cxx_pp);
      pp_cxx_tree_identifier (cxx_pp, DECL_NAME (PTRMEM_CST_MEMBER (t)));
      break;

    case COMPOUND_EXPR:
      pp_cxx_left_paren (cxx_pp);
      dump_expr (TREE_OPERAND (t, 0), flags | TFF_EXPR_IN_PARENS);
      pp_separate_with_comma (cxx_pp);
      dump_expr (TREE_OPERAND (t, 1), flags | TFF_EXPR_IN_PARENS);
      pp_cxx_right_paren (cxx_pp);
      break;

    case COND_EXPR:
      pp_cxx_left_paren (cxx_pp);
      dump_expr (TREE_OPERAND (t, 0), flags | TFF_EXPR_IN_PARENS);
      pp_string (cxx_pp, " ? ");
      dump_expr (TREE_OPERAND (t, 1), flags | TFF_EXPR_IN_PARENS);
      pp_string (cxx_pp, " : ");
      dump_expr (TREE_OPERAND (t, 2), flags | TFF_EXPR_IN_PARENS);
      pp_cxx_right_paren (cxx_pp);
      break;

    case SAVE_EXPR:
      if (TREE_HAS_CONSTRUCTOR (t))
	{
	  pp_cxx_identifier (cxx_pp, "new");
	  pp_cxx_whitespace (cxx_pp);
	  dump_type (TREE_TYPE (TREE_TYPE (t)), flags);
	}
      else
	dump_expr (TREE_OPERAND (t, 0), flags | TFF_EXPR_IN_PARENS);
      break;

    case AGGR_INIT_EXPR:
      {
	tree fn = NULL_TREE;

	if (TREE_CODE (TREE_OPERAND (t, 0)) == ADDR_EXPR)
	  fn = TREE_OPERAND (TREE_OPERAND (t, 0), 0);

	if (fn && TREE_CODE (fn) == FUNCTION_DECL)
	  {
	    if (DECL_CONSTRUCTOR_P (fn))
	      dump_type (DECL_CONTEXT (fn), flags);
	    else
	      dump_decl (fn, 0);
	  }
	else
	  dump_expr (TREE_OPERAND (t, 0), 0);
      }
      pp_cxx_left_paren (cxx_pp);
      if (TREE_OPERAND (t, 1))
	dump_expr_list (TREE_CHAIN (TREE_OPERAND (t, 1)), flags);
      pp_cxx_right_paren (cxx_pp);
      break;

    case CALL_EXPR:
      {
	tree fn = TREE_OPERAND (t, 0);
	tree args = TREE_OPERAND (t, 1);

	if (TREE_CODE (fn) == ADDR_EXPR)
	  fn = TREE_OPERAND (fn, 0);

	/* Nobody is interested in seeing the guts of vcalls.  */
	if (TREE_CODE (fn) == OBJ_TYPE_REF)
	  fn = resolve_virtual_fun_from_obj_type_ref (fn);

	if (TREE_TYPE (fn) != NULL_TREE && NEXT_CODE (fn) == METHOD_TYPE)
	  {
	    tree ob = TREE_VALUE (args);
	    if (TREE_CODE (ob) == ADDR_EXPR)
	      {
		dump_expr (TREE_OPERAND (ob, 0), flags | TFF_EXPR_IN_PARENS);
		pp_cxx_dot (cxx_pp);
	      }
	    else if (TREE_CODE (ob) != PARM_DECL
		     || strcmp (IDENTIFIER_POINTER (DECL_NAME (ob)), "this"))
	      {
		dump_expr (ob, flags | TFF_EXPR_IN_PARENS);
		pp_cxx_arrow (cxx_pp);
	      }
	    args = TREE_CHAIN (args);
	  }
	dump_expr (fn, flags | TFF_EXPR_IN_PARENS);
	pp_cxx_left_paren (cxx_pp);
	dump_expr_list (args, flags);
	pp_cxx_right_paren (cxx_pp);
      }
      break;

    case NEW_EXPR:
      {
	tree type = TREE_OPERAND (t, 1);
	tree init = TREE_OPERAND (t, 2);
	if (NEW_EXPR_USE_GLOBAL (t))
	  pp_cxx_colon_colon (cxx_pp);
	pp_cxx_identifier (cxx_pp, "new");
	if (TREE_OPERAND (t, 0))
	  {
	    pp_cxx_left_paren (cxx_pp);
	    dump_expr_list (TREE_OPERAND (t, 0), flags);
	    pp_cxx_right_paren (cxx_pp);
	    pp_cxx_whitespace (cxx_pp);
	  }
	if (TREE_CODE (type) == ARRAY_REF)
	  type = build_cplus_array_type
	    (TREE_OPERAND (type, 0),
	     build_index_type (fold_build2 (MINUS_EXPR, integer_type_node,
					    TREE_OPERAND (type, 1),
					    integer_one_node)));
	dump_type (type, flags);
	if (init)
	  {
	    pp_cxx_left_paren (cxx_pp);
	    if (TREE_CODE (init) == TREE_LIST)
	      dump_expr_list (init, flags);
	    else if (init == void_zero_node)
	      /* This representation indicates an empty initializer,
		 e.g.: "new int()".  */
	      ;
	    else
	      dump_expr (init, flags);
	    pp_cxx_right_paren (cxx_pp);
	  }
      }
      break;

    case TARGET_EXPR:
      /* Note that this only works for G++ target exprs.  If somebody
	 builds a general TARGET_EXPR, there's no way to represent that
	 it initializes anything other that the parameter slot for the
	 default argument.  Note we may have cleared out the first
	 operand in expand_expr, so don't go killing ourselves.  */
      if (TREE_OPERAND (t, 1))
	dump_expr (TREE_OPERAND (t, 1), flags | TFF_EXPR_IN_PARENS);
      break;

    case INIT_EXPR:
    case MODIFY_EXPR:
    case PLUS_EXPR:
    case MINUS_EXPR:
    case MULT_EXPR:
    case TRUNC_DIV_EXPR:
    case TRUNC_MOD_EXPR:
    case MIN_EXPR:
    case MAX_EXPR:
    case LSHIFT_EXPR:
    case RSHIFT_EXPR:
    case BIT_IOR_EXPR:
    case BIT_XOR_EXPR:
    case BIT_AND_EXPR:
    case TRUTH_ANDIF_EXPR:
    case TRUTH_ORIF_EXPR:
    case LT_EXPR:
    case LE_EXPR:
    case GT_EXPR:
    case GE_EXPR:
    case EQ_EXPR:
    case NE_EXPR:
    case EXACT_DIV_EXPR:
      dump_binary_op (operator_name_info[(int) TREE_CODE (t)].name, t, flags);
      break;

    case CEIL_DIV_EXPR:
    case FLOOR_DIV_EXPR:
    case ROUND_DIV_EXPR:
    case RDIV_EXPR:
      dump_binary_op ("/", t, flags);
      break;

    case CEIL_MOD_EXPR:
    case FLOOR_MOD_EXPR:
    case ROUND_MOD_EXPR:
      dump_binary_op ("%", t, flags);
      break;

    case COMPONENT_REF:
      {
	tree ob = TREE_OPERAND (t, 0);
	if (TREE_CODE (ob) == INDIRECT_REF)
	  {
	    ob = TREE_OPERAND (ob, 0);
	    if (TREE_CODE (ob) != PARM_DECL
		|| (DECL_NAME (ob)
		    && strcmp (IDENTIFIER_POINTER (DECL_NAME (ob)), "this")))
	      {
		dump_expr (ob, flags | TFF_EXPR_IN_PARENS);
		pp_cxx_arrow (cxx_pp);
	      }
	  }
	else
	  {
	    dump_expr (ob, flags | TFF_EXPR_IN_PARENS);
	    pp_cxx_dot (cxx_pp);
	  }
	dump_expr (TREE_OPERAND (t, 1), flags & ~TFF_EXPR_IN_PARENS);
      }
      break;

    case ARRAY_REF:
      dump_expr (TREE_OPERAND (t, 0), flags | TFF_EXPR_IN_PARENS);
      pp_cxx_left_bracket (cxx_pp);
      dump_expr (TREE_OPERAND (t, 1), flags | TFF_EXPR_IN_PARENS);
      pp_cxx_right_bracket (cxx_pp);
      break;

    case UNARY_PLUS_EXPR:
      dump_unary_op ("+", t, flags);
      break;

    case ADDR_EXPR:
      if (TREE_CODE (TREE_OPERAND (t, 0)) == FUNCTION_DECL
	  || TREE_CODE (TREE_OPERAND (t, 0)) == STRING_CST
	  /* An ADDR_EXPR can have reference type.  In that case, we
	     shouldn't print the `&' doing so indicates to the user
	     that the expression has pointer type.  */
	  || (TREE_TYPE (t)
	      && TREE_CODE (TREE_TYPE (t)) == REFERENCE_TYPE))
	dump_expr (TREE_OPERAND (t, 0), flags | TFF_EXPR_IN_PARENS);
      else if (TREE_CODE (TREE_OPERAND (t, 0)) == LABEL_DECL)
	dump_unary_op ("&&", t, flags);
      else
	dump_unary_op ("&", t, flags);
      break;

    case INDIRECT_REF:
      if (TREE_HAS_CONSTRUCTOR (t))
	{
	  t = TREE_OPERAND (t, 0);
	  gcc_assert (TREE_CODE (t) == CALL_EXPR);
	  dump_expr (TREE_OPERAND (t, 0), flags | TFF_EXPR_IN_PARENS);
	  pp_cxx_left_paren (cxx_pp);
	  dump_expr_list (TREE_CHAIN (TREE_OPERAND (t, 1)), flags);
	  pp_cxx_right_paren (cxx_pp);
	}
      else
	{
	  if (TREE_OPERAND (t,0) != NULL_TREE
	      && TREE_TYPE (TREE_OPERAND (t, 0))
	      && NEXT_CODE (TREE_OPERAND (t, 0)) == REFERENCE_TYPE)
	    dump_expr (TREE_OPERAND (t, 0), flags);
	  else
	    dump_unary_op ("*", t, flags);
	}
      break;

    case NEGATE_EXPR:
    case BIT_NOT_EXPR:
    case TRUTH_NOT_EXPR:
    case PREDECREMENT_EXPR:
    case PREINCREMENT_EXPR:
      dump_unary_op (operator_name_info [(int)TREE_CODE (t)].name, t, flags);
      break;

    case POSTDECREMENT_EXPR:
    case POSTINCREMENT_EXPR:
      pp_cxx_left_paren (cxx_pp);
      dump_expr (TREE_OPERAND (t, 0), flags | TFF_EXPR_IN_PARENS);
      pp_cxx_identifier (cxx_pp, operator_name_info[(int)TREE_CODE (t)].name);
      pp_cxx_right_paren (cxx_pp);
      break;

    case NON_LVALUE_EXPR:
      /* FIXME: This is a KLUDGE workaround for a parsing problem.  There
	 should be another level of INDIRECT_REF so that I don't have to do
	 this.  */
      if (TREE_TYPE (t) != NULL_TREE && NEXT_CODE (t) == POINTER_TYPE)
	{
	  tree next = TREE_TYPE (TREE_TYPE (t));

	  while (TREE_CODE (next) == POINTER_TYPE)
	    next = TREE_TYPE (next);

	  if (TREE_CODE (next) == FUNCTION_TYPE)
	    {
	      if (flags & TFF_EXPR_IN_PARENS)
		pp_cxx_left_paren (cxx_pp);
	      pp_cxx_star (cxx_pp);
	      dump_expr (TREE_OPERAND (t, 0), flags & ~TFF_EXPR_IN_PARENS);
	      if (flags & TFF_EXPR_IN_PARENS)
		pp_cxx_right_paren (cxx_pp);
	      break;
	    }
	  /* Else fall through.  */
	}
      dump_expr (TREE_OPERAND (t, 0), flags | TFF_EXPR_IN_PARENS);
      break;

    case NOP_EXPR:
    case CONVERT_EXPR:
      {
	tree op = TREE_OPERAND (t, 0);

	if (!same_type_p (TREE_TYPE (op), TREE_TYPE (t)))
	  {
	    /* It is a cast, but we cannot tell whether it is a
	       reinterpret or static cast. Use the C style notation.  */
	    if (flags & TFF_EXPR_IN_PARENS)
	      pp_cxx_left_paren (cxx_pp);
	    pp_cxx_left_paren (cxx_pp);
	    dump_type (TREE_TYPE (t), flags);
	    pp_cxx_right_paren (cxx_pp);
	    dump_expr (op, flags | TFF_EXPR_IN_PARENS);
	    if (flags & TFF_EXPR_IN_PARENS)
	      pp_cxx_right_paren (cxx_pp);
	  }
	else
	  dump_expr (op, flags);
	break;
      }

    case CONSTRUCTOR:
      if (TREE_TYPE (t) && TYPE_PTRMEMFUNC_P (TREE_TYPE (t)))
	{
	  tree idx = build_ptrmemfunc_access_expr (t, pfn_identifier);

	  if (integer_zerop (idx))
	    {
	      /* A NULL pointer-to-member constant.  */
	      pp_cxx_left_paren (cxx_pp);
	      pp_cxx_left_paren (cxx_pp);
	      dump_type (TREE_TYPE (t), flags);
	      pp_cxx_right_paren (cxx_pp);
	      pp_character (cxx_pp, '0');
	      pp_cxx_right_paren (cxx_pp);
	      break;
	    }
	  else if (host_integerp (idx, 0))
	    {
	      tree virtuals;
	      unsigned HOST_WIDE_INT n;

	      t = TREE_TYPE (TYPE_PTRMEMFUNC_FN_TYPE (TREE_TYPE (t)));
	      t = TYPE_METHOD_BASETYPE (t);
	      virtuals = BINFO_VIRTUALS (TYPE_BINFO (TYPE_MAIN_VARIANT (t)));

	      n = tree_low_cst (idx, 0);

	      /* Map vtable index back one, to allow for the null pointer to
		 member.  */
	      --n;

	      while (n > 0 && virtuals)
		{
		  --n;
		  virtuals = TREE_CHAIN (virtuals);
		}
	      if (virtuals)
		{
		  dump_expr (BV_FN (virtuals),
			     flags | TFF_EXPR_IN_PARENS);
		  break;
		}
	    }
	}
      if (TREE_TYPE (t) && EMPTY_CONSTRUCTOR_P (t))
	{
	  dump_type (TREE_TYPE (t), 0);
	  pp_cxx_left_paren (cxx_pp);
	  pp_cxx_right_paren (cxx_pp);
	}
      else
	{
	  pp_cxx_left_brace (cxx_pp);
	  dump_expr_init_vec (CONSTRUCTOR_ELTS (t), flags);
	  pp_cxx_right_brace (cxx_pp);
	}

      break;

    case OFFSET_REF:
      {
	tree ob = TREE_OPERAND (t, 0);
	if (is_dummy_object (ob))
	  {
	    t = TREE_OPERAND (t, 1);
	    if (TREE_CODE (t) == FUNCTION_DECL)
	      /* A::f */
	      dump_expr (t, flags | TFF_EXPR_IN_PARENS);
	    else if (BASELINK_P (t))
	      dump_expr (OVL_CURRENT (BASELINK_FUNCTIONS (t)),
			 flags | TFF_EXPR_IN_PARENS);
	    else
	      dump_decl (t, flags);
	  }
	else
	  {
	    if (TREE_CODE (ob) == INDIRECT_REF)
	      {
		dump_expr (TREE_OPERAND (ob, 0), flags | TFF_EXPR_IN_PARENS);
		pp_cxx_arrow (cxx_pp);
		pp_cxx_star (cxx_pp);
	      }
	    else
	      {
		dump_expr (ob, flags | TFF_EXPR_IN_PARENS);
		pp_cxx_dot (cxx_pp);
		pp_cxx_star (cxx_pp);
	      }
	    dump_expr (TREE_OPERAND (t, 1), flags | TFF_EXPR_IN_PARENS);
	  }
	break;
      }

    case TEMPLATE_PARM_INDEX:
      dump_decl (TEMPLATE_PARM_DECL (t), flags & ~TFF_DECL_SPECIFIERS);
      break;

    case SCOPE_REF:
      pp_expression (cxx_pp, t);
      break;

    case CAST_EXPR:
      if (TREE_OPERAND (t, 0) == NULL_TREE
	  || TREE_CHAIN (TREE_OPERAND (t, 0)))
	{
	  dump_type (TREE_TYPE (t), flags);
	  pp_cxx_left_paren (cxx_pp);
	  dump_expr_list (TREE_OPERAND (t, 0), flags);
	  pp_cxx_right_paren (cxx_pp);
	}
      else
	{
	  pp_cxx_left_paren (cxx_pp);
	  dump_type (TREE_TYPE (t), flags);
	  pp_cxx_right_paren (cxx_pp);
	  pp_cxx_left_paren (cxx_pp);
	  dump_expr_list (TREE_OPERAND (t, 0), flags);
	  pp_cxx_right_paren (cxx_pp);
	}
      break;

    case STATIC_CAST_EXPR:
      pp_cxx_identifier (cxx_pp, "static_cast");
      goto cast;
    case REINTERPRET_CAST_EXPR:
      pp_cxx_identifier (cxx_pp, "reinterpret_cast");
      goto cast;
    case CONST_CAST_EXPR:
      pp_cxx_identifier (cxx_pp, "const_cast");
      goto cast;
    case DYNAMIC_CAST_EXPR:
      pp_cxx_identifier (cxx_pp, "dynamic_cast");
    cast:
      pp_cxx_begin_template_argument_list (cxx_pp);
      dump_type (TREE_TYPE (t), flags);
      pp_cxx_end_template_argument_list (cxx_pp);
      pp_cxx_left_paren (cxx_pp);
      dump_expr (TREE_OPERAND (t, 0), flags);
      pp_cxx_right_paren (cxx_pp);
      break;

    case ARROW_EXPR:
      dump_expr (TREE_OPERAND (t, 0), flags);
      pp_cxx_arrow (cxx_pp);
      break;

    case SIZEOF_EXPR:
    case ALIGNOF_EXPR:
      if (TREE_CODE (t) == SIZEOF_EXPR)
	pp_cxx_identifier (cxx_pp, "sizeof");
      else
	{
	  gcc_assert (TREE_CODE (t) == ALIGNOF_EXPR);
	  pp_cxx_identifier (cxx_pp, "__alignof__");
	}
      pp_cxx_whitespace (cxx_pp);
      pp_cxx_left_paren (cxx_pp);
      if (TYPE_P (TREE_OPERAND (t, 0)))
	dump_type (TREE_OPERAND (t, 0), flags);
      else
	dump_expr (TREE_OPERAND (t, 0), flags);
      pp_cxx_right_paren (cxx_pp);
      break;

    case REALPART_EXPR:
    case IMAGPART_EXPR:
      pp_cxx_identifier (cxx_pp, operator_name_info[TREE_CODE (t)].name);
      pp_cxx_whitespace (cxx_pp);
      dump_expr (TREE_OPERAND (t, 0), flags);
      break;

    case DEFAULT_ARG:
      pp_identifier (cxx_pp, "<unparsed>");
      break;

    case TRY_CATCH_EXPR:
    case WITH_CLEANUP_EXPR:
    case CLEANUP_POINT_EXPR:
      dump_expr (TREE_OPERAND (t, 0), flags);
      break;

    case PSEUDO_DTOR_EXPR:
      dump_expr (TREE_OPERAND (t, 2), flags);
      pp_cxx_dot (cxx_pp);
      dump_type (TREE_OPERAND (t, 0), flags);
      pp_cxx_colon_colon (cxx_pp);
      pp_cxx_complement (cxx_pp);
      dump_type (TREE_OPERAND (t, 1), flags);
      break;

    case TEMPLATE_ID_EXPR:
      dump_decl (t, flags);
      break;

    case BIND_EXPR:
    case STMT_EXPR:
    case STATEMENT_LIST:
      /* We don't yet have a way of dumping statements in a
	 human-readable format.  */
      pp_string (cxx_pp, "({...})");
      break;

    case LOOP_EXPR:
      pp_string (cxx_pp, "while (1) { ");
      dump_expr (TREE_OPERAND (t, 0), flags & ~TFF_EXPR_IN_PARENS);
      pp_cxx_right_brace (cxx_pp);
      break;

    case EXIT_EXPR:
      pp_string (cxx_pp, "if (");
      dump_expr (TREE_OPERAND (t, 0), flags & ~TFF_EXPR_IN_PARENS);
      pp_string (cxx_pp, ") break; ");
      break;

    case BASELINK:
      dump_expr (get_first_fn (t), flags & ~TFF_EXPR_IN_PARENS);
      break;

    case EMPTY_CLASS_EXPR:
      dump_type (TREE_TYPE (t), flags);
      pp_cxx_left_paren (cxx_pp);
      pp_cxx_right_paren (cxx_pp);
      break;

    case NON_DEPENDENT_EXPR:
      dump_expr (TREE_OPERAND (t, 0), flags);
      break;

      /*  This list is incomplete, but should suffice for now.
	  It is very important that `sorry' does not call
	  `report_error_function'.  That could cause an infinite loop.  */
    default:
      pp_unsupported_tree (cxx_pp, t);
      /* fall through to ERROR_MARK...  */
    case ERROR_MARK:
      pp_identifier (cxx_pp, "<expression error>");
      break;
    }
}

static void
dump_binary_op (const char *opstring, tree t, int flags)
{
  pp_cxx_left_paren (cxx_pp);
  dump_expr (TREE_OPERAND (t, 0), flags | TFF_EXPR_IN_PARENS);
  pp_cxx_whitespace (cxx_pp);
  if (opstring)
    pp_cxx_identifier (cxx_pp, opstring);
  else
    pp_identifier (cxx_pp, "<unknown operator>");
  pp_cxx_whitespace (cxx_pp);
  dump_expr (TREE_OPERAND (t, 1), flags | TFF_EXPR_IN_PARENS);
  pp_cxx_right_paren (cxx_pp);
}

static void
dump_unary_op (const char *opstring, tree t, int flags)
{
  if (flags & TFF_EXPR_IN_PARENS)
    pp_cxx_left_paren (cxx_pp);
  pp_cxx_identifier (cxx_pp, opstring);
  dump_expr (TREE_OPERAND (t, 0), flags & ~TFF_EXPR_IN_PARENS);
  if (flags & TFF_EXPR_IN_PARENS)
    pp_cxx_right_paren (cxx_pp);
}

static void
reinit_cxx_pp (void)
{
  pp_clear_output_area (cxx_pp);
  pp_base (cxx_pp)->padding = pp_none;
  pp_indentation (cxx_pp) = 0;
  pp_needs_newline (cxx_pp) = false;
  cxx_pp->enclosing_scope = 0;
}


/* Exported interface to stringifying types, exprs and decls under TFF_*
   control.  */

const char *
type_as_string (tree typ, int flags)
{
  reinit_cxx_pp ();
  dump_type (typ, flags);
  return pp_formatted_text (cxx_pp);
}

const char *
expr_as_string (tree decl, int flags)
{
  reinit_cxx_pp ();
  dump_expr (decl, flags);
  return pp_formatted_text (cxx_pp);
}

const char *
decl_as_string (tree decl, int flags)
{
  reinit_cxx_pp ();
  dump_decl (decl, flags);
  return pp_formatted_text (cxx_pp);
}

/* Generate the three forms of printable names for cxx_printable_name.  */

const char *
lang_decl_name (tree decl, int v)
{
  if (v >= 2)
    return decl_as_string (decl, TFF_DECL_SPECIFIERS);

  reinit_cxx_pp ();
  if (v == 1 && DECL_CLASS_SCOPE_P (decl))
    {
      dump_type (CP_DECL_CONTEXT (decl), TFF_PLAIN_IDENTIFIER);
      pp_cxx_colon_colon (cxx_pp);
    }

  if (TREE_CODE (decl) == FUNCTION_DECL)
    dump_function_name (decl, TFF_PLAIN_IDENTIFIER);
  else
    dump_decl (DECL_NAME (decl), TFF_PLAIN_IDENTIFIER);

  return pp_formatted_text (cxx_pp);
}

/* Return the location of a tree passed to %+ formats.  */

static location_t
location_of (tree t)
{
  if (TREE_CODE (t) == PARM_DECL && DECL_CONTEXT (t))
    t = DECL_CONTEXT (t);
  else if (TYPE_P (t))
    t = TYPE_MAIN_DECL (t);
  else if (TREE_CODE (t) == OVERLOAD)
    t = OVL_FUNCTION (t);

  return DECL_SOURCE_LOCATION (t);
}

/* Now the interfaces from error et al to dump_type et al. Each takes an
   on/off VERBOSE flag and supply the appropriate TFF_ flags to a dump_
   function.  */

static const char *
decl_to_string (tree decl, int verbose)
{
  int flags = 0;

  if (TREE_CODE (decl) == TYPE_DECL || TREE_CODE (decl) == RECORD_TYPE
      || TREE_CODE (decl) == UNION_TYPE || TREE_CODE (decl) == ENUMERAL_TYPE)
    flags = TFF_CLASS_KEY_OR_ENUM;
  if (verbose)
    flags |= TFF_DECL_SPECIFIERS;
  else if (TREE_CODE (decl) == FUNCTION_DECL)
    flags |= TFF_DECL_SPECIFIERS | TFF_RETURN_TYPE;
  flags |= TFF_TEMPLATE_HEADER;

  reinit_cxx_pp ();
  dump_decl (decl, flags);
  return pp_formatted_text (cxx_pp);
}

static const char *
expr_to_string (tree decl)
{
  reinit_cxx_pp ();
  dump_expr (decl, 0);
  return pp_formatted_text (cxx_pp);
}

static const char *
fndecl_to_string (tree fndecl, int verbose)
{
  int flags;

  flags = TFF_EXCEPTION_SPECIFICATION | TFF_DECL_SPECIFIERS
    | TFF_TEMPLATE_HEADER;
  if (verbose)
    flags |= TFF_FUNCTION_DEFAULT_ARGUMENTS;
  reinit_cxx_pp ();
  dump_decl (fndecl, flags);
  return pp_formatted_text (cxx_pp);
}


static const char *
code_to_string (enum tree_code c)
{
  return tree_code_name [c];
}

const char *
language_to_string (enum languages c)
{
  switch (c)
    {
    case lang_c:
      return "C";

    case lang_cplusplus:
      return "C++";

    case lang_java:
      return "Java";

    default:
      gcc_unreachable ();
    }
  return NULL;
}

/* Return the proper printed version of a parameter to a C++ function.  */

static const char *
parm_to_string (int p)
{
  reinit_cxx_pp ();
  if (p < 0)
    pp_string (cxx_pp, "'this'");
  else
    pp_decimal_int (cxx_pp, p + 1);
  return pp_formatted_text (cxx_pp);
}

static const char *
op_to_string (enum tree_code p)
{
  tree id = operator_name_info[(int) p].identifier;
  return id ? IDENTIFIER_POINTER (id) : "<unknown>";
}

static const char *
type_to_string (tree typ, int verbose)
{
  int flags = 0;
  if (verbose)
    flags |= TFF_CLASS_KEY_OR_ENUM;
  flags |= TFF_TEMPLATE_HEADER;

  reinit_cxx_pp ();
  dump_type (typ, flags);
  return pp_formatted_text (cxx_pp);
}

static const char *
assop_to_string (enum tree_code p)
{
  tree id = assignment_operator_name_info[(int) p].identifier;
  return id ? IDENTIFIER_POINTER (id) : "{unknown}";
}

static const char *
args_to_string (tree p, int verbose)
{
  int flags = 0;
  if (verbose)
    flags |= TFF_CLASS_KEY_OR_ENUM;

  if (p == NULL_TREE)
    return "";

  if (TYPE_P (TREE_VALUE (p)))
    return type_as_string (p, flags);

  reinit_cxx_pp ();
  for (; p; p = TREE_CHAIN (p))
    {
      if (TREE_VALUE (p) == null_node)
	pp_cxx_identifier (cxx_pp, "NULL");
      else
	dump_type (error_type (TREE_VALUE (p)), flags);
      if (TREE_CHAIN (p))
	pp_separate_with_comma (cxx_pp);
    }
  return pp_formatted_text (cxx_pp);
}

static const char *
cv_to_string (tree p, int v)
{
  reinit_cxx_pp ();
  pp_base (cxx_pp)->padding = v ? pp_before : pp_none;
  pp_cxx_cv_qualifier_seq (cxx_pp, p);
  return pp_formatted_text (cxx_pp);
}

/* Langhook for print_error_function.  */
void
cxx_print_error_function (diagnostic_context *context, const char *file)
{
  lhd_print_error_function (context, file);
  pp_base_set_prefix (context->printer, file);
  maybe_print_instantiation_context (context);
}

static void
cp_diagnostic_starter (diagnostic_context *context,
		       diagnostic_info *diagnostic)
{
  diagnostic_report_current_module (context);
  cp_print_error_function (context, diagnostic);
  maybe_print_instantiation_context (context);
  pp_base_set_prefix (context->printer, diagnostic_build_prefix (diagnostic));
}

static void
cp_diagnostic_finalizer (diagnostic_context *context,
			 diagnostic_info *diagnostic ATTRIBUTE_UNUSED)
{
  pp_base_destroy_prefix (context->printer);
}

/* Print current function onto BUFFER, in the process of reporting
   a diagnostic message.  Called from cp_diagnostic_starter.  */
static void
cp_print_error_function (diagnostic_context *context,
			 diagnostic_info *diagnostic)
{
  if (diagnostic_last_function_changed (context))
    {
      const char *old_prefix = context->printer->prefix;
      const char *file = LOCATION_FILE (diagnostic->location);
      char *new_prefix = file ? file_name_as_prefix (file) : NULL;

      pp_base_set_prefix (context->printer, new_prefix);

      if (current_function_decl == NULL)
	pp_base_string (context->printer, "At global scope:");
      else
	pp_printf (context->printer, "In %s %qs:",
		   function_category (current_function_decl),
		   cxx_printable_name (current_function_decl, 2));
      pp_base_newline (context->printer);

      diagnostic_set_last_function (context);
      pp_base_destroy_prefix (context->printer);
      context->printer->prefix = old_prefix;
    }
}

/* Returns a description of FUNCTION using standard terminology.  */
static const char *
function_category (tree fn)
{
  if (DECL_FUNCTION_MEMBER_P (fn))
    {
      if (DECL_STATIC_FUNCTION_P (fn))
	return "static member function";
      else if (DECL_COPY_CONSTRUCTOR_P (fn))
	return "copy constructor";
      else if (DECL_CONSTRUCTOR_P (fn))
	return "constructor";
      else if (DECL_DESTRUCTOR_P (fn))
	return "destructor";
      else
	return "member function";
    }
  else
    return "function";
}

/* Report the full context of a current template instantiation,
   onto BUFFER.  */
static void
print_instantiation_full_context (diagnostic_context *context)
{
  tree p = current_instantiation ();
  location_t location = input_location;

  if (p)
    {
      if (current_function_decl != TINST_DECL (p)
	  && current_function_decl != NULL_TREE)
	/* We can get here during the processing of some synthesized
	   method.  Then, TINST_DECL (p) will be the function that's causing
	   the synthesis.  */
	;
      else
	{
	  if (current_function_decl == TINST_DECL (p))
	    /* Avoid redundancy with the "In function" line.  */;
	  else
	    pp_verbatim (context->printer,
			 "%s: In instantiation of %qs:\n",
			 LOCATION_FILE (location),
			 decl_as_string (TINST_DECL (p),
					 TFF_DECL_SPECIFIERS | TFF_RETURN_TYPE));

	  location = TINST_LOCATION (p);
	  p = TREE_CHAIN (p);
	}
    }

  print_instantiation_partial_context (context, p, location);
}

/* Same as above but less verbose.  */
static void
print_instantiation_partial_context (diagnostic_context *context,
				     tree t, location_t loc)
{
  expanded_location xloc;
  for (; ; t = TREE_CHAIN (t))
    {
      xloc = expand_location (loc);
      if (t == NULL_TREE)
	break;
      pp_verbatim (context->printer, "%s:%d:   instantiated from %qs\n",
		   xloc.file, xloc.line,
		   decl_as_string (TINST_DECL (t),
				   TFF_DECL_SPECIFIERS | TFF_RETURN_TYPE));
      loc = TINST_LOCATION (t);
    }
  pp_verbatim (context->printer, "%s:%d:   instantiated from here",
	       xloc.file, xloc.line);
  pp_base_newline (context->printer);
}

/* Called from cp_thing to print the template context for an error.  */
static void
maybe_print_instantiation_context (diagnostic_context *context)
{
  if (!problematic_instantiation_changed () || current_instantiation () == 0)
    return;

  record_last_problematic_instantiation ();
  print_instantiation_full_context (context);
}

/* Report the bare minimum context of a template instantiation.  */
void
print_instantiation_context (void)
{
  print_instantiation_partial_context
    (global_dc, current_instantiation (), input_location);
  diagnostic_flush_buffer (global_dc);
}

/* Called from output_format -- during diagnostic message processing --
   to handle C++ specific format specifier with the following meanings:
   %A   function argument-list.
   %C	tree code.
   %D   declaration.
   %E   expression.
   %F   function declaration.
   %L	language as used in extern "lang".
   %O	binary operator.
   %P   function parameter whose position is indicated by an integer.
   %Q	assignment operator.
   %T   type.
   %V   cv-qualifier.  */
static bool
cp_printer (pretty_printer *pp, text_info *text, const char *spec,
	    int precision, bool wide, bool set_locus, bool verbose)
{
  const char *result;
  tree t = NULL;
#define next_tree    (t = va_arg (*text->args_ptr, tree))
#define next_tcode   va_arg (*text->args_ptr, enum tree_code)
#define next_lang    va_arg (*text->args_ptr, enum languages)
#define next_int     va_arg (*text->args_ptr, int)

  if (precision != 0 || wide)
    return false;

  if (text->locus == NULL)
    set_locus = false;

  switch (*spec)
    {
    case 'A': result = args_to_string (next_tree, verbose);	break;
    case 'C': result = code_to_string (next_tcode);		break;
    case 'D':
      {
	tree temp = next_tree;
	if (DECL_P (temp)
	    && DECL_DEBUG_EXPR_IS_FROM (temp) && DECL_DEBUG_EXPR (temp))
	  {
	    temp = DECL_DEBUG_EXPR (temp);
	    if (!DECL_P (temp))
	      {
		result = expr_to_string (temp);
		break;
	      }
	  }
	result = decl_to_string (temp, verbose);
      }
      break;
    case 'E': result = expr_to_string (next_tree);		break;
    case 'F': result = fndecl_to_string (next_tree, verbose);	break;
    case 'L': result = language_to_string (next_lang);		break;
    case 'O': result = op_to_string (next_tcode);		break;
    case 'P': result = parm_to_string (next_int);		break;
    case 'Q': result = assop_to_string (next_tcode);		break;
    case 'T': result = type_to_string (next_tree, verbose);	break;
    case 'V': result = cv_to_string (next_tree, verbose);	break;

    default:
      return false;
    }

  pp_base_string (pp, result);
  if (set_locus && t != NULL)
    *text->locus = location_of (t);
  return true;
#undef next_tree
#undef next_tcode
#undef next_lang
#undef next_int
}

/* Callback from cpp_error for PFILE to print diagnostics arising from
   interpreting strings.  The diagnostic is of type LEVEL; MSG is the
   translated message and AP the arguments.  */

void
cp_cpp_error (cpp_reader *pfile ATTRIBUTE_UNUSED, int level,
	      const char *msg, va_list *ap)
{
  diagnostic_info diagnostic;
  diagnostic_t dlevel;
  switch (level)
    {
    case CPP_DL_WARNING:
    case CPP_DL_WARNING_SYSHDR:
      dlevel = DK_WARNING;
      break;
    case CPP_DL_PEDWARN:
      dlevel = pedantic_error_kind ();
      break;
    case CPP_DL_ERROR:
      dlevel = DK_ERROR;
      break;
    case CPP_DL_ICE:
      dlevel = DK_ICE;
      break;
    default:
      gcc_unreachable ();
    }
  diagnostic_set_info_translated (&diagnostic, msg, ap,
				  input_location, dlevel);
  report_diagnostic (&diagnostic);
}
