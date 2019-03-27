/* Support for printing Java types for GDB, the GNU debugger.
   Copyright 1997, 1998, 1999, 2000 Free Software Foundation, Inc.

   This file is part of GDB.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place - Suite 330,
   Boston, MA 02111-1307, USA.  */


#include "defs.h"
#include "symtab.h"
#include "gdbtypes.h"
#include "value.h"
#include "demangle.h"
#include "jv-lang.h"
#include "gdb_string.h"
#include "typeprint.h"
#include "c-lang.h"
#include "cp-abi.h"

/* Local functions */

static void java_type_print_base (struct type * type,
				  struct ui_file *stream, int show,
				  int level);

static void
java_type_print_derivation_info (struct ui_file *stream, struct type *type)
{
  char *name;
  int i;
  int n_bases;
  int prev;

  n_bases = TYPE_N_BASECLASSES (type);

  for (i = 0, prev = 0; i < n_bases; i++)
    {
      int kind;

      kind = BASETYPE_VIA_VIRTUAL (type, i) ? 'I' : 'E';

      fputs_filtered (kind == prev ? ", "
		      : kind == 'I' ? " implements "
		      : " extends ",
		      stream);
      prev = kind;
      name = type_name_no_tag (TYPE_BASECLASS (type, i));

      fprintf_filtered (stream, "%s", name ? name : "(null)");
    }

  if (i > 0)
    fputs_filtered (" ", stream);
}

/* Print the name of the type (or the ultimate pointer target,
   function value or array element), or the description of a
   structure or union.

   SHOW positive means print details about the type (e.g. enum values),
   and print structure elements passing SHOW - 1 for show.
   SHOW negative means just print the type name or struct tag if there is one.
   If there is no name, print something sensible but concise like
   "struct {...}".
   SHOW zero means just print the type name or struct tag if there is one.
   If there is no name, print something sensible but not as concise like
   "struct {int x; int y;}".

   LEVEL is the number of spaces to indent by.
   We increase it for some recursive calls.  */

static void
java_type_print_base (struct type *type, struct ui_file *stream, int show,
		      int level)
{
  int i;
  int len;
  char *mangled_name;
  char *demangled_name;
  QUIT;

  wrap_here ("    ");

  if (type == NULL)
    {
      fputs_filtered ("<type unknown>", stream);
      return;
    }

  /* When SHOW is zero or less, and there is a valid type name, then always
     just print the type name directly from the type.  */

  if (show <= 0
      && TYPE_NAME (type) != NULL)
    {
      fputs_filtered (TYPE_NAME (type), stream);
      return;
    }

  CHECK_TYPEDEF (type);

  switch (TYPE_CODE (type))
    {
    case TYPE_CODE_PTR:
      java_type_print_base (TYPE_TARGET_TYPE (type), stream, show, level);
      break;

    case TYPE_CODE_STRUCT:
      if (TYPE_TAG_NAME (type) != NULL && TYPE_TAG_NAME (type)[0] == '[')
	{			/* array type */
	  char *name = java_demangle_type_signature (TYPE_TAG_NAME (type));
	  fputs_filtered (name, stream);
	  xfree (name);
	  break;
	}

      if (show >= 0)
	fprintf_filtered (stream, "class ");

      if (TYPE_TAG_NAME (type) != NULL)
	{
	  fputs_filtered (TYPE_TAG_NAME (type), stream);
	  if (show > 0)
	    fputs_filtered (" ", stream);
	}

      wrap_here ("    ");

      if (show < 0)
	{
	  /* If we just printed a tag name, no need to print anything else.  */
	  if (TYPE_TAG_NAME (type) == NULL)
	    fprintf_filtered (stream, "{...}");
	}
      else if (show > 0 || TYPE_TAG_NAME (type) == NULL)
	{
	  java_type_print_derivation_info (stream, type);

	  fprintf_filtered (stream, "{\n");
	  if ((TYPE_NFIELDS (type) == 0) && (TYPE_NFN_FIELDS (type) == 0))
	    {
	      if (TYPE_STUB (type))
		fprintfi_filtered (level + 4, stream, "<incomplete type>\n");
	      else
		fprintfi_filtered (level + 4, stream, "<no data fields>\n");
	    }

	  /* If there is a base class for this type,
	     do not print the field that it occupies.  */

	  len = TYPE_NFIELDS (type);
	  for (i = TYPE_N_BASECLASSES (type); i < len; i++)
	    {
	      QUIT;
	      /* Don't print out virtual function table.  */
	      if (strncmp (TYPE_FIELD_NAME (type, i), "_vptr", 5) == 0
		  && is_cplus_marker ((TYPE_FIELD_NAME (type, i))[5]))
		continue;

	      /* Don't print the dummy field "class". */
	      if (strncmp (TYPE_FIELD_NAME (type, i), "class", 5) == 0)
		continue;

	      print_spaces_filtered (level + 4, stream);

	      if (HAVE_CPLUS_STRUCT (type))
		{
		  if (TYPE_FIELD_PROTECTED (type, i))
		    fprintf_filtered (stream, "protected ");
		  else if (TYPE_FIELD_PRIVATE (type, i))
		    fprintf_filtered (stream, "private ");
		  else
		    fprintf_filtered (stream, "public ");
		}

	      if (TYPE_FIELD_STATIC (type, i))
		fprintf_filtered (stream, "static ");

	      java_print_type (TYPE_FIELD_TYPE (type, i),
			       TYPE_FIELD_NAME (type, i),
			       stream, show - 1, level + 4);

	      fprintf_filtered (stream, ";\n");
	    }

	  /* If there are both fields and methods, put a space between. */
	  len = TYPE_NFN_FIELDS (type);
	  if (len)
	    fprintf_filtered (stream, "\n");

	  /* Print out the methods */

	  for (i = 0; i < len; i++)
	    {
	      struct fn_field *f;
	      int j;
	      char *method_name;
	      char *name;
	      int is_constructor;
	      int n_overloads;

	      f = TYPE_FN_FIELDLIST1 (type, i);
	      n_overloads = TYPE_FN_FIELDLIST_LENGTH (type, i);
	      method_name = TYPE_FN_FIELDLIST_NAME (type, i);
	      name = type_name_no_tag (type);
	      is_constructor = name && strcmp (method_name, name) == 0;

	      for (j = 0; j < n_overloads; j++)
		{
		  char *physname;
		  int is_full_physname_constructor;

		  physname = TYPE_FN_FIELD_PHYSNAME (f, j);

		  is_full_physname_constructor
                    = (is_constructor_name (physname)
                       || is_destructor_name (physname));

		  QUIT;

		  print_spaces_filtered (level + 4, stream);

		  if (TYPE_FN_FIELD_PROTECTED (f, j))
		    fprintf_filtered (stream, "protected ");
		  else if (TYPE_FN_FIELD_PRIVATE (f, j))
		    fprintf_filtered (stream, "private ");
		  else if (TYPE_FN_FIELD_PUBLIC (f, j))
		    fprintf_filtered (stream, "public ");

		  if (TYPE_FN_FIELD_ABSTRACT (f, j))
		    fprintf_filtered (stream, "abstract ");
		  if (TYPE_FN_FIELD_STATIC (f, j))
		    fprintf_filtered (stream, "static ");
		  if (TYPE_FN_FIELD_FINAL (f, j))
		    fprintf_filtered (stream, "final ");
		  if (TYPE_FN_FIELD_SYNCHRONIZED (f, j))
		    fprintf_filtered (stream, "synchronized ");
		  if (TYPE_FN_FIELD_NATIVE (f, j))
		    fprintf_filtered (stream, "native ");

		  if (TYPE_TARGET_TYPE (TYPE_FN_FIELD_TYPE (f, j)) == 0)
		    {
		      /* Keep GDB from crashing here.  */
		      fprintf_filtered (stream, "<undefined type> %s;\n",
					TYPE_FN_FIELD_PHYSNAME (f, j));
		      break;
		    }
		  else if (!is_constructor && !is_full_physname_constructor)
		    {
		      type_print (TYPE_TARGET_TYPE (TYPE_FN_FIELD_TYPE (f, j)),
				  "", stream, -1);
		      fputs_filtered (" ", stream);
		    }

		  if (TYPE_FN_FIELD_STUB (f, j))
		    /* Build something we can demangle.  */
		    mangled_name = gdb_mangle_name (type, i, j);
		  else
		    mangled_name = TYPE_FN_FIELD_PHYSNAME (f, j);

		  demangled_name =
		    cplus_demangle (mangled_name,
				    DMGL_ANSI | DMGL_PARAMS | DMGL_JAVA);

		  if (demangled_name == NULL)
		    demangled_name = xstrdup (mangled_name);

		  {
		    char *demangled_no_class;
		    char *ptr;

		    ptr = demangled_no_class = demangled_name;

		    while (1)
		      {
			char c;

			c = *ptr++;

			if (c == 0 || c == '(')
			  break;
			if (c == '.')
			  demangled_no_class = ptr;
		      }

		    fputs_filtered (demangled_no_class, stream);
		    xfree (demangled_name);
		  }

		  if (TYPE_FN_FIELD_STUB (f, j))
		    xfree (mangled_name);

		  fprintf_filtered (stream, ";\n");
		}
	    }

	  fprintfi_filtered (level, stream, "}");
	}
      break;

    default:
      c_type_print_base (type, stream, show, level);
    }
}

/* LEVEL is the depth to indent lines by.  */

extern void c_type_print_varspec_suffix (struct type *, struct ui_file *,
					 int, int, int);

void
java_print_type (struct type *type, char *varstring, struct ui_file *stream,
		 int show, int level)
{
  int demangled_args;

  java_type_print_base (type, stream, show, level);

  if (varstring != NULL && *varstring != '\0')
    {
      fputs_filtered (" ", stream);
      fputs_filtered (varstring, stream);
    }

  /* For demangled function names, we have the arglist as part of the name,
     so don't print an additional pair of ()'s */

  demangled_args = strchr (varstring, '(') != NULL;
  c_type_print_varspec_suffix (type, stream, show, 0, demangled_args);
}
