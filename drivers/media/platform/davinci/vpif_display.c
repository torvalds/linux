/*
 * vpif-display - VPIF display driver
 * Display driver for TI DaVinci VPIF
 *
 * Copyright (C) 2009 Texas Instruments Incorporated - http://www.ti.com/
 * Copyright (C) 2014 Lad, Prabhakar <prabhakar.csengg@gmail.com>
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

#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include <media/v4l2-ioctl.h>

#include "vpif.h"
#include "vpif_display.h"

MODULE_DESCRIPTION("TI DaVinci VPIF Display driver");
MODULE_LICENSE("GPL");
MODULE_VERSION(VPIF_DISPLAY_VERSION);

#define VPIF_V4L2_STD (V4L2_STD_525_60 | V4L2_STD_625_50)

#define vpif_err(fmt, arg...)	v4l2_err(&vpif_obj.v4l2_dev, fmt, ## arg)
#define vpif_dbg(level, debug, fmt, arg...)	\
		v4l2_dbg(level, debug, &vpif_obj.v4l2_dev, fmt, ## arg)

static int debug = 1;

module_param(debug, int, 0644);

MODULE_PARM_DESC(debug, "Debug level 0-1");

#define VPIF_DRIVER_NAME	"vpif_display"

/* Is set to 1 in case of SDTV formats, 2 in case of HDTV formats. */
static int ycmux_mode;

static u8 channel_first_int[VPIF_NUMOBJECTS][2] = { {1, 1} };

static struct vpif_device vpif_obj = { {NULL} };
static struct device *vpif_dev;
static void vpif_calculate_offsets(struct channel_obj *ch);
static void vpif_config_addr(struct channel_obj *ch, int muxmode);

static inline struct vpif_disp_buffer *to_vpif_buffer(struct vb2_buffer *vb)
{
	return container_of(vb, struct vpif_disp_buffer, vb);
}

/**
 * vpif_buffer_prepare :  callback function for buffer prepare
 * @vb: ptr to vb2_buffer
 *
 * This is the callback function for buffer prepare when vb2_qbuf()
 * function is called. The buffer is prepared and user space virtual address
 * or user address is converted into  physical address
 */
static int vpif_buffer_prepare(struct vb2_buffer *vb)
{
	struct channel_obj *ch = vb2_get_drv_priv(vb->vb2_queue);
	struct common_obj *common;

	common = &ch->common[VPIF_VIDEO_INDEX];

	vb2_set_plane_payload(vb, 0, common->fmt.fmt.pix.sizeimage);
	if (vb2_get_plane_payload(vb, 0) > vb2_plane_size(vb, 0))
		return -EINVAL;

	vb->v4l2_buf.field = common->fmt.fmt.pix.field;

	if (vb->vb2_queue->type != V4L2_BUF_TYPE_SLICED_VBI_OUTPUT) {
		unsigned long addr = vb2_dma_contig_plane_dma_addr(vb, 0);

		if (!ISALIGNED(addr + common->ytop_off) ||
			!ISALIGNED(addr + common->ybtm_off) ||
			!ISALIGNED(addr + common->ctop_off) ||
			!ISALIGNED(addr + common->cbtm_off)) {
			vpif_err("buffer offset not aligned to 8 bytes\n");
			return -EINVAL;
		}
	}

	return 0;
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
	struct channel_obj *ch = vb2_get_drv_priv(vq);
	struct common_obj *common = &ch->common[VPIF_VIDEO_INDEX];

	if (fmt && fmt->fmt.pix.sizeimage < common->fmt.fmt.pix.sizeimage)
		return -EINVAL;

	if (vq->num_buffers + *nbuffers < 3)
		*nbuffers = 3 - vq->num_buffers;

	*nplanes = 1;
	sizes[0] = fmt ? fmt->fmt.pix.sizeimage : common->fmt.fmt.pix.sizeimage;
	alloc_ctxs[0] = common->alloc_ctx;

	/* Calculate the offset for Y and C data  in the buffer */
	vpif_calculate_offsets(ch);

	return 0;
}

/**
 * vpif_buffer_queue : Callback function to add buffer to DMA queue
 * @vb: ptr to vb2_buffer
 *
 * This callback fucntion queues the buffer to DMA engine
 */
static void vpif_buffer_queue(struct vb2_buffer *vb)
{
	struct vpif_disp_buffer *buf = to_vpif_buffer(vb);
	struct channel_obj *ch = vb2_get_drv_priv(vb->vb2_queue);
	struct common_obj *common;
	unsigned long flags;

	common = &ch->common[VPIF_VIDEO_INDEX];

	/* add the buffer to the DMA queue */
	spin_lock_irqsave(&common->irqlock, flags);
	list_add_tail(&buf->list, &common->dma_queue);
	spin_unlock_irqrestore(&common->irqlock, flags);
}

/**
 * vpif_start_streaming : Starts the DMA engine for streaming
 * @vb: ptr to vb2_buffer
 * @count: number of buffers
 */
static int vpif_start_streaming(struct vb2_queue *vq, unsigned int count)
{
	struct vpif_display_config *vpif_config_data =
					vpif_dev->platform_data;
	struct channel_obj *ch = vb2_get_drv_priv(vq);
	struct common_obj *common = &ch->common[VPIF_VIDEO_INDEX];
	struct vpif_params *vpif = &ch->vpifparams;
	struct vpif_disp_buffer *buf, *tmp;
	unsigned long addr, flags;
	int ret;

	spin_lock_irqsave(&common->irqlock, flags);

	/* Initialize field_id */
	ch->field_id = 0;

	/* clock settings */
	if (vpif_config_data->set_clock) {
		ret = vpif_config_data->set_clock(ch->vpifparams.std_info.
		ycmux_mode, ch->vpifparams.std_info.hd_sd);
		if (ret < 0) {
			vpif_err("can't set clock\n");
			goto err;
		}
	}

	/* set the parameters and addresses */
	ret = vpif_set_video_params(vpif, ch->channel_id + 2);
	if (ret < 0)
		goto err;

	ycmux_mode = ret;
	vpif_config_addr(ch, ret);
	/* Get the next frame from the buffer queue */
	common->next_frm = common->cur_frm =
			    list_entry(common->dma_queue.next,
				       struct vpif_disp_buffer, list);

	list_del(&common->cur_frm->list);
	spin_unlock_irqrestore(&common->irqlock, flags);
	/* Mark state of the current frame to active */
	common->cur_frm->vb.state = VB2_BUF_STATE_ACTIVE;

	addr = vb2_dma_contig_plane_dma_addr(&common->cur_frm->vb, 0);
	common->set_addr((addr + common->ytop_off),
			    (addr + common->ybtm_off),
			    (addr + common->ctop_off),
			    (addr + common->cbtm_off));

	/*
	 * Set interrupt for both the fields in VPIF
	 * Register enable channel in VPIF register
	 */
	channel_first_int[VPIF_VIDEO_INDEX][ch->channel_id] = 1;
	if (VPIF_CHANNEL2_VIDEO == ch->channel_id) {
		channel2_intr_assert();
		channel2_intr_enable(1);
		enable_channel2(1);
		if (vpif_config_data->chan_config[VPIF_CHANNEL2_VIDEO].clip_en)
			channel2_clipping_enable(1);
	}

	if (VPIF_CHANNEL3_VIDEO == ch->channel_id || ycmux_mode == 2) {
		channel3_intr_assert();
		channel3_intr_enable(1);
		enable_channel3(1);
		if (vpif_config_data->chan_config[VPIF_CHANNEL3_VIDEO].clip_en)
			channel3_clipping_enable(1);
	}

	return 0;

err:
	list_for_each_entry_safe(buf, tmp, &common->dma_queue, list) {
		list_del(&buf->list);
		vb2_buffer_done(&buf->vb, VB2_BUF_STATE_QUEUED);
	}
	spin_unlock_irqrestore(&common->irqlock, flags);

	return ret;
}

/**
 * vpif_stop_streaming : Stop the DMA engine
 * @vq: ptr to vb2_queue
 *
 * This callback stops the DMA engine and any remaining buffers
 * in the DMA queue are released.
 */
static void vpif_stop_streaming(struct vb2_queue *vq)
{
	struct channel_obj *ch = vb2_get_drv_priv(vq);
	struct common_obj *common;
	unsigned long flags;

	common = &ch->common[VPIF_VIDEO_INDEX];

	/* Disable channel */
	if (VPIF_CHANNEL2_VIDEO == ch->channel_id) {
		enable_channel2(0);
		channel2_intr_enable(0);
	}
	if (VPIF_CHANNEL3_VIDEO == ch->channel_id || ycmux_mode == 2) {
		enable_channel3(0);
		channel3_intr_enable(0);
	}

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
						struct vpif_disp_buffer, list);
		list_del(&common->next_frm->list);
		vb2_buffer_done(&common->next_frm->vb, VB2_BUF_STATE_ERROR);
	}
	spin_unlock_irqrestore(&common->irqlock, flags);
}

static struct vb2_ops video_qops = {
	.queue_setup		= vpif_buffer_queue_setup,
	.wait_prepare		= vb2_ops_wait_prepare,
	.wait_finish		= vb2_ops_wait_finish,
	.buf_prepare		= vpif_buffer_prepare,
	.start_streaming	= vpif_start_streaming,
	.stop_streaming		= vpif_stop_streaming,
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

	common->fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUV422P;
	common->fmt.fmt.pix.width = std_info->width;
	common->fmt.fmt.pix.height = std_info->height;
	vpif_dbg(1, debug, "Pixel details: Width = %d,Height = %d\n",
			common->fmt.fmt.pix.width, common->fmt.fmt.pix.height);

	/* Set height and width paramateres */
	common->height = std_info->height;
	common->width = std_info->width;
	common->fmt.fmt.pix.sizeimage = common->height * common->width * 2;

	if (vid_ch->stdid)
		common->fmt.fmt.pix.colorspace = V4L2_COLORSPACE_SMPTE170M;
	else
		common->fmt.fmt.pix.colorspace = V4L2_COLORSPACE_REC709;

	if (ch->vpifparams.std_info.frm_fmt)
		common->fmt.fmt.pix.field = V4L2_FIELD_NONE;
	else
		common->fmt.fmt.pix.field = V4L2_FIELD_INTERLACED;

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
	strlcpy(cap->driver, VPIF_DRIVER_NAME, sizeof(cap->driver));
	snprintf(cap->bus_info, sizeof(cap->bus_info), "platform:%s",
		 dev_name(vpif_dev));
	strlcpy(cap->card, config->card_name, sizeof(cap->card));

	return 0;
}

static int vpif_enum_fmt_vid_out(struct file *file, void  *priv,
					struct v4l2_fmtdesc *fmt)
{
	if (fmt->index != 0)
		return -EINVAL;

	/* Fill in the information about format */
	fmt->type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
	strcpy(fmt->description, "YCbCr4:2:2 YC Planar");
	fmt->pixelformat = V4L2_PIX_FMT_YUV422P;
	fmt->flags = 0;
	return 0;
}

static int vpif_g_fmt_vid_out(struct file *file, void *priv,
				struct v4l2_format *fmt)
{
	struct video_device *vdev = video_devdata(file);
	struct channel_obj *ch = video_get_drvdata(vdev);
	struct common_obj *common = &ch->common[VPIF_VIDEO_INDEX];

	/* Check the validity of the buffer type */
	if (common->fmt.type != fmt->type)
		return -EINVAL;

	if (vpif_update_resolution(ch))
		return -EINVAL;
	*fmt = common->fmt;
	return 0;
}

static int vpif_try_fmt_vid_out(struct file *file, void *priv,
				struct v4l2_format *fmt)
{
	struct video_device *vdev = video_devdata(file);
	struct channel_obj *ch = video_get_drvdata(vdev);
	struct common_obj *common = &ch->common[VPIF_VIDEO_INDEX];
	struct v4l2_pix_format *pixfmt = &fmt->fmt.pix;

	/*
	 * to supress v4l-compliance warnings silently correct
	 * the pixelformat
	 */
	if (pixfmt->pixelformat != V4L2_PIX_FMT_YUV422P)
		pixfmt->pixelformat = common->fmt.fmt.pix.pixelformat;

	if (vpif_update_resolution(ch))
		return -EINVAL;

	pixfmt->colorspace = common->fmt.fmt.pix.colorspace;
	pixfmt->field = common->fmt.fmt.pix.field;
	pixfmt->bytesperline = common->fmt.fmt.pix.width;
	pixfmt->width = common->fmt.fmt.pix.width;
	pixfmt->height = common->fmt.fmt.pix.height;
	pixfmt->sizeimage = pixfmt->bytesperline * pixfmt->height * 2;
	pixfmt->priv = 0;

	return 0;
}

static int vpif_s_fmt_vid_out(struct file *file, void *priv,
				struct v4l2_format *fmt)
{
	struct video_device *vdev = video_devdata(file);
	struct channel_obj *ch = video_get_drvdata(vdev);
	struct common_obj *common = &ch->common[VPIF_VIDEO_INDEX];
	struct v4l2_pix_format *pixfmt = &fmt->fmt.pix;
	int ret;

	if (vb2_is_busy(&common->buffer_queue))
		return -EBUSY;

	ret = vpif_try_fmt_vid_out(file, priv, fmt);
	if (ret)
		return ret;

	/* store the pix format in the channel object */
	common->fmt.fmt.pix = *pixfmt;

	/* store the format in the channel object */
	common->fmt = *fmt;
	return 0;
}

static int vpif_s_std(struct file *file, void *priv, v4l2_std_id std_id)
{
	struct vpif_display_config *config = vpif_dev->platform_data;
	struct video_device *vdev = video_devdata(file);
	struct channel_obj *ch = video_get_drvdata(vdev);
	struct common_obj *common = &ch->common[VPIF_VIDEO_INDEX];
	struct vpif_display_chan_config *chan_cfg;
	struct v4l2_output output;
	int ret;

	if (config->chan_config[ch->channel_id].outputs == NULL)
		return -ENODATA;

	chan_cfg = &config->chan_config[ch->channel_id];
	output = chan_cfg->outputs[ch->output_idx].output;
	if (output.capabilities != V4L2_OUT_CAP_STD)
		return -ENODATA;

	if (vb2_is_busy(&common->buffer_queue))
		return -EBUSY;


	if (!(std_id & VPIF_V4L2_STD))
		return -EINVAL;

	/* Call encoder subdevice function to set the standard */
	ch->video.stdid = std_id;
	memset(&ch->video.dv_timings, 0, sizeof(ch->video.dv_timings));
	/* Get the information about the standard */
	if (vpif_update_resolution(ch))
		return -EINVAL;

	common->fmt.fmt.pix.bytesperline = common->fmt.fmt.pix.width;

	ret = v4l2_device_call_until_err(&vpif_obj.v4l2_dev, 1, video,
						s_std_output, std_id);
	if (ret < 0) {
		vpif_err("Failed to set output standard\n");
		return ret;
	}

	ret = v4l2_device_call_until_err(&vpif_obj.v4l2_dev, 1, video,
							s_std, std_id);
	if (ret < 0)
		vpif_err("Failed to set standard for sub devices\n");
	return ret;
}

static int vpif_g_std(struct file *file, void *priv, v4l2_std_id *std)
{
	struct vpif_display_config *config = vpif_dev->platform_data;
	struct video_device *vdev = video_devdata(file);
	struct channel_obj *ch = video_get_drvdata(vdev);
	struct vpif_display_chan_config *chan_cfg;
	struct v4l2_output output;

	if (config->chan_config[ch->channel_id].outputs == NULL)
		return -ENODATA;

	chan_cfg = &config->chan_config[ch->channel_id];
	output = chan_cfg->outputs[ch->output_idx].output;
	if (output.capabilities != V4L2_OUT_CAP_STD)
		return -ENODATA;

	*std = ch->video.stdid;
	return 0;
}

static int vpif_enum_output(struct file *file, void *fh,
				struct v4l2_output *output)
{

	struct vpif_display_config *config = vpif_dev->platform_data;
	struct video_device *vdev = video_devdata(file);
	struct channel_obj *ch = video_get_drvdata(vdev);
	struct vpif_display_chan_config *chan_cfg;

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
	struct video_device *vdev = video_devdata(file);
	struct channel_obj *ch = video_get_drvdata(vdev);
	struct vpif_display_chan_config *chan_cfg;
	struct common_obj *common = &ch->common[VPIF_VIDEO_INDEX];

	if (vb2_is_busy(&common->buffer_queue))
		return -EBUSY;

	chan_cfg = &config->chan_config[ch->channel_id];

	if (i >= chan_cfg->output_count)
		return -EINVAL;

	return vpif_set_output(config, ch, i);
}

static int vpif_g_output(struct file *file, void *priv, unsigned int *i)
{
	struct video_device *vdev = video_devdata(file);
	struct channel_obj *ch = video_get_drvdata(vdev);

	*i = ch->output_idx;

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
	struct vpif_display_config *config = vpif_dev->platform_data;
	struct video_device *vdev = video_devdata(file);
	struct channel_obj *ch = video_get_drvdata(vdev);
	struct vpif_display_chan_config *chan_cfg;
	struct v4l2_output output;
	int ret;

	if (config->chan_config[ch->channel_id].outputs == NULL)
		return -ENODATA;

	chan_cfg = &config->chan_config[ch->channel_id];
	output = chan_cfg->outputs[ch->output_idx].output;
	if (output.capabilities != V4L2_OUT_CAP_DV_TIMINGS)
		return -ENODATA;

	timings->pad = 0;

	ret = v4l2_subdev_call(ch->sd, pad, enum_dv_timings, timings);
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
	struct vpif_display_config *config = vpif_dev->platform_data;
	struct video_device *vdev = video_devdata(file);
	struct channel_obj *ch = video_get_drvdata(vdev);
	struct vpif_params *vpifparams = &ch->vpifparams;
	struct common_obj *common = &ch->common[VPIF_VIDEO_INDEX];
	struct vpif_channel_config_params *std_info = &vpifparams->std_info;
	struct video_obj *vid_ch = &ch->video;
	struct v4l2_bt_timings *bt = &vid_ch->dv_timings.bt;
	struct vpif_display_chan_config *chan_cfg;
	struct v4l2_output output;
	int ret;

	if (config->chan_config[ch->channel_id].outputs == NULL)
		return -ENODATA;

	chan_cfg = &config->chan_config[ch->channel_id];
	output = chan_cfg->outputs[ch->output_idx].output;
	if (output.capabilities != V4L2_OUT_CAP_DV_TIMINGS)
		return -ENODATA;

	if (vb2_is_busy(&common->buffer_queue))
		return -EBUSY;

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
	struct vpif_display_config *config = vpif_dev->platform_data;
	struct video_device *vdev = video_devdata(file);
	struct channel_obj *ch = video_get_drvdata(vdev);
	struct vpif_display_chan_config *chan_cfg;
	struct video_obj *vid_ch = &ch->video;
	struct v4l2_output output;

	if (config->chan_config[ch->channel_id].outputs == NULL)
		goto error;

	chan_cfg = &config->chan_config[ch->channel_id];
	output = chan_cfg->outputs[ch->output_idx].output;

	if (output.capabilities != V4L2_OUT_CAP_DV_TIMINGS)
		goto error;

	*timings = vid_ch->dv_timings;

	return 0;
error:
	return -ENODATA;
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

/* vpif display ioctl operations */
static const struct v4l2_ioctl_ops vpif_ioctl_ops = {
	.vidioc_querycap		= vpif_querycap,
	.vidioc_enum_fmt_vid_out	= vpif_enum_fmt_vid_out,
	.vidioc_g_fmt_vid_out		= vpif_g_fmt_vid_out,
	.vidioc_s_fmt_vid_out		= vpif_s_fmt_vid_out,
	.vidioc_try_fmt_vid_out		= vpif_try_fmt_vid_out,

	.vidioc_reqbufs			= vb2_ioctl_reqbufs,
	.vidioc_create_bufs		= vb2_ioctl_create_bufs,
	.vidioc_querybuf		= vb2_ioctl_querybuf,
	.vidioc_qbuf			= vb2_ioctl_qbuf,
	.vidioc_dqbuf			= vb2_ioctl_dqbuf,
	.vidioc_expbuf			= vb2_ioctl_expbuf,
	.vidioc_streamon		= vb2_ioctl_streamon,
	.vidioc_streamoff		= vb2_ioctl_streamoff,

	.vidioc_s_std			= vpif_s_std,
	.vidioc_g_std			= vpif_g_std,

	.vidioc_enum_output		= vpif_enum_output,
	.vidioc_s_output		= vpif_s_output,
	.vidioc_g_output		= vpif_g_output,

	.vidioc_enum_dv_timings		= vpif_enum_dv_timings,
	.vidioc_s_dv_timings		= vpif_s_dv_timings,
	.vidioc_g_dv_timings		= vpif_g_dv_timings,

	.vidioc_log_status		= vpif_log_status,
};

static const struct v4l2_file_operations vpif_fops = {
	.owner		= THIS_MODULE,
	.open		= v4l2_fh_open,
	.release	= vb2_fop_release,
	.unlocked_ioctl	= video_ioctl2,
	.mmap		= vb2_fop_mmap,
	.poll		= vb2_fop_poll
};

/*Configure the channels, buffer sizei, request irq */
static int initialize_vpif(void)
{
	int free_channel_objects_index;
	int err, i, j;

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
		if (!strcmp(vpif_obj.config->subdevinfo[i].name,
			    subdev->name)) {
			vpif_obj.sd[i] = subdev;
			vpif_obj.sd[i]->grp_id = 1 << i;
			return 0;
		}

	return -EINVAL;
}

static int vpif_probe_complete(void)
{
	struct common_obj *common;
	struct video_device *vdev;
	struct channel_obj *ch;
	struct vb2_queue *q;
	int j, err, k;

	for (j = 0; j < VPIF_DISPLAY_MAX_DEVICES; j++) {
		ch = vpif_obj.dev[j];
		/* Initialize field of the channel objects */
		for (k = 0; k < VPIF_NUMOBJECTS; k++) {
			common = &ch->common[k];
			spin_lock_init(&common->irqlock);
			mutex_init(&common->lock);
			common->set_addr = NULL;
			common->ytop_off = 0;
			common->ybtm_off = 0;
			common->ctop_off = 0;
			common->cbtm_off = 0;
			common->cur_frm = NULL;
			common->next_frm = NULL;
			memset(&common->fmt, 0, sizeof(common->fmt));
		}
		ch->initialized = 0;
		if (vpif_obj.config->subdev_count)
			ch->sd = vpif_obj.sd[0];
		ch->channel_id = j;

		memset(&ch->vpifparams, 0, sizeof(ch->vpifparams));

		ch->common[VPIF_VIDEO_INDEX].fmt.type =
						V4L2_BUF_TYPE_VIDEO_OUTPUT;

		/* select output 0 */
		err = vpif_set_output(vpif_obj.config, ch, 0);
		if (err)
			goto probe_out;

		/* set initial format */
		ch->video.stdid = V4L2_STD_525_60;
		memset(&ch->video.dv_timings, 0, sizeof(ch->video.dv_timings));
		vpif_update_resolution(ch);

		/* Initialize vb2 queue */
		q = &common->buffer_queue;
		q->type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
		q->io_modes = VB2_MMAP | VB2_USERPTR | VB2_DMABUF;
		q->drv_priv = ch;
		q->ops = &video_qops;
		q->mem_ops = &vb2_dma_contig_memops;
		q->buf_struct_size = sizeof(struct vpif_disp_buffer);
		q->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
		q->min_buffers_needed = 1;
		q->lock = &common->lock;
		err = vb2_queue_init(q);
		if (err) {
			vpif_err("vpif_display: vb2_queue_init() failed\n");
			goto probe_out;
		}

		common->alloc_ctx = vb2_dma_contig_init_ctx(vpif_dev);
		if (IS_ERR(common->alloc_ctx)) {
			vpif_err("Failed to get the context\n");
			err = PTR_ERR(common->alloc_ctx);
			goto probe_out;
		}

		INIT_LIST_HEAD(&common->dma_queue);

		/* register video device */
		vpif_dbg(1, debug, "channel=%x,channel->video_dev=%x\n",
			 (int)ch, (int)&ch->video_dev);

		/* Initialize the video_device structure */
		vdev = ch->video_dev;
		strlcpy(vdev->name, VPIF_DRIVER_NAME, sizeof(vdev->name));
		vdev->release = video_device_release;
		vdev->fops = &vpif_fops;
		vdev->ioctl_ops = &vpif_ioctl_ops;
		vdev->v4l2_dev = &vpif_obj.v4l2_dev;
		vdev->vfl_dir = VFL_DIR_TX;
		vdev->queue = q;
		vdev->lock = &common->lock;
		set_bit(V4L2_FL_USE_FH_PRIO, &vdev->flags);
		video_set_drvdata(ch->video_dev, ch);
		err = video_register_device(vdev, VFL_TYPE_GRABBER,
					    (j ? 3 : 2));
		if (err < 0)
			goto probe_out;
	}

	return 0;

probe_out:
	for (k = 0; k < j; k++) {
		ch = vpif_obj.dev[k];
		common = &ch->common[k];
		vb2_dma_contig_cleanup_ctx(common->alloc_ctx);
		video_unregister_device(ch->video_dev);
		video_device_release(ch->video_dev);
		ch->video_dev = NULL;
	}
	return err;
}

static int vpif_async_complete(struct v4l2_async_notifier *notifier)
{
	return vpif_probe_complete();
}

/*
 * vpif_probe: This function creates device entries by register itself to the
 * V4L2 driver and initializes fields of each channel objects
 */
static __init int vpif_probe(struct platform_device *pdev)
{
	struct vpif_subdev_info *subdevdata;
	int i, j = 0, err = 0;
	int res_idx = 0;
	struct i2c_adapter *i2c_adap;
	struct channel_obj *ch;
	struct video_device *vfd;
	struct resource *res;
	int subdev_count;

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
					IRQF_SHARED, VPIF_DRIVER_NAME,
					(void *)(&vpif_obj.dev[res_idx]->
					channel_id));
		if (err) {
			err = -EINVAL;
			vpif_err("VPIF IRQ request failed\n");
			goto vpif_unregister;
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
			goto vpif_unregister;
		}

		/* Set video_dev to the video device */
		ch->video_dev = vfd;
	}

	vpif_obj.config = pdev->dev.platform_data;
	subdev_count = vpif_obj.config->subdev_count;
	subdevdata = vpif_obj.config->subdevinfo;
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
			vpif_obj.sd[i] =
				v4l2_i2c_new_subdev_board(&vpif_obj.v4l2_dev,
							  i2c_adap,
							  &subdevdata[i].
							  board_info,
							  NULL);
			if (!vpif_obj.sd[i]) {
				vpif_err("Error registering v4l2 subdevice\n");
				err = -ENODEV;
				goto probe_subdev_out;
			}

			if (vpif_obj.sd[i])
				vpif_obj.sd[i]->grp_id = 1 << i;
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
	kfree(vpif_obj.sd);
vpif_sd_error:
	for (i = 0; i < VPIF_DISPLAY_MAX_DEVICES; i++) {
		ch = vpif_obj.dev[i];
		/* Note: does nothing if ch->video_dev == NULL */
		video_device_release(ch->video_dev);
	}
vpif_unregister:
	v4l2_device_unregister(&vpif_obj.v4l2_dev);

	return err;
}

/*
 * vpif_remove: It un-register channels from V4L2 driver
 */
static int vpif_remove(struct platform_device *device)
{
	struct common_obj *common;
	struct channel_obj *ch;
	int i;

	v4l2_device_unregister(&vpif_obj.v4l2_dev);

	kfree(vpif_obj.sd);
	/* un-register device */
	for (i = 0; i < VPIF_DISPLAY_MAX_DEVICES; i++) {
		/* Get the pointer to the channel object */
		ch = vpif_obj.dev[i];
		common = &ch->common[i];
		vb2_dma_contig_cleanup_ctx(common->alloc_ctx);
		/* Unregister video device */
		video_unregister_device(ch->video_dev);

		ch->video_dev = NULL;
		kfree(vpif_obj.dev[i]);
	}

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int vpif_suspend(struct device *dev)
{
	struct common_obj *common;
	struct channel_obj *ch;
	int i;

	for (i = 0; i < VPIF_DISPLAY_MAX_DEVICES; i++) {
		/* Get the pointer to the channel object */
		ch = vpif_obj.dev[i];
		common = &ch->common[VPIF_VIDEO_INDEX];

		if (!vb2_is_streaming(&common->buffer_queue))
			continue;

		mutex_lock(&common->lock);
		/* Disable channel */
		if (ch->channel_id == VPIF_CHANNEL2_VIDEO) {
			enable_channel2(0);
			channel2_intr_enable(0);
		}
		if (ch->channel_id == VPIF_CHANNEL3_VIDEO ||
			ycmux_mode == 2) {
			enable_channel3(0);
			channel3_intr_enable(0);
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

		if (!vb2_is_streaming(&common->buffer_queue))
			continue;

		mutex_lock(&common->lock);
		/* Enable channel */
		if (ch->channel_id == VPIF_CHANNEL2_VIDEO) {
			enable_channel2(1);
			channel2_intr_enable(1);
		}
		if (ch->channel_id == VPIF_CHANNEL3_VIDEO ||
				ycmux_mode == 2) {
			enable_channel3(1);
			channel3_intr_enable(1);
		}
		mutex_unlock(&common->lock);
	}

	return 0;
}

#endif

static SIMPLE_DEV_PM_OPS(vpif_pm_ops, vpif_suspend, vpif_resume);

static __refdata struct platform_driver vpif_driver = {
	.driver	= {
			.name	= VPIF_DRIVER_NAME,
			.owner	= THIS_MODULE,
			.pm	= &vpif_pm_ops,
	},
	.probe	= vpif_probe,
	.remove	= vpif_remove,
};

module_platform_driver(vpif_driver);
