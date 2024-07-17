/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2022-2023 Loongson Technology Corporation Limited
 */
#ifndef __ASM_HW_BREAKPOINT_H
#define __ASM_HW_BREAKPOINT_H

#include <asm/loongarch.h>

#ifdef __KERNEL__

/* Breakpoint */
#define LOONGARCH_BREAKPOINT_EXECUTE		(0 << 0)

/* Watchpoints */
#define LOONGARCH_BREAKPOINT_LOAD		(1 << 0)
#define LOONGARCH_BREAKPOINT_STORE		(1 << 1)

struct arch_hw_breakpoint_ctrl {
	u32 __reserved	: 28,
	len		: 2,
	type		: 2;
};

struct arch_hw_breakpoint {
	u64 address;
	u64 mask;
	struct arch_hw_breakpoint_ctrl ctrl;
};

/* Lengths */
#define LOONGARCH_BREAKPOINT_LEN_1    0b11
#define LOONGARCH_BREAKPOINT_LEN_2    0b10
#define LOONGARCH_BREAKPOINT_LEN_4    0b01
#define LOONGARCH_BREAKPOINT_LEN_8    0b00

/*
 * Limits.
 * Changing these will require modifications to the register accessors.
 */
#define LOONGARCH_MAX_BRP		8
#define LOONGARCH_MAX_WRP		8

/* Virtual debug register bases. */
#define CSR_CFG_ADDR	0
#define CSR_CFG_MASK	(CSR_CFG_ADDR + LOONGARCH_MAX_BRP)
#define CSR_CFG_CTRL	(CSR_CFG_MASK + LOONGARCH_MAX_BRP)
#define CSR_CFG_ASID	(CSR_CFG_CTRL + LOONGARCH_MAX_WRP)

/* Debug register names. */
#define LOONGARCH_CSR_NAME_ADDR	ADDR
#define LOONGARCH_CSR_NAME_MASK	MASK
#define LOONGARCH_CSR_NAME_CTRL	CTRL
#define LOONGARCH_CSR_NAME_ASID	ASID

/* Accessor macros for the debug registers. */
#define LOONGARCH_CSR_WATCH_READ(N, REG, T, VAL)			\
do {								\
	if (T == 0)						\
		VAL = csr_read64(LOONGARCH_CSR_##IB##N##REG);	\
	else							\
		VAL = csr_read64(LOONGARCH_CSR_##DB##N##REG);	\
} while (0)

#define LOONGARCH_CSR_WATCH_WRITE(N, REG, T, VAL)			\
do {								\
	if (T == 0)						\
		csr_write64(VAL, LOONGARCH_CSR_##IB##N##REG);	\
	else							\
		csr_write64(VAL, LOONGARCH_CSR_##DB##N##REG);	\
} while (0)

/* Exact number */
#define CSR_FWPC_NUM		0x3f
#define CSR_MWPC_NUM		0x3f

#define CTRL_PLV_ENABLE		0x1e
#define CTRL_PLV0_ENABLE	0x02
#define CTRL_PLV3_ENABLE	0x10

#define MWPnCFG3_LoadEn		8
#define MWPnCFG3_StoreEn	9

#define MWPnCFG3_Type_mask	0x3
#define MWPnCFG3_Size_mask	0x3

static inline u32 encode_ctrl_reg(struct arch_hw_breakpoint_ctrl ctrl)
{
	return (ctrl.len << 10) | (ctrl.type << 8);
}

static inline void decode_ctrl_reg(u32 reg, struct arch_hw_breakpoint_ctrl *ctrl)
{
	reg >>= 8;
	ctrl->type = reg & MWPnCFG3_Type_mask;
	reg >>= 2;
	ctrl->len  = reg & MWPnCFG3_Size_mask;
}

struct task_struct;
struct notifier_block;
struct perf_event;
struct perf_event_attr;

extern int arch_bp_generic_fields(struct arch_hw_breakpoint_ctrl ctrl,
				  int *gen_len, int *gen_type);
extern int arch_check_bp_in_kernelspace(struct arch_hw_breakpoint *hw);
extern int hw_breakpoint_arch_parse(struct perf_event *bp,
				    const struct perf_event_attr *attr,
				    struct arch_hw_breakpoint *hw);
extern int hw_breakpoint_exceptions_notify(struct notifier_block *unused,
					   unsigned long val, void *data);

extern int arch_install_hw_breakpoint(struct perf_event *bp);
extern void arch_uninstall_hw_breakpoint(struct perf_event *bp);
extern int hw_breakpoint_slots(int type);
extern void hw_breakpoint_pmu_read(struct perf_event *bp);

void breakpoint_handler(struct pt_regs *regs);
void watchpoint_handler(struct pt_regs *regs);

#ifdef CONFIG_HAVE_HW_BREAKPOINT
extern void ptrace_hw_copy_thread(struct task_struct *task);
extern void hw_breakpoint_thread_switch(struct task_struct *next);
#else
static inline void ptrace_hw_copy_thread(struct task_struct *task)
{
}
static inline void hw_breakpoint_thread_switch(struct task_struct *next)
{
}
#endif

/* Determine number of BRP registers available. */
static inline int get_num_brps(void)
{
	return csr_read64(LOONGARCH_CSR_FWPC) & CSR_FWPC_NUM;
}

/* Determine number of WRP registers available. */
static inline int get_num_wrps(void)
{
	return csr_read64(LOONGARCH_CSR_MWPC) & CSR_MWPC_NUM;
}

#endif	/* __KERNEL__ */
#endif	/* __ASM_BREAKPOINT_H */
