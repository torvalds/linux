/*
 * vpif-display - VPIF display driver
 * Display driver for TI DaVinci VPIF
 *
 * Copyright (C) 2009 Texas Instruments Incorporated - http://www.ti.com/
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed .as is. WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
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
#include <linux/slab.h>

#include <asm/irq.h>
#include <asm/page.h>

#include <media/adv7343.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-chip-ident.h>

#include "vpif_display.h"
#include "vpif.h"

MODULE_DESCRIPTION("TI DaVinci VPIF Display driver");
MODULE_LICENSE("GPL");
MODULE_VERSION(VPIF_DISPLAY_VERSION);

#define VPIF_V4L2_STD (V4L2_STD_525_60 | V4L2_STD_625_50)

#define vpif_err(fmt, arg...)	v4l2_err(&vpif_obj.v4l2_dev, fmt, ## arg)
#define vpif_dbg(level, debug, fmt, arg...)	\
		v4l2_dbg(level, debug, &vpif_obj.v4l2_dev, fmt, ## arg)

static int debug = 1;
static u32 ch2_numbuffers = 3;
static u32 ch3_numbuffers = 3;
static u32 ch2_bufsize = 1920 * 1080 * 2;
static u32 ch3_bufsize = 720 * 576 * 2;

module_param(debug, int, 0644);
module_param(ch2_numbuffers, uint, S_IRUGO);
module_param(ch3_numbuffers, uint, S_IRUGO);
module_param(ch2_bufsize, uint, S_IRUGO);
module_param(ch3_bufsize, uint, S_IRUGO);

MODULE_PARM_DESC(debug, "Debug level 0-1");
MODULE_PARM_DESC(ch2_numbuffers, "Channel2 buffer count (default:3)");
MODULE_PARM_DESC(ch3_numbuffers, "Channel3 buffer count (default:3)");
MODULE_PARM_DESC(ch2_bufsize, "Channel2 buffer size (default:1920 x 1080 x 2)");
MODULE_PARM_DESC(ch3_bufsize, "Channel3 buffer size (default:720 x 576 x 2)");

static struct vpif_config_params config_params = {
	.min_numbuffers		= 3,
	.numbuffers[0]		= 3,
	.numbuffers[1]		= 3,
	.min_bufsize[0]		= 720 * 480 * 2,
	.min_bufsize[1]		= 720 * 480 * 2,
	.channel_bufsize[0]	= 1920 * 1080 * 2,
	.channel_bufsize[1]	= 720 * 576 * 2,
};

static struct vpif_device vpif_obj = { {NULL} };
static struct device *vpif_dev;
static void vpif_calculate_offsets(struct channel_obj *ch);
static void vpif_config_addr(struct channel_obj *ch, int muxmode);

/*
 * buffer_prepare: This is the callback function called from vb2_qbuf()
 * function the buffer is prepared and user space virtual address is converted
 * into physical address
 */
static int vpif_buffer_prepare(struct vb2_buffer *vb)
{
	struct vpif_fh *fh = vb2_get_drv_priv(vb->vb2_queue);
	struct vb2_queue *q = vb->vb2_queue;
	struct common_obj *common;
	unsigned long addr;

	common = &fh->channel->common[VPIF_VIDEO_INDEX];
	if (vb->state != VB2_BUF_STATE_ACTIVE &&
		vb->state != VB2_BUF_STATE_PREPARED) {
		vb2_set_plane_payload(vb, 0, common->fmt.fmt.pix.sizeimage);
		if (vb2_plane_vaddr(vb, 0) &&
		vb2_get_plane_payload(vb, 0) > vb2_plane_size(vb, 0))
			goto buf_align_exit;

		addr = vb2_dma_contig_plane_dma_addr(vb, 0);
		if (q->streaming &&
			(V4L2_BUF_TYPE_SLICED_VBI_OUTPUT != q->type)) {
			if (!ISALIGNED(addr + common->ytop_off) ||
			!ISALIGNED(addr + common->ybtm_off) ||
			!ISALIGNED(addr + common->ctop_off) ||
			!ISALIGNED(addr + common->cbtm_off))
				goto buf_align_exit;
		}
	}
	return 0;

buf_align_exit:
	vpif_err("buffer offset not aligned to 8 bytes\n");
	return -EINVAL;
}

/*
 * vpif_buffer_queue_setup: This function allocates memory for the buffers
 */
static int vpif_buffer_queue_setup(struct vb2_queue *vq,
				const struct v4l2_format *fmt,
				unsigned int *nbuffers, unsigned int *nplanes,
				unsigned int sizes[], void *alloc_ctxs[])
{
	struct vpif_fh *fh = vb2_get_drv_priv(vq);
	struct channel_obj *ch = fh->channel;
	struct common_obj *common = &ch->common[VPIF_VIDEO_INDEX];
	unsigned long size;

	if (V4L2_MEMORY_MMAP == common->memory) {
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

/*
 * vpif_buffer_queue: This function adds the buffer to DMA queue
 */
static void vpif_buffer_queue(struct vb2_buffer *vb)
{
	struct vpif_fh *fh = vb2_get_drv_priv(vb->vb2_queue);
	struct vpif_disp_buffer *buf = container_of(vb,
				struct vpif_disp_buffer, vb);
	struct channel_obj *ch = fh->channel;
	struct common_obj *common;
	unsigned long flags;

	common = &ch->common[VPIF_VIDEO_INDEX];

	/* add the buffer to the DMA queue */
	spin_lock_irqsave(&common->irqlock, flags);
	list_add_tail(&buf->list, &common->dma_queue);
	spin_unlock_irqrestore(&common->irqlock, flags);
}

/*
 * vpif_buf_cleanup: This function is called from the videobuf2 layer to
 * free memory allocated to the buffers
 */
static void vpif_buf_cleanup(struct vb2_buffer *vb)
{
	struct vpif_fh *fh = vb2_get_drv_priv(vb->vb2_queue);
	struct vpif_disp_buffer *buf = container_of(vb,
					struct vpif_disp_buffer, vb);
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
	struct vpif_disp_buffer *buf = container_of(vb,
					struct vpif_disp_buffer, vb);

	INIT_LIST_HEAD(&buf->list);

	return 0;
}

static u8 channel_first_int[VPIF_NUMOBJECTS][2] = { {1, 1} };

static int vpif_start_streaming(struct vb2_queue *vq, unsigned int count)
{
	struct vpif_display_config *vpif_config_data =
					vpif_dev->platform_data;
	struct vpif_fh *fh = vb2_get_drv_priv(vq);
	struct channel_obj *ch = fh->channel;
	struct common_obj *common = &ch->common[VPIF_VIDEO_INDEX];
	struct vpif_params *vpif = &ch->vpifparams;
	unsigned long addr = 0;
	unsigned long flags;
	int ret;

	/* If buffer queue is empty, return error */
	spin_lock_irqsave(&common->irqlock, flags);
	if (list_empty(&common->dma_queue)) {
		spin_unlock_irqrestore(&common->irqlock, flags);
		vpif_err("buffer queue is empty\n");
		return -EIO;
	}

	/* Get the next frame from the buffer queue */
	common->next_frm = common->cur_frm =
			    list_entry(common->dma_queue.next,
				       struct vpif_disp_buffer, list);

	list_del(&common->cur_frm->list);
	spin_unlock_irqrestore(&common->irqlock, flags);
	/* Mark state of the current frame to active */
	common->cur_frm->vb.state = VB2_BUF_STATE_ACTIVE;

	/* Initialize field_id and started member */
	ch->field_id = 0;
	common->started = 1;
	addr = vb2_dma_contig_plane_dma_addr(&common->cur_frm->vb, 0);
	/* Calculate the offset for Y and C data  in the buffer */
	vpif_calculate_offsets(ch);

	if ((ch->vpifparams.std_info.frm_fmt &&
		((common->fmt.fmt.pix.field != V4L2_FIELD_NONE)
		&& (common->fmt.fmt.pix.field != V4L2_FIELD_ANY)))
		|| (!ch->vpifparams.std_info.frm_fmt
		&& (common->fmt.fmt.pix.field == V4L2_FIELD_NONE))) {
		vpif_err("conflict in field format and std format\n");
		return -EINVAL;
	}

	/* clock settings */
	if (vpif_config_data->set_clock) {
		ret = vpif_config_data->set_clock(ch->vpifparams.std_info.
		ycmux_mode, ch->vpifparams.std_info.hd_sd);
		if (ret < 0) {
			vpif_err("can't set clock\n");
			return ret;
		}
	}

	/* set the parameters and addresses */
	ret = vpif_set_video_params(vpif, ch->channel_id + 2);
	if (ret < 0)
		return ret;

	common->started = ret;
	vpif_config_addr(ch, ret);
	common->set_addr((addr + common->ytop_off),
			    (addr + common->ybtm_off),
			    (addr + common->ctop_off),
			    (addr + common->cbtm_off));

	/* Set interrupt for both the fields in VPIF
	    Register enable channel in VPIF register */
	channel_first_int[VPIF_VIDEO_INDEX][ch->channel_id] = 1;
	if (VPIF_CHANNEL2_VIDEO == ch->channel_id) {
		channel2_intr_assert();
		channel2_intr_enable(1);
		enable_channel2(1);
		if (vpif_config_data->chan_config[VPIF_CHANNEL2_VIDEO].clip_en)
			channel2_clipping_enable(1);
	}

	if ((VPIF_CHANNEL3_VIDEO == ch->channel_id)
		|| (common->started == 2)) {
		channel3_intr_assert();
		channel3_intr_enable(1);
		enable_channel3(1);
		if (vpif_config_data->chan_config[VPIF_CHANNEL3_VIDEO].clip_en)
			channel3_clipping_enable(1);
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

	/* release all active buffers */
	spin_lock_irqsave(&common->irqlock, flags);
	while (!list_empty(&common->dma_queue)) {
		common->next_frm = list_entry(common->dma_queue.next,
						struct vpif_disp_buffer, list);
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

static void process_progressive_mode(struct common_obj *common)
{
	unsigned long addr = 0;

	spin_lock(&common->irqlock);
	/* Get the next buffer from buffer queue */
	common->next_frm = list_entry(common->dma_queue.next,
				struct vpif_disp_buffer, list);
	/* Remove that buffer from the buffer queue */
	list_del(&common->next_frm->list);
	spin_unlock(&common->irqlock);
	/* Mark status of the buffer as active */
	common->next_frm->vb.state = VB2_BUF_STATE_ACTIVE;

	/* Set top and bottom field addrs in VPIF registers */
	addr = vb2_dma_contig_plane_dma_addr(&common->next_frm->vb, 0);
	common->set_addr(addr + common->ytop_off,
				 addr + common->ybtm_off,
				 addr + common->ctop_off,
				 addr + common->cbtm_off);
}

static void process_interlaced_mode(int fid, struct common_obj *common)
{
	/* device field id and local field id are in sync */
	/* If this is even field */
	if (0 == fid) {
		if (common->cur_frm == common->next_frm)
			return;

		/* one frame is displayed If next frame is
		 *  available, release cur_frm and move on */
		/* Copy frame display time */
		v4l2_get_timestamp(&common->cur_frm->vb.v4l2_buf.timestamp);
		/* Change status of the cur_frm */
		vb2_buffer_done(&common->cur_frm->vb,
					    VB2_BUF_STATE_DONE);
		/* Make cur_frm pointing to next_frm */
		common->cur_frm = common->next_frm;

	} else if (1 == fid) {	/* odd field */
		spin_lock(&common->irqlock);
		if (list_empty(&common->dma_queue)
		    || (common->cur_frm != common->next_frm)) {
			spin_unlock(&common->irqlock);
			return;
		}
		spin_unlock(&common->irqlock);
		/* one field is displayed configure the next
		 * frame if it is available else hold on current
		 * frame */
		/* Get next from the buffer queue */
		process_progressive_mode(common);
	}
}

/*
 * vpif_channel_isr: It changes status of the displayed buffer, takes next
 * buffer from the queue and sets its address in VPIF registers
 */
static irqreturn_t vpif_channel_isr(int irq, void *dev_id)
{
	struct vpif_device *dev = &vpif_obj;
	struct channel_obj *ch;
	struct common_obj *common;
	enum v4l2_field field;
	int fid = -1, i;
	int channel_id = 0;

	channel_id = *(int *)(dev_id);
	if (!vpif_intr_status(channel_id + 2))
		return IRQ_NONE;

	ch = dev->dev[channel_id];
	field = ch->common[VPIF_VIDEO_INDEX].fmt.fmt.pix.field;
	for (i = 0; i < VPIF_NUMOBJECTS; i++) {
		common = &ch->common[i];
		/* If streaming is started in this channel */
		if (0 == common->started)
			continue;

		if (1 == ch->vpifparams.std_info.frm_fmt) {
			spin_lock(&common->irqlock);
			if (list_empty(&common->dma_queue)) {
				spin_unlock(&common->irqlock);
				continue;
			}
			spin_unlock(&common->irqlock);

			/* Progressive mode */
			if (!channel_first_int[i][channel_id]) {
				/* Mark status of the cur_frm to
				 * done and unlock semaphore on it */
				v4l2_get_timestamp(&common->cur_frm->vb.
						   v4l2_buf.timestamp);
				vb2_buffer_done(&common->cur_frm->vb,
					    VB2_BUF_STATE_DONE);
				/* Make cur_frm pointing to next_frm */
				common->cur_frm = common->next_frm;
			}

			channel_first_int[i][channel_id] = 0;
			process_progressive_mode(common);
		} else {
			/* Interlaced mode */
			/* If it is first interrupt, ignore it */

			if (channel_first_int[i][channel_id]) {
				channel_first_int[i][channel_id] = 0;
				continue;
			}

			if (0 == i) {
				ch->field_id ^= 1;
				/* Get field id from VPIF registers */
				fid = vpif_channel_getfid(ch->channel_id + 2);
				/* If fid does not match with stored field id */
				if (fid != ch->field_id) {
					/* Make them in sync */
					if (0 == fid)
						ch->field_id = fid;

					return IRQ_HANDLED;
				}
			}
			process_interlaced_mode(fid, common);
		}
	}

	return IRQ_HANDLED;
}

static int vpif_update_std_info(struct channel_obj *ch)
{
	struct video_obj *vid_ch = &ch->video;
	struct vpif_params *vpifparams = &ch->vpifparams;
	struct vpif_channel_config_params *std_info = &vpifparams->std_info;
	const struct vpif_channel_config_params *config;

	int i;

	for (i = 0; i < vpif_ch_params_count; i++) {
		config = &vpif_ch_params[i];
		if (config->hd_sd == 0) {
			vpif_dbg(2, debug, "SD format\n");
			if (config->stdid & vid_ch->stdid) {
				memcpy(std_info, config, sizeof(*config));
				break;
			}
		}
	}

	if (i == vpif_ch_params_count) {
		vpif_dbg(1, debug, "Format not found\n");
		return -EINVAL;
	}

	return 0;
}

static int vpif_update_resolution(struct channel_obj *ch)
{
	struct common_obj *common = &ch->common[VPIF_VIDEO_INDEX];
	struct video_obj *vid_ch = &ch->video;
	struct vpif_params *vpifparams = &ch->vpifparams;
	struct vpif_channel_config_params *std_info = &vpifparams->std_info;

	if (!vid_ch->stdid && !vid_ch->dv_timings.bt.height)
		return -EINVAL;

	if (vid_ch->stdid) {
		if (vpif_update_std_info(ch))
			return -EINVAL;
	}

	common->fmt.fmt.pix.width = std_info->width;
	common->fmt.fmt.pix.height = std_info->height;
	vpif_dbg(1, debug, "Pixel details: Width = %d,Height = %d\n",
			common->fmt.fmt.pix.width, common->fmt.fmt.pix.height);

	/* Set height and width paramateres */
	common->height = std_info->height;
	common->width = std_info->width;

	return 0;
}

/*
 * vpif_calculate_offsets: This function calculates buffers offset for Y and C
 * in the top and bottom field
 */
static void vpif_calculate_offsets(struct channel_obj *ch)
{
	struct common_obj *common = &ch->common[VPIF_VIDEO_INDEX];
	struct vpif_params *vpifparams = &ch->vpifparams;
	enum v4l2_field field = common->fmt.fmt.pix.field;
	struct video_obj *vid_ch = &ch->video;
	unsigned int hpitch, vpitch, sizeimage;

	if (V4L2_FIELD_ANY == common->fmt.fmt.pix.field) {
		if (ch->vpifparams.std_info.frm_fmt)
			vid_ch->buf_field = V4L2_FIELD_NONE;
		else
			vid_ch->buf_field = V4L2_FIELD_INTERLACED;
	} else {
		vid_ch->buf_field = common->fmt.fmt.pix.field;
	}

	sizeimage = common->fmt.fmt.pix.sizeimage;

	hpitch = common->fmt.fmt.pix.bytesperline;
	vpitch = sizeimage / (hpitch * 2);
	if ((V4L2_FIELD_NONE == vid_ch->buf_field) ||
	    (V4L2_FIELD_INTERLACED == vid_ch->buf_field)) {
		common->ytop_off = 0;
		common->ybtm_off = hpitch;
		common->ctop_off = sizeimage / 2;
		common->cbtm_off = sizeimage / 2 + hpitch;
	} else if (V4L2_FIELD_SEQ_TB == vid_ch->buf_field) {
		common->ytop_off = 0;
		common->ybtm_off = sizeimage / 4;
		common->ctop_off = sizeimage / 2;
		common->cbtm_off = common->ctop_off + sizeimage / 4;
	} else if (V4L2_FIELD_SEQ_BT == vid_ch->buf_field) {
		common->ybtm_off = 0;
		common->ytop_off = sizeimage / 4;
		common->cbtm_off = sizeimage / 2;
		common->ctop_off = common->cbtm_off + sizeimage / 4;
	}

	if ((V4L2_FIELD_NONE == vid_ch->buf_field) ||
	    (V4L2_FIELD_INTERLACED == vid_ch->buf_field)) {
		vpifparams->video_params.storage_mode = 1;
	} else {
		vpifparams->video_params.storage_mode = 0;
	}

	if (ch->vpifparams.std_info.frm_fmt == 1) {
		vpifparams->video_params.hpitch =
		    common->fmt.fmt.pix.bytesperline;
	} else {
		if ((field == V4L2_FIELD_ANY) ||
			(field == V4L2_FIELD_INTERLACED))
			vpifparams->video_params.hpitch =
			    common->fmt.fmt.pix.bytesperline * 2;
		else
			vpifparams->video_params.hpitch =
			    common->fmt.fmt.pix.bytesperline;
	}

	ch->vpifparams.video_params.stdid = ch->vpifparams.std_info.stdid;
}

static void vpif_config_format(struct channel_obj *ch)
{
	struct common_obj *common = &ch->common[VPIF_VIDEO_INDEX];

	common->fmt.fmt.pix.field = V4L2_FIELD_ANY;
	if (config_params.numbuffers[ch->channel_id] == 0)
		common->memory = V4L2_MEMORY_USERPTR;
	else
		common->memory = V4L2_MEMORY_MMAP;

	common->fmt.fmt.pix.sizeimage =
			config_params.channel_bufsize[ch->channel_id];
	common->fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUV422P;
	common->fmt.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
}

static int vpif_check_format(struct channel_obj *ch,
			     struct v4l2_pix_format *pixfmt)
{
	struct common_obj *common = &ch->common[VPIF_VIDEO_INDEX];
	enum v4l2_field field = pixfmt->field;
	u32 sizeimage, hpitch, vpitch;

	if (pixfmt->pixelformat != V4L2_PIX_FMT_YUV422P)
		goto invalid_fmt_exit;

	if (!(VPIF_VALID_FIELD(field)))
		goto invalid_fmt_exit;

	if (pixfmt->bytesperline <= 0)
		goto invalid_pitch_exit;

	sizeimage = pixfmt->sizeimage;

	if (vpif_update_resolution(ch))
		return -EINVAL;

	hpitch = pixfmt->bytesperline;
	vpitch = sizeimage / (hpitch * 2);

	/* Check for valid value of pitch */
	if ((hpitch < ch->vpifparams.std_info.width) ||
	    (vpitch < ch->vpifparams.std_info.height))
		goto invalid_pitch_exit;

	/* Check for 8 byte alignment */
	if (!ISALIGNED(hpitch)) {
		vpif_err("invalid pitch alignment\n");
		return -EINVAL;
	}
	pixfmt->width = common->fmt.fmt.pix.width;
	pixfmt->height = common->fmt.fmt.pix.height;

	return 0;

invalid_fmt_exit:
	vpif_err("invalid field format\n");
	return -EINVAL;

invalid_pitch_exit:
	vpif_err("invalid pitch\n");
	return -EINVAL;
}

static void vpif_config_addr(struct channel_obj *ch, int muxmode)
{
	struct common_obj *common = &ch->common[VPIF_VIDEO_INDEX];

	if (VPIF_CHANNEL3_VIDEO == ch->channel_id) {
		common->set_addr = ch3_set_videobuf_addr;
	} else {
		if (2 == muxmode)
			common->set_addr = ch2_set_videobuf_addr_yc_nmux;
		else
			common->set_addr = ch2_set_videobuf_addr;
	}
}

/*
 * vpif_mmap: It is used to map kernel space buffers into user spaces
 */
static int vpif_mmap(struct file *filep, struct vm_area_struct *vma)
{
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

/*
 * vpif_poll: It is used for select/poll system call
 */
static unsigned int vpif_poll(struct file *filep, poll_table *wait)
{
	struct vpif_fh *fh = filep->private_data;
	struct channel_obj *ch = fh->channel;
	struct common_obj *common = &ch->common[VPIF_VIDEO_INDEX];
	unsigned int res = 0;

	if (common->started) {
		mutex_lock(&common->lock);
		res = vb2_poll(&common->buffer_queue, filep, wait);
		mutex_unlock(&common->lock);
	}

	return res;
}

/*
 * vpif_open: It creates object of file handle structure and stores it in
 * private_data member of filepointer
 */
static int vpif_open(struct file *filep)
{
	struct video_device *vdev = video_devdata(filep);
	struct channel_obj *ch = video_get_drvdata(vdev);
	struct common_obj *common = &ch->common[VPIF_VIDEO_INDEX];
	struct vpif_fh *fh;

	/* Allocate memory for the file handle object */
	fh = kzalloc(sizeof(struct vpif_fh), GFP_KERNEL);
	if (fh == NULL) {
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
	if (!ch->initialized) {
		fh->initialized = 1;
		ch->initialized = 1;
		memset(&ch->vpifparams, 0, sizeof(ch->vpifparams));
	}

	/* Increment channel usrs counter */
	atomic_inc(&ch->usrs);
	/* Set io_allowed[VPIF_VIDEO_INDEX] member to false */
	fh->io_allowed[VPIF_VIDEO_INDEX] = 0;
	/* Initialize priority of this instance to default priority */
	fh->prio = V4L2_PRIORITY_UNSET;
	v4l2_prio_open(&ch->prio, &fh->prio);
	mutex_unlock(&common->lock);

	return 0;
}

/*
 * vpif_release: This function deletes buffer queue, frees the buffers and
 * the vpif file handle
 */
static int vpif_release(struct file *filep)
{
	struct vpif_fh *fh = filep->private_data;
	struct channel_obj *ch = fh->channel;
	struct common_obj *common = &ch->common[VPIF_VIDEO_INDEX];

	mutex_lock(&common->lock);
	/* if this instance is doing IO */
	if (fh->io_allowed[VPIF_VIDEO_INDEX]) {
		/* Reset io_usrs member of channel object */
		common->io_usrs = 0;
		/* Disable channel */
		if (VPIF_CHANNEL2_VIDEO == ch->channel_id) {
			enable_channel2(0);
			channel2_intr_enable(0);
		}
		if ((VPIF_CHANNEL3_VIDEO == ch->channel_id) ||
		    (2 == common->started)) {
			enable_channel3(0);
			channel3_intr_enable(0);
		}
		common->started = 0;

		/* Free buffers allocated */
		vb2_queue_release(&common->buffer_queue);
		vb2_dma_contig_cleanup_ctx(common->alloc_ctx);

		common->numbuffers =
		    config_params.numbuffers[ch->channel_id];
	}

	/* Decrement channel usrs counter */
	atomic_dec(&ch->usrs);
	/* If this file handle has initialize encoder device, reset it */
	if (fh->initialized)
		ch->initialized = 0;

	/* Close the priority */
	v4l2_prio_close(&ch->prio, fh->prio);
	filep->private_data = NULL;
	fh->initialized = 0;
	mutex_unlock(&common->lock);
	kfree(fh);

	return 0;
}

/* functions implementing ioctls */
/**
 * vpif_querycap() - QUERYCAP handler
 * @file: file ptr
 * @priv: file handle
 * @cap: ptr to v4l2_capability structure
 */
static int vpif_querycap(struct file *file, void  *priv,
				struct v4l2_capability *cap)
{
	struct vpif_display_config *config = vpif_dev->platform_data;

	cap->device_caps = V4L2_CAP_VIDEO_OUTPUT | V4L2_CAP_STREAMING;
	cap->capabilities = cap->device_caps | V4L2_CAP_DEVICE_CAPS;
	snprintf(cap->driver, sizeof(cap->driver), "%s", dev_name(vpif_dev));
	snprintf(cap->bus_info, sizeof(cap->bus_info), "platform:%s",
		 dev_name(vpif_dev));
	strlcpy(cap->card, config->card_name, sizeof(cap->card));

	return 0;
}

static int vpif_enum_fmt_vid_out(struct file *file, void  *priv,
					struct v4l2_fmtdesc *fmt)
{
	if (fmt->index != 0) {
		vpif_err("Invalid format index\n");
		return -EINVAL;
	}

	/* Fill in the information about format */
	fmt->type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
	strcpy(fmt->description, "YCbCr4:2:2 YC Planar");
	fmt->pixelformat = V4L2_PIX_FMT_YUV422P;

	return 0;
}

static int vpif_g_fmt_vid_out(struct file *file, void *priv,
				struct v4l2_format *fmt)
{
	struct vpif_fh *fh = priv;
	struct channel_obj *ch = fh->channel;
	struct common_obj *common = &ch->common[VPIF_VIDEO_INDEX];

	/* Check the validity of the buffer type */
	if (common->fmt.type != fmt->type)
		return -EINVAL;

	if (vpif_update_resolution(ch))
		return -EINVAL;
	*fmt = common->fmt;
	return 0;
}

static int vpif_s_fmt_vid_out(struct file *file, void *priv,
				struct v4l2_format *fmt)
{
	struct vpif_fh *fh = priv;
	struct v4l2_pix_format *pixfmt;
	struct channel_obj *ch = fh->channel;
	struct common_obj *common = &ch->common[VPIF_VIDEO_INDEX];
	int ret = 0;

	if ((VPIF_CHANNEL2_VIDEO == ch->channel_id)
	    || (VPIF_CHANNEL3_VIDEO == ch->channel_id)) {
		if (!fh->initialized) {
			vpif_dbg(1, debug, "Channel Busy\n");
			return -EBUSY;
		}

		/* Check for the priority */
		ret = v4l2_prio_check(&ch->prio, fh->prio);
		if (0 != ret)
			return ret;
		fh->initialized = 1;
	}

	if (common->started) {
		vpif_dbg(1, debug, "Streaming in progress\n");
		return -EBUSY;
	}

	pixfmt = &fmt->fmt.pix;
	/* Check for valid field format */
	ret = vpif_check_format(ch, pixfmt);
	if (ret)
		return ret;

	/* store the pix format in the channel object */
	common->fmt.fmt.pix = *pixfmt;
	/* store the format in the channel object */
	common->fmt = *fmt;
	return 0;
}

static int vpif_try_fmt_vid_out(struct file *file, void *priv,
				struct v4l2_format *fmt)
{
	struct vpif_fh *fh = priv;
	struct channel_obj *ch = fh->channel;
	struct common_obj *common = &ch->common[VPIF_VIDEO_INDEX];
	struct v4l2_pix_format *pixfmt = &fmt->fmt.pix;
	int ret = 0;

	ret = vpif_check_format(ch, pixfmt);
	if (ret) {
		*pixfmt = common->fmt.fmt.pix;
		pixfmt->sizeimage = pixfmt->width * pixfmt->height * 2;
	}

	return ret;
}

static int vpif_reqbufs(struct file *file, void *priv,
			struct v4l2_requestbuffers *reqbuf)
{
	struct vpif_fh *fh = priv;
	struct channel_obj *ch = fh->channel;
	struct common_obj *common;
	enum v4l2_field field;
	struct vb2_queue *q;
	u8 index = 0;
	int ret;

	/* This file handle has not initialized the channel,
	   It is not allowed to do settings */
	if ((VPIF_CHANNEL2_VIDEO == ch->channel_id)
	    || (VPIF_CHANNEL3_VIDEO == ch->channel_id)) {
		if (!fh->initialized) {
			vpif_err("Channel Busy\n");
			return -EBUSY;
		}
	}

	if (V4L2_BUF_TYPE_VIDEO_OUTPUT != reqbuf->type)
		return -EINVAL;

	index = VPIF_VIDEO_INDEX;

	common = &ch->common[index];

	if (common->fmt.type != reqbuf->type || !vpif_dev)
		return -EINVAL;
	if (0 != common->io_usrs)
		return -EBUSY;

	if (reqbuf->type == V4L2_BUF_TYPE_VIDEO_OUTPUT) {
		if (common->fmt.fmt.pix.field == V4L2_FIELD_ANY)
			field = V4L2_FIELD_INTERLACED;
		else
			field = common->fmt.fmt.pix.field;
	} else {
		field = V4L2_VBI_INTERLACED;
	}
	/* Initialize videobuf2 queue as per the buffer type */
	common->alloc_ctx = vb2_dma_contig_init_ctx(vpif_dev);
	if (IS_ERR(common->alloc_ctx)) {
		vpif_err("Failed to get the context\n");
		return PTR_ERR(common->alloc_ctx);
	}
	q = &common->buffer_queue;
	q->type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
	q->io_modes = VB2_MMAP | VB2_USERPTR;
	q->drv_priv = fh;
	q->ops = &video_qops;
	q->mem_ops = &vb2_dma_contig_memops;
	q->buf_struct_size = sizeof(struct vpif_disp_buffer);
	q->timestamp_type = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;

	ret = vb2_queue_init(q);
	if (ret) {
		vpif_err("vpif_display: vb2_queue_init() failed\n");
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

static int vpif_querybuf(struct file *file, void *priv,
				struct v4l2_buffer *tbuf)
{
	struct vpif_fh *fh = priv;
	struct channel_obj *ch = fh->channel;
	struct common_obj *common = &ch->common[VPIF_VIDEO_INDEX];

	if (common->fmt.type != tbuf->type)
		return -EINVAL;

	return vb2_querybuf(&common->buffer_queue, tbuf);
}

static int vpif_qbuf(struct file *file, void *priv, struct v4l2_buffer *buf)
{
	struct vpif_fh *fh = NULL;
	struct channel_obj *ch = NULL;
	struct common_obj *common = NULL;

	if (!buf || !priv)
		return -EINVAL;

	fh = priv;
	ch = fh->channel;
	if (!ch)
		return -EINVAL;

	common = &(ch->common[VPIF_VIDEO_INDEX]);
	if (common->fmt.type != buf->type)
		return -EINVAL;

	if (!fh->io_allowed[VPIF_VIDEO_INDEX]) {
		vpif_err("fh->io_allowed\n");
		return -EACCES;
	}

	return vb2_qbuf(&common->buffer_queue, buf);
}

static int vpif_s_std(struct file *file, void *priv, v4l2_std_id std_id)
{
	struct vpif_fh *fh = priv;
	struct channel_obj *ch = fh->channel;
	struct common_obj *common = &ch->common[VPIF_VIDEO_INDEX];
	int ret = 0;

	if (!(std_id & VPIF_V4L2_STD))
		return -EINVAL;

	if (common->started) {
		vpif_err("streaming in progress\n");
		return -EBUSY;
	}

	/* Call encoder subdevice function to set the standard */
	ch->video.stdid = std_id;
	memset(&ch->video.dv_timings, 0, sizeof(ch->video.dv_timings));
	/* Get the information about the standard */
	if (vpif_update_resolution(ch))
		return -EINVAL;

	if ((ch->vpifparams.std_info.width *
		ch->vpifparams.std_info.height * 2) >
		config_params.channel_bufsize[ch->channel_id]) {
		vpif_err("invalid std for this size\n");
		return -EINVAL;
	}

	common->fmt.fmt.pix.bytesperline = common->fmt.fmt.pix.width;
	/* Configure the default format information */
	vpif_config_format(ch);

	ret = v4l2_device_call_until_err(&vpif_obj.v4l2_dev, 1, video,
						s_std_output, std_id);
	if (ret < 0) {
		vpif_err("Failed to set output standard\n");
		return ret;
	}

	ret = v4l2_device_call_until_err(&vpif_obj.v4l2_dev, 1, core,
							s_std, std_id);
	if (ret < 0)
		vpif_err("Failed to set standard for sub devices\n");
	return ret;
}

static int vpif_g_std(struct file *file, void *priv, v4l2_std_id *std)
{
	struct vpif_fh *fh = priv;
	struct channel_obj *ch = fh->channel;

	*std = ch->video.stdid;
	return 0;
}

static int vpif_dqbuf(struct file *file, void *priv, struct v4l2_buffer *p)
{
	struct vpif_fh *fh = priv;
	struct channel_obj *ch = fh->channel;
	struct common_obj *common = &ch->common[VPIF_VIDEO_INDEX];

	return vb2_dqbuf(&common->buffer_queue, p,
					(file->f_flags & O_NONBLOCK));
}

static int vpif_streamon(struct file *file, void *priv,
				enum v4l2_buf_type buftype)
{
	struct vpif_fh *fh = priv;
	struct channel_obj *ch = fh->channel;
	struct common_obj *common = &ch->common[VPIF_VIDEO_INDEX];
	struct channel_obj *oth_ch = vpif_obj.dev[!ch->channel_id];
	int ret = 0;

	if (buftype != V4L2_BUF_TYPE_VIDEO_OUTPUT) {
		vpif_err("buffer type not supported\n");
		return -EINVAL;
	}

	if (!fh->io_allowed[VPIF_VIDEO_INDEX]) {
		vpif_err("fh->io_allowed\n");
		return -EACCES;
	}

	/* If Streaming is already started, return error */
	if (common->started) {
		vpif_err("channel->started\n");
		return -EBUSY;
	}

	if ((ch->channel_id == VPIF_CHANNEL2_VIDEO
		&& oth_ch->common[VPIF_VIDEO_INDEX].started &&
		ch->vpifparams.std_info.ycmux_mode == 0)
		|| ((ch->channel_id == VPIF_CHANNEL3_VIDEO)
		&& (2 == oth_ch->common[VPIF_VIDEO_INDEX].started))) {
		vpif_err("other channel is using\n");
		return -EBUSY;
	}

	ret = vpif_check_format(ch, &common->fmt.fmt.pix);
	if (ret < 0)
		return ret;

	/* Call vb2_streamon to start streaming in videobuf2 */
	ret = vb2_streamon(&common->buffer_queue, buftype);
	if (ret < 0) {
		vpif_err("vb2_streamon\n");
		return ret;
	}

	return ret;
}

static int vpif_streamoff(struct file *file, void *priv,
				enum v4l2_buf_type buftype)
{
	struct vpif_fh *fh = priv;
	struct channel_obj *ch = fh->channel;
	struct common_obj *common = &ch->common[VPIF_VIDEO_INDEX];
	struct vpif_display_config *vpif_config_data =
					vpif_dev->platform_data;

	if (buftype != V4L2_BUF_TYPE_VIDEO_OUTPUT) {
		vpif_err("buffer type not supported\n");
		return -EINVAL;
	}

	if (!fh->io_allowed[VPIF_VIDEO_INDEX]) {
		vpif_err("fh->io_allowed\n");
		return -EACCES;
	}

	if (!common->started) {
		vpif_err("channel->started\n");
		return -EINVAL;
	}

	if (buftype == V4L2_BUF_TYPE_VIDEO_OUTPUT) {
		/* disable channel */
		if (VPIF_CHANNEL2_VIDEO == ch->channel_id) {
			if (vpif_config_data->
				chan_config[VPIF_CHANNEL2_VIDEO].clip_en)
				channel2_clipping_enable(0);
			enable_channel2(0);
			channel2_intr_enable(0);
		}
		if ((VPIF_CHANNEL3_VIDEO == ch->channel_id) ||
					(2 == common->started)) {
			if (vpif_config_data->
				chan_config[VPIF_CHANNEL3_VIDEO].clip_en)
				channel3_clipping_enable(0);
			enable_channel3(0);
			channel3_intr_enable(0);
		}
	}

	common->started = 0;
	return vb2_streamoff(&common->buffer_queue, buftype);
}

static int vpif_cropcap(struct file *file, void *priv,
			struct v4l2_cropcap *crop)
{
	struct vpif_fh *fh = priv;
	struct channel_obj *ch = fh->channel;
	struct common_obj *common = &ch->common[VPIF_VIDEO_INDEX];
	if (V4L2_BUF_TYPE_VIDEO_OUTPUT != crop->type)
		return -EINVAL;

	crop->bounds.left = crop->bounds.top = 0;
	crop->defrect.left = crop->defrect.top = 0;
	crop->defrect.height = crop->bounds.height = common->height;
	crop->defrect.width = crop->bounds.width = common->width;

	return 0;
}

static int vpif_enum_output(struct file *file, void *fh,
				struct v4l2_output *output)
{

	struct vpif_display_config *config = vpif_dev->platform_data;
	struct vpif_display_chan_config *chan_cfg;
	struct vpif_fh *vpif_handler = fh;
	struct channel_obj *ch = vpif_handler->channel;

	chan_cfg = &config->chan_config[ch->channel_id];
	if (output->index >= chan_cfg->output_count) {
		vpif_dbg(1, debug, "Invalid output index\n");
		return -EINVAL;
	}

	*output = chan_cfg->outputs[output->index].output;
	return 0;
}

/**
 * vpif_output_to_subdev() - Maps output to sub device
 * @vpif_cfg - global config ptr
 * @chan_cfg - channel config ptr
 * @index - Given output index from application
 *
 * lookup the sub device information for a given output index.
 * we report all the output to application. output table also
 * has sub device name for the each output
 */
static int
vpif_output_to_subdev(struct vpif_display_config *vpif_cfg,
		      struct vpif_display_chan_config *chan_cfg, int index)
{
	struct vpif_subdev_info *subdev_info;
	const char *subdev_name;
	int i;

	vpif_dbg(2, debug, "vpif_output_to_subdev\n");

	if (chan_cfg->outputs == NULL)
		return -1;

	subdev_name = chan_cfg->outputs[index].subdev_name;
	if (subdev_name == NULL)
		return -1;

	/* loop through the sub device list to get the sub device info */
	for (i = 0; i < vpif_cfg->subdev_count; i++) {
		subdev_info = &vpif_cfg->subdevinfo[i];
		if (!strcmp(subdev_info->name, subdev_name))
			return i;
	}
	return -1;
}

/**
 * vpif_set_output() - Select an output
 * @vpif_cfg - global config ptr
 * @ch - channel
 * @index - Given output index from application
 *
 * Select the given output.
 */
static int vpif_set_output(struct vpif_display_config *vpif_cfg,
		      struct channel_obj *ch, int index)
{
	struct vpif_display_chan_config *chan_cfg =
		&vpif_cfg->chan_config[ch->channel_id];
	struct vpif_subdev_info *subdev_info = NULL;
	struct v4l2_subdev *sd = NULL;
	u32 input = 0, output = 0;
	int sd_index;
	int ret;

	sd_index = vpif_output_to_subdev(vpif_cfg, chan_cfg, index);
	if (sd_index >= 0) {
		sd = vpif_obj.sd[sd_index];
		subdev_info = &vpif_cfg->subdevinfo[sd_index];
	}

	if (sd) {
		input = chan_cfg->outputs[index].input_route;
		output = chan_cfg->outputs[index].output_route;
		ret = v4l2_subdev_call(sd, video, s_routing, input, output, 0);
		if (ret < 0 && ret != -ENOIOCTLCMD) {
			vpif_err("Failed to set output\n");
			return ret;
		}

	}
	ch->output_idx = index;
	ch->sd = sd;
	if (chan_cfg->outputs != NULL)
		/* update tvnorms from the sub device output info */
		ch->video_dev->tvnorms = chan_cfg->outputs[index].output.std;
	return 0;
}

static int vpif_s_output(struct file *file, void *priv, unsigned int i)
{
	struct vpif_display_config *config = vpif_dev->platform_data;
	struct vpif_display_chan_config *chan_cfg;
	struct vpif_fh *fh = priv;
	struct channel_obj *ch = fh->channel;
	struct common_obj *common = &ch->common[VPIF_VIDEO_INDEX];

	chan_cfg = &config->chan_config[ch->channel_id];

	if (i >= chan_cfg->output_count)
		return -EINVAL;

	if (common->started) {
		vpif_err("Streaming in progress\n");
		return -EBUSY;
	}

	return vpif_set_output(config, ch, i);
}

static int vpif_g_output(struct file *file, void *priv, unsigned int *i)
{
	struct vpif_fh *fh = priv;
	struct channel_obj *ch = fh->channel;

	*i = ch->output_idx;

	return 0;
}

static int vpif_g_priority(struct file *file, void *priv, enum v4l2_priority *p)
{
	struct vpif_fh *fh = priv;
	struct channel_obj *ch = fh->channel;

	*p = v4l2_prio_max(&ch->prio);

	return 0;
}

static int vpif_s_priority(struct file *file, void *priv, enum v4l2_priority p)
{
	struct vpif_fh *fh = priv;
	struct channel_obj *ch = fh->channel;

	return v4l2_prio_change(&ch->prio, &fh->prio, p);
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

	std_info->eav2sav = bt->hbackporch + bt->hfrontporch +
		bt->hsync - 8;
	std_info->sav2eav = bt->width;

	std_info->l1 = 1;
	std_info->l3 = bt->vsync + bt->vbackporch + 1;

	if (bt->interlaced) {
		if (bt->il_vbackporch || bt->il_vfrontporch || bt->il_vsync) {
			std_info->vsize = bt->height * 2 +
				bt->vfrontporch + bt->vsync + bt->vbackporch +
				bt->il_vfrontporch + bt->il_vsync +
				bt->il_vbackporch;
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
		std_info->vsize = bt->height + bt->vfrontporch +
			bt->vsync + bt->vbackporch;
		std_info->l5 = std_info->vsize - (bt->vfrontporch - 1);
	}
	strncpy(std_info->name, "Custom timings BT656/1120",
			VPIF_MAX_NAME);
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
 * vpif_g_chip_ident() - Identify the chip
 * @file: file ptr
 * @priv: file handle
 * @chip: chip identity
 *
 * Returns zero or -EINVAL if read operations fails.
 */
static int vpif_g_chip_ident(struct file *file, void *priv,
		struct v4l2_dbg_chip_ident *chip)
{
	chip->ident = V4L2_IDENT_NONE;
	chip->revision = 0;
	if (chip->match.type != V4L2_CHIP_MATCH_I2C_DRIVER &&
			chip->match.type != V4L2_CHIP_MATCH_I2C_ADDR) {
		vpif_dbg(2, debug, "match_type is invalid.\n");
		return -EINVAL;
	}

	return v4l2_device_call_until_err(&vpif_obj.v4l2_dev, 0, core,
			g_chip_ident, chip);
}

#ifdef CONFIG_VIDEO_ADV_DEBUG
/*
 * vpif_dbg_g_register() - Read register
 * @file: file ptr
 * @priv: file handle
 * @reg: register to be read
 *
 * Debugging only
 * Returns zero or -EINVAL if read operations fails.
 */
static int vpif_dbg_g_register(struct file *file, void *priv,
		struct v4l2_dbg_register *reg){
	struct vpif_fh *fh = priv;
	struct channel_obj *ch = fh->channel;

	return v4l2_subdev_call(ch->sd, core, g_register, reg);
}

/*
 * vpif_dbg_s_register() - Write to register
 * @file: file ptr
 * @priv: file handle
 * @reg: register to be modified
 *
 * Debugging only
 * Returns zero or -EINVAL if write operations fails.
 */
static int vpif_dbg_s_register(struct file *file, void *priv,
		const struct v4l2_dbg_register *reg)
{
	struct vpif_fh *fh = priv;
	struct channel_obj *ch = fh->channel;

	return v4l2_subdev_call(ch->sd, core, s_register, reg);
}
#endif

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

/* vpif display ioctl operations */
static const struct v4l2_ioctl_ops vpif_ioctl_ops = {
	.vidioc_querycap        	= vpif_querycap,
	.vidioc_g_priority		= vpif_g_priority,
	.vidioc_s_priority		= vpif_s_priority,
	.vidioc_enum_fmt_vid_out	= vpif_enum_fmt_vid_out,
	.vidioc_g_fmt_vid_out  		= vpif_g_fmt_vid_out,
	.vidioc_s_fmt_vid_out   	= vpif_s_fmt_vid_out,
	.vidioc_try_fmt_vid_out 	= vpif_try_fmt_vid_out,
	.vidioc_reqbufs         	= vpif_reqbufs,
	.vidioc_querybuf        	= vpif_querybuf,
	.vidioc_qbuf            	= vpif_qbuf,
	.vidioc_dqbuf           	= vpif_dqbuf,
	.vidioc_streamon        	= vpif_streamon,
	.vidioc_streamoff       	= vpif_streamoff,
	.vidioc_s_std           	= vpif_s_std,
	.vidioc_g_std			= vpif_g_std,
	.vidioc_enum_output		= vpif_enum_output,
	.vidioc_s_output		= vpif_s_output,
	.vidioc_g_output		= vpif_g_output,
	.vidioc_cropcap         	= vpif_cropcap,
	.vidioc_enum_dv_timings         = vpif_enum_dv_timings,
	.vidioc_s_dv_timings            = vpif_s_dv_timings,
	.vidioc_g_dv_timings            = vpif_g_dv_timings,
	.vidioc_g_chip_ident		= vpif_g_chip_ident,
#ifdef CONFIG_VIDEO_ADV_DEBUG
	.vidioc_g_register		= vpif_dbg_g_register,
	.vidioc_s_register		= vpif_dbg_s_register,
#endif
	.vidioc_log_status		= vpif_log_status,
};

static const struct v4l2_file_operations vpif_fops = {
	.owner		= THIS_MODULE,
	.open		= vpif_open,
	.release	= vpif_release,
	.unlocked_ioctl	= video_ioctl2,
	.mmap		= vpif_mmap,
	.poll		= vpif_poll
};

static struct video_device vpif_video_template = {
	.name		= "vpif",
	.fops		= &vpif_fops,
	.ioctl_ops	= &vpif_ioctl_ops,
};

/*Configure the channels, buffer sizei, request irq */
static int initialize_vpif(void)
{
	int free_channel_objects_index;
	int free_buffer_channel_index;
	int free_buffer_index;
	int err = 0, i, j;

	/* Default number of buffers should be 3 */
	if ((ch2_numbuffers > 0) &&
	    (ch2_numbuffers < config_params.min_numbuffers))
		ch2_numbuffers = config_params.min_numbuffers;
	if ((ch3_numbuffers > 0) &&
	    (ch3_numbuffers < config_params.min_numbuffers))
		ch3_numbuffers = config_params.min_numbuffers;

	/* Set buffer size to min buffers size if invalid buffer size is
	 * given */
	if (ch2_bufsize < config_params.min_bufsize[VPIF_CHANNEL2_VIDEO])
		ch2_bufsize =
		    config_params.min_bufsize[VPIF_CHANNEL2_VIDEO];
	if (ch3_bufsize < config_params.min_bufsize[VPIF_CHANNEL3_VIDEO])
		ch3_bufsize =
		    config_params.min_bufsize[VPIF_CHANNEL3_VIDEO];

	config_params.numbuffers[VPIF_CHANNEL2_VIDEO] = ch2_numbuffers;

	if (ch2_numbuffers) {
		config_params.channel_bufsize[VPIF_CHANNEL2_VIDEO] =
							ch2_bufsize;
	}
	config_params.numbuffers[VPIF_CHANNEL3_VIDEO] = ch3_numbuffers;

	if (ch3_numbuffers) {
		config_params.channel_bufsize[VPIF_CHANNEL3_VIDEO] =
							ch3_bufsize;
	}

	/* Allocate memory for six channel objects */
	for (i = 0; i < VPIF_DISPLAY_MAX_DEVICES; i++) {
		vpif_obj.dev[i] =
		    kzalloc(sizeof(struct channel_obj), GFP_KERNEL);
		/* If memory allocation fails, return error */
		if (!vpif_obj.dev[i]) {
			free_channel_objects_index = i;
			err = -ENOMEM;
			goto vpif_init_free_channel_objects;
		}
	}

	free_channel_objects_index = VPIF_DISPLAY_MAX_DEVICES;
	free_buffer_channel_index = VPIF_DISPLAY_NUM_CHANNELS;
	free_buffer_index = config_params.numbuffers[i - 1];

	return 0;

vpif_init_free_channel_objects:
	for (j = 0; j < free_channel_objects_index; j++)
		kfree(vpif_obj.dev[j]);
	return err;
}

/*
 * vpif_probe: This function creates device entries by register itself to the
 * V4L2 driver and initializes fields of each channel objects
 */
static __init int vpif_probe(struct platform_device *pdev)
{
	struct vpif_subdev_info *subdevdata;
	struct vpif_display_config *config;
	int i, j = 0, k, err = 0;
	int res_idx = 0;
	struct i2c_adapter *i2c_adap;
	struct common_obj *common;
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
		for (i = res->start; i <= res->end; i++) {
			if (request_irq(i, vpif_channel_isr, IRQF_SHARED,
					"VPIF_Display", (void *)
					(&vpif_obj.dev[res_idx]->channel_id))) {
				err = -EBUSY;
				for (j = 0; j < i; j++)
					free_irq(j, (void *)
					(&vpif_obj.dev[res_idx]->channel_id));
				goto vpif_int_err;
			}
		}
		res_idx++;
	}

	for (i = 0; i < VPIF_DISPLAY_MAX_DEVICES; i++) {
		/* Get the pointer to the channel object */
		ch = vpif_obj.dev[i];

		/* Allocate memory for video device */
		vfd = video_device_alloc();
		if (vfd == NULL) {
			for (j = 0; j < i; j++) {
				ch = vpif_obj.dev[j];
				video_device_release(ch->video_dev);
			}
			err = -ENOMEM;
			goto vpif_int_err;
		}

		/* Initialize field of video device */
		*vfd = vpif_video_template;
		vfd->v4l2_dev = &vpif_obj.v4l2_dev;
		vfd->release = video_device_release;
		vfd->vfl_dir = VFL_DIR_TX;
		snprintf(vfd->name, sizeof(vfd->name),
			 "VPIF_Display_DRIVER_V%s",
			 VPIF_DISPLAY_VERSION);

		/* Set video_dev to the video device */
		ch->video_dev = vfd;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (res) {
		size = resource_size(res);
		/* The resources are divided into two equal memory and when
		 * we have HD output we can add them together
		 */
		for (j = 0; j < VPIF_DISPLAY_MAX_DEVICES; j++) {
			ch = vpif_obj.dev[j];
			ch->channel_id = j;

			/* only enabled if second resource exists */
			config_params.video_limit[ch->channel_id] = 0;
			if (size)
				config_params.video_limit[ch->channel_id] =
									size/2;
		}
	}

	i2c_adap = i2c_get_adapter(1);
	config = pdev->dev.platform_data;
	subdev_count = config->subdev_count;
	subdevdata = config->subdevinfo;
	vpif_obj.sd = kzalloc(sizeof(struct v4l2_subdev *) * subdev_count,
								GFP_KERNEL);
	if (vpif_obj.sd == NULL) {
		vpif_err("unable to allocate memory for subdevice pointers\n");
		err = -ENOMEM;
		goto vpif_sd_error;
	}

	for (i = 0; i < subdev_count; i++) {
		vpif_obj.sd[i] = v4l2_i2c_new_subdev_board(&vpif_obj.v4l2_dev,
						i2c_adap,
						&subdevdata[i].board_info,
						NULL);
		if (!vpif_obj.sd[i]) {
			vpif_err("Error registering v4l2 subdevice\n");
			goto probe_subdev_out;
		}

		if (vpif_obj.sd[i])
			vpif_obj.sd[i]->grp_id = 1 << i;
	}

	for (j = 0; j < VPIF_DISPLAY_MAX_DEVICES; j++) {
		ch = vpif_obj.dev[j];
		/* Initialize field of the channel objects */
		atomic_set(&ch->usrs, 0);
		for (k = 0; k < VPIF_NUMOBJECTS; k++) {
			ch->common[k].numbuffers = 0;
			common = &ch->common[k];
			common->io_usrs = 0;
			common->started = 0;
			spin_lock_init(&common->irqlock);
			mutex_init(&common->lock);
			common->numbuffers = 0;
			common->set_addr = NULL;
			common->ytop_off = common->ybtm_off = 0;
			common->ctop_off = common->cbtm_off = 0;
			common->cur_frm = common->next_frm = NULL;
			memset(&common->fmt, 0, sizeof(common->fmt));
			common->numbuffers = config_params.numbuffers[k];

		}
		ch->initialized = 0;
		if (subdev_count)
			ch->sd = vpif_obj.sd[0];
		ch->channel_id = j;
		if (j < 2)
			ch->common[VPIF_VIDEO_INDEX].numbuffers =
			    config_params.numbuffers[ch->channel_id];
		else
			ch->common[VPIF_VIDEO_INDEX].numbuffers = 0;

		memset(&ch->vpifparams, 0, sizeof(ch->vpifparams));

		/* Initialize prio member of channel object */
		v4l2_prio_init(&ch->prio);
		ch->common[VPIF_VIDEO_INDEX].fmt.type =
						V4L2_BUF_TYPE_VIDEO_OUTPUT;
		ch->video_dev->lock = &common->lock;
		video_set_drvdata(ch->video_dev, ch);

		/* select output 0 */
		err = vpif_set_output(config, ch, 0);
		if (err)
			goto probe_out;

		/* register video device */
		vpif_dbg(1, debug, "channel=%x,channel->video_dev=%x\n",
				(int)ch, (int)&ch->video_dev);

		err = video_register_device(ch->video_dev,
					  VFL_TYPE_GRABBER, (j ? 3 : 2));
		if (err < 0)
			goto probe_out;
	}

	v4l2_info(&vpif_obj.v4l2_dev,
			" VPIF display driver initialized\n");
	return 0;

probe_out:
	for (k = 0; k < j; k++) {
		ch = vpif_obj.dev[k];
		video_unregister_device(ch->video_dev);
		video_device_release(ch->video_dev);
		ch->video_dev = NULL;
	}
probe_subdev_out:
	kfree(vpif_obj.sd);
vpif_sd_error:
	for (i = 0; i < VPIF_DISPLAY_MAX_DEVICES; i++) {
		ch = vpif_obj.dev[i];
		/* Note: does nothing if ch->video_dev == NULL */
		video_device_release(ch->video_dev);
	}
vpif_int_err:
	v4l2_device_unregister(&vpif_obj.v4l2_dev);
	vpif_err("VPIF IRQ request failed\n");
	for (i = 0; i < res_idx; i++) {
		res = platform_get_resource(pdev, IORESOURCE_IRQ, i);
		for (j = res->start; j <= res->end; j++)
			free_irq(j, (void *)(&vpif_obj.dev[i]->channel_id));
	}

	return err;
}

/*
 * vpif_remove: It un-register channels from V4L2 driver
 */
static int vpif_remove(struct platform_device *device)
{
	struct channel_obj *ch;
	int i;

	v4l2_device_unregister(&vpif_obj.v4l2_dev);

	/* un-register device */
	for (i = 0; i < VPIF_DISPLAY_MAX_DEVICES; i++) {
		/* Get the pointer to the channel object */
		ch = vpif_obj.dev[i];
		/* Unregister video device */
		video_unregister_device(ch->video_dev);

		ch->video_dev = NULL;
	}

	return 0;
}

#ifdef CONFIG_PM
static int vpif_suspend(struct device *dev)
{
	struct common_obj *common;
	struct channel_obj *ch;
	int i;

	for (i = 0; i < VPIF_DISPLAY_MAX_DEVICES; i++) {
		/* Get the pointer to the channel object */
		ch = vpif_obj.dev[i];
		common = &ch->common[VPIF_VIDEO_INDEX];
		mutex_lock(&common->lock);
		if (atomic_read(&ch->usrs) && common->io_usrs) {
			/* Disable channel */
			if (ch->channel_id == VPIF_CHANNEL2_VIDEO) {
				enable_channel2(0);
				channel2_intr_enable(0);
			}
			if (ch->channel_id == VPIF_CHANNEL3_VIDEO ||
					common->started == 2) {
				enable_channel3(0);
				channel3_intr_enable(0);
			}
		}
		mutex_unlock(&common->lock);
	}

	return 0;
}

static int vpif_resume(struct device *dev)
{

	struct common_obj *common;
	struct channel_obj *ch;
	int i;

	for (i = 0; i < VPIF_DISPLAY_MAX_DEVICES; i++) {
		/* Get the pointer to the channel object */
		ch = vpif_obj.dev[i];
		common = &ch->common[VPIF_VIDEO_INDEX];
		mutex_lock(&common->lock);
		if (atomic_read(&ch->usrs) && common->io_usrs) {
			/* Enable channel */
			if (ch->channel_id == VPIF_CHANNEL2_VIDEO) {
				enable_channel2(1);
				channel2_intr_enable(1);
			}
			if (ch->channel_id == VPIF_CHANNEL3_VIDEO ||
					common->started == 2) {
				enable_channel3(1);
				channel3_intr_enable(1);
			}
		}
		mutex_unlock(&common->lock);
	}

	return 0;
}

static const struct dev_pm_ops vpif_pm = {
	.suspend        = vpif_suspend,
	.resume         = vpif_resume,
};

#define vpif_pm_ops (&vpif_pm)
#else
#define vpif_pm_ops NULL
#endif

static __refdata struct platform_driver vpif_driver = {
	.driver	= {
			.name	= "vpif_display",
			.owner	= THIS_MODULE,
			.pm	= vpif_pm_ops,
	},
	.probe	= vpif_probe,
	.remove	= vpif_remove,
};

static __init int vpif_init(void)
{
	return platform_driver_register(&vpif_driver);
}

/*
 * vpif_cleanup: This function un-registers device and driver to the kernel,
 * frees requested irq handler and de-allocates memory allocated for channel
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
	for (i = 0; i < VPIF_DISPLAY_MAX_DEVICES; i++)
		kfree(vpif_obj.dev[i]);
}

module_init(vpif_init);
module_exit(vpif_cleanup);
