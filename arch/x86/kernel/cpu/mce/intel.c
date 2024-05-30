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
#include <asm/cpu_device_id.h>
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
 * cmci_discover_lock protects against parallel discovery attempts
 * which could race against each other.
 */
static DEFINE_RAW_SPINLOCK(cmci_discover_lock);

/*
 * On systems that do support CMCI but it's disabled, polling for MCEs can
 * cause the same event to be reported multiple times because IA32_MCi_STATUS
 * is shared by the same package.
 */
static DEFINE_SPINLOCK(cmci_poll_lock);

/* Linux non-storm CMCI threshold (may be overridden by BIOS) */
#define CMCI_THRESHOLD		1

/*
 * MCi_CTL2 threshold for each bank when there is no storm.
 * Default value for each bank may have been set by BIOS.
 */
static u16 cmci_threshold[MAX_NR_BANKS];

/*
 * High threshold to limit CMCI rate during storms. Max supported is
 * 0x7FFF. Use this slightly smaller value so it has a distinctive
 * signature when some asks "Why am I not seeing all corrected errors?"
 * A high threshold is used instead of just disabling CMCI for a
 * bank because both corrected and uncorrected errors may be logged
 * in the same bank and signalled with CMCI. The threshold only applies
 * to corrected errors, so keeping CMCI enabled means that uncorrected
 * errors will still be processed in a timely fashion.
 */
#define CMCI_STORM_THRESHOLD	32749

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

/*
 * Set a new CMCI threshold value. Preserve the state of the
 * MCI_CTL2_CMCI_EN bit in case this happens during a
 * cmci_rediscover() operation.
 */
static void cmci_set_threshold(int bank, int thresh)
{
	unsigned long flags;
	u64 val;

	raw_spin_lock_irqsave(&cmci_discover_lock, flags);
	rdmsrl(MSR_IA32_MCx_CTL2(bank), val);
	val &= ~MCI_CTL2_CMCI_THRESHOLD_MASK;
	wrmsrl(MSR_IA32_MCx_CTL2(bank), val | thresh);
	raw_spin_unlock_irqrestore(&cmci_discover_lock, flags);
}

void mce_intel_handle_storm(int bank, bool on)
{
	if (on)
		cmci_set_threshold(bank, CMCI_STORM_THRESHOLD);
	else
		cmci_set_threshold(bank, cmci_threshold[bank]);
}

/*
 * The interrupt handler. This is called on every event.
 * Just call the poller directly to log any events.
 * This could in theory increase the threshold under high load,
 * but doesn't for now.
 */
static void intel_threshold_interrupt(void)
{
	machine_check_poll(MCP_TIMESTAMP, this_cpu_ptr(&mce_banks_owned));
}

/*
 * Check all the reasons why current CPU cannot claim
 * ownership of a bank.
 * 1: CPU already owns this bank
 * 2: BIOS owns this bank
 * 3: Some other CPU owns this bank
 */
static bool cmci_skip_bank(int bank, u64 *val)
{
	unsigned long *owned = (void *)this_cpu_ptr(&mce_banks_owned);

	if (test_bit(bank, owned))
		return true;

	/* Skip banks in firmware first mode */
	if (test_bit(bank, mce_banks_ce_disabled))
		return true;

	rdmsrl(MSR_IA32_MCx_CTL2(bank), *val);

	/* Already owned by someone else? */
	if (*val & MCI_CTL2_CMCI_EN) {
		clear_bit(bank, owned);
		__clear_bit(bank, this_cpu_ptr(mce_poll_banks));
		return true;
	}

	return false;
}

/*
 * Decide which CMCI interrupt threshold to use:
 * 1: If this bank is in storm mode from whichever CPU was
 *    the previous owner, stay in storm mode.
 * 2: If ignoring any threshold set by BIOS, set Linux default
 * 3: Try to honor BIOS threshold (unless buggy BIOS set it at zero).
 */
static u64 cmci_pick_threshold(u64 val, int *bios_zero_thresh)
{
	if ((val & MCI_CTL2_CMCI_THRESHOLD_MASK) == CMCI_STORM_THRESHOLD)
		return val;

	if (!mca_cfg.bios_cmci_threshold) {
		val &= ~MCI_CTL2_CMCI_THRESHOLD_MASK;
		val |= CMCI_THRESHOLD;
	} else if (!(val & MCI_CTL2_CMCI_THRESHOLD_MASK)) {
		/*
		 * If bios_cmci_threshold boot option was specified
		 * but the threshold is zero, we'll try to initialize
		 * it to 1.
		 */
		*bios_zero_thresh = 1;
		val |= CMCI_THRESHOLD;
	}

	return val;
}

/*
 * Try to claim ownership of a bank.
 */
static void cmci_claim_bank(int bank, u64 val, int bios_zero_thresh, int *bios_wrong_thresh)
{
	struct mca_storm_desc *storm = this_cpu_ptr(&storm_desc);

	val |= MCI_CTL2_CMCI_EN;
	wrmsrl(MSR_IA32_MCx_CTL2(bank), val);
	rdmsrl(MSR_IA32_MCx_CTL2(bank), val);

	/* If the enable bit did not stick, this bank should be polled. */
	if (!(val & MCI_CTL2_CMCI_EN)) {
		WARN_ON(!test_bit(bank, this_cpu_ptr(mce_poll_banks)));
		storm->banks[bank].poll_only = true;
		return;
	}

	/* This CPU successfully set the enable bit. */
	set_bit(bank, (void *)this_cpu_ptr(&mce_banks_owned));

	if ((val & MCI_CTL2_CMCI_THRESHOLD_MASK) == CMCI_STORM_THRESHOLD) {
		pr_notice("CPU%d BANK%d CMCI inherited storm\n", smp_processor_id(), bank);
		mce_inherit_storm(bank);
		cmci_storm_begin(bank);
	} else {
		__clear_bit(bank, this_cpu_ptr(mce_poll_banks));
	}

	/*
	 * We are able to set thresholds for some banks that
	 * had a threshold of 0. This means the BIOS has not
	 * set the thresholds properly or does not work with
	 * this boot option. Note down now and report later.
	 */
	if (mca_cfg.bios_cmci_threshold && bios_zero_thresh &&
	    (val & MCI_CTL2_CMCI_THRESHOLD_MASK))
		*bios_wrong_thresh = 1;

	/* Save default threshold for each bank */
	if (cmci_threshold[bank] == 0)
		cmci_threshold[bank] = val & MCI_CTL2_CMCI_THRESHOLD_MASK;
}

/*
 * Enable CMCI (Corrected Machine Check Interrupt) for available MCE banks
 * on this CPU. Use the algorithm recommended in the SDM to discover shared
 * banks. Called during initial bootstrap, and also for hotplug CPU operations
 * to rediscover/reassign machine check banks.
 */
static void cmci_discover(int banks)
{
	int bios_wrong_thresh = 0;
	unsigned long flags;
	int i;

	raw_spin_lock_irqsave(&cmci_discover_lock, flags);
	for (i = 0; i < banks; i++) {
		u64 val;
		int bios_zero_thresh = 0;

		if (cmci_skip_bank(i, &val))
			continue;

		val = cmci_pick_threshold(val, &bios_zero_thresh);
		cmci_claim_bank(i, val, bios_zero_thresh, &bios_wrong_thresh);
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

	if ((val & MCI_CTL2_CMCI_THRESHOLD_MASK) == CMCI_STORM_THRESHOLD)
		cmci_storm_end(bank);
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

/* Bank polling function when CMCI is disabled. */
static void cmci_mc_poll_banks(void)
{
	spin_lock(&cmci_poll_lock);
	machine_check_poll(0, this_cpu_ptr(&mce_poll_banks));
	spin_unlock(&cmci_poll_lock);
}

void intel_init_cmci(void)
{
	int banks;

	if (!cmci_supported(&banks)) {
		mc_poll_banks = cmci_mc_poll_banks;
		return;
	}

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

/*
 * Enable additional error logs from the integrated
 * memory controller on processors that support this.
 */
static void intel_imc_init(struct cpuinfo_x86 *c)
{
	u64 error_control;

	switch (c->x86_vfm) {
	case INTEL_SANDYBRIDGE_X:
	case INTEL_IVYBRIDGE_X:
	case INTEL_HASWELL_X:
		if (rdmsrl_safe(MSR_ERROR_CONTROL, &error_control))
			return;
		error_control |= 2;
		wrmsrl_safe(MSR_ERROR_CONTROL, error_control);
		break;
	}
}

void mce_intel_feature_init(struct cpuinfo_x86 *c)
{
	intel_init_cmci();
	intel_init_lmce();
	intel_imc_init(c);
}

void mce_intel_feature_clear(struct cpuinfo_x86 *c)
{
	intel_clear_lmce();
}

bool intel_filter_mce(struct mce *m)
{
	struct cpuinfo_x86 *c = &boot_cpu_data;

	/* MCE errata HSD131, HSM142, HSW131, BDM48, HSM142 and SKX37 */
	if ((c->x86_vfm == INTEL_HASWELL ||
	     c->x86_vfm == INTEL_HASWELL_L ||
	     c->x86_vfm == INTEL_BROADWELL ||
	     c->x86_vfm == INTEL_HASWELL_G ||
	     c->x86_vfm == INTEL_SKYLAKE_X) &&
	    (m->bank == 0) &&
	    ((m->status & 0xa0000000ffffffff) == 0x80000000000f0005))
		return true;

	return false;
}

/*
 * Check if the address reported by the CPU is in a format we can parse.
 * It would be possible to add code for most other cases, but all would
 * be somewhat complicated (e.g. segment offset would require an instruction
 * parser). So only support physical addresses up to page granularity for now.
 */
bool intel_mce_usable_address(struct mce *m)
{
	if (!(m->status & MCI_STATUS_MISCV))
		return false;

	if (MCI_MISC_ADDR_LSB(m->misc) > PAGE_SHIFT)
		return false;

	if (MCI_MISC_ADDR_MODE(m->misc) != MCI_MISC_ADDR_PHYS)
		return false;

	return true;
}
