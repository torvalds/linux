/* Definitions for Intel 386 using GAS.
   Copyright (C) 1988, 1993, 1994, 1996, 2002, 2004
   Free Software Foundation, Inc.

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

/* Note that i386/seq-gas.h is a GAS configuration that does not use this
   file.  */

/* Use the bsd assembler syntax.  */
/* we need to do this because gas is really a bsd style assembler,
 * and so doesn't work well this these att-isms:
 *
 *  ASM_OUTPUT_SKIP is .set .,.+N, which isn't implemented in gas
 *  ASM_OUTPUT_LOCAL is done with .set .,.+N, but that can't be
 *   used to define bss static space
 *
 * Next is the question of whether to uses underscores.  RMS didn't
 * like this idea at first, but since it is now obvious that we
 * need this separate tm file for use with gas, at least to get
 * dbx debugging info, I think we should also switch to underscores.
 * We can keep i386v for real att style output, and the few
 * people who want both form will have to compile twice.
 */

/* these come from i386/bsd.h, but are specific to sequent */
#undef DBX_NO_XREFS
#undef DBX_CONTIN_LENGTH

/* Ask for COFF symbols.  */

#define SDB_DEBUGGING_INFO 1

/* Output #ident as a .ident.  */

#define ASM_OUTPUT_IDENT(FILE, NAME) fprintf (FILE, "\t.ident \"%s\"\n", NAME);

/* In the past there was confusion as to what the argument to .align was
   in GAS.  For the last several years the rule has been this: for a.out
   file formats that argument is LOG, and for all other file formats the
   argument is 1<<LOG.

   However, GAS now has .p2align and .balign pseudo-ops so to remove any
   doubt or guess work, and since this file is used for both a.out and other
   file formats, we use one of them.  */

#ifdef HAVE_GAS_BALIGN_AND_P2ALIGN 
#undef ASM_OUTPUT_ALIGN
#define ASM_OUTPUT_ALIGN(FILE,LOG) \
  if ((LOG)!=0) fprintf ((FILE), "\t.balign %d\n", 1<<(LOG))
#endif

/* A C statement to output to the stdio stream FILE an assembler
   command to advance the location counter to a multiple of 1<<LOG
   bytes if it is within MAX_SKIP bytes.

   This is used to align code labels according to Intel recommendations.  */

#ifdef HAVE_GAS_MAX_SKIP_P2ALIGN
#  define ASM_OUTPUT_MAX_SKIP_ALIGN(FILE,LOG,MAX_SKIP) \
     if ((LOG) != 0) {\
       if ((MAX_SKIP) == 0) fprintf ((FILE), "\t.p2align %d\n", (LOG)); \
       else fprintf ((FILE), "\t.p2align %d,,%d\n", (LOG), (MAX_SKIP)); \
     }
#endif

/* A C statement or statements which output an assembler instruction
   opcode to the stdio stream STREAM.  The macro-operand PTR is a
   variable of type `char *' which points to the opcode name in its
   "internal" form--the form that is written in the machine description.

   GAS version 1.38.1 doesn't understand the `repz' opcode mnemonic.
   So use `repe' instead.  */

#define ASM_OUTPUT_OPCODE(STREAM, PTR)	\
{									\
  if ((PTR)[0] == 'r'							\
      && (PTR)[1] == 'e'						\
      && (PTR)[2] == 'p')						\
    {									\
      if ((PTR)[3] == 'z')						\
	{								\
	  fprintf (STREAM, "repe");					\
	  (PTR) += 4;							\
	}								\
      else if ((PTR)[3] == 'n' && (PTR)[4] == 'z')			\
	{								\
	  fprintf (STREAM, "repne");					\
	  (PTR) += 5;							\
	}								\
    }									\
}

/* Define macro used to output shift-double opcodes when the shift
   count is in %cl.  Some assemblers require %cl as an argument;
   some don't.

   GAS requires the %cl argument, so override i386/unix.h.  */

#undef SHIFT_DOUBLE_OMITS_COUNT
#define SHIFT_DOUBLE_OMITS_COUNT 0

/* Print opcodes the way that GAS expects them.  */
#define GAS_MNEMONICS 1

/* The comment-starter string as GAS expects it. */
#undef ASM_COMMENT_START
#define ASM_COMMENT_START "#"

#define TARGET_ASM_FILE_START_FILE_DIRECTIVE true
