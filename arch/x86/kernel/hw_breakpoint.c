// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *
 * Copyright (C) 2007 Alan Stern
 * Copyright (C) 2009 IBM Corporation
 * Copyright (C) 2009 Frederic Weisbecker <fweisbec@gmail.com>
 *
 * Authors: Alan Stern <stern@rowland.harvard.edu>
 *          K.Prasad <prasad@linux.vnet.ibm.com>
 *          Frederic Weisbecker <fweisbec@gmail.com>
 */

/*
 * HW_breakpoint: a unified kernel/user-space hardware breakpoint facility,
 * using the CPU's debug registers.
 */

#include <linux/perf_event.h>
#include <linux/hw_breakpoint.h>
#include <linux/irqflags.h>
#include <linux/notifier.h>
#include <linux/kallsyms.h>
#include <linux/kprobes.h>
#include <linux/percpu.h>
#include <linux/kdebug.h>
#include <linux/kernel.h>
#include <linux/export.h>
#include <linux/sched.h>
#include <linux/smp.h>

#include <asm/hw_breakpoint.h>
#include <asm/processor.h>
#include <asm/debugreg.h>
#include <asm/user.h>
#include <asm/desc.h>
#include <asm/tlbflush.h>

/* Per cpu debug control register value */
DEFINE_PER_CPU(unsigned long, cpu_dr7);
EXPORT_PER_CPU_SYMBOL(cpu_dr7);

/* Per cpu debug address registers values */
static DEFINE_PER_CPU(unsigned long, cpu_debugreg[HBP_NUM]);

/*
 * Stores the breakpoints currently in use on each breakpoint address
 * register for each cpus
 */
static DEFINE_PER_CPU(struct perf_event *, bp_per_reg[HBP_NUM]);


static inline unsigned long
__encode_dr7(int drnum, unsigned int len, unsigned int type)
{
	unsigned long bp_info;

	bp_info = (len | type) & 0xf;
	bp_info <<= (DR_CONTROL_SHIFT + drnum * DR_CONTROL_SIZE);
	bp_info |= (DR_GLOBAL_ENABLE << (drnum * DR_ENABLE_SIZE));

	return bp_info;
}

/*
 * Encode the length, type, Exact, and Enable bits for a particular breakpoint
 * as stored in debug register 7.
 */
unsigned long encode_dr7(int drnum, unsigned int len, unsigned int type)
{
	return __encode_dr7(drnum, len, type) | DR_GLOBAL_SLOWDOWN;
}

/*
 * Decode the length and type bits for a particular breakpoint as
 * stored in debug register 7.  Return the "enabled" status.
 */
int decode_dr7(unsigned long dr7, int bpnum, unsigned *len, unsigned *type)
{
	int bp_info = dr7 >> (DR_CONTROL_SHIFT + bpnum * DR_CONTROL_SIZE);

	*len = (bp_info & 0xc) | 0x40;
	*type = (bp_info & 0x3) | 0x80;

	return (dr7 >> (bpnum * DR_ENABLE_SIZE)) & 0x3;
}

/*
 * Install a perf counter breakpoint.
 *
 * We seek a free debug address register and use it for this
 * breakpoint. Eventually we enable it in the debug control register.
 *
 * Atomic: we hold the counter->ctx->lock and we only handle variables
 * and registers local to this cpu.
 */
int arch_install_hw_breakpoint(struct perf_event *bp)
{
	struct arch_hw_breakpoint *info = counter_arch_bp(bp);
	unsigned long *dr7;
	int i;

	lockdep_assert_irqs_disabled();

	for (i = 0; i < HBP_NUM; i++) {
		struct perf_event **slot = this_cpu_ptr(&bp_per_reg[i]);

		if (!*slot) {
			*slot = bp;
			break;
		}
	}

	if (WARN_ONCE(i == HBP_NUM, "Can't find any breakpoint slot"))
		return -EBUSY;

	set_debugreg(info->address, i);
	__this_cpu_write(cpu_debugreg[i], info->address);

	dr7 = this_cpu_ptr(&cpu_dr7);
	*dr7 |= encode_dr7(i, info->len, info->type);

	/*
	 * Ensure we first write cpu_dr7 before we set the DR7 register.
	 * This ensures an NMI never see cpu_dr7 0 when DR7 is not.
	 */
	barrier();

	set_debugreg(*dr7, 7);
	if (info->mask)
		set_dr_addr_mask(info->mask, i);

	return 0;
}

/*
 * Uninstall the breakpoint contained in the given counter.
 *
 * First we search the debug address register it uses and then we disable
 * it.
 *
 * Atomic: we hold the counter->ctx->lock and we only handle variables
 * and registers local to this cpu.
 */
void arch_uninstall_hw_breakpoint(struct perf_event *bp)
{
	struct arch_hw_breakpoint *info = counter_arch_bp(bp);
	unsigned long dr7;
	int i;

	lockdep_assert_irqs_disabled();

	for (i = 0; i < HBP_NUM; i++) {
		struct perf_event **slot = this_cpu_ptr(&bp_per_reg[i]);

		if (*slot == bp) {
			*slot = NULL;
			break;
		}
	}

	if (WARN_ONCE(i == HBP_NUM, "Can't find any breakpoint slot"))
		return;

	dr7 = this_cpu_read(cpu_dr7);
	dr7 &= ~__encode_dr7(i, info->len, info->type);

	set_debugreg(dr7, 7);
	if (info->mask)
		set_dr_addr_mask(0, i);

	/*
	 * Ensure the write to cpu_dr7 is after we've set the DR7 register.
	 * This ensures an NMI never see cpu_dr7 0 when DR7 is not.
	 */
	barrier();

	this_cpu_write(cpu_dr7, dr7);
}

static int arch_bp_generic_len(int x86_len)
{
	switch (x86_len) {
	case X86_BREAKPOINT_LEN_1:
		return HW_BREAKPOINT_LEN_1;
	case X86_BREAKPOINT_LEN_2:
		return HW_BREAKPOINT_LEN_2;
	case X86_BREAKPOINT_LEN_4:
		return HW_BREAKPOINT_LEN_4;
#ifdef CONFIG_X86_64
	case X86_BREAKPOINT_LEN_8:
		return HW_BREAKPOINT_LEN_8;
#endif
	default:
		return -EINVAL;
	}
}

int arch_bp_generic_fields(int x86_len, int x86_type,
			   int *gen_len, int *gen_type)
{
	int len;

	/* Type */
	switch (x86_type) {
	case X86_BREAKPOINT_EXECUTE:
		if (x86_len != X86_BREAKPOINT_LEN_X)
			return -EINVAL;

		*gen_type = HW_BREAKPOINT_X;
		*gen_len = sizeof(long);
		return 0;
	case X86_BREAKPOINT_WRITE:
		*gen_type = HW_BREAKPOINT_W;
		break;
	case X86_BREAKPOINT_RW:
		*gen_type = HW_BREAKPOINT_W | HW_BREAKPOINT_R;
		break;
	default:
		return -EINVAL;
	}

	/* Len */
	len = arch_bp_generic_len(x86_len);
	if (len < 0)
		return -EINVAL;
	*gen_len = len;

	return 0;
}

/*
 * Check for virtual address in kernel space.
 */
int arch_check_bp_in_kernelspace(struct arch_hw_breakpoint *hw)
{
	unsigned long va;
	int len;

	va = hw->address;
	len = arch_bp_generic_len(hw->len);
	WARN_ON_ONCE(len < 0);

	/*
	 * We don't need to worry about va + len - 1 overflowing:
	 * we already require that va is aligned to a multiple of len.
	 */
	return (va >= TASK_SIZE_MAX) || ((va + len - 1) >= TASK_SIZE_MAX);
}

/*
 * Checks whether the range [addr, end], overlaps the area [base, base + size).
 */
static inline bool within_area(unsigned long addr, unsigned long end,
			       unsigned long base, unsigned long size)
{
	return end >= base && addr < (base + size);
}

/*
 * Checks whether the range from addr to end, inclusive, overlaps the fixed
 * mapped CPU entry area range or other ranges used for CPU entry.
 */
static inline bool within_cpu_entry(unsigned long addr, unsigned long end)
{
	int cpu;

	/* CPU entry erea is always used for CPU entry */
	if (within_area(addr, end, CPU_ENTRY_AREA_BASE,
			CPU_ENTRY_AREA_TOTAL_SIZE))
		return true;

	/*
	 * When FSGSBASE is enabled, paranoid_entry() fetches the per-CPU
	 * GSBASE value via __per_cpu_offset or pcpu_unit_offsets.
	 */
#ifdef CONFIG_SMP
	if (within_area(addr, end, (unsigned long)__per_cpu_offset,
			sizeof(unsigned long) * nr_cpu_ids))
		return true;
#else
	if (within_area(addr, end, (unsigned long)&pcpu_unit_offsets,
			sizeof(pcpu_unit_offsets)))
		return true;
#endif

	for_each_possible_cpu(cpu) {
		/* The original rw GDT is being used after load_direct_gdt() */
		if (within_area(addr, end, (unsigned long)get_cpu_gdt_rw(cpu),
				GDT_SIZE))
			return true;

		/*
		 * cpu_tss_rw is not directly referenced by hardware, but
		 * cpu_tss_rw is also used in CPU entry code,
		 */
		if (within_area(addr, end,
				(unsigned long)&per_cpu(cpu_tss_rw, cpu),
				sizeof(struct tss_struct)))
			return true;

		/*
		 * cpu_tlbstate.user_pcid_flush_mask is used for CPU entry.
		 * If a data breakpoint on it, it will cause an unwanted #DB.
		 * Protect the full cpu_tlbstate structure to be sure.
		 */
		if (within_area(addr, end,
				(unsigned long)&per_cpu(cpu_tlbstate, cpu),
				sizeof(struct tlb_state)))
			return true;

		/*
		 * When in guest (X86_FEATURE_HYPERVISOR), local_db_save()
		 * will read per-cpu cpu_dr7 before clear dr7 register.
		 */
		if (within_area(addr, end, (unsigned long)&per_cpu(cpu_dr7, cpu),
				sizeof(cpu_dr7)))
			return true;
	}

	return false;
}

static int arch_build_bp_info(struct perf_event *bp,
			      const struct perf_event_attr *attr,
			      struct arch_hw_breakpoint *hw)
{
	unsigned long bp_end;

	bp_end = attr->bp_addr + attr->bp_len - 1;
	if (bp_end < attr->bp_addr)
		return -EINVAL;

	/*
	 * Prevent any breakpoint of any type that overlaps the CPU
	 * entry area and data.  This protects the IST stacks and also
	 * reduces the chance that we ever find out what happens if
	 * there's a data breakpoint on the GDT, IDT, or TSS.
	 */
	if (within_cpu_entry(attr->bp_addr, bp_end))
		return -EINVAL;

	hw->address = attr->bp_addr;
	hw->mask = 0;

	/* Type */
	switch (attr->bp_type) {
	case HW_BREAKPOINT_W:
		hw->type = X86_BREAKPOINT_WRITE;
		break;
	case HW_BREAKPOINT_W | HW_BREAKPOINT_R:
		hw->type = X86_BREAKPOINT_RW;
		break;
	case HW_BREAKPOINT_X:
		/*
		 * We don't allow kernel breakpoints in places that are not
		 * acceptable for kprobes.  On non-kprobes kernels, we don't
		 * allow kernel breakpoints at all.
		 */
		if (attr->bp_addr >= TASK_SIZE_MAX) {
			if (within_kprobe_blacklist(attr->bp_addr))
				return -EINVAL;
		}

		hw->type = X86_BREAKPOINT_EXECUTE;
		/*
		 * x86 inst breakpoints need to have a specific undefined len.
		 * But we still need to check userspace is not trying to setup
		 * an unsupported length, to get a range breakpoint for example.
		 */
		if (attr->bp_len == sizeof(long)) {
			hw->len = X86_BREAKPOINT_LEN_X;
			return 0;
		}
		fallthrough;
	default:
		return -EINVAL;
	}

	/* Len */
	switch (attr->bp_len) {
	case HW_BREAKPOINT_LEN_1:
		hw->len = X86_BREAKPOINT_LEN_1;
		break;
	case HW_BREAKPOINT_LEN_2:
		hw->len = X86_BREAKPOINT_LEN_2;
		break;
	case HW_BREAKPOINT_LEN_4:
		hw->len = X86_BREAKPOINT_LEN_4;
		break;
#ifdef CONFIG_X86_64
	case HW_BREAKPOINT_LEN_8:
		hw->len = X86_BREAKPOINT_LEN_8;
		break;
#endif
	default:
		/* AMD range breakpoint */
		if (!is_power_of_2(attr->bp_len))
			return -EINVAL;
		if (attr->bp_addr & (attr->bp_len - 1))
			return -EINVAL;

		if (!boot_cpu_has(X86_FEATURE_BPEXT))
			return -EOPNOTSUPP;

		/*
		 * It's impossible to use a range breakpoint to fake out
		 * user vs kernel detection because bp_len - 1 can't
		 * have the high bit set.  If we ever allow range instruction
		 * breakpoints, then we'll have to check for kprobe-blacklisted
		 * addresses anywhere in the range.
		 */
		hw->mask = attr->bp_len - 1;
		hw->len = X86_BREAKPOINT_LEN_1;
	}

	return 0;
}

/*
 * Validate the arch-specific HW Breakpoint register settings
 */
int hw_breakpoint_arch_parse(struct perf_event *bp,
			     const struct perf_event_attr *attr,
			     struct arch_hw_breakpoint *hw)
{
	unsigned int align;
	int ret;


	ret = arch_build_bp_info(bp, attr, hw);
	if (ret)
		return ret;

	switch (hw->len) {
	case X86_BREAKPOINT_LEN_1:
		align = 0;
		if (hw->mask)
			align = hw->mask;
		break;
	case X86_BREAKPOINT_LEN_2:
		align = 1;
		break;
	case X86_BREAKPOINT_LEN_4:
		align = 3;
		break;
#ifdef CONFIG_X86_64
	case X86_BREAKPOINT_LEN_8:
		align = 7;
		break;
#endif
	default:
		WARN_ON_ONCE(1);
		return -EINVAL;
	}

	/*
	 * Check that the low-order bits of the address are appropriate
	 * for the alignment implied by len.
	 */
	if (hw->address & align)
		return -EINVAL;

	return 0;
}

/*
 * Release the user breakpoints used by ptrace
 */
void flush_ptrace_hw_breakpoint(struct task_struct *tsk)
{
	int i;
	struct thread_struct *t = &tsk->thread;

	for (i = 0; i < HBP_NUM; i++) {
		unregister_hw_breakpoint(t->ptrace_bps[i]);
		t->ptrace_bps[i] = NULL;
	}

	t->virtual_dr6 = 0;
	t->ptrace_dr7 = 0;
}

void hw_breakpoint_restore(void)
{
	set_debugreg(__this_cpu_read(cpu_debugreg[0]), 0);
	set_debugreg(__this_cpu_read(cpu_debugreg[1]), 1);
	set_debugreg(__this_cpu_read(cpu_debugreg[2]), 2);
	set_debugreg(__this_cpu_read(cpu_debugreg[3]), 3);
	set_debugreg(DR6_RESERVED, 6);
	set_debugreg(__this_cpu_read(cpu_dr7), 7);
}
EXPORT_SYMBOL_GPL(hw_breakpoint_restore);

/*
 * Handle debug exception notifications.
 *
 * Return value is either NOTIFY_STOP or NOTIFY_DONE as explained below.
 *
 * NOTIFY_DONE returned if one of the following conditions is true.
 * i) When the causative address is from user-space and the exception
 * is a valid one, i.e. not triggered as a result of lazy debug register
 * switching
 * ii) When there are more bits than trap<n> set in DR6 register (such
 * as BD, BS or BT) indicating that more than one debug condition is
 * met and requires some more action in do_debug().
 *
 * NOTIFY_STOP returned for all other cases
 *
 */
static int hw_breakpoint_handler(struct die_args *args)
{
	int i, rc = NOTIFY_STOP;
	struct perf_event *bp;
	unsigned long *dr6_p;
	unsigned long dr6;
	bool bpx;

	/* The DR6 value is pointed by args->err */
	dr6_p = (unsigned long *)ERR_PTR(args->err);
	dr6 = *dr6_p;

	/* Do an early return if no trap bits are set in DR6 */
	if ((dr6 & DR_TRAP_BITS) == 0)
		return NOTIFY_DONE;

	/* Handle all the breakpoints that were triggered */
	for (i = 0; i < HBP_NUM; ++i) {
		if (likely(!(dr6 & (DR_TRAP0 << i))))
			continue;

		bp = this_cpu_read(bp_per_reg[i]);
		if (!bp)
			continue;

		bpx = bp->hw.info.type == X86_BREAKPOINT_EXECUTE;

		/*
		 * TF and data breakpoints are traps and can be merged, however
		 * instruction breakpoints are faults and will be raised
		 * separately.
		 *
		 * However DR6 can indicate both TF and instruction
		 * breakpoints. In that case take TF as that has precedence and
		 * delay the instruction breakpoint for the next exception.
		 */
		if (bpx && (dr6 & DR_STEP))
			continue;

		/*
		 * Reset the 'i'th TRAP bit in dr6 to denote completion of
		 * exception handling
		 */
		(*dr6_p) &= ~(DR_TRAP0 << i);

		perf_bp_event(bp, args->regs);

		/*
		 * Set up resume flag to avoid breakpoint recursion when
		 * returning back to origin.
		 */
		if (bpx)
			args->regs->flags |= X86_EFLAGS_RF;
	}

	/*
	 * Further processing in do_debug() is needed for a) user-space
	 * breakpoints (to generate signals) and b) when the system has
	 * taken exception due to multiple causes
	 */
	if ((current->thread.virtual_dr6 & DR_TRAP_BITS) ||
	    (dr6 & (~DR_TRAP_BITS)))
		rc = NOTIFY_DONE;

	return rc;
}

/*
 * Handle debug exception notifications.
 */
int hw_breakpoint_exceptions_notify(
		struct notifier_block *unused, unsigned long val, void *data)
{
	if (val != DIE_DEBUG)
		return NOTIFY_DONE;

	return hw_breakpoint_handler(data);
}

void hw_breakpoint_pmu_read(struct perf_event *bp)
{
	/* TODO */
}
