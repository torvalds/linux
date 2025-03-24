// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2019 Western Digital Corporation or its affiliates.
 *
 * Authors:
 *     Atish Patra <atish.patra@wdc.com>
 */

#include <linux/errno.h>
#include <linux/err.h>
#include <linux/kvm_host.h>
#include <linux/uaccess.h>
#include <clocksource/timer-riscv.h>
#include <asm/delay.h>
#include <asm/kvm_nacl.h>
#include <asm/kvm_vcpu_timer.h>

static u64 kvm_riscv_current_cycles(struct kvm_guest_timer *gt)
{
	return get_cycles64() + gt->time_delta;
}

static u64 kvm_riscv_delta_cycles2ns(u64 cycles,
				     struct kvm_guest_timer *gt,
				     struct kvm_vcpu_timer *t)
{
	unsigned long flags;
	u64 cycles_now, cycles_delta, delta_ns;

	local_irq_save(flags);
	cycles_now = kvm_riscv_current_cycles(gt);
	if (cycles_now < cycles)
		cycles_delta = cycles - cycles_now;
	else
		cycles_delta = 0;
	delta_ns = (cycles_delta * gt->nsec_mult) >> gt->nsec_shift;
	local_irq_restore(flags);

	return delta_ns;
}

static enum hrtimer_restart kvm_riscv_vcpu_hrtimer_expired(struct hrtimer *h)
{
	u64 delta_ns;
	struct kvm_vcpu_timer *t = container_of(h, struct kvm_vcpu_timer, hrt);
	struct kvm_vcpu *vcpu = container_of(t, struct kvm_vcpu, arch.timer);
	struct kvm_guest_timer *gt = &vcpu->kvm->arch.timer;

	if (kvm_riscv_current_cycles(gt) < t->next_cycles) {
		delta_ns = kvm_riscv_delta_cycles2ns(t->next_cycles, gt, t);
		hrtimer_forward_now(&t->hrt, ktime_set(0, delta_ns));
		return HRTIMER_RESTART;
	}

	t->next_set = false;
	kvm_riscv_vcpu_set_interrupt(vcpu, IRQ_VS_TIMER);

	return HRTIMER_NORESTART;
}

static int kvm_riscv_vcpu_timer_cancel(struct kvm_vcpu_timer *t)
{
	if (!t->init_done || !t->next_set)
		return -EINVAL;

	hrtimer_cancel(&t->hrt);
	t->next_set = false;

	return 0;
}

static int kvm_riscv_vcpu_update_vstimecmp(struct kvm_vcpu *vcpu, u64 ncycles)
{
#if defined(CONFIG_32BIT)
	ncsr_write(CSR_VSTIMECMP, ncycles & 0xFFFFFFFF);
	ncsr_write(CSR_VSTIMECMPH, ncycles >> 32);
#else
	ncsr_write(CSR_VSTIMECMP, ncycles);
#endif
	return 0;
}

static int kvm_riscv_vcpu_update_hrtimer(struct kvm_vcpu *vcpu, u64 ncycles)
{
	struct kvm_vcpu_timer *t = &vcpu->arch.timer;
	struct kvm_guest_timer *gt = &vcpu->kvm->arch.timer;
	u64 delta_ns;

	if (!t->init_done)
		return -EINVAL;

	kvm_riscv_vcpu_unset_interrupt(vcpu, IRQ_VS_TIMER);

	delta_ns = kvm_riscv_delta_cycles2ns(ncycles, gt, t);
	t->next_cycles = ncycles;
	hrtimer_start(&t->hrt, ktime_set(0, delta_ns), HRTIMER_MODE_REL);
	t->next_set = true;

	return 0;
}

int kvm_riscv_vcpu_timer_next_event(struct kvm_vcpu *vcpu, u64 ncycles)
{
	struct kvm_vcpu_timer *t = &vcpu->arch.timer;

	return t->timer_next_event(vcpu, ncycles);
}

static enum hrtimer_restart kvm_riscv_vcpu_vstimer_expired(struct hrtimer *h)
{
	u64 delta_ns;
	struct kvm_vcpu_timer *t = container_of(h, struct kvm_vcpu_timer, hrt);
	struct kvm_vcpu *vcpu = container_of(t, struct kvm_vcpu, arch.timer);
	struct kvm_guest_timer *gt = &vcpu->kvm->arch.timer;

	if (kvm_riscv_current_cycles(gt) < t->next_cycles) {
		delta_ns = kvm_riscv_delta_cycles2ns(t->next_cycles, gt, t);
		hrtimer_forward_now(&t->hrt, ktime_set(0, delta_ns));
		return HRTIMER_RESTART;
	}

	t->next_set = false;
	kvm_vcpu_kick(vcpu);

	return HRTIMER_NORESTART;
}

bool kvm_riscv_vcpu_timer_pending(struct kvm_vcpu *vcpu)
{
	struct kvm_vcpu_timer *t = &vcpu->arch.timer;
	struct kvm_guest_timer *gt = &vcpu->kvm->arch.timer;

	if (!kvm_riscv_delta_cycles2ns(t->next_cycles, gt, t) ||
	    kvm_riscv_vcpu_has_interrupts(vcpu, 1UL << IRQ_VS_TIMER))
		return true;
	else
		return false;
}

static void kvm_riscv_vcpu_timer_blocking(struct kvm_vcpu *vcpu)
{
	struct kvm_vcpu_timer *t = &vcpu->arch.timer;
	struct kvm_guest_timer *gt = &vcpu->kvm->arch.timer;
	u64 delta_ns;

	if (!t->init_done)
		return;

	delta_ns = kvm_riscv_delta_cycles2ns(t->next_cycles, gt, t);
	hrtimer_start(&t->hrt, ktime_set(0, delta_ns), HRTIMER_MODE_REL);
	t->next_set = true;
}

static void kvm_riscv_vcpu_timer_unblocking(struct kvm_vcpu *vcpu)
{
	kvm_riscv_vcpu_timer_cancel(&vcpu->arch.timer);
}

int kvm_riscv_vcpu_get_reg_timer(struct kvm_vcpu *vcpu,
				 const struct kvm_one_reg *reg)
{
	struct kvm_vcpu_timer *t = &vcpu->arch.timer;
	struct kvm_guest_timer *gt = &vcpu->kvm->arch.timer;
	u64 __user *uaddr = (u64 __user *)(unsigned long)reg->addr;
	unsigned long reg_num = reg->id & ~(KVM_REG_ARCH_MASK |
					    KVM_REG_SIZE_MASK |
					    KVM_REG_RISCV_TIMER);
	u64 reg_val;

	if (KVM_REG_SIZE(reg->id) != sizeof(u64))
		return -EINVAL;
	if (reg_num >= sizeof(struct kvm_riscv_timer) / sizeof(u64))
		return -ENOENT;

	switch (reg_num) {
	case KVM_REG_RISCV_TIMER_REG(frequency):
		reg_val = riscv_timebase;
		break;
	case KVM_REG_RISCV_TIMER_REG(time):
		reg_val = kvm_riscv_current_cycles(gt);
		break;
	case KVM_REG_RISCV_TIMER_REG(compare):
		reg_val = t->next_cycles;
		break;
	case KVM_REG_RISCV_TIMER_REG(state):
		reg_val = (t->next_set) ? KVM_RISCV_TIMER_STATE_ON :
					  KVM_RISCV_TIMER_STATE_OFF;
		break;
	default:
		return -ENOENT;
	}

	if (copy_to_user(uaddr, &reg_val, KVM_REG_SIZE(reg->id)))
		return -EFAULT;

	return 0;
}

int kvm_riscv_vcpu_set_reg_timer(struct kvm_vcpu *vcpu,
				 const struct kvm_one_reg *reg)
{
	struct kvm_vcpu_timer *t = &vcpu->arch.timer;
	struct kvm_guest_timer *gt = &vcpu->kvm->arch.timer;
	u64 __user *uaddr = (u64 __user *)(unsigned long)reg->addr;
	unsigned long reg_num = reg->id & ~(KVM_REG_ARCH_MASK |
					    KVM_REG_SIZE_MASK |
					    KVM_REG_RISCV_TIMER);
	u64 reg_val;
	int ret = 0;

	if (KVM_REG_SIZE(reg->id) != sizeof(u64))
		return -EINVAL;
	if (reg_num >= sizeof(struct kvm_riscv_timer) / sizeof(u64))
		return -ENOENT;

	if (copy_from_user(&reg_val, uaddr, KVM_REG_SIZE(reg->id)))
		return -EFAULT;

	switch (reg_num) {
	case KVM_REG_RISCV_TIMER_REG(frequency):
		if (reg_val != riscv_timebase)
			return -EINVAL;
		break;
	case KVM_REG_RISCV_TIMER_REG(time):
		gt->time_delta = reg_val - get_cycles64();
		break;
	case KVM_REG_RISCV_TIMER_REG(compare):
		t->next_cycles = reg_val;
		break;
	case KVM_REG_RISCV_TIMER_REG(state):
		if (reg_val == KVM_RISCV_TIMER_STATE_ON)
			ret = kvm_riscv_vcpu_timer_next_event(vcpu, reg_val);
		else
			ret = kvm_riscv_vcpu_timer_cancel(t);
		break;
	default:
		ret = -ENOENT;
		break;
	}

	return ret;
}

int kvm_riscv_vcpu_timer_init(struct kvm_vcpu *vcpu)
{
	struct kvm_vcpu_timer *t = &vcpu->arch.timer;

	if (t->init_done)
		return -EINVAL;

	hrtimer_init(&t->hrt, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	t->init_done = true;
	t->next_set = false;

	/* Enable sstc for every vcpu if available in hardware */
	if (riscv_isa_extension_available(NULL, SSTC)) {
		t->sstc_enabled = true;
		t->hrt.function = kvm_riscv_vcpu_vstimer_expired;
		t->timer_next_event = kvm_riscv_vcpu_update_vstimecmp;
	} else {
		t->sstc_enabled = false;
		t->hrt.function = kvm_riscv_vcpu_hrtimer_expired;
		t->timer_next_event = kvm_riscv_vcpu_update_hrtimer;
	}

	return 0;
}

int kvm_riscv_vcpu_timer_deinit(struct kvm_vcpu *vcpu)
{
	int ret;

	ret = kvm_riscv_vcpu_timer_cancel(&vcpu->arch.timer);
	vcpu->arch.timer.init_done = false;

	return ret;
}

int kvm_riscv_vcpu_timer_reset(struct kvm_vcpu *vcpu)
{
	struct kvm_vcpu_timer *t = &vcpu->arch.timer;

	t->next_cycles = -1ULL;
	return kvm_riscv_vcpu_timer_cancel(&vcpu->arch.timer);
}

static void kvm_riscv_vcpu_update_timedelta(struct kvm_vcpu *vcpu)
{
	struct kvm_guest_timer *gt = &vcpu->kvm->arch.timer;

#if defined(CONFIG_32BIT)
	ncsr_write(CSR_HTIMEDELTA, (u32)(gt->time_delta));
	ncsr_write(CSR_HTIMEDELTAH, (u32)(gt->time_delta >> 32));
#else
	ncsr_write(CSR_HTIMEDELTA, gt->time_delta);
#endif
}

void kvm_riscv_vcpu_timer_restore(struct kvm_vcpu *vcpu)
{
	struct kvm_vcpu_timer *t = &vcpu->arch.timer;

	kvm_riscv_vcpu_update_timedelta(vcpu);

	if (!t->sstc_enabled)
		return;

#if defined(CONFIG_32BIT)
	ncsr_write(CSR_VSTIMECMP, (u32)t->next_cycles);
	ncsr_write(CSR_VSTIMECMPH, (u32)(t->next_cycles >> 32));
#else
	ncsr_write(CSR_VSTIMECMP, t->next_cycles);
#endif

	/* timer should be enabled for the remaining operations */
	if (unlikely(!t->init_done))
		return;

	kvm_riscv_vcpu_timer_unblocking(vcpu);
}

void kvm_riscv_vcpu_timer_sync(struct kvm_vcpu *vcpu)
{
	struct kvm_vcpu_timer *t = &vcpu->arch.timer;

	if (!t->sstc_enabled)
		return;

#if defined(CONFIG_32BIT)
	t->next_cycles = ncsr_read(CSR_VSTIMECMP);
	t->next_cycles |= (u64)ncsr_read(CSR_VSTIMECMPH) << 32;
#else
	t->next_cycles = ncsr_read(CSR_VSTIMECMP);
#endif
}

void kvm_riscv_vcpu_timer_save(struct kvm_vcpu *vcpu)
{
	struct kvm_vcpu_timer *t = &vcpu->arch.timer;

	if (!t->sstc_enabled)
		return;

	/*
	 * The vstimecmp CSRs are saved by kvm_riscv_vcpu_timer_sync()
	 * upon every VM exit so no need to save here.
	 */

	/* timer should be enabled for the remaining operations */
	if (unlikely(!t->init_done))
		return;

	if (kvm_vcpu_is_blocking(vcpu))
		kvm_riscv_vcpu_timer_blocking(vcpu);
}

void kvm_riscv_guest_timer_init(struct kvm *kvm)
{
	struct kvm_guest_timer *gt = &kvm->arch.timer;

	riscv_cs_get_mult_shift(&gt->nsec_mult, &gt->nsec_shift);
	gt->time_delta = -get_cycles64();
}
