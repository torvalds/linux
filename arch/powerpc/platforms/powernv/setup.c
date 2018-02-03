/*
 * PowerNV setup code.
 *
 * Copyright 2011 IBM Corp.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#undef DEBUG

#include <linux/cpu.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/tty.h>
#include <linux/reboot.h>
#include <linux/init.h>
#include <linux/console.h>
#include <linux/delay.h>
#include <linux/irq.h>
#include <linux/seq_file.h>
#include <linux/of.h>
#include <linux/of_fdt.h>
#include <linux/interrupt.h>
#include <linux/bug.h>
#include <linux/pci.h>
#include <linux/cpufreq.h>

#include <asm/machdep.h>
#include <asm/firmware.h>
#include <asm/xics.h>
#include <asm/xive.h>
#include <asm/opal.h>
#include <asm/kexec.h>
#include <asm/smp.h>

#include "powernv.h"

static void __init pnv_setup_arch(void)
{
	set_arch_panic_timeout(10, ARCH_PANIC_TIMEOUT);

	/* Initialize SMP */
	pnv_smp_init();

	/* Setup PCI */
	pnv_pci_init();

	/* Setup RTC and NVRAM callbacks */
	if (firmware_has_feature(FW_FEATURE_OPAL))
		opal_nvram_init();

	/* Enable NAP mode */
	powersave_nap = 1;

	/* XXX PMCS */
}

static void __init pnv_init(void)
{
	/*
	 * Initialize the LPC bus now so that legacy serial
	 * ports can be found on it
	 */
	opal_lpc_init();

#ifdef CONFIG_HVC_OPAL
	if (firmware_has_feature(FW_FEATURE_OPAL))
		hvc_opal_init_early();
	else
#endif
		add_preferred_console("hvc", 0, NULL);
}

static void __init pnv_init_IRQ(void)
{
	/* Try using a XIVE if available, otherwise use a XICS */
	if (!xive_native_init())
		xics_init();

	WARN_ON(!ppc_md.get_irq);
}

static void pnv_show_cpuinfo(struct seq_file *m)
{
	struct device_node *root;
	const char *model = "";

	root = of_find_node_by_path("/");
	if (root)
		model = of_get_property(root, "model", NULL);
	seq_printf(m, "machine\t\t: PowerNV %s\n", model);
	if (firmware_has_feature(FW_FEATURE_OPAL))
		seq_printf(m, "firmware\t: OPAL\n");
	else
		seq_printf(m, "firmware\t: BML\n");
	of_node_put(root);
	if (radix_enabled())
		seq_printf(m, "MMU\t\t: Radix\n");
	else
		seq_printf(m, "MMU\t\t: Hash\n");
}

static void pnv_prepare_going_down(void)
{
	/*
	 * Disable all notifiers from OPAL, we can't
	 * service interrupts anymore anyway
	 */
	opal_event_shutdown();

	/* Soft disable interrupts */
	local_irq_disable();

	/*
	 * Return secondary CPUs to firwmare if a flash update
	 * is pending otherwise we will get all sort of error
	 * messages about CPU being stuck etc.. This will also
	 * have the side effect of hard disabling interrupts so
	 * past this point, the kernel is effectively dead.
	 */
	opal_flash_term_callback();
}

static void  __noreturn pnv_restart(char *cmd)
{
	long rc = OPAL_BUSY;

	pnv_prepare_going_down();

	while (rc == OPAL_BUSY || rc == OPAL_BUSY_EVENT) {
		rc = opal_cec_reboot();
		if (rc == OPAL_BUSY_EVENT)
			opal_poll_events(NULL);
		else
			mdelay(10);
	}
	for (;;)
		opal_poll_events(NULL);
}

static void __noreturn pnv_power_off(void)
{
	long rc = OPAL_BUSY;

	pnv_prepare_going_down();

	while (rc == OPAL_BUSY || rc == OPAL_BUSY_EVENT) {
		rc = opal_cec_power_down(0);
		if (rc == OPAL_BUSY_EVENT)
			opal_poll_events(NULL);
		else
			mdelay(10);
	}
	for (;;)
		opal_poll_events(NULL);
}

static void __noreturn pnv_halt(void)
{
	pnv_power_off();
}

static void pnv_progress(char *s, unsigned short hex)
{
}

static void pnv_shutdown(void)
{
	/* Let the PCI code clear up IODA tables */
	pnv_pci_shutdown();

	/*
	 * Stop OPAL activity: Unregister all OPAL interrupts so they
	 * don't fire up while we kexec and make sure all potentially
	 * DMA'ing ops are complete (such as dump retrieval).
	 */
	opal_shutdown();
}

#ifdef CONFIG_KEXEC_CORE
static void pnv_kexec_wait_secondaries_down(void)
{
	int my_cpu, i, notified = -1;

	my_cpu = get_cpu();

	for_each_online_cpu(i) {
		uint8_t status;
		int64_t rc, timeout = 1000;

		if (i == my_cpu)
			continue;

		for (;;) {
			rc = opal_query_cpu_status(get_hard_smp_processor_id(i),
						   &status);
			if (rc != OPAL_SUCCESS || status != OPAL_THREAD_STARTED)
				break;
			barrier();
			if (i != notified) {
				printk(KERN_INFO "kexec: waiting for cpu %d "
				       "(physical %d) to enter OPAL\n",
				       i, paca[i].hw_cpu_id);
				notified = i;
			}

			/*
			 * On crash secondaries might be unreachable or hung,
			 * so timeout if we've waited too long
			 * */
			mdelay(1);
			if (timeout-- == 0) {
				printk(KERN_ERR "kexec: timed out waiting for "
				       "cpu %d (physical %d) to enter OPAL\n",
				       i, paca[i].hw_cpu_id);
				break;
			}
		}
	}
}

static void pnv_kexec_cpu_down(int crash_shutdown, int secondary)
{
	u64 reinit_flags;

	if (xive_enabled())
		xive_kexec_teardown_cpu(secondary);
	else
		xics_kexec_teardown_cpu(secondary);

	/* On OPAL, we return all CPUs to firmware */
	if (!firmware_has_feature(FW_FEATURE_OPAL))
		return;

	if (secondary) {
		/* Return secondary CPUs to firmware on OPAL v3 */
		mb();
		get_paca()->kexec_state = KEXEC_STATE_REAL_MODE;
		mb();

		/* Return the CPU to OPAL */
		opal_return_cpu();
	} else {
		/* Primary waits for the secondaries to have reached OPAL */
		pnv_kexec_wait_secondaries_down();

		/* Switch XIVE back to emulation mode */
		if (xive_enabled())
			xive_shutdown();

		/*
		 * We might be running as little-endian - now that interrupts
		 * are disabled, reset the HILE bit to big-endian so we don't
		 * take interrupts in the wrong endian later
		 *
		 * We reinit to enable both radix and hash on P9 to ensure
		 * the mode used by the next kernel is always supported.
		 */
		reinit_flags = OPAL_REINIT_CPUS_HILE_BE;
		if (cpu_has_feature(CPU_FTR_ARCH_300))
			reinit_flags |= OPAL_REINIT_CPUS_MMU_RADIX |
				OPAL_REINIT_CPUS_MMU_HASH;
		opal_reinit_cpus(reinit_flags);
	}
}
#endif /* CONFIG_KEXEC_CORE */

#ifdef CONFIG_MEMORY_HOTPLUG_SPARSE
static unsigned long pnv_memory_block_size(void)
{
	/*
	 * We map the kernel linear region with 1GB large pages on radix. For
	 * memory hot unplug to work our memory block size must be at least
	 * this size.
	 */
	if (radix_enabled())
		return 1UL * 1024 * 1024 * 1024;
	else
		return 256UL * 1024 * 1024;
}
#endif

static void __init pnv_setup_machdep_opal(void)
{
	ppc_md.get_boot_time = opal_get_boot_time;
	ppc_md.restart = pnv_restart;
	pm_power_off = pnv_power_off;
	ppc_md.halt = pnv_halt;
	ppc_md.machine_check_exception = opal_machine_check;
	ppc_md.mce_check_early_recovery = opal_mce_check_early_recovery;
	ppc_md.hmi_exception_early = opal_hmi_exception_early;
	ppc_md.handle_hmi_exception = opal_handle_hmi_exception;
}

static int __init pnv_probe(void)
{
	if (!of_machine_is_compatible("ibm,powernv"))
		return 0;

	if (firmware_has_feature(FW_FEATURE_OPAL))
		pnv_setup_machdep_opal();

	pr_debug("PowerNV detected !\n");

	pnv_init();

	return 1;
}

/*
 * Returns the cpu frequency for 'cpu' in Hz. This is used by
 * /proc/cpuinfo
 */
static unsigned long pnv_get_proc_freq(unsigned int cpu)
{
	unsigned long ret_freq;

	ret_freq = cpufreq_get(cpu) * 1000ul;

	/*
	 * If the backend cpufreq driver does not exist,
         * then fallback to old way of reporting the clockrate.
	 */
	if (!ret_freq)
		ret_freq = ppc_proc_freq;
	return ret_freq;
}

define_machine(powernv) {
	.name			= "PowerNV",
	.probe			= pnv_probe,
	.setup_arch		= pnv_setup_arch,
	.init_IRQ		= pnv_init_IRQ,
	.show_cpuinfo		= pnv_show_cpuinfo,
	.get_proc_freq          = pnv_get_proc_freq,
	.progress		= pnv_progress,
	.machine_shutdown	= pnv_shutdown,
	.power_save             = NULL,
	.calibrate_decr		= generic_calibrate_decr,
#ifdef CONFIG_KEXEC_CORE
	.kexec_cpu_down		= pnv_kexec_cpu_down,
#endif
#ifdef CONFIG_MEMORY_HOTPLUG_SPARSE
	.memory_block_size	= pnv_memory_block_size,
#endif
};
