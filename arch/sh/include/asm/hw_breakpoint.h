#ifndef __ASM_SH_HW_BREAKPOINT_H
#define __ASM_SH_HW_BREAKPOINT_H

#include <linux/kdebug.h>
#include <linux/types.h>
#include <asm/ubc.h>

#ifdef __KERNEL__
#define __ARCH_HW_BREAKPOINT_H

struct arch_hw_breakpoint {
	char		*name; /* Contains name of the symbol to set bkpt */
	unsigned long	address;
	unsigned long	asid;
	u16		len;
	u16		type;
};

enum {
	SH_BREAKPOINT_READ	= (1 << 1),
	SH_BREAKPOINT_WRITE	= (1 << 2),
	SH_BREAKPOINT_RW	= SH_BREAKPOINT_READ | SH_BREAKPOINT_WRITE,

	SH_BREAKPOINT_LEN_1	= (1 << 12),
	SH_BREAKPOINT_LEN_2	= (1 << 13),
	SH_BREAKPOINT_LEN_4	= SH_BREAKPOINT_LEN_1 | SH_BREAKPOINT_LEN_2,
	SH_BREAKPOINT_LEN_8	= (1 << 14),
};

/* Total number of available UBC channels */
#define HBP_NUM		1 /* XXX */

struct perf_event;
struct task_struct;
struct pmu;

extern int arch_check_va_in_userspace(unsigned long va, u16 hbp_len);
extern int arch_validate_hwbkpt_settings(struct perf_event *bp,
					 struct task_struct *tsk);
extern int hw_breakpoint_exceptions_notify(struct notifier_block *unused,
					   unsigned long val, void *data);

int arch_install_hw_breakpoint(struct perf_event *bp);
void arch_uninstall_hw_breakpoint(struct perf_event *bp);
void hw_breakpoint_pmu_read(struct perf_event *bp);
void hw_breakpoint_pmu_unthrottle(struct perf_event *bp);

extern void arch_fill_perf_breakpoint(struct perf_event *bp);

extern struct pmu perf_ops_bp;

#endif /* __KERNEL__ */
#endif /* __ASM_SH_HW_BREAKPOINT_H */
