/*
 *  linux/arch/ppc/kernel/setup.c
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
#include <linux/version.h>
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
#include <asm/plpar_wrappers.h>
#include <asm/xics.h>
#include <asm/cputable.h>

#include "i8259.h"
#include "mpic.h"
#include "pci.h"

#ifdef DEBUG
#define DBG(fmt...) udbg_printf(fmt)
#else
#define DBG(fmt...)
#endif

extern void pSeries_final_fixup(void);

extern void pSeries_get_boot_time(struct rtc_time *rtc_time);
extern void pSeries_get_rtc_time(struct rtc_time *rtc_time);
extern int  pSeries_set_rtc_time(struct rtc_time *rtc_time);
extern void find_udbg_vterm(void);
extern void system_reset_fwnmi(void);	/* from head.S */
extern void machine_check_fwnmi(void);	/* from head.S */
extern void generic_find_legacy_serial_ports(u64 *physport,
		unsigned int *default_speed);

int fwnmi_active;  /* TRUE if an FWNMI handler is present */

extern unsigned long ppc_proc_freq;
extern unsigned long ppc_tb_freq;

extern void pSeries_system_reset_exception(struct pt_regs *regs);
extern int pSeries_machine_check_exception(struct pt_regs *regs);

static volatile void __iomem * chrp_int_ack_special;
struct mpic *pSeries_mpic;

void pSeries_get_cpuinfo(struct seq_file *m)
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

static int pSeries_irq_cascade(struct pt_regs *regs, void *data)
{
	if (chrp_int_ack_special)
		return readb(chrp_int_ack_special);
	else
		return i8259_irq(smp_processor_id());
}

static void __init pSeries_init_mpic(void)
{
        unsigned int *addrp;
	struct device_node *np;
        int i;

	/* All ISUs are setup, complete initialization */
	mpic_init(pSeries_mpic);

	/* Check what kind of cascade ACK we have */
        if (!(np = of_find_node_by_name(NULL, "pci"))
            || !(addrp = (unsigned int *)
                 get_property(np, "8259-interrupt-acknowledge", NULL)))
                printk(KERN_ERR "Cannot find pci to get ack address\n");
        else
		chrp_int_ack_special = ioremap(addrp[prom_n_addr_cells(np)-1], 1);
	of_node_put(np);

	/* Setup the legacy interrupts & controller */
        for (i = 0; i < NUM_ISA_INTERRUPTS; i++)
                irq_desc[i].handler = &i8259_pic;
	i8259_init(0);

	/* Hook cascade to mpic */
	mpic_setup_cascade(NUM_ISA_INTERRUPTS, pSeries_irq_cascade, NULL);
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

static void __init pSeries_setup_arch(void)
{
	/* Fixup ppc_md depending on the type of interrupt controller */
	if (ppc64_interrupt_controller == IC_OPEN_PIC) {
		ppc_md.init_IRQ       = pSeries_init_mpic; 
		ppc_md.get_irq        = mpic_get_irq;
		/* Allocate the mpic now, so that find_and_init_phbs() can
		 * fill the ISUs */
		pSeries_setup_mpic();
	} else {
		ppc_md.init_IRQ       = xics_init_IRQ;
		ppc_md.get_irq        = xics_get_irq;
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
	eeh_init();
	find_and_init_phbs();

#ifdef CONFIG_DUMMY_CONSOLE
	conswitchp = &dummy_con;
#endif

	pSeries_nvram_init();

	if (cur_cpu_spec->firmware_features & FW_FEATURE_SPLPAR)
		vpa_init(boot_cpuid);
}

static int __init pSeries_init_panel(void)
{
	/* Manually leave the kernel version on the panel. */
	ppc_md.progress("Linux ppc64\n", 0);
	ppc_md.progress(UTS_RELEASE, 0);

	return 0;
}
arch_initcall(pSeries_init_panel);


/* Build up the firmware_features bitmask field
 * using contents of device-tree/ibm,hypertas-functions.
 * Ultimately this functionality may be moved into prom.c prom_init().
 */
void __init fw_feature_init(void)
{
	struct device_node * dn;
	char * hypertas;
	unsigned int len;

	DBG(" -> fw_feature_init()\n");

	cur_cpu_spec->firmware_features = 0;
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
					cur_cpu_spec->firmware_features |= 
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
	       cur_cpu_spec->firmware_features);

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

		ppc_md.udbg_putc = udbg_putc;
		ppc_md.udbg_getc = udbg_getc;
		ppc_md.udbg_getc_poll = udbg_getc_poll;
		DBG("Hello World !\n");
	}


	iommu_init_early_pSeries();

	pSeries_discover_pic();

	DBG(" <- pSeries_init_early()\n");
}


static void pSeries_progress(char *s, unsigned short hex)
{
	struct device_node *root;
	int width, *p;
	char *os;
	static int display_character, set_indicator;
	static int max_width;
	static DEFINE_SPINLOCK(progress_lock);
	static int pending_newline = 0;  /* did last write end with unprinted newline? */

	if (!rtas.base)
		return;

	if (max_width == 0) {
		if ((root = find_path_device("/rtas")) &&
		     (p = (unsigned int *)get_property(root,
						       "ibm,display-line-length",
						       NULL)))
			max_width = *p;
		else
			max_width = 0x10;
		display_character = rtas_token("display-character");
		set_indicator = rtas_token("set-indicator");
	}

	if (display_character == RTAS_UNKNOWN_SERVICE) {
		/* use hex display if available */
		if (set_indicator != RTAS_UNKNOWN_SERVICE)
			rtas_call(set_indicator, 3, 1, NULL, 6, 0, hex);
		return;
	}

	spin_lock(&progress_lock);

	/*
	 * Last write ended with newline, but we didn't print it since
	 * it would just clear the bottom line of output. Print it now
	 * instead.
	 *
	 * If no newline is pending, print a CR to start output at the
	 * beginning of the line.
	 */
	if (pending_newline) {
		rtas_call(display_character, 1, 1, NULL, '\r');
		rtas_call(display_character, 1, 1, NULL, '\n');
		pending_newline = 0;
	} else {
		rtas_call(display_character, 1, 1, NULL, '\r');
	}
 
	width = max_width;
	os = s;
	while (*os) {
		if (*os == '\n' || *os == '\r') {
			/* Blank to end of line. */
			while (width-- > 0)
				rtas_call(display_character, 1, 1, NULL, ' ');
 
			/* If newline is the last character, save it
			 * until next call to avoid bumping up the
			 * display output.
			 */
			if (*os == '\n' && !os[1]) {
				pending_newline = 1;
				spin_unlock(&progress_lock);
				return;
			}
 
			/* RTAS wants CR-LF, not just LF */
 
			if (*os == '\n') {
				rtas_call(display_character, 1, 1, NULL, '\r');
				rtas_call(display_character, 1, 1, NULL, '\n');
			} else {
				/* CR might be used to re-draw a line, so we'll
				 * leave it alone and not add LF.
				 */
				rtas_call(display_character, 1, 1, NULL, *os);
			}
 
			width = max_width;
		} else {
			width--;
			rtas_call(display_character, 1, 1, NULL, *os);
		}
 
		os++;
 
		/* if we overwrite the screen length */
		if (width <= 0)
			while ((*os != 0) && (*os != '\n') && (*os != '\r'))
				os++;
	}
 
	/* Blank to end of line. */
	while (width-- > 0)
		rtas_call(display_character, 1, 1, NULL, ' ');

	spin_unlock(&progress_lock);
}

extern void setup_default_decr(void);

/* Some sane defaults: 125 MHz timebase, 1GHz processor */
#define DEFAULT_TB_FREQ		125000000UL
#define DEFAULT_PROC_FREQ	(DEFAULT_TB_FREQ * 8)

static void __init pSeries_calibrate_decr(void)
{
	struct device_node *cpu;
	struct div_result divres;
	unsigned int *fp;
	int node_found;

	/*
	 * The cpu node should have a timebase-frequency property
	 * to tell us the rate at which the decrementer counts.
	 */
	cpu = of_find_node_by_type(NULL, "cpu");

	ppc_tb_freq = DEFAULT_TB_FREQ;		/* hardcoded default */
	node_found = 0;
	if (cpu != 0) {
		fp = (unsigned int *)get_property(cpu, "timebase-frequency",
						  NULL);
		if (fp != 0) {
			node_found = 1;
			ppc_tb_freq = *fp;
		}
	}
	if (!node_found)
		printk(KERN_ERR "WARNING: Estimating decrementer frequency "
				"(not found)\n");

	ppc_proc_freq = DEFAULT_PROC_FREQ;
	node_found = 0;
	if (cpu != 0) {
		fp = (unsigned int *)get_property(cpu, "clock-frequency",
						  NULL);
		if (fp != 0) {
			node_found = 1;
			ppc_proc_freq = *fp;
		}
	}
	if (!node_found)
		printk(KERN_ERR "WARNING: Estimating processor frequency "
				"(not found)\n");

	of_node_put(cpu);

	printk(KERN_INFO "time_init: decrementer frequency = %lu.%.6lu MHz\n",
	       ppc_tb_freq/1000000, ppc_tb_freq%1000000);
	printk(KERN_INFO "time_init: processor frequency   = %lu.%.6lu MHz\n",
	       ppc_proc_freq/1000000, ppc_proc_freq%1000000);

	tb_ticks_per_jiffy = ppc_tb_freq / HZ;
	tb_ticks_per_sec = tb_ticks_per_jiffy * HZ;
	tb_ticks_per_usec = ppc_tb_freq / 1000000;
	tb_to_us = mulhwu_scale_factor(ppc_tb_freq, 1000000);
	div128_by_32(1024*1024, 0, tb_ticks_per_sec, &divres);
	tb_to_xs = divres.result_low;

	setup_default_decr();
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

struct machdep_calls __initdata pSeries_md = {
	.probe			= pSeries_probe,
	.setup_arch		= pSeries_setup_arch,
	.init_early		= pSeries_init_early,
	.get_cpuinfo		= pSeries_get_cpuinfo,
	.log_error		= pSeries_log_error,
	.pcibios_fixup		= pSeries_final_fixup,
	.restart		= rtas_restart,
	.power_off		= rtas_power_off,
	.halt			= rtas_halt,
	.panic			= rtas_os_term,
	.cpu_die		= pSeries_mach_cpu_die,
	.get_boot_time		= pSeries_get_boot_time,
	.get_rtc_time		= pSeries_get_rtc_time,
	.set_rtc_time		= pSeries_set_rtc_time,
	.calibrate_decr		= pSeries_calibrate_decr,
	.progress		= pSeries_progress,
	.check_legacy_ioport	= pSeries_check_legacy_ioport,
	.system_reset_exception = pSeries_system_reset_exception,
	.machine_check_exception = pSeries_machine_check_exception,
};
