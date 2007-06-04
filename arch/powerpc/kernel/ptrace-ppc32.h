/*
 *    Copyright (c) 2007 Benjamin Herrenschmidt, IBM Coproration
 *    Extracted from ptrace.c and ptrace32.c
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file README.legal in the main directory of
 * this archive for more details.
 */

#ifndef _POWERPC_PTRACE_PPC32_H
#define _POWERPC_PTRACE_PPC32_H

/*
 * Set of msr bits that gdb can change on behalf of a process.
 */
#if defined(CONFIG_40x) || defined(CONFIG_BOOKE)
#define MSR_DEBUGCHANGE	0
#else
#define MSR_DEBUGCHANGE	(MSR_SE | MSR_BE)
#endif

/*
 * Max register writeable via put_reg
 */
#define PT_MAX_PUT_REG	PT_MQ

/*
 * Munging of MSR on return from get_regs
 *
 * Nothing to do on ppc32
 */
#define PT_MUNGE_MSR(msr, task)	(msr)


#ifdef CONFIG_SPE

/*
 * For get_evrregs/set_evrregs functions 'data' has the following layout:
 *
 * struct {
 *   u32 evr[32];
 *   u64 acc;
 *   u32 spefscr;
 * }
 */

/*
 * Get contents of SPE register state in task TASK.
 */
static inline int get_evrregs(unsigned long *data, struct task_struct *task)
{
	int i;

	if (!access_ok(VERIFY_WRITE, data, 35 * sizeof(unsigned long)))
		return -EFAULT;

	/* copy SPEFSCR */
	if (__put_user(task->thread.spefscr, &data[34]))
		return -EFAULT;

	/* copy SPE registers EVR[0] .. EVR[31] */
	for (i = 0; i < 32; i++, data++)
		if (__put_user(task->thread.evr[i], data))
			return -EFAULT;

	/* copy ACC */
	if (__put_user64(task->thread.acc, (unsigned long long *)data))
		return -EFAULT;

	return 0;
}

/*
 * Write contents of SPE register state into task TASK.
 */
static inline int set_evrregs(struct task_struct *task, unsigned long *data)
{
	int i;

	if (!access_ok(VERIFY_READ, data, 35 * sizeof(unsigned long)))
		return -EFAULT;

	/* copy SPEFSCR */
	if (__get_user(task->thread.spefscr, &data[34]))
		return -EFAULT;

	/* copy SPE registers EVR[0] .. EVR[31] */
	for (i = 0; i < 32; i++, data++)
		if (__get_user(task->thread.evr[i], data))
			return -EFAULT;
	/* copy ACC */
	if (__get_user64(task->thread.acc, (unsigned long long*)data))
		return -EFAULT;

	return 0;
}
#endif /* CONFIG_SPE */


#endif /* _POWERPC_PTRACE_PPC32_H */
