/* Definitions of target machine for GNU compiler.  Generic IRIX version.
   Copyright (C) 1993, 1995, 1996, 1998, 2000,
   2001, 2002, 2003, 2004 Free Software Foundation, Inc.

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

/* We are compiling for IRIX now.  */
#undef TARGET_IRIX
#define TARGET_IRIX 1

/* The size in bytes of a DWARF field indicating an offset or length
   relative to a debug info section, specified to be 4 bytes in the DWARF-2
   specification.  The SGI/MIPS ABI defines it to be the same as PTR_SIZE.  */
#define DWARF_OFFSET_SIZE PTR_SIZE

/* The size in bytes of the initial length field in a debug info
   section.  The DWARF 3 (draft) specification defines this to be
   either 4 or 12 (with a 4-byte "escape" word when it's 12), but the
   SGI/MIPS ABI predates this standard and defines it to be the same
   as DWARF_OFFSET_SIZE.  */
#define DWARF_INITIAL_LENGTH_SIZE DWARF_OFFSET_SIZE

/* MIPS assemblers don't have the usual .set foo,bar construct;
   .set is used for assembler options instead.  */
#undef SET_ASM_OP
#define ASM_OUTPUT_DEF(FILE, LABEL1, LABEL2)			\
  do								\
    {								\
      fputc ('\t', FILE);					\
      assemble_name (FILE, LABEL1);				\
      fputs (" = ", FILE);					\
      assemble_name (FILE, LABEL2);				\
      fputc ('\n', FILE);					\
    }								\
  while (0)

/* The MIPSpro o32 linker warns about not linking .comment sections.  */
#undef IDENT_ASM_OP

#undef LOCAL_LABEL_PREFIX
#define LOCAL_LABEL_PREFIX (TARGET_NEWABI ? "." : "$")

#undef ASM_DECLARE_OBJECT_NAME
#define ASM_DECLARE_OBJECT_NAME mips_declare_object_name

#undef ASM_FINISH_DECLARE_OBJECT
#define ASM_FINISH_DECLARE_OBJECT mips_finish_declare_object

/* Also do this for libcalls.  */
#undef TARGET_ASM_EXTERNAL_LIBCALL
#define TARGET_ASM_EXTERNAL_LIBCALL irix_output_external_libcall

/* The linker needs a space after "-o".  */
#define SWITCHES_NEED_SPACES "o"

/* Specify wchar_t types.  */
#undef WCHAR_TYPE
#define WCHAR_TYPE (Pmode == DImode ? "int" : "long int")

#undef WCHAR_TYPE_SIZE
#define WCHAR_TYPE_SIZE INT_TYPE_SIZE

/* Same for wint_t.  */
#undef WINT_TYPE
#define WINT_TYPE (Pmode == DImode ? "int" : "long int")

#undef WINT_TYPE_SIZE
#define WINT_TYPE_SIZE 32

/* Plain char is unsigned in the SGI compiler.  */
#undef DEFAULT_SIGNED_CHAR
#define DEFAULT_SIGNED_CHAR 0

#define WORD_SWITCH_TAKES_ARG(STR)			\
  (DEFAULT_WORD_SWITCH_TAKES_ARG (STR)			\
   || strcmp (STR, "rpath") == 0)

#define TARGET_OS_CPP_BUILTINS()				\
  do								\
    {								\
      builtin_define_std ("host_mips");				\
      builtin_define_std ("sgi");				\
      builtin_define_std ("unix");				\
      builtin_define_std ("SYSTYPE_SVR4");			\
      builtin_define ("_MODERN_C");				\
      builtin_define ("_SVR4_SOURCE");				\
      builtin_define ("__DSO__");				\
      builtin_assert ("system=unix");				\
      builtin_assert ("system=svr4");				\
      builtin_assert ("machine=sgi");				\
								\
      if (mips_abi == ABI_32)					\
	{							\
	  builtin_define ("_ABIO32=1");				\
	  builtin_define ("_MIPS_SIM=_ABIO32");			\
	  builtin_define ("_MIPS_SZINT=32");			\
	  builtin_define ("_MIPS_SZLONG=32");			\
	  builtin_define ("_MIPS_SZPTR=32");			\
	}							\
      else if (mips_abi == ABI_64)				\
	{							\
	  builtin_define ("_ABI64=3");				\
	  builtin_define ("_MIPS_SIM=_ABI64");			\
	  builtin_define ("_MIPS_SZINT=32");			\
	  builtin_define ("_MIPS_SZLONG=64");			\
	  builtin_define ("_MIPS_SZPTR=64");			\
	}							\
      else							\
	{							\
	  builtin_define ("_ABIN32=2");				\
	  builtin_define ("_MIPS_SIM=_ABIN32");			\
	  builtin_define ("_MIPS_SZINT=32");			\
	  builtin_define ("_MIPS_SZLONG=32");			\
	  builtin_define ("_MIPS_SZPTR=32");			\
        }							\
								\
      if (!ISA_MIPS1 && !ISA_MIPS2)				\
	builtin_define ("_COMPILER_VERSION=601");		\
								\
      if (!TARGET_FLOAT64)					\
	builtin_define ("_MIPS_FPSET=16");			\
      else							\
	builtin_define ("_MIPS_FPSET=32");			\
								\
      /* We must always define _LONGLONG, even when -ansi is	\
	 used, because IRIX 5 system header files require it.	\
	 This is OK, because gcc never warns when long long	\
	 is used in system header files.			\
								\
	 An alternative would be to support the SGI builtin	\
	 type __long_long.  */					\
      builtin_define ("_LONGLONG");				\
								\
      /* IRIX 6.5.18 and above provide many ISO C99		\
	 features protected by the __c99 macro.			\
	 libstdc++ v3 needs them as well.  */			\
      if (TARGET_IRIX6)						\
	if (flag_isoc99 || c_dialect_cxx ())			\
	  builtin_define ("__c99");				\
								\
      /* The GNU C++ standard library requires that		\
	 __EXTENSIONS__ and _SGI_SOURCE be defined on at	\
	 least IRIX 6.2 and probably all IRIX 6 prior to 6.5.	\
	 We don't need this on IRIX 6.5 itself, but it		\
	 shouldn't hurt other than the namespace pollution.  */	\
      if (!flag_iso || (TARGET_IRIX6 && c_dialect_cxx ()))	\
	{							\
	  builtin_define ("__EXTENSIONS__");			\
	  builtin_define ("_SGI_SOURCE");			\
	}							\
    }								\
  while (0)

#undef SUBTARGET_CC1_SPEC
#define SUBTARGET_CC1_SPEC "%{static: -mno-abicalls}"

#undef INIT_SECTION_ASM_OP
#define INIT_SECTION_ASM_OP "\t.section\t.gcc_init,\"ax\",@progbits"

#undef FINI_SECTION_ASM_OP
#define FINI_SECTION_ASM_OP "\t.section\t.gcc_fini,\"ax\",@progbits"

#ifdef IRIX_USING_GNU_LD
#define IRIX_NO_UNRESOLVED ""
#else
#define IRIX_NO_UNRESOLVED "-no_unresolved"
#endif

/* Generic part of the LINK_SPEC.  */
#undef LINK_SPEC
#define LINK_SPEC "\
%{G*} %{EB} %{EL} %{mips1} %{mips2} %{mips3} %{mips4} \
%{bestGnum} %{shared} %{non_shared} \
%{call_shared} %{no_archive} %{exact_version} \
%{!shared: \
  %{!non_shared: %{!call_shared:%{!r: -call_shared " IRIX_NO_UNRESOLVED "}}}} \
%{rpath} -init __gcc_init -fini __gcc_fini " IRIX_SUBTARGET_LINK_SPEC

/* A linker error can empirically be avoided by removing duplicate
   library search directories.  */
#define LINK_ELIMINATE_DUPLICATE_LDIRECTORIES 1

/* Add -g to mips.h default to avoid confusing gas with local symbols
   generated from stabs info.  */
#undef NM_FLAGS
#define NM_FLAGS "-Bng"

/* The system header files are C++ aware.  */
/* ??? Unfortunately, most but not all of the headers are C++ aware.
   Specifically, curses.h is not, and as a consequence, defining this
   used to prevent libg++ building.  This is no longer the case so
   define it again to prevent other problems, e.g. with getopt in
   unistd.h.  We still need some way to fix just those files that need
   fixing.  */
#define NO_IMPLICIT_EXTERN_C 1

/* -G is incompatible with -KPIC which is the default, so only allow objects
   in the small data section if the user explicitly asks for it.  */
#undef MIPS_DEFAULT_GVALUE
#define MIPS_DEFAULT_GVALUE 0

/* The native o32 IRIX linker does not support merging without a
   special elspec(5) file.  */
#ifndef IRIX_USING_GNU_LD
#undef HAVE_GAS_SHF_MERGE
#define HAVE_GAS_SHF_MERGE 0
#endif
