/* NetBSD/arm a.out version.
   Copyright (C) 1993, 1994, 1997, 1998, 2003, 2004, 2005
   Free Software Foundation, Inc.
   Contributed by Mark Brinicombe (amb@physig.ph.kcl.ac.uk)

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

/* Run-time Target Specification.  */
#undef  TARGET_VERSION
#define TARGET_VERSION fputs (" (ARM/NetBSD)", stderr);

/* Unsigned chars produces much better code than signed.  */
#define DEFAULT_SIGNED_CHAR  0

/* Since we always use GAS as our assembler we support stabs.  */
#define DBX_DEBUGGING_INFO 1

/*#undef ASM_DECLARE_FUNCTION_NAME*/

/* ARM6 family default cpu.  */
#define SUBTARGET_CPU_DEFAULT TARGET_CPU_arm6

#undef TARGET_DEFAULT
#define TARGET_DEFAULT (MASK_APCS_FRAME)

/* Some defines for CPP.
   arm32 is the NetBSD port name, so we always define arm32 and __arm32__.  */
#define TARGET_OS_CPP_BUILTINS()		\
    do {					\
	NETBSD_OS_CPP_BUILTINS_AOUT();		\
	builtin_define_std ("arm32");		\
	builtin_define_std ("unix");		\
	builtin_define_std ("riscbsd");		\
    } while (0)

#undef SUBTARGET_EXTRA_SPECS
#define SUBTARGET_EXTRA_SPECS \
  { "netbsd_cpp_spec",  NETBSD_CPP_SPEC }, \
  { "netbsd_link_spec", NETBSD_LINK_SPEC_AOUT },

#undef CPP_SPEC
#define CPP_SPEC "\
%(cpp_cpu_arch) %(cpp_float) %(cpp_endian) %(netbsd_cpp_spec) \
"

/* Because TARGET_DEFAULT sets MASK_SOFT_FLOAT */
#undef CPP_FLOAT_DEFAULT_SPEC
#define CPP_FLOAT_DEFAULT_SPEC "-D__SOFTFP__"

/* Pass -X to the linker so that it will strip symbols starting with 'L' */
#undef LINK_SPEC
#define LINK_SPEC "-X %(netbsd_link_spec)"

#undef SIZE_TYPE
#define SIZE_TYPE "unsigned int"

#undef PTRDIFF_TYPE
#define PTRDIFF_TYPE "int"

#define HANDLE_SYSV_PRAGMA 1

/* We don't have any limit on the length as out debugger is GDB.  */
#undef DBX_CONTIN_LENGTH

/* NetBSD does its profiling differently to the Acorn compiler. We
   don't need a word following the mcount call; and to skip it
   requires either an assembly stub or use of fomit-frame-pointer when
   compiling the profiling functions.  Since we break Acorn CC
   compatibility below a little more won't hurt.  */

#undef  ARM_FUNCTION_PROFILER
#define ARM_FUNCTION_PROFILER(STREAM,LABELNO)  				    \
{									    \
  fprintf(STREAM, "\tmov\t%sip, %slr\n", REGISTER_PREFIX, REGISTER_PREFIX); \
  fprintf(STREAM, "\tbl\tmcount\n");					    \
}

/* On the ARM `@' introduces a comment, so we must use something else
   for .type directives.  */
#undef TYPE_OPERAND_FMT
#define TYPE_OPERAND_FMT "%%%s"

/* NetBSD uses the old PCC style aggregate returning conventions.  */
#undef DEFAULT_PCC_STRUCT_RETURN
#define DEFAULT_PCC_STRUCT_RETURN 1

/* Although not normally relevant (since by default, all aggregates
   are returned in memory) compiling some parts of libc requires
   non-APCS style struct returns.  */
#undef RETURN_IN_MEMORY

/* VERY BIG NOTE : Change of structure alignment for RiscBSD.
   There are consequences you should be aware of...

   Normally GCC/arm uses a structure alignment of 32 for compatibility
   with armcc.  This means that structures are padded to a word
   boundary.  However this causes problems with bugged NetBSD kernel
   code (possibly userland code as well - I have not checked every
   binary).  The nature of this bugged code is to rely on sizeof()
   returning the correct size of various structures rounded to the
   nearest byte (SCSI and ether code are two examples, the vm system
   is another).  This code breaks when the structure alignment is 32
   as sizeof() will report a word=rounded size.  By changing the
   structure alignment to 8. GCC will conform to what is expected by
   NetBSD.

   This has several side effects that should be considered.
   1. Structures will only be aligned to the size of the largest member.
      i.e. structures containing only bytes will be byte aligned.
           structures containing shorts will be half word aligned.
           structures containing ints will be word aligned.

      This means structures should be padded to a word boundary if
      alignment of 32 is required for byte structures etc.
      
   2. A potential performance penalty may exist if strings are no longer
      word aligned.  GCC will not be able to use word load/stores to copy
      short strings.
      
   This modification is not encouraged but with the present state of the
   NetBSD source tree it is currently the only solution that meets the
   requirements.  */
#undef  DEFAULT_STRUCTURE_SIZE_BOUNDARY
#define DEFAULT_STRUCTURE_SIZE_BOUNDARY 8

/* Clear the instruction cache from `BEG' to `END'.  This makes a
   call to the ARM32_SYNC_ICACHE architecture specific syscall.  */
#define CLEAR_INSN_CACHE(BEG, END)                                     \
{                                                                      \
  extern int sysarch(int number, void *args);                          \
  struct {                                                             \
    unsigned int  addr;                                                \
    int           len;                                                 \
  } s;                                                                 \
  s.addr = (unsigned int)(BEG);                                        \
  s.len = (END) - (BEG);                                               \
  (void)sysarch(0, &s);                                                \
}
