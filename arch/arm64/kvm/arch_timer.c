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
#include <asm/kvm_nested.h>

#include <kvm/arm_vgic.h>
#include <kvm/arm_arch_timer.h>

#include "trace.h"

static struct timecounter *timecounter;
static unsigned int host_vtimer_irq;
static unsigned int host_ptimer_irq;
static u32 host_vtimer_irq_flags;
static u32 host_ptimer_irq_flags;

static DEFINE_STATIC_KEY_FALSE(has_gic_active_state);
DEFINE_STATIC_KEY_FALSE(broken_cntvoff_key);

static const u8 default_ppi[] = {
	[TIMER_PTIMER]  = 30,
	[TIMER_VTIMER]  = 27,
	[TIMER_HPTIMER] = 26,
	[TIMER_HVTIMER] = 28,
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
static bool kvm_arch_timer_get_input_level(int vintid);

static struct irq_ops arch_timer_irq_ops = {
	.get_input_level = kvm_arch_timer_get_input_level,
};

static int nr_timers(struct kvm_vcpu *vcpu)
{
	if (!vcpu_has_nv(vcpu))
		return NR_KVM_EL0_TIMERS;

	return NR_KVM_TIMERS;
}

u32 timer_get_ctl(struct arch_timer_context *ctxt)
{
	struct kvm_vcpu *vcpu = timer_context_to_vcpu(ctxt);

	switch(arch_timer_ctx_index(ctxt)) {
	case TIMER_VTIMER:
		return __vcpu_sys_reg(vcpu, CNTV_CTL_EL0);
	case TIMER_PTIMER:
		return __vcpu_sys_reg(vcpu, CNTP_CTL_EL0);
	case TIMER_HVTIMER:
		return __vcpu_sys_reg(vcpu, CNTHV_CTL_EL2);
	case TIMER_HPTIMER:
		return __vcpu_sys_reg(vcpu, CNTHP_CTL_EL2);
	default:
		WARN_ON(1);
		return 0;
	}
}

u64 timer_get_cval(struct arch_timer_context *ctxt)
{
	struct kvm_vcpu *vcpu = timer_context_to_vcpu(ctxt);

	switch(arch_timer_ctx_index(ctxt)) {
	case TIMER_VTIMER:
		return __vcpu_sys_reg(vcpu, CNTV_CVAL_EL0);
	case TIMER_PTIMER:
		return __vcpu_sys_reg(vcpu, CNTP_CVAL_EL0);
	case TIMER_HVTIMER:
		return __vcpu_sys_reg(vcpu, CNTHV_CVAL_EL2);
	case TIMER_HPTIMER:
		return __vcpu_sys_reg(vcpu, CNTHP_CVAL_EL2);
	default:
		WARN_ON(1);
		return 0;
	}
}

static void timer_set_ctl(struct arch_timer_context *ctxt, u32 ctl)
{
	struct kvm_vcpu *vcpu = timer_context_to_vcpu(ctxt);

	switch(arch_timer_ctx_index(ctxt)) {
	case TIMER_VTIMER:
		__vcpu_assign_sys_reg(vcpu, CNTV_CTL_EL0, ctl);
		break;
	case TIMER_PTIMER:
		__vcpu_assign_sys_reg(vcpu, CNTP_CTL_EL0, ctl);
		break;
	case TIMER_HVTIMER:
		__vcpu_assign_sys_reg(vcpu, CNTHV_CTL_EL2, ctl);
		break;
	case TIMER_HPTIMER:
		__vcpu_assign_sys_reg(vcpu, CNTHP_CTL_EL2, ctl);
		break;
	default:
		WARN_ON(1);
	}
}

static void timer_set_cval(struct arch_timer_context *ctxt, u64 cval)
{
	struct kvm_vcpu *vcpu = timer_context_to_vcpu(ctxt);

	switch(arch_timer_ctx_index(ctxt)) {
	case TIMER_VTIMER:
		__vcpu_assign_sys_reg(vcpu, CNTV_CVAL_EL0, cval);
		break;
	case TIMER_PTIMER:
		__vcpu_assign_sys_reg(vcpu, CNTP_CVAL_EL0, cval);
		break;
	case TIMER_HVTIMER:
		__vcpu_assign_sys_reg(vcpu, CNTHV_CVAL_EL2, cval);
		break;
	case TIMER_HPTIMER:
		__vcpu_assign_sys_reg(vcpu, CNTHP_CVAL_EL2, cval);
		break;
	default:
		WARN_ON(1);
	}
}

u64 kvm_phys_timer_read(void)
{
	return timecounter->cc->read(timecounter->cc);
}

void get_timer_map(struct kvm_vcpu *vcpu, struct timer_map *map)
{
	if (vcpu_has_nv(vcpu)) {
		if (is_hyp_ctxt(vcpu)) {
			map->direct_vtimer = vcpu_hvtimer(vcpu);
			map->direct_ptimer = vcpu_hptimer(vcpu);
			map->emul_vtimer = vcpu_vtimer(vcpu);
			map->emul_ptimer = vcpu_ptimer(vcpu);
		} else {
			map->direct_vtimer = vcpu_vtimer(vcpu);
			map->direct_ptimer = vcpu_ptimer(vcpu);
			map->emul_vtimer = vcpu_hvtimer(vcpu);
			map->emul_ptimer = vcpu_hptimer(vcpu);
		}
	} else if (has_vhe()) {
		map->direct_vtimer = vcpu_vtimer(vcpu);
		map->direct_ptimer = vcpu_ptimer(vcpu);
		map->emul_vtimer = NULL;
		map->emul_ptimer = NULL;
	} else {
		map->direct_vtimer = vcpu_vtimer(vcpu);
		map->direct_ptimer = NULL;
		map->emul_vtimer = NULL;
		map->emul_ptimer = vcpu_ptimer(vcpu);
	}

	trace_kvm_get_timer_map(vcpu->vcpu_id, map);
}

static inline bool userspace_irqchip(struct kvm *kvm)
{
	return unlikely(!irqchip_in_kernel(kvm));
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
					 &timer_ctx->ns_frac);
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
	u64 val = vcpu_get_reg(vcpu, kvm_vcpu_sys_get_rt(vcpu));
	struct arch_timer_context *ctx;

	ctx = is_hyp_ctxt(vcpu) ? vcpu_hvtimer(vcpu) : vcpu_vtimer(vcpu);

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

	for (i = 0; i < nr_timers(vcpu); i++) {
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
	vcpu = timer_context_to_vcpu(ctx);

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
		case TIMER_HVTIMER:
			cnt_ctl = read_sysreg_el0(SYS_CNTV_CTL);
			break;
		case TIMER_PTIMER:
		case TIMER_HPTIMER:
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

static void kvm_timer_update_status(struct arch_timer_context *ctx, bool level)
{
	/*
	 * Paper over NV2 brokenness by publishing the interrupt status
	 * bit. This still results in a poor quality of emulation (guest
	 * writes will have no effect until the next exit).
	 *
	 * But hey, it's fast, right?
	 */
	struct kvm_vcpu *vcpu = timer_context_to_vcpu(ctx);
	if (is_hyp_ctxt(vcpu) &&
	    (ctx == vcpu_vtimer(vcpu) || ctx == vcpu_ptimer(vcpu))) {
		unsigned long val = timer_get_ctl(ctx);
		__assign_bit(__ffs(ARCH_TIMER_CTRL_IT_STAT), &val, level);
		timer_set_ctl(ctx, val);
	}
}

static void kvm_timer_update_irq(struct kvm_vcpu *vcpu, bool new_level,
				 struct arch_timer_context *timer_ctx)
{
	kvm_timer_update_status(timer_ctx, new_level);

	timer_ctx->irq.level = new_level;
	trace_kvm_timer_update_irq(vcpu->vcpu_id, timer_irq(timer_ctx),
				   timer_ctx->irq.level);

	if (userspace_irqchip(vcpu->kvm))
		return;

	kvm_vgic_inject_irq(vcpu->kvm, vcpu,
			    timer_irq(timer_ctx),
			    timer_ctx->irq.level,
			    timer_ctx);
}

/* Only called for a fully emulated timer */
static void timer_emulate(struct arch_timer_context *ctx)
{
	bool should_fire = kvm_timer_should_fire(ctx);

	trace_kvm_timer_emulate(ctx, should_fire);

	if (should_fire != ctx->irq.level)
		kvm_timer_update_irq(timer_context_to_vcpu(ctx), should_fire, ctx);

	kvm_timer_update_status(ctx, should_fire);

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

static void set_cntpoff(u64 cntpoff)
{
	if (has_cntpoff())
		write_sysreg_s(cntpoff, SYS_CNTPOFF_EL2);
}

static void timer_save_state(struct arch_timer_context *ctx)
{
	struct arch_timer_cpu *timer = vcpu_timer(timer_context_to_vcpu(ctx));
	enum kvm_arch_timers index = arch_timer_ctx_index(ctx);
	unsigned long flags;

	if (!timer->enabled)
		return;

	local_irq_save(flags);

	if (!ctx->loaded)
		goto out;

	switch (index) {
		u64 cval;

	case TIMER_VTIMER:
	case TIMER_HVTIMER:
		timer_set_ctl(ctx, read_sysreg_el0(SYS_CNTV_CTL));
		cval = read_sysreg_el0(SYS_CNTV_CVAL);

		if (has_broken_cntvoff())
			cval -= timer_get_offset(ctx);

		timer_set_cval(ctx, cval);

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
	case TIMER_HPTIMER:
		timer_set_ctl(ctx, read_sysreg_el0(SYS_CNTP_CTL));
		cval = read_sysreg_el0(SYS_CNTP_CVAL);

		cval -= timer_get_offset(ctx);

		timer_set_cval(ctx, cval);

		/* Disable the timer */
		write_sysreg_el0(0, SYS_CNTP_CTL);
		isb();

		set_cntpoff(0);
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
	    !kvm_timer_irq_can_fire(map.emul_vtimer) &&
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
	struct arch_timer_cpu *timer = vcpu_timer(timer_context_to_vcpu(ctx));
	enum kvm_arch_timers index = arch_timer_ctx_index(ctx);
	unsigned long flags;

	if (!timer->enabled)
		return;

	local_irq_save(flags);

	if (ctx->loaded)
		goto out;

	switch (index) {
		u64 cval, offset;

	case TIMER_VTIMER:
	case TIMER_HVTIMER:
		cval = timer_get_cval(ctx);
		offset = timer_get_offset(ctx);
		if (has_broken_cntvoff()) {
			set_cntvoff(0);
			cval += offset;
		} else {
			set_cntvoff(offset);
		}
		write_sysreg_el0(cval, SYS_CNTV_CVAL);
		isb();
		write_sysreg_el0(timer_get_ctl(ctx), SYS_CNTV_CTL);
		break;
	case TIMER_PTIMER:
	case TIMER_HPTIMER:
		cval = timer_get_cval(ctx);
		offset = timer_get_offset(ctx);
		set_cntpoff(offset);
		cval += offset;
		write_sysreg_el0(cval, SYS_CNTP_CVAL);
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
	struct kvm_vcpu *vcpu = timer_context_to_vcpu(ctx);
	bool phys_active = false;

	/*
	 * Update the timer output so that it is likely to match the
	 * state we're about to restore. If the timer expires between
	 * this point and the register restoration, we'll take the
	 * interrupt anyway.
	 */
	kvm_timer_update_irq(vcpu, kvm_timer_should_fire(ctx), ctx);

	if (irqchip_in_kernel(vcpu->kvm))
		phys_active = kvm_vgic_map_is_active(vcpu, timer_irq(ctx));

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

/* If _pred is true, set bit in _set, otherwise set it in _clr */
#define assign_clear_set_bit(_pred, _bit, _clr, _set)			\
	do {								\
		if (_pred)						\
			(_set) |= (_bit);				\
		else							\
			(_clr) |= (_bit);				\
	} while (0)

static void kvm_timer_vcpu_load_nested_switch(struct kvm_vcpu *vcpu,
					      struct timer_map *map)
{
	int hw, ret;

	if (!irqchip_in_kernel(vcpu->kvm))
		return;

	/*
	 * We only ever unmap the vtimer irq on a VHE system that runs nested
	 * virtualization, in which case we have both a valid emul_vtimer,
	 * emul_ptimer, direct_vtimer, and direct_ptimer.
	 *
	 * Since this is called from kvm_timer_vcpu_load(), a change between
	 * vEL2 and vEL1/0 will have just happened, and the timer_map will
	 * represent this, and therefore we switch the emul/direct mappings
	 * below.
	 */
	hw = kvm_vgic_get_map(vcpu, timer_irq(map->direct_vtimer));
	if (hw < 0) {
		kvm_vgic_unmap_phys_irq(vcpu, timer_irq(map->emul_vtimer));
		kvm_vgic_unmap_phys_irq(vcpu, timer_irq(map->emul_ptimer));

		ret = kvm_vgic_map_phys_irq(vcpu,
					    map->direct_vtimer->host_timer_irq,
					    timer_irq(map->direct_vtimer),
					    &arch_timer_irq_ops);
		WARN_ON_ONCE(ret);
		ret = kvm_vgic_map_phys_irq(vcpu,
					    map->direct_ptimer->host_timer_irq,
					    timer_irq(map->direct_ptimer),
					    &arch_timer_irq_ops);
		WARN_ON_ONCE(ret);
	}
}

static void timer_set_traps(struct kvm_vcpu *vcpu, struct timer_map *map)
{
	bool tvt, tpt, tvc, tpc, tvt02, tpt02;
	u64 clr, set;

	/*
	 * No trapping gets configured here with nVHE. See
	 * __timer_enable_traps(), which is where the stuff happens.
	 */
	if (!has_vhe())
		return;

	/*
	 * Our default policy is not to trap anything. As we progress
	 * within this function, reality kicks in and we start adding
	 * traps based on emulation requirements.
	 */
	tvt = tpt = tvc = tpc = false;
	tvt02 = tpt02 = false;

	/*
	 * NV2 badly breaks the timer semantics by redirecting accesses to
	 * the EL1 timer state to memory, so let's call ECV to the rescue if
	 * available: we trap all CNT{P,V}_{CTL,CVAL,TVAL}_EL0 accesses.
	 *
	 * The treatment slightly varies depending whether we run a nVHE or
	 * VHE guest: nVHE will use the _EL0 registers directly, while VHE
	 * will use the _EL02 accessors. This translates in different trap
	 * bits.
	 *
	 * None of the trapping is required when running in non-HYP context,
	 * unless required by the L1 hypervisor settings once we advertise
	 * ECV+NV in the guest, or that we need trapping for other reasons.
	 */
	if (cpus_have_final_cap(ARM64_HAS_ECV) && is_hyp_ctxt(vcpu)) {
		if (vcpu_el2_e2h_is_set(vcpu))
			tvt02 = tpt02 = true;
		else
			tvt = tpt = true;
	}

	/*
	 * We have two possibility to deal with a physical offset:
	 *
	 * - Either we have CNTPOFF (yay!) or the offset is 0:
	 *   we let the guest freely access the HW
	 *
	 * - or neither of these condition apply:
	 *   we trap accesses to the HW, but still use it
	 *   after correcting the physical offset
	 */
	if (!has_cntpoff() && timer_get_offset(map->direct_ptimer))
		tpt = tpc = true;

	/*
	 * For the poor sods that could not correctly substract one value
	 * from another, trap the full virtual timer and counter.
	 */
	if (has_broken_cntvoff() && timer_get_offset(map->direct_vtimer))
		tvt = tvc = true;

	/*
	 * Apply the enable bits that the guest hypervisor has requested for
	 * its own guest. We can only add traps that wouldn't have been set
	 * above.
	 * Implementation choices: we do not support NV when E2H=0 in the
	 * guest, and we don't support configuration where E2H is writable
	 * by the guest (either FEAT_VHE or FEAT_E2H0 is implemented, but
	 * not both). This simplifies the handling of the EL1NV* bits.
	 */
	if (is_nested_ctxt(vcpu)) {
		u64 val = __vcpu_sys_reg(vcpu, CNTHCTL_EL2);

		/* Use the VHE format for mental sanity */
		if (!vcpu_el2_e2h_is_set(vcpu))
			val = (val & (CNTHCTL_EL1PCEN | CNTHCTL_EL1PCTEN)) << 10;

		tpt |= !(val & (CNTHCTL_EL1PCEN << 10));
		tpc |= !(val & (CNTHCTL_EL1PCTEN << 10));

		tpt02 |= (val & CNTHCTL_EL1NVPCT);
		tvt02 |= (val & CNTHCTL_EL1NVVCT);
	}

	/*
	 * Now that we have collected our requirements, compute the
	 * trap and enable bits.
	 */
	set = 0;
	clr = 0;

	assign_clear_set_bit(tpt, CNTHCTL_EL1PCEN << 10, set, clr);
	assign_clear_set_bit(tpc, CNTHCTL_EL1PCTEN << 10, set, clr);
	assign_clear_set_bit(tvt, CNTHCTL_EL1TVT, clr, set);
	assign_clear_set_bit(tvc, CNTHCTL_EL1TVCT, clr, set);
	assign_clear_set_bit(tvt02, CNTHCTL_EL1NVVCT, clr, set);
	assign_clear_set_bit(tpt02, CNTHCTL_EL1NVPCT, clr, set);

	/* This only happens on VHE, so use the CNTHCTL_EL2 accessor. */
	sysreg_clear_set(cnthctl_el2, clr, set);
}

void kvm_timer_vcpu_load(struct kvm_vcpu *vcpu)
{
	struct arch_timer_cpu *timer = vcpu_timer(vcpu);
	struct timer_map map;

	if (unlikely(!timer->enabled))
		return;

	get_timer_map(vcpu, &map);

	if (static_branch_likely(&has_gic_active_state)) {
		if (vcpu_has_nv(vcpu))
			kvm_timer_vcpu_load_nested_switch(vcpu, &map);

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
	if (map.emul_vtimer)
		timer_emulate(map.emul_vtimer);
	if (map.emul_ptimer)
		timer_emulate(map.emul_ptimer);

	timer_set_traps(vcpu, &map);
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
	if (map.emul_vtimer)
		soft_timer_cancel(&map.emul_vtimer->hrtimer);
	if (map.emul_ptimer)
		soft_timer_cancel(&map.emul_ptimer->hrtimer);

	if (kvm_vcpu_is_blocking(vcpu))
		kvm_timer_blocking(vcpu);
}

void kvm_timer_sync_nested(struct kvm_vcpu *vcpu)
{
	/*
	 * When NV2 is on, guest hypervisors have their EL1 timer register
	 * accesses redirected to the VNCR page. Any guest action taken on
	 * the timer is postponed until the next exit, leading to a very
	 * poor quality of emulation.
	 *
	 * This is an unmitigated disaster, only papered over by FEAT_ECV,
	 * which allows trapping of the timer registers even with NV2.
	 * Still, this is still worse than FEAT_NV on its own. Meh.
	 */
	if (!cpus_have_final_cap(ARM64_HAS_ECV)) {
		/*
		 * For a VHE guest hypervisor, the EL2 state is directly
		 * stored in the host EL1 timers, while the emulated EL1
		 * state is stored in the VNCR page. The latter could have
		 * been updated behind our back, and we must reset the
		 * emulation of the timers.
		 *
		 * A non-VHE guest hypervisor doesn't have any direct access
		 * to its timers: the EL2 registers trap despite being
		 * notionally direct (we use the EL1 HW, as for VHE), while
		 * the EL1 registers access memory.
		 *
		 * In both cases, process the emulated timers on each guest
		 * exit. Boo.
		 */
		struct timer_map map;
		get_timer_map(vcpu, &map);

		soft_timer_cancel(&map.emul_vtimer->hrtimer);
		soft_timer_cancel(&map.emul_ptimer->hrtimer);
		timer_emulate(map.emul_vtimer);
		timer_emulate(map.emul_ptimer);
	}
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

void kvm_timer_vcpu_reset(struct kvm_vcpu *vcpu)
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
	for (int i = 0; i < nr_timers(vcpu); i++)
		timer_set_ctl(vcpu_get_timer(vcpu, i), 0);

	/*
	 * A vcpu running at EL2 is in charge of the offset applied to
	 * the virtual timer, so use the physical VM offset, and point
	 * the vcpu offset to CNTVOFF_EL2.
	 */
	if (vcpu_has_nv(vcpu)) {
		struct arch_timer_offset *offs = &vcpu_vtimer(vcpu)->offset;

		offs->vcpu_offset = __ctxt_sys_reg(&vcpu->arch.ctxt, CNTVOFF_EL2);
		offs->vm_offset = &vcpu->kvm->arch.timer_data.poffset;
	}

	if (timer->enabled) {
		for (int i = 0; i < nr_timers(vcpu); i++)
			kvm_timer_update_irq(vcpu, false,
					     vcpu_get_timer(vcpu, i));

		if (irqchip_in_kernel(vcpu->kvm)) {
			kvm_vgic_reset_mapped_irq(vcpu, timer_irq(map.direct_vtimer));
			if (map.direct_ptimer)
				kvm_vgic_reset_mapped_irq(vcpu, timer_irq(map.direct_ptimer));
		}
	}

	if (map.emul_vtimer)
		soft_timer_cancel(&map.emul_vtimer->hrtimer);
	if (map.emul_ptimer)
		soft_timer_cancel(&map.emul_ptimer->hrtimer);
}

static void timer_context_init(struct kvm_vcpu *vcpu, int timerid)
{
	struct arch_timer_context *ctxt = vcpu_get_timer(vcpu, timerid);
	struct kvm *kvm = vcpu->kvm;

	ctxt->timer_id = timerid;

	if (timerid == TIMER_VTIMER)
		ctxt->offset.vm_offset = &kvm->arch.timer_data.voffset;
	else
		ctxt->offset.vm_offset = &kvm->arch.timer_data.poffset;

	hrtimer_setup(&ctxt->hrtimer, kvm_hrtimer_expire, CLOCK_MONOTONIC, HRTIMER_MODE_ABS_HARD);

	switch (timerid) {
	case TIMER_PTIMER:
	case TIMER_HPTIMER:
		ctxt->host_timer_irq = host_ptimer_irq;
		break;
	case TIMER_VTIMER:
	case TIMER_HVTIMER:
		ctxt->host_timer_irq = host_vtimer_irq;
		break;
	}
}

void kvm_timer_vcpu_init(struct kvm_vcpu *vcpu)
{
	struct arch_timer_cpu *timer = vcpu_timer(vcpu);

	for (int i = 0; i < NR_KVM_TIMERS; i++)
		timer_context_init(vcpu, i);

	/* Synchronize offsets across timers of a VM if not already provided */
	if (!test_bit(KVM_ARCH_FLAG_VM_COUNTER_OFFSET, &vcpu->kvm->arch.flags)) {
		timer_set_offset(vcpu_vtimer(vcpu), kvm_phys_timer_read());
		timer_set_offset(vcpu_ptimer(vcpu), 0);
	}

	hrtimer_setup(&timer->bg_timer, kvm_bg_timer_expire, CLOCK_MONOTONIC,
		      HRTIMER_MODE_ABS_HARD);
}

void kvm_timer_init_vm(struct kvm *kvm)
{
	for (int i = 0; i < NR_KVM_TIMERS; i++)
		kvm->arch.timer_data.ppi[i] = default_ppi[i];
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

	case TIMER_REG_VOFF:
		val = *timer->offset.vcpu_offset;
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

	if (timer == map.emul_vtimer || timer == map.emul_ptimer)
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

	case TIMER_REG_VOFF:
		*timer->offset.vcpu_offset = val;
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
	if (timer == map.emul_vtimer || timer == map.emul_ptimer) {
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

static void kvm_timer_handle_errata(void)
{
	u64 mmfr0, mmfr1, mmfr4;

	/*
	 * CNTVOFF_EL2 is broken on some implementations. For those, we trap
	 * all virtual timer/counter accesses, requiring FEAT_ECV.
	 *
	 * However, a hypervisor supporting nesting is likely to mitigate the
	 * erratum at L0, and not require other levels to mitigate it (which
	 * would otherwise be a terrible performance sink due to trap
	 * amplification).
	 *
	 * Given that the affected HW implements both FEAT_VHE and FEAT_E2H0,
	 * and that NV is likely not to (because of limitations of the
	 * architecture), only enable the workaround when FEAT_VHE and
	 * FEAT_E2H0 are both detected. Time will tell if this actually holds.
	 */
	mmfr0 = read_sanitised_ftr_reg(SYS_ID_AA64MMFR0_EL1);
	mmfr1 = read_sanitised_ftr_reg(SYS_ID_AA64MMFR1_EL1);
	mmfr4 = read_sanitised_ftr_reg(SYS_ID_AA64MMFR4_EL1);
	if (SYS_FIELD_GET(ID_AA64MMFR1_EL1, VH, mmfr1)		&&
	    !SYS_FIELD_GET(ID_AA64MMFR4_EL1, E2H0, mmfr4)	&&
	    SYS_FIELD_GET(ID_AA64MMFR0_EL1, ECV, mmfr0)		&&
	    (has_vhe() || has_hvhe())				&&
	    cpus_have_final_cap(ARM64_WORKAROUND_QCOM_ORYON_CNTVOFF)) {
		static_branch_enable(&broken_cntvoff_key);
		kvm_info("Broken CNTVOFF_EL2, trapping virtual timer\n");
	}
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
			goto out_free_vtimer_irq;
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
			goto out_free_vtimer_irq;
		}

		if (has_gic) {
			err = irq_set_vcpu_affinity(host_ptimer_irq,
						    kvm_get_running_vcpus());
			if (err) {
				kvm_err("kvm_arch_timer: error setting vcpu affinity\n");
				goto out_free_ptimer_irq;
			}
		}

		kvm_debug("physical timer IRQ%d\n", host_ptimer_irq);
	} else if (has_vhe()) {
		kvm_err("kvm_arch_timer: invalid physical timer IRQ: %d\n",
			info->physical_irq);
		err = -ENODEV;
		goto out_free_vtimer_irq;
	}

	kvm_timer_handle_errata();
	return 0;

out_free_ptimer_irq:
	if (info->physical_irq > 0)
		free_percpu_irq(host_ptimer_irq, kvm_get_running_vcpus());
out_free_vtimer_irq:
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
	u32 ppis = 0;
	bool valid;

	mutex_lock(&vcpu->kvm->arch.config_lock);

	for (int i = 0; i < nr_timers(vcpu); i++) {
		struct arch_timer_context *ctx;
		int irq;

		ctx = vcpu_get_timer(vcpu, i);
		irq = timer_irq(ctx);
		if (kvm_vgic_set_owner(vcpu, irq, ctx))
			break;

		/*
		 * We know by construction that we only have PPIs, so
		 * all values are less than 32.
		 */
		ppis |= BIT(irq);
	}

	valid = hweight32(ppis) == nr_timers(vcpu);

	if (valid)
		set_bit(KVM_ARCH_FLAG_TIMER_PPIS_IMMUTABLE, &vcpu->kvm->arch.flags);

	mutex_unlock(&vcpu->kvm->arch.config_lock);

	return valid;
}

static bool kvm_arch_timer_get_input_level(int vintid)
{
	struct kvm_vcpu *vcpu = kvm_get_running_vcpu();

	if (WARN(!vcpu, "No vcpu context!\n"))
		return false;

	for (int i = 0; i < nr_timers(vcpu); i++) {
		struct arch_timer_context *ctx;

		ctx = vcpu_get_timer(vcpu, i);
		if (timer_irq(ctx) == vintid)
			return kvm_timer_should_fire(ctx);
	}

	/* A timer IRQ has fired, but no matching timer was found? */
	WARN_RATELIMIT(1, "timer INTID%d unknown\n", vintid);

	return false;
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
				    timer_irq(map.direct_vtimer),
				    &arch_timer_irq_ops);
	if (ret)
		return ret;

	if (map.direct_ptimer) {
		ret = kvm_vgic_map_phys_irq(vcpu,
					    map.direct_ptimer->host_timer_irq,
					    timer_irq(map.direct_ptimer),
					    &arch_timer_irq_ops);
	}

	if (ret)
		return ret;

no_vgic:
	timer->enabled = 1;
	return 0;
}

/* If we have CNTPOFF, permanently set ECV to enable it */
void kvm_timer_init_vhe(void)
{
	if (cpus_have_final_cap(ARM64_HAS_ECV_CNTPOFF))
		sysreg_clear_set(cnthctl_el2, 0, CNTHCTL_ECV);
}

int kvm_arm_timer_set_attr(struct kvm_vcpu *vcpu, struct kvm_device_attr *attr)
{
	int __user *uaddr = (int __user *)(long)attr->addr;
	int irq, idx, ret = 0;

	if (!irqchip_in_kernel(vcpu->kvm))
		return -EINVAL;

	if (get_user(irq, uaddr))
		return -EFAULT;

	if (!(irq_is_ppi(irq)))
		return -EINVAL;

	mutex_lock(&vcpu->kvm->arch.config_lock);

	if (test_bit(KVM_ARCH_FLAG_TIMER_PPIS_IMMUTABLE,
		     &vcpu->kvm->arch.flags)) {
		ret = -EBUSY;
		goto out;
	}

	switch (attr->attr) {
	case KVM_ARM_VCPU_TIMER_IRQ_VTIMER:
		idx = TIMER_VTIMER;
		break;
	case KVM_ARM_VCPU_TIMER_IRQ_PTIMER:
		idx = TIMER_PTIMER;
		break;
	case KVM_ARM_VCPU_TIMER_IRQ_HVTIMER:
		idx = TIMER_HVTIMER;
		break;
	case KVM_ARM_VCPU_TIMER_IRQ_HPTIMER:
		idx = TIMER_HPTIMER;
		break;
	default:
		ret = -ENXIO;
		goto out;
	}

	/*
	 * We cannot validate the IRQ unicity before we run, so take it at
	 * face value. The verdict will be given on first vcpu run, for each
	 * vcpu. Yes this is late. Blame it on the stupid API.
	 */
	vcpu->kvm->arch.timer_data.ppi[idx] = irq;

out:
	mutex_unlock(&vcpu->kvm->arch.config_lock);
	return ret;
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
	case KVM_ARM_VCPU_TIMER_IRQ_HVTIMER:
		timer = vcpu_hvtimer(vcpu);
		break;
	case KVM_ARM_VCPU_TIMER_IRQ_HPTIMER:
		timer = vcpu_hptimer(vcpu);
		break;
	default:
		return -ENXIO;
	}

	irq = timer_irq(timer);
	return put_user(irq, uaddr);
}

int kvm_arm_timer_has_attr(struct kvm_vcpu *vcpu, struct kvm_device_attr *attr)
{
	switch (attr->attr) {
	case KVM_ARM_VCPU_TIMER_IRQ_VTIMER:
	case KVM_ARM_VCPU_TIMER_IRQ_PTIMER:
	case KVM_ARM_VCPU_TIMER_IRQ_HVTIMER:
	case KVM_ARM_VCPU_TIMER_IRQ_HPTIMER:
		return 0;
	}

	return -ENXIO;
}

int kvm_vm_ioctl_set_counter_offset(struct kvm *kvm,
				    struct kvm_arm_counter_offset *offset)
{
	int ret = 0;

	if (offset->reserved)
		return -EINVAL;

	mutex_lock(&kvm->lock);

	if (!kvm_trylock_all_vcpus(kvm)) {
		set_bit(KVM_ARCH_FLAG_VM_COUNTER_OFFSET, &kvm->arch.flags);

		/*
		 * If userspace decides to set the offset using this
		 * API rather than merely restoring the counter
		 * values, the offset applies to both the virtual and
		 * physical views.
		 */
		kvm->arch.timer_data.voffset = offset->counter_offset;
		kvm->arch.timer_data.poffset = offset->counter_offset;

		kvm_unlock_all_vcpus(kvm);
	} else {
		ret = -EBUSY;
	}

	mutex_unlock(&kvm->lock);

	return ret;
}
