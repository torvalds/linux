/* Definitions of target machine for GNU compiler,
   for IBM RS/6000 POWER running AIX.
   Copyright (C) 2000, 2001, 2002, 2003, 2004, 2005, 2006
   Free Software Foundation, Inc.

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

/* Yes!  We are AIX!  */
#define DEFAULT_ABI ABI_AIX
#undef  TARGET_AIX
#define TARGET_AIX 1

/* AIX always has a TOC.  */
#define TARGET_NO_TOC 0
#define TARGET_TOC 1
#define FIXED_R2 1

/* AIX allows r13 to be used in 32-bit mode.  */
#define FIXED_R13 0

/* 32-bit and 64-bit AIX stack boundary is 128.  */
#undef  STACK_BOUNDARY
#define STACK_BOUNDARY 128

/* AIX does not support Altivec.  */
#undef  TARGET_ALTIVEC
#define TARGET_ALTIVEC 0
#undef  TARGET_ALTIVEC_ABI
#define TARGET_ALTIVEC_ABI 0
#undef  TARGET_IEEEQUAD
#define TARGET_IEEEQUAD 0

/* The AIX linker will discard static constructors in object files before
   collect has a chance to see them, so scan the object files directly.  */
#define COLLECT_EXPORT_LIST

/* Handle #pragma weak and #pragma pack.  */
#define HANDLE_SYSV_PRAGMA 1

/* This is the only version of nm that collect2 can work with.  */
#define REAL_NM_FILE_NAME "/usr/ucb/nm"

#define USER_LABEL_PREFIX  ""

/* Don't turn -B into -L if the argument specifies a relative file name.  */
#define RELATIVE_PREFIX_NOT_LINKDIR

/* Because of the above, we must have gcc search itself to find libgcc.a.  */
#define LINK_LIBGCC_SPECIAL_1

#define MFWRAP_SPEC " %{static: %{fmudflap|fmudflapth: \
 -brename:malloc,__wrap_malloc -brename:__real_malloc,malloc \
 -brename:free,__wrap_free -brename:__real_free,free \
 -brename:calloc,__wrap_calloc -brename:__real_calloc,calloc \
 -brename:realloc,__wrap_realloc -brename:__real_realloc,realloc \
 -brename:mmap,__wrap_mmap -brename:__real_mmap,mmap \
 -brename:munmap,__wrap_munmap -brename:__real_munmap,munmap \
 -brename:alloca,__wrap_alloca -brename:__real_alloca,alloca \
} %{fmudflapth: \
 -brename:pthread_create,__wrap_pthread_create \
 -brename:__real_pthread_create,pthread_create \
 -brename:pthread_join,__wrap_pthread_join \
 -brename:__real_pthread_join,pthread_join \
 -brename:pthread_exit,__wrap_pthread_exit \
 -brename:__real_pthread_exit,pthread_exit \
}} %{fmudflap|fmudflapth: \
 -brename:main,__wrap_main -brename:__real_main,main \
}"

#define MFLIB_SPEC " %{fmudflap: -lmudflap \
 %{static:%(link_gcc_c_sequence) -lmudflap}} \
 %{fmudflapth: -lmudflapth -lpthread \
 %{static:%(link_gcc_c_sequence) -lmudflapth}} "

/* Names to predefine in the preprocessor for this target machine.  */
#define TARGET_OS_AIX_CPP_BUILTINS()		\
  do						\
    {						\
      builtin_define ("_IBMR2");		\
      builtin_define ("_POWER");		\
      builtin_define ("_AIX");			\
      builtin_define ("_AIX32");		\
      builtin_define ("_AIX41");		\
      builtin_define ("_LONG_LONG");		\
      if (TARGET_LONG_DOUBLE_128)		\
        builtin_define ("__LONGDOUBLE128");	\
      builtin_assert ("system=unix");		\
      builtin_assert ("system=aix");		\
    }						\
  while (0)

/* Define appropriate architecture macros for preprocessor depending on
   target switches.  */

#define CPP_SPEC "%{posix: -D_POSIX_SOURCE}\
   %{ansi: -D_ANSI_C_SOURCE}"

#undef ASM_DEFAULT_SPEC
#define ASM_DEFAULT_SPEC ""

/* Tell the assembler to assume that all undefined names are external.

   Don't do this until the fixed IBM assembler is more generally available.
   When this becomes permanently defined, the ASM_OUTPUT_EXTERNAL,
   ASM_OUTPUT_EXTERNAL_LIBCALL, and RS6000_OUTPUT_BASENAME macros will no
   longer be needed.  Also, the extern declaration of mcount in 
   rs6000_xcoff_file_start will no longer be needed.  */

/* #define ASM_SPEC "-u %(asm_cpu)" */

/* Default location of syscalls.exp under AIX */
#ifndef CROSS_DIRECTORY_STRUCTURE
#define LINK_SYSCALLS_SPEC "-bI:/lib/syscalls.exp"
#else
#define LINK_SYSCALLS_SPEC ""
#endif

/* Default location of libg.exp under AIX */
#ifndef CROSS_DIRECTORY_STRUCTURE
#define LINK_LIBG_SPEC "-bexport:/usr/lib/libg.exp"
#else
#define LINK_LIBG_SPEC ""
#endif

/* Define the options for the binder: Start text at 512, align all segments
   to 512 bytes, and warn if there is text relocation.

   The -bhalt:4 option supposedly changes the level at which ld will abort,
   but it also suppresses warnings about multiply defined symbols and is
   used by the AIX cc command.  So we use it here.

   -bnodelcsect undoes a poor choice of default relating to multiply-defined
   csects.  See AIX documentation for more information about this.

   -bM:SRE tells the linker that the output file is Shared REusable.  Note
   that to actually build a shared library you will also need to specify an
   export list with the -Wl,-bE option.  */

#define LINK_SPEC "-T512 -H512 %{!r:-btextro} -bhalt:4 -bnodelcsect\
%{static:-bnso %(link_syscalls) } \
%{!shared:%{g*: %(link_libg) }} %{shared:-bM:SRE}"

/* Profiled library versions are used by linking with special directories.  */
#define LIB_SPEC "%{pg:-L/lib/profiled -L/usr/lib/profiled}\
%{p:-L/lib/profiled -L/usr/lib/profiled} %{!shared:%{g*:-lg}} -lc"

/* This now supports a natural alignment mode.  */
/* AIX word-aligns FP doubles but doubleword-aligns 64-bit ints.  */
#define ADJUST_FIELD_ALIGN(FIELD, COMPUTED) \
  (TARGET_ALIGN_NATURAL ? (COMPUTED) : \
  (TYPE_MODE (TREE_CODE (TREE_TYPE (FIELD)) == ARRAY_TYPE \
	      ? get_inner_array_type (FIELD) \
	      : TREE_TYPE (FIELD)) == DFmode \
   ? MIN ((COMPUTED), 32) : (COMPUTED)))

/* AIX increases natural record alignment to doubleword if the first
   field is an FP double while the FP fields remain word aligned.  */
#define ROUND_TYPE_ALIGN(STRUCT, COMPUTED, SPECIFIED)			\
  ((TREE_CODE (STRUCT) == RECORD_TYPE					\
    || TREE_CODE (STRUCT) == UNION_TYPE					\
    || TREE_CODE (STRUCT) == QUAL_UNION_TYPE)				\
   && TARGET_ALIGN_NATURAL == 0						\
   ? rs6000_special_round_type_align (STRUCT, COMPUTED, SPECIFIED)	\
   : MAX ((COMPUTED), (SPECIFIED)))

/* The AIX ABI isn't explicit on whether aggregates smaller than a
   word/doubleword should be padded upward or downward.  One could
   reasonably assume that they follow the normal rules for structure
   layout treating the parameter area as any other block of memory,
   then map the reg param area to registers, i.e., pad upward, which
   is the way IBM Compilers for AIX behave.
   Setting both of the following defines results in this behavior.  */
#define AGGREGATE_PADDING_FIXED 1
#define AGGREGATES_PAD_UPWARD_ALWAYS 1

/* Specify padding for the last element of a block move between
   registers and memory.  FIRST is nonzero if this is the only
   element.  */
#define BLOCK_REG_PADDING(MODE, TYPE, FIRST) \
  (!(FIRST) ? upward : FUNCTION_ARG_PADDING (MODE, TYPE))

/* Indicate that jump tables go in the text section.  */

#define JUMP_TABLES_IN_TEXT_SECTION 1

/* Define any extra SPECS that the compiler needs to generate.  */
#undef  SUBTARGET_EXTRA_SPECS
#define SUBTARGET_EXTRA_SPECS						\
  { "link_syscalls",            LINK_SYSCALLS_SPEC },			\
  { "link_libg",                LINK_LIBG_SPEC }

/* Define cutoff for using external functions to save floating point.  */
#define FP_SAVE_INLINE(FIRST_REG) ((FIRST_REG) == 62 || (FIRST_REG) == 63)

/* __throw will restore its own return address to be the same as the
   return address of the function that the throw is being made to.
   This is unfortunate, because we want to check the original
   return address to see if we need to restore the TOC.
   So we have to squirrel it away with this.  */
#define SETUP_FRAME_ADDRESSES() rs6000_aix_emit_builtin_unwind_init ()

/* If the current unwind info (FS) does not contain explicit info
   saving R2, then we have to do a minor amount of code reading to
   figure out if it was saved.  The big problem here is that the
   code that does the save/restore is generated by the linker, so
   we have no good way to determine at compile time what to do.  */

#ifdef __64BIT__
#define MD_FROB_UPDATE_CONTEXT(CTX, FS)					\
  do {									\
    if ((FS)->regs.reg[2].how == REG_UNSAVED)				\
      {									\
	unsigned int *insn						\
	  = (unsigned int *)						\
	    _Unwind_GetGR ((CTX), LINK_REGISTER_REGNUM);		\
	if (*insn == 0xE8410028)					\
	  _Unwind_SetGRPtr ((CTX), 2, (CTX)->cfa + 40);			\
      }									\
  } while (0)
#else
#define MD_FROB_UPDATE_CONTEXT(CTX, FS)					\
  do {									\
    if ((FS)->regs.reg[2].how == REG_UNSAVED)				\
      {									\
	unsigned int *insn						\
	  = (unsigned int *)						\
	    _Unwind_GetGR ((CTX), LINK_REGISTER_REGNUM);		\
	if (*insn == 0x80410014)					\
	  _Unwind_SetGRPtr ((CTX), 2, (CTX)->cfa + 20);			\
      }									\
  } while (0)
#endif

#define PROFILE_HOOK(LABEL)   output_profile_hook (LABEL)

/* Print subsidiary information on the compiler version in use.  */
#define TARGET_VERSION ;

/* No version of AIX fully supports AltiVec or 64-bit instructions in
   32-bit mode.  */
#define OS_MISSING_POWERPC64 1
#define OS_MISSING_ALTIVEC 1

/* WINT_TYPE */
#define WINT_TYPE "int"
