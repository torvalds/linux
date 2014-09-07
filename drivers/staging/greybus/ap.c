/*
 * Greybus "AP" message loop handling
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
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/kthread.h>
#include <linux/device.h>
#include "svc_msg.h"
#include "greybus_desc.h"
#include "greybus.h"

struct ap_msg {
	u8 *data;
	size_t size;
	struct greybus_host_device *hd;
	struct list_head list;
};

static LIST_HEAD(ap_msg_list);
static spinlock_t ap_msg_list_lock;
static struct task_struct *ap_thread;
static wait_queue_head_t ap_wait;


static struct svc_msg *convert_ap_message(struct ap_msg *ap_msg)
{
	struct svc_msg *svc_msg;

	// FIXME - validate message, right now we are trusting the size and data
	// from the AP, what could go wrong?  :)
	// for now, just cast the pointer and run away...

	svc_msg = (struct svc_msg *)ap_msg->data;

	// FIXME - put in correct version numbers
	if ((svc_msg->header.version_major != 0x00) &&
	    (svc_msg->header.version_minor != 0x00))
		return NULL;

	return svc_msg;
}



static struct ap_msg *get_ap_msg(void)
{
	struct ap_msg *ap_msg;
	unsigned long flags;

	spin_lock_irqsave(&ap_msg_list_lock, flags);

	ap_msg = list_first_entry_or_null(&ap_msg_list, struct ap_msg, list);
	if (ap_msg != NULL)
		list_del(&ap_msg->list);
	spin_unlock_irqrestore(&ap_msg_list_lock, flags);

	return ap_msg;
}

static int ap_process_loop(void *data)
{
	struct ap_msg *ap_msg;
	struct svc_msg *svc_msg;

	while (!kthread_should_stop()) {
		wait_event_interruptible(ap_wait, kthread_should_stop());

		if (kthread_should_stop())
			break;

		/* Get some data off of the ap list and process it */
		ap_msg = get_ap_msg();
		if (!ap_msg)
			continue;

		/* Turn the "raw" data into a real message */
		svc_msg = convert_ap_message(ap_msg);
		if (svc_msg) {
			/* Pass the message to the host controller */
			ap_msg->hd->driver->ap_msg(svc_msg, ap_msg->hd);
		}

		/* clean the message up */
		kfree(ap_msg->data);
		kfree(ap_msg);
	}
	return 0;
}

int gb_new_ap_msg(u8 *data, int size, struct greybus_host_device *hd)
{
	struct ap_msg *ap_msg;
	unsigned long flags;

	/*
	 * Totally naive copy the message into a new structure that we slowly
	 * create and add it to the list.  Let's get this working, the odds of
	 * this being any "slow path" for AP messages is really low at this
	 * point in time, but you never know, so this comment is here to point
	 * out that maybe we should use a slab allocator, or even just not copy
	 * the data, but use it directly and force the urbs to be "new" each
	 * time.
	 */

	/* Note - this can, and will, be called in interrupt context. */
	ap_msg = kmalloc(sizeof(*ap_msg), GFP_ATOMIC);
	if (!ap_msg)
		return -ENOMEM;
	ap_msg->data = kmalloc(size, GFP_ATOMIC);
	if (!ap_msg->data) {
		kfree(ap_msg);
		return -ENOMEM;
	}
	memcpy(ap_msg->data, data, size);
	ap_msg->size = size;
	ap_msg->hd = hd;

	spin_lock_irqsave(&ap_msg_list_lock, flags);
	list_add(&ap_msg->list, &ap_msg_list);
	spin_unlock_irqrestore(&ap_msg_list_lock, flags);

	/* kick our thread to handle the message */
	wake_up_interruptible(&ap_wait);

	return 0;
}
EXPORT_SYMBOL_GPL(gb_new_ap_msg);


int gb_thread_init(void)
{
	init_waitqueue_head(&ap_wait);
	spin_lock_init(&ap_msg_list_lock);

	ap_thread = kthread_run(ap_process_loop, NULL, "greybus_ap");
	if (IS_ERR(ap_thread))
		return PTR_ERR(ap_thread);

	return 0;
}

void gb_thread_destroy(void)
{
	kthread_stop(ap_thread);
}


