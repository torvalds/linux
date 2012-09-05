/*
 * PowerPC BookIII S hardware breakpoint definitions
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * Copyright 2010, IBM Corporation.
 * Author: K.Prasad <prasad@linux.vnet.ibm.com>
 *
 */

#ifndef _PPC_BOOK3S_64_HW_BREAKPOINT_H
#define _PPC_BOOK3S_64_HW_BREAKPOINT_H

#ifdef	__KERNEL__
#ifdef CONFIG_HAVE_HW_BREAKPOINT

struct arch_hw_breakpoint {
	unsigned long	address;
	int		type;
	u8		len; /* length of the target data symbol */
	bool		extraneous_interrupt;
};

#include <linux/kdebug.h>
#include <asm/reg.h>
#include <asm/debug.h>

struct perf_event;
struct pmu;
struct perf_sample_data;

#define HW_BREAKPOINT_ALIGN 0x7
/* Maximum permissible length of any HW Breakpoint */
#define HW_BREAKPOINT_LEN 0x8

extern int hw_breakpoint_slots(int type);
extern int arch_bp_generic_fields(int type, int *gen_bp_type);
extern int arch_check_bp_in_kernelspace(struct perf_event *bp);
extern int arch_validate_hwbkpt_settings(struct perf_event *bp);
extern int hw_breakpoint_exceptions_notify(struct notifier_block *unused,
						unsigned long val, void *data);
int arch_install_hw_breakpoint(struct perf_event *bp);
void arch_uninstall_hw_breakpoint(struct perf_event *bp);
void hw_breakpoint_pmu_read(struct perf_event *bp);
extern void flush_ptrace_hw_breakpoint(struct task_struct *tsk);

extern struct pmu perf_ops_bp;
extern void ptrace_triggered(struct perf_event *bp,
			struct perf_sample_data *data, struct pt_regs *regs);
static inline void hw_breakpoint_disable(void)
{
	set_dabr(0);
}
extern void thread_change_pc(struct task_struct *tsk, struct pt_regs *regs);

#else	/* CONFIG_HAVE_HW_BREAKPOINT */
static inline void hw_breakpoint_disable(void) { }
static inline void thread_change_pc(struct task_struct *tsk,
					struct pt_regs *regs) { }
#endif	/* CONFIG_HAVE_HW_BREAKPOINT */
#endif	/* __KERNEL__ */
#endif	/* _PPC_BOOK3S_64_HW_BREAKPOINT_H */
