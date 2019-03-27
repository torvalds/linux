/* Floating point routines for GDB, the GNU debugger.

   Copyright 1986, 1988, 1989, 1990, 1991, 1992, 1993, 1994, 1995,
   1996, 1997, 1998, 1999, 2000, 2001, 2003 Free Software Foundation,
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

/* Support for converting target fp numbers into host DOUBLEST format.  */

/* XXX - This code should really be in libiberty/floatformat.c,
   however configuration issues with libiberty made this very
   difficult to do in the available time.  */

#include "defs.h"
#include "doublest.h"
#include "floatformat.h"
#include "gdb_assert.h"
#include "gdb_string.h"
#include "gdbtypes.h"
#include <math.h>		/* ldexp */

/* The odds that CHAR_BIT will be anything but 8 are low enough that I'm not
   going to bother with trying to muck around with whether it is defined in
   a system header, what we do if not, etc.  */
#define FLOATFORMAT_CHAR_BIT 8

static unsigned long get_field (unsigned char *,
				enum floatformat_byteorders,
				unsigned int, unsigned int, unsigned int);

/* Extract a field which starts at START and is LEN bytes long.  DATA and
   TOTAL_LEN are the thing we are extracting it from, in byteorder ORDER.  */
static unsigned long
get_field (unsigned char *data, enum floatformat_byteorders order,
	   unsigned int total_len, unsigned int start, unsigned int len)
{
  unsigned long result;
  unsigned int cur_byte;
  int cur_bitshift;

  /* Start at the least significant part of the field.  */
  if (order == floatformat_little || order == floatformat_littlebyte_bigword)
    {
      /* We start counting from the other end (i.e, from the high bytes
	 rather than the low bytes).  As such, we need to be concerned
	 with what happens if bit 0 doesn't start on a byte boundary. 
	 I.e, we need to properly handle the case where total_len is
	 not evenly divisible by 8.  So we compute ``excess'' which
	 represents the number of bits from the end of our starting
	 byte needed to get to bit 0. */
      int excess = FLOATFORMAT_CHAR_BIT - (total_len % FLOATFORMAT_CHAR_BIT);
      cur_byte = (total_len / FLOATFORMAT_CHAR_BIT) 
                 - ((start + len + excess) / FLOATFORMAT_CHAR_BIT);
      cur_bitshift = ((start + len + excess) % FLOATFORMAT_CHAR_BIT) 
                     - FLOATFORMAT_CHAR_BIT;
    }
  else
    {
      cur_byte = (start + len) / FLOATFORMAT_CHAR_BIT;
      cur_bitshift =
	((start + len) % FLOATFORMAT_CHAR_BIT) - FLOATFORMAT_CHAR_BIT;
    }
  if (cur_bitshift > -FLOATFORMAT_CHAR_BIT)
    result = *(data + cur_byte) >> (-cur_bitshift);
  else
    result = 0;
  cur_bitshift += FLOATFORMAT_CHAR_BIT;
  if (order == floatformat_little || order == floatformat_littlebyte_bigword)
    ++cur_byte;
  else
    --cur_byte;

  /* Move towards the most significant part of the field.  */
  while (cur_bitshift < len)
    {
      result |= (unsigned long)*(data + cur_byte) << cur_bitshift;
      cur_bitshift += FLOATFORMAT_CHAR_BIT;
      if (order == floatformat_little || order == floatformat_littlebyte_bigword)
	++cur_byte;
      else
	--cur_byte;
    }
  if (len < sizeof(result) * FLOATFORMAT_CHAR_BIT)
    /* Mask out bits which are not part of the field */
    result &= ((1UL << len) - 1);
  return result;
}

/* Convert from FMT to a DOUBLEST.
   FROM is the address of the extended float.
   Store the DOUBLEST in *TO.  */

static void
convert_floatformat_to_doublest (const struct floatformat *fmt,
				 const void *from,
				 DOUBLEST *to)
{
  unsigned char *ufrom = (unsigned char *) from;
  DOUBLEST dto;
  long exponent;
  unsigned long mant;
  unsigned int mant_bits, mant_off;
  int mant_bits_left;
  int special_exponent;		/* It's a NaN, denorm or zero */

  /* If the mantissa bits are not contiguous from one end of the
     mantissa to the other, we need to make a private copy of the
     source bytes that is in the right order since the unpacking
     algorithm assumes that the bits are contiguous.

     Swap the bytes individually rather than accessing them through
     "long *" since we have no guarantee that they start on a long
     alignment, and also sizeof(long) for the host could be different
     than sizeof(long) for the target.  FIXME: Assumes sizeof(long)
     for the target is 4. */

  if (fmt->byteorder == floatformat_littlebyte_bigword)
    {
      static unsigned char *newfrom;
      unsigned char *swapin, *swapout;
      int longswaps;

      longswaps = fmt->totalsize / FLOATFORMAT_CHAR_BIT;
      longswaps >>= 3;

      if (newfrom == NULL)
	{
	  newfrom = (unsigned char *) xmalloc (fmt->totalsize);
	}
      swapout = newfrom;
      swapin = ufrom;
      ufrom = newfrom;
      while (longswaps-- > 0)
	{
	  /* This is ugly, but efficient */
	  *swapout++ = swapin[4];
	  *swapout++ = swapin[5];
	  *swapout++ = swapin[6];
	  *swapout++ = swapin[7];
	  *swapout++ = swapin[0];
	  *swapout++ = swapin[1];
	  *swapout++ = swapin[2];
	  *swapout++ = swapin[3];
	  swapin += 8;
	}
    }

  exponent = get_field (ufrom, fmt->byteorder, fmt->totalsize,
			fmt->exp_start, fmt->exp_len);
  /* Note that if exponent indicates a NaN, we can't really do anything useful
     (not knowing if the host has NaN's, or how to build one).  So it will
     end up as an infinity or something close; that is OK.  */

  mant_bits_left = fmt->man_len;
  mant_off = fmt->man_start;
  dto = 0.0;

  special_exponent = exponent == 0 || exponent == fmt->exp_nan;

  /* Don't bias NaNs. Use minimum exponent for denorms. For simplicity,
     we don't check for zero as the exponent doesn't matter.  Note the cast
     to int; exp_bias is unsigned, so it's important to make sure the
     operation is done in signed arithmetic.  */
  if (!special_exponent)
    exponent -= fmt->exp_bias;
  else if (exponent == 0)
    exponent = 1 - fmt->exp_bias;

  /* Build the result algebraically.  Might go infinite, underflow, etc;
     who cares. */

/* If this format uses a hidden bit, explicitly add it in now.  Otherwise,
   increment the exponent by one to account for the integer bit.  */

  if (!special_exponent)
    {
      if (fmt->intbit == floatformat_intbit_no)
	dto = ldexp (1.0, exponent);
      else
	exponent++;
    }

  while (mant_bits_left > 0)
    {
      mant_bits = min (mant_bits_left, 32);

      mant = get_field (ufrom, fmt->byteorder, fmt->totalsize,
			mant_off, mant_bits);

      dto += ldexp ((double) mant, exponent - mant_bits);
      exponent -= mant_bits;
      mant_off += mant_bits;
      mant_bits_left -= mant_bits;
    }

  /* Negate it if negative.  */
  if (get_field (ufrom, fmt->byteorder, fmt->totalsize, fmt->sign_start, 1))
    dto = -dto;
  *to = dto;
}

static void put_field (unsigned char *, enum floatformat_byteorders,
		       unsigned int,
		       unsigned int, unsigned int, unsigned long);

/* Set a field which starts at START and is LEN bytes long.  DATA and
   TOTAL_LEN are the thing we are extracting it from, in byteorder ORDER.  */
static void
put_field (unsigned char *data, enum floatformat_byteorders order,
	   unsigned int total_len, unsigned int start, unsigned int len,
	   unsigned long stuff_to_put)
{
  unsigned int cur_byte;
  int cur_bitshift;

  /* Start at the least significant part of the field.  */
  if (order == floatformat_little || order == floatformat_littlebyte_bigword)
    {
      int excess = FLOATFORMAT_CHAR_BIT - (total_len % FLOATFORMAT_CHAR_BIT);
      cur_byte = (total_len / FLOATFORMAT_CHAR_BIT) 
                 - ((start + len + excess) / FLOATFORMAT_CHAR_BIT);
      cur_bitshift = ((start + len + excess) % FLOATFORMAT_CHAR_BIT) 
                     - FLOATFORMAT_CHAR_BIT;
    }
  else
    {
      cur_byte = (start + len) / FLOATFORMAT_CHAR_BIT;
      cur_bitshift =
	((start + len) % FLOATFORMAT_CHAR_BIT) - FLOATFORMAT_CHAR_BIT;
    }
  if (cur_bitshift > -FLOATFORMAT_CHAR_BIT)
    {
      *(data + cur_byte) &=
	~(((1 << ((start + len) % FLOATFORMAT_CHAR_BIT)) - 1)
	  << (-cur_bitshift));
      *(data + cur_byte) |=
	(stuff_to_put & ((1 << FLOATFORMAT_CHAR_BIT) - 1)) << (-cur_bitshift);
    }
  cur_bitshift += FLOATFORMAT_CHAR_BIT;
  if (order == floatformat_little || order == floatformat_littlebyte_bigword)
    ++cur_byte;
  else
    --cur_byte;

  /* Move towards the most significant part of the field.  */
  while (cur_bitshift < len)
    {
      if (len - cur_bitshift < FLOATFORMAT_CHAR_BIT)
	{
	  /* This is the last byte.  */
	  *(data + cur_byte) &=
	    ~((1 << (len - cur_bitshift)) - 1);
	  *(data + cur_byte) |= (stuff_to_put >> cur_bitshift);
	}
      else
	*(data + cur_byte) = ((stuff_to_put >> cur_bitshift)
			      & ((1 << FLOATFORMAT_CHAR_BIT) - 1));
      cur_bitshift += FLOATFORMAT_CHAR_BIT;
      if (order == floatformat_little || order == floatformat_littlebyte_bigword)
	++cur_byte;
      else
	--cur_byte;
    }
}

#ifdef HAVE_LONG_DOUBLE
/* Return the fractional part of VALUE, and put the exponent of VALUE in *EPTR.
   The range of the returned value is >= 0.5 and < 1.0.  This is equivalent to
   frexp, but operates on the long double data type.  */

static long double ldfrexp (long double value, int *eptr);

static long double
ldfrexp (long double value, int *eptr)
{
  long double tmp;
  int exp;

  /* Unfortunately, there are no portable functions for extracting the exponent
     of a long double, so we have to do it iteratively by multiplying or dividing
     by two until the fraction is between 0.5 and 1.0.  */

  if (value < 0.0l)
    value = -value;

  tmp = 1.0l;
  exp = 0;

  if (value >= tmp)		/* Value >= 1.0 */
    while (value >= tmp)
      {
	tmp *= 2.0l;
	exp++;
      }
  else if (value != 0.0l)	/* Value < 1.0  and > 0.0 */
    {
      while (value < tmp)
	{
	  tmp /= 2.0l;
	  exp--;
	}
      tmp *= 2.0l;
      exp++;
    }

  *eptr = exp;
  return value / tmp;
}
#endif /* HAVE_LONG_DOUBLE */


/* The converse: convert the DOUBLEST *FROM to an extended float
   and store where TO points.  Neither FROM nor TO have any alignment
   restrictions.  */

static void
convert_doublest_to_floatformat (CONST struct floatformat *fmt,
				 const DOUBLEST *from,
				 void *to)
{
  DOUBLEST dfrom;
  int exponent;
  DOUBLEST mant;
  unsigned int mant_bits, mant_off;
  int mant_bits_left;
  unsigned char *uto = (unsigned char *) to;

  memcpy (&dfrom, from, sizeof (dfrom));
  memset (uto, 0, (fmt->totalsize + FLOATFORMAT_CHAR_BIT - 1) 
                    / FLOATFORMAT_CHAR_BIT);
  if (dfrom == 0)
    return;			/* Result is zero */
  if (dfrom != dfrom)		/* Result is NaN */
    {
      /* From is NaN */
      put_field (uto, fmt->byteorder, fmt->totalsize, fmt->exp_start,
		 fmt->exp_len, fmt->exp_nan);
      /* Be sure it's not infinity, but NaN value is irrel */
      put_field (uto, fmt->byteorder, fmt->totalsize, fmt->man_start,
		 32, 1);
      return;
    }

  /* If negative, set the sign bit.  */
  if (dfrom < 0)
    {
      put_field (uto, fmt->byteorder, fmt->totalsize, fmt->sign_start, 1, 1);
      dfrom = -dfrom;
    }

  if (dfrom + dfrom == dfrom && dfrom != 0.0)	/* Result is Infinity */
    {
      /* Infinity exponent is same as NaN's.  */
      put_field (uto, fmt->byteorder, fmt->totalsize, fmt->exp_start,
		 fmt->exp_len, fmt->exp_nan);
      /* Infinity mantissa is all zeroes.  */
      put_field (uto, fmt->byteorder, fmt->totalsize, fmt->man_start,
		 fmt->man_len, 0);
      return;
    }

#ifdef HAVE_LONG_DOUBLE
  mant = ldfrexp (dfrom, &exponent);
#else
  mant = frexp (dfrom, &exponent);
#endif

  put_field (uto, fmt->byteorder, fmt->totalsize, fmt->exp_start, fmt->exp_len,
	     exponent + fmt->exp_bias - 1);

  mant_bits_left = fmt->man_len;
  mant_off = fmt->man_start;
  while (mant_bits_left > 0)
    {
      unsigned long mant_long;
      mant_bits = mant_bits_left < 32 ? mant_bits_left : 32;

      mant *= 4294967296.0;
      mant_long = ((unsigned long) mant) & 0xffffffffL;
      mant -= mant_long;

      /* If the integer bit is implicit, then we need to discard it.
         If we are discarding a zero, we should be (but are not) creating
         a denormalized number which means adjusting the exponent
         (I think).  */
      if (mant_bits_left == fmt->man_len
	  && fmt->intbit == floatformat_intbit_no)
	{
	  mant_long <<= 1;
	  mant_long &= 0xffffffffL;
          /* If we are processing the top 32 mantissa bits of a doublest
             so as to convert to a float value with implied integer bit,
             we will only be putting 31 of those 32 bits into the
             final value due to the discarding of the top bit.  In the 
             case of a small float value where the number of mantissa 
             bits is less than 32, discarding the top bit does not alter
             the number of bits we will be adding to the result.  */
          if (mant_bits == 32)
            mant_bits -= 1;
	}

      if (mant_bits < 32)
	{
	  /* The bits we want are in the most significant MANT_BITS bits of
	     mant_long.  Move them to the least significant.  */
	  mant_long >>= 32 - mant_bits;
	}

      put_field (uto, fmt->byteorder, fmt->totalsize,
		 mant_off, mant_bits, mant_long);
      mant_off += mant_bits;
      mant_bits_left -= mant_bits;
    }
  if (fmt->byteorder == floatformat_littlebyte_bigword)
    {
      int count;
      unsigned char *swaplow = uto;
      unsigned char *swaphigh = uto + 4;
      unsigned char tmp;

      for (count = 0; count < 4; count++)
	{
	  tmp = *swaplow;
	  *swaplow++ = *swaphigh;
	  *swaphigh++ = tmp;
	}
    }
}

/* Check if VAL (which is assumed to be a floating point number whose
   format is described by FMT) is negative.  */

int
floatformat_is_negative (const struct floatformat *fmt, char *val)
{
  unsigned char *uval = (unsigned char *) val;
  gdb_assert (fmt != NULL);
  return get_field (uval, fmt->byteorder, fmt->totalsize, fmt->sign_start, 1);
}

/* Check if VAL is "not a number" (NaN) for FMT.  */

int
floatformat_is_nan (const struct floatformat *fmt, char *val)
{
  unsigned char *uval = (unsigned char *) val;
  long exponent;
  unsigned long mant;
  unsigned int mant_bits, mant_off;
  int mant_bits_left;

  gdb_assert (fmt != NULL);

  if (! fmt->exp_nan)
    return 0;

  exponent = get_field (uval, fmt->byteorder, fmt->totalsize,
			fmt->exp_start, fmt->exp_len);

  if (exponent != fmt->exp_nan)
    return 0;

  mant_bits_left = fmt->man_len;
  mant_off = fmt->man_start;

  while (mant_bits_left > 0)
    {
      mant_bits = min (mant_bits_left, 32);

      mant = get_field (uval, fmt->byteorder, fmt->totalsize,
			mant_off, mant_bits);

      /* If there is an explicit integer bit, mask it off.  */
      if (mant_off == fmt->man_start
	  && fmt->intbit == floatformat_intbit_yes)
	mant &= ~(1 << (mant_bits - 1));

      if (mant)
	return 1;

      mant_off += mant_bits;
      mant_bits_left -= mant_bits;
    }

  return 0;
}

/* Convert the mantissa of VAL (which is assumed to be a floating
   point number whose format is described by FMT) into a hexadecimal
   and store it in a static string.  Return a pointer to that string.  */

char *
floatformat_mantissa (const struct floatformat *fmt, char *val)
{
  unsigned char *uval = (unsigned char *) val;
  unsigned long mant;
  unsigned int mant_bits, mant_off;
  int mant_bits_left;
  static char res[50];
  char buf[9];

  /* Make sure we have enough room to store the mantissa.  */
  gdb_assert (fmt != NULL);
  gdb_assert (sizeof res > ((fmt->man_len + 7) / 8) * 2);

  mant_off = fmt->man_start;
  mant_bits_left = fmt->man_len;
  mant_bits = (mant_bits_left % 32) > 0 ? mant_bits_left % 32 : 32;

  mant = get_field (uval, fmt->byteorder, fmt->totalsize,
		    mant_off, mant_bits);

  sprintf (res, "%lx", mant);

  mant_off += mant_bits;
  mant_bits_left -= mant_bits;
  
  while (mant_bits_left > 0)
    {
      mant = get_field (uval, fmt->byteorder, fmt->totalsize,
			mant_off, 32);

      sprintf (buf, "%08lx", mant);
      strcat (res, buf);

      mant_off += 32;
      mant_bits_left -= 32;
    }

  return res;
}


/* Convert TO/FROM target to the hosts DOUBLEST floating-point format.

   If the host and target formats agree, we just copy the raw data
   into the appropriate type of variable and return, letting the host
   increase precision as necessary.  Otherwise, we call the conversion
   routine and let it do the dirty work.  */

#ifndef HOST_FLOAT_FORMAT
#define HOST_FLOAT_FORMAT 0
#endif
#ifndef HOST_DOUBLE_FORMAT
#define HOST_DOUBLE_FORMAT 0
#endif
#ifndef HOST_LONG_DOUBLE_FORMAT
#define HOST_LONG_DOUBLE_FORMAT 0
#endif

static const struct floatformat *host_float_format = HOST_FLOAT_FORMAT;
static const struct floatformat *host_double_format = HOST_DOUBLE_FORMAT;
static const struct floatformat *host_long_double_format = HOST_LONG_DOUBLE_FORMAT;

void
floatformat_to_doublest (const struct floatformat *fmt,
			 const void *in, DOUBLEST *out)
{
  gdb_assert (fmt != NULL);
  if (fmt == host_float_format)
    {
      float val;
      memcpy (&val, in, sizeof (val));
      *out = val;
    }
  else if (fmt == host_double_format)
    {
      double val;
      memcpy (&val, in, sizeof (val));
      *out = val;
    }
  else if (fmt == host_long_double_format)
    {
      long double val;
      memcpy (&val, in, sizeof (val));
      *out = val;
    }
  else
    convert_floatformat_to_doublest (fmt, in, out);
}

void
floatformat_from_doublest (const struct floatformat *fmt,
			   const DOUBLEST *in, void *out)
{
  gdb_assert (fmt != NULL);
  if (fmt == host_float_format)
    {
      float val = *in;
      memcpy (out, &val, sizeof (val));
    }
  else if (fmt == host_double_format)
    {
      double val = *in;
      memcpy (out, &val, sizeof (val));
    }
  else if (fmt == host_long_double_format)
    {
      long double val = *in;
      memcpy (out, &val, sizeof (val));
    }
  else
    convert_doublest_to_floatformat (fmt, in, out);
}


/* Return a floating-point format for a floating-point variable of
   length LEN.  Return NULL, if no suitable floating-point format
   could be found.

   We need this functionality since information about the
   floating-point format of a type is not always available to GDB; the
   debug information typically only tells us the size of a
   floating-point type.

   FIXME: kettenis/2001-10-28: In many places, particularly in
   target-dependent code, the format of floating-point types is known,
   but not passed on by GDB.  This should be fixed.  */

static const struct floatformat *
floatformat_from_length (int len)
{
  if (len * TARGET_CHAR_BIT == TARGET_FLOAT_BIT)
    return TARGET_FLOAT_FORMAT;
  else if (len * TARGET_CHAR_BIT == TARGET_DOUBLE_BIT)
    return TARGET_DOUBLE_FORMAT;
  else if (len * TARGET_CHAR_BIT == TARGET_LONG_DOUBLE_BIT)
    return TARGET_LONG_DOUBLE_FORMAT;
  /* On i386 the 'long double' type takes 96 bits,
     while the real number of used bits is only 80,
     both in processor and in memory.  
     The code below accepts the real bit size.  */ 
  else if ((TARGET_LONG_DOUBLE_FORMAT != NULL) 
	   && (len * TARGET_CHAR_BIT ==
               TARGET_LONG_DOUBLE_FORMAT->totalsize))
    return TARGET_LONG_DOUBLE_FORMAT;

  return NULL;
}

const struct floatformat *
floatformat_from_type (const struct type *type)
{
  gdb_assert (TYPE_CODE (type) == TYPE_CODE_FLT);
  if (TYPE_FLOATFORMAT (type) != NULL)
    return TYPE_FLOATFORMAT (type);
  else
    return floatformat_from_length (TYPE_LENGTH (type));
}

/* If the host doesn't define NAN, use zero instead.  */
#ifndef NAN
#define NAN 0.0
#endif

/* Extract a floating-point number of length LEN from a target-order
   byte-stream at ADDR.  Returns the value as type DOUBLEST.  */

static DOUBLEST
extract_floating_by_length (const void *addr, int len)
{
  const struct floatformat *fmt = floatformat_from_length (len);
  DOUBLEST val;

  if (fmt == NULL)
    {
      warning ("Can't extract a floating-point number of %d bytes.", len);
      return NAN;
    }

  floatformat_to_doublest (fmt, addr, &val);
  return val;
}

DOUBLEST
deprecated_extract_floating (const void *addr, int len)
{
  return extract_floating_by_length (addr, len);
}

/* Store VAL as a floating-point number of length LEN to a
   target-order byte-stream at ADDR.  */

static void
store_floating_by_length (void *addr, int len, DOUBLEST val)
{
  const struct floatformat *fmt = floatformat_from_length (len);

  if (fmt == NULL)
    {
      warning ("Can't store a floating-point number of %d bytes.", len);
      memset (addr, 0, len);
      return;
    }

  floatformat_from_doublest (fmt, &val, addr);
}

void
deprecated_store_floating (void *addr, int len, DOUBLEST val)
{
  store_floating_by_length (addr, len, val);
}

/* Extract a floating-point number of type TYPE from a target-order
   byte-stream at ADDR.  Returns the value as type DOUBLEST.  */

DOUBLEST
extract_typed_floating (const void *addr, const struct type *type)
{
  DOUBLEST retval;

  gdb_assert (TYPE_CODE (type) == TYPE_CODE_FLT);

  if (TYPE_FLOATFORMAT (type) == NULL)
    /* Not all code remembers to set the FLOATFORMAT (language
       specific code? stabs?) so handle that here as a special case.  */
    return extract_floating_by_length (addr, TYPE_LENGTH (type));

  floatformat_to_doublest (TYPE_FLOATFORMAT (type), addr, &retval);
  return retval;
}

/* Store VAL as a floating-point number of type TYPE to a target-order
   byte-stream at ADDR.  */

void
store_typed_floating (void *addr, const struct type *type, DOUBLEST val)
{
  gdb_assert (TYPE_CODE (type) == TYPE_CODE_FLT);

  /* FIXME: kettenis/2001-10-28: It is debatable whether we should
     zero out any remaining bytes in the target buffer when TYPE is
     longer than the actual underlying floating-point format.  Perhaps
     we should store a fixed bitpattern in those remaining bytes,
     instead of zero, or perhaps we shouldn't touch those remaining
     bytes at all.

     NOTE: cagney/2001-10-28: With the way things currently work, it
     isn't a good idea to leave the end bits undefined.  This is
     because GDB writes out the entire sizeof(<floating>) bits of the
     floating-point type even though the value might only be stored
     in, and the target processor may only refer to, the first N <
     TYPE_LENGTH (type) bits.  If the end of the buffer wasn't
     initialized, GDB would write undefined data to the target.  An
     errant program, refering to that undefined data, would then
     become non-deterministic.

     See also the function convert_typed_floating below.  */
  memset (addr, 0, TYPE_LENGTH (type));

  if (TYPE_FLOATFORMAT (type) == NULL)
    /* Not all code remembers to set the FLOATFORMAT (language
       specific code? stabs?) so handle that here as a special case.  */
    store_floating_by_length (addr, TYPE_LENGTH (type), val);
  else
    floatformat_from_doublest (TYPE_FLOATFORMAT (type), &val, addr);
}

/* Convert a floating-point number of type FROM_TYPE from a
   target-order byte-stream at FROM to a floating-point number of type
   TO_TYPE, and store it to a target-order byte-stream at TO.  */

void
convert_typed_floating (const void *from, const struct type *from_type,
                        void *to, const struct type *to_type)
{
  const struct floatformat *from_fmt = floatformat_from_type (from_type);
  const struct floatformat *to_fmt = floatformat_from_type (to_type);

  gdb_assert (TYPE_CODE (from_type) == TYPE_CODE_FLT);
  gdb_assert (TYPE_CODE (to_type) == TYPE_CODE_FLT);

  if (from_fmt == NULL || to_fmt == NULL)
    {
      /* If we don't know the floating-point format of FROM_TYPE or
         TO_TYPE, there's not much we can do.  We might make the
         assumption that if the length of FROM_TYPE and TO_TYPE match,
         their floating-point format would match too, but that
         assumption might be wrong on targets that support
         floating-point types that only differ in endianness for
         example.  So we warn instead, and zero out the target buffer.  */
      warning ("Can't convert floating-point number to desired type.");
      memset (to, 0, TYPE_LENGTH (to_type));
    }
  else if (from_fmt == to_fmt)
    {
      /* We're in business.  The floating-point format of FROM_TYPE
         and TO_TYPE match.  However, even though the floating-point
         format matches, the length of the type might still be
         different.  Make sure we don't overrun any buffers.  See
         comment in store_typed_floating for a discussion about
         zeroing out remaining bytes in the target buffer.  */
      memset (to, 0, TYPE_LENGTH (to_type));
      memcpy (to, from, min (TYPE_LENGTH (from_type), TYPE_LENGTH (to_type)));
    }
  else
    {
      /* The floating-point types don't match.  The best we can do
         (aport from simulating the target FPU) is converting to the
         widest floating-point type supported by the host, and then
         again to the desired type.  */
      DOUBLEST d;

      floatformat_to_doublest (from_fmt, from, &d);
      floatformat_from_doublest (to_fmt, &d, to);
    }
}
