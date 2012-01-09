/*
 *  Copyright (c) 2009, Citrix Systems, Inc.
 *  Copyright (c) 2010, Microsoft Corporation.
 *  Copyright (c) 2011, Novell Inc.
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
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/workqueue.h>
#include <linux/sched.h>
#include <linux/wait.h>
#include <linux/input.h>
#include <linux/hid.h>
#include <linux/hiddev.h>
#include <linux/hyperv.h>


struct hv_input_dev_info {
	unsigned int size;
	unsigned short vendor;
	unsigned short product;
	unsigned short version;
	unsigned short reserved[11];
};

/* The maximum size of a synthetic input message. */
#define SYNTHHID_MAX_INPUT_REPORT_SIZE 16

/*
 * Current version
 *
 * History:
 * Beta, RC < 2008/1/22        1,0
 * RC > 2008/1/22              2,0
 */
#define SYNTHHID_INPUT_VERSION_MAJOR	2
#define SYNTHHID_INPUT_VERSION_MINOR	0
#define SYNTHHID_INPUT_VERSION		(SYNTHHID_INPUT_VERSION_MINOR | \
					 (SYNTHHID_INPUT_VERSION_MAJOR << 16))


#pragma pack(push, 1)
/*
 * Message types in the synthetic input protocol
 */
enum synthhid_msg_type {
	SYNTH_HID_PROTOCOL_REQUEST,
	SYNTH_HID_PROTOCOL_RESPONSE,
	SYNTH_HID_INITIAL_DEVICE_INFO,
	SYNTH_HID_INITIAL_DEVICE_INFO_ACK,
	SYNTH_HID_INPUT_REPORT,
	SYNTH_HID_MAX
};

/*
 * Basic message structures.
 */
struct synthhid_msg_hdr {
	enum synthhid_msg_type type;
	u32 size;
};

struct synthhid_msg {
	struct synthhid_msg_hdr header;
	char data[1]; /* Enclosed message */
};

union synthhid_version {
	struct {
		u16 minor_version;
		u16 major_version;
	};
	u32 version;
};

/*
 * Protocol messages
 */
struct synthhid_protocol_request {
	struct synthhid_msg_hdr header;
	union synthhid_version version_requested;
};

struct synthhid_protocol_response {
	struct synthhid_msg_hdr header;
	union synthhid_version version_requested;
	unsigned char approved;
};

struct synthhid_device_info {
	struct synthhid_msg_hdr header;
	struct hv_input_dev_info hid_dev_info;
	struct hid_descriptor hid_descriptor;
};

struct synthhid_device_info_ack {
	struct synthhid_msg_hdr header;
	unsigned char reserved;
};

struct synthhid_input_report {
	struct synthhid_msg_hdr header;
	char buffer[1];
};

#pragma pack(pop)

#define INPUTVSC_SEND_RING_BUFFER_SIZE		(10*PAGE_SIZE)
#define INPUTVSC_RECV_RING_BUFFER_SIZE		(10*PAGE_SIZE)

#define NBITS(x) (((x)/BITS_PER_LONG)+1)

enum pipe_prot_msg_type {
	PIPE_MESSAGE_INVALID,
	PIPE_MESSAGE_DATA,
	PIPE_MESSAGE_MAXIMUM
};


struct pipe_prt_msg {
	enum pipe_prot_msg_type type;
	u32 size;
	char data[1];
};

struct  mousevsc_prt_msg {
	enum pipe_prot_msg_type type;
	u32 size;
	union {
		struct synthhid_protocol_request request;
		struct synthhid_protocol_response response;
		struct synthhid_device_info_ack ack;
	};
};

/*
 * Represents an mousevsc device
 */
struct mousevsc_dev {
	struct hv_device	*device;
	unsigned char		init_complete;
	struct mousevsc_prt_msg	protocol_req;
	struct mousevsc_prt_msg	protocol_resp;
	/* Synchronize the request/response if needed */
	struct completion	wait_event;
	int			dev_info_status;

	struct hid_descriptor	*hid_desc;
	unsigned char		*report_desc;
	u32			report_desc_size;
	struct hv_input_dev_info hid_dev_info;
	int			connected;
	struct hid_device       *hid_device;
};


static struct mousevsc_dev *alloc_input_device(struct hv_device *device)
{
	struct mousevsc_dev *input_dev;

	input_dev = kzalloc(sizeof(struct mousevsc_dev), GFP_KERNEL);

	if (!input_dev)
		return NULL;

	input_dev->device = device;
	hv_set_drvdata(device, input_dev);
	init_completion(&input_dev->wait_event);

	return input_dev;
}

static void free_input_device(struct mousevsc_dev *device)
{
	kfree(device->hid_desc);
	kfree(device->report_desc);
	hv_set_drvdata(device->device, NULL);
	kfree(device);
}


static void mousevsc_on_receive_device_info(struct mousevsc_dev *input_device,
				struct synthhid_device_info *device_info)
{
	int ret = 0;
	struct hid_descriptor *desc;
	struct mousevsc_prt_msg ack;

	/* Assume success for now */
	input_device->dev_info_status = 0;

	memcpy(&input_device->hid_dev_info, &device_info->hid_dev_info,
		sizeof(struct hv_input_dev_info));

	desc = &device_info->hid_descriptor;
	WARN_ON(desc->bLength == 0);

	input_device->hid_desc = kzalloc(desc->bLength, GFP_ATOMIC);

	if (!input_device->hid_desc)
		goto cleanup;

	memcpy(input_device->hid_desc, desc, desc->bLength);

	input_device->report_desc_size = desc->desc[0].wDescriptorLength;
	if (input_device->report_desc_size == 0)
		goto cleanup;
	input_device->report_desc = kzalloc(input_device->report_desc_size,
					  GFP_ATOMIC);

	if (!input_device->report_desc)
		goto cleanup;

	memcpy(input_device->report_desc,
	       ((unsigned char *)desc) + desc->bLength,
	       desc->desc[0].wDescriptorLength);

	/* Send the ack */
	memset(&ack, 0, sizeof(struct mousevsc_prt_msg));

	ack.type = PIPE_MESSAGE_DATA;
	ack.size = sizeof(struct synthhid_device_info_ack);

	ack.ack.header.type = SYNTH_HID_INITIAL_DEVICE_INFO_ACK;
	ack.ack.header.size = 1;
	ack.ack.reserved = 0;

	ret = vmbus_sendpacket(input_device->device->channel,
			&ack,
			sizeof(struct pipe_prt_msg) - sizeof(unsigned char) +
			sizeof(struct synthhid_device_info_ack),
			(unsigned long)&ack,
			VM_PKT_DATA_INBAND,
			VMBUS_DATA_PACKET_FLAG_COMPLETION_REQUESTED);
	if (ret != 0)
		goto cleanup;

	complete(&input_device->wait_event);

	return;

cleanup:
	kfree(input_device->hid_desc);
	input_device->hid_desc = NULL;

	kfree(input_device->report_desc);
	input_device->report_desc = NULL;

	input_device->dev_info_status = -1;
	complete(&input_device->wait_event);
}

static void mousevsc_on_receive(struct hv_device *device,
				struct vmpacket_descriptor *packet)
{
	struct pipe_prt_msg *pipe_msg;
	struct synthhid_msg *hid_msg;
	struct mousevsc_dev *input_dev = hv_get_drvdata(device);
	struct synthhid_input_report *input_report;

	pipe_msg = (struct pipe_prt_msg *)((unsigned long)packet +
						(packet->offset8 << 3));

	if (pipe_msg->type != PIPE_MESSAGE_DATA)
		return;

	hid_msg = (struct synthhid_msg *)&pipe_msg->data[0];

	switch (hid_msg->header.type) {
	case SYNTH_HID_PROTOCOL_RESPONSE:
		memcpy(&input_dev->protocol_resp, pipe_msg,
		       pipe_msg->size + sizeof(struct pipe_prt_msg) -
		       sizeof(unsigned char));
		complete(&input_dev->wait_event);
		break;

	case SYNTH_HID_INITIAL_DEVICE_INFO:
		WARN_ON(pipe_msg->size < sizeof(struct hv_input_dev_info));

		/*
		 * Parse out the device info into device attr,
		 * hid desc and report desc
		 */
		mousevsc_on_receive_device_info(input_dev,
			(struct synthhid_device_info *)&pipe_msg->data[0]);
		break;
	case SYNTH_HID_INPUT_REPORT:
		input_report =
			(struct synthhid_input_report *)&pipe_msg->data[0];
		if (!input_dev->init_complete)
			break;
		hid_input_report(input_dev->hid_device,
				HID_INPUT_REPORT, input_report->buffer,
				input_report->header.size, 1);
		break;
	default:
		pr_err("unsupported hid msg type - type %d len %d",
		       hid_msg->header.type, hid_msg->header.size);
		break;
	}

}

static void mousevsc_on_channel_callback(void *context)
{
	const int packetSize = 0x100;
	int ret = 0;
	struct hv_device *device = (struct hv_device *)context;

	u32 bytes_recvd;
	u64 req_id;
	unsigned char packet[0x100];
	struct vmpacket_descriptor *desc;
	unsigned char	*buffer = packet;
	int	bufferlen = packetSize;


	do {
		ret = vmbus_recvpacket_raw(device->channel, buffer,
					bufferlen, &bytes_recvd, &req_id);

		if (ret == 0) {
			if (bytes_recvd > 0) {
				desc = (struct vmpacket_descriptor *)buffer;

				switch (desc->type) {
				case VM_PKT_COMP:
					break;

				case VM_PKT_DATA_INBAND:
					mousevsc_on_receive(
						device, desc);
					break;

				default:
					pr_err("unhandled packet type %d, tid %llx len %d\n",
						   desc->type,
						   req_id,
						   bytes_recvd);
					break;
				}

				/* reset */
				if (bufferlen > packetSize) {
					kfree(buffer);

					buffer = packet;
					bufferlen = packetSize;
				}
			} else {
				if (bufferlen > packetSize) {
					kfree(buffer);

					buffer = packet;
					bufferlen = packetSize;
				}
				break;
			}
		} else if (ret == -ENOBUFS) {
			/* Handle large packet */
			bufferlen = bytes_recvd;
			buffer = kzalloc(bytes_recvd, GFP_ATOMIC);

			if (buffer == NULL) {
				buffer = packet;
				bufferlen = packetSize;
				break;
			}
		}
	} while (1);

	return;
}

static int mousevsc_connect_to_vsp(struct hv_device *device)
{
	int ret = 0;
	int t;
	struct mousevsc_dev *input_dev = hv_get_drvdata(device);
	struct mousevsc_prt_msg *request;
	struct mousevsc_prt_msg *response;


	request = &input_dev->protocol_req;

	memset(request, 0, sizeof(struct mousevsc_prt_msg));

	request->type = PIPE_MESSAGE_DATA;
	request->size = sizeof(struct synthhid_protocol_request);

	request->request.header.type = SYNTH_HID_PROTOCOL_REQUEST;
	request->request.header.size = sizeof(unsigned int);
	request->request.version_requested.version = SYNTHHID_INPUT_VERSION;


	ret = vmbus_sendpacket(device->channel, request,
				sizeof(struct pipe_prt_msg) -
				sizeof(unsigned char) +
				sizeof(struct synthhid_protocol_request),
				(unsigned long)request,
				VM_PKT_DATA_INBAND,
				VMBUS_DATA_PACKET_FLAG_COMPLETION_REQUESTED);
	if (ret != 0)
		goto cleanup;

	t = wait_for_completion_timeout(&input_dev->wait_event, 5*HZ);
	if (t == 0) {
		ret = -ETIMEDOUT;
		goto cleanup;
	}

	response = &input_dev->protocol_resp;

	if (!response->response.approved) {
		pr_err("synthhid protocol request failed (version %d)",
		       SYNTHHID_INPUT_VERSION);
		ret = -ENODEV;
		goto cleanup;
	}

	t = wait_for_completion_timeout(&input_dev->wait_event, 5*HZ);
	if (t == 0) {
		ret = -ETIMEDOUT;
		goto cleanup;
	}

	/*
	 * We should have gotten the device attr, hid desc and report
	 * desc at this point
	 */
	if (input_dev->dev_info_status)
		ret = -ENOMEM;

cleanup:

	return ret;
}

static int mousevsc_hid_open(struct hid_device *hid)
{
	return 0;
}

static void mousevsc_hid_close(struct hid_device *hid)
{
}

static struct hid_ll_driver mousevsc_ll_driver = {
	.open = mousevsc_hid_open,
	.close = mousevsc_hid_close,
};

static struct hid_driver mousevsc_hid_driver;

static void reportdesc_callback(struct hv_device *dev, void *packet, u32 len)
{
	struct hid_device *hid_dev;
	struct mousevsc_dev *input_device = hv_get_drvdata(dev);

	hid_dev = hid_allocate_device();
	if (IS_ERR(hid_dev))
		return;

	hid_dev->ll_driver = &mousevsc_ll_driver;
	hid_dev->driver = &mousevsc_hid_driver;

	if (hid_parse_report(hid_dev, packet, len))
		return;

	hid_dev->bus = BUS_VIRTUAL;
	hid_dev->vendor = input_device->hid_dev_info.vendor;
	hid_dev->product = input_device->hid_dev_info.product;
	hid_dev->version = input_device->hid_dev_info.version;

	sprintf(hid_dev->name, "%s", "Microsoft Vmbus HID-compliant Mouse");

	if (!hidinput_connect(hid_dev, 0)) {
		hid_dev->claimed |= HID_CLAIMED_INPUT;

		input_device->connected = 1;

	}

	input_device->hid_device = hid_dev;
}

static int mousevsc_on_device_add(struct hv_device *device)
{
	int ret = 0;
	struct mousevsc_dev *input_dev;

	input_dev = alloc_input_device(device);

	if (!input_dev)
		return -ENOMEM;

	input_dev->init_complete = false;

	ret = vmbus_open(device->channel,
		INPUTVSC_SEND_RING_BUFFER_SIZE,
		INPUTVSC_RECV_RING_BUFFER_SIZE,
		NULL,
		0,
		mousevsc_on_channel_callback,
		device
		);

	if (ret != 0) {
		free_input_device(input_dev);
		return ret;
	}


	ret = mousevsc_connect_to_vsp(device);

	if (ret != 0) {
		vmbus_close(device->channel);
		free_input_device(input_dev);
		return ret;
	}


	/* workaround SA-167 */
	if (input_dev->report_desc[14] == 0x25)
		input_dev->report_desc[14] = 0x29;

	reportdesc_callback(device, input_dev->report_desc,
			    input_dev->report_desc_size);

	input_dev->init_complete = true;

	return ret;
}

static int mousevsc_probe(struct hv_device *dev,
			const struct hv_vmbus_device_id *dev_id)
{

	return mousevsc_on_device_add(dev);

}

static int mousevsc_remove(struct hv_device *dev)
{
	struct mousevsc_dev *input_dev = hv_get_drvdata(dev);

	vmbus_close(dev->channel);

	if (input_dev->connected) {
		hidinput_disconnect(input_dev->hid_device);
		input_dev->connected = 0;
		hid_destroy_device(input_dev->hid_device);
	}

	free_input_device(input_dev);

	return 0;
}

static const struct hv_vmbus_device_id id_table[] = {
	/* Mouse guid */
	{ VMBUS_DEVICE(0x9E, 0xB6, 0xA8, 0xCF, 0x4A, 0x5B, 0xc0, 0x4c,
		       0xB9, 0x8B, 0x8B, 0xA1, 0xA1, 0xF3, 0xF9, 0x5A) },
	{ },
};

MODULE_DEVICE_TABLE(vmbus, id_table);

static struct  hv_driver mousevsc_drv = {
	.name = "mousevsc",
	.id_table = id_table,
	.probe = mousevsc_probe,
	.remove = mousevsc_remove,
};

static int __init mousevsc_init(void)
{
	return vmbus_driver_register(&mousevsc_drv);
}

static void __exit mousevsc_exit(void)
{
	vmbus_driver_unregister(&mousevsc_drv);
}

MODULE_LICENSE("GPL");
MODULE_VERSION(HV_DRV_VERSION);
module_init(mousevsc_init);
module_exit(mousevsc_exit);
