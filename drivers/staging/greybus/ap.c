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
#include "greybus_manifest.h"
#include "greybus.h"

struct ap_msg {
	u8 *data;
	size_t size;
	struct greybus_host_device *hd;
	struct work_struct event;
};

static struct workqueue_struct *ap_workqueue;

static struct svc_msg *svc_msg_alloc(enum svc_function_type type)
{
	struct svc_msg *svc_msg;

	svc_msg = kzalloc((sizeof *svc_msg), GFP_KERNEL);
	if (!svc_msg)
		return NULL;

	// FIXME - verify we are only sending message types we should be
	svc_msg->header.type = type;
	return svc_msg;
}

static void svc_msg_free(struct svc_msg *svc_msg)
{
	kfree(svc_msg);
}

static int svc_msg_send(struct svc_msg *svc_msg, struct greybus_host_device *hd)
{
	int retval;

	// FIXME - Do we need to do more than just pass it to the hd and then
	// free it?
	retval = hd->driver->send_svc_msg(svc_msg, hd);

	svc_msg_free(svc_msg);
	return retval;
}


static void svc_handshake(struct svc_function_handshake *handshake,
			  struct greybus_host_device *hd)
{
	struct svc_msg *svc_msg;

	/* A new SVC communication channel, let's verify it was for us */
	if (handshake->handshake_type != SVC_HANDSHAKE_SVC_HELLO) {
		/* we don't know what to do with this, log it and return */
		dev_dbg(&hd->dev, "received invalid handshake type %d\n",
			handshake->handshake_type);
		return;
	}

	/* Send back a AP_HELLO message */
	svc_msg = svc_msg_alloc(SVC_FUNCTION_HANDSHAKE);
	if (!svc_msg)
		return;

	svc_msg->handshake.handshake_type = SVC_HANDSHAKE_AP_HELLO;
	svc_msg_send(svc_msg, hd);
}

static void svc_management(struct svc_function_unipro_management *management,
			   struct greybus_host_device *hd)
{
	/* What?  An AP should not get this message */
	dev_err(&hd->dev, "Got an svc management message???\n");
}

static void svc_hotplug(struct svc_function_hotplug *hotplug,
			struct greybus_host_device *hd)
{
	u8 module_id = hotplug->module_id;

	switch (hotplug->hotplug_event) {
	case SVC_HOTPLUG_EVENT:
		dev_dbg(&hd->dev, "module id %d added\n", module_id);
		// FIXME - add the module to the system
		break;

	case SVC_HOTUNPLUG_EVENT:
		dev_dbg(&hd->dev, "module id %d removed\n", module_id);
		// FIXME - remove the module from the system
		break;

	default:
		dev_err(&hd->dev, "received invalid hotplug message type %d\n",
			hotplug->hotplug_event);
		break;
	}
}

static void svc_ddb(struct svc_function_ddb *ddb,
		    struct greybus_host_device *hd)
{
	/* What?  An AP should not get this message */
	dev_err(&hd->dev, "Got an svc DDB message???\n");
}

static void svc_power(struct svc_function_power *power,
		      struct greybus_host_device *hd)
{
	u8 module_id = power->module_id;

	if (power->power_type != SVC_POWER_BATTERY_STATUS) {
		dev_err(&hd->dev, "received invalid power type %d\n",
			power->power_type);
		return;
	}

	dev_dbg(&hd->dev, "power status for module id %d is %d\n",
		module_id, power->status.status);

	// FIXME - do something with the power information, like update our
	// battery information...
}

static void svc_epm(struct svc_function_epm *epm,
		    struct greybus_host_device *hd)
{
	/* What?  An AP should not get this message */
	dev_err(&hd->dev, "Got an EPM message???\n");
}

static void svc_suspend(struct svc_function_suspend *suspend,
			struct greybus_host_device *hd)
{
	/* What?  An AP should not get this message */
	dev_err(&hd->dev, "Got an suspend message???\n");
}

static struct svc_msg *convert_ap_message(struct ap_msg *ap_msg)
{
	struct svc_msg *svc_msg;

	// FIXME - validate message, right now we are trusting the size and data
	// from the AP, what could go wrong?  :)
	// for now, just cast the pointer and run away...

	svc_msg = (struct svc_msg *)ap_msg->data;

	/* Verify the version is something we can handle with this code */
	if ((svc_msg->header.version_major != GREYBUS_VERSION_MAJOR) &&
	    (svc_msg->header.version_minor != GREYBUS_VERSION_MINOR))
		return NULL;

	return svc_msg;
}

static void ap_process_event(struct work_struct *work)
{
	struct svc_msg *svc_msg;
	struct greybus_host_device *hd;
	struct ap_msg *ap_msg;

	ap_msg = container_of(work, struct ap_msg, event);
	hd = ap_msg->hd;

	/* Turn the "raw" data into a real message */
	svc_msg = convert_ap_message(ap_msg);
	if (!svc_msg) {
		// FIXME log an error???
		return;
	}

	/* Look at the message to figure out what to do with it */
	switch (svc_msg->header.type) {
	case SVC_FUNCTION_HANDSHAKE:
		svc_handshake(&svc_msg->handshake, hd);
		break;
	case SVC_FUNCTION_UNIPRO_NETWORK_MANAGEMENT:
		svc_management(&svc_msg->management, hd);
		break;
	case SVC_FUNCTION_HOTPLUG:
		svc_hotplug(&svc_msg->hotplug, hd);
		break;
	case SVC_FUNCTION_DDB:
		svc_ddb(&svc_msg->ddb, hd);
		break;
	case SVC_FUNCTION_POWER:
		svc_power(&svc_msg->power, hd);
		break;
	case SVC_FUNCTION_EPM:
		svc_epm(&svc_msg->epm, hd);
		break;
	case SVC_FUNCTION_SUSPEND:
		svc_suspend(&svc_msg->suspend, hd);
		break;
	default:
		dev_err(&hd->dev, "received invalid SVC message type %d\n",
			svc_msg->header.type);
	}

	/* clean the message up */
	kfree(ap_msg->data);
	kfree(ap_msg);
}

int gb_new_ap_msg(u8 *data, int size, struct greybus_host_device *hd)
{
	struct ap_msg *ap_msg;

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

	INIT_WORK(&ap_msg->event, ap_process_event);
	queue_work(ap_workqueue, &ap_msg->event);

	return 0;
}
EXPORT_SYMBOL_GPL(gb_new_ap_msg);

int gb_thread_init(void)
{
	ap_workqueue = alloc_workqueue("greybus_ap", 0, 1);
	if (!ap_workqueue)
		return -ENOMEM;

	return 0;
}

void gb_thread_destroy(void)
{
	destroy_workqueue(ap_workqueue);
}


