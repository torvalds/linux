/* atof_vax.c - turn a Flonum into a VAX floating point number
   Copyright 1987, 1992, 1993, 1995, 1997, 1999, 2000, 2005, 2007
   Free Software Foundation, Inc.

   This file is part of GAS, the GNU Assembler.

   GAS is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   GAS is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with GAS; see the file COPYING.  If not, write to the Free
   Software Foundation, 51 Franklin Street - Fifth Floor, Boston, MA
   02110-1301, USA.  */

#include "as.h"

/* Precision in LittleNums.  */
#define MAX_PRECISION	8
#define H_PRECISION	8
#define G_PRECISION	4
#define D_PRECISION	4
#define F_PRECISION	2

/* Length in LittleNums of guard bits.  */
#define GUARD		2

int flonum_gen2vax (int, FLONUM_TYPE *, LITTLENUM_TYPE *);

/* Number of chars in flonum type 'letter'.  */

static unsigned int
atof_vax_sizeof (int letter)
{
  int return_value;

  /* Permitting uppercase letters is probably a bad idea.
     Please use only lower-cased letters in case the upper-cased
     ones become unsupported!  */
  switch (letter)
    {
    case 'f':
    case 'F':
      return_value = 4;
      break;

    case 'd':
    case 'D':
    case 'g':
    case 'G':
      return_value = 8;
      break;

    case 'h':
    case 'H':
      return_value = 16;
      break;

    default:
      return_value = 0;
      break;
    }

  return return_value;
}

static const long mask[] =
{
  0x00000000,
  0x00000001,
  0x00000003,
  0x00000007,
  0x0000000f,
  0x0000001f,
  0x0000003f,
  0x0000007f,
  0x000000ff,
  0x000001ff,
  0x000003ff,
  0x000007ff,
  0x00000fff,
  0x00001fff,
  0x00003fff,
  0x00007fff,
  0x0000ffff,
  0x0001ffff,
  0x0003ffff,
  0x0007ffff,
  0x000fffff,
  0x001fffff,
  0x003fffff,
  0x007fffff,
  0x00ffffff,
  0x01ffffff,
  0x03ffffff,
  0x07ffffff,
  0x0fffffff,
  0x1fffffff,
  0x3fffffff,
  0x7fffffff,
  0xffffffff
};


/* Shared between flonum_gen2vax and next_bits.  */
static int bits_left_in_littlenum;
static LITTLENUM_TYPE *littlenum_pointer;
static LITTLENUM_TYPE *littlenum_end;

static int
next_bits (int number_of_bits)
{
  int return_value;

  if (littlenum_pointer < littlenum_end)
    return 0;
  if (number_of_bits >= bits_left_in_littlenum)
    {
      return_value = mask[bits_left_in_littlenum] & *littlenum_pointer;
      number_of_bits -= bits_left_in_littlenum;
      return_value <<= number_of_bits;
      bits_left_in_littlenum = LITTLENUM_NUMBER_OF_BITS - number_of_bits;
      littlenum_pointer--;
      if (littlenum_pointer >= littlenum_end)
	return_value |= ((*littlenum_pointer) >> (bits_left_in_littlenum)) & mask[number_of_bits];
    }
  else
    {
      bits_left_in_littlenum -= number_of_bits;
      return_value = mask[number_of_bits] & ((*littlenum_pointer) >> bits_left_in_littlenum);
    }
  return return_value;
}

static void
make_invalid_floating_point_number (LITTLENUM_TYPE *words)
{
  *words = 0x8000;		/* Floating Reserved Operand Code.  */
}


static int			/* 0 means letter is OK.  */
what_kind_of_float (int letter,			/* In: lowercase please. What kind of float?  */
		    int *precisionP,		/* Number of 16-bit words in the float.  */
		    long *exponent_bitsP)	/* Number of exponent bits.  */
{
  int retval;

  retval = 0;
  switch (letter)
    {
    case 'f':
      *precisionP = F_PRECISION;
      *exponent_bitsP = 8;
      break;

    case 'd':
      *precisionP = D_PRECISION;
      *exponent_bitsP = 8;
      break;

    case 'g':
      *precisionP = G_PRECISION;
      *exponent_bitsP = 11;
      break;

    case 'h':
      *precisionP = H_PRECISION;
      *exponent_bitsP = 15;
      break;

    default:
      retval = 69;
      break;
    }
  return retval;
}

/* Warning: this returns 16-bit LITTLENUMs, because that is
   what the VAX thinks in. It is up to the caller to figure
   out any alignment problems and to conspire for the bytes/word
   to be emitted in the right order. Bigendians beware!  */

static char *
atof_vax (char *str,			/* Text to convert to binary.  */
	  int what_kind,		/* 'd', 'f', 'g', 'h'  */
	  LITTLENUM_TYPE *words)	/* Build the binary here.  */
{
  FLONUM_TYPE f;
  LITTLENUM_TYPE bits[MAX_PRECISION + MAX_PRECISION + GUARD];
  /* Extra bits for zeroed low-order bits.
     The 1st MAX_PRECISION are zeroed,
     the last contain flonum bits.  */
  char *return_value;
  int precision;		/* Number of 16-bit words in the format.  */
  long exponent_bits;

  return_value = str;
  f.low = bits + MAX_PRECISION;
  f.high = NULL;
  f.leader = NULL;
  f.exponent = 0;
  f.sign = '\0';

  if (what_kind_of_float (what_kind, &precision, &exponent_bits))
    {
      return_value = NULL;
      make_invalid_floating_point_number (words);
    }

  if (return_value)
    {
      memset (bits, '\0', sizeof (LITTLENUM_TYPE) * MAX_PRECISION);

      /* Use more LittleNums than seems
         necessary: the highest flonum may have
         15 leading 0 bits, so could be useless.  */
      f.high = f.low + precision - 1 + GUARD;

      if (atof_generic (&return_value, ".", "eE", &f))
	{
	  make_invalid_floating_point_number (words);
	  return_value = NULL;
	}
      else if (flonum_gen2vax (what_kind, &f, words))
	return_value = NULL;
    }

  return return_value;
}

/* In: a flonum, a vax floating point format.
   Out: a vax floating-point bit pattern.  */

int
flonum_gen2vax (int format_letter,	/* One of 'd' 'f' 'g' 'h'.  */
		FLONUM_TYPE *f,
		LITTLENUM_TYPE *words)	/* Deliver answer here.  */
{
  LITTLENUM_TYPE *lp;
  int precision;
  long exponent_bits;
  int return_value;		/* 0 == OK.  */

  return_value = what_kind_of_float (format_letter, &precision, &exponent_bits);

  if (return_value != 0)
    make_invalid_floating_point_number (words);

  else
    {
      if (f->low > f->leader)
	/* 0.0e0 seen.  */
	memset (words, '\0', sizeof (LITTLENUM_TYPE) * precision);

      else
	{
	  long exponent_1;
	  long exponent_2;
	  long exponent_3;
	  long exponent_4;
	  int exponent_skippage;
	  LITTLENUM_TYPE word1;

	  /* JF: Deal with new Nan, +Inf and -Inf codes.  */
	  if (f->sign != '-' && f->sign != '+')
	    {
	      make_invalid_floating_point_number (words);
	      return return_value;
	    }

	  /* All vaxen floating_point formats (so far) have:
	     Bit 15 is sign bit.
	     Bits 14:n are excess-whatever exponent.
	     Bits n-1:0 (if any) are most significant bits of fraction.
	     Bits 15:0 of the next word are the next most significant bits.
	     And so on for each other word.

	     All this to be compatible with a KF11?? (Which is still faster
	     than lots of vaxen I can think of, but it also has higher
	     maintenance costs ... sigh).

	     So we need: number of bits of exponent, number of bits of
	     mantissa.  */

	  bits_left_in_littlenum = LITTLENUM_NUMBER_OF_BITS;
	  littlenum_pointer = f->leader;
	  littlenum_end = f->low;
	  /* Seek (and forget) 1st significant bit.  */
	  for (exponent_skippage = 0;
	       !next_bits (1);
	       exponent_skippage++);;

	  exponent_1 = f->exponent + f->leader + 1 - f->low;
	  /* Radix LITTLENUM_RADIX, point just higher than f->leader.  */
	  exponent_2 = exponent_1 * LITTLENUM_NUMBER_OF_BITS;
	  /* Radix 2.  */
	  exponent_3 = exponent_2 - exponent_skippage;
	  /* Forget leading zeros, forget 1st bit.  */
	  exponent_4 = exponent_3 + (1 << (exponent_bits - 1));
	  /* Offset exponent.  */

	  if (exponent_4 & ~mask[exponent_bits])
	    {
	      /* Exponent overflow. Lose immediately.  */
	      make_invalid_floating_point_number (words);

	      /* We leave return_value alone: admit we read the
	         number, but return a floating exception
	         because we can't encode the number.  */
	    }
	  else
	    {
	      lp = words;

	      /* Word 1. Sign, exponent and perhaps high bits.
	         Assume 2's complement integers.  */
	      word1 = (((exponent_4 & mask[exponent_bits]) << (15 - exponent_bits))
		       | ((f->sign == '+') ? 0 : 0x8000)
		       | next_bits (15 - exponent_bits));
	      *lp++ = word1;

	      /* The rest of the words are just mantissa bits.  */
	      for (; lp < words + precision; lp++)
		*lp = next_bits (LITTLENUM_NUMBER_OF_BITS);

	      if (next_bits (1))
		{
		  /* Since the NEXT bit is a 1, round UP the mantissa.
		     The cunning design of these hidden-1 floats permits
		     us to let the mantissa overflow into the exponent, and
		     it 'does the right thing'. However, we lose if the
		     highest-order bit of the lowest-order word flips.
		     Is that clear?  */
		  unsigned long carry;

		  /*
		    #if (sizeof(carry)) < ((sizeof(bits[0]) * BITS_PER_CHAR) + 2)
		    Please allow at least 1 more bit in carry than is in a LITTLENUM.
		    We need that extra bit to hold a carry during a LITTLENUM carry
		    propagation. Another extra bit (kept 0) will assure us that we
		    don't get a sticky sign bit after shifting right, and that
		    permits us to propagate the carry without any masking of bits.
		    #endif   */
		  for (carry = 1, lp--;
		       carry && (lp >= words);
		       lp--)
		    {
		      carry = *lp + carry;
		      *lp = carry;
		      carry >>= LITTLENUM_NUMBER_OF_BITS;
		    }

		  if ((word1 ^ *words) & (1 << (LITTLENUM_NUMBER_OF_BITS - 1)))
		    {
		      make_invalid_floating_point_number (words);
		      /* We leave return_value alone: admit we read the
		         number, but return a floating exception
		         because we can't encode the number.  */
		    }
		}
	    }
	}
    }
  return return_value;
}

/* JF this used to be in vax.c but this looks like a better place for it.  */

/* In:	input_line_pointer->the 1st character of a floating-point
  		number.
  	1 letter denoting the type of statement that wants a
  		binary floating point number returned.
  	Address of where to build floating point literal.
  		Assumed to be 'big enough'.
  	Address of where to return size of literal (in chars).
  
   Out:	Input_line_pointer->of next char after floating number.
  	Error message, or 0.
  	Floating point literal.
  	Number of chars we used for the literal.  */

#define MAXIMUM_NUMBER_OF_LITTLENUMS  8 	/* For .hfloats.  */

char *
md_atof (int what_statement_type,
	 char *literalP,
	 int *sizeP)
{
  LITTLENUM_TYPE words[MAXIMUM_NUMBER_OF_LITTLENUMS];
  char kind_of_float;
  unsigned int number_of_chars;
  LITTLENUM_TYPE *littlenumP;

  switch (what_statement_type)
    {
    case 'F':
    case 'f':
      kind_of_float = 'f';
      break;

    case 'D':
    case 'd':
      kind_of_float = 'd';
      break;

    case 'g':
      kind_of_float = 'g';
      break;

    case 'h':
      kind_of_float = 'h';
      break;

    default:
      kind_of_float = 0;
      break;
    };

  if (kind_of_float)
    {
      LITTLENUM_TYPE *limit;

      input_line_pointer = atof_vax (input_line_pointer,
				     kind_of_float,
				     words);
      /* The atof_vax() builds up 16-bit numbers.
         Since the assembler may not be running on
         a little-endian machine, be very careful about
         converting words to chars.  */
      number_of_chars = atof_vax_sizeof (kind_of_float);
      know (number_of_chars <= MAXIMUM_NUMBER_OF_LITTLENUMS * sizeof (LITTLENUM_TYPE));
      limit = words + (number_of_chars / sizeof (LITTLENUM_TYPE));
      for (littlenumP = words; littlenumP < limit; littlenumP++)
	{
	  md_number_to_chars (literalP, *littlenumP, sizeof (LITTLENUM_TYPE));
	  literalP += sizeof (LITTLENUM_TYPE);
	};
    }
  else
    number_of_chars = 0;

  *sizeP = number_of_chars;
  return kind_of_float ? NULL : _("Bad call to md_atof()");
}
