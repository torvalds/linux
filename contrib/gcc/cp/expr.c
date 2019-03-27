/* Convert language-specific tree expression to rtl instructions,
   for GNU compiler.
   Copyright (C) 1988, 1992, 1993, 1994, 1995, 1996, 1997, 1998,
   2000, 2001, 2002, 2003, 2004 Free Software Foundation, Inc.

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
#include "rtl.h"
#include "tree.h"
#include "flags.h"
#include "expr.h"
#include "cp-tree.h"
#include "toplev.h"
#include "except.h"
#include "tm_p.h"

/* Hook used by output_constant to expand language-specific
   constants.  */

tree
cplus_expand_constant (tree cst)
{
  switch (TREE_CODE (cst))
    {
    case PTRMEM_CST:
      {
	tree type = TREE_TYPE (cst);
	tree member;

	/* Find the member.  */
	member = PTRMEM_CST_MEMBER (cst);

	if (TREE_CODE (member) == FIELD_DECL)
	  {
	    /* Find the offset for the field.  */
	    cst = byte_position (member);
	    while (!same_type_p (DECL_CONTEXT (member),
				 TYPE_PTRMEM_CLASS_TYPE (type)))
	      {
		/* The MEMBER must have been nestled within an
		   anonymous aggregate contained in TYPE.  Find the
		   anonymous aggregate.  */
		member = lookup_anon_field (TYPE_PTRMEM_CLASS_TYPE (type),
					    DECL_CONTEXT (member));
		cst = size_binop (PLUS_EXPR, cst, byte_position (member));
	      }
	    cst = fold (build_nop (type, cst));
	  }
	else
	  {
	    tree delta;
	    tree pfn;

	    expand_ptrmemfunc_cst (cst, &delta, &pfn);
	    cst = build_ptrmemfunc1 (type, delta, pfn);
	  }
      }
      break;

    default:
      /* There's nothing to do.  */
      break;
    }

  return cst;
}

/* Hook used by expand_expr to expand language-specific tree codes.  */
/* ??? The only thing that should be here are things needed to expand
   constant initializers; everything else should be handled by the
   gimplification routines.  Are EMPTY_CLASS_EXPR or BASELINK needed?  */

rtx
cxx_expand_expr (tree exp, rtx target, enum machine_mode tmode, int modifier,
		 rtx *alt_rtl)
{
  tree type = TREE_TYPE (exp);
  enum machine_mode mode = TYPE_MODE (type);
  enum tree_code code = TREE_CODE (exp);

  /* No sense saving up arithmetic to be done
     if it's all in the wrong mode to form part of an address.
     And force_operand won't know whether to sign-extend or zero-extend.  */

  if (mode != Pmode && modifier == EXPAND_SUM)
    modifier = EXPAND_NORMAL;

  switch (code)
    {
    case PTRMEM_CST:
      return expand_expr (cplus_expand_constant (exp),
			  target, tmode, modifier);

    case OFFSET_REF:
      /* Offset refs should not make it through to here.  */
      gcc_unreachable ();

    case EMPTY_CLASS_EXPR:
      /* We don't need to generate any code for an empty class.  */
      return const0_rtx;

    case BASELINK:
      return expand_expr (BASELINK_FUNCTIONS (exp), target, tmode,
			  modifier);

    default:
      return c_expand_expr (exp, target, tmode, modifier, alt_rtl);
    }
}
