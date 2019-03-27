/* General Solaris system support.
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
#include "rtl.h"
#include "tm_p.h"
#include "toplev.h"
#include "ggc.h"

tree solaris_pending_aligns, solaris_pending_inits, solaris_pending_finis;

/* Attach any pending attributes for DECL to the list in *ATTRIBUTES.
   Pending attributes come from #pragma or _Pragma, so this code is
   only useful in the C family front ends, but it is included in
   all languages to avoid changing the target machine initializer
   depending on the language.  */

void
solaris_insert_attributes (tree decl, tree *attributes)
{
  tree *x, next;

  if (solaris_pending_aligns != NULL && TREE_CODE (decl) == VAR_DECL)
    for (x = &solaris_pending_aligns; *x; x = &TREE_CHAIN (*x))
      {
	tree name = TREE_PURPOSE (*x);
	tree value = TREE_VALUE (*x);
	if (DECL_NAME (decl) == name)
	  {
	    if (lookup_attribute ("aligned", DECL_ATTRIBUTES (decl))
		|| lookup_attribute ("aligned", *attributes))
	      warning (0, "ignoring %<#pragma align%> for explicitly "
		       "aligned %q+D", decl);
	    else
	      *attributes = tree_cons (get_identifier ("aligned"), value,
				       *attributes);
	    next = TREE_CHAIN (*x);
	    ggc_free (*x);
	    *x = next;
	    break;
	  }
      }

  if (solaris_pending_inits != NULL && TREE_CODE (decl) == FUNCTION_DECL)
    for (x = &solaris_pending_inits; *x; x = &TREE_CHAIN (*x))
      {
	tree name = TREE_PURPOSE (*x);
	if (DECL_NAME (decl) == name)
	  {
	    *attributes = tree_cons (get_identifier ("init"), NULL,
				     *attributes);
	    *attributes = tree_cons (get_identifier ("used"), NULL,
				     *attributes);
	    next = TREE_CHAIN (*x);
	    ggc_free (*x);
	    *x = next;
	    break;
	  }
      }

  if (solaris_pending_finis != NULL && TREE_CODE (decl) == FUNCTION_DECL)
    for (x = &solaris_pending_finis; *x; x = &TREE_CHAIN (*x))
      {
	tree name = TREE_PURPOSE (*x);
	if (DECL_NAME (decl) == name)
	  {
	    *attributes = tree_cons (get_identifier ("fini"), NULL,
				     *attributes);
	    *attributes = tree_cons (get_identifier ("used"), NULL,
				     *attributes);
	    next = TREE_CHAIN (*x);
	    ggc_free (*x);
	    *x = next;
	    break;
	  }
      }
}

/* Output initializer or finalizer entries for DECL to FILE.  */

void
solaris_output_init_fini (FILE *file, tree decl)
{
  if (lookup_attribute ("init", DECL_ATTRIBUTES (decl)))
    {
      fprintf (file, "\t.pushsection\t\".init\"\n");
      ASM_OUTPUT_CALL (file, decl);
      fprintf (file, "\t.popsection\n");
    }

  if (lookup_attribute ("fini", DECL_ATTRIBUTES (decl)))
    {
      fprintf (file, "\t.pushsection\t\".fini\"\n");
      ASM_OUTPUT_CALL (file, decl);
      fprintf (file, "\t.popsection\n");
    }
}

