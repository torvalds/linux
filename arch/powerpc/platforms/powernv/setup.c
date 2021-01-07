// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * PowerNV setup code.
 *
 * Copyright 2011 IBM Corp.
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
#include <linux/memblock.h>

#include <asm/machdep.h>
#include <asm/firmware.h>
#include <asm/xics.h>
#include <asm/xive.h>
#include <asm/opal.h>
#include <asm/kexec.h>
#include <asm/smp.h>
#include <asm/tm.h>
#include <asm/setup.h>
#include <asm/security_features.h>

#include "powernv.h"


static bool fw_feature_is(const char *state, const char *name,
			  struct device_node *fw_features)
{
	struct device_node *np;
	bool rc = false;

	np = of_get_child_by_name(fw_features, name);
	if (np) {
		rc = of_property_read_bool(np, state);
		of_node_put(np);
	}

	return rc;
}

static void init_fw_feat_flags(struct device_node *np)
{
	if (fw_feature_is("enabled", "inst-spec-barrier-ori31,31,0", np))
		security_ftr_set(SEC_FTR_SPEC_BAR_ORI31);

	if (fw_feature_is("enabled", "fw-bcctrl-serialized", np))
		security_ftr_set(SEC_FTR_BCCTRL_SERIALISED);

	if (fw_feature_is("enabled", "inst-l1d-flush-ori30,30,0", np))
		security_ftr_set(SEC_FTR_L1D_FLUSH_ORI30);

	if (fw_feature_is("enabled", "inst-l1d-flush-trig2", np))
		security_ftr_set(SEC_FTR_L1D_FLUSH_TRIG2);

	if (fw_feature_is("enabled", "fw-l1d-thread-split", np))
		security_ftr_set(SEC_FTR_L1D_THREAD_PRIV);

	if (fw_feature_is("enabled", "fw-count-cache-disabled", np))
		security_ftr_set(SEC_FTR_COUNT_CACHE_DISABLED);

	if (fw_feature_is("enabled", "fw-count-cache-flush-bcctr2,0,0", np))
		security_ftr_set(SEC_FTR_BCCTR_FLUSH_ASSIST);

	if (fw_feature_is("enabled", "needs-count-cache-flush-on-context-switch", np))
		security_ftr_set(SEC_FTR_FLUSH_COUNT_CACHE);

	/*
	 * The features below are enabled by default, so we instead look to see
	 * if firmware has *disabled* them, and clear them if so.
	 */
	if (fw_feature_is("disabled", "speculation-policy-favor-security", np))
		security_ftr_clear(SEC_FTR_FAVOUR_SECURITY);

	if (fw_feature_is("disabled", "needs-l1d-flush-msr-pr-0-to-1", np))
		security_ftr_clear(SEC_FTR_L1D_FLUSH_PR);

	if (fw_feature_is("disabled", "needs-l1d-flush-msr-hv-1-to-0", np))
		security_ftr_clear(SEC_FTR_L1D_FLUSH_HV);

	if (fw_feature_is("disabled", "needs-spec-barrier-for-bound-checks", np))
		security_ftr_clear(SEC_FTR_BNDS_CHK_SPEC_BAR);
}

static void pnv_setup_security_mitigations(void)
{
	struct device_node *np, *fw_features;
	enum l1d_flush_type type;
	bool enable;

	/* Default to fallback in case fw-features are not available */
	type = L1D_FLUSH_FALLBACK;

	np = of_find_node_by_name(NULL, "ibm,opal");
	fw_features = of_get_child_by_name(np, "fw-features");
	of_node_put(np);

	if (fw_features) {
		init_fw_feat_flags(fw_features);
		of_node_put(fw_features);

		if (security_ftr_enabled(SEC_FTR_L1D_FLUSH_TRIG2))
			type = L1D_FLUSH_MTTRIG;

		if (security_ftr_enabled(SEC_FTR_L1D_FLUSH_ORI30))
			type = L1D_FLUSH_ORI;
	}

	/*
	 * If we are non-Power9 bare metal, we don't need to flush on kernel
	 * entry or after user access: they fix a P9 specific vulnerability.
	 */
	if (!pvr_version_is(PVR_POWER9)) {
		security_ftr_clear(SEC_FTR_L1D_FLUSH_ENTRY);
		security_ftr_clear(SEC_FTR_L1D_FLUSH_UACCESS);
	}

	enable = security_ftr_enabled(SEC_FTR_FAVOUR_SECURITY) && \
		 (security_ftr_enabled(SEC_FTR_L1D_FLUSH_PR)   || \
		  security_ftr_enabled(SEC_FTR_L1D_FLUSH_HV));

	setup_rfi_flush(type, enable);
	setup_count_cache_flush();

	enable = security_ftr_enabled(SEC_FTR_FAVOUR_SECURITY) &&
		 security_ftr_enabled(SEC_FTR_L1D_FLUSH_ENTRY);
	setup_entry_flush(enable);

	enable = security_ftr_enabled(SEC_FTR_FAVOUR_SECURITY) &&
		 security_ftr_enabled(SEC_FTR_L1D_FLUSH_UACCESS);
	setup_uaccess_flush(enable);

	setup_stf_barrier();
}

static void __init pnv_check_guarded_cores(void)
{
	struct device_node *dn;
	int bad_count = 0;

	for_each_node_by_type(dn, "cpu") {
		if (of_property_match_string(dn, "status", "bad") >= 0)
			bad_count++;
	};

	if (bad_count) {
		printk("  _     _______________\n");
		pr_cont(" | |   /               \\\n");
		pr_cont(" | |   |    WARNING!   |\n");
		pr_cont(" | |   |               |\n");
		pr_cont(" | |   | It looks like |\n");
		pr_cont(" |_|   |  you have %*d |\n", 3, bad_count);
		pr_cont("  _    | guarded cores |\n");
		pr_cont(" (_)   \\_______________/\n");
	}
}

static void __init pnv_setup_arch(void)
{
	set_arch_panic_timeout(10, ARCH_PANIC_TIMEOUT);

	pnv_setup_security_mitigations();

	/* Initialize SMP */
	pnv_smp_init();

	/* Setup PCI */
	pnv_pci_init();

	/* Setup RTC and NVRAM callbacks */
	if (firmware_has_feature(FW_FEATURE_OPAL))
		opal_nvram_init();

	/* Enable NAP mode */
	powersave_nap = 1;

	pnv_check_guarded_cores();

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

	if (!radix_enabled()) {
		size_t size = sizeof(struct slb_entry) * mmu_slb_size;
		int i;

		/* Allocate per cpu area to save old slb contents during MCE */
		for_each_possible_cpu(i) {
			paca_ptrs[i]->mce_faulty_slbs =
					memblock_alloc_node(size,
						__alignof__(struct slb_entry),
						cpu_to_node(i));
		}
	}
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

	/* Print flash update message if one is scheduled. */
	opal_flash_update_print_message();

	smp_send_stop();

	hard_irq_disable();
}

static void  __noreturn pnv_restart(char *cmd)
{
	long rc;

	pnv_prepare_going_down();

	do {
		if (!cmd || !strlen(cmd))
			rc = opal_cec_reboot();
		else if (strcmp(cmd, "full") == 0)
			rc = opal_cec_reboot2(OPAL_REBOOT_FULL_IPL, NULL);
		else if (strcmp(cmd, "mpipl") == 0)
			rc = opal_cec_reboot2(OPAL_REBOOT_MPIPL, NULL);
		else if (strcmp(cmd, "error") == 0)
			rc = opal_cec_reboot2(OPAL_REBOOT_PLATFORM_ERROR, NULL);
		else if (strcmp(cmd, "fast") == 0)
			rc = opal_cec_reboot2(OPAL_REBOOT_FAST, NULL);
		else
			rc = OPAL_UNSUPPORTED;

		if (rc == OPAL_BUSY || rc == OPAL_BUSY_EVENT) {
			/* Opal is busy wait for some time and retry */
			opal_poll_events(NULL);
			mdelay(10);

		} else	if (cmd && rc) {
			/* Unknown error while issuing reboot */
			if (rc == OPAL_UNSUPPORTED)
				pr_err("Unsupported '%s' reboot.\n", cmd);
			else
				pr_err("Unable to issue '%s' reboot. Err=%ld\n",
				       cmd, rc);
			pr_info("Forcing a cec-reboot\n");
			cmd = NULL;
			rc = OPAL_BUSY;

		} else if (rc != OPAL_SUCCESS) {
			/* Unknown error while issuing cec-reboot */
			pr_err("Unable to reboot. Err=%ld\n", rc);
		}

	} while (rc == OPAL_BUSY || rc == OPAL_BUSY_EVENT);

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
				       i, paca_ptrs[i]->hw_cpu_id);
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
				       i, paca_ptrs[i]->hw_cpu_id);
				break;
			}
		}
	}
}

static void pnv_kexec_cpu_down(int crash_shutdown, int secondary)
{
	u64 reinit_flags;

	if (xive_enabled())
		xive_teardown_cpu();
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
		return radix_mem_block_size;
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
	/* ppc_md.system_reset_exception gets filled in by pnv_smp_init() */
	ppc_md.machine_check_exception = opal_machine_check;
	ppc_md.mce_check_early_recovery = opal_mce_check_early_recovery;
	if (opal_check_token(OPAL_HANDLE_HMI2))
		ppc_md.hmi_exception_early = opal_hmi_exception_early2;
	else
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

#ifdef CONFIG_PPC_TRANSACTIONAL_MEM
void __init pnv_tm_init(void)
{
	if (!firmware_has_feature(FW_FEATURE_OPAL) ||
	    !pvr_version_is(PVR_POWER9) ||
	    early_cpu_has_feature(CPU_FTR_TM))
		return;

	if (opal_reinit_cpus(OPAL_REINIT_CPUS_TM_SUSPEND_DISABLED) != OPAL_SUCCESS)
		return;

	pr_info("Enabling TM (Transactional Memory) with Suspend Disabled\n");
	cur_cpu_spec->cpu_features |= CPU_FTR_TM;
	/* Make sure "normal" HTM is off (it should be) */
	cur_cpu_spec->cpu_user_features2 &= ~PPC_FEATURE2_HTM;
	/* Turn on no suspend mode, and HTM no SC */
	cur_cpu_spec->cpu_user_features2 |= PPC_FEATURE2_HTM_NO_SUSPEND | \
					    PPC_FEATURE2_HTM_NOSC;
	tm_suspend_disabled = true;
}
#endif /* CONFIG_PPC_TRANSACTIONAL_MEM */

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

static long pnv_machine_check_early(struct pt_regs *regs)
{
	long handled = 0;

	if (cur_cpu_spec && cur_cpu_spec->machine_check_early)
		handled = cur_cpu_spec->machine_check_early(regs);

	return handled;
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
	.machine_check_early	= pnv_machine_check_early,
#ifdef CONFIG_KEXEC_CORE
	.kexec_cpu_down		= pnv_kexec_cpu_down,
#endif
#ifdef CONFIG_MEMORY_HOTPLUG_SPARSE
	.memory_block_size	= pnv_memory_block_size,
#endif
};
