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
#include <linux/pm_qos.h>
#include <sound/core.h>
#include <sound/initval.h>
#include <sound/pcm.h>

#include <linux/usb.h>
#include <linux/usb_usual.h>
#include <linux/usb/ch9.h>
#include <linux/configfs.h>
#include <linux/usb/composite.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#define DEFAULT_SAMPLE_RATE 44100
#define MAX_SAMPLE_RATE		96000
#define MIN_SAMPLE_RATE		8000
#define DEFAULT_FRAMES_PER_MSEC (DEFAULT_SAMPLE_RATE / 1000)

#define IN_EP_MAX_PACKET_SIZE 256

/* Number of requests to allocate */
#define IN_EP_REQ_COUNT 4

#define AUDIO_AC_INTERFACE	0
#define AUDIO_AS_INTERFACE	1
#define AUDIO_NUM_INTERFACES	1
#define MAX_INST_NAME_LEN     40

static int audio_set_cmd(struct usb_audio_control *con, u8 cmd, int value);
static int audio_get_cmd(struct usb_audio_control *con, u8 cmd);

static u32 sample_rate_table[] = {
8000, 11025, 16000, 22050, 24000,
32000, 40000, 44100, 48000, 56000,
64000, 72000, 80000, 88200, 96000,
};

/* B.3.1  Standard AC Interface Descriptor */
static struct usb_interface_descriptor ac_interface_desc = {
	.bLength =		USB_DT_INTERFACE_SIZE,
	.bDescriptorType =	USB_DT_INTERFACE,
	.bNumEndpoints =	0,
	.bInterfaceClass =	USB_CLASS_AUDIO,
	.bInterfaceSubClass =	USB_SUBCLASS_AUDIOCONTROL,
};

DECLARE_UAC_AC_HEADER_DESCRIPTOR(1);

#define UAC_DT_AC_HEADER_LENGTH	UAC_DT_AC_HEADER_SIZE(AUDIO_NUM_INTERFACES)
/* 1 input terminal, 1 output terminal and 1 feature unit */
#define UAC_DT_TOTAL_LENGTH (UAC_DT_AC_HEADER_LENGTH \
	+ UAC_DT_INPUT_TERMINAL_SIZE + UAC_DT_OUTPUT_TERMINAL_SIZE \
	+ UAC_DT_FEATURE_UNIT_SIZE(0))
/* B.3.2  Class-Specific AC Interface Descriptor */
static struct uac1_ac_header_descriptor_1 ac_header_desc = {
	.bLength =		UAC_DT_AC_HEADER_LENGTH,
	.bDescriptorType =	USB_DT_CS_INTERFACE,
	.bDescriptorSubtype =	UAC_HEADER,
	.bcdADC =		__constant_cpu_to_le16(0x0100),
	.wTotalLength =		__constant_cpu_to_le16(UAC_DT_TOTAL_LENGTH),
	.bInCollection =	AUDIO_NUM_INTERFACES,
	.baInterfaceNr = {
		[0] =		AUDIO_AS_INTERFACE,
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
	.wChannelConfig =	0,
};

DECLARE_UAC_FEATURE_UNIT_DESCRIPTOR(0);

#define FEATURE_UNIT_ID		2
static struct uac_feature_unit_descriptor_0 feature_unit_desc = {
	.bLength		= UAC_DT_FEATURE_UNIT_SIZE(0),
	.bDescriptorType	= USB_DT_CS_INTERFACE,
	.bDescriptorSubtype	= UAC_FEATURE_UNIT,
	.bUnitID		= FEATURE_UNIT_ID,
	.bSourceID		= INPUT_TERMINAL_ID,
	.bControlSize		= 1,
	.bmaControls[0]		= (UAC_FU_MUTE | UAC_FU_VOLUME),
};

static struct usb_audio_control mute_control = {
	.list = LIST_HEAD_INIT(mute_control.list),
	.name = "Mute Control",
	.type = UAC_FU_MUTE,
	/* Todo: add real Mute control code */
	.set = audio_set_cmd,
	.get = audio_get_cmd,
};

static struct usb_audio_control volume_control = {
	.list = LIST_HEAD_INIT(volume_control.list),
	.name = "Volume Control",
	.type = UAC_FU_VOLUME,
	/* Todo: add real Volume control code */
	.set = audio_set_cmd,
	.get = audio_get_cmd,
};

static struct usb_audio_control_selector feature_unit = {
	.list = LIST_HEAD_INIT(feature_unit.list),
	.id = FEATURE_UNIT_ID,
	.name = "Mute & Volume Control",
	.type = UAC_FEATURE_UNIT,
	.desc = (struct usb_descriptor_header *)&feature_unit_desc,
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
	.bTerminalLink =	OUTPUT_TERMINAL_ID,
	.bDelay =		1,
	.wFormatTag =		UAC_FORMAT_TYPE_I_PCM,
};

DECLARE_UAC_FORMAT_TYPE_I_DISCRETE_DESC(15);

static struct uac_format_type_i_discrete_descriptor_15 as_type_i_desc = {
	.bLength =		UAC_FORMAT_TYPE_I_DISCRETE_DESC_SIZE(15),
	.bDescriptorType =	USB_DT_CS_INTERFACE,
	.bDescriptorSubtype =	UAC_FORMAT_TYPE,
	.bFormatType =		UAC_FORMAT_TYPE_I,
	.bSubframeSize =	2,
	.bBitResolution =	16,
	.bSamFreqType =		ARRAY_SIZE(sample_rate_table),
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
	.rate_min		= MIN_SAMPLE_RATE,
	.rate_max		= MAX_SAMPLE_RATE,

	.buffer_bytes_max =	1024 * 1024,
	.period_bytes_min =	64,
	.period_bytes_max =	512 * 1024,
	.periods_min =		2,
	.periods_max =		1024,
};

/*-------------------------------------------------------------------------*/

static u16 g_w_value;
static u8 g_bRequest;

struct audio_source_config {
	int	card;
	int	device;
};

static struct class *audio_source_class;

struct audio_dev {
	struct device			*dev;
	struct usb_function		func;
	struct snd_card			*card;
	struct snd_pcm			*pcm;
	struct snd_pcm_substream *substream;

	struct list_head		idle_reqs;
	struct usb_ep			*in_ep;

	struct work_struct		work;

	spinlock_t			lock;

	/* beginning, end and current position in our buffer */
	void				*buffer_start;
	void				*buffer_end;
	void				*buffer_pos;

	unsigned int			alt;

	/* Control Set command */
	u8 set_cmd;
	struct list_head cs;
	struct usb_audio_control *set_con;

	/* byte size of a "period" */
	unsigned int			period;
	/* bytes sent since last call to snd_pcm_period_elapsed */
	unsigned int			period_offset;
	/* time we started playing */
	ktime_t				start_time;
	/* number of frames sent since start_time */
	s64				frames_sent;

	/* number of frames sent per millisecond */
	s64				frames_per_msec;

	/* current sample rate */
	s64				sample_rate;

	struct audio_source_config	*config;
	/* for creating and issuing QoS requests */
	struct pm_qos_request pm_qos;
};

static inline struct audio_dev *func_to_audio(struct usb_function *f)
{
	return container_of(f, struct audio_dev, func);
}

static void audio_source_work(struct work_struct *data)
{
	struct audio_dev *audio = container_of(data, struct audio_dev, work);
	char buffer[64];
	char *set_interface[4]	= { "USB_STATE=SET_INTERFACE", NULL, NULL,
				    NULL };
	char **uevent_envp = NULL;

	if (audio->alt)
		set_interface[1] = "STREAM_STATE=ON";
	else
		set_interface[1] = "STREAM_STATE=OFF";

	sprintf(buffer, "SAMPLE_RATE=%lld", audio->sample_rate);
		set_interface[2] = buffer;

	uevent_envp = set_interface;

	if (uevent_envp) {
		kobject_uevent_env(&audio->dev->kobj, KOBJ_CHANGE,
				   uevent_envp);
		pr_info("%s: sent uevent %s\n", __func__,
			uevent_envp[0]);
	} else {
		pr_info("%s: did not send uevent set interface\n",
			__func__);
	}
}

static ssize_t
alt_show(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct audio_dev *audio = dev_get_drvdata(dev);

	return sprintf(buf, "%d\n", audio->alt);
}

static ssize_t
alt_store(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t size)
{
	unsigned int value;
	struct audio_dev *audio = dev_get_drvdata(dev);

	if (sscanf(buf, "%d\n", &value) != 1)
		return -1;

	if (audio->alt != value) {
		audio->alt = value;
		schedule_work(&audio->work);
	}
	return size;
}
static DEVICE_ATTR_RW(alt);

static struct device_attribute *audio_source_attributes[] = {
	&dev_attr_alt,
	NULL
};

static int audio_source_create_device(struct audio_dev *audio)
{
	struct device_attribute **attrs = audio_source_attributes;
	struct device_attribute *attr;
	int err;

	audio->dev = device_create(audio_source_class, NULL,
				   MKDEV(0, 0), NULL, "audio_source0");
	if (IS_ERR(audio->dev))
		return PTR_ERR(audio->dev);

	dev_set_drvdata(audio->dev, audio);

	while ((attr = *attrs++)) {
		err = device_create_file(audio->dev, attr);
		if (err) {
			device_destroy(audio_source_class, audio->dev->devt);
			return err;
		}
	}
	return 0;
}

/*-------------------------------------------------------------------------*/

struct audio_source_instance {
	struct usb_function_instance func_inst;
	const char *name;
	struct audio_source_config *config;
	struct device *audio_device;
};

static void audio_source_attr_release(struct config_item *item);

static struct configfs_item_operations audio_source_item_ops = {
	.release        = audio_source_attr_release,
};

static struct config_item_type audio_source_func_type = {
	.ct_item_ops    = &audio_source_item_ops,
	.ct_owner       = THIS_MODULE,
};

static ssize_t audio_source_pcm_show(struct device *dev,
		struct device_attribute *attr, char *buf);

static DEVICE_ATTR(pcm, S_IRUGO, audio_source_pcm_show, NULL);

static struct device_attribute *audio_source_function_attributes[] = {
	&dev_attr_pcm,
	NULL
};

/*--------------------------------------------------------------------------*/

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

static int audio_set_cmd(struct usb_audio_control *con, u8 cmd, int value)
{
	con->data[cmd] = value;

	return 0;
}

static int audio_get_cmd(struct usb_audio_control *con, u8 cmd)
{
	return con->data[cmd];
}

/* Todo: add more control selecotor dynamically */
static int control_selector_init(struct audio_dev *audio)
{
	INIT_LIST_HEAD(&audio->cs);
	list_add(&feature_unit.list, &audio->cs);

	INIT_LIST_HEAD(&feature_unit.control);
	list_add(&mute_control.list, &feature_unit.control);
	list_add(&volume_control.list, &feature_unit.control);

	volume_control.data[UAC__CUR] = 0xffc0;
	volume_control.data[UAC__MIN] = 0xe3a0;
	volume_control.data[UAC__MAX] = 0xfff0;
	volume_control.data[UAC__RES] = 0x0030;

	return 0;
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
	msecs = div_s64((ktime_to_ns(now) - ktime_to_ns(audio->start_time)),
			1000000);
	frames = div_s64((msecs * audio->sample_rate), 1000);

	/* Readjust our frames_sent if we fall too far behind.
	 * If we get too far behind it is better to drop some frames than
	 * to keep sending data too fast in an attempt to catch up.
	 */
	if (frames - audio->frames_sent > 10 * audio->frames_per_msec)
		audio->frames_sent = frames - audio->frames_per_msec;

	frames -= audio->frames_sent;

	/* We need to send something to keep the pipeline going */
	if (frames <= 0)
		frames = audio->frames_per_msec;

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
	struct audio_dev *audio = req->context;
	u8 *buf = req->buf;
	u32 data = 0;

	pr_debug("audio_control_complete req->status %d req->actual %d\n",
		 req->status, req->actual);

	if (req->status)
		return;

	if (g_w_value == UAC_EP_CS_ATTR_SAMPLE_RATE << 8) {
		switch (g_bRequest) {
		case UAC_SET_CUR:
			g_w_value = 0;
			g_bRequest = 0;

			audio->sample_rate = (s64)buf[0];
			audio->sample_rate += ((s64)buf[1]) << 8;
			audio->sample_rate += ((s64)buf[2]) << 16;
			audio->frames_per_msec = audio->sample_rate;
			do_div(audio->frames_per_msec, 1000);
			pr_info("audio source set sample rate to %lld\n",
				audio->sample_rate);
			break;
		case UAC_SET_MIN:
			/* fallthrough */
		case UAC_SET_MAX:
			/* fallthrough */
		case UAC_SET_RES:
			/* fallthrough */
			break;
		default:
			break;
		}
	}
	if (audio->set_con) {
		memcpy(&data, req->buf, req->length);
		audio->set_con->set(audio->set_con, audio->set_cmd,
				le16_to_cpu(data));
		audio->set_con = NULL;
	}
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
	if (audio->alt)
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
		if (w_value == UAC_EP_CS_ATTR_SAMPLE_RATE << 8) {
			g_w_value = w_value;
			g_bRequest = ctrl->bRequest;
		}
		/* fallthrough */
	case UAC_SET_MIN:
		/* fallthrough */
	case UAC_SET_MAX:
		/* fallthrough */
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
	struct audio_dev *audio = func_to_audio(f);
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
			buf[0] = (u8)audio->sample_rate;
			buf[1] = (u8)(audio->sample_rate >> 8);
			buf[2] = (u8)(audio->sample_rate >> 16);
			value = 3;
			break;

		case UAC_GET_MIN:
			buf[0] = (u8)MIN_SAMPLE_RATE;
			buf[1] = (u8)(MIN_SAMPLE_RATE >> 8);
			buf[2] = (u8)(MIN_SAMPLE_RATE >> 16);
			value = 3;
			break;

		case UAC_GET_MAX:
			buf[0] = (u8)MAX_SAMPLE_RATE;
			buf[1] = (u8)(MAX_SAMPLE_RATE >> 8);
			buf[2] = (u8)(MAX_SAMPLE_RATE >> 16);
			value = 3;
			break;

		case UAC_GET_RES:
			/* return our sample rate */
			buf[0] = (u8)MAX_SAMPLE_RATE;
			buf[1] = (u8)(MAX_SAMPLE_RATE >> 8);
			buf[2] = (u8)(MAX_SAMPLE_RATE >> 16);
			value = 3;
			break;
		default:
			break;
		}
	}

	return value;
}

static int audio_set_intf_req(struct usb_function *f,
		const struct usb_ctrlrequest *ctrl)
{
	struct audio_dev	*audio = func_to_audio(f);
	struct usb_composite_dev *cdev = f->config->cdev;
	u8			id = ((le16_to_cpu(ctrl->wIndex) >> 8) & 0xFF);
	u16			len = le16_to_cpu(ctrl->wLength);
	u16			w_value = le16_to_cpu(ctrl->wValue);
	u8			con_sel = (w_value >> 8) & 0xFF;
	u8			cmd = (ctrl->bRequest & 0x0F);
	struct usb_audio_control_selector *cs;
	struct usb_audio_control *con;

	DBG(cdev, "bRequest 0x%x, w_value 0x%04x, len %d, entity %d\n",
			ctrl->bRequest, w_value, len, id);

	list_for_each_entry(cs, &audio->cs, list) {
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

	return len;
}

static int audio_get_intf_req(struct usb_function *f,
		const struct usb_ctrlrequest *ctrl)
{
	struct audio_dev	*audio = func_to_audio(f);
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

	list_for_each_entry(cs, &audio->cs, list) {
		if (cs->id == id) {
			list_for_each_entry(con, &cs->control, list) {
				if (con->type == con_sel && con->get) {
					value = con->get(con, cmd);
					break;
				}
			}
		}
	}

	len = min_t(size_t, sizeof(value), len);
	memcpy(req->buf, &value, len);

	return len;
}

static int
audio_setup(struct usb_function *f, const struct usb_ctrlrequest *ctrl)
{
	struct audio_dev *audio = func_to_audio(f);
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

	case USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE:
		value = audio_set_intf_req(f, ctrl);
		break;

	case USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_INTERFACE:
		value = audio_get_intf_req(f, ctrl);
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
		req->context = audio;
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
	struct usb_composite_dev *cdev = f->config->cdev;
	int ret;

	pr_debug("audio_set_alt intf %d, alt %d\n", intf, alt);

	ret = config_ep_by_speed(cdev->gadget, f, audio->in_ep);
	if (ret)
		return ret;

	if (audio->alt != alt) {
		audio->alt = alt;
		schedule_work(&audio->work);
	}

	usb_ep_enable(audio->in_ep);
	return 0;
}

static int audio_get_alt(struct usb_function *f, unsigned intf)
{
	struct audio_dev *audio = func_to_audio(f);

	pr_debug("%s intf %d, alt %d\n", __func__, intf, audio->alt);

	return audio->alt;
}

static void audio_disable(struct usb_function *f)
{
	struct audio_dev	*audio = func_to_audio(f);

	audio->alt = 0;
	schedule_work(&audio->work);

	pr_debug("audio_disable\n");
	usb_ep_disable(audio->in_ep);
}

static void audio_free_func(struct usb_function *f)
{
	/* no-op */
}

/*-------------------------------------------------------------------------*/

static void audio_build_desc(struct audio_dev *audio)
{
	u8 *sam_freq;
	u32 rate, i;

	/* Set channel numbers */
	input_terminal_desc.bNrChannels = 2;
	as_type_i_desc.bNrChannels = 2;

	/* Set sample rates */
	for (i = 0; i < ARRAY_SIZE(sample_rate_table); i++) {
		rate = sample_rate_table[i];
		sam_freq = as_type_i_desc.tSamFreq[i];
		memcpy(sam_freq, &rate, 3);
	}
}


static int snd_card_setup(struct usb_configuration *c,
	struct audio_source_config *config);
static struct audio_source_instance *to_fi_audio_source(
	const struct usb_function_instance *fi);


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
	int err;

	if (IS_ENABLED(CONFIG_USB_CONFIGFS)) {
		struct audio_source_instance *fi_audio =
				to_fi_audio_source(f->fi);
		struct audio_source_config *config =
				fi_audio->config;

		audio->alt = 0;
		audio->sample_rate = DEFAULT_SAMPLE_RATE;
		audio->frames_per_msec = DEFAULT_FRAMES_PER_MSEC;

		control_selector_init(audio);

		err = snd_card_setup(c, config);
		if (err)
			return err;

		audio_source_class = class_create(THIS_MODULE, "audio_source");
		if (IS_ERR(audio_source_class))
			return PTR_ERR(audio_source_class);

		INIT_WORK(&audio->work, audio_source_work);
		audio_source_create_device(audio);
	}

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

	/* AUDIO_AS_INTERFACE */
	ac_header_desc.baInterfaceNr[0] = status;

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

	f->fs_descriptors = fs_audio_desc;
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

	cancel_work_sync(&audio->work);
	device_destroy(audio_source_class, audio->dev->devt);
	class_destroy(audio_source_class);

	snd_card_free_when_closed(audio->card);
	audio->card = NULL;
	audio->pcm = NULL;
	audio->substream = NULL;
	audio->in_ep = NULL;
	audio->dev = NULL;

	if (IS_ENABLED(CONFIG_USB_CONFIGFS)) {
		struct audio_source_instance *fi_audio =
				to_fi_audio_source(f->fi);
		struct audio_source_config *config =
				fi_audio->config;

		config->card = -1;
		config->device = -1;
	}
}

static void audio_pcm_playback_start(struct audio_dev *audio)
{
	if (audio->alt) {
		audio->start_time = ktime_get();
		audio->frames_sent = 0;
		audio_send(audio);
	}
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

	/* Add the QoS request and set the latency to 0 */
	pm_qos_add_request(&audio->pm_qos, PM_QOS_CPU_DMA_LATENCY, 0);

	return 0;
}

static int audio_pcm_close(struct snd_pcm_substream *substream)
{
	struct audio_dev *audio = substream->private_data;
	unsigned long flags;

	spin_lock_irqsave(&audio->lock, flags);

	/* Remove the QoS request */
	pm_qos_remove_request(&audio->pm_qos);

	audio->substream = NULL;
	spin_unlock_irqrestore(&audio->lock, flags);

	return 0;
}

static int audio_pcm_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params)
{
	unsigned int channels = params_channels(params);
	unsigned int rate = params_rate(params);

	if (rate > MAX_SAMPLE_RATE || rate < MIN_SAMPLE_RATE)
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
		.get_alt = audio_get_alt,
		.setup = audio_setup,
		.disable = audio_disable,
		.free_func = audio_free_func,
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

#ifndef CONFIG_USB_CONFIGFS
int audio_source_bind_config(struct usb_configuration *c,
		struct audio_source_config *config)
{
	struct audio_dev *audio;
	int err;

	config->card = -1;
	config->device = -1;

	audio = &_audio_dev;

	audio->alt = 0;
	audio->sample_rate = DEFAULT_SAMPLE_RATE;
	audio->frames_per_msec = DEFAULT_FRAMES_PER_MSEC;

	err = snd_card_setup(c, config);
	if (err)
		return err;

	control_selector_init(audio);

	err = usb_add_function(c, &audio->func);
	if (err)
		goto add_fail;

	audio_source_class = class_create(THIS_MODULE, "audio_source");
	if (IS_ERR(audio_source_class))
		return PTR_ERR(audio_source_class);

	INIT_WORK(&audio->work, audio_source_work);
	audio_source_create_device(audio);

	return 0;

add_fail:
	snd_card_free(audio->card);
	return err;
}
#endif

static int snd_card_setup(struct usb_configuration *c,
		struct audio_source_config *config)
{
	struct audio_dev *audio;
	struct snd_card *card;
	struct snd_pcm *pcm;
	int err;

	audio = &_audio_dev;

	err = snd_card_new(&c->cdev->gadget->dev,
			SNDRV_DEFAULT_IDX1, SNDRV_DEFAULT_STR1,
			THIS_MODULE, 0, &card);
	if (err)
		return err;

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

	config->card = pcm->card->number;
	config->device = pcm->device;
	audio->card = card;
	return 0;

register_fail:
pcm_fail:
	snd_card_free(audio->card);
	return err;
}

static struct audio_source_instance *to_audio_source_instance(
					struct config_item *item)
{
	return container_of(to_config_group(item), struct audio_source_instance,
		func_inst.group);
}

static struct audio_source_instance *to_fi_audio_source(
					const struct usb_function_instance *fi)
{
	return container_of(fi, struct audio_source_instance, func_inst);
}

static void audio_source_attr_release(struct config_item *item)
{
	struct audio_source_instance *fi_audio = to_audio_source_instance(item);

	usb_put_function_instance(&fi_audio->func_inst);
}

static int audio_source_set_inst_name(struct usb_function_instance *fi,
					const char *name)
{
	struct audio_source_instance *fi_audio;
	char *ptr;
	int name_len;

	name_len = strlen(name) + 1;
	if (name_len > MAX_INST_NAME_LEN)
		return -ENAMETOOLONG;

	ptr = kstrndup(name, name_len, GFP_KERNEL);
	if (!ptr)
		return -ENOMEM;

	fi_audio = to_fi_audio_source(fi);
	fi_audio->name = ptr;

	return 0;
}

static void audio_source_free_inst(struct usb_function_instance *fi)
{
	struct audio_source_instance *fi_audio;

	fi_audio = to_fi_audio_source(fi);
	device_destroy(fi_audio->audio_device->class,
			fi_audio->audio_device->devt);
	kfree(fi_audio->name);
	kfree(fi_audio->config);
}

static ssize_t audio_source_pcm_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct audio_source_instance *fi_audio = dev_get_drvdata(dev);
	struct audio_source_config *config = fi_audio->config;

	/* print PCM card and device numbers */
	return sprintf(buf, "%d %d\n", config->card, config->device);
}

struct device *create_function_device(char *name);

static struct usb_function_instance *audio_source_alloc_inst(void)
{
	struct audio_source_instance *fi_audio;
	struct device_attribute **attrs;
	struct device_attribute *attr;
	struct device *dev;
	void *err_ptr;
	int err = 0;

	fi_audio = kzalloc(sizeof(*fi_audio), GFP_KERNEL);
	if (!fi_audio)
		return ERR_PTR(-ENOMEM);

	fi_audio->func_inst.set_inst_name = audio_source_set_inst_name;
	fi_audio->func_inst.free_func_inst = audio_source_free_inst;

	fi_audio->config = kzalloc(sizeof(struct audio_source_config),
							GFP_KERNEL);
	if (!fi_audio->config) {
		err_ptr = ERR_PTR(-ENOMEM);
		goto fail_audio;
	}

	config_group_init_type_name(&fi_audio->func_inst.group, "",
						&audio_source_func_type);
	dev = create_function_device("f_audio_source");

	if (IS_ERR(dev)) {
		err_ptr = dev;
		goto fail_audio_config;
	}

	fi_audio->config->card = -1;
	fi_audio->config->device = -1;
	fi_audio->audio_device = dev;

	attrs = audio_source_function_attributes;
	if (attrs) {
		while ((attr = *attrs++) && !err)
			err = device_create_file(dev, attr);
		if (err) {
			err_ptr = ERR_PTR(-EINVAL);
			goto fail_device;
		}
	}

	dev_set_drvdata(dev, fi_audio);
	_audio_dev.config = fi_audio->config;

	return  &fi_audio->func_inst;

fail_device:
	device_destroy(dev->class, dev->devt);
fail_audio_config:
	kfree(fi_audio->config);
fail_audio:
	kfree(fi_audio);
	return err_ptr;

}

static struct usb_function *audio_source_alloc(struct usb_function_instance *fi)
{
	return &_audio_dev.func;
}

DECLARE_USB_FUNCTION_INIT(audio_source, audio_source_alloc_inst,
			audio_source_alloc);
MODULE_LICENSE("GPL");
