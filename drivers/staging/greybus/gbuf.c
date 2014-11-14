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
#include <linux/kref.h>
#include <linux/device.h>
#include <linux/slab.h>

#include "greybus.h"

static struct kmem_cache *gbuf_head_cache;

/**
 * greybus_alloc_gbuf - allocate a greybus buffer
 *
 * @gmod: greybus device that wants to allocate this
 * @cport: cport to send the data to
 * @complete: callback when the gbuf is finished with
 * @size: size of the buffer
 * @gfp_mask: allocation mask
 *
 * TODO: someday it will be nice to handle DMA, but for now, due to the
 * architecture we are stuck with, the greybus core has to allocate the buffer
 * that the driver can then fill up with the data to be sent out.  Curse
 * hardware designers for this issue...
 */
struct gbuf *greybus_alloc_gbuf(struct gb_operation *operation,
				unsigned int size,
				bool outbound,
				gfp_t gfp_mask)
{
	struct greybus_host_device *hd = operation->connection->hd;
	struct gbuf *gbuf;
	int retval;

	gbuf = kmem_cache_zalloc(gbuf_head_cache, gfp_mask);
	if (!gbuf)
		return NULL;

	kref_init(&gbuf->kref);
	gbuf->operation = operation;
	gbuf->outbound = outbound;
	gbuf->status = -EBADR;	/* Initial value--means "never set" */

	/* Host controller specific allocation for the actual buffer */
	retval = hd->driver->alloc_gbuf_data(gbuf, size, gfp_mask);
	if (retval) {
		kmem_cache_free(gbuf_head_cache, gbuf);
		return NULL;
	}

	return gbuf;
}
EXPORT_SYMBOL_GPL(greybus_alloc_gbuf);

static DEFINE_MUTEX(gbuf_mutex);

static void free_gbuf(struct kref *kref)
{
	struct gbuf *gbuf = container_of(kref, struct gbuf, kref);
	struct greybus_host_device *hd = gbuf->operation->connection->hd;

	hd->driver->free_gbuf_data(gbuf);

	kmem_cache_free(gbuf_head_cache, gbuf);
	mutex_unlock(&gbuf_mutex);
}

void greybus_free_gbuf(struct gbuf *gbuf)
{
	/* drop the reference count and get out of here */
	kref_put_mutex(&gbuf->kref, free_gbuf, &gbuf_mutex);
}
EXPORT_SYMBOL_GPL(greybus_free_gbuf);

struct gbuf *greybus_get_gbuf(struct gbuf *gbuf)
{
	mutex_lock(&gbuf_mutex);
	kref_get(&gbuf->kref);
	mutex_unlock(&gbuf_mutex);
	return gbuf;
}
EXPORT_SYMBOL_GPL(greybus_get_gbuf);

int greybus_submit_gbuf(struct gbuf *gbuf, gfp_t gfp_mask)
{
	struct greybus_host_device *hd = gbuf->operation->connection->hd;

	gbuf->status = -EINPROGRESS;

	return hd->driver->submit_gbuf(gbuf, gfp_mask);
}

void greybus_kill_gbuf(struct gbuf *gbuf)
{
	struct greybus_host_device *hd = gbuf->operation->connection->hd;

	if (gbuf->status != -EINPROGRESS)
		return;

	hd->driver->kill_gbuf(gbuf);
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

int gb_gbuf_init(void)
{
	gbuf_head_cache = kmem_cache_create("gbuf_head_cache",
					    sizeof(struct gbuf), 0, 0, NULL);
	return 0;
}

void gb_gbuf_exit(void)
{
	kmem_cache_destroy(gbuf_head_cache);
	gbuf_head_cache = NULL;
}
