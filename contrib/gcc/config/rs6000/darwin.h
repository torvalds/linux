/* Target definitions for PowerPC running Darwin (Mac OS X).
   Copyright (C) 1997, 2000, 2001, 2003, 2004, 2005, 2006
   Free Software Foundation, Inc.
   Contributed by Apple Computer Inc.

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

#undef  TARGET_VERSION
#define TARGET_VERSION fprintf (stderr, " (Darwin/PowerPC)");

/* The "Darwin ABI" is mostly like AIX, but with some key differences.  */

#define DEFAULT_ABI ABI_DARWIN

#ifdef IN_LIBGCC2
#undef TARGET_64BIT
#ifdef __powerpc64__
#define TARGET_64BIT 1
#else
#define TARGET_64BIT 0
#endif
#endif

/* The object file format is Mach-O.  */

#define TARGET_OBJECT_FORMAT OBJECT_MACHO

/* Size of the Obj-C jump buffer.  */
#define OBJC_JBLEN ((TARGET_64BIT) ? (26*2 + 18*2 + 129 + 1) : (26 + 18*2 + 129 + 1))

/* We're not ever going to do TOCs.  */

#define TARGET_TOC 0
#define TARGET_NO_TOC 1

/* Override the default rs6000 definition.  */
#undef  PTRDIFF_TYPE
#define PTRDIFF_TYPE (TARGET_64BIT ? "long int" : "int")

/* Translate config/rs6000/darwin.opt to config/darwin.h.  */
#define TARGET_DYNAMIC_NO_PIC (TARGET_MACHO_DYNAMIC_NO_PIC)

#define TARGET_OS_CPP_BUILTINS()                \
  do                                            \
    {                                           \
      if (!TARGET_64BIT) builtin_define ("__ppc__");   \
      if (TARGET_64BIT) builtin_define ("__ppc64__");  \
      builtin_define ("__POWERPC__");           \
      builtin_define ("__NATURAL_ALIGNMENT__"); \
      darwin_cpp_builtins (pfile);		\
    }                                           \
  while (0)


#define SUBTARGET_OVERRIDE_OPTIONS					\
do {									\
  /* The Darwin ABI always includes AltiVec, can't be (validly) turned	\
     off.  */								\
  rs6000_altivec_abi = 1;						\
  TARGET_ALTIVEC_VRSAVE = 1;						\
  if (DEFAULT_ABI == ABI_DARWIN)					\
  {									\
    if (MACHO_DYNAMIC_NO_PIC_P)						\
      {									\
        if (flag_pic)							\
            warning (0, "-mdynamic-no-pic overrides -fpic or -fPIC");	\
        flag_pic = 0;							\
      }									\
    else if (flag_pic == 1)						\
      {									\
        flag_pic = 2;							\
      }									\
  }									\
  if (TARGET_64BIT && ! TARGET_POWERPC64)				\
    {									\
      target_flags |= MASK_POWERPC64;					\
      warning (0, "-m64 requires PowerPC64 architecture, enabling");	\
    }									\
  if (flag_mkernel)							\
    {									\
      rs6000_default_long_calls = 1;					\
      target_flags |= MASK_SOFT_FLOAT;					\
    }									\
									\
  /* Make -m64 imply -maltivec.  Darwin's 64-bit ABI includes		\
     Altivec.  */							\
  if (!flag_mkernel && !flag_apple_kext					\
      && TARGET_64BIT							\
      && ! (target_flags_explicit & MASK_ALTIVEC))			\
    target_flags |= MASK_ALTIVEC;					\
									\
  /* Unless the user (not the configurer) has explicitly overridden	\
     it with -mcpu=G3 or -mno-altivec, then 10.5+ targets default to	\
     G4 unless targetting the kernel.  */				\
  if (!flag_mkernel							\
      && !flag_apple_kext						\
      && darwin_macosx_version_min					\
      && strverscmp (darwin_macosx_version_min, "10.5") >= 0		\
      && ! (target_flags_explicit & MASK_ALTIVEC)			\
      && ! rs6000_select[1].string)					\
    {									\
      target_flags |= MASK_ALTIVEC;					\
    }									\
} while(0)

#define C_COMMON_OVERRIDE_OPTIONS do {					\
  /* On powerpc, __cxa_get_exception_ptr is available starting in the	\
     10.4.6 libstdc++.dylib.  */					\
  if ((! darwin_macosx_version_min					\
       || strverscmp (darwin_macosx_version_min, "10.4.6") < 0)		\
      && flag_use_cxa_get_exception_ptr == 2)				\
    flag_use_cxa_get_exception_ptr = 0;					\
  if (flag_mkernel)							\
    flag_no_builtin = 1;						\
  SUBTARGET_C_COMMON_OVERRIDE_OPTIONS;					\
} while (0)

/* Darwin has 128-bit long double support in libc in 10.4 and later.
   Default to 128-bit long doubles even on earlier platforms for ABI
   consistency; arithmetic will work even if libc and libm support is
   not available.  */

#define RS6000_DEFAULT_LONG_DOUBLE_SIZE 128


/* We want -fPIC by default, unless we're using -static to compile for
   the kernel or some such.  */

#define CC1_SPEC "\
  %{g: %{!fno-eliminate-unused-debug-symbols: -feliminate-unused-debug-symbols }} \
  %{static: %{Zdynamic: %e conflicting code gen style switches are used}}\
  %{!mkernel:%{!static:%{!mdynamic-no-pic:-fPIC}}}"

#define DARWIN_ARCH_SPEC "%{m64:ppc64;:ppc}"

#define DARWIN_SUBARCH_SPEC "			\
 %{m64: ppc64}					\
 %{!m64:					\
 %{mcpu=601:ppc601;				\
   mcpu=603:ppc603;				\
   mcpu=603e:ppc603;				\
   mcpu=604:ppc604;				\
   mcpu=604e:ppc604e;				\
   mcpu=740:ppc750;				\
   mcpu=750:ppc750;				\
   mcpu=G3:ppc750;				\
   mcpu=7400:ppc7400;				\
   mcpu=G4:ppc7400;				\
   mcpu=7450:ppc7450;				\
   mcpu=970:ppc970;				\
   mcpu=power4:ppc970;				\
   mcpu=G5:ppc970;				\
   :ppc}}"

/* crt2.o is at least partially required for 10.3.x and earlier.  */
#define DARWIN_CRT2_SPEC \
  "%{!m64:%:version-compare(!> 10.4 mmacosx-version-min= crt2.o%s)}"

#undef SUBTARGET_EXTRA_SPECS
#define SUBTARGET_EXTRA_SPECS			\
  { "darwin_arch", DARWIN_ARCH_SPEC },		\
  { "darwin_crt2", DARWIN_CRT2_SPEC },		\
  { "darwin_subarch", DARWIN_SUBARCH_SPEC },

/* Output a .machine directive.  */
#undef TARGET_ASM_FILE_START
#define TARGET_ASM_FILE_START rs6000_darwin_file_start

/* The "-faltivec" option should have been called "-maltivec" all
   along.  -ffix-and-continue and -findirect-data is for compatibility
   for old compilers.  */

#define SUBTARGET_OPTION_TRANSLATE_TABLE				\
  { "-ffix-and-continue", "-mfix-and-continue" },			\
  { "-findirect-data", "-mfix-and-continue" },				\
  { "-faltivec", "-maltivec -include altivec.h" },			\
  { "-fno-altivec", "-mno-altivec" },					\
  { "-Waltivec-long-deprecated",	"-mwarn-altivec-long" },	\
  { "-Wno-altivec-long-deprecated", "-mno-warn-altivec-long" }

/* Make both r2 and r13 available for allocation.  */
#define FIXED_R2 0
#define FIXED_R13 0

/* Base register for access to local variables of the function.  */

#undef  HARD_FRAME_POINTER_REGNUM
#define HARD_FRAME_POINTER_REGNUM 30

#undef  RS6000_PIC_OFFSET_TABLE_REGNUM
#define RS6000_PIC_OFFSET_TABLE_REGNUM 31

/* Pad the outgoing args area to 16 bytes instead of the usual 8.  */

#undef STARTING_FRAME_OFFSET
#define STARTING_FRAME_OFFSET						\
  (FRAME_GROWS_DOWNWARD							\
   ? 0									\
   : (RS6000_ALIGN (current_function_outgoing_args_size, 16)		\
      + RS6000_SAVE_AREA))

#undef STACK_DYNAMIC_OFFSET
#define STACK_DYNAMIC_OFFSET(FUNDECL)					\
  (RS6000_ALIGN (current_function_outgoing_args_size, 16)		\
   + (STACK_POINTER_OFFSET))

/* These are used by -fbranch-probabilities */
#define HOT_TEXT_SECTION_NAME "__TEXT,__text,regular,pure_instructions"
#define UNLIKELY_EXECUTED_TEXT_SECTION_NAME \
                              "__TEXT,__unlikely,regular,pure_instructions"

/* Define cutoff for using external functions to save floating point.
   Currently on Darwin, always use inline stores.  */

#undef	FP_SAVE_INLINE
#define FP_SAVE_INLINE(FIRST_REG) ((FIRST_REG) < 64)

/* Darwin uses a function call if everything needs to be saved/restored.  */
#undef WORLD_SAVE_P
#define WORLD_SAVE_P(INFO) ((INFO)->world_save_p)

/* The assembler wants the alternate register names, but without
   leading percent sign.  */
#undef REGISTER_NAMES
#define REGISTER_NAMES							\
{									\
     "r0",  "r1",  "r2",  "r3",  "r4",  "r5",  "r6",  "r7",		\
     "r8",  "r9", "r10", "r11", "r12", "r13", "r14", "r15",		\
    "r16", "r17", "r18", "r19", "r20", "r21", "r22", "r23",		\
    "r24", "r25", "r26", "r27", "r28", "r29", "r30", "r31",		\
     "f0",  "f1",  "f2",  "f3",  "f4",  "f5",  "f6",  "f7",		\
     "f8",  "f9", "f10", "f11", "f12", "f13", "f14", "f15",		\
    "f16", "f17", "f18", "f19", "f20", "f21", "f22", "f23",		\
    "f24", "f25", "f26", "f27", "f28", "f29", "f30", "f31",		\
     "mq",  "lr", "ctr",  "ap",						\
    "cr0", "cr1", "cr2", "cr3", "cr4", "cr5", "cr6", "cr7",		\
    "xer",								\
     "v0",  "v1",  "v2",  "v3",  "v4",  "v5",  "v6",  "v7",             \
     "v8",  "v9", "v10", "v11", "v12", "v13", "v14", "v15",             \
    "v16", "v17", "v18", "v19", "v20", "v21", "v22", "v23",             \
    "v24", "v25", "v26", "v27", "v28", "v29", "v30", "v31",             \
    "vrsave", "vscr",							\
    "spe_acc", "spefscr",                                               \
    "sfp"								\
}

/* This outputs NAME to FILE.  */

#undef  RS6000_OUTPUT_BASENAME
#define RS6000_OUTPUT_BASENAME(FILE, NAME)	\
    assemble_name (FILE, NAME)

/* Globalizing directive for a label.  */
#undef GLOBAL_ASM_OP
#define GLOBAL_ASM_OP "\t.globl "
#undef TARGET_ASM_GLOBALIZE_LABEL

/* This is how to output an internal label prefix.  rs6000.c uses this
   when generating traceback tables.  */
/* Not really used for Darwin?  */

#undef ASM_OUTPUT_INTERNAL_LABEL_PREFIX
#define ASM_OUTPUT_INTERNAL_LABEL_PREFIX(FILE,PREFIX)	\
  fprintf (FILE, "%s", PREFIX)

/* This says how to output an assembler line to define a global common
   symbol.  */
#define ASM_OUTPUT_COMMON(FILE, NAME, SIZE, ROUNDED)			\
  do {									\
    unsigned HOST_WIDE_INT _new_size = SIZE;				\
    fputs (".comm ", (FILE));						\
    RS6000_OUTPUT_BASENAME ((FILE), (NAME));				\
    if (_new_size == 0) _new_size = 1;					\
    fprintf ((FILE), ","HOST_WIDE_INT_PRINT_UNSIGNED"\n", _new_size);	\
  } while (0)

/* Override the standard rs6000 definition.  */

#undef ASM_COMMENT_START
#define ASM_COMMENT_START ";"

/* FP save and restore routines.  */
#define	SAVE_FP_PREFIX "._savef"
#define SAVE_FP_SUFFIX ""
#define	RESTORE_FP_PREFIX "._restf"
#define RESTORE_FP_SUFFIX ""

/* This is how to output an assembler line that says to advance
   the location counter to a multiple of 2**LOG bytes using the
   "nop" instruction as padding.  */

#define ASM_OUTPUT_ALIGN_WITH_NOP(FILE,LOG)                   \
  do                                                          \
    {                                                         \
      if ((LOG) < 3)                                          \
        {                                                     \
          ASM_OUTPUT_ALIGN (FILE,LOG);                        \
        }                                                     \
      else /* nop == ori r0,r0,0 */                           \
        fprintf (FILE, "\t.align32 %d,0x60000000\n", (LOG));  \
    } while (0)

#ifdef HAVE_GAS_MAX_SKIP_P2ALIGN
/* This is supported in cctools 465 and later.  The macro test
   above prevents using it in earlier build environments.  */
#define ASM_OUTPUT_MAX_SKIP_ALIGN(FILE,LOG,MAX_SKIP)          \
  if ((LOG) != 0)                                             \
    {                                                         \
      if ((MAX_SKIP) == 0)                                    \
        fprintf ((FILE), "\t.p2align %d\n", (LOG));           \
      else                                                    \
        fprintf ((FILE), "\t.p2align %d,,%d\n", (LOG), (MAX_SKIP)); \
    }
#endif

/* Generate insns to call the profiler.  */

#define PROFILE_HOOK(LABEL)   output_profile_hook (LABEL)

/* Function name to call to do profiling.  */

#define RS6000_MCOUNT "*mcount"

/* Default processor: G4, and G5 for 64-bit.  */

#undef PROCESSOR_DEFAULT
#define PROCESSOR_DEFAULT  PROCESSOR_PPC7400
#undef PROCESSOR_DEFAULT64
#define PROCESSOR_DEFAULT64  PROCESSOR_POWER4

/* Default target flag settings.  Despite the fact that STMW/LMW
   serializes, it's still a big code size win to use them.  Use FSEL by
   default as well.  */

#undef  TARGET_DEFAULT
#define TARGET_DEFAULT (MASK_POWERPC | MASK_MULTIPLE | MASK_NEW_MNEMONICS \
                      | MASK_PPC_GFXOPT)

/* Darwin only runs on PowerPC, so short-circuit POWER patterns.  */
#undef  TARGET_POWER
#define TARGET_POWER 0
#undef  TARGET_IEEEQUAD
#define TARGET_IEEEQUAD 0

/* Since Darwin doesn't do TOCs, stub this out.  */

#define ASM_OUTPUT_SPECIAL_POOL_ENTRY_P(X, MODE)  ((void)X, (void)MODE, 0)

/* Unlike most other PowerPC targets, chars are signed, for
   consistency with other Darwin architectures.  */

#undef DEFAULT_SIGNED_CHAR
#define DEFAULT_SIGNED_CHAR (1)

/* Given an rtx X being reloaded into a reg required to be
   in class CLASS, return the class of reg to actually use.
   In general this is just CLASS; but on some machines
   in some cases it is preferable to use a more restrictive class.

   On the RS/6000, we have to return NO_REGS when we want to reload a
   floating-point CONST_DOUBLE to force it to be copied to memory.

   Don't allow R0 when loading the address of, or otherwise furtling with,
   a SYMBOL_REF.  */

#undef PREFERRED_RELOAD_CLASS
#define PREFERRED_RELOAD_CLASS(X,CLASS)				\
  ((CONSTANT_P (X)						\
    && reg_classes_intersect_p ((CLASS), FLOAT_REGS))		\
   ? NO_REGS							\
   : ((GET_CODE (X) == SYMBOL_REF || GET_CODE (X) == HIGH)	\
      && reg_class_subset_p (BASE_REGS, (CLASS)))		\
   ? BASE_REGS							\
   : (GET_MODE_CLASS (GET_MODE (X)) == MODE_INT			\
      && (CLASS) == NON_SPECIAL_REGS)				\
   ? GENERAL_REGS						\
   : (CLASS))

/* Fix for emit_group_load (): force large constants to be pushed via regs.  */
#define ALWAYS_PUSH_CONSTS_USING_REGS_P		1

/* This now supports a natural alignment mode */
/* Darwin word-aligns FP doubles but doubleword-aligns 64-bit ints.  */
#define ADJUST_FIELD_ALIGN(FIELD, COMPUTED) \
  (TARGET_ALIGN_NATURAL ? (COMPUTED) : \
  (TYPE_MODE (TREE_CODE (TREE_TYPE (FIELD)) == ARRAY_TYPE \
	      ? get_inner_array_type (FIELD) \
	      : TREE_TYPE (FIELD)) == DFmode \
   ? MIN ((COMPUTED), 32) : (COMPUTED)))

/* Darwin increases natural record alignment to doubleword if the first
   field is an FP double while the FP fields remain word aligned.  */
#define ROUND_TYPE_ALIGN(STRUCT, COMPUTED, SPECIFIED)			\
  ((TREE_CODE (STRUCT) == RECORD_TYPE					\
    || TREE_CODE (STRUCT) == UNION_TYPE					\
    || TREE_CODE (STRUCT) == QUAL_UNION_TYPE)				\
   && TARGET_ALIGN_NATURAL == 0                         		\
   ? rs6000_special_round_type_align (STRUCT, COMPUTED, SPECIFIED)	\
   : (TREE_CODE (STRUCT) == VECTOR_TYPE					\
      && ALTIVEC_VECTOR_MODE (TYPE_MODE (STRUCT))) 			\
   ? MAX (MAX ((COMPUTED), (SPECIFIED)), 128)          			 \
   : MAX ((COMPUTED), (SPECIFIED)))

/* Specify padding for the last element of a block move between
   registers and memory.  FIRST is nonzero if this is the only
   element.  */
#define BLOCK_REG_PADDING(MODE, TYPE, FIRST) \
  (!(FIRST) ? upward : FUNCTION_ARG_PADDING (MODE, TYPE))

/* XXX: Darwin supports neither .quad, or .llong, but it also doesn't
   support 64 bit PowerPC either, so this just keeps things happy.  */
#define DOUBLE_INT_ASM_OP "\t.quad\t"

/* For binary compatibility with 2.95; Darwin C APIs use bool from
   stdbool.h, which was an int-sized enum in 2.95.  Users can explicitly
   choose to have sizeof(bool)==1 with the -mone-byte-bool switch. */
#define BOOL_TYPE_SIZE (darwin_one_byte_bool ? CHAR_TYPE_SIZE : INT_TYPE_SIZE)

#undef REGISTER_TARGET_PRAGMAS
#define REGISTER_TARGET_PRAGMAS() \
  do \
    { \
      DARWIN_REGISTER_TARGET_PRAGMAS(); \
      targetm.resolve_overloaded_builtin = altivec_resolve_overloaded_builtin; \
    } \
  while (0)

#ifdef IN_LIBGCC2
#include <stdbool.h>
#endif

#define MD_UNWIND_SUPPORT "config/rs6000/darwin-unwind.h"

#define HAS_MD_FALLBACK_FRAME_STATE_FOR 1

/* True, iff we're generating fast turn around debugging code.  When
   true, we arrange for function prologues to start with 5 nops so
   that gdb may insert code to redirect them, and for data to be
   accessed indirectly.  The runtime uses this indirection to forward
   references for data to the original instance of that data.  */

#define TARGET_FIX_AND_CONTINUE (darwin_fix_and_continue)

/* This is the reserved direct dispatch address for Objective-C.  */
#define OFFS_MSGSEND_FAST		0xFFFEFF00

/* This is the reserved ivar address Objective-C.  */
#define OFFS_ASSIGNIVAR_FAST		0xFFFEFEC0

/* Old versions of Mac OS/Darwin don't have C99 functions available.  */
#undef TARGET_C99_FUNCTIONS
#define TARGET_C99_FUNCTIONS					\
  (TARGET_64BIT							\
   || (darwin_macosx_version_min				\
       && strverscmp (darwin_macosx_version_min, "10.3") >= 0))

/* When generating kernel code or kexts, we don't use Altivec by
   default, as kernel code doesn't save/restore those registers.  */
#define OS_MISSING_ALTIVEC (flag_mkernel || flag_apple_kext)
