
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
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 */

#include <linux/kvm_host.h>
#include <linux/kvm.h>
#include <linux/mm.h>
#include <linux/highmem.h>
#include <linux/smp.h>
#include <linux/hrtimer.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/math64.h>
#include <linux/slab.h>
#include <asm/processor.h>
#include <asm/msr.h>
#include <asm/page.h>
#include <asm/current.h>
#include <asm/apicdef.h>
#include <linux/atomic.h>
#include "kvm_cache_regs.h"
#include "irq.h"
#include "trace.h"
#include "x86.h"
#include "cpuid.h"

#ifndef CONFIG_X86_64
#define mod_64(x, y) ((x) - (y) * div64_u64(x, y))
#else
#define mod_64(x, y) ((x) % (y))
#endif

#define PRId64 "d"
#define PRIx64 "llx"
#define PRIu64 "u"
#define PRIo64 "o"

#define APIC_BUS_CYCLE_NS 1

/* #define apic_debug(fmt,arg...) printk(KERN_WARNING fmt,##arg) */
#define apic_debug(fmt, arg...)

#define APIC_LVT_NUM			6
/* 14 is the version for Xeon and Pentium 8.4.8*/
#define APIC_VERSION			(0x14UL | ((APIC_LVT_NUM - 1) << 16))
#define LAPIC_MMIO_LENGTH		(1 << 12)
/* followed define is not in apicdef.h */
#define APIC_SHORT_MASK			0xc0000
#define APIC_DEST_NOSHORT		0x0
#define APIC_DEST_MASK			0x800
#define MAX_APIC_VECTOR			256

#define VEC_POS(v) ((v) & (32 - 1))
#define REG_POS(v) (((v) >> 5) << 4)

static unsigned int min_timer_period_us = 500;
module_param(min_timer_period_us, uint, S_IRUGO | S_IWUSR);

static inline u32 apic_get_reg(struct kvm_lapic *apic, int reg_off)
{
	return *((u32 *) (apic->regs + reg_off));
}

static inline void apic_set_reg(struct kvm_lapic *apic, int reg_off, u32 val)
{
	*((u32 *) (apic->regs + reg_off)) = val;
}

static inline int apic_test_and_set_vector(int vec, void *bitmap)
{
	return test_and_set_bit(VEC_POS(vec), (bitmap) + REG_POS(vec));
}

static inline int apic_test_and_clear_vector(int vec, void *bitmap)
{
	return test_and_clear_bit(VEC_POS(vec), (bitmap) + REG_POS(vec));
}

static inline void apic_set_vector(int vec, void *bitmap)
{
	set_bit(VEC_POS(vec), (bitmap) + REG_POS(vec));
}

static inline void apic_clear_vector(int vec, void *bitmap)
{
	clear_bit(VEC_POS(vec), (bitmap) + REG_POS(vec));
}

static inline int apic_hw_enabled(struct kvm_lapic *apic)
{
	return (apic)->vcpu->arch.apic_base & MSR_IA32_APICBASE_ENABLE;
}

static inline int  apic_sw_enabled(struct kvm_lapic *apic)
{
	return apic_get_reg(apic, APIC_SPIV) & APIC_SPIV_APIC_ENABLED;
}

static inline int apic_enabled(struct kvm_lapic *apic)
{
	return apic_sw_enabled(apic) &&	apic_hw_enabled(apic);
}

#define LVT_MASK	\
	(APIC_LVT_MASKED | APIC_SEND_PENDING | APIC_VECTOR_MASK)

#define LINT_MASK	\
	(LVT_MASK | APIC_MODE_MASK | APIC_INPUT_POLARITY | \
	 APIC_LVT_REMOTE_IRR | APIC_LVT_LEVEL_TRIGGER)

static inline int kvm_apic_id(struct kvm_lapic *apic)
{
	return (apic_get_reg(apic, APIC_ID) >> 24) & 0xff;
}

static inline int apic_lvt_enabled(struct kvm_lapic *apic, int lvt_type)
{
	return !(apic_get_reg(apic, lvt_type) & APIC_LVT_MASKED);
}

static inline int apic_lvt_vector(struct kvm_lapic *apic, int lvt_type)
{
	return apic_get_reg(apic, lvt_type) & APIC_VECTOR_MASK;
}

static inline int apic_lvtt_oneshot(struct kvm_lapic *apic)
{
	return ((apic_get_reg(apic, APIC_LVTT) &
		apic->lapic_timer.timer_mode_mask) == APIC_LVT_TIMER_ONESHOT);
}

static inline int apic_lvtt_period(struct kvm_lapic *apic)
{
	return ((apic_get_reg(apic, APIC_LVTT) &
		apic->lapic_timer.timer_mode_mask) == APIC_LVT_TIMER_PERIODIC);
}

static inline int apic_lvtt_tscdeadline(struct kvm_lapic *apic)
{
	return ((apic_get_reg(apic, APIC_LVTT) &
		apic->lapic_timer.timer_mode_mask) ==
			APIC_LVT_TIMER_TSCDEADLINE);
}

static inline int apic_lvt_nmi_mode(u32 lvt_val)
{
	return (lvt_val & (APIC_MODE_MASK | APIC_LVT_MASKED)) == APIC_DM_NMI;
}

void kvm_apic_set_version(struct kvm_vcpu *vcpu)
{
	struct kvm_lapic *apic = vcpu->arch.apic;
	struct kvm_cpuid_entry2 *feat;
	u32 v = APIC_VERSION;

	if (!irqchip_in_kernel(vcpu->kvm))
		return;

	feat = kvm_find_cpuid_entry(apic->vcpu, 0x1, 0);
	if (feat && (feat->ecx & (1 << (X86_FEATURE_X2APIC & 31))))
		v |= APIC_LVR_DIRECTED_EOI;
	apic_set_reg(apic, APIC_LVR, v);
}

static inline int apic_x2apic_mode(struct kvm_lapic *apic)
{
	return apic->vcpu->arch.apic_base & X2APIC_ENABLE;
}

static unsigned int apic_lvt_mask[APIC_LVT_NUM] = {
	LVT_MASK ,      /* part LVTT mask, timer mode mask added at runtime */
	LVT_MASK | APIC_MODE_MASK,	/* LVTTHMR */
	LVT_MASK | APIC_MODE_MASK,	/* LVTPC */
	LINT_MASK, LINT_MASK,	/* LVT0-1 */
	LVT_MASK		/* LVTERR */
};

static int find_highest_vector(void *bitmap)
{
	u32 *word = bitmap;
	int word_offset = MAX_APIC_VECTOR >> 5;

	while ((word_offset != 0) && (word[(--word_offset) << 2] == 0))
		continue;

	if (likely(!word_offset && !word[0]))
		return -1;
	else
		return fls(word[word_offset << 2]) - 1 + (word_offset << 5);
}

static inline int apic_test_and_set_irr(int vec, struct kvm_lapic *apic)
{
	apic->irr_pending = true;
	return apic_test_and_set_vector(vec, apic->regs + APIC_IRR);
}

static inline int apic_search_irr(struct kvm_lapic *apic)
{
	return find_highest_vector(apic->regs + APIC_IRR);
}

static inline int apic_find_highest_irr(struct kvm_lapic *apic)
{
	int result;

	if (!apic->irr_pending)
		return -1;

	result = apic_search_irr(apic);
	ASSERT(result == -1 || result >= 16);

	return result;
}

static inline void apic_clear_irr(int vec, struct kvm_lapic *apic)
{
	apic->irr_pending = false;
	apic_clear_vector(vec, apic->regs + APIC_IRR);
	if (apic_search_irr(apic) != -1)
		apic->irr_pending = true;
}

int kvm_lapic_find_highest_irr(struct kvm_vcpu *vcpu)
{
	struct kvm_lapic *apic = vcpu->arch.apic;
	int highest_irr;

	/* This may race with setting of irr in __apic_accept_irq() and
	 * value returned may be wrong, but kvm_vcpu_kick() in __apic_accept_irq
	 * will cause vmexit immediately and the value will be recalculated
	 * on the next vmentry.
	 */
	if (!apic)
		return 0;
	highest_irr = apic_find_highest_irr(apic);

	return highest_irr;
}

static int __apic_accept_irq(struct kvm_lapic *apic, int delivery_mode,
			     int vector, int level, int trig_mode);

int kvm_apic_set_irq(struct kvm_vcpu *vcpu, struct kvm_lapic_irq *irq)
{
	struct kvm_lapic *apic = vcpu->arch.apic;

	return __apic_accept_irq(apic, irq->delivery_mode, irq->vector,
			irq->level, irq->trig_mode);
}

static inline int apic_find_highest_isr(struct kvm_lapic *apic)
{
	int result;

	result = find_highest_vector(apic->regs + APIC_ISR);
	ASSERT(result == -1 || result >= 16);

	return result;
}

static void apic_update_ppr(struct kvm_lapic *apic)
{
	u32 tpr, isrv, ppr, old_ppr;
	int isr;

	old_ppr = apic_get_reg(apic, APIC_PROCPRI);
	tpr = apic_get_reg(apic, APIC_TASKPRI);
	isr = apic_find_highest_isr(apic);
	isrv = (isr != -1) ? isr : 0;

	if ((tpr & 0xf0) >= (isrv & 0xf0))
		ppr = tpr & 0xff;
	else
		ppr = isrv & 0xf0;

	apic_debug("vlapic %p, ppr 0x%x, isr 0x%x, isrv 0x%x",
		   apic, ppr, isr, isrv);

	if (old_ppr != ppr) {
		apic_set_reg(apic, APIC_PROCPRI, ppr);
		if (ppr < old_ppr)
			kvm_make_request(KVM_REQ_EVENT, apic->vcpu);
	}
}

static void apic_set_tpr(struct kvm_lapic *apic, u32 tpr)
{
	apic_set_reg(apic, APIC_TASKPRI, tpr);
	apic_update_ppr(apic);
}

int kvm_apic_match_physical_addr(struct kvm_lapic *apic, u16 dest)
{
	return dest == 0xff || kvm_apic_id(apic) == dest;
}

int kvm_apic_match_logical_addr(struct kvm_lapic *apic, u8 mda)
{
	int result = 0;
	u32 logical_id;

	if (apic_x2apic_mode(apic)) {
		logical_id = apic_get_reg(apic, APIC_LDR);
		return logical_id & mda;
	}

	logical_id = GET_APIC_LOGICAL_ID(apic_get_reg(apic, APIC_LDR));

	switch (apic_get_reg(apic, APIC_DFR)) {
	case APIC_DFR_FLAT:
		if (logical_id & mda)
			result = 1;
		break;
	case APIC_DFR_CLUSTER:
		if (((logical_id >> 4) == (mda >> 0x4))
		    && (logical_id & mda & 0xf))
			result = 1;
		break;
	default:
		apic_debug("Bad DFR vcpu %d: %08x\n",
			   apic->vcpu->vcpu_id, apic_get_reg(apic, APIC_DFR));
		break;
	}

	return result;
}

int kvm_apic_match_dest(struct kvm_vcpu *vcpu, struct kvm_lapic *source,
			   int short_hand, int dest, int dest_mode)
{
	int result = 0;
	struct kvm_lapic *target = vcpu->arch.apic;

	apic_debug("target %p, source %p, dest 0x%x, "
		   "dest_mode 0x%x, short_hand 0x%x\n",
		   target, source, dest, dest_mode, short_hand);

	ASSERT(target);
	switch (short_hand) {
	case APIC_DEST_NOSHORT:
		if (dest_mode == 0)
			/* Physical mode. */
			result = kvm_apic_match_physical_addr(target, dest);
		else
			/* Logical mode. */
			result = kvm_apic_match_logical_addr(target, dest);
		break;
	case APIC_DEST_SELF:
		result = (target == source);
		break;
	case APIC_DEST_ALLINC:
		result = 1;
		break;
	case APIC_DEST_ALLBUT:
		result = (target != source);
		break;
	default:
		apic_debug("kvm: apic: Bad dest shorthand value %x\n",
			   short_hand);
		break;
	}

	return result;
}

/*
 * Add a pending IRQ into lapic.
 * Return 1 if successfully added and 0 if discarded.
 */
static int __apic_accept_irq(struct kvm_lapic *apic, int delivery_mode,
			     int vector, int level, int trig_mode)
{
	int result = 0;
	struct kvm_vcpu *vcpu = apic->vcpu;

	switch (delivery_mode) {
	case APIC_DM_LOWEST:
		vcpu->arch.apic_arb_prio++;
	case APIC_DM_FIXED:
		/* FIXME add logic for vcpu on reset */
		if (unlikely(!apic_enabled(apic)))
			break;

		if (trig_mode) {
			apic_debug("level trig mode for vector %d", vector);
			apic_set_vector(vector, apic->regs + APIC_TMR);
		} else
			apic_clear_vector(vector, apic->regs + APIC_TMR);

		result = !apic_test_and_set_irr(vector, apic);
		trace_kvm_apic_accept_irq(vcpu->vcpu_id, delivery_mode,
					  trig_mode, vector, !result);
		if (!result) {
			if (trig_mode)
				apic_debug("level trig mode repeatedly for "
						"vector %d", vector);
			break;
		}

		kvm_make_request(KVM_REQ_EVENT, vcpu);
		kvm_vcpu_kick(vcpu);
		break;

	case APIC_DM_REMRD:
		apic_debug("Ignoring delivery mode 3\n");
		break;

	case APIC_DM_SMI:
		apic_debug("Ignoring guest SMI\n");
		break;

	case APIC_DM_NMI:
		result = 1;
		kvm_inject_nmi(vcpu);
		kvm_vcpu_kick(vcpu);
		break;

	case APIC_DM_INIT:
		if (level) {
			result = 1;
			vcpu->arch.mp_state = KVM_MP_STATE_INIT_RECEIVED;
			kvm_make_request(KVM_REQ_EVENT, vcpu);
			kvm_vcpu_kick(vcpu);
		} else {
			apic_debug("Ignoring de-assert INIT to vcpu %d\n",
				   vcpu->vcpu_id);
		}
		break;

	case APIC_DM_STARTUP:
		apic_debug("SIPI to vcpu %d vector 0x%02x\n",
			   vcpu->vcpu_id, vector);
		if (vcpu->arch.mp_state == KVM_MP_STATE_INIT_RECEIVED) {
			result = 1;
			vcpu->arch.sipi_vector = vector;
			vcpu->arch.mp_state = KVM_MP_STATE_SIPI_RECEIVED;
			kvm_make_request(KVM_REQ_EVENT, vcpu);
			kvm_vcpu_kick(vcpu);
		}
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

int kvm_apic_compare_prio(struct kvm_vcpu *vcpu1, struct kvm_vcpu *vcpu2)
{
	return vcpu1->arch.apic_arb_prio - vcpu2->arch.apic_arb_prio;
}

static void apic_set_eoi(struct kvm_lapic *apic)
{
	int vector = apic_find_highest_isr(apic);
	int trigger_mode;
	/*
	 * Not every write EOI will has corresponding ISR,
	 * one example is when Kernel check timer on setup_IO_APIC
	 */
	if (vector == -1)
		return;

	apic_clear_vector(vector, apic->regs + APIC_ISR);
	apic_update_ppr(apic);

	if (apic_test_and_clear_vector(vector, apic->regs + APIC_TMR))
		trigger_mode = IOAPIC_LEVEL_TRIG;
	else
		trigger_mode = IOAPIC_EDGE_TRIG;
	if (!(apic_get_reg(apic, APIC_SPIV) & APIC_SPIV_DIRECTED_EOI))
		kvm_ioapic_update_eoi(apic->vcpu->kvm, vector, trigger_mode);
	kvm_make_request(KVM_REQ_EVENT, apic->vcpu);
}

static void apic_send_ipi(struct kvm_lapic *apic)
{
	u32 icr_low = apic_get_reg(apic, APIC_ICR);
	u32 icr_high = apic_get_reg(apic, APIC_ICR2);
	struct kvm_lapic_irq irq;

	irq.vector = icr_low & APIC_VECTOR_MASK;
	irq.delivery_mode = icr_low & APIC_MODE_MASK;
	irq.dest_mode = icr_low & APIC_DEST_MASK;
	irq.level = icr_low & APIC_INT_ASSERT;
	irq.trig_mode = icr_low & APIC_INT_LEVELTRIG;
	irq.shorthand = icr_low & APIC_SHORT_MASK;
	if (apic_x2apic_mode(apic))
		irq.dest_id = icr_high;
	else
		irq.dest_id = GET_APIC_DEST_FIELD(icr_high);

	trace_kvm_apic_ipi(icr_low, irq.dest_id);

	apic_debug("icr_high 0x%x, icr_low 0x%x, "
		   "short_hand 0x%x, dest 0x%x, trig_mode 0x%x, level 0x%x, "
		   "dest_mode 0x%x, delivery_mode 0x%x, vector 0x%x\n",
		   icr_high, icr_low, irq.shorthand, irq.dest_id,
		   irq.trig_mode, irq.level, irq.dest_mode, irq.delivery_mode,
		   irq.vector);

	kvm_irq_delivery_to_apic(apic->vcpu->kvm, apic, &irq);
}

static u32 apic_get_tmcct(struct kvm_lapic *apic)
{
	ktime_t remaining;
	s64 ns;
	u32 tmcct;

	ASSERT(apic != NULL);

	/* if initial count is 0, current count should also be 0 */
	if (apic_get_reg(apic, APIC_TMICT) == 0)
		return 0;

	remaining = hrtimer_get_remaining(&apic->lapic_timer.timer);
	if (ktime_to_ns(remaining) < 0)
		remaining = ktime_set(0, 0);

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
	case APIC_ID:
		if (apic_x2apic_mode(apic))
			val = kvm_apic_id(apic);
		else
			val = kvm_apic_id(apic) << 24;
		break;
	case APIC_ARBPRI:
		apic_debug("Access APIC ARBPRI register which is for P6\n");
		break;

	case APIC_TMCCT:	/* Timer CCR */
		if (apic_lvtt_tscdeadline(apic))
			return 0;

		val = apic_get_tmcct(apic);
		break;

	case APIC_TASKPRI:
		report_tpr_access(apic, false);
		/* fall thru */
	default:
		apic_update_ppr(apic);
		val = apic_get_reg(apic, offset);
		break;
	}

	return val;
}

static inline struct kvm_lapic *to_lapic(struct kvm_io_device *dev)
{
	return container_of(dev, struct kvm_lapic, dev);
}

static int apic_reg_read(struct kvm_lapic *apic, u32 offset, int len,
		void *data)
{
	unsigned char alignment = offset & 0xf;
	u32 result;
	/* this bitmask has a bit cleared for each reserver register */
	static const u64 rmask = 0x43ff01ffffffe70cULL;

	if ((alignment + len) > 4) {
		apic_debug("KVM_APIC_READ: alignment error %x %d\n",
			   offset, len);
		return 1;
	}

	if (offset > 0x3f0 || !(rmask & (1ULL << (offset >> 4)))) {
		apic_debug("KVM_APIC_READ: read reserved register %x\n",
			   offset);
		return 1;
	}

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
	return apic_hw_enabled(apic) &&
	    addr >= apic->base_address &&
	    addr < apic->base_address + LAPIC_MMIO_LENGTH;
}

static int apic_mmio_read(struct kvm_io_device *this,
			   gpa_t address, int len, void *data)
{
	struct kvm_lapic *apic = to_lapic(this);
	u32 offset = address - apic->base_address;

	if (!apic_mmio_in_range(apic, address))
		return -EOPNOTSUPP;

	apic_reg_read(apic, offset, len, data);

	return 0;
}

static void update_divide_count(struct kvm_lapic *apic)
{
	u32 tmp1, tmp2, tdcr;

	tdcr = apic_get_reg(apic, APIC_TDCR);
	tmp1 = tdcr & 0xf;
	tmp2 = ((tmp1 & 0x3) | ((tmp1 & 0x8) >> 1)) + 1;
	apic->divide_count = 0x1 << (tmp2 & 0x7);

	apic_debug("timer divide count is 0x%x\n",
				   apic->divide_count);
}

static void start_apic_timer(struct kvm_lapic *apic)
{
	ktime_t now;
	atomic_set(&apic->lapic_timer.pending, 0);

	if (apic_lvtt_period(apic) || apic_lvtt_oneshot(apic)) {
		/* lapic timer in oneshot or peroidic mode */
		now = apic->lapic_timer.timer.base->get_time();
		apic->lapic_timer.period = (u64)apic_get_reg(apic, APIC_TMICT)
			    * APIC_BUS_CYCLE_NS * apic->divide_count;

		if (!apic->lapic_timer.period)
			return;
		/*
		 * Do not allow the guest to program periodic timers with small
		 * interval, since the hrtimers are not throttled by the host
		 * scheduler.
		 */
		if (apic_lvtt_period(apic)) {
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

		hrtimer_start(&apic->lapic_timer.timer,
			      ktime_add_ns(now, apic->lapic_timer.period),
			      HRTIMER_MODE_ABS);

		apic_debug("%s: bus cycle is %" PRId64 "ns, now 0x%016"
			   PRIx64 ", "
			   "timer initial count 0x%x, period %lldns, "
			   "expire @ 0x%016" PRIx64 ".\n", __func__,
			   APIC_BUS_CYCLE_NS, ktime_to_ns(now),
			   apic_get_reg(apic, APIC_TMICT),
			   apic->lapic_timer.period,
			   ktime_to_ns(ktime_add_ns(now,
					apic->lapic_timer.period)));
	} else if (apic_lvtt_tscdeadline(apic)) {
		/* lapic timer in tsc deadline mode */
		u64 guest_tsc, tscdeadline = apic->lapic_timer.tscdeadline;
		u64 ns = 0;
		struct kvm_vcpu *vcpu = apic->vcpu;
		unsigned long this_tsc_khz = vcpu_tsc_khz(vcpu);
		unsigned long flags;

		if (unlikely(!tscdeadline || !this_tsc_khz))
			return;

		local_irq_save(flags);

		now = apic->lapic_timer.timer.base->get_time();
		guest_tsc = kvm_x86_ops->read_l1_tsc(vcpu);
		if (likely(tscdeadline > guest_tsc)) {
			ns = (tscdeadline - guest_tsc) * 1000000ULL;
			do_div(ns, this_tsc_khz);
		}
		hrtimer_start(&apic->lapic_timer.timer,
			ktime_add_ns(now, ns), HRTIMER_MODE_ABS);

		local_irq_restore(flags);
	}
}

static void apic_manage_nmi_watchdog(struct kvm_lapic *apic, u32 lvt0_val)
{
	int nmi_wd_enabled = apic_lvt_nmi_mode(apic_get_reg(apic, APIC_LVT0));

	if (apic_lvt_nmi_mode(lvt0_val)) {
		if (!nmi_wd_enabled) {
			apic_debug("Receive NMI setting on APIC_LVT0 "
				   "for cpu %d\n", apic->vcpu->vcpu_id);
			apic->vcpu->kvm->arch.vapics_in_nmi_mode++;
		}
	} else if (nmi_wd_enabled)
		apic->vcpu->kvm->arch.vapics_in_nmi_mode--;
}

static int apic_reg_write(struct kvm_lapic *apic, u32 reg, u32 val)
{
	int ret = 0;

	trace_kvm_apic_write(reg, val);

	switch (reg) {
	case APIC_ID:		/* Local APIC ID */
		if (!apic_x2apic_mode(apic))
			apic_set_reg(apic, APIC_ID, val);
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
			apic_set_reg(apic, APIC_LDR, val & APIC_LDR_MASK);
		else
			ret = 1;
		break;

	case APIC_DFR:
		if (!apic_x2apic_mode(apic))
			apic_set_reg(apic, APIC_DFR, val | 0x0FFFFFFF);
		else
			ret = 1;
		break;

	case APIC_SPIV: {
		u32 mask = 0x3ff;
		if (apic_get_reg(apic, APIC_LVR) & APIC_LVR_DIRECTED_EOI)
			mask |= APIC_SPIV_DIRECTED_EOI;
		apic_set_reg(apic, APIC_SPIV, val & mask);
		if (!(val & APIC_SPIV_APIC_ENABLED)) {
			int i;
			u32 lvt_val;

			for (i = 0; i < APIC_LVT_NUM; i++) {
				lvt_val = apic_get_reg(apic,
						       APIC_LVTT + 0x10 * i);
				apic_set_reg(apic, APIC_LVTT + 0x10 * i,
					     lvt_val | APIC_LVT_MASKED);
			}
			atomic_set(&apic->lapic_timer.pending, 0);

		}
		break;
	}
	case APIC_ICR:
		/* No delay here, so we always clear the pending bit */
		apic_set_reg(apic, APIC_ICR, val & ~(1 << 12));
		apic_send_ipi(apic);
		break;

	case APIC_ICR2:
		if (!apic_x2apic_mode(apic))
			val &= 0xff000000;
		apic_set_reg(apic, APIC_ICR2, val);
		break;

	case APIC_LVT0:
		apic_manage_nmi_watchdog(apic, val);
	case APIC_LVTTHMR:
	case APIC_LVTPC:
	case APIC_LVT1:
	case APIC_LVTERR:
		/* TODO: Check vector */
		if (!apic_sw_enabled(apic))
			val |= APIC_LVT_MASKED;

		val &= apic_lvt_mask[(reg - APIC_LVTT) >> 4];
		apic_set_reg(apic, reg, val);

		break;

	case APIC_LVTT:
		if ((apic_get_reg(apic, APIC_LVTT) &
		    apic->lapic_timer.timer_mode_mask) !=
		   (val & apic->lapic_timer.timer_mode_mask))
			hrtimer_cancel(&apic->lapic_timer.timer);

		if (!apic_sw_enabled(apic))
			val |= APIC_LVT_MASKED;
		val &= (apic_lvt_mask[0] | apic->lapic_timer.timer_mode_mask);
		apic_set_reg(apic, APIC_LVTT, val);
		break;

	case APIC_TMICT:
		if (apic_lvtt_tscdeadline(apic))
			break;

		hrtimer_cancel(&apic->lapic_timer.timer);
		apic_set_reg(apic, APIC_TMICT, val);
		start_apic_timer(apic);
		break;

	case APIC_TDCR:
		if (val & 4)
			apic_debug("KVM_WRITE:TDCR %x\n", val);
		apic_set_reg(apic, APIC_TDCR, val);
		update_divide_count(apic);
		break;

	case APIC_ESR:
		if (apic_x2apic_mode(apic) && val != 0) {
			apic_debug("KVM_WRITE:ESR not zero %x\n", val);
			ret = 1;
		}
		break;

	case APIC_SELF_IPI:
		if (apic_x2apic_mode(apic)) {
			apic_reg_write(apic, APIC_ICR, 0x40000 | (val & 0xff));
		} else
			ret = 1;
		break;
	default:
		ret = 1;
		break;
	}
	if (ret)
		apic_debug("Local APIC Write to read-only register %x\n", reg);
	return ret;
}

static int apic_mmio_write(struct kvm_io_device *this,
			    gpa_t address, int len, const void *data)
{
	struct kvm_lapic *apic = to_lapic(this);
	unsigned int offset = address - apic->base_address;
	u32 val;

	if (!apic_mmio_in_range(apic, address))
		return -EOPNOTSUPP;

	/*
	 * APIC register must be aligned on 128-bits boundary.
	 * 32/64/128 bits registers must be accessed thru 32 bits.
	 * Refer SDM 8.4.1
	 */
	if (len != 4 || (offset & 0xf)) {
		/* Don't shout loud, $infamous_os would cause only noise. */
		apic_debug("apic write: bad size=%d %lx\n", len, (long)address);
		return 0;
	}

	val = *(u32*)data;

	/* too common printing */
	if (offset != APIC_EOI)
		apic_debug("%s: offset 0x%x with length 0x%x, and value is "
			   "0x%x\n", __func__, offset, len, val);

	apic_reg_write(apic, offset & 0xff0, val);

	return 0;
}

void kvm_lapic_set_eoi(struct kvm_vcpu *vcpu)
{
	struct kvm_lapic *apic = vcpu->arch.apic;

	if (apic)
		apic_reg_write(vcpu->arch.apic, APIC_EOI, 0);
}
EXPORT_SYMBOL_GPL(kvm_lapic_set_eoi);

void kvm_free_lapic(struct kvm_vcpu *vcpu)
{
	if (!vcpu->arch.apic)
		return;

	hrtimer_cancel(&vcpu->arch.apic->lapic_timer.timer);

	if (vcpu->arch.apic->regs)
		free_page((unsigned long)vcpu->arch.apic->regs);

	kfree(vcpu->arch.apic);
}

/*
 *----------------------------------------------------------------------
 * LAPIC interface
 *----------------------------------------------------------------------
 */

u64 kvm_get_lapic_tscdeadline_msr(struct kvm_vcpu *vcpu)
{
	struct kvm_lapic *apic = vcpu->arch.apic;
	if (!apic)
		return 0;

	if (apic_lvtt_oneshot(apic) || apic_lvtt_period(apic))
		return 0;

	return apic->lapic_timer.tscdeadline;
}

void kvm_set_lapic_tscdeadline_msr(struct kvm_vcpu *vcpu, u64 data)
{
	struct kvm_lapic *apic = vcpu->arch.apic;
	if (!apic)
		return;

	if (apic_lvtt_oneshot(apic) || apic_lvtt_period(apic))
		return;

	hrtimer_cancel(&apic->lapic_timer.timer);
	apic->lapic_timer.tscdeadline = data;
	start_apic_timer(apic);
}

void kvm_lapic_set_tpr(struct kvm_vcpu *vcpu, unsigned long cr8)
{
	struct kvm_lapic *apic = vcpu->arch.apic;

	if (!apic)
		return;
	apic_set_tpr(apic, ((cr8 & 0x0f) << 4)
		     | (apic_get_reg(apic, APIC_TASKPRI) & 4));
}

u64 kvm_lapic_get_cr8(struct kvm_vcpu *vcpu)
{
	struct kvm_lapic *apic = vcpu->arch.apic;
	u64 tpr;

	if (!apic)
		return 0;
	tpr = (u64) apic_get_reg(apic, APIC_TASKPRI);

	return (tpr & 0xf0) >> 4;
}

void kvm_lapic_set_base(struct kvm_vcpu *vcpu, u64 value)
{
	struct kvm_lapic *apic = vcpu->arch.apic;

	if (!apic) {
		value |= MSR_IA32_APICBASE_BSP;
		vcpu->arch.apic_base = value;
		return;
	}

	if (!kvm_vcpu_is_bsp(apic->vcpu))
		value &= ~MSR_IA32_APICBASE_BSP;

	vcpu->arch.apic_base = value;
	if (apic_x2apic_mode(apic)) {
		u32 id = kvm_apic_id(apic);
		u32 ldr = ((id & ~0xf) << 16) | (1 << (id & 0xf));
		apic_set_reg(apic, APIC_LDR, ldr);
	}
	apic->base_address = apic->vcpu->arch.apic_base &
			     MSR_IA32_APICBASE_BASE;

	/* with FSB delivery interrupt, we can restart APIC functionality */
	apic_debug("apic base msr is 0x%016" PRIx64 ", and base address is "
		   "0x%lx.\n", apic->vcpu->arch.apic_base, apic->base_address);

}

void kvm_lapic_reset(struct kvm_vcpu *vcpu)
{
	struct kvm_lapic *apic;
	int i;

	apic_debug("%s\n", __func__);

	ASSERT(vcpu);
	apic = vcpu->arch.apic;
	ASSERT(apic != NULL);

	/* Stop the timer in case it's a reset to an active apic */
	hrtimer_cancel(&apic->lapic_timer.timer);

	apic_set_reg(apic, APIC_ID, vcpu->vcpu_id << 24);
	kvm_apic_set_version(apic->vcpu);

	for (i = 0; i < APIC_LVT_NUM; i++)
		apic_set_reg(apic, APIC_LVTT + 0x10 * i, APIC_LVT_MASKED);
	apic_set_reg(apic, APIC_LVT0,
		     SET_APIC_DELIVERY_MODE(0, APIC_MODE_EXTINT));

	apic_set_reg(apic, APIC_DFR, 0xffffffffU);
	apic_set_reg(apic, APIC_SPIV, 0xff);
	apic_set_reg(apic, APIC_TASKPRI, 0);
	apic_set_reg(apic, APIC_LDR, 0);
	apic_set_reg(apic, APIC_ESR, 0);
	apic_set_reg(apic, APIC_ICR, 0);
	apic_set_reg(apic, APIC_ICR2, 0);
	apic_set_reg(apic, APIC_TDCR, 0);
	apic_set_reg(apic, APIC_TMICT, 0);
	for (i = 0; i < 8; i++) {
		apic_set_reg(apic, APIC_IRR + 0x10 * i, 0);
		apic_set_reg(apic, APIC_ISR + 0x10 * i, 0);
		apic_set_reg(apic, APIC_TMR + 0x10 * i, 0);
	}
	apic->irr_pending = false;
	update_divide_count(apic);
	atomic_set(&apic->lapic_timer.pending, 0);
	if (kvm_vcpu_is_bsp(vcpu))
		vcpu->arch.apic_base |= MSR_IA32_APICBASE_BSP;
	apic_update_ppr(apic);

	vcpu->arch.apic_arb_prio = 0;

	apic_debug(KERN_INFO "%s: vcpu=%p, id=%d, base_msr="
		   "0x%016" PRIx64 ", base_address=0x%0lx.\n", __func__,
		   vcpu, kvm_apic_id(apic),
		   vcpu->arch.apic_base, apic->base_address);
}

bool kvm_apic_present(struct kvm_vcpu *vcpu)
{
	return vcpu->arch.apic && apic_hw_enabled(vcpu->arch.apic);
}

int kvm_lapic_enabled(struct kvm_vcpu *vcpu)
{
	return kvm_apic_present(vcpu) && apic_sw_enabled(vcpu->arch.apic);
}

/*
 *----------------------------------------------------------------------
 * timer interface
 *----------------------------------------------------------------------
 */

static bool lapic_is_periodic(struct kvm_timer *ktimer)
{
	struct kvm_lapic *apic = container_of(ktimer, struct kvm_lapic,
					      lapic_timer);
	return apic_lvtt_period(apic);
}

int apic_has_pending_timer(struct kvm_vcpu *vcpu)
{
	struct kvm_lapic *lapic = vcpu->arch.apic;

	if (lapic && apic_enabled(lapic) && apic_lvt_enabled(lapic, APIC_LVTT))
		return atomic_read(&lapic->lapic_timer.pending);

	return 0;
}

static int kvm_apic_local_deliver(struct kvm_lapic *apic, int lvt_type)
{
	u32 reg = apic_get_reg(apic, lvt_type);
	int vector, mode, trig_mode;

	if (apic_hw_enabled(apic) && !(reg & APIC_LVT_MASKED)) {
		vector = reg & APIC_VECTOR_MASK;
		mode = reg & APIC_MODE_MASK;
		trig_mode = reg & APIC_LVT_LEVEL_TRIGGER;
		return __apic_accept_irq(apic, mode, vector, 1, trig_mode);
	}
	return 0;
}

void kvm_apic_nmi_wd_deliver(struct kvm_vcpu *vcpu)
{
	struct kvm_lapic *apic = vcpu->arch.apic;

	if (apic)
		kvm_apic_local_deliver(apic, APIC_LVT0);
}

static struct kvm_timer_ops lapic_timer_ops = {
	.is_periodic = lapic_is_periodic,
};

static const struct kvm_io_device_ops apic_mmio_ops = {
	.read     = apic_mmio_read,
	.write    = apic_mmio_write,
};

int kvm_create_lapic(struct kvm_vcpu *vcpu)
{
	struct kvm_lapic *apic;

	ASSERT(vcpu != NULL);
	apic_debug("apic_init %d\n", vcpu->vcpu_id);

	apic = kzalloc(sizeof(*apic), GFP_KERNEL);
	if (!apic)
		goto nomem;

	vcpu->arch.apic = apic;

	apic->regs = (void *)get_zeroed_page(GFP_KERNEL);
	if (!apic->regs) {
		printk(KERN_ERR "malloc apic regs error for vcpu %x\n",
		       vcpu->vcpu_id);
		goto nomem_free_apic;
	}
	apic->vcpu = vcpu;

	hrtimer_init(&apic->lapic_timer.timer, CLOCK_MONOTONIC,
		     HRTIMER_MODE_ABS);
	apic->lapic_timer.timer.function = kvm_timer_fn;
	apic->lapic_timer.t_ops = &lapic_timer_ops;
	apic->lapic_timer.kvm = vcpu->kvm;
	apic->lapic_timer.vcpu = vcpu;

	apic->base_address = APIC_DEFAULT_PHYS_BASE;
	vcpu->arch.apic_base = APIC_DEFAULT_PHYS_BASE;

	kvm_lapic_reset(vcpu);
	kvm_iodevice_init(&apic->dev, &apic_mmio_ops);

	return 0;
nomem_free_apic:
	kfree(apic);
nomem:
	return -ENOMEM;
}

int kvm_apic_has_interrupt(struct kvm_vcpu *vcpu)
{
	struct kvm_lapic *apic = vcpu->arch.apic;
	int highest_irr;

	if (!apic || !apic_enabled(apic))
		return -1;

	apic_update_ppr(apic);
	highest_irr = apic_find_highest_irr(apic);
	if ((highest_irr == -1) ||
	    ((highest_irr & 0xF0) <= apic_get_reg(apic, APIC_PROCPRI)))
		return -1;
	return highest_irr;
}

int kvm_apic_accept_pic_intr(struct kvm_vcpu *vcpu)
{
	u32 lvt0 = apic_get_reg(vcpu->arch.apic, APIC_LVT0);
	int r = 0;

	if (!apic_hw_enabled(vcpu->arch.apic))
		r = 1;
	if ((lvt0 & APIC_LVT_MASKED) == 0 &&
	    GET_APIC_DELIVERY_MODE(lvt0) == APIC_MODE_EXTINT)
		r = 1;
	return r;
}

void kvm_inject_apic_timer_irqs(struct kvm_vcpu *vcpu)
{
	struct kvm_lapic *apic = vcpu->arch.apic;

	if (apic && atomic_read(&apic->lapic_timer.pending) > 0) {
		if (kvm_apic_local_deliver(apic, APIC_LVTT))
			atomic_dec(&apic->lapic_timer.pending);
	}
}

int kvm_get_apic_interrupt(struct kvm_vcpu *vcpu)
{
	int vector = kvm_apic_has_interrupt(vcpu);
	struct kvm_lapic *apic = vcpu->arch.apic;

	if (vector == -1)
		return -1;

	apic_set_vector(vector, apic->regs + APIC_ISR);
	apic_update_ppr(apic);
	apic_clear_irr(vector, apic);
	return vector;
}

void kvm_apic_post_state_restore(struct kvm_vcpu *vcpu)
{
	struct kvm_lapic *apic = vcpu->arch.apic;

	apic->base_address = vcpu->arch.apic_base &
			     MSR_IA32_APICBASE_BASE;
	kvm_apic_set_version(vcpu);

	apic_update_ppr(apic);
	hrtimer_cancel(&apic->lapic_timer.timer);
	update_divide_count(apic);
	start_apic_timer(apic);
	apic->irr_pending = true;
	kvm_make_request(KVM_REQ_EVENT, vcpu);
}

void __kvm_migrate_apic_timer(struct kvm_vcpu *vcpu)
{
	struct kvm_lapic *apic = vcpu->arch.apic;
	struct hrtimer *timer;

	if (!apic)
		return;

	timer = &apic->lapic_timer.timer;
	if (hrtimer_cancel(timer))
		hrtimer_start_expires(timer, HRTIMER_MODE_ABS);
}

void kvm_lapic_sync_from_vapic(struct kvm_vcpu *vcpu)
{
	u32 data;
	void *vapic;

	if (!irqchip_in_kernel(vcpu->kvm) || !vcpu->arch.apic->vapic_addr)
		return;

	vapic = kmap_atomic(vcpu->arch.apic->vapic_page, KM_USER0);
	data = *(u32 *)(vapic + offset_in_page(vcpu->arch.apic->vapic_addr));
	kunmap_atomic(vapic, KM_USER0);

	apic_set_tpr(vcpu->arch.apic, data & 0xff);
}

void kvm_lapic_sync_to_vapic(struct kvm_vcpu *vcpu)
{
	u32 data, tpr;
	int max_irr, max_isr;
	struct kvm_lapic *apic;
	void *vapic;

	if (!irqchip_in_kernel(vcpu->kvm) || !vcpu->arch.apic->vapic_addr)
		return;

	apic = vcpu->arch.apic;
	tpr = apic_get_reg(apic, APIC_TASKPRI) & 0xff;
	max_irr = apic_find_highest_irr(apic);
	if (max_irr < 0)
		max_irr = 0;
	max_isr = apic_find_highest_isr(apic);
	if (max_isr < 0)
		max_isr = 0;
	data = (tpr & 0xff) | ((max_isr & 0xf0) << 8) | (max_irr << 24);

	vapic = kmap_atomic(vcpu->arch.apic->vapic_page, KM_USER0);
	*(u32 *)(vapic + offset_in_page(vcpu->arch.apic->vapic_addr)) = data;
	kunmap_atomic(vapic, KM_USER0);
}

void kvm_lapic_set_vapic_addr(struct kvm_vcpu *vcpu, gpa_t vapic_addr)
{
	if (!irqchip_in_kernel(vcpu->kvm))
		return;

	vcpu->arch.apic->vapic_addr = vapic_addr;
}

int kvm_x2apic_msr_write(struct kvm_vcpu *vcpu, u32 msr, u64 data)
{
	struct kvm_lapic *apic = vcpu->arch.apic;
	u32 reg = (msr - APIC_BASE_MSR) << 4;

	if (!irqchip_in_kernel(vcpu->kvm) || !apic_x2apic_mode(apic))
		return 1;

	/* if this is ICR write vector before command */
	if (msr == 0x830)
		apic_reg_write(apic, APIC_ICR2, (u32)(data >> 32));
	return apic_reg_write(apic, reg, (u32)data);
}

int kvm_x2apic_msr_read(struct kvm_vcpu *vcpu, u32 msr, u64 *data)
{
	struct kvm_lapic *apic = vcpu->arch.apic;
	u32 reg = (msr - APIC_BASE_MSR) << 4, low, high = 0;

	if (!irqchip_in_kernel(vcpu->kvm) || !apic_x2apic_mode(apic))
		return 1;

	if (apic_reg_read(apic, reg, 4, &low))
		return 1;
	if (msr == 0x830)
		apic_reg_read(apic, APIC_ICR2, 4, &high);

	*data = (((u64)high) << 32) | low;

	return 0;
}

int kvm_hv_vapic_msr_write(struct kvm_vcpu *vcpu, u32 reg, u64 data)
{
	struct kvm_lapic *apic = vcpu->arch.apic;

	if (!irqchip_in_kernel(vcpu->kvm))
		return 1;

	/* if this is ICR write vector before command */
	if (reg == APIC_ICR)
		apic_reg_write(apic, APIC_ICR2, (u32)(data >> 32));
	return apic_reg_write(apic, reg, (u32)data);
}

int kvm_hv_vapic_msr_read(struct kvm_vcpu *vcpu, u32 reg, u64 *data)
{
	struct kvm_lapic *apic = vcpu->arch.apic;
	u32 low, high = 0;

	if (!irqchip_in_kernel(vcpu->kvm))
		return 1;

	if (apic_reg_read(apic, reg, 4, &low))
		return 1;
	if (reg == APIC_ICR)
		apic_reg_read(apic, APIC_ICR2, 4, &high);

	*data = (((u64)high) << 32) | low;

	return 0;
}
