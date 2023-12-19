// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020-2023 Loongson Technology Corporation Limited
 */

#include <linux/kvm_host.h>
#include <asm/kvm_csr.h>
#include <asm/kvm_vcpu.h>

/*
 * ktime_to_tick() - Scale ktime_t to timer tick value.
 */
static inline u64 ktime_to_tick(struct kvm_vcpu *vcpu, ktime_t now)
{
	u64 delta;

	delta = ktime_to_ns(now);
	return div_u64(delta * vcpu->arch.timer_mhz, MNSEC_PER_SEC);
}

static inline u64 tick_to_ns(struct kvm_vcpu *vcpu, u64 tick)
{
	return div_u64(tick * MNSEC_PER_SEC, vcpu->arch.timer_mhz);
}

/*
 * Push timer forward on timeout.
 * Handle an hrtimer event by push the hrtimer forward a period.
 */
static enum hrtimer_restart kvm_count_timeout(struct kvm_vcpu *vcpu)
{
	unsigned long cfg, period;

	/* Add periodic tick to current expire time */
	cfg = kvm_read_sw_gcsr(vcpu->arch.csr, LOONGARCH_CSR_TCFG);
	if (cfg & CSR_TCFG_PERIOD) {
		period = tick_to_ns(vcpu, cfg & CSR_TCFG_VAL);
		hrtimer_add_expires_ns(&vcpu->arch.swtimer, period);
		return HRTIMER_RESTART;
	} else
		return HRTIMER_NORESTART;
}

/* Low level hrtimer wake routine */
enum hrtimer_restart kvm_swtimer_wakeup(struct hrtimer *timer)
{
	struct kvm_vcpu *vcpu;

	vcpu = container_of(timer, struct kvm_vcpu, arch.swtimer);
	kvm_queue_irq(vcpu, INT_TI);
	rcuwait_wake_up(&vcpu->wait);

	return kvm_count_timeout(vcpu);
}

/*
 * Initialise the timer to the specified frequency, zero it
 */
void kvm_init_timer(struct kvm_vcpu *vcpu, unsigned long timer_hz)
{
	vcpu->arch.timer_mhz = timer_hz >> 20;

	/* Starting at 0 */
	kvm_write_sw_gcsr(vcpu->arch.csr, LOONGARCH_CSR_TVAL, 0);
}

/*
 * Restore hard timer state and enable guest to access timer registers
 * without trap, should be called with irq disabled
 */
void kvm_acquire_timer(struct kvm_vcpu *vcpu)
{
	unsigned long cfg;

	cfg = read_csr_gcfg();
	if (!(cfg & CSR_GCFG_TIT))
		return;

	/* Enable guest access to hard timer */
	write_csr_gcfg(cfg & ~CSR_GCFG_TIT);

	/*
	 * Freeze the soft-timer and sync the guest stable timer with it. We do
	 * this with interrupts disabled to avoid latency.
	 */
	hrtimer_cancel(&vcpu->arch.swtimer);
}

/*
 * Restore soft timer state from saved context.
 */
void kvm_restore_timer(struct kvm_vcpu *vcpu)
{
	unsigned long cfg, delta, period;
	ktime_t expire, now;
	struct loongarch_csrs *csr = vcpu->arch.csr;

	/*
	 * Set guest stable timer cfg csr
	 */
	cfg = kvm_read_sw_gcsr(csr, LOONGARCH_CSR_TCFG);
	kvm_restore_hw_gcsr(csr, LOONGARCH_CSR_ESTAT);
	kvm_restore_hw_gcsr(csr, LOONGARCH_CSR_TCFG);
	if (!(cfg & CSR_TCFG_EN)) {
		/* Guest timer is disabled, just restore timer registers */
		kvm_restore_hw_gcsr(csr, LOONGARCH_CSR_TVAL);
		return;
	}

	/*
	 * Set remainder tick value if not expired
	 */
	now = ktime_get();
	expire = vcpu->arch.expire;
	if (ktime_before(now, expire))
		delta = ktime_to_tick(vcpu, ktime_sub(expire, now));
	else {
		if (cfg & CSR_TCFG_PERIOD) {
			period = cfg & CSR_TCFG_VAL;
			delta = ktime_to_tick(vcpu, ktime_sub(now, expire));
			delta = period - (delta % period);
		} else
			delta = 0;
		/*
		 * Inject timer here though sw timer should inject timer
		 * interrupt async already, since sw timer may be cancelled
		 * during injecting intr async in function kvm_acquire_timer
		 */
		kvm_queue_irq(vcpu, INT_TI);
	}

	write_gcsr_timertick(delta);
}

/*
 * Save guest timer state and switch to software emulation of guest
 * timer. The hard timer must already be in use, so preemption should be
 * disabled.
 */
static void _kvm_save_timer(struct kvm_vcpu *vcpu)
{
	unsigned long ticks, delta;
	ktime_t expire;
	struct loongarch_csrs *csr = vcpu->arch.csr;

	ticks = kvm_read_sw_gcsr(csr, LOONGARCH_CSR_TVAL);
	delta = tick_to_ns(vcpu, ticks);
	expire = ktime_add_ns(ktime_get(), delta);
	vcpu->arch.expire = expire;
	if (ticks) {
		/*
		 * Update hrtimer to use new timeout
		 * HRTIMER_MODE_PINNED is suggested since vcpu may run in
		 * the same physical cpu in next time
		 */
		hrtimer_cancel(&vcpu->arch.swtimer);
		hrtimer_start(&vcpu->arch.swtimer, expire, HRTIMER_MODE_ABS_PINNED);
	} else
		/*
		 * Inject timer interrupt so that hall polling can dectect and exit
		 */
		kvm_queue_irq(vcpu, INT_TI);
}

/*
 * Save guest timer state and switch to soft guest timer if hard timer was in
 * use.
 */
void kvm_save_timer(struct kvm_vcpu *vcpu)
{
	unsigned long cfg;
	struct loongarch_csrs *csr = vcpu->arch.csr;

	preempt_disable();
	cfg = read_csr_gcfg();
	if (!(cfg & CSR_GCFG_TIT)) {
		/* Disable guest use of hard timer */
		write_csr_gcfg(cfg | CSR_GCFG_TIT);

		/* Save hard timer state */
		kvm_save_hw_gcsr(csr, LOONGARCH_CSR_TCFG);
		kvm_save_hw_gcsr(csr, LOONGARCH_CSR_TVAL);
		if (kvm_read_sw_gcsr(csr, LOONGARCH_CSR_TCFG) & CSR_TCFG_EN)
			_kvm_save_timer(vcpu);
	}

	/* Save timer-related state to vCPU context */
	kvm_save_hw_gcsr(csr, LOONGARCH_CSR_ESTAT);
	preempt_enable();
}

void kvm_reset_timer(struct kvm_vcpu *vcpu)
{
	write_gcsr_timercfg(0);
	kvm_write_sw_gcsr(vcpu->arch.csr, LOONGARCH_CSR_TCFG, 0);
	hrtimer_cancel(&vcpu->arch.swtimer);
}
