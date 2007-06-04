/*
 *    Copyright (c) 2002 Stephen Rothwell, IBM Coproration
 *    Extracted from ptrace.c and ptrace32.c
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file README.legal in the main directory of
 * this archive for more details.
 */

#ifndef _POWERPC_PTRACE_PPC64_H
#define _POWERPC_PTRACE_PPC64_H

/*
 * Set of msr bits that gdb can change on behalf of a process.
 */
#define MSR_DEBUGCHANGE	(MSR_FE0 | MSR_SE | MSR_BE | MSR_FE1)

/*
 * Max register writeable via put_reg
 */
#define PT_MAX_PUT_REG	PT_CCR

/*
 * Munging of MSR on return from get_regs
 *
 * Put the correct FP bits in, they might be wrong as a result
 * of our lazy FP restore.
 */

#define PT_MUNGE_MSR(msr, task)	({ (msr) | (task)->thread.fpexc_mode; })

static inline int ptrace_set_debugreg(struct task_struct *task,
				      unsigned long addr, unsigned long data)
{
	/* We only support one DABR and no IABRS at the moment */
	if (addr > 0)
		return -EINVAL;

	/* The bottom 3 bits are flags */
	if ((data & ~0x7UL) >= TASK_SIZE)
		return -EIO;

	/* Ensure translation is on */
	if (data && !(data & DABR_TRANSLATION))
		return -EIO;

	task->thread.dabr = data;
	return 0;
}

#endif /* _POWERPC_PTRACE_PPC64_H */
