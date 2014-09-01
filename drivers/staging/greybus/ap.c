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
#include "greybus.h"

/*
 * AP <-> SVC message structure format:
 *
 * 
 *
 */
enum svc_function_type {
	SVC_FUNCTION_HANDSHAKE			= 0x00,
	SVC_FUNCTION_UNIPRO_NETWORK_MANAGEMENT	= 0x01,
	SVC_FUNCTION_HOTPLUG			= 0x02,
	SVC_FUNCTION_DDB			= 0x03,
	SVC_FUNCTION_POWER			= 0x04,
	SVC_FUNCTION_EPM			= 0x05,
	SVC_FUNCTION_SUSPEND			= 0x06,
};

struct svc_msg_header {
	u8	function;
	u8	type;		/* enum svc_function_type */
	u8	version_major;
	u8	version_minor;
	u16	payload_length;
};

enum svc_function_handshake_type {
	SVC_HANDSHAKE_SVC_HELLO		= 0x00,
	SVC_HANDSHAKE_AP_HELLO		= 0x01,
	SVC_HANDSHAKE_MODULE_HELLO	= 0x02,
};

struct svc_function_handshake {
	u8	handshake_type;	/* enum svc_function_handshake_type */
};

struct svc_function_unipro_set_route {
	u8	source_device_id;
	u8	source_cport_id;
	u8	destination_device_id;
	u8	destination_cport_id;
};

struct svc_function_unipro_link_up {
	u8	device_id;
};

enum svc_function_management_event {
	SVC_MANAGEMENT_SET_ROUTE	= 0x00,
	SVC_MANAGEMENT_LINK_UP		= 0x01,
};

struct svc_function_unipro_management {
	u8	management_packet_type;	/* enum svc_function_management_event */
	union {
		struct svc_function_unipro_set_route	set_route;
		struct svc_function_unipro_link_up	link_up;
	};
};

enum svc_function_hotplug_event {
	SVC_HOTPLUG_EVENT	= 0x00,
	SVC_HOTUNPLUG_EVENT	= 0x01,
};

struct svc_function_hotplug {
	u8	hotplug_event;	/* enum svc_function_hotplug_event */
	u8	device_id;
};

enum svc_function_ddb_type {
	SVC_DDB_GET		= 0x00,
	SVC_DDB_RESPONSE	= 0x01,
};

struct svc_function_ddb_get {
	u8	device_id;
	u8	message_id;
};

struct svc_function_ddb_response {
	u8	device_id;
	u8	message_id;
	u16	descriptor_length;
	u8	ddb[0];
};

struct svc_function_ddb {
	u8	ddb_type;	/* enum svc_function_ddb_type */
	union {
		struct svc_function_ddb_get		ddb_get;
		struct svc_function_ddb_response	ddb_response;
	};
};

enum svc_function_power_type {
	SVC_POWER_BATTERY_STATUS		= 0x00,
	SVC_POWER_BATTERY_STATUS_REQUEST	= 0x01,
};

enum svc_function_battery_status {
	SVC_BATTERY_UNKNOWN		= 0x00,
	SVC_BATTERY_CHARGING		= 0x01,
	SVC_BATTERY_DISCHARGING		= 0x02,
	SVC_BATTERY_NOT_CHARGING	= 0x03,
	SVC_BATTERY_FULL		= 0x04,
};

struct svc_function_power_battery_status {
	u16	charge_full;
	u16	charge_now;
	u8	status;	/* enum svc_function_battery_status */
};

struct svc_function_power_battery_status_request {

};

struct svc_function_power {
	u8	power_type;	/* enum svc_function_power_type */
	union {
		struct svc_function_power_battery_status		status;
		struct svc_function_power_battery_status_request	request;
	};
};

struct svc_msg {
	struct svc_msg_header	header;
	union {
		struct svc_function_handshake		handshake;
		struct svc_function_unipro_management	management;
		struct svc_function_hotplug		hotplug;
		struct svc_function_ddb			ddb;
		u8				data[0];
	};
};


struct ap_msg {
	u8 *data;
	int size;
	struct list_head list;
};

static LIST_HEAD(ap_msg_list);
static spinlock_t ap_msg_list_lock;
static struct task_struct *ap_thread;
static wait_queue_head_t ap_wait;

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

	while (!kthread_should_stop()) {
		wait_event_interruptible(ap_wait, kthread_should_stop());

		if (kthread_should_stop())
			break;

		/* Get some data off of the ap list and process it */
		ap_msg = get_ap_msg();
		if (!ap_msg)
			continue;

		// FIXME - process the message

		/* clean the message up */
		kfree(ap_msg->data);
		kfree(ap_msg);
	}
	return 0;
}

int gb_new_ap_msg(u8 *data, int size)
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

	spin_lock_irqsave(&ap_msg_list_lock, flags);
	list_add(&ap_msg->list, &ap_msg_list);
	spin_unlock_irqrestore(&ap_msg_list_lock, flags);

	/* kick our thread to handle the message */
	wake_up_interruptible(&ap_wait);

	return 0;
}

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


