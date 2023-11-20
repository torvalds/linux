// SPDX-License-Identifier: GPL-2.0+
/*
 * f_ncm.c -- USB CDC Network (NCM) link function driver
 *
 * Copyright (C) 2010 Nokia Corporation
 * Contact: Yauheni Kaliuta <yauheni.kaliuta@nokia.com>
 *
 * The driver borrows from f_ecm.c which is:
 *
 * Copyright (C) 2003-2005,2008 David Brownell
 * Copyright (C) 2008 Nokia Corporation
 */

#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/etherdevice.h>
#include <linux/crc32.h>

#include <linux/usb/cdc.h>

#include "u_ether.h"
#include "u_ether_configfs.h"
#include "u_ncm.h"
#include "configfs.h"

/*
 * This function is a "CDC Network Control Model" (CDC NCM) Ethernet link.
 * NCM is intended to be used with high-speed network attachments.
 *
 * Note that NCM requires the use of "alternate settings" for its data
 * interface.  This means that the set_alt() method has real work to do,
 * and also means that a get_alt() method is required.
 */

/* to trigger crc/non-crc ndp signature */

#define NCM_NDP_HDR_CRC		0x01000000

enum ncm_notify_state {
	NCM_NOTIFY_NONE,		/* don't notify */
	NCM_NOTIFY_CONNECT,		/* issue CONNECT next */
	NCM_NOTIFY_SPEED,		/* issue SPEED_CHANGE next */
};

struct f_ncm {
	struct gether			port;
	u8				ctrl_id, data_id;

	char				ethaddr[14];

	struct usb_ep			*notify;
	struct usb_request		*notify_req;
	u8				notify_state;
	atomic_t			notify_count;
	bool				is_open;

	const struct ndp_parser_opts	*parser_opts;
	bool				is_crc;
	u32				ndp_sign;

	/*
	 * for notification, it is accessed from both
	 * callback and ethernet open/close
	 */
	spinlock_t			lock;

	struct net_device		*netdev;

	/* For multi-frame NDP TX */
	struct sk_buff			*skb_tx_data;
	struct sk_buff			*skb_tx_ndp;
	u16				ndp_dgram_count;
	struct hrtimer			task_timer;
};

static inline struct f_ncm *func_to_ncm(struct usb_function *f)
{
	return container_of(f, struct f_ncm, port.func);
}

/*-------------------------------------------------------------------------*/

/*
 * We cannot group frames so use just the minimal size which ok to put
 * one max-size ethernet frame.
 * If the host can group frames, allow it to do that, 16K is selected,
 * because it's used by default by the current linux host driver
 */
#define NTB_DEFAULT_IN_SIZE	16384
#define NTB_OUT_SIZE		16384

/* Allocation for storing the NDP, 32 should suffice for a
 * 16k packet. This allows a maximum of 32 * 507 Byte packets to
 * be transmitted in a single 16kB skb, though when sending full size
 * packets this limit will be plenty.
 * Smaller packets are not likely to be trying to maximize the
 * throughput and will be mstly sending smaller infrequent frames.
 */
#define TX_MAX_NUM_DPE		32

/* Delay for the transmit to wait before sending an unfilled NTB frame. */
#define TX_TIMEOUT_NSECS	300000

#define FORMATS_SUPPORTED	(USB_CDC_NCM_NTB16_SUPPORTED |	\
				 USB_CDC_NCM_NTB32_SUPPORTED)

static struct usb_cdc_ncm_ntb_parameters ntb_parameters = {
	.wLength = cpu_to_le16(sizeof(ntb_parameters)),
	.bmNtbFormatsSupported = cpu_to_le16(FORMATS_SUPPORTED),
	.dwNtbInMaxSize = cpu_to_le32(NTB_DEFAULT_IN_SIZE),
	.wNdpInDivisor = cpu_to_le16(4),
	.wNdpInPayloadRemainder = cpu_to_le16(0),
	.wNdpInAlignment = cpu_to_le16(4),

	.dwNtbOutMaxSize = cpu_to_le32(NTB_OUT_SIZE),
	.wNdpOutDivisor = cpu_to_le16(4),
	.wNdpOutPayloadRemainder = cpu_to_le16(0),
	.wNdpOutAlignment = cpu_to_le16(4),
};

/*
 * Use wMaxPacketSize big enough to fit CDC_NOTIFY_SPEED_CHANGE in one
 * packet, to simplify cancellation; and a big transfer interval, to
 * waste less bandwidth.
 */

#define NCM_STATUS_INTERVAL_MS		32
#define NCM_STATUS_BYTECOUNT		16	/* 8 byte header + data */

static struct usb_interface_assoc_descriptor ncm_iad_desc = {
	.bLength =		sizeof ncm_iad_desc,
	.bDescriptorType =	USB_DT_INTERFACE_ASSOCIATION,

	/* .bFirstInterface =	DYNAMIC, */
	.bInterfaceCount =	2,	/* control + data */
	.bFunctionClass =	USB_CLASS_COMM,
	.bFunctionSubClass =	USB_CDC_SUBCLASS_NCM,
	.bFunctionProtocol =	USB_CDC_PROTO_NONE,
	/* .iFunction =		DYNAMIC */
};

/* interface descriptor: */

static struct usb_interface_descriptor ncm_control_intf = {
	.bLength =		sizeof ncm_control_intf,
	.bDescriptorType =	USB_DT_INTERFACE,

	/* .bInterfaceNumber = DYNAMIC */
	.bNumEndpoints =	1,
	.bInterfaceClass =	USB_CLASS_COMM,
	.bInterfaceSubClass =	USB_CDC_SUBCLASS_NCM,
	.bInterfaceProtocol =	USB_CDC_PROTO_NONE,
	/* .iInterface = DYNAMIC */
};

static struct usb_cdc_header_desc ncm_header_desc = {
	.bLength =		sizeof ncm_header_desc,
	.bDescriptorType =	USB_DT_CS_INTERFACE,
	.bDescriptorSubType =	USB_CDC_HEADER_TYPE,

	.bcdCDC =		cpu_to_le16(0x0110),
};

static struct usb_cdc_union_desc ncm_union_desc = {
	.bLength =		sizeof(ncm_union_desc),
	.bDescriptorType =	USB_DT_CS_INTERFACE,
	.bDescriptorSubType =	USB_CDC_UNION_TYPE,
	/* .bMasterInterface0 =	DYNAMIC */
	/* .bSlaveInterface0 =	DYNAMIC */
};

static struct usb_cdc_ether_desc ecm_desc = {
	.bLength =		sizeof ecm_desc,
	.bDescriptorType =	USB_DT_CS_INTERFACE,
	.bDescriptorSubType =	USB_CDC_ETHERNET_TYPE,

	/* this descriptor actually adds value, surprise! */
	/* .iMACAddress = DYNAMIC */
	.bmEthernetStatistics =	cpu_to_le32(0), /* no statistics */
	.wMaxSegmentSize =	cpu_to_le16(ETH_FRAME_LEN),
	.wNumberMCFilters =	cpu_to_le16(0),
	.bNumberPowerFilters =	0,
};

#define NCAPS	(USB_CDC_NCM_NCAP_ETH_FILTER | USB_CDC_NCM_NCAP_CRC_MODE)

static struct usb_cdc_ncm_desc ncm_desc = {
	.bLength =		sizeof ncm_desc,
	.bDescriptorType =	USB_DT_CS_INTERFACE,
	.bDescriptorSubType =	USB_CDC_NCM_TYPE,

	.bcdNcmVersion =	cpu_to_le16(0x0100),
	/* can process SetEthernetPacketFilter */
	.bmNetworkCapabilities = NCAPS,
};

/* the default data interface has no endpoints ... */

static struct usb_interface_descriptor ncm_data_nop_intf = {
	.bLength =		sizeof ncm_data_nop_intf,
	.bDescriptorType =	USB_DT_INTERFACE,

	.bInterfaceNumber =	1,
	.bAlternateSetting =	0,
	.bNumEndpoints =	0,
	.bInterfaceClass =	USB_CLASS_CDC_DATA,
	.bInterfaceSubClass =	0,
	.bInterfaceProtocol =	USB_CDC_NCM_PROTO_NTB,
	/* .iInterface = DYNAMIC */
};

/* ... but the "real" data interface has two bulk endpoints */

static struct usb_interface_descriptor ncm_data_intf = {
	.bLength =		sizeof ncm_data_intf,
	.bDescriptorType =	USB_DT_INTERFACE,

	.bInterfaceNumber =	1,
	.bAlternateSetting =	1,
	.bNumEndpoints =	2,
	.bInterfaceClass =	USB_CLASS_CDC_DATA,
	.bInterfaceSubClass =	0,
	.bInterfaceProtocol =	USB_CDC_NCM_PROTO_NTB,
	/* .iInterface = DYNAMIC */
};

/* full speed support: */

static struct usb_endpoint_descriptor fs_ncm_notify_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,

	.bEndpointAddress =	USB_DIR_IN,
	.bmAttributes =		USB_ENDPOINT_XFER_INT,
	.wMaxPacketSize =	cpu_to_le16(NCM_STATUS_BYTECOUNT),
	.bInterval =		NCM_STATUS_INTERVAL_MS,
};

static struct usb_endpoint_descriptor fs_ncm_in_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,

	.bEndpointAddress =	USB_DIR_IN,
	.bmAttributes =		USB_ENDPOINT_XFER_BULK,
};

static struct usb_endpoint_descriptor fs_ncm_out_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,

	.bEndpointAddress =	USB_DIR_OUT,
	.bmAttributes =		USB_ENDPOINT_XFER_BULK,
};

static struct usb_descriptor_header *ncm_fs_function[] = {
	(struct usb_descriptor_header *) &ncm_iad_desc,
	/* CDC NCM control descriptors */
	(struct usb_descriptor_header *) &ncm_control_intf,
	(struct usb_descriptor_header *) &ncm_header_desc,
	(struct usb_descriptor_header *) &ncm_union_desc,
	(struct usb_descriptor_header *) &ecm_desc,
	(struct usb_descriptor_header *) &ncm_desc,
	(struct usb_descriptor_header *) &fs_ncm_notify_desc,
	/* data interface, altsettings 0 and 1 */
	(struct usb_descriptor_header *) &ncm_data_nop_intf,
	(struct usb_descriptor_header *) &ncm_data_intf,
	(struct usb_descriptor_header *) &fs_ncm_in_desc,
	(struct usb_descriptor_header *) &fs_ncm_out_desc,
	NULL,
};

/* high speed support: */

static struct usb_endpoint_descriptor hs_ncm_notify_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,

	.bEndpointAddress =	USB_DIR_IN,
	.bmAttributes =		USB_ENDPOINT_XFER_INT,
	.wMaxPacketSize =	cpu_to_le16(NCM_STATUS_BYTECOUNT),
	.bInterval =		USB_MS_TO_HS_INTERVAL(NCM_STATUS_INTERVAL_MS),
};
static struct usb_endpoint_descriptor hs_ncm_in_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,

	.bEndpointAddress =	USB_DIR_IN,
	.bmAttributes =		USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize =	cpu_to_le16(512),
};

static struct usb_endpoint_descriptor hs_ncm_out_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,

	.bEndpointAddress =	USB_DIR_OUT,
	.bmAttributes =		USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize =	cpu_to_le16(512),
};

static struct usb_descriptor_header *ncm_hs_function[] = {
	(struct usb_descriptor_header *) &ncm_iad_desc,
	/* CDC NCM control descriptors */
	(struct usb_descriptor_header *) &ncm_control_intf,
	(struct usb_descriptor_header *) &ncm_header_desc,
	(struct usb_descriptor_header *) &ncm_union_desc,
	(struct usb_descriptor_header *) &ecm_desc,
	(struct usb_descriptor_header *) &ncm_desc,
	(struct usb_descriptor_header *) &hs_ncm_notify_desc,
	/* data interface, altsettings 0 and 1 */
	(struct usb_descriptor_header *) &ncm_data_nop_intf,
	(struct usb_descriptor_header *) &ncm_data_intf,
	(struct usb_descriptor_header *) &hs_ncm_in_desc,
	(struct usb_descriptor_header *) &hs_ncm_out_desc,
	NULL,
};


/* super speed support: */

static struct usb_endpoint_descriptor ss_ncm_notify_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,

	.bEndpointAddress =	USB_DIR_IN,
	.bmAttributes =		USB_ENDPOINT_XFER_INT,
	.wMaxPacketSize =	cpu_to_le16(NCM_STATUS_BYTECOUNT),
	.bInterval =		USB_MS_TO_HS_INTERVAL(NCM_STATUS_INTERVAL_MS)
};

static struct usb_ss_ep_comp_descriptor ss_ncm_notify_comp_desc = {
	.bLength =		sizeof(ss_ncm_notify_comp_desc),
	.bDescriptorType =	USB_DT_SS_ENDPOINT_COMP,

	/* the following 3 values can be tweaked if necessary */
	/* .bMaxBurst =		0, */
	/* .bmAttributes =	0, */
	.wBytesPerInterval =	cpu_to_le16(NCM_STATUS_BYTECOUNT),
};

static struct usb_endpoint_descriptor ss_ncm_in_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,

	.bEndpointAddress =	USB_DIR_IN,
	.bmAttributes =		USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize =	cpu_to_le16(1024),
};

static struct usb_endpoint_descriptor ss_ncm_out_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,

	.bEndpointAddress =	USB_DIR_OUT,
	.bmAttributes =		USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize =	cpu_to_le16(1024),
};

static struct usb_ss_ep_comp_descriptor ss_ncm_bulk_comp_desc = {
	.bLength =		sizeof(ss_ncm_bulk_comp_desc),
	.bDescriptorType =	USB_DT_SS_ENDPOINT_COMP,

	/* the following 2 values can be tweaked if necessary */
	.bMaxBurst =		15,
	/* .bmAttributes =	0, */
};

static struct usb_descriptor_header *ncm_ss_function[] = {
	(struct usb_descriptor_header *) &ncm_iad_desc,
	/* CDC NCM control descriptors */
	(struct usb_descriptor_header *) &ncm_control_intf,
	(struct usb_descriptor_header *) &ncm_header_desc,
	(struct usb_descriptor_header *) &ncm_union_desc,
	(struct usb_descriptor_header *) &ecm_desc,
	(struct usb_descriptor_header *) &ncm_desc,
	(struct usb_descriptor_header *) &ss_ncm_notify_desc,
	(struct usb_descriptor_header *) &ss_ncm_notify_comp_desc,
	/* data interface, altsettings 0 and 1 */
	(struct usb_descriptor_header *) &ncm_data_nop_intf,
	(struct usb_descriptor_header *) &ncm_data_intf,
	(struct usb_descriptor_header *) &ss_ncm_in_desc,
	(struct usb_descriptor_header *) &ss_ncm_bulk_comp_desc,
	(struct usb_descriptor_header *) &ss_ncm_out_desc,
	(struct usb_descriptor_header *) &ss_ncm_bulk_comp_desc,
	NULL,
};

/* string descriptors: */

#define STRING_CTRL_IDX	0
#define STRING_MAC_IDX	1
#define STRING_DATA_IDX	2
#define STRING_IAD_IDX	3

static struct usb_string ncm_string_defs[] = {
	[STRING_CTRL_IDX].s = "CDC Network Control Model (NCM)",
	[STRING_MAC_IDX].s = "",
	[STRING_DATA_IDX].s = "CDC Network Data",
	[STRING_IAD_IDX].s = "CDC NCM",
	{  } /* end of list */
};

static struct usb_gadget_strings ncm_string_table = {
	.language =		0x0409,	/* en-us */
	.strings =		ncm_string_defs,
};

static struct usb_gadget_strings *ncm_strings[] = {
	&ncm_string_table,
	NULL,
};

/*
 * Here are options for NCM Datagram Pointer table (NDP) parser.
 * There are 2 different formats: NDP16 and NDP32 in the spec (ch. 3),
 * in NDP16 offsets and sizes fields are 1 16bit word wide,
 * in NDP32 -- 2 16bit words wide. Also signatures are different.
 * To make the parser code the same, put the differences in the structure,
 * and switch pointers to the structures when the format is changed.
 */

struct ndp_parser_opts {
	u32		nth_sign;
	u32		ndp_sign;
	unsigned	nth_size;
	unsigned	ndp_size;
	unsigned	dpe_size;
	unsigned	ndplen_align;
	/* sizes in u16 units */
	unsigned	dgram_item_len; /* index or length */
	unsigned	block_length;
	unsigned	ndp_index;
	unsigned	reserved1;
	unsigned	reserved2;
	unsigned	next_ndp_index;
};

static const struct ndp_parser_opts ndp16_opts = {
	.nth_sign = USB_CDC_NCM_NTH16_SIGN,
	.ndp_sign = USB_CDC_NCM_NDP16_NOCRC_SIGN,
	.nth_size = sizeof(struct usb_cdc_ncm_nth16),
	.ndp_size = sizeof(struct usb_cdc_ncm_ndp16),
	.dpe_size = sizeof(struct usb_cdc_ncm_dpe16),
	.ndplen_align = 4,
	.dgram_item_len = 1,
	.block_length = 1,
	.ndp_index = 1,
	.reserved1 = 0,
	.reserved2 = 0,
	.next_ndp_index = 1,
};

static const struct ndp_parser_opts ndp32_opts = {
	.nth_sign = USB_CDC_NCM_NTH32_SIGN,
	.ndp_sign = USB_CDC_NCM_NDP32_NOCRC_SIGN,
	.nth_size = sizeof(struct usb_cdc_ncm_nth32),
	.ndp_size = sizeof(struct usb_cdc_ncm_ndp32),
	.dpe_size = sizeof(struct usb_cdc_ncm_dpe32),
	.ndplen_align = 8,
	.dgram_item_len = 2,
	.block_length = 2,
	.ndp_index = 2,
	.reserved1 = 1,
	.reserved2 = 2,
	.next_ndp_index = 2,
};

static inline void put_ncm(__le16 **p, unsigned size, unsigned val)
{
	switch (size) {
	case 1:
		put_unaligned_le16((u16)val, *p);
		break;
	case 2:
		put_unaligned_le32((u32)val, *p);

		break;
	default:
		BUG();
	}

	*p += size;
}

static inline unsigned get_ncm(__le16 **p, unsigned size)
{
	unsigned tmp;

	switch (size) {
	case 1:
		tmp = get_unaligned_le16(*p);
		break;
	case 2:
		tmp = get_unaligned_le32(*p);
		break;
	default:
		BUG();
	}

	*p += size;
	return tmp;
}

/*-------------------------------------------------------------------------*/

static inline void ncm_reset_values(struct f_ncm *ncm)
{
	ncm->parser_opts = &ndp16_opts;
	ncm->is_crc = false;
	ncm->ndp_sign = ncm->parser_opts->ndp_sign;
	ncm->port.cdc_filter = DEFAULT_FILTER;

	/* doesn't make sense for ncm, fixed size used */
	ncm->port.header_len = 0;

	ncm->port.fixed_out_len = le32_to_cpu(ntb_parameters.dwNtbOutMaxSize);
	ncm->port.fixed_in_len = NTB_DEFAULT_IN_SIZE;
}

/*
 * Context: ncm->lock held
 */
static void ncm_do_notify(struct f_ncm *ncm)
{
	struct usb_request		*req = ncm->notify_req;
	struct usb_cdc_notification	*event;
	struct usb_composite_dev	*cdev = ncm->port.func.config->cdev;
	__le32				*data;
	int				status;

	/* notification already in flight? */
	if (atomic_read(&ncm->notify_count))
		return;

	event = req->buf;
	switch (ncm->notify_state) {
	case NCM_NOTIFY_NONE:
		return;

	case NCM_NOTIFY_CONNECT:
		event->bNotificationType = USB_CDC_NOTIFY_NETWORK_CONNECTION;
		if (ncm->is_open)
			event->wValue = cpu_to_le16(1);
		else
			event->wValue = cpu_to_le16(0);
		event->wLength = 0;
		req->length = sizeof *event;

		DBG(cdev, "notify connect %s\n",
				ncm->is_open ? "true" : "false");
		ncm->notify_state = NCM_NOTIFY_NONE;
		break;

	case NCM_NOTIFY_SPEED:
		event->bNotificationType = USB_CDC_NOTIFY_SPEED_CHANGE;
		event->wValue = cpu_to_le16(0);
		event->wLength = cpu_to_le16(8);
		req->length = NCM_STATUS_BYTECOUNT;

		/* SPEED_CHANGE data is up/down speeds in bits/sec */
		data = req->buf + sizeof *event;
		data[0] = cpu_to_le32(gether_bitrate(cdev->gadget));
		data[1] = data[0];

		DBG(cdev, "notify speed %u\n", gether_bitrate(cdev->gadget));
		ncm->notify_state = NCM_NOTIFY_CONNECT;
		break;
	}
	event->bmRequestType = 0xA1;
	event->wIndex = cpu_to_le16(ncm->ctrl_id);

	atomic_inc(&ncm->notify_count);

	/*
	 * In double buffering if there is a space in FIFO,
	 * completion callback can be called right after the call,
	 * so unlocking
	 */
	spin_unlock(&ncm->lock);
	status = usb_ep_queue(ncm->notify, req, GFP_ATOMIC);
	spin_lock(&ncm->lock);
	if (status < 0) {
		atomic_dec(&ncm->notify_count);
		DBG(cdev, "notify --> %d\n", status);
	}
}

/*
 * Context: ncm->lock held
 */
static void ncm_notify(struct f_ncm *ncm)
{
	/*
	 * NOTE on most versions of Linux, host side cdc-ethernet
	 * won't listen for notifications until its netdevice opens.
	 * The first notification then sits in the FIFO for a long
	 * time, and the second one is queued.
	 *
	 * If ncm_notify() is called before the second (CONNECT)
	 * notification is sent, then it will reset to send the SPEED
	 * notificaion again (and again, and again), but it's not a problem
	 */
	ncm->notify_state = NCM_NOTIFY_SPEED;
	ncm_do_notify(ncm);
}

static void ncm_notify_complete(struct usb_ep *ep, struct usb_request *req)
{
	struct f_ncm			*ncm = req->context;
	struct usb_composite_dev	*cdev = ncm->port.func.config->cdev;
	struct usb_cdc_notification	*event = req->buf;

	spin_lock(&ncm->lock);
	switch (req->status) {
	case 0:
		VDBG(cdev, "Notification %02x sent\n",
		     event->bNotificationType);
		atomic_dec(&ncm->notify_count);
		break;
	case -ECONNRESET:
	case -ESHUTDOWN:
		atomic_set(&ncm->notify_count, 0);
		ncm->notify_state = NCM_NOTIFY_NONE;
		break;
	default:
		DBG(cdev, "event %02x --> %d\n",
			event->bNotificationType, req->status);
		atomic_dec(&ncm->notify_count);
		break;
	}
	ncm_do_notify(ncm);
	spin_unlock(&ncm->lock);
}

static void ncm_ep0out_complete(struct usb_ep *ep, struct usb_request *req)
{
	/* now for SET_NTB_INPUT_SIZE only */
	unsigned		in_size;
	struct usb_function	*f = req->context;
	struct f_ncm		*ncm = func_to_ncm(f);
	struct usb_composite_dev *cdev = f->config->cdev;

	req->context = NULL;
	if (req->status || req->actual != req->length) {
		DBG(cdev, "Bad control-OUT transfer\n");
		goto invalid;
	}

	in_size = get_unaligned_le32(req->buf);
	if (in_size < USB_CDC_NCM_NTB_MIN_IN_SIZE ||
	    in_size > le32_to_cpu(ntb_parameters.dwNtbInMaxSize)) {
		DBG(cdev, "Got wrong INPUT SIZE (%d) from host\n", in_size);
		goto invalid;
	}

	ncm->port.fixed_in_len = in_size;
	VDBG(cdev, "Set NTB INPUT SIZE %d\n", in_size);
	return;

invalid:
	usb_ep_set_halt(ep);
	return;
}

static int ncm_setup(struct usb_function *f, const struct usb_ctrlrequest *ctrl)
{
	struct f_ncm		*ncm = func_to_ncm(f);
	struct usb_composite_dev *cdev = f->config->cdev;
	struct usb_request	*req = cdev->req;
	int			value = -EOPNOTSUPP;
	u16			w_index = le16_to_cpu(ctrl->wIndex);
	u16			w_value = le16_to_cpu(ctrl->wValue);
	u16			w_length = le16_to_cpu(ctrl->wLength);

	/*
	 * composite driver infrastructure handles everything except
	 * CDC class messages; interface activation uses set_alt().
	 */
	switch ((ctrl->bRequestType << 8) | ctrl->bRequest) {
	case ((USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE) << 8)
			| USB_CDC_SET_ETHERNET_PACKET_FILTER:
		/*
		 * see 6.2.30: no data, wIndex = interface,
		 * wValue = packet filter bitmap
		 */
		if (w_length != 0 || w_index != ncm->ctrl_id)
			goto invalid;
		DBG(cdev, "packet filter %02x\n", w_value);
		/*
		 * REVISIT locking of cdc_filter.  This assumes the UDC
		 * driver won't have a concurrent packet TX irq running on
		 * another CPU; or that if it does, this write is atomic...
		 */
		ncm->port.cdc_filter = w_value;
		value = 0;
		break;
	/*
	 * and optionally:
	 * case USB_CDC_SEND_ENCAPSULATED_COMMAND:
	 * case USB_CDC_GET_ENCAPSULATED_RESPONSE:
	 * case USB_CDC_SET_ETHERNET_MULTICAST_FILTERS:
	 * case USB_CDC_SET_ETHERNET_PM_PATTERN_FILTER:
	 * case USB_CDC_GET_ETHERNET_PM_PATTERN_FILTER:
	 * case USB_CDC_GET_ETHERNET_STATISTIC:
	 */

	case ((USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_INTERFACE) << 8)
		| USB_CDC_GET_NTB_PARAMETERS:

		if (w_length == 0 || w_value != 0 || w_index != ncm->ctrl_id)
			goto invalid;
		value = w_length > sizeof ntb_parameters ?
			sizeof ntb_parameters : w_length;
		memcpy(req->buf, &ntb_parameters, value);
		VDBG(cdev, "Host asked NTB parameters\n");
		break;

	case ((USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_INTERFACE) << 8)
		| USB_CDC_GET_NTB_INPUT_SIZE:

		if (w_length < 4 || w_value != 0 || w_index != ncm->ctrl_id)
			goto invalid;
		put_unaligned_le32(ncm->port.fixed_in_len, req->buf);
		value = 4;
		VDBG(cdev, "Host asked INPUT SIZE, sending %d\n",
		     ncm->port.fixed_in_len);
		break;

	case ((USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE) << 8)
		| USB_CDC_SET_NTB_INPUT_SIZE:
	{
		if (w_length != 4 || w_value != 0 || w_index != ncm->ctrl_id)
			goto invalid;
		req->complete = ncm_ep0out_complete;
		req->length = w_length;
		req->context = f;

		value = req->length;
		break;
	}

	case ((USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_INTERFACE) << 8)
		| USB_CDC_GET_NTB_FORMAT:
	{
		uint16_t format;

		if (w_length < 2 || w_value != 0 || w_index != ncm->ctrl_id)
			goto invalid;
		format = (ncm->parser_opts == &ndp16_opts) ? 0x0000 : 0x0001;
		put_unaligned_le16(format, req->buf);
		value = 2;
		VDBG(cdev, "Host asked NTB FORMAT, sending %d\n", format);
		break;
	}

	case ((USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE) << 8)
		| USB_CDC_SET_NTB_FORMAT:
	{
		if (w_length != 0 || w_index != ncm->ctrl_id)
			goto invalid;
		switch (w_value) {
		case 0x0000:
			ncm->parser_opts = &ndp16_opts;
			DBG(cdev, "NCM16 selected\n");
			break;
		case 0x0001:
			ncm->parser_opts = &ndp32_opts;
			DBG(cdev, "NCM32 selected\n");
			break;
		default:
			goto invalid;
		}
		value = 0;
		break;
	}
	case ((USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_INTERFACE) << 8)
		| USB_CDC_GET_CRC_MODE:
	{
		uint16_t is_crc;

		if (w_length < 2 || w_value != 0 || w_index != ncm->ctrl_id)
			goto invalid;
		is_crc = ncm->is_crc ? 0x0001 : 0x0000;
		put_unaligned_le16(is_crc, req->buf);
		value = 2;
		VDBG(cdev, "Host asked CRC MODE, sending %d\n", is_crc);
		break;
	}

	case ((USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE) << 8)
		| USB_CDC_SET_CRC_MODE:
	{
		if (w_length != 0 || w_index != ncm->ctrl_id)
			goto invalid;
		switch (w_value) {
		case 0x0000:
			ncm->is_crc = false;
			DBG(cdev, "non-CRC mode selected\n");
			break;
		case 0x0001:
			ncm->is_crc = true;
			DBG(cdev, "CRC mode selected\n");
			break;
		default:
			goto invalid;
		}
		value = 0;
		break;
	}

	/* and disabled in ncm descriptor: */
	/* case USB_CDC_GET_NET_ADDRESS: */
	/* case USB_CDC_SET_NET_ADDRESS: */
	/* case USB_CDC_GET_MAX_DATAGRAM_SIZE: */
	/* case USB_CDC_SET_MAX_DATAGRAM_SIZE: */

	default:
invalid:
		DBG(cdev, "invalid control req%02x.%02x v%04x i%04x l%d\n",
			ctrl->bRequestType, ctrl->bRequest,
			w_value, w_index, w_length);
	}
	ncm->ndp_sign = ncm->parser_opts->ndp_sign |
		(ncm->is_crc ? NCM_NDP_HDR_CRC : 0);

	/* respond with data transfer or status phase? */
	if (value >= 0) {
		DBG(cdev, "ncm req%02x.%02x v%04x i%04x l%d\n",
			ctrl->bRequestType, ctrl->bRequest,
			w_value, w_index, w_length);
		req->zero = 0;
		req->length = value;
		value = usb_ep_queue(cdev->gadget->ep0, req, GFP_ATOMIC);
		if (value < 0)
			ERROR(cdev, "ncm req %02x.%02x response err %d\n",
					ctrl->bRequestType, ctrl->bRequest,
					value);
	}

	/* device either stalls (value < 0) or reports success */
	return value;
}


static int ncm_set_alt(struct usb_function *f, unsigned intf, unsigned alt)
{
	struct f_ncm		*ncm = func_to_ncm(f);
	struct usb_composite_dev *cdev = f->config->cdev;

	/* Control interface has only altsetting 0 */
	if (intf == ncm->ctrl_id) {
		if (alt != 0)
			goto fail;

		DBG(cdev, "reset ncm control %d\n", intf);
		usb_ep_disable(ncm->notify);

		if (!(ncm->notify->desc)) {
			DBG(cdev, "init ncm ctrl %d\n", intf);
			if (config_ep_by_speed(cdev->gadget, f, ncm->notify))
				goto fail;
		}
		usb_ep_enable(ncm->notify);

	/* Data interface has two altsettings, 0 and 1 */
	} else if (intf == ncm->data_id) {
		if (alt > 1)
			goto fail;

		if (ncm->port.in_ep->enabled) {
			DBG(cdev, "reset ncm\n");
			ncm->netdev = NULL;
			gether_disconnect(&ncm->port);
			ncm_reset_values(ncm);
		}

		/*
		 * CDC Network only sends data in non-default altsettings.
		 * Changing altsettings resets filters, statistics, etc.
		 */
		if (alt == 1) {
			struct net_device	*net;

			if (!ncm->port.in_ep->desc ||
			    !ncm->port.out_ep->desc) {
				DBG(cdev, "init ncm\n");
				if (config_ep_by_speed(cdev->gadget, f,
						       ncm->port.in_ep) ||
				    config_ep_by_speed(cdev->gadget, f,
						       ncm->port.out_ep)) {
					ncm->port.in_ep->desc = NULL;
					ncm->port.out_ep->desc = NULL;
					goto fail;
				}
			}

			/* TODO */
			/* Enable zlps by default for NCM conformance;
			 * override for musb_hdrc (avoids txdma ovhead)
			 */
			ncm->port.is_zlp_ok =
				gadget_is_zlp_supported(cdev->gadget);
			ncm->port.cdc_filter = DEFAULT_FILTER;
			DBG(cdev, "activate ncm\n");
			net = gether_connect(&ncm->port);
			if (IS_ERR(net))
				return PTR_ERR(net);
			ncm->netdev = net;
		}

		spin_lock(&ncm->lock);
		ncm_notify(ncm);
		spin_unlock(&ncm->lock);
	} else
		goto fail;

	return 0;
fail:
	return -EINVAL;
}

/*
 * Because the data interface supports multiple altsettings,
 * this NCM function *MUST* implement a get_alt() method.
 */
static int ncm_get_alt(struct usb_function *f, unsigned intf)
{
	struct f_ncm		*ncm = func_to_ncm(f);

	if (intf == ncm->ctrl_id)
		return 0;
	return ncm->port.in_ep->enabled ? 1 : 0;
}

static struct sk_buff *package_for_tx(struct f_ncm *ncm)
{
	__le16		*ntb_iter;
	struct sk_buff	*skb2 = NULL;
	unsigned	ndp_pad;
	unsigned	ndp_index;
	unsigned	new_len;

	const struct ndp_parser_opts *opts = ncm->parser_opts;
	const int ndp_align = le16_to_cpu(ntb_parameters.wNdpInAlignment);
	const int dgram_idx_len = 2 * 2 * opts->dgram_item_len;

	/* Stop the timer */
	hrtimer_try_to_cancel(&ncm->task_timer);

	ndp_pad = ALIGN(ncm->skb_tx_data->len, ndp_align) -
			ncm->skb_tx_data->len;
	ndp_index = ncm->skb_tx_data->len + ndp_pad;
	new_len = ndp_index + dgram_idx_len + ncm->skb_tx_ndp->len;

	/* Set the final BlockLength and wNdpIndex */
	ntb_iter = (void *) ncm->skb_tx_data->data;
	/* Increment pointer to BlockLength */
	ntb_iter += 2 + 1 + 1;
	put_ncm(&ntb_iter, opts->block_length, new_len);
	put_ncm(&ntb_iter, opts->ndp_index, ndp_index);

	/* Set the final NDP wLength */
	new_len = opts->ndp_size +
			(ncm->ndp_dgram_count * dgram_idx_len);
	ncm->ndp_dgram_count = 0;
	/* Increment from start to wLength */
	ntb_iter = (void *) ncm->skb_tx_ndp->data;
	ntb_iter += 2;
	put_unaligned_le16(new_len, ntb_iter);

	/* Merge the skbs */
	swap(skb2, ncm->skb_tx_data);
	if (ncm->skb_tx_data) {
		dev_consume_skb_any(ncm->skb_tx_data);
		ncm->skb_tx_data = NULL;
	}

	/* Insert NDP alignment. */
	skb_put_zero(skb2, ndp_pad);

	/* Copy NTB across. */
	skb_put_data(skb2, ncm->skb_tx_ndp->data, ncm->skb_tx_ndp->len);
	dev_consume_skb_any(ncm->skb_tx_ndp);
	ncm->skb_tx_ndp = NULL;

	/* Insert zero'd datagram. */
	skb_put_zero(skb2, dgram_idx_len);

	return skb2;
}

static struct sk_buff *ncm_wrap_ntb(struct gether *port,
				    struct sk_buff *skb)
{
	struct f_ncm	*ncm = func_to_ncm(&port->func);
	struct sk_buff	*skb2 = NULL;

	if (skb) {
		int		ncb_len = 0;
		__le16		*ntb_data;
		__le16		*ntb_ndp;
		int		dgram_pad;

		unsigned	max_size = ncm->port.fixed_in_len;
		const struct ndp_parser_opts *opts = ncm->parser_opts;
		const int ndp_align = le16_to_cpu(ntb_parameters.wNdpInAlignment);
		const int div = le16_to_cpu(ntb_parameters.wNdpInDivisor);
		const int rem = le16_to_cpu(ntb_parameters.wNdpInPayloadRemainder);
		const int dgram_idx_len = 2 * 2 * opts->dgram_item_len;

		/* Add the CRC if required up front */
		if (ncm->is_crc) {
			uint32_t	crc;
			__le16		*crc_pos;

			crc = ~crc32_le(~0,
					skb->data,
					skb->len);
			crc_pos = skb_put(skb, sizeof(uint32_t));
			put_unaligned_le32(crc, crc_pos);
		}

		/* If the new skb is too big for the current NCM NTB then
		 * set the current stored skb to be sent now and clear it
		 * ready for new data.
		 * NOTE: Assume maximum align for speed of calculation.
		 */
		if (ncm->skb_tx_data
		    && (ncm->ndp_dgram_count >= TX_MAX_NUM_DPE
		    || (ncm->skb_tx_data->len +
		    div + rem + skb->len +
		    ncm->skb_tx_ndp->len + ndp_align + (2 * dgram_idx_len))
		    > max_size)) {
			skb2 = package_for_tx(ncm);
			if (!skb2)
				goto err;
		}

		if (!ncm->skb_tx_data) {
			ncb_len = opts->nth_size;
			dgram_pad = ALIGN(ncb_len, div) + rem - ncb_len;
			ncb_len += dgram_pad;

			/* Create a new skb for the NTH and datagrams. */
			ncm->skb_tx_data = alloc_skb(max_size, GFP_ATOMIC);
			if (!ncm->skb_tx_data)
				goto err;

			ncm->skb_tx_data->dev = ncm->netdev;
			ntb_data = skb_put_zero(ncm->skb_tx_data, ncb_len);
			/* dwSignature */
			put_unaligned_le32(opts->nth_sign, ntb_data);
			ntb_data += 2;
			/* wHeaderLength */
			put_unaligned_le16(opts->nth_size, ntb_data++);

			/* Allocate an skb for storing the NDP,
			 * TX_MAX_NUM_DPE should easily suffice for a
			 * 16k packet.
			 */
			ncm->skb_tx_ndp = alloc_skb((int)(opts->ndp_size
						    + opts->dpe_size
						    * TX_MAX_NUM_DPE),
						    GFP_ATOMIC);
			if (!ncm->skb_tx_ndp)
				goto err;

			ncm->skb_tx_ndp->dev = ncm->netdev;
			ntb_ndp = skb_put(ncm->skb_tx_ndp, opts->ndp_size);
			memset(ntb_ndp, 0, ncb_len);
			/* dwSignature */
			put_unaligned_le32(ncm->ndp_sign, ntb_ndp);
			ntb_ndp += 2;

			/* There is always a zeroed entry */
			ncm->ndp_dgram_count = 1;

			/* Note: we skip opts->next_ndp_index */

			/* Start the timer. */
			hrtimer_start(&ncm->task_timer, TX_TIMEOUT_NSECS,
				      HRTIMER_MODE_REL_SOFT);
		}

		/* Add the datagram position entries */
		ntb_ndp = skb_put_zero(ncm->skb_tx_ndp, dgram_idx_len);

		ncb_len = ncm->skb_tx_data->len;
		dgram_pad = ALIGN(ncb_len, div) + rem - ncb_len;
		ncb_len += dgram_pad;

		/* (d)wDatagramIndex */
		put_ncm(&ntb_ndp, opts->dgram_item_len, ncb_len);
		/* (d)wDatagramLength */
		put_ncm(&ntb_ndp, opts->dgram_item_len, skb->len);
		ncm->ndp_dgram_count++;

		/* Add the new data to the skb */
		skb_put_zero(ncm->skb_tx_data, dgram_pad);
		skb_put_data(ncm->skb_tx_data, skb->data, skb->len);
		dev_consume_skb_any(skb);
		skb = NULL;

	} else if (ncm->skb_tx_data) {
		/* If we get here ncm_wrap_ntb() was called with NULL skb,
		 * because eth_start_xmit() was called with NULL skb by
		 * ncm_tx_timeout() - hence, this is our signal to flush/send.
		 */
		skb2 = package_for_tx(ncm);
		if (!skb2)
			goto err;
	}

	return skb2;

err:
	ncm->netdev->stats.tx_dropped++;

	if (skb)
		dev_kfree_skb_any(skb);
	if (ncm->skb_tx_data)
		dev_kfree_skb_any(ncm->skb_tx_data);
	if (ncm->skb_tx_ndp)
		dev_kfree_skb_any(ncm->skb_tx_ndp);

	return NULL;
}

/*
 * The transmit should only be run if no skb data has been sent
 * for a certain duration.
 */
static enum hrtimer_restart ncm_tx_timeout(struct hrtimer *data)
{
	struct f_ncm *ncm = container_of(data, struct f_ncm, task_timer);
	struct net_device *netdev = READ_ONCE(ncm->netdev);

	if (netdev) {
		/* XXX This allowance of a NULL skb argument to ndo_start_xmit
		 * XXX is not sane.  The gadget layer should be redesigned so
		 * XXX that the dev->wrap() invocations to build SKBs is transparent
		 * XXX and performed in some way outside of the ndo_start_xmit
		 * XXX interface.
		 *
		 * This will call directly into u_ether's eth_start_xmit()
		 */
		netdev->netdev_ops->ndo_start_xmit(NULL, netdev);
	}
	return HRTIMER_NORESTART;
}

static int ncm_unwrap_ntb(struct gether *port,
			  struct sk_buff *skb,
			  struct sk_buff_head *list)
{
	struct f_ncm	*ncm = func_to_ncm(&port->func);
	unsigned char	*ntb_ptr = skb->data;
	__le16		*tmp;
	unsigned	index, index2;
	int		ndp_index;
	unsigned	dg_len, dg_len2;
	unsigned	ndp_len;
	unsigned	block_len;
	struct sk_buff	*skb2;
	int		ret = -EINVAL;
	unsigned	ntb_max = le32_to_cpu(ntb_parameters.dwNtbOutMaxSize);
	unsigned	frame_max = le16_to_cpu(ecm_desc.wMaxSegmentSize);
	const struct ndp_parser_opts *opts = ncm->parser_opts;
	unsigned	crc_len = ncm->is_crc ? sizeof(uint32_t) : 0;
	int		dgram_counter;
	int		to_process = skb->len;

parse_ntb:
	tmp = (__le16 *)ntb_ptr;

	/* dwSignature */
	if (get_unaligned_le32(tmp) != opts->nth_sign) {
		INFO(port->func.config->cdev, "Wrong NTH SIGN, skblen %d\n",
			skb->len);
		print_hex_dump(KERN_INFO, "HEAD:", DUMP_PREFIX_ADDRESS, 32, 1,
			       skb->data, 32, false);

		goto err;
	}
	tmp += 2;
	/* wHeaderLength */
	if (get_unaligned_le16(tmp++) != opts->nth_size) {
		INFO(port->func.config->cdev, "Wrong NTB headersize\n");
		goto err;
	}
	tmp++; /* skip wSequence */

	block_len = get_ncm(&tmp, opts->block_length);
	/* (d)wBlockLength */
	if (block_len > ntb_max) {
		INFO(port->func.config->cdev, "OUT size exceeded\n");
		goto err;
	}

	ndp_index = get_ncm(&tmp, opts->ndp_index);

	/* Run through all the NDP's in the NTB */
	do {
		/*
		 * NCM 3.2
		 * dwNdpIndex
		 */
		if (((ndp_index % 4) != 0) ||
				(ndp_index < opts->nth_size) ||
				(ndp_index > (block_len -
					      opts->ndp_size))) {
			INFO(port->func.config->cdev, "Bad index: %#X\n",
			     ndp_index);
			goto err;
		}

		/*
		 * walk through NDP
		 * dwSignature
		 */
		tmp = (__le16 *)(ntb_ptr + ndp_index);
		if (get_unaligned_le32(tmp) != ncm->ndp_sign) {
			INFO(port->func.config->cdev, "Wrong NDP SIGN\n");
			goto err;
		}
		tmp += 2;

		ndp_len = get_unaligned_le16(tmp++);
		/*
		 * NCM 3.3.1
		 * wLength
		 * entry is 2 items
		 * item size is 16/32 bits, opts->dgram_item_len * 2 bytes
		 * minimal: struct usb_cdc_ncm_ndpX + normal entry + zero entry
		 * Each entry is a dgram index and a dgram length.
		 */
		if ((ndp_len < opts->ndp_size
				+ 2 * 2 * (opts->dgram_item_len * 2)) ||
				(ndp_len % opts->ndplen_align != 0)) {
			INFO(port->func.config->cdev, "Bad NDP length: %#X\n",
			     ndp_len);
			goto err;
		}
		tmp += opts->reserved1;
		/* Check for another NDP (d)wNextNdpIndex */
		ndp_index = get_ncm(&tmp, opts->next_ndp_index);
		tmp += opts->reserved2;

		ndp_len -= opts->ndp_size;
		index2 = get_ncm(&tmp, opts->dgram_item_len);
		dg_len2 = get_ncm(&tmp, opts->dgram_item_len);
		dgram_counter = 0;

		do {
			index = index2;
			/* wDatagramIndex[0] */
			if ((index < opts->nth_size) ||
					(index > block_len - opts->dpe_size)) {
				INFO(port->func.config->cdev,
				     "Bad index: %#X\n", index);
				goto err;
			}

			dg_len = dg_len2;
			/*
			 * wDatagramLength[0]
			 * ethernet hdr + crc or larger than max frame size
			 */
			if ((dg_len < 14 + crc_len) ||
					(dg_len > frame_max)) {
				INFO(port->func.config->cdev,
				     "Bad dgram length: %#X\n", dg_len);
				goto err;
			}
			if (ncm->is_crc) {
				uint32_t crc, crc2;

				crc = get_unaligned_le32(ntb_ptr +
							 index + dg_len -
							 crc_len);
				crc2 = ~crc32_le(~0,
						 ntb_ptr + index,
						 dg_len - crc_len);
				if (crc != crc2) {
					INFO(port->func.config->cdev,
					     "Bad CRC\n");
					goto err;
				}
			}

			index2 = get_ncm(&tmp, opts->dgram_item_len);
			dg_len2 = get_ncm(&tmp, opts->dgram_item_len);

			/* wDatagramIndex[1] */
			if (index2 > block_len - opts->dpe_size) {
				INFO(port->func.config->cdev,
				     "Bad index: %#X\n", index2);
				goto err;
			}

			/*
			 * Copy the data into a new skb.
			 * This ensures the truesize is correct
			 */
			skb2 = netdev_alloc_skb_ip_align(ncm->netdev,
							 dg_len - crc_len);
			if (skb2 == NULL)
				goto err;
			skb_put_data(skb2, ntb_ptr + index,
				     dg_len - crc_len);

			skb_queue_tail(list, skb2);

			ndp_len -= 2 * (opts->dgram_item_len * 2);

			dgram_counter++;
			if (index2 == 0 || dg_len2 == 0)
				break;
		} while (ndp_len > 2 * (opts->dgram_item_len * 2));
	} while (ndp_index);

	VDBG(port->func.config->cdev,
	     "Parsed NTB with %d frames\n", dgram_counter);

	to_process -= block_len;
	if (to_process != 0) {
		ntb_ptr = (unsigned char *)(ntb_ptr + block_len);
		goto parse_ntb;
	}

	dev_consume_skb_any(skb);

	return 0;
err:
	skb_queue_purge(list);
	dev_kfree_skb_any(skb);
	return ret;
}

static void ncm_disable(struct usb_function *f)
{
	struct f_ncm		*ncm = func_to_ncm(f);
	struct usb_composite_dev *cdev = f->config->cdev;

	DBG(cdev, "ncm deactivated\n");

	if (ncm->port.in_ep->enabled) {
		ncm->netdev = NULL;
		gether_disconnect(&ncm->port);
	}

	if (ncm->notify->enabled) {
		usb_ep_disable(ncm->notify);
		ncm->notify->desc = NULL;
	}
}

/*-------------------------------------------------------------------------*/

/*
 * Callbacks let us notify the host about connect/disconnect when the
 * net device is opened or closed.
 *
 * For testing, note that link states on this side include both opened
 * and closed variants of:
 *
 *   - disconnected/unconfigured
 *   - configured but inactive (data alt 0)
 *   - configured and active (data alt 1)
 *
 * Each needs to be tested with unplug, rmmod, SET_CONFIGURATION, and
 * SET_INTERFACE (altsetting).  Remember also that "configured" doesn't
 * imply the host is actually polling the notification endpoint, and
 * likewise that "active" doesn't imply it's actually using the data
 * endpoints for traffic.
 */

static void ncm_open(struct gether *geth)
{
	struct f_ncm		*ncm = func_to_ncm(&geth->func);

	DBG(ncm->port.func.config->cdev, "%s\n", __func__);

	spin_lock(&ncm->lock);
	ncm->is_open = true;
	ncm_notify(ncm);
	spin_unlock(&ncm->lock);
}

static void ncm_close(struct gether *geth)
{
	struct f_ncm		*ncm = func_to_ncm(&geth->func);

	DBG(ncm->port.func.config->cdev, "%s\n", __func__);

	spin_lock(&ncm->lock);
	ncm->is_open = false;
	ncm_notify(ncm);
	spin_unlock(&ncm->lock);
}

/*-------------------------------------------------------------------------*/

/* ethernet function driver setup/binding */

static int ncm_bind(struct usb_configuration *c, struct usb_function *f)
{
	struct usb_composite_dev *cdev = c->cdev;
	struct f_ncm		*ncm = func_to_ncm(f);
	struct usb_string	*us;
	int			status = 0;
	struct usb_ep		*ep;
	struct f_ncm_opts	*ncm_opts;

	if (!can_support_ecm(cdev->gadget))
		return -EINVAL;

	ncm_opts = container_of(f->fi, struct f_ncm_opts, func_inst);

	if (cdev->use_os_string) {
		f->os_desc_table = kzalloc(sizeof(*f->os_desc_table),
					   GFP_KERNEL);
		if (!f->os_desc_table)
			return -ENOMEM;
		f->os_desc_n = 1;
		f->os_desc_table[0].os_desc = &ncm_opts->ncm_os_desc;
	}

	mutex_lock(&ncm_opts->lock);
	gether_set_gadget(ncm_opts->net, cdev->gadget);
	if (!ncm_opts->bound)
		status = gether_register_netdev(ncm_opts->net);
	mutex_unlock(&ncm_opts->lock);

	if (status)
		goto fail;

	ncm_opts->bound = true;

	us = usb_gstrings_attach(cdev, ncm_strings,
				 ARRAY_SIZE(ncm_string_defs));
	if (IS_ERR(us)) {
		status = PTR_ERR(us);
		goto fail;
	}
	ncm_control_intf.iInterface = us[STRING_CTRL_IDX].id;
	ncm_data_nop_intf.iInterface = us[STRING_DATA_IDX].id;
	ncm_data_intf.iInterface = us[STRING_DATA_IDX].id;
	ecm_desc.iMACAddress = us[STRING_MAC_IDX].id;
	ncm_iad_desc.iFunction = us[STRING_IAD_IDX].id;

	/* allocate instance-specific interface IDs */
	status = usb_interface_id(c, f);
	if (status < 0)
		goto fail;
	ncm->ctrl_id = status;
	ncm_iad_desc.bFirstInterface = status;

	ncm_control_intf.bInterfaceNumber = status;
	ncm_union_desc.bMasterInterface0 = status;

	if (cdev->use_os_string)
		f->os_desc_table[0].if_id =
			ncm_iad_desc.bFirstInterface;

	status = usb_interface_id(c, f);
	if (status < 0)
		goto fail;
	ncm->data_id = status;

	ncm_data_nop_intf.bInterfaceNumber = status;
	ncm_data_intf.bInterfaceNumber = status;
	ncm_union_desc.bSlaveInterface0 = status;

	status = -ENODEV;

	/* allocate instance-specific endpoints */
	ep = usb_ep_autoconfig(cdev->gadget, &fs_ncm_in_desc);
	if (!ep)
		goto fail;
	ncm->port.in_ep = ep;

	ep = usb_ep_autoconfig(cdev->gadget, &fs_ncm_out_desc);
	if (!ep)
		goto fail;
	ncm->port.out_ep = ep;

	ep = usb_ep_autoconfig(cdev->gadget, &fs_ncm_notify_desc);
	if (!ep)
		goto fail;
	ncm->notify = ep;

	status = -ENOMEM;

	/* allocate notification request and buffer */
	ncm->notify_req = usb_ep_alloc_request(ep, GFP_KERNEL);
	if (!ncm->notify_req)
		goto fail;
	ncm->notify_req->buf = kmalloc(NCM_STATUS_BYTECOUNT, GFP_KERNEL);
	if (!ncm->notify_req->buf)
		goto fail;
	ncm->notify_req->context = ncm;
	ncm->notify_req->complete = ncm_notify_complete;

	/*
	 * support all relevant hardware speeds... we expect that when
	 * hardware is dual speed, all bulk-capable endpoints work at
	 * both speeds
	 */
	hs_ncm_in_desc.bEndpointAddress = fs_ncm_in_desc.bEndpointAddress;
	hs_ncm_out_desc.bEndpointAddress = fs_ncm_out_desc.bEndpointAddress;
	hs_ncm_notify_desc.bEndpointAddress =
		fs_ncm_notify_desc.bEndpointAddress;

	ss_ncm_in_desc.bEndpointAddress = fs_ncm_in_desc.bEndpointAddress;
	ss_ncm_out_desc.bEndpointAddress = fs_ncm_out_desc.bEndpointAddress;
	ss_ncm_notify_desc.bEndpointAddress =
		fs_ncm_notify_desc.bEndpointAddress;

	status = usb_assign_descriptors(f, ncm_fs_function, ncm_hs_function,
			ncm_ss_function, ncm_ss_function);
	if (status)
		goto fail;

	/*
	 * NOTE:  all that is done without knowing or caring about
	 * the network link ... which is unavailable to this code
	 * until we're activated via set_alt().
	 */

	ncm->port.open = ncm_open;
	ncm->port.close = ncm_close;

	hrtimer_init(&ncm->task_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL_SOFT);
	ncm->task_timer.function = ncm_tx_timeout;

	DBG(cdev, "CDC Network: IN/%s OUT/%s NOTIFY/%s\n",
			ncm->port.in_ep->name, ncm->port.out_ep->name,
			ncm->notify->name);
	return 0;

fail:
	kfree(f->os_desc_table);
	f->os_desc_n = 0;

	if (ncm->notify_req) {
		kfree(ncm->notify_req->buf);
		usb_ep_free_request(ncm->notify, ncm->notify_req);
	}

	ERROR(cdev, "%s: can't bind, err %d\n", f->name, status);

	return status;
}

static inline struct f_ncm_opts *to_f_ncm_opts(struct config_item *item)
{
	return container_of(to_config_group(item), struct f_ncm_opts,
			    func_inst.group);
}

/* f_ncm_item_ops */
USB_ETHERNET_CONFIGFS_ITEM(ncm);

/* f_ncm_opts_dev_addr */
USB_ETHERNET_CONFIGFS_ITEM_ATTR_DEV_ADDR(ncm);

/* f_ncm_opts_host_addr */
USB_ETHERNET_CONFIGFS_ITEM_ATTR_HOST_ADDR(ncm);

/* f_ncm_opts_qmult */
USB_ETHERNET_CONFIGFS_ITEM_ATTR_QMULT(ncm);

/* f_ncm_opts_ifname */
USB_ETHERNET_CONFIGFS_ITEM_ATTR_IFNAME(ncm);

static struct configfs_attribute *ncm_attrs[] = {
	&ncm_opts_attr_dev_addr,
	&ncm_opts_attr_host_addr,
	&ncm_opts_attr_qmult,
	&ncm_opts_attr_ifname,
	NULL,
};

static const struct config_item_type ncm_func_type = {
	.ct_item_ops	= &ncm_item_ops,
	.ct_attrs	= ncm_attrs,
	.ct_owner	= THIS_MODULE,
};

static void ncm_free_inst(struct usb_function_instance *f)
{
	struct f_ncm_opts *opts;

	opts = container_of(f, struct f_ncm_opts, func_inst);
	if (opts->bound)
		gether_cleanup(netdev_priv(opts->net));
	else
		free_netdev(opts->net);
	kfree(opts->ncm_interf_group);
	kfree(opts);
}

static struct usb_function_instance *ncm_alloc_inst(void)
{
	struct f_ncm_opts *opts;
	struct usb_os_desc *descs[1];
	char *names[1];
	struct config_group *ncm_interf_group;

	opts = kzalloc(sizeof(*opts), GFP_KERNEL);
	if (!opts)
		return ERR_PTR(-ENOMEM);
	opts->ncm_os_desc.ext_compat_id = opts->ncm_ext_compat_id;

	mutex_init(&opts->lock);
	opts->func_inst.free_func_inst = ncm_free_inst;
	opts->net = gether_setup_default();
	if (IS_ERR(opts->net)) {
		struct net_device *net = opts->net;
		kfree(opts);
		return ERR_CAST(net);
	}
	INIT_LIST_HEAD(&opts->ncm_os_desc.ext_prop);

	descs[0] = &opts->ncm_os_desc;
	names[0] = "ncm";

	config_group_init_type_name(&opts->func_inst.group, "", &ncm_func_type);
	ncm_interf_group =
		usb_os_desc_prepare_interf_dir(&opts->func_inst.group, 1, descs,
					       names, THIS_MODULE);
	if (IS_ERR(ncm_interf_group)) {
		ncm_free_inst(&opts->func_inst);
		return ERR_CAST(ncm_interf_group);
	}
	opts->ncm_interf_group = ncm_interf_group;

	return &opts->func_inst;
}

static void ncm_free(struct usb_function *f)
{
	struct f_ncm *ncm;
	struct f_ncm_opts *opts;

	ncm = func_to_ncm(f);
	opts = container_of(f->fi, struct f_ncm_opts, func_inst);
	kfree(ncm);
	mutex_lock(&opts->lock);
	opts->refcnt--;
	mutex_unlock(&opts->lock);
}

static void ncm_unbind(struct usb_configuration *c, struct usb_function *f)
{
	struct f_ncm *ncm = func_to_ncm(f);

	DBG(c->cdev, "ncm unbind\n");

	hrtimer_cancel(&ncm->task_timer);

	kfree(f->os_desc_table);
	f->os_desc_n = 0;

	ncm_string_defs[0].id = 0;
	usb_free_all_descriptors(f);

	if (atomic_read(&ncm->notify_count)) {
		usb_ep_dequeue(ncm->notify, ncm->notify_req);
		atomic_set(&ncm->notify_count, 0);
	}

	kfree(ncm->notify_req->buf);
	usb_ep_free_request(ncm->notify, ncm->notify_req);
}

static struct usb_function *ncm_alloc(struct usb_function_instance *fi)
{
	struct f_ncm		*ncm;
	struct f_ncm_opts	*opts;
	int status;

	/* allocate and initialize one new instance */
	ncm = kzalloc(sizeof(*ncm), GFP_KERNEL);
	if (!ncm)
		return ERR_PTR(-ENOMEM);

	opts = container_of(fi, struct f_ncm_opts, func_inst);
	mutex_lock(&opts->lock);
	opts->refcnt++;

	/* export host's Ethernet address in CDC format */
	status = gether_get_host_addr_cdc(opts->net, ncm->ethaddr,
				      sizeof(ncm->ethaddr));
	if (status < 12) { /* strlen("01234567890a") */
		kfree(ncm);
		mutex_unlock(&opts->lock);
		return ERR_PTR(-EINVAL);
	}
	ncm_string_defs[STRING_MAC_IDX].s = ncm->ethaddr;

	spin_lock_init(&ncm->lock);
	ncm_reset_values(ncm);
	ncm->port.ioport = netdev_priv(opts->net);
	mutex_unlock(&opts->lock);
	ncm->port.is_fixed = true;
	ncm->port.supports_multi_frame = true;

	ncm->port.func.name = "cdc_network";
	/* descriptors are per-instance copies */
	ncm->port.func.bind = ncm_bind;
	ncm->port.func.unbind = ncm_unbind;
	ncm->port.func.set_alt = ncm_set_alt;
	ncm->port.func.get_alt = ncm_get_alt;
	ncm->port.func.setup = ncm_setup;
	ncm->port.func.disable = ncm_disable;
	ncm->port.func.free_func = ncm_free;

	ncm->port.wrap = ncm_wrap_ntb;
	ncm->port.unwrap = ncm_unwrap_ntb;

	return &ncm->port.func;
}

DECLARE_USB_FUNCTION_INIT(ncm, ncm_alloc_inst, ncm_alloc);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Yauheni Kaliuta");
