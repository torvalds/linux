/*
 * f_uac2.c -- USB Audio Class 2.0 Function
 *
 * Copyright (C) 2011
 *    Yadwinder Singh (yadi.brar01@gmail.com)
 *    Jaswinder Singh (jaswinder.singh@linaro.org)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/usb/audio.h>
#include <linux/usb/audio-v2.h>
#include <linux/platform_device.h>
#include <linux/module.h>

#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>

#include "u_uac2.h"

/* Keep everyone on toes */
#define USB_XFERS	2

/*
 * The driver implements a simple UAC_2 topology.
 * USB-OUT -> IT_1 -> OT_3 -> ALSA_Capture
 * ALSA_Playback -> IT_2 -> OT_4 -> USB-IN
 * Capture and Playback sampling rates are independently
 *  controlled by two clock sources :
 *    CLK_5 := c_srate, and CLK_6 := p_srate
 */
#define USB_OUT_IT_ID	1
#define IO_IN_IT_ID	2
#define IO_OUT_OT_ID	3
#define USB_IN_OT_ID	4
#define USB_OUT_CLK_ID	5
#define USB_IN_CLK_ID	6

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

static const char *uac2_name = "snd_uac2";

struct uac2_req {
	struct uac2_rtd_params *pp; /* parent param */
	struct usb_request *req;
};

struct uac2_rtd_params {
	struct snd_uac2_chip *uac2; /* parent chip */
	bool ep_enabled; /* if the ep is enabled */
	/* Size of the ring buffer */
	size_t dma_bytes;
	unsigned char *dma_area;

	struct snd_pcm_substream *ss;

	/* Ring buffer */
	ssize_t hw_ptr;

	void *rbuf;

	size_t period_size;

	unsigned max_psize;
	struct uac2_req ureq[USB_XFERS];

	spinlock_t lock;
};

struct snd_uac2_chip {
	struct platform_device pdev;
	struct platform_driver pdrv;

	struct uac2_rtd_params p_prm;
	struct uac2_rtd_params c_prm;

	struct snd_card *card;
	struct snd_pcm *pcm;

	/* timekeeping for the playback endpoint */
	unsigned int p_interval;
	unsigned int p_residue;

	/* pre-calculated values for playback iso completion */
	unsigned int p_pktsize;
	unsigned int p_pktsize_residue;
	unsigned int p_framesize;
};

#define BUFF_SIZE_MAX	(PAGE_SIZE * 16)
#define PRD_SIZE_MAX	PAGE_SIZE
#define MIN_PERIODS	4

static struct snd_pcm_hardware uac2_pcm_hardware = {
	.info = SNDRV_PCM_INFO_INTERLEAVED | SNDRV_PCM_INFO_BLOCK_TRANSFER
		 | SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_MMAP_VALID
		 | SNDRV_PCM_INFO_PAUSE | SNDRV_PCM_INFO_RESUME,
	.rates = SNDRV_PCM_RATE_CONTINUOUS,
	.periods_max = BUFF_SIZE_MAX / PRD_SIZE_MAX,
	.buffer_bytes_max = BUFF_SIZE_MAX,
	.period_bytes_max = PRD_SIZE_MAX,
	.periods_min = MIN_PERIODS,
};

struct audio_dev {
	u8 ac_intf, ac_alt;
	u8 as_out_intf, as_out_alt;
	u8 as_in_intf, as_in_alt;

	struct usb_ep *in_ep, *out_ep;
	struct usb_function func;

	/* The ALSA Sound Card it represents on the USB-Client side */
	struct snd_uac2_chip uac2;
};

static inline
struct audio_dev *func_to_agdev(struct usb_function *f)
{
	return container_of(f, struct audio_dev, func);
}

static inline
struct audio_dev *uac2_to_agdev(struct snd_uac2_chip *u)
{
	return container_of(u, struct audio_dev, uac2);
}

static inline
struct snd_uac2_chip *pdev_to_uac2(struct platform_device *p)
{
	return container_of(p, struct snd_uac2_chip, pdev);
}

static inline
struct f_uac2_opts *agdev_to_uac2_opts(struct audio_dev *agdev)
{
	return container_of(agdev->func.fi, struct f_uac2_opts, func_inst);
}

static inline
uint num_channels(uint chanmask)
{
	uint num = 0;

	while (chanmask) {
		num += (chanmask & 1);
		chanmask >>= 1;
	}

	return num;
}

static void
agdev_iso_complete(struct usb_ep *ep, struct usb_request *req)
{
	unsigned pending;
	unsigned long flags;
	unsigned int hw_ptr;
	bool update_alsa = false;
	int status = req->status;
	struct uac2_req *ur = req->context;
	struct snd_pcm_substream *substream;
	struct uac2_rtd_params *prm = ur->pp;
	struct snd_uac2_chip *uac2 = prm->uac2;

	/* i/f shutting down */
	if (!prm->ep_enabled || req->status == -ESHUTDOWN)
		return;

	/*
	 * We can't really do much about bad xfers.
	 * Afterall, the ISOCH xfers could fail legitimately.
	 */
	if (status)
		pr_debug("%s: iso_complete status(%d) %d/%d\n",
			__func__, status, req->actual, req->length);

	substream = prm->ss;

	/* Do nothing if ALSA isn't active */
	if (!substream)
		goto exit;

	spin_lock_irqsave(&prm->lock, flags);

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		/*
		 * For each IN packet, take the quotient of the current data
		 * rate and the endpoint's interval as the base packet size.
		 * If there is a residue from this division, add it to the
		 * residue accumulator.
		 */
		req->length = uac2->p_pktsize;
		uac2->p_residue += uac2->p_pktsize_residue;

		/*
		 * Whenever there are more bytes in the accumulator than we
		 * need to add one more sample frame, increase this packet's
		 * size and decrease the accumulator.
		 */
		if (uac2->p_residue / uac2->p_interval >= uac2->p_framesize) {
			req->length += uac2->p_framesize;
			uac2->p_residue -= uac2->p_framesize *
					   uac2->p_interval;
		}

		req->actual = req->length;
	}

	pending = prm->hw_ptr % prm->period_size;
	pending += req->actual;
	if (pending >= prm->period_size)
		update_alsa = true;

	hw_ptr = prm->hw_ptr;
	prm->hw_ptr = (prm->hw_ptr + req->actual) % prm->dma_bytes;

	spin_unlock_irqrestore(&prm->lock, flags);

	/* Pack USB load in ALSA ring buffer */
	pending = prm->dma_bytes - hw_ptr;

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		if (unlikely(pending < req->actual)) {
			memcpy(req->buf, prm->dma_area + hw_ptr, pending);
			memcpy(req->buf + pending, prm->dma_area,
			       req->actual - pending);
		} else {
			memcpy(req->buf, prm->dma_area + hw_ptr, req->actual);
		}
	} else {
		if (unlikely(pending < req->actual)) {
			memcpy(prm->dma_area + hw_ptr, req->buf, pending);
			memcpy(prm->dma_area, req->buf + pending,
			       req->actual - pending);
		} else {
			memcpy(prm->dma_area + hw_ptr, req->buf, req->actual);
		}
	}

exit:
	if (usb_ep_queue(ep, req, GFP_ATOMIC))
		dev_err(&uac2->pdev.dev, "%d Error!\n", __LINE__);

	if (update_alsa)
		snd_pcm_period_elapsed(substream);

	return;
}

static int
uac2_pcm_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct snd_uac2_chip *uac2 = snd_pcm_substream_chip(substream);
	struct uac2_rtd_params *prm;
	unsigned long flags;
	int err = 0;

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		prm = &uac2->p_prm;
	else
		prm = &uac2->c_prm;

	spin_lock_irqsave(&prm->lock, flags);

	/* Reset */
	prm->hw_ptr = 0;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
		prm->ss = substream;
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
		prm->ss = NULL;
		break;
	default:
		err = -EINVAL;
	}

	spin_unlock_irqrestore(&prm->lock, flags);

	/* Clear buffer after Play stops */
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK && !prm->ss)
		memset(prm->rbuf, 0, prm->max_psize * USB_XFERS);

	return err;
}

static snd_pcm_uframes_t uac2_pcm_pointer(struct snd_pcm_substream *substream)
{
	struct snd_uac2_chip *uac2 = snd_pcm_substream_chip(substream);
	struct uac2_rtd_params *prm;

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		prm = &uac2->p_prm;
	else
		prm = &uac2->c_prm;

	return bytes_to_frames(substream->runtime, prm->hw_ptr);
}

static int uac2_pcm_hw_params(struct snd_pcm_substream *substream,
			       struct snd_pcm_hw_params *hw_params)
{
	struct snd_uac2_chip *uac2 = snd_pcm_substream_chip(substream);
	struct uac2_rtd_params *prm;
	int err;

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		prm = &uac2->p_prm;
	else
		prm = &uac2->c_prm;

	err = snd_pcm_lib_malloc_pages(substream,
					params_buffer_bytes(hw_params));
	if (err >= 0) {
		prm->dma_bytes = substream->runtime->dma_bytes;
		prm->dma_area = substream->runtime->dma_area;
		prm->period_size = params_period_bytes(hw_params);
	}

	return err;
}

static int uac2_pcm_hw_free(struct snd_pcm_substream *substream)
{
	struct snd_uac2_chip *uac2 = snd_pcm_substream_chip(substream);
	struct uac2_rtd_params *prm;

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		prm = &uac2->p_prm;
	else
		prm = &uac2->c_prm;

	prm->dma_area = NULL;
	prm->dma_bytes = 0;
	prm->period_size = 0;

	return snd_pcm_lib_free_pages(substream);
}

static int uac2_pcm_open(struct snd_pcm_substream *substream)
{
	struct snd_uac2_chip *uac2 = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct audio_dev *audio_dev;
	struct f_uac2_opts *opts;
	int p_ssize, c_ssize;
	int p_srate, c_srate;
	int p_chmask, c_chmask;

	audio_dev = uac2_to_agdev(uac2);
	opts = container_of(audio_dev->func.fi, struct f_uac2_opts, func_inst);
	p_ssize = opts->p_ssize;
	c_ssize = opts->c_ssize;
	p_srate = opts->p_srate;
	c_srate = opts->c_srate;
	p_chmask = opts->p_chmask;
	c_chmask = opts->c_chmask;
	uac2->p_residue = 0;

	runtime->hw = uac2_pcm_hardware;

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		spin_lock_init(&uac2->p_prm.lock);
		runtime->hw.rate_min = p_srate;
		switch (p_ssize) {
		case 3:
			runtime->hw.formats = SNDRV_PCM_FMTBIT_S24_3LE;
			break;
		case 4:
			runtime->hw.formats = SNDRV_PCM_FMTBIT_S32_LE;
			break;
		default:
			runtime->hw.formats = SNDRV_PCM_FMTBIT_S16_LE;
			break;
		}
		runtime->hw.channels_min = num_channels(p_chmask);
		runtime->hw.period_bytes_min = 2 * uac2->p_prm.max_psize
						/ runtime->hw.periods_min;
	} else {
		spin_lock_init(&uac2->c_prm.lock);
		runtime->hw.rate_min = c_srate;
		switch (c_ssize) {
		case 3:
			runtime->hw.formats = SNDRV_PCM_FMTBIT_S24_3LE;
			break;
		case 4:
			runtime->hw.formats = SNDRV_PCM_FMTBIT_S32_LE;
			break;
		default:
			runtime->hw.formats = SNDRV_PCM_FMTBIT_S16_LE;
			break;
		}
		runtime->hw.channels_min = num_channels(c_chmask);
		runtime->hw.period_bytes_min = 2 * uac2->c_prm.max_psize
						/ runtime->hw.periods_min;
	}

	runtime->hw.rate_max = runtime->hw.rate_min;
	runtime->hw.channels_max = runtime->hw.channels_min;

	snd_pcm_hw_constraint_integer(runtime, SNDRV_PCM_HW_PARAM_PERIODS);

	return 0;
}

/* ALSA cries without these function pointers */
static int uac2_pcm_null(struct snd_pcm_substream *substream)
{
	return 0;
}

static struct snd_pcm_ops uac2_pcm_ops = {
	.open = uac2_pcm_open,
	.close = uac2_pcm_null,
	.ioctl = snd_pcm_lib_ioctl,
	.hw_params = uac2_pcm_hw_params,
	.hw_free = uac2_pcm_hw_free,
	.trigger = uac2_pcm_trigger,
	.pointer = uac2_pcm_pointer,
	.prepare = uac2_pcm_null,
};

static int snd_uac2_probe(struct platform_device *pdev)
{
	struct snd_uac2_chip *uac2 = pdev_to_uac2(pdev);
	struct snd_card *card;
	struct snd_pcm *pcm;
	struct audio_dev *audio_dev;
	struct f_uac2_opts *opts;
	int err;
	int p_chmask, c_chmask;

	audio_dev = uac2_to_agdev(uac2);
	opts = container_of(audio_dev->func.fi, struct f_uac2_opts, func_inst);
	p_chmask = opts->p_chmask;
	c_chmask = opts->c_chmask;

	/* Choose any slot, with no id */
	err = snd_card_new(&pdev->dev, -1, NULL, THIS_MODULE, 0, &card);
	if (err < 0)
		return err;

	uac2->card = card;

	/*
	 * Create first PCM device
	 * Create a substream only for non-zero channel streams
	 */
	err = snd_pcm_new(uac2->card, "UAC2 PCM", 0,
			       p_chmask ? 1 : 0, c_chmask ? 1 : 0, &pcm);
	if (err < 0)
		goto snd_fail;

	strcpy(pcm->name, "UAC2 PCM");
	pcm->private_data = uac2;

	uac2->pcm = pcm;

	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &uac2_pcm_ops);
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE, &uac2_pcm_ops);

	strcpy(card->driver, "UAC2_Gadget");
	strcpy(card->shortname, "UAC2_Gadget");
	sprintf(card->longname, "UAC2_Gadget %i", pdev->id);

	snd_pcm_lib_preallocate_pages_for_all(pcm, SNDRV_DMA_TYPE_CONTINUOUS,
		snd_dma_continuous_data(GFP_KERNEL), 0, BUFF_SIZE_MAX);

	err = snd_card_register(card);
	if (!err) {
		platform_set_drvdata(pdev, card);
		return 0;
	}

snd_fail:
	snd_card_free(card);

	uac2->pcm = NULL;
	uac2->card = NULL;

	return err;
}

static int snd_uac2_remove(struct platform_device *pdev)
{
	struct snd_card *card = platform_get_drvdata(pdev);

	if (card)
		return snd_card_free(card);

	return 0;
}

static void snd_uac2_release(struct device *dev)
{
	dev_dbg(dev, "releasing '%s'\n", dev_name(dev));
}

static int alsa_uac2_init(struct audio_dev *agdev)
{
	struct snd_uac2_chip *uac2 = &agdev->uac2;
	int err;

	uac2->pdrv.probe = snd_uac2_probe;
	uac2->pdrv.remove = snd_uac2_remove;
	uac2->pdrv.driver.name = uac2_name;

	uac2->pdev.id = 0;
	uac2->pdev.name = uac2_name;
	uac2->pdev.dev.release = snd_uac2_release;

	/* Register snd_uac2 driver */
	err = platform_driver_register(&uac2->pdrv);
	if (err)
		return err;

	/* Register snd_uac2 device */
	err = platform_device_register(&uac2->pdev);
	if (err)
		platform_driver_unregister(&uac2->pdrv);

	return err;
}

static void alsa_uac2_exit(struct audio_dev *agdev)
{
	struct snd_uac2_chip *uac2 = &agdev->uac2;

	platform_driver_unregister(&uac2->pdrv);
	platform_device_unregister(&uac2->pdev);
}


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

static char clksrc_in[8];
static char clksrc_out[8];

static struct usb_string strings_fn[] = {
	[STR_ASSOC].s = "Source/Sink",
	[STR_IF_CTRL].s = "Topology Control",
	[STR_CLKSRC_IN].s = clksrc_in,
	[STR_CLKSRC_OUT].s = clksrc_out,
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

static struct usb_qualifier_descriptor devqual_desc = {
	.bLength = sizeof devqual_desc,
	.bDescriptorType = USB_DT_DEVICE_QUALIFIER,

	.bcdUSB = cpu_to_le16(0x200),
	.bDeviceClass = USB_CLASS_MISC,
	.bDeviceSubClass = 0x02,
	.bDeviceProtocol = 0x01,
	.bNumConfigurations = 1,
	.bRESERVED = 0,
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
	.bClockID = USB_IN_CLK_ID,
	.bmAttributes = UAC_CLOCK_SOURCE_TYPE_INT_FIXED,
	.bmControls = (CONTROL_RDONLY << CLK_FREQ_CTRL),
	.bAssocTerminal = 0,
};

/* Clock source for OUT traffic */
static struct uac_clock_source_descriptor out_clk_src_desc = {
	.bLength = sizeof out_clk_src_desc,
	.bDescriptorType = USB_DT_CS_INTERFACE,

	.bDescriptorSubtype = UAC2_CLOCK_SOURCE,
	.bClockID = USB_OUT_CLK_ID,
	.bmAttributes = UAC_CLOCK_SOURCE_TYPE_INT_FIXED,
	.bmControls = (CONTROL_RDONLY << CLK_FREQ_CTRL),
	.bAssocTerminal = 0,
};

/* Input Terminal for USB_OUT */
static struct uac2_input_terminal_descriptor usb_out_it_desc = {
	.bLength = sizeof usb_out_it_desc,
	.bDescriptorType = USB_DT_CS_INTERFACE,

	.bDescriptorSubtype = UAC_INPUT_TERMINAL,
	.bTerminalID = USB_OUT_IT_ID,
	.wTerminalType = cpu_to_le16(UAC_TERMINAL_STREAMING),
	.bAssocTerminal = 0,
	.bCSourceID = USB_OUT_CLK_ID,
	.iChannelNames = 0,
	.bmControls = (CONTROL_RDWR << COPY_CTRL),
};

/* Input Terminal for I/O-In */
static struct uac2_input_terminal_descriptor io_in_it_desc = {
	.bLength = sizeof io_in_it_desc,
	.bDescriptorType = USB_DT_CS_INTERFACE,

	.bDescriptorSubtype = UAC_INPUT_TERMINAL,
	.bTerminalID = IO_IN_IT_ID,
	.wTerminalType = cpu_to_le16(UAC_INPUT_TERMINAL_UNDEFINED),
	.bAssocTerminal = 0,
	.bCSourceID = USB_IN_CLK_ID,
	.iChannelNames = 0,
	.bmControls = (CONTROL_RDWR << COPY_CTRL),
};

/* Ouput Terminal for USB_IN */
static struct uac2_output_terminal_descriptor usb_in_ot_desc = {
	.bLength = sizeof usb_in_ot_desc,
	.bDescriptorType = USB_DT_CS_INTERFACE,

	.bDescriptorSubtype = UAC_OUTPUT_TERMINAL,
	.bTerminalID = USB_IN_OT_ID,
	.wTerminalType = cpu_to_le16(UAC_TERMINAL_STREAMING),
	.bAssocTerminal = 0,
	.bSourceID = IO_IN_IT_ID,
	.bCSourceID = USB_IN_CLK_ID,
	.bmControls = (CONTROL_RDWR << COPY_CTRL),
};

/* Ouput Terminal for I/O-Out */
static struct uac2_output_terminal_descriptor io_out_ot_desc = {
	.bLength = sizeof io_out_ot_desc,
	.bDescriptorType = USB_DT_CS_INTERFACE,

	.bDescriptorSubtype = UAC_OUTPUT_TERMINAL,
	.bTerminalID = IO_OUT_OT_ID,
	.wTerminalType = cpu_to_le16(UAC_OUTPUT_TERMINAL_UNDEFINED),
	.bAssocTerminal = 0,
	.bSourceID = USB_OUT_IT_ID,
	.bCSourceID = USB_OUT_CLK_ID,
	.bmControls = (CONTROL_RDWR << COPY_CTRL),
};

static struct uac2_ac_header_descriptor ac_hdr_desc = {
	.bLength = sizeof ac_hdr_desc,
	.bDescriptorType = USB_DT_CS_INTERFACE,

	.bDescriptorSubtype = UAC_MS_HEADER,
	.bcdADC = cpu_to_le16(0x200),
	.bCategory = UAC2_FUNCTION_IO_BOX,
	.wTotalLength = sizeof in_clk_src_desc + sizeof out_clk_src_desc
			 + sizeof usb_out_it_desc + sizeof io_in_it_desc
			+ sizeof usb_in_ot_desc + sizeof io_out_ot_desc,
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
	.bTerminalLink = USB_OUT_IT_ID,
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
	.bmAttributes = USB_ENDPOINT_XFER_ISOC | USB_ENDPOINT_SYNC_ASYNC,
	.wMaxPacketSize = cpu_to_le16(1023),
	.bInterval = 1,
};

static struct usb_endpoint_descriptor hs_epout_desc = {
	.bLength = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType = USB_DT_ENDPOINT,

	.bmAttributes = USB_ENDPOINT_XFER_ISOC | USB_ENDPOINT_SYNC_ASYNC,
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
	.bTerminalLink = USB_IN_OT_ID,
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
	.bmAttributes = USB_ENDPOINT_XFER_ISOC | USB_ENDPOINT_SYNC_ASYNC,
	.wMaxPacketSize = cpu_to_le16(1023),
	.bInterval = 1,
};

static struct usb_endpoint_descriptor hs_epin_desc = {
	.bLength = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType = USB_DT_ENDPOINT,

	.bmAttributes = USB_ENDPOINT_XFER_ISOC | USB_ENDPOINT_SYNC_ASYNC,
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
	__u32	dCUR;
};

struct cntrl_range_lay3 {
	__u16	wNumSubRanges;
	__u32	dMIN;
	__u32	dMAX;
	__u32	dRES;
} __packed;

static inline void
free_ep(struct uac2_rtd_params *prm, struct usb_ep *ep)
{
	struct snd_uac2_chip *uac2 = prm->uac2;
	int i;

	if (!prm->ep_enabled)
		return;

	prm->ep_enabled = false;

	for (i = 0; i < USB_XFERS; i++) {
		if (prm->ureq[i].req) {
			usb_ep_dequeue(ep, prm->ureq[i].req);
			usb_ep_free_request(ep, prm->ureq[i].req);
			prm->ureq[i].req = NULL;
		}
	}

	if (usb_ep_disable(ep))
		dev_err(&uac2->pdev.dev,
			"%s:%d Error!\n", __func__, __LINE__);
}

static int
afunc_bind(struct usb_configuration *cfg, struct usb_function *fn)
{
	struct audio_dev *agdev = func_to_agdev(fn);
	struct snd_uac2_chip *uac2 = &agdev->uac2;
	struct usb_composite_dev *cdev = cfg->cdev;
	struct usb_gadget *gadget = cdev->gadget;
	struct device *dev = &uac2->pdev.dev;
	struct uac2_rtd_params *prm;
	struct f_uac2_opts *uac2_opts;
	struct usb_string *us;
	int ret;

	uac2_opts = container_of(fn->fi, struct f_uac2_opts, func_inst);

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

	snprintf(clksrc_in, sizeof(clksrc_in), "%uHz", uac2_opts->p_srate);
	snprintf(clksrc_out, sizeof(clksrc_out), "%uHz", uac2_opts->c_srate);

	ret = usb_interface_id(cfg, fn);
	if (ret < 0) {
		dev_err(dev, "%s:%d Error!\n", __func__, __LINE__);
		return ret;
	}
	std_ac_if_desc.bInterfaceNumber = ret;
	agdev->ac_intf = ret;
	agdev->ac_alt = 0;

	ret = usb_interface_id(cfg, fn);
	if (ret < 0) {
		dev_err(dev, "%s:%d Error!\n", __func__, __LINE__);
		return ret;
	}
	std_as_out_if0_desc.bInterfaceNumber = ret;
	std_as_out_if1_desc.bInterfaceNumber = ret;
	agdev->as_out_intf = ret;
	agdev->as_out_alt = 0;

	ret = usb_interface_id(cfg, fn);
	if (ret < 0) {
		dev_err(dev, "%s:%d Error!\n", __func__, __LINE__);
		return ret;
	}
	std_as_in_if0_desc.bInterfaceNumber = ret;
	std_as_in_if1_desc.bInterfaceNumber = ret;
	agdev->as_in_intf = ret;
	agdev->as_in_alt = 0;

	agdev->out_ep = usb_ep_autoconfig(gadget, &fs_epout_desc);
	if (!agdev->out_ep) {
		dev_err(dev, "%s:%d Error!\n", __func__, __LINE__);
		goto err;
	}
	agdev->out_ep->driver_data = agdev;

	agdev->in_ep = usb_ep_autoconfig(gadget, &fs_epin_desc);
	if (!agdev->in_ep) {
		dev_err(dev, "%s:%d Error!\n", __func__, __LINE__);
		goto err;
	}
	agdev->in_ep->driver_data = agdev;

	uac2->p_prm.uac2 = uac2;
	uac2->c_prm.uac2 = uac2;

	hs_epout_desc.bEndpointAddress = fs_epout_desc.bEndpointAddress;
	hs_epout_desc.wMaxPacketSize = fs_epout_desc.wMaxPacketSize;
	hs_epin_desc.bEndpointAddress = fs_epin_desc.bEndpointAddress;
	hs_epin_desc.wMaxPacketSize = fs_epin_desc.wMaxPacketSize;

	ret = usb_assign_descriptors(fn, fs_audio_desc, hs_audio_desc, NULL);
	if (ret)
		goto err;

	prm = &agdev->uac2.c_prm;
	prm->max_psize = hs_epout_desc.wMaxPacketSize;
	prm->rbuf = kzalloc(prm->max_psize * USB_XFERS, GFP_KERNEL);
	if (!prm->rbuf) {
		prm->max_psize = 0;
		goto err_free_descs;
	}

	prm = &agdev->uac2.p_prm;
	prm->max_psize = hs_epin_desc.wMaxPacketSize;
	prm->rbuf = kzalloc(prm->max_psize * USB_XFERS, GFP_KERNEL);
	if (!prm->rbuf) {
		prm->max_psize = 0;
		goto err_free_descs;
	}

	ret = alsa_uac2_init(agdev);
	if (ret)
		goto err_free_descs;
	return 0;

err_free_descs:
	usb_free_all_descriptors(fn);
err:
	kfree(agdev->uac2.p_prm.rbuf);
	kfree(agdev->uac2.c_prm.rbuf);
	if (agdev->in_ep)
		agdev->in_ep->driver_data = NULL;
	if (agdev->out_ep)
		agdev->out_ep->driver_data = NULL;
	return -EINVAL;
}

static int
afunc_set_alt(struct usb_function *fn, unsigned intf, unsigned alt)
{
	struct usb_composite_dev *cdev = fn->config->cdev;
	struct audio_dev *agdev = func_to_agdev(fn);
	struct snd_uac2_chip *uac2 = &agdev->uac2;
	struct usb_gadget *gadget = cdev->gadget;
	struct device *dev = &uac2->pdev.dev;
	struct usb_request *req;
	struct usb_ep *ep;
	struct uac2_rtd_params *prm;
	int req_len, i;

	/* No i/f has more than 2 alt settings */
	if (alt > 1) {
		dev_err(dev, "%s:%d Error!\n", __func__, __LINE__);
		return -EINVAL;
	}

	if (intf == agdev->ac_intf) {
		/* Control I/f has only 1 AltSetting - 0 */
		if (alt) {
			dev_err(dev, "%s:%d Error!\n", __func__, __LINE__);
			return -EINVAL;
		}
		return 0;
	}

	if (intf == agdev->as_out_intf) {
		ep = agdev->out_ep;
		prm = &uac2->c_prm;
		config_ep_by_speed(gadget, fn, ep);
		agdev->as_out_alt = alt;
		req_len = prm->max_psize;
	} else if (intf == agdev->as_in_intf) {
		struct f_uac2_opts *opts = agdev_to_uac2_opts(agdev);
		unsigned int factor, rate;
		struct usb_endpoint_descriptor *ep_desc;

		ep = agdev->in_ep;
		prm = &uac2->p_prm;
		config_ep_by_speed(gadget, fn, ep);
		agdev->as_in_alt = alt;

		/* pre-calculate the playback endpoint's interval */
		if (gadget->speed == USB_SPEED_FULL) {
			ep_desc = &fs_epin_desc;
			factor = 1000;
		} else {
			ep_desc = &hs_epin_desc;
			factor = 8000;
		}

		/* pre-compute some values for iso_complete() */
		uac2->p_framesize = opts->p_ssize *
				    num_channels(opts->p_chmask);
		rate = opts->p_srate * uac2->p_framesize;
		uac2->p_interval = factor / (1 << (ep_desc->bInterval - 1));
		uac2->p_pktsize = min_t(unsigned int, rate / uac2->p_interval,
					prm->max_psize);

		if (uac2->p_pktsize < prm->max_psize)
			uac2->p_pktsize_residue = rate % uac2->p_interval;
		else
			uac2->p_pktsize_residue = 0;

		req_len = uac2->p_pktsize;
		uac2->p_residue = 0;
	} else {
		dev_err(dev, "%s:%d Error!\n", __func__, __LINE__);
		return -EINVAL;
	}

	if (alt == 0) {
		free_ep(prm, ep);
		return 0;
	}

	prm->ep_enabled = true;
	usb_ep_enable(ep);

	for (i = 0; i < USB_XFERS; i++) {
		if (!prm->ureq[i].req) {
			req = usb_ep_alloc_request(ep, GFP_ATOMIC);
			if (req == NULL)
				return -ENOMEM;

			prm->ureq[i].req = req;
			prm->ureq[i].pp = prm;

			req->zero = 0;
			req->context = &prm->ureq[i];
			req->length = req_len;
			req->complete = agdev_iso_complete;
			req->buf = prm->rbuf + i * prm->max_psize;
		}

		if (usb_ep_queue(ep, prm->ureq[i].req, GFP_ATOMIC))
			dev_err(dev, "%s:%d Error!\n", __func__, __LINE__);
	}

	return 0;
}

static int
afunc_get_alt(struct usb_function *fn, unsigned intf)
{
	struct audio_dev *agdev = func_to_agdev(fn);
	struct snd_uac2_chip *uac2 = &agdev->uac2;

	if (intf == agdev->ac_intf)
		return agdev->ac_alt;
	else if (intf == agdev->as_out_intf)
		return agdev->as_out_alt;
	else if (intf == agdev->as_in_intf)
		return agdev->as_in_alt;
	else
		dev_err(&uac2->pdev.dev,
			"%s:%d Invalid Interface %d!\n",
			__func__, __LINE__, intf);

	return -EINVAL;
}

static void
afunc_disable(struct usb_function *fn)
{
	struct audio_dev *agdev = func_to_agdev(fn);
	struct snd_uac2_chip *uac2 = &agdev->uac2;

	free_ep(&uac2->p_prm, agdev->in_ep);
	agdev->as_in_alt = 0;

	free_ep(&uac2->c_prm, agdev->out_ep);
	agdev->as_out_alt = 0;
}

static int
in_rq_cur(struct usb_function *fn, const struct usb_ctrlrequest *cr)
{
	struct usb_request *req = fn->config->cdev->req;
	struct audio_dev *agdev = func_to_agdev(fn);
	struct snd_uac2_chip *uac2 = &agdev->uac2;
	struct f_uac2_opts *opts;
	u16 w_length = le16_to_cpu(cr->wLength);
	u16 w_index = le16_to_cpu(cr->wIndex);
	u16 w_value = le16_to_cpu(cr->wValue);
	u8 entity_id = (w_index >> 8) & 0xff;
	u8 control_selector = w_value >> 8;
	int value = -EOPNOTSUPP;
	int p_srate, c_srate;

	opts = agdev_to_uac2_opts(agdev);
	p_srate = opts->p_srate;
	c_srate = opts->c_srate;

	if (control_selector == UAC2_CS_CONTROL_SAM_FREQ) {
		struct cntrl_cur_lay3 c;

		if (entity_id == USB_IN_CLK_ID)
			c.dCUR = p_srate;
		else if (entity_id == USB_OUT_CLK_ID)
			c.dCUR = c_srate;

		value = min_t(unsigned, w_length, sizeof c);
		memcpy(req->buf, &c, value);
	} else if (control_selector == UAC2_CS_CONTROL_CLOCK_VALID) {
		*(u8 *)req->buf = 1;
		value = min_t(unsigned, w_length, 1);
	} else {
		dev_err(&uac2->pdev.dev,
			"%s:%d control_selector=%d TODO!\n",
			__func__, __LINE__, control_selector);
	}

	return value;
}

static int
in_rq_range(struct usb_function *fn, const struct usb_ctrlrequest *cr)
{
	struct usb_request *req = fn->config->cdev->req;
	struct audio_dev *agdev = func_to_agdev(fn);
	struct snd_uac2_chip *uac2 = &agdev->uac2;
	struct f_uac2_opts *opts;
	u16 w_length = le16_to_cpu(cr->wLength);
	u16 w_index = le16_to_cpu(cr->wIndex);
	u16 w_value = le16_to_cpu(cr->wValue);
	u8 entity_id = (w_index >> 8) & 0xff;
	u8 control_selector = w_value >> 8;
	struct cntrl_range_lay3 r;
	int value = -EOPNOTSUPP;
	int p_srate, c_srate;

	opts = agdev_to_uac2_opts(agdev);
	p_srate = opts->p_srate;
	c_srate = opts->c_srate;

	if (control_selector == UAC2_CS_CONTROL_SAM_FREQ) {
		if (entity_id == USB_IN_CLK_ID)
			r.dMIN = p_srate;
		else if (entity_id == USB_OUT_CLK_ID)
			r.dMIN = c_srate;
		else
			return -EOPNOTSUPP;

		r.dMAX = r.dMIN;
		r.dRES = 0;
		r.wNumSubRanges = 1;

		value = min_t(unsigned, w_length, sizeof r);
		memcpy(req->buf, &r, value);
	} else {
		dev_err(&uac2->pdev.dev,
			"%s:%d control_selector=%d TODO!\n",
			__func__, __LINE__, control_selector);
	}

	return value;
}

static int
ac_rq_in(struct usb_function *fn, const struct usb_ctrlrequest *cr)
{
	if (cr->bRequest == UAC2_CS_CUR)
		return in_rq_cur(fn, cr);
	else if (cr->bRequest == UAC2_CS_RANGE)
		return in_rq_range(fn, cr);
	else
		return -EOPNOTSUPP;
}

static int
out_rq_cur(struct usb_function *fn, const struct usb_ctrlrequest *cr)
{
	u16 w_length = le16_to_cpu(cr->wLength);
	u16 w_value = le16_to_cpu(cr->wValue);
	u8 control_selector = w_value >> 8;

	if (control_selector == UAC2_CS_CONTROL_SAM_FREQ)
		return w_length;

	return -EOPNOTSUPP;
}

static int
setup_rq_inf(struct usb_function *fn, const struct usb_ctrlrequest *cr)
{
	struct audio_dev *agdev = func_to_agdev(fn);
	struct snd_uac2_chip *uac2 = &agdev->uac2;
	u16 w_index = le16_to_cpu(cr->wIndex);
	u8 intf = w_index & 0xff;

	if (intf != agdev->ac_intf) {
		dev_err(&uac2->pdev.dev,
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
	struct audio_dev *agdev = func_to_agdev(fn);
	struct snd_uac2_chip *uac2 = &agdev->uac2;
	struct usb_request *req = cdev->req;
	u16 w_length = le16_to_cpu(cr->wLength);
	int value = -EOPNOTSUPP;

	/* Only Class specific requests are supposed to reach here */
	if ((cr->bRequestType & USB_TYPE_MASK) != USB_TYPE_CLASS)
		return -EOPNOTSUPP;

	if ((cr->bRequestType & USB_RECIP_MASK) == USB_RECIP_INTERFACE)
		value = setup_rq_inf(fn, cr);
	else
		dev_err(&uac2->pdev.dev, "%s:%d Error!\n", __func__, __LINE__);

	if (value >= 0) {
		req->length = value;
		req->zero = value < w_length;
		value = usb_ep_queue(cdev->gadget->ep0, req, GFP_ATOMIC);
		if (value < 0) {
			dev_err(&uac2->pdev.dev,
				"%s:%d Error!\n", __func__, __LINE__);
			req->status = 0;
		}
	}

	return value;
}

static inline struct f_uac2_opts *to_f_uac2_opts(struct config_item *item)
{
	return container_of(to_config_group(item), struct f_uac2_opts,
			    func_inst.group);
}

CONFIGFS_ATTR_STRUCT(f_uac2_opts);
CONFIGFS_ATTR_OPS(f_uac2_opts);

static void f_uac2_attr_release(struct config_item *item)
{
	struct f_uac2_opts *opts = to_f_uac2_opts(item);

	usb_put_function_instance(&opts->func_inst);
}

static struct configfs_item_operations f_uac2_item_ops = {
	.release	= f_uac2_attr_release,
	.show_attribute	= f_uac2_opts_attr_show,
	.store_attribute = f_uac2_opts_attr_store,
};

#define UAC2_ATTRIBUTE(name)						\
static ssize_t f_uac2_opts_##name##_show(struct f_uac2_opts *opts,	\
					 char *page)			\
{									\
	int result;							\
									\
	mutex_lock(&opts->lock);					\
	result = sprintf(page, "%u\n", opts->name);			\
	mutex_unlock(&opts->lock);					\
									\
	return result;							\
}									\
									\
static ssize_t f_uac2_opts_##name##_store(struct f_uac2_opts *opts,	\
					  const char *page, size_t len)	\
{									\
	int ret;							\
	u32 num;							\
									\
	mutex_lock(&opts->lock);					\
	if (opts->refcnt) {						\
		ret = -EBUSY;						\
		goto end;						\
	}								\
									\
	ret = kstrtou32(page, 0, &num);					\
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
static struct f_uac2_opts_attribute f_uac2_opts_##name =		\
	__CONFIGFS_ATTR(name, S_IRUGO | S_IWUSR,			\
			f_uac2_opts_##name##_show,			\
			f_uac2_opts_##name##_store)

UAC2_ATTRIBUTE(p_chmask);
UAC2_ATTRIBUTE(p_srate);
UAC2_ATTRIBUTE(p_ssize);
UAC2_ATTRIBUTE(c_chmask);
UAC2_ATTRIBUTE(c_srate);
UAC2_ATTRIBUTE(c_ssize);

static struct configfs_attribute *f_uac2_attrs[] = {
	&f_uac2_opts_p_chmask.attr,
	&f_uac2_opts_p_srate.attr,
	&f_uac2_opts_p_ssize.attr,
	&f_uac2_opts_c_chmask.attr,
	&f_uac2_opts_c_srate.attr,
	&f_uac2_opts_c_ssize.attr,
	NULL,
};

static struct config_item_type f_uac2_func_type = {
	.ct_item_ops	= &f_uac2_item_ops,
	.ct_attrs	= f_uac2_attrs,
	.ct_owner	= THIS_MODULE,
};

static void afunc_free_inst(struct usb_function_instance *f)
{
	struct f_uac2_opts *opts;

	opts = container_of(f, struct f_uac2_opts, func_inst);
	kfree(opts);
}

static struct usb_function_instance *afunc_alloc_inst(void)
{
	struct f_uac2_opts *opts;

	opts = kzalloc(sizeof(*opts), GFP_KERNEL);
	if (!opts)
		return ERR_PTR(-ENOMEM);

	mutex_init(&opts->lock);
	opts->func_inst.free_func_inst = afunc_free_inst;

	config_group_init_type_name(&opts->func_inst.group, "",
				    &f_uac2_func_type);

	opts->p_chmask = UAC2_DEF_PCHMASK;
	opts->p_srate = UAC2_DEF_PSRATE;
	opts->p_ssize = UAC2_DEF_PSSIZE;
	opts->c_chmask = UAC2_DEF_CCHMASK;
	opts->c_srate = UAC2_DEF_CSRATE;
	opts->c_ssize = UAC2_DEF_CSSIZE;
	return &opts->func_inst;
}

static void afunc_free(struct usb_function *f)
{
	struct audio_dev *agdev;
	struct f_uac2_opts *opts;

	agdev = func_to_agdev(f);
	opts = container_of(f->fi, struct f_uac2_opts, func_inst);
	kfree(agdev);
	mutex_lock(&opts->lock);
	--opts->refcnt;
	mutex_unlock(&opts->lock);
}

static void afunc_unbind(struct usb_configuration *c, struct usb_function *f)
{
	struct audio_dev *agdev = func_to_agdev(f);
	struct uac2_rtd_params *prm;

	alsa_uac2_exit(agdev);

	prm = &agdev->uac2.p_prm;
	kfree(prm->rbuf);

	prm = &agdev->uac2.c_prm;
	kfree(prm->rbuf);
	usb_free_all_descriptors(f);

	if (agdev->in_ep)
		agdev->in_ep->driver_data = NULL;
	if (agdev->out_ep)
		agdev->out_ep->driver_data = NULL;
}

static struct usb_function *afunc_alloc(struct usb_function_instance *fi)
{
	struct audio_dev *agdev;
	struct f_uac2_opts *opts;

	agdev = kzalloc(sizeof(*agdev), GFP_KERNEL);
	if (agdev == NULL)
		return ERR_PTR(-ENOMEM);

	opts = container_of(fi, struct f_uac2_opts, func_inst);
	mutex_lock(&opts->lock);
	++opts->refcnt;
	mutex_unlock(&opts->lock);

	agdev->func.name = "uac2_func";
	agdev->func.bind = afunc_bind;
	agdev->func.unbind = afunc_unbind;
	agdev->func.set_alt = afunc_set_alt;
	agdev->func.get_alt = afunc_get_alt;
	agdev->func.disable = afunc_disable;
	agdev->func.setup = afunc_setup;
	agdev->func.free_func = afunc_free;

	return &agdev->func;
}

DECLARE_USB_FUNCTION_INIT(uac2, afunc_alloc_inst, afunc_alloc);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Yadwinder Singh");
MODULE_AUTHOR("Jaswinder Singh");
