/*
 *  Copyright (c) 2013, Microsoft Corporation.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms and conditions of the GNU General Public License,
 *  version 2, as published by the Free Software Foundation.
 *
 *  This program is distributed in the hope it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 *  more details.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/completion.h>
#include <linux/hyperv.h>
#include <linux/serio.h>
#include <linux/slab.h>

/*
 * Current version 1.0
 *
 */
#define SYNTH_KBD_VERSION_MAJOR 1
#define SYNTH_KBD_VERSION_MINOR	0
#define SYNTH_KBD_VERSION		(SYNTH_KBD_VERSION_MINOR | \
					 (SYNTH_KBD_VERSION_MAJOR << 16))


/*
 * Message types in the synthetic input protocol
 */
enum synth_kbd_msg_type {
	SYNTH_KBD_PROTOCOL_REQUEST = 1,
	SYNTH_KBD_PROTOCOL_RESPONSE = 2,
	SYNTH_KBD_EVENT = 3,
	SYNTH_KBD_LED_INDICATORS = 4,
};

/*
 * Basic message structures.
 */
struct synth_kbd_msg_hdr {
	__le32 type;
};

struct synth_kbd_msg {
	struct synth_kbd_msg_hdr header;
	char data[]; /* Enclosed message */
};

union synth_kbd_version {
	__le32 version;
};

/*
 * Protocol messages
 */
struct synth_kbd_protocol_request {
	struct synth_kbd_msg_hdr header;
	union synth_kbd_version version_requested;
};

#define PROTOCOL_ACCEPTED	BIT(0)
struct synth_kbd_protocol_response {
	struct synth_kbd_msg_hdr header;
	__le32 proto_status;
};

#define IS_UNICODE	BIT(0)
#define IS_BREAK	BIT(1)
#define IS_E0		BIT(2)
#define IS_E1		BIT(3)
struct synth_kbd_keystroke {
	struct synth_kbd_msg_hdr header;
	__le16 make_code;
	__le16 reserved0;
	__le32 info; /* Additional information */
};


#define HK_MAXIMUM_MESSAGE_SIZE 256

#define KBD_VSC_SEND_RING_BUFFER_SIZE		(10 * PAGE_SIZE)
#define KBD_VSC_RECV_RING_BUFFER_SIZE		(10 * PAGE_SIZE)

#define XTKBD_EMUL0     0xe0
#define XTKBD_EMUL1     0xe1
#define XTKBD_RELEASE   0x80


/*
 * Represents a keyboard device
 */
struct hv_kbd_dev {
	struct hv_device *hv_dev;
	struct serio *hv_serio;
	struct synth_kbd_protocol_request protocol_req;
	struct synth_kbd_protocol_response protocol_resp;
	/* Synchronize the request/response if needed */
	struct completion wait_event;
	spinlock_t lock; /* protects 'started' field */
	bool started;
};

static void hv_kbd_on_receive(struct hv_device *hv_dev,
			      struct synth_kbd_msg *msg, u32 msg_length)
{
	struct hv_kbd_dev *kbd_dev = hv_get_drvdata(hv_dev);
	struct synth_kbd_keystroke *ks_msg;
	unsigned long flags;
	u32 msg_type = __le32_to_cpu(msg->header.type);
	u32 info;
	u16 scan_code;

	switch (msg_type) {
	case SYNTH_KBD_PROTOCOL_RESPONSE:
		/*
		 * Validate the information provided by the host.
		 * If the host is giving us a bogus packet,
		 * drop the packet (hoping the problem
		 * goes away).
		 */
		if (msg_length < sizeof(struct synth_kbd_protocol_response)) {
			dev_err(&hv_dev->device,
				"Illegal protocol response packet (len: %d)\n",
				msg_length);
			break;
		}

		memcpy(&kbd_dev->protocol_resp, msg,
			sizeof(struct synth_kbd_protocol_response));
		complete(&kbd_dev->wait_event);
		break;

	case SYNTH_KBD_EVENT:
		/*
		 * Validate the information provided by the host.
		 * If the host is giving us a bogus packet,
		 * drop the packet (hoping the problem
		 * goes away).
		 */
		if (msg_length < sizeof(struct  synth_kbd_keystroke)) {
			dev_err(&hv_dev->device,
				"Illegal keyboard event packet (len: %d)\n",
				msg_length);
			break;
		}

		ks_msg = (struct synth_kbd_keystroke *)msg;
		info = __le32_to_cpu(ks_msg->info);

		/*
		 * Inject the information through the serio interrupt.
		 */
		spin_lock_irqsave(&kbd_dev->lock, flags);
		if (kbd_dev->started) {
			if (info & IS_E0)
				serio_interrupt(kbd_dev->hv_serio,
						XTKBD_EMUL0, 0);
			if (info & IS_E1)
				serio_interrupt(kbd_dev->hv_serio,
						XTKBD_EMUL1, 0);
			scan_code = __le16_to_cpu(ks_msg->make_code);
			if (info & IS_BREAK)
				scan_code |= XTKBD_RELEASE;

			serio_interrupt(kbd_dev->hv_serio, scan_code, 0);
		}
		spin_unlock_irqrestore(&kbd_dev->lock, flags);

		/*
		 * Only trigger a wakeup on key down, otherwise
		 * "echo freeze > /sys/power/state" can't really enter the
		 * state because the Enter-UP can trigger a wakeup at once.
		 */
		if (!(info & IS_BREAK))
			pm_wakeup_hard_event(&hv_dev->device);

		break;

	default:
		dev_err(&hv_dev->device,
			"unhandled message type %d\n", msg_type);
	}
}

static void hv_kbd_handle_received_packet(struct hv_device *hv_dev,
					  struct vmpacket_descriptor *desc,
					  u32 bytes_recvd,
					  u64 req_id)
{
	struct synth_kbd_msg *msg;
	u32 msg_sz;

	switch (desc->type) {
	case VM_PKT_COMP:
		break;

	case VM_PKT_DATA_INBAND:
		/*
		 * We have a packet that has "inband" data. The API used
		 * for retrieving the packet guarantees that the complete
		 * packet is read. So, minimally, we should be able to
		 * parse the payload header safely (assuming that the host
		 * can be trusted.  Trusting the host seems to be a
		 * reasonable assumption because in a virtualized
		 * environment there is not whole lot you can do if you
		 * don't trust the host.
		 *
		 * Nonetheless, let us validate if the host can be trusted
		 * (in a trivial way).  The interesting aspect of this
		 * validation is how do you recover if we discover that the
		 * host is not to be trusted? Simply dropping the packet, I
		 * don't think is an appropriate recovery.  In the interest
		 * of failing fast, it may be better to crash the guest.
		 * For now, I will just drop the packet!
		 */

		msg_sz = bytes_recvd - (desc->offset8 << 3);
		if (msg_sz <= sizeof(struct synth_kbd_msg_hdr)) {
			/*
			 * Drop the packet and hope
			 * the problem magically goes away.
			 */
			dev_err(&hv_dev->device,
				"Illegal packet (type: %d, tid: %llx, size: %d)\n",
				desc->type, req_id, msg_sz);
			break;
		}

		msg = (void *)desc + (desc->offset8 << 3);
		hv_kbd_on_receive(hv_dev, msg, msg_sz);
		break;

	default:
		dev_err(&hv_dev->device,
			"unhandled packet type %d, tid %llx len %d\n",
			desc->type, req_id, bytes_recvd);
		break;
	}
}

static void hv_kbd_on_channel_callback(void *context)
{
	struct hv_device *hv_dev = context;
	void *buffer;
	int bufferlen = 0x100; /* Start with sensible size */
	u32 bytes_recvd;
	u64 req_id;
	int error;

	buffer = kmalloc(bufferlen, GFP_ATOMIC);
	if (!buffer)
		return;

	while (1) {
		error = vmbus_recvpacket_raw(hv_dev->channel, buffer, bufferlen,
					     &bytes_recvd, &req_id);
		switch (error) {
		case 0:
			if (bytes_recvd == 0) {
				kfree(buffer);
				return;
			}

			hv_kbd_handle_received_packet(hv_dev, buffer,
						      bytes_recvd, req_id);
			break;

		case -ENOBUFS:
			kfree(buffer);
			/* Handle large packet */
			bufferlen = bytes_recvd;
			buffer = kmalloc(bytes_recvd, GFP_ATOMIC);
			if (!buffer)
				return;
			break;
		}
	}
}

static int hv_kbd_connect_to_vsp(struct hv_device *hv_dev)
{
	struct hv_kbd_dev *kbd_dev = hv_get_drvdata(hv_dev);
	struct synth_kbd_protocol_request *request;
	struct synth_kbd_protocol_response *response;
	u32 proto_status;
	int error;

	request = &kbd_dev->protocol_req;
	memset(request, 0, sizeof(struct synth_kbd_protocol_request));
	request->header.type = __cpu_to_le32(SYNTH_KBD_PROTOCOL_REQUEST);
	request->version_requested.version = __cpu_to_le32(SYNTH_KBD_VERSION);

	error = vmbus_sendpacket(hv_dev->channel, request,
				 sizeof(struct synth_kbd_protocol_request),
				 (unsigned long)request,
				 VM_PKT_DATA_INBAND,
				 VMBUS_DATA_PACKET_FLAG_COMPLETION_REQUESTED);
	if (error)
		return error;

	if (!wait_for_completion_timeout(&kbd_dev->wait_event, 10 * HZ))
		return -ETIMEDOUT;

	response = &kbd_dev->protocol_resp;
	proto_status = __le32_to_cpu(response->proto_status);
	if (!(proto_status & PROTOCOL_ACCEPTED)) {
		dev_err(&hv_dev->device,
			"synth_kbd protocol request failed (version %d)\n",
		        SYNTH_KBD_VERSION);
		return -ENODEV;
	}

	return 0;
}

static int hv_kbd_start(struct serio *serio)
{
	struct hv_kbd_dev *kbd_dev = serio->port_data;
	unsigned long flags;

	spin_lock_irqsave(&kbd_dev->lock, flags);
	kbd_dev->started = true;
	spin_unlock_irqrestore(&kbd_dev->lock, flags);

	return 0;
}

static void hv_kbd_stop(struct serio *serio)
{
	struct hv_kbd_dev *kbd_dev = serio->port_data;
	unsigned long flags;

	spin_lock_irqsave(&kbd_dev->lock, flags);
	kbd_dev->started = false;
	spin_unlock_irqrestore(&kbd_dev->lock, flags);
}

static int hv_kbd_probe(struct hv_device *hv_dev,
			const struct hv_vmbus_device_id *dev_id)
{
	struct hv_kbd_dev *kbd_dev;
	struct serio *hv_serio;
	int error;

	kbd_dev = kzalloc(sizeof(struct hv_kbd_dev), GFP_KERNEL);
	hv_serio = kzalloc(sizeof(struct serio), GFP_KERNEL);
	if (!kbd_dev || !hv_serio) {
		error = -ENOMEM;
		goto err_free_mem;
	}

	kbd_dev->hv_dev = hv_dev;
	kbd_dev->hv_serio = hv_serio;
	spin_lock_init(&kbd_dev->lock);
	init_completion(&kbd_dev->wait_event);
	hv_set_drvdata(hv_dev, kbd_dev);

	hv_serio->dev.parent  = &hv_dev->device;
	hv_serio->id.type = SERIO_8042_XL;
	hv_serio->port_data = kbd_dev;
	strlcpy(hv_serio->name, dev_name(&hv_dev->device),
		sizeof(hv_serio->name));
	strlcpy(hv_serio->phys, dev_name(&hv_dev->device),
		sizeof(hv_serio->phys));

	hv_serio->start = hv_kbd_start;
	hv_serio->stop = hv_kbd_stop;

	error = vmbus_open(hv_dev->channel,
			   KBD_VSC_SEND_RING_BUFFER_SIZE,
			   KBD_VSC_RECV_RING_BUFFER_SIZE,
			   NULL, 0,
			   hv_kbd_on_channel_callback,
			   hv_dev);
	if (error)
		goto err_free_mem;

	error = hv_kbd_connect_to_vsp(hv_dev);
	if (error)
		goto err_close_vmbus;

	serio_register_port(kbd_dev->hv_serio);

	device_init_wakeup(&hv_dev->device, true);

	return 0;

err_close_vmbus:
	vmbus_close(hv_dev->channel);
err_free_mem:
	kfree(hv_serio);
	kfree(kbd_dev);
	return error;
}

static int hv_kbd_remove(struct hv_device *hv_dev)
{
	struct hv_kbd_dev *kbd_dev = hv_get_drvdata(hv_dev);

	serio_unregister_port(kbd_dev->hv_serio);
	vmbus_close(hv_dev->channel);
	kfree(kbd_dev);

	hv_set_drvdata(hv_dev, NULL);

	return 0;
}

static const struct hv_vmbus_device_id id_table[] = {
	/* Keyboard guid */
	{ HV_KBD_GUID, },
	{ },
};

MODULE_DEVICE_TABLE(vmbus, id_table);

static struct  hv_driver hv_kbd_drv = {
	.name = KBUILD_MODNAME,
	.id_table = id_table,
	.probe = hv_kbd_probe,
	.remove = hv_kbd_remove,
};

static int __init hv_kbd_init(void)
{
	return vmbus_driver_register(&hv_kbd_drv);
}

static void __exit hv_kbd_exit(void)
{
	vmbus_driver_unregister(&hv_kbd_drv);
}

MODULE_LICENSE("GPL");
module_init(hv_kbd_init);
module_exit(hv_kbd_exit);
