/* Operating system specific defines to be used when targeting GCC for
   hosting on Windows32, using a Unix style C library and tools.
   Copyright (C) 1995, 1996, 1997, 1998, 1999, 2000, 2001, 2002, 2003,
   2004, 2005
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

#define DBX_DEBUGGING_INFO 1
#define SDB_DEBUGGING_INFO 1
#undef PREFERRED_DEBUGGING_TYPE
#define PREFERRED_DEBUGGING_TYPE DBX_DEBUG

#ifdef HAVE_GAS_PE_SECREL32_RELOC
#define DWARF2_DEBUGGING_INFO 1

#undef DBX_REGISTER_NUMBER
#define DBX_REGISTER_NUMBER(n) (write_symbols == DWARF2_DEBUG   \
                                ? svr4_dbx_register_map[n]      \
                                : dbx_register_map[n])

/* Use section relative relocations for debugging offsets.  Unlike
   other targets that fake this by putting the section VMA at 0, PE
   won't allow it.  */
#define ASM_OUTPUT_DWARF_OFFSET(FILE, SIZE, LABEL, SECTION)	\
  do {								\
    if (SIZE != 4)						\
      abort ();							\
								\
    fputs ("\t.secrel32\t", FILE);				\
    assemble_name (FILE, LABEL);				\
  } while (0)
#endif

#define TARGET_EXECUTABLE_SUFFIX ".exe"

#include <stdio.h>

#define MAYBE_UWIN_CPP_BUILTINS() /* Nothing.  */

#define TARGET_OS_CPP_BUILTINS()					\
  do									\
    {									\
	builtin_define ("_X86_=1");					\
	builtin_assert ("system=winnt");				\
	builtin_define ("__stdcall=__attribute__((__stdcall__))");	\
	builtin_define ("__fastcall=__attribute__((__fastcall__))");	\
	builtin_define ("__cdecl=__attribute__((__cdecl__))");		\
	if (!flag_iso)							\
	  {								\
	    builtin_define ("_stdcall=__attribute__((__stdcall__))");	\
	    builtin_define ("_fastcall=__attribute__((__fastcall__))");	\
	    builtin_define ("_cdecl=__attribute__((__cdecl__))");	\
	  }								\
	/* Even though linkonce works with static libs, this is needed 	\
	    to compare typeinfo symbols across dll boundaries.  */	\
	builtin_define ("__GXX_MERGED_TYPEINFO_NAMES=0");		\
	MAYBE_UWIN_CPP_BUILTINS ();					\
	EXTRA_OS_CPP_BUILTINS ();					\
  }									\
  while (0)

/* Get tree.c to declare a target-specific specialization of
   merge_decl_attributes.  */
#define TARGET_DLLIMPORT_DECL_ATTRIBUTES 1

/* This macro defines names of additional specifications to put in the specs
   that can be used in various specifications like CC1_SPEC.  Its definition
   is an initializer with a subgrouping for each command option.

   Each subgrouping contains a string constant, that defines the
   specification name, and a string constant that used by the GCC driver
   program.

   Do not define this macro if it does not need to do anything.  */

#undef  SUBTARGET_EXTRA_SPECS
#define SUBTARGET_EXTRA_SPECS						\
  { "mingw_include_path", DEFAULT_TARGET_MACHINE }

#undef MATH_LIBRARY
#define MATH_LIBRARY ""

#define SIZE_TYPE "unsigned int"
#define PTRDIFF_TYPE "int"
#define WCHAR_TYPE_SIZE 16
#define WCHAR_TYPE "short unsigned int"


/* Enable parsing of #pragma pack(push,<n>) and #pragma pack(pop).  */
#define HANDLE_PRAGMA_PACK_PUSH_POP 1

union tree_node;
#define TREE union tree_node *

#define drectve_section() \
  (fprintf (asm_out_file, "\t.section .drectve\n"), \
   in_section = NULL)

/* Older versions of gas don't handle 'r' as data.
   Explicitly set data flag with 'd'.  */  
#define READONLY_DATA_SECTION_ASM_OP "\t.section .rdata,\"dr\""

/* Don't allow flag_pic to propagate since gas may produce invalid code
   otherwise.  */

#undef  SUBTARGET_OVERRIDE_OPTIONS
#define SUBTARGET_OVERRIDE_OPTIONS					\
do {									\
  if (flag_pic)								\
    {									\
      warning (0, "-f%s ignored for target (all code is position independent)",\
	       (flag_pic > 1) ? "PIC" : "pic");				\
      flag_pic = 0;							\
    }									\
} while (0)								\

/* Define this macro if references to a symbol must be treated
   differently depending on something about the variable or
   function named by the symbol (such as what section it is in).

   On i386 running Windows NT, modify the assembler name with a suffix
   consisting of an atsign (@) followed by string of digits that represents
   the number of bytes of arguments passed to the function, if it has the
   attribute STDCALL.

   In addition, we must mark dll symbols specially. Definitions of
   dllexport'd objects install some info in the .drectve section.
   References to dllimport'd objects are fetched indirectly via
   _imp__.  If both are declared, dllexport overrides.  This is also
   needed to implement one-only vtables: they go into their own
   section and we need to set DECL_SECTION_NAME so we do that here.
   Note that we can be called twice on the same decl.  */

#undef SUBTARGET_ENCODE_SECTION_INFO
#define SUBTARGET_ENCODE_SECTION_INFO  i386_pe_encode_section_info
#undef  TARGET_STRIP_NAME_ENCODING
#define TARGET_STRIP_NAME_ENCODING  i386_pe_strip_name_encoding_full

/* Output a reference to a label.  */
#undef ASM_OUTPUT_LABELREF
#define ASM_OUTPUT_LABELREF  i386_pe_output_labelref

#undef  COMMON_ASM_OP
#define COMMON_ASM_OP	"\t.comm\t"

/* Output a common block.  */
#undef ASM_OUTPUT_COMMON
#define ASM_OUTPUT_COMMON(STREAM, NAME, SIZE, ROUNDED)	\
do {							\
  if (i386_pe_dllexport_name_p (NAME))			\
    i386_pe_record_exported_symbol (NAME, 1);		\
  if (! i386_pe_dllimport_name_p (NAME))		\
    {							\
      fprintf ((STREAM), "\t.comm\t");			\
      assemble_name ((STREAM), (NAME));			\
      fprintf ((STREAM), ", %d\t%s %d\n",		\
	       (int)(ROUNDED), ASM_COMMENT_START, (int)(SIZE));	\
    }							\
} while (0)

/* Output the label for an initialized variable.  */
#undef ASM_DECLARE_OBJECT_NAME
#define ASM_DECLARE_OBJECT_NAME(STREAM, NAME, DECL)	\
do {							\
  if (i386_pe_dllexport_name_p (NAME))			\
    i386_pe_record_exported_symbol (NAME, 1);		\
  ASM_OUTPUT_LABEL ((STREAM), (NAME));			\
} while (0)


/* Emit code to check the stack when allocating more that 4000
   bytes in one go.  */

#define CHECK_STACK_LIMIT 4000

/* By default, target has a 80387, uses IEEE compatible arithmetic,
   returns float values in the 387 and needs stack probes.
   We also align doubles to 64-bits for MSVC default compatibility.  */

#undef TARGET_SUBTARGET_DEFAULT
#define TARGET_SUBTARGET_DEFAULT \
   (MASK_80387 | MASK_IEEE_FP | MASK_FLOAT_RETURNS | MASK_STACK_PROBE \
    | MASK_ALIGN_DOUBLE)

/* This is how to output an assembler line
   that says to advance the location counter
   to a multiple of 2**LOG bytes.  */

#undef ASM_OUTPUT_ALIGN
#define ASM_OUTPUT_ALIGN(FILE,LOG)	\
    if ((LOG)!=0) fprintf ((FILE), "\t.align %d\n", 1<<(LOG))

/* Windows uses explicit import from shared libraries.  */
#define MULTIPLE_SYMBOL_SPACES 1

extern void i386_pe_unique_section (TREE, int);
#define TARGET_ASM_UNIQUE_SECTION i386_pe_unique_section
#define TARGET_ASM_FUNCTION_RODATA_SECTION default_no_function_rodata_section

#define SUPPORTS_ONE_ONLY 1

/* Switch into a generic section.  */
#define TARGET_ASM_NAMED_SECTION  i386_pe_asm_named_section

/* Select attributes for named sections.  */
#define TARGET_SECTION_TYPE_FLAGS  i386_pe_section_type_flags

/* Write the extra assembler code needed to declare a function
   properly.  If we are generating SDB debugging information, this
   will happen automatically, so we only need to handle other cases.  */
#undef ASM_DECLARE_FUNCTION_NAME
#define ASM_DECLARE_FUNCTION_NAME(FILE, NAME, DECL)			\
  do									\
    {									\
      if (i386_pe_dllexport_name_p (NAME))				\
	i386_pe_record_exported_symbol (NAME, 0);			\
      if (write_symbols != SDB_DEBUG)					\
	i386_pe_declare_function_type (FILE, NAME, TREE_PUBLIC (DECL));	\
      ASM_OUTPUT_LABEL (FILE, NAME);					\
    }									\
  while (0)

/* Add an external function to the list of functions to be declared at
   the end of the file.  */
#define ASM_OUTPUT_EXTERNAL(FILE, DECL, NAME)				\
  do									\
    {									\
      if (TREE_CODE (DECL) == FUNCTION_DECL)				\
	i386_pe_record_external_function ((DECL), (NAME));		\
    }									\
  while (0)

/* Declare the type properly for any external libcall.  */
#define ASM_OUTPUT_EXTERNAL_LIBCALL(FILE, FUN) \
  i386_pe_declare_function_type (FILE, XSTR (FUN, 0), 1)

/* This says out to put a global symbol in the BSS section.  */
#undef ASM_OUTPUT_ALIGNED_BSS
#define ASM_OUTPUT_ALIGNED_BSS(FILE, DECL, NAME, SIZE, ALIGN) \
  asm_output_aligned_bss ((FILE), (DECL), (NAME), (SIZE), (ALIGN))

/* Output function declarations at the end of the file.  */
#undef TARGET_ASM_FILE_END
#define TARGET_ASM_FILE_END i386_pe_file_end

#undef ASM_COMMENT_START
#define ASM_COMMENT_START " #"

/* DWARF2 Unwinding doesn't work with exception handling yet.  To make
   it work, we need to build a libgcc_s.dll, and dcrt0.o should be
   changed to call __register_frame_info/__deregister_frame_info.  */
#define DWARF2_UNWIND_INFO 0

/* Don't assume anything about the header files.  */
#define NO_IMPLICIT_EXTERN_C

#undef PROFILE_HOOK
#define PROFILE_HOOK(LABEL)						\
  if (MAIN_NAME_P (DECL_NAME (current_function_decl)))			\
    {									\
      emit_call_insn (gen_rtx_CALL (VOIDmode,				\
	gen_rtx_MEM (FUNCTION_MODE,					\
		     gen_rtx_SYMBOL_REF (Pmode, "_monstartup")),	\
	const0_rtx));							\
    }

/* Java Native Interface (JNI) methods on Win32 are invoked using the
   stdcall calling convention.  */
#undef MODIFY_JNI_METHOD_CALL
#define MODIFY_JNI_METHOD_CALL(MDECL)					      \
  build_type_attribute_variant ((MDECL),				      \
			       build_tree_list (get_identifier ("stdcall"),   \
						NULL))

/* External function declarations.  */

extern void i386_pe_record_external_function (tree, const char *);
extern void i386_pe_declare_function_type (FILE *, const char *, int);
extern void i386_pe_record_exported_symbol (const char *, int);
extern void i386_pe_file_end (void);
extern int i386_pe_dllexport_name_p (const char *);
extern int i386_pe_dllimport_name_p (const char *);

/* For Win32 ABI compatibility */
#undef DEFAULT_PCC_STRUCT_RETURN
#define DEFAULT_PCC_STRUCT_RETURN 0

/* MSVC returns aggregate types of up to 8 bytes via registers.
   See i386.c:ix86_return_in_memory.  */
#undef MS_AGGREGATE_RETURN
#define MS_AGGREGATE_RETURN 1

/* No data type wants to be aligned rounder than this.  */
#undef	BIGGEST_ALIGNMENT
#define BIGGEST_ALIGNMENT 128

/* Biggest alignment supported by the object file format of this
   machine.  Use this macro to limit the alignment which can be
   specified using the `__attribute__ ((aligned (N)))' construct.  If
   not defined, the default value is `BIGGEST_ALIGNMENT'.  */
#undef MAX_OFILE_ALIGNMENT
/* IMAGE_SCN_ALIGN_8192BYTES is the largest section alignment flag
   specified in the PECOFF60 spec.  Native MS compiler also limits
   user-specified alignment to 8192 bytes.  */
#define MAX_OFILE_ALIGNMENT (8192 * 8)

/* Native complier aligns internal doubles in structures on dword boundaries.  */
#undef	BIGGEST_FIELD_ALIGNMENT
#define BIGGEST_FIELD_ALIGNMENT 64

/* A bit-field declared as `int' forces `int' alignment for the struct.  */
#undef PCC_BITFIELD_TYPE_MATTERS
#define PCC_BITFIELD_TYPE_MATTERS 1
#define GROUP_BITFIELDS_BY_ALIGN TYPE_NATIVE(rec)

/* Enable alias attribute support.  */
#ifndef SET_ASM_OP
#define SET_ASM_OP "\t.set\t"
#endif
/* This implements the `alias' attribute, keeping any stdcall or
   fastcall decoration.  */
#undef	ASM_OUTPUT_DEF_FROM_DECLS
#define	ASM_OUTPUT_DEF_FROM_DECLS(STREAM, DECL, TARGET) 		\
  do									\
    {									\
      const char *alias;						\
      rtx rtlname = XEXP (DECL_RTL (DECL), 0);				\
      if (GET_CODE (rtlname) == SYMBOL_REF)				\
	alias = XSTR (rtlname, 0);					\
      else								\
	abort ();							\
      if (TREE_CODE (DECL) == FUNCTION_DECL)				\
	i386_pe_declare_function_type (STREAM, alias,			\
				       TREE_PUBLIC (DECL));		\
      ASM_OUTPUT_DEF (STREAM, alias, IDENTIFIER_POINTER (TARGET));	\
    } while (0)

/* GNU as supports weak symbols on PECOFF. */
#ifdef HAVE_GAS_WEAK
#define ASM_WEAKEN_LABEL(FILE, NAME)  \
  do                                  \
    {                                 \
      fputs ("\t.weak\t", (FILE));    \
      assemble_name ((FILE), (NAME)); \
      fputc ('\n', (FILE));           \
    }                                 \
  while (0)
#endif /* HAVE_GAS_WEAK */

/* FIXME: SUPPORTS_WEAK && TARGET_HAVE_NAMED_SECTIONS is true,
   but for .jcr section to work we also need crtbegin and crtend
   objects.  */
#define TARGET_USE_JCR_SECTION 0

/* Decide whether it is safe to use a local alias for a virtual function
   when constructing thunks.  */
#undef TARGET_USE_LOCAL_THUNK_ALIAS_P
#define TARGET_USE_LOCAL_THUNK_ALIAS_P(DECL) (!DECL_ONE_ONLY (DECL))

#define SUBTARGET_ATTRIBUTE_TABLE \
  /* { name, min_len, max_len, decl_req, type_req, fn_type_req, handler } */ \
  { "selectany", 0, 0, true, false, false, ix86_handle_selectany_attribute }

/*  mcount() does not need a counter variable.  */
#undef NO_PROFILE_COUNTERS
#define NO_PROFILE_COUNTERS 1

#define TARGET_VALID_DLLIMPORT_ATTRIBUTE_P i386_pe_valid_dllimport_attribute_p
#define TARGET_CXX_ADJUST_CLASS_AT_DEFINITION i386_pe_adjust_class_at_definition

#undef TREE

#ifndef BUFSIZ
# undef FILE
#endif
