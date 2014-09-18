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

#include <asm/statfs.h>
#include "../include/lprocfs_status.h"
#include "../include/obd_class.h"
#include <linux/seq_file.h>
#include "lov_internal.h"

static int lov_stripesize_seq_show(struct seq_file *m, void *v)
{
	struct obd_device *dev = (struct obd_device *)m->private;
	struct lov_desc *desc;

	LASSERT(dev != NULL);
	desc = &dev->u.lov.desc;
	return seq_printf(m, "%llu\n", desc->ld_default_stripe_size);
}

static ssize_t lov_stripesize_seq_write(struct file *file, const char *buffer,
				    size_t count, loff_t *off)
{
	struct obd_device *dev = ((struct seq_file *)file->private_data)->private;
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
LPROC_SEQ_FOPS(lov_stripesize);

static int lov_stripeoffset_seq_show(struct seq_file *m, void *v)
{
	struct obd_device *dev = (struct obd_device *)m->private;
	struct lov_desc *desc;

	LASSERT(dev != NULL);
	desc = &dev->u.lov.desc;
	return seq_printf(m, "%llu\n", desc->ld_default_stripe_offset);
}

static ssize_t lov_stripeoffset_seq_write(struct file *file, const char *buffer,
				      size_t count, loff_t *off)
{
	struct obd_device *dev = ((struct seq_file *)file->private_data)->private;
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
LPROC_SEQ_FOPS(lov_stripeoffset);

static int lov_stripetype_seq_show(struct seq_file *m, void *v)
{
	struct obd_device *dev = (struct obd_device *)m->private;
	struct lov_desc *desc;

	LASSERT(dev != NULL);
	desc = &dev->u.lov.desc;
	return seq_printf(m, "%u\n", desc->ld_pattern);
}

static ssize_t lov_stripetype_seq_write(struct file *file, const char *buffer,
				    size_t count, loff_t *off)
{
	struct obd_device *dev = ((struct seq_file *)file->private_data)->private;
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
LPROC_SEQ_FOPS(lov_stripetype);

static int lov_stripecount_seq_show(struct seq_file *m, void *v)
{
	struct obd_device *dev = (struct obd_device *)m->private;
	struct lov_desc *desc;

	LASSERT(dev != NULL);
	desc = &dev->u.lov.desc;
	return seq_printf(m, "%d\n",
			(__s16)(desc->ld_default_stripe_count + 1) - 1);
}

static ssize_t lov_stripecount_seq_write(struct file *file, const char *buffer,
				     size_t count, loff_t *off)
{
	struct obd_device *dev = ((struct seq_file *)file->private_data)->private;
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
LPROC_SEQ_FOPS(lov_stripecount);

static int lov_numobd_seq_show(struct seq_file *m, void *v)
{
	struct obd_device *dev = (struct obd_device *)m->private;
	struct lov_desc *desc;

	LASSERT(dev != NULL);
	desc = &dev->u.lov.desc;
	return seq_printf(m, "%u\n", desc->ld_tgt_count);
}
LPROC_SEQ_FOPS_RO(lov_numobd);

static int lov_activeobd_seq_show(struct seq_file *m, void *v)
{
	struct obd_device *dev = (struct obd_device *)m->private;
	struct lov_desc *desc;

	LASSERT(dev != NULL);
	desc = &dev->u.lov.desc;
	return seq_printf(m, "%u\n", desc->ld_active_tgt_count);
}
LPROC_SEQ_FOPS_RO(lov_activeobd);

static int lov_desc_uuid_seq_show(struct seq_file *m, void *v)
{
	struct obd_device *dev = (struct obd_device *)m->private;
	struct lov_obd *lov;

	LASSERT(dev != NULL);
	lov = &dev->u.lov;
	return seq_printf(m, "%s\n", lov->desc.ld_uuid.uuid);
}
LPROC_SEQ_FOPS_RO(lov_desc_uuid);

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
	struct seq_file *seq;
	int rc;

	rc = seq_open(file, &lov_tgt_sops);
	if (rc)
		return rc;

	seq = file->private_data;
	seq->private = PDE_DATA(inode);
	return 0;
}

LPROC_SEQ_FOPS_RO_TYPE(lov, uuid);
LPROC_SEQ_FOPS_RO_TYPE(lov, filestotal);
LPROC_SEQ_FOPS_RO_TYPE(lov, filesfree);
LPROC_SEQ_FOPS_RO_TYPE(lov, blksize);
LPROC_SEQ_FOPS_RO_TYPE(lov, kbytestotal);
LPROC_SEQ_FOPS_RO_TYPE(lov, kbytesfree);
LPROC_SEQ_FOPS_RO_TYPE(lov, kbytesavail);

struct lprocfs_vars lprocfs_lov_obd_vars[] = {
	{ "uuid",	  &lov_uuid_fops,	  NULL, 0 },
	{ "stripesize",   &lov_stripesize_fops,   NULL },
	{ "stripeoffset", &lov_stripeoffset_fops, NULL },
	{ "stripecount",  &lov_stripecount_fops,  NULL },
	{ "stripetype",   &lov_stripetype_fops,   NULL },
	{ "numobd",       &lov_numobd_fops,	  NULL, 0 },
	{ "activeobd",    &lov_activeobd_fops,	  NULL, 0 },
	{ "filestotal",   &lov_filestotal_fops,   NULL, 0 },
	{ "filesfree",    &lov_filesfree_fops,    NULL, 0 },
	/*{ "filegroups", lprocfs_rd_filegroups,  NULL, 0 },*/
	{ "blocksize",    &lov_blksize_fops,      NULL, 0 },
	{ "kbytestotal",  &lov_kbytestotal_fops,  NULL, 0 },
	{ "kbytesfree",   &lov_kbytesfree_fops,   NULL, 0 },
	{ "kbytesavail",  &lov_kbytesavail_fops,  NULL, 0 },
	{ "desc_uuid",    &lov_desc_uuid_fops,    NULL, 0 },
	{ NULL }
};

LPROC_SEQ_FOPS_RO_TYPE(lov, numrefs);

static struct lprocfs_vars lprocfs_lov_module_vars[] = {
	{ "num_refs",     &lov_numrefs_fops,     NULL, 0 },
	{ NULL }
};

void lprocfs_lov_init_vars(struct lprocfs_static_vars *lvars)
{
    lvars->module_vars  = lprocfs_lov_module_vars;
    lvars->obd_vars     = lprocfs_lov_obd_vars;
}

const struct file_operations lov_proc_target_fops = {
	.owner   = THIS_MODULE,
	.open    = lov_target_seq_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = lprocfs_seq_release,
};
