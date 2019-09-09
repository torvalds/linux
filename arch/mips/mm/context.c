// SPDX-License-Identifier: GPL-2.0
#include <linux/atomic.h>
#include <linux/mmu_context.h>
#include <linux/percpu.h>
#include <linux/spinlock.h>

static DEFINE_RAW_SPINLOCK(cpu_mmid_lock);

static atomic64_t mmid_version;
static unsigned int num_mmids;
static unsigned long *mmid_map;

static DEFINE_PER_CPU(u64, reserved_mmids);
static cpumask_t tlb_flush_pending;

static bool asid_versions_eq(int cpu, u64 a, u64 b)
{
	return ((a ^ b) & asid_version_mask(cpu)) == 0;
}

void get_new_mmu_context(struct mm_struct *mm)
{
	unsigned int cpu;
	u64 asid;

	/*
	 * This function is specific to ASIDs, and should not be called when
	 * MMIDs are in use.
	 */
	if (WARN_ON(IS_ENABLED(CONFIG_DEBUG_VM) && cpu_has_mmid))
		return;

	cpu = smp_processor_id();
	asid = asid_cache(cpu);

	if (!((asid += cpu_asid_inc()) & cpu_asid_mask(&cpu_data[cpu]))) {
		if (cpu_has_vtag_icache)
			flush_icache_all();
		local_flush_tlb_all();	/* start new asid cycle */
	}

	set_cpu_context(cpu, mm, asid);
	asid_cache(cpu) = asid;
}
EXPORT_SYMBOL_GPL(get_new_mmu_context);

void check_mmu_context(struct mm_struct *mm)
{
	unsigned int cpu = smp_processor_id();

	/*
	 * This function is specific to ASIDs, and should not be called when
	 * MMIDs are in use.
	 */
	if (WARN_ON(IS_ENABLED(CONFIG_DEBUG_VM) && cpu_has_mmid))
		return;

	/* Check if our ASID is of an older version and thus invalid */
	if (!asid_versions_eq(cpu, cpu_context(cpu, mm), asid_cache(cpu)))
		get_new_mmu_context(mm);
}
EXPORT_SYMBOL_GPL(check_mmu_context);

static void flush_context(void)
{
	u64 mmid;
	int cpu;

	/* Update the list of reserved MMIDs and the MMID bitmap */
	bitmap_clear(mmid_map, 0, num_mmids);

	/* Reserve an MMID for kmap/wired entries */
	__set_bit(MMID_KERNEL_WIRED, mmid_map);

	for_each_possible_cpu(cpu) {
		mmid = xchg_relaxed(&cpu_data[cpu].asid_cache, 0);

		/*
		 * If this CPU has already been through a
		 * rollover, but hasn't run another task in
		 * the meantime, we must preserve its reserved
		 * MMID, as this is the only trace we have of
		 * the process it is still running.
		 */
		if (mmid == 0)
			mmid = per_cpu(reserved_mmids, cpu);

		__set_bit(mmid & cpu_asid_mask(&cpu_data[cpu]), mmid_map);
		per_cpu(reserved_mmids, cpu) = mmid;
	}

	/*
	 * Queue a TLB invalidation for each CPU to perform on next
	 * context-switch
	 */
	cpumask_setall(&tlb_flush_pending);
}

static bool check_update_reserved_mmid(u64 mmid, u64 newmmid)
{
	bool hit;
	int cpu;

	/*
	 * Iterate over the set of reserved MMIDs looking for a match.
	 * If we find one, then we can update our mm to use newmmid
	 * (i.e. the same MMID in the current generation) but we can't
	 * exit the loop early, since we need to ensure that all copies
	 * of the old MMID are updated to reflect the mm. Failure to do
	 * so could result in us missing the reserved MMID in a future
	 * generation.
	 */
	hit = false;
	for_each_possible_cpu(cpu) {
		if (per_cpu(reserved_mmids, cpu) == mmid) {
			hit = true;
			per_cpu(reserved_mmids, cpu) = newmmid;
		}
	}

	return hit;
}

static u64 get_new_mmid(struct mm_struct *mm)
{
	static u32 cur_idx = MMID_KERNEL_WIRED + 1;
	u64 mmid, version, mmid_mask;

	mmid = cpu_context(0, mm);
	version = atomic64_read(&mmid_version);
	mmid_mask = cpu_asid_mask(&boot_cpu_data);

	if (!asid_versions_eq(0, mmid, 0)) {
		u64 newmmid = version | (mmid & mmid_mask);

		/*
		 * If our current MMID was active during a rollover, we
		 * can continue to use it and this was just a false alarm.
		 */
		if (check_update_reserved_mmid(mmid, newmmid)) {
			mmid = newmmid;
			goto set_context;
		}

		/*
		 * We had a valid MMID in a previous life, so try to re-use
		 * it if possible.
		 */
		if (!__test_and_set_bit(mmid & mmid_mask, mmid_map)) {
			mmid = newmmid;
			goto set_context;
		}
	}

	/* Allocate a free MMID */
	mmid = find_next_zero_bit(mmid_map, num_mmids, cur_idx);
	if (mmid != num_mmids)
		goto reserve_mmid;

	/* We're out of MMIDs, so increment the global version */
	version = atomic64_add_return_relaxed(asid_first_version(0),
					      &mmid_version);

	/* Note currently active MMIDs & mark TLBs as requiring flushes */
	flush_context();

	/* We have more MMIDs than CPUs, so this will always succeed */
	mmid = find_first_zero_bit(mmid_map, num_mmids);

reserve_mmid:
	__set_bit(mmid, mmid_map);
	cur_idx = mmid;
	mmid |= version;
set_context:
	set_cpu_context(0, mm, mmid);
	return mmid;
}

void check_switch_mmu_context(struct mm_struct *mm)
{
	unsigned int cpu = smp_processor_id();
	u64 ctx, old_active_mmid;
	unsigned long flags;

	if (!cpu_has_mmid) {
		check_mmu_context(mm);
		write_c0_entryhi(cpu_asid(cpu, mm));
		goto setup_pgd;
	}

	/*
	 * MMID switch fast-path, to avoid acquiring cpu_mmid_lock when it's
	 * unnecessary.
	 *
	 * The memory ordering here is subtle. If our active_mmids is non-zero
	 * and the MMID matches the current version, then we update the CPU's
	 * asid_cache with a relaxed cmpxchg. Racing with a concurrent rollover
	 * means that either:
	 *
	 * - We get a zero back from the cmpxchg and end up waiting on
	 *   cpu_mmid_lock in check_mmu_context(). Taking the lock synchronises
	 *   with the rollover and so we are forced to see the updated
	 *   generation.
	 *
	 * - We get a valid MMID back from the cmpxchg, which means the
	 *   relaxed xchg in flush_context will treat us as reserved
	 *   because atomic RmWs are totally ordered for a given location.
	 */
	ctx = cpu_context(cpu, mm);
	old_active_mmid = READ_ONCE(cpu_data[cpu].asid_cache);
	if (!old_active_mmid ||
	    !asid_versions_eq(cpu, ctx, atomic64_read(&mmid_version)) ||
	    !cmpxchg_relaxed(&cpu_data[cpu].asid_cache, old_active_mmid, ctx)) {
		raw_spin_lock_irqsave(&cpu_mmid_lock, flags);

		ctx = cpu_context(cpu, mm);
		if (!asid_versions_eq(cpu, ctx, atomic64_read(&mmid_version)))
			ctx = get_new_mmid(mm);

		WRITE_ONCE(cpu_data[cpu].asid_cache, ctx);
		raw_spin_unlock_irqrestore(&cpu_mmid_lock, flags);
	}

	/*
	 * Invalidate the local TLB if needed. Note that we must only clear our
	 * bit in tlb_flush_pending after this is complete, so that the
	 * cpu_has_shared_ftlb_entries case below isn't misled.
	 */
	if (cpumask_test_cpu(cpu, &tlb_flush_pending)) {
		if (cpu_has_vtag_icache)
			flush_icache_all();
		local_flush_tlb_all();
		cpumask_clear_cpu(cpu, &tlb_flush_pending);
	}

	write_c0_memorymapid(ctx & cpu_asid_mask(&boot_cpu_data));

	/*
	 * If this CPU shares FTLB entries with its siblings and one or more of
	 * those siblings hasn't yet invalidated its TLB following a version
	 * increase then we need to invalidate any TLB entries for our MMID
	 * that we might otherwise pick up from a sibling.
	 *
	 * We ifdef on CONFIG_SMP because cpu_sibling_map isn't defined in
	 * CONFIG_SMP=n kernels.
	 */
#ifdef CONFIG_SMP
	if (cpu_has_shared_ftlb_entries &&
	    cpumask_intersects(&tlb_flush_pending, &cpu_sibling_map[cpu])) {
		/* Ensure we operate on the new MMID */
		mtc0_tlbw_hazard();

		/*
		 * Invalidate all TLB entries associated with the new
		 * MMID, and wait for the invalidation to complete.
		 */
		ginvt_mmid();
		sync_ginv();
	}
#endif

setup_pgd:
	TLBMISS_HANDLER_SETUP_PGD(mm->pgd);
}
EXPORT_SYMBOL_GPL(check_switch_mmu_context);

static int mmid_init(void)
{
	if (!cpu_has_mmid)
		return 0;

	/*
	 * Expect allocation after rollover to fail if we don't have at least
	 * one more MMID than CPUs.
	 */
	num_mmids = asid_first_version(0);
	WARN_ON(num_mmids <= num_possible_cpus());

	atomic64_set(&mmid_version, asid_first_version(0));
	mmid_map = kcalloc(BITS_TO_LONGS(num_mmids), sizeof(*mmid_map),
			   GFP_KERNEL);
	if (!mmid_map)
		panic("Failed to allocate bitmap for %u MMIDs\n", num_mmids);

	/* Reserve an MMID for kmap/wired entries */
	__set_bit(MMID_KERNEL_WIRED, mmid_map);

	pr_info("MMID allocator initialised with %u entries\n", num_mmids);
	return 0;
}
early_initcall(mmid_init);
