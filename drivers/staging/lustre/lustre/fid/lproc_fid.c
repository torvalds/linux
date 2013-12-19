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
 * Copyright (c) 2011, 2012, Intel Corporation.
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

# include <linux/libcfs/libcfs.h>
# include <linux/module.h>

#include <obd.h>
#include <obd_class.h>
#include <dt_object.h>
#include <md_object.h>
#include <obd_support.h>
#include <lustre_req_layout.h>
#include <lustre_fid.h>
#include "fid_internal.h"

#ifdef LPROCFS
/*
 * Note: this function is only used for testing, it is no safe for production
 * use.
 */
static int
lprocfs_fid_write_common(const char *buffer, unsigned long count,
			 struct lu_seq_range *range)
{
	struct lu_seq_range tmp;
	int rc;

	LASSERT(range != NULL);

	rc = sscanf(buffer, "[%llx - %llx]\n",
		    (long long unsigned *)&tmp.lsr_start,
		    (long long unsigned *)&tmp.lsr_end);
	if (rc != 2 || !range_is_sane(&tmp) || range_is_zero(&tmp))
		return -EINVAL;
	*range = tmp;
	return 0;
}

/* Client side procfs stuff */
static ssize_t
lprocfs_fid_space_seq_write(struct file *file, const char *buffer,
			    size_t count, loff_t *off)
{
	struct lu_client_seq *seq = ((struct seq_file *)file->private_data)->private;
	int rc;

	LASSERT(seq != NULL);

	mutex_lock(&seq->lcs_mutex);
	rc = lprocfs_fid_write_common(buffer, count, &seq->lcs_space);

	if (rc == 0) {
		CDEBUG(D_INFO, "%s: Space: "DRANGE"\n",
		       seq->lcs_name, PRANGE(&seq->lcs_space));
	}

	mutex_unlock(&seq->lcs_mutex);

	return count;
}

static int
lprocfs_fid_space_seq_show(struct seq_file *m, void *unused)
{
	struct lu_client_seq *seq = (struct lu_client_seq *)m->private;
	int rc;

	LASSERT(seq != NULL);

	mutex_lock(&seq->lcs_mutex);
	rc = seq_printf(m, "["LPX64" - "LPX64"]:%x:%s\n", PRANGE(&seq->lcs_space));
	mutex_unlock(&seq->lcs_mutex);

	return rc;
}

static ssize_t
lprocfs_fid_width_seq_write(struct file *file, const char *buffer,
			    size_t count, loff_t *off)
{
	struct lu_client_seq *seq = ((struct seq_file *)file->private_data)->private;
	__u64  max;
	int rc, val;

	LASSERT(seq != NULL);

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

		if (rc == 0) {
			CDEBUG(D_INFO, "%s: Sequence size: "LPU64"\n",
			       seq->lcs_name, seq->lcs_width);
		}
	}

	mutex_unlock(&seq->lcs_mutex);

	return count;
}

static int
lprocfs_fid_width_seq_show(struct seq_file *m, void *unused)
{
	struct lu_client_seq *seq = (struct lu_client_seq *)m->private;
	int rc;

	LASSERT(seq != NULL);

	mutex_lock(&seq->lcs_mutex);
	rc = seq_printf(m, LPU64"\n", seq->lcs_width);
	mutex_unlock(&seq->lcs_mutex);

	return rc;
}

static int
lprocfs_fid_fid_seq_show(struct seq_file *m, void *unused)
{
	struct lu_client_seq *seq = (struct lu_client_seq *)m->private;
	int rc;

	LASSERT(seq != NULL);

	mutex_lock(&seq->lcs_mutex);
	rc = seq_printf(m, DFID"\n", PFID(&seq->lcs_fid));
	mutex_unlock(&seq->lcs_mutex);

	return rc;
}

static int
lprocfs_fid_server_seq_show(struct seq_file *m, void *unused)
{
	struct lu_client_seq *seq = (struct lu_client_seq *)m->private;
	struct client_obd *cli;
	int rc;

	LASSERT(seq != NULL);

	if (seq->lcs_exp != NULL) {
		cli = &seq->lcs_exp->exp_obd->u.cli;
		rc = seq_printf(m, "%s\n", cli->cl_target_uuid.uuid);
	} else {
		rc = seq_printf(m, "%s\n", seq->lcs_srv->lss_name);
	}
	return rc;
}

LPROC_SEQ_FOPS(lprocfs_fid_space);
LPROC_SEQ_FOPS(lprocfs_fid_width);
LPROC_SEQ_FOPS_RO(lprocfs_fid_server);
LPROC_SEQ_FOPS_RO(lprocfs_fid_fid);

struct lprocfs_vars seq_client_proc_list[] = {
	{ "space", &lprocfs_fid_space_fops },
	{ "width", &lprocfs_fid_width_fops },
	{ "server", &lprocfs_fid_server_fops },
	{ "fid", &lprocfs_fid_fid_fops },
	{ NULL }
};
#endif
