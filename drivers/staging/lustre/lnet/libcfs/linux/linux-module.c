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

#include <linux/libcfs/libcfs.h>

#define LNET_MINOR 240

static inline size_t libcfs_ioctl_packlen(struct libcfs_ioctl_data *data)
{
	size_t len = sizeof(*data);

	len += cfs_size_round(data->ioc_inllen1);
	len += cfs_size_round(data->ioc_inllen2);
	return len;
}

static inline bool libcfs_ioctl_is_invalid(struct libcfs_ioctl_data *data)
{
	if (data->ioc_hdr.ioc_len > BIT(30)) {
		CERROR("LIBCFS ioctl: ioc_len larger than 1<<30\n");
		return true;
	}
	if (data->ioc_inllen1 > BIT(30)) {
		CERROR("LIBCFS ioctl: ioc_inllen1 larger than 1<<30\n");
		return true;
	}
	if (data->ioc_inllen2 > BIT(30)) {
		CERROR("LIBCFS ioctl: ioc_inllen2 larger than 1<<30\n");
		return true;
	}
	if (data->ioc_inlbuf1 && !data->ioc_inllen1) {
		CERROR("LIBCFS ioctl: inlbuf1 pointer but 0 length\n");
		return true;
	}
	if (data->ioc_inlbuf2 && !data->ioc_inllen2) {
		CERROR("LIBCFS ioctl: inlbuf2 pointer but 0 length\n");
		return true;
	}
	if (data->ioc_pbuf1 && !data->ioc_plen1) {
		CERROR("LIBCFS ioctl: pbuf1 pointer but 0 length\n");
		return true;
	}
	if (data->ioc_pbuf2 && !data->ioc_plen2) {
		CERROR("LIBCFS ioctl: pbuf2 pointer but 0 length\n");
		return true;
	}
	if (data->ioc_plen1 && !data->ioc_pbuf1) {
		CERROR("LIBCFS ioctl: plen1 nonzero but no pbuf1 pointer\n");
		return true;
	}
	if (data->ioc_plen2 && !data->ioc_pbuf2) {
		CERROR("LIBCFS ioctl: plen2 nonzero but no pbuf2 pointer\n");
		return true;
	}
	if ((u32)libcfs_ioctl_packlen(data) != data->ioc_hdr.ioc_len) {
		CERROR("LIBCFS ioctl: packlen != ioc_len\n");
		return true;
	}
	if (data->ioc_inllen1 &&
	    data->ioc_bulk[data->ioc_inllen1 - 1] != '\0') {
		CERROR("LIBCFS ioctl: inlbuf1 not 0 terminated\n");
		return true;
	}
	if (data->ioc_inllen2 &&
	    data->ioc_bulk[cfs_size_round(data->ioc_inllen1) +
			   data->ioc_inllen2 - 1] != '\0') {
		CERROR("LIBCFS ioctl: inlbuf2 not 0 terminated\n");
		return true;
	}
	return false;
}

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
	int err;

	if (copy_from_user(&hdr, uhdr, sizeof(hdr)))
		return -EFAULT;

	if (hdr.ioc_version != LIBCFS_IOCTL_VERSION &&
	    hdr.ioc_version != LIBCFS_IOCTL_VERSION2) {
		CERROR("libcfs ioctl: version mismatch expected %#x, got %#x\n",
		       LIBCFS_IOCTL_VERSION, hdr.ioc_version);
		return -EINVAL;
	}

	if (hdr.ioc_len < sizeof(hdr)) {
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
		err = -EFAULT;
		goto free;
	}

	if ((*hdr_pp)->ioc_version != hdr.ioc_version ||
	    (*hdr_pp)->ioc_len != hdr.ioc_len) {
		err = -EINVAL;
		goto free;
	}

	return 0;

free:
	LIBCFS_FREE(*hdr_pp, hdr.ioc_len);
	return err;
}

static long
libcfs_psdev_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	if (!capable(CAP_SYS_ADMIN))
		return -EACCES;

	if (_IOC_TYPE(cmd) != IOC_LIBCFS_TYPE ||
	    _IOC_NR(cmd) < IOC_LIBCFS_MIN_NR  ||
	    _IOC_NR(cmd) > IOC_LIBCFS_MAX_NR) {
		CDEBUG(D_IOCTL, "invalid ioctl ( type %d, nr %d, size %d )\n",
		       _IOC_TYPE(cmd), _IOC_NR(cmd), _IOC_SIZE(cmd));
		return -EINVAL;
	}

	return libcfs_ioctl(cmd, (void __user *)arg);
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
