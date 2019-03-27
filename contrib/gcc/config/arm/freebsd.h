/* Definitions for StrongARM running FreeBSD using the ELF format
   Copyright (C) 2001, 2004 Free Software Foundation, Inc.
   Contributed by David E. O'Brien <obrien@FreeBSD.org> and BSDi.

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

#undef  SUBTARGET_CPP_SPEC
#define SUBTARGET_CPP_SPEC FBSD_CPP_SPEC

#undef  SUBTARGET_EXTRA_SPECS
#define SUBTARGET_EXTRA_SPECS \
  { "subtarget_extra_asm_spec",	SUBTARGET_EXTRA_ASM_SPEC }, \
  { "subtarget_asm_float_spec", SUBTARGET_ASM_FLOAT_SPEC }, \
  { "fbsd_dynamic_linker", FBSD_DYNAMIC_LINKER }

#undef SUBTARGET_EXTRA_ASM_SPEC
#ifdef TARGET_ARM_EABI
#define SUBTARGET_EXTRA_ASM_SPEC	\
  "%{mabi=apcs-gnu|mabi=atpcs:-meabi=gnu;:-meabi=4} %{fpic|fpie:-k} %{fPIC|fPIE:-k}"
#else
#define SUBTARGET_EXTRA_ASM_SPEC	\
  "-matpcs %{fpic|fpie:-k} %{fPIC|fPIE:-k}"
#endif

/* Default to full FPA if -mhard-float is specified. */
#undef SUBTARGET_ASM_FLOAT_SPEC
#define SUBTARGET_ASM_FLOAT_SPEC		\
  "%{mhard-float:-mfpu=fpa}			\
   %{mfloat-abi=hard:{!mfpu=*:-mfpu=fpa}}	\
   %{!mhard-float: %{msoft-float:-mfpu=softvfp;:-mfpu=softvfp}}"

#undef	LINK_SPEC
#define LINK_SPEC "							\
  %{p:%nconsider using `-pg' instead of `-p' with gprof(1) }		\
  %{v:-V}								\
  %{assert*} %{R*} %{rpath*} %{defsym*}					\
  %{shared:-Bshareable %{h*} %{soname*}}				\
  %{!shared:								\
    %{!static:								\
      %{rdynamic:-export-dynamic}					\
      %{!dynamic-linker:-dynamic-linker %(fbsd_dynamic_linker) }}	\
    %{static:-Bstatic}}							\
  %{!static:--hash-style=both --enable-new-dtags}			\
  %{symbolic:-Bsymbolic}						\
  -X %{mbig-endian:-EB} %{mlittle-endian:-EL}"

/************************[  Target stuff  ]***********************************/


#ifndef TARGET_ENDIAN_DEFAULT
#define TARGET_ENDIAN_DEFAULT 0
#endif

#ifdef TARGET_ARM_EABI
/* We default to a soft-float ABI so that binaries can run on all
   target hardware.  */
#undef TARGET_DEFAULT_FLOAT_ABI
#define TARGET_DEFAULT_FLOAT_ABI ARM_FLOAT_ABI_SOFT

#undef TARGET_DEFAULT
#define TARGET_DEFAULT (MASK_INTERWORK | TARGET_ENDIAN_DEFAULT)

#undef ARM_DEFAULT_ABI
#define ARM_DEFAULT_ABI ARM_ABI_AAPCS_LINUX

#undef  TARGET_OS_CPP_BUILTINS
#define TARGET_OS_CPP_BUILTINS() 		\
  do						\
    {						\
      FBSD_TARGET_OS_CPP_BUILTINS();		\
      TARGET_BPABI_CPP_BUILTINS();		\
    }						\
  while (false)
#else
/* Default it to use ATPCS with soft-VFP.  */
#undef TARGET_DEFAULT
#define TARGET_DEFAULT			\
  (MASK_APCS_FRAME			\
   | TARGET_ENDIAN_DEFAULT)

#undef ARM_DEFAULT_ABI
#define ARM_DEFAULT_ABI ARM_ABI_ATPCS

#undef FPUTYPE_DEFAULT
#define FPUTYPE_DEFAULT FPUTYPE_VFP
#endif

/* Define the actual types of some ANSI-mandated types.
   Needs to agree with <machine/ansi.h>.  GCC defaults come from c-decl.c,
   c-common.c, and config/<arch>/<arch>.h.  */

/* arm.h gets this wrong for FreeBSD.  We use the GCC defaults instead.  */

#undef  SIZE_TYPE
#define SIZE_TYPE	"unsigned int"

#undef  PTRDIFF_TYPE
#define PTRDIFF_TYPE	"int"

/* We use the GCC defaults here.  */
#undef WCHAR_TYPE

#if defined(FREEBSD_ARCH_armv6)
#undef  SUBTARGET_CPU_DEFAULT
#define SUBTARGET_CPU_DEFAULT	TARGET_CPU_arm1176jzs
#undef FBSD_TARGET_CPU_CPP_BUILTINS
#define FBSD_TARGET_CPU_CPP_BUILTINS()		\
  do {						\
    builtin_define ("__FreeBSD_ARCH_armv6__");	\
  } while (0)
#undef  TARGET_VERSION
#define TARGET_VERSION fprintf (stderr, " (FreeBSD/armv6 ELF)");
#else
#undef  SUBTARGET_CPU_DEFAULT
#define SUBTARGET_CPU_DEFAULT	TARGET_CPU_arm9
#undef  TARGET_VERSION
#define TARGET_VERSION fprintf (stderr, " (FreeBSD/StrongARM ELF)");
#endif

/* FreeBSD does its profiling differently to the Acorn compiler. We
   don't need a word following the mcount call; and to skip it
   requires either an assembly stub or use of fomit-frame-pointer when
   compiling the profiling functions.  Since we break Acorn CC
   compatibility below a little more won't hurt.  */

#undef ARM_FUNCTION_PROFILER
#define ARM_FUNCTION_PROFILER(STREAM,LABELNO)		\
{							\
  asm_fprintf (STREAM, "\tmov\t%Rip, %Rlr\n");		\
  asm_fprintf (STREAM, "\tbl\t__mcount%s\n",		\
	       (TARGET_ARM && NEED_PLT_RELOC)		\
	       ? "(PLT)" : "");				\
}

/* Clear the instruction cache from `BEG' to `END'.  This makes a
   call to the ARM_SYNC_ICACHE architecture specific syscall.  */
#define CLEAR_INSN_CACHE(BEG, END)					\
do									\
  {									\
    extern int sysarch(int number, void *args);				\
    struct								\
      {									\
	unsigned int addr;						\
	int          len;						\
      } s;								\
    s.addr = (unsigned int)(BEG);					\
    s.len = (END) - (BEG);						\
    (void) sysarch (0, &s);						\
  }									\
while (0)

