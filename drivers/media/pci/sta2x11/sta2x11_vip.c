/*
 * This is the driver for the STA2x11 Video Input Port.
 *
 * Copyright (C) 2012       ST Microelectronics
 *     author: Federico Vaga <federico.vaga@gmail.com>
 * Copyright (C) 2010       WindRiver Systems, Inc.
 *     authors: Andreas Kies <andreas.kies@windriver.com>
 *              Vlad Lungu   <vlad.lungu@windriver.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * The full GNU General Public License is included in this distribution in
 * the file called "COPYING".
 *
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/videodev2.h>
#include <linux/kmod.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/gpio.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <media/v4l2-common.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-fh.h>
#include <media/v4l2-event.h>
#include <media/videobuf2-dma-contig.h>

#include "sta2x11_vip.h"

#define DRV_VERSION "1.3"

#ifndef PCI_DEVICE_ID_STMICRO_VIP
#define PCI_DEVICE_ID_STMICRO_VIP 0xCC0D
#endif

#define MAX_FRAMES 4

/*Register offsets*/
#define DVP_CTL		0x00
#define DVP_TFO		0x04
#define DVP_TFS		0x08
#define DVP_BFO		0x0C
#define DVP_BFS		0x10
#define DVP_VTP		0x14
#define DVP_VBP		0x18
#define DVP_VMP		0x1C
#define DVP_ITM		0x98
#define DVP_ITS		0x9C
#define DVP_STA		0xA0
#define DVP_HLFLN	0xA8
#define DVP_RGB		0xC0
#define DVP_PKZ		0xF0

/*Register fields*/
#define DVP_CTL_ENA	0x00000001
#define DVP_CTL_RST	0x80000000
#define DVP_CTL_DIS	(~0x00040001)

#define DVP_IT_VSB	0x00000008
#define DVP_IT_VST	0x00000010
#define DVP_IT_FIFO	0x00000020

#define DVP_HLFLN_SD	0x00000001

#define SAVE_COUNT 8
#define AUX_COUNT 3
#define IRQ_COUNT 1


struct vip_buffer {
	struct vb2_v4l2_buffer vb;
	struct list_head	list;
	dma_addr_t		dma;
};
static inline struct vip_buffer *to_vip_buffer(struct vb2_v4l2_buffer *vb2)
{
	return container_of(vb2, struct vip_buffer, vb);
}

/**
 * struct sta2x11_vip - All internal data for one instance of device
 * @v4l2_dev: device registered in v4l layer
 * @video_dev: properties of our device
 * @pdev: PCI device
 * @adapter: contains I2C adapter information
 * @register_save_area: All relevant register are saved here during suspend
 * @decoder: contains information about video DAC
 * @ctrl_hdl: handler for control framework
 * @format: pixel format, fixed UYVY
 * @std: video standard (e.g. PAL/NTSC)
 * @input: input line for video signal ( 0 or 1 )
 * @disabled: Device is in power down state
 * @slock: for excluse acces of registers
 * @vb_vidq: queue maintained by videobuf2 layer
 * @buffer_list: list of buffer in use
 * @sequence: sequence number of acquired buffer
 * @active: current active buffer
 * @lock: used in videobuf2 callback
 * @tcount: Number of top frames
 * @bcount: Number of bottom frames
 * @overflow: Number of FIFO overflows
 * @iomem: hardware base address
 * @config: I2C and gpio config from platform
 *
 * All non-local data is accessed via this structure.
 */
struct sta2x11_vip {
	struct v4l2_device v4l2_dev;
	struct video_device video_dev;
	struct pci_dev *pdev;
	struct i2c_adapter *adapter;
	unsigned int register_save_area[IRQ_COUNT + SAVE_COUNT + AUX_COUNT];
	struct v4l2_subdev *decoder;
	struct v4l2_ctrl_handler ctrl_hdl;


	struct v4l2_pix_format format;
	v4l2_std_id std;
	unsigned int input;
	int disabled;
	spinlock_t slock;

	struct vb2_queue vb_vidq;
	struct list_head buffer_list;
	unsigned int sequence;
	struct vip_buffer *active; /* current active buffer */
	spinlock_t lock; /* Used in videobuf2 callback */

	/* Interrupt counters */
	int tcount, bcount;
	int overflow;

	void __iomem *iomem;	/* I/O Memory */
	struct vip_config *config;
};

static const unsigned int registers_to_save[AUX_COUNT] = {
	DVP_HLFLN, DVP_RGB, DVP_PKZ
};

static struct v4l2_pix_format formats_50[] = {
	{			/*PAL interlaced */
	 .width = 720,
	 .height = 576,
	 .pixelformat = V4L2_PIX_FMT_UYVY,
	 .field = V4L2_FIELD_INTERLACED,
	 .bytesperline = 720 * 2,
	 .sizeimage = 720 * 2 * 576,
	 .colorspace = V4L2_COLORSPACE_SMPTE170M},
	{			/*PAL top */
	 .width = 720,
	 .height = 288,
	 .pixelformat = V4L2_PIX_FMT_UYVY,
	 .field = V4L2_FIELD_TOP,
	 .bytesperline = 720 * 2,
	 .sizeimage = 720 * 2 * 288,
	 .colorspace = V4L2_COLORSPACE_SMPTE170M},
	{			/*PAL bottom */
	 .width = 720,
	 .height = 288,
	 .pixelformat = V4L2_PIX_FMT_UYVY,
	 .field = V4L2_FIELD_BOTTOM,
	 .bytesperline = 720 * 2,
	 .sizeimage = 720 * 2 * 288,
	 .colorspace = V4L2_COLORSPACE_SMPTE170M},

};

static struct v4l2_pix_format formats_60[] = {
	{			/*NTSC interlaced */
	 .width = 720,
	 .height = 480,
	 .pixelformat = V4L2_PIX_FMT_UYVY,
	 .field = V4L2_FIELD_INTERLACED,
	 .bytesperline = 720 * 2,
	 .sizeimage = 720 * 2 * 480,
	 .colorspace = V4L2_COLORSPACE_SMPTE170M},
	{			/*NTSC top */
	 .width = 720,
	 .height = 240,
	 .pixelformat = V4L2_PIX_FMT_UYVY,
	 .field = V4L2_FIELD_TOP,
	 .bytesperline = 720 * 2,
	 .sizeimage = 720 * 2 * 240,
	 .colorspace = V4L2_COLORSPACE_SMPTE170M},
	{			/*NTSC bottom */
	 .width = 720,
	 .height = 240,
	 .pixelformat = V4L2_PIX_FMT_UYVY,
	 .field = V4L2_FIELD_BOTTOM,
	 .bytesperline = 720 * 2,
	 .sizeimage = 720 * 2 * 240,
	 .colorspace = V4L2_COLORSPACE_SMPTE170M},
};

/* Write VIP register */
static inline void reg_write(struct sta2x11_vip *vip, unsigned int reg, u32 val)
{
	iowrite32((val), (vip->iomem)+(reg));
}
/* Read VIP register */
static inline u32 reg_read(struct sta2x11_vip *vip, unsigned int reg)
{
	return  ioread32((vip->iomem)+(reg));
}
/* Start DMA acquisition */
static void start_dma(struct sta2x11_vip *vip, struct vip_buffer *vip_buf)
{
	unsigned long offset = 0;

	if (vip->format.field == V4L2_FIELD_INTERLACED)
		offset = vip->format.width * 2;

	spin_lock_irq(&vip->slock);
	/* Enable acquisition */
	reg_write(vip, DVP_CTL, reg_read(vip, DVP_CTL) | DVP_CTL_ENA);
	/* Set Top and Bottom Field memory address */
	reg_write(vip, DVP_VTP, (u32)vip_buf->dma);
	reg_write(vip, DVP_VBP, (u32)vip_buf->dma + offset);
	spin_unlock_irq(&vip->slock);
}

/* Fetch the next buffer to activate */
static void vip_active_buf_next(struct sta2x11_vip *vip)
{
	/* Get the next buffer */
	spin_lock(&vip->lock);
	if (list_empty(&vip->buffer_list)) {/* No available buffer */
		spin_unlock(&vip->lock);
		return;
	}
	vip->active = list_first_entry(&vip->buffer_list,
				       struct vip_buffer,
				       list);
	/* Reset Top and Bottom counter */
	vip->tcount = 0;
	vip->bcount = 0;
	spin_unlock(&vip->lock);
	if (vb2_is_streaming(&vip->vb_vidq)) {	/* streaming is on */
		start_dma(vip, vip->active);	/* start dma capture */
	}
}


/* Videobuf2 Operations */
static int queue_setup(struct vb2_queue *vq,
		       unsigned int *nbuffers, unsigned int *nplanes,
		       unsigned int sizes[], struct device *alloc_devs[])
{
	struct sta2x11_vip *vip = vb2_get_drv_priv(vq);

	if (!(*nbuffers) || *nbuffers < MAX_FRAMES)
		*nbuffers = MAX_FRAMES;

	*nplanes = 1;
	sizes[0] = vip->format.sizeimage;

	vip->sequence = 0;
	vip->active = NULL;
	vip->tcount = 0;
	vip->bcount = 0;

	return 0;
};
static int buffer_init(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct vip_buffer *vip_buf = to_vip_buffer(vbuf);

	vip_buf->dma = vb2_dma_contig_plane_dma_addr(vb, 0);
	INIT_LIST_HEAD(&vip_buf->list);
	return 0;
}

static int buffer_prepare(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct sta2x11_vip *vip = vb2_get_drv_priv(vb->vb2_queue);
	struct vip_buffer *vip_buf = to_vip_buffer(vbuf);
	unsigned long size;

	size = vip->format.sizeimage;
	if (vb2_plane_size(vb, 0) < size) {
		v4l2_err(&vip->v4l2_dev, "buffer too small (%lu < %lu)\n",
			 vb2_plane_size(vb, 0), size);
		return -EINVAL;
	}

	vb2_set_plane_payload(&vip_buf->vb.vb2_buf, 0, size);

	return 0;
}
static void buffer_queue(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct sta2x11_vip *vip = vb2_get_drv_priv(vb->vb2_queue);
	struct vip_buffer *vip_buf = to_vip_buffer(vbuf);

	spin_lock(&vip->lock);
	list_add_tail(&vip_buf->list, &vip->buffer_list);
	if (!vip->active) {	/* No active buffer, active the first one */
		vip->active = list_first_entry(&vip->buffer_list,
					       struct vip_buffer,
					       list);
		if (vb2_is_streaming(&vip->vb_vidq))	/* streaming is on */
			start_dma(vip, vip_buf);	/* start dma capture */
	}
	spin_unlock(&vip->lock);
}
static void buffer_finish(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct sta2x11_vip *vip = vb2_get_drv_priv(vb->vb2_queue);
	struct vip_buffer *vip_buf = to_vip_buffer(vbuf);

	/* Buffer handled, remove it from the list */
	spin_lock(&vip->lock);
	list_del_init(&vip_buf->list);
	spin_unlock(&vip->lock);

	if (vb2_is_streaming(vb->vb2_queue))
		vip_active_buf_next(vip);
}

static int start_streaming(struct vb2_queue *vq, unsigned int count)
{
	struct sta2x11_vip *vip = vb2_get_drv_priv(vq);

	spin_lock_irq(&vip->slock);
	/* Enable interrupt VSYNC Top and Bottom*/
	reg_write(vip, DVP_ITM, DVP_IT_VSB | DVP_IT_VST);
	spin_unlock_irq(&vip->slock);

	if (count)
		start_dma(vip, vip->active);

	return 0;
}

/* abort streaming and wait for last buffer */
static void stop_streaming(struct vb2_queue *vq)
{
	struct sta2x11_vip *vip = vb2_get_drv_priv(vq);
	struct vip_buffer *vip_buf, *node;

	/* Disable acquisition */
	reg_write(vip, DVP_CTL, reg_read(vip, DVP_CTL) & ~DVP_CTL_ENA);
	/* Disable all interrupts */
	reg_write(vip, DVP_ITM, 0);

	/* Release all active buffers */
	spin_lock(&vip->lock);
	list_for_each_entry_safe(vip_buf, node, &vip->buffer_list, list) {
		vb2_buffer_done(&vip_buf->vb.vb2_buf, VB2_BUF_STATE_ERROR);
		list_del(&vip_buf->list);
	}
	spin_unlock(&vip->lock);
}

static const struct vb2_ops vip_video_qops = {
	.queue_setup		= queue_setup,
	.buf_init		= buffer_init,
	.buf_prepare		= buffer_prepare,
	.buf_finish		= buffer_finish,
	.buf_queue		= buffer_queue,
	.start_streaming	= start_streaming,
	.stop_streaming		= stop_streaming,
};


/* File Operations */
static const struct v4l2_file_operations vip_fops = {
	.owner = THIS_MODULE,
	.open = v4l2_fh_open,
	.release = vb2_fop_release,
	.unlocked_ioctl = video_ioctl2,
	.read = vb2_fop_read,
	.mmap = vb2_fop_mmap,
	.poll = vb2_fop_poll
};


/**
 * vidioc_querycap - return capabilities of device
 * @file: descriptor of device
 * @cap: contains return values
 *
 * the capabilities of the device are returned
 *
 * return value: 0, no error.
 */
static int vidioc_querycap(struct file *file, void *priv,
			   struct v4l2_capability *cap)
{
	struct sta2x11_vip *vip = video_drvdata(file);

	strcpy(cap->driver, KBUILD_MODNAME);
	strcpy(cap->card, KBUILD_MODNAME);
	snprintf(cap->bus_info, sizeof(cap->bus_info), "PCI:%s",
		 pci_name(vip->pdev));
	cap->device_caps = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_READWRITE |
			   V4L2_CAP_STREAMING;
	cap->capabilities = cap->device_caps | V4L2_CAP_DEVICE_CAPS;

	return 0;
}

/**
 * vidioc_s_std - set video standard
 * @file: descriptor of device
 * @std: contains standard to be set
 *
 * the video standard is set
 *
 * return value: 0, no error.
 *
 * -EIO, no input signal detected
 *
 * other, returned from video DAC.
 */
static int vidioc_s_std(struct file *file, void *priv, v4l2_std_id std)
{
	struct sta2x11_vip *vip = video_drvdata(file);

	/*
	 * This is here for backwards compatibility only.
	 * The use of V4L2_STD_ALL to trigger a querystd is non-standard.
	 */
	if (std == V4L2_STD_ALL) {
		v4l2_subdev_call(vip->decoder, video, querystd, &std);
		if (std == V4L2_STD_UNKNOWN)
			return -EIO;
	}

	if (vip->std != std) {
		vip->std = std;
		if (V4L2_STD_525_60 & std)
			vip->format = formats_60[0];
		else
			vip->format = formats_50[0];
	}

	return v4l2_subdev_call(vip->decoder, video, s_std, std);
}

/**
 * vidioc_g_std - get video standard
 * @file: descriptor of device
 * @std: contains return values
 *
 * the current video standard is returned
 *
 * return value: 0, no error.
 */
static int vidioc_g_std(struct file *file, void *priv, v4l2_std_id *std)
{
	struct sta2x11_vip *vip = video_drvdata(file);

	*std = vip->std;
	return 0;
}

/**
 * vidioc_querystd - get possible video standards
 * @file: descriptor of device
 * @std: contains return values
 *
 * all possible video standards are returned
 *
 * return value: delivered by video DAC routine.
 */
static int vidioc_querystd(struct file *file, void *priv, v4l2_std_id *std)
{
	struct sta2x11_vip *vip = video_drvdata(file);

	return v4l2_subdev_call(vip->decoder, video, querystd, std);
}

static int vidioc_enum_input(struct file *file, void *priv,
			     struct v4l2_input *inp)
{
	if (inp->index > 1)
		return -EINVAL;

	inp->type = V4L2_INPUT_TYPE_CAMERA;
	inp->std = V4L2_STD_ALL;
	sprintf(inp->name, "Camera %u", inp->index);

	return 0;
}

/**
 * vidioc_s_input - set input line
 * @file: descriptor of device
 * @i: new input line number
 *
 * the current active input line is set
 *
 * return value: 0, no error.
 *
 * -EINVAL, line number out of range
 */
static int vidioc_s_input(struct file *file, void *priv, unsigned int i)
{
	struct sta2x11_vip *vip = video_drvdata(file);
	int ret;

	if (i > 1)
		return -EINVAL;
	ret = v4l2_subdev_call(vip->decoder, video, s_routing, i, 0, 0);

	if (!ret)
		vip->input = i;

	return 0;
}

/**
 * vidioc_g_input - return input line
 * @file: descriptor of device
 * @i: returned input line number
 *
 * the current active input line is returned
 *
 * return value: always 0.
 */
static int vidioc_g_input(struct file *file, void *priv, unsigned int *i)
{
	struct sta2x11_vip *vip = video_drvdata(file);

	*i = vip->input;
	return 0;
}

/**
 * vidioc_enum_fmt_vid_cap - return video capture format
 * @f: returned format information
 *
 * returns name and format of video capture
 * Only UYVY is supported by hardware.
 *
 * return value: always 0.
 */
static int vidioc_enum_fmt_vid_cap(struct file *file, void *priv,
				   struct v4l2_fmtdesc *f)
{

	if (f->index != 0)
		return -EINVAL;

	strcpy(f->description, "4:2:2, packed, UYVY");
	f->pixelformat = V4L2_PIX_FMT_UYVY;
	f->flags = 0;
	return 0;
}

/**
 * vidioc_try_fmt_vid_cap - set video capture format
 * @file: descriptor of device
 * @f: new format
 *
 * new video format is set which includes width and
 * field type. width is fixed to 720, no scaling.
 * Only UYVY is supported by this hardware.
 * the minimum height is 200, the maximum is 576 (PAL)
 *
 * return value: 0, no error
 *
 * -EINVAL, pixel or field format not supported
 *
 */
static int vidioc_try_fmt_vid_cap(struct file *file, void *priv,
				  struct v4l2_format *f)
{
	struct sta2x11_vip *vip = video_drvdata(file);
	int interlace_lim;

	if (V4L2_PIX_FMT_UYVY != f->fmt.pix.pixelformat) {
		v4l2_warn(&vip->v4l2_dev, "Invalid format, only UYVY supported\n");
		return -EINVAL;
	}

	if (V4L2_STD_525_60 & vip->std)
		interlace_lim = 240;
	else
		interlace_lim = 288;

	switch (f->fmt.pix.field) {
	default:
	case V4L2_FIELD_ANY:
		if (interlace_lim < f->fmt.pix.height)
			f->fmt.pix.field = V4L2_FIELD_INTERLACED;
		else
			f->fmt.pix.field = V4L2_FIELD_BOTTOM;
		break;
	case V4L2_FIELD_TOP:
	case V4L2_FIELD_BOTTOM:
		if (interlace_lim < f->fmt.pix.height)
			f->fmt.pix.height = interlace_lim;
		break;
	case V4L2_FIELD_INTERLACED:
		break;
	}

	/* It is the only supported format */
	f->fmt.pix.pixelformat = V4L2_PIX_FMT_UYVY;
	f->fmt.pix.height &= ~1;
	if (2 * interlace_lim < f->fmt.pix.height)
		f->fmt.pix.height = 2 * interlace_lim;
	if (200 > f->fmt.pix.height)
		f->fmt.pix.height = 200;
	f->fmt.pix.width = 720;
	f->fmt.pix.bytesperline = f->fmt.pix.width * 2;
	f->fmt.pix.sizeimage = f->fmt.pix.width * 2 * f->fmt.pix.height;
	f->fmt.pix.colorspace = V4L2_COLORSPACE_SMPTE170M;
	return 0;
}

/**
 * vidioc_s_fmt_vid_cap - set current video format parameters
 * @file: descriptor of device
 * @f: returned format information
 *
 * set new capture format
 * return value: 0, no error
 *
 * other, delivered by video DAC routine.
 */
static int vidioc_s_fmt_vid_cap(struct file *file, void *priv,
				struct v4l2_format *f)
{
	struct sta2x11_vip *vip = video_drvdata(file);
	unsigned int t_stop, b_stop, pitch;
	int ret;

	ret = vidioc_try_fmt_vid_cap(file, priv, f);
	if (ret)
		return ret;

	if (vb2_is_busy(&vip->vb_vidq)) {
		/* Can't change format during acquisition */
		v4l2_err(&vip->v4l2_dev, "device busy\n");
		return -EBUSY;
	}
	vip->format = f->fmt.pix;
	switch (vip->format.field) {
	case V4L2_FIELD_INTERLACED:
		t_stop = ((vip->format.height / 2 - 1) << 16) |
			 (2 * vip->format.width - 1);
		b_stop = t_stop;
		pitch = 4 * vip->format.width;
		break;
	case V4L2_FIELD_TOP:
		t_stop = ((vip->format.height - 1) << 16) |
			 (2 * vip->format.width - 1);
		b_stop = (0 << 16) | (2 * vip->format.width - 1);
		pitch = 2 * vip->format.width;
		break;
	case V4L2_FIELD_BOTTOM:
		t_stop = (0 << 16) | (2 * vip->format.width - 1);
		b_stop = (vip->format.height << 16) |
			 (2 * vip->format.width - 1);
		pitch = 2 * vip->format.width;
		break;
	default:
		v4l2_err(&vip->v4l2_dev, "unknown field format\n");
		return -EINVAL;
	}

	spin_lock_irq(&vip->slock);
	/* Y-X Top Field Offset */
	reg_write(vip, DVP_TFO, 0);
	/* Y-X Bottom Field Offset */
	reg_write(vip, DVP_BFO, 0);
	/* Y-X Top Field Stop*/
	reg_write(vip, DVP_TFS, t_stop);
	/* Y-X Bottom Field Stop */
	reg_write(vip, DVP_BFS, b_stop);
	/* Video Memory Pitch */
	reg_write(vip, DVP_VMP, pitch);
	spin_unlock_irq(&vip->slock);

	return 0;
}

/**
 * vidioc_g_fmt_vid_cap - get current video format parameters
 * @file: descriptor of device
 * @f: contains format information
 *
 * returns current video format parameters
 *
 * return value: 0, always successful
 */
static int vidioc_g_fmt_vid_cap(struct file *file, void *priv,
				struct v4l2_format *f)
{
	struct sta2x11_vip *vip = video_drvdata(file);

	f->fmt.pix = vip->format;

	return 0;
}

static const struct v4l2_ioctl_ops vip_ioctl_ops = {
	.vidioc_querycap = vidioc_querycap,
	/* FMT handling */
	.vidioc_enum_fmt_vid_cap = vidioc_enum_fmt_vid_cap,
	.vidioc_g_fmt_vid_cap = vidioc_g_fmt_vid_cap,
	.vidioc_s_fmt_vid_cap = vidioc_s_fmt_vid_cap,
	.vidioc_try_fmt_vid_cap = vidioc_try_fmt_vid_cap,
	/* Buffer handlers */
	.vidioc_create_bufs = vb2_ioctl_create_bufs,
	.vidioc_prepare_buf = vb2_ioctl_prepare_buf,
	.vidioc_reqbufs = vb2_ioctl_reqbufs,
	.vidioc_querybuf = vb2_ioctl_querybuf,
	.vidioc_qbuf = vb2_ioctl_qbuf,
	.vidioc_dqbuf = vb2_ioctl_dqbuf,
	/* Stream on/off */
	.vidioc_streamon = vb2_ioctl_streamon,
	.vidioc_streamoff = vb2_ioctl_streamoff,
	/* Standard handling */
	.vidioc_g_std = vidioc_g_std,
	.vidioc_s_std = vidioc_s_std,
	.vidioc_querystd = vidioc_querystd,
	/* Input handling */
	.vidioc_enum_input = vidioc_enum_input,
	.vidioc_g_input = vidioc_g_input,
	.vidioc_s_input = vidioc_s_input,
	/* Log status ioctl */
	.vidioc_log_status = v4l2_ctrl_log_status,
	/* Event handling */
	.vidioc_subscribe_event = v4l2_ctrl_subscribe_event,
	.vidioc_unsubscribe_event = v4l2_event_unsubscribe,
};

static const struct video_device video_dev_template = {
	.name = KBUILD_MODNAME,
	.release = video_device_release_empty,
	.fops = &vip_fops,
	.ioctl_ops = &vip_ioctl_ops,
	.tvnorms = V4L2_STD_ALL,
};

/**
 * vip_irq - interrupt routine
 * @irq: Number of interrupt ( not used, correct number is assumed )
 * @vip: local data structure containing all information
 *
 * check for both frame interrupts set ( top and bottom ).
 * check FIFO overflow, but limit number of log messages after open.
 * signal a complete buffer if done
 *
 * return value: IRQ_NONE, interrupt was not generated by VIP
 *
 * IRQ_HANDLED, interrupt done.
 */
static irqreturn_t vip_irq(int irq, struct sta2x11_vip *vip)
{
	unsigned int status;

	status = reg_read(vip, DVP_ITS);

	if (!status)		/* No interrupt to handle */
		return IRQ_NONE;

	if (status & DVP_IT_FIFO)
		if (vip->overflow++ > 5)
			pr_info("VIP: fifo overflow\n");

	if ((status & DVP_IT_VST) && (status & DVP_IT_VSB)) {
		/* this is bad, we are too slow, hope the condition is gone
		 * on the next frame */
		return IRQ_HANDLED;
	}

	if (status & DVP_IT_VST)
		if ((++vip->tcount) < 2)
			return IRQ_HANDLED;
	if (status & DVP_IT_VSB) {
		vip->bcount++;
		return IRQ_HANDLED;
	}

	if (vip->active) { /* Acquisition is over on this buffer */
		/* Disable acquisition */
		reg_write(vip, DVP_CTL, reg_read(vip, DVP_CTL) & ~DVP_CTL_ENA);
		/* Remove the active buffer from the list */
		vip->active->vb.vb2_buf.timestamp = ktime_get_ns();
		vip->active->vb.sequence = vip->sequence++;
		vb2_buffer_done(&vip->active->vb.vb2_buf, VB2_BUF_STATE_DONE);
	}

	return IRQ_HANDLED;
}

static void sta2x11_vip_init_register(struct sta2x11_vip *vip)
{
	/* Register initialization */
	spin_lock_irq(&vip->slock);
	/* Clean interrupt */
	reg_read(vip, DVP_ITS);
	/* Enable Half Line per vertical */
	reg_write(vip, DVP_HLFLN, DVP_HLFLN_SD);
	/* Reset VIP control */
	reg_write(vip, DVP_CTL, DVP_CTL_RST);
	/* Clear VIP control */
	reg_write(vip, DVP_CTL, 0);
	spin_unlock_irq(&vip->slock);
}
static void sta2x11_vip_clear_register(struct sta2x11_vip *vip)
{
	spin_lock_irq(&vip->slock);
	/* Disable interrupt */
	reg_write(vip, DVP_ITM, 0);
	/* Reset VIP Control */
	reg_write(vip, DVP_CTL, DVP_CTL_RST);
	/* Clear VIP Control */
	reg_write(vip, DVP_CTL, 0);
	/* Clean VIP Interrupt */
	reg_read(vip, DVP_ITS);
	spin_unlock_irq(&vip->slock);
}
static int sta2x11_vip_init_buffer(struct sta2x11_vip *vip)
{
	int err;

	err = dma_set_coherent_mask(&vip->pdev->dev, DMA_BIT_MASK(29));
	if (err) {
		v4l2_err(&vip->v4l2_dev, "Cannot configure coherent mask");
		return err;
	}
	memset(&vip->vb_vidq, 0, sizeof(struct vb2_queue));
	vip->vb_vidq.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	vip->vb_vidq.io_modes = VB2_MMAP | VB2_READ;
	vip->vb_vidq.drv_priv = vip;
	vip->vb_vidq.buf_struct_size = sizeof(struct vip_buffer);
	vip->vb_vidq.ops = &vip_video_qops;
	vip->vb_vidq.mem_ops = &vb2_dma_contig_memops;
	vip->vb_vidq.timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
	vip->vb_vidq.dev = &vip->pdev->dev;
	err = vb2_queue_init(&vip->vb_vidq);
	if (err)
		return err;
	INIT_LIST_HEAD(&vip->buffer_list);
	spin_lock_init(&vip->lock);
	return 0;
}

static int sta2x11_vip_init_controls(struct sta2x11_vip *vip)
{
	/*
	 * Inititialize an empty control so VIP can inerithing controls
	 * from ADV7180
	 */
	v4l2_ctrl_handler_init(&vip->ctrl_hdl, 0);

	vip->v4l2_dev.ctrl_handler = &vip->ctrl_hdl;
	if (vip->ctrl_hdl.error) {
		int err = vip->ctrl_hdl.error;

		v4l2_ctrl_handler_free(&vip->ctrl_hdl);
		return err;
	}

	return 0;
}

/**
 * vip_gpio_reserve - reserve gpio pin
 * @dev: device
 * @pin: GPIO pin number
 * @dir: direction, input or output
 * @name: GPIO pin name
 *
 */
static int vip_gpio_reserve(struct device *dev, int pin, int dir,
			    const char *name)
{
	int ret;

	if (pin == -1)
		return 0;

	ret = gpio_request(pin, name);
	if (ret) {
		dev_err(dev, "Failed to allocate pin %d (%s)\n", pin, name);
		return ret;
	}

	ret = gpio_direction_output(pin, dir);
	if (ret) {
		dev_err(dev, "Failed to set direction for pin %d (%s)\n",
			pin, name);
		gpio_free(pin);
		return ret;
	}

	ret = gpio_export(pin, false);
	if (ret) {
		dev_err(dev, "Failed to export pin %d (%s)\n", pin, name);
		gpio_free(pin);
		return ret;
	}

	return 0;
}

/**
 * vip_gpio_release - release gpio pin
 * @dev: device
 * @pin: GPIO pin number
 * @name: GPIO pin name
 *
 */
static void vip_gpio_release(struct device *dev, int pin, const char *name)
{
	if (pin != -1) {
		dev_dbg(dev, "releasing pin %d (%s)\n",	pin, name);
		gpio_unexport(pin);
		gpio_free(pin);
	}
}

/**
 * sta2x11_vip_init_one - init one instance of video device
 * @pdev: PCI device
 * @ent: (not used)
 *
 * allocate reset pins for DAC.
 * Reset video DAC, this is done via reset line.
 * allocate memory for managing device
 * request interrupt
 * map IO region
 * register device
 * find and initialize video DAC
 *
 * return value: 0, no error
 *
 * -ENOMEM, no memory
 *
 * -ENODEV, device could not be detected or registered
 */
static int sta2x11_vip_init_one(struct pci_dev *pdev,
				const struct pci_device_id *ent)
{
	int ret;
	struct sta2x11_vip *vip;
	struct vip_config *config;

	/* Check if hardware support 26-bit DMA */
	if (dma_set_mask(&pdev->dev, DMA_BIT_MASK(26))) {
		dev_err(&pdev->dev, "26-bit DMA addressing not available\n");
		return -EINVAL;
	}
	/* Enable PCI */
	ret = pci_enable_device(pdev);
	if (ret)
		return ret;

	/* Get VIP platform data */
	config = dev_get_platdata(&pdev->dev);
	if (!config) {
		dev_info(&pdev->dev, "VIP slot disabled\n");
		ret = -EINVAL;
		goto disable;
	}

	/* Power configuration */
	ret = vip_gpio_reserve(&pdev->dev, config->pwr_pin, 0,
			       config->pwr_name);
	if (ret)
		goto disable;

	if (config->reset_pin >= 0) {
		ret = vip_gpio_reserve(&pdev->dev, config->reset_pin, 0,
				       config->reset_name);
		if (ret) {
			vip_gpio_release(&pdev->dev, config->pwr_pin,
					 config->pwr_name);
			goto disable;
		}
	}
	if (config->pwr_pin != -1) {
		/* Datasheet says 5ms between PWR and RST */
		usleep_range(5000, 25000);
		ret = gpio_direction_output(config->pwr_pin, 1);
	}

	if (config->reset_pin != -1) {
		/* Datasheet says 5ms between PWR and RST */
		usleep_range(5000, 25000);
		ret = gpio_direction_output(config->reset_pin, 1);
	}
	usleep_range(5000, 25000);

	/* Allocate a new VIP instance */
	vip = kzalloc(sizeof(struct sta2x11_vip), GFP_KERNEL);
	if (!vip) {
		ret = -ENOMEM;
		goto release_gpios;
	}
	vip->pdev = pdev;
	vip->std = V4L2_STD_PAL;
	vip->format = formats_50[0];
	vip->config = config;

	ret = sta2x11_vip_init_controls(vip);
	if (ret)
		goto free_mem;
	ret = v4l2_device_register(&pdev->dev, &vip->v4l2_dev);
	if (ret)
		goto free_mem;

	dev_dbg(&pdev->dev, "BAR #0 at 0x%lx 0x%lx irq %d\n",
		(unsigned long)pci_resource_start(pdev, 0),
		(unsigned long)pci_resource_len(pdev, 0), pdev->irq);

	pci_set_master(pdev);

	ret = pci_request_regions(pdev, KBUILD_MODNAME);
	if (ret)
		goto unreg;

	vip->iomem = pci_iomap(pdev, 0, 0x100);
	if (!vip->iomem) {
		ret = -ENOMEM;
		goto release;
	}

	pci_enable_msi(pdev);

	/* Initialize buffer */
	ret = sta2x11_vip_init_buffer(vip);
	if (ret)
		goto unmap;

	spin_lock_init(&vip->slock);

	ret = request_irq(pdev->irq,
			  (irq_handler_t) vip_irq,
			  IRQF_SHARED, KBUILD_MODNAME, vip);
	if (ret) {
		dev_err(&pdev->dev, "request_irq failed\n");
		ret = -ENODEV;
		goto release_buf;
	}

	/* Initialize and register video device */
	vip->video_dev = video_dev_template;
	vip->video_dev.v4l2_dev = &vip->v4l2_dev;
	vip->video_dev.queue = &vip->vb_vidq;
	video_set_drvdata(&vip->video_dev, vip);

	ret = video_register_device(&vip->video_dev, VFL_TYPE_GRABBER, -1);
	if (ret)
		goto vrelease;

	/* Get ADV7180 subdevice */
	vip->adapter = i2c_get_adapter(vip->config->i2c_id);
	if (!vip->adapter) {
		ret = -ENODEV;
		dev_err(&pdev->dev, "no I2C adapter found\n");
		goto vunreg;
	}

	vip->decoder = v4l2_i2c_new_subdev(&vip->v4l2_dev, vip->adapter,
					   "adv7180", vip->config->i2c_addr,
					   NULL);
	if (!vip->decoder) {
		ret = -ENODEV;
		dev_err(&pdev->dev, "no decoder found\n");
		goto vunreg;
	}

	i2c_put_adapter(vip->adapter);
	v4l2_subdev_call(vip->decoder, core, init, 0);

	sta2x11_vip_init_register(vip);

	dev_info(&pdev->dev, "STA2X11 Video Input Port (VIP) loaded\n");
	return 0;

vunreg:
	video_set_drvdata(&vip->video_dev, NULL);
vrelease:
	video_unregister_device(&vip->video_dev);
	free_irq(pdev->irq, vip);
release_buf:
	pci_disable_msi(pdev);
unmap:
	vb2_queue_release(&vip->vb_vidq);
	pci_iounmap(pdev, vip->iomem);
release:
	pci_release_regions(pdev);
unreg:
	v4l2_device_unregister(&vip->v4l2_dev);
free_mem:
	kfree(vip);
release_gpios:
	vip_gpio_release(&pdev->dev, config->reset_pin, config->reset_name);
	vip_gpio_release(&pdev->dev, config->pwr_pin, config->pwr_name);
disable:
	/*
	 * do not call pci_disable_device on sta2x11 because it break all
	 * other Bus masters on this EP
	 */
	return ret;
}

/**
 * sta2x11_vip_remove_one - release device
 * @pdev: PCI device
 *
 * Undo everything done in .._init_one
 *
 * unregister video device
 * free interrupt
 * unmap ioadresses
 * free memory
 * free GPIO pins
 */
static void sta2x11_vip_remove_one(struct pci_dev *pdev)
{
	struct v4l2_device *v4l2_dev = pci_get_drvdata(pdev);
	struct sta2x11_vip *vip =
	    container_of(v4l2_dev, struct sta2x11_vip, v4l2_dev);

	sta2x11_vip_clear_register(vip);

	video_set_drvdata(&vip->video_dev, NULL);
	video_unregister_device(&vip->video_dev);
	free_irq(pdev->irq, vip);
	pci_disable_msi(pdev);
	vb2_queue_release(&vip->vb_vidq);
	pci_iounmap(pdev, vip->iomem);
	pci_release_regions(pdev);

	v4l2_device_unregister(&vip->v4l2_dev);

	vip_gpio_release(&pdev->dev, vip->config->pwr_pin,
			 vip->config->pwr_name);
	vip_gpio_release(&pdev->dev, vip->config->reset_pin,
			 vip->config->reset_name);

	kfree(vip);
	/*
	 * do not call pci_disable_device on sta2x11 because it break all
	 * other Bus masters on this EP
	 */
}

#ifdef CONFIG_PM

/**
 * sta2x11_vip_suspend - set device into power save mode
 * @pdev: PCI device
 * @state: new state of device
 *
 * all relevant registers are saved and an attempt to set a new state is made.
 *
 * return value: 0 always indicate success,
 * even if device could not be disabled. (workaround for hardware problem)
 */
static int sta2x11_vip_suspend(struct pci_dev *pdev, pm_message_t state)
{
	struct v4l2_device *v4l2_dev = pci_get_drvdata(pdev);
	struct sta2x11_vip *vip =
	    container_of(v4l2_dev, struct sta2x11_vip, v4l2_dev);
	unsigned long flags;
	int i;

	spin_lock_irqsave(&vip->slock, flags);
	vip->register_save_area[0] = reg_read(vip, DVP_CTL);
	reg_write(vip, DVP_CTL, vip->register_save_area[0] & DVP_CTL_DIS);
	vip->register_save_area[SAVE_COUNT] = reg_read(vip, DVP_ITM);
	reg_write(vip, DVP_ITM, 0);
	for (i = 1; i < SAVE_COUNT; i++)
		vip->register_save_area[i] = reg_read(vip, 4 * i);
	for (i = 0; i < AUX_COUNT; i++)
		vip->register_save_area[SAVE_COUNT + IRQ_COUNT + i] =
		    reg_read(vip, registers_to_save[i]);
	spin_unlock_irqrestore(&vip->slock, flags);
	/* save pci state */
	pci_save_state(pdev);
	if (pci_set_power_state(pdev, pci_choose_state(pdev, state))) {
		/*
		 * do not call pci_disable_device on sta2x11 because it
		 * break all other Bus masters on this EP
		 */
		vip->disabled = 1;
	}

	pr_info("VIP: suspend\n");
	return 0;
}

/**
 * sta2x11_vip_resume - resume device operation
 * @pdev : PCI device
 *
 * re-enable device, set PCI state to powered and restore registers.
 * resume normal device operation afterwards.
 *
 * return value: 0, no error.
 *
 * other, could not set device to power on state.
 */
static int sta2x11_vip_resume(struct pci_dev *pdev)
{
	struct v4l2_device *v4l2_dev = pci_get_drvdata(pdev);
	struct sta2x11_vip *vip =
	    container_of(v4l2_dev, struct sta2x11_vip, v4l2_dev);
	unsigned long flags;
	int ret, i;

	pr_info("VIP: resume\n");
	/* restore pci state */
	if (vip->disabled) {
		ret = pci_enable_device(pdev);
		if (ret) {
			pr_warn("VIP: Can't enable device.\n");
			return ret;
		}
		vip->disabled = 0;
	}
	ret = pci_set_power_state(pdev, PCI_D0);
	if (ret) {
		/*
		 * do not call pci_disable_device on sta2x11 because it
		 * break all other Bus masters on this EP
		 */
		pr_warn("VIP: Can't enable device.\n");
		vip->disabled = 1;
		return ret;
	}

	pci_restore_state(pdev);

	spin_lock_irqsave(&vip->slock, flags);
	for (i = 1; i < SAVE_COUNT; i++)
		reg_write(vip, 4 * i, vip->register_save_area[i]);
	for (i = 0; i < AUX_COUNT; i++)
		reg_write(vip, registers_to_save[i],
			  vip->register_save_area[SAVE_COUNT + IRQ_COUNT + i]);
	reg_write(vip, DVP_CTL, vip->register_save_area[0]);
	reg_write(vip, DVP_ITM, vip->register_save_area[SAVE_COUNT]);
	spin_unlock_irqrestore(&vip->slock, flags);
	return 0;
}

#endif

static const struct pci_device_id sta2x11_vip_pci_tbl[] = {
	{PCI_DEVICE(PCI_VENDOR_ID_STMICRO, PCI_DEVICE_ID_STMICRO_VIP)},
	{0,}
};

static struct pci_driver sta2x11_vip_driver = {
	.name = KBUILD_MODNAME,
	.probe = sta2x11_vip_init_one,
	.remove = sta2x11_vip_remove_one,
	.id_table = sta2x11_vip_pci_tbl,
#ifdef CONFIG_PM
	.suspend = sta2x11_vip_suspend,
	.resume = sta2x11_vip_resume,
#endif
};

static int __init sta2x11_vip_init_module(void)
{
	return pci_register_driver(&sta2x11_vip_driver);
}

static void __exit sta2x11_vip_exit_module(void)
{
	pci_unregister_driver(&sta2x11_vip_driver);
}

#ifdef MODULE
module_init(sta2x11_vip_init_module);
module_exit(sta2x11_vip_exit_module);
#else
late_initcall_sync(sta2x11_vip_init_module);
#endif

MODULE_DESCRIPTION("STA2X11 Video Input Port driver");
MODULE_AUTHOR("Wind River");
MODULE_LICENSE("GPL v2");
MODULE_SUPPORTED_DEVICE("sta2x11 video input");
MODULE_VERSION(DRV_VERSION);
MODULE_DEVICE_TABLE(pci, sta2x11_vip_pci_tbl);
