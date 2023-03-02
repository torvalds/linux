// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2012 ARM Ltd.
 * Author: Marc Zyngier <marc.zyngier@arm.com>
 */

#include <linux/cpu.h>
#include <linux/kvm.h>
#include <linux/kvm_host.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/uaccess.h>

#include <clocksource/arm_arch_timer.h>
#include <asm/arch_timer.h>
#include <asm/kvm_emulate.h>
#include <asm/kvm_hyp.h>

#include <kvm/arm_vgic.h>
#include <kvm/arm_arch_timer.h>

#include "trace.h"

static struct timecounter *timecounter;
static unsigned int host_vtimer_irq;
static unsigned int host_ptimer_irq;
static u32 host_vtimer_irq_flags;
static u32 host_ptimer_irq_flags;

static DEFINE_STATIC_KEY_FALSE(has_gic_active_state);

static const struct kvm_irq_level default_ptimer_irq = {
	.irq	= 30,
	.level	= 1,
};

static const struct kvm_irq_level default_vtimer_irq = {
	.irq	= 27,
	.level	= 1,
};

static bool kvm_timer_irq_can_fire(struct arch_timer_context *timer_ctx);
static void kvm_timer_update_irq(struct kvm_vcpu *vcpu, bool new_level,
				 struct arch_timer_context *timer_ctx);
static bool kvm_timer_should_fire(struct arch_timer_context *timer_ctx);
static void kvm_arm_timer_write(struct kvm_vcpu *vcpu,
				struct arch_timer_context *timer,
				enum kvm_arch_timer_regs treg,
				u64 val);
static u64 kvm_arm_timer_read(struct kvm_vcpu *vcpu,
			      struct arch_timer_context *timer,
			      enum kvm_arch_timer_regs treg);

u32 timer_get_ctl(struct arch_timer_context *ctxt)
{
	struct kvm_vcpu *vcpu = ctxt->vcpu;

	switch(arch_timer_ctx_index(ctxt)) {
	case TIMER_VTIMER:
		return __vcpu_sys_reg(vcpu, CNTV_CTL_EL0);
	case TIMER_PTIMER:
		return __vcpu_sys_reg(vcpu, CNTP_CTL_EL0);
	default:
		WARN_ON(1);
		return 0;
	}
}

u64 timer_get_cval(struct arch_timer_context *ctxt)
{
	struct kvm_vcpu *vcpu = ctxt->vcpu;

	switch(arch_timer_ctx_index(ctxt)) {
	case TIMER_VTIMER:
		return __vcpu_sys_reg(vcpu, CNTV_CVAL_EL0);
	case TIMER_PTIMER:
		return __vcpu_sys_reg(vcpu, CNTP_CVAL_EL0);
	default:
		WARN_ON(1);
		return 0;
	}
}

static u64 timer_get_offset(struct arch_timer_context *ctxt)
{
	struct kvm_vcpu *vcpu = ctxt->vcpu;

	switch(arch_timer_ctx_index(ctxt)) {
	case TIMER_VTIMER:
		return __vcpu_sys_reg(vcpu, CNTVOFF_EL2);
	default:
		return 0;
	}
}

static void timer_set_ctl(struct arch_timer_context *ctxt, u32 ctl)
{
	struct kvm_vcpu *vcpu = ctxt->vcpu;

	switch(arch_timer_ctx_index(ctxt)) {
	case TIMER_VTIMER:
		__vcpu_sys_reg(vcpu, CNTV_CTL_EL0) = ctl;
		break;
	case TIMER_PTIMER:
		__vcpu_sys_reg(vcpu, CNTP_CTL_EL0) = ctl;
		break;
	default:
		WARN_ON(1);
	}
}

static void timer_set_cval(struct arch_timer_context *ctxt, u64 cval)
{
	struct kvm_vcpu *vcpu = ctxt->vcpu;

	switch(arch_timer_ctx_index(ctxt)) {
	case TIMER_VTIMER:
		__vcpu_sys_reg(vcpu, CNTV_CVAL_EL0) = cval;
		break;
	case TIMER_PTIMER:
		__vcpu_sys_reg(vcpu, CNTP_CVAL_EL0) = cval;
		break;
	default:
		WARN_ON(1);
	}
}

static void timer_set_offset(struct arch_timer_context *ctxt, u64 offset)
{
	struct kvm_vcpu *vcpu = ctxt->vcpu;

	switch(arch_timer_ctx_index(ctxt)) {
	case TIMER_VTIMER:
		__vcpu_sys_reg(vcpu, CNTVOFF_EL2) = offset;
		break;
	default:
		WARN(offset, "timer %ld\n", arch_timer_ctx_index(ctxt));
	}
}

u64 kvm_phys_timer_read(void)
{
	return timecounter->cc->read(timecounter->cc);
}

static void get_timer_map(struct kvm_vcpu *vcpu, struct timer_map *map)
{
	if (has_vhe()) {
		map->direct_vtimer = vcpu_vtimer(vcpu);
		map->direct_ptimer = vcpu_ptimer(vcpu);
		map->emul_ptimer = NULL;
	} else {
		map->direct_vtimer = vcpu_vtimer(vcpu);
		map->direct_ptimer = NULL;
		map->emul_ptimer = vcpu_ptimer(vcpu);
	}

	trace_kvm_get_timer_map(vcpu->vcpu_id, map);
}

static inline bool userspace_irqchip(struct kvm *kvm)
{
	return static_branch_unlikely(&userspace_irqchip_in_use) &&
		unlikely(!irqchip_in_kernel(kvm));
}

static void soft_timer_start(struct hrtimer *hrt, u64 ns)
{
	hrtimer_start(hrt, ktime_add_ns(ktime_get(), ns),
		      HRTIMER_MODE_ABS_HARD);
}

static void soft_timer_cancel(struct hrtimer *hrt)
{
	hrtimer_cancel(hrt);
}

static irqreturn_t kvm_arch_timer_handler(int irq, void *dev_id)
{
	struct kvm_vcpu *vcpu = *(struct kvm_vcpu **)dev_id;
	struct arch_timer_context *ctx;
	struct timer_map map;

	/*
	 * We may see a timer interrupt after vcpu_put() has been called which
	 * sets the CPU's vcpu pointer to NULL, because even though the timer
	 * has been disabled in timer_save_state(), the hardware interrupt
	 * signal may not have been retired from the interrupt controller yet.
	 */
	if (!vcpu)
		return IRQ_HANDLED;

	get_timer_map(vcpu, &map);

	if (irq == host_vtimer_irq)
		ctx = map.direct_vtimer;
	else
		ctx = map.direct_ptimer;

	if (kvm_timer_should_fire(ctx))
		kvm_timer_update_irq(vcpu, true, ctx);

	if (userspace_irqchip(vcpu->kvm) &&
	    !static_branch_unlikely(&has_gic_active_state))
		disable_percpu_irq(host_vtimer_irq);

	return IRQ_HANDLED;
}

static u64 kvm_counter_compute_delta(struct arch_timer_context *timer_ctx,
				     u64 val)
{
	u64 now = kvm_phys_timer_read() - timer_get_offset(timer_ctx);

	if (now < val) {
		u64 ns;

		ns = cyclecounter_cyc2ns(timecounter->cc,
					 val - now,
					 timecounter->mask,
					 &timecounter->frac);
		return ns;
	}

	return 0;
}

static u64 kvm_timer_compute_delta(struct arch_timer_context *timer_ctx)
{
	return kvm_counter_compute_delta(timer_ctx, timer_get_cval(timer_ctx));
}

static bool kvm_timer_irq_can_fire(struct arch_timer_context *timer_ctx)
{
	WARN_ON(timer_ctx && timer_ctx->loaded);
	return timer_ctx &&
		((timer_get_ctl(timer_ctx) &
		  (ARCH_TIMER_CTRL_IT_MASK | ARCH_TIMER_CTRL_ENABLE)) == ARCH_TIMER_CTRL_ENABLE);
}

static bool vcpu_has_wfit_active(struct kvm_vcpu *vcpu)
{
	return (cpus_have_final_cap(ARM64_HAS_WFXT) &&
		vcpu_get_flag(vcpu, IN_WFIT));
}

static u64 wfit_delay_ns(struct kvm_vcpu *vcpu)
{
	struct arch_timer_context *ctx = vcpu_vtimer(vcpu);
	u64 val = vcpu_get_reg(vcpu, kvm_vcpu_sys_get_rt(vcpu));

	return kvm_counter_compute_delta(ctx, val);
}

/*
 * Returns the earliest expiration time in ns among guest timers.
 * Note that it will return 0 if none of timers can fire.
 */
static u64 kvm_timer_earliest_exp(struct kvm_vcpu *vcpu)
{
	u64 min_delta = ULLONG_MAX;
	int i;

	for (i = 0; i < NR_KVM_TIMERS; i++) {
		struct arch_timer_context *ctx = &vcpu->arch.timer_cpu.timers[i];

		WARN(ctx->loaded, "timer %d loaded\n", i);
		if (kvm_timer_irq_can_fire(ctx))
			min_delta = min(min_delta, kvm_timer_compute_delta(ctx));
	}

	if (vcpu_has_wfit_active(vcpu))
		min_delta = min(min_delta, wfit_delay_ns(vcpu));

	/* If none of timers can fire, then return 0 */
	if (min_delta == ULLONG_MAX)
		return 0;

	return min_delta;
}

static enum hrtimer_restart kvm_bg_timer_expire(struct hrtimer *hrt)
{
	struct arch_timer_cpu *timer;
	struct kvm_vcpu *vcpu;
	u64 ns;

	timer = container_of(hrt, struct arch_timer_cpu, bg_timer);
	vcpu = container_of(timer, struct kvm_vcpu, arch.timer_cpu);

	/*
	 * Check that the timer has really expired from the guest's
	 * PoV (NTP on the host may have forced it to expire
	 * early). If we should have slept longer, restart it.
	 */
	ns = kvm_timer_earliest_exp(vcpu);
	if (unlikely(ns)) {
		hrtimer_forward_now(hrt, ns_to_ktime(ns));
		return HRTIMER_RESTART;
	}

	kvm_vcpu_wake_up(vcpu);
	return HRTIMER_NORESTART;
}

static enum hrtimer_restart kvm_hrtimer_expire(struct hrtimer *hrt)
{
	struct arch_timer_context *ctx;
	struct kvm_vcpu *vcpu;
	u64 ns;

	ctx = container_of(hrt, struct arch_timer_context, hrtimer);
	vcpu = ctx->vcpu;

	trace_kvm_timer_hrtimer_expire(ctx);

	/*
	 * Check that the timer has really expired from the guest's
	 * PoV (NTP on the host may have forced it to expire
	 * early). If not ready, schedule for a later time.
	 */
	ns = kvm_timer_compute_delta(ctx);
	if (unlikely(ns)) {
		hrtimer_forward_now(hrt, ns_to_ktime(ns));
		return HRTIMER_RESTART;
	}

	kvm_timer_update_irq(vcpu, true, ctx);
	return HRTIMER_NORESTART;
}

static bool kvm_timer_should_fire(struct arch_timer_context *timer_ctx)
{
	enum kvm_arch_timers index;
	u64 cval, now;

	if (!timer_ctx)
		return false;

	index = arch_timer_ctx_index(timer_ctx);

	if (timer_ctx->loaded) {
		u32 cnt_ctl = 0;

		switch (index) {
		case TIMER_VTIMER:
			cnt_ctl = read_sysreg_el0(SYS_CNTV_CTL);
			break;
		case TIMER_PTIMER:
			cnt_ctl = read_sysreg_el0(SYS_CNTP_CTL);
			break;
		case NR_KVM_TIMERS:
			/* GCC is braindead */
			cnt_ctl = 0;
			break;
		}

		return  (cnt_ctl & ARCH_TIMER_CTRL_ENABLE) &&
		        (cnt_ctl & ARCH_TIMER_CTRL_IT_STAT) &&
		       !(cnt_ctl & ARCH_TIMER_CTRL_IT_MASK);
	}

	if (!kvm_timer_irq_can_fire(timer_ctx))
		return false;

	cval = timer_get_cval(timer_ctx);
	now = kvm_phys_timer_read() - timer_get_offset(timer_ctx);

	return cval <= now;
}

int kvm_cpu_has_pending_timer(struct kvm_vcpu *vcpu)
{
	return vcpu_has_wfit_active(vcpu) && wfit_delay_ns(vcpu) == 0;
}

/*
 * Reflect the timer output level into the kvm_run structure
 */
void kvm_timer_update_run(struct kvm_vcpu *vcpu)
{
	struct arch_timer_context *vtimer = vcpu_vtimer(vcpu);
	struct arch_timer_context *ptimer = vcpu_ptimer(vcpu);
	struct kvm_sync_regs *regs = &vcpu->run->s.regs;

	/* Populate the device bitmap with the timer states */
	regs->device_irq_level &= ~(KVM_ARM_DEV_EL1_VTIMER |
				    KVM_ARM_DEV_EL1_PTIMER);
	if (kvm_timer_should_fire(vtimer))
		regs->device_irq_level |= KVM_ARM_DEV_EL1_VTIMER;
	if (kvm_timer_should_fire(ptimer))
		regs->device_irq_level |= KVM_ARM_DEV_EL1_PTIMER;
}

static void kvm_timer_update_irq(struct kvm_vcpu *vcpu, bool new_level,
				 struct arch_timer_context *timer_ctx)
{
	int ret;

	timer_ctx->irq.level = new_level;
	trace_kvm_timer_update_irq(vcpu->vcpu_id, timer_ctx->irq.irq,
				   timer_ctx->irq.level);

	if (!userspace_irqchip(vcpu->kvm)) {
		ret = kvm_vgic_inject_irq(vcpu->kvm, vcpu->vcpu_id,
					  timer_ctx->irq.irq,
					  timer_ctx->irq.level,
					  timer_ctx);
		WARN_ON(ret);
	}
}

/* Only called for a fully emulated timer */
static void timer_emulate(struct arch_timer_context *ctx)
{
	bool should_fire = kvm_timer_should_fire(ctx);

	trace_kvm_timer_emulate(ctx, should_fire);

	if (should_fire != ctx->irq.level) {
		kvm_timer_update_irq(ctx->vcpu, should_fire, ctx);
		return;
	}

	/*
	 * If the timer can fire now, we don't need to have a soft timer
	 * scheduled for the future.  If the timer cannot fire at all,
	 * then we also don't need a soft timer.
	 */
	if (should_fire || !kvm_timer_irq_can_fire(ctx))
		return;

	soft_timer_start(&ctx->hrtimer, kvm_timer_compute_delta(ctx));
}

static void set_cntvoff(u64 cntvoff)
{
	kvm_call_hyp(__kvm_timer_set_cntvoff, cntvoff);
}

static void timer_save_state(struct arch_timer_context *ctx)
{
	struct arch_timer_cpu *timer = vcpu_timer(ctx->vcpu);
	enum kvm_arch_timers index = arch_timer_ctx_index(ctx);
	unsigned long flags;

	if (!timer->enabled)
		return;

	local_irq_save(flags);

	if (!ctx->loaded)
		goto out;

	switch (index) {
	case TIMER_VTIMER:
		timer_set_ctl(ctx, read_sysreg_el0(SYS_CNTV_CTL));
		timer_set_cval(ctx, read_sysreg_el0(SYS_CNTV_CVAL));

		/* Disable the timer */
		write_sysreg_el0(0, SYS_CNTV_CTL);
		isb();

		/*
		 * The kernel may decide to run userspace after
		 * calling vcpu_put, so we reset cntvoff to 0 to
		 * ensure a consistent read between user accesses to
		 * the virtual counter and kernel access to the
		 * physical counter of non-VHE case.
		 *
		 * For VHE, the virtual counter uses a fixed virtual
		 * offset of zero, so no need to zero CNTVOFF_EL2
		 * register, but this is actually useful when switching
		 * between EL1/vEL2 with NV.
		 *
		 * Do it unconditionally, as this is either unavoidable
		 * or dirt cheap.
		 */
		set_cntvoff(0);
		break;
	case TIMER_PTIMER:
		timer_set_ctl(ctx, read_sysreg_el0(SYS_CNTP_CTL));
		timer_set_cval(ctx, read_sysreg_el0(SYS_CNTP_CVAL));

		/* Disable the timer */
		write_sysreg_el0(0, SYS_CNTP_CTL);
		isb();

		break;
	case NR_KVM_TIMERS:
		BUG();
	}

	trace_kvm_timer_save_state(ctx);

	ctx->loaded = false;
out:
	local_irq_restore(flags);
}

/*
 * Schedule the background timer before calling kvm_vcpu_halt, so that this
 * thread is removed from its waitqueue and made runnable when there's a timer
 * interrupt to handle.
 */
static void kvm_timer_blocking(struct kvm_vcpu *vcpu)
{
	struct arch_timer_cpu *timer = vcpu_timer(vcpu);
	struct timer_map map;

	get_timer_map(vcpu, &map);

	/*
	 * If no timers are capable of raising interrupts (disabled or
	 * masked), then there's no more work for us to do.
	 */
	if (!kvm_timer_irq_can_fire(map.direct_vtimer) &&
	    !kvm_timer_irq_can_fire(map.direct_ptimer) &&
	    !kvm_timer_irq_can_fire(map.emul_ptimer) &&
	    !vcpu_has_wfit_active(vcpu))
		return;

	/*
	 * At least one guest time will expire. Schedule a background timer.
	 * Set the earliest expiration time among the guest timers.
	 */
	soft_timer_start(&timer->bg_timer, kvm_timer_earliest_exp(vcpu));
}

static void kvm_timer_unblocking(struct kvm_vcpu *vcpu)
{
	struct arch_timer_cpu *timer = vcpu_timer(vcpu);

	soft_timer_cancel(&timer->bg_timer);
}

static void timer_restore_state(struct arch_timer_context *ctx)
{
	struct arch_timer_cpu *timer = vcpu_timer(ctx->vcpu);
	enum kvm_arch_timers index = arch_timer_ctx_index(ctx);
	unsigned long flags;

	if (!timer->enabled)
		return;

	local_irq_save(flags);

	if (ctx->loaded)
		goto out;

	switch (index) {
	case TIMER_VTIMER:
		set_cntvoff(timer_get_offset(ctx));
		write_sysreg_el0(timer_get_cval(ctx), SYS_CNTV_CVAL);
		isb();
		write_sysreg_el0(timer_get_ctl(ctx), SYS_CNTV_CTL);
		break;
	case TIMER_PTIMER:
		write_sysreg_el0(timer_get_cval(ctx), SYS_CNTP_CVAL);
		isb();
		write_sysreg_el0(timer_get_ctl(ctx), SYS_CNTP_CTL);
		break;
	case NR_KVM_TIMERS:
		BUG();
	}

	trace_kvm_timer_restore_state(ctx);

	ctx->loaded = true;
out:
	local_irq_restore(flags);
}

static inline void set_timer_irq_phys_active(struct arch_timer_context *ctx, bool active)
{
	int r;
	r = irq_set_irqchip_state(ctx->host_timer_irq, IRQCHIP_STATE_ACTIVE, active);
	WARN_ON(r);
}

static void kvm_timer_vcpu_load_gic(struct arch_timer_context *ctx)
{
	struct kvm_vcpu *vcpu = ctx->vcpu;
	bool phys_active = false;

	/*
	 * Update the timer output so that it is likely to match the
	 * state we're about to restore. If the timer expires between
	 * this point and the register restoration, we'll take the
	 * interrupt anyway.
	 */
	kvm_timer_update_irq(ctx->vcpu, kvm_timer_should_fire(ctx), ctx);

	if (irqchip_in_kernel(vcpu->kvm))
		phys_active = kvm_vgic_map_is_active(vcpu, ctx->irq.irq);

	phys_active |= ctx->irq.level;

	set_timer_irq_phys_active(ctx, phys_active);
}

static void kvm_timer_vcpu_load_nogic(struct kvm_vcpu *vcpu)
{
	struct arch_timer_context *vtimer = vcpu_vtimer(vcpu);

	/*
	 * Update the timer output so that it is likely to match the
	 * state we're about to restore. If the timer expires between
	 * this point and the register restoration, we'll take the
	 * interrupt anyway.
	 */
	kvm_timer_update_irq(vcpu, kvm_timer_should_fire(vtimer), vtimer);

	/*
	 * When using a userspace irqchip with the architected timers and a
	 * host interrupt controller that doesn't support an active state, we
	 * must still prevent continuously exiting from the guest, and
	 * therefore mask the physical interrupt by disabling it on the host
	 * interrupt controller when the virtual level is high, such that the
	 * guest can make forward progress.  Once we detect the output level
	 * being de-asserted, we unmask the interrupt again so that we exit
	 * from the guest when the timer fires.
	 */
	if (vtimer->irq.level)
		disable_percpu_irq(host_vtimer_irq);
	else
		enable_percpu_irq(host_vtimer_irq, host_vtimer_irq_flags);
}

void kvm_timer_vcpu_load(struct kvm_vcpu *vcpu)
{
	struct arch_timer_cpu *timer = vcpu_timer(vcpu);
	struct timer_map map;

	if (unlikely(!timer->enabled))
		return;

	get_timer_map(vcpu, &map);

	if (static_branch_likely(&has_gic_active_state)) {
		kvm_timer_vcpu_load_gic(map.direct_vtimer);
		if (map.direct_ptimer)
			kvm_timer_vcpu_load_gic(map.direct_ptimer);
	} else {
		kvm_timer_vcpu_load_nogic(vcpu);
	}

	kvm_timer_unblocking(vcpu);

	timer_restore_state(map.direct_vtimer);
	if (map.direct_ptimer)
		timer_restore_state(map.direct_ptimer);

	if (map.emul_ptimer)
		timer_emulate(map.emul_ptimer);
}

bool kvm_timer_should_notify_user(struct kvm_vcpu *vcpu)
{
	struct arch_timer_context *vtimer = vcpu_vtimer(vcpu);
	struct arch_timer_context *ptimer = vcpu_ptimer(vcpu);
	struct kvm_sync_regs *sregs = &vcpu->run->s.regs;
	bool vlevel, plevel;

	if (likely(irqchip_in_kernel(vcpu->kvm)))
		return false;

	vlevel = sregs->device_irq_level & KVM_ARM_DEV_EL1_VTIMER;
	plevel = sregs->device_irq_level & KVM_ARM_DEV_EL1_PTIMER;

	return kvm_timer_should_fire(vtimer) != vlevel ||
	       kvm_timer_should_fire(ptimer) != plevel;
}

void kvm_timer_vcpu_put(struct kvm_vcpu *vcpu)
{
	struct arch_timer_cpu *timer = vcpu_timer(vcpu);
	struct timer_map map;

	if (unlikely(!timer->enabled))
		return;

	get_timer_map(vcpu, &map);

	timer_save_state(map.direct_vtimer);
	if (map.direct_ptimer)
		timer_save_state(map.direct_ptimer);

	/*
	 * Cancel soft timer emulation, because the only case where we
	 * need it after a vcpu_put is in the context of a sleeping VCPU, and
	 * in that case we already factor in the deadline for the physical
	 * timer when scheduling the bg_timer.
	 *
	 * In any case, we re-schedule the hrtimer for the physical timer when
	 * coming back to the VCPU thread in kvm_timer_vcpu_load().
	 */
	if (map.emul_ptimer)
		soft_timer_cancel(&map.emul_ptimer->hrtimer);

	if (kvm_vcpu_is_blocking(vcpu))
		kvm_timer_blocking(vcpu);
}

/*
 * With a userspace irqchip we have to check if the guest de-asserted the
 * timer and if so, unmask the timer irq signal on the host interrupt
 * controller to ensure that we see future timer signals.
 */
static void unmask_vtimer_irq_user(struct kvm_vcpu *vcpu)
{
	struct arch_timer_context *vtimer = vcpu_vtimer(vcpu);

	if (!kvm_timer_should_fire(vtimer)) {
		kvm_timer_update_irq(vcpu, false, vtimer);
		if (static_branch_likely(&has_gic_active_state))
			set_timer_irq_phys_active(vtimer, false);
		else
			enable_percpu_irq(host_vtimer_irq, host_vtimer_irq_flags);
	}
}

void kvm_timer_sync_user(struct kvm_vcpu *vcpu)
{
	struct arch_timer_cpu *timer = vcpu_timer(vcpu);

	if (unlikely(!timer->enabled))
		return;

	if (unlikely(!irqchip_in_kernel(vcpu->kvm)))
		unmask_vtimer_irq_user(vcpu);
}

int kvm_timer_vcpu_reset(struct kvm_vcpu *vcpu)
{
	struct arch_timer_cpu *timer = vcpu_timer(vcpu);
	struct timer_map map;

	get_timer_map(vcpu, &map);

	/*
	 * The bits in CNTV_CTL are architecturally reset to UNKNOWN for ARMv8
	 * and to 0 for ARMv7.  We provide an implementation that always
	 * resets the timer to be disabled and unmasked and is compliant with
	 * the ARMv7 architecture.
	 */
	timer_set_ctl(vcpu_vtimer(vcpu), 0);
	timer_set_ctl(vcpu_ptimer(vcpu), 0);

	if (timer->enabled) {
		kvm_timer_update_irq(vcpu, false, vcpu_vtimer(vcpu));
		kvm_timer_update_irq(vcpu, false, vcpu_ptimer(vcpu));

		if (irqchip_in_kernel(vcpu->kvm)) {
			kvm_vgic_reset_mapped_irq(vcpu, map.direct_vtimer->irq.irq);
			if (map.direct_ptimer)
				kvm_vgic_reset_mapped_irq(vcpu, map.direct_ptimer->irq.irq);
		}
	}

	if (map.emul_ptimer)
		soft_timer_cancel(&map.emul_ptimer->hrtimer);

	return 0;
}

/* Make the updates of cntvoff for all vtimer contexts atomic */
static void update_vtimer_cntvoff(struct kvm_vcpu *vcpu, u64 cntvoff)
{
	unsigned long i;
	struct kvm *kvm = vcpu->kvm;
	struct kvm_vcpu *tmp;

	mutex_lock(&kvm->lock);
	kvm_for_each_vcpu(i, tmp, kvm)
		timer_set_offset(vcpu_vtimer(tmp), cntvoff);

	/*
	 * When called from the vcpu create path, the CPU being created is not
	 * included in the loop above, so we just set it here as well.
	 */
	timer_set_offset(vcpu_vtimer(vcpu), cntvoff);
	mutex_unlock(&kvm->lock);
}

void kvm_timer_vcpu_init(struct kvm_vcpu *vcpu)
{
	struct arch_timer_cpu *timer = vcpu_timer(vcpu);
	struct arch_timer_context *vtimer = vcpu_vtimer(vcpu);
	struct arch_timer_context *ptimer = vcpu_ptimer(vcpu);

	vtimer->vcpu = vcpu;
	ptimer->vcpu = vcpu;

	/* Synchronize cntvoff across all vtimers of a VM. */
	update_vtimer_cntvoff(vcpu, kvm_phys_timer_read());
	timer_set_offset(ptimer, 0);

	hrtimer_init(&timer->bg_timer, CLOCK_MONOTONIC, HRTIMER_MODE_ABS_HARD);
	timer->bg_timer.function = kvm_bg_timer_expire;

	hrtimer_init(&vtimer->hrtimer, CLOCK_MONOTONIC, HRTIMER_MODE_ABS_HARD);
	hrtimer_init(&ptimer->hrtimer, CLOCK_MONOTONIC, HRTIMER_MODE_ABS_HARD);
	vtimer->hrtimer.function = kvm_hrtimer_expire;
	ptimer->hrtimer.function = kvm_hrtimer_expire;

	vtimer->irq.irq = default_vtimer_irq.irq;
	ptimer->irq.irq = default_ptimer_irq.irq;

	vtimer->host_timer_irq = host_vtimer_irq;
	ptimer->host_timer_irq = host_ptimer_irq;

	vtimer->host_timer_irq_flags = host_vtimer_irq_flags;
	ptimer->host_timer_irq_flags = host_ptimer_irq_flags;
}

void kvm_timer_cpu_up(void)
{
	enable_percpu_irq(host_vtimer_irq, host_vtimer_irq_flags);
	if (host_ptimer_irq)
		enable_percpu_irq(host_ptimer_irq, host_ptimer_irq_flags);
}

void kvm_timer_cpu_down(void)
{
	disable_percpu_irq(host_vtimer_irq);
	if (host_ptimer_irq)
		disable_percpu_irq(host_ptimer_irq);
}

int kvm_arm_timer_set_reg(struct kvm_vcpu *vcpu, u64 regid, u64 value)
{
	struct arch_timer_context *timer;

	switch (regid) {
	case KVM_REG_ARM_TIMER_CTL:
		timer = vcpu_vtimer(vcpu);
		kvm_arm_timer_write(vcpu, timer, TIMER_REG_CTL, value);
		break;
	case KVM_REG_ARM_TIMER_CNT:
		timer = vcpu_vtimer(vcpu);
		update_vtimer_cntvoff(vcpu, kvm_phys_timer_read() - value);
		break;
	case KVM_REG_ARM_TIMER_CVAL:
		timer = vcpu_vtimer(vcpu);
		kvm_arm_timer_write(vcpu, timer, TIMER_REG_CVAL, value);
		break;
	case KVM_REG_ARM_PTIMER_CTL:
		timer = vcpu_ptimer(vcpu);
		kvm_arm_timer_write(vcpu, timer, TIMER_REG_CTL, value);
		break;
	case KVM_REG_ARM_PTIMER_CVAL:
		timer = vcpu_ptimer(vcpu);
		kvm_arm_timer_write(vcpu, timer, TIMER_REG_CVAL, value);
		break;

	default:
		return -1;
	}

	return 0;
}

static u64 read_timer_ctl(struct arch_timer_context *timer)
{
	/*
	 * Set ISTATUS bit if it's expired.
	 * Note that according to ARMv8 ARM Issue A.k, ISTATUS bit is
	 * UNKNOWN when ENABLE bit is 0, so we chose to set ISTATUS bit
	 * regardless of ENABLE bit for our implementation convenience.
	 */
	u32 ctl = timer_get_ctl(timer);

	if (!kvm_timer_compute_delta(timer))
		ctl |= ARCH_TIMER_CTRL_IT_STAT;

	return ctl;
}

u64 kvm_arm_timer_get_reg(struct kvm_vcpu *vcpu, u64 regid)
{
	switch (regid) {
	case KVM_REG_ARM_TIMER_CTL:
		return kvm_arm_timer_read(vcpu,
					  vcpu_vtimer(vcpu), TIMER_REG_CTL);
	case KVM_REG_ARM_TIMER_CNT:
		return kvm_arm_timer_read(vcpu,
					  vcpu_vtimer(vcpu), TIMER_REG_CNT);
	case KVM_REG_ARM_TIMER_CVAL:
		return kvm_arm_timer_read(vcpu,
					  vcpu_vtimer(vcpu), TIMER_REG_CVAL);
	case KVM_REG_ARM_PTIMER_CTL:
		return kvm_arm_timer_read(vcpu,
					  vcpu_ptimer(vcpu), TIMER_REG_CTL);
	case KVM_REG_ARM_PTIMER_CNT:
		return kvm_arm_timer_read(vcpu,
					  vcpu_ptimer(vcpu), TIMER_REG_CNT);
	case KVM_REG_ARM_PTIMER_CVAL:
		return kvm_arm_timer_read(vcpu,
					  vcpu_ptimer(vcpu), TIMER_REG_CVAL);
	}
	return (u64)-1;
}

static u64 kvm_arm_timer_read(struct kvm_vcpu *vcpu,
			      struct arch_timer_context *timer,
			      enum kvm_arch_timer_regs treg)
{
	u64 val;

	switch (treg) {
	case TIMER_REG_TVAL:
		val = timer_get_cval(timer) - kvm_phys_timer_read() + timer_get_offset(timer);
		val = lower_32_bits(val);
		break;

	case TIMER_REG_CTL:
		val = read_timer_ctl(timer);
		break;

	case TIMER_REG_CVAL:
		val = timer_get_cval(timer);
		break;

	case TIMER_REG_CNT:
		val = kvm_phys_timer_read() - timer_get_offset(timer);
		break;

	default:
		BUG();
	}

	return val;
}

u64 kvm_arm_timer_read_sysreg(struct kvm_vcpu *vcpu,
			      enum kvm_arch_timers tmr,
			      enum kvm_arch_timer_regs treg)
{
	struct arch_timer_context *timer;
	struct timer_map map;
	u64 val;

	get_timer_map(vcpu, &map);
	timer = vcpu_get_timer(vcpu, tmr);

	if (timer == map.emul_ptimer)
		return kvm_arm_timer_read(vcpu, timer, treg);

	preempt_disable();
	timer_save_state(timer);

	val = kvm_arm_timer_read(vcpu, timer, treg);

	timer_restore_state(timer);
	preempt_enable();

	return val;
}

static void kvm_arm_timer_write(struct kvm_vcpu *vcpu,
				struct arch_timer_context *timer,
				enum kvm_arch_timer_regs treg,
				u64 val)
{
	switch (treg) {
	case TIMER_REG_TVAL:
		timer_set_cval(timer, kvm_phys_timer_read() - timer_get_offset(timer) + (s32)val);
		break;

	case TIMER_REG_CTL:
		timer_set_ctl(timer, val & ~ARCH_TIMER_CTRL_IT_STAT);
		break;

	case TIMER_REG_CVAL:
		timer_set_cval(timer, val);
		break;

	default:
		BUG();
	}
}

void kvm_arm_timer_write_sysreg(struct kvm_vcpu *vcpu,
				enum kvm_arch_timers tmr,
				enum kvm_arch_timer_regs treg,
				u64 val)
{
	struct arch_timer_context *timer;
	struct timer_map map;

	get_timer_map(vcpu, &map);
	timer = vcpu_get_timer(vcpu, tmr);
	if (timer == map.emul_ptimer) {
		soft_timer_cancel(&timer->hrtimer);
		kvm_arm_timer_write(vcpu, timer, treg, val);
		timer_emulate(timer);
	} else {
		preempt_disable();
		timer_save_state(timer);
		kvm_arm_timer_write(vcpu, timer, treg, val);
		timer_restore_state(timer);
		preempt_enable();
	}
}

static int timer_irq_set_vcpu_affinity(struct irq_data *d, void *vcpu)
{
	if (vcpu)
		irqd_set_forwarded_to_vcpu(d);
	else
		irqd_clr_forwarded_to_vcpu(d);

	return 0;
}

static int timer_irq_set_irqchip_state(struct irq_data *d,
				       enum irqchip_irq_state which, bool val)
{
	if (which != IRQCHIP_STATE_ACTIVE || !irqd_is_forwarded_to_vcpu(d))
		return irq_chip_set_parent_state(d, which, val);

	if (val)
		irq_chip_mask_parent(d);
	else
		irq_chip_unmask_parent(d);

	return 0;
}

static void timer_irq_eoi(struct irq_data *d)
{
	if (!irqd_is_forwarded_to_vcpu(d))
		irq_chip_eoi_parent(d);
}

static void timer_irq_ack(struct irq_data *d)
{
	d = d->parent_data;
	if (d->chip->irq_ack)
		d->chip->irq_ack(d);
}

static struct irq_chip timer_chip = {
	.name			= "KVM",
	.irq_ack		= timer_irq_ack,
	.irq_mask		= irq_chip_mask_parent,
	.irq_unmask		= irq_chip_unmask_parent,
	.irq_eoi		= timer_irq_eoi,
	.irq_set_type		= irq_chip_set_type_parent,
	.irq_set_vcpu_affinity	= timer_irq_set_vcpu_affinity,
	.irq_set_irqchip_state	= timer_irq_set_irqchip_state,
};

static int timer_irq_domain_alloc(struct irq_domain *domain, unsigned int virq,
				  unsigned int nr_irqs, void *arg)
{
	irq_hw_number_t hwirq = (uintptr_t)arg;

	return irq_domain_set_hwirq_and_chip(domain, virq, hwirq,
					     &timer_chip, NULL);
}

static void timer_irq_domain_free(struct irq_domain *domain, unsigned int virq,
				  unsigned int nr_irqs)
{
}

static const struct irq_domain_ops timer_domain_ops = {
	.alloc	= timer_irq_domain_alloc,
	.free	= timer_irq_domain_free,
};

static struct irq_ops arch_timer_irq_ops = {
	.get_input_level = kvm_arch_timer_get_input_level,
};

static void kvm_irq_fixup_flags(unsigned int virq, u32 *flags)
{
	*flags = irq_get_trigger_type(virq);
	if (*flags != IRQF_TRIGGER_HIGH && *flags != IRQF_TRIGGER_LOW) {
		kvm_err("Invalid trigger for timer IRQ%d, assuming level low\n",
			virq);
		*flags = IRQF_TRIGGER_LOW;
	}
}

static int kvm_irq_init(struct arch_timer_kvm_info *info)
{
	struct irq_domain *domain = NULL;

	if (info->virtual_irq <= 0) {
		kvm_err("kvm_arch_timer: invalid virtual timer IRQ: %d\n",
			info->virtual_irq);
		return -ENODEV;
	}

	host_vtimer_irq = info->virtual_irq;
	kvm_irq_fixup_flags(host_vtimer_irq, &host_vtimer_irq_flags);

	if (kvm_vgic_global_state.no_hw_deactivation) {
		struct fwnode_handle *fwnode;
		struct irq_data *data;

		fwnode = irq_domain_alloc_named_fwnode("kvm-timer");
		if (!fwnode)
			return -ENOMEM;

		/* Assume both vtimer and ptimer in the same parent */
		data = irq_get_irq_data(host_vtimer_irq);
		domain = irq_domain_create_hierarchy(data->domain, 0,
						     NR_KVM_TIMERS, fwnode,
						     &timer_domain_ops, NULL);
		if (!domain) {
			irq_domain_free_fwnode(fwnode);
			return -ENOMEM;
		}

		arch_timer_irq_ops.flags |= VGIC_IRQ_SW_RESAMPLE;
		WARN_ON(irq_domain_push_irq(domain, host_vtimer_irq,
					    (void *)TIMER_VTIMER));
	}

	if (info->physical_irq > 0) {
		host_ptimer_irq = info->physical_irq;
		kvm_irq_fixup_flags(host_ptimer_irq, &host_ptimer_irq_flags);

		if (domain)
			WARN_ON(irq_domain_push_irq(domain, host_ptimer_irq,
						    (void *)TIMER_PTIMER));
	}

	return 0;
}

int __init kvm_timer_hyp_init(bool has_gic)
{
	struct arch_timer_kvm_info *info;
	int err;

	info = arch_timer_get_kvm_info();
	timecounter = &info->timecounter;

	if (!timecounter->cc) {
		kvm_err("kvm_arch_timer: uninitialized timecounter\n");
		return -ENODEV;
	}

	err = kvm_irq_init(info);
	if (err)
		return err;

	/* First, do the virtual EL1 timer irq */

	err = request_percpu_irq(host_vtimer_irq, kvm_arch_timer_handler,
				 "kvm guest vtimer", kvm_get_running_vcpus());
	if (err) {
		kvm_err("kvm_arch_timer: can't request vtimer interrupt %d (%d)\n",
			host_vtimer_irq, err);
		return err;
	}

	if (has_gic) {
		err = irq_set_vcpu_affinity(host_vtimer_irq,
					    kvm_get_running_vcpus());
		if (err) {
			kvm_err("kvm_arch_timer: error setting vcpu affinity\n");
			goto out_free_irq;
		}

		static_branch_enable(&has_gic_active_state);
	}

	kvm_debug("virtual timer IRQ%d\n", host_vtimer_irq);

	/* Now let's do the physical EL1 timer irq */

	if (info->physical_irq > 0) {
		err = request_percpu_irq(host_ptimer_irq, kvm_arch_timer_handler,
					 "kvm guest ptimer", kvm_get_running_vcpus());
		if (err) {
			kvm_err("kvm_arch_timer: can't request ptimer interrupt %d (%d)\n",
				host_ptimer_irq, err);
			return err;
		}

		if (has_gic) {
			err = irq_set_vcpu_affinity(host_ptimer_irq,
						    kvm_get_running_vcpus());
			if (err) {
				kvm_err("kvm_arch_timer: error setting vcpu affinity\n");
				goto out_free_irq;
			}
		}

		kvm_debug("physical timer IRQ%d\n", host_ptimer_irq);
	} else if (has_vhe()) {
		kvm_err("kvm_arch_timer: invalid physical timer IRQ: %d\n",
			info->physical_irq);
		err = -ENODEV;
		goto out_free_irq;
	}

	return 0;
out_free_irq:
	free_percpu_irq(host_vtimer_irq, kvm_get_running_vcpus());
	return err;
}

void kvm_timer_vcpu_terminate(struct kvm_vcpu *vcpu)
{
	struct arch_timer_cpu *timer = vcpu_timer(vcpu);

	soft_timer_cancel(&timer->bg_timer);
}

static bool timer_irqs_are_valid(struct kvm_vcpu *vcpu)
{
	int vtimer_irq, ptimer_irq, ret;
	unsigned long i;

	vtimer_irq = vcpu_vtimer(vcpu)->irq.irq;
	ret = kvm_vgic_set_owner(vcpu, vtimer_irq, vcpu_vtimer(vcpu));
	if (ret)
		return false;

	ptimer_irq = vcpu_ptimer(vcpu)->irq.irq;
	ret = kvm_vgic_set_owner(vcpu, ptimer_irq, vcpu_ptimer(vcpu));
	if (ret)
		return false;

	kvm_for_each_vcpu(i, vcpu, vcpu->kvm) {
		if (vcpu_vtimer(vcpu)->irq.irq != vtimer_irq ||
		    vcpu_ptimer(vcpu)->irq.irq != ptimer_irq)
			return false;
	}

	return true;
}

bool kvm_arch_timer_get_input_level(int vintid)
{
	struct kvm_vcpu *vcpu = kvm_get_running_vcpu();
	struct arch_timer_context *timer;

	if (WARN(!vcpu, "No vcpu context!\n"))
		return false;

	if (vintid == vcpu_vtimer(vcpu)->irq.irq)
		timer = vcpu_vtimer(vcpu);
	else if (vintid == vcpu_ptimer(vcpu)->irq.irq)
		timer = vcpu_ptimer(vcpu);
	else
		BUG();

	return kvm_timer_should_fire(timer);
}

int kvm_timer_enable(struct kvm_vcpu *vcpu)
{
	struct arch_timer_cpu *timer = vcpu_timer(vcpu);
	struct timer_map map;
	int ret;

	if (timer->enabled)
		return 0;

	/* Without a VGIC we do not map virtual IRQs to physical IRQs */
	if (!irqchip_in_kernel(vcpu->kvm))
		goto no_vgic;

	/*
	 * At this stage, we have the guarantee that the vgic is both
	 * available and initialized.
	 */
	if (!timer_irqs_are_valid(vcpu)) {
		kvm_debug("incorrectly configured timer irqs\n");
		return -EINVAL;
	}

	get_timer_map(vcpu, &map);

	ret = kvm_vgic_map_phys_irq(vcpu,
				    map.direct_vtimer->host_timer_irq,
				    map.direct_vtimer->irq.irq,
				    &arch_timer_irq_ops);
	if (ret)
		return ret;

	if (map.direct_ptimer) {
		ret = kvm_vgic_map_phys_irq(vcpu,
					    map.direct_ptimer->host_timer_irq,
					    map.direct_ptimer->irq.irq,
					    &arch_timer_irq_ops);
	}

	if (ret)
		return ret;

no_vgic:
	timer->enabled = 1;
	return 0;
}

/*
 * On VHE system, we only need to configure the EL2 timer trap register once,
 * not for every world switch.
 * The host kernel runs at EL2 with HCR_EL2.TGE == 1,
 * and this makes those bits have no effect for the host kernel execution.
 */
void kvm_timer_init_vhe(void)
{
	/* When HCR_EL2.E2H ==1, EL1PCEN and EL1PCTEN are shifted by 10 */
	u32 cnthctl_shift = 10;
	u64 val;

	/*
	 * VHE systems allow the guest direct access to the EL1 physical
	 * timer/counter.
	 */
	val = read_sysreg(cnthctl_el2);
	val |= (CNTHCTL_EL1PCEN << cnthctl_shift);
	val |= (CNTHCTL_EL1PCTEN << cnthctl_shift);
	write_sysreg(val, cnthctl_el2);
}

static void set_timer_irqs(struct kvm *kvm, int vtimer_irq, int ptimer_irq)
{
	struct kvm_vcpu *vcpu;
	unsigned long i;

	kvm_for_each_vcpu(i, vcpu, kvm) {
		vcpu_vtimer(vcpu)->irq.irq = vtimer_irq;
		vcpu_ptimer(vcpu)->irq.irq = ptimer_irq;
	}
}

int kvm_arm_timer_set_attr(struct kvm_vcpu *vcpu, struct kvm_device_attr *attr)
{
	int __user *uaddr = (int __user *)(long)attr->addr;
	struct arch_timer_context *vtimer = vcpu_vtimer(vcpu);
	struct arch_timer_context *ptimer = vcpu_ptimer(vcpu);
	int irq;

	if (!irqchip_in_kernel(vcpu->kvm))
		return -EINVAL;

	if (get_user(irq, uaddr))
		return -EFAULT;

	if (!(irq_is_ppi(irq)))
		return -EINVAL;

	if (vcpu->arch.timer_cpu.enabled)
		return -EBUSY;

	switch (attr->attr) {
	case KVM_ARM_VCPU_TIMER_IRQ_VTIMER:
		set_timer_irqs(vcpu->kvm, irq, ptimer->irq.irq);
		break;
	case KVM_ARM_VCPU_TIMER_IRQ_PTIMER:
		set_timer_irqs(vcpu->kvm, vtimer->irq.irq, irq);
		break;
	default:
		return -ENXIO;
	}

	return 0;
}

int kvm_arm_timer_get_attr(struct kvm_vcpu *vcpu, struct kvm_device_attr *attr)
{
	int __user *uaddr = (int __user *)(long)attr->addr;
	struct arch_timer_context *timer;
	int irq;

	switch (attr->attr) {
	case KVM_ARM_VCPU_TIMER_IRQ_VTIMER:
		timer = vcpu_vtimer(vcpu);
		break;
	case KVM_ARM_VCPU_TIMER_IRQ_PTIMER:
		timer = vcpu_ptimer(vcpu);
		break;
	default:
		return -ENXIO;
	}

	irq = timer->irq.irq;
	return put_user(irq, uaddr);
}

int kvm_arm_timer_has_attr(struct kvm_vcpu *vcpu, struct kvm_device_attr *attr)
{
	switch (attr->attr) {
	case KVM_ARM_VCPU_TIMER_IRQ_VTIMER:
	case KVM_ARM_VCPU_TIMER_IRQ_PTIMER:
		return 0;
	}

	return -ENXIO;
}
