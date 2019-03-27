/* Definitions of target machine for GCC, for ELF on NetBSD/sparc
   and NetBSD/sparc64.
   Copyright (C) 2002, 2003, 2004, 2005 Free Software Foundation, Inc.
   Contributed by Matthew Green (mrg@eterna.com.au).

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

#define TARGET_OS_CPP_BUILTINS()			\
  do							\
    {							\
      NETBSD_OS_CPP_BUILTINS_ELF();			\
      if (TARGET_ARCH64)				\
	{						\
	  builtin_define ("__sparc64__");		\
	  builtin_define ("__sparc_v9__");		\
	  builtin_define ("__sparcv9");			\
	}						\
      else						\
	builtin_define ("__sparc");			\
      builtin_define ("__sparc__");			\
    }							\
  while (0)

/* Make sure these are undefined.  */
#undef MD_EXEC_PREFIX
#undef MD_STARTFILE_PREFIX

/* CPP defines used by all NetBSD targets.  */
#undef CPP_SUBTARGET_SPEC
#define CPP_SUBTARGET_SPEC "%(netbsd_cpp_spec)"

/* SIZE_TYPE and PTRDIFF_TYPE are wrong from sparc/sparc.h.  */
#undef SIZE_TYPE
#define SIZE_TYPE "long unsigned int"

#undef PTRDIFF_TYPE
#define PTRDIFF_TYPE "long int"

/* This is the char to use for continuation (in case we need to turn
   continuation back on).  */
#undef DBX_CONTIN_CHAR
#define DBX_CONTIN_CHAR '?'

#undef  LOCAL_LABEL_PREFIX
#define LOCAL_LABEL_PREFIX  "."

/* This is how to store into the string LABEL
   the symbol_ref name of an internal numbered label where
   PREFIX is the class of label and NUM is the number within the class.
   This is suitable for output with `assemble_name'.  */

#undef  ASM_GENERATE_INTERNAL_LABEL
#define ASM_GENERATE_INTERNAL_LABEL(LABEL,PREFIX,NUM)	\
  sprintf ((LABEL), "*.L%s%ld", (PREFIX), (long)(NUM))

#undef USER_LABEL_PREFIX
#define USER_LABEL_PREFIX ""

#undef ASM_SPEC
#define ASM_SPEC "%{fpic|fPIC|fpie|fPIE:-K PIC} %{V} %{v:%{!V:-V}} \
%{mlittle-endian:-EL} \
%(asm_cpu) %(asm_arch) %(asm_relax)"

#undef STDC_0_IN_SYSTEM_HEADERS

/* Attempt to enable execute permissions on the stack.  */
#define ENABLE_EXECUTE_STACK NETBSD_ENABLE_EXECUTE_STACK

#undef TARGET_VERSION
#define TARGET_VERSION fprintf (stderr, " (%s)", TARGET_NAME);

/* Below here exists the merged NetBSD/sparc & NetBSD/sparc64 compiler
   description, allowing one to build 32 bit or 64 bit applications
   on either.  We define the sparc & sparc64 versions of things,
   occasionally a neutral version (should be the same as "netbsd-elf.h")
   and then based on SPARC_BI_ARCH, DEFAULT_ARCH32_P, and TARGET_CPU_DEFAULT,
   we choose the correct version.  */

/* We use the default NetBSD ELF STARTFILE_SPEC and ENDFILE_SPEC
   definitions, even for the SPARC_BI_ARCH compiler, because NetBSD does
   not have a default place to find these libraries..  */

/* Name the port(s).  */
#define TARGET_NAME64     "NetBSD/sparc64 ELF"
#define TARGET_NAME32     "NetBSD/sparc ELF"

/* TARGET_CPU_DEFAULT is set in Makefile.in.  We test for 64-bit default
   platform here.  */

#if TARGET_CPU_DEFAULT == TARGET_CPU_v9 \
 || TARGET_CPU_DEFAULT == TARGET_CPU_ultrasparc
/* A 64 bit v9 compiler with stack-bias,
   in a Medium/Low code model environment.  */

#undef TARGET_DEFAULT
#define TARGET_DEFAULT \
  (MASK_V9 + MASK_PTR64 + MASK_64BIT /* + MASK_HARD_QUAD */ \
   + MASK_STACK_BIAS + MASK_APP_REGS + MASK_FPU + MASK_LONG_DOUBLE_128)

#undef SPARC_DEFAULT_CMODEL
#define SPARC_DEFAULT_CMODEL CM_MEDANY

#endif

/* CC1_SPEC for NetBSD/sparc.  */
#define CC1_SPEC32 \
 "%{sun4:} %{target:} \
  %{mcypress:-mcpu=cypress} \
  %{msparclite:-mcpu=sparclite} %{mf930:-mcpu=f930} %{mf934:-mcpu=f934} \
  %{mv8:-mcpu=v8} %{msupersparc:-mcpu=supersparc} \
  %{m32:%{m64:%emay not use both -m32 and -m64}} \
  %{m64: \
    -mptr64 -mstack-bias -mno-v8plus -mlong-double-128 \
    %{!mcpu*: \
      %{!mcypress: \
        %{!msparclite: \
	  %{!mf930: \
	    %{!mf934: \
	      %{!mv8*: \
	        %{!msupersparc:-mcpu=ultrasparc}}}}}}} \
    %{!mno-vis:%{!mcpu=v9:-mvis}} \
    %{p:-mcmodel=medlow} \
    %{pg:-mcmodel=medlow}}"

#define CC1_SPEC64 \
 "%{sun4:} %{target:} \
  %{mcypress:-mcpu=cypress} \
  %{msparclite:-mcpu=sparclite} %{mf930:-mcpu=f930} %{mf934:-mcpu=f934} \
  %{mv8:-mcpu=v8} %{msupersparc:-mcpu=supersparc} \
  %{m32:%{m64:%emay not use both -m32 and -m64}} \
  %{m32: \
    -mptr32 -mno-stack-bias \
    %{!mlong-double-128:-mlong-double-64} \
    %{!mcpu*: \
      %{!mcypress: \
	%{!msparclite: \
	  %{!mf930: \
	    %{!mf934: \
	      %{!mv8*: \
		%{!msupersparc:-mcpu=cypress}}}}}}}} \
  %{!m32: \
    %{p:-mcmodel=medlow} \
    %{pg:-mcmodel=medlow}}"

/* Make sure we use the right output format.  Pick a default and then
   make sure -m32/-m64 switch to the right one.  */

#define LINK_ARCH32_SPEC "-m elf32_sparc"

#define LINK_ARCH64_SPEC "-m elf64_sparc"

#define LINK_ARCH_SPEC \
 "%{m32:%(link_arch32)} \
  %{m64:%(link_arch64)} \
  %{!m32:%{!m64:%(link_arch_default)}}"

#undef LINK_SPEC
#define LINK_SPEC \
 "%(link_arch) \
  %{!mno-relax:%{!r:-relax}} \
  %(netbsd_link_spec)"

#define NETBSD_ENTRY_POINT "__start"

#if DEFAULT_ARCH32_P
#define LINK_ARCH_DEFAULT_SPEC LINK_ARCH32_SPEC
#else
#define LINK_ARCH_DEFAULT_SPEC LINK_ARCH64_SPEC
#endif

/* What extra spec entries do we need?  */
#undef SUBTARGET_EXTRA_SPECS
#define SUBTARGET_EXTRA_SPECS \
  { "link_arch32",		LINK_ARCH32_SPEC }, \
  { "link_arch64",		LINK_ARCH64_SPEC }, \
  { "link_arch_default",	LINK_ARCH_DEFAULT_SPEC }, \
  { "link_arch",		LINK_ARCH_SPEC }, \
  { "netbsd_cpp_spec",		NETBSD_CPP_SPEC }, \
  { "netbsd_link_spec",		NETBSD_LINK_SPEC_ELF }, \
  { "netbsd_entry_point",	NETBSD_ENTRY_POINT },


/* Build a compiler that supports -m32 and -m64?  */

#ifdef SPARC_BI_ARCH

#undef LONG_DOUBLE_TYPE_SIZE
#define LONG_DOUBLE_TYPE_SIZE (TARGET_LONG_DOUBLE_128 ? 128 : 64)

#if defined(__arch64__) || defined(__LONG_DOUBLE_128__)
#define LIBGCC2_LONG_DOUBLE_TYPE_SIZE 128
#else
#define LIBGCC2_LONG_DOUBLE_TYPE_SIZE 64
#endif

#undef  CC1_SPEC
#if DEFAULT_ARCH32_P
#define CC1_SPEC CC1_SPEC32
#else
#define CC1_SPEC CC1_SPEC64
#endif

#if DEFAULT_ARCH32_P
#define MULTILIB_DEFAULTS { "m32" }
#else
#define MULTILIB_DEFAULTS { "m64" }
#endif

/* Name the port.  */
#undef TARGET_NAME
#define TARGET_NAME     (DEFAULT_ARCH32_P ? TARGET_NAME32 : TARGET_NAME64)

#else	/* SPARC_BI_ARCH */

#if TARGET_CPU_DEFAULT == TARGET_CPU_v9 \
 || TARGET_CPU_DEFAULT == TARGET_CPU_ultrasparc

#undef LONG_DOUBLE_TYPE_SIZE
#define LONG_DOUBLE_TYPE_SIZE 128

#undef LIBGCC2_LONG_DOUBLE_TYPE_SIZE
#define LIBGCC2_LONG_DOUBLE_TYPE_SIZE 128

#undef  CC1_SPEC
#define CC1_SPEC CC1_SPEC64

#undef TARGET_NAME
#define TARGET_NAME     TARGET_NAME64

#else	/* TARGET_CPU_DEFAULT == TARGET_CPU_v9 \
	|| TARGET_CPU_DEFAULT == TARGET_CPU_ultrasparc */

/* A 32-bit only compiler.  NetBSD don't support 128 bit `long double'
   for 32-bit code, unlike Solaris.  */

#undef LONG_DOUBLE_TYPE_SIZE
#define LONG_DOUBLE_TYPE_SIZE 64

#undef LIBGCC2_LONG_DOUBLE_TYPE_SIZE
#define LIBGCC2_LONG_DOUBLE_TYPE_SIZE 64

#undef  CC1_SPEC
#define CC1_SPEC CC1_SPEC32

#undef TARGET_NAME
#define TARGET_NAME     TARGET_NAME32

#endif	/* TARGET_CPU_DEFAULT == TARGET_CPU_v9 \
	|| TARGET_CPU_DEFAULT == TARGET_CPU_ultrasparc */

#endif	/* SPARC_BI_ARCH */

/* We use GNU ld so undefine this so that attribute((init_priority)) works.  */
#undef CTORS_SECTION_ASM_OP
#undef DTORS_SECTION_ASM_OP
