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
 * Copyright (c) 2012, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 */
#define DEBUG_SUBSYSTEM S_CLASS

#include <linux/version.h>
#include <asm/statfs.h>
#include <lprocfs_status.h>
#include <obd_class.h>
#include <linux/seq_file.h>
#include "lov_internal.h"

#ifdef LPROCFS
static int lov_rd_stripesize(char *page, char **start, off_t off, int count,
			     int *eof, void *data)
{
	struct obd_device *dev = (struct obd_device *)data;
	struct lov_desc *desc;

	LASSERT(dev != NULL);
	desc = &dev->u.lov.desc;
	*eof = 1;
	return snprintf(page, count, LPU64"\n", desc->ld_default_stripe_size);
}

static int lov_wr_stripesize(struct file *file, const char *buffer,
			       unsigned long count, void *data)
{
	struct obd_device *dev = (struct obd_device *)data;
	struct lov_desc *desc;
	__u64 val;
	int rc;

	LASSERT(dev != NULL);
	desc = &dev->u.lov.desc;
	rc = lprocfs_write_u64_helper(buffer, count, &val);
	if (rc)
		return rc;

	lov_fix_desc_stripe_size(&val);
	desc->ld_default_stripe_size = val;
	return count;
}

static int lov_rd_stripeoffset(char *page, char **start, off_t off, int count,
			       int *eof, void *data)
{
	struct obd_device *dev = (struct obd_device *)data;
	struct lov_desc *desc;

	LASSERT(dev != NULL);
	desc = &dev->u.lov.desc;
	*eof = 1;
	return snprintf(page, count, LPU64"\n", desc->ld_default_stripe_offset);
}

static int lov_wr_stripeoffset(struct file *file, const char *buffer,
			       unsigned long count, void *data)
{
	struct obd_device *dev = (struct obd_device *)data;
	struct lov_desc *desc;
	__u64 val;
	int rc;

	LASSERT(dev != NULL);
	desc = &dev->u.lov.desc;
	rc = lprocfs_write_u64_helper(buffer, count, &val);
	if (rc)
		return rc;

	desc->ld_default_stripe_offset = val;
	return count;
}

static int lov_rd_stripetype(char *page, char **start, off_t off, int count,
			     int *eof, void *data)
{
	struct obd_device* dev = (struct obd_device*)data;
	struct lov_desc *desc;

	LASSERT(dev != NULL);
	desc = &dev->u.lov.desc;
	*eof = 1;
	return snprintf(page, count, "%u\n", desc->ld_pattern);
}

static int lov_wr_stripetype(struct file *file, const char *buffer,
			     unsigned long count, void *data)
{
	struct obd_device *dev = (struct obd_device *)data;
	struct lov_desc *desc;
	int val, rc;

	LASSERT(dev != NULL);
	desc = &dev->u.lov.desc;
	rc = lprocfs_write_helper(buffer, count, &val);
	if (rc)
		return rc;

	lov_fix_desc_pattern(&val);
	desc->ld_pattern = val;
	return count;
}

static int lov_rd_stripecount(char *page, char **start, off_t off, int count,
			      int *eof, void *data)
{
	struct obd_device *dev = (struct obd_device *)data;
	struct lov_desc *desc;

	LASSERT(dev != NULL);
	desc = &dev->u.lov.desc;
	*eof = 1;
	return snprintf(page, count, "%d\n",
			(__s16)(desc->ld_default_stripe_count + 1) - 1);
}

static int lov_wr_stripecount(struct file *file, const char *buffer,
			      unsigned long count, void *data)
{
	struct obd_device *dev = (struct obd_device *)data;
	struct lov_desc *desc;
	int val, rc;

	LASSERT(dev != NULL);
	desc = &dev->u.lov.desc;
	rc = lprocfs_write_helper(buffer, count, &val);
	if (rc)
		return rc;

	lov_fix_desc_stripe_count(&val);
	desc->ld_default_stripe_count = val;
	return count;
}

static int lov_rd_numobd(char *page, char **start, off_t off, int count,
			 int *eof, void *data)
{
	struct obd_device *dev = (struct obd_device*)data;
	struct lov_desc *desc;

	LASSERT(dev != NULL);
	desc = &dev->u.lov.desc;
	*eof = 1;
	return snprintf(page, count, "%u\n", desc->ld_tgt_count);

}

static int lov_rd_activeobd(char *page, char **start, off_t off, int count,
			    int *eof, void *data)
{
	struct obd_device* dev = (struct obd_device*)data;
	struct lov_desc *desc;

	LASSERT(dev != NULL);
	desc = &dev->u.lov.desc;
	*eof = 1;
	return snprintf(page, count, "%u\n", desc->ld_active_tgt_count);
}

static int lov_rd_desc_uuid(char *page, char **start, off_t off, int count,
			    int *eof, void *data)
{
	struct obd_device *dev = (struct obd_device*) data;
	struct lov_obd *lov;

	LASSERT(dev != NULL);
	lov = &dev->u.lov;
	*eof = 1;
	return snprintf(page, count, "%s\n", lov->desc.ld_uuid.uuid);
}

static void *lov_tgt_seq_start(struct seq_file *p, loff_t *pos)
{
	struct obd_device *dev = p->private;
	struct lov_obd *lov = &dev->u.lov;

	while (*pos < lov->desc.ld_tgt_count) {
		if (lov->lov_tgts[*pos])
			return lov->lov_tgts[*pos];
		++*pos;
	}
	return NULL;
}

static void lov_tgt_seq_stop(struct seq_file *p, void *v)
{
}

static void *lov_tgt_seq_next(struct seq_file *p, void *v, loff_t *pos)
{
	struct obd_device *dev = p->private;
	struct lov_obd *lov = &dev->u.lov;

	while (++*pos < lov->desc.ld_tgt_count) {
		if (lov->lov_tgts[*pos])
			return lov->lov_tgts[*pos];
	}
	return NULL;
}

static int lov_tgt_seq_show(struct seq_file *p, void *v)
{
	struct lov_tgt_desc *tgt = v;
	return seq_printf(p, "%d: %s %sACTIVE\n", tgt->ltd_index,
			  obd_uuid2str(&tgt->ltd_uuid),
			  tgt->ltd_active ? "" : "IN");
}

struct seq_operations lov_tgt_sops = {
	.start = lov_tgt_seq_start,
	.stop = lov_tgt_seq_stop,
	.next = lov_tgt_seq_next,
	.show = lov_tgt_seq_show,
};

static int lov_target_seq_open(struct inode *inode, struct file *file)
{
	struct proc_dir_entry *dp = PDE(inode);
	struct seq_file *seq;
	int rc;

	LPROCFS_ENTRY_AND_CHECK(dp);
	rc = seq_open(file, &lov_tgt_sops);
	if (rc) {
		LPROCFS_EXIT();
		return rc;
	}

	seq = file->private_data;
	seq->private = dp->data;
	return 0;
}

struct lprocfs_vars lprocfs_lov_obd_vars[] = {
	{ "uuid",	 lprocfs_rd_uuid,	0, 0 },
	{ "stripesize",   lov_rd_stripesize,      lov_wr_stripesize, 0 },
	{ "stripeoffset", lov_rd_stripeoffset,    lov_wr_stripeoffset, 0 },
	{ "stripecount",  lov_rd_stripecount,     lov_wr_stripecount, 0 },
	{ "stripetype",   lov_rd_stripetype,      lov_wr_stripetype, 0 },
	{ "numobd",       lov_rd_numobd,	  0, 0 },
	{ "activeobd",    lov_rd_activeobd,       0, 0 },
	{ "filestotal",   lprocfs_rd_filestotal,  0, 0 },
	{ "filesfree",    lprocfs_rd_filesfree,   0, 0 },
	/*{ "filegroups", lprocfs_rd_filegroups,  0, 0 },*/
	{ "blocksize",    lprocfs_rd_blksize,     0, 0 },
	{ "kbytestotal",  lprocfs_rd_kbytestotal, 0, 0 },
	{ "kbytesfree",   lprocfs_rd_kbytesfree,  0, 0 },
	{ "kbytesavail",  lprocfs_rd_kbytesavail, 0, 0 },
	{ "desc_uuid",    lov_rd_desc_uuid,       0, 0 },
	{ 0 }
};

static struct lprocfs_vars lprocfs_lov_module_vars[] = {
	{ "num_refs",     lprocfs_rd_numrefs,     0, 0 },
	{ 0 }
};

void lprocfs_lov_init_vars(struct lprocfs_static_vars *lvars)
{
    lvars->module_vars  = lprocfs_lov_module_vars;
    lvars->obd_vars     = lprocfs_lov_obd_vars;
}

struct file_operations lov_proc_target_fops = {
	.owner   = THIS_MODULE,
	.open    = lov_target_seq_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = lprocfs_seq_release,
};
#endif /* LPROCFS */
