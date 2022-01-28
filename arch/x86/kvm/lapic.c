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
#include <asm/processor.h>
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
#include "cpuid.h"
#include "hyperv.h"

#ifndef CONFIG_X86_64
#define mod_64(x, y) ((x) - (y) * div64_u64(x, y))
#else
#define mod_64(x, y) ((x) % (y))
#endif

#define PRId64 "d"
#define PRIx64 "llx"
#define PRIu64 "u"
#define PRIo64 "o"

/* 14 is the version for Xeon and Pentium 8.4.8*/
#define APIC_VERSION			(0x14UL | ((KVM_APIC_LVT_NUM - 1) << 16))
#define LAPIC_MMIO_LENGTH		(1 << 12)
/* followed define is not in apicdef.h */
#define MAX_APIC_VECTOR			256
#define APIC_VECTORS_PER_REG		32

static bool lapic_timer_advance_dynamic __read_mostly;
#define LAPIC_TIMER_ADVANCE_ADJUST_MIN	100	/* clock cycles */
#define LAPIC_TIMER_ADVANCE_ADJUST_MAX	10000	/* clock cycles */
#define LAPIC_TIMER_ADVANCE_NS_INIT	1000
#define LAPIC_TIMER_ADVANCE_NS_MAX     5000
/* step-by-step approximation to mitigate fluctuation */
#define LAPIC_TIMER_ADVANCE_ADJUST_STEP 8

static inline int apic_test_vector(int vec, void *bitmap)
{
	return test_bit(VEC_POS(vec), (bitmap) + REG_POS(vec));
}

bool kvm_apic_pending_eoi(struct kvm_vcpu *vcpu, int vector)
{
	struct kvm_lapic *apic = vcpu->arch.apic;

	return apic_test_vector(vector, apic->regs + APIC_ISR) ||
		apic_test_vector(vector, apic->regs + APIC_IRR);
}

static inline int __apic_test_and_set_vector(int vec, void *bitmap)
{
	return __test_and_set_bit(VEC_POS(vec), (bitmap) + REG_POS(vec));
}

static inline int __apic_test_and_clear_vector(int vec, void *bitmap)
{
	return __test_and_clear_bit(VEC_POS(vec), (bitmap) + REG_POS(vec));
}

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
	return pi_inject_timer && kvm_vcpu_apicv_active(vcpu);
}

bool kvm_can_use_hv_timer(struct kvm_vcpu *vcpu)
{
	return kvm_x86_ops.set_hv_timer
	       && !(kvm_mwait_in_guest(vcpu->kvm) ||
		    kvm_can_post_timer_interrupt(vcpu));
}
EXPORT_SYMBOL_GPL(kvm_can_use_hv_timer);

static bool kvm_use_posted_timer_interrupt(struct kvm_vcpu *vcpu)
{
	return kvm_can_post_timer_interrupt(vcpu) && vcpu->mode == IN_GUEST_MODE;
}

static inline bool kvm_apic_map_get_logical_dest(struct kvm_apic_map *map,
		u32 dest_id, struct kvm_lapic ***cluster, u16 *mask) {
	switch (map->mode) {
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
	default:
		/* Not optimized. */
		return false;
	}
}

static void kvm_apic_map_free(struct rcu_head *rcu)
{
	struct kvm_apic_map *map = container_of(rcu, struct kvm_apic_map, rcu);

	kvfree(map);
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

void kvm_recalculate_apic_map(struct kvm *kvm)
{
	struct kvm_apic_map *new, *old = NULL;
	struct kvm_vcpu *vcpu;
	unsigned long i;
	u32 max_id = 255; /* enough space for any xAPIC ID */

	/* Read kvm->arch.apic_map_dirty before kvm->arch.apic_map.  */
	if (atomic_read_acquire(&kvm->arch.apic_map_dirty) == CLEAN)
		return;

	WARN_ONCE(!irqchip_in_kernel(kvm),
		  "Dirty APIC map without an in-kernel local APIC");

	mutex_lock(&kvm->arch.apic_map_lock);
	/*
	 * Read kvm->arch.apic_map_dirty before kvm->arch.apic_map
	 * (if clean) or the APIC registers (if dirty).
	 */
	if (atomic_cmpxchg_acquire(&kvm->arch.apic_map_dirty,
				   DIRTY, UPDATE_IN_PROGRESS) == CLEAN) {
		/* Someone else has updated the map. */
		mutex_unlock(&kvm->arch.apic_map_lock);
		return;
	}

	kvm_for_each_vcpu(i, vcpu, kvm)
		if (kvm_apic_present(vcpu))
			max_id = max(max_id, kvm_x2apic_id(vcpu->arch.apic));

	new = kvzalloc(sizeof(struct kvm_apic_map) +
	                   sizeof(struct kvm_lapic *) * ((u64)max_id + 1),
			   GFP_KERNEL_ACCOUNT);

	if (!new)
		goto out;

	new->max_apic_id = max_id;

	kvm_for_each_vcpu(i, vcpu, kvm) {
		struct kvm_lapic *apic = vcpu->arch.apic;
		struct kvm_lapic **cluster;
		u16 mask;
		u32 ldr;
		u8 xapic_id;
		u32 x2apic_id;

		if (!kvm_apic_present(vcpu))
			continue;

		xapic_id = kvm_xapic_id(apic);
		x2apic_id = kvm_x2apic_id(apic);

		/* Hotplug hack: see kvm_apic_match_physical_addr(), ... */
		if ((apic_x2apic_mode(apic) || x2apic_id > 0xff) &&
				x2apic_id <= new->max_apic_id)
			new->phys_map[x2apic_id] = apic;
		/*
		 * ... xAPIC ID of VCPUs with APIC ID > 0xff will wrap-around,
		 * prevent them from masking VCPUs with APIC ID <= 0xff.
		 */
		if (!apic_x2apic_mode(apic) && !new->phys_map[xapic_id])
			new->phys_map[xapic_id] = apic;

		if (!kvm_apic_sw_enabled(apic))
			continue;

		ldr = kvm_lapic_get_reg(apic, APIC_LDR);

		if (apic_x2apic_mode(apic)) {
			new->mode |= KVM_APIC_MODE_X2APIC;
		} else if (ldr) {
			ldr = GET_APIC_LOGICAL_ID(ldr);
			if (kvm_lapic_get_reg(apic, APIC_DFR) == APIC_DFR_FLAT)
				new->mode |= KVM_APIC_MODE_XAPIC_FLAT;
			else
				new->mode |= KVM_APIC_MODE_XAPIC_CLUSTER;
		}

		if (!kvm_apic_map_get_logical_dest(new, ldr, &cluster, &mask))
			continue;

		if (mask)
			cluster[ffs(mask) - 1] = apic;
	}
out:
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
		call_rcu(&old->rcu, kvm_apic_map_free);

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
	if (enabled)
		kvm_make_request(KVM_REQ_APF_READY, apic->vcpu);
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

static inline u32 kvm_apic_calc_x2apic_ldr(u32 id)
{
	return ((id >> 4) << 16) | (1 << (id & 0xf));
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

void kvm_apic_set_version(struct kvm_vcpu *vcpu)
{
	struct kvm_lapic *apic = vcpu->arch.apic;
	u32 v = APIC_VERSION;

	if (!lapic_in_kernel(vcpu))
		return;

	/*
	 * KVM emulates 82093AA datasheet (with in-kernel IOAPIC implementation)
	 * which doesn't have EOI register; Some buggy OSes (e.g. Windows with
	 * Hyper-V role) disable EOI broadcast in lapic not checking for IOAPIC
	 * version first and level-triggered interrupts never get EOIed in
	 * IOAPIC.
	 */
	if (guest_cpuid_has(vcpu, X86_FEATURE_X2APIC) &&
	    !ioapic_in_kernel(vcpu->kvm))
		v |= APIC_LVR_DIRECTED_EOI;
	kvm_lapic_set_reg(apic, APIC_LVR, v);
}

static const unsigned int apic_lvt_mask[KVM_APIC_LVT_NUM] = {
	LVT_MASK ,      /* part LVTT mask, timer mode mask added at runtime */
	LVT_MASK | APIC_MODE_MASK,	/* LVTTHMR */
	LVT_MASK | APIC_MODE_MASK,	/* LVTPC */
	LINT_MASK, LINT_MASK,	/* LVT0-1 */
	LVT_MASK		/* LVTERR */
};

static int find_highest_vector(void *bitmap)
{
	int vec;
	u32 *reg;

	for (vec = MAX_APIC_VECTOR - APIC_VECTORS_PER_REG;
	     vec >= 0; vec -= APIC_VECTORS_PER_REG) {
		reg = bitmap + REG_POS(vec);
		if (*reg)
			return __fls(*reg) + vec;
	}

	return -1;
}

static u8 count_vectors(void *bitmap)
{
	int vec;
	u32 *reg;
	u8 count = 0;

	for (vec = 0; vec < MAX_APIC_VECTOR; vec += APIC_VECTORS_PER_REG) {
		reg = bitmap + REG_POS(vec);
		count += hweight32(*reg);
	}

	return count;
}

bool __kvm_apic_update_irr(u32 *pir, void *regs, int *max_irr)
{
	u32 i, vec;
	u32 pir_val, irr_val, prev_irr_val;
	int max_updated_irr;

	max_updated_irr = -1;
	*max_irr = -1;

	for (i = vec = 0; i <= 7; i++, vec += 32) {
		pir_val = READ_ONCE(pir[i]);
		irr_val = *((u32 *)(regs + APIC_IRR + i * 0x10));
		if (pir_val) {
			prev_irr_val = irr_val;
			irr_val |= xchg(&pir[i], 0);
			*((u32 *)(regs + APIC_IRR + i * 0x10)) = irr_val;
			if (prev_irr_val != irr_val) {
				max_updated_irr =
					__fls(irr_val ^ prev_irr_val) + vec;
			}
		}
		if (irr_val)
			*max_irr = __fls(irr_val) + vec;
	}

	return ((max_updated_irr != -1) &&
		(max_updated_irr == *max_irr));
}
EXPORT_SYMBOL_GPL(__kvm_apic_update_irr);

bool kvm_apic_update_irr(struct kvm_vcpu *vcpu, u32 *pir, int *max_irr)
{
	struct kvm_lapic *apic = vcpu->arch.apic;

	return __kvm_apic_update_irr(pir, apic->regs, max_irr);
}
EXPORT_SYMBOL_GPL(kvm_apic_update_irr);

static inline int apic_search_irr(struct kvm_lapic *apic)
{
	return find_highest_vector(apic->regs + APIC_IRR);
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
	struct kvm_vcpu *vcpu;

	vcpu = apic->vcpu;

	if (unlikely(vcpu->arch.apicv_active)) {
		/* need to update RVI */
		kvm_lapic_clear_vector(vec, apic->regs + APIC_IRR);
		static_call(kvm_x86_hwapic_irr_update)(vcpu,
				apic_find_highest_irr(apic));
	} else {
		apic->irr_pending = false;
		kvm_lapic_clear_vector(vec, apic->regs + APIC_IRR);
		if (apic_search_irr(apic) != -1)
			apic->irr_pending = true;
	}
}

void kvm_apic_clear_irr(struct kvm_vcpu *vcpu, int vec)
{
	apic_clear_irr(vec, vcpu->arch.apic);
}
EXPORT_SYMBOL_GPL(kvm_apic_clear_irr);

static inline void apic_set_isr(int vec, struct kvm_lapic *apic)
{
	struct kvm_vcpu *vcpu;

	if (__apic_test_and_set_vector(vec, apic->regs + APIC_ISR))
		return;

	vcpu = apic->vcpu;

	/*
	 * With APIC virtualization enabled, all caching is disabled
	 * because the processor can modify ISR under the hood.  Instead
	 * just set SVI.
	 */
	if (unlikely(vcpu->arch.apicv_active))
		static_call(kvm_x86_hwapic_isr_update)(vcpu, vec);
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

	result = find_highest_vector(apic->regs + APIC_ISR);
	ASSERT(result == -1 || result >= 16);

	return result;
}

static inline void apic_clear_isr(int vec, struct kvm_lapic *apic)
{
	struct kvm_vcpu *vcpu;
	if (!__apic_test_and_clear_vector(vec, apic->regs + APIC_ISR))
		return;

	vcpu = apic->vcpu;

	/*
	 * We do get here for APIC virtualization enabled if the guest
	 * uses the Hyper-V APIC enlightenment.  In this case we may need
	 * to trigger a new interrupt delivery by writing the SVI field;
	 * on the other hand isr_count and highest_isr_cache are unused
	 * and must be left alone.
	 */
	if (unlikely(vcpu->arch.apicv_active))
		static_call(kvm_x86_hwapic_isr_update)(vcpu,
						apic_find_highest_isr(apic));
	else {
		--apic->isr_count;
		BUG_ON(apic->isr_count < 0);
		apic->highest_isr_cache = -1;
	}
}

int kvm_lapic_find_highest_irr(struct kvm_vcpu *vcpu)
{
	/* This may race with setting of irr in __apic_accept_irq() and
	 * value returned may be wrong, but kvm_vcpu_kick() in __apic_accept_irq
	 * will cause vmexit immediately and the value will be recalculated
	 * on the next vmentry.
	 */
	return apic_find_highest_irr(vcpu->arch.apic);
}
EXPORT_SYMBOL_GPL(kvm_lapic_find_highest_irr);

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
		highest_irr = static_call(kvm_x86_sync_pir_to_irr)(apic->vcpu);
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
EXPORT_SYMBOL_GPL(kvm_apic_update_ppr);

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

	if (apic_x2apic_mode(apic))
		return mda == kvm_x2apic_id(apic);

	/*
	 * Hotplug hack: Make LAPIC in xAPIC mode also accept interrupts as if
	 * it were in x2APIC mode.  Hotplugged VCPUs start in xAPIC mode and
	 * this allows unique addressing of VCPUs with APIC ID over 0xff.
	 * The 0xff condition is needed because writeable xAPIC ID.
	 */
	if (kvm_x2apic_id(apic) > 0xff && mda == kvm_x2apic_id(apic))
		return true;

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
EXPORT_SYMBOL_GPL(kvm_apic_match_dest);

int kvm_vector_to_index(u32 vector, u32 dest_vcpus,
		       const unsigned long *bitmap, u32 bitmap_size)
{
	u32 mod;
	int i, idx = -1;

	mod = vector % dest_vcpus;

	for (i = 0; i <= mod; i++) {
		idx = find_next_bit(bitmap, bitmap_size, idx + 1);
		BUG_ON(idx == bitmap_size);
	}

	return idx;
}

static void kvm_apic_disabled_lapic_found(struct kvm *kvm)
{
	if (!kvm->arch.disabled_lapic_found) {
		kvm->arch.disabled_lapic_found = true;
		printk(KERN_INFO
		       "Disabled LAPIC found during irq injection\n");
	}
}

static bool kvm_apic_is_broadcast_dest(struct kvm *kvm, struct kvm_lapic **src,
		struct kvm_lapic_irq *irq, struct kvm_apic_map *map)
{
	if (kvm->arch.x2apic_broadcast_quirk_disabled) {
		if ((irq->dest_id == APIC_BROADCAST &&
				map->mode != KVM_APIC_MODE_X2APIC))
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

	if (!kvm_vector_hashing_enabled()) {
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
bool kvm_intr_is_single_vcpu_fast(struct kvm *kvm, struct kvm_lapic_irq *irq,
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
				kvm_lapic_set_vector(vector,
						     apic->regs + APIC_TMR);
			else
				kvm_lapic_clear_vector(vector,
						       apic->regs + APIC_TMR);
		}

		static_call(kvm_x86_deliver_interrupt)(apic, delivery_mode,
						       trig_mode, vector);
		break;

	case APIC_DM_REMRD:
		result = 1;
		vcpu->arch.pv.pv_unhalted = 1;
		kvm_make_request(KVM_REQ_EVENT, vcpu);
		kvm_vcpu_kick(vcpu);
		break;

	case APIC_DM_SMI:
		result = 1;
		kvm_make_request(KVM_REQ_SMI, vcpu);
		kvm_vcpu_kick(vcpu);
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

int kvm_apic_compare_prio(struct kvm_vcpu *vcpu1, struct kvm_vcpu *vcpu2)
{
	return vcpu1->arch.apic_arb_prio - vcpu2->arch.apic_arb_prio;
}

static bool kvm_ioapic_handles_vector(struct kvm_lapic *apic, int vector)
{
	return test_bit(vector, apic->vcpu->arch.ioapic_handled_vectors);
}

static void kvm_ioapic_send_eoi(struct kvm_lapic *apic, int vector)
{
	int trigger_mode;

	/* Eoi the ioapic only if the ioapic doesn't own the vector. */
	if (!kvm_ioapic_handles_vector(apic, vector))
		return;

	/* Request a KVM exit to inform the userspace IOAPIC. */
	if (irqchip_split(apic->vcpu->kvm)) {
		apic->vcpu->arch.pending_ioapic_eoi = vector;
		kvm_make_request(KVM_REQ_IOAPIC_EOI_EXIT, apic->vcpu);
		return;
	}

	if (apic_test_vector(vector, apic->regs + APIC_TMR))
		trigger_mode = IOAPIC_LEVEL_TRIG;
	else
		trigger_mode = IOAPIC_EDGE_TRIG;

	kvm_ioapic_update_eoi(apic->vcpu, vector, trigger_mode);
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

	if (to_hv_vcpu(apic->vcpu) &&
	    test_bit(vector, to_hv_synic(apic->vcpu)->vec_bitmap))
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
EXPORT_SYMBOL_GPL(kvm_apic_set_eoi_accelerated);

void kvm_apic_send_ipi(struct kvm_lapic *apic, u32 icr_low, u32 icr_high)
{
	struct kvm_lapic_irq irq;

	irq.vector = icr_low & APIC_VECTOR_MASK;
	irq.delivery_mode = icr_low & APIC_MODE_MASK;
	irq.dest_mode = icr_low & APIC_DEST_MASK;
	irq.level = (icr_low & APIC_INT_ASSERT) != 0;
	irq.trig_mode = icr_low & APIC_INT_LEVELTRIG;
	irq.shorthand = icr_low & APIC_SHORT_MASK;
	irq.msi_redir_hint = false;
	if (apic_x2apic_mode(apic))
		irq.dest_id = icr_high;
	else
		irq.dest_id = GET_APIC_DEST_FIELD(icr_high);

	trace_kvm_apic_ipi(icr_low, irq.dest_id);

	kvm_irq_delivery_to_apic(apic->vcpu->kvm, apic, &irq, NULL);
}

static u32 apic_get_tmcct(struct kvm_lapic *apic)
{
	ktime_t remaining, now;
	s64 ns;
	u32 tmcct;

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
	tmcct = div64_u64(ns,
			 (APIC_BUS_CYCLE_NS * apic->divide_count));

	return tmcct;
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

int kvm_lapic_reg_read(struct kvm_lapic *apic, u32 offset, int len,
		void *data)
{
	unsigned char alignment = offset & 0xf;
	u32 result;
	/* this bitmask has a bit cleared for each reserved register */
	u64 valid_reg_mask =
		APIC_REG_MASK(APIC_ID) |
		APIC_REG_MASK(APIC_LVR) |
		APIC_REG_MASK(APIC_TASKPRI) |
		APIC_REG_MASK(APIC_PROCPRI) |
		APIC_REG_MASK(APIC_LDR) |
		APIC_REG_MASK(APIC_DFR) |
		APIC_REG_MASK(APIC_SPIV) |
		APIC_REGS_MASK(APIC_ISR, APIC_ISR_NR) |
		APIC_REGS_MASK(APIC_TMR, APIC_ISR_NR) |
		APIC_REGS_MASK(APIC_IRR, APIC_ISR_NR) |
		APIC_REG_MASK(APIC_ESR) |
		APIC_REG_MASK(APIC_ICR) |
		APIC_REG_MASK(APIC_ICR2) |
		APIC_REG_MASK(APIC_LVTT) |
		APIC_REG_MASK(APIC_LVTTHMR) |
		APIC_REG_MASK(APIC_LVTPC) |
		APIC_REG_MASK(APIC_LVT0) |
		APIC_REG_MASK(APIC_LVT1) |
		APIC_REG_MASK(APIC_LVTERR) |
		APIC_REG_MASK(APIC_TMICT) |
		APIC_REG_MASK(APIC_TMCCT) |
		APIC_REG_MASK(APIC_TDCR);

	/* ARBPRI is not valid on x2APIC */
	if (!apic_x2apic_mode(apic))
		valid_reg_mask |= APIC_REG_MASK(APIC_ARBPRI);

	if (alignment + len > 4)
		return 1;

	if (offset > 0x3f0 || !(valid_reg_mask & APIC_REG_MASK(offset)))
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
EXPORT_SYMBOL_GPL(kvm_lapic_reg_read);

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
			pr_info_ratelimited(
			    "kvm: vcpu %i: requested %lld ns "
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
	u32 reg = kvm_lapic_get_reg(apic, APIC_LVTT);

	if (kvm_apic_hw_enabled(apic)) {
		int vec = reg & APIC_VECTOR_MASK;
		void *bitmap = apic->regs + APIC_ISR;

		if (vcpu->arch.apicv_active)
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
	if (vcpu->arch.tsc_scaling_ratio == kvm_default_tsc_scaling_ratio) {
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
	apic->lapic_timer.advance_expire_delta = guest_tsc - tsc_deadline;

	if (lapic_timer_advance_dynamic) {
		adjust_lapic_timer_advance(vcpu, apic->lapic_timer.advance_expire_delta);
		/*
		 * If the timer fired early, reread the TSC to account for the
		 * overhead of the above adjustment to avoid waiting longer
		 * than is necessary.
		 */
		if (guest_tsc < tsc_deadline)
			guest_tsc = kvm_read_l1_tsc(vcpu, rdtsc());
	}

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
EXPORT_SYMBOL_GPL(kvm_wait_lapic_expire);

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

	if (!from_timer_fn && vcpu->arch.apicv_active) {
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
	unsigned long this_tsc_khz = vcpu->arch.virtual_tsc_khz;
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
	return (u64)tmict * APIC_BUS_CYCLE_NS * (u64)apic->divide_count;
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
			if (unlikely(deadline <= 0))
				deadline = apic->lapic_timer.period;
			else if (unlikely(deadline > apic->lapic_timer.period)) {
				pr_info_ratelimited(
				    "kvm: vcpu %i: requested lapic timer restore with "
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
EXPORT_SYMBOL_GPL(kvm_lapic_hv_timer_in_use);

static void cancel_hv_timer(struct kvm_lapic *apic)
{
	WARN_ON(preemptible());
	WARN_ON(!apic->lapic_timer.hv_timer_in_use);
	static_call(kvm_x86_cancel_hv_timer)(apic->vcpu);
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

	if (static_call(kvm_x86_set_hv_timer)(vcpu, ktimer->tscdeadline, &expired))
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
EXPORT_SYMBOL_GPL(kvm_lapic_expired_hv_timer);

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

int kvm_lapic_reg_write(struct kvm_lapic *apic, u32 reg, u32 val)
{
	int ret = 0;

	trace_kvm_apic_write(reg, val);

	switch (reg) {
	case APIC_ID:		/* Local APIC ID */
		if (!apic_x2apic_mode(apic))
			kvm_apic_set_xapic_id(apic, val >> 24);
		else
			ret = 1;
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
			u32 lvt_val;

			for (i = 0; i < KVM_APIC_LVT_NUM; i++) {
				lvt_val = kvm_lapic_get_reg(apic,
						       APIC_LVTT + 0x10 * i);
				kvm_lapic_set_reg(apic, APIC_LVTT + 0x10 * i,
					     lvt_val | APIC_LVT_MASKED);
			}
			apic_update_lvtt(apic);
			atomic_set(&apic->lapic_timer.pending, 0);

		}
		break;
	}
	case APIC_ICR:
		/* No delay here, so we always clear the pending bit */
		val &= ~(1 << 12);
		kvm_apic_send_ipi(apic, val, kvm_lapic_get_reg(apic, APIC_ICR2));
		kvm_lapic_set_reg(apic, APIC_ICR, val);
		break;

	case APIC_ICR2:
		if (!apic_x2apic_mode(apic))
			val &= 0xff000000;
		kvm_lapic_set_reg(apic, APIC_ICR2, val);
		break;

	case APIC_LVT0:
		apic_manage_nmi_watchdog(apic, val);
		fallthrough;
	case APIC_LVTTHMR:
	case APIC_LVTPC:
	case APIC_LVT1:
	case APIC_LVTERR: {
		/* TODO: Check vector */
		size_t size;
		u32 index;

		if (!kvm_apic_sw_enabled(apic))
			val |= APIC_LVT_MASKED;
		size = ARRAY_SIZE(apic_lvt_mask);
		index = array_index_nospec(
				(reg - APIC_LVTT) >> 4, size);
		val &= apic_lvt_mask[index];
		kvm_lapic_set_reg(apic, reg, val);
		break;
	}

	case APIC_LVTT:
		if (!kvm_apic_sw_enabled(apic))
			val |= APIC_LVT_MASKED;
		val &= (apic_lvt_mask[0] | apic->lapic_timer.timer_mode_mask);
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
		if (apic_x2apic_mode(apic)) {
			kvm_lapic_reg_write(apic, APIC_ICR,
					    APIC_DEST_SELF | (val & APIC_VECTOR_MASK));
		} else
			ret = 1;
		break;
	default:
		ret = 1;
		break;
	}

	kvm_recalculate_apic_map(apic->vcpu->kvm);

	return ret;
}
EXPORT_SYMBOL_GPL(kvm_lapic_reg_write);

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
EXPORT_SYMBOL_GPL(kvm_lapic_set_eoi);

/* emulate APIC access in a trap manner */
void kvm_apic_write_nodecode(struct kvm_vcpu *vcpu, u32 offset)
{
	u32 val = 0;

	/* hw has done the conditional check and inst decode */
	offset &= 0xff0;

	kvm_lapic_reg_read(vcpu->arch.apic, offset, 4, &val);

	/* TODO: optimize to just emulate side effect w/o one more write */
	kvm_lapic_reg_write(vcpu->arch.apic, offset, val);
}
EXPORT_SYMBOL_GPL(kvm_apic_write_nodecode);

void kvm_free_lapic(struct kvm_vcpu *vcpu)
{
	struct kvm_lapic *apic = vcpu->arch.apic;

	if (!vcpu->arch.apic)
		return;

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
	struct kvm_lapic *apic = vcpu->arch.apic;

	apic_set_tpr(apic, ((cr8 & 0x0f) << 4)
		     | (kvm_lapic_get_reg(apic, APIC_TASKPRI) & 4));
}

u64 kvm_lapic_get_cr8(struct kvm_vcpu *vcpu)
{
	u64 tpr;

	tpr = (u64) kvm_lapic_get_reg(vcpu->arch.apic, APIC_TASKPRI);

	return (tpr & 0xf0) >> 4;
}

void kvm_lapic_set_base(struct kvm_vcpu *vcpu, u64 value)
{
	u64 old_value = vcpu->arch.apic_base;
	struct kvm_lapic *apic = vcpu->arch.apic;

	vcpu->arch.apic_base = value;

	if ((old_value ^ value) & MSR_IA32_APICBASE_ENABLE)
		kvm_update_cpuid_runtime(vcpu);

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

	if (((old_value ^ value) & X2APIC_ENABLE) && (value & X2APIC_ENABLE))
		kvm_apic_set_x2apic_id(apic, vcpu->vcpu_id);

	if ((old_value ^ value) & (MSR_IA32_APICBASE_ENABLE | X2APIC_ENABLE))
		static_call(kvm_x86_set_virtual_apic_mode)(vcpu);

	apic->base_address = apic->vcpu->arch.apic_base &
			     MSR_IA32_APICBASE_BASE;

	if ((value & MSR_IA32_APICBASE_ENABLE) &&
	     apic->base_address != APIC_DEFAULT_PHYS_BASE)
		pr_warn_once("APIC base relocation is unsupported by KVM");
}

void kvm_apic_update_apicv(struct kvm_vcpu *vcpu)
{
	struct kvm_lapic *apic = vcpu->arch.apic;

	if (vcpu->arch.apicv_active) {
		/* irr_pending is always true when apicv is activated. */
		apic->irr_pending = true;
		apic->isr_count = 1;
	} else {
		/*
		 * Don't clear irr_pending, searching the IRR can race with
		 * updates from the CPU as APICv is still active from hardware's
		 * perspective.  The flag will be cleared as appropriate when
		 * KVM injects the interrupt.
		 */
		apic->isr_count = count_vectors(apic->regs + APIC_ISR);
	}
}
EXPORT_SYMBOL_GPL(kvm_apic_update_apicv);

void kvm_lapic_reset(struct kvm_vcpu *vcpu, bool init_event)
{
	struct kvm_lapic *apic = vcpu->arch.apic;
	u64 msr_val;
	int i;

	if (!init_event) {
		msr_val = APIC_DEFAULT_PHYS_BASE | MSR_IA32_APICBASE_ENABLE;
		if (kvm_vcpu_is_reset_bsp(vcpu))
			msr_val |= MSR_IA32_APICBASE_BSP;
		kvm_lapic_set_base(vcpu, msr_val);
	}

	if (!apic)
		return;

	/* Stop the timer in case it's a reset to an active apic */
	hrtimer_cancel(&apic->lapic_timer.timer);

	/* The xAPIC ID is set at RESET even if the APIC was already enabled. */
	if (!init_event)
		kvm_apic_set_xapic_id(apic, vcpu->vcpu_id);
	kvm_apic_set_version(apic->vcpu);

	for (i = 0; i < KVM_APIC_LVT_NUM; i++)
		kvm_lapic_set_reg(apic, APIC_LVTT + 0x10 * i, APIC_LVT_MASKED);
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
	kvm_lapic_set_reg(apic, APIC_ICR, 0);
	kvm_lapic_set_reg(apic, APIC_ICR2, 0);
	kvm_lapic_set_reg(apic, APIC_TDCR, 0);
	kvm_lapic_set_reg(apic, APIC_TMICT, 0);
	for (i = 0; i < 8; i++) {
		kvm_lapic_set_reg(apic, APIC_IRR + 0x10 * i, 0);
		kvm_lapic_set_reg(apic, APIC_ISR + 0x10 * i, 0);
		kvm_lapic_set_reg(apic, APIC_TMR + 0x10 * i, 0);
	}
	kvm_apic_update_apicv(vcpu);
	apic->highest_isr_cache = -1;
	update_divide_count(apic);
	atomic_set(&apic->lapic_timer.pending, 0);

	vcpu->arch.pv_eoi.msr_val = 0;
	apic_update_ppr(apic);
	if (vcpu->arch.apicv_active) {
		static_call(kvm_x86_apicv_post_state_restore)(vcpu);
		static_call(kvm_x86_hwapic_irr_update)(vcpu, -1);
		static_call(kvm_x86_hwapic_isr_update)(vcpu, -1);
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

	if (kvm_apic_hw_enabled(apic) && !(reg & APIC_LVT_MASKED)) {
		vector = reg & APIC_VECTOR_MASK;
		mode = reg & APIC_MODE_MASK;
		trig_mode = reg & APIC_LVT_LEVEL_TRIGGER;
		return __apic_accept_irq(apic, mode, vector, 1, trig_mode,
					NULL);
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

int kvm_create_lapic(struct kvm_vcpu *vcpu, int timer_advance_ns)
{
	struct kvm_lapic *apic;

	ASSERT(vcpu != NULL);

	apic = kzalloc(sizeof(*apic), GFP_KERNEL_ACCOUNT);
	if (!apic)
		goto nomem;

	vcpu->arch.apic = apic;

	apic->regs = (void *)get_zeroed_page(GFP_KERNEL_ACCOUNT);
	if (!apic->regs) {
		printk(KERN_ERR "malloc apic regs error for vcpu %x\n",
		       vcpu->vcpu_id);
		goto nomem_free_apic;
	}
	apic->vcpu = vcpu;

	hrtimer_init(&apic->lapic_timer.timer, CLOCK_MONOTONIC,
		     HRTIMER_MODE_ABS_HARD);
	apic->lapic_timer.timer.function = apic_timer_fn;
	if (timer_advance_ns == -1) {
		apic->lapic_timer.timer_advance_ns = LAPIC_TIMER_ADVANCE_NS_INIT;
		lapic_timer_advance_dynamic = true;
	} else {
		apic->lapic_timer.timer_advance_ns = timer_advance_ns;
		lapic_timer_advance_dynamic = false;
	}

	/*
	 * Stuff the APIC ENABLE bit in lieu of temporarily incrementing
	 * apic_hw_disabled; the full RESET value is set by kvm_lapic_reset().
	 */
	vcpu->arch.apic_base = MSR_IA32_APICBASE_ENABLE;
	static_branch_inc(&apic_sw_disabled.key); /* sw disabled at reset */
	kvm_iodevice_init(&apic->dev, &apic_mmio_ops);

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

	__apic_update_ppr(apic, &ppr);
	return apic_has_interrupt_for_ppr(apic, ppr);
}
EXPORT_SYMBOL_GPL(kvm_apic_has_interrupt);

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

int kvm_get_apic_interrupt(struct kvm_vcpu *vcpu)
{
	int vector = kvm_apic_has_interrupt(vcpu);
	struct kvm_lapic *apic = vcpu->arch.apic;
	u32 ppr;

	if (vector == -1)
		return -1;

	/*
	 * We get here even with APIC virtualization enabled, if doing
	 * nested virtualization and L1 runs with the "acknowledge interrupt
	 * on exit" mode.  Then we cannot inject the interrupt via RVI,
	 * because the process would deliver it through the IDT.
	 */

	apic_clear_irr(vector, apic);
	if (to_hv_vcpu(vcpu) && test_bit(vector, to_hv_synic(vcpu)->auto_eoi_bitmap)) {
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

	return vector;
}

static int kvm_apic_state_fixup(struct kvm_vcpu *vcpu,
		struct kvm_lapic_state *s, bool set)
{
	if (apic_x2apic_mode(vcpu->arch.apic)) {
		u32 *id = (u32 *)(s->regs + APIC_ID);
		u32 *ldr = (u32 *)(s->regs + APIC_LDR);

		if (vcpu->kvm->arch.x2apic_format) {
			if (*id != vcpu->vcpu_id)
				return -EINVAL;
		} else {
			if (set)
				*id >>= 24;
			else
				*id <<= 24;
		}

		/* In x2APIC mode, the LDR is fixed and based on the id */
		if (set)
			*ldr = kvm_apic_calc_x2apic_ldr(*id);
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
	__kvm_lapic_set_reg(s->regs, APIC_TMCCT,
			    __apic_read(vcpu->arch.apic, APIC_TMCCT));

	return kvm_apic_state_fixup(vcpu, s, false);
}

int kvm_apic_set_state(struct kvm_vcpu *vcpu, struct kvm_lapic_state *s)
{
	struct kvm_lapic *apic = vcpu->arch.apic;
	int r;

	kvm_lapic_set_base(vcpu, vcpu->arch.apic_base);
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
	apic->highest_isr_cache = -1;
	if (vcpu->arch.apicv_active) {
		static_call(kvm_x86_apicv_post_state_restore)(vcpu);
		static_call(kvm_x86_hwapic_irr_update)(vcpu,
				apic_find_highest_irr(apic));
		static_call(kvm_x86_hwapic_isr_update)(vcpu,
				apic_find_highest_isr(apic));
	}
	kvm_make_request(KVM_REQ_EVENT, vcpu);
	if (ioapic_in_kernel(vcpu->kvm))
		kvm_rtc_eoi_tracking_restore_one(vcpu);

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

int kvm_x2apic_msr_write(struct kvm_vcpu *vcpu, u32 msr, u64 data)
{
	struct kvm_lapic *apic = vcpu->arch.apic;
	u32 reg = (msr - APIC_BASE_MSR) << 4;

	if (!lapic_in_kernel(vcpu) || !apic_x2apic_mode(apic))
		return 1;

	if (reg == APIC_ICR2)
		return 1;

	/* if this is ICR write vector before command */
	if (reg == APIC_ICR)
		kvm_lapic_reg_write(apic, APIC_ICR2, (u32)(data >> 32));
	return kvm_lapic_reg_write(apic, reg, (u32)data);
}

int kvm_x2apic_msr_read(struct kvm_vcpu *vcpu, u32 msr, u64 *data)
{
	struct kvm_lapic *apic = vcpu->arch.apic;
	u32 reg = (msr - APIC_BASE_MSR) << 4, low, high = 0;

	if (!lapic_in_kernel(vcpu) || !apic_x2apic_mode(apic))
		return 1;

	if (reg == APIC_DFR || reg == APIC_ICR2)
		return 1;

	if (kvm_lapic_reg_read(apic, reg, 4, &low))
		return 1;
	if (reg == APIC_ICR)
		kvm_lapic_reg_read(apic, APIC_ICR2, 4, &high);

	*data = (((u64)high) << 32) | low;

	return 0;
}

int kvm_hv_vapic_msr_write(struct kvm_vcpu *vcpu, u32 reg, u64 data)
{
	struct kvm_lapic *apic = vcpu->arch.apic;

	if (!lapic_in_kernel(vcpu))
		return 1;

	/* if this is ICR write vector before command */
	if (reg == APIC_ICR)
		kvm_lapic_reg_write(apic, APIC_ICR2, (u32)(data >> 32));
	return kvm_lapic_reg_write(apic, reg, (u32)data);
}

int kvm_hv_vapic_msr_read(struct kvm_vcpu *vcpu, u32 reg, u64 *data)
{
	struct kvm_lapic *apic = vcpu->arch.apic;
	u32 low, high = 0;

	if (!lapic_in_kernel(vcpu))
		return 1;

	if (kvm_lapic_reg_read(apic, reg, 4, &low))
		return 1;
	if (reg == APIC_ICR)
		kvm_lapic_reg_read(apic, APIC_ICR2, 4, &high);

	*data = (((u64)high) << 32) | low;

	return 0;
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
	unsigned long pe;

	if (!lapic_in_kernel(vcpu))
		return 0;

	/*
	 * Read pending events before calling the check_events
	 * callback.
	 */
	pe = smp_load_acquire(&apic->pending_events);
	if (!pe)
		return 0;

	if (is_guest_mode(vcpu)) {
		r = kvm_check_nested_events(vcpu);
		if (r < 0)
			return r == -EBUSY ? 0 : r;
		/*
		 * If an event has happened and caused a vmexit,
		 * we know INITs are latched and therefore
		 * we will not incorrectly deliver an APIC
		 * event instead of a vmexit.
		 */
	}

	/*
	 * INITs are latched while CPU is in specific states
	 * (SMM, VMX root mode, SVM with GIF=0).
	 * Because a CPU cannot be in these states immediately
	 * after it has processed an INIT signal (and thus in
	 * KVM_MP_STATE_INIT_RECEIVED state), just eat SIPIs
	 * and leave the INIT pending.
	 */
	if (kvm_vcpu_latch_init(vcpu)) {
		WARN_ON_ONCE(vcpu->arch.mp_state == KVM_MP_STATE_INIT_RECEIVED);
		if (test_bit(KVM_APIC_SIPI, &pe))
			clear_bit(KVM_APIC_SIPI, &apic->pending_events);
		return 0;
	}

	if (test_bit(KVM_APIC_INIT, &pe)) {
		clear_bit(KVM_APIC_INIT, &apic->pending_events);
		kvm_vcpu_reset(vcpu, true);
		if (kvm_vcpu_is_bsp(apic->vcpu))
			vcpu->arch.mp_state = KVM_MP_STATE_RUNNABLE;
		else
			vcpu->arch.mp_state = KVM_MP_STATE_INIT_RECEIVED;
	}
	if (test_bit(KVM_APIC_SIPI, &pe)) {
		clear_bit(KVM_APIC_SIPI, &apic->pending_events);
		if (vcpu->arch.mp_state == KVM_MP_STATE_INIT_RECEIVED) {
			/* evaluate pending_events before reading the vector */
			smp_rmb();
			sipi_vector = apic->sipi_vector;
			static_call(kvm_x86_vcpu_deliver_sipi_vector)(vcpu, sipi_vector);
			vcpu->arch.mp_state = KVM_MP_STATE_RUNNABLE;
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
