/* Support for printing Ada values for GDB, the GNU debugger.  
   Copyright 1986, 1988, 1989, 1991, 1992, 1993, 1994, 1997, 2001
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
Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.  */

#include <ctype.h>
#include "defs.h"
#include "symtab.h"
#include "gdbtypes.h"
#include "expression.h"
#include "value.h"
#include "demangle.h"
#include "valprint.h"
#include "language.h"
#include "annotate.h"
#include "ada-lang.h"
#include "c-lang.h"
#include "infcall.h"

/* Encapsulates arguments to ada_val_print. */
struct ada_val_print_args
{
  struct type *type;
  char *valaddr0;
  int embedded_offset;
  CORE_ADDR address;
  struct ui_file *stream;
  int format;
  int deref_ref;
  int recurse;
  enum val_prettyprint pretty;
};

static void print_record (struct type *, char *, struct ui_file *, int,
			  int, enum val_prettyprint);

static int print_field_values (struct type *, char *, struct ui_file *,
			       int, int, enum val_prettyprint,
			       int, struct type *, char *);

static int print_variant_part (struct type *, int, char *,
			       struct ui_file *, int, int,
			       enum val_prettyprint, int, struct type *,
			       char *);

static void val_print_packed_array_elements (struct type *, char *valaddr,
					     int, struct ui_file *, int, int,
					     enum val_prettyprint);

static void adjust_type_signedness (struct type *);

static int ada_val_print_stub (void *args0);

static int ada_val_print_1 (struct type *, char *, int, CORE_ADDR,
			    struct ui_file *, int, int, int,
			    enum val_prettyprint);


/* Make TYPE unsigned if its range of values includes no negatives. */
static void
adjust_type_signedness (struct type *type)
{
  if (type != NULL && TYPE_CODE (type) == TYPE_CODE_RANGE
      && TYPE_LOW_BOUND (type) >= 0)
    TYPE_FLAGS (type) |= TYPE_FLAG_UNSIGNED;
}

/* Assuming TYPE is a simple array type, prints its lower bound on STREAM,
   if non-standard (i.e., other than 1 for numbers, other than lower bound
   of index type for enumerated type). Returns 1 if something printed, 
   otherwise 0. */

static int
print_optional_low_bound (struct ui_file *stream, struct type *type)
{
  struct type *index_type;
  long low_bound;

  index_type = TYPE_INDEX_TYPE (type);
  low_bound = 0;

  if (index_type == NULL)
    return 0;
  if (TYPE_CODE (index_type) == TYPE_CODE_RANGE)
    {
      low_bound = TYPE_LOW_BOUND (index_type);
      index_type = TYPE_TARGET_TYPE (index_type);
    }
  else
    return 0;

  switch (TYPE_CODE (index_type))
    {
    case TYPE_CODE_ENUM:
      if (low_bound == TYPE_FIELD_BITPOS (index_type, 0))
	return 0;
      break;
    case TYPE_CODE_UNDEF:
      index_type = builtin_type_long;
      /* FALL THROUGH */
    default:
      if (low_bound == 1)
	return 0;
      break;
    }

  ada_print_scalar (index_type, (LONGEST) low_bound, stream);
  fprintf_filtered (stream, " => ");
  return 1;
}

/*  Version of val_print_array_elements for GNAT-style packed arrays.
    Prints elements of packed array of type TYPE at bit offset
    BITOFFSET from VALADDR on STREAM.  Formats according to FORMAT and
    separates with commas. RECURSE is the recursion (nesting) level.
    If PRETTY, uses "prettier" format. TYPE must have been decoded (as
    by ada_coerce_to_simple_array).  */

static void
val_print_packed_array_elements (struct type *type, char *valaddr,
				 int bitoffset, struct ui_file *stream,
				 int format, int recurse,
				 enum val_prettyprint pretty)
{
  unsigned int i;
  unsigned int things_printed = 0;
  unsigned len;
  struct type *elttype;
  unsigned eltlen;
  /* Position of the array element we are examining to see
     whether it is repeated.  */
  unsigned int rep1;
  /* Number of repetitions we have detected so far.  */
  unsigned int reps;
  unsigned long bitsize = TYPE_FIELD_BITSIZE (type, 0);
  struct value *mark = value_mark ();

  elttype = TYPE_TARGET_TYPE (type);
  eltlen = TYPE_LENGTH (check_typedef (elttype));

  {
    LONGEST low, high;
    if (get_discrete_bounds (TYPE_FIELD_TYPE (type, 0), &low, &high) < 0)
      len = 1;
    else
      len = high - low + 1;
  }

  i = 0;
  annotate_array_section_begin (i, elttype);

  while (i < len && things_printed < print_max)
    {
      struct value *v0, *v1;
      int i0;

      if (i != 0)
	{
	  if (prettyprint_arrays)
	    {
	      fprintf_filtered (stream, ",\n");
	      print_spaces_filtered (2 + 2 * recurse, stream);
	    }
	  else
	    {
	      fprintf_filtered (stream, ", ");
	    }
	}
      wrap_here (n_spaces (2 + 2 * recurse));

      i0 = i;
      v0 = ada_value_primitive_packed_val (NULL, valaddr,
					   (i0 * bitsize) / HOST_CHAR_BIT,
					   (i0 * bitsize) % HOST_CHAR_BIT,
					   bitsize, elttype);
      while (1)
	{
	  i += 1;
	  if (i >= len)
	    break;
	  v1 = ada_value_primitive_packed_val (NULL, valaddr,
					       (i * bitsize) / HOST_CHAR_BIT,
					       (i * bitsize) % HOST_CHAR_BIT,
					       bitsize, elttype);
	  if (memcmp (VALUE_CONTENTS (v0), VALUE_CONTENTS (v1), eltlen) != 0)
	    break;
	}

      if (i - i0 > repeat_count_threshold)
	{
	  val_print (elttype, VALUE_CONTENTS (v0), 0, 0, stream, format,
		     0, recurse + 1, pretty);
	  annotate_elt_rep (i - i0);
	  fprintf_filtered (stream, " <repeats %u times>", i - i0);
	  annotate_elt_rep_end ();

	}
      else
	{
	  int j;
	  for (j = i0; j < i; j += 1)
	    {
	      if (j > i0)
		{
		  if (prettyprint_arrays)
		    {
		      fprintf_filtered (stream, ",\n");
		      print_spaces_filtered (2 + 2 * recurse, stream);
		    }
		  else
		    {
		      fprintf_filtered (stream, ", ");
		    }
		  wrap_here (n_spaces (2 + 2 * recurse));
		}
	      val_print (elttype, VALUE_CONTENTS (v0), 0, 0, stream, format,
			 0, recurse + 1, pretty);
	      annotate_elt ();
	    }
	}
      things_printed += i - i0;
    }
  annotate_array_section_end ();
  if (i < len)
    {
      fprintf_filtered (stream, "...");
    }

  value_free_to_mark (mark);
}

static struct type *
printable_val_type (struct type *type, char *valaddr)
{
  return ada_to_fixed_type (ada_aligned_type (type), valaddr, 0, NULL);
}

/* Print the character C on STREAM as part of the contents of a literal
   string whose delimiter is QUOTER.  TYPE_LEN is the length in bytes
   (1 or 2) of the character. */

void
ada_emit_char (int c, struct ui_file *stream, int quoter, int type_len)
{
  if (type_len != 2)
    type_len = 1;

  c &= (1 << (type_len * TARGET_CHAR_BIT)) - 1;

  if (isascii (c) && isprint (c))
    {
      if (c == quoter && c == '"')
	fprintf_filtered (stream, "[\"%c\"]", quoter);
      else
	fprintf_filtered (stream, "%c", c);
    }
  else
    fprintf_filtered (stream, "[\"%0*x\"]", type_len * 2, c);
}

/* Character #I of STRING, given that TYPE_LEN is the size in bytes (1
   or 2) of a character. */

static int
char_at (char *string, int i, int type_len)
{
  if (type_len == 1)
    return string[i];
  else
    return (int) extract_unsigned_integer (string + 2 * i, 2);
}

void
ada_printchar (int c, struct ui_file *stream)
{
  fputs_filtered ("'", stream);
  ada_emit_char (c, stream, '\'', 1);
  fputs_filtered ("'", stream);
}

/* [From print_type_scalar in typeprint.c].   Print VAL on STREAM in a
   form appropriate for TYPE. */

void
ada_print_scalar (struct type *type, LONGEST val, struct ui_file *stream)
{
  unsigned int i;
  unsigned len;

  CHECK_TYPEDEF (type);

  switch (TYPE_CODE (type))
    {

    case TYPE_CODE_ENUM:
      len = TYPE_NFIELDS (type);
      for (i = 0; i < len; i++)
	{
	  if (TYPE_FIELD_BITPOS (type, i) == val)
	    {
	      break;
	    }
	}
      if (i < len)
	{
	  fputs_filtered (ada_enum_name (TYPE_FIELD_NAME (type, i)), stream);
	}
      else
	{
	  print_longest (stream, 'd', 0, val);
	}
      break;

    case TYPE_CODE_INT:
      print_longest (stream, TYPE_UNSIGNED (type) ? 'u' : 'd', 0, val);
      break;

    case TYPE_CODE_CHAR:
      LA_PRINT_CHAR ((unsigned char) val, stream);
      break;

    case TYPE_CODE_BOOL:
      fprintf_filtered (stream, val ? "true" : "false");
      break;

    case TYPE_CODE_RANGE:
      ada_print_scalar (TYPE_TARGET_TYPE (type), val, stream);
      return;

    case TYPE_CODE_UNDEF:
    case TYPE_CODE_PTR:
    case TYPE_CODE_ARRAY:
    case TYPE_CODE_STRUCT:
    case TYPE_CODE_UNION:
    case TYPE_CODE_FUNC:
    case TYPE_CODE_FLT:
    case TYPE_CODE_VOID:
    case TYPE_CODE_SET:
    case TYPE_CODE_STRING:
    case TYPE_CODE_ERROR:
    case TYPE_CODE_MEMBER:
    case TYPE_CODE_METHOD:
    case TYPE_CODE_REF:
      warning ("internal error: unhandled type in ada_print_scalar");
      break;

    default:
      error ("Invalid type code in symbol table.");
    }
  gdb_flush (stream);
}

/* Print the character string STRING, printing at most LENGTH characters.
   Printing stops early if the number hits print_max; repeat counts
   are printed as appropriate.  Print ellipses at the end if we
   had to stop before printing LENGTH characters, or if
   FORCE_ELLIPSES.   TYPE_LEN is the length (1 or 2) of the character type.
 */

static void
printstr (struct ui_file *stream, char *string, unsigned int length,
	  int force_ellipses, int type_len)
{
  unsigned int i;
  unsigned int things_printed = 0;
  int in_quotes = 0;
  int need_comma = 0;

  if (length == 0)
    {
      fputs_filtered ("\"\"", stream);
      return;
    }

  for (i = 0; i < length && things_printed < print_max; i += 1)
    {
      /* Position of the character we are examining
         to see whether it is repeated.  */
      unsigned int rep1;
      /* Number of repetitions we have detected so far.  */
      unsigned int reps;

      QUIT;

      if (need_comma)
	{
	  fputs_filtered (", ", stream);
	  need_comma = 0;
	}

      rep1 = i + 1;
      reps = 1;
      while (rep1 < length &&
	     char_at (string, rep1, type_len) == char_at (string, i,
							  type_len))
	{
	  rep1 += 1;
	  reps += 1;
	}

      if (reps > repeat_count_threshold)
	{
	  if (in_quotes)
	    {
	      if (inspect_it)
		fputs_filtered ("\\\", ", stream);
	      else
		fputs_filtered ("\", ", stream);
	      in_quotes = 0;
	    }
	  fputs_filtered ("'", stream);
	  ada_emit_char (char_at (string, i, type_len), stream, '\'',
			 type_len);
	  fputs_filtered ("'", stream);
	  fprintf_filtered (stream, " <repeats %u times>", reps);
	  i = rep1 - 1;
	  things_printed += repeat_count_threshold;
	  need_comma = 1;
	}
      else
	{
	  if (!in_quotes)
	    {
	      if (inspect_it)
		fputs_filtered ("\\\"", stream);
	      else
		fputs_filtered ("\"", stream);
	      in_quotes = 1;
	    }
	  ada_emit_char (char_at (string, i, type_len), stream, '"',
			 type_len);
	  things_printed += 1;
	}
    }

  /* Terminate the quotes if necessary.  */
  if (in_quotes)
    {
      if (inspect_it)
	fputs_filtered ("\\\"", stream);
      else
	fputs_filtered ("\"", stream);
    }

  if (force_ellipses || i < length)
    fputs_filtered ("...", stream);
}

void
ada_printstr (struct ui_file *stream, char *string, unsigned int length,
	      int force_ellipses, int width)
{
  printstr (stream, string, length, force_ellipses, width);
}


/* Print data of type TYPE located at VALADDR (within GDB), which came from
   the inferior at address ADDRESS, onto stdio stream STREAM according to
   FORMAT (a letter as for the printf % codes or 0 for natural format).  
   The data at VALADDR is in target byte order.

   If the data is printed as a string, returns the number of string characters
   printed.

   If DEREF_REF is nonzero, then dereference references, otherwise just print
   them like pointers.

   RECURSE indicates the amount of indentation to supply before
   continuation lines; this amount is roughly twice the value of RECURSE.

   When PRETTY is non-zero, prints record fields on separate lines.
   (For some reason, the current version of gdb instead uses a global
   variable---prettyprint_arrays--- to causes a similar effect on
   arrays.)  */

int
ada_val_print (struct type *type, char *valaddr0, int embedded_offset,
	       CORE_ADDR address, struct ui_file *stream, int format,
	       int deref_ref, int recurse, enum val_prettyprint pretty)
{
  struct ada_val_print_args args;
  args.type = type;
  args.valaddr0 = valaddr0;
  args.embedded_offset = embedded_offset;
  args.address = address;
  args.stream = stream;
  args.format = format;
  args.deref_ref = deref_ref;
  args.recurse = recurse;
  args.pretty = pretty;

  return catch_errors (ada_val_print_stub, &args, NULL, RETURN_MASK_ALL);
}

/* Helper for ada_val_print; used as argument to catch_errors to
   unmarshal the arguments to ada_val_print_1, which does the work. */
static int
ada_val_print_stub (void * args0)
{
  struct ada_val_print_args *argsp = (struct ada_val_print_args *) args0;
  return ada_val_print_1 (argsp->type, argsp->valaddr0,
			  argsp->embedded_offset, argsp->address,
			  argsp->stream, argsp->format, argsp->deref_ref,
			  argsp->recurse, argsp->pretty);
}

/* See the comment on ada_val_print.  This function differs in that it
 * does not catch evaluation errors (leaving that to ada_val_print). */

static int
ada_val_print_1 (struct type *type, char *valaddr0, int embedded_offset,
		 CORE_ADDR address, struct ui_file *stream, int format,
		 int deref_ref, int recurse, enum val_prettyprint pretty)
{
  unsigned int len;
  int i;
  struct type *elttype;
  unsigned int eltlen;
  LONGEST val;
  CORE_ADDR addr;
  char *valaddr = valaddr0 + embedded_offset;

  CHECK_TYPEDEF (type);

  if (ada_is_array_descriptor (type) || ada_is_packed_array_type (type))
    {
      int retn;
      struct value *mark = value_mark ();
      struct value *val;
      val = value_from_contents_and_address (type, valaddr, address);
      val = ada_coerce_to_simple_array_ptr (val);
      if (val == NULL)
	{
	  fprintf_filtered (stream, "(null)");
	  retn = 0;
	}
      else
	retn = ada_val_print_1 (VALUE_TYPE (val), VALUE_CONTENTS (val), 0,
				VALUE_ADDRESS (val), stream, format,
				deref_ref, recurse, pretty);
      value_free_to_mark (mark);
      return retn;
    }

  valaddr = ada_aligned_value_addr (type, valaddr);
  embedded_offset -= valaddr - valaddr0 - embedded_offset;
  type = printable_val_type (type, valaddr);

  switch (TYPE_CODE (type))
    {
    default:
      return c_val_print (type, valaddr0, embedded_offset, address, stream,
			  format, deref_ref, recurse, pretty);

    case TYPE_CODE_INT:
    case TYPE_CODE_RANGE:
      if (ada_is_fixed_point_type (type))
	{
	  LONGEST v = unpack_long (type, valaddr);
	  int len = TYPE_LENGTH (type);

	  fprintf_filtered (stream, len < 4 ? "%.11g" : "%.17g",
			    (double) ada_fixed_to_float (type, v));
	  return 0;
	}
      else if (ada_is_vax_floating_type (type))
	{
	  struct value *val =
	    value_from_contents_and_address (type, valaddr, address);
	  struct value *func = ada_vax_float_print_function (type);
	  if (func != 0)
	    {
	      static struct type *parray_of_char = NULL;
	      struct value *printable_val;

	      if (parray_of_char == NULL)
		parray_of_char =
		  make_pointer_type
		  (create_array_type
		   (NULL, builtin_type_char,
		    create_range_type (NULL, builtin_type_int, 0, 32)), NULL);

	      printable_val =
		value_ind (value_cast (parray_of_char,
				       call_function_by_hand (func, 1,
							      &val)));

	      fprintf_filtered (stream, "%s", VALUE_CONTENTS (printable_val));
	      return 0;
	    }
	  /* No special printing function.  Do as best we can. */
	}
      else if (TYPE_CODE (type) == TYPE_CODE_RANGE)
	{
	  struct type *target_type = TYPE_TARGET_TYPE (type);
	  if (TYPE_LENGTH (type) != TYPE_LENGTH (target_type))
	    {
	      /* Obscure case of range type that has different length from
	         its base type.  Perform a conversion, or we will get a
	         nonsense value.  Actually, we could use the same
	         code regardless of lengths; I'm just avoiding a cast. */
	      struct value *v = value_cast (target_type,
					    value_from_contents_and_address
					    (type, valaddr, 0));
	      return ada_val_print_1 (target_type, VALUE_CONTENTS (v), 0, 0,
				      stream, format, 0, recurse + 1, pretty);
	    }
	  else
	    return ada_val_print_1 (TYPE_TARGET_TYPE (type),
				    valaddr0, embedded_offset,
				    address, stream, format, deref_ref,
				    recurse, pretty);
	}
      else
	{
	  format = format ? format : output_format;
	  if (format)
	    {
	      print_scalar_formatted (valaddr, type, format, 0, stream);
	    }
	  else
	    {
	      val_print_type_code_int (type, valaddr, stream);
	      if (ada_is_character_type (type))
		{
		  fputs_filtered (" ", stream);
		  ada_printchar ((unsigned char) unpack_long (type, valaddr),
				 stream);
		}
	    }
	  return 0;
	}

    case TYPE_CODE_ENUM:
      if (format)
	{
	  print_scalar_formatted (valaddr, type, format, 0, stream);
	  break;
	}
      len = TYPE_NFIELDS (type);
      val = unpack_long (type, valaddr);
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
	  const char *name = ada_enum_name (TYPE_FIELD_NAME (type, i));
	  if (name[0] == '\'')
	    fprintf_filtered (stream, "%ld %s", (long) val, name);
	  else
	    fputs_filtered (name, stream);
	}
      else
	{
	  print_longest (stream, 'd', 0, val);
	}
      break;

    case TYPE_CODE_UNION:
    case TYPE_CODE_STRUCT:
      if (ada_is_bogus_array_descriptor (type))
	{
	  fprintf_filtered (stream, "(...?)");
	  return 0;
	}
      else
	{
	  print_record (type, valaddr, stream, format, recurse, pretty);
	  return 0;
	}

    case TYPE_CODE_ARRAY:
      if (TYPE_LENGTH (type) > 0 && TYPE_LENGTH (TYPE_TARGET_TYPE (type)) > 0)
	{
	  elttype = TYPE_TARGET_TYPE (type);
	  eltlen = TYPE_LENGTH (elttype);
	  len = TYPE_LENGTH (type) / eltlen;

	  /* For an array of chars, print with string syntax.  */
	  if (ada_is_string_type (type) && (format == 0 || format == 's'))
	    {
	      if (prettyprint_arrays)
		{
		  print_spaces_filtered (2 + 2 * recurse, stream);
		}
	      /* If requested, look for the first null char and only print
	         elements up to it.  */
	      if (stop_print_at_null)
		{
		  int temp_len;

		  /* Look for a NULL char. */
		  for (temp_len = 0;
		       temp_len < len && temp_len < print_max
		       && char_at (valaddr, temp_len, eltlen) != 0;
		       temp_len += 1);
		  len = temp_len;
		}

	      printstr (stream, valaddr, len, 0, eltlen);
	    }
	  else
	    {
	      len = 0;
	      fprintf_filtered (stream, "(");
	      print_optional_low_bound (stream, type);
	      if (TYPE_FIELD_BITSIZE (type, 0) > 0)
		val_print_packed_array_elements (type, valaddr, 0, stream,
						 format, recurse, pretty);
	      else
		val_print_array_elements (type, valaddr, address, stream,
					  format, deref_ref, recurse,
					  pretty, 0);
	      fprintf_filtered (stream, ")");
	    }
	  gdb_flush (stream);
	  return len;
	}

    case TYPE_CODE_REF:
      elttype = check_typedef (TYPE_TARGET_TYPE (type));
      if (addressprint)
	{
	  fprintf_filtered (stream, "@");
	  /* Extract an address, assume that the address is unsigned.  */
	  print_address_numeric
	    (extract_unsigned_integer (valaddr,
				       TARGET_PTR_BIT / HOST_CHAR_BIT),
	     1, stream);
	  if (deref_ref)
	    fputs_filtered (": ", stream);
	}
      /* De-reference the reference */
      if (deref_ref)
	{
	  if (TYPE_CODE (elttype) != TYPE_CODE_UNDEF)
	    {
	      LONGEST deref_val_int = (LONGEST)
		unpack_pointer (lookup_pointer_type (builtin_type_void),
				valaddr);
	      if (deref_val_int != 0)
		{
		  struct value *deref_val =
		    ada_value_ind (value_from_longest
				   (lookup_pointer_type (elttype),
				    deref_val_int));
		  val_print (VALUE_TYPE (deref_val),
			     VALUE_CONTENTS (deref_val), 0,
			     VALUE_ADDRESS (deref_val), stream, format,
			     deref_ref, recurse + 1, pretty);
		}
	      else
		fputs_filtered ("(null)", stream);
	    }
	  else
	    fputs_filtered ("???", stream);
	}
      break;
    }
  return 0;
}

static int
print_variant_part (struct type *type, int field_num, char *valaddr,
		    struct ui_file *stream, int format, int recurse,
		    enum val_prettyprint pretty, int comma_needed,
		    struct type *outer_type, char *outer_valaddr)
{
  struct type *var_type = TYPE_FIELD_TYPE (type, field_num);
  int which = ada_which_variant_applies (var_type, outer_type, outer_valaddr);

  if (which < 0)
    return 0;
  else
    return print_field_values
      (TYPE_FIELD_TYPE (var_type, which),
       valaddr + TYPE_FIELD_BITPOS (type, field_num) / HOST_CHAR_BIT
       + TYPE_FIELD_BITPOS (var_type, which) / HOST_CHAR_BIT,
       stream, format, recurse, pretty,
       comma_needed, outer_type, outer_valaddr);
}

int
ada_value_print (struct value *val0, struct ui_file *stream, int format,
		 enum val_prettyprint pretty)
{
  char *valaddr = VALUE_CONTENTS (val0);
  CORE_ADDR address = VALUE_ADDRESS (val0) + VALUE_OFFSET (val0);
  struct type *type =
    ada_to_fixed_type (VALUE_TYPE (val0), valaddr, address, NULL);
  struct value *val =
    value_from_contents_and_address (type, valaddr, address);

  /* If it is a pointer, indicate what it points to. */
  if (TYPE_CODE (type) == TYPE_CODE_PTR || TYPE_CODE (type) == TYPE_CODE_REF)
    {
      /* Hack:  remove (char *) for char strings.  Their
         type is indicated by the quoted string anyway. */
      if (TYPE_CODE (type) == TYPE_CODE_PTR &&
	  TYPE_LENGTH (TYPE_TARGET_TYPE (type)) == sizeof (char) &&
	  TYPE_CODE (TYPE_TARGET_TYPE (type)) == TYPE_CODE_INT &&
	  !TYPE_UNSIGNED (TYPE_TARGET_TYPE (type)))
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
  else if (ada_is_array_descriptor (type))
    {
      fprintf_filtered (stream, "(");
      type_print (type, "", stream, -1);
      fprintf_filtered (stream, ") ");
    }
  else if (ada_is_bogus_array_descriptor (type))
    {
      fprintf_filtered (stream, "(");
      type_print (type, "", stream, -1);
      fprintf_filtered (stream, ") (...?)");
      return 0;
    }
  return (val_print (type, VALUE_CONTENTS (val), 0, address,
		     stream, format, 1, 0, pretty));
}

static void
print_record (struct type *type, char *valaddr, struct ui_file *stream,
	      int format, int recurse, enum val_prettyprint pretty)
{
  CHECK_TYPEDEF (type);

  fprintf_filtered (stream, "(");

  if (print_field_values (type, valaddr, stream, format, recurse, pretty,
			  0, type, valaddr) != 0 && pretty)
    {
      fprintf_filtered (stream, "\n");
      print_spaces_filtered (2 * recurse, stream);
    }

  fprintf_filtered (stream, ")");
}

/* Print out fields of value at VALADDR having structure type TYPE.
  
   TYPE, VALADDR, STREAM, FORMAT, RECURSE, and PRETTY have the
   same meanings as in ada_print_value and ada_val_print.   

   OUTER_TYPE and OUTER_VALADDR give type and address of enclosing record
   (used to get discriminant values when printing variant parts).

   COMMA_NEEDED is 1 if fields have been printed at the current recursion 
   level, so that a comma is needed before any field printed by this
   call. 

   Returns 1 if COMMA_NEEDED or any fields were printed. */

static int
print_field_values (struct type *type, char *valaddr, struct ui_file *stream,
		    int format, int recurse, enum val_prettyprint pretty,
		    int comma_needed, struct type *outer_type,
		    char *outer_valaddr)
{
  int i, len;

  len = TYPE_NFIELDS (type);

  for (i = 0; i < len; i += 1)
    {
      if (ada_is_ignored_field (type, i))
	continue;

      if (ada_is_wrapper_field (type, i))
	{
	  comma_needed =
	    print_field_values (TYPE_FIELD_TYPE (type, i),
				valaddr
				+ TYPE_FIELD_BITPOS (type, i) / HOST_CHAR_BIT,
				stream, format, recurse, pretty,
				comma_needed, type, valaddr);
	  continue;
	}
      else if (ada_is_variant_part (type, i))
	{
	  comma_needed =
	    print_variant_part (type, i, valaddr,
				stream, format, recurse, pretty, comma_needed,
				outer_type, outer_valaddr);
	  continue;
	}

      if (comma_needed)
	fprintf_filtered (stream, ", ");
      comma_needed = 1;

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
	  fprintf_symbol_filtered (stream, TYPE_FIELD_NAME (type, i),
				   language_cplus, DMGL_NO_OPTS);
	  fputs_filtered ("\" \"", stream);
	  fprintf_symbol_filtered (stream, TYPE_FIELD_NAME (type, i),
				   language_cplus, DMGL_NO_OPTS);
	  fputs_filtered ("\") \"", stream);
	}
      else
	{
	  annotate_field_begin (TYPE_FIELD_TYPE (type, i));
	  fprintf_filtered (stream, "%.*s",
			    ada_name_prefix_len (TYPE_FIELD_NAME (type, i)),
			    TYPE_FIELD_NAME (type, i));
	  annotate_field_name_end ();
	  fputs_filtered (" => ", stream);
	  annotate_field_value ();
	}

      if (TYPE_FIELD_PACKED (type, i))
	{
	  struct value *v;

	  /* Bitfields require special handling, especially due to byte
	     order problems.  */
	  if (TYPE_CPLUS_SPECIFIC (type) != NULL
	      && TYPE_FIELD_IGNORE (type, i))
	    {
	      fputs_filtered ("<optimized out or zero length>", stream);
	    }
	  else
	    {
	      int bit_pos = TYPE_FIELD_BITPOS (type, i);
	      int bit_size = TYPE_FIELD_BITSIZE (type, i);

	      adjust_type_signedness (TYPE_FIELD_TYPE (type, i));
	      v = ada_value_primitive_packed_val (NULL, valaddr,
						  bit_pos / HOST_CHAR_BIT,
						  bit_pos % HOST_CHAR_BIT,
						  bit_size,
						  TYPE_FIELD_TYPE (type, i));
	      val_print (TYPE_FIELD_TYPE (type, i), VALUE_CONTENTS (v), 0, 0,
			 stream, format, 0, recurse + 1, pretty);
	    }
	}
      else
	ada_val_print (TYPE_FIELD_TYPE (type, i),
		       valaddr + TYPE_FIELD_BITPOS (type, i) / HOST_CHAR_BIT,
		       0, 0, stream, format, 0, recurse + 1, pretty);
      annotate_field_end ();
    }

  return comma_needed;
}
