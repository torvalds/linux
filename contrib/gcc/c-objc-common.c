/* Some code common to C and ObjC front ends.
   Copyright (C) 2001, 2002, 2003, 2004, 2005 Free Software Foundation, Inc.

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
#include "tree.h"
#include "rtl.h"
#include "insn-config.h"
#include "integrate.h"
#include "c-tree.h"
#include "c-pretty-print.h"
#include "function.h"
#include "flags.h"
#include "toplev.h"
#include "diagnostic.h"
#include "tree-inline.h"
#include "varray.h"
#include "ggc.h"
#include "langhooks.h"
#include "tree-mudflap.h"
#include "target.h"
#include "c-objc-common.h"

static bool c_tree_printer (pretty_printer *, text_info *, const char *,
			    int, bool, bool, bool);

bool
c_missing_noreturn_ok_p (tree decl)
{
  /* A missing noreturn is not ok for freestanding implementations and
     ok for the `main' function in hosted implementations.  */
  return flag_hosted && MAIN_NAME_P (DECL_ASSEMBLER_NAME (decl));
}

/* We want to inline `extern inline' functions even if this would
   violate inlining limits.  Some glibc and linux constructs depend on
   such functions always being inlined when optimizing.  */

int
c_disregard_inline_limits (tree fn)
{
  if (lookup_attribute ("always_inline", DECL_ATTRIBUTES (fn)) != NULL)
    return 1;

  return (!flag_really_no_inline && DECL_DECLARED_INLINE_P (fn)
	  && DECL_EXTERNAL (fn));
}

int
c_cannot_inline_tree_fn (tree *fnp)
{
  tree fn = *fnp;
  bool do_warning = (warn_inline
		     && DECL_INLINE (fn)
		     && DECL_DECLARED_INLINE_P (fn)
		     && !DECL_IN_SYSTEM_HEADER (fn));

  if (flag_really_no_inline
      && lookup_attribute ("always_inline", DECL_ATTRIBUTES (fn)) == NULL)
    {
      if (do_warning)
	warning (OPT_Winline, "function %q+F can never be inlined because it "
		 "is suppressed using -fno-inline", fn);
      goto cannot_inline;
    }

  /* Don't auto-inline anything that might not be bound within
     this unit of translation.  */
  if (!DECL_DECLARED_INLINE_P (fn) && !targetm.binds_local_p (fn))
    {
      if (do_warning)
	warning (OPT_Winline, "function %q+F can never be inlined because it "
		 "might not be bound within this unit of translation", fn);
      goto cannot_inline;
    }

  if (!function_attribute_inlinable_p (fn))
    {
      if (do_warning)
	warning (OPT_Winline, "function %q+F can never be inlined because it "
		 "uses attributes conflicting with inlining", fn);
      goto cannot_inline;
    }

  return 0;

 cannot_inline:
  DECL_UNINLINABLE (fn) = 1;
  return 1;
}

/* Called from check_global_declarations.  */

bool
c_warn_unused_global_decl (tree decl)
{
  if (TREE_CODE (decl) == FUNCTION_DECL && DECL_DECLARED_INLINE_P (decl))
    return false;
  if (DECL_IN_SYSTEM_HEADER (decl))
    return false;

  return true;
}

/* Initialization common to C and Objective-C front ends.  */
bool
c_objc_common_init (void)
{
  c_init_decl_processing ();

  if (c_common_init () == false)
    return false;

  /* These were not defined in the Objective-C front end, but I'm
     putting them here anyway.  The diagnostic format decoder might
     want an enhanced ObjC implementation.  */
  diagnostic_format_decoder (global_dc) = &c_tree_printer;

  /* If still unspecified, make it match -std=c99
     (allowing for -pedantic-errors).  */
  if (mesg_implicit_function_declaration < 0)
    {
      if (flag_isoc99)
	mesg_implicit_function_declaration = flag_pedantic_errors ? 2 : 1;
      else
	mesg_implicit_function_declaration = 0;
    }

  return true;
}

/* Called during diagnostic message formatting process to print a
   source-level entity onto BUFFER.  The meaning of the format specifiers
   is as follows:
   %D: a general decl,
   %E: an identifier or expression,
   %F: a function declaration,
   %T: a type.

   These format specifiers form a subset of the format specifiers set used
   by the C++ front-end.
   Please notice when called, the `%' part was already skipped by the
   diagnostic machinery.  */
static bool
c_tree_printer (pretty_printer *pp, text_info *text, const char *spec,
		int precision, bool wide, bool set_locus, bool hash)
{
  tree t = va_arg (*text->args_ptr, tree);
  tree name;
  const char *n = "({anonymous})";
  c_pretty_printer *cpp = (c_pretty_printer *) pp;
  pp->padding = pp_none;

  if (precision != 0 || wide || hash)
    return false;

  if (set_locus && text->locus)
    *text->locus = DECL_SOURCE_LOCATION (t);

  switch (*spec)
    {
    case 'D':
      if (DECL_DEBUG_EXPR_IS_FROM (t) && DECL_DEBUG_EXPR (t))
	{
	  t = DECL_DEBUG_EXPR (t);
	  if (!DECL_P (t))
	    {
	      pp_c_expression (cpp, t);
	      return true;
	    }
	}
      /* FALLTHRU */

    case 'F':
      if (DECL_NAME (t))
	n = lang_hooks.decl_printable_name (t, 2);
      break;

    case 'T':
      gcc_assert (TYPE_P (t));
      name = TYPE_NAME (t);

      if (name && TREE_CODE (name) == TYPE_DECL)
	{
	  if (DECL_NAME (name))
	    pp_string (cpp, lang_hooks.decl_printable_name (name, 2));
	  else
	    pp_type_id (cpp, t);
	  return true;
	}
      else
	{
	  pp_type_id (cpp, t);
	  return true;
	}
      break;

    case 'E':
      if (TREE_CODE (t) == IDENTIFIER_NODE)
	n = IDENTIFIER_POINTER (t);
      else
	{
	  pp_expression (cpp, t);
	  return true;
	}
      break;

    default:
      return false;
    }

  pp_string (cpp, n);
  return true;
}

/* In C and ObjC, all decls have "C" linkage.  */
bool
has_c_linkage (tree decl ATTRIBUTE_UNUSED)
{
  return true;
}

void
c_initialize_diagnostics (diagnostic_context *context)
{
  pretty_printer *base = context->printer;
  c_pretty_printer *pp = XNEW (c_pretty_printer);
  memcpy (pp_base (pp), base, sizeof (pretty_printer));
  pp_c_pretty_printer_init (pp);
  context->printer = (pretty_printer *) pp;

  /* It is safe to free this object because it was previously XNEW()'d.  */
  XDELETE (base);
}

int
c_types_compatible_p (tree x, tree y)
{
    return comptypes (TYPE_MAIN_VARIANT (x), TYPE_MAIN_VARIANT (y));
}

/* Determine if the type is a vla type for the backend.  */

bool
c_vla_unspec_p (tree x, tree fn ATTRIBUTE_UNUSED)
{
  return c_vla_type_p (x);
}
