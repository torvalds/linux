// SPDX-License-Identifier: GPL-2.0-only

/*
 * Local APIC virtualization
 *
 * Copyright (C) 2006 Qumranet, Inc.
 * Copyright (C) 2007 Novell
 * Copyright (C) 2007 Intel
 * Copyright 2009 Red Hat, Inc. and/or its affiliates.
 *
 * Authors:
 *   Dor Laor <dor.laor@qumranet.com>
 *   Gregory Haskins <ghaskins@novell.com>
 *   Yaozu (Eddie) Dong <eddie.dong@intel.com>
 *
 * Based on Xen 3.1 code, Copyright (c) 2004, Intel Corporation.
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kvm_host.h>
#include <linux/kvm.h>
#include <linux/mm.h>
#include <linux/highmem.h>
#include <linux/smp.h>
#include <linux/hrtimer.h>
#include <linux/io.h>
#include <linux/export.h>
#include <linux/math64.h>
#include <linux/slab.h>
#include <asm/apic.h>
#include <asm/processor.h>
#include <asm/mce.h>
#include <asm/msr.h>
#include <asm/page.h>
#include <asm/current.h>
#include <asm/apicdef.h>
#include <asm/delay.h>
#include <linux/atomic.h>
#include <linux/jump_label.h>
#include "kvm_cache_regs.h"
#include "irq.h"
#include "ioapic.h"
#include "trace.h"
#include "x86.h"
#include "xen.h"
#include "cpuid.h"
#include "hyperv.h"
#include "smm.h"

#ifndef CONFIG_X86_64
#define mod_64(x, y) ((x) - (y) * div64_u64(x, y))
#else
#define mod_64(x, y) ((x) % (y))
#endif

/* 14 is the version for Xeon and Pentium 8.4.8*/
#define APIC_VERSION			0x14UL
#define LAPIC_MMIO_LENGTH		(1 << 12)

/*
 * Enable local APIC timer advancement (tscdeadline mode only) with adaptive
 * tuning.  When enabled, KVM programs the host timer event to fire early, i.e.
 * before the deadline expires, to account for the delay between taking the
 * VM-Exit (to inject the guest event) and the subsequent VM-Enter to resume
 * the guest, i.e. so that the interrupt arrives in the guest with minimal
 * latency relative to the deadline programmed by the guest.
 */
static bool lapic_timer_advance __read_mostly = true;
module_param(lapic_timer_advance, bool, 0444);

#define LAPIC_TIMER_ADVANCE_ADJUST_MIN	100	/* clock cycles */
#define LAPIC_TIMER_ADVANCE_ADJUST_MAX	10000	/* clock cycles */
#define LAPIC_TIMER_ADVANCE_NS_INIT	1000
#define LAPIC_TIMER_ADVANCE_NS_MAX     5000
/* step-by-step approximation to mitigate fluctuation */
#define LAPIC_TIMER_ADVANCE_ADJUST_STEP 8

static bool __read_mostly vector_hashing_enabled = true;
module_param_named(vector_hashing, vector_hashing_enabled, bool, 0444);

static int kvm_lapic_msr_read(struct kvm_lapic *apic, u32 reg, u64 *data);
static int kvm_lapic_msr_write(struct kvm_lapic *apic, u32 reg, u64 data);

static inline void kvm_lapic_set_reg(struct kvm_lapic *apic, int reg_off, u32 val)
{
	apic_set_reg(apic->regs, reg_off, val);
}

static __always_inline u64 kvm_lapic_get_reg64(struct kvm_lapic *apic, int reg)
{
	return apic_get_reg64(apic->regs, reg);
}

static __always_inline void kvm_lapic_set_reg64(struct kvm_lapic *apic,
						int reg, u64 val)
{
	apic_set_reg64(apic->regs, reg, val);
}

bool kvm_apic_pending_eoi(struct kvm_vcpu *vcpu, int vector)
{
	struct kvm_lapic *apic = vcpu->arch.apic;

	return apic_test_vector(vector, apic->regs + APIC_ISR) ||
		apic_test_vector(vector, apic->regs + APIC_IRR);
}

__read_mostly DEFINE_STATIC_KEY_FALSE(kvm_has_noapic_vcpu);
EXPORT_SYMBOL_FOR_KVM_INTERNAL(kvm_has_noapic_vcpu);

__read_mostly DEFINE_STATIC_KEY_DEFERRED_FALSE(apic_hw_disabled, HZ);
__read_mostly DEFINE_STATIC_KEY_DEFERRED_FALSE(apic_sw_disabled, HZ);

static inline int apic_enabled(struct kvm_lapic *apic)
{
	return kvm_apic_sw_enabled(apic) &&	kvm_apic_hw_enabled(apic);
}

#define LVT_MASK	\
	(APIC_LVT_MASKED | APIC_SEND_PENDING | APIC_VECTOR_MASK)

#define LINT_MASK	\
	(LVT_MASK | APIC_MODE_MASK | APIC_INPUT_POLARITY | \
	 APIC_LVT_REMOTE_IRR | APIC_LVT_LEVEL_TRIGGER)

static inline u32 kvm_x2apic_id(struct kvm_lapic *apic)
{
	return apic->vcpu->vcpu_id;
}

static bool kvm_can_post_timer_interrupt(struct kvm_vcpu *vcpu)
{
	return pi_inject_timer && kvm_vcpu_apicv_active(vcpu) &&
		(kvm_mwait_in_guest(vcpu->kvm) || kvm_hlt_in_guest(vcpu->kvm));
}

static bool kvm_can_use_hv_timer(struct kvm_vcpu *vcpu)
{
	return kvm_x86_ops.set_hv_timer
	       && !(kvm_mwait_in_guest(vcpu->kvm) ||
		    kvm_can_post_timer_interrupt(vcpu));
}

static bool kvm_use_posted_timer_interrupt(struct kvm_vcpu *vcpu)
{
	return kvm_can_post_timer_interrupt(vcpu) && vcpu->mode == IN_GUEST_MODE;
}

static inline u32 kvm_apic_calc_x2apic_ldr(u32 id)
{
	return ((id >> 4) << 16) | (1 << (id & 0xf));
}

static inline bool kvm_apic_map_get_logical_dest(struct kvm_apic_map *map,
		u32 dest_id, struct kvm_lapic ***cluster, u16 *mask) {
	switch (map->logical_mode) {
	case KVM_APIC_MODE_SW_DISABLED:
		/* Arbitrarily use the flat map so that @cluster isn't NULL. */
		*cluster = map->xapic_flat_map;
		*mask = 0;
		return true;
	case KVM_APIC_MODE_X2APIC: {
		u32 offset = (dest_id >> 16) * 16;
		u32 max_apic_id = map->max_apic_id;

		if (offset <= max_apic_id) {
			u8 cluster_size = min(max_apic_id - offset + 1, 16U);

			offset = array_index_nospec(offset, map->max_apic_id + 1);
			*cluster = &map->phys_map[offset];
			*mask = dest_id & (0xffff >> (16 - cluster_size));
		} else {
			*mask = 0;
		}

		return true;
		}
	case KVM_APIC_MODE_XAPIC_FLAT:
		*cluster = map->xapic_flat_map;
		*mask = dest_id & 0xff;
		return true;
	case KVM_APIC_MODE_XAPIC_CLUSTER:
		*cluster = map->xapic_cluster_map[(dest_id >> 4) & 0xf];
		*mask = dest_id & 0xf;
		return true;
	case KVM_APIC_MODE_MAP_DISABLED:
		return false;
	default:
		WARN_ON_ONCE(1);
		return false;
	}
}

static int kvm_recalculate_phys_map(struct kvm_apic_map *new,
				    struct kvm_vcpu *vcpu,
				    bool *xapic_id_mismatch)
{
	struct kvm_lapic *apic = vcpu->arch.apic;
	u32 x2apic_id = kvm_x2apic_id(apic);
	u32 xapic_id = kvm_xapic_id(apic);
	u32 physical_id;

	/*
	 * For simplicity, KVM always allocates enough space for all possible
	 * xAPIC IDs.  Yell, but don't kill the VM, as KVM can continue on
	 * without the optimized map.
	 */
	if (WARN_ON_ONCE(xapic_id > new->max_apic_id))
		return -EINVAL;

	/*
	 * Bail if a vCPU was added and/or enabled its APIC between allocating
	 * the map and doing the actual calculations for the map.  Note, KVM
	 * hardcodes the x2APIC ID to vcpu_id, i.e. there's no TOCTOU bug if
	 * the compiler decides to reload x2apic_id after this check.
	 */
	if (x2apic_id > new->max_apic_id)
		return -E2BIG;

	/*
	 * Deliberately truncate the vCPU ID when detecting a mismatched APIC
	 * ID to avoid false positives if the vCPU ID, i.e. x2APIC ID, is a
	 * 32-bit value.  Any unwanted aliasing due to truncation results will
	 * be detected below.
	 */
	if (!apic_x2apic_mode(apic) && xapic_id != (u8)vcpu->vcpu_id)
		*xapic_id_mismatch = true;

	/*
	 * Apply KVM's hotplug hack if userspace has enable 32-bit APIC IDs.
	 * Allow sending events to vCPUs by their x2APIC ID even if the target
	 * vCPU is in legacy xAPIC mode, and silently ignore aliased xAPIC IDs
	 * (the x2APIC ID is truncated to 8 bits, causing IDs > 0xff to wrap
	 * and collide).
	 *
	 * Honor the architectural (and KVM's non-optimized) behavior if
	 * userspace has not enabled 32-bit x2APIC IDs.  Each APIC is supposed
	 * to process messages independently.  If multiple vCPUs have the same
	 * effective APIC ID, e.g. due to the x2APIC wrap or because the guest
	 * manually modified its xAPIC IDs, events targeting that ID are
	 * supposed to be recognized by all vCPUs with said ID.
	 */
	if (vcpu->kvm->arch.x2apic_format) {
		/* See also kvm_apic_match_physical_addr(). */
		if (apic_x2apic_mode(apic) || x2apic_id > 0xff)
			new->phys_map[x2apic_id] = apic;

		if (!apic_x2apic_mode(apic) && !new->phys_map[xapic_id])
			new->phys_map[xapic_id] = apic;
	} else {
		/*
		 * Disable the optimized map if the physical APIC ID is already
		 * mapped, i.e. is aliased to multiple vCPUs.  The optimized
		 * map requires a strict 1:1 mapping between IDs and vCPUs.
		 */
		if (apic_x2apic_mode(apic))
			physical_id = x2apic_id;
		else
			physical_id = xapic_id;

		if (new->phys_map[physical_id])
			return -EINVAL;

		new->phys_map[physical_id] = apic;
	}

	return 0;
}

static void kvm_recalculate_logical_map(struct kvm_apic_map *new,
					struct kvm_vcpu *vcpu)
{
	struct kvm_lapic *apic = vcpu->arch.apic;
	enum kvm_apic_logical_mode logical_mode;
	struct kvm_lapic **cluster;
	u16 mask;
	u32 ldr;

	if (new->logical_mode == KVM_APIC_MODE_MAP_DISABLED)
		return;

	if (!kvm_apic_sw_enabled(apic))
		return;

	ldr = kvm_lapic_get_reg(apic, APIC_LDR);
	if (!ldr)
		return;

	if (apic_x2apic_mode(apic)) {
		logical_mode = KVM_APIC_MODE_X2APIC;
	} else {
		ldr = GET_APIC_LOGICAL_ID(ldr);
		if (kvm_lapic_get_reg(apic, APIC_DFR) == APIC_DFR_FLAT)
			logical_mode = KVM_APIC_MODE_XAPIC_FLAT;
		else
			logical_mode = KVM_APIC_MODE_XAPIC_CLUSTER;
	}

	/*
	 * To optimize logical mode delivery, all software-enabled APICs must
	 * be configured for the same mode.
	 */
	if (new->logical_mode == KVM_APIC_MODE_SW_DISABLED) {
		new->logical_mode = logical_mode;
	} else if (new->logical_mode != logical_mode) {
		new->logical_mode = KVM_APIC_MODE_MAP_DISABLED;
		return;
	}

	/*
	 * In x2APIC mode, the LDR is read-only and derived directly from the
	 * x2APIC ID, thus is guaranteed to be addressable.  KVM reuses
	 * kvm_apic_map.phys_map to optimize logical mode x2APIC interrupts by
	 * reversing the LDR calculation to get cluster of APICs, i.e. no
	 * additional work is required.
	 */
	if (apic_x2apic_mode(apic))
		return;

	if (WARN_ON_ONCE(!kvm_apic_map_get_logical_dest(new, ldr,
							&cluster, &mask))) {
		new->logical_mode = KVM_APIC_MODE_MAP_DISABLED;
		return;
	}

	if (!mask)
		return;

	ldr = ffs(mask) - 1;
	if (!is_power_of_2(mask) || cluster[ldr])
		new->logical_mode = KVM_APIC_MODE_MAP_DISABLED;
	else
		cluster[ldr] = apic;
}

/*
 * CLEAN -> DIRTY and UPDATE_IN_PROGRESS -> DIRTY changes happen without a lock.
 *
 * DIRTY -> UPDATE_IN_PROGRESS and UPDATE_IN_PROGRESS -> CLEAN happen with
 * apic_map_lock_held.
 */
enum {
	CLEAN,
	UPDATE_IN_PROGRESS,
	DIRTY
};

static void kvm_recalculate_apic_map(struct kvm *kvm)
{
	struct kvm_apic_map *new, *old = NULL;
	struct kvm_vcpu *vcpu;
	unsigned long i;
	u32 max_id = 255; /* enough space for any xAPIC ID */
	bool xapic_id_mismatch;
	int r;

	/* Read kvm->arch.apic_map_dirty before kvm->arch.apic_map.  */
	if (atomic_read_acquire(&kvm->arch.apic_map_dirty) == CLEAN)
		return;

	WARN_ONCE(!irqchip_in_kernel(kvm),
		  "Dirty APIC map without an in-kernel local APIC");

	mutex_lock(&kvm->arch.apic_map_lock);

retry:
	/*
	 * Read kvm->arch.apic_map_dirty before kvm->arch.apic_map (if clean)
	 * or the APIC registers (if dirty).  Note, on retry the map may have
	 * not yet been marked dirty by whatever task changed a vCPU's x2APIC
	 * ID, i.e. the map may still show up as in-progress.  In that case
	 * this task still needs to retry and complete its calculation.
	 */
	if (atomic_cmpxchg_acquire(&kvm->arch.apic_map_dirty,
				   DIRTY, UPDATE_IN_PROGRESS) == CLEAN) {
		/* Someone else has updated the map. */
		mutex_unlock(&kvm->arch.apic_map_lock);
		return;
	}

	/*
	 * Reset the mismatch flag between attempts so that KVM does the right
	 * thing if a vCPU changes its xAPIC ID, but do NOT reset max_id, i.e.
	 * keep max_id strictly increasing.  Disallowing max_id from shrinking
	 * ensures KVM won't get stuck in an infinite loop, e.g. if the vCPU
	 * with the highest x2APIC ID is toggling its APIC on and off.
	 */
	xapic_id_mismatch = false;

	kvm_for_each_vcpu(i, vcpu, kvm)
		if (kvm_apic_present(vcpu))
			max_id = max(max_id, kvm_x2apic_id(vcpu->arch.apic));

	new = kvzalloc(sizeof(struct kvm_apic_map) +
	                   sizeof(struct kvm_lapic *) * ((u64)max_id + 1),
			   GFP_KERNEL_ACCOUNT);

	if (!new)
		goto out;

	new->max_apic_id = max_id;
	new->logical_mode = KVM_APIC_MODE_SW_DISABLED;

	kvm_for_each_vcpu(i, vcpu, kvm) {
		if (!kvm_apic_present(vcpu))
			continue;

		r = kvm_recalculate_phys_map(new, vcpu, &xapic_id_mismatch);
		if (r) {
			kvfree(new);
			new = NULL;
			if (r == -E2BIG) {
				cond_resched();
				goto retry;
			}

			goto out;
		}

		kvm_recalculate_logical_map(new, vcpu);
	}
out:
	/*
	 * The optimized map is effectively KVM's internal version of APICv,
	 * and all unwanted aliasing that results in disabling the optimized
	 * map also applies to APICv.
	 */
	if (!new)
		kvm_set_apicv_inhibit(kvm, APICV_INHIBIT_REASON_PHYSICAL_ID_ALIASED);
	else
		kvm_clear_apicv_inhibit(kvm, APICV_INHIBIT_REASON_PHYSICAL_ID_ALIASED);

	if (!new || new->logical_mode == KVM_APIC_MODE_MAP_DISABLED)
		kvm_set_apicv_inhibit(kvm, APICV_INHIBIT_REASON_LOGICAL_ID_ALIASED);
	else
		kvm_clear_apicv_inhibit(kvm, APICV_INHIBIT_REASON_LOGICAL_ID_ALIASED);

	if (xapic_id_mismatch)
		kvm_set_apicv_inhibit(kvm, APICV_INHIBIT_REASON_APIC_ID_MODIFIED);
	else
		kvm_clear_apicv_inhibit(kvm, APICV_INHIBIT_REASON_APIC_ID_MODIFIED);

	old = rcu_dereference_protected(kvm->arch.apic_map,
			lockdep_is_held(&kvm->arch.apic_map_lock));
	rcu_assign_pointer(kvm->arch.apic_map, new);
	/*
	 * Write kvm->arch.apic_map before clearing apic->apic_map_dirty.
	 * If another update has come in, leave it DIRTY.
	 */
	atomic_cmpxchg_release(&kvm->arch.apic_map_dirty,
			       UPDATE_IN_PROGRESS, CLEAN);
	mutex_unlock(&kvm->arch.apic_map_lock);

	if (old)
		kvfree_rcu(old, rcu);

	kvm_make_scan_ioapic_request(kvm);
}

static inline void apic_set_spiv(struct kvm_lapic *apic, u32 val)
{
	bool enabled = val & APIC_SPIV_APIC_ENABLED;

	kvm_lapic_set_reg(apic, APIC_SPIV, val);

	if (enabled != apic->sw_enabled) {
		apic->sw_enabled = enabled;
		if (enabled)
			static_branch_slow_dec_deferred(&apic_sw_disabled);
		else
			static_branch_inc(&apic_sw_disabled.key);

		atomic_set_release(&apic->vcpu->kvm->arch.apic_map_dirty, DIRTY);
	}

	/* Check if there are APF page ready requests pending */
	if (enabled) {
		kvm_make_request(KVM_REQ_APF_READY, apic->vcpu);
		kvm_xen_sw_enable_lapic(apic->vcpu);
	}
}

static inline void kvm_apic_set_xapic_id(struct kvm_lapic *apic, u8 id)
{
	kvm_lapic_set_reg(apic, APIC_ID, id << 24);
	atomic_set_release(&apic->vcpu->kvm->arch.apic_map_dirty, DIRTY);
}

static inline void kvm_apic_set_ldr(struct kvm_lapic *apic, u32 id)
{
	kvm_lapic_set_reg(apic, APIC_LDR, id);
	atomic_set_release(&apic->vcpu->kvm->arch.apic_map_dirty, DIRTY);
}

static inline void kvm_apic_set_dfr(struct kvm_lapic *apic, u32 val)
{
	kvm_lapic_set_reg(apic, APIC_DFR, val);
	atomic_set_release(&apic->vcpu->kvm->arch.apic_map_dirty, DIRTY);
}

static inline void kvm_apic_set_x2apic_id(struct kvm_lapic *apic, u32 id)
{
	u32 ldr = kvm_apic_calc_x2apic_ldr(id);

	WARN_ON_ONCE(id != apic->vcpu->vcpu_id);

	kvm_lapic_set_reg(apic, APIC_ID, id);
	kvm_lapic_set_reg(apic, APIC_LDR, ldr);
	atomic_set_release(&apic->vcpu->kvm->arch.apic_map_dirty, DIRTY);
}

static inline int apic_lvt_enabled(struct kvm_lapic *apic, int lvt_type)
{
	return !(kvm_lapic_get_reg(apic, lvt_type) & APIC_LVT_MASKED);
}

static inline int apic_lvtt_oneshot(struct kvm_lapic *apic)
{
	return apic->lapic_timer.timer_mode == APIC_LVT_TIMER_ONESHOT;
}

static inline int apic_lvtt_period(struct kvm_lapic *apic)
{
	return apic->lapic_timer.timer_mode == APIC_LVT_TIMER_PERIODIC;
}

static inline int apic_lvtt_tscdeadline(struct kvm_lapic *apic)
{
	return apic->lapic_timer.timer_mode == APIC_LVT_TIMER_TSCDEADLINE;
}

static inline int apic_lvt_nmi_mode(u32 lvt_val)
{
	return (lvt_val & (APIC_MODE_MASK | APIC_LVT_MASKED)) == APIC_DM_NMI;
}

static inline bool kvm_lapic_lvt_supported(struct kvm_lapic *apic, int lvt_index)
{
	return apic->nr_lvt_entries > lvt_index;
}

static inline int kvm_apic_calc_nr_lvt_entries(struct kvm_vcpu *vcpu)
{
	return KVM_APIC_MAX_NR_LVT_ENTRIES - !(vcpu->arch.mcg_cap & MCG_CMCI_P);
}

void kvm_apic_set_version(struct kvm_vcpu *vcpu)
{
	struct kvm_lapic *apic = vcpu->arch.apic;
	u32 v = 0;

	if (!lapic_in_kernel(vcpu))
		return;

	v = APIC_VERSION | ((apic->nr_lvt_entries - 1) << 16);

	/*
	 * KVM emulates 82093AA datasheet (with in-kernel IOAPIC implementation)
	 * which doesn't have EOI register; Some buggy OSes (e.g. Windows with
	 * Hyper-V role) disable EOI broadcast in lapic not checking for IOAPIC
	 * version first and level-triggered interrupts never get EOIed in
	 * IOAPIC.
	 */
	if (guest_cpu_cap_has(vcpu, X86_FEATURE_X2APIC) &&
	    !ioapic_in_kernel(vcpu->kvm))
		v |= APIC_LVR_DIRECTED_EOI;
	kvm_lapic_set_reg(apic, APIC_LVR, v);
}

void kvm_apic_after_set_mcg_cap(struct kvm_vcpu *vcpu)
{
	int nr_lvt_entries = kvm_apic_calc_nr_lvt_entries(vcpu);
	struct kvm_lapic *apic = vcpu->arch.apic;
	int i;

	if (!lapic_in_kernel(vcpu) || nr_lvt_entries == apic->nr_lvt_entries)
		return;

	/* Initialize/mask any "new" LVT entries. */
	for (i = apic->nr_lvt_entries; i < nr_lvt_entries; i++)
		kvm_lapic_set_reg(apic, APIC_LVTx(i), APIC_LVT_MASKED);

	apic->nr_lvt_entries = nr_lvt_entries;

	/* The number of LVT entries is reflected in the version register. */
	kvm_apic_set_version(vcpu);
}

static const unsigned int apic_lvt_mask[KVM_APIC_MAX_NR_LVT_ENTRIES] = {
	[LVT_TIMER] = LVT_MASK,      /* timer mode mask added at runtime */
	[LVT_THERMAL_MONITOR] = LVT_MASK | APIC_MODE_MASK,
	[LVT_PERFORMANCE_COUNTER] = LVT_MASK | APIC_MODE_MASK,
	[LVT_LINT0] = LINT_MASK,
	[LVT_LINT1] = LINT_MASK,
	[LVT_ERROR] = LVT_MASK,
	[LVT_CMCI] = LVT_MASK | APIC_MODE_MASK
};

static u8 count_vectors(void *bitmap)
{
	int vec;
	u32 *reg;
	u8 count = 0;

	for (vec = 0; vec < MAX_APIC_VECTOR; vec += APIC_VECTORS_PER_REG) {
		reg = bitmap + APIC_VECTOR_TO_REG_OFFSET(vec);
		count += hweight32(*reg);
	}

	return count;
}

bool __kvm_apic_update_irr(unsigned long *pir, void *regs, int *max_irr)
{
	unsigned long pir_vals[NR_PIR_WORDS];
	u32 *__pir = (void *)pir_vals;
	u32 i, vec;
	u32 irr_val, prev_irr_val;
	int max_updated_irr;

	max_updated_irr = -1;
	*max_irr = -1;

	if (!pi_harvest_pir(pir, pir_vals))
		return false;

	for (i = vec = 0; i <= 7; i++, vec += 32) {
		u32 *p_irr = (u32 *)(regs + APIC_IRR + i * 0x10);

		irr_val = READ_ONCE(*p_irr);

		if (__pir[i]) {
			prev_irr_val = irr_val;
			do {
				irr_val = prev_irr_val | __pir[i];
			} while (prev_irr_val != irr_val &&
				 !try_cmpxchg(p_irr, &prev_irr_val, irr_val));

			if (prev_irr_val != irr_val)
				max_updated_irr = __fls(irr_val ^ prev_irr_val) + vec;
		}
		if (irr_val)
			*max_irr = __fls(irr_val) + vec;
	}

	return ((max_updated_irr != -1) &&
		(max_updated_irr == *max_irr));
}
EXPORT_SYMBOL_FOR_KVM_INTERNAL(__kvm_apic_update_irr);

bool kvm_apic_update_irr(struct kvm_vcpu *vcpu, unsigned long *pir, int *max_irr)
{
	struct kvm_lapic *apic = vcpu->arch.apic;
	bool irr_updated = __kvm_apic_update_irr(pir, apic->regs, max_irr);

	if (unlikely(!apic->apicv_active && irr_updated))
		apic->irr_pending = true;
	return irr_updated;
}
EXPORT_SYMBOL_FOR_KVM_INTERNAL(kvm_apic_update_irr);

static inline int apic_search_irr(struct kvm_lapic *apic)
{
	return apic_find_highest_vector(apic->regs + APIC_IRR);
}

static inline int apic_find_highest_irr(struct kvm_lapic *apic)
{
	int result;

	/*
	 * Note that irr_pending is just a hint. It will be always
	 * true with virtual interrupt delivery enabled.
	 */
	if (!apic->irr_pending)
		return -1;

	result = apic_search_irr(apic);
	ASSERT(result == -1 || result >= 16);

	return result;
}

static inline void apic_clear_irr(int vec, struct kvm_lapic *apic)
{
	if (unlikely(apic->apicv_active)) {
		apic_clear_vector(vec, apic->regs + APIC_IRR);
	} else {
		apic->irr_pending = false;
		apic_clear_vector(vec, apic->regs + APIC_IRR);
		if (apic_search_irr(apic) != -1)
			apic->irr_pending = true;
	}
}

void kvm_apic_clear_irr(struct kvm_vcpu *vcpu, int vec)
{
	apic_clear_irr(vec, vcpu->arch.apic);
}
EXPORT_SYMBOL_FOR_KVM_INTERNAL(kvm_apic_clear_irr);

static void *apic_vector_to_isr(int vec, struct kvm_lapic *apic)
{
	return apic->regs + APIC_ISR + APIC_VECTOR_TO_REG_OFFSET(vec);
}

static inline void apic_set_isr(int vec, struct kvm_lapic *apic)
{
	if (__test_and_set_bit(APIC_VECTOR_TO_BIT_NUMBER(vec),
			       apic_vector_to_isr(vec, apic)))
		return;

	/*
	 * With APIC virtualization enabled, all caching is disabled
	 * because the processor can modify ISR under the hood.  Instead
	 * just set SVI.
	 */
	if (unlikely(apic->apicv_active))
		kvm_x86_call(hwapic_isr_update)(apic->vcpu, vec);
	else {
		++apic->isr_count;
		BUG_ON(apic->isr_count > MAX_APIC_VECTOR);
		/*
		 * ISR (in service register) bit is set when injecting an interrupt.
		 * The highest vector is injected. Thus the latest bit set matches
		 * the highest bit in ISR.
		 */
		apic->highest_isr_cache = vec;
	}
}

static inline int apic_find_highest_isr(struct kvm_lapic *apic)
{
	int result;

	/*
	 * Note that isr_count is always 1, and highest_isr_cache
	 * is always -1, with APIC virtualization enabled.
	 */
	if (!apic->isr_count)
		return -1;
	if (likely(apic->highest_isr_cache != -1))
		return apic->highest_isr_cache;

	result = apic_find_highest_vector(apic->regs + APIC_ISR);
	ASSERT(result == -1 || result >= 16);

	return result;
}

static inline void apic_clear_isr(int vec, struct kvm_lapic *apic)
{
	if (!__test_and_clear_bit(APIC_VECTOR_TO_BIT_NUMBER(vec),
				  apic_vector_to_isr(vec, apic)))
		return;

	/*
	 * We do get here for APIC virtualization enabled if the guest
	 * uses the Hyper-V APIC enlightenment.  In this case we may need
	 * to trigger a new interrupt delivery by writing the SVI field;
	 * on the other hand isr_count and highest_isr_cache are unused
	 * and must be left alone.
	 */
	if (unlikely(apic->apicv_active))
		kvm_x86_call(hwapic_isr_update)(apic->vcpu, apic_find_highest_isr(apic));
	else {
		--apic->isr_count;
		BUG_ON(apic->isr_count < 0);
		apic->highest_isr_cache = -1;
	}
}

void kvm_apic_update_hwapic_isr(struct kvm_vcpu *vcpu)
{
	struct kvm_lapic *apic = vcpu->arch.apic;

	if (WARN_ON_ONCE(!lapic_in_kernel(vcpu)) || !apic->apicv_active)
		return;

	kvm_x86_call(hwapic_isr_update)(vcpu, apic_find_highest_isr(apic));
}
EXPORT_SYMBOL_FOR_KVM_INTERNAL(kvm_apic_update_hwapic_isr);

int kvm_lapic_find_highest_irr(struct kvm_vcpu *vcpu)
{
	/* This may race with setting of irr in __apic_accept_irq() and
	 * value returned may be wrong, but kvm_vcpu_kick() in __apic_accept_irq
	 * will cause vmexit immediately and the value will be recalculated
	 * on the next vmentry.
	 */
	return apic_find_highest_irr(vcpu->arch.apic);
}
EXPORT_SYMBOL_FOR_KVM_INTERNAL(kvm_lapic_find_highest_irr);

static int __apic_accept_irq(struct kvm_lapic *apic, int delivery_mode,
			     int vector, int level, int trig_mode,
			     struct dest_map *dest_map);

int kvm_apic_set_irq(struct kvm_vcpu *vcpu, struct kvm_lapic_irq *irq,
		     struct dest_map *dest_map)
{
	struct kvm_lapic *apic = vcpu->arch.apic;

	return __apic_accept_irq(apic, irq->delivery_mode, irq->vector,
			irq->level, irq->trig_mode, dest_map);
}

static int __pv_send_ipi(unsigned long *ipi_bitmap, struct kvm_apic_map *map,
			 struct kvm_lapic_irq *irq, u32 min)
{
	int i, count = 0;
	struct kvm_vcpu *vcpu;

	if (min > map->max_apic_id)
		return 0;

	min = array_index_nospec(min, map->max_apic_id + 1);

	for_each_set_bit(i, ipi_bitmap,
		min((u32)BITS_PER_LONG, (map->max_apic_id - min + 1))) {
		if (map->phys_map[min + i]) {
			vcpu = map->phys_map[min + i]->vcpu;
			count += kvm_apic_set_irq(vcpu, irq, NULL);
		}
	}

	return count;
}

int kvm_pv_send_ipi(struct kvm *kvm, unsigned long ipi_bitmap_low,
		    unsigned long ipi_bitmap_high, u32 min,
		    unsigned long icr, int op_64_bit)
{
	struct kvm_apic_map *map;
	struct kvm_lapic_irq irq = {0};
	int cluster_size = op_64_bit ? 64 : 32;
	int count;

	if (icr & (APIC_DEST_MASK | APIC_SHORT_MASK))
		return -KVM_EINVAL;

	irq.vector = icr & APIC_VECTOR_MASK;
	irq.delivery_mode = icr & APIC_MODE_MASK;
	irq.level = (icr & APIC_INT_ASSERT) != 0;
	irq.trig_mode = icr & APIC_INT_LEVELTRIG;

	rcu_read_lock();
	map = rcu_dereference(kvm->arch.apic_map);

	count = -EOPNOTSUPP;
	if (likely(map)) {
		count = __pv_send_ipi(&ipi_bitmap_low, map, &irq, min);
		min += cluster_size;
		count += __pv_send_ipi(&ipi_bitmap_high, map, &irq, min);
	}

	rcu_read_unlock();
	return count;
}

static int pv_eoi_put_user(struct kvm_vcpu *vcpu, u8 val)
{

	return kvm_write_guest_cached(vcpu->kvm, &vcpu->arch.pv_eoi.data, &val,
				      sizeof(val));
}

static int pv_eoi_get_user(struct kvm_vcpu *vcpu, u8 *val)
{

	return kvm_read_guest_cached(vcpu->kvm, &vcpu->arch.pv_eoi.data, val,
				      sizeof(*val));
}

static inline bool pv_eoi_enabled(struct kvm_vcpu *vcpu)
{
	return vcpu->arch.pv_eoi.msr_val & KVM_MSR_ENABLED;
}

static void pv_eoi_set_pending(struct kvm_vcpu *vcpu)
{
	if (pv_eoi_put_user(vcpu, KVM_PV_EOI_ENABLED) < 0)
		return;

	__set_bit(KVM_APIC_PV_EOI_PENDING, &vcpu->arch.apic_attention);
}

static bool pv_eoi_test_and_clr_pending(struct kvm_vcpu *vcpu)
{
	u8 val;

	if (pv_eoi_get_user(vcpu, &val) < 0)
		return false;

	val &= KVM_PV_EOI_ENABLED;

	if (val && pv_eoi_put_user(vcpu, KVM_PV_EOI_DISABLED) < 0)
		return false;

	/*
	 * Clear pending bit in any case: it will be set again on vmentry.
	 * While this might not be ideal from performance point of view,
	 * this makes sure pv eoi is only enabled when we know it's safe.
	 */
	__clear_bit(KVM_APIC_PV_EOI_PENDING, &vcpu->arch.apic_attention);

	return val;
}

static int apic_has_interrupt_for_ppr(struct kvm_lapic *apic, u32 ppr)
{
	int highest_irr;
	if (kvm_x86_ops.sync_pir_to_irr)
		highest_irr = kvm_x86_call(sync_pir_to_irr)(apic->vcpu);
	else
		highest_irr = apic_find_highest_irr(apic);
	if (highest_irr == -1 || (highest_irr & 0xF0) <= ppr)
		return -1;
	return highest_irr;
}

static bool __apic_update_ppr(struct kvm_lapic *apic, u32 *new_ppr)
{
	u32 tpr, isrv, ppr, old_ppr;
	int isr;

	old_ppr = kvm_lapic_get_reg(apic, APIC_PROCPRI);
	tpr = kvm_lapic_get_reg(apic, APIC_TASKPRI);
	isr = apic_find_highest_isr(apic);
	isrv = (isr != -1) ? isr : 0;

	if ((tpr & 0xf0) >= (isrv & 0xf0))
		ppr = tpr & 0xff;
	else
		ppr = isrv & 0xf0;

	*new_ppr = ppr;
	if (old_ppr != ppr)
		kvm_lapic_set_reg(apic, APIC_PROCPRI, ppr);

	return ppr < old_ppr;
}

static void apic_update_ppr(struct kvm_lapic *apic)
{
	u32 ppr;

	if (__apic_update_ppr(apic, &ppr) &&
	    apic_has_interrupt_for_ppr(apic, ppr) != -1)
		kvm_make_request(KVM_REQ_EVENT, apic->vcpu);
}

void kvm_apic_update_ppr(struct kvm_vcpu *vcpu)
{
	apic_update_ppr(vcpu->arch.apic);
}
EXPORT_SYMBOL_FOR_KVM_INTERNAL(kvm_apic_update_ppr);

static void apic_set_tpr(struct kvm_lapic *apic, u32 tpr)
{
	kvm_lapic_set_reg(apic, APIC_TASKPRI, tpr);
	apic_update_ppr(apic);
}

static bool kvm_apic_broadcast(struct kvm_lapic *apic, u32 mda)
{
	return mda == (apic_x2apic_mode(apic) ?
			X2APIC_BROADCAST : APIC_BROADCAST);
}

static bool kvm_apic_match_physical_addr(struct kvm_lapic *apic, u32 mda)
{
	if (kvm_apic_broadcast(apic, mda))
		return true;

	/*
	 * Hotplug hack: Accept interrupts for vCPUs in xAPIC mode as if they
	 * were in x2APIC mode if the target APIC ID can't be encoded as an
	 * xAPIC ID.  This allows unique addressing of hotplugged vCPUs (which
	 * start in xAPIC mode) with an APIC ID that is unaddressable in xAPIC
	 * mode.  Match the x2APIC ID if and only if the target APIC ID can't
	 * be encoded in xAPIC to avoid spurious matches against a vCPU that
	 * changed its (addressable) xAPIC ID (which is writable).
	 */
	if (apic_x2apic_mode(apic) || mda > 0xff)
		return mda == kvm_x2apic_id(apic);

	return mda == kvm_xapic_id(apic);
}

static bool kvm_apic_match_logical_addr(struct kvm_lapic *apic, u32 mda)
{
	u32 logical_id;

	if (kvm_apic_broadcast(apic, mda))
		return true;

	logical_id = kvm_lapic_get_reg(apic, APIC_LDR);

	if (apic_x2apic_mode(apic))
		return ((logical_id >> 16) == (mda >> 16))
		       && (logical_id & mda & 0xffff) != 0;

	logical_id = GET_APIC_LOGICAL_ID(logical_id);

	switch (kvm_lapic_get_reg(apic, APIC_DFR)) {
	case APIC_DFR_FLAT:
		return (logical_id & mda) != 0;
	case APIC_DFR_CLUSTER:
		return ((logical_id >> 4) == (mda >> 4))
		       && (logical_id & mda & 0xf) != 0;
	default:
		return false;
	}
}

/* The KVM local APIC implementation has two quirks:
 *
 *  - Real hardware delivers interrupts destined to x2APIC ID > 0xff to LAPICs
 *    in xAPIC mode if the "destination & 0xff" matches its xAPIC ID.
 *    KVM doesn't do that aliasing.
 *
 *  - in-kernel IOAPIC messages have to be delivered directly to
 *    x2APIC, because the kernel does not support interrupt remapping.
 *    In order to support broadcast without interrupt remapping, x2APIC
 *    rewrites the destination of non-IPI messages from APIC_BROADCAST
 *    to X2APIC_BROADCAST.
 *
 * The broadcast quirk can be disabled with KVM_CAP_X2APIC_API.  This is
 * important when userspace wants to use x2APIC-format MSIs, because
 * APIC_BROADCAST (0xff) is a legal route for "cluster 0, CPUs 0-7".
 */
static u32 kvm_apic_mda(struct kvm_vcpu *vcpu, unsigned int dest_id,
		struct kvm_lapic *source, struct kvm_lapic *target)
{
	bool ipi = source != NULL;

	if (!vcpu->kvm->arch.x2apic_broadcast_quirk_disabled &&
	    !ipi && dest_id == APIC_BROADCAST && apic_x2apic_mode(target))
		return X2APIC_BROADCAST;

	return dest_id;
}

bool kvm_apic_match_dest(struct kvm_vcpu *vcpu, struct kvm_lapic *source,
			   int shorthand, unsigned int dest, int dest_mode)
{
	struct kvm_lapic *target = vcpu->arch.apic;
	u32 mda = kvm_apic_mda(vcpu, dest, source, target);

	ASSERT(target);
	switch (shorthand) {
	case APIC_DEST_NOSHORT:
		if (dest_mode == APIC_DEST_PHYSICAL)
			return kvm_apic_match_physical_addr(target, mda);
		else
			return kvm_apic_match_logical_addr(target, mda);
	case APIC_DEST_SELF:
		return target == source;
	case APIC_DEST_ALLINC:
		return true;
	case APIC_DEST_ALLBUT:
		return target != source;
	default:
		return false;
	}
}
EXPORT_SYMBOL_FOR_KVM_INTERNAL(kvm_apic_match_dest);

static int kvm_vector_to_index(u32 vector, u32 dest_vcpus,
			       const unsigned long *bitmap, u32 bitmap_size)
{
	int idx = find_nth_bit(bitmap, bitmap_size, vector % dest_vcpus);

	BUG_ON(idx >= bitmap_size);
	return idx;
}

static void kvm_apic_disabled_lapic_found(struct kvm *kvm)
{
	if (!kvm->arch.disabled_lapic_found) {
		kvm->arch.disabled_lapic_found = true;
		pr_info("Disabled LAPIC found during irq injection\n");
	}
}

static bool kvm_apic_is_broadcast_dest(struct kvm *kvm, struct kvm_lapic **src,
		struct kvm_lapic_irq *irq, struct kvm_apic_map *map)
{
	if (kvm->arch.x2apic_broadcast_quirk_disabled) {
		if ((irq->dest_id == APIC_BROADCAST &&
		     map->logical_mode != KVM_APIC_MODE_X2APIC))
			return true;
		if (irq->dest_id == X2APIC_BROADCAST)
			return true;
	} else {
		bool x2apic_ipi = src && *src && apic_x2apic_mode(*src);
		if (irq->dest_id == (x2apic_ipi ?
		                     X2APIC_BROADCAST : APIC_BROADCAST))
			return true;
	}

	return false;
}

static bool kvm_lowest_prio_delivery(struct kvm_lapic_irq *irq)
{
	return (irq->delivery_mode == APIC_DM_LOWEST || irq->msi_redir_hint);
}

static int kvm_apic_compare_prio(struct kvm_vcpu *vcpu1, struct kvm_vcpu *vcpu2)
{
	return vcpu1->arch.apic_arb_prio - vcpu2->arch.apic_arb_prio;
}

/* Return true if the interrupt can be handled by using *bitmap as index mask
 * for valid destinations in *dst array.
 * Return false if kvm_apic_map_get_dest_lapic did nothing useful.
 * Note: we may have zero kvm_lapic destinations when we return true, which
 * means that the interrupt should be dropped.  In this case, *bitmap would be
 * zero and *dst undefined.
 */
static inline bool kvm_apic_map_get_dest_lapic(struct kvm *kvm,
		struct kvm_lapic **src, struct kvm_lapic_irq *irq,
		struct kvm_apic_map *map, struct kvm_lapic ***dst,
		unsigned long *bitmap)
{
	int i, lowest;

	if (irq->shorthand == APIC_DEST_SELF && src) {
		*dst = src;
		*bitmap = 1;
		return true;
	} else if (irq->shorthand)
		return false;

	if (!map || kvm_apic_is_broadcast_dest(kvm, src, irq, map))
		return false;

	if (irq->dest_mode == APIC_DEST_PHYSICAL) {
		if (irq->dest_id > map->max_apic_id) {
			*bitmap = 0;
		} else {
			u32 dest_id = array_index_nospec(irq->dest_id, map->max_apic_id + 1);
			*dst = &map->phys_map[dest_id];
			*bitmap = 1;
		}
		return true;
	}

	*bitmap = 0;
	if (!kvm_apic_map_get_logical_dest(map, irq->dest_id, dst,
				(u16 *)bitmap))
		return false;

	if (!kvm_lowest_prio_delivery(irq))
		return true;

	if (!vector_hashing_enabled) {
		lowest = -1;
		for_each_set_bit(i, bitmap, 16) {
			if (!(*dst)[i])
				continue;
			if (lowest < 0)
				lowest = i;
			else if (kvm_apic_compare_prio((*dst)[i]->vcpu,
						(*dst)[lowest]->vcpu) < 0)
				lowest = i;
		}
	} else {
		if (!*bitmap)
			return true;

		lowest = kvm_vector_to_index(irq->vector, hweight16(*bitmap),
				bitmap, 16);

		if (!(*dst)[lowest]) {
			kvm_apic_disabled_lapic_found(kvm);
			*bitmap = 0;
			return true;
		}
	}

	*bitmap = (lowest >= 0) ? 1 << lowest : 0;

	return true;
}

bool kvm_irq_delivery_to_apic_fast(struct kvm *kvm, struct kvm_lapic *src,
		struct kvm_lapic_irq *irq, int *r, struct dest_map *dest_map)
{
	struct kvm_apic_map *map;
	unsigned long bitmap;
	struct kvm_lapic **dst = NULL;
	int i;
	bool ret;

	*r = -1;

	if (irq->shorthand == APIC_DEST_SELF) {
		if (KVM_BUG_ON(!src, kvm)) {
			*r = 0;
			return true;
		}
		*r = kvm_apic_set_irq(src->vcpu, irq, dest_map);
		return true;
	}

	rcu_read_lock();
	map = rcu_dereference(kvm->arch.apic_map);

	ret = kvm_apic_map_get_dest_lapic(kvm, &src, irq, map, &dst, &bitmap);
	if (ret) {
		*r = 0;
		for_each_set_bit(i, &bitmap, 16) {
			if (!dst[i])
				continue;
			*r += kvm_apic_set_irq(dst[i]->vcpu, irq, dest_map);
		}
	}

	rcu_read_unlock();
	return ret;
}

/*
 * This routine tries to handle interrupts in posted mode, here is how
 * it deals with different cases:
 * - For single-destination interrupts, handle it in posted mode
 * - Else if vector hashing is enabled and it is a lowest-priority
 *   interrupt, handle it in posted mode and use the following mechanism
 *   to find the destination vCPU.
 *	1. For lowest-priority interrupts, store all the possible
 *	   destination vCPUs in an array.
 *	2. Use "guest vector % max number of destination vCPUs" to find
 *	   the right destination vCPU in the array for the lowest-priority
 *	   interrupt.
 * - Otherwise, use remapped mode to inject the interrupt.
 */
static bool kvm_intr_is_single_vcpu_fast(struct kvm *kvm,
					 struct kvm_lapic_irq *irq,
					 struct kvm_vcpu **dest_vcpu)
{
	struct kvm_apic_map *map;
	unsigned long bitmap;
	struct kvm_lapic **dst = NULL;
	bool ret = false;

	if (irq->shorthand)
		return false;

	rcu_read_lock();
	map = rcu_dereference(kvm->arch.apic_map);

	if (kvm_apic_map_get_dest_lapic(kvm, NULL, irq, map, &dst, &bitmap) &&
			hweight16(bitmap) == 1) {
		unsigned long i = find_first_bit(&bitmap, 16);

		if (dst[i]) {
			*dest_vcpu = dst[i]->vcpu;
			ret = true;
		}
	}

	rcu_read_unlock();
	return ret;
}

bool kvm_intr_is_single_vcpu(struct kvm *kvm, struct kvm_lapic_irq *irq,
			     struct kvm_vcpu **dest_vcpu)
{
	int r = 0;
	unsigned long i;
	struct kvm_vcpu *vcpu;

	if (kvm_intr_is_single_vcpu_fast(kvm, irq, dest_vcpu))
		return true;

	kvm_for_each_vcpu(i, vcpu, kvm) {
		if (!kvm_apic_present(vcpu))
			continue;

		if (!kvm_apic_match_dest(vcpu, NULL, irq->shorthand,
					irq->dest_id, irq->dest_mode))
			continue;

		if (++r == 2)
			return false;

		*dest_vcpu = vcpu;
	}

	return r == 1;
}
EXPORT_SYMBOL_FOR_KVM_INTERNAL(kvm_intr_is_single_vcpu);

int kvm_irq_delivery_to_apic(struct kvm *kvm, struct kvm_lapic *src,
			     struct kvm_lapic_irq *irq, struct dest_map *dest_map)
{
	int r = -1;
	struct kvm_vcpu *vcpu, *lowest = NULL;
	unsigned long i, dest_vcpu_bitmap[BITS_TO_LONGS(KVM_MAX_VCPUS)];
	unsigned int dest_vcpus = 0;

	if (kvm_irq_delivery_to_apic_fast(kvm, src, irq, &r, dest_map))
		return r;

	if (irq->dest_mode == APIC_DEST_PHYSICAL &&
	    irq->dest_id == 0xff && kvm_lowest_prio_delivery(irq)) {
		pr_info("apic: phys broadcast and lowest prio\n");
		irq->delivery_mode = APIC_DM_FIXED;
	}

	memset(dest_vcpu_bitmap, 0, sizeof(dest_vcpu_bitmap));

	kvm_for_each_vcpu(i, vcpu, kvm) {
		if (!kvm_apic_present(vcpu))
			continue;

		if (!kvm_apic_match_dest(vcpu, src, irq->shorthand,
					irq->dest_id, irq->dest_mode))
			continue;

		if (!kvm_lowest_prio_delivery(irq)) {
			if (r < 0)
				r = 0;
			r += kvm_apic_set_irq(vcpu, irq, dest_map);
		} else if (kvm_apic_sw_enabled(vcpu->arch.apic)) {
			if (!vector_hashing_enabled) {
				if (!lowest)
					lowest = vcpu;
				else if (kvm_apic_compare_prio(vcpu, lowest) < 0)
					lowest = vcpu;
			} else {
				__set_bit(i, dest_vcpu_bitmap);
				dest_vcpus++;
			}
		}
	}

	if (dest_vcpus != 0) {
		int idx = kvm_vector_to_index(irq->vector, dest_vcpus,
					dest_vcpu_bitmap, KVM_MAX_VCPUS);

		lowest = kvm_get_vcpu(kvm, idx);
	}

	if (lowest)
		r = kvm_apic_set_irq(lowest, irq, dest_map);

	return r;
}

/*
 * Add a pending IRQ into lapic.
 * Return 1 if successfully added and 0 if discarded.
 */
static int __apic_accept_irq(struct kvm_lapic *apic, int delivery_mode,
			     int vector, int level, int trig_mode,
			     struct dest_map *dest_map)
{
	int result = 0;
	struct kvm_vcpu *vcpu = apic->vcpu;

	trace_kvm_apic_accept_irq(vcpu->vcpu_id, delivery_mode,
				  trig_mode, vector);
	switch (delivery_mode) {
	case APIC_DM_LOWEST:
		vcpu->arch.apic_arb_prio++;
		fallthrough;
	case APIC_DM_FIXED:
		if (unlikely(trig_mode && !level))
			break;

		/* FIXME add logic for vcpu on reset */
		if (unlikely(!apic_enabled(apic)))
			break;

		result = 1;

		if (dest_map) {
			__set_bit(vcpu->vcpu_id, dest_map->map);
			dest_map->vectors[vcpu->vcpu_id] = vector;
		}

		if (apic_test_vector(vector, apic->regs + APIC_TMR) != !!trig_mode) {
			if (trig_mode)
				apic_set_vector(vector, apic->regs + APIC_TMR);
			else
				apic_clear_vector(vector, apic->regs + APIC_TMR);
		}

		kvm_x86_call(deliver_interrupt)(apic, delivery_mode,
						trig_mode, vector);
		break;

	case APIC_DM_REMRD:
		result = 1;
		vcpu->arch.pv.pv_unhalted = 1;
		kvm_make_request(KVM_REQ_EVENT, vcpu);
		kvm_vcpu_kick(vcpu);
		break;

	case APIC_DM_SMI:
		if (!kvm_inject_smi(vcpu)) {
			kvm_vcpu_kick(vcpu);
			result = 1;
		}
		break;

	case APIC_DM_NMI:
		result = 1;
		kvm_inject_nmi(vcpu);
		kvm_vcpu_kick(vcpu);
		break;

	case APIC_DM_INIT:
		if (!trig_mode || level) {
			result = 1;
			/* assumes that there are only KVM_APIC_INIT/SIPI */
			apic->pending_events = (1UL << KVM_APIC_INIT);
			kvm_make_request(KVM_REQ_EVENT, vcpu);
			kvm_vcpu_kick(vcpu);
		}
		break;

	case APIC_DM_STARTUP:
		result = 1;
		apic->sipi_vector = vector;
		/* make sure sipi_vector is visible for the receiver */
		smp_wmb();
		set_bit(KVM_APIC_SIPI, &apic->pending_events);
		kvm_make_request(KVM_REQ_EVENT, vcpu);
		kvm_vcpu_kick(vcpu);
		break;

	case APIC_DM_EXTINT:
		/*
		 * Should only be called by kvm_apic_local_deliver() with LVT0,
		 * before NMI watchdog was enabled. Already handled by
		 * kvm_apic_accept_pic_intr().
		 */
		break;

	default:
		printk(KERN_ERR "TODO: unsupported delivery mode %x\n",
		       delivery_mode);
		break;
	}
	return result;
}

/*
 * This routine identifies the destination vcpus mask meant to receive the
 * IOAPIC interrupts. It either uses kvm_apic_map_get_dest_lapic() to find
 * out the destination vcpus array and set the bitmap or it traverses to
 * each available vcpu to identify the same.
 */
void kvm_bitmap_or_dest_vcpus(struct kvm *kvm, struct kvm_lapic_irq *irq,
			      unsigned long *vcpu_bitmap)
{
	struct kvm_lapic **dest_vcpu = NULL;
	struct kvm_lapic *src = NULL;
	struct kvm_apic_map *map;
	struct kvm_vcpu *vcpu;
	unsigned long bitmap, i;
	int vcpu_idx;
	bool ret;

	rcu_read_lock();
	map = rcu_dereference(kvm->arch.apic_map);

	ret = kvm_apic_map_get_dest_lapic(kvm, &src, irq, map, &dest_vcpu,
					  &bitmap);
	if (ret) {
		for_each_set_bit(i, &bitmap, 16) {
			if (!dest_vcpu[i])
				continue;
			vcpu_idx = dest_vcpu[i]->vcpu->vcpu_idx;
			__set_bit(vcpu_idx, vcpu_bitmap);
		}
	} else {
		kvm_for_each_vcpu(i, vcpu, kvm) {
			if (!kvm_apic_present(vcpu))
				continue;
			if (!kvm_apic_match_dest(vcpu, NULL,
						 irq->shorthand,
						 irq->dest_id,
						 irq->dest_mode))
				continue;
			__set_bit(i, vcpu_bitmap);
		}
	}
	rcu_read_unlock();
}

static bool kvm_ioapic_handles_vector(struct kvm_lapic *apic, int vector)
{
	return test_bit(vector, apic->vcpu->arch.ioapic_handled_vectors);
}

static void kvm_ioapic_send_eoi(struct kvm_lapic *apic, int vector)
{
	int __maybe_unused trigger_mode;

	/* Eoi the ioapic only if the ioapic doesn't own the vector. */
	if (!kvm_ioapic_handles_vector(apic, vector))
		return;

	/*
	 * If the intercepted EOI is for an IRQ that was pending from previous
	 * routing, then re-scan the I/O APIC routes as EOIs for the IRQ likely
	 * no longer need to be intercepted.
	 */
	if (apic->vcpu->arch.highest_stale_pending_ioapic_eoi == vector)
		kvm_make_request(KVM_REQ_SCAN_IOAPIC, apic->vcpu);

	/* Request a KVM exit to inform the userspace IOAPIC. */
	if (irqchip_split(apic->vcpu->kvm)) {
		apic->vcpu->arch.pending_ioapic_eoi = vector;
		kvm_make_request(KVM_REQ_IOAPIC_EOI_EXIT, apic->vcpu);
		return;
	}

#ifdef CONFIG_KVM_IOAPIC
	if (apic_test_vector(vector, apic->regs + APIC_TMR))
		trigger_mode = IOAPIC_LEVEL_TRIG;
	else
		trigger_mode = IOAPIC_EDGE_TRIG;

	kvm_ioapic_update_eoi(apic->vcpu, vector, trigger_mode);
#endif
}

static int apic_set_eoi(struct kvm_lapic *apic)
{
	int vector = apic_find_highest_isr(apic);

	trace_kvm_eoi(apic, vector);

	/*
	 * Not every write EOI will has corresponding ISR,
	 * one example is when Kernel check timer on setup_IO_APIC
	 */
	if (vector == -1)
		return vector;

	apic_clear_isr(vector, apic);
	apic_update_ppr(apic);

	if (kvm_hv_synic_has_vector(apic->vcpu, vector))
		kvm_hv_synic_send_eoi(apic->vcpu, vector);

	kvm_ioapic_send_eoi(apic, vector);
	kvm_make_request(KVM_REQ_EVENT, apic->vcpu);
	return vector;
}

/*
 * this interface assumes a trap-like exit, which has already finished
 * desired side effect including vISR and vPPR update.
 */
void kvm_apic_set_eoi_accelerated(struct kvm_vcpu *vcpu, int vector)
{
	struct kvm_lapic *apic = vcpu->arch.apic;

	trace_kvm_eoi(apic, vector);

	kvm_ioapic_send_eoi(apic, vector);
	kvm_make_request(KVM_REQ_EVENT, apic->vcpu);
}
EXPORT_SYMBOL_FOR_KVM_INTERNAL(kvm_apic_set_eoi_accelerated);

static void kvm_icr_to_lapic_irq(struct kvm_lapic *apic, u32 icr_low,
				 u32 icr_high, struct kvm_lapic_irq *irq)
{
	/* KVM has no delay and should always clear the BUSY/PENDING flag. */
	WARN_ON_ONCE(icr_low & APIC_ICR_BUSY);

	irq->vector = icr_low & APIC_VECTOR_MASK;
	irq->delivery_mode = icr_low & APIC_MODE_MASK;
	irq->dest_mode = icr_low & APIC_DEST_MASK;
	irq->level = (icr_low & APIC_INT_ASSERT) != 0;
	irq->trig_mode = icr_low & APIC_INT_LEVELTRIG;
	irq->shorthand = icr_low & APIC_SHORT_MASK;
	irq->msi_redir_hint = false;
	if (apic_x2apic_mode(apic))
		irq->dest_id = icr_high;
	else
		irq->dest_id = GET_XAPIC_DEST_FIELD(icr_high);
}

void kvm_apic_send_ipi(struct kvm_lapic *apic, u32 icr_low, u32 icr_high)
{
	struct kvm_lapic_irq irq;

	kvm_icr_to_lapic_irq(apic, icr_low, icr_high, &irq);

	trace_kvm_apic_ipi(icr_low, irq.dest_id);

	kvm_irq_delivery_to_apic(apic->vcpu->kvm, apic, &irq, NULL);
}
EXPORT_SYMBOL_FOR_KVM_INTERNAL(kvm_apic_send_ipi);

static u32 apic_get_tmcct(struct kvm_lapic *apic)
{
	ktime_t remaining, now;
	s64 ns;

	ASSERT(apic != NULL);

	/* if initial count is 0, current count should also be 0 */
	if (kvm_lapic_get_reg(apic, APIC_TMICT) == 0 ||
		apic->lapic_timer.period == 0)
		return 0;

	now = ktime_get();
	remaining = ktime_sub(apic->lapic_timer.target_expiration, now);
	if (ktime_to_ns(remaining) < 0)
		remaining = 0;

	ns = mod_64(ktime_to_ns(remaining), apic->lapic_timer.period);
	return div64_u64(ns, (apic->vcpu->kvm->arch.apic_bus_cycle_ns *
			      apic->divide_count));
}

static void __report_tpr_access(struct kvm_lapic *apic, bool write)
{
	struct kvm_vcpu *vcpu = apic->vcpu;
	struct kvm_run *run = vcpu->run;

	kvm_make_request(KVM_REQ_REPORT_TPR_ACCESS, vcpu);
	run->tpr_access.rip = kvm_rip_read(vcpu);
	run->tpr_access.is_write = write;
}

static inline void report_tpr_access(struct kvm_lapic *apic, bool write)
{
	if (apic->vcpu->arch.tpr_access_reporting)
		__report_tpr_access(apic, write);
}

static u32 __apic_read(struct kvm_lapic *apic, unsigned int offset)
{
	u32 val = 0;

	if (offset >= LAPIC_MMIO_LENGTH)
		return 0;

	switch (offset) {
	case APIC_ARBPRI:
		break;

	case APIC_TMCCT:	/* Timer CCR */
		if (apic_lvtt_tscdeadline(apic))
			return 0;

		val = apic_get_tmcct(apic);
		break;
	case APIC_PROCPRI:
		apic_update_ppr(apic);
		val = kvm_lapic_get_reg(apic, offset);
		break;
	case APIC_TASKPRI:
		report_tpr_access(apic, false);
		fallthrough;
	default:
		val = kvm_lapic_get_reg(apic, offset);
		break;
	}

	return val;
}

static inline struct kvm_lapic *to_lapic(struct kvm_io_device *dev)
{
	return container_of(dev, struct kvm_lapic, dev);
}

#define APIC_REG_MASK(reg)	(1ull << ((reg) >> 4))
#define APIC_REGS_MASK(first, count) \
	(APIC_REG_MASK(first) * ((1ull << (count)) - 1))

u64 kvm_lapic_readable_reg_mask(struct kvm_lapic *apic)
{
	/* Leave bits '0' for reserved and write-only registers. */
	u64 valid_reg_mask =
		APIC_REG_MASK(APIC_ID) |
		APIC_REG_MASK(APIC_LVR) |
		APIC_REG_MASK(APIC_TASKPRI) |
		APIC_REG_MASK(APIC_PROCPRI) |
		APIC_REG_MASK(APIC_LDR) |
		APIC_REG_MASK(APIC_SPIV) |
		APIC_REGS_MASK(APIC_ISR, APIC_ISR_NR) |
		APIC_REGS_MASK(APIC_TMR, APIC_ISR_NR) |
		APIC_REGS_MASK(APIC_IRR, APIC_ISR_NR) |
		APIC_REG_MASK(APIC_ESR) |
		APIC_REG_MASK(APIC_ICR) |
		APIC_REG_MASK(APIC_LVTT) |
		APIC_REG_MASK(APIC_LVTTHMR) |
		APIC_REG_MASK(APIC_LVTPC) |
		APIC_REG_MASK(APIC_LVT0) |
		APIC_REG_MASK(APIC_LVT1) |
		APIC_REG_MASK(APIC_LVTERR) |
		APIC_REG_MASK(APIC_TMICT) |
		APIC_REG_MASK(APIC_TMCCT) |
		APIC_REG_MASK(APIC_TDCR);

	if (kvm_lapic_lvt_supported(apic, LVT_CMCI))
		valid_reg_mask |= APIC_REG_MASK(APIC_LVTCMCI);

	/* ARBPRI, DFR, and ICR2 are not valid in x2APIC mode. */
	if (!apic_x2apic_mode(apic))
		valid_reg_mask |= APIC_REG_MASK(APIC_ARBPRI) |
				  APIC_REG_MASK(APIC_DFR) |
				  APIC_REG_MASK(APIC_ICR2);

	return valid_reg_mask;
}
EXPORT_SYMBOL_FOR_KVM_INTERNAL(kvm_lapic_readable_reg_mask);

static int kvm_lapic_reg_read(struct kvm_lapic *apic, u32 offset, int len,
			      void *data)
{
	unsigned char alignment = offset & 0xf;
	u32 result;

	/*
	 * WARN if KVM reads ICR in x2APIC mode, as it's an 8-byte register in
	 * x2APIC and needs to be manually handled by the caller.
	 */
	WARN_ON_ONCE(apic_x2apic_mode(apic) && offset == APIC_ICR);

	if (alignment + len > 4)
		return 1;

	if (offset > 0x3f0 ||
	    !(kvm_lapic_readable_reg_mask(apic) & APIC_REG_MASK(offset)))
		return 1;

	result = __apic_read(apic, offset & ~0xf);

	trace_kvm_apic_read(offset, result);

	switch (len) {
	case 1:
	case 2:
	case 4:
		memcpy(data, (char *)&result + alignment, len);
		break;
	default:
		printk(KERN_ERR "Local APIC read with len = %x, "
		       "should be 1,2, or 4 instead\n", len);
		break;
	}
	return 0;
}

static int apic_mmio_in_range(struct kvm_lapic *apic, gpa_t addr)
{
	return addr >= apic->base_address &&
		addr < apic->base_address + LAPIC_MMIO_LENGTH;
}

static int apic_mmio_read(struct kvm_vcpu *vcpu, struct kvm_io_device *this,
			   gpa_t address, int len, void *data)
{
	struct kvm_lapic *apic = to_lapic(this);
	u32 offset = address - apic->base_address;

	if (!apic_mmio_in_range(apic, address))
		return -EOPNOTSUPP;

	if (!kvm_apic_hw_enabled(apic) || apic_x2apic_mode(apic)) {
		if (!kvm_check_has_quirk(vcpu->kvm,
					 KVM_X86_QUIRK_LAPIC_MMIO_HOLE))
			return -EOPNOTSUPP;

		memset(data, 0xff, len);
		return 0;
	}

	kvm_lapic_reg_read(apic, offset, len, data);

	return 0;
}

static void update_divide_count(struct kvm_lapic *apic)
{
	u32 tmp1, tmp2, tdcr;

	tdcr = kvm_lapic_get_reg(apic, APIC_TDCR);
	tmp1 = tdcr & 0xf;
	tmp2 = ((tmp1 & 0x3) | ((tmp1 & 0x8) >> 1)) + 1;
	apic->divide_count = 0x1 << (tmp2 & 0x7);
}

static void limit_periodic_timer_frequency(struct kvm_lapic *apic)
{
	/*
	 * Do not allow the guest to program periodic timers with small
	 * interval, since the hrtimers are not throttled by the host
	 * scheduler.
	 */
	if (apic_lvtt_period(apic) && apic->lapic_timer.period) {
		s64 min_period = min_timer_period_us * 1000LL;

		if (apic->lapic_timer.period < min_period) {
			pr_info_once(
			    "vcpu %i: requested %lld ns "
			    "lapic timer period limited to %lld ns\n",
			    apic->vcpu->vcpu_id,
			    apic->lapic_timer.period, min_period);
			apic->lapic_timer.period = min_period;
		}
	}
}

static void cancel_hv_timer(struct kvm_lapic *apic);

static void cancel_apic_timer(struct kvm_lapic *apic)
{
	hrtimer_cancel(&apic->lapic_timer.timer);
	preempt_disable();
	if (apic->lapic_timer.hv_timer_in_use)
		cancel_hv_timer(apic);
	preempt_enable();
	atomic_set(&apic->lapic_timer.pending, 0);
}

static void apic_update_lvtt(struct kvm_lapic *apic)
{
	u32 timer_mode = kvm_lapic_get_reg(apic, APIC_LVTT) &
			apic->lapic_timer.timer_mode_mask;

	if (apic->lapic_timer.timer_mode != timer_mode) {
		if (apic_lvtt_tscdeadline(apic) != (timer_mode ==
				APIC_LVT_TIMER_TSCDEADLINE)) {
			cancel_apic_timer(apic);
			kvm_lapic_set_reg(apic, APIC_TMICT, 0);
			apic->lapic_timer.period = 0;
			apic->lapic_timer.tscdeadline = 0;
		}
		apic->lapic_timer.timer_mode = timer_mode;
		limit_periodic_timer_frequency(apic);
	}
}

/*
 * On APICv, this test will cause a busy wait
 * during a higher-priority task.
 */

static bool lapic_timer_int_injected(struct kvm_vcpu *vcpu)
{
	struct kvm_lapic *apic = vcpu->arch.apic;
	u32 reg;

	/*
	 * Assume a timer IRQ was "injected" if the APIC is protected.  KVM's
	 * copy of the vIRR is bogus, it's the responsibility of the caller to
	 * precisely check whether or not a timer IRQ is pending.
	 */
	if (apic->guest_apic_protected)
		return true;

	reg = kvm_lapic_get_reg(apic, APIC_LVTT);
	if (kvm_apic_hw_enabled(apic)) {
		int vec = reg & APIC_VECTOR_MASK;
		void *bitmap = apic->regs + APIC_ISR;

		if (apic->apicv_active)
			bitmap = apic->regs + APIC_IRR;

		if (apic_test_vector(vec, bitmap))
			return true;
	}
	return false;
}

static inline void __wait_lapic_expire(struct kvm_vcpu *vcpu, u64 guest_cycles)
{
	u64 timer_advance_ns = vcpu->arch.apic->lapic_timer.timer_advance_ns;

	/*
	 * If the guest TSC is running at a different ratio than the host, then
	 * convert the delay to nanoseconds to achieve an accurate delay.  Note
	 * that __delay() uses delay_tsc whenever the hardware has TSC, thus
	 * always for VMX enabled hardware.
	 */
	if (vcpu->arch.tsc_scaling_ratio == kvm_caps.default_tsc_scaling_ratio) {
		__delay(min(guest_cycles,
			nsec_to_cycles(vcpu, timer_advance_ns)));
	} else {
		u64 delay_ns = guest_cycles * 1000000ULL;
		do_div(delay_ns, vcpu->arch.virtual_tsc_khz);
		ndelay(min_t(u32, delay_ns, timer_advance_ns));
	}
}

static inline void adjust_lapic_timer_advance(struct kvm_vcpu *vcpu,
					      s64 advance_expire_delta)
{
	struct kvm_lapic *apic = vcpu->arch.apic;
	u32 timer_advance_ns = apic->lapic_timer.timer_advance_ns;
	u64 ns;

	/* Do not adjust for tiny fluctuations or large random spikes. */
	if (abs(advance_expire_delta) > LAPIC_TIMER_ADVANCE_ADJUST_MAX ||
	    abs(advance_expire_delta) < LAPIC_TIMER_ADVANCE_ADJUST_MIN)
		return;

	/* too early */
	if (advance_expire_delta < 0) {
		ns = -advance_expire_delta * 1000000ULL;
		do_div(ns, vcpu->arch.virtual_tsc_khz);
		timer_advance_ns -= ns/LAPIC_TIMER_ADVANCE_ADJUST_STEP;
	} else {
	/* too late */
		ns = advance_expire_delta * 1000000ULL;
		do_div(ns, vcpu->arch.virtual_tsc_khz);
		timer_advance_ns += ns/LAPIC_TIMER_ADVANCE_ADJUST_STEP;
	}

	if (unlikely(timer_advance_ns > LAPIC_TIMER_ADVANCE_NS_MAX))
		timer_advance_ns = LAPIC_TIMER_ADVANCE_NS_INIT;
	apic->lapic_timer.timer_advance_ns = timer_advance_ns;
}

static void __kvm_wait_lapic_expire(struct kvm_vcpu *vcpu)
{
	struct kvm_lapic *apic = vcpu->arch.apic;
	u64 guest_tsc, tsc_deadline;

	tsc_deadline = apic->lapic_timer.expired_tscdeadline;
	apic->lapic_timer.expired_tscdeadline = 0;
	guest_tsc = kvm_read_l1_tsc(vcpu, rdtsc());
	trace_kvm_wait_lapic_expire(vcpu->vcpu_id, guest_tsc - tsc_deadline);

	adjust_lapic_timer_advance(vcpu, guest_tsc - tsc_deadline);

	/*
	 * If the timer fired early, reread the TSC to account for the overhead
	 * of the above adjustment to avoid waiting longer than is necessary.
	 */
	if (guest_tsc < tsc_deadline)
		guest_tsc = kvm_read_l1_tsc(vcpu, rdtsc());

	if (guest_tsc < tsc_deadline)
		__wait_lapic_expire(vcpu, tsc_deadline - guest_tsc);
}

void kvm_wait_lapic_expire(struct kvm_vcpu *vcpu)
{
	if (lapic_in_kernel(vcpu) &&
	    vcpu->arch.apic->lapic_timer.expired_tscdeadline &&
	    vcpu->arch.apic->lapic_timer.timer_advance_ns &&
	    lapic_timer_int_injected(vcpu))
		__kvm_wait_lapic_expire(vcpu);
}
EXPORT_SYMBOL_FOR_KVM_INTERNAL(kvm_wait_lapic_expire);

static void kvm_apic_inject_pending_timer_irqs(struct kvm_lapic *apic)
{
	struct kvm_timer *ktimer = &apic->lapic_timer;

	kvm_apic_local_deliver(apic, APIC_LVTT);
	if (apic_lvtt_tscdeadline(apic)) {
		ktimer->tscdeadline = 0;
	} else if (apic_lvtt_oneshot(apic)) {
		ktimer->tscdeadline = 0;
		ktimer->target_expiration = 0;
	}
}

static void apic_timer_expired(struct kvm_lapic *apic, bool from_timer_fn)
{
	struct kvm_vcpu *vcpu = apic->vcpu;
	struct kvm_timer *ktimer = &apic->lapic_timer;

	if (atomic_read(&apic->lapic_timer.pending))
		return;

	if (apic_lvtt_tscdeadline(apic) || ktimer->hv_timer_in_use)
		ktimer->expired_tscdeadline = ktimer->tscdeadline;

	if (!from_timer_fn && apic->apicv_active) {
		WARN_ON(kvm_get_running_vcpu() != vcpu);
		kvm_apic_inject_pending_timer_irqs(apic);
		return;
	}

	if (kvm_use_posted_timer_interrupt(apic->vcpu)) {
		/*
		 * Ensure the guest's timer has truly expired before posting an
		 * interrupt.  Open code the relevant checks to avoid querying
		 * lapic_timer_int_injected(), which will be false since the
		 * interrupt isn't yet injected.  Waiting until after injecting
		 * is not an option since that won't help a posted interrupt.
		 */
		if (vcpu->arch.apic->lapic_timer.expired_tscdeadline &&
		    vcpu->arch.apic->lapic_timer.timer_advance_ns)
			__kvm_wait_lapic_expire(vcpu);
		kvm_apic_inject_pending_timer_irqs(apic);
		return;
	}

	atomic_inc(&apic->lapic_timer.pending);
	kvm_make_request(KVM_REQ_UNBLOCK, vcpu);
	if (from_timer_fn)
		kvm_vcpu_kick(vcpu);
}

static void start_sw_tscdeadline(struct kvm_lapic *apic)
{
	struct kvm_timer *ktimer = &apic->lapic_timer;
	u64 guest_tsc, tscdeadline = ktimer->tscdeadline;
	u64 ns = 0;
	ktime_t expire;
	struct kvm_vcpu *vcpu = apic->vcpu;
	u32 this_tsc_khz = vcpu->arch.virtual_tsc_khz;
	unsigned long flags;
	ktime_t now;

	if (unlikely(!tscdeadline || !this_tsc_khz))
		return;

	local_irq_save(flags);

	now = ktime_get();
	guest_tsc = kvm_read_l1_tsc(vcpu, rdtsc());

	ns = (tscdeadline - guest_tsc) * 1000000ULL;
	do_div(ns, this_tsc_khz);

	if (likely(tscdeadline > guest_tsc) &&
	    likely(ns > apic->lapic_timer.timer_advance_ns)) {
		expire = ktime_add_ns(now, ns);
		expire = ktime_sub_ns(expire, ktimer->timer_advance_ns);
		hrtimer_start(&ktimer->timer, expire, HRTIMER_MODE_ABS_HARD);
	} else
		apic_timer_expired(apic, false);

	local_irq_restore(flags);
}

static inline u64 tmict_to_ns(struct kvm_lapic *apic, u32 tmict)
{
	return (u64)tmict * apic->vcpu->kvm->arch.apic_bus_cycle_ns *
		(u64)apic->divide_count;
}

static void update_target_expiration(struct kvm_lapic *apic, uint32_t old_divisor)
{
	ktime_t now, remaining;
	u64 ns_remaining_old, ns_remaining_new;

	apic->lapic_timer.period =
			tmict_to_ns(apic, kvm_lapic_get_reg(apic, APIC_TMICT));
	limit_periodic_timer_frequency(apic);

	now = ktime_get();
	remaining = ktime_sub(apic->lapic_timer.target_expiration, now);
	if (ktime_to_ns(remaining) < 0)
		remaining = 0;

	ns_remaining_old = ktime_to_ns(remaining);
	ns_remaining_new = mul_u64_u32_div(ns_remaining_old,
	                                   apic->divide_count, old_divisor);

	apic->lapic_timer.tscdeadline +=
		nsec_to_cycles(apic->vcpu, ns_remaining_new) -
		nsec_to_cycles(apic->vcpu, ns_remaining_old);
	apic->lapic_timer.target_expiration = ktime_add_ns(now, ns_remaining_new);
}

static bool set_target_expiration(struct kvm_lapic *apic, u32 count_reg)
{
	ktime_t now;
	u64 tscl = rdtsc();
	s64 deadline;

	now = ktime_get();
	apic->lapic_timer.period =
			tmict_to_ns(apic, kvm_lapic_get_reg(apic, APIC_TMICT));

	if (!apic->lapic_timer.period) {
		apic->lapic_timer.tscdeadline = 0;
		return false;
	}

	limit_periodic_timer_frequency(apic);
	deadline = apic->lapic_timer.period;

	if (apic_lvtt_period(apic) || apic_lvtt_oneshot(apic)) {
		if (unlikely(count_reg != APIC_TMICT)) {
			deadline = tmict_to_ns(apic,
				     kvm_lapic_get_reg(apic, count_reg));
			if (unlikely(deadline <= 0)) {
				if (apic_lvtt_period(apic))
					deadline = apic->lapic_timer.period;
				else
					deadline = 0;
			}
			else if (unlikely(deadline > apic->lapic_timer.period)) {
				pr_info_ratelimited(
				    "vcpu %i: requested lapic timer restore with "
				    "starting count register %#x=%u (%lld ns) > initial count (%lld ns). "
				    "Using initial count to start timer.\n",
				    apic->vcpu->vcpu_id,
				    count_reg,
				    kvm_lapic_get_reg(apic, count_reg),
				    deadline, apic->lapic_timer.period);
				kvm_lapic_set_reg(apic, count_reg, 0);
				deadline = apic->lapic_timer.period;
			}
		}
	}

	apic->lapic_timer.tscdeadline = kvm_read_l1_tsc(apic->vcpu, tscl) +
		nsec_to_cycles(apic->vcpu, deadline);
	apic->lapic_timer.target_expiration = ktime_add_ns(now, deadline);

	return true;
}

static void advance_periodic_target_expiration(struct kvm_lapic *apic)
{
	ktime_t now = ktime_get();
	u64 tscl = rdtsc();
	ktime_t delta;

	/*
	 * Synchronize both deadlines to the same time source or
	 * differences in the periods (caused by differences in the
	 * underlying clocks or numerical approximation errors) will
	 * cause the two to drift apart over time as the errors
	 * accumulate.
	 */
	apic->lapic_timer.target_expiration =
		ktime_add_ns(apic->lapic_timer.target_expiration,
				apic->lapic_timer.period);
	delta = ktime_sub(apic->lapic_timer.target_expiration, now);
	apic->lapic_timer.tscdeadline = kvm_read_l1_tsc(apic->vcpu, tscl) +
		nsec_to_cycles(apic->vcpu, delta);
}

static void start_sw_period(struct kvm_lapic *apic)
{
	if (!apic->lapic_timer.period)
		return;

	if (ktime_after(ktime_get(),
			apic->lapic_timer.target_expiration)) {
		apic_timer_expired(apic, false);

		if (apic_lvtt_oneshot(apic))
			return;

		advance_periodic_target_expiration(apic);
	}

	hrtimer_start(&apic->lapic_timer.timer,
		apic->lapic_timer.target_expiration,
		HRTIMER_MODE_ABS_HARD);
}

bool kvm_lapic_hv_timer_in_use(struct kvm_vcpu *vcpu)
{
	if (!lapic_in_kernel(vcpu))
		return false;

	return vcpu->arch.apic->lapic_timer.hv_timer_in_use;
}

static void cancel_hv_timer(struct kvm_lapic *apic)
{
	WARN_ON(preemptible());
	WARN_ON(!apic->lapic_timer.hv_timer_in_use);
	kvm_x86_call(cancel_hv_timer)(apic->vcpu);
	apic->lapic_timer.hv_timer_in_use = false;
}

static bool start_hv_timer(struct kvm_lapic *apic)
{
	struct kvm_timer *ktimer = &apic->lapic_timer;
	struct kvm_vcpu *vcpu = apic->vcpu;
	bool expired;

	WARN_ON(preemptible());
	if (!kvm_can_use_hv_timer(vcpu))
		return false;

	if (!ktimer->tscdeadline)
		return false;

	if (kvm_x86_call(set_hv_timer)(vcpu, ktimer->tscdeadline, &expired))
		return false;

	ktimer->hv_timer_in_use = true;
	hrtimer_cancel(&ktimer->timer);

	/*
	 * To simplify handling the periodic timer, leave the hv timer running
	 * even if the deadline timer has expired, i.e. rely on the resulting
	 * VM-Exit to recompute the periodic timer's target expiration.
	 */
	if (!apic_lvtt_period(apic)) {
		/*
		 * Cancel the hv timer if the sw timer fired while the hv timer
		 * was being programmed, or if the hv timer itself expired.
		 */
		if (atomic_read(&ktimer->pending)) {
			cancel_hv_timer(apic);
		} else if (expired) {
			apic_timer_expired(apic, false);
			cancel_hv_timer(apic);
		}
	}

	trace_kvm_hv_timer_state(vcpu->vcpu_id, ktimer->hv_timer_in_use);

	return true;
}

static void start_sw_timer(struct kvm_lapic *apic)
{
	struct kvm_timer *ktimer = &apic->lapic_timer;

	WARN_ON(preemptible());
	if (apic->lapic_timer.hv_timer_in_use)
		cancel_hv_timer(apic);
	if (!apic_lvtt_period(apic) && atomic_read(&ktimer->pending))
		return;

	if (apic_lvtt_period(apic) || apic_lvtt_oneshot(apic))
		start_sw_period(apic);
	else if (apic_lvtt_tscdeadline(apic))
		start_sw_tscdeadline(apic);
	trace_kvm_hv_timer_state(apic->vcpu->vcpu_id, false);
}

static void restart_apic_timer(struct kvm_lapic *apic)
{
	preempt_disable();

	if (!apic_lvtt_period(apic) && atomic_read(&apic->lapic_timer.pending))
		goto out;

	if (!start_hv_timer(apic))
		start_sw_timer(apic);
out:
	preempt_enable();
}

void kvm_lapic_expired_hv_timer(struct kvm_vcpu *vcpu)
{
	struct kvm_lapic *apic = vcpu->arch.apic;

	preempt_disable();
	/* If the preempt notifier has already run, it also called apic_timer_expired */
	if (!apic->lapic_timer.hv_timer_in_use)
		goto out;
	WARN_ON(kvm_vcpu_is_blocking(vcpu));
	apic_timer_expired(apic, false);
	cancel_hv_timer(apic);

	if (apic_lvtt_period(apic) && apic->lapic_timer.period) {
		advance_periodic_target_expiration(apic);
		restart_apic_timer(apic);
	}
out:
	preempt_enable();
}
EXPORT_SYMBOL_FOR_KVM_INTERNAL(kvm_lapic_expired_hv_timer);

void kvm_lapic_switch_to_hv_timer(struct kvm_vcpu *vcpu)
{
	restart_apic_timer(vcpu->arch.apic);
}

void kvm_lapic_switch_to_sw_timer(struct kvm_vcpu *vcpu)
{
	struct kvm_lapic *apic = vcpu->arch.apic;

	preempt_disable();
	/* Possibly the TSC deadline timer is not enabled yet */
	if (apic->lapic_timer.hv_timer_in_use)
		start_sw_timer(apic);
	preempt_enable();
}

void kvm_lapic_restart_hv_timer(struct kvm_vcpu *vcpu)
{
	struct kvm_lapic *apic = vcpu->arch.apic;

	WARN_ON(!apic->lapic_timer.hv_timer_in_use);
	restart_apic_timer(apic);
}

static void __start_apic_timer(struct kvm_lapic *apic, u32 count_reg)
{
	atomic_set(&apic->lapic_timer.pending, 0);

	if ((apic_lvtt_period(apic) || apic_lvtt_oneshot(apic))
	    && !set_target_expiration(apic, count_reg))
		return;

	restart_apic_timer(apic);
}

static void start_apic_timer(struct kvm_lapic *apic)
{
	__start_apic_timer(apic, APIC_TMICT);
}

static void apic_manage_nmi_watchdog(struct kvm_lapic *apic, u32 lvt0_val)
{
	bool lvt0_in_nmi_mode = apic_lvt_nmi_mode(lvt0_val);

	if (apic->lvt0_in_nmi_mode != lvt0_in_nmi_mode) {
		apic->lvt0_in_nmi_mode = lvt0_in_nmi_mode;
		if (lvt0_in_nmi_mode) {
			atomic_inc(&apic->vcpu->kvm->arch.vapics_in_nmi_mode);
		} else
			atomic_dec(&apic->vcpu->kvm->arch.vapics_in_nmi_mode);
	}
}

static int get_lvt_index(u32 reg)
{
	if (reg == APIC_LVTCMCI)
		return LVT_CMCI;
	if (reg < APIC_LVTT || reg > APIC_LVTERR)
		return -1;
	return array_index_nospec(
			(reg - APIC_LVTT) >> 4, KVM_APIC_MAX_NR_LVT_ENTRIES);
}

static int kvm_lapic_reg_write(struct kvm_lapic *apic, u32 reg, u32 val)
{
	int ret = 0;

	trace_kvm_apic_write(reg, val);

	switch (reg) {
	case APIC_ID:		/* Local APIC ID */
		if (!apic_x2apic_mode(apic)) {
			kvm_apic_set_xapic_id(apic, val >> 24);
		} else {
			ret = 1;
		}
		break;

	case APIC_TASKPRI:
		report_tpr_access(apic, true);
		apic_set_tpr(apic, val & 0xff);
		break;

	case APIC_EOI:
		apic_set_eoi(apic);
		break;

	case APIC_LDR:
		if (!apic_x2apic_mode(apic))
			kvm_apic_set_ldr(apic, val & APIC_LDR_MASK);
		else
			ret = 1;
		break;

	case APIC_DFR:
		if (!apic_x2apic_mode(apic))
			kvm_apic_set_dfr(apic, val | 0x0FFFFFFF);
		else
			ret = 1;
		break;

	case APIC_SPIV: {
		u32 mask = 0x3ff;
		if (kvm_lapic_get_reg(apic, APIC_LVR) & APIC_LVR_DIRECTED_EOI)
			mask |= APIC_SPIV_DIRECTED_EOI;
		apic_set_spiv(apic, val & mask);
		if (!(val & APIC_SPIV_APIC_ENABLED)) {
			int i;

			for (i = 0; i < apic->nr_lvt_entries; i++) {
				kvm_lapic_set_reg(apic, APIC_LVTx(i),
					kvm_lapic_get_reg(apic, APIC_LVTx(i)) | APIC_LVT_MASKED);
			}
			apic_update_lvtt(apic);
			atomic_set(&apic->lapic_timer.pending, 0);

		}
		break;
	}
	case APIC_ICR:
		WARN_ON_ONCE(apic_x2apic_mode(apic));

		/* No delay here, so we always clear the pending bit */
		val &= ~APIC_ICR_BUSY;
		kvm_apic_send_ipi(apic, val, kvm_lapic_get_reg(apic, APIC_ICR2));
		kvm_lapic_set_reg(apic, APIC_ICR, val);
		break;
	case APIC_ICR2:
		if (apic_x2apic_mode(apic))
			ret = 1;
		else
			kvm_lapic_set_reg(apic, APIC_ICR2, val & 0xff000000);
		break;

	case APIC_LVT0:
		apic_manage_nmi_watchdog(apic, val);
		fallthrough;
	case APIC_LVTTHMR:
	case APIC_LVTPC:
	case APIC_LVT1:
	case APIC_LVTERR:
	case APIC_LVTCMCI: {
		u32 index = get_lvt_index(reg);
		if (!kvm_lapic_lvt_supported(apic, index)) {
			ret = 1;
			break;
		}
		if (!kvm_apic_sw_enabled(apic))
			val |= APIC_LVT_MASKED;
		val &= apic_lvt_mask[index];
		kvm_lapic_set_reg(apic, reg, val);
		break;
	}

	case APIC_LVTT:
		if (!kvm_apic_sw_enabled(apic))
			val |= APIC_LVT_MASKED;
		val &= (apic_lvt_mask[LVT_TIMER] | apic->lapic_timer.timer_mode_mask);
		kvm_lapic_set_reg(apic, APIC_LVTT, val);
		apic_update_lvtt(apic);
		break;

	case APIC_TMICT:
		if (apic_lvtt_tscdeadline(apic))
			break;

		cancel_apic_timer(apic);
		kvm_lapic_set_reg(apic, APIC_TMICT, val);
		start_apic_timer(apic);
		break;

	case APIC_TDCR: {
		uint32_t old_divisor = apic->divide_count;

		kvm_lapic_set_reg(apic, APIC_TDCR, val & 0xb);
		update_divide_count(apic);
		if (apic->divide_count != old_divisor &&
				apic->lapic_timer.period) {
			hrtimer_cancel(&apic->lapic_timer.timer);
			update_target_expiration(apic, old_divisor);
			restart_apic_timer(apic);
		}
		break;
	}
	case APIC_ESR:
		if (apic_x2apic_mode(apic) && val != 0)
			ret = 1;
		break;

	case APIC_SELF_IPI:
		/*
		 * Self-IPI exists only when x2APIC is enabled.  Bits 7:0 hold
		 * the vector, everything else is reserved.
		 */
		if (!apic_x2apic_mode(apic) || (val & ~APIC_VECTOR_MASK))
			ret = 1;
		else
			kvm_apic_send_ipi(apic, APIC_DEST_SELF | val, 0);
		break;
	default:
		ret = 1;
		break;
	}

	/*
	 * Recalculate APIC maps if necessary, e.g. if the software enable bit
	 * was toggled, the APIC ID changed, etc...   The maps are marked dirty
	 * on relevant changes, i.e. this is a nop for most writes.
	 */
	kvm_recalculate_apic_map(apic->vcpu->kvm);

	return ret;
}

static int apic_mmio_write(struct kvm_vcpu *vcpu, struct kvm_io_device *this,
			    gpa_t address, int len, const void *data)
{
	struct kvm_lapic *apic = to_lapic(this);
	unsigned int offset = address - apic->base_address;
	u32 val;

	if (!apic_mmio_in_range(apic, address))
		return -EOPNOTSUPP;

	if (!kvm_apic_hw_enabled(apic) || apic_x2apic_mode(apic)) {
		if (!kvm_check_has_quirk(vcpu->kvm,
					 KVM_X86_QUIRK_LAPIC_MMIO_HOLE))
			return -EOPNOTSUPP;

		return 0;
	}

	/*
	 * APIC register must be aligned on 128-bits boundary.
	 * 32/64/128 bits registers must be accessed thru 32 bits.
	 * Refer SDM 8.4.1
	 */
	if (len != 4 || (offset & 0xf))
		return 0;

	val = *(u32*)data;

	kvm_lapic_reg_write(apic, offset & 0xff0, val);

	return 0;
}

void kvm_lapic_set_eoi(struct kvm_vcpu *vcpu)
{
	kvm_lapic_reg_write(vcpu->arch.apic, APIC_EOI, 0);
}
EXPORT_SYMBOL_FOR_KVM_INTERNAL(kvm_lapic_set_eoi);

#define X2APIC_ICR_RESERVED_BITS (GENMASK_ULL(31, 20) | GENMASK_ULL(17, 16) | BIT(13))

static int __kvm_x2apic_icr_write(struct kvm_lapic *apic, u64 data, bool fast)
{
	if (data & X2APIC_ICR_RESERVED_BITS)
		return 1;

	/*
	 * The BUSY bit is reserved on both Intel and AMD in x2APIC mode, but
	 * only AMD requires it to be zero, Intel essentially just ignores the
	 * bit.  And if IPI virtualization (Intel) or x2AVIC (AMD) is enabled,
	 * the CPU performs the reserved bits checks, i.e. the underlying CPU
	 * behavior will "win".  Arbitrarily clear the BUSY bit, as there is no
	 * sane way to provide consistent behavior with respect to hardware.
	 */
	data &= ~APIC_ICR_BUSY;

	if (fast) {
		struct kvm_lapic_irq irq;
		int ignored;

		kvm_icr_to_lapic_irq(apic, (u32)data, (u32)(data >> 32), &irq);

		if (!kvm_irq_delivery_to_apic_fast(apic->vcpu->kvm, apic, &irq,
						   &ignored, NULL))
			return -EWOULDBLOCK;

		trace_kvm_apic_ipi((u32)data, irq.dest_id);
	} else {
		kvm_apic_send_ipi(apic, (u32)data, (u32)(data >> 32));
	}
	if (kvm_x86_ops.x2apic_icr_is_split) {
		kvm_lapic_set_reg(apic, APIC_ICR, data);
		kvm_lapic_set_reg(apic, APIC_ICR2, data >> 32);
	} else {
		kvm_lapic_set_reg64(apic, APIC_ICR, data);
	}
	trace_kvm_apic_write(APIC_ICR, data);
	return 0;
}

static int kvm_x2apic_icr_write(struct kvm_lapic *apic, u64 data)
{
	return __kvm_x2apic_icr_write(apic, data, false);
}

int kvm_x2apic_icr_write_fast(struct kvm_lapic *apic, u64 data)
{
	return __kvm_x2apic_icr_write(apic, data, true);
}

static u64 kvm_x2apic_icr_read(struct kvm_lapic *apic)
{
	if (kvm_x86_ops.x2apic_icr_is_split)
		return (u64)kvm_lapic_get_reg(apic, APIC_ICR) |
		       (u64)kvm_lapic_get_reg(apic, APIC_ICR2) << 32;

	return kvm_lapic_get_reg64(apic, APIC_ICR);
}

/* emulate APIC access in a trap manner */
void kvm_apic_write_nodecode(struct kvm_vcpu *vcpu, u32 offset)
{
	struct kvm_lapic *apic = vcpu->arch.apic;

	/*
	 * ICR is a single 64-bit register when x2APIC is enabled, all others
	 * registers hold 32-bit values.  For legacy xAPIC, ICR writes need to
	 * go down the common path to get the upper half from ICR2.
	 *
	 * Note, using the write helpers may incur an unnecessary write to the
	 * virtual APIC state, but KVM needs to conditionally modify the value
	 * in certain cases, e.g. to clear the ICR busy bit.  The cost of extra
	 * conditional branches is likely a wash relative to the cost of the
	 * maybe-unecessary write, and both are in the noise anyways.
	 */
	if (apic_x2apic_mode(apic) && offset == APIC_ICR)
		WARN_ON_ONCE(kvm_x2apic_icr_write(apic, kvm_x2apic_icr_read(apic)));
	else
		kvm_lapic_reg_write(apic, offset, kvm_lapic_get_reg(apic, offset));
}
EXPORT_SYMBOL_FOR_KVM_INTERNAL(kvm_apic_write_nodecode);

void kvm_free_lapic(struct kvm_vcpu *vcpu)
{
	struct kvm_lapic *apic = vcpu->arch.apic;

	if (!vcpu->arch.apic) {
		static_branch_dec(&kvm_has_noapic_vcpu);
		return;
	}

	hrtimer_cancel(&apic->lapic_timer.timer);

	if (!(vcpu->arch.apic_base & MSR_IA32_APICBASE_ENABLE))
		static_branch_slow_dec_deferred(&apic_hw_disabled);

	if (!apic->sw_enabled)
		static_branch_slow_dec_deferred(&apic_sw_disabled);

	if (apic->regs)
		free_page((unsigned long)apic->regs);

	kfree(apic);
}

/*
 *----------------------------------------------------------------------
 * LAPIC interface
 *----------------------------------------------------------------------
 */
u64 kvm_get_lapic_tscdeadline_msr(struct kvm_vcpu *vcpu)
{
	struct kvm_lapic *apic = vcpu->arch.apic;

	if (!kvm_apic_present(vcpu) || !apic_lvtt_tscdeadline(apic))
		return 0;

	return apic->lapic_timer.tscdeadline;
}

void kvm_set_lapic_tscdeadline_msr(struct kvm_vcpu *vcpu, u64 data)
{
	struct kvm_lapic *apic = vcpu->arch.apic;

	if (!kvm_apic_present(vcpu) || !apic_lvtt_tscdeadline(apic))
		return;

	hrtimer_cancel(&apic->lapic_timer.timer);
	apic->lapic_timer.tscdeadline = data;
	start_apic_timer(apic);
}

void kvm_lapic_set_tpr(struct kvm_vcpu *vcpu, unsigned long cr8)
{
	apic_set_tpr(vcpu->arch.apic, (cr8 & 0x0f) << 4);
}

u64 kvm_lapic_get_cr8(struct kvm_vcpu *vcpu)
{
	u64 tpr;

	tpr = (u64) kvm_lapic_get_reg(vcpu->arch.apic, APIC_TASKPRI);

	return (tpr & 0xf0) >> 4;
}

static void __kvm_apic_set_base(struct kvm_vcpu *vcpu, u64 value)
{
	u64 old_value = vcpu->arch.apic_base;
	struct kvm_lapic *apic = vcpu->arch.apic;

	vcpu->arch.apic_base = value;

	if ((old_value ^ value) & MSR_IA32_APICBASE_ENABLE)
		vcpu->arch.cpuid_dynamic_bits_dirty = true;

	if (!apic)
		return;

	/* update jump label if enable bit changes */
	if ((old_value ^ value) & MSR_IA32_APICBASE_ENABLE) {
		if (value & MSR_IA32_APICBASE_ENABLE) {
			kvm_apic_set_xapic_id(apic, vcpu->vcpu_id);
			static_branch_slow_dec_deferred(&apic_hw_disabled);
			/* Check if there are APF page ready requests pending */
			kvm_make_request(KVM_REQ_APF_READY, vcpu);
		} else {
			static_branch_inc(&apic_hw_disabled.key);
			atomic_set_release(&apic->vcpu->kvm->arch.apic_map_dirty, DIRTY);
		}
	}

	if ((old_value ^ value) & X2APIC_ENABLE) {
		if (value & X2APIC_ENABLE)
			kvm_apic_set_x2apic_id(apic, vcpu->vcpu_id);
		else if (value & MSR_IA32_APICBASE_ENABLE)
			kvm_apic_set_xapic_id(apic, vcpu->vcpu_id);
	}

	if ((old_value ^ value) & (MSR_IA32_APICBASE_ENABLE | X2APIC_ENABLE)) {
		kvm_make_request(KVM_REQ_APICV_UPDATE, vcpu);
		kvm_x86_call(set_virtual_apic_mode)(vcpu);
	}

	apic->base_address = apic->vcpu->arch.apic_base &
			     MSR_IA32_APICBASE_BASE;

	if ((value & MSR_IA32_APICBASE_ENABLE) &&
	     apic->base_address != APIC_DEFAULT_PHYS_BASE) {
		kvm_set_apicv_inhibit(apic->vcpu->kvm,
				      APICV_INHIBIT_REASON_APIC_BASE_MODIFIED);
	}
}

int kvm_apic_set_base(struct kvm_vcpu *vcpu, u64 value, bool host_initiated)
{
	enum lapic_mode old_mode = kvm_get_apic_mode(vcpu);
	enum lapic_mode new_mode = kvm_apic_mode(value);

	if (vcpu->arch.apic_base == value)
		return 0;

	u64 reserved_bits = kvm_vcpu_reserved_gpa_bits_raw(vcpu) | 0x2ff |
		(guest_cpu_cap_has(vcpu, X86_FEATURE_X2APIC) ? 0 : X2APIC_ENABLE);

	if ((value & reserved_bits) != 0 || new_mode == LAPIC_MODE_INVALID)
		return 1;
	if (!host_initiated) {
		if (old_mode == LAPIC_MODE_X2APIC && new_mode == LAPIC_MODE_XAPIC)
			return 1;
		if (old_mode == LAPIC_MODE_DISABLED && new_mode == LAPIC_MODE_X2APIC)
			return 1;
	}

	__kvm_apic_set_base(vcpu, value);
	kvm_recalculate_apic_map(vcpu->kvm);
	return 0;
}
EXPORT_SYMBOL_FOR_KVM_INTERNAL(kvm_apic_set_base);

void kvm_apic_update_apicv(struct kvm_vcpu *vcpu)
{
	struct kvm_lapic *apic = vcpu->arch.apic;

	/*
	 * When APICv is enabled, KVM must always search the IRR for a pending
	 * IRQ, as other vCPUs and devices can set IRR bits even if the vCPU
	 * isn't running.  If APICv is disabled, KVM _should_ search the IRR
	 * for a pending IRQ.  But KVM currently doesn't ensure *all* hardware,
	 * e.g. CPUs and IOMMUs, has seen the change in state, i.e. searching
	 * the IRR at this time could race with IRQ delivery from hardware that
	 * still sees APICv as being enabled.
	 *
	 * FIXME: Ensure other vCPUs and devices observe the change in APICv
	 *        state prior to updating KVM's metadata caches, so that KVM
	 *        can safely search the IRR and set irr_pending accordingly.
	 */
	apic->irr_pending = true;

	if (apic->apicv_active)
		apic->isr_count = 1;
	else
		apic->isr_count = count_vectors(apic->regs + APIC_ISR);

	apic->highest_isr_cache = -1;
}

int kvm_alloc_apic_access_page(struct kvm *kvm)
{
	void __user *hva;

	guard(mutex)(&kvm->slots_lock);

	if (kvm->arch.apic_access_memslot_enabled ||
	    kvm->arch.apic_access_memslot_inhibited)
		return 0;

	hva = __x86_set_memory_region(kvm, APIC_ACCESS_PAGE_PRIVATE_MEMSLOT,
				      APIC_DEFAULT_PHYS_BASE, PAGE_SIZE);
	if (IS_ERR(hva))
		return PTR_ERR(hva);

	kvm->arch.apic_access_memslot_enabled = true;

	return 0;
}
EXPORT_SYMBOL_FOR_KVM_INTERNAL(kvm_alloc_apic_access_page);

void kvm_inhibit_apic_access_page(struct kvm_vcpu *vcpu)
{
	struct kvm *kvm = vcpu->kvm;

	if (!kvm->arch.apic_access_memslot_enabled)
		return;

	kvm_vcpu_srcu_read_unlock(vcpu);

	mutex_lock(&kvm->slots_lock);

	if (kvm->arch.apic_access_memslot_enabled) {
		__x86_set_memory_region(kvm, APIC_ACCESS_PAGE_PRIVATE_MEMSLOT, 0, 0);
		/*
		 * Clear "enabled" after the memslot is deleted so that a
		 * different vCPU doesn't get a false negative when checking
		 * the flag out of slots_lock.  No additional memory barrier is
		 * needed as modifying memslots requires waiting other vCPUs to
		 * drop SRCU (see above), and false positives are ok as the
		 * flag is rechecked after acquiring slots_lock.
		 */
		kvm->arch.apic_access_memslot_enabled = false;

		/*
		 * Mark the memslot as inhibited to prevent reallocating the
		 * memslot during vCPU creation, e.g. if a vCPU is hotplugged.
		 */
		kvm->arch.apic_access_memslot_inhibited = true;
	}

	mutex_unlock(&kvm->slots_lock);

	kvm_vcpu_srcu_read_lock(vcpu);
}

void kvm_lapic_reset(struct kvm_vcpu *vcpu, bool init_event)
{
	struct kvm_lapic *apic = vcpu->arch.apic;
	u64 msr_val;
	int i;

	kvm_x86_call(apicv_pre_state_restore)(vcpu);

	if (!init_event) {
		msr_val = APIC_DEFAULT_PHYS_BASE | MSR_IA32_APICBASE_ENABLE;
		if (kvm_vcpu_is_reset_bsp(vcpu))
			msr_val |= MSR_IA32_APICBASE_BSP;

		/*
		 * Use the inner helper to avoid an extra recalcuation of the
		 * optimized APIC map if some other task has dirtied the map.
		 * The recalculation needed for this vCPU will be done after
		 * all APIC state has been initialized (see below).
		 */
		__kvm_apic_set_base(vcpu, msr_val);
	}

	if (!apic)
		return;

	/* Stop the timer in case it's a reset to an active apic */
	hrtimer_cancel(&apic->lapic_timer.timer);

	/* The xAPIC ID is set at RESET even if the APIC was already enabled. */
	if (!init_event)
		kvm_apic_set_xapic_id(apic, vcpu->vcpu_id);
	kvm_apic_set_version(apic->vcpu);

	for (i = 0; i < apic->nr_lvt_entries; i++)
		kvm_lapic_set_reg(apic, APIC_LVTx(i), APIC_LVT_MASKED);
	apic_update_lvtt(apic);
	if (kvm_vcpu_is_reset_bsp(vcpu) &&
	    kvm_check_has_quirk(vcpu->kvm, KVM_X86_QUIRK_LINT0_REENABLED))
		kvm_lapic_set_reg(apic, APIC_LVT0,
			     SET_APIC_DELIVERY_MODE(0, APIC_MODE_EXTINT));
	apic_manage_nmi_watchdog(apic, kvm_lapic_get_reg(apic, APIC_LVT0));

	kvm_apic_set_dfr(apic, 0xffffffffU);
	apic_set_spiv(apic, 0xff);
	kvm_lapic_set_reg(apic, APIC_TASKPRI, 0);
	if (!apic_x2apic_mode(apic))
		kvm_apic_set_ldr(apic, 0);
	kvm_lapic_set_reg(apic, APIC_ESR, 0);
	if (!apic_x2apic_mode(apic)) {
		kvm_lapic_set_reg(apic, APIC_ICR, 0);
		kvm_lapic_set_reg(apic, APIC_ICR2, 0);
	} else {
		kvm_lapic_set_reg64(apic, APIC_ICR, 0);
	}
	kvm_lapic_set_reg(apic, APIC_TDCR, 0);
	kvm_lapic_set_reg(apic, APIC_TMICT, 0);
	for (i = 0; i < 8; i++) {
		kvm_lapic_set_reg(apic, APIC_IRR + 0x10 * i, 0);
		kvm_lapic_set_reg(apic, APIC_ISR + 0x10 * i, 0);
		kvm_lapic_set_reg(apic, APIC_TMR + 0x10 * i, 0);
	}
	kvm_apic_update_apicv(vcpu);
	update_divide_count(apic);
	atomic_set(&apic->lapic_timer.pending, 0);

	vcpu->arch.pv_eoi.msr_val = 0;
	apic_update_ppr(apic);
	if (apic->apicv_active) {
		kvm_x86_call(apicv_post_state_restore)(vcpu);
		kvm_x86_call(hwapic_isr_update)(vcpu, -1);
	}

	vcpu->arch.apic_arb_prio = 0;
	vcpu->arch.apic_attention = 0;

	kvm_recalculate_apic_map(vcpu->kvm);
}

/*
 *----------------------------------------------------------------------
 * timer interface
 *----------------------------------------------------------------------
 */

static bool lapic_is_periodic(struct kvm_lapic *apic)
{
	return apic_lvtt_period(apic);
}

int apic_has_pending_timer(struct kvm_vcpu *vcpu)
{
	struct kvm_lapic *apic = vcpu->arch.apic;

	if (apic_enabled(apic) && apic_lvt_enabled(apic, APIC_LVTT))
		return atomic_read(&apic->lapic_timer.pending);

	return 0;
}

int kvm_apic_local_deliver(struct kvm_lapic *apic, int lvt_type)
{
	u32 reg = kvm_lapic_get_reg(apic, lvt_type);
	int vector, mode, trig_mode;
	int r;

	if (kvm_apic_hw_enabled(apic) && !(reg & APIC_LVT_MASKED)) {
		vector = reg & APIC_VECTOR_MASK;
		mode = reg & APIC_MODE_MASK;
		trig_mode = reg & APIC_LVT_LEVEL_TRIGGER;

		r = __apic_accept_irq(apic, mode, vector, 1, trig_mode, NULL);
		if (r && lvt_type == APIC_LVTPC &&
		    guest_cpuid_is_intel_compatible(apic->vcpu))
			kvm_lapic_set_reg(apic, APIC_LVTPC, reg | APIC_LVT_MASKED);
		return r;
	}
	return 0;
}

void kvm_apic_nmi_wd_deliver(struct kvm_vcpu *vcpu)
{
	struct kvm_lapic *apic = vcpu->arch.apic;

	if (apic)
		kvm_apic_local_deliver(apic, APIC_LVT0);
}

static const struct kvm_io_device_ops apic_mmio_ops = {
	.read     = apic_mmio_read,
	.write    = apic_mmio_write,
};

static enum hrtimer_restart apic_timer_fn(struct hrtimer *data)
{
	struct kvm_timer *ktimer = container_of(data, struct kvm_timer, timer);
	struct kvm_lapic *apic = container_of(ktimer, struct kvm_lapic, lapic_timer);

	apic_timer_expired(apic, true);

	if (lapic_is_periodic(apic)) {
		advance_periodic_target_expiration(apic);
		hrtimer_add_expires_ns(&ktimer->timer, ktimer->period);
		return HRTIMER_RESTART;
	} else
		return HRTIMER_NORESTART;
}

int kvm_create_lapic(struct kvm_vcpu *vcpu)
{
	struct kvm_lapic *apic;

	ASSERT(vcpu != NULL);

	if (!irqchip_in_kernel(vcpu->kvm)) {
		static_branch_inc(&kvm_has_noapic_vcpu);
		return 0;
	}

	apic = kzalloc(sizeof(*apic), GFP_KERNEL_ACCOUNT);
	if (!apic)
		goto nomem;

	vcpu->arch.apic = apic;

	if (kvm_x86_ops.alloc_apic_backing_page)
		apic->regs = kvm_x86_call(alloc_apic_backing_page)(vcpu);
	else
		apic->regs = (void *)get_zeroed_page(GFP_KERNEL_ACCOUNT);
	if (!apic->regs) {
		printk(KERN_ERR "malloc apic regs error for vcpu %x\n",
		       vcpu->vcpu_id);
		goto nomem_free_apic;
	}
	apic->vcpu = vcpu;

	apic->nr_lvt_entries = kvm_apic_calc_nr_lvt_entries(vcpu);

	hrtimer_setup(&apic->lapic_timer.timer, apic_timer_fn, CLOCK_MONOTONIC,
		      HRTIMER_MODE_ABS_HARD);
	if (lapic_timer_advance)
		apic->lapic_timer.timer_advance_ns = LAPIC_TIMER_ADVANCE_NS_INIT;

	/*
	 * Stuff the APIC ENABLE bit in lieu of temporarily incrementing
	 * apic_hw_disabled; the full RESET value is set by kvm_lapic_reset().
	 */
	vcpu->arch.apic_base = MSR_IA32_APICBASE_ENABLE;
	static_branch_inc(&apic_sw_disabled.key); /* sw disabled at reset */
	kvm_iodevice_init(&apic->dev, &apic_mmio_ops);

	/*
	 * Defer evaluating inhibits until the vCPU is first run, as this vCPU
	 * will not get notified of any changes until this vCPU is visible to
	 * other vCPUs (marked online and added to the set of vCPUs).
	 *
	 * Opportunistically mark APICv active as VMX in particularly is highly
	 * unlikely to have inhibits.  Ignore the current per-VM APICv state so
	 * that vCPU creation is guaranteed to run with a deterministic value,
	 * the request will ensure the vCPU gets the correct state before VM-Entry.
	 */
	if (enable_apicv) {
		apic->apicv_active = true;
		kvm_make_request(KVM_REQ_APICV_UPDATE, vcpu);
	}

	return 0;
nomem_free_apic:
	kfree(apic);
	vcpu->arch.apic = NULL;
nomem:
	return -ENOMEM;
}

int kvm_apic_has_interrupt(struct kvm_vcpu *vcpu)
{
	struct kvm_lapic *apic = vcpu->arch.apic;
	u32 ppr;

	if (!kvm_apic_present(vcpu))
		return -1;

	if (apic->guest_apic_protected)
		return -1;

	__apic_update_ppr(apic, &ppr);
	return apic_has_interrupt_for_ppr(apic, ppr);
}
EXPORT_SYMBOL_FOR_KVM_INTERNAL(kvm_apic_has_interrupt);

int kvm_apic_accept_pic_intr(struct kvm_vcpu *vcpu)
{
	u32 lvt0 = kvm_lapic_get_reg(vcpu->arch.apic, APIC_LVT0);

	if (!kvm_apic_hw_enabled(vcpu->arch.apic))
		return 1;
	if ((lvt0 & APIC_LVT_MASKED) == 0 &&
	    GET_APIC_DELIVERY_MODE(lvt0) == APIC_MODE_EXTINT)
		return 1;
	return 0;
}

void kvm_inject_apic_timer_irqs(struct kvm_vcpu *vcpu)
{
	struct kvm_lapic *apic = vcpu->arch.apic;

	if (atomic_read(&apic->lapic_timer.pending) > 0) {
		kvm_apic_inject_pending_timer_irqs(apic);
		atomic_set(&apic->lapic_timer.pending, 0);
	}
}

void kvm_apic_ack_interrupt(struct kvm_vcpu *vcpu, int vector)
{
	struct kvm_lapic *apic = vcpu->arch.apic;
	u32 ppr;

	if (WARN_ON_ONCE(vector < 0 || !apic))
		return;

	/*
	 * We get here even with APIC virtualization enabled, if doing
	 * nested virtualization and L1 runs with the "acknowledge interrupt
	 * on exit" mode.  Then we cannot inject the interrupt via RVI,
	 * because the process would deliver it through the IDT.
	 */

	apic_clear_irr(vector, apic);
	if (kvm_hv_synic_auto_eoi_set(vcpu, vector)) {
		/*
		 * For auto-EOI interrupts, there might be another pending
		 * interrupt above PPR, so check whether to raise another
		 * KVM_REQ_EVENT.
		 */
		apic_update_ppr(apic);
	} else {
		/*
		 * For normal interrupts, PPR has been raised and there cannot
		 * be a higher-priority pending interrupt---except if there was
		 * a concurrent interrupt injection, but that would have
		 * triggered KVM_REQ_EVENT already.
		 */
		apic_set_isr(vector, apic);
		__apic_update_ppr(apic, &ppr);
	}

}
EXPORT_SYMBOL_FOR_KVM_INTERNAL(kvm_apic_ack_interrupt);

static int kvm_apic_state_fixup(struct kvm_vcpu *vcpu,
		struct kvm_lapic_state *s, bool set)
{
	if (apic_x2apic_mode(vcpu->arch.apic)) {
		u32 x2apic_id = kvm_x2apic_id(vcpu->arch.apic);
		u32 *id = (u32 *)(s->regs + APIC_ID);
		u32 *ldr = (u32 *)(s->regs + APIC_LDR);
		u64 icr;

		if (vcpu->kvm->arch.x2apic_format) {
			if (*id != x2apic_id)
				return -EINVAL;
		} else {
			/*
			 * Ignore the userspace value when setting APIC state.
			 * KVM's model is that the x2APIC ID is readonly, e.g.
			 * KVM only supports delivering interrupts to KVM's
			 * version of the x2APIC ID.  However, for backwards
			 * compatibility, don't reject attempts to set a
			 * mismatched ID for userspace that hasn't opted into
			 * x2apic_format.
			 */
			if (set)
				*id = x2apic_id;
			else
				*id = x2apic_id << 24;
		}

		/*
		 * In x2APIC mode, the LDR is fixed and based on the id.  And
		 * if the ICR is _not_ split, ICR is internally a single 64-bit
		 * register, but needs to be split to ICR+ICR2 in userspace for
		 * backwards compatibility.
		 */
		if (set)
			*ldr = kvm_apic_calc_x2apic_ldr(x2apic_id);

		if (!kvm_x86_ops.x2apic_icr_is_split) {
			if (set) {
				icr = apic_get_reg(s->regs, APIC_ICR) |
				      (u64)apic_get_reg(s->regs, APIC_ICR2) << 32;
				apic_set_reg64(s->regs, APIC_ICR, icr);
			} else {
				icr = apic_get_reg64(s->regs, APIC_ICR);
				apic_set_reg(s->regs, APIC_ICR2, icr >> 32);
			}
		}
	}

	return 0;
}

int kvm_apic_get_state(struct kvm_vcpu *vcpu, struct kvm_lapic_state *s)
{
	memcpy(s->regs, vcpu->arch.apic->regs, sizeof(*s));

	/*
	 * Get calculated timer current count for remaining timer period (if
	 * any) and store it in the returned register set.
	 */
	apic_set_reg(s->regs, APIC_TMCCT, __apic_read(vcpu->arch.apic, APIC_TMCCT));

	return kvm_apic_state_fixup(vcpu, s, false);
}

int kvm_apic_set_state(struct kvm_vcpu *vcpu, struct kvm_lapic_state *s)
{
	struct kvm_lapic *apic = vcpu->arch.apic;
	int r;

	kvm_x86_call(apicv_pre_state_restore)(vcpu);

	/* set SPIV separately to get count of SW disabled APICs right */
	apic_set_spiv(apic, *((u32 *)(s->regs + APIC_SPIV)));

	r = kvm_apic_state_fixup(vcpu, s, true);
	if (r) {
		kvm_recalculate_apic_map(vcpu->kvm);
		return r;
	}
	memcpy(vcpu->arch.apic->regs, s->regs, sizeof(*s));

	atomic_set_release(&apic->vcpu->kvm->arch.apic_map_dirty, DIRTY);
	kvm_recalculate_apic_map(vcpu->kvm);
	kvm_apic_set_version(vcpu);

	apic_update_ppr(apic);
	cancel_apic_timer(apic);
	apic->lapic_timer.expired_tscdeadline = 0;
	apic_update_lvtt(apic);
	apic_manage_nmi_watchdog(apic, kvm_lapic_get_reg(apic, APIC_LVT0));
	update_divide_count(apic);
	__start_apic_timer(apic, APIC_TMCCT);
	kvm_lapic_set_reg(apic, APIC_TMCCT, 0);
	kvm_apic_update_apicv(vcpu);
	if (apic->apicv_active) {
		kvm_x86_call(apicv_post_state_restore)(vcpu);
		kvm_x86_call(hwapic_isr_update)(vcpu, apic_find_highest_isr(apic));
	}
	kvm_make_request(KVM_REQ_EVENT, vcpu);

#ifdef CONFIG_KVM_IOAPIC
	if (ioapic_in_kernel(vcpu->kvm))
		kvm_rtc_eoi_tracking_restore_one(vcpu);
#endif

	vcpu->arch.apic_arb_prio = 0;

	return 0;
}

void __kvm_migrate_apic_timer(struct kvm_vcpu *vcpu)
{
	struct hrtimer *timer;

	if (!lapic_in_kernel(vcpu) ||
		kvm_can_post_timer_interrupt(vcpu))
		return;

	timer = &vcpu->arch.apic->lapic_timer.timer;
	if (hrtimer_cancel(timer))
		hrtimer_start_expires(timer, HRTIMER_MODE_ABS_HARD);
}

/*
 * apic_sync_pv_eoi_from_guest - called on vmexit or cancel interrupt
 *
 * Detect whether guest triggered PV EOI since the
 * last entry. If yes, set EOI on guests's behalf.
 * Clear PV EOI in guest memory in any case.
 */
static void apic_sync_pv_eoi_from_guest(struct kvm_vcpu *vcpu,
					struct kvm_lapic *apic)
{
	int vector;
	/*
	 * PV EOI state is derived from KVM_APIC_PV_EOI_PENDING in host
	 * and KVM_PV_EOI_ENABLED in guest memory as follows:
	 *
	 * KVM_APIC_PV_EOI_PENDING is unset:
	 * 	-> host disabled PV EOI.
	 * KVM_APIC_PV_EOI_PENDING is set, KVM_PV_EOI_ENABLED is set:
	 * 	-> host enabled PV EOI, guest did not execute EOI yet.
	 * KVM_APIC_PV_EOI_PENDING is set, KVM_PV_EOI_ENABLED is unset:
	 * 	-> host enabled PV EOI, guest executed EOI.
	 */
	BUG_ON(!pv_eoi_enabled(vcpu));

	if (pv_eoi_test_and_clr_pending(vcpu))
		return;
	vector = apic_set_eoi(apic);
	trace_kvm_pv_eoi(apic, vector);
}

void kvm_lapic_sync_from_vapic(struct kvm_vcpu *vcpu)
{
	u32 data;

	if (test_bit(KVM_APIC_PV_EOI_PENDING, &vcpu->arch.apic_attention))
		apic_sync_pv_eoi_from_guest(vcpu, vcpu->arch.apic);

	if (!test_bit(KVM_APIC_CHECK_VAPIC, &vcpu->arch.apic_attention))
		return;

	if (kvm_read_guest_cached(vcpu->kvm, &vcpu->arch.apic->vapic_cache, &data,
				  sizeof(u32)))
		return;

	apic_set_tpr(vcpu->arch.apic, data & 0xff);
}

/*
 * apic_sync_pv_eoi_to_guest - called before vmentry
 *
 * Detect whether it's safe to enable PV EOI and
 * if yes do so.
 */
static void apic_sync_pv_eoi_to_guest(struct kvm_vcpu *vcpu,
					struct kvm_lapic *apic)
{
	if (!pv_eoi_enabled(vcpu) ||
	    /* IRR set or many bits in ISR: could be nested. */
	    apic->irr_pending ||
	    /* Cache not set: could be safe but we don't bother. */
	    apic->highest_isr_cache == -1 ||
	    /* Need EOI to update ioapic. */
	    kvm_ioapic_handles_vector(apic, apic->highest_isr_cache)) {
		/*
		 * PV EOI was disabled by apic_sync_pv_eoi_from_guest
		 * so we need not do anything here.
		 */
		return;
	}

	pv_eoi_set_pending(apic->vcpu);
}

void kvm_lapic_sync_to_vapic(struct kvm_vcpu *vcpu)
{
	u32 data, tpr;
	int max_irr, max_isr;
	struct kvm_lapic *apic = vcpu->arch.apic;

	apic_sync_pv_eoi_to_guest(vcpu, apic);

	if (!test_bit(KVM_APIC_CHECK_VAPIC, &vcpu->arch.apic_attention))
		return;

	tpr = kvm_lapic_get_reg(apic, APIC_TASKPRI) & 0xff;
	max_irr = apic_find_highest_irr(apic);
	if (max_irr < 0)
		max_irr = 0;
	max_isr = apic_find_highest_isr(apic);
	if (max_isr < 0)
		max_isr = 0;
	data = (tpr & 0xff) | ((max_isr & 0xf0) << 8) | (max_irr << 24);

	kvm_write_guest_cached(vcpu->kvm, &vcpu->arch.apic->vapic_cache, &data,
				sizeof(u32));
}

int kvm_lapic_set_vapic_addr(struct kvm_vcpu *vcpu, gpa_t vapic_addr)
{
	if (vapic_addr) {
		if (kvm_gfn_to_hva_cache_init(vcpu->kvm,
					&vcpu->arch.apic->vapic_cache,
					vapic_addr, sizeof(u32)))
			return -EINVAL;
		__set_bit(KVM_APIC_CHECK_VAPIC, &vcpu->arch.apic_attention);
	} else {
		__clear_bit(KVM_APIC_CHECK_VAPIC, &vcpu->arch.apic_attention);
	}

	vcpu->arch.apic->vapic_addr = vapic_addr;
	return 0;
}

static int kvm_lapic_msr_read(struct kvm_lapic *apic, u32 reg, u64 *data)
{
	u32 low;

	if (reg == APIC_ICR) {
		*data = kvm_x2apic_icr_read(apic);
		return 0;
	}

	if (kvm_lapic_reg_read(apic, reg, 4, &low))
		return 1;

	*data = low;

	return 0;
}

static int kvm_lapic_msr_write(struct kvm_lapic *apic, u32 reg, u64 data)
{
	/*
	 * ICR is a 64-bit register in x2APIC mode (and Hyper-V PV vAPIC) and
	 * can be written as such, all other registers remain accessible only
	 * through 32-bit reads/writes.
	 */
	if (reg == APIC_ICR)
		return kvm_x2apic_icr_write(apic, data);

	/* Bits 63:32 are reserved in all other registers. */
	if (data >> 32)
		return 1;

	return kvm_lapic_reg_write(apic, reg, (u32)data);
}

int kvm_x2apic_msr_write(struct kvm_vcpu *vcpu, u32 msr, u64 data)
{
	struct kvm_lapic *apic = vcpu->arch.apic;
	u32 reg = (msr - APIC_BASE_MSR) << 4;

	if (!lapic_in_kernel(vcpu) || !apic_x2apic_mode(apic))
		return 1;

	return kvm_lapic_msr_write(apic, reg, data);
}

int kvm_x2apic_msr_read(struct kvm_vcpu *vcpu, u32 msr, u64 *data)
{
	struct kvm_lapic *apic = vcpu->arch.apic;
	u32 reg = (msr - APIC_BASE_MSR) << 4;

	if (!lapic_in_kernel(vcpu) || !apic_x2apic_mode(apic))
		return 1;

	return kvm_lapic_msr_read(apic, reg, data);
}

int kvm_hv_vapic_msr_write(struct kvm_vcpu *vcpu, u32 reg, u64 data)
{
	if (!lapic_in_kernel(vcpu))
		return 1;

	return kvm_lapic_msr_write(vcpu->arch.apic, reg, data);
}

int kvm_hv_vapic_msr_read(struct kvm_vcpu *vcpu, u32 reg, u64 *data)
{
	if (!lapic_in_kernel(vcpu))
		return 1;

	return kvm_lapic_msr_read(vcpu->arch.apic, reg, data);
}

int kvm_lapic_set_pv_eoi(struct kvm_vcpu *vcpu, u64 data, unsigned long len)
{
	u64 addr = data & ~KVM_MSR_ENABLED;
	struct gfn_to_hva_cache *ghc = &vcpu->arch.pv_eoi.data;
	unsigned long new_len;
	int ret;

	if (!IS_ALIGNED(addr, 4))
		return 1;

	if (data & KVM_MSR_ENABLED) {
		if (addr == ghc->gpa && len <= ghc->len)
			new_len = ghc->len;
		else
			new_len = len;

		ret = kvm_gfn_to_hva_cache_init(vcpu->kvm, ghc, addr, new_len);
		if (ret)
			return ret;
	}

	vcpu->arch.pv_eoi.msr_val = data;

	return 0;
}

int kvm_apic_accept_events(struct kvm_vcpu *vcpu)
{
	struct kvm_lapic *apic = vcpu->arch.apic;
	u8 sipi_vector;
	int r;

	if (!kvm_apic_has_pending_init_or_sipi(vcpu))
		return 0;

	if (is_guest_mode(vcpu)) {
		r = kvm_check_nested_events(vcpu);
		if (r < 0)
			return r == -EBUSY ? 0 : r;
		/*
		 * Continue processing INIT/SIPI even if a nested VM-Exit
		 * occurred, e.g. pending SIPIs should be dropped if INIT+SIPI
		 * are blocked as a result of transitioning to VMX root mode.
		 */
	}

	/*
	 * INITs are blocked while CPU is in specific states (SMM, VMX root
	 * mode, SVM with GIF=0), while SIPIs are dropped if the CPU isn't in
	 * wait-for-SIPI (WFS).
	 */
	if (!kvm_apic_init_sipi_allowed(vcpu)) {
		WARN_ON_ONCE(vcpu->arch.mp_state == KVM_MP_STATE_INIT_RECEIVED);
		clear_bit(KVM_APIC_SIPI, &apic->pending_events);
		return 0;
	}

	if (test_and_clear_bit(KVM_APIC_INIT, &apic->pending_events)) {
		kvm_vcpu_reset(vcpu, true);
		if (kvm_vcpu_is_bsp(apic->vcpu))
			kvm_set_mp_state(vcpu, KVM_MP_STATE_RUNNABLE);
		else
			kvm_set_mp_state(vcpu, KVM_MP_STATE_INIT_RECEIVED);
	}
	if (test_and_clear_bit(KVM_APIC_SIPI, &apic->pending_events)) {
		if (vcpu->arch.mp_state == KVM_MP_STATE_INIT_RECEIVED) {
			/* evaluate pending_events before reading the vector */
			smp_rmb();
			sipi_vector = apic->sipi_vector;
			kvm_x86_call(vcpu_deliver_sipi_vector)(vcpu,
							       sipi_vector);
			kvm_set_mp_state(vcpu, KVM_MP_STATE_RUNNABLE);
		}
	}
	return 0;
}

void kvm_lapic_exit(void)
{
	static_key_deferred_flush(&apic_hw_disabled);
	WARN_ON(static_branch_unlikely(&apic_hw_disabled.key));
	static_key_deferred_flush(&apic_sw_disabled);
	WARN_ON(static_branch_unlikely(&apic_sw_disabled.key));
}
