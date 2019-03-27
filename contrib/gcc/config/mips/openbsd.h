/* Configuration for  a MIPS ABI32 OpenBSD target.
   Copyright (C) 1999, 2003, 2004 Free Software Foundation, Inc.

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

/* Definitions needed for OpenBSD, to avoid picking mips 'defaults'.  */

/* GAS must know this.  */
#undef SUBTARGET_ASM_SPEC
#define SUBTARGET_ASM_SPEC "%{fPIC|fPIE:-KPIC}"

#define AS_NEEDS_DASH_FOR_PIPED_INPUT

/* CPP specific OpenBSD specs.  */
#undef SUBTARGET_CPP_SPEC
#define SUBTARGET_CPP_SPEC OBSD_CPP_SPEC

/* Needed for ELF (inspired by netbsd-elf).  */
#undef LOCAL_LABEL_PREFIX
#define LOCAL_LABEL_PREFIX	"."

/* The profiling lib spec here is not really correct but we leave
   it as it is until we have some kind of profiling working.  */
#define LIB_SPEC OBSD_LIB_SPEC

/* mips assembler uses .set for arcane purposes.  __attribute__((alias))
   and friends won't work until we get recent binutils with .weakext
	support.  */
#undef SET_ASM_OP

#define TARGET_OS_CPP_BUILTINS()			\
    do {						\
	builtin_define ("__unix__");			\
	builtin_define ("__SYSTYPE_BSD__");		\
	builtin_define ("__NO_LEADING_UNDERSCORES__");	\
	builtin_define ("__GP_SUPPORT__");		\
	builtin_define ("__OpenBSD__");			\
	builtin_assert ("system=unix");			\
	builtin_assert ("system=OpenBSD");		\
} while (0)

/* Layout of source language data types.  */

/* This must agree with <machine/ansi.h>.  */
#undef SIZE_TYPE
#define SIZE_TYPE "unsigned int"

#undef PTRDIFF_TYPE
#define PTRDIFF_TYPE "int"

#undef WCHAR_TYPE
#define WCHAR_TYPE "int"

#undef WCHAR_TYPE_SIZE
#define WCHAR_TYPE_SIZE 32

/* Controlling the compilation driver.  */

/* LINK_SPEC appropriate for OpenBSD:  support for GCC options
   -static, -assert, and -nostdlib. Dynamic loader control.  */
#undef LINK_SPEC
#define LINK_SPEC \
  "%{G*} %{EB} %{EL} %{mips1} %{mips2} %{mips3} \
   %{bestGnum} %{shared} %{non_shared} \
   %{call_shared} %{no_archive} %{exact_version} \
   %{!shared: %{!non_shared: %{!call_shared: -non_shared}}} \
   %{!dynamic-linker:-dynamic-linker /usr/libexec/ld.so} \
   %{!nostdlib:%{!r*:%{!e*:-e __start}}} -dc -dp \
   %{static:-Bstatic} %{!static:-Bdynamic} %{assert*}"

/* -G is incompatible with -KPIC which is the default, so only allow objects
   in the small data section if the user explicitly asks for it.  */
#undef MIPS_DEFAULT_GVALUE
#define MIPS_DEFAULT_GVALUE 0


/* Since gas and gld are standard on OpenBSD, we don't need these.  */
#undef ASM_FINAL_SPEC
#undef STARTFILE_SPEC

/* Switch into a generic section.  */
#undef TARGET_ASM_NAMED_SECTION
#define TARGET_ASM_NAMED_SECTION  default_elf_asm_named_section
