/*
 * Gadget Function Driver for USB audio source device
 *
 * Copyright (C) 2012 Google, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/device.h>
#include <linux/usb/audio.h>
#include <linux/wait.h>
#include <sound/core.h>
#include <sound/initval.h>
#include <sound/pcm.h>

#define SAMPLE_RATE 44100
#define FRAMES_PER_MSEC (SAMPLE_RATE / 1000)

#define IN_EP_MAX_PACKET_SIZE 384

/* Number of requests to allocate */
#define IN_EP_REQ_COUNT 4

#define AUDIO_AC_INTERFACE	0
#define AUDIO_AS_INTERFACE	1
#define AUDIO_NUM_INTERFACES	2

/* B.3.1  Standard AC Interface Descriptor */
static struct usb_interface_descriptor ac_interface_desc = {
	.bLength =		USB_DT_INTERFACE_SIZE,
	.bDescriptorType =	USB_DT_INTERFACE,
	.bNumEndpoints =	0,
	.bInterfaceClass =	USB_CLASS_AUDIO,
	.bInterfaceSubClass =	USB_SUBCLASS_AUDIOCONTROL,
};

DECLARE_UAC_AC_HEADER_DESCRIPTOR(2);

#define UAC_DT_AC_HEADER_LENGTH	UAC_DT_AC_HEADER_SIZE(AUDIO_NUM_INTERFACES)
/* 1 input terminal, 1 output terminal and 1 feature unit */
#define UAC_DT_TOTAL_LENGTH (UAC_DT_AC_HEADER_LENGTH \
	+ UAC_DT_INPUT_TERMINAL_SIZE + UAC_DT_OUTPUT_TERMINAL_SIZE \
	+ UAC_DT_FEATURE_UNIT_SIZE(0))
/* B.3.2  Class-Specific AC Interface Descriptor */
static struct uac1_ac_header_descriptor_2 ac_header_desc = {
	.bLength =		UAC_DT_AC_HEADER_LENGTH,
	.bDescriptorType =	USB_DT_CS_INTERFACE,
	.bDescriptorSubtype =	UAC_HEADER,
	.bcdADC =		__constant_cpu_to_le16(0x0100),
	.wTotalLength =		__constant_cpu_to_le16(UAC_DT_TOTAL_LENGTH),
	.bInCollection =	AUDIO_NUM_INTERFACES,
	.baInterfaceNr = {
		[0] =		AUDIO_AC_INTERFACE,
		[1] =		AUDIO_AS_INTERFACE,
	}
};

#define INPUT_TERMINAL_ID	1
static struct uac_input_terminal_descriptor input_terminal_desc = {
	.bLength =		UAC_DT_INPUT_TERMINAL_SIZE,
	.bDescriptorType =	USB_DT_CS_INTERFACE,
	.bDescriptorSubtype =	UAC_INPUT_TERMINAL,
	.bTerminalID =		INPUT_TERMINAL_ID,
	.wTerminalType =	UAC_INPUT_TERMINAL_MICROPHONE,
	.bAssocTerminal =	0,
	.wChannelConfig =	0x3,
};

DECLARE_UAC_FEATURE_UNIT_DESCRIPTOR(0);

#define FEATURE_UNIT_ID		2
static struct uac_feature_unit_descriptor_0 feature_unit_desc = {
	.bLength		= UAC_DT_FEATURE_UNIT_SIZE(0),
	.bDescriptorType	= USB_DT_CS_INTERFACE,
	.bDescriptorSubtype	= UAC_FEATURE_UNIT,
	.bUnitID		= FEATURE_UNIT_ID,
	.bSourceID		= INPUT_TERMINAL_ID,
	.bControlSize		= 2,
};

#define OUTPUT_TERMINAL_ID	3
static struct uac1_output_terminal_descriptor output_terminal_desc = {
	.bLength		= UAC_DT_OUTPUT_TERMINAL_SIZE,
	.bDescriptorType	= USB_DT_CS_INTERFACE,
	.bDescriptorSubtype	= UAC_OUTPUT_TERMINAL,
	.bTerminalID		= OUTPUT_TERMINAL_ID,
	.wTerminalType		= UAC_TERMINAL_STREAMING,
	.bAssocTerminal		= FEATURE_UNIT_ID,
	.bSourceID		= FEATURE_UNIT_ID,
};

/* B.4.1  Standard AS Interface Descriptor */
static struct usb_interface_descriptor as_interface_alt_0_desc = {
	.bLength =		USB_DT_INTERFACE_SIZE,
	.bDescriptorType =	USB_DT_INTERFACE,
	.bAlternateSetting =	0,
	.bNumEndpoints =	0,
	.bInterfaceClass =	USB_CLASS_AUDIO,
	.bInterfaceSubClass =	USB_SUBCLASS_AUDIOSTREAMING,
};

static struct usb_interface_descriptor as_interface_alt_1_desc = {
	.bLength =		USB_DT_INTERFACE_SIZE,
	.bDescriptorType =	USB_DT_INTERFACE,
	.bAlternateSetting =	1,
	.bNumEndpoints =	1,
	.bInterfaceClass =	USB_CLASS_AUDIO,
	.bInterfaceSubClass =	USB_SUBCLASS_AUDIOSTREAMING,
};

/* B.4.2  Class-Specific AS Interface Descriptor */
static struct uac1_as_header_descriptor as_header_desc = {
	.bLength =		UAC_DT_AS_HEADER_SIZE,
	.bDescriptorType =	USB_DT_CS_INTERFACE,
	.bDescriptorSubtype =	UAC_AS_GENERAL,
	.bTerminalLink =	INPUT_TERMINAL_ID,
	.bDelay =		1,
	.wFormatTag =		UAC_FORMAT_TYPE_I_PCM,
};

DECLARE_UAC_FORMAT_TYPE_I_DISCRETE_DESC(1);

static struct uac_format_type_i_discrete_descriptor_1 as_type_i_desc = {
	.bLength =		UAC_FORMAT_TYPE_I_DISCRETE_DESC_SIZE(1),
	.bDescriptorType =	USB_DT_CS_INTERFACE,
	.bDescriptorSubtype =	UAC_FORMAT_TYPE,
	.bFormatType =		UAC_FORMAT_TYPE_I,
	.bSubframeSize =	2,
	.bBitResolution =	16,
	.bSamFreqType =		1,
};

/* Standard ISO IN Endpoint Descriptor for highspeed */
static struct usb_endpoint_descriptor hs_as_in_ep_desc  = {
	.bLength =		USB_DT_ENDPOINT_AUDIO_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,
	.bEndpointAddress =	USB_DIR_IN,
	.bmAttributes =		USB_ENDPOINT_SYNC_SYNC
				| USB_ENDPOINT_XFER_ISOC,
	.wMaxPacketSize =	__constant_cpu_to_le16(IN_EP_MAX_PACKET_SIZE),
	.bInterval =		4, /* poll 1 per millisecond */
};

/* Standard ISO IN Endpoint Descriptor for highspeed */
static struct usb_endpoint_descriptor fs_as_in_ep_desc  = {
	.bLength =		USB_DT_ENDPOINT_AUDIO_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,
	.bEndpointAddress =	USB_DIR_IN,
	.bmAttributes =		USB_ENDPOINT_SYNC_SYNC
				| USB_ENDPOINT_XFER_ISOC,
	.wMaxPacketSize =	__constant_cpu_to_le16(IN_EP_MAX_PACKET_SIZE),
	.bInterval =		1, /* poll 1 per millisecond */
};

/* Class-specific AS ISO OUT Endpoint Descriptor */
static struct uac_iso_endpoint_descriptor as_iso_in_desc = {
	.bLength =		UAC_ISO_ENDPOINT_DESC_SIZE,
	.bDescriptorType =	USB_DT_CS_ENDPOINT,
	.bDescriptorSubtype =	UAC_EP_GENERAL,
	.bmAttributes =		1,
	.bLockDelayUnits =	1,
	.wLockDelay =		__constant_cpu_to_le16(1),
};

static struct usb_descriptor_header *hs_audio_desc[] = {
	(struct usb_descriptor_header *)&ac_interface_desc,
	(struct usb_descriptor_header *)&ac_header_desc,

	(struct usb_descriptor_header *)&input_terminal_desc,
	(struct usb_descriptor_header *)&output_terminal_desc,
	(struct usb_descriptor_header *)&feature_unit_desc,

	(struct usb_descriptor_header *)&as_interface_alt_0_desc,
	(struct usb_descriptor_header *)&as_interface_alt_1_desc,
	(struct usb_descriptor_header *)&as_header_desc,

	(struct usb_descriptor_header *)&as_type_i_desc,

	(struct usb_descriptor_header *)&hs_as_in_ep_desc,
	(struct usb_descriptor_header *)&as_iso_in_desc,
	NULL,
};

static struct usb_descriptor_header *fs_audio_desc[] = {
	(struct usb_descriptor_header *)&ac_interface_desc,
	(struct usb_descriptor_header *)&ac_header_desc,

	(struct usb_descriptor_header *)&input_terminal_desc,
	(struct usb_descriptor_header *)&output_terminal_desc,
	(struct usb_descriptor_header *)&feature_unit_desc,

	(struct usb_descriptor_header *)&as_interface_alt_0_desc,
	(struct usb_descriptor_header *)&as_interface_alt_1_desc,
	(struct usb_descriptor_header *)&as_header_desc,

	(struct usb_descriptor_header *)&as_type_i_desc,

	(struct usb_descriptor_header *)&fs_as_in_ep_desc,
	(struct usb_descriptor_header *)&as_iso_in_desc,
	NULL,
};

static struct snd_pcm_hardware audio_hw_info = {
	.info =			SNDRV_PCM_INFO_MMAP |
				SNDRV_PCM_INFO_MMAP_VALID |
				SNDRV_PCM_INFO_BATCH |
				SNDRV_PCM_INFO_INTERLEAVED |
				SNDRV_PCM_INFO_BLOCK_TRANSFER,

	.formats		= SNDRV_PCM_FMTBIT_S16_LE,
	.channels_min		= 2,
	.channels_max		= 2,
	.rate_min		= SAMPLE_RATE,
	.rate_max		= SAMPLE_RATE,

	.buffer_bytes_max =	1024 * 1024,
	.period_bytes_min =	64,
	.period_bytes_max =	512 * 1024,
	.periods_min =		2,
	.periods_max =		1024,
};

/*-------------------------------------------------------------------------*/

struct audio_source_config {
	int	card;
	int	device;
};

struct audio_dev {
	struct usb_function		func;
	struct snd_card			*card;
	struct snd_pcm			*pcm;
	struct snd_pcm_substream *substream;

	struct list_head		idle_reqs;
	struct usb_ep			*in_ep;

	spinlock_t			lock;

	/* beginning, end and current position in our buffer */
	void				*buffer_start;
	void				*buffer_end;
	void				*buffer_pos;

	/* byte size of a "period" */
	unsigned int			period;
	/* bytes sent since last call to snd_pcm_period_elapsed */
	unsigned int			period_offset;
	/* time we started playing */
	ktime_t				start_time;
	/* number of frames sent since start_time */
	s64				frames_sent;
};

static inline struct audio_dev *func_to_audio(struct usb_function *f)
{
	return container_of(f, struct audio_dev, func);
}

/*-------------------------------------------------------------------------*/

static struct usb_request *audio_request_new(struct usb_ep *ep, int buffer_size)
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

static void audio_request_free(struct usb_request *req, struct usb_ep *ep)
{
	if (req) {
		kfree(req->buf);
		usb_ep_free_request(ep, req);
	}
}

static void audio_req_put(struct audio_dev *audio, struct usb_request *req)
{
	unsigned long flags;

	spin_lock_irqsave(&audio->lock, flags);
	list_add_tail(&req->list, &audio->idle_reqs);
	spin_unlock_irqrestore(&audio->lock, flags);
}

static struct usb_request *audio_req_get(struct audio_dev *audio)
{
	unsigned long flags;
	struct usb_request *req;

	spin_lock_irqsave(&audio->lock, flags);
	if (list_empty(&audio->idle_reqs)) {
		req = 0;
	} else {
		req = list_first_entry(&audio->idle_reqs, struct usb_request,
				list);
		list_del(&req->list);
	}
	spin_unlock_irqrestore(&audio->lock, flags);
	return req;
}

/* send the appropriate number of packets to match our bitrate */
static void audio_send(struct audio_dev *audio)
{
	struct snd_pcm_runtime *runtime;
	struct usb_request *req;
	int length, length1, length2, ret;
	s64 msecs;
	s64 frames;
	ktime_t now;

	/* audio->substream will be null if we have been closed */
	if (!audio->substream)
		return;
	/* audio->buffer_pos will be null if we have been stopped */
	if (!audio->buffer_pos)
		return;

	runtime = audio->substream->runtime;

	/* compute number of frames to send */
	now = ktime_get();
	msecs = ktime_to_ns(now) - ktime_to_ns(audio->start_time);
	do_div(msecs, 1000000);
	frames = msecs * SAMPLE_RATE;
	do_div(frames, 1000);

	/* Readjust our frames_sent if we fall too far behind.
	 * If we get too far behind it is better to drop some frames than
	 * to keep sending data too fast in an attempt to catch up.
	 */
	if (frames - audio->frames_sent > 10 * FRAMES_PER_MSEC)
		audio->frames_sent = frames - FRAMES_PER_MSEC;

	frames -= audio->frames_sent;

	/* We need to send something to keep the pipeline going */
	if (frames <= 0)
		frames = FRAMES_PER_MSEC;

	while (frames > 0) {
		req = audio_req_get(audio);
		if (!req)
			break;

		length = frames_to_bytes(runtime, frames);
		if (length > IN_EP_MAX_PACKET_SIZE)
			length = IN_EP_MAX_PACKET_SIZE;

		if (audio->buffer_pos + length > audio->buffer_end)
			length1 = audio->buffer_end - audio->buffer_pos;
		else
			length1 = length;
		memcpy(req->buf, audio->buffer_pos, length1);
		if (length1 < length) {
			/* Wrap around and copy remaining length
			 * at beginning of buffer.
			 */
			length2 = length - length1;
			memcpy(req->buf + length1, audio->buffer_start,
					length2);
			audio->buffer_pos = audio->buffer_start + length2;
		} else {
			audio->buffer_pos += length1;
			if (audio->buffer_pos >= audio->buffer_end)
				audio->buffer_pos = audio->buffer_start;
		}

		req->length = length;
		ret = usb_ep_queue(audio->in_ep, req, GFP_ATOMIC);
		if (ret < 0) {
			pr_err("usb_ep_queue failed ret: %d\n", ret);
			audio_req_put(audio, req);
			break;
		}

		frames -= bytes_to_frames(runtime, length);
		audio->frames_sent += bytes_to_frames(runtime, length);
	}
}

static void audio_control_complete(struct usb_ep *ep, struct usb_request *req)
{
	/* nothing to do here */
}

static void audio_data_complete(struct usb_ep *ep, struct usb_request *req)
{
	struct audio_dev *audio = req->context;

	pr_debug("audio_data_complete req->status %d req->actual %d\n",
		req->status, req->actual);

	audio_req_put(audio, req);

	if (!audio->buffer_start || req->status)
		return;

	audio->period_offset += req->actual;
	if (audio->period_offset >= audio->period) {
		snd_pcm_period_elapsed(audio->substream);
		audio->period_offset = 0;
	}
	audio_send(audio);
}

static int audio_set_endpoint_req(struct usb_function *f,
		const struct usb_ctrlrequest *ctrl)
{
	int value = -EOPNOTSUPP;
	u16 ep = le16_to_cpu(ctrl->wIndex);
	u16 len = le16_to_cpu(ctrl->wLength);
	u16 w_value = le16_to_cpu(ctrl->wValue);

	pr_debug("bRequest 0x%x, w_value 0x%04x, len %d, endpoint %d\n",
			ctrl->bRequest, w_value, len, ep);

	switch (ctrl->bRequest) {
	case UAC_SET_CUR:
	case UAC_SET_MIN:
	case UAC_SET_MAX:
	case UAC_SET_RES:
		value = len;
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
	int value = -EOPNOTSUPP;
	u8 ep = ((le16_to_cpu(ctrl->wIndex) >> 8) & 0xFF);
	u16 len = le16_to_cpu(ctrl->wLength);
	u16 w_value = le16_to_cpu(ctrl->wValue);
	u8 *buf = cdev->req->buf;

	pr_debug("bRequest 0x%x, w_value 0x%04x, len %d, endpoint %d\n",
			ctrl->bRequest, w_value, len, ep);

	if (w_value == UAC_EP_CS_ATTR_SAMPLE_RATE << 8) {
		switch (ctrl->bRequest) {
		case UAC_GET_CUR:
		case UAC_GET_MIN:
		case UAC_GET_MAX:
		case UAC_GET_RES:
			/* return our sample rate */
			buf[0] = (u8)SAMPLE_RATE;
			buf[1] = (u8)(SAMPLE_RATE >> 8);
			buf[2] = (u8)(SAMPLE_RATE >> 16);
			value = 3;
			break;
		default:
			break;
		}
	}

	return value;
}

static int
audio_setup(struct usb_function *f, const struct usb_ctrlrequest *ctrl)
{
	struct usb_composite_dev *cdev = f->config->cdev;
	struct usb_request *req = cdev->req;
	int value = -EOPNOTSUPP;
	u16 w_index = le16_to_cpu(ctrl->wIndex);
	u16 w_value = le16_to_cpu(ctrl->wValue);
	u16 w_length = le16_to_cpu(ctrl->wLength);

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
	}

	/* respond with data transfer or status phase? */
	if (value >= 0) {
		pr_debug("audio req%02x.%02x v%04x i%04x l%d\n",
			ctrl->bRequestType, ctrl->bRequest,
			w_value, w_index, w_length);
		req->zero = 0;
		req->length = value;
		req->complete = audio_control_complete;
		value = usb_ep_queue(cdev->gadget->ep0, req, GFP_ATOMIC);
		if (value < 0)
			pr_err("audio response on err %d\n", value);
	}

	/* device either stalls (value < 0) or reports success */
	return value;
}

static int audio_set_alt(struct usb_function *f, unsigned intf, unsigned alt)
{
	struct audio_dev *audio = func_to_audio(f);

	pr_debug("audio_set_alt intf %d, alt %d\n", intf, alt);
	usb_ep_enable(audio->in_ep, audio->in_desc);
	return 0;
}

static void audio_disable(struct usb_function *f)
{
	struct audio_dev	*audio = func_to_audio(f);

	pr_debug("audio_disable\n");
	usb_ep_disable(audio->in_ep);
}

/*-------------------------------------------------------------------------*/

static void audio_build_desc(struct audio_dev *audio)
{
	u8 *sam_freq;
	int rate;

	/* Set channel numbers */
	input_terminal_desc.bNrChannels = 2;
	as_type_i_desc.bNrChannels = 2;

	/* Set sample rates */
	rate = SAMPLE_RATE;
	sam_freq = as_type_i_desc.tSamFreq[0];
	memcpy(sam_freq, &rate, 3);
}

/* audio function driver setup/binding */
static int
audio_bind(struct usb_configuration *c, struct usb_function *f)
{
	struct usb_composite_dev *cdev = c->cdev;
	struct audio_dev *audio = func_to_audio(f);
	int status;
	struct usb_ep *ep;
	struct usb_request *req;
	int i;

	audio_build_desc(audio);

	/* allocate instance-specific interface IDs, and patch descriptors */
	status = usb_interface_id(c, f);
	if (status < 0)
		goto fail;
	ac_interface_desc.bInterfaceNumber = status;

	status = usb_interface_id(c, f);
	if (status < 0)
		goto fail;
	as_interface_alt_0_desc.bInterfaceNumber = status;
	as_interface_alt_1_desc.bInterfaceNumber = status;

	status = -ENODEV;

	/* allocate our endpoint */
	ep = usb_ep_autoconfig(cdev->gadget, &fs_as_in_ep_desc);
	if (!ep)
		goto fail;
	audio->in_ep = ep;
	ep->driver_data = audio; /* claim */

	if (gadget_is_dualspeed(c->cdev->gadget))
		hs_as_in_ep_desc.bEndpointAddress =
			fs_as_in_ep_desc.bEndpointAddress;

	f->descriptors = fs_audio_desc;
	f->hs_descriptors = hs_audio_desc;

	for (i = 0, status = 0; i < IN_EP_REQ_COUNT && status == 0; i++) {
		req = audio_request_new(ep, IN_EP_MAX_PACKET_SIZE);
		if (req) {
			req->context = audio;
			req->complete = audio_data_complete;
			audio_req_put(audio, req);
		} else
			status = -ENOMEM;
	}

fail:
	return status;
}

static void
audio_unbind(struct usb_configuration *c, struct usb_function *f)
{
	struct audio_dev *audio = func_to_audio(f);
	struct usb_request *req;

	while ((req = audio_req_get(audio)))
		audio_request_free(req, audio->in_ep);

	snd_card_free_when_closed(audio->card);
	audio->card = NULL;
	audio->pcm = NULL;
	audio->substream = NULL;
	audio->in_ep = NULL;
}

static void audio_pcm_playback_start(struct audio_dev *audio)
{
	audio->start_time = ktime_get();
	audio->frames_sent = 0;
	audio_send(audio);
}

static void audio_pcm_playback_stop(struct audio_dev *audio)
{
	unsigned long flags;

	spin_lock_irqsave(&audio->lock, flags);
	audio->buffer_start = 0;
	audio->buffer_end = 0;
	audio->buffer_pos = 0;
	spin_unlock_irqrestore(&audio->lock, flags);
}

static int audio_pcm_open(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct audio_dev *audio = substream->private_data;

	runtime->private_data = audio;
	runtime->hw = audio_hw_info;
	snd_pcm_limit_hw_rates(runtime);
	runtime->hw.channels_max = 2;

	audio->substream = substream;
	return 0;
}

static int audio_pcm_close(struct snd_pcm_substream *substream)
{
	struct audio_dev *audio = substream->private_data;
	unsigned long flags;

	spin_lock_irqsave(&audio->lock, flags);
	audio->substream = NULL;
	spin_unlock_irqrestore(&audio->lock, flags);

	return 0;
}

static int audio_pcm_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params)
{
	unsigned int channels = params_channels(params);
	unsigned int rate = params_rate(params);

	if (rate != SAMPLE_RATE)
		return -EINVAL;
	if (channels != 2)
		return -EINVAL;

	return snd_pcm_lib_alloc_vmalloc_buffer(substream,
		params_buffer_bytes(params));
}

static int audio_pcm_hw_free(struct snd_pcm_substream *substream)
{
	return snd_pcm_lib_free_vmalloc_buffer(substream);
}

static int audio_pcm_prepare(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct audio_dev *audio = runtime->private_data;

	audio->period = snd_pcm_lib_period_bytes(substream);
	audio->period_offset = 0;
	audio->buffer_start = runtime->dma_area;
	audio->buffer_end = audio->buffer_start
		+ snd_pcm_lib_buffer_bytes(substream);
	audio->buffer_pos = audio->buffer_start;

	return 0;
}

static snd_pcm_uframes_t audio_pcm_pointer(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct audio_dev *audio = runtime->private_data;
	ssize_t bytes = audio->buffer_pos - audio->buffer_start;

	/* return offset of next frame to fill in our buffer */
	return bytes_to_frames(runtime, bytes);
}

static int audio_pcm_playback_trigger(struct snd_pcm_substream *substream,
					int cmd)
{
	struct audio_dev *audio = substream->runtime->private_data;
	int ret = 0;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
		audio_pcm_playback_start(audio);
		break;

	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
		audio_pcm_playback_stop(audio);
		break;

	default:
		ret = -EINVAL;
	}

	return ret;
}

static struct audio_dev _audio_dev = {
	.func = {
		.name = "audio_source",
		.bind = audio_bind,
		.unbind = audio_unbind,
		.set_alt = audio_set_alt,
		.setup = audio_setup,
		.disable = audio_disable,
	},
	.lock = __SPIN_LOCK_UNLOCKED(_audio_dev.lock),
	.idle_reqs = LIST_HEAD_INIT(_audio_dev.idle_reqs),
};

static struct snd_pcm_ops audio_playback_ops = {
	.open		= audio_pcm_open,
	.close		= audio_pcm_close,
	.ioctl		= snd_pcm_lib_ioctl,
	.hw_params	= audio_pcm_hw_params,
	.hw_free	= audio_pcm_hw_free,
	.prepare	= audio_pcm_prepare,
	.trigger	= audio_pcm_playback_trigger,
	.pointer	= audio_pcm_pointer,
};

int audio_source_bind_config(struct usb_configuration *c,
		struct audio_source_config *config)
{
	struct audio_dev *audio;
	struct snd_card *card;
	struct snd_pcm *pcm;
	int err;

	config->card = -1;
	config->device = -1;

	audio = &_audio_dev;

	err = snd_card_create(SNDRV_DEFAULT_IDX1, SNDRV_DEFAULT_STR1,
			THIS_MODULE, 0, &card);
	if (err)
		return err;

	snd_card_set_dev(card, &c->cdev->gadget->dev);

	err = snd_pcm_new(card, "USB audio source", 0, 1, 0, &pcm);
	if (err)
		goto pcm_fail;
	pcm->private_data = audio;
	pcm->info_flags = 0;
	audio->pcm = pcm;

	strlcpy(pcm->name, "USB gadget audio", sizeof(pcm->name));

	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &audio_playback_ops);
	snd_pcm_lib_preallocate_pages_for_all(pcm, SNDRV_DMA_TYPE_DEV,
				NULL, 0, 64 * 1024);

	strlcpy(card->driver, "audio_source", sizeof(card->driver));
	strlcpy(card->shortname, card->driver, sizeof(card->shortname));
	strlcpy(card->longname, "USB accessory audio source",
		sizeof(card->longname));

	err = snd_card_register(card);
	if (err)
		goto register_fail;

	err = usb_add_function(c, &audio->func);
	if (err)
		goto add_fail;

	config->card = pcm->card->number;
	config->device = pcm->device;
	audio->card = card;
	return 0;

add_fail:
register_fail:
pcm_fail:
	snd_card_free(audio->card);
	return err;
}
