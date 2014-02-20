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

#include <linux/cpu.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/stddef.h>
#include <linux/unistd.h>
#include <linux/user.h>
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
#include <linux/export.h>
#include <linux/delay.h>
#include <linux/irq.h>
#include <linux/seq_file.h>
#include <linux/root_dev.h>
#include <linux/cpuidle.h>
#include <linux/of.h>
#include <linux/kexec.h>

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
#include <asm/pmc.h>
#include <asm/mpic.h>
#include <asm/xics.h>
#include <asm/ppc-pci.h>
#include <asm/i8259.h>
#include <asm/udbg.h>
#include <asm/smp.h>
#include <asm/firmware.h>
#include <asm/eeh.h>
#include <asm/reg.h>
#include <asm/plpar_wrappers.h>

#include "pseries.h"

int CMO_PrPSP = -1;
int CMO_SecPSP = -1;
unsigned long CMO_PageSize = (ASM_CONST(1) << IOMMU_PAGE_SHIFT_4K);
EXPORT_SYMBOL(CMO_PageSize);

int fwnmi_active;  /* TRUE if an FWNMI handler is present */

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

static void pseries_8259_cascade(unsigned int irq, struct irq_desc *desc)
{
	struct irq_chip *chip = irq_desc_get_chip(desc);
	unsigned int cascade_irq = i8259_irq();

	if (cascade_irq != NO_IRQ)
		generic_handle_irq(cascade_irq);

	chip->irq_eoi(&desc->irq_data);
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
	irq_set_chained_handler(cascade, pseries_8259_cascade);
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
	if (opprop != NULL) {
		openpic_addr = of_read_number(opprop, naddr);
		printk(KERN_DEBUG "OpenPIC addr: %lx\n", openpic_addr);
	}
	of_node_put(np);

	BUG_ON(openpic_addr == 0);

	/* Setup the openpic driver */
	mpic = mpic_alloc(pSeries_mpic_node, openpic_addr,
			MPIC_NO_RESET, 16, 0, " MPIC     ");
	BUG_ON(mpic == NULL);

	/* Add ISUs */
	opplen /= sizeof(u32);
	for (n = 0, i = naddr; i < opplen; i += naddr, n++) {
		unsigned long isuaddr = of_read_number(opprop + i, naddr);
		mpic_assign_isu(mpic, n, isuaddr);
	}

	/* Setup top-level get_irq */
	ppc_md.get_irq = mpic_get_irq;

	/* All ISUs are setup, complete initialization */
	mpic_init(mpic);

	/* Look for cascade */
	pseries_setup_i8259_cascade();
}

static void __init pseries_xics_init_IRQ(void)
{
	xics_init();
	pseries_setup_i8259_cascade();
}

static void pseries_lpar_enable_pmcs(void)
{
	unsigned long set, reset;

	set = 1UL << 63;
	reset = 0;
	plpar_hcall_norets(H_PERFMON, set, reset);
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

static int pci_dn_reconfig_notifier(struct notifier_block *nb, unsigned long action, void *node)
{
	struct device_node *np = node;
	struct pci_dn *pci = NULL;
	int err = NOTIFY_OK;

	switch (action) {
	case OF_RECONFIG_ATTACH_NODE:
		pci = np->parent->data;
		if (pci) {
			update_dn_pci_info(np, pci->phb);

			/* Create EEH device for the OF node */
			eeh_dev_init(np, pci->phb);
		}
		break;
	default:
		err = NOTIFY_DONE;
		break;
	}
	return err;
}

static struct notifier_block pci_dn_reconfig_nb = {
	.notifier_call = pci_dn_reconfig_notifier,
};

struct kmem_cache *dtl_cache;

#ifdef CONFIG_VIRT_CPU_ACCOUNTING_NATIVE
/*
 * Allocate space for the dispatch trace log for all possible cpus
 * and register the buffers with the hypervisor.  This is used for
 * computing time stolen by the hypervisor.
 */
static int alloc_dispatch_logs(void)
{
	int cpu, ret;
	struct paca_struct *pp;
	struct dtl_entry *dtl;

	if (!firmware_has_feature(FW_FEATURE_SPLPAR))
		return 0;

	if (!dtl_cache)
		return 0;

	for_each_possible_cpu(cpu) {
		pp = &paca[cpu];
		dtl = kmem_cache_alloc(dtl_cache, GFP_KERNEL);
		if (!dtl) {
			pr_warn("Failed to allocate dispatch trace log for cpu %d\n",
				cpu);
			pr_warn("Stolen time statistics will be unreliable\n");
			break;
		}

		pp->dtl_ridx = 0;
		pp->dispatch_log = dtl;
		pp->dispatch_log_end = dtl + N_DISPATCH_LOG;
		pp->dtl_curr = dtl;
	}

	/* Register the DTL for the current (boot) cpu */
	dtl = get_paca()->dispatch_log;
	get_paca()->dtl_ridx = 0;
	get_paca()->dtl_curr = dtl;
	get_paca()->lppaca_ptr->dtl_idx = 0;

	/* hypervisor reads buffer length from this field */
	dtl->enqueue_to_dispatch_time = cpu_to_be32(DISPATCH_LOG_BYTES);
	ret = register_dtl(hard_smp_processor_id(), __pa(dtl));
	if (ret)
		pr_err("WARNING: DTL registration of cpu %d (hw %d) failed "
		       "with %d\n", smp_processor_id(),
		       hard_smp_processor_id(), ret);
	get_paca()->lppaca_ptr->dtl_enable_mask = 2;

	return 0;
}
#else /* !CONFIG_VIRT_CPU_ACCOUNTING_NATIVE */
static inline int alloc_dispatch_logs(void)
{
	return 0;
}
#endif /* CONFIG_VIRT_CPU_ACCOUNTING_NATIVE */

static int alloc_dispatch_log_kmem_cache(void)
{
	dtl_cache = kmem_cache_create("dtl", DISPATCH_LOG_BYTES,
						DISPATCH_LOG_BYTES, 0, NULL);
	if (!dtl_cache) {
		pr_warn("Failed to create dispatch trace log buffer cache\n");
		pr_warn("Stolen time statistics will be unreliable\n");
		return 0;
	}

	return alloc_dispatch_logs();
}
early_initcall(alloc_dispatch_log_kmem_cache);

static void pseries_lpar_idle(void)
{
	/* This would call on the cpuidle framework, and the back-end pseries
	 * driver to  go to idle states
	 */
	if (cpuidle_idle_call()) {
		/* On error, execute default handler
		 * to go into low thread priority and possibly
		 * low power mode by cedeing processor to hypervisor
		 */

		/* Indicate to hypervisor that we are idle. */
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
}

/*
 * Enable relocation on during exceptions. This has partition wide scope and
 * may take a while to complete, if it takes longer than one second we will
 * just give up rather than wasting any more time on this - if that turns out
 * to ever be a problem in practice we can move this into a kernel thread to
 * finish off the process later in boot.
 */
long pSeries_enable_reloc_on_exc(void)
{
	long rc;
	unsigned int delay, total_delay = 0;

	while (1) {
		rc = enable_reloc_on_exceptions();
		if (!H_IS_LONG_BUSY(rc))
			return rc;

		delay = get_longbusy_msecs(rc);
		total_delay += delay;
		if (total_delay > 1000) {
			pr_warn("Warning: Giving up waiting to enable "
				"relocation on exceptions (%u msec)!\n",
				total_delay);
			return rc;
		}

		mdelay(delay);
	}
}
EXPORT_SYMBOL(pSeries_enable_reloc_on_exc);

long pSeries_disable_reloc_on_exc(void)
{
	long rc;

	while (1) {
		rc = disable_reloc_on_exceptions();
		if (!H_IS_LONG_BUSY(rc))
			return rc;
		mdelay(get_longbusy_msecs(rc));
	}
}
EXPORT_SYMBOL(pSeries_disable_reloc_on_exc);

#ifdef CONFIG_KEXEC
static void pSeries_machine_kexec(struct kimage *image)
{
	long rc;

	if (firmware_has_feature(FW_FEATURE_SET_MODE) &&
	    (image->type != KEXEC_TYPE_CRASH)) {
		rc = pSeries_disable_reloc_on_exc();
		if (rc != H_SUCCESS)
			pr_warning("Warning: Failed to disable relocation on "
				   "exceptions: %ld\n", rc);
	}

	default_machine_kexec(image);
}
#endif

#ifdef __LITTLE_ENDIAN__
long pseries_big_endian_exceptions(void)
{
	long rc;

	while (1) {
		rc = enable_big_endian_exceptions();
		if (!H_IS_LONG_BUSY(rc))
			return rc;
		mdelay(get_longbusy_msecs(rc));
	}
}

static long pseries_little_endian_exceptions(void)
{
	long rc;

	while (1) {
		rc = enable_little_endian_exceptions();
		if (!H_IS_LONG_BUSY(rc))
			return rc;
		mdelay(get_longbusy_msecs(rc));
	}
}
#endif

static void __init pSeries_setup_arch(void)
{
	set_arch_panic_timeout(10, ARCH_PANIC_TIMEOUT);

	/* Discover PIC type and setup ppc_md accordingly */
	pseries_discover_pic();

	/* openpic global configuration register (64-bit format). */
	/* openpic Interrupt Source Unit pointer (64-bit format). */
	/* python0 facility area (mmio) (64-bit format) REAL address. */

	/* init to some ~sane value until calibrate_delay() runs */
	loops_per_jiffy = 50000000;

	fwnmi_init();

	/* By default, only probe PCI (can be overriden by rtas_pci) */
	pci_add_flags(PCI_PROBE_ONLY);

	/* Find and initialize PCI host bridges */
	init_pci_config_tokens();
	find_and_init_phbs();
	of_reconfig_notifier_register(&pci_dn_reconfig_nb);

	pSeries_nvram_init();

	if (firmware_has_feature(FW_FEATURE_LPAR)) {
		vpa_init(boot_cpuid);
		ppc_md.power_save = pseries_lpar_idle;
		ppc_md.enable_pmcs = pseries_lpar_enable_pmcs;
	} else {
		/* No special idle routine */
		ppc_md.enable_pmcs = power4_enable_pmcs;
	}

	ppc_md.pcibios_root_bridge_prepare = pseries_root_bridge_prepare;

	if (firmware_has_feature(FW_FEATURE_SET_MODE)) {
		long rc;
		if ((rc = pSeries_enable_reloc_on_exc()) != H_SUCCESS) {
			pr_warn("Unable to enable relocation on exceptions: "
				"%ld\n", rc);
		}
	}
}

static int __init pSeries_init_panel(void)
{
	/* Manually leave the kernel version on the panel. */
	ppc_md.progress("Linux ppc64\n", 0);
	ppc_md.progress(init_utsname()->version, 0);

	return 0;
}
machine_arch_initcall(pseries, pSeries_init_panel);

static int pseries_set_dabr(unsigned long dabr, unsigned long dabrx)
{
	return plpar_hcall_norets(H_SET_DABR, dabr);
}

static int pseries_set_xdabr(unsigned long dabr, unsigned long dabrx)
{
	/* Have to set at least one bit in the DABRX according to PAPR */
	if (dabrx == 0 && dabr == 0)
		dabrx = DABRX_USER;
	/* PAPR says we can only set kernel and user bits */
	dabrx &= DABRX_KERNEL | DABRX_USER;

	return plpar_hcall_norets(H_SET_XDABR, dabr, dabrx);
}

static int pseries_set_dawr(unsigned long dawr, unsigned long dawrx)
{
	/* PAPR says we can't set HYP */
	dawrx &= ~DAWRX_HYP;

	return  plapr_set_watchpoint0(dawr, dawrx);
}

#define CMO_CHARACTERISTICS_TOKEN 44
#define CMO_MAXLENGTH 1026

void pSeries_coalesce_init(void)
{
	struct hvcall_mpp_x_data mpp_x_data;

	if (firmware_has_feature(FW_FEATURE_CMO) && !h_get_mpp_x(&mpp_x_data))
		powerpc_firmware_features |= FW_FEATURE_XCMO;
	else
		powerpc_firmware_features &= ~FW_FEATURE_XCMO;
}

/**
 * fw_cmo_feature_init - FW_FEATURE_CMO is not stored in ibm,hypertas-functions,
 * handle that here. (Stolen from parse_system_parameter_string)
 */
void pSeries_cmo_feature_init(void)
{
	char *ptr, *key, *value, *end;
	int call_status;
	int page_order = IOMMU_PAGE_SHIFT_4K;

	pr_debug(" -> fw_cmo_feature_init()\n");
	spin_lock(&rtas_data_buf_lock);
	memset(rtas_data_buf, 0, RTAS_DATA_BUF_SIZE);
	call_status = rtas_call(rtas_token("ibm,get-system-parameter"), 3, 1,
				NULL,
				CMO_CHARACTERISTICS_TOKEN,
				__pa(rtas_data_buf),
				RTAS_DATA_BUF_SIZE);

	if (call_status != 0) {
		spin_unlock(&rtas_data_buf_lock);
		pr_debug("CMO not available\n");
		pr_debug(" <- fw_cmo_feature_init()\n");
		return;
	}

	end = rtas_data_buf + CMO_MAXLENGTH - 2;
	ptr = rtas_data_buf + 2;	/* step over strlen value */
	key = value = ptr;

	while (*ptr && (ptr <= end)) {
		/* Separate the key and value by replacing '=' with '\0' and
		 * point the value at the string after the '='
		 */
		if (ptr[0] == '=') {
			ptr[0] = '\0';
			value = ptr + 1;
		} else if (ptr[0] == '\0' || ptr[0] == ',') {
			/* Terminate the string containing the key/value pair */
			ptr[0] = '\0';

			if (key == value) {
				pr_debug("Malformed key/value pair\n");
				/* Never found a '=', end processing */
				break;
			}

			if (0 == strcmp(key, "CMOPageSize"))
				page_order = simple_strtol(value, NULL, 10);
			else if (0 == strcmp(key, "PrPSP"))
				CMO_PrPSP = simple_strtol(value, NULL, 10);
			else if (0 == strcmp(key, "SecPSP"))
				CMO_SecPSP = simple_strtol(value, NULL, 10);
			value = key = ptr + 1;
		}
		ptr++;
	}

	/* Page size is returned as the power of 2 of the page size,
	 * convert to the page size in bytes before returning
	 */
	CMO_PageSize = 1 << page_order;
	pr_debug("CMO_PageSize = %lu\n", CMO_PageSize);

	if (CMO_PrPSP != -1 || CMO_SecPSP != -1) {
		pr_info("CMO enabled\n");
		pr_debug("CMO enabled, PrPSP=%d, SecPSP=%d\n", CMO_PrPSP,
		         CMO_SecPSP);
		powerpc_firmware_features |= FW_FEATURE_CMO;
		pSeries_coalesce_init();
	} else
		pr_debug("CMO not enabled, PrPSP=%d, SecPSP=%d\n", CMO_PrPSP,
		         CMO_SecPSP);
	spin_unlock(&rtas_data_buf_lock);
	pr_debug(" <- fw_cmo_feature_init()\n");
}

/*
 * Early initialization.  Relocation is on but do not reference unbolted pages
 */
static void __init pSeries_init_early(void)
{
	pr_debug(" -> pSeries_init_early()\n");

#ifdef CONFIG_HVC_CONSOLE
	if (firmware_has_feature(FW_FEATURE_LPAR))
		hvc_vio_init_early();
#endif
	if (firmware_has_feature(FW_FEATURE_XDABR))
		ppc_md.set_dabr = pseries_set_xdabr;
	else if (firmware_has_feature(FW_FEATURE_DABR))
		ppc_md.set_dabr = pseries_set_dabr;

	if (firmware_has_feature(FW_FEATURE_SET_MODE))
		ppc_md.set_dawr = pseries_set_dawr;

	pSeries_cmo_feature_init();
	iommu_init_early_pSeries();

	pr_debug(" <- pSeries_init_early()\n");
}

/*
 * Called very early, MMU is off, device-tree isn't unflattened
 */

static int __init pseries_probe_fw_features(unsigned long node,
					    const char *uname, int depth,
					    void *data)
{
	const char *prop;
	unsigned long len;
	static int hypertas_found;
	static int vec5_found;

	if (depth != 1)
		return 0;

	if (!strcmp(uname, "rtas") || !strcmp(uname, "rtas@0")) {
		prop = of_get_flat_dt_prop(node, "ibm,hypertas-functions",
					   &len);
		if (prop) {
			powerpc_firmware_features |= FW_FEATURE_LPAR;
			fw_hypertas_feature_init(prop, len);
		}

		hypertas_found = 1;
	}

	if (!strcmp(uname, "chosen")) {
		prop = of_get_flat_dt_prop(node, "ibm,architecture-vec-5",
					   &len);
		if (prop)
			fw_vec5_feature_init(prop, len);

		vec5_found = 1;
	}

	return hypertas_found && vec5_found;
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

	pr_debug("pSeries detected, looking for LPAR capability...\n");

	/* Now try to figure out if we are running on LPAR */
	of_scan_flat_dt(pseries_probe_fw_features, NULL);

#ifdef __LITTLE_ENDIAN__
	if (firmware_has_feature(FW_FEATURE_SET_MODE)) {
		long rc;
		/*
		 * Tell the hypervisor that we want our exceptions to
		 * be taken in little endian mode. If this fails we don't
		 * want to use BUG() because it will trigger an exception.
		 */
		rc = pseries_little_endian_exceptions();
		if (rc) {
			ppc_md.progress("H_SET_MODE LE exception fail", 0);
			panic("Could not enable little endian exceptions");
		}
	}
#endif

	if (firmware_has_feature(FW_FEATURE_LPAR))
		hpte_init_lpar();
	else
		hpte_init_native();

	pr_debug("Machine is%s LPAR !\n",
	         (powerpc_firmware_features & FW_FEATURE_LPAR) ? "" : " not");

	return 1;
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
static void pSeries_power_off(void)
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
#ifdef CONFIG_KEXEC
	.machine_kexec          = pSeries_machine_kexec,
#endif
};
