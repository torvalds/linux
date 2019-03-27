/* Definitions of target machine for GNU compiler.  MIPS version.
   Copyright (C) 1989, 1990, 1991, 1992, 1993, 1994, 1995, 1996, 1997, 1998
   1999, 2000, 2001, 2002, 2003, 2004, 2005 Free Software Foundation, Inc.
   Contributed by A. Lichnewsky (lich@inria.inria.fr).
   Changed by Michael Meissner	(meissner@osf.org).
   64 bit r4000 support by Ian Lance Taylor (ian@cygnus.com) and
   Brendan Eich (brendan@microunity.com).

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


/* MIPS external variables defined in mips.c.  */

/* Which processor to schedule for.  Since there is no difference between
   a R2000 and R3000 in terms of the scheduler, we collapse them into
   just an R3000.  The elements of the enumeration must match exactly
   the cpu attribute in the mips.md machine description.  */

enum processor_type {
  PROCESSOR_R3000,
  PROCESSOR_4KC,
  PROCESSOR_4KP,
  PROCESSOR_5KC,
  PROCESSOR_5KF,
  PROCESSOR_20KC,
  PROCESSOR_24K,
  PROCESSOR_24KX,
  PROCESSOR_M4K,
  PROCESSOR_OCTEON,
  PROCESSOR_R3900,
  PROCESSOR_R6000,
  PROCESSOR_R4000,
  PROCESSOR_R4100,
  PROCESSOR_R4111,
  PROCESSOR_R4120,
  PROCESSOR_R4130,
  PROCESSOR_R4300,
  PROCESSOR_R4600,
  PROCESSOR_R4650,
  PROCESSOR_R5000,
  PROCESSOR_R5400,
  PROCESSOR_R5500,
  PROCESSOR_R7000,
  PROCESSOR_R8000,
  PROCESSOR_R9000,
  PROCESSOR_SB1,
  PROCESSOR_SB1A,
  PROCESSOR_SR71000,
  PROCESSOR_MAX
};

/* Costs of various operations on the different architectures.  */

struct mips_rtx_cost_data
{
  unsigned short fp_add;
  unsigned short fp_mult_sf;
  unsigned short fp_mult_df;
  unsigned short fp_div_sf;
  unsigned short fp_div_df;
  unsigned short int_mult_si;
  unsigned short int_mult_di;
  unsigned short int_div_si;
  unsigned short int_div_di;
  unsigned short branch_cost;
  unsigned short memory_latency;
};

/* Which ABI to use.  ABI_32 (original 32, or o32), ABI_N32 (n32),
   ABI_64 (n64) are all defined by SGI.  ABI_O64 is o32 extended
   to work on a 64 bit machine.  */

#define ABI_32  0
#define ABI_N32 1
#define ABI_64  2
#define ABI_EABI 3
#define ABI_O64  4

/* Information about one recognized processor.  Defined here for the
   benefit of TARGET_CPU_CPP_BUILTINS.  */
struct mips_cpu_info {
  /* The 'canonical' name of the processor as far as GCC is concerned.
     It's typically a manufacturer's prefix followed by a numerical
     designation.  It should be lower case.  */
  const char *name;

  /* The internal processor number that most closely matches this
     entry.  Several processors can have the same value, if there's no
     difference between them from GCC's point of view.  */
  enum processor_type cpu;

  /* The ISA level that the processor implements.  */
  int isa;
};

#ifndef USED_FOR_TARGET
extern char mips_print_operand_punct[256]; /* print_operand punctuation chars */
extern const char *current_function_file; /* filename current function is in */
extern int num_source_filenames;	/* current .file # */
extern int mips_section_threshold;	/* # bytes of data/sdata cutoff */
extern int sym_lineno;			/* sgi next label # for each stmt */
extern int set_noreorder;		/* # of nested .set noreorder's  */
extern int set_nomacro;			/* # of nested .set nomacro's  */
extern int set_noat;			/* # of nested .set noat's  */
extern int set_volatile;		/* # of nested .set volatile's  */
extern int mips_branch_likely;		/* emit 'l' after br (branch likely) */
extern int mips_dbx_regno[];		/* Map register # to debug register # */
extern bool mips_split_p[];
extern GTY(()) rtx cmp_operands[2];
extern enum processor_type mips_arch;   /* which cpu to codegen for */
extern enum processor_type mips_tune;   /* which cpu to schedule for */
extern int mips_isa;			/* architectural level */
extern int mips_abi;			/* which ABI to use */
extern int mips16_hard_float;		/* mips16 without -msoft-float */
extern const struct mips_cpu_info mips_cpu_info_table[];
extern const struct mips_cpu_info *mips_arch_info;
extern const struct mips_cpu_info *mips_tune_info;
extern const struct mips_rtx_cost_data *mips_cost;
#endif

/* Macros to silence warnings about numbers being signed in traditional
   C and unsigned in ISO C when compiled on 32-bit hosts.  */

#define BITMASK_HIGH	(((unsigned long)1) << 31)	/* 0x80000000 */
#define BITMASK_UPPER16	((unsigned long)0xffff << 16)	/* 0xffff0000 */
#define BITMASK_LOWER16	((unsigned long)0xffff)		/* 0x0000ffff */


/* Run-time compilation parameters selecting different hardware subsets.  */

/* True if the call patterns should be split into a jalr followed by
   an instruction to restore $gp.  This is only ever true for SVR4 PIC,
   in which $gp is call-clobbered.  It is only safe to split the load
   from the call when every use of $gp is explicit.  */

#define TARGET_SPLIT_CALLS \
  (TARGET_EXPLICIT_RELOCS && TARGET_ABICALLS && !TARGET_NEWABI)

/* True if we're generating a form of -mabicalls in which we can use
   operators like %hi and %lo to refer to locally-binding symbols.
   We can only do this for -mno-shared, and only then if we can use
   relocation operations instead of assembly macros.  It isn't really
   worth using absolute sequences for 64-bit symbols because GOT
   accesses are so much shorter.  */

#define TARGET_ABSOLUTE_ABICALLS	\
  (TARGET_ABICALLS			\
   && !TARGET_SHARED			\
   && TARGET_EXPLICIT_RELOCS		\
   && !ABI_HAS_64BIT_SYMBOLS)

/* True if we can optimize sibling calls.  For simplicity, we only
   handle cases in which call_insn_operand will reject invalid
   sibcall addresses.  There are two cases in which this isn't true:

      - TARGET_MIPS16.  call_insn_operand accepts constant addresses
	but there is no direct jump instruction.  It isn't worth
	using sibling calls in this case anyway; they would usually
	be longer than normal calls.

      - TARGET_ABICALLS && !TARGET_EXPLICIT_RELOCS.  call_insn_operand
	accepts global constants, but "jr $25" is the only allowed
	sibcall.  */

#define TARGET_SIBCALLS \
  (!TARGET_MIPS16 && (!TARGET_ABICALLS || TARGET_EXPLICIT_RELOCS))

/* True if .gpword or .gpdword should be used for switch tables.

   Although GAS does understand .gpdword, the SGI linker mishandles
   the relocations GAS generates (R_MIPS_GPREL32 followed by R_MIPS_64).
   We therefore disable GP-relative switch tables for n64 on IRIX targets.  */
#define TARGET_GPWORD (TARGET_ABICALLS && !(mips_abi == ABI_64 && TARGET_IRIX))

/* Generate mips16 code */
#define TARGET_MIPS16		((target_flags & MASK_MIPS16) != 0)
/* Generate mips16e code. Default 16bit ASE for mips32/mips32r2/mips64 */
#define GENERATE_MIPS16E	(TARGET_MIPS16 && mips_isa >= 32)

/* Generic ISA defines.  */
#define ISA_MIPS1		    (mips_isa == 1)
#define ISA_MIPS2		    (mips_isa == 2)
#define ISA_MIPS3                   (mips_isa == 3)
#define ISA_MIPS4		    (mips_isa == 4)
#define ISA_MIPS32		    (mips_isa == 32)
#define ISA_MIPS32R2		    (mips_isa == 33)
#define ISA_MIPS64                  (mips_isa == 64)
#define	ISA_MIPS64R2		    (mips_isa == 65)

/* Architecture target defines.  */
#define TARGET_MIPS3900             (mips_arch == PROCESSOR_R3900)
#define TARGET_MIPS4000             (mips_arch == PROCESSOR_R4000)
#define TARGET_MIPS4120             (mips_arch == PROCESSOR_R4120)
#define TARGET_MIPS4130             (mips_arch == PROCESSOR_R4130)
#define TARGET_MIPS5400             (mips_arch == PROCESSOR_R5400)
#define TARGET_MIPS5500             (mips_arch == PROCESSOR_R5500)
#define TARGET_MIPS7000             (mips_arch == PROCESSOR_R7000)
#define TARGET_MIPS9000             (mips_arch == PROCESSOR_R9000)
#define TARGET_SB1                  (mips_arch == PROCESSOR_SB1		\
				     || mips_arch == PROCESSOR_SB1A)
#define TARGET_SR71K                (mips_arch == PROCESSOR_SR71000)
#define TARGET_OCTEON               (mips_arch == PROCESSOR_OCTEON)

/* Scheduling target defines.  */
#define TUNE_MIPS3000               (mips_tune == PROCESSOR_R3000)
#define TUNE_MIPS3900               (mips_tune == PROCESSOR_R3900)
#define TUNE_MIPS4000               (mips_tune == PROCESSOR_R4000)
#define TUNE_MIPS4120               (mips_tune == PROCESSOR_R4120)
#define TUNE_MIPS4130               (mips_tune == PROCESSOR_R4130)
#define TUNE_MIPS5000               (mips_tune == PROCESSOR_R5000)
#define TUNE_MIPS5400               (mips_tune == PROCESSOR_R5400)
#define TUNE_MIPS5500               (mips_tune == PROCESSOR_R5500)
#define TUNE_MIPS6000               (mips_tune == PROCESSOR_R6000)
#define TUNE_MIPS7000               (mips_tune == PROCESSOR_R7000)
#define TUNE_MIPS9000               (mips_tune == PROCESSOR_R9000)
#define TUNE_SB1                    (mips_tune == PROCESSOR_SB1		\
				     || mips_tune == PROCESSOR_SB1A)
#define TUNE_OCTEON                 (mips_tune == PROCESSOR_OCTEON)

/* True if the pre-reload scheduler should try to create chains of
   multiply-add or multiply-subtract instructions.  For example,
   suppose we have:

	t1 = a * b
	t2 = t1 + c * d
	t3 = e * f
	t4 = t3 - g * h

   t1 will have a higher priority than t2 and t3 will have a higher
   priority than t4.  However, before reload, there is no dependence
   between t1 and t3, and they can often have similar priorities.
   The scheduler will then tend to prefer:

	t1 = a * b
	t3 = e * f
	t2 = t1 + c * d
	t4 = t3 - g * h

   which stops us from making full use of macc/madd-style instructions.
   This sort of situation occurs frequently in Fourier transforms and
   in unrolled loops.

   To counter this, the TUNE_MACC_CHAINS code will reorder the ready
   queue so that chained multiply-add and multiply-subtract instructions
   appear ahead of any other instruction that is likely to clobber lo.
   In the example above, if t2 and t3 become ready at the same time,
   the code ensures that t2 is scheduled first.

   Multiply-accumulate instructions are a bigger win for some targets
   than others, so this macro is defined on an opt-in basis.  */
#define TUNE_MACC_CHAINS	    (TUNE_MIPS5500		\
				     || TUNE_MIPS4120		\
				     || TUNE_MIPS4130)

#define TARGET_OLDABI		    (mips_abi == ABI_32 || mips_abi == ABI_O64)
#define TARGET_NEWABI		    (mips_abi == ABI_N32 || mips_abi == ABI_64)

/* IRIX specific stuff.  */
#define TARGET_IRIX	   0
#define TARGET_IRIX6	   0

/* Define preprocessor macros for the -march and -mtune options.
   PREFIX is either _MIPS_ARCH or _MIPS_TUNE, INFO is the selected
   processor.  If INFO's canonical name is "foo", define PREFIX to
   be "foo", and define an additional macro PREFIX_FOO.  */
#define MIPS_CPP_SET_PROCESSOR(PREFIX, INFO)			\
  do								\
    {								\
      char *macro, *p;						\
								\
      macro = concat ((PREFIX), "_", (INFO)->name, NULL);	\
      for (p = macro; *p != 0; p++)				\
	if (*p == '+')						\
	  *p = 'P';						\
	else							\
	  *p = TOUPPER (*p);					\
								\
      builtin_define (macro);					\
      builtin_define_with_value ((PREFIX), (INFO)->name, 1);	\
      free (macro);						\
    }								\
  while (0)

/* Target CPU builtins.  */
#define TARGET_CPU_CPP_BUILTINS()				\
  do								\
    {								\
      /* Everyone but IRIX defines this to mips.  */            \
      if (!TARGET_IRIX)                                         \
        builtin_assert ("machine=mips");                        \
                                                                \
      builtin_assert ("cpu=mips");				\
      builtin_define ("__mips__");     				\
      builtin_define ("_mips");					\
								\
      /* We do this here because __mips is defined below	\
	 and so we can't use builtin_define_std.  */		\
      if (!flag_iso)						\
	builtin_define ("mips");				\
								\
      if (TARGET_64BIT)						\
	builtin_define ("__mips64");				\
								\
      if (!TARGET_IRIX)						\
	{							\
	  /* Treat _R3000 and _R4000 like register-size		\
	     defines, which is how they've historically		\
	     been used.  */					\
	  if (TARGET_64BIT)					\
	    {							\
	      builtin_define_std ("R4000");			\
	      builtin_define ("_R4000");			\
	    }							\
	  else							\
	    {							\
	      builtin_define_std ("R3000");			\
	      builtin_define ("_R3000");			\
	    }							\
	}							\
      if (TARGET_FLOAT64)					\
	builtin_define ("__mips_fpr=64");			\
      else							\
	builtin_define ("__mips_fpr=32");			\
								\
      if (TARGET_MIPS16)					\
	builtin_define ("__mips16");				\
								\
      if (TARGET_MIPS3D)					\
	builtin_define ("__mips3d");				\
								\
      if (TARGET_DSP)						\
	builtin_define ("__mips_dsp");				\
								\
      MIPS_CPP_SET_PROCESSOR ("_MIPS_ARCH", mips_arch_info);	\
      MIPS_CPP_SET_PROCESSOR ("_MIPS_TUNE", mips_tune_info);	\
								\
      if (ISA_MIPS1)						\
	{							\
	  builtin_define ("__mips=1");				\
	  builtin_define ("_MIPS_ISA=_MIPS_ISA_MIPS1");		\
	}							\
      else if (ISA_MIPS2)					\
	{							\
	  builtin_define ("__mips=2");				\
	  builtin_define ("_MIPS_ISA=_MIPS_ISA_MIPS2");		\
	}							\
      else if (ISA_MIPS3)					\
	{							\
	  builtin_define ("__mips=3");				\
	  builtin_define ("_MIPS_ISA=_MIPS_ISA_MIPS3");		\
	}							\
      else if (ISA_MIPS4)					\
	{							\
	  builtin_define ("__mips=4");				\
	  builtin_define ("_MIPS_ISA=_MIPS_ISA_MIPS4");		\
	}							\
      else if (ISA_MIPS32)					\
	{							\
	  builtin_define ("__mips=32");				\
	  builtin_define ("__mips_isa_rev=1");			\
	  builtin_define ("_MIPS_ISA=_MIPS_ISA_MIPS32");	\
	}							\
      else if (ISA_MIPS32R2)					\
	{							\
	  builtin_define ("__mips=32");				\
	  builtin_define ("__mips_isa_rev=2");			\
	  builtin_define ("_MIPS_ISA=_MIPS_ISA_MIPS32");	\
	}							\
      else if (ISA_MIPS64)					\
	{							\
	  builtin_define ("__mips=64");				\
	  builtin_define ("__mips_isa_rev=1");			\
	  builtin_define ("_MIPS_ISA=_MIPS_ISA_MIPS64");	\
	}							\
      else if (ISA_MIPS64R2)					\
	{							\
	  builtin_define ("__mips=64");				\
	  builtin_define ("__mips_isa_rev=2");			\
	  builtin_define ("_MIPS_ISA=_MIPS_ISA_MIPS64");	\
	}							\
								\
      if (TARGET_HARD_FLOAT)					\
	builtin_define ("__mips_hard_float");			\
      else if (TARGET_SOFT_FLOAT)				\
	builtin_define ("__mips_soft_float");			\
								\
      if (TARGET_SINGLE_FLOAT)					\
	builtin_define ("__mips_single_float");			\
								\
      if (TARGET_PAIRED_SINGLE_FLOAT)				\
	builtin_define ("__mips_paired_single_float");		\
								\
      if (TARGET_BIG_ENDIAN)					\
	{							\
	  builtin_define_std ("MIPSEB");			\
	  builtin_define ("_MIPSEB");				\
	}							\
      else							\
	{							\
	  builtin_define_std ("MIPSEL");			\
	  builtin_define ("_MIPSEL");				\
	}							\
								\
        /* Macros dependent on the C dialect.  */		\
      if (preprocessing_asm_p ())				\
	{							\
          builtin_define_std ("LANGUAGE_ASSEMBLY");		\
	  builtin_define ("_LANGUAGE_ASSEMBLY");		\
	}							\
      else if (c_dialect_cxx ())				\
        {							\
	  builtin_define ("_LANGUAGE_C_PLUS_PLUS");		\
          builtin_define ("__LANGUAGE_C_PLUS_PLUS");		\
          builtin_define ("__LANGUAGE_C_PLUS_PLUS__");		\
        }							\
      else							\
	{							\
          builtin_define_std ("LANGUAGE_C");			\
	  builtin_define ("_LANGUAGE_C");			\
	}							\
      if (c_dialect_objc ())					\
        {							\
	  builtin_define ("_LANGUAGE_OBJECTIVE_C");		\
          builtin_define ("__LANGUAGE_OBJECTIVE_C");		\
	  /* Bizarre, but needed at least for Irix.  */		\
	  builtin_define_std ("LANGUAGE_C");			\
	  builtin_define ("_LANGUAGE_C");			\
        }							\
								\
      if (mips_abi == ABI_EABI)					\
	builtin_define ("__mips_eabi");				\
								\
} while (0)

/* Default target_flags if no switches are specified  */

#ifndef TARGET_DEFAULT
#define TARGET_DEFAULT 0
#endif

#ifndef TARGET_CPU_DEFAULT
#define TARGET_CPU_DEFAULT 0
#endif

#ifndef TARGET_ENDIAN_DEFAULT
#define TARGET_ENDIAN_DEFAULT MASK_BIG_ENDIAN
#endif

#ifndef TARGET_FP_EXCEPTIONS_DEFAULT
#define TARGET_FP_EXCEPTIONS_DEFAULT MASK_FP_EXCEPTIONS
#endif

/* 'from-abi' makes a good default: you get whatever the ABI requires.  */
#ifndef MIPS_ISA_DEFAULT
#ifndef MIPS_CPU_STRING_DEFAULT
#define MIPS_CPU_STRING_DEFAULT "from-abi"
#endif
#endif

#ifdef IN_LIBGCC2
#undef TARGET_64BIT
/* Make this compile time constant for libgcc2 */
#ifdef __mips64
#define TARGET_64BIT		1
#else
#define TARGET_64BIT		0
#endif
#endif /* IN_LIBGCC2 */

#define TARGET_LIBGCC_SDATA_SECTION ".sdata"

#ifndef MULTILIB_ENDIAN_DEFAULT
#if TARGET_ENDIAN_DEFAULT == 0
#define MULTILIB_ENDIAN_DEFAULT "EL"
#else
#define MULTILIB_ENDIAN_DEFAULT "EB"
#endif
#endif

#ifndef MULTILIB_ISA_DEFAULT
#  if MIPS_ISA_DEFAULT == 1
#    define MULTILIB_ISA_DEFAULT "mips1"
#  else
#    if MIPS_ISA_DEFAULT == 2
#      define MULTILIB_ISA_DEFAULT "mips2"
#    else
#      if MIPS_ISA_DEFAULT == 3
#        define MULTILIB_ISA_DEFAULT "mips3"
#      else
#        if MIPS_ISA_DEFAULT == 4
#          define MULTILIB_ISA_DEFAULT "mips4"
#        else
#          if MIPS_ISA_DEFAULT == 32
#            define MULTILIB_ISA_DEFAULT "mips32"
#          else
#            if MIPS_ISA_DEFAULT == 33
#              define MULTILIB_ISA_DEFAULT "mips32r2"
#            else
#              if MIPS_ISA_DEFAULT == 64
#                define MULTILIB_ISA_DEFAULT "mips64"
#              else
#                if MIPS_ISA_DEFAULT == 65
#                  define MULTILIB_ISA_DEFAULT "mips64r2"
#                else
#                  define MULTILIB_ISA_DEFAULT "mips1"
#                endif
#              endif
#            endif
#          endif
#        endif
#      endif
#    endif
#  endif
#endif

#ifndef MULTILIB_DEFAULTS
#define MULTILIB_DEFAULTS \
    { MULTILIB_ENDIAN_DEFAULT, MULTILIB_ISA_DEFAULT, MULTILIB_ABI_DEFAULT }
#endif

/* We must pass -EL to the linker by default for little endian embedded
   targets using linker scripts with a OUTPUT_FORMAT line.  Otherwise, the
   linker will default to using big-endian output files.  The OUTPUT_FORMAT
   line must be in the linker script, otherwise -EB/-EL will not work.  */

#ifndef ENDIAN_SPEC
#if TARGET_ENDIAN_DEFAULT == 0
#define ENDIAN_SPEC "%{!EB:%{!meb:-EL}} %{EB|meb:-EB}"
#else
#define ENDIAN_SPEC "%{!EL:%{!mel:-EB}} %{EL|mel:-EL}"
#endif
#endif

/* Support for a compile-time default CPU, et cetera.  The rules are:
   --with-arch is ignored if -march is specified or a -mips is specified
     (other than -mips16).
   --with-tune is ignored if -mtune is specified.
   --with-abi is ignored if -mabi is specified.
   --with-float is ignored if -mhard-float or -msoft-float are
     specified.
   --with-divide is ignored if -mdivide-traps or -mdivide-breaks are
     specified. */
#define OPTION_DEFAULT_SPECS \
  {"arch", "%{!march=*:%{mips16:-march=%(VALUE)}%{!mips*:-march=%(VALUE)}}" }, \
  {"tune", "%{!mtune=*:-mtune=%(VALUE)}" }, \
  {"abi", "%{!mabi=*:-mabi=%(VALUE)}" }, \
  {"float", "%{!msoft-float:%{!mhard-float:-m%(VALUE)-float}}" }, \
  {"divide", "%{!mdivide-traps:%{!mdivide-breaks:-mdivide-%(VALUE)}}" }


#define GENERATE_DIVIDE_TRAPS (TARGET_DIVIDE_TRAPS \
                               && ISA_HAS_COND_TRAP)

#define GENERATE_BRANCHLIKELY   (TARGET_BRANCHLIKELY                    \
				 && !TARGET_SR71K                       \
				 && !TARGET_MIPS16)

/* Generate three-operand multiply instructions for SImode.  */
#define GENERATE_MULT3_SI       ((TARGET_MIPS3900                       \
                                  || TARGET_MIPS5400                    \
                                  || TARGET_MIPS5500                    \
                                  || TARGET_MIPS7000                    \
                                  || TARGET_MIPS9000                    \
				  || TARGET_MAD				\
                                  || ISA_MIPS32	                        \
                                  || ISA_MIPS32R2                       \
                                  || ISA_MIPS64                         \
                                  || ISA_MIPS64R2)                      \
                                 && !TARGET_MIPS16)

/* Generate three-operand multiply instructions for DImode.  */
#define GENERATE_MULT3_DI       ((TARGET_MIPS3900)                      \
				 && !TARGET_MIPS16)

/* True if the ABI can only work with 64-bit integer registers.  We
   generally allow ad-hoc variations for TARGET_SINGLE_FLOAT, but
   otherwise floating-point registers must also be 64-bit.  */
#define ABI_NEEDS_64BIT_REGS	(TARGET_NEWABI || mips_abi == ABI_O64)

/* Likewise for 32-bit regs.  */
#define ABI_NEEDS_32BIT_REGS	(mips_abi == ABI_32)

/* True if symbols are 64 bits wide.  At present, n64 is the only
   ABI for which this is true.  */
#define ABI_HAS_64BIT_SYMBOLS	(mips_abi == ABI_64 && !TARGET_SYM32)

/* ISA has instructions for managing 64 bit fp and gp regs (e.g. mips3).  */
#define ISA_HAS_64BIT_REGS	(ISA_MIPS3				\
				 || ISA_MIPS4				\
                                 || ISA_MIPS64				\
				 || ISA_MIPS64R2)

/* ISA has branch likely instructions (e.g. mips2).  */
/* Disable branchlikely for tx39 until compare rewrite.  They haven't
   been generated up to this point.  */
#define ISA_HAS_BRANCHLIKELY	(!ISA_MIPS1)

/* ISA has the conditional move instructions introduced in mips4.  */
#define ISA_HAS_CONDMOVE        ((ISA_MIPS4				\
				  || ISA_MIPS32	                        \
				  || ISA_MIPS32R2                       \
				  || ISA_MIPS64				\
				  || ISA_MIPS64R2)			\
                                 && !TARGET_MIPS5500                    \
				 && !TARGET_MIPS16)

/* ISA has the mips4 FP condition code instructions: FP-compare to CC,
   branch on CC, and move (both FP and non-FP) on CC.  */
#define ISA_HAS_8CC		(ISA_MIPS4				\
                         	 || ISA_MIPS32	                        \
                         	 || ISA_MIPS32R2                        \
				 || ISA_MIPS64				\
				 || ISA_MIPS64R2)

/* This is a catch all for other mips4 instructions: indexed load, the
   FP madd and msub instructions, and the FP recip and recip sqrt
   instructions.  */
#define ISA_HAS_FP4             ((ISA_MIPS4				\
				  || ISA_MIPS64				\
				  || ISA_MIPS64R2)     			\
 				 && !TARGET_MIPS16)

/* ISA has conditional trap instructions.  */
#define ISA_HAS_COND_TRAP	(!ISA_MIPS1				\
				 && !TARGET_MIPS16)

/* ISA has integer multiply-accumulate instructions, madd and msub.  */
#define ISA_HAS_MADD_MSUB       ((ISA_MIPS32				\
				  || ISA_MIPS32R2			\
				  || ISA_MIPS64				\
				  || ISA_MIPS64R2			\
				  ) && !TARGET_MIPS16)

/* ISA has floating-point nmadd and nmsub instructions.  */
#define ISA_HAS_NMADD_NMSUB	((ISA_MIPS4				\
				  || ISA_MIPS64				\
				  || ISA_MIPS64R2)			\
                                 && (!TARGET_MIPS5400 || TARGET_MAD)    \
				 && ! TARGET_MIPS16)

/* ISA has count leading zeroes/ones instruction (not implemented).  */
#define ISA_HAS_CLZ_CLO         ((ISA_MIPS32				\
                                  || ISA_MIPS32R2			\
                                  || ISA_MIPS64				\
                                  || ISA_MIPS64R2			\
                                 ) && !TARGET_MIPS16)

/* ISA has double-word count leading zeroes/ones instruction (not
   implemented).  */
#define ISA_HAS_DCLZ_DCLO       (ISA_MIPS64				\
				 || ISA_MIPS64R2			\
				 && !TARGET_MIPS16)

/* ISA has three operand multiply instructions that put
   the high part in an accumulator: mulhi or mulhiu.  */
#define ISA_HAS_MULHI           (TARGET_MIPS5400                        \
                                 || TARGET_MIPS5500                     \
                                 || TARGET_SR71K                        \
                                 )

/* ISA has three operand multiply instructions that
   negates the result and puts the result in an accumulator.  */
#define ISA_HAS_MULS            (TARGET_MIPS5400                        \
                                 || TARGET_MIPS5500                     \
                                 || TARGET_SR71K                        \
                                 )

/* ISA has three operand multiply instructions that subtracts the
   result from a 4th operand and puts the result in an accumulator.  */
#define ISA_HAS_MSAC            (TARGET_MIPS5400                        \
                                 || TARGET_MIPS5500                     \
                                 || TARGET_SR71K                        \
                                 )
/* ISA has three operand multiply instructions that  the result
   from a 4th operand and puts the result in an accumulator.  */
#define ISA_HAS_MACC            ((TARGET_MIPS4120 && !TARGET_MIPS16)	\
                                 || (TARGET_MIPS4130 && !TARGET_MIPS16)	\
                                 || TARGET_MIPS5400                     \
                                 || TARGET_MIPS5500                     \
                                 || TARGET_SR71K                        \
                                 )

/* ISA has NEC VR-style MACC, MACCHI, DMACC and DMACCHI instructions.  */
#define ISA_HAS_MACCHI		(!TARGET_MIPS16				\
				 && (TARGET_MIPS4120			\
				     || TARGET_MIPS4130))

/* ISA has 32-bit rotate right instruction.  */
#define ISA_HAS_ROTR_SI         (!TARGET_MIPS16                         \
                                 && (ISA_MIPS32R2                       \
                                     || ISA_MIPS64R2                    \
                                     || TARGET_MIPS5400                 \
                                     || TARGET_MIPS5500                 \
                                     || TARGET_SR71K                    \
                                     ))

/* ISA has 64-bit rotate right instruction.  */
#define ISA_HAS_ROTR_DI         (TARGET_64BIT                           \
                                 && !TARGET_MIPS16                      \
                                 && (TARGET_MIPS5400                    \
                                     || TARGET_MIPS5500                 \
                                     || TARGET_SR71K                    \
                                     ))

/* ISA has data prefetch instructions.  This controls use of 'pref'.  */
#define ISA_HAS_PREFETCH	((ISA_MIPS4				\
				  || ISA_MIPS32				\
				  || ISA_MIPS32R2			\
				  || ISA_MIPS64		       		\
				  || ISA_MIPS64R2)	       		\
				 && !TARGET_MIPS16)

/* ISA has data indexed prefetch instructions.  This controls use of
   'prefx', along with TARGET_HARD_FLOAT and TARGET_DOUBLE_FLOAT.
   (prefx is a cop1x instruction, so can only be used if FP is
   enabled.)  */
#define ISA_HAS_PREFETCHX       ((ISA_MIPS4				\
				  || ISA_MIPS64				\
				  || ISA_MIPS64R2)			\
 				 && !TARGET_MIPS16)

/* True if trunc.w.s and trunc.w.d are real (not synthetic)
   instructions.  Both require TARGET_HARD_FLOAT, and trunc.w.d
   also requires TARGET_DOUBLE_FLOAT.  */
#define ISA_HAS_TRUNC_W		(!ISA_MIPS1)

/* ISA includes the MIPS32r2 seb and seh instructions.  */
#define ISA_HAS_SEB_SEH         (!TARGET_MIPS16                        \
                                 && (ISA_MIPS32R2                      \
				     || ISA_MIPS64R2			\
                                     ))

/* ISA includes the MIPS32/64 rev 2 ext and ins instructions.  */
#define ISA_HAS_EXT_INS         (!TARGET_MIPS16                        \
                                 && (ISA_MIPS32R2                      \
				     || ISA_MIPS64R2		       \
                                     ))

/* True if the result of a load is not available to the next instruction.
   A nop will then be needed between instructions like "lw $4,..."
   and "addiu $4,$4,1".  */
#define ISA_HAS_LOAD_DELAY	(mips_isa == 1				\
				 && !TARGET_MIPS3900			\
				 && !TARGET_MIPS16)

/* Likewise mtc1 and mfc1.  */
#define ISA_HAS_XFER_DELAY	(mips_isa <= 3)

/* Likewise floating-point comparisons.  */
#define ISA_HAS_FCMP_DELAY	(mips_isa <= 3)

/* True if mflo and mfhi can be immediately followed by instructions
   which write to the HI and LO registers.

   According to MIPS specifications, MIPS ISAs I, II, and III need
   (at least) two instructions between the reads of HI/LO and
   instructions which write them, and later ISAs do not.  Contradicting
   the MIPS specifications, some MIPS IV processor user manuals (e.g.
   the UM for the NEC Vr5000) document needing the instructions between
   HI/LO reads and writes, as well.  Therefore, we declare only MIPS32,
   MIPS64 and later ISAs to have the interlocks, plus any specific
   earlier-ISA CPUs for which CPU documentation declares that the
   instructions are really interlocked.  */
#define ISA_HAS_HILO_INTERLOCKS	(ISA_MIPS32				\
				 || ISA_MIPS32R2			\
				 || ISA_MIPS64				\
				 || ISA_MIPS64R2			\
				 || TARGET_MIPS5500)

/* Add -G xx support.  */

#undef  SWITCH_TAKES_ARG
#define SWITCH_TAKES_ARG(CHAR)						\
  (DEFAULT_SWITCH_TAKES_ARG (CHAR) || (CHAR) == 'G')

#define OVERRIDE_OPTIONS override_options ()

#define CONDITIONAL_REGISTER_USAGE mips_conditional_register_usage ()

/* Show we can debug even without a frame pointer.  */
#define CAN_DEBUG_WITHOUT_FP

/* Tell collect what flags to pass to nm.  */
#ifndef NM_FLAGS
#define NM_FLAGS "-Bn"
#endif


#ifndef MIPS_ABI_DEFAULT
#define MIPS_ABI_DEFAULT ABI_32
#endif

/* Use the most portable ABI flag for the ASM specs.  */

#if MIPS_ABI_DEFAULT == ABI_32
#define MULTILIB_ABI_DEFAULT "mabi=32"
#endif

#if MIPS_ABI_DEFAULT == ABI_O64
#define MULTILIB_ABI_DEFAULT "mabi=o64"
#endif

#if MIPS_ABI_DEFAULT == ABI_N32
#define MULTILIB_ABI_DEFAULT "mabi=n32"
#endif

#if MIPS_ABI_DEFAULT == ABI_64
#define MULTILIB_ABI_DEFAULT "mabi=64"
#endif

#if MIPS_ABI_DEFAULT == ABI_EABI
#define MULTILIB_ABI_DEFAULT "mabi=eabi"
#endif

/* SUBTARGET_ASM_OPTIMIZING_SPEC handles passing optimization options
   to the assembler.  It may be overridden by subtargets.  */
#ifndef SUBTARGET_ASM_OPTIMIZING_SPEC
#define SUBTARGET_ASM_OPTIMIZING_SPEC "\
%{noasmopt:-O0} \
%{!noasmopt:%{O:-O2} %{O1:-O2} %{O2:-O2} %{O3:-O3}}"
#endif

/* SUBTARGET_ASM_DEBUGGING_SPEC handles passing debugging options to
   the assembler.  It may be overridden by subtargets.

   Beginning with gas 2.13, -mdebug must be passed to correctly handle
   COFF debugging info.  */

#ifndef SUBTARGET_ASM_DEBUGGING_SPEC
#define SUBTARGET_ASM_DEBUGGING_SPEC "\
%{g} %{g0} %{g1} %{g2} %{g3} \
%{ggdb:-g} %{ggdb0:-g0} %{ggdb1:-g1} %{ggdb2:-g2} %{ggdb3:-g3} \
%{gstabs:-g} %{gstabs0:-g0} %{gstabs1:-g1} %{gstabs2:-g2} %{gstabs3:-g3} \
%{gstabs+:-g} %{gstabs+0:-g0} %{gstabs+1:-g1} %{gstabs+2:-g2} %{gstabs+3:-g3} \
%{gcoff:-g} %{gcoff0:-g0} %{gcoff1:-g1} %{gcoff2:-g2} %{gcoff3:-g3} \
%{gcoff*:-mdebug} %{!gcoff*:-no-mdebug}"
#endif

/* SUBTARGET_ASM_SPEC is always passed to the assembler.  It may be
   overridden by subtargets.  */

#ifndef SUBTARGET_ASM_SPEC
#define SUBTARGET_ASM_SPEC ""
#endif

#undef ASM_SPEC
#define ASM_SPEC "\
%{G*} %(endian_spec) %{mips1} %{mips2} %{mips3} %{mips4} \
%{mips32} %{mips32r2} %{mips64} \
%{mips16:%{!mno-mips16:-mips16}} %{mno-mips16:-no-mips16} \
%{mips3d:-mips3d} \
%{mdsp} \
%{mfix-vr4120} %{mfix-vr4130} \
%(subtarget_asm_optimizing_spec) \
%(subtarget_asm_debugging_spec) \
%{mabi=*} %{!mabi*: %(asm_abi_default_spec)} \
%{mgp32} %{mgp64} %{march=*} %{mxgot:-xgot} \
%{mshared} %{mno-shared} \
%{msym32} %{mno-sym32} \
%{mtune=*} %{v} \
%(subtarget_asm_spec)"

/* Extra switches sometimes passed to the linker.  */
/* ??? The bestGnum will never be passed to the linker, because the gcc driver
  will interpret it as a -b option.  */

#ifndef LINK_SPEC
#define LINK_SPEC "\
%(endian_spec) \
%{G*} %{mips1} %{mips2} %{mips3} %{mips4} %{mips32} %{mips32r2} %{mips64} \
%{bestGnum} %{shared} %{non_shared}"
#endif  /* LINK_SPEC defined */


/* Specs for the compiler proper */

/* SUBTARGET_CC1_SPEC is passed to the compiler proper.  It may be
   overridden by subtargets.  */
#ifndef SUBTARGET_CC1_SPEC
#define SUBTARGET_CC1_SPEC ""
#endif

/* CC1_SPEC is the set of arguments to pass to the compiler proper.  */

#undef CC1_SPEC
#define CC1_SPEC "\
%{gline:%{!g:%{!g0:%{!g1:%{!g2: -g1}}}}} \
%{G*} %{EB:-meb} %{EL:-mel} %{EB:%{EL:%emay not use both -EB and -EL}} \
%{save-temps: } \
%(subtarget_cc1_spec)"

/* Preprocessor specs.  */

/* SUBTARGET_CPP_SPEC is passed to the preprocessor.  It may be
   overridden by subtargets.  */
#ifndef SUBTARGET_CPP_SPEC
#define SUBTARGET_CPP_SPEC ""
#endif

#define CPP_SPEC "%(subtarget_cpp_spec)"

/* This macro defines names of additional specifications to put in the specs
   that can be used in various specifications like CC1_SPEC.  Its definition
   is an initializer with a subgrouping for each command option.

   Each subgrouping contains a string constant, that defines the
   specification name, and a string constant that used by the GCC driver
   program.

   Do not define this macro if it does not need to do anything.  */

#define EXTRA_SPECS							\
  { "subtarget_cc1_spec", SUBTARGET_CC1_SPEC },				\
  { "subtarget_cpp_spec", SUBTARGET_CPP_SPEC },				\
  { "subtarget_asm_optimizing_spec", SUBTARGET_ASM_OPTIMIZING_SPEC },	\
  { "subtarget_asm_debugging_spec", SUBTARGET_ASM_DEBUGGING_SPEC },	\
  { "subtarget_asm_spec", SUBTARGET_ASM_SPEC },				\
  { "asm_abi_default_spec", "-" MULTILIB_ABI_DEFAULT },			\
  { "endian_spec", ENDIAN_SPEC },					\
  SUBTARGET_EXTRA_SPECS

#ifndef SUBTARGET_EXTRA_SPECS
#define SUBTARGET_EXTRA_SPECS
#endif

#define DBX_DEBUGGING_INFO 1		/* generate stabs (OSF/rose) */
#define MIPS_DEBUGGING_INFO 1		/* MIPS specific debugging info */
#define DWARF2_DEBUGGING_INFO 1         /* dwarf2 debugging info */

#ifndef PREFERRED_DEBUGGING_TYPE
#define PREFERRED_DEBUGGING_TYPE DWARF2_DEBUG
#endif

#define DWARF2_ADDR_SIZE (ABI_HAS_64BIT_SYMBOLS ? 8 : 4)

/* By default, turn on GDB extensions.  */
#define DEFAULT_GDB_EXTENSIONS 1

/* Local compiler-generated symbols must have a prefix that the assembler
   understands.   By default, this is $, although some targets (e.g.,
   NetBSD-ELF) need to override this.  */

#ifndef LOCAL_LABEL_PREFIX
#define LOCAL_LABEL_PREFIX	"$"
#endif

/* By default on the mips, external symbols do not have an underscore
   prepended, but some targets (e.g., NetBSD) require this.  */

#ifndef USER_LABEL_PREFIX
#define USER_LABEL_PREFIX	""
#endif

/* On Sun 4, this limit is 2048.  We use 1500 to be safe,
   since the length can run past this up to a continuation point.  */
#undef DBX_CONTIN_LENGTH
#define DBX_CONTIN_LENGTH 1500

/* How to renumber registers for dbx and gdb.  */
#define DBX_REGISTER_NUMBER(REGNO) mips_dbx_regno[ (REGNO) ]

/* The mapping from gcc register number to DWARF 2 CFA column number.  */
#define DWARF_FRAME_REGNUM(REG)	(REG)

/* The DWARF 2 CFA column which tracks the return address.  */
#define DWARF_FRAME_RETURN_COLUMN (GP_REG_FIRST + 31)

/* The DWARF 2 CFA column which tracks the return address from a
   signal handler context.  */
#define SIGNAL_UNWIND_RETURN_COLUMN (FP_REG_LAST + 1)

/* Before the prologue, RA lives in r31.  */
#define INCOMING_RETURN_ADDR_RTX  gen_rtx_REG (VOIDmode, GP_REG_FIRST + 31)

/* Describe how we implement __builtin_eh_return.  */
#define EH_RETURN_DATA_REGNO(N) \
  ((N) < (TARGET_MIPS16 ? 2 : 4) ? (N) + GP_ARG_FIRST : INVALID_REGNUM)

#define EH_RETURN_STACKADJ_RTX  gen_rtx_REG (Pmode, GP_REG_FIRST + 3)

/* Offsets recorded in opcodes are a multiple of this alignment factor.
   The default for this in 64-bit mode is 8, which causes problems with
   SFmode register saves.  */
#define DWARF_CIE_DATA_ALIGNMENT -4

/* Correct the offset of automatic variables and arguments.  Note that
   the MIPS debug format wants all automatic variables and arguments
   to be in terms of the virtual frame pointer (stack pointer before
   any adjustment in the function), while the MIPS 3.0 linker wants
   the frame pointer to be the stack pointer after the initial
   adjustment.  */

#define DEBUGGER_AUTO_OFFSET(X)				\
  mips_debugger_offset (X, (HOST_WIDE_INT) 0)
#define DEBUGGER_ARG_OFFSET(OFFSET, X)			\
  mips_debugger_offset (X, (HOST_WIDE_INT) OFFSET)

/* Target machine storage layout */

#define BITS_BIG_ENDIAN 0
#define BYTES_BIG_ENDIAN (TARGET_BIG_ENDIAN != 0)
#define WORDS_BIG_ENDIAN (TARGET_BIG_ENDIAN != 0)

/* Define this to set the endianness to use in libgcc2.c, which can
   not depend on target_flags.  */
#if !defined(MIPSEL) && !defined(__MIPSEL__)
#define LIBGCC2_WORDS_BIG_ENDIAN 1
#else
#define LIBGCC2_WORDS_BIG_ENDIAN 0
#endif

#define MAX_BITS_PER_WORD 64

/* Width of a word, in units (bytes).  */
#define UNITS_PER_WORD (TARGET_64BIT ? 8 : 4)
#ifndef IN_LIBGCC2
#define MIN_UNITS_PER_WORD 4
#endif

/* For MIPS, width of a floating point register.  */
#define UNITS_PER_FPREG (TARGET_FLOAT64 ? 8 : 4)

/* If register $f0 holds a floating-point value, $f(0 + FP_INC) is
   the next available register.  */
#define FP_INC (TARGET_FLOAT64 || TARGET_SINGLE_FLOAT ? 1 : 2)

/* The largest size of value that can be held in floating-point
   registers and moved with a single instruction.  */
#define UNITS_PER_HWFPVALUE (TARGET_SOFT_FLOAT ? 0 : FP_INC * UNITS_PER_FPREG)

/* The largest size of value that can be held in floating-point
   registers.  */
#define UNITS_PER_FPVALUE			\
  (TARGET_SOFT_FLOAT ? 0			\
   : TARGET_SINGLE_FLOAT ? UNITS_PER_FPREG	\
   : LONG_DOUBLE_TYPE_SIZE / BITS_PER_UNIT)

/* The number of bytes in a double.  */
#define UNITS_PER_DOUBLE (TYPE_PRECISION (double_type_node) / BITS_PER_UNIT)

#define UNITS_PER_SIMD_WORD (TARGET_PAIRED_SINGLE_FLOAT ? 8 : UNITS_PER_WORD)

/* Set the sizes of the core types.  */
#define SHORT_TYPE_SIZE 16
#define INT_TYPE_SIZE 32
#define LONG_TYPE_SIZE (TARGET_LONG64 ? 64 : 32)
#define LONG_LONG_TYPE_SIZE 64

#define FLOAT_TYPE_SIZE 32
#define DOUBLE_TYPE_SIZE 64
#define LONG_DOUBLE_TYPE_SIZE (TARGET_NEWABI ? 128 : 64)

/* long double is not a fixed mode, but the idea is that, if we
   support long double, we also want a 128-bit integer type.  */
#define MAX_FIXED_MODE_SIZE LONG_DOUBLE_TYPE_SIZE

#ifdef IN_LIBGCC2
#if  (defined _ABIN32 && _MIPS_SIM == _ABIN32) \
  || (defined _ABI64 && _MIPS_SIM == _ABI64)
#  define LIBGCC2_LONG_DOUBLE_TYPE_SIZE 128
# else
#  define LIBGCC2_LONG_DOUBLE_TYPE_SIZE 64
# endif
#endif

/* Width in bits of a pointer.  */
#ifndef POINTER_SIZE
#define POINTER_SIZE ((TARGET_LONG64 && TARGET_64BIT) ? 64 : 32)
#endif

/* Allocation boundary (in *bits*) for storing arguments in argument list.  */
#define PARM_BOUNDARY BITS_PER_WORD

/* Allocation boundary (in *bits*) for the code of a function.  */
#define FUNCTION_BOUNDARY 32

/* Alignment of field after `int : 0' in a structure.  */
#define EMPTY_FIELD_BOUNDARY 32

/* Every structure's size must be a multiple of this.  */
/* 8 is observed right on a DECstation and on riscos 4.02.  */
#define STRUCTURE_SIZE_BOUNDARY 8

/* There is no point aligning anything to a rounder boundary than this.  */
#define BIGGEST_ALIGNMENT LONG_DOUBLE_TYPE_SIZE

/* All accesses must be aligned.  */
#define STRICT_ALIGNMENT 1

/* Define this if you wish to imitate the way many other C compilers
   handle alignment of bitfields and the structures that contain
   them.

   The behavior is that the type written for a bit-field (`int',
   `short', or other integer type) imposes an alignment for the
   entire structure, as if the structure really did contain an
   ordinary field of that type.  In addition, the bit-field is placed
   within the structure so that it would fit within such a field,
   not crossing a boundary for it.

   Thus, on most machines, a bit-field whose type is written as `int'
   would not cross a four-byte boundary, and would force four-byte
   alignment for the whole structure.  (The alignment used may not
   be four bytes; it is controlled by the other alignment
   parameters.)

   If the macro is defined, its definition should be a C expression;
   a nonzero value for the expression enables this behavior.  */

#define PCC_BITFIELD_TYPE_MATTERS 1

/* If defined, a C expression to compute the alignment given to a
   constant that is being placed in memory.  CONSTANT is the constant
   and ALIGN is the alignment that the object would ordinarily have.
   The value of this macro is used instead of that alignment to align
   the object.

   If this macro is not defined, then ALIGN is used.

   The typical use of this macro is to increase alignment for string
   constants to be word aligned so that `strcpy' calls that copy
   constants can be done inline.  */

#define CONSTANT_ALIGNMENT(EXP, ALIGN)					\
  ((TREE_CODE (EXP) == STRING_CST  || TREE_CODE (EXP) == CONSTRUCTOR)	\
   && (ALIGN) < BITS_PER_WORD ? BITS_PER_WORD : (ALIGN))

/* If defined, a C expression to compute the alignment for a static
   variable.  TYPE is the data type, and ALIGN is the alignment that
   the object would ordinarily have.  The value of this macro is used
   instead of that alignment to align the object.

   If this macro is not defined, then ALIGN is used.

   One use of this macro is to increase alignment of medium-size
   data to make it all fit in fewer cache lines.  Another is to
   cause character arrays to be word-aligned so that `strcpy' calls
   that copy constants to character arrays can be done inline.  */

#undef DATA_ALIGNMENT
#define DATA_ALIGNMENT(TYPE, ALIGN)					\
  ((((ALIGN) < BITS_PER_WORD)						\
    && (TREE_CODE (TYPE) == ARRAY_TYPE					\
	|| TREE_CODE (TYPE) == UNION_TYPE				\
	|| TREE_CODE (TYPE) == RECORD_TYPE)) ? BITS_PER_WORD : (ALIGN))


#define PAD_VARARGS_DOWN \
  (FUNCTION_ARG_PADDING (TYPE_MODE (type), type) == downward)

/* Define if operations between registers always perform the operation
   on the full register even if a narrower mode is specified.  */
#define WORD_REGISTER_OPERATIONS

/* When in 64 bit mode, move insns will sign extend SImode and CCmode
   moves.  All other references are zero extended.  */
#define LOAD_EXTEND_OP(MODE) \
  (TARGET_64BIT && ((MODE) == SImode || (MODE) == CCmode) \
   ? SIGN_EXTEND : ZERO_EXTEND)

/* Define this macro if it is advisable to hold scalars in registers
   in a wider mode than that declared by the program.  In such cases,
   the value is constrained to be within the bounds of the declared
   type, but kept valid in the wider mode.  The signedness of the
   extension may differ from that of the type.  */

#define PROMOTE_MODE(MODE, UNSIGNEDP, TYPE)	\
  if (GET_MODE_CLASS (MODE) == MODE_INT		\
      && GET_MODE_SIZE (MODE) < UNITS_PER_WORD) \
    {                                           \
      if ((MODE) == SImode)                     \
        (UNSIGNEDP) = 0;                        \
      (MODE) = Pmode;                           \
    }

/* Define if loading short immediate values into registers sign extends.  */
#define SHORT_IMMEDIATES_SIGN_EXTEND

/* The [d]clz instructions have the natural values at 0.  */

#define CLZ_DEFINED_VALUE_AT_ZERO(MODE, VALUE) \
  ((VALUE) = GET_MODE_BITSIZE (MODE), true)

/* Standard register usage.  */

/* Number of hardware registers.  We have:

   - 32 integer registers
   - 32 floating point registers
   - 8 condition code registers
   - 2 accumulator registers (hi and lo)
   - 32 registers each for coprocessors 0, 2 and 3
   - 3 fake registers:
	- ARG_POINTER_REGNUM
	- FRAME_POINTER_REGNUM
	- FAKE_CALL_REGNO (see the comment above load_callsi for details)
   - 3 dummy entries that were used at various times in the past.
   - 6 DSP accumulator registers (3 hi-lo pairs) for MIPS DSP ASE
   - 6 DSP control registers  */

#define FIRST_PSEUDO_REGISTER 188

/* By default, fix the kernel registers ($26 and $27), the global
   pointer ($28) and the stack pointer ($29).  This can change
   depending on the command-line options.

   Regarding coprocessor registers: without evidence to the contrary,
   it's best to assume that each coprocessor register has a unique
   use.  This can be overridden, in, e.g., override_options() or
   CONDITIONAL_REGISTER_USAGE should the assumption be inappropriate
   for a particular target.  */

#define FIXED_REGISTERS							\
{									\
  1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,			\
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0,			\
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,			\
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,			\
  0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1,			\
  /* COP0 registers */							\
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,			\
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,			\
  /* COP2 registers */							\
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,			\
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,			\
  /* COP3 registers */							\
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,			\
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,			\
  /* 6 DSP accumulator registers & 6 control registers */		\
  0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1					\
}


/* Set up this array for o32 by default.

   Note that we don't mark $31 as a call-clobbered register.  The idea is
   that it's really the call instructions themselves which clobber $31.
   We don't care what the called function does with it afterwards.

   This approach makes it easier to implement sibcalls.  Unlike normal
   calls, sibcalls don't clobber $31, so the register reaches the
   called function in tact.  EPILOGUE_USES says that $31 is useful
   to the called function.  */

#define CALL_USED_REGISTERS						\
{									\
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,			\
  0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 0, 0,			\
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,			\
  1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,			\
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,			\
  /* COP0 registers */							\
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,			\
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,			\
  /* COP2 registers */							\
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,			\
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,			\
  /* COP3 registers */							\
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,			\
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,			\
  /* 6 DSP accumulator registers & 6 control registers */		\
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1					\
}


/* Define this since $28, though fixed, is call-saved in many ABIs.  */

#define CALL_REALLY_USED_REGISTERS                                      \
{ /* General registers.  */                                             \
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,                       \
  0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 0, 1, 0, 0,                       \
  /* Floating-point registers.  */                                      \
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,			\
  1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,			\
  /* Others.  */                                                        \
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,			\
  /* COP0 registers */							\
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,			\
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,			\
  /* COP2 registers */							\
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,			\
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,			\
  /* COP3 registers */							\
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,			\
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,			\
  /* 6 DSP accumulator registers & 6 control registers */		\
  1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0					\
}

/* Internal macros to classify a register number as to whether it's a
   general purpose register, a floating point register, a
   multiply/divide register, or a status register.  */

#define GP_REG_FIRST 0
#define GP_REG_LAST  31
#define GP_REG_NUM   (GP_REG_LAST - GP_REG_FIRST + 1)
#define GP_DBX_FIRST 0

#define FP_REG_FIRST 32
#define FP_REG_LAST  63
#define FP_REG_NUM   (FP_REG_LAST - FP_REG_FIRST + 1)
#define FP_DBX_FIRST ((write_symbols == DBX_DEBUG) ? 38 : 32)

#define MD_REG_FIRST 64
#define MD_REG_LAST  65
#define MD_REG_NUM   (MD_REG_LAST - MD_REG_FIRST + 1)
#define MD_DBX_FIRST (FP_DBX_FIRST + FP_REG_NUM)

#define ST_REG_FIRST 67
#define ST_REG_LAST  74
#define ST_REG_NUM   (ST_REG_LAST - ST_REG_FIRST + 1)


/* FIXME: renumber.  */
#define COP0_REG_FIRST 80
#define COP0_REG_LAST 111
#define COP0_REG_NUM (COP0_REG_LAST - COP0_REG_FIRST + 1)

#define COP2_REG_FIRST 112
#define COP2_REG_LAST 143
#define COP2_REG_NUM (COP2_REG_LAST - COP2_REG_FIRST + 1)

#define COP3_REG_FIRST 144
#define COP3_REG_LAST 175
#define COP3_REG_NUM (COP3_REG_LAST - COP3_REG_FIRST + 1)
/* ALL_COP_REG_NUM assumes that COP0,2,and 3 are numbered consecutively.  */
#define ALL_COP_REG_NUM (COP3_REG_LAST - COP0_REG_FIRST + 1)

#define DSP_ACC_REG_FIRST 176
#define DSP_ACC_REG_LAST 181
#define DSP_ACC_REG_NUM (DSP_ACC_REG_LAST - DSP_ACC_REG_FIRST + 1)

#define AT_REGNUM	(GP_REG_FIRST + 1)
#define HI_REGNUM	(MD_REG_FIRST + 0)
#define LO_REGNUM	(MD_REG_FIRST + 1)
#define AC1HI_REGNUM	(DSP_ACC_REG_FIRST + 0)
#define AC1LO_REGNUM	(DSP_ACC_REG_FIRST + 1)
#define AC2HI_REGNUM	(DSP_ACC_REG_FIRST + 2)
#define AC2LO_REGNUM	(DSP_ACC_REG_FIRST + 3)
#define AC3HI_REGNUM	(DSP_ACC_REG_FIRST + 4)
#define AC3LO_REGNUM	(DSP_ACC_REG_FIRST + 5)

/* FPSW_REGNUM is the single condition code used if !ISA_HAS_8CC.
   If ISA_HAS_8CC, it should not be used, and an arbitrary ST_REG
   should be used instead.  */
#define FPSW_REGNUM	ST_REG_FIRST

#define GP_REG_P(REGNO)	\
  ((unsigned int) ((int) (REGNO) - GP_REG_FIRST) < GP_REG_NUM)
#define M16_REG_P(REGNO) \
  (((REGNO) >= 2 && (REGNO) <= 7) || (REGNO) == 16 || (REGNO) == 17)
#define FP_REG_P(REGNO)  \
  ((unsigned int) ((int) (REGNO) - FP_REG_FIRST) < FP_REG_NUM)
#define MD_REG_P(REGNO) \
  ((unsigned int) ((int) (REGNO) - MD_REG_FIRST) < MD_REG_NUM)
#define ST_REG_P(REGNO) \
  ((unsigned int) ((int) (REGNO) - ST_REG_FIRST) < ST_REG_NUM)
#define COP0_REG_P(REGNO) \
  ((unsigned int) ((int) (REGNO) - COP0_REG_FIRST) < COP0_REG_NUM)
#define COP2_REG_P(REGNO) \
  ((unsigned int) ((int) (REGNO) - COP2_REG_FIRST) < COP2_REG_NUM)
#define COP3_REG_P(REGNO) \
  ((unsigned int) ((int) (REGNO) - COP3_REG_FIRST) < COP3_REG_NUM)
#define ALL_COP_REG_P(REGNO) \
  ((unsigned int) ((int) (REGNO) - COP0_REG_FIRST) < ALL_COP_REG_NUM)
/* Test if REGNO is one of the 6 new DSP accumulators.  */
#define DSP_ACC_REG_P(REGNO) \
  ((unsigned int) ((int) (REGNO) - DSP_ACC_REG_FIRST) < DSP_ACC_REG_NUM)
/* Test if REGNO is hi, lo, or one of the 6 new DSP accumulators.  */
#define ACC_REG_P(REGNO) \
  (MD_REG_P (REGNO) || DSP_ACC_REG_P (REGNO))
/* Test if REGNO is HI or the first register of 3 new DSP accumulator pairs.  */
#define ACC_HI_REG_P(REGNO) \
  ((REGNO) == HI_REGNUM || (REGNO) == AC1HI_REGNUM || (REGNO) == AC2HI_REGNUM \
   || (REGNO) == AC3HI_REGNUM)

#define FP_REG_RTX_P(X) (REG_P (X) && FP_REG_P (REGNO (X)))

/* True if X is (const (unspec [(const_int 0)] UNSPEC_GP)).  This is used
   to initialize the mips16 gp pseudo register.  */
#define CONST_GP_P(X)				\
  (GET_CODE (X) == CONST			\
   && GET_CODE (XEXP (X, 0)) == UNSPEC		\
   && XINT (XEXP (X, 0), 1) == UNSPEC_GP)

/* Return coprocessor number from register number.  */

#define COPNUM_AS_CHAR_FROM_REGNUM(REGNO) 				\
  (COP0_REG_P (REGNO) ? '0' : COP2_REG_P (REGNO) ? '2'			\
   : COP3_REG_P (REGNO) ? '3' : '?')


#define HARD_REGNO_NREGS(REGNO, MODE) mips_hard_regno_nregs (REGNO, MODE)

/* To make the code simpler, HARD_REGNO_MODE_OK just references an
   array built in override_options.  Because machmodes.h is not yet
   included before this file is processed, the MODE bound can't be
   expressed here.  */

extern char mips_hard_regno_mode_ok[][FIRST_PSEUDO_REGISTER];

#define HARD_REGNO_MODE_OK(REGNO, MODE)					\
  mips_hard_regno_mode_ok[ (int)(MODE) ][ (REGNO) ]

/* Value is 1 if it is a good idea to tie two pseudo registers
   when one has mode MODE1 and one has mode MODE2.
   If HARD_REGNO_MODE_OK could produce different values for MODE1 and MODE2,
   for any hard reg, then this must be 0 for correct output.  */
#define MODES_TIEABLE_P(MODE1, MODE2)					\
  ((GET_MODE_CLASS (MODE1) == MODE_FLOAT ||				\
    GET_MODE_CLASS (MODE1) == MODE_COMPLEX_FLOAT)			\
   == (GET_MODE_CLASS (MODE2) == MODE_FLOAT ||				\
       GET_MODE_CLASS (MODE2) == MODE_COMPLEX_FLOAT))

/* Register to use for pushing function arguments.  */
#define STACK_POINTER_REGNUM (GP_REG_FIRST + 29)

/* These two registers don't really exist: they get eliminated to either
   the stack or hard frame pointer.  */
#define ARG_POINTER_REGNUM 77
#define FRAME_POINTER_REGNUM 78

/* $30 is not available on the mips16, so we use $17 as the frame
   pointer.  */
#define HARD_FRAME_POINTER_REGNUM \
  (TARGET_MIPS16 ? GP_REG_FIRST + 17 : GP_REG_FIRST + 30)

/* Value should be nonzero if functions must have frame pointers.
   Zero means the frame pointer need not be set up (and parms
   may be accessed via the stack pointer) in functions that seem suitable.
   This is computed in `reload', in reload1.c.  */
#define FRAME_POINTER_REQUIRED (current_function_calls_alloca)

/* Register in which static-chain is passed to a function.  */
#define STATIC_CHAIN_REGNUM (GP_REG_FIRST + 2)

/* Registers used as temporaries in prologue/epilogue code.  If we're
   generating mips16 code, these registers must come from the core set
   of 8.  The prologue register mustn't conflict with any incoming
   arguments, the static chain pointer, or the frame pointer.  The
   epilogue temporary mustn't conflict with the return registers, the
   frame pointer, the EH stack adjustment, or the EH data registers.  */

#define MIPS_PROLOGUE_TEMP_REGNUM (GP_REG_FIRST + 3)
#define MIPS_EPILOGUE_TEMP_REGNUM (GP_REG_FIRST + (TARGET_MIPS16 ? 6 : 8))

#define MIPS_PROLOGUE_TEMP(MODE) gen_rtx_REG (MODE, MIPS_PROLOGUE_TEMP_REGNUM)
#define MIPS_EPILOGUE_TEMP(MODE) gen_rtx_REG (MODE, MIPS_EPILOGUE_TEMP_REGNUM)

/* Define this macro if it is as good or better to call a constant
   function address than to call an address kept in a register.  */
#define NO_FUNCTION_CSE 1

/* The ABI-defined global pointer.  Sometimes we use a different
   register in leaf functions: see PIC_OFFSET_TABLE_REGNUM.  */
#define GLOBAL_POINTER_REGNUM (GP_REG_FIRST + 28)

/* We normally use $28 as the global pointer.  However, when generating
   n32/64 PIC, it is better for leaf functions to use a call-clobbered
   register instead.  They can then avoid saving and restoring $28
   and perhaps avoid using a frame at all.

   When a leaf function uses something other than $28, mips_expand_prologue
   will modify pic_offset_table_rtx in place.  Take the register number
   from there after reload.  */
#define PIC_OFFSET_TABLE_REGNUM \
  (reload_completed ? REGNO (pic_offset_table_rtx) : GLOBAL_POINTER_REGNUM)

#define PIC_FUNCTION_ADDR_REGNUM (GP_REG_FIRST + 25)

/* Define the classes of registers for register constraints in the
   machine description.  Also define ranges of constants.

   One of the classes must always be named ALL_REGS and include all hard regs.
   If there is more than one class, another class must be named NO_REGS
   and contain no registers.

   The name GENERAL_REGS must be the name of a class (or an alias for
   another name such as ALL_REGS).  This is the class of registers
   that is allowed by "g" or "r" in a register constraint.
   Also, registers outside this class are allocated only when
   instructions express preferences for them.

   The classes must be numbered in nondecreasing order; that is,
   a larger-numbered class must never be contained completely
   in a smaller-numbered class.

   For any two classes, it is very desirable that there be another
   class that represents their union.  */

enum reg_class
{
  NO_REGS,			/* no registers in set */
  M16_NA_REGS,			/* mips16 regs not used to pass args */
  M16_REGS,			/* mips16 directly accessible registers */
  T_REG,			/* mips16 T register ($24) */
  M16_T_REGS,			/* mips16 registers plus T register */
  PIC_FN_ADDR_REG,		/* SVR4 PIC function address register */
  V1_REG,			/* Register $v1 ($3) used for TLS access.  */
  LEA_REGS,			/* Every GPR except $25 */
  GR_REGS,			/* integer registers */
  FP_REGS,			/* floating point registers */
  HI_REG,			/* hi register */
  LO_REG,			/* lo register */
  MD_REGS,			/* multiply/divide registers (hi/lo) */
  COP0_REGS,			/* generic coprocessor classes */
  COP2_REGS,
  COP3_REGS,
  HI_AND_GR_REGS,		/* union classes */
  LO_AND_GR_REGS,
  HI_AND_FP_REGS,
  COP0_AND_GR_REGS,
  COP2_AND_GR_REGS,
  COP3_AND_GR_REGS,
  ALL_COP_REGS,
  ALL_COP_AND_GR_REGS,
  ST_REGS,			/* status registers (fp status) */
  DSP_ACC_REGS,			/* DSP accumulator registers */
  ACC_REGS,			/* Hi/Lo and DSP accumulator registers */
  ALL_REGS,			/* all registers */
  LIM_REG_CLASSES		/* max value + 1 */
};

#define N_REG_CLASSES (int) LIM_REG_CLASSES

#define GENERAL_REGS GR_REGS

/* An initializer containing the names of the register classes as C
   string constants.  These names are used in writing some of the
   debugging dumps.  */

#define REG_CLASS_NAMES							\
{									\
  "NO_REGS",								\
  "M16_NA_REGS",							\
  "M16_REGS",								\
  "T_REG",								\
  "M16_T_REGS",								\
  "PIC_FN_ADDR_REG",							\
  "V1_REG",								\
  "LEA_REGS",								\
  "GR_REGS",								\
  "FP_REGS",								\
  "HI_REG",								\
  "LO_REG",								\
  "MD_REGS",								\
  /* coprocessor registers */						\
  "COP0_REGS",								\
  "COP2_REGS",								\
  "COP3_REGS",								\
  "HI_AND_GR_REGS",							\
  "LO_AND_GR_REGS",							\
  "HI_AND_FP_REGS",							\
  "COP0_AND_GR_REGS",							\
  "COP2_AND_GR_REGS",							\
  "COP3_AND_GR_REGS",							\
  "ALL_COP_REGS",							\
  "ALL_COP_AND_GR_REGS",						\
  "ST_REGS",								\
  "DSP_ACC_REGS",							\
  "ACC_REGS",								\
  "ALL_REGS"								\
}

/* An initializer containing the contents of the register classes,
   as integers which are bit masks.  The Nth integer specifies the
   contents of class N.  The way the integer MASK is interpreted is
   that register R is in the class if `MASK & (1 << R)' is 1.

   When the machine has more than 32 registers, an integer does not
   suffice.  Then the integers are replaced by sub-initializers,
   braced groupings containing several integers.  Each
   sub-initializer must be suitable as an initializer for the type
   `HARD_REG_SET' which is defined in `hard-reg-set.h'.  */

#define REG_CLASS_CONTENTS						                                \
{									                                \
  { 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000 },	/* no registers */	\
  { 0x0003000c, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000 },	/* mips16 nonarg regs */\
  { 0x000300fc, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000 },	/* mips16 registers */	\
  { 0x01000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000 },	/* mips16 T register */	\
  { 0x010300fc, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000 },	/* mips16 and T regs */ \
  { 0x02000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000 },	/* SVR4 PIC function address register */ \
  { 0x00000008, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000 },	/* only $v1 */ \
  { 0xfdffffff, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000 },	/* Every other GPR except $25 */   \
  { 0xffffffff, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000 },	/* integer registers */	\
  { 0x00000000, 0xffffffff, 0x00000000, 0x00000000, 0x00000000, 0x00000000 },	/* floating registers*/	\
  { 0x00000000, 0x00000000, 0x00000001, 0x00000000, 0x00000000, 0x00000000 },	/* hi register */	\
  { 0x00000000, 0x00000000, 0x00000002, 0x00000000, 0x00000000, 0x00000000 },	/* lo register */	\
  { 0x00000000, 0x00000000, 0x00000003, 0x00000000, 0x00000000, 0x00000000 },	/* mul/div registers */	\
  { 0x00000000, 0x00000000, 0xffff0000, 0x0000ffff, 0x00000000, 0x00000000 },   /* cop0 registers */    \
  { 0x00000000, 0x00000000, 0x00000000, 0xffff0000, 0x0000ffff, 0x00000000 },   /* cop2 registers */    \
  { 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0xffff0000, 0x0000ffff },   /* cop3 registers */    \
  { 0xffffffff, 0x00000000, 0x00000001, 0x00000000, 0x00000000, 0x00000000 },	/* union classes */     \
  { 0xffffffff, 0x00000000, 0x00000002, 0x00000000, 0x00000000, 0x00000000 },				\
  { 0x00000000, 0xffffffff, 0x00000001, 0x00000000, 0x00000000, 0x00000000 },				\
  { 0xffffffff, 0x00000000, 0xffff0000, 0x0000ffff, 0x00000000, 0x00000000 },			        \
  { 0xffffffff, 0x00000000, 0x00000000, 0xffff0000, 0x0000ffff, 0x00000000 },	                        \
  { 0xffffffff, 0x00000000, 0x00000000, 0x00000000, 0xffff0000, 0x0000ffff },                           \
  { 0x00000000, 0x00000000, 0xffff0000, 0xffffffff, 0xffffffff, 0x0000ffff },                           \
  { 0xffffffff, 0x00000000, 0xffff0000, 0xffffffff, 0xffffffff, 0x0000ffff },                           \
  { 0x00000000, 0x00000000, 0x000007f8, 0x00000000, 0x00000000, 0x00000000 },	/* status registers */	\
  { 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x003f0000 },	/* dsp accumulator registers */	\
  { 0x00000000, 0x00000000, 0x00000003, 0x00000000, 0x00000000, 0x003f0000 },	/* hi/lo and dsp accumulator registers */	\
  { 0xffffffff, 0xffffffff, 0xffff07ff, 0xffffffff, 0xffffffff, 0x0fffffff }	/* all registers */	\
}


/* A C expression whose value is a register class containing hard
   register REGNO.  In general there is more that one such class;
   choose a class which is "minimal", meaning that no smaller class
   also contains the register.  */

extern const enum reg_class mips_regno_to_class[];

#define REGNO_REG_CLASS(REGNO) mips_regno_to_class[ (REGNO) ]

/* A macro whose definition is the name of the class to which a
   valid base register must belong.  A base register is one used in
   an address which is the register value plus a displacement.  */

#define BASE_REG_CLASS  (TARGET_MIPS16 ? M16_REGS : GR_REGS)

/* A macro whose definition is the name of the class to which a
   valid index register must belong.  An index register is one used
   in an address where its value is either multiplied by a scale
   factor or added to another register (as well as added to a
   displacement).  */

#define INDEX_REG_CLASS NO_REGS

/* When SMALL_REGISTER_CLASSES is nonzero, the compiler allows
   registers explicitly used in the rtl to be used as spill registers
   but prevents the compiler from extending the lifetime of these
   registers.  */

#define SMALL_REGISTER_CLASSES (TARGET_MIPS16)

/* This macro is used later on in the file.  */
#define GR_REG_CLASS_P(CLASS)						\
  ((CLASS) == GR_REGS || (CLASS) == M16_REGS || (CLASS) == T_REG	\
   || (CLASS) == M16_T_REGS || (CLASS) == M16_NA_REGS			\
   || (CLASS) == V1_REG							\
   || (CLASS) == PIC_FN_ADDR_REG || (CLASS) == LEA_REGS)

/* This macro is also used later on in the file.  */
#define COP_REG_CLASS_P(CLASS)						\
  ((CLASS)  == COP0_REGS || (CLASS) == COP2_REGS || (CLASS) == COP3_REGS)

/* REG_ALLOC_ORDER is to order in which to allocate registers.  This
   is the default value (allocate the registers in numeric order).  We
   define it just so that we can override it for the mips16 target in
   ORDER_REGS_FOR_LOCAL_ALLOC.  */

#define REG_ALLOC_ORDER							\
{  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15,	\
  16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31,	\
  32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47,	\
  48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63,	\
  64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79,	\
  80, 81, 82, 83, 84, 85, 86, 87, 88, 89, 90, 91, 92, 93, 94, 95,	\
  96, 97, 98, 99, 100,101,102,103,104,105,106,107,108,109,110,111,	\
  112,113,114,115,116,117,118,119,120,121,122,123,124,125,126,127,	\
  128,129,130,131,132,133,134,135,136,137,138,139,140,141,142,143,	\
  144,145,146,147,148,149,150,151,152,153,154,155,156,157,158,159,	\
  160,161,162,163,164,165,166,167,168,169,170,171,172,173,174,175,	\
  176,177,178,179,180,181,182,183,184,185,186,187			\
}

/* ORDER_REGS_FOR_LOCAL_ALLOC is a macro which permits reg_alloc_order
   to be rearranged based on a particular function.  On the mips16, we
   want to allocate $24 (T_REG) before other registers for
   instructions for which it is possible.  */

#define ORDER_REGS_FOR_LOCAL_ALLOC mips_order_regs_for_local_alloc ()

/* True if VALUE is an unsigned 6-bit number.  */

#define UIMM6_OPERAND(VALUE) \
  (((VALUE) & ~(unsigned HOST_WIDE_INT) 0x3f) == 0)

/* True if VALUE is a signed 10-bit number.  */

#define IMM10_OPERAND(VALUE) \
  ((unsigned HOST_WIDE_INT) (VALUE) + 0x200 < 0x400)

/* True if VALUE is a signed 16-bit number.  */

#define SMALL_OPERAND(VALUE) \
  ((unsigned HOST_WIDE_INT) (VALUE) + 0x8000 < 0x10000)

/* True if VALUE is an unsigned 16-bit number.  */

#define SMALL_OPERAND_UNSIGNED(VALUE) \
  (((VALUE) & ~(unsigned HOST_WIDE_INT) 0xffff) == 0)

/* True if VALUE can be loaded into a register using LUI.  */

#define LUI_OPERAND(VALUE)					\
  (((VALUE) | 0x7fff0000) == 0x7fff0000				\
   || ((VALUE) | 0x7fff0000) + 0x10000 == 0)

/* Return a value X with the low 16 bits clear, and such that
   VALUE - X is a signed 16-bit value.  */

#define CONST_HIGH_PART(VALUE) \
  (((VALUE) + 0x8000) & ~(unsigned HOST_WIDE_INT) 0xffff)

#define CONST_LOW_PART(VALUE) \
  ((VALUE) - CONST_HIGH_PART (VALUE))

#define SMALL_INT(X) SMALL_OPERAND (INTVAL (X))
#define SMALL_INT_UNSIGNED(X) SMALL_OPERAND_UNSIGNED (INTVAL (X))
#define LUI_INT(X) LUI_OPERAND (INTVAL (X))

#define PREFERRED_RELOAD_CLASS(X,CLASS)					\
  mips_preferred_reload_class (X, CLASS)

/* Certain machines have the property that some registers cannot be
   copied to some other registers without using memory.  Define this
   macro on those machines to be a C expression that is nonzero if
   objects of mode MODE in registers of CLASS1 can only be copied to
   registers of class CLASS2 by storing a register of CLASS1 into
   memory and loading that memory location into a register of CLASS2.

   Do not define this macro if its value would always be zero.  */
#if 0
#define SECONDARY_MEMORY_NEEDED(CLASS1, CLASS2, MODE)			\
  ((!TARGET_DEBUG_H_MODE						\
    && GET_MODE_CLASS (MODE) == MODE_INT				\
    && ((CLASS1 == FP_REGS && GR_REG_CLASS_P (CLASS2))			\
	|| (GR_REG_CLASS_P (CLASS1) && CLASS2 == FP_REGS)))		\
   || (TARGET_FLOAT64 && !TARGET_64BIT && (MODE) == DFmode		\
       && ((GR_REG_CLASS_P (CLASS1) && CLASS2 == FP_REGS)		\
	   || (GR_REG_CLASS_P (CLASS2) && CLASS1 == FP_REGS))))
#endif
/* The HI and LO registers can only be reloaded via the general
   registers.  Condition code registers can only be loaded to the
   general registers, and from the floating point registers.  */

#define SECONDARY_INPUT_RELOAD_CLASS(CLASS, MODE, X)			\
  mips_secondary_reload_class (CLASS, MODE, X, 1)
#define SECONDARY_OUTPUT_RELOAD_CLASS(CLASS, MODE, X)			\
  mips_secondary_reload_class (CLASS, MODE, X, 0)

/* Return the maximum number of consecutive registers
   needed to represent mode MODE in a register of class CLASS.  */

#define CLASS_MAX_NREGS(CLASS, MODE) mips_class_max_nregs (CLASS, MODE)

#define CANNOT_CHANGE_MODE_CLASS(FROM, TO, CLASS) \
  mips_cannot_change_mode_class (FROM, TO, CLASS)

/* Stack layout; function entry, exit and calling.  */

#define STACK_GROWS_DOWNWARD

/* The offset of the first local variable from the beginning of the frame.
   See compute_frame_size for details about the frame layout.

   ??? If flag_profile_values is true, and we are generating 32-bit code, then
   we assume that we will need 16 bytes of argument space.  This is because
   the value profiling code may emit calls to cmpdi2 in leaf functions.
   Without this hack, the local variables will start at sp+8 and the gp save
   area will be at sp+16, and thus they will overlap.  compute_frame_size is
   OK because it uses STARTING_FRAME_OFFSET to compute cprestore_size, which
   will end up as 24 instead of 8.  This won't be needed if profiling code is
   inserted before virtual register instantiation.  */

#define STARTING_FRAME_OFFSET						\
  ((flag_profile_values && ! TARGET_64BIT				\
    ? MAX (REG_PARM_STACK_SPACE(NULL), current_function_outgoing_args_size) \
    : current_function_outgoing_args_size)				\
   + (TARGET_ABICALLS && !TARGET_NEWABI					\
      ? MIPS_STACK_ALIGN (UNITS_PER_WORD) : 0))

#define RETURN_ADDR_RTX mips_return_addr

/* Since the mips16 ISA mode is encoded in the least-significant bit
   of the address, mask it off return addresses for purposes of
   finding exception handling regions.  */

#define MASK_RETURN_ADDR GEN_INT (-2)


/* Similarly, don't use the least-significant bit to tell pointers to
   code from vtable index.  */

#define TARGET_PTRMEMFUNC_VBIT_LOCATION ptrmemfunc_vbit_in_delta

/* The eliminations to $17 are only used for mips16 code.  See the
   definition of HARD_FRAME_POINTER_REGNUM.  */

#define ELIMINABLE_REGS							\
{{ ARG_POINTER_REGNUM,   STACK_POINTER_REGNUM},				\
 { ARG_POINTER_REGNUM,   GP_REG_FIRST + 30},				\
 { ARG_POINTER_REGNUM,   GP_REG_FIRST + 17},				\
 { FRAME_POINTER_REGNUM, STACK_POINTER_REGNUM},				\
 { FRAME_POINTER_REGNUM, GP_REG_FIRST + 30},				\
 { FRAME_POINTER_REGNUM, GP_REG_FIRST + 17}}

/* We can always eliminate to the hard frame pointer.  We can eliminate
   to the stack pointer unless a frame pointer is needed.

   In mips16 mode, we need a frame pointer for a large frame; otherwise,
   reload may be unable to compute the address of a local variable,
   since there is no way to add a large constant to the stack pointer
   without using a temporary register.  */
#define CAN_ELIMINATE(FROM, TO)						\
  ((TO) == HARD_FRAME_POINTER_REGNUM 				        \
   || ((TO) == STACK_POINTER_REGNUM && !frame_pointer_needed		\
       && (!TARGET_MIPS16						\
	   || compute_frame_size (get_frame_size ()) < 32768)))

#define INITIAL_ELIMINATION_OFFSET(FROM, TO, OFFSET) \
  (OFFSET) = mips_initial_elimination_offset ((FROM), (TO))

/* Allocate stack space for arguments at the beginning of each function.  */
#define ACCUMULATE_OUTGOING_ARGS 1

/* The argument pointer always points to the first argument.  */
#define FIRST_PARM_OFFSET(FNDECL) 0

/* o32 and o64 reserve stack space for all argument registers.  */
#define REG_PARM_STACK_SPACE(FNDECL) 			\
  (TARGET_OLDABI					\
   ? (MAX_ARGS_IN_REGISTERS * UNITS_PER_WORD)		\
   : 0)

/* Define this if it is the responsibility of the caller to
   allocate the area reserved for arguments passed in registers.
   If `ACCUMULATE_OUTGOING_ARGS' is also defined, the only effect
   of this macro is to determine whether the space is included in
   `current_function_outgoing_args_size'.  */
#define OUTGOING_REG_PARM_STACK_SPACE

#define STACK_BOUNDARY (TARGET_NEWABI ? 128 : 64)

#define RETURN_POPS_ARGS(FUNDECL,FUNTYPE,SIZE) 0

/* Symbolic macros for the registers used to return integer and floating
   point values.  */

#define GP_RETURN (GP_REG_FIRST + 2)
#define FP_RETURN ((TARGET_SOFT_FLOAT) ? GP_RETURN : (FP_REG_FIRST + 0))

#define MAX_ARGS_IN_REGISTERS (TARGET_OLDABI ? 4 : 8)

/* Symbolic macros for the first/last argument registers.  */

#define GP_ARG_FIRST (GP_REG_FIRST + 4)
#define GP_ARG_LAST  (GP_ARG_FIRST + MAX_ARGS_IN_REGISTERS - 1)
#define FP_ARG_FIRST (FP_REG_FIRST + 12)
#define FP_ARG_LAST  (FP_ARG_FIRST + MAX_ARGS_IN_REGISTERS - 1)

#define LIBCALL_VALUE(MODE) \
  mips_function_value (NULL_TREE, NULL, (MODE))

#define FUNCTION_VALUE(VALTYPE, FUNC) \
  mips_function_value ((VALTYPE), (FUNC), VOIDmode)

/* 1 if N is a possible register number for a function value.
   On the MIPS, R2 R3 and F0 F2 are the only register thus used.
   Currently, R2 and F0 are only implemented here (C has no complex type)  */

#define FUNCTION_VALUE_REGNO_P(N) ((N) == GP_RETURN || (N) == FP_RETURN \
  || (LONG_DOUBLE_TYPE_SIZE == 128 && FP_RETURN != GP_RETURN \
      && (N) == FP_RETURN + 2))

/* 1 if N is a possible register number for function argument passing.
   We have no FP argument registers when soft-float.  When FP registers
   are 32 bits, we can't directly reference the odd numbered ones.  */

#define FUNCTION_ARG_REGNO_P(N)					\
  ((IN_RANGE((N), GP_ARG_FIRST, GP_ARG_LAST)			\
    || (IN_RANGE((N), FP_ARG_FIRST, FP_ARG_LAST)))		\
   && !fixed_regs[N])

/* This structure has to cope with two different argument allocation
   schemes.  Most MIPS ABIs view the arguments as a structure, of which
   the first N words go in registers and the rest go on the stack.  If I
   < N, the Ith word might go in Ith integer argument register or in a
   floating-point register.  For these ABIs, we only need to remember
   the offset of the current argument into the structure.

   The EABI instead allocates the integer and floating-point arguments
   separately.  The first N words of FP arguments go in FP registers,
   the rest go on the stack.  Likewise, the first N words of the other
   arguments go in integer registers, and the rest go on the stack.  We
   need to maintain three counts: the number of integer registers used,
   the number of floating-point registers used, and the number of words
   passed on the stack.

   We could keep separate information for the two ABIs (a word count for
   the standard ABIs, and three separate counts for the EABI).  But it
   seems simpler to view the standard ABIs as forms of EABI that do not
   allocate floating-point registers.

   So for the standard ABIs, the first N words are allocated to integer
   registers, and function_arg decides on an argument-by-argument basis
   whether that argument should really go in an integer register, or in
   a floating-point one.  */

typedef struct mips_args {
  /* Always true for varargs functions.  Otherwise true if at least
     one argument has been passed in an integer register.  */
  int gp_reg_found;

  /* The number of arguments seen so far.  */
  unsigned int arg_number;

  /* The number of integer registers used so far.  For all ABIs except
     EABI, this is the number of words that have been added to the
     argument structure, limited to MAX_ARGS_IN_REGISTERS.  */
  unsigned int num_gprs;

  /* For EABI, the number of floating-point registers used so far.  */
  unsigned int num_fprs;

  /* The number of words passed on the stack.  */
  unsigned int stack_words;

  /* On the mips16, we need to keep track of which floating point
     arguments were passed in general registers, but would have been
     passed in the FP regs if this were a 32 bit function, so that we
     can move them to the FP regs if we wind up calling a 32 bit
     function.  We record this information in fp_code, encoded in base
     four.  A zero digit means no floating point argument, a one digit
     means an SFmode argument, and a two digit means a DFmode argument,
     and a three digit is not used.  The low order digit is the first
     argument.  Thus 6 == 1 * 4 + 2 means a DFmode argument followed by
     an SFmode argument.  ??? A more sophisticated approach will be
     needed if MIPS_ABI != ABI_32.  */
  int fp_code;

  /* True if the function has a prototype.  */
  int prototype;
} CUMULATIVE_ARGS;

/* Initialize a variable CUM of type CUMULATIVE_ARGS
   for a call to a function whose data type is FNTYPE.
   For a library call, FNTYPE is 0.  */

#define INIT_CUMULATIVE_ARGS(CUM, FNTYPE, LIBNAME, INDIRECT, N_NAMED_ARGS) \
  init_cumulative_args (&CUM, FNTYPE, LIBNAME)				\

/* Update the data in CUM to advance over an argument
   of mode MODE and data type TYPE.
   (TYPE is null for libcalls where that information may not be available.)  */

#define FUNCTION_ARG_ADVANCE(CUM, MODE, TYPE, NAMED)			\
  function_arg_advance (&CUM, MODE, TYPE, NAMED)

/* Determine where to put an argument to a function.
   Value is zero to push the argument on the stack,
   or a hard register in which to store the argument.

   MODE is the argument's machine mode.
   TYPE is the data type of the argument (as a tree).
    This is null for libcalls where that information may
    not be available.
   CUM is a variable of type CUMULATIVE_ARGS which gives info about
    the preceding args and about the function being called.
   NAMED is nonzero if this argument is a named parameter
    (otherwise it is an extra parameter matching an ellipsis).  */

#define FUNCTION_ARG(CUM, MODE, TYPE, NAMED) \
  function_arg( &CUM, MODE, TYPE, NAMED)

#define FUNCTION_ARG_BOUNDARY function_arg_boundary

#define FUNCTION_ARG_PADDING(MODE, TYPE)		\
  (mips_pad_arg_upward (MODE, TYPE) ? upward : downward)

#define BLOCK_REG_PADDING(MODE, TYPE, FIRST)		\
  (mips_pad_reg_upward (MODE, TYPE) ? upward : downward)

/* True if using EABI and varargs can be passed in floating-point
   registers.  Under these conditions, we need a more complex form
   of va_list, which tracks GPR, FPR and stack arguments separately.  */
#define EABI_FLOAT_VARARGS_P \
	(mips_abi == ABI_EABI && UNITS_PER_FPVALUE >= UNITS_PER_DOUBLE)


/* Say that the epilogue uses the return address register.  Note that
   in the case of sibcalls, the values "used by the epilogue" are
   considered live at the start of the called function.  */
#define EPILOGUE_USES(REGNO) ((REGNO) == 31)

/* Treat LOC as a byte offset from the stack pointer and round it up
   to the next fully-aligned offset.  */
#define MIPS_STACK_ALIGN(LOC) \
  (TARGET_NEWABI ? ((LOC) + 15) & -16 : ((LOC) + 7) & -8)


/* Implement `va_start' for varargs and stdarg.  */
#define EXPAND_BUILTIN_VA_START(valist, nextarg) \
  mips_va_start (valist, nextarg)

/* Output assembler code to FILE to increment profiler label # LABELNO
   for profiling a function entry.  */

#define FUNCTION_PROFILER(FILE, LABELNO)				\
{									\
  if (TARGET_MIPS16)							\
    sorry ("mips16 function profiling");				\
  fprintf (FILE, "\t.set\tnoat\n");					\
  fprintf (FILE, "\tmove\t%s,%s\t\t# save current return address\n",	\
	   reg_names[GP_REG_FIRST + 1], reg_names[GP_REG_FIRST + 31]);	\
  if (!TARGET_NEWABI)							\
    {									\
      fprintf (FILE,							\
	       "\t%s\t%s,%s,%d\t\t# _mcount pops 2 words from  stack\n", \
	       TARGET_64BIT ? "dsubu" : "subu",				\
	       reg_names[STACK_POINTER_REGNUM],				\
	       reg_names[STACK_POINTER_REGNUM],				\
	       Pmode == DImode ? 16 : 8);				\
    }									\
  fprintf (FILE, "\tjal\t_mcount\n");                                   \
  fprintf (FILE, "\t.set\tat\n");					\
}

/* No mips port has ever used the profiler counter word, so don't emit it
   or the label for it.  */

#define NO_PROFILE_COUNTERS 1

/* Define this macro if the code for function profiling should come
   before the function prologue.  Normally, the profiling code comes
   after.  */

/* #define PROFILE_BEFORE_PROLOGUE */

/* EXIT_IGNORE_STACK should be nonzero if, when returning from a function,
   the stack pointer does not matter.  The value is tested only in
   functions that have frame pointers.
   No definition is equivalent to always zero.  */

#define EXIT_IGNORE_STACK 1


/* A C statement to output, on the stream FILE, assembler code for a
   block of data that contains the constant parts of a trampoline.
   This code should not include a label--the label is taken care of
   automatically.  */

#define TRAMPOLINE_TEMPLATE(STREAM)					\
{									\
  if (ptr_mode == DImode)						\
    fprintf (STREAM, "\t.word\t0x03e0082d\t\t# dmove   $1,$31\n");	\
  else									\
    fprintf (STREAM, "\t.word\t0x03e00821\t\t# move   $1,$31\n");	\
  fprintf (STREAM, "\t.word\t0x04110001\t\t# bgezal $0,.+8\n");		\
  fprintf (STREAM, "\t.word\t0x00000000\t\t# nop\n");			\
  if (ptr_mode == DImode)						\
    {									\
      fprintf (STREAM, "\t.word\t0xdfe30014\t\t# ld     $3,20($31)\n");	\
      fprintf (STREAM, "\t.word\t0xdfe2001c\t\t# ld     $2,28($31)\n");	\
      fprintf (STREAM, "\t.word\t0x0060c82d\t\t# dmove  $25,$3\n");	\
    }									\
  else									\
    {									\
      fprintf (STREAM, "\t.word\t0x8fe30014\t\t# lw     $3,20($31)\n");	\
      fprintf (STREAM, "\t.word\t0x8fe20018\t\t# lw     $2,24($31)\n");	\
      fprintf (STREAM, "\t.word\t0x0060c821\t\t# move   $25,$3\n");	\
    }									\
  fprintf (STREAM, "\t.word\t0x00600008\t\t# jr     $3\n");		\
  if (ptr_mode == DImode)						\
    {									\
      fprintf (STREAM, "\t.word\t0x0020f82d\t\t# dmove   $31,$1\n");	\
      fprintf (STREAM, "\t.dword\t0x00000000\t\t# <function address>\n"); \
      fprintf (STREAM, "\t.dword\t0x00000000\t\t# <static chain value>\n"); \
    }									\
  else									\
    {									\
      fprintf (STREAM, "\t.word\t0x0020f821\t\t# move   $31,$1\n");	\
      fprintf (STREAM, "\t.word\t0x00000000\t\t# <function address>\n"); \
      fprintf (STREAM, "\t.word\t0x00000000\t\t# <static chain value>\n"); \
    }									\
}

/* A C expression for the size in bytes of the trampoline, as an
   integer.  */

#define TRAMPOLINE_SIZE (32 + GET_MODE_SIZE (ptr_mode) * 2)

/* Alignment required for trampolines, in bits.  */

#define TRAMPOLINE_ALIGNMENT GET_MODE_BITSIZE (ptr_mode)

/* INITIALIZE_TRAMPOLINE calls this library function to flush
   program and data caches.  */

#ifndef CACHE_FLUSH_FUNC
#define CACHE_FLUSH_FUNC "_flush_cache"
#endif

/* A C statement to initialize the variable parts of a trampoline.
   ADDR is an RTX for the address of the trampoline; FNADDR is an
   RTX for the address of the nested function; STATIC_CHAIN is an
   RTX for the static chain value that should be passed to the
   function when it is called.  */

#define INITIALIZE_TRAMPOLINE(ADDR, FUNC, CHAIN)			    \
{									    \
  rtx func_addr, chain_addr;						    \
									    \
  func_addr = plus_constant (ADDR, 32);					    \
  chain_addr = plus_constant (func_addr, GET_MODE_SIZE (ptr_mode));	    \
  emit_move_insn (gen_rtx_MEM (ptr_mode, func_addr), FUNC);		    \
  emit_move_insn (gen_rtx_MEM (ptr_mode, chain_addr), CHAIN);		    \
									    \
  /* Flush both caches.  We need to flush the data cache in case	    \
     the system has a write-back cache.  */				    \
  /* ??? Should check the return value for errors.  */			    \
  if (mips_cache_flush_func && mips_cache_flush_func[0])		    \
    emit_library_call (gen_rtx_SYMBOL_REF (Pmode, mips_cache_flush_func),   \
		       0, VOIDmode, 3, ADDR, Pmode,			    \
		       GEN_INT (TRAMPOLINE_SIZE), TYPE_MODE (integer_type_node),\
		       GEN_INT (3), TYPE_MODE (integer_type_node));	    \
}

/* Addressing modes, and classification of registers for them.  */

#define REGNO_OK_FOR_INDEX_P(REGNO) 0
#define REGNO_MODE_OK_FOR_BASE_P(REGNO, MODE) \
  mips_regno_mode_ok_for_base_p (REGNO, MODE, 1)

/* The macros REG_OK_FOR..._P assume that the arg is a REG rtx
   and check its validity for a certain class.
   We have two alternate definitions for each of them.
   The usual definition accepts all pseudo regs; the other rejects them all.
   The symbol REG_OK_STRICT causes the latter definition to be used.

   Most source files want to accept pseudo regs in the hope that
   they will get allocated to the class that the insn wants them to be in.
   Some source files that are used after register allocation
   need to be strict.  */

#ifndef REG_OK_STRICT
#define REG_MODE_OK_FOR_BASE_P(X, MODE) \
  mips_regno_mode_ok_for_base_p (REGNO (X), MODE, 0)
#else
#define REG_MODE_OK_FOR_BASE_P(X, MODE) \
  mips_regno_mode_ok_for_base_p (REGNO (X), MODE, 1)
#endif

#define REG_OK_FOR_INDEX_P(X) 0


/* Maximum number of registers that can appear in a valid memory address.  */

#define MAX_REGS_PER_ADDRESS 1

#ifdef REG_OK_STRICT
#define GO_IF_LEGITIMATE_ADDRESS(MODE, X, ADDR)	\
{						\
  if (mips_legitimate_address_p (MODE, X, 1))	\
    goto ADDR;					\
}
#else
#define GO_IF_LEGITIMATE_ADDRESS(MODE, X, ADDR)	\
{						\
  if (mips_legitimate_address_p (MODE, X, 0))	\
    goto ADDR;					\
}
#endif

/* Check for constness inline but use mips_legitimate_address_p
   to check whether a constant really is an address.  */

#define CONSTANT_ADDRESS_P(X) \
  (CONSTANT_P (X) && mips_legitimate_address_p (SImode, X, 0))

#define LEGITIMATE_CONSTANT_P(X) (mips_const_insns (X) > 0)

#define LEGITIMIZE_ADDRESS(X,OLDX,MODE,WIN)			\
  do {								\
    if (mips_legitimize_address (&(X), MODE))			\
      goto WIN;							\
  } while (0)


/* A C statement or compound statement with a conditional `goto
   LABEL;' executed if memory address X (an RTX) can have different
   meanings depending on the machine mode of the memory reference it
   is used for.

   Autoincrement and autodecrement addresses typically have
   mode-dependent effects because the amount of the increment or
   decrement is the size of the operand being addressed.  Some
   machines have other mode-dependent addresses.  Many RISC machines
   have no mode-dependent addresses.

   You may assume that ADDR is a valid address for the machine.  */

#define GO_IF_MODE_DEPENDENT_ADDRESS(ADDR,LABEL) {}

/* This handles the magic '..CURRENT_FUNCTION' symbol, which means
   'the start of the function that this code is output in'.  */

#define ASM_OUTPUT_LABELREF(FILE,NAME)  \
  if (strcmp (NAME, "..CURRENT_FUNCTION") == 0)				\
    asm_fprintf ((FILE), "%U%s",					\
		 XSTR (XEXP (DECL_RTL (current_function_decl), 0), 0));	\
  else									\
    asm_fprintf ((FILE), "%U%s", (NAME))

/* Flag to mark a function decl symbol that requires a long call.  */
#define SYMBOL_FLAG_LONG_CALL	(SYMBOL_FLAG_MACH_DEP << 0)
#define SYMBOL_REF_LONG_CALL_P(X)					\
  ((SYMBOL_REF_FLAGS (X) & SYMBOL_FLAG_LONG_CALL) != 0)

/* Specify the machine mode that this machine uses
   for the index in the tablejump instruction.
   ??? Using HImode in mips16 mode can cause overflow.  */
#define CASE_VECTOR_MODE \
  (TARGET_MIPS16 ? HImode : ptr_mode)

/* Define as C expression which evaluates to nonzero if the tablejump
   instruction expects the table to contain offsets from the address of the
   table.
   Do not define this if the table should contain absolute addresses.  */
#define CASE_VECTOR_PC_RELATIVE (TARGET_MIPS16)

/* Define this as 1 if `char' should by default be signed; else as 0.  */
#ifndef DEFAULT_SIGNED_CHAR
#define DEFAULT_SIGNED_CHAR 1
#endif

/* Max number of bytes we can move from memory to memory
   in one reasonably fast instruction.  */
#define MOVE_MAX (TARGET_64BIT ? 8 : 4)
#define MAX_MOVE_MAX 8

/* Define this macro as a C expression which is nonzero if
   accessing less than a word of memory (i.e. a `char' or a
   `short') is no faster than accessing a word of memory, i.e., if
   such access require more than one instruction or if there is no
   difference in cost between byte and (aligned) word loads.

   On RISC machines, it tends to generate better code to define
   this as 1, since it avoids making a QI or HI mode register.  */
#define SLOW_BYTE_ACCESS 1

/* Define this to be nonzero if shift instructions ignore all but the low-order
   few bits.  */
#define SHIFT_COUNT_TRUNCATED 1

/* Value is 1 if truncating an integer of INPREC bits to OUTPREC bits
   is done just by pretending it is already truncated.  */
#define TRULY_NOOP_TRUNCATION(OUTPREC, INPREC) \
  (TARGET_64BIT ? ((INPREC) <= 32 || (OUTPREC) > 32) : 1)


/* Specify the machine mode that pointers have.
   After generation of rtl, the compiler makes no further distinction
   between pointers and any other objects of this machine mode.  */

#ifndef Pmode
#define Pmode (TARGET_64BIT && TARGET_LONG64 ? DImode : SImode)
#endif

/* Give call MEMs SImode since it is the "most permissive" mode
   for both 32-bit and 64-bit targets.  */

#define FUNCTION_MODE SImode


/* The cost of loading values from the constant pool.  It should be
   larger than the cost of any constant we want to synthesize in-line.  */

#define CONSTANT_POOL_COST COSTS_N_INSNS (8)

/* A C expression for the cost of moving data from a register in
   class FROM to one in class TO.  The classes are expressed using
   the enumeration values such as `GENERAL_REGS'.  A value of 2 is
   the default; other values are interpreted relative to that.

   It is not required that the cost always equal 2 when FROM is the
   same as TO; on some machines it is expensive to move between
   registers if they are not general registers.

   If reload sees an insn consisting of a single `set' between two
   hard registers, and if `REGISTER_MOVE_COST' applied to their
   classes returns a value of 2, reload does not check to ensure
   that the constraints of the insn are met.  Setting a cost of
   other than 2 will allow reload to verify that the constraints are
   met.  You should do this if the `movM' pattern's constraints do
   not allow such copying.  */

#define REGISTER_MOVE_COST(MODE, FROM, TO)				\
  mips_register_move_cost (MODE, FROM, TO)

#define MEMORY_MOVE_COST(MODE,CLASS,TO_P) \
  (mips_cost->memory_latency	      		\
   + memory_move_secondary_cost ((MODE), (CLASS), (TO_P)))

/* Define if copies to/from condition code registers should be avoided.

   This is needed for the MIPS because reload_outcc is not complete;
   it needs to handle cases where the source is a general or another
   condition code register.  */
#define AVOID_CCMODE_COPIES

/* A C expression for the cost of a branch instruction.  A value of
   1 is the default; other values are interpreted relative to that.  */

#define BRANCH_COST mips_cost->branch_cost
#define LOGICAL_OP_NON_SHORT_CIRCUIT 0

/* If defined, modifies the length assigned to instruction INSN as a
   function of the context in which it is used.  LENGTH is an lvalue
   that contains the initially computed length of the insn and should
   be updated with the correct length of the insn.  */
#define ADJUST_INSN_LENGTH(INSN, LENGTH) \
  ((LENGTH) = mips_adjust_insn_length ((INSN), (LENGTH)))

/* Return the asm template for a non-MIPS16 conditional branch instruction.
   OPCODE is the opcode's mnemonic and OPERANDS is the asm template for
   its operands.  */
#define MIPS_BRANCH(OPCODE, OPERANDS) \
  "%*" OPCODE "%?\t" OPERANDS "%/"

/* Return the asm template for a call.  INSN is the instruction's mnemonic
   ("j" or "jal"), OPERANDS are its operands, and OPNO is the operand number
   of the target.

   When generating -mabicalls without explicit relocation operators,
   all calls should use assembly macros.  Otherwise, all indirect
   calls should use "jr" or "jalr"; we will arrange to restore $gp
   afterwards if necessary.  Finally, we can only generate direct
   calls for -mabicalls by temporarily switching to non-PIC mode.  */
#define MIPS_CALL(INSN, OPERANDS, OPNO)				\
  (TARGET_ABICALLS && !TARGET_EXPLICIT_RELOCS			\
   ? "%*" INSN "\t%" #OPNO "%/"					\
   : REG_P (OPERANDS[OPNO])					\
   ? "%*" INSN "r\t%" #OPNO "%/"				\
   : TARGET_ABICALLS						\
   ? (".option\tpic0\n\t"					\
      "%*" INSN "\t%" #OPNO "%/\n\t"				\
      ".option\tpic2")						\
   : "%*" INSN "\t%" #OPNO "%/")

/* Control the assembler format that we output.  */

/* Output to assembler file text saying following lines
   may contain character constants, extra white space, comments, etc.  */

#ifndef ASM_APP_ON
#define ASM_APP_ON " #APP\n"
#endif

/* Output to assembler file text saying following lines
   no longer contain unusual constructs.  */

#ifndef ASM_APP_OFF
#define ASM_APP_OFF " #NO_APP\n"
#endif

#define REGISTER_NAMES							   \
{ "$0",   "$1",   "$2",   "$3",   "$4",   "$5",   "$6",   "$7",		   \
  "$8",   "$9",   "$10",  "$11",  "$12",  "$13",  "$14",  "$15",	   \
  "$16",  "$17",  "$18",  "$19",  "$20",  "$21",  "$22",  "$23",	   \
  "$24",  "$25",  "$26",  "$27",  "$28",  "$sp",  "$fp",  "$31",	   \
  "$f0",  "$f1",  "$f2",  "$f3",  "$f4",  "$f5",  "$f6",  "$f7",	   \
  "$f8",  "$f9",  "$f10", "$f11", "$f12", "$f13", "$f14", "$f15",	   \
  "$f16", "$f17", "$f18", "$f19", "$f20", "$f21", "$f22", "$f23",	   \
  "$f24", "$f25", "$f26", "$f27", "$f28", "$f29", "$f30", "$f31",	   \
  "hi",   "lo",   "",     "$fcc0","$fcc1","$fcc2","$fcc3","$fcc4",	   \
  "$fcc5","$fcc6","$fcc7","", "", "$arg", "$frame", "$fakec",		   \
  "$c0r0", "$c0r1", "$c0r2", "$c0r3", "$c0r4", "$c0r5", "$c0r6", "$c0r7",  \
  "$c0r8", "$c0r9", "$c0r10","$c0r11","$c0r12","$c0r13","$c0r14","$c0r15", \
  "$c0r16","$c0r17","$c0r18","$c0r19","$c0r20","$c0r21","$c0r22","$c0r23", \
  "$c0r24","$c0r25","$c0r26","$c0r27","$c0r28","$c0r29","$c0r30","$c0r31", \
  "$c2r0", "$c2r1", "$c2r2", "$c2r3", "$c2r4", "$c2r5", "$c2r6", "$c2r7",  \
  "$c2r8", "$c2r9", "$c2r10","$c2r11","$c2r12","$c2r13","$c2r14","$c2r15", \
  "$c2r16","$c2r17","$c2r18","$c2r19","$c2r20","$c2r21","$c2r22","$c2r23", \
  "$c2r24","$c2r25","$c2r26","$c2r27","$c2r28","$c2r29","$c2r30","$c2r31", \
  "$c3r0", "$c3r1", "$c3r2", "$c3r3", "$c3r4", "$c3r5", "$c3r6", "$c3r7",  \
  "$c3r8", "$c3r9", "$c3r10","$c3r11","$c3r12","$c3r13","$c3r14","$c3r15", \
  "$c3r16","$c3r17","$c3r18","$c3r19","$c3r20","$c3r21","$c3r22","$c3r23", \
  "$c3r24","$c3r25","$c3r26","$c3r27","$c3r28","$c3r29","$c3r30","$c3r31", \
  "$ac1hi","$ac1lo","$ac2hi","$ac2lo","$ac3hi","$ac3lo","$dsp_po","$dsp_sc", \
  "$dsp_ca","$dsp_ou","$dsp_cc","$dsp_ef" }

/* List the "software" names for each register.  Also list the numerical
   names for $fp and $sp.  */

#define ADDITIONAL_REGISTER_NAMES					\
{									\
  { "$29",	29 + GP_REG_FIRST },					\
  { "$30",	30 + GP_REG_FIRST },					\
  { "at",	 1 + GP_REG_FIRST },					\
  { "v0",	 2 + GP_REG_FIRST },					\
  { "v1",	 3 + GP_REG_FIRST },					\
  { "a0",	 4 + GP_REG_FIRST },					\
  { "a1",	 5 + GP_REG_FIRST },					\
  { "a2",	 6 + GP_REG_FIRST },					\
  { "a3",	 7 + GP_REG_FIRST },					\
  { "t0",	 8 + GP_REG_FIRST },					\
  { "t1",	 9 + GP_REG_FIRST },					\
  { "t2",	10 + GP_REG_FIRST },					\
  { "t3",	11 + GP_REG_FIRST },					\
  { "t4",	12 + GP_REG_FIRST },					\
  { "t5",	13 + GP_REG_FIRST },					\
  { "t6",	14 + GP_REG_FIRST },					\
  { "t7",	15 + GP_REG_FIRST },					\
  { "s0",	16 + GP_REG_FIRST },					\
  { "s1",	17 + GP_REG_FIRST },					\
  { "s2",	18 + GP_REG_FIRST },					\
  { "s3",	19 + GP_REG_FIRST },					\
  { "s4",	20 + GP_REG_FIRST },					\
  { "s5",	21 + GP_REG_FIRST },					\
  { "s6",	22 + GP_REG_FIRST },					\
  { "s7",	23 + GP_REG_FIRST },					\
  { "t8",	24 + GP_REG_FIRST },					\
  { "t9",	25 + GP_REG_FIRST },					\
  { "k0",	26 + GP_REG_FIRST },					\
  { "k1",	27 + GP_REG_FIRST },					\
  { "gp",	28 + GP_REG_FIRST },					\
  { "sp",	29 + GP_REG_FIRST },					\
  { "fp",	30 + GP_REG_FIRST },					\
  { "ra",	31 + GP_REG_FIRST },					\
  ALL_COP_ADDITIONAL_REGISTER_NAMES					\
}

/* This is meant to be redefined in the host dependent files.  It is a
   set of alternative names and regnums for mips coprocessors.  */

#define ALL_COP_ADDITIONAL_REGISTER_NAMES

/* A C compound statement to output to stdio stream STREAM the
   assembler syntax for an instruction operand X.  X is an RTL
   expression.

   CODE is a value that can be used to specify one of several ways
   of printing the operand.  It is used when identical operands
   must be printed differently depending on the context.  CODE
   comes from the `%' specification that was used to request
   printing of the operand.  If the specification was just `%DIGIT'
   then CODE is 0; if the specification was `%LTR DIGIT' then CODE
   is the ASCII code for LTR.

   If X is a register, this macro should print the register's name.
   The names can be found in an array `reg_names' whose type is
   `char *[]'.  `reg_names' is initialized from `REGISTER_NAMES'.

   When the machine description has a specification `%PUNCT' (a `%'
   followed by a punctuation character), this macro is called with
   a null pointer for X and the punctuation character for CODE.

   See mips.c for the MIPS specific codes.  */

#define PRINT_OPERAND(FILE, X, CODE) print_operand (FILE, X, CODE)

/* A C expression which evaluates to true if CODE is a valid
   punctuation character for use in the `PRINT_OPERAND' macro.  If
   `PRINT_OPERAND_PUNCT_VALID_P' is not defined, it means that no
   punctuation characters (except for the standard one, `%') are
   used in this way.  */

#define PRINT_OPERAND_PUNCT_VALID_P(CODE) mips_print_operand_punct[CODE]

/* A C compound statement to output to stdio stream STREAM the
   assembler syntax for an instruction operand that is a memory
   reference whose address is ADDR.  ADDR is an RTL expression.  */

#define PRINT_OPERAND_ADDRESS(FILE, ADDR) print_operand_address (FILE, ADDR)


/* A C statement, to be executed after all slot-filler instructions
   have been output.  If necessary, call `dbr_sequence_length' to
   determine the number of slots filled in a sequence (zero if not
   currently outputting a sequence), to decide how many no-ops to
   output, or whatever.

   Don't define this macro if it has nothing to do, but it is
   helpful in reading assembly output if the extent of the delay
   sequence is made explicit (e.g. with white space).

   Note that output routines for instructions with delay slots must
   be prepared to deal with not being output as part of a sequence
   (i.e.  when the scheduling pass is not run, or when no slot
   fillers could be found.)  The variable `final_sequence' is null
   when not processing a sequence, otherwise it contains the
   `sequence' rtx being output.  */

#define DBR_OUTPUT_SEQEND(STREAM)					\
do									\
  {									\
    if (set_nomacro > 0 && --set_nomacro == 0)				\
      fputs ("\t.set\tmacro\n", STREAM);				\
									\
    if (set_noreorder > 0 && --set_noreorder == 0)			\
      fputs ("\t.set\treorder\n", STREAM);				\
									\
    fputs ("\n", STREAM);						\
  }									\
while (0)


/* How to tell the debugger about changes of source files.  */
#define ASM_OUTPUT_SOURCE_FILENAME(STREAM, NAME)			\
  mips_output_filename (STREAM, NAME)

/* mips-tfile does not understand .stabd directives.  */
#define DBX_OUTPUT_SOURCE_LINE(STREAM, LINE, COUNTER) do {	\
  dbxout_begin_stabn_sline (LINE);				\
  dbxout_stab_value_internal_label ("LM", &COUNTER);		\
} while (0)

/* Use .loc directives for SDB line numbers.  */
#define SDB_OUTPUT_SOURCE_LINE(STREAM, LINE)			\
  fprintf (STREAM, "\t.loc\t%d %d\n", num_source_filenames, LINE)

/* The MIPS implementation uses some labels for its own purpose.  The
   following lists what labels are created, and are all formed by the
   pattern $L[a-z].*.  The machine independent portion of GCC creates
   labels matching:  $L[A-Z][0-9]+ and $L[0-9]+.

	LM[0-9]+	Silicon Graphics/ECOFF stabs label before each stmt.
	$Lb[0-9]+	Begin blocks for MIPS debug support
	$Lc[0-9]+	Label for use in s<xx> operation.
	$Le[0-9]+	End blocks for MIPS debug support  */

#undef ASM_DECLARE_OBJECT_NAME
#define ASM_DECLARE_OBJECT_NAME(STREAM, NAME, DECL) \
  mips_declare_object (STREAM, NAME, "", ":\n", 0)

/* Globalizing directive for a label.  */
#define GLOBAL_ASM_OP "\t.globl\t"

/* This says how to define a global common symbol.  */

#define ASM_OUTPUT_ALIGNED_DECL_COMMON mips_output_aligned_decl_common

/* This says how to define a local common symbol (i.e., not visible to
   linker).  */

#ifndef ASM_OUTPUT_ALIGNED_LOCAL
#define ASM_OUTPUT_ALIGNED_LOCAL(STREAM, NAME, SIZE, ALIGN) \
  mips_declare_common_object (STREAM, NAME, "\n\t.lcomm\t", SIZE, ALIGN, false)
#endif

/* This says how to output an external.  It would be possible not to
   output anything and let undefined symbol become external. However
   the assembler uses length information on externals to allocate in
   data/sdata bss/sbss, thereby saving exec time.  */

#define ASM_OUTPUT_EXTERNAL(STREAM,DECL,NAME) \
  mips_output_external(STREAM,DECL,NAME)

/* This is how to declare a function name.  The actual work of
   emitting the label is moved to function_prologue, so that we can
   get the line number correctly emitted before the .ent directive,
   and after any .file directives.  Define as empty so that the function
   is not declared before the .ent directive elsewhere.  */

#undef ASM_DECLARE_FUNCTION_NAME
#define ASM_DECLARE_FUNCTION_NAME(STREAM,NAME,DECL)

#ifndef FUNCTION_NAME_ALREADY_DECLARED
#define FUNCTION_NAME_ALREADY_DECLARED 0
#endif

/* This is how to store into the string LABEL
   the symbol_ref name of an internal numbered label where
   PREFIX is the class of label and NUM is the number within the class.
   This is suitable for output with `assemble_name'.  */

#undef ASM_GENERATE_INTERNAL_LABEL
#define ASM_GENERATE_INTERNAL_LABEL(LABEL,PREFIX,NUM)			\
  sprintf ((LABEL), "*%s%s%ld", (LOCAL_LABEL_PREFIX), (PREFIX), (long)(NUM))

/* This is how to output an element of a case-vector that is absolute.  */

#define ASM_OUTPUT_ADDR_VEC_ELT(STREAM, VALUE)				\
  fprintf (STREAM, "\t%s\t%sL%d\n",					\
	   ptr_mode == DImode ? ".dword" : ".word",			\
	   LOCAL_LABEL_PREFIX,						\
	   VALUE)

/* This is how to output an element of a case-vector.  We can make the
   entries PC-relative in MIPS16 code and GP-relative when .gp(d)word
   is supported.  */

#define ASM_OUTPUT_ADDR_DIFF_ELT(STREAM, BODY, VALUE, REL)		\
do {									\
  if (TARGET_MIPS16)							\
    fprintf (STREAM, "\t.half\t%sL%d-%sL%d\n",				\
	     LOCAL_LABEL_PREFIX, VALUE, LOCAL_LABEL_PREFIX, REL);	\
  else if (TARGET_GPWORD)						\
    fprintf (STREAM, "\t%s\t%sL%d\n",					\
	     ptr_mode == DImode ? ".gpdword" : ".gpword",		\
	     LOCAL_LABEL_PREFIX, VALUE);				\
  else									\
    fprintf (STREAM, "\t%s\t%sL%d\n",					\
	     ptr_mode == DImode ? ".dword" : ".word",			\
	     LOCAL_LABEL_PREFIX, VALUE);				\
} while (0)

/* When generating MIPS16 code, we want the jump table to be in the text
   section so that we can load its address using a PC-relative addition.  */
#define JUMP_TABLES_IN_TEXT_SECTION TARGET_MIPS16

/* This is how to output an assembler line
   that says to advance the location counter
   to a multiple of 2**LOG bytes.  */

#define ASM_OUTPUT_ALIGN(STREAM,LOG)					\
  fprintf (STREAM, "\t.align\t%d\n", (LOG))

/* This is how to output an assembler line to advance the location
   counter by SIZE bytes.  */

#undef ASM_OUTPUT_SKIP
#define ASM_OUTPUT_SKIP(STREAM,SIZE)					\
  fprintf (STREAM, "\t.space\t"HOST_WIDE_INT_PRINT_UNSIGNED"\n", (SIZE))

/* This is how to output a string.  */
#undef ASM_OUTPUT_ASCII
#define ASM_OUTPUT_ASCII(STREAM, STRING, LEN)				\
  mips_output_ascii (STREAM, STRING, LEN, "\t.ascii\t")

/* Output #ident as a in the read-only data section.  */
#undef  ASM_OUTPUT_IDENT
#define ASM_OUTPUT_IDENT(FILE, STRING)					\
{									\
  const char *p = STRING;						\
  int size = strlen (p) + 1;						\
  switch_to_section (readonly_data_section);				\
  assemble_string (p, size);						\
}

/* Default to -G 8 */
#ifndef MIPS_DEFAULT_GVALUE
#define MIPS_DEFAULT_GVALUE 8
#endif

/* Define the strings to put out for each section in the object file.  */
#define TEXT_SECTION_ASM_OP	"\t.text"	/* instructions */
#define DATA_SECTION_ASM_OP	"\t.data"	/* large data */

#undef READONLY_DATA_SECTION_ASM_OP
#define READONLY_DATA_SECTION_ASM_OP	"\t.rdata"	/* read-only data */

#define ASM_OUTPUT_REG_PUSH(STREAM,REGNO)				\
do									\
  {									\
    fprintf (STREAM, "\t%s\t%s,%s,8\n\t%s\t%s,0(%s)\n",			\
	     TARGET_64BIT ? "dsubu" : "subu",				\
	     reg_names[STACK_POINTER_REGNUM],				\
	     reg_names[STACK_POINTER_REGNUM],				\
	     TARGET_64BIT ? "sd" : "sw",				\
	     reg_names[REGNO],						\
	     reg_names[STACK_POINTER_REGNUM]);				\
  }									\
while (0)

#define ASM_OUTPUT_REG_POP(STREAM,REGNO)				\
do									\
  {									\
    if (! set_noreorder)						\
      fprintf (STREAM, "\t.set\tnoreorder\n");				\
									\
    fprintf (STREAM, "\t%s\t%s,0(%s)\n\t%s\t%s,%s,8\n",			\
	     TARGET_64BIT ? "ld" : "lw",				\
	     reg_names[REGNO],						\
	     reg_names[STACK_POINTER_REGNUM],				\
	     TARGET_64BIT ? "daddu" : "addu",				\
	     reg_names[STACK_POINTER_REGNUM],				\
	     reg_names[STACK_POINTER_REGNUM]);				\
									\
    if (! set_noreorder)						\
      fprintf (STREAM, "\t.set\treorder\n");				\
  }									\
while (0)

/* How to start an assembler comment.
   The leading space is important (the mips native assembler requires it).  */
#ifndef ASM_COMMENT_START
#define ASM_COMMENT_START " #"
#endif

/* Default definitions for size_t and ptrdiff_t.  We must override the
   definitions from ../svr4.h on mips-*-linux-gnu.  */

#undef SIZE_TYPE
#define SIZE_TYPE (POINTER_SIZE == 64 ? "long unsigned int" : "unsigned int")

#undef PTRDIFF_TYPE
#define PTRDIFF_TYPE (POINTER_SIZE == 64 ? "long int" : "int")

#ifndef __mips16
/* Since the bits of the _init and _fini function is spread across
   many object files, each potentially with its own GP, we must assume
   we need to load our GP.  We don't preserve $gp or $ra, since each
   init/fini chunk is supposed to initialize $gp, and crti/crtn
   already take care of preserving $ra and, when appropriate, $gp.  */
#if (defined _ABIO32 && _MIPS_SIM == _ABIO32)
#define CRT_CALL_STATIC_FUNCTION(SECTION_OP, FUNC)	\
   asm (SECTION_OP "\n\
	.set noreorder\n\
	bal 1f\n\
	nop\n\
1:	.cpload $31\n\
	.set reorder\n\
	.local " USER_LABEL_PREFIX #FUNC "\n\
	jal " USER_LABEL_PREFIX #FUNC "\n\
	" TEXT_SECTION_ASM_OP);
#endif /* Switch to #elif when we're no longer limited by K&R C.  */
#if (defined _ABIN32 && _MIPS_SIM == _ABIN32) \
   || (defined _ABI64 && _MIPS_SIM == _ABI64)
#define CRT_CALL_STATIC_FUNCTION(SECTION_OP, FUNC)	\
   asm (SECTION_OP "\n\
	.set noreorder\n\
	bal 1f\n\
	nop\n\
1:	.set reorder\n\
	.cpsetup $31, $2, 1b\n\
	.local " USER_LABEL_PREFIX #FUNC "\n\
	jal " USER_LABEL_PREFIX #FUNC "\n\
	" TEXT_SECTION_ASM_OP);
#endif
#endif

#ifndef HAVE_AS_TLS
#define HAVE_AS_TLS 0
#endif
