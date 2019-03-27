/* Support for printing Pascal values for GDB, the GNU debugger.
   Copyright 2000, 2001, 2003
   Free Software Foundation, Inc.

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
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.  */

/* This file is derived from c-valprint.c */

#include "defs.h"
#include "gdb_obstack.h"
#include "symtab.h"
#include "gdbtypes.h"
#include "expression.h"
#include "value.h"
#include "command.h"
#include "gdbcmd.h"
#include "gdbcore.h"
#include "demangle.h"
#include "valprint.h"
#include "typeprint.h"
#include "language.h"
#include "target.h"
#include "annotate.h"
#include "p-lang.h"
#include "cp-abi.h"




/* Print data of type TYPE located at VALADDR (within GDB), which came from
   the inferior at address ADDRESS, onto stdio stream STREAM according to
   FORMAT (a letter or 0 for natural format).  The data at VALADDR is in
   target byte order.

   If the data are a string pointer, returns the number of string characters
   printed.

   If DEREF_REF is nonzero, then dereference references, otherwise just print
   them like pointers.

   The PRETTY parameter controls prettyprinting.  */


int
pascal_val_print (struct type *type, char *valaddr, int embedded_offset,
		  CORE_ADDR address, struct ui_file *stream, int format,
		  int deref_ref, int recurse, enum val_prettyprint pretty)
{
  unsigned int i = 0;	/* Number of characters printed */
  unsigned len;
  struct type *elttype;
  unsigned eltlen;
  int length_pos, length_size, string_pos;
  int char_size;
  LONGEST val;
  CORE_ADDR addr;

  CHECK_TYPEDEF (type);
  switch (TYPE_CODE (type))
    {
    case TYPE_CODE_ARRAY:
      if (TYPE_LENGTH (type) > 0 && TYPE_LENGTH (TYPE_TARGET_TYPE (type)) > 0)
	{
	  elttype = check_typedef (TYPE_TARGET_TYPE (type));
	  eltlen = TYPE_LENGTH (elttype);
	  len = TYPE_LENGTH (type) / eltlen;
	  if (prettyprint_arrays)
	    {
	      print_spaces_filtered (2 + 2 * recurse, stream);
	    }
	  /* For an array of chars, print with string syntax.  */
	  if (eltlen == 1 &&
	      ((TYPE_CODE (elttype) == TYPE_CODE_INT)
	       || ((current_language->la_language == language_m2)
		   && (TYPE_CODE (elttype) == TYPE_CODE_CHAR)))
	      && (format == 0 || format == 's'))
	    {
	      /* If requested, look for the first null char and only print
	         elements up to it.  */
	      if (stop_print_at_null)
		{
		  unsigned int temp_len;

		  /* Look for a NULL char. */
		  for (temp_len = 0;
		       (valaddr + embedded_offset)[temp_len]
		       && temp_len < len && temp_len < print_max;
		       temp_len++);
		  len = temp_len;
		}

	      LA_PRINT_STRING (stream, valaddr + embedded_offset, len, 1, 0);
	      i = len;
	    }
	  else
	    {
	      fprintf_filtered (stream, "{");
	      /* If this is a virtual function table, print the 0th
	         entry specially, and the rest of the members normally.  */
	      if (pascal_object_is_vtbl_ptr_type (elttype))
		{
		  i = 1;
		  fprintf_filtered (stream, "%d vtable entries", len - 1);
		}
	      else
		{
		  i = 0;
		}
	      val_print_array_elements (type, valaddr + embedded_offset, address, stream,
				     format, deref_ref, recurse, pretty, i);
	      fprintf_filtered (stream, "}");
	    }
	  break;
	}
      /* Array of unspecified length: treat like pointer to first elt.  */
      addr = address;
      goto print_unpacked_pointer;

    case TYPE_CODE_PTR:
      if (format && format != 's')
	{
	  print_scalar_formatted (valaddr + embedded_offset, type, format, 0, stream);
	  break;
	}
      if (vtblprint && pascal_object_is_vtbl_ptr_type (type))
	{
	  /* Print the unmangled name if desired.  */
	  /* Print vtable entry - we only get here if we ARE using
	     -fvtable_thunks.  (Otherwise, look under TYPE_CODE_STRUCT.) */
	  /* Extract the address, assume that it is unsigned.  */
	  print_address_demangle (extract_unsigned_integer (valaddr + embedded_offset, TYPE_LENGTH (type)),
				  stream, demangle);
	  break;
	}
      elttype = check_typedef (TYPE_TARGET_TYPE (type));
      if (TYPE_CODE (elttype) == TYPE_CODE_METHOD)
	{
	  pascal_object_print_class_method (valaddr + embedded_offset, type, stream);
	}
      else if (TYPE_CODE (elttype) == TYPE_CODE_MEMBER)
	{
	  pascal_object_print_class_member (valaddr + embedded_offset,
				 TYPE_DOMAIN_TYPE (TYPE_TARGET_TYPE (type)),
					    stream, "&");
	}
      else
	{
	  addr = unpack_pointer (type, valaddr + embedded_offset);
	print_unpacked_pointer:
	  elttype = check_typedef (TYPE_TARGET_TYPE (type));

	  if (TYPE_CODE (elttype) == TYPE_CODE_FUNC)
	    {
	      /* Try to print what function it points to.  */
	      print_address_demangle (addr, stream, demangle);
	      /* Return value is irrelevant except for string pointers.  */
	      return (0);
	    }

	  if (addressprint && format != 's')
	    {
	      print_address_numeric (addr, 1, stream);
	    }

	  /* For a pointer to char or unsigned char, also print the string
	     pointed to, unless pointer is null.  */
	  if (TYPE_LENGTH (elttype) == 1
	      && TYPE_CODE (elttype) == TYPE_CODE_INT
	      && (format == 0 || format == 's')
	      && addr != 0)
	    {
	      /* no wide string yet */
	      i = val_print_string (addr, -1, 1, stream);
	    }
	  /* also for pointers to pascal strings */
	  /* Note: this is Free Pascal specific:
	     as GDB does not recognize stabs pascal strings
	     Pascal strings are mapped to records
	     with lowercase names PM  */
          if (is_pascal_string_type (elttype, &length_pos, &length_size,
                                     &string_pos, &char_size, NULL)
	      && addr != 0)
	    {
	      ULONGEST string_length;
              void *buffer;
              buffer = xmalloc (length_size);
              read_memory (addr + length_pos, buffer, length_size);
	      string_length = extract_unsigned_integer (buffer, length_size);
              xfree (buffer);
              i = val_print_string (addr + string_pos, string_length, char_size, stream);
	    }
	  else if (pascal_object_is_vtbl_member (type))
	    {
	      /* print vtbl's nicely */
	      CORE_ADDR vt_address = unpack_pointer (type, valaddr + embedded_offset);

	      struct minimal_symbol *msymbol =
	      lookup_minimal_symbol_by_pc (vt_address);
	      if ((msymbol != NULL)
		  && (vt_address == SYMBOL_VALUE_ADDRESS (msymbol)))
		{
		  fputs_filtered (" <", stream);
		  fputs_filtered (SYMBOL_PRINT_NAME (msymbol), stream);
		  fputs_filtered (">", stream);
		}
	      if (vt_address && vtblprint)
		{
		  struct value *vt_val;
		  struct symbol *wsym = (struct symbol *) NULL;
		  struct type *wtype;
		  struct block *block = (struct block *) NULL;
		  int is_this_fld;

		  if (msymbol != NULL)
		    wsym = lookup_symbol (DEPRECATED_SYMBOL_NAME (msymbol), block,
					  VAR_DOMAIN, &is_this_fld, NULL);

		  if (wsym)
		    {
		      wtype = SYMBOL_TYPE (wsym);
		    }
		  else
		    {
		      wtype = TYPE_TARGET_TYPE (type);
		    }
		  vt_val = value_at (wtype, vt_address, NULL);
		  common_val_print (vt_val, stream, format, deref_ref,
				    recurse + 1, pretty);
		  if (pretty)
		    {
		      fprintf_filtered (stream, "\n");
		      print_spaces_filtered (2 + 2 * recurse, stream);
		    }
		}
	    }

	  /* Return number of characters printed, including the terminating
	     '\0' if we reached the end.  val_print_string takes care including
	     the terminating '\0' if necessary.  */
	  return i;
	}
      break;

    case TYPE_CODE_MEMBER:
      error ("not implemented: member type in pascal_val_print");
      break;

    case TYPE_CODE_REF:
      elttype = check_typedef (TYPE_TARGET_TYPE (type));
      if (TYPE_CODE (elttype) == TYPE_CODE_MEMBER)
	{
	  pascal_object_print_class_member (valaddr + embedded_offset,
					    TYPE_DOMAIN_TYPE (elttype),
					    stream, "");
	  break;
	}
      if (addressprint)
	{
	  fprintf_filtered (stream, "@");
	  /* Extract the address, assume that it is unsigned.  */
	  print_address_numeric
	    (extract_unsigned_integer (valaddr + embedded_offset,
				       TARGET_PTR_BIT / HOST_CHAR_BIT),
	     1, stream);
	  if (deref_ref)
	    fputs_filtered (": ", stream);
	}
      /* De-reference the reference.  */
      if (deref_ref)
	{
	  if (TYPE_CODE (elttype) != TYPE_CODE_UNDEF)
	    {
	      struct value *deref_val =
	      value_at
	      (TYPE_TARGET_TYPE (type),
	       unpack_pointer (lookup_pointer_type (builtin_type_void),
			       valaddr + embedded_offset),
	       NULL);
	      common_val_print (deref_val, stream, format, deref_ref,
				recurse + 1, pretty);
	    }
	  else
	    fputs_filtered ("???", stream);
	}
      break;

    case TYPE_CODE_UNION:
      if (recurse && !unionprint)
	{
	  fprintf_filtered (stream, "{...}");
	  break;
	}
      /* Fall through.  */
    case TYPE_CODE_STRUCT:
      if (vtblprint && pascal_object_is_vtbl_ptr_type (type))
	{
	  /* Print the unmangled name if desired.  */
	  /* Print vtable entry - we only get here if NOT using
	     -fvtable_thunks.  (Otherwise, look under TYPE_CODE_PTR.) */
	  /* Extract the address, assume that it is unsigned.  */
	  print_address_demangle
	    (extract_unsigned_integer (valaddr + embedded_offset + TYPE_FIELD_BITPOS (type, VTBL_FNADDR_OFFSET) / 8,
				       TYPE_LENGTH (TYPE_FIELD_TYPE (type, VTBL_FNADDR_OFFSET))),
	     stream, demangle);
	}
      else
	{
          if (is_pascal_string_type (type, &length_pos, &length_size,
                                     &string_pos, &char_size, NULL))
	    {
	      len = extract_unsigned_integer (valaddr + embedded_offset + length_pos, length_size);
	      LA_PRINT_STRING (stream, valaddr + embedded_offset + string_pos, len, char_size, 0);
	    }
	  else
	    pascal_object_print_value_fields (type, valaddr + embedded_offset, address, stream, format,
					      recurse, pretty, NULL, 0);
	}
      break;

    case TYPE_CODE_ENUM:
      if (format)
	{
	  print_scalar_formatted (valaddr + embedded_offset, type, format, 0, stream);
	  break;
	}
      len = TYPE_NFIELDS (type);
      val = unpack_long (type, valaddr + embedded_offset);
      for (i = 0; i < len; i++)
	{
	  QUIT;
	  if (val == TYPE_FIELD_BITPOS (type, i))
	    {
	      break;
	    }
	}
      if (i < len)
	{
	  fputs_filtered (TYPE_FIELD_NAME (type, i), stream);
	}
      else
	{
	  print_longest (stream, 'd', 0, val);
	}
      break;

    case TYPE_CODE_FUNC:
      if (format)
	{
	  print_scalar_formatted (valaddr + embedded_offset, type, format, 0, stream);
	  break;
	}
      /* FIXME, we should consider, at least for ANSI C language, eliminating
         the distinction made between FUNCs and POINTERs to FUNCs.  */
      fprintf_filtered (stream, "{");
      type_print (type, "", stream, -1);
      fprintf_filtered (stream, "} ");
      /* Try to print what function it points to, and its address.  */
      print_address_demangle (address, stream, demangle);
      break;

    case TYPE_CODE_BOOL:
      format = format ? format : output_format;
      if (format)
	print_scalar_formatted (valaddr + embedded_offset, type, format, 0, stream);
      else
	{
	  val = unpack_long (type, valaddr + embedded_offset);
	  if (val == 0)
	    fputs_filtered ("false", stream);
	  else if (val == 1)
	    fputs_filtered ("true", stream);
	  else
	    {
	      fputs_filtered ("true (", stream);
	      fprintf_filtered (stream, "%ld)", (long int) val);
	    }
	}
      break;

    case TYPE_CODE_RANGE:
      /* FIXME: create_range_type does not set the unsigned bit in a
         range type (I think it probably should copy it from the target
         type), so we won't print values which are too large to
         fit in a signed integer correctly.  */
      /* FIXME: Doesn't handle ranges of enums correctly.  (Can't just
         print with the target type, though, because the size of our type
         and the target type might differ).  */
      /* FALLTHROUGH */

    case TYPE_CODE_INT:
      format = format ? format : output_format;
      if (format)
	{
	  print_scalar_formatted (valaddr + embedded_offset, type, format, 0, stream);
	}
      else
	{
	  val_print_type_code_int (type, valaddr + embedded_offset, stream);
	}
      break;

    case TYPE_CODE_CHAR:
      format = format ? format : output_format;
      if (format)
	{
	  print_scalar_formatted (valaddr + embedded_offset, type, format, 0, stream);
	}
      else
	{
	  val = unpack_long (type, valaddr + embedded_offset);
	  if (TYPE_UNSIGNED (type))
	    fprintf_filtered (stream, "%u", (unsigned int) val);
	  else
	    fprintf_filtered (stream, "%d", (int) val);
	  fputs_filtered (" ", stream);
	  LA_PRINT_CHAR ((unsigned char) val, stream);
	}
      break;

    case TYPE_CODE_FLT:
      if (format)
	{
	  print_scalar_formatted (valaddr + embedded_offset, type, format, 0, stream);
	}
      else
	{
	  print_floating (valaddr + embedded_offset, type, stream);
	}
      break;

    case TYPE_CODE_BITSTRING:
    case TYPE_CODE_SET:
      elttype = TYPE_INDEX_TYPE (type);
      CHECK_TYPEDEF (elttype);
      if (TYPE_STUB (elttype))
	{
	  fprintf_filtered (stream, "<incomplete type>");
	  gdb_flush (stream);
	  break;
	}
      else
	{
	  struct type *range = elttype;
	  LONGEST low_bound, high_bound;
	  int i;
	  int is_bitstring = TYPE_CODE (type) == TYPE_CODE_BITSTRING;
	  int need_comma = 0;

	  if (is_bitstring)
	    fputs_filtered ("B'", stream);
	  else
	    fputs_filtered ("[", stream);

	  i = get_discrete_bounds (range, &low_bound, &high_bound);
	maybe_bad_bstring:
	  if (i < 0)
	    {
	      fputs_filtered ("<error value>", stream);
	      goto done;
	    }

	  for (i = low_bound; i <= high_bound; i++)
	    {
	      int element = value_bit_index (type, valaddr + embedded_offset, i);
	      if (element < 0)
		{
		  i = element;
		  goto maybe_bad_bstring;
		}
	      if (is_bitstring)
		fprintf_filtered (stream, "%d", element);
	      else if (element)
		{
		  if (need_comma)
		    fputs_filtered (", ", stream);
		  print_type_scalar (range, i, stream);
		  need_comma = 1;

		  if (i + 1 <= high_bound && value_bit_index (type, valaddr + embedded_offset, ++i))
		    {
		      int j = i;
		      fputs_filtered ("..", stream);
		      while (i + 1 <= high_bound
			     && value_bit_index (type, valaddr + embedded_offset, ++i))
			j = i;
		      print_type_scalar (range, j, stream);
		    }
		}
	    }
	done:
	  if (is_bitstring)
	    fputs_filtered ("'", stream);
	  else
	    fputs_filtered ("]", stream);
	}
      break;

    case TYPE_CODE_VOID:
      fprintf_filtered (stream, "void");
      break;

    case TYPE_CODE_ERROR:
      fprintf_filtered (stream, "<error type>");
      break;

    case TYPE_CODE_UNDEF:
      /* This happens (without TYPE_FLAG_STUB set) on systems which don't use
         dbx xrefs (NO_DBX_XREFS in gcc) if a file has a "struct foo *bar"
         and no complete type for struct foo in that file.  */
      fprintf_filtered (stream, "<incomplete type>");
      break;

    default:
      error ("Invalid pascal type code %d in symbol table.", TYPE_CODE (type));
    }
  gdb_flush (stream);
  return (0);
}

int
pascal_value_print (struct value *val, struct ui_file *stream, int format,
		    enum val_prettyprint pretty)
{
  struct type *type = VALUE_TYPE (val);

  /* If it is a pointer, indicate what it points to.

     Print type also if it is a reference.

     Object pascal: if it is a member pointer, we will take care
     of that when we print it.  */
  if (TYPE_CODE (type) == TYPE_CODE_PTR ||
      TYPE_CODE (type) == TYPE_CODE_REF)
    {
      /* Hack:  remove (char *) for char strings.  Their
         type is indicated by the quoted string anyway. */
      if (TYPE_CODE (type) == TYPE_CODE_PTR &&
	  TYPE_NAME (type) == NULL &&
	  TYPE_NAME (TYPE_TARGET_TYPE (type)) != NULL
	  && strcmp (TYPE_NAME (TYPE_TARGET_TYPE (type)), "char") == 0)
	{
	  /* Print nothing */
	}
      else
	{
	  fprintf_filtered (stream, "(");
	  type_print (type, "", stream, -1);
	  fprintf_filtered (stream, ") ");
	}
    }
  return common_val_print (val, stream, format, 1, 0, pretty);
}


/******************************************************************************
                    Inserted from cp-valprint
******************************************************************************/

extern int vtblprint;		/* Controls printing of vtbl's */
extern int objectprint;		/* Controls looking up an object's derived type
				   using what we find in its vtables.  */
static int pascal_static_field_print;	/* Controls printing of static fields. */

static struct obstack dont_print_vb_obstack;
static struct obstack dont_print_statmem_obstack;

static void pascal_object_print_static_field (struct value *,
					      struct ui_file *, int, int,
					      enum val_prettyprint);

static void
  pascal_object_print_value (struct type *, char *, CORE_ADDR, struct ui_file *,
			     int, int, enum val_prettyprint, struct type **);

void
pascal_object_print_class_method (char *valaddr, struct type *type,
				  struct ui_file *stream)
{
  struct type *domain;
  struct fn_field *f = NULL;
  int j = 0;
  int len2;
  int offset;
  char *kind = "";
  CORE_ADDR addr;
  struct symbol *sym;
  unsigned len;
  unsigned int i;
  struct type *target_type = check_typedef (TYPE_TARGET_TYPE (type));

  domain = TYPE_DOMAIN_TYPE (target_type);
  if (domain == (struct type *) NULL)
    {
      fprintf_filtered (stream, "<unknown>");
      return;
    }
  addr = unpack_pointer (lookup_pointer_type (builtin_type_void), valaddr);
  if (METHOD_PTR_IS_VIRTUAL (addr))
    {
      offset = METHOD_PTR_TO_VOFFSET (addr);
      len = TYPE_NFN_FIELDS (domain);
      for (i = 0; i < len; i++)
	{
	  f = TYPE_FN_FIELDLIST1 (domain, i);
	  len2 = TYPE_FN_FIELDLIST_LENGTH (domain, i);

	  check_stub_method_group (domain, i);
	  for (j = 0; j < len2; j++)
	    {
	      if (TYPE_FN_FIELD_VOFFSET (f, j) == offset)
		{
		  kind = "virtual ";
		  goto common;
		}
	    }
	}
    }
  else
    {
      sym = find_pc_function (addr);
      if (sym == 0)
	{
	  error ("invalid pointer to member function");
	}
      len = TYPE_NFN_FIELDS (domain);
      for (i = 0; i < len; i++)
	{
	  f = TYPE_FN_FIELDLIST1 (domain, i);
	  len2 = TYPE_FN_FIELDLIST_LENGTH (domain, i);

	  check_stub_method_group (domain, i);
	  for (j = 0; j < len2; j++)
	    {
	      if (DEPRECATED_STREQ (DEPRECATED_SYMBOL_NAME (sym), TYPE_FN_FIELD_PHYSNAME (f, j)))
		goto common;
	    }
	}
    }
common:
  if (i < len)
    {
      char *demangled_name;

      fprintf_filtered (stream, "&");
      fputs_filtered (kind, stream);
      demangled_name = cplus_demangle (TYPE_FN_FIELD_PHYSNAME (f, j),
				       DMGL_ANSI | DMGL_PARAMS);
      if (demangled_name == NULL)
	fprintf_filtered (stream, "<badly mangled name %s>",
			  TYPE_FN_FIELD_PHYSNAME (f, j));
      else
	{
	  fputs_filtered (demangled_name, stream);
	  xfree (demangled_name);
	}
    }
  else
    {
      fprintf_filtered (stream, "(");
      type_print (type, "", stream, -1);
      fprintf_filtered (stream, ") %d", (int) addr >> 3);
    }
}

/* It was changed to this after 2.4.5.  */
const char pascal_vtbl_ptr_name[] =
{'_', '_', 'v', 't', 'b', 'l', '_', 'p', 't', 'r', '_', 't', 'y', 'p', 'e', 0};

/* Return truth value for assertion that TYPE is of the type
   "pointer to virtual function".  */

int
pascal_object_is_vtbl_ptr_type (struct type *type)
{
  char *typename = type_name_no_tag (type);

  return (typename != NULL
	  && strcmp (typename, pascal_vtbl_ptr_name) == 0);
}

/* Return truth value for the assertion that TYPE is of the type
   "pointer to virtual function table".  */

int
pascal_object_is_vtbl_member (struct type *type)
{
  if (TYPE_CODE (type) == TYPE_CODE_PTR)
    {
      type = TYPE_TARGET_TYPE (type);
      if (TYPE_CODE (type) == TYPE_CODE_ARRAY)
	{
	  type = TYPE_TARGET_TYPE (type);
	  if (TYPE_CODE (type) == TYPE_CODE_STRUCT	/* if not using thunks */
	      || TYPE_CODE (type) == TYPE_CODE_PTR)	/* if using thunks */
	    {
	      /* Virtual functions tables are full of pointers
	         to virtual functions. */
	      return pascal_object_is_vtbl_ptr_type (type);
	    }
	}
    }
  return 0;
}

/* Mutually recursive subroutines of pascal_object_print_value and c_val_print to
   print out a structure's fields: pascal_object_print_value_fields and pascal_object_print_value.

   TYPE, VALADDR, ADDRESS, STREAM, RECURSE, and PRETTY have the
   same meanings as in pascal_object_print_value and c_val_print.

   DONT_PRINT is an array of baseclass types that we
   should not print, or zero if called from top level.  */

void
pascal_object_print_value_fields (struct type *type, char *valaddr,
				  CORE_ADDR address, struct ui_file *stream,
				  int format, int recurse,
				  enum val_prettyprint pretty,
				  struct type **dont_print_vb,
				  int dont_print_statmem)
{
  int i, len, n_baseclasses;
  struct obstack tmp_obstack;
  char *last_dont_print = obstack_next_free (&dont_print_statmem_obstack);

  CHECK_TYPEDEF (type);

  fprintf_filtered (stream, "{");
  len = TYPE_NFIELDS (type);
  n_baseclasses = TYPE_N_BASECLASSES (type);

  /* Print out baseclasses such that we don't print
     duplicates of virtual baseclasses.  */
  if (n_baseclasses > 0)
    pascal_object_print_value (type, valaddr, address, stream,
			       format, recurse + 1, pretty, dont_print_vb);

  if (!len && n_baseclasses == 1)
    fprintf_filtered (stream, "<No data fields>");
  else
    {
      int fields_seen = 0;

      if (dont_print_statmem == 0)
	{
	  /* If we're at top level, carve out a completely fresh
	     chunk of the obstack and use that until this particular
	     invocation returns.  */
	  tmp_obstack = dont_print_statmem_obstack;
	  obstack_finish (&dont_print_statmem_obstack);
	}

      for (i = n_baseclasses; i < len; i++)
	{
	  /* If requested, skip printing of static fields.  */
	  if (!pascal_static_field_print && TYPE_FIELD_STATIC (type, i))
	    continue;
	  if (fields_seen)
	    fprintf_filtered (stream, ", ");
	  else if (n_baseclasses > 0)
	    {
	      if (pretty)
		{
		  fprintf_filtered (stream, "\n");
		  print_spaces_filtered (2 + 2 * recurse, stream);
		  fputs_filtered ("members of ", stream);
		  fputs_filtered (type_name_no_tag (type), stream);
		  fputs_filtered (": ", stream);
		}
	    }
	  fields_seen = 1;

	  if (pretty)
	    {
	      fprintf_filtered (stream, "\n");
	      print_spaces_filtered (2 + 2 * recurse, stream);
	    }
	  else
	    {
	      wrap_here (n_spaces (2 + 2 * recurse));
	    }
	  if (inspect_it)
	    {
	      if (TYPE_CODE (TYPE_FIELD_TYPE (type, i)) == TYPE_CODE_PTR)
		fputs_filtered ("\"( ptr \"", stream);
	      else
		fputs_filtered ("\"( nodef \"", stream);
	      if (TYPE_FIELD_STATIC (type, i))
		fputs_filtered ("static ", stream);
	      fprintf_symbol_filtered (stream, TYPE_FIELD_NAME (type, i),
				       language_cplus,
				       DMGL_PARAMS | DMGL_ANSI);
	      fputs_filtered ("\" \"", stream);
	      fprintf_symbol_filtered (stream, TYPE_FIELD_NAME (type, i),
				       language_cplus,
				       DMGL_PARAMS | DMGL_ANSI);
	      fputs_filtered ("\") \"", stream);
	    }
	  else
	    {
	      annotate_field_begin (TYPE_FIELD_TYPE (type, i));

	      if (TYPE_FIELD_STATIC (type, i))
		fputs_filtered ("static ", stream);
	      fprintf_symbol_filtered (stream, TYPE_FIELD_NAME (type, i),
				       language_cplus,
				       DMGL_PARAMS | DMGL_ANSI);
	      annotate_field_name_end ();
	      fputs_filtered (" = ", stream);
	      annotate_field_value ();
	    }

	  if (!TYPE_FIELD_STATIC (type, i) && TYPE_FIELD_PACKED (type, i))
	    {
	      struct value *v;

	      /* Bitfields require special handling, especially due to byte
	         order problems.  */
	      if (TYPE_FIELD_IGNORE (type, i))
		{
		  fputs_filtered ("<optimized out or zero length>", stream);
		}
	      else
		{
		  v = value_from_longest (TYPE_FIELD_TYPE (type, i),
				   unpack_field_as_long (type, valaddr, i));

		  common_val_print (v, stream, format, 0, recurse + 1, pretty);
		}
	    }
	  else
	    {
	      if (TYPE_FIELD_IGNORE (type, i))
		{
		  fputs_filtered ("<optimized out or zero length>", stream);
		}
	      else if (TYPE_FIELD_STATIC (type, i))
		{
		  /* struct value *v = value_static_field (type, i); v4.17 specific */
		  struct value *v;
		  v = value_from_longest (TYPE_FIELD_TYPE (type, i),
				   unpack_field_as_long (type, valaddr, i));

		  if (v == NULL)
		    fputs_filtered ("<optimized out>", stream);
		  else
		    pascal_object_print_static_field (v, stream, format,
						      recurse + 1, pretty);
		}
	      else
		{
		  /* val_print (TYPE_FIELD_TYPE (type, i),
		     valaddr + TYPE_FIELD_BITPOS (type, i) / 8,
		     address + TYPE_FIELD_BITPOS (type, i) / 8, 0,
		     stream, format, 0, recurse + 1, pretty); */
		  val_print (TYPE_FIELD_TYPE (type, i),
			     valaddr, TYPE_FIELD_BITPOS (type, i) / 8,
			     address + TYPE_FIELD_BITPOS (type, i) / 8,
			     stream, format, 0, recurse + 1, pretty);
		}
	    }
	  annotate_field_end ();
	}

      if (dont_print_statmem == 0)
	{
	  /* Free the space used to deal with the printing
	     of the members from top level.  */
	  obstack_free (&dont_print_statmem_obstack, last_dont_print);
	  dont_print_statmem_obstack = tmp_obstack;
	}

      if (pretty)
	{
	  fprintf_filtered (stream, "\n");
	  print_spaces_filtered (2 * recurse, stream);
	}
    }
  fprintf_filtered (stream, "}");
}

/* Special val_print routine to avoid printing multiple copies of virtual
   baseclasses.  */

void
pascal_object_print_value (struct type *type, char *valaddr, CORE_ADDR address,
			   struct ui_file *stream, int format, int recurse,
			   enum val_prettyprint pretty,
			   struct type **dont_print_vb)
{
  struct obstack tmp_obstack;
  struct type **last_dont_print
  = (struct type **) obstack_next_free (&dont_print_vb_obstack);
  int i, n_baseclasses = TYPE_N_BASECLASSES (type);

  if (dont_print_vb == 0)
    {
      /* If we're at top level, carve out a completely fresh
         chunk of the obstack and use that until this particular
         invocation returns.  */
      tmp_obstack = dont_print_vb_obstack;
      /* Bump up the high-water mark.  Now alpha is omega.  */
      obstack_finish (&dont_print_vb_obstack);
    }

  for (i = 0; i < n_baseclasses; i++)
    {
      int boffset;
      struct type *baseclass = check_typedef (TYPE_BASECLASS (type, i));
      char *basename = TYPE_NAME (baseclass);
      char *base_valaddr;

      if (BASETYPE_VIA_VIRTUAL (type, i))
	{
	  struct type **first_dont_print
	  = (struct type **) obstack_base (&dont_print_vb_obstack);

	  int j = (struct type **) obstack_next_free (&dont_print_vb_obstack)
	  - first_dont_print;

	  while (--j >= 0)
	    if (baseclass == first_dont_print[j])
	      goto flush_it;

	  obstack_ptr_grow (&dont_print_vb_obstack, baseclass);
	}

      boffset = baseclass_offset (type, i, valaddr, address);

      if (pretty)
	{
	  fprintf_filtered (stream, "\n");
	  print_spaces_filtered (2 * recurse, stream);
	}
      fputs_filtered ("<", stream);
      /* Not sure what the best notation is in the case where there is no
         baseclass name.  */

      fputs_filtered (basename ? basename : "", stream);
      fputs_filtered ("> = ", stream);

      /* The virtual base class pointer might have been clobbered by the
         user program. Make sure that it still points to a valid memory
         location.  */

      if (boffset != -1 && (boffset < 0 || boffset >= TYPE_LENGTH (type)))
	{
	  /* FIXME (alloc): not safe is baseclass is really really big. */
	  base_valaddr = (char *) alloca (TYPE_LENGTH (baseclass));
	  if (target_read_memory (address + boffset, base_valaddr,
				  TYPE_LENGTH (baseclass)) != 0)
	    boffset = -1;
	}
      else
	base_valaddr = valaddr + boffset;

      if (boffset == -1)
	fprintf_filtered (stream, "<invalid address>");
      else
	pascal_object_print_value_fields (baseclass, base_valaddr, address + boffset,
					  stream, format, recurse, pretty,
		     (struct type **) obstack_base (&dont_print_vb_obstack),
					  0);
      fputs_filtered (", ", stream);

    flush_it:
      ;
    }

  if (dont_print_vb == 0)
    {
      /* Free the space used to deal with the printing
         of this type from top level.  */
      obstack_free (&dont_print_vb_obstack, last_dont_print);
      /* Reset watermark so that we can continue protecting
         ourselves from whatever we were protecting ourselves.  */
      dont_print_vb_obstack = tmp_obstack;
    }
}

/* Print value of a static member.
   To avoid infinite recursion when printing a class that contains
   a static instance of the class, we keep the addresses of all printed
   static member classes in an obstack and refuse to print them more
   than once.

   VAL contains the value to print, STREAM, RECURSE, and PRETTY
   have the same meanings as in c_val_print.  */

static void
pascal_object_print_static_field (struct value *val,
				  struct ui_file *stream, int format,
				  int recurse, enum val_prettyprint pretty)
{
  struct type *type = VALUE_TYPE (val);

  if (TYPE_CODE (type) == TYPE_CODE_STRUCT)
    {
      CORE_ADDR *first_dont_print;
      int i;

      first_dont_print
	= (CORE_ADDR *) obstack_base (&dont_print_statmem_obstack);
      i = (CORE_ADDR *) obstack_next_free (&dont_print_statmem_obstack)
	- first_dont_print;

      while (--i >= 0)
	{
	  if (VALUE_ADDRESS (val) == first_dont_print[i])
	    {
	      fputs_filtered ("<same as static member of an already seen type>",
			      stream);
	      return;
	    }
	}

      obstack_grow (&dont_print_statmem_obstack, (char *) &VALUE_ADDRESS (val),
		    sizeof (CORE_ADDR));

      CHECK_TYPEDEF (type);
      pascal_object_print_value_fields (type, VALUE_CONTENTS (val), VALUE_ADDRESS (val),
				  stream, format, recurse, pretty, NULL, 1);
      return;
    }
  common_val_print (val, stream, format, 0, recurse, pretty);
}

void
pascal_object_print_class_member (char *valaddr, struct type *domain,
				  struct ui_file *stream, char *prefix)
{

  /* VAL is a byte offset into the structure type DOMAIN.
     Find the name of the field for that offset and
     print it.  */
  int extra = 0;
  int bits = 0;
  unsigned int i;
  unsigned len = TYPE_NFIELDS (domain);
  /* @@ Make VAL into bit offset */
  LONGEST val = unpack_long (builtin_type_int, valaddr) << 3;
  for (i = TYPE_N_BASECLASSES (domain); i < len; i++)
    {
      int bitpos = TYPE_FIELD_BITPOS (domain, i);
      QUIT;
      if (val == bitpos)
	break;
      if (val < bitpos && i != 0)
	{
	  /* Somehow pointing into a field.  */
	  i -= 1;
	  extra = (val - TYPE_FIELD_BITPOS (domain, i));
	  if (extra & 0x7)
	    bits = 1;
	  else
	    extra >>= 3;
	  break;
	}
    }
  if (i < len)
    {
      char *name;
      fputs_filtered (prefix, stream);
      name = type_name_no_tag (domain);
      if (name)
	fputs_filtered (name, stream);
      else
	pascal_type_print_base (domain, stream, 0, 0);
      fprintf_filtered (stream, "::");
      fputs_filtered (TYPE_FIELD_NAME (domain, i), stream);
      if (extra)
	fprintf_filtered (stream, " + %d bytes", extra);
      if (bits)
	fprintf_filtered (stream, " (offset in bits)");
    }
  else
    fprintf_filtered (stream, "%ld", (long int) (val >> 3));
}

extern initialize_file_ftype _initialize_pascal_valprint; /* -Wmissing-prototypes */

void
_initialize_pascal_valprint (void)
{
  add_show_from_set
    (add_set_cmd ("pascal_static-members", class_support, var_boolean,
		  (char *) &pascal_static_field_print,
		  "Set printing of pascal static members.",
		  &setprintlist),
     &showprintlist);
  /* Turn on printing of static fields.  */
  pascal_static_field_print = 1;

}
