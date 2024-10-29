// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * OpenRISC setup.c
 *
 * Linux architectural port borrowing liberally from similar works of
 * others.  All original copyrights apply as per the original source
 * declaration.
 *
 * Modifications for the OpenRISC architecture:
 * Copyright (C) 2003 Matjaz Breskvar <phoenix@bsemi.com>
 * Copyright (C) 2010-2011 Jonas Bonn <jonas@southpole.se>
 *
 * This file handles the architecture-dependent parts of initialization
 */

#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/stddef.h>
#include <linux/unistd.h>
#include <linux/ptrace.h>
#include <linux/slab.h>
#include <linux/tty.h>
#include <linux/ioport.h>
#include <linux/delay.h>
#include <linux/console.h>
#include <linux/init.h>
#include <linux/memblock.h>
#include <linux/seq_file.h>
#include <linux/serial.h>
#include <linux/initrd.h>
#include <linux/of_fdt.h>
#include <linux/of.h>
#include <linux/device.h>

#include <asm/sections.h>
#include <asm/types.h>
#include <asm/setup.h>
#include <asm/io.h>
#include <asm/cpuinfo.h>
#include <asm/delay.h>

#include "vmlinux.h"

static void __init setup_memory(void)
{
	unsigned long ram_start_pfn;
	unsigned long ram_end_pfn;
	phys_addr_t memory_start, memory_end;

	memory_end = memory_start = 0;

	/* Find main memory where is the kernel, we assume its the only one */
	memory_start = memblock_start_of_DRAM();
	memory_end = memblock_end_of_DRAM();

	if (!memory_end) {
		panic("No memory!");
	}

	ram_start_pfn = PFN_UP(memory_start);
	ram_end_pfn = PFN_DOWN(memblock_end_of_DRAM());

	/* setup bootmem globals (we use no_bootmem, but mm still depends on this) */
	min_low_pfn = ram_start_pfn;
	max_low_pfn = ram_end_pfn;
	max_pfn = ram_end_pfn;

	/*
	 * initialize the boot-time allocator (with low memory only).
	 *
	 * This makes the memory from the end of the kernel to the end of
	 * RAM usable.
	 */
	memblock_reserve(__pa(_stext), _end - _stext);

#ifdef CONFIG_BLK_DEV_INITRD
	/* Then reserve the initrd, if any */
	if (initrd_start && (initrd_end > initrd_start)) {
		unsigned long aligned_start = ALIGN_DOWN(initrd_start, PAGE_SIZE);
		unsigned long aligned_end = ALIGN(initrd_end, PAGE_SIZE);

		memblock_reserve(__pa(aligned_start), aligned_end - aligned_start);
	}
#endif /* CONFIG_BLK_DEV_INITRD */

	early_init_fdt_reserve_self();
	early_init_fdt_scan_reserved_mem();

	memblock_dump_all();
}

struct cpuinfo_or1k cpuinfo_or1k[NR_CPUS];

static void print_cpuinfo(void)
{
	unsigned long upr = mfspr(SPR_UPR);
	unsigned long vr = mfspr(SPR_VR);
	unsigned int version;
	unsigned int revision;
	struct cpuinfo_or1k *cpuinfo = &cpuinfo_or1k[smp_processor_id()];

	version = (vr & SPR_VR_VER) >> 24;
	revision = (vr & SPR_VR_REV);

	printk(KERN_INFO "CPU: OpenRISC-%x (revision %d) @%d MHz\n",
	       version, revision, cpuinfo->clock_frequency / 1000000);

	if (!(upr & SPR_UPR_UP)) {
		printk(KERN_INFO
		       "-- no UPR register... unable to detect configuration\n");
		return;
	}

	if (upr & SPR_UPR_DCP)
		printk(KERN_INFO
		       "-- dcache: %4d bytes total, %2d bytes/line, %d way(s)\n",
		       cpuinfo->dcache_size, cpuinfo->dcache_block_size,
		       cpuinfo->dcache_ways);
	else
		printk(KERN_INFO "-- dcache disabled\n");
	if (upr & SPR_UPR_ICP)
		printk(KERN_INFO
		       "-- icache: %4d bytes total, %2d bytes/line, %d way(s)\n",
		       cpuinfo->icache_size, cpuinfo->icache_block_size,
		       cpuinfo->icache_ways);
	else
		printk(KERN_INFO "-- icache disabled\n");

	if (upr & SPR_UPR_DMP)
		printk(KERN_INFO "-- dmmu: %4d entries, %lu way(s)\n",
		       1 << ((mfspr(SPR_DMMUCFGR) & SPR_DMMUCFGR_NTS) >> 2),
		       1 + (mfspr(SPR_DMMUCFGR) & SPR_DMMUCFGR_NTW));
	if (upr & SPR_UPR_IMP)
		printk(KERN_INFO "-- immu: %4d entries, %lu way(s)\n",
		       1 << ((mfspr(SPR_IMMUCFGR) & SPR_IMMUCFGR_NTS) >> 2),
		       1 + (mfspr(SPR_IMMUCFGR) & SPR_IMMUCFGR_NTW));

	printk(KERN_INFO "-- additional features:\n");
	if (upr & SPR_UPR_DUP)
		printk(KERN_INFO "-- debug unit\n");
	if (upr & SPR_UPR_PCUP)
		printk(KERN_INFO "-- performance counters\n");
	if (upr & SPR_UPR_PMP)
		printk(KERN_INFO "-- power management\n");
	if (upr & SPR_UPR_PICP)
		printk(KERN_INFO "-- PIC\n");
	if (upr & SPR_UPR_TTP)
		printk(KERN_INFO "-- timer\n");
	if (upr & SPR_UPR_CUP)
		printk(KERN_INFO "-- custom unit(s)\n");
}

static struct device_node *setup_find_cpu_node(int cpu)
{
	u32 hwid;
	struct device_node *cpun;

	for_each_of_cpu_node(cpun) {
		if (of_property_read_u32(cpun, "reg", &hwid))
			continue;
		if (hwid == cpu)
			return cpun;
	}

	return NULL;
}

void __init setup_cpuinfo(void)
{
	struct device_node *cpu;
	unsigned long iccfgr, dccfgr;
	unsigned long cache_set_size;
	int cpu_id = smp_processor_id();
	struct cpuinfo_or1k *cpuinfo = &cpuinfo_or1k[cpu_id];

	cpu = setup_find_cpu_node(cpu_id);
	if (!cpu)
		panic("Couldn't find CPU%d in device tree...\n", cpu_id);

	iccfgr = mfspr(SPR_ICCFGR);
	cpuinfo->icache_ways = 1 << (iccfgr & SPR_ICCFGR_NCW);
	cache_set_size = 1 << ((iccfgr & SPR_ICCFGR_NCS) >> 3);
	cpuinfo->icache_block_size = 16 << ((iccfgr & SPR_ICCFGR_CBS) >> 7);
	cpuinfo->icache_size =
	    cache_set_size * cpuinfo->icache_ways * cpuinfo->icache_block_size;

	dccfgr = mfspr(SPR_DCCFGR);
	cpuinfo->dcache_ways = 1 << (dccfgr & SPR_DCCFGR_NCW);
	cache_set_size = 1 << ((dccfgr & SPR_DCCFGR_NCS) >> 3);
	cpuinfo->dcache_block_size = 16 << ((dccfgr & SPR_DCCFGR_CBS) >> 7);
	cpuinfo->dcache_size =
	    cache_set_size * cpuinfo->dcache_ways * cpuinfo->dcache_block_size;

	if (of_property_read_u32(cpu, "clock-frequency",
				 &cpuinfo->clock_frequency)) {
		printk(KERN_WARNING
		       "Device tree missing CPU 'clock-frequency' parameter."
		       "Assuming frequency 25MHZ"
		       "This is probably not what you want.");
	}

	cpuinfo->coreid = mfspr(SPR_COREID);

	of_node_put(cpu);

	print_cpuinfo();
}

/**
 * or1k_early_setup
 * @fdt: pointer to the start of the device tree in memory or NULL
 *
 * Handles the pointer to the device tree that this kernel is to use
 * for establishing the available platform devices.
 *
 * Falls back on built-in device tree in case null pointer is passed.
 */

void __init or1k_early_setup(void *fdt)
{
	if (fdt)
		pr_info("FDT at %p\n", fdt);
	else {
		fdt = __dtb_start;
		pr_info("Compiled-in FDT at %p\n", fdt);
	}
	early_init_devtree(fdt);
}

static inline unsigned long extract_value_bits(unsigned long reg,
					       short bit_nr, short width)
{
	return (reg >> bit_nr) & (0 << width);
}

static inline unsigned long extract_value(unsigned long reg, unsigned long mask)
{
	while (!(mask & 0x1)) {
		reg = reg >> 1;
		mask = mask >> 1;
	}
	return mask & reg;
}

/*
 * calibrate_delay
 *
 * Lightweight calibrate_delay implementation that calculates loops_per_jiffy
 * from the clock frequency passed in via the device tree
 *
 */

void calibrate_delay(void)
{
	const int *val;
	struct device_node *cpu = setup_find_cpu_node(smp_processor_id());

	val = of_get_property(cpu, "clock-frequency", NULL);
	if (!val)
		panic("no cpu 'clock-frequency' parameter in device tree");
	loops_per_jiffy = *val / HZ;
	pr_cont("%lu.%02lu BogoMIPS (lpj=%lu)\n",
		loops_per_jiffy / (500000 / HZ),
		(loops_per_jiffy / (5000 / HZ)) % 100, loops_per_jiffy);

	of_node_put(cpu);
}

void __init setup_arch(char **cmdline_p)
{
	/* setup memblock allocator */
	setup_memory();

	unflatten_and_copy_device_tree();

	setup_cpuinfo();

#ifdef CONFIG_SMP
	smp_init_cpus();
#endif

	/* process 1's initial memory region is the kernel code/data */
	setup_initial_init_mm(_stext, _etext, _edata, _end);

#ifdef CONFIG_BLK_DEV_INITRD
	if (initrd_start == initrd_end) {
		printk(KERN_INFO "Initial ramdisk not found\n");
		initrd_start = 0;
		initrd_end = 0;
	} else {
		printk(KERN_INFO "Initial ramdisk at: 0x%p (%lu bytes)\n",
		       (void *)(initrd_start), initrd_end - initrd_start);
		initrd_below_start_ok = 1;
	}
#endif

	/* paging_init() sets up the MMU and marks all pages as reserved */
	paging_init();

	*cmdline_p = boot_command_line;

	printk(KERN_INFO "OpenRISC Linux -- http://openrisc.io\n");
}

static int show_cpuinfo(struct seq_file *m, void *v)
{
	unsigned int vr, cpucfgr;
	unsigned int avr;
	unsigned int version;
	struct cpuinfo_or1k *cpuinfo = v;

	vr = mfspr(SPR_VR);
	cpucfgr = mfspr(SPR_CPUCFGR);

#ifdef CONFIG_SMP
	seq_printf(m, "processor\t\t: %d\n", cpuinfo->coreid);
#endif
	if (vr & SPR_VR_UVRP) {
		vr = mfspr(SPR_VR2);
		version = vr & SPR_VR2_VER;
		avr = mfspr(SPR_AVR);
		seq_printf(m, "cpu architecture\t: "
			   "OpenRISC 1000 (%d.%d-rev%d)\n",
			   (avr >> 24) & 0xff,
			   (avr >> 16) & 0xff,
			   (avr >> 8) & 0xff);
		seq_printf(m, "cpu implementation id\t: 0x%x\n",
			   (vr & SPR_VR2_CPUID) >> 24);
		seq_printf(m, "cpu version\t\t: 0x%x\n", version);
	} else {
		version = (vr & SPR_VR_VER) >> 24;
		seq_printf(m, "cpu\t\t\t: OpenRISC-%x\n", version);
		seq_printf(m, "revision\t\t: %d\n", vr & SPR_VR_REV);
	}
	seq_printf(m, "frequency\t\t: %ld\n", loops_per_jiffy * HZ);
	seq_printf(m, "dcache size\t\t: %d bytes\n", cpuinfo->dcache_size);
	seq_printf(m, "dcache block size\t: %d bytes\n",
		   cpuinfo->dcache_block_size);
	seq_printf(m, "dcache ways\t\t: %d\n", cpuinfo->dcache_ways);
	seq_printf(m, "icache size\t\t: %d bytes\n", cpuinfo->icache_size);
	seq_printf(m, "icache block size\t: %d bytes\n",
		   cpuinfo->icache_block_size);
	seq_printf(m, "icache ways\t\t: %d\n", cpuinfo->icache_ways);
	seq_printf(m, "immu\t\t\t: %d entries, %lu ways\n",
		   1 << ((mfspr(SPR_DMMUCFGR) & SPR_DMMUCFGR_NTS) >> 2),
		   1 + (mfspr(SPR_DMMUCFGR) & SPR_DMMUCFGR_NTW));
	seq_printf(m, "dmmu\t\t\t: %d entries, %lu ways\n",
		   1 << ((mfspr(SPR_IMMUCFGR) & SPR_IMMUCFGR_NTS) >> 2),
		   1 + (mfspr(SPR_IMMUCFGR) & SPR_IMMUCFGR_NTW));
	seq_printf(m, "bogomips\t\t: %lu.%02lu\n",
		   (loops_per_jiffy * HZ) / 500000,
		   ((loops_per_jiffy * HZ) / 5000) % 100);

	seq_puts(m, "features\t\t: ");
	seq_printf(m, "%s ", cpucfgr & SPR_CPUCFGR_OB32S ? "orbis32" : "");
	seq_printf(m, "%s ", cpucfgr & SPR_CPUCFGR_OB64S ? "orbis64" : "");
	seq_printf(m, "%s ", cpucfgr & SPR_CPUCFGR_OF32S ? "orfpx32" : "");
	seq_printf(m, "%s ", cpucfgr & SPR_CPUCFGR_OF64S ? "orfpx64" : "");
	seq_printf(m, "%s ", cpucfgr & SPR_CPUCFGR_OV64S ? "orvdx64" : "");
	seq_puts(m, "\n");

	seq_puts(m, "\n");

	return 0;
}

static void *c_start(struct seq_file *m, loff_t *pos)
{
	*pos = cpumask_next(*pos - 1, cpu_online_mask);
	if ((*pos) < nr_cpu_ids)
		return &cpuinfo_or1k[*pos];
	return NULL;
}

static void *c_next(struct seq_file *m, void *v, loff_t *pos)
{
	(*pos)++;
	return c_start(m, pos);
}

static void c_stop(struct seq_file *m, void *v)
{
}

const struct seq_operations cpuinfo_op = {
	.start = c_start,
	.next = c_next,
	.stop = c_stop,
	.show = show_cpuinfo,
};
