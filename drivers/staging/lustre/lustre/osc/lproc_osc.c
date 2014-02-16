/*
 * GPL HEADER START
 *
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 only,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License version 2 for more details (a copy is included
 * in the LICENSE file that accompanied this code).
 *
 * You should have received a copy of the GNU General Public License
 * version 2 along with this program; If not, see
 * http://www.sun.com/software/products/lustre/docs/GPLv2.pdf
 *
 * Please contact Sun Microsystems, Inc., 4150 Network Circle, Santa Clara,
 * CA 95054 USA or visit www.sun.com if you need additional information or
 * have any questions.
 *
 * GPL HEADER END
 */
/*
 * Copyright (c) 2002, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright (c) 2011, 2012, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 */
#define DEBUG_SUBSYSTEM S_CLASS

#include <asm/statfs.h>
#include <obd_cksum.h>
#include <obd_class.h>
#include <lprocfs_status.h>
#include <linux/seq_file.h>
#include "osc_internal.h"

static int osc_active_seq_show(struct seq_file *m, void *v)
{
	struct obd_device *dev = m->private;
	int rc;

	LPROCFS_CLIMP_CHECK(dev);
	rc = seq_printf(m, "%d\n", !dev->u.cli.cl_import->imp_deactive);
	LPROCFS_CLIMP_EXIT(dev);
	return rc;
}

static ssize_t osc_active_seq_write(struct file *file, const char *buffer,
				    size_t count, loff_t *off)
{
	struct obd_device *dev = ((struct seq_file *)file->private_data)->private;
	int val, rc;

	rc = lprocfs_write_helper(buffer, count, &val);
	if (rc)
		return rc;
	if (val < 0 || val > 1)
		return -ERANGE;

	/* opposite senses */
	if (dev->u.cli.cl_import->imp_deactive == val)
		rc = ptlrpc_set_import_active(dev->u.cli.cl_import, val);
	else
		CDEBUG(D_CONFIG, "activate %d: ignoring repeat request\n", val);

	return count;
}
LPROC_SEQ_FOPS(osc_active);

static int osc_max_rpcs_in_flight_seq_show(struct seq_file *m, void *v)
{
	struct obd_device *dev = m->private;
	struct client_obd *cli = &dev->u.cli;
	int rc;

	client_obd_list_lock(&cli->cl_loi_list_lock);
	rc = seq_printf(m, "%u\n", cli->cl_max_rpcs_in_flight);
	client_obd_list_unlock(&cli->cl_loi_list_lock);
	return rc;
}

static ssize_t osc_max_rpcs_in_flight_seq_write(struct file *file,
			const char *buffer, size_t count, loff_t *off)
{
	struct obd_device *dev = ((struct seq_file *)file->private_data)->private;
	struct client_obd *cli = &dev->u.cli;
	struct ptlrpc_request_pool *pool = cli->cl_import->imp_rq_pool;
	int val, rc;

	rc = lprocfs_write_helper(buffer, count, &val);
	if (rc)
		return rc;

	if (val < 1 || val > OSC_MAX_RIF_MAX)
		return -ERANGE;

	LPROCFS_CLIMP_CHECK(dev);
	if (pool && val > cli->cl_max_rpcs_in_flight)
		pool->prp_populate(pool, val-cli->cl_max_rpcs_in_flight);

	client_obd_list_lock(&cli->cl_loi_list_lock);
	cli->cl_max_rpcs_in_flight = val;
	client_obd_list_unlock(&cli->cl_loi_list_lock);

	LPROCFS_CLIMP_EXIT(dev);
	return count;
}
LPROC_SEQ_FOPS(osc_max_rpcs_in_flight);

static int osc_max_dirty_mb_seq_show(struct seq_file *m, void *v)
{
	struct obd_device *dev = m->private;
	struct client_obd *cli = &dev->u.cli;
	long val;
	int mult;

	client_obd_list_lock(&cli->cl_loi_list_lock);
	val = cli->cl_dirty_max;
	client_obd_list_unlock(&cli->cl_loi_list_lock);

	mult = 1 << 20;
	return lprocfs_seq_read_frac_helper(m, val, mult);
}

static ssize_t osc_max_dirty_mb_seq_write(struct file *file, const char *buffer,
				      size_t count, loff_t *off)
{
	struct obd_device *dev = ((struct seq_file *)file->private_data)->private;
	struct client_obd *cli = &dev->u.cli;
	int pages_number, mult, rc;

	mult = 1 << (20 - PAGE_CACHE_SHIFT);
	rc = lprocfs_write_frac_helper(buffer, count, &pages_number, mult);
	if (rc)
		return rc;

	if (pages_number <= 0 ||
	    pages_number > OSC_MAX_DIRTY_MB_MAX << (20 - PAGE_CACHE_SHIFT) ||
	    pages_number > totalram_pages / 4) /* 1/4 of RAM */
		return -ERANGE;

	client_obd_list_lock(&cli->cl_loi_list_lock);
	cli->cl_dirty_max = (obd_count)(pages_number << PAGE_CACHE_SHIFT);
	osc_wake_cache_waiters(cli);
	client_obd_list_unlock(&cli->cl_loi_list_lock);

	return count;
}
LPROC_SEQ_FOPS(osc_max_dirty_mb);

static int osc_cached_mb_seq_show(struct seq_file *m, void *v)
{
	struct obd_device *dev = m->private;
	struct client_obd *cli = &dev->u.cli;
	int shift = 20 - PAGE_CACHE_SHIFT;
	int rc;

	rc = seq_printf(m,
		      "used_mb: %d\n"
		      "busy_cnt: %d\n",
		      (atomic_read(&cli->cl_lru_in_list) +
			atomic_read(&cli->cl_lru_busy)) >> shift,
		      atomic_read(&cli->cl_lru_busy));

	return rc;
}

/* shrink the number of caching pages to a specific number */
static ssize_t osc_cached_mb_seq_write(struct file *file, const char *buffer,
				   size_t count, loff_t *off)
{
	struct obd_device *dev = ((struct seq_file *)file->private_data)->private;
	struct client_obd *cli = &dev->u.cli;
	int pages_number, mult, rc;

	mult = 1 << (20 - PAGE_CACHE_SHIFT);
	buffer = lprocfs_find_named_value(buffer, "used_mb:", &count);
	rc = lprocfs_write_frac_helper(buffer, count, &pages_number, mult);
	if (rc)
		return rc;

	if (pages_number < 0)
		return -ERANGE;

	rc = atomic_read(&cli->cl_lru_in_list) - pages_number;
	if (rc > 0)
		(void)osc_lru_shrink(cli, rc);

	return count;
}
LPROC_SEQ_FOPS(osc_cached_mb);

static int osc_cur_dirty_bytes_seq_show(struct seq_file *m, void *v)
{
	struct obd_device *dev = m->private;
	struct client_obd *cli = &dev->u.cli;
	int rc;

	client_obd_list_lock(&cli->cl_loi_list_lock);
	rc = seq_printf(m, "%lu\n", cli->cl_dirty);
	client_obd_list_unlock(&cli->cl_loi_list_lock);
	return rc;
}
LPROC_SEQ_FOPS_RO(osc_cur_dirty_bytes);

static int osc_cur_grant_bytes_seq_show(struct seq_file *m, void *v)
{
	struct obd_device *dev = m->private;
	struct client_obd *cli = &dev->u.cli;
	int rc;

	client_obd_list_lock(&cli->cl_loi_list_lock);
	rc = seq_printf(m, "%lu\n", cli->cl_avail_grant);
	client_obd_list_unlock(&cli->cl_loi_list_lock);
	return rc;
}

static ssize_t osc_cur_grant_bytes_seq_write(struct file *file, const char *buffer,
				  size_t count, loff_t *off)
{
	struct obd_device *obd = ((struct seq_file *)file->private_data)->private;
	struct client_obd *cli = &obd->u.cli;
	int		rc;
	__u64	      val;

	if (obd == NULL)
		return 0;

	rc = lprocfs_write_u64_helper(buffer, count, &val);
	if (rc)
		return rc;

	/* this is only for shrinking grant */
	client_obd_list_lock(&cli->cl_loi_list_lock);
	if (val >= cli->cl_avail_grant) {
		client_obd_list_unlock(&cli->cl_loi_list_lock);
		return 0;
	}
	client_obd_list_unlock(&cli->cl_loi_list_lock);

	LPROCFS_CLIMP_CHECK(obd);
	if (cli->cl_import->imp_state == LUSTRE_IMP_FULL)
		rc = osc_shrink_grant_to_target(cli, val);
	LPROCFS_CLIMP_EXIT(obd);
	if (rc)
		return rc;
	return count;
}
LPROC_SEQ_FOPS(osc_cur_grant_bytes);

static int osc_cur_lost_grant_bytes_seq_show(struct seq_file *m, void *v)
{
	struct obd_device *dev = m->private;
	struct client_obd *cli = &dev->u.cli;
	int rc;

	client_obd_list_lock(&cli->cl_loi_list_lock);
	rc = seq_printf(m, "%lu\n", cli->cl_lost_grant);
	client_obd_list_unlock(&cli->cl_loi_list_lock);
	return rc;
}
LPROC_SEQ_FOPS_RO(osc_cur_lost_grant_bytes);

static int osc_grant_shrink_interval_seq_show(struct seq_file *m, void *v)
{
	struct obd_device *obd = m->private;

	if (obd == NULL)
		return 0;
	return seq_printf(m, "%d\n",
			obd->u.cli.cl_grant_shrink_interval);
}

static ssize_t osc_grant_shrink_interval_seq_write(struct file *file,
				const char *buffer, size_t count, loff_t *off)
{
	struct obd_device *obd = ((struct seq_file *)file->private_data)->private;
	int val, rc;

	if (obd == NULL)
		return 0;

	rc = lprocfs_write_helper(buffer, count, &val);
	if (rc)
		return rc;

	if (val <= 0)
		return -ERANGE;

	obd->u.cli.cl_grant_shrink_interval = val;

	return count;
}
LPROC_SEQ_FOPS(osc_grant_shrink_interval);

static int osc_checksum_seq_show(struct seq_file *m, void *v)
{
	struct obd_device *obd = m->private;

	if (obd == NULL)
		return 0;

	return seq_printf(m, "%d\n",
			obd->u.cli.cl_checksum ? 1 : 0);
}

static ssize_t osc_checksum_seq_write(struct file *file, const char *buffer,
			   size_t count, loff_t *off)
{
	struct obd_device *obd = ((struct seq_file *)file->private_data)->private;
	int val, rc;

	if (obd == NULL)
		return 0;

	rc = lprocfs_write_helper(buffer, count, &val);
	if (rc)
		return rc;

	obd->u.cli.cl_checksum = (val ? 1 : 0);

	return count;
}
LPROC_SEQ_FOPS(osc_checksum);

static int osc_checksum_type_seq_show(struct seq_file *m, void *v)
{
	struct obd_device *obd = m->private;
	int i;
	DECLARE_CKSUM_NAME;

	if (obd == NULL)
		return 0;

	for (i = 0; i < ARRAY_SIZE(cksum_name); i++) {
		if (((1 << i) & obd->u.cli.cl_supp_cksum_types) == 0)
			continue;
		if (obd->u.cli.cl_cksum_type == (1 << i))
			seq_printf(m, "[%s] ", cksum_name[i]);
		else
			seq_printf(m, "%s ", cksum_name[i]);
	}
	seq_printf(m, "\n");
	return 0;
}

static ssize_t osc_checksum_type_seq_write(struct file *file, const char *buffer,
				size_t count, loff_t *off)
{
	struct obd_device *obd = ((struct seq_file *)file->private_data)->private;
	int i;
	DECLARE_CKSUM_NAME;
	char kernbuf[10];

	if (obd == NULL)
		return 0;

	if (count > sizeof(kernbuf) - 1)
		return -EINVAL;
	if (copy_from_user(kernbuf, buffer, count))
		return -EFAULT;
	if (count > 0 && kernbuf[count - 1] == '\n')
		kernbuf[count - 1] = '\0';
	else
		kernbuf[count] = '\0';

	for (i = 0; i < ARRAY_SIZE(cksum_name); i++) {
		if (((1 << i) & obd->u.cli.cl_supp_cksum_types) == 0)
			continue;
		if (!strcmp(kernbuf, cksum_name[i])) {
		       obd->u.cli.cl_cksum_type = 1 << i;
		       return count;
		}
	}
	return -EINVAL;
}
LPROC_SEQ_FOPS(osc_checksum_type);

static int osc_resend_count_seq_show(struct seq_file *m, void *v)
{
	struct obd_device *obd = m->private;

	return seq_printf(m, "%u\n", atomic_read(&obd->u.cli.cl_resends));
}

static ssize_t osc_resend_count_seq_write(struct file *file, const char *buffer,
			       size_t count, loff_t *off)
{
	struct obd_device *obd = ((struct seq_file *)file->private_data)->private;
	int val, rc;

	rc = lprocfs_write_helper(buffer, count, &val);
	if (rc)
		return rc;

	if (val < 0)
	       return -EINVAL;

	atomic_set(&obd->u.cli.cl_resends, val);

	return count;
}
LPROC_SEQ_FOPS(osc_resend_count);

static int osc_contention_seconds_seq_show(struct seq_file *m, void *v)
{
	struct obd_device *obd = m->private;
	struct osc_device *od  = obd2osc_dev(obd);

	return seq_printf(m, "%u\n", od->od_contention_time);
}

static ssize_t osc_contention_seconds_seq_write(struct file *file, const char *buffer,
				     size_t count, loff_t *off)
{
	struct obd_device *obd = ((struct seq_file *)file->private_data)->private;
	struct osc_device *od  = obd2osc_dev(obd);

	return lprocfs_write_helper(buffer, count, &od->od_contention_time) ?:
		count;
}
LPROC_SEQ_FOPS(osc_contention_seconds);

static int osc_lockless_truncate_seq_show(struct seq_file *m, void *v)
{
	struct obd_device *obd = m->private;
	struct osc_device *od  = obd2osc_dev(obd);

	return seq_printf(m, "%u\n", od->od_lockless_truncate);
}

static ssize_t osc_lockless_truncate_seq_write(struct file *file, const char *buffer,
				    size_t count, loff_t *off)
{
	struct obd_device *obd = ((struct seq_file *)file->private_data)->private;
	struct osc_device *od  = obd2osc_dev(obd);

	return lprocfs_write_helper(buffer, count, &od->od_lockless_truncate) ?:
		count;
}
LPROC_SEQ_FOPS(osc_lockless_truncate);

static int osc_destroys_in_flight_seq_show(struct seq_file *m, void *v)
{
	struct obd_device *obd = m->private;
	return seq_printf(m, "%u\n",
			atomic_read(&obd->u.cli.cl_destroy_in_flight));
}
LPROC_SEQ_FOPS_RO(osc_destroys_in_flight);

static int osc_obd_max_pages_per_rpc_seq_show(struct seq_file *m, void *v)
{
	return lprocfs_obd_rd_max_pages_per_rpc(m, m->private);
}

static ssize_t osc_obd_max_pages_per_rpc_seq_write(struct file *file,
				const char *buffer, size_t count, loff_t *off)
{
	struct obd_device *dev = ((struct seq_file *)file->private_data)->private;
	struct client_obd *cli = &dev->u.cli;
	struct obd_connect_data *ocd = &cli->cl_import->imp_connect_data;
	int chunk_mask, rc;
	__u64 val;

	rc = lprocfs_write_u64_helper(buffer, count, &val);
	if (rc)
		return rc;

	/* if the max_pages is specified in bytes, convert to pages */
	if (val >= ONE_MB_BRW_SIZE)
		val >>= PAGE_CACHE_SHIFT;

	LPROCFS_CLIMP_CHECK(dev);

	chunk_mask = ~((1 << (cli->cl_chunkbits - PAGE_CACHE_SHIFT)) - 1);
	/* max_pages_per_rpc must be chunk aligned */
	val = (val + ~chunk_mask) & chunk_mask;
	if (val == 0 || val > ocd->ocd_brw_size >> PAGE_CACHE_SHIFT) {
		LPROCFS_CLIMP_EXIT(dev);
		return -ERANGE;
	}
	client_obd_list_lock(&cli->cl_loi_list_lock);
	cli->cl_max_pages_per_rpc = val;
	client_obd_list_unlock(&cli->cl_loi_list_lock);

	LPROCFS_CLIMP_EXIT(dev);
	return count;
}
LPROC_SEQ_FOPS(osc_obd_max_pages_per_rpc);

LPROC_SEQ_FOPS_RO_TYPE(osc, uuid);
LPROC_SEQ_FOPS_RO_TYPE(osc, connect_flags);
LPROC_SEQ_FOPS_RO_TYPE(osc, blksize);
LPROC_SEQ_FOPS_RO_TYPE(osc, kbytestotal);
LPROC_SEQ_FOPS_RO_TYPE(osc, kbytesfree);
LPROC_SEQ_FOPS_RO_TYPE(osc, kbytesavail);
LPROC_SEQ_FOPS_RO_TYPE(osc, filestotal);
LPROC_SEQ_FOPS_RO_TYPE(osc, filesfree);
LPROC_SEQ_FOPS_RO_TYPE(osc, server_uuid);
LPROC_SEQ_FOPS_RO_TYPE(osc, conn_uuid);
LPROC_SEQ_FOPS_RO_TYPE(osc, timeouts);
LPROC_SEQ_FOPS_RO_TYPE(osc, state);

LPROC_SEQ_FOPS_WR_ONLY(osc, ping);

LPROC_SEQ_FOPS_RW_TYPE(osc, import);
LPROC_SEQ_FOPS_RW_TYPE(osc, pinger_recov);

static struct lprocfs_vars lprocfs_osc_obd_vars[] = {
	{ "uuid",	     &osc_uuid_fops,	0, 0 },
	{ "ping",	     &osc_ping_fops,    0, 0222 },
	{ "connect_flags",   &osc_connect_flags_fops, 0, 0 },
	{ "blocksize",       &osc_blksize_fops,     0, 0 },
	{ "kbytestotal",     &osc_kbytestotal_fops, 0, 0 },
	{ "kbytesfree",      &osc_kbytesfree_fops,  0, 0 },
	{ "kbytesavail",     &osc_kbytesavail_fops, 0, 0 },
	{ "filestotal",      &osc_filestotal_fops,  0, 0 },
	{ "filesfree",       &osc_filesfree_fops,   0, 0 },
	//{ "filegroups",      lprocfs_rd_filegroups,  0, 0 },
	{ "ost_server_uuid", &osc_server_uuid_fops, 0, 0 },
	{ "ost_conn_uuid",   &osc_conn_uuid_fops, 0, 0 },
	{ "active",	     &osc_active_fops, 0 },
	{ "max_pages_per_rpc", &osc_obd_max_pages_per_rpc_fops, 0 },
	{ "max_rpcs_in_flight", &osc_max_rpcs_in_flight_fops, 0 },
	{ "destroys_in_flight", &osc_destroys_in_flight_fops, 0, 0 },
	{ "max_dirty_mb",    &osc_max_dirty_mb_fops, 0 },
	{ "osc_cached_mb",   &osc_cached_mb_fops, 0 },
	{ "cur_dirty_bytes", &osc_cur_dirty_bytes_fops, 0, 0 },
	{ "cur_grant_bytes", &osc_cur_grant_bytes_fops, 0 },
	{ "cur_lost_grant_bytes", &osc_cur_lost_grant_bytes_fops, 0, 0},
	{ "grant_shrink_interval", &osc_grant_shrink_interval_fops, 0 },
	{ "checksums",       &osc_checksum_fops, 0 },
	{ "checksum_type",   &osc_checksum_type_fops, 0 },
	{ "resend_count",    &osc_resend_count_fops, 0},
	{ "timeouts",	     &osc_timeouts_fops, 0, 0 },
	{ "contention_seconds", &osc_contention_seconds_fops, 0 },
	{ "lockless_truncate",  &osc_lockless_truncate_fops, 0 },
	{ "import",		&osc_import_fops, 0 },
	{ "state",		&osc_state_fops, 0, 0 },
	{ "pinger_recov",	&osc_pinger_recov_fops, 0 },
	{ 0 }
};

LPROC_SEQ_FOPS_RO_TYPE(osc, numrefs);
static struct lprocfs_vars lprocfs_osc_module_vars[] = {
	{ "num_refs",	&osc_numrefs_fops,     0, 0 },
	{ 0 }
};

#define pct(a,b) (b ? a * 100 / b : 0)

static int osc_rpc_stats_seq_show(struct seq_file *seq, void *v)
{
	struct timeval now;
	struct obd_device *dev = seq->private;
	struct client_obd *cli = &dev->u.cli;
	unsigned long read_tot = 0, write_tot = 0, read_cum, write_cum;
	int i;

	do_gettimeofday(&now);

	client_obd_list_lock(&cli->cl_loi_list_lock);

	seq_printf(seq, "snapshot_time:	 %lu.%lu (secs.usecs)\n",
		   now.tv_sec, (unsigned long)now.tv_usec);
	seq_printf(seq, "read RPCs in flight:  %d\n",
		   cli->cl_r_in_flight);
	seq_printf(seq, "write RPCs in flight: %d\n",
		   cli->cl_w_in_flight);
	seq_printf(seq, "pending write pages:  %d\n",
		   atomic_read(&cli->cl_pending_w_pages));
	seq_printf(seq, "pending read pages:   %d\n",
		   atomic_read(&cli->cl_pending_r_pages));

	seq_printf(seq, "\n\t\t\tread\t\t\twrite\n");
	seq_printf(seq, "pages per rpc	 rpcs   %% cum %% |");
	seq_printf(seq, "       rpcs   %% cum %%\n");

	read_tot = lprocfs_oh_sum(&cli->cl_read_page_hist);
	write_tot = lprocfs_oh_sum(&cli->cl_write_page_hist);

	read_cum = 0;
	write_cum = 0;
	for (i = 0; i < OBD_HIST_MAX; i++) {
		unsigned long r = cli->cl_read_page_hist.oh_buckets[i];
		unsigned long w = cli->cl_write_page_hist.oh_buckets[i];
		read_cum += r;
		write_cum += w;
		seq_printf(seq, "%d:\t\t%10lu %3lu %3lu   | %10lu %3lu %3lu\n",
				 1 << i, r, pct(r, read_tot),
				 pct(read_cum, read_tot), w,
				 pct(w, write_tot),
				 pct(write_cum, write_tot));
		if (read_cum == read_tot && write_cum == write_tot)
			break;
	}

	seq_printf(seq, "\n\t\t\tread\t\t\twrite\n");
	seq_printf(seq, "rpcs in flight	rpcs   %% cum %% |");
	seq_printf(seq, "       rpcs   %% cum %%\n");

	read_tot = lprocfs_oh_sum(&cli->cl_read_rpc_hist);
	write_tot = lprocfs_oh_sum(&cli->cl_write_rpc_hist);

	read_cum = 0;
	write_cum = 0;
	for (i = 0; i < OBD_HIST_MAX; i++) {
		unsigned long r = cli->cl_read_rpc_hist.oh_buckets[i];
		unsigned long w = cli->cl_write_rpc_hist.oh_buckets[i];
		read_cum += r;
		write_cum += w;
		seq_printf(seq, "%d:\t\t%10lu %3lu %3lu   | %10lu %3lu %3lu\n",
				 i, r, pct(r, read_tot),
				 pct(read_cum, read_tot), w,
				 pct(w, write_tot),
				 pct(write_cum, write_tot));
		if (read_cum == read_tot && write_cum == write_tot)
			break;
	}

	seq_printf(seq, "\n\t\t\tread\t\t\twrite\n");
	seq_printf(seq, "offset		rpcs   %% cum %% |");
	seq_printf(seq, "       rpcs   %% cum %%\n");

	read_tot = lprocfs_oh_sum(&cli->cl_read_offset_hist);
	write_tot = lprocfs_oh_sum(&cli->cl_write_offset_hist);

	read_cum = 0;
	write_cum = 0;
	for (i = 0; i < OBD_HIST_MAX; i++) {
		unsigned long r = cli->cl_read_offset_hist.oh_buckets[i];
		unsigned long w = cli->cl_write_offset_hist.oh_buckets[i];
		read_cum += r;
		write_cum += w;
		seq_printf(seq, "%d:\t\t%10lu %3lu %3lu   | %10lu %3lu %3lu\n",
			   (i == 0) ? 0 : 1 << (i - 1),
			   r, pct(r, read_tot), pct(read_cum, read_tot),
			   w, pct(w, write_tot), pct(write_cum, write_tot));
		if (read_cum == read_tot && write_cum == write_tot)
			break;
	}

	client_obd_list_unlock(&cli->cl_loi_list_lock);

	return 0;
}
#undef pct

static ssize_t osc_rpc_stats_seq_write(struct file *file, const char *buf,
				       size_t len, loff_t *off)
{
	struct seq_file *seq = file->private_data;
	struct obd_device *dev = seq->private;
	struct client_obd *cli = &dev->u.cli;

	lprocfs_oh_clear(&cli->cl_read_rpc_hist);
	lprocfs_oh_clear(&cli->cl_write_rpc_hist);
	lprocfs_oh_clear(&cli->cl_read_page_hist);
	lprocfs_oh_clear(&cli->cl_write_page_hist);
	lprocfs_oh_clear(&cli->cl_read_offset_hist);
	lprocfs_oh_clear(&cli->cl_write_offset_hist);

	return len;
}

LPROC_SEQ_FOPS(osc_rpc_stats);

static int osc_stats_seq_show(struct seq_file *seq, void *v)
{
	struct timeval now;
	struct obd_device *dev = seq->private;
	struct osc_stats *stats = &obd2osc_dev(dev)->od_stats;

	do_gettimeofday(&now);

	seq_printf(seq, "snapshot_time:	 %lu.%lu (secs.usecs)\n",
		   now.tv_sec, (unsigned long)now.tv_usec);
	seq_printf(seq, "lockless_write_bytes\t\t"LPU64"\n",
		   stats->os_lockless_writes);
	seq_printf(seq, "lockless_read_bytes\t\t"LPU64"\n",
		   stats->os_lockless_reads);
	seq_printf(seq, "lockless_truncate\t\t"LPU64"\n",
		   stats->os_lockless_truncates);
	return 0;
}

static ssize_t osc_stats_seq_write(struct file *file, const char *buf,
				   size_t len, loff_t *off)
{
	struct seq_file *seq = file->private_data;
	struct obd_device *dev = seq->private;
	struct osc_stats *stats = &obd2osc_dev(dev)->od_stats;

	memset(stats, 0, sizeof(*stats));
	return len;
}

LPROC_SEQ_FOPS(osc_stats);

int lproc_osc_attach_seqstat(struct obd_device *dev)
{
	int rc;

	rc = lprocfs_seq_create(dev->obd_proc_entry, "osc_stats", 0644,
				&osc_stats_fops, dev);
	if (rc == 0)
		rc = lprocfs_obd_seq_create(dev, "rpc_stats", 0644,
					    &osc_rpc_stats_fops, dev);

	return rc;
}

void lprocfs_osc_init_vars(struct lprocfs_static_vars *lvars)
{
	lvars->module_vars = lprocfs_osc_module_vars;
	lvars->obd_vars    = lprocfs_osc_obd_vars;
}
