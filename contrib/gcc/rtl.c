/* RTL utility routines.
   Copyright (C) 1987, 1988, 1991, 1994, 1997, 1998, 1999, 2000, 2001, 2002,
   2003, 2004, 2005, 2006 Free Software Foundation, Inc.

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free
Software Foundation; either version 2, or (at your option) any later
version.

GCC is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING.  If not, write to the Free
Software Foundation, 51 Franklin Street, Fifth Floor, Boston, MA
02110-1301, USA.  */

/* This file is compiled twice: once for the generator programs
   once for the compiler.  */
#ifdef GENERATOR_FILE
#include "bconfig.h"
#else
#include "config.h"
#endif

#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "rtl.h"
#include "real.h"
#include "ggc.h"
#ifdef GENERATOR_FILE
# include "errors.h"
#else
# include "toplev.h"
#endif


/* Indexed by rtx code, gives number of operands for an rtx with that code.
   Does NOT include rtx header data (code and links).  */

#define DEF_RTL_EXPR(ENUM, NAME, FORMAT, CLASS)   sizeof FORMAT - 1 ,

const unsigned char rtx_length[NUM_RTX_CODE] = {
#include "rtl.def"
};

#undef DEF_RTL_EXPR

/* Indexed by rtx code, gives the name of that kind of rtx, as a C string.  */

#define DEF_RTL_EXPR(ENUM, NAME, FORMAT, CLASS)   NAME ,

const char * const rtx_name[NUM_RTX_CODE] = {
#include "rtl.def"		/* rtl expressions are documented here */
};

#undef DEF_RTL_EXPR

/* Indexed by rtx code, gives a sequence of operand-types for
   rtx's of that code.  The sequence is a C string in which
   each character describes one operand.  */

const char * const rtx_format[NUM_RTX_CODE] = {
  /* "*" undefined.
         can cause a warning message
     "0" field is unused (or used in a phase-dependent manner)
         prints nothing
     "i" an integer
         prints the integer
     "n" like "i", but prints entries from `note_insn_name'
     "w" an integer of width HOST_BITS_PER_WIDE_INT
         prints the integer
     "s" a pointer to a string
         prints the string
     "S" like "s", but optional:
	 the containing rtx may end before this operand
     "T" like "s", but treated specially by the RTL reader;
         only found in machine description patterns.
     "e" a pointer to an rtl expression
         prints the expression
     "E" a pointer to a vector that points to a number of rtl expressions
         prints a list of the rtl expressions
     "V" like "E", but optional:
	 the containing rtx may end before this operand
     "u" a pointer to another insn
         prints the uid of the insn.
     "b" is a pointer to a bitmap header.
     "B" is a basic block pointer.
     "t" is a tree pointer.  */

#define DEF_RTL_EXPR(ENUM, NAME, FORMAT, CLASS)   FORMAT ,
#include "rtl.def"		/* rtl expressions are defined here */
#undef DEF_RTL_EXPR
};

/* Indexed by rtx code, gives a character representing the "class" of
   that rtx code.  See rtl.def for documentation on the defined classes.  */

const enum rtx_class rtx_class[NUM_RTX_CODE] = {
#define DEF_RTL_EXPR(ENUM, NAME, FORMAT, CLASS)   CLASS,
#include "rtl.def"		/* rtl expressions are defined here */
#undef DEF_RTL_EXPR
};

/* Indexed by rtx code, gives the size of the rtx in bytes.  */

const unsigned char rtx_code_size[NUM_RTX_CODE] = {
#define DEF_RTL_EXPR(ENUM, NAME, FORMAT, CLASS)				\
  ((ENUM) == CONST_INT || (ENUM) == CONST_DOUBLE			\
   ? RTX_HDR_SIZE + (sizeof FORMAT - 1) * sizeof (HOST_WIDE_INT)	\
   : RTX_HDR_SIZE + (sizeof FORMAT - 1) * sizeof (rtunion)),

#include "rtl.def"
#undef DEF_RTL_EXPR
};

/* Make sure all NOTE_INSN_* values are negative.  */
extern char NOTE_INSN_MAX_isnt_negative_adjust_NOTE_INSN_BIAS
[NOTE_INSN_MAX < 0 ? 1 : -1];

/* Names for kinds of NOTEs and REG_NOTEs.  */

const char * const note_insn_name[NOTE_INSN_MAX - NOTE_INSN_BIAS] =
{
  "",
#define DEF_INSN_NOTE(NAME) #NAME,
#include "insn-notes.def"
#undef DEF_INSN_NOTE
};

const char * const reg_note_name[REG_NOTE_MAX] =
{
#define DEF_REG_NOTE(NAME) #NAME,
#include "reg-notes.def"
#undef DEF_REG_NOTE
};

#ifdef GATHER_STATISTICS
static int rtx_alloc_counts[(int) LAST_AND_UNUSED_RTX_CODE];
static int rtx_alloc_sizes[(int) LAST_AND_UNUSED_RTX_CODE];
static int rtvec_alloc_counts;
static int rtvec_alloc_sizes;
#endif


/* Allocate an rtx vector of N elements.
   Store the length, and initialize all elements to zero.  */

rtvec
rtvec_alloc (int n)
{
  rtvec rt;

  rt = ggc_alloc_rtvec (n);
  /* Clear out the vector.  */
  memset (&rt->elem[0], 0, n * sizeof (rtx));

  PUT_NUM_ELEM (rt, n);

#ifdef GATHER_STATISTICS
  rtvec_alloc_counts++;
  rtvec_alloc_sizes += n * sizeof (rtx);
#endif

  return rt;
}

/* Return the number of bytes occupied by rtx value X.  */

unsigned int
rtx_size (rtx x)
{
  if (GET_CODE (x) == SYMBOL_REF && SYMBOL_REF_HAS_BLOCK_INFO_P (x))
    return RTX_HDR_SIZE + sizeof (struct block_symbol);
  return RTX_CODE_SIZE (GET_CODE (x));
}

/* Allocate an rtx of code CODE.  The CODE is stored in the rtx;
   all the rest is initialized to zero.  */

rtx
rtx_alloc_stat (RTX_CODE code MEM_STAT_DECL)
{
  rtx rt;

  rt = (rtx) ggc_alloc_zone_pass_stat (RTX_CODE_SIZE (code), &rtl_zone);

  /* We want to clear everything up to the FLD array.  Normally, this
     is one int, but we don't want to assume that and it isn't very
     portable anyway; this is.  */

  memset (rt, 0, RTX_HDR_SIZE);
  PUT_CODE (rt, code);

#ifdef GATHER_STATISTICS
  rtx_alloc_counts[code]++;
  rtx_alloc_sizes[code] += RTX_CODE_SIZE (code);
#endif

  return rt;
}


/* Create a new copy of an rtx.
   Recursively copies the operands of the rtx,
   except for those few rtx codes that are sharable.  */

rtx
copy_rtx (rtx orig)
{
  rtx copy;
  int i, j;
  RTX_CODE code;
  const char *format_ptr;

  code = GET_CODE (orig);

  switch (code)
    {
    case REG:
    case CONST_INT:
    case CONST_DOUBLE:
    case CONST_VECTOR:
    case SYMBOL_REF:
    case CODE_LABEL:
    case PC:
    case CC0:
    case SCRATCH:
      /* SCRATCH must be shared because they represent distinct values.  */
      return orig;
    case CLOBBER:
      if (REG_P (XEXP (orig, 0)) && REGNO (XEXP (orig, 0)) < FIRST_PSEUDO_REGISTER)
	return orig;
      break;

    case CONST:
      /* CONST can be shared if it contains a SYMBOL_REF.  If it contains
	 a LABEL_REF, it isn't sharable.  */
      if (GET_CODE (XEXP (orig, 0)) == PLUS
	  && GET_CODE (XEXP (XEXP (orig, 0), 0)) == SYMBOL_REF
	  && GET_CODE (XEXP (XEXP (orig, 0), 1)) == CONST_INT)
	return orig;
      break;

      /* A MEM with a constant address is not sharable.  The problem is that
	 the constant address may need to be reloaded.  If the mem is shared,
	 then reloading one copy of this mem will cause all copies to appear
	 to have been reloaded.  */

    default:
      break;
    }

  /* Copy the various flags, fields, and other information.  We assume
     that all fields need copying, and then clear the fields that should
     not be copied.  That is the sensible default behavior, and forces
     us to explicitly document why we are *not* copying a flag.  */
  copy = shallow_copy_rtx (orig);

  /* We do not copy the USED flag, which is used as a mark bit during
     walks over the RTL.  */
  RTX_FLAG (copy, used) = 0;

  /* We do not copy FRAME_RELATED for INSNs.  */
  if (INSN_P (orig))
    RTX_FLAG (copy, frame_related) = 0;
  RTX_FLAG (copy, jump) = RTX_FLAG (orig, jump);
  RTX_FLAG (copy, call) = RTX_FLAG (orig, call);

  format_ptr = GET_RTX_FORMAT (GET_CODE (copy));

  for (i = 0; i < GET_RTX_LENGTH (GET_CODE (copy)); i++)
    switch (*format_ptr++)
      {
      case 'e':
	if (XEXP (orig, i) != NULL)
	  XEXP (copy, i) = copy_rtx (XEXP (orig, i));
	break;

      case 'E':
      case 'V':
	if (XVEC (orig, i) != NULL)
	  {
	    XVEC (copy, i) = rtvec_alloc (XVECLEN (orig, i));
	    for (j = 0; j < XVECLEN (copy, i); j++)
	      XVECEXP (copy, i, j) = copy_rtx (XVECEXP (orig, i, j));
	  }
	break;

      case 't':
      case 'w':
      case 'i':
      case 's':
      case 'S':
      case 'T':
      case 'u':
      case 'B':
      case '0':
	/* These are left unchanged.  */
	break;

      default:
	gcc_unreachable ();
      }
  return copy;
}

/* Create a new copy of an rtx.  Only copy just one level.  */

rtx
shallow_copy_rtx_stat (rtx orig MEM_STAT_DECL)
{
  unsigned int size;
  rtx copy;

  size = rtx_size (orig);
  copy = (rtx) ggc_alloc_zone_pass_stat (size, &rtl_zone);
  memcpy (copy, orig, size);
  return copy;
}

/* Nonzero when we are generating CONCATs.  */
int generating_concat_p;

/* Nonzero when we are expanding trees to RTL.  */
int currently_expanding_to_rtl;


/* Return 1 if X and Y are identical-looking rtx's.
   This is the Lisp function EQUAL for rtx arguments.  */

int
rtx_equal_p (rtx x, rtx y)
{
  int i;
  int j;
  enum rtx_code code;
  const char *fmt;

  if (x == y)
    return 1;
  if (x == 0 || y == 0)
    return 0;

  code = GET_CODE (x);
  /* Rtx's of different codes cannot be equal.  */
  if (code != GET_CODE (y))
    return 0;

  /* (MULT:SI x y) and (MULT:HI x y) are NOT equivalent.
     (REG:SI x) and (REG:HI x) are NOT equivalent.  */

  if (GET_MODE (x) != GET_MODE (y))
    return 0;

  /* Some RTL can be compared nonrecursively.  */
  switch (code)
    {
    case REG:
      return (REGNO (x) == REGNO (y));

    case LABEL_REF:
      return XEXP (x, 0) == XEXP (y, 0);

    case SYMBOL_REF:
      return XSTR (x, 0) == XSTR (y, 0);

    case SCRATCH:
    case CONST_DOUBLE:
    case CONST_INT:
      return 0;

    default:
      break;
    }

  /* Compare the elements.  If any pair of corresponding elements
     fail to match, return 0 for the whole thing.  */

  fmt = GET_RTX_FORMAT (code);
  for (i = GET_RTX_LENGTH (code) - 1; i >= 0; i--)
    {
      switch (fmt[i])
	{
	case 'w':
	  if (XWINT (x, i) != XWINT (y, i))
	    return 0;
	  break;

	case 'n':
	case 'i':
	  if (XINT (x, i) != XINT (y, i))
	    return 0;
	  break;

	case 'V':
	case 'E':
	  /* Two vectors must have the same length.  */
	  if (XVECLEN (x, i) != XVECLEN (y, i))
	    return 0;

	  /* And the corresponding elements must match.  */
	  for (j = 0; j < XVECLEN (x, i); j++)
	    if (rtx_equal_p (XVECEXP (x, i, j), XVECEXP (y, i, j)) == 0)
	      return 0;
	  break;

	case 'e':
	  if (rtx_equal_p (XEXP (x, i), XEXP (y, i)) == 0)
	    return 0;
	  break;

	case 'S':
	case 's':
	  if ((XSTR (x, i) || XSTR (y, i))
	      && (! XSTR (x, i) || ! XSTR (y, i)
		  || strcmp (XSTR (x, i), XSTR (y, i))))
	    return 0;
	  break;

	case 'u':
	  /* These are just backpointers, so they don't matter.  */
	  break;

	case '0':
	case 't':
	  break;

	  /* It is believed that rtx's at this level will never
	     contain anything but integers and other rtx's,
	     except for within LABEL_REFs and SYMBOL_REFs.  */
	default:
	  gcc_unreachable ();
	}
    }
  return 1;
}

void
dump_rtx_statistics (void)
{
#ifdef GATHER_STATISTICS
  int i;
  int total_counts = 0;
  int total_sizes = 0;
  fprintf (stderr, "\nRTX Kind               Count      Bytes\n");
  fprintf (stderr, "---------------------------------------\n");
  for (i = 0; i < LAST_AND_UNUSED_RTX_CODE; i++)
    if (rtx_alloc_counts[i])
      {
        fprintf (stderr, "%-20s %7d %10d\n", GET_RTX_NAME (i),
                 rtx_alloc_counts[i], rtx_alloc_sizes[i]);
        total_counts += rtx_alloc_counts[i];
        total_sizes += rtx_alloc_sizes[i];
      }
  if (rtvec_alloc_counts)
    {
      fprintf (stderr, "%-20s %7d %10d\n", "rtvec",
               rtvec_alloc_counts, rtvec_alloc_sizes);
      total_counts += rtvec_alloc_counts;
      total_sizes += rtvec_alloc_sizes;
    }
  fprintf (stderr, "---------------------------------------\n");
  fprintf (stderr, "%-20s %7d %10d\n",
           "Total", total_counts, total_sizes);
  fprintf (stderr, "---------------------------------------\n");
#endif  
}

#if defined ENABLE_RTL_CHECKING && (GCC_VERSION >= 2007)
void
rtl_check_failed_bounds (rtx r, int n, const char *file, int line,
			 const char *func)
{
  internal_error
    ("RTL check: access of elt %d of '%s' with last elt %d in %s, at %s:%d",
     n, GET_RTX_NAME (GET_CODE (r)), GET_RTX_LENGTH (GET_CODE (r)) - 1,
     func, trim_filename (file), line);
}

void
rtl_check_failed_type1 (rtx r, int n, int c1, const char *file, int line,
			const char *func)
{
  internal_error
    ("RTL check: expected elt %d type '%c', have '%c' (rtx %s) in %s, at %s:%d",
     n, c1, GET_RTX_FORMAT (GET_CODE (r))[n], GET_RTX_NAME (GET_CODE (r)),
     func, trim_filename (file), line);
}

void
rtl_check_failed_type2 (rtx r, int n, int c1, int c2, const char *file,
			int line, const char *func)
{
  internal_error
    ("RTL check: expected elt %d type '%c' or '%c', have '%c' (rtx %s) in %s, at %s:%d",
     n, c1, c2, GET_RTX_FORMAT (GET_CODE (r))[n], GET_RTX_NAME (GET_CODE (r)),
     func, trim_filename (file), line);
}

void
rtl_check_failed_code1 (rtx r, enum rtx_code code, const char *file,
			int line, const char *func)
{
  internal_error ("RTL check: expected code '%s', have '%s' in %s, at %s:%d",
		  GET_RTX_NAME (code), GET_RTX_NAME (GET_CODE (r)), func,
		  trim_filename (file), line);
}

void
rtl_check_failed_code2 (rtx r, enum rtx_code code1, enum rtx_code code2,
			const char *file, int line, const char *func)
{
  internal_error
    ("RTL check: expected code '%s' or '%s', have '%s' in %s, at %s:%d",
     GET_RTX_NAME (code1), GET_RTX_NAME (code2), GET_RTX_NAME (GET_CODE (r)),
     func, trim_filename (file), line);
}

void
rtl_check_failed_code_mode (rtx r, enum rtx_code code, enum machine_mode mode,
			    bool not_mode, const char *file, int line,
			    const char *func)
{
  internal_error ((not_mode
		   ? ("RTL check: expected code '%s' and not mode '%s', "
		      "have code '%s' and mode '%s' in %s, at %s:%d")
		   : ("RTL check: expected code '%s' and mode '%s', "
		      "have code '%s' and mode '%s' in %s, at %s:%d")),
		  GET_RTX_NAME (code), GET_MODE_NAME (mode),
		  GET_RTX_NAME (GET_CODE (r)), GET_MODE_NAME (GET_MODE (r)),
		  func, trim_filename (file), line);
}

/* Report that line LINE of FILE tried to access the block symbol fields
   of a non-block symbol.  FUNC is the function that contains the line.  */

void
rtl_check_failed_block_symbol (const char *file, int line, const char *func)
{
  internal_error
    ("RTL check: attempt to treat non-block symbol as a block symbol "
     "in %s, at %s:%d", func, trim_filename (file), line);
}

/* XXX Maybe print the vector?  */
void
rtvec_check_failed_bounds (rtvec r, int n, const char *file, int line,
			   const char *func)
{
  internal_error
    ("RTL check: access of elt %d of vector with last elt %d in %s, at %s:%d",
     n, GET_NUM_ELEM (r) - 1, func, trim_filename (file), line);
}
#endif /* ENABLE_RTL_CHECKING */

#if defined ENABLE_RTL_FLAG_CHECKING
void
rtl_check_failed_flag (const char *name, rtx r, const char *file,
		       int line, const char *func)
{
  internal_error
    ("RTL flag check: %s used with unexpected rtx code '%s' in %s, at %s:%d",
     name, GET_RTX_NAME (GET_CODE (r)), func, trim_filename (file), line);
}
#endif /* ENABLE_RTL_FLAG_CHECKING */
