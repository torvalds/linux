// SPDX-License-Identifier: GPL-2.0-only
#include <linux/init.h>

#include <linux/mm.h>
#include <linux/spinlock.h>
#include <linux/smp.h>
#include <linux/interrupt.h>
#include <linux/export.h>
#include <linux/cpu.h>
#include <linux/debugfs.h>

#include <asm/tlbflush.h>
#include <asm/mmu_context.h>
#include <asm/nospec-branch.h>
#include <asm/cache.h>
#include <asm/apic.h>

#include "mm_internal.h"

#ifdef CONFIG_PARAVIRT
# define STATIC_NOPV
#else
# define STATIC_NOPV			static
# define __flush_tlb_local		native_flush_tlb_local
# define __flush_tlb_global		native_flush_tlb_global
# define __flush_tlb_one_user(addr)	native_flush_tlb_one_user(addr)
# define __flush_tlb_others(msk, info)	native_flush_tlb_others(msk, info)
#endif

/*
 *	TLB flushing, formerly SMP-only
 *		c/o Linus Torvalds.
 *
 *	These mean you can really definitely utterly forget about
 *	writing to user space from interrupts. (Its not allowed anyway).
 *
 *	Optimizations Manfred Spraul <manfred@colorfullife.com>
 *
 *	More scalable flush, from Andi Kleen
 *
 *	Implement flush IPI by CALL_FUNCTION_VECTOR, Alex Shi
 */

/*
 * Use bit 0 to mangle the TIF_SPEC_IB state into the mm pointer which is
 * stored in cpu_tlb_state.last_user_mm_ibpb.
 */
#define LAST_USER_MM_IBPB	0x1UL

/*
 * The x86 feature is called PCID (Process Context IDentifier). It is similar
 * to what is traditionally called ASID on the RISC processors.
 *
 * We don't use the traditional ASID implementation, where each process/mm gets
 * its own ASID and flush/restart when we run out of ASID space.
 *
 * Instead we have a small per-cpu array of ASIDs and cache the last few mm's
 * that came by on this CPU, allowing cheaper switch_mm between processes on
 * this CPU.
 *
 * We end up with different spaces for different things. To avoid confusion we
 * use different names for each of them:
 *
 * ASID  - [0, TLB_NR_DYN_ASIDS-1]
 *         the canonical identifier for an mm
 *
 * kPCID - [1, TLB_NR_DYN_ASIDS]
 *         the value we write into the PCID part of CR3; corresponds to the
 *         ASID+1, because PCID 0 is special.
 *
 * uPCID - [2048 + 1, 2048 + TLB_NR_DYN_ASIDS]
 *         for KPTI each mm has two address spaces and thus needs two
 *         PCID values, but we can still do with a single ASID denomination
 *         for each mm. Corresponds to kPCID + 2048.
 *
 */

/* There are 12 bits of space for ASIDS in CR3 */
#define CR3_HW_ASID_BITS		12

/*
 * When enabled, PAGE_TABLE_ISOLATION consumes a single bit for
 * user/kernel switches
 */
#ifdef CONFIG_PAGE_TABLE_ISOLATION
# define PTI_CONSUMED_PCID_BITS	1
#else
# define PTI_CONSUMED_PCID_BITS	0
#endif

#define CR3_AVAIL_PCID_BITS (X86_CR3_PCID_BITS - PTI_CONSUMED_PCID_BITS)

/*
 * ASIDs are zero-based: 0->MAX_AVAIL_ASID are valid.  -1 below to account
 * for them being zero-based.  Another -1 is because PCID 0 is reserved for
 * use by non-PCID-aware users.
 */
#define MAX_ASID_AVAILABLE ((1 << CR3_AVAIL_PCID_BITS) - 2)

/*
 * Given @asid, compute kPCID
 */
static inline u16 kern_pcid(u16 asid)
{
	VM_WARN_ON_ONCE(asid > MAX_ASID_AVAILABLE);

#ifdef CONFIG_PAGE_TABLE_ISOLATION
	/*
	 * Make sure that the dynamic ASID space does not confict with the
	 * bit we are using to switch between user and kernel ASIDs.
	 */
	BUILD_BUG_ON(TLB_NR_DYN_ASIDS >= (1 << X86_CR3_PTI_PCID_USER_BIT));

	/*
	 * The ASID being passed in here should have respected the
	 * MAX_ASID_AVAILABLE and thus never have the switch bit set.
	 */
	VM_WARN_ON_ONCE(asid & (1 << X86_CR3_PTI_PCID_USER_BIT));
#endif
	/*
	 * The dynamically-assigned ASIDs that get passed in are small
	 * (<TLB_NR_DYN_ASIDS).  They never have the high switch bit set,
	 * so do not bother to clear it.
	 *
	 * If PCID is on, ASID-aware code paths put the ASID+1 into the
	 * PCID bits.  This serves two purposes.  It prevents a nasty
	 * situation in which PCID-unaware code saves CR3, loads some other
	 * value (with PCID == 0), and then restores CR3, thus corrupting
	 * the TLB for ASID 0 if the saved ASID was nonzero.  It also means
	 * that any bugs involving loading a PCID-enabled CR3 with
	 * CR4.PCIDE off will trigger deterministically.
	 */
	return asid + 1;
}

/*
 * Given @asid, compute uPCID
 */
static inline u16 user_pcid(u16 asid)
{
	u16 ret = kern_pcid(asid);
#ifdef CONFIG_PAGE_TABLE_ISOLATION
	ret |= 1 << X86_CR3_PTI_PCID_USER_BIT;
#endif
	return ret;
}

static inline unsigned long build_cr3(pgd_t *pgd, u16 asid)
{
	if (static_cpu_has(X86_FEATURE_PCID)) {
		return __sme_pa(pgd) | kern_pcid(asid);
	} else {
		VM_WARN_ON_ONCE(asid != 0);
		return __sme_pa(pgd);
	}
}

static inline unsigned long build_cr3_noflush(pgd_t *pgd, u16 asid)
{
	VM_WARN_ON_ONCE(asid > MAX_ASID_AVAILABLE);
	/*
	 * Use boot_cpu_has() instead of this_cpu_has() as this function
	 * might be called during early boot. This should work even after
	 * boot because all CPU's the have same capabilities:
	 */
	VM_WARN_ON_ONCE(!boot_cpu_has(X86_FEATURE_PCID));
	return __sme_pa(pgd) | kern_pcid(asid) | CR3_NOFLUSH;
}

/*
 * We get here when we do something requiring a TLB invalidation
 * but could not go invalidate all of the contexts.  We do the
 * necessary invalidation by clearing out the 'ctx_id' which
 * forces a TLB flush when the context is loaded.
 */
static void clear_asid_other(void)
{
	u16 asid;

	/*
	 * This is only expected to be set if we have disabled
	 * kernel _PAGE_GLOBAL pages.
	 */
	if (!static_cpu_has(X86_FEATURE_PTI)) {
		WARN_ON_ONCE(1);
		return;
	}

	for (asid = 0; asid < TLB_NR_DYN_ASIDS; asid++) {
		/* Do not need to flush the current asid */
		if (asid == this_cpu_read(cpu_tlbstate.loaded_mm_asid))
			continue;
		/*
		 * Make sure the next time we go to switch to
		 * this asid, we do a flush:
		 */
		this_cpu_write(cpu_tlbstate.ctxs[asid].ctx_id, 0);
	}
	this_cpu_write(cpu_tlbstate.invalidate_other, false);
}

atomic64_t last_mm_ctx_id = ATOMIC64_INIT(1);


static void choose_new_asid(struct mm_struct *next, u64 next_tlb_gen,
			    u16 *new_asid, bool *need_flush)
{
	u16 asid;

	if (!static_cpu_has(X86_FEATURE_PCID)) {
		*new_asid = 0;
		*need_flush = true;
		return;
	}

	if (this_cpu_read(cpu_tlbstate.invalidate_other))
		clear_asid_other();

	for (asid = 0; asid < TLB_NR_DYN_ASIDS; asid++) {
		if (this_cpu_read(cpu_tlbstate.ctxs[asid].ctx_id) !=
		    next->context.ctx_id)
			continue;

		*new_asid = asid;
		*need_flush = (this_cpu_read(cpu_tlbstate.ctxs[asid].tlb_gen) <
			       next_tlb_gen);
		return;
	}

	/*
	 * We don't currently own an ASID slot on this CPU.
	 * Allocate a slot.
	 */
	*new_asid = this_cpu_add_return(cpu_tlbstate.next_asid, 1) - 1;
	if (*new_asid >= TLB_NR_DYN_ASIDS) {
		*new_asid = 0;
		this_cpu_write(cpu_tlbstate.next_asid, 1);
	}
	*need_flush = true;
}

/*
 * Given an ASID, flush the corresponding user ASID.  We can delay this
 * until the next time we switch to it.
 *
 * See SWITCH_TO_USER_CR3.
 */
static inline void invalidate_user_asid(u16 asid)
{
	/* There is no user ASID if address space separation is off */
	if (!IS_ENABLED(CONFIG_PAGE_TABLE_ISOLATION))
		return;

	/*
	 * We only have a single ASID if PCID is off and the CR3
	 * write will have flushed it.
	 */
	if (!cpu_feature_enabled(X86_FEATURE_PCID))
		return;

	if (!static_cpu_has(X86_FEATURE_PTI))
		return;

	__set_bit(kern_pcid(asid),
		  (unsigned long *)this_cpu_ptr(&cpu_tlbstate.user_pcid_flush_mask));
}

static void load_new_mm_cr3(pgd_t *pgdir, u16 new_asid, bool need_flush)
{
	unsigned long new_mm_cr3;

	if (need_flush) {
		invalidate_user_asid(new_asid);
		new_mm_cr3 = build_cr3(pgdir, new_asid);
	} else {
		new_mm_cr3 = build_cr3_noflush(pgdir, new_asid);
	}

	/*
	 * Caution: many callers of this function expect
	 * that load_cr3() is serializing and orders TLB
	 * fills with respect to the mm_cpumask writes.
	 */
	write_cr3(new_mm_cr3);
}

void leave_mm(int cpu)
{
	struct mm_struct *loaded_mm = this_cpu_read(cpu_tlbstate.loaded_mm);

	/*
	 * It's plausible that we're in lazy TLB mode while our mm is init_mm.
	 * If so, our callers still expect us to flush the TLB, but there
	 * aren't any user TLB entries in init_mm to worry about.
	 *
	 * This needs to happen before any other sanity checks due to
	 * intel_idle's shenanigans.
	 */
	if (loaded_mm == &init_mm)
		return;

	/* Warn if we're not lazy. */
	WARN_ON(!this_cpu_read(cpu_tlbstate.is_lazy));

	switch_mm(NULL, &init_mm, NULL);
}
EXPORT_SYMBOL_GPL(leave_mm);

void switch_mm(struct mm_struct *prev, struct mm_struct *next,
	       struct task_struct *tsk)
{
	unsigned long flags;

	local_irq_save(flags);
	switch_mm_irqs_off(prev, next, tsk);
	local_irq_restore(flags);
}

static inline unsigned long mm_mangle_tif_spec_ib(struct task_struct *next)
{
	unsigned long next_tif = task_thread_info(next)->flags;
	unsigned long ibpb = (next_tif >> TIF_SPEC_IB) & LAST_USER_MM_IBPB;

	return (unsigned long)next->mm | ibpb;
}

static void cond_ibpb(struct task_struct *next)
{
	if (!next || !next->mm)
		return;

	/*
	 * Both, the conditional and the always IBPB mode use the mm
	 * pointer to avoid the IBPB when switching between tasks of the
	 * same process. Using the mm pointer instead of mm->context.ctx_id
	 * opens a hypothetical hole vs. mm_struct reuse, which is more or
	 * less impossible to control by an attacker. Aside of that it
	 * would only affect the first schedule so the theoretically
	 * exposed data is not really interesting.
	 */
	if (static_branch_likely(&switch_mm_cond_ibpb)) {
		unsigned long prev_mm, next_mm;

		/*
		 * This is a bit more complex than the always mode because
		 * it has to handle two cases:
		 *
		 * 1) Switch from a user space task (potential attacker)
		 *    which has TIF_SPEC_IB set to a user space task
		 *    (potential victim) which has TIF_SPEC_IB not set.
		 *
		 * 2) Switch from a user space task (potential attacker)
		 *    which has TIF_SPEC_IB not set to a user space task
		 *    (potential victim) which has TIF_SPEC_IB set.
		 *
		 * This could be done by unconditionally issuing IBPB when
		 * a task which has TIF_SPEC_IB set is either scheduled in
		 * or out. Though that results in two flushes when:
		 *
		 * - the same user space task is scheduled out and later
		 *   scheduled in again and only a kernel thread ran in
		 *   between.
		 *
		 * - a user space task belonging to the same process is
		 *   scheduled in after a kernel thread ran in between
		 *
		 * - a user space task belonging to the same process is
		 *   scheduled in immediately.
		 *
		 * Optimize this with reasonably small overhead for the
		 * above cases. Mangle the TIF_SPEC_IB bit into the mm
		 * pointer of the incoming task which is stored in
		 * cpu_tlbstate.last_user_mm_ibpb for comparison.
		 */
		next_mm = mm_mangle_tif_spec_ib(next);
		prev_mm = this_cpu_read(cpu_tlbstate.last_user_mm_ibpb);

		/*
		 * Issue IBPB only if the mm's are different and one or
		 * both have the IBPB bit set.
		 */
		if (next_mm != prev_mm &&
		    (next_mm | prev_mm) & LAST_USER_MM_IBPB)
			indirect_branch_prediction_barrier();

		this_cpu_write(cpu_tlbstate.last_user_mm_ibpb, next_mm);
	}

	if (static_branch_unlikely(&switch_mm_always_ibpb)) {
		/*
		 * Only flush when switching to a user space task with a
		 * different context than the user space task which ran
		 * last on this CPU.
		 */
		if (this_cpu_read(cpu_tlbstate.last_user_mm) != next->mm) {
			indirect_branch_prediction_barrier();
			this_cpu_write(cpu_tlbstate.last_user_mm, next->mm);
		}
	}
}

#ifdef CONFIG_PERF_EVENTS
static inline void cr4_update_pce_mm(struct mm_struct *mm)
{
	if (static_branch_unlikely(&rdpmc_always_available_key) ||
	    (!static_branch_unlikely(&rdpmc_never_available_key) &&
	     atomic_read(&mm->context.perf_rdpmc_allowed)))
		cr4_set_bits_irqsoff(X86_CR4_PCE);
	else
		cr4_clear_bits_irqsoff(X86_CR4_PCE);
}

void cr4_update_pce(void *ignored)
{
	cr4_update_pce_mm(this_cpu_read(cpu_tlbstate.loaded_mm));
}

#else
static inline void cr4_update_pce_mm(struct mm_struct *mm) { }
#endif

void switch_mm_irqs_off(struct mm_struct *prev, struct mm_struct *next,
			struct task_struct *tsk)
{
	struct mm_struct *real_prev = this_cpu_read(cpu_tlbstate.loaded_mm);
	u16 prev_asid = this_cpu_read(cpu_tlbstate.loaded_mm_asid);
	bool was_lazy = this_cpu_read(cpu_tlbstate.is_lazy);
	unsigned cpu = smp_processor_id();
	u64 next_tlb_gen;
	bool need_flush;
	u16 new_asid;

	/*
	 * NB: The scheduler will call us with prev == next when switching
	 * from lazy TLB mode to normal mode if active_mm isn't changing.
	 * When this happens, we don't assume that CR3 (and hence
	 * cpu_tlbstate.loaded_mm) matches next.
	 *
	 * NB: leave_mm() calls us with prev == NULL and tsk == NULL.
	 */

	/* We don't want flush_tlb_func() to run concurrently with us. */
	if (IS_ENABLED(CONFIG_PROVE_LOCKING))
		WARN_ON_ONCE(!irqs_disabled());

	/*
	 * Verify that CR3 is what we think it is.  This will catch
	 * hypothetical buggy code that directly switches to swapper_pg_dir
	 * without going through leave_mm() / switch_mm_irqs_off() or that
	 * does something like write_cr3(read_cr3_pa()).
	 *
	 * Only do this check if CONFIG_DEBUG_VM=y because __read_cr3()
	 * isn't free.
	 */
#ifdef CONFIG_DEBUG_VM
	if (WARN_ON_ONCE(__read_cr3() != build_cr3(real_prev->pgd, prev_asid))) {
		/*
		 * If we were to BUG here, we'd be very likely to kill
		 * the system so hard that we don't see the call trace.
		 * Try to recover instead by ignoring the error and doing
		 * a global flush to minimize the chance of corruption.
		 *
		 * (This is far from being a fully correct recovery.
		 *  Architecturally, the CPU could prefetch something
		 *  back into an incorrect ASID slot and leave it there
		 *  to cause trouble down the road.  It's better than
		 *  nothing, though.)
		 */
		__flush_tlb_all();
	}
#endif
	this_cpu_write(cpu_tlbstate.is_lazy, false);

	/*
	 * The membarrier system call requires a full memory barrier and
	 * core serialization before returning to user-space, after
	 * storing to rq->curr, when changing mm.  This is because
	 * membarrier() sends IPIs to all CPUs that are in the target mm
	 * to make them issue memory barriers.  However, if another CPU
	 * switches to/from the target mm concurrently with
	 * membarrier(), it can cause that CPU not to receive an IPI
	 * when it really should issue a memory barrier.  Writing to CR3
	 * provides that full memory barrier and core serializing
	 * instruction.
	 */
	if (real_prev == next) {
		VM_WARN_ON(this_cpu_read(cpu_tlbstate.ctxs[prev_asid].ctx_id) !=
			   next->context.ctx_id);

		/*
		 * Even in lazy TLB mode, the CPU should stay set in the
		 * mm_cpumask. The TLB shootdown code can figure out from
		 * from cpu_tlbstate.is_lazy whether or not to send an IPI.
		 */
		if (WARN_ON_ONCE(real_prev != &init_mm &&
				 !cpumask_test_cpu(cpu, mm_cpumask(next))))
			cpumask_set_cpu(cpu, mm_cpumask(next));

		/*
		 * If the CPU is not in lazy TLB mode, we are just switching
		 * from one thread in a process to another thread in the same
		 * process. No TLB flush required.
		 */
		if (!was_lazy)
			return;

		/*
		 * Read the tlb_gen to check whether a flush is needed.
		 * If the TLB is up to date, just use it.
		 * The barrier synchronizes with the tlb_gen increment in
		 * the TLB shootdown code.
		 */
		smp_mb();
		next_tlb_gen = atomic64_read(&next->context.tlb_gen);
		if (this_cpu_read(cpu_tlbstate.ctxs[prev_asid].tlb_gen) ==
				next_tlb_gen)
			return;

		/*
		 * TLB contents went out of date while we were in lazy
		 * mode. Fall through to the TLB switching code below.
		 */
		new_asid = prev_asid;
		need_flush = true;
	} else {
		/*
		 * Avoid user/user BTB poisoning by flushing the branch
		 * predictor when switching between processes. This stops
		 * one process from doing Spectre-v2 attacks on another.
		 */
		cond_ibpb(tsk);

		/*
		 * Stop remote flushes for the previous mm.
		 * Skip kernel threads; we never send init_mm TLB flushing IPIs,
		 * but the bitmap manipulation can cause cache line contention.
		 */
		if (real_prev != &init_mm) {
			VM_WARN_ON_ONCE(!cpumask_test_cpu(cpu,
						mm_cpumask(real_prev)));
			cpumask_clear_cpu(cpu, mm_cpumask(real_prev));
		}

		/*
		 * Start remote flushes and then read tlb_gen.
		 */
		if (next != &init_mm)
			cpumask_set_cpu(cpu, mm_cpumask(next));
		next_tlb_gen = atomic64_read(&next->context.tlb_gen);

		choose_new_asid(next, next_tlb_gen, &new_asid, &need_flush);

		/* Let nmi_uaccess_okay() know that we're changing CR3. */
		this_cpu_write(cpu_tlbstate.loaded_mm, LOADED_MM_SWITCHING);
		barrier();
	}

	if (need_flush) {
		this_cpu_write(cpu_tlbstate.ctxs[new_asid].ctx_id, next->context.ctx_id);
		this_cpu_write(cpu_tlbstate.ctxs[new_asid].tlb_gen, next_tlb_gen);
		load_new_mm_cr3(next->pgd, new_asid, true);

		trace_tlb_flush(TLB_FLUSH_ON_TASK_SWITCH, TLB_FLUSH_ALL);
	} else {
		/* The new ASID is already up to date. */
		load_new_mm_cr3(next->pgd, new_asid, false);

		trace_tlb_flush(TLB_FLUSH_ON_TASK_SWITCH, 0);
	}

	/* Make sure we write CR3 before loaded_mm. */
	barrier();

	this_cpu_write(cpu_tlbstate.loaded_mm, next);
	this_cpu_write(cpu_tlbstate.loaded_mm_asid, new_asid);

	if (next != real_prev) {
		cr4_update_pce_mm(next);
		switch_ldt(real_prev, next);
	}
}

/*
 * Please ignore the name of this function.  It should be called
 * switch_to_kernel_thread().
 *
 * enter_lazy_tlb() is a hint from the scheduler that we are entering a
 * kernel thread or other context without an mm.  Acceptable implementations
 * include doing nothing whatsoever, switching to init_mm, or various clever
 * lazy tricks to try to minimize TLB flushes.
 *
 * The scheduler reserves the right to call enter_lazy_tlb() several times
 * in a row.  It will notify us that we're going back to a real mm by
 * calling switch_mm_irqs_off().
 */
void enter_lazy_tlb(struct mm_struct *mm, struct task_struct *tsk)
{
	if (this_cpu_read(cpu_tlbstate.loaded_mm) == &init_mm)
		return;

	this_cpu_write(cpu_tlbstate.is_lazy, true);
}

/*
 * Call this when reinitializing a CPU.  It fixes the following potential
 * problems:
 *
 * - The ASID changed from what cpu_tlbstate thinks it is (most likely
 *   because the CPU was taken down and came back up with CR3's PCID
 *   bits clear.  CPU hotplug can do this.
 *
 * - The TLB contains junk in slots corresponding to inactive ASIDs.
 *
 * - The CPU went so far out to lunch that it may have missed a TLB
 *   flush.
 */
void initialize_tlbstate_and_flush(void)
{
	int i;
	struct mm_struct *mm = this_cpu_read(cpu_tlbstate.loaded_mm);
	u64 tlb_gen = atomic64_read(&init_mm.context.tlb_gen);
	unsigned long cr3 = __read_cr3();

	/* Assert that CR3 already references the right mm. */
	WARN_ON((cr3 & CR3_ADDR_MASK) != __pa(mm->pgd));

	/*
	 * Assert that CR4.PCIDE is set if needed.  (CR4.PCIDE initialization
	 * doesn't work like other CR4 bits because it can only be set from
	 * long mode.)
	 */
	WARN_ON(boot_cpu_has(X86_FEATURE_PCID) &&
		!(cr4_read_shadow() & X86_CR4_PCIDE));

	/* Force ASID 0 and force a TLB flush. */
	write_cr3(build_cr3(mm->pgd, 0));

	/* Reinitialize tlbstate. */
	this_cpu_write(cpu_tlbstate.last_user_mm_ibpb, LAST_USER_MM_IBPB);
	this_cpu_write(cpu_tlbstate.loaded_mm_asid, 0);
	this_cpu_write(cpu_tlbstate.next_asid, 1);
	this_cpu_write(cpu_tlbstate.ctxs[0].ctx_id, mm->context.ctx_id);
	this_cpu_write(cpu_tlbstate.ctxs[0].tlb_gen, tlb_gen);

	for (i = 1; i < TLB_NR_DYN_ASIDS; i++)
		this_cpu_write(cpu_tlbstate.ctxs[i].ctx_id, 0);
}

/*
 * flush_tlb_func()'s memory ordering requirement is that any
 * TLB fills that happen after we flush the TLB are ordered after we
 * read active_mm's tlb_gen.  We don't need any explicit barriers
 * because all x86 flush operations are serializing and the
 * atomic64_read operation won't be reordered by the compiler.
 */
static void flush_tlb_func(void *info)
{
	/*
	 * We have three different tlb_gen values in here.  They are:
	 *
	 * - mm_tlb_gen:     the latest generation.
	 * - local_tlb_gen:  the generation that this CPU has already caught
	 *                   up to.
	 * - f->new_tlb_gen: the generation that the requester of the flush
	 *                   wants us to catch up to.
	 */
	const struct flush_tlb_info *f = info;
	struct mm_struct *loaded_mm = this_cpu_read(cpu_tlbstate.loaded_mm);
	u32 loaded_mm_asid = this_cpu_read(cpu_tlbstate.loaded_mm_asid);
	u64 mm_tlb_gen = atomic64_read(&loaded_mm->context.tlb_gen);
	u64 local_tlb_gen = this_cpu_read(cpu_tlbstate.ctxs[loaded_mm_asid].tlb_gen);
	bool local = smp_processor_id() == f->initiating_cpu;
	unsigned long nr_invalidate = 0;

	/* This code cannot presently handle being reentered. */
	VM_WARN_ON(!irqs_disabled());

	if (!local) {
		inc_irq_stat(irq_tlb_count);
		count_vm_tlb_event(NR_TLB_REMOTE_FLUSH_RECEIVED);

		/* Can only happen on remote CPUs */
		if (f->mm && f->mm != loaded_mm)
			return;
	}

	if (unlikely(loaded_mm == &init_mm))
		return;

	VM_WARN_ON(this_cpu_read(cpu_tlbstate.ctxs[loaded_mm_asid].ctx_id) !=
		   loaded_mm->context.ctx_id);

	if (this_cpu_read(cpu_tlbstate.is_lazy)) {
		/*
		 * We're in lazy mode.  We need to at least flush our
		 * paging-structure cache to avoid speculatively reading
		 * garbage into our TLB.  Since switching to init_mm is barely
		 * slower than a minimal flush, just switch to init_mm.
		 *
		 * This should be rare, with native_flush_tlb_others skipping
		 * IPIs to lazy TLB mode CPUs.
		 */
		switch_mm_irqs_off(NULL, &init_mm, NULL);
		return;
	}

	if (unlikely(local_tlb_gen == mm_tlb_gen)) {
		/*
		 * There's nothing to do: we're already up to date.  This can
		 * happen if two concurrent flushes happen -- the first flush to
		 * be handled can catch us all the way up, leaving no work for
		 * the second flush.
		 */
		goto done;
	}

	WARN_ON_ONCE(local_tlb_gen > mm_tlb_gen);
	WARN_ON_ONCE(f->new_tlb_gen > mm_tlb_gen);

	/*
	 * If we get to this point, we know that our TLB is out of date.
	 * This does not strictly imply that we need to flush (it's
	 * possible that f->new_tlb_gen <= local_tlb_gen), but we're
	 * going to need to flush in the very near future, so we might
	 * as well get it over with.
	 *
	 * The only question is whether to do a full or partial flush.
	 *
	 * We do a partial flush if requested and two extra conditions
	 * are met:
	 *
	 * 1. f->new_tlb_gen == local_tlb_gen + 1.  We have an invariant that
	 *    we've always done all needed flushes to catch up to
	 *    local_tlb_gen.  If, for example, local_tlb_gen == 2 and
	 *    f->new_tlb_gen == 3, then we know that the flush needed to bring
	 *    us up to date for tlb_gen 3 is the partial flush we're
	 *    processing.
	 *
	 *    As an example of why this check is needed, suppose that there
	 *    are two concurrent flushes.  The first is a full flush that
	 *    changes context.tlb_gen from 1 to 2.  The second is a partial
	 *    flush that changes context.tlb_gen from 2 to 3.  If they get
	 *    processed on this CPU in reverse order, we'll see
	 *     local_tlb_gen == 1, mm_tlb_gen == 3, and end != TLB_FLUSH_ALL.
	 *    If we were to use __flush_tlb_one_user() and set local_tlb_gen to
	 *    3, we'd be break the invariant: we'd update local_tlb_gen above
	 *    1 without the full flush that's needed for tlb_gen 2.
	 *
	 * 2. f->new_tlb_gen == mm_tlb_gen.  This is purely an optimiation.
	 *    Partial TLB flushes are not all that much cheaper than full TLB
	 *    flushes, so it seems unlikely that it would be a performance win
	 *    to do a partial flush if that won't bring our TLB fully up to
	 *    date.  By doing a full flush instead, we can increase
	 *    local_tlb_gen all the way to mm_tlb_gen and we can probably
	 *    avoid another flush in the very near future.
	 */
	if (f->end != TLB_FLUSH_ALL &&
	    f->new_tlb_gen == local_tlb_gen + 1 &&
	    f->new_tlb_gen == mm_tlb_gen) {
		/* Partial flush */
		unsigned long addr = f->start;

		nr_invalidate = (f->end - f->start) >> f->stride_shift;

		while (addr < f->end) {
			flush_tlb_one_user(addr);
			addr += 1UL << f->stride_shift;
		}
		if (local)
			count_vm_tlb_events(NR_TLB_LOCAL_FLUSH_ONE, nr_invalidate);
	} else {
		/* Full flush. */
		nr_invalidate = TLB_FLUSH_ALL;

		flush_tlb_local();
		if (local)
			count_vm_tlb_event(NR_TLB_LOCAL_FLUSH_ALL);
	}

	/* Both paths above update our state to mm_tlb_gen. */
	this_cpu_write(cpu_tlbstate.ctxs[loaded_mm_asid].tlb_gen, mm_tlb_gen);

	/* Tracing is done in a unified manner to reduce the code size */
done:
	trace_tlb_flush(!local ? TLB_REMOTE_SHOOTDOWN :
				(f->mm == NULL) ? TLB_LOCAL_SHOOTDOWN :
						  TLB_LOCAL_MM_SHOOTDOWN,
			nr_invalidate);
}

static bool tlb_is_not_lazy(int cpu, void *data)
{
	return !per_cpu(cpu_tlbstate.is_lazy, cpu);
}

STATIC_NOPV void native_flush_tlb_others(const struct cpumask *cpumask,
					 const struct flush_tlb_info *info)
{
	count_vm_tlb_event(NR_TLB_REMOTE_FLUSH);
	if (info->end == TLB_FLUSH_ALL)
		trace_tlb_flush(TLB_REMOTE_SEND_IPI, TLB_FLUSH_ALL);
	else
		trace_tlb_flush(TLB_REMOTE_SEND_IPI,
				(info->end - info->start) >> PAGE_SHIFT);

	/*
	 * If no page tables were freed, we can skip sending IPIs to
	 * CPUs in lazy TLB mode. They will flush the CPU themselves
	 * at the next context switch.
	 *
	 * However, if page tables are getting freed, we need to send the
	 * IPI everywhere, to prevent CPUs in lazy TLB mode from tripping
	 * up on the new contents of what used to be page tables, while
	 * doing a speculative memory access.
	 */
	if (info->freed_tables)
		smp_call_function_many(cpumask, flush_tlb_func,
			       (void *)info, 1);
	else
		on_each_cpu_cond_mask(tlb_is_not_lazy, flush_tlb_func,
				(void *)info, 1, cpumask);
}

void flush_tlb_others(const struct cpumask *cpumask,
		      const struct flush_tlb_info *info)
{
	__flush_tlb_others(cpumask, info);
}

/*
 * See Documentation/x86/tlb.rst for details.  We choose 33
 * because it is large enough to cover the vast majority (at
 * least 95%) of allocations, and is small enough that we are
 * confident it will not cause too much overhead.  Each single
 * flush is about 100 ns, so this caps the maximum overhead at
 * _about_ 3,000 ns.
 *
 * This is in units of pages.
 */
unsigned long tlb_single_page_flush_ceiling __read_mostly = 33;

static DEFINE_PER_CPU_SHARED_ALIGNED(struct flush_tlb_info, flush_tlb_info);

#ifdef CONFIG_DEBUG_VM
static DEFINE_PER_CPU(unsigned int, flush_tlb_info_idx);
#endif

static inline struct flush_tlb_info *get_flush_tlb_info(struct mm_struct *mm,
			unsigned long start, unsigned long end,
			unsigned int stride_shift, bool freed_tables,
			u64 new_tlb_gen)
{
	struct flush_tlb_info *info = this_cpu_ptr(&flush_tlb_info);

#ifdef CONFIG_DEBUG_VM
	/*
	 * Ensure that the following code is non-reentrant and flush_tlb_info
	 * is not overwritten. This means no TLB flushing is initiated by
	 * interrupt handlers and machine-check exception handlers.
	 */
	BUG_ON(this_cpu_inc_return(flush_tlb_info_idx) != 1);
#endif

	info->start		= start;
	info->end		= end;
	info->mm		= mm;
	info->stride_shift	= stride_shift;
	info->freed_tables	= freed_tables;
	info->new_tlb_gen	= new_tlb_gen;
	info->initiating_cpu	= smp_processor_id();

	return info;
}

static inline void put_flush_tlb_info(void)
{
#ifdef CONFIG_DEBUG_VM
	/* Complete reentrency prevention checks */
	barrier();
	this_cpu_dec(flush_tlb_info_idx);
#endif
}

void flush_tlb_mm_range(struct mm_struct *mm, unsigned long start,
				unsigned long end, unsigned int stride_shift,
				bool freed_tables)
{
	struct flush_tlb_info *info;
	u64 new_tlb_gen;
	int cpu;

	cpu = get_cpu();

	/* Should we flush just the requested range? */
	if ((end == TLB_FLUSH_ALL) ||
	    ((end - start) >> stride_shift) > tlb_single_page_flush_ceiling) {
		start = 0;
		end = TLB_FLUSH_ALL;
	}

	/* This is also a barrier that synchronizes with switch_mm(). */
	new_tlb_gen = inc_mm_tlb_gen(mm);

	info = get_flush_tlb_info(mm, start, end, stride_shift, freed_tables,
				  new_tlb_gen);

	if (mm == this_cpu_read(cpu_tlbstate.loaded_mm)) {
		lockdep_assert_irqs_enabled();
		local_irq_disable();
		flush_tlb_func(info);
		local_irq_enable();
	}

	if (cpumask_any_but(mm_cpumask(mm), cpu) < nr_cpu_ids)
		flush_tlb_others(mm_cpumask(mm), info);

	put_flush_tlb_info();
	put_cpu();
}


static void do_flush_tlb_all(void *info)
{
	count_vm_tlb_event(NR_TLB_REMOTE_FLUSH_RECEIVED);
	__flush_tlb_all();
}

void flush_tlb_all(void)
{
	count_vm_tlb_event(NR_TLB_REMOTE_FLUSH);
	on_each_cpu(do_flush_tlb_all, NULL, 1);
}

static void do_kernel_range_flush(void *info)
{
	struct flush_tlb_info *f = info;
	unsigned long addr;

	/* flush range by one by one 'invlpg' */
	for (addr = f->start; addr < f->end; addr += PAGE_SIZE)
		flush_tlb_one_kernel(addr);
}

void flush_tlb_kernel_range(unsigned long start, unsigned long end)
{
	/* Balance as user space task's flush, a bit conservative */
	if (end == TLB_FLUSH_ALL ||
	    (end - start) > tlb_single_page_flush_ceiling << PAGE_SHIFT) {
		on_each_cpu(do_flush_tlb_all, NULL, 1);
	} else {
		struct flush_tlb_info *info;

		preempt_disable();
		info = get_flush_tlb_info(NULL, start, end, 0, false, 0);

		on_each_cpu(do_kernel_range_flush, info, 1);

		put_flush_tlb_info();
		preempt_enable();
	}
}

/*
 * This can be used from process context to figure out what the value of
 * CR3 is without needing to do a (slow) __read_cr3().
 *
 * It's intended to be used for code like KVM that sneakily changes CR3
 * and needs to restore it.  It needs to be used very carefully.
 */
unsigned long __get_current_cr3_fast(void)
{
	unsigned long cr3 = build_cr3(this_cpu_read(cpu_tlbstate.loaded_mm)->pgd,
		this_cpu_read(cpu_tlbstate.loaded_mm_asid));

	/* For now, be very restrictive about when this can be called. */
	VM_WARN_ON(in_nmi() || preemptible());

	VM_BUG_ON(cr3 != __read_cr3());
	return cr3;
}
EXPORT_SYMBOL_GPL(__get_current_cr3_fast);

/*
 * Flush one page in the kernel mapping
 */
void flush_tlb_one_kernel(unsigned long addr)
{
	count_vm_tlb_event(NR_TLB_LOCAL_FLUSH_ONE);

	/*
	 * If PTI is off, then __flush_tlb_one_user() is just INVLPG or its
	 * paravirt equivalent.  Even with PCID, this is sufficient: we only
	 * use PCID if we also use global PTEs for the kernel mapping, and
	 * INVLPG flushes global translations across all address spaces.
	 *
	 * If PTI is on, then the kernel is mapped with non-global PTEs, and
	 * __flush_tlb_one_user() will flush the given address for the current
	 * kernel address space and for its usermode counterpart, but it does
	 * not flush it for other address spaces.
	 */
	flush_tlb_one_user(addr);

	if (!static_cpu_has(X86_FEATURE_PTI))
		return;

	/*
	 * See above.  We need to propagate the flush to all other address
	 * spaces.  In principle, we only need to propagate it to kernelmode
	 * address spaces, but the extra bookkeeping we would need is not
	 * worth it.
	 */
	this_cpu_write(cpu_tlbstate.invalidate_other, true);
}

/*
 * Flush one page in the user mapping
 */
STATIC_NOPV void native_flush_tlb_one_user(unsigned long addr)
{
	u32 loaded_mm_asid = this_cpu_read(cpu_tlbstate.loaded_mm_asid);

	asm volatile("invlpg (%0)" ::"r" (addr) : "memory");

	if (!static_cpu_has(X86_FEATURE_PTI))
		return;

	/*
	 * Some platforms #GP if we call invpcid(type=1/2) before CR4.PCIDE=1.
	 * Just use invalidate_user_asid() in case we are called early.
	 */
	if (!this_cpu_has(X86_FEATURE_INVPCID_SINGLE))
		invalidate_user_asid(loaded_mm_asid);
	else
		invpcid_flush_one(user_pcid(loaded_mm_asid), addr);
}

void flush_tlb_one_user(unsigned long addr)
{
	__flush_tlb_one_user(addr);
}

/*
 * Flush everything
 */
STATIC_NOPV void native_flush_tlb_global(void)
{
	unsigned long cr4, flags;

	if (static_cpu_has(X86_FEATURE_INVPCID)) {
		/*
		 * Using INVPCID is considerably faster than a pair of writes
		 * to CR4 sandwiched inside an IRQ flag save/restore.
		 *
		 * Note, this works with CR4.PCIDE=0 or 1.
		 */
		invpcid_flush_all();
		return;
	}

	/*
	 * Read-modify-write to CR4 - protect it from preemption and
	 * from interrupts. (Use the raw variant because this code can
	 * be called from deep inside debugging code.)
	 */
	raw_local_irq_save(flags);

	cr4 = this_cpu_read(cpu_tlbstate.cr4);
	/* toggle PGE */
	native_write_cr4(cr4 ^ X86_CR4_PGE);
	/* write old PGE again and flush TLBs */
	native_write_cr4(cr4);

	raw_local_irq_restore(flags);
}

/*
 * Flush the entire current user mapping
 */
STATIC_NOPV void native_flush_tlb_local(void)
{
	/*
	 * Preemption or interrupts must be disabled to protect the access
	 * to the per CPU variable and to prevent being preempted between
	 * read_cr3() and write_cr3().
	 */
	WARN_ON_ONCE(preemptible());

	invalidate_user_asid(this_cpu_read(cpu_tlbstate.loaded_mm_asid));

	/* If current->mm == NULL then the read_cr3() "borrows" an mm */
	native_write_cr3(__native_read_cr3());
}

void flush_tlb_local(void)
{
	__flush_tlb_local();
}

/*
 * Flush everything
 */
void __flush_tlb_all(void)
{
	/*
	 * This is to catch users with enabled preemption and the PGE feature
	 * and don't trigger the warning in __native_flush_tlb().
	 */
	VM_WARN_ON_ONCE(preemptible());

	if (boot_cpu_has(X86_FEATURE_PGE)) {
		__flush_tlb_global();
	} else {
		/*
		 * !PGE -> !PCID (setup_pcid()), thus every flush is total.
		 */
		flush_tlb_local();
	}
}
EXPORT_SYMBOL_GPL(__flush_tlb_all);

void arch_tlbbatch_flush(struct arch_tlbflush_unmap_batch *batch)
{
	struct flush_tlb_info *info;

	int cpu = get_cpu();

	info = get_flush_tlb_info(NULL, 0, TLB_FLUSH_ALL, 0, false, 0);
	if (cpumask_test_cpu(cpu, &batch->cpumask)) {
		lockdep_assert_irqs_enabled();
		local_irq_disable();
		flush_tlb_func(info);
		local_irq_enable();
	}

	if (cpumask_any_but(&batch->cpumask, cpu) < nr_cpu_ids)
		flush_tlb_others(&batch->cpumask, info);

	cpumask_clear(&batch->cpumask);

	put_flush_tlb_info();
	put_cpu();
}

/*
 * Blindly accessing user memory from NMI context can be dangerous
 * if we're in the middle of switching the current user task or
 * switching the loaded mm.  It can also be dangerous if we
 * interrupted some kernel code that was temporarily using a
 * different mm.
 */
bool nmi_uaccess_okay(void)
{
	struct mm_struct *loaded_mm = this_cpu_read(cpu_tlbstate.loaded_mm);
	struct mm_struct *current_mm = current->mm;

	VM_WARN_ON_ONCE(!loaded_mm);

	/*
	 * The condition we want to check is
	 * current_mm->pgd == __va(read_cr3_pa()).  This may be slow, though,
	 * if we're running in a VM with shadow paging, and nmi_uaccess_okay()
	 * is supposed to be reasonably fast.
	 *
	 * Instead, we check the almost equivalent but somewhat conservative
	 * condition below, and we rely on the fact that switch_mm_irqs_off()
	 * sets loaded_mm to LOADED_MM_SWITCHING before writing to CR3.
	 */
	if (loaded_mm != current_mm)
		return false;

	VM_WARN_ON_ONCE(current_mm->pgd != __va(read_cr3_pa()));

	return true;
}

static ssize_t tlbflush_read_file(struct file *file, char __user *user_buf,
			     size_t count, loff_t *ppos)
{
	char buf[32];
	unsigned int len;

	len = sprintf(buf, "%ld\n", tlb_single_page_flush_ceiling);
	return simple_read_from_buffer(user_buf, count, ppos, buf, len);
}

static ssize_t tlbflush_write_file(struct file *file,
		 const char __user *user_buf, size_t count, loff_t *ppos)
{
	char buf[32];
	ssize_t len;
	int ceiling;

	len = min(count, sizeof(buf) - 1);
	if (copy_from_user(buf, user_buf, len))
		return -EFAULT;

	buf[len] = '\0';
	if (kstrtoint(buf, 0, &ceiling))
		return -EINVAL;

	if (ceiling < 0)
		return -EINVAL;

	tlb_single_page_flush_ceiling = ceiling;
	return count;
}

static const struct file_operations fops_tlbflush = {
	.read = tlbflush_read_file,
	.write = tlbflush_write_file,
	.llseek = default_llseek,
};

static int __init create_tlb_single_page_flush_ceiling(void)
{
	debugfs_create_file("tlb_single_page_flush_ceiling", S_IRUSR | S_IWUSR,
			    arch_debugfs_dir, NULL, &fops_tlbflush);
	return 0;
}
late_initcall(create_tlb_single_page_flush_ceiling);
