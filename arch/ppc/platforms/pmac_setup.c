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
#include <asm/bootx.h>
#include <asm/cputable.h>
#include <asm/btext.h>
#include <asm/pmac_feature.h>
#include <asm/time.h>
#include <asm/of_device.h>
#include <asm/mmu_context.h>

#include "pmac_pic.h"
#include "mem_pieces.h"

#undef SHOW_GATWICK_IRQS

extern long pmac_time_init(void);
extern unsigned long pmac_get_rtc_time(void);
extern int pmac_set_rtc_time(unsigned long nowtime);
extern void pmac_read_rtc_time(void);
extern void pmac_calibrate_decr(void);
extern void pmac_pcibios_fixup(void);
extern void pmac_find_bridges(void);
extern unsigned long pmac_ide_get_base(int index);
extern void pmac_ide_init_hwif_ports(hw_regs_t *hw,
	unsigned long data_port, unsigned long ctrl_port, int *irq);

extern void pmac_nvram_update(void);
extern unsigned char pmac_nvram_read_byte(int addr);
extern void pmac_nvram_write_byte(int addr, unsigned char val);
extern int pmac_pci_enable_device_hook(struct pci_dev *dev, int initial);
extern void pmac_pcibios_after_init(void);
extern int of_show_percpuinfo(struct seq_file *m, int i);

struct device_node *memory_node;

unsigned char drive_info;

int ppc_override_l2cr = 0;
int ppc_override_l2cr_value;
int has_l2cache = 0;

static int current_root_goodness = -1;

extern int pmac_newworld;

#define DEFAULT_ROOT_DEVICE Root_SDA1	/* sda1 - slightly silly choice */

extern void zs_kgdb_hook(int tty_num);
static void ohare_init(void);
#ifdef CONFIG_BOOTX_TEXT
static void pmac_progress(char *s, unsigned short hex);
#endif

sys_ctrler_t sys_ctrler = SYS_CTRLER_UNKNOWN;

#ifdef CONFIG_SMP
extern struct smp_ops_t psurge_smp_ops;
extern struct smp_ops_t core99_smp_ops;
#endif /* CONFIG_SMP */

static int __pmac
pmac_show_cpuinfo(struct seq_file *m)
{
	struct device_node *np;
	char *pp;
	int plen;
	int mbmodel = pmac_call_feature(PMAC_FTR_GET_MB_INFO,
		NULL, PMAC_MB_INFO_MODEL, 0);
	unsigned int mbflags = (unsigned int)pmac_call_feature(PMAC_FTR_GET_MB_INFO,
		NULL, PMAC_MB_INFO_FLAGS, 0);
	char* mbname;

	if (pmac_call_feature(PMAC_FTR_GET_MB_INFO, NULL, PMAC_MB_INFO_NAME, (int)&mbname) != 0)
		mbname = "Unknown";

	/* find motherboard type */
	seq_printf(m, "machine\t\t: ");
	np = find_devices("device-tree");
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
	} else
		seq_printf(m, "PowerMac\n");

	/* print parsed model */
	seq_printf(m, "detected as\t: %d (%s)\n", mbmodel, mbname);
	seq_printf(m, "pmac flags\t: %08x\n", mbflags);

	/* find l2 cache info */
	np = find_devices("l2-cache");
	if (np == 0)
		np = find_type_devices("cache");
	if (np != 0) {
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
	}

	/* find ram info */
	np = find_devices("memory");
	if (np != 0) {
		int n;
		struct reg_property *reg = (struct reg_property *)
			get_property(np, "reg", &n);

		if (reg != 0) {
			unsigned long total = 0;

			for (n /= sizeof(struct reg_property); n > 0; --n)
				total += (reg++)->size;
			seq_printf(m, "memory\t\t: %luMB\n", total >> 20);
		}
	}

	/* Checks "l2cr-value" property in the registry */
	np = find_devices("cpus");
	if (np == 0)
		np = find_type_devices("cpu");
	if (np != 0) {
		unsigned int *l2cr = (unsigned int *)
			get_property(np, "l2cr-value", NULL);
		if (l2cr != 0) {
			seq_printf(m, "l2cr override\t: 0x%x\n", *l2cr);
		}
	}

	/* Indicate newworld/oldworld */
	seq_printf(m, "pmac-generation\t: %s\n",
		   pmac_newworld ? "NewWorld" : "OldWorld");


	return 0;
}

static int __openfirmware
pmac_show_percpuinfo(struct seq_file *m, int i)
{
#ifdef CONFIG_CPU_FREQ_PMAC
	extern unsigned int pmac_get_one_cpufreq(int i);
	unsigned int freq = pmac_get_one_cpufreq(i);
	if (freq != 0) {
		seq_printf(m, "clock\t\t: %dMHz\n", freq/1000);
		return 0;
	}
#endif /* CONFIG_CPU_FREQ_PMAC */
	return of_show_percpuinfo(m, i);
}

static volatile u32 *sysctrl_regs;

void __init
pmac_setup_arch(void)
{
	struct device_node *cpu;
	int *fp;
	unsigned long pvr;

	pvr = PVR_VER(mfspr(SPRN_PVR));

	/* Set loops_per_jiffy to a half-way reasonable value,
	   for use until calibrate_delay gets called. */
	cpu = find_type_devices("cpu");
	if (cpu != 0) {
		fp = (int *) get_property(cpu, "clock-frequency", NULL);
		if (fp != 0) {
			if (pvr == 4 || pvr >= 8)
				/* 604, G3, G4 etc. */
				loops_per_jiffy = *fp / HZ;
			else
				/* 601, 603, etc. */
				loops_per_jiffy = *fp / (2*HZ);
		} else
			loops_per_jiffy = 50000000 / HZ;
	}

	/* this area has the CPU identification register
	   and some registers used by smp boards */
	sysctrl_regs = (volatile u32 *) ioremap(0xf8000000, 0x1000);
	ohare_init();

	/* Lookup PCI hosts */
	pmac_find_bridges();

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
		printk(KERN_INFO "L2CR overriden (0x%x), backside cache is %s\n",
			ppc_override_l2cr_value, (ppc_override_l2cr_value & 0x80000000)
				? "enabled" : "disabled");

#ifdef CONFIG_KGDB
	zs_kgdb_hook(0);
#endif

#ifdef CONFIG_ADB_CUDA
	find_via_cuda();
#else
	if (find_devices("via-cuda")) {
		printk("WARNING ! Your machine is Cuda based but your kernel\n");
		printk("          wasn't compiled with CONFIG_ADB_CUDA option !\n");
	}
#endif
#ifdef CONFIG_ADB_PMU
	find_via_pmu();
#else
	if (find_devices("via-pmu")) {
		printk("WARNING ! Your machine is PMU based but your kernel\n");
		printk("          wasn't compiled with CONFIG_ADB_PMU option !\n");
	}
#endif
#ifdef CONFIG_NVRAM
	pmac_nvram_init();
#endif
#ifdef CONFIG_BLK_DEV_INITRD
	if (initrd_start)
		ROOT_DEV = Root_RAM0;
	else
#endif
		ROOT_DEV = DEFAULT_ROOT_DEVICE;

#ifdef CONFIG_SMP
	/* Check for Core99 */
	if (find_devices("uni-n") || find_devices("u3"))
		ppc_md.smp_ops = &core99_smp_ops;
	else
		ppc_md.smp_ops = &psurge_smp_ops;
#endif /* CONFIG_SMP */

	pci_create_OF_bus_map();
}

static void __init ohare_init(void)
{
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

extern char *bootpath;
extern char *bootdevice;
void *boot_host;
int boot_target;
int boot_part;
extern dev_t boot_dev;

#ifdef CONFIG_SCSI
void __init
note_scsi_host(struct device_node *node, void *host)
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
#endif

#if defined(CONFIG_BLK_DEV_IDE) && defined(CONFIG_BLK_DEV_IDE_PMAC)
static dev_t __init
find_ide_boot(void)
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

static void __init
find_boot_device(void)
{
#if defined(CONFIG_BLK_DEV_IDE) && defined(CONFIG_BLK_DEV_IDE_PMAC)
	boot_dev = find_ide_boot();
#endif
}

static int initializing = 1;
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
	if (cur_cpu_spec[0]->cpu_features & CPU_FTR_ALTIVEC)
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
void __pmac
note_bootable_part(dev_t dev, int part, int goodness)
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

static void __pmac
pmac_restart(char *cmd)
{
#ifdef CONFIG_ADB_CUDA
	struct adb_request req;
#endif /* CONFIG_ADB_CUDA */

	switch (sys_ctrler) {
#ifdef CONFIG_ADB_CUDA
	case SYS_CTRLER_CUDA:
		cuda_request(&req, NULL, 2, CUDA_PACKET,
			     CUDA_RESET_SYSTEM);
		for (;;)
			cuda_poll();
		break;
#endif /* CONFIG_ADB_CUDA */
#ifdef CONFIG_ADB_PMU
	case SYS_CTRLER_PMU:
		pmu_restart();
		break;
#endif /* CONFIG_ADB_PMU */
	default: ;
	}
}

static void __pmac
pmac_power_off(void)
{
#ifdef CONFIG_ADB_CUDA
	struct adb_request req;
#endif /* CONFIG_ADB_CUDA */

	switch (sys_ctrler) {
#ifdef CONFIG_ADB_CUDA
	case SYS_CTRLER_CUDA:
		cuda_request(&req, NULL, 2, CUDA_PACKET,
			     CUDA_POWERDOWN);
		for (;;)
			cuda_poll();
		break;
#endif /* CONFIG_ADB_CUDA */
#ifdef CONFIG_ADB_PMU
	case SYS_CTRLER_PMU:
		pmu_shutdown();
		break;
#endif /* CONFIG_ADB_PMU */
	default: ;
	}
}

static void __pmac
pmac_halt(void)
{
   pmac_power_off();
}

/*
 * Read in a property describing some pieces of memory.
 */

static int __init
get_mem_prop(char *name, struct mem_pieces *mp)
{
	struct reg_property *rp;
	int i, s;
	unsigned int *ip;
	int nac = prom_n_addr_cells(memory_node);
	int nsc = prom_n_size_cells(memory_node);

	ip = (unsigned int *) get_property(memory_node, name, &s);
	if (ip == NULL) {
		printk(KERN_ERR "error: couldn't get %s property on /memory\n",
		       name);
		return 0;
	}
	s /= (nsc + nac) * 4;
	rp = mp->regions;
	for (i = 0; i < s; ++i, ip += nac+nsc) {
		if (nac >= 2 && ip[nac-2] != 0)
			continue;
		rp->address = ip[nac-1];
		if (nsc >= 2 && ip[nac+nsc-2] != 0)
			rp->size = ~0U;
		else
			rp->size = ip[nac+nsc-1];
		++rp;
	}
	mp->n_regions = rp - mp->regions;

	/* Make sure the pieces are sorted. */
	mem_pieces_sort(mp);
	mem_pieces_coalesce(mp);
	return 1;
}

/*
 * On systems with Open Firmware, collect information about
 * physical RAM and which pieces are already in use.
 * At this point, we have (at least) the first 8MB mapped with a BAT.
 * Our text, data, bss use something over 1MB, starting at 0.
 * Open Firmware may be using 1MB at the 4MB point.
 */
unsigned long __init
pmac_find_end_of_memory(void)
{
	unsigned long a, total;
	struct mem_pieces phys_mem;

	/*
	 * Find out where physical memory is, and check that it
	 * starts at 0 and is contiguous.  It seems that RAM is
	 * always physically contiguous on Power Macintoshes.
	 *
	 * Supporting discontiguous physical memory isn't hard,
	 * it just makes the virtual <-> physical mapping functions
	 * more complicated (or else you end up wasting space
	 * in mem_map).
	 */
	memory_node = find_devices("memory");
	if (memory_node == NULL || !get_mem_prop("reg", &phys_mem)
	    || phys_mem.n_regions == 0)
		panic("No RAM??");
	a = phys_mem.regions[0].address;
	if (a != 0)
		panic("RAM doesn't start at physical address 0");
	total = phys_mem.regions[0].size;

	if (phys_mem.n_regions > 1) {
		printk("RAM starting at 0x%x is not contiguous\n",
		       phys_mem.regions[1].address);
		printk("Using RAM from 0 to 0x%lx\n", total-1);
	}

	return total;
}

void __init
pmac_init(unsigned long r3, unsigned long r4, unsigned long r5,
	  unsigned long r6, unsigned long r7)
{
	/* isa_io_base gets set in pmac_find_bridges */
	isa_mem_base = PMAC_ISA_MEM_BASE;
	pci_dram_offset = PMAC_PCI_DRAM_OFFSET;
	ISA_DMA_THRESHOLD = ~0L;
	DMA_MODE_READ = 1;
	DMA_MODE_WRITE = 2;

	ppc_md.setup_arch     = pmac_setup_arch;
	ppc_md.show_cpuinfo   = pmac_show_cpuinfo;
	ppc_md.show_percpuinfo = pmac_show_percpuinfo;
	ppc_md.irq_canonicalize = NULL;
	ppc_md.init_IRQ       = pmac_pic_init;
	ppc_md.get_irq        = pmac_get_irq; /* Changed later on ... */

	ppc_md.pcibios_fixup  = pmac_pcibios_fixup;
	ppc_md.pcibios_enable_device_hook = pmac_pci_enable_device_hook;
	ppc_md.pcibios_after_init = pmac_pcibios_after_init;
	ppc_md.phys_mem_access_prot = pci_phys_mem_access_prot;

	ppc_md.restart        = pmac_restart;
	ppc_md.power_off      = pmac_power_off;
	ppc_md.halt           = pmac_halt;

	ppc_md.time_init      = pmac_time_init;
	ppc_md.set_rtc_time   = pmac_set_rtc_time;
	ppc_md.get_rtc_time   = pmac_get_rtc_time;
	ppc_md.calibrate_decr = pmac_calibrate_decr;

	ppc_md.find_end_of_memory = pmac_find_end_of_memory;

	ppc_md.feature_call   = pmac_do_feature_call;

#if defined(CONFIG_BLK_DEV_IDE) || defined(CONFIG_BLK_DEV_IDE_MODULE)
#ifdef CONFIG_BLK_DEV_IDE_PMAC
        ppc_ide_md.ide_init_hwif	= pmac_ide_init_hwif_ports;
        ppc_ide_md.default_io_base	= pmac_ide_get_base;
#endif /* CONFIG_BLK_DEV_IDE_PMAC */
#endif /* defined(CONFIG_BLK_DEV_IDE) || defined(CONFIG_BLK_DEV_IDE_MODULE) */

#ifdef CONFIG_BOOTX_TEXT
	ppc_md.progress = pmac_progress;
#endif /* CONFIG_BOOTX_TEXT */

	if (ppc_md.progress) ppc_md.progress("pmac_init(): exit", 0);

}

#ifdef CONFIG_BOOTX_TEXT
static void __init
pmac_progress(char *s, unsigned short hex)
{
	if (boot_text_mapped) {
		btext_drawstring(s);
		btext_drawchar('\n');
	}
}
#endif /* CONFIG_BOOTX_TEXT */

static int __init
pmac_declare_of_platform_devices(void)
{
	struct device_node *np;

	np = find_devices("uni-n");
	if (np) {
		for (np = np->child; np != NULL; np = np->sibling)
			if (strncmp(np->name, "i2c", 3) == 0) {
				of_platform_device_create(np, "uni-n-i2c",
							  NULL);
				break;
			}
	}
	np = find_devices("u3");
	if (np) {
		for (np = np->child; np != NULL; np = np->sibling)
			if (strncmp(np->name, "i2c", 3) == 0) {
				of_platform_device_create(np, "u3-i2c",
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

	return 0;
}

device_initcall(pmac_declare_of_platform_devices);
