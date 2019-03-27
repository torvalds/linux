/* Demangler component interface functions.
   Copyright (C) 2004 Free Software Foundation, Inc.
   Written by Ian Lance Taylor <ian@wasabisystems.com>.

   This file is part of the libiberty library, which is part of GCC.

   This file is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   In addition to the permissions in the GNU General Public License, the
   Free Software Foundation gives you unlimited permission to link the
   compiled version of this file into combinations with other programs,
   and to distribute those combinations without any restriction coming
   from the use of this file.  (The General Public License restrictions
   do apply in other respects; for example, they cover modification of
   the file, and distribution when not linked into a combined
   executable.)

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin Street - Fifth Floor, Boston, MA 02110-1301, USA. 
*/

/* This file implements a few interface functions which are provided
   for use with struct demangle_component trees.  These functions are
   declared in demangle.h.  These functions are closely tied to the
   demangler code in cp-demangle.c, and other interface functions can
   be found in that file.  We put these functions in a separate file
   because they are not needed by the demangler, and so we avoid
   having them pulled in by programs which only need the
   demangler.  */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif
#ifdef HAVE_STRING_H
#include <string.h>
#endif

#include "ansidecl.h"
#include "libiberty.h"
#include "demangle.h"
#include "cp-demangle.h"

/* Fill in most component types.  */

int
cplus_demangle_fill_component (struct demangle_component *p,
                               enum demangle_component_type type,
                               struct demangle_component *left,
                                struct demangle_component *right)
{
  if (p == NULL)
    return 0;
  switch (type)
    {
    case DEMANGLE_COMPONENT_QUAL_NAME:
    case DEMANGLE_COMPONENT_LOCAL_NAME:
    case DEMANGLE_COMPONENT_TYPED_NAME:
    case DEMANGLE_COMPONENT_TEMPLATE:
    case DEMANGLE_COMPONENT_CONSTRUCTION_VTABLE:
    case DEMANGLE_COMPONENT_VENDOR_TYPE_QUAL:
    case DEMANGLE_COMPONENT_FUNCTION_TYPE:
    case DEMANGLE_COMPONENT_ARRAY_TYPE:
    case DEMANGLE_COMPONENT_PTRMEM_TYPE:
    case DEMANGLE_COMPONENT_ARGLIST:
    case DEMANGLE_COMPONENT_TEMPLATE_ARGLIST:
    case DEMANGLE_COMPONENT_UNARY:
    case DEMANGLE_COMPONENT_BINARY:
    case DEMANGLE_COMPONENT_BINARY_ARGS:
    case DEMANGLE_COMPONENT_TRINARY:
    case DEMANGLE_COMPONENT_TRINARY_ARG1:
    case DEMANGLE_COMPONENT_TRINARY_ARG2:
    case DEMANGLE_COMPONENT_LITERAL:
    case DEMANGLE_COMPONENT_LITERAL_NEG:
      break;

      /* These component types only have one subtree.  */
    case DEMANGLE_COMPONENT_VTABLE:
    case DEMANGLE_COMPONENT_VTT:
    case DEMANGLE_COMPONENT_TYPEINFO:
    case DEMANGLE_COMPONENT_TYPEINFO_NAME:
    case DEMANGLE_COMPONENT_TYPEINFO_FN:
    case DEMANGLE_COMPONENT_THUNK:
    case DEMANGLE_COMPONENT_VIRTUAL_THUNK:
    case DEMANGLE_COMPONENT_COVARIANT_THUNK:
    case DEMANGLE_COMPONENT_JAVA_CLASS:
    case DEMANGLE_COMPONENT_GUARD:
    case DEMANGLE_COMPONENT_REFTEMP:
    case DEMANGLE_COMPONENT_RESTRICT:
    case DEMANGLE_COMPONENT_VOLATILE:
    case DEMANGLE_COMPONENT_CONST:
    case DEMANGLE_COMPONENT_RESTRICT_THIS:
    case DEMANGLE_COMPONENT_VOLATILE_THIS:
    case DEMANGLE_COMPONENT_CONST_THIS:
    case DEMANGLE_COMPONENT_POINTER:
    case DEMANGLE_COMPONENT_REFERENCE:
    case DEMANGLE_COMPONENT_COMPLEX:
    case DEMANGLE_COMPONENT_IMAGINARY:
    case DEMANGLE_COMPONENT_VENDOR_TYPE:
    case DEMANGLE_COMPONENT_CAST:
      if (right != NULL)
	return 0;
      break;

    default:
      /* Other types do not use subtrees.  */
      return 0;
    }

  p->type = type;
  p->u.s_binary.left = left;
  p->u.s_binary.right = right;

  return 1;
}

/* Fill in a DEMANGLE_COMPONENT_BUILTIN_TYPE.  */

int
cplus_demangle_fill_builtin_type (struct demangle_component *p,
                                  const char *type_name)
{
  int len;
  unsigned int i;

  if (p == NULL || type_name == NULL)
    return 0;
  len = strlen (type_name);
  for (i = 0; i < D_BUILTIN_TYPE_COUNT; ++i)
    {
      if (len == cplus_demangle_builtin_types[i].len
	  && strcmp (type_name, cplus_demangle_builtin_types[i].name) == 0)
	{
	  p->type = DEMANGLE_COMPONENT_BUILTIN_TYPE;
	  p->u.s_builtin.type = &cplus_demangle_builtin_types[i];
	  return 1;
	}
    }
  return 0;
}

/* Fill in a DEMANGLE_COMPONENT_OPERATOR.  */

int
cplus_demangle_fill_operator (struct demangle_component *p,
                              const char *opname, int args)
{
  int len;
  unsigned int i;

  if (p == NULL || opname == NULL)
    return 0;
  len = strlen (opname);
  for (i = 0; cplus_demangle_operators[i].name != NULL; ++i)
    {
      if (len == cplus_demangle_operators[i].len
	  && args == cplus_demangle_operators[i].args
	  && strcmp (opname, cplus_demangle_operators[i].name) == 0)
	{
	  p->type = DEMANGLE_COMPONENT_OPERATOR;
	  p->u.s_operator.op = &cplus_demangle_operators[i];
	  return 1;
	}
    }
  return 0;
}

/* Translate a mangled name into components.  */

struct demangle_component *
cplus_demangle_v3_components (const char *mangled, int options, void **mem)
{
  size_t len;
  int type;
  struct d_info di;
  struct demangle_component *dc;

  len = strlen (mangled);

  if (mangled[0] == '_' && mangled[1] == 'Z')
    type = 0;
  else
    {
      if ((options & DMGL_TYPES) == 0)
	return NULL;
      type = 1;
    }

  cplus_demangle_init_info (mangled, options, len, &di);

  di.comps = ((struct demangle_component *)
	      malloc (di.num_comps * sizeof (struct demangle_component)));
  di.subs = ((struct demangle_component **)
	     malloc (di.num_subs * sizeof (struct demangle_component *)));
  if (di.comps == NULL || di.subs == NULL)
    {
      if (di.comps != NULL)
	free (di.comps);
      if (di.subs != NULL)
	free (di.subs);
      return NULL;
    }

  if (! type)
    dc = cplus_demangle_mangled_name (&di, 1);
  else
    dc = cplus_demangle_type (&di);

  /* If DMGL_PARAMS is set, then if we didn't consume the entire
     mangled string, then we didn't successfully demangle it.  */
  if ((options & DMGL_PARAMS) != 0 && d_peek_char (&di) != '\0')
    dc = NULL;

  free (di.subs);

  if (dc != NULL)
    *mem = di.comps;
  else
    free (di.comps);

  return dc;
}
