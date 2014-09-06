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


/**
 * greybus_alloc_gbuf - allocate a greybus buffer
 *
 * @gdev: greybus device that wants to allocate this
 * @cport: cport to send the data to
 * @complete: callback when the gbuf is finished with
 * @size: size of the buffer
 * @gfp_mask: allocation mask
 * @context: context added to the gbuf by the driver
 *
 * TODO: someday it will be nice to handle DMA, but for now, due to the
 * architecture we are stuck with, the greybus core has to allocate the buffer
 * that the driver can then fill up with the data to be sent out.  Curse
 * hardware designers for this issue...
 */
struct gbuf *greybus_alloc_gbuf(struct greybus_device *gdev,
				struct gdev_cport *cport,
				gbuf_complete_t complete,
				unsigned int size,
				gfp_t gfp_mask,
				void *context)
{
	struct gbuf *gbuf;
	int retval;

	/*
	 * change this to a slab allocation if it's too slow, but for now, let's
	 * be dumb and simple.
	 */
	gbuf = kzalloc(sizeof(*gbuf), gfp_mask);
	if (!gbuf)
		return NULL;

	kref_init(&gbuf->kref);
	gbuf->gdev = gdev;
	gbuf->cport = cport;
	gbuf->complete = complete;
	gbuf->context = context;

	/* Host controller specific allocation for the actual buffer */
	retval = gbuf->gdev->hd->driver->alloc_gbuf(gbuf, size, gfp_mask);
	if (retval) {
		kfree(gbuf);
		return NULL;
	}

	return gbuf;
}
EXPORT_SYMBOL_GPL(greybus_alloc_gbuf);

static DEFINE_MUTEX(gbuf_mutex);

static void free_gbuf(struct kref *kref)
{
	struct gbuf *gbuf = container_of(kref, struct gbuf, kref);

	/* let the host controller free what it wants to */
	gbuf->gdev->hd->driver->free_gbuf(gbuf);

	kfree(gbuf);
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



int greybus_submit_gbuf(struct gbuf *gbuf, gfp_t mem_flags)
{
	return -ENOMEM;
}

int greybus_kill_gbuf(struct gbuf *gbuf)
{
	return -ENOMEM;
}



