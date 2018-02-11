// SPDX-License-Identifier: GPL-2.0
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
 * http://www.gnu.org/licenses/gpl-2.0.html
 *
 * GPL HEADER END
 */
/*
 * Copyright (c) 2002, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright (c) 2011, 2015, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 */
#define DEBUG_SUBSYSTEM S_CLASS

#include <linux/vfs.h>
#include <obd_class.h>
#include <lprocfs_status.h>
#include "mdc_internal.h"

static ssize_t active_show(struct kobject *kobj, struct attribute *attr,
			   char *buf)
{
	struct obd_device *dev = container_of(kobj, struct obd_device,
					      obd_kobj);

	return sprintf(buf, "%u\n", !dev->u.cli.cl_import->imp_deactive);
}

static ssize_t active_store(struct kobject *kobj, struct attribute *attr,
			    const char *buffer, size_t count)
{
	struct obd_device *dev = container_of(kobj, struct obd_device,
					      obd_kobj);
	unsigned long val;
	int rc;

	rc = kstrtoul(buffer, 10, &val);
	if (rc)
		return rc;

	if (val > 1)
		return -ERANGE;

	/* opposite senses */
	if (dev->u.cli.cl_import->imp_deactive == val) {
		rc = ptlrpc_set_import_active(dev->u.cli.cl_import, val);
		if (rc)
			count = rc;
	} else {
		CDEBUG(D_CONFIG, "activate %lu: ignoring repeat request\n", val);
	}
	return count;
}
LUSTRE_RW_ATTR(active);

static ssize_t max_rpcs_in_flight_show(struct kobject *kobj,
				       struct attribute *attr,
				       char *buf)
{
	int len;
	struct obd_device *dev = container_of(kobj, struct obd_device,
					      obd_kobj);
	__u32 max;

	max = obd_get_max_rpcs_in_flight(&dev->u.cli);
	len = sprintf(buf, "%u\n", max);

	return len;
}

static ssize_t max_rpcs_in_flight_store(struct kobject *kobj,
					struct attribute *attr,
					const char *buffer,
					size_t count)
{
	struct obd_device *dev = container_of(kobj, struct obd_device,
					      obd_kobj);
	int rc;
	unsigned long val;

	rc = kstrtoul(buffer, 10, &val);
	if (rc)
		return rc;

	rc = obd_set_max_rpcs_in_flight(&dev->u.cli, val);
	if (rc)
		count = rc;

	return count;
}
LUSTRE_RW_ATTR(max_rpcs_in_flight);

static ssize_t max_mod_rpcs_in_flight_show(struct kobject *kobj,
					   struct attribute *attr,
					   char *buf)
{
	struct obd_device *dev = container_of(kobj, struct obd_device,
					      obd_kobj);
	u16 max;
	int len;

	max = dev->u.cli.cl_max_mod_rpcs_in_flight;
	len = sprintf(buf, "%hu\n", max);

	return len;
}

static ssize_t max_mod_rpcs_in_flight_store(struct kobject *kobj,
					    struct attribute *attr,
					    const char *buffer,
					    size_t count)
{
	struct obd_device *dev = container_of(kobj, struct obd_device,
					      obd_kobj);
	u16 val;
	int rc;

	rc = kstrtou16(buffer, 10, &val);
	if (rc)
		return rc;

	rc = obd_set_max_mod_rpcs_in_flight(&dev->u.cli, val);
	if (rc)
		count = rc;

	return count;
}
LUSTRE_RW_ATTR(max_mod_rpcs_in_flight);

static int mdc_rpc_stats_seq_show(struct seq_file *seq, void *v)
{
	struct obd_device *dev = seq->private;

	return obd_mod_rpc_stats_seq_show(&dev->u.cli, seq);
}

static ssize_t mdc_rpc_stats_seq_write(struct file *file,
				       const char __user *buf,
				       size_t len, loff_t *off)
{
	struct seq_file *seq = file->private_data;
	struct obd_device *dev = seq->private;
	struct client_obd *cli = &dev->u.cli;

	lprocfs_oh_clear(&cli->cl_mod_rpcs_hist);

	return len;
}
LPROC_SEQ_FOPS(mdc_rpc_stats);

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
	{ "pinger_recov",	&mdc_pinger_recov_fops,		NULL, 0 },
	{ .name =	"rpc_stats",
	  .fops =	&mdc_rpc_stats_fops		},
	{ NULL }
};

static struct attribute *mdc_attrs[] = {
	&lustre_attr_active.attr,
	&lustre_attr_max_rpcs_in_flight.attr,
	&lustre_attr_max_mod_rpcs_in_flight.attr,
	&lustre_attr_max_pages_per_rpc.attr,
	NULL,
};

static const struct attribute_group mdc_attr_group = {
	.attrs = mdc_attrs,
};

void lprocfs_mdc_init_vars(struct lprocfs_static_vars *lvars)
{
	lvars->sysfs_vars   = &mdc_attr_group;
	lvars->obd_vars     = lprocfs_mdc_obd_vars;
}
