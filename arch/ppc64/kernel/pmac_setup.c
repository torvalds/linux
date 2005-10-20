/*
 *  arch/ppc/platforms/setup.c
 *
 *  PowerPC version
 *    Copyright (C) 1995-1996 Gary Thomas (gdt@linuxppc.org)
 *
 *  Adapted for Power Macintosh by Paul Mackerras
 *    Copyright (C) 1996 Paul Mackerras (paulus@cs.anu.edu.au)
 *
 *  Derived from "arch/alpha/kernel/setup.c"
 *    Copyright (C) 1995 Linus Torvalds
 *
 *  Maintained by Benjamin Herrenschmidt (benh@kernel.crashing.org)
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 *
 */

/*
 * bootup setup stuff..
 */

#undef DEBUG

#include <linux/config.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/stddef.h>
#include <linux/unistd.h>
#include <linux/ptrace.h>
#include <linux/slab.h>
#include <linux/user.h>
#include <linux/a.out.h>
#include <linux/tty.h>
#include <linux/string.h>
#include <linux/delay.h>
#include <linux/ioport.h>
#include <linux/major.h>
#include <linux/initrd.h>
#include <linux/vt_kern.h>
#include <linux/console.h>
#include <linux/ide.h>
#include <linux/pci.h>
#include <linux/adb.h>
#include <linux/cuda.h>
#include <linux/pmu.h>
#include <linux/irq.h>
#include <linux/seq_file.h>
#include <linux/root_dev.h>
#include <linux/bitops.h>

#include <asm/processor.h>
#include <asm/sections.h>
#include <asm/prom.h>
#include <asm/system.h>
#include <asm/io.h>
#include <asm/pci-bridge.h>
#include <asm/iommu.h>
#include <asm/machdep.h>
#include <asm/dma.h>
#include <asm/btext.h>
#include <asm/cputable.h>
#include <asm/pmac_feature.h>
#include <asm/time.h>
#include <asm/of_device.h>
#include <asm/lmb.h>
#include <asm/smu.h>
#include <asm/pmc.h>

#include "pmac.h"
#include "mpic.h"

#ifdef DEBUG
#define DBG(fmt...) udbg_printf(fmt)
#else
#define DBG(fmt...)
#endif

static int current_root_goodness = -1;
#define DEFAULT_ROOT_DEVICE Root_SDA1	/* sda1 - slightly silly choice */

extern  int powersave_nap;
int sccdbg;

sys_ctrler_t sys_ctrler;
EXPORT_SYMBOL(sys_ctrler);

#ifdef CONFIG_PMAC_SMU
unsigned long smu_cmdbuf_abs;
EXPORT_SYMBOL(smu_cmdbuf_abs);
#endif

extern void udbg_init_scc(struct device_node *np);

static void __pmac pmac_show_cpuinfo(struct seq_file *m)
{
	struct device_node *np;
	char *pp;
	int plen;
	char* mbname;
	int mbmodel = pmac_call_feature(PMAC_FTR_GET_MB_INFO, NULL,
					PMAC_MB_INFO_MODEL, 0);
	unsigned int mbflags = pmac_call_feature(PMAC_FTR_GET_MB_INFO, NULL,
						 PMAC_MB_INFO_FLAGS, 0);

	if (pmac_call_feature(PMAC_FTR_GET_MB_INFO, NULL, PMAC_MB_INFO_NAME,
			      (long)&mbname) != 0)
		mbname = "Unknown";
	
	/* find motherboard type */
	seq_printf(m, "machine\t\t: ");
	np = of_find_node_by_path("/");
	if (np != NULL) {
		pp = (char *) get_property(np, "model", NULL);
		if (pp != NULL)
			seq_printf(m, "%s\n", pp);
		else
			seq_printf(m, "PowerMac\n");
		pp = (char *) get_property(np, "compatible", &plen);
		if (pp != NULL) {
			seq_printf(m, "motherboard\t:");
			while (plen > 0) {
				int l = strlen(pp) + 1;
				seq_printf(m, " %s", pp);
				plen -= l;
				pp += l;
			}
			seq_printf(m, "\n");
		}
		of_node_put(np);
	} else
		seq_printf(m, "PowerMac\n");

	/* print parsed model */
	seq_printf(m, "detected as\t: %d (%s)\n", mbmodel, mbname);
	seq_printf(m, "pmac flags\t: %08x\n", mbflags);

	/* Indicate newworld */
	seq_printf(m, "pmac-generation\t: NewWorld\n");
}


static void __init pmac_setup_arch(void)
{
	/* init to some ~sane value until calibrate_delay() runs */
	loops_per_jiffy = 50000000;

	/* Probe motherboard chipset */
	pmac_feature_init();
#if 0
	/* Lock-enable the SCC channel used for debug */
	if (sccdbg) {
		np = of_find_node_by_name(NULL, "escc");
		if (np)
			pmac_call_feature(PMAC_FTR_SCC_ENABLE, np,
					  PMAC_SCC_ASYNC | PMAC_SCC_FLAG_XMON, 1);
	}
#endif
	/* We can NAP */
	powersave_nap = 1;

#ifdef CONFIG_ADB_PMU
	/* Initialize the PMU if any */
	find_via_pmu();
#endif
#ifdef CONFIG_PMAC_SMU
	/* Initialize the SMU if any */
	smu_init();
#endif

	/* Init NVRAM access */
	pmac_nvram_init();

	/* Setup SMP callback */
#ifdef CONFIG_SMP
	pmac_setup_smp();
#endif

	/* Lookup PCI hosts */
       	pmac_pci_init();

#ifdef CONFIG_DUMMY_CONSOLE
	conswitchp = &dummy_con;
#endif

	printk(KERN_INFO "Using native/NAP idle loop\n");
}

#ifdef CONFIG_SCSI
void note_scsi_host(struct device_node *node, void *host)
{
	/* Obsolete */
}
#endif


static int initializing = 1;

static int pmac_late_init(void)
{
	initializing = 0;
	return 0;
}

late_initcall(pmac_late_init);

/* can't be __init - can be called whenever a disk is first accessed */
void __pmac note_bootable_part(dev_t dev, int part, int goodness)
{
	extern dev_t boot_dev;
	char *p;

	if (!initializing)
		return;
	if ((goodness <= current_root_goodness) &&
	    ROOT_DEV != DEFAULT_ROOT_DEVICE)
		return;
	p = strstr(saved_command_line, "root=");
	if (p != NULL && (p == saved_command_line || p[-1] == ' '))
		return;

	if (!boot_dev || dev == boot_dev) {
		ROOT_DEV = dev + part;
		boot_dev = 0;
		current_root_goodness = goodness;
	}
}

static void __pmac pmac_restart(char *cmd)
{
	switch(sys_ctrler) {
#ifdef CONFIG_ADB_PMU
	case SYS_CTRLER_PMU:
		pmu_restart();
		break;
#endif

#ifdef CONFIG_PMAC_SMU
	case SYS_CTRLER_SMU:
		smu_restart();
		break;
#endif
	default:
		;
	}
}

static void __pmac pmac_power_off(void)
{
	switch(sys_ctrler) {
#ifdef CONFIG_ADB_PMU
	case SYS_CTRLER_PMU:
		pmu_shutdown();
		break;
#endif
#ifdef CONFIG_PMAC_SMU
	case SYS_CTRLER_SMU:
		smu_shutdown();
		break;
#endif
	default:
		;
	}
}

static void __pmac pmac_halt(void)
{
	pmac_power_off();
}

#ifdef CONFIG_BOOTX_TEXT
static void btext_putc(unsigned char c)
{
	btext_drawchar(c);
}

static void __init init_boot_display(void)
{
	char *name;
	struct device_node *np = NULL; 
	int rc = -ENODEV;

	printk("trying to initialize btext ...\n");

	name = (char *)get_property(of_chosen, "linux,stdout-path", NULL);
	if (name != NULL) {
		np = of_find_node_by_path(name);
		if (np != NULL) {
			if (strcmp(np->type, "display") != 0) {
				printk("boot stdout isn't a display !\n");
				of_node_put(np);
				np = NULL;
			}
		}
	}
	if (np)
		rc = btext_initialize(np);
	if (rc == 0)
		return;

	for (np = NULL; (np = of_find_node_by_type(np, "display"));) {
		if (get_property(np, "linux,opened", NULL)) {
			printk("trying %s ...\n", np->full_name);
			rc = btext_initialize(np);
			printk("result: %d\n", rc);
		}
		if (rc == 0)
			return;
	}
}
#endif /* CONFIG_BOOTX_TEXT */

/* 
 * Early initialization.
 */
static void __init pmac_init_early(void)
{
	DBG(" -> pmac_init_early\n");

	/* Initialize hash table, from now on, we can take hash faults
	 * and call ioremap
	 */
	hpte_init_native();

	/* Init SCC */
       	if (strstr(cmd_line, "sccdbg")) {
		sccdbg = 1;
       		udbg_init_scc(NULL);
       	}
#ifdef CONFIG_BOOTX_TEXT
	else {
		init_boot_display();

		udbg_putc = btext_putc;
	}
#endif /* CONFIG_BOOTX_TEXT */

	/* Setup interrupt mapping options */
	ppc64_interrupt_controller = IC_OPEN_PIC;

	iommu_init_early_u3();

	DBG(" <- pmac_init_early\n");
}

static int pmac_u3_cascade(struct pt_regs *regs, void *data)
{
	return mpic_get_one_irq((struct mpic *)data, regs);
}

static __init void pmac_init_IRQ(void)
{
        struct device_node *irqctrler  = NULL;
        struct device_node *irqctrler2 = NULL;
	struct device_node *np = NULL;
	struct mpic *mpic1, *mpic2;

	/* We first try to detect Apple's new Core99 chipset, since mac-io
	 * is quite different on those machines and contains an IBM MPIC2.
	 */
	while ((np = of_find_node_by_type(np, "open-pic")) != NULL) {
		struct device_node *parent = of_get_parent(np);
		if (parent && !strcmp(parent->name, "u3"))
			irqctrler2 = of_node_get(np);
		else
			irqctrler = of_node_get(np);
		of_node_put(parent);
	}
	if (irqctrler != NULL && irqctrler->n_addrs > 0) {
		unsigned char senses[128];

		printk(KERN_INFO "PowerMac using OpenPIC irq controller at 0x%08x\n",
		       (unsigned int)irqctrler->addrs[0].address);

		prom_get_irq_senses(senses, 0, 128);
		mpic1 = mpic_alloc(irqctrler->addrs[0].address,
				   MPIC_PRIMARY | MPIC_WANTS_RESET,
				   0, 0, 128, 256, senses, 128, " K2-MPIC  ");
		BUG_ON(mpic1 == NULL);
		mpic_init(mpic1);		

		if (irqctrler2 != NULL && irqctrler2->n_intrs > 0 &&
		    irqctrler2->n_addrs > 0) {
			printk(KERN_INFO "Slave OpenPIC at 0x%08x hooked on IRQ %d\n",
			       (u32)irqctrler2->addrs[0].address,
			       irqctrler2->intrs[0].line);

			pmac_call_feature(PMAC_FTR_ENABLE_MPIC, irqctrler2, 0, 0);
			prom_get_irq_senses(senses, 128, 128 + 128);

			/* We don't need to set MPIC_BROKEN_U3 here since we don't have
			 * hypertransport interrupts routed to it
			 */
			mpic2 = mpic_alloc(irqctrler2->addrs[0].address,
					   MPIC_BIG_ENDIAN | MPIC_WANTS_RESET,
					   0, 128, 128, 0, senses, 128, " U3-MPIC  ");
			BUG_ON(mpic2 == NULL);
			mpic_init(mpic2);
			mpic_setup_cascade(irqctrler2->intrs[0].line,
					   pmac_u3_cascade, mpic2);
		}
	}
	of_node_put(irqctrler);
	of_node_put(irqctrler2);
}

static void __init pmac_progress(char *s, unsigned short hex)
{
	if (sccdbg) {
		udbg_puts(s);
		udbg_puts("\n");
	}
#ifdef CONFIG_BOOTX_TEXT
	else if (boot_text_mapped) {
		btext_drawstring(s);
		btext_drawstring("\n");
	}
#endif /* CONFIG_BOOTX_TEXT */
}

/*
 * pmac has no legacy IO, anything calling this function has to
 * fail or bad things will happen
 */
static int pmac_check_legacy_ioport(unsigned int baseport)
{
	return -ENODEV;
}

static int __init pmac_declare_of_platform_devices(void)
{
	struct device_node *np, *npp;

	npp = of_find_node_by_name(NULL, "u3");
	if (npp) {
		for (np = NULL; (np = of_get_next_child(npp, np)) != NULL;) {
			if (strncmp(np->name, "i2c", 3) == 0) {
				of_platform_device_create(np, "u3-i2c", NULL);
				of_node_put(np);
				break;
			}
		}
		of_node_put(npp);
	}
        npp = of_find_node_by_type(NULL, "smu");
        if (npp) {
		of_platform_device_create(npp, "smu", NULL);
		of_node_put(npp);
	}

	return 0;
}

device_initcall(pmac_declare_of_platform_devices);

/*
 * Called very early, MMU is off, device-tree isn't unflattened
 */
static int __init pmac_probe(int platform)
{
	if (platform != PLATFORM_POWERMAC)
		return 0;
	/*
	 * On U3, the DART (iommu) must be allocated now since it
	 * has an impact on htab_initialize (due to the large page it
	 * occupies having to be broken up so the DART itself is not
	 * part of the cacheable linar mapping
	 */
	alloc_u3_dart_table();

#ifdef CONFIG_PMAC_SMU
	/*
	 * SMU based G5s need some memory below 2Gb, at least the current
	 * driver needs that. We have to allocate it now. We allocate 4k
	 * (1 small page) for now.
	 */
	smu_cmdbuf_abs = lmb_alloc_base(4096, 4096, 0x80000000UL);
#endif /* CONFIG_PMAC_SMU */

	return 1;
}

static int pmac_probe_mode(struct pci_bus *bus)
{
	struct device_node *node = bus->sysdata;

	/* We need to use normal PCI probing for the AGP bus,
	   since the device for the AGP bridge isn't in the tree. */
	if (bus->self == NULL && device_is_compatible(node, "u3-agp"))
		return PCI_PROBE_NORMAL;

	return PCI_PROBE_DEVTREE;
}

struct machdep_calls __initdata pmac_md = {
#ifdef CONFIG_HOTPLUG_CPU
	.cpu_die		= generic_mach_cpu_die,
#endif
	.probe			= pmac_probe,
	.setup_arch		= pmac_setup_arch,
	.init_early		= pmac_init_early,
       	.get_cpuinfo		= pmac_show_cpuinfo,
	.init_IRQ		= pmac_init_IRQ,
	.get_irq		= mpic_get_irq,
	.pcibios_fixup		= pmac_pcibios_fixup,
	.pci_probe_mode		= pmac_probe_mode,
	.restart		= pmac_restart,
	.power_off		= pmac_power_off,
	.halt			= pmac_halt,
       	.get_boot_time		= pmac_get_boot_time,
       	.set_rtc_time		= pmac_set_rtc_time,
       	.get_rtc_time		= pmac_get_rtc_time,
      	.calibrate_decr		= pmac_calibrate_decr,
	.feature_call		= pmac_do_feature_call,
	.progress		= pmac_progress,
	.check_legacy_ioport	= pmac_check_legacy_ioport,
	.idle_loop		= native_idle,
	.enable_pmcs		= power4_enable_pmcs,
};
