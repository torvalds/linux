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

int libcfs_ioctl_getdata(char *buf, char *end, void *arg)
{
	struct libcfs_ioctl_hdr   *hdr;
	struct libcfs_ioctl_data  *data;
	int orig_len;

	hdr = (struct libcfs_ioctl_hdr *)buf;
	data = (struct libcfs_ioctl_data *)buf;

	if (copy_from_user(buf, arg, sizeof(*hdr)))
		return -EFAULT;

	if (hdr->ioc_version != LIBCFS_IOCTL_VERSION) {
		CERROR("PORTALS: version mismatch kernel vs application\n");
		return -EINVAL;
	}

	if (hdr->ioc_len >= end - buf) {
		CERROR("PORTALS: user buffer exceeds kernel buffer\n");
		return -EINVAL;
	}

	if (hdr->ioc_len < sizeof(struct libcfs_ioctl_data)) {
		CERROR("PORTALS: user buffer too small for ioctl\n");
		return -EINVAL;
	}

	orig_len = hdr->ioc_len;
	if (copy_from_user(buf, arg, hdr->ioc_len))
		return -EFAULT;
	if (orig_len != data->ioc_len)
		return -EINVAL;

	if (libcfs_ioctl_is_invalid(data)) {
		CERROR("PORTALS: ioctl not correctly formatted\n");
		return -EINVAL;
	}

	if (data->ioc_inllen1)
		data->ioc_inlbuf1 = &data->ioc_bulk[0];

	if (data->ioc_inllen2)
		data->ioc_inlbuf2 = &data->ioc_bulk[0] +
			cfs_size_round(data->ioc_inllen1);

	return 0;
}

int libcfs_ioctl_popdata(void *arg, void *data, int size)
{
	if (copy_to_user((char *)arg, data, size))
		return -EFAULT;
	return 0;
}

static int
libcfs_psdev_open(struct inode *inode, struct file *file)
{
	struct libcfs_device_userstate **pdu = NULL;
	int    rc = 0;

	if (!inode)
		return -EINVAL;
	pdu = (struct libcfs_device_userstate **)&file->private_data;
	if (libcfs_psdev_ops.p_open != NULL)
		rc = libcfs_psdev_ops.p_open(0, (void *)pdu);
	else
		return -EPERM;
	return rc;
}

/* called when closing /dev/device */
static int
libcfs_psdev_release(struct inode *inode, struct file *file)
{
	struct libcfs_device_userstate *pdu;
	int    rc = 0;

	if (!inode)
		return -EINVAL;
	pdu = file->private_data;
	if (libcfs_psdev_ops.p_close != NULL)
		rc = libcfs_psdev_ops.p_close(0, (void *)pdu);
	else
		rc = -EPERM;
	return rc;
}

static long libcfs_ioctl(struct file *file,
			 unsigned int cmd, unsigned long arg)
{
	struct cfs_psdev_file	 pfile;
	int    rc = 0;

	if (!capable(CAP_SYS_ADMIN))
		return -EACCES;

	if (_IOC_TYPE(cmd) != IOC_LIBCFS_TYPE ||
	     _IOC_NR(cmd) < IOC_LIBCFS_MIN_NR  ||
	     _IOC_NR(cmd) > IOC_LIBCFS_MAX_NR) {
		CDEBUG(D_IOCTL, "invalid ioctl ( type %d, nr %d, size %d )\n",
		       _IOC_TYPE(cmd), _IOC_NR(cmd), _IOC_SIZE(cmd));
		return -EINVAL;
	}

	/* Handle platform-dependent IOC requests */
	switch (cmd) {
	case IOC_LIBCFS_PANIC:
		if (!capable(CFS_CAP_SYS_BOOT))
			return -EPERM;
		panic("debugctl-invoked panic");
		return 0;
	case IOC_LIBCFS_MEMHOG:
		if (!capable(CFS_CAP_SYS_ADMIN))
			return -EPERM;
		/* go thought */
	}

	pfile.off = 0;
	pfile.private_data = file->private_data;
	if (libcfs_psdev_ops.p_ioctl != NULL)
		rc = libcfs_psdev_ops.p_ioctl(&pfile, cmd, (void *)arg);
	else
		rc = -EPERM;
	return rc;
}

static const struct file_operations libcfs_fops = {
	.unlocked_ioctl	= libcfs_ioctl,
	.open		= libcfs_psdev_open,
	.release	= libcfs_psdev_release,
};

struct miscdevice libcfs_dev = {
	.minor = LNET_MINOR,
	.name = "lnet",
	.fops = &libcfs_fops,
};
