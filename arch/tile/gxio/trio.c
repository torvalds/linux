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
 * Implementation of trio gxio calls.
 */

#include <linux/errno.h>
#include <linux/io.h>
#include <linux/module.h>

#include <gxio/trio.h>
#include <gxio/iorpc_globals.h>
#include <gxio/iorpc_trio.h>
#include <gxio/kiorpc.h>

int gxio_trio_init(gxio_trio_context_t *context, unsigned int trio_index)
{
	char file[32];
	int fd;

	snprintf(file, sizeof(file), "trio/%d/iorpc", trio_index);
	fd = hv_dev_open((HV_VirtAddr) file, 0);
	if (fd < 0) {
		context->fd = -1;

		if (fd >= GXIO_ERR_MIN && fd <= GXIO_ERR_MAX)
			return fd;
		else
			return -ENODEV;
	}

	context->fd = fd;

	return 0;
}

EXPORT_SYMBOL_GPL(gxio_trio_init);
