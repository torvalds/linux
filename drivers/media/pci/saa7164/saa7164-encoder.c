/*
 *  Driver for the NXP SAA7164 PCIe bridge
 *
 *  Copyright (c) 2010-2015 Steven Toth <stoth@kernellabs.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include "saa7164.h"

#define ENCODER_MAX_BITRATE 6500000
#define ENCODER_MIN_BITRATE 1000000
#define ENCODER_DEF_BITRATE 5000000

/*
 * This is a dummy non-zero value for the sizeimage field of v4l2_pix_format.
 * It is not actually used for anything since this driver does not support
 * stream I/O, only read(), and because this driver produces an MPEG stream
 * and not discrete frames. But the V4L2 spec doesn't allow for this value
 * to be 0, so set it to 0x10000 instead.
 *
 * If we ever change this driver to support stream I/O, then this field
 * will be the size of the streaming buffers.
 */
#define SAA7164_SIZEIMAGE (0x10000)

static struct saa7164_tvnorm saa7164_tvnorms[] = {
	{
		.name      = "NTSC-M",
		.id        = V4L2_STD_NTSC_M,
	}, {
		.name      = "NTSC-JP",
		.id        = V4L2_STD_NTSC_M_JP,
	}
};

/* Take the encoder configuration form the port struct and
 * flush it to the hardware.
 */
static void saa7164_encoder_configure(struct saa7164_port *port)
{
	struct saa7164_dev *dev = port->dev;
	dprintk(DBGLVL_ENC, "%s()\n", __func__);

	port->encoder_params.width = port->width;
	port->encoder_params.height = port->height;
	port->encoder_params.is_50hz =
		(port->encodernorm.id & V4L2_STD_625_50) != 0;

	/* Set up the DIF (enable it) for analog mode by default */
	saa7164_api_initialize_dif(port);

	/* Configure the correct video standard */
	saa7164_api_configure_dif(port, port->encodernorm.id);

	/* Ensure the audio decoder is correct configured */
	saa7164_api_set_audio_std(port);
}

static int saa7164_encoder_buffers_dealloc(struct saa7164_port *port)
{
	struct list_head *c, *n, *p, *q, *l, *v;
	struct saa7164_dev *dev = port->dev;
	struct saa7164_buffer *buf;
	struct saa7164_user_buffer *ubuf;

	/* Remove any allocated buffers */
	mutex_lock(&port->dmaqueue_lock);

	dprintk(DBGLVL_ENC, "%s(port=%d) dmaqueue\n", __func__, port->nr);
	list_for_each_safe(c, n, &port->dmaqueue.list) {
		buf = list_entry(c, struct saa7164_buffer, list);
		list_del(c);
		saa7164_buffer_dealloc(buf);
	}

	dprintk(DBGLVL_ENC, "%s(port=%d) used\n", __func__, port->nr);
	list_for_each_safe(p, q, &port->list_buf_used.list) {
		ubuf = list_entry(p, struct saa7164_user_buffer, list);
		list_del(p);
		saa7164_buffer_dealloc_user(ubuf);
	}

	dprintk(DBGLVL_ENC, "%s(port=%d) free\n", __func__, port->nr);
	list_for_each_safe(l, v, &port->list_buf_free.list) {
		ubuf = list_entry(l, struct saa7164_user_buffer, list);
		list_del(l);
		saa7164_buffer_dealloc_user(ubuf);
	}

	mutex_unlock(&port->dmaqueue_lock);
	dprintk(DBGLVL_ENC, "%s(port=%d) done\n", __func__, port->nr);

	return 0;
}

/* Dynamic buffer switch at encoder start time */
static int saa7164_encoder_buffers_alloc(struct saa7164_port *port)
{
	struct saa7164_dev *dev = port->dev;
	struct saa7164_buffer *buf;
	struct saa7164_user_buffer *ubuf;
	struct tmHWStreamParameters *params = &port->hw_streamingparams;
	int result = -ENODEV, i;
	int len = 0;

	dprintk(DBGLVL_ENC, "%s()\n", __func__);

	if (port->encoder_params.stream_type ==
		V4L2_MPEG_STREAM_TYPE_MPEG2_PS) {
		dprintk(DBGLVL_ENC,
			"%s() type=V4L2_MPEG_STREAM_TYPE_MPEG2_PS\n",
			__func__);
		params->samplesperline = 128;
		params->numberoflines = 256;
		params->pitch = 128;
		params->numpagetables = 2 +
			((SAA7164_PS_NUMBER_OF_LINES * 128) / PAGE_SIZE);
	} else
	if (port->encoder_params.stream_type ==
		V4L2_MPEG_STREAM_TYPE_MPEG2_TS) {
		dprintk(DBGLVL_ENC,
			"%s() type=V4L2_MPEG_STREAM_TYPE_MPEG2_TS\n",
			__func__);
		params->samplesperline = 188;
		params->numberoflines = 312;
		params->pitch = 188;
		params->numpagetables = 2 +
			((SAA7164_TS_NUMBER_OF_LINES * 188) / PAGE_SIZE);
	} else
		BUG();

	/* Init and establish defaults */
	params->bitspersample = 8;
	params->linethreshold = 0;
	params->pagetablelistvirt = NULL;
	params->pagetablelistphys = NULL;
	params->numpagetableentries = port->hwcfg.buffercount;

	/* Allocate the PCI resources, buffers (hard) */
	for (i = 0; i < port->hwcfg.buffercount; i++) {
		buf = saa7164_buffer_alloc(port,
			params->numberoflines *
			params->pitch);

		if (!buf) {
			printk(KERN_ERR "%s() failed (errno = %d), unable to allocate buffer\n",
				__func__, result);
			result = -ENOMEM;
			goto failed;
		} else {

			mutex_lock(&port->dmaqueue_lock);
			list_add_tail(&buf->list, &port->dmaqueue.list);
			mutex_unlock(&port->dmaqueue_lock);

		}
	}

	/* Allocate some kernel buffers for copying
	 * to userpsace.
	 */
	len = params->numberoflines * params->pitch;

	if (encoder_buffers < 16)
		encoder_buffers = 16;
	if (encoder_buffers > 512)
		encoder_buffers = 512;

	for (i = 0; i < encoder_buffers; i++) {

		ubuf = saa7164_buffer_alloc_user(dev, len);
		if (ubuf) {
			mutex_lock(&port->dmaqueue_lock);
			list_add_tail(&ubuf->list, &port->list_buf_free.list);
			mutex_unlock(&port->dmaqueue_lock);
		}

	}

	result = 0;

failed:
	return result;
}

static int saa7164_encoder_initialize(struct saa7164_port *port)
{
	saa7164_encoder_configure(port);
	return 0;
}

/* -- V4L2 --------------------------------------------------------- */
int saa7164_s_std(struct saa7164_port *port, v4l2_std_id id)
{
	struct saa7164_dev *dev = port->dev;
	unsigned int i;

	dprintk(DBGLVL_ENC, "%s(id=0x%x)\n", __func__, (u32)id);

	for (i = 0; i < ARRAY_SIZE(saa7164_tvnorms); i++) {
		if (id & saa7164_tvnorms[i].id)
			break;
	}
	if (i == ARRAY_SIZE(saa7164_tvnorms))
		return -EINVAL;

	port->encodernorm = saa7164_tvnorms[i];
	port->std = id;

	/* Update the audio decoder while is not running in
	 * auto detect mode.
	 */
	saa7164_api_set_audio_std(port);

	dprintk(DBGLVL_ENC, "%s(id=0x%x) OK\n", __func__, (u32)id);

	return 0;
}

static int vidioc_s_std(struct file *file, void *priv, v4l2_std_id id)
{
	struct saa7164_encoder_fh *fh = file->private_data;

	return saa7164_s_std(fh->port, id);
}

int saa7164_g_std(struct saa7164_port *port, v4l2_std_id *id)
{
	*id = port->std;
	return 0;
}

static int vidioc_g_std(struct file *file, void *priv, v4l2_std_id *id)
{
	struct saa7164_encoder_fh *fh = file->private_data;

	return saa7164_g_std(fh->port, id);
}

int saa7164_enum_input(struct file *file, void *priv, struct v4l2_input *i)
{
	static const char * const inputs[] = {
		"tuner", "composite", "svideo", "aux",
		"composite 2", "svideo 2", "aux 2"
	};
	int n;

	if (i->index >= 7)
		return -EINVAL;

	strcpy(i->name, inputs[i->index]);

	if (i->index == 0)
		i->type = V4L2_INPUT_TYPE_TUNER;
	else
		i->type  = V4L2_INPUT_TYPE_CAMERA;

	for (n = 0; n < ARRAY_SIZE(saa7164_tvnorms); n++)
		i->std |= saa7164_tvnorms[n].id;

	return 0;
}

int saa7164_g_input(struct saa7164_port *port, unsigned int *i)
{
	struct saa7164_dev *dev = port->dev;

	if (saa7164_api_get_videomux(port) != SAA_OK)
		return -EIO;

	*i = (port->mux_input - 1);

	dprintk(DBGLVL_ENC, "%s() input=%d\n", __func__, *i);

	return 0;
}

static int vidioc_g_input(struct file *file, void *priv, unsigned int *i)
{
	struct saa7164_encoder_fh *fh = file->private_data;

	return saa7164_g_input(fh->port, i);
}

int saa7164_s_input(struct saa7164_port *port, unsigned int i)
{
	struct saa7164_dev *dev = port->dev;

	dprintk(DBGLVL_ENC, "%s() input=%d\n", __func__, i);

	if (i >= 7)
		return -EINVAL;

	port->mux_input = i + 1;

	if (saa7164_api_set_videomux(port) != SAA_OK)
		return -EIO;

	return 0;
}

static int vidioc_s_input(struct file *file, void *priv, unsigned int i)
{
	struct saa7164_encoder_fh *fh = file->private_data;

	return saa7164_s_input(fh->port, i);
}

int saa7164_g_tuner(struct file *file, void *priv, struct v4l2_tuner *t)
{
	struct saa7164_encoder_fh *fh = file->private_data;
	struct saa7164_port *port = fh->port;
	struct saa7164_dev *dev = port->dev;

	if (0 != t->index)
		return -EINVAL;

	strcpy(t->name, "tuner");
	t->capability = V4L2_TUNER_CAP_NORM | V4L2_TUNER_CAP_STEREO;
	t->rangelow = SAA7164_TV_MIN_FREQ;
	t->rangehigh = SAA7164_TV_MAX_FREQ;

	dprintk(DBGLVL_ENC, "VIDIOC_G_TUNER: tuner type %d\n", t->type);

	return 0;
}

int saa7164_s_tuner(struct file *file, void *priv,
			   const struct v4l2_tuner *t)
{
	if (0 != t->index)
		return -EINVAL;

	/* Update the A/V core */
	return 0;
}

int saa7164_g_frequency(struct saa7164_port *port, struct v4l2_frequency *f)
{
	if (f->tuner)
		return -EINVAL;

	f->frequency = port->freq;
	return 0;
}

static int vidioc_g_frequency(struct file *file, void *priv,
	struct v4l2_frequency *f)
{
	struct saa7164_encoder_fh *fh = file->private_data;

	return saa7164_g_frequency(fh->port, f);
}

int saa7164_s_frequency(struct saa7164_port *port,
			const struct v4l2_frequency *f)
{
	struct saa7164_dev *dev = port->dev;
	struct saa7164_port *tsport;
	struct dvb_frontend *fe;

	/* TODO: Pull this for the std */
	struct analog_parameters params = {
		.mode      = V4L2_TUNER_ANALOG_TV,
		.audmode   = V4L2_TUNER_MODE_STEREO,
		.std       = port->encodernorm.id,
		.frequency = f->frequency
	};

	/* Stop the encoder */
	dprintk(DBGLVL_ENC, "%s() frequency=%d tuner=%d\n", __func__,
		f->frequency, f->tuner);

	if (f->tuner != 0)
		return -EINVAL;

	port->freq = clamp(f->frequency,
			   SAA7164_TV_MIN_FREQ, SAA7164_TV_MAX_FREQ);

	/* Update the hardware */
	if (port->nr == SAA7164_PORT_ENC1)
		tsport = &dev->ports[SAA7164_PORT_TS1];
	else if (port->nr == SAA7164_PORT_ENC2)
		tsport = &dev->ports[SAA7164_PORT_TS2];
	else
		BUG();

	fe = tsport->dvb.frontend;

	if (fe && fe->ops.tuner_ops.set_analog_params)
		fe->ops.tuner_ops.set_analog_params(fe, &params);
	else
		printk(KERN_ERR "%s() No analog tuner, aborting\n", __func__);

	saa7164_encoder_initialize(port);

	return 0;
}

static int vidioc_s_frequency(struct file *file, void *priv,
			      const struct v4l2_frequency *f)
{
	struct saa7164_encoder_fh *fh = file->private_data;

	return saa7164_s_frequency(fh->port, f);
}

static int saa7164_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct saa7164_port *port =
		container_of(ctrl->handler, struct saa7164_port, ctrl_handler);
	struct saa7164_encoder_params *params = &port->encoder_params;
	int ret = 0;

	switch (ctrl->id) {
	case V4L2_CID_BRIGHTNESS:
		port->ctl_brightness = ctrl->val;
		saa7164_api_set_usercontrol(port, PU_BRIGHTNESS_CONTROL);
		break;
	case V4L2_CID_CONTRAST:
		port->ctl_contrast = ctrl->val;
		saa7164_api_set_usercontrol(port, PU_CONTRAST_CONTROL);
		break;
	case V4L2_CID_SATURATION:
		port->ctl_saturation = ctrl->val;
		saa7164_api_set_usercontrol(port, PU_SATURATION_CONTROL);
		break;
	case V4L2_CID_HUE:
		port->ctl_hue = ctrl->val;
		saa7164_api_set_usercontrol(port, PU_HUE_CONTROL);
		break;
	case V4L2_CID_SHARPNESS:
		port->ctl_sharpness = ctrl->val;
		saa7164_api_set_usercontrol(port, PU_SHARPNESS_CONTROL);
		break;
	case V4L2_CID_AUDIO_VOLUME:
		port->ctl_volume = ctrl->val;
		saa7164_api_set_audio_volume(port, port->ctl_volume);
		break;
	case V4L2_CID_MPEG_VIDEO_BITRATE:
		params->bitrate = ctrl->val;
		break;
	case V4L2_CID_MPEG_STREAM_TYPE:
		params->stream_type = ctrl->val;
		break;
	case V4L2_CID_MPEG_AUDIO_MUTE:
		params->ctl_mute = ctrl->val;
		ret = saa7164_api_audio_mute(port, params->ctl_mute);
		if (ret != SAA_OK) {
			printk(KERN_ERR "%s() error, ret = 0x%x\n", __func__,
				ret);
			ret = -EIO;
		}
		break;
	case V4L2_CID_MPEG_VIDEO_ASPECT:
		params->ctl_aspect = ctrl->val;
		ret = saa7164_api_set_aspect_ratio(port);
		if (ret != SAA_OK) {
			printk(KERN_ERR "%s() error, ret = 0x%x\n", __func__,
				ret);
			ret = -EIO;
		}
		break;
	case V4L2_CID_MPEG_VIDEO_BITRATE_MODE:
		params->bitrate_mode = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_B_FRAMES:
		params->refdist = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_BITRATE_PEAK:
		params->bitrate_peak = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_GOP_SIZE:
		params->gop_size = ctrl->val;
		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}

static int vidioc_querycap(struct file *file, void  *priv,
	struct v4l2_capability *cap)
{
	struct saa7164_encoder_fh *fh = file->private_data;
	struct saa7164_port *port = fh->port;
	struct saa7164_dev *dev = port->dev;

	strcpy(cap->driver, dev->name);
	strlcpy(cap->card, saa7164_boards[dev->board].name,
		sizeof(cap->card));
	sprintf(cap->bus_info, "PCI:%s", pci_name(dev->pci));

	cap->device_caps =
		V4L2_CAP_VIDEO_CAPTURE |
		V4L2_CAP_READWRITE |
		V4L2_CAP_TUNER;

	cap->capabilities = cap->device_caps |
		V4L2_CAP_VBI_CAPTURE |
		V4L2_CAP_DEVICE_CAPS;

	return 0;
}

static int vidioc_enum_fmt_vid_cap(struct file *file, void  *priv,
	struct v4l2_fmtdesc *f)
{
	if (f->index != 0)
		return -EINVAL;

	strlcpy(f->description, "MPEG", sizeof(f->description));
	f->pixelformat = V4L2_PIX_FMT_MPEG;

	return 0;
}

static int vidioc_fmt_vid_cap(struct file *file, void *priv,
				struct v4l2_format *f)
{
	struct saa7164_encoder_fh *fh = file->private_data;
	struct saa7164_port *port = fh->port;

	f->fmt.pix.pixelformat  = V4L2_PIX_FMT_MPEG;
	f->fmt.pix.bytesperline = 0;
	f->fmt.pix.sizeimage    = SAA7164_SIZEIMAGE;
	f->fmt.pix.field        = V4L2_FIELD_INTERLACED;
	f->fmt.pix.colorspace   = V4L2_COLORSPACE_SMPTE170M;
	f->fmt.pix.width        = port->width;
	f->fmt.pix.height       = port->height;
	return 0;
}

static int saa7164_encoder_stop_port(struct saa7164_port *port)
{
	struct saa7164_dev *dev = port->dev;
	int ret;

	ret = saa7164_api_transition_port(port, SAA_DMASTATE_STOP);
	if ((ret != SAA_OK) && (ret != SAA_ERR_ALREADY_STOPPED)) {
		printk(KERN_ERR "%s() stop transition failed, ret = 0x%x\n",
			__func__, ret);
		ret = -EIO;
	} else {
		dprintk(DBGLVL_ENC, "%s()    Stopped\n", __func__);
		ret = 0;
	}

	return ret;
}

static int saa7164_encoder_acquire_port(struct saa7164_port *port)
{
	struct saa7164_dev *dev = port->dev;
	int ret;

	ret = saa7164_api_transition_port(port, SAA_DMASTATE_ACQUIRE);
	if ((ret != SAA_OK) && (ret != SAA_ERR_ALREADY_STOPPED)) {
		printk(KERN_ERR "%s() acquire transition failed, ret = 0x%x\n",
			__func__, ret);
		ret = -EIO;
	} else {
		dprintk(DBGLVL_ENC, "%s() Acquired\n", __func__);
		ret = 0;
	}

	return ret;
}

static int saa7164_encoder_pause_port(struct saa7164_port *port)
{
	struct saa7164_dev *dev = port->dev;
	int ret;

	ret = saa7164_api_transition_port(port, SAA_DMASTATE_PAUSE);
	if ((ret != SAA_OK) && (ret != SAA_ERR_ALREADY_STOPPED)) {
		printk(KERN_ERR "%s() pause transition failed, ret = 0x%x\n",
			__func__, ret);
		ret = -EIO;
	} else {
		dprintk(DBGLVL_ENC, "%s()   Paused\n", __func__);
		ret = 0;
	}

	return ret;
}

/* Firmware is very windows centric, meaning you have to transition
 * the part through AVStream / KS Windows stages, forwards or backwards.
 * States are: stopped, acquired (h/w), paused, started.
 * We have to leave here will all of the soft buffers on the free list,
 * else the cfg_post() func won't have soft buffers to correctly configure.
 */
static int saa7164_encoder_stop_streaming(struct saa7164_port *port)
{
	struct saa7164_dev *dev = port->dev;
	struct saa7164_buffer *buf;
	struct saa7164_user_buffer *ubuf;
	struct list_head *c, *n;
	int ret;

	dprintk(DBGLVL_ENC, "%s(port=%d)\n", __func__, port->nr);

	ret = saa7164_encoder_pause_port(port);
	ret = saa7164_encoder_acquire_port(port);
	ret = saa7164_encoder_stop_port(port);

	dprintk(DBGLVL_ENC, "%s(port=%d) Hardware stopped\n", __func__,
		port->nr);

	/* Reset the state of any allocated buffer resources */
	mutex_lock(&port->dmaqueue_lock);

	/* Reset the hard and soft buffer state */
	list_for_each_safe(c, n, &port->dmaqueue.list) {
		buf = list_entry(c, struct saa7164_buffer, list);
		buf->flags = SAA7164_BUFFER_FREE;
		buf->pos = 0;
	}

	list_for_each_safe(c, n, &port->list_buf_used.list) {
		ubuf = list_entry(c, struct saa7164_user_buffer, list);
		ubuf->pos = 0;
		list_move_tail(&ubuf->list, &port->list_buf_free.list);
	}

	mutex_unlock(&port->dmaqueue_lock);

	/* Free any allocated resources */
	saa7164_encoder_buffers_dealloc(port);

	dprintk(DBGLVL_ENC, "%s(port=%d) Released\n", __func__, port->nr);

	return ret;
}

static int saa7164_encoder_start_streaming(struct saa7164_port *port)
{
	struct saa7164_dev *dev = port->dev;
	int result, ret = 0;

	dprintk(DBGLVL_ENC, "%s(port=%d)\n", __func__, port->nr);

	port->done_first_interrupt = 0;

	/* allocate all of the PCIe DMA buffer resources on the fly,
	 * allowing switching between TS and PS payloads without
	 * requiring a complete driver reload.
	 */
	saa7164_encoder_buffers_alloc(port);

	/* Configure the encoder with any cache values */
	saa7164_api_set_encoder(port);
	saa7164_api_get_encoder(port);

	/* Place the empty buffers on the hardware */
	saa7164_buffer_cfg_port(port);

	/* Acquire the hardware */
	result = saa7164_api_transition_port(port, SAA_DMASTATE_ACQUIRE);
	if ((result != SAA_OK) && (result != SAA_ERR_ALREADY_STOPPED)) {
		printk(KERN_ERR "%s() acquire transition failed, res = 0x%x\n",
			__func__, result);

		/* Stop the hardware, regardless */
		result = saa7164_api_transition_port(port, SAA_DMASTATE_STOP);
		if ((result != SAA_OK) && (result != SAA_ERR_ALREADY_STOPPED)) {
			printk(KERN_ERR "%s() acquire/forced stop transition failed, res = 0x%x\n",
			       __func__, result);
		}
		ret = -EIO;
		goto out;
	} else
		dprintk(DBGLVL_ENC, "%s()   Acquired\n", __func__);

	/* Pause the hardware */
	result = saa7164_api_transition_port(port, SAA_DMASTATE_PAUSE);
	if ((result != SAA_OK) && (result != SAA_ERR_ALREADY_STOPPED)) {
		printk(KERN_ERR "%s() pause transition failed, res = 0x%x\n",
				__func__, result);

		/* Stop the hardware, regardless */
		result = saa7164_api_transition_port(port, SAA_DMASTATE_STOP);
		if ((result != SAA_OK) && (result != SAA_ERR_ALREADY_STOPPED)) {
			printk(KERN_ERR "%s() pause/forced stop transition failed, res = 0x%x\n",
			       __func__, result);
		}

		ret = -EIO;
		goto out;
	} else
		dprintk(DBGLVL_ENC, "%s()   Paused\n", __func__);

	/* Start the hardware */
	result = saa7164_api_transition_port(port, SAA_DMASTATE_RUN);
	if ((result != SAA_OK) && (result != SAA_ERR_ALREADY_STOPPED)) {
		printk(KERN_ERR "%s() run transition failed, result = 0x%x\n",
				__func__, result);

		/* Stop the hardware, regardless */
		result = saa7164_api_transition_port(port, SAA_DMASTATE_STOP);
		if ((result != SAA_OK) && (result != SAA_ERR_ALREADY_STOPPED)) {
			printk(KERN_ERR "%s() run/forced stop transition failed, res = 0x%x\n",
			       __func__, result);
		}

		ret = -EIO;
	} else
		dprintk(DBGLVL_ENC, "%s()   Running\n", __func__);

out:
	return ret;
}

static int fops_open(struct file *file)
{
	struct saa7164_dev *dev;
	struct saa7164_port *port;
	struct saa7164_encoder_fh *fh;

	port = (struct saa7164_port *)video_get_drvdata(video_devdata(file));
	if (!port)
		return -ENODEV;

	dev = port->dev;

	dprintk(DBGLVL_ENC, "%s()\n", __func__);

	/* allocate + initialize per filehandle data */
	fh = kzalloc(sizeof(*fh), GFP_KERNEL);
	if (NULL == fh)
		return -ENOMEM;

	fh->port = port;
	v4l2_fh_init(&fh->fh, video_devdata(file));
	v4l2_fh_add(&fh->fh);
	file->private_data = fh;

	return 0;
}

static int fops_release(struct file *file)
{
	struct saa7164_encoder_fh *fh = file->private_data;
	struct saa7164_port *port = fh->port;
	struct saa7164_dev *dev = port->dev;

	dprintk(DBGLVL_ENC, "%s()\n", __func__);

	/* Shut device down on last close */
	if (atomic_cmpxchg(&fh->v4l_reading, 1, 0) == 1) {
		if (atomic_dec_return(&port->v4l_reader_count) == 0) {
			/* stop mpeg capture then cancel buffers */
			saa7164_encoder_stop_streaming(port);
		}
	}

	v4l2_fh_del(&fh->fh);
	v4l2_fh_exit(&fh->fh);
	kfree(fh);

	return 0;
}

static struct
saa7164_user_buffer *saa7164_enc_next_buf(struct saa7164_port *port)
{
	struct saa7164_user_buffer *ubuf = NULL;
	struct saa7164_dev *dev = port->dev;
	u32 crc;

	mutex_lock(&port->dmaqueue_lock);
	if (!list_empty(&port->list_buf_used.list)) {
		ubuf = list_first_entry(&port->list_buf_used.list,
			struct saa7164_user_buffer, list);

		if (crc_checking) {
			crc = crc32(0, ubuf->data, ubuf->actual_size);
			if (crc != ubuf->crc) {
				printk(KERN_ERR
		"%s() ubuf %p crc became invalid, was 0x%x became 0x%x\n",
					__func__,
					ubuf, ubuf->crc, crc);
			}
		}

	}
	mutex_unlock(&port->dmaqueue_lock);

	dprintk(DBGLVL_ENC, "%s() returns %p\n", __func__, ubuf);

	return ubuf;
}

static ssize_t fops_read(struct file *file, char __user *buffer,
	size_t count, loff_t *pos)
{
	struct saa7164_encoder_fh *fh = file->private_data;
	struct saa7164_port *port = fh->port;
	struct saa7164_user_buffer *ubuf = NULL;
	struct saa7164_dev *dev = port->dev;
	int ret = 0;
	int rem, cnt;
	u8 *p;

	port->last_read_msecs_diff = port->last_read_msecs;
	port->last_read_msecs = jiffies_to_msecs(jiffies);
	port->last_read_msecs_diff = port->last_read_msecs -
		port->last_read_msecs_diff;

	saa7164_histogram_update(&port->read_interval,
		port->last_read_msecs_diff);

	if (*pos) {
		printk(KERN_ERR "%s() ESPIPE\n", __func__);
		return -ESPIPE;
	}

	if (atomic_cmpxchg(&fh->v4l_reading, 0, 1) == 0) {
		if (atomic_inc_return(&port->v4l_reader_count) == 1) {

			if (saa7164_encoder_initialize(port) < 0) {
				printk(KERN_ERR "%s() EINVAL\n", __func__);
				return -EINVAL;
			}

			saa7164_encoder_start_streaming(port);
			msleep(200);
		}
	}

	/* blocking wait for buffer */
	if ((file->f_flags & O_NONBLOCK) == 0) {
		if (wait_event_interruptible(port->wait_read,
			saa7164_enc_next_buf(port))) {
				printk(KERN_ERR "%s() ERESTARTSYS\n", __func__);
				return -ERESTARTSYS;
		}
	}

	/* Pull the first buffer from the used list */
	ubuf = saa7164_enc_next_buf(port);

	while ((count > 0) && ubuf) {

		/* set remaining bytes to copy */
		rem = ubuf->actual_size - ubuf->pos;
		cnt = rem > count ? count : rem;

		p = ubuf->data + ubuf->pos;

		dprintk(DBGLVL_ENC,
			"%s() count=%d cnt=%d rem=%d buf=%p buf->pos=%d\n",
			__func__, (int)count, cnt, rem, ubuf, ubuf->pos);

		if (copy_to_user(buffer, p, cnt)) {
			printk(KERN_ERR "%s() copy_to_user failed\n", __func__);
			if (!ret) {
				printk(KERN_ERR "%s() EFAULT\n", __func__);
				ret = -EFAULT;
			}
			goto err;
		}

		ubuf->pos += cnt;
		count -= cnt;
		buffer += cnt;
		ret += cnt;

		if (ubuf->pos > ubuf->actual_size)
			printk(KERN_ERR "read() pos > actual, huh?\n");

		if (ubuf->pos == ubuf->actual_size) {

			/* finished with current buffer, take next buffer */

			/* Requeue the buffer on the free list */
			ubuf->pos = 0;

			mutex_lock(&port->dmaqueue_lock);
			list_move_tail(&ubuf->list, &port->list_buf_free.list);
			mutex_unlock(&port->dmaqueue_lock);

			/* Dequeue next */
			if ((file->f_flags & O_NONBLOCK) == 0) {
				if (wait_event_interruptible(port->wait_read,
					saa7164_enc_next_buf(port))) {
						break;
				}
			}
			ubuf = saa7164_enc_next_buf(port);
		}
	}
err:
	if (!ret && !ubuf)
		ret = -EAGAIN;

	return ret;
}

static unsigned int fops_poll(struct file *file, poll_table *wait)
{
	unsigned long req_events = poll_requested_events(wait);
	struct saa7164_encoder_fh *fh =
		(struct saa7164_encoder_fh *)file->private_data;
	struct saa7164_port *port = fh->port;
	unsigned int mask = v4l2_ctrl_poll(file, wait);

	port->last_poll_msecs_diff = port->last_poll_msecs;
	port->last_poll_msecs = jiffies_to_msecs(jiffies);
	port->last_poll_msecs_diff = port->last_poll_msecs -
		port->last_poll_msecs_diff;

	saa7164_histogram_update(&port->poll_interval,
		port->last_poll_msecs_diff);

	if (!(req_events & (POLLIN | POLLRDNORM)))
		return mask;

	if (atomic_cmpxchg(&fh->v4l_reading, 0, 1) == 0) {
		if (atomic_inc_return(&port->v4l_reader_count) == 1) {
			if (saa7164_encoder_initialize(port) < 0)
				return mask | POLLERR;
			saa7164_encoder_start_streaming(port);
			msleep(200);
		}
	}

	/* Pull the first buffer from the used list */
	if (!list_empty(&port->list_buf_used.list))
		mask |= POLLIN | POLLRDNORM;

	return mask;
}

static const struct v4l2_ctrl_ops saa7164_ctrl_ops = {
	.s_ctrl = saa7164_s_ctrl,
};

static const struct v4l2_file_operations mpeg_fops = {
	.owner		= THIS_MODULE,
	.open		= fops_open,
	.release	= fops_release,
	.read		= fops_read,
	.poll		= fops_poll,
	.unlocked_ioctl	= video_ioctl2,
};

static const struct v4l2_ioctl_ops mpeg_ioctl_ops = {
	.vidioc_s_std		 = vidioc_s_std,
	.vidioc_g_std		 = vidioc_g_std,
	.vidioc_enum_input	 = saa7164_enum_input,
	.vidioc_g_input		 = vidioc_g_input,
	.vidioc_s_input		 = vidioc_s_input,
	.vidioc_g_tuner		 = saa7164_g_tuner,
	.vidioc_s_tuner		 = saa7164_s_tuner,
	.vidioc_g_frequency	 = vidioc_g_frequency,
	.vidioc_s_frequency	 = vidioc_s_frequency,
	.vidioc_querycap	 = vidioc_querycap,
	.vidioc_enum_fmt_vid_cap = vidioc_enum_fmt_vid_cap,
	.vidioc_g_fmt_vid_cap	 = vidioc_fmt_vid_cap,
	.vidioc_try_fmt_vid_cap	 = vidioc_fmt_vid_cap,
	.vidioc_s_fmt_vid_cap	 = vidioc_fmt_vid_cap,
	.vidioc_log_status	 = v4l2_ctrl_log_status,
	.vidioc_subscribe_event  = v4l2_ctrl_subscribe_event,
	.vidioc_unsubscribe_event = v4l2_event_unsubscribe,
};

static struct video_device saa7164_mpeg_template = {
	.name          = "saa7164",
	.fops          = &mpeg_fops,
	.ioctl_ops     = &mpeg_ioctl_ops,
	.minor         = -1,
	.tvnorms       = SAA7164_NORMS,
};

static struct video_device *saa7164_encoder_alloc(
	struct saa7164_port *port,
	struct pci_dev *pci,
	struct video_device *template,
	char *type)
{
	struct video_device *vfd;
	struct saa7164_dev *dev = port->dev;

	dprintk(DBGLVL_ENC, "%s()\n", __func__);

	vfd = video_device_alloc();
	if (NULL == vfd)
		return NULL;

	*vfd = *template;
	snprintf(vfd->name, sizeof(vfd->name), "%s %s (%s)", dev->name,
		type, saa7164_boards[dev->board].name);

	vfd->v4l2_dev  = &dev->v4l2_dev;
	vfd->release = video_device_release;
	return vfd;
}

int saa7164_encoder_register(struct saa7164_port *port)
{
	struct saa7164_dev *dev = port->dev;
	struct v4l2_ctrl_handler *hdl = &port->ctrl_handler;
	int result = -ENODEV;

	dprintk(DBGLVL_ENC, "%s()\n", __func__);

	BUG_ON(port->type != SAA7164_MPEG_ENCODER);

	/* Sanity check that the PCI configuration space is active */
	if (port->hwcfg.BARLocation == 0) {
		printk(KERN_ERR "%s() failed (errno = %d), NO PCI configuration\n",
			__func__, result);
		result = -ENOMEM;
		goto failed;
	}

	/* Establish encoder defaults here */
	/* Set default TV standard */
	port->encodernorm = saa7164_tvnorms[0];
	port->width = 720;
	port->mux_input = 1; /* Composite */
	port->video_format = EU_VIDEO_FORMAT_MPEG_2;
	port->audio_format = 0;
	port->video_resolution = 0;
	port->freq = SAA7164_TV_MIN_FREQ;

	v4l2_ctrl_handler_init(hdl, 14);
	v4l2_ctrl_new_std(hdl, &saa7164_ctrl_ops,
			  V4L2_CID_BRIGHTNESS, 0, 255, 1, 127);
	v4l2_ctrl_new_std(hdl, &saa7164_ctrl_ops,
			  V4L2_CID_CONTRAST, 0, 255, 1, 66);
	v4l2_ctrl_new_std(hdl, &saa7164_ctrl_ops,
			  V4L2_CID_SATURATION, 0, 255, 1, 62);
	v4l2_ctrl_new_std(hdl, &saa7164_ctrl_ops,
			  V4L2_CID_HUE, 0, 255, 1, 128);
	v4l2_ctrl_new_std(hdl, &saa7164_ctrl_ops,
			  V4L2_CID_SHARPNESS, 0x0, 0x0f, 1, 8);
	v4l2_ctrl_new_std(hdl, &saa7164_ctrl_ops,
			  V4L2_CID_MPEG_AUDIO_MUTE, 0x0, 0x01, 1, 0);
	v4l2_ctrl_new_std(hdl, &saa7164_ctrl_ops,
			  V4L2_CID_AUDIO_VOLUME, -83, 24, 1, 20);
	v4l2_ctrl_new_std(hdl, &saa7164_ctrl_ops,
			  V4L2_CID_MPEG_VIDEO_BITRATE,
			  ENCODER_MIN_BITRATE, ENCODER_MAX_BITRATE,
			  100000, ENCODER_DEF_BITRATE);
	v4l2_ctrl_new_std_menu(hdl, &saa7164_ctrl_ops,
			       V4L2_CID_MPEG_STREAM_TYPE,
			       V4L2_MPEG_STREAM_TYPE_MPEG2_TS, 0,
			       V4L2_MPEG_STREAM_TYPE_MPEG2_PS);
	v4l2_ctrl_new_std_menu(hdl, &saa7164_ctrl_ops,
			       V4L2_CID_MPEG_VIDEO_ASPECT,
			       V4L2_MPEG_VIDEO_ASPECT_221x100, 0,
			       V4L2_MPEG_VIDEO_ASPECT_4x3);
	v4l2_ctrl_new_std(hdl, &saa7164_ctrl_ops,
			  V4L2_CID_MPEG_VIDEO_GOP_SIZE, 1, 255, 1, 15);
	v4l2_ctrl_new_std_menu(hdl, &saa7164_ctrl_ops,
			       V4L2_CID_MPEG_VIDEO_BITRATE_MODE,
			       V4L2_MPEG_VIDEO_BITRATE_MODE_CBR, 0,
			       V4L2_MPEG_VIDEO_BITRATE_MODE_VBR);
	v4l2_ctrl_new_std(hdl, &saa7164_ctrl_ops,
			  V4L2_CID_MPEG_VIDEO_B_FRAMES, 1, 3, 1, 1);
	v4l2_ctrl_new_std(hdl, &saa7164_ctrl_ops,
			  V4L2_CID_MPEG_VIDEO_BITRATE_PEAK,
			  ENCODER_MIN_BITRATE, ENCODER_MAX_BITRATE,
			  100000, ENCODER_DEF_BITRATE);
	if (hdl->error) {
		result = hdl->error;
		goto failed;
	}

	port->std = V4L2_STD_NTSC_M;

	if (port->encodernorm.id & V4L2_STD_525_60)
		port->height = 480;
	else
		port->height = 576;

	/* Allocate and register the video device node */
	port->v4l_device = saa7164_encoder_alloc(port,
		dev->pci, &saa7164_mpeg_template, "mpeg");

	if (!port->v4l_device) {
		printk(KERN_INFO "%s: can't allocate mpeg device\n",
			dev->name);
		result = -ENOMEM;
		goto failed;
	}

	port->v4l_device->ctrl_handler = hdl;
	v4l2_ctrl_handler_setup(hdl);
	video_set_drvdata(port->v4l_device, port);
	result = video_register_device(port->v4l_device,
		VFL_TYPE_GRABBER, -1);
	if (result < 0) {
		printk(KERN_INFO "%s: can't register mpeg device\n",
			dev->name);
		/* TODO: We're going to leak here if we don't dealloc
		 The buffers above. The unreg function can't deal wit it.
		*/
		goto failed;
	}

	printk(KERN_INFO "%s: registered device video%d [mpeg]\n",
		dev->name, port->v4l_device->num);

	/* Configure the hardware defaults */
	saa7164_api_set_videomux(port);
	saa7164_api_set_usercontrol(port, PU_BRIGHTNESS_CONTROL);
	saa7164_api_set_usercontrol(port, PU_CONTRAST_CONTROL);
	saa7164_api_set_usercontrol(port, PU_HUE_CONTROL);
	saa7164_api_set_usercontrol(port, PU_SATURATION_CONTROL);
	saa7164_api_set_usercontrol(port, PU_SHARPNESS_CONTROL);
	saa7164_api_audio_mute(port, 0);
	saa7164_api_set_audio_volume(port, 20);
	saa7164_api_set_aspect_ratio(port);

	/* Disable audio standard detection, it's buggy */
	saa7164_api_set_audio_detection(port, 0);

	saa7164_api_set_encoder(port);
	saa7164_api_get_encoder(port);

	result = 0;
failed:
	return result;
}

void saa7164_encoder_unregister(struct saa7164_port *port)
{
	struct saa7164_dev *dev = port->dev;

	dprintk(DBGLVL_ENC, "%s(port=%d)\n", __func__, port->nr);

	BUG_ON(port->type != SAA7164_MPEG_ENCODER);

	if (port->v4l_device) {
		if (port->v4l_device->minor != -1)
			video_unregister_device(port->v4l_device);
		else
			video_device_release(port->v4l_device);

		port->v4l_device = NULL;
	}
	v4l2_ctrl_handler_free(&port->ctrl_handler);

	dprintk(DBGLVL_ENC, "%s(port=%d) done\n", __func__, port->nr);
}

