// SPDX-License-Identifier: GPL-2.0+
/*
 * f_uac1.c -- USB Audio Class 1.0 Function (using u_audio API)
 *
 * Copyright (C) 2016 Ruslan Bilovol <ruslan.bilovol@gmail.com>
 * Copyright (C) 2021 Julian Scheel <julian@jusst.de>
 *
 * This driver doesn't expect any real Audio codec to be present
 * on the device - the audio streams are simply sinked to and
 * sourced from a virtual ALSA sound card created.
 *
 * This file is based on f_uac1.c which is
 *   Copyright (C) 2008 Bryan Wu <cooloney@kernel.org>
 *   Copyright (C) 2008 Analog Devices, Inc
 */

#include <linux/usb/audio.h>
#include <linux/module.h>

#include "u_audio.h"
#include "u_uac1.h"

/* UAC1 spec: 3.7.2.3 Audio Channel Cluster Format */
#define UAC1_CHANNEL_MASK 0x0FFF

#define USB_OUT_FU_ID	(out_feature_unit_desc->bUnitID)
#define USB_IN_FU_ID	(in_feature_unit_desc->bUnitID)

#define EPIN_EN(_opts) ((_opts)->p_chmask != 0)
#define EPOUT_EN(_opts) ((_opts)->c_chmask != 0)
#define FUIN_EN(_opts) ((_opts)->p_mute_present \
			|| (_opts)->p_volume_present)
#define FUOUT_EN(_opts) ((_opts)->c_mute_present \
			|| (_opts)->c_volume_present)

struct f_uac1 {
	struct g_audio g_audio;
	u8 ac_intf, as_in_intf, as_out_intf;
	u8 ac_alt, as_in_alt, as_out_alt;	/* needed for get_alt() */

	struct usb_ctrlrequest setup_cr;	/* will be used in data stage */

	/* Interrupt IN endpoint of AC interface */
	struct usb_ep	*int_ep;
	atomic_t	int_count;
	int ctl_id;		/* EP id */
	int c_srate;	/* current capture srate */
	int p_srate;	/* current playback prate */
};

static inline struct f_uac1 *func_to_uac1(struct usb_function *f)
{
	return container_of(f, struct f_uac1, g_audio.func);
}

static inline struct f_uac1_opts *g_audio_to_uac1_opts(struct g_audio *audio)
{
	return container_of(audio->func.fi, struct f_uac1_opts, func_inst);
}

/*
 * DESCRIPTORS ... most are static, but strings and full
 * configuration descriptors are built on demand.
 */

/*
 * We have three interfaces - one AudioControl and two AudioStreaming
 *
 * The driver implements a simple UAC_1 topology.
 * USB-OUT -> IT_1 -> OT_2 -> ALSA_Capture
 * ALSA_Playback -> IT_3 -> OT_4 -> USB-IN
 */

/* B.3.1  Standard AC Interface Descriptor */
static struct usb_interface_descriptor ac_interface_desc = {
	.bLength =		USB_DT_INTERFACE_SIZE,
	.bDescriptorType =	USB_DT_INTERFACE,
	/* .bNumEndpoints =	DYNAMIC */
	.bInterfaceClass =	USB_CLASS_AUDIO,
	.bInterfaceSubClass =	USB_SUBCLASS_AUDIOCONTROL,
};

/* B.3.2  Class-Specific AC Interface Descriptor */
static struct uac1_ac_header_descriptor *ac_header_desc;

static struct uac_input_terminal_descriptor usb_out_it_desc = {
	.bLength =		UAC_DT_INPUT_TERMINAL_SIZE,
	.bDescriptorType =	USB_DT_CS_INTERFACE,
	.bDescriptorSubtype =	UAC_INPUT_TERMINAL,
	/* .bTerminalID =	DYNAMIC */
	.wTerminalType =	cpu_to_le16(UAC_TERMINAL_STREAMING),
	.bAssocTerminal =	0,
	.wChannelConfig =	cpu_to_le16(0x3),
};

static struct uac1_output_terminal_descriptor io_out_ot_desc = {
	.bLength		= UAC_DT_OUTPUT_TERMINAL_SIZE,
	.bDescriptorType	= USB_DT_CS_INTERFACE,
	.bDescriptorSubtype	= UAC_OUTPUT_TERMINAL,
	/* .bTerminalID =	DYNAMIC */
	.wTerminalType		= cpu_to_le16(UAC_OUTPUT_TERMINAL_SPEAKER),
	.bAssocTerminal		= 0,
	/* .bSourceID =		DYNAMIC */
};

static struct uac_input_terminal_descriptor io_in_it_desc = {
	.bLength		= UAC_DT_INPUT_TERMINAL_SIZE,
	.bDescriptorType	= USB_DT_CS_INTERFACE,
	.bDescriptorSubtype	= UAC_INPUT_TERMINAL,
	/* .bTerminalID		= DYNAMIC */
	.wTerminalType		= cpu_to_le16(UAC_INPUT_TERMINAL_MICROPHONE),
	.bAssocTerminal		= 0,
	.wChannelConfig		= cpu_to_le16(0x3),
};

static struct uac1_output_terminal_descriptor usb_in_ot_desc = {
	.bLength =		UAC_DT_OUTPUT_TERMINAL_SIZE,
	.bDescriptorType =	USB_DT_CS_INTERFACE,
	.bDescriptorSubtype =	UAC_OUTPUT_TERMINAL,
	/* .bTerminalID =	DYNAMIC */
	.wTerminalType =	cpu_to_le16(UAC_TERMINAL_STREAMING),
	.bAssocTerminal =	0,
	/* .bSourceID =		DYNAMIC */
};

static struct uac_feature_unit_descriptor *in_feature_unit_desc;
static struct uac_feature_unit_descriptor *out_feature_unit_desc;

/* AC IN Interrupt Endpoint */
static struct usb_endpoint_descriptor ac_int_ep_desc = {
	.bLength = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType = USB_DT_ENDPOINT,
	.bEndpointAddress = USB_DIR_IN,
	.bmAttributes = USB_ENDPOINT_XFER_INT,
	.wMaxPacketSize = cpu_to_le16(2),
	.bInterval = 4,
};

/* B.4.1  Standard AS Interface Descriptor */
static struct usb_interface_descriptor as_out_interface_alt_0_desc = {
	.bLength =		USB_DT_INTERFACE_SIZE,
	.bDescriptorType =	USB_DT_INTERFACE,
	.bAlternateSetting =	0,
	.bNumEndpoints =	0,
	.bInterfaceClass =	USB_CLASS_AUDIO,
	.bInterfaceSubClass =	USB_SUBCLASS_AUDIOSTREAMING,
};

static struct usb_interface_descriptor as_out_interface_alt_1_desc = {
	.bLength =		USB_DT_INTERFACE_SIZE,
	.bDescriptorType =	USB_DT_INTERFACE,
	.bAlternateSetting =	1,
	.bNumEndpoints =	1,
	.bInterfaceClass =	USB_CLASS_AUDIO,
	.bInterfaceSubClass =	USB_SUBCLASS_AUDIOSTREAMING,
};

static struct usb_interface_descriptor as_in_interface_alt_0_desc = {
	.bLength =		USB_DT_INTERFACE_SIZE,
	.bDescriptorType =	USB_DT_INTERFACE,
	.bAlternateSetting =	0,
	.bNumEndpoints =	0,
	.bInterfaceClass =	USB_CLASS_AUDIO,
	.bInterfaceSubClass =	USB_SUBCLASS_AUDIOSTREAMING,
};

static struct usb_interface_descriptor as_in_interface_alt_1_desc = {
	.bLength =		USB_DT_INTERFACE_SIZE,
	.bDescriptorType =	USB_DT_INTERFACE,
	.bAlternateSetting =	1,
	.bNumEndpoints =	1,
	.bInterfaceClass =	USB_CLASS_AUDIO,
	.bInterfaceSubClass =	USB_SUBCLASS_AUDIOSTREAMING,
};

/* B.4.2  Class-Specific AS Interface Descriptor */
static struct uac1_as_header_descriptor as_out_header_desc = {
	.bLength =		UAC_DT_AS_HEADER_SIZE,
	.bDescriptorType =	USB_DT_CS_INTERFACE,
	.bDescriptorSubtype =	UAC_AS_GENERAL,
	/* .bTerminalLink =	DYNAMIC */
	.bDelay =		1,
	.wFormatTag =		cpu_to_le16(UAC_FORMAT_TYPE_I_PCM),
};

static struct uac1_as_header_descriptor as_in_header_desc = {
	.bLength =		UAC_DT_AS_HEADER_SIZE,
	.bDescriptorType =	USB_DT_CS_INTERFACE,
	.bDescriptorSubtype =	UAC_AS_GENERAL,
	/* .bTerminalLink =	DYNAMIC */
	.bDelay =		1,
	.wFormatTag =		cpu_to_le16(UAC_FORMAT_TYPE_I_PCM),
};

DECLARE_UAC_FORMAT_TYPE_I_DISCRETE_DESC(UAC_MAX_RATES);
#define uac_format_type_i_discrete_descriptor			\
	uac_format_type_i_discrete_descriptor_##UAC_MAX_RATES

static struct uac_format_type_i_discrete_descriptor as_out_type_i_desc = {
	.bLength =		0, /* filled on rate setup */
	.bDescriptorType =	USB_DT_CS_INTERFACE,
	.bDescriptorSubtype =	UAC_FORMAT_TYPE,
	.bFormatType =		UAC_FORMAT_TYPE_I,
	.bSubframeSize =	2,
	.bBitResolution =	16,
	.bSamFreqType =		0, /* filled on rate setup */
};

/* Standard ISO OUT Endpoint Descriptor */
static struct usb_endpoint_descriptor as_out_ep_desc  = {
	.bLength =		USB_DT_ENDPOINT_AUDIO_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,
	.bEndpointAddress =	USB_DIR_OUT,
	.bmAttributes =		USB_ENDPOINT_SYNC_ADAPTIVE
				| USB_ENDPOINT_XFER_ISOC,
	.wMaxPacketSize	=	cpu_to_le16(UAC1_OUT_EP_MAX_PACKET_SIZE),
	.bInterval =		4,
};

/* Class-specific AS ISO OUT Endpoint Descriptor */
static struct uac_iso_endpoint_descriptor as_iso_out_desc = {
	.bLength =		UAC_ISO_ENDPOINT_DESC_SIZE,
	.bDescriptorType =	USB_DT_CS_ENDPOINT,
	.bDescriptorSubtype =	UAC_EP_GENERAL,
	.bmAttributes =		1,
	.bLockDelayUnits =	1,
	.wLockDelay =		cpu_to_le16(1),
};

static struct uac_format_type_i_discrete_descriptor as_in_type_i_desc = {
	.bLength =		0, /* filled on rate setup */
	.bDescriptorType =	USB_DT_CS_INTERFACE,
	.bDescriptorSubtype =	UAC_FORMAT_TYPE,
	.bFormatType =		UAC_FORMAT_TYPE_I,
	.bSubframeSize =	2,
	.bBitResolution =	16,
	.bSamFreqType =		0, /* filled on rate setup */
};

/* Standard ISO OUT Endpoint Descriptor */
static struct usb_endpoint_descriptor as_in_ep_desc  = {
	.bLength =		USB_DT_ENDPOINT_AUDIO_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,
	.bEndpointAddress =	USB_DIR_IN,
	.bmAttributes =		USB_ENDPOINT_SYNC_ASYNC
				| USB_ENDPOINT_XFER_ISOC,
	.wMaxPacketSize	=	cpu_to_le16(UAC1_OUT_EP_MAX_PACKET_SIZE),
	.bInterval =		4,
};

/* Class-specific AS ISO OUT Endpoint Descriptor */
static struct uac_iso_endpoint_descriptor as_iso_in_desc = {
	.bLength =		UAC_ISO_ENDPOINT_DESC_SIZE,
	.bDescriptorType =	USB_DT_CS_ENDPOINT,
	.bDescriptorSubtype =	UAC_EP_GENERAL,
	.bmAttributes =		1,
	.bLockDelayUnits =	0,
	.wLockDelay =		0,
};

static struct usb_descriptor_header *f_audio_desc[] = {
	(struct usb_descriptor_header *)&ac_interface_desc,
	(struct usb_descriptor_header *)&ac_header_desc,

	(struct usb_descriptor_header *)&usb_out_it_desc,
	(struct usb_descriptor_header *)&io_out_ot_desc,
	(struct usb_descriptor_header *)&out_feature_unit_desc,

	(struct usb_descriptor_header *)&io_in_it_desc,
	(struct usb_descriptor_header *)&usb_in_ot_desc,
	(struct usb_descriptor_header *)&in_feature_unit_desc,

	(struct usb_descriptor_header *)&ac_int_ep_desc,

	(struct usb_descriptor_header *)&as_out_interface_alt_0_desc,
	(struct usb_descriptor_header *)&as_out_interface_alt_1_desc,
	(struct usb_descriptor_header *)&as_out_header_desc,

	(struct usb_descriptor_header *)&as_out_type_i_desc,

	(struct usb_descriptor_header *)&as_out_ep_desc,
	(struct usb_descriptor_header *)&as_iso_out_desc,

	(struct usb_descriptor_header *)&as_in_interface_alt_0_desc,
	(struct usb_descriptor_header *)&as_in_interface_alt_1_desc,
	(struct usb_descriptor_header *)&as_in_header_desc,

	(struct usb_descriptor_header *)&as_in_type_i_desc,

	(struct usb_descriptor_header *)&as_in_ep_desc,
	(struct usb_descriptor_header *)&as_iso_in_desc,
	NULL,
};

enum {
	STR_AC_IF,
	STR_USB_OUT_IT,
	STR_USB_OUT_IT_CH_NAMES,
	STR_IO_OUT_OT,
	STR_IO_IN_IT,
	STR_IO_IN_IT_CH_NAMES,
	STR_USB_IN_OT,
	STR_FU_IN,
	STR_FU_OUT,
	STR_AS_OUT_IF_ALT0,
	STR_AS_OUT_IF_ALT1,
	STR_AS_IN_IF_ALT0,
	STR_AS_IN_IF_ALT1,
};

static struct usb_string strings_uac1[] = {
	/* [STR_AC_IF].s = DYNAMIC, */
	[STR_USB_OUT_IT].s = "Playback Input terminal",
	[STR_USB_OUT_IT_CH_NAMES].s = "Playback Channels",
	[STR_IO_OUT_OT].s = "Playback Output terminal",
	[STR_IO_IN_IT].s = "Capture Input terminal",
	[STR_IO_IN_IT_CH_NAMES].s = "Capture Channels",
	[STR_USB_IN_OT].s = "Capture Output terminal",
	[STR_FU_IN].s = "Capture Volume",
	[STR_FU_OUT].s = "Playback Volume",
	[STR_AS_OUT_IF_ALT0].s = "Playback Inactive",
	[STR_AS_OUT_IF_ALT1].s = "Playback Active",
	[STR_AS_IN_IF_ALT0].s = "Capture Inactive",
	[STR_AS_IN_IF_ALT1].s = "Capture Active",
	{ },
};

static struct usb_gadget_strings str_uac1 = {
	.language = 0x0409,	/* en-us */
	.strings = strings_uac1,
};

static struct usb_gadget_strings *uac1_strings[] = {
	&str_uac1,
	NULL,
};

/*
 * This function is an ALSA sound card following USB Audio Class Spec 1.0.
 */

static void uac_cs_attr_sample_rate(struct usb_ep *ep, struct usb_request *req)
{
	struct usb_function *fn = ep->driver_data;
	struct usb_composite_dev *cdev = fn->config->cdev;
	struct g_audio *agdev = func_to_g_audio(fn);
	struct f_uac1 *uac1 = func_to_uac1(fn);
	u8 *buf = (u8 *)req->buf;
	u32 val = 0;

	if (req->actual != 3) {
		WARN(cdev, "Invalid data size for UAC_EP_CS_ATTR_SAMPLE_RATE.\n");
		return;
	}

	val = buf[0] | (buf[1] << 8) | (buf[2] << 16);
	if (uac1->ctl_id == (USB_DIR_IN | 2)) {
		uac1->p_srate = val;
		u_audio_set_playback_srate(agdev, uac1->p_srate);
	} else if (uac1->ctl_id == (USB_DIR_OUT | 1)) {
		uac1->c_srate = val;
		u_audio_set_capture_srate(agdev, uac1->c_srate);
	}
}

static void audio_notify_complete(struct usb_ep *_ep, struct usb_request *req)
{
	struct g_audio *audio = req->context;
	struct f_uac1 *uac1 = func_to_uac1(&audio->func);

	atomic_dec(&uac1->int_count);
	kfree(req->buf);
	usb_ep_free_request(_ep, req);
}

static int audio_notify(struct g_audio *audio, int unit_id, int cs)
{
	struct f_uac1 *uac1 = func_to_uac1(&audio->func);
	struct usb_request *req;
	struct uac1_status_word *msg;
	int ret;

	if (!uac1->int_ep->enabled)
		return 0;

	if (atomic_inc_return(&uac1->int_count) > UAC1_DEF_INT_REQ_NUM) {
		atomic_dec(&uac1->int_count);
		return 0;
	}

	req = usb_ep_alloc_request(uac1->int_ep, GFP_ATOMIC);
	if (req == NULL) {
		ret = -ENOMEM;
		goto err_dec_int_count;
	}

	msg = kmalloc(sizeof(*msg), GFP_ATOMIC);
	if (msg == NULL) {
		ret = -ENOMEM;
		goto err_free_request;
	}

	msg->bStatusType = UAC1_STATUS_TYPE_IRQ_PENDING
				| UAC1_STATUS_TYPE_ORIG_AUDIO_CONTROL_IF;
	msg->bOriginator = unit_id;

	req->length = sizeof(*msg);
	req->buf = msg;
	req->context = audio;
	req->complete = audio_notify_complete;

	ret = usb_ep_queue(uac1->int_ep, req, GFP_ATOMIC);

	if (ret)
		goto err_free_msg;

	return 0;

err_free_msg:
	kfree(msg);
err_free_request:
	usb_ep_free_request(uac1->int_ep, req);
err_dec_int_count:
	atomic_dec(&uac1->int_count);

	return ret;
}

static int
in_rq_cur(struct usb_function *fn, const struct usb_ctrlrequest *cr)
{
	struct usb_request *req = fn->config->cdev->req;
	struct g_audio *audio = func_to_g_audio(fn);
	struct f_uac1_opts *opts = g_audio_to_uac1_opts(audio);
	u16 w_length = le16_to_cpu(cr->wLength);
	u16 w_index = le16_to_cpu(cr->wIndex);
	u16 w_value = le16_to_cpu(cr->wValue);
	u8 entity_id = (w_index >> 8) & 0xff;
	u8 control_selector = w_value >> 8;
	int value = -EOPNOTSUPP;

	if ((FUIN_EN(opts) && (entity_id == USB_IN_FU_ID)) ||
			(FUOUT_EN(opts) && (entity_id == USB_OUT_FU_ID))) {
		unsigned int is_playback = 0;

		if (FUIN_EN(opts) && (entity_id == USB_IN_FU_ID))
			is_playback = 1;

		if (control_selector == UAC_FU_MUTE) {
			unsigned int mute;

			u_audio_get_mute(audio, is_playback, &mute);

			*(u8 *)req->buf = mute;
			value = min_t(unsigned int, w_length, 1);
		} else if (control_selector == UAC_FU_VOLUME) {
			__le16 c;
			s16 volume;

			u_audio_get_volume(audio, is_playback, &volume);

			c = cpu_to_le16(volume);

			value = min_t(unsigned int, w_length, sizeof(c));
			memcpy(req->buf, &c, value);
		} else {
			dev_err(&audio->gadget->dev,
				"%s:%d control_selector=%d TODO!\n",
				__func__, __LINE__, control_selector);
		}
	} else {
		dev_err(&audio->gadget->dev,
			"%s:%d entity_id=%d control_selector=%d TODO!\n",
			__func__, __LINE__, entity_id, control_selector);
	}

	return value;
}

static int
in_rq_min(struct usb_function *fn, const struct usb_ctrlrequest *cr)
{
	struct usb_request *req = fn->config->cdev->req;
	struct g_audio *audio = func_to_g_audio(fn);
	struct f_uac1_opts *opts = g_audio_to_uac1_opts(audio);
	u16 w_length = le16_to_cpu(cr->wLength);
	u16 w_index = le16_to_cpu(cr->wIndex);
	u16 w_value = le16_to_cpu(cr->wValue);
	u8 entity_id = (w_index >> 8) & 0xff;
	u8 control_selector = w_value >> 8;
	int value = -EOPNOTSUPP;

	if ((FUIN_EN(opts) && (entity_id == USB_IN_FU_ID)) ||
			(FUOUT_EN(opts) && (entity_id == USB_OUT_FU_ID))) {
		unsigned int is_playback = 0;

		if (FUIN_EN(opts) && (entity_id == USB_IN_FU_ID))
			is_playback = 1;

		if (control_selector == UAC_FU_VOLUME) {
			__le16 r;
			s16 min_db;

			if (is_playback)
				min_db = opts->p_volume_min;
			else
				min_db = opts->c_volume_min;

			r = cpu_to_le16(min_db);

			value = min_t(unsigned int, w_length, sizeof(r));
			memcpy(req->buf, &r, value);
		} else {
			dev_err(&audio->gadget->dev,
				"%s:%d control_selector=%d TODO!\n",
				__func__, __LINE__, control_selector);
		}
	} else {
		dev_err(&audio->gadget->dev,
			"%s:%d entity_id=%d control_selector=%d TODO!\n",
			__func__, __LINE__, entity_id, control_selector);
	}

	return value;
}

static int
in_rq_max(struct usb_function *fn, const struct usb_ctrlrequest *cr)
{
	struct usb_request *req = fn->config->cdev->req;
	struct g_audio *audio = func_to_g_audio(fn);
	struct f_uac1_opts *opts = g_audio_to_uac1_opts(audio);
	u16 w_length = le16_to_cpu(cr->wLength);
	u16 w_index = le16_to_cpu(cr->wIndex);
	u16 w_value = le16_to_cpu(cr->wValue);
	u8 entity_id = (w_index >> 8) & 0xff;
	u8 control_selector = w_value >> 8;
	int value = -EOPNOTSUPP;

	if ((FUIN_EN(opts) && (entity_id == USB_IN_FU_ID)) ||
			(FUOUT_EN(opts) && (entity_id == USB_OUT_FU_ID))) {
		unsigned int is_playback = 0;

		if (FUIN_EN(opts) && (entity_id == USB_IN_FU_ID))
			is_playback = 1;

		if (control_selector == UAC_FU_VOLUME) {
			__le16 r;
			s16 max_db;

			if (is_playback)
				max_db = opts->p_volume_max;
			else
				max_db = opts->c_volume_max;

			r = cpu_to_le16(max_db);

			value = min_t(unsigned int, w_length, sizeof(r));
			memcpy(req->buf, &r, value);
		} else {
			dev_err(&audio->gadget->dev,
				"%s:%d control_selector=%d TODO!\n",
				__func__, __LINE__, control_selector);
		}
	} else {
		dev_err(&audio->gadget->dev,
			"%s:%d entity_id=%d control_selector=%d TODO!\n",
			__func__, __LINE__, entity_id, control_selector);
	}

	return value;
}

static int
in_rq_res(struct usb_function *fn, const struct usb_ctrlrequest *cr)
{
	struct usb_request *req = fn->config->cdev->req;
	struct g_audio *audio = func_to_g_audio(fn);
	struct f_uac1_opts *opts = g_audio_to_uac1_opts(audio);
	u16 w_length = le16_to_cpu(cr->wLength);
	u16 w_index = le16_to_cpu(cr->wIndex);
	u16 w_value = le16_to_cpu(cr->wValue);
	u8 entity_id = (w_index >> 8) & 0xff;
	u8 control_selector = w_value >> 8;
	int value = -EOPNOTSUPP;

	if ((FUIN_EN(opts) && (entity_id == USB_IN_FU_ID)) ||
			(FUOUT_EN(opts) && (entity_id == USB_OUT_FU_ID))) {
		unsigned int is_playback = 0;

		if (FUIN_EN(opts) && (entity_id == USB_IN_FU_ID))
			is_playback = 1;

		if (control_selector == UAC_FU_VOLUME) {
			__le16 r;
			s16 res_db;

			if (is_playback)
				res_db = opts->p_volume_res;
			else
				res_db = opts->c_volume_res;

			r = cpu_to_le16(res_db);

			value = min_t(unsigned int, w_length, sizeof(r));
			memcpy(req->buf, &r, value);
		} else {
			dev_err(&audio->gadget->dev,
				"%s:%d control_selector=%d TODO!\n",
				__func__, __LINE__, control_selector);
		}
	} else {
		dev_err(&audio->gadget->dev,
			"%s:%d entity_id=%d control_selector=%d TODO!\n",
			__func__, __LINE__, entity_id, control_selector);
	}

	return value;
}

static void
out_rq_cur_complete(struct usb_ep *ep, struct usb_request *req)
{
	struct g_audio *audio = req->context;
	struct usb_composite_dev *cdev = audio->func.config->cdev;
	struct f_uac1_opts *opts = g_audio_to_uac1_opts(audio);
	struct f_uac1 *uac1 = func_to_uac1(&audio->func);
	struct usb_ctrlrequest *cr = &uac1->setup_cr;
	u16 w_index = le16_to_cpu(cr->wIndex);
	u16 w_value = le16_to_cpu(cr->wValue);
	u8 entity_id = (w_index >> 8) & 0xff;
	u8 control_selector = w_value >> 8;

	if (req->status != 0) {
		dev_dbg(&cdev->gadget->dev, "completion err %d\n", req->status);
		return;
	}

	if ((FUIN_EN(opts) && (entity_id == USB_IN_FU_ID)) ||
			(FUOUT_EN(opts) && (entity_id == USB_OUT_FU_ID))) {
		unsigned int is_playback = 0;

		if (FUIN_EN(opts) && (entity_id == USB_IN_FU_ID))
			is_playback = 1;

		if (control_selector == UAC_FU_MUTE) {
			u8 mute = *(u8 *)req->buf;

			u_audio_set_mute(audio, is_playback, mute);

			return;
		} else if (control_selector == UAC_FU_VOLUME) {
			__le16 *c = req->buf;
			s16 volume;

			volume = le16_to_cpu(*c);
			u_audio_set_volume(audio, is_playback, volume);

			return;
		} else {
			dev_err(&audio->gadget->dev,
				"%s:%d control_selector=%d TODO!\n",
				__func__, __LINE__, control_selector);
			usb_ep_set_halt(ep);
		}
	} else {
		dev_err(&audio->gadget->dev,
			"%s:%d entity_id=%d control_selector=%d TODO!\n",
			__func__, __LINE__, entity_id, control_selector);
		usb_ep_set_halt(ep);

	}
}

static int
out_rq_cur(struct usb_function *fn, const struct usb_ctrlrequest *cr)
{
	struct usb_request *req = fn->config->cdev->req;
	struct g_audio *audio = func_to_g_audio(fn);
	struct f_uac1_opts *opts = g_audio_to_uac1_opts(audio);
	struct f_uac1 *uac1 = func_to_uac1(&audio->func);
	u16 w_length = le16_to_cpu(cr->wLength);
	u16 w_index = le16_to_cpu(cr->wIndex);
	u16 w_value = le16_to_cpu(cr->wValue);
	u8 entity_id = (w_index >> 8) & 0xff;
	u8 control_selector = w_value >> 8;

	if ((FUIN_EN(opts) && (entity_id == USB_IN_FU_ID)) ||
			(FUOUT_EN(opts) && (entity_id == USB_OUT_FU_ID))) {
		memcpy(&uac1->setup_cr, cr, sizeof(*cr));
		req->context = audio;
		req->complete = out_rq_cur_complete;

		return w_length;
	} else {
		dev_err(&audio->gadget->dev,
			"%s:%d entity_id=%d control_selector=%d TODO!\n",
			__func__, __LINE__, entity_id, control_selector);
	}
	return -EOPNOTSUPP;
}

static int ac_rq_in(struct usb_function *f,
		const struct usb_ctrlrequest *ctrl)
{
	struct usb_composite_dev *cdev = f->config->cdev;
	int value = -EOPNOTSUPP;
	u8 ep = ((le16_to_cpu(ctrl->wIndex) >> 8) & 0xFF);
	u16 len = le16_to_cpu(ctrl->wLength);
	u16 w_value = le16_to_cpu(ctrl->wValue);

	DBG(cdev, "bRequest 0x%x, w_value 0x%04x, len %d, endpoint %d\n",
			ctrl->bRequest, w_value, len, ep);

	switch (ctrl->bRequest) {
	case UAC_GET_CUR:
		return in_rq_cur(f, ctrl);
	case UAC_GET_MIN:
		return in_rq_min(f, ctrl);
	case UAC_GET_MAX:
		return in_rq_max(f, ctrl);
	case UAC_GET_RES:
		return in_rq_res(f, ctrl);
	case UAC_GET_MEM:
		break;
	case UAC_GET_STAT:
		value = len;
		break;
	default:
		break;
	}

	return value;
}

static int audio_set_endpoint_req(struct usb_function *f,
		const struct usb_ctrlrequest *ctrl)
{
	struct usb_composite_dev *cdev = f->config->cdev;
	struct usb_request	*req = f->config->cdev->req;
	struct f_uac1		*uac1 = func_to_uac1(f);
	int			value = -EOPNOTSUPP;
	u16			ep = le16_to_cpu(ctrl->wIndex);
	u16			len = le16_to_cpu(ctrl->wLength);
	u16			w_value = le16_to_cpu(ctrl->wValue);
	u8			cs = w_value >> 8;

	DBG(cdev, "bRequest 0x%x, w_value 0x%04x, len %d, endpoint %d\n",
			ctrl->bRequest, w_value, len, ep);

	switch (ctrl->bRequest) {
	case UAC_SET_CUR: {
		if (cs == UAC_EP_CS_ATTR_SAMPLE_RATE) {
			cdev->gadget->ep0->driver_data = f;
			uac1->ctl_id = ep;
			req->complete = uac_cs_attr_sample_rate;
		}
		value = len;
		break;
	}

	case UAC_SET_MIN:
		break;

	case UAC_SET_MAX:
		break;

	case UAC_SET_RES:
		break;

	case UAC_SET_MEM:
		break;

	default:
		break;
	}

	return value;
}

static int audio_get_endpoint_req(struct usb_function *f,
		const struct usb_ctrlrequest *ctrl)
{
	struct usb_composite_dev *cdev = f->config->cdev;
	struct usb_request *req = f->config->cdev->req;
	struct f_uac1 *uac1 = func_to_uac1(f);
	u8 *buf = (u8 *)req->buf;
	int value = -EOPNOTSUPP;
	u8 ep = le16_to_cpu(ctrl->wIndex);
	u16 len = le16_to_cpu(ctrl->wLength);
	u16 w_value = le16_to_cpu(ctrl->wValue);
	u8 cs = w_value >> 8;
	u32 val = 0;

	DBG(cdev, "bRequest 0x%x, w_value 0x%04x, len %d, endpoint %d\n",
			ctrl->bRequest, w_value, len, ep);

	switch (ctrl->bRequest) {
	case UAC_GET_CUR: {
		if (cs == UAC_EP_CS_ATTR_SAMPLE_RATE) {
			if (ep == (USB_DIR_IN | 2))
				val = uac1->p_srate;
			else if (ep == (USB_DIR_OUT | 1))
				val = uac1->c_srate;
			buf[2] = (val >> 16) & 0xff;
			buf[1] = (val >> 8) & 0xff;
			buf[0] = val & 0xff;
		}
		value = len;
		break;
	}
	case UAC_GET_MIN:
	case UAC_GET_MAX:
	case UAC_GET_RES:
		value = len;
		break;
	case UAC_GET_MEM:
		break;
	default:
		break;
	}

	return value;
}

static int
f_audio_setup(struct usb_function *f, const struct usb_ctrlrequest *ctrl)
{
	struct usb_composite_dev *cdev = f->config->cdev;
	struct usb_request	*req = cdev->req;
	int			value = -EOPNOTSUPP;
	u16			w_index = le16_to_cpu(ctrl->wIndex);
	u16			w_value = le16_to_cpu(ctrl->wValue);
	u16			w_length = le16_to_cpu(ctrl->wLength);

	/* composite driver infrastructure handles everything; interface
	 * activation uses set_alt().
	 */
	switch (ctrl->bRequestType) {
	case USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_ENDPOINT:
		value = audio_set_endpoint_req(f, ctrl);
		break;

	case USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_ENDPOINT:
		value = audio_get_endpoint_req(f, ctrl);
		break;
	case USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE:
		if (ctrl->bRequest == UAC_SET_CUR)
			value = out_rq_cur(f, ctrl);
		break;
	case USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_INTERFACE:
		value = ac_rq_in(f, ctrl);
		break;
	default:
		ERROR(cdev, "invalid control req%02x.%02x v%04x i%04x l%d\n",
			ctrl->bRequestType, ctrl->bRequest,
			w_value, w_index, w_length);
	}

	/* respond with data transfer or status phase? */
	if (value >= 0) {
		DBG(cdev, "audio req%02x.%02x v%04x i%04x l%d\n",
			ctrl->bRequestType, ctrl->bRequest,
			w_value, w_index, w_length);
		req->zero = 0;
		req->length = value;
		value = usb_ep_queue(cdev->gadget->ep0, req, GFP_ATOMIC);
		if (value < 0)
			ERROR(cdev, "audio response on err %d\n", value);
	}

	/* device either stalls (value < 0) or reports success */
	return value;
}

static int f_audio_set_alt(struct usb_function *f, unsigned intf, unsigned alt)
{
	struct usb_composite_dev *cdev = f->config->cdev;
	struct usb_gadget *gadget = cdev->gadget;
	struct device *dev = &gadget->dev;
	struct g_audio *audio = func_to_g_audio(f);
	struct f_uac1 *uac1 = func_to_uac1(f);
	int ret = 0;

	/* No i/f has more than 2 alt settings */
	if (alt > 1) {
		dev_err(dev, "%s:%d Error!\n", __func__, __LINE__);
		return -EINVAL;
	}

	if (intf == uac1->ac_intf) {
		/* Control I/f has only 1 AltSetting - 0 */
		if (alt) {
			dev_err(dev, "%s:%d Error!\n", __func__, __LINE__);
			return -EINVAL;
		}

		/* restart interrupt endpoint */
		if (uac1->int_ep) {
			usb_ep_disable(uac1->int_ep);
			config_ep_by_speed(gadget, &audio->func, uac1->int_ep);
			usb_ep_enable(uac1->int_ep);
		}

		return 0;
	}

	if (intf == uac1->as_out_intf) {
		uac1->as_out_alt = alt;

		if (alt)
			ret = u_audio_start_capture(&uac1->g_audio);
		else
			u_audio_stop_capture(&uac1->g_audio);
	} else if (intf == uac1->as_in_intf) {
		uac1->as_in_alt = alt;

		if (alt)
			ret = u_audio_start_playback(&uac1->g_audio);
		else
			u_audio_stop_playback(&uac1->g_audio);
	} else {
		dev_err(dev, "%s:%d Error!\n", __func__, __LINE__);
		return -EINVAL;
	}

	return ret;
}

static int f_audio_get_alt(struct usb_function *f, unsigned intf)
{
	struct usb_composite_dev *cdev = f->config->cdev;
	struct usb_gadget *gadget = cdev->gadget;
	struct device *dev = &gadget->dev;
	struct f_uac1 *uac1 = func_to_uac1(f);

	if (intf == uac1->ac_intf)
		return uac1->ac_alt;
	else if (intf == uac1->as_out_intf)
		return uac1->as_out_alt;
	else if (intf == uac1->as_in_intf)
		return uac1->as_in_alt;
	else
		dev_err(dev, "%s:%d Invalid Interface %d!\n",
			__func__, __LINE__, intf);

	return -EINVAL;
}


static void f_audio_disable(struct usb_function *f)
{
	struct f_uac1 *uac1 = func_to_uac1(f);

	uac1->as_out_alt = 0;
	uac1->as_in_alt = 0;

	u_audio_stop_playback(&uac1->g_audio);
	u_audio_stop_capture(&uac1->g_audio);
	if (uac1->int_ep)
		usb_ep_disable(uac1->int_ep);
}

static void
f_audio_suspend(struct usb_function *f)
{
	struct f_uac1 *uac1 = func_to_uac1(f);

	u_audio_suspend(&uac1->g_audio);
}

/*-------------------------------------------------------------------------*/
static struct uac_feature_unit_descriptor *build_fu_desc(int chmask)
{
	struct uac_feature_unit_descriptor *fu_desc;
	int channels = num_channels(chmask);
	int fu_desc_size = UAC_DT_FEATURE_UNIT_SIZE(channels);

	fu_desc = kzalloc(fu_desc_size, GFP_KERNEL);
	if (!fu_desc)
		return NULL;

	fu_desc->bLength = fu_desc_size;
	fu_desc->bDescriptorType = USB_DT_CS_INTERFACE;

	fu_desc->bDescriptorSubtype = UAC_FEATURE_UNIT;
	fu_desc->bControlSize  = 2;

	/* bUnitID, bSourceID and bmaControls will be defined later */

	return fu_desc;
}

/* B.3.2  Class-Specific AC Interface Descriptor */
static struct
uac1_ac_header_descriptor *build_ac_header_desc(struct f_uac1_opts *opts)
{
	struct uac1_ac_header_descriptor *ac_desc;
	int ac_header_desc_size;
	int num_ifaces = 0;

	if (EPOUT_EN(opts))
		num_ifaces++;
	if (EPIN_EN(opts))
		num_ifaces++;

	ac_header_desc_size = UAC_DT_AC_HEADER_SIZE(num_ifaces);

	ac_desc = kzalloc(ac_header_desc_size, GFP_KERNEL);
	if (!ac_desc)
		return NULL;

	ac_desc->bLength = ac_header_desc_size;
	ac_desc->bDescriptorType = USB_DT_CS_INTERFACE;
	ac_desc->bDescriptorSubtype = UAC_HEADER;
	ac_desc->bcdADC = cpu_to_le16(0x0100);
	ac_desc->bInCollection = num_ifaces;

	/* wTotalLength and baInterfaceNr will be defined later */

	return ac_desc;
}

/* Use macro to overcome line length limitation */
#define USBDHDR(p) (struct usb_descriptor_header *)(p)

static void setup_descriptor(struct f_uac1_opts *opts)
{
	/* patch descriptors */
	int i = 1; /* ID's start with 1 */

	if (EPOUT_EN(opts))
		usb_out_it_desc.bTerminalID = i++;
	if (EPIN_EN(opts))
		io_in_it_desc.bTerminalID = i++;
	if (EPOUT_EN(opts))
		io_out_ot_desc.bTerminalID = i++;
	if (EPIN_EN(opts))
		usb_in_ot_desc.bTerminalID = i++;
	if (FUOUT_EN(opts))
		out_feature_unit_desc->bUnitID = i++;
	if (FUIN_EN(opts))
		in_feature_unit_desc->bUnitID = i++;

	if (FUIN_EN(opts)) {
		usb_in_ot_desc.bSourceID = in_feature_unit_desc->bUnitID;
		in_feature_unit_desc->bSourceID = io_in_it_desc.bTerminalID;
	} else {
		usb_in_ot_desc.bSourceID = io_in_it_desc.bTerminalID;
	}
	if (FUOUT_EN(opts)) {
		io_out_ot_desc.bSourceID = out_feature_unit_desc->bUnitID;
		out_feature_unit_desc->bSourceID = usb_out_it_desc.bTerminalID;
	} else {
		io_out_ot_desc.bSourceID = usb_out_it_desc.bTerminalID;
	}

	as_out_header_desc.bTerminalLink = usb_out_it_desc.bTerminalID;
	as_in_header_desc.bTerminalLink = usb_in_ot_desc.bTerminalID;

	ac_header_desc->wTotalLength = cpu_to_le16(ac_header_desc->bLength);

	if (EPIN_EN(opts)) {
		u16 len = le16_to_cpu(ac_header_desc->wTotalLength);

		len += sizeof(usb_in_ot_desc);
		len += sizeof(io_in_it_desc);
		if (FUIN_EN(opts))
			len += in_feature_unit_desc->bLength;
		ac_header_desc->wTotalLength = cpu_to_le16(len);
	}
	if (EPOUT_EN(opts)) {
		u16 len = le16_to_cpu(ac_header_desc->wTotalLength);

		len += sizeof(usb_out_it_desc);
		len += sizeof(io_out_ot_desc);
		if (FUOUT_EN(opts))
			len += out_feature_unit_desc->bLength;
		ac_header_desc->wTotalLength = cpu_to_le16(len);
	}

	i = 0;
	f_audio_desc[i++] = USBDHDR(&ac_interface_desc);
	f_audio_desc[i++] = USBDHDR(ac_header_desc);

	if (EPOUT_EN(opts)) {
		f_audio_desc[i++] = USBDHDR(&usb_out_it_desc);
		f_audio_desc[i++] = USBDHDR(&io_out_ot_desc);
		if (FUOUT_EN(opts))
			f_audio_desc[i++] = USBDHDR(out_feature_unit_desc);
	}

	if (EPIN_EN(opts)) {
		f_audio_desc[i++] = USBDHDR(&io_in_it_desc);
		f_audio_desc[i++] = USBDHDR(&usb_in_ot_desc);
		if (FUIN_EN(opts))
			f_audio_desc[i++] = USBDHDR(in_feature_unit_desc);
	}

	if (FUOUT_EN(opts) || FUIN_EN(opts))
		f_audio_desc[i++] = USBDHDR(&ac_int_ep_desc);

	if (EPOUT_EN(opts)) {
		f_audio_desc[i++] = USBDHDR(&as_out_interface_alt_0_desc);
		f_audio_desc[i++] = USBDHDR(&as_out_interface_alt_1_desc);
		f_audio_desc[i++] = USBDHDR(&as_out_header_desc);
		f_audio_desc[i++] = USBDHDR(&as_out_type_i_desc);
		f_audio_desc[i++] = USBDHDR(&as_out_ep_desc);
		f_audio_desc[i++] = USBDHDR(&as_iso_out_desc);
	}
	if (EPIN_EN(opts)) {
		f_audio_desc[i++] = USBDHDR(&as_in_interface_alt_0_desc);
		f_audio_desc[i++] = USBDHDR(&as_in_interface_alt_1_desc);
		f_audio_desc[i++] = USBDHDR(&as_in_header_desc);
		f_audio_desc[i++] = USBDHDR(&as_in_type_i_desc);
		f_audio_desc[i++] = USBDHDR(&as_in_ep_desc);
		f_audio_desc[i++] = USBDHDR(&as_iso_in_desc);
	}
	f_audio_desc[i] = NULL;
}

static int f_audio_validate_opts(struct g_audio *audio, struct device *dev)
{
	struct f_uac1_opts *opts = g_audio_to_uac1_opts(audio);

	if (!opts->p_chmask && !opts->c_chmask) {
		dev_err(dev, "Error: no playback and capture channels\n");
		return -EINVAL;
	} else if (opts->p_chmask & ~UAC1_CHANNEL_MASK) {
		dev_err(dev, "Error: unsupported playback channels mask\n");
		return -EINVAL;
	} else if (opts->c_chmask & ~UAC1_CHANNEL_MASK) {
		dev_err(dev, "Error: unsupported capture channels mask\n");
		return -EINVAL;
	} else if ((opts->p_ssize < 1) || (opts->p_ssize > 4)) {
		dev_err(dev, "Error: incorrect playback sample size\n");
		return -EINVAL;
	} else if ((opts->c_ssize < 1) || (opts->c_ssize > 4)) {
		dev_err(dev, "Error: incorrect capture sample size\n");
		return -EINVAL;
	} else if (!opts->p_srates[0]) {
		dev_err(dev, "Error: incorrect playback sampling rate\n");
		return -EINVAL;
	} else if (!opts->c_srates[0]) {
		dev_err(dev, "Error: incorrect capture sampling rate\n");
		return -EINVAL;
	}

	if (opts->p_volume_max <= opts->p_volume_min) {
		dev_err(dev, "Error: incorrect playback volume max/min\n");
		return -EINVAL;
	} else if (opts->c_volume_max <= opts->c_volume_min) {
		dev_err(dev, "Error: incorrect capture volume max/min\n");
		return -EINVAL;
	} else if (opts->p_volume_res <= 0) {
		dev_err(dev, "Error: negative/zero playback volume resolution\n");
		return -EINVAL;
	} else if (opts->c_volume_res <= 0) {
		dev_err(dev, "Error: negative/zero capture volume resolution\n");
		return -EINVAL;
	}

	if ((opts->p_volume_max - opts->p_volume_min) % opts->p_volume_res) {
		dev_err(dev, "Error: incorrect playback volume resolution\n");
		return -EINVAL;
	} else if ((opts->c_volume_max - opts->c_volume_min) % opts->c_volume_res) {
		dev_err(dev, "Error: incorrect capture volume resolution\n");
		return -EINVAL;
	}

	return 0;
}

/* audio function driver setup/binding */
static int f_audio_bind(struct usb_configuration *c, struct usb_function *f)
{
	struct usb_composite_dev	*cdev = c->cdev;
	struct usb_gadget		*gadget = cdev->gadget;
	struct device			*dev = &gadget->dev;
	struct f_uac1			*uac1 = func_to_uac1(f);
	struct g_audio			*audio = func_to_g_audio(f);
	struct f_uac1_opts		*audio_opts;
	struct usb_ep			*ep = NULL;
	struct usb_string		*us;
	int				ba_iface_id;
	int				status;
	int				idx, i;

	status = f_audio_validate_opts(audio, dev);
	if (status)
		return status;

	audio_opts = container_of(f->fi, struct f_uac1_opts, func_inst);

	strings_uac1[STR_AC_IF].s = audio_opts->function_name;

	us = usb_gstrings_attach(cdev, uac1_strings, ARRAY_SIZE(strings_uac1));
	if (IS_ERR(us))
		return PTR_ERR(us);

	ac_header_desc = build_ac_header_desc(audio_opts);
	if (!ac_header_desc)
		return -ENOMEM;

	if (FUOUT_EN(audio_opts)) {
		out_feature_unit_desc = build_fu_desc(audio_opts->c_chmask);
		if (!out_feature_unit_desc) {
			status = -ENOMEM;
			goto fail;
		}
	}
	if (FUIN_EN(audio_opts)) {
		in_feature_unit_desc = build_fu_desc(audio_opts->p_chmask);
		if (!in_feature_unit_desc) {
			status = -ENOMEM;
			goto err_free_fu;
		}
	}

	ac_interface_desc.iInterface = us[STR_AC_IF].id;
	usb_out_it_desc.iTerminal = us[STR_USB_OUT_IT].id;
	usb_out_it_desc.iChannelNames = us[STR_USB_OUT_IT_CH_NAMES].id;
	io_out_ot_desc.iTerminal = us[STR_IO_OUT_OT].id;
	as_out_interface_alt_0_desc.iInterface = us[STR_AS_OUT_IF_ALT0].id;
	as_out_interface_alt_1_desc.iInterface = us[STR_AS_OUT_IF_ALT1].id;
	io_in_it_desc.iTerminal = us[STR_IO_IN_IT].id;
	io_in_it_desc.iChannelNames = us[STR_IO_IN_IT_CH_NAMES].id;
	usb_in_ot_desc.iTerminal = us[STR_USB_IN_OT].id;
	as_in_interface_alt_0_desc.iInterface = us[STR_AS_IN_IF_ALT0].id;
	as_in_interface_alt_1_desc.iInterface = us[STR_AS_IN_IF_ALT1].id;

	if (FUOUT_EN(audio_opts)) {
		u8 *i_feature;

		i_feature = (u8 *)out_feature_unit_desc +
					out_feature_unit_desc->bLength - 1;
		*i_feature = us[STR_FU_OUT].id;
	}
	if (FUIN_EN(audio_opts)) {
		u8 *i_feature;

		i_feature = (u8 *)in_feature_unit_desc +
					in_feature_unit_desc->bLength - 1;
		*i_feature = us[STR_FU_IN].id;
	}

	/* Set channel numbers */
	usb_out_it_desc.bNrChannels = num_channels(audio_opts->c_chmask);
	usb_out_it_desc.wChannelConfig = cpu_to_le16(audio_opts->c_chmask);
	as_out_type_i_desc.bNrChannels = num_channels(audio_opts->c_chmask);
	as_out_type_i_desc.bSubframeSize = audio_opts->c_ssize;
	as_out_type_i_desc.bBitResolution = audio_opts->c_ssize * 8;
	io_in_it_desc.bNrChannels = num_channels(audio_opts->p_chmask);
	io_in_it_desc.wChannelConfig = cpu_to_le16(audio_opts->p_chmask);
	as_in_type_i_desc.bNrChannels = num_channels(audio_opts->p_chmask);
	as_in_type_i_desc.bSubframeSize = audio_opts->p_ssize;
	as_in_type_i_desc.bBitResolution = audio_opts->p_ssize * 8;

	if (FUOUT_EN(audio_opts)) {
		__le16 *bma = (__le16 *)&out_feature_unit_desc->bmaControls[0];
		u32 control = 0;

		if (audio_opts->c_mute_present)
			control |= UAC_FU_MUTE;
		if (audio_opts->c_volume_present)
			control |= UAC_FU_VOLUME;
		*bma = cpu_to_le16(control);
	}
	if (FUIN_EN(audio_opts)) {
		__le16 *bma = (__le16 *)&in_feature_unit_desc->bmaControls[0];
		u32 control = 0;

		if (audio_opts->p_mute_present)
			control |= UAC_FU_MUTE;
		if (audio_opts->p_volume_present)
			control |= UAC_FU_VOLUME;
		*bma = cpu_to_le16(control);
	}

	/* Set sample rates */
	for (i = 0, idx = 0; i < UAC_MAX_RATES; i++) {
		if (audio_opts->c_srates[i] == 0)
			break;
		memcpy(as_out_type_i_desc.tSamFreq[idx++],
				&audio_opts->c_srates[i], 3);
	}
	as_out_type_i_desc.bLength = UAC_FORMAT_TYPE_I_DISCRETE_DESC_SIZE(idx);
	as_out_type_i_desc.bSamFreqType = idx;

	for (i = 0, idx = 0; i < UAC_MAX_RATES; i++) {
		if (audio_opts->p_srates[i] == 0)
			break;
		memcpy(as_in_type_i_desc.tSamFreq[idx++],
				&audio_opts->p_srates[i], 3);
	}
	as_in_type_i_desc.bLength = UAC_FORMAT_TYPE_I_DISCRETE_DESC_SIZE(idx);
	as_in_type_i_desc.bSamFreqType = idx;
	uac1->p_srate = audio_opts->p_srates[0];
	uac1->c_srate = audio_opts->c_srates[0];

	/* allocate instance-specific interface IDs, and patch descriptors */
	status = usb_interface_id(c, f);
	if (status < 0)
		goto err_free_fu;
	ac_interface_desc.bInterfaceNumber = status;
	uac1->ac_intf = status;
	uac1->ac_alt = 0;

	ba_iface_id = 0;

	if (EPOUT_EN(audio_opts)) {
		status = usb_interface_id(c, f);
		if (status < 0)
			goto err_free_fu;
		as_out_interface_alt_0_desc.bInterfaceNumber = status;
		as_out_interface_alt_1_desc.bInterfaceNumber = status;
		ac_header_desc->baInterfaceNr[ba_iface_id++] = status;
		uac1->as_out_intf = status;
		uac1->as_out_alt = 0;
	}

	if (EPIN_EN(audio_opts)) {
		status = usb_interface_id(c, f);
		if (status < 0)
			goto err_free_fu;
		as_in_interface_alt_0_desc.bInterfaceNumber = status;
		as_in_interface_alt_1_desc.bInterfaceNumber = status;
		ac_header_desc->baInterfaceNr[ba_iface_id++] = status;
		uac1->as_in_intf = status;
		uac1->as_in_alt = 0;
	}

	audio->gadget = gadget;

	status = -ENODEV;

	ac_interface_desc.bNumEndpoints = 0;

	/* allocate AC interrupt endpoint */
	if (FUOUT_EN(audio_opts) || FUIN_EN(audio_opts)) {
		ep = usb_ep_autoconfig(cdev->gadget, &ac_int_ep_desc);
		if (!ep)
			goto err_free_fu;
		uac1->int_ep = ep;
		uac1->int_ep->desc = &ac_int_ep_desc;

		ac_interface_desc.bNumEndpoints = 1;
	}

	/* allocate instance-specific endpoints */
	if (EPOUT_EN(audio_opts)) {
		ep = usb_ep_autoconfig(cdev->gadget, &as_out_ep_desc);
		if (!ep)
			goto err_free_fu;
		audio->out_ep = ep;
		audio->out_ep->desc = &as_out_ep_desc;
	}

	if (EPIN_EN(audio_opts)) {
		ep = usb_ep_autoconfig(cdev->gadget, &as_in_ep_desc);
		if (!ep)
			goto err_free_fu;
		audio->in_ep = ep;
		audio->in_ep->desc = &as_in_ep_desc;
	}

	setup_descriptor(audio_opts);

	/* copy descriptors, and track endpoint copies */
	status = usb_assign_descriptors(f, f_audio_desc, f_audio_desc, NULL,
					NULL);
	if (status)
		goto err_free_fu;

	audio->out_ep_maxpsize = le16_to_cpu(as_out_ep_desc.wMaxPacketSize);
	audio->in_ep_maxpsize = le16_to_cpu(as_in_ep_desc.wMaxPacketSize);
	audio->params.c_chmask = audio_opts->c_chmask;
	memcpy(audio->params.c_srates, audio_opts->c_srates,
			sizeof(audio->params.c_srates));
	audio->params.c_ssize = audio_opts->c_ssize;
	if (FUIN_EN(audio_opts)) {
		audio->params.p_fu.id = USB_IN_FU_ID;
		audio->params.p_fu.mute_present = audio_opts->p_mute_present;
		audio->params.p_fu.volume_present =
				audio_opts->p_volume_present;
		audio->params.p_fu.volume_min = audio_opts->p_volume_min;
		audio->params.p_fu.volume_max = audio_opts->p_volume_max;
		audio->params.p_fu.volume_res = audio_opts->p_volume_res;
	}
	audio->params.p_chmask = audio_opts->p_chmask;
	memcpy(audio->params.p_srates, audio_opts->p_srates,
			sizeof(audio->params.p_srates));
	audio->params.p_ssize = audio_opts->p_ssize;
	if (FUOUT_EN(audio_opts)) {
		audio->params.c_fu.id = USB_OUT_FU_ID;
		audio->params.c_fu.mute_present = audio_opts->c_mute_present;
		audio->params.c_fu.volume_present =
				audio_opts->c_volume_present;
		audio->params.c_fu.volume_min = audio_opts->c_volume_min;
		audio->params.c_fu.volume_max = audio_opts->c_volume_max;
		audio->params.c_fu.volume_res = audio_opts->c_volume_res;
	}
	audio->params.req_number = audio_opts->req_number;
	audio->params.fb_max = FBACK_FAST_MAX;
	if (FUOUT_EN(audio_opts) || FUIN_EN(audio_opts))
		audio->notify = audio_notify;

	status = g_audio_setup(audio, "UAC1_PCM", "UAC1_Gadget");
	if (status)
		goto err_card_register;

	return 0;

err_card_register:
	usb_free_all_descriptors(f);
err_free_fu:
	kfree(out_feature_unit_desc);
	out_feature_unit_desc = NULL;
	kfree(in_feature_unit_desc);
	in_feature_unit_desc = NULL;
fail:
	kfree(ac_header_desc);
	ac_header_desc = NULL;
	return status;
}

/*-------------------------------------------------------------------------*/

static inline struct f_uac1_opts *to_f_uac1_opts(struct config_item *item)
{
	return container_of(to_config_group(item), struct f_uac1_opts,
			    func_inst.group);
}

static void f_uac1_attr_release(struct config_item *item)
{
	struct f_uac1_opts *opts = to_f_uac1_opts(item);

	usb_put_function_instance(&opts->func_inst);
}

static struct configfs_item_operations f_uac1_item_ops = {
	.release	= f_uac1_attr_release,
};

#define uac1_kstrtou32			kstrtou32
#define uac1_kstrtos16			kstrtos16
#define uac1_kstrtobool(s, base, res)	kstrtobool((s), (res))

static const char *u32_fmt = "%u\n";
static const char *s16_fmt = "%hd\n";
static const char *bool_fmt = "%u\n";

#define UAC1_ATTRIBUTE(type, name)					\
static ssize_t f_uac1_opts_##name##_show(				\
					  struct config_item *item,	\
					  char *page)			\
{									\
	struct f_uac1_opts *opts = to_f_uac1_opts(item);		\
	int result;							\
									\
	mutex_lock(&opts->lock);					\
	result = sprintf(page, type##_fmt, opts->name);			\
	mutex_unlock(&opts->lock);					\
									\
	return result;							\
}									\
									\
static ssize_t f_uac1_opts_##name##_store(				\
					  struct config_item *item,	\
					  const char *page, size_t len)	\
{									\
	struct f_uac1_opts *opts = to_f_uac1_opts(item);		\
	int ret;							\
	type num;							\
									\
	mutex_lock(&opts->lock);					\
	if (opts->refcnt) {						\
		ret = -EBUSY;						\
		goto end;						\
	}								\
									\
	ret = uac1_kstrto##type(page, 0, &num);				\
	if (ret)							\
		goto end;						\
									\
	opts->name = num;						\
	ret = len;							\
									\
end:									\
	mutex_unlock(&opts->lock);					\
	return ret;							\
}									\
									\
CONFIGFS_ATTR(f_uac1_opts_, name)

#define UAC1_RATE_ATTRIBUTE(name)					\
static ssize_t f_uac1_opts_##name##_show(struct config_item *item,	\
					 char *page)			\
{									\
	struct f_uac1_opts *opts = to_f_uac1_opts(item);		\
	int result = 0;							\
	int i;								\
									\
	mutex_lock(&opts->lock);					\
	page[0] = '\0';							\
	for (i = 0; i < UAC_MAX_RATES; i++) {				\
		if (opts->name##s[i] == 0)				\
			break;						\
		result += sprintf(page + strlen(page), "%u,",		\
				opts->name##s[i]);			\
	}								\
	if (strlen(page) > 0)						\
		page[strlen(page) - 1] = '\n';				\
	mutex_unlock(&opts->lock);					\
									\
	return result;							\
}									\
									\
static ssize_t f_uac1_opts_##name##_store(struct config_item *item,	\
					  const char *page, size_t len)	\
{									\
	struct f_uac1_opts *opts = to_f_uac1_opts(item);		\
	char *split_page = NULL;					\
	int ret = -EINVAL;						\
	char *token;							\
	u32 num;							\
	int i;								\
									\
	mutex_lock(&opts->lock);					\
	if (opts->refcnt) {						\
		ret = -EBUSY;						\
		goto end;						\
	}								\
									\
	i = 0;								\
	memset(opts->name##s, 0x00, sizeof(opts->name##s));		\
	split_page = kstrdup(page, GFP_KERNEL);				\
	while ((token = strsep(&split_page, ",")) != NULL) {		\
		ret = kstrtou32(token, 0, &num);			\
		if (ret)						\
			goto end;					\
									\
		opts->name##s[i++] = num;				\
		ret = len;						\
	};								\
									\
end:									\
	kfree(split_page);						\
	mutex_unlock(&opts->lock);					\
	return ret;							\
}									\
									\
CONFIGFS_ATTR(f_uac1_opts_, name)

#define UAC1_ATTRIBUTE_STRING(name)					\
static ssize_t f_uac1_opts_##name##_show(struct config_item *item,	\
					 char *page)			\
{									\
	struct f_uac1_opts *opts = to_f_uac1_opts(item);		\
	int result;							\
									\
	mutex_lock(&opts->lock);					\
	result = snprintf(page, sizeof(opts->name), "%s", opts->name);	\
	mutex_unlock(&opts->lock);					\
									\
	return result;							\
}									\
									\
static ssize_t f_uac1_opts_##name##_store(struct config_item *item,	\
					  const char *page, size_t len)	\
{									\
	struct f_uac1_opts *opts = to_f_uac1_opts(item);		\
	int ret = 0;							\
									\
	mutex_lock(&opts->lock);					\
	if (opts->refcnt) {						\
		ret = -EBUSY;						\
		goto end;						\
	}								\
									\
	ret = snprintf(opts->name, min(sizeof(opts->name), len),	\
			"%s", page);					\
									\
end:									\
	mutex_unlock(&opts->lock);					\
	return ret;							\
}									\
									\
CONFIGFS_ATTR(f_uac1_opts_, name)

UAC1_ATTRIBUTE(u32, c_chmask);
UAC1_RATE_ATTRIBUTE(c_srate);
UAC1_ATTRIBUTE(u32, c_ssize);
UAC1_ATTRIBUTE(u32, p_chmask);
UAC1_RATE_ATTRIBUTE(p_srate);
UAC1_ATTRIBUTE(u32, p_ssize);
UAC1_ATTRIBUTE(u32, req_number);

UAC1_ATTRIBUTE(bool, p_mute_present);
UAC1_ATTRIBUTE(bool, p_volume_present);
UAC1_ATTRIBUTE(s16, p_volume_min);
UAC1_ATTRIBUTE(s16, p_volume_max);
UAC1_ATTRIBUTE(s16, p_volume_res);

UAC1_ATTRIBUTE(bool, c_mute_present);
UAC1_ATTRIBUTE(bool, c_volume_present);
UAC1_ATTRIBUTE(s16, c_volume_min);
UAC1_ATTRIBUTE(s16, c_volume_max);
UAC1_ATTRIBUTE(s16, c_volume_res);
UAC1_ATTRIBUTE_STRING(function_name);

static struct configfs_attribute *f_uac1_attrs[] = {
	&f_uac1_opts_attr_c_chmask,
	&f_uac1_opts_attr_c_srate,
	&f_uac1_opts_attr_c_ssize,
	&f_uac1_opts_attr_p_chmask,
	&f_uac1_opts_attr_p_srate,
	&f_uac1_opts_attr_p_ssize,
	&f_uac1_opts_attr_req_number,

	&f_uac1_opts_attr_p_mute_present,
	&f_uac1_opts_attr_p_volume_present,
	&f_uac1_opts_attr_p_volume_min,
	&f_uac1_opts_attr_p_volume_max,
	&f_uac1_opts_attr_p_volume_res,

	&f_uac1_opts_attr_c_mute_present,
	&f_uac1_opts_attr_c_volume_present,
	&f_uac1_opts_attr_c_volume_min,
	&f_uac1_opts_attr_c_volume_max,
	&f_uac1_opts_attr_c_volume_res,

	&f_uac1_opts_attr_function_name,

	NULL,
};

static const struct config_item_type f_uac1_func_type = {
	.ct_item_ops	= &f_uac1_item_ops,
	.ct_attrs	= f_uac1_attrs,
	.ct_owner	= THIS_MODULE,
};

static void f_audio_free_inst(struct usb_function_instance *f)
{
	struct f_uac1_opts *opts;

	opts = container_of(f, struct f_uac1_opts, func_inst);
	kfree(opts);
}

static struct usb_function_instance *f_audio_alloc_inst(void)
{
	struct f_uac1_opts *opts;

	opts = kzalloc(sizeof(*opts), GFP_KERNEL);
	if (!opts)
		return ERR_PTR(-ENOMEM);

	mutex_init(&opts->lock);
	opts->func_inst.free_func_inst = f_audio_free_inst;

	config_group_init_type_name(&opts->func_inst.group, "",
				    &f_uac1_func_type);

	opts->c_chmask = UAC1_DEF_CCHMASK;
	opts->c_srates[0] = UAC1_DEF_CSRATE;
	opts->c_ssize = UAC1_DEF_CSSIZE;
	opts->p_chmask = UAC1_DEF_PCHMASK;
	opts->p_srates[0] = UAC1_DEF_PSRATE;
	opts->p_ssize = UAC1_DEF_PSSIZE;

	opts->p_mute_present = UAC1_DEF_MUTE_PRESENT;
	opts->p_volume_present = UAC1_DEF_VOLUME_PRESENT;
	opts->p_volume_min = UAC1_DEF_MIN_DB;
	opts->p_volume_max = UAC1_DEF_MAX_DB;
	opts->p_volume_res = UAC1_DEF_RES_DB;

	opts->c_mute_present = UAC1_DEF_MUTE_PRESENT;
	opts->c_volume_present = UAC1_DEF_VOLUME_PRESENT;
	opts->c_volume_min = UAC1_DEF_MIN_DB;
	opts->c_volume_max = UAC1_DEF_MAX_DB;
	opts->c_volume_res = UAC1_DEF_RES_DB;

	opts->req_number = UAC1_DEF_REQ_NUM;

	snprintf(opts->function_name, sizeof(opts->function_name), "AC Interface");

	return &opts->func_inst;
}

static void f_audio_free(struct usb_function *f)
{
	struct g_audio *audio;
	struct f_uac1_opts *opts;

	audio = func_to_g_audio(f);
	opts = container_of(f->fi, struct f_uac1_opts, func_inst);
	kfree(audio);
	mutex_lock(&opts->lock);
	--opts->refcnt;
	mutex_unlock(&opts->lock);
}

static void f_audio_unbind(struct usb_configuration *c, struct usb_function *f)
{
	struct g_audio *audio = func_to_g_audio(f);

	g_audio_cleanup(audio);
	usb_free_all_descriptors(f);

	kfree(out_feature_unit_desc);
	out_feature_unit_desc = NULL;
	kfree(in_feature_unit_desc);
	in_feature_unit_desc = NULL;

	kfree(ac_header_desc);
	ac_header_desc = NULL;

	audio->gadget = NULL;
}

static struct usb_function *f_audio_alloc(struct usb_function_instance *fi)
{
	struct f_uac1 *uac1;
	struct f_uac1_opts *opts;

	/* allocate and initialize one new instance */
	uac1 = kzalloc(sizeof(*uac1), GFP_KERNEL);
	if (!uac1)
		return ERR_PTR(-ENOMEM);

	opts = container_of(fi, struct f_uac1_opts, func_inst);
	mutex_lock(&opts->lock);
	++opts->refcnt;
	mutex_unlock(&opts->lock);

	uac1->g_audio.func.name = "uac1_func";
	uac1->g_audio.func.bind = f_audio_bind;
	uac1->g_audio.func.unbind = f_audio_unbind;
	uac1->g_audio.func.set_alt = f_audio_set_alt;
	uac1->g_audio.func.get_alt = f_audio_get_alt;
	uac1->g_audio.func.setup = f_audio_setup;
	uac1->g_audio.func.disable = f_audio_disable;
	uac1->g_audio.func.suspend = f_audio_suspend;
	uac1->g_audio.func.free_func = f_audio_free;

	return &uac1->g_audio.func;
}

DECLARE_USB_FUNCTION_INIT(uac1, f_audio_alloc_inst, f_audio_alloc);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Ruslan Bilovol");
