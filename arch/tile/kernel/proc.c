/*
 * Copyright 2010 Tilera Corporation. All Rights Reserved.
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 *   NON INFRINGEMENT.  See the GNU General Public License for
 *   more details.
 */

#include <linux/smp.h>
#include <linux/seq_file.h>
#include <linux/threads.h>
#include <linux/cpumask.h>
#include <linux/timex.h>
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/proc_fs.h>
#include <linux/sysctl.h>
#include <linux/hardirq.h>
#include <linux/mman.h>
#include <asm/pgtable.h>
#include <asm/processor.h>
#include <asm/sections.h>
#include <asm/homecache.h>
#include <arch/chip.h>


/*
 * Support /proc/cpuinfo
 */

#define cpu_to_ptr(n) ((void *)((long)(n)+1))
#define ptr_to_cpu(p) ((long)(p) - 1)

static int show_cpuinfo(struct seq_file *m, void *v)
{
	int n = ptr_to_cpu(v);

	if (n == 0) {
		char buf[NR_CPUS*5];
		cpulist_scnprintf(buf, sizeof(buf), cpu_online_mask);
		seq_printf(m, "cpu count\t: %d\n", num_online_cpus());
		seq_printf(m, "cpu list\t: %s\n", buf);
		seq_printf(m, "model name\t: %s\n", chip_model);
		seq_printf(m, "flags\t\t:\n");  /* nothing for now */
		seq_printf(m, "cpu MHz\t\t: %llu.%06llu\n",
			   get_clock_rate() / 1000000,
			   (get_clock_rate() % 1000000));
		seq_printf(m, "bogomips\t: %lu.%02lu\n\n",
			   loops_per_jiffy/(500000/HZ),
			   (loops_per_jiffy/(5000/HZ)) % 100);
	}

#ifdef CONFIG_SMP
	if (!cpu_online(n))
		return 0;
#endif

	seq_printf(m, "processor\t: %d\n", n);

	/* Print only num_online_cpus() blank lines total. */
	if (cpumask_next(n, cpu_online_mask) < nr_cpu_ids)
		seq_printf(m, "\n");

	return 0;
}

static void *c_start(struct seq_file *m, loff_t *pos)
{
	return *pos < nr_cpu_ids ? cpu_to_ptr(*pos) : NULL;
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
	.show	= show_cpuinfo,
};
