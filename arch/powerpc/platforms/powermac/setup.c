// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Powermac setup and early boot code plus other random bits.
 *
 *  PowerPC version
 *    Copyright (C) 1995-1996 Gary Thomas (gdt@linuxppc.org)
 *
 *  Adapted for Power Macintosh by Paul Mackerras
 *    Copyright (C) 1996 Paul Mackerras (paulus@samba.org)
 *
 *  Derived from "arch/alpha/kernel/setup.c"
 *    Copyright (C) 1995 Linus Torvalds
 *
 *  Maintained by Benjamin Herrenschmidt (benh@kernel.crashing.org)
 */

/*
 * bootup setup stuff..
 */

#include <linux/init.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/stddef.h>
#include <linux/unistd.h>
#include <linux/ptrace.h>
#include <linux/export.h>
#include <linux/user.h>
#include <linux/tty.h>
#include <linux/string.h>
#include <linux/delay.h>
#include <linux/ioport.h>
#include <linux/major.h>
#include <linux/initrd.h>
#include <linux/vt_kern.h>
#include <linux/console.h>
#include <linux/pci.h>
#include <linux/adb.h>
#include <linux/cuda.h>
#include <linux/pmu.h>
#include <linux/irq.h>
#include <linux/seq_file.h>
#include <linux/root_dev.h>
#include <linux/bitops.h>
#include <linux/suspend.h>
#include <linux/of_device.h>
#include <linux/of_platform.h>

#include <asm/reg.h>
#include <asm/sections.h>
#include <asm/io.h>
#include <asm/pci-bridge.h>
#include <asm/ohare.h>
#include <asm/mediabay.h>
#include <asm/machdep.h>
#include <asm/dma.h>
#include <asm/cputable.h>
#include <asm/btext.h>
#include <asm/pmac_feature.h>
#include <asm/time.h>
#include <asm/mmu_context.h>
#include <asm/iommu.h>
#include <asm/smu.h>
#include <asm/pmc.h>
#include <asm/udbg.h>

#include "pmac.h"

#undef SHOW_GATWICK_IRQS

static int has_l2cache;

int pmac_newworld;

static int current_root_goodness = -1;

#define DEFAULT_ROOT_DEVICE Root_SDA1	/* sda1 - slightly silly choice */

sys_ctrler_t sys_ctrler = SYS_CTRLER_UNKNOWN;
EXPORT_SYMBOL(sys_ctrler);

static void pmac_show_cpuinfo(struct seq_file *m)
{
	struct device_node *np;
	const char *pp;
	int plen;
	int mbmodel;
	unsigned int mbflags;
	char* mbname;

	mbmodel = pmac_call_feature(PMAC_FTR_GET_MB_INFO, NULL,
				    PMAC_MB_INFO_MODEL, 0);
	mbflags = pmac_call_feature(PMAC_FTR_GET_MB_INFO, NULL,
				    PMAC_MB_INFO_FLAGS, 0);
	if (pmac_call_feature(PMAC_FTR_GET_MB_INFO, NULL, PMAC_MB_INFO_NAME,
			      (long) &mbname) != 0)
		mbname = "Unknown";

	/* find motherboard type */
	seq_printf(m, "machine\t\t: ");
	np = of_find_node_by_path("/");
	if (np != NULL) {
		pp = of_get_property(np, "model", NULL);
		if (pp != NULL)
			seq_printf(m, "%s\n", pp);
		else
			seq_printf(m, "PowerMac\n");
		pp = of_get_property(np, "compatible", &plen);
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

	/* find l2 cache info */
	np = of_find_node_by_name(NULL, "l2-cache");
	if (np == NULL)
		np = of_find_node_by_type(NULL, "cache");
	if (np != NULL) {
		const unsigned int *ic =
			of_get_property(np, "i-cache-size", NULL);
		const unsigned int *dc =
			of_get_property(np, "d-cache-size", NULL);
		seq_printf(m, "L2 cache\t:");
		has_l2cache = 1;
		if (of_get_property(np, "cache-unified", NULL) && dc) {
			seq_printf(m, " %dK unified", *dc / 1024);
		} else {
			if (ic)
				seq_printf(m, " %dK instruction", *ic / 1024);
			if (dc)
				seq_printf(m, "%s %dK data",
					   (ic? " +": ""), *dc / 1024);
		}
		pp = of_get_property(np, "ram-type", NULL);
		if (pp)
			seq_printf(m, " %s", pp);
		seq_printf(m, "\n");
		of_node_put(np);
	}

	/* Indicate newworld/oldworld */
	seq_printf(m, "pmac-generation\t: %s\n",
		   pmac_newworld ? "NewWorld" : "OldWorld");
}

#ifndef CONFIG_ADB_CUDA
int __init find_via_cuda(void)
{
	struct device_node *dn = of_find_node_by_name(NULL, "via-cuda");

	if (!dn)
		return 0;
	of_node_put(dn);
	printk("WARNING ! Your machine is CUDA-based but your kernel\n");
	printk("          wasn't compiled with CONFIG_ADB_CUDA option !\n");
	return 0;
}
#endif

#ifndef CONFIG_ADB_PMU
int __init find_via_pmu(void)
{
	struct device_node *dn = of_find_node_by_name(NULL, "via-pmu");

	if (!dn)
		return 0;
	of_node_put(dn);
	printk("WARNING ! Your machine is PMU-based but your kernel\n");
	printk("          wasn't compiled with CONFIG_ADB_PMU option !\n");
	return 0;
}
#endif

#ifndef CONFIG_PMAC_SMU
int __init smu_init(void)
{
	/* should check and warn if SMU is present */
	return 0;
}
#endif

#ifdef CONFIG_PPC32
static volatile u32 *sysctrl_regs;

static void __init ohare_init(void)
{
	struct device_node *dn;

	/* this area has the CPU identification register
	   and some registers used by smp boards */
	sysctrl_regs = (volatile u32 *) ioremap(0xf8000000, 0x1000);

	/*
	 * Turn on the L2 cache.
	 * We assume that we have a PSX memory controller iff
	 * we have an ohare I/O controller.
	 */
	dn = of_find_node_by_name(NULL, "ohare");
	if (dn) {
		of_node_put(dn);
		if (((sysctrl_regs[2] >> 24) & 0xf) >= 3) {
			if (sysctrl_regs[4] & 0x10)
				sysctrl_regs[4] |= 0x04000020;
			else
				sysctrl_regs[4] |= 0x04000000;
			if(has_l2cache)
				printk(KERN_INFO "Level 2 cache enabled\n");
		}
	}
}

static void __init l2cr_init(void)
{
	/* Checks "l2cr-value" property in the registry */
	if (cpu_has_feature(CPU_FTR_L2CR)) {
		struct device_node *np;

		for_each_of_cpu_node(np) {
			const unsigned int *l2cr =
				of_get_property(np, "l2cr-value", NULL);
			if (l2cr) {
				_set_L2CR(0);
				_set_L2CR(*l2cr);
				pr_info("L2CR overridden (0x%x), backside cache is %s\n",
					*l2cr, ((*l2cr) & 0x80000000) ?
					"enabled" : "disabled");
			}
			of_node_put(np);
			break;
		}
	}
}
#endif

static void __init pmac_setup_arch(void)
{
	struct device_node *cpu, *ic;
	const int *fp;
	unsigned long pvr;

	pvr = PVR_VER(mfspr(SPRN_PVR));

	/* Set loops_per_jiffy to a half-way reasonable value,
	   for use until calibrate_delay gets called. */
	loops_per_jiffy = 50000000 / HZ;

	for_each_of_cpu_node(cpu) {
		fp = of_get_property(cpu, "clock-frequency", NULL);
		if (fp != NULL) {
			if (pvr >= 0x30 && pvr < 0x80)
				/* PPC970 etc. */
				loops_per_jiffy = *fp / (3 * HZ);
			else if (pvr == 4 || pvr >= 8)
				/* 604, G3, G4 etc. */
				loops_per_jiffy = *fp / HZ;
			else
				/* 603, etc. */
				loops_per_jiffy = *fp / (2 * HZ);
			of_node_put(cpu);
			break;
		}
	}

	/* See if newworld or oldworld */
	ic = of_find_node_with_property(NULL, "interrupt-controller");
	if (ic) {
		pmac_newworld = 1;
		of_node_put(ic);
	}

#ifdef CONFIG_PPC32
	ohare_init();
	l2cr_init();
#endif /* CONFIG_PPC32 */

	find_via_cuda();
	find_via_pmu();
	smu_init();

#if IS_ENABLED(CONFIG_NVRAM)
	pmac_nvram_init();
#endif
#ifdef CONFIG_PPC32
#ifdef CONFIG_BLK_DEV_INITRD
	if (initrd_start)
		ROOT_DEV = Root_RAM0;
	else
#endif
		ROOT_DEV = DEFAULT_ROOT_DEVICE;
#endif

#ifdef CONFIG_ADB
	if (strstr(boot_command_line, "adb_sync")) {
		extern int __adb_probe_sync;
		__adb_probe_sync = 1;
	}
#endif /* CONFIG_ADB */
}

static int initializing = 1;

static int pmac_late_init(void)
{
	initializing = 0;
	return 0;
}
machine_late_initcall(powermac, pmac_late_init);

void note_bootable_part(dev_t dev, int part, int goodness);
/*
 * This is __ref because we check for "initializing" before
 * touching any of the __init sensitive things and "initializing"
 * will be false after __init time. This can't be __init because it
 * can be called whenever a disk is first accessed.
 */
void __ref note_bootable_part(dev_t dev, int part, int goodness)
{
	char *p;

	if (!initializing)
		return;
	if ((goodness <= current_root_goodness) &&
	    ROOT_DEV != DEFAULT_ROOT_DEVICE)
		return;
	p = strstr(boot_command_line, "root=");
	if (p != NULL && (p == boot_command_line || p[-1] == ' '))
		return;

	ROOT_DEV = dev + part;
	current_root_goodness = goodness;
}

#ifdef CONFIG_ADB_CUDA
static void __noreturn cuda_restart(void)
{
	struct adb_request req;

	cuda_request(&req, NULL, 2, CUDA_PACKET, CUDA_RESET_SYSTEM);
	for (;;)
		cuda_poll();
}

static void __noreturn cuda_shutdown(void)
{
	struct adb_request req;

	cuda_request(&req, NULL, 2, CUDA_PACKET, CUDA_POWERDOWN);
	for (;;)
		cuda_poll();
}

#else
#define cuda_restart()
#define cuda_shutdown()
#endif

#ifndef CONFIG_ADB_PMU
#define pmu_restart()
#define pmu_shutdown()
#endif

#ifndef CONFIG_PMAC_SMU
#define smu_restart()
#define smu_shutdown()
#endif

static void __noreturn pmac_restart(char *cmd)
{
	switch (sys_ctrler) {
	case SYS_CTRLER_CUDA:
		cuda_restart();
		break;
	case SYS_CTRLER_PMU:
		pmu_restart();
		break;
	case SYS_CTRLER_SMU:
		smu_restart();
		break;
	default: ;
	}
	while (1) ;
}

static void __noreturn pmac_power_off(void)
{
	switch (sys_ctrler) {
	case SYS_CTRLER_CUDA:
		cuda_shutdown();
		break;
	case SYS_CTRLER_PMU:
		pmu_shutdown();
		break;
	case SYS_CTRLER_SMU:
		smu_shutdown();
		break;
	default: ;
	}
	while (1) ;
}

static void __noreturn
pmac_halt(void)
{
	pmac_power_off();
}

/* 
 * Early initialization.
 */
static void __init pmac_init(void)
{
	/* Enable early btext debug if requested */
	if (strstr(boot_command_line, "btextdbg")) {
		udbg_adb_init_early();
		register_early_udbg_console();
	}

	/* Probe motherboard chipset */
	pmac_feature_init();

	/* Initialize debug stuff */
	udbg_scc_init(!!strstr(boot_command_line, "sccdbg"));
	udbg_adb_init(!!strstr(boot_command_line, "btextdbg"));

#ifdef CONFIG_PPC64
	iommu_init_early_dart(&pmac_pci_controller_ops);
#endif

	/* SMP Init has to be done early as we need to patch up
	 * cpu_possible_mask before interrupt stacks are allocated
	 * or kaboom...
	 */
#ifdef CONFIG_SMP
	pmac_setup_smp();
#endif
}

static int __init pmac_declare_of_platform_devices(void)
{
	struct device_node *np;

	np = of_find_node_by_name(NULL, "valkyrie");
	if (np) {
		of_platform_device_create(np, "valkyrie", NULL);
		of_node_put(np);
	}
	np = of_find_node_by_name(NULL, "platinum");
	if (np) {
		of_platform_device_create(np, "platinum", NULL);
		of_node_put(np);
	}
        np = of_find_node_by_type(NULL, "smu");
        if (np) {
		of_platform_device_create(np, "smu", NULL);
		of_node_put(np);
	}
	np = of_find_node_by_type(NULL, "fcu");
	if (np == NULL) {
		/* Some machines have strangely broken device-tree */
		np = of_find_node_by_path("/u3@0,f8000000/i2c@f8001000/fan@15e");
	}
	if (np) {
		of_platform_device_create(np, "temperature", NULL);
		of_node_put(np);
	}

	return 0;
}
machine_device_initcall(powermac, pmac_declare_of_platform_devices);

#ifdef CONFIG_SERIAL_PMACZILOG_CONSOLE
/*
 * This is called very early, as part of console_init() (typically just after
 * time_init()). This function is respondible for trying to find a good
 * default console on serial ports. It tries to match the open firmware
 * default output with one of the available serial console drivers.
 */
static int __init check_pmac_serial_console(void)
{
	struct device_node *prom_stdout = NULL;
	int offset = 0;
	const char *name;
#ifdef CONFIG_SERIAL_PMACZILOG_TTYS
	char *devname = "ttyS";
#else
	char *devname = "ttyPZ";
#endif

	pr_debug(" -> check_pmac_serial_console()\n");

	/* The user has requested a console so this is already set up. */
	if (strstr(boot_command_line, "console=")) {
		pr_debug(" console was specified !\n");
		return -EBUSY;
	}

	if (!of_chosen) {
		pr_debug(" of_chosen is NULL !\n");
		return -ENODEV;
	}

	/* We are getting a weird phandle from OF ... */
	/* ... So use the full path instead */
	name = of_get_property(of_chosen, "linux,stdout-path", NULL);
	if (name == NULL) {
		pr_debug(" no linux,stdout-path !\n");
		return -ENODEV;
	}
	prom_stdout = of_find_node_by_path(name);
	if (!prom_stdout) {
		pr_debug(" can't find stdout package %s !\n", name);
		return -ENODEV;
	}
	pr_debug("stdout is %pOF\n", prom_stdout);

	if (of_node_name_eq(prom_stdout, "ch-a"))
		offset = 0;
	else if (of_node_name_eq(prom_stdout, "ch-b"))
		offset = 1;
	else
		goto not_found;
	of_node_put(prom_stdout);

	pr_debug("Found serial console at %s%d\n", devname, offset);

	return add_preferred_console(devname, offset, NULL);

 not_found:
	pr_debug("No preferred console found !\n");
	of_node_put(prom_stdout);
	return -ENODEV;
}
console_initcall(check_pmac_serial_console);

#endif /* CONFIG_SERIAL_PMACZILOG_CONSOLE */

/*
 * Called very early, MMU is off, device-tree isn't unflattened
 */
static int __init pmac_probe(void)
{
	if (!of_machine_is_compatible("Power Macintosh") &&
	    !of_machine_is_compatible("MacRISC"))
		return 0;

#ifdef CONFIG_PPC32
	/* isa_io_base gets set in pmac_pci_init */
	DMA_MODE_READ = 1;
	DMA_MODE_WRITE = 2;
#endif /* CONFIG_PPC32 */

	pm_power_off = pmac_power_off;

	pmac_init();

	return 1;
}

define_machine(powermac) {
	.name			= "PowerMac",
	.probe			= pmac_probe,
	.setup_arch		= pmac_setup_arch,
	.discover_phbs		= pmac_pci_init,
	.show_cpuinfo		= pmac_show_cpuinfo,
	.init_IRQ		= pmac_pic_init,
	.get_irq		= NULL,	/* changed later */
	.pci_irq_fixup		= pmac_pci_irq_fixup,
	.restart		= pmac_restart,
	.halt			= pmac_halt,
	.time_init		= pmac_time_init,
	.get_boot_time		= pmac_get_boot_time,
	.set_rtc_time		= pmac_set_rtc_time,
	.get_rtc_time		= pmac_get_rtc_time,
	.calibrate_decr		= pmac_calibrate_decr,
	.feature_call		= pmac_do_feature_call,
	.progress		= udbg_progress,
#ifdef CONFIG_PPC64
	.power_save		= power4_idle,
	.enable_pmcs		= power4_enable_pmcs,
#endif /* CONFIG_PPC64 */
#ifdef CONFIG_PPC32
	.pcibios_after_init	= pmac_pcibios_after_init,
	.phys_mem_access_prot	= pci_phys_mem_access_prot,
#endif
};
