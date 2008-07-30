/*
 * SN Platform GRU Driver
 *
 *              PROC INTERFACES
 *
 * This file supports the /proc interfaces for the GRU driver
 *
 *  Copyright (c) 2008 Silicon Graphics, Inc.  All Rights Reserved.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */

#include <linux/proc_fs.h>
#include <linux/device.h>
#include <linux/seq_file.h>
#include <linux/uaccess.h>
#include "gru.h"
#include "grulib.h"
#include "grutables.h"

#define printstat(s, f)		printstat_val(s, &gru_stats.f, #f)

static void printstat_val(struct seq_file *s, atomic_long_t *v, char *id)
{
	unsigned long val = atomic_long_read(v);

	if (val)
		seq_printf(s, "%16lu %s\n", val, id);
}

static int statistics_show(struct seq_file *s, void *p)
{
	printstat(s, vdata_alloc);
	printstat(s, vdata_free);
	printstat(s, gts_alloc);
	printstat(s, gts_free);
	printstat(s, vdata_double_alloc);
	printstat(s, gts_double_allocate);
	printstat(s, assign_context);
	printstat(s, assign_context_failed);
	printstat(s, free_context);
	printstat(s, load_context);
	printstat(s, unload_context);
	printstat(s, steal_context);
	printstat(s, steal_context_failed);
	printstat(s, nopfn);
	printstat(s, break_cow);
	printstat(s, asid_new);
	printstat(s, asid_next);
	printstat(s, asid_wrap);
	printstat(s, asid_reuse);
	printstat(s, intr);
	printstat(s, call_os);
	printstat(s, call_os_check_for_bug);
	printstat(s, call_os_wait_queue);
	printstat(s, user_flush_tlb);
	printstat(s, user_unload_context);
	printstat(s, user_exception);
	printstat(s, set_task_slice);
	printstat(s, migrate_check);
	printstat(s, migrated_retarget);
	printstat(s, migrated_unload);
	printstat(s, migrated_unload_delay);
	printstat(s, migrated_nopfn_retarget);
	printstat(s, migrated_nopfn_unload);
	printstat(s, tlb_dropin);
	printstat(s, tlb_dropin_fail_no_asid);
	printstat(s, tlb_dropin_fail_upm);
	printstat(s, tlb_dropin_fail_invalid);
	printstat(s, tlb_dropin_fail_range_active);
	printstat(s, tlb_dropin_fail_idle);
	printstat(s, tlb_dropin_fail_fmm);
	printstat(s, mmu_invalidate_range);
	printstat(s, mmu_invalidate_page);
	printstat(s, mmu_clear_flush_young);
	printstat(s, flush_tlb);
	printstat(s, flush_tlb_gru);
	printstat(s, flush_tlb_gru_tgh);
	printstat(s, flush_tlb_gru_zero_asid);
	printstat(s, copy_gpa);
	printstat(s, mesq_receive);
	printstat(s, mesq_receive_none);
	printstat(s, mesq_send);
	printstat(s, mesq_send_failed);
	printstat(s, mesq_noop);
	printstat(s, mesq_send_unexpected_error);
	printstat(s, mesq_send_lb_overflow);
	printstat(s, mesq_send_qlimit_reached);
	printstat(s, mesq_send_amo_nacked);
	printstat(s, mesq_send_put_nacked);
	printstat(s, mesq_qf_not_full);
	printstat(s, mesq_qf_locked);
	printstat(s, mesq_qf_noop_not_full);
	printstat(s, mesq_qf_switch_head_failed);
	printstat(s, mesq_qf_unexpected_error);
	printstat(s, mesq_noop_unexpected_error);
	printstat(s, mesq_noop_lb_overflow);
	printstat(s, mesq_noop_qlimit_reached);
	printstat(s, mesq_noop_amo_nacked);
	printstat(s, mesq_noop_put_nacked);
	return 0;
}

static ssize_t statistics_write(struct file *file, const char __user *userbuf,
				size_t count, loff_t *data)
{
	memset(&gru_stats, 0, sizeof(gru_stats));
	return count;
}

static int options_show(struct seq_file *s, void *p)
{
	seq_printf(s, "0x%lx\n", gru_options);
	return 0;
}

static ssize_t options_write(struct file *file, const char __user *userbuf,
			     size_t count, loff_t *data)
{
	unsigned long val;
	char buf[80];

	if (copy_from_user
	    (buf, userbuf, count < sizeof(buf) ? count : sizeof(buf)))
		return -EFAULT;
	if (!strict_strtoul(buf, 10, &val))
		gru_options = val;

	return count;
}

static int cch_seq_show(struct seq_file *file, void *data)
{
	long gid = *(long *)data;
	int i;
	struct gru_state *gru = GID_TO_GRU(gid);
	struct gru_thread_state *ts;
	const char *mode[] = { "??", "UPM", "INTR", "OS_POLL" };

	if (gid == 0)
		seq_printf(file, "#%5s%5s%6s%9s%6s%8s%8s\n", "gid", "bid",
			   "ctx#", "pid", "cbrs", "dsbytes", "mode");
	if (gru)
		for (i = 0; i < GRU_NUM_CCH; i++) {
			ts = gru->gs_gts[i];
			if (!ts)
				continue;
			seq_printf(file, " %5d%5d%6d%9d%6d%8d%8s\n",
				   gru->gs_gid, gru->gs_blade_id, i,
				   ts->ts_tgid_owner,
				   ts->ts_cbr_au_count * GRU_CBR_AU_SIZE,
				   ts->ts_cbr_au_count * GRU_DSR_AU_BYTES,
				   mode[ts->ts_user_options &
					GRU_OPT_MISS_MASK]);
		}

	return 0;
}

static int gru_seq_show(struct seq_file *file, void *data)
{
	long gid = *(long *)data, ctxfree, cbrfree, dsrfree;
	struct gru_state *gru = GID_TO_GRU(gid);

	if (gid == 0) {
		seq_printf(file, "#%5s%5s%7s%6s%6s%8s%6s%6s\n", "gid", "nid",
			   "ctx", "cbr", "dsr", "ctx", "cbr", "dsr");
		seq_printf(file, "#%5s%5s%7s%6s%6s%8s%6s%6s\n", "", "", "busy",
			   "busy", "busy", "free", "free", "free");
	}
	if (gru) {
		ctxfree = GRU_NUM_CCH - gru->gs_active_contexts;
		cbrfree = hweight64(gru->gs_cbr_map) * GRU_CBR_AU_SIZE;
		dsrfree = hweight64(gru->gs_dsr_map) * GRU_DSR_AU_BYTES;
		seq_printf(file, " %5d%5d%7ld%6ld%6ld%8ld%6ld%6ld\n",
			   gru->gs_gid, gru->gs_blade_id, GRU_NUM_CCH - ctxfree,
			   GRU_NUM_CBE - cbrfree, GRU_NUM_DSR_BYTES - dsrfree,
			   ctxfree, cbrfree, dsrfree);
	}

	return 0;
}

static void seq_stop(struct seq_file *file, void *data)
{
}

static void *seq_start(struct seq_file *file, loff_t *gid)
{
	if (*gid < GRU_MAX_GRUS)
		return gid;
	return NULL;
}

static void *seq_next(struct seq_file *file, void *data, loff_t *gid)
{
	(*gid)++;
	if (*gid < GRU_MAX_GRUS)
		return gid;
	return NULL;
}

static const struct seq_operations cch_seq_ops = {
	.start	= seq_start,
	.next	= seq_next,
	.stop	= seq_stop,
	.show	= cch_seq_show
};

static const struct seq_operations gru_seq_ops = {
	.start	= seq_start,
	.next	= seq_next,
	.stop	= seq_stop,
	.show	= gru_seq_show
};

static int statistics_open(struct inode *inode, struct file *file)
{
	return single_open(file, statistics_show, NULL);
}

static int options_open(struct inode *inode, struct file *file)
{
	return single_open(file, options_show, NULL);
}

static int cch_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &cch_seq_ops);
}

static int gru_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &gru_seq_ops);
}

/* *INDENT-OFF* */
static const struct file_operations statistics_fops = {
	.open 		= statistics_open,
	.read 		= seq_read,
	.write 		= statistics_write,
	.llseek 	= seq_lseek,
	.release 	= single_release,
};

static const struct file_operations options_fops = {
	.open 		= options_open,
	.read 		= seq_read,
	.write 		= options_write,
	.llseek 	= seq_lseek,
	.release 	= single_release,
};

static const struct file_operations cch_fops = {
	.open 		= cch_open,
	.read 		= seq_read,
	.llseek 	= seq_lseek,
	.release 	= seq_release,
};
static const struct file_operations gru_fops = {
	.open 		= gru_open,
	.read 		= seq_read,
	.llseek 	= seq_lseek,
	.release 	= seq_release,
};

static struct proc_entry {
	char *name;
	int mode;
	const struct file_operations *fops;
	struct proc_dir_entry *entry;
} proc_files[] = {
	{"statistics", 0644, &statistics_fops},
	{"debug_options", 0644, &options_fops},
	{"cch_status", 0444, &cch_fops},
	{"gru_status", 0444, &gru_fops},
	{NULL}
};
/* *INDENT-ON* */

static struct proc_dir_entry *proc_gru __read_mostly;

static int create_proc_file(struct proc_entry *p)
{
	p->entry = create_proc_entry(p->name, p->mode, proc_gru);
	if (!p->entry)
		return -1;
	p->entry->proc_fops = p->fops;
	return 0;
}

static void delete_proc_files(void)
{
	struct proc_entry *p;

	if (proc_gru) {
		for (p = proc_files; p->name; p++)
			if (p->entry)
				remove_proc_entry(p->name, proc_gru);
		remove_proc_entry("gru", NULL);
	}
}

int gru_proc_init(void)
{
	struct proc_entry *p;

	proc_mkdir("sgi_uv", NULL);
	proc_gru = proc_mkdir("sgi_uv/gru", NULL);

	for (p = proc_files; p->name; p++)
		if (create_proc_file(p))
			goto err;
	return 0;

err:
	delete_proc_files();
	return -1;
}

void gru_proc_exit(void)
{
	delete_proc_files();
}
