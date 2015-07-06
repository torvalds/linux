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
 * Copyright (c) 1999, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright (c) 2011, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 */

#include <linux/module.h>
#include <linux/sysctl.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/stat.h>
#include <linux/ctype.h>
#include <linux/bitops.h>
#include <linux/uaccess.h>
#include <linux/utsname.h>

#define DEBUG_SUBSYSTEM S_CLASS

#include "../../include/obd_support.h"
#include "../../include/lprocfs_status.h"

#ifdef CONFIG_SYSCTL
static struct ctl_table_header *obd_table_header;
#endif

struct static_lustre_uintvalue_attr {
	struct {
		struct attribute attr;
		ssize_t (*show)(struct kobject *kobj, struct attribute *attr,
				char *buf);
		ssize_t (*store)(struct kobject *kobj, struct attribute *attr,
				 const char *buf, size_t len);
	} u;
	int *value;
};

static ssize_t static_uintvalue_show(struct kobject *kobj,
				    struct attribute *attr,
				    char *buf)
{
	struct static_lustre_uintvalue_attr *lattr = (void *)attr;

	return sprintf(buf, "%d\n", *lattr->value);
}

static ssize_t static_uintvalue_store(struct kobject *kobj,
				     struct attribute *attr,
				     const char *buffer, size_t count)
{
	struct static_lustre_uintvalue_attr *lattr  = (void *)attr;
	int rc;
	unsigned int val;

	rc = kstrtouint(buffer, 10, &val);
	if (rc)
		return rc;

	*lattr->value = val;

	return count;
}

#define LUSTRE_STATIC_UINT_ATTR(name, value) \
static struct static_lustre_uintvalue_attr lustre_sattr_##name =	\
					{__ATTR(name, 0644,		\
						static_uintvalue_show,	\
						static_uintvalue_store),\
					  value }

LUSTRE_STATIC_UINT_ATTR(timeout, &obd_timeout);

static ssize_t max_dirty_mb_show(struct kobject *kobj, struct attribute *attr,
				 char *buf)
{
	return sprintf(buf, "%ul\n",
			obd_max_dirty_pages / (1 << (20 - PAGE_CACHE_SHIFT)));
}

static ssize_t max_dirty_mb_store(struct kobject *kobj, struct attribute *attr,
				  const char *buffer, size_t count)
{
	int rc;
	unsigned long val;

	rc = kstrtoul(buffer, 10, &val);
	if (rc)
		return rc;

	val *= 1 << (20 - PAGE_CACHE_SHIFT); /* convert to pages */

	if (val > ((totalram_pages / 10) * 9)) {
		/* Somebody wants to assign too much memory to dirty pages */
		return -EINVAL;
	}

	if (val < 4 << (20 - PAGE_CACHE_SHIFT)) {
		/* Less than 4 Mb for dirty cache is also bad */
		return -EINVAL;
	}

	obd_max_dirty_pages = val;

	return count;
}
LUSTRE_RW_ATTR(max_dirty_mb);

#ifdef CONFIG_SYSCTL
static struct ctl_table obd_table[] = {
	{
		.procname = "debug_peer_on_timeout",
		.data     = &obd_debug_peer_on_timeout,
		.maxlen   = sizeof(int),
		.mode     = 0644,
		.proc_handler = &proc_dointvec
	},
	{
		.procname = "dump_on_timeout",
		.data     = &obd_dump_on_timeout,
		.maxlen   = sizeof(int),
		.mode     = 0644,
		.proc_handler = &proc_dointvec
	},
	{
		.procname = "dump_on_eviction",
		.data     = &obd_dump_on_eviction,
		.maxlen   = sizeof(int),
		.mode     = 0644,
		.proc_handler = &proc_dointvec
	},
	{
		.procname = "at_min",
		.data     = &at_min,
		.maxlen   = sizeof(int),
		.mode     = 0644,
		.proc_handler = &proc_dointvec,
	},
	{
		.procname = "at_max",
		.data     = &at_max,
		.maxlen   = sizeof(int),
		.mode     = 0644,
		.proc_handler = &proc_dointvec,
	},
	{
		.procname = "at_extra",
		.data     = &at_extra,
		.maxlen   = sizeof(int),
		.mode     = 0644,
		.proc_handler = &proc_dointvec,
	},
	{
		.procname = "at_early_margin",
		.data     = &at_early_margin,
		.maxlen   = sizeof(int),
		.mode     = 0644,
		.proc_handler = &proc_dointvec,
	},
	{
		.procname = "at_history",
		.data     = &at_history,
		.maxlen   = sizeof(int),
		.mode     = 0644,
		.proc_handler = &proc_dointvec,
	},
	{}
};

static struct ctl_table parent_table[] = {
	{
		.procname = "lustre",
		.data     = NULL,
		.maxlen   = 0,
		.mode     = 0555,
		.child    = obd_table
	},
	{}
};
#endif

static struct attribute *lustre_attrs[] = {
	&lustre_sattr_timeout.u.attr,
	&lustre_attr_max_dirty_mb.attr,
	NULL,
};

static struct attribute_group lustre_attr_group = {
	.attrs = lustre_attrs,
};

int obd_sysctl_init(void)
{
#ifdef CONFIG_SYSCTL
	if (!obd_table_header)
		obd_table_header = register_sysctl_table(parent_table);
#endif
	return sysfs_create_group(lustre_kobj, &lustre_attr_group);
}

void obd_sysctl_clean(void)
{
#ifdef CONFIG_SYSCTL
	if (obd_table_header)
		unregister_sysctl_table(obd_table_header);
	obd_table_header = NULL;
#endif
}
