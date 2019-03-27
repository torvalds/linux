/* Definitions for Intel 386 running FreeBSD with ELF format
   Copyright (C) 1996, 2000, 2002, 2004 Free Software Foundation, Inc.
   Contributed by Eric Youngdale.
   Modified for stabs-in-ELF by H.J. Lu.
   Adapted from GNU/Linux version by John Polstra.
   Continued development by David O'Brien <obrien@freebsd.org>

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

/* $FreeBSD$ */

#undef  CC1_SPEC
#define CC1_SPEC "%(cc1_cpu) %{profile:-p}"

/* Provide a LINK_SPEC appropriate for FreeBSD.  Here we provide support
   for the special GCC options -static and -shared, which allow us to
   link things in one of these three modes by applying the appropriate
   combinations of options at link-time. We like to support here for
   as many of the other GNU linker options as possible. But I don't
   have the time to search for those flags. I am sure how to add
   support for -soname shared_object_name. H.J.

   When the -shared link option is used a final link is not being
   done.  */

#undef	LINK_SPEC
#define LINK_SPEC "\
 %{p:%nconsider using `-pg' instead of `-p' with gprof(1) } \
    %{v:-V} \
    %{assert*} %{R*} %{rpath*} %{defsym*} \
    %{shared:-Bshareable %{h*} %{soname*}} \
    %{!shared: \
      %{!static: \
	%{rdynamic: -export-dynamic} \
	%{!dynamic-linker:-dynamic-linker %(fbsd_dynamic_linker) }} \
      %{static:-Bstatic}} \
    %{!static:--hash-style=both --enable-new-dtags} \
    %{symbolic:-Bsymbolic}"

/* Reset our STARTFILE_SPEC which was properly set in config/freebsd.h
   but trashed by config/<cpu>/<file.h>. */

#undef  STARTFILE_SPEC
#define STARTFILE_SPEC FBSD_STARTFILE_SPEC

/* Provide an ENDFILE_SPEC appropriate for FreeBSD/i386.  */

#undef  ENDFILE_SPEC
#define ENDFILE_SPEC FBSD_ENDFILE_SPEC


/************************[  Target stuff  ]***********************************/

/* Define the actual types of some ANSI-mandated types.
   Needs to agree with <machine/ansi.h>.  GCC defaults come from c-decl.c,
   c-common.c, and config/<arch>/<arch>.h.  */

#undef  SIZE_TYPE
#define SIZE_TYPE	(TARGET_64BIT ? "long unsigned int" : "unsigned int")

#undef  PTRDIFF_TYPE
#define PTRDIFF_TYPE	(TARGET_64BIT ? "long int" : "int")

#undef  WCHAR_TYPE_SIZE
#define WCHAR_TYPE_SIZE	(TARGET_64BIT ? 32 : BITS_PER_WORD)

#undef  SUBTARGET_EXTRA_SPECS	/* i386.h bogusly defines it.  */
#define SUBTARGET_EXTRA_SPECS \
  { "fbsd_dynamic_linker", FBSD_DYNAMIC_LINKER }

#define TARGET_VERSION	fprintf (stderr, " (i386 FreeBSD/ELF)");

#define TARGET_ELF	1

/* Don't default to pcc-struct-return, we want to retain compatibility with
   older gcc versions AND pcc-struct-return is nonreentrant.
   (even though the SVR4 ABI for the i386 says that records and unions are
   returned in memory).  */

#undef  DEFAULT_PCC_STRUCT_RETURN
#define DEFAULT_PCC_STRUCT_RETURN 0

/* FreeBSD sets the rounding precision of the FPU to 53 bits.  Let the
   compiler get the contents of <float.h> and std::numeric_limits correct.  */
#undef  TARGET_96_ROUND_53_LONG_DOUBLE
#define TARGET_96_ROUND_53_LONG_DOUBLE (!TARGET_64BIT)

/* Tell final.c that we don't need a label passed to mcount.  */
#define NO_PROFILE_COUNTERS	1

/* Output assembler code to FILE to begin profiling of the current function.
   LABELNO is an optional label.  */

#undef  MCOUNT_NAME
#define MCOUNT_NAME ".mcount"

/* Output assembler code to FILE to end profiling of the current function.  */

#undef  FUNCTION_PROFILER_EPILOGUE	/* BDE will need to fix this. */


/************************[  Assembler stuff  ]********************************/

/* Override the default comment-starter of "/" from unix.h.  */
#undef  ASM_COMMENT_START
#define ASM_COMMENT_START "#"

/* Override the default comment-starter of "/APP" from unix.h.  */
#undef  ASM_APP_ON
#define ASM_APP_ON	"#APP\n"
#undef  ASM_APP_OFF
#define ASM_APP_OFF	"#NO_APP\n"

/* XXX:DEO do we still need this override to defaults.h ?? */
/* This is how to output a reference to a user-level label named NAME.  */
#undef  ASM_OUTPUT_LABELREF
#define ASM_OUTPUT_LABELREF(FILE, NAME)					\
  do {									\
    const char *xname = (NAME);						\
    /* Hack to avoid writing lots of rtl in				\
       FUNCTION_PROFILER_EPILOGUE ().  */				\
    if (*xname == '.' && strcmp(xname + 1, "mexitcount") == 0)		\
      {									\
	if (flag_pic)							\
	  fprintf ((FILE), "*%s@GOT(%%ebx)", xname);			\
	else								\
	  fprintf ((FILE), "%s", xname);				\
      }									\
    else								\
      {									\
	  if (xname[0] == '%')						\
	    xname += 2;							\
	  if (xname[0] == '*')						\
	    xname += 1;							\
	  else								\
	    fputs (user_label_prefix, FILE);				\
	  fputs (xname, FILE);						\
      }									\
} while (0)

/* This is how to hack on the symbol code of certain relcalcitrant
   symbols to modify their output in output_pic_addr_const ().  */

#undef  ASM_HACK_SYMBOLREF_CODE	/* BDE will need to fix this. */

/* A C statement to output to the stdio stream FILE an assembler
   command to advance the location counter to a multiple of 1<<LOG
   bytes if it is within MAX_SKIP bytes.

   This is used to align code labels according to Intel recommendations.  */

/* XXX configuration of this is broken in the same way as HAVE_GAS_SHF_MERGE,
   but it is easier to fix in an MD way.  */

#ifdef HAVE_GAS_MAX_SKIP_P2ALIGN
#undef  ASM_OUTPUT_MAX_SKIP_ALIGN
#define ASM_OUTPUT_MAX_SKIP_ALIGN(FILE, LOG, MAX_SKIP)			\
  do {									\
    if ((LOG) != 0) {							\
      if ((MAX_SKIP) == 0)						\
	fprintf ((FILE), "\t.p2align %d\n", (LOG));			\
      else								\
	fprintf ((FILE), "\t.p2align %d,,%d\n", (LOG), (MAX_SKIP));	\
    }									\
  } while (0)
#endif

/* If defined, a C expression whose value is a string containing the
   assembler operation to identify the following data as
   uninitialized global data.  If not defined, and neither
   `ASM_OUTPUT_BSS' nor `ASM_OUTPUT_ALIGNED_BSS' are defined,
   uninitialized global data will be output in the data section if
   `-fno-common' is passed, otherwise `ASM_OUTPUT_COMMON' will be
   used.  */
#undef  BSS_SECTION_ASM_OP
#define BSS_SECTION_ASM_OP "\t.section\t.bss"

/* Like `ASM_OUTPUT_BSS' except takes the required alignment as a
   separate, explicit argument.  If you define this macro, it is used
   in place of `ASM_OUTPUT_BSS', and gives you more flexibility in
   handling the required alignment of the variable.  The alignment is
   specified as the number of bits.

   Try to use function `asm_output_aligned_bss' defined in file
   `varasm.c' when defining this macro.  */
#undef  ASM_OUTPUT_ALIGNED_BSS
#define ASM_OUTPUT_ALIGNED_BSS(FILE, DECL, NAME, SIZE, ALIGN) \
  asm_output_aligned_bss (FILE, DECL, NAME, SIZE, ALIGN)

/************************[  Debugger stuff  ]*********************************/

#undef  DBX_REGISTER_NUMBER
#define DBX_REGISTER_NUMBER(n)	(TARGET_64BIT ? dbx64_register_map[n]	\
				: (write_symbols == DWARF2_DEBUG)	\
				  ? svr4_dbx_register_map[(n)]		\
				  : dbx_register_map[(n)])

/* The same functions are used to creating the DWARF2 debug info and C++
   unwind info (except.c).  Regardless of the debug format requested, the
   register numbers used in exception unwinding sections still have to be
   DWARF compatible.  IMO the GCC folks may be abusing the DBX_REGISTER_NUMBER
   macro to mean too much.  */
#define DWARF_FRAME_REGNUM(n)	(TARGET_64BIT ? dbx64_register_map[n]	\
				: svr4_dbx_register_map[(n)])

/* stabs-in-elf has offsets relative to function beginning */
#undef  DBX_OUTPUT_LBRAC
#define DBX_OUTPUT_LBRAC(FILE, NAME)					\
  do {									\
    fprintf (asm_out_file, "%s %d,0,0,", ASM_STABN_OP, N_LBRAC);	\
    assemble_name (asm_out_file, NAME);					\
        fputc ('-', asm_out_file);					\
        assemble_name (asm_out_file,					\
		 XSTR (XEXP (DECL_RTL (current_function_decl), 0), 0));	\
    fprintf (asm_out_file, "\n");					\
  } while (0)

#undef  DBX_OUTPUT_RBRAC
#define DBX_OUTPUT_RBRAC(FILE, NAME)					\
  do {									\
    fprintf (asm_out_file, "%s %d,0,0,", ASM_STABN_OP, N_RBRAC);	\
    assemble_name (asm_out_file, NAME);					\
        fputc ('-', asm_out_file);					\
        assemble_name (asm_out_file,					\
		 XSTR (XEXP (DECL_RTL (current_function_decl), 0), 0));	\
    fprintf (asm_out_file, "\n");					\
  } while (0)

#undef NEED_INDICATE_EXEC_STACK
#define NEED_INDICATE_EXEC_STACK 1
