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
 * Copyright (c) 2008, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright (c) 2012, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 */

#define DEBUG_SUBSYSTEM S_LNET

#include "../../../include/linux/libcfs/libcfs.h"

#define LNET_MINOR 240

int libcfs_ioctl_data_adjust(struct libcfs_ioctl_data *data)
{
	if (libcfs_ioctl_is_invalid(data)) {
		CERROR("libcfs ioctl: parameter not correctly formatted\n");
		return -EINVAL;
	}

	if (data->ioc_inllen1)
		data->ioc_inlbuf1 = &data->ioc_bulk[0];

	if (data->ioc_inllen2)
		data->ioc_inlbuf2 = &data->ioc_bulk[0] +
			cfs_size_round(data->ioc_inllen1);

	return 0;
}

int libcfs_ioctl_getdata(struct libcfs_ioctl_hdr **hdr_pp,
			 const struct libcfs_ioctl_hdr __user *uhdr)
{
	struct libcfs_ioctl_hdr hdr;
	int err = 0;

	if (copy_from_user(&hdr, uhdr, sizeof(uhdr)))
		return -EFAULT;

	if (hdr.ioc_version != LIBCFS_IOCTL_VERSION &&
	    hdr.ioc_version != LIBCFS_IOCTL_VERSION2) {
		CERROR("libcfs ioctl: version mismatch expected %#x, got %#x\n",
		       LIBCFS_IOCTL_VERSION, hdr.ioc_version);
		return -EINVAL;
	}

	if (hdr.ioc_len < sizeof(struct libcfs_ioctl_data)) {
		CERROR("libcfs ioctl: user buffer too small for ioctl\n");
		return -EINVAL;
	}

	if (hdr.ioc_len > LIBCFS_IOC_DATA_MAX) {
		CERROR("libcfs ioctl: user buffer is too large %d/%d\n",
		       hdr.ioc_len, LIBCFS_IOC_DATA_MAX);
		return -EINVAL;
	}

	LIBCFS_ALLOC(*hdr_pp, hdr.ioc_len);
	if (!*hdr_pp)
		return -ENOMEM;

	if (copy_from_user(*hdr_pp, uhdr, hdr.ioc_len)) {
		LIBCFS_FREE(*hdr_pp, hdr.ioc_len);
		err = -EFAULT;
	}
	return err;
}

static long
libcfs_psdev_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct cfs_psdev_file	 pfile;

	if (!capable(CAP_SYS_ADMIN))
		return -EACCES;

	if (_IOC_TYPE(cmd) != IOC_LIBCFS_TYPE ||
	    _IOC_NR(cmd) < IOC_LIBCFS_MIN_NR  ||
	    _IOC_NR(cmd) > IOC_LIBCFS_MAX_NR) {
		CDEBUG(D_IOCTL, "invalid ioctl ( type %d, nr %d, size %d )\n",
		       _IOC_TYPE(cmd), _IOC_NR(cmd), _IOC_SIZE(cmd));
		return -EINVAL;
	}

	return libcfs_ioctl(&pfile, cmd, (void __user *)arg);
}

static const struct file_operations libcfs_fops = {
	.owner		= THIS_MODULE,
	.unlocked_ioctl	= libcfs_psdev_ioctl,
};

struct miscdevice libcfs_dev = {
	.minor = LNET_MINOR,
	.name = "lnet",
	.fops = &libcfs_fops,
};
