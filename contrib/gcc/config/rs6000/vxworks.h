/* Definitions of target machine for GNU compiler.  Vxworks PowerPC version.
   Copyright (C) 1996, 2000, 2002, 2003, 2004, 2005
   Free Software Foundation, Inc.
   Contributed by CodeSourcery, LLC.

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free
Software Foundation; either version 2, or (at your option) any later
version.

GCC is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING.  If not, write to the Free
Software Foundation, 51 Franklin Street, Fifth Floor, Boston, MA
02110-1301, USA.  */

/* Note to future editors: VxWorks is mostly an EABI target.  We do
   not use rs6000/eabi.h because we would have to override most of
   it anyway.  However, if you change that file, consider making
   analogous changes here too.  */

#undef TARGET_VERSION
#define TARGET_VERSION fprintf (stderr, " (PowerPC VxWorks)");

/* CPP predefined macros.  */

#undef TARGET_OS_CPP_BUILTINS
#define TARGET_OS_CPP_BUILTINS()		\
  do						\
    {						\
      builtin_define ("__ppc");			\
      builtin_define ("__EABI__");		\
      builtin_define ("__ELF__");		\
      builtin_define ("__vxworks");		\
      builtin_define ("__VXWORKS__");		\
      if (!TARGET_SOFT_FLOAT)			\
	builtin_define ("__hardfp");		\
						\
      /* C89 namespace violation! */		\
      builtin_define ("CPU_FAMILY=PPC");	\
    }						\
  while (0)

/* Only big endian PPC is supported by VxWorks.  */
#undef BYTES_BIG_ENDIAN
#define BYTES_BIG_ENDIAN 1

/* We have to kill off the entire specs set created by rs6000/sysv4.h
   and substitute our own set.  The top level vxworks.h has done some
   of this for us.  */

#undef SUBTARGET_EXTRA_SPECS
#undef CPP_SPEC
#undef CC1_SPEC
#undef ASM_SPEC

#define SUBTARGET_EXTRA_SPECS /* none needed */

/* FIXME: The only reason we allow no -mcpu switch at all is because
   config-ml.in insists on a "." multilib. */
#define CPP_SPEC \
"%{!DCPU=*:		  \
   %{mcpu=403 : -DCPU=PPC403  ; \
     mcpu=405 : -DCPU=PPC405  ; \
     mcpu=440 : -DCPU=PPC440  ; \
     mcpu=603 : -DCPU=PPC603  ; \
     mcpu=604 : -DCPU=PPC604  ; \
     mcpu=860 : -DCPU=PPC860  ; \
     mcpu=8540: -DCPU=PPC85XX ; \
              : -DCPU=PPC604  }}" \
VXWORKS_ADDITIONAL_CPP_SPEC

#define CC1_SPEC						\
"%{G*} %{mno-sdata:-msdata=none} %{msdata:-msdata=default}	\
 %{mlittle|mlittle-endian:-mstrict-align}			\
 %{profile: -p}		\
 %{fvec:-maltivec} %{fvec-eabi:-maltivec -mabi=altivec}"

#define ASM_SPEC \
"%(asm_cpu) \
 %{.s: %{mregnames} %{mno-regnames}} %{.S: %{mregnames} %{mno-regnames}} \
 %{v:-v} %{Qy:} %{!Qn:-Qy} %{n} %{T} %{Ym,*} %{Yd,*} %{Wa,*:%*} \
 %{mrelocatable} %{mrelocatable-lib} %{fpic:-K PIC} %{fPIC:-K PIC} -mbig"

#undef  LIB_SPEC
#define LIB_SPEC VXWORKS_LIB_SPEC
#undef  LINK_SPEC
#define LINK_SPEC VXWORKS_LINK_SPEC
#undef  STARTFILE_SPEC
#define STARTFILE_SPEC VXWORKS_STARTFILE_SPEC
#undef  ENDFILE_SPEC
#define ENDFILE_SPEC VXWORKS_ENDFILE_SPEC

/* There is no default multilib.  */
#undef MULTILIB_DEFAULTS

#undef TARGET_DEFAULT
#define TARGET_DEFAULT \
  (MASK_POWERPC | MASK_NEW_MNEMONICS | MASK_EABI | MASK_STRICT_ALIGN)

#undef PROCESSOR_DEFAULT
#define PROCESSOR_DEFAULT PROCESSOR_PPC604

/* Nor sdata, for kernel mode.  We use this in
   SUBSUBTARGET_INITIALIZE_OPTIONS, after rs6000_rtp has been initialized.  */
#undef SDATA_DEFAULT_SIZE
#define SDATA_DEFAULT_SIZE (TARGET_VXWORKS_RTP ? 8 : 0)

#undef  STACK_BOUNDARY
#define STACK_BOUNDARY (16*BITS_PER_UNIT)
/* Override sysv4.h, reset to the default.  */
#undef  PREFERRED_STACK_BOUNDARY

/* Enable SPE */
#undef TARGET_SPE_ABI
#undef TARGET_SPE
#undef TARGET_E500
#undef TARGET_ISEL
#undef TARGET_FPRS

#define TARGET_SPE_ABI rs6000_spe_abi
#define TARGET_SPE rs6000_spe
#define TARGET_E500 (rs6000_cpu == PROCESSOR_PPC8540)
#define TARGET_ISEL rs6000_isel
#define TARGET_FPRS (!rs6000_float_gprs)

/* Make -mcpu=8540 imply SPE.  ISEL is automatically enabled, the
   others must be done by hand.  Handle -mrtp.  Disable -fPIC
   for -mrtp - the VxWorks PIC model is not compatible with it.  */
#undef SUBSUBTARGET_OVERRIDE_OPTIONS
#define SUBSUBTARGET_OVERRIDE_OPTIONS		\
  do {						\
    if (TARGET_E500)				\
      {						\
	rs6000_spe = 1;				\
	rs6000_spe_abi = 1;			\
	rs6000_float_gprs = 1;			\
      }						\
						\
  if (!g_switch_set)				\
    g_switch_value = SDATA_DEFAULT_SIZE;	\
  VXWORKS_OVERRIDE_OPTIONS;			\
  } while (0)

/* No _mcount profiling on VxWorks.  */
#undef FUNCTION_PROFILER
#define FUNCTION_PROFILER(FILE,LABELNO) VXWORKS_FUNCTION_PROFILER(FILE,LABELNO)
