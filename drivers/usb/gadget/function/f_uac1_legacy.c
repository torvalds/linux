// SPDX-License-Identifier: GPL-2.0+
/*
 * f_uac1_legacy.c -- USB Audio class function driver
 *
 * Copyright (C) 2008 Bryan Wu <cooloney@kernel.org>
 * Copyright (C) 2008 Analog Devices, Inc
 * Copyright (c) 2012-2015,2020, The Linux Foundation. All rights reserved.
 * Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifdef pr_fmt
#undef pr_fmt
#endif
#define pr_fmt(fmt) "%s: " fmt, __func__

#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/atomic.h>
#include <linux/configfs.h>
#include "u_uac1_legacy.h"

static int generic_set_cmd(struct usb_audio_control *con, u8 cmd, int value);
static int generic_get_cmd(struct usb_audio_control *con, u8 cmd);

#define SPEAKER_INPUT_TERMINAL_ID	3
#define SPEAKER_OUTPUT_TERMINAL_ID	4

#define MICROPHONE_INPUT_TERMINAL_ID	1
#define MICROPHONE_OUTPUT_TERMINAL_ID	2

/*
 * DESCRIPTORS ... most are static, but strings and full
 * configuration descriptors are built on demand.
 */

/*
 * We have two interfaces- AudioControl and AudioStreaming
 */
#define F_AUDIO_AC_INTERFACE	0
#define F_AUDIO_AS_INTERFACE	1
#define F_AUDIO_NUM_INTERFACES	2

/* B.3.1  Standard AC Interface Descriptor */
static struct usb_interface_descriptor uac1_interface_desc = {
	.bLength =		USB_DT_INTERFACE_SIZE,
	.bDescriptorType =	USB_DT_INTERFACE,
	.bNumEndpoints =	0,
	.bInterfaceClass =	USB_CLASS_AUDIO,
	.bInterfaceSubClass =	USB_SUBCLASS_AUDIOCONTROL,
};

DECLARE_UAC_AC_HEADER_DESCRIPTOR(2);

#define UAC_DT_AC_HEADER_LENGTH	UAC_DT_AC_HEADER_SIZE(F_AUDIO_NUM_INTERFACES)
#define UAC_DT_TOTAL_LENGTH (           \
	UAC_DT_AC_HEADER_LENGTH       + \
	UAC_DT_INPUT_TERMINAL_SIZE    + \
	UAC_DT_OUTPUT_TERMINAL_SIZE   + \
	UAC_DT_INPUT_TERMINAL_SIZE    + \
	UAC_DT_OUTPUT_TERMINAL_SIZE     \
	)

/* B.3.2  Class-Specific AC Interface Descriptor */
static struct uac1_ac_header_descriptor_2 uac1_header_desc = {
	.bLength =		UAC_DT_AC_HEADER_LENGTH,
	.bDescriptorType =	USB_DT_CS_INTERFACE,
	.bDescriptorSubtype =	UAC_HEADER,
	.bcdADC =		cpu_to_le16(0x0100),
	.wTotalLength =		cpu_to_le16(UAC_DT_TOTAL_LENGTH),
	.bInCollection =	F_AUDIO_NUM_INTERFACES,
};

static struct uac_input_terminal_descriptor speaker_input_terminal_desc = {
	.bLength =		UAC_DT_INPUT_TERMINAL_SIZE,
	.bDescriptorType =	USB_DT_CS_INTERFACE,
	.bDescriptorSubtype =	UAC_INPUT_TERMINAL,
	.bTerminalID =		SPEAKER_INPUT_TERMINAL_ID,
	.wTerminalType =	UAC_TERMINAL_STREAMING,
	.bAssocTerminal =	SPEAKER_OUTPUT_TERMINAL_ID,
	.wChannelConfig =	0x3,
};

static struct uac1_output_terminal_descriptor speaker_output_terminal_desc = {
	.bLength		= UAC_DT_OUTPUT_TERMINAL_SIZE,
	.bDescriptorType	= USB_DT_CS_INTERFACE,
	.bDescriptorSubtype	= UAC_OUTPUT_TERMINAL,
	.bTerminalID		= SPEAKER_OUTPUT_TERMINAL_ID,
	.wTerminalType		= UAC_OUTPUT_TERMINAL_SPEAKER,
	.bAssocTerminal		= SPEAKER_INPUT_TERMINAL_ID,
	.bSourceID		= SPEAKER_INPUT_TERMINAL_ID,
};

static struct usb_audio_control speaker_mute_control = {
	.list = LIST_HEAD_INIT(speaker_mute_control.list),
	.name = "Speaker Mute Control",
	.type = UAC_FU_MUTE,
	/* Todo: add real Mute control code */
	.set = generic_set_cmd,
	.get = generic_get_cmd,
};

static struct usb_audio_control speaker_volume_control = {
	.list = LIST_HEAD_INIT(speaker_volume_control.list),
	.name = "Speaker Volume Control",
	.type = UAC_FU_VOLUME,
	/* Todo: add real Volume control code */
	.set = generic_set_cmd,
	.get = generic_get_cmd,
};

static struct usb_audio_control_selector speaker_fu_controls = {
	.list = LIST_HEAD_INIT(speaker_fu_controls.list),
	.name = "Speaker Function Unit Controls",
};

static struct uac_input_terminal_descriptor microphone_input_terminal_desc = {
	.bLength		= UAC_DT_INPUT_TERMINAL_SIZE,
	.bDescriptorType	= USB_DT_CS_INTERFACE,
	.bDescriptorSubtype	= UAC_INPUT_TERMINAL,
	.bTerminalID		= MICROPHONE_INPUT_TERMINAL_ID,
	.wTerminalType		= UAC_INPUT_TERMINAL_MICROPHONE,
	.bAssocTerminal		= MICROPHONE_OUTPUT_TERMINAL_ID,
	.bNrChannels		= 1,
	.wChannelConfig		= 0x3,
};

static struct
uac1_output_terminal_descriptor microphone_output_terminal_desc = {
	.bLength		= UAC_DT_OUTPUT_TERMINAL_SIZE,
	.bDescriptorType	= USB_DT_CS_INTERFACE,
	.bDescriptorSubtype	= UAC_OUTPUT_TERMINAL,
	.bTerminalID		= MICROPHONE_OUTPUT_TERMINAL_ID,
	.wTerminalType		= UAC_TERMINAL_STREAMING,
	.bAssocTerminal		= MICROPHONE_INPUT_TERMINAL_ID,
	.bSourceID		= MICROPHONE_INPUT_TERMINAL_ID,
};

static struct usb_audio_control microphone_mute_control = {
	.list = LIST_HEAD_INIT(microphone_mute_control.list),
	.name = "Microphone Mute Control",
	.type = UAC_FU_MUTE,
	/* Todo: add real Mute control code */
	.set = generic_set_cmd,
	.get = generic_get_cmd,
};

static struct usb_audio_control microphone_volume_control = {
	.list = LIST_HEAD_INIT(microphone_volume_control.list),
	.name = "Microphone Volume Control",
	.type = UAC_FU_VOLUME,
	/* Todo: add real Volume control code */
	.set = generic_set_cmd,
	.get = generic_get_cmd,
};

static struct usb_audio_control_selector microphone_fu_controls = {
	.list = LIST_HEAD_INIT(microphone_fu_controls.list),
	.name = "Microphone Feature Unit Controls",
};

/* B.4.1  Standard AS Interface Descriptor */
static struct usb_interface_descriptor speaker_as_interface_alt_0_desc = {
	.bLength =		USB_DT_INTERFACE_SIZE,
	.bDescriptorType =	USB_DT_INTERFACE,
	.bAlternateSetting =	0,
	.bNumEndpoints =	0,
	.bInterfaceClass =	USB_CLASS_AUDIO,
	.bInterfaceSubClass =	USB_SUBCLASS_AUDIOSTREAMING,
};

static struct usb_interface_descriptor speaker_as_interface_alt_1_desc = {
	.bLength =		USB_DT_INTERFACE_SIZE,
	.bDescriptorType =	USB_DT_INTERFACE,
	.bAlternateSetting =	1,
	.bNumEndpoints =	1,
	.bInterfaceClass =	USB_CLASS_AUDIO,
	.bInterfaceSubClass =	USB_SUBCLASS_AUDIOSTREAMING,
};

/* B.4.2  Class-Specific AS Interface Descriptor */
static struct uac1_as_header_descriptor speaker_as_header_desc = {
	.bLength =		UAC_DT_AS_HEADER_SIZE,
	.bDescriptorType =	USB_DT_CS_INTERFACE,
	.bDescriptorSubtype =	UAC_AS_GENERAL,
	.bTerminalLink =	SPEAKER_INPUT_TERMINAL_ID,
	.bDelay =		1,
	.wFormatTag =		UAC_FORMAT_TYPE_I_PCM,
};

DECLARE_UAC_FORMAT_TYPE_I_DISCRETE_DESC(1);

static struct uac_format_type_i_discrete_descriptor_1 speaker_as_type_i_desc = {
	.bLength =		UAC_FORMAT_TYPE_I_DISCRETE_DESC_SIZE(1),
	.bDescriptorType =	USB_DT_CS_INTERFACE,
	.bDescriptorSubtype =	UAC_FORMAT_TYPE,
	.bFormatType =		UAC_FORMAT_TYPE_I,
	.bSubframeSize =	2,
	.bBitResolution =	16,
	.bSamFreqType =		1,
};

/* Standard ISO OUT Endpoint Descriptor */
static struct usb_endpoint_descriptor speaker_as_ep_out_desc  = {
	.bLength =		USB_DT_ENDPOINT_AUDIO_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,
	.bEndpointAddress =	USB_DIR_OUT,
	.bmAttributes =		USB_ENDPOINT_SYNC_ADAPTIVE
				| USB_ENDPOINT_XFER_ISOC,
	.wMaxPacketSize	=	cpu_to_le16(UAC1_OUT_EP_MAX_PACKET_SIZE),
	.bInterval =		4,
};

static struct usb_ss_ep_comp_descriptor speaker_as_ep_out_comp_desc = {
	 .bLength =		 sizeof(speaker_as_ep_out_comp_desc),
	 .bDescriptorType =	 USB_DT_SS_ENDPOINT_COMP,

	 .wBytesPerInterval =	cpu_to_le16(1024),
};

/* Class-specific AS ISO OUT Endpoint Descriptor */
static struct uac_iso_endpoint_descriptor speaker_as_iso_out_desc = {
	.bLength =		UAC_ISO_ENDPOINT_DESC_SIZE,
	.bDescriptorType =	USB_DT_CS_ENDPOINT,
	.bDescriptorSubtype =	UAC_EP_GENERAL,
	.bmAttributes = 	1,
	.bLockDelayUnits =	1,
	.wLockDelay =		cpu_to_le16(1),
};

static struct usb_audio_control speaker_sample_freq_control = {
	.list = LIST_HEAD_INIT(speaker_sample_freq_control.list),
	.name = "Speaker Sampling Frequency Control",
	.type = UAC_EP_CS_ATTR_SAMPLE_RATE,
	.set  = generic_set_cmd,
	.get  = generic_get_cmd,
};

static struct usb_audio_control_selector speaker_as_iso_out = {
	.list = LIST_HEAD_INIT(speaker_as_iso_out.list),
	.name = "Speaker Iso-out Endpoint Control",
	.type = UAC_EP_GENERAL,
	.desc = (struct usb_descriptor_header *)&speaker_as_iso_out_desc,
};

/*---------------------------------*/

/* B.4.1  Standard AS Interface Descriptor */
static struct usb_interface_descriptor microphone_as_interface_alt_0_desc = {
	.bLength		= USB_DT_INTERFACE_SIZE,
	.bDescriptorType	= USB_DT_INTERFACE,
	.bAlternateSetting	= 0,
	.bNumEndpoints		= 0,
	.bInterfaceClass	= USB_CLASS_AUDIO,
	.bInterfaceSubClass	= USB_SUBCLASS_AUDIOSTREAMING,
};

static struct usb_interface_descriptor microphone_as_interface_alt_1_desc = {
	.bLength		= USB_DT_INTERFACE_SIZE,
	.bDescriptorType	= USB_DT_INTERFACE,
	.bAlternateSetting	= 1,
	.bNumEndpoints		= 1,
	.bInterfaceClass	= USB_CLASS_AUDIO,
	.bInterfaceSubClass	= USB_SUBCLASS_AUDIOSTREAMING,
};

/* B.4.2  Class-Specific AS Interface Descriptor */
static struct uac1_as_header_descriptor microphone_as_header_desc = {
	.bLength		= UAC_DT_AS_HEADER_SIZE,
	.bDescriptorType	= USB_DT_CS_INTERFACE,
	.bDescriptorSubtype	= UAC_AS_GENERAL,
	.bTerminalLink		= MICROPHONE_OUTPUT_TERMINAL_ID,
	.bDelay			= 1,
	.wFormatTag		= UAC_FORMAT_TYPE_I_PCM,
};

static struct
uac_format_type_i_discrete_descriptor_1 microphone_as_type_i_desc = {
	.bLength		= UAC_FORMAT_TYPE_I_DISCRETE_DESC_SIZE(1),
	.bDescriptorType	= USB_DT_CS_INTERFACE,
	.bDescriptorSubtype	= UAC_FORMAT_TYPE,
	.bFormatType		= UAC_FORMAT_TYPE_I,
	.bNrChannels		= 1,
	.bSubframeSize		= 2,
	.bBitResolution		= 16,
	.bSamFreqType		= 1,
};

/* Standard ISO IN Endpoint Descriptor */
static struct usb_endpoint_descriptor microphone_as_ep_in_desc = {
	.bLength		= USB_DT_ENDPOINT_AUDIO_SIZE,
	.bDescriptorType	= USB_DT_ENDPOINT,
	.bEndpointAddress	= USB_DIR_IN,
	.bmAttributes		=
		USB_ENDPOINT_XFER_ISOC | USB_ENDPOINT_SYNC_ASYNC,
	.wMaxPacketSize		= cpu_to_le16(UAC1_IN_EP_MAX_PACKET_SIZE),
	.bInterval		= 4,
};

static struct usb_ss_ep_comp_descriptor microphone_as_ep_in_comp_desc = {
	 .bLength =		 sizeof(microphone_as_ep_in_comp_desc),
	 .bDescriptorType =	 USB_DT_SS_ENDPOINT_COMP,

	 .wBytesPerInterval =	cpu_to_le16(1024),
};

 /* Class-specific AS ISO IN Endpoint Descriptor */
static struct uac_iso_endpoint_descriptor microphone_as_iso_in_desc  = {
	.bLength		= UAC_ISO_ENDPOINT_DESC_SIZE,
	.bDescriptorType	= USB_DT_CS_ENDPOINT,
	.bDescriptorSubtype	= UAC_EP_GENERAL,
	.bmAttributes		= 1,
	.bLockDelayUnits	= 1,
	.wLockDelay		= cpu_to_le16(1),
};

static struct usb_audio_control microphone_sample_freq_control = {
	.list = LIST_HEAD_INIT(microphone_sample_freq_control.list),
	.name = "Microphone Sampling Frequency Control",
	.type = UAC_EP_CS_ATTR_SAMPLE_RATE,
	.set  = generic_set_cmd,
	.get  = generic_get_cmd,
};

static struct usb_audio_control_selector microphone_as_iso_in = {
	.list = LIST_HEAD_INIT(microphone_as_iso_in.list),
	.name = "Microphone Iso-IN Endpoint Control",
	.type = UAC_EP_GENERAL,
	.desc = (struct usb_descriptor_header *)&microphone_as_iso_in_desc,
};

static struct usb_interface_assoc_descriptor
audio_iad_descriptor = {
	.bLength =		sizeof(audio_iad_descriptor),
	.bDescriptorType =	USB_DT_INTERFACE_ASSOCIATION,

	.bFirstInterface =	0, /* updated at bind */
	.bInterfaceCount =	3,
	.bFunctionClass =	USB_CLASS_AUDIO,
	.bFunctionSubClass =	0,
	.bFunctionProtocol =	UAC_VERSION_1,
};

static struct usb_descriptor_header *f_audio_desc[] = {
	(struct usb_descriptor_header *)&audio_iad_descriptor,

	(struct usb_descriptor_header *)&uac1_interface_desc,
	(struct usb_descriptor_header *)&uac1_header_desc,

	(struct usb_descriptor_header *)&microphone_input_terminal_desc,
	(struct usb_descriptor_header *)&microphone_output_terminal_desc,

	(struct usb_descriptor_header *)&speaker_input_terminal_desc,
	(struct usb_descriptor_header *)&speaker_output_terminal_desc,

	(struct usb_descriptor_header *)&microphone_as_interface_alt_0_desc,
	(struct usb_descriptor_header *)&microphone_as_interface_alt_1_desc,
	(struct usb_descriptor_header *)&microphone_as_header_desc,
	(struct usb_descriptor_header *)&microphone_as_type_i_desc,
	(struct usb_descriptor_header *)&microphone_as_ep_in_desc,
	(struct usb_descriptor_header *)&microphone_as_iso_in_desc,

	(struct usb_descriptor_header *)&speaker_as_interface_alt_0_desc,
	(struct usb_descriptor_header *)&speaker_as_interface_alt_1_desc,
	(struct usb_descriptor_header *)&speaker_as_header_desc,
	(struct usb_descriptor_header *)&speaker_as_type_i_desc,
	(struct usb_descriptor_header *)&speaker_as_ep_out_desc,
	(struct usb_descriptor_header *)&speaker_as_iso_out_desc,

	NULL,
};

static struct usb_descriptor_header *f_audio_ss_desc[]  = {
	(struct usb_descriptor_header *)&audio_iad_descriptor,

	(struct usb_descriptor_header *)&uac1_interface_desc,
	(struct usb_descriptor_header *)&uac1_header_desc,

	(struct usb_descriptor_header *)&microphone_input_terminal_desc,
	(struct usb_descriptor_header *)&microphone_output_terminal_desc,

	(struct usb_descriptor_header *)&speaker_input_terminal_desc,
	(struct usb_descriptor_header *)&speaker_output_terminal_desc,

	(struct usb_descriptor_header *)&microphone_as_interface_alt_0_desc,
	(struct usb_descriptor_header *)&microphone_as_interface_alt_1_desc,
	(struct usb_descriptor_header *)&microphone_as_header_desc,
	(struct usb_descriptor_header *)&microphone_as_type_i_desc,
	(struct usb_descriptor_header *)&microphone_as_ep_in_desc,
	(struct usb_descriptor_header *)&microphone_as_ep_in_comp_desc,
	(struct usb_descriptor_header *)&microphone_as_iso_in_desc,

	(struct usb_descriptor_header *)&speaker_as_interface_alt_0_desc,
	(struct usb_descriptor_header *)&speaker_as_interface_alt_1_desc,
	(struct usb_descriptor_header *)&speaker_as_header_desc,
	(struct usb_descriptor_header *)&speaker_as_type_i_desc,
	(struct usb_descriptor_header *)&speaker_as_ep_out_desc,
	(struct usb_descriptor_header *)&speaker_as_ep_out_comp_desc,
	(struct usb_descriptor_header *)&speaker_as_iso_out_desc,

	NULL,
};

enum {
	STR_AC_IF,
	STR_INPUT_TERMINAL,
	STR_INPUT_TERMINAL_CH_NAMES,
	STR_FEAT_DESC_0,
	STR_OUTPUT_TERMINAL,
	STR_AS_IF_ALT0,
	STR_AS_IF_ALT1,
};

static struct usb_string strings_uac1[] = {
	[STR_AC_IF].s = "AC Interface",
	[STR_INPUT_TERMINAL].s = "Input terminal",
	[STR_INPUT_TERMINAL_CH_NAMES].s = "Channels",
	[STR_FEAT_DESC_0].s = "Volume control & mute",
	[STR_OUTPUT_TERMINAL].s = "Output terminal",
	[STR_AS_IF_ALT0].s = "AS Interface",
	[STR_AS_IF_ALT1].s = "AS Interface",
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

/*-------------------------------------------------------------------------*/
struct f_audio_buf {
	u8 *buf;
	int actual;
	struct list_head list;
};

static struct f_audio_buf *f_audio_buffer_alloc(int buf_size)
{
	struct f_audio_buf *copy_buf;

	copy_buf = kzalloc(sizeof *copy_buf, GFP_ATOMIC);
	if (!copy_buf)
		return ERR_PTR(-ENOMEM);

	copy_buf->buf = kzalloc(buf_size, GFP_ATOMIC);
	if (!copy_buf->buf) {
		kfree(copy_buf);
		return ERR_PTR(-ENOMEM);
	}

	return copy_buf;
}

static void f_audio_buffer_free(struct f_audio_buf *audio_buf)
{
	if (audio_buf) {
		kfree(audio_buf->buf);
		kfree(audio_buf);
	}
}
/*-------------------------------------------------------------------------*/

struct f_audio {
	struct gaudio			card;
	atomic_t			online;
	struct mutex                    mutex;
	struct work_struct		close_work;

	/* endpoints handle full and/or high speeds */
	struct usb_ep			*out_ep;
	struct usb_ep			*in_ep;

	spinlock_t			playback_lock;
	struct f_audio_buf		*playback_copy_buf;
	struct work_struct		playback_work;
	struct list_head		play_queue;

	spinlock_t			capture_lock;
	struct f_audio_buf		*capture_copy_buf;
	struct work_struct		capture_work;
	struct list_head		capture_queue;

	u8				alt_intf[F_AUDIO_NUM_INTERFACES];

	/* Control Set command */
	struct list_head		fu_cs;
	struct list_head		ep_cs;
	u8				set_cmd;
	struct usb_audio_control	*set_con;
};

static inline struct f_audio *func_to_audio(struct usb_function *f)
{
	return container_of(f, struct f_audio, card.func);
}

/*-------------------------------------------------------------------------*/

static void f_audio_playback_work(struct work_struct *data)
{
	struct f_audio *audio = container_of(data, struct f_audio, playback_work);
	struct f_audio_buf *play_buf;
	struct f_uac1_legacy_opts *opts =
			container_of(audio->card.func.fi, struct f_uac1_legacy_opts, func_inst);
	int audio_playback_buf_size = opts->audio_playback_buf_size;
	unsigned long flags;
	int res = 0;

	pr_debug("%s: started\n", __func__);
	if (!atomic_read(&audio->online)) {
		pr_debug("%s offline\n", __func__);
		return;
	}
	/* set up ASLA audio devices if not already done */
	mutex_lock(&audio->mutex);
	res = gaudio_setup(&audio->card);
	if (res < 0) {
		mutex_unlock(&audio->mutex);
		return;
	}
	mutex_unlock(&audio->mutex);

	spin_lock_irqsave(&audio->playback_lock, flags);
	if (list_empty(&audio->play_queue)) {
		pr_err("playback_buf is empty\n");
		spin_unlock_irqrestore(&audio->playback_lock, flags);
		return;
	}
	play_buf = list_first_entry(&audio->play_queue,
			struct f_audio_buf, list);
	list_del(&play_buf->list);
	spin_unlock_irqrestore(&audio->playback_lock, flags);

	pr_debug("play_buf->actual = %d\n", play_buf->actual);

	res = u_audio_playback(&audio->card, play_buf->buf,
					audio_playback_buf_size);
	if (res)
		pr_err("copying failed\n");

	f_audio_buffer_free(play_buf);
}

static int
f_audio_playback_ep_complete(struct usb_ep *ep, struct usb_request *req)
{
	struct f_audio *audio = req->context;
	struct usb_composite_dev *cdev = audio->card.func.config->cdev;
	struct f_audio_buf *copy_buf = audio->playback_copy_buf;
	struct f_uac1_legacy_opts *opts;
	int audio_playback_buf_size;
	unsigned long flags;
	int err;

	opts = container_of(audio->card.func.fi, struct f_uac1_legacy_opts, func_inst);
	audio_playback_buf_size = opts->audio_playback_buf_size;

	if (!copy_buf)
		return -EINVAL;

	/* Copy buffer is full, add it to the play_queue */
	if (audio_playback_buf_size - copy_buf->actual < req->actual) {
		pr_debug("audio_playback_buf_size %d - copy_buf->actual %d, req->actual %d\n",
			audio_playback_buf_size, copy_buf->actual, req->actual);
		spin_lock_irqsave(&audio->playback_lock, flags);
		if (!list_empty(&audio->play_queue) &&
					opts->audio_playback_realtime) {
			pr_debug("over-runs, audio write slow.. drop the packet\n");
			f_audio_buffer_free(copy_buf);
		} else {
			list_add_tail(&copy_buf->list, &audio->play_queue);
		}
		spin_unlock_irqrestore(&audio->playback_lock, flags);
		schedule_work(&audio->playback_work);
		copy_buf = f_audio_buffer_alloc(audio_playback_buf_size);
		if (IS_ERR(copy_buf)) {
			pr_err("Failed to allocate playback_copy_buf\n");
			return -ENOMEM;
		}
	}

	pr_debug("Playback %d bytes\n", req->actual);

	memcpy(copy_buf->buf + copy_buf->actual, req->buf, req->actual);
	copy_buf->actual += req->actual;
	audio->playback_copy_buf = copy_buf;

	err = usb_ep_queue(ep, req, GFP_ATOMIC);
	if (err)
		ERROR(cdev, "%s queue req: %d\n", ep->name, err);

	return err;
}

static void f_audio_capture_work(struct work_struct *data)
{
	struct f_audio *audio =
			container_of(data, struct f_audio, capture_work);
	struct f_audio_buf *capture_buf;
	struct f_uac1_legacy_opts *opts =
			container_of(audio->card.func.fi, struct f_uac1_legacy_opts, func_inst);
	int audio_capture_buf_size = opts->audio_capture_buf_size;
	unsigned long flags;
	int res = 0;

	pr_debug("%s Started\n", __func__);
	if (!atomic_read(&audio->online)) {
		pr_debug("%s offline\n", __func__);
		return;
	}
	/* set up ASLA audio devices if not already done */
	mutex_lock(&audio->mutex);
	res = gaudio_setup(&audio->card);
	if (res < 0) {
		mutex_unlock(&audio->mutex);
		return;
	}
	mutex_unlock(&audio->mutex);

	spin_lock_irqsave(&audio->capture_lock, flags);
	if (!list_empty(&audio->capture_queue)) {
		spin_unlock_irqrestore(&audio->capture_lock, flags);
		pr_debug("%s !! buffer already filled\n", __func__);
		return;
	}
	spin_unlock_irqrestore(&audio->capture_lock, flags);

	capture_buf = f_audio_buffer_alloc(audio_capture_buf_size);
	if (capture_buf <= 0) {
		pr_err("%s: buffer alloc failed\n", __func__);
		return;
	}

	res = u_audio_capture(&audio->card, capture_buf->buf,
			audio_capture_buf_size);
	if (res)
		pr_err("copying failed\n");

	pr_debug("Queue capture packet: size %d\n", audio_capture_buf_size);
	spin_lock_irqsave(&audio->capture_lock, flags);
	list_add_tail(&capture_buf->list, &audio->capture_queue);
	spin_unlock_irqrestore(&audio->capture_lock, flags);
}

static int
f_audio_capture_ep_complete(struct usb_ep *ep, struct usb_request *req)
{
	struct f_audio *audio = req->context;
	struct f_audio_buf *copy_buf = audio->capture_copy_buf;
	struct f_uac1_legacy_opts *opts =
			container_of(audio->card.func.fi, struct f_uac1_legacy_opts, func_inst);
	int audio_capture_buf_size = opts->audio_capture_buf_size;
	unsigned long flags;
	int err = 0;

	if (copy_buf == NULL) {
		pr_debug("copy_buf == NULL\n");
		spin_lock_irqsave(&audio->capture_lock, flags);
		if (list_empty(&audio->capture_queue)) {
			spin_unlock_irqrestore(&audio->capture_lock, flags);
			pr_debug("%s no data from Audio to send\n", __func__);
			schedule_work(&audio->capture_work);
			memset(req->buf, 0, opts->req_capture_buf_size);
			goto done;
		}
		copy_buf = list_first_entry(&audio->capture_queue,
						struct f_audio_buf, list);
		list_del(&copy_buf->list);

		if (list_empty(&audio->capture_queue))
			schedule_work(&audio->capture_work);

		audio->capture_copy_buf = copy_buf;
		spin_unlock_irqrestore(&audio->capture_lock, flags);
	}

	pr_debug("Copy %d bytes\n", req->actual);
	memcpy(req->buf, copy_buf->buf + copy_buf->actual, req->actual);
	copy_buf->actual += req->actual;

	if (audio_capture_buf_size - copy_buf->actual < req->actual) {
		f_audio_buffer_free(copy_buf);
		audio->capture_copy_buf = 0;
		schedule_work(&audio->capture_work);
	}
done:
	err = usb_ep_queue(ep, req, GFP_ATOMIC);
	if (err)
		pr_err("Failed to queue %s req: err - %d\n", ep->name, err);

	return err;
}

static void f_audio_complete(struct usb_ep *ep, struct usb_request *req)
{
	struct f_audio *audio = req->context;
	int status = req->status;
	u32 data = 0;

	switch (status) {

	case 0:				/* normal completion? */
		if (ep == audio->out_ep) {
			f_audio_playback_ep_complete(ep, req);
		} else if (ep == audio->in_ep) {
			f_audio_capture_ep_complete(ep, req);
		} else if (audio->set_con) {
			memcpy(&data, req->buf, req->length);
			audio->set_con->set(audio->set_con, audio->set_cmd,
					le16_to_cpu(data));
			audio->set_con = NULL;
		}
		break;
	default:
		pr_err("Failed completion: status %d\n", status);

	  fallthrough;
	case -ECONNRESET:
	case -ESHUTDOWN:
		kfree(req->buf);
		usb_ep_free_request(ep, req);
		break;
	}
}

static int audio_set_intf_req(struct usb_function *f,
		const struct usb_ctrlrequest *ctrl)
{
	struct f_audio		*audio = func_to_audio(f);
	struct usb_composite_dev *cdev = f->config->cdev;
	struct usb_request	*req = cdev->req;
	u8			id = ((le16_to_cpu(ctrl->wIndex) >> 8) & 0xFF);
	u16			len = le16_to_cpu(ctrl->wLength);
	u16			w_value = le16_to_cpu(ctrl->wValue);
	u8			con_sel = (w_value >> 8) & 0xFF;
	u8			cmd = (ctrl->bRequest & 0x0F);
	struct usb_audio_control_selector *cs;
	struct usb_audio_control *con;

	DBG(cdev, "bRequest 0x%x, w_value 0x%04x, len %d, entity %d\n",
			ctrl->bRequest, w_value, len, id);

	list_for_each_entry(cs, &audio->fu_cs, list) {
		if (cs->id == id) {
			list_for_each_entry(con, &cs->control, list) {
				if (con->type == con_sel) {
					audio->set_con = con;
					break;
				}
			}
			break;
		}
	}

	audio->set_cmd = cmd;
	req->context = audio;
	req->complete = f_audio_complete;

	return len;
}

static int audio_get_intf_req(struct usb_function *f,
		const struct usb_ctrlrequest *ctrl)
{
	struct f_audio		*audio = func_to_audio(f);
	struct usb_composite_dev *cdev = f->config->cdev;
	struct usb_request	*req = cdev->req;
	int			value = -EOPNOTSUPP;
	u8			id = ((le16_to_cpu(ctrl->wIndex) >> 8) & 0xFF);
	u16			len = le16_to_cpu(ctrl->wLength);
	u16			w_value = le16_to_cpu(ctrl->wValue);
	u8			con_sel = (w_value >> 8) & 0xFF;
	u8			cmd = (ctrl->bRequest & 0x0F);
	struct usb_audio_control_selector *cs;
	struct usb_audio_control *con;

	DBG(cdev, "bRequest 0x%x, w_value 0x%04x, len %d, entity %d\n",
			ctrl->bRequest, w_value, len, id);

	list_for_each_entry(cs, &audio->fu_cs, list) {
		if (cs->id == id) {
			list_for_each_entry(con, &cs->control, list) {
				if (con->type == con_sel && con->get) {
					value = con->get(con, cmd);
					break;
				}
			}
			break;
		}
	}

	req->context = audio;
	req->complete = f_audio_complete;
	len = min_t(size_t, sizeof(value), len);
	memcpy(req->buf, &value, len);

	return len;
}

static void audio_set_endpoint_complete(struct usb_ep *ep,
					struct usb_request *req)
{
	struct f_audio *audio = req->context;
	u32 data = 0;

	if (req->status == 0 && audio->set_con) {
		memcpy(&data, req->buf, req->length);
		audio->set_con->set(audio->set_con, audio->set_cmd,
					le32_to_cpu(data));
		audio->set_con = NULL;
	}
}

static int audio_set_endpoint_req(struct usb_function *f,
		const struct usb_ctrlrequest *ctrl)
{
	struct usb_composite_dev *cdev = f->config->cdev;
	int			value = -EOPNOTSUPP;
	u16			ep = le16_to_cpu(ctrl->wIndex);
	u16			len = le16_to_cpu(ctrl->wLength);
	u16			w_value = le16_to_cpu(ctrl->wValue);

	struct f_audio *audio = func_to_audio(f);
	struct usb_request *req = cdev->req;
	struct usb_audio_control_selector *cs;
	struct usb_audio_control *con;

	u8	epnum   = ep & ~0x80;
	u8	con_sel = (w_value >> 8) & 0xFF;
	u8	cmd     = (ctrl->bRequest & 0x0F);

	DBG(cdev, "bRequest 0x%x, w_value 0x%04x, len %d, endp %d, epnum %d\n",
			ctrl->bRequest, w_value, len, ep, epnum);

	list_for_each_entry(cs, &audio->ep_cs, list) {
		if (cs->id != epnum)
			continue;

		list_for_each_entry(con, &cs->control, list) {
			if (con->type != con_sel)
				continue;

			switch (cmd) {
			case UAC__CUR:
			case UAC__MIN:
			case UAC__MAX:
			case UAC__RES:
				audio->set_con = con;
				audio->set_cmd = cmd;
				req->context   = audio;
				req->complete  = audio_set_endpoint_complete;
				value = len;
				break;
			case UAC__MEM:
				break;
			default:
				pr_err("Unknown command\n");
				break;
			}
			break;
		}
		break;
	}

	return value;
}

static int audio_get_endpoint_req(struct usb_function *f,
		const struct usb_ctrlrequest *ctrl)
{
	struct f_audio *audio = func_to_audio(f);
	struct usb_composite_dev *cdev = f->config->cdev;
	struct usb_request *req = cdev->req;
	struct usb_audio_control_selector *cs;
	struct usb_audio_control *con;
	int data;

	int value   = -EOPNOTSUPP;
	u8  ep      = (le16_to_cpu(ctrl->wIndex) & 0x7F);
	u8  epnum   = ep & ~0x80;
	u16 len     = le16_to_cpu(ctrl->wLength);
	u16 w_value = le16_to_cpu(ctrl->wValue);
	u8  con_sel = (w_value >> 8) & 0xFF;
	u8  cmd     = (ctrl->bRequest & 0x0F);

	DBG(cdev, "bRequest 0x%x, w_value 0x%04x, len %d, endpoint %d\n",
			ctrl->bRequest, w_value, len, ep);

	list_for_each_entry(cs, &audio->ep_cs, list) {
		if (cs->id != epnum)
			continue;

		list_for_each_entry(con, &cs->control, list) {
			if (con->type != con_sel)
				continue;

			switch (cmd) {
			case UAC__CUR:
			case UAC__MIN:
			case UAC__MAX:
			case UAC__RES:
				data = cpu_to_le32(generic_get_cmd(con, cmd));
				memcpy(req->buf, &data, len);
				value = len;
				break;
			case UAC__MEM:
				break;
			default:
				break;
			}
			break;
		}
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
	case USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE:
		pr_debug("USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE\n");
		value = audio_set_intf_req(f, ctrl);
		break;

	case USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_INTERFACE:
		pr_debug("USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_INTERFACE\n");
		value = audio_get_intf_req(f, ctrl);
		break;

	case USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_ENDPOINT:
		pr_debug("USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_ENDPOINT\n");
		value = audio_set_endpoint_req(f, ctrl);
		break;

	case USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_ENDPOINT:
		pr_debug("USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_ENDPOINT\n");
		value = audio_get_endpoint_req(f, ctrl);
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
		req->zero = 1;
		req->length = value;
		value = usb_ep_queue(cdev->gadget->ep0, req, GFP_ATOMIC);
		if (value < 0)
			ERROR(cdev, "audio response on err %d\n", value);
	}

	/* device either stalls (value < 0) or reports success */
	return value;
}

static int f_audio_get_alt(struct usb_function *f, unsigned int intf)
{
	struct f_audio	*audio = func_to_audio(f);

	if (intf == uac1_header_desc.baInterfaceNr[0])
		return audio->alt_intf[0];
	if (intf == uac1_header_desc.baInterfaceNr[1])
		return audio->alt_intf[1];

	return 0;
}

static int f_audio_set_alt(struct usb_function *f, unsigned intf, unsigned alt)
{
	struct f_audio		*audio = func_to_audio(f);
	struct usb_composite_dev *cdev = f->config->cdev;
	struct usb_ep *out_ep = audio->out_ep;
	struct usb_ep *in_ep = audio->in_ep;
	struct usb_request *req;
	unsigned long flags;
	struct f_uac1_legacy_opts *opts;
	int req_playback_buf_size, req_playback_count, audio_playback_buf_size;
	int req_capture_buf_size, req_capture_count;
	int i = 0, err = 0;

	DBG(cdev, "intf %d, alt %d\n", intf, alt);

	opts = container_of(f->fi, struct f_uac1_legacy_opts, func_inst);
	req_playback_buf_size = opts->req_playback_buf_size;
	req_capture_buf_size = opts->req_capture_buf_size;
	req_playback_count = opts->req_playback_count;
	req_capture_count = opts->req_capture_count;
	audio_playback_buf_size = opts->audio_playback_buf_size;

	atomic_set(&audio->online, 1);
	if (intf == uac1_header_desc.baInterfaceNr[0]) {
		if (audio->alt_intf[0] == alt) {
			pr_debug("Alt interface is already set to %d. Do nothing.\n",
				alt);

			return 0;
		}

		if (alt == 1) {
			err = config_ep_by_speed(cdev->gadget, f, in_ep);
			if (err)
				return err;

			err = usb_ep_enable(in_ep);
			if (err) {
				pr_err("Failed to enable capture ep\n");
				return err;
			}
			in_ep->driver_data = audio;
			audio->capture_copy_buf = 0;

			schedule_work(&audio->capture_work);
			for (i = 0; i < req_capture_count && err == 0; i++) {
				/* Allocate a write buffer */
				req = usb_ep_alloc_request(in_ep, GFP_ATOMIC);
				if (!req) {
					pr_err("request allocation failed\n");
					return -ENOMEM;
				}
				req->buf = kzalloc(req_capture_buf_size, GFP_ATOMIC);
				if (!req->buf)
					return -ENOMEM;

				req->length = req_capture_buf_size;
				req->context = audio;
				req->complete =	f_audio_complete;
				err = usb_ep_queue(in_ep, req, GFP_ATOMIC);
				if (err)
					pr_err("Failed to queue %s req,err%d\n",
						 in_ep->name, err);
			}
		} else {
			struct f_audio_buf *capture_buf;

			usb_ep_disable(in_ep);
			spin_lock_irqsave(&audio->capture_lock, flags);
			while (!list_empty(&audio->capture_queue)) {
				capture_buf =
					list_first_entry(
						&audio->capture_queue,
						 struct f_audio_buf,
						 list);
				list_del(&capture_buf->list);
				f_audio_buffer_free(capture_buf);
			}
			spin_unlock_irqrestore(&audio->capture_lock, flags);
		}
		audio->alt_intf[0] = alt;
	} else if (intf == uac1_header_desc.baInterfaceNr[1]) {
		if (audio->alt_intf[1] == alt) {
			pr_debug("Alt interface is already set to %d. Do nothing.\n",
				alt);

			return 0;
		}

		if (alt == 1) {
			err = config_ep_by_speed(cdev->gadget, f, out_ep);
			if (err)
				return err;

			err = usb_ep_enable(out_ep);
			if (err) {
				pr_err("Failed to enable playback ep\n");
				return err;
			}
			out_ep->driver_data = audio;
			audio->playback_copy_buf =
				f_audio_buffer_alloc(audio_playback_buf_size);
			if (IS_ERR(audio->playback_copy_buf)) {
				pr_err("Failed to allocate playback_copy_buf\n");
				return -ENOMEM;
			}

			/*
			 * allocate a bunch of read buffers
			 * and queue them all at once.
			 */
			for (i = 0; i < req_playback_count && err == 0; i++) {
				req = usb_ep_alloc_request(out_ep, GFP_ATOMIC);
				if (!req) {
					pr_err("request allocation failed\n");
					return -ENOMEM;
				}
				req->buf = kzalloc(req_playback_buf_size,
						GFP_ATOMIC);
				if (!req->buf)
					return -ENOMEM;

				req->length = req_playback_buf_size;
				req->context = audio;
				req->complete =	f_audio_complete;
				err = usb_ep_queue(out_ep, req, GFP_ATOMIC);
				if (err)
					pr_err("Failed to queue %s queue req: err %d\n",
						out_ep->name, err);
			}
			pr_debug("Allocated %d requests\n", req_playback_count);
		} else {
			struct f_audio_buf *playback_copy_buf =
				audio->playback_copy_buf;
			usb_ep_disable(out_ep);
			if (playback_copy_buf) {
				pr_err("Schedule playback_work\n");
				list_add_tail(&playback_copy_buf->list,
						&audio->play_queue);
				schedule_work(&audio->playback_work);
				audio->playback_copy_buf = NULL;
			} else {
				pr_err("playback_buf is empty. Stop.\n");
			}
		}
		audio->alt_intf[1] = alt;
	} else {
		pr_err("Interface %d. Do nothing. Return %d\n", intf, err);
	}

	return err;
}

static void f_audio_close_work(struct work_struct *data)
{
	struct f_audio *audio =
			container_of(data, struct f_audio, close_work);

	pr_debug("close audio files\n");
	mutex_lock(&audio->mutex);
	gaudio_cleanup(&audio->card);
	mutex_unlock(&audio->mutex);
}

static void f_audio_disable(struct usb_function *f)
{
	struct f_audio	*audio = func_to_audio(f);
	struct usb_ep	*out_ep = audio->out_ep;
	struct usb_ep	*in_ep = audio->in_ep;

	pr_debug("Disable audio\n");
	atomic_set(&audio->online, 0);
	usb_ep_disable(in_ep);
	usb_ep_disable(out_ep);

	u_audio_clear(&audio->card);
	schedule_work(&audio->close_work);

	return;
}

/*-------------------------------------------------------------------------*/

static void f_audio_build_desc(struct f_audio *audio)
{
	struct gaudio *card = &audio->card;
	u8 *sam_freq;
	int rate;

	/* Set channel numbers */
	speaker_input_terminal_desc.bNrChannels =
			u_audio_get_playback_channels(card);
	speaker_as_type_i_desc.bNrChannels =
			u_audio_get_playback_channels(card);

	microphone_input_terminal_desc.bNrChannels =
			u_audio_get_capture_channels(card);
	microphone_as_type_i_desc.bNrChannels =
			u_audio_get_capture_channels(card);

	/* Set sample rates */
	rate = u_audio_get_playback_rate(card);
	sam_freq = speaker_as_type_i_desc.tSamFreq[0];
	memcpy(sam_freq, &rate, 3);
	/* Update maxP as per sample rate, bInterval assumed as 1msec */
	speaker_as_ep_out_desc.wMaxPacketSize = (rate / 1000) * 2;

	rate = u_audio_get_capture_rate(card);
	sam_freq = microphone_as_type_i_desc.tSamFreq[0];
	memcpy(sam_freq, &rate, 3);
	/* Update maxP as per sample rate, bInterval assumed as 1msec */
	microphone_as_ep_in_desc.wMaxPacketSize = (rate / 1000) * 2;

	/* Todo: Set Sample bits and other parameters */

	return;
}

/* audio function driver setup/binding */
static int
f_audio_bind(struct usb_configuration *c, struct usb_function *f)
{
	struct usb_composite_dev *cdev = c->cdev;
	struct f_audio		*audio = func_to_audio(f);
	struct usb_string	*us;
	int			status;
	struct usb_ep		*ep = NULL;
	u8			epaddr;
	struct f_uac1_legacy_opts	*audio_opts;

	audio_opts = container_of(f->fi, struct f_uac1_legacy_opts, func_inst);
	audio->card.gadget = c->cdev->gadget;
	audio_opts->card = &audio->card;
	/* set up ASLA audio devices */
	if (!audio_opts->bound) {
		status = gaudio_setup(&audio->card);
		if (status < 0)
			goto fail;
		audio_opts->bound = true;
	}
	us = usb_gstrings_attach(cdev, uac1_strings, ARRAY_SIZE(strings_uac1));
	if (IS_ERR(us)) {
		status = PTR_ERR(us);
		goto fail;
	}
	uac1_interface_desc.iInterface = us[STR_AC_IF].id;
	speaker_input_terminal_desc.iTerminal = us[STR_INPUT_TERMINAL].id;
	speaker_input_terminal_desc.iChannelNames =
			us[STR_INPUT_TERMINAL_CH_NAMES].id;
	speaker_output_terminal_desc.iTerminal = us[STR_OUTPUT_TERMINAL].id;
	speaker_as_interface_alt_0_desc.iInterface = us[STR_AS_IF_ALT0].id;
	speaker_as_interface_alt_1_desc.iInterface = us[STR_AS_IF_ALT1].id;


	f_audio_build_desc(audio);

	/* allocate instance-specific interface IDs, and patch descriptors */
	status = usb_interface_id(c, f);
	if (status < 0) {
		pr_err("%s: failed to allocate desc interface\n", __func__);
		goto fail;
	}
	uac1_interface_desc.bInterfaceNumber = status;
	audio_iad_descriptor.bFirstInterface = status;

	status = usb_interface_id(c, f);
	if (status < 0) {
		pr_err("%s: failed to allocate alt interface\n", __func__);
		goto fail;
	}
	microphone_as_interface_alt_0_desc.bInterfaceNumber = status;
	microphone_as_interface_alt_1_desc.bInterfaceNumber = status;
	uac1_header_desc.baInterfaceNr[0] = status;
	audio->alt_intf[0] = 0;

	status = usb_interface_id(c, f);
	if (status < 0) {
		pr_err("%s: failed to allocate alt interface\n", __func__);
		goto fail;
	}
	speaker_as_interface_alt_0_desc.bInterfaceNumber = status;
	speaker_as_interface_alt_1_desc.bInterfaceNumber = status;
	uac1_header_desc.baInterfaceNr[1] = status;
	audio->alt_intf[1] = 0;

	status = -ENODEV;

	/* allocate instance-specific endpoints */
	ep = usb_ep_autoconfig(cdev->gadget, &microphone_as_ep_in_desc);
	if (!ep) {
		pr_err("%s: failed to autoconfig in endpoint\n", __func__);
		goto fail;
	}
	audio->in_ep = ep;
	ep->desc = &microphone_as_ep_in_desc;
	ep->driver_data = cdev;

	status = -ENODEV;

	ep = usb_ep_autoconfig(cdev->gadget, &speaker_as_ep_out_desc);
	if (!ep) {
		pr_err("%s: failed to autoconfig out endpoint\n", __func__);
		goto fail;
	}
	audio->out_ep = ep;
	ep->desc = &speaker_as_ep_out_desc;
	ep->driver_data = cdev;	/* claim */

	/* associate bEndpointAddress with usb_function */
	epaddr = microphone_as_ep_in_desc.bEndpointAddress & ~USB_DIR_IN;
	microphone_as_iso_in.id = epaddr;

	epaddr = speaker_as_ep_out_desc.bEndpointAddress & ~USB_DIR_IN;
	speaker_as_iso_out.id = epaddr;

	/* copy descriptors, and track endpoint copies */
	status = usb_assign_descriptors(f, f_audio_desc, f_audio_desc,
					f_audio_ss_desc, NULL);
	if (status)
		goto fail;
	return 0;

fail:
	gaudio_cleanup(&audio->card);
	if (ep)
		ep->driver_data = NULL;
	return status;
}

/*-------------------------------------------------------------------------*/

static int generic_set_cmd(struct usb_audio_control *con, u8 cmd, int value)
{
	con->data[cmd] = value;

	return 0;
}

static int generic_get_cmd(struct usb_audio_control *con, u8 cmd)
{
	return con->data[cmd];
}

/* Todo: add more control selecotor dynamically */
static int control_selector_init(struct f_audio *audio)
{
	INIT_LIST_HEAD(&audio->fu_cs);
	list_add(&microphone_fu_controls.list, &audio->fu_cs);
	list_add(&speaker_fu_controls.list, &audio->fu_cs);

	INIT_LIST_HEAD(&microphone_fu_controls.control);
	list_add(&microphone_mute_control.list,
		 &microphone_fu_controls.control);
	list_add(&microphone_volume_control.list,
		 &microphone_fu_controls.control);

	INIT_LIST_HEAD(&speaker_fu_controls.control);
	list_add(&speaker_mute_control.list,
		 &speaker_fu_controls.control);
	list_add(&speaker_volume_control.list,
		 &speaker_fu_controls.control);

	microphone_volume_control.data[UAC__CUR] = 0xffc0;
	microphone_volume_control.data[UAC__MIN] = 0xe3a0;
	microphone_volume_control.data[UAC__MAX] = 0xfff0;
	microphone_volume_control.data[UAC__RES] = 0x0030;

	speaker_volume_control.data[UAC__CUR] = 0xffc0;
	speaker_volume_control.data[UAC__MIN] = 0xe3a0;
	speaker_volume_control.data[UAC__MAX] = 0xfff0;
	speaker_volume_control.data[UAC__RES] = 0x0030;

	INIT_LIST_HEAD(&audio->ep_cs);
	list_add(&speaker_as_iso_out.list, &audio->ep_cs);
	list_add(&microphone_as_iso_in.list, &audio->ep_cs);

	INIT_LIST_HEAD(&microphone_as_iso_in.control);
	list_add(&microphone_sample_freq_control.list,
		 &microphone_as_iso_in.control);

	INIT_LIST_HEAD(&speaker_as_iso_out.control);
	list_add(&speaker_sample_freq_control.list,
		 &speaker_as_iso_out.control);

	return 0;
}

static inline struct f_uac1_legacy_opts *to_f_uac1_opts(
						struct config_item *item)
{
	return container_of(to_config_group(item), struct f_uac1_legacy_opts, func_inst.group);
}

static void f_uac1_attr_release(struct config_item *item)
{
	struct f_uac1_legacy_opts *opts = to_f_uac1_opts(item);

	usb_put_function_instance(&opts->func_inst);
}

static struct configfs_item_operations f_uac1_item_ops = {
	.release	= f_uac1_attr_release,
};

#define UAC1_INT_ATTRIBUTE(name)					\
static ssize_t f_uac1_opts_##name##_show(struct config_item *item,      \
					 char *page)                    \
{                                                                       \
	struct f_uac1_legacy_opts *opts = to_f_uac1_opts(item);         \
	int result;                                                     \
									\
	mutex_lock(&opts->lock);                                        \
	result = snprintf(page, PAGE_SIZE, "%u\n", opts->name);         \
	mutex_unlock(&opts->lock);                                      \
									\
	return result;                                                  \
}                                                                       \
									\
static ssize_t f_uac1_opts_##name##_store(struct config_item *item,     \
					  const char *page, size_t len) \
{                                                                       \
	struct f_uac1_legacy_opts *opts = to_f_uac1_opts(item);         \
	int ret;                                                        \
	u32 num;                                                        \
									\
	mutex_lock(&opts->lock);                                        \
	if (opts->refcnt) {                                             \
		ret = -EBUSY;                                           \
		goto end;                                               \
	}                                                               \
									\
	ret = kstrtou32(page, 0, &num);                                 \
	if (ret)                                                        \
		goto end;                                               \
									\
	opts->name = num;                                               \
	ret = len;                                                      \
									\
end:                                                                    \
	mutex_unlock(&opts->lock);                                      \
	return ret;                                                     \
}                                                                       \
									\
CONFIGFS_ATTR(f_uac1_opts_, name)

UAC1_INT_ATTRIBUTE(req_playback_buf_size);
UAC1_INT_ATTRIBUTE(req_capture_buf_size);
UAC1_INT_ATTRIBUTE(req_playback_count);
UAC1_INT_ATTRIBUTE(req_capture_count);
UAC1_INT_ATTRIBUTE(audio_playback_buf_size);
UAC1_INT_ATTRIBUTE(audio_capture_buf_size);
UAC1_INT_ATTRIBUTE(audio_playback_realtime);
UAC1_INT_ATTRIBUTE(sample_rate);

#define UAC1_STR_ATTRIBUTE(name)					\
static ssize_t f_uac1_opts_##name##_show(struct config_item *item,      \
					 char *page)                    \
{                                                                       \
	struct f_uac1_legacy_opts *opts = to_f_uac1_opts(item);         \
	int result;                                                     \
									\
	mutex_lock(&opts->lock);                                        \
	result = snprintf(page, PAGE_SIZE, "%s\n", opts->name);         \
	mutex_unlock(&opts->lock);                                      \
									\
	return result;                                                  \
}                                                                       \
									\
static ssize_t f_uac1_opts_##name##_store(struct config_item *item,     \
					  const char *page, size_t len) \
{                                                                       \
	struct f_uac1_legacy_opts *opts = to_f_uac1_opts(item);         \
	int ret = -EBUSY;                                               \
	char *tmp;                                                      \
									\
	mutex_lock(&opts->lock);                                        \
	if (opts->refcnt)                                               \
		goto end;                                               \
									\
	tmp = kstrndup(page, len, GFP_KERNEL);                          \
	if (tmp) {                                                      \
		ret = -ENOMEM;                                          \
		goto end;                                               \
	}                                                               \
	if (opts->name##_alloc)                                         \
		kfree(opts->name);                                      \
	opts->name##_alloc = true;                                      \
	opts->name = tmp;                                               \
	ret = len;                                                      \
									\
end:                                                                    \
	mutex_unlock(&opts->lock);                                      \
	return ret;                                                     \
}                                                                       \
									\
CONFIGFS_ATTR(f_uac1_opts_, name)

UAC1_STR_ATTRIBUTE(fn_play);
UAC1_STR_ATTRIBUTE(fn_cap);
UAC1_STR_ATTRIBUTE(fn_cntl);

static struct configfs_attribute *f_uac1_attrs[] = {
	&f_uac1_opts_attr_req_playback_buf_size,
	&f_uac1_opts_attr_req_capture_buf_size,
	&f_uac1_opts_attr_req_playback_count,
	&f_uac1_opts_attr_req_capture_count,
	&f_uac1_opts_attr_audio_playback_buf_size,
	&f_uac1_opts_attr_audio_capture_buf_size,
	&f_uac1_opts_attr_audio_playback_realtime,
	&f_uac1_opts_attr_sample_rate,
	&f_uac1_opts_attr_fn_play,
	&f_uac1_opts_attr_fn_cap,
	&f_uac1_opts_attr_fn_cntl,
	NULL,
};

static struct config_item_type f_uac1_func_type = {
	.ct_item_ops	= &f_uac1_item_ops,
	.ct_attrs	= f_uac1_attrs,
	.ct_owner	= THIS_MODULE,
};

static void f_audio_free_inst(struct usb_function_instance *f)
{
	struct f_uac1_legacy_opts *opts;

	opts = container_of(f, struct f_uac1_legacy_opts, func_inst);
	gaudio_cleanup(opts->card);
	if (opts->fn_play_alloc)
		kfree(opts->fn_play);
	if (opts->fn_cap_alloc)
		kfree(opts->fn_cap);
	if (opts->fn_cntl_alloc)
		kfree(opts->fn_cntl);
	kfree(opts);
}

static struct usb_function_instance *f_audio_alloc_inst(void)
{
	struct f_uac1_legacy_opts *opts;

	opts = kzalloc(sizeof(*opts), GFP_KERNEL);
	if (!opts)
		return ERR_PTR(-ENOMEM);

	mutex_init(&opts->lock);
	opts->func_inst.free_func_inst = f_audio_free_inst;

	config_group_init_type_name(&opts->func_inst.group, "",
				    &f_uac1_func_type);

	opts->req_playback_buf_size = UAC1_OUT_EP_MAX_PACKET_SIZE;
	opts->req_capture_buf_size = UAC1_IN_EP_MAX_PACKET_SIZE;
	opts->req_playback_count = UAC1_OUT_REQ_COUNT;
	opts->req_capture_count = UAC1_IN_REQ_COUNT;
	opts->audio_playback_buf_size = UAC1_AUDIO_PLAYBACK_BUF_SIZE;
	opts->audio_capture_buf_size = UAC1_AUDIO_CAPTURE_BUF_SIZE;
	opts->audio_playback_realtime = 1;
	opts->sample_rate = UAC1_SAMPLE_RATE;
	opts->fn_play = FILE_PCM_PLAYBACK;
	opts->fn_cap = FILE_PCM_CAPTURE;
	opts->fn_cntl = FILE_CONTROL;
	return &opts->func_inst;
}

static void f_audio_free(struct usb_function *f)
{
	struct f_audio *audio = func_to_audio(f);
	struct f_uac1_legacy_opts *opts;

	opts = container_of(f->fi, struct f_uac1_legacy_opts, func_inst);
	kfree(audio);
	mutex_lock(&opts->lock);
	--opts->refcnt;
	mutex_unlock(&opts->lock);
}

static void f_audio_unbind(struct usb_configuration *c, struct usb_function *f)
{
	struct f_audio *audio = func_to_audio(f);

	flush_work(&audio->playback_work);
	flush_work(&audio->capture_work);
	flush_work(&audio->close_work);
	gaudio_cleanup(&audio->card);
	usb_free_all_descriptors(f);
}

static struct usb_function *f_audio_alloc(struct usb_function_instance *fi)
{
	struct f_audio *audio;
	struct f_uac1_legacy_opts *opts;

	/* allocate and initialize one new instance */
	audio = kzalloc(sizeof(*audio), GFP_KERNEL);
	if (!audio)
		return ERR_PTR(-ENOMEM);

	audio->card.func.name = "g_audio";

	opts = container_of(fi, struct f_uac1_legacy_opts, func_inst);
	mutex_lock(&opts->lock);
	++opts->refcnt;
	mutex_unlock(&opts->lock);
	INIT_LIST_HEAD(&audio->play_queue);
	spin_lock_init(&audio->playback_lock);

	INIT_LIST_HEAD(&audio->capture_queue);
	spin_lock_init(&audio->capture_lock);

	audio->card.func.bind = f_audio_bind;
	audio->card.func.unbind = f_audio_unbind;
	audio->card.func.get_alt = f_audio_get_alt;
	audio->card.func.set_alt = f_audio_set_alt;
	audio->card.func.setup = f_audio_setup;
	audio->card.func.disable = f_audio_disable;
	audio->card.func.free_func = f_audio_free;

	control_selector_init(audio);

	INIT_WORK(&audio->playback_work, f_audio_playback_work);
	INIT_WORK(&audio->capture_work, f_audio_capture_work);
	INIT_WORK(&audio->close_work, f_audio_close_work);
	mutex_init(&audio->mutex);

	return &audio->card.func;
}

DECLARE_USB_FUNCTION_INIT(uac1_legacy, f_audio_alloc_inst, f_audio_alloc);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Bryan Wu");
