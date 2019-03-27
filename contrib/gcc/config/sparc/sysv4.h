/* Target definitions for GNU compiler for SPARC running System V.4
   Copyright (C) 1991, 1992, 1995, 1996, 1997, 1998, 2000, 2002
   Free Software Foundation, Inc.
   Contributed by Ron Guilmette (rfg@monkeys.com).

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

#ifndef TARGET_VERSION
#define TARGET_VERSION fprintf (stderr, " (sparc ELF)"); 
#endif

/* ??? Put back the SIZE_TYPE/PTRDIFF_TYPE definitions set by sparc.h.
   Why, exactly, is svr4.h messing with this?  Seems like the chip 
   would know best.  */

#undef SIZE_TYPE
#define SIZE_TYPE (TARGET_ARCH64 ? "long unsigned int" : "unsigned int")

#undef PTRDIFF_TYPE
#define PTRDIFF_TYPE (TARGET_ARCH64 ? "long int" : "int")

/* Undefined some symbols which are defined in "svr4.h" but which are
   appropriate only for typical svr4 systems, but not for the specific
   case of svr4 running on a SPARC.  */

#undef INIT_SECTION_ASM_OP
#undef FINI_SECTION_ASM_OP
#undef READONLY_DATA_SECTION_ASM_OP
#undef TYPE_OPERAND_FMT
#undef PUSHSECTION_FORMAT
#undef STRING_ASM_OP
#undef COMMON_ASM_OP
#undef SKIP_ASM_OP
#undef SET_ASM_OP	/* Has no equivalent.  See ASM_OUTPUT_DEF below.  */

/* The native assembler can't compute differences between symbols in different
   sections when generating pic code, so we must put jump tables in the
   text section.  */
/* But we now defer the tables to the end of the function, so we make
   this 0 to not confuse the branch shortening code.  */
#define JUMP_TABLES_IN_TEXT_SECTION 0

/* Pass -K to the assembler when PIC.  */
#undef ASM_SPEC
#define ASM_SPEC \
  "%{v:-V} %{Qy:} %{!Qn:-Qy} %{n} %{T} %{Ym,*} %{Yd,*} %{Wa,*:%*} \
   %{fpic|fPIC|fpie|fPIE:-K PIC} %(asm_cpu)"

/* Define the names of various pseudo-op used by the SPARC/svr4 assembler.
   Note that many of these are different from the typical pseudo-ops used
   by most svr4 assemblers.  That is probably due to a (misguided?) attempt
   to keep the SPARC/svr4 assembler somewhat compatible with the SPARC/SunOS
   assembler.  */

#define STRING_ASM_OP		"\t.asciz\t"
#define COMMON_ASM_OP		"\t.common\t"
#define SKIP_ASM_OP		"\t.skip\t"
#define PUSHSECTION_ASM_OP	"\t.pushsection\t"
#define POPSECTION_ASM_OP	"\t.popsection"

/* This is the format used to print the second operand of a .type pseudo-op
   for the SPARC/svr4 assembler.  */

#define TYPE_OPERAND_FMT      "#%s"

/* This is the format used to print a .pushsection pseudo-op (and its operand)
   for the SPARC/svr4 assembler.  */

#define PUSHSECTION_FORMAT	"%s\"%s\"\n"

#undef ASM_OUTPUT_CASE_LABEL
#define ASM_OUTPUT_CASE_LABEL(FILE, PREFIX, NUM, JUMPTABLE)		\
do { ASM_OUTPUT_ALIGN ((FILE), Pmode == SImode ? 2 : 3);		\
     (*targetm.asm_out.internal_label) ((FILE), PREFIX, NUM);		\
   } while (0)

/* This is how to equate one symbol to another symbol.  The syntax used is
   `SYM1=SYM2'.  Note that this is different from the way equates are done
   with most svr4 assemblers, where the syntax is `.set SYM1,SYM2'.  */

#define ASM_OUTPUT_DEF(FILE,LABEL1,LABEL2)				\
 do {	fprintf ((FILE), "\t");						\
	assemble_name (FILE, LABEL1);					\
	fprintf (FILE, " = ");						\
	assemble_name (FILE, LABEL2);					\
	fprintf (FILE, "\n");						\
  } while (0)

/* Define how the SPARC registers should be numbered for Dwarf output.
   The numbering provided here should be compatible with the native
   svr4 SDB debugger in the SPARC/svr4 reference port.  The numbering
   is as follows:

   Assembly name	gcc internal regno	Dwarf regno
   ----------------------------------------------------------
   g0-g7		0-7			0-7
   o0-o7		8-15			8-15
   l0-l7		16-23			16-23
   i0-i7		24-31			24-31
   f0-f31		32-63			40-71
*/

#define DBX_REGISTER_NUMBER(REGNO) ((REGNO) < 32 ? (REGNO) : (REGNO) + 8)

/* A set of symbol definitions for assembly pseudo-ops which will
   get us switched to various sections of interest.  These are used
   in all places where we simply want to switch to a section, and
   *not* to push the previous section name onto the assembler's
   section names stack (as we do often in dwarfout.c).  */

#define TEXT_SECTION_ASM_OP	"\t.section\t\".text\""
#define DATA_SECTION_ASM_OP	"\t.section\t\".data\""
#define BSS_SECTION_ASM_OP	"\t.section\t\".bss\""
#define READONLY_DATA_SECTION_ASM_OP "\t.section\t\".rodata\""
#define INIT_SECTION_ASM_OP	"\t.section\t\".init\""
#define FINI_SECTION_ASM_OP	"\t.section\t\".fini\""

/* Define the pseudo-ops used to switch to the .ctors and .dtors sections.
 
   Note that we want to give these sections the SHF_WRITE attribute
   because these sections will actually contain data (i.e. tables of
   addresses of functions in the current root executable or shared library
   file) and, in the case of a shared library, the relocatable addresses
   will have to be properly resolved/relocated (and then written into) by
   the dynamic linker when it actually attaches the given shared library
   to the executing process.  (Note that on SVR4, you may wish to use the
   `-z text' option to the ELF linker, when building a shared library, as
   an additional check that you are doing everything right.  But if you do
   use the `-z text' option when building a shared library, you will get
   errors unless the .ctors and .dtors sections are marked as writable
   via the SHF_WRITE attribute.)  */
 
#undef CTORS_SECTION_ASM_OP
#define CTORS_SECTION_ASM_OP    "\t.section\t\".ctors\",#alloc,#write"
#undef DTORS_SECTION_ASM_OP
#define DTORS_SECTION_ASM_OP    "\t.section\t\".dtors\",#alloc,#write"

/* Switch into a generic section.  */
#undef TARGET_ASM_NAMED_SECTION
#define TARGET_ASM_NAMED_SECTION  sparc_elf_asm_named_section

#undef ASM_OUTPUT_ALIGNED_BSS
#define ASM_OUTPUT_ALIGNED_BSS(FILE, DECL, NAME, SIZE, ALIGN) \
  asm_output_aligned_bss (FILE, DECL, NAME, SIZE, ALIGN)

/* Override the name of the mcount profiling function.  */

#undef MCOUNT_FUNCTION
#define MCOUNT_FUNCTION "*_mcount"
