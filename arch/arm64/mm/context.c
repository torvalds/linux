// SPDX-License-Identifier: GPL-2.0-only
/*
 * Based on arch/arm/mm/context.c
 *
 * Copyright (C) 2002-2003 Deep Blue Solutions Ltd, all rights reserved.
 * Copyright (C) 2012 ARM Ltd.
 */

#include <linux/bitfield.h>
#include <linux/bitops.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/mm.h>

#include <asm/cpufeature.h>
#include <asm/mmu_context.h>
#include <asm/smp.h>
#include <asm/tlbflush.h>

static u32 asid_bits;
static DEFINE_RAW_SPINLOCK(cpu_asid_lock);

static atomic64_t asid_generation;
static unsigned long *asid_map;

static DEFINE_PER_CPU(atomic64_t, active_asids);
static DEFINE_PER_CPU(u64, reserved_asids);
static cpumask_t tlb_flush_pending;

static unsigned long max_pinned_asids;
static unsigned long nr_pinned_asids;
static unsigned long *pinned_asid_map;

#define ASID_MASK		(~GENMASK(asid_bits - 1, 0))
#define ASID_FIRST_VERSION	(1UL << asid_bits)

#define NUM_USER_ASIDS		ASID_FIRST_VERSION
#define ctxid2asid(asid)	((asid) & ~ASID_MASK)
#define asid2ctxid(asid, genid)	((asid) | (genid))

/* Get the ASIDBits supported by the current CPU */
static u32 get_cpu_asid_bits(void)
{
	u32 asid;
	int fld = cpuid_feature_extract_unsigned_field(read_cpuid(ID_AA64MMFR0_EL1),
						ID_AA64MMFR0_ASID_SHIFT);

	switch (fld) {
	default:
		pr_warn("CPU%d: Unknown ASID size (%d); assuming 8-bit\n",
					smp_processor_id(),  fld);
		fallthrough;
	case ID_AA64MMFR0_ASID_8:
		asid = 8;
		break;
	case ID_AA64MMFR0_ASID_16:
		asid = 16;
	}

	return asid;
}

/* Check if the current cpu's ASIDBits is compatible with asid_bits */
void verify_cpu_asid_bits(void)
{
	u32 asid = get_cpu_asid_bits();

	if (asid < asid_bits) {
		/*
		 * We cannot decrease the ASID size at runtime, so panic if we support
		 * fewer ASID bits than the boot CPU.
		 */
		pr_crit("CPU%d: smaller ASID size(%u) than boot CPU (%u)\n",
				smp_processor_id(), asid, asid_bits);
		cpu_panic_kernel();
	}
}

static void set_kpti_asid_bits(unsigned long *map)
{
	unsigned int len = BITS_TO_LONGS(NUM_USER_ASIDS) * sizeof(unsigned long);
	/*
	 * In case of KPTI kernel/user ASIDs are allocated in
	 * pairs, the bottom bit distinguishes the two: if it
	 * is set, then the ASID will map only userspace. Thus
	 * mark even as reserved for kernel.
	 */
	memset(map, 0xaa, len);
}

static void set_reserved_asid_bits(void)
{
	if (pinned_asid_map)
		bitmap_copy(asid_map, pinned_asid_map, NUM_USER_ASIDS);
	else if (arm64_kernel_unmapped_at_el0())
		set_kpti_asid_bits(asid_map);
	else
		bitmap_clear(asid_map, 0, NUM_USER_ASIDS);
}

#define asid_gen_match(asid) \
	(!(((asid) ^ atomic64_read(&asid_generation)) >> asid_bits))

static void flush_context(void)
{
	int i;
	u64 asid;

	/* Update the list of reserved ASIDs and the ASID bitmap. */
	set_reserved_asid_bits();

	for_each_possible_cpu(i) {
		asid = atomic64_xchg_relaxed(&per_cpu(active_asids, i), 0);
		/*
		 * If this CPU has already been through a
		 * rollover, but hasn't run another task in
		 * the meantime, we must preserve its reserved
		 * ASID, as this is the only trace we have of
		 * the process it is still running.
		 */
		if (asid == 0)
			asid = per_cpu(reserved_asids, i);
		__set_bit(ctxid2asid(asid), asid_map);
		per_cpu(reserved_asids, i) = asid;
	}

	/*
	 * Queue a TLB invalidation for each CPU to perform on next
	 * context-switch
	 */
	cpumask_setall(&tlb_flush_pending);
}

static bool check_update_reserved_asid(u64 asid, u64 newasid)
{
	int cpu;
	bool hit = false;

	/*
	 * Iterate over the set of reserved ASIDs looking for a match.
	 * If we find one, then we can update our mm to use newasid
	 * (i.e. the same ASID in the current generation) but we can't
	 * exit the loop early, since we need to ensure that all copies
	 * of the old ASID are updated to reflect the mm. Failure to do
	 * so could result in us missing the reserved ASID in a future
	 * generation.
	 */
	for_each_possible_cpu(cpu) {
		if (per_cpu(reserved_asids, cpu) == asid) {
			hit = true;
			per_cpu(reserved_asids, cpu) = newasid;
		}
	}

	return hit;
}

static u64 new_context(struct mm_struct *mm)
{
	static u32 cur_idx = 1;
	u64 asid = atomic64_read(&mm->context.id);
	u64 generation = atomic64_read(&asid_generation);

	if (asid != 0) {
		u64 newasid = asid2ctxid(ctxid2asid(asid), generation);

		/*
		 * If our current ASID was active during a rollover, we
		 * can continue to use it and this was just a false alarm.
		 */
		if (check_update_reserved_asid(asid, newasid))
			return newasid;

		/*
		 * If it is pinned, we can keep using it. Note that reserved
		 * takes priority, because even if it is also pinned, we need to
		 * update the generation into the reserved_asids.
		 */
		if (refcount_read(&mm->context.pinned))
			return newasid;

		/*
		 * We had a valid ASID in a previous life, so try to re-use
		 * it if possible.
		 */
		if (!__test_and_set_bit(ctxid2asid(asid), asid_map))
			return newasid;
	}

	/*
	 * Allocate a free ASID. If we can't find one, take a note of the
	 * currently active ASIDs and mark the TLBs as requiring flushes.  We
	 * always count from ASID #2 (index 1), as we use ASID #0 when setting
	 * a reserved TTBR0 for the init_mm and we allocate ASIDs in even/odd
	 * pairs.
	 */
	asid = find_next_zero_bit(asid_map, NUM_USER_ASIDS, cur_idx);
	if (asid != NUM_USER_ASIDS)
		goto set_asid;

	/* We're out of ASIDs, so increment the global generation count */
	generation = atomic64_add_return_relaxed(ASID_FIRST_VERSION,
						 &asid_generation);
	flush_context();

	/* We have more ASIDs than CPUs, so this will always succeed */
	asid = find_next_zero_bit(asid_map, NUM_USER_ASIDS, 1);

set_asid:
	__set_bit(asid, asid_map);
	cur_idx = asid;
	return asid2ctxid(asid, generation);
}

void check_and_switch_context(struct mm_struct *mm)
{
	unsigned long flags;
	unsigned int cpu;
	u64 asid, old_active_asid;

	if (system_supports_cnp())
		cpu_set_reserved_ttbr0();

	asid = atomic64_read(&mm->context.id);

	/*
	 * The memory ordering here is subtle.
	 * If our active_asids is non-zero and the ASID matches the current
	 * generation, then we update the active_asids entry with a relaxed
	 * cmpxchg. Racing with a concurrent rollover means that either:
	 *
	 * - We get a zero back from the cmpxchg and end up waiting on the
	 *   lock. Taking the lock synchronises with the rollover and so
	 *   we are forced to see the updated generation.
	 *
	 * - We get a valid ASID back from the cmpxchg, which means the
	 *   relaxed xchg in flush_context will treat us as reserved
	 *   because atomic RmWs are totally ordered for a given location.
	 */
	old_active_asid = atomic64_read(this_cpu_ptr(&active_asids));
	if (old_active_asid && asid_gen_match(asid) &&
	    atomic64_cmpxchg_relaxed(this_cpu_ptr(&active_asids),
				     old_active_asid, asid))
		goto switch_mm_fastpath;

	raw_spin_lock_irqsave(&cpu_asid_lock, flags);
	/* Check that our ASID belongs to the current generation. */
	asid = atomic64_read(&mm->context.id);
	if (!asid_gen_match(asid)) {
		asid = new_context(mm);
		atomic64_set(&mm->context.id, asid);
	}

	cpu = smp_processor_id();
	if (cpumask_test_and_clear_cpu(cpu, &tlb_flush_pending))
		local_flush_tlb_all();

	atomic64_set(this_cpu_ptr(&active_asids), asid);
	raw_spin_unlock_irqrestore(&cpu_asid_lock, flags);

switch_mm_fastpath:

	arm64_apply_bp_hardening();

	/*
	 * Defer TTBR0_EL1 setting for user threads to uaccess_enable() when
	 * emulating PAN.
	 */
	if (!system_uses_ttbr0_pan())
		cpu_switch_mm(mm->pgd, mm);
}

unsigned long arm64_mm_context_get(struct mm_struct *mm)
{
	unsigned long flags;
	u64 asid;

	if (!pinned_asid_map)
		return 0;

	raw_spin_lock_irqsave(&cpu_asid_lock, flags);

	asid = atomic64_read(&mm->context.id);

	if (refcount_inc_not_zero(&mm->context.pinned))
		goto out_unlock;

	if (nr_pinned_asids >= max_pinned_asids) {
		asid = 0;
		goto out_unlock;
	}

	if (!asid_gen_match(asid)) {
		/*
		 * We went through one or more rollover since that ASID was
		 * used. Ensure that it is still valid, or generate a new one.
		 */
		asid = new_context(mm);
		atomic64_set(&mm->context.id, asid);
	}

	nr_pinned_asids++;
	__set_bit(ctxid2asid(asid), pinned_asid_map);
	refcount_set(&mm->context.pinned, 1);

out_unlock:
	raw_spin_unlock_irqrestore(&cpu_asid_lock, flags);

	asid = ctxid2asid(asid);

	/* Set the equivalent of USER_ASID_BIT */
	if (asid && arm64_kernel_unmapped_at_el0())
		asid |= 1;

	return asid;
}
EXPORT_SYMBOL_GPL(arm64_mm_context_get);

void arm64_mm_context_put(struct mm_struct *mm)
{
	unsigned long flags;
	u64 asid = atomic64_read(&mm->context.id);

	if (!pinned_asid_map)
		return;

	raw_spin_lock_irqsave(&cpu_asid_lock, flags);

	if (refcount_dec_and_test(&mm->context.pinned)) {
		__clear_bit(ctxid2asid(asid), pinned_asid_map);
		nr_pinned_asids--;
	}

	raw_spin_unlock_irqrestore(&cpu_asid_lock, flags);
}
EXPORT_SYMBOL_GPL(arm64_mm_context_put);

/* Errata workaround post TTBRx_EL1 update. */
asmlinkage void post_ttbr_update_workaround(void)
{
	if (!IS_ENABLED(CONFIG_CAVIUM_ERRATUM_27456))
		return;

	asm(ALTERNATIVE("nop; nop; nop",
			"ic iallu; dsb nsh; isb",
			ARM64_WORKAROUND_CAVIUM_27456));
}

void cpu_do_switch_mm(phys_addr_t pgd_phys, struct mm_struct *mm)
{
	unsigned long ttbr1 = read_sysreg(ttbr1_el1);
	unsigned long asid = ASID(mm);
	unsigned long ttbr0 = phys_to_ttbr(pgd_phys);

	/* Skip CNP for the reserved ASID */
	if (system_supports_cnp() && asid)
		ttbr0 |= TTBR_CNP_BIT;

	/* SW PAN needs a copy of the ASID in TTBR0 for entry */
	if (IS_ENABLED(CONFIG_ARM64_SW_TTBR0_PAN))
		ttbr0 |= FIELD_PREP(TTBR_ASID_MASK, asid);

	/* Set ASID in TTBR1 since TCR.A1 is set */
	ttbr1 &= ~TTBR_ASID_MASK;
	ttbr1 |= FIELD_PREP(TTBR_ASID_MASK, asid);

	write_sysreg(ttbr1, ttbr1_el1);
	isb();
	write_sysreg(ttbr0, ttbr0_el1);
	isb();
	post_ttbr_update_workaround();
}

static int asids_update_limit(void)
{
	unsigned long num_available_asids = NUM_USER_ASIDS;

	if (arm64_kernel_unmapped_at_el0()) {
		num_available_asids /= 2;
		if (pinned_asid_map)
			set_kpti_asid_bits(pinned_asid_map);
	}
	/*
	 * Expect allocation after rollover to fail if we don't have at least
	 * one more ASID than CPUs. ASID #0 is reserved for init_mm.
	 */
	WARN_ON(num_available_asids - 1 <= num_possible_cpus());
	pr_info("ASID allocator initialised with %lu entries\n",
		num_available_asids);

	/*
	 * There must always be an ASID available after rollover. Ensure that,
	 * even if all CPUs have a reserved ASID and the maximum number of ASIDs
	 * are pinned, there still is at least one empty slot in the ASID map.
	 */
	max_pinned_asids = num_available_asids - num_possible_cpus() - 2;
	return 0;
}
arch_initcall(asids_update_limit);

static int asids_init(void)
{
	asid_bits = get_cpu_asid_bits();
	atomic64_set(&asid_generation, ASID_FIRST_VERSION);
	asid_map = bitmap_zalloc(NUM_USER_ASIDS, GFP_KERNEL);
	if (!asid_map)
		panic("Failed to allocate bitmap for %lu ASIDs\n",
		      NUM_USER_ASIDS);

	pinned_asid_map = bitmap_zalloc(NUM_USER_ASIDS, GFP_KERNEL);
	nr_pinned_asids = 0;

	/*
	 * We cannot call set_reserved_asid_bits() here because CPU
	 * caps are not finalized yet, so it is safer to assume KPTI
	 * and reserve kernel ASID's from beginning.
	 */
	if (IS_ENABLED(CONFIG_UNMAP_KERNEL_AT_EL0))
		set_kpti_asid_bits(asid_map);
	return 0;
}
early_initcall(asids_init);
