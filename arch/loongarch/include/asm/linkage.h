/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_LINKAGE_H
#define __ASM_LINKAGE_H

#define __ALIGN		.align 2
#define __ALIGN_STR	__stringify(__ALIGN)

#define SYM_FUNC_START(name)				\
	SYM_START(name, SYM_L_GLOBAL, SYM_A_ALIGN)	\
	.cfi_startproc;

#define SYM_FUNC_START_NOALIGN(name)			\
	SYM_START(name, SYM_L_GLOBAL, SYM_A_NONE)	\
	.cfi_startproc;

#define SYM_FUNC_START_LOCAL(name)			\
	SYM_START(name, SYM_L_LOCAL, SYM_A_ALIGN)	\
	.cfi_startproc;

#define SYM_FUNC_START_LOCAL_NOALIGN(name)		\
	SYM_START(name, SYM_L_LOCAL, SYM_A_NONE)	\
	.cfi_startproc;

#define SYM_FUNC_START_WEAK(name)			\
	SYM_START(name, SYM_L_WEAK, SYM_A_ALIGN)	\
	.cfi_startproc;

#define SYM_FUNC_START_WEAK_NOALIGN(name)		\
	SYM_START(name, SYM_L_WEAK, SYM_A_NONE)		\
	.cfi_startproc;

#define SYM_FUNC_END(name)				\
	.cfi_endproc;					\
	SYM_END(name, SYM_T_FUNC)

#define SYM_CODE_START(name)				\
	SYM_START(name, SYM_L_GLOBAL, SYM_A_ALIGN)	\
	.cfi_startproc;

#define SYM_CODE_END(name)				\
	.cfi_endproc;					\
	SYM_END(name, SYM_T_NONE)

/*
 * This is for the signal handler trampoline, which is used as the return
 * address of the signal handlers in userspace instead of called normally.
 * The long standing libgcc bug https://gcc.gnu.org/PR124050 requires a
 * nop between .cfi_startproc and the actual address of the trampoline, so
 * we cannot simply use SYM_FUNC_START.
 *
 * This wrapper also contains all the .cfi_* directives for recovering
 * the content of the GPRs and the "return address" (where the rt_sigreturn
 * syscall will jump to), assuming there is a struct rt_sigframe (where
 * a struct sigcontext containing those information we need to recover) at
 * $sp.  The "DWARF for the LoongArch(TM) Architecture" manual states
 * column 0 is for $zero, but it does not make too much sense to
 * save/restore the hardware zero register.  Repurpose this column here
 * for the return address (here it's not the content of $ra we cannot use
 * the default column 3).
 */
#define SYM_SIGFUNC_START(name)				\
	.cfi_startproc;					\
	.cfi_signal_frame;				\
	.cfi_def_cfa 3, RT_SIGFRAME_SC;			\
	.cfi_return_column 0;				\
	.cfi_offset 0, SC_PC;				\
							\
	.irp num, 1,  2,  3,  4,  5,  6,  7,  8, 	\
		  9,  10, 11, 12, 13, 14, 15, 16,	\
		  17, 18, 19, 20, 21, 22, 23, 24,	\
		  25, 26, 27, 28, 29, 30, 31;		\
	.cfi_offset \num, SC_REGS + \num * SZREG;	\
	.endr;						\
							\
	nop;						\
	SYM_START(name, SYM_L_GLOBAL, SYM_A_ALIGN)

#define SYM_SIGFUNC_END(name) SYM_FUNC_END(name)

#endif
