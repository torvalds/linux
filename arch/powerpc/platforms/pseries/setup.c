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

#include <linux/config.h>
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
#include <asm/firmware.h>
#include <asm/pmc.h>
#include <asm/mpic.h>
#include <asm/ppc-pci.h>
#include <asm/i8259.h>
#include <asm/udbg.h>

#include "plpar_wrappers.h"

#ifdef DEBUG
#define DBG(fmt...) udbg_printf(fmt)
#else
#define DBG(fmt...)
#endif

extern void find_udbg_vterm(void);
extern void system_reset_fwnmi(void);	/* from head.S */
extern void machine_check_fwnmi(void);	/* from head.S */
extern void generic_find_legacy_serial_ports(u64 *physport,
		unsigned int *default_speed);

int fwnmi_active;  /* TRUE if an FWNMI handler is present */

extern void pSeries_system_reset_exception(struct pt_regs *regs);
extern int pSeries_machine_check_exception(struct pt_regs *regs);

static void pseries_shared_idle(void);
static void pseries_dedicated_idle(void);

struct mpic *pSeries_mpic;

void pSeries_show_cpuinfo(struct seq_file *m)
{
	struct device_node *root;
	const char *model = "";

	root = of_find_node_by_path("/");
	if (root)
		model = get_property(root, "model", NULL);
	seq_printf(m, "machine\t\t: CHRP %s\n", model);
	of_node_put(root);
}

/* Initialize firmware assisted non-maskable interrupts if
 * the firmware supports this feature.
 *
 */
static void __init fwnmi_init(void)
{
	int ret;
	int ibm_nmi_register = rtas_token("ibm,nmi-register");
	if (ibm_nmi_register == RTAS_UNKNOWN_SERVICE)
		return;
	ret = rtas_call(ibm_nmi_register, 2, 1, NULL,
			__pa((unsigned long)system_reset_fwnmi),
			__pa((unsigned long)machine_check_fwnmi));
	if (ret == 0)
		fwnmi_active = 1;
}

static void __init pSeries_init_mpic(void)
{
        unsigned int *addrp;
	struct device_node *np;
	unsigned long intack = 0;

	/* All ISUs are setup, complete initialization */
	mpic_init(pSeries_mpic);

	/* Check what kind of cascade ACK we have */
        if (!(np = of_find_node_by_name(NULL, "pci"))
            || !(addrp = (unsigned int *)
                 get_property(np, "8259-interrupt-acknowledge", NULL)))
                printk(KERN_ERR "Cannot find pci to get ack address\n");
        else
		intack = addrp[prom_n_addr_cells(np)-1];
	of_node_put(np);

	/* Setup the legacy interrupts & controller */
	i8259_init(intack, 0);

	/* Hook cascade to mpic */
	mpic_setup_cascade(NUM_ISA_INTERRUPTS, i8259_irq_cascade, NULL);
}

static void __init pSeries_setup_mpic(void)
{
	unsigned int *opprop;
	unsigned long openpic_addr = 0;
        unsigned char senses[NR_IRQS - NUM_ISA_INTERRUPTS];
        struct device_node *root;
	int irq_count;

	/* Find the Open PIC if present */
	root = of_find_node_by_path("/");
	opprop = (unsigned int *) get_property(root, "platform-open-pic", NULL);
	if (opprop != 0) {
		int n = prom_n_addr_cells(root);

		for (openpic_addr = 0; n > 0; --n)
			openpic_addr = (openpic_addr << 32) + *opprop++;
		printk(KERN_DEBUG "OpenPIC addr: %lx\n", openpic_addr);
	}
	of_node_put(root);

	BUG_ON(openpic_addr == 0);

	/* Get the sense values from OF */
	prom_get_irq_senses(senses, NUM_ISA_INTERRUPTS, NR_IRQS);
	
	/* Setup the openpic driver */
	irq_count = NR_IRQS - NUM_ISA_INTERRUPTS - 4; /* leave room for IPIs */
	pSeries_mpic = mpic_alloc(openpic_addr, MPIC_PRIMARY,
				  16, 16, irq_count, /* isu size, irq offset, irq count */ 
				  NR_IRQS - 4, /* ipi offset */
				  senses, irq_count, /* sense & sense size */
				  " MPIC     ");
}

static void pseries_lpar_enable_pmcs(void)
{
	unsigned long set, reset;

	power4_enable_pmcs();

	set = 1UL << 63;
	reset = 0;
	plpar_hcall_norets(H_PERFMON, set, reset);

	/* instruct hypervisor to maintain PMCs */
	if (firmware_has_feature(FW_FEATURE_SPLPAR))
		get_paca()->lppaca.pmcregs_in_use = 1;
}

static void __init pSeries_setup_arch(void)
{
	/* Fixup ppc_md depending on the type of interrupt controller */
	if (ppc64_interrupt_controller == IC_OPEN_PIC) {
		ppc_md.init_IRQ       = pSeries_init_mpic;
		ppc_md.get_irq        = mpic_get_irq;
	 	ppc_md.cpu_irq_down   = mpic_teardown_this_cpu;
		/* Allocate the mpic now, so that find_and_init_phbs() can
		 * fill the ISUs */
		pSeries_setup_mpic();
	} else {
		ppc_md.init_IRQ       = xics_init_IRQ;
		ppc_md.get_irq        = xics_get_irq;
		ppc_md.cpu_irq_down   = xics_teardown_cpu;
	}

#ifdef CONFIG_SMP
	smp_init_pSeries();
#endif
	/* openpic global configuration register (64-bit format). */
	/* openpic Interrupt Source Unit pointer (64-bit format). */
	/* python0 facility area (mmio) (64-bit format) REAL address. */

	/* init to some ~sane value until calibrate_delay() runs */
	loops_per_jiffy = 50000000;

	if (ROOT_DEV == 0) {
		printk("No ramdisk, default root is /dev/sda2\n");
		ROOT_DEV = Root_SDA2;
	}

	fwnmi_init();

	/* Find and initialize PCI host bridges */
	init_pci_config_tokens();
	find_and_init_phbs();
	eeh_init();

	pSeries_nvram_init();

	/* Choose an idle loop */
	if (firmware_has_feature(FW_FEATURE_SPLPAR)) {
		vpa_init(boot_cpuid);
		if (get_paca()->lppaca.shared_proc) {
			printk(KERN_INFO "Using shared processor idle loop\n");
			ppc_md.idle_loop = pseries_shared_idle;
		} else {
			printk(KERN_INFO "Using dedicated idle loop\n");
			ppc_md.idle_loop = pseries_dedicated_idle;
		}
	} else {
		printk(KERN_INFO "Using default idle loop\n");
		ppc_md.idle_loop = default_idle;
	}

	if (systemcfg->platform & PLATFORM_LPAR)
		ppc_md.enable_pmcs = pseries_lpar_enable_pmcs;
	else
		ppc_md.enable_pmcs = power4_enable_pmcs;
}

static int __init pSeries_init_panel(void)
{
	/* Manually leave the kernel version on the panel. */
	ppc_md.progress("Linux ppc64\n", 0);
	ppc_md.progress(system_utsname.version, 0);

	return 0;
}
arch_initcall(pSeries_init_panel);


/* Build up the ppc64_firmware_features bitmask field
 * using contents of device-tree/ibm,hypertas-functions.
 * Ultimately this functionality may be moved into prom.c prom_init().
 */
static void __init fw_feature_init(void)
{
	struct device_node * dn;
	char * hypertas;
	unsigned int len;

	DBG(" -> fw_feature_init()\n");

	ppc64_firmware_features = 0;
	dn = of_find_node_by_path("/rtas");
	if (dn == NULL) {
		printk(KERN_ERR "WARNING ! Cannot find RTAS in device-tree !\n");
		goto no_rtas;
	}

	hypertas = get_property(dn, "ibm,hypertas-functions", &len);
	if (hypertas) {
		while (len > 0){
			int i, hypertas_len;
			/* check value against table of strings */
			for(i=0; i < FIRMWARE_MAX_FEATURES ;i++) {
				if ((firmware_features_table[i].name) &&
				    (strcmp(firmware_features_table[i].name,hypertas))==0) {
					/* we have a match */
					ppc64_firmware_features |= 
						(firmware_features_table[i].val);
					break;
				} 
			}
			hypertas_len = strlen(hypertas);
			len -= hypertas_len +1;
			hypertas+= hypertas_len +1;
		}
	}

	of_node_put(dn);
 no_rtas:
	printk(KERN_INFO "firmware_features = 0x%lx\n", 
	       ppc64_firmware_features);

	DBG(" <- fw_feature_init()\n");
}


static  void __init pSeries_discover_pic(void)
{
	struct device_node *np;
	char *typep;

	/*
	 * Setup interrupt mapping options that are needed for finish_device_tree
	 * to properly parse the OF interrupt tree & do the virtual irq mapping
	 */
	__irq_offset_value = NUM_ISA_INTERRUPTS;
	ppc64_interrupt_controller = IC_INVALID;
	for (np = NULL; (np = of_find_node_by_name(np, "interrupt-controller"));) {
		typep = (char *)get_property(np, "compatible", NULL);
		if (strstr(typep, "open-pic"))
			ppc64_interrupt_controller = IC_OPEN_PIC;
		else if (strstr(typep, "ppc-xicp"))
			ppc64_interrupt_controller = IC_PPC_XIC;
		else
			printk("pSeries_discover_pic: failed to recognize"
			       " interrupt-controller\n");
		break;
	}
}

static void pSeries_mach_cpu_die(void)
{
	local_irq_disable();
	idle_task_exit();
	/* Some hardware requires clearing the CPPR, while other hardware does not
	 * it is safe either way
	 */
	pSeriesLP_cppr_info(0, 0);
	rtas_stop_self();
	/* Should never get here... */
	BUG();
	for(;;);
}

static int pseries_set_dabr(unsigned long dabr)
{
	if (firmware_has_feature(FW_FEATURE_XDABR)) {
		/* We want to catch accesses from kernel and userspace */
		return plpar_set_xdabr(dabr, H_DABRX_KERNEL | H_DABRX_USER);
	}

	return plpar_set_dabr(dabr);
}


/*
 * Early initialization.  Relocation is on but do not reference unbolted pages
 */
static void __init pSeries_init_early(void)
{
	void *comport;
	int iommu_off = 0;
	unsigned int default_speed;
	u64 physport;

	DBG(" -> pSeries_init_early()\n");

	fw_feature_init();
	
	if (systemcfg->platform & PLATFORM_LPAR)
		hpte_init_lpar();
	else {
		hpte_init_native();
		iommu_off = (of_chosen &&
			     get_property(of_chosen, "linux,iommu-off", NULL));
	}

	generic_find_legacy_serial_ports(&physport, &default_speed);

	if (systemcfg->platform & PLATFORM_LPAR)
		find_udbg_vterm();
	else if (physport) {
		/* Map the uart for udbg. */
		comport = (void *)ioremap(physport, 16);
		udbg_init_uart(comport, default_speed);

		DBG("Hello World !\n");
	}

	if (firmware_has_feature(FW_FEATURE_XDABR | FW_FEATURE_DABR))
		ppc_md.set_dabr = pseries_set_dabr;

	iommu_init_early_pSeries();

	pSeries_discover_pic();

	DBG(" <- pSeries_init_early()\n");
}


static int pSeries_check_legacy_ioport(unsigned int baseport)
{
	struct device_node *np;

#define I8042_DATA_REG	0x60
#define FDC_BASE	0x3f0


	switch(baseport) {
	case I8042_DATA_REG:
		np = of_find_node_by_type(NULL, "8042");
		if (np == NULL)
			return -ENODEV;
		of_node_put(np);
		break;
	case FDC_BASE:
		np = of_find_node_by_type(NULL, "fdc");
		if (np == NULL)
			return -ENODEV;
		of_node_put(np);
		break;
	}
	return 0;
}

/*
 * Called very early, MMU is off, device-tree isn't unflattened
 */
extern struct machdep_calls pSeries_md;

static int __init pSeries_probe(int platform)
{
	if (platform != PLATFORM_PSERIES &&
	    platform != PLATFORM_PSERIES_LPAR)
		return 0;

	/* if we have some ppc_md fixups for LPAR to do, do
	 * it here ...
	 */

	return 1;
}

DECLARE_PER_CPU(unsigned long, smt_snooze_delay);

static inline void dedicated_idle_sleep(unsigned int cpu)
{
	struct paca_struct *ppaca = &paca[cpu ^ 1];

	/* Only sleep if the other thread is not idle */
	if (!(ppaca->lppaca.idle)) {
		local_irq_disable();

		/*
		 * We are about to sleep the thread and so wont be polling any
		 * more.
		 */
		clear_thread_flag(TIF_POLLING_NRFLAG);

		/*
		 * SMT dynamic mode. Cede will result in this thread going
		 * dormant, if the partner thread is still doing work.  Thread
		 * wakes up if partner goes idle, an interrupt is presented, or
		 * a prod occurs.  Returning from the cede enables external
		 * interrupts.
		 */
		if (!need_resched())
			cede_processor();
		else
			local_irq_enable();
	} else {
		/*
		 * Give the HV an opportunity at the processor, since we are
		 * not doing any work.
		 */
		poll_pending();
	}
}

static void pseries_dedicated_idle(void)
{ 
	long oldval;
	struct paca_struct *lpaca = get_paca();
	unsigned int cpu = smp_processor_id();
	unsigned long start_snooze;
	unsigned long *smt_snooze_delay = &__get_cpu_var(smt_snooze_delay);

	while (1) {
		/*
		 * Indicate to the HV that we are idle. Now would be
		 * a good time to find other work to dispatch.
		 */
		lpaca->lppaca.idle = 1;

		oldval = test_and_clear_thread_flag(TIF_NEED_RESCHED);
		if (!oldval) {
			set_thread_flag(TIF_POLLING_NRFLAG);

			start_snooze = __get_tb() +
				*smt_snooze_delay * tb_ticks_per_usec;

			while (!need_resched() && !cpu_is_offline(cpu)) {
				ppc64_runlatch_off();

				/*
				 * Go into low thread priority and possibly
				 * low power mode.
				 */
				HMT_low();
				HMT_very_low();

				if (*smt_snooze_delay != 0 &&
				    __get_tb() > start_snooze) {
					HMT_medium();
					dedicated_idle_sleep(cpu);
				}

			}

			HMT_medium();
			clear_thread_flag(TIF_POLLING_NRFLAG);
		} else {
			set_need_resched();
		}

		lpaca->lppaca.idle = 0;
		ppc64_runlatch_on();

		schedule();

		if (cpu_is_offline(cpu) && system_state == SYSTEM_RUNNING)
			cpu_die();
	}
}

static void pseries_shared_idle(void)
{
	struct paca_struct *lpaca = get_paca();
	unsigned int cpu = smp_processor_id();

	while (1) {
		/*
		 * Indicate to the HV that we are idle. Now would be
		 * a good time to find other work to dispatch.
		 */
		lpaca->lppaca.idle = 1;

		while (!need_resched() && !cpu_is_offline(cpu)) {
			local_irq_disable();
			ppc64_runlatch_off();

			/*
			 * Yield the processor to the hypervisor.  We return if
			 * an external interrupt occurs (which are driven prior
			 * to returning here) or if a prod occurs from another
			 * processor. When returning here, external interrupts
			 * are enabled.
			 *
			 * Check need_resched() again with interrupts disabled
			 * to avoid a race.
			 */
			if (!need_resched())
				cede_processor();
			else
				local_irq_enable();

			HMT_medium();
		}

		lpaca->lppaca.idle = 0;
		ppc64_runlatch_on();

		schedule();

		if (cpu_is_offline(cpu) && system_state == SYSTEM_RUNNING)
			cpu_die();
	}
}

static int pSeries_pci_probe_mode(struct pci_bus *bus)
{
	if (systemcfg->platform & PLATFORM_LPAR)
		return PCI_PROBE_DEVTREE;
	return PCI_PROBE_NORMAL;
}

struct machdep_calls __initdata pSeries_md = {
	.probe			= pSeries_probe,
	.setup_arch		= pSeries_setup_arch,
	.init_early		= pSeries_init_early,
	.show_cpuinfo		= pSeries_show_cpuinfo,
	.log_error		= pSeries_log_error,
	.pcibios_fixup		= pSeries_final_fixup,
	.pci_probe_mode		= pSeries_pci_probe_mode,
	.irq_bus_setup		= pSeries_irq_bus_setup,
	.restart		= rtas_restart,
	.power_off		= rtas_power_off,
	.halt			= rtas_halt,
	.panic			= rtas_os_term,
	.cpu_die		= pSeries_mach_cpu_die,
	.get_boot_time		= rtas_get_boot_time,
	.get_rtc_time		= rtas_get_rtc_time,
	.set_rtc_time		= rtas_set_rtc_time,
	.calibrate_decr		= generic_calibrate_decr,
	.progress		= rtas_progress,
	.check_legacy_ioport	= pSeries_check_legacy_ioport,
	.system_reset_exception = pSeries_system_reset_exception,
	.machine_check_exception = pSeries_machine_check_exception,
};
