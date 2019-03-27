/* Definitions of target machine for GNU compiler, for IBM RS/6000.
   Copyright (C) 1992, 1993, 1994, 1995, 1996, 1997, 1998, 1999,
   2000, 2001, 2002, 2003, 2004, 2005, 2006
   Free Software Foundation, Inc.
   Contributed by Richard Kenner (kenner@vlsi1.ultra.nyu.edu)

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
   along with GCC; see the file COPYING.  If not, write to the
   Free Software Foundation, 51 Franklin Street, Fifth Floor, Boston,
   MA 02110-1301, USA.  */

/* Note that some other tm.h files include this one and then override
   many of the definitions.  */

/* Definitions for the object file format.  These are set at
   compile-time.  */

#define OBJECT_XCOFF 1
#define OBJECT_ELF 2
#define OBJECT_PEF 3
#define OBJECT_MACHO 4

#define TARGET_ELF (TARGET_OBJECT_FORMAT == OBJECT_ELF)
#define TARGET_XCOFF (TARGET_OBJECT_FORMAT == OBJECT_XCOFF)
#define TARGET_MACOS (TARGET_OBJECT_FORMAT == OBJECT_PEF)
#define TARGET_MACHO (TARGET_OBJECT_FORMAT == OBJECT_MACHO)

#ifndef TARGET_AIX
#define TARGET_AIX 0
#endif

/* Control whether function entry points use a "dot" symbol when
   ABI_AIX.  */
#define DOT_SYMBOLS 1

/* Default string to use for cpu if not specified.  */
#ifndef TARGET_CPU_DEFAULT
#define TARGET_CPU_DEFAULT ((char *)0)
#endif

/* If configured for PPC405, support PPC405CR Erratum77.  */
#ifdef CONFIG_PPC405CR
#define PPC405_ERRATUM77 (rs6000_cpu == PROCESSOR_PPC405)
#else
#define PPC405_ERRATUM77 0
#endif

/* Common ASM definitions used by ASM_SPEC among the various targets
   for handling -mcpu=xxx switches.  */
#define ASM_CPU_SPEC \
"%{!mcpu*: \
  %{mpower: %{!mpower2: -mpwr}} \
  %{mpower2: -mpwrx} \
  %{mpowerpc64*: -mppc64} \
  %{!mpowerpc64*: %{mpowerpc*: -mppc}} \
  %{mno-power: %{!mpowerpc*: -mcom}} \
  %{!mno-power: %{!mpower*: %(asm_default)}}} \
%{mcpu=common: -mcom} \
%{mcpu=power: -mpwr} \
%{mcpu=power2: -mpwrx} \
%{mcpu=power3: -mppc64} \
%{mcpu=power4: -mpower4} \
%{mcpu=power5: -mpower4} \
%{mcpu=power5+: -mpower4} \
%{mcpu=power6: -mpower4 -maltivec} \
%{mcpu=powerpc: -mppc} \
%{mcpu=rios: -mpwr} \
%{mcpu=rios1: -mpwr} \
%{mcpu=rios2: -mpwrx} \
%{mcpu=rsc: -mpwr} \
%{mcpu=rsc1: -mpwr} \
%{mcpu=rs64a: -mppc64} \
%{mcpu=401: -mppc} \
%{mcpu=403: -m403} \
%{mcpu=405: -m405} \
%{mcpu=405fp: -m405} \
%{mcpu=440: -m440} \
%{mcpu=440fp: -m440} \
%{mcpu=505: -mppc} \
%{mcpu=601: -m601} \
%{mcpu=602: -mppc} \
%{mcpu=603: -mppc} \
%{mcpu=603e: -mppc} \
%{mcpu=ec603e: -mppc} \
%{mcpu=604: -mppc} \
%{mcpu=604e: -mppc} \
%{mcpu=620: -mppc64} \
%{mcpu=630: -mppc64} \
%{mcpu=740: -mppc} \
%{mcpu=750: -mppc} \
%{mcpu=G3: -mppc} \
%{mcpu=7400: -mppc -maltivec} \
%{mcpu=7450: -mppc -maltivec} \
%{mcpu=G4: -mppc -maltivec} \
%{mcpu=801: -mppc} \
%{mcpu=821: -mppc} \
%{mcpu=823: -mppc} \
%{mcpu=860: -mppc} \
%{mcpu=970: -mpower4 -maltivec} \
%{mcpu=G5: -mpower4 -maltivec} \
%{mcpu=8540: -me500} \
%{maltivec: -maltivec} \
-many"

#define CPP_DEFAULT_SPEC ""

#define ASM_DEFAULT_SPEC ""

/* This macro defines names of additional specifications to put in the specs
   that can be used in various specifications like CC1_SPEC.  Its definition
   is an initializer with a subgrouping for each command option.

   Each subgrouping contains a string constant, that defines the
   specification name, and a string constant that used by the GCC driver
   program.

   Do not define this macro if it does not need to do anything.  */

#define SUBTARGET_EXTRA_SPECS

#define EXTRA_SPECS							\
  { "cpp_default",		CPP_DEFAULT_SPEC },			\
  { "asm_cpu",			ASM_CPU_SPEC },				\
  { "asm_default",		ASM_DEFAULT_SPEC },			\
  SUBTARGET_EXTRA_SPECS

/* Architecture type.  */

/* Define TARGET_MFCRF if the target assembler does not support the
   optional field operand for mfcr.  */

#ifndef HAVE_AS_MFCRF
#undef  TARGET_MFCRF
#define TARGET_MFCRF 0
#endif

/* Define TARGET_POPCNTB if the target assembler does not support the
   popcount byte instruction.  */

#ifndef HAVE_AS_POPCNTB
#undef  TARGET_POPCNTB
#define TARGET_POPCNTB 0
#endif

/* Define TARGET_FPRND if the target assembler does not support the
   fp rounding instructions.  */

#ifndef HAVE_AS_FPRND
#undef  TARGET_FPRND
#define TARGET_FPRND 0
#endif

#ifndef TARGET_SECURE_PLT
#define TARGET_SECURE_PLT 0
#endif

#define TARGET_32BIT		(! TARGET_64BIT)

#ifndef HAVE_AS_TLS
#define HAVE_AS_TLS 0
#endif

/* Return 1 for a symbol ref for a thread-local storage symbol.  */
#define RS6000_SYMBOL_REF_TLS_P(RTX) \
  (GET_CODE (RTX) == SYMBOL_REF && SYMBOL_REF_TLS_MODEL (RTX) != 0)

#ifdef IN_LIBGCC2
/* For libgcc2 we make sure this is a compile time constant */
#if defined (__64BIT__) || defined (__powerpc64__) || defined (__ppc64__)
#undef TARGET_POWERPC64
#define TARGET_POWERPC64	1
#else
#undef TARGET_POWERPC64
#define TARGET_POWERPC64	0
#endif
#else
    /* The option machinery will define this.  */
#endif

#define TARGET_DEFAULT (MASK_POWER | MASK_MULTIPLE | MASK_STRING)

/* Processor type.  Order must match cpu attribute in MD file.  */
enum processor_type
 {
   PROCESSOR_RIOS1,
   PROCESSOR_RIOS2,
   PROCESSOR_RS64A,
   PROCESSOR_MPCCORE,
   PROCESSOR_PPC403,
   PROCESSOR_PPC405,
   PROCESSOR_PPC440,
   PROCESSOR_PPC601,
   PROCESSOR_PPC603,
   PROCESSOR_PPC604,
   PROCESSOR_PPC604e,
   PROCESSOR_PPC620,
   PROCESSOR_PPC630,
   PROCESSOR_PPC750,
   PROCESSOR_PPC7400,
   PROCESSOR_PPC7450,
   PROCESSOR_PPC8540,
   PROCESSOR_POWER4,
   PROCESSOR_POWER5
};

extern enum processor_type rs6000_cpu;

/* Recast the processor type to the cpu attribute.  */
#define rs6000_cpu_attr ((enum attr_cpu)rs6000_cpu)

/* Define generic processor types based upon current deployment.  */
#define PROCESSOR_COMMON    PROCESSOR_PPC601
#define PROCESSOR_POWER     PROCESSOR_RIOS1
#define PROCESSOR_POWERPC   PROCESSOR_PPC604
#define PROCESSOR_POWERPC64 PROCESSOR_RS64A

/* Define the default processor.  This is overridden by other tm.h files.  */
#define PROCESSOR_DEFAULT   PROCESSOR_RIOS1
#define PROCESSOR_DEFAULT64 PROCESSOR_RS64A

/* Specify the dialect of assembler to use.  New mnemonics is dialect one
   and the old mnemonics are dialect zero.  */
#define ASSEMBLER_DIALECT (TARGET_NEW_MNEMONICS ? 1 : 0)

/* Types of costly dependences.  */
enum rs6000_dependence_cost
 {
   max_dep_latency = 1000,
   no_dep_costly,
   all_deps_costly,
   true_store_to_load_dep_costly,
   store_to_load_dep_costly
 };

/* Types of nop insertion schemes in sched target hook sched_finish.  */
enum rs6000_nop_insertion
  {
    sched_finish_regroup_exact = 1000,
    sched_finish_pad_groups,
    sched_finish_none
  };

/* Dispatch group termination caused by an insn.  */
enum group_termination
  {
    current_group,
    previous_group
  };

/* Support for a compile-time default CPU, et cetera.  The rules are:
   --with-cpu is ignored if -mcpu is specified.
   --with-tune is ignored if -mtune is specified.
   --with-float is ignored if -mhard-float or -msoft-float are
    specified.  */
#define OPTION_DEFAULT_SPECS \
  {"cpu", "%{!mcpu=*:-mcpu=%(VALUE)}" }, \
  {"tune", "%{!mtune=*:-mtune=%(VALUE)}" }, \
  {"float", "%{!msoft-float:%{!mhard-float:-m%(VALUE)-float}}" }

/* rs6000_select[0] is reserved for the default cpu defined via --with-cpu */
struct rs6000_cpu_select
{
  const char *string;
  const char *name;
  int set_tune_p;
  int set_arch_p;
};

extern struct rs6000_cpu_select rs6000_select[];

/* Debug support */
extern const char *rs6000_debug_name;	/* Name for -mdebug-xxxx option */
extern int rs6000_debug_stack;		/* debug stack applications */
extern int rs6000_debug_arg;		/* debug argument handling */

#define	TARGET_DEBUG_STACK	rs6000_debug_stack
#define	TARGET_DEBUG_ARG	rs6000_debug_arg

extern const char *rs6000_traceback_name; /* Type of traceback table.  */

/* These are separate from target_flags because we've run out of bits
   there.  */
extern int rs6000_long_double_type_size;
extern int rs6000_ieeequad;
extern int rs6000_altivec_abi;
extern int rs6000_spe_abi;
extern int rs6000_float_gprs;
extern int rs6000_alignment_flags;
extern const char *rs6000_sched_insert_nops_str;
extern enum rs6000_nop_insertion rs6000_sched_insert_nops;

/* Alignment options for fields in structures for sub-targets following
   AIX-like ABI.
   ALIGN_POWER word-aligns FP doubles (default AIX ABI).
   ALIGN_NATURAL doubleword-aligns FP doubles (align to object size).

   Override the macro definitions when compiling libobjc to avoid undefined
   reference to rs6000_alignment_flags due to library's use of GCC alignment
   macros which use the macros below.  */

#ifndef IN_TARGET_LIBS
#define MASK_ALIGN_POWER   0x00000000
#define MASK_ALIGN_NATURAL 0x00000001
#define TARGET_ALIGN_NATURAL (rs6000_alignment_flags & MASK_ALIGN_NATURAL)
#else
#define TARGET_ALIGN_NATURAL 0
#endif

#define TARGET_LONG_DOUBLE_128 (rs6000_long_double_type_size == 128)
#define TARGET_IEEEQUAD rs6000_ieeequad
#define TARGET_ALTIVEC_ABI rs6000_altivec_abi

#define TARGET_SPE_ABI 0
#define TARGET_SPE 0
#define TARGET_E500 0
#define TARGET_ISEL 0
#define TARGET_FPRS 1
#define TARGET_E500_SINGLE 0
#define TARGET_E500_DOUBLE 0

/* E500 processors only support plain "sync", not lwsync.  */
#define TARGET_NO_LWSYNC TARGET_E500

/* Sometimes certain combinations of command options do not make sense
   on a particular target machine.  You can define a macro
   `OVERRIDE_OPTIONS' to take account of this.  This macro, if
   defined, is executed once just after all the command options have
   been parsed.

   Do not use this macro to turn on various extra optimizations for
   `-O'.  That is what `OPTIMIZATION_OPTIONS' is for.

   On the RS/6000 this is used to define the target cpu type.  */

#define OVERRIDE_OPTIONS rs6000_override_options (TARGET_CPU_DEFAULT)

/* Define this to change the optimizations performed by default.  */
#define OPTIMIZATION_OPTIONS(LEVEL,SIZE) optimization_options(LEVEL,SIZE)

/* Show we can debug even without a frame pointer.  */
#define CAN_DEBUG_WITHOUT_FP

/* Target pragma.  */
#define REGISTER_TARGET_PRAGMAS() do {				\
  c_register_pragma (0, "longcall", rs6000_pragma_longcall);	\
  targetm.resolve_overloaded_builtin = altivec_resolve_overloaded_builtin; \
} while (0)

/* Target #defines.  */
#define TARGET_CPU_CPP_BUILTINS() \
  rs6000_cpu_cpp_builtins (pfile)

/* This is used by rs6000_cpu_cpp_builtins to indicate the byte order
   we're compiling for.  Some configurations may need to override it.  */
#define RS6000_CPU_CPP_ENDIAN_BUILTINS()	\
  do						\
    {						\
      if (BYTES_BIG_ENDIAN)			\
	{					\
	  builtin_define ("__BIG_ENDIAN__");	\
	  builtin_define ("_BIG_ENDIAN");	\
	  builtin_assert ("machine=bigendian");	\
	}					\
      else					\
	{					\
	  builtin_define ("__LITTLE_ENDIAN__");	\
	  builtin_define ("_LITTLE_ENDIAN");	\
	  builtin_assert ("machine=littleendian"); \
	}					\
    }						\
  while (0)

/* Target machine storage layout.  */

/* Define this macro if it is advisable to hold scalars in registers
   in a wider mode than that declared by the program.  In such cases,
   the value is constrained to be within the bounds of the declared
   type, but kept valid in the wider mode.  The signedness of the
   extension may differ from that of the type.  */

#define PROMOTE_MODE(MODE,UNSIGNEDP,TYPE)	\
  if (GET_MODE_CLASS (MODE) == MODE_INT		\
      && GET_MODE_SIZE (MODE) < UNITS_PER_WORD) \
    (MODE) = TARGET_32BIT ? SImode : DImode;

/* Define this if most significant bit is lowest numbered
   in instructions that operate on numbered bit-fields.  */
/* That is true on RS/6000.  */
#define BITS_BIG_ENDIAN 1

/* Define this if most significant byte of a word is the lowest numbered.  */
/* That is true on RS/6000.  */
#define BYTES_BIG_ENDIAN 1

/* Define this if most significant word of a multiword number is lowest
   numbered.

   For RS/6000 we can decide arbitrarily since there are no machine
   instructions for them.  Might as well be consistent with bits and bytes.  */
#define WORDS_BIG_ENDIAN 1

#define MAX_BITS_PER_WORD 64

/* Width of a word, in units (bytes).  */
#define UNITS_PER_WORD (! TARGET_POWERPC64 ? 4 : 8)
#ifdef IN_LIBGCC2
#define MIN_UNITS_PER_WORD UNITS_PER_WORD
#else
#define MIN_UNITS_PER_WORD 4
#endif
#define UNITS_PER_FP_WORD 8
#define UNITS_PER_ALTIVEC_WORD 16
#define UNITS_PER_SPE_WORD 8

/* Type used for ptrdiff_t, as a string used in a declaration.  */
#define PTRDIFF_TYPE "int"

/* Type used for size_t, as a string used in a declaration.  */
#define SIZE_TYPE "long unsigned int"

/* Type used for wchar_t, as a string used in a declaration.  */
#define WCHAR_TYPE "short unsigned int"

/* Width of wchar_t in bits.  */
#define WCHAR_TYPE_SIZE 16

/* A C expression for the size in bits of the type `short' on the
   target machine.  If you don't define this, the default is half a
   word.  (If this would be less than one storage unit, it is
   rounded up to one unit.)  */
#define SHORT_TYPE_SIZE 16

/* A C expression for the size in bits of the type `int' on the
   target machine.  If you don't define this, the default is one
   word.  */
#define INT_TYPE_SIZE 32

/* A C expression for the size in bits of the type `long' on the
   target machine.  If you don't define this, the default is one
   word.  */
#define LONG_TYPE_SIZE (TARGET_32BIT ? 32 : 64)

/* A C expression for the size in bits of the type `long long' on the
   target machine.  If you don't define this, the default is two
   words.  */
#define LONG_LONG_TYPE_SIZE 64

/* A C expression for the size in bits of the type `float' on the
   target machine.  If you don't define this, the default is one
   word.  */
#define FLOAT_TYPE_SIZE 32

/* A C expression for the size in bits of the type `double' on the
   target machine.  If you don't define this, the default is two
   words.  */
#define DOUBLE_TYPE_SIZE 64

/* A C expression for the size in bits of the type `long double' on
   the target machine.  If you don't define this, the default is two
   words.  */
#define LONG_DOUBLE_TYPE_SIZE rs6000_long_double_type_size

/* Define this to set long double type size to use in libgcc2.c, which can
   not depend on target_flags.  */
#ifdef __LONG_DOUBLE_128__
#define LIBGCC2_LONG_DOUBLE_TYPE_SIZE 128
#else
#define LIBGCC2_LONG_DOUBLE_TYPE_SIZE 64
#endif

/* Work around rs6000_long_double_type_size dependency in ada/targtyps.c.  */
#define WIDEST_HARDWARE_FP_SIZE 64

/* Width in bits of a pointer.
   See also the macro `Pmode' defined below.  */
#define POINTER_SIZE (TARGET_32BIT ? 32 : 64)

/* Allocation boundary (in *bits*) for storing arguments in argument list.  */
#define PARM_BOUNDARY (TARGET_32BIT ? 32 : 64)

/* Boundary (in *bits*) on which stack pointer should be aligned.  */
#define STACK_BOUNDARY \
  ((TARGET_32BIT && !TARGET_ALTIVEC && !TARGET_ALTIVEC_ABI) ? 64 : 128)

/* Allocation boundary (in *bits*) for the code of a function.  */
#define FUNCTION_BOUNDARY 32

/* No data type wants to be aligned rounder than this.  */
#define BIGGEST_ALIGNMENT 128

/* A C expression to compute the alignment for a variables in the
   local store.  TYPE is the data type, and ALIGN is the alignment
   that the object would ordinarily have.  */
#define LOCAL_ALIGNMENT(TYPE, ALIGN)				\
  ((TARGET_ALTIVEC && TREE_CODE (TYPE) == VECTOR_TYPE) ? 128 :	\
    (TARGET_E500_DOUBLE && TYPE_MODE (TYPE) == DFmode) ? 64 : \
    (TARGET_SPE && TREE_CODE (TYPE) == VECTOR_TYPE \
     && SPE_VECTOR_MODE (TYPE_MODE (TYPE))) ? 64 : ALIGN)

/* Alignment of field after `int : 0' in a structure.  */
#define EMPTY_FIELD_BOUNDARY 32

/* Every structure's size must be a multiple of this.  */
#define STRUCTURE_SIZE_BOUNDARY 8

/* Return 1 if a structure or array containing FIELD should be
   accessed using `BLKMODE'.

   For the SPE, simd types are V2SI, and gcc can be tempted to put the
   entire thing in a DI and use subregs to access the internals.
   store_bit_field() will force (subreg:DI (reg:V2SI x))'s to the
   back-end.  Because a single GPR can hold a V2SI, but not a DI, the
   best thing to do is set structs to BLKmode and avoid Severe Tire
   Damage.

   On e500 v2, DF and DI modes suffer from the same anomaly.  DF can
   fit into 1, whereas DI still needs two.  */
#define MEMBER_TYPE_FORCES_BLK(FIELD, MODE) \
  ((TARGET_SPE && TREE_CODE (TREE_TYPE (FIELD)) == VECTOR_TYPE) \
   || (TARGET_E500_DOUBLE && (MODE) == DFmode))

/* A bit-field declared as `int' forces `int' alignment for the struct.  */
#define PCC_BITFIELD_TYPE_MATTERS 1

/* Make strings word-aligned so strcpy from constants will be faster.
   Make vector constants quadword aligned.  */
#define CONSTANT_ALIGNMENT(EXP, ALIGN)                           \
  (TREE_CODE (EXP) == STRING_CST	                         \
   && (ALIGN) < BITS_PER_WORD                                    \
   ? BITS_PER_WORD                                               \
   : (ALIGN))

/* Make arrays of chars word-aligned for the same reasons.
   Align vectors to 128 bits.  Align SPE vectors and E500 v2 doubles to
   64 bits.  */
#define DATA_ALIGNMENT(TYPE, ALIGN)		\
  (TREE_CODE (TYPE) == VECTOR_TYPE ? (TARGET_SPE_ABI ? 64 : 128)	\
   : (TARGET_E500_DOUBLE && TYPE_MODE (TYPE) == DFmode) ? 64 \
   : TREE_CODE (TYPE) == ARRAY_TYPE		\
   && TYPE_MODE (TREE_TYPE (TYPE)) == QImode	\
   && (ALIGN) < BITS_PER_WORD ? BITS_PER_WORD : (ALIGN))

/* Nonzero if move instructions will actually fail to work
   when given unaligned data.  */
#define STRICT_ALIGNMENT 0

/* Define this macro to be the value 1 if unaligned accesses have a cost
   many times greater than aligned accesses, for example if they are
   emulated in a trap handler.  */
#define SLOW_UNALIGNED_ACCESS(MODE, ALIGN)				\
  (STRICT_ALIGNMENT							\
   || (((MODE) == SFmode || (MODE) == DFmode || (MODE) == TFmode	\
	|| (MODE) == DImode)						\
       && (ALIGN) < 32))

/* Standard register usage.  */

/* Number of actual hardware registers.
   The hardware registers are assigned numbers for the compiler
   from 0 to just below FIRST_PSEUDO_REGISTER.
   All registers that the compiler knows about must be given numbers,
   even those that are not normally considered general registers.

   RS/6000 has 32 fixed-point registers, 32 floating-point registers,
   an MQ register, a count register, a link register, and 8 condition
   register fields, which we view here as separate registers.  AltiVec
   adds 32 vector registers and a VRsave register.

   In addition, the difference between the frame and argument pointers is
   a function of the number of registers saved, so we need to have a
   register for AP that will later be eliminated in favor of SP or FP.
   This is a normal register, but it is fixed.

   We also create a pseudo register for float/int conversions, that will
   really represent the memory location used.  It is represented here as
   a register, in order to work around problems in allocating stack storage
   in inline functions.

   Another pseudo (not included in DWARF_FRAME_REGISTERS) is soft frame
   pointer, which is eventually eliminated in favor of SP or FP.  */

#define FIRST_PSEUDO_REGISTER 114

/* This must be included for pre gcc 3.0 glibc compatibility.  */
#define PRE_GCC3_DWARF_FRAME_REGISTERS 77

/* Add 32 dwarf columns for synthetic SPE registers.  */
#define DWARF_FRAME_REGISTERS ((FIRST_PSEUDO_REGISTER - 1) + 32)

/* The SPE has an additional 32 synthetic registers, with DWARF debug
   info numbering for these registers starting at 1200.  While eh_frame
   register numbering need not be the same as the debug info numbering,
   we choose to number these regs for eh_frame at 1200 too.  This allows
   future versions of the rs6000 backend to add hard registers and
   continue to use the gcc hard register numbering for eh_frame.  If the
   extra SPE registers in eh_frame were numbered starting from the
   current value of FIRST_PSEUDO_REGISTER, then if FIRST_PSEUDO_REGISTER
   changed we'd need to introduce a mapping in DWARF_FRAME_REGNUM to
   avoid invalidating older SPE eh_frame info.

   We must map them here to avoid huge unwinder tables mostly consisting
   of unused space.  */
#define DWARF_REG_TO_UNWIND_COLUMN(r) \
  ((r) > 1200 ? ((r) - 1200 + FIRST_PSEUDO_REGISTER - 1) : (r))

/* Use standard DWARF numbering for DWARF debugging information.  */
#define DBX_REGISTER_NUMBER(REGNO) rs6000_dbx_register_number (REGNO)

/* Use gcc hard register numbering for eh_frame.  */
#define DWARF_FRAME_REGNUM(REGNO) (REGNO)

/* Map register numbers held in the call frame info that gcc has
   collected using DWARF_FRAME_REGNUM to those that should be output in
   .debug_frame and .eh_frame.  We continue to use gcc hard reg numbers
   for .eh_frame, but use the numbers mandated by the various ABIs for
   .debug_frame.  rs6000_emit_prologue has translated any combination of
   CR2, CR3, CR4 saves to a save of CR2.  The actual code emitted saves
   the whole of CR, so we map CR2_REGNO to the DWARF reg for CR.  */
#define DWARF2_FRAME_REG_OUT(REGNO, FOR_EH)	\
  ((FOR_EH) ? (REGNO)				\
   : (REGNO) == CR2_REGNO ? 64			\
   : DBX_REGISTER_NUMBER (REGNO))

/* 1 for registers that have pervasive standard uses
   and are not available for the register allocator.

   On RS/6000, r1 is used for the stack.  On Darwin, r2 is available
   as a local register; for all other OS's r2 is the TOC pointer.

   cr5 is not supposed to be used.

   On System V implementations, r13 is fixed and not available for use.  */

#define FIXED_REGISTERS  \
  {0, 1, FIXED_R2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, FIXED_R13, 0, 0, \
   0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, \
   0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, \
   0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, \
   0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0, 0, 1,	   \
   /* AltiVec registers.  */			   \
   0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, \
   0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, \
   1, 1						   \
   , 1, 1, 1                                       \
}

/* 1 for registers not available across function calls.
   These must include the FIXED_REGISTERS and also any
   registers that can be used without being saved.
   The latter must include the registers where values are returned
   and the register where structure-value addresses are passed.
   Aside from that, you can include as many other registers as you like.  */

#define CALL_USED_REGISTERS  \
  {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, FIXED_R13, 0, 0, \
   0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, \
   1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, \
   0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, \
   1, 1, 1, 1, 1, 1, 0, 0, 0, 1, 1, 1, 1,	   \
   /* AltiVec registers.  */			   \
   0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, \
   0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, \
   1, 1						   \
   , 1, 1, 1                                       \
}

/* Like `CALL_USED_REGISTERS' except this macro doesn't require that
   the entire set of `FIXED_REGISTERS' be included.
   (`CALL_USED_REGISTERS' must be a superset of `FIXED_REGISTERS').
   This macro is optional.  If not specified, it defaults to the value
   of `CALL_USED_REGISTERS'.  */

#define CALL_REALLY_USED_REGISTERS  \
  {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, FIXED_R13, 0, 0, \
   0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, \
   1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, \
   0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, \
   1, 1, 1, 1, 1, 1, 0, 0, 0, 1, 1, 1, 1,	   \
   /* AltiVec registers.  */			   \
   0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, \
   0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, \
   0, 0						   \
   , 0, 0, 0                                       \
}

#define MQ_REGNO     64
#define CR0_REGNO    68
#define CR1_REGNO    69
#define CR2_REGNO    70
#define CR3_REGNO    71
#define CR4_REGNO    72
#define MAX_CR_REGNO 75
#define XER_REGNO    76
#define FIRST_ALTIVEC_REGNO	77
#define LAST_ALTIVEC_REGNO	108
#define TOTAL_ALTIVEC_REGS	(LAST_ALTIVEC_REGNO - FIRST_ALTIVEC_REGNO + 1)
#define VRSAVE_REGNO		109
#define VSCR_REGNO		110
#define SPE_ACC_REGNO		111
#define SPEFSCR_REGNO		112

#define FIRST_SAVED_ALTIVEC_REGNO (FIRST_ALTIVEC_REGNO+20)
#define FIRST_SAVED_FP_REGNO    (14+32)
#define FIRST_SAVED_GP_REGNO 13

/* List the order in which to allocate registers.  Each register must be
   listed once, even those in FIXED_REGISTERS.

   We allocate in the following order:
	fp0		(not saved or used for anything)
	fp13 - fp2	(not saved; incoming fp arg registers)
	fp1		(not saved; return value)
	fp31 - fp14	(saved; order given to save least number)
	cr7, cr6	(not saved or special)
	cr1		(not saved, but used for FP operations)
	cr0		(not saved, but used for arithmetic operations)
	cr4, cr3, cr2	(saved)
	r0		(not saved; cannot be base reg)
	r9		(not saved; best for TImode)
	r11, r10, r8-r4	(not saved; highest used first to make less conflict)
	r3		(not saved; return value register)
	r31 - r13	(saved; order given to save least number)
	r12		(not saved; if used for DImode or DFmode would use r13)
	mq		(not saved; best to use it if we can)
	ctr		(not saved; when we have the choice ctr is better)
	lr		(saved)
	cr5, r1, r2, ap, xer (fixed)
	v0 - v1		(not saved or used for anything)
	v13 - v3	(not saved; incoming vector arg registers)
	v2		(not saved; incoming vector arg reg; return value)
	v19 - v14	(not saved or used for anything)
	v31 - v20	(saved; order given to save least number)
	vrsave, vscr	(fixed)
	spe_acc, spefscr (fixed)
	sfp		(fixed)
*/

#if FIXED_R2 == 1
#define MAYBE_R2_AVAILABLE
#define MAYBE_R2_FIXED 2,
#else
#define MAYBE_R2_AVAILABLE 2,
#define MAYBE_R2_FIXED
#endif

#define REG_ALLOC_ORDER						\
  {32,								\
   45, 44, 43, 42, 41, 40, 39, 38, 37, 36, 35, 34,		\
   33,								\
   63, 62, 61, 60, 59, 58, 57, 56, 55, 54, 53, 52, 51,		\
   50, 49, 48, 47, 46,						\
   75, 74, 69, 68, 72, 71, 70,					\
   0, MAYBE_R2_AVAILABLE					\
   9, 11, 10, 8, 7, 6, 5, 4,					\
   3,								\
   31, 30, 29, 28, 27, 26, 25, 24, 23, 22, 21, 20, 19,		\
   18, 17, 16, 15, 14, 13, 12,					\
   64, 66, 65,							\
   73, 1, MAYBE_R2_FIXED 67, 76,				\
   /* AltiVec registers.  */					\
   77, 78,							\
   90, 89, 88, 87, 86, 85, 84, 83, 82, 81, 80,			\
   79,								\
   96, 95, 94, 93, 92, 91,					\
   108, 107, 106, 105, 104, 103, 102, 101, 100, 99, 98, 97,	\
   109, 110,							\
   111, 112, 113						\
}

/* True if register is floating-point.  */
#define FP_REGNO_P(N) ((N) >= 32 && (N) <= 63)

/* True if register is a condition register.  */
#define CR_REGNO_P(N) ((N) >= 68 && (N) <= 75)

/* True if register is a condition register, but not cr0.  */
#define CR_REGNO_NOT_CR0_P(N) ((N) >= 69 && (N) <= 75)

/* True if register is an integer register.  */
#define INT_REGNO_P(N) \
  ((N) <= 31 || (N) == ARG_POINTER_REGNUM || (N) == FRAME_POINTER_REGNUM)

/* SPE SIMD registers are just the GPRs.  */
#define SPE_SIMD_REGNO_P(N) ((N) <= 31)

/* True if register is the XER register.  */
#define XER_REGNO_P(N) ((N) == XER_REGNO)

/* True if register is an AltiVec register.  */
#define ALTIVEC_REGNO_P(N) ((N) >= FIRST_ALTIVEC_REGNO && (N) <= LAST_ALTIVEC_REGNO)

/* Return number of consecutive hard regs needed starting at reg REGNO
   to hold something of mode MODE.  */

#define HARD_REGNO_NREGS(REGNO, MODE) rs6000_hard_regno_nregs ((REGNO), (MODE))

#define HARD_REGNO_CALL_PART_CLOBBERED(REGNO, MODE)	\
  ((TARGET_32BIT && TARGET_POWERPC64			\
    && (GET_MODE_SIZE (MODE) > 4)  \
    && INT_REGNO_P (REGNO)) ? 1 : 0)

#define ALTIVEC_VECTOR_MODE(MODE)	\
	 ((MODE) == V16QImode		\
	  || (MODE) == V8HImode		\
	  || (MODE) == V4SFmode		\
	  || (MODE) == V4SImode)

#define SPE_VECTOR_MODE(MODE)		\
	((MODE) == V4HImode          	\
         || (MODE) == V2SFmode          \
         || (MODE) == V1DImode          \
         || (MODE) == V2SImode)

#define UNITS_PER_SIMD_WORD					\
        (TARGET_ALTIVEC ? UNITS_PER_ALTIVEC_WORD		\
	 : (TARGET_SPE ? UNITS_PER_SPE_WORD : UNITS_PER_WORD))

/* Value is TRUE if hard register REGNO can hold a value of
   machine-mode MODE.  */
#define HARD_REGNO_MODE_OK(REGNO, MODE) \
  rs6000_hard_regno_mode_ok_p[(int)(MODE)][REGNO]

/* Value is 1 if it is a good idea to tie two pseudo registers
   when one has mode MODE1 and one has mode MODE2.
   If HARD_REGNO_MODE_OK could produce different values for MODE1 and MODE2,
   for any hard reg, then this must be 0 for correct output.  */
#define MODES_TIEABLE_P(MODE1, MODE2) \
  (SCALAR_FLOAT_MODE_P (MODE1)			\
   ? SCALAR_FLOAT_MODE_P (MODE2)		\
   : SCALAR_FLOAT_MODE_P (MODE2)		\
   ? SCALAR_FLOAT_MODE_P (MODE1)		\
   : GET_MODE_CLASS (MODE1) == MODE_CC		\
   ? GET_MODE_CLASS (MODE2) == MODE_CC		\
   : GET_MODE_CLASS (MODE2) == MODE_CC		\
   ? GET_MODE_CLASS (MODE1) == MODE_CC		\
   : SPE_VECTOR_MODE (MODE1)			\
   ? SPE_VECTOR_MODE (MODE2)			\
   : SPE_VECTOR_MODE (MODE2)			\
   ? SPE_VECTOR_MODE (MODE1)			\
   : ALTIVEC_VECTOR_MODE (MODE1)		\
   ? ALTIVEC_VECTOR_MODE (MODE2)		\
   : ALTIVEC_VECTOR_MODE (MODE2)		\
   ? ALTIVEC_VECTOR_MODE (MODE1)		\
   : 1)

/* Post-reload, we can't use any new AltiVec registers, as we already
   emitted the vrsave mask.  */

#define HARD_REGNO_RENAME_OK(SRC, DST) \
  (! ALTIVEC_REGNO_P (DST) || regs_ever_live[DST])

/* A C expression returning the cost of moving data from a register of class
   CLASS1 to one of CLASS2.  */

#define REGISTER_MOVE_COST rs6000_register_move_cost

/* A C expressions returning the cost of moving data of MODE from a register to
   or from memory.  */

#define MEMORY_MOVE_COST rs6000_memory_move_cost

/* Specify the cost of a branch insn; roughly the number of extra insns that
   should be added to avoid a branch.

   Set this to 3 on the RS/6000 since that is roughly the average cost of an
   unscheduled conditional branch.  */

#define BRANCH_COST 3

/* Override BRANCH_COST heuristic which empirically produces worse
   performance for removing short circuiting from the logical ops.  */

#define LOGICAL_OP_NON_SHORT_CIRCUIT 0

/* A fixed register used at prologue and epilogue generation to fix
   addressing modes.  The SPE needs heavy addressing fixes at the last
   minute, and it's best to save a register for it.

   AltiVec also needs fixes, but we've gotten around using r11, which
   is actually wrong because when use_backchain_to_restore_sp is true,
   we end up clobbering r11.

   The AltiVec case needs to be fixed.  Dunno if we should break ABI
   compatibility and reserve a register for it as well..  */

#define FIXED_SCRATCH (TARGET_SPE ? 14 : 11)

/* Define this macro to change register usage conditional on target
   flags.  */

#define CONDITIONAL_REGISTER_USAGE rs6000_conditional_register_usage ()

/* Specify the registers used for certain standard purposes.
   The values of these macros are register numbers.  */

/* RS/6000 pc isn't overloaded on a register that the compiler knows about.  */
/* #define PC_REGNUM  */

/* Register to use for pushing function arguments.  */
#define STACK_POINTER_REGNUM 1

/* Base register for access to local variables of the function.  */
#define HARD_FRAME_POINTER_REGNUM 31

/* Base register for access to local variables of the function.  */
#define FRAME_POINTER_REGNUM 113

/* Value should be nonzero if functions must have frame pointers.
   Zero means the frame pointer need not be set up (and parms
   may be accessed via the stack pointer) in functions that seem suitable.
   This is computed in `reload', in reload1.c.  */
#define FRAME_POINTER_REQUIRED 0

/* Base register for access to arguments of the function.  */
#define ARG_POINTER_REGNUM 67

/* Place to put static chain when calling a function that requires it.  */
#define STATIC_CHAIN_REGNUM 11

/* Link register number.  */
#define LINK_REGISTER_REGNUM 65

/* Count register number.  */
#define COUNT_REGISTER_REGNUM 66

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

/* The RS/6000 has three types of registers, fixed-point, floating-point,
   and condition registers, plus three special registers, MQ, CTR, and the
   link register.  AltiVec adds a vector register class.

   However, r0 is special in that it cannot be used as a base register.
   So make a class for registers valid as base registers.

   Also, cr0 is the only condition code register that can be used in
   arithmetic insns, so make a separate class for it.  */

enum reg_class
{
  NO_REGS,
  BASE_REGS,
  GENERAL_REGS,
  FLOAT_REGS,
  ALTIVEC_REGS,
  VRSAVE_REGS,
  VSCR_REGS,
  SPE_ACC_REGS,
  SPEFSCR_REGS,
  NON_SPECIAL_REGS,
  MQ_REGS,
  LINK_REGS,
  CTR_REGS,
  LINK_OR_CTR_REGS,
  SPECIAL_REGS,
  SPEC_OR_GEN_REGS,
  CR0_REGS,
  CR_REGS,
  NON_FLOAT_REGS,
  XER_REGS,
  ALL_REGS,
  LIM_REG_CLASSES
};

#define N_REG_CLASSES (int) LIM_REG_CLASSES

/* Give names of register classes as strings for dump file.  */

#define REG_CLASS_NAMES							\
{									\
  "NO_REGS",								\
  "BASE_REGS",								\
  "GENERAL_REGS",							\
  "FLOAT_REGS",								\
  "ALTIVEC_REGS",							\
  "VRSAVE_REGS",							\
  "VSCR_REGS",								\
  "SPE_ACC_REGS",                                                       \
  "SPEFSCR_REGS",                                                       \
  "NON_SPECIAL_REGS",							\
  "MQ_REGS",								\
  "LINK_REGS",								\
  "CTR_REGS",								\
  "LINK_OR_CTR_REGS",							\
  "SPECIAL_REGS",							\
  "SPEC_OR_GEN_REGS",							\
  "CR0_REGS",								\
  "CR_REGS",								\
  "NON_FLOAT_REGS",							\
  "XER_REGS",								\
  "ALL_REGS"								\
}

/* Define which registers fit in which classes.
   This is an initializer for a vector of HARD_REG_SET
   of length N_REG_CLASSES.  */

#define REG_CLASS_CONTENTS						     \
{									     \
  { 0x00000000, 0x00000000, 0x00000000, 0x00000000 }, /* NO_REGS */	     \
  { 0xfffffffe, 0x00000000, 0x00000008, 0x00020000 }, /* BASE_REGS */	     \
  { 0xffffffff, 0x00000000, 0x00000008, 0x00020000 }, /* GENERAL_REGS */     \
  { 0x00000000, 0xffffffff, 0x00000000, 0x00000000 }, /* FLOAT_REGS */       \
  { 0x00000000, 0x00000000, 0xffffe000, 0x00001fff }, /* ALTIVEC_REGS */     \
  { 0x00000000, 0x00000000, 0x00000000, 0x00002000 }, /* VRSAVE_REGS */	     \
  { 0x00000000, 0x00000000, 0x00000000, 0x00004000 }, /* VSCR_REGS */	     \
  { 0x00000000, 0x00000000, 0x00000000, 0x00008000 }, /* SPE_ACC_REGS */     \
  { 0x00000000, 0x00000000, 0x00000000, 0x00010000 }, /* SPEFSCR_REGS */     \
  { 0xffffffff, 0xffffffff, 0x00000008, 0x00020000 }, /* NON_SPECIAL_REGS */ \
  { 0x00000000, 0x00000000, 0x00000001, 0x00000000 }, /* MQ_REGS */	     \
  { 0x00000000, 0x00000000, 0x00000002, 0x00000000 }, /* LINK_REGS */	     \
  { 0x00000000, 0x00000000, 0x00000004, 0x00000000 }, /* CTR_REGS */	     \
  { 0x00000000, 0x00000000, 0x00000006, 0x00000000 }, /* LINK_OR_CTR_REGS */ \
  { 0x00000000, 0x00000000, 0x00000007, 0x00002000 }, /* SPECIAL_REGS */     \
  { 0xffffffff, 0x00000000, 0x0000000f, 0x00022000 }, /* SPEC_OR_GEN_REGS */ \
  { 0x00000000, 0x00000000, 0x00000010, 0x00000000 }, /* CR0_REGS */	     \
  { 0x00000000, 0x00000000, 0x00000ff0, 0x00000000 }, /* CR_REGS */	     \
  { 0xffffffff, 0x00000000, 0x0000efff, 0x00020000 }, /* NON_FLOAT_REGS */   \
  { 0x00000000, 0x00000000, 0x00001000, 0x00000000 }, /* XER_REGS */	     \
  { 0xffffffff, 0xffffffff, 0xffffffff, 0x0003ffff }  /* ALL_REGS */	     \
}

/* The same information, inverted:
   Return the class number of the smallest class containing
   reg number REGNO.  This could be a conditional expression
   or could index an array.  */

#define REGNO_REG_CLASS(REGNO)			\
 ((REGNO) == 0 ? GENERAL_REGS			\
  : (REGNO) < 32 ? BASE_REGS			\
  : FP_REGNO_P (REGNO) ? FLOAT_REGS		\
  : ALTIVEC_REGNO_P (REGNO) ? ALTIVEC_REGS	\
  : (REGNO) == CR0_REGNO ? CR0_REGS		\
  : CR_REGNO_P (REGNO) ? CR_REGS		\
  : (REGNO) == MQ_REGNO ? MQ_REGS		\
  : (REGNO) == LINK_REGISTER_REGNUM ? LINK_REGS	\
  : (REGNO) == COUNT_REGISTER_REGNUM ? CTR_REGS	\
  : (REGNO) == ARG_POINTER_REGNUM ? BASE_REGS	\
  : (REGNO) == XER_REGNO ? XER_REGS		\
  : (REGNO) == VRSAVE_REGNO ? VRSAVE_REGS	\
  : (REGNO) == VSCR_REGNO ? VRSAVE_REGS		\
  : (REGNO) == SPE_ACC_REGNO ? SPE_ACC_REGS	\
  : (REGNO) == SPEFSCR_REGNO ? SPEFSCR_REGS	\
  : (REGNO) == FRAME_POINTER_REGNUM ? BASE_REGS	\
  : NO_REGS)

/* The class value for index registers, and the one for base regs.  */
#define INDEX_REG_CLASS GENERAL_REGS
#define BASE_REG_CLASS BASE_REGS

/* Given an rtx X being reloaded into a reg required to be
   in class CLASS, return the class of reg to actually use.
   In general this is just CLASS; but on some machines
   in some cases it is preferable to use a more restrictive class.

   On the RS/6000, we have to return NO_REGS when we want to reload a
   floating-point CONST_DOUBLE to force it to be copied to memory.

   We also don't want to reload integer values into floating-point
   registers if we can at all help it.  In fact, this can
   cause reload to die, if it tries to generate a reload of CTR
   into a FP register and discovers it doesn't have the memory location
   required.

   ??? Would it be a good idea to have reload do the converse, that is
   try to reload floating modes into FP registers if possible?
 */

#define PREFERRED_RELOAD_CLASS(X,CLASS)			\
  ((CONSTANT_P (X)					\
    && reg_classes_intersect_p ((CLASS), FLOAT_REGS))	\
   ? NO_REGS 						\
   : (GET_MODE_CLASS (GET_MODE (X)) == MODE_INT 	\
      && (CLASS) == NON_SPECIAL_REGS)			\
   ? GENERAL_REGS					\
   : (CLASS))

/* Return the register class of a scratch register needed to copy IN into
   or out of a register in CLASS in MODE.  If it can be done directly,
   NO_REGS is returned.  */

#define SECONDARY_RELOAD_CLASS(CLASS,MODE,IN) \
  rs6000_secondary_reload_class (CLASS, MODE, IN)

/* If we are copying between FP or AltiVec registers and anything
   else, we need a memory location.  */

#define SECONDARY_MEMORY_NEEDED(CLASS1,CLASS2,MODE) 		\
 ((CLASS1) != (CLASS2) && ((CLASS1) == FLOAT_REGS		\
			   || (CLASS2) == FLOAT_REGS		\
			   || (CLASS1) == ALTIVEC_REGS		\
			   || (CLASS2) == ALTIVEC_REGS))

/* Return the maximum number of consecutive registers
   needed to represent mode MODE in a register of class CLASS.

   On RS/6000, this is the size of MODE in words,
   except in the FP regs, where a single reg is enough for two words.  */
#define CLASS_MAX_NREGS(CLASS, MODE)					\
 (((CLASS) == FLOAT_REGS) 						\
  ? ((GET_MODE_SIZE (MODE) + UNITS_PER_FP_WORD - 1) / UNITS_PER_FP_WORD) \
  : (TARGET_E500_DOUBLE && (CLASS) == GENERAL_REGS && (MODE) == DFmode) \
  ? 1                                                                   \
  : ((GET_MODE_SIZE (MODE) + UNITS_PER_WORD - 1) / UNITS_PER_WORD))

/* Return nonzero if for CLASS a mode change from FROM to TO is invalid.  */

#define CANNOT_CHANGE_MODE_CLASS(FROM, TO, CLASS)			\
  (GET_MODE_SIZE (FROM) != GET_MODE_SIZE (TO)				\
   ? ((GET_MODE_SIZE (FROM) < 8 || GET_MODE_SIZE (TO) < 8		\
       || TARGET_IEEEQUAD)						\
      && reg_classes_intersect_p (FLOAT_REGS, CLASS))			\
   : (((TARGET_E500_DOUBLE						\
	&& ((((TO) == DFmode) + ((FROM) == DFmode)) == 1		\
	    || (((TO) == DImode) + ((FROM) == DImode)) == 1))		\
       || (TARGET_SPE							\
	   && (SPE_VECTOR_MODE (FROM) + SPE_VECTOR_MODE (TO)) == 1))	\
      && reg_classes_intersect_p (GENERAL_REGS, CLASS)))

/* Stack layout; function entry, exit and calling.  */

/* Enumeration to give which calling sequence to use.  */
enum rs6000_abi {
  ABI_NONE,
  ABI_AIX,			/* IBM's AIX */
  ABI_V4,			/* System V.4/eabi */
  ABI_DARWIN			/* Apple's Darwin (OS X kernel) */
};

extern enum rs6000_abi rs6000_current_abi;	/* available for use by subtarget */

/* Define this if pushing a word on the stack
   makes the stack pointer a smaller address.  */
#define STACK_GROWS_DOWNWARD

/* Offsets recorded in opcodes are a multiple of this alignment factor.  */
#define DWARF_CIE_DATA_ALIGNMENT (-((int) (TARGET_32BIT ? 4 : 8)))

/* Define this to nonzero if the nominal address of the stack frame
   is at the high-address end of the local variables;
   that is, each additional local variable allocated
   goes at a more negative offset in the frame.

   On the RS/6000, we grow upwards, from the area after the outgoing
   arguments.  */
#define FRAME_GROWS_DOWNWARD (flag_stack_protect != 0)

/* Size of the outgoing register save area */
#define RS6000_REG_SAVE ((DEFAULT_ABI == ABI_AIX			\
			  || DEFAULT_ABI == ABI_DARWIN)			\
			 ? (TARGET_64BIT ? 64 : 32)			\
			 : 0)

/* Size of the fixed area on the stack */
#define RS6000_SAVE_AREA \
  (((DEFAULT_ABI == ABI_AIX || DEFAULT_ABI == ABI_DARWIN) ? 24 : 8)	\
   << (TARGET_64BIT ? 1 : 0))

/* MEM representing address to save the TOC register */
#define RS6000_SAVE_TOC gen_rtx_MEM (Pmode, \
				     plus_constant (stack_pointer_rtx, \
						    (TARGET_32BIT ? 20 : 40)))

/* Align an address */
#define RS6000_ALIGN(n,a) (((n) + (a) - 1) & ~((a) - 1))

/* Offset within stack frame to start allocating local variables at.
   If FRAME_GROWS_DOWNWARD, this is the offset to the END of the
   first local allocated.  Otherwise, it is the offset to the BEGINNING
   of the first local allocated.

   On the RS/6000, the frame pointer is the same as the stack pointer,
   except for dynamic allocations.  So we start after the fixed area and
   outgoing parameter area.  */

#define STARTING_FRAME_OFFSET						\
  (FRAME_GROWS_DOWNWARD							\
   ? 0									\
   : (RS6000_ALIGN (current_function_outgoing_args_size,		\
		    TARGET_ALTIVEC ? 16 : 8)				\
      + RS6000_SAVE_AREA))

/* Offset from the stack pointer register to an item dynamically
   allocated on the stack, e.g., by `alloca'.

   The default value for this macro is `STACK_POINTER_OFFSET' plus the
   length of the outgoing arguments.  The default is correct for most
   machines.  See `function.c' for details.  */
#define STACK_DYNAMIC_OFFSET(FUNDECL)					\
  (RS6000_ALIGN (current_function_outgoing_args_size,			\
		 TARGET_ALTIVEC ? 16 : 8)				\
   + (STACK_POINTER_OFFSET))

/* If we generate an insn to push BYTES bytes,
   this says how many the stack pointer really advances by.
   On RS/6000, don't define this because there are no push insns.  */
/*  #define PUSH_ROUNDING(BYTES) */

/* Offset of first parameter from the argument pointer register value.
   On the RS/6000, we define the argument pointer to the start of the fixed
   area.  */
#define FIRST_PARM_OFFSET(FNDECL) RS6000_SAVE_AREA

/* Offset from the argument pointer register value to the top of
   stack.  This is different from FIRST_PARM_OFFSET because of the
   register save area.  */
#define ARG_POINTER_CFA_OFFSET(FNDECL) 0

/* Define this if stack space is still allocated for a parameter passed
   in a register.  The value is the number of bytes allocated to this
   area.  */
#define REG_PARM_STACK_SPACE(FNDECL)	RS6000_REG_SAVE

/* Define this if the above stack space is to be considered part of the
   space allocated by the caller.  */
#define OUTGOING_REG_PARM_STACK_SPACE

/* This is the difference between the logical top of stack and the actual sp.

   For the RS/6000, sp points past the fixed area.  */
#define STACK_POINTER_OFFSET RS6000_SAVE_AREA

/* Define this if the maximum size of all the outgoing args is to be
   accumulated and pushed during the prologue.  The amount can be
   found in the variable current_function_outgoing_args_size.  */
#define ACCUMULATE_OUTGOING_ARGS 1

/* Value is the number of bytes of arguments automatically
   popped when returning from a subroutine call.
   FUNDECL is the declaration node of the function (as a tree),
   FUNTYPE is the data type of the function (as a tree),
   or for a library call it is an identifier node for the subroutine name.
   SIZE is the number of bytes of arguments passed on the stack.  */

#define RETURN_POPS_ARGS(FUNDECL,FUNTYPE,SIZE) 0

/* Define how to find the value returned by a function.
   VALTYPE is the data type of the value (as a tree).
   If the precise function being called is known, FUNC is its FUNCTION_DECL;
   otherwise, FUNC is 0.  */

#define FUNCTION_VALUE(VALTYPE, FUNC) rs6000_function_value ((VALTYPE), (FUNC))

/* Define how to find the value returned by a library function
   assuming the value has mode MODE.  */

#define LIBCALL_VALUE(MODE) rs6000_libcall_value ((MODE))

/* DRAFT_V4_STRUCT_RET defaults off.  */
#define DRAFT_V4_STRUCT_RET 0

/* Let TARGET_RETURN_IN_MEMORY control what happens.  */
#define DEFAULT_PCC_STRUCT_RETURN 0

/* Mode of stack savearea.
   FUNCTION is VOIDmode because calling convention maintains SP.
   BLOCK needs Pmode for SP.
   NONLOCAL needs twice Pmode to maintain both backchain and SP.  */
#define STACK_SAVEAREA_MODE(LEVEL)	\
  (LEVEL == SAVE_FUNCTION ? VOIDmode	\
  : LEVEL == SAVE_NONLOCAL ? (TARGET_32BIT ? DImode : TImode) : Pmode)

/* Minimum and maximum general purpose registers used to hold arguments.  */
#define GP_ARG_MIN_REG 3
#define GP_ARG_MAX_REG 10
#define GP_ARG_NUM_REG (GP_ARG_MAX_REG - GP_ARG_MIN_REG + 1)

/* Minimum and maximum floating point registers used to hold arguments.  */
#define FP_ARG_MIN_REG 33
#define	FP_ARG_AIX_MAX_REG 45
#define	FP_ARG_V4_MAX_REG  40
#define	FP_ARG_MAX_REG ((DEFAULT_ABI == ABI_AIX				\
			 || DEFAULT_ABI == ABI_DARWIN)			\
			? FP_ARG_AIX_MAX_REG : FP_ARG_V4_MAX_REG)
#define FP_ARG_NUM_REG (FP_ARG_MAX_REG - FP_ARG_MIN_REG + 1)

/* Minimum and maximum AltiVec registers used to hold arguments.  */
#define ALTIVEC_ARG_MIN_REG (FIRST_ALTIVEC_REGNO + 2)
#define ALTIVEC_ARG_MAX_REG (ALTIVEC_ARG_MIN_REG + 11)
#define ALTIVEC_ARG_NUM_REG (ALTIVEC_ARG_MAX_REG - ALTIVEC_ARG_MIN_REG + 1)

/* Return registers */
#define GP_ARG_RETURN GP_ARG_MIN_REG
#define FP_ARG_RETURN FP_ARG_MIN_REG
#define ALTIVEC_ARG_RETURN (FIRST_ALTIVEC_REGNO + 2)

/* Flags for the call/call_value rtl operations set up by function_arg */
#define CALL_NORMAL		0x00000000	/* no special processing */
/* Bits in 0x00000001 are unused.  */
#define CALL_V4_CLEAR_FP_ARGS	0x00000002	/* V.4, no FP args passed */
#define CALL_V4_SET_FP_ARGS	0x00000004	/* V.4, FP args were passed */
#define CALL_LONG		0x00000008	/* always call indirect */
#define CALL_LIBCALL		0x00000010	/* libcall */

/* We don't have prologue and epilogue functions to save/restore
   everything for most ABIs.  */
#define WORLD_SAVE_P(INFO) 0

/* 1 if N is a possible register number for a function value
   as seen by the caller.

   On RS/6000, this is r3, fp1, and v2 (for AltiVec).  */
#define FUNCTION_VALUE_REGNO_P(N)					\
  ((N) == GP_ARG_RETURN							\
   || ((N) == FP_ARG_RETURN && TARGET_HARD_FLOAT && TARGET_FPRS)	\
   || ((N) == ALTIVEC_ARG_RETURN && TARGET_ALTIVEC && TARGET_ALTIVEC_ABI))

/* 1 if N is a possible register number for function argument passing.
   On RS/6000, these are r3-r10 and fp1-fp13.
   On AltiVec, v2 - v13 are used for passing vectors.  */
#define FUNCTION_ARG_REGNO_P(N)						\
  ((unsigned) (N) - GP_ARG_MIN_REG < GP_ARG_NUM_REG			\
   || ((unsigned) (N) - ALTIVEC_ARG_MIN_REG < ALTIVEC_ARG_NUM_REG	\
       && TARGET_ALTIVEC && TARGET_ALTIVEC_ABI)				\
   || ((unsigned) (N) - FP_ARG_MIN_REG < FP_ARG_NUM_REG			\
       && TARGET_HARD_FLOAT && TARGET_FPRS))

/* Define a data type for recording info about an argument list
   during the scan of that argument list.  This data type should
   hold all necessary information about the function itself
   and about the args processed so far, enough to enable macros
   such as FUNCTION_ARG to determine where the next arg should go.

   On the RS/6000, this is a structure.  The first element is the number of
   total argument words, the second is used to store the next
   floating-point register number, and the third says how many more args we
   have prototype types for.

   For ABI_V4, we treat these slightly differently -- `sysv_gregno' is
   the next available GP register, `fregno' is the next available FP
   register, and `words' is the number of words used on the stack.

   The varargs/stdarg support requires that this structure's size
   be a multiple of sizeof(int).  */

typedef struct rs6000_args
{
  int words;			/* # words used for passing GP registers */
  int fregno;			/* next available FP register */
  int vregno;			/* next available AltiVec register */
  int nargs_prototype;		/* # args left in the current prototype */
  int prototype;		/* Whether a prototype was defined */
  int stdarg;			/* Whether function is a stdarg function.  */
  int call_cookie;		/* Do special things for this call */
  int sysv_gregno;		/* next available GP register */
  int intoffset;		/* running offset in struct (darwin64) */
  int use_stack;		/* any part of struct on stack (darwin64) */
  int named;			/* false for varargs params */
} CUMULATIVE_ARGS;

/* Initialize a variable CUM of type CUMULATIVE_ARGS
   for a call to a function whose data type is FNTYPE.
   For a library call, FNTYPE is 0.  */

#define INIT_CUMULATIVE_ARGS(CUM, FNTYPE, LIBNAME, INDIRECT, N_NAMED_ARGS) \
  init_cumulative_args (&CUM, FNTYPE, LIBNAME, FALSE, FALSE, N_NAMED_ARGS)

/* Similar, but when scanning the definition of a procedure.  We always
   set NARGS_PROTOTYPE large so we never return an EXPR_LIST.  */

#define INIT_CUMULATIVE_INCOMING_ARGS(CUM, FNTYPE, LIBNAME) \
  init_cumulative_args (&CUM, FNTYPE, LIBNAME, TRUE, FALSE, 1000)

/* Like INIT_CUMULATIVE_ARGS' but only used for outgoing libcalls.  */

#define INIT_CUMULATIVE_LIBCALL_ARGS(CUM, MODE, LIBNAME) \
  init_cumulative_args (&CUM, NULL_TREE, LIBNAME, FALSE, TRUE, 0)

/* Update the data in CUM to advance over an argument
   of mode MODE and data type TYPE.
   (TYPE is null for libcalls where that information may not be available.)  */

#define FUNCTION_ARG_ADVANCE(CUM, MODE, TYPE, NAMED)	\
  function_arg_advance (&CUM, MODE, TYPE, NAMED, 0)

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
    (otherwise it is an extra parameter matching an ellipsis).

   On RS/6000 the first eight words of non-FP are normally in registers
   and the rest are pushed.  The first 13 FP args are in registers.

   If this is floating-point and no prototype is specified, we use
   both an FP and integer register (or possibly FP reg and stack).  Library
   functions (when TYPE is zero) always have the proper types for args,
   so we can pass the FP value just in one register.  emit_library_function
   doesn't support EXPR_LIST anyway.  */

#define FUNCTION_ARG(CUM, MODE, TYPE, NAMED) \
  function_arg (&CUM, MODE, TYPE, NAMED)

/* If defined, a C expression which determines whether, and in which
   direction, to pad out an argument with extra space.  The value
   should be of type `enum direction': either `upward' to pad above
   the argument, `downward' to pad below, or `none' to inhibit
   padding.  */

#define FUNCTION_ARG_PADDING(MODE, TYPE) function_arg_padding (MODE, TYPE)

/* If defined, a C expression that gives the alignment boundary, in bits,
   of an argument with the specified mode and type.  If it is not defined,
   PARM_BOUNDARY is used for all arguments.  */

#define FUNCTION_ARG_BOUNDARY(MODE, TYPE) \
  function_arg_boundary (MODE, TYPE)

/* Implement `va_start' for varargs and stdarg.  */
#define EXPAND_BUILTIN_VA_START(valist, nextarg) \
  rs6000_va_start (valist, nextarg)

#define PAD_VARARGS_DOWN \
   (FUNCTION_ARG_PADDING (TYPE_MODE (type), type) == downward)

/* Output assembler code to FILE to increment profiler label # LABELNO
   for profiling a function entry.  */

#define FUNCTION_PROFILER(FILE, LABELNO)	\
  output_function_profiler ((FILE), (LABELNO));

/* EXIT_IGNORE_STACK should be nonzero if, when returning from a function,
   the stack pointer does not matter. No definition is equivalent to
   always zero.

   On the RS/6000, this is nonzero because we can restore the stack from
   its backpointer, which we maintain.  */
#define EXIT_IGNORE_STACK	1

/* Define this macro as a C expression that is nonzero for registers
   that are used by the epilogue or the return' pattern.  The stack
   and frame pointer registers are already be assumed to be used as
   needed.  */

#define	EPILOGUE_USES(REGNO)					\
  ((reload_completed && (REGNO) == LINK_REGISTER_REGNUM)	\
   || (TARGET_ALTIVEC && (REGNO) == VRSAVE_REGNO)		\
   || (current_function_calls_eh_return				\
       && TARGET_AIX						\
       && (REGNO) == 2))


/* TRAMPOLINE_TEMPLATE deleted */

/* Length in units of the trampoline for entering a nested function.  */

#define TRAMPOLINE_SIZE rs6000_trampoline_size ()

/* Emit RTL insns to initialize the variable parts of a trampoline.
   FNADDR is an RTX for the address of the function's pure code.
   CXT is an RTX for the static chain value for the function.  */

#define INITIALIZE_TRAMPOLINE(ADDR, FNADDR, CXT)		\
  rs6000_initialize_trampoline (ADDR, FNADDR, CXT)

/* Definitions for __builtin_return_address and __builtin_frame_address.
   __builtin_return_address (0) should give link register (65), enable
   this.  */
/* This should be uncommented, so that the link register is used, but
   currently this would result in unmatched insns and spilling fixed
   registers so we'll leave it for another day.  When these problems are
   taken care of one additional fetch will be necessary in RETURN_ADDR_RTX.
   (mrs) */
/* #define RETURN_ADDR_IN_PREVIOUS_FRAME */

/* Number of bytes into the frame return addresses can be found.  See
   rs6000_stack_info in rs6000.c for more information on how the different
   abi's store the return address.  */
#define RETURN_ADDRESS_OFFSET						\
 ((DEFAULT_ABI == ABI_AIX						\
   || DEFAULT_ABI == ABI_DARWIN)	? (TARGET_32BIT ? 8 : 16) :	\
  (DEFAULT_ABI == ABI_V4)		? 4 :				\
  (internal_error ("RETURN_ADDRESS_OFFSET not supported"), 0))

/* The current return address is in link register (65).  The return address
   of anything farther back is accessed normally at an offset of 8 from the
   frame pointer.  */
#define RETURN_ADDR_RTX(COUNT, FRAME)                 \
  (rs6000_return_addr (COUNT, FRAME))


/* Definitions for register eliminations.

   We have two registers that can be eliminated on the RS/6000.  First, the
   frame pointer register can often be eliminated in favor of the stack
   pointer register.  Secondly, the argument pointer register can always be
   eliminated; it is replaced with either the stack or frame pointer.

   In addition, we use the elimination mechanism to see if r30 is needed
   Initially we assume that it isn't.  If it is, we spill it.  This is done
   by making it an eliminable register.  We replace it with itself so that
   if it isn't needed, then existing uses won't be modified.  */

/* This is an array of structures.  Each structure initializes one pair
   of eliminable registers.  The "from" register number is given first,
   followed by "to".  Eliminations of the same "from" register are listed
   in order of preference.  */
#define ELIMINABLE_REGS					\
{{ HARD_FRAME_POINTER_REGNUM, STACK_POINTER_REGNUM},	\
 { FRAME_POINTER_REGNUM, STACK_POINTER_REGNUM},		\
 { FRAME_POINTER_REGNUM, HARD_FRAME_POINTER_REGNUM},	\
 { ARG_POINTER_REGNUM, STACK_POINTER_REGNUM},		\
 { ARG_POINTER_REGNUM, HARD_FRAME_POINTER_REGNUM},	\
 { RS6000_PIC_OFFSET_TABLE_REGNUM, RS6000_PIC_OFFSET_TABLE_REGNUM } }

/* Given FROM and TO register numbers, say whether this elimination is allowed.
   Frame pointer elimination is automatically handled.

   For the RS/6000, if frame pointer elimination is being done, we would like
   to convert ap into fp, not sp.

   We need r30 if -mminimal-toc was specified, and there are constant pool
   references.  */

#define CAN_ELIMINATE(FROM, TO)						\
 ((FROM) == ARG_POINTER_REGNUM && (TO) == STACK_POINTER_REGNUM		\
  ? ! frame_pointer_needed						\
  : (FROM) == RS6000_PIC_OFFSET_TABLE_REGNUM 				\
  ? ! TARGET_MINIMAL_TOC || TARGET_NO_TOC || get_pool_size () == 0	\
  : 1)

/* Define the offset between two registers, one to be eliminated, and the other
   its replacement, at the start of a routine.  */
#define INITIAL_ELIMINATION_OFFSET(FROM, TO, OFFSET) \
  ((OFFSET) = rs6000_initial_elimination_offset(FROM, TO))

/* Addressing modes, and classification of registers for them.  */

#define HAVE_PRE_DECREMENT 1
#define HAVE_PRE_INCREMENT 1

/* Macros to check register numbers against specific register classes.  */

/* These assume that REGNO is a hard or pseudo reg number.
   They give nonzero only if REGNO is a hard reg of the suitable class
   or a pseudo reg currently allocated to a suitable hard reg.
   Since they use reg_renumber, they are safe only once reg_renumber
   has been allocated, which happens in local-alloc.c.  */

#define REGNO_OK_FOR_INDEX_P(REGNO)				\
((REGNO) < FIRST_PSEUDO_REGISTER				\
 ? (REGNO) <= 31 || (REGNO) == 67				\
   || (REGNO) == FRAME_POINTER_REGNUM				\
 : (reg_renumber[REGNO] >= 0					\
    && (reg_renumber[REGNO] <= 31 || reg_renumber[REGNO] == 67	\
	|| reg_renumber[REGNO] == FRAME_POINTER_REGNUM)))

#define REGNO_OK_FOR_BASE_P(REGNO)				\
((REGNO) < FIRST_PSEUDO_REGISTER				\
 ? ((REGNO) > 0 && (REGNO) <= 31) || (REGNO) == 67		\
   || (REGNO) == FRAME_POINTER_REGNUM				\
 : (reg_renumber[REGNO] > 0					\
    && (reg_renumber[REGNO] <= 31 || reg_renumber[REGNO] == 67	\
	|| reg_renumber[REGNO] == FRAME_POINTER_REGNUM)))

/* Maximum number of registers that can appear in a valid memory address.  */

#define MAX_REGS_PER_ADDRESS 2

/* Recognize any constant value that is a valid address.  */

#define CONSTANT_ADDRESS_P(X)   \
  (GET_CODE (X) == LABEL_REF || GET_CODE (X) == SYMBOL_REF		\
   || GET_CODE (X) == CONST_INT || GET_CODE (X) == CONST		\
   || GET_CODE (X) == HIGH)

/* Nonzero if the constant value X is a legitimate general operand.
   It is given that X satisfies CONSTANT_P or is a CONST_DOUBLE.

   On the RS/6000, all integer constants are acceptable, most won't be valid
   for particular insns, though.  Only easy FP constants are
   acceptable.  */

#define LEGITIMATE_CONSTANT_P(X)				\
  (((GET_CODE (X) != CONST_DOUBLE				\
     && GET_CODE (X) != CONST_VECTOR)				\
    || GET_MODE (X) == VOIDmode					\
    || (TARGET_POWERPC64 && GET_MODE (X) == DImode)		\
    || easy_fp_constant (X, GET_MODE (X))			\
    || easy_vector_constant (X, GET_MODE (X)))			\
   && !rs6000_tls_referenced_p (X))

#define EASY_VECTOR_15(n) ((n) >= -16 && (n) <= 15)
#define EASY_VECTOR_15_ADD_SELF(n) (!EASY_VECTOR_15((n))	\
				    && EASY_VECTOR_15((n) >> 1) \
				    && ((n) & 1) == 0)

/* The macros REG_OK_FOR..._P assume that the arg is a REG rtx
   and check its validity for a certain class.
   We have two alternate definitions for each of them.
   The usual definition accepts all pseudo regs; the other rejects
   them unless they have been allocated suitable hard regs.
   The symbol REG_OK_STRICT causes the latter definition to be used.

   Most source files want to accept pseudo regs in the hope that
   they will get allocated to the class that the insn wants them to be in.
   Source files for reload pass need to be strict.
   After reload, it makes no difference, since pseudo regs have
   been eliminated by then.  */

#ifdef REG_OK_STRICT
# define REG_OK_STRICT_FLAG 1
#else
# define REG_OK_STRICT_FLAG 0
#endif

/* Nonzero if X is a hard reg that can be used as an index
   or if it is a pseudo reg in the non-strict case.  */
#define INT_REG_OK_FOR_INDEX_P(X, STRICT)			\
  ((!(STRICT) && REGNO (X) >= FIRST_PSEUDO_REGISTER)		\
   || REGNO_OK_FOR_INDEX_P (REGNO (X)))

/* Nonzero if X is a hard reg that can be used as a base reg
   or if it is a pseudo reg in the non-strict case.  */
#define INT_REG_OK_FOR_BASE_P(X, STRICT)			\
  ((!(STRICT) && REGNO (X) >= FIRST_PSEUDO_REGISTER)		\
   || REGNO_OK_FOR_BASE_P (REGNO (X)))

#define REG_OK_FOR_INDEX_P(X) INT_REG_OK_FOR_INDEX_P (X, REG_OK_STRICT_FLAG)
#define REG_OK_FOR_BASE_P(X)  INT_REG_OK_FOR_BASE_P (X, REG_OK_STRICT_FLAG)

/* GO_IF_LEGITIMATE_ADDRESS recognizes an RTL expression
   that is a valid memory address for an instruction.
   The MODE argument is the machine mode for the MEM expression
   that wants to use this address.

   On the RS/6000, there are four valid addresses: a SYMBOL_REF that
   refers to a constant pool entry of an address (or the sum of it
   plus a constant), a short (16-bit signed) constant plus a register,
   the sum of two registers, or a register indirect, possibly with an
   auto-increment.  For DFmode and DImode with a constant plus register,
   we must ensure that both words are addressable or PowerPC64 with offset
   word aligned.

   For modes spanning multiple registers (DFmode in 32-bit GPRs,
   32-bit DImode, TImode), indexed addressing cannot be used because
   adjacent memory cells are accessed by adding word-sized offsets
   during assembly output.  */

#define GO_IF_LEGITIMATE_ADDRESS(MODE, X, ADDR)			\
{ if (rs6000_legitimate_address (MODE, X, REG_OK_STRICT_FLAG))	\
    goto ADDR;							\
}

/* Try machine-dependent ways of modifying an illegitimate address
   to be legitimate.  If we find one, return the new, valid address.
   This macro is used in only one place: `memory_address' in explow.c.

   OLDX is the address as it was before break_out_memory_refs was called.
   In some cases it is useful to look at this to decide what needs to be done.

   MODE and WIN are passed so that this macro can use
   GO_IF_LEGITIMATE_ADDRESS.

   It is always safe for this macro to do nothing.  It exists to recognize
   opportunities to optimize the output.

   On RS/6000, first check for the sum of a register with a constant
   integer that is out of range.  If so, generate code to add the
   constant with the low-order 16 bits masked to the register and force
   this result into another register (this can be done with `cau').
   Then generate an address of REG+(CONST&0xffff), allowing for the
   possibility of bit 16 being a one.

   Then check for the sum of a register and something not constant, try to
   load the other things into a register and return the sum.  */

#define LEGITIMIZE_ADDRESS(X,OLDX,MODE,WIN)			\
{  rtx result = rs6000_legitimize_address (X, OLDX, MODE);	\
   if (result != NULL_RTX)					\
     {								\
       (X) = result;						\
       goto WIN;						\
     }								\
}

/* Try a machine-dependent way of reloading an illegitimate address
   operand.  If we find one, push the reload and jump to WIN.  This
   macro is used in only one place: `find_reloads_address' in reload.c.

   Implemented on rs6000 by rs6000_legitimize_reload_address.
   Note that (X) is evaluated twice; this is safe in current usage.  */

#define LEGITIMIZE_RELOAD_ADDRESS(X,MODE,OPNUM,TYPE,IND_LEVELS,WIN)	     \
do {									     \
  int win;								     \
  (X) = rs6000_legitimize_reload_address ((X), (MODE), (OPNUM),		     \
			(int)(TYPE), (IND_LEVELS), &win);		     \
  if ( win )								     \
    goto WIN;								     \
} while (0)

/* Go to LABEL if ADDR (a legitimate address expression)
   has an effect that depends on the machine mode it is used for.  */

#define GO_IF_MODE_DEPENDENT_ADDRESS(ADDR,LABEL)		\
do {								\
  if (rs6000_mode_dependent_address (ADDR))			\
    goto LABEL;							\
} while (0)

/* The register number of the register used to address a table of
   static data addresses in memory.  In some cases this register is
   defined by a processor's "application binary interface" (ABI).
   When this macro is defined, RTL is generated for this register
   once, as with the stack pointer and frame pointer registers.  If
   this macro is not defined, it is up to the machine-dependent files
   to allocate such a register (if necessary).  */

#define RS6000_PIC_OFFSET_TABLE_REGNUM 30
#define PIC_OFFSET_TABLE_REGNUM (flag_pic ? RS6000_PIC_OFFSET_TABLE_REGNUM : INVALID_REGNUM)

#define TOC_REGISTER (TARGET_MINIMAL_TOC ? RS6000_PIC_OFFSET_TABLE_REGNUM : 2)

/* Define this macro if the register defined by
   `PIC_OFFSET_TABLE_REGNUM' is clobbered by calls.  Do not define
   this macro if `PIC_OFFSET_TABLE_REGNUM' is not defined.  */

/* #define PIC_OFFSET_TABLE_REG_CALL_CLOBBERED */

/* A C expression that is nonzero if X is a legitimate immediate
   operand on the target machine when generating position independent
   code.  You can assume that X satisfies `CONSTANT_P', so you need
   not check this.  You can also assume FLAG_PIC is true, so you need
   not check it either.  You need not define this macro if all
   constants (including `SYMBOL_REF') can be immediate operands when
   generating position independent code.  */

/* #define LEGITIMATE_PIC_OPERAND_P (X) */

/* Define this if some processing needs to be done immediately before
   emitting code for an insn.  */

/* #define FINAL_PRESCAN_INSN(INSN,OPERANDS,NOPERANDS) */

/* Specify the machine mode that this machine uses
   for the index in the tablejump instruction.  */
#define CASE_VECTOR_MODE SImode

/* Define as C expression which evaluates to nonzero if the tablejump
   instruction expects the table to contain offsets from the address of the
   table.
   Do not define this if the table should contain absolute addresses.  */
#define CASE_VECTOR_PC_RELATIVE 1

/* Define this as 1 if `char' should by default be signed; else as 0.  */
#define DEFAULT_SIGNED_CHAR 0

/* This flag, if defined, says the same insns that convert to a signed fixnum
   also convert validly to an unsigned one.  */

/* #define FIXUNS_TRUNC_LIKE_FIX_TRUNC */

/* An integer expression for the size in bits of the largest integer machine
   mode that should actually be used.  */

/* Allow pairs of registers to be used, which is the intent of the default.  */
#define MAX_FIXED_MODE_SIZE GET_MODE_BITSIZE (TARGET_POWERPC64 ? TImode : DImode)

/* Max number of bytes we can move from memory to memory
   in one reasonably fast instruction.  */
#define MOVE_MAX (! TARGET_POWERPC64 ? 4 : 8)
#define MAX_MOVE_MAX 8

/* Nonzero if access to memory by bytes is no faster than for words.
   Also nonzero if doing byte operations (specifically shifts) in registers
   is undesirable.  */
#define SLOW_BYTE_ACCESS 1

/* Define if operations between registers always perform the operation
   on the full register even if a narrower mode is specified.  */
#define WORD_REGISTER_OPERATIONS

/* Define if loading in MODE, an integral mode narrower than BITS_PER_WORD
   will either zero-extend or sign-extend.  The value of this macro should
   be the code that says which one of the two operations is implicitly
   done, UNKNOWN if none.  */
#define LOAD_EXTEND_OP(MODE) ZERO_EXTEND

/* Define if loading short immediate values into registers sign extends.  */
#define SHORT_IMMEDIATES_SIGN_EXTEND

/* Value is 1 if truncating an integer of INPREC bits to OUTPREC bits
   is done just by pretending it is already truncated.  */
#define TRULY_NOOP_TRUNCATION(OUTPREC, INPREC) 1

/* The cntlzw and cntlzd instructions return 32 and 64 for input of zero.  */
#define CLZ_DEFINED_VALUE_AT_ZERO(MODE, VALUE) \
  ((VALUE) = ((MODE) == SImode ? 32 : 64))

/* The CTZ patterns return -1 for input of zero.  */
#define CTZ_DEFINED_VALUE_AT_ZERO(MODE, VALUE) ((VALUE) = -1)

/* Specify the machine mode that pointers have.
   After generation of rtl, the compiler makes no further distinction
   between pointers and any other objects of this machine mode.  */
#define Pmode (TARGET_32BIT ? SImode : DImode)

/* Supply definition of STACK_SIZE_MODE for allocate_dynamic_stack_space.  */
#define STACK_SIZE_MODE (TARGET_32BIT ? SImode : DImode)

/* Mode of a function address in a call instruction (for indexing purposes).
   Doesn't matter on RS/6000.  */
#define FUNCTION_MODE SImode

/* Define this if addresses of constant functions
   shouldn't be put through pseudo regs where they can be cse'd.
   Desirable on machines where ordinary constants are expensive
   but a CALL with constant address is cheap.  */
#define NO_FUNCTION_CSE

/* Define this to be nonzero if shift instructions ignore all but the low-order
   few bits.

   The sle and sre instructions which allow SHIFT_COUNT_TRUNCATED
   have been dropped from the PowerPC architecture.  */

#define SHIFT_COUNT_TRUNCATED (TARGET_POWER ? 1 : 0)

/* Adjust the length of an INSN.  LENGTH is the currently-computed length and
   should be adjusted to reflect any required changes.  This macro is used when
   there is some systematic length adjustment required that would be difficult
   to express in the length attribute.  */

/* #define ADJUST_INSN_LENGTH(X,LENGTH) */

/* Given a comparison code (EQ, NE, etc.) and the first operand of a
   COMPARE, return the mode to be used for the comparison.  For
   floating-point, CCFPmode should be used.  CCUNSmode should be used
   for unsigned comparisons.  CCEQmode should be used when we are
   doing an inequality comparison on the result of a
   comparison.  CCmode should be used in all other cases.  */

#define SELECT_CC_MODE(OP,X,Y) \
  (SCALAR_FLOAT_MODE_P (GET_MODE (X)) ? CCFPmode	\
   : (OP) == GTU || (OP) == LTU || (OP) == GEU || (OP) == LEU ? CCUNSmode \
   : (((OP) == EQ || (OP) == NE) && COMPARISON_P (X)			  \
      ? CCEQmode : CCmode))

/* Can the condition code MODE be safely reversed?  This is safe in
   all cases on this port, because at present it doesn't use the
   trapping FP comparisons (fcmpo).  */
#define REVERSIBLE_CC_MODE(MODE) 1

/* Given a condition code and a mode, return the inverse condition.  */
#define REVERSE_CONDITION(CODE, MODE) rs6000_reverse_condition (MODE, CODE)

/* Define the information needed to generate branch and scc insns.  This is
   stored from the compare operation.  */

extern GTY(()) rtx rs6000_compare_op0;
extern GTY(()) rtx rs6000_compare_op1;
extern int rs6000_compare_fp_p;

/* Control the assembler format that we output.  */

/* A C string constant describing how to begin a comment in the target
   assembler language.  The compiler assumes that the comment will end at
   the end of the line.  */
#define ASM_COMMENT_START " #"

/* Flag to say the TOC is initialized */
extern int toc_initialized;

/* Macro to output a special constant pool entry.  Go to WIN if we output
   it.  Otherwise, it is written the usual way.

   On the RS/6000, toc entries are handled this way.  */

#define ASM_OUTPUT_SPECIAL_POOL_ENTRY(FILE, X, MODE, ALIGN, LABELNO, WIN) \
{ if (ASM_OUTPUT_SPECIAL_POOL_ENTRY_P (X, MODE))			  \
    {									  \
      output_toc (FILE, X, LABELNO, MODE);				  \
      goto WIN;								  \
    }									  \
}

#ifdef HAVE_GAS_WEAK
#define RS6000_WEAK 1
#else
#define RS6000_WEAK 0
#endif

#if RS6000_WEAK
/* Used in lieu of ASM_WEAKEN_LABEL.  */
#define	ASM_WEAKEN_DECL(FILE, DECL, NAME, VAL)			 	\
  do									\
    {									\
      fputs ("\t.weak\t", (FILE));					\
      RS6000_OUTPUT_BASENAME ((FILE), (NAME)); 				\
      if ((DECL) && TREE_CODE (DECL) == FUNCTION_DECL			\
	  && DEFAULT_ABI == ABI_AIX && DOT_SYMBOLS)			\
	{								\
	  if (TARGET_XCOFF)						\
	    fputs ("[DS]", (FILE));					\
	  fputs ("\n\t.weak\t.", (FILE));				\
	  RS6000_OUTPUT_BASENAME ((FILE), (NAME)); 			\
	}								\
      fputc ('\n', (FILE));						\
      if (VAL)								\
	{								\
	  ASM_OUTPUT_DEF ((FILE), (NAME), (VAL));			\
	  if ((DECL) && TREE_CODE (DECL) == FUNCTION_DECL		\
	      && DEFAULT_ABI == ABI_AIX && DOT_SYMBOLS)			\
	    {								\
	      fputs ("\t.set\t.", (FILE));				\
	      RS6000_OUTPUT_BASENAME ((FILE), (NAME));			\
	      fputs (",.", (FILE));					\
	      RS6000_OUTPUT_BASENAME ((FILE), (VAL));			\
	      fputc ('\n', (FILE));					\
	    }								\
	}								\
    }									\
  while (0)
#endif

#if HAVE_GAS_WEAKREF
#define ASM_OUTPUT_WEAKREF(FILE, DECL, NAME, VALUE)			\
  do									\
    {									\
      fputs ("\t.weakref\t", (FILE));					\
      RS6000_OUTPUT_BASENAME ((FILE), (NAME)); 				\
      fputs (", ", (FILE));						\
      RS6000_OUTPUT_BASENAME ((FILE), (VALUE));				\
      if ((DECL) && TREE_CODE (DECL) == FUNCTION_DECL			\
	  && DEFAULT_ABI == ABI_AIX && DOT_SYMBOLS)			\
	{								\
	  fputs ("\n\t.weakref\t.", (FILE));				\
	  RS6000_OUTPUT_BASENAME ((FILE), (NAME)); 			\
	  fputs (", .", (FILE));					\
	  RS6000_OUTPUT_BASENAME ((FILE), (VALUE));			\
	}								\
      fputc ('\n', (FILE));						\
    } while (0)
#endif

/* This implements the `alias' attribute.  */
#undef	ASM_OUTPUT_DEF_FROM_DECLS
#define	ASM_OUTPUT_DEF_FROM_DECLS(FILE, DECL, TARGET)			\
  do									\
    {									\
      const char *alias = XSTR (XEXP (DECL_RTL (DECL), 0), 0);		\
      const char *name = IDENTIFIER_POINTER (TARGET);			\
      if (TREE_CODE (DECL) == FUNCTION_DECL				\
	  && DEFAULT_ABI == ABI_AIX && DOT_SYMBOLS)			\
	{								\
	  if (TREE_PUBLIC (DECL))					\
	    {								\
	      if (!RS6000_WEAK || !DECL_WEAK (DECL))			\
		{							\
		  fputs ("\t.globl\t.", FILE);				\
		  RS6000_OUTPUT_BASENAME (FILE, alias);			\
		  putc ('\n', FILE);					\
		}							\
	    }								\
	  else if (TARGET_XCOFF)					\
	    {								\
	      fputs ("\t.lglobl\t.", FILE);				\
	      RS6000_OUTPUT_BASENAME (FILE, alias);			\
	      putc ('\n', FILE);					\
	    }								\
	  fputs ("\t.set\t.", FILE);					\
	  RS6000_OUTPUT_BASENAME (FILE, alias);				\
	  fputs (",.", FILE);						\
	  RS6000_OUTPUT_BASENAME (FILE, name);				\
	  fputc ('\n', FILE);						\
	}								\
      ASM_OUTPUT_DEF (FILE, alias, name);				\
    }									\
   while (0)

#define TARGET_ASM_FILE_START rs6000_file_start

/* Output to assembler file text saying following lines
   may contain character constants, extra white space, comments, etc.  */

#define ASM_APP_ON ""

/* Output to assembler file text saying following lines
   no longer contain unusual constructs.  */

#define ASM_APP_OFF ""

/* How to refer to registers in assembler output.
   This sequence is indexed by compiler's hard-register-number (see above).  */

extern char rs6000_reg_names[][8];	/* register names (0 vs. %r0).  */

#define REGISTER_NAMES							\
{									\
  &rs6000_reg_names[ 0][0],	/* r0   */				\
  &rs6000_reg_names[ 1][0],	/* r1	*/				\
  &rs6000_reg_names[ 2][0],     /* r2	*/				\
  &rs6000_reg_names[ 3][0],	/* r3	*/				\
  &rs6000_reg_names[ 4][0],	/* r4	*/				\
  &rs6000_reg_names[ 5][0],	/* r5	*/				\
  &rs6000_reg_names[ 6][0],	/* r6	*/				\
  &rs6000_reg_names[ 7][0],	/* r7	*/				\
  &rs6000_reg_names[ 8][0],	/* r8	*/				\
  &rs6000_reg_names[ 9][0],	/* r9	*/				\
  &rs6000_reg_names[10][0],	/* r10  */				\
  &rs6000_reg_names[11][0],	/* r11  */				\
  &rs6000_reg_names[12][0],	/* r12  */				\
  &rs6000_reg_names[13][0],	/* r13  */				\
  &rs6000_reg_names[14][0],	/* r14  */				\
  &rs6000_reg_names[15][0],	/* r15  */				\
  &rs6000_reg_names[16][0],	/* r16  */				\
  &rs6000_reg_names[17][0],	/* r17  */				\
  &rs6000_reg_names[18][0],	/* r18  */				\
  &rs6000_reg_names[19][0],	/* r19  */				\
  &rs6000_reg_names[20][0],	/* r20  */				\
  &rs6000_reg_names[21][0],	/* r21  */				\
  &rs6000_reg_names[22][0],	/* r22  */				\
  &rs6000_reg_names[23][0],	/* r23  */				\
  &rs6000_reg_names[24][0],	/* r24  */				\
  &rs6000_reg_names[25][0],	/* r25  */				\
  &rs6000_reg_names[26][0],	/* r26  */				\
  &rs6000_reg_names[27][0],	/* r27  */				\
  &rs6000_reg_names[28][0],	/* r28  */				\
  &rs6000_reg_names[29][0],	/* r29  */				\
  &rs6000_reg_names[30][0],	/* r30  */				\
  &rs6000_reg_names[31][0],	/* r31  */				\
									\
  &rs6000_reg_names[32][0],     /* fr0  */				\
  &rs6000_reg_names[33][0],	/* fr1  */				\
  &rs6000_reg_names[34][0],	/* fr2  */				\
  &rs6000_reg_names[35][0],	/* fr3  */				\
  &rs6000_reg_names[36][0],	/* fr4  */				\
  &rs6000_reg_names[37][0],	/* fr5  */				\
  &rs6000_reg_names[38][0],	/* fr6  */				\
  &rs6000_reg_names[39][0],	/* fr7  */				\
  &rs6000_reg_names[40][0],	/* fr8  */				\
  &rs6000_reg_names[41][0],	/* fr9  */				\
  &rs6000_reg_names[42][0],	/* fr10 */				\
  &rs6000_reg_names[43][0],	/* fr11 */				\
  &rs6000_reg_names[44][0],	/* fr12 */				\
  &rs6000_reg_names[45][0],	/* fr13 */				\
  &rs6000_reg_names[46][0],	/* fr14 */				\
  &rs6000_reg_names[47][0],	/* fr15 */				\
  &rs6000_reg_names[48][0],	/* fr16 */				\
  &rs6000_reg_names[49][0],	/* fr17 */				\
  &rs6000_reg_names[50][0],	/* fr18 */				\
  &rs6000_reg_names[51][0],	/* fr19 */				\
  &rs6000_reg_names[52][0],	/* fr20 */				\
  &rs6000_reg_names[53][0],	/* fr21 */				\
  &rs6000_reg_names[54][0],	/* fr22 */				\
  &rs6000_reg_names[55][0],	/* fr23 */				\
  &rs6000_reg_names[56][0],	/* fr24 */				\
  &rs6000_reg_names[57][0],	/* fr25 */				\
  &rs6000_reg_names[58][0],	/* fr26 */				\
  &rs6000_reg_names[59][0],	/* fr27 */				\
  &rs6000_reg_names[60][0],	/* fr28 */				\
  &rs6000_reg_names[61][0],	/* fr29 */				\
  &rs6000_reg_names[62][0],	/* fr30 */				\
  &rs6000_reg_names[63][0],	/* fr31 */				\
									\
  &rs6000_reg_names[64][0],     /* mq   */				\
  &rs6000_reg_names[65][0],	/* lr   */				\
  &rs6000_reg_names[66][0],	/* ctr  */				\
  &rs6000_reg_names[67][0],	/* ap   */				\
									\
  &rs6000_reg_names[68][0],	/* cr0  */				\
  &rs6000_reg_names[69][0],	/* cr1  */				\
  &rs6000_reg_names[70][0],	/* cr2  */				\
  &rs6000_reg_names[71][0],	/* cr3  */				\
  &rs6000_reg_names[72][0],	/* cr4  */				\
  &rs6000_reg_names[73][0],	/* cr5  */				\
  &rs6000_reg_names[74][0],	/* cr6  */				\
  &rs6000_reg_names[75][0],	/* cr7  */				\
									\
  &rs6000_reg_names[76][0],	/* xer  */				\
									\
  &rs6000_reg_names[77][0],	/* v0  */				\
  &rs6000_reg_names[78][0],	/* v1  */				\
  &rs6000_reg_names[79][0],	/* v2  */				\
  &rs6000_reg_names[80][0],	/* v3  */				\
  &rs6000_reg_names[81][0],	/* v4  */				\
  &rs6000_reg_names[82][0],	/* v5  */				\
  &rs6000_reg_names[83][0],	/* v6  */				\
  &rs6000_reg_names[84][0],	/* v7  */				\
  &rs6000_reg_names[85][0],	/* v8  */				\
  &rs6000_reg_names[86][0],	/* v9  */				\
  &rs6000_reg_names[87][0],	/* v10  */				\
  &rs6000_reg_names[88][0],	/* v11  */				\
  &rs6000_reg_names[89][0],	/* v12  */				\
  &rs6000_reg_names[90][0],	/* v13  */				\
  &rs6000_reg_names[91][0],	/* v14  */				\
  &rs6000_reg_names[92][0],	/* v15  */				\
  &rs6000_reg_names[93][0],	/* v16  */				\
  &rs6000_reg_names[94][0],	/* v17  */				\
  &rs6000_reg_names[95][0],	/* v18  */				\
  &rs6000_reg_names[96][0],	/* v19  */				\
  &rs6000_reg_names[97][0],	/* v20  */				\
  &rs6000_reg_names[98][0],	/* v21  */				\
  &rs6000_reg_names[99][0],	/* v22  */				\
  &rs6000_reg_names[100][0],	/* v23  */				\
  &rs6000_reg_names[101][0],	/* v24  */				\
  &rs6000_reg_names[102][0],	/* v25  */				\
  &rs6000_reg_names[103][0],	/* v26  */				\
  &rs6000_reg_names[104][0],	/* v27  */				\
  &rs6000_reg_names[105][0],	/* v28  */				\
  &rs6000_reg_names[106][0],	/* v29  */				\
  &rs6000_reg_names[107][0],	/* v30  */				\
  &rs6000_reg_names[108][0],	/* v31  */				\
  &rs6000_reg_names[109][0],	/* vrsave  */				\
  &rs6000_reg_names[110][0],	/* vscr  */				\
  &rs6000_reg_names[111][0],	/* spe_acc */				\
  &rs6000_reg_names[112][0],	/* spefscr */				\
  &rs6000_reg_names[113][0],	/* sfp  */				\
}

/* Table of additional register names to use in user input.  */

#define ADDITIONAL_REGISTER_NAMES \
 {{"r0",    0}, {"r1",    1}, {"r2",    2}, {"r3",    3},	\
  {"r4",    4}, {"r5",    5}, {"r6",    6}, {"r7",    7},	\
  {"r8",    8}, {"r9",    9}, {"r10",  10}, {"r11",  11},	\
  {"r12",  12}, {"r13",  13}, {"r14",  14}, {"r15",  15},	\
  {"r16",  16}, {"r17",  17}, {"r18",  18}, {"r19",  19},	\
  {"r20",  20}, {"r21",  21}, {"r22",  22}, {"r23",  23},	\
  {"r24",  24}, {"r25",  25}, {"r26",  26}, {"r27",  27},	\
  {"r28",  28}, {"r29",  29}, {"r30",  30}, {"r31",  31},	\
  {"fr0",  32}, {"fr1",  33}, {"fr2",  34}, {"fr3",  35},	\
  {"fr4",  36}, {"fr5",  37}, {"fr6",  38}, {"fr7",  39},	\
  {"fr8",  40}, {"fr9",  41}, {"fr10", 42}, {"fr11", 43},	\
  {"fr12", 44}, {"fr13", 45}, {"fr14", 46}, {"fr15", 47},	\
  {"fr16", 48}, {"fr17", 49}, {"fr18", 50}, {"fr19", 51},	\
  {"fr20", 52}, {"fr21", 53}, {"fr22", 54}, {"fr23", 55},	\
  {"fr24", 56}, {"fr25", 57}, {"fr26", 58}, {"fr27", 59},	\
  {"fr28", 60}, {"fr29", 61}, {"fr30", 62}, {"fr31", 63},	\
  {"v0",   77}, {"v1",   78}, {"v2",   79}, {"v3",   80},       \
  {"v4",   81}, {"v5",   82}, {"v6",   83}, {"v7",   84},       \
  {"v8",   85}, {"v9",   86}, {"v10",  87}, {"v11",  88},       \
  {"v12",  89}, {"v13",  90}, {"v14",  91}, {"v15",  92},       \
  {"v16",  93}, {"v17",  94}, {"v18",  95}, {"v19",  96},       \
  {"v20",  97}, {"v21",  98}, {"v22",  99}, {"v23",  100},	\
  {"v24",  101},{"v25",  102},{"v26",  103},{"v27",  104},      \
  {"v28",  105},{"v29",  106},{"v30",  107},{"v31",  108},      \
  {"vrsave", 109}, {"vscr", 110},				\
  {"spe_acc", 111}, {"spefscr", 112},				\
  /* no additional names for: mq, lr, ctr, ap */		\
  {"cr0",  68}, {"cr1",  69}, {"cr2",  70}, {"cr3",  71},	\
  {"cr4",  72}, {"cr5",  73}, {"cr6",  74}, {"cr7",  75},	\
  {"cc",   68}, {"sp",    1}, {"toc",   2} }

/* Text to write out after a CALL that may be replaced by glue code by
   the loader.  This depends on the AIX version.  */
#define RS6000_CALL_GLUE "cror 31,31,31"

/* This is how to output an element of a case-vector that is relative.  */

#define ASM_OUTPUT_ADDR_DIFF_ELT(FILE, BODY, VALUE, REL) \
  do { char buf[100];					\
       fputs ("\t.long ", FILE);			\
       ASM_GENERATE_INTERNAL_LABEL (buf, "L", VALUE);	\
       assemble_name (FILE, buf);			\
       putc ('-', FILE);				\
       ASM_GENERATE_INTERNAL_LABEL (buf, "L", REL);	\
       assemble_name (FILE, buf);			\
       putc ('\n', FILE);				\
     } while (0)

/* This is how to output an assembler line
   that says to advance the location counter
   to a multiple of 2**LOG bytes.  */

#define ASM_OUTPUT_ALIGN(FILE,LOG)	\
  if ((LOG) != 0)			\
    fprintf (FILE, "\t.align %d\n", (LOG))

/* Pick up the return address upon entry to a procedure. Used for
   dwarf2 unwind information.  This also enables the table driven
   mechanism.  */

#define INCOMING_RETURN_ADDR_RTX   gen_rtx_REG (Pmode, LINK_REGISTER_REGNUM)
#define DWARF_FRAME_RETURN_COLUMN  DWARF_FRAME_REGNUM (LINK_REGISTER_REGNUM)

/* Describe how we implement __builtin_eh_return.  */
#define EH_RETURN_DATA_REGNO(N) ((N) < 4 ? (N) + 3 : INVALID_REGNUM)
#define EH_RETURN_STACKADJ_RTX  gen_rtx_REG (Pmode, 10)

/* Print operand X (an rtx) in assembler syntax to file FILE.
   CODE is a letter or dot (`z' in `%z0') or 0 if no letter was specified.
   For `%' followed by punctuation, CODE is the punctuation and X is null.  */

#define PRINT_OPERAND(FILE, X, CODE)  print_operand (FILE, X, CODE)

/* Define which CODE values are valid.  */

#define PRINT_OPERAND_PUNCT_VALID_P(CODE)  \
  ((CODE) == '.' || (CODE) == '&')

/* Print a memory address as an operand to reference that memory location.  */

#define PRINT_OPERAND_ADDRESS(FILE, ADDR) print_operand_address (FILE, ADDR)

/* uncomment for disabling the corresponding default options */
/* #define  MACHINE_no_sched_interblock */
/* #define  MACHINE_no_sched_speculative */
/* #define  MACHINE_no_sched_speculative_load */

/* General flags.  */
extern int flag_pic;
extern int optimize;
extern int flag_expensive_optimizations;
extern int frame_pointer_needed;

enum rs6000_builtins
{
  /* AltiVec builtins.  */
  ALTIVEC_BUILTIN_ST_INTERNAL_4si,
  ALTIVEC_BUILTIN_LD_INTERNAL_4si,
  ALTIVEC_BUILTIN_ST_INTERNAL_8hi,
  ALTIVEC_BUILTIN_LD_INTERNAL_8hi,
  ALTIVEC_BUILTIN_ST_INTERNAL_16qi,
  ALTIVEC_BUILTIN_LD_INTERNAL_16qi,
  ALTIVEC_BUILTIN_ST_INTERNAL_4sf,
  ALTIVEC_BUILTIN_LD_INTERNAL_4sf,
  ALTIVEC_BUILTIN_VADDUBM,
  ALTIVEC_BUILTIN_VADDUHM,
  ALTIVEC_BUILTIN_VADDUWM,
  ALTIVEC_BUILTIN_VADDFP,
  ALTIVEC_BUILTIN_VADDCUW,
  ALTIVEC_BUILTIN_VADDUBS,
  ALTIVEC_BUILTIN_VADDSBS,
  ALTIVEC_BUILTIN_VADDUHS,
  ALTIVEC_BUILTIN_VADDSHS,
  ALTIVEC_BUILTIN_VADDUWS,
  ALTIVEC_BUILTIN_VADDSWS,
  ALTIVEC_BUILTIN_VAND,
  ALTIVEC_BUILTIN_VANDC,
  ALTIVEC_BUILTIN_VAVGUB,
  ALTIVEC_BUILTIN_VAVGSB,
  ALTIVEC_BUILTIN_VAVGUH,
  ALTIVEC_BUILTIN_VAVGSH,
  ALTIVEC_BUILTIN_VAVGUW,
  ALTIVEC_BUILTIN_VAVGSW,
  ALTIVEC_BUILTIN_VCFUX,
  ALTIVEC_BUILTIN_VCFSX,
  ALTIVEC_BUILTIN_VCTSXS,
  ALTIVEC_BUILTIN_VCTUXS,
  ALTIVEC_BUILTIN_VCMPBFP,
  ALTIVEC_BUILTIN_VCMPEQUB,
  ALTIVEC_BUILTIN_VCMPEQUH,
  ALTIVEC_BUILTIN_VCMPEQUW,
  ALTIVEC_BUILTIN_VCMPEQFP,
  ALTIVEC_BUILTIN_VCMPGEFP,
  ALTIVEC_BUILTIN_VCMPGTUB,
  ALTIVEC_BUILTIN_VCMPGTSB,
  ALTIVEC_BUILTIN_VCMPGTUH,
  ALTIVEC_BUILTIN_VCMPGTSH,
  ALTIVEC_BUILTIN_VCMPGTUW,
  ALTIVEC_BUILTIN_VCMPGTSW,
  ALTIVEC_BUILTIN_VCMPGTFP,
  ALTIVEC_BUILTIN_VEXPTEFP,
  ALTIVEC_BUILTIN_VLOGEFP,
  ALTIVEC_BUILTIN_VMADDFP,
  ALTIVEC_BUILTIN_VMAXUB,
  ALTIVEC_BUILTIN_VMAXSB,
  ALTIVEC_BUILTIN_VMAXUH,
  ALTIVEC_BUILTIN_VMAXSH,
  ALTIVEC_BUILTIN_VMAXUW,
  ALTIVEC_BUILTIN_VMAXSW,
  ALTIVEC_BUILTIN_VMAXFP,
  ALTIVEC_BUILTIN_VMHADDSHS,
  ALTIVEC_BUILTIN_VMHRADDSHS,
  ALTIVEC_BUILTIN_VMLADDUHM,
  ALTIVEC_BUILTIN_VMRGHB,
  ALTIVEC_BUILTIN_VMRGHH,
  ALTIVEC_BUILTIN_VMRGHW,
  ALTIVEC_BUILTIN_VMRGLB,
  ALTIVEC_BUILTIN_VMRGLH,
  ALTIVEC_BUILTIN_VMRGLW,
  ALTIVEC_BUILTIN_VMSUMUBM,
  ALTIVEC_BUILTIN_VMSUMMBM,
  ALTIVEC_BUILTIN_VMSUMUHM,
  ALTIVEC_BUILTIN_VMSUMSHM,
  ALTIVEC_BUILTIN_VMSUMUHS,
  ALTIVEC_BUILTIN_VMSUMSHS,
  ALTIVEC_BUILTIN_VMINUB,
  ALTIVEC_BUILTIN_VMINSB,
  ALTIVEC_BUILTIN_VMINUH,
  ALTIVEC_BUILTIN_VMINSH,
  ALTIVEC_BUILTIN_VMINUW,
  ALTIVEC_BUILTIN_VMINSW,
  ALTIVEC_BUILTIN_VMINFP,
  ALTIVEC_BUILTIN_VMULEUB,
  ALTIVEC_BUILTIN_VMULESB,
  ALTIVEC_BUILTIN_VMULEUH,
  ALTIVEC_BUILTIN_VMULESH,
  ALTIVEC_BUILTIN_VMULOUB,
  ALTIVEC_BUILTIN_VMULOSB,
  ALTIVEC_BUILTIN_VMULOUH,
  ALTIVEC_BUILTIN_VMULOSH,
  ALTIVEC_BUILTIN_VNMSUBFP,
  ALTIVEC_BUILTIN_VNOR,
  ALTIVEC_BUILTIN_VOR,
  ALTIVEC_BUILTIN_VSEL_4SI,
  ALTIVEC_BUILTIN_VSEL_4SF,
  ALTIVEC_BUILTIN_VSEL_8HI,
  ALTIVEC_BUILTIN_VSEL_16QI,
  ALTIVEC_BUILTIN_VPERM_4SI,
  ALTIVEC_BUILTIN_VPERM_4SF,
  ALTIVEC_BUILTIN_VPERM_8HI,
  ALTIVEC_BUILTIN_VPERM_16QI,
  ALTIVEC_BUILTIN_VPKUHUM,
  ALTIVEC_BUILTIN_VPKUWUM,
  ALTIVEC_BUILTIN_VPKPX,
  ALTIVEC_BUILTIN_VPKUHSS,
  ALTIVEC_BUILTIN_VPKSHSS,
  ALTIVEC_BUILTIN_VPKUWSS,
  ALTIVEC_BUILTIN_VPKSWSS,
  ALTIVEC_BUILTIN_VPKUHUS,
  ALTIVEC_BUILTIN_VPKSHUS,
  ALTIVEC_BUILTIN_VPKUWUS,
  ALTIVEC_BUILTIN_VPKSWUS,
  ALTIVEC_BUILTIN_VREFP,
  ALTIVEC_BUILTIN_VRFIM,
  ALTIVEC_BUILTIN_VRFIN,
  ALTIVEC_BUILTIN_VRFIP,
  ALTIVEC_BUILTIN_VRFIZ,
  ALTIVEC_BUILTIN_VRLB,
  ALTIVEC_BUILTIN_VRLH,
  ALTIVEC_BUILTIN_VRLW,
  ALTIVEC_BUILTIN_VRSQRTEFP,
  ALTIVEC_BUILTIN_VSLB,
  ALTIVEC_BUILTIN_VSLH,
  ALTIVEC_BUILTIN_VSLW,
  ALTIVEC_BUILTIN_VSL,
  ALTIVEC_BUILTIN_VSLO,
  ALTIVEC_BUILTIN_VSPLTB,
  ALTIVEC_BUILTIN_VSPLTH,
  ALTIVEC_BUILTIN_VSPLTW,
  ALTIVEC_BUILTIN_VSPLTISB,
  ALTIVEC_BUILTIN_VSPLTISH,
  ALTIVEC_BUILTIN_VSPLTISW,
  ALTIVEC_BUILTIN_VSRB,
  ALTIVEC_BUILTIN_VSRH,
  ALTIVEC_BUILTIN_VSRW,
  ALTIVEC_BUILTIN_VSRAB,
  ALTIVEC_BUILTIN_VSRAH,
  ALTIVEC_BUILTIN_VSRAW,
  ALTIVEC_BUILTIN_VSR,
  ALTIVEC_BUILTIN_VSRO,
  ALTIVEC_BUILTIN_VSUBUBM,
  ALTIVEC_BUILTIN_VSUBUHM,
  ALTIVEC_BUILTIN_VSUBUWM,
  ALTIVEC_BUILTIN_VSUBFP,
  ALTIVEC_BUILTIN_VSUBCUW,
  ALTIVEC_BUILTIN_VSUBUBS,
  ALTIVEC_BUILTIN_VSUBSBS,
  ALTIVEC_BUILTIN_VSUBUHS,
  ALTIVEC_BUILTIN_VSUBSHS,
  ALTIVEC_BUILTIN_VSUBUWS,
  ALTIVEC_BUILTIN_VSUBSWS,
  ALTIVEC_BUILTIN_VSUM4UBS,
  ALTIVEC_BUILTIN_VSUM4SBS,
  ALTIVEC_BUILTIN_VSUM4SHS,
  ALTIVEC_BUILTIN_VSUM2SWS,
  ALTIVEC_BUILTIN_VSUMSWS,
  ALTIVEC_BUILTIN_VXOR,
  ALTIVEC_BUILTIN_VSLDOI_16QI,
  ALTIVEC_BUILTIN_VSLDOI_8HI,
  ALTIVEC_BUILTIN_VSLDOI_4SI,
  ALTIVEC_BUILTIN_VSLDOI_4SF,
  ALTIVEC_BUILTIN_VUPKHSB,
  ALTIVEC_BUILTIN_VUPKHPX,
  ALTIVEC_BUILTIN_VUPKHSH,
  ALTIVEC_BUILTIN_VUPKLSB,
  ALTIVEC_BUILTIN_VUPKLPX,
  ALTIVEC_BUILTIN_VUPKLSH,
  ALTIVEC_BUILTIN_MTVSCR,
  ALTIVEC_BUILTIN_MFVSCR,
  ALTIVEC_BUILTIN_DSSALL,
  ALTIVEC_BUILTIN_DSS,
  ALTIVEC_BUILTIN_LVSL,
  ALTIVEC_BUILTIN_LVSR,
  ALTIVEC_BUILTIN_DSTT,
  ALTIVEC_BUILTIN_DSTST,
  ALTIVEC_BUILTIN_DSTSTT,
  ALTIVEC_BUILTIN_DST,
  ALTIVEC_BUILTIN_LVEBX,
  ALTIVEC_BUILTIN_LVEHX,
  ALTIVEC_BUILTIN_LVEWX,
  ALTIVEC_BUILTIN_LVXL,
  ALTIVEC_BUILTIN_LVX,
  ALTIVEC_BUILTIN_STVX,
  ALTIVEC_BUILTIN_STVEBX,
  ALTIVEC_BUILTIN_STVEHX,
  ALTIVEC_BUILTIN_STVEWX,
  ALTIVEC_BUILTIN_STVXL,
  ALTIVEC_BUILTIN_VCMPBFP_P,
  ALTIVEC_BUILTIN_VCMPEQFP_P,
  ALTIVEC_BUILTIN_VCMPEQUB_P,
  ALTIVEC_BUILTIN_VCMPEQUH_P,
  ALTIVEC_BUILTIN_VCMPEQUW_P,
  ALTIVEC_BUILTIN_VCMPGEFP_P,
  ALTIVEC_BUILTIN_VCMPGTFP_P,
  ALTIVEC_BUILTIN_VCMPGTSB_P,
  ALTIVEC_BUILTIN_VCMPGTSH_P,
  ALTIVEC_BUILTIN_VCMPGTSW_P,
  ALTIVEC_BUILTIN_VCMPGTUB_P,
  ALTIVEC_BUILTIN_VCMPGTUH_P,
  ALTIVEC_BUILTIN_VCMPGTUW_P,
  ALTIVEC_BUILTIN_ABSS_V4SI,
  ALTIVEC_BUILTIN_ABSS_V8HI,
  ALTIVEC_BUILTIN_ABSS_V16QI,
  ALTIVEC_BUILTIN_ABS_V4SI,
  ALTIVEC_BUILTIN_ABS_V4SF,
  ALTIVEC_BUILTIN_ABS_V8HI,
  ALTIVEC_BUILTIN_ABS_V16QI,
  ALTIVEC_BUILTIN_MASK_FOR_LOAD,
  ALTIVEC_BUILTIN_MASK_FOR_STORE,
  ALTIVEC_BUILTIN_VEC_INIT_V4SI,
  ALTIVEC_BUILTIN_VEC_INIT_V8HI,
  ALTIVEC_BUILTIN_VEC_INIT_V16QI,
  ALTIVEC_BUILTIN_VEC_INIT_V4SF,
  ALTIVEC_BUILTIN_VEC_SET_V4SI,
  ALTIVEC_BUILTIN_VEC_SET_V8HI,
  ALTIVEC_BUILTIN_VEC_SET_V16QI,
  ALTIVEC_BUILTIN_VEC_SET_V4SF,
  ALTIVEC_BUILTIN_VEC_EXT_V4SI,
  ALTIVEC_BUILTIN_VEC_EXT_V8HI,
  ALTIVEC_BUILTIN_VEC_EXT_V16QI,
  ALTIVEC_BUILTIN_VEC_EXT_V4SF,

  /* Altivec overloaded builtins.  */
  ALTIVEC_BUILTIN_VCMPEQ_P,
  ALTIVEC_BUILTIN_OVERLOADED_FIRST = ALTIVEC_BUILTIN_VCMPEQ_P,
  ALTIVEC_BUILTIN_VCMPGT_P,
  ALTIVEC_BUILTIN_VCMPGE_P,
  ALTIVEC_BUILTIN_VEC_ABS,
  ALTIVEC_BUILTIN_VEC_ABSS,
  ALTIVEC_BUILTIN_VEC_ADD,
  ALTIVEC_BUILTIN_VEC_ADDC,
  ALTIVEC_BUILTIN_VEC_ADDS,
  ALTIVEC_BUILTIN_VEC_AND,
  ALTIVEC_BUILTIN_VEC_ANDC,
  ALTIVEC_BUILTIN_VEC_AVG,
  ALTIVEC_BUILTIN_VEC_CEIL,
  ALTIVEC_BUILTIN_VEC_CMPB,
  ALTIVEC_BUILTIN_VEC_CMPEQ,
  ALTIVEC_BUILTIN_VEC_CMPEQUB,
  ALTIVEC_BUILTIN_VEC_CMPEQUH,
  ALTIVEC_BUILTIN_VEC_CMPEQUW,
  ALTIVEC_BUILTIN_VEC_CMPGE,
  ALTIVEC_BUILTIN_VEC_CMPGT,
  ALTIVEC_BUILTIN_VEC_CMPLE,
  ALTIVEC_BUILTIN_VEC_CMPLT,
  ALTIVEC_BUILTIN_VEC_CTF,
  ALTIVEC_BUILTIN_VEC_CTS,
  ALTIVEC_BUILTIN_VEC_CTU,
  ALTIVEC_BUILTIN_VEC_DST,
  ALTIVEC_BUILTIN_VEC_DSTST,
  ALTIVEC_BUILTIN_VEC_DSTSTT,
  ALTIVEC_BUILTIN_VEC_DSTT,
  ALTIVEC_BUILTIN_VEC_EXPTE,
  ALTIVEC_BUILTIN_VEC_FLOOR,
  ALTIVEC_BUILTIN_VEC_LD,
  ALTIVEC_BUILTIN_VEC_LDE,
  ALTIVEC_BUILTIN_VEC_LDL,
  ALTIVEC_BUILTIN_VEC_LOGE,
  ALTIVEC_BUILTIN_VEC_LVEBX,
  ALTIVEC_BUILTIN_VEC_LVEHX,
  ALTIVEC_BUILTIN_VEC_LVEWX,
  ALTIVEC_BUILTIN_VEC_LVSL,
  ALTIVEC_BUILTIN_VEC_LVSR,
  ALTIVEC_BUILTIN_VEC_MADD,
  ALTIVEC_BUILTIN_VEC_MADDS,
  ALTIVEC_BUILTIN_VEC_MAX,
  ALTIVEC_BUILTIN_VEC_MERGEH,
  ALTIVEC_BUILTIN_VEC_MERGEL,
  ALTIVEC_BUILTIN_VEC_MIN,
  ALTIVEC_BUILTIN_VEC_MLADD,
  ALTIVEC_BUILTIN_VEC_MPERM,
  ALTIVEC_BUILTIN_VEC_MRADDS,
  ALTIVEC_BUILTIN_VEC_MRGHB,
  ALTIVEC_BUILTIN_VEC_MRGHH,
  ALTIVEC_BUILTIN_VEC_MRGHW,
  ALTIVEC_BUILTIN_VEC_MRGLB,
  ALTIVEC_BUILTIN_VEC_MRGLH,
  ALTIVEC_BUILTIN_VEC_MRGLW,
  ALTIVEC_BUILTIN_VEC_MSUM,
  ALTIVEC_BUILTIN_VEC_MSUMS,
  ALTIVEC_BUILTIN_VEC_MTVSCR,
  ALTIVEC_BUILTIN_VEC_MULE,
  ALTIVEC_BUILTIN_VEC_MULO,
  ALTIVEC_BUILTIN_VEC_NMSUB,
  ALTIVEC_BUILTIN_VEC_NOR,
  ALTIVEC_BUILTIN_VEC_OR,
  ALTIVEC_BUILTIN_VEC_PACK,
  ALTIVEC_BUILTIN_VEC_PACKPX,
  ALTIVEC_BUILTIN_VEC_PACKS,
  ALTIVEC_BUILTIN_VEC_PACKSU,
  ALTIVEC_BUILTIN_VEC_PERM,
  ALTIVEC_BUILTIN_VEC_RE,
  ALTIVEC_BUILTIN_VEC_RL,
  ALTIVEC_BUILTIN_VEC_ROUND,
  ALTIVEC_BUILTIN_VEC_RSQRTE,
  ALTIVEC_BUILTIN_VEC_SEL,
  ALTIVEC_BUILTIN_VEC_SL,
  ALTIVEC_BUILTIN_VEC_SLD,
  ALTIVEC_BUILTIN_VEC_SLL,
  ALTIVEC_BUILTIN_VEC_SLO,
  ALTIVEC_BUILTIN_VEC_SPLAT,
  ALTIVEC_BUILTIN_VEC_SPLAT_S16,
  ALTIVEC_BUILTIN_VEC_SPLAT_S32,
  ALTIVEC_BUILTIN_VEC_SPLAT_S8,
  ALTIVEC_BUILTIN_VEC_SPLAT_U16,
  ALTIVEC_BUILTIN_VEC_SPLAT_U32,
  ALTIVEC_BUILTIN_VEC_SPLAT_U8,
  ALTIVEC_BUILTIN_VEC_SPLTB,
  ALTIVEC_BUILTIN_VEC_SPLTH,
  ALTIVEC_BUILTIN_VEC_SPLTW,
  ALTIVEC_BUILTIN_VEC_SR,
  ALTIVEC_BUILTIN_VEC_SRA,
  ALTIVEC_BUILTIN_VEC_SRL,
  ALTIVEC_BUILTIN_VEC_SRO,
  ALTIVEC_BUILTIN_VEC_ST,
  ALTIVEC_BUILTIN_VEC_STE,
  ALTIVEC_BUILTIN_VEC_STL,
  ALTIVEC_BUILTIN_VEC_STVEBX,
  ALTIVEC_BUILTIN_VEC_STVEHX,
  ALTIVEC_BUILTIN_VEC_STVEWX,
  ALTIVEC_BUILTIN_VEC_SUB,
  ALTIVEC_BUILTIN_VEC_SUBC,
  ALTIVEC_BUILTIN_VEC_SUBS,
  ALTIVEC_BUILTIN_VEC_SUM2S,
  ALTIVEC_BUILTIN_VEC_SUM4S,
  ALTIVEC_BUILTIN_VEC_SUMS,
  ALTIVEC_BUILTIN_VEC_TRUNC,
  ALTIVEC_BUILTIN_VEC_UNPACKH,
  ALTIVEC_BUILTIN_VEC_UNPACKL,
  ALTIVEC_BUILTIN_VEC_VADDFP,
  ALTIVEC_BUILTIN_VEC_VADDSBS,
  ALTIVEC_BUILTIN_VEC_VADDSHS,
  ALTIVEC_BUILTIN_VEC_VADDSWS,
  ALTIVEC_BUILTIN_VEC_VADDUBM,
  ALTIVEC_BUILTIN_VEC_VADDUBS,
  ALTIVEC_BUILTIN_VEC_VADDUHM,
  ALTIVEC_BUILTIN_VEC_VADDUHS,
  ALTIVEC_BUILTIN_VEC_VADDUWM,
  ALTIVEC_BUILTIN_VEC_VADDUWS,
  ALTIVEC_BUILTIN_VEC_VAVGSB,
  ALTIVEC_BUILTIN_VEC_VAVGSH,
  ALTIVEC_BUILTIN_VEC_VAVGSW,
  ALTIVEC_BUILTIN_VEC_VAVGUB,
  ALTIVEC_BUILTIN_VEC_VAVGUH,
  ALTIVEC_BUILTIN_VEC_VAVGUW,
  ALTIVEC_BUILTIN_VEC_VCFSX,
  ALTIVEC_BUILTIN_VEC_VCFUX,
  ALTIVEC_BUILTIN_VEC_VCMPEQFP,
  ALTIVEC_BUILTIN_VEC_VCMPEQUB,
  ALTIVEC_BUILTIN_VEC_VCMPEQUH,
  ALTIVEC_BUILTIN_VEC_VCMPEQUW,
  ALTIVEC_BUILTIN_VEC_VCMPGTFP,
  ALTIVEC_BUILTIN_VEC_VCMPGTSB,
  ALTIVEC_BUILTIN_VEC_VCMPGTSH,
  ALTIVEC_BUILTIN_VEC_VCMPGTSW,
  ALTIVEC_BUILTIN_VEC_VCMPGTUB,
  ALTIVEC_BUILTIN_VEC_VCMPGTUH,
  ALTIVEC_BUILTIN_VEC_VCMPGTUW,
  ALTIVEC_BUILTIN_VEC_VMAXFP,
  ALTIVEC_BUILTIN_VEC_VMAXSB,
  ALTIVEC_BUILTIN_VEC_VMAXSH,
  ALTIVEC_BUILTIN_VEC_VMAXSW,
  ALTIVEC_BUILTIN_VEC_VMAXUB,
  ALTIVEC_BUILTIN_VEC_VMAXUH,
  ALTIVEC_BUILTIN_VEC_VMAXUW,
  ALTIVEC_BUILTIN_VEC_VMINFP,
  ALTIVEC_BUILTIN_VEC_VMINSB,
  ALTIVEC_BUILTIN_VEC_VMINSH,
  ALTIVEC_BUILTIN_VEC_VMINSW,
  ALTIVEC_BUILTIN_VEC_VMINUB,
  ALTIVEC_BUILTIN_VEC_VMINUH,
  ALTIVEC_BUILTIN_VEC_VMINUW,
  ALTIVEC_BUILTIN_VEC_VMRGHB,
  ALTIVEC_BUILTIN_VEC_VMRGHH,
  ALTIVEC_BUILTIN_VEC_VMRGHW,
  ALTIVEC_BUILTIN_VEC_VMRGLB,
  ALTIVEC_BUILTIN_VEC_VMRGLH,
  ALTIVEC_BUILTIN_VEC_VMRGLW,
  ALTIVEC_BUILTIN_VEC_VMSUMMBM,
  ALTIVEC_BUILTIN_VEC_VMSUMSHM,
  ALTIVEC_BUILTIN_VEC_VMSUMSHS,
  ALTIVEC_BUILTIN_VEC_VMSUMUBM,
  ALTIVEC_BUILTIN_VEC_VMSUMUHM,
  ALTIVEC_BUILTIN_VEC_VMSUMUHS,
  ALTIVEC_BUILTIN_VEC_VMULESB,
  ALTIVEC_BUILTIN_VEC_VMULESH,
  ALTIVEC_BUILTIN_VEC_VMULEUB,
  ALTIVEC_BUILTIN_VEC_VMULEUH,
  ALTIVEC_BUILTIN_VEC_VMULOSB,
  ALTIVEC_BUILTIN_VEC_VMULOSH,
  ALTIVEC_BUILTIN_VEC_VMULOUB,
  ALTIVEC_BUILTIN_VEC_VMULOUH,
  ALTIVEC_BUILTIN_VEC_VPKSHSS,
  ALTIVEC_BUILTIN_VEC_VPKSHUS,
  ALTIVEC_BUILTIN_VEC_VPKSWSS,
  ALTIVEC_BUILTIN_VEC_VPKSWUS,
  ALTIVEC_BUILTIN_VEC_VPKUHUM,
  ALTIVEC_BUILTIN_VEC_VPKUHUS,
  ALTIVEC_BUILTIN_VEC_VPKUWUM,
  ALTIVEC_BUILTIN_VEC_VPKUWUS,
  ALTIVEC_BUILTIN_VEC_VRLB,
  ALTIVEC_BUILTIN_VEC_VRLH,
  ALTIVEC_BUILTIN_VEC_VRLW,
  ALTIVEC_BUILTIN_VEC_VSLB,
  ALTIVEC_BUILTIN_VEC_VSLH,
  ALTIVEC_BUILTIN_VEC_VSLW,
  ALTIVEC_BUILTIN_VEC_VSPLTB,
  ALTIVEC_BUILTIN_VEC_VSPLTH,
  ALTIVEC_BUILTIN_VEC_VSPLTW,
  ALTIVEC_BUILTIN_VEC_VSRAB,
  ALTIVEC_BUILTIN_VEC_VSRAH,
  ALTIVEC_BUILTIN_VEC_VSRAW,
  ALTIVEC_BUILTIN_VEC_VSRB,
  ALTIVEC_BUILTIN_VEC_VSRH,
  ALTIVEC_BUILTIN_VEC_VSRW,
  ALTIVEC_BUILTIN_VEC_VSUBFP,
  ALTIVEC_BUILTIN_VEC_VSUBSBS,
  ALTIVEC_BUILTIN_VEC_VSUBSHS,
  ALTIVEC_BUILTIN_VEC_VSUBSWS,
  ALTIVEC_BUILTIN_VEC_VSUBUBM,
  ALTIVEC_BUILTIN_VEC_VSUBUBS,
  ALTIVEC_BUILTIN_VEC_VSUBUHM,
  ALTIVEC_BUILTIN_VEC_VSUBUHS,
  ALTIVEC_BUILTIN_VEC_VSUBUWM,
  ALTIVEC_BUILTIN_VEC_VSUBUWS,
  ALTIVEC_BUILTIN_VEC_VSUM4SBS,
  ALTIVEC_BUILTIN_VEC_VSUM4SHS,
  ALTIVEC_BUILTIN_VEC_VSUM4UBS,
  ALTIVEC_BUILTIN_VEC_VUPKHPX,
  ALTIVEC_BUILTIN_VEC_VUPKHSB,
  ALTIVEC_BUILTIN_VEC_VUPKHSH,
  ALTIVEC_BUILTIN_VEC_VUPKLPX,
  ALTIVEC_BUILTIN_VEC_VUPKLSB,
  ALTIVEC_BUILTIN_VEC_VUPKLSH,
  ALTIVEC_BUILTIN_VEC_XOR,
  ALTIVEC_BUILTIN_VEC_STEP,
  ALTIVEC_BUILTIN_OVERLOADED_LAST = ALTIVEC_BUILTIN_VEC_STEP,

  /* SPE builtins.  */
  SPE_BUILTIN_EVADDW,
  SPE_BUILTIN_EVAND,
  SPE_BUILTIN_EVANDC,
  SPE_BUILTIN_EVDIVWS,
  SPE_BUILTIN_EVDIVWU,
  SPE_BUILTIN_EVEQV,
  SPE_BUILTIN_EVFSADD,
  SPE_BUILTIN_EVFSDIV,
  SPE_BUILTIN_EVFSMUL,
  SPE_BUILTIN_EVFSSUB,
  SPE_BUILTIN_EVLDDX,
  SPE_BUILTIN_EVLDHX,
  SPE_BUILTIN_EVLDWX,
  SPE_BUILTIN_EVLHHESPLATX,
  SPE_BUILTIN_EVLHHOSSPLATX,
  SPE_BUILTIN_EVLHHOUSPLATX,
  SPE_BUILTIN_EVLWHEX,
  SPE_BUILTIN_EVLWHOSX,
  SPE_BUILTIN_EVLWHOUX,
  SPE_BUILTIN_EVLWHSPLATX,
  SPE_BUILTIN_EVLWWSPLATX,
  SPE_BUILTIN_EVMERGEHI,
  SPE_BUILTIN_EVMERGEHILO,
  SPE_BUILTIN_EVMERGELO,
  SPE_BUILTIN_EVMERGELOHI,
  SPE_BUILTIN_EVMHEGSMFAA,
  SPE_BUILTIN_EVMHEGSMFAN,
  SPE_BUILTIN_EVMHEGSMIAA,
  SPE_BUILTIN_EVMHEGSMIAN,
  SPE_BUILTIN_EVMHEGUMIAA,
  SPE_BUILTIN_EVMHEGUMIAN,
  SPE_BUILTIN_EVMHESMF,
  SPE_BUILTIN_EVMHESMFA,
  SPE_BUILTIN_EVMHESMFAAW,
  SPE_BUILTIN_EVMHESMFANW,
  SPE_BUILTIN_EVMHESMI,
  SPE_BUILTIN_EVMHESMIA,
  SPE_BUILTIN_EVMHESMIAAW,
  SPE_BUILTIN_EVMHESMIANW,
  SPE_BUILTIN_EVMHESSF,
  SPE_BUILTIN_EVMHESSFA,
  SPE_BUILTIN_EVMHESSFAAW,
  SPE_BUILTIN_EVMHESSFANW,
  SPE_BUILTIN_EVMHESSIAAW,
  SPE_BUILTIN_EVMHESSIANW,
  SPE_BUILTIN_EVMHEUMI,
  SPE_BUILTIN_EVMHEUMIA,
  SPE_BUILTIN_EVMHEUMIAAW,
  SPE_BUILTIN_EVMHEUMIANW,
  SPE_BUILTIN_EVMHEUSIAAW,
  SPE_BUILTIN_EVMHEUSIANW,
  SPE_BUILTIN_EVMHOGSMFAA,
  SPE_BUILTIN_EVMHOGSMFAN,
  SPE_BUILTIN_EVMHOGSMIAA,
  SPE_BUILTIN_EVMHOGSMIAN,
  SPE_BUILTIN_EVMHOGUMIAA,
  SPE_BUILTIN_EVMHOGUMIAN,
  SPE_BUILTIN_EVMHOSMF,
  SPE_BUILTIN_EVMHOSMFA,
  SPE_BUILTIN_EVMHOSMFAAW,
  SPE_BUILTIN_EVMHOSMFANW,
  SPE_BUILTIN_EVMHOSMI,
  SPE_BUILTIN_EVMHOSMIA,
  SPE_BUILTIN_EVMHOSMIAAW,
  SPE_BUILTIN_EVMHOSMIANW,
  SPE_BUILTIN_EVMHOSSF,
  SPE_BUILTIN_EVMHOSSFA,
  SPE_BUILTIN_EVMHOSSFAAW,
  SPE_BUILTIN_EVMHOSSFANW,
  SPE_BUILTIN_EVMHOSSIAAW,
  SPE_BUILTIN_EVMHOSSIANW,
  SPE_BUILTIN_EVMHOUMI,
  SPE_BUILTIN_EVMHOUMIA,
  SPE_BUILTIN_EVMHOUMIAAW,
  SPE_BUILTIN_EVMHOUMIANW,
  SPE_BUILTIN_EVMHOUSIAAW,
  SPE_BUILTIN_EVMHOUSIANW,
  SPE_BUILTIN_EVMWHSMF,
  SPE_BUILTIN_EVMWHSMFA,
  SPE_BUILTIN_EVMWHSMI,
  SPE_BUILTIN_EVMWHSMIA,
  SPE_BUILTIN_EVMWHSSF,
  SPE_BUILTIN_EVMWHSSFA,
  SPE_BUILTIN_EVMWHUMI,
  SPE_BUILTIN_EVMWHUMIA,
  SPE_BUILTIN_EVMWLSMIAAW,
  SPE_BUILTIN_EVMWLSMIANW,
  SPE_BUILTIN_EVMWLSSIAAW,
  SPE_BUILTIN_EVMWLSSIANW,
  SPE_BUILTIN_EVMWLUMI,
  SPE_BUILTIN_EVMWLUMIA,
  SPE_BUILTIN_EVMWLUMIAAW,
  SPE_BUILTIN_EVMWLUMIANW,
  SPE_BUILTIN_EVMWLUSIAAW,
  SPE_BUILTIN_EVMWLUSIANW,
  SPE_BUILTIN_EVMWSMF,
  SPE_BUILTIN_EVMWSMFA,
  SPE_BUILTIN_EVMWSMFAA,
  SPE_BUILTIN_EVMWSMFAN,
  SPE_BUILTIN_EVMWSMI,
  SPE_BUILTIN_EVMWSMIA,
  SPE_BUILTIN_EVMWSMIAA,
  SPE_BUILTIN_EVMWSMIAN,
  SPE_BUILTIN_EVMWHSSFAA,
  SPE_BUILTIN_EVMWSSF,
  SPE_BUILTIN_EVMWSSFA,
  SPE_BUILTIN_EVMWSSFAA,
  SPE_BUILTIN_EVMWSSFAN,
  SPE_BUILTIN_EVMWUMI,
  SPE_BUILTIN_EVMWUMIA,
  SPE_BUILTIN_EVMWUMIAA,
  SPE_BUILTIN_EVMWUMIAN,
  SPE_BUILTIN_EVNAND,
  SPE_BUILTIN_EVNOR,
  SPE_BUILTIN_EVOR,
  SPE_BUILTIN_EVORC,
  SPE_BUILTIN_EVRLW,
  SPE_BUILTIN_EVSLW,
  SPE_BUILTIN_EVSRWS,
  SPE_BUILTIN_EVSRWU,
  SPE_BUILTIN_EVSTDDX,
  SPE_BUILTIN_EVSTDHX,
  SPE_BUILTIN_EVSTDWX,
  SPE_BUILTIN_EVSTWHEX,
  SPE_BUILTIN_EVSTWHOX,
  SPE_BUILTIN_EVSTWWEX,
  SPE_BUILTIN_EVSTWWOX,
  SPE_BUILTIN_EVSUBFW,
  SPE_BUILTIN_EVXOR,
  SPE_BUILTIN_EVABS,
  SPE_BUILTIN_EVADDSMIAAW,
  SPE_BUILTIN_EVADDSSIAAW,
  SPE_BUILTIN_EVADDUMIAAW,
  SPE_BUILTIN_EVADDUSIAAW,
  SPE_BUILTIN_EVCNTLSW,
  SPE_BUILTIN_EVCNTLZW,
  SPE_BUILTIN_EVEXTSB,
  SPE_BUILTIN_EVEXTSH,
  SPE_BUILTIN_EVFSABS,
  SPE_BUILTIN_EVFSCFSF,
  SPE_BUILTIN_EVFSCFSI,
  SPE_BUILTIN_EVFSCFUF,
  SPE_BUILTIN_EVFSCFUI,
  SPE_BUILTIN_EVFSCTSF,
  SPE_BUILTIN_EVFSCTSI,
  SPE_BUILTIN_EVFSCTSIZ,
  SPE_BUILTIN_EVFSCTUF,
  SPE_BUILTIN_EVFSCTUI,
  SPE_BUILTIN_EVFSCTUIZ,
  SPE_BUILTIN_EVFSNABS,
  SPE_BUILTIN_EVFSNEG,
  SPE_BUILTIN_EVMRA,
  SPE_BUILTIN_EVNEG,
  SPE_BUILTIN_EVRNDW,
  SPE_BUILTIN_EVSUBFSMIAAW,
  SPE_BUILTIN_EVSUBFSSIAAW,
  SPE_BUILTIN_EVSUBFUMIAAW,
  SPE_BUILTIN_EVSUBFUSIAAW,
  SPE_BUILTIN_EVADDIW,
  SPE_BUILTIN_EVLDD,
  SPE_BUILTIN_EVLDH,
  SPE_BUILTIN_EVLDW,
  SPE_BUILTIN_EVLHHESPLAT,
  SPE_BUILTIN_EVLHHOSSPLAT,
  SPE_BUILTIN_EVLHHOUSPLAT,
  SPE_BUILTIN_EVLWHE,
  SPE_BUILTIN_EVLWHOS,
  SPE_BUILTIN_EVLWHOU,
  SPE_BUILTIN_EVLWHSPLAT,
  SPE_BUILTIN_EVLWWSPLAT,
  SPE_BUILTIN_EVRLWI,
  SPE_BUILTIN_EVSLWI,
  SPE_BUILTIN_EVSRWIS,
  SPE_BUILTIN_EVSRWIU,
  SPE_BUILTIN_EVSTDD,
  SPE_BUILTIN_EVSTDH,
  SPE_BUILTIN_EVSTDW,
  SPE_BUILTIN_EVSTWHE,
  SPE_BUILTIN_EVSTWHO,
  SPE_BUILTIN_EVSTWWE,
  SPE_BUILTIN_EVSTWWO,
  SPE_BUILTIN_EVSUBIFW,

  /* Compares.  */
  SPE_BUILTIN_EVCMPEQ,
  SPE_BUILTIN_EVCMPGTS,
  SPE_BUILTIN_EVCMPGTU,
  SPE_BUILTIN_EVCMPLTS,
  SPE_BUILTIN_EVCMPLTU,
  SPE_BUILTIN_EVFSCMPEQ,
  SPE_BUILTIN_EVFSCMPGT,
  SPE_BUILTIN_EVFSCMPLT,
  SPE_BUILTIN_EVFSTSTEQ,
  SPE_BUILTIN_EVFSTSTGT,
  SPE_BUILTIN_EVFSTSTLT,

  /* EVSEL compares.  */
  SPE_BUILTIN_EVSEL_CMPEQ,
  SPE_BUILTIN_EVSEL_CMPGTS,
  SPE_BUILTIN_EVSEL_CMPGTU,
  SPE_BUILTIN_EVSEL_CMPLTS,
  SPE_BUILTIN_EVSEL_CMPLTU,
  SPE_BUILTIN_EVSEL_FSCMPEQ,
  SPE_BUILTIN_EVSEL_FSCMPGT,
  SPE_BUILTIN_EVSEL_FSCMPLT,
  SPE_BUILTIN_EVSEL_FSTSTEQ,
  SPE_BUILTIN_EVSEL_FSTSTGT,
  SPE_BUILTIN_EVSEL_FSTSTLT,

  SPE_BUILTIN_EVSPLATFI,
  SPE_BUILTIN_EVSPLATI,
  SPE_BUILTIN_EVMWHSSMAA,
  SPE_BUILTIN_EVMWHSMFAA,
  SPE_BUILTIN_EVMWHSMIAA,
  SPE_BUILTIN_EVMWHUSIAA,
  SPE_BUILTIN_EVMWHUMIAA,
  SPE_BUILTIN_EVMWHSSFAN,
  SPE_BUILTIN_EVMWHSSIAN,
  SPE_BUILTIN_EVMWHSMFAN,
  SPE_BUILTIN_EVMWHSMIAN,
  SPE_BUILTIN_EVMWHUSIAN,
  SPE_BUILTIN_EVMWHUMIAN,
  SPE_BUILTIN_EVMWHGSSFAA,
  SPE_BUILTIN_EVMWHGSMFAA,
  SPE_BUILTIN_EVMWHGSMIAA,
  SPE_BUILTIN_EVMWHGUMIAA,
  SPE_BUILTIN_EVMWHGSSFAN,
  SPE_BUILTIN_EVMWHGSMFAN,
  SPE_BUILTIN_EVMWHGSMIAN,
  SPE_BUILTIN_EVMWHGUMIAN,
  SPE_BUILTIN_MTSPEFSCR,
  SPE_BUILTIN_MFSPEFSCR,
  SPE_BUILTIN_BRINC,

  RS6000_BUILTIN_COUNT
};

enum rs6000_builtin_type_index
{
  RS6000_BTI_NOT_OPAQUE,
  RS6000_BTI_opaque_V2SI,
  RS6000_BTI_opaque_V2SF,
  RS6000_BTI_opaque_p_V2SI,
  RS6000_BTI_opaque_V4SI,
  RS6000_BTI_V16QI,
  RS6000_BTI_V2SI,
  RS6000_BTI_V2SF,
  RS6000_BTI_V4HI,
  RS6000_BTI_V4SI,
  RS6000_BTI_V4SF,
  RS6000_BTI_V8HI,
  RS6000_BTI_unsigned_V16QI,
  RS6000_BTI_unsigned_V8HI,
  RS6000_BTI_unsigned_V4SI,
  RS6000_BTI_bool_char,          /* __bool char */
  RS6000_BTI_bool_short,         /* __bool short */
  RS6000_BTI_bool_int,           /* __bool int */
  RS6000_BTI_pixel,              /* __pixel */
  RS6000_BTI_bool_V16QI,         /* __vector __bool char */
  RS6000_BTI_bool_V8HI,          /* __vector __bool short */
  RS6000_BTI_bool_V4SI,          /* __vector __bool int */
  RS6000_BTI_pixel_V8HI,         /* __vector __pixel */
  RS6000_BTI_long,	         /* long_integer_type_node */
  RS6000_BTI_unsigned_long,      /* long_unsigned_type_node */
  RS6000_BTI_INTQI,	         /* intQI_type_node */
  RS6000_BTI_UINTQI,		 /* unsigned_intQI_type_node */
  RS6000_BTI_INTHI,	         /* intHI_type_node */
  RS6000_BTI_UINTHI,		 /* unsigned_intHI_type_node */
  RS6000_BTI_INTSI,		 /* intSI_type_node */
  RS6000_BTI_UINTSI,		 /* unsigned_intSI_type_node */
  RS6000_BTI_float,	         /* float_type_node */
  RS6000_BTI_void,	         /* void_type_node */
  RS6000_BTI_MAX
};


#define opaque_V2SI_type_node         (rs6000_builtin_types[RS6000_BTI_opaque_V2SI])
#define opaque_V2SF_type_node         (rs6000_builtin_types[RS6000_BTI_opaque_V2SF])
#define opaque_p_V2SI_type_node       (rs6000_builtin_types[RS6000_BTI_opaque_p_V2SI])
#define opaque_V4SI_type_node         (rs6000_builtin_types[RS6000_BTI_opaque_V4SI])
#define V16QI_type_node               (rs6000_builtin_types[RS6000_BTI_V16QI])
#define V2SI_type_node                (rs6000_builtin_types[RS6000_BTI_V2SI])
#define V2SF_type_node                (rs6000_builtin_types[RS6000_BTI_V2SF])
#define V4HI_type_node                (rs6000_builtin_types[RS6000_BTI_V4HI])
#define V4SI_type_node                (rs6000_builtin_types[RS6000_BTI_V4SI])
#define V4SF_type_node                (rs6000_builtin_types[RS6000_BTI_V4SF])
#define V8HI_type_node                (rs6000_builtin_types[RS6000_BTI_V8HI])
#define unsigned_V16QI_type_node      (rs6000_builtin_types[RS6000_BTI_unsigned_V16QI])
#define unsigned_V8HI_type_node       (rs6000_builtin_types[RS6000_BTI_unsigned_V8HI])
#define unsigned_V4SI_type_node       (rs6000_builtin_types[RS6000_BTI_unsigned_V4SI])
#define bool_char_type_node           (rs6000_builtin_types[RS6000_BTI_bool_char])
#define bool_short_type_node          (rs6000_builtin_types[RS6000_BTI_bool_short])
#define bool_int_type_node            (rs6000_builtin_types[RS6000_BTI_bool_int])
#define pixel_type_node               (rs6000_builtin_types[RS6000_BTI_pixel])
#define bool_V16QI_type_node	      (rs6000_builtin_types[RS6000_BTI_bool_V16QI])
#define bool_V8HI_type_node	      (rs6000_builtin_types[RS6000_BTI_bool_V8HI])
#define bool_V4SI_type_node	      (rs6000_builtin_types[RS6000_BTI_bool_V4SI])
#define pixel_V8HI_type_node	      (rs6000_builtin_types[RS6000_BTI_pixel_V8HI])

#define long_integer_type_internal_node  (rs6000_builtin_types[RS6000_BTI_long])
#define long_unsigned_type_internal_node (rs6000_builtin_types[RS6000_BTI_unsigned_long])
#define intQI_type_internal_node	 (rs6000_builtin_types[RS6000_BTI_INTQI])
#define uintQI_type_internal_node	 (rs6000_builtin_types[RS6000_BTI_UINTQI])
#define intHI_type_internal_node	 (rs6000_builtin_types[RS6000_BTI_INTHI])
#define uintHI_type_internal_node	 (rs6000_builtin_types[RS6000_BTI_UINTHI])
#define intSI_type_internal_node	 (rs6000_builtin_types[RS6000_BTI_INTSI])
#define uintSI_type_internal_node	 (rs6000_builtin_types[RS6000_BTI_UINTSI])
#define float_type_internal_node	 (rs6000_builtin_types[RS6000_BTI_float])
#define void_type_internal_node		 (rs6000_builtin_types[RS6000_BTI_void])

extern GTY(()) tree rs6000_builtin_types[RS6000_BTI_MAX];
extern GTY(()) tree rs6000_builtin_decls[RS6000_BUILTIN_COUNT];

