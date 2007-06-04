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


#endif /* _POWERPC_PTRACE_PPC32_H */
