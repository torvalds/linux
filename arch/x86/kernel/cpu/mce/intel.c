// SPDX-License-Identifier: GPL-2.0
/*
 * Intel specific MCE features.
 * Copyright 2004 Zwane Mwaikambo <zwane@linuxpower.ca>
 * Copyright (C) 2008, 2009 Intel Corporation
 * Author: Andi Kleen
 */

#include <linux/gfp.h>
#include <linux/interrupt.h>
#include <linux/percpu.h>
#include <linux/sched.h>
#include <linux/cpumask.h>
#include <asm/apic.h>
#include <asm/cpufeature.h>
#include <asm/intel-family.h>
#include <asm/processor.h>
#include <asm/msr.h>
#include <asm/mce.h>

#include "internal.h"

/*
 * Support for Intel Correct Machine Check Interrupts. This allows
 * the CPU to raise an interrupt when a corrected machine check happened.
 * Normally we pick those up using a regular polling timer.
 * Also supports reliable discovery of shared banks.
 */

/*
 * CMCI can be delivered to multiple cpus that share a machine check bank
 * so we need to designate a single cpu to process errors logged in each bank
 * in the interrupt handler (otherwise we would have many races and potential
 * double reporting of the same error).
 * Note that this can change when a cpu is offlined or brought online since
 * some MCA banks are shared across cpus. When a cpu is offlined, cmci_clear()
 * disables CMCI on all banks owned by the cpu and clears this bitfield. At
 * this point, cmci_rediscover() kicks in and a different cpu may end up
 * taking ownership of some of the shared MCA banks that were previously
 * owned by the offlined cpu.
 */
static DEFINE_PER_CPU(mce_banks_t, mce_banks_owned);

/*
 * CMCI storm detection backoff counter
 *
 * During storm, we reset this counter to INITIAL_CHECK_INTERVAL in case we've
 * encountered an error. If not, we decrement it by one. We signal the end of
 * the CMCI storm when it reaches 0.
 */
static DEFINE_PER_CPU(int, cmci_backoff_cnt);

/*
 * cmci_discover_lock protects against parallel discovery attempts
 * which could race against each other.
 */
static DEFINE_RAW_SPINLOCK(cmci_discover_lock);

#define CMCI_THRESHOLD		1
#define CMCI_POLL_INTERVAL	(30 * HZ)
#define CMCI_STORM_INTERVAL	(HZ)
#define CMCI_STORM_THRESHOLD	15

static DEFINE_PER_CPU(unsigned long, cmci_time_stamp);
static DEFINE_PER_CPU(unsigned int, cmci_storm_cnt);
static DEFINE_PER_CPU(unsigned int, cmci_storm_state);

enum {
	CMCI_STORM_NONE,
	CMCI_STORM_ACTIVE,
	CMCI_STORM_SUBSIDED,
};

static atomic_t cmci_storm_on_cpus;

static int cmci_supported(int *banks)
{
	u64 cap;

	if (mca_cfg.cmci_disabled || mca_cfg.ignore_ce)
		return 0;

	/*
	 * Vendor check is not strictly needed, but the initial
	 * initialization is vendor keyed and this
	 * makes sure none of the backdoors are entered otherwise.
	 */
	if (boot_cpu_data.x86_vendor != X86_VENDOR_INTEL &&
	    boot_cpu_data.x86_vendor != X86_VENDOR_ZHAOXIN)
		return 0;

	if (!boot_cpu_has(X86_FEATURE_APIC) || lapic_get_maxlvt() < 6)
		return 0;
	rdmsrl(MSR_IA32_MCG_CAP, cap);
	*banks = min_t(unsigned, MAX_NR_BANKS, cap & 0xff);
	return !!(cap & MCG_CMCI_P);
}

static bool lmce_supported(void)
{
	u64 tmp;

	if (mca_cfg.lmce_disabled)
		return false;

	rdmsrl(MSR_IA32_MCG_CAP, tmp);

	/*
	 * LMCE depends on recovery support in the processor. Hence both
	 * MCG_SER_P and MCG_LMCE_P should be present in MCG_CAP.
	 */
	if ((tmp & (MCG_SER_P | MCG_LMCE_P)) !=
		   (MCG_SER_P | MCG_LMCE_P))
		return false;

	/*
	 * BIOS should indicate support for LMCE by setting bit 20 in
	 * IA32_FEAT_CTL without which touching MCG_EXT_CTL will generate a #GP
	 * fault.  The MSR must also be locked for LMCE_ENABLED to take effect.
	 * WARN if the MSR isn't locked as init_ia32_feat_ctl() unconditionally
	 * locks the MSR in the event that it wasn't already locked by BIOS.
	 */
	rdmsrl(MSR_IA32_FEAT_CTL, tmp);
	if (WARN_ON_ONCE(!(tmp & FEAT_CTL_LOCKED)))
		return false;

	return tmp & FEAT_CTL_LMCE_ENABLED;
}

bool mce_intel_cmci_poll(void)
{
	if (__this_cpu_read(cmci_storm_state) == CMCI_STORM_NONE)
		return false;

	/*
	 * Reset the counter if we've logged an error in the last poll
	 * during the storm.
	 */
	if (machine_check_poll(0, this_cpu_ptr(&mce_banks_owned)))
		this_cpu_write(cmci_backoff_cnt, INITIAL_CHECK_INTERVAL);
	else
		this_cpu_dec(cmci_backoff_cnt);

	return true;
}

void mce_intel_hcpu_update(unsigned long cpu)
{
	if (per_cpu(cmci_storm_state, cpu) == CMCI_STORM_ACTIVE)
		atomic_dec(&cmci_storm_on_cpus);

	per_cpu(cmci_storm_state, cpu) = CMCI_STORM_NONE;
}

static void cmci_toggle_interrupt_mode(bool on)
{
	unsigned long flags, *owned;
	int bank;
	u64 val;

	raw_spin_lock_irqsave(&cmci_discover_lock, flags);
	owned = this_cpu_ptr(mce_banks_owned);
	for_each_set_bit(bank, owned, MAX_NR_BANKS) {
		rdmsrl(MSR_IA32_MCx_CTL2(bank), val);

		if (on)
			val |= MCI_CTL2_CMCI_EN;
		else
			val &= ~MCI_CTL2_CMCI_EN;

		wrmsrl(MSR_IA32_MCx_CTL2(bank), val);
	}
	raw_spin_unlock_irqrestore(&cmci_discover_lock, flags);
}

unsigned long cmci_intel_adjust_timer(unsigned long interval)
{
	if ((this_cpu_read(cmci_backoff_cnt) > 0) &&
	    (__this_cpu_read(cmci_storm_state) == CMCI_STORM_ACTIVE)) {
		mce_notify_irq();
		return CMCI_STORM_INTERVAL;
	}

	switch (__this_cpu_read(cmci_storm_state)) {
	case CMCI_STORM_ACTIVE:

		/*
		 * We switch back to interrupt mode once the poll timer has
		 * silenced itself. That means no events recorded and the timer
		 * interval is back to our poll interval.
		 */
		__this_cpu_write(cmci_storm_state, CMCI_STORM_SUBSIDED);
		if (!atomic_sub_return(1, &cmci_storm_on_cpus))
			pr_notice("CMCI storm subsided: switching to interrupt mode\n");

		fallthrough;

	case CMCI_STORM_SUBSIDED:
		/*
		 * We wait for all CPUs to go back to SUBSIDED state. When that
		 * happens we switch back to interrupt mode.
		 */
		if (!atomic_read(&cmci_storm_on_cpus)) {
			__this_cpu_write(cmci_storm_state, CMCI_STORM_NONE);
			cmci_toggle_interrupt_mode(true);
			cmci_recheck();
		}
		return CMCI_POLL_INTERVAL;
	default:

		/* We have shiny weather. Let the poll do whatever it thinks. */
		return interval;
	}
}

static bool cmci_storm_detect(void)
{
	unsigned int cnt = __this_cpu_read(cmci_storm_cnt);
	unsigned long ts = __this_cpu_read(cmci_time_stamp);
	unsigned long now = jiffies;
	int r;

	if (__this_cpu_read(cmci_storm_state) != CMCI_STORM_NONE)
		return true;

	if (time_before_eq(now, ts + CMCI_STORM_INTERVAL)) {
		cnt++;
	} else {
		cnt = 1;
		__this_cpu_write(cmci_time_stamp, now);
	}
	__this_cpu_write(cmci_storm_cnt, cnt);

	if (cnt <= CMCI_STORM_THRESHOLD)
		return false;

	cmci_toggle_interrupt_mode(false);
	__this_cpu_write(cmci_storm_state, CMCI_STORM_ACTIVE);
	r = atomic_add_return(1, &cmci_storm_on_cpus);
	mce_timer_kick(CMCI_STORM_INTERVAL);
	this_cpu_write(cmci_backoff_cnt, INITIAL_CHECK_INTERVAL);

	if (r == 1)
		pr_notice("CMCI storm detected: switching to poll mode\n");
	return true;
}

/*
 * The interrupt handler. This is called on every event.
 * Just call the poller directly to log any events.
 * This could in theory increase the threshold under high load,
 * but doesn't for now.
 */
static void intel_threshold_interrupt(void)
{
	if (cmci_storm_detect())
		return;

	machine_check_poll(MCP_TIMESTAMP, this_cpu_ptr(&mce_banks_owned));
}

/*
 * Enable CMCI (Corrected Machine Check Interrupt) for available MCE banks
 * on this CPU. Use the algorithm recommended in the SDM to discover shared
 * banks.
 */
static void cmci_discover(int banks)
{
	unsigned long *owned = (void *)this_cpu_ptr(&mce_banks_owned);
	unsigned long flags;
	int i;
	int bios_wrong_thresh = 0;

	raw_spin_lock_irqsave(&cmci_discover_lock, flags);
	for (i = 0; i < banks; i++) {
		u64 val;
		int bios_zero_thresh = 0;

		if (test_bit(i, owned))
			continue;

		/* Skip banks in firmware first mode */
		if (test_bit(i, mce_banks_ce_disabled))
			continue;

		rdmsrl(MSR_IA32_MCx_CTL2(i), val);

		/* Already owned by someone else? */
		if (val & MCI_CTL2_CMCI_EN) {
			clear_bit(i, owned);
			__clear_bit(i, this_cpu_ptr(mce_poll_banks));
			continue;
		}

		if (!mca_cfg.bios_cmci_threshold) {
			val &= ~MCI_CTL2_CMCI_THRESHOLD_MASK;
			val |= CMCI_THRESHOLD;
		} else if (!(val & MCI_CTL2_CMCI_THRESHOLD_MASK)) {
			/*
			 * If bios_cmci_threshold boot option was specified
			 * but the threshold is zero, we'll try to initialize
			 * it to 1.
			 */
			bios_zero_thresh = 1;
			val |= CMCI_THRESHOLD;
		}

		val |= MCI_CTL2_CMCI_EN;
		wrmsrl(MSR_IA32_MCx_CTL2(i), val);
		rdmsrl(MSR_IA32_MCx_CTL2(i), val);

		/* Did the enable bit stick? -- the bank supports CMCI */
		if (val & MCI_CTL2_CMCI_EN) {
			set_bit(i, owned);
			__clear_bit(i, this_cpu_ptr(mce_poll_banks));
			/*
			 * We are able to set thresholds for some banks that
			 * had a threshold of 0. This means the BIOS has not
			 * set the thresholds properly or does not work with
			 * this boot option. Note down now and report later.
			 */
			if (mca_cfg.bios_cmci_threshold && bios_zero_thresh &&
					(val & MCI_CTL2_CMCI_THRESHOLD_MASK))
				bios_wrong_thresh = 1;
		} else {
			WARN_ON(!test_bit(i, this_cpu_ptr(mce_poll_banks)));
		}
	}
	raw_spin_unlock_irqrestore(&cmci_discover_lock, flags);
	if (mca_cfg.bios_cmci_threshold && bios_wrong_thresh) {
		pr_info_once(
			"bios_cmci_threshold: Some banks do not have valid thresholds set\n");
		pr_info_once(
			"bios_cmci_threshold: Make sure your BIOS supports this boot option\n");
	}
}

/*
 * Just in case we missed an event during initialization check
 * all the CMCI owned banks.
 */
void cmci_recheck(void)
{
	unsigned long flags;
	int banks;

	if (!mce_available(raw_cpu_ptr(&cpu_info)) || !cmci_supported(&banks))
		return;

	local_irq_save(flags);
	machine_check_poll(0, this_cpu_ptr(&mce_banks_owned));
	local_irq_restore(flags);
}

/* Caller must hold the lock on cmci_discover_lock */
static void __cmci_disable_bank(int bank)
{
	u64 val;

	if (!test_bit(bank, this_cpu_ptr(mce_banks_owned)))
		return;
	rdmsrl(MSR_IA32_MCx_CTL2(bank), val);
	val &= ~MCI_CTL2_CMCI_EN;
	wrmsrl(MSR_IA32_MCx_CTL2(bank), val);
	__clear_bit(bank, this_cpu_ptr(mce_banks_owned));
}

/*
 * Disable CMCI on this CPU for all banks it owns when it goes down.
 * This allows other CPUs to claim the banks on rediscovery.
 */
void cmci_clear(void)
{
	unsigned long flags;
	int i;
	int banks;

	if (!cmci_supported(&banks))
		return;
	raw_spin_lock_irqsave(&cmci_discover_lock, flags);
	for (i = 0; i < banks; i++)
		__cmci_disable_bank(i);
	raw_spin_unlock_irqrestore(&cmci_discover_lock, flags);
}

static void cmci_rediscover_work_func(void *arg)
{
	int banks;

	/* Recheck banks in case CPUs don't all have the same */
	if (cmci_supported(&banks))
		cmci_discover(banks);
}

/* After a CPU went down cycle through all the others and rediscover */
void cmci_rediscover(void)
{
	int banks;

	if (!cmci_supported(&banks))
		return;

	on_each_cpu(cmci_rediscover_work_func, NULL, 1);
}

/*
 * Reenable CMCI on this CPU in case a CPU down failed.
 */
void cmci_reenable(void)
{
	int banks;
	if (cmci_supported(&banks))
		cmci_discover(banks);
}

void cmci_disable_bank(int bank)
{
	int banks;
	unsigned long flags;

	if (!cmci_supported(&banks))
		return;

	raw_spin_lock_irqsave(&cmci_discover_lock, flags);
	__cmci_disable_bank(bank);
	raw_spin_unlock_irqrestore(&cmci_discover_lock, flags);
}

void intel_init_cmci(void)
{
	int banks;

	if (!cmci_supported(&banks))
		return;

	mce_threshold_vector = intel_threshold_interrupt;
	cmci_discover(banks);
	/*
	 * For CPU #0 this runs with still disabled APIC, but that's
	 * ok because only the vector is set up. We still do another
	 * check for the banks later for CPU #0 just to make sure
	 * to not miss any events.
	 */
	apic_write(APIC_LVTCMCI, THRESHOLD_APIC_VECTOR|APIC_DM_FIXED);
	cmci_recheck();
}

void intel_init_lmce(void)
{
	u64 val;

	if (!lmce_supported())
		return;

	rdmsrl(MSR_IA32_MCG_EXT_CTL, val);

	if (!(val & MCG_EXT_CTL_LMCE_EN))
		wrmsrl(MSR_IA32_MCG_EXT_CTL, val | MCG_EXT_CTL_LMCE_EN);
}

void intel_clear_lmce(void)
{
	u64 val;

	if (!lmce_supported())
		return;

	rdmsrl(MSR_IA32_MCG_EXT_CTL, val);
	val &= ~MCG_EXT_CTL_LMCE_EN;
	wrmsrl(MSR_IA32_MCG_EXT_CTL, val);
}

static void intel_ppin_init(struct cpuinfo_x86 *c)
{
	unsigned long long val;

	/*
	 * Even if testing the presence of the MSR would be enough, we don't
	 * want to risk the situation where other models reuse this MSR for
	 * other purposes.
	 */
	switch (c->x86_model) {
	case INTEL_FAM6_IVYBRIDGE_X:
	case INTEL_FAM6_HASWELL_X:
	case INTEL_FAM6_BROADWELL_D:
	case INTEL_FAM6_BROADWELL_X:
	case INTEL_FAM6_SKYLAKE_X:
	case INTEL_FAM6_ICELAKE_X:
	case INTEL_FAM6_XEON_PHI_KNL:
	case INTEL_FAM6_XEON_PHI_KNM:

		if (rdmsrl_safe(MSR_PPIN_CTL, &val))
			return;

		if ((val & 3UL) == 1UL) {
			/* PPIN locked in disabled mode */
			return;
		}

		/* If PPIN is disabled, try to enable */
		if (!(val & 2UL)) {
			wrmsrl_safe(MSR_PPIN_CTL,  val | 2UL);
			rdmsrl_safe(MSR_PPIN_CTL, &val);
		}

		/* Is the enable bit set? */
		if (val & 2UL)
			set_cpu_cap(c, X86_FEATURE_INTEL_PPIN);
	}
}

/*
 * Enable additional error logs from the integrated
 * memory controller on processors that support this.
 */
static void intel_imc_init(struct cpuinfo_x86 *c)
{
	u64 error_control;

	switch (c->x86_model) {
	case INTEL_FAM6_SANDYBRIDGE_X:
	case INTEL_FAM6_IVYBRIDGE_X:
	case INTEL_FAM6_HASWELL_X:
		if (rdmsrl_safe(MSR_ERROR_CONTROL, &error_control))
			return;
		error_control |= 2;
		wrmsrl_safe(MSR_ERROR_CONTROL, error_control);
		break;
	}
}

void mce_intel_feature_init(struct cpuinfo_x86 *c)
{
	intel_init_thermal(c);
	intel_init_cmci();
	intel_init_lmce();
	intel_ppin_init(c);
	intel_imc_init(c);
}

void mce_intel_feature_clear(struct cpuinfo_x86 *c)
{
	intel_clear_lmce();
}

bool intel_filter_mce(struct mce *m)
{
	struct cpuinfo_x86 *c = &boot_cpu_data;

	/* MCE errata HSD131, HSM142, HSW131, BDM48, and HSM142 */
	if ((c->x86 == 6) &&
	    ((c->x86_model == INTEL_FAM6_HASWELL) ||
	     (c->x86_model == INTEL_FAM6_HASWELL_L) ||
	     (c->x86_model == INTEL_FAM6_BROADWELL) ||
	     (c->x86_model == INTEL_FAM6_HASWELL_G)) &&
	    (m->bank == 0) &&
	    ((m->status & 0xa0000000ffffffff) == 0x80000000000f0005))
		return true;

	return false;
}
