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
 * Copyright (c) 2004, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright (c) 2012, 2015, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 */

#define DEBUG_SUBSYSTEM S_LNET

#include <linux/lnet/lib-lnet.h>
#include <uapi/linux/lnet/lnet-dlc.h>

static int config_on_load;
module_param(config_on_load, int, 0444);
MODULE_PARM_DESC(config_on_load, "configure network at module load");

static struct mutex lnet_config_mutex;

static int
lnet_configure(void *arg)
{
	/* 'arg' only there so I can be passed to cfs_create_thread() */
	int rc = 0;

	mutex_lock(&lnet_config_mutex);

	if (!the_lnet.ln_niinit_self) {
		rc = try_module_get(THIS_MODULE);

		if (rc != 1)
			goto out;

		rc = LNetNIInit(LNET_PID_LUSTRE);
		if (rc >= 0) {
			the_lnet.ln_niinit_self = 1;
			rc = 0;
		} else {
			module_put(THIS_MODULE);
		}
	}

out:
	mutex_unlock(&lnet_config_mutex);
	return rc;
}

static int
lnet_unconfigure(void)
{
	int refcount;

	mutex_lock(&lnet_config_mutex);

	if (the_lnet.ln_niinit_self) {
		the_lnet.ln_niinit_self = 0;
		LNetNIFini();
		module_put(THIS_MODULE);
	}

	mutex_lock(&the_lnet.ln_api_mutex);
	refcount = the_lnet.ln_refcount;
	mutex_unlock(&the_lnet.ln_api_mutex);

	mutex_unlock(&lnet_config_mutex);
	return !refcount ? 0 : -EBUSY;
}

static int
lnet_dyn_configure(struct libcfs_ioctl_hdr *hdr)
{
	struct lnet_ioctl_config_data *conf =
		(struct lnet_ioctl_config_data *)hdr;
	int rc;

	if (conf->cfg_hdr.ioc_len < sizeof(*conf))
		return -EINVAL;

	mutex_lock(&lnet_config_mutex);
	if (!the_lnet.ln_niinit_self) {
		rc = -EINVAL;
		goto out_unlock;
	}
	rc = lnet_dyn_add_ni(LNET_PID_LUSTRE, conf);
out_unlock:
	mutex_unlock(&lnet_config_mutex);

	return rc;
}

static int
lnet_dyn_unconfigure(struct libcfs_ioctl_hdr *hdr)
{
	struct lnet_ioctl_config_data *conf =
		(struct lnet_ioctl_config_data *)hdr;
	int rc;

	if (conf->cfg_hdr.ioc_len < sizeof(*conf))
		return -EINVAL;

	mutex_lock(&lnet_config_mutex);
	if (!the_lnet.ln_niinit_self) {
		rc = -EINVAL;
		goto out_unlock;
	}
	rc = lnet_dyn_del_ni(conf->cfg_net);
out_unlock:
	mutex_unlock(&lnet_config_mutex);

	return rc;
}

static int
lnet_ioctl(unsigned int cmd, struct libcfs_ioctl_hdr *hdr)
{
	int rc;

	switch (cmd) {
	case IOC_LIBCFS_CONFIGURE: {
		struct libcfs_ioctl_data *data =
			(struct libcfs_ioctl_data *)hdr;

		if (data->ioc_hdr.ioc_len < sizeof(*data))
			return -EINVAL;

		the_lnet.ln_nis_from_mod_params = data->ioc_flags;
		return lnet_configure(NULL);
	}

	case IOC_LIBCFS_UNCONFIGURE:
		return lnet_unconfigure();

	case IOC_LIBCFS_ADD_NET:
		return lnet_dyn_configure(hdr);

	case IOC_LIBCFS_DEL_NET:
		return lnet_dyn_unconfigure(hdr);

	default:
		/*
		 * Passing LNET_PID_ANY only gives me a ref if the net is up
		 * already; I'll need it to ensure the net can't go down while
		 * I'm called into it
		 */
		rc = LNetNIInit(LNET_PID_ANY);
		if (rc >= 0) {
			rc = LNetCtl(cmd, hdr);
			LNetNIFini();
		}
		return rc;
	}
}

static DECLARE_IOCTL_HANDLER(lnet_ioctl_handler, lnet_ioctl);

static int __init lnet_init(void)
{
	int rc;

	mutex_init(&lnet_config_mutex);

	rc = lnet_lib_init();
	if (rc) {
		CERROR("lnet_lib_init: error %d\n", rc);
		return rc;
	}

	rc = libcfs_register_ioctl(&lnet_ioctl_handler);
	LASSERT(!rc);

	if (config_on_load) {
		/*
		 * Have to schedule a separate thread to avoid deadlocking
		 * in modload
		 */
		(void)kthread_run(lnet_configure, NULL, "lnet_initd");
	}

	return 0;
}

static void __exit lnet_exit(void)
{
	int rc;

	rc = libcfs_deregister_ioctl(&lnet_ioctl_handler);
	LASSERT(!rc);

	lnet_lib_exit();
}

MODULE_AUTHOR("OpenSFS, Inc. <http://www.lustre.org/>");
MODULE_DESCRIPTION("Lustre Networking layer");
MODULE_VERSION(LNET_VERSION);
MODULE_LICENSE("GPL");

module_init(lnet_init);
module_exit(lnet_exit);
