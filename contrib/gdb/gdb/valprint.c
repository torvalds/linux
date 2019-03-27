/* Print values for GDB, the GNU debugger.

   Copyright 1986, 1988, 1989, 1990, 1991, 1992, 1993, 1994, 1995,
   1996, 1997, 1998, 1999, 2000, 2001, 2002 Free Software Foundation,
   Inc.

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
#include "gdb_string.h"
#include "symtab.h"
#include "gdbtypes.h"
#include "value.h"
#include "gdbcore.h"
#include "gdbcmd.h"
#include "target.h"
#include "language.h"
#include "annotate.h"
#include "valprint.h"
#include "floatformat.h"
#include "doublest.h"

#include <errno.h>

/* Prototypes for local functions */

static int partial_memory_read (CORE_ADDR memaddr, char *myaddr,
				int len, int *errnoptr);

static void show_print (char *, int);

static void set_print (char *, int);

static void set_radix (char *, int);

static void show_radix (char *, int);

static void set_input_radix (char *, int, struct cmd_list_element *);

static void set_input_radix_1 (int, unsigned);

static void set_output_radix (char *, int, struct cmd_list_element *);

static void set_output_radix_1 (int, unsigned);

void _initialize_valprint (void);

/* Maximum number of chars to print for a string pointer value or vector
   contents, or UINT_MAX for no limit.  Note that "set print elements 0"
   stores UINT_MAX in print_max, which displays in a show command as
   "unlimited". */

unsigned int print_max;
#define PRINT_MAX_DEFAULT 200	/* Start print_max off at this value. */

/* Default input and output radixes, and output format letter.  */

unsigned input_radix = 10;
unsigned output_radix = 10;
int output_format = 0;

/* Print repeat counts if there are more than this many repetitions of an
   element in an array.  Referenced by the low level language dependent
   print routines. */

unsigned int repeat_count_threshold = 10;

/* If nonzero, stops printing of char arrays at first null. */

int stop_print_at_null;

/* Controls pretty printing of structures. */

int prettyprint_structs;

/* Controls pretty printing of arrays.  */

int prettyprint_arrays;

/* If nonzero, causes unions inside structures or other unions to be
   printed. */

int unionprint;			/* Controls printing of nested unions.  */

/* If nonzero, causes machine addresses to be printed in certain contexts. */

int addressprint;		/* Controls printing of machine addresses */


/* Print data of type TYPE located at VALADDR (within GDB), which came from
   the inferior at address ADDRESS, onto stdio stream STREAM according to
   FORMAT (a letter, or 0 for natural format using TYPE).

   If DEREF_REF is nonzero, then dereference references, otherwise just print
   them like pointers.

   The PRETTY parameter controls prettyprinting.

   If the data are a string pointer, returns the number of string characters
   printed.

   FIXME:  The data at VALADDR is in target byte order.  If gdb is ever
   enhanced to be able to debug more than the single target it was compiled
   for (specific CPU type and thus specific target byte ordering), then
   either the print routines are going to have to take this into account,
   or the data is going to have to be passed into here already converted
   to the host byte ordering, whichever is more convenient. */


int
val_print (struct type *type, char *valaddr, int embedded_offset,
	   CORE_ADDR address, struct ui_file *stream, int format, int deref_ref,
	   int recurse, enum val_prettyprint pretty)
{
  struct type *real_type = check_typedef (type);
  if (pretty == Val_pretty_default)
    {
      pretty = prettyprint_structs ? Val_prettyprint : Val_no_prettyprint;
    }

  QUIT;

  /* Ensure that the type is complete and not just a stub.  If the type is
     only a stub and we can't find and substitute its complete type, then
     print appropriate string and return.  */

  if (TYPE_STUB (real_type))
    {
      fprintf_filtered (stream, "<incomplete type>");
      gdb_flush (stream);
      return (0);
    }

  return (LA_VAL_PRINT (type, valaddr, embedded_offset, address,
			stream, format, deref_ref, recurse, pretty));
}

/* Check whether the value VAL is printable.  Return 1 if it is;
   return 0 and print an appropriate error message to STREAM if it
   is not.  */

static int
value_check_printable (struct value *val, struct ui_file *stream)
{
  if (val == 0)
    {
      fprintf_filtered (stream, "<address of value unknown>");
      return 0;
    }

  if (VALUE_OPTIMIZED_OUT (val))
    {
      fprintf_filtered (stream, "<value optimized out>");
      return 0;
    }

  return 1;
}

/* Print the value VAL onto stream STREAM according to FORMAT (a
   letter, or 0 for natural format using TYPE).

   If DEREF_REF is nonzero, then dereference references, otherwise just print
   them like pointers.

   The PRETTY parameter controls prettyprinting.

   If the data are a string pointer, returns the number of string characters
   printed.

   This is a preferable interface to val_print, above, because it uses
   GDB's value mechanism.  */

int
common_val_print (struct value *val, struct ui_file *stream, int format,
		  int deref_ref, int recurse, enum val_prettyprint pretty)
{
  if (!value_check_printable (val, stream))
    return 0;

  return val_print (VALUE_TYPE(val), VALUE_CONTENTS_ALL (val),
		    VALUE_EMBEDDED_OFFSET (val), VALUE_ADDRESS (val),
		    stream, format, deref_ref, recurse, pretty);
}

/* Print the value VAL in C-ish syntax on stream STREAM.
   FORMAT is a format-letter, or 0 for print in natural format of data type.
   If the object printed is a string pointer, returns
   the number of string bytes printed.  */

int
value_print (struct value *val, struct ui_file *stream, int format,
	     enum val_prettyprint pretty)
{
  if (!value_check_printable (val, stream))
    return 0;

  return LA_VALUE_PRINT (val, stream, format, pretty);
}

/* Called by various <lang>_val_print routines to print
   TYPE_CODE_INT's.  TYPE is the type.  VALADDR is the address of the
   value.  STREAM is where to print the value.  */

void
val_print_type_code_int (struct type *type, char *valaddr,
			 struct ui_file *stream)
{
  if (TYPE_LENGTH (type) > sizeof (LONGEST))
    {
      LONGEST val;

      if (TYPE_UNSIGNED (type)
	  && extract_long_unsigned_integer (valaddr, TYPE_LENGTH (type),
					    &val))
	{
	  print_longest (stream, 'u', 0, val);
	}
      else
	{
	  /* Signed, or we couldn't turn an unsigned value into a
	     LONGEST.  For signed values, one could assume two's
	     complement (a reasonable assumption, I think) and do
	     better than this.  */
	  print_hex_chars (stream, (unsigned char *) valaddr,
			   TYPE_LENGTH (type));
	}
    }
  else
    {
      print_longest (stream, TYPE_UNSIGNED (type) ? 'u' : 'd', 0,
		     unpack_long (type, valaddr));
    }
}

/* Print a number according to FORMAT which is one of d,u,x,o,b,h,w,g.
   The raison d'etre of this function is to consolidate printing of 
   LONG_LONG's into this one function.  Some platforms have long longs but
   don't have a printf() that supports "ll" in the format string.  We handle
   these by seeing if the number is representable as either a signed or
   unsigned long, depending upon what format is desired, and if not we just
   bail out and print the number in hex.

   The format chars b,h,w,g are from print_scalar_formatted().  If USE_LOCAL,
   format it according to the current language (this should be used for most
   integers which GDB prints, the exception is things like protocols where
   the format of the integer is a protocol thing, not a user-visible thing).
 */

#if defined (CC_HAS_LONG_LONG) && !defined (PRINTF_HAS_LONG_LONG)
static void print_decimal (struct ui_file * stream, char *sign,
			   int use_local, ULONGEST val_ulong);
static void
print_decimal (struct ui_file *stream, char *sign, int use_local,
	       ULONGEST val_ulong)
{
  unsigned long temp[3];
  int i = 0;
  do
    {
      temp[i] = val_ulong % (1000 * 1000 * 1000);
      val_ulong /= (1000 * 1000 * 1000);
      i++;
    }
  while (val_ulong != 0 && i < (sizeof (temp) / sizeof (temp[0])));
  switch (i)
    {
    case 1:
      fprintf_filtered (stream, "%s%lu",
			sign, temp[0]);
      break;
    case 2:
      fprintf_filtered (stream, "%s%lu%09lu",
			sign, temp[1], temp[0]);
      break;
    case 3:
      fprintf_filtered (stream, "%s%lu%09lu%09lu",
			sign, temp[2], temp[1], temp[0]);
      break;
    default:
      internal_error (__FILE__, __LINE__, "failed internal consistency check");
    }
  return;
}
#endif

void
print_longest (struct ui_file *stream, int format, int use_local,
	       LONGEST val_long)
{
#if defined (CC_HAS_LONG_LONG) && !defined (PRINTF_HAS_LONG_LONG)
  if (sizeof (long) < sizeof (LONGEST))
    {
      switch (format)
	{
	case 'd':
	  {
	    /* Print a signed value, that doesn't fit in a long */
	    if ((long) val_long != val_long)
	      {
		if (val_long < 0)
		  print_decimal (stream, "-", use_local, -val_long);
		else
		  print_decimal (stream, "", use_local, val_long);
		return;
	      }
	    break;
	  }
	case 'u':
	  {
	    /* Print an unsigned value, that doesn't fit in a long */
	    if ((unsigned long) val_long != (ULONGEST) val_long)
	      {
		print_decimal (stream, "", use_local, val_long);
		return;
	      }
	    break;
	  }
	case 'x':
	case 'o':
	case 'b':
	case 'h':
	case 'w':
	case 'g':
	  /* Print as unsigned value, must fit completely in unsigned long */
	  {
	    unsigned long temp = val_long;
	    if (temp != val_long)
	      {
		/* Urk, can't represent value in long so print in hex.
		   Do shift in two operations so that if sizeof (long)
		   == sizeof (LONGEST) we can avoid warnings from
		   picky compilers about shifts >= the size of the
		   shiftee in bits */
		unsigned long vbot = (unsigned long) val_long;
		LONGEST temp = (val_long >> (sizeof (long) * HOST_CHAR_BIT - 1));
		unsigned long vtop = temp >> 1;
		fprintf_filtered (stream, "0x%lx%08lx", vtop, vbot);
		return;
	      }
	    break;
	  }
	}
    }
#endif

#if defined (CC_HAS_LONG_LONG) && defined (PRINTF_HAS_LONG_LONG)
  switch (format)
    {
    case 'd':
      fprintf_filtered (stream,
			use_local ? local_decimal_format_custom ("ll")
			: "%lld",
			(long long) val_long);
      break;
    case 'u':
      fprintf_filtered (stream, "%llu", (long long) val_long);
      break;
    case 'x':
      fprintf_filtered (stream,
			use_local ? local_hex_format_custom ("ll")
			: "%llx",
			(unsigned long long) val_long);
      break;
    case 'o':
      fprintf_filtered (stream,
			use_local ? local_octal_format_custom ("ll")
			: "%llo",
			(unsigned long long) val_long);
      break;
    case 'b':
      fprintf_filtered (stream, local_hex_format_custom ("02ll"), val_long);
      break;
    case 'h':
      fprintf_filtered (stream, local_hex_format_custom ("04ll"), val_long);
      break;
    case 'w':
      fprintf_filtered (stream, local_hex_format_custom ("08ll"), val_long);
      break;
    case 'g':
      fprintf_filtered (stream, local_hex_format_custom ("016ll"), val_long);
      break;
    default:
      internal_error (__FILE__, __LINE__, "failed internal consistency check");
    }
#else /* !CC_HAS_LONG_LONG || !PRINTF_HAS_LONG_LONG */
  /* In the following it is important to coerce (val_long) to a long. It does
     nothing if !LONG_LONG, but it will chop off the top half (which we know
     we can ignore) if the host supports long longs.  */

  switch (format)
    {
    case 'd':
      fprintf_filtered (stream,
			use_local ? local_decimal_format_custom ("l")
			: "%ld",
			(long) val_long);
      break;
    case 'u':
      fprintf_filtered (stream, "%lu", (unsigned long) val_long);
      break;
    case 'x':
      fprintf_filtered (stream,
			use_local ? local_hex_format_custom ("l")
			: "%lx",
			(unsigned long) val_long);
      break;
    case 'o':
      fprintf_filtered (stream,
			use_local ? local_octal_format_custom ("l")
			: "%lo",
			(unsigned long) val_long);
      break;
    case 'b':
      fprintf_filtered (stream, local_hex_format_custom ("02l"),
			(unsigned long) val_long);
      break;
    case 'h':
      fprintf_filtered (stream, local_hex_format_custom ("04l"),
			(unsigned long) val_long);
      break;
    case 'w':
      fprintf_filtered (stream, local_hex_format_custom ("08l"),
			(unsigned long) val_long);
      break;
    case 'g':
      fprintf_filtered (stream, local_hex_format_custom ("016l"),
			(unsigned long) val_long);
      break;
    default:
      internal_error (__FILE__, __LINE__, "failed internal consistency check");
    }
#endif /* CC_HAS_LONG_LONG || PRINTF_HAS_LONG_LONG */
}

/* This used to be a macro, but I don't think it is called often enough
   to merit such treatment.  */
/* Convert a LONGEST to an int.  This is used in contexts (e.g. number of
   arguments to a function, number in a value history, register number, etc.)
   where the value must not be larger than can fit in an int.  */

int
longest_to_int (LONGEST arg)
{
  /* Let the compiler do the work */
  int rtnval = (int) arg;

  /* Check for overflows or underflows */
  if (sizeof (LONGEST) > sizeof (int))
    {
      if (rtnval != arg)
	{
	  error ("Value out of range.");
	}
    }
  return (rtnval);
}

/* Print a floating point value of type TYPE (not always a
   TYPE_CODE_FLT), pointed to in GDB by VALADDR, on STREAM.  */

void
print_floating (char *valaddr, struct type *type, struct ui_file *stream)
{
  DOUBLEST doub;
  int inv;
  const struct floatformat *fmt = NULL;
  unsigned len = TYPE_LENGTH (type);

  /* If it is a floating-point, check for obvious problems.  */
  if (TYPE_CODE (type) == TYPE_CODE_FLT)
    fmt = floatformat_from_type (type);
  if (fmt != NULL && floatformat_is_nan (fmt, valaddr))
    {
      if (floatformat_is_negative (fmt, valaddr))
	fprintf_filtered (stream, "-");
      fprintf_filtered (stream, "nan(");
      fputs_filtered (local_hex_format_prefix (), stream);
      fputs_filtered (floatformat_mantissa (fmt, valaddr), stream);
      fputs_filtered (local_hex_format_suffix (), stream);
      fprintf_filtered (stream, ")");
      return;
    }

  /* NOTE: cagney/2002-01-15: The TYPE passed into print_floating()
     isn't necessarily a TYPE_CODE_FLT.  Consequently, unpack_double
     needs to be used as that takes care of any necessary type
     conversions.  Such conversions are of course direct to DOUBLEST
     and disregard any possible target floating point limitations.
     For instance, a u64 would be converted and displayed exactly on a
     host with 80 bit DOUBLEST but with loss of information on a host
     with 64 bit DOUBLEST.  */

  doub = unpack_double (type, valaddr, &inv);
  if (inv)
    {
      fprintf_filtered (stream, "<invalid float value>");
      return;
    }

  /* FIXME: kettenis/2001-01-20: The following code makes too much
     assumptions about the host and target floating point format.  */

  /* NOTE: cagney/2002-02-03: Since the TYPE of what was passed in may
     not necessarially be a TYPE_CODE_FLT, the below ignores that and
     instead uses the type's length to determine the precision of the
     floating-point value being printed.  */

  if (len < sizeof (double))
      fprintf_filtered (stream, "%.9g", (double) doub);
  else if (len == sizeof (double))
      fprintf_filtered (stream, "%.17g", (double) doub);
  else
#ifdef PRINTF_HAS_LONG_DOUBLE
    fprintf_filtered (stream, "%.35Lg", doub);
#else
    /* This at least wins with values that are representable as
       doubles.  */
    fprintf_filtered (stream, "%.17g", (double) doub);
#endif
}

void
print_binary_chars (struct ui_file *stream, unsigned char *valaddr,
		    unsigned len)
{

#define BITS_IN_BYTES 8

  unsigned char *p;
  unsigned int i;
  int b;

  /* Declared "int" so it will be signed.
   * This ensures that right shift will shift in zeros.
   */
  const int mask = 0x080;

  /* FIXME: We should be not printing leading zeroes in most cases.  */

  fputs_filtered (local_binary_format_prefix (), stream);
  if (TARGET_BYTE_ORDER == BFD_ENDIAN_BIG)
    {
      for (p = valaddr;
	   p < valaddr + len;
	   p++)
	{
	  /* Every byte has 8 binary characters; peel off
	   * and print from the MSB end.
	   */
	  for (i = 0; i < (BITS_IN_BYTES * sizeof (*p)); i++)
	    {
	      if (*p & (mask >> i))
		b = 1;
	      else
		b = 0;

	      fprintf_filtered (stream, "%1d", b);
	    }
	}
    }
  else
    {
      for (p = valaddr + len - 1;
	   p >= valaddr;
	   p--)
	{
	  for (i = 0; i < (BITS_IN_BYTES * sizeof (*p)); i++)
	    {
	      if (*p & (mask >> i))
		b = 1;
	      else
		b = 0;

	      fprintf_filtered (stream, "%1d", b);
	    }
	}
    }
  fputs_filtered (local_binary_format_suffix (), stream);
}

/* VALADDR points to an integer of LEN bytes.
 * Print it in octal on stream or format it in buf.
 */
void
print_octal_chars (struct ui_file *stream, unsigned char *valaddr, unsigned len)
{
  unsigned char *p;
  unsigned char octa1, octa2, octa3, carry;
  int cycle;

  /* FIXME: We should be not printing leading zeroes in most cases.  */


  /* Octal is 3 bits, which doesn't fit.  Yuk.  So we have to track
   * the extra bits, which cycle every three bytes:
   *
   * Byte side:       0            1             2          3
   *                         |             |            |            |
   * bit number   123 456 78 | 9 012 345 6 | 78 901 234 | 567 890 12 |
   *
   * Octal side:   0   1   carry  3   4  carry ...
   *
   * Cycle number:    0             1            2
   *
   * But of course we are printing from the high side, so we have to
   * figure out where in the cycle we are so that we end up with no
   * left over bits at the end.
   */
#define BITS_IN_OCTAL 3
#define HIGH_ZERO     0340
#define LOW_ZERO      0016
#define CARRY_ZERO    0003
#define HIGH_ONE      0200
#define MID_ONE       0160
#define LOW_ONE       0016
#define CARRY_ONE     0001
#define HIGH_TWO      0300
#define MID_TWO       0070
#define LOW_TWO       0007

  /* For 32 we start in cycle 2, with two bits and one bit carry;
   * for 64 in cycle in cycle 1, with one bit and a two bit carry.
   */
  cycle = (len * BITS_IN_BYTES) % BITS_IN_OCTAL;
  carry = 0;

  fputs_filtered (local_octal_format_prefix (), stream);
  if (TARGET_BYTE_ORDER == BFD_ENDIAN_BIG)
    {
      for (p = valaddr;
	   p < valaddr + len;
	   p++)
	{
	  switch (cycle)
	    {
	    case 0:
	      /* No carry in, carry out two bits.
	       */
	      octa1 = (HIGH_ZERO & *p) >> 5;
	      octa2 = (LOW_ZERO & *p) >> 2;
	      carry = (CARRY_ZERO & *p);
	      fprintf_filtered (stream, "%o", octa1);
	      fprintf_filtered (stream, "%o", octa2);
	      break;

	    case 1:
	      /* Carry in two bits, carry out one bit.
	       */
	      octa1 = (carry << 1) | ((HIGH_ONE & *p) >> 7);
	      octa2 = (MID_ONE & *p) >> 4;
	      octa3 = (LOW_ONE & *p) >> 1;
	      carry = (CARRY_ONE & *p);
	      fprintf_filtered (stream, "%o", octa1);
	      fprintf_filtered (stream, "%o", octa2);
	      fprintf_filtered (stream, "%o", octa3);
	      break;

	    case 2:
	      /* Carry in one bit, no carry out.
	       */
	      octa1 = (carry << 2) | ((HIGH_TWO & *p) >> 6);
	      octa2 = (MID_TWO & *p) >> 3;
	      octa3 = (LOW_TWO & *p);
	      carry = 0;
	      fprintf_filtered (stream, "%o", octa1);
	      fprintf_filtered (stream, "%o", octa2);
	      fprintf_filtered (stream, "%o", octa3);
	      break;

	    default:
	      error ("Internal error in octal conversion;");
	    }

	  cycle++;
	  cycle = cycle % BITS_IN_OCTAL;
	}
    }
  else
    {
      for (p = valaddr + len - 1;
	   p >= valaddr;
	   p--)
	{
	  switch (cycle)
	    {
	    case 0:
	      /* Carry out, no carry in */
	      octa1 = (HIGH_ZERO & *p) >> 5;
	      octa2 = (LOW_ZERO & *p) >> 2;
	      carry = (CARRY_ZERO & *p);
	      fprintf_filtered (stream, "%o", octa1);
	      fprintf_filtered (stream, "%o", octa2);
	      break;

	    case 1:
	      /* Carry in, carry out */
	      octa1 = (carry << 1) | ((HIGH_ONE & *p) >> 7);
	      octa2 = (MID_ONE & *p) >> 4;
	      octa3 = (LOW_ONE & *p) >> 1;
	      carry = (CARRY_ONE & *p);
	      fprintf_filtered (stream, "%o", octa1);
	      fprintf_filtered (stream, "%o", octa2);
	      fprintf_filtered (stream, "%o", octa3);
	      break;

	    case 2:
	      /* Carry in, no carry out */
	      octa1 = (carry << 2) | ((HIGH_TWO & *p) >> 6);
	      octa2 = (MID_TWO & *p) >> 3;
	      octa3 = (LOW_TWO & *p);
	      carry = 0;
	      fprintf_filtered (stream, "%o", octa1);
	      fprintf_filtered (stream, "%o", octa2);
	      fprintf_filtered (stream, "%o", octa3);
	      break;

	    default:
	      error ("Internal error in octal conversion;");
	    }

	  cycle++;
	  cycle = cycle % BITS_IN_OCTAL;
	}
    }

  fputs_filtered (local_octal_format_suffix (), stream);
}

/* VALADDR points to an integer of LEN bytes.
 * Print it in decimal on stream or format it in buf.
 */
void
print_decimal_chars (struct ui_file *stream, unsigned char *valaddr,
		     unsigned len)
{
#define TEN             10
#define TWO_TO_FOURTH   16
#define CARRY_OUT(  x ) ((x) / TEN)	/* extend char to int */
#define CARRY_LEFT( x ) ((x) % TEN)
#define SHIFT( x )      ((x) << 4)
#define START_P \
        ((TARGET_BYTE_ORDER == BFD_ENDIAN_BIG) ? valaddr : valaddr + len - 1)
#define NOT_END_P \
        ((TARGET_BYTE_ORDER == BFD_ENDIAN_BIG) ? (p < valaddr + len) : (p >= valaddr))
#define NEXT_P \
        ((TARGET_BYTE_ORDER == BFD_ENDIAN_BIG) ? p++ : p-- )
#define LOW_NIBBLE(  x ) ( (x) & 0x00F)
#define HIGH_NIBBLE( x ) (((x) & 0x0F0) >> 4)

  unsigned char *p;
  unsigned char *digits;
  int carry;
  int decimal_len;
  int i, j, decimal_digits;
  int dummy;
  int flip;

  /* Base-ten number is less than twice as many digits
   * as the base 16 number, which is 2 digits per byte.
   */
  decimal_len = len * 2 * 2;
  digits = xmalloc (decimal_len);

  for (i = 0; i < decimal_len; i++)
    {
      digits[i] = 0;
    }

  fputs_filtered (local_decimal_format_prefix (), stream);

  /* Ok, we have an unknown number of bytes of data to be printed in
   * decimal.
   *
   * Given a hex number (in nibbles) as XYZ, we start by taking X and
   * decemalizing it as "x1 x2" in two decimal nibbles.  Then we multiply
   * the nibbles by 16, add Y and re-decimalize.  Repeat with Z.
   *
   * The trick is that "digits" holds a base-10 number, but sometimes
   * the individual digits are > 10. 
   *
   * Outer loop is per nibble (hex digit) of input, from MSD end to
   * LSD end.
   */
  decimal_digits = 0;		/* Number of decimal digits so far */
  p = START_P;
  flip = 0;
  while (NOT_END_P)
    {
      /*
       * Multiply current base-ten number by 16 in place.
       * Each digit was between 0 and 9, now is between
       * 0 and 144.
       */
      for (j = 0; j < decimal_digits; j++)
	{
	  digits[j] = SHIFT (digits[j]);
	}

      /* Take the next nibble off the input and add it to what
       * we've got in the LSB position.  Bottom 'digit' is now
       * between 0 and 159.
       *
       * "flip" is used to run this loop twice for each byte.
       */
      if (flip == 0)
	{
	  /* Take top nibble.
	   */
	  digits[0] += HIGH_NIBBLE (*p);
	  flip = 1;
	}
      else
	{
	  /* Take low nibble and bump our pointer "p".
	   */
	  digits[0] += LOW_NIBBLE (*p);
	  NEXT_P;
	  flip = 0;
	}

      /* Re-decimalize.  We have to do this often enough
       * that we don't overflow, but once per nibble is
       * overkill.  Easier this way, though.  Note that the
       * carry is often larger than 10 (e.g. max initial
       * carry out of lowest nibble is 15, could bubble all
       * the way up greater than 10).  So we have to do
       * the carrying beyond the last current digit.
       */
      carry = 0;
      for (j = 0; j < decimal_len - 1; j++)
	{
	  digits[j] += carry;

	  /* "/" won't handle an unsigned char with
	   * a value that if signed would be negative.
	   * So extend to longword int via "dummy".
	   */
	  dummy = digits[j];
	  carry = CARRY_OUT (dummy);
	  digits[j] = CARRY_LEFT (dummy);

	  if (j >= decimal_digits && carry == 0)
	    {
	      /*
	       * All higher digits are 0 and we
	       * no longer have a carry.
	       *
	       * Note: "j" is 0-based, "decimal_digits" is
	       *       1-based.
	       */
	      decimal_digits = j + 1;
	      break;
	    }
	}
    }

  /* Ok, now "digits" is the decimal representation, with
   * the "decimal_digits" actual digits.  Print!
   */
  for (i = decimal_digits - 1; i >= 0; i--)
    {
      fprintf_filtered (stream, "%1d", digits[i]);
    }
  xfree (digits);

  fputs_filtered (local_decimal_format_suffix (), stream);
}

/* VALADDR points to an integer of LEN bytes.  Print it in hex on stream.  */

void
print_hex_chars (struct ui_file *stream, unsigned char *valaddr, unsigned len)
{
  unsigned char *p;

  /* FIXME: We should be not printing leading zeroes in most cases.  */

  fputs_filtered (local_hex_format_prefix (), stream);
  if (TARGET_BYTE_ORDER == BFD_ENDIAN_BIG)
    {
      for (p = valaddr;
	   p < valaddr + len;
	   p++)
	{
	  fprintf_filtered (stream, "%02x", *p);
	}
    }
  else
    {
      for (p = valaddr + len - 1;
	   p >= valaddr;
	   p--)
	{
	  fprintf_filtered (stream, "%02x", *p);
	}
    }
  fputs_filtered (local_hex_format_suffix (), stream);
}

/* VALADDR points to a char integer of LEN bytes.  Print it out in appropriate language form on stream.  
   Omit any leading zero chars.  */

void
print_char_chars (struct ui_file *stream, unsigned char *valaddr, unsigned len)
{
  unsigned char *p;

  if (TARGET_BYTE_ORDER == BFD_ENDIAN_BIG)
    {
      p = valaddr;
      while (p < valaddr + len - 1 && *p == 0)
	++p;

      while (p < valaddr + len)
	{
	  LA_EMIT_CHAR (*p, stream, '\'');
	  ++p;
	}
    }
  else
    {
      p = valaddr + len - 1;
      while (p > valaddr && *p == 0)
	--p;

      while (p >= valaddr)
	{
	  LA_EMIT_CHAR (*p, stream, '\'');
	  --p;
	}
    }
}

/*  Called by various <lang>_val_print routines to print elements of an
   array in the form "<elem1>, <elem2>, <elem3>, ...".

   (FIXME?)  Assumes array element separator is a comma, which is correct
   for all languages currently handled.
   (FIXME?)  Some languages have a notation for repeated array elements,
   perhaps we should try to use that notation when appropriate.
 */

void
val_print_array_elements (struct type *type, char *valaddr, CORE_ADDR address,
			  struct ui_file *stream, int format, int deref_ref,
			  int recurse, enum val_prettyprint pretty,
			  unsigned int i)
{
  unsigned int things_printed = 0;
  unsigned len;
  struct type *elttype;
  unsigned eltlen;
  /* Position of the array element we are examining to see
     whether it is repeated.  */
  unsigned int rep1;
  /* Number of repetitions we have detected so far.  */
  unsigned int reps;

  elttype = TYPE_TARGET_TYPE (type);
  eltlen = TYPE_LENGTH (check_typedef (elttype));
  len = TYPE_LENGTH (type) / eltlen;

  annotate_array_section_begin (i, elttype);

  for (; i < len && things_printed < print_max; i++)
    {
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

      rep1 = i + 1;
      reps = 1;
      while ((rep1 < len) &&
	     !memcmp (valaddr + i * eltlen, valaddr + rep1 * eltlen, eltlen))
	{
	  ++reps;
	  ++rep1;
	}

      if (reps > repeat_count_threshold)
	{
	  val_print (elttype, valaddr + i * eltlen, 0, 0, stream, format,
		     deref_ref, recurse + 1, pretty);
	  annotate_elt_rep (reps);
	  fprintf_filtered (stream, " <repeats %u times>", reps);
	  annotate_elt_rep_end ();

	  i = rep1 - 1;
	  things_printed += repeat_count_threshold;
	}
      else
	{
	  val_print (elttype, valaddr + i * eltlen, 0, 0, stream, format,
		     deref_ref, recurse + 1, pretty);
	  annotate_elt ();
	  things_printed++;
	}
    }
  annotate_array_section_end ();
  if (i < len)
    {
      fprintf_filtered (stream, "...");
    }
}

/* Read LEN bytes of target memory at address MEMADDR, placing the
   results in GDB's memory at MYADDR.  Returns a count of the bytes
   actually read, and optionally an errno value in the location
   pointed to by ERRNOPTR if ERRNOPTR is non-null. */

/* FIXME: cagney/1999-10-14: Only used by val_print_string.  Can this
   function be eliminated.  */

static int
partial_memory_read (CORE_ADDR memaddr, char *myaddr, int len, int *errnoptr)
{
  int nread;			/* Number of bytes actually read. */
  int errcode;			/* Error from last read. */

  /* First try a complete read. */
  errcode = target_read_memory (memaddr, myaddr, len);
  if (errcode == 0)
    {
      /* Got it all. */
      nread = len;
    }
  else
    {
      /* Loop, reading one byte at a time until we get as much as we can. */
      for (errcode = 0, nread = 0; len > 0 && errcode == 0; nread++, len--)
	{
	  errcode = target_read_memory (memaddr++, myaddr++, 1);
	}
      /* If an error, the last read was unsuccessful, so adjust count. */
      if (errcode != 0)
	{
	  nread--;
	}
    }
  if (errnoptr != NULL)
    {
      *errnoptr = errcode;
    }
  return (nread);
}

/*  Print a string from the inferior, starting at ADDR and printing up to LEN
   characters, of WIDTH bytes a piece, to STREAM.  If LEN is -1, printing
   stops at the first null byte, otherwise printing proceeds (including null
   bytes) until either print_max or LEN characters have been printed,
   whichever is smaller. */

/* FIXME: Use target_read_string.  */

int
val_print_string (CORE_ADDR addr, int len, int width, struct ui_file *stream)
{
  int force_ellipsis = 0;	/* Force ellipsis to be printed if nonzero. */
  int errcode;			/* Errno returned from bad reads. */
  unsigned int fetchlimit;	/* Maximum number of chars to print. */
  unsigned int nfetch;		/* Chars to fetch / chars fetched. */
  unsigned int chunksize;	/* Size of each fetch, in chars. */
  char *buffer = NULL;		/* Dynamically growable fetch buffer. */
  char *bufptr;			/* Pointer to next available byte in buffer. */
  char *limit;			/* First location past end of fetch buffer. */
  struct cleanup *old_chain = NULL;	/* Top of the old cleanup chain. */
  int found_nul;		/* Non-zero if we found the nul char */

  /* First we need to figure out the limit on the number of characters we are
     going to attempt to fetch and print.  This is actually pretty simple.  If
     LEN >= zero, then the limit is the minimum of LEN and print_max.  If
     LEN is -1, then the limit is print_max.  This is true regardless of
     whether print_max is zero, UINT_MAX (unlimited), or something in between,
     because finding the null byte (or available memory) is what actually
     limits the fetch. */

  fetchlimit = (len == -1 ? print_max : min (len, print_max));

  /* Now decide how large of chunks to try to read in one operation.  This
     is also pretty simple.  If LEN >= zero, then we want fetchlimit chars,
     so we might as well read them all in one operation.  If LEN is -1, we
     are looking for a null terminator to end the fetching, so we might as
     well read in blocks that are large enough to be efficient, but not so
     large as to be slow if fetchlimit happens to be large.  So we choose the
     minimum of 8 and fetchlimit.  We used to use 200 instead of 8 but
     200 is way too big for remote debugging over a serial line.  */

  chunksize = (len == -1 ? min (8, fetchlimit) : fetchlimit);

  /* Loop until we either have all the characters to print, or we encounter
     some error, such as bumping into the end of the address space. */

  found_nul = 0;
  old_chain = make_cleanup (null_cleanup, 0);

  if (len > 0)
    {
      buffer = (char *) xmalloc (len * width);
      bufptr = buffer;
      old_chain = make_cleanup (xfree, buffer);

      nfetch = partial_memory_read (addr, bufptr, len * width, &errcode)
	/ width;
      addr += nfetch * width;
      bufptr += nfetch * width;
    }
  else if (len == -1)
    {
      unsigned long bufsize = 0;
      do
	{
	  QUIT;
	  nfetch = min (chunksize, fetchlimit - bufsize);

	  if (buffer == NULL)
	    buffer = (char *) xmalloc (nfetch * width);
	  else
	    {
	      discard_cleanups (old_chain);
	      buffer = (char *) xrealloc (buffer, (nfetch + bufsize) * width);
	    }

	  old_chain = make_cleanup (xfree, buffer);
	  bufptr = buffer + bufsize * width;
	  bufsize += nfetch;

	  /* Read as much as we can. */
	  nfetch = partial_memory_read (addr, bufptr, nfetch * width, &errcode)
	    / width;

	  /* Scan this chunk for the null byte that terminates the string
	     to print.  If found, we don't need to fetch any more.  Note
	     that bufptr is explicitly left pointing at the next character
	     after the null byte, or at the next character after the end of
	     the buffer. */

	  limit = bufptr + nfetch * width;
	  while (bufptr < limit)
	    {
	      unsigned long c;

	      c = extract_unsigned_integer (bufptr, width);
	      addr += width;
	      bufptr += width;
	      if (c == 0)
		{
		  /* We don't care about any error which happened after
		     the NULL terminator.  */
		  errcode = 0;
		  found_nul = 1;
		  break;
		}
	    }
	}
      while (errcode == 0	/* no error */
	     && bufptr - buffer < fetchlimit * width	/* no overrun */
	     && !found_nul);	/* haven't found nul yet */
    }
  else
    {				/* length of string is really 0! */
      buffer = bufptr = NULL;
      errcode = 0;
    }

  /* bufptr and addr now point immediately beyond the last byte which we
     consider part of the string (including a '\0' which ends the string).  */

  /* We now have either successfully filled the buffer to fetchlimit, or
     terminated early due to an error or finding a null char when LEN is -1. */

  if (len == -1 && !found_nul)
    {
      char *peekbuf;

      /* We didn't find a null terminator we were looking for.  Attempt
         to peek at the next character.  If not successful, or it is not
         a null byte, then force ellipsis to be printed.  */

      peekbuf = (char *) alloca (width);

      if (target_read_memory (addr, peekbuf, width) == 0
	  && extract_unsigned_integer (peekbuf, width) != 0)
	force_ellipsis = 1;
    }
  else if ((len >= 0 && errcode != 0) || (len > (bufptr - buffer) / width))
    {
      /* Getting an error when we have a requested length, or fetching less
         than the number of characters actually requested, always make us
         print ellipsis. */
      force_ellipsis = 1;
    }

  QUIT;

  /* If we get an error before fetching anything, don't print a string.
     But if we fetch something and then get an error, print the string
     and then the error message.  */
  if (errcode == 0 || bufptr > buffer)
    {
      if (addressprint)
	{
	  fputs_filtered (" ", stream);
	}
      LA_PRINT_STRING (stream, buffer, (bufptr - buffer) / width, width, force_ellipsis);
    }

  if (errcode != 0)
    {
      if (errcode == EIO)
	{
	  fprintf_filtered (stream, " <Address ");
	  print_address_numeric (addr, 1, stream);
	  fprintf_filtered (stream, " out of bounds>");
	}
      else
	{
	  fprintf_filtered (stream, " <Error reading address ");
	  print_address_numeric (addr, 1, stream);
	  fprintf_filtered (stream, ": %s>", safe_strerror (errcode));
	}
    }
  gdb_flush (stream);
  do_cleanups (old_chain);
  return ((bufptr - buffer) / width);
}


/* Validate an input or output radix setting, and make sure the user
   knows what they really did here.  Radix setting is confusing, e.g.
   setting the input radix to "10" never changes it!  */

static void
set_input_radix (char *args, int from_tty, struct cmd_list_element *c)
{
  set_input_radix_1 (from_tty, input_radix);
}

static void
set_input_radix_1 (int from_tty, unsigned radix)
{
  /* We don't currently disallow any input radix except 0 or 1, which don't
     make any mathematical sense.  In theory, we can deal with any input
     radix greater than 1, even if we don't have unique digits for every
     value from 0 to radix-1, but in practice we lose on large radix values.
     We should either fix the lossage or restrict the radix range more.
     (FIXME). */

  if (radix < 2)
    {
      /* FIXME: cagney/2002-03-17: This needs to revert the bad radix
         value.  */
      error ("Nonsense input radix ``decimal %u''; input radix unchanged.",
	     radix);
    }
  input_radix = radix;
  if (from_tty)
    {
      printf_filtered ("Input radix now set to decimal %u, hex %x, octal %o.\n",
		       radix, radix, radix);
    }
}

static void
set_output_radix (char *args, int from_tty, struct cmd_list_element *c)
{
  set_output_radix_1 (from_tty, output_radix);
}

static void
set_output_radix_1 (int from_tty, unsigned radix)
{
  /* Validate the radix and disallow ones that we aren't prepared to
     handle correctly, leaving the radix unchanged. */
  switch (radix)
    {
    case 16:
      output_format = 'x';	/* hex */
      break;
    case 10:
      output_format = 0;	/* decimal */
      break;
    case 8:
      output_format = 'o';	/* octal */
      break;
    default:
      /* FIXME: cagney/2002-03-17: This needs to revert the bad radix
         value.  */
      error ("Unsupported output radix ``decimal %u''; output radix unchanged.",
	     radix);
    }
  output_radix = radix;
  if (from_tty)
    {
      printf_filtered ("Output radix now set to decimal %u, hex %x, octal %o.\n",
		       radix, radix, radix);
    }
}

/* Set both the input and output radix at once.  Try to set the output radix
   first, since it has the most restrictive range.  An radix that is valid as
   an output radix is also valid as an input radix.

   It may be useful to have an unusual input radix.  If the user wishes to
   set an input radix that is not valid as an output radix, he needs to use
   the 'set input-radix' command. */

static void
set_radix (char *arg, int from_tty)
{
  unsigned radix;

  radix = (arg == NULL) ? 10 : parse_and_eval_long (arg);
  set_output_radix_1 (0, radix);
  set_input_radix_1 (0, radix);
  if (from_tty)
    {
      printf_filtered ("Input and output radices now set to decimal %u, hex %x, octal %o.\n",
		       radix, radix, radix);
    }
}

/* Show both the input and output radices. */

static void
show_radix (char *arg, int from_tty)
{
  if (from_tty)
    {
      if (input_radix == output_radix)
	{
	  printf_filtered ("Input and output radices set to decimal %u, hex %x, octal %o.\n",
			   input_radix, input_radix, input_radix);
	}
      else
	{
	  printf_filtered ("Input radix set to decimal %u, hex %x, octal %o.\n",
			   input_radix, input_radix, input_radix);
	  printf_filtered ("Output radix set to decimal %u, hex %x, octal %o.\n",
			   output_radix, output_radix, output_radix);
	}
    }
}


static void
set_print (char *arg, int from_tty)
{
  printf_unfiltered (
     "\"set print\" must be followed by the name of a print subcommand.\n");
  help_list (setprintlist, "set print ", -1, gdb_stdout);
}

static void
show_print (char *args, int from_tty)
{
  cmd_show_list (showprintlist, from_tty, "");
}

void
_initialize_valprint (void)
{
  struct cmd_list_element *c;

  add_prefix_cmd ("print", no_class, set_print,
		  "Generic command for setting how things print.",
		  &setprintlist, "set print ", 0, &setlist);
  add_alias_cmd ("p", "print", no_class, 1, &setlist);
  /* prefer set print to set prompt */
  add_alias_cmd ("pr", "print", no_class, 1, &setlist);

  add_prefix_cmd ("print", no_class, show_print,
		  "Generic command for showing print settings.",
		  &showprintlist, "show print ", 0, &showlist);
  add_alias_cmd ("p", "print", no_class, 1, &showlist);
  add_alias_cmd ("pr", "print", no_class, 1, &showlist);

  add_show_from_set
    (add_set_cmd ("elements", no_class, var_uinteger, (char *) &print_max,
		  "Set limit on string chars or array elements to print.\n\
\"set print elements 0\" causes there to be no limit.",
		  &setprintlist),
     &showprintlist);

  add_show_from_set
    (add_set_cmd ("null-stop", no_class, var_boolean,
		  (char *) &stop_print_at_null,
		  "Set printing of char arrays to stop at first null char.",
		  &setprintlist),
     &showprintlist);

  add_show_from_set
    (add_set_cmd ("repeats", no_class, var_uinteger,
		  (char *) &repeat_count_threshold,
		  "Set threshold for repeated print elements.\n\
\"set print repeats 0\" causes all elements to be individually printed.",
		  &setprintlist),
     &showprintlist);

  add_show_from_set
    (add_set_cmd ("pretty", class_support, var_boolean,
		  (char *) &prettyprint_structs,
		  "Set prettyprinting of structures.",
		  &setprintlist),
     &showprintlist);

  add_show_from_set
    (add_set_cmd ("union", class_support, var_boolean, (char *) &unionprint,
		  "Set printing of unions interior to structures.",
		  &setprintlist),
     &showprintlist);

  add_show_from_set
    (add_set_cmd ("array", class_support, var_boolean,
		  (char *) &prettyprint_arrays,
		  "Set prettyprinting of arrays.",
		  &setprintlist),
     &showprintlist);

  add_show_from_set
    (add_set_cmd ("address", class_support, var_boolean, (char *) &addressprint,
		  "Set printing of addresses.",
		  &setprintlist),
     &showprintlist);

  c = add_set_cmd ("input-radix", class_support, var_uinteger,
		   (char *) &input_radix,
		   "Set default input radix for entering numbers.",
		   &setlist);
  add_show_from_set (c, &showlist);
  set_cmd_sfunc (c, set_input_radix);

  c = add_set_cmd ("output-radix", class_support, var_uinteger,
		   (char *) &output_radix,
		   "Set default output radix for printing of values.",
		   &setlist);
  add_show_from_set (c, &showlist);
  set_cmd_sfunc (c, set_output_radix);

  /* The "set radix" and "show radix" commands are special in that they are
     like normal set and show commands but allow two normally independent
     variables to be either set or shown with a single command.  So the
     usual add_set_cmd() and add_show_from_set() commands aren't really
     appropriate. */
  add_cmd ("radix", class_support, set_radix,
	   "Set default input and output number radices.\n\
Use 'set input-radix' or 'set output-radix' to independently set each.\n\
Without an argument, sets both radices back to the default value of 10.",
	   &setlist);
  add_cmd ("radix", class_support, show_radix,
	   "Show the default input and output number radices.\n\
Use 'show input-radix' or 'show output-radix' to independently show each.",
	   &showlist);

  /* Give people the defaults which they are used to.  */
  prettyprint_structs = 0;
  prettyprint_arrays = 0;
  unionprint = 1;
  addressprint = 1;
  print_max = PRINT_MAX_DEFAULT;
}
