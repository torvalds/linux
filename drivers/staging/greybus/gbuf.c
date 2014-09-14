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
#include <linux/workqueue.h>

#include "greybus.h"

static struct gbuf *__alloc_gbuf(struct greybus_device *gdev,
				struct gdev_cport *cport,
				gbuf_complete_t complete,
				gfp_t gfp_mask,
				void *context)
{
	struct gbuf *gbuf;

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

	return gbuf;
}

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

	gbuf = __alloc_gbuf(gdev, cport, complete, gfp_mask, context);
	if (!gbuf)
		return NULL;

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
	// FIXME - implement
	return -ENOMEM;
}

int greybus_kill_gbuf(struct gbuf *gbuf)
{
	// FIXME - implement
	return -ENOMEM;
}

/* Can be called in interrupt context, do the work and get out of here */
void greybus_gbuf_finished(struct gbuf *gbuf)
{
	// FIXME - implement
}
EXPORT_SYMBOL_GPL(greybus_gbuf_finished);

#define MAX_CPORTS	1024
struct gb_cport_handler {
	gbuf_complete_t handler;
	struct gdev_cport cport;
	struct greybus_device *gdev;
	void *context;
};

static struct gb_cport_handler cport_handler[MAX_CPORTS];
// FIXME - use a lock for this list of handlers, but really, for now we don't
// need it, we don't have a dynamic system...

int gb_register_cport_complete(struct greybus_device *gdev,
			       gbuf_complete_t handler, int cport,
			       void *context)
{
	if (cport_handler[cport].handler)
		return -EINVAL;
	cport_handler[cport].context = context;
	cport_handler[cport].gdev = gdev;
	cport_handler[cport].cport.number = cport;
	cport_handler[cport].handler = handler;
	return 0;
}

void gb_deregister_cport_complete(int cport)
{
	cport_handler[cport].handler = NULL;
}

struct cport_msg {
	struct gbuf *gbuf;
	struct work_struct event;
};

static struct workqueue_struct *cport_workqueue;

static void cport_process_event(struct work_struct *work)
{
	struct cport_msg *cm;
	struct gbuf *gbuf;

	cm = container_of(work, struct cport_msg, event);

	gbuf = cm->gbuf;

	/* call the gbuf handler */
	gbuf->complete(gbuf);

	/* free all the memory */
	kfree(gbuf->transfer_buffer);
	kfree(gbuf);
	kfree(cm);
}

void greybus_cport_in_data(struct greybus_host_device *hd, int cport, u8 *data,
			   size_t length)
{
	struct gb_cport_handler *ch;
	struct gbuf *gbuf;
	struct cport_msg *cm;

	/* first check to see if we have a cport handler for this cport */
	ch = &cport_handler[cport];
	if (!ch->handler) {
		/* Ugh, drop the data on the floor, after logging it... */
		dev_err(&hd->dev,
			"Received data for cport %d, but no handler!\n",
			cport);
		return;
	}

	gbuf = __alloc_gbuf(ch->gdev, &ch->cport, ch->handler, GFP_ATOMIC,
			ch->context);
	if (!gbuf) {
		/* Again, something bad went wrong, log it... */
		pr_err("can't allocate gbuf???\n");
		return;
	}
	gbuf->hdpriv = hd;

	/*
	 * FIXME:
	 * Very dumb copy data method for now, if this is slow (odds are it will
	 * be, we should move to a model where the hd "owns" all buffers, but we
	 * want something up and working first for now.
	 */
	gbuf->transfer_buffer = kmalloc(length, GFP_ATOMIC);
	if (!gbuf->transfer_buffer) {
		kfree(gbuf);
		return;
	}
	memcpy(gbuf->transfer_buffer, data, length);
	gbuf->transfer_buffer_length = length;

	/* Again with the slow allocate... */
	cm = kmalloc(sizeof(*cm), GFP_ATOMIC);

	/* Queue up the cport message to be handled in user context */
	cm->gbuf = gbuf;
	INIT_WORK(&cm->event, cport_process_event);
	queue_work(cport_workqueue, &cm->event);
}
EXPORT_SYMBOL_GPL(greybus_cport_in_data);

int gb_gbuf_init(void)
{
	cport_workqueue = alloc_workqueue("greybus_gbuf", 0, 1);
	if (!cport_workqueue)
		return -ENOMEM;

	return 0;
}

void gb_gbuf_exit(void)
{
	destroy_workqueue(cport_workqueue);
}
