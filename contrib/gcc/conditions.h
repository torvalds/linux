/* Definitions for condition code handling in final.c and output routines.
   Copyright (C) 1987 Free Software Foundation, Inc.

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

/* None of the things in the files exist if we don't use CC0.  */

#ifdef HAVE_cc0

/* The variable cc_status says how to interpret the condition code.
   It is set by output routines for an instruction that sets the cc's
   and examined by output routines for jump instructions.

   cc_status contains two components named `value1' and `value2'
   that record two equivalent expressions for the values that the
   condition codes were set from.  (Either or both may be null if
   there is no useful expression to record.)  These fields are
   used for eliminating redundant test and compare instructions
   in the cases where the condition codes were already set by the
   previous instruction.

   cc_status.flags contains flags which say that the condition codes
   were set in a nonstandard manner.  The output of jump instructions
   uses these flags to compensate and produce the standard result
   with the nonstandard condition codes.  Standard flags are defined here.
   The tm.h file can also define other machine-dependent flags.

   cc_status also contains a machine-dependent component `mdep'
   whose type, `CC_STATUS_MDEP', may be defined as a macro in the
   tm.h file.  */

#ifndef CC_STATUS_MDEP
#define CC_STATUS_MDEP int
#endif

#ifndef CC_STATUS_MDEP_INIT
#define CC_STATUS_MDEP_INIT 0
#endif

typedef struct {int flags; rtx value1, value2; CC_STATUS_MDEP mdep;} CC_STATUS;

/* While outputting an insn as assembler code,
   this is the status BEFORE that insn.  */
extern CC_STATUS cc_prev_status;

/* While outputting an insn as assembler code,
   this is being altered to the status AFTER that insn.  */
extern CC_STATUS cc_status;

/* These are the machine-independent flags:  */

/* Set if the sign of the cc value is inverted:
   output a following jump-if-less as a jump-if-greater, etc.  */
#define CC_REVERSED 1

/* This bit means that the current setting of the N bit is bogus
   and conditional jumps should use the Z bit in its place.
   This state obtains when an extraction of a signed single-bit field
   or an arithmetic shift right of a byte by 7 bits
   is turned into a btst, because btst does not set the N bit.  */
#define CC_NOT_POSITIVE 2

/* This bit means that the current setting of the N bit is bogus
   and conditional jumps should pretend that the N bit is clear.
   Used after extraction of an unsigned bit
   or logical shift right of a byte by 7 bits is turned into a btst.
   The btst does not alter the N bit, but the result of that shift
   or extract is never negative.  */
#define CC_NOT_NEGATIVE 4

/* This bit means that the current setting of the overflow flag
   is bogus and conditional jumps should pretend there is no overflow.  */
/* ??? Note that for most targets this macro is misnamed as it applies
   to the carry flag, not the overflow flag.  */
#define CC_NO_OVERFLOW 010

/* This bit means that what ought to be in the Z bit
   should be tested as the complement of the N bit.  */
#define CC_Z_IN_NOT_N 020

/* This bit means that what ought to be in the Z bit
   should be tested as the N bit.  */
#define CC_Z_IN_N 040

/* Nonzero if we must invert the sense of the following branch, i.e.
   change EQ to NE.  This is not safe for IEEE floating point operations!
   It is intended for use only when a combination of arithmetic
   or logical insns can leave the condition codes set in a fortuitous
   (though inverted) state.  */
#define CC_INVERTED 0100

/* Nonzero if we must convert signed condition operators to unsigned.
   This is only used by machine description files.  */
#define CC_NOT_SIGNED 0200

/* This is how to initialize the variable cc_status.
   final does this at appropriate moments.  */

#define CC_STATUS_INIT  \
 (cc_status.flags = 0, cc_status.value1 = 0, cc_status.value2 = 0,  \
  CC_STATUS_MDEP_INIT)

#endif
