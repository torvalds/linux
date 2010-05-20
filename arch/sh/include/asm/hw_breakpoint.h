#ifndef __ASM_SH_HW_BREAKPOINT_H
#define __ASM_SH_HW_BREAKPOINT_H

#ifdef __KERNEL__
#define __ARCH_HW_BREAKPOINT_H

#include <linux/kdebug.h>
#include <linux/types.h>

struct arch_hw_breakpoint {
	char		*name; /* Contains name of the symbol to set bkpt */
	unsigned long	address;
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

struct sh_ubc {
	const char	*name;
	unsigned int	num_events;
	unsigned int	trap_nr;
	void		(*enable)(struct arch_hw_breakpoint *, int);
	void		(*disable)(struct arch_hw_breakpoint *, int);
	void		(*enable_all)(unsigned long);
	void		(*disable_all)(void);
	unsigned long	(*active_mask)(void);
	unsigned long	(*triggered_mask)(void);
	void		(*clear_triggered_mask)(unsigned long);
	struct clk	*clk;	/* optional interface clock / MSTP bit */
};

struct perf_event;
struct task_struct;
struct pmu;

/* Maximum number of UBC channels */
#define HBP_NUM		2

static inline int hw_breakpoint_slots(int type)
{
	return HBP_NUM;
}

/* arch/sh/kernel/hw_breakpoint.c */
extern int arch_check_bp_in_kernelspace(struct perf_event *bp);
extern int arch_validate_hwbkpt_settings(struct perf_event *bp);
extern int hw_breakpoint_exceptions_notify(struct notifier_block *unused,
					   unsigned long val, void *data);

int arch_install_hw_breakpoint(struct perf_event *bp);
void arch_uninstall_hw_breakpoint(struct perf_event *bp);
void hw_breakpoint_pmu_read(struct perf_event *bp);

extern void arch_fill_perf_breakpoint(struct perf_event *bp);
extern int register_sh_ubc(struct sh_ubc *);

extern struct pmu perf_ops_bp;

#endif /* __KERNEL__ */
#endif /* __ASM_SH_HW_BREAKPOINT_H */
