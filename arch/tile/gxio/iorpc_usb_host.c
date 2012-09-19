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
#include "gxio/iorpc_usb_host.h"

struct cfg_interrupt_param {
	union iorpc_interrupt interrupt;
};

int gxio_usb_host_cfg_interrupt(gxio_usb_host_context_t * context, int inter_x,
				int inter_y, int inter_ipi, int inter_event)
{
	struct cfg_interrupt_param temp;
	struct cfg_interrupt_param *params = &temp;

	params->interrupt.kernel.x = inter_x;
	params->interrupt.kernel.y = inter_y;
	params->interrupt.kernel.ipi = inter_ipi;
	params->interrupt.kernel.event = inter_event;

	return hv_dev_pwrite(context->fd, 0, (HV_VirtAddr) params,
			     sizeof(*params), GXIO_USB_HOST_OP_CFG_INTERRUPT);
}

EXPORT_SYMBOL(gxio_usb_host_cfg_interrupt);

struct register_client_memory_param {
	HV_PTE pte;
	unsigned int flags;
};

int gxio_usb_host_register_client_memory(gxio_usb_host_context_t * context,
					 HV_PTE pte, unsigned int flags)
{
	struct register_client_memory_param temp;
	struct register_client_memory_param *params = &temp;

	params->pte = pte;
	params->flags = flags;

	return hv_dev_pwrite(context->fd, 0, (HV_VirtAddr) params,
			     sizeof(*params),
			     GXIO_USB_HOST_OP_REGISTER_CLIENT_MEMORY);
}

EXPORT_SYMBOL(gxio_usb_host_register_client_memory);

struct get_mmio_base_param {
	HV_PTE base;
};

int gxio_usb_host_get_mmio_base(gxio_usb_host_context_t * context, HV_PTE *base)
{
	int __result;
	struct get_mmio_base_param temp;
	struct get_mmio_base_param *params = &temp;

	__result =
	    hv_dev_pread(context->fd, 0, (HV_VirtAddr) params, sizeof(*params),
			 GXIO_USB_HOST_OP_GET_MMIO_BASE);
	*base = params->base;

	return __result;
}

EXPORT_SYMBOL(gxio_usb_host_get_mmio_base);

struct check_mmio_offset_param {
	unsigned long offset;
	unsigned long size;
};

int gxio_usb_host_check_mmio_offset(gxio_usb_host_context_t * context,
				    unsigned long offset, unsigned long size)
{
	struct check_mmio_offset_param temp;
	struct check_mmio_offset_param *params = &temp;

	params->offset = offset;
	params->size = size;

	return hv_dev_pwrite(context->fd, 0, (HV_VirtAddr) params,
			     sizeof(*params),
			     GXIO_USB_HOST_OP_CHECK_MMIO_OFFSET);
}

EXPORT_SYMBOL(gxio_usb_host_check_mmio_offset);
