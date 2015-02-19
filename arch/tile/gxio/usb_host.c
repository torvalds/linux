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

/*
 *
 * Implementation of USB gxio calls.
 */

#include <linux/io.h>
#include <linux/errno.h>
#include <linux/module.h>

#include <gxio/iorpc_globals.h>
#include <gxio/iorpc_usb_host.h>
#include <gxio/kiorpc.h>
#include <gxio/usb_host.h>

int gxio_usb_host_init(gxio_usb_host_context_t *context, int usb_index,
		       int is_ehci)
{
	char file[32];
	int fd;

	if (is_ehci)
		snprintf(file, sizeof(file), "usb_host/%d/iorpc/ehci",
			 usb_index);
	else
		snprintf(file, sizeof(file), "usb_host/%d/iorpc/ohci",
			 usb_index);

	fd = hv_dev_open((HV_VirtAddr) file, 0);
	if (fd < 0) {
		if (fd >= GXIO_ERR_MIN && fd <= GXIO_ERR_MAX)
			return fd;
		else
			return -ENODEV;
	}

	context->fd = fd;

	// Map in the MMIO space.
	context->mmio_base =
		(void __force *)iorpc_ioremap(fd, 0, HV_USB_HOST_MMIO_SIZE);

	if (context->mmio_base == NULL) {
		hv_dev_close(context->fd);
		return -ENODEV;
	}

	return 0;
}

EXPORT_SYMBOL_GPL(gxio_usb_host_init);

int gxio_usb_host_destroy(gxio_usb_host_context_t *context)
{
	iounmap((void __force __iomem *)(context->mmio_base));
	hv_dev_close(context->fd);

	context->mmio_base = NULL;
	context->fd = -1;

	return 0;
}

EXPORT_SYMBOL_GPL(gxio_usb_host_destroy);

void *gxio_usb_host_get_reg_start(gxio_usb_host_context_t *context)
{
	return context->mmio_base;
}

EXPORT_SYMBOL_GPL(gxio_usb_host_get_reg_start);

size_t gxio_usb_host_get_reg_len(gxio_usb_host_context_t *context)
{
	return HV_USB_HOST_MMIO_SIZE;
}

EXPORT_SYMBOL_GPL(gxio_usb_host_get_reg_len);
