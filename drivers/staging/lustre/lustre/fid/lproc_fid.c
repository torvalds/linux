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
 * Copyright (c) 2007, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright (c) 2011, 2015, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 *
 * lustre/fid/lproc_fid.c
 *
 * Lustre Sequence Manager
 *
 * Author: Yury Umanets <umka@clusterfs.com>
 */

#define DEBUG_SUBSYSTEM S_FID

#include "../../include/linux/libcfs/libcfs.h"
#include <linux/module.h>

#include "../include/obd.h"
#include "../include/obd_class.h"
#include "../include/obd_support.h"
#include "../include/lustre_req_layout.h"
#include "../include/lustre_fid.h"
#include "fid_internal.h"

/* Format: [0x64BIT_INT - 0x64BIT_INT] + 32 bytes just in case */
#define MAX_FID_RANGE_STRLEN (32 + 2 * 2 * sizeof(__u64))
/*
 * Note: this function is only used for testing, it is no safe for production
 * use.
 */
static int
ldebugfs_fid_write_common(const char __user *buffer, size_t count,
			  struct lu_seq_range *range)
{
	struct lu_seq_range tmp;
	int rc;
	char kernbuf[MAX_FID_RANGE_STRLEN];

	LASSERT(range);

	if (count >= sizeof(kernbuf))
		return -EINVAL;

	if (copy_from_user(kernbuf, buffer, count))
		return -EFAULT;

	kernbuf[count] = 0;

	if (count == 5 && strcmp(kernbuf, "clear") == 0) {
		memset(range, 0, sizeof(*range));
		return count;
	}

	/* of the form "[0x0000000240000400 - 0x000000028000400]" */
	rc = sscanf(kernbuf, "[%llx - %llx]\n",
		    (unsigned long long *)&tmp.lsr_start,
		    (unsigned long long *)&tmp.lsr_end);
	if (rc != 2)
		return -EINVAL;
	if (!range_is_sane(&tmp) || range_is_zero(&tmp) ||
	    tmp.lsr_start < range->lsr_start || tmp.lsr_end > range->lsr_end)
		return -EINVAL;
	*range = tmp;
	return count;
}

/* Client side debugfs stuff */
static ssize_t
ldebugfs_fid_space_seq_write(struct file *file,
			     const char __user *buffer,
			     size_t count, loff_t *off)
{
	struct lu_client_seq *seq;
	int rc;

	seq = ((struct seq_file *)file->private_data)->private;

	mutex_lock(&seq->lcs_mutex);
	rc = ldebugfs_fid_write_common(buffer, count, &seq->lcs_space);

	if (rc == 0) {
		CDEBUG(D_INFO, "%s: Space: "DRANGE"\n",
		       seq->lcs_name, PRANGE(&seq->lcs_space));
	}

	mutex_unlock(&seq->lcs_mutex);

	return count;
}

static int
ldebugfs_fid_space_seq_show(struct seq_file *m, void *unused)
{
	struct lu_client_seq *seq = (struct lu_client_seq *)m->private;

	mutex_lock(&seq->lcs_mutex);
	seq_printf(m, "[%#llx - %#llx]:%x:%s\n", PRANGE(&seq->lcs_space));
	mutex_unlock(&seq->lcs_mutex);

	return 0;
}

static ssize_t
ldebugfs_fid_width_seq_write(struct file *file,
			     const char __user *buffer,
			     size_t count, loff_t *off)
{
	struct lu_client_seq *seq;
	__u64  max;
	int rc, val;

	seq = ((struct seq_file *)file->private_data)->private;

	rc = lprocfs_write_helper(buffer, count, &val);
	if (rc)
		return rc;

	mutex_lock(&seq->lcs_mutex);
	if (seq->lcs_type == LUSTRE_SEQ_DATA)
		max = LUSTRE_DATA_SEQ_MAX_WIDTH;
	else
		max = LUSTRE_METADATA_SEQ_MAX_WIDTH;

	if (val <= max && val > 0) {
		seq->lcs_width = val;

		CDEBUG(D_INFO, "%s: Sequence size: %llu\n", seq->lcs_name,
		       seq->lcs_width);
	}

	mutex_unlock(&seq->lcs_mutex);

	return count;
}

static int
ldebugfs_fid_width_seq_show(struct seq_file *m, void *unused)
{
	struct lu_client_seq *seq = (struct lu_client_seq *)m->private;

	mutex_lock(&seq->lcs_mutex);
	seq_printf(m, "%llu\n", seq->lcs_width);
	mutex_unlock(&seq->lcs_mutex);

	return 0;
}

static int
ldebugfs_fid_fid_seq_show(struct seq_file *m, void *unused)
{
	struct lu_client_seq *seq = (struct lu_client_seq *)m->private;

	mutex_lock(&seq->lcs_mutex);
	seq_printf(m, DFID "\n", PFID(&seq->lcs_fid));
	mutex_unlock(&seq->lcs_mutex);

	return 0;
}

static int
ldebugfs_fid_server_seq_show(struct seq_file *m, void *unused)
{
	struct lu_client_seq *seq = (struct lu_client_seq *)m->private;
	struct client_obd *cli;

	if (seq->lcs_exp) {
		cli = &seq->lcs_exp->exp_obd->u.cli;
		seq_printf(m, "%s\n", cli->cl_target_uuid.uuid);
	}

	return 0;
}

LPROC_SEQ_FOPS(ldebugfs_fid_space);
LPROC_SEQ_FOPS(ldebugfs_fid_width);
LPROC_SEQ_FOPS_RO(ldebugfs_fid_server);
LPROC_SEQ_FOPS_RO(ldebugfs_fid_fid);

struct lprocfs_vars seq_client_debugfs_list[] = {
	{ "space", &ldebugfs_fid_space_fops },
	{ "width", &ldebugfs_fid_width_fops },
	{ "server", &ldebugfs_fid_server_fops },
	{ "fid", &ldebugfs_fid_fid_fops },
	{ NULL }
};
