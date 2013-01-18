/*
 * Copyright (C) 2004, 2007-2010, 2011-2012 Synopsys, Inc. (www.synopsys.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/seq_file.h>
#include <linux/fs.h>
#include <linux/delay.h>
#include <linux/root_dev.h>
#include <linux/console.h>
#include <linux/module.h>
#include <linux/cpu.h>
#include <linux/of_fdt.h>
#include <asm/sections.h>
#include <asm/arcregs.h>
#include <asm/tlb.h>
#include <asm/cache.h>
#include <asm/setup.h>
#include <asm/page.h>
#include <asm/irq.h>
#include <asm/arcregs.h>
#include <asm/prom.h>

#define FIX_PTR(x)  __asm__ __volatile__(";" : "+r"(x))

int running_on_hw = 1;	/* vs. on ISS */

char __initdata command_line[COMMAND_LINE_SIZE];

struct task_struct *_current_task[NR_CPUS];	/* For stack switching */

struct cpuinfo_arc cpuinfo_arc700[NR_CPUS];

void __init read_arc_build_cfg_regs(void)
{
	read_decode_mmu_bcr();
	read_decode_cache_bcr();
}

/*
 * Initialize and setup the processor core
 * This is called by all the CPUs thus should not do special case stuff
 *    such as only for boot CPU etc
 */

void __init setup_processor(void)
{
	read_arc_build_cfg_regs();
	arc_init_IRQ();
	arc_mmu_init();
	arc_cache_init();
}

void __init __attribute__((weak)) arc_platform_early_init(void)
{
}

void __init setup_arch(char **cmdline_p)
{
	int rc;

#ifdef CONFIG_CMDLINE_UBOOT
	/* Make sure that a whitespace is inserted before */
	strlcat(command_line, " ", sizeof(command_line));
#endif
	/*
	 * Append .config cmdline to base command line, which might already
	 * contain u-boot "bootargs" (handled by head.S, if so configured)
	 */
	strlcat(command_line, CONFIG_CMDLINE, sizeof(command_line));

	/* Save unparsed command line copy for /proc/cmdline */
	strlcpy(boot_command_line, command_line, COMMAND_LINE_SIZE);
	*cmdline_p = command_line;

	rc = setup_machine_fdt(__dtb_start);

	/* To force early parsing of things like mem=xxx */
	parse_early_param();

	/* Platform/board specific: e.g. early console registration */
	arc_platform_early_init();

	setup_processor();

#ifdef CONFIG_SMP
	smp_init_cpus();
#endif

	setup_arch_memory();

	unflatten_device_tree();

	/* Can be issue if someone passes cmd line arg "ro"
	 * But that is unlikely so keeping it as it is
	 */
	root_mountflags &= ~MS_RDONLY;

	console_verbose();

#if defined(CONFIG_VT) && defined(CONFIG_DUMMY_CONSOLE)
	conswitchp = &dummy_con;
#endif

}

/*
 *  Get CPU information for use by the procfs.
 */

#define cpu_to_ptr(c)	((void *)(0xFFFF0000 | (unsigned int)(c)))
#define ptr_to_cpu(p)	(~0xFFFF0000UL & (unsigned int)(p))

static int show_cpuinfo(struct seq_file *m, void *v)
{
	char *str;
	int cpu_id = ptr_to_cpu(v);

	str = (char *)__get_free_page(GFP_TEMPORARY);
	if (!str)
		goto done;

	seq_printf(m, "ARC700 #%d\n", cpu_id);

	seq_printf(m, "Bogo MIPS : \t%lu.%02lu\n",
		   loops_per_jiffy / (500000 / HZ),
		   (loops_per_jiffy / (5000 / HZ)) % 100);

	free_page((unsigned long)str);
done:
	seq_printf(m, "\n\n");

	return 0;
}

static void *c_start(struct seq_file *m, loff_t *pos)
{
	/*
	 * Callback returns cpu-id to iterator for show routine, NULL to stop.
	 * However since NULL is also a valid cpu-id (0), we use a round-about
	 * way to pass it w/o having to kmalloc/free a 2 byte string.
	 * Encode cpu-id as 0xFFcccc, which is decoded by show routine.
	 */
	return *pos < num_possible_cpus() ? cpu_to_ptr(*pos) : NULL;
}

static void *c_next(struct seq_file *m, void *v, loff_t *pos)
{
	++*pos;
	return c_start(m, pos);
}

static void c_stop(struct seq_file *m, void *v)
{
}

const struct seq_operations cpuinfo_op = {
	.start	= c_start,
	.next	= c_next,
	.stop	= c_stop,
	.show	= show_cpuinfo
};

static DEFINE_PER_CPU(struct cpu, cpu_topology);

static int __init topology_init(void)
{
	int cpu;

	for_each_present_cpu(cpu)
	    register_cpu(&per_cpu(cpu_topology, cpu), cpu);

	return 0;
}

subsys_initcall(topology_init);
