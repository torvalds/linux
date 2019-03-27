/* Definitions of target machine for GNU compiler, for ARM with a.out
   Copyright (C) 1995, 1996, 1997, 1998, 1999, 2000, 2004
   Free Software Foundation, Inc.
   Contributed by Richard Earnshaw (rearnsha@armltd.co.uk).
   
   This file is part of GCC.

   GCC is free software; you can redistribute it and/or modify it
   under the terms of the GNU General Public License as published
   by the Free Software Foundation; either version 2, or (at your
   option) any later version.

   GCC is distributed in the hope that it will be useful, but WITHOUT
   ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
   or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public
   License for more details.

   You should have received a copy of the GNU General Public License
   along with GCC; see the file COPYING.  If not, write to
   the Free Software Foundation, 51 Franklin Street, Fifth Floor,
   Boston, MA 02110-1301, USA.  */

#ifndef ASM_APP_ON
#define ASM_APP_ON  		""
#endif
#ifndef ASM_APP_OFF
#define ASM_APP_OFF  		""
#endif

/* Switch to the text or data segment.  */
#define TEXT_SECTION_ASM_OP  	"\t.text"
#define DATA_SECTION_ASM_OP  	"\t.data"
#define BSS_SECTION_ASM_OP   	"\t.bss"

/* Note: If USER_LABEL_PREFIX or LOCAL_LABEL_PREFIX are changed,
   make sure that this change is reflected in the function
   coff_arm_is_local_label_name() in bfd/coff-arm.c.  */
#ifndef REGISTER_PREFIX
#define REGISTER_PREFIX 	""
#endif

#ifndef USER_LABEL_PREFIX
#define USER_LABEL_PREFIX 	"_"
#endif

#ifndef LOCAL_LABEL_PREFIX
#define LOCAL_LABEL_PREFIX 	""
#endif

/* The assembler's names for the registers.  */
#ifndef REGISTER_NAMES
#define REGISTER_NAMES				   \
{				                   \
  "r0", "r1", "r2", "r3", "r4", "r5", "r6", "r7",  \
  "r8", "r9", "sl", "fp", "ip", "sp", "lr", "pc",  \
  "f0", "f1", "f2", "f3", "f4", "f5", "f6", "f7",  \
  "cc", "sfp", "afp",		   		   \
  "mv0",   "mv1",   "mv2",   "mv3",		   \
  "mv4",   "mv5",   "mv6",   "mv7",		   \
  "mv8",   "mv9",   "mv10",  "mv11",		   \
  "mv12",  "mv13",  "mv14",  "mv15",		   \
  "wcgr0", "wcgr1", "wcgr2", "wcgr3",		   \
  "wr0",   "wr1",   "wr2",   "wr3",		   \
  "wr4",   "wr5",   "wr6",   "wr7",		   \
  "wr8",   "wr9",   "wr10",  "wr11",		   \
  "wr12",  "wr13",  "wr14",  "wr15",		   \
  "s0",  "s1",  "s2",  "s3",  "s4",  "s5",  "s6",  "s7",  \
  "s8",  "s9",  "s10", "s11", "s12", "s13", "s14", "s15", \
  "s16", "s17", "s18", "s19", "s20", "s21", "s22", "s23", \
  "s24", "s25", "s26", "s27", "s28", "s29", "s30", "s31", \
  "vfpcc"					   \
}
#endif

#ifndef ADDITIONAL_REGISTER_NAMES
#define ADDITIONAL_REGISTER_NAMES		\
{						\
  {"a1", 0},					\
  {"a2", 1},					\
  {"a3", 2},					\
  {"a4", 3},					\
  {"v1", 4},					\
  {"v2", 5},					\
  {"v3", 6},					\
  {"v4", 7},					\
  {"v5", 8},					\
  {"v6", 9},					\
  {"rfp", 9}, /* Gcc used to call it this */	\
  {"sb", 9},					\
  {"v7", 10},					\
  {"r10", 10},	/* sl */			\
  {"r11", 11},	/* fp */			\
  {"r12", 12},	/* ip */			\
  {"r13", 13},	/* sp */			\
  {"r14", 14},	/* lr */			\
  {"r15", 15},	/* pc */			\
  {"mvf0", 27},					\
  {"mvf1", 28},					\
  {"mvf2", 29},					\
  {"mvf3", 30},					\
  {"mvf4", 31},					\
  {"mvf5", 32},					\
  {"mvf6", 33},					\
  {"mvf7", 34},					\
  {"mvf8", 35},					\
  {"mvf9", 36},					\
  {"mvf10", 37},				\
  {"mvf11", 38},				\
  {"mvf12", 39},				\
  {"mvf13", 40},				\
  {"mvf14", 41},				\
  {"mvf15", 42},				\
  {"mvd0", 27},					\
  {"mvd1", 28},					\
  {"mvd2", 29},					\
  {"mvd3", 30},					\
  {"mvd4", 31},					\
  {"mvd5", 32},					\
  {"mvd6", 33},					\
  {"mvd7", 34},					\
  {"mvd8", 35},					\
  {"mvd9", 36},					\
  {"mvd10", 37},				\
  {"mvd11", 38},				\
  {"mvd12", 39},				\
  {"mvd13", 40},				\
  {"mvd14", 41},				\
  {"mvd15", 42},				\
  {"mvfx0", 27},				\
  {"mvfx1", 28},				\
  {"mvfx2", 29},				\
  {"mvfx3", 30},				\
  {"mvfx4", 31},				\
  {"mvfx5", 32},				\
  {"mvfx6", 33},				\
  {"mvfx7", 34},				\
  {"mvfx8", 35},				\
  {"mvfx9", 36},				\
  {"mvfx10", 37},				\
  {"mvfx11", 38},				\
  {"mvfx12", 39},				\
  {"mvfx13", 40},				\
  {"mvfx14", 41},				\
  {"mvfx15", 42},				\
  {"mvdx0", 27},				\
  {"mvdx1", 28},				\
  {"mvdx2", 29},				\
  {"mvdx3", 30},				\
  {"mvdx4", 31},				\
  {"mvdx5", 32},				\
  {"mvdx6", 33},				\
  {"mvdx7", 34},				\
  {"mvdx8", 35},				\
  {"mvdx9", 36},				\
  {"mvdx10", 37},				\
  {"mvdx11", 38},				\
  {"mvdx12", 39},				\
  {"mvdx13", 40},				\
  {"mvdx14", 41},				\
  {"mvdx15", 42},				\
  {"d0", 63},					\
  {"d1", 65},					\
  {"d2", 67},					\
  {"d3", 69},					\
  {"d4", 71},					\
  {"d5", 73},					\
  {"d6", 75},					\
  {"d7", 77},					\
  {"d8", 79},					\
  {"d9", 81},					\
  {"d10", 83},					\
  {"d11", 85},					\
  {"d12", 87},					\
  {"d13", 89},					\
  {"d14", 91},					\
  {"d15", 93},					\
}
#endif

/* Arm Assembler barfs on dollars.  */
#define DOLLARS_IN_IDENTIFIERS 0

#ifndef NO_DOLLAR_IN_LABEL
#define NO_DOLLAR_IN_LABEL 1
#endif

/* Generate DBX debugging information.  riscix.h will undefine this because
   the native assembler does not support stabs.  */
#define DBX_DEBUGGING_INFO 1

/* Acorn dbx moans about continuation chars, so don't use any.  */
#ifndef DBX_CONTIN_LENGTH
#define DBX_CONTIN_LENGTH  0
#endif

/* Output a function label definition.  */
#ifndef ASM_DECLARE_FUNCTION_NAME
#define ASM_DECLARE_FUNCTION_NAME(STREAM, NAME, DECL)	\
  do							\
    {							\
      ARM_DECLARE_FUNCTION_NAME (STREAM, NAME, DECL);   \
      ASM_OUTPUT_LABEL (STREAM, NAME);			\
    }							\
  while (0)
#endif

/* Globalizing directive for a label.  */
#define GLOBAL_ASM_OP "\t.global\t"

/* Make an internal label into a string.  */
#ifndef ASM_GENERATE_INTERNAL_LABEL
#define ASM_GENERATE_INTERNAL_LABEL(STRING, PREFIX, NUM)  \
  sprintf (STRING, "*%s%s%u", LOCAL_LABEL_PREFIX, PREFIX, (unsigned int)(NUM))
#endif
     
/* Output an element of a dispatch table.  */
#define ASM_OUTPUT_ADDR_VEC_ELT(STREAM, VALUE)  \
  asm_fprintf (STREAM, "\t.word\t%LL%d\n", VALUE)

#define ASM_OUTPUT_ADDR_DIFF_ELT(STREAM, BODY, VALUE, REL)		\
  do									\
    {									\
      if (TARGET_ARM)							\
	asm_fprintf (STREAM, "\tb\t%LL%d\n", VALUE);			\
      else								\
	asm_fprintf (STREAM, "\t.word\t%LL%d-%LL%d\n", VALUE, REL);	\
    }									\
  while (0)


#undef  ASM_OUTPUT_ASCII
#define ASM_OUTPUT_ASCII(STREAM, PTR, LEN)  \
  output_ascii_pseudo_op (STREAM, (const unsigned char *) (PTR), LEN)

/* Output a gap.  In fact we fill it with nulls.  */
#undef  ASM_OUTPUT_SKIP
#define ASM_OUTPUT_SKIP(STREAM, NBYTES) 	\
  fprintf (STREAM, "\t.space\t%d\n", (int) (NBYTES))

/* Align output to a power of two.  Horrible /bin/as.  */
#ifndef ASM_OUTPUT_ALIGN  
#define ASM_OUTPUT_ALIGN(STREAM, POWER)			\
  do							\
    {							\
      register int amount = 1 << (POWER);		\
							\
      if (amount == 2)					\
	fprintf (STREAM, "\t.even\n");			\
      else if (amount != 1)				\
	fprintf (STREAM, "\t.align\t%d\n", amount - 4);	\
    }							\
  while (0)
#endif

/* Output a common block.  */
#ifndef ASM_OUTPUT_COMMON
#define ASM_OUTPUT_COMMON(STREAM, NAME, SIZE, ROUNDED)	\
  do							\
    {							\
      fprintf (STREAM, "\t.comm\t");			\
      assemble_name (STREAM, NAME);			\
      asm_fprintf (STREAM, ", %d\t%@ %d\n", 		\
	           (int)(ROUNDED), (int)(SIZE));	\
    }							\
  while (0)
#endif
     
/* Output a local common block.  /bin/as can't do this, so hack a
   `.space' into the bss segment.  Note that this is *bad* practice,
   which is guaranteed NOT to work since it doesn't define STATIC
   COMMON space but merely STATIC BSS space.  */
#ifndef ASM_OUTPUT_ALIGNED_LOCAL
#define ASM_OUTPUT_ALIGNED_LOCAL(STREAM, NAME, SIZE, ALIGN)		\
  do									\
    {									\
      switch_to_section (bss_section);					\
      ASM_OUTPUT_ALIGN (STREAM, floor_log2 (ALIGN / BITS_PER_UNIT));	\
      ASM_OUTPUT_LABEL (STREAM, NAME);					\
      fprintf (STREAM, "\t.space\t%d\n", (int)(SIZE));			\
    }									\
  while (0)
#endif
     
/* Output a zero-initialized block.  */
#ifndef ASM_OUTPUT_ALIGNED_BSS
#define ASM_OUTPUT_ALIGNED_BSS(STREAM, DECL, NAME, SIZE, ALIGN) \
  asm_output_aligned_bss (STREAM, DECL, NAME, SIZE, ALIGN)
#endif

/* Output a #ident directive.  */
#ifndef ASM_OUTPUT_IDENT
#define ASM_OUTPUT_IDENT(STREAM,STRING)  \
  asm_fprintf (STREAM, "%@ - - - ident %s\n", STRING)
#endif
     
#ifndef ASM_COMMENT_START
#define ASM_COMMENT_START 	"@"
#endif

/* This works for GAS and some other assemblers.  */
#define SET_ASM_OP		"\t.set\t"
