
/*
 * Local APIC virtualization
 *
 * Copyright (C) 2006 Qumranet, Inc.
 * Copyright (C) 2007 Novell
 * Copyright (C) 2007 Intel
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

#include "kvm.h"
#include <linux/kvm.h>
#include <linux/mm.h>
#include <linux/highmem.h>
#include <linux/smp.h>
#include <linux/hrtimer.h>
#include <linux/io.h>
#include <linux/module.h>
#include <asm/processor.h>
#include <asm/msr.h>
#include <asm/page.h>
#include <asm/current.h>
#include <asm/apicdef.h>
#include <asm/atomic.h>
#include <asm/div64.h>
#include "irq.h"

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
	return (apic)->vcpu->apic_base & MSR_IA32_APICBASE_ENABLE;
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

static inline int apic_lvtt_period(struct kvm_lapic *apic)
{
	return apic_get_reg(apic, APIC_LVTT) & APIC_LVT_TIMER_PERIODIC;
}

static unsigned int apic_lvt_mask[APIC_LVT_NUM] = {
	LVT_MASK | APIC_LVT_TIMER_PERIODIC,	/* LVTT */
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
	return apic_test_and_set_vector(vec, apic->regs + APIC_IRR);
}

static inline void apic_clear_irr(int vec, struct kvm_lapic *apic)
{
	apic_clear_vector(vec, apic->regs + APIC_IRR);
}

static inline int apic_find_highest_irr(struct kvm_lapic *apic)
{
	int result;

	result = find_highest_vector(apic->regs + APIC_IRR);
	ASSERT(result == -1 || result >= 16);

	return result;
}

int kvm_lapic_find_highest_irr(struct kvm_vcpu *vcpu)
{
	struct kvm_lapic *apic = (struct kvm_lapic *)vcpu->apic;
	int highest_irr;

	if (!apic)
		return 0;
	highest_irr = apic_find_highest_irr(apic);

	return highest_irr;
}
EXPORT_SYMBOL_GPL(kvm_lapic_find_highest_irr);

int kvm_apic_set_irq(struct kvm_lapic *apic, u8 vec, u8 trig)
{
	if (!apic_test_and_set_irr(vec, apic)) {
		/* a new pending irq is set in IRR */
		if (trig)
			apic_set_vector(vec, apic->regs + APIC_TMR);
		else
			apic_clear_vector(vec, apic->regs + APIC_TMR);
		kvm_vcpu_kick(apic->vcpu);
		return 1;
	}
	return 0;
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
	u32 tpr, isrv, ppr;
	int isr;

	tpr = apic_get_reg(apic, APIC_TASKPRI);
	isr = apic_find_highest_isr(apic);
	isrv = (isr != -1) ? isr : 0;

	if ((tpr & 0xf0) >= (isrv & 0xf0))
		ppr = tpr & 0xff;
	else
		ppr = isrv & 0xf0;

	apic_debug("vlapic %p, ppr 0x%x, isr 0x%x, isrv 0x%x",
		   apic, ppr, isr, isrv);

	apic_set_reg(apic, APIC_PROCPRI, ppr);
}

static void apic_set_tpr(struct kvm_lapic *apic, u32 tpr)
{
	apic_set_reg(apic, APIC_TASKPRI, tpr);
	apic_update_ppr(apic);
}

int kvm_apic_match_physical_addr(struct kvm_lapic *apic, u16 dest)
{
	return kvm_apic_id(apic) == dest;
}

int kvm_apic_match_logical_addr(struct kvm_lapic *apic, u8 mda)
{
	int result = 0;
	u8 logical_id;

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
		printk(KERN_WARNING "Bad DFR vcpu %d: %08x\n",
		       apic->vcpu->vcpu_id, apic_get_reg(apic, APIC_DFR));
		break;
	}

	return result;
}

static int apic_match_dest(struct kvm_vcpu *vcpu, struct kvm_lapic *source,
			   int short_hand, int dest, int dest_mode)
{
	int result = 0;
	struct kvm_lapic *target = vcpu->apic;

	apic_debug("target %p, source %p, dest 0x%x, "
		   "dest_mode 0x%x, short_hand 0x%x",
		   target, source, dest, dest_mode, short_hand);

	ASSERT(!target);
	switch (short_hand) {
	case APIC_DEST_NOSHORT:
		if (dest_mode == 0) {
			/* Physical mode. */
			if ((dest == 0xFF) || (dest == kvm_apic_id(target)))
				result = 1;
		} else
			/* Logical mode. */
			result = kvm_apic_match_logical_addr(target, dest);
		break;
	case APIC_DEST_SELF:
		if (target == source)
			result = 1;
		break;
	case APIC_DEST_ALLINC:
		result = 1;
		break;
	case APIC_DEST_ALLBUT:
		if (target != source)
			result = 1;
		break;
	default:
		printk(KERN_WARNING "Bad dest shorthand value %x\n",
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
	int orig_irr, result = 0;
	struct kvm_vcpu *vcpu = apic->vcpu;

	switch (delivery_mode) {
	case APIC_DM_FIXED:
	case APIC_DM_LOWEST:
		/* FIXME add logic for vcpu on reset */
		if (unlikely(!apic_enabled(apic)))
			break;

		orig_irr = apic_test_and_set_irr(vector, apic);
		if (orig_irr && trig_mode) {
			apic_debug("level trig mode repeatedly for vector %d",
				   vector);
			break;
		}

		if (trig_mode) {
			apic_debug("level trig mode for vector %d", vector);
			apic_set_vector(vector, apic->regs + APIC_TMR);
		} else
			apic_clear_vector(vector, apic->regs + APIC_TMR);

		if (vcpu->mp_state == VCPU_MP_STATE_RUNNABLE)
			kvm_vcpu_kick(vcpu);
		else if (vcpu->mp_state == VCPU_MP_STATE_HALTED) {
			vcpu->mp_state = VCPU_MP_STATE_RUNNABLE;
			if (waitqueue_active(&vcpu->wq))
				wake_up_interruptible(&vcpu->wq);
		}

		result = (orig_irr == 0);
		break;

	case APIC_DM_REMRD:
		printk(KERN_DEBUG "Ignoring delivery mode 3\n");
		break;

	case APIC_DM_SMI:
		printk(KERN_DEBUG "Ignoring guest SMI\n");
		break;
	case APIC_DM_NMI:
		printk(KERN_DEBUG "Ignoring guest NMI\n");
		break;

	case APIC_DM_INIT:
		if (level) {
			if (vcpu->mp_state == VCPU_MP_STATE_RUNNABLE)
				printk(KERN_DEBUG
				       "INIT on a runnable vcpu %d\n",
				       vcpu->vcpu_id);
			vcpu->mp_state = VCPU_MP_STATE_INIT_RECEIVED;
			kvm_vcpu_kick(vcpu);
		} else {
			printk(KERN_DEBUG
			       "Ignoring de-assert INIT to vcpu %d\n",
			       vcpu->vcpu_id);
		}

		break;

	case APIC_DM_STARTUP:
		printk(KERN_DEBUG "SIPI to vcpu %d vector 0x%02x\n",
		       vcpu->vcpu_id, vector);
		if (vcpu->mp_state == VCPU_MP_STATE_INIT_RECEIVED) {
			vcpu->sipi_vector = vector;
			vcpu->mp_state = VCPU_MP_STATE_SIPI_RECEIVED;
			if (waitqueue_active(&vcpu->wq))
				wake_up_interruptible(&vcpu->wq);
		}
		break;

	default:
		printk(KERN_ERR "TODO: unsupported delivery mode %x\n",
		       delivery_mode);
		break;
	}
	return result;
}

struct kvm_lapic *kvm_apic_round_robin(struct kvm *kvm, u8 vector,
				       unsigned long bitmap)
{
	int vcpu_id;
	int last;
	int next;
	struct kvm_lapic *apic;

	last = kvm->round_robin_prev_vcpu;
	next = last;

	do {
		if (++next == KVM_MAX_VCPUS)
			next = 0;
		if (kvm->vcpus[next] == NULL || !test_bit(next, &bitmap))
			continue;
		apic = kvm->vcpus[next]->apic;
		if (apic && apic_enabled(apic))
			break;
		apic = NULL;
	} while (next != last);
	kvm->round_robin_prev_vcpu = next;

	if (!apic) {
		vcpu_id = ffs(bitmap) - 1;
		if (vcpu_id < 0) {
			vcpu_id = 0;
			printk(KERN_DEBUG "vcpu not ready for apic_round_robin\n");
		}
		apic = kvm->vcpus[vcpu_id]->apic;
	}

	return apic;
}

static void apic_set_eoi(struct kvm_lapic *apic)
{
	int vector = apic_find_highest_isr(apic);

	/*
	 * Not every write EOI will has corresponding ISR,
	 * one example is when Kernel check timer on setup_IO_APIC
	 */
	if (vector == -1)
		return;

	apic_clear_vector(vector, apic->regs + APIC_ISR);
	apic_update_ppr(apic);

	if (apic_test_and_clear_vector(vector, apic->regs + APIC_TMR))
		kvm_ioapic_update_eoi(apic->vcpu->kvm, vector);
}

static void apic_send_ipi(struct kvm_lapic *apic)
{
	u32 icr_low = apic_get_reg(apic, APIC_ICR);
	u32 icr_high = apic_get_reg(apic, APIC_ICR2);

	unsigned int dest = GET_APIC_DEST_FIELD(icr_high);
	unsigned int short_hand = icr_low & APIC_SHORT_MASK;
	unsigned int trig_mode = icr_low & APIC_INT_LEVELTRIG;
	unsigned int level = icr_low & APIC_INT_ASSERT;
	unsigned int dest_mode = icr_low & APIC_DEST_MASK;
	unsigned int delivery_mode = icr_low & APIC_MODE_MASK;
	unsigned int vector = icr_low & APIC_VECTOR_MASK;

	struct kvm_lapic *target;
	struct kvm_vcpu *vcpu;
	unsigned long lpr_map = 0;
	int i;

	apic_debug("icr_high 0x%x, icr_low 0x%x, "
		   "short_hand 0x%x, dest 0x%x, trig_mode 0x%x, level 0x%x, "
		   "dest_mode 0x%x, delivery_mode 0x%x, vector 0x%x\n",
		   icr_high, icr_low, short_hand, dest,
		   trig_mode, level, dest_mode, delivery_mode, vector);

	for (i = 0; i < KVM_MAX_VCPUS; i++) {
		vcpu = apic->vcpu->kvm->vcpus[i];
		if (!vcpu)
			continue;

		if (vcpu->apic &&
		    apic_match_dest(vcpu, apic, short_hand, dest, dest_mode)) {
			if (delivery_mode == APIC_DM_LOWEST)
				set_bit(vcpu->vcpu_id, &lpr_map);
			else
				__apic_accept_irq(vcpu->apic, delivery_mode,
						  vector, level, trig_mode);
		}
	}

	if (delivery_mode == APIC_DM_LOWEST) {
		target = kvm_apic_round_robin(vcpu->kvm, vector, lpr_map);
		if (target != NULL)
			__apic_accept_irq(target, delivery_mode,
					  vector, level, trig_mode);
	}
}

static u32 apic_get_tmcct(struct kvm_lapic *apic)
{
	u32 counter_passed;
	ktime_t passed, now = apic->timer.dev.base->get_time();
	u32 tmcct = apic_get_reg(apic, APIC_TMICT);

	ASSERT(apic != NULL);

	if (unlikely(ktime_to_ns(now) <=
		ktime_to_ns(apic->timer.last_update))) {
		/* Wrap around */
		passed = ktime_add(( {
				    (ktime_t) {
				    .tv64 = KTIME_MAX -
				    (apic->timer.last_update).tv64}; }
				   ), now);
		apic_debug("time elapsed\n");
	} else
		passed = ktime_sub(now, apic->timer.last_update);

	counter_passed = div64_64(ktime_to_ns(passed),
				  (APIC_BUS_CYCLE_NS * apic->timer.divide_count));
	tmcct -= counter_passed;

	if (tmcct <= 0) {
		if (unlikely(!apic_lvtt_period(apic)))
			tmcct = 0;
		else
			do {
				tmcct += apic_get_reg(apic, APIC_TMICT);
			} while (tmcct <= 0);
	}

	return tmcct;
}

static u32 __apic_read(struct kvm_lapic *apic, unsigned int offset)
{
	u32 val = 0;

	if (offset >= LAPIC_MMIO_LENGTH)
		return 0;

	switch (offset) {
	case APIC_ARBPRI:
		printk(KERN_WARNING "Access APIC ARBPRI register "
		       "which is for P6\n");
		break;

	case APIC_TMCCT:	/* Timer CCR */
		val = apic_get_tmcct(apic);
		break;

	default:
		apic_update_ppr(apic);
		val = apic_get_reg(apic, offset);
		break;
	}

	return val;
}

static void apic_mmio_read(struct kvm_io_device *this,
			   gpa_t address, int len, void *data)
{
	struct kvm_lapic *apic = (struct kvm_lapic *)this->private;
	unsigned int offset = address - apic->base_address;
	unsigned char alignment = offset & 0xf;
	u32 result;

	if ((alignment + len) > 4) {
		printk(KERN_ERR "KVM_APIC_READ: alignment error %lx %d",
		       (unsigned long)address, len);
		return;
	}
	result = __apic_read(apic, offset & ~0xf);

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
}

static void update_divide_count(struct kvm_lapic *apic)
{
	u32 tmp1, tmp2, tdcr;

	tdcr = apic_get_reg(apic, APIC_TDCR);
	tmp1 = tdcr & 0xf;
	tmp2 = ((tmp1 & 0x3) | ((tmp1 & 0x8) >> 1)) + 1;
	apic->timer.divide_count = 0x1 << (tmp2 & 0x7);

	apic_debug("timer divide count is 0x%x\n",
				   apic->timer.divide_count);
}

static void start_apic_timer(struct kvm_lapic *apic)
{
	ktime_t now = apic->timer.dev.base->get_time();

	apic->timer.last_update = now;

	apic->timer.period = apic_get_reg(apic, APIC_TMICT) *
		    APIC_BUS_CYCLE_NS * apic->timer.divide_count;
	atomic_set(&apic->timer.pending, 0);
	hrtimer_start(&apic->timer.dev,
		      ktime_add_ns(now, apic->timer.period),
		      HRTIMER_MODE_ABS);

	apic_debug("%s: bus cycle is %" PRId64 "ns, now 0x%016"
			   PRIx64 ", "
			   "timer initial count 0x%x, period %lldns, "
			   "expire @ 0x%016" PRIx64 ".\n", __FUNCTION__,
			   APIC_BUS_CYCLE_NS, ktime_to_ns(now),
			   apic_get_reg(apic, APIC_TMICT),
			   apic->timer.period,
			   ktime_to_ns(ktime_add_ns(now,
					apic->timer.period)));
}

static void apic_mmio_write(struct kvm_io_device *this,
			    gpa_t address, int len, const void *data)
{
	struct kvm_lapic *apic = (struct kvm_lapic *)this->private;
	unsigned int offset = address - apic->base_address;
	unsigned char alignment = offset & 0xf;
	u32 val;

	/*
	 * APIC register must be aligned on 128-bits boundary.
	 * 32/64/128 bits registers must be accessed thru 32 bits.
	 * Refer SDM 8.4.1
	 */
	if (len != 4 || alignment) {
		if (printk_ratelimit())
			printk(KERN_ERR "apic write: bad size=%d %lx\n",
			       len, (long)address);
		return;
	}

	val = *(u32 *) data;

	/* too common printing */
	if (offset != APIC_EOI)
		apic_debug("%s: offset 0x%x with length 0x%x, and value is "
			   "0x%x\n", __FUNCTION__, offset, len, val);

	offset &= 0xff0;

	switch (offset) {
	case APIC_ID:		/* Local APIC ID */
		apic_set_reg(apic, APIC_ID, val);
		break;

	case APIC_TASKPRI:
		apic_set_tpr(apic, val & 0xff);
		break;

	case APIC_EOI:
		apic_set_eoi(apic);
		break;

	case APIC_LDR:
		apic_set_reg(apic, APIC_LDR, val & APIC_LDR_MASK);
		break;

	case APIC_DFR:
		apic_set_reg(apic, APIC_DFR, val | 0x0FFFFFFF);
		break;

	case APIC_SPIV:
		apic_set_reg(apic, APIC_SPIV, val & 0x3ff);
		if (!(val & APIC_SPIV_APIC_ENABLED)) {
			int i;
			u32 lvt_val;

			for (i = 0; i < APIC_LVT_NUM; i++) {
				lvt_val = apic_get_reg(apic,
						       APIC_LVTT + 0x10 * i);
				apic_set_reg(apic, APIC_LVTT + 0x10 * i,
					     lvt_val | APIC_LVT_MASKED);
			}
			atomic_set(&apic->timer.pending, 0);

		}
		break;

	case APIC_ICR:
		/* No delay here, so we always clear the pending bit */
		apic_set_reg(apic, APIC_ICR, val & ~(1 << 12));
		apic_send_ipi(apic);
		break;

	case APIC_ICR2:
		apic_set_reg(apic, APIC_ICR2, val & 0xff000000);
		break;

	case APIC_LVTT:
	case APIC_LVTTHMR:
	case APIC_LVTPC:
	case APIC_LVT0:
	case APIC_LVT1:
	case APIC_LVTERR:
		/* TODO: Check vector */
		if (!apic_sw_enabled(apic))
			val |= APIC_LVT_MASKED;

		val &= apic_lvt_mask[(offset - APIC_LVTT) >> 4];
		apic_set_reg(apic, offset, val);

		break;

	case APIC_TMICT:
		hrtimer_cancel(&apic->timer.dev);
		apic_set_reg(apic, APIC_TMICT, val);
		start_apic_timer(apic);
		return;

	case APIC_TDCR:
		if (val & 4)
			printk(KERN_ERR "KVM_WRITE:TDCR %x\n", val);
		apic_set_reg(apic, APIC_TDCR, val);
		update_divide_count(apic);
		break;

	default:
		apic_debug("Local APIC Write to read-only register %x\n",
			   offset);
		break;
	}

}

static int apic_mmio_range(struct kvm_io_device *this, gpa_t addr)
{
	struct kvm_lapic *apic = (struct kvm_lapic *)this->private;
	int ret = 0;


	if (apic_hw_enabled(apic) &&
	    (addr >= apic->base_address) &&
	    (addr < (apic->base_address + LAPIC_MMIO_LENGTH)))
		ret = 1;

	return ret;
}

void kvm_free_apic(struct kvm_lapic *apic)
{
	if (!apic)
		return;

	hrtimer_cancel(&apic->timer.dev);

	if (apic->regs_page) {
		__free_page(apic->regs_page);
		apic->regs_page = 0;
	}

	kfree(apic);
}

/*
 *----------------------------------------------------------------------
 * LAPIC interface
 *----------------------------------------------------------------------
 */

void kvm_lapic_set_tpr(struct kvm_vcpu *vcpu, unsigned long cr8)
{
	struct kvm_lapic *apic = (struct kvm_lapic *)vcpu->apic;

	if (!apic)
		return;
	apic_set_tpr(apic, ((cr8 & 0x0f) << 4));
}

u64 kvm_lapic_get_cr8(struct kvm_vcpu *vcpu)
{
	struct kvm_lapic *apic = (struct kvm_lapic *)vcpu->apic;
	u64 tpr;

	if (!apic)
		return 0;
	tpr = (u64) apic_get_reg(apic, APIC_TASKPRI);

	return (tpr & 0xf0) >> 4;
}
EXPORT_SYMBOL_GPL(kvm_lapic_get_cr8);

void kvm_lapic_set_base(struct kvm_vcpu *vcpu, u64 value)
{
	struct kvm_lapic *apic = (struct kvm_lapic *)vcpu->apic;

	if (!apic) {
		value |= MSR_IA32_APICBASE_BSP;
		vcpu->apic_base = value;
		return;
	}
	if (apic->vcpu->vcpu_id)
		value &= ~MSR_IA32_APICBASE_BSP;

	vcpu->apic_base = value;
	apic->base_address = apic->vcpu->apic_base &
			     MSR_IA32_APICBASE_BASE;

	/* with FSB delivery interrupt, we can restart APIC functionality */
	apic_debug("apic base msr is 0x%016" PRIx64 ", and base address is "
		   "0x%lx.\n", apic->apic_base, apic->base_address);

}

u64 kvm_lapic_get_base(struct kvm_vcpu *vcpu)
{
	return vcpu->apic_base;
}
EXPORT_SYMBOL_GPL(kvm_lapic_get_base);

void kvm_lapic_reset(struct kvm_vcpu *vcpu)
{
	struct kvm_lapic *apic;
	int i;

	apic_debug("%s\n", __FUNCTION__);

	ASSERT(vcpu);
	apic = vcpu->apic;
	ASSERT(apic != NULL);

	/* Stop the timer in case it's a reset to an active apic */
	hrtimer_cancel(&apic->timer.dev);

	apic_set_reg(apic, APIC_ID, vcpu->vcpu_id << 24);
	apic_set_reg(apic, APIC_LVR, APIC_VERSION);

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
	apic->timer.divide_count = 0;
	atomic_set(&apic->timer.pending, 0);
	if (vcpu->vcpu_id == 0)
		vcpu->apic_base |= MSR_IA32_APICBASE_BSP;
	apic_update_ppr(apic);

	apic_debug(KERN_INFO "%s: vcpu=%p, id=%d, base_msr="
		   "0x%016" PRIx64 ", base_address=0x%0lx.\n", __FUNCTION__,
		   vcpu, kvm_apic_id(apic),
		   vcpu->apic_base, apic->base_address);
}
EXPORT_SYMBOL_GPL(kvm_lapic_reset);

int kvm_lapic_enabled(struct kvm_vcpu *vcpu)
{
	struct kvm_lapic *apic = (struct kvm_lapic *)vcpu->apic;
	int ret = 0;

	if (!apic)
		return 0;
	ret = apic_enabled(apic);

	return ret;
}
EXPORT_SYMBOL_GPL(kvm_lapic_enabled);

/*
 *----------------------------------------------------------------------
 * timer interface
 *----------------------------------------------------------------------
 */

/* TODO: make sure __apic_timer_fn runs in current pCPU */
static int __apic_timer_fn(struct kvm_lapic *apic)
{
	int result = 0;
	wait_queue_head_t *q = &apic->vcpu->wq;

	atomic_inc(&apic->timer.pending);
	if (waitqueue_active(q))
	{
		apic->vcpu->mp_state = VCPU_MP_STATE_RUNNABLE;
		wake_up_interruptible(q);
	}
	if (apic_lvtt_period(apic)) {
		result = 1;
		apic->timer.dev.expires = ktime_add_ns(
					apic->timer.dev.expires,
					apic->timer.period);
	}
	return result;
}

static int __inject_apic_timer_irq(struct kvm_lapic *apic)
{
	int vector;

	vector = apic_lvt_vector(apic, APIC_LVTT);
	return __apic_accept_irq(apic, APIC_DM_FIXED, vector, 1, 0);
}

static enum hrtimer_restart apic_timer_fn(struct hrtimer *data)
{
	struct kvm_lapic *apic;
	int restart_timer = 0;

	apic = container_of(data, struct kvm_lapic, timer.dev);

	restart_timer = __apic_timer_fn(apic);

	if (restart_timer)
		return HRTIMER_RESTART;
	else
		return HRTIMER_NORESTART;
}

int kvm_create_lapic(struct kvm_vcpu *vcpu)
{
	struct kvm_lapic *apic;

	ASSERT(vcpu != NULL);
	apic_debug("apic_init %d\n", vcpu->vcpu_id);

	apic = kzalloc(sizeof(*apic), GFP_KERNEL);
	if (!apic)
		goto nomem;

	vcpu->apic = apic;

	apic->regs_page = alloc_page(GFP_KERNEL);
	if (apic->regs_page == NULL) {
		printk(KERN_ERR "malloc apic regs error for vcpu %x\n",
		       vcpu->vcpu_id);
		goto nomem;
	}
	apic->regs = page_address(apic->regs_page);
	memset(apic->regs, 0, PAGE_SIZE);
	apic->vcpu = vcpu;

	hrtimer_init(&apic->timer.dev, CLOCK_MONOTONIC, HRTIMER_MODE_ABS);
	apic->timer.dev.function = apic_timer_fn;
	apic->base_address = APIC_DEFAULT_PHYS_BASE;
	vcpu->apic_base = APIC_DEFAULT_PHYS_BASE;

	kvm_lapic_reset(vcpu);
	apic->dev.read = apic_mmio_read;
	apic->dev.write = apic_mmio_write;
	apic->dev.in_range = apic_mmio_range;
	apic->dev.private = apic;

	return 0;
nomem:
	kvm_free_apic(apic);
	return -ENOMEM;
}
EXPORT_SYMBOL_GPL(kvm_create_lapic);

int kvm_apic_has_interrupt(struct kvm_vcpu *vcpu)
{
	struct kvm_lapic *apic = vcpu->apic;
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
	u32 lvt0 = apic_get_reg(vcpu->apic, APIC_LVT0);
	int r = 0;

	if (vcpu->vcpu_id == 0) {
		if (!apic_hw_enabled(vcpu->apic))
			r = 1;
		if ((lvt0 & APIC_LVT_MASKED) == 0 &&
		    GET_APIC_DELIVERY_MODE(lvt0) == APIC_MODE_EXTINT)
			r = 1;
	}
	return r;
}

void kvm_inject_apic_timer_irqs(struct kvm_vcpu *vcpu)
{
	struct kvm_lapic *apic = vcpu->apic;

	if (apic && apic_lvt_enabled(apic, APIC_LVTT) &&
		atomic_read(&apic->timer.pending) > 0) {
		if (__inject_apic_timer_irq(apic))
			atomic_dec(&apic->timer.pending);
	}
}

void kvm_apic_timer_intr_post(struct kvm_vcpu *vcpu, int vec)
{
	struct kvm_lapic *apic = vcpu->apic;

	if (apic && apic_lvt_vector(apic, APIC_LVTT) == vec)
		apic->timer.last_update = ktime_add_ns(
				apic->timer.last_update,
				apic->timer.period);
}

int kvm_get_apic_interrupt(struct kvm_vcpu *vcpu)
{
	int vector = kvm_apic_has_interrupt(vcpu);
	struct kvm_lapic *apic = vcpu->apic;

	if (vector == -1)
		return -1;

	apic_set_vector(vector, apic->regs + APIC_ISR);
	apic_update_ppr(apic);
	apic_clear_irr(vector, apic);
	return vector;
}

void kvm_apic_post_state_restore(struct kvm_vcpu *vcpu)
{
	struct kvm_lapic *apic = vcpu->apic;

	apic->base_address = vcpu->apic_base &
			     MSR_IA32_APICBASE_BASE;
	apic_set_reg(apic, APIC_LVR, APIC_VERSION);
	apic_update_ppr(apic);
	hrtimer_cancel(&apic->timer.dev);
	update_divide_count(apic);
	start_apic_timer(apic);
}

void kvm_migrate_apic_timer(struct kvm_vcpu *vcpu)
{
	struct kvm_lapic *apic = vcpu->apic;
	struct hrtimer *timer;

	if (!apic)
		return;

	timer = &apic->timer.dev;
	if (hrtimer_cancel(timer))
		hrtimer_start(timer, timer->expires, HRTIMER_MODE_ABS);
}
EXPORT_SYMBOL_GPL(kvm_migrate_apic_timer);
