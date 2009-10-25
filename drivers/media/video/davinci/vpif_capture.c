/*
 * Copyright (C) 2009 Texas Instruments Inc
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 * TODO : add support for VBI & HBI data service
 *	  add static buffer allocation
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/workqueue.h>
#include <linux/string.h>
#include <linux/videodev2.h>
#include <linux/wait.h>
#include <linux/time.h>
#include <linux/i2c.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/version.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ioctl.h>

#include "vpif_capture.h"
#include "vpif.h"

MODULE_DESCRIPTION("TI DaVinci VPIF Capture driver");
MODULE_LICENSE("GPL");

#define vpif_err(fmt, arg...)	v4l2_err(&vpif_obj.v4l2_dev, fmt, ## arg)
#define vpif_dbg(level, debug, fmt, arg...)	\
		v4l2_dbg(level, debug, &vpif_obj.v4l2_dev, fmt, ## arg)

static int debug = 1;
static u32 ch0_numbuffers = 3;
static u32 ch1_numbuffers = 3;
static u32 ch0_bufsize = 1920 * 1080 * 2;
static u32 ch1_bufsize = 720 * 576 * 2;

module_param(debug, int, 0644);
module_param(ch0_numbuffers, uint, S_IRUGO);
module_param(ch1_numbuffers, uint, S_IRUGO);
module_param(ch0_bufsize, uint, S_IRUGO);
module_param(ch1_bufsize, uint, S_IRUGO);

MODULE_PARM_DESC(debug, "Debug level 0-1");
MODULE_PARM_DESC(ch2_numbuffers, "Channel0 buffer count (default:3)");
MODULE_PARM_DESC(ch3_numbuffers, "Channel1 buffer count (default:3)");
MODULE_PARM_DESC(ch2_bufsize, "Channel0 buffer size (default:1920 x 1080 x 2)");
MODULE_PARM_DESC(ch3_bufsize, "Channel1 buffer size (default:720 x 576 x 2)");

static struct vpif_config_params config_params = {
	.min_numbuffers = 3,
	.numbuffers[0] = 3,
	.numbuffers[1] = 3,
	.min_bufsize[0] = 720 * 480 * 2,
	.min_bufsize[1] = 720 * 480 * 2,
	.channel_bufsize[0] = 1920 * 1080 * 2,
	.channel_bufsize[1] = 720 * 576 * 2,
};

/* global variables */
static struct vpif_device vpif_obj = { {NULL} };
static struct device *vpif_dev;

/**
 * ch_params: video standard configuration parameters for vpif
 */
static const struct vpif_channel_config_params ch_params[] = {
	{
		"NTSC_M", 720, 480, 30, 0, 1, 268, 1440, 1, 23, 263, 266,
		286, 525, 525, 0, 1, 0, V4L2_STD_525_60,
	},
	{
		"PAL_BDGHIK", 720, 576, 25, 0, 1, 280, 1440, 1, 23, 311, 313,
		336, 624, 625, 0, 1, 0, V4L2_STD_625_50,
	},
};

/**
 * vpif_uservirt_to_phys : translate user/virtual address to phy address
 * @virtp: user/virtual address
 *
 * This inline function is used to convert user space virtual address to
 * physical address.
 */
static inline u32 vpif_uservirt_to_phys(u32 virtp)
{
	unsigned long physp = 0;
	struct mm_struct *mm = current->mm;
	struct vm_area_struct *vma;

	vma = find_vma(mm, virtp);

	/* For kernel direct-mapped memory, take the easy way */
	if (virtp >= PAGE_OFFSET)
		physp = virt_to_phys((void *)virtp);
	else if (vma && (vma->vm_flags & VM_IO) && (vma->vm_pgoff))
		/**
		 * this will catch, kernel-allocated, mmaped-to-usermode
		 * addresses
		 */
		physp = (vma->vm_pgoff << PAGE_SHIFT) + (virtp - vma->vm_start);
	else {
		/* otherwise, use get_user_pages() for general userland pages */
		int res, nr_pages = 1;
			struct page *pages;

		down_read(&current->mm->mmap_sem);

		res = get_user_pages(current, current->mm,
				     virtp, nr_pages, 1, 0, &pages, NULL);
		up_read(&current->mm->mmap_sem);

		if (res == nr_pages)
			physp = __pa(page_address(&pages[0]) +
				     (virtp & ~PAGE_MASK));
		else {
			vpif_err("get_user_pages failed\n");
			return 0;
		}
	}
	return physp;
}

/**
 * buffer_prepare :  callback function for buffer prepare
 * @q : buffer queue ptr
 * @vb: ptr to video buffer
 * @field: field info
 *
 * This is the callback function for buffer prepare when videobuf_qbuf()
 * function is called. The buffer is prepared and user space virtual address
 * or user address is converted into  physical address
 */
static int vpif_buffer_prepare(struct videobuf_queue *q,
			       struct videobuf_buffer *vb,
			       enum v4l2_field field)
{
	/* Get the file handle object and channel object */
	struct vpif_fh *fh = q->priv_data;
	struct channel_obj *ch = fh->channel;
	struct common_obj *common;
	unsigned long addr;


	vpif_dbg(2, debug, "vpif_buffer_prepare\n");

	common = &ch->common[VPIF_VIDEO_INDEX];

	/* If buffer is not initialized, initialize it */
	if (VIDEOBUF_NEEDS_INIT == vb->state) {
		vb->width = common->width;
		vb->height = common->height;
		vb->size = vb->width * vb->height;
		vb->field = field;
	}
	vb->state = VIDEOBUF_PREPARED;
	/**
	 * if user pointer memory mechanism is used, get the physical
	 * address of the buffer
	 */
	if (V4L2_MEMORY_USERPTR == common->memory) {
		if (0 == vb->baddr) {
			vpif_dbg(1, debug, "buffer address is 0\n");
			return -EINVAL;

		}
		vb->boff = vpif_uservirt_to_phys(vb->baddr);
		if (!IS_ALIGNED(vb->boff, 8))
			goto exit;
	}

	addr = vb->boff;
	if (q->streaming) {
		if (!IS_ALIGNED((addr + common->ytop_off), 8) ||
		    !IS_ALIGNED((addr + common->ybtm_off), 8) ||
		    !IS_ALIGNED((addr + common->ctop_off), 8) ||
		    !IS_ALIGNED((addr + common->cbtm_off), 8))
			goto exit;
	}
	return 0;
exit:
	vpif_dbg(1, debug, "buffer_prepare:offset is not aligned to 8 bytes\n");
	return -EINVAL;
}

/**
 * vpif_buffer_setup : Callback function for buffer setup.
 * @q: buffer queue ptr
 * @count: number of buffers
 * @size: size of the buffer
 *
 * This callback function is called when reqbuf() is called to adjust
 * the buffer count and buffer size
 */
static int vpif_buffer_setup(struct videobuf_queue *q, unsigned int *count,
			     unsigned int *size)
{
	/* Get the file handle object and channel object */
	struct vpif_fh *fh = q->priv_data;
	struct channel_obj *ch = fh->channel;
	struct common_obj *common;

	common = &ch->common[VPIF_VIDEO_INDEX];

	vpif_dbg(2, debug, "vpif_buffer_setup\n");

	/* If memory type is not mmap, return */
	if (V4L2_MEMORY_MMAP != common->memory)
		return 0;

	/* Calculate the size of the buffer */
	*size = config_params.channel_bufsize[ch->channel_id];

	if (*count < config_params.min_numbuffers)
		*count = config_params.min_numbuffers;
	return 0;
}

/**
 * vpif_buffer_queue : Callback function to add buffer to DMA queue
 * @q: ptr to videobuf_queue
 * @vb: ptr to videobuf_buffer
 */
static void vpif_buffer_queue(struct videobuf_queue *q,
			      struct videobuf_buffer *vb)
{
	/* Get the file handle object and channel object */
	struct vpif_fh *fh = q->priv_data;
	struct channel_obj *ch = fh->channel;
	struct common_obj *common;

	common = &ch->common[VPIF_VIDEO_INDEX];

	vpif_dbg(2, debug, "vpif_buffer_queue\n");

	/* add the buffer to the DMA queue */
	list_add_tail(&vb->queue, &common->dma_queue);
	/* Change state of the buffer */
	vb->state = VIDEOBUF_QUEUED;
}

/**
 * vpif_buffer_release : Callback function to free buffer
 * @q: buffer queue ptr
 * @vb: ptr to video buffer
 *
 * This function is called from the videobuf layer to free memory
 * allocated to  the buffers
 */
static void vpif_buffer_release(struct videobuf_queue *q,
				struct videobuf_buffer *vb)
{
	/* Get the file handle object and channel object */
	struct vpif_fh *fh = q->priv_data;
	struct channel_obj *ch = fh->channel;
	struct common_obj *common;

	common = &ch->common[VPIF_VIDEO_INDEX];

	videobuf_dma_contig_free(q, vb);
	vb->state = VIDEOBUF_NEEDS_INIT;
}

static struct videobuf_queue_ops video_qops = {
	.buf_setup = vpif_buffer_setup,
	.buf_prepare = vpif_buffer_prepare,
	.buf_queue = vpif_buffer_queue,
	.buf_release = vpif_buffer_release,
};

static u8 channel_first_int[VPIF_NUMBER_OF_OBJECTS][2] =
	{ {1, 1} };

/**
 * vpif_process_buffer_complete: process a completed buffer
 * @common: ptr to common channel object
 *
 * This function time stamp the buffer and mark it as DONE. It also
 * wake up any process waiting on the QUEUE and set the next buffer
 * as current
 */
static void vpif_process_buffer_complete(struct common_obj *common)
{
	do_gettimeofday(&common->cur_frm->ts);
	common->cur_frm->state = VIDEOBUF_DONE;
	wake_up_interruptible(&common->cur_frm->done);
	/* Make curFrm pointing to nextFrm */
	common->cur_frm = common->next_frm;
}

/**
 * vpif_schedule_next_buffer: set next buffer address for capture
 * @common : ptr to common channel object
 *
 * This function will get next buffer from the dma queue and
 * set the buffer address in the vpif register for capture.
 * the buffer is marked active
 */
static void vpif_schedule_next_buffer(struct common_obj *common)
{
	unsigned long addr = 0;

	common->next_frm = list_entry(common->dma_queue.next,
				     struct videobuf_buffer, queue);
	/* Remove that buffer from the buffer queue */
	list_del(&common->next_frm->queue);
	common->next_frm->state = VIDEOBUF_ACTIVE;
	if (V4L2_MEMORY_USERPTR == common->memory)
		addr = common->next_frm->boff;
	else
		addr = videobuf_to_dma_contig(common->next_frm);

	/* Set top and bottom field addresses in VPIF registers */
	common->set_addr(addr + common->ytop_off,
			 addr + common->ybtm_off,
			 addr + common->ctop_off,
			 addr + common->cbtm_off);
}

/**
 * vpif_channel_isr : ISR handler for vpif capture
 * @irq: irq number
 * @dev_id: dev_id ptr
 *
 * It changes status of the captured buffer, takes next buffer from the queue
 * and sets its address in VPIF  registers
 */
static irqreturn_t vpif_channel_isr(int irq, void *dev_id)
{
	struct vpif_device *dev = &vpif_obj;
	struct common_obj *common;
	struct channel_obj *ch;
	enum v4l2_field field;
	int channel_id = 0;
	int fid = -1, i;

	channel_id = *(int *)(dev_id);
	ch = dev->dev[channel_id];

	field = ch->common[VPIF_VIDEO_INDEX].fmt.fmt.pix.field;

	for (i = 0; i < VPIF_NUMBER_OF_OBJECTS; i++) {
		common = &ch->common[i];
		/* skip If streaming is not started in this channel */
		if (0 == common->started)
			continue;

		/* Check the field format */
		if (1 == ch->vpifparams.std_info.frm_fmt) {
			/* Progressive mode */
			if (list_empty(&common->dma_queue))
				continue;

			if (!channel_first_int[i][channel_id])
				vpif_process_buffer_complete(common);

			channel_first_int[i][channel_id] = 0;

			vpif_schedule_next_buffer(common);


			channel_first_int[i][channel_id] = 0;
		} else {
			/**
			 * Interlaced mode. If it is first interrupt, ignore
			 * it
			 */
			if (channel_first_int[i][channel_id]) {
				channel_first_int[i][channel_id] = 0;
				continue;
			}
			if (0 == i) {
				ch->field_id ^= 1;
				/* Get field id from VPIF registers */
				fid = vpif_channel_getfid(ch->channel_id);
				if (fid != ch->field_id) {
					/**
					 * If field id does not match stored
					 * field id, make them in sync
					 */
					if (0 == fid)
						ch->field_id = fid;
					return IRQ_HANDLED;
				}
			}
			/* device field id and local field id are in sync */
			if (0 == fid) {
				/* this is even field */
				if (common->cur_frm == common->next_frm)
					continue;

				/* mark the current buffer as done */
				vpif_process_buffer_complete(common);
			} else if (1 == fid) {
				/* odd field */
				if (list_empty(&common->dma_queue) ||
				    (common->cur_frm != common->next_frm))
					continue;

				vpif_schedule_next_buffer(common);
			}
		}
	}
	return IRQ_HANDLED;
}

/**
 * vpif_update_std_info() - update standard related info
 * @ch: ptr to channel object
 *
 * For a given standard selected by application, update values
 * in the device data structures
 */
static int vpif_update_std_info(struct channel_obj *ch)
{
	struct common_obj *common = &ch->common[VPIF_VIDEO_INDEX];
	struct vpif_params *vpifparams = &ch->vpifparams;
	const struct vpif_channel_config_params *config;
	struct vpif_channel_config_params *std_info;
	struct video_obj *vid_ch = &ch->video;
	int index;

	vpif_dbg(2, debug, "vpif_update_std_info\n");

	std_info = &vpifparams->std_info;

	for (index = 0; index < ARRAY_SIZE(ch_params); index++) {
		config = &ch_params[index];
		if (config->stdid & vid_ch->stdid) {
			memcpy(std_info, config, sizeof(*config));
			break;
		}
	}

	/* standard not found */
	if (index == ARRAY_SIZE(ch_params))
		return -EINVAL;

	common->fmt.fmt.pix.width = std_info->width;
	common->width = std_info->width;
	common->fmt.fmt.pix.height = std_info->height;
	common->height = std_info->height;
	common->fmt.fmt.pix.bytesperline = std_info->width;
	vpifparams->video_params.hpitch = std_info->width;
	vpifparams->video_params.storage_mode = std_info->frm_fmt;
	return 0;
}

/**
 * vpif_calculate_offsets : This function calculates buffers offsets
 * @ch : ptr to channel object
 *
 * This function calculates buffer offsets for Y and C in the top and
 * bottom field
 */
static void vpif_calculate_offsets(struct channel_obj *ch)
{
	unsigned int hpitch, vpitch, sizeimage;
	struct video_obj *vid_ch = &(ch->video);
	struct vpif_params *vpifparams = &ch->vpifparams;
	struct common_obj *common = &ch->common[VPIF_VIDEO_INDEX];
	enum v4l2_field field = common->fmt.fmt.pix.field;

	vpif_dbg(2, debug, "vpif_calculate_offsets\n");

	if (V4L2_FIELD_ANY == field) {
		if (vpifparams->std_info.frm_fmt)
			vid_ch->buf_field = V4L2_FIELD_NONE;
		else
			vid_ch->buf_field = V4L2_FIELD_INTERLACED;
	} else
		vid_ch->buf_field = common->fmt.fmt.pix.field;

	if (V4L2_MEMORY_USERPTR == common->memory)
		sizeimage = common->fmt.fmt.pix.sizeimage;
	else
		sizeimage = config_params.channel_bufsize[ch->channel_id];

	hpitch = common->fmt.fmt.pix.bytesperline;
	vpitch = sizeimage / (hpitch * 2);

	if ((V4L2_FIELD_NONE == vid_ch->buf_field) ||
	    (V4L2_FIELD_INTERLACED == vid_ch->buf_field)) {
		/* Calculate offsets for Y top, Y Bottom, C top and C Bottom */
		common->ytop_off = 0;
		common->ybtm_off = hpitch;
		common->ctop_off = sizeimage / 2;
		common->cbtm_off = sizeimage / 2 + hpitch;
	} else if (V4L2_FIELD_SEQ_TB == vid_ch->buf_field) {
		/* Calculate offsets for Y top, Y Bottom, C top and C Bottom */
		common->ytop_off = 0;
		common->ybtm_off = sizeimage / 4;
		common->ctop_off = sizeimage / 2;
		common->cbtm_off = common->ctop_off + sizeimage / 4;
	} else if (V4L2_FIELD_SEQ_BT == vid_ch->buf_field) {
		/* Calculate offsets for Y top, Y Bottom, C top and C Bottom */
		common->ybtm_off = 0;
		common->ytop_off = sizeimage / 4;
		common->cbtm_off = sizeimage / 2;
		common->ctop_off = common->cbtm_off + sizeimage / 4;
	}
	if ((V4L2_FIELD_NONE == vid_ch->buf_field) ||
	    (V4L2_FIELD_INTERLACED == vid_ch->buf_field))
		vpifparams->video_params.storage_mode = 1;
	else
		vpifparams->video_params.storage_mode = 0;

	if (1 == vpifparams->std_info.frm_fmt)
		vpifparams->video_params.hpitch =
		    common->fmt.fmt.pix.bytesperline;
	else {
		if ((field == V4L2_FIELD_ANY)
		    || (field == V4L2_FIELD_INTERLACED))
			vpifparams->video_params.hpitch =
			    common->fmt.fmt.pix.bytesperline * 2;
		else
			vpifparams->video_params.hpitch =
			    common->fmt.fmt.pix.bytesperline;
	}

	ch->vpifparams.video_params.stdid = vpifparams->std_info.stdid;
}

/**
 * vpif_config_format: configure default frame format in the device
 * ch : ptr to channel object
 */
static void vpif_config_format(struct channel_obj *ch)
{
	struct common_obj *common = &ch->common[VPIF_VIDEO_INDEX];

	vpif_dbg(2, debug, "vpif_config_format\n");

	common->fmt.fmt.pix.field = V4L2_FIELD_ANY;
	if (config_params.numbuffers[ch->channel_id] == 0)
		common->memory = V4L2_MEMORY_USERPTR;
	else
		common->memory = V4L2_MEMORY_MMAP;

	common->fmt.fmt.pix.sizeimage
	    = config_params.channel_bufsize[ch->channel_id];

	if (ch->vpifparams.iface.if_type == VPIF_IF_RAW_BAYER)
		common->fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_SBGGR8;
	else
		common->fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUV422P;
	common->fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
}

/**
 * vpif_get_default_field() - Get default field type based on interface
 * @vpif_params - ptr to vpif params
 */
static inline enum v4l2_field vpif_get_default_field(
				struct vpif_interface *iface)
{
	return (iface->if_type == VPIF_IF_RAW_BAYER) ? V4L2_FIELD_NONE :
						V4L2_FIELD_INTERLACED;
}

/**
 * vpif_check_format()  - check given pixel format for compatibility
 * @ch - channel  ptr
 * @pixfmt - Given pixel format
 * @update - update the values as per hardware requirement
 *
 * Check the application pixel format for S_FMT and update the input
 * values as per hardware limits for TRY_FMT. The default pixel and
 * field format is selected based on interface type.
 */
static int vpif_check_format(struct channel_obj *ch,
			     struct v4l2_pix_format *pixfmt,
			     int update)
{
	struct common_obj *common = &(ch->common[VPIF_VIDEO_INDEX]);
	struct vpif_params *vpif_params = &ch->vpifparams;
	enum v4l2_field field = pixfmt->field;
	u32 sizeimage, hpitch, vpitch;
	int ret = -EINVAL;

	vpif_dbg(2, debug, "vpif_check_format\n");
	/**
	 * first check for the pixel format. If if_type is Raw bayer,
	 * only V4L2_PIX_FMT_SBGGR8 format is supported. Otherwise only
	 * V4L2_PIX_FMT_YUV422P is supported
	 */
	if (vpif_params->iface.if_type == VPIF_IF_RAW_BAYER) {
		if (pixfmt->pixelformat != V4L2_PIX_FMT_SBGGR8) {
			if (!update) {
				vpif_dbg(2, debug, "invalid pix format\n");
				goto exit;
			}
			pixfmt->pixelformat = V4L2_PIX_FMT_SBGGR8;
		}
	} else {
		if (pixfmt->pixelformat != V4L2_PIX_FMT_YUV422P) {
			if (!update) {
				vpif_dbg(2, debug, "invalid pixel format\n");
				goto exit;
			}
			pixfmt->pixelformat = V4L2_PIX_FMT_YUV422P;
		}
	}

	if (!(VPIF_VALID_FIELD(field))) {
		if (!update) {
			vpif_dbg(2, debug, "invalid field format\n");
			goto exit;
		}
		/**
		 * By default use FIELD_NONE for RAW Bayer capture
		 * and FIELD_INTERLACED for other interfaces
		 */
		field = vpif_get_default_field(&vpif_params->iface);
	} else if (field == V4L2_FIELD_ANY)
		/* unsupported field. Use default */
		field = vpif_get_default_field(&vpif_params->iface);

	/* validate the hpitch */
	hpitch = pixfmt->bytesperline;
	if (hpitch < vpif_params->std_info.width) {
		if (!update) {
			vpif_dbg(2, debug, "invalid hpitch\n");
			goto exit;
		}
		hpitch = vpif_params->std_info.width;
	}

	if (V4L2_MEMORY_USERPTR == common->memory)
		sizeimage = pixfmt->sizeimage;
	else
		sizeimage = config_params.channel_bufsize[ch->channel_id];

	vpitch = sizeimage / (hpitch * 2);

	/* validate the vpitch */
	if (vpitch < vpif_params->std_info.height) {
		if (!update) {
			vpif_dbg(2, debug, "Invalid vpitch\n");
			goto exit;
		}
		vpitch = vpif_params->std_info.height;
	}

	/* Check for 8 byte alignment */
	if (!ALIGN(hpitch, 8)) {
		if (!update) {
			vpif_dbg(2, debug, "invalid pitch alignment\n");
			goto exit;
		}
		/* adjust to next 8 byte boundary */
		hpitch = (((hpitch + 7) / 8) * 8);
	}
	/* if update is set, modify the bytesperline and sizeimage */
	if (update) {
		pixfmt->bytesperline = hpitch;
		pixfmt->sizeimage = hpitch * vpitch * 2;
	}
	/**
	 * Image width and height is always based on current standard width and
	 * height
	 */
	pixfmt->width = common->fmt.fmt.pix.width;
	pixfmt->height = common->fmt.fmt.pix.height;
	return 0;
exit:
	return ret;
}

/**
 * vpif_config_addr() - function to configure buffer address in vpif
 * @ch - channel ptr
 * @muxmode - channel mux mode
 */
static void vpif_config_addr(struct channel_obj *ch, int muxmode)
{
	struct common_obj *common;

	vpif_dbg(2, debug, "vpif_config_addr\n");

	common = &(ch->common[VPIF_VIDEO_INDEX]);

	if (VPIF_CHANNEL1_VIDEO == ch->channel_id)
		common->set_addr = ch1_set_videobuf_addr;
	else if (2 == muxmode)
		common->set_addr = ch0_set_videobuf_addr_yc_nmux;
	else
		common->set_addr = ch0_set_videobuf_addr;
}

/**
 * vpfe_mmap : It is used to map kernel space buffers into user spaces
 * @filep: file pointer
 * @vma: ptr to vm_area_struct
 */
static int vpif_mmap(struct file *filep, struct vm_area_struct *vma)
{
	/* Get the channel object and file handle object */
	struct vpif_fh *fh = filep->private_data;
	struct channel_obj *ch = fh->channel;
	struct common_obj *common = &(ch->common[VPIF_VIDEO_INDEX]);

	vpif_dbg(2, debug, "vpif_mmap\n");

	return videobuf_mmap_mapper(&common->buffer_queue, vma);
}

/**
 * vpif_poll: It is used for select/poll system call
 * @filep: file pointer
 * @wait: poll table to wait
 */
static unsigned int vpif_poll(struct file *filep, poll_table * wait)
{
	int err = 0;
	struct vpif_fh *fh = filep->private_data;
	struct channel_obj *channel = fh->channel;
	struct common_obj *common = &(channel->common[VPIF_VIDEO_INDEX]);

	vpif_dbg(2, debug, "vpif_poll\n");

	if (common->started)
		err = videobuf_poll_stream(filep, &common->buffer_queue, wait);

	return 0;
}

/**
 * vpif_open : vpif open handler
 * @filep: file ptr
 *
 * It creates object of file handle structure and stores it in private_data
 * member of filepointer
 */
static int vpif_open(struct file *filep)
{
	struct vpif_capture_config *config = vpif_dev->platform_data;
	struct video_device *vdev = video_devdata(filep);
	struct common_obj *common;
	struct video_obj *vid_ch;
	struct channel_obj *ch;
	struct vpif_fh *fh;
	int i, ret = 0;

	vpif_dbg(2, debug, "vpif_open\n");

	ch = video_get_drvdata(vdev);

	vid_ch = &ch->video;
	common = &ch->common[VPIF_VIDEO_INDEX];

	if (mutex_lock_interruptible(&common->lock))
		return -ERESTARTSYS;

	if (NULL == ch->curr_subdev_info) {
		/**
		 * search through the sub device to see a registered
		 * sub device and make it as current sub device
		 */
		for (i = 0; i < config->subdev_count; i++) {
			if (vpif_obj.sd[i]) {
				/* the sub device is registered */
				ch->curr_subdev_info = &config->subdev_info[i];
				/* make first input as the current input */
				vid_ch->input_idx = 0;
				break;
			}
		}
		if (i == config->subdev_count) {
			vpif_err("No sub device registered\n");
			ret = -ENOENT;
			goto exit;
		}
	}

	/* Allocate memory for the file handle object */
	fh = kmalloc(sizeof(struct vpif_fh), GFP_KERNEL);
	if (NULL == fh) {
		vpif_err("unable to allocate memory for file handle object\n");
		ret = -ENOMEM;
		goto exit;
	}

	/* store pointer to fh in private_data member of filep */
	filep->private_data = fh;
	fh->channel = ch;
	fh->initialized = 0;
	/* If decoder is not initialized. initialize it */
	if (!ch->initialized) {
		fh->initialized = 1;
		ch->initialized = 1;
		memset(&(ch->vpifparams), 0, sizeof(struct vpif_params));
	}
	/* Increment channel usrs counter */
	ch->usrs++;
	/* Set io_allowed member to false */
	fh->io_allowed[VPIF_VIDEO_INDEX] = 0;
	/* Initialize priority of this instance to default priority */
	fh->prio = V4L2_PRIORITY_UNSET;
	v4l2_prio_open(&ch->prio, &fh->prio);
exit:
	mutex_unlock(&common->lock);
	return ret;
}

/**
 * vpif_release : function to clean up file close
 * @filep: file pointer
 *
 * This function deletes buffer queue, frees the buffers and the vpfe file
 * handle
 */
static int vpif_release(struct file *filep)
{
	struct vpif_fh *fh = filep->private_data;
	struct channel_obj *ch = fh->channel;
	struct common_obj *common;

	vpif_dbg(2, debug, "vpif_release\n");

	common = &ch->common[VPIF_VIDEO_INDEX];

	if (mutex_lock_interruptible(&common->lock))
		return -ERESTARTSYS;

	/* if this instance is doing IO */
	if (fh->io_allowed[VPIF_VIDEO_INDEX]) {
		/* Reset io_usrs member of channel object */
		common->io_usrs = 0;
		/* Disable channel as per its device type and channel id */
		if (VPIF_CHANNEL0_VIDEO == ch->channel_id) {
			enable_channel0(0);
			channel0_intr_enable(0);
		}
		if ((VPIF_CHANNEL1_VIDEO == ch->channel_id) ||
		    (2 == common->started)) {
			enable_channel1(0);
			channel1_intr_enable(0);
		}
		common->started = 0;
		/* Free buffers allocated */
		videobuf_queue_cancel(&common->buffer_queue);
		videobuf_mmap_free(&common->buffer_queue);
	}

	/* Decrement channel usrs counter */
	ch->usrs--;

	/* unlock mutex on channel object */
	mutex_unlock(&common->lock);

	/* Close the priority */
	v4l2_prio_close(&ch->prio, &fh->prio);

	if (fh->initialized)
		ch->initialized = 0;

	filep->private_data = NULL;
	kfree(fh);
	return 0;
}

/**
 * vpif_reqbufs() - request buffer handler
 * @file: file ptr
 * @priv: file handle
 * @reqbuf: request buffer structure ptr
 */
static int vpif_reqbufs(struct file *file, void *priv,
			struct v4l2_requestbuffers *reqbuf)
{
	struct vpif_fh *fh = priv;
	struct channel_obj *ch = fh->channel;
	struct common_obj *common;
	u8 index = 0;
	int ret = 0;

	vpif_dbg(2, debug, "vpif_reqbufs\n");

	/**
	 * This file handle has not initialized the channel,
	 * It is not allowed to do settings
	 */
	if ((VPIF_CHANNEL0_VIDEO == ch->channel_id)
	    || (VPIF_CHANNEL1_VIDEO == ch->channel_id)) {
		if (!fh->initialized) {
			vpif_dbg(1, debug, "Channel Busy\n");
			return -EBUSY;
		}
	}

	if (V4L2_BUF_TYPE_VIDEO_CAPTURE != reqbuf->type)
		return -EINVAL;

	index = VPIF_VIDEO_INDEX;

	common = &ch->common[index];

	if (mutex_lock_interruptible(&common->lock))
		return -ERESTARTSYS;

	if (0 != common->io_usrs) {
		ret = -EBUSY;
		goto reqbuf_exit;
	}

	/* Initialize videobuf queue as per the buffer type */
	videobuf_queue_dma_contig_init(&common->buffer_queue,
					    &video_qops, NULL,
					    &common->irqlock,
					    reqbuf->type,
					    common->fmt.fmt.pix.field,
					    sizeof(struct videobuf_buffer), fh);

	/* Set io allowed member of file handle to TRUE */
	fh->io_allowed[index] = 1;
	/* Increment io usrs member of channel object to 1 */
	common->io_usrs = 1;
	/* Store type of memory requested in channel object */
	common->memory = reqbuf->memory;
	INIT_LIST_HEAD(&common->dma_queue);

	/* Allocate buffers */
	ret = videobuf_reqbufs(&common->buffer_queue, reqbuf);

reqbuf_exit:
	mutex_unlock(&common->lock);
	return ret;
}

/**
 * vpif_querybuf() - query buffer handler
 * @file: file ptr
 * @priv: file handle
 * @buf: v4l2 buffer structure ptr
 */
static int vpif_querybuf(struct file *file, void *priv,
				struct v4l2_buffer *buf)
{
	struct vpif_fh *fh = priv;
	struct channel_obj *ch = fh->channel;
	struct common_obj *common = &ch->common[VPIF_VIDEO_INDEX];

	vpif_dbg(2, debug, "vpif_querybuf\n");

	if (common->fmt.type != buf->type)
		return -EINVAL;

	if (common->memory != V4L2_MEMORY_MMAP) {
		vpif_dbg(1, debug, "Invalid memory\n");
		return -EINVAL;
	}

	return videobuf_querybuf(&common->buffer_queue, buf);
}

/**
 * vpif_qbuf() - query buffer handler
 * @file: file ptr
 * @priv: file handle
 * @buf: v4l2 buffer structure ptr
 */
static int vpif_qbuf(struct file *file, void *priv, struct v4l2_buffer *buf)
{

	struct vpif_fh *fh = priv;
	struct channel_obj *ch = fh->channel;
	struct common_obj *common = &ch->common[VPIF_VIDEO_INDEX];
	struct v4l2_buffer tbuf = *buf;
	struct videobuf_buffer *buf1;
	unsigned long addr = 0;
	unsigned long flags;
	int ret = 0;

	vpif_dbg(2, debug, "vpif_qbuf\n");

	if (common->fmt.type != tbuf.type) {
		vpif_err("invalid buffer type\n");
		return -EINVAL;
	}

	if (!fh->io_allowed[VPIF_VIDEO_INDEX]) {
		vpif_err("fh io not allowed \n");
		return -EACCES;
	}

	if (!(list_empty(&common->dma_queue)) ||
	    (common->cur_frm != common->next_frm) ||
	    !common->started ||
	    (common->started && (0 == ch->field_id)))
		return videobuf_qbuf(&common->buffer_queue, buf);

	/* bufferqueue is empty store buffer address in VPIF registers */
	mutex_lock(&common->buffer_queue.vb_lock);
	buf1 = common->buffer_queue.bufs[tbuf.index];

	if ((buf1->state == VIDEOBUF_QUEUED) ||
	    (buf1->state == VIDEOBUF_ACTIVE)) {
		vpif_err("invalid state\n");
		goto qbuf_exit;
	}

	switch (buf1->memory) {
	case V4L2_MEMORY_MMAP:
		if (buf1->baddr == 0)
			goto qbuf_exit;
		break;

	case V4L2_MEMORY_USERPTR:
		if (tbuf.length < buf1->bsize)
			goto qbuf_exit;

		if ((VIDEOBUF_NEEDS_INIT != buf1->state)
			    && (buf1->baddr != tbuf.m.userptr))
			vpif_buffer_release(&common->buffer_queue, buf1);
			buf1->baddr = tbuf.m.userptr;
		break;

	default:
		goto qbuf_exit;
	}

	local_irq_save(flags);
	ret = vpif_buffer_prepare(&common->buffer_queue, buf1,
					common->buffer_queue.field);
	if (ret < 0) {
		local_irq_restore(flags);
		goto qbuf_exit;
	}

	buf1->state = VIDEOBUF_ACTIVE;

	if (V4L2_MEMORY_USERPTR == common->memory)
		addr = buf1->boff;
	else
		addr = videobuf_to_dma_contig(buf1);

	common->next_frm = buf1;
	common->set_addr(addr + common->ytop_off,
			 addr + common->ybtm_off,
			 addr + common->ctop_off,
			 addr + common->cbtm_off);

	local_irq_restore(flags);
	list_add_tail(&buf1->stream, &common->buffer_queue.stream);
	mutex_unlock(&common->buffer_queue.vb_lock);
	return 0;

qbuf_exit:
	mutex_unlock(&common->buffer_queue.vb_lock);
	return -EINVAL;
}

/**
 * vpif_dqbuf() - query buffer handler
 * @file: file ptr
 * @priv: file handle
 * @buf: v4l2 buffer structure ptr
 */
static int vpif_dqbuf(struct file *file, void *priv, struct v4l2_buffer *buf)
{
	struct vpif_fh *fh = priv;
	struct channel_obj *ch = fh->channel;
	struct common_obj *common = &ch->common[VPIF_VIDEO_INDEX];

	vpif_dbg(2, debug, "vpif_dqbuf\n");

	return videobuf_dqbuf(&common->buffer_queue, buf,
					file->f_flags & O_NONBLOCK);
}

/**
 * vpif_streamon() - streamon handler
 * @file: file ptr
 * @priv: file handle
 * @buftype: v4l2 buffer type
 */
static int vpif_streamon(struct file *file, void *priv,
				enum v4l2_buf_type buftype)
{

	struct vpif_capture_config *config = vpif_dev->platform_data;
	struct vpif_fh *fh = priv;
	struct channel_obj *ch = fh->channel;
	struct common_obj *common = &ch->common[VPIF_VIDEO_INDEX];
	struct channel_obj *oth_ch = vpif_obj.dev[!ch->channel_id];
	struct vpif_params *vpif;
	unsigned long addr = 0;
	int ret = 0;

	vpif_dbg(2, debug, "vpif_streamon\n");

	vpif = &ch->vpifparams;

	if (buftype != V4L2_BUF_TYPE_VIDEO_CAPTURE) {
		vpif_dbg(1, debug, "buffer type not supported\n");
		return -EINVAL;
	}

	/* If file handle is not allowed IO, return error */
	if (!fh->io_allowed[VPIF_VIDEO_INDEX]) {
		vpif_dbg(1, debug, "io not allowed\n");
		return -EACCES;
	}

	/* If Streaming is already started, return error */
	if (common->started) {
		vpif_dbg(1, debug, "channel->started\n");
		return -EBUSY;
	}

	if ((ch->channel_id == VPIF_CHANNEL0_VIDEO &&
	    oth_ch->common[VPIF_VIDEO_INDEX].started &&
	    vpif->std_info.ycmux_mode == 0) ||
	   ((ch->channel_id == VPIF_CHANNEL1_VIDEO) &&
	    (2 == oth_ch->common[VPIF_VIDEO_INDEX].started))) {
		vpif_dbg(1, debug, "other channel is being used\n");
		return -EBUSY;
	}

	ret = vpif_check_format(ch, &common->fmt.fmt.pix, 0);
	if (ret)
		return ret;

	/* Enable streamon on the sub device */
	ret = v4l2_subdev_call(vpif_obj.sd[ch->curr_sd_index], video,
				s_stream, 1);

	if (ret && (ret != -ENOIOCTLCMD)) {
		vpif_dbg(1, debug, "stream on failed in subdev\n");
		return ret;
	}

	/* Call videobuf_streamon to start streaming in videobuf */
	ret = videobuf_streamon(&common->buffer_queue);
	if (ret) {
		vpif_dbg(1, debug, "videobuf_streamon\n");
		return ret;
	}

	if (mutex_lock_interruptible(&common->lock)) {
		ret = -ERESTARTSYS;
		goto streamoff_exit;
	}

	/* If buffer queue is empty, return error */
	if (list_empty(&common->dma_queue)) {
		vpif_dbg(1, debug, "buffer queue is empty\n");
		ret = -EIO;
		goto exit;
	}

	/* Get the next frame from the buffer queue */
	common->cur_frm = list_entry(common->dma_queue.next,
				    struct videobuf_buffer, queue);
	common->next_frm = common->cur_frm;

	/* Remove buffer from the buffer queue */
	list_del(&common->cur_frm->queue);
	/* Mark state of the current frame to active */
	common->cur_frm->state = VIDEOBUF_ACTIVE;
	/* Initialize field_id and started member */
	ch->field_id = 0;
	common->started = 1;

	if (V4L2_MEMORY_USERPTR == common->memory)
		addr = common->cur_frm->boff;
	else
		addr = videobuf_to_dma_contig(common->cur_frm);

	/* Calculate the offset for Y and C data in the buffer */
	vpif_calculate_offsets(ch);

	if ((vpif->std_info.frm_fmt &&
	    ((common->fmt.fmt.pix.field != V4L2_FIELD_NONE) &&
	     (common->fmt.fmt.pix.field != V4L2_FIELD_ANY))) ||
	    (!vpif->std_info.frm_fmt &&
	     (common->fmt.fmt.pix.field == V4L2_FIELD_NONE))) {
		vpif_dbg(1, debug, "conflict in field format and std format\n");
		ret = -EINVAL;
		goto exit;
	}

	/* configure 1 or 2 channel mode */
	ret = config->setup_input_channel_mode(vpif->std_info.ycmux_mode);

	if (ret < 0) {
		vpif_dbg(1, debug, "can't set vpif channel mode\n");
		goto exit;
	}

	/* Call vpif_set_params function to set the parameters and addresses */
	ret = vpif_set_video_params(vpif, ch->channel_id);

	if (ret < 0) {
		vpif_dbg(1, debug, "can't set video params\n");
		goto exit;
	}

	common->started = ret;
	vpif_config_addr(ch, ret);

	common->set_addr(addr + common->ytop_off,
			 addr + common->ybtm_off,
			 addr + common->ctop_off,
			 addr + common->cbtm_off);

	/**
	 * Set interrupt for both the fields in VPIF Register enable channel in
	 * VPIF register
	 */
	if ((VPIF_CHANNEL0_VIDEO == ch->channel_id)) {
		channel0_intr_assert();
		channel0_intr_enable(1);
		enable_channel0(1);
	}
	if ((VPIF_CHANNEL1_VIDEO == ch->channel_id) ||
	    (common->started == 2)) {
		channel1_intr_assert();
		channel1_intr_enable(1);
		enable_channel1(1);
	}
	channel_first_int[VPIF_VIDEO_INDEX][ch->channel_id] = 1;
	mutex_unlock(&common->lock);
	return ret;

exit:
	mutex_unlock(&common->lock);
streamoff_exit:
	ret = videobuf_streamoff(&common->buffer_queue);
	return ret;
}

/**
 * vpif_streamoff() - streamoff handler
 * @file: file ptr
 * @priv: file handle
 * @buftype: v4l2 buffer type
 */
static int vpif_streamoff(struct file *file, void *priv,
				enum v4l2_buf_type buftype)
{

	struct vpif_fh *fh = priv;
	struct channel_obj *ch = fh->channel;
	struct common_obj *common = &ch->common[VPIF_VIDEO_INDEX];
	int ret;

	vpif_dbg(2, debug, "vpif_streamoff\n");

	if (buftype != V4L2_BUF_TYPE_VIDEO_CAPTURE) {
		vpif_dbg(1, debug, "buffer type not supported\n");
		return -EINVAL;
	}

	/* If io is allowed for this file handle, return error */
	if (!fh->io_allowed[VPIF_VIDEO_INDEX]) {
		vpif_dbg(1, debug, "io not allowed\n");
		return -EACCES;
	}

	/* If streaming is not started, return error */
	if (!common->started) {
		vpif_dbg(1, debug, "channel->started\n");
		return -EINVAL;
	}

	if (mutex_lock_interruptible(&common->lock))
		return -ERESTARTSYS;

	/* disable channel */
	if (VPIF_CHANNEL0_VIDEO == ch->channel_id) {
		enable_channel0(0);
		channel0_intr_enable(0);
	} else {
		enable_channel1(0);
		channel1_intr_enable(0);
	}

	common->started = 0;

	ret = v4l2_subdev_call(vpif_obj.sd[ch->curr_sd_index], video,
				s_stream, 0);

	if (ret && (ret != -ENOIOCTLCMD))
		vpif_dbg(1, debug, "stream off failed in subdev\n");

	mutex_unlock(&common->lock);

	return videobuf_streamoff(&common->buffer_queue);
}

/**
 * vpif_map_sub_device_to_input() - Maps sub device to input
 * @ch - ptr to channel
 * @config - ptr to capture configuration
 * @input_index - Given input index from application
 * @sub_device_index - index into sd table
 *
 * lookup the sub device information for a given input index.
 * we report all the inputs to application. inputs table also
 * has sub device name for the each input
 */
static struct vpif_subdev_info *vpif_map_sub_device_to_input(
				struct channel_obj *ch,
				struct vpif_capture_config *vpif_cfg,
				int input_index,
				int *sub_device_index)
{
	struct vpif_capture_chan_config *chan_cfg;
	struct vpif_subdev_info *subdev_info = NULL;
	const char *subdev_name = NULL;
	int i;

	vpif_dbg(2, debug, "vpif_map_sub_device_to_input\n");

	chan_cfg = &vpif_cfg->chan_config[ch->channel_id];

	/**
	 * search through the inputs to find the sub device supporting
	 * the input
	 */
	for (i = 0; i < chan_cfg->input_count; i++) {
		/* For each sub device, loop through input */
		if (i == input_index) {
			subdev_name = chan_cfg->inputs[i].subdev_name;
			break;
		}
	}

	/* if reached maximum. return null */
	if (i == chan_cfg->input_count || (NULL == subdev_name))
		return subdev_info;

	/* loop through the sub device list to get the sub device info */
	for (i = 0; i < vpif_cfg->subdev_count; i++) {
		subdev_info = &vpif_cfg->subdev_info[i];
		if (!strcmp(subdev_info->name, subdev_name))
			break;
	}

	if (i == vpif_cfg->subdev_count)
		return subdev_info;

	/* check if the sub device is registered */
	if (NULL == vpif_obj.sd[i])
		return NULL;

	*sub_device_index = i;
	return subdev_info;
}

/**
 * vpif_querystd() - querystd handler
 * @file: file ptr
 * @priv: file handle
 * @std_id: ptr to std id
 *
 * This function is called to detect standard at the selected input
 */
static int vpif_querystd(struct file *file, void *priv, v4l2_std_id *std_id)
{
	struct vpif_fh *fh = priv;
	struct channel_obj *ch = fh->channel;
	struct common_obj *common = &ch->common[VPIF_VIDEO_INDEX];
	int ret = 0;

	vpif_dbg(2, debug, "vpif_querystd\n");

	if (mutex_lock_interruptible(&common->lock))
		return -ERESTARTSYS;

	/* Call querystd function of decoder device */
	ret = v4l2_subdev_call(vpif_obj.sd[ch->curr_sd_index], video,
				querystd, std_id);
	if (ret < 0)
		vpif_dbg(1, debug, "Failed to set standard for sub devices\n");

	mutex_unlock(&common->lock);
	return ret;
}

/**
 * vpif_g_std() - get STD handler
 * @file: file ptr
 * @priv: file handle
 * @std_id: ptr to std id
 */
static int vpif_g_std(struct file *file, void *priv, v4l2_std_id *std)
{
	struct vpif_fh *fh = priv;
	struct channel_obj *ch = fh->channel;

	vpif_dbg(2, debug, "vpif_g_std\n");

	*std = ch->video.stdid;
	return 0;
}

/**
 * vpif_s_std() - set STD handler
 * @file: file ptr
 * @priv: file handle
 * @std_id: ptr to std id
 */
static int vpif_s_std(struct file *file, void *priv, v4l2_std_id *std_id)
{
	struct vpif_fh *fh = priv;
	struct channel_obj *ch = fh->channel;
	struct common_obj *common = &ch->common[VPIF_VIDEO_INDEX];
	int ret = 0;

	vpif_dbg(2, debug, "vpif_s_std\n");

	if (common->started) {
		vpif_err("streaming in progress\n");
		return -EBUSY;
	}

	if ((VPIF_CHANNEL0_VIDEO == ch->channel_id) ||
	    (VPIF_CHANNEL1_VIDEO == ch->channel_id)) {
		if (!fh->initialized) {
			vpif_dbg(1, debug, "Channel Busy\n");
			return -EBUSY;
		}
	}

	ret = v4l2_prio_check(&ch->prio, &fh->prio);
	if (0 != ret)
		return ret;

	fh->initialized = 1;

	/* Call encoder subdevice function to set the standard */
	if (mutex_lock_interruptible(&common->lock))
		return -ERESTARTSYS;

	ch->video.stdid = *std_id;

	/* Get the information about the standard */
	if (vpif_update_std_info(ch)) {
		ret = -EINVAL;
		vpif_err("Error getting the standard info\n");
		goto s_std_exit;
	}

	/* Configure the default format information */
	vpif_config_format(ch);

	/* set standard in the sub device */
	ret = v4l2_subdev_call(vpif_obj.sd[ch->curr_sd_index], core,
				s_std, *std_id);
	if (ret < 0)
		vpif_dbg(1, debug, "Failed to set standard for sub devices\n");

s_std_exit:
	mutex_unlock(&common->lock);
	return ret;
}

/**
 * vpif_enum_input() - ENUMINPUT handler
 * @file: file ptr
 * @priv: file handle
 * @input: ptr to input structure
 */
static int vpif_enum_input(struct file *file, void *priv,
				struct v4l2_input *input)
{

	struct vpif_capture_config *config = vpif_dev->platform_data;
	struct vpif_capture_chan_config *chan_cfg;
	struct vpif_fh *fh = priv;
	struct channel_obj *ch = fh->channel;

	chan_cfg = &config->chan_config[ch->channel_id];

	if (input->index >= chan_cfg->input_count) {
		vpif_dbg(1, debug, "Invalid input index\n");
		return -EINVAL;
	}

	memcpy(input, &chan_cfg->inputs[input->index].input,
		sizeof(*input));
	return 0;
}

/**
 * vpif_g_input() - Get INPUT handler
 * @file: file ptr
 * @priv: file handle
 * @index: ptr to input index
 */
static int vpif_g_input(struct file *file, void *priv, unsigned int *index)
{
	struct vpif_fh *fh = priv;
	struct channel_obj *ch = fh->channel;
	struct video_obj *vid_ch = &ch->video;

	*index = vid_ch->input_idx;

	return 0;
}

/**
 * vpif_s_input() - Set INPUT handler
 * @file: file ptr
 * @priv: file handle
 * @index: input index
 */
static int vpif_s_input(struct file *file, void *priv, unsigned int index)
{
	struct vpif_capture_config *config = vpif_dev->platform_data;
	struct vpif_capture_chan_config *chan_cfg;
	struct vpif_fh *fh = priv;
	struct channel_obj *ch = fh->channel;
	struct common_obj *common = &ch->common[VPIF_VIDEO_INDEX];
	struct video_obj *vid_ch = &ch->video;
	struct vpif_subdev_info *subdev_info;
	int ret = 0, sd_index = 0;
	u32 input = 0, output = 0;

	chan_cfg = &config->chan_config[ch->channel_id];

	if (common->started) {
		vpif_err("Streaming in progress\n");
		return -EBUSY;
	}

	if ((VPIF_CHANNEL0_VIDEO == ch->channel_id) ||
	    (VPIF_CHANNEL1_VIDEO == ch->channel_id)) {
		if (!fh->initialized) {
			vpif_dbg(1, debug, "Channel Busy\n");
			return -EBUSY;
		}
	}

	ret = v4l2_prio_check(&ch->prio, &fh->prio);
	if (0 != ret)
		return ret;

	fh->initialized = 1;
	subdev_info = vpif_map_sub_device_to_input(ch, config, index,
						   &sd_index);
	if (NULL == subdev_info) {
		vpif_dbg(1, debug,
			"couldn't lookup sub device for the input index\n");
		return -EINVAL;
	}

	if (mutex_lock_interruptible(&common->lock))
		return -ERESTARTSYS;

	/* first setup input path from sub device to vpif */
	if (config->setup_input_path) {
		ret = config->setup_input_path(ch->channel_id,
					       subdev_info->name);
		if (ret < 0) {
			vpif_dbg(1, debug, "couldn't setup input path for the"
				" sub device %s, for input index %d\n",
				subdev_info->name, index);
			goto exit;
		}
	}

	if (subdev_info->can_route) {
		input = subdev_info->input;
		output = subdev_info->output;
		ret = v4l2_subdev_call(vpif_obj.sd[sd_index], video, s_routing,
					input, output, 0);
		if (ret < 0) {
			vpif_dbg(1, debug, "Failed to set input\n");
			goto exit;
		}
	}
	vid_ch->input_idx = index;
	ch->curr_subdev_info = subdev_info;
	ch->curr_sd_index = sd_index;
	/* copy interface parameters to vpif */
	ch->vpifparams.iface = subdev_info->vpif_if;

	/* update tvnorms from the sub device input info */
	ch->video_dev->tvnorms = chan_cfg->inputs[index].input.std;

exit:
	mutex_unlock(&common->lock);
	return ret;
}

/**
 * vpif_enum_fmt_vid_cap() - ENUM_FMT handler
 * @file: file ptr
 * @priv: file handle
 * @index: input index
 */
static int vpif_enum_fmt_vid_cap(struct file *file, void  *priv,
					struct v4l2_fmtdesc *fmt)
{
	struct vpif_fh *fh = priv;
	struct channel_obj *ch = fh->channel;

	if (fmt->index != 0) {
		vpif_dbg(1, debug, "Invalid format index\n");
		return -EINVAL;
	}

	/* Fill in the information about format */
	if (ch->vpifparams.iface.if_type == VPIF_IF_RAW_BAYER) {
		fmt->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		strcpy(fmt->description, "Raw Mode -Bayer Pattern GrRBGb");
		fmt->pixelformat = V4L2_PIX_FMT_SBGGR8;
	} else {
		fmt->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		strcpy(fmt->description, "YCbCr4:2:2 YC Planar");
		fmt->pixelformat = V4L2_PIX_FMT_YUV422P;
	}
	return 0;
}

/**
 * vpif_try_fmt_vid_cap() - TRY_FMT handler
 * @file: file ptr
 * @priv: file handle
 * @fmt: ptr to v4l2 format structure
 */
static int vpif_try_fmt_vid_cap(struct file *file, void *priv,
				struct v4l2_format *fmt)
{
	struct vpif_fh *fh = priv;
	struct channel_obj *ch = fh->channel;
	struct v4l2_pix_format *pixfmt = &fmt->fmt.pix;

	return vpif_check_format(ch, pixfmt, 1);
}


/**
 * vpif_g_fmt_vid_cap() - Set INPUT handler
 * @file: file ptr
 * @priv: file handle
 * @fmt: ptr to v4l2 format structure
 */
static int vpif_g_fmt_vid_cap(struct file *file, void *priv,
				struct v4l2_format *fmt)
{
	struct vpif_fh *fh = priv;
	struct channel_obj *ch = fh->channel;
	struct common_obj *common = &ch->common[VPIF_VIDEO_INDEX];

	/* Check the validity of the buffer type */
	if (common->fmt.type != fmt->type)
		return -EINVAL;

	/* Fill in the information about format */
	if (mutex_lock_interruptible(&common->lock))
		return -ERESTARTSYS;

	*fmt = common->fmt;
	mutex_unlock(&common->lock);
	return 0;
}

/**
 * vpif_s_fmt_vid_cap() - Set FMT handler
 * @file: file ptr
 * @priv: file handle
 * @fmt: ptr to v4l2 format structure
 */
static int vpif_s_fmt_vid_cap(struct file *file, void *priv,
				struct v4l2_format *fmt)
{
	struct vpif_fh *fh = priv;
	struct channel_obj *ch = fh->channel;
	struct common_obj *common = &ch->common[VPIF_VIDEO_INDEX];
	struct v4l2_pix_format *pixfmt;
	int ret = 0;

	vpif_dbg(2, debug, "VIDIOC_S_FMT\n");

	/* If streaming is started, return error */
	if (common->started) {
		vpif_dbg(1, debug, "Streaming is started\n");
		return -EBUSY;
	}

	if ((VPIF_CHANNEL0_VIDEO == ch->channel_id) ||
	    (VPIF_CHANNEL1_VIDEO == ch->channel_id)) {
		if (!fh->initialized) {
			vpif_dbg(1, debug, "Channel Busy\n");
			return -EBUSY;
		}
	}

	ret = v4l2_prio_check(&ch->prio, &fh->prio);
	if (0 != ret)
		return ret;

	fh->initialized = 1;

	pixfmt = &fmt->fmt.pix;
	/* Check for valid field format */
	ret = vpif_check_format(ch, pixfmt, 0);

	if (ret)
		return ret;
	/* store the format in the channel object */
	if (mutex_lock_interruptible(&common->lock))
		return -ERESTARTSYS;

	common->fmt = *fmt;
	mutex_unlock(&common->lock);

	return 0;
}

/**
 * vpif_querycap() - QUERYCAP handler
 * @file: file ptr
 * @priv: file handle
 * @cap: ptr to v4l2_capability structure
 */
static int vpif_querycap(struct file *file, void  *priv,
				struct v4l2_capability *cap)
{
	struct vpif_capture_config *config = vpif_dev->platform_data;

	cap->version = VPIF_CAPTURE_VERSION_CODE;
	cap->capabilities = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING;
	strlcpy(cap->driver, "vpif capture", sizeof(cap->driver));
	strlcpy(cap->bus_info, "DM646x Platform", sizeof(cap->bus_info));
	strlcpy(cap->card, config->card_name, sizeof(cap->card));

	return 0;
}

/**
 * vpif_g_priority() - get priority handler
 * @file: file ptr
 * @priv: file handle
 * @prio: ptr to v4l2_priority structure
 */
static int vpif_g_priority(struct file *file, void *priv,
			   enum v4l2_priority *prio)
{
	struct vpif_fh *fh = priv;
	struct channel_obj *ch = fh->channel;

	*prio = v4l2_prio_max(&ch->prio);

	return 0;
}

/**
 * vpif_s_priority() - set priority handler
 * @file: file ptr
 * @priv: file handle
 * @prio: ptr to v4l2_priority structure
 */
static int vpif_s_priority(struct file *file, void *priv, enum v4l2_priority p)
{
	struct vpif_fh *fh = priv;
	struct channel_obj *ch = fh->channel;

	return v4l2_prio_change(&ch->prio, &fh->prio, p);
}

/**
 * vpif_cropcap() - cropcap handler
 * @file: file ptr
 * @priv: file handle
 * @crop: ptr to v4l2_cropcap structure
 */
static int vpif_cropcap(struct file *file, void *priv,
			struct v4l2_cropcap *crop)
{
	struct vpif_fh *fh = priv;
	struct channel_obj *ch = fh->channel;
	struct common_obj *common = &ch->common[VPIF_VIDEO_INDEX];

	if (V4L2_BUF_TYPE_VIDEO_CAPTURE != crop->type)
		return -EINVAL;

	crop->bounds.left = 0;
	crop->bounds.top = 0;
	crop->bounds.height = common->height;
	crop->bounds.width = common->width;
	crop->defrect = crop->bounds;
	return 0;
}

/* vpif capture ioctl operations */
static const struct v4l2_ioctl_ops vpif_ioctl_ops = {
	.vidioc_querycap        	= vpif_querycap,
	.vidioc_g_priority		= vpif_g_priority,
	.vidioc_s_priority		= vpif_s_priority,
	.vidioc_enum_fmt_vid_cap	= vpif_enum_fmt_vid_cap,
	.vidioc_g_fmt_vid_cap  		= vpif_g_fmt_vid_cap,
	.vidioc_s_fmt_vid_cap		= vpif_s_fmt_vid_cap,
	.vidioc_try_fmt_vid_cap		= vpif_try_fmt_vid_cap,
	.vidioc_enum_input		= vpif_enum_input,
	.vidioc_s_input			= vpif_s_input,
	.vidioc_g_input			= vpif_g_input,
	.vidioc_reqbufs         	= vpif_reqbufs,
	.vidioc_querybuf        	= vpif_querybuf,
	.vidioc_querystd		= vpif_querystd,
	.vidioc_s_std           	= vpif_s_std,
	.vidioc_g_std			= vpif_g_std,
	.vidioc_qbuf            	= vpif_qbuf,
	.vidioc_dqbuf           	= vpif_dqbuf,
	.vidioc_streamon        	= vpif_streamon,
	.vidioc_streamoff       	= vpif_streamoff,
	.vidioc_cropcap         	= vpif_cropcap,
};

/* vpif file operations */
static struct v4l2_file_operations vpif_fops = {
	.owner = THIS_MODULE,
	.open = vpif_open,
	.release = vpif_release,
	.ioctl = video_ioctl2,
	.mmap = vpif_mmap,
	.poll = vpif_poll
};

/* vpif video template */
static struct video_device vpif_video_template = {
	.name		= "vpif",
	.fops		= &vpif_fops,
	.minor		= -1,
	.ioctl_ops	= &vpif_ioctl_ops,
};

/**
 * initialize_vpif() - Initialize vpif data structures
 *
 * Allocate memory for data structures and initialize them
 */
static int initialize_vpif(void)
{
	int err = 0, i, j;
	int free_channel_objects_index;

	/* Default number of buffers should be 3 */
	if ((ch0_numbuffers > 0) &&
	    (ch0_numbuffers < config_params.min_numbuffers))
		ch0_numbuffers = config_params.min_numbuffers;
	if ((ch1_numbuffers > 0) &&
	    (ch1_numbuffers < config_params.min_numbuffers))
		ch1_numbuffers = config_params.min_numbuffers;

	/* Set buffer size to min buffers size if it is invalid */
	if (ch0_bufsize < config_params.min_bufsize[VPIF_CHANNEL0_VIDEO])
		ch0_bufsize =
		    config_params.min_bufsize[VPIF_CHANNEL0_VIDEO];
	if (ch1_bufsize < config_params.min_bufsize[VPIF_CHANNEL1_VIDEO])
		ch1_bufsize =
		    config_params.min_bufsize[VPIF_CHANNEL1_VIDEO];

	config_params.numbuffers[VPIF_CHANNEL0_VIDEO] = ch0_numbuffers;
	config_params.numbuffers[VPIF_CHANNEL1_VIDEO] = ch1_numbuffers;
	if (ch0_numbuffers) {
		config_params.channel_bufsize[VPIF_CHANNEL0_VIDEO]
		    = ch0_bufsize;
	}
	if (ch1_numbuffers) {
		config_params.channel_bufsize[VPIF_CHANNEL1_VIDEO]
		    = ch1_bufsize;
	}

	/* Allocate memory for six channel objects */
	for (i = 0; i < VPIF_CAPTURE_MAX_DEVICES; i++) {
		vpif_obj.dev[i] =
		    kzalloc(sizeof(*vpif_obj.dev[i]), GFP_KERNEL);
		/* If memory allocation fails, return error */
		if (!vpif_obj.dev[i]) {
			free_channel_objects_index = i;
			err = -ENOMEM;
			goto vpif_init_free_channel_objects;
		}
	}
	return 0;

vpif_init_free_channel_objects:
	for (j = 0; j < free_channel_objects_index; j++)
		kfree(vpif_obj.dev[j]);
	return err;
}

/**
 * vpif_probe : This function probes the vpif capture driver
 * @pdev: platform device pointer
 *
 * This creates device entries by register itself to the V4L2 driver and
 * initializes fields of each channel objects
 */
static __init int vpif_probe(struct platform_device *pdev)
{
	struct vpif_subdev_info *subdevdata;
	struct vpif_capture_config *config;
	int i, j, k, m, q, err;
	struct i2c_adapter *i2c_adap;
	struct channel_obj *ch;
	struct common_obj *common;
	struct video_device *vfd;
	struct resource *res;
	int subdev_count;

	vpif_dev = &pdev->dev;

	err = initialize_vpif();
	if (err) {
		v4l2_err(vpif_dev->driver, "Error initializing vpif\n");
		return err;
	}

	k = 0;
	while ((res = platform_get_resource(pdev, IORESOURCE_IRQ, k))) {
		for (i = res->start; i <= res->end; i++) {
			if (request_irq(i, vpif_channel_isr, IRQF_DISABLED,
					"DM646x_Capture",
				(void *)(&vpif_obj.dev[k]->channel_id))) {
				err = -EBUSY;
				i--;
				goto vpif_int_err;
			}
		}
		k++;
	}

	for (i = 0; i < VPIF_CAPTURE_MAX_DEVICES; i++) {
		/* Get the pointer to the channel object */
		ch = vpif_obj.dev[i];
		/* Allocate memory for video device */
		vfd = video_device_alloc();
		if (NULL == vfd) {
			for (j = 0; j < i; j++) {
				ch = vpif_obj.dev[j];
				video_device_release(ch->video_dev);
			}
			err = -ENOMEM;
			goto vpif_dev_alloc_err;
		}

		/* Initialize field of video device */
		*vfd = vpif_video_template;
		vfd->v4l2_dev = &vpif_obj.v4l2_dev;
		vfd->release = video_device_release;
		snprintf(vfd->name, sizeof(vfd->name),
			 "DM646x_VPIFCapture_DRIVER_V%d.%d.%d",
			 (VPIF_CAPTURE_VERSION_CODE >> 16) & 0xff,
			 (VPIF_CAPTURE_VERSION_CODE >> 8) & 0xff,
			 (VPIF_CAPTURE_VERSION_CODE) & 0xff);
		/* Set video_dev to the video device */
		ch->video_dev = vfd;
	}

	for (j = 0; j < VPIF_CAPTURE_MAX_DEVICES; j++) {
		ch = vpif_obj.dev[j];
		ch->channel_id = j;
		common = &(ch->common[VPIF_VIDEO_INDEX]);
		spin_lock_init(&common->irqlock);
		mutex_init(&common->lock);
		/* Initialize prio member of channel object */
		v4l2_prio_init(&ch->prio);
		err = video_register_device(ch->video_dev,
					    VFL_TYPE_GRABBER, (j ? 1 : 0));
		if (err)
			goto probe_out;

		video_set_drvdata(ch->video_dev, ch);

	}

	i2c_adap = i2c_get_adapter(1);
	config = pdev->dev.platform_data;

	subdev_count = config->subdev_count;
	vpif_obj.sd = kmalloc(sizeof(struct v4l2_subdev *) * subdev_count,
				GFP_KERNEL);
	if (vpif_obj.sd == NULL) {
		vpif_err("unable to allocate memory for subdevice pointers\n");
		err = -ENOMEM;
		goto probe_out;
	}

	err = v4l2_device_register(vpif_dev, &vpif_obj.v4l2_dev);
	if (err) {
		v4l2_err(vpif_dev->driver, "Error registering v4l2 device\n");
		goto probe_subdev_out;
	}

	for (i = 0; i < subdev_count; i++) {
		subdevdata = &config->subdev_info[i];
		vpif_obj.sd[i] =
			v4l2_i2c_new_subdev_board(&vpif_obj.v4l2_dev,
						  i2c_adap,
						  subdevdata->name,
						  &subdevdata->board_info,
						  NULL);

		if (!vpif_obj.sd[i]) {
			vpif_err("Error registering v4l2 subdevice\n");
			goto probe_subdev_out;
		}
		v4l2_info(&vpif_obj.v4l2_dev, "registered sub device %s\n",
			  subdevdata->name);

		if (vpif_obj.sd[i])
			vpif_obj.sd[i]->grp_id = 1 << i;
	}
	v4l2_info(&vpif_obj.v4l2_dev, "DM646x VPIF Capture driver"
		  " initialized\n");

	return 0;

probe_subdev_out:
	/* free sub devices memory */
	kfree(vpif_obj.sd);

	j = VPIF_CAPTURE_MAX_DEVICES;
probe_out:
	v4l2_device_unregister(&vpif_obj.v4l2_dev);
	for (k = 0; k < j; k++) {
		/* Get the pointer to the channel object */
		ch = vpif_obj.dev[k];
		/* Unregister video device */
		video_unregister_device(ch->video_dev);
	}

vpif_dev_alloc_err:
	k = VPIF_CAPTURE_MAX_DEVICES-1;
	res = platform_get_resource(pdev, IORESOURCE_IRQ, k);
	i = res->end;

vpif_int_err:
	for (q = k; q >= 0; q--) {
		for (m = i; m >= (int)res->start; m--)
			free_irq(m, (void *)(&vpif_obj.dev[q]->channel_id));

		res = platform_get_resource(pdev, IORESOURCE_IRQ, q-1);
		if (res)
			i = res->end;
	}
	return err;
}

/**
 * vpif_remove() - driver remove handler
 * @device: ptr to platform device structure
 *
 * The vidoe device is unregistered
 */
static int vpif_remove(struct platform_device *device)
{
	int i;
	struct channel_obj *ch;

	v4l2_device_unregister(&vpif_obj.v4l2_dev);

	/* un-register device */
	for (i = 0; i < VPIF_CAPTURE_MAX_DEVICES; i++) {
		/* Get the pointer to the channel object */
		ch = vpif_obj.dev[i];
		/* Unregister video device */
		video_unregister_device(ch->video_dev);
	}
	return 0;
}

/**
 * vpif_suspend: vpif device suspend
 *
 * TODO: Add suspend code here
 */
static int
vpif_suspend(struct device *dev)
{
	return -1;
}

/**
 * vpif_resume: vpif device suspend
 *
 * TODO: Add resume code here
 */
static int
vpif_resume(struct device *dev)
{
	return -1;
}

static struct dev_pm_ops vpif_dev_pm_ops = {
	.suspend = vpif_suspend,
	.resume = vpif_resume,
};

static struct platform_driver vpif_driver = {
	.driver	= {
		.name	= "vpif_capture",
		.owner	= THIS_MODULE,
		.pm = &vpif_dev_pm_ops,
	},
	.probe = vpif_probe,
	.remove = vpif_remove,
};

/**
 * vpif_init: initialize the vpif driver
 *
 * This function registers device and driver to the kernel, requests irq
 * handler and allocates memory
 * for channel objects
 */
static __init int vpif_init(void)
{
	return platform_driver_register(&vpif_driver);
}

/**
 * vpif_cleanup : This function clean up the vpif capture resources
 *
 * This will un-registers device and driver to the kernel, frees
 * requested irq handler and de-allocates memory allocated for channel
 * objects.
 */
static void vpif_cleanup(void)
{
	struct platform_device *pdev;
	struct resource *res;
	int irq_num;
	int i = 0;

	pdev = container_of(vpif_dev, struct platform_device, dev);
	while ((res = platform_get_resource(pdev, IORESOURCE_IRQ, i))) {
		for (irq_num = res->start; irq_num <= res->end; irq_num++)
			free_irq(irq_num,
				 (void *)(&vpif_obj.dev[i]->channel_id));
		i++;
	}

	platform_driver_unregister(&vpif_driver);

	kfree(vpif_obj.sd);
	for (i = 0; i < VPIF_CAPTURE_MAX_DEVICES; i++)
		kfree(vpif_obj.dev[i]);
}

/* Function for module initialization and cleanup */
module_init(vpif_init);
module_exit(vpif_cleanup);
