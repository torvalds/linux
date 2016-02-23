/*
 * Copyright 2012 Tilera Corporation. All Rights Reserved.
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 *   NON INFRINGEMENT.  See the GNU General Public License for
 *   more details.
 */

/* This file is machine-generated; DO NOT EDIT! */
#include "gxio/iorpc_globals.h"

struct arm_pollfd_param {
	union iorpc_pollfd pollfd;
};

int __iorpc_arm_pollfd(int fd, int pollfd_cookie)
{
	struct arm_pollfd_param temp;
	struct arm_pollfd_param *params = &temp;

	params->pollfd.kernel.cookie = pollfd_cookie;

	return hv_dev_pwrite(fd, 0, (HV_VirtAddr) params, sizeof(*params),
			     IORPC_OP_ARM_POLLFD);
}

EXPORT_SYMBOL(__iorpc_arm_pollfd);

struct close_pollfd_param {
	union iorpc_pollfd pollfd;
};

int __iorpc_close_pollfd(int fd, int pollfd_cookie)
{
	struct close_pollfd_param temp;
	struct close_pollfd_param *params = &temp;

	params->pollfd.kernel.cookie = pollfd_cookie;

	return hv_dev_pwrite(fd, 0, (HV_VirtAddr) params, sizeof(*params),
			     IORPC_OP_CLOSE_POLLFD);
}

EXPORT_SYMBOL(__iorpc_close_pollfd);

struct get_mmio_base_param {
	HV_PTE base;
};

int __iorpc_get_mmio_base(int fd, HV_PTE *base)
{
	int __result;
	struct get_mmio_base_param temp;
	struct get_mmio_base_param *params = &temp;

	__result =
	    hv_dev_pread(fd, 0, (HV_VirtAddr) params, sizeof(*params),
			 IORPC_OP_GET_MMIO_BASE);
	*base = params->base;

	return __result;
}

EXPORT_SYMBOL(__iorpc_get_mmio_base);

struct check_mmio_offset_param {
	unsigned long offset;
	unsigned long size;
};

int __iorpc_check_mmio_offset(int fd, unsigned long offset, unsigned long size)
{
	struct check_mmio_offset_param temp;
	struct check_mmio_offset_param *params = &temp;

	params->offset = offset;
	params->size = size;

	return hv_dev_pwrite(fd, 0, (HV_VirtAddr) params, sizeof(*params),
			     IORPC_OP_CHECK_MMIO_OFFSET);
}

EXPORT_SYMBOL(__iorpc_check_mmio_offset);
