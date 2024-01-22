// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2009 Texas Instruments Inc
 * Copyright (C) 2014 Lad, Prabhakar <prabhakar.csengg@gmail.com>
 *
 * TODO : add support for VBI & HBI data service
 *	  add static buffer allocation
 */

#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/of_graph.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include <media/v4l2-fwnode.h>
#include <media/v4l2-ioctl.h>
#include <media/i2c/tvp514x.h>
#include <media/v4l2-mediabus.h>

#include <linux/videodev2.h>

#include "vpif.h"
#include "vpif_capture.h"

MODULE_DESCRIPTION("TI DaVinci VPIF Capture driver");
MODULE_LICENSE("GPL");
MODULE_VERSION(VPIF_CAPTURE_VERSION);

#define vpif_err(fmt, arg...)	v4l2_err(&vpif_obj.v4l2_dev, fmt, ## arg)
#define vpif_dbg(level, debug, fmt, arg...)	\
		v4l2_dbg(level, debug, &vpif_obj.v4l2_dev, fmt, ## arg)

static int debug = 1;

module_param(debug, int, 0644);

MODULE_PARM_DESC(debug, "Debug level 0-1");

#define VPIF_DRIVER_NAME	"vpif_capture"
MODULE_ALIAS("platform:" VPIF_DRIVER_NAME);

/* global variables */
static struct vpif_device vpif_obj = { {NULL} };
static struct device *vpif_dev;
static void vpif_calculate_offsets(struct channel_obj *ch);
static void vpif_config_addr(struct channel_obj *ch, int muxmode);

static u8 channel_first_int[VPIF_NUMBER_OF_OBJECTS][2] = { {1, 1} };

/* Is set to 1 in case of SDTV formats, 2 in case of HDTV formats. */
static int ycmux_mode;

static inline
struct vpif_cap_buffer *to_vpif_buffer(struct vb2_v4l2_buffer *vb)
{
	return container_of(vb, struct vpif_cap_buffer, vb);
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
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct vb2_queue *q = vb->vb2_queue;
	struct channel_obj *ch = vb2_get_drv_priv(q);
	struct common_obj *common;
	unsigned long addr;

	vpif_dbg(2, debug, "vpif_buffer_prepare\n");

	common = &ch->common[VPIF_VIDEO_INDEX];

	vb2_set_plane_payload(vb, 0, common->fmt.fmt.pix.sizeimage);
	if (vb2_get_plane_payload(vb, 0) > vb2_plane_size(vb, 0))
		return -EINVAL;

	vbuf->field = common->fmt.fmt.pix.field;

	addr = vb2_dma_contig_plane_dma_addr(vb, 0);
	if (!IS_ALIGNED((addr + common->ytop_off), 8) ||
		!IS_ALIGNED((addr + common->ybtm_off), 8) ||
		!IS_ALIGNED((addr + common->ctop_off), 8) ||
		!IS_ALIGNED((addr + common->cbtm_off), 8)) {
		vpif_dbg(1, debug, "offset is not aligned\n");
		return -EINVAL;
	}

	return 0;
}

/**
 * vpif_buffer_queue_setup : Callback function for buffer setup.
 * @vq: vb2_queue ptr
 * @nbuffers: ptr to number of buffers requested by application
 * @nplanes: contains number of distinct video planes needed to hold a frame
 * @sizes: contains the size (in bytes) of each plane.
 * @alloc_devs: ptr to allocation context
 *
 * This callback function is called when reqbuf() is called to adjust
 * the buffer count and buffer size
 */
static int vpif_buffer_queue_setup(struct vb2_queue *vq,
				unsigned int *nbuffers, unsigned int *nplanes,
				unsigned int sizes[], struct device *alloc_devs[])
{
	struct channel_obj *ch = vb2_get_drv_priv(vq);
	struct common_obj *common = &ch->common[VPIF_VIDEO_INDEX];
	unsigned size = common->fmt.fmt.pix.sizeimage;
	unsigned int q_num_bufs = vb2_get_num_buffers(vq);

	vpif_dbg(2, debug, "vpif_buffer_setup\n");

	if (*nplanes) {
		if (sizes[0] < size)
			return -EINVAL;
		size = sizes[0];
	}

	if (q_num_bufs + *nbuffers < 3)
		*nbuffers = 3 - q_num_bufs;

	*nplanes = 1;
	sizes[0] = size;

	/* Calculate the offset for Y and C data in the buffer */
	vpif_calculate_offsets(ch);

	return 0;
}

/**
 * vpif_buffer_queue : Callback function to add buffer to DMA queue
 * @vb: ptr to vb2_buffer
 */
static void vpif_buffer_queue(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct channel_obj *ch = vb2_get_drv_priv(vb->vb2_queue);
	struct vpif_cap_buffer *buf = to_vpif_buffer(vbuf);
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
 * vpif_start_streaming : Starts the DMA engine for streaming
 * @vq: ptr to vb2_buffer
 * @count: number of buffers
 */
static int vpif_start_streaming(struct vb2_queue *vq, unsigned int count)
{
	struct vpif_capture_config *vpif_config_data =
					vpif_dev->platform_data;
	struct channel_obj *ch = vb2_get_drv_priv(vq);
	struct common_obj *common = &ch->common[VPIF_VIDEO_INDEX];
	struct vpif_params *vpif = &ch->vpifparams;
	struct vpif_cap_buffer *buf, *tmp;
	unsigned long addr, flags;
	int ret;

	/* Initialize field_id */
	ch->field_id = 0;

	/* configure 1 or 2 channel mode */
	if (vpif_config_data->setup_input_channel_mode) {
		ret = vpif_config_data->
			setup_input_channel_mode(vpif->std_info.ycmux_mode);
		if (ret < 0) {
			vpif_dbg(1, debug, "can't set vpif channel mode\n");
			goto err;
		}
	}

	ret = v4l2_subdev_call(ch->sd, video, s_stream, 1);
	if (ret && ret != -ENOIOCTLCMD && ret != -ENODEV) {
		vpif_dbg(1, debug, "stream on failed in subdev\n");
		goto err;
	}

	/* Call vpif_set_params function to set the parameters and addresses */
	ret = vpif_set_video_params(vpif, ch->channel_id);
	if (ret < 0) {
		vpif_dbg(1, debug, "can't set video params\n");
		goto err;
	}

	ycmux_mode = ret;
	vpif_config_addr(ch, ret);

	/* Get the next frame from the buffer queue */
	spin_lock_irqsave(&common->irqlock, flags);
	common->cur_frm = common->next_frm = list_entry(common->dma_queue.next,
				    struct vpif_cap_buffer, list);
	/* Remove buffer from the buffer queue */
	list_del(&common->cur_frm->list);
	spin_unlock_irqrestore(&common->irqlock, flags);

	addr = vb2_dma_contig_plane_dma_addr(&common->cur_frm->vb.vb2_buf, 0);

	common->set_addr(addr + common->ytop_off,
			 addr + common->ybtm_off,
			 addr + common->ctop_off,
			 addr + common->cbtm_off);

	/**
	 * Set interrupt for both the fields in VPIF Register enable channel in
	 * VPIF register
	 */
	channel_first_int[VPIF_VIDEO_INDEX][ch->channel_id] = 1;
	if (VPIF_CHANNEL0_VIDEO == ch->channel_id) {
		channel0_intr_assert();
		channel0_intr_enable(1);
		enable_channel0(1);
	}
	if (VPIF_CHANNEL1_VIDEO == ch->channel_id ||
		ycmux_mode == 2) {
		channel1_intr_assert();
		channel1_intr_enable(1);
		enable_channel1(1);
	}

	return 0;

err:
	spin_lock_irqsave(&common->irqlock, flags);
	list_for_each_entry_safe(buf, tmp, &common->dma_queue, list) {
		list_del(&buf->list);
		vb2_buffer_done(&buf->vb.vb2_buf, VB2_BUF_STATE_QUEUED);
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
	int ret;

	common = &ch->common[VPIF_VIDEO_INDEX];

	/* Disable channel as per its device type and channel id */
	if (VPIF_CHANNEL0_VIDEO == ch->channel_id) {
		enable_channel0(0);
		channel0_intr_enable(0);
	}
	if (VPIF_CHANNEL1_VIDEO == ch->channel_id ||
		ycmux_mode == 2) {
		enable_channel1(0);
		channel1_intr_enable(0);
	}

	ycmux_mode = 0;

	ret = v4l2_subdev_call(ch->sd, video, s_stream, 0);
	if (ret && ret != -ENOIOCTLCMD && ret != -ENODEV)
		vpif_dbg(1, debug, "stream off failed in subdev\n");

	/* release all active buffers */
	if (common->cur_frm == common->next_frm) {
		vb2_buffer_done(&common->cur_frm->vb.vb2_buf,
				VB2_BUF_STATE_ERROR);
	} else {
		if (common->cur_frm)
			vb2_buffer_done(&common->cur_frm->vb.vb2_buf,
					VB2_BUF_STATE_ERROR);
		if (common->next_frm)
			vb2_buffer_done(&common->next_frm->vb.vb2_buf,
					VB2_BUF_STATE_ERROR);
	}

	spin_lock_irqsave(&common->irqlock, flags);
	while (!list_empty(&common->dma_queue)) {
		common->next_frm = list_entry(common->dma_queue.next,
						struct vpif_cap_buffer, list);
		list_del(&common->next_frm->list);
		vb2_buffer_done(&common->next_frm->vb.vb2_buf,
				VB2_BUF_STATE_ERROR);
	}
	spin_unlock_irqrestore(&common->irqlock, flags);
}

static const struct vb2_ops video_qops = {
	.queue_setup		= vpif_buffer_queue_setup,
	.buf_prepare		= vpif_buffer_prepare,
	.start_streaming	= vpif_start_streaming,
	.stop_streaming		= vpif_stop_streaming,
	.buf_queue		= vpif_buffer_queue,
	.wait_prepare		= vb2_ops_wait_prepare,
	.wait_finish		= vb2_ops_wait_finish,
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
	common->cur_frm->vb.vb2_buf.timestamp = ktime_get_ns();
	vb2_buffer_done(&common->cur_frm->vb.vb2_buf, VB2_BUF_STATE_DONE);
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
	addr = vb2_dma_contig_plane_dma_addr(&common->next_frm->vb.vb2_buf, 0);

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
	int channel_id;
	int fid = -1, i;

	channel_id = *(int *)(dev_id);
	if (!vpif_intr_status(channel_id))
		return IRQ_NONE;

	ch = dev->dev[channel_id];

	for (i = 0; i < VPIF_NUMBER_OF_OBJECTS; i++) {
		common = &ch->common[i];
		/* skip If streaming is not started in this channel */
		/* Check the field format */
		if (1 == ch->vpifparams.std_info.frm_fmt ||
		    common->fmt.fmt.pix.field == V4L2_FIELD_NONE) {
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
	struct v4l2_pix_format *pixfmt = &common->fmt.fmt.pix;

	vpif_dbg(2, debug, "vpif_update_std_info\n");

	/*
	 * if called after try_fmt or g_fmt, there will already be a size
	 * so use that by default.
	 */
	if (pixfmt->width && pixfmt->height) {
		if (pixfmt->field == V4L2_FIELD_ANY ||
		    pixfmt->field == V4L2_FIELD_NONE)
			pixfmt->field = V4L2_FIELD_NONE;

		vpifparams->iface.if_type = VPIF_IF_BT656;
		if (pixfmt->pixelformat == V4L2_PIX_FMT_SGRBG10 ||
		    pixfmt->pixelformat == V4L2_PIX_FMT_SBGGR8)
			vpifparams->iface.if_type = VPIF_IF_RAW_BAYER;

		if (pixfmt->pixelformat == V4L2_PIX_FMT_SGRBG10)
			vpifparams->params.data_sz = 1; /* 10 bits/pixel.  */

		/*
		 * For raw formats from camera sensors, we don't need
		 * the std_info from table lookup, so nothing else to do here.
		 */
		if (vpifparams->iface.if_type == VPIF_IF_RAW_BAYER) {
			memset(std_info, 0, sizeof(struct vpif_channel_config_params));
			vpifparams->std_info.capture_format = 1; /* CCD/raw mode */
			return 0;
		}
	}

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
	common->fmt.fmt.pix.sizeimage = common->height * common->width * 2;
	common->fmt.fmt.pix.bytesperline = std_info->width;
	vpifparams->video_params.hpitch = std_info->width;
	vpifparams->video_params.storage_mode = std_info->frm_fmt;

	if (vid_ch->stdid)
		common->fmt.fmt.pix.colorspace = V4L2_COLORSPACE_SMPTE170M;
	else
		common->fmt.fmt.pix.colorspace = V4L2_COLORSPACE_REC709;

	if (ch->vpifparams.std_info.frm_fmt)
		common->fmt.fmt.pix.field = V4L2_FIELD_NONE;
	else
		common->fmt.fmt.pix.field = V4L2_FIELD_INTERLACED;

	if (ch->vpifparams.iface.if_type == VPIF_IF_RAW_BAYER)
		common->fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_SBGGR8;
	else
		common->fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_NV16;

	common->fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

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
	unsigned int hpitch, sizeimage;
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
 * vpif_config_addr() - function to configure buffer address in vpif
 * @ch: channel ptr
 * @muxmode: channel mux mode
 */
static void vpif_config_addr(struct channel_obj *ch, int muxmode)
{
	struct common_obj *common;

	vpif_dbg(2, debug, "vpif_config_addr\n");

	common = &(ch->common[VPIF_VIDEO_INDEX]);

	if (VPIF_CHANNEL1_VIDEO == ch->channel_id)
		common->set_addr = ch1_set_video_buf_addr;
	else if (2 == muxmode)
		common->set_addr = ch0_set_video_buf_addr_yc_nmux;
	else
		common->set_addr = ch0_set_video_buf_addr;
}

/**
 * vpif_input_to_subdev() - Maps input to sub device
 * @vpif_cfg: global config ptr
 * @chan_cfg: channel config ptr
 * @input_index: Given input index from application
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

	if (!chan_cfg)
		return -1;
	if (input_index >= chan_cfg->input_count)
		return -1;
	subdev_name = chan_cfg->inputs[input_index].subdev_name;
	if (!subdev_name)
		return -1;

	/* loop through the sub device list to get the sub device info */
	for (i = 0; i < vpif_cfg->subdev_count; i++) {
		subdev_info = &vpif_cfg->subdev_info[i];
		if (subdev_info && !strcmp(subdev_info->name, subdev_name))
			return i;
	}
	return -1;
}

/**
 * vpif_set_input() - Select an input
 * @vpif_cfg: global config ptr
 * @ch: channel
 * @index: Given input index from application
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
	} else {
		/* no subdevice, no input to setup */
		return 0;
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
	ch->video_dev.tvnorms = chan_cfg->inputs[index].input.std;
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
	struct video_device *vdev = video_devdata(file);
	struct channel_obj *ch = video_get_drvdata(vdev);
	int ret;

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
 * @std: ptr to std id
 */
static int vpif_g_std(struct file *file, void *priv, v4l2_std_id *std)
{
	struct vpif_capture_config *config = vpif_dev->platform_data;
	struct video_device *vdev = video_devdata(file);
	struct channel_obj *ch = video_get_drvdata(vdev);
	struct vpif_capture_chan_config *chan_cfg;
	struct v4l2_input input;

	vpif_dbg(2, debug, "vpif_g_std\n");

	if (!config->chan_config[ch->channel_id].inputs)
		return -ENODATA;

	chan_cfg = &config->chan_config[ch->channel_id];
	input = chan_cfg->inputs[ch->input_idx].input;
	if (input.capabilities != V4L2_IN_CAP_STD)
		return -ENODATA;

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
	struct vpif_capture_config *config = vpif_dev->platform_data;
	struct video_device *vdev = video_devdata(file);
	struct channel_obj *ch = video_get_drvdata(vdev);
	struct common_obj *common = &ch->common[VPIF_VIDEO_INDEX];
	struct vpif_capture_chan_config *chan_cfg;
	struct v4l2_input input;
	int ret;

	vpif_dbg(2, debug, "vpif_s_std\n");

	if (!config->chan_config[ch->channel_id].inputs)
		return -ENODATA;

	chan_cfg = &config->chan_config[ch->channel_id];
	input = chan_cfg->inputs[ch->input_idx].input;
	if (input.capabilities != V4L2_IN_CAP_STD)
		return -ENODATA;

	if (vb2_is_busy(&common->buffer_queue))
		return -EBUSY;

	/* Call encoder subdevice function to set the standard */
	ch->video.stdid = std_id;
	memset(&ch->video.dv_timings, 0, sizeof(ch->video.dv_timings));

	/* Get the information about the standard */
	if (vpif_update_std_info(ch)) {
		vpif_err("Error getting the standard info\n");
		return -EINVAL;
	}

	/* set standard in the sub device */
	ret = v4l2_subdev_call(ch->sd, video, s_std, std_id);
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
	struct video_device *vdev = video_devdata(file);
	struct channel_obj *ch = video_get_drvdata(vdev);
	struct vpif_capture_chan_config *chan_cfg;

	chan_cfg = &config->chan_config[ch->channel_id];

	if (input->index >= chan_cfg->input_count)
		return -EINVAL;

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
	struct video_device *vdev = video_devdata(file);
	struct channel_obj *ch = video_get_drvdata(vdev);

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
	struct video_device *vdev = video_devdata(file);
	struct channel_obj *ch = video_get_drvdata(vdev);
	struct common_obj *common = &ch->common[VPIF_VIDEO_INDEX];
	struct vpif_capture_chan_config *chan_cfg;

	chan_cfg = &config->chan_config[ch->channel_id];

	if (index >= chan_cfg->input_count)
		return -EINVAL;

	if (vb2_is_busy(&common->buffer_queue))
		return -EBUSY;

	return vpif_set_input(config, ch, index);
}

/**
 * vpif_enum_fmt_vid_cap() - ENUM_FMT handler
 * @file: file ptr
 * @priv: file handle
 * @fmt: ptr to V4L2 format descriptor
 */
static int vpif_enum_fmt_vid_cap(struct file *file, void  *priv,
					struct v4l2_fmtdesc *fmt)
{
	struct video_device *vdev = video_devdata(file);
	struct channel_obj *ch = video_get_drvdata(vdev);

	if (fmt->index != 0) {
		vpif_dbg(1, debug, "Invalid format index\n");
		return -EINVAL;
	}

	/* Fill in the information about format */
	if (ch->vpifparams.iface.if_type == VPIF_IF_RAW_BAYER)
		fmt->pixelformat = V4L2_PIX_FMT_SBGGR8;
	else
		fmt->pixelformat = V4L2_PIX_FMT_NV16;
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
	struct video_device *vdev = video_devdata(file);
	struct channel_obj *ch = video_get_drvdata(vdev);
	struct v4l2_pix_format *pixfmt = &fmt->fmt.pix;
	struct common_obj *common = &(ch->common[VPIF_VIDEO_INDEX]);

	common->fmt = *fmt;
	vpif_update_std_info(ch);

	pixfmt->field = common->fmt.fmt.pix.field;
	pixfmt->colorspace = common->fmt.fmt.pix.colorspace;
	pixfmt->bytesperline = common->fmt.fmt.pix.width;
	pixfmt->width = common->fmt.fmt.pix.width;
	pixfmt->height = common->fmt.fmt.pix.height;
	pixfmt->sizeimage = pixfmt->bytesperline * pixfmt->height * 2;
	if (pixfmt->pixelformat == V4L2_PIX_FMT_SGRBG10) {
		pixfmt->bytesperline = common->fmt.fmt.pix.width * 2;
		pixfmt->sizeimage = pixfmt->bytesperline * pixfmt->height;
	}

	dev_dbg(vpif_dev, "%s: %d x %d; pitch=%d pixelformat=0x%08x, field=%d, size=%d\n", __func__,
		pixfmt->width, pixfmt->height,
		pixfmt->bytesperline, pixfmt->pixelformat,
		pixfmt->field, pixfmt->sizeimage);

	return 0;
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
	struct video_device *vdev = video_devdata(file);
	struct channel_obj *ch = video_get_drvdata(vdev);
	struct common_obj *common = &ch->common[VPIF_VIDEO_INDEX];
	struct v4l2_pix_format *pix_fmt = &fmt->fmt.pix;
	struct v4l2_subdev_format format = {
		.which = V4L2_SUBDEV_FORMAT_ACTIVE,
	};
	struct v4l2_mbus_framefmt *mbus_fmt = &format.format;
	int ret;

	/* Check the validity of the buffer type */
	if (common->fmt.type != fmt->type)
		return -EINVAL;

	/* By default, use currently set fmt */
	*fmt = common->fmt;

	/* If subdev has get_fmt, use that to override */
	ret = v4l2_subdev_call(ch->sd, pad, get_fmt, NULL, &format);
	if (!ret && mbus_fmt->code) {
		v4l2_fill_pix_format(pix_fmt, mbus_fmt);
		pix_fmt->bytesperline = pix_fmt->width;
		if (mbus_fmt->code == MEDIA_BUS_FMT_SGRBG10_1X10) {
			/* e.g. mt9v032 */
			pix_fmt->pixelformat = V4L2_PIX_FMT_SGRBG10;
			pix_fmt->bytesperline = pix_fmt->width * 2;
		} else if (mbus_fmt->code == MEDIA_BUS_FMT_UYVY8_2X8) {
			/* e.g. tvp514x */
			pix_fmt->pixelformat = V4L2_PIX_FMT_NV16;
			pix_fmt->bytesperline = pix_fmt->width * 2;
		} else {
			dev_warn(vpif_dev, "%s: Unhandled media-bus format 0x%x\n",
				 __func__, mbus_fmt->code);
		}
		pix_fmt->sizeimage = pix_fmt->bytesperline * pix_fmt->height;
		dev_dbg(vpif_dev, "%s: %d x %d; pitch=%d, pixelformat=0x%08x, code=0x%x, field=%d, size=%d\n", __func__,
			pix_fmt->width, pix_fmt->height,
			pix_fmt->bytesperline, pix_fmt->pixelformat,
			mbus_fmt->code, pix_fmt->field, pix_fmt->sizeimage);

		common->fmt = *fmt;
		vpif_update_std_info(ch);
	}

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
	struct video_device *vdev = video_devdata(file);
	struct channel_obj *ch = video_get_drvdata(vdev);
	struct common_obj *common = &ch->common[VPIF_VIDEO_INDEX];
	int ret;

	vpif_dbg(2, debug, "%s\n", __func__);

	if (vb2_is_busy(&common->buffer_queue))
		return -EBUSY;

	ret = vpif_try_fmt_vid_cap(file, priv, fmt);
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

	strscpy(cap->driver, VPIF_DRIVER_NAME, sizeof(cap->driver));
	strscpy(cap->card, config->card_name, sizeof(cap->card));

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
	struct vpif_capture_config *config = vpif_dev->platform_data;
	struct video_device *vdev = video_devdata(file);
	struct channel_obj *ch = video_get_drvdata(vdev);
	struct vpif_capture_chan_config *chan_cfg;
	struct v4l2_input input;
	int ret;

	if (!config->chan_config[ch->channel_id].inputs)
		return -ENODATA;

	chan_cfg = &config->chan_config[ch->channel_id];
	input = chan_cfg->inputs[ch->input_idx].input;
	if (input.capabilities != V4L2_IN_CAP_DV_TIMINGS)
		return -ENODATA;

	timings->pad = 0;

	ret = v4l2_subdev_call(ch->sd, pad, enum_dv_timings, timings);
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
	struct vpif_capture_config *config = vpif_dev->platform_data;
	struct video_device *vdev = video_devdata(file);
	struct channel_obj *ch = video_get_drvdata(vdev);
	struct vpif_capture_chan_config *chan_cfg;
	struct v4l2_input input;
	int ret;

	if (!config->chan_config[ch->channel_id].inputs)
		return -ENODATA;

	chan_cfg = &config->chan_config[ch->channel_id];
	input = chan_cfg->inputs[ch->input_idx].input;
	if (input.capabilities != V4L2_IN_CAP_DV_TIMINGS)
		return -ENODATA;

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
	struct vpif_capture_config *config = vpif_dev->platform_data;
	struct video_device *vdev = video_devdata(file);
	struct channel_obj *ch = video_get_drvdata(vdev);
	struct vpif_params *vpifparams = &ch->vpifparams;
	struct vpif_channel_config_params *std_info = &vpifparams->std_info;
	struct common_obj *common = &ch->common[VPIF_VIDEO_INDEX];
	struct video_obj *vid_ch = &ch->video;
	struct v4l2_bt_timings *bt = &vid_ch->dv_timings.bt;
	struct vpif_capture_chan_config *chan_cfg;
	struct v4l2_input input;
	int ret;

	if (!config->chan_config[ch->channel_id].inputs)
		return -ENODATA;

	chan_cfg = &config->chan_config[ch->channel_id];
	input = chan_cfg->inputs[ch->input_idx].input;
	if (input.capabilities != V4L2_IN_CAP_DV_TIMINGS)
		return -ENODATA;

	if (timings->type != V4L2_DV_BT_656_1120) {
		vpif_dbg(2, debug, "Timing type not defined\n");
		return -EINVAL;
	}

	if (vb2_is_busy(&common->buffer_queue))
		return -EBUSY;

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
		vpif_dbg(2, debug, "Timings for width, height, horizontal back porch, horizontal sync, horizontal front porch, vertical back porch, vertical sync and vertical back porch must be defined\n");
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
			vpif_dbg(2, debug, "Required timing values for interlaced BT format missing\n");
			return -EINVAL;
		}
	} else {
		std_info->l5 = std_info->vsize - (bt->vfrontporch - 1);
	}
	strscpy(std_info->name, "Custom timings BT656/1120",
		sizeof(std_info->name));
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
	struct vpif_capture_config *config = vpif_dev->platform_data;
	struct video_device *vdev = video_devdata(file);
	struct channel_obj *ch = video_get_drvdata(vdev);
	struct video_obj *vid_ch = &ch->video;
	struct vpif_capture_chan_config *chan_cfg;
	struct v4l2_input input;

	if (!config->chan_config[ch->channel_id].inputs)
		return -ENODATA;

	chan_cfg = &config->chan_config[ch->channel_id];
	input = chan_cfg->inputs[ch->input_idx].input;
	if (input.capabilities != V4L2_IN_CAP_DV_TIMINGS)
		return -ENODATA;

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
	.vidioc_querycap		= vpif_querycap,
	.vidioc_enum_fmt_vid_cap	= vpif_enum_fmt_vid_cap,
	.vidioc_g_fmt_vid_cap		= vpif_g_fmt_vid_cap,
	.vidioc_s_fmt_vid_cap		= vpif_s_fmt_vid_cap,
	.vidioc_try_fmt_vid_cap		= vpif_try_fmt_vid_cap,

	.vidioc_enum_input		= vpif_enum_input,
	.vidioc_s_input			= vpif_s_input,
	.vidioc_g_input			= vpif_g_input,

	.vidioc_reqbufs			= vb2_ioctl_reqbufs,
	.vidioc_create_bufs		= vb2_ioctl_create_bufs,
	.vidioc_querybuf		= vb2_ioctl_querybuf,
	.vidioc_qbuf			= vb2_ioctl_qbuf,
	.vidioc_dqbuf			= vb2_ioctl_dqbuf,
	.vidioc_expbuf			= vb2_ioctl_expbuf,
	.vidioc_streamon		= vb2_ioctl_streamon,
	.vidioc_streamoff		= vb2_ioctl_streamoff,

	.vidioc_querystd		= vpif_querystd,
	.vidioc_s_std			= vpif_s_std,
	.vidioc_g_std			= vpif_g_std,

	.vidioc_enum_dv_timings		= vpif_enum_dv_timings,
	.vidioc_query_dv_timings	= vpif_query_dv_timings,
	.vidioc_s_dv_timings		= vpif_s_dv_timings,
	.vidioc_g_dv_timings		= vpif_g_dv_timings,

	.vidioc_log_status		= vpif_log_status,
};

/* vpif file operations */
static const struct v4l2_file_operations vpif_fops = {
	.owner = THIS_MODULE,
	.open = v4l2_fh_open,
	.release = vb2_fop_release,
	.unlocked_ioctl = video_ioctl2,
	.mmap = vb2_fop_mmap,
	.poll = vb2_fop_poll
};

/**
 * initialize_vpif() - Initialize vpif data structures
 *
 * Allocate memory for data structures and initialize them
 */
static int initialize_vpif(void)
{
	int err, i, j;
	int free_channel_objects_index;

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

static inline void free_vpif_objs(void)
{
	int i;

	for (i = 0; i < VPIF_CAPTURE_MAX_DEVICES; i++)
		kfree(vpif_obj.dev[i]);
}

static int vpif_async_bound(struct v4l2_async_notifier *notifier,
			    struct v4l2_subdev *subdev,
			    struct v4l2_async_connection *asd)
{
	int i;

	for (i = 0; i < vpif_obj.config->asd_sizes[0]; i++) {
		struct v4l2_async_connection *_asd = vpif_obj.config->asd[i];
		const struct fwnode_handle *fwnode = _asd->match.fwnode;

		if (fwnode == subdev->fwnode) {
			vpif_obj.sd[i] = subdev;
			vpif_obj.config->chan_config->inputs[i].subdev_name =
				(char *)to_of_node(subdev->fwnode)->full_name;
			vpif_dbg(2, debug,
				 "%s: setting input %d subdev_name = %s\n",
				 __func__, i,
				vpif_obj.config->chan_config->inputs[i].subdev_name);
			return 0;
		}
	}

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
	struct video_device *vdev;
	struct channel_obj *ch;
	struct vb2_queue *q;
	int j, err, k;

	for (j = 0; j < VPIF_CAPTURE_MAX_DEVICES; j++) {
		ch = vpif_obj.dev[j];
		ch->channel_id = j;
		common = &(ch->common[VPIF_VIDEO_INDEX]);
		spin_lock_init(&common->irqlock);
		mutex_init(&common->lock);

		/* select input 0 */
		err = vpif_set_input(vpif_obj.config, ch, 0);
		if (err)
			goto probe_out;

		/* set initial format */
		ch->video.stdid = V4L2_STD_525_60;
		memset(&ch->video.dv_timings, 0, sizeof(ch->video.dv_timings));
		common->fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		vpif_update_std_info(ch);

		/* Initialize vb2 queue */
		q = &common->buffer_queue;
		q->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		q->io_modes = VB2_MMAP | VB2_USERPTR | VB2_DMABUF;
		q->drv_priv = ch;
		q->ops = &video_qops;
		q->mem_ops = &vb2_dma_contig_memops;
		q->buf_struct_size = sizeof(struct vpif_cap_buffer);
		q->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
		q->min_queued_buffers = 1;
		q->lock = &common->lock;
		q->dev = vpif_dev;

		err = vb2_queue_init(q);
		if (err) {
			vpif_err("vpif_capture: vb2_queue_init() failed\n");
			goto probe_out;
		}

		INIT_LIST_HEAD(&common->dma_queue);

		/* Initialize the video_device structure */
		vdev = &ch->video_dev;
		strscpy(vdev->name, VPIF_DRIVER_NAME, sizeof(vdev->name));
		vdev->release = video_device_release_empty;
		vdev->fops = &vpif_fops;
		vdev->ioctl_ops = &vpif_ioctl_ops;
		vdev->v4l2_dev = &vpif_obj.v4l2_dev;
		vdev->vfl_dir = VFL_DIR_RX;
		vdev->queue = q;
		vdev->lock = &common->lock;
		vdev->device_caps = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING;
		video_set_drvdata(&ch->video_dev, ch);
		err = video_register_device(vdev,
					    VFL_TYPE_VIDEO, (j ? 1 : 0));
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
		video_unregister_device(&ch->video_dev);
	}

	return err;
}

static int vpif_async_complete(struct v4l2_async_notifier *notifier)
{
	return vpif_probe_complete();
}

static const struct v4l2_async_notifier_operations vpif_async_ops = {
	.bound = vpif_async_bound,
	.complete = vpif_async_complete,
};

static struct vpif_capture_config *
vpif_capture_get_pdata(struct platform_device *pdev,
		       struct v4l2_device *v4l2_dev)
{
	struct device_node *endpoint = NULL;
	struct device_node *rem = NULL;
	struct vpif_capture_config *pdata;
	struct vpif_subdev_info *sdinfo;
	struct vpif_capture_chan_config *chan;
	unsigned int i;

	v4l2_async_nf_init(&vpif_obj.notifier, v4l2_dev);

	/*
	 * DT boot: OF node from parent device contains
	 * video ports & endpoints data.
	 */
	if (pdev->dev.parent && pdev->dev.parent->of_node)
		pdev->dev.of_node = pdev->dev.parent->of_node;
	if (!IS_ENABLED(CONFIG_OF) || !pdev->dev.of_node)
		return pdev->dev.platform_data;

	pdata = devm_kzalloc(&pdev->dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		return NULL;
	pdata->subdev_info =
		devm_kcalloc(&pdev->dev,
			     VPIF_CAPTURE_NUM_CHANNELS,
			     sizeof(*pdata->subdev_info),
			     GFP_KERNEL);

	if (!pdata->subdev_info)
		return NULL;

	for (i = 0; i < VPIF_CAPTURE_NUM_CHANNELS; i++) {
		struct v4l2_fwnode_endpoint bus_cfg = { .bus_type = 0 };
		unsigned int flags;
		int err;

		endpoint = of_graph_get_next_endpoint(pdev->dev.of_node,
						      endpoint);
		if (!endpoint)
			break;

		rem = of_graph_get_remote_port_parent(endpoint);
		if (!rem) {
			dev_dbg(&pdev->dev, "Remote device at %pOF not found\n",
				endpoint);
			goto done;
		}

		sdinfo = &pdata->subdev_info[i];
		chan = &pdata->chan_config[i];
		chan->inputs = devm_kcalloc(&pdev->dev,
					    VPIF_CAPTURE_NUM_CHANNELS,
					    sizeof(*chan->inputs),
					    GFP_KERNEL);
		if (!chan->inputs)
			goto err_cleanup;

		chan->input_count++;
		chan->inputs[i].input.type = V4L2_INPUT_TYPE_CAMERA;
		chan->inputs[i].input.std = V4L2_STD_ALL;
		chan->inputs[i].input.capabilities = V4L2_IN_CAP_STD;

		err = v4l2_fwnode_endpoint_parse(of_fwnode_handle(endpoint),
						 &bus_cfg);
		if (err) {
			dev_err(&pdev->dev, "Could not parse the endpoint\n");
			of_node_put(rem);
			goto done;
		}

		dev_dbg(&pdev->dev, "Endpoint %pOF, bus_width = %d\n",
			endpoint, bus_cfg.bus.parallel.bus_width);

		flags = bus_cfg.bus.parallel.flags;

		if (flags & V4L2_MBUS_HSYNC_ACTIVE_HIGH)
			chan->vpif_if.hd_pol = 1;

		if (flags & V4L2_MBUS_VSYNC_ACTIVE_HIGH)
			chan->vpif_if.vd_pol = 1;

		dev_dbg(&pdev->dev, "Remote device %pOF found\n", rem);
		sdinfo->name = rem->full_name;

		pdata->asd[i] = v4l2_async_nf_add_fwnode(&vpif_obj.notifier,
							 of_fwnode_handle(rem),
							 struct v4l2_async_connection);
		if (IS_ERR(pdata->asd[i]))
			goto err_cleanup;

		of_node_put(rem);
	}

done:
	of_node_put(endpoint);
	pdata->asd_sizes[0] = i;
	pdata->subdev_count = i;
	pdata->card_name = "DA850/OMAP-L138 Video Capture";

	return pdata;

err_cleanup:
	of_node_put(rem);
	of_node_put(endpoint);
	v4l2_async_nf_cleanup(&vpif_obj.notifier);

	return NULL;
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
	struct i2c_adapter *i2c_adap;
	int subdev_count;
	int res_idx = 0;
	int i, err;

	vpif_dev = &pdev->dev;

	err = initialize_vpif();
	if (err) {
		v4l2_err(vpif_dev->driver, "Error initializing vpif\n");
		return err;
	}

	err = v4l2_device_register(vpif_dev, &vpif_obj.v4l2_dev);
	if (err) {
		v4l2_err(vpif_dev->driver, "Error registering v4l2 device\n");
		goto vpif_free;
	}

	do {
		int irq;

		err = platform_get_irq_optional(pdev, res_idx);
		if (err < 0 && err != -ENXIO)
			goto vpif_unregister;
		if (err > 0)
			irq = err;
		else
			break;

		err = devm_request_irq(&pdev->dev, irq, vpif_channel_isr,
				       IRQF_SHARED, VPIF_DRIVER_NAME,
				       (void *)(&vpif_obj.dev[res_idx]->channel_id));
		if (err)
			goto vpif_unregister;
	} while (++res_idx);

	pdev->dev.platform_data =
		vpif_capture_get_pdata(pdev, &vpif_obj.v4l2_dev);
	if (!pdev->dev.platform_data) {
		err = -EINVAL;
		dev_warn(&pdev->dev, "Missing platform data. Giving up.\n");
		goto vpif_unregister;
	}

	vpif_obj.config = pdev->dev.platform_data;

	subdev_count = vpif_obj.config->subdev_count;
	vpif_obj.sd = kcalloc(subdev_count, sizeof(*vpif_obj.sd), GFP_KERNEL);
	if (!vpif_obj.sd) {
		err = -ENOMEM;
		goto probe_subdev_out;
	}

	if (!vpif_obj.config->asd_sizes[0]) {
		int i2c_id = vpif_obj.config->i2c_adapter_id;

		i2c_adap = i2c_get_adapter(i2c_id);
		WARN_ON(!i2c_adap);
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
		err = vpif_probe_complete();
		if (err)
			goto probe_subdev_out;
	} else {
		vpif_obj.notifier.ops = &vpif_async_ops;
		err = v4l2_async_nf_register(&vpif_obj.notifier);
		if (err) {
			vpif_err("Error registering async notifier\n");
			err = -EINVAL;
			goto probe_subdev_out;
		}
	}

	return 0;

probe_subdev_out:
	v4l2_async_nf_cleanup(&vpif_obj.notifier);
	/* free sub devices memory */
	kfree(vpif_obj.sd);
vpif_unregister:
	v4l2_device_unregister(&vpif_obj.v4l2_dev);
vpif_free:
	free_vpif_objs();

	return err;
}

/**
 * vpif_remove() - driver remove handler
 * @device: ptr to platform device structure
 *
 * The vidoe device is unregistered
 */
static void vpif_remove(struct platform_device *device)
{
	struct channel_obj *ch;
	int i;

	v4l2_async_nf_unregister(&vpif_obj.notifier);
	v4l2_async_nf_cleanup(&vpif_obj.notifier);
	v4l2_device_unregister(&vpif_obj.v4l2_dev);

	kfree(vpif_obj.sd);
	/* un-register device */
	for (i = 0; i < VPIF_CAPTURE_MAX_DEVICES; i++) {
		/* Get the pointer to the channel object */
		ch = vpif_obj.dev[i];
		/* Unregister video device */
		video_unregister_device(&ch->video_dev);
		kfree(vpif_obj.dev[i]);
	}
}

#ifdef CONFIG_PM_SLEEP
/**
 * vpif_suspend: vpif device suspend
 * @dev: pointer to &struct device
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

		if (!vb2_start_streaming_called(&common->buffer_queue))
			continue;

		mutex_lock(&common->lock);
		/* Disable channel */
		if (ch->channel_id == VPIF_CHANNEL0_VIDEO) {
			enable_channel0(0);
			channel0_intr_enable(0);
		}
		if (ch->channel_id == VPIF_CHANNEL1_VIDEO ||
			ycmux_mode == 2) {
			enable_channel1(0);
			channel1_intr_enable(0);
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

		if (!vb2_start_streaming_called(&common->buffer_queue))
			continue;

		mutex_lock(&common->lock);
		/* Enable channel */
		if (ch->channel_id == VPIF_CHANNEL0_VIDEO) {
			enable_channel0(1);
			channel0_intr_enable(1);
		}
		if (ch->channel_id == VPIF_CHANNEL1_VIDEO ||
			ycmux_mode == 2) {
			enable_channel1(1);
			channel1_intr_enable(1);
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
		.pm	= &vpif_pm_ops,
	},
	.probe = vpif_probe,
	.remove_new = vpif_remove,
};

module_platform_driver(vpif_driver);
