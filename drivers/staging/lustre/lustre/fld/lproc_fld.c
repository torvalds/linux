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
 * Copyright (c) 2012, 2015, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 *
 * lustre/fld/lproc_fld.c
 *
 * FLD (FIDs Location Database)
 *
 * Author: Yury Umanets <umka@clusterfs.com>
 *	Di Wang <di.wang@whamcloud.com>
 */

#define DEBUG_SUBSYSTEM S_FLD

#include "../../include/linux/libcfs/libcfs.h"
#include <linux/module.h>

#include "../include/obd.h"
#include "../include/obd_class.h"
#include "../include/obd_support.h"
#include "../include/lustre_req_layout.h"
#include "../include/lustre_fld.h"
#include "../include/lustre_fid.h"
#include "fld_internal.h"

static int
fld_debugfs_targets_seq_show(struct seq_file *m, void *unused)
{
	struct lu_client_fld *fld = (struct lu_client_fld *)m->private;
	struct lu_fld_target *target;

	LASSERT(fld != NULL);

	spin_lock(&fld->lcf_lock);
	list_for_each_entry(target,
				&fld->lcf_targets, ft_chain)
		seq_printf(m, "%s\n", fld_target_name(target));
	spin_unlock(&fld->lcf_lock);

	return 0;
}

static int
fld_debugfs_hash_seq_show(struct seq_file *m, void *unused)
{
	struct lu_client_fld *fld = (struct lu_client_fld *)m->private;

	LASSERT(fld != NULL);

	spin_lock(&fld->lcf_lock);
	seq_printf(m, "%s\n", fld->lcf_hash->fh_name);
	spin_unlock(&fld->lcf_lock);

	return 0;
}

static ssize_t
fld_debugfs_hash_seq_write(struct file *file,
			   const char __user *buffer,
			   size_t count, loff_t *off)
{
	struct lu_client_fld *fld;
	struct lu_fld_hash *hash = NULL;
	char fh_name[8];
	int i;

	if (count > sizeof(fh_name))
		return -ENAMETOOLONG;

	if (copy_from_user(fh_name, buffer, count) != 0)
		return -EFAULT;

	fld = ((struct seq_file *)file->private_data)->private;
	LASSERT(fld != NULL);

	for (i = 0; fld_hash[i].fh_name != NULL; i++) {
		if (count != strlen(fld_hash[i].fh_name))
			continue;

		if (!strncmp(fld_hash[i].fh_name, fh_name, count)) {
			hash = &fld_hash[i];
			break;
		}
	}

	if (hash != NULL) {
		spin_lock(&fld->lcf_lock);
		fld->lcf_hash = hash;
		spin_unlock(&fld->lcf_lock);

		CDEBUG(D_INFO, "%s: Changed hash to \"%s\"\n",
		       fld->lcf_name, hash->fh_name);
	}

	return count;
}

static ssize_t
fld_debugfs_cache_flush_write(struct file *file, const char __user *buffer,
			      size_t count, loff_t *pos)
{
	struct lu_client_fld *fld = file->private_data;

	LASSERT(fld != NULL);

	fld_cache_flush(fld->lcf_cache);

	CDEBUG(D_INFO, "%s: Lookup cache is flushed\n", fld->lcf_name);

	return count;
}

static int
fld_debugfs_cache_flush_release(struct inode *inode, struct file *file)
{
	file->private_data = NULL;
	return 0;
}

static struct file_operations fld_debugfs_cache_flush_fops = {
	.owner		= THIS_MODULE,
	.open           = simple_open,
	.write		= fld_debugfs_cache_flush_write,
	.release	= fld_debugfs_cache_flush_release,
};

LPROC_SEQ_FOPS_RO(fld_debugfs_targets);
LPROC_SEQ_FOPS(fld_debugfs_hash);

struct lprocfs_vars fld_client_debugfs_list[] = {
	{ "targets",	 &fld_debugfs_targets_fops },
	{ "hash",	 &fld_debugfs_hash_fops },
	{ "cache_flush", &fld_debugfs_cache_flush_fops },
	{ NULL }
};
