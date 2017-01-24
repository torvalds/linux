/*
 * Copyright (c) 2013,2016 Lubomir Rintel
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification.
 * 2. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL").
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
/*
 * Fushicai USBTV007 Audio-Video Grabber Driver
 *
 * Product web site:
 * http://www.fushicai.com/products_detail/&productId=d05449ee-b690-42f9-a661-aa7353894bed.html
 *
 * Following LWN articles were very useful in construction of this driver:
 * Video4Linux2 API series: http://lwn.net/Articles/203924/
 * videobuf2 API explanation: http://lwn.net/Articles/447435/
 * Thanks go to Jonathan Corbet for providing this quality documentation.
 * He is awesome.
 *
 * No physical hardware was harmed running Windows during the
 * reverse-engineering activity
 */

#include <media/v4l2-ioctl.h>
#include <media/videobuf2-v4l2.h>

#include "usbtv.h"

static struct usbtv_norm_params norm_params[] = {
	{
		.norm = V4L2_STD_525_60,
		.cap_width = 720,
		.cap_height = 480,
	},
	{
		.norm = V4L2_STD_PAL,
		.cap_width = 720,
		.cap_height = 576,
	}
};

static int usbtv_configure_for_norm(struct usbtv *usbtv, v4l2_std_id norm)
{
	int i, ret = 0;
	struct usbtv_norm_params *params = NULL;

	for (i = 0; i < ARRAY_SIZE(norm_params); i++) {
		if (norm_params[i].norm & norm) {
			params = &norm_params[i];
			break;
		}
	}

	if (params) {
		usbtv->width = params->cap_width;
		usbtv->height = params->cap_height;
		usbtv->n_chunks = usbtv->width * usbtv->height
						/ 4 / USBTV_CHUNK;
		usbtv->norm = params->norm;
	} else
		ret = -EINVAL;

	return ret;
}

static int usbtv_select_input(struct usbtv *usbtv, int input)
{
	int ret;

	static const u16 composite[][2] = {
		{ USBTV_BASE + 0x0105, 0x0060 },
		{ USBTV_BASE + 0x011f, 0x00f2 },
		{ USBTV_BASE + 0x0127, 0x0060 },
		{ USBTV_BASE + 0x00ae, 0x0010 },
		{ USBTV_BASE + 0x0239, 0x0060 },
	};

	static const u16 svideo[][2] = {
		{ USBTV_BASE + 0x0105, 0x0010 },
		{ USBTV_BASE + 0x011f, 0x00ff },
		{ USBTV_BASE + 0x0127, 0x0060 },
		{ USBTV_BASE + 0x00ae, 0x0030 },
		{ USBTV_BASE + 0x0239, 0x0060 },
	};

	switch (input) {
	case USBTV_COMPOSITE_INPUT:
		ret = usbtv_set_regs(usbtv, composite, ARRAY_SIZE(composite));
		break;
	case USBTV_SVIDEO_INPUT:
		ret = usbtv_set_regs(usbtv, svideo, ARRAY_SIZE(svideo));
		break;
	default:
		ret = -EINVAL;
	}

	if (!ret)
		usbtv->input = input;

	return ret;
}

static int usbtv_select_norm(struct usbtv *usbtv, v4l2_std_id norm)
{
	int ret;
	static const u16 pal[][2] = {
		{ USBTV_BASE + 0x001a, 0x0068 },
		{ USBTV_BASE + 0x010e, 0x0072 },
		{ USBTV_BASE + 0x010f, 0x00a2 },
		{ USBTV_BASE + 0x0112, 0x00b0 },
		{ USBTV_BASE + 0x0117, 0x0001 },
		{ USBTV_BASE + 0x0118, 0x002c },
		{ USBTV_BASE + 0x012d, 0x0010 },
		{ USBTV_BASE + 0x012f, 0x0020 },
		{ USBTV_BASE + 0x024f, 0x0002 },
		{ USBTV_BASE + 0x0254, 0x0059 },
		{ USBTV_BASE + 0x025a, 0x0016 },
		{ USBTV_BASE + 0x025b, 0x0035 },
		{ USBTV_BASE + 0x0263, 0x0017 },
		{ USBTV_BASE + 0x0266, 0x0016 },
		{ USBTV_BASE + 0x0267, 0x0036 }
	};

	static const u16 ntsc[][2] = {
		{ USBTV_BASE + 0x001a, 0x0079 },
		{ USBTV_BASE + 0x010e, 0x0068 },
		{ USBTV_BASE + 0x010f, 0x009c },
		{ USBTV_BASE + 0x0112, 0x00f0 },
		{ USBTV_BASE + 0x0117, 0x0000 },
		{ USBTV_BASE + 0x0118, 0x00fc },
		{ USBTV_BASE + 0x012d, 0x0004 },
		{ USBTV_BASE + 0x012f, 0x0008 },
		{ USBTV_BASE + 0x024f, 0x0001 },
		{ USBTV_BASE + 0x0254, 0x005f },
		{ USBTV_BASE + 0x025a, 0x0012 },
		{ USBTV_BASE + 0x025b, 0x0001 },
		{ USBTV_BASE + 0x0263, 0x001c },
		{ USBTV_BASE + 0x0266, 0x0011 },
		{ USBTV_BASE + 0x0267, 0x0005 }
	};

	ret = usbtv_configure_for_norm(usbtv, norm);

	if (!ret) {
		if (norm & V4L2_STD_525_60)
			ret = usbtv_set_regs(usbtv, ntsc, ARRAY_SIZE(ntsc));
		else if (norm & V4L2_STD_PAL)
			ret = usbtv_set_regs(usbtv, pal, ARRAY_SIZE(pal));
	}

	return ret;
}

static int usbtv_setup_capture(struct usbtv *usbtv)
{
	int ret;
	static const u16 setup[][2] = {
		/* These seem to enable the device. */
		{ USBTV_BASE + 0x0008, 0x0001 },
		{ USBTV_BASE + 0x01d0, 0x00ff },
		{ USBTV_BASE + 0x01d9, 0x0002 },

		/* These seem to influence color parameters, such as
		 * brightness, etc. */
		{ USBTV_BASE + 0x0239, 0x0040 },
		{ USBTV_BASE + 0x0240, 0x0000 },
		{ USBTV_BASE + 0x0241, 0x0000 },
		{ USBTV_BASE + 0x0242, 0x0002 },
		{ USBTV_BASE + 0x0243, 0x0080 },
		{ USBTV_BASE + 0x0244, 0x0012 },
		{ USBTV_BASE + 0x0245, 0x0090 },
		{ USBTV_BASE + 0x0246, 0x0000 },

		{ USBTV_BASE + 0x0278, 0x002d },
		{ USBTV_BASE + 0x0279, 0x000a },
		{ USBTV_BASE + 0x027a, 0x0032 },
		{ 0xf890, 0x000c },
		{ 0xf894, 0x0086 },

		{ USBTV_BASE + 0x00ac, 0x00c0 },
		{ USBTV_BASE + 0x00ad, 0x0000 },
		{ USBTV_BASE + 0x00a2, 0x0012 },
		{ USBTV_BASE + 0x00a3, 0x00e0 },
		{ USBTV_BASE + 0x00a4, 0x0028 },
		{ USBTV_BASE + 0x00a5, 0x0082 },
		{ USBTV_BASE + 0x00a7, 0x0080 },
		{ USBTV_BASE + 0x0000, 0x0014 },
		{ USBTV_BASE + 0x0006, 0x0003 },
		{ USBTV_BASE + 0x0090, 0x0099 },
		{ USBTV_BASE + 0x0091, 0x0090 },
		{ USBTV_BASE + 0x0094, 0x0068 },
		{ USBTV_BASE + 0x0095, 0x0070 },
		{ USBTV_BASE + 0x009c, 0x0030 },
		{ USBTV_BASE + 0x009d, 0x00c0 },
		{ USBTV_BASE + 0x009e, 0x00e0 },
		{ USBTV_BASE + 0x0019, 0x0006 },
		{ USBTV_BASE + 0x008c, 0x00ba },
		{ USBTV_BASE + 0x0101, 0x00ff },
		{ USBTV_BASE + 0x010c, 0x00b3 },
		{ USBTV_BASE + 0x01b2, 0x0080 },
		{ USBTV_BASE + 0x01b4, 0x00a0 },
		{ USBTV_BASE + 0x014c, 0x00ff },
		{ USBTV_BASE + 0x014d, 0x00ca },
		{ USBTV_BASE + 0x0113, 0x0053 },
		{ USBTV_BASE + 0x0119, 0x008a },
		{ USBTV_BASE + 0x013c, 0x0003 },
		{ USBTV_BASE + 0x0150, 0x009c },
		{ USBTV_BASE + 0x0151, 0x0071 },
		{ USBTV_BASE + 0x0152, 0x00c6 },
		{ USBTV_BASE + 0x0153, 0x0084 },
		{ USBTV_BASE + 0x0154, 0x00bc },
		{ USBTV_BASE + 0x0155, 0x00a0 },
		{ USBTV_BASE + 0x0156, 0x00a0 },
		{ USBTV_BASE + 0x0157, 0x009c },
		{ USBTV_BASE + 0x0158, 0x001f },
		{ USBTV_BASE + 0x0159, 0x0006 },
		{ USBTV_BASE + 0x015d, 0x0000 },

		{ USBTV_BASE + 0x0003, 0x0004 },
		{ USBTV_BASE + 0x0100, 0x00d3 },
		{ USBTV_BASE + 0x0115, 0x0015 },
		{ USBTV_BASE + 0x0220, 0x002e },
		{ USBTV_BASE + 0x0225, 0x0008 },
		{ USBTV_BASE + 0x024e, 0x0002 },
		{ USBTV_BASE + 0x024e, 0x0002 },
		{ USBTV_BASE + 0x024f, 0x0002 },
	};

	ret = usbtv_set_regs(usbtv, setup, ARRAY_SIZE(setup));
	if (ret)
		return ret;

	ret = usbtv_select_norm(usbtv, usbtv->norm);
	if (ret)
		return ret;

	ret = usbtv_select_input(usbtv, usbtv->input);
	if (ret)
		return ret;

	ret = v4l2_ctrl_handler_setup(&usbtv->ctrl);
	if (ret)
		return ret;

	return 0;
}

/* Copy data from chunk into a frame buffer, deinterlacing the data
 * into every second line. Unfortunately, they don't align nicely into
 * 720 pixel lines, as the chunk is 240 words long, which is 480 pixels.
 * Therefore, we break down the chunk into two halves before copying,
 * so that we can interleave a line if needed.
 *
 * Each "chunk" is 240 words; a word in this context equals 4 bytes.
 * Image format is YUYV/YUV 4:2:2, consisting of Y Cr Y Cb, defining two
 * pixels, the Cr and Cb shared between the two pixels, but each having
 * separate Y values. Thus, the 240 words equal 480 pixels. It therefore,
 * takes 1.5 chunks to make a 720 pixel-wide line for the frame.
 * The image is interlaced, so there is a "scan" of odd lines, followed
 * by "scan" of even numbered lines.
 *
 * Following code is writing the chunks in correct sequence, skipping
 * the rows based on "odd" value.
 * line 1: chunk[0][  0..479] chunk[0][480..959] chunk[1][  0..479]
 * line 3: chunk[1][480..959] chunk[2][  0..479] chunk[2][480..959]
 * ...etc.
 */
static void usbtv_chunk_to_vbuf(u32 *frame, __be32 *src, int chunk_no, int odd)
{
	int half;

	for (half = 0; half < 2; half++) {
		int part_no = chunk_no * 2 + half;
		int line = part_no / 3;
		int part_index = (line * 2 + !odd) * 3 + (part_no % 3);

		u32 *dst = &frame[part_index * USBTV_CHUNK/2];

		memcpy(dst, src, USBTV_CHUNK/2 * sizeof(*src));
		src += USBTV_CHUNK/2;
	}
}

/* Called for each 256-byte image chunk.
 * First word identifies the chunk, followed by 240 words of image
 * data and padding. */
static void usbtv_image_chunk(struct usbtv *usbtv, __be32 *chunk)
{
	int frame_id, odd, chunk_no;
	u32 *frame;
	struct usbtv_buf *buf;
	unsigned long flags;

	/* Ignore corrupted lines. */
	if (!USBTV_MAGIC_OK(chunk))
		return;
	frame_id = USBTV_FRAME_ID(chunk);
	odd = USBTV_ODD(chunk);
	chunk_no = USBTV_CHUNK_NO(chunk);
	if (chunk_no >= usbtv->n_chunks)
		return;

	/* Beginning of a frame. */
	if (chunk_no == 0) {
		usbtv->frame_id = frame_id;
		usbtv->chunks_done = 0;
	}

	if (usbtv->frame_id != frame_id)
		return;

	spin_lock_irqsave(&usbtv->buflock, flags);
	if (list_empty(&usbtv->bufs)) {
		/* No free buffers. Userspace likely too slow. */
		spin_unlock_irqrestore(&usbtv->buflock, flags);
		return;
	}

	/* First available buffer. */
	buf = list_first_entry(&usbtv->bufs, struct usbtv_buf, list);
	frame = vb2_plane_vaddr(&buf->vb.vb2_buf, 0);

	/* Copy the chunk data. */
	usbtv_chunk_to_vbuf(frame, &chunk[1], chunk_no, odd);
	usbtv->chunks_done++;

	/* Last chunk in a field */
	if (chunk_no == usbtv->n_chunks-1) {
		/* Last chunk in a frame, signalling an end */
		if (odd && !usbtv->last_odd) {
			int size = vb2_plane_size(&buf->vb.vb2_buf, 0);
			enum vb2_buffer_state state = usbtv->chunks_done ==
				usbtv->n_chunks ?
				VB2_BUF_STATE_DONE :
				VB2_BUF_STATE_ERROR;

			buf->vb.field = V4L2_FIELD_INTERLACED;
			buf->vb.sequence = usbtv->sequence++;
			buf->vb.vb2_buf.timestamp = ktime_get_ns();
			vb2_set_plane_payload(&buf->vb.vb2_buf, 0, size);
			vb2_buffer_done(&buf->vb.vb2_buf, state);
			list_del(&buf->list);
		}
		usbtv->last_odd = odd;
	}

	spin_unlock_irqrestore(&usbtv->buflock, flags);
}

/* Got image data. Each packet contains a number of 256-word chunks we
 * compose the image from. */
static void usbtv_iso_cb(struct urb *ip)
{
	int ret;
	int i;
	struct usbtv *usbtv = (struct usbtv *)ip->context;

	switch (ip->status) {
	/* All fine. */
	case 0:
		break;
	/* Device disconnected or capture stopped? */
	case -ENODEV:
	case -ENOENT:
	case -ECONNRESET:
	case -ESHUTDOWN:
		return;
	/* Unknown error. Retry. */
	default:
		dev_warn(usbtv->dev, "Bad response for ISO request.\n");
		goto resubmit;
	}

	for (i = 0; i < ip->number_of_packets; i++) {
		int size = ip->iso_frame_desc[i].actual_length;
		unsigned char *data = ip->transfer_buffer +
				ip->iso_frame_desc[i].offset;
		int offset;

		for (offset = 0; USBTV_CHUNK_SIZE * offset < size; offset++)
			usbtv_image_chunk(usbtv,
				(__be32 *)&data[USBTV_CHUNK_SIZE * offset]);
	}

resubmit:
	ret = usb_submit_urb(ip, GFP_ATOMIC);
	if (ret < 0)
		dev_warn(usbtv->dev, "Could not resubmit ISO URB\n");
}

static struct urb *usbtv_setup_iso_transfer(struct usbtv *usbtv)
{
	struct urb *ip;
	int size = usbtv->iso_size;
	int i;

	ip = usb_alloc_urb(USBTV_ISOC_PACKETS, GFP_KERNEL);
	if (ip == NULL)
		return NULL;

	ip->dev = usbtv->udev;
	ip->context = usbtv;
	ip->pipe = usb_rcvisocpipe(usbtv->udev, USBTV_VIDEO_ENDP);
	ip->interval = 1;
	ip->transfer_flags = URB_ISO_ASAP;
	ip->transfer_buffer = kzalloc(size * USBTV_ISOC_PACKETS,
						GFP_KERNEL);
	if (!ip->transfer_buffer) {
		usb_free_urb(ip);
		return NULL;
	}
	ip->complete = usbtv_iso_cb;
	ip->number_of_packets = USBTV_ISOC_PACKETS;
	ip->transfer_buffer_length = size * USBTV_ISOC_PACKETS;
	for (i = 0; i < USBTV_ISOC_PACKETS; i++) {
		ip->iso_frame_desc[i].offset = size * i;
		ip->iso_frame_desc[i].length = size;
	}

	return ip;
}

static void usbtv_stop(struct usbtv *usbtv)
{
	int i;
	unsigned long flags;

	/* Cancel running transfers. */
	for (i = 0; i < USBTV_ISOC_TRANSFERS; i++) {
		struct urb *ip = usbtv->isoc_urbs[i];

		if (ip == NULL)
			continue;
		usb_kill_urb(ip);
		kfree(ip->transfer_buffer);
		usb_free_urb(ip);
		usbtv->isoc_urbs[i] = NULL;
	}

	/* Return buffers to userspace. */
	spin_lock_irqsave(&usbtv->buflock, flags);
	while (!list_empty(&usbtv->bufs)) {
		struct usbtv_buf *buf = list_first_entry(&usbtv->bufs,
						struct usbtv_buf, list);
		vb2_buffer_done(&buf->vb.vb2_buf, VB2_BUF_STATE_ERROR);
		list_del(&buf->list);
	}
	spin_unlock_irqrestore(&usbtv->buflock, flags);
}

static int usbtv_start(struct usbtv *usbtv)
{
	int i;
	int ret;

	usbtv_audio_suspend(usbtv);

	ret = usb_set_interface(usbtv->udev, 0, 0);
	if (ret < 0)
		return ret;

	ret = usbtv_setup_capture(usbtv);
	if (ret < 0)
		return ret;

	ret = usb_set_interface(usbtv->udev, 0, 1);
	if (ret < 0)
		return ret;

	usbtv_audio_resume(usbtv);

	for (i = 0; i < USBTV_ISOC_TRANSFERS; i++) {
		struct urb *ip;

		ip = usbtv_setup_iso_transfer(usbtv);
		if (ip == NULL) {
			ret = -ENOMEM;
			goto start_fail;
		}
		usbtv->isoc_urbs[i] = ip;

		ret = usb_submit_urb(ip, GFP_KERNEL);
		if (ret < 0)
			goto start_fail;
	}

	return 0;

start_fail:
	usbtv_stop(usbtv);
	return ret;
}

static int usbtv_querycap(struct file *file, void *priv,
				struct v4l2_capability *cap)
{
	struct usbtv *dev = video_drvdata(file);

	strlcpy(cap->driver, "usbtv", sizeof(cap->driver));
	strlcpy(cap->card, "usbtv", sizeof(cap->card));
	usb_make_path(dev->udev, cap->bus_info, sizeof(cap->bus_info));
	cap->device_caps = V4L2_CAP_VIDEO_CAPTURE;
	cap->device_caps |= V4L2_CAP_READWRITE | V4L2_CAP_STREAMING;
	cap->capabilities = cap->device_caps | V4L2_CAP_DEVICE_CAPS;
	return 0;
}

static int usbtv_enum_input(struct file *file, void *priv,
					struct v4l2_input *i)
{
	struct usbtv *dev = video_drvdata(file);

	switch (i->index) {
	case USBTV_COMPOSITE_INPUT:
		strlcpy(i->name, "Composite", sizeof(i->name));
		break;
	case USBTV_SVIDEO_INPUT:
		strlcpy(i->name, "S-Video", sizeof(i->name));
		break;
	default:
		return -EINVAL;
	}

	i->type = V4L2_INPUT_TYPE_CAMERA;
	i->std = dev->vdev.tvnorms;
	return 0;
}

static int usbtv_enum_fmt_vid_cap(struct file *file, void  *priv,
					struct v4l2_fmtdesc *f)
{
	if (f->index > 0)
		return -EINVAL;

	strlcpy(f->description, "16 bpp YUY2, 4:2:2, packed",
					sizeof(f->description));
	f->pixelformat = V4L2_PIX_FMT_YUYV;
	return 0;
}

static int usbtv_fmt_vid_cap(struct file *file, void *priv,
					struct v4l2_format *f)
{
	struct usbtv *usbtv = video_drvdata(file);

	f->fmt.pix.width = usbtv->width;
	f->fmt.pix.height = usbtv->height;
	f->fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
	f->fmt.pix.field = V4L2_FIELD_INTERLACED;
	f->fmt.pix.bytesperline = usbtv->width * 2;
	f->fmt.pix.sizeimage = (f->fmt.pix.bytesperline * f->fmt.pix.height);
	f->fmt.pix.colorspace = V4L2_COLORSPACE_SMPTE170M;

	return 0;
}

static int usbtv_g_std(struct file *file, void *priv, v4l2_std_id *norm)
{
	struct usbtv *usbtv = video_drvdata(file);
	*norm = usbtv->norm;
	return 0;
}

static int usbtv_s_std(struct file *file, void *priv, v4l2_std_id norm)
{
	int ret = -EINVAL;
	struct usbtv *usbtv = video_drvdata(file);

	if ((norm & V4L2_STD_525_60) || (norm & V4L2_STD_PAL))
		ret = usbtv_select_norm(usbtv, norm);

	return ret;
}

static int usbtv_g_input(struct file *file, void *priv, unsigned int *i)
{
	struct usbtv *usbtv = video_drvdata(file);
	*i = usbtv->input;
	return 0;
}

static int usbtv_s_input(struct file *file, void *priv, unsigned int i)
{
	struct usbtv *usbtv = video_drvdata(file);

	return usbtv_select_input(usbtv, i);
}

static struct v4l2_ioctl_ops usbtv_ioctl_ops = {
	.vidioc_querycap = usbtv_querycap,
	.vidioc_enum_input = usbtv_enum_input,
	.vidioc_enum_fmt_vid_cap = usbtv_enum_fmt_vid_cap,
	.vidioc_g_fmt_vid_cap = usbtv_fmt_vid_cap,
	.vidioc_try_fmt_vid_cap = usbtv_fmt_vid_cap,
	.vidioc_s_fmt_vid_cap = usbtv_fmt_vid_cap,
	.vidioc_g_std = usbtv_g_std,
	.vidioc_s_std = usbtv_s_std,
	.vidioc_g_input = usbtv_g_input,
	.vidioc_s_input = usbtv_s_input,

	.vidioc_reqbufs = vb2_ioctl_reqbufs,
	.vidioc_prepare_buf = vb2_ioctl_prepare_buf,
	.vidioc_querybuf = vb2_ioctl_querybuf,
	.vidioc_create_bufs = vb2_ioctl_create_bufs,
	.vidioc_qbuf = vb2_ioctl_qbuf,
	.vidioc_dqbuf = vb2_ioctl_dqbuf,
	.vidioc_streamon = vb2_ioctl_streamon,
	.vidioc_streamoff = vb2_ioctl_streamoff,
};

static struct v4l2_file_operations usbtv_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = video_ioctl2,
	.mmap = vb2_fop_mmap,
	.open = v4l2_fh_open,
	.release = vb2_fop_release,
	.read = vb2_fop_read,
	.poll = vb2_fop_poll,
};

static int usbtv_queue_setup(struct vb2_queue *vq,
	unsigned int *nbuffers,
	unsigned int *nplanes, unsigned int sizes[], struct device *alloc_devs[])
{
	struct usbtv *usbtv = vb2_get_drv_priv(vq);
	unsigned size = USBTV_CHUNK * usbtv->n_chunks * 2 * sizeof(u32);

	if (vq->num_buffers + *nbuffers < 2)
		*nbuffers = 2 - vq->num_buffers;
	if (*nplanes)
		return sizes[0] < size ? -EINVAL : 0;
	*nplanes = 1;
	sizes[0] = size;

	return 0;
}

static void usbtv_buf_queue(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct usbtv *usbtv = vb2_get_drv_priv(vb->vb2_queue);
	struct usbtv_buf *buf = container_of(vbuf, struct usbtv_buf, vb);
	unsigned long flags;

	if (usbtv->udev == NULL) {
		vb2_buffer_done(vb, VB2_BUF_STATE_ERROR);
		return;
	}

	spin_lock_irqsave(&usbtv->buflock, flags);
	list_add_tail(&buf->list, &usbtv->bufs);
	spin_unlock_irqrestore(&usbtv->buflock, flags);
}

static int usbtv_start_streaming(struct vb2_queue *vq, unsigned int count)
{
	struct usbtv *usbtv = vb2_get_drv_priv(vq);

	if (usbtv->udev == NULL)
		return -ENODEV;

	usbtv->last_odd = 1;
	usbtv->sequence = 0;
	return usbtv_start(usbtv);
}

static void usbtv_stop_streaming(struct vb2_queue *vq)
{
	struct usbtv *usbtv = vb2_get_drv_priv(vq);

	if (usbtv->udev)
		usbtv_stop(usbtv);
}

static const struct vb2_ops usbtv_vb2_ops = {
	.queue_setup = usbtv_queue_setup,
	.buf_queue = usbtv_buf_queue,
	.start_streaming = usbtv_start_streaming,
	.stop_streaming = usbtv_stop_streaming,
};

static int usbtv_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct usbtv *usbtv = container_of(ctrl->handler, struct usbtv,
								ctrl);
	u8 *data;
	u16 index, size;
	int ret;

	data = kmalloc(3, GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	/*
	 * Read in the current brightness/contrast registers. We need them
	 * both, because the values are for some reason interleaved.
	 */
	if (ctrl->id == V4L2_CID_BRIGHTNESS || ctrl->id == V4L2_CID_CONTRAST) {
		ret = usb_control_msg(usbtv->udev,
			usb_sndctrlpipe(usbtv->udev, 0), USBTV_CONTROL_REG,
			USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
			0, USBTV_BASE + 0x0244, (void *)data, 3, 0);
		if (ret < 0)
			goto error;
	}

	switch (ctrl->id) {
	case V4L2_CID_BRIGHTNESS:
		index = USBTV_BASE + 0x0244;
		size = 3;
		data[0] &= 0xf0;
		data[0] |= (ctrl->val >> 8) & 0xf;
		data[2] = ctrl->val & 0xff;
		break;
	case V4L2_CID_CONTRAST:
		index = USBTV_BASE + 0x0244;
		size = 3;
		data[0] &= 0x0f;
		data[0] |= (ctrl->val >> 4) & 0xf0;
		data[1] = ctrl->val & 0xff;
		break;
	case V4L2_CID_SATURATION:
		index = USBTV_BASE + 0x0242;
		data[0] = ctrl->val >> 8;
		data[1] = ctrl->val & 0xff;
		size = 2;
		break;
	case V4L2_CID_HUE:
		index = USBTV_BASE + 0x0240;
		size = 2;
		if (ctrl->val > 0) {
			data[0] = 0x92 + (ctrl->val >> 8);
			data[1] = ctrl->val & 0xff;
		} else {
			data[0] = 0x82 + (-ctrl->val >> 8);
			data[1] = -ctrl->val & 0xff;
		}
		break;
	default:
		kfree(data);
		return -EINVAL;
	}

	ret = usb_control_msg(usbtv->udev, usb_sndctrlpipe(usbtv->udev, 0),
			USBTV_CONTROL_REG,
			USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
			0, index, (void *)data, size, 0);

error:
	if (ret < 0)
		dev_warn(usbtv->dev, "Failed to submit a control request.\n");

	kfree(data);
	return ret;
}

static const struct v4l2_ctrl_ops usbtv_ctrl_ops = {
	.s_ctrl = usbtv_s_ctrl,
};

static void usbtv_release(struct v4l2_device *v4l2_dev)
{
	struct usbtv *usbtv = container_of(v4l2_dev, struct usbtv, v4l2_dev);

	v4l2_device_unregister(&usbtv->v4l2_dev);
	v4l2_ctrl_handler_free(&usbtv->ctrl);
	vb2_queue_release(&usbtv->vb2q);
	kfree(usbtv);
}

int usbtv_video_init(struct usbtv *usbtv)
{
	int ret;

	(void)usbtv_configure_for_norm(usbtv, V4L2_STD_525_60);

	spin_lock_init(&usbtv->buflock);
	mutex_init(&usbtv->v4l2_lock);
	mutex_init(&usbtv->vb2q_lock);
	INIT_LIST_HEAD(&usbtv->bufs);

	/* videobuf2 structure */
	usbtv->vb2q.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	usbtv->vb2q.io_modes = VB2_MMAP | VB2_USERPTR | VB2_READ;
	usbtv->vb2q.drv_priv = usbtv;
	usbtv->vb2q.buf_struct_size = sizeof(struct usbtv_buf);
	usbtv->vb2q.ops = &usbtv_vb2_ops;
	usbtv->vb2q.mem_ops = &vb2_vmalloc_memops;
	usbtv->vb2q.timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
	usbtv->vb2q.lock = &usbtv->vb2q_lock;
	ret = vb2_queue_init(&usbtv->vb2q);
	if (ret < 0) {
		dev_warn(usbtv->dev, "Could not initialize videobuf2 queue\n");
		return ret;
	}

	/* controls */
	v4l2_ctrl_handler_init(&usbtv->ctrl, 4);
	v4l2_ctrl_new_std(&usbtv->ctrl, &usbtv_ctrl_ops,
			V4L2_CID_CONTRAST, 0, 0x3ff, 1, 0x1d0);
	v4l2_ctrl_new_std(&usbtv->ctrl, &usbtv_ctrl_ops,
			V4L2_CID_BRIGHTNESS, 0, 0x3ff, 1, 0x1c0);
	v4l2_ctrl_new_std(&usbtv->ctrl, &usbtv_ctrl_ops,
			V4L2_CID_SATURATION, 0, 0x3ff, 1, 0x200);
	v4l2_ctrl_new_std(&usbtv->ctrl, &usbtv_ctrl_ops,
			V4L2_CID_HUE, -0xdff, 0xdff, 1, 0x000);
	ret = usbtv->ctrl.error;
	if (ret < 0) {
		dev_warn(usbtv->dev, "Could not initialize controls\n");
		goto ctrl_fail;
	}

	/* v4l2 structure */
	usbtv->v4l2_dev.ctrl_handler = &usbtv->ctrl;
	usbtv->v4l2_dev.release = usbtv_release;
	ret = v4l2_device_register(usbtv->dev, &usbtv->v4l2_dev);
	if (ret < 0) {
		dev_warn(usbtv->dev, "Could not register v4l2 device\n");
		goto v4l2_fail;
	}

	/* Video structure */
	strlcpy(usbtv->vdev.name, "usbtv", sizeof(usbtv->vdev.name));
	usbtv->vdev.v4l2_dev = &usbtv->v4l2_dev;
	usbtv->vdev.release = video_device_release_empty;
	usbtv->vdev.fops = &usbtv_fops;
	usbtv->vdev.ioctl_ops = &usbtv_ioctl_ops;
	usbtv->vdev.tvnorms = USBTV_TV_STD;
	usbtv->vdev.queue = &usbtv->vb2q;
	usbtv->vdev.lock = &usbtv->v4l2_lock;
	video_set_drvdata(&usbtv->vdev, usbtv);
	ret = video_register_device(&usbtv->vdev, VFL_TYPE_GRABBER, -1);
	if (ret < 0) {
		dev_warn(usbtv->dev, "Could not register video device\n");
		goto vdev_fail;
	}

	return 0;

vdev_fail:
	v4l2_device_unregister(&usbtv->v4l2_dev);
v4l2_fail:
ctrl_fail:
	v4l2_ctrl_handler_free(&usbtv->ctrl);
	vb2_queue_release(&usbtv->vb2q);

	return ret;
}

void usbtv_video_free(struct usbtv *usbtv)
{
	mutex_lock(&usbtv->vb2q_lock);
	mutex_lock(&usbtv->v4l2_lock);

	usbtv_stop(usbtv);
	video_unregister_device(&usbtv->vdev);
	v4l2_device_disconnect(&usbtv->v4l2_dev);

	mutex_unlock(&usbtv->v4l2_lock);
	mutex_unlock(&usbtv->vb2q_lock);

	v4l2_device_put(&usbtv->v4l2_dev);
}
