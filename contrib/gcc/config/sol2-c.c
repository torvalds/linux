/* Solaris support needed only by C/C++ frontends.
   Copyright (C) 2004, 2005  Free Software Foundation, Inc.
   Contributed by CodeSourcery, LLC.

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
#include "tree.h"
#include "tm.h"
#include "tm_p.h"
#include "toplev.h"

#include "c-format.h"
#include "intl.h"

#include "cpplib.h"
#include "c-pragma.h"
#include "c-common.h"

/* cmn_err only accepts "l" and "ll".  */
static const format_length_info cmn_err_length_specs[] =
{
  { "l", FMT_LEN_l, STD_C89, "ll", FMT_LEN_ll, STD_C89 },
  { NULL, 0, 0, NULL, 0, 0 }
};

static const format_flag_spec cmn_err_flag_specs[] =
{
  { 'w',  0, 0, N_("field width"),     N_("field width in printf format"),     STD_C89 },
  { 'L',  0, 0, N_("length modifier"), N_("length modifier in printf format"), STD_C89 },
  { 0, 0, 0, NULL, NULL, 0 }
};


static const format_flag_pair cmn_err_flag_pairs[] =
{
  { 0, 0, 0, 0 }
};

static const format_char_info bitfield_string_type =
  { "b",   1, STD_C89, { T89_C,   BADLEN,  BADLEN,  BADLEN,  BADLEN,  BADLEN,  BADLEN,  BADLEN,  BADLEN  }, "",   "cR", NULL };

static const format_char_info cmn_err_char_table[] =
{
  /* C89 conversion specifiers.  */
  { "dD",  0, STD_C89, { T89_I,   BADLEN,  BADLEN,  T89_L,   T9L_LL,  BADLEN,  BADLEN,  BADLEN,  BADLEN  }, "w",  "",   NULL },
  { "oOxX",0, STD_C89, { T89_UI,  BADLEN,  BADLEN,  T89_UL,  T9L_ULL, BADLEN,  BADLEN,  BADLEN,  BADLEN  }, "w",  "",   NULL },
  { "u",   0, STD_C89, { T89_UI,  BADLEN,  BADLEN,  T89_UL,  T9L_ULL, BADLEN,  BADLEN,  BADLEN,  BADLEN  }, "w",  "",   NULL },
  { "c",   0, STD_C89, { T89_C,   BADLEN,  BADLEN,  BADLEN,  BADLEN,  BADLEN,  BADLEN,  BADLEN,  BADLEN  }, "w",  "",   NULL },
  { "p",   1, STD_C89, { T89_V,   BADLEN,  BADLEN,  BADLEN,  BADLEN,  BADLEN,  BADLEN,  BADLEN,  BADLEN  }, "w", "c",  NULL },
  { "s",   1, STD_C89, { T89_C,   BADLEN,  BADLEN,  BADLEN,  BADLEN,  BADLEN,  BADLEN,  BADLEN,  BADLEN  }, "w",  "cR", NULL },
  { "b",   0, STD_C89, { T89_I,   BADLEN,  BADLEN,  BADLEN,  BADLEN,  BADLEN,  BADLEN,  BADLEN,  BADLEN  }, "w",   "",   &bitfield_string_type },
  { NULL,  0, 0, NOLENGTHS, NULL, NULL, NULL }
};

const format_kind_info solaris_format_types[] = {
  { "cmn_err",  cmn_err_length_specs,  cmn_err_char_table, "", NULL,
    cmn_err_flag_specs, cmn_err_flag_pairs,
    FMT_FLAG_ARG_CONVERT|FMT_FLAG_EMPTY_PREC_OK,
    'w', 0, 0, 0, 'L',
    &integer_type_node, &integer_type_node
  }
};

/* Handle #pragma align ALIGNMENT (VAR [, VAR]...)  */

static void
solaris_pragma_align (cpp_reader *pfile ATTRIBUTE_UNUSED)
{
  tree t, x;
  enum cpp_ttype ttype;
  HOST_WIDE_INT low;

  if (pragma_lex (&x) != CPP_NUMBER
      || pragma_lex (&t) != CPP_OPEN_PAREN)
    {
      warning (0, "malformed %<#pragma align%>, ignoring");
      return;
    }

  low = TREE_INT_CST_LOW (x);
  if (TREE_INT_CST_HIGH (x) != 0
      || (low != 1 && low != 2 && low != 4 && low != 8 && low != 16
	  && low != 32 && low != 64 && low != 128))
    {
      warning (0, "invalid alignment for %<#pragma align%>, ignoring");
      return;
    }

  ttype = pragma_lex (&t);
  if (ttype != CPP_NAME)
    {
      warning (0, "malformed %<#pragma align%>, ignoring");
      return;
    }

  while (1)
    {
      tree decl = identifier_global_value (t);
      if (decl && DECL_P (decl))
	warning (0, "%<#pragma align%> must appear before the declaration of "
		 "%D, ignoring", decl);
      else
	solaris_pending_aligns = tree_cons (t, build_tree_list (NULL, x),
					    solaris_pending_aligns);

      ttype = pragma_lex (&t);
      if (ttype == CPP_COMMA)
	{
	  ttype = pragma_lex (&t);
	  if (ttype != CPP_NAME)
	    {
	      warning (0, "malformed %<#pragma align%>");
	      return;
	    }
	}
      else if (ttype == CPP_CLOSE_PAREN)
	{
	  if (pragma_lex (&t) != CPP_EOF)
	    warning (0, "junk at end of %<#pragma align%>");
	  return;
	}
      else
	{
	  warning (0, "malformed %<#pragma align%>");
	  return;
	}
    }
}

/* Handle #pragma init (function [, function]...)  */

static void
solaris_pragma_init (cpp_reader *pfile ATTRIBUTE_UNUSED)
{
  tree t;
  enum cpp_ttype ttype;

  if (pragma_lex (&t) != CPP_OPEN_PAREN)
    {
      warning (0, "malformed %<#pragma init%>, ignoring");
      return;
    }

  ttype = pragma_lex (&t);
  if (ttype != CPP_NAME)
    {
      warning (0, "malformed %<#pragma init%>, ignoring");
      return;
    }

  while (1)
    {
      tree decl = identifier_global_value (t);
      if (decl && DECL_P (decl))
	{
	  tree init_list = build_tree_list (get_identifier ("init"),
					    NULL);
	  tree attrs = tree_cons (get_identifier ("used"), NULL, init_list);
	  decl_attributes (&decl, attrs, 0);
	}
      else
	solaris_pending_inits = tree_cons (t, NULL, solaris_pending_inits);

      ttype = pragma_lex (&t);
      if (ttype == CPP_COMMA)
	{
	  ttype = pragma_lex (&t);
	  if (ttype != CPP_NAME)
	    {
	      warning (0, "malformed %<#pragma init%>");
	      return;
	    }
	}
      else if (ttype == CPP_CLOSE_PAREN)
	{
	  if (pragma_lex (&t) != CPP_EOF)
	    warning (0, "junk at end of %<#pragma init%>");
	  return;
	}
      else
	{
	  warning (0, "malformed %<#pragma init%>");
	  return;
	}
    }
}

/* Handle #pragma fini (function [, function]...)  */

static void
solaris_pragma_fini (cpp_reader *pfile ATTRIBUTE_UNUSED)
{
  tree t;
  enum cpp_ttype ttype;

  if (pragma_lex (&t) != CPP_OPEN_PAREN)
    {
      warning (0, "malformed %<#pragma fini%>, ignoring");
      return;
    }

  ttype = pragma_lex (&t);
  if (ttype != CPP_NAME)
    {
      warning (0, "malformed %<#pragma fini%>, ignoring");
      return;
    }

  while (1)
    {
      tree decl = identifier_global_value (t);
      if (decl && DECL_P (decl))
	{
	  tree fini_list = build_tree_list (get_identifier ("fini"),
					    NULL);
	  tree attrs = tree_cons (get_identifier ("used"), NULL, fini_list);
	  decl_attributes (&decl, attrs, 0);
	}
      else
	solaris_pending_finis = tree_cons (t, NULL, solaris_pending_finis);

      ttype = pragma_lex (&t);
      if (ttype == CPP_COMMA)
	{
	  ttype = pragma_lex (&t);
	  if (ttype != CPP_NAME)
	    {
	      warning (0, "malformed %<#pragma fini%>");
	      return;
	    }
	}
      else if (ttype == CPP_CLOSE_PAREN)
	{
	  if (pragma_lex (&t) != CPP_EOF)
	    warning (0, "junk at end of %<#pragma fini%>");
	  return;
	}
      else
	{
	  warning (0, "malformed %<#pragma fini%>");
	  return;
	}
    }
}

/* Register Solaris-specific #pragma directives.  */

void
solaris_register_pragmas (void)
{
  c_register_pragma_with_expansion (0, "align", solaris_pragma_align);
  c_register_pragma (0, "init", solaris_pragma_init);
  c_register_pragma (0, "fini", solaris_pragma_fini);
}
