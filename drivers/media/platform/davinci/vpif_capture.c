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

#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include <media/v4l2-ioctl.h>

#include "vpif.h"
#include "vpif_capture.h"

MODULE_DESCRIPTION("TI DaVinci VPIF Capture driver");
MODULE_LICENSE("GPL");
MODULE_VERSION(VPIF_CAPTURE_VERSION);

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
static void vpif_calculate_offsets(struct channel_obj *ch);
static void vpif_config_addr(struct channel_obj *ch, int muxmode);

/**
 * buffer_prepare :  callback function for buffer prepare
 * @vb: ptr to vb2_buffer
 *
 * This is the callback function for buffer prepare when vb2_qbuf()
 * function is called. The buffer is prepared and user space virtual address
 * or user address is converted into  physical address
 */
static int vpif_buffer_prepare(struct vb2_buffer *vb)
{
	/* Get the file handle object and channel object */
	struct vpif_fh *fh = vb2_get_drv_priv(vb->vb2_queue);
	struct vb2_queue *q = vb->vb2_queue;
	struct channel_obj *ch = fh->channel;
	struct common_obj *common;
	unsigned long addr;

	vpif_dbg(2, debug, "vpif_buffer_prepare\n");

	common = &ch->common[VPIF_VIDEO_INDEX];

	if (vb->state != VB2_BUF_STATE_ACTIVE &&
		vb->state != VB2_BUF_STATE_PREPARED) {
		vb2_set_plane_payload(vb, 0, common->fmt.fmt.pix.sizeimage);
		if (vb2_plane_vaddr(vb, 0) &&
		vb2_get_plane_payload(vb, 0) > vb2_plane_size(vb, 0))
			goto exit;
		addr = vb2_dma_contig_plane_dma_addr(vb, 0);

		if (q->streaming) {
			if (!IS_ALIGNED((addr + common->ytop_off), 8) ||
				!IS_ALIGNED((addr + common->ybtm_off), 8) ||
				!IS_ALIGNED((addr + common->ctop_off), 8) ||
				!IS_ALIGNED((addr + common->cbtm_off), 8))
				goto exit;
		}
	}
	return 0;
exit:
	vpif_dbg(1, debug, "buffer_prepare:offset is not aligned to 8 bytes\n");
	return -EINVAL;
}

/**
 * vpif_buffer_queue_setup : Callback function for buffer setup.
 * @vq: vb2_queue ptr
 * @fmt: v4l2 format
 * @nbuffers: ptr to number of buffers requested by application
 * @nplanes:: contains number of distinct video planes needed to hold a frame
 * @sizes[]: contains the size (in bytes) of each plane.
 * @alloc_ctxs: ptr to allocation context
 *
 * This callback function is called when reqbuf() is called to adjust
 * the buffer count and buffer size
 */
static int vpif_buffer_queue_setup(struct vb2_queue *vq,
				const struct v4l2_format *fmt,
				unsigned int *nbuffers, unsigned int *nplanes,
				unsigned int sizes[], void *alloc_ctxs[])
{
	/* Get the file handle object and channel object */
	struct vpif_fh *fh = vb2_get_drv_priv(vq);
	struct channel_obj *ch = fh->channel;
	struct common_obj *common;
	unsigned long size;

	common = &ch->common[VPIF_VIDEO_INDEX];

	vpif_dbg(2, debug, "vpif_buffer_setup\n");

	/* If memory type is not mmap, return */
	if (V4L2_MEMORY_MMAP == common->memory) {
		/* Calculate the size of the buffer */
		size = config_params.channel_bufsize[ch->channel_id];
		/*
		 * Checking if the buffer size exceeds the available buffer
		 * ycmux_mode = 0 means 1 channel mode HD and
		 * ycmux_mode = 1 means 2 channels mode SD
		 */
		if (ch->vpifparams.std_info.ycmux_mode == 0) {
			if (config_params.video_limit[ch->channel_id])
				while (size * *nbuffers >
					(config_params.video_limit[0]
						+ config_params.video_limit[1]))
					(*nbuffers)--;
		} else {
			if (config_params.video_limit[ch->channel_id])
				while (size * *nbuffers >
				config_params.video_limit[ch->channel_id])
					(*nbuffers)--;
		}

	} else {
		size = common->fmt.fmt.pix.sizeimage;
	}

	if (*nbuffers < config_params.min_numbuffers)
		*nbuffers = config_params.min_numbuffers;

	*nplanes = 1;
	sizes[0] = size;
	alloc_ctxs[0] = common->alloc_ctx;

	return 0;
}

/**
 * vpif_buffer_queue : Callback function to add buffer to DMA queue
 * @vb: ptr to vb2_buffer
 */
static void vpif_buffer_queue(struct vb2_buffer *vb)
{
	/* Get the file handle object and channel object */
	struct vpif_fh *fh = vb2_get_drv_priv(vb->vb2_queue);
	struct channel_obj *ch = fh->channel;
	struct vpif_cap_buffer *buf = container_of(vb,
				struct vpif_cap_buffer, vb);
	struct common_obj *common;
	unsigned long flags;

	common = &ch->common[VPIF_VIDEO_INDEX];

	vpif_dbg(2, debug, "vpif_buffer_queue\n");

	spin_lock_irqsave(&common->irqlock, flags);
	/* add the buffer to the DMA queue */
	list_add_tail(&buf->list, &common->dma_queue);
	spin_unlock_irqrestore(&common->irqlock, flags);
}

/**
 * vpif_buf_cleanup : Callback function to free buffer
 * @vb: ptr to vb2_buffer
 *
 * This function is called from the videobuf2 layer to free memory
 * allocated to  the buffers
 */
static void vpif_buf_cleanup(struct vb2_buffer *vb)
{
	/* Get the file handle object and channel object */
	struct vpif_fh *fh = vb2_get_drv_priv(vb->vb2_queue);
	struct vpif_cap_buffer *buf = container_of(vb,
					struct vpif_cap_buffer, vb);
	struct channel_obj *ch = fh->channel;
	struct common_obj *common;
	unsigned long flags;

	common = &ch->common[VPIF_VIDEO_INDEX];

	spin_lock_irqsave(&common->irqlock, flags);
	if (vb->state == VB2_BUF_STATE_ACTIVE)
		list_del_init(&buf->list);
	spin_unlock_irqrestore(&common->irqlock, flags);

}

static void vpif_wait_prepare(struct vb2_queue *vq)
{
	struct vpif_fh *fh = vb2_get_drv_priv(vq);
	struct channel_obj *ch = fh->channel;
	struct common_obj *common;

	common = &ch->common[VPIF_VIDEO_INDEX];
	mutex_unlock(&common->lock);
}

static void vpif_wait_finish(struct vb2_queue *vq)
{
	struct vpif_fh *fh = vb2_get_drv_priv(vq);
	struct channel_obj *ch = fh->channel;
	struct common_obj *common;

	common = &ch->common[VPIF_VIDEO_INDEX];
	mutex_lock(&common->lock);
}

static int vpif_buffer_init(struct vb2_buffer *vb)
{
	struct vpif_cap_buffer *buf = container_of(vb,
					struct vpif_cap_buffer, vb);

	INIT_LIST_HEAD(&buf->list);

	return 0;
}

static u8 channel_first_int[VPIF_NUMBER_OF_OBJECTS][2] =
	{ {1, 1} };

static int vpif_start_streaming(struct vb2_queue *vq, unsigned int count)
{
	struct vpif_capture_config *vpif_config_data =
					vpif_dev->platform_data;
	struct vpif_fh *fh = vb2_get_drv_priv(vq);
	struct channel_obj *ch = fh->channel;
	struct common_obj *common = &ch->common[VPIF_VIDEO_INDEX];
	struct vpif_params *vpif = &ch->vpifparams;
	unsigned long addr = 0;
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&common->irqlock, flags);

	/* Get the next frame from the buffer queue */
	common->cur_frm = common->next_frm = list_entry(common->dma_queue.next,
				    struct vpif_cap_buffer, list);
	/* Remove buffer from the buffer queue */
	list_del(&common->cur_frm->list);
	spin_unlock_irqrestore(&common->irqlock, flags);
	/* Mark state of the current frame to active */
	common->cur_frm->vb.state = VB2_BUF_STATE_ACTIVE;
	/* Initialize field_id and started member */
	ch->field_id = 0;
	common->started = 1;
	addr = vb2_dma_contig_plane_dma_addr(&common->cur_frm->vb, 0);

	/* Calculate the offset for Y and C data in the buffer */
	vpif_calculate_offsets(ch);

	if ((vpif->std_info.frm_fmt &&
	    ((common->fmt.fmt.pix.field != V4L2_FIELD_NONE) &&
	     (common->fmt.fmt.pix.field != V4L2_FIELD_ANY))) ||
	    (!vpif->std_info.frm_fmt &&
	     (common->fmt.fmt.pix.field == V4L2_FIELD_NONE))) {
		vpif_dbg(1, debug, "conflict in field format and std format\n");
		return -EINVAL;
	}

	/* configure 1 or 2 channel mode */
	if (vpif_config_data->setup_input_channel_mode) {
		ret = vpif_config_data->
			setup_input_channel_mode(vpif->std_info.ycmux_mode);
		if (ret < 0) {
			vpif_dbg(1, debug, "can't set vpif channel mode\n");
			return ret;
		}
	}

	/* Call vpif_set_params function to set the parameters and addresses */
	ret = vpif_set_video_params(vpif, ch->channel_id);

	if (ret < 0) {
		vpif_dbg(1, debug, "can't set video params\n");
		return ret;
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
	channel_first_int[VPIF_VIDEO_INDEX][ch->channel_id] = 1;
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

	return 0;
}

/* abort streaming and wait for last buffer */
static int vpif_stop_streaming(struct vb2_queue *vq)
{
	struct vpif_fh *fh = vb2_get_drv_priv(vq);
	struct channel_obj *ch = fh->channel;
	struct common_obj *common;
	unsigned long flags;

	if (!vb2_is_streaming(vq))
		return 0;

	common = &ch->common[VPIF_VIDEO_INDEX];

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

	/* release all active buffers */
	spin_lock_irqsave(&common->irqlock, flags);
	if (common->cur_frm == common->next_frm) {
		vb2_buffer_done(&common->cur_frm->vb, VB2_BUF_STATE_ERROR);
	} else {
		if (common->cur_frm != NULL)
			vb2_buffer_done(&common->cur_frm->vb,
					VB2_BUF_STATE_ERROR);
		if (common->next_frm != NULL)
			vb2_buffer_done(&common->next_frm->vb,
					VB2_BUF_STATE_ERROR);
	}

	while (!list_empty(&common->dma_queue)) {
		common->next_frm = list_entry(common->dma_queue.next,
						struct vpif_cap_buffer, list);
		list_del(&common->next_frm->list);
		vb2_buffer_done(&common->next_frm->vb, VB2_BUF_STATE_ERROR);
	}
	spin_unlock_irqrestore(&common->irqlock, flags);

	return 0;
}

static struct vb2_ops video_qops = {
	.queue_setup		= vpif_buffer_queue_setup,
	.wait_prepare		= vpif_wait_prepare,
	.wait_finish		= vpif_wait_finish,
	.buf_init		= vpif_buffer_init,
	.buf_prepare		= vpif_buffer_prepare,
	.start_streaming	= vpif_start_streaming,
	.stop_streaming		= vpif_stop_streaming,
	.buf_cleanup		= vpif_buf_cleanup,
	.buf_queue		= vpif_buffer_queue,
};

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
	v4l2_get_timestamp(&common->cur_frm->vb.v4l2_buf.timestamp);
	vb2_buffer_done(&common->cur_frm->vb,
					    VB2_BUF_STATE_DONE);
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

	spin_lock(&common->irqlock);
	common->next_frm = list_entry(common->dma_queue.next,
				     struct vpif_cap_buffer, list);
	/* Remove that buffer from the buffer queue */
	list_del(&common->next_frm->list);
	spin_unlock(&common->irqlock);
	common->next_frm->vb.state = VB2_BUF_STATE_ACTIVE;
	addr = vb2_dma_contig_plane_dma_addr(&common->next_frm->vb, 0);

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
 * and sets its address in VPIF registers
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
	if (!vpif_intr_status(channel_id))
		return IRQ_NONE;

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
			spin_lock(&common->irqlock);
			if (list_empty(&common->dma_queue)) {
				spin_unlock(&common->irqlock);
				continue;
			}
			spin_unlock(&common->irqlock);

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
				spin_lock(&common->irqlock);
				if (list_empty(&common->dma_queue) ||
				    (common->cur_frm != common->next_frm)) {
					spin_unlock(&common->irqlock);
					continue;
				}
				spin_unlock(&common->irqlock);

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
	struct vpif_channel_config_params *std_info = &vpifparams->std_info;
	struct video_obj *vid_ch = &ch->video;
	int index;

	vpif_dbg(2, debug, "vpif_update_std_info\n");

	for (index = 0; index < vpif_ch_params_count; index++) {
		config = &vpif_ch_params[index];
		if (config->hd_sd == 0) {
			vpif_dbg(2, debug, "SD format\n");
			if (config->stdid & vid_ch->stdid) {
				memcpy(std_info, config, sizeof(*config));
				break;
			}
		} else {
			vpif_dbg(2, debug, "HD format\n");
			if (!memcmp(&config->dv_timings, &vid_ch->dv_timings,
				sizeof(vid_ch->dv_timings))) {
				memcpy(std_info, config, sizeof(*config));
				break;
			}
		}
	}

	/* standard not found */
	if (index == vpif_ch_params_count)
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

	sizeimage = common->fmt.fmt.pix.sizeimage;

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

	sizeimage = pixfmt->sizeimage;

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
 * vpif_mmap : It is used to map kernel space buffers into user spaces
 * @filep: file pointer
 * @vma: ptr to vm_area_struct
 */
static int vpif_mmap(struct file *filep, struct vm_area_struct *vma)
{
	/* Get the channel object and file handle object */
	struct vpif_fh *fh = filep->private_data;
	struct channel_obj *ch = fh->channel;
	struct common_obj *common = &(ch->common[VPIF_VIDEO_INDEX]);
	int ret;

	vpif_dbg(2, debug, "vpif_mmap\n");

	if (mutex_lock_interruptible(&common->lock))
		return -ERESTARTSYS;
	ret = vb2_mmap(&common->buffer_queue, vma);
	mutex_unlock(&common->lock);
	return ret;
}

/**
 * vpif_poll: It is used for select/poll system call
 * @filep: file pointer
 * @wait: poll table to wait
 */
static unsigned int vpif_poll(struct file *filep, poll_table * wait)
{
	struct vpif_fh *fh = filep->private_data;
	struct channel_obj *channel = fh->channel;
	struct common_obj *common = &(channel->common[VPIF_VIDEO_INDEX]);
	unsigned int res = 0;

	vpif_dbg(2, debug, "vpif_poll\n");

	if (common->started) {
		mutex_lock(&common->lock);
		res = vb2_poll(&common->buffer_queue, filep, wait);
		mutex_unlock(&common->lock);
	}
	return res;
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
	struct video_device *vdev = video_devdata(filep);
	struct common_obj *common;
	struct video_obj *vid_ch;
	struct channel_obj *ch;
	struct vpif_fh *fh;

	vpif_dbg(2, debug, "vpif_open\n");

	ch = video_get_drvdata(vdev);

	vid_ch = &ch->video;
	common = &ch->common[VPIF_VIDEO_INDEX];

	/* Allocate memory for the file handle object */
	fh = kzalloc(sizeof(struct vpif_fh), GFP_KERNEL);
	if (NULL == fh) {
		vpif_err("unable to allocate memory for file handle object\n");
		return -ENOMEM;
	}

	if (mutex_lock_interruptible(&common->lock)) {
		kfree(fh);
		return -ERESTARTSYS;
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
	mutex_unlock(&common->lock);
	return 0;
}

/**
 * vpif_release : function to clean up file close
 * @filep: file pointer
 *
 * This function deletes buffer queue, frees the buffers and the vpif file
 * handle
 */
static int vpif_release(struct file *filep)
{
	struct vpif_fh *fh = filep->private_data;
	struct channel_obj *ch = fh->channel;
	struct common_obj *common;

	vpif_dbg(2, debug, "vpif_release\n");

	common = &ch->common[VPIF_VIDEO_INDEX];

	mutex_lock(&common->lock);
	/* if this instance is doing IO */
	if (fh->io_allowed[VPIF_VIDEO_INDEX]) {
		/* Reset io_usrs member of channel object */
		common->io_usrs = 0;
		/* Free buffers allocated */
		vb2_queue_release(&common->buffer_queue);
		vb2_dma_contig_cleanup_ctx(common->alloc_ctx);
	}

	/* Decrement channel usrs counter */
	ch->usrs--;

	/* Close the priority */
	v4l2_prio_close(&ch->prio, fh->prio);

	if (fh->initialized)
		ch->initialized = 0;

	mutex_unlock(&common->lock);
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
	struct vb2_queue *q;
	int ret;

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

	if (V4L2_BUF_TYPE_VIDEO_CAPTURE != reqbuf->type || !vpif_dev)
		return -EINVAL;

	index = VPIF_VIDEO_INDEX;

	common = &ch->common[index];

	if (0 != common->io_usrs)
		return -EBUSY;

	/* Initialize videobuf2 queue as per the buffer type */
	common->alloc_ctx = vb2_dma_contig_init_ctx(vpif_dev);
	if (IS_ERR(common->alloc_ctx)) {
		vpif_err("Failed to get the context\n");
		return PTR_ERR(common->alloc_ctx);
	}
	q = &common->buffer_queue;
	q->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	q->io_modes = VB2_MMAP | VB2_USERPTR;
	q->drv_priv = fh;
	q->ops = &video_qops;
	q->mem_ops = &vb2_dma_contig_memops;
	q->buf_struct_size = sizeof(struct vpif_cap_buffer);
	q->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
	q->min_buffers_needed = 1;

	ret = vb2_queue_init(q);
	if (ret) {
		vpif_err("vpif_capture: vb2_queue_init() failed\n");
		vb2_dma_contig_cleanup_ctx(common->alloc_ctx);
		return ret;
	}
	/* Set io allowed member of file handle to TRUE */
	fh->io_allowed[index] = 1;
	/* Increment io usrs member of channel object to 1 */
	common->io_usrs = 1;
	/* Store type of memory requested in channel object */
	common->memory = reqbuf->memory;
	INIT_LIST_HEAD(&common->dma_queue);

	/* Allocate buffers */
	return vb2_reqbufs(&common->buffer_queue, reqbuf);
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

	return vb2_querybuf(&common->buffer_queue, buf);
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

	vpif_dbg(2, debug, "vpif_qbuf\n");

	if (common->fmt.type != tbuf.type) {
		vpif_err("invalid buffer type\n");
		return -EINVAL;
	}

	if (!fh->io_allowed[VPIF_VIDEO_INDEX]) {
		vpif_err("fh io not allowed\n");
		return -EACCES;
	}

	return vb2_qbuf(&common->buffer_queue, buf);
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

	return vb2_dqbuf(&common->buffer_queue, buf,
			 (file->f_flags & O_NONBLOCK));
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

	struct vpif_fh *fh = priv;
	struct channel_obj *ch = fh->channel;
	struct common_obj *common = &ch->common[VPIF_VIDEO_INDEX];
	struct channel_obj *oth_ch = vpif_obj.dev[!ch->channel_id];
	struct vpif_params *vpif;
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
	ret = v4l2_subdev_call(ch->sd, video, s_stream, 1);

	if (ret && ret != -ENOIOCTLCMD && ret != -ENODEV) {
		vpif_dbg(1, debug, "stream on failed in subdev\n");
		return ret;
	}

	/* Call vb2_streamon to start streaming in videobuf2 */
	ret = vb2_streamon(&common->buffer_queue, buftype);
	if (ret) {
		vpif_dbg(1, debug, "vb2_streamon\n");
		return ret;
	}

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

	/* disable channel */
	if (VPIF_CHANNEL0_VIDEO == ch->channel_id) {
		enable_channel0(0);
		channel0_intr_enable(0);
	} else {
		enable_channel1(0);
		channel1_intr_enable(0);
	}

	common->started = 0;

	ret = v4l2_subdev_call(ch->sd, video, s_stream, 0);

	if (ret && ret != -ENOIOCTLCMD && ret != -ENODEV)
		vpif_dbg(1, debug, "stream off failed in subdev\n");

	return vb2_streamoff(&common->buffer_queue, buftype);
}

/**
 * vpif_input_to_subdev() - Maps input to sub device
 * @vpif_cfg - global config ptr
 * @chan_cfg - channel config ptr
 * @input_index - Given input index from application
 *
 * lookup the sub device information for a given input index.
 * we report all the inputs to application. inputs table also
 * has sub device name for the each input
 */
static int vpif_input_to_subdev(
		struct vpif_capture_config *vpif_cfg,
		struct vpif_capture_chan_config *chan_cfg,
		int input_index)
{
	struct vpif_subdev_info *subdev_info;
	const char *subdev_name;
	int i;

	vpif_dbg(2, debug, "vpif_input_to_subdev\n");

	subdev_name = chan_cfg->inputs[input_index].subdev_name;
	if (subdev_name == NULL)
		return -1;

	/* loop through the sub device list to get the sub device info */
	for (i = 0; i < vpif_cfg->subdev_count; i++) {
		subdev_info = &vpif_cfg->subdev_info[i];
		if (!strcmp(subdev_info->name, subdev_name))
			return i;
	}
	return -1;
}

/**
 * vpif_set_input() - Select an input
 * @vpif_cfg - global config ptr
 * @ch - channel
 * @_index - Given input index from application
 *
 * Select the given input.
 */
static int vpif_set_input(
		struct vpif_capture_config *vpif_cfg,
		struct channel_obj *ch,
		int index)
{
	struct vpif_capture_chan_config *chan_cfg =
			&vpif_cfg->chan_config[ch->channel_id];
	struct vpif_subdev_info *subdev_info = NULL;
	struct v4l2_subdev *sd = NULL;
	u32 input = 0, output = 0;
	int sd_index;
	int ret;

	sd_index = vpif_input_to_subdev(vpif_cfg, chan_cfg, index);
	if (sd_index >= 0) {
		sd = vpif_obj.sd[sd_index];
		subdev_info = &vpif_cfg->subdev_info[sd_index];
	}

	/* first setup input path from sub device to vpif */
	if (sd && vpif_cfg->setup_input_path) {
		ret = vpif_cfg->setup_input_path(ch->channel_id,
				       subdev_info->name);
		if (ret < 0) {
			vpif_dbg(1, debug, "couldn't setup input path for the" \
			" sub device %s, for input index %d\n",
			subdev_info->name, index);
			return ret;
		}
	}

	if (sd) {
		input = chan_cfg->inputs[index].input_route;
		output = chan_cfg->inputs[index].output_route;
		ret = v4l2_subdev_call(sd, video, s_routing,
				input, output, 0);
		if (ret < 0 && ret != -ENOIOCTLCMD) {
			vpif_dbg(1, debug, "Failed to set input\n");
			return ret;
		}
	}
	ch->input_idx = index;
	ch->sd = sd;
	/* copy interface parameters to vpif */
	ch->vpifparams.iface = chan_cfg->vpif_if;

	/* update tvnorms from the sub device input info */
	ch->video_dev->tvnorms = chan_cfg->inputs[index].input.std;
	return 0;
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
	int ret = 0;

	vpif_dbg(2, debug, "vpif_querystd\n");

	/* Call querystd function of decoder device */
	ret = v4l2_subdev_call(ch->sd, video, querystd, std_id);

	if (ret == -ENOIOCTLCMD || ret == -ENODEV)
		return -ENODATA;
	if (ret) {
		vpif_dbg(1, debug, "Failed to query standard for sub devices\n");
		return ret;
	}

	return 0;
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
static int vpif_s_std(struct file *file, void *priv, v4l2_std_id std_id)
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

	ret = v4l2_prio_check(&ch->prio, fh->prio);
	if (0 != ret)
		return ret;

	fh->initialized = 1;

	/* Call encoder subdevice function to set the standard */
	ch->video.stdid = std_id;
	memset(&ch->video.dv_timings, 0, sizeof(ch->video.dv_timings));

	/* Get the information about the standard */
	if (vpif_update_std_info(ch)) {
		vpif_err("Error getting the standard info\n");
		return -EINVAL;
	}

	/* Configure the default format information */
	vpif_config_format(ch);

	/* set standard in the sub device */
	ret = v4l2_subdev_call(ch->sd, core, s_std, std_id);
	if (ret && ret != -ENOIOCTLCMD && ret != -ENODEV) {
		vpif_dbg(1, debug, "Failed to set standard for sub devices\n");
		return ret;
	}
	return 0;
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

	*index = ch->input_idx;
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
	int ret;

	chan_cfg = &config->chan_config[ch->channel_id];

	if (index >= chan_cfg->input_count)
		return -EINVAL;

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

	ret = v4l2_prio_check(&ch->prio, fh->prio);
	if (0 != ret)
		return ret;

	fh->initialized = 1;
	return vpif_set_input(config, ch, index);
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
	*fmt = common->fmt;
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

	vpif_dbg(2, debug, "%s\n", __func__);

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

	ret = v4l2_prio_check(&ch->prio, fh->prio);
	if (0 != ret)
		return ret;

	fh->initialized = 1;

	pixfmt = &fmt->fmt.pix;
	/* Check for valid field format */
	ret = vpif_check_format(ch, pixfmt, 0);

	if (ret)
		return ret;
	/* store the format in the channel object */
	common->fmt = *fmt;
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

	cap->device_caps = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING;
	cap->capabilities = cap->device_caps | V4L2_CAP_DEVICE_CAPS;
	snprintf(cap->driver, sizeof(cap->driver), "%s", dev_name(vpif_dev));
	snprintf(cap->bus_info, sizeof(cap->bus_info), "platform:%s",
		 dev_name(vpif_dev));
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

/**
 * vpif_enum_dv_timings() - ENUM_DV_TIMINGS handler
 * @file: file ptr
 * @priv: file handle
 * @timings: input timings
 */
static int
vpif_enum_dv_timings(struct file *file, void *priv,
		     struct v4l2_enum_dv_timings *timings)
{
	struct vpif_fh *fh = priv;
	struct channel_obj *ch = fh->channel;
	int ret;

	ret = v4l2_subdev_call(ch->sd, video, enum_dv_timings, timings);
	if (ret == -ENOIOCTLCMD || ret == -ENODEV)
		return -EINVAL;
	return ret;
}

/**
 * vpif_query_dv_timings() - QUERY_DV_TIMINGS handler
 * @file: file ptr
 * @priv: file handle
 * @timings: input timings
 */
static int
vpif_query_dv_timings(struct file *file, void *priv,
		      struct v4l2_dv_timings *timings)
{
	struct vpif_fh *fh = priv;
	struct channel_obj *ch = fh->channel;
	int ret;

	ret = v4l2_subdev_call(ch->sd, video, query_dv_timings, timings);
	if (ret == -ENOIOCTLCMD || ret == -ENODEV)
		return -ENODATA;
	return ret;
}

/**
 * vpif_s_dv_timings() - S_DV_TIMINGS handler
 * @file: file ptr
 * @priv: file handle
 * @timings: digital video timings
 */
static int vpif_s_dv_timings(struct file *file, void *priv,
		struct v4l2_dv_timings *timings)
{
	struct vpif_fh *fh = priv;
	struct channel_obj *ch = fh->channel;
	struct vpif_params *vpifparams = &ch->vpifparams;
	struct vpif_channel_config_params *std_info = &vpifparams->std_info;
	struct video_obj *vid_ch = &ch->video;
	struct v4l2_bt_timings *bt = &vid_ch->dv_timings.bt;
	int ret;

	if (timings->type != V4L2_DV_BT_656_1120) {
		vpif_dbg(2, debug, "Timing type not defined\n");
		return -EINVAL;
	}

	/* Configure subdevice timings, if any */
	ret = v4l2_subdev_call(ch->sd, video, s_dv_timings, timings);
	if (ret == -ENOIOCTLCMD || ret == -ENODEV)
		ret = 0;
	if (ret < 0) {
		vpif_dbg(2, debug, "Error setting custom DV timings\n");
		return ret;
	}

	if (!(timings->bt.width && timings->bt.height &&
				(timings->bt.hbackporch ||
				 timings->bt.hfrontporch ||
				 timings->bt.hsync) &&
				timings->bt.vfrontporch &&
				(timings->bt.vbackporch ||
				 timings->bt.vsync))) {
		vpif_dbg(2, debug, "Timings for width, height, "
				"horizontal back porch, horizontal sync, "
				"horizontal front porch, vertical back porch, "
				"vertical sync and vertical back porch "
				"must be defined\n");
		return -EINVAL;
	}

	vid_ch->dv_timings = *timings;

	/* Configure video port timings */

	std_info->eav2sav = V4L2_DV_BT_BLANKING_WIDTH(bt) - 8;
	std_info->sav2eav = bt->width;

	std_info->l1 = 1;
	std_info->l3 = bt->vsync + bt->vbackporch + 1;

	std_info->vsize = V4L2_DV_BT_FRAME_HEIGHT(bt);
	if (bt->interlaced) {
		if (bt->il_vbackporch || bt->il_vfrontporch || bt->il_vsync) {
			std_info->l5 = std_info->vsize/2 -
				(bt->vfrontporch - 1);
			std_info->l7 = std_info->vsize/2 + 1;
			std_info->l9 = std_info->l7 + bt->il_vsync +
				bt->il_vbackporch + 1;
			std_info->l11 = std_info->vsize -
				(bt->il_vfrontporch - 1);
		} else {
			vpif_dbg(2, debug, "Required timing values for "
					"interlaced BT format missing\n");
			return -EINVAL;
		}
	} else {
		std_info->l5 = std_info->vsize - (bt->vfrontporch - 1);
	}
	strncpy(std_info->name, "Custom timings BT656/1120", VPIF_MAX_NAME);
	std_info->width = bt->width;
	std_info->height = bt->height;
	std_info->frm_fmt = bt->interlaced ? 0 : 1;
	std_info->ycmux_mode = 0;
	std_info->capture_format = 0;
	std_info->vbi_supported = 0;
	std_info->hd_sd = 1;
	std_info->stdid = 0;

	vid_ch->stdid = 0;
	return 0;
}

/**
 * vpif_g_dv_timings() - G_DV_TIMINGS handler
 * @file: file ptr
 * @priv: file handle
 * @timings: digital video timings
 */
static int vpif_g_dv_timings(struct file *file, void *priv,
		struct v4l2_dv_timings *timings)
{
	struct vpif_fh *fh = priv;
	struct channel_obj *ch = fh->channel;
	struct video_obj *vid_ch = &ch->video;

	*timings = vid_ch->dv_timings;

	return 0;
}

/*
 * vpif_log_status() - Status information
 * @file: file ptr
 * @priv: file handle
 *
 * Returns zero.
 */
static int vpif_log_status(struct file *filep, void *priv)
{
	/* status for sub devices */
	v4l2_device_call_all(&vpif_obj.v4l2_dev, 0, core, log_status);

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
	.vidioc_enum_dv_timings         = vpif_enum_dv_timings,
	.vidioc_query_dv_timings        = vpif_query_dv_timings,
	.vidioc_s_dv_timings            = vpif_s_dv_timings,
	.vidioc_g_dv_timings            = vpif_g_dv_timings,
	.vidioc_log_status		= vpif_log_status,
};

/* vpif file operations */
static struct v4l2_file_operations vpif_fops = {
	.owner = THIS_MODULE,
	.open = vpif_open,
	.release = vpif_release,
	.unlocked_ioctl = video_ioctl2,
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

static int vpif_async_bound(struct v4l2_async_notifier *notifier,
			    struct v4l2_subdev *subdev,
			    struct v4l2_async_subdev *asd)
{
	int i;

	for (i = 0; i < vpif_obj.config->subdev_count; i++)
		if (!strcmp(vpif_obj.config->subdev_info[i].name,
			    subdev->name)) {
			vpif_obj.sd[i] = subdev;
			return 0;
		}

	return -EINVAL;
}

static int vpif_probe_complete(void)
{
	struct common_obj *common;
	struct channel_obj *ch;
	int i, j, err, k;

	for (j = 0; j < VPIF_CAPTURE_MAX_DEVICES; j++) {
		ch = vpif_obj.dev[j];
		ch->channel_id = j;
		common = &(ch->common[VPIF_VIDEO_INDEX]);
		spin_lock_init(&common->irqlock);
		mutex_init(&common->lock);
		ch->video_dev->lock = &common->lock;
		/* Initialize prio member of channel object */
		v4l2_prio_init(&ch->prio);
		video_set_drvdata(ch->video_dev, ch);

		/* select input 0 */
		err = vpif_set_input(vpif_obj.config, ch, 0);
		if (err)
			goto probe_out;

		err = video_register_device(ch->video_dev,
					    VFL_TYPE_GRABBER, (j ? 1 : 0));
		if (err)
			goto probe_out;
	}

	v4l2_info(&vpif_obj.v4l2_dev, "VPIF capture driver initialized\n");
	return 0;

probe_out:
	for (k = 0; k < j; k++) {
		/* Get the pointer to the channel object */
		ch = vpif_obj.dev[k];
		/* Unregister video device */
		video_unregister_device(ch->video_dev);
	}
	kfree(vpif_obj.sd);
	for (i = 0; i < VPIF_CAPTURE_MAX_DEVICES; i++) {
		ch = vpif_obj.dev[i];
		/* Note: does nothing if ch->video_dev == NULL */
		video_device_release(ch->video_dev);
	}
	v4l2_device_unregister(&vpif_obj.v4l2_dev);

	return err;
}

static int vpif_async_complete(struct v4l2_async_notifier *notifier)
{
	return vpif_probe_complete();
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
	int i, j, err;
	int res_idx = 0;
	struct i2c_adapter *i2c_adap;
	struct channel_obj *ch;
	struct video_device *vfd;
	struct resource *res;
	int subdev_count;
	size_t size;

	vpif_dev = &pdev->dev;

	err = initialize_vpif();
	if (err) {
		v4l2_err(vpif_dev->driver, "Error initializing vpif\n");
		return err;
	}

	err = v4l2_device_register(vpif_dev, &vpif_obj.v4l2_dev);
	if (err) {
		v4l2_err(vpif_dev->driver, "Error registering v4l2 device\n");
		return err;
	}

	while ((res = platform_get_resource(pdev, IORESOURCE_IRQ, res_idx))) {
		err = devm_request_irq(&pdev->dev, res->start, vpif_channel_isr,
					IRQF_SHARED, "VPIF_Capture",
					(void *)(&vpif_obj.dev[res_idx]->
					channel_id));
		if (err) {
			err = -EINVAL;
			goto vpif_unregister;
		}
		res_idx++;
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
			goto vpif_unregister;
		}

		/* Initialize field of video device */
		*vfd = vpif_video_template;
		vfd->v4l2_dev = &vpif_obj.v4l2_dev;
		vfd->release = video_device_release;
		snprintf(vfd->name, sizeof(vfd->name),
			 "VPIF_Capture_DRIVER_V%s",
			 VPIF_CAPTURE_VERSION);
		/* Set video_dev to the video device */
		ch->video_dev = vfd;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (res) {
		size = resource_size(res);
		/* The resources are divided into two equal memory and when we
		 * have HD output we can add them together
		 */
		for (j = 0; j < VPIF_CAPTURE_MAX_DEVICES; j++) {
			ch = vpif_obj.dev[j];
			ch->channel_id = j;
			/* only enabled if second resource exists */
			config_params.video_limit[ch->channel_id] = 0;
			if (size)
				config_params.video_limit[ch->channel_id] =
									size/2;
		}
	}

	vpif_obj.config = pdev->dev.platform_data;

	subdev_count = vpif_obj.config->subdev_count;
	vpif_obj.sd = kzalloc(sizeof(struct v4l2_subdev *) * subdev_count,
				GFP_KERNEL);
	if (vpif_obj.sd == NULL) {
		vpif_err("unable to allocate memory for subdevice pointers\n");
		err = -ENOMEM;
		goto vpif_sd_error;
	}

	if (!vpif_obj.config->asd_sizes) {
		i2c_adap = i2c_get_adapter(1);
		for (i = 0; i < subdev_count; i++) {
			subdevdata = &vpif_obj.config->subdev_info[i];
			vpif_obj.sd[i] =
				v4l2_i2c_new_subdev_board(&vpif_obj.v4l2_dev,
							  i2c_adap,
							  &subdevdata->
							  board_info,
							  NULL);

			if (!vpif_obj.sd[i]) {
				vpif_err("Error registering v4l2 subdevice\n");
				err = -ENODEV;
				goto probe_subdev_out;
			}
			v4l2_info(&vpif_obj.v4l2_dev,
				  "registered sub device %s\n",
				   subdevdata->name);
		}
		vpif_probe_complete();
	} else {
		vpif_obj.notifier.subdevs = vpif_obj.config->asd;
		vpif_obj.notifier.num_subdevs = vpif_obj.config->asd_sizes[0];
		vpif_obj.notifier.bound = vpif_async_bound;
		vpif_obj.notifier.complete = vpif_async_complete;
		err = v4l2_async_notifier_register(&vpif_obj.v4l2_dev,
						   &vpif_obj.notifier);
		if (err) {
			vpif_err("Error registering async notifier\n");
			err = -EINVAL;
			goto probe_subdev_out;
		}
	}

	return 0;

probe_subdev_out:
	/* free sub devices memory */
	kfree(vpif_obj.sd);

vpif_sd_error:
	for (i = 0; i < VPIF_CAPTURE_MAX_DEVICES; i++) {
		ch = vpif_obj.dev[i];
		/* Note: does nothing if ch->video_dev == NULL */
		video_device_release(ch->video_dev);
	}
vpif_unregister:
	v4l2_device_unregister(&vpif_obj.v4l2_dev);

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
	struct channel_obj *ch;
	int i;

	v4l2_device_unregister(&vpif_obj.v4l2_dev);

	kfree(vpif_obj.sd);
	/* un-register device */
	for (i = 0; i < VPIF_CAPTURE_MAX_DEVICES; i++) {
		/* Get the pointer to the channel object */
		ch = vpif_obj.dev[i];
		/* Unregister video device */
		video_unregister_device(ch->video_dev);
		kfree(vpif_obj.dev[i]);
	}
	return 0;
}

#ifdef CONFIG_PM
/**
 * vpif_suspend: vpif device suspend
 */
static int vpif_suspend(struct device *dev)
{

	struct common_obj *common;
	struct channel_obj *ch;
	int i;

	for (i = 0; i < VPIF_CAPTURE_MAX_DEVICES; i++) {
		/* Get the pointer to the channel object */
		ch = vpif_obj.dev[i];
		common = &ch->common[VPIF_VIDEO_INDEX];
		mutex_lock(&common->lock);
		if (ch->usrs && common->io_usrs) {
			/* Disable channel */
			if (ch->channel_id == VPIF_CHANNEL0_VIDEO) {
				enable_channel0(0);
				channel0_intr_enable(0);
			}
			if (ch->channel_id == VPIF_CHANNEL1_VIDEO ||
			    common->started == 2) {
				enable_channel1(0);
				channel1_intr_enable(0);
			}
		}
		mutex_unlock(&common->lock);
	}

	return 0;
}

/*
 * vpif_resume: vpif device suspend
 */
static int vpif_resume(struct device *dev)
{
	struct common_obj *common;
	struct channel_obj *ch;
	int i;

	for (i = 0; i < VPIF_CAPTURE_MAX_DEVICES; i++) {
		/* Get the pointer to the channel object */
		ch = vpif_obj.dev[i];
		common = &ch->common[VPIF_VIDEO_INDEX];
		mutex_lock(&common->lock);
		if (ch->usrs && common->io_usrs) {
			/* Disable channel */
			if (ch->channel_id == VPIF_CHANNEL0_VIDEO) {
				enable_channel0(1);
				channel0_intr_enable(1);
			}
			if (ch->channel_id == VPIF_CHANNEL1_VIDEO ||
			    common->started == 2) {
				enable_channel1(1);
				channel1_intr_enable(1);
			}
		}
		mutex_unlock(&common->lock);
	}

	return 0;
}

static const struct dev_pm_ops vpif_dev_pm_ops = {
	.suspend = vpif_suspend,
	.resume = vpif_resume,
};

#define vpif_pm_ops (&vpif_dev_pm_ops)
#else
#define vpif_pm_ops NULL
#endif

static __refdata struct platform_driver vpif_driver = {
	.driver	= {
		.name	= "vpif_capture",
		.owner	= THIS_MODULE,
		.pm	= vpif_pm_ops,
	},
	.probe = vpif_probe,
	.remove = vpif_remove,
};

module_platform_driver(vpif_driver);
