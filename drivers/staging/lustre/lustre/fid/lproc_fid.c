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
seq_proc_write_common(struct file *file, const char *buffer,
		      unsigned long count, void *data,
		      struct lu_seq_range *range)
{
	struct lu_seq_range tmp;
	int rc;
	ENTRY;

	LASSERT(range != NULL);

	rc = sscanf(buffer, "[%llx - %llx]\n",
		    (long long unsigned *)&tmp.lsr_start,
		    (long long unsigned *)&tmp.lsr_end);
	if (rc != 2 || !range_is_sane(&tmp) || range_is_zero(&tmp))
		RETURN(-EINVAL);
	*range = tmp;
	RETURN(0);
}

static int
seq_proc_read_common(char *page, char **start, off_t off,
		     int count, int *eof, void *data,
		     struct lu_seq_range *range)
{
	int rc;
	ENTRY;

	*eof = 1;
	rc = snprintf(page, count, "["LPX64" - "LPX64"]:%x:%s\n",
			PRANGE(range));
	RETURN(rc);
}

/*
 * Server side procfs stuff.
 */
static int
seq_server_proc_write_space(struct file *file, const char *buffer,
			    unsigned long count, void *data)
{
	struct lu_server_seq *seq = (struct lu_server_seq *)data;
	int rc;
	ENTRY;

	LASSERT(seq != NULL);

	mutex_lock(&seq->lss_mutex);
	rc = seq_proc_write_common(file, buffer, count,
				   data, &seq->lss_space);
	if (rc == 0) {
		CDEBUG(D_INFO, "%s: Space: "DRANGE"\n",
		       seq->lss_name, PRANGE(&seq->lss_space));
	}

	mutex_unlock(&seq->lss_mutex);

	RETURN(count);
}

static int
seq_server_proc_read_space(char *page, char **start, off_t off,
			   int count, int *eof, void *data)
{
	struct lu_server_seq *seq = (struct lu_server_seq *)data;
	int rc;
	ENTRY;

	LASSERT(seq != NULL);

	mutex_lock(&seq->lss_mutex);
	rc = seq_proc_read_common(page, start, off, count, eof,
				  data, &seq->lss_space);
	mutex_unlock(&seq->lss_mutex);

	RETURN(rc);
}

static int
seq_server_proc_read_server(char *page, char **start, off_t off,
			    int count, int *eof, void *data)
{
	struct lu_server_seq *seq = (struct lu_server_seq *)data;
	struct client_obd *cli;
	int rc;
	ENTRY;

	LASSERT(seq != NULL);

	*eof = 1;
	if (seq->lss_cli) {
		if (seq->lss_cli->lcs_exp != NULL) {
			cli = &seq->lss_cli->lcs_exp->exp_obd->u.cli;
			rc = snprintf(page, count, "%s\n",
				      cli->cl_target_uuid.uuid);
		} else {
			rc = snprintf(page, count, "%s\n",
				      seq->lss_cli->lcs_srv->lss_name);
		}
	} else {
		rc = snprintf(page, count, "<none>\n");
	}

	RETURN(rc);
}

static int
seq_server_proc_write_width(struct file *file, const char *buffer,
			    unsigned long count, void *data)
{
	struct lu_server_seq *seq = (struct lu_server_seq *)data;
	int rc, val;
	ENTRY;

	LASSERT(seq != NULL);

	mutex_lock(&seq->lss_mutex);

	rc = lprocfs_write_helper(buffer, count, &val);
	if (rc != 0) {
		CERROR("%s: invalid width.\n", seq->lss_name);
		GOTO(out_unlock, rc);
	}

	seq->lss_width = val;

	CDEBUG(D_INFO, "%s: Width: "LPU64"\n",
	       seq->lss_name, seq->lss_width);
out_unlock:
	mutex_unlock(&seq->lss_mutex);

	RETURN(count);
}

static int
seq_server_proc_read_width(char *page, char **start, off_t off,
			   int count, int *eof, void *data)
{
	struct lu_server_seq *seq = (struct lu_server_seq *)data;
	int rc;
	ENTRY;

	LASSERT(seq != NULL);

	mutex_lock(&seq->lss_mutex);
	rc = snprintf(page, count, LPU64"\n", seq->lss_width);
	mutex_unlock(&seq->lss_mutex);

	RETURN(rc);
}

/* Client side procfs stuff */
static int
seq_client_proc_write_space(struct file *file, const char *buffer,
			    unsigned long count, void *data)
{
	struct lu_client_seq *seq = (struct lu_client_seq *)data;
	int rc;
	ENTRY;

	LASSERT(seq != NULL);

	mutex_lock(&seq->lcs_mutex);
	rc = seq_proc_write_common(file, buffer, count,
				   data, &seq->lcs_space);

	if (rc == 0) {
		CDEBUG(D_INFO, "%s: Space: "DRANGE"\n",
		       seq->lcs_name, PRANGE(&seq->lcs_space));
	}

	mutex_unlock(&seq->lcs_mutex);

	RETURN(count);
}

static int
seq_client_proc_read_space(char *page, char **start, off_t off,
			   int count, int *eof, void *data)
{
	struct lu_client_seq *seq = (struct lu_client_seq *)data;
	int rc;
	ENTRY;

	LASSERT(seq != NULL);

	mutex_lock(&seq->lcs_mutex);
	rc = seq_proc_read_common(page, start, off, count, eof,
				  data, &seq->lcs_space);
	mutex_unlock(&seq->lcs_mutex);

	RETURN(rc);
}

static int
seq_client_proc_write_width(struct file *file, const char *buffer,
			    unsigned long count, void *data)
{
	struct lu_client_seq *seq = (struct lu_client_seq *)data;
	__u64  max;
	int rc, val;
	ENTRY;

	LASSERT(seq != NULL);

	mutex_lock(&seq->lcs_mutex);

	rc = lprocfs_write_helper(buffer, count, &val);
	if (rc) {
		mutex_unlock(&seq->lcs_mutex);
		RETURN(rc);
	}

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

	RETURN(count);
}

static int
seq_client_proc_read_width(char *page, char **start, off_t off,
			   int count, int *eof, void *data)
{
	struct lu_client_seq *seq = (struct lu_client_seq *)data;
	int rc;
	ENTRY;

	LASSERT(seq != NULL);

	mutex_lock(&seq->lcs_mutex);
	rc = snprintf(page, count, LPU64"\n", seq->lcs_width);
	mutex_unlock(&seq->lcs_mutex);

	RETURN(rc);
}

static int
seq_client_proc_read_fid(char *page, char **start, off_t off,
			 int count, int *eof, void *data)
{
	struct lu_client_seq *seq = (struct lu_client_seq *)data;
	int rc;
	ENTRY;

	LASSERT(seq != NULL);

	mutex_lock(&seq->lcs_mutex);
	rc = snprintf(page, count, DFID"\n", PFID(&seq->lcs_fid));
	mutex_unlock(&seq->lcs_mutex);

	RETURN(rc);
}

static int
seq_client_proc_read_server(char *page, char **start, off_t off,
			    int count, int *eof, void *data)
{
	struct lu_client_seq *seq = (struct lu_client_seq *)data;
	struct client_obd *cli;
	int rc;
	ENTRY;

	LASSERT(seq != NULL);

	if (seq->lcs_exp != NULL) {
		cli = &seq->lcs_exp->exp_obd->u.cli;
		rc = snprintf(page, count, "%s\n", cli->cl_target_uuid.uuid);
	} else {
		rc = snprintf(page, count, "%s\n", seq->lcs_srv->lss_name);
	}
	RETURN(rc);
}

struct lprocfs_vars seq_server_proc_list[] = {
	{ "space",    seq_server_proc_read_space, seq_server_proc_write_space, NULL },
	{ "width",    seq_server_proc_read_width, seq_server_proc_write_width, NULL },
	{ "server",   seq_server_proc_read_server, NULL, NULL },
	{ NULL }};

struct lprocfs_vars seq_client_proc_list[] = {
	{ "space",    seq_client_proc_read_space, seq_client_proc_write_space, NULL },
	{ "width",    seq_client_proc_read_width, seq_client_proc_write_width, NULL },
	{ "server",   seq_client_proc_read_server, NULL, NULL },
	{ "fid",      seq_client_proc_read_fid, NULL, NULL },
	{ NULL }};
#endif
