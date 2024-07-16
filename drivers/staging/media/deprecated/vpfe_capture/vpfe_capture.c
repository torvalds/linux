// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2008-2009 Texas Instruments Inc
 *
 * Driver name : VPFE Capture driver
 *    VPFE Capture driver allows applications to capture and stream video
 *    frames on DaVinci SoCs (DM6446, DM355 etc) from a YUV source such as
 *    TVP5146 or  Raw Bayer RGB image data from an image sensor
 *    such as Microns' MT9T001, MT9T031 etc.
 *
 *    These SoCs have, in common, a Video Processing Subsystem (VPSS) that
 *    consists of a Video Processing Front End (VPFE) for capturing
 *    video/raw image data and Video Processing Back End (VPBE) for displaying
 *    YUV data through an in-built analog encoder or Digital LCD port. This
 *    driver is for capture through VPFE. A typical EVM using these SoCs have
 *    following high level configuration.
 *
 *    decoder(TVP5146/		YUV/
 *	     MT9T001)   -->  Raw Bayer RGB ---> MUX -> VPFE (CCDC/ISIF)
 *				data input              |      |
 *							V      |
 *						      SDRAM    |
 *							       V
 *							   Image Processor
 *							       |
 *							       V
 *							     SDRAM
 *    The data flow happens from a decoder connected to the VPFE over a
 *    YUV embedded (BT.656/BT.1120) or separate sync or raw bayer rgb interface
 *    and to the input of VPFE through an optional MUX (if more inputs are
 *    to be interfaced on the EVM). The input data is first passed through
 *    CCDC (CCD Controller, a.k.a Image Sensor Interface, ISIF). The CCDC
 *    does very little or no processing on YUV data and does pre-process Raw
 *    Bayer RGB data through modules such as Defect Pixel Correction (DFC)
 *    Color Space Conversion (CSC), data gain/offset etc. After this, data
 *    can be written to SDRAM or can be connected to the image processing
 *    block such as IPIPE (on DM355 only).
 *
 *    Features supported
 *		- MMAP IO
 *		- Capture using TVP5146 over BT.656
 *		- support for interfacing decoders using sub device model
 *		- Work with DM355 or DM6446 CCDC to do Raw Bayer RGB/YUV
 *		  data capture to SDRAM.
 *    TODO list
 *		- Support multiple REQBUF after open
 *		- Support for de-allocating buffers through REQBUF
 *		- Support for Raw Bayer RGB capture
 *		- Support for chaining Image Processor
 *		- Support for static allocation of buffers
 *		- Support for USERPTR IO
 *		- Support for STREAMON before QBUF
 *		- Support for control ioctls
 */
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <media/v4l2-common.h>
#include <linux/io.h>
#include <media/davinci/vpfe_capture.h>
#include "ccdc_hw_device.h"

static int debug;
static u32 numbuffers = 3;
static u32 bufsize = (720 * 576 * 2);

module_param(numbuffers, uint, S_IRUGO);
module_param(bufsize, uint, S_IRUGO);
module_param(debug, int, 0644);

MODULE_PARM_DESC(numbuffers, "buffer count (default:3)");
MODULE_PARM_DESC(bufsize, "buffer size in bytes (default:720 x 576 x 2)");
MODULE_PARM_DESC(debug, "Debug level 0-1");

MODULE_DESCRIPTION("VPFE Video for Linux Capture Driver");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Texas Instruments");

/* standard information */
struct vpfe_standard {
	v4l2_std_id std_id;
	unsigned int width;
	unsigned int height;
	struct v4l2_fract pixelaspect;
	/* 0 - progressive, 1 - interlaced */
	int frame_format;
};

/* ccdc configuration */
struct ccdc_config {
	/* This make sure vpfe is probed and ready to go */
	int vpfe_probed;
	/* name of ccdc device */
	char name[32];
};

/* data structures */
static struct vpfe_config_params config_params = {
	.min_numbuffers = 3,
	.numbuffers = 3,
	.min_bufsize = 720 * 480 * 2,
	.device_bufsize = 720 * 576 * 2,
};

/* ccdc device registered */
static const struct ccdc_hw_device *ccdc_dev;
/* lock for accessing ccdc information */
static DEFINE_MUTEX(ccdc_lock);
/* ccdc configuration */
static struct ccdc_config *ccdc_cfg;

static const struct vpfe_standard vpfe_standards[] = {
	{V4L2_STD_525_60, 720, 480, {11, 10}, 1},
	{V4L2_STD_625_50, 720, 576, {54, 59}, 1},
};

/* Used when raw Bayer image from ccdc is directly captured to SDRAM */
static const struct vpfe_pixel_format vpfe_pix_fmts[] = {
	{
		.pixelformat = V4L2_PIX_FMT_SBGGR8,
		.bpp = 1,
	},
	{
		.pixelformat = V4L2_PIX_FMT_SBGGR16,
		.bpp = 2,
	},
	{
		.pixelformat = V4L2_PIX_FMT_SGRBG10DPCM8,
		.bpp = 1,
	},
	{
		.pixelformat = V4L2_PIX_FMT_UYVY,
		.bpp = 2,
	},
	{
		.pixelformat = V4L2_PIX_FMT_YUYV,
		.bpp = 2,
	},
	{
		.pixelformat = V4L2_PIX_FMT_NV12,
		.bpp = 1,
	},
};

/*
 * vpfe_lookup_pix_format()
 * lookup an entry in the vpfe pix format table based on pix_format
 */
static const struct vpfe_pixel_format *vpfe_lookup_pix_format(u32 pix_format)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(vpfe_pix_fmts); i++) {
		if (pix_format == vpfe_pix_fmts[i].pixelformat)
			return &vpfe_pix_fmts[i];
	}
	return NULL;
}

/*
 * vpfe_register_ccdc_device. CCDC module calls this to
 * register with vpfe capture
 */
int vpfe_register_ccdc_device(const struct ccdc_hw_device *dev)
{
	int ret = 0;
	printk(KERN_NOTICE "vpfe_register_ccdc_device: %s\n", dev->name);

	if (!dev->hw_ops.open ||
	    !dev->hw_ops.enable ||
	    !dev->hw_ops.set_hw_if_params ||
	    !dev->hw_ops.configure ||
	    !dev->hw_ops.set_buftype ||
	    !dev->hw_ops.get_buftype ||
	    !dev->hw_ops.enum_pix ||
	    !dev->hw_ops.set_frame_format ||
	    !dev->hw_ops.get_frame_format ||
	    !dev->hw_ops.get_pixel_format ||
	    !dev->hw_ops.set_pixel_format ||
	    !dev->hw_ops.set_image_window ||
	    !dev->hw_ops.get_image_window ||
	    !dev->hw_ops.get_line_length ||
	    !dev->hw_ops.getfid)
		return -EINVAL;

	mutex_lock(&ccdc_lock);
	if (!ccdc_cfg) {
		/*
		 * TODO. Will this ever happen? if so, we need to fix it.
		 * Probably we need to add the request to a linked list and
		 * walk through it during vpfe probe
		 */
		printk(KERN_ERR "vpfe capture not initialized\n");
		ret = -EFAULT;
		goto unlock;
	}

	if (strcmp(dev->name, ccdc_cfg->name)) {
		/* ignore this ccdc */
		ret = -EINVAL;
		goto unlock;
	}

	if (ccdc_dev) {
		printk(KERN_ERR "ccdc already registered\n");
		ret = -EINVAL;
		goto unlock;
	}

	ccdc_dev = dev;
unlock:
	mutex_unlock(&ccdc_lock);
	return ret;
}
EXPORT_SYMBOL(vpfe_register_ccdc_device);

/*
 * vpfe_unregister_ccdc_device. CCDC module calls this to
 * unregister with vpfe capture
 */
void vpfe_unregister_ccdc_device(const struct ccdc_hw_device *dev)
{
	if (!dev) {
		printk(KERN_ERR "invalid ccdc device ptr\n");
		return;
	}

	printk(KERN_NOTICE "vpfe_unregister_ccdc_device, dev->name = %s\n",
		dev->name);

	if (strcmp(dev->name, ccdc_cfg->name)) {
		/* ignore this ccdc */
		return;
	}

	mutex_lock(&ccdc_lock);
	ccdc_dev = NULL;
	mutex_unlock(&ccdc_lock);
}
EXPORT_SYMBOL(vpfe_unregister_ccdc_device);

/*
 * vpfe_config_ccdc_image_format()
 * For a pix format, configure ccdc to setup the capture
 */
static int vpfe_config_ccdc_image_format(struct vpfe_device *vpfe_dev)
{
	enum ccdc_frmfmt frm_fmt = CCDC_FRMFMT_INTERLACED;
	int ret = 0;

	if (ccdc_dev->hw_ops.set_pixel_format(
			vpfe_dev->fmt.fmt.pix.pixelformat) < 0) {
		v4l2_err(&vpfe_dev->v4l2_dev,
			"couldn't set pix format in ccdc\n");
		return -EINVAL;
	}
	/* configure the image window */
	ccdc_dev->hw_ops.set_image_window(&vpfe_dev->crop);

	switch (vpfe_dev->fmt.fmt.pix.field) {
	case V4L2_FIELD_INTERLACED:
		/* do nothing, since it is default */
		ret = ccdc_dev->hw_ops.set_buftype(
				CCDC_BUFTYPE_FLD_INTERLEAVED);
		break;
	case V4L2_FIELD_NONE:
		frm_fmt = CCDC_FRMFMT_PROGRESSIVE;
		/* buffer type only applicable for interlaced scan */
		break;
	case V4L2_FIELD_SEQ_TB:
		ret = ccdc_dev->hw_ops.set_buftype(
				CCDC_BUFTYPE_FLD_SEPARATED);
		break;
	default:
		return -EINVAL;
	}

	/* set the frame format */
	if (!ret)
		ret = ccdc_dev->hw_ops.set_frame_format(frm_fmt);
	return ret;
}
/*
 * vpfe_config_image_format()
 * For a given standard, this functions sets up the default
 * pix format & crop values in the vpfe device and ccdc.  It first
 * starts with defaults based values from the standard table.
 * It then checks if sub device supports get_fmt and then override the
 * values based on that.Sets crop values to match with scan resolution
 * starting at 0,0. It calls vpfe_config_ccdc_image_format() set the
 * values in ccdc
 */
static int vpfe_config_image_format(struct vpfe_device *vpfe_dev,
				    v4l2_std_id std_id)
{
	struct vpfe_subdev_info *sdinfo = vpfe_dev->current_subdev;
	struct v4l2_subdev_format fmt = {
		.which = V4L2_SUBDEV_FORMAT_ACTIVE,
	};
	struct v4l2_mbus_framefmt *mbus_fmt = &fmt.format;
	struct v4l2_pix_format *pix = &vpfe_dev->fmt.fmt.pix;
	int i, ret;

	for (i = 0; i < ARRAY_SIZE(vpfe_standards); i++) {
		if (vpfe_standards[i].std_id & std_id) {
			vpfe_dev->std_info.active_pixels =
					vpfe_standards[i].width;
			vpfe_dev->std_info.active_lines =
					vpfe_standards[i].height;
			vpfe_dev->std_info.frame_format =
					vpfe_standards[i].frame_format;
			vpfe_dev->std_index = i;
			break;
		}
	}

	if (i ==  ARRAY_SIZE(vpfe_standards)) {
		v4l2_err(&vpfe_dev->v4l2_dev, "standard not supported\n");
		return -EINVAL;
	}

	vpfe_dev->crop.top = 0;
	vpfe_dev->crop.left = 0;
	vpfe_dev->crop.width = vpfe_dev->std_info.active_pixels;
	vpfe_dev->crop.height = vpfe_dev->std_info.active_lines;
	pix->width = vpfe_dev->crop.width;
	pix->height = vpfe_dev->crop.height;

	/* first field and frame format based on standard frame format */
	if (vpfe_dev->std_info.frame_format) {
		pix->field = V4L2_FIELD_INTERLACED;
		/* assume V4L2_PIX_FMT_UYVY as default */
		pix->pixelformat = V4L2_PIX_FMT_UYVY;
		v4l2_fill_mbus_format(mbus_fmt, pix,
				MEDIA_BUS_FMT_YUYV10_2X10);
	} else {
		pix->field = V4L2_FIELD_NONE;
		/* assume V4L2_PIX_FMT_SBGGR8 */
		pix->pixelformat = V4L2_PIX_FMT_SBGGR8;
		v4l2_fill_mbus_format(mbus_fmt, pix,
				MEDIA_BUS_FMT_SBGGR8_1X8);
	}

	/* if sub device supports get_fmt, override the defaults */
	ret = v4l2_device_call_until_err(&vpfe_dev->v4l2_dev,
			sdinfo->grp_id, pad, get_fmt, NULL, &fmt);

	if (ret && ret != -ENOIOCTLCMD) {
		v4l2_err(&vpfe_dev->v4l2_dev,
			"error in getting get_fmt from sub device\n");
		return ret;
	}
	v4l2_fill_pix_format(pix, mbus_fmt);
	pix->bytesperline = pix->width * 2;
	pix->sizeimage = pix->bytesperline * pix->height;

	/* Sets the values in CCDC */
	ret = vpfe_config_ccdc_image_format(vpfe_dev);
	if (ret)
		return ret;

	/* Update the values of sizeimage and bytesperline */
	pix->bytesperline = ccdc_dev->hw_ops.get_line_length();
	pix->sizeimage = pix->bytesperline * pix->height;

	return 0;
}

static int vpfe_initialize_device(struct vpfe_device *vpfe_dev)
{
	int ret;

	/* set first input of current subdevice as the current input */
	vpfe_dev->current_input = 0;

	/* set default standard */
	vpfe_dev->std_index = 0;

	/* Configure the default format information */
	ret = vpfe_config_image_format(vpfe_dev,
				vpfe_standards[vpfe_dev->std_index].std_id);
	if (ret)
		return ret;

	/* now open the ccdc device to initialize it */
	mutex_lock(&ccdc_lock);
	if (!ccdc_dev) {
		v4l2_err(&vpfe_dev->v4l2_dev, "ccdc device not registered\n");
		ret = -ENODEV;
		goto unlock;
	}

	if (!try_module_get(ccdc_dev->owner)) {
		v4l2_err(&vpfe_dev->v4l2_dev, "Couldn't lock ccdc module\n");
		ret = -ENODEV;
		goto unlock;
	}
	ret = ccdc_dev->hw_ops.open(vpfe_dev->pdev);
	if (!ret)
		vpfe_dev->initialized = 1;

	/* Clear all VPFE/CCDC interrupts */
	if (vpfe_dev->cfg->clr_intr)
		vpfe_dev->cfg->clr_intr(-1);

unlock:
	mutex_unlock(&ccdc_lock);
	return ret;
}

/*
 * vpfe_open : It creates object of file handle structure and
 * stores it in private_data  member of filepointer
 */
static int vpfe_open(struct file *file)
{
	struct vpfe_device *vpfe_dev = video_drvdata(file);
	struct video_device *vdev = video_devdata(file);
	struct vpfe_fh *fh;

	v4l2_dbg(1, debug, &vpfe_dev->v4l2_dev, "vpfe_open\n");

	if (!vpfe_dev->cfg->num_subdevs) {
		v4l2_err(&vpfe_dev->v4l2_dev, "No decoder registered\n");
		return -ENODEV;
	}

	/* Allocate memory for the file handle object */
	fh = kmalloc(sizeof(*fh), GFP_KERNEL);
	if (!fh)
		return -ENOMEM;

	/* store pointer to fh in private_data member of file */
	file->private_data = fh;
	fh->vpfe_dev = vpfe_dev;
	v4l2_fh_init(&fh->fh, vdev);
	mutex_lock(&vpfe_dev->lock);
	/* If decoder is not initialized. initialize it */
	if (!vpfe_dev->initialized) {
		if (vpfe_initialize_device(vpfe_dev)) {
			mutex_unlock(&vpfe_dev->lock);
			v4l2_fh_exit(&fh->fh);
			kfree(fh);
			return -ENODEV;
		}
	}
	/* Increment device usrs counter */
	vpfe_dev->usrs++;
	/* Set io_allowed member to false */
	fh->io_allowed = 0;
	v4l2_fh_add(&fh->fh);
	mutex_unlock(&vpfe_dev->lock);
	return 0;
}

static void vpfe_schedule_next_buffer(struct vpfe_device *vpfe_dev)
{
	unsigned long addr;

	vpfe_dev->next_frm = list_entry(vpfe_dev->dma_queue.next,
					struct videobuf_buffer, queue);
	list_del(&vpfe_dev->next_frm->queue);
	vpfe_dev->next_frm->state = VIDEOBUF_ACTIVE;
	addr = videobuf_to_dma_contig(vpfe_dev->next_frm);

	ccdc_dev->hw_ops.setfbaddr(addr);
}

static void vpfe_schedule_bottom_field(struct vpfe_device *vpfe_dev)
{
	unsigned long addr;

	addr = videobuf_to_dma_contig(vpfe_dev->cur_frm);
	addr += vpfe_dev->field_off;
	ccdc_dev->hw_ops.setfbaddr(addr);
}

static void vpfe_process_buffer_complete(struct vpfe_device *vpfe_dev)
{
	vpfe_dev->cur_frm->ts = ktime_get_ns();
	vpfe_dev->cur_frm->state = VIDEOBUF_DONE;
	vpfe_dev->cur_frm->size = vpfe_dev->fmt.fmt.pix.sizeimage;
	wake_up_interruptible(&vpfe_dev->cur_frm->done);
	vpfe_dev->cur_frm = vpfe_dev->next_frm;
}

/* ISR for VINT0*/
static irqreturn_t vpfe_isr(int irq, void *dev_id)
{
	struct vpfe_device *vpfe_dev = dev_id;
	enum v4l2_field field;
	int fid;

	v4l2_dbg(1, debug, &vpfe_dev->v4l2_dev, "\nStarting vpfe_isr...\n");
	field = vpfe_dev->fmt.fmt.pix.field;

	/* if streaming not started, don't do anything */
	if (!vpfe_dev->started)
		goto clear_intr;

	/* only for 6446 this will be applicable */
	if (ccdc_dev->hw_ops.reset)
		ccdc_dev->hw_ops.reset();

	if (field == V4L2_FIELD_NONE) {
		/* handle progressive frame capture */
		v4l2_dbg(1, debug, &vpfe_dev->v4l2_dev,
			"frame format is progressive...\n");
		if (vpfe_dev->cur_frm != vpfe_dev->next_frm)
			vpfe_process_buffer_complete(vpfe_dev);
		goto clear_intr;
	}

	/* interlaced or TB capture check which field we are in hardware */
	fid = ccdc_dev->hw_ops.getfid();

	/* switch the software maintained field id */
	vpfe_dev->field_id ^= 1;
	v4l2_dbg(1, debug, &vpfe_dev->v4l2_dev, "field id = %x:%x.\n",
		fid, vpfe_dev->field_id);
	if (fid == vpfe_dev->field_id) {
		/* we are in-sync here,continue */
		if (fid == 0) {
			/*
			 * One frame is just being captured. If the next frame
			 * is available, release the current frame and move on
			 */
			if (vpfe_dev->cur_frm != vpfe_dev->next_frm)
				vpfe_process_buffer_complete(vpfe_dev);
			/*
			 * based on whether the two fields are stored
			 * interleavely or separately in memory, reconfigure
			 * the CCDC memory address
			 */
			if (field == V4L2_FIELD_SEQ_TB)
				vpfe_schedule_bottom_field(vpfe_dev);
			goto clear_intr;
		}
		/*
		 * if one field is just being captured configure
		 * the next frame get the next frame from the empty
		 * queue if no frame is available hold on to the
		 * current buffer
		 */
		spin_lock(&vpfe_dev->dma_queue_lock);
		if (!list_empty(&vpfe_dev->dma_queue) &&
		    vpfe_dev->cur_frm == vpfe_dev->next_frm)
			vpfe_schedule_next_buffer(vpfe_dev);
		spin_unlock(&vpfe_dev->dma_queue_lock);
	} else if (fid == 0) {
		/*
		 * out of sync. Recover from any hardware out-of-sync.
		 * May loose one frame
		 */
		vpfe_dev->field_id = fid;
	}
clear_intr:
	if (vpfe_dev->cfg->clr_intr)
		vpfe_dev->cfg->clr_intr(irq);

	return IRQ_HANDLED;
}

/* vdint1_isr - isr handler for VINT1 interrupt */
static irqreturn_t vdint1_isr(int irq, void *dev_id)
{
	struct vpfe_device *vpfe_dev = dev_id;

	v4l2_dbg(1, debug, &vpfe_dev->v4l2_dev, "\nInside vdint1_isr...\n");

	/* if streaming not started, don't do anything */
	if (!vpfe_dev->started) {
		if (vpfe_dev->cfg->clr_intr)
			vpfe_dev->cfg->clr_intr(irq);
		return IRQ_HANDLED;
	}

	spin_lock(&vpfe_dev->dma_queue_lock);
	if ((vpfe_dev->fmt.fmt.pix.field == V4L2_FIELD_NONE) &&
	    !list_empty(&vpfe_dev->dma_queue) &&
	    vpfe_dev->cur_frm == vpfe_dev->next_frm)
		vpfe_schedule_next_buffer(vpfe_dev);
	spin_unlock(&vpfe_dev->dma_queue_lock);

	if (vpfe_dev->cfg->clr_intr)
		vpfe_dev->cfg->clr_intr(irq);

	return IRQ_HANDLED;
}

static void vpfe_detach_irq(struct vpfe_device *vpfe_dev)
{
	enum ccdc_frmfmt frame_format;

	frame_format = ccdc_dev->hw_ops.get_frame_format();
	if (frame_format == CCDC_FRMFMT_PROGRESSIVE)
		free_irq(vpfe_dev->ccdc_irq1, vpfe_dev);
}

static int vpfe_attach_irq(struct vpfe_device *vpfe_dev)
{
	enum ccdc_frmfmt frame_format;

	frame_format = ccdc_dev->hw_ops.get_frame_format();
	if (frame_format == CCDC_FRMFMT_PROGRESSIVE) {
		return request_irq(vpfe_dev->ccdc_irq1, vdint1_isr,
				    0, "vpfe_capture1",
				    vpfe_dev);
	}
	return 0;
}

/* vpfe_stop_ccdc_capture: stop streaming in ccdc/isif */
static void vpfe_stop_ccdc_capture(struct vpfe_device *vpfe_dev)
{
	vpfe_dev->started = 0;
	ccdc_dev->hw_ops.enable(0);
	if (ccdc_dev->hw_ops.enable_out_to_sdram)
		ccdc_dev->hw_ops.enable_out_to_sdram(0);
}

/*
 * vpfe_release : This function deletes buffer queue, frees the
 * buffers and the vpfe file  handle
 */
static int vpfe_release(struct file *file)
{
	struct vpfe_device *vpfe_dev = video_drvdata(file);
	struct vpfe_fh *fh = file->private_data;
	struct vpfe_subdev_info *sdinfo;
	int ret;

	v4l2_dbg(1, debug, &vpfe_dev->v4l2_dev, "vpfe_release\n");

	/* Get the device lock */
	mutex_lock(&vpfe_dev->lock);
	/* if this instance is doing IO */
	if (fh->io_allowed) {
		if (vpfe_dev->started) {
			sdinfo = vpfe_dev->current_subdev;
			ret = v4l2_device_call_until_err(&vpfe_dev->v4l2_dev,
							 sdinfo->grp_id,
							 video, s_stream, 0);
			if (ret && (ret != -ENOIOCTLCMD))
				v4l2_err(&vpfe_dev->v4l2_dev,
				"stream off failed in subdev\n");
			vpfe_stop_ccdc_capture(vpfe_dev);
			vpfe_detach_irq(vpfe_dev);
			videobuf_streamoff(&vpfe_dev->buffer_queue);
		}
		vpfe_dev->io_usrs = 0;
		vpfe_dev->numbuffers = config_params.numbuffers;
		videobuf_stop(&vpfe_dev->buffer_queue);
		videobuf_mmap_free(&vpfe_dev->buffer_queue);
	}

	/* Decrement device usrs counter */
	vpfe_dev->usrs--;
	v4l2_fh_del(&fh->fh);
	v4l2_fh_exit(&fh->fh);
	/* If this is the last file handle */
	if (!vpfe_dev->usrs) {
		vpfe_dev->initialized = 0;
		if (ccdc_dev->hw_ops.close)
			ccdc_dev->hw_ops.close(vpfe_dev->pdev);
		module_put(ccdc_dev->owner);
	}
	mutex_unlock(&vpfe_dev->lock);
	file->private_data = NULL;
	/* Free memory allocated to file handle object */
	kfree(fh);
	return 0;
}

/*
 * vpfe_mmap : It is used to map kernel space buffers
 * into user spaces
 */
static int vpfe_mmap(struct file *file, struct vm_area_struct *vma)
{
	/* Get the device object and file handle object */
	struct vpfe_device *vpfe_dev = video_drvdata(file);

	v4l2_dbg(1, debug, &vpfe_dev->v4l2_dev, "vpfe_mmap\n");

	return videobuf_mmap_mapper(&vpfe_dev->buffer_queue, vma);
}

/*
 * vpfe_poll: It is used for select/poll system call
 */
static __poll_t vpfe_poll(struct file *file, poll_table *wait)
{
	struct vpfe_device *vpfe_dev = video_drvdata(file);

	v4l2_dbg(1, debug, &vpfe_dev->v4l2_dev, "vpfe_poll\n");

	if (vpfe_dev->started)
		return videobuf_poll_stream(file,
					    &vpfe_dev->buffer_queue, wait);
	return 0;
}

/* vpfe capture driver file operations */
static const struct v4l2_file_operations vpfe_fops = {
	.owner = THIS_MODULE,
	.open = vpfe_open,
	.release = vpfe_release,
	.unlocked_ioctl = video_ioctl2,
	.mmap = vpfe_mmap,
	.poll = vpfe_poll
};

/*
 * vpfe_check_format()
 * This function adjust the input pixel format as per hardware
 * capabilities and update the same in pixfmt.
 * Following algorithm used :-
 *
 *	If given pixformat is not in the vpfe list of pix formats or not
 *	supported by the hardware, current value of pixformat in the device
 *	is used
 *	If given field is not supported, then current field is used. If field
 *	is different from current, then it is matched with that from sub device.
 *	Minimum height is 2 lines for interlaced or tb field and 1 line for
 *	progressive. Maximum height is clamped to active active lines of scan
 *	Minimum width is 32 bytes in memory and width is clamped to active
 *	pixels of scan.
 *	bytesperline is a multiple of 32.
 */
static const struct vpfe_pixel_format *
	vpfe_check_format(struct vpfe_device *vpfe_dev,
			  struct v4l2_pix_format *pixfmt)
{
	u32 min_height = 1, min_width = 32, max_width, max_height;
	const struct vpfe_pixel_format *vpfe_pix_fmt;
	u32 pix;
	int temp, found;

	vpfe_pix_fmt = vpfe_lookup_pix_format(pixfmt->pixelformat);
	if (!vpfe_pix_fmt) {
		/*
		 * use current pixel format in the vpfe device. We
		 * will find this pix format in the table
		 */
		pixfmt->pixelformat = vpfe_dev->fmt.fmt.pix.pixelformat;
		vpfe_pix_fmt = vpfe_lookup_pix_format(pixfmt->pixelformat);
	}

	/* check if hw supports it */
	temp = 0;
	found = 0;
	while (ccdc_dev->hw_ops.enum_pix(&pix, temp) >= 0) {
		if (vpfe_pix_fmt->pixelformat == pix) {
			found = 1;
			break;
		}
		temp++;
	}

	if (!found) {
		/* use current pixel format */
		pixfmt->pixelformat = vpfe_dev->fmt.fmt.pix.pixelformat;
		/*
		 * Since this is currently used in the vpfe device, we
		 * will find this pix format in the table
		 */
		vpfe_pix_fmt = vpfe_lookup_pix_format(pixfmt->pixelformat);
	}

	/* check what field format is supported */
	if (pixfmt->field == V4L2_FIELD_ANY) {
		/* if field is any, use current value as default */
		pixfmt->field = vpfe_dev->fmt.fmt.pix.field;
	}

	/*
	 * if field is not same as current field in the vpfe device
	 * try matching the field with the sub device field
	 */
	if (vpfe_dev->fmt.fmt.pix.field != pixfmt->field) {
		/*
		 * If field value is not in the supported fields, use current
		 * field used in the device as default
		 */
		switch (pixfmt->field) {
		case V4L2_FIELD_INTERLACED:
		case V4L2_FIELD_SEQ_TB:
			/* if sub device is supporting progressive, use that */
			if (!vpfe_dev->std_info.frame_format)
				pixfmt->field = V4L2_FIELD_NONE;
			break;
		case V4L2_FIELD_NONE:
			if (vpfe_dev->std_info.frame_format)
				pixfmt->field = V4L2_FIELD_INTERLACED;
			break;

		default:
			/* use current field as default */
			pixfmt->field = vpfe_dev->fmt.fmt.pix.field;
			break;
		}
	}

	/* Now adjust image resolutions supported */
	if (pixfmt->field == V4L2_FIELD_INTERLACED ||
	    pixfmt->field == V4L2_FIELD_SEQ_TB)
		min_height = 2;

	max_width = vpfe_dev->std_info.active_pixels;
	max_height = vpfe_dev->std_info.active_lines;
	min_width /= vpfe_pix_fmt->bpp;

	v4l2_info(&vpfe_dev->v4l2_dev, "width = %d, height = %d, bpp = %d\n",
		  pixfmt->width, pixfmt->height, vpfe_pix_fmt->bpp);

	pixfmt->width = clamp((pixfmt->width), min_width, max_width);
	pixfmt->height = clamp((pixfmt->height), min_height, max_height);

	/* If interlaced, adjust height to be a multiple of 2 */
	if (pixfmt->field == V4L2_FIELD_INTERLACED)
		pixfmt->height &= (~1);
	/*
	 * recalculate bytesperline and sizeimage since width
	 * and height might have changed
	 */
	pixfmt->bytesperline = (((pixfmt->width * vpfe_pix_fmt->bpp) + 31)
				& ~31);
	if (pixfmt->pixelformat == V4L2_PIX_FMT_NV12)
		pixfmt->sizeimage =
			pixfmt->bytesperline * pixfmt->height +
			((pixfmt->bytesperline * pixfmt->height) >> 1);
	else
		pixfmt->sizeimage = pixfmt->bytesperline * pixfmt->height;

	v4l2_info(&vpfe_dev->v4l2_dev, "adjusted width = %d, height = %d, bpp = %d, bytesperline = %d, sizeimage = %d\n",
		 pixfmt->width, pixfmt->height, vpfe_pix_fmt->bpp,
		 pixfmt->bytesperline, pixfmt->sizeimage);
	return vpfe_pix_fmt;
}

static int vpfe_querycap(struct file *file, void  *priv,
			       struct v4l2_capability *cap)
{
	struct vpfe_device *vpfe_dev = video_drvdata(file);

	v4l2_dbg(1, debug, &vpfe_dev->v4l2_dev, "vpfe_querycap\n");

	strscpy(cap->driver, CAPTURE_DRV_NAME, sizeof(cap->driver));
	strscpy(cap->bus_info, "VPFE", sizeof(cap->bus_info));
	strscpy(cap->card, vpfe_dev->cfg->card_name, sizeof(cap->card));
	return 0;
}

static int vpfe_g_fmt_vid_cap(struct file *file, void *priv,
				struct v4l2_format *fmt)
{
	struct vpfe_device *vpfe_dev = video_drvdata(file);

	v4l2_dbg(1, debug, &vpfe_dev->v4l2_dev, "vpfe_g_fmt_vid_cap\n");
	/* Fill in the information about format */
	*fmt = vpfe_dev->fmt;
	return 0;
}

static int vpfe_enum_fmt_vid_cap(struct file *file, void  *priv,
				   struct v4l2_fmtdesc *fmt)
{
	struct vpfe_device *vpfe_dev = video_drvdata(file);
	const struct vpfe_pixel_format *pix_fmt;
	u32 pix;

	v4l2_dbg(1, debug, &vpfe_dev->v4l2_dev, "vpfe_enum_fmt_vid_cap\n");

	if (ccdc_dev->hw_ops.enum_pix(&pix, fmt->index) < 0)
		return -EINVAL;

	/* Fill in the information about format */
	pix_fmt = vpfe_lookup_pix_format(pix);
	if (pix_fmt) {
		fmt->pixelformat = pix_fmt->pixelformat;
		return 0;
	}
	return -EINVAL;
}

static int vpfe_s_fmt_vid_cap(struct file *file, void *priv,
				struct v4l2_format *fmt)
{
	struct vpfe_device *vpfe_dev = video_drvdata(file);
	const struct vpfe_pixel_format *pix_fmts;
	int ret;

	v4l2_dbg(1, debug, &vpfe_dev->v4l2_dev, "vpfe_s_fmt_vid_cap\n");

	/* If streaming is started, return error */
	if (vpfe_dev->started) {
		v4l2_err(&vpfe_dev->v4l2_dev, "Streaming is started\n");
		return -EBUSY;
	}

	/* Check for valid frame format */
	pix_fmts = vpfe_check_format(vpfe_dev, &fmt->fmt.pix);
	if (!pix_fmts)
		return -EINVAL;

	/* store the pixel format in the device  object */
	ret = mutex_lock_interruptible(&vpfe_dev->lock);
	if (ret)
		return ret;

	/* First detach any IRQ if currently attached */
	vpfe_detach_irq(vpfe_dev);
	vpfe_dev->fmt = *fmt;
	/* set image capture parameters in the ccdc */
	ret = vpfe_config_ccdc_image_format(vpfe_dev);
	mutex_unlock(&vpfe_dev->lock);
	return ret;
}

static int vpfe_try_fmt_vid_cap(struct file *file, void *priv,
				  struct v4l2_format *f)
{
	struct vpfe_device *vpfe_dev = video_drvdata(file);
	const struct vpfe_pixel_format *pix_fmts;

	v4l2_dbg(1, debug, &vpfe_dev->v4l2_dev, "vpfe_try_fmt_vid_cap\n");

	pix_fmts = vpfe_check_format(vpfe_dev, &f->fmt.pix);
	if (!pix_fmts)
		return -EINVAL;
	return 0;
}

/*
 * vpfe_get_subdev_input_index - Get subdev index and subdev input index for a
 * given app input index
 */
static int vpfe_get_subdev_input_index(struct vpfe_device *vpfe_dev,
					int *subdev_index,
					int *subdev_input_index,
					int app_input_index)
{
	struct vpfe_config *cfg = vpfe_dev->cfg;
	struct vpfe_subdev_info *sdinfo;
	int i, j = 0;

	for (i = 0; i < cfg->num_subdevs; i++) {
		sdinfo = &cfg->sub_devs[i];
		if (app_input_index < (j + sdinfo->num_inputs)) {
			*subdev_index = i;
			*subdev_input_index = app_input_index - j;
			return 0;
		}
		j += sdinfo->num_inputs;
	}
	return -EINVAL;
}

/*
 * vpfe_get_app_input - Get app input index for a given subdev input index
 * driver stores the input index of the current sub device and translate it
 * when application request the current input
 */
static int vpfe_get_app_input_index(struct vpfe_device *vpfe_dev,
				    int *app_input_index)
{
	struct vpfe_config *cfg = vpfe_dev->cfg;
	struct vpfe_subdev_info *sdinfo;
	int i, j = 0;

	for (i = 0; i < cfg->num_subdevs; i++) {
		sdinfo = &cfg->sub_devs[i];
		if (!strcmp(sdinfo->name, vpfe_dev->current_subdev->name)) {
			if (vpfe_dev->current_input >= sdinfo->num_inputs)
				return -1;
			*app_input_index = j + vpfe_dev->current_input;
			return 0;
		}
		j += sdinfo->num_inputs;
	}
	return -EINVAL;
}

static int vpfe_enum_input(struct file *file, void *priv,
				 struct v4l2_input *inp)
{
	struct vpfe_device *vpfe_dev = video_drvdata(file);
	struct vpfe_subdev_info *sdinfo;
	int subdev, index ;

	v4l2_dbg(1, debug, &vpfe_dev->v4l2_dev, "vpfe_enum_input\n");

	if (vpfe_get_subdev_input_index(vpfe_dev,
					&subdev,
					&index,
					inp->index) < 0) {
		v4l2_err(&vpfe_dev->v4l2_dev, "input information not found for the subdev\n");
		return -EINVAL;
	}
	sdinfo = &vpfe_dev->cfg->sub_devs[subdev];
	*inp = sdinfo->inputs[index];
	return 0;
}

static int vpfe_g_input(struct file *file, void *priv, unsigned int *index)
{
	struct vpfe_device *vpfe_dev = video_drvdata(file);

	v4l2_dbg(1, debug, &vpfe_dev->v4l2_dev, "vpfe_g_input\n");

	return vpfe_get_app_input_index(vpfe_dev, index);
}


static int vpfe_s_input(struct file *file, void *priv, unsigned int index)
{
	struct vpfe_device *vpfe_dev = video_drvdata(file);
	struct v4l2_subdev *sd;
	struct vpfe_subdev_info *sdinfo;
	int subdev_index, inp_index;
	struct vpfe_route *route;
	u32 input, output;
	int ret;

	v4l2_dbg(1, debug, &vpfe_dev->v4l2_dev, "vpfe_s_input\n");

	ret = mutex_lock_interruptible(&vpfe_dev->lock);
	if (ret)
		return ret;

	/*
	 * If streaming is started return device busy
	 * error
	 */
	if (vpfe_dev->started) {
		v4l2_err(&vpfe_dev->v4l2_dev, "Streaming is on\n");
		ret = -EBUSY;
		goto unlock_out;
	}
	ret = vpfe_get_subdev_input_index(vpfe_dev,
					  &subdev_index,
					  &inp_index,
					  index);
	if (ret < 0) {
		v4l2_err(&vpfe_dev->v4l2_dev, "invalid input index\n");
		goto unlock_out;
	}

	sdinfo = &vpfe_dev->cfg->sub_devs[subdev_index];
	sd = vpfe_dev->sd[subdev_index];
	route = &sdinfo->routes[inp_index];
	if (route && sdinfo->can_route) {
		input = route->input;
		output = route->output;
	} else {
		input = 0;
		output = 0;
	}

	if (sd)
		ret = v4l2_subdev_call(sd, video, s_routing, input, output, 0);

	if (ret) {
		v4l2_err(&vpfe_dev->v4l2_dev,
			"vpfe_doioctl:error in setting input in decoder\n");
		ret = -EINVAL;
		goto unlock_out;
	}
	vpfe_dev->current_subdev = sdinfo;
	if (sd)
		vpfe_dev->v4l2_dev.ctrl_handler = sd->ctrl_handler;
	vpfe_dev->current_input = index;
	vpfe_dev->std_index = 0;

	/* set the bus/interface parameter for the sub device in ccdc */
	ret = ccdc_dev->hw_ops.set_hw_if_params(&sdinfo->ccdc_if_params);
	if (ret)
		goto unlock_out;

	/* set the default image parameters in the device */
	ret = vpfe_config_image_format(vpfe_dev,
				vpfe_standards[vpfe_dev->std_index].std_id);
unlock_out:
	mutex_unlock(&vpfe_dev->lock);
	return ret;
}

static int vpfe_querystd(struct file *file, void *priv, v4l2_std_id *std_id)
{
	struct vpfe_device *vpfe_dev = video_drvdata(file);
	struct vpfe_subdev_info *sdinfo;
	int ret;

	v4l2_dbg(1, debug, &vpfe_dev->v4l2_dev, "vpfe_querystd\n");

	ret = mutex_lock_interruptible(&vpfe_dev->lock);
	sdinfo = vpfe_dev->current_subdev;
	if (ret)
		return ret;
	/* Call querystd function of decoder device */
	ret = v4l2_device_call_until_err(&vpfe_dev->v4l2_dev, sdinfo->grp_id,
					 video, querystd, std_id);
	mutex_unlock(&vpfe_dev->lock);
	return ret;
}

static int vpfe_s_std(struct file *file, void *priv, v4l2_std_id std_id)
{
	struct vpfe_device *vpfe_dev = video_drvdata(file);
	struct vpfe_subdev_info *sdinfo;
	int ret;

	v4l2_dbg(1, debug, &vpfe_dev->v4l2_dev, "vpfe_s_std\n");

	/* Call decoder driver function to set the standard */
	ret = mutex_lock_interruptible(&vpfe_dev->lock);
	if (ret)
		return ret;

	sdinfo = vpfe_dev->current_subdev;
	/* If streaming is started, return device busy error */
	if (vpfe_dev->started) {
		v4l2_err(&vpfe_dev->v4l2_dev, "streaming is started\n");
		ret = -EBUSY;
		goto unlock_out;
	}

	ret = v4l2_device_call_until_err(&vpfe_dev->v4l2_dev, sdinfo->grp_id,
					 video, s_std, std_id);
	if (ret < 0) {
		v4l2_err(&vpfe_dev->v4l2_dev, "Failed to set standard\n");
		goto unlock_out;
	}
	ret = vpfe_config_image_format(vpfe_dev, std_id);

unlock_out:
	mutex_unlock(&vpfe_dev->lock);
	return ret;
}

static int vpfe_g_std(struct file *file, void *priv, v4l2_std_id *std_id)
{
	struct vpfe_device *vpfe_dev = video_drvdata(file);

	v4l2_dbg(1, debug, &vpfe_dev->v4l2_dev, "vpfe_g_std\n");

	*std_id = vpfe_standards[vpfe_dev->std_index].std_id;
	return 0;
}
/*
 *  Videobuf operations
 */
static int vpfe_videobuf_setup(struct videobuf_queue *vq,
				unsigned int *count,
				unsigned int *size)
{
	struct vpfe_fh *fh = vq->priv_data;
	struct vpfe_device *vpfe_dev = fh->vpfe_dev;

	v4l2_dbg(1, debug, &vpfe_dev->v4l2_dev, "vpfe_buffer_setup\n");
	*size = vpfe_dev->fmt.fmt.pix.sizeimage;
	if (vpfe_dev->memory == V4L2_MEMORY_MMAP &&
		vpfe_dev->fmt.fmt.pix.sizeimage > config_params.device_bufsize)
		*size = config_params.device_bufsize;

	if (*count < config_params.min_numbuffers)
		*count = config_params.min_numbuffers;
	v4l2_dbg(1, debug, &vpfe_dev->v4l2_dev,
		"count=%d, size=%d\n", *count, *size);
	return 0;
}

static int vpfe_videobuf_prepare(struct videobuf_queue *vq,
				struct videobuf_buffer *vb,
				enum v4l2_field field)
{
	struct vpfe_fh *fh = vq->priv_data;
	struct vpfe_device *vpfe_dev = fh->vpfe_dev;
	unsigned long addr;
	int ret;

	v4l2_dbg(1, debug, &vpfe_dev->v4l2_dev, "vpfe_buffer_prepare\n");

	/* If buffer is not initialized, initialize it */
	if (VIDEOBUF_NEEDS_INIT == vb->state) {
		vb->width = vpfe_dev->fmt.fmt.pix.width;
		vb->height = vpfe_dev->fmt.fmt.pix.height;
		vb->size = vpfe_dev->fmt.fmt.pix.sizeimage;
		vb->field = field;

		ret = videobuf_iolock(vq, vb, NULL);
		if (ret < 0)
			return ret;

		addr = videobuf_to_dma_contig(vb);
		/* Make sure user addresses are aligned to 32 bytes */
		if (!ALIGN(addr, 32))
			return -EINVAL;

		vb->state = VIDEOBUF_PREPARED;
	}
	return 0;
}

static void vpfe_videobuf_queue(struct videobuf_queue *vq,
				struct videobuf_buffer *vb)
{
	/* Get the file handle object and device object */
	struct vpfe_fh *fh = vq->priv_data;
	struct vpfe_device *vpfe_dev = fh->vpfe_dev;
	unsigned long flags;

	v4l2_dbg(1, debug, &vpfe_dev->v4l2_dev, "vpfe_buffer_queue\n");

	/* add the buffer to the DMA queue */
	spin_lock_irqsave(&vpfe_dev->dma_queue_lock, flags);
	list_add_tail(&vb->queue, &vpfe_dev->dma_queue);
	spin_unlock_irqrestore(&vpfe_dev->dma_queue_lock, flags);

	/* Change state of the buffer */
	vb->state = VIDEOBUF_QUEUED;
}

static void vpfe_videobuf_release(struct videobuf_queue *vq,
				  struct videobuf_buffer *vb)
{
	struct vpfe_fh *fh = vq->priv_data;
	struct vpfe_device *vpfe_dev = fh->vpfe_dev;
	unsigned long flags;

	v4l2_dbg(1, debug, &vpfe_dev->v4l2_dev, "vpfe_videobuf_release\n");

	/*
	 * We need to flush the buffer from the dma queue since
	 * they are de-allocated
	 */
	spin_lock_irqsave(&vpfe_dev->dma_queue_lock, flags);
	INIT_LIST_HEAD(&vpfe_dev->dma_queue);
	spin_unlock_irqrestore(&vpfe_dev->dma_queue_lock, flags);
	videobuf_dma_contig_free(vq, vb);
	vb->state = VIDEOBUF_NEEDS_INIT;
}

static const struct videobuf_queue_ops vpfe_videobuf_qops = {
	.buf_setup      = vpfe_videobuf_setup,
	.buf_prepare    = vpfe_videobuf_prepare,
	.buf_queue      = vpfe_videobuf_queue,
	.buf_release    = vpfe_videobuf_release,
};

/*
 * vpfe_reqbufs. currently support REQBUF only once opening
 * the device.
 */
static int vpfe_reqbufs(struct file *file, void *priv,
			struct v4l2_requestbuffers *req_buf)
{
	struct vpfe_device *vpfe_dev = video_drvdata(file);
	struct vpfe_fh *fh = file->private_data;
	int ret;

	v4l2_dbg(1, debug, &vpfe_dev->v4l2_dev, "vpfe_reqbufs\n");

	if (V4L2_BUF_TYPE_VIDEO_CAPTURE != req_buf->type) {
		v4l2_err(&vpfe_dev->v4l2_dev, "Invalid buffer type\n");
		return -EINVAL;
	}

	ret = mutex_lock_interruptible(&vpfe_dev->lock);
	if (ret)
		return ret;

	if (vpfe_dev->io_usrs != 0) {
		v4l2_err(&vpfe_dev->v4l2_dev, "Only one IO user allowed\n");
		ret = -EBUSY;
		goto unlock_out;
	}

	vpfe_dev->memory = req_buf->memory;
	videobuf_queue_dma_contig_init(&vpfe_dev->buffer_queue,
				&vpfe_videobuf_qops,
				vpfe_dev->pdev,
				&vpfe_dev->irqlock,
				req_buf->type,
				vpfe_dev->fmt.fmt.pix.field,
				sizeof(struct videobuf_buffer),
				fh, NULL);

	fh->io_allowed = 1;
	vpfe_dev->io_usrs = 1;
	INIT_LIST_HEAD(&vpfe_dev->dma_queue);
	ret = videobuf_reqbufs(&vpfe_dev->buffer_queue, req_buf);
unlock_out:
	mutex_unlock(&vpfe_dev->lock);
	return ret;
}

static int vpfe_querybuf(struct file *file, void *priv,
			 struct v4l2_buffer *buf)
{
	struct vpfe_device *vpfe_dev = video_drvdata(file);

	v4l2_dbg(1, debug, &vpfe_dev->v4l2_dev, "vpfe_querybuf\n");

	if (V4L2_BUF_TYPE_VIDEO_CAPTURE != buf->type) {
		v4l2_err(&vpfe_dev->v4l2_dev, "Invalid buf type\n");
		return  -EINVAL;
	}

	if (vpfe_dev->memory != V4L2_MEMORY_MMAP) {
		v4l2_err(&vpfe_dev->v4l2_dev, "Invalid memory\n");
		return -EINVAL;
	}
	/* Call videobuf_querybuf to get information */
	return videobuf_querybuf(&vpfe_dev->buffer_queue, buf);
}

static int vpfe_qbuf(struct file *file, void *priv,
		     struct v4l2_buffer *p)
{
	struct vpfe_device *vpfe_dev = video_drvdata(file);
	struct vpfe_fh *fh = file->private_data;

	v4l2_dbg(1, debug, &vpfe_dev->v4l2_dev, "vpfe_qbuf\n");

	if (V4L2_BUF_TYPE_VIDEO_CAPTURE != p->type) {
		v4l2_err(&vpfe_dev->v4l2_dev, "Invalid buf type\n");
		return -EINVAL;
	}

	/*
	 * If this file handle is not allowed to do IO,
	 * return error
	 */
	if (!fh->io_allowed) {
		v4l2_err(&vpfe_dev->v4l2_dev, "fh->io_allowed\n");
		return -EACCES;
	}
	return videobuf_qbuf(&vpfe_dev->buffer_queue, p);
}

static int vpfe_dqbuf(struct file *file, void *priv,
		      struct v4l2_buffer *buf)
{
	struct vpfe_device *vpfe_dev = video_drvdata(file);

	v4l2_dbg(1, debug, &vpfe_dev->v4l2_dev, "vpfe_dqbuf\n");

	if (V4L2_BUF_TYPE_VIDEO_CAPTURE != buf->type) {
		v4l2_err(&vpfe_dev->v4l2_dev, "Invalid buf type\n");
		return -EINVAL;
	}
	return videobuf_dqbuf(&vpfe_dev->buffer_queue,
				      buf, file->f_flags & O_NONBLOCK);
}

/*
 * vpfe_calculate_offsets : This function calculates buffers offset
 * for top and bottom field
 */
static void vpfe_calculate_offsets(struct vpfe_device *vpfe_dev)
{
	struct v4l2_rect image_win;

	v4l2_dbg(1, debug, &vpfe_dev->v4l2_dev, "vpfe_calculate_offsets\n");

	ccdc_dev->hw_ops.get_image_window(&image_win);
	vpfe_dev->field_off = image_win.height * image_win.width;
}

/* vpfe_start_ccdc_capture: start streaming in ccdc/isif */
static void vpfe_start_ccdc_capture(struct vpfe_device *vpfe_dev)
{
	ccdc_dev->hw_ops.enable(1);
	if (ccdc_dev->hw_ops.enable_out_to_sdram)
		ccdc_dev->hw_ops.enable_out_to_sdram(1);
	vpfe_dev->started = 1;
}

/*
 * vpfe_streamon. Assume the DMA queue is not empty.
 * application is expected to call QBUF before calling
 * this ioctl. If not, driver returns error
 */
static int vpfe_streamon(struct file *file, void *priv,
			 enum v4l2_buf_type buf_type)
{
	struct vpfe_device *vpfe_dev = video_drvdata(file);
	struct vpfe_fh *fh = file->private_data;
	struct vpfe_subdev_info *sdinfo;
	unsigned long addr;
	int ret;

	v4l2_dbg(1, debug, &vpfe_dev->v4l2_dev, "vpfe_streamon\n");

	if (V4L2_BUF_TYPE_VIDEO_CAPTURE != buf_type) {
		v4l2_err(&vpfe_dev->v4l2_dev, "Invalid buf type\n");
		return -EINVAL;
	}

	/* If file handle is not allowed IO, return error */
	if (!fh->io_allowed) {
		v4l2_err(&vpfe_dev->v4l2_dev, "fh->io_allowed\n");
		return -EACCES;
	}

	sdinfo = vpfe_dev->current_subdev;
	ret = v4l2_device_call_until_err(&vpfe_dev->v4l2_dev, sdinfo->grp_id,
					video, s_stream, 1);

	if (ret && (ret != -ENOIOCTLCMD)) {
		v4l2_err(&vpfe_dev->v4l2_dev, "stream on failed in subdev\n");
		return -EINVAL;
	}

	/* If buffer queue is empty, return error */
	if (list_empty(&vpfe_dev->buffer_queue.stream)) {
		v4l2_err(&vpfe_dev->v4l2_dev, "buffer queue is empty\n");
		return -EIO;
	}

	/* Call videobuf_streamon to start streaming * in videobuf */
	ret = videobuf_streamon(&vpfe_dev->buffer_queue);
	if (ret)
		return ret;


	ret = mutex_lock_interruptible(&vpfe_dev->lock);
	if (ret)
		goto streamoff;
	/* Get the next frame from the buffer queue */
	vpfe_dev->next_frm = list_entry(vpfe_dev->dma_queue.next,
					struct videobuf_buffer, queue);
	vpfe_dev->cur_frm = vpfe_dev->next_frm;
	/* Remove buffer from the buffer queue */
	list_del(&vpfe_dev->cur_frm->queue);
	/* Mark state of the current frame to active */
	vpfe_dev->cur_frm->state = VIDEOBUF_ACTIVE;
	/* Initialize field_id and started member */
	vpfe_dev->field_id = 0;
	addr = videobuf_to_dma_contig(vpfe_dev->cur_frm);

	/* Calculate field offset */
	vpfe_calculate_offsets(vpfe_dev);

	if (vpfe_attach_irq(vpfe_dev) < 0) {
		v4l2_err(&vpfe_dev->v4l2_dev,
			 "Error in attaching interrupt handle\n");
		ret = -EFAULT;
		goto unlock_out;
	}
	if (ccdc_dev->hw_ops.configure() < 0) {
		v4l2_err(&vpfe_dev->v4l2_dev,
			 "Error in configuring ccdc\n");
		ret = -EINVAL;
		goto unlock_out;
	}
	ccdc_dev->hw_ops.setfbaddr((unsigned long)(addr));
	vpfe_start_ccdc_capture(vpfe_dev);
	mutex_unlock(&vpfe_dev->lock);
	return ret;
unlock_out:
	mutex_unlock(&vpfe_dev->lock);
streamoff:
	videobuf_streamoff(&vpfe_dev->buffer_queue);
	return ret;
}

static int vpfe_streamoff(struct file *file, void *priv,
			  enum v4l2_buf_type buf_type)
{
	struct vpfe_device *vpfe_dev = video_drvdata(file);
	struct vpfe_fh *fh = file->private_data;
	struct vpfe_subdev_info *sdinfo;
	int ret;

	v4l2_dbg(1, debug, &vpfe_dev->v4l2_dev, "vpfe_streamoff\n");

	if (V4L2_BUF_TYPE_VIDEO_CAPTURE != buf_type) {
		v4l2_err(&vpfe_dev->v4l2_dev, "Invalid buf type\n");
		return -EINVAL;
	}

	/* If io is allowed for this file handle, return error */
	if (!fh->io_allowed) {
		v4l2_err(&vpfe_dev->v4l2_dev, "fh->io_allowed\n");
		return -EACCES;
	}

	/* If streaming is not started, return error */
	if (!vpfe_dev->started) {
		v4l2_err(&vpfe_dev->v4l2_dev, "device started\n");
		return -EINVAL;
	}

	ret = mutex_lock_interruptible(&vpfe_dev->lock);
	if (ret)
		return ret;

	vpfe_stop_ccdc_capture(vpfe_dev);
	vpfe_detach_irq(vpfe_dev);

	sdinfo = vpfe_dev->current_subdev;
	ret = v4l2_device_call_until_err(&vpfe_dev->v4l2_dev, sdinfo->grp_id,
					video, s_stream, 0);

	if (ret && (ret != -ENOIOCTLCMD))
		v4l2_err(&vpfe_dev->v4l2_dev, "stream off failed in subdev\n");
	ret = videobuf_streamoff(&vpfe_dev->buffer_queue);
	mutex_unlock(&vpfe_dev->lock);
	return ret;
}

static int vpfe_g_pixelaspect(struct file *file, void *priv,
			      int type, struct v4l2_fract *f)
{
	struct vpfe_device *vpfe_dev = video_drvdata(file);

	v4l2_dbg(1, debug, &vpfe_dev->v4l2_dev, "vpfe_g_pixelaspect\n");

	if (type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;
	/* If std_index is invalid, then just return (== 1:1 aspect) */
	if (vpfe_dev->std_index >= ARRAY_SIZE(vpfe_standards))
		return 0;

	*f = vpfe_standards[vpfe_dev->std_index].pixelaspect;
	return 0;
}

static int vpfe_g_selection(struct file *file, void *priv,
			    struct v4l2_selection *sel)
{
	struct vpfe_device *vpfe_dev = video_drvdata(file);

	v4l2_dbg(1, debug, &vpfe_dev->v4l2_dev, "vpfe_g_selection\n");

	if (sel->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	switch (sel->target) {
	case V4L2_SEL_TGT_CROP:
		sel->r = vpfe_dev->crop;
		break;
	case V4L2_SEL_TGT_CROP_DEFAULT:
	case V4L2_SEL_TGT_CROP_BOUNDS:
		sel->r.width = vpfe_standards[vpfe_dev->std_index].width;
		sel->r.height = vpfe_standards[vpfe_dev->std_index].height;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int vpfe_s_selection(struct file *file, void *priv,
			    struct v4l2_selection *sel)
{
	struct vpfe_device *vpfe_dev = video_drvdata(file);
	struct v4l2_rect rect = sel->r;
	int ret;

	v4l2_dbg(1, debug, &vpfe_dev->v4l2_dev, "vpfe_s_selection\n");

	if (sel->type != V4L2_BUF_TYPE_VIDEO_CAPTURE ||
	    sel->target != V4L2_SEL_TGT_CROP)
		return -EINVAL;

	if (vpfe_dev->started) {
		/* make sure streaming is not started */
		v4l2_err(&vpfe_dev->v4l2_dev,
			"Cannot change crop when streaming is ON\n");
		return -EBUSY;
	}

	ret = mutex_lock_interruptible(&vpfe_dev->lock);
	if (ret)
		return ret;

	if (rect.top < 0 || rect.left < 0) {
		v4l2_err(&vpfe_dev->v4l2_dev,
			"doesn't support negative values for top & left\n");
		ret = -EINVAL;
		goto unlock_out;
	}

	/* adjust the width to 16 pixel boundary */
	rect.width = ((rect.width + 15) & ~0xf);

	/* make sure parameters are valid */
	if ((rect.left + rect.width >
		vpfe_dev->std_info.active_pixels) ||
	    (rect.top + rect.height >
		vpfe_dev->std_info.active_lines)) {
		v4l2_err(&vpfe_dev->v4l2_dev, "Error in S_SELECTION params\n");
		ret = -EINVAL;
		goto unlock_out;
	}
	ccdc_dev->hw_ops.set_image_window(&rect);
	vpfe_dev->fmt.fmt.pix.width = rect.width;
	vpfe_dev->fmt.fmt.pix.height = rect.height;
	vpfe_dev->fmt.fmt.pix.bytesperline =
		ccdc_dev->hw_ops.get_line_length();
	vpfe_dev->fmt.fmt.pix.sizeimage =
		vpfe_dev->fmt.fmt.pix.bytesperline *
		vpfe_dev->fmt.fmt.pix.height;
	vpfe_dev->crop = rect;
	sel->r = rect;
unlock_out:
	mutex_unlock(&vpfe_dev->lock);
	return ret;
}

/* vpfe capture ioctl operations */
static const struct v4l2_ioctl_ops vpfe_ioctl_ops = {
	.vidioc_querycap	 = vpfe_querycap,
	.vidioc_g_fmt_vid_cap    = vpfe_g_fmt_vid_cap,
	.vidioc_enum_fmt_vid_cap = vpfe_enum_fmt_vid_cap,
	.vidioc_s_fmt_vid_cap    = vpfe_s_fmt_vid_cap,
	.vidioc_try_fmt_vid_cap  = vpfe_try_fmt_vid_cap,
	.vidioc_enum_input	 = vpfe_enum_input,
	.vidioc_g_input		 = vpfe_g_input,
	.vidioc_s_input		 = vpfe_s_input,
	.vidioc_querystd	 = vpfe_querystd,
	.vidioc_s_std		 = vpfe_s_std,
	.vidioc_g_std		 = vpfe_g_std,
	.vidioc_reqbufs		 = vpfe_reqbufs,
	.vidioc_querybuf	 = vpfe_querybuf,
	.vidioc_qbuf		 = vpfe_qbuf,
	.vidioc_dqbuf		 = vpfe_dqbuf,
	.vidioc_streamon	 = vpfe_streamon,
	.vidioc_streamoff	 = vpfe_streamoff,
	.vidioc_g_pixelaspect	 = vpfe_g_pixelaspect,
	.vidioc_g_selection	 = vpfe_g_selection,
	.vidioc_s_selection	 = vpfe_s_selection,
};

static struct vpfe_device *vpfe_initialize(void)
{
	struct vpfe_device *vpfe_dev;

	/* Default number of buffers should be 3 */
	if ((numbuffers > 0) &&
	    (numbuffers < config_params.min_numbuffers))
		numbuffers = config_params.min_numbuffers;

	/*
	 * Set buffer size to min buffers size if invalid buffer size is
	 * given
	 */
	if (bufsize < config_params.min_bufsize)
		bufsize = config_params.min_bufsize;

	config_params.numbuffers = numbuffers;

	if (numbuffers)
		config_params.device_bufsize = bufsize;

	/* Allocate memory for device objects */
	vpfe_dev = kzalloc(sizeof(*vpfe_dev), GFP_KERNEL);

	return vpfe_dev;
}

/*
 * vpfe_probe : This function creates device entries by register
 * itself to the V4L2 driver and initializes fields of each
 * device objects
 */
static int vpfe_probe(struct platform_device *pdev)
{
	struct vpfe_subdev_info *sdinfo;
	struct vpfe_config *vpfe_cfg;
	struct resource *res1;
	struct vpfe_device *vpfe_dev;
	struct i2c_adapter *i2c_adap;
	struct video_device *vfd;
	int ret, i, j;
	int num_subdevs = 0;

	/* Get the pointer to the device object */
	vpfe_dev = vpfe_initialize();

	if (!vpfe_dev) {
		v4l2_err(pdev->dev.driver,
			"Failed to allocate memory for vpfe_dev\n");
		return -ENOMEM;
	}

	vpfe_dev->pdev = &pdev->dev;

	if (!pdev->dev.platform_data) {
		v4l2_err(pdev->dev.driver, "Unable to get vpfe config\n");
		ret = -ENODEV;
		goto probe_free_dev_mem;
	}

	vpfe_cfg = pdev->dev.platform_data;
	vpfe_dev->cfg = vpfe_cfg;
	if (!vpfe_cfg->ccdc || !vpfe_cfg->card_name || !vpfe_cfg->sub_devs) {
		v4l2_err(pdev->dev.driver, "null ptr in vpfe_cfg\n");
		ret = -ENOENT;
		goto probe_free_dev_mem;
	}

	/* Allocate memory for ccdc configuration */
	ccdc_cfg = kmalloc(sizeof(*ccdc_cfg), GFP_KERNEL);
	if (!ccdc_cfg) {
		ret = -ENOMEM;
		goto probe_free_dev_mem;
	}

	mutex_lock(&ccdc_lock);

	strscpy(ccdc_cfg->name, vpfe_cfg->ccdc, sizeof(ccdc_cfg->name));
	/* Get VINT0 irq resource */
	res1 = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if (!res1) {
		v4l2_err(pdev->dev.driver,
			 "Unable to get interrupt for VINT0\n");
		ret = -ENODEV;
		goto probe_free_ccdc_cfg_mem;
	}
	vpfe_dev->ccdc_irq0 = res1->start;

	/* Get VINT1 irq resource */
	res1 = platform_get_resource(pdev, IORESOURCE_IRQ, 1);
	if (!res1) {
		v4l2_err(pdev->dev.driver,
			 "Unable to get interrupt for VINT1\n");
		ret = -ENODEV;
		goto probe_free_ccdc_cfg_mem;
	}
	vpfe_dev->ccdc_irq1 = res1->start;

	ret = request_irq(vpfe_dev->ccdc_irq0, vpfe_isr, 0,
			  "vpfe_capture0", vpfe_dev);

	if (0 != ret) {
		v4l2_err(pdev->dev.driver, "Unable to request interrupt\n");
		goto probe_free_ccdc_cfg_mem;
	}

	vfd = &vpfe_dev->video_dev;
	/* Initialize field of video device */
	vfd->release		= video_device_release_empty;
	vfd->fops		= &vpfe_fops;
	vfd->ioctl_ops		= &vpfe_ioctl_ops;
	vfd->tvnorms		= 0;
	vfd->v4l2_dev		= &vpfe_dev->v4l2_dev;
	vfd->device_caps	= V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING;
	snprintf(vfd->name, sizeof(vfd->name),
		 "%s_V%d.%d.%d",
		 CAPTURE_DRV_NAME,
		 (VPFE_CAPTURE_VERSION_CODE >> 16) & 0xff,
		 (VPFE_CAPTURE_VERSION_CODE >> 8) & 0xff,
		 (VPFE_CAPTURE_VERSION_CODE) & 0xff);

	ret = v4l2_device_register(&pdev->dev, &vpfe_dev->v4l2_dev);
	if (ret) {
		v4l2_err(pdev->dev.driver,
			"Unable to register v4l2 device.\n");
		goto probe_out_release_irq;
	}
	v4l2_info(&vpfe_dev->v4l2_dev, "v4l2 device registered\n");
	spin_lock_init(&vpfe_dev->irqlock);
	spin_lock_init(&vpfe_dev->dma_queue_lock);
	mutex_init(&vpfe_dev->lock);

	/* Initialize field of the device objects */
	vpfe_dev->numbuffers = config_params.numbuffers;

	/* register video device */
	v4l2_dbg(1, debug, &vpfe_dev->v4l2_dev,
		"trying to register vpfe device.\n");
	v4l2_dbg(1, debug, &vpfe_dev->v4l2_dev,
		"video_dev=%p\n", &vpfe_dev->video_dev);
	vpfe_dev->fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	ret = video_register_device(&vpfe_dev->video_dev,
				    VFL_TYPE_VIDEO, -1);

	if (ret) {
		v4l2_err(pdev->dev.driver,
			"Unable to register video device.\n");
		goto probe_out_v4l2_unregister;
	}

	v4l2_info(&vpfe_dev->v4l2_dev, "video device registered\n");
	/* set the driver data in platform device */
	platform_set_drvdata(pdev, vpfe_dev);
	/* set driver private data */
	video_set_drvdata(&vpfe_dev->video_dev, vpfe_dev);
	i2c_adap = i2c_get_adapter(vpfe_cfg->i2c_adapter_id);
	num_subdevs = vpfe_cfg->num_subdevs;
	vpfe_dev->sd = kmalloc_array(num_subdevs,
				     sizeof(*vpfe_dev->sd),
				     GFP_KERNEL);
	if (!vpfe_dev->sd) {
		ret = -ENOMEM;
		goto probe_out_video_unregister;
	}

	for (i = 0; i < num_subdevs; i++) {
		struct v4l2_input *inps;

		sdinfo = &vpfe_cfg->sub_devs[i];

		/* Load up the subdevice */
		vpfe_dev->sd[i] =
			v4l2_i2c_new_subdev_board(&vpfe_dev->v4l2_dev,
						  i2c_adap,
						  &sdinfo->board_info,
						  NULL);
		if (vpfe_dev->sd[i]) {
			v4l2_info(&vpfe_dev->v4l2_dev,
				  "v4l2 sub device %s registered\n",
				  sdinfo->name);
			vpfe_dev->sd[i]->grp_id = sdinfo->grp_id;
			/* update tvnorms from the sub devices */
			for (j = 0; j < sdinfo->num_inputs; j++) {
				inps = &sdinfo->inputs[j];
				vfd->tvnorms |= inps->std;
			}
		} else {
			v4l2_info(&vpfe_dev->v4l2_dev,
				  "v4l2 sub device %s register fails\n",
				  sdinfo->name);
			ret = -ENXIO;
			goto probe_sd_out;
		}
	}

	/* set first sub device as current one */
	vpfe_dev->current_subdev = &vpfe_cfg->sub_devs[0];
	vpfe_dev->v4l2_dev.ctrl_handler = vpfe_dev->sd[0]->ctrl_handler;

	/* We have at least one sub device to work with */
	mutex_unlock(&ccdc_lock);
	return 0;

probe_sd_out:
	kfree(vpfe_dev->sd);
probe_out_video_unregister:
	video_unregister_device(&vpfe_dev->video_dev);
probe_out_v4l2_unregister:
	v4l2_device_unregister(&vpfe_dev->v4l2_dev);
probe_out_release_irq:
	free_irq(vpfe_dev->ccdc_irq0, vpfe_dev);
probe_free_ccdc_cfg_mem:
	kfree(ccdc_cfg);
	mutex_unlock(&ccdc_lock);
probe_free_dev_mem:
	kfree(vpfe_dev);
	return ret;
}

/*
 * vpfe_remove : It un-register device from V4L2 driver
 */
static int vpfe_remove(struct platform_device *pdev)
{
	struct vpfe_device *vpfe_dev = platform_get_drvdata(pdev);

	v4l2_info(pdev->dev.driver, "vpfe_remove\n");

	free_irq(vpfe_dev->ccdc_irq0, vpfe_dev);
	kfree(vpfe_dev->sd);
	v4l2_device_unregister(&vpfe_dev->v4l2_dev);
	video_unregister_device(&vpfe_dev->video_dev);
	kfree(vpfe_dev);
	kfree(ccdc_cfg);
	return 0;
}

static int vpfe_suspend(struct device *dev)
{
	return 0;
}

static int vpfe_resume(struct device *dev)
{
	return 0;
}

static const struct dev_pm_ops vpfe_dev_pm_ops = {
	.suspend = vpfe_suspend,
	.resume = vpfe_resume,
};

static struct platform_driver vpfe_driver = {
	.driver = {
		.name = CAPTURE_DRV_NAME,
		.pm = &vpfe_dev_pm_ops,
	},
	.probe = vpfe_probe,
	.remove = vpfe_remove,
};

module_platform_driver(vpfe_driver);
