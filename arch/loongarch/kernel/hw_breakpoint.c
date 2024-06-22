// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2022-2023 Loongson Technology Corporation Limited
 */
#define pr_fmt(fmt) "hw-breakpoint: " fmt

#include <linux/hw_breakpoint.h>
#include <linux/kprobes.h>
#include <linux/perf_event.h>

#include <asm/hw_breakpoint.h>

/* Breakpoint currently in use for each BRP. */
static DEFINE_PER_CPU(struct perf_event *, bp_on_reg[LOONGARCH_MAX_BRP]);

/* Watchpoint currently in use for each WRP. */
static DEFINE_PER_CPU(struct perf_event *, wp_on_reg[LOONGARCH_MAX_WRP]);

int hw_breakpoint_slots(int type)
{
	/*
	 * We can be called early, so don't rely on
	 * our static variables being initialised.
	 */
	switch (type) {
	case TYPE_INST:
		return get_num_brps();
	case TYPE_DATA:
		return get_num_wrps();
	default:
		pr_warn("unknown slot type: %d\n", type);
		return 0;
	}
}

#define READ_WB_REG_CASE(OFF, N, REG, T, VAL)		\
	case (OFF + N):					\
		LOONGARCH_CSR_WATCH_READ(N, REG, T, VAL);	\
		break

#define WRITE_WB_REG_CASE(OFF, N, REG, T, VAL)		\
	case (OFF + N):					\
		LOONGARCH_CSR_WATCH_WRITE(N, REG, T, VAL);	\
		break

#define GEN_READ_WB_REG_CASES(OFF, REG, T, VAL)		\
	READ_WB_REG_CASE(OFF, 0, REG, T, VAL);		\
	READ_WB_REG_CASE(OFF, 1, REG, T, VAL);		\
	READ_WB_REG_CASE(OFF, 2, REG, T, VAL);		\
	READ_WB_REG_CASE(OFF, 3, REG, T, VAL);		\
	READ_WB_REG_CASE(OFF, 4, REG, T, VAL);		\
	READ_WB_REG_CASE(OFF, 5, REG, T, VAL);		\
	READ_WB_REG_CASE(OFF, 6, REG, T, VAL);		\
	READ_WB_REG_CASE(OFF, 7, REG, T, VAL);

#define GEN_WRITE_WB_REG_CASES(OFF, REG, T, VAL)	\
	WRITE_WB_REG_CASE(OFF, 0, REG, T, VAL);		\
	WRITE_WB_REG_CASE(OFF, 1, REG, T, VAL);		\
	WRITE_WB_REG_CASE(OFF, 2, REG, T, VAL);		\
	WRITE_WB_REG_CASE(OFF, 3, REG, T, VAL);		\
	WRITE_WB_REG_CASE(OFF, 4, REG, T, VAL);		\
	WRITE_WB_REG_CASE(OFF, 5, REG, T, VAL);		\
	WRITE_WB_REG_CASE(OFF, 6, REG, T, VAL);		\
	WRITE_WB_REG_CASE(OFF, 7, REG, T, VAL);

static u64 read_wb_reg(int reg, int n, int t)
{
	u64 val = 0;

	switch (reg + n) {
	GEN_READ_WB_REG_CASES(CSR_CFG_ADDR, ADDR, t, val);
	GEN_READ_WB_REG_CASES(CSR_CFG_MASK, MASK, t, val);
	GEN_READ_WB_REG_CASES(CSR_CFG_CTRL, CTRL, t, val);
	GEN_READ_WB_REG_CASES(CSR_CFG_ASID, ASID, t, val);
	default:
		pr_warn("Attempt to read from unknown breakpoint register %d\n", n);
	}

	return val;
}
NOKPROBE_SYMBOL(read_wb_reg);

static void write_wb_reg(int reg, int n, int t, u64 val)
{
	switch (reg + n) {
	GEN_WRITE_WB_REG_CASES(CSR_CFG_ADDR, ADDR, t, val);
	GEN_WRITE_WB_REG_CASES(CSR_CFG_MASK, MASK, t, val);
	GEN_WRITE_WB_REG_CASES(CSR_CFG_CTRL, CTRL, t, val);
	GEN_WRITE_WB_REG_CASES(CSR_CFG_ASID, ASID, t, val);
	default:
		pr_warn("Attempt to write to unknown breakpoint register %d\n", n);
	}
}
NOKPROBE_SYMBOL(write_wb_reg);

enum hw_breakpoint_ops {
	HW_BREAKPOINT_INSTALL,
	HW_BREAKPOINT_UNINSTALL,
};

/*
 * hw_breakpoint_slot_setup - Find and setup a perf slot according to operations
 *
 * @slots: pointer to array of slots
 * @max_slots: max number of slots
 * @bp: perf_event to setup
 * @ops: operation to be carried out on the slot
 *
 * Return:
 *	slot index on success
 *	-ENOSPC if no slot is available/matches
 *	-EINVAL on wrong operations parameter
 */

static int hw_breakpoint_slot_setup(struct perf_event **slots, int max_slots,
				    struct perf_event *bp, enum hw_breakpoint_ops ops)
{
	int i;
	struct perf_event **slot;

	for (i = 0; i < max_slots; ++i) {
		slot = &slots[i];
		switch (ops) {
		case HW_BREAKPOINT_INSTALL:
			if (!*slot) {
				*slot = bp;
				return i;
			}
			break;
		case HW_BREAKPOINT_UNINSTALL:
			if (*slot == bp) {
				*slot = NULL;
				return i;
			}
			break;
		default:
			pr_warn_once("Unhandled hw breakpoint ops %d\n", ops);
			return -EINVAL;
		}
	}

	return -ENOSPC;
}

void ptrace_hw_copy_thread(struct task_struct *tsk)
{
	memset(tsk->thread.hbp_break, 0, sizeof(tsk->thread.hbp_break));
	memset(tsk->thread.hbp_watch, 0, sizeof(tsk->thread.hbp_watch));
}

/*
 * Unregister breakpoints from this task and reset the pointers in the thread_struct.
 */
void flush_ptrace_hw_breakpoint(struct task_struct *tsk)
{
	int i;
	struct thread_struct *t = &tsk->thread;

	for (i = 0; i < LOONGARCH_MAX_BRP; i++) {
		if (t->hbp_break[i]) {
			unregister_hw_breakpoint(t->hbp_break[i]);
			t->hbp_break[i] = NULL;
		}
	}

	for (i = 0; i < LOONGARCH_MAX_WRP; i++) {
		if (t->hbp_watch[i]) {
			unregister_hw_breakpoint(t->hbp_watch[i]);
			t->hbp_watch[i] = NULL;
		}
	}
}

static int hw_breakpoint_control(struct perf_event *bp,
				 enum hw_breakpoint_ops ops)
{
	u32 ctrl, privilege;
	int i, max_slots, enable;
	struct pt_regs *regs;
	struct perf_event **slots;
	struct arch_hw_breakpoint *info = counter_arch_bp(bp);

	if (arch_check_bp_in_kernelspace(info))
		privilege = CTRL_PLV0_ENABLE;
	else
		privilege = CTRL_PLV3_ENABLE;

	/*  Whether bp belongs to a task. */
	if (bp->hw.target)
		regs = task_pt_regs(bp->hw.target);

	if (info->ctrl.type == LOONGARCH_BREAKPOINT_EXECUTE) {
		/* Breakpoint */
		slots = this_cpu_ptr(bp_on_reg);
		max_slots = boot_cpu_data.watch_ireg_count;
	} else {
		/* Watchpoint */
		slots = this_cpu_ptr(wp_on_reg);
		max_slots = boot_cpu_data.watch_dreg_count;
	}

	i = hw_breakpoint_slot_setup(slots, max_slots, bp, ops);

	if (WARN_ONCE(i < 0, "Can't find any breakpoint slot"))
		return i;

	switch (ops) {
	case HW_BREAKPOINT_INSTALL:
		/* Set the FWPnCFG/MWPnCFG 1~4 register. */
		if (info->ctrl.type == LOONGARCH_BREAKPOINT_EXECUTE) {
			write_wb_reg(CSR_CFG_ADDR, i, 0, info->address);
			write_wb_reg(CSR_CFG_MASK, i, 0, info->mask);
			write_wb_reg(CSR_CFG_ASID, i, 0, 0);
			write_wb_reg(CSR_CFG_CTRL, i, 0, privilege);
		} else {
			write_wb_reg(CSR_CFG_ADDR, i, 1, info->address);
			write_wb_reg(CSR_CFG_MASK, i, 1, info->mask);
			write_wb_reg(CSR_CFG_ASID, i, 1, 0);
			ctrl = encode_ctrl_reg(info->ctrl);
			write_wb_reg(CSR_CFG_CTRL, i, 1, ctrl | privilege);
		}
		enable = csr_read64(LOONGARCH_CSR_CRMD);
		csr_write64(CSR_CRMD_WE | enable, LOONGARCH_CSR_CRMD);
		if (bp->hw.target)
			regs->csr_prmd |= CSR_PRMD_PWE;
		break;
	case HW_BREAKPOINT_UNINSTALL:
		/* Reset the FWPnCFG/MWPnCFG 1~4 register. */
		if (info->ctrl.type == LOONGARCH_BREAKPOINT_EXECUTE) {
			write_wb_reg(CSR_CFG_ADDR, i, 0, 0);
			write_wb_reg(CSR_CFG_MASK, i, 0, 0);
			write_wb_reg(CSR_CFG_CTRL, i, 0, 0);
			write_wb_reg(CSR_CFG_ASID, i, 0, 0);
		} else {
			write_wb_reg(CSR_CFG_ADDR, i, 1, 0);
			write_wb_reg(CSR_CFG_MASK, i, 1, 0);
			write_wb_reg(CSR_CFG_CTRL, i, 1, 0);
			write_wb_reg(CSR_CFG_ASID, i, 1, 0);
		}
		if (bp->hw.target)
			regs->csr_prmd &= ~CSR_PRMD_PWE;
		break;
	}

	return 0;
}

/*
 * Install a perf counter breakpoint.
 */
int arch_install_hw_breakpoint(struct perf_event *bp)
{
	return hw_breakpoint_control(bp, HW_BREAKPOINT_INSTALL);
}

void arch_uninstall_hw_breakpoint(struct perf_event *bp)
{
	hw_breakpoint_control(bp, HW_BREAKPOINT_UNINSTALL);
}

static int get_hbp_len(u8 hbp_len)
{
	unsigned int len_in_bytes = 0;

	switch (hbp_len) {
	case LOONGARCH_BREAKPOINT_LEN_1:
		len_in_bytes = 1;
		break;
	case LOONGARCH_BREAKPOINT_LEN_2:
		len_in_bytes = 2;
		break;
	case LOONGARCH_BREAKPOINT_LEN_4:
		len_in_bytes = 4;
		break;
	case LOONGARCH_BREAKPOINT_LEN_8:
		len_in_bytes = 8;
		break;
	}

	return len_in_bytes;
}

/*
 * Check whether bp virtual address is in kernel space.
 */
int arch_check_bp_in_kernelspace(struct arch_hw_breakpoint *hw)
{
	unsigned int len;
	unsigned long va;

	va = hw->address;
	len = get_hbp_len(hw->ctrl.len);

	return (va >= TASK_SIZE) && ((va + len - 1) >= TASK_SIZE);
}

/*
 * Extract generic type and length encodings from an arch_hw_breakpoint_ctrl.
 * Hopefully this will disappear when ptrace can bypass the conversion
 * to generic breakpoint descriptions.
 */
int arch_bp_generic_fields(struct arch_hw_breakpoint_ctrl ctrl,
			   int *gen_len, int *gen_type)
{
	/* Type */
	switch (ctrl.type) {
	case LOONGARCH_BREAKPOINT_EXECUTE:
		*gen_type = HW_BREAKPOINT_X;
		break;
	case LOONGARCH_BREAKPOINT_LOAD:
		*gen_type = HW_BREAKPOINT_R;
		break;
	case LOONGARCH_BREAKPOINT_STORE:
		*gen_type = HW_BREAKPOINT_W;
		break;
	case LOONGARCH_BREAKPOINT_LOAD | LOONGARCH_BREAKPOINT_STORE:
		*gen_type = HW_BREAKPOINT_RW;
		break;
	default:
		return -EINVAL;
	}

	/* Len */
	switch (ctrl.len) {
	case LOONGARCH_BREAKPOINT_LEN_1:
		*gen_len = HW_BREAKPOINT_LEN_1;
		break;
	case LOONGARCH_BREAKPOINT_LEN_2:
		*gen_len = HW_BREAKPOINT_LEN_2;
		break;
	case LOONGARCH_BREAKPOINT_LEN_4:
		*gen_len = HW_BREAKPOINT_LEN_4;
		break;
	case LOONGARCH_BREAKPOINT_LEN_8:
		*gen_len = HW_BREAKPOINT_LEN_8;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

/*
 * Construct an arch_hw_breakpoint from a perf_event.
 */
static int arch_build_bp_info(struct perf_event *bp,
			      const struct perf_event_attr *attr,
			      struct arch_hw_breakpoint *hw)
{
	/* Type */
	switch (attr->bp_type) {
	case HW_BREAKPOINT_X:
		hw->ctrl.type = LOONGARCH_BREAKPOINT_EXECUTE;
		break;
	case HW_BREAKPOINT_R:
		hw->ctrl.type = LOONGARCH_BREAKPOINT_LOAD;
		break;
	case HW_BREAKPOINT_W:
		hw->ctrl.type = LOONGARCH_BREAKPOINT_STORE;
		break;
	case HW_BREAKPOINT_RW:
		hw->ctrl.type = LOONGARCH_BREAKPOINT_LOAD | LOONGARCH_BREAKPOINT_STORE;
		break;
	default:
		return -EINVAL;
	}

	/* Len */
	switch (attr->bp_len) {
	case HW_BREAKPOINT_LEN_1:
		hw->ctrl.len = LOONGARCH_BREAKPOINT_LEN_1;
		break;
	case HW_BREAKPOINT_LEN_2:
		hw->ctrl.len = LOONGARCH_BREAKPOINT_LEN_2;
		break;
	case HW_BREAKPOINT_LEN_4:
		hw->ctrl.len = LOONGARCH_BREAKPOINT_LEN_4;
		break;
	case HW_BREAKPOINT_LEN_8:
		hw->ctrl.len = LOONGARCH_BREAKPOINT_LEN_8;
		break;
	default:
		return -EINVAL;
	}

	/* Address */
	hw->address = attr->bp_addr;

	return 0;
}

/*
 * Validate the arch-specific HW Breakpoint register settings.
 */
int hw_breakpoint_arch_parse(struct perf_event *bp,
			     const struct perf_event_attr *attr,
			     struct arch_hw_breakpoint *hw)
{
	int ret;
	u64 alignment_mask;

	/* Build the arch_hw_breakpoint. */
	ret = arch_build_bp_info(bp, attr, hw);
	if (ret)
		return ret;

	if (hw->ctrl.type == LOONGARCH_BREAKPOINT_EXECUTE) {
		alignment_mask = 0x3;
		hw->address &= ~alignment_mask;
	}

	return 0;
}

static void update_bp_registers(struct pt_regs *regs, int enable, int type)
{
	u32 ctrl;
	int i, max_slots;
	struct perf_event **slots;
	struct arch_hw_breakpoint *info;

	switch (type) {
	case 0:
		slots = this_cpu_ptr(bp_on_reg);
		max_slots = boot_cpu_data.watch_ireg_count;
		break;
	case 1:
		slots = this_cpu_ptr(wp_on_reg);
		max_slots = boot_cpu_data.watch_dreg_count;
		break;
	default:
		return;
	}

	for (i = 0; i < max_slots; ++i) {
		if (!slots[i])
			continue;

		info = counter_arch_bp(slots[i]);
		if (enable) {
			if ((info->ctrl.type == LOONGARCH_BREAKPOINT_EXECUTE) && (type == 0)) {
				write_wb_reg(CSR_CFG_CTRL, i, 0, CTRL_PLV_ENABLE);
				write_wb_reg(CSR_CFG_CTRL, i, 0, CTRL_PLV_ENABLE);
			} else {
				ctrl = read_wb_reg(CSR_CFG_CTRL, i, 1);
				if (info->ctrl.type == LOONGARCH_BREAKPOINT_LOAD)
					ctrl |= 0x1 << MWPnCFG3_LoadEn;
				if (info->ctrl.type == LOONGARCH_BREAKPOINT_STORE)
					ctrl |= 0x1 << MWPnCFG3_StoreEn;
				write_wb_reg(CSR_CFG_CTRL, i, 1, ctrl);
			}
			regs->csr_prmd |= CSR_PRMD_PWE;
		} else {
			if ((info->ctrl.type == LOONGARCH_BREAKPOINT_EXECUTE) && (type == 0)) {
				write_wb_reg(CSR_CFG_CTRL, i, 0, 0);
			} else {
				ctrl = read_wb_reg(CSR_CFG_CTRL, i, 1);
				if (info->ctrl.type == LOONGARCH_BREAKPOINT_LOAD)
					ctrl &= ~0x1 << MWPnCFG3_LoadEn;
				if (info->ctrl.type == LOONGARCH_BREAKPOINT_STORE)
					ctrl &= ~0x1 << MWPnCFG3_StoreEn;
				write_wb_reg(CSR_CFG_CTRL, i, 1, ctrl);
			}
			regs->csr_prmd &= ~CSR_PRMD_PWE;
		}
	}
}
NOKPROBE_SYMBOL(update_bp_registers);

/*
 * Debug exception handlers.
 */
void breakpoint_handler(struct pt_regs *regs)
{
	int i;
	struct perf_event *bp, **slots;

	slots = this_cpu_ptr(bp_on_reg);

	for (i = 0; i < boot_cpu_data.watch_ireg_count; ++i) {
		if ((csr_read32(LOONGARCH_CSR_FWPS) & (0x1 << i))) {
			bp = slots[i];
			if (bp == NULL)
				continue;
			perf_bp_event(bp, regs);
			csr_write32(0x1 << i, LOONGARCH_CSR_FWPS);
			update_bp_registers(regs, 0, 0);
		}
	}
}
NOKPROBE_SYMBOL(breakpoint_handler);

void watchpoint_handler(struct pt_regs *regs)
{
	int i;
	struct perf_event *wp, **slots;

	slots = this_cpu_ptr(wp_on_reg);

	for (i = 0; i < boot_cpu_data.watch_dreg_count; ++i) {
		if ((csr_read32(LOONGARCH_CSR_MWPS) & (0x1 << i))) {
			wp = slots[i];
			if (wp == NULL)
				continue;
			perf_bp_event(wp, regs);
			csr_write32(0x1 << i, LOONGARCH_CSR_MWPS);
			update_bp_registers(regs, 0, 1);
		}
	}
}
NOKPROBE_SYMBOL(watchpoint_handler);

static int __init arch_hw_breakpoint_init(void)
{
	int cpu;

	boot_cpu_data.watch_ireg_count = get_num_brps();
	boot_cpu_data.watch_dreg_count = get_num_wrps();

	pr_info("Found %d breakpoint and %d watchpoint registers.\n",
		boot_cpu_data.watch_ireg_count, boot_cpu_data.watch_dreg_count);

	for (cpu = 1; cpu < NR_CPUS; cpu++) {
		cpu_data[cpu].watch_ireg_count = boot_cpu_data.watch_ireg_count;
		cpu_data[cpu].watch_dreg_count = boot_cpu_data.watch_dreg_count;
	}

	return 0;
}
arch_initcall(arch_hw_breakpoint_init);

void hw_breakpoint_thread_switch(struct task_struct *next)
{
	u64 addr, mask;
	struct pt_regs *regs = task_pt_regs(next);

	if (test_tsk_thread_flag(next, TIF_SINGLESTEP)) {
		addr = read_wb_reg(CSR_CFG_ADDR, 0, 0);
		mask = read_wb_reg(CSR_CFG_MASK, 0, 0);
		if (!((regs->csr_era ^ addr) & ~mask))
			csr_write32(CSR_FWPC_SKIP, LOONGARCH_CSR_FWPS);
		regs->csr_prmd |= CSR_PRMD_PWE;
	} else {
		/* Update breakpoints */
		update_bp_registers(regs, 1, 0);
		/* Update watchpoints */
		update_bp_registers(regs, 1, 1);
	}
}

void hw_breakpoint_pmu_read(struct perf_event *bp)
{
}

/*
 * Dummy function to register with die_notifier.
 */
int hw_breakpoint_exceptions_notify(struct notifier_block *unused,
				    unsigned long val, void *data)
{
	return NOTIFY_DONE;
}
