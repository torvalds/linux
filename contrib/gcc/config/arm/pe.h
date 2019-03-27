/* Definitions of target machine for GNU compiler, for ARM with PE obj format.
   Copyright (C) 1995, 1996, 1999, 2000, 2002, 2003, 2004, 2005
   Free Software Foundation, Inc.
   Contributed by Doug Evans (dje@cygnus.com).
   
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

/* Enable PE specific code.  */
#define ARM_PE		1

#define ARM_PE_FLAG_CHAR '@'

/* Ensure that @x. will be stripped from the function name.  */
#undef SUBTARGET_NAME_ENCODING_LENGTHS
#define SUBTARGET_NAME_ENCODING_LENGTHS  \
  case ARM_PE_FLAG_CHAR: return 3;

#undef  USER_LABEL_PREFIX
#define USER_LABEL_PREFIX "_"


/* Run-time Target Specification.  */
#undef  TARGET_VERSION
#define TARGET_VERSION fputs (" (ARM/pe)", stderr)

/* Get tree.c to declare a target-specific specialization of
   merge_decl_attributes.  */
#define TARGET_DLLIMPORT_DECL_ATTRIBUTES 1

#undef  SUBTARGET_CPP_SPEC
#define SUBTARGET_CPP_SPEC "-D__pe__"

#undef  TARGET_DEFAULT
#define TARGET_DEFAULT	(MASK_NOP_FUN_DLLIMPORT)

#undef  MULTILIB_DEFAULTS
#define MULTILIB_DEFAULTS \
  { "marm", "mlittle-endian", "msoft-float", "mno-thumb-interwork" }  

#undef  WCHAR_TYPE
#define WCHAR_TYPE 	"short unsigned int"
#undef  WCHAR_TYPE_SIZE
#define WCHAR_TYPE_SIZE 16

/* r11 is fixed.  */
#undef  SUBTARGET_CONDITIONAL_REGISTER_USAGE
#define SUBTARGET_CONDITIONAL_REGISTER_USAGE \
  fixed_regs [11] = 1; \
  call_used_regs [11] = 1;


/* PE/COFF uses explicit import from shared libraries.  */
#define MULTIPLE_SYMBOL_SPACES 1

#define TARGET_ASM_UNIQUE_SECTION arm_pe_unique_section
#define TARGET_ASM_FUNCTION_RODATA_SECTION default_no_function_rodata_section

#define SUPPORTS_ONE_ONLY 1

/* Switch into a generic section.  */
#undef  TARGET_ASM_NAMED_SECTION
#define TARGET_ASM_NAMED_SECTION  default_pe_asm_named_section

#define TARGET_ASM_FILE_START_FILE_DIRECTIVE true

/* Output a reference to a label.  */
#undef  ASM_OUTPUT_LABELREF
#define ASM_OUTPUT_LABELREF(STREAM, NAME)  \
  asm_fprintf (STREAM, "%U%s", arm_strip_name_encoding (NAME))

/* Output a function definition label.  */
#undef  ASM_DECLARE_FUNCTION_NAME
#define ASM_DECLARE_FUNCTION_NAME(STREAM, NAME, DECL)   	\
  do								\
    {								\
      if (arm_dllexport_name_p (NAME))				\
	{							\
	  drectve_section ();					\
	  fprintf (STREAM, "\t.ascii \" -export:%s\"\n",	\
		   arm_strip_name_encoding (NAME));		\
	  switch_to_section (function_section (DECL));		\
	}							\
      ARM_DECLARE_FUNCTION_NAME (STREAM, NAME, DECL);		\
      if (TARGET_THUMB)						\
	fprintf (STREAM, "\t.code 16\n");			\
      ASM_OUTPUT_LABEL (STREAM, NAME);				\
    }								\
  while (0)

/* Output a common block.  */
#undef  ASM_OUTPUT_COMMON
#define ASM_OUTPUT_COMMON(STREAM, NAME, SIZE, ROUNDED)	\
  do							\
    {							\
      if (arm_dllexport_name_p (NAME))			\
	{						\
	  drectve_section ();				\
	  fprintf ((STREAM), "\t.ascii \" -export:%s\"\n",\
		   arm_strip_name_encoding (NAME));	\
	}						\
      if (! arm_dllimport_name_p (NAME))		\
	{						\
	  fprintf ((STREAM), "\t.comm\t"); 		\
	  assemble_name ((STREAM), (NAME));		\
	  asm_fprintf ((STREAM), ", %d\t%@ %d\n",	\
 		   (int)(ROUNDED), (int)(SIZE));	\
	}						\
    }							\
  while (0)

/* Output the label for an initialized variable.  */
#undef  ASM_DECLARE_OBJECT_NAME
#define ASM_DECLARE_OBJECT_NAME(STREAM, NAME, DECL) 	\
  do							\
    {							\
      if (arm_dllexport_name_p (NAME))			\
	{						\
	  section *save_section = in_section;		\
	  drectve_section ();				\
	  fprintf (STREAM, "\t.ascii \" -export:%s\"\n",\
		   arm_strip_name_encoding (NAME));	\
	  switch_to_section (save_section);		\
	}						\
      ASM_OUTPUT_LABEL ((STREAM), (NAME));		\
    }							\
  while (0)

/* Support the ctors/dtors and other sections.  */

#define DRECTVE_SECTION_ASM_OP	"\t.section .drectve"

#define drectve_section() \
  (fprintf (asm_out_file, "%s\n", DRECTVE_SECTION_ASM_OP), \
   in_section = NULL)
