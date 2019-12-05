/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * PowerPC BookIII S hardware breakpoint definitions
 *
 * Copyright 2010, IBM Corporation.
 * Author: K.Prasad <prasad@linux.vnet.ibm.com>
 */

#ifndef _PPC_BOOK3S_64_HW_BREAKPOINT_H
#define _PPC_BOOK3S_64_HW_BREAKPOINT_H

#ifdef	__KERNEL__
struct arch_hw_breakpoint {
	unsigned long	address;
	u16		type;
	u16		len; /* length of the target data symbol */
	u16		hw_len; /* length programmed in hw */
};

/* Note: Don't change the the first 6 bits below as they are in the same order
 * as the dabr and dabrx.
 */
#define HW_BRK_TYPE_READ		0x01
#define HW_BRK_TYPE_WRITE		0x02
#define HW_BRK_TYPE_TRANSLATE		0x04
#define HW_BRK_TYPE_USER		0x08
#define HW_BRK_TYPE_KERNEL		0x10
#define HW_BRK_TYPE_HYP			0x20
#define HW_BRK_TYPE_EXTRANEOUS_IRQ	0x80

/* bits that overlap with the bottom 3 bits of the dabr */
#define HW_BRK_TYPE_RDWR	(HW_BRK_TYPE_READ | HW_BRK_TYPE_WRITE)
#define HW_BRK_TYPE_DABR	(HW_BRK_TYPE_RDWR | HW_BRK_TYPE_TRANSLATE)
#define HW_BRK_TYPE_PRIV_ALL	(HW_BRK_TYPE_USER | HW_BRK_TYPE_KERNEL | \
				 HW_BRK_TYPE_HYP)

#define HW_BREAKPOINT_ALIGN 0x7

#define DABR_MAX_LEN	8
#define DAWR_MAX_LEN	512

#ifdef CONFIG_HAVE_HW_BREAKPOINT
#include <linux/kdebug.h>
#include <asm/reg.h>
#include <asm/debug.h>

struct perf_event_attr;
struct perf_event;
struct pmu;
struct perf_sample_data;
struct task_struct;

extern int hw_breakpoint_slots(int type);
extern int arch_bp_generic_fields(int type, int *gen_bp_type);
extern int arch_check_bp_in_kernelspace(struct arch_hw_breakpoint *hw);
extern int hw_breakpoint_arch_parse(struct perf_event *bp,
				    const struct perf_event_attr *attr,
				    struct arch_hw_breakpoint *hw);
extern int hw_breakpoint_exceptions_notify(struct notifier_block *unused,
						unsigned long val, void *data);
int arch_install_hw_breakpoint(struct perf_event *bp);
void arch_uninstall_hw_breakpoint(struct perf_event *bp);
void arch_unregister_hw_breakpoint(struct perf_event *bp);
void hw_breakpoint_pmu_read(struct perf_event *bp);
extern void flush_ptrace_hw_breakpoint(struct task_struct *tsk);

extern struct pmu perf_ops_bp;
extern void ptrace_triggered(struct perf_event *bp,
			struct perf_sample_data *data, struct pt_regs *regs);
static inline void hw_breakpoint_disable(void)
{
	struct arch_hw_breakpoint brk;

	brk.address = 0;
	brk.type = 0;
	brk.len = 0;
	brk.hw_len = 0;
	if (ppc_breakpoint_available())
		__set_breakpoint(&brk);
}
extern void thread_change_pc(struct task_struct *tsk, struct pt_regs *regs);
int hw_breakpoint_handler(struct die_args *args);

#else	/* CONFIG_HAVE_HW_BREAKPOINT */
static inline void hw_breakpoint_disable(void) { }
static inline void thread_change_pc(struct task_struct *tsk,
					struct pt_regs *regs) { }

#endif	/* CONFIG_HAVE_HW_BREAKPOINT */


#ifdef CONFIG_PPC_DAWR
extern bool dawr_force_enable;
static inline bool dawr_enabled(void)
{
	return dawr_force_enable;
}
int set_dawr(struct arch_hw_breakpoint *brk);
#else
static inline bool dawr_enabled(void) { return false; }
static inline int set_dawr(struct arch_hw_breakpoint *brk) { return -1; }
#endif

#endif	/* __KERNEL__ */
#endif	/* _PPC_BOOK3S_64_HW_BREAKPOINT_H */
