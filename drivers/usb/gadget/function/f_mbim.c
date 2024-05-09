// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2012-2017, 2020, The Linux Foundation. All rights reserved.
 * Copyright (c) 2023-2024, Qualcomm Innovation Center, Inc. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#define pr_fmt(fmt) "%s: " fmt, __func__

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/cdev.h>

#include <linux/usb/cdc.h>
#include <linux/usb/composite.h>
#include <linux/platform_device.h>

#include <linux/spinlock.h>
#include <linux/io.h>
#include <asm/byteorder.h>
#include <asm/unaligned.h>

#include "configfs.h"
#include "u_rmnet.h"

/*
 * This function is a "Mobile Broadband Interface Model" (MBIM) link.
 * MBIM is intended to be used with high-speed network attachments.
 *
 * Note that MBIM requires the use of "alternate settings" for its data
 * interface.  This means that the set_alt() method has real work to do,
 * and also means that a get_alt() method is required.
 */

#define MBIM_BULK_BUFFER_SIZE		4096
#define MAX_CTRL_PKT_SIZE		4096
#define MAX_INST_NAME_LEN		40
#define MAX_MBIM_INSTANCES		1
#define USB_CDC_RESET_FUNCTION		0x05

enum mbim_peripheral_ep_type {
	MBIM_DATA_EP_TYPE_RESERVED   = 0x0,
	MBIM_DATA_EP_TYPE_HSIC       = 0x1,
	MBIM_DATA_EP_TYPE_HSUSB      = 0x2,
	MBIM_DATA_EP_TYPE_PCIE       = 0x3,
	MBIM_DATA_EP_TYPE_EMBEDDED   = 0x4,
	MBIM_DATA_EP_TYPE_BAM_DMUX   = 0x5,
};

struct mbim_peripheral_ep_info {
	enum mbim_peripheral_ep_type	ep_type;
	u32  peripheral_iface_id;
};

struct mbim_ipa_ep_pair {
	u32 cons_pipe_num;
	u32 prod_pipe_num;
};

struct mbim_ipa_ep_info {
	struct mbim_peripheral_ep_info ph_ep_info;
	struct mbim_ipa_ep_pair        ipa_ep_pair;
};

#define MBIM_IOCTL_MAGIC	 'o'
#define MBIM_GET_NTB_SIZE	 _IOR(MBIM_IOCTL_MAGIC, 2, u32)
#define MBIM_GET_DATAGRAM_COUNT	 _IOR(MBIM_IOCTL_MAGIC, 3, u16)

#define MBIM_EP_LOOKUP	_IOR(MBIM_IOCTL_MAGIC, 4, struct mbim_ipa_ep_info)


#define NR_MBIM_PORTS			1
#define MBIM_DEFAULT_PORT		0
#define XPORT_STR_LEN			12

/* ID for Microsoft OS String */
#define MBIM_OS_STRING_ID   0xEE

struct ctrl_pkt {
	void			*buf;
	int			len;
	struct list_head	list;
};

struct mbim_ep_descs {
	struct usb_endpoint_descriptor	*in;
	struct usb_endpoint_descriptor	*out;
	struct usb_endpoint_descriptor	*notify;
};

struct mbim_notify_port {
	struct usb_ep			*notify;
	struct usb_request		*notify_req;
	u8				notify_state;
	atomic_t			notify_count;
};

enum mbim_notify_state {
	MBIM_NOTIFY_NONE,
	MBIM_NOTIFY_CONNECT,
	MBIM_NOTIFY_SPEED,
	MBIM_NOTIFY_RESPONSE_AVAILABLE,
};

enum transport_type {
	USB_GADGET_XPORT_UNDEF,
	USB_GADGET_XPORT_TTY,
	USB_GADGET_XPORT_SMD,
	USB_GADGET_XPORT_QTI,
	USB_GADGET_XPORT_BAM2BAM,
	USB_GADGET_XPORT_BAM2BAM_IPA,
	USB_GADGET_XPORT_HSIC,
	USB_GADGET_XPORT_HSUART,
	USB_GADGET_XPORT_ETHER,
	USB_GADGET_XPORT_CHAR_BRIDGE,
	USB_GADGET_XPORT_BAM_DMUX,
	USB_GADGET_XPORT_NONE,
};

struct f_mbim {
	struct usb_function		function;
	struct usb_composite_dev	*cdev;

	atomic_t			online;
	atomic_t			ctrl_online;

	atomic_t			open_excl;
	atomic_t			ioctl_excl;
	atomic_t			read_excl;
	atomic_t			write_excl;

	wait_queue_head_t		read_wq;

	enum bam_dmux_func_type         bam_dmux_func_type;
	enum transport_type		xport;
	u8				port_num;
	struct data_port		bam_port;
	struct mbim_notify_port		not_port;
	struct grmnet			port;

	struct mbim_ep_descs		fs;
	struct mbim_ep_descs		hs;

	u8				ctrl_id, data_id;
	bool				data_interface_up;

	spinlock_t			lock;

	struct list_head		cpkt_req_q;
	struct list_head		cpkt_resp_q;

	u32				ntb_input_size;
	u16				ntb_max_datagrams;

	atomic_t			error;
	unsigned int			cpkt_drop_cnt;
	bool				remote_wakeup_enabled;
	struct cdev			mbim_cdev;
	dev_t				devno;
	struct device			*dev;
	struct class			*mbim_class;
};

struct mbim_ntb_input_size {
	u32	ntb_input_size;
	u16	ntb_max_datagrams;
	u16	reserved;
};

/* temporary variable used between mbim_open() and mbim_gadget_bind() */
static struct f_mbim *_mbim_dev;

static unsigned int nr_mbim_ports;

static struct mbim_ports {
	struct f_mbim	*port;
	unsigned int	port_num;
} mbim_ports[NR_MBIM_PORTS];

static inline struct f_mbim *func_to_mbim(struct usb_function *f)
{
	return container_of(f, struct f_mbim, function);
}

static inline struct f_mbim *port_to_mbim(struct grmnet *r)
{
	return container_of(r, struct f_mbim, port);
}

struct f_mbim_opts {
	struct usb_function_instance func_inst;
	struct f_mbim *usb_mbim;

	/* os desc support */
	struct config_group *interf_group;
	char ext_compat_id[16];
	struct usb_os_desc os_desc;

	char *channel_name;
	int	refcnt;
};

/*-------------------------------------------------------------------------*/

#define MBIM_NTB_DEFAULT_IN_SIZE	(0x4000)
#define MBIM_NTB_OUT_SIZE		(0x1000)
#define MBIM_NDP_IN_DIVISOR		(0x4)

#define NTB_DEFAULT_IN_SIZE_IPA	(0x4000)
#define MBIM_NTB_OUT_SIZE_IPA		(0x4000)

#define MBIM_FORMATS_SUPPORTED	USB_CDC_NCM_NTB16_SUPPORTED

static struct usb_cdc_ncm_ntb_parameters mbim_ntb_parameters = {
	.wLength = sizeof(mbim_ntb_parameters),
	.bmNtbFormatsSupported = cpu_to_le16(MBIM_FORMATS_SUPPORTED),
	.dwNtbInMaxSize = cpu_to_le32(MBIM_NTB_DEFAULT_IN_SIZE),
	.wNdpInDivisor = cpu_to_le16(MBIM_NDP_IN_DIVISOR),
	.wNdpInPayloadRemainder = cpu_to_le16(0),
	.wNdpInAlignment = cpu_to_le16(4),

	.dwNtbOutMaxSize = cpu_to_le32(MBIM_NTB_OUT_SIZE),
	.wNdpOutDivisor = cpu_to_le16(4),
	.wNdpOutPayloadRemainder = cpu_to_le16(0),
	.wNdpOutAlignment = cpu_to_le16(4),
	.wNtbOutMaxDatagrams = 0,
};

/*
 * Use wMaxPacketSize big enough to fit CDC_NOTIFY_SPEED_CHANGE in one
 * packet, to simplify cancellation; and a big transfer interval, to
 * waste less bandwidth.
 */

#define LOG2_STATUS_INTERVAL_MSEC	5	/* 1 << 5 == 32 msec */
#define NCM_STATUS_BYTECOUNT		16	/* 8 byte header + data */

static struct usb_interface_assoc_descriptor mbim_iad_desc = {
	.bLength =		sizeof(mbim_iad_desc),
	.bDescriptorType =	USB_DT_INTERFACE_ASSOCIATION,

	/* .bFirstInterface =	DYNAMIC, */
	.bInterfaceCount =	2,	/* control + data */
	.bFunctionClass =	2,
	.bFunctionSubClass =	0x0e,
	.bFunctionProtocol =	0,
	/* .iFunction =		DYNAMIC */
};

/* interface descriptor: */
static struct usb_interface_descriptor mbim_control_intf = {
	.bLength =		sizeof(mbim_control_intf),
	.bDescriptorType =	USB_DT_INTERFACE,

	/* .bInterfaceNumber = DYNAMIC */
	.bNumEndpoints =	1,
	.bInterfaceClass =	0x02,
	.bInterfaceSubClass =	0x0e,
	.bInterfaceProtocol =	0,
	/* .iInterface = DYNAMIC */
};

static struct usb_cdc_header_desc mbim_header_desc = {
	.bLength =		sizeof(mbim_header_desc),
	.bDescriptorType =	USB_DT_CS_INTERFACE,
	.bDescriptorSubType =	USB_CDC_HEADER_TYPE,

	.bcdCDC =		cpu_to_le16(0x0110),
};

static struct usb_cdc_union_desc mbim_union_desc = {
	.bLength =		sizeof(mbim_union_desc),
	.bDescriptorType =	USB_DT_CS_INTERFACE,
	.bDescriptorSubType =	USB_CDC_UNION_TYPE,
	/* .bMasterInterface0 =	DYNAMIC */
	/* .bSlaveInterface0 =	DYNAMIC */
};

static struct usb_cdc_mbim_desc mbim_desc = {
	.bLength =		sizeof(mbim_desc),
	.bDescriptorType =	USB_DT_CS_INTERFACE,
	.bDescriptorSubType =	USB_CDC_MBIM_TYPE,

	.bcdMBIMVersion =	cpu_to_le16(0x0100),

	.wMaxControlMessage =	cpu_to_le16(0x1000),
	.bNumberFilters =	0x20,
	.bMaxFilterSize =	0x80,
	.wMaxSegmentSize =	cpu_to_le16(0x800),
	.bmNetworkCapabilities = 0x20,
};

static struct usb_cdc_mbim_extended_desc ext_mbb_desc = {
	.bLength =	sizeof(ext_mbb_desc),
	.bDescriptorType =	USB_DT_CS_INTERFACE,
	.bDescriptorSubType =	USB_CDC_MBIM_EXTENDED_TYPE,

	.bcdMBIMExtendedVersion =		cpu_to_le16(0x0100),
	.bMaxOutstandingCommandMessages =	64,
	.wMTU =					cpu_to_le16(1500),
};

/* the default data interface has no endpoints ... */
static struct usb_interface_descriptor mbim_data_nop_intf = {
	.bLength =		sizeof(mbim_data_nop_intf),
	.bDescriptorType =	USB_DT_INTERFACE,

	/* .bInterfaceNumber = DYNAMIC */
	.bAlternateSetting =	0,
	.bNumEndpoints =	0,
	.bInterfaceClass =	0x0a,
	.bInterfaceSubClass =	0,
	.bInterfaceProtocol =	0x02,
	/* .iInterface = DYNAMIC */
};

/* ... but the "real" data interface has two bulk endpoints */
static struct usb_interface_descriptor mbim_data_intf = {
	.bLength =		sizeof(mbim_data_intf),
	.bDescriptorType =	USB_DT_INTERFACE,

	/* .bInterfaceNumber = DYNAMIC */
	.bAlternateSetting =	1,
	.bNumEndpoints =	2,
	.bInterfaceClass =	0x0a,
	.bInterfaceSubClass =	0,
	.bInterfaceProtocol =	0x02,
	/* .iInterface = DYNAMIC */
};

/* full speed support: */

static struct usb_endpoint_descriptor fs_mbim_notify_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,

	.bEndpointAddress =	USB_DIR_IN,
	.bmAttributes =		USB_ENDPOINT_XFER_INT,
	.wMaxPacketSize =	4*cpu_to_le16(NCM_STATUS_BYTECOUNT),
	.bInterval =		1 << LOG2_STATUS_INTERVAL_MSEC,
};

static struct usb_endpoint_descriptor fs_mbim_in_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,

	.bEndpointAddress =	USB_DIR_IN,
	.bmAttributes =		USB_ENDPOINT_XFER_BULK,
};

static struct usb_endpoint_descriptor fs_mbim_out_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,

	.bEndpointAddress =	USB_DIR_OUT,
	.bmAttributes =		USB_ENDPOINT_XFER_BULK,
};

static struct usb_descriptor_header *mbim_fs_function[] = {
	(struct usb_descriptor_header *) &mbim_iad_desc,
	/* MBIM control descriptors */
	(struct usb_descriptor_header *) &mbim_control_intf,
	(struct usb_descriptor_header *) &mbim_header_desc,
	(struct usb_descriptor_header *) &mbim_union_desc,
	(struct usb_descriptor_header *) &mbim_desc,
	(struct usb_descriptor_header *) &ext_mbb_desc,
	(struct usb_descriptor_header *) &fs_mbim_notify_desc,
	/* data interface, altsettings 0 and 1 */
	(struct usb_descriptor_header *) &mbim_data_nop_intf,
	(struct usb_descriptor_header *) &mbim_data_intf,
	(struct usb_descriptor_header *) &fs_mbim_in_desc,
	(struct usb_descriptor_header *) &fs_mbim_out_desc,
	NULL,
};

/* high speed support: */

static struct usb_endpoint_descriptor hs_mbim_notify_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,

	.bEndpointAddress =	USB_DIR_IN,
	.bmAttributes =		USB_ENDPOINT_XFER_INT,
	.wMaxPacketSize =	4*cpu_to_le16(NCM_STATUS_BYTECOUNT),
	.bInterval =		LOG2_STATUS_INTERVAL_MSEC + 4,
};
static struct usb_endpoint_descriptor hs_mbim_in_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,

	.bEndpointAddress =	USB_DIR_IN,
	.bmAttributes =		USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize =	cpu_to_le16(512),
};

static struct usb_endpoint_descriptor hs_mbim_out_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,

	.bEndpointAddress =	USB_DIR_OUT,
	.bmAttributes =		USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize =	cpu_to_le16(512),
};

static struct usb_descriptor_header *mbim_hs_function[] = {
	(struct usb_descriptor_header *) &mbim_iad_desc,
	/* MBIM control descriptors */
	(struct usb_descriptor_header *) &mbim_control_intf,
	(struct usb_descriptor_header *) &mbim_header_desc,
	(struct usb_descriptor_header *) &mbim_union_desc,
	(struct usb_descriptor_header *) &mbim_desc,
	(struct usb_descriptor_header *) &ext_mbb_desc,
	(struct usb_descriptor_header *) &hs_mbim_notify_desc,
	/* data interface, altsettings 0 and 1 */
	(struct usb_descriptor_header *) &mbim_data_nop_intf,
	(struct usb_descriptor_header *) &mbim_data_intf,
	(struct usb_descriptor_header *) &hs_mbim_in_desc,
	(struct usb_descriptor_header *) &hs_mbim_out_desc,
	NULL,
};

/* Super Speed Support */
static struct usb_endpoint_descriptor ss_mbim_notify_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,
	.bEndpointAddress =	USB_DIR_IN,
	.bmAttributes =		USB_ENDPOINT_XFER_INT,
	.wMaxPacketSize =	4*cpu_to_le16(NCM_STATUS_BYTECOUNT),
	.bInterval =		LOG2_STATUS_INTERVAL_MSEC + 4,
};

static struct usb_ss_ep_comp_descriptor ss_mbim_notify_comp_desc = {
	.bLength =		sizeof(ss_mbim_notify_comp_desc),
	.bDescriptorType =	USB_DT_SS_ENDPOINT_COMP,

	/* the following 3 values can be tweaked if necessary */
	/* .bMaxBurst =         0, */
	/* .bmAttributes =      0, */
	.wBytesPerInterval =	4*cpu_to_le16(NCM_STATUS_BYTECOUNT),
};

static struct usb_endpoint_descriptor ss_mbim_in_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,
	.bEndpointAddress =	USB_DIR_IN,
	.bmAttributes =		USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize =	cpu_to_le16(1024),
};

static struct usb_ss_ep_comp_descriptor ss_mbim_in_comp_desc = {
	.bLength =              sizeof(ss_mbim_in_comp_desc),
	.bDescriptorType =      USB_DT_SS_ENDPOINT_COMP,

	/* the following 2 values can be tweaked if necessary */
	/* .bMaxBurst =         0, */
	/* .bmAttributes =      0, */
};

static struct usb_endpoint_descriptor ss_mbim_out_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,
	.bEndpointAddress =	USB_DIR_OUT,
	.bmAttributes =		USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize =	cpu_to_le16(1024),
};

static struct usb_ss_ep_comp_descriptor ss_mbim_out_comp_desc = {
	.bLength =		sizeof(ss_mbim_out_comp_desc),
	.bDescriptorType =	USB_DT_SS_ENDPOINT_COMP,

	/* the following 2 values can be tweaked if necessary */
	/* .bMaxBurst =         0, */
	/* .bmAttributes =      0, */
};

static struct usb_descriptor_header *mbim_ss_function[] = {
	(struct usb_descriptor_header *) &mbim_iad_desc,
	/* MBIM control descriptors */
	(struct usb_descriptor_header *) &mbim_control_intf,
	(struct usb_descriptor_header *) &mbim_header_desc,
	(struct usb_descriptor_header *) &mbim_union_desc,
	(struct usb_descriptor_header *) &mbim_desc,
	(struct usb_descriptor_header *) &ext_mbb_desc,
	(struct usb_descriptor_header *) &ss_mbim_notify_desc,
	(struct usb_descriptor_header *) &ss_mbim_notify_comp_desc,
	/* data interface, altsettings 0 and 1 */
	(struct usb_descriptor_header *) &mbim_data_nop_intf,
	(struct usb_descriptor_header *) &mbim_data_intf,
	(struct usb_descriptor_header *) &ss_mbim_in_desc,
	(struct usb_descriptor_header *) &ss_mbim_in_comp_desc,
	(struct usb_descriptor_header *) &ss_mbim_out_desc,
	(struct usb_descriptor_header *) &ss_mbim_out_comp_desc,
	NULL,
};

/* string descriptors: */

#define STRING_CTRL_IDX	0
#define STRING_DATA_IDX	1

static struct usb_string mbim_string_defs[] = {
	[STRING_CTRL_IDX].s = "MBIM Control",
	[STRING_DATA_IDX].s = "MBIM Data",
	{  } /* end of list */
};

static struct usb_gadget_strings mbim_string_table = {
	.language =		0x0409,	/* en-us */
	.strings =		mbim_string_defs,
};

static struct usb_gadget_strings *mbim_strings[] = {
	&mbim_string_table,
	NULL,
};



static inline int mbim_lock(atomic_t *excl)
{
	if (atomic_inc_return(excl) == 1)
		return 0;

	atomic_dec(excl);
	return -EBUSY;
}

static inline void mbim_unlock(atomic_t *excl)
{
	atomic_dec(excl);
}

static struct ctrl_pkt *mbim_alloc_ctrl_pkt(unsigned int len, gfp_t flags)
{
	struct ctrl_pkt *pkt;

	pkt = kzalloc(sizeof(struct ctrl_pkt), flags);
	if (!pkt)
		return ERR_PTR(-ENOMEM);

	pkt->buf = kmalloc(len, flags);
	if (!pkt->buf) {
		kfree(pkt);
		return ERR_PTR(-ENOMEM);
	}
	pkt->len = len;

	return pkt;
}

static void mbim_free_ctrl_pkt(struct ctrl_pkt *pkt)
{
	if (pkt) {
		kfree(pkt->buf);
		kfree(pkt);
	}
}

static struct usb_request *mbim_alloc_req(struct usb_ep *ep, int buffer_size)
{
	struct usb_request *req = usb_ep_alloc_request(ep, GFP_KERNEL);

	if (!req)
		return NULL;

	req->buf = kmalloc(buffer_size, GFP_KERNEL);
	if (!req->buf) {
		usb_ep_free_request(ep, req);
		return NULL;
	}
	req->length = buffer_size;
	return req;
}

void fmbim_free_req(struct usb_ep *ep, struct usb_request *req)
{
	if (req) {
		kfree(req->buf);
		usb_ep_free_request(ep, req);
	}
}

/* ---------------------------- BAM INTERFACE ----------------------------- */
/* -------------------------------------------------------------------------*/

static inline void mbim_reset_values(struct f_mbim *mbim)
{
	mbim->ntb_input_size = MBIM_NTB_DEFAULT_IN_SIZE;

	atomic_set(&mbim->online, 0);
}

static void mbim_reset_function_queue(struct f_mbim *dev)
{
	struct ctrl_pkt	*cpkt = NULL;

	pr_debug("Queue empty packet for QBI\n");

	spin_lock(&dev->lock);

	cpkt = mbim_alloc_ctrl_pkt(0, GFP_ATOMIC);
	if (!cpkt) {
		pr_err("%s: Unable to allocate reset function pkt\n", __func__);
		spin_unlock(&dev->lock);
		return;
	}

	list_add_tail(&cpkt->list, &dev->cpkt_req_q);
	spin_unlock(&dev->lock);

	pr_debug("%s: Wake up read queue\n", __func__);
	wake_up(&dev->read_wq);
}

static void fmbim_reset_cmd_complete(struct usb_ep *ep, struct usb_request *req)
{
	struct f_mbim		*dev = req->context;

	mbim_reset_function_queue(dev);
}

static void mbim_clear_queues(struct f_mbim *mbim)
{
	struct ctrl_pkt	*cpkt = NULL;
	struct list_head *act, *tmp;

	spin_lock(&mbim->lock);
	list_for_each_safe(act, tmp, &mbim->cpkt_req_q) {
		cpkt = list_entry(act, struct ctrl_pkt, list);
		list_del(&cpkt->list);
		mbim_free_ctrl_pkt(cpkt);
	}
	list_for_each_safe(act, tmp, &mbim->cpkt_resp_q) {
		cpkt = list_entry(act, struct ctrl_pkt, list);
		list_del(&cpkt->list);
		mbim_free_ctrl_pkt(cpkt);
	}
	spin_unlock(&mbim->lock);
}

/*
 * Context: mbim->lock held
 */
static void mbim_do_notify(struct f_mbim *mbim)
{
	struct usb_request		*req = mbim->not_port.notify_req;
	struct usb_cdc_notification	*event;
	int				status;

	pr_debug("notify_state: %d\n", mbim->not_port.notify_state);

	if (!req)
		return;

	event = req->buf;

	switch (mbim->not_port.notify_state) {

	case MBIM_NOTIFY_NONE:
		if (atomic_read(&mbim->not_port.notify_count) > 0)
			pr_err("Pending notifications in MBIM_NOTIFY_NONE\n");
		else
			pr_debug("No pending notifications\n");

		return;

	case MBIM_NOTIFY_RESPONSE_AVAILABLE:
		pr_debug("Notification %02x sent\n", event->bNotificationType);

		if (atomic_read(&mbim->not_port.notify_count) <= 0) {
			pr_debug("notify_response_available: done\n");
			return;
		}

		spin_unlock(&mbim->lock);
		status = usb_ep_queue(mbim->not_port.notify,
				req, GFP_ATOMIC);
		spin_lock(&mbim->lock);
		if (status) {
			atomic_dec(&mbim->not_port.notify_count);
			pr_err("Queue notify request failed, err: %d\n",
					status);
		}

		return;
	}

	event->bmRequestType = 0xA1;
	event->wIndex = cpu_to_le16(mbim->ctrl_id);

	/*
	 * In double buffering if there is a space in FIFO,
	 * completion callback can be called right after the call,
	 * so unlocking
	 */
	atomic_inc(&mbim->not_port.notify_count);
	pr_debug("queue request: notify_count = %d\n",
		atomic_read(&mbim->not_port.notify_count));
	spin_unlock(&mbim->lock);
	status = usb_ep_queue(mbim->not_port.notify, req,
			GFP_ATOMIC);
	spin_lock(&mbim->lock);
	if (status) {
		atomic_dec(&mbim->not_port.notify_count);
		pr_err("usb_ep_queue failed, err: %d\n", status);
	}
}

static void mbim_notify_complete(struct usb_ep *ep, struct usb_request *req)
{
	struct f_mbim			*mbim = req->context;
	struct usb_cdc_notification	*event = req->buf;

	pr_debug("dev:%pK\n", mbim);

	spin_lock(&mbim->lock);
	switch (req->status) {
	case 0:
		atomic_dec(&mbim->not_port.notify_count);
		pr_debug("notify_count = %d\n",
			atomic_read(&mbim->not_port.notify_count));
		break;

	case -ECONNRESET:
	case -ESHUTDOWN:
		/* connection gone */
		mbim->not_port.notify_state = MBIM_NOTIFY_NONE;
		atomic_set(&mbim->not_port.notify_count, 0);
		spin_unlock(&mbim->lock);
		mbim_clear_queues(mbim);
		mbim_reset_function_queue(mbim);
		spin_lock(&mbim->lock);
		break;
	default:
		pr_err("Unknown event %02x --> %d\n",
			event->bNotificationType, req->status);
		break;
	}

	mbim_do_notify(mbim);
	spin_unlock(&mbim->lock);

	pr_debug("dev:%pK Exit\n", mbim);
}

static void mbim_ep0out_complete(struct usb_ep *ep, struct usb_request *req)
{
	/* now for SET_NTB_INPUT_SIZE only */
	unsigned int		in_size = 0;
	struct usb_function	*f = req->context;
	struct f_mbim		*mbim = func_to_mbim(f);
	struct mbim_ntb_input_size *ntb = NULL;

	pr_debug("dev:%pK\n", mbim);

	req->context = NULL;
	if (req->status || req->actual != req->length) {
		pr_err("Bad control-OUT transfer\n");
		goto invalid;
	}

	if (req->length == 4) {
		in_size = get_unaligned_le32(req->buf);
		if (in_size < USB_CDC_NCM_NTB_MIN_IN_SIZE ||
		    in_size > le32_to_cpu(mbim_ntb_parameters.dwNtbInMaxSize)) {
			pr_err("Illegal INPUT SIZE (%d) from host\n", in_size);
			goto invalid;
		}
	} else if (req->length == 8) {
		ntb = (struct mbim_ntb_input_size *)req->buf;
		in_size = get_unaligned_le32(&(ntb->ntb_input_size));
		if (in_size < USB_CDC_NCM_NTB_MIN_IN_SIZE ||
		    in_size > le32_to_cpu(mbim_ntb_parameters.dwNtbInMaxSize)) {
			pr_err("Illegal INPUT SIZE (%d) from host\n", in_size);
			goto invalid;
		}
		mbim->ntb_max_datagrams =
			get_unaligned_le16(&(ntb->ntb_max_datagrams));
	} else {
		pr_err("Illegal NTB length %d\n", in_size);
		goto invalid;
	}

	pr_debug("Set NTB INPUT SIZE %d\n", in_size);

	mbim->ntb_input_size = in_size;
	return;

invalid:
	usb_ep_set_halt(ep);

	pr_err("dev:%pK Failed\n", mbim);
}

static void
fmbim_cmd_complete(struct usb_ep *ep, struct usb_request *req)
{
	struct f_mbim		*dev = req->context;
	struct ctrl_pkt		*cpkt = NULL;
	int			len = req->actual;
	static bool		first_command_sent;

	if (!dev) {
		pr_err("mbim dev is null\n");
		return;
	}

	if (req->status < 0) {
		pr_err("mbim command error %d\n", req->status);
		return;
	}

	/*
	 * Wait for user to process prev MBIM_OPEN cmd before handling new one.
	 * However don't drop first command during bootup as file may not be
	 * opened by now. Queue the command in this case.
	 */
	if (!atomic_read(&dev->open_excl) && first_command_sent) {
		pr_err("mbim not opened yet, dropping cmd pkt = %d\n", len);
		return;
	}
	if (!first_command_sent)
		first_command_sent = true;

	pr_debug("dev:%pK port#%d\n", dev, dev->port_num);

	cpkt = mbim_alloc_ctrl_pkt(len, GFP_ATOMIC);
	if (!cpkt) {
		pr_err("Unable to allocate ctrl pkt\n");
		return;
	}

	pr_debug("Add to cpkt_req_q packet with len = %d\n", len);
	memcpy(cpkt->buf, req->buf, len);

	spin_lock(&dev->lock);

	list_add_tail(&cpkt->list, &dev->cpkt_req_q);
	spin_unlock(&dev->lock);

	/* wakeup read thread */
	pr_debug("Wake up read queue\n");
	wake_up(&dev->read_wq);
}

static int
mbim_setup(struct usb_function *f, const struct usb_ctrlrequest *ctrl)
{
	struct f_mbim			*mbim = func_to_mbim(f);
	struct usb_composite_dev	*cdev = mbim->cdev;
	struct usb_request		*req = cdev->req;
	struct ctrl_pkt		*cpkt = NULL;
	int	value = -EOPNOTSUPP;
	u16	w_index = le16_to_cpu(ctrl->wIndex);
	u16	w_value = le16_to_cpu(ctrl->wValue);
	u16	w_length = le16_to_cpu(ctrl->wLength);

	/*
	 * composite driver infrastructure handles everything except
	 * CDC class messages; interface activation uses set_alt().
	 */

	if (!atomic_read(&mbim->online)) {
		pr_warn("usb cable is not connected\n");
		return -ENOTCONN;
	}

	switch ((ctrl->bRequestType << 8) | ctrl->bRequest) {
	case ((USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE) << 8)
		| USB_CDC_RESET_FUNCTION:

		pr_debug("USB_CDC_RESET_FUNCTION\n");
		value = 0;
		req->complete = fmbim_reset_cmd_complete;
		req->context = mbim;
		break;

	case ((USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE) << 8)
		| USB_CDC_SEND_ENCAPSULATED_COMMAND:

		pr_debug("USB_CDC_SEND_ENCAPSULATED_COMMAND\n");

		if (w_length > req->length) {
			pr_debug("w_length > req->length: %d > %d\n",
			w_length, req->length);
		}
		value = w_length;
		req->complete = fmbim_cmd_complete;
		req->context = mbim;
		break;

	case ((USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_INTERFACE) << 8)
		| USB_CDC_GET_ENCAPSULATED_RESPONSE:

		pr_debug("USB_CDC_GET_ENCAPSULATED_RESPONSE\n");

		if (w_value) {
			pr_err("w_length > 0: %d\n", w_length);
			break;
		}

		pr_debug("req%02x.%02x v%04x i%04x l%d\n",
			ctrl->bRequestType, ctrl->bRequest,
			w_value, w_index, w_length);

		spin_lock(&mbim->lock);
		if (list_empty(&mbim->cpkt_resp_q)) {
			pr_err("ctrl resp queue empty\n");
			spin_unlock(&mbim->lock);
			break;
		}

		cpkt = list_first_entry(&mbim->cpkt_resp_q,
					struct ctrl_pkt, list);
		list_del(&cpkt->list);
		spin_unlock(&mbim->lock);

		value = min_t(unsigned int, w_length, cpkt->len);
		memcpy(req->buf, cpkt->buf, value);
		mbim_free_ctrl_pkt(cpkt);

		pr_debug("copied encapsulated_response %d bytes\n",
			value);

		break;

	case ((USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_INTERFACE) << 8)
		| USB_CDC_GET_NTB_PARAMETERS:

		pr_debug("USB_CDC_GET_NTB_PARAMETERS\n");

		if (w_length == 0 || w_value != 0 || w_index != mbim->ctrl_id)
			break;

		value = w_length > sizeof(mbim_ntb_parameters) ?
			sizeof(mbim_ntb_parameters) : w_length;
		memcpy(req->buf, &mbim_ntb_parameters, value);
		break;

	case ((USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_INTERFACE) << 8)
		| USB_CDC_GET_NTB_INPUT_SIZE:

		pr_debug("USB_CDC_GET_NTB_INPUT_SIZE\n");

		if (w_length < 4 || w_value != 0 || w_index != mbim->ctrl_id)
			break;

		put_unaligned_le32(mbim->ntb_input_size, req->buf);
		value = 4;
		pr_debug("Reply to host INPUT SIZE %d\n",
		     mbim->ntb_input_size);
		break;

	case ((USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE) << 8)
		| USB_CDC_SET_NTB_INPUT_SIZE:

		pr_debug("USB_CDC_SET_NTB_INPUT_SIZE\n");

		if (w_length != 4 && w_length != 8) {
			pr_err("wrong NTB length %d\n", w_length);
			break;
		}

		if (w_value != 0 || w_index != mbim->ctrl_id)
			break;

		req->complete = mbim_ep0out_complete;
		req->length = w_length;
		req->context = f;

		value = req->length;
		break;

	/* optional in mbim descriptor: */
	/* case USB_CDC_GET_MAX_DATAGRAM_SIZE: */
	/* case USB_CDC_SET_MAX_DATAGRAM_SIZE: */

	default:
	pr_err("invalid control req: %02x.%02x v%04x i%04x l%d\n",
		ctrl->bRequestType, ctrl->bRequest,
		w_value, w_index, w_length);
	}

	 /* respond with data transfer or status phase? */
	if (value >= 0) {
		pr_debug("control request: %02x.%02x v%04x i%04x l%d\n",
			ctrl->bRequestType, ctrl->bRequest,
			w_value, w_index, w_length);
		req->zero = (value < w_length);
		req->length = value;
		value = usb_ep_queue(cdev->gadget->ep0, req, GFP_ATOMIC);

		if (value < 0) {
			pr_err("queueing req failed: %02x.%02x, err %d\n",
				ctrl->bRequestType,
			       ctrl->bRequest, value);
		}
	} else {
		pr_err("ctrl req err %d: %02x.%02x v%04x i%04x l%d\n",
			value, ctrl->bRequestType, ctrl->bRequest,
			w_value, w_index, w_length);
	}

	/* device either stalls (value < 0) or reports success */
	return value;
}

static int mbim_set_alt(struct usb_function *f, unsigned int intf,
				unsigned int alt)
{
	struct f_mbim		*mbim = func_to_mbim(f);
	struct usb_composite_dev *cdev = f->config->cdev;
	int ret = 0;

	pr_debug("intf=%u, alt=%u\n", intf, alt);

	/* Control interface has only altsetting 0 */
	if (intf == mbim->ctrl_id) {
		if (alt != 0)
			goto fail;
		if (mbim->not_port.notify->driver_data) {
			pr_debug("reset mbim control %d\n", intf);
			usb_ep_disable(mbim->not_port.notify);
		}

		ret = config_ep_by_speed(cdev->gadget, f,
					mbim->not_port.notify);
		if (ret) {
			mbim->not_port.notify->desc = NULL;
			pr_err("Failed configuring notify ep %s: err %d\n",
				mbim->not_port.notify->name, ret);
			return ret;
		}

		ret = usb_ep_enable(mbim->not_port.notify);
		if (ret) {
			pr_err("usb ep#%s enable failed, err#%d\n",
				mbim->not_port.notify->name, ret);
			return ret;
		}
		mbim->not_port.notify->driver_data = mbim;

	/* Data interface has two altsettings, 0 and 1 */
	} else if (intf == mbim->data_id) {

		pr_debug("DATA_INTERFACE id %d, data interface status %d\n",
				mbim->data_id, mbim->data_interface_up);

		if (alt > 1)
			goto fail;

		if (mbim->data_interface_up == alt)
			return 0;

		if (mbim->bam_port.in->driver_data)
			mbim_reset_values(mbim);

		if (alt == 0) {
			/*
			 * perform bam data disconnect handshake upon usb
			 * disconnect
			 */
			switch (mbim->xport) {
			case USB_GADGET_XPORT_BAM_DMUX:
				gbam_disconnect(&mbim->bam_port,
					mbim->bam_dmux_func_type);
				break;
			case USB_GADGET_XPORT_BAM2BAM_IPA:
				break;
			default:
				pr_err("unknown transport\n");
			}
			goto notify_ready;
		}

		/*
		 * CDC Network only sends data in non-default altsettings.
		 * Changing altsettings resets filters, statistics, etc.
		 */
		ret = config_ep_by_speed(cdev->gadget, f,
				mbim->bam_port.in);
		if (ret) {
			mbim->bam_port.in->desc = NULL;
			pr_err("IN ep %s failed: %d\n",
					mbim->bam_port.in->name, ret);
			return ret;
		}

		pr_debug("Set mbim port in_desc = 0x%pK\n",
				mbim->bam_port.in->desc);

		ret = config_ep_by_speed(cdev->gadget, f,
				mbim->bam_port.out);
		if (ret) {
			mbim->bam_port.out->desc = NULL;
			pr_err("OUT ep %s failed: %d\n",
					mbim->bam_port.out->name, ret);
			return ret;
		}

		pr_debug("Set mbim port out_desc = 0x%pK\n",
				mbim->bam_port.out->desc);

		pr_debug("Activate mbim\n");
		switch (mbim->xport) {
		case USB_GADGET_XPORT_BAM_DMUX:
			ret = gbam_connect(&mbim->bam_port,
					mbim->bam_dmux_func_type);
			if (ret)
				pr_err("%s: gbam_connect failed: err:%d\n",
					__func__, ret);
			break;
		case USB_GADGET_XPORT_BAM2BAM_IPA:
			break;
		default:
			pr_err("unknown transport\n");
		}
notify_ready:
		mbim->data_interface_up = alt;
		spin_lock(&mbim->lock);
		mbim->not_port.notify_state = MBIM_NOTIFY_RESPONSE_AVAILABLE;
		spin_unlock(&mbim->lock);
	} else {
		goto fail;
	}

	atomic_set(&mbim->online, 1);
	return 0;

fail:
	pr_err("ERROR: Illegal Interface\n");
	return -EINVAL;
}

/*
 * Because the data interface supports multiple altsettings,
 * this MBIM function *MUST* implement a get_alt() method.
 */
static int mbim_get_alt(struct usb_function *f, unsigned int intf)
{
	struct f_mbim	*mbim = func_to_mbim(f);

	if (intf == mbim->ctrl_id)
		return 0;
	else if (intf == mbim->data_id)
		return mbim->data_interface_up;

	return -EINVAL;
}

static void mbim_disable(struct usb_function *f)
{
	struct f_mbim	*mbim = func_to_mbim(f);

	atomic_set(&mbim->online, 0);
	mbim->remote_wakeup_enabled = false;

	 /* Disable Control Path */
	if (mbim->not_port.notify->driver_data) {
		usb_ep_fifo_flush(mbim->not_port.notify);
		usb_ep_disable(mbim->not_port.notify);
		mbim->not_port.notify->driver_data = NULL;
	}
	atomic_set(&mbim->not_port.notify_count, 0);
	mbim->not_port.notify_state = MBIM_NOTIFY_NONE;

	mbim_clear_queues(mbim);
	mbim_reset_function_queue(mbim);

	/* Disable Data Path  - only if it was initialized already (alt=1) */
	if (!mbim->data_interface_up) {
		pr_debug("MBIM data interface is not opened. Returning\n");
		return;
	}

	switch (mbim->xport) {
	case USB_GADGET_XPORT_BAM_DMUX:
		gbam_disconnect(&mbim->bam_port, mbim->bam_dmux_func_type);
		break;
	case USB_GADGET_XPORT_BAM2BAM_IPA:
		break;
	default:
		pr_err("unknown transport\n");
	}

	mbim->data_interface_up = false;
}

#define MBIM_ACTIVE_PORT	0

static void mbim_suspend(struct usb_function *f)
{
	struct f_mbim	*mbim = func_to_mbim(f);


	if (mbim->xport == USB_GADGET_XPORT_BAM_DMUX)
		return;


	/* MBIM data interface is up only when alt setting is set to 1. */
	if (!mbim->data_interface_up) {
		pr_debug("MBIM data interface is not opened. Returning\n");
		return;
	}

}

static void mbim_resume(struct usb_function *f)
{
	struct f_mbim	*mbim = func_to_mbim(f);

	if (mbim->xport == USB_GADGET_XPORT_BAM_DMUX)
		return;

	/* resume control path by queuing notify req */
	spin_lock(&mbim->lock);
	mbim_do_notify(mbim);
	spin_unlock(&mbim->lock);

	/* MBIM data interface is up only when alt setting is set to 1. */
	if (!mbim->data_interface_up) {
		pr_debug("MBIM data interface is not opened. Returning\n");
		return;
	}
}

static int mbim_func_suspend(struct usb_function *f, unsigned char options)
{
	enum {
		MBIM_FUNC_SUSPEND_MASK   = 0x1,
		MBIM_FUNC_WAKEUP_EN_MASK = 0x2
	};

	struct f_mbim	*mbim = func_to_mbim(f);

	if (f == NULL)
		return -EINVAL;

	pr_debug("Got Function Suspend(%u) command for %s function\n",
		options, f->name ? f->name : "");

	/* Function Suspend is supported by Super Speed devices only */
	if (mbim->cdev->gadget->speed != USB_SPEED_SUPER)
		return -EOPNOTSUPP;

	return 0;
}
static void mbim_purge_responses(struct f_mbim *dev)
{
	unsigned long flags;
	struct ctrl_pkt *cpkt;

	pr_debug("%s: Purging responses\n", __func__);
	spin_lock_irqsave(&dev->lock, flags);
	while (!list_empty(&dev->cpkt_resp_q)) {
		cpkt = list_first_entry(&dev->cpkt_resp_q,
				struct ctrl_pkt, list);

		list_del(&cpkt->list);
		mbim_free_ctrl_pkt(cpkt);
	}
	atomic_set(&dev->not_port.notify_count, 0);
	spin_unlock_irqrestore(&dev->lock, flags);
}

static void mbim_ctrl_response_available(struct f_mbim *dev)
{
	struct usb_request		*req = dev->not_port.notify_req;
	struct usb_cdc_notification	*event;
	unsigned long			flags;
	int				ret;
	struct ctrl_pkt		*cpkt;

	pr_debug("%s:dev:%pK\n", __func__, dev);
	spin_lock_irqsave(&dev->lock, flags);
	if (!atomic_read(&dev->online) || !req || !req->buf) {
		spin_unlock_irqrestore(&dev->lock, flags);
		return;
	}

	if (atomic_read(&dev->not_port.notify_count) != 1) {
		spin_unlock_irqrestore(&dev->lock, flags);
		return;
	}

	event = req->buf;
	event->bmRequestType = USB_DIR_IN | USB_TYPE_CLASS
			| USB_RECIP_INTERFACE;
	event->bNotificationType = USB_CDC_NOTIFY_RESPONSE_AVAILABLE;
	event->wValue = cpu_to_le16(0);
	event->wIndex = cpu_to_le16(dev->ctrl_id);
	event->wLength = cpu_to_le16(0);
	spin_unlock_irqrestore(&dev->lock, flags);

	ret = usb_ep_queue(dev->not_port.notify, dev->not_port.notify_req,
					GFP_ATOMIC);
	if (ret) {
		spin_lock_irqsave(&dev->lock, flags);
		if (!list_empty(&dev->cpkt_resp_q)) {
			if (atomic_read(&dev->not_port.notify_count) > 0)
				atomic_dec(&dev->not_port.notify_count);
			else {
				pr_err("%s: Invalid count to decrement\n",
					 __func__);
				spin_unlock_irqrestore(&dev->lock, flags);
				return;
			}
			cpkt = list_first_entry(&dev->cpkt_resp_q,
					struct ctrl_pkt, list);
			list_del(&cpkt->list);
			mbim_free_ctrl_pkt(cpkt);
		}
		spin_unlock_irqrestore(&dev->lock, flags);
		pr_debug("ep enqueue error %d\n", ret);
	}
}

static void mbim_connect(struct grmnet *gr)
{
	struct f_mbim			*dev;

	if (!gr) {
		pr_err("%s: Invalid grmnet:%pK\n", __func__, gr);
		return;
	}

	dev = port_to_mbim(gr);

	atomic_set(&dev->ctrl_online, 1);
}

static void mbim_disconnect(struct grmnet *gr)
{
	struct f_mbim			*dev;
	struct usb_cdc_notification	*event;
	int				status;

	if (!gr) {
		pr_err("%s: Invalid grmnet:%pK\n", __func__, gr);
		return;
	}

	dev = port_to_mbim(gr);

	atomic_set(&dev->ctrl_online, 0);

	if (!atomic_read(&dev->online)) {
		pr_debug("%s: nothing to do\n", __func__);
		return;
	}

	usb_ep_fifo_flush(dev->not_port.notify);

	event = dev->not_port.notify_req->buf;
	event->bmRequestType = USB_DIR_IN | USB_TYPE_CLASS
			| USB_RECIP_INTERFACE;
	event->bNotificationType = USB_CDC_NOTIFY_NETWORK_CONNECTION;
	event->wValue = cpu_to_le16(0);
	event->wIndex = cpu_to_le16(dev->ctrl_id);
	event->wLength = cpu_to_le16(0);

	status = usb_ep_queue(dev->not_port.notify, dev->not_port.notify_req,
				GFP_ATOMIC);
	if (status < 0) {
		if (!atomic_read(&dev->online))
			return;
		pr_err("%s: mbim notify ep enqueue error %d\n",
				__func__, status);
	}

	mbim_purge_responses(dev);
}

static int
mbim_send_cpkt_response(void *gr, void *buf, size_t len)
{
	struct f_mbim		*dev;
	struct ctrl_pkt	*cpkt;
	unsigned long		flags;

	if (!gr || !buf) {
		pr_err("%s: Invalid grmnet/buf, grmnet:%pK buf:%pK\n",
				__func__, gr, buf);
		return -ENODEV;
	}
	cpkt = mbim_alloc_ctrl_pkt(len, GFP_ATOMIC);
	if (IS_ERR(cpkt)) {
		pr_err("%s: Unable to allocate ctrl pkt\n", __func__);
		return -ENOMEM;
	}
	memcpy(cpkt->buf, buf, len);
	cpkt->len = len;

	dev = port_to_mbim(gr);

	pr_debug("%s: dev: %pK\n", __func__, dev);
	if (!atomic_read(&dev->online) || !atomic_read(&dev->ctrl_online)) {
		mbim_free_ctrl_pkt(cpkt);
		return 0;
	}

	spin_lock_irqsave(&dev->lock, flags);
	list_add_tail(&cpkt->list, &dev->cpkt_resp_q);
	spin_unlock_irqrestore(&dev->lock, flags);

	mbim_ctrl_response_available(dev);

	return 0;
}

static int mbim_get_status(struct usb_function *f)
{
	enum {
		MBIM_STS_FUNC_WAKEUP_CAP_SHIFT  = 0,
		MBIM_STS_FUNC_WAKEUP_EN_SHIFT   = 1
	};

	unsigned int remote_wakeup_enabled_bit;
	const unsigned int remote_wakeup_capable_bit = 1;

	remote_wakeup_enabled_bit = 0;
	return (remote_wakeup_enabled_bit << MBIM_STS_FUNC_WAKEUP_EN_SHIFT) |
		(remote_wakeup_capable_bit << MBIM_STS_FUNC_WAKEUP_CAP_SHIFT);
}

/*---------------------- function driver setup/binding ---------------------*/

static int
mbim_bind(struct usb_configuration *c, struct usb_function *f)
{
	struct usb_composite_dev	*cdev = c->cdev;
	struct f_mbim			*mbim = func_to_mbim(f);
	int				status;
	struct usb_ep			*ep;
	struct usb_cdc_notification	*event;

	struct f_mbim_opts *opts;

	mbim->cdev = cdev;

	/* maybe allocate device-global string IDs */
	if (mbim_string_defs[0].id == 0) {

		/* control interface label */
		status = usb_string_id(mbim->cdev);
		if (status < 0)
			goto fail;
		mbim_string_defs[STRING_CTRL_IDX].id = status;
		mbim_control_intf.iInterface = status;

		/* data interface label */
		status = usb_string_id(mbim->cdev);
		if (status < 0)
			goto fail;
		mbim_string_defs[STRING_DATA_IDX].id = status;
		mbim_data_nop_intf.iInterface = status;
		mbim_data_intf.iInterface = status;
	}

	/* allocate instance-specific interface IDs */
	status = usb_interface_id(c, f);
	if (status < 0)
		goto fail;
	mbim->ctrl_id = status;
	mbim_iad_desc.bFirstInterface = status;

	mbim_control_intf.bInterfaceNumber = status;
	mbim_union_desc.bMasterInterface0 = status;

	status = usb_interface_id(c, f);
	if (status < 0)
		goto fail;
	mbim->data_id = status;
	mbim->data_interface_up = false;

	mbim_data_nop_intf.bInterfaceNumber = status;
	mbim_data_intf.bInterfaceNumber = status;
	mbim_union_desc.bSlaveInterface0 = status;

	mbim->bam_port.cdev = cdev;
	mbim->bam_port.func = &mbim->function;

	status = -ENODEV;

	/* allocate instance-specific endpoints */
	ep = usb_ep_autoconfig(cdev->gadget, &fs_mbim_in_desc);
	if (!ep) {
		pr_err("usb epin autoconfig failed\n");
		goto fail;
	}
	pr_debug("usb epin autoconfig succeeded\n");
	ep->driver_data = cdev;	/* claim */
	mbim->bam_port.in = ep;

	ep = usb_ep_autoconfig(cdev->gadget, &fs_mbim_out_desc);
	if (!ep) {
		pr_err("usb epout autoconfig failed\n");
		goto fail;
	}

	ep->driver_data = cdev;	/* claim */
	mbim->bam_port.out = ep;

	ep = usb_ep_autoconfig(cdev->gadget, &fs_mbim_notify_desc);
	if (!ep) {
		pr_err("usb notify ep autoconfig failed\n");
		goto fail;
	}

	mbim->not_port.notify = ep;
	ep->driver_data = cdev;	/* claim */

	status = -ENOMEM;

	/* allocate notification request and buffer */
	mbim->not_port.notify_req = mbim_alloc_req(ep, NCM_STATUS_BYTECOUNT);
	if (!mbim->not_port.notify_req)
		goto fail;

	mbim->not_port.notify_req->context = mbim;
	mbim->not_port.notify_req->complete = mbim_notify_complete;
	mbim->not_port.notify_req->length = sizeof(*event);
	event = mbim->not_port.notify_req->buf;
	event->bmRequestType = USB_DIR_IN | USB_TYPE_CLASS
			| USB_RECIP_INTERFACE;
	event->bNotificationType = USB_CDC_NOTIFY_RESPONSE_AVAILABLE;
	event->wValue = cpu_to_le16(0);
	event->wIndex = cpu_to_le16(mbim->ctrl_id);
	event->wLength = cpu_to_le16(0);

	/* copy descriptors, and track endpoint copies */
	f->fs_descriptors = usb_copy_descriptors(mbim_fs_function);
	if (!f->fs_descriptors)
		goto fail;

	/*
	 * support all relevant hardware speeds... we expect that when
	 * hardware is dual speed, all bulk-capable endpoints work at
	 * both speeds
	 */
	if (gadget_is_dualspeed(c->cdev->gadget)) {
		hs_mbim_in_desc.bEndpointAddress =
				fs_mbim_in_desc.bEndpointAddress;
		hs_mbim_out_desc.bEndpointAddress =
				fs_mbim_out_desc.bEndpointAddress;
		hs_mbim_notify_desc.bEndpointAddress =
				fs_mbim_notify_desc.bEndpointAddress;

		/* copy descriptors, and track endpoint copies */
		f->hs_descriptors = usb_copy_descriptors(mbim_hs_function);
		if (!f->hs_descriptors)
			goto fail;
	}

	if (gadget_is_superspeed(c->cdev->gadget)) {
		ss_mbim_in_desc.bEndpointAddress =
				fs_mbim_in_desc.bEndpointAddress;
		ss_mbim_out_desc.bEndpointAddress =
				fs_mbim_out_desc.bEndpointAddress;
		ss_mbim_notify_desc.bEndpointAddress =
				fs_mbim_notify_desc.bEndpointAddress;

		/* copy descriptors, and track endpoint copies */
		f->ss_descriptors = usb_copy_descriptors(mbim_ss_function);
		if (!f->ss_descriptors)
			goto fail;
	}

	if (cdev->use_os_string) {
		f->os_desc_table = kzalloc(sizeof(*f->os_desc_table),
						GFP_KERNEL);
		if (!f->os_desc_table)
			return -ENOMEM;
		opts = container_of(f->fi, struct f_mbim_opts, func_inst);
		f->os_desc_n = 1;
		f->os_desc_table[0].os_desc = &opts->os_desc;
		f->os_desc_table[0].if_id = mbim->data_id;
	}

	pr_debug("mbim(%d): %s speed IN/%s OUT/%s NOTIFY/%s\n",
			mbim->port_num,
			gadget_is_dualspeed(c->cdev->gadget) ? "dual" : "full",
			mbim->bam_port.in->name, mbim->bam_port.out->name,
			mbim->not_port.notify->name);

	return 0;

fail:
	pr_err("%s failed to bind, err %d\n", f->name, status);

	if (f->ss_descriptors)
		usb_free_descriptors(f->ss_descriptors);
	if (f->hs_descriptors)
		usb_free_descriptors(f->hs_descriptors);
	if (f->fs_descriptors)
		usb_free_descriptors(f->fs_descriptors);

	if (mbim->not_port.notify_req) {
		kfree(mbim->not_port.notify_req->buf);
		usb_ep_free_request(mbim->not_port.notify,
				    mbim->not_port.notify_req);
	}

	/* we might as well release our claims on endpoints */
	if (mbim->not_port.notify)
		mbim->not_port.notify->driver_data = NULL;
	if (mbim->bam_port.out)
		mbim->bam_port.out->driver_data = NULL;
	if (mbim->bam_port.in)
		mbim->bam_port.in->driver_data = NULL;

	kfree(f->os_desc_table);

	return status;
}

static void mbim_unbind(struct usb_configuration *c, struct usb_function *f)
{
	struct f_mbim	*mbim = func_to_mbim(f);

	pr_debug("unbinding mbim\n");

	if (gadget_is_superspeed(c->cdev->gadget))
		usb_free_descriptors(f->ss_descriptors);

	if (gadget_is_dualspeed(c->cdev->gadget))
		usb_free_descriptors(f->hs_descriptors);
	usb_free_descriptors(f->fs_descriptors);

	kfree(mbim->not_port.notify_req->buf);
	usb_ep_free_request(mbim->not_port.notify, mbim->not_port.notify_req);

	kfree(f->os_desc_table);
	f->os_desc_table = NULL;
	f->os_desc_n = 0;

	if (mbim->xport == USB_GADGET_XPORT_BAM_DMUX)
		gbam_cleanup(mbim->bam_dmux_func_type);
}

static void mbim_free(struct usb_function *f)
{
	struct f_mbim_opts *opts;

	opts = container_of(f->fi, struct f_mbim_opts, func_inst);
	opts->refcnt--;
}

/**
 * mbim_bind_config - add MBIM link to a configuration
 * @c: the configuration to support the network link
 * Context: single threaded during gadget setup
 * Returns zero on success, else negative errno.
 */
static struct usb_function *mbim_bind_config(struct usb_function_instance *fi,
			unsigned int portno, char *xport_name)
{
	struct f_mbim_opts	*opts;
	struct f_mbim	*mbim = NULL;
	int status = 0;

	pr_debug("port number %u xport name %s\n", portno, xport_name);

	opts = container_of(fi, struct f_mbim_opts, func_inst);
	opts->refcnt++;

	if (portno >= nr_mbim_ports) {
		pr_err("Can not add port %u. Max ports = %d\n",
		       portno, nr_mbim_ports);
		return NULL;
	}

	/* allocate and initialize one new instance */
	mbim = mbim_ports[portno].port;
	if (!mbim) {
		pr_err("mbim struct not allocated\n");
		return NULL;
	}

	mbim_reset_values(mbim);

	mbim->function.name = "usb_mbim";
	mbim->function.strings = mbim_strings;
	mbim->function.bind = mbim_bind;
	mbim->function.unbind = mbim_unbind;
	mbim->function.set_alt = mbim_set_alt;
	mbim->function.get_alt = mbim_get_alt;
	mbim->function.setup = mbim_setup;
	mbim->function.disable = mbim_disable;
	mbim->function.suspend = mbim_suspend;
	mbim->function.func_suspend = mbim_func_suspend;
	mbim->function.get_status = mbim_get_status;
	mbim->function.resume = mbim_resume;
	mbim->function.free_func = mbim_free;
	mbim->port.send_cpkt_response = mbim_send_cpkt_response;
	mbim->port.disconnect = mbim_disconnect;
	mbim->port.connect = mbim_connect;

	if (mbim->xport == USB_GADGET_XPORT_BAM_DMUX) {
		status = gbam_mbim_setup();

		if (status)
			pr_err("%s: bam setup failed with %d\n", __func__,
					status);

	}

	pr_debug("Exit status %d\n", status);

	return &mbim->function;
}

/* ------------ MBIM DRIVER File Operations API for USER SPACE ------------ */

static ssize_t
mbim_read(struct file *fp, char __user *buf, size_t count, loff_t *pos)
{
	struct f_mbim *dev = fp->private_data;
	struct ctrl_pkt *cpkt = NULL;
	unsigned long	flags;
	int ret = 0;

	pr_debug("Enter(%zu)\n", count);

	if (!dev) {
		pr_err("Received NULL mbim pointer\n");
		return -ENODEV;
	}

	if (count > MBIM_BULK_BUFFER_SIZE) {
		pr_err("Buffer size is too big %zu, should be at most %d\n",
			count, MBIM_BULK_BUFFER_SIZE);
		return -EINVAL;
	}

	if (mbim_lock(&dev->read_excl)) {
		pr_err("Previous reading is not finished yet\n");
		return -EBUSY;
	}

	if (atomic_read(&dev->error)) {
		mbim_unlock(&dev->read_excl);
		return -EIO;
	}

	spin_lock_irqsave(&dev->lock, flags);
	while (list_empty(&dev->cpkt_req_q)) {
		pr_debug("Requests list is empty. Wait.\n");
		spin_unlock_irqrestore(&dev->lock, flags);
		ret = wait_event_interruptible(dev->read_wq,
			!list_empty(&dev->cpkt_req_q));
		if (ret < 0) {
			pr_err("Waiting failed\n");
			mbim_unlock(&dev->read_excl);
			return -ERESTARTSYS;
		}
		pr_debug("Received request packet\n");
		spin_lock_irqsave(&dev->lock, flags);
	}

	cpkt = list_first_entry(&dev->cpkt_req_q, struct ctrl_pkt,
							list);
	if (cpkt->len > count) {
		spin_unlock_irqrestore(&dev->lock, flags);
		mbim_unlock(&dev->read_excl);
		pr_err("cpkt size too big:%d > buf size:%zu\n",
				cpkt->len, count);
		return -ENOMEM;
	}

	pr_debug("cpkt size:%d\n", cpkt->len);

	list_del(&cpkt->list);
	spin_unlock_irqrestore(&dev->lock, flags);
	mbim_unlock(&dev->read_excl);

	ret = copy_to_user(buf, cpkt->buf, cpkt->len);
	if (ret) {
		pr_err("copy_to_user failed: err %d\n", ret);
		ret = -ENOMEM;
	} else {
		pr_debug("copied %d bytes to user\n", cpkt->len);
		ret = cpkt->len;
	}

	mbim_free_ctrl_pkt(cpkt);

	return ret;
}

static ssize_t
mbim_write(struct file *fp, const char __user *buf, size_t count, loff_t *pos)
{
	struct f_mbim *dev = fp->private_data;
	struct ctrl_pkt *cpkt = NULL;
	struct usb_request *req = dev->not_port.notify_req;
	int ret = 0;
	unsigned long flags;
	struct usb_cdc_notification	*event;

	pr_debug("Enter(%zu)\n", count);

	if (!dev || !req || !req->buf) {
		pr_err("%s: dev %pK req %pK req->buf %pK\n",
			__func__, dev, req, req ? req->buf : req);
		return -ENODEV;
	}

	if (!count || count > MAX_CTRL_PKT_SIZE) {
		pr_err("error: ctrl pkt length %zu\n", count);
		return -EINVAL;
	}

	if (mbim_lock(&dev->write_excl)) {
		pr_err("Previous writing not finished yet\n");
		return -EBUSY;
	}

	if (!atomic_read(&dev->online)) {
		pr_err("USB cable not connected\n");
		mbim_unlock(&dev->write_excl);
		return -EPIPE;
	}

	if (dev->not_port.notify_state != MBIM_NOTIFY_RESPONSE_AVAILABLE) {
		pr_err("dev:%pK state=%d error\n", dev,
			dev->not_port.notify_state);
		mbim_unlock(&dev->write_excl);
		return -EINVAL;
	}


	cpkt = mbim_alloc_ctrl_pkt(count, GFP_KERNEL);
	if (!cpkt) {
		pr_err("failed to allocate ctrl pkt\n");
		mbim_unlock(&dev->write_excl);
		return -ENOMEM;
	}

	ret = copy_from_user(cpkt->buf, buf, count);
	if (ret) {
		pr_err("copy_from_user failed err:%d\n", ret);
		mbim_free_ctrl_pkt(cpkt);
		mbim_unlock(&dev->write_excl);
		return ret;
	}

	spin_lock_irqsave(&dev->lock, flags);
	list_add_tail(&cpkt->list, &dev->cpkt_resp_q);

	if (atomic_inc_return(&dev->not_port.notify_count) != 1) {
		pr_debug("delay ep_queue: notifications queue is busy[%d]\n",
			atomic_read(&dev->not_port.notify_count));
		spin_unlock_irqrestore(&dev->lock, flags);
		mbim_unlock(&dev->write_excl);
		return count;
	}

	dev->not_port.notify_req->context = dev;
	dev->not_port.notify_req->complete = mbim_notify_complete;
	dev->not_port.notify_req->length = sizeof(*event);
	event = dev->not_port.notify_req->buf;
	event->bmRequestType = USB_DIR_IN | USB_TYPE_CLASS
			| USB_RECIP_INTERFACE;
	event->bNotificationType = USB_CDC_NOTIFY_RESPONSE_AVAILABLE;
	event->wValue = cpu_to_le16(0);
	event->wIndex = cpu_to_le16(dev->ctrl_id);
	event->wLength = cpu_to_le16(0);

	spin_unlock_irqrestore(&dev->lock, flags);

	ret = usb_ep_queue(dev->not_port.notify, req, GFP_ATOMIC);
	if (ret == -EOPNOTSUPP || (ret < 0 && ret != -EAGAIN && ret != EBUSY)) {
		spin_lock_irqsave(&dev->lock, flags);
		/* check if device disconnected while we dropped lock */
		if (atomic_read(&dev->online)) {
			list_del(&cpkt->list);
			atomic_dec(&dev->not_port.notify_count);
			mbim_free_ctrl_pkt(cpkt);
		}
		dev->cpkt_drop_cnt++;
		spin_unlock_irqrestore(&dev->lock, flags);
		pr_err("drop ctrl pkt of len %d error %d\n", cpkt->len, ret);
	} else {
		ret = 0;
	}
	mbim_unlock(&dev->write_excl);

	pr_debug("Exit(%zu)\n", count);

	return ret ? ret : count;
}

static int mbim_open(struct inode *ip, struct file *fp)
{
	while (!_mbim_dev) {
		pr_err("mbim_dev not created yet\n");
		return -ENODEV;
	}

	if (mbim_lock(&_mbim_dev->open_excl)) {
		pr_err("Already opened\n");
		return -EBUSY;
	}

	if (!atomic_read(&_mbim_dev->online))
		pr_err("USB cable not connected\n");

	fp->private_data = _mbim_dev;

	atomic_set(&_mbim_dev->error, 0);
	return 0;
}

static int mbim_release(struct inode *ip, struct file *fp)
{
	mbim_unlock(&_mbim_dev->open_excl);

	return 0;
}

#define BAM_DMUX_CHANNEL_ID 8
static long mbim_ioctl(struct file *fp, unsigned int cmd, unsigned long arg)
{
	struct f_mbim *mbim = fp->private_data;
	struct mbim_ipa_ep_info info;
	int ret = 0;

	pr_debug("Received command %d\n", cmd);

	if (!mbim) {
		pr_err("Bad parameter\n");
		return -EINVAL;
	}

	if (mbim_lock(&mbim->ioctl_excl))
		return -EBUSY;

	switch (cmd) {
	case MBIM_GET_NTB_SIZE:
		ret = copy_to_user((void __user *)arg,
			&mbim->ntb_input_size, sizeof(mbim->ntb_input_size));
		if (ret) {
			pr_err("copying to user space failed\n");
			ret = -EFAULT;
		}
		pr_debug("Sent NTB size %d\n", mbim->ntb_input_size);
		break;
	case MBIM_GET_DATAGRAM_COUNT:
		ret = copy_to_user((void __user *)arg,
			&mbim->ntb_max_datagrams,
			sizeof(mbim->ntb_max_datagrams));
		if (ret) {
			pr_err("copying to user space failed\n");
			ret = -EFAULT;
		}
		pr_debug("Sent NTB datagrams count %d\n",
			mbim->ntb_max_datagrams);
		break;

	case MBIM_EP_LOOKUP:
		if (!atomic_read(&mbim->online)) {
			pr_warn("usb cable is not connected\n");
			mbim_unlock(&mbim->ioctl_excl);
			return -ENOTCONN;
		}

		switch (mbim->xport) {
		case USB_GADGET_XPORT_BAM_DMUX:
			/*
			 * Rmnet and MBIM share the same BAM-DMUX channel.
			 * This channel number 8 should be in sync with
			 * the one defined in u_bam.c.
			 */
			info.ph_ep_info.ep_type = MBIM_DATA_EP_TYPE_BAM_DMUX;
			info.ph_ep_info.peripheral_iface_id =
						BAM_DMUX_CHANNEL_ID;
			info.ipa_ep_pair.cons_pipe_num = 0;
			info.ipa_ep_pair.prod_pipe_num = 0;
			break;
		case USB_GADGET_XPORT_BAM2BAM_IPA:
			break;
		default:
			ret = -ENODEV;
			pr_err("unknown transport\n");
			goto fail;
		}

		ret = copy_to_user((void __user *)arg, &info,
			sizeof(info));
		if (ret) {
			pr_err("copying to user space failed\n");
			ret = -EFAULT;
		}
		break;

	default:
		pr_err("wrong parameter\n");
		ret = -EINVAL;
	}

fail:
	mbim_unlock(&mbim->ioctl_excl);

	return ret;
}

#define MBIM_MODULE_NAME "android_mbim"

/* file operations for MBIM device /dev/android_mbim */
static const struct file_operations mbim_fops = {
	.owner = THIS_MODULE,
	.open = mbim_open,
	.release = mbim_release,
	.read = mbim_read,
	.write = mbim_write,
	.unlocked_ioctl	= mbim_ioctl,
};

static int mbim_chrdev_init(struct f_mbim *dev)
{
	int ret;

	dev->mbim_class = class_create(THIS_MODULE, MBIM_MODULE_NAME);
	if (IS_ERR(dev->mbim_class)) {
		pr_err("class_create() failed ENOMEM\n");
		ret = -ENOMEM;
		return ret;
	}
	ret = alloc_chrdev_region(&dev->devno, 0, 1, MBIM_MODULE_NAME);
	if (ret < 0) {
		pr_err("alloc_chrdev_region() failed ret:%i\n", ret);
		class_destroy(dev->mbim_class);
		return ret;
	}

	dev->dev = device_create(dev->mbim_class, NULL, dev->devno,
				dev->dev, MBIM_MODULE_NAME);
	if (IS_ERR(dev->dev)) {
		pr_err("device_create() failed for port\n");
		ret = -ENOMEM;
		class_destroy(dev->mbim_class);
		unregister_chrdev_region(dev->devno, 1);
		return ret;
	}

	cdev_init(&dev->mbim_cdev, &mbim_fops);

	dev->mbim_cdev.owner = THIS_MODULE;

	ret = cdev_add(&dev->mbim_cdev, dev->devno, 1);
	if (ret < 0) {
		pr_err("cdev_add() failed ret:%d\n", ret);
		class_destroy(dev->mbim_class);
		unregister_chrdev_region(dev->devno, 1);
		cdev_del(&dev->mbim_cdev);
		return ret;
	}

	return ret;
}

static void mbim_chrdev_cleanup(struct f_mbim *dev)
{

	class_destroy(dev->mbim_class);
	unregister_chrdev_region(dev->devno, 1);
	cdev_del(&dev->mbim_cdev);

}

static int mbim_init(int instances)
{
	int i;
	struct f_mbim *dev = NULL;
	int ret;

	pr_debug("%s: initialize %d instances\n", __func__, instances);

	if (instances > NR_MBIM_PORTS) {
		pr_err("Max-%d instances supported\n", NR_MBIM_PORTS);
		return -EINVAL;
	}

	for (i = 0; i < instances; i++) {
		dev = kzalloc(sizeof(struct f_mbim), GFP_KERNEL);
		if (!dev) {
			ret = -ENOMEM;
			goto fail_probe;
		}

		dev->port_num = i;
		dev->bam_port.ipa_consumer_ep = -1;
		dev->bam_port.ipa_producer_ep = -1;

		INIT_LIST_HEAD(&dev->cpkt_req_q);
		INIT_LIST_HEAD(&dev->cpkt_resp_q);
		spin_lock_init(&dev->lock);

		mbim_ports[i].port = dev;
		mbim_ports[i].port_num = i;

		init_waitqueue_head(&dev->read_wq);

		atomic_set(&dev->open_excl, 0);
		atomic_set(&dev->ioctl_excl, 0);
		atomic_set(&dev->read_excl, 0);
		atomic_set(&dev->write_excl, 0);

		nr_mbim_ports++;

	}

	_mbim_dev = dev;
	ret = mbim_chrdev_init(dev);
	if (ret) {
		pr_err("mbim driver failed to register\n");
		goto fail_probe;
	}

	pr_debug("%s : Initialized %d ports\n", __func__, nr_mbim_ports);

	return ret;

fail_probe:
	pr_err("%s : Failed\n", __func__);
	for (i = 0; i < nr_mbim_ports; i++) {
		kfree(mbim_ports[i].port);
		mbim_ports[i].port = NULL;
	}

	return ret;
}

static void fmbim_cleanup(void)
{
	int i = 0;

	for (i = 0; i < nr_mbim_ports; i++) {
		kfree(mbim_ports[i].port);
		mbim_ports[i].port = NULL;
	}
	nr_mbim_ports = 0;

	mbim_chrdev_cleanup(_mbim_dev);

	_mbim_dev = NULL;
}

static void mbim_free_inst(struct usb_function_instance *f)
{
	struct f_mbim_opts *opts = container_of(f, struct f_mbim_opts,
						func_inst);

	if (opts && opts->interf_group)
		kfree(opts->interf_group);

	kfree(opts->usb_mbim);
	kfree(opts);
}

static int name_to_prot(struct f_mbim *dev, const char *name)
{
	if (!name)
		goto error;

	if (!strncasecmp("rmnet", name, MAX_INST_NAME_LEN)) {
		dev->xport = USB_GADGET_XPORT_BAM2BAM;
	} else if (!strncasecmp("dpl", name, MAX_INST_NAME_LEN)) {
		dev->xport = USB_GADGET_XPORT_BAM2BAM_IPA;
	} else if (!strncasecmp("rmnet_bam_dmux", name, MAX_INST_NAME_LEN)) {
		dev->xport = USB_GADGET_XPORT_BAM_DMUX;
		dev->bam_dmux_func_type = BAM_DMUX_FUNC_RMNET;
	} else if (!strncasecmp("dpl_bam_dmux", name, MAX_INST_NAME_LEN)) {
		dev->xport = USB_GADGET_XPORT_BAM_DMUX;
		dev->bam_dmux_func_type = BAM_DMUX_FUNC_DPL;
	} else if (!strncasecmp("mbim", name, MAX_INST_NAME_LEN)) {
		dev->xport = USB_GADGET_XPORT_BAM_DMUX;
		dev->bam_dmux_func_type = BAM_DMUX_FUNC_MBIM;
	}

	return 0;

error:
	return -EINVAL;
}

static int mbim_set_inst_name(struct usb_function_instance *fi,
		const char *name)
{
	int name_len, ret = 0;
	struct f_mbim *dev;
	struct f_mbim_opts *opts = container_of(fi,
					struct f_mbim_opts, func_inst);

	struct usb_os_desc *descs[1];
	char *names[1];

	name_len = strlen(name) + 1;
	if (name_len > MAX_INST_NAME_LEN)
		return -ENAMETOOLONG;

	dev = _mbim_dev;

	ret = name_to_prot(dev, name);
	if (ret < 0) {
		pr_err("%s: failed to find prot for %s instance\n",
		__func__, name);
		goto fail;
	}

	opts->os_desc.ext_compat_id = opts->ext_compat_id;
	INIT_LIST_HEAD(&opts->os_desc.ext_prop);
	descs[0] = &opts->os_desc;
	names[0] = "MBIM";
	opts->interf_group = usb_os_desc_prepare_interf_dir(
					&opts->func_inst.group, 1,
					descs, names, THIS_MODULE);

	opts->usb_mbim = dev;
	return 0;

fail:
	pr_err("%s: failed to find prot for %s instance\n", __func__, name);
	kfree(dev);
	return ret;
}

static inline struct f_mbim_opts *to_f_mbim_opts(struct config_item *item)
{
	return container_of(to_config_group(item), struct f_mbim_opts,
				func_inst.group);
}

static void mbim_opts_release(struct config_item *item)
{
	struct f_mbim_opts *opts = to_f_mbim_opts(item);

	usb_put_function_instance(&opts->func_inst);
};

static struct configfs_item_operations mbim_item_ops = {
	.release = mbim_opts_release,
};

static struct config_item_type mbim_func_type = {
	.ct_item_ops    = &mbim_item_ops,
	.ct_owner       = THIS_MODULE,
};

static struct usb_function_instance *mbim_alloc_inst(void)
{
	struct f_mbim_opts *opts;

	opts = kzalloc(sizeof(*opts), GFP_KERNEL);
	if (!opts)
		return ERR_PTR(-ENOMEM);

	opts->func_inst.set_inst_name = mbim_set_inst_name;
	opts->func_inst.free_func_inst = mbim_free_inst;

	config_group_init_type_name(&opts->func_inst.group, "",
				&mbim_func_type);
	return &opts->func_inst;
}

static struct usb_function *mbim_alloc(struct usb_function_instance *fi)
{
	return mbim_bind_config(fi, 0, "mbim");
}

DECLARE_USB_FUNCTION(mbim, mbim_alloc_inst, mbim_alloc);

static int __init usb_mbim_init(void)
{
	int ret;

	ret = mbim_init(MAX_MBIM_INSTANCES);
	if (!ret) {
		ret = usb_function_register(&mbimusb_func);
		if (ret) {
			pr_err("%s: failed to register mbim %d\n",
					__func__, ret);
			return ret;
		}
	}
	return ret;
}

static void __exit usb_mbim_exit(void)
{
	usb_function_unregister(&mbimusb_func);
	fmbim_cleanup();
}

module_init(usb_mbim_init);
module_exit(usb_mbim_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("USB MBIM Function Driver");
