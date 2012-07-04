/*
 * This is the driver for the STA2x11 Video Input Port.
 *
 * Copyright (C) 2010       WindRiver Systems, Inc.
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
 * Author: Andreas Kies <andreas.kies@windriver.com>
 *		Vlad Lungu <vlad.lungu@windriver.com>
 *
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/vmalloc.h>

#include <linux/videodev2.h>

#include <linux/kmod.h>

#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/mutex.h>
#include <linux/io.h>
#include <linux/gpio.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <media/v4l2-common.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ioctl.h>
#include <media/videobuf-dma-contig.h>

#include "sta2x11_vip.h"

#define DRV_NAME "sta2x11_vip"
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
#define DVP_VTP         0x14
#define DVP_VBP         0x18
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

#define REG_WRITE(vip, reg, value) iowrite32((value), (vip->iomem)+(reg))
#define REG_READ(vip, reg) ioread32((vip->iomem)+(reg))

#define SAVE_COUNT 8
#define AUX_COUNT 3
#define IRQ_COUNT 1

/**
 * struct sta2x11_vip - All internal data for one instance of device
 * @v4l2_dev: device registered in v4l layer
 * @video_dev: properties of our device
 * @pdev: PCI device
 * @adapter: contains I2C adapter information
 * @register_save_area: All relevant register are saved here during suspend
 * @decoder: contains information about video DAC
 * @format: pixel format, fixed UYVY
 * @std: video standard (e.g. PAL/NTSC)
 * @input: input line for video signal ( 0 or 1 )
 * @users: Number of open of device ( max. 1 )
 * @disabled: Device is in power down state
 * @mutex: ensures exclusive opening of device
 * @slock: for excluse acces of registers
 * @vb_vidq: queue maintained by videobuf layer
 * @capture: linked list of capture buffer
 * @active: struct videobuf_buffer currently beingg filled
 * @started: device is ready to capture frame
 * @closing: device will be shut down
 * @tcount: Number of top frames
 * @bcount: Number of bottom frames
 * @overflow: Number of FIFO overflows
 * @mem_spare: small buffer of unused frame
 * @dma_spare: dma addres of mem_spare
 * @iomem: hardware base address
 * @config: I2C and gpio config from platform
 *
 * All non-local data is accessed via this structure.
 */

struct sta2x11_vip {
	struct v4l2_device v4l2_dev;
	struct video_device *video_dev;
	struct pci_dev *pdev;
	struct i2c_adapter *adapter;
	unsigned int register_save_area[IRQ_COUNT + SAVE_COUNT + AUX_COUNT];
	struct v4l2_subdev *decoder;
	struct v4l2_pix_format format;
	v4l2_std_id std;
	unsigned int input;
	int users;
	int disabled;
	struct mutex mutex;	/* exclusive access during open */
	spinlock_t slock;	/* spin lock for hardware and queue access */
	struct videobuf_queue vb_vidq;
	struct list_head capture;
	struct videobuf_buffer *active;
	int started, closing, tcount, bcount;
	int overflow;
	void *mem_spare;
	dma_addr_t dma_spare;
	void *iomem;
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

/**
 * buf_setup - Get size and number of video buffer
 * @vq: queue in videobuf
 * @count: Number of buffers (1..MAX_FRAMES).
 *		0 use default value.
 * @size:  size of buffer in bytes
 *
 * returns size and number of buffers
 * a preset value of 0 returns the default number.
 * return value: 0, always succesfull.
 */
static int buf_setup(struct videobuf_queue *vq, unsigned int *count,
		     unsigned int *size)
{
	struct sta2x11_vip *vip = vq->priv_data;

	*size = vip->format.width * vip->format.height * 2;
	if (0 == *count || MAX_FRAMES < *count)
		*count = MAX_FRAMES;
	return 0;
};

/**
 * buf_prepare - prepare buffer for usage
 * @vq: queue in videobuf layer
 * @vb: buffer to be prepared
 * @field: type of video data (interlaced/non-interlaced)
 *
 * Allocate or realloc buffer
 * return value: 0, successful.
 *
 * -EINVAL, supplied buffer is too small.
 *
 *  other, buffer could not be locked.
 */
static int buf_prepare(struct videobuf_queue *vq, struct videobuf_buffer *vb,
		       enum v4l2_field field)
{
	struct sta2x11_vip *vip = vq->priv_data;
	int ret;

	vb->size = vip->format.width * vip->format.height * 2;
	if ((0 != vb->baddr) && (vb->bsize < vb->size))
		return -EINVAL;
	vb->width = vip->format.width;
	vb->height = vip->format.height;
	vb->field = field;

	if (VIDEOBUF_NEEDS_INIT == vb->state) {
		ret = videobuf_iolock(vq, vb, NULL);
		if (ret)
			goto fail;
	}
	vb->state = VIDEOBUF_PREPARED;
	return 0;
fail:
	videobuf_dma_contig_free(vq, vb);
	vb->state = VIDEOBUF_NEEDS_INIT;
	return ret;
}

/**
 * buf_queu - queue buffer for filling
 * @vq: queue in videobuf layer
 * @vb: buffer to be queued
 *
 * if capturing is already running, the buffer will be queued. Otherwise
 * capture is started and the buffer is used directly.
 */
static void buf_queue(struct videobuf_queue *vq, struct videobuf_buffer *vb)
{
	struct sta2x11_vip *vip = vq->priv_data;
	u32 dma;

	vb->state = VIDEOBUF_QUEUED;

	if (vip->active) {
		list_add_tail(&vb->queue, &vip->capture);
		return;
	}

	vip->started = 1;
	vip->tcount = 0;
	vip->bcount = 0;
	vip->active = vb;
	vb->state = VIDEOBUF_ACTIVE;

	dma = videobuf_to_dma_contig(vb);

	REG_WRITE(vip, DVP_TFO, (0 << 16) | (0));
	/* despite of interlace mode, upper and lower frames start at zero */
	REG_WRITE(vip, DVP_BFO, (0 << 16) | (0));

	switch (vip->format.field) {
	case V4L2_FIELD_INTERLACED:
		REG_WRITE(vip, DVP_TFS,
			  ((vip->format.height / 2 - 1) << 16) |
			  (2 * vip->format.width - 1));
		REG_WRITE(vip, DVP_BFS, ((vip->format.height / 2 - 1) << 16) |
			  (2 * vip->format.width - 1));
		REG_WRITE(vip, DVP_VTP, dma);
		REG_WRITE(vip, DVP_VBP, dma + vip->format.width * 2);
		REG_WRITE(vip, DVP_VMP, 4 * vip->format.width);
		break;
	case V4L2_FIELD_TOP:
		REG_WRITE(vip, DVP_TFS,
			  ((vip->format.height - 1) << 16) |
			  (2 * vip->format.width - 1));
		REG_WRITE(vip, DVP_BFS, ((0) << 16) |
			  (2 * vip->format.width - 1));
		REG_WRITE(vip, DVP_VTP, dma);
		REG_WRITE(vip, DVP_VBP, dma);
		REG_WRITE(vip, DVP_VMP, 2 * vip->format.width);
		break;
	case V4L2_FIELD_BOTTOM:
		REG_WRITE(vip, DVP_TFS, ((0) << 16) |
			  (2 * vip->format.width - 1));
		REG_WRITE(vip, DVP_BFS,
			  ((vip->format.height) << 16) |
			  (2 * vip->format.width - 1));
		REG_WRITE(vip, DVP_VTP, dma);
		REG_WRITE(vip, DVP_VBP, dma);
		REG_WRITE(vip, DVP_VMP, 2 * vip->format.width);
		break;

	default:
		pr_warning("VIP: unknown field format\n");
		return;
	}

	REG_WRITE(vip, DVP_CTL, DVP_CTL_ENA);
}

/**
 * buff_release - release buffer
 * @vq: queue in videobuf layer
 * @vb: buffer to be released
 *
 * release buffer in videobuf layer
 */
static void buf_release(struct videobuf_queue *vq, struct videobuf_buffer *vb)
{

	videobuf_dma_contig_free(vq, vb);
	vb->state = VIDEOBUF_NEEDS_INIT;
}

static struct videobuf_queue_ops vip_qops = {
	.buf_setup = buf_setup,
	.buf_prepare = buf_prepare,
	.buf_queue = buf_queue,
	.buf_release = buf_release,
};

/**
 * vip_open - open video device
 * @file: descriptor of device
 *
 * open device, make sure it is only opened once.
 * return value: 0, no error.
 *
 * -EBUSY, device is already opened
 *
 * -ENOMEM, no memory for auxiliary DMA buffer
 */
static int vip_open(struct file *file)
{
	struct video_device *dev = video_devdata(file);
	struct sta2x11_vip *vip = video_get_drvdata(dev);

	mutex_lock(&vip->mutex);
	vip->users++;

	if (vip->users > 1) {
		vip->users--;
		mutex_unlock(&vip->mutex);
		return -EBUSY;
	}

	file->private_data = dev;
	vip->overflow = 0;
	vip->started = 0;
	vip->closing = 0;
	vip->active = NULL;

	INIT_LIST_HEAD(&vip->capture);
	vip->mem_spare = dma_alloc_coherent(&vip->pdev->dev, 64,
					    &vip->dma_spare, GFP_KERNEL);
	if (!vip->mem_spare) {
		vip->users--;
		mutex_unlock(&vip->mutex);
		return -ENOMEM;
	}

	mutex_unlock(&vip->mutex);
	videobuf_queue_dma_contig_init_cached(&vip->vb_vidq,
					      &vip_qops,
					      &vip->pdev->dev,
					      &vip->slock,
					      V4L2_BUF_TYPE_VIDEO_CAPTURE,
					      V4L2_FIELD_INTERLACED,
					      sizeof(struct videobuf_buffer),
					      vip, NULL);
	REG_READ(vip, DVP_ITS);
	REG_WRITE(vip, DVP_HLFLN, DVP_HLFLN_SD);
	REG_WRITE(vip, DVP_ITM, DVP_IT_VSB | DVP_IT_VST);
	REG_WRITE(vip, DVP_CTL, DVP_CTL_RST);
	REG_WRITE(vip, DVP_CTL, 0);
	REG_READ(vip, DVP_ITS);
	return 0;
}

/**
 * vip_close - close video device
 * @file: descriptor of device
 *
 * close video device, wait until all pending operations are finished
 * ( maximum FRAME_MAX buffers pending )
 * Turn off interrupts.
 *
 * return value: 0, always succesful.
 */
static int vip_close(struct file *file)
{
	struct video_device *dev = video_devdata(file);
	struct sta2x11_vip *vip = video_get_drvdata(dev);

	vip->closing = 1;
	if (vip->active)
		videobuf_waiton(&vip->vb_vidq, vip->active, 0, 0);
	spin_lock_irq(&vip->slock);

	REG_WRITE(vip, DVP_ITM, 0);
	REG_WRITE(vip, DVP_CTL, DVP_CTL_RST);
	REG_WRITE(vip, DVP_CTL, 0);
	REG_READ(vip, DVP_ITS);

	vip->started = 0;
	vip->active = NULL;

	spin_unlock_irq(&vip->slock);

	videobuf_stop(&vip->vb_vidq);
	videobuf_mmap_free(&vip->vb_vidq);

	dma_free_coherent(&vip->pdev->dev, 64, vip->mem_spare, vip->dma_spare);
	file->private_data = NULL;
	mutex_lock(&vip->mutex);
	vip->users--;
	mutex_unlock(&vip->mutex);
	return 0;
}

/**
 * vip_read - read from video input
 * @file: descriptor of device
 * @data: user buffer
 * @count: number of bytes to be read
 * @ppos: position within stream
 *
 * read video data from video device.
 * handling is done in generic videobuf layer
 * return value: provided by videobuf layer
 */
static ssize_t vip_read(struct file *file, char __user *data,
			size_t count, loff_t *ppos)
{
	struct video_device *dev = file->private_data;
	struct sta2x11_vip *vip = video_get_drvdata(dev);

	return videobuf_read_stream(&vip->vb_vidq, data, count, ppos, 0,
				    file->f_flags & O_NONBLOCK);
}

/**
 * vip_mmap - map user buffer
 * @file: descriptor of device
 * @vma: user buffer
 *
 * map user space buffer into kernel mode, including DMA address.
 * handling is done in generic videobuf layer.
 * return value: provided by videobuf layer
 */
static int vip_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct video_device *dev = file->private_data;
	struct sta2x11_vip *vip = video_get_drvdata(dev);

	return videobuf_mmap_mapper(&vip->vb_vidq, vma);
}

/**
 * vip_poll - poll for event
 * @file: descriptor of device
 * @wait: contains events to be waited for
 *
 * wait for event related to video device.
 * handling is done in generic videobuf layer.
 * return value: provided by videobuf layer
 */
static unsigned int vip_poll(struct file *file, struct poll_table_struct *wait)
{
	struct video_device *dev = file->private_data;
	struct sta2x11_vip *vip = video_get_drvdata(dev);

	return videobuf_poll_stream(file, &vip->vb_vidq, wait);
}

/**
 * vidioc_querycap - return capabilities of device
 * @file: descriptor of device (not used)
 * @priv: points to current videodevice
 * @cap: contains return values
 *
 * the capabilities of the device are returned
 *
 * return value: 0, no error.
 */
static int vidioc_querycap(struct file *file, void *priv,
			   struct v4l2_capability *cap)
{
	struct video_device *dev = priv;
	struct sta2x11_vip *vip = video_get_drvdata(dev);

	memset(cap, 0, sizeof(struct v4l2_capability));
	strcpy(cap->driver, DRV_NAME);
	strcpy(cap->card, DRV_NAME);
	cap->version = 0;
	snprintf(cap->bus_info, sizeof(cap->bus_info), "PCI:%s",
		 pci_name(vip->pdev));
	cap->capabilities = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_READWRITE |
	    V4L2_CAP_STREAMING;

	return 0;
}

/**
 * vidioc_s_std - set video standard
 * @file: descriptor of device (not used)
 * @priv: points to current videodevice
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
static int vidioc_s_std(struct file *file, void *priv, v4l2_std_id *std)
{
	struct video_device *dev = priv;
	struct sta2x11_vip *vip = video_get_drvdata(dev);
	v4l2_std_id oldstd = vip->std, newstd;
	int status;

	if (V4L2_STD_ALL == *std) {
		v4l2_subdev_call(vip->decoder, core, s_std, *std);
		ssleep(2);
		v4l2_subdev_call(vip->decoder, video, querystd, &newstd);
		v4l2_subdev_call(vip->decoder, video, g_input_status, &status);
		if (status & V4L2_IN_ST_NO_SIGNAL)
			return -EIO;
		*std = vip->std = newstd;
		if (oldstd != *std) {
			if (V4L2_STD_525_60 & (*std))
				vip->format = formats_60[0];
			else
				vip->format = formats_50[0];
		}
		return 0;
	}

	if (oldstd != *std) {
		if (V4L2_STD_525_60 & (*std))
			vip->format = formats_60[0];
		else
			vip->format = formats_50[0];
	}

	return v4l2_subdev_call(vip->decoder, core, s_std, *std);
}

/**
 * vidioc_g_std - get video standard
 * @file: descriptor of device (not used)
 * @priv: points to current videodevice
 * @std: contains return values
 *
 * the current video standard is returned
 *
 * return value: 0, no error.
 */
static int vidioc_g_std(struct file *file, void *priv, v4l2_std_id *std)
{
	struct video_device *dev = priv;
	struct sta2x11_vip *vip = video_get_drvdata(dev);

	*std = vip->std;
	return 0;
}

/**
 * vidioc_querystd - get possible video standards
 * @file: descriptor of device (not used)
 * @priv: points to current videodevice
 * @std: contains return values
 *
 * all possible video standards are returned
 *
 * return value: delivered by video DAC routine.
 */
static int vidioc_querystd(struct file *file, void *priv, v4l2_std_id *std)
{
	struct video_device *dev = priv;
	struct sta2x11_vip *vip = video_get_drvdata(dev);

	return v4l2_subdev_call(vip->decoder, video, querystd, std);

}

/**
 * vidioc_queryctl - get possible control settings
 * @file: descriptor of device (not used)
 * @priv: points to current videodevice
 * @ctrl: contains return values
 *
 * return possible values for a control
 * return value: delivered by video DAC routine.
 */
static int vidioc_queryctrl(struct file *file, void *priv,
			    struct v4l2_queryctrl *ctrl)
{
	struct video_device *dev = priv;
	struct sta2x11_vip *vip = video_get_drvdata(dev);

	return v4l2_subdev_call(vip->decoder, core, queryctrl, ctrl);
}

/**
 * vidioc_g_ctl - get control value
 * @file: descriptor of device (not used)
 * @priv: points to current videodevice
 * @ctrl: contains return values
 *
 * return setting for a control value
 * return value: delivered by video DAC routine.
 */
static int vidioc_g_ctrl(struct file *file, void *priv,
			 struct v4l2_control *ctrl)
{
	struct video_device *dev = priv;
	struct sta2x11_vip *vip = video_get_drvdata(dev);

	return v4l2_subdev_call(vip->decoder, core, g_ctrl, ctrl);
}

/**
 * vidioc_s_ctl - set control value
 * @file: descriptor of device (not used)
 * @priv: points to current videodevice
 * @ctrl: contains value to be set
 *
 * set value for a specific control
 * return value: delivered by video DAC routine.
 */
static int vidioc_s_ctrl(struct file *file, void *priv,
			 struct v4l2_control *ctrl)
{
	struct video_device *dev = priv;
	struct sta2x11_vip *vip = video_get_drvdata(dev);

	return v4l2_subdev_call(vip->decoder, core, s_ctrl, ctrl);
}

/**
 * vidioc_enum_input - return name of input line
 * @file: descriptor of device (not used)
 * @priv: points to current videodevice
 * @inp: contains return values
 *
 * the user friendly name of the input line is returned
 *
 * return value: 0, no error.
 *
 * -EINVAL, input line number out of range
 */
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
 * @file: descriptor of device ( not used)
 * @priv: points to current videodevice
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
	struct video_device *dev = priv;
	struct sta2x11_vip *vip = video_get_drvdata(dev);
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
 * @file: descriptor of device ( not used)
 * @priv: points to current videodevice
 * @i: returned input line number
 *
 * the current active input line is returned
 *
 * return value: always 0.
 */
static int vidioc_g_input(struct file *file, void *priv, unsigned int *i)
{
	struct video_device *dev = priv;
	struct sta2x11_vip *vip = video_get_drvdata(dev);

	*i = vip->input;
	return 0;
}

/**
 * vidioc_enum_fmt_vid_cap - return video capture format
 * @file: descriptor of device ( not used)
 * @priv: points to current videodevice
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
 * @file: descriptor of device ( not used)
 * @priv: points to current videodevice
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
	struct video_device *dev = priv;
	struct sta2x11_vip *vip = video_get_drvdata(dev);
	int interlace_lim;

	if (V4L2_PIX_FMT_UYVY != f->fmt.pix.pixelformat)
		return -EINVAL;

	if (V4L2_STD_525_60 & vip->std)
		interlace_lim = 240;
	else
		interlace_lim = 288;

	switch (f->fmt.pix.field) {
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
	default:
		return -EINVAL;
	}

	f->fmt.pix.height &= ~1;
	if (2 * interlace_lim < f->fmt.pix.height)
		f->fmt.pix.height = 2 * interlace_lim;
	if (200 > f->fmt.pix.height)
		f->fmt.pix.height = 200;
	f->fmt.pix.width = 720;
	f->fmt.pix.bytesperline = f->fmt.pix.width * 2;
	f->fmt.pix.sizeimage = f->fmt.pix.width * 2 * f->fmt.pix.height;
	f->fmt.pix.colorspace = V4L2_COLORSPACE_SMPTE170M;
	f->fmt.pix.priv = 0;
	return 0;
}

/**
 * vidioc_s_fmt_vid_cap - set current video format parameters
 * @file: descriptor of device ( not used)
 * @priv: points to current videodevice
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
	struct video_device *dev = priv;
	struct sta2x11_vip *vip = video_get_drvdata(dev);
	int ret;

	ret = vidioc_try_fmt_vid_cap(file, priv, f);
	if (ret)
		return ret;

	memcpy(&vip->format, &f->fmt.pix, sizeof(struct v4l2_pix_format));
	return 0;
}

/**
 * vidioc_g_fmt_vid_cap - get current video format parameters
 * @file: descriptor of device ( not used)
 * @priv: points to current videodevice
 * @f: contains format information
 *
 * returns current video format parameters
 *
 * return value: 0, always successful
 */
static int vidioc_g_fmt_vid_cap(struct file *file, void *priv,
				struct v4l2_format *f)
{
	struct video_device *dev = priv;
	struct sta2x11_vip *vip = video_get_drvdata(dev);

	memcpy(&f->fmt.pix, &vip->format, sizeof(struct v4l2_pix_format));
	return 0;
}

/**
 * vidioc_reqfs - request buffer
 * @file: descriptor of device ( not used)
 * @priv: points to current videodevice
 * @p: video buffer
 *
 * Handling is done in generic videobuf layer.
 */
static int vidioc_reqbufs(struct file *file, void *priv,
			  struct v4l2_requestbuffers *p)
{
	struct video_device *dev = priv;
	struct sta2x11_vip *vip = video_get_drvdata(dev);

	return videobuf_reqbufs(&vip->vb_vidq, p);
}

/**
 * vidioc_querybuf - query buffer
 * @file: descriptor of device ( not used)
 * @priv: points to current videodevice
 * @p: video buffer
 *
 * query buffer state.
 * Handling is done in generic videobuf layer.
 */
static int vidioc_querybuf(struct file *file, void *priv, struct v4l2_buffer *p)
{
	struct video_device *dev = priv;
	struct sta2x11_vip *vip = video_get_drvdata(dev);

	return videobuf_querybuf(&vip->vb_vidq, p);
}

/**
 * vidioc_qbuf - queue a buffer
 * @file: descriptor of device ( not used)
 * @priv: points to current videodevice
 * @p: video buffer
 *
 * Handling is done in generic videobuf layer.
 */
static int vidioc_qbuf(struct file *file, void *priv, struct v4l2_buffer *p)
{
	struct video_device *dev = priv;
	struct sta2x11_vip *vip = video_get_drvdata(dev);

	return videobuf_qbuf(&vip->vb_vidq, p);
}

/**
 * vidioc_dqbuf - dequeue a buffer
 * @file: descriptor of device ( not used)
 * @priv: points to current videodevice
 * @p: video buffer
 *
 * Handling is done in generic videobuf layer.
 */
static int vidioc_dqbuf(struct file *file, void *priv, struct v4l2_buffer *p)
{
	struct video_device *dev = priv;
	struct sta2x11_vip *vip = video_get_drvdata(dev);

	return videobuf_dqbuf(&vip->vb_vidq, p, file->f_flags & O_NONBLOCK);
}

/**
 * vidioc_streamon - turn on streaming
 * @file: descriptor of device ( not used)
 * @priv: points to current videodevice
 * @type: type of capture
 *
 * turn on streaming.
 * Handling is done in generic videobuf layer.
 */
static int vidioc_streamon(struct file *file, void *priv,
			   enum v4l2_buf_type type)
{
	struct video_device *dev = priv;
	struct sta2x11_vip *vip = video_get_drvdata(dev);

	return videobuf_streamon(&vip->vb_vidq);
}

/**
 * vidioc_streamoff - turn off streaming
 * @file: descriptor of device ( not used)
 * @priv: points to current videodevice
 * @type: type of capture
 *
 * turn off streaming.
 * Handling is done in generic videobuf layer.
 */
static int vidioc_streamoff(struct file *file, void *priv,
			    enum v4l2_buf_type type)
{
	struct video_device *dev = priv;
	struct sta2x11_vip *vip = video_get_drvdata(dev);

	return videobuf_streamoff(&vip->vb_vidq);
}

static const struct v4l2_file_operations vip_fops = {
	.owner = THIS_MODULE,
	.open = vip_open,
	.release = vip_close,
	.ioctl = video_ioctl2,
	.read = vip_read,
	.mmap = vip_mmap,
	.poll = vip_poll
};

static const struct v4l2_ioctl_ops vip_ioctl_ops = {
	.vidioc_querycap = vidioc_querycap,
	.vidioc_s_std = vidioc_s_std,
	.vidioc_g_std = vidioc_g_std,
	.vidioc_querystd = vidioc_querystd,
	.vidioc_queryctrl = vidioc_queryctrl,
	.vidioc_g_ctrl = vidioc_g_ctrl,
	.vidioc_s_ctrl = vidioc_s_ctrl,
	.vidioc_enum_input = vidioc_enum_input,
	.vidioc_try_fmt_vid_cap = vidioc_try_fmt_vid_cap,
	.vidioc_s_input = vidioc_s_input,
	.vidioc_g_input = vidioc_g_input,
	.vidioc_enum_fmt_vid_cap = vidioc_enum_fmt_vid_cap,
	.vidioc_s_fmt_vid_cap = vidioc_s_fmt_vid_cap,
	.vidioc_g_fmt_vid_cap = vidioc_g_fmt_vid_cap,
	.vidioc_reqbufs = vidioc_reqbufs,
	.vidioc_querybuf = vidioc_querybuf,
	.vidioc_qbuf = vidioc_qbuf,
	.vidioc_dqbuf = vidioc_dqbuf,
	.vidioc_streamon = vidioc_streamon,
	.vidioc_streamoff = vidioc_streamoff,
};

static struct video_device video_dev_template = {
	.name = DRV_NAME,
	.release = video_device_release,
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
 * signal a complete buffer if done.
 * dequeue a new buffer if available.
 * disable VIP if no buffer available.
 *
 * return value: IRQ_NONE, interrupt was not generated by VIP
 *
 * IRQ_HANDLED, interrupt done.
 */
static irqreturn_t vip_irq(int irq, struct sta2x11_vip *vip)
{
	u32 status, dma;
	unsigned long flags;
	struct videobuf_buffer *vb;

	status = REG_READ(vip, DVP_ITS);

	if (!status) {
		pr_debug("VIP: irq ignored\n");
		return IRQ_NONE;
	}

	if (!vip->started)
		return IRQ_HANDLED;

	if (status & DVP_IT_VSB)
		vip->bcount++;

	if (status & DVP_IT_VST)
		vip->tcount++;

	if ((DVP_IT_VSB | DVP_IT_VST) == (status & (DVP_IT_VST | DVP_IT_VSB))) {
		/* this is bad, we are too slow, hope the condition is gone
		 * on the next frame */
		pr_info("VIP: both irqs\n");
		return IRQ_HANDLED;
	}

	if (status & DVP_IT_FIFO) {
		if (5 > vip->overflow++)
			pr_info("VIP: fifo overflow\n");
	}

	if (2 > vip->tcount)
		return IRQ_HANDLED;

	if (status & DVP_IT_VSB)
		return IRQ_HANDLED;

	spin_lock_irqsave(&vip->slock, flags);

	REG_WRITE(vip, DVP_CTL, REG_READ(vip, DVP_CTL) & ~DVP_CTL_ENA);
	if (vip->active) {
		do_gettimeofday(&vip->active->ts);
		vip->active->field_count++;
		vip->active->state = VIDEOBUF_DONE;
		wake_up(&vip->active->done);
		vip->active = NULL;
	}
	if (!vip->closing) {
		if (list_empty(&vip->capture))
			goto done;

		vb = list_first_entry(&vip->capture, struct videobuf_buffer,
				      queue);
		if (NULL == vb) {
			pr_info("VIP: no buffer\n");
			goto done;
		}
		vb->state = VIDEOBUF_ACTIVE;
		list_del(&vb->queue);
		vip->active = vb;
		dma = videobuf_to_dma_contig(vb);
		switch (vip->format.field) {
		case V4L2_FIELD_INTERLACED:
			REG_WRITE(vip, DVP_VTP, dma);
			REG_WRITE(vip, DVP_VBP, dma + vip->format.width * 2);
			break;
		case V4L2_FIELD_TOP:
		case V4L2_FIELD_BOTTOM:
			REG_WRITE(vip, DVP_VTP, dma);
			REG_WRITE(vip, DVP_VBP, dma);
			break;
		default:
			pr_warning("VIP: unknown field format\n");
			goto done;
			break;
		}
		REG_WRITE(vip, DVP_CTL, REG_READ(vip, DVP_CTL) | DVP_CTL_ENA);
	}
done:
	spin_unlock_irqrestore(&vip->slock, flags);
	return IRQ_HANDLED;
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
static int __devinit sta2x11_vip_init_one(struct pci_dev *pdev,
					  const struct pci_device_id *ent)
{
	int ret;
	struct sta2x11_vip *vip;
	struct vip_config *config;

	ret = pci_enable_device(pdev);
	if (ret)
		return ret;

	config = dev_get_platdata(&pdev->dev);
	if (!config) {
		dev_info(&pdev->dev, "VIP slot disabled\n");
		ret = -EINVAL;
		goto disable;
	}

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

	vip = kzalloc(sizeof(struct sta2x11_vip), GFP_KERNEL);
	if (!vip) {
		ret = -ENOMEM;
		goto release_gpios;
	}

	vip->pdev = pdev;
	vip->std = V4L2_STD_PAL;
	vip->format = formats_50[0];
	vip->config = config;

	if (v4l2_device_register(&pdev->dev, &vip->v4l2_dev))
		goto free_mem;

	dev_dbg(&pdev->dev, "BAR #0 at 0x%lx 0x%lx irq %d\n",
		(unsigned long)pci_resource_start(pdev, 0),
		(unsigned long)pci_resource_len(pdev, 0), pdev->irq);

	pci_set_master(pdev);

	ret = pci_request_regions(pdev, DRV_NAME);
	if (ret)
		goto unreg;

	vip->iomem = pci_iomap(pdev, 0, 0x100);
	if (!vip->iomem) {
		ret = -ENOMEM; /* FIXME */
		goto release;
	}

	pci_enable_msi(pdev);

	INIT_LIST_HEAD(&vip->capture);
	spin_lock_init(&vip->slock);
	mutex_init(&vip->mutex);
	vip->started = 0;
	vip->disabled = 0;

	ret = request_irq(pdev->irq,
			  (irq_handler_t) vip_irq,
			  IRQF_SHARED, DRV_NAME, vip);
	if (ret) {
		dev_err(&pdev->dev, "request_irq failed\n");
		ret = -ENODEV;
		goto unmap;
	}

	vip->video_dev = video_device_alloc();
	if (!vip->video_dev) {
		ret = -ENOMEM;
		goto release_irq;
	}

	*(vip->video_dev) = video_dev_template;
	video_set_drvdata(vip->video_dev, vip);

	ret = video_register_device(vip->video_dev, VFL_TYPE_GRABBER, -1);
	if (ret)
		goto vrelease;

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

	pr_info("STA2X11 Video Input Port (VIP) loaded\n");
	return 0;

vunreg:
	video_set_drvdata(vip->video_dev, NULL);
vrelease:
	if (video_is_registered(vip->video_dev))
		video_unregister_device(vip->video_dev);
	else
		video_device_release(vip->video_dev);
release_irq:
	free_irq(pdev->irq, vip);
	pci_disable_msi(pdev);
unmap:
	pci_iounmap(pdev, vip->iomem);
	mutex_destroy(&vip->mutex);
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
static void __devexit sta2x11_vip_remove_one(struct pci_dev *pdev)
{
	struct v4l2_device *v4l2_dev = pci_get_drvdata(pdev);
	struct sta2x11_vip *vip =
	    container_of(v4l2_dev, struct sta2x11_vip, v4l2_dev);

	video_set_drvdata(vip->video_dev, NULL);
	video_unregister_device(vip->video_dev);
	/*do not call video_device_release() here, is already done */
	free_irq(pdev->irq, vip);
	pci_disable_msi(pdev);
	pci_iounmap(pdev, vip->iomem);
	pci_release_regions(pdev);

	v4l2_device_unregister(&vip->v4l2_dev);
	mutex_destroy(&vip->mutex);

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
 *
 * reurn value : 0, always succesful, even if hardware does not not support
 * power down mode.
 */
static int sta2x11_vip_suspend(struct pci_dev *pdev, pm_message_t state)
{
	struct v4l2_device *v4l2_dev = pci_get_drvdata(pdev);
	struct sta2x11_vip *vip =
	    container_of(v4l2_dev, struct sta2x11_vip, v4l2_dev);
	unsigned long flags;
	int i;

	spin_lock_irqsave(&vip->slock, flags);
	vip->register_save_area[0] = REG_READ(vip, DVP_CTL);
	REG_WRITE(vip, DVP_CTL, vip->register_save_area[0] & DVP_CTL_DIS);
	vip->register_save_area[SAVE_COUNT] = REG_READ(vip, DVP_ITM);
	REG_WRITE(vip, DVP_ITM, 0);
	for (i = 1; i < SAVE_COUNT; i++)
		vip->register_save_area[i] = REG_READ(vip, 4 * i);
	for (i = 0; i < AUX_COUNT; i++)
		vip->register_save_area[SAVE_COUNT + IRQ_COUNT + i] =
		    REG_READ(vip, registers_to_save[i]);
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
			pr_warning("VIP: Can't enable device.\n");
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
		pr_warning("VIP: Can't enable device.\n");
		vip->disabled = 1;
		return ret;
	}

	pci_restore_state(pdev);

	spin_lock_irqsave(&vip->slock, flags);
	for (i = 1; i < SAVE_COUNT; i++)
		REG_WRITE(vip, 4 * i, vip->register_save_area[i]);
	for (i = 0; i < AUX_COUNT; i++)
		REG_WRITE(vip, registers_to_save[i],
			  vip->register_save_area[SAVE_COUNT + IRQ_COUNT + i]);
	REG_WRITE(vip, DVP_CTL, vip->register_save_area[0]);
	REG_WRITE(vip, DVP_ITM, vip->register_save_area[SAVE_COUNT]);
	spin_unlock_irqrestore(&vip->slock, flags);
	return 0;
}

#endif

static DEFINE_PCI_DEVICE_TABLE(sta2x11_vip_pci_tbl) = {
	{PCI_DEVICE(PCI_VENDOR_ID_STMICRO, PCI_DEVICE_ID_STMICRO_VIP)},
	{0,}
};

static struct pci_driver sta2x11_vip_driver = {
	.name = DRV_NAME,
	.probe = sta2x11_vip_init_one,
	.remove = __devexit_p(sta2x11_vip_remove_one),
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
