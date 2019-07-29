// SPDX-License-Identifier: GPL-2.0-only
/*
 * HW_breakpoint: a unified kernel/user-space hardware breakpoint facility,
 * using the CPU's debug registers.
 *
 * Copyright (C) 2012 ARM Limited
 * Author: Will Deacon <will.deacon@arm.com>
 */

#define pr_fmt(fmt) "hw-breakpoint: " fmt

#include <linux/compat.h>
#include <linux/cpu_pm.h>
#include <linux/errno.h>
#include <linux/hw_breakpoint.h>
#include <linux/kprobes.h>
#include <linux/perf_event.h>
#include <linux/ptrace.h>
#include <linux/smp.h>
#include <linux/uaccess.h>

#include <asm/current.h>
#include <asm/debug-monitors.h>
#include <asm/hw_breakpoint.h>
#include <asm/traps.h>
#include <asm/cputype.h>
#include <asm/system_misc.h>

/* Breakpoint currently in use for each BRP. */
static DEFINE_PER_CPU(struct perf_event *, bp_on_reg[ARM_MAX_BRP]);

/* Watchpoint currently in use for each WRP. */
static DEFINE_PER_CPU(struct perf_event *, wp_on_reg[ARM_MAX_WRP]);

/* Currently stepping a per-CPU kernel breakpoint. */
static DEFINE_PER_CPU(int, stepping_kernel_bp);

/* Number of BRP/WRP registers on this CPU. */
static int core_num_brps;
static int core_num_wrps;

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
		pr_warning("unknown slot type: %d\n", type);
		return 0;
	}
}

#define READ_WB_REG_CASE(OFF, N, REG, VAL)	\
	case (OFF + N):				\
		AARCH64_DBG_READ(N, REG, VAL);	\
		break

#define WRITE_WB_REG_CASE(OFF, N, REG, VAL)	\
	case (OFF + N):				\
		AARCH64_DBG_WRITE(N, REG, VAL);	\
		break

#define GEN_READ_WB_REG_CASES(OFF, REG, VAL)	\
	READ_WB_REG_CASE(OFF,  0, REG, VAL);	\
	READ_WB_REG_CASE(OFF,  1, REG, VAL);	\
	READ_WB_REG_CASE(OFF,  2, REG, VAL);	\
	READ_WB_REG_CASE(OFF,  3, REG, VAL);	\
	READ_WB_REG_CASE(OFF,  4, REG, VAL);	\
	READ_WB_REG_CASE(OFF,  5, REG, VAL);	\
	READ_WB_REG_CASE(OFF,  6, REG, VAL);	\
	READ_WB_REG_CASE(OFF,  7, REG, VAL);	\
	READ_WB_REG_CASE(OFF,  8, REG, VAL);	\
	READ_WB_REG_CASE(OFF,  9, REG, VAL);	\
	READ_WB_REG_CASE(OFF, 10, REG, VAL);	\
	READ_WB_REG_CASE(OFF, 11, REG, VAL);	\
	READ_WB_REG_CASE(OFF, 12, REG, VAL);	\
	READ_WB_REG_CASE(OFF, 13, REG, VAL);	\
	READ_WB_REG_CASE(OFF, 14, REG, VAL);	\
	READ_WB_REG_CASE(OFF, 15, REG, VAL)

#define GEN_WRITE_WB_REG_CASES(OFF, REG, VAL)	\
	WRITE_WB_REG_CASE(OFF,  0, REG, VAL);	\
	WRITE_WB_REG_CASE(OFF,  1, REG, VAL);	\
	WRITE_WB_REG_CASE(OFF,  2, REG, VAL);	\
	WRITE_WB_REG_CASE(OFF,  3, REG, VAL);	\
	WRITE_WB_REG_CASE(OFF,  4, REG, VAL);	\
	WRITE_WB_REG_CASE(OFF,  5, REG, VAL);	\
	WRITE_WB_REG_CASE(OFF,  6, REG, VAL);	\
	WRITE_WB_REG_CASE(OFF,  7, REG, VAL);	\
	WRITE_WB_REG_CASE(OFF,  8, REG, VAL);	\
	WRITE_WB_REG_CASE(OFF,  9, REG, VAL);	\
	WRITE_WB_REG_CASE(OFF, 10, REG, VAL);	\
	WRITE_WB_REG_CASE(OFF, 11, REG, VAL);	\
	WRITE_WB_REG_CASE(OFF, 12, REG, VAL);	\
	WRITE_WB_REG_CASE(OFF, 13, REG, VAL);	\
	WRITE_WB_REG_CASE(OFF, 14, REG, VAL);	\
	WRITE_WB_REG_CASE(OFF, 15, REG, VAL)

static u64 read_wb_reg(int reg, int n)
{
	u64 val = 0;

	switch (reg + n) {
	GEN_READ_WB_REG_CASES(AARCH64_DBG_REG_BVR, AARCH64_DBG_REG_NAME_BVR, val);
	GEN_READ_WB_REG_CASES(AARCH64_DBG_REG_BCR, AARCH64_DBG_REG_NAME_BCR, val);
	GEN_READ_WB_REG_CASES(AARCH64_DBG_REG_WVR, AARCH64_DBG_REG_NAME_WVR, val);
	GEN_READ_WB_REG_CASES(AARCH64_DBG_REG_WCR, AARCH64_DBG_REG_NAME_WCR, val);
	default:
		pr_warning("attempt to read from unknown breakpoint register %d\n", n);
	}

	return val;
}
NOKPROBE_SYMBOL(read_wb_reg);

static void write_wb_reg(int reg, int n, u64 val)
{
	switch (reg + n) {
	GEN_WRITE_WB_REG_CASES(AARCH64_DBG_REG_BVR, AARCH64_DBG_REG_NAME_BVR, val);
	GEN_WRITE_WB_REG_CASES(AARCH64_DBG_REG_BCR, AARCH64_DBG_REG_NAME_BCR, val);
	GEN_WRITE_WB_REG_CASES(AARCH64_DBG_REG_WVR, AARCH64_DBG_REG_NAME_WVR, val);
	GEN_WRITE_WB_REG_CASES(AARCH64_DBG_REG_WCR, AARCH64_DBG_REG_NAME_WCR, val);
	default:
		pr_warning("attempt to write to unknown breakpoint register %d\n", n);
	}
	isb();
}
NOKPROBE_SYMBOL(write_wb_reg);

/*
 * Convert a breakpoint privilege level to the corresponding exception
 * level.
 */
static enum dbg_active_el debug_exception_level(int privilege)
{
	switch (privilege) {
	case AARCH64_BREAKPOINT_EL0:
		return DBG_ACTIVE_EL0;
	case AARCH64_BREAKPOINT_EL1:
		return DBG_ACTIVE_EL1;
	default:
		pr_warning("invalid breakpoint privilege level %d\n", privilege);
		return -EINVAL;
	}
}
NOKPROBE_SYMBOL(debug_exception_level);

enum hw_breakpoint_ops {
	HW_BREAKPOINT_INSTALL,
	HW_BREAKPOINT_UNINSTALL,
	HW_BREAKPOINT_RESTORE
};

static int is_compat_bp(struct perf_event *bp)
{
	struct task_struct *tsk = bp->hw.target;

	/*
	 * tsk can be NULL for per-cpu (non-ptrace) breakpoints.
	 * In this case, use the native interface, since we don't have
	 * the notion of a "compat CPU" and could end up relying on
	 * deprecated behaviour if we use unaligned watchpoints in
	 * AArch64 state.
	 */
	return tsk && is_compat_thread(task_thread_info(tsk));
}

/**
 * hw_breakpoint_slot_setup - Find and setup a perf slot according to
 *			      operations
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
				    struct perf_event *bp,
				    enum hw_breakpoint_ops ops)
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
		case HW_BREAKPOINT_RESTORE:
			if (*slot == bp)
				return i;
			break;
		default:
			pr_warn_once("Unhandled hw breakpoint ops %d\n", ops);
			return -EINVAL;
		}
	}
	return -ENOSPC;
}

static int hw_breakpoint_control(struct perf_event *bp,
				 enum hw_breakpoint_ops ops)
{
	struct arch_hw_breakpoint *info = counter_arch_bp(bp);
	struct perf_event **slots;
	struct debug_info *debug_info = &current->thread.debug;
	int i, max_slots, ctrl_reg, val_reg, reg_enable;
	enum dbg_active_el dbg_el = debug_exception_level(info->ctrl.privilege);
	u32 ctrl;

	if (info->ctrl.type == ARM_BREAKPOINT_EXECUTE) {
		/* Breakpoint */
		ctrl_reg = AARCH64_DBG_REG_BCR;
		val_reg = AARCH64_DBG_REG_BVR;
		slots = this_cpu_ptr(bp_on_reg);
		max_slots = core_num_brps;
		reg_enable = !debug_info->bps_disabled;
	} else {
		/* Watchpoint */
		ctrl_reg = AARCH64_DBG_REG_WCR;
		val_reg = AARCH64_DBG_REG_WVR;
		slots = this_cpu_ptr(wp_on_reg);
		max_slots = core_num_wrps;
		reg_enable = !debug_info->wps_disabled;
	}

	i = hw_breakpoint_slot_setup(slots, max_slots, bp, ops);

	if (WARN_ONCE(i < 0, "Can't find any breakpoint slot"))
		return i;

	switch (ops) {
	case HW_BREAKPOINT_INSTALL:
		/*
		 * Ensure debug monitors are enabled at the correct exception
		 * level.
		 */
		enable_debug_monitors(dbg_el);
		/* Fall through */
	case HW_BREAKPOINT_RESTORE:
		/* Setup the address register. */
		write_wb_reg(val_reg, i, info->address);

		/* Setup the control register. */
		ctrl = encode_ctrl_reg(info->ctrl);
		write_wb_reg(ctrl_reg, i,
			     reg_enable ? ctrl | 0x1 : ctrl & ~0x1);
		break;
	case HW_BREAKPOINT_UNINSTALL:
		/* Reset the control register. */
		write_wb_reg(ctrl_reg, i, 0);

		/*
		 * Release the debug monitors for the correct exception
		 * level.
		 */
		disable_debug_monitors(dbg_el);
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
	case ARM_BREAKPOINT_LEN_1:
		len_in_bytes = 1;
		break;
	case ARM_BREAKPOINT_LEN_2:
		len_in_bytes = 2;
		break;
	case ARM_BREAKPOINT_LEN_3:
		len_in_bytes = 3;
		break;
	case ARM_BREAKPOINT_LEN_4:
		len_in_bytes = 4;
		break;
	case ARM_BREAKPOINT_LEN_5:
		len_in_bytes = 5;
		break;
	case ARM_BREAKPOINT_LEN_6:
		len_in_bytes = 6;
		break;
	case ARM_BREAKPOINT_LEN_7:
		len_in_bytes = 7;
		break;
	case ARM_BREAKPOINT_LEN_8:
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
			   int *gen_len, int *gen_type, int *offset)
{
	/* Type */
	switch (ctrl.type) {
	case ARM_BREAKPOINT_EXECUTE:
		*gen_type = HW_BREAKPOINT_X;
		break;
	case ARM_BREAKPOINT_LOAD:
		*gen_type = HW_BREAKPOINT_R;
		break;
	case ARM_BREAKPOINT_STORE:
		*gen_type = HW_BREAKPOINT_W;
		break;
	case ARM_BREAKPOINT_LOAD | ARM_BREAKPOINT_STORE:
		*gen_type = HW_BREAKPOINT_RW;
		break;
	default:
		return -EINVAL;
	}

	if (!ctrl.len)
		return -EINVAL;
	*offset = __ffs(ctrl.len);

	/* Len */
	switch (ctrl.len >> *offset) {
	case ARM_BREAKPOINT_LEN_1:
		*gen_len = HW_BREAKPOINT_LEN_1;
		break;
	case ARM_BREAKPOINT_LEN_2:
		*gen_len = HW_BREAKPOINT_LEN_2;
		break;
	case ARM_BREAKPOINT_LEN_3:
		*gen_len = HW_BREAKPOINT_LEN_3;
		break;
	case ARM_BREAKPOINT_LEN_4:
		*gen_len = HW_BREAKPOINT_LEN_4;
		break;
	case ARM_BREAKPOINT_LEN_5:
		*gen_len = HW_BREAKPOINT_LEN_5;
		break;
	case ARM_BREAKPOINT_LEN_6:
		*gen_len = HW_BREAKPOINT_LEN_6;
		break;
	case ARM_BREAKPOINT_LEN_7:
		*gen_len = HW_BREAKPOINT_LEN_7;
		break;
	case ARM_BREAKPOINT_LEN_8:
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
		hw->ctrl.type = ARM_BREAKPOINT_EXECUTE;
		break;
	case HW_BREAKPOINT_R:
		hw->ctrl.type = ARM_BREAKPOINT_LOAD;
		break;
	case HW_BREAKPOINT_W:
		hw->ctrl.type = ARM_BREAKPOINT_STORE;
		break;
	case HW_BREAKPOINT_RW:
		hw->ctrl.type = ARM_BREAKPOINT_LOAD | ARM_BREAKPOINT_STORE;
		break;
	default:
		return -EINVAL;
	}

	/* Len */
	switch (attr->bp_len) {
	case HW_BREAKPOINT_LEN_1:
		hw->ctrl.len = ARM_BREAKPOINT_LEN_1;
		break;
	case HW_BREAKPOINT_LEN_2:
		hw->ctrl.len = ARM_BREAKPOINT_LEN_2;
		break;
	case HW_BREAKPOINT_LEN_3:
		hw->ctrl.len = ARM_BREAKPOINT_LEN_3;
		break;
	case HW_BREAKPOINT_LEN_4:
		hw->ctrl.len = ARM_BREAKPOINT_LEN_4;
		break;
	case HW_BREAKPOINT_LEN_5:
		hw->ctrl.len = ARM_BREAKPOINT_LEN_5;
		break;
	case HW_BREAKPOINT_LEN_6:
		hw->ctrl.len = ARM_BREAKPOINT_LEN_6;
		break;
	case HW_BREAKPOINT_LEN_7:
		hw->ctrl.len = ARM_BREAKPOINT_LEN_7;
		break;
	case HW_BREAKPOINT_LEN_8:
		hw->ctrl.len = ARM_BREAKPOINT_LEN_8;
		break;
	default:
		return -EINVAL;
	}

	/*
	 * On AArch64, we only permit breakpoints of length 4, whereas
	 * AArch32 also requires breakpoints of length 2 for Thumb.
	 * Watchpoints can be of length 1, 2, 4 or 8 bytes.
	 */
	if (hw->ctrl.type == ARM_BREAKPOINT_EXECUTE) {
		if (is_compat_bp(bp)) {
			if (hw->ctrl.len != ARM_BREAKPOINT_LEN_2 &&
			    hw->ctrl.len != ARM_BREAKPOINT_LEN_4)
				return -EINVAL;
		} else if (hw->ctrl.len != ARM_BREAKPOINT_LEN_4) {
			/*
			 * FIXME: Some tools (I'm looking at you perf) assume
			 *	  that breakpoints should be sizeof(long). This
			 *	  is nonsense. For now, we fix up the parameter
			 *	  but we should probably return -EINVAL instead.
			 */
			hw->ctrl.len = ARM_BREAKPOINT_LEN_4;
		}
	}

	/* Address */
	hw->address = attr->bp_addr;

	/*
	 * Privilege
	 * Note that we disallow combined EL0/EL1 breakpoints because
	 * that would complicate the stepping code.
	 */
	if (arch_check_bp_in_kernelspace(hw))
		hw->ctrl.privilege = AARCH64_BREAKPOINT_EL1;
	else
		hw->ctrl.privilege = AARCH64_BREAKPOINT_EL0;

	/* Enabled? */
	hw->ctrl.enabled = !attr->disabled;

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
	u64 alignment_mask, offset;

	/* Build the arch_hw_breakpoint. */
	ret = arch_build_bp_info(bp, attr, hw);
	if (ret)
		return ret;

	/*
	 * Check address alignment.
	 * We don't do any clever alignment correction for watchpoints
	 * because using 64-bit unaligned addresses is deprecated for
	 * AArch64.
	 *
	 * AArch32 tasks expect some simple alignment fixups, so emulate
	 * that here.
	 */
	if (is_compat_bp(bp)) {
		if (hw->ctrl.len == ARM_BREAKPOINT_LEN_8)
			alignment_mask = 0x7;
		else
			alignment_mask = 0x3;
		offset = hw->address & alignment_mask;
		switch (offset) {
		case 0:
			/* Aligned */
			break;
		case 1:
		case 2:
			/* Allow halfword watchpoints and breakpoints. */
			if (hw->ctrl.len == ARM_BREAKPOINT_LEN_2)
				break;
		case 3:
			/* Allow single byte watchpoint. */
			if (hw->ctrl.len == ARM_BREAKPOINT_LEN_1)
				break;
		default:
			return -EINVAL;
		}
	} else {
		if (hw->ctrl.type == ARM_BREAKPOINT_EXECUTE)
			alignment_mask = 0x3;
		else
			alignment_mask = 0x7;
		offset = hw->address & alignment_mask;
	}

	hw->address &= ~alignment_mask;
	hw->ctrl.len <<= offset;

	/*
	 * Disallow per-task kernel breakpoints since these would
	 * complicate the stepping code.
	 */
	if (hw->ctrl.privilege == AARCH64_BREAKPOINT_EL1 && bp->hw.target)
		return -EINVAL;

	return 0;
}

/*
 * Enable/disable all of the breakpoints active at the specified
 * exception level at the register level.
 * This is used when single-stepping after a breakpoint exception.
 */
static void toggle_bp_registers(int reg, enum dbg_active_el el, int enable)
{
	int i, max_slots, privilege;
	u32 ctrl;
	struct perf_event **slots;

	switch (reg) {
	case AARCH64_DBG_REG_BCR:
		slots = this_cpu_ptr(bp_on_reg);
		max_slots = core_num_brps;
		break;
	case AARCH64_DBG_REG_WCR:
		slots = this_cpu_ptr(wp_on_reg);
		max_slots = core_num_wrps;
		break;
	default:
		return;
	}

	for (i = 0; i < max_slots; ++i) {
		if (!slots[i])
			continue;

		privilege = counter_arch_bp(slots[i])->ctrl.privilege;
		if (debug_exception_level(privilege) != el)
			continue;

		ctrl = read_wb_reg(reg, i);
		if (enable)
			ctrl |= 0x1;
		else
			ctrl &= ~0x1;
		write_wb_reg(reg, i, ctrl);
	}
}
NOKPROBE_SYMBOL(toggle_bp_registers);

/*
 * Debug exception handlers.
 */
static int breakpoint_handler(unsigned long unused, unsigned int esr,
			      struct pt_regs *regs)
{
	int i, step = 0, *kernel_step;
	u32 ctrl_reg;
	u64 addr, val;
	struct perf_event *bp, **slots;
	struct debug_info *debug_info;
	struct arch_hw_breakpoint_ctrl ctrl;

	slots = this_cpu_ptr(bp_on_reg);
	addr = instruction_pointer(regs);
	debug_info = &current->thread.debug;

	for (i = 0; i < core_num_brps; ++i) {
		rcu_read_lock();

		bp = slots[i];

		if (bp == NULL)
			goto unlock;

		/* Check if the breakpoint value matches. */
		val = read_wb_reg(AARCH64_DBG_REG_BVR, i);
		if (val != (addr & ~0x3))
			goto unlock;

		/* Possible match, check the byte address select to confirm. */
		ctrl_reg = read_wb_reg(AARCH64_DBG_REG_BCR, i);
		decode_ctrl_reg(ctrl_reg, &ctrl);
		if (!((1 << (addr & 0x3)) & ctrl.len))
			goto unlock;

		counter_arch_bp(bp)->trigger = addr;
		perf_bp_event(bp, regs);

		/* Do we need to handle the stepping? */
		if (is_default_overflow_handler(bp))
			step = 1;
unlock:
		rcu_read_unlock();
	}

	if (!step)
		return 0;

	if (user_mode(regs)) {
		debug_info->bps_disabled = 1;
		toggle_bp_registers(AARCH64_DBG_REG_BCR, DBG_ACTIVE_EL0, 0);

		/* If we're already stepping a watchpoint, just return. */
		if (debug_info->wps_disabled)
			return 0;

		if (test_thread_flag(TIF_SINGLESTEP))
			debug_info->suspended_step = 1;
		else
			user_enable_single_step(current);
	} else {
		toggle_bp_registers(AARCH64_DBG_REG_BCR, DBG_ACTIVE_EL1, 0);
		kernel_step = this_cpu_ptr(&stepping_kernel_bp);

		if (*kernel_step != ARM_KERNEL_STEP_NONE)
			return 0;

		if (kernel_active_single_step()) {
			*kernel_step = ARM_KERNEL_STEP_SUSPEND;
		} else {
			*kernel_step = ARM_KERNEL_STEP_ACTIVE;
			kernel_enable_single_step(regs);
		}
	}

	return 0;
}
NOKPROBE_SYMBOL(breakpoint_handler);

/*
 * Arm64 hardware does not always report a watchpoint hit address that matches
 * one of the watchpoints set. It can also report an address "near" the
 * watchpoint if a single instruction access both watched and unwatched
 * addresses. There is no straight-forward way, short of disassembling the
 * offending instruction, to map that address back to the watchpoint. This
 * function computes the distance of the memory access from the watchpoint as a
 * heuristic for the likelyhood that a given access triggered the watchpoint.
 *
 * See Section D2.10.5 "Determining the memory location that caused a Watchpoint
 * exception" of ARMv8 Architecture Reference Manual for details.
 *
 * The function returns the distance of the address from the bytes watched by
 * the watchpoint. In case of an exact match, it returns 0.
 */
static u64 get_distance_from_watchpoint(unsigned long addr, u64 val,
					struct arch_hw_breakpoint_ctrl *ctrl)
{
	u64 wp_low, wp_high;
	u32 lens, lene;

	addr = untagged_addr(addr);

	lens = __ffs(ctrl->len);
	lene = __fls(ctrl->len);

	wp_low = val + lens;
	wp_high = val + lene;
	if (addr < wp_low)
		return wp_low - addr;
	else if (addr > wp_high)
		return addr - wp_high;
	else
		return 0;
}

static int watchpoint_handler(unsigned long addr, unsigned int esr,
			      struct pt_regs *regs)
{
	int i, step = 0, *kernel_step, access, closest_match = 0;
	u64 min_dist = -1, dist;
	u32 ctrl_reg;
	u64 val;
	struct perf_event *wp, **slots;
	struct debug_info *debug_info;
	struct arch_hw_breakpoint *info;
	struct arch_hw_breakpoint_ctrl ctrl;

	slots = this_cpu_ptr(wp_on_reg);
	debug_info = &current->thread.debug;

	/*
	 * Find all watchpoints that match the reported address. If no exact
	 * match is found. Attribute the hit to the closest watchpoint.
	 */
	rcu_read_lock();
	for (i = 0; i < core_num_wrps; ++i) {
		wp = slots[i];
		if (wp == NULL)
			continue;

		/*
		 * Check that the access type matches.
		 * 0 => load, otherwise => store
		 */
		access = (esr & AARCH64_ESR_ACCESS_MASK) ? HW_BREAKPOINT_W :
			 HW_BREAKPOINT_R;
		if (!(access & hw_breakpoint_type(wp)))
			continue;

		/* Check if the watchpoint value and byte select match. */
		val = read_wb_reg(AARCH64_DBG_REG_WVR, i);
		ctrl_reg = read_wb_reg(AARCH64_DBG_REG_WCR, i);
		decode_ctrl_reg(ctrl_reg, &ctrl);
		dist = get_distance_from_watchpoint(addr, val, &ctrl);
		if (dist < min_dist) {
			min_dist = dist;
			closest_match = i;
		}
		/* Is this an exact match? */
		if (dist != 0)
			continue;

		info = counter_arch_bp(wp);
		info->trigger = addr;
		perf_bp_event(wp, regs);

		/* Do we need to handle the stepping? */
		if (is_default_overflow_handler(wp))
			step = 1;
	}
	if (min_dist > 0 && min_dist != -1) {
		/* No exact match found. */
		wp = slots[closest_match];
		info = counter_arch_bp(wp);
		info->trigger = addr;
		perf_bp_event(wp, regs);

		/* Do we need to handle the stepping? */
		if (is_default_overflow_handler(wp))
			step = 1;
	}
	rcu_read_unlock();

	if (!step)
		return 0;

	/*
	 * We always disable EL0 watchpoints because the kernel can
	 * cause these to fire via an unprivileged access.
	 */
	toggle_bp_registers(AARCH64_DBG_REG_WCR, DBG_ACTIVE_EL0, 0);

	if (user_mode(regs)) {
		debug_info->wps_disabled = 1;

		/* If we're already stepping a breakpoint, just return. */
		if (debug_info->bps_disabled)
			return 0;

		if (test_thread_flag(TIF_SINGLESTEP))
			debug_info->suspended_step = 1;
		else
			user_enable_single_step(current);
	} else {
		toggle_bp_registers(AARCH64_DBG_REG_WCR, DBG_ACTIVE_EL1, 0);
		kernel_step = this_cpu_ptr(&stepping_kernel_bp);

		if (*kernel_step != ARM_KERNEL_STEP_NONE)
			return 0;

		if (kernel_active_single_step()) {
			*kernel_step = ARM_KERNEL_STEP_SUSPEND;
		} else {
			*kernel_step = ARM_KERNEL_STEP_ACTIVE;
			kernel_enable_single_step(regs);
		}
	}

	return 0;
}
NOKPROBE_SYMBOL(watchpoint_handler);

/*
 * Handle single-step exception.
 */
int reinstall_suspended_bps(struct pt_regs *regs)
{
	struct debug_info *debug_info = &current->thread.debug;
	int handled_exception = 0, *kernel_step;

	kernel_step = this_cpu_ptr(&stepping_kernel_bp);

	/*
	 * Called from single-step exception handler.
	 * Return 0 if execution can resume, 1 if a SIGTRAP should be
	 * reported.
	 */
	if (user_mode(regs)) {
		if (debug_info->bps_disabled) {
			debug_info->bps_disabled = 0;
			toggle_bp_registers(AARCH64_DBG_REG_BCR, DBG_ACTIVE_EL0, 1);
			handled_exception = 1;
		}

		if (debug_info->wps_disabled) {
			debug_info->wps_disabled = 0;
			toggle_bp_registers(AARCH64_DBG_REG_WCR, DBG_ACTIVE_EL0, 1);
			handled_exception = 1;
		}

		if (handled_exception) {
			if (debug_info->suspended_step) {
				debug_info->suspended_step = 0;
				/* Allow exception handling to fall-through. */
				handled_exception = 0;
			} else {
				user_disable_single_step(current);
			}
		}
	} else if (*kernel_step != ARM_KERNEL_STEP_NONE) {
		toggle_bp_registers(AARCH64_DBG_REG_BCR, DBG_ACTIVE_EL1, 1);
		toggle_bp_registers(AARCH64_DBG_REG_WCR, DBG_ACTIVE_EL1, 1);

		if (!debug_info->wps_disabled)
			toggle_bp_registers(AARCH64_DBG_REG_WCR, DBG_ACTIVE_EL0, 1);

		if (*kernel_step != ARM_KERNEL_STEP_SUSPEND) {
			kernel_disable_single_step();
			handled_exception = 1;
		} else {
			handled_exception = 0;
		}

		*kernel_step = ARM_KERNEL_STEP_NONE;
	}

	return !handled_exception;
}
NOKPROBE_SYMBOL(reinstall_suspended_bps);

/*
 * Context-switcher for restoring suspended breakpoints.
 */
void hw_breakpoint_thread_switch(struct task_struct *next)
{
	/*
	 *           current        next
	 * disabled: 0              0     => The usual case, NOTIFY_DONE
	 *           0              1     => Disable the registers
	 *           1              0     => Enable the registers
	 *           1              1     => NOTIFY_DONE. per-task bps will
	 *                                   get taken care of by perf.
	 */

	struct debug_info *current_debug_info, *next_debug_info;

	current_debug_info = &current->thread.debug;
	next_debug_info = &next->thread.debug;

	/* Update breakpoints. */
	if (current_debug_info->bps_disabled != next_debug_info->bps_disabled)
		toggle_bp_registers(AARCH64_DBG_REG_BCR,
				    DBG_ACTIVE_EL0,
				    !next_debug_info->bps_disabled);

	/* Update watchpoints. */
	if (current_debug_info->wps_disabled != next_debug_info->wps_disabled)
		toggle_bp_registers(AARCH64_DBG_REG_WCR,
				    DBG_ACTIVE_EL0,
				    !next_debug_info->wps_disabled);
}

/*
 * CPU initialisation.
 */
static int hw_breakpoint_reset(unsigned int cpu)
{
	int i;
	struct perf_event **slots;
	/*
	 * When a CPU goes through cold-boot, it does not have any installed
	 * slot, so it is safe to share the same function for restoring and
	 * resetting breakpoints; when a CPU is hotplugged in, it goes
	 * through the slots, which are all empty, hence it just resets control
	 * and value for debug registers.
	 * When this function is triggered on warm-boot through a CPU PM
	 * notifier some slots might be initialized; if so they are
	 * reprogrammed according to the debug slots content.
	 */
	for (slots = this_cpu_ptr(bp_on_reg), i = 0; i < core_num_brps; ++i) {
		if (slots[i]) {
			hw_breakpoint_control(slots[i], HW_BREAKPOINT_RESTORE);
		} else {
			write_wb_reg(AARCH64_DBG_REG_BCR, i, 0UL);
			write_wb_reg(AARCH64_DBG_REG_BVR, i, 0UL);
		}
	}

	for (slots = this_cpu_ptr(wp_on_reg), i = 0; i < core_num_wrps; ++i) {
		if (slots[i]) {
			hw_breakpoint_control(slots[i], HW_BREAKPOINT_RESTORE);
		} else {
			write_wb_reg(AARCH64_DBG_REG_WCR, i, 0UL);
			write_wb_reg(AARCH64_DBG_REG_WVR, i, 0UL);
		}
	}

	return 0;
}

#ifdef CONFIG_CPU_PM
extern void cpu_suspend_set_dbg_restorer(int (*hw_bp_restore)(unsigned int));
#else
static inline void cpu_suspend_set_dbg_restorer(int (*hw_bp_restore)(unsigned int))
{
}
#endif

/*
 * One-time initialisation.
 */
static int __init arch_hw_breakpoint_init(void)
{
	int ret;

	core_num_brps = get_num_brps();
	core_num_wrps = get_num_wrps();

	pr_info("found %d breakpoint and %d watchpoint registers.\n",
		core_num_brps, core_num_wrps);

	/* Register debug fault handlers. */
	hook_debug_fault_code(DBG_ESR_EVT_HWBP, breakpoint_handler, SIGTRAP,
			      TRAP_HWBKPT, "hw-breakpoint handler");
	hook_debug_fault_code(DBG_ESR_EVT_HWWP, watchpoint_handler, SIGTRAP,
			      TRAP_HWBKPT, "hw-watchpoint handler");

	/*
	 * Reset the breakpoint resources. We assume that a halting
	 * debugger will leave the world in a nice state for us.
	 */
	ret = cpuhp_setup_state(CPUHP_AP_PERF_ARM_HW_BREAKPOINT_STARTING,
			  "perf/arm64/hw_breakpoint:starting",
			  hw_breakpoint_reset, NULL);
	if (ret)
		pr_err("failed to register CPU hotplug notifier: %d\n", ret);

	/* Register cpu_suspend hw breakpoint restore hook */
	cpu_suspend_set_dbg_restorer(hw_breakpoint_reset);

	return ret;
}
arch_initcall(arch_hw_breakpoint_init);

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
