// SPDX-License-Identifier: GPL-2.0+
/*
 * f_uac2.c -- USB Audio Class 2.0 Function
 *
 * Copyright (C) 2011
 *    Yadwinder Singh (yadi.brar01@gmail.com)
 *    Jaswinder Singh (jaswinder.singh@linaro.org)
 * Copyright (C) 2017 Julian Scheel <julian@jusst.de>
 */

#include <linux/usb/audio.h>
#include <linux/usb/audio-v2.h>
#include <linux/module.h>

#include "u_audio.h"
#include "u_uac.h"

/*
 * The driver implements a simple UAC_2 topology.
 * USB-OUT -> IT_1 -> OT_3 -> ALSA_Capture
 * ALSA_Playback -> IT_2 -> OT_4 -> USB-IN
 * Capture and Playback sampling rates are independently
 *  controlled by two clock sources :
 *    CLK_5 := c_srate, and CLK_6 := p_srate
 */
#define USB_OUT_CLK_ID	(out_clk_src_desc.bClockID)
#define USB_IN_CLK_ID	(in_clk_src_desc.bClockID)

#define CONTROL_ABSENT	0
#define CONTROL_RDONLY	1
#define CONTROL_RDWR	3

#define CLK_FREQ_CTRL	0
#define CLK_VLD_CTRL	2

#define COPY_CTRL	0
#define CONN_CTRL	2
#define OVRLD_CTRL	4
#define CLSTR_CTRL	6
#define UNFLW_CTRL	8
#define OVFLW_CTRL	10

#define EPIN_EN(_opts) ((_opts)->p_chmask != 0)
#define EPOUT_EN(_opts) ((_opts)->c_chmask != 0)

/* --------- USB Function Interface ------------- */

enum {
	STR_ASSOC,
	STR_IF_CTRL,
	STR_CLKSRC_IN,
	STR_CLKSRC_OUT,
	STR_USB_IT,
	STR_IO_IT,
	STR_USB_OT,
	STR_IO_OT,
	STR_AS_OUT_ALT0,
	STR_AS_OUT_ALT1,
	STR_AS_IN_ALT0,
	STR_AS_IN_ALT1,
};

static struct usb_string strings_fn[] = {
	[STR_ASSOC].s = "Source/Sink",
	[STR_IF_CTRL].s = "Topology Control",
	[STR_CLKSRC_IN].s = "Input clock",
	[STR_CLKSRC_OUT].s = "Output clock",
	[STR_USB_IT].s = "USBH Out",
	[STR_IO_IT].s = "USBD Out",
	[STR_USB_OT].s = "USBH In",
	[STR_IO_OT].s = "USBD In",
	[STR_AS_OUT_ALT0].s = "Playback Inactive",
	[STR_AS_OUT_ALT1].s = "Playback Active",
	[STR_AS_IN_ALT0].s = "Capture Inactive",
	[STR_AS_IN_ALT1].s = "Capture Active",
	{ },
};

static struct usb_gadget_strings str_fn = {
	.language = 0x0409,	/* en-us */
	.strings = strings_fn,
};

static struct usb_gadget_strings *fn_strings[] = {
	&str_fn,
	NULL,
};

static struct usb_interface_assoc_descriptor iad_desc = {
	.bLength = sizeof iad_desc,
	.bDescriptorType = USB_DT_INTERFACE_ASSOCIATION,

	.bFirstInterface = 0,
	.bInterfaceCount = 3,
	.bFunctionClass = USB_CLASS_AUDIO,
	.bFunctionSubClass = UAC2_FUNCTION_SUBCLASS_UNDEFINED,
	.bFunctionProtocol = UAC_VERSION_2,
};

/* Audio Control Interface */
static struct usb_interface_descriptor std_ac_if_desc = {
	.bLength = sizeof std_ac_if_desc,
	.bDescriptorType = USB_DT_INTERFACE,

	.bAlternateSetting = 0,
	.bNumEndpoints = 0,
	.bInterfaceClass = USB_CLASS_AUDIO,
	.bInterfaceSubClass = USB_SUBCLASS_AUDIOCONTROL,
	.bInterfaceProtocol = UAC_VERSION_2,
};

/* Clock source for IN traffic */
static struct uac_clock_source_descriptor in_clk_src_desc = {
	.bLength = sizeof in_clk_src_desc,
	.bDescriptorType = USB_DT_CS_INTERFACE,

	.bDescriptorSubtype = UAC2_CLOCK_SOURCE,
	/* .bClockID = DYNAMIC */
	.bmAttributes = UAC_CLOCK_SOURCE_TYPE_INT_FIXED,
	.bmControls = (CONTROL_RDWR << CLK_FREQ_CTRL),
	.bAssocTerminal = 0,
};

/* Clock source for OUT traffic */
static struct uac_clock_source_descriptor out_clk_src_desc = {
	.bLength = sizeof out_clk_src_desc,
	.bDescriptorType = USB_DT_CS_INTERFACE,

	.bDescriptorSubtype = UAC2_CLOCK_SOURCE,
	/* .bClockID = DYNAMIC */
	.bmAttributes = UAC_CLOCK_SOURCE_TYPE_INT_FIXED,
	.bmControls = (CONTROL_RDWR << CLK_FREQ_CTRL),
	.bAssocTerminal = 0,
};

/* Input Terminal for USB_OUT */
static struct uac2_input_terminal_descriptor usb_out_it_desc = {
	.bLength = sizeof usb_out_it_desc,
	.bDescriptorType = USB_DT_CS_INTERFACE,

	.bDescriptorSubtype = UAC_INPUT_TERMINAL,
	/* .bTerminalID = DYNAMIC */
	.wTerminalType = cpu_to_le16(UAC_TERMINAL_STREAMING),
	.bAssocTerminal = 0,
	/* .bCSourceID = DYNAMIC */
	.iChannelNames = 0,
	.bmControls = cpu_to_le16(CONTROL_RDWR << COPY_CTRL),
};

/* Input Terminal for I/O-In */
static struct uac2_input_terminal_descriptor io_in_it_desc = {
	.bLength = sizeof io_in_it_desc,
	.bDescriptorType = USB_DT_CS_INTERFACE,

	.bDescriptorSubtype = UAC_INPUT_TERMINAL,
	/* .bTerminalID = DYNAMIC */
	.wTerminalType = cpu_to_le16(UAC_INPUT_TERMINAL_MICROPHONE),
	.bAssocTerminal = 0,
	/* .bCSourceID = DYNAMIC */
	.iChannelNames = 0,
	.bmControls = cpu_to_le16(CONTROL_RDWR << COPY_CTRL),
};

/* Ouput Terminal for USB_IN */
static struct uac2_output_terminal_descriptor usb_in_ot_desc = {
	.bLength = sizeof usb_in_ot_desc,
	.bDescriptorType = USB_DT_CS_INTERFACE,

	.bDescriptorSubtype = UAC_OUTPUT_TERMINAL,
	/* .bTerminalID = DYNAMIC */
	.wTerminalType = cpu_to_le16(UAC_TERMINAL_STREAMING),
	.bAssocTerminal = 0,
	/* .bSourceID = DYNAMIC */
	/* .bCSourceID = DYNAMIC */
	.bmControls = cpu_to_le16(CONTROL_RDWR << COPY_CTRL),
};

/* Ouput Terminal for I/O-Out */
static struct uac2_output_terminal_descriptor io_out_ot_desc = {
	.bLength = sizeof io_out_ot_desc,
	.bDescriptorType = USB_DT_CS_INTERFACE,

	.bDescriptorSubtype = UAC_OUTPUT_TERMINAL,
	/* .bTerminalID = DYNAMIC */
	.wTerminalType = cpu_to_le16(UAC_OUTPUT_TERMINAL_SPEAKER),
	.bAssocTerminal = 0,
	/* .bSourceID = DYNAMIC */
	/* .bCSourceID = DYNAMIC */
	.bmControls = cpu_to_le16(CONTROL_RDWR << COPY_CTRL),
};

static struct uac2_ac_header_descriptor ac_hdr_desc = {
	.bLength = sizeof ac_hdr_desc,
	.bDescriptorType = USB_DT_CS_INTERFACE,

	.bDescriptorSubtype = UAC_HEADER,
	.bcdADC = cpu_to_le16(0x200),
	.bCategory = UAC2_FUNCTION_IO_BOX,
	.wTotalLength = cpu_to_le16(sizeof ac_hdr_desc + sizeof in_clk_src_desc
			+ sizeof out_clk_src_desc + sizeof usb_out_it_desc
			+ sizeof io_in_it_desc + sizeof usb_in_ot_desc
			+ sizeof io_out_ot_desc),
	.bmControls = 0,
};

/* Audio Streaming OUT Interface - Alt0 */
static struct usb_interface_descriptor std_as_out_if0_desc = {
	.bLength = sizeof std_as_out_if0_desc,
	.bDescriptorType = USB_DT_INTERFACE,

	.bAlternateSetting = 0,
	.bNumEndpoints = 0,
	.bInterfaceClass = USB_CLASS_AUDIO,
	.bInterfaceSubClass = USB_SUBCLASS_AUDIOSTREAMING,
	.bInterfaceProtocol = UAC_VERSION_2,
};

/* Audio Streaming OUT Interface - Alt1 */
static struct usb_interface_descriptor std_as_out_if1_desc = {
	.bLength = sizeof std_as_out_if1_desc,
	.bDescriptorType = USB_DT_INTERFACE,

	.bAlternateSetting = 1,
	.bNumEndpoints = 1,
	.bInterfaceClass = USB_CLASS_AUDIO,
	.bInterfaceSubClass = USB_SUBCLASS_AUDIOSTREAMING,
	.bInterfaceProtocol = UAC_VERSION_2,
};

/* Audio Stream OUT Intface Desc */
static struct uac2_as_header_descriptor as_out_hdr_desc = {
	.bLength = sizeof as_out_hdr_desc,
	.bDescriptorType = USB_DT_CS_INTERFACE,

	.bDescriptorSubtype = UAC_AS_GENERAL,
	/* .bTerminalLink = DYNAMIC */
	.bmControls = 0,
	.bFormatType = UAC_FORMAT_TYPE_I,
	.bmFormats = cpu_to_le32(UAC_FORMAT_TYPE_I_PCM),
	.iChannelNames = 0,
};

/* Audio USB_OUT Format */
static struct uac2_format_type_i_descriptor as_out_fmt1_desc = {
	.bLength = sizeof as_out_fmt1_desc,
	.bDescriptorType = USB_DT_CS_INTERFACE,
	.bDescriptorSubtype = UAC_FORMAT_TYPE,
	.bFormatType = UAC_FORMAT_TYPE_I,
};

/* STD AS ISO OUT Endpoint */
static struct usb_endpoint_descriptor fs_epout_desc = {
	.bLength = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType = USB_DT_ENDPOINT,

	.bEndpointAddress = USB_DIR_OUT,
	.bmAttributes = USB_ENDPOINT_XFER_ISOC | USB_ENDPOINT_SYNC_ADAPTIVE,
	.wMaxPacketSize = cpu_to_le16(1023),
	.bInterval = 1,
};

static struct usb_endpoint_descriptor hs_epout_desc = {
	.bLength = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType = USB_DT_ENDPOINT,

	.bmAttributes = USB_ENDPOINT_XFER_ISOC | USB_ENDPOINT_SYNC_ADAPTIVE,
	.wMaxPacketSize = cpu_to_le16(1024),
	.bInterval = 4,
};

/* CS AS ISO OUT Endpoint */
static struct uac2_iso_endpoint_descriptor as_iso_out_desc = {
	.bLength = sizeof as_iso_out_desc,
	.bDescriptorType = USB_DT_CS_ENDPOINT,

	.bDescriptorSubtype = UAC_EP_GENERAL,
	.bmAttributes = 0,
	.bmControls = 0,
	.bLockDelayUnits = 0,
	.wLockDelay = 0,
};

/* Audio Streaming IN Interface - Alt0 */
static struct usb_interface_descriptor std_as_in_if0_desc = {
	.bLength = sizeof std_as_in_if0_desc,
	.bDescriptorType = USB_DT_INTERFACE,

	.bAlternateSetting = 0,
	.bNumEndpoints = 0,
	.bInterfaceClass = USB_CLASS_AUDIO,
	.bInterfaceSubClass = USB_SUBCLASS_AUDIOSTREAMING,
	.bInterfaceProtocol = UAC_VERSION_2,
};

/* Audio Streaming IN Interface - Alt1 */
static struct usb_interface_descriptor std_as_in_if1_desc = {
	.bLength = sizeof std_as_in_if1_desc,
	.bDescriptorType = USB_DT_INTERFACE,

	.bAlternateSetting = 1,
	.bNumEndpoints = 1,
	.bInterfaceClass = USB_CLASS_AUDIO,
	.bInterfaceSubClass = USB_SUBCLASS_AUDIOSTREAMING,
	.bInterfaceProtocol = UAC_VERSION_2,
};

/* Audio Stream IN Intface Desc */
static struct uac2_as_header_descriptor as_in_hdr_desc = {
	.bLength = sizeof as_in_hdr_desc,
	.bDescriptorType = USB_DT_CS_INTERFACE,

	.bDescriptorSubtype = UAC_AS_GENERAL,
	/* .bTerminalLink = DYNAMIC */
	.bmControls = 0,
	.bFormatType = UAC_FORMAT_TYPE_I,
	.bmFormats = cpu_to_le32(UAC_FORMAT_TYPE_I_PCM),
	.iChannelNames = 0,
};

/* Audio USB_IN Format */
static struct uac2_format_type_i_descriptor as_in_fmt1_desc = {
	.bLength = sizeof as_in_fmt1_desc,
	.bDescriptorType = USB_DT_CS_INTERFACE,
	.bDescriptorSubtype = UAC_FORMAT_TYPE,
	.bFormatType = UAC_FORMAT_TYPE_I,
};

/* STD AS ISO IN Endpoint */
static struct usb_endpoint_descriptor fs_epin_desc = {
	.bLength = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType = USB_DT_ENDPOINT,

	.bEndpointAddress = USB_DIR_IN,
	.bmAttributes = USB_ENDPOINT_XFER_ISOC | USB_ENDPOINT_SYNC_SYNC,
	.wMaxPacketSize = cpu_to_le16(1023),
	.bInterval = 1,
};

static struct usb_endpoint_descriptor hs_epin_desc = {
	.bLength = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType = USB_DT_ENDPOINT,

	.bmAttributes = USB_ENDPOINT_XFER_ISOC | USB_ENDPOINT_SYNC_SYNC,
	.wMaxPacketSize = cpu_to_le16(1024),
	.bInterval = 4,
};

/* CS AS ISO IN Endpoint */
static struct uac2_iso_endpoint_descriptor as_iso_in_desc = {
	.bLength = sizeof as_iso_in_desc,
	.bDescriptorType = USB_DT_CS_ENDPOINT,

	.bDescriptorSubtype = UAC_EP_GENERAL,
	.bmAttributes = 0,
	.bmControls = 0,
	.bLockDelayUnits = 0,
	.wLockDelay = 0,
};

static struct usb_descriptor_header *fs_audio_desc[] = {
	(struct usb_descriptor_header *)&iad_desc,
	(struct usb_descriptor_header *)&std_ac_if_desc,

	(struct usb_descriptor_header *)&ac_hdr_desc,
	(struct usb_descriptor_header *)&in_clk_src_desc,
	(struct usb_descriptor_header *)&out_clk_src_desc,
	(struct usb_descriptor_header *)&usb_out_it_desc,
	(struct usb_descriptor_header *)&io_in_it_desc,
	(struct usb_descriptor_header *)&usb_in_ot_desc,
	(struct usb_descriptor_header *)&io_out_ot_desc,

	(struct usb_descriptor_header *)&std_as_out_if0_desc,
	(struct usb_descriptor_header *)&std_as_out_if1_desc,

	(struct usb_descriptor_header *)&as_out_hdr_desc,
	(struct usb_descriptor_header *)&as_out_fmt1_desc,
	(struct usb_descriptor_header *)&fs_epout_desc,
	(struct usb_descriptor_header *)&as_iso_out_desc,

	(struct usb_descriptor_header *)&std_as_in_if0_desc,
	(struct usb_descriptor_header *)&std_as_in_if1_desc,

	(struct usb_descriptor_header *)&as_in_hdr_desc,
	(struct usb_descriptor_header *)&as_in_fmt1_desc,
	(struct usb_descriptor_header *)&fs_epin_desc,
	(struct usb_descriptor_header *)&as_iso_in_desc,
	NULL,
};

static struct usb_descriptor_header *hs_audio_desc[] = {
	(struct usb_descriptor_header *)&iad_desc,
	(struct usb_descriptor_header *)&std_ac_if_desc,

	(struct usb_descriptor_header *)&ac_hdr_desc,
	(struct usb_descriptor_header *)&in_clk_src_desc,
	(struct usb_descriptor_header *)&out_clk_src_desc,
	(struct usb_descriptor_header *)&usb_out_it_desc,
	(struct usb_descriptor_header *)&io_in_it_desc,
	(struct usb_descriptor_header *)&usb_in_ot_desc,
	(struct usb_descriptor_header *)&io_out_ot_desc,

	(struct usb_descriptor_header *)&std_as_out_if0_desc,
	(struct usb_descriptor_header *)&std_as_out_if1_desc,

	(struct usb_descriptor_header *)&as_out_hdr_desc,
	(struct usb_descriptor_header *)&as_out_fmt1_desc,
	(struct usb_descriptor_header *)&hs_epout_desc,
	(struct usb_descriptor_header *)&as_iso_out_desc,

	(struct usb_descriptor_header *)&std_as_in_if0_desc,
	(struct usb_descriptor_header *)&std_as_in_if1_desc,

	(struct usb_descriptor_header *)&as_in_hdr_desc,
	(struct usb_descriptor_header *)&as_in_fmt1_desc,
	(struct usb_descriptor_header *)&hs_epin_desc,
	(struct usb_descriptor_header *)&as_iso_in_desc,
	NULL,
};

struct cntrl_cur_lay3 {
	__le32	dCUR;
};

struct cntrl_range_lay3 {
	__le32	dMIN;
	__le32	dMAX;
	__le32	dRES;
} __packed;

#define ranges_size(c) (sizeof(c.wNumSubRanges) + c.wNumSubRanges \
		* sizeof(struct cntrl_ranges_lay3))
struct cntrl_ranges_lay3 {
	__u16	wNumSubRanges;
	struct cntrl_range_lay3 r[UAC_MAX_RATES];
} __packed;

static void set_ep_max_packet_size(const struct f_uac_opts *uac2_opts,
	struct usb_endpoint_descriptor *ep_desc,
	unsigned int factor, bool is_playback)
{
	int chmask, srate = 0, ssize;
	u16 max_packet_size;
	int i;

	if (is_playback) {
		chmask = uac2_opts->p_chmask;
		for (i = 0; i < UAC_MAX_RATES; i++) {
			if (uac2_opts->p_srate[i] == 0)
				break;
			if (uac2_opts->p_srate[i] > srate)
				srate = uac2_opts->p_srate[i];
		}
		ssize = uac2_opts->p_ssize;
	} else {
		chmask = uac2_opts->c_chmask;
		for (i = 0; i < UAC_MAX_RATES; i++) {
			if (uac2_opts->c_srate[i] == 0)
				break;
			if (uac2_opts->c_srate[i] > srate)
				srate = uac2_opts->c_srate[i];
		}
		ssize = uac2_opts->c_ssize;
	}

	max_packet_size = num_channels(chmask) * ssize *
		DIV_ROUND_UP(srate, factor / (1 << (ep_desc->bInterval - 1)));
	ep_desc->wMaxPacketSize = cpu_to_le16(min_t(u16, max_packet_size,
				le16_to_cpu(ep_desc->wMaxPacketSize)));
}

/* Use macro to overcome line length limitation */
#define USBDHDR(p) (struct usb_descriptor_header *)(p)

static void setup_descriptor(struct f_uac_opts *opts)
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
	if (EPOUT_EN(opts))
		out_clk_src_desc.bClockID = i++;
	if (EPIN_EN(opts))
		in_clk_src_desc.bClockID = i++;

	usb_out_it_desc.bCSourceID = out_clk_src_desc.bClockID;
	usb_in_ot_desc.bSourceID = io_in_it_desc.bTerminalID;
	usb_in_ot_desc.bCSourceID = in_clk_src_desc.bClockID;
	io_in_it_desc.bCSourceID = in_clk_src_desc.bClockID;
	io_out_ot_desc.bCSourceID = out_clk_src_desc.bClockID;
	io_out_ot_desc.bSourceID = usb_out_it_desc.bTerminalID;
	as_out_hdr_desc.bTerminalLink = usb_out_it_desc.bTerminalID;
	as_in_hdr_desc.bTerminalLink = usb_in_ot_desc.bTerminalID;

	iad_desc.bInterfaceCount = 1;
	ac_hdr_desc.wTotalLength = sizeof ac_hdr_desc;

	if (EPIN_EN(opts)) {
		u16 len = le16_to_cpu(ac_hdr_desc.wTotalLength);

		len += sizeof(in_clk_src_desc);
		len += sizeof(usb_in_ot_desc);
		len += sizeof(io_in_it_desc);
		ac_hdr_desc.wTotalLength = cpu_to_le16(len);
		iad_desc.bInterfaceCount++;
	}
	if (EPOUT_EN(opts)) {
		u16 len = le16_to_cpu(ac_hdr_desc.wTotalLength);

		len += sizeof(out_clk_src_desc);
		len += sizeof(usb_out_it_desc);
		len += sizeof(io_out_ot_desc);
		ac_hdr_desc.wTotalLength = cpu_to_le16(len);
		iad_desc.bInterfaceCount++;
	}

	i = 0;
	fs_audio_desc[i++] = USBDHDR(&iad_desc);
	fs_audio_desc[i++] = USBDHDR(&std_ac_if_desc);
	fs_audio_desc[i++] = USBDHDR(&ac_hdr_desc);
	if (EPIN_EN(opts))
		fs_audio_desc[i++] = USBDHDR(&in_clk_src_desc);
	if (EPOUT_EN(opts)) {
		fs_audio_desc[i++] = USBDHDR(&out_clk_src_desc);
		fs_audio_desc[i++] = USBDHDR(&usb_out_it_desc);
	}
	if (EPIN_EN(opts)) {
		fs_audio_desc[i++] = USBDHDR(&io_in_it_desc);
		fs_audio_desc[i++] = USBDHDR(&usb_in_ot_desc);
	}
	if (EPOUT_EN(opts)) {
		fs_audio_desc[i++] = USBDHDR(&io_out_ot_desc);
		fs_audio_desc[i++] = USBDHDR(&std_as_out_if0_desc);
		fs_audio_desc[i++] = USBDHDR(&std_as_out_if1_desc);
		fs_audio_desc[i++] = USBDHDR(&as_out_hdr_desc);
		fs_audio_desc[i++] = USBDHDR(&as_out_fmt1_desc);
		fs_audio_desc[i++] = USBDHDR(&fs_epout_desc);
		fs_audio_desc[i++] = USBDHDR(&as_iso_out_desc);
	}
	if (EPIN_EN(opts)) {
		fs_audio_desc[i++] = USBDHDR(&std_as_in_if0_desc);
		fs_audio_desc[i++] = USBDHDR(&std_as_in_if1_desc);
		fs_audio_desc[i++] = USBDHDR(&as_in_hdr_desc);
		fs_audio_desc[i++] = USBDHDR(&as_in_fmt1_desc);
		fs_audio_desc[i++] = USBDHDR(&fs_epin_desc);
		fs_audio_desc[i++] = USBDHDR(&as_iso_in_desc);
	}
	fs_audio_desc[i] = NULL;

	i = 0;
	hs_audio_desc[i++] = USBDHDR(&iad_desc);
	hs_audio_desc[i++] = USBDHDR(&std_ac_if_desc);
	hs_audio_desc[i++] = USBDHDR(&ac_hdr_desc);
	if (EPIN_EN(opts))
		hs_audio_desc[i++] = USBDHDR(&in_clk_src_desc);
	if (EPOUT_EN(opts)) {
		hs_audio_desc[i++] = USBDHDR(&out_clk_src_desc);
		hs_audio_desc[i++] = USBDHDR(&usb_out_it_desc);
	}
	if (EPIN_EN(opts)) {
		hs_audio_desc[i++] = USBDHDR(&io_in_it_desc);
		hs_audio_desc[i++] = USBDHDR(&usb_in_ot_desc);
	}
	if (EPOUT_EN(opts)) {
		hs_audio_desc[i++] = USBDHDR(&io_out_ot_desc);
		hs_audio_desc[i++] = USBDHDR(&std_as_out_if0_desc);
		hs_audio_desc[i++] = USBDHDR(&std_as_out_if1_desc);
		hs_audio_desc[i++] = USBDHDR(&as_out_hdr_desc);
		hs_audio_desc[i++] = USBDHDR(&as_out_fmt1_desc);
		hs_audio_desc[i++] = USBDHDR(&hs_epout_desc);
		hs_audio_desc[i++] = USBDHDR(&as_iso_out_desc);
	}
	if (EPIN_EN(opts)) {
		hs_audio_desc[i++] = USBDHDR(&std_as_in_if0_desc);
		hs_audio_desc[i++] = USBDHDR(&std_as_in_if1_desc);
		hs_audio_desc[i++] = USBDHDR(&as_in_hdr_desc);
		hs_audio_desc[i++] = USBDHDR(&as_in_fmt1_desc);
		hs_audio_desc[i++] = USBDHDR(&hs_epin_desc);
		hs_audio_desc[i++] = USBDHDR(&as_iso_in_desc);
	}
	hs_audio_desc[i] = NULL;
}

static int
afunc_bind(struct usb_configuration *cfg, struct usb_function *fn)
{
	struct f_uac *uac2 = func_to_uac(fn);
	struct g_audio *agdev = func_to_g_audio(fn);
	struct usb_composite_dev *cdev = cfg->cdev;
	struct usb_gadget *gadget = cdev->gadget;
	struct device *dev = &gadget->dev;
	struct f_uac_opts *uac2_opts;
	struct usb_string *us;
	int ret;

	uac2_opts = container_of(fn->fi, struct f_uac_opts, func_inst);

	us = usb_gstrings_attach(cdev, fn_strings, ARRAY_SIZE(strings_fn));
	if (IS_ERR(us))
		return PTR_ERR(us);
	iad_desc.iFunction = us[STR_ASSOC].id;
	std_ac_if_desc.iInterface = us[STR_IF_CTRL].id;
	in_clk_src_desc.iClockSource = us[STR_CLKSRC_IN].id;
	out_clk_src_desc.iClockSource = us[STR_CLKSRC_OUT].id;
	usb_out_it_desc.iTerminal = us[STR_USB_IT].id;
	io_in_it_desc.iTerminal = us[STR_IO_IT].id;
	usb_in_ot_desc.iTerminal = us[STR_USB_OT].id;
	io_out_ot_desc.iTerminal = us[STR_IO_OT].id;
	std_as_out_if0_desc.iInterface = us[STR_AS_OUT_ALT0].id;
	std_as_out_if1_desc.iInterface = us[STR_AS_OUT_ALT1].id;
	std_as_in_if0_desc.iInterface = us[STR_AS_IN_ALT0].id;
	std_as_in_if1_desc.iInterface = us[STR_AS_IN_ALT1].id;


	/* Initialize the configurable parameters */
	usb_out_it_desc.bNrChannels = num_channels(uac2_opts->c_chmask);
	usb_out_it_desc.bmChannelConfig = cpu_to_le32(uac2_opts->c_chmask);
	io_in_it_desc.bNrChannels = num_channels(uac2_opts->p_chmask);
	io_in_it_desc.bmChannelConfig = cpu_to_le32(uac2_opts->p_chmask);
	as_out_hdr_desc.bNrChannels = num_channels(uac2_opts->c_chmask);
	as_out_hdr_desc.bmChannelConfig = cpu_to_le32(uac2_opts->c_chmask);
	as_in_hdr_desc.bNrChannels = num_channels(uac2_opts->p_chmask);
	as_in_hdr_desc.bmChannelConfig = cpu_to_le32(uac2_opts->p_chmask);
	as_out_fmt1_desc.bSubslotSize = uac2_opts->c_ssize;
	as_out_fmt1_desc.bBitResolution = uac2_opts->c_ssize * 8;
	as_in_fmt1_desc.bSubslotSize = uac2_opts->p_ssize;
	as_in_fmt1_desc.bBitResolution = uac2_opts->p_ssize * 8;

	ret = usb_interface_id(cfg, fn);
	if (ret < 0) {
		dev_err(dev, "%s:%d Error!\n", __func__, __LINE__);
		return ret;
	}
	iad_desc.bFirstInterface = ret;

	std_ac_if_desc.bInterfaceNumber = ret;
	uac2->ac_intf = ret;
	uac2->ac_alt = 0;

	if (EPOUT_EN(uac2_opts)) {
		ret = usb_interface_id(cfg, fn);
		if (ret < 0) {
			dev_err(dev, "%s:%d Error!\n", __func__, __LINE__);
			return ret;
		}
		std_as_out_if0_desc.bInterfaceNumber = ret;
		std_as_out_if1_desc.bInterfaceNumber = ret;
		uac2->as_out_intf = ret;
		uac2->as_out_alt = 0;
	}

	if (EPIN_EN(uac2_opts)) {
		ret = usb_interface_id(cfg, fn);
		if (ret < 0) {
			dev_err(dev, "%s:%d Error!\n", __func__, __LINE__);
			return ret;
		}
		std_as_in_if0_desc.bInterfaceNumber = ret;
		std_as_in_if1_desc.bInterfaceNumber = ret;
		uac2->as_in_intf = ret;
		uac2->as_in_alt = 0;
	}

	/* Calculate wMaxPacketSize according to audio bandwidth */
	set_ep_max_packet_size(uac2_opts, &fs_epin_desc, 1000, true);
	set_ep_max_packet_size(uac2_opts, &fs_epout_desc, 1000, false);
	set_ep_max_packet_size(uac2_opts, &hs_epin_desc, 8000, true);
	set_ep_max_packet_size(uac2_opts, &hs_epout_desc, 8000, false);

	if (EPOUT_EN(uac2_opts)) {
		agdev->out_ep = usb_ep_autoconfig(gadget, &fs_epout_desc);
		if (!agdev->out_ep) {
			dev_err(dev, "%s:%d Error!\n", __func__, __LINE__);
			return -ENODEV;
		}
	}

	if (EPIN_EN(uac2_opts)) {
		agdev->in_ep = usb_ep_autoconfig(gadget, &fs_epin_desc);
		if (!agdev->in_ep) {
			dev_err(dev, "%s:%d Error!\n", __func__, __LINE__);
			return -ENODEV;
		}
	}

	agdev->in_ep_maxpsize = max_t(u16,
				le16_to_cpu(fs_epin_desc.wMaxPacketSize),
				le16_to_cpu(hs_epin_desc.wMaxPacketSize));
	agdev->out_ep_maxpsize = max_t(u16,
				le16_to_cpu(fs_epout_desc.wMaxPacketSize),
				le16_to_cpu(hs_epout_desc.wMaxPacketSize));

	hs_epout_desc.bEndpointAddress = fs_epout_desc.bEndpointAddress;
	hs_epin_desc.bEndpointAddress = fs_epin_desc.bEndpointAddress;

	setup_descriptor(uac2_opts);

	ret = usb_assign_descriptors(fn, fs_audio_desc, hs_audio_desc, NULL,
				     NULL);
	if (ret)
		return ret;

	agdev->gadget = gadget;

	agdev->params.p_chmask = uac2_opts->p_chmask;
	memcpy(agdev->params.p_srate, uac2_opts->p_srate,
			sizeof(agdev->params.p_srate));
	agdev->params.p_srate_active = uac2_opts->p_srate_active;
	agdev->params.p_ssize = uac2_opts->p_ssize;
	agdev->params.c_chmask = uac2_opts->c_chmask;
	memcpy(agdev->params.c_srate, uac2_opts->c_srate,
			sizeof(agdev->params.c_srate));
	agdev->params.c_srate_active = uac2_opts->c_srate_active;
	agdev->params.c_ssize = uac2_opts->c_ssize;
	agdev->params.req_number = uac2_opts->req_number;
	ret = g_audio_setup(agdev, "UAC2 PCM", "UAC2_Gadget");
	if (ret)
		goto err_free_descs;
	return 0;

err_free_descs:
	usb_free_all_descriptors(fn);
	agdev->gadget = NULL;
	return ret;
}

static int
afunc_set_alt(struct usb_function *fn, unsigned intf, unsigned alt)
{
	struct usb_composite_dev *cdev = fn->config->cdev;
	struct f_uac *uac2 = func_to_uac(fn);
	struct usb_gadget *gadget = cdev->gadget;
	struct device *dev = &gadget->dev;
	int ret = 0;

	/* No i/f has more than 2 alt settings */
	if (alt > 1) {
		dev_err(dev, "%s:%d Error!\n", __func__, __LINE__);
		return -EINVAL;
	}

	if (intf == uac2->ac_intf) {
		/* Control I/f has only 1 AltSetting - 0 */
		if (alt) {
			dev_err(dev, "%s:%d Error!\n", __func__, __LINE__);
			return -EINVAL;
		}
		return 0;
	}

	if (intf == uac2->as_out_intf) {
		uac2->as_out_alt = alt;

		if (alt)
			ret = u_audio_start_capture(&uac2->g_audio);
		else
			u_audio_stop_capture(&uac2->g_audio);
	} else if (intf == uac2->as_in_intf) {
		uac2->as_in_alt = alt;

		if (alt)
			ret = u_audio_start_playback(&uac2->g_audio);
		else
			u_audio_stop_playback(&uac2->g_audio);
	} else {
		dev_err(dev, "%s:%d Error!\n", __func__, __LINE__);
		return -EINVAL;
	}

	return ret;
}

static int
afunc_get_alt(struct usb_function *fn, unsigned intf)
{
	struct f_uac *uac2 = func_to_uac(fn);
	struct g_audio *agdev = func_to_g_audio(fn);

	if (intf == uac2->ac_intf)
		return uac2->ac_alt;
	else if (intf == uac2->as_out_intf)
		return uac2->as_out_alt;
	else if (intf == uac2->as_in_intf)
		return uac2->as_in_alt;
	else
		dev_err(&agdev->gadget->dev,
			"%s:%d Invalid Interface %d!\n",
			__func__, __LINE__, intf);

	return -EINVAL;
}

static void
afunc_disable(struct usb_function *fn)
{
	struct f_uac *uac2 = func_to_uac(fn);

	uac2->as_in_alt = 0;
	uac2->as_out_alt = 0;
	u_audio_stop_capture(&uac2->g_audio);
	u_audio_stop_playback(&uac2->g_audio);
}

static int
in_rq_cur(struct usb_function *fn, const struct usb_ctrlrequest *cr)
{
	struct usb_request *req = fn->config->cdev->req;
	struct g_audio *agdev = func_to_g_audio(fn);
	struct f_uac_opts *opts;
	u16 w_length = le16_to_cpu(cr->wLength);
	u16 w_index = le16_to_cpu(cr->wIndex);
	u16 w_value = le16_to_cpu(cr->wValue);
	u8 entity_id = (w_index >> 8) & 0xff;
	u8 control_selector = w_value >> 8;
	int value = -EOPNOTSUPP;
	int p_srate, c_srate;

	opts = g_audio_to_uac_opts(agdev);
	p_srate = opts->p_srate_active;
	c_srate = opts->c_srate_active;

	if (control_selector == UAC2_CS_CONTROL_SAM_FREQ) {
		struct cntrl_cur_lay3 c;
		memset(&c, 0, sizeof(struct cntrl_cur_lay3));

		if (entity_id == USB_IN_CLK_ID)
			c.dCUR = cpu_to_le32(p_srate);
		else if (entity_id == USB_OUT_CLK_ID)
			c.dCUR = cpu_to_le32(c_srate);

		DBG(fn->config->cdev, "%s(): %d\n", __func__, c.dCUR);
		value = min_t(unsigned, w_length, sizeof c);
		memcpy(req->buf, &c, value);
	} else if (control_selector == UAC2_CS_CONTROL_CLOCK_VALID) {
		*(u8 *)req->buf = 1;
		value = min_t(unsigned, w_length, 1);
	} else {
		dev_err(&agdev->gadget->dev,
			"%s:%d control_selector=%d TODO!\n",
			__func__, __LINE__, control_selector);
	}

	return value;
}

static int
in_rq_range(struct usb_function *fn, const struct usb_ctrlrequest *cr)
{
	struct usb_request *req = fn->config->cdev->req;
	struct g_audio *agdev = func_to_g_audio(fn);
	struct f_uac_opts *opts;
	u16 w_length = le16_to_cpu(cr->wLength);
	u16 w_index = le16_to_cpu(cr->wIndex);
	u16 w_value = le16_to_cpu(cr->wValue);
	u8 entity_id = (w_index >> 8) & 0xff;
	u8 control_selector = w_value >> 8;
	struct cntrl_ranges_lay3 rs;
	int value = -EOPNOTSUPP;
	int srate = 0;
	int i;

	opts = g_audio_to_uac_opts(agdev);

	if (control_selector == UAC2_CS_CONTROL_SAM_FREQ) {
		rs.wNumSubRanges = 0;
		for (i = 0; i < UAC_MAX_RATES; i++) {
			if (entity_id == USB_IN_CLK_ID)
				srate = opts->p_srate[i];
			else if (entity_id == USB_OUT_CLK_ID)
				srate = opts->c_srate[i];
			else
				return -EOPNOTSUPP;

			if (srate == 0)
				break;

			rs.r[rs.wNumSubRanges].dMIN = srate;
			rs.r[rs.wNumSubRanges].dMAX = srate;
			rs.r[rs.wNumSubRanges].dRES = 0;
			rs.wNumSubRanges++;
			DBG(fn->config->cdev,
					"%s(): clk %d: report rate %d. %d\n",
					__func__, entity_id, rs.wNumSubRanges,
					srate);
		}

		value = min_t(unsigned int, w_length, ranges_size(rs));
		DBG(fn->config->cdev, "%s(): send %d rates, size %d\n",
				__func__, rs.wNumSubRanges, value);
		memcpy(req->buf, &rs, value);
	} else {
		dev_err(&agdev->gadget->dev,
			"%s:%d control_selector=%d TODO!\n",
			__func__, __LINE__, control_selector);
	}

	return value;
}

static int
ac_rq_in(struct usb_function *fn, const struct usb_ctrlrequest *cr)
{
	DBG(fn->config->cdev, "%s(): %d\n", __func__, cr->bRequest);
	if (cr->bRequest == UAC2_CS_CUR)
		return in_rq_cur(fn, cr);
	else if (cr->bRequest == UAC2_CS_RANGE)
		return in_rq_range(fn, cr);
	else
		return -EOPNOTSUPP;
}

static void uac2_cs_control_sam_freq(struct usb_ep *ep, struct usb_request *req)
{
	struct usb_function *fn = ep->driver_data;
	struct usb_composite_dev *cdev = fn->config->cdev;
	struct g_audio *agdev = func_to_g_audio(fn);
	struct f_uac *uac2 = func_to_uac(fn);
	struct f_uac_opts *opts = g_audio_to_uac_opts(agdev);
	u32 val;

	if (req->actual != 4) {
		WARN(cdev, "Invalid data size for UAC2_CS_CONTROL_SAM_FREQ.\n");
		return;
	}

	val = le32_to_cpu(*((u32 *)req->buf));
	if (uac2->ctl_id == USB_IN_CLK_ID) {
		opts->p_srate_active = val;
		u_audio_set_playback_srate(agdev, opts->p_srate_active);
	} else if (uac2->ctl_id == USB_OUT_CLK_ID) {
		opts->c_srate_active = val;
		u_audio_set_capture_srate(agdev, opts->c_srate_active);
	}
}

static int
out_rq_cur(struct usb_function *fn, const struct usb_ctrlrequest *cr)
{
	struct usb_composite_dev *cdev = fn->config->cdev;
	struct usb_request *req = cdev->req;
	u16 w_length = le16_to_cpu(cr->wLength);
	struct f_uac *uac2 = func_to_uac(fn);
	u16 w_value = le16_to_cpu(cr->wValue);
	u16 w_index = le16_to_cpu(cr->wIndex);
	u8 control_selector = w_value >> 8;
	u8 clock_id = w_index >> 8;

	if (control_selector == UAC2_CS_CONTROL_SAM_FREQ) {
		DBG(cdev, "control_selector UAC2_CS_CONTROL_SAM_FREQ, clock: %d\n",
				clock_id);
		cdev->gadget->ep0->driver_data = fn;
		uac2->ctl_id = clock_id;
		req->complete = uac2_cs_control_sam_freq;
		return w_length;
	}

	return -EOPNOTSUPP;
}

static int
setup_rq_inf(struct usb_function *fn, const struct usb_ctrlrequest *cr)
{
	struct f_uac *uac2 = func_to_uac(fn);
	struct g_audio *agdev = func_to_g_audio(fn);
	u16 w_index = le16_to_cpu(cr->wIndex);
	u8 intf = w_index & 0xff;

	if (intf != uac2->ac_intf) {
		dev_err(&agdev->gadget->dev,
			"%s:%d Error!\n", __func__, __LINE__);
		return -EOPNOTSUPP;
	}

	if (cr->bRequestType & USB_DIR_IN)
		return ac_rq_in(fn, cr);
	else if (cr->bRequest == UAC2_CS_CUR)
		return out_rq_cur(fn, cr);

	return -EOPNOTSUPP;
}

static int
afunc_setup(struct usb_function *fn, const struct usb_ctrlrequest *cr)
{
	struct usb_composite_dev *cdev = fn->config->cdev;
	struct g_audio *agdev = func_to_g_audio(fn);
	struct usb_request *req = cdev->req;
	u16 w_length = le16_to_cpu(cr->wLength);
	int value = -EOPNOTSUPP;

	/* Only Class specific requests are supposed to reach here */
	if ((cr->bRequestType & USB_TYPE_MASK) != USB_TYPE_CLASS)
		return -EOPNOTSUPP;

	if ((cr->bRequestType & USB_RECIP_MASK) == USB_RECIP_INTERFACE)
		value = setup_rq_inf(fn, cr);
	else
		dev_err(&agdev->gadget->dev, "%s:%d Error!\n",
				__func__, __LINE__);

	if (value >= 0) {
		req->length = value;
		req->zero = value < w_length;
		value = usb_ep_queue(cdev->gadget->ep0, req, GFP_ATOMIC);
		if (value < 0) {
			dev_err(&agdev->gadget->dev,
				"%s:%d Error!\n", __func__, __LINE__);
			req->status = 0;
		}
	}

	return value;
}

static struct configfs_item_operations f_uac2_item_ops = {
	.release	= f_uac_attr_release,
};

UAC_ATTRIBUTE(p_chmask);
UAC_ATTRIBUTE(p_ssize);
UAC_ATTRIBUTE(c_chmask);
UAC_ATTRIBUTE(c_ssize);
UAC_ATTRIBUTE(req_number);

UAC_RATE_ATTRIBUTE(p_srate);
UAC_RATE_ATTRIBUTE(c_srate);

static struct configfs_attribute *f_uac2_attrs[] = {
	&f_uac_opts_attr_p_chmask,
	&f_uac_opts_attr_p_srate,
	&f_uac_opts_attr_p_ssize,
	&f_uac_opts_attr_c_chmask,
	&f_uac_opts_attr_c_srate,
	&f_uac_opts_attr_c_ssize,
	&f_uac_opts_attr_req_number,
	NULL,
};

static const struct config_item_type f_uac2_func_type = {
	.ct_item_ops	= &f_uac2_item_ops,
	.ct_attrs	= f_uac2_attrs,
	.ct_owner	= THIS_MODULE,
};

static void afunc_free_inst(struct usb_function_instance *f)
{
	struct f_uac_opts *opts;

	opts = container_of(f, struct f_uac_opts, func_inst);
	kfree(opts);
}

static struct usb_function_instance *afunc_alloc_inst(void)
{
	struct f_uac_opts *opts;

	opts = kzalloc(sizeof(*opts), GFP_KERNEL);
	if (!opts)
		return ERR_PTR(-ENOMEM);

	mutex_init(&opts->lock);
	opts->func_inst.free_func_inst = afunc_free_inst;

	config_group_init_type_name(&opts->func_inst.group, "",
				    &f_uac2_func_type);

	opts->p_chmask = UAC_DEF_PCHMASK;
	opts->p_srate[0] = UAC_DEF_PSRATE;
	opts->p_srate_active = UAC_DEF_PSRATE;
	opts->p_ssize = UAC_DEF_PSSIZE;
	opts->c_chmask = UAC_DEF_CCHMASK;
	opts->c_srate[0] = UAC_DEF_CSRATE;
	opts->c_srate_active = UAC_DEF_CSRATE;
	opts->c_ssize = UAC_DEF_CSSIZE;
	opts->req_number = UAC_DEF_REQ_NUM;
	return &opts->func_inst;
}

static void afunc_free(struct usb_function *f)
{
	struct g_audio *agdev;
	struct f_uac_opts *opts;

	agdev = func_to_g_audio(f);
	opts = container_of(f->fi, struct f_uac_opts, func_inst);
	kfree(agdev);
	mutex_lock(&opts->lock);
	--opts->refcnt;
	mutex_unlock(&opts->lock);
}

static void afunc_unbind(struct usb_configuration *c, struct usb_function *f)
{
	struct g_audio *agdev = func_to_g_audio(f);

	g_audio_cleanup(agdev);
	usb_free_all_descriptors(f);

	agdev->gadget = NULL;
}

static struct usb_function *afunc_alloc(struct usb_function_instance *fi)
{
	struct f_uac	*uac2;
	struct f_uac_opts *opts;

	uac2 = kzalloc(sizeof(*uac2), GFP_KERNEL);
	if (uac2 == NULL)
		return ERR_PTR(-ENOMEM);

	opts = container_of(fi, struct f_uac_opts, func_inst);
	mutex_lock(&opts->lock);
	++opts->refcnt;
	mutex_unlock(&opts->lock);

	uac2->g_audio.func.name = "uac2_func";
	uac2->g_audio.func.bind = afunc_bind;
	uac2->g_audio.func.unbind = afunc_unbind;
	uac2->g_audio.func.set_alt = afunc_set_alt;
	uac2->g_audio.func.get_alt = afunc_get_alt;
	uac2->g_audio.func.disable = afunc_disable;
	uac2->g_audio.func.setup = afunc_setup;
	uac2->g_audio.func.free_func = afunc_free;

	return &uac2->g_audio.func;
}

DECLARE_USB_FUNCTION_INIT(uac2, afunc_alloc_inst, afunc_alloc);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Yadwinder Singh");
MODULE_AUTHOR("Jaswinder Singh");
