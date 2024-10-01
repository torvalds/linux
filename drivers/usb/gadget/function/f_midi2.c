// SPDX-License-Identifier: GPL-2.0+
/*
 * f_midi2.c -- USB MIDI 2.0 class function driver
 */

#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>

#include <sound/core.h>
#include <sound/control.h>
#include <sound/ump.h>
#include <sound/ump_msg.h>
#include <sound/ump_convert.h>

#include <linux/usb/ch9.h>
#include <linux/usb/func_utils.h>
#include <linux/usb/gadget.h>
#include <linux/usb/audio.h>
#include <linux/usb/midi-v2.h>

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

/* Temporary buffer for altset 0 MIDI 1.0 handling */
struct f_midi2_midi1_port {
	unsigned int pending; /* pending bytes on the input buffer */
	u8 buf[32];	/* raw MIDI 1.0 byte input */
	u8 state;	/* running status */
	u8 data[2];	/* rendered USB MIDI 1.0 packet data */
};

/* MIDI 1.0 message states */
enum {
	STATE_INITIAL = 0,	/* pseudo state */
	STATE_1PARAM,
	STATE_2PARAM_1,
	STATE_2PARAM_2,
	STATE_SYSEX_0,
	STATE_SYSEX_1,
	STATE_SYSEX_2,
	STATE_REAL_TIME,
	STATE_FINISHED,		/* pseudo state */
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

	u8 in_group_to_cable[SNDRV_UMP_MAX_GROUPS]; /* map to cable; 1-based! */
};

/* indices for USB strings */
enum {
	STR_IFACE = 0,
	STR_GTB1 = 1,
};

/* 1-based GTB id to string id */
#define gtb_to_str_id(id)	(STR_GTB1 + (id) - 1)

/* mapping from MIDI 1.0 cable to UMP group */
struct midi1_cable_mapping {
	struct f_midi2_ep *ep;
	unsigned char block;
	unsigned char group;
};

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

	/* number of MIDI 1.0 I/O cables */
	unsigned int num_midi1_in;
	unsigned int num_midi1_out;

	/* conversion for MIDI 1.0 EP-in */
	struct f_midi2_midi1_port midi1_port[MAX_CABLES];
	/* conversion for MIDI 1.0 EP-out */
	struct ump_cvt_to_ump midi1_ump_cvt;
	/* mapping between cables and UMP groups */
	struct midi1_cable_mapping in_cable_mapping[MAX_CABLES];
	struct midi1_cable_mapping out_cable_mapping[MAX_CABLES];

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

/* convert from MIDI protocol number (1 or 2) to SNDRV_UMP_EP_INFO_PROTO_* */
#define to_ump_protocol(v)	(((v) & 3) << 8)

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
DECLARE_USB_MS_ENDPOINT_DESCRIPTOR(16);
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
	.bNumEndpoints =	2, // to be filled
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

static struct usb_ms_endpoint_descriptor_16 midi2_midi1_ep_out_class_desc = {
	.bLength =		0x05, // to be filled
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

static struct usb_ms_endpoint_descriptor_16 midi2_midi1_ep_in_class_desc = {
	.bLength =		0x05, // to be filled
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
	.bNumEndpoints =	2, // to be filled
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
	NULL
};

static void *midi2_midi1_ep_out_descs[] = {
	&midi2_midi1_ep_out_desc,
	&midi2_midi1_ep_out_class_desc,
	NULL
};

static void *midi2_midi1_ep_in_descs[] = {
	&midi2_midi1_ep_in_desc,
	&midi2_midi1_ep_in_class_desc,
	NULL
};

static void *midi2_midi1_ep_out_ss_descs[] = {
	&midi2_midi1_ep_out_desc,
	&midi2_midi1_ep_out_ss_comp_desc,
	&midi2_midi1_ep_out_class_desc,
	NULL
};

static void *midi2_midi1_ep_in_ss_descs[] = {
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

	if (ep->info.protocol == 2)
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
			ep->info.protocol = 2;
			DBG(midi2, "Switching Protocol to MIDI2\n");
		} else {
			ep->info.protocol = 1;
			DBG(midi2, "Switching Protocol to MIDI1\n");
		}
		snd_ump_switch_protocol(ep->ump, to_ump_protocol(ep->info.protocol));
		reply_ump_stream_ep_config(ep);
		return;
	case UMP_STREAM_MSG_STATUS_FB_DISCOVERY:
		if (format)
			return; // invalid
		blk = (*data >> 8) & 0xff;
		if (blk == 0xff) {
			/* inquiry for all blocks */
			for (blk = 0; blk < ep->num_blks; blk++) {
				if (*data & UMP_STREAM_MSG_REQUEST_FB_INFO)
					reply_ump_stream_fb_info(ep, blk);
				if (*data & UMP_STREAM_MSG_REQUEST_FB_NAME)
					reply_ump_stream_fb_name(ep, blk);
			}
		} else if (blk < ep->num_blks) {
			/* only the specified block */
			if (*data & UMP_STREAM_MSG_REQUEST_FB_INFO)
				reply_ump_stream_fb_info(ep, blk);
			if (*data & UMP_STREAM_MSG_REQUEST_FB_NAME)
				reply_ump_stream_fb_name(ep, blk);
		}
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

/*
 * MIDI1 (altset 0) USB request handling
 */

/* process one MIDI byte -- copied from f_midi.c
 *
 * fill the packet or request if needed
 * returns true if the request became empty (queued)
 */
static bool process_midi1_byte(struct f_midi2 *midi2, u8 cable, u8 b,
			       struct usb_request **req_p)
{
	struct f_midi2_midi1_port *port = &midi2->midi1_port[cable];
	u8 p[4] = { cable << 4, 0, 0, 0 };
	int next_state = STATE_INITIAL;
	struct usb_request *req = *req_p;

	switch (b) {
	case 0xf8 ... 0xff:
		/* System Real-Time Messages */
		p[0] |= 0x0f;
		p[1] = b;
		next_state = port->state;
		port->state = STATE_REAL_TIME;
		break;

	case 0xf7:
		/* End of SysEx */
		switch (port->state) {
		case STATE_SYSEX_0:
			p[0] |= 0x05;
			p[1] = 0xf7;
			next_state = STATE_FINISHED;
			break;
		case STATE_SYSEX_1:
			p[0] |= 0x06;
			p[1] = port->data[0];
			p[2] = 0xf7;
			next_state = STATE_FINISHED;
			break;
		case STATE_SYSEX_2:
			p[0] |= 0x07;
			p[1] = port->data[0];
			p[2] = port->data[1];
			p[3] = 0xf7;
			next_state = STATE_FINISHED;
			break;
		default:
			/* Ignore byte */
			next_state = port->state;
			port->state = STATE_INITIAL;
		}
		break;

	case 0xf0 ... 0xf6:
		/* System Common Messages */
		port->data[0] = port->data[1] = 0;
		port->state = STATE_INITIAL;
		switch (b) {
		case 0xf0:
			port->data[0] = b;
			port->data[1] = 0;
			next_state = STATE_SYSEX_1;
			break;
		case 0xf1:
		case 0xf3:
			port->data[0] = b;
			next_state = STATE_1PARAM;
			break;
		case 0xf2:
			port->data[0] = b;
			next_state = STATE_2PARAM_1;
			break;
		case 0xf4:
		case 0xf5:
			next_state = STATE_INITIAL;
			break;
		case 0xf6:
			p[0] |= 0x05;
			p[1] = 0xf6;
			next_state = STATE_FINISHED;
			break;
		}
		break;

	case 0x80 ... 0xef:
		/*
		 * Channel Voice Messages, Channel Mode Messages
		 * and Control Change Messages.
		 */
		port->data[0] = b;
		port->data[1] = 0;
		port->state = STATE_INITIAL;
		if (b >= 0xc0 && b <= 0xdf)
			next_state = STATE_1PARAM;
		else
			next_state = STATE_2PARAM_1;
		break;

	case 0x00 ... 0x7f:
		/* Message parameters */
		switch (port->state) {
		case STATE_1PARAM:
			if (port->data[0] < 0xf0)
				p[0] |= port->data[0] >> 4;
			else
				p[0] |= 0x02;

			p[1] = port->data[0];
			p[2] = b;
			/* This is to allow Running State Messages */
			next_state = STATE_1PARAM;
			break;
		case STATE_2PARAM_1:
			port->data[1] = b;
			next_state = STATE_2PARAM_2;
			break;
		case STATE_2PARAM_2:
			if (port->data[0] < 0xf0)
				p[0] |= port->data[0] >> 4;
			else
				p[0] |= 0x03;

			p[1] = port->data[0];
			p[2] = port->data[1];
			p[3] = b;
			/* This is to allow Running State Messages */
			next_state = STATE_2PARAM_1;
			break;
		case STATE_SYSEX_0:
			port->data[0] = b;
			next_state = STATE_SYSEX_1;
			break;
		case STATE_SYSEX_1:
			port->data[1] = b;
			next_state = STATE_SYSEX_2;
			break;
		case STATE_SYSEX_2:
			p[0] |= 0x04;
			p[1] = port->data[0];
			p[2] = port->data[1];
			p[3] = b;
			next_state = STATE_SYSEX_0;
			break;
		}
		break;
	}

	/* States where we have to write into the USB request */
	if (next_state == STATE_FINISHED ||
	    port->state == STATE_SYSEX_2 ||
	    port->state == STATE_1PARAM ||
	    port->state == STATE_2PARAM_2 ||
	    port->state == STATE_REAL_TIME) {
		memcpy(req->buf + req->length, p, sizeof(p));
		req->length += sizeof(p);

		if (next_state == STATE_FINISHED) {
			next_state = STATE_INITIAL;
			port->data[0] = port->data[1] = 0;
		}

		if (midi2->info.req_buf_size - req->length <= 4) {
			queue_request_ep_raw(req);
			*req_p = NULL;
			return true;
		}
	}

	port->state = next_state;
	return false;
}

/* process all pending MIDI bytes in the internal buffer;
 * returns true if the request gets empty
 * returns false if all have been processed
 */
static bool process_midi1_pending_buf(struct f_midi2 *midi2,
				      struct usb_request **req_p)
{
	unsigned int cable, c;

	for (cable = 0; cable < midi2->num_midi1_in; cable++) {
		struct f_midi2_midi1_port *port = &midi2->midi1_port[cable];

		if (!port->pending)
			continue;
		for (c = 0; c < port->pending; c++) {
			if (process_midi1_byte(midi2, cable, port->buf[c],
					       req_p)) {
				port->pending -= c;
				if (port->pending)
					memmove(port->buf, port->buf + c,
						port->pending);
				return true;
			}
		}
		port->pending = 0;
	}

	return false;
}

/* fill the MIDI bytes onto the temporary buffer
 */
static void fill_midi1_pending_buf(struct f_midi2 *midi2, u8 cable, u8 *buf,
				   unsigned int size)
{
	struct f_midi2_midi1_port *port = &midi2->midi1_port[cable];

	if (port->pending + size > sizeof(port->buf))
		return;
	memcpy(port->buf + port->pending, buf, size);
	port->pending += size;
}

/* try to process data given from the associated UMP stream */
static void process_midi1_transmit(struct f_midi2 *midi2)
{
	struct f_midi2_usb_ep *usb_ep = &midi2->midi1_ep_in;
	struct f_midi2_ep *ep = &midi2->midi2_eps[0];
	struct usb_request *req = NULL;
	/* 12 is the largest outcome (4 MIDI1 cmds) for a single UMP packet */
	unsigned char outbuf[12];
	unsigned char group, cable;
	int len, size;
	u32 ump;

	if (!usb_ep->usb_ep || !usb_ep->usb_ep->enabled)
		return;

	for (;;) {
		if (!req) {
			req = get_empty_request(usb_ep);
			if (!req)
				break;
		}

		if (process_midi1_pending_buf(midi2, &req))
			continue;

		len = snd_ump_transmit(ep->ump, &ump, 4);
		if (len <= 0)
			break;
		if (snd_ump_receive_ump_val(ep->ump, ump) <= 0)
			continue;
		size = snd_ump_convert_from_ump(ep->ump->input_buf, outbuf,
						&group);
		if (size <= 0)
			continue;
		cable = ep->in_group_to_cable[group];
		if (!cable)
			continue;
		cable--; /* to 0-base */
		fill_midi1_pending_buf(midi2, cable, outbuf, size);
	}

	if (req) {
		if (req->length)
			queue_request_ep_raw(req);
		else
			put_empty_request(req);
	}
}

/* complete handler for MIDI1 EP-in requests */
static void f_midi2_midi1_ep_in_complete(struct usb_ep *usb_ep,
					 struct usb_request *req)
{
	struct f_midi2_req_ctx *ctx = req->context;
	struct f_midi2 *midi2 = ctx->usb_ep->card;
	int status = req->status;

	put_empty_request(req);

	if (status) {
		DBG(midi2, "%s complete error %d: %d/%d\n",
		    usb_ep->name, status, req->actual, req->length);
		return;
	}

	process_midi1_transmit(midi2);
}

/* complete handler for MIDI1 EP-out requests */
static void f_midi2_midi1_ep_out_complete(struct usb_ep *usb_ep,
					  struct usb_request *req)
{
	struct f_midi2_req_ctx *ctx = req->context;
	struct f_midi2 *midi2 = ctx->usb_ep->card;
	struct f_midi2_ep *ep;
	struct ump_cvt_to_ump *cvt = &midi2->midi1_ump_cvt;
	static const u8 midi1_packet_bytes[16] = {
		0, 0, 2, 3, 3, 1, 2, 3, 3, 3, 3, 3, 2, 2, 3, 1
	};
	unsigned int group, cable, bytes, c, len;
	int status = req->status;
	const u8 *buf = req->buf;

	if (status) {
		DBG(midi2, "%s complete error %d: %d/%d\n",
		    usb_ep->name, status, req->actual, req->length);
		goto error;
	}

	len = req->actual >> 2;
	for (; len; len--, buf += 4) {
		cable = *buf >> 4;
		ep = midi2->out_cable_mapping[cable].ep;
		if (!ep)
			continue;
		group = midi2->out_cable_mapping[cable].group;
		bytes = midi1_packet_bytes[*buf & 0x0f];
		for (c = 0; c < bytes; c++) {
			snd_ump_convert_to_ump(cvt, group,
					       to_ump_protocol(ep->info.protocol),
					       buf[c + 1]);
			if (cvt->ump_bytes) {
				snd_ump_receive(ep->ump, cvt->ump,
						cvt->ump_bytes);
				cvt->ump_bytes = 0;
			}
		}
	}

	if (midi2->operation_mode != MIDI_OP_MODE_MIDI1)
		goto error;

	if (queue_request_ep_raw(req))
		goto error;
	return;

 error:
	put_empty_request(req);
}

/*
 * Common EP handling helpers
 */

/* Start MIDI EP */
static int f_midi2_start_ep(struct f_midi2_usb_ep *usb_ep,
			    struct usb_function *fn)
{
	int err;

	if (!usb_ep->usb_ep)
		return 0;

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

	if (!usb_ep->usb_ep || !usb_ep->num_reqs)
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

	if (!usb_ep->usb_ep)
		return 0;
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
			   void *desc,
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

	usb_ep->reqs = kcalloc(midi2->info.num_reqs, sizeof(*usb_ep->reqs),
			       GFP_KERNEL);
	if (!usb_ep->reqs)
		return -ENOMEM;
	for (i = 0; i < midi2->info.num_reqs; i++) {
		usb_ep->reqs[i].index = i;
		usb_ep->reqs[i].usb_ep = usb_ep;
		set_bit(i, &usb_ep->free_reqs);
		usb_ep->num_reqs++;
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

	if (!usb_ep->usb_ep)
		return;

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

/* stop both IN and OUT EPs */
static void f_midi2_stop_eps(struct f_midi2_usb_ep *ep_in,
			     struct f_midi2_usb_ep *ep_out)
{
	f_midi2_drop_reqs(ep_in);
	f_midi2_drop_reqs(ep_out);
	f_midi2_free_ep_reqs(ep_in);
	f_midi2_free_ep_reqs(ep_out);
}

/* start/queue both IN and OUT EPs */
static int f_midi2_start_eps(struct f_midi2_usb_ep *ep_in,
			     struct f_midi2_usb_ep *ep_out,
			     struct usb_function *fn)
{
	int err;

	err = f_midi2_start_ep(ep_in, fn);
	if (err)
		return err;
	err = f_midi2_start_ep(ep_out, fn);
	if (err)
		return err;

	err = f_midi2_alloc_ep_reqs(ep_in);
	if (err)
		return err;
	err = f_midi2_alloc_ep_reqs(ep_out);
	if (err)
		return err;

	f_midi2_queue_out_reqs(ep_out);
	return 0;
}

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

	if (op_mode != MIDI_OP_MODE_MIDI1)
		f_midi2_stop_eps(&midi2->midi1_ep_in, &midi2->midi1_ep_out);

	if (op_mode != MIDI_OP_MODE_MIDI2) {
		for (i = 0; i < midi2->num_eps; i++) {
			ep = &midi2->midi2_eps[i];
			f_midi2_stop_eps(&ep->ep_in, &ep->ep_out);
		}
	}

	if (op_mode == MIDI_OP_MODE_MIDI1)
		return f_midi2_start_eps(&midi2->midi1_ep_in,
					 &midi2->midi1_ep_out, fn);

	if (op_mode == MIDI_OP_MODE_MIDI2) {
		for (i = 0; i < midi2->num_eps; i++) {
			ep = &midi2->midi2_eps[i];

			err = f_midi2_start_eps(&ep->ep_in, &ep->ep_out, fn);
			if (err)
				return err;
		}
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

			if (ep->info.protocol == 2)
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
	struct f_midi2 *midi2 = ep->card;

	if (up && dir == SNDRV_RAWMIDI_STREAM_OUTPUT) {
		switch (midi2->operation_mode) {
		case MIDI_OP_MODE_MIDI1:
			process_midi1_transmit(midi2);
			break;
		case MIDI_OP_MODE_MIDI2:
			process_ump_transmit(ep);
			break;
		}
	}
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
 * "Operation Mode" control element
 */
static int f_midi2_operation_mode_info(struct snd_kcontrol *kcontrol,
				       struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = MIDI_OP_MODE_UNSET;
	uinfo->value.integer.max = MIDI_OP_MODE_MIDI2;
	return 0;
}

static int f_midi2_operation_mode_get(struct snd_kcontrol *kcontrol,
				      struct snd_ctl_elem_value *ucontrol)
{
	struct f_midi2 *midi2 = snd_kcontrol_chip(kcontrol);

	ucontrol->value.integer.value[0] = midi2->operation_mode;
	return 0;
}

static const struct snd_kcontrol_new operation_mode_ctl = {
	.iface = SNDRV_CTL_ELEM_IFACE_RAWMIDI,
	.name = "Operation Mode",
	.access = SNDRV_CTL_ELEM_ACCESS_READ | SNDRV_CTL_ELEM_ACCESS_VOLATILE,
	.info = f_midi2_operation_mode_info,
	.get = f_midi2_operation_mode_get,
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
		ump->info.protocol = to_ump_protocol(ep->info.protocol);
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

	err = snd_ctl_add(card, snd_ctl_new1(&operation_mode_ctl, midi2));
	if (err < 0)
		goto error;

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

	/* MIDI 1.0 jacks */
	unsigned char jack_in, jack_out, jack_id;
	struct usb_midi_in_jack_descriptor jack_ins[MAX_CABLES];
	struct usb_midi_out_jack_descriptor_1 jack_outs[MAX_CABLES];
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

static int append_midi1_in_jack(struct f_midi2 *midi2,
				struct f_midi2_usb_config *config,
				struct midi1_cable_mapping *map,
				unsigned int type)
{
	struct usb_midi_in_jack_descriptor *jack =
		&config->jack_ins[config->jack_in++];
	int id = ++config->jack_id;
	int err;

	jack->bLength = 0x06;
	jack->bDescriptorType = USB_DT_CS_INTERFACE;
	jack->bDescriptorSubtype = USB_MS_MIDI_IN_JACK;
	jack->bJackType = type;
	jack->bJackID = id;
	/* use the corresponding block name as jack name */
	if (map->ep)
		jack->iJack = map->ep->blks[map->block].string_id;

	err = append_config(config, jack);
	if (err < 0)
		return err;
	return id;
}

static int append_midi1_out_jack(struct f_midi2 *midi2,
				 struct f_midi2_usb_config *config,
				 struct midi1_cable_mapping *map,
				 unsigned int type, unsigned int source)
{
	struct usb_midi_out_jack_descriptor_1 *jack =
		&config->jack_outs[config->jack_out++];
	int id = ++config->jack_id;
	int err;

	jack->bLength = 0x09;
	jack->bDescriptorType = USB_DT_CS_INTERFACE;
	jack->bDescriptorSubtype = USB_MS_MIDI_OUT_JACK;
	jack->bJackType = type;
	jack->bJackID = id;
	jack->bNrInputPins = 1;
	jack->pins[0].baSourceID = source;
	jack->pins[0].baSourcePin = 0x01;
	/* use the corresponding block name as jack name */
	if (map->ep)
		jack->iJack = map->ep->blks[map->block].string_id;

	err = append_config(config, jack);
	if (err < 0)
		return err;
	return id;
}

static int f_midi2_create_usb_configs(struct f_midi2 *midi2,
				      struct f_midi2_usb_config *config,
				      int speed)
{
	void **midi1_in_eps, **midi1_out_eps;
	int i, jack, total;
	int err;

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
		midi1_in_eps = midi2_midi1_ep_in_descs;
		midi1_out_eps = midi2_midi1_ep_out_descs;
		break;
	case USB_SPEED_SUPER:
		midi2_midi1_ep_out_desc.wMaxPacketSize = cpu_to_le16(1024);
		midi2_midi1_ep_in_desc.wMaxPacketSize = cpu_to_le16(1024);
		for (i = 0; i < midi2->num_eps; i++)
			midi2_midi2_ep_out_desc[i].wMaxPacketSize =
				cpu_to_le16(1024);
		midi1_in_eps = midi2_midi1_ep_in_ss_descs;
		midi1_out_eps = midi2_midi1_ep_out_ss_descs;
		break;
	}

	err = append_configs(config, midi2_audio_descs);
	if (err < 0)
		return err;

	if (midi2->num_midi1_in && midi2->num_midi1_out)
		midi2_midi1_if_desc.bNumEndpoints = 2;
	else
		midi2_midi1_if_desc.bNumEndpoints = 1;

	err = append_configs(config, midi2_midi1_descs);
	if (err < 0)
		return err;

	total = USB_DT_MS_HEADER_SIZE;
	if (midi2->num_midi1_out) {
		midi2_midi1_ep_out_class_desc.bLength =
			USB_DT_MS_ENDPOINT_SIZE(midi2->num_midi1_out);
		total += midi2_midi1_ep_out_class_desc.bLength;
		midi2_midi1_ep_out_class_desc.bNumEmbMIDIJack =
			midi2->num_midi1_out;
		total += midi2->num_midi1_out *
			(USB_DT_MIDI_IN_SIZE + USB_DT_MIDI_OUT_SIZE(1));
		for (i = 0; i < midi2->num_midi1_out; i++) {
			jack = append_midi1_in_jack(midi2, config,
						    &midi2->in_cable_mapping[i],
						    USB_MS_EMBEDDED);
			if (jack < 0)
				return jack;
			midi2_midi1_ep_out_class_desc.baAssocJackID[i] = jack;
			jack = append_midi1_out_jack(midi2, config,
						     &midi2->in_cable_mapping[i],
						     USB_MS_EXTERNAL, jack);
			if (jack < 0)
				return jack;
		}
	}

	if (midi2->num_midi1_in) {
		midi2_midi1_ep_in_class_desc.bLength =
			USB_DT_MS_ENDPOINT_SIZE(midi2->num_midi1_in);
		total += midi2_midi1_ep_in_class_desc.bLength;
		midi2_midi1_ep_in_class_desc.bNumEmbMIDIJack =
			midi2->num_midi1_in;
		total += midi2->num_midi1_in *
			(USB_DT_MIDI_IN_SIZE + USB_DT_MIDI_OUT_SIZE(1));
		for (i = 0; i < midi2->num_midi1_in; i++) {
			jack = append_midi1_in_jack(midi2, config,
						    &midi2->out_cable_mapping[i],
						    USB_MS_EXTERNAL);
			if (jack < 0)
				return jack;
			jack = append_midi1_out_jack(midi2, config,
						     &midi2->out_cable_mapping[i],
						     USB_MS_EMBEDDED, jack);
			if (jack < 0)
				return jack;
			midi2_midi1_ep_in_class_desc.baAssocJackID[i] = jack;
		}
	}

	midi2_midi1_class_desc.wTotalLength = cpu_to_le16(total);

	if (midi2->num_midi1_out) {
		err = append_configs(config, midi1_out_eps);
		if (err < 0)
			return err;
	}
	if (midi2->num_midi1_in) {
		err = append_configs(config, midi1_in_eps);
		if (err < 0)
			return err;
	}

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
			       f_midi2_ep_in_complete);
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
			       f_midi2_ep_out_complete);
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

	midi2_midi2_if_desc.bNumEndpoints = midi2->num_eps * 2;

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
	if (midi2->midi2_eps[0].blks[0].info.direction != SNDRV_UMP_DIR_OUTPUT) {
		status = f_midi2_init_ep(midi2, NULL, &midi2->midi1_ep_in,
					 &midi2_midi1_ep_in_desc,
					 f_midi2_midi1_ep_in_complete);
		if (status)
			goto fail;
	}

	if (midi2->midi2_eps[0].blks[0].info.direction != SNDRV_UMP_DIR_INPUT) {
		status = f_midi2_init_ep(midi2, NULL, &midi2->midi1_ep_out,
					 &midi2_midi1_ep_out_desc,
					 f_midi2_midi1_ep_out_complete);
		if (status)
			goto fail;
	}

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

	status = f_midi2_create_usb_configs(midi2, &config, USB_SPEED_HIGH);
	if (status < 0)
		goto fail;
	f->hs_descriptors = usb_copy_descriptors(config.list);
	if (!f->hs_descriptors) {
		status = -ENOMEM;
		goto fail;
	}
	f_midi2_free_usb_configs(&config);

	status = f_midi2_create_usb_configs(midi2, &config, USB_SPEED_SUPER);
	if (status < 0)
		goto fail;
	f->ss_descriptors = usb_copy_descriptors(config.list);
	if (!f->ss_descriptors) {
		status = -ENOMEM;
		goto fail;
	}
	f_midi2_free_usb_configs(&config);

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

/*
 * ConfigFS interface
 */

/* type conversion helpers */
static inline struct f_midi2_opts *to_f_midi2_opts(struct config_item *item)
{
	return container_of(to_config_group(item), struct f_midi2_opts,
			    func_inst.group);
}

static inline struct f_midi2_ep_opts *
to_f_midi2_ep_opts(struct config_item *item)
{
	return container_of(to_config_group(item), struct f_midi2_ep_opts,
			    group);
}

static inline struct f_midi2_block_opts *
to_f_midi2_block_opts(struct config_item *item)
{
	return container_of(to_config_group(item), struct f_midi2_block_opts,
			    group);
}

/* trim the string to be usable for EP and FB name strings */
static void make_name_string(char *s)
{
	char *p;

	p = strchr(s, '\n');
	if (p)
		*p = 0;

	p = s + strlen(s);
	for (; p > s && isspace(*p); p--)
		*p = 0;
}

/* configfs helpers: generic show/store for unisnged int */
static ssize_t f_midi2_opts_uint_show(struct f_midi2_opts *opts,
				      u32 val, const char *format, char *page)
{
	int result;

	mutex_lock(&opts->lock);
	result = sprintf(page, format, val);
	mutex_unlock(&opts->lock);
	return result;
}

static ssize_t f_midi2_opts_uint_store(struct f_midi2_opts *opts,
				       u32 *valp, u32 minval, u32 maxval,
				       const char *page, size_t len)
{
	int ret;
	u32 val;

	mutex_lock(&opts->lock);
	if (opts->refcnt) {
		ret = -EBUSY;
		goto end;
	}

	ret = kstrtou32(page, 0, &val);
	if (ret)
		goto end;
	if (val < minval || val > maxval) {
		ret = -EINVAL;
		goto end;
	}

	*valp = val;
	ret = len;

end:
	mutex_unlock(&opts->lock);
	return ret;
}

/* generic store for bool */
static ssize_t f_midi2_opts_bool_store(struct f_midi2_opts *opts,
				       bool *valp, const char *page, size_t len)
{
	int ret;
	bool val;

	mutex_lock(&opts->lock);
	if (opts->refcnt) {
		ret = -EBUSY;
		goto end;
	}

	ret = kstrtobool(page, &val);
	if (ret)
		goto end;
	*valp = val;
	ret = len;

end:
	mutex_unlock(&opts->lock);
	return ret;
}

/* generic show/store for string */
static ssize_t f_midi2_opts_str_show(struct f_midi2_opts *opts,
				     const char *str, char *page)
{
	int result = 0;

	mutex_lock(&opts->lock);
	if (str)
		result = scnprintf(page, PAGE_SIZE, "%s\n", str);
	mutex_unlock(&opts->lock);
	return result;
}

static ssize_t f_midi2_opts_str_store(struct f_midi2_opts *opts,
				      const char **strp, size_t maxlen,
				      const char *page, size_t len)
{
	char *c;
	int ret;

	mutex_lock(&opts->lock);
	if (opts->refcnt) {
		ret = -EBUSY;
		goto end;
	}

	c = kstrndup(page, min(len, maxlen), GFP_KERNEL);
	if (!c) {
		ret = -ENOMEM;
		goto end;
	}

	kfree(*strp);
	make_name_string(c);
	*strp = c;
	ret = len;

end:
	mutex_unlock(&opts->lock);
	return ret;
}

/*
 * Definitions for UMP Block config
 */

/* define an uint option for block */
#define F_MIDI2_BLOCK_OPT(name, format, minval, maxval)			\
static ssize_t f_midi2_block_opts_##name##_show(struct config_item *item,\
					  char *page)			\
{									\
	struct f_midi2_block_opts *opts = to_f_midi2_block_opts(item);	\
	return f_midi2_opts_uint_show(opts->ep->opts, opts->info.name,	\
				      format "\n", page);		\
}									\
									\
static ssize_t f_midi2_block_opts_##name##_store(struct config_item *item,\
					 const char *page, size_t len)	\
{									\
	struct f_midi2_block_opts *opts = to_f_midi2_block_opts(item);	\
	return f_midi2_opts_uint_store(opts->ep->opts, &opts->info.name,\
				       minval, maxval, page, len);	\
}									\
									\
CONFIGFS_ATTR(f_midi2_block_opts_, name)

/* define a boolean option for block */
#define F_MIDI2_BLOCK_BOOL_OPT(name)					\
static ssize_t f_midi2_block_opts_##name##_show(struct config_item *item,\
					  char *page)			\
{									\
	struct f_midi2_block_opts *opts = to_f_midi2_block_opts(item);	\
	return f_midi2_opts_uint_show(opts->ep->opts, opts->info.name,	\
				      "%u\n", page);			\
}									\
									\
static ssize_t f_midi2_block_opts_##name##_store(struct config_item *item,\
					 const char *page, size_t len)	\
{									\
	struct f_midi2_block_opts *opts = to_f_midi2_block_opts(item);	\
	return f_midi2_opts_bool_store(opts->ep->opts, &opts->info.name,\
				       page, len);			\
}									\
									\
CONFIGFS_ATTR(f_midi2_block_opts_, name)

F_MIDI2_BLOCK_OPT(direction, "0x%x", 1, 3);
F_MIDI2_BLOCK_OPT(first_group, "0x%x", 0, 15);
F_MIDI2_BLOCK_OPT(num_groups, "0x%x", 1, 16);
F_MIDI2_BLOCK_OPT(midi1_first_group, "0x%x", 0, 15);
F_MIDI2_BLOCK_OPT(midi1_num_groups, "0x%x", 0, 16);
F_MIDI2_BLOCK_OPT(ui_hint, "0x%x", 0, 3);
F_MIDI2_BLOCK_OPT(midi_ci_version, "%u", 0, 1);
F_MIDI2_BLOCK_OPT(sysex8_streams, "%u", 0, 255);
F_MIDI2_BLOCK_OPT(is_midi1, "%u", 0, 2);
F_MIDI2_BLOCK_BOOL_OPT(active);

static ssize_t f_midi2_block_opts_name_show(struct config_item *item,
					    char *page)
{
	struct f_midi2_block_opts *opts = to_f_midi2_block_opts(item);

	return f_midi2_opts_str_show(opts->ep->opts, opts->info.name, page);
}

static ssize_t f_midi2_block_opts_name_store(struct config_item *item,
					     const char *page, size_t len)
{
	struct f_midi2_block_opts *opts = to_f_midi2_block_opts(item);

	return f_midi2_opts_str_store(opts->ep->opts, &opts->info.name, 128,
				      page, len);
}

CONFIGFS_ATTR(f_midi2_block_opts_, name);

static struct configfs_attribute *f_midi2_block_attrs[] = {
	&f_midi2_block_opts_attr_direction,
	&f_midi2_block_opts_attr_first_group,
	&f_midi2_block_opts_attr_num_groups,
	&f_midi2_block_opts_attr_midi1_first_group,
	&f_midi2_block_opts_attr_midi1_num_groups,
	&f_midi2_block_opts_attr_ui_hint,
	&f_midi2_block_opts_attr_midi_ci_version,
	&f_midi2_block_opts_attr_sysex8_streams,
	&f_midi2_block_opts_attr_is_midi1,
	&f_midi2_block_opts_attr_active,
	&f_midi2_block_opts_attr_name,
	NULL,
};

static void f_midi2_block_opts_release(struct config_item *item)
{
	struct f_midi2_block_opts *opts = to_f_midi2_block_opts(item);

	kfree(opts->info.name);
	kfree(opts);
}

static struct configfs_item_operations f_midi2_block_item_ops = {
	.release	= f_midi2_block_opts_release,
};

static const struct config_item_type f_midi2_block_type = {
	.ct_item_ops	= &f_midi2_block_item_ops,
	.ct_attrs	= f_midi2_block_attrs,
	.ct_owner	= THIS_MODULE,
};

/* create a f_midi2_block_opts instance for the given block number */
static int f_midi2_block_opts_create(struct f_midi2_ep_opts *ep_opts,
				     unsigned int blk,
				     struct f_midi2_block_opts **block_p)
{
	struct f_midi2_block_opts *block_opts;
	int ret = 0;

	mutex_lock(&ep_opts->opts->lock);
	if (ep_opts->opts->refcnt || ep_opts->blks[blk]) {
		ret = -EBUSY;
		goto out;
	}

	block_opts = kzalloc(sizeof(*block_opts), GFP_KERNEL);
	if (!block_opts) {
		ret = -ENOMEM;
		goto out;
	}

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

 out:
	mutex_unlock(&ep_opts->opts->lock);
	return ret;
}

/* make_group callback for a block */
static struct config_group *
f_midi2_opts_block_make(struct config_group *group, const char *name)
{
	struct f_midi2_ep_opts *ep_opts;
	struct f_midi2_block_opts *block_opts;
	unsigned int blk;
	int ret;

	if (strncmp(name, "block.", 6))
		return ERR_PTR(-EINVAL);
	ret = kstrtouint(name + 6, 10, &blk);
	if (ret)
		return ERR_PTR(ret);

	ep_opts = to_f_midi2_ep_opts(&group->cg_item);

	if (blk >= SNDRV_UMP_MAX_BLOCKS)
		return ERR_PTR(-EINVAL);
	if (ep_opts->blks[blk])
		return ERR_PTR(-EBUSY);
	ret = f_midi2_block_opts_create(ep_opts, blk, &block_opts);
	if (ret)
		return ERR_PTR(ret);

	config_group_init_type_name(&block_opts->group, name,
				    &f_midi2_block_type);
	return &block_opts->group;
}

/* drop_item callback for a block */
static void
f_midi2_opts_block_drop(struct config_group *group, struct config_item *item)
{
	struct f_midi2_block_opts *block_opts = to_f_midi2_block_opts(item);

	mutex_lock(&block_opts->ep->opts->lock);
	block_opts->ep->blks[block_opts->id] = NULL;
	mutex_unlock(&block_opts->ep->opts->lock);
	config_item_put(item);
}

/*
 * Definitions for UMP Endpoint config
 */

/* define an uint option for EP */
#define F_MIDI2_EP_OPT(name, format, minval, maxval)			\
static ssize_t f_midi2_ep_opts_##name##_show(struct config_item *item,	\
					     char *page)		\
{									\
	struct f_midi2_ep_opts *opts = to_f_midi2_ep_opts(item);	\
	return f_midi2_opts_uint_show(opts->opts, opts->info.name,	\
				      format "\n", page);		\
}									\
									\
static ssize_t f_midi2_ep_opts_##name##_store(struct config_item *item,	\
					   const char *page, size_t len)\
{									\
	struct f_midi2_ep_opts *opts = to_f_midi2_ep_opts(item);	\
	return f_midi2_opts_uint_store(opts->opts, &opts->info.name,	\
				       minval, maxval, page, len);	\
}									\
									\
CONFIGFS_ATTR(f_midi2_ep_opts_, name)

/* define a string option for EP */
#define F_MIDI2_EP_STR_OPT(name, maxlen)				\
static ssize_t f_midi2_ep_opts_##name##_show(struct config_item *item,	\
					     char *page)		\
{									\
	struct f_midi2_ep_opts *opts = to_f_midi2_ep_opts(item);	\
	return f_midi2_opts_str_show(opts->opts, opts->info.name, page);\
}									\
									\
static ssize_t f_midi2_ep_opts_##name##_store(struct config_item *item,	\
					 const char *page, size_t len)	\
{									\
	struct f_midi2_ep_opts *opts = to_f_midi2_ep_opts(item);	\
	return f_midi2_opts_str_store(opts->opts, &opts->info.name, maxlen,\
				      page, len);			\
}									\
									\
CONFIGFS_ATTR(f_midi2_ep_opts_, name)

F_MIDI2_EP_OPT(protocol, "0x%x", 1, 2);
F_MIDI2_EP_OPT(protocol_caps, "0x%x", 1, 3);
F_MIDI2_EP_OPT(manufacturer, "0x%x", 0, 0xffffff);
F_MIDI2_EP_OPT(family, "0x%x", 0, 0xffff);
F_MIDI2_EP_OPT(model, "0x%x", 0, 0xffff);
F_MIDI2_EP_OPT(sw_revision, "0x%x", 0, 0xffffffff);
F_MIDI2_EP_STR_OPT(ep_name, 128);
F_MIDI2_EP_STR_OPT(product_id, 128);

static struct configfs_attribute *f_midi2_ep_attrs[] = {
	&f_midi2_ep_opts_attr_protocol,
	&f_midi2_ep_opts_attr_protocol_caps,
	&f_midi2_ep_opts_attr_ep_name,
	&f_midi2_ep_opts_attr_product_id,
	&f_midi2_ep_opts_attr_manufacturer,
	&f_midi2_ep_opts_attr_family,
	&f_midi2_ep_opts_attr_model,
	&f_midi2_ep_opts_attr_sw_revision,
	NULL,
};

static void f_midi2_ep_opts_release(struct config_item *item)
{
	struct f_midi2_ep_opts *opts = to_f_midi2_ep_opts(item);

	kfree(opts->info.ep_name);
	kfree(opts->info.product_id);
	kfree(opts);
}

static struct configfs_item_operations f_midi2_ep_item_ops = {
	.release	= f_midi2_ep_opts_release,
};

static struct configfs_group_operations f_midi2_ep_group_ops = {
	.make_group	= f_midi2_opts_block_make,
	.drop_item	= f_midi2_opts_block_drop,
};

static const struct config_item_type f_midi2_ep_type = {
	.ct_item_ops	= &f_midi2_ep_item_ops,
	.ct_group_ops	= &f_midi2_ep_group_ops,
	.ct_attrs	= f_midi2_ep_attrs,
	.ct_owner	= THIS_MODULE,
};

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

/* make_group callback for an EP */
static struct config_group *
f_midi2_opts_ep_make(struct config_group *group, const char *name)
{
	struct f_midi2_opts *opts;
	struct f_midi2_ep_opts *ep_opts;
	unsigned int index;
	int ret;

	if (strncmp(name, "ep.", 3))
		return ERR_PTR(-EINVAL);
	ret = kstrtouint(name + 3, 10, &index);
	if (ret)
		return ERR_PTR(ret);

	opts = to_f_midi2_opts(&group->cg_item);
	if (index >= MAX_UMP_EPS)
		return ERR_PTR(-EINVAL);
	if (opts->eps[index])
		return ERR_PTR(-EBUSY);
	ret = f_midi2_ep_opts_create(opts, index, &ep_opts);
	if (ret)
		return ERR_PTR(ret);

	config_group_init_type_name(&ep_opts->group, name, &f_midi2_ep_type);
	return &ep_opts->group;
}

/* drop_item callback for an EP */
static void
f_midi2_opts_ep_drop(struct config_group *group, struct config_item *item)
{
	struct f_midi2_ep_opts *ep_opts = to_f_midi2_ep_opts(item);

	mutex_lock(&ep_opts->opts->lock);
	ep_opts->opts->eps[ep_opts->index] = NULL;
	mutex_unlock(&ep_opts->opts->lock);
	config_item_put(item);
}

/*
 * Definitions for card config
 */

/* define a bool option for card */
#define F_MIDI2_BOOL_OPT(name)						\
static ssize_t f_midi2_opts_##name##_show(struct config_item *item,	\
					  char *page)			\
{									\
	struct f_midi2_opts *opts = to_f_midi2_opts(item);		\
	return f_midi2_opts_uint_show(opts, opts->info.name,		\
				      "%u\n", page);			\
}									\
									\
static ssize_t f_midi2_opts_##name##_store(struct config_item *item,	\
					 const char *page, size_t len)	\
{									\
	struct f_midi2_opts *opts = to_f_midi2_opts(item);		\
	return f_midi2_opts_bool_store(opts, &opts->info.name,		\
				       page, len);			\
}									\
									\
CONFIGFS_ATTR(f_midi2_opts_, name)

F_MIDI2_BOOL_OPT(process_ump);
F_MIDI2_BOOL_OPT(static_block);

static ssize_t f_midi2_opts_iface_name_show(struct config_item *item,
					    char *page)
{
	struct f_midi2_opts *opts = to_f_midi2_opts(item);

	return f_midi2_opts_str_show(opts, opts->info.iface_name, page);
}

static ssize_t f_midi2_opts_iface_name_store(struct config_item *item,
					     const char *page, size_t len)
{
	struct f_midi2_opts *opts = to_f_midi2_opts(item);

	return f_midi2_opts_str_store(opts, &opts->info.iface_name, 128,
				      page, len);
}

CONFIGFS_ATTR(f_midi2_opts_, iface_name);

static struct configfs_attribute *f_midi2_attrs[] = {
	&f_midi2_opts_attr_process_ump,
	&f_midi2_opts_attr_static_block,
	&f_midi2_opts_attr_iface_name,
	NULL
};

static void f_midi2_opts_release(struct config_item *item)
{
	struct f_midi2_opts *opts = to_f_midi2_opts(item);

	usb_put_function_instance(&opts->func_inst);
}

static struct configfs_item_operations f_midi2_item_ops = {
	.release	= f_midi2_opts_release,
};

static struct configfs_group_operations f_midi2_group_ops = {
	.make_group	= f_midi2_opts_ep_make,
	.drop_item	= f_midi2_opts_ep_drop,
};

static const struct config_item_type f_midi2_func_type = {
	.ct_item_ops	= &f_midi2_item_ops,
	.ct_group_ops	= &f_midi2_group_ops,
	.ct_attrs	= f_midi2_attrs,
	.ct_owner	= THIS_MODULE,
};

static void f_midi2_free_inst(struct usb_function_instance *f)
{
	struct f_midi2_opts *opts;

	opts = container_of(f, struct f_midi2_opts, func_inst);

	kfree(opts->info.iface_name);
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

	/* create the default ep */
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

	/* set up the default MIDI1 (that is mandatory) */
	block_opts->info.midi1_num_groups = 1;

	config_group_init_type_name(&opts->func_inst.group, "",
				    &f_midi2_func_type);

	config_group_init_type_name(&ep_opts->group, "ep.0",
				    &f_midi2_ep_type);
	configfs_add_default_group(&ep_opts->group, &opts->func_inst.group);

	config_group_init_type_name(&block_opts->group, "block.0",
				    &f_midi2_block_type);
	configfs_add_default_group(&block_opts->group, &ep_opts->group);

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

/* verify the parameters set up via configfs;
 * return the number of EPs or a negative error
 */
static int verify_parameters(struct f_midi2_opts *opts)
{
	int i, j, num_eps, num_blks;
	struct f_midi2_ep_info *ep;
	struct f_midi2_block_info *bp;

	for (num_eps = 0; num_eps < MAX_UMP_EPS && opts->eps[num_eps];
	     num_eps++)
		;
	if (!num_eps) {
		pr_err("f_midi2: No EP is defined\n");
		return -EINVAL;
	}

	num_blks = 0;
	for (i = 0; i < num_eps; i++) {
		ep = &opts->eps[i]->info;
		if (!(ep->protocol_caps & ep->protocol)) {
			pr_err("f_midi2: Invalid protocol 0x%x (caps 0x%x) for EP %d\n",
			       ep->protocol, ep->protocol_caps, i);
			return -EINVAL;
		}

		for (j = 0; j < SNDRV_UMP_MAX_BLOCKS && opts->eps[i]->blks[j];
		     j++, num_blks++) {
			bp = &opts->eps[i]->blks[j]->info;
			if (bp->first_group + bp->num_groups > SNDRV_UMP_MAX_GROUPS) {
				pr_err("f_midi2: Invalid group definitions for block %d:%d\n",
				       i, j);
				return -EINVAL;
			}

			if (bp->midi1_num_groups) {
				if (bp->midi1_first_group < bp->first_group ||
				    bp->midi1_first_group + bp->midi1_num_groups >
				    bp->first_group + bp->num_groups) {
					pr_err("f_midi2: Invalid MIDI1 group definitions for block %d:%d\n",
					       i, j);
					return -EINVAL;
				}
			}
		}
	}
	if (!num_blks) {
		pr_err("f_midi2: No block is defined\n");
		return -EINVAL;
	}

	return num_eps;
}

/* fill mapping between MIDI 1.0 cable and UMP EP/group */
static void fill_midi1_cable_mapping(struct f_midi2 *midi2,
				     struct f_midi2_ep *ep,
				     int blk)
{
	const struct f_midi2_block_info *binfo = &ep->blks[blk].info;
	struct midi1_cable_mapping *map;
	int i, group;

	if (!binfo->midi1_num_groups)
		return;
	if (binfo->direction != SNDRV_UMP_DIR_OUTPUT) {
		group = binfo->midi1_first_group;
		map = midi2->in_cable_mapping + midi2->num_midi1_in;
		for (i = 0; i < binfo->midi1_num_groups; i++, group++, map++) {
			if (midi2->num_midi1_in >= MAX_CABLES)
				break;
			map->ep = ep;
			map->block = blk;
			map->group = group;
			midi2->num_midi1_in++;
			/* store 1-based cable number */
			ep->in_group_to_cable[group] = midi2->num_midi1_in;
		}
	}

	if (binfo->direction != SNDRV_UMP_DIR_INPUT) {
		group = binfo->midi1_first_group;
		map = midi2->out_cable_mapping + midi2->num_midi1_out;
		for (i = 0; i < binfo->midi1_num_groups; i++, group++, map++) {
			if (midi2->num_midi1_out >= MAX_CABLES)
				break;
			map->ep = ep;
			map->block = blk;
			map->group = group;
			midi2->num_midi1_out++;
		}
	}
}

/* gadget alloc callback */
static struct usb_function *f_midi2_alloc(struct usb_function_instance *fi)
{
	struct f_midi2 *midi2;
	struct f_midi2_opts *opts;
	struct f_midi2_ep *ep;
	struct f_midi2_block *bp;
	int i, num_eps, blk;

	midi2 = kzalloc(sizeof(*midi2), GFP_KERNEL);
	if (!midi2)
		return ERR_PTR(-ENOMEM);

	opts = container_of(fi, struct f_midi2_opts, func_inst);
	mutex_lock(&opts->lock);
	num_eps = verify_parameters(opts);
	if (num_eps < 0) {
		mutex_unlock(&opts->lock);
		kfree(midi2);
		return ERR_PTR(num_eps);
	}
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
	midi2->num_eps = num_eps;

	for (i = 0; i < num_eps; i++) {
		ep = &midi2->midi2_eps[i];
		ep->info = opts->eps[i]->info;
		ep->card = midi2;
		for (blk = 0; blk < SNDRV_UMP_MAX_BLOCKS &&
			     opts->eps[i]->blks[blk]; blk++) {
			bp = &ep->blks[blk];
			ep->num_blks++;
			bp->info = opts->eps[i]->blks[blk]->info;
			bp->gtb_id = ++midi2->total_blocks;
		}
	}

	midi2->string_defs = kcalloc(midi2->total_blocks + 1,
				     sizeof(*midi2->string_defs), GFP_KERNEL);
	if (!midi2->string_defs) {
		do_f_midi2_free(midi2, opts);
		return ERR_PTR(-ENOMEM);
	}

	if (opts->info.iface_name && *opts->info.iface_name)
		midi2->string_defs[STR_IFACE].s = opts->info.iface_name;
	else
		midi2->string_defs[STR_IFACE].s = ump_ep_name(&midi2->midi2_eps[0]);

	for (i = 0; i < midi2->num_eps; i++) {
		ep = &midi2->midi2_eps[i];
		for (blk = 0; blk < ep->num_blks; blk++) {
			bp = &ep->blks[blk];
			midi2->string_defs[gtb_to_str_id(bp->gtb_id)].s =
				ump_fb_name(&bp->info);

			fill_midi1_cable_mapping(midi2, ep, blk);
		}
	}

	if (!midi2->num_midi1_in && !midi2->num_midi1_out) {
		pr_err("f_midi2: MIDI1 definition is missing\n");
		do_f_midi2_free(midi2, opts);
		return ERR_PTR(-EINVAL);
	}

	return &midi2->func;
}

DECLARE_USB_FUNCTION_INIT(midi2, f_midi2_alloc_inst, f_midi2_alloc);

MODULE_DESCRIPTION("USB MIDI 2.0 class function driver");
MODULE_LICENSE("GPL");
