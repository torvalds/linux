/* Definitions of target machine for GNU compiler, for ARM.
   Copyright (C) 1991, 1993, 1994, 1995, 1996, 1997, 1998, 1999, 2000,
   2001, 2002, 2003, 2004, 2005, 2006 Free Software Foundation, Inc.
   Contributed by Pieter `Tiggr' Schoenmakers (rcpieter@win.tue.nl)
   and Martin Simmons (@harleqn.co.uk).
   More major hacks by Richard Earnshaw (rearnsha@arm.com)
   Minor hacks by Nick Clifton (nickc@cygnus.com)

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
   the Free Software Foundation, 51 Franklin Street, Fifth Floor, Boston,
   MA 02110-1301, USA.  */

#ifndef GCC_ARM_H
#define GCC_ARM_H

/* The architecture define.  */
extern char arm_arch_name[];

/* Target CPU builtins.  */
#define TARGET_CPU_CPP_BUILTINS()			\
  do							\
    {							\
	/* Define __arm__ even when in thumb mode, for	\
	   consistency with armcc.  */			\
	builtin_define ("__arm__");			\
	builtin_define ("__APCS_32__");			\
	if (TARGET_THUMB)				\
	  builtin_define ("__thumb__");			\
							\
	if (TARGET_BIG_END)				\
	  {						\
	    builtin_define ("__ARMEB__");		\
	    if (TARGET_THUMB)				\
	      builtin_define ("__THUMBEB__");		\
	    if (TARGET_LITTLE_WORDS)			\
	      builtin_define ("__ARMWEL__");		\
	  }						\
        else						\
	  {						\
	    builtin_define ("__ARMEL__");		\
	    if (TARGET_THUMB)				\
	      builtin_define ("__THUMBEL__");		\
	  }						\
							\
	if (TARGET_SOFT_FLOAT)				\
	  builtin_define ("__SOFTFP__");		\
							\
	if (TARGET_VFP)					\
	  builtin_define ("__VFP_FP__");		\
							\
	/* Add a define for interworking.		\
	   Needed when building libgcc.a.  */		\
	if (arm_cpp_interwork)				\
	  builtin_define ("__THUMB_INTERWORK__");	\
							\
	builtin_assert ("cpu=arm");			\
	builtin_assert ("machine=arm");			\
							\
	builtin_define (arm_arch_name);			\
	if (arm_arch_cirrus)				\
	  builtin_define ("__MAVERICK__");		\
	if (arm_arch_xscale)				\
	  builtin_define ("__XSCALE__");		\
	if (arm_arch_iwmmxt)				\
	  builtin_define ("__IWMMXT__");		\
	if (TARGET_AAPCS_BASED)				\
	  builtin_define ("__ARM_EABI__");		\
    } while (0)

/* The various ARM cores.  */
enum processor_type
{
#define ARM_CORE(NAME, IDENT, ARCH, FLAGS, COSTS) \
  IDENT,
#include "arm-cores.def"
#undef ARM_CORE
  /* Used to indicate that no processor has been specified.  */
  arm_none
};

enum target_cpus
{
#define ARM_CORE(NAME, IDENT, ARCH, FLAGS, COSTS) \
  TARGET_CPU_##IDENT,
#include "arm-cores.def"
#undef ARM_CORE
  TARGET_CPU_generic
};

/* The processor for which instructions should be scheduled.  */
extern enum processor_type arm_tune;

typedef enum arm_cond_code
{
  ARM_EQ = 0, ARM_NE, ARM_CS, ARM_CC, ARM_MI, ARM_PL, ARM_VS, ARM_VC,
  ARM_HI, ARM_LS, ARM_GE, ARM_LT, ARM_GT, ARM_LE, ARM_AL, ARM_NV
}
arm_cc;

extern arm_cc arm_current_cc;

#define ARM_INVERSE_CONDITION_CODE(X)  ((arm_cc) (((int)X) ^ 1))

extern int arm_target_label;
extern int arm_ccfsm_state;
extern GTY(()) rtx arm_target_insn;
/* Define the information needed to generate branch insns.  This is
   stored from the compare operation.  */
extern GTY(()) rtx arm_compare_op0;
extern GTY(()) rtx arm_compare_op1;
/* The label of the current constant pool.  */
extern rtx pool_vector_label;
/* Set to 1 when a return insn is output, this means that the epilogue
   is not needed.  */
extern int return_used_this_function;
/* Used to produce AOF syntax assembler.  */
extern GTY(()) rtx aof_pic_label;

/* Just in case configure has failed to define anything.  */
#ifndef TARGET_CPU_DEFAULT
#define TARGET_CPU_DEFAULT TARGET_CPU_generic
#endif


#undef  CPP_SPEC
#define CPP_SPEC "%(subtarget_cpp_spec)					\
%{msoft-float:%{mhard-float:						\
	%e-msoft-float and -mhard_float may not be used together}}	\
%{mbig-endian:%{mlittle-endian:						\
	%e-mbig-endian and -mlittle-endian may not be used together}}"

#ifndef CC1_SPEC
#define CC1_SPEC ""
#endif

/* This macro defines names of additional specifications to put in the specs
   that can be used in various specifications like CC1_SPEC.  Its definition
   is an initializer with a subgrouping for each command option.

   Each subgrouping contains a string constant, that defines the
   specification name, and a string constant that used by the GCC driver
   program.

   Do not define this macro if it does not need to do anything.  */
#define EXTRA_SPECS						\
  { "subtarget_cpp_spec",	SUBTARGET_CPP_SPEC },           \
  SUBTARGET_EXTRA_SPECS

#ifndef SUBTARGET_EXTRA_SPECS
#define SUBTARGET_EXTRA_SPECS
#endif

#ifndef SUBTARGET_CPP_SPEC
#define SUBTARGET_CPP_SPEC      ""
#endif

/* Run-time Target Specification.  */
#ifndef TARGET_VERSION
#define TARGET_VERSION fputs (" (ARM/generic)", stderr);
#endif

#define TARGET_SOFT_FLOAT		(arm_float_abi == ARM_FLOAT_ABI_SOFT)
/* Use hardware floating point instructions. */
#define TARGET_HARD_FLOAT		(arm_float_abi != ARM_FLOAT_ABI_SOFT)
/* Use hardware floating point calling convention.  */
#define TARGET_HARD_FLOAT_ABI		(arm_float_abi == ARM_FLOAT_ABI_HARD)
#define TARGET_FPA			(arm_fp_model == ARM_FP_MODEL_FPA)
#define TARGET_MAVERICK			(arm_fp_model == ARM_FP_MODEL_MAVERICK)
#define TARGET_VFP			(arm_fp_model == ARM_FP_MODEL_VFP)
#define TARGET_IWMMXT			(arm_arch_iwmmxt)
#define TARGET_REALLY_IWMMXT		(TARGET_IWMMXT && TARGET_ARM)
#define TARGET_IWMMXT_ABI (TARGET_ARM && arm_abi == ARM_ABI_IWMMXT)
#define TARGET_ARM                      (! TARGET_THUMB)
#define TARGET_EITHER			1 /* (TARGET_ARM | TARGET_THUMB) */
#define TARGET_BACKTRACE	        (leaf_function_p () \
				         ? TARGET_TPCS_LEAF_FRAME \
				         : TARGET_TPCS_FRAME)
#define TARGET_LDRD			(arm_arch5e && ARM_DOUBLEWORD_ALIGN)
#define TARGET_AAPCS_BASED \
    (arm_abi != ARM_ABI_APCS && arm_abi != ARM_ABI_ATPCS)

#define TARGET_HARD_TP			(target_thread_pointer == TP_CP15)
#define TARGET_SOFT_TP			(target_thread_pointer == TP_SOFT)

/* True iff the full BPABI is being used.  If TARGET_BPABI is true,
   then TARGET_AAPCS_BASED must be true -- but the converse does not
   hold.  TARGET_BPABI implies the use of the BPABI runtime library,
   etc., in addition to just the AAPCS calling conventions.  */
#ifndef TARGET_BPABI
#define TARGET_BPABI false
#endif

/* Support for a compile-time default CPU, et cetera.  The rules are:
   --with-arch is ignored if -march or -mcpu are specified.
   --with-cpu is ignored if -march or -mcpu are specified, and is overridden
    by --with-arch.
   --with-tune is ignored if -mtune or -mcpu are specified (but not affected
     by -march).
   --with-float is ignored if -mhard-float, -msoft-float or -mfloat-abi are
   specified.
   --with-fpu is ignored if -mfpu is specified.
   --with-abi is ignored is -mabi is specified.  */
#define OPTION_DEFAULT_SPECS \
  {"arch", "%{!march=*:%{!mcpu=*:-march=%(VALUE)}}" }, \
  {"cpu", "%{!march=*:%{!mcpu=*:-mcpu=%(VALUE)}}" }, \
  {"tune", "%{!mcpu=*:%{!mtune=*:-mtune=%(VALUE)}}" }, \
  {"float", \
    "%{!msoft-float:%{!mhard-float:%{!mfloat-abi=*:-mfloat-abi=%(VALUE)}}}" }, \
  {"fpu", "%{!mfpu=*:-mfpu=%(VALUE)}"}, \
  {"abi", "%{!mabi=*:-mabi=%(VALUE)}"}, \
  {"mode", "%{!marm:%{!mthumb:-m%(VALUE)}}"},

/* Which floating point model to use.  */
enum arm_fp_model
{
  ARM_FP_MODEL_UNKNOWN,
  /* FPA model (Hardware or software).  */
  ARM_FP_MODEL_FPA,
  /* Cirrus Maverick floating point model.  */
  ARM_FP_MODEL_MAVERICK,
  /* VFP floating point model.  */
  ARM_FP_MODEL_VFP
};

extern enum arm_fp_model arm_fp_model;

/* Which floating point hardware is available.  Also update
   fp_model_for_fpu in arm.c when adding entries to this list.  */
enum fputype
{
  /* No FP hardware.  */
  FPUTYPE_NONE,
  /* Full FPA support.  */
  FPUTYPE_FPA,
  /* Emulated FPA hardware, Issue 2 emulator (no LFM/SFM).  */
  FPUTYPE_FPA_EMU2,
  /* Emulated FPA hardware, Issue 3 emulator.  */
  FPUTYPE_FPA_EMU3,
  /* Cirrus Maverick floating point co-processor.  */
  FPUTYPE_MAVERICK,
  /* VFP.  */
  FPUTYPE_VFP
};

/* Recast the floating point class to be the floating point attribute.  */
#define arm_fpu_attr ((enum attr_fpu) arm_fpu_tune)

/* What type of floating point to tune for */
extern enum fputype arm_fpu_tune;

/* What type of floating point instructions are available */
extern enum fputype arm_fpu_arch;

enum float_abi_type
{
  ARM_FLOAT_ABI_SOFT,
  ARM_FLOAT_ABI_SOFTFP,
  ARM_FLOAT_ABI_HARD
};

extern enum float_abi_type arm_float_abi;

#ifndef TARGET_DEFAULT_FLOAT_ABI
#define TARGET_DEFAULT_FLOAT_ABI ARM_FLOAT_ABI_SOFT
#endif

/* Which ABI to use.  */
enum arm_abi_type
{
  ARM_ABI_APCS,
  ARM_ABI_ATPCS,
  ARM_ABI_AAPCS,
  ARM_ABI_IWMMXT,
  ARM_ABI_AAPCS_LINUX
};

extern enum arm_abi_type arm_abi;

#ifndef ARM_DEFAULT_ABI
#define ARM_DEFAULT_ABI ARM_ABI_APCS
#endif

/* Which thread pointer access sequence to use.  */
enum arm_tp_type {
  TP_AUTO,
  TP_SOFT,
  TP_CP15
};

extern enum arm_tp_type target_thread_pointer;

/* Nonzero if this chip supports the ARM Architecture 3M extensions.  */
extern int arm_arch3m;

/* Nonzero if this chip supports the ARM Architecture 4 extensions.  */
extern int arm_arch4;

/* Nonzero if this chip supports the ARM Architecture 4T extensions.  */
extern int arm_arch4t;

/* Nonzero if this chip supports the ARM Architecture 5 extensions.  */
extern int arm_arch5;

/* Nonzero if this chip supports the ARM Architecture 5E extensions.  */
extern int arm_arch5e;

/* Nonzero if this chip supports the ARM Architecture 6 extensions.  */
extern int arm_arch6;

/* Nonzero if this chip can benefit from load scheduling.  */
extern int arm_ld_sched;

/* Nonzero if generating thumb code.  */
extern int thumb_code;

/* Nonzero if this chip is a StrongARM.  */
extern int arm_tune_strongarm;

/* Nonzero if this chip is a Cirrus variant.  */
extern int arm_arch_cirrus;

/* Nonzero if this chip supports Intel XScale with Wireless MMX technology.  */
extern int arm_arch_iwmmxt;

/* Nonzero if this chip is an XScale.  */
extern int arm_arch_xscale;

/* Nonzero if tuning for XScale.  */
extern int arm_tune_xscale;

/* Nonzero if tuning for stores via the write buffer.  */
extern int arm_tune_wbuf;

/* Nonzero if we should define __THUMB_INTERWORK__ in the
   preprocessor.
   XXX This is a bit of a hack, it's intended to help work around
   problems in GLD which doesn't understand that armv5t code is
   interworking clean.  */
extern int arm_cpp_interwork;

#ifndef TARGET_DEFAULT
#define TARGET_DEFAULT  (MASK_APCS_FRAME)
#endif

/* The frame pointer register used in gcc has nothing to do with debugging;
   that is controlled by the APCS-FRAME option.  */
#define CAN_DEBUG_WITHOUT_FP

#define OVERRIDE_OPTIONS  arm_override_options ()

/* Nonzero if PIC code requires explicit qualifiers to generate
   PLT and GOT relocs rather than the assembler doing so implicitly.
   Subtargets can override these if required.  */
#ifndef NEED_GOT_RELOC
#define NEED_GOT_RELOC	0
#endif
#ifndef NEED_PLT_RELOC
#define NEED_PLT_RELOC	0
#endif

/* Nonzero if we need to refer to the GOT with a PC-relative
   offset.  In other words, generate

   .word	_GLOBAL_OFFSET_TABLE_ - [. - (.Lxx + 8)]

   rather than

   .word	_GLOBAL_OFFSET_TABLE_ - (.Lxx + 8)

   The default is true, which matches NetBSD.  Subtargets can
   override this if required.  */
#ifndef GOT_PCREL
#define GOT_PCREL   1
#endif

/* Target machine storage Layout.  */


/* Define this macro if it is advisable to hold scalars in registers
   in a wider mode than that declared by the program.  In such cases,
   the value is constrained to be within the bounds of the declared
   type, but kept valid in the wider mode.  The signedness of the
   extension may differ from that of the type.  */

/* It is far faster to zero extend chars than to sign extend them */

#define PROMOTE_MODE(MODE, UNSIGNEDP, TYPE)	\
  if (GET_MODE_CLASS (MODE) == MODE_INT		\
      && GET_MODE_SIZE (MODE) < 4)      	\
    {						\
      if (MODE == QImode)			\
	UNSIGNEDP = 1;				\
      else if (MODE == HImode)			\
	UNSIGNEDP = 1;				\
      (MODE) = SImode;				\
    }

#define PROMOTE_FUNCTION_MODE(MODE, UNSIGNEDP, TYPE)	\
  if ((GET_MODE_CLASS (MODE) == MODE_INT		\
       || GET_MODE_CLASS (MODE) == MODE_COMPLEX_INT)    \
      && GET_MODE_SIZE (MODE) < 4)                      \
    (MODE) = SImode;				        \

/* Define this if most significant bit is lowest numbered
   in instructions that operate on numbered bit-fields.  */
#define BITS_BIG_ENDIAN  0

/* Define this if most significant byte of a word is the lowest numbered.
   Most ARM processors are run in little endian mode, so that is the default.
   If you want to have it run-time selectable, change the definition in a
   cover file to be TARGET_BIG_ENDIAN.  */
#define BYTES_BIG_ENDIAN  (TARGET_BIG_END != 0)

/* Define this if most significant word of a multiword number is the lowest
   numbered.
   This is always false, even when in big-endian mode.  */
#define WORDS_BIG_ENDIAN  (BYTES_BIG_ENDIAN && ! TARGET_LITTLE_WORDS)

/* LIBGCC2_WORDS_BIG_ENDIAN has to be a constant, so we define this based
   on processor pre-defineds when compiling libgcc2.c.  */
#if defined(__ARMEB__) && !defined(__ARMWEL__)
#define LIBGCC2_WORDS_BIG_ENDIAN 1
#else
#define LIBGCC2_WORDS_BIG_ENDIAN 0
#endif

/* Define this if most significant word of doubles is the lowest numbered.
   The rules are different based on whether or not we use FPA-format,
   VFP-format or some other floating point co-processor's format doubles.  */
#define FLOAT_WORDS_BIG_ENDIAN (arm_float_words_big_endian ())

#define UNITS_PER_WORD	4

/* True if natural alignment is used for doubleword types.  */
#define ARM_DOUBLEWORD_ALIGN	TARGET_AAPCS_BASED

#define DOUBLEWORD_ALIGNMENT 64

#define PARM_BOUNDARY  	32

#define STACK_BOUNDARY  (ARM_DOUBLEWORD_ALIGN ? DOUBLEWORD_ALIGNMENT : 32)

#define PREFERRED_STACK_BOUNDARY \
    (arm_abi == ARM_ABI_ATPCS ? 64 : STACK_BOUNDARY)

#define FUNCTION_BOUNDARY  32

/* The lowest bit is used to indicate Thumb-mode functions, so the
   vbit must go into the delta field of pointers to member
   functions.  */
#define TARGET_PTRMEMFUNC_VBIT_LOCATION ptrmemfunc_vbit_in_delta

#define EMPTY_FIELD_BOUNDARY  32

#define BIGGEST_ALIGNMENT (ARM_DOUBLEWORD_ALIGN ? DOUBLEWORD_ALIGNMENT : 32)

/* XXX Blah -- this macro is used directly by libobjc.  Since it
   supports no vector modes, cut out the complexity and fall back
   on BIGGEST_FIELD_ALIGNMENT.  */
#ifdef IN_TARGET_LIBS
#define BIGGEST_FIELD_ALIGNMENT 64
#endif

/* Make strings word-aligned so strcpy from constants will be faster.  */
#define CONSTANT_ALIGNMENT_FACTOR (TARGET_THUMB || ! arm_tune_xscale ? 1 : 2)

#define CONSTANT_ALIGNMENT(EXP, ALIGN)				\
   ((TREE_CODE (EXP) == STRING_CST				\
     && (ALIGN) < BITS_PER_WORD * CONSTANT_ALIGNMENT_FACTOR)	\
    ? BITS_PER_WORD * CONSTANT_ALIGNMENT_FACTOR : (ALIGN))

/* Setting STRUCTURE_SIZE_BOUNDARY to 32 produces more efficient code, but the
   value set in previous versions of this toolchain was 8, which produces more
   compact structures.  The command line option -mstructure_size_boundary=<n>
   can be used to change this value.  For compatibility with the ARM SDK
   however the value should be left at 32.  ARM SDT Reference Manual (ARM DUI
   0020D) page 2-20 says "Structures are aligned on word boundaries".
   The AAPCS specifies a value of 8.  */
#define STRUCTURE_SIZE_BOUNDARY arm_structure_size_boundary
extern int arm_structure_size_boundary;

/* This is the value used to initialize arm_structure_size_boundary.  If a
   particular arm target wants to change the default value it should change
   the definition of this macro, not STRUCTURE_SIZE_BOUNDARY.  See netbsd.h
   for an example of this.  */
#ifndef DEFAULT_STRUCTURE_SIZE_BOUNDARY
#define DEFAULT_STRUCTURE_SIZE_BOUNDARY 32
#endif

/* Nonzero if move instructions will actually fail to work
   when given unaligned data.  */
#define STRICT_ALIGNMENT 1

/* wchar_t is unsigned under the AAPCS.  */
#ifndef WCHAR_TYPE
#define WCHAR_TYPE (TARGET_AAPCS_BASED ? "unsigned int" : "int")
#undef  WCHAR_TYPE_SIZE
#define WCHAR_TYPE_SIZE BITS_PER_WORD
#endif

#ifndef SIZE_TYPE
#define SIZE_TYPE (TARGET_AAPCS_BASED ? "unsigned int" : "long unsigned int")
#endif

#ifndef PTRDIFF_TYPE
#define PTRDIFF_TYPE (TARGET_AAPCS_BASED ? "int" : "long int")
#endif

/* AAPCS requires that structure alignment is affected by bitfields.  */
#ifndef PCC_BITFIELD_TYPE_MATTERS
#define PCC_BITFIELD_TYPE_MATTERS TARGET_AAPCS_BASED
#endif


/* Standard register usage.  */

/* Register allocation in ARM Procedure Call Standard (as used on RISCiX):
   (S - saved over call).

	r0	   *	argument word/integer result
	r1-r3		argument word

	r4-r8	     S	register variable
	r9	     S	(rfp) register variable (real frame pointer)

	r10  	   F S	(sl) stack limit (used by -mapcs-stack-check)
	r11 	   F S	(fp) argument pointer
	r12		(ip) temp workspace
	r13  	   F S	(sp) lower end of current stack frame
	r14		(lr) link address/workspace
	r15	   F	(pc) program counter

	f0		floating point result
	f1-f3		floating point scratch

	f4-f7	     S	floating point variable

	cc		This is NOT a real register, but is used internally
	                to represent things that use or set the condition
			codes.
	sfp             This isn't either.  It is used during rtl generation
	                since the offset between the frame pointer and the
			auto's isn't known until after register allocation.
	afp		Nor this, we only need this because of non-local
	                goto.  Without it fp appears to be used and the
			elimination code won't get rid of sfp.  It tracks
			fp exactly at all times.

   *: See CONDITIONAL_REGISTER_USAGE  */

/*
  	mvf0		Cirrus floating point result
	mvf1-mvf3	Cirrus floating point scratch
	mvf4-mvf15   S	Cirrus floating point variable.  */

/*	s0-s15		VFP scratch (aka d0-d7).
	s16-s31	      S	VFP variable (aka d8-d15).
	vfpcc		Not a real register.  Represents the VFP condition
			code flags.  */

/* The stack backtrace structure is as follows:
  fp points to here:  |  save code pointer  |      [fp]
                      |  return link value  |      [fp, #-4]
                      |  return sp value    |      [fp, #-8]
                      |  return fp value    |      [fp, #-12]
                     [|  saved r10 value    |]
                     [|  saved r9 value     |]
                     [|  saved r8 value     |]
                     [|  saved r7 value     |]
                     [|  saved r6 value     |]
                     [|  saved r5 value     |]
                     [|  saved r4 value     |]
                     [|  saved r3 value     |]
                     [|  saved r2 value     |]
                     [|  saved r1 value     |]
                     [|  saved r0 value     |]
                     [|  saved f7 value     |]     three words
                     [|  saved f6 value     |]     three words
                     [|  saved f5 value     |]     three words
                     [|  saved f4 value     |]     three words
  r0-r3 are not normally saved in a C function.  */

/* 1 for registers that have pervasive standard uses
   and are not available for the register allocator.  */
#define FIXED_REGISTERS \
{                       \
  0,0,0,0,0,0,0,0,	\
  0,0,0,0,0,1,0,1,	\
  0,0,0,0,0,0,0,0,	\
  1,1,1,		\
  1,1,1,1,1,1,1,1,	\
  1,1,1,1,1,1,1,1,	\
  1,1,1,1,1,1,1,1,	\
  1,1,1,1,1,1,1,1,	\
  1,1,1,1,		\
  1,1,1,1,1,1,1,1,	\
  1,1,1,1,1,1,1,1,	\
  1,1,1,1,1,1,1,1,	\
  1,1,1,1,1,1,1,1,	\
  1			\
}

/* 1 for registers not available across function calls.
   These must include the FIXED_REGISTERS and also any
   registers that can be used without being saved.
   The latter must include the registers where values are returned
   and the register where structure-value addresses are passed.
   Aside from that, you can include as many other registers as you like.
   The CC is not preserved over function calls on the ARM 6, so it is
   easier to assume this for all.  SFP is preserved, since FP is.  */
#define CALL_USED_REGISTERS  \
{                            \
  1,1,1,1,0,0,0,0,	     \
  0,0,0,0,1,1,1,1,	     \
  1,1,1,1,0,0,0,0,	     \
  1,1,1,		     \
  1,1,1,1,1,1,1,1,	     \
  1,1,1,1,1,1,1,1,	     \
  1,1,1,1,1,1,1,1,	     \
  1,1,1,1,1,1,1,1,	     \
  1,1,1,1,		     \
  1,1,1,1,1,1,1,1,	     \
  1,1,1,1,1,1,1,1,	     \
  1,1,1,1,1,1,1,1,	     \
  1,1,1,1,1,1,1,1,	     \
  1			     \
}

#ifndef SUBTARGET_CONDITIONAL_REGISTER_USAGE
#define SUBTARGET_CONDITIONAL_REGISTER_USAGE
#endif

#define CONDITIONAL_REGISTER_USAGE				\
{								\
  int regno;							\
								\
  if (TARGET_SOFT_FLOAT || TARGET_THUMB || !TARGET_FPA)		\
    {								\
      for (regno = FIRST_FPA_REGNUM;				\
	   regno <= LAST_FPA_REGNUM; ++regno)			\
	fixed_regs[regno] = call_used_regs[regno] = 1;		\
    }								\
								\
  if (TARGET_THUMB && optimize_size)				\
    {								\
      /* When optimizing for size, it's better not to use	\
	 the HI regs, because of the overhead of stacking 	\
	 them.  */						\
      for (regno = FIRST_HI_REGNUM;				\
	   regno <= LAST_HI_REGNUM; ++regno)			\
	fixed_regs[regno] = call_used_regs[regno] = 1;		\
    }								\
								\
  /* The link register can be clobbered by any branch insn,	\
     but we have no way to track that at present, so mark	\
     it as unavailable.  */					\
  if (TARGET_THUMB)						\
    fixed_regs[LR_REGNUM] = call_used_regs[LR_REGNUM] = 1;	\
								\
  if (TARGET_ARM && TARGET_HARD_FLOAT)				\
    {								\
      if (TARGET_MAVERICK)					\
	{							\
	  for (regno = FIRST_FPA_REGNUM;			\
	       regno <= LAST_FPA_REGNUM; ++ regno)		\
	    fixed_regs[regno] = call_used_regs[regno] = 1;	\
	  for (regno = FIRST_CIRRUS_FP_REGNUM;			\
	       regno <= LAST_CIRRUS_FP_REGNUM; ++ regno)	\
	    {							\
	      fixed_regs[regno] = 0;				\
	      call_used_regs[regno] = regno < FIRST_CIRRUS_FP_REGNUM + 4; \
	    }							\
	}							\
      if (TARGET_VFP)						\
	{							\
	  for (regno = FIRST_VFP_REGNUM;			\
	       regno <= LAST_VFP_REGNUM; ++ regno)		\
	    {							\
	      fixed_regs[regno] = 0;				\
	      call_used_regs[regno] = regno < FIRST_VFP_REGNUM + 16; \
	    }							\
	}							\
    }								\
								\
  if (TARGET_REALLY_IWMMXT)					\
    {								\
      regno = FIRST_IWMMXT_GR_REGNUM;				\
      /* The 2002/10/09 revision of the XScale ABI has wCG0     \
         and wCG1 as call-preserved registers.  The 2002/11/21  \
         revision changed this so that all wCG registers are    \
         scratch registers.  */					\
      for (regno = FIRST_IWMMXT_GR_REGNUM;			\
	   regno <= LAST_IWMMXT_GR_REGNUM; ++ regno)		\
	fixed_regs[regno] = 0;					\
      /* The XScale ABI has wR0 - wR9 as scratch registers,     \
	 the rest as call-preserved registers.  */		\
      for (regno = FIRST_IWMMXT_REGNUM;				\
	   regno <= LAST_IWMMXT_REGNUM; ++ regno)		\
	{							\
	  fixed_regs[regno] = 0;				\
	  call_used_regs[regno] = regno < FIRST_IWMMXT_REGNUM + 10; \
	}							\
    }								\
								\
  if ((unsigned) PIC_OFFSET_TABLE_REGNUM != INVALID_REGNUM)	\
    {								\
      fixed_regs[PIC_OFFSET_TABLE_REGNUM] = 1;			\
      call_used_regs[PIC_OFFSET_TABLE_REGNUM] = 1;		\
    }								\
  else if (TARGET_APCS_STACK)					\
    {								\
      fixed_regs[10]     = 1;					\
      call_used_regs[10] = 1;					\
    }								\
  /* -mcaller-super-interworking reserves r11 for calls to	\
     _interwork_r11_call_via_rN().  Making the register global	\
     is an easy way of ensuring that it remains valid for all	\
     calls.  */							\
  if (TARGET_APCS_FRAME || TARGET_CALLER_INTERWORKING		\
      || TARGET_TPCS_FRAME || TARGET_TPCS_LEAF_FRAME)		\
    {								\
      fixed_regs[ARM_HARD_FRAME_POINTER_REGNUM] = 1;		\
      call_used_regs[ARM_HARD_FRAME_POINTER_REGNUM] = 1;	\
      if (TARGET_CALLER_INTERWORKING)				\
	global_regs[ARM_HARD_FRAME_POINTER_REGNUM] = 1;		\
    }								\
  SUBTARGET_CONDITIONAL_REGISTER_USAGE				\
}

/* These are a couple of extensions to the formats accepted
   by asm_fprintf:
     %@ prints out ASM_COMMENT_START
     %r prints out REGISTER_PREFIX reg_names[arg]  */
#define ASM_FPRINTF_EXTENSIONS(FILE, ARGS, P)		\
  case '@':						\
    fputs (ASM_COMMENT_START, FILE);			\
    break;						\
							\
  case 'r':						\
    fputs (REGISTER_PREFIX, FILE);			\
    fputs (reg_names [va_arg (ARGS, int)], FILE);	\
    break;

/* Round X up to the nearest word.  */
#define ROUND_UP_WORD(X) (((X) + 3) & ~3)

/* Convert fron bytes to ints.  */
#define ARM_NUM_INTS(X) (((X) + UNITS_PER_WORD - 1) / UNITS_PER_WORD)

/* The number of (integer) registers required to hold a quantity of type MODE.
   Also used for VFP registers.  */
#define ARM_NUM_REGS(MODE)				\
  ARM_NUM_INTS (GET_MODE_SIZE (MODE))

/* The number of (integer) registers required to hold a quantity of TYPE MODE.  */
#define ARM_NUM_REGS2(MODE, TYPE)                   \
  ARM_NUM_INTS ((MODE) == BLKmode ? 		\
  int_size_in_bytes (TYPE) : GET_MODE_SIZE (MODE))

/* The number of (integer) argument register available.  */
#define NUM_ARG_REGS		4

/* Return the register number of the N'th (integer) argument.  */
#define ARG_REGISTER(N) 	(N - 1)

/* Specify the registers used for certain standard purposes.
   The values of these macros are register numbers.  */

/* The number of the last argument register.  */
#define LAST_ARG_REGNUM 	ARG_REGISTER (NUM_ARG_REGS)

/* The numbers of the Thumb register ranges.  */
#define FIRST_LO_REGNUM  	0
#define LAST_LO_REGNUM  	7
#define FIRST_HI_REGNUM		8
#define LAST_HI_REGNUM		11

#ifndef TARGET_UNWIND_INFO
/* We use sjlj exceptions for backwards compatibility.  */
#define MUST_USE_SJLJ_EXCEPTIONS 1
#endif

/* We can generate DWARF2 Unwind info, even though we don't use it.  */
#define DWARF2_UNWIND_INFO 1

/* Use r0 and r1 to pass exception handling information.  */
#define EH_RETURN_DATA_REGNO(N) (((N) < 2) ? N : INVALID_REGNUM)

/* The register that holds the return address in exception handlers.  */
#define ARM_EH_STACKADJ_REGNUM	2
#define EH_RETURN_STACKADJ_RTX	gen_rtx_REG (SImode, ARM_EH_STACKADJ_REGNUM)

/* The native (Norcroft) Pascal compiler for the ARM passes the static chain
   as an invisible last argument (possible since varargs don't exist in
   Pascal), so the following is not true.  */
#define STATIC_CHAIN_REGNUM	(TARGET_ARM ? 12 : 9)

/* Define this to be where the real frame pointer is if it is not possible to
   work out the offset between the frame pointer and the automatic variables
   until after register allocation has taken place.  FRAME_POINTER_REGNUM
   should point to a special register that we will make sure is eliminated.

   For the Thumb we have another problem.  The TPCS defines the frame pointer
   as r11, and GCC believes that it is always possible to use the frame pointer
   as base register for addressing purposes.  (See comments in
   find_reloads_address()).  But - the Thumb does not allow high registers,
   including r11, to be used as base address registers.  Hence our problem.

   The solution used here, and in the old thumb port is to use r7 instead of
   r11 as the hard frame pointer and to have special code to generate
   backtrace structures on the stack (if required to do so via a command line
   option) using r11.  This is the only 'user visible' use of r11 as a frame
   pointer.  */
#define ARM_HARD_FRAME_POINTER_REGNUM	11
#define THUMB_HARD_FRAME_POINTER_REGNUM	 7

#define HARD_FRAME_POINTER_REGNUM		\
  (TARGET_ARM					\
   ? ARM_HARD_FRAME_POINTER_REGNUM		\
   : THUMB_HARD_FRAME_POINTER_REGNUM)

#define FP_REGNUM	                HARD_FRAME_POINTER_REGNUM

/* Register to use for pushing function arguments.  */
#define STACK_POINTER_REGNUM	SP_REGNUM

/* ARM floating pointer registers.  */
#define FIRST_FPA_REGNUM 	16
#define LAST_FPA_REGNUM  	23
#define IS_FPA_REGNUM(REGNUM) \
  (((REGNUM) >= FIRST_FPA_REGNUM) && ((REGNUM) <= LAST_FPA_REGNUM))

#define FIRST_IWMMXT_GR_REGNUM	43
#define LAST_IWMMXT_GR_REGNUM	46
#define FIRST_IWMMXT_REGNUM	47
#define LAST_IWMMXT_REGNUM	62
#define IS_IWMMXT_REGNUM(REGNUM) \
  (((REGNUM) >= FIRST_IWMMXT_REGNUM) && ((REGNUM) <= LAST_IWMMXT_REGNUM))
#define IS_IWMMXT_GR_REGNUM(REGNUM) \
  (((REGNUM) >= FIRST_IWMMXT_GR_REGNUM) && ((REGNUM) <= LAST_IWMMXT_GR_REGNUM))

/* Base register for access to local variables of the function.  */
#define FRAME_POINTER_REGNUM	25

/* Base register for access to arguments of the function.  */
#define ARG_POINTER_REGNUM	26

#define FIRST_CIRRUS_FP_REGNUM	27
#define LAST_CIRRUS_FP_REGNUM	42
#define IS_CIRRUS_REGNUM(REGNUM) \
  (((REGNUM) >= FIRST_CIRRUS_FP_REGNUM) && ((REGNUM) <= LAST_CIRRUS_FP_REGNUM))

#define FIRST_VFP_REGNUM	63
#define LAST_VFP_REGNUM		94
#define IS_VFP_REGNUM(REGNUM) \
  (((REGNUM) >= FIRST_VFP_REGNUM) && ((REGNUM) <= LAST_VFP_REGNUM))

/* The number of hard registers is 16 ARM + 8 FPA + 1 CC + 1 SFP + 1 AFP.  */
/* + 16 Cirrus registers take us up to 43.  */
/* Intel Wireless MMX Technology registers add 16 + 4 more.  */
/* VFP adds 32 + 1 more.  */
#define FIRST_PSEUDO_REGISTER   96

#define DBX_REGISTER_NUMBER(REGNO) arm_dbx_register_number (REGNO)

/* Value should be nonzero if functions must have frame pointers.
   Zero means the frame pointer need not be set up (and parms may be accessed
   via the stack pointer) in functions that seem suitable.
   If we have to have a frame pointer we might as well make use of it.
   APCS says that the frame pointer does not need to be pushed in leaf
   functions, or simple tail call functions.  */

#ifndef SUBTARGET_FRAME_POINTER_REQUIRED
#define SUBTARGET_FRAME_POINTER_REQUIRED 0
#endif

#define FRAME_POINTER_REQUIRED					\
  (current_function_has_nonlocal_label				\
   || SUBTARGET_FRAME_POINTER_REQUIRED				\
   || (TARGET_ARM && TARGET_APCS_FRAME && ! leaf_function_p ()))

/* Return number of consecutive hard regs needed starting at reg REGNO
   to hold something of mode MODE.
   This is ordinarily the length in words of a value of mode MODE
   but can be less for certain modes in special long registers.

   On the ARM regs are UNITS_PER_WORD bits wide; FPA regs can hold any FP
   mode.  */
#define HARD_REGNO_NREGS(REGNO, MODE)  	\
  ((TARGET_ARM 				\
    && REGNO >= FIRST_FPA_REGNUM	\
    && REGNO != FRAME_POINTER_REGNUM	\
    && REGNO != ARG_POINTER_REGNUM)	\
    && !IS_VFP_REGNUM (REGNO)		\
   ? 1 : ARM_NUM_REGS (MODE))

/* Return true if REGNO is suitable for holding a quantity of type MODE.  */
#define HARD_REGNO_MODE_OK(REGNO, MODE)					\
  arm_hard_regno_mode_ok ((REGNO), (MODE))

/* Value is 1 if it is a good idea to tie two pseudo registers
   when one has mode MODE1 and one has mode MODE2.
   If HARD_REGNO_MODE_OK could produce different values for MODE1 and MODE2,
   for any hard reg, then this must be 0 for correct output.  */
#define MODES_TIEABLE_P(MODE1, MODE2)  \
  (GET_MODE_CLASS (MODE1) == GET_MODE_CLASS (MODE2))

#define VALID_IWMMXT_REG_MODE(MODE) \
 (arm_vector_mode_supported_p (MODE) || (MODE) == DImode)

/* The order in which register should be allocated.  It is good to use ip
   since no saving is required (though calls clobber it) and it never contains
   function parameters.  It is quite good to use lr since other calls may
   clobber it anyway.  Allocate r0 through r3 in reverse order since r3 is
   least likely to contain a function parameter; in addition results are
   returned in r0.  */

#define REG_ALLOC_ORDER  	    \
{                                   \
     3,  2,  1,  0, 12, 14,  4,  5, \
     6,  7,  8, 10,  9, 11, 13, 15, \
    16, 17, 18, 19, 20, 21, 22, 23, \
    27, 28, 29, 30, 31, 32, 33, 34, \
    35, 36, 37, 38, 39, 40, 41, 42, \
    43, 44, 45, 46, 47, 48, 49, 50, \
    51, 52, 53, 54, 55, 56, 57, 58, \
    59, 60, 61, 62,		    \
    24, 25, 26,			    \
    78, 77, 76, 75, 74, 73, 72, 71, \
    70, 69, 68, 67, 66, 65, 64, 63, \
    79, 80, 81, 82, 83, 84, 85, 86, \
    87, 88, 89, 90, 91, 92, 93, 94, \
    95				    \
}

/* Interrupt functions can only use registers that have already been
   saved by the prologue, even if they would normally be
   call-clobbered.  */
#define HARD_REGNO_RENAME_OK(SRC, DST)					\
	(! IS_INTERRUPT (cfun->machine->func_type) ||			\
		regs_ever_live[DST])

/* Register and constant classes.  */

/* Register classes: used to be simple, just all ARM regs or all FPA regs
   Now that the Thumb is involved it has become more complicated.  */
enum reg_class
{
  NO_REGS,
  FPA_REGS,
  CIRRUS_REGS,
  VFP_REGS,
  IWMMXT_GR_REGS,
  IWMMXT_REGS,
  LO_REGS,
  STACK_REG,
  BASE_REGS,
  HI_REGS,
  CC_REG,
  VFPCC_REG,
  GENERAL_REGS,
  ALL_REGS,
  LIM_REG_CLASSES
};

#define N_REG_CLASSES  (int) LIM_REG_CLASSES

/* Give names of register classes as strings for dump file.  */
#define REG_CLASS_NAMES  \
{			\
  "NO_REGS",		\
  "FPA_REGS",		\
  "CIRRUS_REGS",	\
  "VFP_REGS",		\
  "IWMMXT_GR_REGS",	\
  "IWMMXT_REGS",	\
  "LO_REGS",		\
  "STACK_REG",		\
  "BASE_REGS",		\
  "HI_REGS",		\
  "CC_REG",		\
  "VFPCC_REG",		\
  "GENERAL_REGS",	\
  "ALL_REGS",		\
}

/* Define which registers fit in which classes.
   This is an initializer for a vector of HARD_REG_SET
   of length N_REG_CLASSES.  */
#define REG_CLASS_CONTENTS					\
{								\
  { 0x00000000, 0x00000000, 0x00000000 }, /* NO_REGS  */	\
  { 0x00FF0000, 0x00000000, 0x00000000 }, /* FPA_REGS */	\
  { 0xF8000000, 0x000007FF, 0x00000000 }, /* CIRRUS_REGS */	\
  { 0x00000000, 0x80000000, 0x7FFFFFFF }, /* VFP_REGS  */	\
  { 0x00000000, 0x00007800, 0x00000000 }, /* IWMMXT_GR_REGS */	\
  { 0x00000000, 0x7FFF8000, 0x00000000 }, /* IWMMXT_REGS */	\
  { 0x000000FF, 0x00000000, 0x00000000 }, /* LO_REGS */		\
  { 0x00002000, 0x00000000, 0x00000000 }, /* STACK_REG */	\
  { 0x000020FF, 0x00000000, 0x00000000 }, /* BASE_REGS */	\
  { 0x0000FF00, 0x00000000, 0x00000000 }, /* HI_REGS */		\
  { 0x01000000, 0x00000000, 0x00000000 }, /* CC_REG */		\
  { 0x00000000, 0x00000000, 0x80000000 }, /* VFPCC_REG */	\
  { 0x0200FFFF, 0x00000000, 0x00000000 }, /* GENERAL_REGS */	\
  { 0xFAFFFFFF, 0xFFFFFFFF, 0x7FFFFFFF }  /* ALL_REGS */	\
}

/* The same information, inverted:
   Return the class number of the smallest class containing
   reg number REGNO.  This could be a conditional expression
   or could index an array.  */
#define REGNO_REG_CLASS(REGNO)  arm_regno_class (REGNO)

/* FPA registers can't do subreg as all values are reformatted to internal
   precision.  VFP registers may only be accessed in the mode they
   were set.  */
#define CANNOT_CHANGE_MODE_CLASS(FROM, TO, CLASS)	\
  (GET_MODE_SIZE (FROM) != GET_MODE_SIZE (TO)		\
   ? reg_classes_intersect_p (FPA_REGS, (CLASS))	\
     || reg_classes_intersect_p (VFP_REGS, (CLASS))	\
   : 0)

/* We need to define this for LO_REGS on thumb.  Otherwise we can end up
   using r0-r4 for function arguments, r7 for the stack frame and don't
   have enough left over to do doubleword arithmetic.  */
#define CLASS_LIKELY_SPILLED_P(CLASS)	\
    ((TARGET_THUMB && (CLASS) == LO_REGS)	\
     || (CLASS) == CC_REG)

/* The class value for index registers, and the one for base regs.  */
#define INDEX_REG_CLASS  (TARGET_THUMB ? LO_REGS : GENERAL_REGS)
#define BASE_REG_CLASS   (TARGET_THUMB ? LO_REGS : GENERAL_REGS)

/* For the Thumb the high registers cannot be used as base registers
   when addressing quantities in QI or HI mode; if we don't know the
   mode, then we must be conservative.  */
#define MODE_BASE_REG_CLASS(MODE)					\
    (TARGET_ARM ? GENERAL_REGS :					\
     (((MODE) == SImode) ? BASE_REGS : LO_REGS))

/* For Thumb we can not support SP+reg addressing, so we return LO_REGS
   instead of BASE_REGS.  */
#define MODE_BASE_REG_REG_CLASS(MODE) BASE_REG_CLASS

/* When SMALL_REGISTER_CLASSES is nonzero, the compiler allows
   registers explicitly used in the rtl to be used as spill registers
   but prevents the compiler from extending the lifetime of these
   registers.  */
#define SMALL_REGISTER_CLASSES   TARGET_THUMB

/* Given an rtx X being reloaded into a reg required to be
   in class CLASS, return the class of reg to actually use.
   In general this is just CLASS, but for the Thumb we prefer
   a LO_REGS class or a subset.  */
#define PREFERRED_RELOAD_CLASS(X, CLASS)	\
  (TARGET_ARM ? (CLASS) :			\
   ((CLASS) == BASE_REGS ? (CLASS) : LO_REGS))

/* Must leave BASE_REGS reloads alone */
#define THUMB_SECONDARY_INPUT_RELOAD_CLASS(CLASS, MODE, X)		\
  ((CLASS) != LO_REGS && (CLASS) != BASE_REGS				\
   ? ((true_regnum (X) == -1 ? LO_REGS					\
       : (true_regnum (X) + HARD_REGNO_NREGS (0, MODE) > 8) ? LO_REGS	\
       : NO_REGS)) 							\
   : NO_REGS)

#define THUMB_SECONDARY_OUTPUT_RELOAD_CLASS(CLASS, MODE, X)		\
  ((CLASS) != LO_REGS && (CLASS) != BASE_REGS				\
   ? ((true_regnum (X) == -1 ? LO_REGS					\
       : (true_regnum (X) + HARD_REGNO_NREGS (0, MODE) > 8) ? LO_REGS	\
       : NO_REGS)) 							\
   : NO_REGS)

/* Return the register class of a scratch register needed to copy IN into
   or out of a register in CLASS in MODE.  If it can be done directly,
   NO_REGS is returned.  */
#define SECONDARY_OUTPUT_RELOAD_CLASS(CLASS, MODE, X)		\
  /* Restrict which direct reloads are allowed for VFP/iWMMXt regs.  */ \
  ((TARGET_VFP && TARGET_HARD_FLOAT				\
    && (CLASS) == VFP_REGS)					\
   ? coproc_secondary_reload_class (MODE, X, FALSE)		\
   : (TARGET_IWMMXT && (CLASS) == IWMMXT_REGS)			\
   ? coproc_secondary_reload_class (MODE, X, TRUE)		\
   : TARGET_ARM							\
   ? (((MODE) == HImode && ! arm_arch4 && true_regnum (X) == -1) \
    ? GENERAL_REGS : NO_REGS)					\
   : THUMB_SECONDARY_OUTPUT_RELOAD_CLASS (CLASS, MODE, X))

/* If we need to load shorts byte-at-a-time, then we need a scratch.  */
#define SECONDARY_INPUT_RELOAD_CLASS(CLASS, MODE, X)		\
  /* Restrict which direct reloads are allowed for VFP/iWMMXt regs.  */ \
  ((TARGET_VFP && TARGET_HARD_FLOAT				\
    && (CLASS) == VFP_REGS)					\
    ? coproc_secondary_reload_class (MODE, X, FALSE) :		\
    (TARGET_IWMMXT && (CLASS) == IWMMXT_REGS) ?			\
    coproc_secondary_reload_class (MODE, X, TRUE) :		\
  /* Cannot load constants into Cirrus registers.  */		\
   (TARGET_MAVERICK && TARGET_HARD_FLOAT			\
     && (CLASS) == CIRRUS_REGS					\
     && (CONSTANT_P (X) || GET_CODE (X) == SYMBOL_REF))		\
    ? GENERAL_REGS :						\
  (TARGET_ARM ?							\
   (((CLASS) == IWMMXT_REGS || (CLASS) == IWMMXT_GR_REGS)	\
      && CONSTANT_P (X))					\
   ? GENERAL_REGS :						\
   (((MODE) == HImode && ! arm_arch4				\
     && (GET_CODE (X) == MEM					\
	 || ((GET_CODE (X) == REG || GET_CODE (X) == SUBREG)	\
	     && true_regnum (X) == -1)))			\
    ? GENERAL_REGS : NO_REGS)					\
   : THUMB_SECONDARY_INPUT_RELOAD_CLASS (CLASS, MODE, X)))

/* Try a machine-dependent way of reloading an illegitimate address
   operand.  If we find one, push the reload and jump to WIN.  This
   macro is used in only one place: `find_reloads_address' in reload.c.

   For the ARM, we wish to handle large displacements off a base
   register by splitting the addend across a MOV and the mem insn.
   This can cut the number of reloads needed.  */
#define ARM_LEGITIMIZE_RELOAD_ADDRESS(X, MODE, OPNUM, TYPE, IND, WIN)	   \
  do									   \
    {									   \
      if (GET_CODE (X) == PLUS						   \
	  && GET_CODE (XEXP (X, 0)) == REG				   \
	  && REGNO (XEXP (X, 0)) < FIRST_PSEUDO_REGISTER		   \
	  && REG_MODE_OK_FOR_BASE_P (XEXP (X, 0), MODE)			   \
	  && GET_CODE (XEXP (X, 1)) == CONST_INT)			   \
	{								   \
	  HOST_WIDE_INT val = INTVAL (XEXP (X, 1));			   \
	  HOST_WIDE_INT low, high;					   \
									   \
	  if (MODE == DImode || (MODE == DFmode && TARGET_SOFT_FLOAT))	   \
	    low = ((val & 0xf) ^ 0x8) - 0x8;				   \
	  else if (TARGET_MAVERICK && TARGET_HARD_FLOAT)		   \
	    /* Need to be careful, -256 is not a valid offset.  */	   \
	    low = val >= 0 ? (val & 0xff) : -((-val) & 0xff);		   \
	  else if (MODE == SImode					   \
		   || (MODE == SFmode && TARGET_SOFT_FLOAT)		   \
		   || ((MODE == HImode || MODE == QImode) && ! arm_arch4)) \
	    /* Need to be careful, -4096 is not a valid offset.  */	   \
	    low = val >= 0 ? (val & 0xfff) : -((-val) & 0xfff);		   \
	  else if ((MODE == HImode || MODE == QImode) && arm_arch4)	   \
	    /* Need to be careful, -256 is not a valid offset.  */	   \
	    low = val >= 0 ? (val & 0xff) : -((-val) & 0xff);		   \
	  else if (GET_MODE_CLASS (MODE) == MODE_FLOAT			   \
		   && TARGET_HARD_FLOAT && TARGET_FPA)			   \
	    /* Need to be careful, -1024 is not a valid offset.  */	   \
	    low = val >= 0 ? (val & 0x3ff) : -((-val) & 0x3ff);		   \
	  else								   \
	    break;							   \
									   \
	  high = ((((val - low) & (unsigned HOST_WIDE_INT) 0xffffffff)	   \
		   ^ (unsigned HOST_WIDE_INT) 0x80000000)		   \
		  - (unsigned HOST_WIDE_INT) 0x80000000);		   \
	  /* Check for overflow or zero */				   \
	  if (low == 0 || high == 0 || (high + low != val))		   \
	    break;							   \
									   \
	  /* Reload the high part into a base reg; leave the low part	   \
	     in the mem.  */						   \
	  X = gen_rtx_PLUS (GET_MODE (X),				   \
			    gen_rtx_PLUS (GET_MODE (X), XEXP (X, 0),	   \
					  GEN_INT (high)),		   \
			    GEN_INT (low));				   \
	  push_reload (XEXP (X, 0), NULL_RTX, &XEXP (X, 0), NULL,	   \
		       MODE_BASE_REG_CLASS (MODE), GET_MODE (X), 	   \
		       VOIDmode, 0, 0, OPNUM, TYPE);			   \
	  goto WIN;							   \
	}								   \
    }									   \
  while (0)

/* XXX If an HImode FP+large_offset address is converted to an HImode
   SP+large_offset address, then reload won't know how to fix it.  It sees
   only that SP isn't valid for HImode, and so reloads the SP into an index
   register, but the resulting address is still invalid because the offset
   is too big.  We fix it here instead by reloading the entire address.  */
/* We could probably achieve better results by defining PROMOTE_MODE to help
   cope with the variances between the Thumb's signed and unsigned byte and
   halfword load instructions.  */
#define THUMB_LEGITIMIZE_RELOAD_ADDRESS(X, MODE, OPNUM, TYPE, IND_L, WIN)     \
do {									      \
  rtx new_x = thumb_legitimize_reload_address (&X, MODE, OPNUM, TYPE, IND_L); \
  if (new_x)								      \
    {									      \
      X = new_x;							      \
      goto WIN;								      \
    }									      \
} while (0)

#define LEGITIMIZE_RELOAD_ADDRESS(X, MODE, OPNUM, TYPE, IND_LEVELS, WIN)   \
  if (TARGET_ARM)							   \
    ARM_LEGITIMIZE_RELOAD_ADDRESS (X, MODE, OPNUM, TYPE, IND_LEVELS, WIN); \
  else									   \
    THUMB_LEGITIMIZE_RELOAD_ADDRESS (X, MODE, OPNUM, TYPE, IND_LEVELS, WIN)

/* Return the maximum number of consecutive registers
   needed to represent mode MODE in a register of class CLASS.
   ARM regs are UNITS_PER_WORD bits while FPA regs can hold any FP mode */
#define CLASS_MAX_NREGS(CLASS, MODE)  \
  (((CLASS) == FPA_REGS || (CLASS) == CIRRUS_REGS) ? 1 : ARM_NUM_REGS (MODE))

/* If defined, gives a class of registers that cannot be used as the
   operand of a SUBREG that changes the mode of the object illegally.  */

/* Moves between FPA_REGS and GENERAL_REGS are two memory insns.  */
#define REGISTER_MOVE_COST(MODE, FROM, TO)		\
  (TARGET_ARM ?						\
   ((FROM) == FPA_REGS && (TO) != FPA_REGS ? 20 :	\
    (FROM) != FPA_REGS && (TO) == FPA_REGS ? 20 :	\
    (FROM) == VFP_REGS && (TO) != VFP_REGS ? 10 :  \
    (FROM) != VFP_REGS && (TO) == VFP_REGS ? 10 :  \
    (FROM) == IWMMXT_REGS && (TO) != IWMMXT_REGS ? 4 :  \
    (FROM) != IWMMXT_REGS && (TO) == IWMMXT_REGS ? 4 :  \
    (FROM) == IWMMXT_GR_REGS || (TO) == IWMMXT_GR_REGS ? 20 :  \
    (FROM) == CIRRUS_REGS && (TO) != CIRRUS_REGS ? 20 :	\
    (FROM) != CIRRUS_REGS && (TO) == CIRRUS_REGS ? 20 :	\
   2)							\
   :							\
   ((FROM) == HI_REGS || (TO) == HI_REGS) ? 4 : 2)

/* Stack layout; function entry, exit and calling.  */

/* Define this if pushing a word on the stack
   makes the stack pointer a smaller address.  */
#define STACK_GROWS_DOWNWARD  1

/* Define this to nonzero if the nominal address of the stack frame
   is at the high-address end of the local variables;
   that is, each additional local variable allocated
   goes at a more negative offset in the frame.  */
#define FRAME_GROWS_DOWNWARD 1

/* The amount of scratch space needed by _interwork_{r7,r11}_call_via_rN().
   When present, it is one word in size, and sits at the top of the frame,
   between the soft frame pointer and either r7 or r11.

   We only need _interwork_rM_call_via_rN() for -mcaller-super-interworking,
   and only then if some outgoing arguments are passed on the stack.  It would
   be tempting to also check whether the stack arguments are passed by indirect
   calls, but there seems to be no reason in principle why a post-reload pass
   couldn't convert a direct call into an indirect one.  */
#define CALLER_INTERWORKING_SLOT_SIZE			\
  (TARGET_CALLER_INTERWORKING				\
   && current_function_outgoing_args_size != 0		\
   ? UNITS_PER_WORD : 0)

/* Offset within stack frame to start allocating local variables at.
   If FRAME_GROWS_DOWNWARD, this is the offset to the END of the
   first local allocated.  Otherwise, it is the offset to the BEGINNING
   of the first local allocated.  */
#define STARTING_FRAME_OFFSET  0

/* If we generate an insn to push BYTES bytes,
   this says how many the stack pointer really advances by.  */
/* The push insns do not do this rounding implicitly.
   So don't define this.  */
/* #define PUSH_ROUNDING(NPUSHED)  ROUND_UP_WORD (NPUSHED) */

/* Define this if the maximum size of all the outgoing args is to be
   accumulated and pushed during the prologue.  The amount can be
   found in the variable current_function_outgoing_args_size.  */
#define ACCUMULATE_OUTGOING_ARGS 1

/* Offset of first parameter from the argument pointer register value.  */
#define FIRST_PARM_OFFSET(FNDECL)  (TARGET_ARM ? 4 : 0)

/* Value is the number of byte of arguments automatically
   popped when returning from a subroutine call.
   FUNDECL is the declaration node of the function (as a tree),
   FUNTYPE is the data type of the function (as a tree),
   or for a library call it is an identifier node for the subroutine name.
   SIZE is the number of bytes of arguments passed on the stack.

   On the ARM, the caller does not pop any of its arguments that were passed
   on the stack.  */
#define RETURN_POPS_ARGS(FUNDECL, FUNTYPE, SIZE)  0

/* Define how to find the value returned by a library function
   assuming the value has mode MODE.  */
#define LIBCALL_VALUE(MODE)  \
  (TARGET_ARM && TARGET_HARD_FLOAT_ABI && TARGET_FPA			\
   && GET_MODE_CLASS (MODE) == MODE_FLOAT				\
   ? gen_rtx_REG (MODE, FIRST_FPA_REGNUM)				\
   : TARGET_ARM && TARGET_HARD_FLOAT_ABI && TARGET_MAVERICK		\
     && GET_MODE_CLASS (MODE) == MODE_FLOAT				\
   ? gen_rtx_REG (MODE, FIRST_CIRRUS_FP_REGNUM) 			\
   : TARGET_IWMMXT_ABI && arm_vector_mode_supported_p (MODE)    	\
   ? gen_rtx_REG (MODE, FIRST_IWMMXT_REGNUM) 				\
   : gen_rtx_REG (MODE, ARG_REGISTER (1)))

/* Define how to find the value returned by a function.
   VALTYPE is the data type of the value (as a tree).
   If the precise function being called is known, FUNC is its FUNCTION_DECL;
   otherwise, FUNC is 0.  */
#define FUNCTION_VALUE(VALTYPE, FUNC) \
  arm_function_value (VALTYPE, FUNC);

/* 1 if N is a possible register number for a function value.
   On the ARM, only r0 and f0 can return results.  */
/* On a Cirrus chip, mvf0 can return results.  */
#define FUNCTION_VALUE_REGNO_P(REGNO)  \
  ((REGNO) == ARG_REGISTER (1) \
   || (TARGET_ARM && ((REGNO) == FIRST_CIRRUS_FP_REGNUM)		\
       && TARGET_HARD_FLOAT_ABI && TARGET_MAVERICK)			\
   || ((REGNO) == FIRST_IWMMXT_REGNUM && TARGET_IWMMXT_ABI) \
   || (TARGET_ARM && ((REGNO) == FIRST_FPA_REGNUM)			\
       && TARGET_HARD_FLOAT_ABI && TARGET_FPA))

/* Amount of memory needed for an untyped call to save all possible return
   registers.  */
#define APPLY_RESULT_SIZE arm_apply_result_size()

/* How large values are returned */
/* A C expression which can inhibit the returning of certain function values
   in registers, based on the type of value.  */
#define RETURN_IN_MEMORY(TYPE) arm_return_in_memory (TYPE)

/* Define DEFAULT_PCC_STRUCT_RETURN to 1 if all structure and union return
   values must be in memory.  On the ARM, they need only do so if larger
   than a word, or if they contain elements offset from zero in the struct.  */
#define DEFAULT_PCC_STRUCT_RETURN 0

/* Flags for the call/call_value rtl operations set up by function_arg.  */
#define CALL_NORMAL		0x00000000	/* No special processing.  */
#define CALL_LONG		0x00000001	/* Always call indirect.  */
#define CALL_SHORT		0x00000002	/* Never call indirect.  */

/* These bits describe the different types of function supported
   by the ARM backend.  They are exclusive.  i.e. a function cannot be both a
   normal function and an interworked function, for example.  Knowing the
   type of a function is important for determining its prologue and
   epilogue sequences.
   Note value 7 is currently unassigned.  Also note that the interrupt
   function types all have bit 2 set, so that they can be tested for easily.
   Note that 0 is deliberately chosen for ARM_FT_UNKNOWN so that when the
   machine_function structure is initialized (to zero) func_type will
   default to unknown.  This will force the first use of arm_current_func_type
   to call arm_compute_func_type.  */
#define ARM_FT_UNKNOWN		 0 /* Type has not yet been determined.  */
#define ARM_FT_NORMAL		 1 /* Your normal, straightforward function.  */
#define ARM_FT_INTERWORKED	 2 /* A function that supports interworking.  */
#define ARM_FT_ISR		 4 /* An interrupt service routine.  */
#define ARM_FT_FIQ		 5 /* A fast interrupt service routine.  */
#define ARM_FT_EXCEPTION	 6 /* An ARM exception handler (subcase of ISR).  */

#define ARM_FT_TYPE_MASK	((1 << 3) - 1)

/* In addition functions can have several type modifiers,
   outlined by these bit masks:  */
#define ARM_FT_INTERRUPT	(1 << 2) /* Note overlap with FT_ISR and above.  */
#define ARM_FT_NAKED		(1 << 3) /* No prologue or epilogue.  */
#define ARM_FT_VOLATILE		(1 << 4) /* Does not return.  */
#define ARM_FT_NESTED		(1 << 5) /* Embedded inside another func.  */

/* Some macros to test these flags.  */
#define ARM_FUNC_TYPE(t)	(t & ARM_FT_TYPE_MASK)
#define IS_INTERRUPT(t)		(t & ARM_FT_INTERRUPT)
#define IS_VOLATILE(t)     	(t & ARM_FT_VOLATILE)
#define IS_NAKED(t)        	(t & ARM_FT_NAKED)
#define IS_NESTED(t)       	(t & ARM_FT_NESTED)


/* Structure used to hold the function stack frame layout.  Offsets are
   relative to the stack pointer on function entry.  Positive offsets are
   in the direction of stack growth.
   Only soft_frame is used in thumb mode.  */

typedef struct arm_stack_offsets GTY(())
{
  int saved_args;	/* ARG_POINTER_REGNUM.  */
  int frame;		/* ARM_HARD_FRAME_POINTER_REGNUM.  */
  int saved_regs;
  int soft_frame;	/* FRAME_POINTER_REGNUM.  */
  int locals_base;	/* THUMB_HARD_FRAME_POINTER_REGNUM.  */
  int outgoing_args;	/* STACK_POINTER_REGNUM.  */
}
arm_stack_offsets;

/* A C structure for machine-specific, per-function data.
   This is added to the cfun structure.  */
typedef struct machine_function GTY(())
{
  /* Additional stack adjustment in __builtin_eh_throw.  */
  rtx eh_epilogue_sp_ofs;
  /* Records if LR has to be saved for far jumps.  */
  int far_jump_used;
  /* Records if ARG_POINTER was ever live.  */
  int arg_pointer_live;
  /* Records if the save of LR has been eliminated.  */
  int lr_save_eliminated;
  /* The size of the stack frame.  Only valid after reload.  */
  arm_stack_offsets stack_offsets;
  /* Records the type of the current function.  */
  unsigned long func_type;
  /* Record if the function has a variable argument list.  */
  int uses_anonymous_args;
  /* Records if sibcalls are blocked because an argument
     register is needed to preserve stack alignment.  */
  int sibcall_blocked;
  /* The PIC register for this function.  This might be a pseudo.  */
  rtx pic_reg;
  /* Labels for per-function Thumb call-via stubs.  One per potential calling
     register.  We can never call via LR or PC.  We can call via SP if a
     trampoline happens to be on the top of the stack.  */
  rtx call_via[14];
}
machine_function;

/* As in the machine_function, a global set of call-via labels, for code 
   that is in text_section.  */
extern GTY(()) rtx thumb_call_via_label[14];

/* A C type for declaring a variable that is used as the first argument of
   `FUNCTION_ARG' and other related values.  For some target machines, the
   type `int' suffices and can hold the number of bytes of argument so far.  */
typedef struct
{
  /* This is the number of registers of arguments scanned so far.  */
  int nregs;
  /* This is the number of iWMMXt register arguments scanned so far.  */
  int iwmmxt_nregs;
  int named_count;
  int nargs;
  /* One of CALL_NORMAL, CALL_LONG or CALL_SHORT.  */
  int call_cookie;
  int can_split;
} CUMULATIVE_ARGS;

/* Define where to put the arguments to a function.
   Value is zero to push the argument on the stack,
   or a hard register in which to store the argument.

   MODE is the argument's machine mode.
   TYPE is the data type of the argument (as a tree).
    This is null for libcalls where that information may
    not be available.
   CUM is a variable of type CUMULATIVE_ARGS which gives info about
    the preceding args and about the function being called.
   NAMED is nonzero if this argument is a named parameter
    (otherwise it is an extra parameter matching an ellipsis).

   On the ARM, normally the first 16 bytes are passed in registers r0-r3; all
   other arguments are passed on the stack.  If (NAMED == 0) (which happens
   only in assign_parms, since TARGET_SETUP_INCOMING_VARARGS is
   defined), say it is passed in the stack (function_prologue will
   indeed make it pass in the stack if necessary).  */
#define FUNCTION_ARG(CUM, MODE, TYPE, NAMED) \
  arm_function_arg (&(CUM), (MODE), (TYPE), (NAMED))

#define FUNCTION_ARG_PADDING(MODE, TYPE) \
  (arm_pad_arg_upward (MODE, TYPE) ? upward : downward)

#define BLOCK_REG_PADDING(MODE, TYPE, FIRST) \
  (arm_pad_reg_upward (MODE, TYPE, FIRST) ? upward : downward)

/* For AAPCS, padding should never be below the argument. For other ABIs,
 * mimic the default.  */
#define PAD_VARARGS_DOWN \
  ((TARGET_AAPCS_BASED) ? 0 : BYTES_BIG_ENDIAN)

/* Initialize a variable CUM of type CUMULATIVE_ARGS
   for a call to a function whose data type is FNTYPE.
   For a library call, FNTYPE is 0.
   On the ARM, the offset starts at 0.  */
#define INIT_CUMULATIVE_ARGS(CUM, FNTYPE, LIBNAME, FNDECL, N_NAMED_ARGS) \
  arm_init_cumulative_args (&(CUM), (FNTYPE), (LIBNAME), (FNDECL))

/* Update the data in CUM to advance over an argument
   of mode MODE and data type TYPE.
   (TYPE is null for libcalls where that information may not be available.)  */
#define FUNCTION_ARG_ADVANCE(CUM, MODE, TYPE, NAMED)	\
  (CUM).nargs += 1;					\
  if (arm_vector_mode_supported_p (MODE)	       	\
      && (CUM).named_count > (CUM).nargs)		\
    (CUM).iwmmxt_nregs += 1;				\
  else							\
    (CUM).nregs += ARM_NUM_REGS2 (MODE, TYPE)

/* If defined, a C expression that gives the alignment boundary, in bits, of an
   argument with the specified mode and type.  If it is not defined,
   `PARM_BOUNDARY' is used for all arguments.  */
#define FUNCTION_ARG_BOUNDARY(MODE,TYPE) \
   ((ARM_DOUBLEWORD_ALIGN && arm_needs_doubleword_align (MODE, TYPE)) \
   ? DOUBLEWORD_ALIGNMENT \
   : PARM_BOUNDARY )

/* 1 if N is a possible register number for function argument passing.
   On the ARM, r0-r3 are used to pass args.  */
#define FUNCTION_ARG_REGNO_P(REGNO)	\
   (IN_RANGE ((REGNO), 0, 3)		\
    || (TARGET_IWMMXT_ABI		\
	&& IN_RANGE ((REGNO), FIRST_IWMMXT_REGNUM, FIRST_IWMMXT_REGNUM + 9)))


/* If your target environment doesn't prefix user functions with an
   underscore, you may wish to re-define this to prevent any conflicts.
   e.g. AOF may prefix mcount with an underscore.  */
#ifndef ARM_MCOUNT_NAME
#define ARM_MCOUNT_NAME "*mcount"
#endif

/* Call the function profiler with a given profile label.  The Acorn
   compiler puts this BEFORE the prolog but gcc puts it afterwards.
   On the ARM the full profile code will look like:
	.data
	LP1
		.word	0
	.text
		mov	ip, lr
		bl	mcount
		.word	LP1

   profile_function() in final.c outputs the .data section, FUNCTION_PROFILER
   will output the .text section.

   The ``mov ip,lr'' seems like a good idea to stick with cc convention.
   ``prof'' doesn't seem to mind about this!

   Note - this version of the code is designed to work in both ARM and
   Thumb modes.  */
#ifndef ARM_FUNCTION_PROFILER
#define ARM_FUNCTION_PROFILER(STREAM, LABELNO)  	\
{							\
  char temp[20];					\
  rtx sym;						\
							\
  asm_fprintf (STREAM, "\tmov\t%r, %r\n\tbl\t",		\
	   IP_REGNUM, LR_REGNUM);			\
  assemble_name (STREAM, ARM_MCOUNT_NAME);		\
  fputc ('\n', STREAM);					\
  ASM_GENERATE_INTERNAL_LABEL (temp, "LP", LABELNO);	\
  sym = gen_rtx_SYMBOL_REF (Pmode, temp);		\
  assemble_aligned_integer (UNITS_PER_WORD, sym);	\
}
#endif

#ifdef THUMB_FUNCTION_PROFILER
#define FUNCTION_PROFILER(STREAM, LABELNO)		\
  if (TARGET_ARM)					\
    ARM_FUNCTION_PROFILER (STREAM, LABELNO)		\
  else							\
    THUMB_FUNCTION_PROFILER (STREAM, LABELNO)
#else
#define FUNCTION_PROFILER(STREAM, LABELNO)		\
    ARM_FUNCTION_PROFILER (STREAM, LABELNO)
#endif

/* EXIT_IGNORE_STACK should be nonzero if, when returning from a function,
   the stack pointer does not matter.  The value is tested only in
   functions that have frame pointers.
   No definition is equivalent to always zero.

   On the ARM, the function epilogue recovers the stack pointer from the
   frame.  */
#define EXIT_IGNORE_STACK 1

#define EPILOGUE_USES(REGNO) (reload_completed && (REGNO) == LR_REGNUM)

/* Determine if the epilogue should be output as RTL.
   You should override this if you define FUNCTION_EXTRA_EPILOGUE.  */
#define USE_RETURN_INSN(ISCOND)				\
  (TARGET_ARM ? use_return_insn (ISCOND, NULL) : 0)

/* Definitions for register eliminations.

   This is an array of structures.  Each structure initializes one pair
   of eliminable registers.  The "from" register number is given first,
   followed by "to".  Eliminations of the same "from" register are listed
   in order of preference.

   We have two registers that can be eliminated on the ARM.  First, the
   arg pointer register can often be eliminated in favor of the stack
   pointer register.  Secondly, the pseudo frame pointer register can always
   be eliminated; it is replaced with either the stack or the real frame
   pointer.  Note we have to use {ARM|THUMB}_HARD_FRAME_POINTER_REGNUM
   because the definition of HARD_FRAME_POINTER_REGNUM is not a constant.  */

#define ELIMINABLE_REGS						\
{{ ARG_POINTER_REGNUM,        STACK_POINTER_REGNUM            },\
 { ARG_POINTER_REGNUM,        FRAME_POINTER_REGNUM            },\
 { ARG_POINTER_REGNUM,        ARM_HARD_FRAME_POINTER_REGNUM   },\
 { ARG_POINTER_REGNUM,        THUMB_HARD_FRAME_POINTER_REGNUM },\
 { FRAME_POINTER_REGNUM,      STACK_POINTER_REGNUM            },\
 { FRAME_POINTER_REGNUM,      ARM_HARD_FRAME_POINTER_REGNUM   },\
 { FRAME_POINTER_REGNUM,      THUMB_HARD_FRAME_POINTER_REGNUM }}

/* Given FROM and TO register numbers, say whether this elimination is
   allowed.  Frame pointer elimination is automatically handled.

   All eliminations are permissible.  Note that ARG_POINTER_REGNUM and
   HARD_FRAME_POINTER_REGNUM are in fact the same thing.  If we need a frame
   pointer, we must eliminate FRAME_POINTER_REGNUM into
   HARD_FRAME_POINTER_REGNUM and not into STACK_POINTER_REGNUM or
   ARG_POINTER_REGNUM.  */
#define CAN_ELIMINATE(FROM, TO)						\
  (((TO) == FRAME_POINTER_REGNUM && (FROM) == ARG_POINTER_REGNUM) ? 0 :	\
   ((TO) == STACK_POINTER_REGNUM && frame_pointer_needed) ? 0 :		\
   ((TO) == ARM_HARD_FRAME_POINTER_REGNUM && TARGET_THUMB) ? 0 :	\
   ((TO) == THUMB_HARD_FRAME_POINTER_REGNUM && TARGET_ARM) ? 0 :	\
   1)

/* Define the offset between two registers, one to be eliminated, and the
   other its replacement, at the start of a routine.  */
#define INITIAL_ELIMINATION_OFFSET(FROM, TO, OFFSET)			\
  if (TARGET_ARM)							\
    (OFFSET) = arm_compute_initial_elimination_offset (FROM, TO);	\
  else									\
    (OFFSET) = thumb_compute_initial_elimination_offset (FROM, TO)

/* Special case handling of the location of arguments passed on the stack.  */
#define DEBUGGER_ARG_OFFSET(value, addr) value ? value : arm_debugger_arg_offset (value, addr)

/* Initialize data used by insn expanders.  This is called from insn_emit,
   once for every function before code is generated.  */
#define INIT_EXPANDERS  arm_init_expanders ()

/* Output assembler code for a block containing the constant parts
   of a trampoline, leaving space for the variable parts.

   On the ARM, (if r8 is the static chain regnum, and remembering that
   referencing pc adds an offset of 8) the trampoline looks like:
	   ldr 		r8, [pc, #0]
	   ldr		pc, [pc]
	   .word	static chain value
	   .word	function's address
   XXX FIXME: When the trampoline returns, r8 will be clobbered.  */
#define ARM_TRAMPOLINE_TEMPLATE(FILE)				\
{								\
  asm_fprintf (FILE, "\tldr\t%r, [%r, #0]\n",			\
	       STATIC_CHAIN_REGNUM, PC_REGNUM);			\
  asm_fprintf (FILE, "\tldr\t%r, [%r, #0]\n",			\
	       PC_REGNUM, PC_REGNUM);				\
  assemble_aligned_integer (UNITS_PER_WORD, const0_rtx);	\
  assemble_aligned_integer (UNITS_PER_WORD, const0_rtx);	\
}

/* On the Thumb we always switch into ARM mode to execute the trampoline.
   Why - because it is easier.  This code will always be branched to via
   a BX instruction and since the compiler magically generates the address
   of the function the linker has no opportunity to ensure that the
   bottom bit is set.  Thus the processor will be in ARM mode when it
   reaches this code.  So we duplicate the ARM trampoline code and add
   a switch into Thumb mode as well.  */
#define THUMB_TRAMPOLINE_TEMPLATE(FILE)		\
{						\
  fprintf (FILE, "\t.code 32\n");		\
  fprintf (FILE, ".Ltrampoline_start:\n");	\
  asm_fprintf (FILE, "\tldr\t%r, [%r, #8]\n",	\
	       STATIC_CHAIN_REGNUM, PC_REGNUM);	\
  asm_fprintf (FILE, "\tldr\t%r, [%r, #8]\n",	\
	       IP_REGNUM, PC_REGNUM);		\
  asm_fprintf (FILE, "\torr\t%r, %r, #1\n",     \
	       IP_REGNUM, IP_REGNUM);     	\
  asm_fprintf (FILE, "\tbx\t%r\n", IP_REGNUM);	\
  fprintf (FILE, "\t.word\t0\n");		\
  fprintf (FILE, "\t.word\t0\n");		\
  fprintf (FILE, "\t.code 16\n");		\
}

#define TRAMPOLINE_TEMPLATE(FILE)		\
  if (TARGET_ARM)				\
    ARM_TRAMPOLINE_TEMPLATE (FILE)		\
  else						\
    THUMB_TRAMPOLINE_TEMPLATE (FILE)

/* Length in units of the trampoline for entering a nested function.  */
#define TRAMPOLINE_SIZE  (TARGET_ARM ? 16 : 24)

/* Alignment required for a trampoline in bits.  */
#define TRAMPOLINE_ALIGNMENT  32


/* Emit RTL insns to initialize the variable parts of a trampoline.
   FNADDR is an RTX for the address of the function's pure code.
   CXT is an RTX for the static chain value for the function.  */
#ifndef INITIALIZE_TRAMPOLINE
#define INITIALIZE_TRAMPOLINE(TRAMP, FNADDR, CXT)			\
{									\
  emit_move_insn (gen_rtx_MEM (SImode,					\
			       plus_constant (TRAMP,			\
					      TARGET_ARM ? 8 : 16)),	\
		  CXT);							\
  emit_move_insn (gen_rtx_MEM (SImode,					\
			       plus_constant (TRAMP,			\
					      TARGET_ARM ? 12 : 20)),	\
		  FNADDR);						\
  emit_library_call (gen_rtx_SYMBOL_REF (Pmode, "__clear_cache"),	\
		     0, VOIDmode, 2, TRAMP, Pmode,			\
		     plus_constant (TRAMP, TRAMPOLINE_SIZE), Pmode);	\
}
#endif


/* Addressing modes, and classification of registers for them.  */
#define HAVE_POST_INCREMENT   1
#define HAVE_PRE_INCREMENT    TARGET_ARM
#define HAVE_POST_DECREMENT   TARGET_ARM
#define HAVE_PRE_DECREMENT    TARGET_ARM
#define HAVE_PRE_MODIFY_DISP  TARGET_ARM
#define HAVE_POST_MODIFY_DISP TARGET_ARM
#define HAVE_PRE_MODIFY_REG   TARGET_ARM
#define HAVE_POST_MODIFY_REG  TARGET_ARM

/* Macros to check register numbers against specific register classes.  */

/* These assume that REGNO is a hard or pseudo reg number.
   They give nonzero only if REGNO is a hard reg of the suitable class
   or a pseudo reg currently allocated to a suitable hard reg.
   Since they use reg_renumber, they are safe only once reg_renumber
   has been allocated, which happens in local-alloc.c.  */
#define TEST_REGNO(R, TEST, VALUE) \
  ((R TEST VALUE) || ((unsigned) reg_renumber[R] TEST VALUE))

/*   On the ARM, don't allow the pc to be used.  */
#define ARM_REGNO_OK_FOR_BASE_P(REGNO)			\
  (TEST_REGNO (REGNO, <, PC_REGNUM)			\
   || TEST_REGNO (REGNO, ==, FRAME_POINTER_REGNUM)	\
   || TEST_REGNO (REGNO, ==, ARG_POINTER_REGNUM))

#define THUMB_REGNO_MODE_OK_FOR_BASE_P(REGNO, MODE)		\
  (TEST_REGNO (REGNO, <=, LAST_LO_REGNUM)			\
   || (GET_MODE_SIZE (MODE) >= 4				\
       && TEST_REGNO (REGNO, ==, STACK_POINTER_REGNUM)))

#define REGNO_MODE_OK_FOR_BASE_P(REGNO, MODE)		\
  (TARGET_THUMB						\
   ? THUMB_REGNO_MODE_OK_FOR_BASE_P (REGNO, MODE)	\
   : ARM_REGNO_OK_FOR_BASE_P (REGNO))

/* Nonzero if X can be the base register in a reg+reg addressing mode.
   For Thumb, we can not use SP + reg, so reject SP.  */
#define REGNO_MODE_OK_FOR_REG_BASE_P(X, MODE)	\
  REGNO_OK_FOR_INDEX_P (X)

/* For ARM code, we don't care about the mode, but for Thumb, the index
   must be suitable for use in a QImode load.  */
#define REGNO_OK_FOR_INDEX_P(REGNO)	\
  REGNO_MODE_OK_FOR_BASE_P (REGNO, QImode)

/* Maximum number of registers that can appear in a valid memory address.
   Shifts in addresses can't be by a register.  */
#define MAX_REGS_PER_ADDRESS 2

/* Recognize any constant value that is a valid address.  */
/* XXX We can address any constant, eventually...  */

#ifdef AOF_ASSEMBLER

#define CONSTANT_ADDRESS_P(X)		\
  (GET_CODE (X) == SYMBOL_REF && CONSTANT_POOL_ADDRESS_P (X))

#else

#define CONSTANT_ADDRESS_P(X)  			\
  (GET_CODE (X) == SYMBOL_REF 			\
   && (CONSTANT_POOL_ADDRESS_P (X)		\
       || (TARGET_ARM && optimize > 0 && SYMBOL_REF_FLAG (X))))

#endif /* AOF_ASSEMBLER */

/* Nonzero if the constant value X is a legitimate general operand.
   It is given that X satisfies CONSTANT_P or is a CONST_DOUBLE.

   On the ARM, allow any integer (invalid ones are removed later by insn
   patterns), nice doubles and symbol_refs which refer to the function's
   constant pool XXX.

   When generating pic allow anything.  */
#define ARM_LEGITIMATE_CONSTANT_P(X)	(flag_pic || ! label_mentioned_p (X))

#define THUMB_LEGITIMATE_CONSTANT_P(X)	\
 (   GET_CODE (X) == CONST_INT		\
  || GET_CODE (X) == CONST_DOUBLE	\
  || CONSTANT_ADDRESS_P (X)		\
  || flag_pic)

#define LEGITIMATE_CONSTANT_P(X)			\
  (!arm_tls_referenced_p (X)				\
   && (TARGET_ARM ? ARM_LEGITIMATE_CONSTANT_P (X)	\
		  : THUMB_LEGITIMATE_CONSTANT_P (X)))

/* Special characters prefixed to function names
   in order to encode attribute like information.
   Note, '@' and '*' have already been taken.  */
#define SHORT_CALL_FLAG_CHAR	'^'
#define LONG_CALL_FLAG_CHAR	'#'

#define ENCODED_SHORT_CALL_ATTR_P(SYMBOL_NAME)	\
  (*(SYMBOL_NAME) == SHORT_CALL_FLAG_CHAR)

#define ENCODED_LONG_CALL_ATTR_P(SYMBOL_NAME)	\
  (*(SYMBOL_NAME) == LONG_CALL_FLAG_CHAR)

#ifndef SUBTARGET_NAME_ENCODING_LENGTHS
#define SUBTARGET_NAME_ENCODING_LENGTHS
#endif

/* This is a C fragment for the inside of a switch statement.
   Each case label should return the number of characters to
   be stripped from the start of a function's name, if that
   name starts with the indicated character.  */
#define ARM_NAME_ENCODING_LENGTHS		\
  case SHORT_CALL_FLAG_CHAR: return 1;		\
  case LONG_CALL_FLAG_CHAR:  return 1;		\
  case '*':  return 1;				\
  SUBTARGET_NAME_ENCODING_LENGTHS

/* This is how to output a reference to a user-level label named NAME.
   `assemble_name' uses this.  */
#undef  ASM_OUTPUT_LABELREF
#define ASM_OUTPUT_LABELREF(FILE, NAME)		\
   arm_asm_output_labelref (FILE, NAME)

/* The EABI specifies that constructors should go in .init_array.
   Other targets use .ctors for compatibility.  */
#ifndef ARM_EABI_CTORS_SECTION_OP
#define ARM_EABI_CTORS_SECTION_OP \
  "\t.section\t.init_array,\"aw\",%init_array"
#endif
#ifndef ARM_EABI_DTORS_SECTION_OP
#define ARM_EABI_DTORS_SECTION_OP \
  "\t.section\t.fini_array,\"aw\",%fini_array"
#endif
#define ARM_CTORS_SECTION_OP \
  "\t.section\t.ctors,\"aw\",%progbits"
#define ARM_DTORS_SECTION_OP \
  "\t.section\t.dtors,\"aw\",%progbits"

/* Define CTORS_SECTION_ASM_OP.  */
#undef CTORS_SECTION_ASM_OP
#undef DTORS_SECTION_ASM_OP
#ifndef IN_LIBGCC2
# define CTORS_SECTION_ASM_OP \
   (TARGET_AAPCS_BASED ? ARM_EABI_CTORS_SECTION_OP : ARM_CTORS_SECTION_OP)
# define DTORS_SECTION_ASM_OP \
   (TARGET_AAPCS_BASED ? ARM_EABI_DTORS_SECTION_OP : ARM_DTORS_SECTION_OP)
#else /* !defined (IN_LIBGCC2) */
/* In libgcc, CTORS_SECTION_ASM_OP must be a compile-time constant,
   so we cannot use the definition above.  */
# ifdef __ARM_EABI__
/* The .ctors section is not part of the EABI, so we do not define
   CTORS_SECTION_ASM_OP when in libgcc; that prevents crtstuff
   from trying to use it.  We do define it when doing normal
   compilation, as .init_array can be used instead of .ctors.  */
/* There is no need to emit begin or end markers when using
   init_array; the dynamic linker will compute the size of the
   array itself based on special symbols created by the static
   linker.  However, we do need to arrange to set up
   exception-handling here.  */
#   define CTOR_LIST_BEGIN asm (ARM_EABI_CTORS_SECTION_OP)
#   define CTOR_LIST_END /* empty */
#   define DTOR_LIST_BEGIN asm (ARM_EABI_DTORS_SECTION_OP)
#   define DTOR_LIST_END /* empty */
# else /* !defined (__ARM_EABI__) */
#  ifndef __clang__
#   define CTORS_SECTION_ASM_OP ARM_CTORS_SECTION_OP
#   define DTORS_SECTION_ASM_OP ARM_DTORS_SECTION_OP
#  endif
# endif /* !defined (__ARM_EABI__) */
#endif /* !defined (IN_LIBCC2) */

/* True if the operating system can merge entities with vague linkage
   (e.g., symbols in COMDAT group) during dynamic linking.  */
#ifndef TARGET_ARM_DYNAMIC_VAGUE_LINKAGE_P
#define TARGET_ARM_DYNAMIC_VAGUE_LINKAGE_P true
#endif

/* Set the short-call flag for any function compiled in the current
   compilation unit.  We skip this for functions with the section
   attribute when long-calls are in effect as this tells the compiler
   that the section might be placed a long way from the caller.
   See arm_is_longcall_p() for more information.  */
#define ARM_DECLARE_FUNCTION_SIZE(STREAM, NAME, DECL)	\
  if (!TARGET_LONG_CALLS || ! DECL_SECTION_NAME (DECL)) \
    arm_encode_call_attribute (DECL, SHORT_CALL_FLAG_CHAR)

#define ARM_OUTPUT_FN_UNWIND(F, PROLOGUE) arm_output_fn_unwind (F, PROLOGUE)

#ifdef TARGET_UNWIND_INFO
#define ARM_EABI_UNWIND_TABLES \
  ((!USING_SJLJ_EXCEPTIONS && flag_exceptions) || flag_unwind_tables)
#else
#define ARM_EABI_UNWIND_TABLES 0
#endif

/* The macros REG_OK_FOR..._P assume that the arg is a REG rtx
   and check its validity for a certain class.
   We have two alternate definitions for each of them.
   The usual definition accepts all pseudo regs; the other rejects
   them unless they have been allocated suitable hard regs.
   The symbol REG_OK_STRICT causes the latter definition to be used.  */
#ifndef REG_OK_STRICT

#define ARM_REG_OK_FOR_BASE_P(X)		\
  (REGNO (X) <= LAST_ARM_REGNUM			\
   || REGNO (X) >= FIRST_PSEUDO_REGISTER	\
   || REGNO (X) == FRAME_POINTER_REGNUM		\
   || REGNO (X) == ARG_POINTER_REGNUM)

#define THUMB_REG_MODE_OK_FOR_BASE_P(X, MODE)	\
  (REGNO (X) <= LAST_LO_REGNUM			\
   || REGNO (X) >= FIRST_PSEUDO_REGISTER	\
   || (GET_MODE_SIZE (MODE) >= 4		\
       && (REGNO (X) == STACK_POINTER_REGNUM	\
	   || (X) == hard_frame_pointer_rtx	\
	   || (X) == arg_pointer_rtx)))

#define REG_STRICT_P 0

#else /* REG_OK_STRICT */

#define ARM_REG_OK_FOR_BASE_P(X) 		\
  ARM_REGNO_OK_FOR_BASE_P (REGNO (X))

#define THUMB_REG_MODE_OK_FOR_BASE_P(X, MODE)	\
  THUMB_REGNO_MODE_OK_FOR_BASE_P (REGNO (X), MODE)

#define REG_STRICT_P 1

#endif /* REG_OK_STRICT */

/* Now define some helpers in terms of the above.  */

#define REG_MODE_OK_FOR_BASE_P(X, MODE)		\
  (TARGET_THUMB					\
   ? THUMB_REG_MODE_OK_FOR_BASE_P (X, MODE)	\
   : ARM_REG_OK_FOR_BASE_P (X))

#define ARM_REG_OK_FOR_INDEX_P(X) ARM_REG_OK_FOR_BASE_P (X)

/* For Thumb, a valid index register is anything that can be used in
   a byte load instruction.  */
#define THUMB_REG_OK_FOR_INDEX_P(X) THUMB_REG_MODE_OK_FOR_BASE_P (X, QImode)

/* Nonzero if X is a hard reg that can be used as an index
   or if it is a pseudo reg.  On the Thumb, the stack pointer
   is not suitable.  */
#define REG_OK_FOR_INDEX_P(X)			\
  (TARGET_THUMB					\
   ? THUMB_REG_OK_FOR_INDEX_P (X)		\
   : ARM_REG_OK_FOR_INDEX_P (X))

/* Nonzero if X can be the base register in a reg+reg addressing mode.
   For Thumb, we can not use SP + reg, so reject SP.  */
#define REG_MODE_OK_FOR_REG_BASE_P(X, MODE)	\
  REG_OK_FOR_INDEX_P (X)

/* GO_IF_LEGITIMATE_ADDRESS recognizes an RTL expression
   that is a valid memory address for an instruction.
   The MODE argument is the machine mode for the MEM expression
   that wants to use this address.  */

#define ARM_BASE_REGISTER_RTX_P(X)  \
  (GET_CODE (X) == REG && ARM_REG_OK_FOR_BASE_P (X))

#define ARM_INDEX_REGISTER_RTX_P(X)  \
  (GET_CODE (X) == REG && ARM_REG_OK_FOR_INDEX_P (X))

#define ARM_GO_IF_LEGITIMATE_ADDRESS(MODE,X,WIN)		\
  {								\
    if (arm_legitimate_address_p (MODE, X, SET, REG_STRICT_P))	\
      goto WIN;							\
  }

#define THUMB_GO_IF_LEGITIMATE_ADDRESS(MODE,X,WIN)		\
  {								\
    if (thumb_legitimate_address_p (MODE, X, REG_STRICT_P))	\
      goto WIN;							\
  }

#define GO_IF_LEGITIMATE_ADDRESS(MODE, X, WIN)				\
  if (TARGET_ARM)							\
    ARM_GO_IF_LEGITIMATE_ADDRESS (MODE, X, WIN)  			\
  else /* if (TARGET_THUMB) */						\
    THUMB_GO_IF_LEGITIMATE_ADDRESS (MODE, X, WIN)


/* Try machine-dependent ways of modifying an illegitimate address
   to be legitimate.  If we find one, return the new, valid address.  */
#define ARM_LEGITIMIZE_ADDRESS(X, OLDX, MODE, WIN)	\
do {							\
  X = arm_legitimize_address (X, OLDX, MODE);		\
} while (0)

#define THUMB_LEGITIMIZE_ADDRESS(X, OLDX, MODE, WIN)	\
do {							\
  X = thumb_legitimize_address (X, OLDX, MODE);		\
} while (0)

#define LEGITIMIZE_ADDRESS(X, OLDX, MODE, WIN)		\
do {							\
  if (TARGET_ARM)					\
    ARM_LEGITIMIZE_ADDRESS (X, OLDX, MODE, WIN);	\
  else							\
    THUMB_LEGITIMIZE_ADDRESS (X, OLDX, MODE, WIN);	\
							\
  if (memory_address_p (MODE, X))			\
    goto WIN;						\
} while (0)

/* Go to LABEL if ADDR (a legitimate address expression)
   has an effect that depends on the machine mode it is used for.  */
#define ARM_GO_IF_MODE_DEPENDENT_ADDRESS(ADDR, LABEL)  			\
{									\
  if (   GET_CODE (ADDR) == PRE_DEC || GET_CODE (ADDR) == POST_DEC	\
      || GET_CODE (ADDR) == PRE_INC || GET_CODE (ADDR) == POST_INC)	\
    goto LABEL;								\
}

/* Nothing helpful to do for the Thumb */
#define GO_IF_MODE_DEPENDENT_ADDRESS(ADDR, LABEL)	\
  if (TARGET_ARM)					\
    ARM_GO_IF_MODE_DEPENDENT_ADDRESS (ADDR, LABEL)


/* Specify the machine mode that this machine uses
   for the index in the tablejump instruction.  */
#define CASE_VECTOR_MODE Pmode

/* signed 'char' is most compatible, but RISC OS wants it unsigned.
   unsigned is probably best, but may break some code.  */
#ifndef DEFAULT_SIGNED_CHAR
#define DEFAULT_SIGNED_CHAR  0
#endif

/* Max number of bytes we can move from memory to memory
   in one reasonably fast instruction.  */
#define MOVE_MAX 4

#undef  MOVE_RATIO
#define MOVE_RATIO (arm_tune_xscale ? 4 : 2)

/* Define if operations between registers always perform the operation
   on the full register even if a narrower mode is specified.  */
#define WORD_REGISTER_OPERATIONS

/* Define if loading in MODE, an integral mode narrower than BITS_PER_WORD
   will either zero-extend or sign-extend.  The value of this macro should
   be the code that says which one of the two operations is implicitly
   done, UNKNOWN if none.  */
#define LOAD_EXTEND_OP(MODE)						\
  (TARGET_THUMB ? ZERO_EXTEND :						\
   ((arm_arch4 || (MODE) == QImode) ? ZERO_EXTEND			\
    : ((BYTES_BIG_ENDIAN && (MODE) == HImode) ? SIGN_EXTEND : UNKNOWN)))

/* Nonzero if access to memory by bytes is slow and undesirable.  */
#define SLOW_BYTE_ACCESS 0

#define SLOW_UNALIGNED_ACCESS(MODE, ALIGN) 1

/* Immediate shift counts are truncated by the output routines (or was it
   the assembler?).  Shift counts in a register are truncated by ARM.  Note
   that the native compiler puts too large (> 32) immediate shift counts
   into a register and shifts by the register, letting the ARM decide what
   to do instead of doing that itself.  */
/* This is all wrong.  Defining SHIFT_COUNT_TRUNCATED tells combine that
   code like (X << (Y % 32)) for register X, Y is equivalent to (X << Y).
   On the arm, Y in a register is used modulo 256 for the shift. Only for
   rotates is modulo 32 used.  */
/* #define SHIFT_COUNT_TRUNCATED 1 */

/* All integers have the same format so truncation is easy.  */
#define TRULY_NOOP_TRUNCATION(OUTPREC, INPREC)  1

/* Calling from registers is a massive pain.  */
#define NO_FUNCTION_CSE 1

/* The machine modes of pointers and functions */
#define Pmode  SImode
#define FUNCTION_MODE  Pmode

#define ARM_FRAME_RTX(X)					\
  (   (X) == frame_pointer_rtx || (X) == stack_pointer_rtx	\
   || (X) == arg_pointer_rtx)

/* Moves to and from memory are quite expensive */
#define MEMORY_MOVE_COST(M, CLASS, IN)			\
  (TARGET_ARM ? 10 :					\
   ((GET_MODE_SIZE (M) < 4 ? 8 : 2 * GET_MODE_SIZE (M))	\
    * (CLASS == LO_REGS ? 1 : 2)))

/* Try to generate sequences that don't involve branches, we can then use
   conditional instructions */
#define BRANCH_COST \
  (TARGET_ARM ? 4 : (optimize > 1 ? 1 : 0))

/* Position Independent Code.  */
/* We decide which register to use based on the compilation options and
   the assembler in use; this is more general than the APCS restriction of
   using sb (r9) all the time.  */
extern unsigned arm_pic_register;

/* The register number of the register used to address a table of static
   data addresses in memory.  */
#define PIC_OFFSET_TABLE_REGNUM arm_pic_register

/* We can't directly access anything that contains a symbol,
   nor can we indirect via the constant pool.  One exception is
   UNSPEC_TLS, which is always PIC.  */
#define LEGITIMATE_PIC_OPERAND_P(X)					\
	(!(symbol_mentioned_p (X)					\
	   || label_mentioned_p (X)					\
	   || (GET_CODE (X) == SYMBOL_REF				\
	       && CONSTANT_POOL_ADDRESS_P (X)				\
	       && (symbol_mentioned_p (get_pool_constant (X))		\
		   || label_mentioned_p (get_pool_constant (X)))))	\
	 || tls_mentioned_p (X))

/* We need to know when we are making a constant pool; this determines
   whether data needs to be in the GOT or can be referenced via a GOT
   offset.  */
extern int making_const_table;

/* Handle pragmas for compatibility with Intel's compilers.  */
#define REGISTER_TARGET_PRAGMAS() do {					\
  c_register_pragma (0, "long_calls", arm_pr_long_calls);		\
  c_register_pragma (0, "no_long_calls", arm_pr_no_long_calls);		\
  c_register_pragma (0, "long_calls_off", arm_pr_long_calls_off);	\
} while (0)

/* Condition code information.  */
/* Given a comparison code (EQ, NE, etc.) and the first operand of a COMPARE,
   return the mode to be used for the comparison.  */

#define SELECT_CC_MODE(OP, X, Y)  arm_select_cc_mode (OP, X, Y)

#define REVERSIBLE_CC_MODE(MODE) 1

#define REVERSE_CONDITION(CODE,MODE) \
  (((MODE) == CCFPmode || (MODE) == CCFPEmode) \
   ? reverse_condition_maybe_unordered (code) \
   : reverse_condition (code))

#define CANONICALIZE_COMPARISON(CODE, OP0, OP1)				\
  do									\
    {									\
      if (GET_CODE (OP1) == CONST_INT					\
          && ! (const_ok_for_arm (INTVAL (OP1))				\
	        || (const_ok_for_arm (- INTVAL (OP1)))))		\
        {								\
          rtx const_op = OP1;						\
          CODE = arm_canonicalize_comparison ((CODE), GET_MODE (OP0),	\
					      &const_op);		\
          OP1 = const_op;						\
        }								\
    }									\
  while (0)

/* The arm5 clz instruction returns 32.  */
#define CLZ_DEFINED_VALUE_AT_ZERO(MODE, VALUE)  ((VALUE) = 32, 1)

#undef  ASM_APP_OFF
#define ASM_APP_OFF (TARGET_THUMB ? "\t.code\t16\n" : "")

/* Output a push or a pop instruction (only used when profiling).  */
#define ASM_OUTPUT_REG_PUSH(STREAM, REGNO)		\
  do							\
    {							\
      if (TARGET_ARM)					\
	asm_fprintf (STREAM,"\tstmfd\t%r!,{%r}\n",	\
		     STACK_POINTER_REGNUM, REGNO);	\
      else						\
	asm_fprintf (STREAM, "\tpush {%r}\n", REGNO);	\
    } while (0)


#define ASM_OUTPUT_REG_POP(STREAM, REGNO)		\
  do							\
    {							\
      if (TARGET_ARM)					\
	asm_fprintf (STREAM, "\tldmfd\t%r!,{%r}\n",	\
		     STACK_POINTER_REGNUM, REGNO);	\
      else						\
	asm_fprintf (STREAM, "\tpop {%r}\n", REGNO);	\
    } while (0)

/* This is how to output a label which precedes a jumptable.  Since
   Thumb instructions are 2 bytes, we may need explicit alignment here.  */
#undef  ASM_OUTPUT_CASE_LABEL
#define ASM_OUTPUT_CASE_LABEL(FILE, PREFIX, NUM, JUMPTABLE)	\
  do								\
    {								\
      if (TARGET_THUMB)						\
        ASM_OUTPUT_ALIGN (FILE, 2);				\
      (*targetm.asm_out.internal_label) (FILE, PREFIX, NUM);	\
    }								\
  while (0)

#define ARM_DECLARE_FUNCTION_NAME(STREAM, NAME, DECL) 	\
  do							\
    {							\
      if (TARGET_THUMB) 				\
        {						\
          if (is_called_in_ARM_mode (DECL)      \
			  || current_function_is_thunk)		\
            fprintf (STREAM, "\t.code 32\n") ;		\
          else						\
           fprintf (STREAM, "\t.code 16\n\t.thumb_func\n") ;	\
        }						\
      if (TARGET_POKE_FUNCTION_NAME)			\
        arm_poke_function_name (STREAM, (char *) NAME);	\
    }							\
  while (0)

/* For aliases of functions we use .thumb_set instead.  */
#define ASM_OUTPUT_DEF_FROM_DECLS(FILE, DECL1, DECL2)		\
  do						   		\
    {								\
      const char *const LABEL1 = XSTR (XEXP (DECL_RTL (decl), 0), 0); \
      const char *const LABEL2 = IDENTIFIER_POINTER (DECL2);	\
								\
      if (TARGET_THUMB && TREE_CODE (DECL1) == FUNCTION_DECL)	\
	{							\
	  fprintf (FILE, "\t.thumb_set ");			\
	  assemble_name (FILE, LABEL1);			   	\
	  fprintf (FILE, ",");			   		\
	  assemble_name (FILE, LABEL2);		   		\
	  fprintf (FILE, "\n");					\
	}							\
      else							\
	ASM_OUTPUT_DEF (FILE, LABEL1, LABEL2);			\
    }								\
  while (0)

#ifdef HAVE_GAS_MAX_SKIP_P2ALIGN
/* To support -falign-* switches we need to use .p2align so
   that alignment directives in code sections will be padded
   with no-op instructions, rather than zeroes.  */
#define ASM_OUTPUT_MAX_SKIP_ALIGN(FILE, LOG, MAX_SKIP)		\
  if ((LOG) != 0)						\
    {								\
      if ((MAX_SKIP) == 0)					\
        fprintf ((FILE), "\t.p2align %d\n", (int) (LOG));	\
      else							\
        fprintf ((FILE), "\t.p2align %d,,%d\n",			\
                 (int) (LOG), (int) (MAX_SKIP));		\
    }
#endif

/* Only perform branch elimination (by making instructions conditional) if
   we're optimizing.  Otherwise it's of no use anyway.  */
#define FINAL_PRESCAN_INSN(INSN, OPVEC, NOPERANDS)	\
  if (TARGET_ARM && optimize)				\
    arm_final_prescan_insn (INSN);			\
  else if (TARGET_THUMB)				\
    thumb_final_prescan_insn (INSN)

#define PRINT_OPERAND_PUNCT_VALID_P(CODE)	\
  (CODE == '@' || CODE == '|'			\
   || (TARGET_ARM   && (CODE == '?'))		\
   || (TARGET_THUMB && (CODE == '_')))

/* Output an operand of an instruction.  */
#define PRINT_OPERAND(STREAM, X, CODE)  \
  arm_print_operand (STREAM, X, CODE)

#define ARM_SIGN_EXTEND(x)  ((HOST_WIDE_INT)			\
  (HOST_BITS_PER_WIDE_INT <= 32 ? (unsigned HOST_WIDE_INT) (x)	\
   : ((((unsigned HOST_WIDE_INT)(x)) & (unsigned HOST_WIDE_INT) 0xffffffff) |\
      ((((unsigned HOST_WIDE_INT)(x)) & (unsigned HOST_WIDE_INT) 0x80000000) \
       ? ((~ (unsigned HOST_WIDE_INT) 0)			\
	  & ~ (unsigned HOST_WIDE_INT) 0xffffffff)		\
       : 0))))

/* Output the address of an operand.  */
#define ARM_PRINT_OPERAND_ADDRESS(STREAM, X)				\
{									\
    int is_minus = GET_CODE (X) == MINUS;				\
									\
    if (GET_CODE (X) == REG)						\
      asm_fprintf (STREAM, "[%r, #0]", REGNO (X));			\
    else if (GET_CODE (X) == PLUS || is_minus)				\
      {									\
	rtx base = XEXP (X, 0);						\
	rtx index = XEXP (X, 1);					\
	HOST_WIDE_INT offset = 0;					\
	if (GET_CODE (base) != REG)					\
	  {								\
	    /* Ensure that BASE is a register.  */			\
            /* (one of them must be).  */				\
	    rtx temp = base;						\
	    base = index;						\
	    index = temp;						\
	  }								\
	switch (GET_CODE (index))					\
	  {								\
	  case CONST_INT:						\
	    offset = INTVAL (index);					\
	    if (is_minus)						\
	      offset = -offset;						\
	    asm_fprintf (STREAM, "[%r, #%wd]",				\
		         REGNO (base), offset);				\
	    break;							\
									\
	  case REG:							\
	    asm_fprintf (STREAM, "[%r, %s%r]",				\
		     REGNO (base), is_minus ? "-" : "",			\
		     REGNO (index));					\
	    break;							\
									\
	  case MULT:							\
	  case ASHIFTRT:						\
	  case LSHIFTRT:						\
	  case ASHIFT:							\
	  case ROTATERT:						\
	  {								\
	    asm_fprintf (STREAM, "[%r, %s%r",				\
		         REGNO (base), is_minus ? "-" : "",		\
                         REGNO (XEXP (index, 0)));			\
	    arm_print_operand (STREAM, index, 'S');			\
	    fputs ("]", STREAM);					\
	    break;							\
	  }								\
									\
	  default:							\
	    gcc_unreachable ();						\
	}								\
    }									\
  else if (GET_CODE (X) == PRE_INC || GET_CODE (X) == POST_INC		\
	   || GET_CODE (X) == PRE_DEC || GET_CODE (X) == POST_DEC)	\
    {									\
      extern enum machine_mode output_memory_reference_mode;		\
									\
      gcc_assert (GET_CODE (XEXP (X, 0)) == REG);			\
									\
      if (GET_CODE (X) == PRE_DEC || GET_CODE (X) == PRE_INC)		\
	asm_fprintf (STREAM, "[%r, #%s%d]!",				\
		     REGNO (XEXP (X, 0)),				\
		     GET_CODE (X) == PRE_DEC ? "-" : "",		\
		     GET_MODE_SIZE (output_memory_reference_mode));	\
      else								\
	asm_fprintf (STREAM, "[%r], #%s%d",				\
		     REGNO (XEXP (X, 0)),				\
		     GET_CODE (X) == POST_DEC ? "-" : "",		\
		     GET_MODE_SIZE (output_memory_reference_mode));	\
    }									\
  else if (GET_CODE (X) == PRE_MODIFY)					\
    {									\
      asm_fprintf (STREAM, "[%r, ", REGNO (XEXP (X, 0)));		\
      if (GET_CODE (XEXP (XEXP (X, 1), 1)) == CONST_INT)		\
	asm_fprintf (STREAM, "#%wd]!", 					\
		     INTVAL (XEXP (XEXP (X, 1), 1)));			\
      else								\
	asm_fprintf (STREAM, "%r]!", 					\
		     REGNO (XEXP (XEXP (X, 1), 1)));			\
    }									\
  else if (GET_CODE (X) == POST_MODIFY)					\
    {									\
      asm_fprintf (STREAM, "[%r], ", REGNO (XEXP (X, 0)));		\
      if (GET_CODE (XEXP (XEXP (X, 1), 1)) == CONST_INT)		\
	asm_fprintf (STREAM, "#%wd", 					\
		     INTVAL (XEXP (XEXP (X, 1), 1)));			\
      else								\
	asm_fprintf (STREAM, "%r", 					\
		     REGNO (XEXP (XEXP (X, 1), 1)));			\
    }									\
  else output_addr_const (STREAM, X);					\
}

#define THUMB_PRINT_OPERAND_ADDRESS(STREAM, X)		\
{							\
  if (GET_CODE (X) == REG)				\
    asm_fprintf (STREAM, "[%r]", REGNO (X));		\
  else if (GET_CODE (X) == POST_INC)			\
    asm_fprintf (STREAM, "%r!", REGNO (XEXP (X, 0)));	\
  else if (GET_CODE (X) == PLUS)			\
    {							\
      gcc_assert (GET_CODE (XEXP (X, 0)) == REG);	\
      if (GET_CODE (XEXP (X, 1)) == CONST_INT)		\
	asm_fprintf (STREAM, "[%r, #%wd]", 		\
		     REGNO (XEXP (X, 0)),		\
		     INTVAL (XEXP (X, 1)));		\
      else						\
	asm_fprintf (STREAM, "[%r, %r]",		\
		     REGNO (XEXP (X, 0)),		\
		     REGNO (XEXP (X, 1)));		\
    }							\
  else							\
    output_addr_const (STREAM, X);			\
}

#define PRINT_OPERAND_ADDRESS(STREAM, X)	\
  if (TARGET_ARM)				\
    ARM_PRINT_OPERAND_ADDRESS (STREAM, X)	\
  else						\
    THUMB_PRINT_OPERAND_ADDRESS (STREAM, X)

#define OUTPUT_ADDR_CONST_EXTRA(file, x, fail)		\
  if (arm_output_addr_const_extra (file, x) == FALSE)	\
    goto fail

/* A C expression whose value is RTL representing the value of the return
   address for the frame COUNT steps up from the current frame.  */

#define RETURN_ADDR_RTX(COUNT, FRAME) \
  arm_return_addr (COUNT, FRAME)

/* Mask of the bits in the PC that contain the real return address
   when running in 26-bit mode.  */
#define RETURN_ADDR_MASK26 (0x03fffffc)

/* Pick up the return address upon entry to a procedure. Used for
   dwarf2 unwind information.  This also enables the table driven
   mechanism.  */
#define INCOMING_RETURN_ADDR_RTX	gen_rtx_REG (Pmode, LR_REGNUM)
#define DWARF_FRAME_RETURN_COLUMN	DWARF_FRAME_REGNUM (LR_REGNUM)

/* Used to mask out junk bits from the return address, such as
   processor state, interrupt status, condition codes and the like.  */
#define MASK_RETURN_ADDR \
  /* If we are generating code for an ARM2/ARM3 machine or for an ARM6	\
     in 26 bit mode, the condition codes must be masked out of the	\
     return address.  This does not apply to ARM6 and later processors	\
     when running in 32 bit mode.  */					\
  ((arm_arch4 || TARGET_THUMB)						\
   ? (gen_int_mode ((unsigned long)0xffffffff, Pmode))			\
   : arm_gen_return_addr_mask ())


enum arm_builtins
{
  ARM_BUILTIN_GETWCX,
  ARM_BUILTIN_SETWCX,

  ARM_BUILTIN_WZERO,

  ARM_BUILTIN_WAVG2BR,
  ARM_BUILTIN_WAVG2HR,
  ARM_BUILTIN_WAVG2B,
  ARM_BUILTIN_WAVG2H,

  ARM_BUILTIN_WACCB,
  ARM_BUILTIN_WACCH,
  ARM_BUILTIN_WACCW,

  ARM_BUILTIN_WMACS,
  ARM_BUILTIN_WMACSZ,
  ARM_BUILTIN_WMACU,
  ARM_BUILTIN_WMACUZ,

  ARM_BUILTIN_WSADB,
  ARM_BUILTIN_WSADBZ,
  ARM_BUILTIN_WSADH,
  ARM_BUILTIN_WSADHZ,

  ARM_BUILTIN_WALIGN,

  ARM_BUILTIN_TMIA,
  ARM_BUILTIN_TMIAPH,
  ARM_BUILTIN_TMIABB,
  ARM_BUILTIN_TMIABT,
  ARM_BUILTIN_TMIATB,
  ARM_BUILTIN_TMIATT,

  ARM_BUILTIN_TMOVMSKB,
  ARM_BUILTIN_TMOVMSKH,
  ARM_BUILTIN_TMOVMSKW,

  ARM_BUILTIN_TBCSTB,
  ARM_BUILTIN_TBCSTH,
  ARM_BUILTIN_TBCSTW,

  ARM_BUILTIN_WMADDS,
  ARM_BUILTIN_WMADDU,

  ARM_BUILTIN_WPACKHSS,
  ARM_BUILTIN_WPACKWSS,
  ARM_BUILTIN_WPACKDSS,
  ARM_BUILTIN_WPACKHUS,
  ARM_BUILTIN_WPACKWUS,
  ARM_BUILTIN_WPACKDUS,

  ARM_BUILTIN_WADDB,
  ARM_BUILTIN_WADDH,
  ARM_BUILTIN_WADDW,
  ARM_BUILTIN_WADDSSB,
  ARM_BUILTIN_WADDSSH,
  ARM_BUILTIN_WADDSSW,
  ARM_BUILTIN_WADDUSB,
  ARM_BUILTIN_WADDUSH,
  ARM_BUILTIN_WADDUSW,
  ARM_BUILTIN_WSUBB,
  ARM_BUILTIN_WSUBH,
  ARM_BUILTIN_WSUBW,
  ARM_BUILTIN_WSUBSSB,
  ARM_BUILTIN_WSUBSSH,
  ARM_BUILTIN_WSUBSSW,
  ARM_BUILTIN_WSUBUSB,
  ARM_BUILTIN_WSUBUSH,
  ARM_BUILTIN_WSUBUSW,

  ARM_BUILTIN_WAND,
  ARM_BUILTIN_WANDN,
  ARM_BUILTIN_WOR,
  ARM_BUILTIN_WXOR,

  ARM_BUILTIN_WCMPEQB,
  ARM_BUILTIN_WCMPEQH,
  ARM_BUILTIN_WCMPEQW,
  ARM_BUILTIN_WCMPGTUB,
  ARM_BUILTIN_WCMPGTUH,
  ARM_BUILTIN_WCMPGTUW,
  ARM_BUILTIN_WCMPGTSB,
  ARM_BUILTIN_WCMPGTSH,
  ARM_BUILTIN_WCMPGTSW,

  ARM_BUILTIN_TEXTRMSB,
  ARM_BUILTIN_TEXTRMSH,
  ARM_BUILTIN_TEXTRMSW,
  ARM_BUILTIN_TEXTRMUB,
  ARM_BUILTIN_TEXTRMUH,
  ARM_BUILTIN_TEXTRMUW,
  ARM_BUILTIN_TINSRB,
  ARM_BUILTIN_TINSRH,
  ARM_BUILTIN_TINSRW,

  ARM_BUILTIN_WMAXSW,
  ARM_BUILTIN_WMAXSH,
  ARM_BUILTIN_WMAXSB,
  ARM_BUILTIN_WMAXUW,
  ARM_BUILTIN_WMAXUH,
  ARM_BUILTIN_WMAXUB,
  ARM_BUILTIN_WMINSW,
  ARM_BUILTIN_WMINSH,
  ARM_BUILTIN_WMINSB,
  ARM_BUILTIN_WMINUW,
  ARM_BUILTIN_WMINUH,
  ARM_BUILTIN_WMINUB,

  ARM_BUILTIN_WMULUM,
  ARM_BUILTIN_WMULSM,
  ARM_BUILTIN_WMULUL,

  ARM_BUILTIN_PSADBH,
  ARM_BUILTIN_WSHUFH,

  ARM_BUILTIN_WSLLH,
  ARM_BUILTIN_WSLLW,
  ARM_BUILTIN_WSLLD,
  ARM_BUILTIN_WSRAH,
  ARM_BUILTIN_WSRAW,
  ARM_BUILTIN_WSRAD,
  ARM_BUILTIN_WSRLH,
  ARM_BUILTIN_WSRLW,
  ARM_BUILTIN_WSRLD,
  ARM_BUILTIN_WRORH,
  ARM_BUILTIN_WRORW,
  ARM_BUILTIN_WRORD,
  ARM_BUILTIN_WSLLHI,
  ARM_BUILTIN_WSLLWI,
  ARM_BUILTIN_WSLLDI,
  ARM_BUILTIN_WSRAHI,
  ARM_BUILTIN_WSRAWI,
  ARM_BUILTIN_WSRADI,
  ARM_BUILTIN_WSRLHI,
  ARM_BUILTIN_WSRLWI,
  ARM_BUILTIN_WSRLDI,
  ARM_BUILTIN_WRORHI,
  ARM_BUILTIN_WRORWI,
  ARM_BUILTIN_WRORDI,

  ARM_BUILTIN_WUNPCKIHB,
  ARM_BUILTIN_WUNPCKIHH,
  ARM_BUILTIN_WUNPCKIHW,
  ARM_BUILTIN_WUNPCKILB,
  ARM_BUILTIN_WUNPCKILH,
  ARM_BUILTIN_WUNPCKILW,

  ARM_BUILTIN_WUNPCKEHSB,
  ARM_BUILTIN_WUNPCKEHSH,
  ARM_BUILTIN_WUNPCKEHSW,
  ARM_BUILTIN_WUNPCKEHUB,
  ARM_BUILTIN_WUNPCKEHUH,
  ARM_BUILTIN_WUNPCKEHUW,
  ARM_BUILTIN_WUNPCKELSB,
  ARM_BUILTIN_WUNPCKELSH,
  ARM_BUILTIN_WUNPCKELSW,
  ARM_BUILTIN_WUNPCKELUB,
  ARM_BUILTIN_WUNPCKELUH,
  ARM_BUILTIN_WUNPCKELUW,

  ARM_BUILTIN_THREAD_POINTER,

  ARM_BUILTIN_MAX
};
#endif /* ! GCC_ARM_H */
