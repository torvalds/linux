/* Definitions for Sun SPARC64 running FreeBSD using the ELF format
   Copyright (C) 2001, 2002, 2004, 2005, 2006 Free Software Foundation, Inc.
   Contributed by David E. O'Brien <obrien@FreeBSD.org> and BSDi.

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

#undef  SUBTARGET_EXTRA_SPECS
#define SUBTARGET_EXTRA_SPECS \
  { "fbsd_dynamic_linker", FBSD_DYNAMIC_LINKER }

/* FreeBSD needs the platform name (sparc64) defined.
   Emacs needs to know if the arch is 64 or 32-bits.
   This also selects which targets are available via -mcpu.  */

#undef  FBSD_TARGET_CPU_CPP_BUILTINS
#define FBSD_TARGET_CPU_CPP_BUILTINS()		\
  do						\
    {						\
      builtin_define ("__LP64__");		\
      builtin_define ("__sparc64__");		\
      builtin_define ("__sparc_v9__");		\
      builtin_define ("__sparcv9");		\
      builtin_define ("__sparc__");		\
      builtin_define ("__arch64__");		\
    }						\
  while (0)

#define LINK_SPEC "%(link_arch)						\
  %{!mno-relax:%{!r:-relax}}						\
  %{p:%nconsider using `-pg' instead of `-p' with gprof(1)}		\
  %{v:-V}								\
  %{assert*} %{R*} %{rpath*} %{defsym*}					\
  %{shared:-Bshareable %{h*} %{soname*}}				\
  %{!shared:								\
    %{!static:								\
      %{rdynamic:-export-dynamic}					\
      %{!dynamic-linker:-dynamic-linker %(fbsd_dynamic_linker) }}	\
    %{static:-Bstatic}}							\
  %{!static:--hash-style=both --enable-new-dtags}			\
  %{symbolic:-Bsymbolic}"


/************************[  Target stuff  ]***********************************/

/* Define the actual types of some ANSI-mandated types.  
   Needs to agree with <machine/ansi.h>.  GCC defaults come from c-decl.c,
   c-common.c, and config/<arch>/<arch>.h.  */

/* Earlier headers may get this wrong for FreeBSD.
   We use the GCC defaults instead.  */
#undef WCHAR_TYPE

#undef  WCHAR_TYPE_SIZE
#define WCHAR_TYPE_SIZE 32

/* Define for support of TFmode long double.
   SPARC ABI says that long double is 4 words.  */
#undef  LONG_DOUBLE_TYPE_SIZE
#define LONG_DOUBLE_TYPE_SIZE (TARGET_LONG_DOUBLE_128 ? 128 : 64)

/* Define this to set long double type size to use in libgcc2.c, which can
   not depend on target_flags.  */
#if defined(__arch64__) || defined(__LONG_DOUBLE_128__)
#define LIBGCC2_LONG_DOUBLE_TYPE_SIZE 128
#else
#define LIBGCC2_LONG_DOUBLE_TYPE_SIZE 64
#endif

/* Definitions for 64-bit SPARC running systems with ELF. */

#undef  TARGET_VERSION
#define TARGET_VERSION fprintf (stderr, " (FreeBSD/sparc64 ELF)");

#define TARGET_ELF		1

/* XXX */
/* A 64 bit v9 compiler with stack-bias,
   in a Medium/mid code model environment.  */

#undef  TARGET_DEFAULT
#define TARGET_DEFAULT \
  (MASK_V9 + MASK_64BIT + MASK_PTR64 /* + MASK_FASTER_STRUCTS */ \
   + MASK_STACK_BIAS + MASK_APP_REGS + MASK_FPU \
   + MASK_LONG_DOUBLE_128 /* + MASK_HARD_QUAD */)

/* The default code model.  */
#undef  SPARC_DEFAULT_CMODEL
#define SPARC_DEFAULT_CMODEL	CM_MEDLOW

#define ENABLE_EXECUTE_STACK						\
  static int need_enable_exec_stack;					\
  static void check_enabling(void) __attribute__ ((constructor));	\
  static void check_enabling(void)					\
  {									\
    extern int sysctlbyname(const char *, void *, size_t *, void *, size_t);\
    int prot = 0;							\
    size_t len = sizeof(prot);						\
									\
    sysctlbyname ("kern.stackprot", &prot, &len, NULL, 0);		\
    if (prot != 7)							\
      need_enable_exec_stack = 1;					\
  }									\
  extern void __enable_execute_stack (void *);				\
  void __enable_execute_stack (void *addr)				\
  {									\
    if (!need_enable_exec_stack)					\
      return;								\
    else {								\
      /* 7 is PROT_READ | PROT_WRITE | PROT_EXEC */ 			\
      if (mprotect (addr, TRAMPOLINE_SIZE, 7) < 0)			\
        perror ("mprotect of trampoline code");				\
    }									\
  }


/************************[  Assembler stuff  ]********************************/

#undef	LOCAL_LABEL_PREFIX
#define LOCAL_LABEL_PREFIX  "."

/* XXX2 */
/* This is how to store into the string LABEL
   the symbol_ref name of an internal numbered label where
   PREFIX is the class of label and NUM is the number within the class.
   This is suitable for output with `assemble_name'.  */

#undef  ASM_GENERATE_INTERNAL_LABEL
#define ASM_GENERATE_INTERNAL_LABEL(LABEL,PREFIX,NUM)			\
  sprintf (LABEL, "*.L%s%lu", PREFIX, (unsigned long)(NUM))


/************************[  Debugger stuff  ]*********************************/

/* This is the char to use for continuation (in case we need to turn
   continuation back on).  */

#undef  DBX_CONTIN_CHAR
#define DBX_CONTIN_CHAR	'?'

/* DWARF bits.  */

/* Follow Irix 6 and not the Dwarf2 draft in using 64-bit offsets. 
   Obviously the Dwarf2 folks havn't tried to actually build systems
   with their spec.  On a 64-bit system, only 64-bit relocs become
   RELATIVE relocations.  */

/* #define DWARF_OFFSET_SIZE PTR_SIZE */

#ifdef HAVE_AS_TLS
#undef TARGET_SUN_TLS
#undef TARGET_GNU_TLS
#define TARGET_SUN_TLS 0
#define TARGET_GNU_TLS 1
#endif

#undef ENDFILE_SPEC
#define ENDFILE_SPEC						\
  "%{ffast-math|funsafe-math-optimizations:crtfastmath.o%s} "	\
  FBSD_ENDFILE_SPEC

/* We use GNU ld so undefine this so that attribute((init_priority)) works.  */
#undef CTORS_SECTION_ASM_OP
#undef DTORS_SECTION_ASM_OP
