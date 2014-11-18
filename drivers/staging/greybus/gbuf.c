/*
 * Greybus gbuf handling
 *
 * Copyright 2014 Google Inc.
 *
 * Released under the GPLv2 only.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/types.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/device.h>

#include "greybus.h"

int greybus_submit_gbuf(struct gbuf *gbuf, gfp_t gfp_mask)
{
	gbuf->status = -EINPROGRESS;

	return gbuf->hd->driver->submit_gbuf(gbuf, gfp_mask);
}

void greybus_kill_gbuf(struct gbuf *gbuf)
{
	if (gbuf->status != -EINPROGRESS)
		return;

	gbuf->hd->driver->kill_gbuf(gbuf);
}

void greybus_cport_in(struct greybus_host_device *hd, u16 cport_id,
			u8 *data, size_t length)
{
	struct gb_connection *connection;

	connection = gb_hd_connection_find(hd, cport_id);
	if (!connection) {
		dev_err(hd->parent,
			"nonexistent connection (%zu bytes dropped)\n", length);
		return;
	}
	gb_connection_operation_recv(connection, data, length);
}
EXPORT_SYMBOL_GPL(greybus_cport_in);
