// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Performance event support - powerpc architecture code
 *
 * Copyright 2008-2009 Paul Mackerras, IBM Corporation.
 */
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/sched/clock.h>
#include <linux/perf_event.h>
#include <linux/percpu.h>
#include <linux/hardirq.h>
#include <linux/uaccess.h>
#include <asm/reg.h>
#include <asm/pmc.h>
#include <asm/machdep.h>
#include <asm/firmware.h>
#include <asm/ptrace.h>
#include <asm/code-patching.h>

#ifdef CONFIG_PPC64
#include "internal.h"
#endif

#define BHRB_MAX_ENTRIES	32
#define BHRB_TARGET		0x0000000000000002
#define BHRB_PREDICTION		0x0000000000000001
#define BHRB_EA			0xFFFFFFFFFFFFFFFCUL

struct cpu_hw_events {
	int n_events;
	int n_percpu;
	int disabled;
	int n_added;
	int n_limited;
	u8  pmcs_enabled;
	struct perf_event *event[MAX_HWEVENTS];
	u64 events[MAX_HWEVENTS];
	unsigned int flags[MAX_HWEVENTS];
	struct mmcr_regs mmcr;
	struct perf_event *limited_counter[MAX_LIMITED_HWCOUNTERS];
	u8  limited_hwidx[MAX_LIMITED_HWCOUNTERS];
	u64 alternatives[MAX_HWEVENTS][MAX_EVENT_ALTERNATIVES];
	unsigned long amasks[MAX_HWEVENTS][MAX_EVENT_ALTERNATIVES];
	unsigned long avalues[MAX_HWEVENTS][MAX_EVENT_ALTERNATIVES];

	unsigned int txn_flags;
	int n_txn_start;

	/* BHRB bits */
	u64				bhrb_filter;	/* BHRB HW branch filter */
	unsigned int			bhrb_users;
	void				*bhrb_context;
	struct	perf_branch_stack	bhrb_stack;
	struct	perf_branch_entry	bhrb_entries[BHRB_MAX_ENTRIES];
	u64				ic_init;
};

static DEFINE_PER_CPU(struct cpu_hw_events, cpu_hw_events);

static struct power_pmu *ppmu;

/*
 * Normally, to ignore kernel events we set the FCS (freeze counters
 * in supervisor mode) bit in MMCR0, but if the kernel runs with the
 * hypervisor bit set in the MSR, or if we are running on a processor
 * where the hypervisor bit is forced to 1 (as on Apple G5 processors),
 * then we need to use the FCHV bit to ignore kernel events.
 */
static unsigned int freeze_events_kernel = MMCR0_FCS;

/*
 * 32-bit doesn't have MMCRA but does have an MMCR2,
 * and a few other names are different.
 * Also 32-bit doesn't have MMCR3, SIER2 and SIER3.
 * Define them as zero knowing that any code path accessing
 * these registers (via mtspr/mfspr) are done under ppmu flag
 * check for PPMU_ARCH_31 and we will not enter that code path
 * for 32-bit.
 */
#ifdef CONFIG_PPC32

#define MMCR0_FCHV		0
#define MMCR0_PMCjCE		MMCR0_PMCnCE
#define MMCR0_FC56		0
#define MMCR0_PMAO		0
#define MMCR0_EBE		0
#define MMCR0_BHRBA		0
#define MMCR0_PMCC		0
#define MMCR0_PMCC_U6		0

#define SPRN_MMCRA		SPRN_MMCR2
#define SPRN_MMCR3		0
#define SPRN_SIER2		0
#define SPRN_SIER3		0
#define MMCRA_SAMPLE_ENABLE	0
#define MMCRA_BHRB_DISABLE     0

static inline unsigned long perf_ip_adjust(struct pt_regs *regs)
{
	return 0;
}
static inline void perf_get_data_addr(struct perf_event *event, struct pt_regs *regs, u64 *addrp) { }
static inline u32 perf_get_misc_flags(struct pt_regs *regs)
{
	return 0;
}
static inline void perf_read_regs(struct pt_regs *regs)
{
	regs->result = 0;
}
static inline int perf_intr_is_nmi(struct pt_regs *regs)
{
	return 0;
}

static inline int siar_valid(struct pt_regs *regs)
{
	return 1;
}

static bool is_ebb_event(struct perf_event *event) { return false; }
static int ebb_event_check(struct perf_event *event) { return 0; }
static void ebb_event_add(struct perf_event *event) { }
static void ebb_switch_out(unsigned long mmcr0) { }
static unsigned long ebb_switch_in(bool ebb, struct cpu_hw_events *cpuhw)
{
	return cpuhw->mmcr.mmcr0;
}

static inline void power_pmu_bhrb_enable(struct perf_event *event) {}
static inline void power_pmu_bhrb_disable(struct perf_event *event) {}
static void power_pmu_sched_task(struct perf_event_context *ctx, bool sched_in) {}
static inline void power_pmu_bhrb_read(struct perf_event *event, struct cpu_hw_events *cpuhw) {}
static void pmao_restore_workaround(bool ebb) { }
#endif /* CONFIG_PPC32 */

bool is_sier_available(void)
{
	if (ppmu->flags & PPMU_HAS_SIER)
		return true;

	return false;
}

static bool regs_use_siar(struct pt_regs *regs)
{
	/*
	 * When we take a performance monitor exception the regs are setup
	 * using perf_read_regs() which overloads some fields, in particular
	 * regs->result to tell us whether to use SIAR.
	 *
	 * However if the regs are from another exception, eg. a syscall, then
	 * they have not been setup using perf_read_regs() and so regs->result
	 * is something random.
	 */
	return ((TRAP(regs) == 0xf00) && regs->result);
}

/*
 * Things that are specific to 64-bit implementations.
 */
#ifdef CONFIG_PPC64

static inline unsigned long perf_ip_adjust(struct pt_regs *regs)
{
	unsigned long mmcra = regs->dsisr;

	if ((ppmu->flags & PPMU_HAS_SSLOT) && (mmcra & MMCRA_SAMPLE_ENABLE)) {
		unsigned long slot = (mmcra & MMCRA_SLOT) >> MMCRA_SLOT_SHIFT;
		if (slot > 1)
			return 4 * (slot - 1);
	}

	return 0;
}

/*
 * The user wants a data address recorded.
 * If we're not doing instruction sampling, give them the SDAR
 * (sampled data address).  If we are doing instruction sampling, then
 * only give them the SDAR if it corresponds to the instruction
 * pointed to by SIAR; this is indicated by the [POWER6_]MMCRA_SDSYNC, the
 * [POWER7P_]MMCRA_SDAR_VALID bit in MMCRA, or the SDAR_VALID bit in SIER.
 */
static inline void perf_get_data_addr(struct perf_event *event, struct pt_regs *regs, u64 *addrp)
{
	unsigned long mmcra = regs->dsisr;
	bool sdar_valid;

	if (ppmu->flags & PPMU_HAS_SIER)
		sdar_valid = regs->dar & SIER_SDAR_VALID;
	else {
		unsigned long sdsync;

		if (ppmu->flags & PPMU_SIAR_VALID)
			sdsync = POWER7P_MMCRA_SDAR_VALID;
		else if (ppmu->flags & PPMU_ALT_SIPR)
			sdsync = POWER6_MMCRA_SDSYNC;
		else if (ppmu->flags & PPMU_NO_SIAR)
			sdsync = MMCRA_SAMPLE_ENABLE;
		else
			sdsync = MMCRA_SDSYNC;

		sdar_valid = mmcra & sdsync;
	}

	if (!(mmcra & MMCRA_SAMPLE_ENABLE) || sdar_valid)
		*addrp = mfspr(SPRN_SDAR);

	if (is_kernel_addr(mfspr(SPRN_SDAR)) && perf_allow_kernel(&event->attr) != 0)
		*addrp = 0;
}

static bool regs_sihv(struct pt_regs *regs)
{
	unsigned long sihv = MMCRA_SIHV;

	if (ppmu->flags & PPMU_HAS_SIER)
		return !!(regs->dar & SIER_SIHV);

	if (ppmu->flags & PPMU_ALT_SIPR)
		sihv = POWER6_MMCRA_SIHV;

	return !!(regs->dsisr & sihv);
}

static bool regs_sipr(struct pt_regs *regs)
{
	unsigned long sipr = MMCRA_SIPR;

	if (ppmu->flags & PPMU_HAS_SIER)
		return !!(regs->dar & SIER_SIPR);

	if (ppmu->flags & PPMU_ALT_SIPR)
		sipr = POWER6_MMCRA_SIPR;

	return !!(regs->dsisr & sipr);
}

static inline u32 perf_flags_from_msr(struct pt_regs *regs)
{
	if (regs->msr & MSR_PR)
		return PERF_RECORD_MISC_USER;
	if ((regs->msr & MSR_HV) && freeze_events_kernel != MMCR0_FCHV)
		return PERF_RECORD_MISC_HYPERVISOR;
	return PERF_RECORD_MISC_KERNEL;
}

static inline u32 perf_get_misc_flags(struct pt_regs *regs)
{
	bool use_siar = regs_use_siar(regs);

	if (!use_siar)
		return perf_flags_from_msr(regs);

	/*
	 * If we don't have flags in MMCRA, rather than using
	 * the MSR, we intuit the flags from the address in
	 * SIAR which should give slightly more reliable
	 * results
	 */
	if (ppmu->flags & PPMU_NO_SIPR) {
		unsigned long siar = mfspr(SPRN_SIAR);
		if (is_kernel_addr(siar))
			return PERF_RECORD_MISC_KERNEL;
		return PERF_RECORD_MISC_USER;
	}

	/* PR has priority over HV, so order below is important */
	if (regs_sipr(regs))
		return PERF_RECORD_MISC_USER;

	if (regs_sihv(regs) && (freeze_events_kernel != MMCR0_FCHV))
		return PERF_RECORD_MISC_HYPERVISOR;

	return PERF_RECORD_MISC_KERNEL;
}

/*
 * Overload regs->dsisr to store MMCRA so we only need to read it once
 * on each interrupt.
 * Overload regs->dar to store SIER if we have it.
 * Overload regs->result to specify whether we should use the MSR (result
 * is zero) or the SIAR (result is non zero).
 */
static inline void perf_read_regs(struct pt_regs *regs)
{
	unsigned long mmcra = mfspr(SPRN_MMCRA);
	int marked = mmcra & MMCRA_SAMPLE_ENABLE;
	int use_siar;

	regs->dsisr = mmcra;

	if (ppmu->flags & PPMU_HAS_SIER)
		regs->dar = mfspr(SPRN_SIER);

	/*
	 * If this isn't a PMU exception (eg a software event) the SIAR is
	 * not valid. Use pt_regs.
	 *
	 * If it is a marked event use the SIAR.
	 *
	 * If the PMU doesn't update the SIAR for non marked events use
	 * pt_regs.
	 *
	 * If the PMU has HV/PR flags then check to see if they
	 * place the exception in userspace. If so, use pt_regs. In
	 * continuous sampling mode the SIAR and the PMU exception are
	 * not synchronised, so they may be many instructions apart.
	 * This can result in confusing backtraces. We still want
	 * hypervisor samples as well as samples in the kernel with
	 * interrupts off hence the userspace check.
	 */
	if (TRAP(regs) != 0xf00)
		use_siar = 0;
	else if ((ppmu->flags & PPMU_NO_SIAR))
		use_siar = 0;
	else if (marked)
		use_siar = 1;
	else if ((ppmu->flags & PPMU_NO_CONT_SAMPLING))
		use_siar = 0;
	else if (!(ppmu->flags & PPMU_NO_SIPR) && regs_sipr(regs))
		use_siar = 0;
	else
		use_siar = 1;

	regs->result = use_siar;
}

/*
 * If interrupts were soft-disabled when a PMU interrupt occurs, treat
 * it as an NMI.
 */
static inline int perf_intr_is_nmi(struct pt_regs *regs)
{
	return (regs->softe & IRQS_DISABLED);
}

/*
 * On processors like P7+ that have the SIAR-Valid bit, marked instructions
 * must be sampled only if the SIAR-valid bit is set.
 *
 * For unmarked instructions and for processors that don't have the SIAR-Valid
 * bit, assume that SIAR is valid.
 */
static inline int siar_valid(struct pt_regs *regs)
{
	unsigned long mmcra = regs->dsisr;
	int marked = mmcra & MMCRA_SAMPLE_ENABLE;

	if (marked) {
		if (ppmu->flags & PPMU_HAS_SIER)
			return regs->dar & SIER_SIAR_VALID;

		if (ppmu->flags & PPMU_SIAR_VALID)
			return mmcra & POWER7P_MMCRA_SIAR_VALID;
	}

	return 1;
}


/* Reset all possible BHRB entries */
static void power_pmu_bhrb_reset(void)
{
	asm volatile(PPC_CLRBHRB);
}

static void power_pmu_bhrb_enable(struct perf_event *event)
{
	struct cpu_hw_events *cpuhw = this_cpu_ptr(&cpu_hw_events);

	if (!ppmu->bhrb_nr)
		return;

	/* Clear BHRB if we changed task context to avoid data leaks */
	if (event->ctx->task && cpuhw->bhrb_context != event->ctx) {
		power_pmu_bhrb_reset();
		cpuhw->bhrb_context = event->ctx;
	}
	cpuhw->bhrb_users++;
	perf_sched_cb_inc(event->ctx->pmu);
}

static void power_pmu_bhrb_disable(struct perf_event *event)
{
	struct cpu_hw_events *cpuhw = this_cpu_ptr(&cpu_hw_events);

	if (!ppmu->bhrb_nr)
		return;

	WARN_ON_ONCE(!cpuhw->bhrb_users);
	cpuhw->bhrb_users--;
	perf_sched_cb_dec(event->ctx->pmu);

	if (!cpuhw->disabled && !cpuhw->bhrb_users) {
		/* BHRB cannot be turned off when other
		 * events are active on the PMU.
		 */

		/* avoid stale pointer */
		cpuhw->bhrb_context = NULL;
	}
}

/* Called from ctxsw to prevent one process's branch entries to
 * mingle with the other process's entries during context switch.
 */
static void power_pmu_sched_task(struct perf_event_context *ctx, bool sched_in)
{
	if (!ppmu->bhrb_nr)
		return;

	if (sched_in)
		power_pmu_bhrb_reset();
}
/* Calculate the to address for a branch */
static __u64 power_pmu_bhrb_to(u64 addr)
{
	unsigned int instr;
	__u64 target;

	if (is_kernel_addr(addr)) {
		if (copy_from_kernel_nofault(&instr, (void *)addr,
				sizeof(instr)))
			return 0;

		return branch_target((struct ppc_inst *)&instr);
	}

	/* Userspace: need copy instruction here then translate it */
	if (copy_from_user_nofault(&instr, (unsigned int __user *)addr,
			sizeof(instr)))
		return 0;

	target = branch_target((struct ppc_inst *)&instr);
	if ((!target) || (instr & BRANCH_ABSOLUTE))
		return target;

	/* Translate relative branch target from kernel to user address */
	return target - (unsigned long)&instr + addr;
}

/* Processing BHRB entries */
static void power_pmu_bhrb_read(struct perf_event *event, struct cpu_hw_events *cpuhw)
{
	u64 val;
	u64 addr;
	int r_index, u_index, pred;

	r_index = 0;
	u_index = 0;
	while (r_index < ppmu->bhrb_nr) {
		/* Assembly read function */
		val = read_bhrb(r_index++);
		if (!val)
			/* Terminal marker: End of valid BHRB entries */
			break;
		else {
			addr = val & BHRB_EA;
			pred = val & BHRB_PREDICTION;

			if (!addr)
				/* invalid entry */
				continue;

			/*
			 * BHRB rolling buffer could very much contain the kernel
			 * addresses at this point. Check the privileges before
			 * exporting it to userspace (avoid exposure of regions
			 * where we could have speculative execution)
			 * Incase of ISA v3.1, BHRB will capture only user-space
			 * addresses, hence include a check before filtering code
			 */
			if (!(ppmu->flags & PPMU_ARCH_31) &&
				is_kernel_addr(addr) && perf_allow_kernel(&event->attr) != 0)
				continue;

			/* Branches are read most recent first (ie. mfbhrb 0 is
			 * the most recent branch).
			 * There are two types of valid entries:
			 * 1) a target entry which is the to address of a
			 *    computed goto like a blr,bctr,btar.  The next
			 *    entry read from the bhrb will be branch
			 *    corresponding to this target (ie. the actual
			 *    blr/bctr/btar instruction).
			 * 2) a from address which is an actual branch.  If a
			 *    target entry proceeds this, then this is the
			 *    matching branch for that target.  If this is not
			 *    following a target entry, then this is a branch
			 *    where the target is given as an immediate field
			 *    in the instruction (ie. an i or b form branch).
			 *    In this case we need to read the instruction from
			 *    memory to determine the target/to address.
			 */

			if (val & BHRB_TARGET) {
				/* Target branches use two entries
				 * (ie. computed gotos/XL form)
				 */
				cpuhw->bhrb_entries[u_index].to = addr;
				cpuhw->bhrb_entries[u_index].mispred = pred;
				cpuhw->bhrb_entries[u_index].predicted = ~pred;

				/* Get from address in next entry */
				val = read_bhrb(r_index++);
				addr = val & BHRB_EA;
				if (val & BHRB_TARGET) {
					/* Shouldn't have two targets in a
					   row.. Reset index and try again */
					r_index--;
					addr = 0;
				}
				cpuhw->bhrb_entries[u_index].from = addr;
			} else {
				/* Branches to immediate field 
				   (ie I or B form) */
				cpuhw->bhrb_entries[u_index].from = addr;
				cpuhw->bhrb_entries[u_index].to =
					power_pmu_bhrb_to(addr);
				cpuhw->bhrb_entries[u_index].mispred = pred;
				cpuhw->bhrb_entries[u_index].predicted = ~pred;
			}
			u_index++;

		}
	}
	cpuhw->bhrb_stack.nr = u_index;
	cpuhw->bhrb_stack.hw_idx = -1ULL;
	return;
}

static bool is_ebb_event(struct perf_event *event)
{
	/*
	 * This could be a per-PMU callback, but we'd rather avoid the cost. We
	 * check that the PMU supports EBB, meaning those that don't can still
	 * use bit 63 of the event code for something else if they wish.
	 */
	return (ppmu->flags & PPMU_ARCH_207S) &&
	       ((event->attr.config >> PERF_EVENT_CONFIG_EBB_SHIFT) & 1);
}

static int ebb_event_check(struct perf_event *event)
{
	struct perf_event *leader = event->group_leader;

	/* Event and group leader must agree on EBB */
	if (is_ebb_event(leader) != is_ebb_event(event))
		return -EINVAL;

	if (is_ebb_event(event)) {
		if (!(event->attach_state & PERF_ATTACH_TASK))
			return -EINVAL;

		if (!leader->attr.pinned || !leader->attr.exclusive)
			return -EINVAL;

		if (event->attr.freq ||
		    event->attr.inherit ||
		    event->attr.sample_type ||
		    event->attr.sample_period ||
		    event->attr.enable_on_exec)
			return -EINVAL;
	}

	return 0;
}

static void ebb_event_add(struct perf_event *event)
{
	if (!is_ebb_event(event) || current->thread.used_ebb)
		return;

	/*
	 * IFF this is the first time we've added an EBB event, set
	 * PMXE in the user MMCR0 so we can detect when it's cleared by
	 * userspace. We need this so that we can context switch while
	 * userspace is in the EBB handler (where PMXE is 0).
	 */
	current->thread.used_ebb = 1;
	current->thread.mmcr0 |= MMCR0_PMXE;
}

static void ebb_switch_out(unsigned long mmcr0)
{
	if (!(mmcr0 & MMCR0_EBE))
		return;

	current->thread.siar  = mfspr(SPRN_SIAR);
	current->thread.sier  = mfspr(SPRN_SIER);
	current->thread.sdar  = mfspr(SPRN_SDAR);
	current->thread.mmcr0 = mmcr0 & MMCR0_USER_MASK;
	current->thread.mmcr2 = mfspr(SPRN_MMCR2) & MMCR2_USER_MASK;
	if (ppmu->flags & PPMU_ARCH_31) {
		current->thread.mmcr3 = mfspr(SPRN_MMCR3);
		current->thread.sier2 = mfspr(SPRN_SIER2);
		current->thread.sier3 = mfspr(SPRN_SIER3);
	}
}

static unsigned long ebb_switch_in(bool ebb, struct cpu_hw_events *cpuhw)
{
	unsigned long mmcr0 = cpuhw->mmcr.mmcr0;

	if (!ebb)
		goto out;

	/* Enable EBB and read/write to all 6 PMCs and BHRB for userspace */
	mmcr0 |= MMCR0_EBE | MMCR0_BHRBA | MMCR0_PMCC_U6;

	/*
	 * Add any bits from the user MMCR0, FC or PMAO. This is compatible
	 * with pmao_restore_workaround() because we may add PMAO but we never
	 * clear it here.
	 */
	mmcr0 |= current->thread.mmcr0;

	/*
	 * Be careful not to set PMXE if userspace had it cleared. This is also
	 * compatible with pmao_restore_workaround() because it has already
	 * cleared PMXE and we leave PMAO alone.
	 */
	if (!(current->thread.mmcr0 & MMCR0_PMXE))
		mmcr0 &= ~MMCR0_PMXE;

	mtspr(SPRN_SIAR, current->thread.siar);
	mtspr(SPRN_SIER, current->thread.sier);
	mtspr(SPRN_SDAR, current->thread.sdar);

	/*
	 * Merge the kernel & user values of MMCR2. The semantics we implement
	 * are that the user MMCR2 can set bits, ie. cause counters to freeze,
	 * but not clear bits. If a task wants to be able to clear bits, ie.
	 * unfreeze counters, it should not set exclude_xxx in its events and
	 * instead manage the MMCR2 entirely by itself.
	 */
	mtspr(SPRN_MMCR2, cpuhw->mmcr.mmcr2 | current->thread.mmcr2);

	if (ppmu->flags & PPMU_ARCH_31) {
		mtspr(SPRN_MMCR3, current->thread.mmcr3);
		mtspr(SPRN_SIER2, current->thread.sier2);
		mtspr(SPRN_SIER3, current->thread.sier3);
	}
out:
	return mmcr0;
}

static void pmao_restore_workaround(bool ebb)
{
	unsigned pmcs[6];

	if (!cpu_has_feature(CPU_FTR_PMAO_BUG))
		return;

	/*
	 * On POWER8E there is a hardware defect which affects the PMU context
	 * switch logic, ie. power_pmu_disable/enable().
	 *
	 * When a counter overflows PMXE is cleared and FC/PMAO is set in MMCR0
	 * by the hardware. Sometime later the actual PMU exception is
	 * delivered.
	 *
	 * If we context switch, or simply disable/enable, the PMU prior to the
	 * exception arriving, the exception will be lost when we clear PMAO.
	 *
	 * When we reenable the PMU, we will write the saved MMCR0 with PMAO
	 * set, and this _should_ generate an exception. However because of the
	 * defect no exception is generated when we write PMAO, and we get
	 * stuck with no counters counting but no exception delivered.
	 *
	 * The workaround is to detect this case and tweak the hardware to
	 * create another pending PMU exception.
	 *
	 * We do that by setting up PMC6 (cycles) for an imminent overflow and
	 * enabling the PMU. That causes a new exception to be generated in the
	 * chip, but we don't take it yet because we have interrupts hard
	 * disabled. We then write back the PMU state as we want it to be seen
	 * by the exception handler. When we reenable interrupts the exception
	 * handler will be called and see the correct state.
	 *
	 * The logic is the same for EBB, except that the exception is gated by
	 * us having interrupts hard disabled as well as the fact that we are
	 * not in userspace. The exception is finally delivered when we return
	 * to userspace.
	 */

	/* Only if PMAO is set and PMAO_SYNC is clear */
	if ((current->thread.mmcr0 & (MMCR0_PMAO | MMCR0_PMAO_SYNC)) != MMCR0_PMAO)
		return;

	/* If we're doing EBB, only if BESCR[GE] is set */
	if (ebb && !(current->thread.bescr & BESCR_GE))
		return;

	/*
	 * We are already soft-disabled in power_pmu_enable(). We need to hard
	 * disable to actually prevent the PMU exception from firing.
	 */
	hard_irq_disable();

	/*
	 * This is a bit gross, but we know we're on POWER8E and have 6 PMCs.
	 * Using read/write_pmc() in a for loop adds 12 function calls and
	 * almost doubles our code size.
	 */
	pmcs[0] = mfspr(SPRN_PMC1);
	pmcs[1] = mfspr(SPRN_PMC2);
	pmcs[2] = mfspr(SPRN_PMC3);
	pmcs[3] = mfspr(SPRN_PMC4);
	pmcs[4] = mfspr(SPRN_PMC5);
	pmcs[5] = mfspr(SPRN_PMC6);

	/* Ensure all freeze bits are unset */
	mtspr(SPRN_MMCR2, 0);

	/* Set up PMC6 to overflow in one cycle */
	mtspr(SPRN_PMC6, 0x7FFFFFFE);

	/* Enable exceptions and unfreeze PMC6 */
	mtspr(SPRN_MMCR0, MMCR0_PMXE | MMCR0_PMCjCE | MMCR0_PMAO);

	/* Now we need to refreeze and restore the PMCs */
	mtspr(SPRN_MMCR0, MMCR0_FC | MMCR0_PMAO);

	mtspr(SPRN_PMC1, pmcs[0]);
	mtspr(SPRN_PMC2, pmcs[1]);
	mtspr(SPRN_PMC3, pmcs[2]);
	mtspr(SPRN_PMC4, pmcs[3]);
	mtspr(SPRN_PMC5, pmcs[4]);
	mtspr(SPRN_PMC6, pmcs[5]);
}

#endif /* CONFIG_PPC64 */

static void perf_event_interrupt(struct pt_regs *regs);

/*
 * Read one performance monitor counter (PMC).
 */
static unsigned long read_pmc(int idx)
{
	unsigned long val;

	switch (idx) {
	case 1:
		val = mfspr(SPRN_PMC1);
		break;
	case 2:
		val = mfspr(SPRN_PMC2);
		break;
	case 3:
		val = mfspr(SPRN_PMC3);
		break;
	case 4:
		val = mfspr(SPRN_PMC4);
		break;
	case 5:
		val = mfspr(SPRN_PMC5);
		break;
	case 6:
		val = mfspr(SPRN_PMC6);
		break;
#ifdef CONFIG_PPC64
	case 7:
		val = mfspr(SPRN_PMC7);
		break;
	case 8:
		val = mfspr(SPRN_PMC8);
		break;
#endif /* CONFIG_PPC64 */
	default:
		printk(KERN_ERR "oops trying to read PMC%d\n", idx);
		val = 0;
	}
	return val;
}

/*
 * Write one PMC.
 */
static void write_pmc(int idx, unsigned long val)
{
	switch (idx) {
	case 1:
		mtspr(SPRN_PMC1, val);
		break;
	case 2:
		mtspr(SPRN_PMC2, val);
		break;
	case 3:
		mtspr(SPRN_PMC3, val);
		break;
	case 4:
		mtspr(SPRN_PMC4, val);
		break;
	case 5:
		mtspr(SPRN_PMC5, val);
		break;
	case 6:
		mtspr(SPRN_PMC6, val);
		break;
#ifdef CONFIG_PPC64
	case 7:
		mtspr(SPRN_PMC7, val);
		break;
	case 8:
		mtspr(SPRN_PMC8, val);
		break;
#endif /* CONFIG_PPC64 */
	default:
		printk(KERN_ERR "oops trying to write PMC%d\n", idx);
	}
}

/* Called from sysrq_handle_showregs() */
void perf_event_print_debug(void)
{
	unsigned long sdar, sier, flags;
	u32 pmcs[MAX_HWEVENTS];
	int i;

	if (!ppmu) {
		pr_info("Performance monitor hardware not registered.\n");
		return;
	}

	if (!ppmu->n_counter)
		return;

	local_irq_save(flags);

	pr_info("CPU: %d PMU registers, ppmu = %s n_counters = %d",
		 smp_processor_id(), ppmu->name, ppmu->n_counter);

	for (i = 0; i < ppmu->n_counter; i++)
		pmcs[i] = read_pmc(i + 1);

	for (; i < MAX_HWEVENTS; i++)
		pmcs[i] = 0xdeadbeef;

	pr_info("PMC1:  %08x PMC2: %08x PMC3: %08x PMC4: %08x\n",
		 pmcs[0], pmcs[1], pmcs[2], pmcs[3]);

	if (ppmu->n_counter > 4)
		pr_info("PMC5:  %08x PMC6: %08x PMC7: %08x PMC8: %08x\n",
			 pmcs[4], pmcs[5], pmcs[6], pmcs[7]);

	pr_info("MMCR0: %016lx MMCR1: %016lx MMCRA: %016lx\n",
		mfspr(SPRN_MMCR0), mfspr(SPRN_MMCR1), mfspr(SPRN_MMCRA));

	sdar = sier = 0;
#ifdef CONFIG_PPC64
	sdar = mfspr(SPRN_SDAR);

	if (ppmu->flags & PPMU_HAS_SIER)
		sier = mfspr(SPRN_SIER);

	if (ppmu->flags & PPMU_ARCH_207S) {
		pr_info("MMCR2: %016lx EBBHR: %016lx\n",
			mfspr(SPRN_MMCR2), mfspr(SPRN_EBBHR));
		pr_info("EBBRR: %016lx BESCR: %016lx\n",
			mfspr(SPRN_EBBRR), mfspr(SPRN_BESCR));
	}

	if (ppmu->flags & PPMU_ARCH_31) {
		pr_info("MMCR3: %016lx SIER2: %016lx SIER3: %016lx\n",
			mfspr(SPRN_MMCR3), mfspr(SPRN_SIER2), mfspr(SPRN_SIER3));
	}
#endif
	pr_info("SIAR:  %016lx SDAR:  %016lx SIER:  %016lx\n",
		mfspr(SPRN_SIAR), sdar, sier);

	local_irq_restore(flags);
}

/*
 * Check if a set of events can all go on the PMU at once.
 * If they can't, this will look at alternative codes for the events
 * and see if any combination of alternative codes is feasible.
 * The feasible set is returned in event_id[].
 */
static int power_check_constraints(struct cpu_hw_events *cpuhw,
				   u64 event_id[], unsigned int cflags[],
				   int n_ev)
{
	unsigned long mask, value, nv;
	unsigned long smasks[MAX_HWEVENTS], svalues[MAX_HWEVENTS];
	int n_alt[MAX_HWEVENTS], choice[MAX_HWEVENTS];
	int i, j;
	unsigned long addf = ppmu->add_fields;
	unsigned long tadd = ppmu->test_adder;
	unsigned long grp_mask = ppmu->group_constraint_mask;
	unsigned long grp_val = ppmu->group_constraint_val;

	if (n_ev > ppmu->n_counter)
		return -1;

	/* First see if the events will go on as-is */
	for (i = 0; i < n_ev; ++i) {
		if ((cflags[i] & PPMU_LIMITED_PMC_REQD)
		    && !ppmu->limited_pmc_event(event_id[i])) {
			ppmu->get_alternatives(event_id[i], cflags[i],
					       cpuhw->alternatives[i]);
			event_id[i] = cpuhw->alternatives[i][0];
		}
		if (ppmu->get_constraint(event_id[i], &cpuhw->amasks[i][0],
					 &cpuhw->avalues[i][0]))
			return -1;
	}
	value = mask = 0;
	for (i = 0; i < n_ev; ++i) {
		nv = (value | cpuhw->avalues[i][0]) +
			(value & cpuhw->avalues[i][0] & addf);

		if (((((nv + tadd) ^ value) & mask) & (~grp_mask)) != 0)
			break;

		if (((((nv + tadd) ^ cpuhw->avalues[i][0]) & cpuhw->amasks[i][0])
			& (~grp_mask)) != 0)
			break;

		value = nv;
		mask |= cpuhw->amasks[i][0];
	}
	if (i == n_ev) {
		if ((value & mask & grp_mask) != (mask & grp_val))
			return -1;
		else
			return 0;	/* all OK */
	}

	/* doesn't work, gather alternatives... */
	if (!ppmu->get_alternatives)
		return -1;
	for (i = 0; i < n_ev; ++i) {
		choice[i] = 0;
		n_alt[i] = ppmu->get_alternatives(event_id[i], cflags[i],
						  cpuhw->alternatives[i]);
		for (j = 1; j < n_alt[i]; ++j)
			ppmu->get_constraint(cpuhw->alternatives[i][j],
					     &cpuhw->amasks[i][j],
					     &cpuhw->avalues[i][j]);
	}

	/* enumerate all possibilities and see if any will work */
	i = 0;
	j = -1;
	value = mask = nv = 0;
	while (i < n_ev) {
		if (j >= 0) {
			/* we're backtracking, restore context */
			value = svalues[i];
			mask = smasks[i];
			j = choice[i];
		}
		/*
		 * See if any alternative k for event_id i,
		 * where k > j, will satisfy the constraints.
		 */
		while (++j < n_alt[i]) {
			nv = (value | cpuhw->avalues[i][j]) +
				(value & cpuhw->avalues[i][j] & addf);
			if ((((nv + tadd) ^ value) & mask) == 0 &&
			    (((nv + tadd) ^ cpuhw->avalues[i][j])
			     & cpuhw->amasks[i][j]) == 0)
				break;
		}
		if (j >= n_alt[i]) {
			/*
			 * No feasible alternative, backtrack
			 * to event_id i-1 and continue enumerating its
			 * alternatives from where we got up to.
			 */
			if (--i < 0)
				return -1;
		} else {
			/*
			 * Found a feasible alternative for event_id i,
			 * remember where we got up to with this event_id,
			 * go on to the next event_id, and start with
			 * the first alternative for it.
			 */
			choice[i] = j;
			svalues[i] = value;
			smasks[i] = mask;
			value = nv;
			mask |= cpuhw->amasks[i][j];
			++i;
			j = -1;
		}
	}

	/* OK, we have a feasible combination, tell the caller the solution */
	for (i = 0; i < n_ev; ++i)
		event_id[i] = cpuhw->alternatives[i][choice[i]];
	return 0;
}

/*
 * Check if newly-added events have consistent settings for
 * exclude_{user,kernel,hv} with each other and any previously
 * added events.
 */
static int check_excludes(struct perf_event **ctrs, unsigned int cflags[],
			  int n_prev, int n_new)
{
	int eu = 0, ek = 0, eh = 0;
	int i, n, first;
	struct perf_event *event;

	/*
	 * If the PMU we're on supports per event exclude settings then we
	 * don't need to do any of this logic. NB. This assumes no PMU has both
	 * per event exclude and limited PMCs.
	 */
	if (ppmu->flags & PPMU_ARCH_207S)
		return 0;

	n = n_prev + n_new;
	if (n <= 1)
		return 0;

	first = 1;
	for (i = 0; i < n; ++i) {
		if (cflags[i] & PPMU_LIMITED_PMC_OK) {
			cflags[i] &= ~PPMU_LIMITED_PMC_REQD;
			continue;
		}
		event = ctrs[i];
		if (first) {
			eu = event->attr.exclude_user;
			ek = event->attr.exclude_kernel;
			eh = event->attr.exclude_hv;
			first = 0;
		} else if (event->attr.exclude_user != eu ||
			   event->attr.exclude_kernel != ek ||
			   event->attr.exclude_hv != eh) {
			return -EAGAIN;
		}
	}

	if (eu || ek || eh)
		for (i = 0; i < n; ++i)
			if (cflags[i] & PPMU_LIMITED_PMC_OK)
				cflags[i] |= PPMU_LIMITED_PMC_REQD;

	return 0;
}

static u64 check_and_compute_delta(u64 prev, u64 val)
{
	u64 delta = (val - prev) & 0xfffffffful;

	/*
	 * POWER7 can roll back counter values, if the new value is smaller
	 * than the previous value it will cause the delta and the counter to
	 * have bogus values unless we rolled a counter over.  If a coutner is
	 * rolled back, it will be smaller, but within 256, which is the maximum
	 * number of events to rollback at once.  If we detect a rollback
	 * return 0.  This can lead to a small lack of precision in the
	 * counters.
	 */
	if (prev > val && (prev - val) < 256)
		delta = 0;

	return delta;
}

static void power_pmu_read(struct perf_event *event)
{
	s64 val, delta, prev;

	if (event->hw.state & PERF_HES_STOPPED)
		return;

	if (!event->hw.idx)
		return;

	if (is_ebb_event(event)) {
		val = read_pmc(event->hw.idx);
		local64_set(&event->hw.prev_count, val);
		return;
	}

	/*
	 * Performance monitor interrupts come even when interrupts
	 * are soft-disabled, as long as interrupts are hard-enabled.
	 * Therefore we treat them like NMIs.
	 */
	do {
		prev = local64_read(&event->hw.prev_count);
		barrier();
		val = read_pmc(event->hw.idx);
		delta = check_and_compute_delta(prev, val);
		if (!delta)
			return;
	} while (local64_cmpxchg(&event->hw.prev_count, prev, val) != prev);

	local64_add(delta, &event->count);

	/*
	 * A number of places program the PMC with (0x80000000 - period_left).
	 * We never want period_left to be less than 1 because we will program
	 * the PMC with a value >= 0x800000000 and an edge detected PMC will
	 * roll around to 0 before taking an exception. We have seen this
	 * on POWER8.
	 *
	 * To fix this, clamp the minimum value of period_left to 1.
	 */
	do {
		prev = local64_read(&event->hw.period_left);
		val = prev - delta;
		if (val < 1)
			val = 1;
	} while (local64_cmpxchg(&event->hw.period_left, prev, val) != prev);
}

/*
 * On some machines, PMC5 and PMC6 can't be written, don't respect
 * the freeze conditions, and don't generate interrupts.  This tells
 * us if `event' is using such a PMC.
 */
static int is_limited_pmc(int pmcnum)
{
	return (ppmu->flags & PPMU_LIMITED_PMC5_6)
		&& (pmcnum == 5 || pmcnum == 6);
}

static void freeze_limited_counters(struct cpu_hw_events *cpuhw,
				    unsigned long pmc5, unsigned long pmc6)
{
	struct perf_event *event;
	u64 val, prev, delta;
	int i;

	for (i = 0; i < cpuhw->n_limited; ++i) {
		event = cpuhw->limited_counter[i];
		if (!event->hw.idx)
			continue;
		val = (event->hw.idx == 5) ? pmc5 : pmc6;
		prev = local64_read(&event->hw.prev_count);
		event->hw.idx = 0;
		delta = check_and_compute_delta(prev, val);
		if (delta)
			local64_add(delta, &event->count);
	}
}

static void thaw_limited_counters(struct cpu_hw_events *cpuhw,
				  unsigned long pmc5, unsigned long pmc6)
{
	struct perf_event *event;
	u64 val, prev;
	int i;

	for (i = 0; i < cpuhw->n_limited; ++i) {
		event = cpuhw->limited_counter[i];
		event->hw.idx = cpuhw->limited_hwidx[i];
		val = (event->hw.idx == 5) ? pmc5 : pmc6;
		prev = local64_read(&event->hw.prev_count);
		if (check_and_compute_delta(prev, val))
			local64_set(&event->hw.prev_count, val);
		perf_event_update_userpage(event);
	}
}

/*
 * Since limited events don't respect the freeze conditions, we
 * have to read them immediately after freezing or unfreezing the
 * other events.  We try to keep the values from the limited
 * events as consistent as possible by keeping the delay (in
 * cycles and instructions) between freezing/unfreezing and reading
 * the limited events as small and consistent as possible.
 * Therefore, if any limited events are in use, we read them
 * both, and always in the same order, to minimize variability,
 * and do it inside the same asm that writes MMCR0.
 */
static void write_mmcr0(struct cpu_hw_events *cpuhw, unsigned long mmcr0)
{
	unsigned long pmc5, pmc6;

	if (!cpuhw->n_limited) {
		mtspr(SPRN_MMCR0, mmcr0);
		return;
	}

	/*
	 * Write MMCR0, then read PMC5 and PMC6 immediately.
	 * To ensure we don't get a performance monitor interrupt
	 * between writing MMCR0 and freezing/thawing the limited
	 * events, we first write MMCR0 with the event overflow
	 * interrupt enable bits turned off.
	 */
	asm volatile("mtspr %3,%2; mfspr %0,%4; mfspr %1,%5"
		     : "=&r" (pmc5), "=&r" (pmc6)
		     : "r" (mmcr0 & ~(MMCR0_PMC1CE | MMCR0_PMCjCE)),
		       "i" (SPRN_MMCR0),
		       "i" (SPRN_PMC5), "i" (SPRN_PMC6));

	if (mmcr0 & MMCR0_FC)
		freeze_limited_counters(cpuhw, pmc5, pmc6);
	else
		thaw_limited_counters(cpuhw, pmc5, pmc6);

	/*
	 * Write the full MMCR0 including the event overflow interrupt
	 * enable bits, if necessary.
	 */
	if (mmcr0 & (MMCR0_PMC1CE | MMCR0_PMCjCE))
		mtspr(SPRN_MMCR0, mmcr0);
}

/*
 * Disable all events to prevent PMU interrupts and to allow
 * events to be added or removed.
 */
static void power_pmu_disable(struct pmu *pmu)
{
	struct cpu_hw_events *cpuhw;
	unsigned long flags, mmcr0, val, mmcra;

	if (!ppmu)
		return;
	local_irq_save(flags);
	cpuhw = this_cpu_ptr(&cpu_hw_events);

	if (!cpuhw->disabled) {
		/*
		 * Check if we ever enabled the PMU on this cpu.
		 */
		if (!cpuhw->pmcs_enabled) {
			ppc_enable_pmcs();
			cpuhw->pmcs_enabled = 1;
		}

		/*
		 * Set the 'freeze counters' bit, clear EBE/BHRBA/PMCC/PMAO/FC56
		 */
		val  = mmcr0 = mfspr(SPRN_MMCR0);
		val |= MMCR0_FC;
		val &= ~(MMCR0_EBE | MMCR0_BHRBA | MMCR0_PMCC | MMCR0_PMAO |
			 MMCR0_FC56);

		/*
		 * The barrier is to make sure the mtspr has been
		 * executed and the PMU has frozen the events etc.
		 * before we return.
		 */
		write_mmcr0(cpuhw, val);
		mb();
		isync();

		val = mmcra = cpuhw->mmcr.mmcra;

		/*
		 * Disable instruction sampling if it was enabled
		 */
		if (cpuhw->mmcr.mmcra & MMCRA_SAMPLE_ENABLE)
			val &= ~MMCRA_SAMPLE_ENABLE;

		/* Disable BHRB via mmcra (BHRBRD) for p10 */
		if (ppmu->flags & PPMU_ARCH_31)
			val |= MMCRA_BHRB_DISABLE;

		/*
		 * Write SPRN_MMCRA if mmcra has either disabled
		 * instruction sampling or BHRB.
		 */
		if (val != mmcra) {
			mtspr(SPRN_MMCRA, mmcra);
			mb();
			isync();
		}

		cpuhw->disabled = 1;
		cpuhw->n_added = 0;

		ebb_switch_out(mmcr0);

#ifdef CONFIG_PPC64
		/*
		 * These are readable by userspace, may contain kernel
		 * addresses and are not switched by context switch, so clear
		 * them now to avoid leaking anything to userspace in general
		 * including to another process.
		 */
		if (ppmu->flags & PPMU_ARCH_207S) {
			mtspr(SPRN_SDAR, 0);
			mtspr(SPRN_SIAR, 0);
		}
#endif
	}

	local_irq_restore(flags);
}

/*
 * Re-enable all events if disable == 0.
 * If we were previously disabled and events were added, then
 * put the new config on the PMU.
 */
static void power_pmu_enable(struct pmu *pmu)
{
	struct perf_event *event;
	struct cpu_hw_events *cpuhw;
	unsigned long flags;
	long i;
	unsigned long val, mmcr0;
	s64 left;
	unsigned int hwc_index[MAX_HWEVENTS];
	int n_lim;
	int idx;
	bool ebb;

	if (!ppmu)
		return;
	local_irq_save(flags);

	cpuhw = this_cpu_ptr(&cpu_hw_events);
	if (!cpuhw->disabled)
		goto out;

	if (cpuhw->n_events == 0) {
		ppc_set_pmu_inuse(0);
		goto out;
	}

	cpuhw->disabled = 0;

	/*
	 * EBB requires an exclusive group and all events must have the EBB
	 * flag set, or not set, so we can just check a single event. Also we
	 * know we have at least one event.
	 */
	ebb = is_ebb_event(cpuhw->event[0]);

	/*
	 * If we didn't change anything, or only removed events,
	 * no need to recalculate MMCR* settings and reset the PMCs.
	 * Just reenable the PMU with the current MMCR* settings
	 * (possibly updated for removal of events).
	 */
	if (!cpuhw->n_added) {
		mtspr(SPRN_MMCRA, cpuhw->mmcr.mmcra & ~MMCRA_SAMPLE_ENABLE);
		mtspr(SPRN_MMCR1, cpuhw->mmcr.mmcr1);
		if (ppmu->flags & PPMU_ARCH_31)
			mtspr(SPRN_MMCR3, cpuhw->mmcr.mmcr3);
		goto out_enable;
	}

	/*
	 * Clear all MMCR settings and recompute them for the new set of events.
	 */
	memset(&cpuhw->mmcr, 0, sizeof(cpuhw->mmcr));

	if (ppmu->compute_mmcr(cpuhw->events, cpuhw->n_events, hwc_index,
			       &cpuhw->mmcr, cpuhw->event)) {
		/* shouldn't ever get here */
		printk(KERN_ERR "oops compute_mmcr failed\n");
		goto out;
	}

	if (!(ppmu->flags & PPMU_ARCH_207S)) {
		/*
		 * Add in MMCR0 freeze bits corresponding to the attr.exclude_*
		 * bits for the first event. We have already checked that all
		 * events have the same value for these bits as the first event.
		 */
		event = cpuhw->event[0];
		if (event->attr.exclude_user)
			cpuhw->mmcr.mmcr0 |= MMCR0_FCP;
		if (event->attr.exclude_kernel)
			cpuhw->mmcr.mmcr0 |= freeze_events_kernel;
		if (event->attr.exclude_hv)
			cpuhw->mmcr.mmcr0 |= MMCR0_FCHV;
	}

	/*
	 * Write the new configuration to MMCR* with the freeze
	 * bit set and set the hardware events to their initial values.
	 * Then unfreeze the events.
	 */
	ppc_set_pmu_inuse(1);
	mtspr(SPRN_MMCRA, cpuhw->mmcr.mmcra & ~MMCRA_SAMPLE_ENABLE);
	mtspr(SPRN_MMCR1, cpuhw->mmcr.mmcr1);
	mtspr(SPRN_MMCR0, (cpuhw->mmcr.mmcr0 & ~(MMCR0_PMC1CE | MMCR0_PMCjCE))
				| MMCR0_FC);
	if (ppmu->flags & PPMU_ARCH_207S)
		mtspr(SPRN_MMCR2, cpuhw->mmcr.mmcr2);

	if (ppmu->flags & PPMU_ARCH_31)
		mtspr(SPRN_MMCR3, cpuhw->mmcr.mmcr3);

	/*
	 * Read off any pre-existing events that need to move
	 * to another PMC.
	 */
	for (i = 0; i < cpuhw->n_events; ++i) {
		event = cpuhw->event[i];
		if (event->hw.idx && event->hw.idx != hwc_index[i] + 1) {
			power_pmu_read(event);
			write_pmc(event->hw.idx, 0);
			event->hw.idx = 0;
		}
	}

	/*
	 * Initialize the PMCs for all the new and moved events.
	 */
	cpuhw->n_limited = n_lim = 0;
	for (i = 0; i < cpuhw->n_events; ++i) {
		event = cpuhw->event[i];
		if (event->hw.idx)
			continue;
		idx = hwc_index[i] + 1;
		if (is_limited_pmc(idx)) {
			cpuhw->limited_counter[n_lim] = event;
			cpuhw->limited_hwidx[n_lim] = idx;
			++n_lim;
			continue;
		}

		if (ebb)
			val = local64_read(&event->hw.prev_count);
		else {
			val = 0;
			if (event->hw.sample_period) {
				left = local64_read(&event->hw.period_left);
				if (left < 0x80000000L)
					val = 0x80000000L - left;
			}
			local64_set(&event->hw.prev_count, val);
		}

		event->hw.idx = idx;
		if (event->hw.state & PERF_HES_STOPPED)
			val = 0;
		write_pmc(idx, val);

		perf_event_update_userpage(event);
	}
	cpuhw->n_limited = n_lim;
	cpuhw->mmcr.mmcr0 |= MMCR0_PMXE | MMCR0_FCECE;

 out_enable:
	pmao_restore_workaround(ebb);

	mmcr0 = ebb_switch_in(ebb, cpuhw);

	mb();
	if (cpuhw->bhrb_users)
		ppmu->config_bhrb(cpuhw->bhrb_filter);

	write_mmcr0(cpuhw, mmcr0);

	/*
	 * Enable instruction sampling if necessary
	 */
	if (cpuhw->mmcr.mmcra & MMCRA_SAMPLE_ENABLE) {
		mb();
		mtspr(SPRN_MMCRA, cpuhw->mmcr.mmcra);
	}

 out:

	local_irq_restore(flags);
}

static int collect_events(struct perf_event *group, int max_count,
			  struct perf_event *ctrs[], u64 *events,
			  unsigned int *flags)
{
	int n = 0;
	struct perf_event *event;

	if (group->pmu->task_ctx_nr == perf_hw_context) {
		if (n >= max_count)
			return -1;
		ctrs[n] = group;
		flags[n] = group->hw.event_base;
		events[n++] = group->hw.config;
	}
	for_each_sibling_event(event, group) {
		if (event->pmu->task_ctx_nr == perf_hw_context &&
		    event->state != PERF_EVENT_STATE_OFF) {
			if (n >= max_count)
				return -1;
			ctrs[n] = event;
			flags[n] = event->hw.event_base;
			events[n++] = event->hw.config;
		}
	}
	return n;
}

/*
 * Add an event to the PMU.
 * If all events are not already frozen, then we disable and
 * re-enable the PMU in order to get hw_perf_enable to do the
 * actual work of reconfiguring the PMU.
 */
static int power_pmu_add(struct perf_event *event, int ef_flags)
{
	struct cpu_hw_events *cpuhw;
	unsigned long flags;
	int n0;
	int ret = -EAGAIN;

	local_irq_save(flags);
	perf_pmu_disable(event->pmu);

	/*
	 * Add the event to the list (if there is room)
	 * and check whether the total set is still feasible.
	 */
	cpuhw = this_cpu_ptr(&cpu_hw_events);
	n0 = cpuhw->n_events;
	if (n0 >= ppmu->n_counter)
		goto out;
	cpuhw->event[n0] = event;
	cpuhw->events[n0] = event->hw.config;
	cpuhw->flags[n0] = event->hw.event_base;

	/*
	 * This event may have been disabled/stopped in record_and_restart()
	 * because we exceeded the ->event_limit. If re-starting the event,
	 * clear the ->hw.state (STOPPED and UPTODATE flags), so the user
	 * notification is re-enabled.
	 */
	if (!(ef_flags & PERF_EF_START))
		event->hw.state = PERF_HES_STOPPED | PERF_HES_UPTODATE;
	else
		event->hw.state = 0;

	/*
	 * If group events scheduling transaction was started,
	 * skip the schedulability test here, it will be performed
	 * at commit time(->commit_txn) as a whole
	 */
	if (cpuhw->txn_flags & PERF_PMU_TXN_ADD)
		goto nocheck;

	if (check_excludes(cpuhw->event, cpuhw->flags, n0, 1))
		goto out;
	if (power_check_constraints(cpuhw, cpuhw->events, cpuhw->flags, n0 + 1))
		goto out;
	event->hw.config = cpuhw->events[n0];

nocheck:
	ebb_event_add(event);

	++cpuhw->n_events;
	++cpuhw->n_added;

	ret = 0;
 out:
	if (has_branch_stack(event)) {
		power_pmu_bhrb_enable(event);
		cpuhw->bhrb_filter = ppmu->bhrb_filter_map(
					event->attr.branch_sample_type);
	}

	perf_pmu_enable(event->pmu);
	local_irq_restore(flags);
	return ret;
}

/*
 * Remove an event from the PMU.
 */
static void power_pmu_del(struct perf_event *event, int ef_flags)
{
	struct cpu_hw_events *cpuhw;
	long i;
	unsigned long flags;

	local_irq_save(flags);
	perf_pmu_disable(event->pmu);

	power_pmu_read(event);

	cpuhw = this_cpu_ptr(&cpu_hw_events);
	for (i = 0; i < cpuhw->n_events; ++i) {
		if (event == cpuhw->event[i]) {
			while (++i < cpuhw->n_events) {
				cpuhw->event[i-1] = cpuhw->event[i];
				cpuhw->events[i-1] = cpuhw->events[i];
				cpuhw->flags[i-1] = cpuhw->flags[i];
			}
			--cpuhw->n_events;
			ppmu->disable_pmc(event->hw.idx - 1, &cpuhw->mmcr);
			if (event->hw.idx) {
				write_pmc(event->hw.idx, 0);
				event->hw.idx = 0;
			}
			perf_event_update_userpage(event);
			break;
		}
	}
	for (i = 0; i < cpuhw->n_limited; ++i)
		if (event == cpuhw->limited_counter[i])
			break;
	if (i < cpuhw->n_limited) {
		while (++i < cpuhw->n_limited) {
			cpuhw->limited_counter[i-1] = cpuhw->limited_counter[i];
			cpuhw->limited_hwidx[i-1] = cpuhw->limited_hwidx[i];
		}
		--cpuhw->n_limited;
	}
	if (cpuhw->n_events == 0) {
		/* disable exceptions if no events are running */
		cpuhw->mmcr.mmcr0 &= ~(MMCR0_PMXE | MMCR0_FCECE);
	}

	if (has_branch_stack(event))
		power_pmu_bhrb_disable(event);

	perf_pmu_enable(event->pmu);
	local_irq_restore(flags);
}

/*
 * POWER-PMU does not support disabling individual counters, hence
 * program their cycle counter to their max value and ignore the interrupts.
 */

static void power_pmu_start(struct perf_event *event, int ef_flags)
{
	unsigned long flags;
	s64 left;
	unsigned long val;

	if (!event->hw.idx || !event->hw.sample_period)
		return;

	if (!(event->hw.state & PERF_HES_STOPPED))
		return;

	if (ef_flags & PERF_EF_RELOAD)
		WARN_ON_ONCE(!(event->hw.state & PERF_HES_UPTODATE));

	local_irq_save(flags);
	perf_pmu_disable(event->pmu);

	event->hw.state = 0;
	left = local64_read(&event->hw.period_left);

	val = 0;
	if (left < 0x80000000L)
		val = 0x80000000L - left;

	write_pmc(event->hw.idx, val);

	perf_event_update_userpage(event);
	perf_pmu_enable(event->pmu);
	local_irq_restore(flags);
}

static void power_pmu_stop(struct perf_event *event, int ef_flags)
{
	unsigned long flags;

	if (!event->hw.idx || !event->hw.sample_period)
		return;

	if (event->hw.state & PERF_HES_STOPPED)
		return;

	local_irq_save(flags);
	perf_pmu_disable(event->pmu);

	power_pmu_read(event);
	event->hw.state |= PERF_HES_STOPPED | PERF_HES_UPTODATE;
	write_pmc(event->hw.idx, 0);

	perf_event_update_userpage(event);
	perf_pmu_enable(event->pmu);
	local_irq_restore(flags);
}

/*
 * Start group events scheduling transaction
 * Set the flag to make pmu::enable() not perform the
 * schedulability test, it will be performed at commit time
 *
 * We only support PERF_PMU_TXN_ADD transactions. Save the
 * transaction flags but otherwise ignore non-PERF_PMU_TXN_ADD
 * transactions.
 */
static void power_pmu_start_txn(struct pmu *pmu, unsigned int txn_flags)
{
	struct cpu_hw_events *cpuhw = this_cpu_ptr(&cpu_hw_events);

	WARN_ON_ONCE(cpuhw->txn_flags);		/* txn already in flight */

	cpuhw->txn_flags = txn_flags;
	if (txn_flags & ~PERF_PMU_TXN_ADD)
		return;

	perf_pmu_disable(pmu);
	cpuhw->n_txn_start = cpuhw->n_events;
}

/*
 * Stop group events scheduling transaction
 * Clear the flag and pmu::enable() will perform the
 * schedulability test.
 */
static void power_pmu_cancel_txn(struct pmu *pmu)
{
	struct cpu_hw_events *cpuhw = this_cpu_ptr(&cpu_hw_events);
	unsigned int txn_flags;

	WARN_ON_ONCE(!cpuhw->txn_flags);	/* no txn in flight */

	txn_flags = cpuhw->txn_flags;
	cpuhw->txn_flags = 0;
	if (txn_flags & ~PERF_PMU_TXN_ADD)
		return;

	perf_pmu_enable(pmu);
}

/*
 * Commit group events scheduling transaction
 * Perform the group schedulability test as a whole
 * Return 0 if success
 */
static int power_pmu_commit_txn(struct pmu *pmu)
{
	struct cpu_hw_events *cpuhw;
	long i, n;

	if (!ppmu)
		return -EAGAIN;

	cpuhw = this_cpu_ptr(&cpu_hw_events);
	WARN_ON_ONCE(!cpuhw->txn_flags);	/* no txn in flight */

	if (cpuhw->txn_flags & ~PERF_PMU_TXN_ADD) {
		cpuhw->txn_flags = 0;
		return 0;
	}

	n = cpuhw->n_events;
	if (check_excludes(cpuhw->event, cpuhw->flags, 0, n))
		return -EAGAIN;
	i = power_check_constraints(cpuhw, cpuhw->events, cpuhw->flags, n);
	if (i < 0)
		return -EAGAIN;

	for (i = cpuhw->n_txn_start; i < n; ++i)
		cpuhw->event[i]->hw.config = cpuhw->events[i];

	cpuhw->txn_flags = 0;
	perf_pmu_enable(pmu);
	return 0;
}

/*
 * Return 1 if we might be able to put event on a limited PMC,
 * or 0 if not.
 * An event can only go on a limited PMC if it counts something
 * that a limited PMC can count, doesn't require interrupts, and
 * doesn't exclude any processor mode.
 */
static int can_go_on_limited_pmc(struct perf_event *event, u64 ev,
				 unsigned int flags)
{
	int n;
	u64 alt[MAX_EVENT_ALTERNATIVES];

	if (event->attr.exclude_user
	    || event->attr.exclude_kernel
	    || event->attr.exclude_hv
	    || event->attr.sample_period)
		return 0;

	if (ppmu->limited_pmc_event(ev))
		return 1;

	/*
	 * The requested event_id isn't on a limited PMC already;
	 * see if any alternative code goes on a limited PMC.
	 */
	if (!ppmu->get_alternatives)
		return 0;

	flags |= PPMU_LIMITED_PMC_OK | PPMU_LIMITED_PMC_REQD;
	n = ppmu->get_alternatives(ev, flags, alt);

	return n > 0;
}

/*
 * Find an alternative event_id that goes on a normal PMC, if possible,
 * and return the event_id code, or 0 if there is no such alternative.
 * (Note: event_id code 0 is "don't count" on all machines.)
 */
static u64 normal_pmc_alternative(u64 ev, unsigned long flags)
{
	u64 alt[MAX_EVENT_ALTERNATIVES];
	int n;

	flags &= ~(PPMU_LIMITED_PMC_OK | PPMU_LIMITED_PMC_REQD);
	n = ppmu->get_alternatives(ev, flags, alt);
	if (!n)
		return 0;
	return alt[0];
}

/* Number of perf_events counting hardware events */
static atomic_t num_events;
/* Used to avoid races in calling reserve/release_pmc_hardware */
static DEFINE_MUTEX(pmc_reserve_mutex);

/*
 * Release the PMU if this is the last perf_event.
 */
static void hw_perf_event_destroy(struct perf_event *event)
{
	if (!atomic_add_unless(&num_events, -1, 1)) {
		mutex_lock(&pmc_reserve_mutex);
		if (atomic_dec_return(&num_events) == 0)
			release_pmc_hardware();
		mutex_unlock(&pmc_reserve_mutex);
	}
}

/*
 * Translate a generic cache event_id config to a raw event_id code.
 */
static int hw_perf_cache_event(u64 config, u64 *eventp)
{
	unsigned long type, op, result;
	u64 ev;

	if (!ppmu->cache_events)
		return -EINVAL;

	/* unpack config */
	type = config & 0xff;
	op = (config >> 8) & 0xff;
	result = (config >> 16) & 0xff;

	if (type >= PERF_COUNT_HW_CACHE_MAX ||
	    op >= PERF_COUNT_HW_CACHE_OP_MAX ||
	    result >= PERF_COUNT_HW_CACHE_RESULT_MAX)
		return -EINVAL;

	ev = (*ppmu->cache_events)[type][op][result];
	if (ev == 0)
		return -EOPNOTSUPP;
	if (ev == -1)
		return -EINVAL;
	*eventp = ev;
	return 0;
}

static bool is_event_blacklisted(u64 ev)
{
	int i;

	for (i=0; i < ppmu->n_blacklist_ev; i++) {
		if (ppmu->blacklist_ev[i] == ev)
			return true;
	}

	return false;
}

static int power_pmu_event_init(struct perf_event *event)
{
	u64 ev;
	unsigned long flags;
	struct perf_event *ctrs[MAX_HWEVENTS];
	u64 events[MAX_HWEVENTS];
	unsigned int cflags[MAX_HWEVENTS];
	int n;
	int err;
	struct cpu_hw_events *cpuhw;
	u64 bhrb_filter;

	if (!ppmu)
		return -ENOENT;

	if (has_branch_stack(event)) {
	        /* PMU has BHRB enabled */
		if (!(ppmu->flags & PPMU_ARCH_207S))
			return -EOPNOTSUPP;
	}

	switch (event->attr.type) {
	case PERF_TYPE_HARDWARE:
		ev = event->attr.config;
		if (ev >= ppmu->n_generic || ppmu->generic_events[ev] == 0)
			return -EOPNOTSUPP;

		if (ppmu->blacklist_ev && is_event_blacklisted(ev))
			return -EINVAL;
		ev = ppmu->generic_events[ev];
		break;
	case PERF_TYPE_HW_CACHE:
		err = hw_perf_cache_event(event->attr.config, &ev);
		if (err)
			return err;

		if (ppmu->blacklist_ev && is_event_blacklisted(ev))
			return -EINVAL;
		break;
	case PERF_TYPE_RAW:
		ev = event->attr.config;

		if (ppmu->blacklist_ev && is_event_blacklisted(ev))
			return -EINVAL;
		break;
	default:
		return -ENOENT;
	}

	event->hw.config_base = ev;
	event->hw.idx = 0;

	/*
	 * If we are not running on a hypervisor, force the
	 * exclude_hv bit to 0 so that we don't care what
	 * the user set it to.
	 */
	if (!firmware_has_feature(FW_FEATURE_LPAR))
		event->attr.exclude_hv = 0;

	/*
	 * If this is a per-task event, then we can use
	 * PM_RUN_* events interchangeably with their non RUN_*
	 * equivalents, e.g. PM_RUN_CYC instead of PM_CYC.
	 * XXX we should check if the task is an idle task.
	 */
	flags = 0;
	if (event->attach_state & PERF_ATTACH_TASK)
		flags |= PPMU_ONLY_COUNT_RUN;

	/*
	 * If this machine has limited events, check whether this
	 * event_id could go on a limited event.
	 */
	if (ppmu->flags & PPMU_LIMITED_PMC5_6) {
		if (can_go_on_limited_pmc(event, ev, flags)) {
			flags |= PPMU_LIMITED_PMC_OK;
		} else if (ppmu->limited_pmc_event(ev)) {
			/*
			 * The requested event_id is on a limited PMC,
			 * but we can't use a limited PMC; see if any
			 * alternative goes on a normal PMC.
			 */
			ev = normal_pmc_alternative(ev, flags);
			if (!ev)
				return -EINVAL;
		}
	}

	/* Extra checks for EBB */
	err = ebb_event_check(event);
	if (err)
		return err;

	/*
	 * If this is in a group, check if it can go on with all the
	 * other hardware events in the group.  We assume the event
	 * hasn't been linked into its leader's sibling list at this point.
	 */
	n = 0;
	if (event->group_leader != event) {
		n = collect_events(event->group_leader, ppmu->n_counter - 1,
				   ctrs, events, cflags);
		if (n < 0)
			return -EINVAL;
	}
	events[n] = ev;
	ctrs[n] = event;
	cflags[n] = flags;
	if (check_excludes(ctrs, cflags, n, 1))
		return -EINVAL;

	cpuhw = &get_cpu_var(cpu_hw_events);
	err = power_check_constraints(cpuhw, events, cflags, n + 1);

	if (has_branch_stack(event)) {
		bhrb_filter = ppmu->bhrb_filter_map(
					event->attr.branch_sample_type);

		if (bhrb_filter == -1) {
			put_cpu_var(cpu_hw_events);
			return -EOPNOTSUPP;
		}
		cpuhw->bhrb_filter = bhrb_filter;
	}

	put_cpu_var(cpu_hw_events);
	if (err)
		return -EINVAL;

	event->hw.config = events[n];
	event->hw.event_base = cflags[n];
	event->hw.last_period = event->hw.sample_period;
	local64_set(&event->hw.period_left, event->hw.last_period);

	/*
	 * For EBB events we just context switch the PMC value, we don't do any
	 * of the sample_period logic. We use hw.prev_count for this.
	 */
	if (is_ebb_event(event))
		local64_set(&event->hw.prev_count, 0);

	/*
	 * See if we need to reserve the PMU.
	 * If no events are currently in use, then we have to take a
	 * mutex to ensure that we don't race with another task doing
	 * reserve_pmc_hardware or release_pmc_hardware.
	 */
	err = 0;
	if (!atomic_inc_not_zero(&num_events)) {
		mutex_lock(&pmc_reserve_mutex);
		if (atomic_read(&num_events) == 0 &&
		    reserve_pmc_hardware(perf_event_interrupt))
			err = -EBUSY;
		else
			atomic_inc(&num_events);
		mutex_unlock(&pmc_reserve_mutex);
	}
	event->destroy = hw_perf_event_destroy;

	return err;
}

static int power_pmu_event_idx(struct perf_event *event)
{
	return event->hw.idx;
}

ssize_t power_events_sysfs_show(struct device *dev,
				struct device_attribute *attr, char *page)
{
	struct perf_pmu_events_attr *pmu_attr;

	pmu_attr = container_of(attr, struct perf_pmu_events_attr, attr);

	return sprintf(page, "event=0x%02llx\n", pmu_attr->id);
}

static struct pmu power_pmu = {
	.pmu_enable	= power_pmu_enable,
	.pmu_disable	= power_pmu_disable,
	.event_init	= power_pmu_event_init,
	.add		= power_pmu_add,
	.del		= power_pmu_del,
	.start		= power_pmu_start,
	.stop		= power_pmu_stop,
	.read		= power_pmu_read,
	.start_txn	= power_pmu_start_txn,
	.cancel_txn	= power_pmu_cancel_txn,
	.commit_txn	= power_pmu_commit_txn,
	.event_idx	= power_pmu_event_idx,
	.sched_task	= power_pmu_sched_task,
};

/*
 * A counter has overflowed; update its count and record
 * things if requested.  Note that interrupts are hard-disabled
 * here so there is no possibility of being interrupted.
 */
static void record_and_restart(struct perf_event *event, unsigned long val,
			       struct pt_regs *regs)
{
	u64 period = event->hw.sample_period;
	s64 prev, delta, left;
	int record = 0;

	if (event->hw.state & PERF_HES_STOPPED) {
		write_pmc(event->hw.idx, 0);
		return;
	}

	/* we don't have to worry about interrupts here */
	prev = local64_read(&event->hw.prev_count);
	delta = check_and_compute_delta(prev, val);
	local64_add(delta, &event->count);

	/*
	 * See if the total period for this event has expired,
	 * and update for the next period.
	 */
	val = 0;
	left = local64_read(&event->hw.period_left) - delta;
	if (delta == 0)
		left++;
	if (period) {
		if (left <= 0) {
			left += period;
			if (left <= 0)
				left = period;
			record = siar_valid(regs);
			event->hw.last_period = event->hw.sample_period;
		}
		if (left < 0x80000000LL)
			val = 0x80000000LL - left;
	}

	write_pmc(event->hw.idx, val);
	local64_set(&event->hw.prev_count, val);
	local64_set(&event->hw.period_left, left);
	perf_event_update_userpage(event);

	/*
	 * Finally record data if requested.
	 */
	if (record) {
		struct perf_sample_data data;

		perf_sample_data_init(&data, ~0ULL, event->hw.last_period);

		if (event->attr.sample_type &
		    (PERF_SAMPLE_ADDR | PERF_SAMPLE_PHYS_ADDR))
			perf_get_data_addr(event, regs, &data.addr);

		if (event->attr.sample_type & PERF_SAMPLE_BRANCH_STACK) {
			struct cpu_hw_events *cpuhw;
			cpuhw = this_cpu_ptr(&cpu_hw_events);
			power_pmu_bhrb_read(event, cpuhw);
			data.br_stack = &cpuhw->bhrb_stack;
		}

		if (event->attr.sample_type & PERF_SAMPLE_DATA_SRC &&
						ppmu->get_mem_data_src)
			ppmu->get_mem_data_src(&data.data_src, ppmu->flags, regs);

		if (event->attr.sample_type & PERF_SAMPLE_WEIGHT &&
						ppmu->get_mem_weight)
			ppmu->get_mem_weight(&data.weight);

		if (perf_event_overflow(event, &data, regs))
			power_pmu_stop(event, 0);
	}
}

/*
 * Called from generic code to get the misc flags (i.e. processor mode)
 * for an event_id.
 */
unsigned long perf_misc_flags(struct pt_regs *regs)
{
	u32 flags = perf_get_misc_flags(regs);

	if (flags)
		return flags;
	return user_mode(regs) ? PERF_RECORD_MISC_USER :
		PERF_RECORD_MISC_KERNEL;
}

/*
 * Called from generic code to get the instruction pointer
 * for an event_id.
 */
unsigned long perf_instruction_pointer(struct pt_regs *regs)
{
	bool use_siar = regs_use_siar(regs);

	if (use_siar && siar_valid(regs))
		return mfspr(SPRN_SIAR) + perf_ip_adjust(regs);
	else if (use_siar)
		return 0;		// no valid instruction pointer
	else
		return regs->nip;
}

static bool pmc_overflow_power7(unsigned long val)
{
	/*
	 * Events on POWER7 can roll back if a speculative event doesn't
	 * eventually complete. Unfortunately in some rare cases they will
	 * raise a performance monitor exception. We need to catch this to
	 * ensure we reset the PMC. In all cases the PMC will be 256 or less
	 * cycles from overflow.
	 *
	 * We only do this if the first pass fails to find any overflowing
	 * PMCs because a user might set a period of less than 256 and we
	 * don't want to mistakenly reset them.
	 */
	if ((0x80000000 - val) <= 256)
		return true;

	return false;
}

static bool pmc_overflow(unsigned long val)
{
	if ((int)val < 0)
		return true;

	return false;
}

/*
 * Performance monitor interrupt stuff
 */
static void __perf_event_interrupt(struct pt_regs *regs)
{
	int i, j;
	struct cpu_hw_events *cpuhw = this_cpu_ptr(&cpu_hw_events);
	struct perf_event *event;
	unsigned long val[8];
	int found, active;
	int nmi;

	if (cpuhw->n_limited)
		freeze_limited_counters(cpuhw, mfspr(SPRN_PMC5),
					mfspr(SPRN_PMC6));

	perf_read_regs(regs);

	/*
	 * If perf interrupts hit in a local_irq_disable (soft-masked) region,
	 * we consider them as NMIs. This is required to prevent hash faults on
	 * user addresses when reading callchains. See the NMI test in
	 * do_hash_page.
	 */
	nmi = perf_intr_is_nmi(regs);
	if (nmi)
		nmi_enter();
	else
		irq_enter();

	/* Read all the PMCs since we'll need them a bunch of times */
	for (i = 0; i < ppmu->n_counter; ++i)
		val[i] = read_pmc(i + 1);

	/* Try to find what caused the IRQ */
	found = 0;
	for (i = 0; i < ppmu->n_counter; ++i) {
		if (!pmc_overflow(val[i]))
			continue;
		if (is_limited_pmc(i + 1))
			continue; /* these won't generate IRQs */
		/*
		 * We've found one that's overflowed.  For active
		 * counters we need to log this.  For inactive
		 * counters, we need to reset it anyway
		 */
		found = 1;
		active = 0;
		for (j = 0; j < cpuhw->n_events; ++j) {
			event = cpuhw->event[j];
			if (event->hw.idx == (i + 1)) {
				active = 1;
				record_and_restart(event, val[i], regs);
				break;
			}
		}
		if (!active)
			/* reset non active counters that have overflowed */
			write_pmc(i + 1, 0);
	}
	if (!found && pvr_version_is(PVR_POWER7)) {
		/* check active counters for special buggy p7 overflow */
		for (i = 0; i < cpuhw->n_events; ++i) {
			event = cpuhw->event[i];
			if (!event->hw.idx || is_limited_pmc(event->hw.idx))
				continue;
			if (pmc_overflow_power7(val[event->hw.idx - 1])) {
				/* event has overflowed in a buggy way*/
				found = 1;
				record_and_restart(event,
						   val[event->hw.idx - 1],
						   regs);
			}
		}
	}
	if (!found && !nmi && printk_ratelimit())
		printk(KERN_WARNING "Can't find PMC that caused IRQ\n");

	/*
	 * Reset MMCR0 to its normal value.  This will set PMXE and
	 * clear FC (freeze counters) and PMAO (perf mon alert occurred)
	 * and thus allow interrupts to occur again.
	 * XXX might want to use MSR.PM to keep the events frozen until
	 * we get back out of this interrupt.
	 */
	write_mmcr0(cpuhw, cpuhw->mmcr.mmcr0);

	if (nmi)
		nmi_exit();
	else
		irq_exit();
}

static void perf_event_interrupt(struct pt_regs *regs)
{
	u64 start_clock = sched_clock();

	__perf_event_interrupt(regs);
	perf_sample_event_took(sched_clock() - start_clock);
}

static int power_pmu_prepare_cpu(unsigned int cpu)
{
	struct cpu_hw_events *cpuhw = &per_cpu(cpu_hw_events, cpu);

	if (ppmu) {
		memset(cpuhw, 0, sizeof(*cpuhw));
		cpuhw->mmcr.mmcr0 = MMCR0_FC;
	}
	return 0;
}

int register_power_pmu(struct power_pmu *pmu)
{
	if (ppmu)
		return -EBUSY;		/* something's already registered */

	ppmu = pmu;
	pr_info("%s performance monitor hardware support registered\n",
		pmu->name);

	power_pmu.attr_groups = ppmu->attr_groups;

#ifdef MSR_HV
	/*
	 * Use FCHV to ignore kernel events if MSR.HV is set.
	 */
	if (mfmsr() & MSR_HV)
		freeze_events_kernel = MMCR0_FCHV;
#endif /* CONFIG_PPC64 */

	perf_pmu_register(&power_pmu, "cpu", PERF_TYPE_RAW);
	cpuhp_setup_state(CPUHP_PERF_POWER, "perf/powerpc:prepare",
			  power_pmu_prepare_cpu, NULL);
	return 0;
}

#ifdef CONFIG_PPC64
static int __init init_ppc64_pmu(void)
{
	/* run through all the pmu drivers one at a time */
	if (!init_power5_pmu())
		return 0;
	else if (!init_power5p_pmu())
		return 0;
	else if (!init_power6_pmu())
		return 0;
	else if (!init_power7_pmu())
		return 0;
	else if (!init_power8_pmu())
		return 0;
	else if (!init_power9_pmu())
		return 0;
	else if (!init_power10_pmu())
		return 0;
	else if (!init_ppc970_pmu())
		return 0;
	else
		return init_generic_compat_pmu();
}
early_initcall(init_ppc64_pmu);
#endif
