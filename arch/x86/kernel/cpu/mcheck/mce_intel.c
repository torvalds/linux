/*
 * Intel specific MCE features.
 * Copyright 2004 Zwane Mwaikambo <zwane@linuxpower.ca>
 * Copyright (C) 2008, 2009 Intel Corporation
 * Author: Andi Kleen
 */

#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/percpu.h>
#include <linux/sched.h>
#include <asm/apic.h>
#include <asm/processor.h>
#include <asm/msr.h>
#include <asm/mce.h>

/*
 * Support for Intel Correct Machine Check Interrupts. This allows
 * the CPU to raise an interrupt when a corrected machine check happened.
 * Normally we pick those up using a regular polling timer.
 * Also supports reliable discovery of shared banks.
 */

static DEFINE_PER_CPU(mce_banks_t, mce_banks_owned);

/*
 * cmci_discover_lock protects against parallel discovery attempts
 * which could race against each other.
 */
static DEFINE_SPINLOCK(cmci_discover_lock);

#define CMCI_THRESHOLD 1

static int cmci_supported(int *banks)
{
	u64 cap;

	if (mce_cmci_disabled || mce_ignore_ce)
		return 0;

	/*
	 * Vendor check is not strictly needed, but the initial
	 * initialization is vendor keyed and this
	 * makes sure none of the backdoors are entered otherwise.
	 */
	if (boot_cpu_data.x86_vendor != X86_VENDOR_INTEL)
		return 0;
	if (!cpu_has_apic || lapic_get_maxlvt() < 6)
		return 0;
	rdmsrl(MSR_IA32_MCG_CAP, cap);
	*banks = min_t(unsigned, MAX_NR_BANKS, cap & 0xff);
	return !!(cap & MCG_CMCI_P);
}

/*
 * The interrupt handler. This is called on every event.
 * Just call the poller directly to log any events.
 * This could in theory increase the threshold under high load,
 * but doesn't for now.
 */
static void intel_threshold_interrupt(void)
{
	machine_check_poll(MCP_TIMESTAMP, &__get_cpu_var(mce_banks_owned));
	mce_notify_irq();
}

static void print_update(char *type, int *hdr, int num)
{
	if (*hdr == 0)
		printk(KERN_INFO "CPU %d MCA banks", smp_processor_id());
	*hdr = 1;
	printk(KERN_CONT " %s:%d", type, num);
}

/*
 * Enable CMCI (Corrected Machine Check Interrupt) for available MCE banks
 * on this CPU. Use the algorithm recommended in the SDM to discover shared
 * banks.
 */
static void cmci_discover(int banks, int boot)
{
	unsigned long *owned = (void *)&__get_cpu_var(mce_banks_owned);
	unsigned long flags;
	int hdr = 0;
	int i;

	spin_lock_irqsave(&cmci_discover_lock, flags);
	for (i = 0; i < banks; i++) {
		u64 val;

		if (test_bit(i, owned))
			continue;

		rdmsrl(MSR_IA32_MCx_CTL2(i), val);

		/* Already owned by someone else? */
		if (val & CMCI_EN) {
			if (test_and_clear_bit(i, owned) || boot)
				print_update("SHD", &hdr, i);
			__clear_bit(i, __get_cpu_var(mce_poll_banks));
			continue;
		}

		val |= CMCI_EN | CMCI_THRESHOLD;
		wrmsrl(MSR_IA32_MCx_CTL2(i), val);
		rdmsrl(MSR_IA32_MCx_CTL2(i), val);

		/* Did the enable bit stick? -- the bank supports CMCI */
		if (val & CMCI_EN) {
			if (!test_and_set_bit(i, owned) || boot)
				print_update("CMCI", &hdr, i);
			__clear_bit(i, __get_cpu_var(mce_poll_banks));
		} else {
			WARN_ON(!test_bit(i, __get_cpu_var(mce_poll_banks)));
		}
	}
	spin_unlock_irqrestore(&cmci_discover_lock, flags);
	if (hdr)
		printk(KERN_CONT "\n");
}

/*
 * Just in case we missed an event during initialization check
 * all the CMCI owned banks.
 */
void cmci_recheck(void)
{
	unsigned long flags;
	int banks;

	if (!mce_available(&current_cpu_data) || !cmci_supported(&banks))
		return;
	local_irq_save(flags);
	machine_check_poll(MCP_TIMESTAMP, &__get_cpu_var(mce_banks_owned));
	local_irq_restore(flags);
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
	u64 val;

	if (!cmci_supported(&banks))
		return;
	spin_lock_irqsave(&cmci_discover_lock, flags);
	for (i = 0; i < banks; i++) {
		if (!test_bit(i, __get_cpu_var(mce_banks_owned)))
			continue;
		/* Disable CMCI */
		rdmsrl(MSR_IA32_MCx_CTL2(i), val);
		val &= ~(CMCI_EN|CMCI_THRESHOLD_MASK);
		wrmsrl(MSR_IA32_MCx_CTL2(i), val);
		__clear_bit(i, __get_cpu_var(mce_banks_owned));
	}
	spin_unlock_irqrestore(&cmci_discover_lock, flags);
}

/*
 * After a CPU went down cycle through all the others and rediscover
 * Must run in process context.
 */
void cmci_rediscover(int dying)
{
	int banks;
	int cpu;
	cpumask_var_t old;

	if (!cmci_supported(&banks))
		return;
	if (!alloc_cpumask_var(&old, GFP_KERNEL))
		return;
	cpumask_copy(old, &current->cpus_allowed);

	for_each_online_cpu(cpu) {
		if (cpu == dying)
			continue;
		if (set_cpus_allowed_ptr(current, cpumask_of(cpu)))
			continue;
		/* Recheck banks in case CPUs don't all have the same */
		if (cmci_supported(&banks))
			cmci_discover(banks, 0);
	}

	set_cpus_allowed_ptr(current, old);
	free_cpumask_var(old);
}

/*
 * Reenable CMCI on this CPU in case a CPU down failed.
 */
void cmci_reenable(void)
{
	int banks;
	if (cmci_supported(&banks))
		cmci_discover(banks, 0);
}

static void intel_init_cmci(void)
{
	int banks;

	if (!cmci_supported(&banks))
		return;

	mce_threshold_vector = intel_threshold_interrupt;
	cmci_discover(banks, 1);
	/*
	 * For CPU #0 this runs with still disabled APIC, but that's
	 * ok because only the vector is set up. We still do another
	 * check for the banks later for CPU #0 just to make sure
	 * to not miss any events.
	 */
	apic_write(APIC_LVTCMCI, THRESHOLD_APIC_VECTOR|APIC_DM_FIXED);
	cmci_recheck();
}

void mce_intel_feature_init(struct cpuinfo_x86 *c)
{
	intel_init_thermal(c);
	intel_init_cmci();
}
