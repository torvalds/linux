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

#include <linux/vfs.h>
#include "../include/obd_class.h"
#include "../include/lprocfs_status.h"
#include "mdc_internal.h"

static ssize_t max_rpcs_in_flight_show(struct kobject *kobj,
				       struct attribute *attr,
				       char *buf)
{
	int len;
	struct obd_device *dev = container_of(kobj, struct obd_device,
					      obd_kobj);
	struct client_obd *cli = &dev->u.cli;

	client_obd_list_lock(&cli->cl_loi_list_lock);
	len = sprintf(buf, "%u\n", cli->cl_max_rpcs_in_flight);
	client_obd_list_unlock(&cli->cl_loi_list_lock);

	return len;
}

static ssize_t max_rpcs_in_flight_store(struct kobject *kobj,
					struct attribute *attr,
					const char *buffer,
					size_t count)
{
	struct obd_device *dev = container_of(kobj, struct obd_device,
					      obd_kobj);
	struct client_obd *cli = &dev->u.cli;
	int rc;
	unsigned long val;

	rc = kstrtoul(buffer, 10, &val);
	if (rc)
		return rc;

	if (val < 1 || val > MDC_MAX_RIF_MAX)
		return -ERANGE;

	client_obd_list_lock(&cli->cl_loi_list_lock);
	cli->cl_max_rpcs_in_flight = val;
	client_obd_list_unlock(&cli->cl_loi_list_lock);

	return count;
}
LUSTRE_RW_ATTR(max_rpcs_in_flight);

static int mdc_kuc_open(struct inode *inode, struct file *file)
{
	return single_open(file, NULL, inode->i_private);
}

/* temporary for testing */
static ssize_t mdc_kuc_write(struct file *file,
				const char __user *buffer,
				size_t count, loff_t *off)
{
	struct obd_device *obd =
			((struct seq_file *)file->private_data)->private;
	struct kuc_hdr		*lh;
	struct hsm_action_list	*hal;
	struct hsm_action_item	*hai;
	int			 len;
	int			 fd, rc;

	rc = lprocfs_write_helper(buffer, count, &fd);
	if (rc)
		return rc;

	if (fd < 0)
		return -ERANGE;
	CWARN("message to fd %d\n", fd);

	len = sizeof(*lh) + sizeof(*hal) + MTI_NAME_MAXLEN +
		/* for mockup below */ 2 * cfs_size_round(sizeof(*hai));

	lh = kzalloc(len, GFP_NOFS);
	if (!lh)
		return -ENOMEM;

	lh->kuc_magic = KUC_MAGIC;
	lh->kuc_transport = KUC_TRANSPORT_HSM;
	lh->kuc_msgtype = HMT_ACTION_LIST;
	lh->kuc_msglen = len;

	hal = (struct hsm_action_list *)(lh + 1);
	hal->hal_version = HAL_VERSION;
	hal->hal_archive_id = 1;
	hal->hal_flags = 0;
	obd_uuid2fsname(hal->hal_fsname, obd->obd_name, MTI_NAME_MAXLEN);

	/* mock up an action list */
	hal->hal_count = 2;
	hai = hai_zero(hal);
	hai->hai_action = HSMA_ARCHIVE;
	hai->hai_fid.f_oid = 5;
	hai->hai_len = sizeof(*hai);
	hai = hai_next(hai);
	hai->hai_action = HSMA_RESTORE;
	hai->hai_fid.f_oid = 10;
	hai->hai_len = sizeof(*hai);

	/* This works for either broadcast or unicast to a single fd */
	if (fd == 0) {
		rc = libcfs_kkuc_group_put(KUC_GRP_HSM, lh);
	} else {
		struct file *fp = fget(fd);

		rc = libcfs_kkuc_msg_put(fp, lh);
		fput(fp);
	}
	kfree(lh);
	if (rc < 0)
		return rc;
	return count;
}

static struct file_operations mdc_kuc_fops = {
	.open		= mdc_kuc_open,
	.write		= mdc_kuc_write,
	.release	= single_release,
};

LPROC_SEQ_FOPS_WR_ONLY(mdc, ping);

LPROC_SEQ_FOPS_RO_TYPE(mdc, connect_flags);
LPROC_SEQ_FOPS_RO_TYPE(mdc, server_uuid);
LPROC_SEQ_FOPS_RO_TYPE(mdc, conn_uuid);
LPROC_SEQ_FOPS_RO_TYPE(mdc, timeouts);
LPROC_SEQ_FOPS_RO_TYPE(mdc, state);

/*
 * Note: below sysfs entry is provided, but not currently in use, instead
 * sbi->sb_md_brw_size is used, the per obd variable should be used
 * when DNE is enabled, and dir pages are managed in MDC layer.
 * Don't forget to enable sysfs store function then.
 */
static ssize_t max_pages_per_rpc_show(struct kobject *kobj,
				      struct attribute *attr,
				      char *buf)
{
	struct obd_device *dev = container_of(kobj, struct obd_device,
					      obd_kobj);
	struct client_obd *cli = &dev->u.cli;

	return sprintf(buf, "%d\n", cli->cl_max_pages_per_rpc);
}
LUSTRE_RO_ATTR(max_pages_per_rpc);

LPROC_SEQ_FOPS_RW_TYPE(mdc, import);
LPROC_SEQ_FOPS_RW_TYPE(mdc, pinger_recov);

static struct lprocfs_vars lprocfs_mdc_obd_vars[] = {
	{ "ping",		&mdc_ping_fops,			NULL, 0222 },
	{ "connect_flags",	&mdc_connect_flags_fops,	NULL, 0 },
	/*{ "filegroups",	lprocfs_rd_filegroups,		NULL, 0 },*/
	{ "mds_server_uuid",	&mdc_server_uuid_fops,		NULL, 0 },
	{ "mds_conn_uuid",	&mdc_conn_uuid_fops,		NULL, 0 },
	{ "timeouts",		&mdc_timeouts_fops,		NULL, 0 },
	{ "import",		&mdc_import_fops,		NULL, 0 },
	{ "state",		&mdc_state_fops,		NULL, 0 },
	{ "hsm_nl",		&mdc_kuc_fops,			NULL, 0200 },
	{ "pinger_recov",	&mdc_pinger_recov_fops,		NULL, 0 },
	{ NULL }
};

static struct attribute *mdc_attrs[] = {
	&lustre_attr_max_rpcs_in_flight.attr,
	&lustre_attr_max_pages_per_rpc.attr,
	NULL,
};

static struct attribute_group mdc_attr_group = {
	.attrs = mdc_attrs,
};

void lprocfs_mdc_init_vars(struct lprocfs_static_vars *lvars)
{
	lvars->sysfs_vars   = &mdc_attr_group;
	lvars->obd_vars     = lprocfs_mdc_obd_vars;
}
