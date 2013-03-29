/*
 * include/asm-xtensa/ptrace.h
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2001 - 2005 Tensilica Inc.
 */
#ifndef _XTENSA_PTRACE_H
#define _XTENSA_PTRACE_H

#include <uapi/asm/ptrace.h>


#ifndef __ASSEMBLY__

#include <asm/coprocessor.h>

/*
 * This struct defines the way the registers are stored on the
 * kernel stack during a system call or other kernel entry.
 */
struct pt_regs {
	unsigned long pc;		/*   4 */
	unsigned long ps;		/*   8 */
	unsigned long depc;		/*  12 */
	unsigned long exccause;		/*  16 */
	unsigned long excvaddr;		/*  20 */
	unsigned long debugcause;	/*  24 */
	unsigned long wmask;		/*  28 */
	unsigned long lbeg;		/*  32 */
	unsigned long lend;		/*  36 */
	unsigned long lcount;		/*  40 */
	unsigned long sar;		/*  44 */
	unsigned long windowbase;	/*  48 */
	unsigned long windowstart;	/*  52 */
	unsigned long syscall;		/*  56 */
	unsigned long icountlevel;	/*  60 */
	unsigned long scompare1;	/*  64 */
	unsigned long threadptr;	/*  68 */

	/* Additional configurable registers that are used by the compiler. */
	xtregs_opt_t xtregs_opt;

	/* Make sure the areg field is 16 bytes aligned. */
	int align[0] __attribute__ ((aligned(16)));

	/* current register frame.
	 * Note: The ESF for kernel exceptions ends after 16 registers!
	 */
	unsigned long areg[16];
};

#include <variant/core.h>

# define arch_has_single_step()	(1)
# define task_pt_regs(tsk) ((struct pt_regs*) \
	(task_stack_page(tsk) + KERNEL_STACK_SIZE - (XCHAL_NUM_AREGS-16)*4) - 1)
# define user_mode(regs) (((regs)->ps & 0x00000020)!=0)
# define instruction_pointer(regs) ((regs)->pc)

# ifndef CONFIG_SMP
#  define profile_pc(regs) instruction_pointer(regs)
# endif

#define user_stack_pointer(regs) ((regs)->areg[1])

#else	/* __ASSEMBLY__ */

# include <asm/asm-offsets.h>
#define PT_REGS_OFFSET	  (KERNEL_STACK_SIZE - PT_USER_SIZE)

#endif	/* !__ASSEMBLY__ */

#endif	/* _XTENSA_PTRACE_H */
