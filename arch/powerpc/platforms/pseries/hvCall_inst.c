// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2006 Mike Kravetz IBM Corporation
 *
 * Hypervisor Call Instrumentation
 */

#include <linux/kernel.h>
#include <linux/percpu.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/cpumask.h>
#include <asm/hvcall.h>
#include <asm/firmware.h>
#include <asm/cputable.h>
#include <asm/trace.h>
#include <asm/machdep.h>

/* For hcall instrumentation. One structure per-hcall, per-CPU */
struct hcall_stats {
	unsigned long	num_calls;	/* number of calls (on this CPU) */
	unsigned long	tb_total;	/* total wall time (mftb) of calls. */
	unsigned long	purr_total;	/* total cpu time (PURR) of calls. */
	unsigned long	tb_start;
	unsigned long	purr_start;
};
#define HCALL_STAT_ARRAY_SIZE	((MAX_HCALL_OPCODE >> 2) + 1)

DEFINE_PER_CPU(struct hcall_stats[HCALL_STAT_ARRAY_SIZE], hcall_stats);

/*
 * Routines for displaying the statistics in debugfs
 */
static void *hc_start(struct seq_file *m, loff_t *pos)
{
	if ((int)*pos < (HCALL_STAT_ARRAY_SIZE-1))
		return (void *)(unsigned long)(*pos + 1);

	return NULL;
}

static void *hc_next(struct seq_file *m, void *p, loff_t * pos)
{
	++*pos;

	return hc_start(m, pos);
}

static void hc_stop(struct seq_file *m, void *p)
{
}

static int hc_show(struct seq_file *m, void *p)
{
	unsigned long h_num = (unsigned long)p;
	struct hcall_stats *hs = m->private;

	if (hs[h_num].num_calls) {
		if (cpu_has_feature(CPU_FTR_PURR))
			seq_printf(m, "%lu %lu %lu %lu\n", h_num<<2,
				   hs[h_num].num_calls,
				   hs[h_num].tb_total,
				   hs[h_num].purr_total);
		else
			seq_printf(m, "%lu %lu %lu\n", h_num<<2,
				   hs[h_num].num_calls,
				   hs[h_num].tb_total);
	}

	return 0;
}

static const struct seq_operations hcall_inst_sops = {
        .start = hc_start,
        .next  = hc_next,
        .stop  = hc_stop,
        .show  = hc_show
};

DEFINE_SEQ_ATTRIBUTE(hcall_inst);

#define	HCALL_ROOT_DIR		"hcall_inst"
#define CPU_NAME_BUF_SIZE	32


static void probe_hcall_entry(void *ignored, unsigned long opcode, unsigned long *args)
{
	struct hcall_stats *h;

	if (opcode > MAX_HCALL_OPCODE)
		return;

	h = this_cpu_ptr(&hcall_stats[opcode / 4]);
	h->tb_start = mftb();
	h->purr_start = mfspr(SPRN_PURR);
}

static void probe_hcall_exit(void *ignored, unsigned long opcode, long retval,
			     unsigned long *retbuf)
{
	struct hcall_stats *h;

	if (opcode > MAX_HCALL_OPCODE)
		return;

	h = this_cpu_ptr(&hcall_stats[opcode / 4]);
	h->num_calls++;
	h->tb_total += mftb() - h->tb_start;
	h->purr_total += mfspr(SPRN_PURR) - h->purr_start;
}

static int __init hcall_inst_init(void)
{
	struct dentry *hcall_root;
	char cpu_name_buf[CPU_NAME_BUF_SIZE];
	int cpu;

	if (!firmware_has_feature(FW_FEATURE_LPAR))
		return 0;

	if (register_trace_hcall_entry(probe_hcall_entry, NULL))
		return -EINVAL;

	if (register_trace_hcall_exit(probe_hcall_exit, NULL)) {
		unregister_trace_hcall_entry(probe_hcall_entry, NULL);
		return -EINVAL;
	}

	hcall_root = debugfs_create_dir(HCALL_ROOT_DIR, NULL);

	for_each_possible_cpu(cpu) {
		snprintf(cpu_name_buf, CPU_NAME_BUF_SIZE, "cpu%d", cpu);
		debugfs_create_file(cpu_name_buf, 0444, hcall_root,
				    per_cpu(hcall_stats, cpu),
				    &hcall_inst_fops);
	}

	return 0;
}
machine_device_initcall(pseries, hcall_inst_init);
