/*
 * Copyright 2013 Tilera Corporation. All Rights Reserved.
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
 * Implementation of UART gxio calls.
 */

#include <linux/io.h>
#include <linux/errno.h>
#include <linux/module.h>

#include <gxio/uart.h>
#include <gxio/iorpc_globals.h>
#include <gxio/iorpc_uart.h>
#include <gxio/kiorpc.h>

int gxio_uart_init(gxio_uart_context_t *context, int uart_index)
{
	char file[32];
	int fd;

	snprintf(file, sizeof(file), "uart/%d/iorpc", uart_index);
	fd = hv_dev_open((HV_VirtAddr) file, 0);
	if (fd < 0) {
		if (fd >= GXIO_ERR_MIN && fd <= GXIO_ERR_MAX)
			return fd;
		else
			return -ENODEV;
	}

	context->fd = fd;

	/* Map in the MMIO space. */
	context->mmio_base = (void __force *)
		iorpc_ioremap(fd, HV_UART_MMIO_OFFSET, HV_UART_MMIO_SIZE);

	if (context->mmio_base == NULL) {
		hv_dev_close(context->fd);
		context->fd = -1;
		return -ENODEV;
	}

	return 0;
}

EXPORT_SYMBOL_GPL(gxio_uart_init);

int gxio_uart_destroy(gxio_uart_context_t *context)
{
	iounmap((void __force __iomem *)(context->mmio_base));
	hv_dev_close(context->fd);

	context->mmio_base = NULL;
	context->fd = -1;

	return 0;
}

EXPORT_SYMBOL_GPL(gxio_uart_destroy);

/* UART register write wrapper. */
void gxio_uart_write(gxio_uart_context_t *context, uint64_t offset,
		     uint64_t word)
{
	__gxio_mmio_write(context->mmio_base + offset, word);
}

EXPORT_SYMBOL_GPL(gxio_uart_write);

/* UART register read wrapper. */
uint64_t gxio_uart_read(gxio_uart_context_t *context, uint64_t offset)
{
	return __gxio_mmio_read(context->mmio_base + offset);
}

EXPORT_SYMBOL_GPL(gxio_uart_read);
