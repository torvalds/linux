/* Configuration for an OpenBSD i386 target.
   
   Copyright (C) 2005 Free Software Foundation, Inc.

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

/* This gets defined in tm.h->linux.h->svr4.h, and keeps us from using
   libraries compiled with the native cc, so undef it. */
#undef NO_DOLLAR_IN_LABEL

/* Override the default comment-starter of "/".  */
#undef ASM_COMMENT_START
#define ASM_COMMENT_START "#"

#undef DBX_REGISTER_NUMBER
#define DBX_REGISTER_NUMBER(n)  svr4_dbx_register_map[n]

/* This goes away when the math-emulator is fixed */
#undef TARGET_DEFAULT
#define TARGET_DEFAULT \
  (MASK_80387 | MASK_IEEE_FP | MASK_FLOAT_RETURNS | MASK_NO_FANCY_MATH_387)

/* Run-time target specifications */

#define TARGET_OS_CPP_BUILTINS()		\
  do						\
    {						\
    	OPENBSD_OS_CPP_BUILTINS();		\
    }						\
  while (0)

/* As an elf system, we need crtbegin/crtend stuff.  */
#undef STARTFILE_SPEC
#define STARTFILE_SPEC "\
	%{!shared: %{pg:gcrt0%O%s} %{!pg:%{p:gcrt0%O%s} %{!p:crt0%O%s}} \
	crtbegin%O%s} %{shared:crtbeginS%O%s}"
#undef ENDFILE_SPEC
#define ENDFILE_SPEC "%{!shared:crtend%O%s} %{shared:crtendS%O%s}"

/* Layout of source language data types.  */

/* This must agree with <machine/ansi.h> */
#undef SIZE_TYPE
#define SIZE_TYPE "unsigned int"

#undef PTRDIFF_TYPE
#define PTRDIFF_TYPE "int"

#undef WCHAR_TYPE
#define WCHAR_TYPE "int"

#undef WCHAR_TYPE_SIZE
#define WCHAR_TYPE_SIZE BITS_PER_WORD

/* Assembler format: overall framework.  */

#undef ASM_APP_ON
#define ASM_APP_ON "#APP\n"

#undef ASM_APP_OFF
#define ASM_APP_OFF "#NO_APP\n"

#undef SET_ASM_OP
#define SET_ASM_OP	"\t.set\t"

/* The following macros were originally stolen from i386v4.h.
   These have to be defined to get PIC code correct.  */

/* Assembler format: dispatch tables.  */

/* Assembler format: sections.  */

/* Stack & calling: aggregate returns.  */

/* Don't default to pcc-struct-return, because gcc is the only compiler, and
   we want to retain compatibility with older gcc versions.  */
#define DEFAULT_PCC_STRUCT_RETURN 0

/* Assembler format: alignment output.  */

#ifdef HAVE_GAS_MAX_SKIP_P2ALIGN
#define ASM_OUTPUT_MAX_SKIP_ALIGN(FILE,LOG,MAX_SKIP) \
  if ((LOG) != 0) {\
    if ((MAX_SKIP) == 0) fprintf ((FILE), "\t.p2align %d\n", (LOG)); \
    else fprintf ((FILE), "\t.p2align %d,,%d\n", (LOG), (MAX_SKIP)); \
  }
#endif

/* Stack & calling: profiling.  */

/* OpenBSD's profiler recovers all information from the stack pointer.
   The icky part is not here, but in machine/profile.h.  */
#undef FUNCTION_PROFILER
#define FUNCTION_PROFILER(FILE, LABELNO)  \
  fputs (flag_pic ? "\tcall __mcount@PLT\n": "\tcall __mcount\n", FILE);

/* Assembler format: exception region output.  */

/* our configuration still doesn't handle dwarf2 correctly */
#define DWARF2_UNWIND_INFO 0

/* Assembler format: alignment output.  */

/* Note that we pick up ASM_OUTPUT_MAX_SKIP_ALIGN from i386/gas.h */

/* Note that we pick up ASM_OUTPUT_MI_THUNK from unix.h.  */

#undef LINK_SPEC
#define LINK_SPEC \
  "%{!shared:%{!nostdlib:%{!r*:%{!e*:-e __start}}}} \
   %{shared:-shared} %{R*} \
   %{static:-Bstatic} \
   %{!static:-Bdynamic} \
   %{assert*} \
   %{!dynamic-linker:-dynamic-linker /usr/libexec/ld.so}"

#define OBSD_HAS_CORRECT_SPECS
