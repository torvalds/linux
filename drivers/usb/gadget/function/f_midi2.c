// SPDX-License-Identifier: GPL-2.0+
/*
 * f_midi2.c -- USB MIDI 2.0 class function driver
 */

#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>

#include <sound/core.h>
#include <sound/ump.h>
#include <sound/ump_msg.h>

#include <linux/usb/ch9.h>
#include <linux/usb/gadget.h>
#include <linux/usb/audio.h>
#include <linux/usb/midi-v2.h>

#include "u_f.h"
#include "u_midi2.h"

struct f_midi2;
struct f_midi2_ep;
struct f_midi2_usb_ep;

/* Context for each USB request */
struct f_midi2_req_ctx {
	struct f_midi2_usb_ep *usb_ep;	/* belonging USB EP */
	unsigned int index;		/* array index: 0-31 */
	struct usb_request *req;	/* assigned request */
};

/* Resources for a USB Endpoint */
struct f_midi2_usb_ep {
	struct f_midi2 *card;		/* belonging card */
	struct f_midi2_ep *ep;		/* belonging UMP EP (optional) */
	struct usb_ep *usb_ep;		/* assigned USB EP */
	void (*complete)(struct usb_ep *usb_ep, struct usb_request *req);
	unsigned long free_reqs;	/* bitmap for unused requests */
	unsigned int num_reqs;		/* number of allocated requests */
	struct f_midi2_req_ctx *reqs;	/* request context array */
};

/* Resources for UMP Function Block (and USB Group Terminal Block) */
struct f_midi2_block {
	struct f_midi2_block_info info;	/* FB info, copied from configfs */
	struct snd_ump_block *fb;	/* assigned FB */
	unsigned int gtb_id;		/* assigned GTB id */
	unsigned int string_id;		/* assigned string id */
};

/* Resources for UMP Endpoint */
struct f_midi2_ep {
	struct snd_ump_endpoint *ump;	/* assigned UMP EP */
	struct f_midi2 *card;		/* belonging MIDI 2.0 device */

	struct f_midi2_ep_info info;	/* UMP EP info, copied from configfs */
	unsigned int num_blks;		/* number of FBs */
	struct f_midi2_block blks[SNDRV_UMP_MAX_BLOCKS];	/* UMP FBs */

	struct f_midi2_usb_ep ep_in;	/* USB MIDI EP-in */
	struct f_midi2_usb_ep ep_out;	/* USB MIDI EP-out */
};

/* indices for USB strings */
enum {
	STR_IFACE = 0,
	STR_GTB1 = 1,
};

/* 1-based GTB id to string id */
#define gtb_to_str_id(id)	(STR_GTB1 + (id) - 1)

/* operation mode */
enum {
	MIDI_OP_MODE_UNSET,	/* no altset set yet */
	MIDI_OP_MODE_MIDI1,	/* MIDI 1.0 (altset 0) is used */
	MIDI_OP_MODE_MIDI2,	/* MIDI 2.0 (altset 1) is used */
};

/* Resources for MIDI 2.0 Device */
struct f_midi2 {
	struct usb_function func;
	struct usb_gadget *gadget;
	struct snd_card *card;

	/* MIDI 1.0 in/out USB EPs */
	struct f_midi2_usb_ep midi1_ep_in;
	struct f_midi2_usb_ep midi1_ep_out;

	int midi_if;			/* USB MIDI interface number */
	int operation_mode;		/* current operation mode */

	spinlock_t queue_lock;

	struct f_midi2_card_info info;	/* card info, copied from configfs */

	unsigned int num_eps;
	struct f_midi2_ep midi2_eps[MAX_UMP_EPS];

	unsigned int total_blocks;	/* total number of blocks of all EPs */
	struct usb_string *string_defs;
	struct usb_string *strings;
};

#define func_to_midi2(f)	container_of(f, struct f_midi2, func)

/* get EP name string */
static const char *ump_ep_name(const struct f_midi2_ep *ep)
{
	return ep->info.ep_name ? ep->info.ep_name : "MIDI 2.0 Gadget";
}

/* get EP product ID string */
static const char *ump_product_id(const struct f_midi2_ep *ep)
{
	return ep->info.product_id ? ep->info.product_id : "Unique Product ID";
}

/* get FB name string */
static const char *ump_fb_name(const struct f_midi2_block_info *info)
{
	return info->name ? info->name : "MIDI 2.0 Gadget I/O";
}

/*
 * USB Descriptor Definitions
 */
/* GTB header descriptor */
static struct usb_ms20_gr_trm_block_header_descriptor gtb_header_desc = {
	.bLength =		sizeof(gtb_header_desc),
	.bDescriptorType =	USB_DT_CS_GR_TRM_BLOCK,
	.bDescriptorSubtype =	USB_MS_GR_TRM_BLOCK_HEADER,
	.wTotalLength =		__cpu_to_le16(0x12), // to be filled
};

/* GTB descriptor template: most items are replaced dynamically */
static struct usb_ms20_gr_trm_block_descriptor gtb_desc = {
	.bLength =		sizeof(gtb_desc),
	.bDescriptorType =	USB_DT_CS_GR_TRM_BLOCK,
	.bDescriptorSubtype =	USB_MS_GR_TRM_BLOCK,
	.bGrpTrmBlkID =		0x01,
	.bGrpTrmBlkType =	USB_MS_GR_TRM_BLOCK_TYPE_BIDIRECTIONAL,
	.nGroupTrm =		0x00,
	.nNumGroupTrm =		1,
	.iBlockItem =		0,
	.bMIDIProtocol =	USB_MS_MIDI_PROTO_1_0_64,
	.wMaxInputBandwidth =	0,
	.wMaxOutputBandwidth =	0,
};

DECLARE_USB_MIDI_OUT_JACK_DESCRIPTOR(1);
DECLARE_USB_MS_ENDPOINT_DESCRIPTOR(1);
DECLARE_UAC_AC_HEADER_DESCRIPTOR(1);
DECLARE_USB_MS20_ENDPOINT_DESCRIPTOR(32);

#define EP_MAX_PACKET_INT	8

/* Audio Control Interface */
static struct usb_interface_descriptor midi2_audio_if_desc = {
	.bLength =		USB_DT_INTERFACE_SIZE,
	.bDescriptorType =	USB_DT_INTERFACE,
	.bInterfaceNumber =	0, // to be filled
	.bNumEndpoints =	0,
	.bInterfaceClass =	USB_CLASS_AUDIO,
	.bInterfaceSubClass =	USB_SUBCLASS_AUDIOCONTROL,
	.bInterfaceProtocol =	0,
	.iInterface =		0,
};

static struct uac1_ac_header_descriptor_1 midi2_audio_class_desc = {
	.bLength =		0x09,
	.bDescriptorType =	USB_DT_CS_INTERFACE,
	.bDescriptorSubtype =	0x01,
	.bcdADC =		__cpu_to_le16(0x0100),
	.wTotalLength =		__cpu_to_le16(0x0009),
	.bInCollection =	0x01,
	.baInterfaceNr =	{ 0x01 }, // to be filled
};

/* MIDI 1.0 Streaming Interface (altset 0) */
static struct usb_interface_descriptor midi2_midi1_if_desc = {
	.bLength =		USB_DT_INTERFACE_SIZE,
	.bDescriptorType =	USB_DT_INTERFACE,
	.bInterfaceNumber =	0, // to be filled
	.bAlternateSetting =	0,
	.bNumEndpoints =	2,
	.bInterfaceClass =	USB_CLASS_AUDIO,
	.bInterfaceSubClass =	USB_SUBCLASS_MIDISTREAMING,
	.bInterfaceProtocol =	0,
	.iInterface =		0, // to be filled
};

static struct usb_ms_header_descriptor midi2_midi1_class_desc = {
	.bLength =		0x07,
	.bDescriptorType =	USB_DT_CS_INTERFACE,
	.bDescriptorSubtype =	USB_MS_HEADER,
	.bcdMSC =		__cpu_to_le16(0x0100),
	.wTotalLength =		__cpu_to_le16(0x41), // to be calculated
};

/* MIDI 1.0 IN (Embedded) Jack */
static struct usb_midi_in_jack_descriptor midi2_midi1_in_jack1_desc = {
	.bLength =		0x06,
	.bDescriptorType =	USB_DT_CS_INTERFACE,
	.bDescriptorSubtype =	USB_MS_MIDI_IN_JACK,
	.bJackType =		USB_MS_EMBEDDED,
	.bJackID =		0x01,
	.iJack =		0,
};

/* MIDI 1.0 IN (External) Jack */
static struct usb_midi_in_jack_descriptor midi2_midi1_in_jack2_desc = {
	.bLength =		0x06,
	.bDescriptorType =	USB_DT_CS_INTERFACE,
	.bDescriptorSubtype =	USB_MS_MIDI_IN_JACK,
	.bJackType =		USB_MS_EXTERNAL,
	.bJackID =		0x02,
	.iJack =		0,
};

/* MIDI 1.0 OUT (Embedded) Jack */
static struct usb_midi_out_jack_descriptor_1 midi2_midi1_out_jack1_desc = {
	.bLength =		0x09,
	.bDescriptorType =	USB_DT_CS_INTERFACE,
	.bDescriptorSubtype =	USB_MS_MIDI_OUT_JACK,
	.bJackType =		USB_MS_EMBEDDED,
	.bJackID =		0x03,
	.bNrInputPins =		1,
	.pins =			{ { 0x02, 0x01 } },
	.iJack =		0,
};

/* MIDI 1.0 OUT (External) Jack */
static struct usb_midi_out_jack_descriptor_1 midi2_midi1_out_jack2_desc = {
	.bLength =		0x09,
	.bDescriptorType =	USB_DT_CS_INTERFACE,
	.bDescriptorSubtype =	USB_MS_MIDI_OUT_JACK,
	.bJackType =		USB_MS_EXTERNAL,
	.bJackID =		0x04,
	.bNrInputPins =		1,
	.pins =			{ { 0x01, 0x01 } },
	.iJack =		0,
};

/* MIDI 1.0 EP OUT */
static struct usb_endpoint_descriptor midi2_midi1_ep_out_desc = {
	.bLength =		USB_DT_ENDPOINT_AUDIO_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,
	.bEndpointAddress =	USB_DIR_OUT | 0, // set up dynamically
	.bmAttributes =		USB_ENDPOINT_XFER_BULK,
};

static struct usb_ss_ep_comp_descriptor midi2_midi1_ep_out_ss_comp_desc = {
	.bLength                = sizeof(midi2_midi1_ep_out_ss_comp_desc),
	.bDescriptorType        = USB_DT_SS_ENDPOINT_COMP,
};

static struct usb_ms_endpoint_descriptor_1 midi2_midi1_ep_out_class_desc = {
	.bLength =		0x05,
	.bDescriptorType =	USB_DT_CS_ENDPOINT,
	.bDescriptorSubtype =	USB_MS_GENERAL,
	.bNumEmbMIDIJack =	1,
	.baAssocJackID =	{ 0x01 },
};

/* MIDI 1.0 EP IN */
static struct usb_endpoint_descriptor midi2_midi1_ep_in_desc = {
	.bLength =		USB_DT_ENDPOINT_AUDIO_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,
	.bEndpointAddress =	USB_DIR_IN | 0, // set up dynamically
	.bmAttributes =		USB_ENDPOINT_XFER_BULK,
};

static struct usb_ss_ep_comp_descriptor midi2_midi1_ep_in_ss_comp_desc = {
	.bLength                = sizeof(midi2_midi1_ep_in_ss_comp_desc),
	.bDescriptorType        = USB_DT_SS_ENDPOINT_COMP,
};

static struct usb_ms_endpoint_descriptor_1 midi2_midi1_ep_in_class_desc = {
	.bLength =		0x05,
	.bDescriptorType =	USB_DT_CS_ENDPOINT,
	.bDescriptorSubtype =	USB_MS_GENERAL,
	.bNumEmbMIDIJack =	1,
	.baAssocJackID =	{ 0x03 },
};

/* MIDI 2.0 Streaming Interface (altset 1) */
static struct usb_interface_descriptor midi2_midi2_if_desc = {
	.bLength =		USB_DT_INTERFACE_SIZE,
	.bDescriptorType =	USB_DT_INTERFACE,
	.bInterfaceNumber =	0, // to be filled
	.bAlternateSetting =	1,
	.bNumEndpoints =	2,
	.bInterfaceClass =	USB_CLASS_AUDIO,
	.bInterfaceSubClass =	USB_SUBCLASS_MIDISTREAMING,
	.bInterfaceProtocol =	0,
	.iInterface =		0, // to be filled
};

static struct usb_ms_header_descriptor midi2_midi2_class_desc = {
	.bLength =		0x07,
	.bDescriptorType =	USB_DT_CS_INTERFACE,
	.bDescriptorSubtype =	USB_MS_HEADER,
	.bcdMSC =		__cpu_to_le16(0x0200),
	.wTotalLength =		__cpu_to_le16(0x07),
};

/* MIDI 2.0 EP OUT */
static struct usb_endpoint_descriptor midi2_midi2_ep_out_desc[MAX_UMP_EPS];

static struct usb_ss_ep_comp_descriptor midi2_midi2_ep_out_ss_comp_desc = {
	.bLength                = sizeof(midi2_midi1_ep_out_ss_comp_desc),
	.bDescriptorType        = USB_DT_SS_ENDPOINT_COMP,
};

static struct usb_ms20_endpoint_descriptor_32 midi2_midi2_ep_out_class_desc[MAX_UMP_EPS];

/* MIDI 2.0 EP IN */
static struct usb_endpoint_descriptor midi2_midi2_ep_in_desc[MAX_UMP_EPS];

static struct usb_ss_ep_comp_descriptor midi2_midi2_ep_in_ss_comp_desc = {
	.bLength                = sizeof(midi2_midi2_ep_in_ss_comp_desc),
	.bDescriptorType        = USB_DT_SS_ENDPOINT_COMP,
};

static struct usb_ms20_endpoint_descriptor_32 midi2_midi2_ep_in_class_desc[MAX_UMP_EPS];

/* Arrays of descriptors to be created */
static void *midi2_audio_descs[] = {
	&midi2_audio_if_desc,
	&midi2_audio_class_desc,
	NULL
};

static void *midi2_midi1_descs[] = {
	&midi2_midi1_if_desc,
	&midi2_midi1_class_desc,
	&midi2_midi1_in_jack1_desc,
	&midi2_midi1_in_jack2_desc,
	&midi2_midi1_out_jack1_desc,
	&midi2_midi1_out_jack2_desc,
	NULL
};

static void *midi2_midi1_ep_descs[] = {
	&midi2_midi1_ep_out_desc,
	&midi2_midi1_ep_out_class_desc,
	&midi2_midi1_ep_in_desc,
	&midi2_midi1_ep_in_class_desc,
	NULL
};

static void *midi2_midi1_ep_ss_descs[] = {
	&midi2_midi1_ep_out_desc,
	&midi2_midi1_ep_out_ss_comp_desc,
	&midi2_midi1_ep_out_class_desc,
	&midi2_midi1_ep_in_desc,
	&midi2_midi1_ep_in_ss_comp_desc,
	&midi2_midi1_ep_in_class_desc,
	NULL
};

static void *midi2_midi2_descs[] = {
	&midi2_midi2_if_desc,
	&midi2_midi2_class_desc,
	NULL
};

/*
 * USB request handling
 */

/* get an empty request for the given EP */
static struct usb_request *get_empty_request(struct f_midi2_usb_ep *usb_ep)
{
	struct usb_request *req = NULL;
	unsigned long flags;
	int index;

	spin_lock_irqsave(&usb_ep->card->queue_lock, flags);
	if (!usb_ep->free_reqs)
		goto unlock;
	index = find_first_bit(&usb_ep->free_reqs, usb_ep->num_reqs);
	if (index >= usb_ep->num_reqs)
		goto unlock;
	req = usb_ep->reqs[index].req;
	if (!req)
		goto unlock;
	clear_bit(index, &usb_ep->free_reqs);
	req->length = 0;
 unlock:
	spin_unlock_irqrestore(&usb_ep->card->queue_lock, flags);
	return req;
}

/* put the empty request back */
static void put_empty_request(struct usb_request *req)
{
	struct f_midi2_req_ctx *ctx = req->context;
	unsigned long flags;

	spin_lock_irqsave(&ctx->usb_ep->card->queue_lock, flags);
	set_bit(ctx->index, &ctx->usb_ep->free_reqs);
	spin_unlock_irqrestore(&ctx->usb_ep->card->queue_lock, flags);
}

/*
 * UMP v1.1 Stream message handling
 */

/* queue a request to UMP EP; request is either queued or freed after this */
static int queue_request_ep_raw(struct usb_request *req)
{
	struct f_midi2_req_ctx *ctx = req->context;
	int err;

	req->complete = ctx->usb_ep->complete;
	err = usb_ep_queue(ctx->usb_ep->usb_ep, req, GFP_ATOMIC);
	if (err) {
		put_empty_request(req);
		return err;
	}
	return 0;
}

/* queue a request with endianness conversion */
static int queue_request_ep_in(struct usb_request *req)
{
	/* UMP packets have to be converted to little-endian */
	cpu_to_le32_array((u32 *)req->buf, req->length >> 2);
	return queue_request_ep_raw(req);
}

/* reply a UMP packet via EP-in */
static int reply_ep_in(struct f_midi2_ep *ep, const void *buf, int len)
{
	struct f_midi2_usb_ep *usb_ep = &ep->ep_in;
	struct usb_request *req;

	req = get_empty_request(usb_ep);
	if (!req)
		return -ENOSPC;

	req->length = len;
	memcpy(req->buf, buf, len);
	return queue_request_ep_in(req);
}

/* reply a UMP stream EP info */
static void reply_ump_stream_ep_info(struct f_midi2_ep *ep)
{
	struct snd_ump_stream_msg_ep_info rep = {
		.type = UMP_MSG_TYPE_STREAM,
		.status = UMP_STREAM_MSG_STATUS_EP_INFO,
		.ump_version_major = 0x01,
		.ump_version_minor = 0x01,
		.num_function_blocks = ep->num_blks,
		.static_function_block = !!ep->card->info.static_block,
		.protocol = (UMP_STREAM_MSG_EP_INFO_CAP_MIDI1 |
			     UMP_STREAM_MSG_EP_INFO_CAP_MIDI2) >> 8,
	};

	reply_ep_in(ep, &rep, sizeof(rep));
}

/* reply a UMP EP device info */
static void reply_ump_stream_ep_device(struct f_midi2_ep *ep)
{
	struct snd_ump_stream_msg_devince_info rep = {
		.type = UMP_MSG_TYPE_STREAM,
		.status = UMP_STREAM_MSG_STATUS_DEVICE_INFO,
		.manufacture_id = ep->info.manufacturer,
		.family_lsb = ep->info.family & 0xff,
		.family_msb = (ep->info.family >> 8) & 0xff,
		.model_lsb = ep->info.model & 0xff,
		.model_msb = (ep->info.model >> 8) & 0xff,
		.sw_revision = ep->info.sw_revision,
	};

	reply_ep_in(ep, &rep, sizeof(rep));
}

#define UMP_STREAM_PKT_BYTES	16	/* UMP stream packet size = 16 bytes*/
#define UMP_STREAM_EP_STR_OFF	2	/* offset of name string for EP info */
#define UMP_STREAM_FB_STR_OFF	3	/* offset of name string for FB info */

/* Helper to replay a string */
static void reply_ump_stream_string(struct f_midi2_ep *ep, const u8 *name,
				    unsigned int type, unsigned int extra,
				    unsigned int start_ofs)
{
	struct f_midi2_usb_ep *usb_ep = &ep->ep_in;
	struct f_midi2 *midi2 = ep->card;
	struct usb_request *req;
	unsigned int pos;
	u32 *buf;

	if (!*name)
		return;
	req = get_empty_request(usb_ep);
	if (!req)
		return;

	buf = (u32 *)req->buf;
	pos = start_ofs;
	for (;;) {
		if (pos == start_ofs) {
			memset(buf, 0, UMP_STREAM_PKT_BYTES);
			buf[0] = ump_stream_compose(type, 0) | extra;
		}
		buf[pos / 4] |= *name++ << ((3 - (pos % 4)) * 8);
		if (!*name) {
			if (req->length)
				buf[0] |= UMP_STREAM_MSG_FORMAT_END << 26;
			req->length += UMP_STREAM_PKT_BYTES;
			break;
		}
		if (++pos == UMP_STREAM_PKT_BYTES) {
			if (!req->length)
				buf[0] |= UMP_STREAM_MSG_FORMAT_START << 26;
			else
				buf[0] |= UMP_STREAM_MSG_FORMAT_CONTINUE << 26;
			req->length += UMP_STREAM_PKT_BYTES;
			if (midi2->info.req_buf_size - req->length < UMP_STREAM_PKT_BYTES)
				break;
			buf += 4;
			pos = start_ofs;
		}
	}

	if (req->length)
		queue_request_ep_in(req);
	else
		put_empty_request(req);
}

/* Reply a UMP EP name string */
static void reply_ump_stream_ep_name(struct f_midi2_ep *ep)
{
	reply_ump_stream_string(ep, ump_ep_name(ep),
				UMP_STREAM_MSG_STATUS_EP_NAME, 0,
				UMP_STREAM_EP_STR_OFF);
}

/* Reply a UMP EP product ID string */
static void reply_ump_stream_ep_pid(struct f_midi2_ep *ep)
{
	reply_ump_stream_string(ep, ump_product_id(ep),
				UMP_STREAM_MSG_STATUS_PRODUCT_ID, 0,
				UMP_STREAM_EP_STR_OFF);
}

/* Reply a UMP EP stream config */
static void reply_ump_stream_ep_config(struct f_midi2_ep *ep)
{
	struct snd_ump_stream_msg_stream_cfg rep = {
		.type = UMP_MSG_TYPE_STREAM,
		.status = UMP_STREAM_MSG_STATUS_STREAM_CFG,
	};

	if ((ep->info.protocol & SNDRV_UMP_EP_INFO_PROTO_MIDI_MASK) ==
	    SNDRV_UMP_EP_INFO_PROTO_MIDI2)
		rep.protocol = UMP_STREAM_MSG_EP_INFO_CAP_MIDI2 >> 8;
	else
		rep.protocol = UMP_STREAM_MSG_EP_INFO_CAP_MIDI1 >> 8;

	reply_ep_in(ep, &rep, sizeof(rep));
}

/* Reply a UMP FB info */
static void reply_ump_stream_fb_info(struct f_midi2_ep *ep, int blk)
{
	struct f_midi2_block_info *b = &ep->blks[blk].info;
	struct snd_ump_stream_msg_fb_info rep = {
		.type = UMP_MSG_TYPE_STREAM,
		.status = UMP_STREAM_MSG_STATUS_FB_INFO,
		.active = !!b->active,
		.function_block_id = blk,
		.ui_hint = b->ui_hint,
		.midi_10 = b->is_midi1,
		.direction = b->direction,
		.first_group = b->first_group,
		.num_groups = b->num_groups,
		.midi_ci_version = b->midi_ci_version,
		.sysex8_streams = b->sysex8_streams,
	};

	reply_ep_in(ep, &rep, sizeof(rep));
}

/* Reply a FB name string */
static void reply_ump_stream_fb_name(struct f_midi2_ep *ep, unsigned int blk)
{
	reply_ump_stream_string(ep, ump_fb_name(&ep->blks[blk].info),
				UMP_STREAM_MSG_STATUS_FB_NAME, blk << 8,
				UMP_STREAM_FB_STR_OFF);
}

/* Process a UMP Stream message */
static void process_ump_stream_msg(struct f_midi2_ep *ep, const u32 *data)
{
	struct f_midi2 *midi2 = ep->card;
	unsigned int format, status, blk;

	format = ump_stream_message_format(*data);
	status = ump_stream_message_status(*data);
	switch (status) {
	case UMP_STREAM_MSG_STATUS_EP_DISCOVERY:
		if (format)
			return; // invalid
		if (data[1] & UMP_STREAM_MSG_REQUEST_EP_INFO)
			reply_ump_stream_ep_info(ep);
		if (data[1] & UMP_STREAM_MSG_REQUEST_DEVICE_INFO)
			reply_ump_stream_ep_device(ep);
		if (data[1] & UMP_STREAM_MSG_REQUEST_EP_NAME)
			reply_ump_stream_ep_name(ep);
		if (data[1] & UMP_STREAM_MSG_REQUEST_PRODUCT_ID)
			reply_ump_stream_ep_pid(ep);
		if (data[1] & UMP_STREAM_MSG_REQUEST_STREAM_CFG)
			reply_ump_stream_ep_config(ep);
		return;
	case UMP_STREAM_MSG_STATUS_STREAM_CFG_REQUEST:
		if (*data & UMP_STREAM_MSG_EP_INFO_CAP_MIDI2) {
			ep->info.protocol = SNDRV_UMP_EP_INFO_PROTO_MIDI2;
			DBG(midi2, "Switching Protocol to MIDI2\n");
		} else {
			ep->info.protocol = SNDRV_UMP_EP_INFO_PROTO_MIDI1;
			DBG(midi2, "Switching Protocol to MIDI1\n");
		}
		snd_ump_switch_protocol(ep->ump, ep->info.protocol);
		reply_ump_stream_ep_config(ep);
		return;
	case UMP_STREAM_MSG_STATUS_FB_DISCOVERY:
		if (format)
			return; // invalid
		blk = (*data >> 8) & 0xff;
		if (blk >= ep->num_blks)
			return;
		if (*data & UMP_STREAM_MSG_REQUEST_FB_INFO)
			reply_ump_stream_fb_info(ep, blk);
		if (*data & UMP_STREAM_MSG_REQUEST_FB_NAME)
			reply_ump_stream_fb_name(ep, blk);
		return;
	}
}

/* Process UMP messages included in a USB request */
static void process_ump(struct f_midi2_ep *ep, const struct usb_request *req)
{
	const u32 *data = (u32 *)req->buf;
	int len = req->actual >> 2;
	const u32 *in_buf = ep->ump->input_buf;

	for (; len > 0; len--, data++) {
		if (snd_ump_receive_ump_val(ep->ump, *data) <= 0)
			continue;
		if (ump_message_type(*in_buf) == UMP_MSG_TYPE_STREAM)
			process_ump_stream_msg(ep, in_buf);
	}
}

/*
 * MIDI 2.0 UMP USB request handling
 */

/* complete handler for UMP EP-out requests */
static void f_midi2_ep_out_complete(struct usb_ep *usb_ep,
				    struct usb_request *req)
{
	struct f_midi2_req_ctx *ctx = req->context;
	struct f_midi2_ep *ep = ctx->usb_ep->ep;
	struct f_midi2 *midi2 = ep->card;
	int status = req->status;

	if (status) {
		DBG(midi2, "%s complete error %d: %d/%d\n",
		    usb_ep->name, status, req->actual, req->length);
		goto error;
	}

	/* convert to UMP packet in native endianness */
	le32_to_cpu_array((u32 *)req->buf, req->actual >> 2);

	if (midi2->info.process_ump)
		process_ump(ep, req);

	snd_ump_receive(ep->ump, req->buf, req->actual & ~3);

	if (midi2->operation_mode != MIDI_OP_MODE_MIDI2)
		goto error;

	if (queue_request_ep_raw(req))
		goto error;
	return;

 error:
	put_empty_request(req);
}

/* Transmit UMP packets received from user-space to the gadget */
static void process_ump_transmit(struct f_midi2_ep *ep)
{
	struct f_midi2_usb_ep *usb_ep = &ep->ep_in;
	struct f_midi2 *midi2 = ep->card;
	struct usb_request *req;
	int len;

	if (!usb_ep->usb_ep->enabled)
		return;

	for (;;) {
		req = get_empty_request(usb_ep);
		if (!req)
			break;
		len = snd_ump_transmit(ep->ump, (u32 *)req->buf,
				       midi2->info.req_buf_size);
		if (len <= 0) {
			put_empty_request(req);
			break;
		}

		req->length = len;
		if (queue_request_ep_in(req) < 0)
			break;
	}
}

/* Complete handler for UMP EP-in requests */
static void f_midi2_ep_in_complete(struct usb_ep *usb_ep,
				   struct usb_request *req)
{
	struct f_midi2_req_ctx *ctx = req->context;
	struct f_midi2_ep *ep = ctx->usb_ep->ep;
	struct f_midi2 *midi2 = ep->card;
	int status = req->status;

	put_empty_request(req);

	if (status) {
		DBG(midi2, "%s complete error %d: %d/%d\n",
		    usb_ep->name, status, req->actual, req->length);
		return;
	}

	process_ump_transmit(ep);
}

/* Start MIDI EP */
static int f_midi2_start_ep(struct f_midi2_usb_ep *usb_ep,
			    struct usb_function *fn)
{
	int err;

	usb_ep_disable(usb_ep->usb_ep);
	err = config_ep_by_speed(usb_ep->card->gadget, fn, usb_ep->usb_ep);
	if (err)
		return err;
	return usb_ep_enable(usb_ep->usb_ep);
}

/* Drop pending requests */
static void f_midi2_drop_reqs(struct f_midi2_usb_ep *usb_ep)
{
	int i;

	if (!usb_ep->num_reqs)
		return;

	for (i = 0; i < usb_ep->num_reqs; i++) {
		if (!test_bit(i, &usb_ep->free_reqs) && usb_ep->reqs[i].req) {
			usb_ep_dequeue(usb_ep->usb_ep, usb_ep->reqs[i].req);
			set_bit(i, &usb_ep->free_reqs);
		}
	}
}

/* Allocate requests for the given EP */
static int f_midi2_alloc_ep_reqs(struct f_midi2_usb_ep *usb_ep)
{
	struct f_midi2 *midi2 = usb_ep->card;
	int i;

	if (!usb_ep->reqs)
		return -EINVAL;

	for (i = 0; i < midi2->info.num_reqs; i++) {
		if (usb_ep->reqs[i].req)
			continue;
		usb_ep->reqs[i].req = alloc_ep_req(usb_ep->usb_ep,
						   midi2->info.req_buf_size);
		if (!usb_ep->reqs[i].req)
			return -ENOMEM;
		usb_ep->reqs[i].req->context = &usb_ep->reqs[i];
	}
	return 0;
}

/* Free allocated requests */
static void f_midi2_free_ep_reqs(struct f_midi2_usb_ep *usb_ep)
{
	struct f_midi2 *midi2 = usb_ep->card;
	int i;

	for (i = 0; i < midi2->info.num_reqs; i++) {
		if (!usb_ep->reqs[i].req)
			continue;
		free_ep_req(usb_ep->usb_ep, usb_ep->reqs[i].req);
		usb_ep->reqs[i].req = NULL;
	}
}

/* Initialize EP */
static int f_midi2_init_ep(struct f_midi2 *midi2, struct f_midi2_ep *ep,
			   struct f_midi2_usb_ep *usb_ep,
			   void *desc, int num_reqs,
			   void (*complete)(struct usb_ep *usb_ep,
					    struct usb_request *req))
{
	int i;

	usb_ep->card = midi2;
	usb_ep->ep = ep;
	usb_ep->usb_ep = usb_ep_autoconfig(midi2->gadget, desc);
	if (!usb_ep->usb_ep)
		return -ENODEV;
	usb_ep->complete = complete;

	if (num_reqs) {
		usb_ep->reqs = kcalloc(num_reqs, sizeof(*usb_ep->reqs),
				       GFP_KERNEL);
		if (!usb_ep->reqs)
			return -ENOMEM;
		for (i = 0; i < num_reqs; i++) {
			usb_ep->reqs[i].index = i;
			usb_ep->reqs[i].usb_ep = usb_ep;
			set_bit(i, &usb_ep->free_reqs);
			usb_ep->num_reqs++;
		}
	}

	return 0;
}

/* Free EP */
static void f_midi2_free_ep(struct f_midi2_usb_ep *usb_ep)
{
	f_midi2_drop_reqs(usb_ep);

	f_midi2_free_ep_reqs(usb_ep);

	kfree(usb_ep->reqs);
	usb_ep->num_reqs = 0;
	usb_ep->free_reqs = 0;
	usb_ep->reqs = NULL;
}

/* Queue requests for EP-out at start */
static void f_midi2_queue_out_reqs(struct f_midi2_usb_ep *usb_ep)
{
	int i, err;

	for (i = 0; i < usb_ep->num_reqs; i++) {
		if (!test_bit(i, &usb_ep->free_reqs) || !usb_ep->reqs[i].req)
			continue;
		usb_ep->reqs[i].req->complete = usb_ep->complete;
		err = usb_ep_queue(usb_ep->usb_ep, usb_ep->reqs[i].req,
				   GFP_ATOMIC);
		if (!err)
			clear_bit(i, &usb_ep->free_reqs);
	}
}

/*
 * Gadget Function callbacks
 */

/* gadget function set_alt callback */
static int f_midi2_set_alt(struct usb_function *fn, unsigned int intf,
			   unsigned int alt)
{
	struct f_midi2 *midi2 = func_to_midi2(fn);
	struct f_midi2_ep *ep;
	int i, op_mode, err;

	if (intf != midi2->midi_if || alt > 1)
		return 0;

	if (alt == 0)
		op_mode = MIDI_OP_MODE_MIDI1;
	else if (alt == 1)
		op_mode = MIDI_OP_MODE_MIDI2;
	else
		op_mode = MIDI_OP_MODE_UNSET;

	if (midi2->operation_mode == op_mode)
		return 0;

	midi2->operation_mode = op_mode;

	if (op_mode != MIDI_OP_MODE_MIDI2) {
		for (i = 0; i < midi2->num_eps; i++) {
			ep = &midi2->midi2_eps[i];
			f_midi2_drop_reqs(&ep->ep_in);
			f_midi2_drop_reqs(&ep->ep_out);
			f_midi2_free_ep_reqs(&ep->ep_in);
			f_midi2_free_ep_reqs(&ep->ep_out);
		}
		return 0;
	}

	for (i = 0; i < midi2->num_eps; i++) {
		ep = &midi2->midi2_eps[i];

		err = f_midi2_start_ep(&ep->ep_in, fn);
		if (err)
			return err;
		err = f_midi2_start_ep(&ep->ep_out, fn);
		if (err)
			return err;

		err = f_midi2_alloc_ep_reqs(&ep->ep_in);
		if (err)
			return err;
		err = f_midi2_alloc_ep_reqs(&ep->ep_out);
		if (err)
			return err;

		f_midi2_queue_out_reqs(&ep->ep_out);
	}

	return 0;
}

/* gadget function get_alt callback */
static int f_midi2_get_alt(struct usb_function *fn, unsigned int intf)
{
	struct f_midi2 *midi2 = func_to_midi2(fn);

	if (intf == midi2->midi_if &&
	    midi2->operation_mode == MIDI_OP_MODE_MIDI2)
		return 1;
	return 0;
}

/* convert UMP direction to USB MIDI 2.0 direction */
static unsigned int ump_to_usb_dir(unsigned int ump_dir)
{
	switch (ump_dir) {
	case SNDRV_UMP_DIR_INPUT:
		return USB_MS_GR_TRM_BLOCK_TYPE_INPUT_ONLY;
	case SNDRV_UMP_DIR_OUTPUT:
		return USB_MS_GR_TRM_BLOCK_TYPE_OUTPUT_ONLY;
	default:
		return USB_MS_GR_TRM_BLOCK_TYPE_BIDIRECTIONAL;
	}
}

/* assign GTB descriptors (for the given request) */
static void assign_block_descriptors(struct f_midi2 *midi2,
				     struct usb_request *req,
				     int max_len)
{
	struct usb_ms20_gr_trm_block_header_descriptor header;
	struct usb_ms20_gr_trm_block_descriptor *desc;
	struct f_midi2_block_info *b;
	struct f_midi2_ep *ep;
	int i, blk, len;
	char *data;

	len = sizeof(gtb_header_desc) + sizeof(gtb_desc) * midi2->total_blocks;
	if (WARN_ON(len > midi2->info.req_buf_size))
		return;

	header = gtb_header_desc;
	header.wTotalLength = cpu_to_le16(len);
	if (max_len < len) {
		len = min_t(int, len, sizeof(header));
		memcpy(req->buf, &header, len);
		req->length = len;
		req->zero = len < max_len;
		return;
	}

	memcpy(req->buf, &header, sizeof(header));
	data = req->buf + sizeof(header);
	for (i = 0; i < midi2->num_eps; i++) {
		ep = &midi2->midi2_eps[i];
		for (blk = 0; blk < ep->num_blks; blk++) {
			b = &ep->blks[blk].info;
			desc = (struct usb_ms20_gr_trm_block_descriptor *)data;

			*desc = gtb_desc;
			desc->bGrpTrmBlkID = ep->blks[blk].gtb_id;
			desc->bGrpTrmBlkType = ump_to_usb_dir(b->direction);
			desc->nGroupTrm = b->first_group;
			desc->nNumGroupTrm = b->num_groups;
			desc->iBlockItem = ep->blks[blk].string_id;

			if (ep->info.protocol & SNDRV_UMP_EP_INFO_PROTO_MIDI2)
				desc->bMIDIProtocol = USB_MS_MIDI_PROTO_2_0;
			else
				desc->bMIDIProtocol = USB_MS_MIDI_PROTO_1_0_128;

			if (b->is_midi1 == 2) {
				desc->wMaxInputBandwidth = cpu_to_le16(1);
				desc->wMaxOutputBandwidth = cpu_to_le16(1);
			}

			data += sizeof(*desc);
		}
	}

	req->length = len;
	req->zero = len < max_len;
}

/* gadget function setup callback: handle GTB requests */
static int f_midi2_setup(struct usb_function *fn,
			 const struct usb_ctrlrequest *ctrl)
{
	struct f_midi2 *midi2 = func_to_midi2(fn);
	struct usb_composite_dev *cdev = fn->config->cdev;
	struct usb_request *req = cdev->req;
	u16 value, length;

	if ((ctrl->bRequestType & USB_TYPE_MASK) != USB_TYPE_STANDARD ||
	    ctrl->bRequest != USB_REQ_GET_DESCRIPTOR)
		return -EOPNOTSUPP;

	value = le16_to_cpu(ctrl->wValue);
	length = le16_to_cpu(ctrl->wLength);

	if ((value >> 8) != USB_DT_CS_GR_TRM_BLOCK)
		return -EOPNOTSUPP;

	/* handle only altset 1 */
	if ((value & 0xff) != 1)
		return -EOPNOTSUPP;

	assign_block_descriptors(midi2, req, length);
	return usb_ep_queue(cdev->gadget->ep0, req, GFP_ATOMIC);
}

/* gadget function disable callback */
static void f_midi2_disable(struct usb_function *fn)
{
	struct f_midi2 *midi2 = func_to_midi2(fn);

	midi2->operation_mode = MIDI_OP_MODE_UNSET;
}

/*
 * ALSA UMP ops: most of them are NOPs, only trigger for write is needed
 */
static int f_midi2_ump_open(struct snd_ump_endpoint *ump, int dir)
{
	return 0;
}

static void f_midi2_ump_close(struct snd_ump_endpoint *ump, int dir)
{
}

static void f_midi2_ump_trigger(struct snd_ump_endpoint *ump, int dir, int up)
{
	struct f_midi2_ep *ep = ump->private_data;

	if (up && dir == SNDRV_RAWMIDI_STREAM_OUTPUT)
		process_ump_transmit(ep);
}

static void f_midi2_ump_drain(struct snd_ump_endpoint *ump, int dir)
{
}

static const struct snd_ump_ops f_midi2_ump_ops = {
	.open = f_midi2_ump_open,
	.close = f_midi2_ump_close,
	.trigger = f_midi2_ump_trigger,
	.drain = f_midi2_ump_drain,
};

/*
 * ALSA UMP instance creation / deletion
 */
static void f_midi2_free_card(struct f_midi2 *midi2)
{
	if (midi2->card) {
		snd_card_free_when_closed(midi2->card);
		midi2->card = NULL;
	}
}

/* use a reverse direction for the gadget host */
static int reverse_dir(int dir)
{
	if (!dir || dir == SNDRV_UMP_DIR_BIDIRECTION)
		return dir;
	return (dir == SNDRV_UMP_DIR_OUTPUT) ?
		SNDRV_UMP_DIR_INPUT : SNDRV_UMP_DIR_OUTPUT;
}

static int f_midi2_create_card(struct f_midi2 *midi2)
{
	struct snd_card *card;
	struct snd_ump_endpoint *ump;
	struct f_midi2_ep *ep;
	int i, id, blk, err;
	__be32 sw;

	err = snd_card_new(&midi2->gadget->dev, -1, NULL, THIS_MODULE, 0,
			   &card);
	if (err < 0)
		return err;
	midi2->card = card;

	strcpy(card->driver, "f_midi2");
	strcpy(card->shortname, "MIDI 2.0 Gadget");
	strcpy(card->longname, "MIDI 2.0 Gadget");

	id = 0;
	for (i = 0; i < midi2->num_eps; i++) {
		ep = &midi2->midi2_eps[i];
		err = snd_ump_endpoint_new(card, "MIDI 2.0 Gadget", id,
					   1, 1, &ump);
		if (err < 0)
			goto error;
		id++;

		ep->ump = ump;
		ump->no_process_stream = true;
		ump->private_data = ep;
		ump->ops = &f_midi2_ump_ops;
		if (midi2->info.static_block)
			ump->info.flags |= SNDRV_UMP_EP_INFO_STATIC_BLOCKS;
		ump->info.protocol_caps = (ep->info.protocol_caps & 3) << 8;
		ump->info.protocol = (ep->info.protocol & 3) << 8;
		ump->info.version = 0x0101;
		ump->info.family_id = ep->info.family;
		ump->info.model_id = ep->info.model;
		ump->info.manufacturer_id = ep->info.manufacturer & 0xffffff;
		sw = cpu_to_be32(ep->info.sw_revision);
		memcpy(ump->info.sw_revision, &sw, 4);

		strscpy(ump->info.name, ump_ep_name(ep),
			sizeof(ump->info.name));
		strscpy(ump->info.product_id, ump_product_id(ep),
			sizeof(ump->info.product_id));
		strscpy(ump->core.name, ump->info.name, sizeof(ump->core.name));

		for (blk = 0; blk < ep->num_blks; blk++) {
			const struct f_midi2_block_info *b = &ep->blks[blk].info;
			struct snd_ump_block *fb;

			err = snd_ump_block_new(ump, blk,
						reverse_dir(b->direction),
						b->first_group, b->num_groups,
						&ep->blks[blk].fb);
			if (err < 0)
				goto error;
			fb = ep->blks[blk].fb;
			fb->info.active = !!b->active;
			fb->info.midi_ci_version = b->midi_ci_version;
			fb->info.ui_hint = reverse_dir(b->ui_hint);
			fb->info.sysex8_streams = b->sysex8_streams;
			fb->info.flags |= b->is_midi1;
			strscpy(fb->info.name, ump_fb_name(b),
				sizeof(fb->info.name));
		}
	}

	for (i = 0; i < midi2->num_eps; i++) {
		err = snd_ump_attach_legacy_rawmidi(midi2->midi2_eps[i].ump,
						    "Legacy MIDI", id);
		if (err < 0)
			goto error;
		id++;
	}

	err = snd_card_register(card);
	if (err < 0)
		goto error;

	return 0;

 error:
	f_midi2_free_card(midi2);
	return err;
}

/*
 * Creation of USB descriptors
 */
struct f_midi2_usb_config {
	struct usb_descriptor_header **list;
	unsigned int size;
	unsigned int alloc;
};

static int append_config(struct f_midi2_usb_config *config, void *d)
{
	unsigned int size;
	void *buf;

	if (config->size + 2 >= config->alloc) {
		size = config->size + 16;
		buf = krealloc(config->list, size * sizeof(void *), GFP_KERNEL);
		if (!buf)
			return -ENOMEM;
		config->list = buf;
		config->alloc = size;
	}

	config->list[config->size] = d;
	config->size++;
	config->list[config->size] = NULL;
	return 0;
}

static int append_configs(struct f_midi2_usb_config *config, void **d)
{
	int err;

	for (; *d; d++) {
		err = append_config(config, *d);
		if (err)
			return err;
	}
	return 0;
}

static int f_midi2_create_usb_configs(struct f_midi2 *midi2,
				      struct f_midi2_usb_config *config,
				      int speed)
{
	void **midi1_eps;
	int i, err;

	switch (speed) {
	default:
	case USB_SPEED_HIGH:
		midi2_midi1_ep_out_desc.wMaxPacketSize = cpu_to_le16(512);
		midi2_midi1_ep_in_desc.wMaxPacketSize = cpu_to_le16(512);
		for (i = 0; i < midi2->num_eps; i++)
			midi2_midi2_ep_out_desc[i].wMaxPacketSize =
				cpu_to_le16(512);
		fallthrough;
	case USB_SPEED_FULL:
		midi1_eps = midi2_midi1_ep_descs;
		break;
	case USB_SPEED_SUPER:
	case USB_SPEED_SUPER_PLUS:
		midi2_midi1_ep_out_desc.wMaxPacketSize = cpu_to_le16(1024);
		midi2_midi1_ep_in_desc.wMaxPacketSize = cpu_to_le16(1024);
		for (i = 0; i < midi2->num_eps; i++)
			midi2_midi2_ep_out_desc[i].wMaxPacketSize =
				cpu_to_le16(1024);
		midi1_eps = midi2_midi1_ep_ss_descs;
		break;
	}

	err = append_configs(config, midi2_audio_descs);
	if (err < 0)
		return err;
	err = append_configs(config, midi2_midi1_descs);
	if (err < 0)
		return err;
	err = append_configs(config, midi1_eps);
	if (err < 0)
		return err;
	err = append_configs(config, midi2_midi2_descs);
	if (err < 0)
		return err;

	for (i = 0; i < midi2->num_eps; i++) {
		err = append_config(config, &midi2_midi2_ep_out_desc[i]);
		if (err < 0)
			return err;
		if (speed == USB_SPEED_SUPER || speed == USB_SPEED_SUPER_PLUS) {
			err = append_config(config, &midi2_midi2_ep_out_ss_comp_desc);
			if (err < 0)
				return err;
		}
		err = append_config(config, &midi2_midi2_ep_out_class_desc[i]);
		if (err < 0)
			return err;
		err = append_config(config, &midi2_midi2_ep_in_desc[i]);
		if (err < 0)
			return err;
		if (speed == USB_SPEED_SUPER || speed == USB_SPEED_SUPER_PLUS) {
			err = append_config(config, &midi2_midi2_ep_in_ss_comp_desc);
			if (err < 0)
				return err;
		}
		err = append_config(config, &midi2_midi2_ep_in_class_desc[i]);
		if (err < 0)
			return err;
	}

	return 0;
}

static void f_midi2_free_usb_configs(struct f_midi2_usb_config *config)
{
	kfree(config->list);
	memset(config, 0, sizeof(*config));
}

/* as we use the static descriptors for simplicity, serialize bind call */
static DEFINE_MUTEX(f_midi2_desc_mutex);

/* fill MIDI2 EP class-specific descriptor */
static void fill_midi2_class_desc(struct f_midi2_ep *ep,
				  struct usb_ms20_endpoint_descriptor_32 *cdesc)
{
	int blk;

	cdesc->bLength = USB_DT_MS20_ENDPOINT_SIZE(ep->num_blks);
	cdesc->bDescriptorType = USB_DT_CS_ENDPOINT;
	cdesc->bDescriptorSubtype = USB_MS_GENERAL_2_0;
	cdesc->bNumGrpTrmBlock = ep->num_blks;
	for (blk = 0; blk < ep->num_blks; blk++)
		cdesc->baAssoGrpTrmBlkID[blk] = ep->blks[blk].gtb_id;
}

/* initialize MIDI2 EP-in */
static int f_midi2_init_midi2_ep_in(struct f_midi2 *midi2, int index)
{
	struct f_midi2_ep *ep = &midi2->midi2_eps[index];
	struct usb_endpoint_descriptor *desc = &midi2_midi2_ep_in_desc[index];

	desc->bLength = USB_DT_ENDPOINT_SIZE;
	desc->bDescriptorType = USB_DT_ENDPOINT;
	desc->bEndpointAddress = USB_DIR_IN;
	desc->bmAttributes = USB_ENDPOINT_XFER_INT;
	desc->wMaxPacketSize = cpu_to_le16(EP_MAX_PACKET_INT);
	desc->bInterval = 1;

	fill_midi2_class_desc(ep, &midi2_midi2_ep_in_class_desc[index]);

	return f_midi2_init_ep(midi2, ep, &ep->ep_in, desc,
			       midi2->info.num_reqs, f_midi2_ep_in_complete);
}

/* initialize MIDI2 EP-out */
static int f_midi2_init_midi2_ep_out(struct f_midi2 *midi2, int index)
{
	struct f_midi2_ep *ep = &midi2->midi2_eps[index];
	struct usb_endpoint_descriptor *desc = &midi2_midi2_ep_out_desc[index];

	desc->bLength = USB_DT_ENDPOINT_SIZE;
	desc->bDescriptorType = USB_DT_ENDPOINT;
	desc->bEndpointAddress = USB_DIR_OUT;
	desc->bmAttributes = USB_ENDPOINT_XFER_BULK;

	fill_midi2_class_desc(ep, &midi2_midi2_ep_out_class_desc[index]);

	return f_midi2_init_ep(midi2, ep, &ep->ep_out, desc,
			       midi2->info.num_reqs, f_midi2_ep_out_complete);
}

/* gadget function bind callback */
static int f_midi2_bind(struct usb_configuration *c, struct usb_function *f)
{
	struct usb_composite_dev *cdev = c->cdev;
	struct f_midi2 *midi2 = func_to_midi2(f);
	struct f_midi2_ep *ep;
	struct f_midi2_usb_config config = {};
	struct usb_gadget_strings string_fn = {
		.language = 0x0409,	/* en-us */
		.strings = midi2->string_defs,
	};
	struct usb_gadget_strings *strings[] = {
		&string_fn,
		NULL,
	};
	int i, blk, status;

	midi2->gadget = cdev->gadget;
	midi2->operation_mode = MIDI_OP_MODE_UNSET;

	status = f_midi2_create_card(midi2);
	if (status < 0)
		goto fail_register;

	/* maybe allocate device-global string ID */
	midi2->strings = usb_gstrings_attach(c->cdev, strings,
					     midi2->total_blocks + 1);
	if (IS_ERR(midi2->strings)) {
		status = PTR_ERR(midi2->strings);
		goto fail_string;
	}

	mutex_lock(&f_midi2_desc_mutex);
	midi2_midi1_if_desc.iInterface = midi2->strings[STR_IFACE].id;
	midi2_midi2_if_desc.iInterface = midi2->strings[STR_IFACE].id;
	for (i = 0; i < midi2->num_eps; i++) {
		ep = &midi2->midi2_eps[i];
		for (blk = 0; blk < ep->num_blks; blk++)
			ep->blks[blk].string_id =
				midi2->strings[gtb_to_str_id(ep->blks[blk].gtb_id)].id;
	}

	/* audio interface */
	status = usb_interface_id(c, f);
	if (status < 0)
		goto fail;
	midi2_audio_if_desc.bInterfaceNumber = status;

	/* MIDI streaming */
	status = usb_interface_id(c, f);
	if (status < 0)
		goto fail;
	midi2->midi_if = status;
	midi2_midi1_if_desc.bInterfaceNumber = status;
	midi2_midi2_if_desc.bInterfaceNumber = status;
	midi2_audio_class_desc.baInterfaceNr[0] = status;

	/* allocate instance-specific endpoints */
	status = f_midi2_init_ep(midi2, NULL, &midi2->midi1_ep_in,
				 &midi2_midi1_ep_in_desc, 0, NULL);
	if (status)
		goto fail;
	status = f_midi2_init_ep(midi2, NULL, &midi2->midi1_ep_out,
				 &midi2_midi1_ep_out_desc, 0, NULL);
	if (status)
		goto fail;

	for (i = 0; i < midi2->num_eps; i++) {
		status = f_midi2_init_midi2_ep_in(midi2, i);
		if (status)
			goto fail;
		status = f_midi2_init_midi2_ep_out(midi2, i);
		if (status)
			goto fail;
	}

	status = f_midi2_create_usb_configs(midi2, &config, USB_SPEED_FULL);
	if (status < 0)
		goto fail;
	f->fs_descriptors = usb_copy_descriptors(config.list);
	if (!f->fs_descriptors) {
		status = -ENOMEM;
		goto fail;
	}
	f_midi2_free_usb_configs(&config);

	if (gadget_is_dualspeed(midi2->gadget)) {
		status = f_midi2_create_usb_configs(midi2, &config, USB_SPEED_HIGH);
		if (status < 0)
			goto fail;
		f->hs_descriptors = usb_copy_descriptors(config.list);
		if (!f->hs_descriptors) {
			status = -ENOMEM;
			goto fail;
		}
		f_midi2_free_usb_configs(&config);
	}

	if (gadget_is_superspeed(midi2->gadget)) {
		status = f_midi2_create_usb_configs(midi2, &config, USB_SPEED_SUPER);
		if (status < 0)
			goto fail;
		f->ss_descriptors = usb_copy_descriptors(config.list);
		if (!f->ss_descriptors) {
			status = -ENOMEM;
			goto fail;
		}
		if (gadget_is_superspeed_plus(midi2->gadget)) {
			f->ssp_descriptors = usb_copy_descriptors(config.list);
			if (!f->ssp_descriptors) {
				status = -ENOMEM;
				goto fail;
			}
		}
		f_midi2_free_usb_configs(&config);
	}

	mutex_unlock(&f_midi2_desc_mutex);
	return 0;

fail:
	f_midi2_free_usb_configs(&config);
	mutex_unlock(&f_midi2_desc_mutex);
	usb_free_all_descriptors(f);
fail_string:
	f_midi2_free_card(midi2);
fail_register:
	ERROR(midi2, "%s: can't bind, err %d\n", f->name, status);
	return status;
}

/* gadget function unbind callback */
static void f_midi2_unbind(struct usb_configuration *c, struct usb_function *f)
{
	struct f_midi2 *midi2 = func_to_midi2(f);
	int i;

	f_midi2_free_card(midi2);

	f_midi2_free_ep(&midi2->midi1_ep_in);
	f_midi2_free_ep(&midi2->midi1_ep_out);
	for (i = 0; i < midi2->num_eps; i++) {
		f_midi2_free_ep(&midi2->midi2_eps[i].ep_in);
		f_midi2_free_ep(&midi2->midi2_eps[i].ep_out);
	}

	usb_free_all_descriptors(f);
}

/* create a f_midi2_block_opts instance for the given block number */
static int f_midi2_block_opts_create(struct f_midi2_ep_opts *ep_opts,
				     unsigned int blk,
				     struct f_midi2_block_opts **block_p)
{
	struct f_midi2_block_opts *block_opts;

	block_opts = kzalloc(sizeof(*block_opts), GFP_KERNEL);
	if (!block_opts)
		return -ENOMEM;

	block_opts->ep = ep_opts;
	block_opts->id = blk;

	/* set up the default values */
	block_opts->info.direction = SNDRV_UMP_DIR_BIDIRECTION;
	block_opts->info.first_group = 0;
	block_opts->info.num_groups = 1;
	block_opts->info.ui_hint = SNDRV_UMP_BLOCK_UI_HINT_BOTH;
	block_opts->info.active = 1;

	ep_opts->blks[blk] = block_opts;
	*block_p = block_opts;
	return 0;
}

/* create a f_midi2_ep_opts instance */
static int f_midi2_ep_opts_create(struct f_midi2_opts *opts,
				  unsigned int index,
				  struct f_midi2_ep_opts **ep_p)
{
	struct f_midi2_ep_opts *ep_opts;

	ep_opts = kzalloc(sizeof(*ep_opts), GFP_KERNEL);
	if (!ep_opts)
		return -ENOMEM;

	ep_opts->opts = opts;
	ep_opts->index = index;

	/* set up the default values */
	ep_opts->info.protocol = 2;
	ep_opts->info.protocol_caps = 3;

	opts->eps[index] = ep_opts;
	*ep_p = ep_opts;
	return 0;
}

static const struct config_item_type f_midi2_func_type = {
	.ct_owner	= THIS_MODULE,
};

static void f_midi2_free_inst(struct usb_function_instance *f)
{
	struct f_midi2_opts *opts;

	opts = container_of(f, struct f_midi2_opts, func_inst);

	/* we have only one EP and one FB */
	if (opts->eps[0]) {
		kfree(opts->eps[0]->blks[0]);
		kfree(opts->eps[0]);
	}
	kfree(opts);
}

/* gadget alloc_inst */
static struct usb_function_instance *f_midi2_alloc_inst(void)
{
	struct f_midi2_opts *opts;
	struct f_midi2_ep_opts *ep_opts;
	struct f_midi2_block_opts *block_opts;
	int ret;

	opts = kzalloc(sizeof(*opts), GFP_KERNEL);
	if (!opts)
		return ERR_PTR(-ENOMEM);

	mutex_init(&opts->lock);
	opts->func_inst.free_func_inst = f_midi2_free_inst;
	opts->info.process_ump = true;
	opts->info.static_block = true;
	opts->info.num_reqs = 32;
	opts->info.req_buf_size = 512;

	ret = f_midi2_ep_opts_create(opts, 0, &ep_opts);
	if (ret) {
		kfree(opts);
		return ERR_PTR(ret);
	}

	/* create the default block */
	ret = f_midi2_block_opts_create(ep_opts, 0, &block_opts);
	if (ret) {
		kfree(ep_opts);
		kfree(opts);
		return ERR_PTR(ret);
	}

	config_group_init_type_name(&opts->func_inst.group, "",
				    &f_midi2_func_type);
	return &opts->func_inst;
}

static void do_f_midi2_free(struct f_midi2 *midi2, struct f_midi2_opts *opts)
{
	mutex_lock(&opts->lock);
	--opts->refcnt;
	mutex_unlock(&opts->lock);
	kfree(midi2->string_defs);
	kfree(midi2);
}

static void f_midi2_free(struct usb_function *f)
{
	do_f_midi2_free(func_to_midi2(f),
			container_of(f->fi, struct f_midi2_opts, func_inst));
}

/* gadget alloc callback */
static struct usb_function *f_midi2_alloc(struct usb_function_instance *fi)
{
	struct f_midi2 *midi2;
	struct f_midi2_opts *opts;
	int i;

	midi2 = kzalloc(sizeof(*midi2), GFP_KERNEL);
	if (!midi2)
		return ERR_PTR(-ENOMEM);

	opts = container_of(fi, struct f_midi2_opts, func_inst);
	mutex_lock(&opts->lock);
	++opts->refcnt;
	mutex_unlock(&opts->lock);

	spin_lock_init(&midi2->queue_lock);

	midi2->func.name = "midi2_func";
	midi2->func.bind = f_midi2_bind;
	midi2->func.unbind = f_midi2_unbind;
	midi2->func.get_alt = f_midi2_get_alt;
	midi2->func.set_alt = f_midi2_set_alt;
	midi2->func.setup = f_midi2_setup;
	midi2->func.disable = f_midi2_disable;
	midi2->func.free_func = f_midi2_free;

	midi2->info = opts->info;

	/* fixed 1 UMP EP and 1 UMP FB as of now */
	midi2->num_eps = 1;
	midi2->midi2_eps[0].info = opts->eps[0]->info;
	midi2->midi2_eps[0].card = midi2;
	midi2->midi2_eps[0].num_blks = 1;
	midi2->midi2_eps[0].blks[0].info = opts->eps[0]->blks[0]->info;
	midi2->midi2_eps[0].blks[0].gtb_id = 1;

	for (i = 0; i < midi2->num_eps; i++)
		midi2->total_blocks += midi2->midi2_eps[i].num_blks;

	midi2->string_defs = kcalloc(midi2->total_blocks + 1,
				     sizeof(*midi2->string_defs), GFP_KERNEL);
	if (!midi2->string_defs) {
		do_f_midi2_free(midi2, opts);
		return ERR_PTR(-ENOMEM);
	}

	if (opts->info.iface_name && *opts->info.iface_name)
		midi2->string_defs[0].s = opts->info.iface_name;
	else
		midi2->string_defs[0].s = ump_ep_name(&midi2->midi2_eps[0]);
	midi2->string_defs[1].s = ump_fb_name(&midi2->midi2_eps[0].blks[0].info);

	return &midi2->func;
}

DECLARE_USB_FUNCTION_INIT(midi2, f_midi2_alloc_inst, f_midi2_alloc);

MODULE_LICENSE("GPL");
