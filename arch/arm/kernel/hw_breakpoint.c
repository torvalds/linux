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
#include <linux/cpu_pm.h>
#include <linux/coresight.h>

#include <asm/cacheflush.h>
#include <asm/cputype.h>
#include <asm/current.h>
#include <asm/hw_breakpoint.h>
#include <asm/traps.h>

/* Breakpoint currently in use for each BRP. */
static DEFINE_PER_CPU(struct perf_event *, bp_on_reg[ARM_MAX_BRP]);

/* Watchpoint currently in use for each WRP. */
static DEFINE_PER_CPU(struct perf_event *, wp_on_reg[ARM_MAX_WRP]);

/* Number of BRP/WRP registers on this CPU. */
static int core_num_brps;
static int core_num_wrps;

/* Debug architecture version. */
static u8 debug_arch;

/* Does debug architecture support OS Save and Restore? */
static bool has_ossr;

/* Maximum supported watchpoint length. */
static u8 max_watchpoint_len;

#define READ_WB_REG_CASE(OP2, M, VAL)			\
	case ((OP2 << 4) + M):				\
		ARM_DBG_READ(c0, c ## M, OP2, VAL);	\
		break

#define WRITE_WB_REG_CASE(OP2, M, VAL)			\
	case ((OP2 << 4) + M):				\
		ARM_DBG_WRITE(c0, c ## M, OP2, VAL);	\
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
		pr_warn("attempt to read from unknown breakpoint register %d\n",
			n);
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
		pr_warn("attempt to write to unknown breakpoint register %d\n",
			n);
	}
	isb();
}

/* Determine debug architecture. */
static u8 get_debug_arch(void)
{
	u32 didr;

	/* Do we implement the extended CPUID interface? */
	if (((read_cpuid_id() >> 16) & 0xf) != 0xf) {
		pr_warn_once("CPUID feature registers not supported. "
			     "Assuming v6 debug is present.\n");
		return ARM_DEBUG_ARCH_V6;
	}

	ARM_DBG_READ(c0, c0, 0, didr);
	return (didr >> 16) & 0xf;
}

u8 arch_get_debug_arch(void)
{
	return debug_arch;
}

static int debug_arch_supported(void)
{
	u8 arch = get_debug_arch();

	/* We don't support the memory-mapped interface. */
	return (arch >= ARM_DEBUG_ARCH_V6 && arch <= ARM_DEBUG_ARCH_V7_ECP14) ||
		arch >= ARM_DEBUG_ARCH_V7_1;
}

/* Can we determine the watchpoint access type from the fsr? */
static int debug_exception_updates_fsr(void)
{
	return get_debug_arch() >= ARM_DEBUG_ARCH_V8;
}

/* Determine number of WRP registers available. */
static int get_num_wrp_resources(void)
{
	u32 didr;
	ARM_DBG_READ(c0, c0, 0, didr);
	return ((didr >> 28) & 0xf) + 1;
}

/* Determine number of BRP registers available. */
static int get_num_brp_resources(void)
{
	u32 didr;
	ARM_DBG_READ(c0, c0, 0, didr);
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
	 * On debug architectures prior to 7.1, when a watchpoint fires, the
	 * only way to work out which watchpoint it was is by disassembling
	 * the faulting instruction and working out the address of the memory
	 * access.
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
	if (get_debug_arch() < ARM_DEBUG_ARCH_V7_1)
		return 1;

	return get_num_wrp_resources();
}

/* Determine number of usable BRPs available. */
static int get_num_brps(void)
{
	int brps = get_num_brp_resources();
	return core_has_mismatch_brps() ? brps - 1 : brps;
}

/*
 * In order to access the breakpoint/watchpoint control registers,
 * we must be running in debug monitor mode. Unfortunately, we can
 * be put into halting debug mode at any time by an external debugger
 * but there is nothing we can do to prevent that.
 */
static int monitor_mode_enabled(void)
{
	u32 dscr;
	ARM_DBG_READ(c0, c1, 0, dscr);
	return !!(dscr & ARM_DSCR_MDBGEN);
}

static int enable_monitor_mode(void)
{
	u32 dscr;
	ARM_DBG_READ(c0, c1, 0, dscr);

	/* If monitor mode is already enabled, just return. */
	if (dscr & ARM_DSCR_MDBGEN)
		goto out;

	/* Write to the corresponding DSCR. */
	switch (get_debug_arch()) {
	case ARM_DEBUG_ARCH_V6:
	case ARM_DEBUG_ARCH_V6_1:
		ARM_DBG_WRITE(c0, c1, 0, (dscr | ARM_DSCR_MDBGEN));
		break;
	case ARM_DEBUG_ARCH_V7_ECP14:
	case ARM_DEBUG_ARCH_V7_1:
	case ARM_DEBUG_ARCH_V8:
		ARM_DBG_WRITE(c0, c2, 2, (dscr | ARM_DSCR_MDBGEN));
		isb();
		break;
	default:
		return -ENODEV;
	}

	/* Check that the write made it through. */
	ARM_DBG_READ(c0, c1, 0, dscr);
	if (!(dscr & ARM_DSCR_MDBGEN)) {
		pr_warn_once("Failed to enable monitor mode on CPU %d.\n",
				smp_processor_id());
		return -EPERM;
	}

out:
	return 0;
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
		pr_warn("unknown slot type: %d\n", type);
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
	int i, max_slots, ctrl_base, val_base;
	u32 addr, ctrl;

	addr = info->address;
	ctrl = encode_ctrl_reg(info->ctrl) | 0x1;

	if (info->ctrl.type == ARM_BREAKPOINT_EXECUTE) {
		/* Breakpoint */
		ctrl_base = ARM_BASE_BCR;
		val_base = ARM_BASE_BVR;
		slots = this_cpu_ptr(bp_on_reg);
		max_slots = core_num_brps;
	} else {
		/* Watchpoint */
		ctrl_base = ARM_BASE_WCR;
		val_base = ARM_BASE_WVR;
		slots = this_cpu_ptr(wp_on_reg);
		max_slots = core_num_wrps;
	}

	for (i = 0; i < max_slots; ++i) {
		slot = &slots[i];

		if (!*slot) {
			*slot = bp;
			break;
		}
	}

	if (i == max_slots) {
		pr_warn("Can't find any breakpoint slot\n");
		return -EBUSY;
	}

	/* Override the breakpoint data with the step data. */
	if (info->step_ctrl.enabled) {
		addr = info->trigger & ~0x3;
		ctrl = encode_ctrl_reg(info->step_ctrl);
		if (info->ctrl.type != ARM_BREAKPOINT_EXECUTE) {
			i = 0;
			ctrl_base = ARM_BASE_BCR + core_num_brps;
			val_base = ARM_BASE_BVR + core_num_brps;
		}
	}

	/* Setup the address register. */
	write_wb_reg(val_base + i, addr);

	/* Setup the control register. */
	write_wb_reg(ctrl_base + i, ctrl);
	return 0;
}

void arch_uninstall_hw_breakpoint(struct perf_event *bp)
{
	struct arch_hw_breakpoint *info = counter_arch_bp(bp);
	struct perf_event **slot, **slots;
	int i, max_slots, base;

	if (info->ctrl.type == ARM_BREAKPOINT_EXECUTE) {
		/* Breakpoint */
		base = ARM_BASE_BCR;
		slots = this_cpu_ptr(bp_on_reg);
		max_slots = core_num_brps;
	} else {
		/* Watchpoint */
		base = ARM_BASE_WCR;
		slots = this_cpu_ptr(wp_on_reg);
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

	if (i == max_slots) {
		pr_warn("Can't find any breakpoint slot\n");
		return;
	}

	/* Ensure that we disable the mismatch breakpoint. */
	if (info->ctrl.type != ARM_BREAKPOINT_EXECUTE &&
	    info->step_ctrl.enabled) {
		i = 0;
		base = ARM_BASE_BCR + core_num_brps;
	}

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

	/* Ensure that we are in monitor debug mode. */
	if (!monitor_mode_enabled())
		return -ENODEV;

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
	case 2:
		/* Allow halfword watchpoints and breakpoints. */
		if (info->ctrl.len == ARM_BREAKPOINT_LEN_2)
			break;
	case 3:
		/* Allow single byte watchpoint. */
		if (info->ctrl.len == ARM_BREAKPOINT_LEN_1)
			break;
	default:
		ret = -EINVAL;
		goto out;
	}

	info->address &= ~alignment_mask;
	info->ctrl.len <<= offset;

	if (is_default_overflow_handler(bp)) {
		/*
		 * Mismatch breakpoints are required for single-stepping
		 * breakpoints.
		 */
		if (!core_has_mismatch_brps())
			return -EINVAL;

		/* We don't allow mismatch breakpoints in kernel space. */
		if (arch_check_bp_in_kernelspace(bp))
			return -EPERM;

		/*
		 * Per-cpu breakpoints are not supported by our stepping
		 * mechanism.
		 */
		if (!bp->hw.target)
			return -EINVAL;

		/*
		 * We only support specific access types if the fsr
		 * reports them.
		 */
		if (!debug_exception_updates_fsr() &&
		    (info->ctrl.type == ARM_BREAKPOINT_LOAD ||
		     info->ctrl.type == ARM_BREAKPOINT_STORE))
			return -EINVAL;
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

static void watchpoint_handler(unsigned long addr, unsigned int fsr,
			       struct pt_regs *regs)
{
	int i, access;
	u32 val, ctrl_reg, alignment_mask;
	struct perf_event *wp, **slots;
	struct arch_hw_breakpoint *info;
	struct arch_hw_breakpoint_ctrl ctrl;

	slots = this_cpu_ptr(wp_on_reg);

	for (i = 0; i < core_num_wrps; ++i) {
		rcu_read_lock();

		wp = slots[i];

		if (wp == NULL)
			goto unlock;

		info = counter_arch_bp(wp);
		/*
		 * The DFAR is an unknown value on debug architectures prior
		 * to 7.1. Since we only allow a single watchpoint on these
		 * older CPUs, we can set the trigger to the lowest possible
		 * faulting address.
		 */
		if (debug_arch < ARM_DEBUG_ARCH_V7_1) {
			BUG_ON(i > 0);
			info->trigger = wp->attr.bp_addr;
		} else {
			if (info->ctrl.len == ARM_BREAKPOINT_LEN_8)
				alignment_mask = 0x7;
			else
				alignment_mask = 0x3;

			/* Check if the watchpoint value matches. */
			val = read_wb_reg(ARM_BASE_WVR + i);
			if (val != (addr & ~alignment_mask))
				goto unlock;

			/* Possible match, check the byte address select. */
			ctrl_reg = read_wb_reg(ARM_BASE_WCR + i);
			decode_ctrl_reg(ctrl_reg, &ctrl);
			if (!((1 << (addr & alignment_mask)) & ctrl.len))
				goto unlock;

			/* Check that the access type matches. */
			if (debug_exception_updates_fsr()) {
				access = (fsr & ARM_FSR_ACCESS_MASK) ?
					  HW_BREAKPOINT_W : HW_BREAKPOINT_R;
				if (!(access & hw_breakpoint_type(wp)))
					goto unlock;
			}

			/* We have a winner. */
			info->trigger = addr;
		}

		pr_debug("watchpoint fired: address = 0x%x\n", info->trigger);
		perf_bp_event(wp, regs);

		/*
		 * If no overflow handler is present, insert a temporary
		 * mismatch breakpoint so we can single-step over the
		 * watchpoint trigger.
		 */
		if (is_default_overflow_handler(wp))
			enable_single_step(wp, instruction_pointer(regs));

unlock:
		rcu_read_unlock();
	}
}

static void watchpoint_single_step_handler(unsigned long pc)
{
	int i;
	struct perf_event *wp, **slots;
	struct arch_hw_breakpoint *info;

	slots = this_cpu_ptr(wp_on_reg);

	for (i = 0; i < core_num_wrps; ++i) {
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

	slots = this_cpu_ptr(bp_on_reg);

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
 * Prefetch Abort Handler [breakpoint] with interrupts disabled.
 */
static int hw_breakpoint_pending(unsigned long addr, unsigned int fsr,
				 struct pt_regs *regs)
{
	int ret = 0;
	u32 dscr;

	preempt_disable();

	if (interrupts_enabled(regs))
		local_irq_enable();

	/* We only handle watchpoints and hardware breakpoints. */
	ARM_DBG_READ(c0, c1, 0, dscr);

	/* Perform perf callbacks. */
	switch (ARM_DSCR_MOE(dscr)) {
	case ARM_ENTRY_BREAKPOINT:
		breakpoint_handler(addr, regs);
		break;
	case ARM_ENTRY_ASYNC_WATCHPOINT:
		WARN(1, "Asynchronous watchpoint exception taken. Debugging results may be unreliable\n");
	case ARM_ENTRY_SYNC_WATCHPOINT:
		watchpoint_handler(addr, fsr, regs);
		break;
	default:
		ret = 1; /* Unhandled fault. */
	}

	preempt_enable();

	return ret;
}

/*
 * One-time initialisation.
 */
static cpumask_t debug_err_mask;

static int debug_reg_trap(struct pt_regs *regs, unsigned int instr)
{
	int cpu = smp_processor_id();

	pr_warn("Debug register access (0x%x) caused undefined instruction on CPU %d\n",
		instr, cpu);

	/* Set the error flag for this CPU and skip the faulting instruction. */
	cpumask_set_cpu(cpu, &debug_err_mask);
	instruction_pointer(regs) += 4;
	return 0;
}

static struct undef_hook debug_reg_hook = {
	.instr_mask	= 0x0fe80f10,
	.instr_val	= 0x0e000e10,
	.fn		= debug_reg_trap,
};

/* Does this core support OS Save and Restore? */
static bool core_has_os_save_restore(void)
{
	u32 oslsr;

	switch (get_debug_arch()) {
	case ARM_DEBUG_ARCH_V7_1:
		return true;
	case ARM_DEBUG_ARCH_V7_ECP14:
		ARM_DBG_READ(c1, c1, 4, oslsr);
		if (oslsr & ARM_OSLSR_OSLM0)
			return true;
	default:
		return false;
	}
}

static void reset_ctrl_regs(void *unused)
{
	int i, raw_num_brps, err = 0, cpu = smp_processor_id();
	u32 val;

	/*
	 * v7 debug contains save and restore registers so that debug state
	 * can be maintained across low-power modes without leaving the debug
	 * logic powered up. It is IMPLEMENTATION DEFINED whether we can access
	 * the debug registers out of reset, so we must unlock the OS Lock
	 * Access Register to avoid taking undefined instruction exceptions
	 * later on.
	 */
	switch (debug_arch) {
	case ARM_DEBUG_ARCH_V6:
	case ARM_DEBUG_ARCH_V6_1:
		/* ARMv6 cores clear the registers out of reset. */
		goto out_mdbgen;
	case ARM_DEBUG_ARCH_V7_ECP14:
		/*
		 * Ensure sticky power-down is clear (i.e. debug logic is
		 * powered up).
		 */
		ARM_DBG_READ(c1, c5, 4, val);
		if ((val & 0x1) == 0)
			err = -EPERM;

		if (!has_ossr)
			goto clear_vcr;
		break;
	case ARM_DEBUG_ARCH_V7_1:
		/*
		 * Ensure the OS double lock is clear.
		 */
		ARM_DBG_READ(c1, c3, 4, val);
		if ((val & 0x1) == 1)
			err = -EPERM;
		break;
	}

	if (err) {
		pr_warn_once("CPU %d debug is powered down!\n", cpu);
		cpumask_or(&debug_err_mask, &debug_err_mask, cpumask_of(cpu));
		return;
	}

	/*
	 * Unconditionally clear the OS lock by writing a value
	 * other than CS_LAR_KEY to the access register.
	 */
	ARM_DBG_WRITE(c1, c0, 4, ~CORESIGHT_UNLOCK);
	isb();

	/*
	 * Clear any configured vector-catch events before
	 * enabling monitor mode.
	 */
clear_vcr:
	ARM_DBG_WRITE(c0, c7, 0, 0);
	isb();

	if (cpumask_intersects(&debug_err_mask, cpumask_of(cpu))) {
		pr_warn_once("CPU %d failed to disable vector catch\n", cpu);
		return;
	}

	/*
	 * The control/value register pairs are UNKNOWN out of reset so
	 * clear them to avoid spurious debug events.
	 */
	raw_num_brps = get_num_brp_resources();
	for (i = 0; i < raw_num_brps; ++i) {
		write_wb_reg(ARM_BASE_BCR + i, 0UL);
		write_wb_reg(ARM_BASE_BVR + i, 0UL);
	}

	for (i = 0; i < core_num_wrps; ++i) {
		write_wb_reg(ARM_BASE_WCR + i, 0UL);
		write_wb_reg(ARM_BASE_WVR + i, 0UL);
	}

	if (cpumask_intersects(&debug_err_mask, cpumask_of(cpu))) {
		pr_warn_once("CPU %d failed to clear debug register pairs\n", cpu);
		return;
	}

	/*
	 * Have a crack at enabling monitor mode. We don't actually need
	 * it yet, but reporting an error early is useful if it fails.
	 */
out_mdbgen:
	if (enable_monitor_mode())
		cpumask_or(&debug_err_mask, &debug_err_mask, cpumask_of(cpu));
}

static int dbg_reset_notify(struct notifier_block *self,
				      unsigned long action, void *cpu)
{
	if ((action & ~CPU_TASKS_FROZEN) == CPU_ONLINE)
		smp_call_function_single((int)cpu, reset_ctrl_regs, NULL, 1);

	return NOTIFY_OK;
}

static struct notifier_block dbg_reset_nb = {
	.notifier_call = dbg_reset_notify,
};

#ifdef CONFIG_CPU_PM
static int dbg_cpu_pm_notify(struct notifier_block *self, unsigned long action,
			     void *v)
{
	if (action == CPU_PM_EXIT)
		reset_ctrl_regs(NULL);

	return NOTIFY_OK;
}

static struct notifier_block dbg_cpu_pm_nb = {
	.notifier_call = dbg_cpu_pm_notify,
};

static void __init pm_init(void)
{
	cpu_pm_register_notifier(&dbg_cpu_pm_nb);
}
#else
static inline void pm_init(void)
{
}
#endif

static int __init arch_hw_breakpoint_init(void)
{
	debug_arch = get_debug_arch();

	if (!debug_arch_supported()) {
		pr_info("debug architecture 0x%x unsupported.\n", debug_arch);
		return 0;
	}

	/*
	 * Scorpion CPUs (at least those in APQ8060) seem to set DBGPRSR.SPD
	 * whenever a WFI is issued, even if the core is not powered down, in
	 * violation of the architecture.  When DBGPRSR.SPD is set, accesses to
	 * breakpoint and watchpoint registers are treated as undefined, so
	 * this results in boot time and runtime failures when these are
	 * accessed and we unexpectedly take a trap.
	 *
	 * It's not clear if/how this can be worked around, so we blacklist
	 * Scorpion CPUs to avoid these issues.
	*/
	if (read_cpuid_part() == ARM_CPU_PART_SCORPION) {
		pr_info("Scorpion CPU detected. Hardware breakpoints and watchpoints disabled\n");
		return 0;
	}

	has_ossr = core_has_os_save_restore();

	/* Determine how many BRPs/WRPs are available. */
	core_num_brps = get_num_brps();
	core_num_wrps = get_num_wrps();

	cpu_notifier_register_begin();

	/*
	 * We need to tread carefully here because DBGSWENABLE may be
	 * driven low on this core and there isn't an architected way to
	 * determine that.
	 */
	register_undef_hook(&debug_reg_hook);

	/*
	 * Reset the breakpoint resources. We assume that a halting
	 * debugger will leave the world in a nice state for us.
	 */
	on_each_cpu(reset_ctrl_regs, NULL, 1);
	unregister_undef_hook(&debug_reg_hook);
	if (!cpumask_empty(&debug_err_mask)) {
		core_num_brps = 0;
		core_num_wrps = 0;
		cpu_notifier_register_done();
		return 0;
	}

	pr_info("found %d " "%s" "breakpoint and %d watchpoint registers.\n",
		core_num_brps, core_has_mismatch_brps() ? "(+1 reserved) " :
		"", core_num_wrps);

	/* Work out the maximum supported watchpoint length. */
	max_watchpoint_len = get_max_wp_len();
	pr_info("maximum watchpoint size is %u bytes.\n",
			max_watchpoint_len);

	/* Register debug fault handler. */
	hook_fault_code(FAULT_CODE_DEBUG, hw_breakpoint_pending, SIGTRAP,
			TRAP_HWBKPT, "watchpoint debug exception");
	hook_ifault_code(FAULT_CODE_DEBUG, hw_breakpoint_pending, SIGTRAP,
			TRAP_HWBKPT, "breakpoint debug exception");

	/* Register hotplug and PM notifiers. */
	__register_cpu_notifier(&dbg_reset_nb);

	cpu_notifier_register_done();

	pm_init();
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
