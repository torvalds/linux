// SPDX-License-Identifier: GPL-2.0
/*
 * VMID allocator.
 *
 * Based on Arm64 ASID allocator algorithm.
 * Please refer arch/arm64/mm/context.c for detailed
 * comments on algorithm.
 *
 * Copyright (C) 2002-2003 Deep Blue Solutions Ltd, all rights reserved.
 * Copyright (C) 2012 ARM Ltd.
 */

#include <linux/bitfield.h>
#include <linux/bitops.h>

#include <asm/kvm_asm.h>
#include <asm/kvm_mmu.h>

unsigned int __ro_after_init kvm_arm_vmid_bits;
static DEFINE_RAW_SPINLOCK(cpu_vmid_lock);

static atomic64_t vmid_generation;
static unsigned long *vmid_map;

static DEFINE_PER_CPU(atomic64_t, active_vmids);
static DEFINE_PER_CPU(u64, reserved_vmids);

#define VMID_MASK		(~GENMASK(kvm_arm_vmid_bits - 1, 0))
#define VMID_FIRST_VERSION	(1UL << kvm_arm_vmid_bits)

#define NUM_USER_VMIDS		VMID_FIRST_VERSION
#define vmid2idx(vmid)		((vmid) & ~VMID_MASK)
#define idx2vmid(idx)		vmid2idx(idx)

/*
 * As vmid #0 is always reserved, we will never allocate one
 * as below and can be treated as invalid. This is used to
 * set the active_vmids on vCPU schedule out.
 */
#define VMID_ACTIVE_INVALID		VMID_FIRST_VERSION

#define vmid_gen_match(vmid) \
	(!(((vmid) ^ atomic64_read(&vmid_generation)) >> kvm_arm_vmid_bits))

static void flush_context(void)
{
	int cpu;
	u64 vmid;

	bitmap_clear(vmid_map, 0, NUM_USER_VMIDS);

	for_each_possible_cpu(cpu) {
		vmid = atomic64_xchg_relaxed(&per_cpu(active_vmids, cpu), 0);

		/* Preserve reserved VMID */
		if (vmid == 0)
			vmid = per_cpu(reserved_vmids, cpu);
		__set_bit(vmid2idx(vmid), vmid_map);
		per_cpu(reserved_vmids, cpu) = vmid;
	}

	/*
	 * Unlike ASID allocator, we expect less frequent rollover in
	 * case of VMIDs. Hence, instead of marking the CPU as
	 * flush_pending and issuing a local context invalidation on
	 * the next context-switch, we broadcast TLB flush + I-cache
	 * invalidation over the inner shareable domain on rollover.
	 */
	kvm_call_hyp(__kvm_flush_vm_context);
}

static bool check_update_reserved_vmid(u64 vmid, u64 newvmid)
{
	int cpu;
	bool hit = false;

	/*
	 * Iterate over the set of reserved VMIDs looking for a match
	 * and update to use newvmid (i.e. the same VMID in the current
	 * generation).
	 */
	for_each_possible_cpu(cpu) {
		if (per_cpu(reserved_vmids, cpu) == vmid) {
			hit = true;
			per_cpu(reserved_vmids, cpu) = newvmid;
		}
	}

	return hit;
}

static u64 new_vmid(struct kvm_vmid *kvm_vmid)
{
	static u32 cur_idx = 1;
	u64 vmid = atomic64_read(&kvm_vmid->id);
	u64 generation = atomic64_read(&vmid_generation);

	if (vmid != 0) {
		u64 newvmid = generation | (vmid & ~VMID_MASK);

		if (check_update_reserved_vmid(vmid, newvmid)) {
			atomic64_set(&kvm_vmid->id, newvmid);
			return newvmid;
		}

		if (!__test_and_set_bit(vmid2idx(vmid), vmid_map)) {
			atomic64_set(&kvm_vmid->id, newvmid);
			return newvmid;
		}
	}

	vmid = find_next_zero_bit(vmid_map, NUM_USER_VMIDS, cur_idx);
	if (vmid != NUM_USER_VMIDS)
		goto set_vmid;

	/* We're out of VMIDs, so increment the global generation count */
	generation = atomic64_add_return_relaxed(VMID_FIRST_VERSION,
						 &vmid_generation);
	flush_context();

	/* We have more VMIDs than CPUs, so this will always succeed */
	vmid = find_next_zero_bit(vmid_map, NUM_USER_VMIDS, 1);

set_vmid:
	__set_bit(vmid, vmid_map);
	cur_idx = vmid;
	vmid = idx2vmid(vmid) | generation;
	atomic64_set(&kvm_vmid->id, vmid);
	return vmid;
}

/* Called from vCPU sched out with preemption disabled */
void kvm_arm_vmid_clear_active(void)
{
	atomic64_set(this_cpu_ptr(&active_vmids), VMID_ACTIVE_INVALID);
}

void kvm_arm_vmid_update(struct kvm_vmid *kvm_vmid)
{
	unsigned long flags;
	u64 vmid, old_active_vmid;

	vmid = atomic64_read(&kvm_vmid->id);

	/*
	 * Please refer comments in check_and_switch_context() in
	 * arch/arm64/mm/context.c.
	 *
	 * Unlike ASID allocator, we set the active_vmids to
	 * VMID_ACTIVE_INVALID on vCPU schedule out to avoid
	 * reserving the VMID space needlessly on rollover.
	 * Hence explicitly check here for a "!= 0" to
	 * handle the sync with a concurrent rollover.
	 */
	old_active_vmid = atomic64_read(this_cpu_ptr(&active_vmids));
	if (old_active_vmid != 0 && vmid_gen_match(vmid) &&
	    0 != atomic64_cmpxchg_relaxed(this_cpu_ptr(&active_vmids),
					  old_active_vmid, vmid))
		return;

	raw_spin_lock_irqsave(&cpu_vmid_lock, flags);

	/* Check that our VMID belongs to the current generation. */
	vmid = atomic64_read(&kvm_vmid->id);
	if (!vmid_gen_match(vmid))
		vmid = new_vmid(kvm_vmid);

	atomic64_set(this_cpu_ptr(&active_vmids), vmid);
	raw_spin_unlock_irqrestore(&cpu_vmid_lock, flags);
}

/*
 * Initialize the VMID allocator
 */
int __init kvm_arm_vmid_alloc_init(void)
{
	kvm_arm_vmid_bits = kvm_get_vmid_bits();

	/*
	 * Expect allocation after rollover to fail if we don't have
	 * at least one more VMID than CPUs. VMID #0 is always reserved.
	 */
	WARN_ON(NUM_USER_VMIDS - 1 <= num_possible_cpus());
	atomic64_set(&vmid_generation, VMID_FIRST_VERSION);
	vmid_map = kcalloc(BITS_TO_LONGS(NUM_USER_VMIDS),
			   sizeof(*vmid_map), GFP_KERNEL);
	if (!vmid_map)
		return -ENOMEM;

	return 0;
}

void __init kvm_arm_vmid_alloc_free(void)
{
	kfree(vmid_map);
}
