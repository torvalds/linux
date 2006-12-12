/*
 * Copyright (C) 2006 Mike Kravetz IBM Corporation
 *
 * Hypervisor Call Instrumentation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */

#include <linux/kernel.h>
#include <linux/percpu.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/cpumask.h>
#include <asm/hvcall.h>
#include <asm/firmware.h>
#include <asm/cputable.h>

DEFINE_PER_CPU(struct hcall_stats[HCALL_STAT_ARRAY_SIZE], hcall_stats);

/*
 * Routines for displaying the statistics in debugfs
 */
static void *hc_start(struct seq_file *m, loff_t *pos)
{
	if ((int)*pos < HCALL_STAT_ARRAY_SIZE)
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
	struct hcall_stats *hs = (struct hcall_stats *)m->private;

	if (hs[h_num].num_calls) {
		if (!cpu_has_feature(CPU_FTR_PURR))
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

static struct seq_operations hcall_inst_seq_ops = {
        .start = hc_start,
        .next  = hc_next,
        .stop  = hc_stop,
        .show  = hc_show
};

static int hcall_inst_seq_open(struct inode *inode, struct file *file)
{
	int rc;
	struct seq_file *seq;

	rc = seq_open(file, &hcall_inst_seq_ops);
	seq = file->private_data;
	seq->private = file->f_path.dentry->d_inode->i_private;

	return rc;
}

static struct file_operations hcall_inst_seq_fops = {
	.open = hcall_inst_seq_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = seq_release,
};

#define	HCALL_ROOT_DIR		"hcall_inst"
#define CPU_NAME_BUF_SIZE	32

static int __init hcall_inst_init(void)
{
	struct dentry *hcall_root;
	struct dentry *hcall_file;
	char cpu_name_buf[CPU_NAME_BUF_SIZE];
	int cpu;

	if (!firmware_has_feature(FW_FEATURE_LPAR))
		return 0;

	hcall_root = debugfs_create_dir(HCALL_ROOT_DIR, NULL);
	if (!hcall_root)
		return -ENOMEM;

	for_each_possible_cpu(cpu) {
		snprintf(cpu_name_buf, CPU_NAME_BUF_SIZE, "cpu%d", cpu);
		hcall_file = debugfs_create_file(cpu_name_buf, S_IRUGO,
						 hcall_root,
						 per_cpu(hcall_stats, cpu),
						 &hcall_inst_seq_fops);
		if (!hcall_file)
			return -ENOMEM;
	}

	return 0;
}
__initcall(hcall_inst_init);
