/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
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
 * Copyright (C) 2009, 2010 ARM Limited
 *
 * Author: Will Deacon <will.deacon@arm.com>
 */

/*
 * HW_breakpoint: a unified kernel/user-space hardware breakpoint facility,
 * using the CPU's debug registers.
 */
#define pr_fmt(fmt) "hw-breakpoint: " fmt

#include <linux/errno.h>
#include <linux/hardirq.h>
#include <linux/perf_event.h>
#include <linux/hw_breakpoint.h>
#include <linux/smp.h>

#include <asm/cacheflush.h>
#include <asm/cputype.h>
#include <asm/current.h>
#include <asm/hw_breakpoint.h>
#include <asm/kdebug.h>
#include <asm/system.h>
#include <asm/traps.h>

/* Breakpoint currently in use for each BRP. */
static DEFINE_PER_CPU(struct perf_event *, bp_on_reg[ARM_MAX_BRP]);

/* Watchpoint currently in use for each WRP. */
static DEFINE_PER_CPU(struct perf_event *, wp_on_reg[ARM_MAX_WRP]);

/* Number of BRP/WRP registers on this CPU. */
static int core_num_brps;
static int core_num_reserved_brps;
static int core_num_wrps;

/* Debug architecture version. */
static u8 debug_arch;

/* Maximum supported watchpoint length. */
static u8 max_watchpoint_len;

#define READ_WB_REG_CASE(OP2, M, VAL)		\
	case ((OP2 << 4) + M):			\
		ARM_DBG_READ(c ## M, OP2, VAL); \
		break

#define WRITE_WB_REG_CASE(OP2, M, VAL)		\
	case ((OP2 << 4) + M):			\
		ARM_DBG_WRITE(c ## M, OP2, VAL);\
		break

#define GEN_READ_WB_REG_CASES(OP2, VAL)		\
	READ_WB_REG_CASE(OP2, 0, VAL);		\
	READ_WB_REG_CASE(OP2, 1, VAL);		\
	READ_WB_REG_CASE(OP2, 2, VAL);		\
	READ_WB_REG_CASE(OP2, 3, VAL);		\
	READ_WB_REG_CASE(OP2, 4, VAL);		\
	READ_WB_REG_CASE(OP2, 5, VAL);		\
	READ_WB_REG_CASE(OP2, 6, VAL);		\
	READ_WB_REG_CASE(OP2, 7, VAL);		\
	READ_WB_REG_CASE(OP2, 8, VAL);		\
	READ_WB_REG_CASE(OP2, 9, VAL);		\
	READ_WB_REG_CASE(OP2, 10, VAL);		\
	READ_WB_REG_CASE(OP2, 11, VAL);		\
	READ_WB_REG_CASE(OP2, 12, VAL);		\
	READ_WB_REG_CASE(OP2, 13, VAL);		\
	READ_WB_REG_CASE(OP2, 14, VAL);		\
	READ_WB_REG_CASE(OP2, 15, VAL)

#define GEN_WRITE_WB_REG_CASES(OP2, VAL)	\
	WRITE_WB_REG_CASE(OP2, 0, VAL);		\
	WRITE_WB_REG_CASE(OP2, 1, VAL);		\
	WRITE_WB_REG_CASE(OP2, 2, VAL);		\
	WRITE_WB_REG_CASE(OP2, 3, VAL);		\
	WRITE_WB_REG_CASE(OP2, 4, VAL);		\
	WRITE_WB_REG_CASE(OP2, 5, VAL);		\
	WRITE_WB_REG_CASE(OP2, 6, VAL);		\
	WRITE_WB_REG_CASE(OP2, 7, VAL);		\
	WRITE_WB_REG_CASE(OP2, 8, VAL);		\
	WRITE_WB_REG_CASE(OP2, 9, VAL);		\
	WRITE_WB_REG_CASE(OP2, 10, VAL);	\
	WRITE_WB_REG_CASE(OP2, 11, VAL);	\
	WRITE_WB_REG_CASE(OP2, 12, VAL);	\
	WRITE_WB_REG_CASE(OP2, 13, VAL);	\
	WRITE_WB_REG_CASE(OP2, 14, VAL);	\
	WRITE_WB_REG_CASE(OP2, 15, VAL)

static u32 read_wb_reg(int n)
{
	u32 val = 0;

	switch (n) {
	GEN_READ_WB_REG_CASES(ARM_OP2_BVR, val);
	GEN_READ_WB_REG_CASES(ARM_OP2_BCR, val);
	GEN_READ_WB_REG_CASES(ARM_OP2_WVR, val);
	GEN_READ_WB_REG_CASES(ARM_OP2_WCR, val);
	default:
		pr_warning("attempt to read from unknown breakpoint "
				"register %d\n", n);
	}

	return val;
}

static void write_wb_reg(int n, u32 val)
{
	switch (n) {
	GEN_WRITE_WB_REG_CASES(ARM_OP2_BVR, val);
	GEN_WRITE_WB_REG_CASES(ARM_OP2_BCR, val);
	GEN_WRITE_WB_REG_CASES(ARM_OP2_WVR, val);
	GEN_WRITE_WB_REG_CASES(ARM_OP2_WCR, val);
	default:
		pr_warning("attempt to write to unknown breakpoint "
				"register %d\n", n);
	}
	isb();
}

/* Determine debug architecture. */
static u8 get_debug_arch(void)
{
	u32 didr;

	/* Do we implement the extended CPUID interface? */
	if (WARN_ONCE((((read_cpuid_id() >> 16) & 0xf) != 0xf),
	    "CPUID feature registers not supported. "
	    "Assuming v6 debug is present.\n"))
		return ARM_DEBUG_ARCH_V6;

	ARM_DBG_READ(c0, 0, didr);
	return (didr >> 16) & 0xf;
}

u8 arch_get_debug_arch(void)
{
	return debug_arch;
}

static int debug_arch_supported(void)
{
	u8 arch = get_debug_arch();
	return arch >= ARM_DEBUG_ARCH_V6 && arch <= ARM_DEBUG_ARCH_V7_ECP14;
}

/* Determine number of BRP register available. */
static int get_num_brp_resources(void)
{
	u32 didr;
	ARM_DBG_READ(c0, 0, didr);
	return ((didr >> 24) & 0xf) + 1;
}

/* Does this core support mismatch breakpoints? */
static int core_has_mismatch_brps(void)
{
	return (get_debug_arch() >= ARM_DEBUG_ARCH_V7_ECP14 &&
		get_num_brp_resources() > 1);
}

/* Determine number of usable WRPs available. */
static int get_num_wrps(void)
{
	/*
	 * FIXME: When a watchpoint fires, the only way to work out which
	 * watchpoint it was is by disassembling the faulting instruction
	 * and working out the address of the memory access.
	 *
	 * Furthermore, we can only do this if the watchpoint was precise
	 * since imprecise watchpoints prevent us from calculating register
	 * based addresses.
	 *
	 * Providing we have more than 1 breakpoint register, we only report
	 * a single watchpoint register for the time being. This way, we always
	 * know which watchpoint fired. In the future we can either add a
	 * disassembler and address generation emulator, or we can insert a
	 * check to see if the DFAR is set on watchpoint exception entry
	 * [the ARM ARM states that the DFAR is UNKNOWN, but experience shows
	 * that it is set on some implementations].
	 */

#if 0
	int wrps;
	u32 didr;
	ARM_DBG_READ(c0, 0, didr);
	wrps = ((didr >> 28) & 0xf) + 1;
#endif
	int wrps = 1;

	if (core_has_mismatch_brps() && wrps >= get_num_brp_resources())
		wrps = get_num_brp_resources() - 1;

	return wrps;
}

/* We reserve one breakpoint for each watchpoint. */
static int get_num_reserved_brps(void)
{
	if (core_has_mismatch_brps())
		return get_num_wrps();
	return 0;
}

/* Determine number of usable BRPs available. */
static int get_num_brps(void)
{
	int brps = get_num_brp_resources();
	if (core_has_mismatch_brps())
		brps -= get_num_reserved_brps();
	return brps;
}

/*
 * In order to access the breakpoint/watchpoint control registers,
 * we must be running in debug monitor mode. Unfortunately, we can
 * be put into halting debug mode at any time by an external debugger
 * but there is nothing we can do to prevent that.
 */
static int enable_monitor_mode(void)
{
	u32 dscr;
	int ret = 0;

	ARM_DBG_READ(c1, 0, dscr);

	/* Ensure that halting mode is disabled. */
	if (WARN_ONCE(dscr & ARM_DSCR_HDBGEN,
			"halting debug mode enabled. Unable to access hardware resources.\n")) {
		ret = -EPERM;
		goto out;
	}

	/* If monitor mode is already enabled, just return. */
	if (dscr & ARM_DSCR_MDBGEN)
		goto out;

	/* Write to the corresponding DSCR. */
	switch (get_debug_arch()) {
	case ARM_DEBUG_ARCH_V6:
	case ARM_DEBUG_ARCH_V6_1:
		ARM_DBG_WRITE(c1, 0, (dscr | ARM_DSCR_MDBGEN));
		break;
	case ARM_DEBUG_ARCH_V7_ECP14:
		ARM_DBG_WRITE(c2, 2, (dscr | ARM_DSCR_MDBGEN));
		break;
	default:
		ret = -ENODEV;
		goto out;
	}

	/* Check that the write made it through. */
	ARM_DBG_READ(c1, 0, dscr);
	if (!(dscr & ARM_DSCR_MDBGEN))
		ret = -EPERM;

out:
	return ret;
}

int hw_breakpoint_slots(int type)
{
	if (!debug_arch_supported())
		return 0;

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

/*
 * Check if 8-bit byte-address select is available.
 * This clobbers WRP 0.
 */
static u8 get_max_wp_len(void)
{
	u32 ctrl_reg;
	struct arch_hw_breakpoint_ctrl ctrl;
	u8 size = 4;

	if (debug_arch < ARM_DEBUG_ARCH_V7_ECP14)
		goto out;

	memset(&ctrl, 0, sizeof(ctrl));
	ctrl.len = ARM_BREAKPOINT_LEN_8;
	ctrl_reg = encode_ctrl_reg(ctrl);

	write_wb_reg(ARM_BASE_WVR, 0);
	write_wb_reg(ARM_BASE_WCR, ctrl_reg);
	if ((read_wb_reg(ARM_BASE_WCR) & ctrl_reg) == ctrl_reg)
		size = 8;

out:
	return size;
}

u8 arch_get_max_wp_len(void)
{
	return max_watchpoint_len;
}

/*
 * Install a perf counter breakpoint.
 */
int arch_install_hw_breakpoint(struct perf_event *bp)
{
	struct arch_hw_breakpoint *info = counter_arch_bp(bp);
	struct perf_event **slot, **slots;
	int i, max_slots, ctrl_base, val_base, ret = 0;
	u32 addr, ctrl;

	/* Ensure that we are in monitor mode and halting mode is disabled. */
	ret = enable_monitor_mode();
	if (ret)
		goto out;

	addr = info->address;
	ctrl = encode_ctrl_reg(info->ctrl) | 0x1;

	if (info->ctrl.type == ARM_BREAKPOINT_EXECUTE) {
		/* Breakpoint */
		ctrl_base = ARM_BASE_BCR;
		val_base = ARM_BASE_BVR;
		slots = (struct perf_event **)__get_cpu_var(bp_on_reg);
		max_slots = core_num_brps;
		if (info->step_ctrl.enabled) {
			/* Override the breakpoint data with the step data. */
			addr = info->trigger & ~0x3;
			ctrl = encode_ctrl_reg(info->step_ctrl);
		}
	} else {
		/* Watchpoint */
		if (info->step_ctrl.enabled) {
			/* Install into the reserved breakpoint region. */
			ctrl_base = ARM_BASE_BCR + core_num_brps;
			val_base = ARM_BASE_BVR + core_num_brps;
			/* Override the watchpoint data with the step data. */
			addr = info->trigger & ~0x3;
			ctrl = encode_ctrl_reg(info->step_ctrl);
		} else {
			ctrl_base = ARM_BASE_WCR;
			val_base = ARM_BASE_WVR;
		}
		slots = (struct perf_event **)__get_cpu_var(wp_on_reg);
		max_slots = core_num_wrps;
	}

	for (i = 0; i < max_slots; ++i) {
		slot = &slots[i];

		if (!*slot) {
			*slot = bp;
			break;
		}
	}

	if (WARN_ONCE(i == max_slots, "Can't find any breakpoint slot\n")) {
		ret = -EBUSY;
		goto out;
	}

	/* Setup the address register. */
	write_wb_reg(val_base + i, addr);

	/* Setup the control register. */
	write_wb_reg(ctrl_base + i, ctrl);

out:
	return ret;
}

void arch_uninstall_hw_breakpoint(struct perf_event *bp)
{
	struct arch_hw_breakpoint *info = counter_arch_bp(bp);
	struct perf_event **slot, **slots;
	int i, max_slots, base;

	if (info->ctrl.type == ARM_BREAKPOINT_EXECUTE) {
		/* Breakpoint */
		base = ARM_BASE_BCR;
		slots = (struct perf_event **)__get_cpu_var(bp_on_reg);
		max_slots = core_num_brps;
	} else {
		/* Watchpoint */
		if (info->step_ctrl.enabled)
			base = ARM_BASE_BCR + core_num_brps;
		else
			base = ARM_BASE_WCR;
		slots = (struct perf_event **)__get_cpu_var(wp_on_reg);
		max_slots = core_num_wrps;
	}

	/* Remove the breakpoint. */
	for (i = 0; i < max_slots; ++i) {
		slot = &slots[i];

		if (*slot == bp) {
			*slot = NULL;
			break;
		}
	}

	if (WARN_ONCE(i == max_slots, "Can't find any breakpoint slot\n"))
		return;

	/* Reset the control register. */
	write_wb_reg(base + i, 0);
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
	case ARM_BREAKPOINT_LEN_4:
		len_in_bytes = 4;
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
int arch_check_bp_in_kernelspace(struct perf_event *bp)
{
	unsigned int len;
	unsigned long va;
	struct arch_hw_breakpoint *info = counter_arch_bp(bp);

	va = info->address;
	len = get_hbp_len(info->ctrl.len);

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

	/* Len */
	switch (ctrl.len) {
	case ARM_BREAKPOINT_LEN_1:
		*gen_len = HW_BREAKPOINT_LEN_1;
		break;
	case ARM_BREAKPOINT_LEN_2:
		*gen_len = HW_BREAKPOINT_LEN_2;
		break;
	case ARM_BREAKPOINT_LEN_4:
		*gen_len = HW_BREAKPOINT_LEN_4;
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
static int arch_build_bp_info(struct perf_event *bp)
{
	struct arch_hw_breakpoint *info = counter_arch_bp(bp);

	/* Type */
	switch (bp->attr.bp_type) {
	case HW_BREAKPOINT_X:
		info->ctrl.type = ARM_BREAKPOINT_EXECUTE;
		break;
	case HW_BREAKPOINT_R:
		info->ctrl.type = ARM_BREAKPOINT_LOAD;
		break;
	case HW_BREAKPOINT_W:
		info->ctrl.type = ARM_BREAKPOINT_STORE;
		break;
	case HW_BREAKPOINT_RW:
		info->ctrl.type = ARM_BREAKPOINT_LOAD | ARM_BREAKPOINT_STORE;
		break;
	default:
		return -EINVAL;
	}

	/* Len */
	switch (bp->attr.bp_len) {
	case HW_BREAKPOINT_LEN_1:
		info->ctrl.len = ARM_BREAKPOINT_LEN_1;
		break;
	case HW_BREAKPOINT_LEN_2:
		info->ctrl.len = ARM_BREAKPOINT_LEN_2;
		break;
	case HW_BREAKPOINT_LEN_4:
		info->ctrl.len = ARM_BREAKPOINT_LEN_4;
		break;
	case HW_BREAKPOINT_LEN_8:
		info->ctrl.len = ARM_BREAKPOINT_LEN_8;
		if ((info->ctrl.type != ARM_BREAKPOINT_EXECUTE)
			&& max_watchpoint_len >= 8)
			break;
	default:
		return -EINVAL;
	}

	/*
	 * Breakpoints must be of length 2 (thumb) or 4 (ARM) bytes.
	 * Watchpoints can be of length 1, 2, 4 or 8 bytes if supported
	 * by the hardware and must be aligned to the appropriate number of
	 * bytes.
	 */
	if (info->ctrl.type == ARM_BREAKPOINT_EXECUTE &&
	    info->ctrl.len != ARM_BREAKPOINT_LEN_2 &&
	    info->ctrl.len != ARM_BREAKPOINT_LEN_4)
		return -EINVAL;

	/* Address */
	info->address = bp->attr.bp_addr;

	/* Privilege */
	info->ctrl.privilege = ARM_BREAKPOINT_USER;
	if (arch_check_bp_in_kernelspace(bp))
		info->ctrl.privilege |= ARM_BREAKPOINT_PRIV;

	/* Enabled? */
	info->ctrl.enabled = !bp->attr.disabled;

	/* Mismatch */
	info->ctrl.mismatch = 0;

	return 0;
}

/*
 * Validate the arch-specific HW Breakpoint register settings.
 */
int arch_validate_hwbkpt_settings(struct perf_event *bp)
{
	struct arch_hw_breakpoint *info = counter_arch_bp(bp);
	int ret = 0;
	u32 offset, alignment_mask = 0x3;

	/* Build the arch_hw_breakpoint. */
	ret = arch_build_bp_info(bp);
	if (ret)
		goto out;

	/* Check address alignment. */
	if (info->ctrl.len == ARM_BREAKPOINT_LEN_8)
		alignment_mask = 0x7;
	offset = info->address & alignment_mask;
	switch (offset) {
	case 0:
		/* Aligned */
		break;
	case 1:
		/* Allow single byte watchpoint. */
		if (info->ctrl.len == ARM_BREAKPOINT_LEN_1)
			break;
	case 2:
		/* Allow halfword watchpoints and breakpoints. */
		if (info->ctrl.len == ARM_BREAKPOINT_LEN_2)
			break;
	default:
		ret = -EINVAL;
		goto out;
	}

	info->address &= ~alignment_mask;
	info->ctrl.len <<= offset;

	/*
	 * Currently we rely on an overflow handler to take
	 * care of single-stepping the breakpoint when it fires.
	 * In the case of userspace breakpoints on a core with V7 debug,
	 * we can use the mismatch feature as a poor-man's hardware
	 * single-step, but this only works for per-task breakpoints.
	 */
	if (WARN_ONCE(!bp->overflow_handler &&
		(arch_check_bp_in_kernelspace(bp) || !core_has_mismatch_brps()
		 || !bp->hw.bp_target),
			"overflow handler required but none found\n")) {
		ret = -EINVAL;
	}
out:
	return ret;
}

/*
 * Enable/disable single-stepping over the breakpoint bp at address addr.
 */
static void enable_single_step(struct perf_event *bp, u32 addr)
{
	struct arch_hw_breakpoint *info = counter_arch_bp(bp);

	arch_uninstall_hw_breakpoint(bp);
	info->step_ctrl.mismatch  = 1;
	info->step_ctrl.len	  = ARM_BREAKPOINT_LEN_4;
	info->step_ctrl.type	  = ARM_BREAKPOINT_EXECUTE;
	info->step_ctrl.privilege = info->ctrl.privilege;
	info->step_ctrl.enabled	  = 1;
	info->trigger		  = addr;
	arch_install_hw_breakpoint(bp);
}

static void disable_single_step(struct perf_event *bp)
{
	arch_uninstall_hw_breakpoint(bp);
	counter_arch_bp(bp)->step_ctrl.enabled = 0;
	arch_install_hw_breakpoint(bp);
}

static void watchpoint_handler(unsigned long unknown, struct pt_regs *regs)
{
	int i;
	struct perf_event *wp, **slots;
	struct arch_hw_breakpoint *info;

	slots = (struct perf_event **)__get_cpu_var(wp_on_reg);

	/* Without a disassembler, we can only handle 1 watchpoint. */
	BUG_ON(core_num_wrps > 1);

	for (i = 0; i < core_num_wrps; ++i) {
		rcu_read_lock();

		wp = slots[i];

		if (wp == NULL) {
			rcu_read_unlock();
			continue;
		}

		/*
		 * The DFAR is an unknown value. Since we only allow a
		 * single watchpoint, we can set the trigger to the lowest
		 * possible faulting address.
		 */
		info = counter_arch_bp(wp);
		info->trigger = wp->attr.bp_addr;
		pr_debug("watchpoint fired: address = 0x%x\n", info->trigger);
		perf_bp_event(wp, regs);

		/*
		 * If no overflow handler is present, insert a temporary
		 * mismatch breakpoint so we can single-step over the
		 * watchpoint trigger.
		 */
		if (!wp->overflow_handler)
			enable_single_step(wp, instruction_pointer(regs));

		rcu_read_unlock();
	}
}

static void watchpoint_single_step_handler(unsigned long pc)
{
	int i;
	struct perf_event *wp, **slots;
	struct arch_hw_breakpoint *info;

	slots = (struct perf_event **)__get_cpu_var(wp_on_reg);

	for (i = 0; i < core_num_reserved_brps; ++i) {
		rcu_read_lock();

		wp = slots[i];

		if (wp == NULL)
			goto unlock;

		info = counter_arch_bp(wp);
		if (!info->step_ctrl.enabled)
			goto unlock;

		/*
		 * Restore the original watchpoint if we've completed the
		 * single-step.
		 */
		if (info->trigger != pc)
			disable_single_step(wp);

unlock:
		rcu_read_unlock();
	}
}

static void breakpoint_handler(unsigned long unknown, struct pt_regs *regs)
{
	int i;
	u32 ctrl_reg, val, addr;
	struct perf_event *bp, **slots;
	struct arch_hw_breakpoint *info;
	struct arch_hw_breakpoint_ctrl ctrl;

	slots = (struct perf_event **)__get_cpu_var(bp_on_reg);

	/* The exception entry code places the amended lr in the PC. */
	addr = regs->ARM_pc;

	/* Check the currently installed breakpoints first. */
	for (i = 0; i < core_num_brps; ++i) {
		rcu_read_lock();

		bp = slots[i];

		if (bp == NULL)
			goto unlock;

		info = counter_arch_bp(bp);

		/* Check if the breakpoint value matches. */
		val = read_wb_reg(ARM_BASE_BVR + i);
		if (val != (addr & ~0x3))
			goto mismatch;

		/* Possible match, check the byte address select to confirm. */
		ctrl_reg = read_wb_reg(ARM_BASE_BCR + i);
		decode_ctrl_reg(ctrl_reg, &ctrl);
		if ((1 << (addr & 0x3)) & ctrl.len) {
			info->trigger = addr;
			pr_debug("breakpoint fired: address = 0x%x\n", addr);
			perf_bp_event(bp, regs);
			if (!bp->overflow_handler)
				enable_single_step(bp, addr);
			goto unlock;
		}

mismatch:
		/* If we're stepping a breakpoint, it can now be restored. */
		if (info->step_ctrl.enabled)
			disable_single_step(bp);
unlock:
		rcu_read_unlock();
	}

	/* Handle any pending watchpoint single-step breakpoints. */
	watchpoint_single_step_handler(addr);
}

/*
 * Called from either the Data Abort Handler [watchpoint] or the
 * Prefetch Abort Handler [breakpoint] with preemption disabled.
 */
static int hw_breakpoint_pending(unsigned long addr, unsigned int fsr,
				 struct pt_regs *regs)
{
	int ret = 0;
	u32 dscr;

	/* We must be called with preemption disabled. */
	WARN_ON(preemptible());

	/* We only handle watchpoints and hardware breakpoints. */
	ARM_DBG_READ(c1, 0, dscr);

	/* Perform perf callbacks. */
	switch (ARM_DSCR_MOE(dscr)) {
	case ARM_ENTRY_BREAKPOINT:
		breakpoint_handler(addr, regs);
		break;
	case ARM_ENTRY_ASYNC_WATCHPOINT:
		WARN(1, "Asynchronous watchpoint exception taken. Debugging results may be unreliable\n");
	case ARM_ENTRY_SYNC_WATCHPOINT:
		watchpoint_handler(addr, regs);
		break;
	default:
		ret = 1; /* Unhandled fault. */
	}

	/*
	 * Re-enable preemption after it was disabled in the
	 * low-level exception handling code.
	 */
	preempt_enable();

	return ret;
}

/*
 * One-time initialisation.
 */
static void reset_ctrl_regs(void *info)
{
	int i, cpu = smp_processor_id();
	u32 dbg_power;
	cpumask_t *cpumask = info;

	/*
	 * v7 debug contains save and restore registers so that debug state
	 * can be maintained across low-power modes without leaving the debug
	 * logic powered up. It is IMPLEMENTATION DEFINED whether we can access
	 * the debug registers out of reset, so we must unlock the OS Lock
	 * Access Register to avoid taking undefined instruction exceptions
	 * later on.
	 */
	if (debug_arch >= ARM_DEBUG_ARCH_V7_ECP14) {
		/*
		 * Ensure sticky power-down is clear (i.e. debug logic is
		 * powered up).
		 */
		asm volatile("mrc p14, 0, %0, c1, c5, 4" : "=r" (dbg_power));
		if ((dbg_power & 0x1) == 0) {
			pr_warning("CPU %d debug is powered down!\n", cpu);
			cpumask_or(cpumask, cpumask, cpumask_of(cpu));
			return;
		}

		/*
		 * Unconditionally clear the lock by writing a value
		 * other than 0xC5ACCE55 to the access register.
		 */
		asm volatile("mcr p14, 0, %0, c1, c0, 4" : : "r" (0));
		isb();

		/*
		 * Clear any configured vector-catch events before
		 * enabling monitor mode.
		 */
		asm volatile("mcr p14, 0, %0, c0, c7, 0" : : "r" (0));
		isb();
	}

	if (enable_monitor_mode())
		return;

	/* We must also reset any reserved registers. */
	for (i = 0; i < core_num_brps + core_num_reserved_brps; ++i) {
		write_wb_reg(ARM_BASE_BCR + i, 0UL);
		write_wb_reg(ARM_BASE_BVR + i, 0UL);
	}

	for (i = 0; i < core_num_wrps; ++i) {
		write_wb_reg(ARM_BASE_WCR + i, 0UL);
		write_wb_reg(ARM_BASE_WVR + i, 0UL);
	}
}

static int __cpuinit dbg_reset_notify(struct notifier_block *self,
				      unsigned long action, void *cpu)
{
	if (action == CPU_ONLINE)
		smp_call_function_single((int)cpu, reset_ctrl_regs, NULL, 1);
	return NOTIFY_OK;
}

static struct notifier_block __cpuinitdata dbg_reset_nb = {
	.notifier_call = dbg_reset_notify,
};

static int __init arch_hw_breakpoint_init(void)
{
	u32 dscr;
	cpumask_t cpumask = { CPU_BITS_NONE };

	debug_arch = get_debug_arch();

	if (!debug_arch_supported()) {
		pr_info("debug architecture 0x%x unsupported.\n", debug_arch);
		return 0;
	}

	/* Determine how many BRPs/WRPs are available. */
	core_num_brps = get_num_brps();
	core_num_reserved_brps = get_num_reserved_brps();
	core_num_wrps = get_num_wrps();

	pr_info("found %d breakpoint and %d watchpoint registers.\n",
		core_num_brps + core_num_reserved_brps, core_num_wrps);

	if (core_num_reserved_brps)
		pr_info("%d breakpoint(s) reserved for watchpoint "
				"single-step.\n", core_num_reserved_brps);

	/*
	 * Reset the breakpoint resources. We assume that a halting
	 * debugger will leave the world in a nice state for us.
	 */
	on_each_cpu(reset_ctrl_regs, &cpumask, 1);
	if (!cpumask_empty(&cpumask)) {
		core_num_brps = 0;
		core_num_reserved_brps = 0;
		core_num_wrps = 0;
		return 0;
	}

	ARM_DBG_READ(c1, 0, dscr);
	if (dscr & ARM_DSCR_HDBGEN) {
		max_watchpoint_len = 4;
		pr_warning("halting debug mode enabled. Assuming maximum watchpoint size of %u bytes.\n",
			   max_watchpoint_len);
	} else {
		/* Work out the maximum supported watchpoint length. */
		max_watchpoint_len = get_max_wp_len();
		pr_info("maximum watchpoint size is %u bytes.\n",
				max_watchpoint_len);
	}

	/* Register debug fault handler. */
	hook_fault_code(2, hw_breakpoint_pending, SIGTRAP, TRAP_HWBKPT,
			"watchpoint debug exception");
	hook_ifault_code(2, hw_breakpoint_pending, SIGTRAP, TRAP_HWBKPT,
			"breakpoint debug exception");

	/* Register hotplug notifier. */
	register_cpu_notifier(&dbg_reset_nb);
	return 0;
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
