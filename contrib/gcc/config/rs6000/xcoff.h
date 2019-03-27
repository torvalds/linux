/* Definitions of target machine for GNU compiler,
   for some generic XCOFF file format
   Copyright (C) 2001, 2002, 2003, 2004, 2007 Free Software Foundation, Inc.

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
   along with GCC; see the file COPYING.  If not, write to the
   Free Software Foundation, 51 Franklin Street, Fifth Floor, Boston,
   MA 02110-1301, USA.  */

#define TARGET_OBJECT_FORMAT OBJECT_XCOFF

/* The RS/6000 uses the XCOFF format.  */
#define XCOFF_DEBUGGING_INFO 1

/* Define if the object format being used is COFF or a superset.  */
#define OBJECT_FORMAT_COFF

/* Define the magic numbers that we recognize as COFF.
 
    AIX 4.3 adds U803XTOCMAGIC (0757) for 64-bit objects and AIX V5 adds
    U64_TOCMAGIC (0767), but collect2.c does not include files in the
    correct order to conditionally define the symbolic name in this macro.
 
    The AIX linker accepts import/export files as object files,
    so accept "#!" (0x2321) magic number.  */
#define MY_ISCOFF(magic) \
  ((magic) == U802WRMAGIC || (magic) == U802ROMAGIC \
   || (magic) == U802TOCMAGIC || (magic) == 0757 || (magic) == 0767 \
   || (magic) == 0x2321)

/* We don't have GAS for the RS/6000 yet, so don't write out special
    .stabs in cc1plus.  */

#define FASCIST_ASSEMBLER

/* We define this to prevent the name mangler from putting dollar signs into
   function names.  */

#define NO_DOLLAR_IN_LABEL

/* We define this to 0 so that gcc will never accept a dollar sign in a
   variable name.  This is needed because the AIX assembler will not accept
   dollar signs.  */

#define DOLLARS_IN_IDENTIFIERS 0

/* AIX .align pseudo-op accept value from 0 to 12, corresponding to
   log base 2 of the alignment in bytes; 12 = 4096 bytes = 32768 bits.  */

#define MAX_OFILE_ALIGNMENT 32768

/* Return nonzero if this entry is to be written into the constant
   pool in a special way.  We do so if this is a SYMBOL_REF, LABEL_REF
   or a CONST containing one of them.  If -mfp-in-toc (the default),
   we also do this for floating-point constants.  We actually can only
   do this if the FP formats of the target and host machines are the
   same, but we can't check that since not every file that uses
   GO_IF_LEGITIMATE_ADDRESS_P includes real.h.  We also do this when
   we can write the entry into the TOC and the entry is not larger
   than a TOC entry.  */

#define ASM_OUTPUT_SPECIAL_POOL_ENTRY_P(X, MODE)			\
  (TARGET_TOC								\
   && (GET_CODE (X) == SYMBOL_REF					\
       || (GET_CODE (X) == CONST && GET_CODE (XEXP (X, 0)) == PLUS	\
	   && GET_CODE (XEXP (XEXP (X, 0), 0)) == SYMBOL_REF)		\
       || GET_CODE (X) == LABEL_REF					\
       || (GET_CODE (X) == CONST_INT 					\
	   && GET_MODE_BITSIZE (MODE) <= GET_MODE_BITSIZE (Pmode))	\
       || (GET_CODE (X) == CONST_DOUBLE					\
	   && (TARGET_POWERPC64						\
	       || TARGET_MINIMAL_TOC					\
	       || (SCALAR_FLOAT_MODE_P (GET_MODE (X))			\
		   && ! TARGET_NO_FP_IN_TOC)))))

#define TARGET_ASM_OUTPUT_ANCHOR  rs6000_xcoff_asm_output_anchor
#define TARGET_ASM_GLOBALIZE_LABEL  rs6000_xcoff_asm_globalize_label
#define TARGET_ASM_INIT_SECTIONS  rs6000_xcoff_asm_init_sections
#define TARGET_ASM_RELOC_RW_MASK  rs6000_xcoff_reloc_rw_mask
#define TARGET_ASM_NAMED_SECTION  rs6000_xcoff_asm_named_section
#define TARGET_ASM_SELECT_SECTION  rs6000_xcoff_select_section
#define TARGET_ASM_SELECT_RTX_SECTION  rs6000_xcoff_select_rtx_section
#define TARGET_ASM_UNIQUE_SECTION  rs6000_xcoff_unique_section
#define TARGET_ASM_FUNCTION_RODATA_SECTION default_no_function_rodata_section
#define TARGET_STRIP_NAME_ENCODING  rs6000_xcoff_strip_name_encoding
#define TARGET_SECTION_TYPE_FLAGS  rs6000_xcoff_section_type_flags

/* FP save and restore routines.  */
#define	SAVE_FP_PREFIX "._savef"
#define SAVE_FP_SUFFIX ""
#define	RESTORE_FP_PREFIX "._restf"
#define RESTORE_FP_SUFFIX ""

/* Function name to call to do profiling.  */
#undef  RS6000_MCOUNT
#define RS6000_MCOUNT ".__mcount"

/* This outputs NAME to FILE up to the first null or '['.  */

#define RS6000_OUTPUT_BASENAME(FILE, NAME) \
  assemble_name ((FILE), (*targetm.strip_name_encoding) (NAME))

/* This is how to output the definition of a user-level label named NAME,
   such as the label on a static function or variable NAME.  */

#define ASM_OUTPUT_LABEL(FILE,NAME)	\
  do { RS6000_OUTPUT_BASENAME (FILE, NAME); fputs (":\n", FILE); } while (0)

/* This is how to output a command to make the user-level label named NAME
   defined for reference from other files.  */

/* Globalizing directive for a label.  */
#define GLOBAL_ASM_OP "\t.globl "

#undef TARGET_ASM_FILE_START
#define TARGET_ASM_FILE_START rs6000_xcoff_file_start
#define TARGET_ASM_FILE_END rs6000_xcoff_file_end
#undef TARGET_ASM_FILE_START_FILE_DIRECTIVE
#define TARGET_ASM_FILE_START_FILE_DIRECTIVE false

/* This macro produces the initial definition of a function name.
   On the RS/6000, we need to place an extra '.' in the function name and
   output the function descriptor.

   The csect for the function will have already been created when
   text_section was selected.  We do have to go back to that csect, however.

   The third and fourth parameters to the .function pseudo-op (16 and 044)
   are placeholders which no longer have any use.  */

#define ASM_DECLARE_FUNCTION_NAME(FILE,NAME,DECL)		\
{ if (TREE_PUBLIC (DECL))					\
    {								\
      if (!RS6000_WEAK || !DECL_WEAK (decl))			\
	{							\
	  fputs ("\t.globl .", FILE);				\
	  RS6000_OUTPUT_BASENAME (FILE, NAME);			\
	  putc ('\n', FILE);					\
	}							\
    }								\
  else								\
    {								\
      fputs ("\t.lglobl .", FILE);				\
      RS6000_OUTPUT_BASENAME (FILE, NAME);			\
      putc ('\n', FILE);					\
    }								\
  fputs ("\t.csect ", FILE);					\
  RS6000_OUTPUT_BASENAME (FILE, NAME);				\
  fputs (TARGET_32BIT ? "[DS]\n" : "[DS],3\n", FILE);		\
  RS6000_OUTPUT_BASENAME (FILE, NAME);				\
  fputs (":\n", FILE);						\
  fputs (TARGET_32BIT ? "\t.long ." : "\t.llong .", FILE);	\
  RS6000_OUTPUT_BASENAME (FILE, NAME);				\
  fputs (", TOC[tc0], 0\n", FILE);				\
  in_section = NULL;						\
  switch_to_section (function_section (DECL));			\
  putc ('.', FILE);						\
  RS6000_OUTPUT_BASENAME (FILE, NAME);				\
  fputs (":\n", FILE);						\
  if (write_symbols != NO_DEBUG)				\
    xcoffout_declare_function (FILE, DECL, NAME);		\
}

/* Output a reference to SYM on FILE.  */

#define ASM_OUTPUT_SYMBOL_REF(FILE, SYM) \
  rs6000_output_symbol_ref (FILE, SYM)

/* This says how to output an external.  */

#undef  ASM_OUTPUT_EXTERNAL
#define ASM_OUTPUT_EXTERNAL(FILE, DECL, NAME)				\
{ rtx _symref = XEXP (DECL_RTL (DECL), 0);				\
  if ((TREE_CODE (DECL) == VAR_DECL					\
       || TREE_CODE (DECL) == FUNCTION_DECL)				\
      && (NAME)[strlen (NAME) - 1] != ']')				\
    {									\
      XSTR (_symref, 0) = concat (XSTR (_symref, 0),			\
				  (TREE_CODE (DECL) == FUNCTION_DECL	\
				   ? "[DS]" : "[RW]"),			\
				  NULL);				\
    }									\
}

/* This is how to output an internal label prefix.  rs6000.c uses this
   when generating traceback tables.  */

#define ASM_OUTPUT_INTERNAL_LABEL_PREFIX(FILE,PREFIX)   \
  fprintf (FILE, "%s..", PREFIX)

/* This is how to output a label for a jump table.  Arguments are the same as
   for (*targetm.asm_out.internal_label), except the insn for the jump table is
   passed.  */

#define ASM_OUTPUT_CASE_LABEL(FILE,PREFIX,NUM,TABLEINSN)	\
{ ASM_OUTPUT_ALIGN (FILE, 2); (*targetm.asm_out.internal_label) (FILE, PREFIX, NUM); }

/* This is how to store into the string LABEL
   the symbol_ref name of an internal numbered label where
   PREFIX is the class of label and NUM is the number within the class.
   This is suitable for output with `assemble_name'.  */

#define ASM_GENERATE_INTERNAL_LABEL(LABEL,PREFIX,NUM)	\
  sprintf (LABEL, "*%s..%u", (PREFIX), (unsigned) (NUM))

/* This is how to output an assembler line to define N characters starting
   at P to FILE.  */

#define ASM_OUTPUT_ASCII(FILE, P, N)  output_ascii ((FILE), (P), (N))

/* This is how to advance the location counter by SIZE bytes.  */

#define SKIP_ASM_OP "\t.space "

#define ASM_OUTPUT_SKIP(FILE,SIZE)  \
  fprintf (FILE, "%s"HOST_WIDE_INT_PRINT_UNSIGNED"\n", SKIP_ASM_OP, (SIZE))

/* This says how to output an assembler line
   to define a global common symbol.  */

#define COMMON_ASM_OP "\t.comm "

#define ASM_OUTPUT_ALIGNED_COMMON(FILE, NAME, SIZE, ALIGN)	\
  do { fputs (COMMON_ASM_OP, (FILE));			\
       RS6000_OUTPUT_BASENAME ((FILE), (NAME));		\
       if ((ALIGN) > 32)				\
	 fprintf ((FILE), ","HOST_WIDE_INT_PRINT_UNSIGNED",%u\n", (SIZE), \
		  exact_log2 ((ALIGN) / BITS_PER_UNIT)); \
       else if ((SIZE) > 4)				\
         fprintf ((FILE), ","HOST_WIDE_INT_PRINT_UNSIGNED",3\n", (SIZE)); \
       else						\
	 fprintf ((FILE), ","HOST_WIDE_INT_PRINT_UNSIGNED"\n", (SIZE)); \
  } while (0)

/* This says how to output an assembler line
   to define a local common symbol.
   Alignment cannot be specified, but we can try to maintain
   alignment after preceding TOC section if it was aligned
   for 64-bit mode.  */

#define LOCAL_COMMON_ASM_OP "\t.lcomm "

#define ASM_OUTPUT_LOCAL(FILE, NAME, SIZE, ROUNDED)	\
  do { fputs (LOCAL_COMMON_ASM_OP, (FILE));		\
       RS6000_OUTPUT_BASENAME ((FILE), (NAME));		\
       fprintf ((FILE), ","HOST_WIDE_INT_PRINT_UNSIGNED",%s\n", \
		(TARGET_32BIT ? (SIZE) : (ROUNDED)),	\
		xcoff_bss_section_name);		\
     } while (0)

/* This is how we tell the assembler that two symbols have the same value.  */
#define SET_ASM_OP "\t.set "

/* This is how we tell the assembler to equate two values.  */
#define ASM_OUTPUT_DEF(FILE,LABEL1,LABEL2)				\
 do {	fprintf ((FILE), "%s", SET_ASM_OP);				\
	RS6000_OUTPUT_BASENAME (FILE, LABEL1);				\
	fprintf (FILE, ",");						\
	RS6000_OUTPUT_BASENAME (FILE, LABEL2);				\
	fprintf (FILE, "\n");						\
  } while (0)

/* Used by rs6000_assemble_integer, among others.  */
#define DOUBLE_INT_ASM_OP "\t.llong\t"

/* Output before instructions.  */
#define TEXT_SECTION_ASM_OP "\t.csect .text[PR]"

/* Output before writable data.
   Align entire section to BIGGEST_ALIGNMENT.  */
#define DATA_SECTION_ASM_OP "\t.csect .data[RW],3"

/* Define to prevent DWARF2 unwind info in the data section rather
   than in the .eh_frame section.  We do this because the AIX linker
   would otherwise garbage collect these sections.  */
#define EH_FRAME_IN_DATA_SECTION 1
