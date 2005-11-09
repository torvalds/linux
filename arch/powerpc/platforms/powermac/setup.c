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
#include <linux/suspend.h>

#include <asm/reg.h>
#include <asm/sections.h>
#include <asm/prom.h>
#include <asm/system.h>
#include <asm/pgtable.h>
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
#include <asm/of_device.h>
#include <asm/mmu_context.h>
#include <asm/iommu.h>
#include <asm/smu.h>
#include <asm/pmc.h>
#include <asm/mpic.h>
#include <asm/lmb.h>

#include "pmac.h"

#undef SHOW_GATWICK_IRQS

unsigned char drive_info;

int ppc_override_l2cr = 0;
int ppc_override_l2cr_value;
int has_l2cache = 0;

int pmac_newworld = 1;

static int current_root_goodness = -1;

extern int pmac_newworld;
extern struct machdep_calls pmac_md;

#define DEFAULT_ROOT_DEVICE Root_SDA1	/* sda1 - slightly silly choice */

#ifdef CONFIG_PPC64
#include <asm/udbg.h>
int sccdbg;
#endif

extern void zs_kgdb_hook(int tty_num);

sys_ctrler_t sys_ctrler = SYS_CTRLER_UNKNOWN;
EXPORT_SYMBOL(sys_ctrler);

#ifdef CONFIG_PMAC_SMU
unsigned long smu_cmdbuf_abs;
EXPORT_SYMBOL(smu_cmdbuf_abs);
#endif

#ifdef CONFIG_SMP
extern struct smp_ops_t psurge_smp_ops;
extern struct smp_ops_t core99_smp_ops;
#endif /* CONFIG_SMP */

static void pmac_show_cpuinfo(struct seq_file *m)
{
	struct device_node *np;
	char *pp;
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

	/* find l2 cache info */
	np = of_find_node_by_name(NULL, "l2-cache");
	if (np == NULL)
		np = of_find_node_by_type(NULL, "cache");
	if (np != NULL) {
		unsigned int *ic = (unsigned int *)
			get_property(np, "i-cache-size", NULL);
		unsigned int *dc = (unsigned int *)
			get_property(np, "d-cache-size", NULL);
		seq_printf(m, "L2 cache\t:");
		has_l2cache = 1;
		if (get_property(np, "cache-unified", NULL) != 0 && dc) {
			seq_printf(m, " %dK unified", *dc / 1024);
		} else {
			if (ic)
				seq_printf(m, " %dK instruction", *ic / 1024);
			if (dc)
				seq_printf(m, "%s %dK data",
					   (ic? " +": ""), *dc / 1024);
		}
		pp = get_property(np, "ram-type", NULL);
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
int find_via_cuda(void)
{
	if (!find_devices("via-cuda"))
		return 0;
	printk("WARNING ! Your machine is CUDA-based but your kernel\n");
	printk("          wasn't compiled with CONFIG_ADB_CUDA option !\n");
	return 0;
}
#endif

#ifndef CONFIG_ADB_PMU
int find_via_pmu(void)
{
	if (!find_devices("via-pmu"))
		return 0;
	printk("WARNING ! Your machine is PMU-based but your kernel\n");
	printk("          wasn't compiled with CONFIG_ADB_PMU option !\n");
	return 0;
}
#endif

#ifndef CONFIG_PMAC_SMU
int smu_init(void)
{
	/* should check and warn if SMU is present */
	return 0;
}
#endif

#ifdef CONFIG_PPC32
static volatile u32 *sysctrl_regs;

static void __init ohare_init(void)
{
	/* this area has the CPU identification register
	   and some registers used by smp boards */
	sysctrl_regs = (volatile u32 *) ioremap(0xf8000000, 0x1000);

	/*
	 * Turn on the L2 cache.
	 * We assume that we have a PSX memory controller iff
	 * we have an ohare I/O controller.
	 */
	if (find_devices("ohare") != NULL) {
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
		struct device_node *np = find_devices("cpus");
		if (np == 0)
			np = find_type_devices("cpu");
		if (np != 0) {
			unsigned int *l2cr = (unsigned int *)
				get_property(np, "l2cr-value", NULL);
			if (l2cr != 0) {
				ppc_override_l2cr = 1;
				ppc_override_l2cr_value = *l2cr;
				_set_L2CR(0);
				_set_L2CR(ppc_override_l2cr_value);
			}
		}
	}

	if (ppc_override_l2cr)
		printk(KERN_INFO "L2CR overridden (0x%x), "
		       "backside cache is %s\n",
		       ppc_override_l2cr_value,
		       (ppc_override_l2cr_value & 0x80000000)
				? "enabled" : "disabled");
}
#endif

void __init pmac_setup_arch(void)
{
	struct device_node *cpu, *ic;
	int *fp;
	unsigned long pvr;

	pvr = PVR_VER(mfspr(SPRN_PVR));

	/* Set loops_per_jiffy to a half-way reasonable value,
	   for use until calibrate_delay gets called. */
	loops_per_jiffy = 50000000 / HZ;
	cpu = of_find_node_by_type(NULL, "cpu");
	if (cpu != NULL) {
		fp = (int *) get_property(cpu, "clock-frequency", NULL);
		if (fp != NULL) {
			if (pvr >= 0x30 && pvr < 0x80)
				/* PPC970 etc. */
				loops_per_jiffy = *fp / (3 * HZ);
			else if (pvr == 4 || pvr >= 8)
				/* 604, G3, G4 etc. */
				loops_per_jiffy = *fp / HZ;
			else
				/* 601, 603, etc. */
				loops_per_jiffy = *fp / (2 * HZ);
		}
		of_node_put(cpu);
	}

	/* See if newworld or oldworld */
	for (ic = NULL; (ic = of_find_all_nodes(ic)) != NULL; )
		if (get_property(ic, "interrupt-controller", NULL))
			break;
	pmac_newworld = (ic != NULL);
	if (ic)
		of_node_put(ic);

	/* Lookup PCI hosts */
	pmac_pci_init();

#ifdef CONFIG_PPC32
	ohare_init();
	l2cr_init();
#endif /* CONFIG_PPC32 */

#ifdef CONFIG_PPC64
	/* Probe motherboard chipset */
	/* this is done earlier in setup_arch for 32-bit */
	pmac_feature_init();

	/* We can NAP */
	powersave_nap = 1;
	printk(KERN_INFO "Using native/NAP idle loop\n");
#endif

#ifdef CONFIG_KGDB
	zs_kgdb_hook(0);
#endif

	find_via_cuda();
	find_via_pmu();
	smu_init();

#if defined(CONFIG_NVRAM) || defined(CONFIG_PPC64)
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

#ifdef CONFIG_SMP
	/* Check for Core99 */
	if (find_devices("uni-n") || find_devices("u3"))
		smp_ops = &core99_smp_ops;
#ifdef CONFIG_PPC32
	else
		smp_ops = &psurge_smp_ops;
#endif
#endif /* CONFIG_SMP */
}

char *bootpath;
char *bootdevice;
void *boot_host;
int boot_target;
int boot_part;
extern dev_t boot_dev;

#ifdef CONFIG_SCSI
void __init note_scsi_host(struct device_node *node, void *host)
{
	int l;
	char *p;

	l = strlen(node->full_name);
	if (bootpath != NULL && bootdevice != NULL
	    && strncmp(node->full_name, bootdevice, l) == 0
	    && (bootdevice[l] == '/' || bootdevice[l] == 0)) {
		boot_host = host;
		/*
		 * There's a bug in OF 1.0.5.  (Why am I not surprised.)
		 * If you pass a path like scsi/sd@1:0 to canon, it returns
		 * something like /bandit@F2000000/gc@10/53c94@10000/sd@0,0
		 * That is, the scsi target number doesn't get preserved.
		 * So we pick the target number out of bootpath and use that.
		 */
		p = strstr(bootpath, "/sd@");
		if (p != NULL) {
			p += 4;
			boot_target = simple_strtoul(p, NULL, 10);
			p = strchr(p, ':');
			if (p != NULL)
				boot_part = simple_strtoul(p + 1, NULL, 10);
		}
	}
}
EXPORT_SYMBOL(note_scsi_host);
#endif

#if defined(CONFIG_BLK_DEV_IDE) && defined(CONFIG_BLK_DEV_IDE_PMAC)
static dev_t __init find_ide_boot(void)
{
	char *p;
	int n;
	dev_t __init pmac_find_ide_boot(char *bootdevice, int n);

	if (bootdevice == NULL)
		return 0;
	p = strrchr(bootdevice, '/');
	if (p == NULL)
		return 0;
	n = p - bootdevice;

	return pmac_find_ide_boot(bootdevice, n);
}
#endif /* CONFIG_BLK_DEV_IDE && CONFIG_BLK_DEV_IDE_PMAC */

static void __init find_boot_device(void)
{
#if defined(CONFIG_BLK_DEV_IDE) && defined(CONFIG_BLK_DEV_IDE_PMAC)
	boot_dev = find_ide_boot();
#endif
}

/* TODO: Merge the suspend-to-ram with the common code !!!
 * currently, this is a stub implementation for suspend-to-disk
 * only
 */

#ifdef CONFIG_SOFTWARE_SUSPEND

static int pmac_pm_prepare(suspend_state_t state)
{
	printk(KERN_DEBUG "%s(%d)\n", __FUNCTION__, state);

	return 0;
}

static int pmac_pm_enter(suspend_state_t state)
{
	printk(KERN_DEBUG "%s(%d)\n", __FUNCTION__, state);

	/* Giveup the lazy FPU & vec so we don't have to back them
	 * up from the low level code
	 */
	enable_kernel_fp();

#ifdef CONFIG_ALTIVEC
	if (cur_cpu_spec->cpu_features & CPU_FTR_ALTIVEC)
		enable_kernel_altivec();
#endif /* CONFIG_ALTIVEC */

	return 0;
}

static int pmac_pm_finish(suspend_state_t state)
{
	printk(KERN_DEBUG "%s(%d)\n", __FUNCTION__, state);

	/* Restore userland MMU context */
	set_context(current->active_mm->context, current->active_mm->pgd);

	return 0;
}

static struct pm_ops pmac_pm_ops = {
	.pm_disk_mode	= PM_DISK_SHUTDOWN,
	.prepare	= pmac_pm_prepare,
	.enter		= pmac_pm_enter,
	.finish		= pmac_pm_finish,
};

#endif /* CONFIG_SOFTWARE_SUSPEND */

static int initializing = 1;

static int pmac_late_init(void)
{
	initializing = 0;
#ifdef CONFIG_SOFTWARE_SUSPEND
	pm_set_ops(&pmac_pm_ops);
#endif /* CONFIG_SOFTWARE_SUSPEND */
	return 0;
}

late_initcall(pmac_late_init);

/* can't be __init - can be called whenever a disk is first accessed */
void note_bootable_part(dev_t dev, int part, int goodness)
{
	static int found_boot = 0;
	char *p;

	if (!initializing)
		return;
	if ((goodness <= current_root_goodness) &&
	    ROOT_DEV != DEFAULT_ROOT_DEVICE)
		return;
	p = strstr(saved_command_line, "root=");
	if (p != NULL && (p == saved_command_line || p[-1] == ' '))
		return;

	if (!found_boot) {
		find_boot_device();
		found_boot = 1;
	}
	if (!boot_dev || dev == boot_dev) {
		ROOT_DEV = dev + part;
		boot_dev = 0;
		current_root_goodness = goodness;
	}
}

#ifdef CONFIG_ADB_CUDA
static void cuda_restart(void)
{
	struct adb_request req;

	cuda_request(&req, NULL, 2, CUDA_PACKET, CUDA_RESET_SYSTEM);
	for (;;)
		cuda_poll();
}

static void cuda_shutdown(void)
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

static void pmac_restart(char *cmd)
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
}

static void pmac_power_off(void)
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
}

static void
pmac_halt(void)
{
	pmac_power_off();
}

#ifdef CONFIG_PPC32
void __init pmac_init(void)
{
	/* isa_io_base gets set in pmac_pci_init */
	isa_mem_base = PMAC_ISA_MEM_BASE;
	pci_dram_offset = PMAC_PCI_DRAM_OFFSET;
	ISA_DMA_THRESHOLD = ~0L;
	DMA_MODE_READ = 1;
	DMA_MODE_WRITE = 2;

	ppc_md = pmac_md;

#if defined(CONFIG_BLK_DEV_IDE) || defined(CONFIG_BLK_DEV_IDE_MODULE)
#ifdef CONFIG_BLK_DEV_IDE_PMAC
        ppc_ide_md.ide_init_hwif	= pmac_ide_init_hwif_ports;
        ppc_ide_md.default_io_base	= pmac_ide_get_base;
#endif /* CONFIG_BLK_DEV_IDE_PMAC */
#endif /* defined(CONFIG_BLK_DEV_IDE) || defined(CONFIG_BLK_DEV_IDE_MODULE) */

	if (ppc_md.progress) ppc_md.progress("pmac_init(): exit", 0);

}
#endif

/* 
 * Early initialization.
 */
static void __init pmac_init_early(void)
{
#ifdef CONFIG_PPC64
	/* Initialize hash table, from now on, we can take hash faults
	 * and call ioremap
	 */
	hpte_init_native();

	/* Init SCC */
	if (strstr(cmd_line, "sccdbg")) {
		sccdbg = 1;
		udbg_init_scc(NULL);
	}

	/* Setup interrupt mapping options */
	ppc64_interrupt_controller = IC_OPEN_PIC;

	iommu_init_early_u3();
#endif
}

static void __init pmac_progress(char *s, unsigned short hex)
{
#ifdef CONFIG_PPC64
	if (sccdbg) {
		udbg_puts(s);
		udbg_puts("\n");
		return;
	}
#endif
#ifdef CONFIG_BOOTX_TEXT
	if (boot_text_mapped) {
		btext_drawstring(s);
		btext_drawchar('\n');
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

	np = find_devices("uni-n");
	if (np) {
		for (np = np->child; np != NULL; np = np->sibling)
			if (strncmp(np->name, "i2c", 3) == 0) {
				of_platform_device_create(np, "uni-n-i2c",
							  NULL);
				break;
			}
	}
	np = find_devices("valkyrie");
	if (np)
		of_platform_device_create(np, "valkyrie", NULL);
	np = find_devices("platinum");
	if (np)
		of_platform_device_create(np, "platinum", NULL);

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
        np = of_find_node_by_type(NULL, "smu");
        if (np) {
		of_platform_device_create(np, "smu", NULL);
		of_node_put(np);
	}

	return 0;
}

device_initcall(pmac_declare_of_platform_devices);

/*
 * Called very early, MMU is off, device-tree isn't unflattened
 */
static int __init pmac_probe(int platform)
{
#ifdef CONFIG_PPC64
	if (platform != PLATFORM_POWERMAC)
		return 0;

	/*
	 * On U3, the DART (iommu) must be allocated now since it
	 * has an impact on htab_initialize (due to the large page it
	 * occupies having to be broken up so the DART itself is not
	 * part of the cacheable linar mapping
	 */
	alloc_u3_dart_table();
#endif

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

#ifdef CONFIG_PPC64
static int pmac_probe_mode(struct pci_bus *bus)
{
	struct device_node *node = bus->sysdata;

	/* We need to use normal PCI probing for the AGP bus,
	   since the device for the AGP bridge isn't in the tree. */
	if (bus->self == NULL && device_is_compatible(node, "u3-agp"))
		return PCI_PROBE_NORMAL;

	return PCI_PROBE_DEVTREE;
}
#endif

struct machdep_calls __initdata pmac_md = {
#if defined(CONFIG_HOTPLUG_CPU) && defined(CONFIG_PPC64)
	.cpu_die		= generic_mach_cpu_die,
#endif
	.probe			= pmac_probe,
	.setup_arch		= pmac_setup_arch,
	.init_early		= pmac_init_early,
	.show_cpuinfo		= pmac_show_cpuinfo,
	.init_IRQ		= pmac_pic_init,
	.get_irq		= mpic_get_irq,	/* changed later */
	.pcibios_fixup		= pmac_pcibios_fixup,
	.restart		= pmac_restart,
	.power_off		= pmac_power_off,
	.halt			= pmac_halt,
	.time_init		= pmac_time_init,
	.get_boot_time		= pmac_get_boot_time,
	.set_rtc_time		= pmac_set_rtc_time,
	.get_rtc_time		= pmac_get_rtc_time,
	.calibrate_decr		= pmac_calibrate_decr,
	.feature_call		= pmac_do_feature_call,
	.check_legacy_ioport	= pmac_check_legacy_ioport,
	.progress		= pmac_progress,
#ifdef CONFIG_PPC64
	.pci_probe_mode		= pmac_probe_mode,
	.idle_loop		= native_idle,
	.enable_pmcs		= power4_enable_pmcs,
#endif
#ifdef CONFIG_PPC32
	.pcibios_enable_device_hook = pmac_pci_enable_device_hook,
	.pcibios_after_init	= pmac_pcibios_after_init,
	.phys_mem_access_prot	= pci_phys_mem_access_prot,
#endif
};
