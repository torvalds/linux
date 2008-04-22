/*
 *  64-bit pSeries and RS/6000 setup code.
 *
 *  Copyright (C) 1995  Linus Torvalds
 *  Adapted from 'alpha' version by Gary Thomas
 *  Modified by Cort Dougan (cort@cs.nmt.edu)
 *  Modified by PPC64 Team, IBM Corp
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

/*
 * bootup setup stuff..
 */

#undef DEBUG

#include <linux/cpu.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/stddef.h>
#include <linux/unistd.h>
#include <linux/slab.h>
#include <linux/user.h>
#include <linux/a.out.h>
#include <linux/tty.h>
#include <linux/major.h>
#include <linux/interrupt.h>
#include <linux/reboot.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/console.h>
#include <linux/pci.h>
#include <linux/utsname.h>
#include <linux/adb.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/irq.h>
#include <linux/seq_file.h>
#include <linux/root_dev.h>

#include <asm/mmu.h>
#include <asm/processor.h>
#include <asm/io.h>
#include <asm/pgtable.h>
#include <asm/prom.h>
#include <asm/rtas.h>
#include <asm/pci-bridge.h>
#include <asm/iommu.h>
#include <asm/dma.h>
#include <asm/machdep.h>
#include <asm/irq.h>
#include <asm/time.h>
#include <asm/nvram.h>
#include "xics.h"
#include <asm/pmc.h>
#include <asm/mpic.h>
#include <asm/ppc-pci.h>
#include <asm/i8259.h>
#include <asm/udbg.h>
#include <asm/smp.h>
#include <asm/firmware.h>
#include <asm/eeh.h>

#include "plpar_wrappers.h"
#include "pseries.h"

#ifdef DEBUG
#define DBG(fmt...) udbg_printf(fmt)
#else
#define DBG(fmt...)
#endif

int fwnmi_active;  /* TRUE if an FWNMI handler is present */

static void pseries_shared_idle_sleep(void);
static void pseries_dedicated_idle_sleep(void);

static struct device_node *pSeries_mpic_node;

static void pSeries_show_cpuinfo(struct seq_file *m)
{
	struct device_node *root;
	const char *model = "";

	root = of_find_node_by_path("/");
	if (root)
		model = of_get_property(root, "model", NULL);
	seq_printf(m, "machine\t\t: CHRP %s\n", model);
	of_node_put(root);
}

/* Initialize firmware assisted non-maskable interrupts if
 * the firmware supports this feature.
 */
static void __init fwnmi_init(void)
{
	unsigned long system_reset_addr, machine_check_addr;

	int ibm_nmi_register = rtas_token("ibm,nmi-register");
	if (ibm_nmi_register == RTAS_UNKNOWN_SERVICE)
		return;

	/* If the kernel's not linked at zero we point the firmware at low
	 * addresses anyway, and use a trampoline to get to the real code. */
	system_reset_addr  = __pa(system_reset_fwnmi) - PHYSICAL_START;
	machine_check_addr = __pa(machine_check_fwnmi) - PHYSICAL_START;

	if (0 == rtas_call(ibm_nmi_register, 2, 1, NULL, system_reset_addr,
				machine_check_addr))
		fwnmi_active = 1;
}

void pseries_8259_cascade(unsigned int irq, struct irq_desc *desc)
{
	unsigned int cascade_irq = i8259_irq();
	if (cascade_irq != NO_IRQ)
		generic_handle_irq(cascade_irq);
	desc->chip->eoi(irq);
}

static void __init pseries_setup_i8259_cascade(void)
{
	struct device_node *np, *old, *found = NULL;
	unsigned int cascade;
	const u32 *addrp;
	unsigned long intack = 0;
	int naddr;

	for_each_node_by_type(np, "interrupt-controller") {
		if (of_device_is_compatible(np, "chrp,iic")) {
			found = np;
			break;
		}
	}

	if (found == NULL) {
		printk(KERN_DEBUG "pic: no ISA interrupt controller\n");
		return;
	}

	cascade = irq_of_parse_and_map(found, 0);
	if (cascade == NO_IRQ) {
		printk(KERN_ERR "pic: failed to map cascade interrupt");
		return;
	}
	pr_debug("pic: cascade mapped to irq %d\n", cascade);

	for (old = of_node_get(found); old != NULL ; old = np) {
		np = of_get_parent(old);
		of_node_put(old);
		if (np == NULL)
			break;
		if (strcmp(np->name, "pci") != 0)
			continue;
		addrp = of_get_property(np, "8259-interrupt-acknowledge", NULL);
		if (addrp == NULL)
			continue;
		naddr = of_n_addr_cells(np);
		intack = addrp[naddr-1];
		if (naddr > 1)
			intack |= ((unsigned long)addrp[naddr-2]) << 32;
	}
	if (intack)
		printk(KERN_DEBUG "pic: PCI 8259 intack at 0x%016lx\n", intack);
	i8259_init(found, intack);
	of_node_put(found);
	set_irq_chained_handler(cascade, pseries_8259_cascade);
}

static void __init pseries_mpic_init_IRQ(void)
{
	struct device_node *np;
	const unsigned int *opprop;
	unsigned long openpic_addr = 0;
	int naddr, n, i, opplen;
	struct mpic *mpic;

	np = of_find_node_by_path("/");
	naddr = of_n_addr_cells(np);
	opprop = of_get_property(np, "platform-open-pic", &opplen);
	if (opprop != 0) {
		openpic_addr = of_read_number(opprop, naddr);
		printk(KERN_DEBUG "OpenPIC addr: %lx\n", openpic_addr);
	}
	of_node_put(np);

	BUG_ON(openpic_addr == 0);

	/* Setup the openpic driver */
	mpic = mpic_alloc(pSeries_mpic_node, openpic_addr,
			  MPIC_PRIMARY,
			  16, 250, /* isu size, irq count */
			  " MPIC     ");
	BUG_ON(mpic == NULL);

	/* Add ISUs */
	opplen /= sizeof(u32);
	for (n = 0, i = naddr; i < opplen; i += naddr, n++) {
		unsigned long isuaddr = of_read_number(opprop + i, naddr);
		mpic_assign_isu(mpic, n, isuaddr);
	}

	/* All ISUs are setup, complete initialization */
	mpic_init(mpic);

	/* Look for cascade */
	pseries_setup_i8259_cascade();
}

static void __init pseries_xics_init_IRQ(void)
{
	xics_init_IRQ();
	pseries_setup_i8259_cascade();
}

static void pseries_lpar_enable_pmcs(void)
{
	unsigned long set, reset;

	set = 1UL << 63;
	reset = 0;
	plpar_hcall_norets(H_PERFMON, set, reset);

	/* instruct hypervisor to maintain PMCs */
	if (firmware_has_feature(FW_FEATURE_SPLPAR))
		get_lppaca()->pmcregs_in_use = 1;
}

static void __init pseries_discover_pic(void)
{
	struct device_node *np;
	const char *typep;

	for (np = NULL; (np = of_find_node_by_name(np,
						   "interrupt-controller"));) {
		typep = of_get_property(np, "compatible", NULL);
		if (strstr(typep, "open-pic")) {
			pSeries_mpic_node = of_node_get(np);
			ppc_md.init_IRQ       = pseries_mpic_init_IRQ;
			ppc_md.get_irq        = mpic_get_irq;
			setup_kexec_cpu_down_mpic();
			smp_init_pseries_mpic();
			return;
		} else if (strstr(typep, "ppc-xicp")) {
			ppc_md.init_IRQ       = pseries_xics_init_IRQ;
			setup_kexec_cpu_down_xics();
			smp_init_pseries_xics();
			return;
		}
	}
	printk(KERN_ERR "pSeries_discover_pic: failed to recognize"
	       " interrupt-controller\n");
}

static void __init pSeries_setup_arch(void)
{
	/* Discover PIC type and setup ppc_md accordingly */
	pseries_discover_pic();

	/* openpic global configuration register (64-bit format). */
	/* openpic Interrupt Source Unit pointer (64-bit format). */
	/* python0 facility area (mmio) (64-bit format) REAL address. */

	/* init to some ~sane value until calibrate_delay() runs */
	loops_per_jiffy = 50000000;

	fwnmi_init();

	/* Find and initialize PCI host bridges */
	init_pci_config_tokens();
	find_and_init_phbs();
	eeh_init();

	pSeries_nvram_init();

	/* Choose an idle loop */
	if (firmware_has_feature(FW_FEATURE_SPLPAR)) {
		vpa_init(boot_cpuid);
		if (get_lppaca()->shared_proc) {
			printk(KERN_DEBUG "Using shared processor idle loop\n");
			ppc_md.power_save = pseries_shared_idle_sleep;
		} else {
			printk(KERN_DEBUG "Using dedicated idle loop\n");
			ppc_md.power_save = pseries_dedicated_idle_sleep;
		}
	} else {
		printk(KERN_DEBUG "Using default idle loop\n");
	}

	if (firmware_has_feature(FW_FEATURE_LPAR))
		ppc_md.enable_pmcs = pseries_lpar_enable_pmcs;
	else
		ppc_md.enable_pmcs = power4_enable_pmcs;
}

static int __init pSeries_init_panel(void)
{
	/* Manually leave the kernel version on the panel. */
	ppc_md.progress("Linux ppc64\n", 0);
	ppc_md.progress(init_utsname()->version, 0);

	return 0;
}
arch_initcall(pSeries_init_panel);

static int pseries_set_dabr(unsigned long dabr)
{
	return plpar_hcall_norets(H_SET_DABR, dabr);
}

static int pseries_set_xdabr(unsigned long dabr)
{
	/* We want to catch accesses from kernel and userspace */
	return plpar_hcall_norets(H_SET_XDABR, dabr,
			H_DABRX_KERNEL | H_DABRX_USER);
}

/*
 * Early initialization.  Relocation is on but do not reference unbolted pages
 */
static void __init pSeries_init_early(void)
{
	DBG(" -> pSeries_init_early()\n");

	if (firmware_has_feature(FW_FEATURE_LPAR))
		find_udbg_vterm();

	if (firmware_has_feature(FW_FEATURE_DABR))
		ppc_md.set_dabr = pseries_set_dabr;
	else if (firmware_has_feature(FW_FEATURE_XDABR))
		ppc_md.set_dabr = pseries_set_xdabr;

	iommu_init_early_pSeries();

	DBG(" <- pSeries_init_early()\n");
}

/*
 * Called very early, MMU is off, device-tree isn't unflattened
 */

static int __init pSeries_probe_hypertas(unsigned long node,
					 const char *uname, int depth,
					 void *data)
{
	const char *hypertas;
	unsigned long len;

	if (depth != 1 ||
	    (strcmp(uname, "rtas") != 0 && strcmp(uname, "rtas@0") != 0))
		return 0;

	hypertas = of_get_flat_dt_prop(node, "ibm,hypertas-functions", &len);
	if (!hypertas)
		return 1;

	powerpc_firmware_features |= FW_FEATURE_LPAR;
	fw_feature_init(hypertas, len);

	return 1;
}

static int __init pSeries_probe(void)
{
	unsigned long root = of_get_flat_dt_root();
 	char *dtype = of_get_flat_dt_prop(root, "device_type", NULL);

 	if (dtype == NULL)
 		return 0;
 	if (strcmp(dtype, "chrp"))
		return 0;

	/* Cell blades firmware claims to be chrp while it's not. Until this
	 * is fixed, we need to avoid those here.
	 */
	if (of_flat_dt_is_compatible(root, "IBM,CPBW-1.0") ||
	    of_flat_dt_is_compatible(root, "IBM,CBEA"))
		return 0;

	DBG("pSeries detected, looking for LPAR capability...\n");

	/* Now try to figure out if we are running on LPAR */
	of_scan_flat_dt(pSeries_probe_hypertas, NULL);

	if (firmware_has_feature(FW_FEATURE_LPAR))
		hpte_init_lpar();
	else
		hpte_init_native();

	DBG("Machine is%s LPAR !\n",
	    (powerpc_firmware_features & FW_FEATURE_LPAR) ? "" : " not");

	return 1;
}


DECLARE_PER_CPU(unsigned long, smt_snooze_delay);

static void pseries_dedicated_idle_sleep(void)
{ 
	unsigned int cpu = smp_processor_id();
	unsigned long start_snooze;
	unsigned long in_purr, out_purr;

	/*
	 * Indicate to the HV that we are idle. Now would be
	 * a good time to find other work to dispatch.
	 */
	get_lppaca()->idle = 1;
	get_lppaca()->donate_dedicated_cpu = 1;
	in_purr = mfspr(SPRN_PURR);

	/*
	 * We come in with interrupts disabled, and need_resched()
	 * has been checked recently.  If we should poll for a little
	 * while, do so.
	 */
	if (__get_cpu_var(smt_snooze_delay)) {
		start_snooze = get_tb() +
			__get_cpu_var(smt_snooze_delay) * tb_ticks_per_usec;
		local_irq_enable();
		set_thread_flag(TIF_POLLING_NRFLAG);

		while (get_tb() < start_snooze) {
			if (need_resched() || cpu_is_offline(cpu))
				goto out;
			ppc64_runlatch_off();
			HMT_low();
			HMT_very_low();
		}

		HMT_medium();
		clear_thread_flag(TIF_POLLING_NRFLAG);
		smp_mb();
		local_irq_disable();
		if (need_resched() || cpu_is_offline(cpu))
			goto out;
	}

	cede_processor();

out:
	HMT_medium();
	out_purr = mfspr(SPRN_PURR);
	get_lppaca()->wait_state_cycles += out_purr - in_purr;
	get_lppaca()->donate_dedicated_cpu = 0;
	get_lppaca()->idle = 0;
}

static void pseries_shared_idle_sleep(void)
{
	/*
	 * Indicate to the HV that we are idle. Now would be
	 * a good time to find other work to dispatch.
	 */
	get_lppaca()->idle = 1;

	/*
	 * Yield the processor to the hypervisor.  We return if
	 * an external interrupt occurs (which are driven prior
	 * to returning here) or if a prod occurs from another
	 * processor. When returning here, external interrupts
	 * are enabled.
	 */
	cede_processor();

	get_lppaca()->idle = 0;
}

static int pSeries_pci_probe_mode(struct pci_bus *bus)
{
	if (firmware_has_feature(FW_FEATURE_LPAR))
		return PCI_PROBE_DEVTREE;
	return PCI_PROBE_NORMAL;
}

/**
 * pSeries_power_off - tell firmware about how to power off the system.
 *
 * This function calls either the power-off rtas token in normal cases
 * or the ibm,power-off-ups token (if present & requested) in case of
 * a power failure. If power-off token is used, power on will only be
 * possible with power button press. If ibm,power-off-ups token is used
 * it will allow auto poweron after power is restored.
 */
void pSeries_power_off(void)
{
	int rc;
	int rtas_poweroff_ups_token = rtas_token("ibm,power-off-ups");

	if (rtas_flash_term_hook)
		rtas_flash_term_hook(SYS_POWER_OFF);

	if (rtas_poweron_auto == 0 ||
		rtas_poweroff_ups_token == RTAS_UNKNOWN_SERVICE) {
		rc = rtas_call(rtas_token("power-off"), 2, 1, NULL, -1, -1);
		printk(KERN_INFO "RTAS power-off returned %d\n", rc);
	} else {
		rc = rtas_call(rtas_poweroff_ups_token, 0, 1, NULL);
		printk(KERN_INFO "RTAS ibm,power-off-ups returned %d\n", rc);
	}
	for (;;);
}

#ifndef CONFIG_PCI
void pSeries_final_fixup(void) { }
#endif

define_machine(pseries) {
	.name			= "pSeries",
	.probe			= pSeries_probe,
	.setup_arch		= pSeries_setup_arch,
	.init_early		= pSeries_init_early,
	.show_cpuinfo		= pSeries_show_cpuinfo,
	.log_error		= pSeries_log_error,
	.pcibios_fixup		= pSeries_final_fixup,
	.pci_probe_mode		= pSeries_pci_probe_mode,
	.restart		= rtas_restart,
	.power_off		= pSeries_power_off,
	.halt			= rtas_halt,
	.panic			= rtas_os_term,
	.get_boot_time		= rtas_get_boot_time,
	.get_rtc_time		= rtas_get_rtc_time,
	.set_rtc_time		= rtas_set_rtc_time,
	.calibrate_decr		= generic_calibrate_decr,
	.progress		= rtas_progress,
	.system_reset_exception = pSeries_system_reset_exception,
	.machine_check_exception = pSeries_machine_check_exception,
};
