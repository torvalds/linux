// SPDX-License-Identifier: GPL-2.0
/*
 * camss-vfe.c
 *
 * Qualcomm MSM Camera Subsystem - VFE (Video Front End) Module
 *
 * Copyright (c) 2013-2015, The Linux Foundation. All rights reserved.
 * Copyright (C) 2015-2018 Linaro Ltd.
 */
#include <linux/clk.h>
#include <linux/completion.h>
#include <linux/interrupt.h>
#include <linux/iommu.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/spinlock_types.h>
#include <linux/spinlock.h>
#include <media/media-entity.h>
#include <media/v4l2-device.h>
#include <media/v4l2-subdev.h>

#include "camss-vfe.h"
#include "camss.h"

#define MSM_VFE_NAME "msm_vfe"

#define vfe_line_array(ptr_line)	\
	((const struct vfe_line (*)[]) &(ptr_line[-(ptr_line->id)]))

#define to_vfe(ptr_line)	\
	container_of(vfe_line_array(ptr_line), struct vfe_device, line)

/* VFE reset timeout */
#define VFE_RESET_TIMEOUT_MS 50
/* VFE halt timeout */
#define VFE_HALT_TIMEOUT_MS 100
/* Max number of frame drop updates per frame */
#define VFE_FRAME_DROP_UPDATES 2
/* Frame drop value. VAL + UPDATES - 1 should not exceed 31 */
#define VFE_FRAME_DROP_VAL 30

#define VFE_NEXT_SOF_MS 500

#define SCALER_RATIO_MAX 16

struct vfe_format {
	u32 code;
	u8 bpp;
};

static const struct vfe_format formats_rdi_8x16[] = {
	{ MEDIA_BUS_FMT_UYVY8_2X8, 8 },
	{ MEDIA_BUS_FMT_VYUY8_2X8, 8 },
	{ MEDIA_BUS_FMT_YUYV8_2X8, 8 },
	{ MEDIA_BUS_FMT_YVYU8_2X8, 8 },
	{ MEDIA_BUS_FMT_SBGGR8_1X8, 8 },
	{ MEDIA_BUS_FMT_SGBRG8_1X8, 8 },
	{ MEDIA_BUS_FMT_SGRBG8_1X8, 8 },
	{ MEDIA_BUS_FMT_SRGGB8_1X8, 8 },
	{ MEDIA_BUS_FMT_SBGGR10_1X10, 10 },
	{ MEDIA_BUS_FMT_SGBRG10_1X10, 10 },
	{ MEDIA_BUS_FMT_SGRBG10_1X10, 10 },
	{ MEDIA_BUS_FMT_SRGGB10_1X10, 10 },
	{ MEDIA_BUS_FMT_SBGGR12_1X12, 12 },
	{ MEDIA_BUS_FMT_SGBRG12_1X12, 12 },
	{ MEDIA_BUS_FMT_SGRBG12_1X12, 12 },
	{ MEDIA_BUS_FMT_SRGGB12_1X12, 12 },
	{ MEDIA_BUS_FMT_Y10_1X10, 10 },
};

static const struct vfe_format formats_pix_8x16[] = {
	{ MEDIA_BUS_FMT_UYVY8_2X8, 8 },
	{ MEDIA_BUS_FMT_VYUY8_2X8, 8 },
	{ MEDIA_BUS_FMT_YUYV8_2X8, 8 },
	{ MEDIA_BUS_FMT_YVYU8_2X8, 8 },
};

static const struct vfe_format formats_rdi_8x96[] = {
	{ MEDIA_BUS_FMT_UYVY8_2X8, 8 },
	{ MEDIA_BUS_FMT_VYUY8_2X8, 8 },
	{ MEDIA_BUS_FMT_YUYV8_2X8, 8 },
	{ MEDIA_BUS_FMT_YVYU8_2X8, 8 },
	{ MEDIA_BUS_FMT_SBGGR8_1X8, 8 },
	{ MEDIA_BUS_FMT_SGBRG8_1X8, 8 },
	{ MEDIA_BUS_FMT_SGRBG8_1X8, 8 },
	{ MEDIA_BUS_FMT_SRGGB8_1X8, 8 },
	{ MEDIA_BUS_FMT_SBGGR10_1X10, 10 },
	{ MEDIA_BUS_FMT_SGBRG10_1X10, 10 },
	{ MEDIA_BUS_FMT_SGRBG10_1X10, 10 },
	{ MEDIA_BUS_FMT_SRGGB10_1X10, 10 },
	{ MEDIA_BUS_FMT_SBGGR10_2X8_PADHI_LE, 16 },
	{ MEDIA_BUS_FMT_SBGGR12_1X12, 12 },
	{ MEDIA_BUS_FMT_SGBRG12_1X12, 12 },
	{ MEDIA_BUS_FMT_SGRBG12_1X12, 12 },
	{ MEDIA_BUS_FMT_SRGGB12_1X12, 12 },
	{ MEDIA_BUS_FMT_SBGGR14_1X14, 14 },
	{ MEDIA_BUS_FMT_SGBRG14_1X14, 14 },
	{ MEDIA_BUS_FMT_SGRBG14_1X14, 14 },
	{ MEDIA_BUS_FMT_SRGGB14_1X14, 14 },
	{ MEDIA_BUS_FMT_Y10_1X10, 10 },
	{ MEDIA_BUS_FMT_Y10_2X8_PADHI_LE, 16 },
};

static const struct vfe_format formats_pix_8x96[] = {
	{ MEDIA_BUS_FMT_UYVY8_2X8, 8 },
	{ MEDIA_BUS_FMT_VYUY8_2X8, 8 },
	{ MEDIA_BUS_FMT_YUYV8_2X8, 8 },
	{ MEDIA_BUS_FMT_YVYU8_2X8, 8 },
};

/*
 * vfe_get_bpp - map media bus format to bits per pixel
 * @formats: supported media bus formats array
 * @nformats: size of @formats array
 * @code: media bus format code
 *
 * Return number of bits per pixel
 */
static u8 vfe_get_bpp(const struct vfe_format *formats,
		      unsigned int nformats, u32 code)
{
	unsigned int i;

	for (i = 0; i < nformats; i++)
		if (code == formats[i].code)
			return formats[i].bpp;

	WARN(1, "Unknown format\n");

	return formats[0].bpp;
}

static u32 vfe_find_code(u32 *code, unsigned int n_code,
			 unsigned int index, u32 req_code)
{
	int i;

	if (!req_code && (index >= n_code))
		return 0;

	for (i = 0; i < n_code; i++)
		if (req_code) {
			if (req_code == code[i])
				return req_code;
		} else {
			if (i == index)
				return code[i];
		}

	return code[0];
}

static u32 vfe_src_pad_code(struct vfe_line *line, u32 sink_code,
			    unsigned int index, u32 src_req_code)
{
	struct vfe_device *vfe = to_vfe(line);

	if (vfe->camss->version == CAMSS_8x16)
		switch (sink_code) {
		case MEDIA_BUS_FMT_YUYV8_2X8:
		{
			u32 src_code[] = {
				MEDIA_BUS_FMT_YUYV8_2X8,
				MEDIA_BUS_FMT_YUYV8_1_5X8,
			};

			return vfe_find_code(src_code, ARRAY_SIZE(src_code),
					     index, src_req_code);
		}
		case MEDIA_BUS_FMT_YVYU8_2X8:
		{
			u32 src_code[] = {
				MEDIA_BUS_FMT_YVYU8_2X8,
				MEDIA_BUS_FMT_YVYU8_1_5X8,
			};

			return vfe_find_code(src_code, ARRAY_SIZE(src_code),
					     index, src_req_code);
		}
		case MEDIA_BUS_FMT_UYVY8_2X8:
		{
			u32 src_code[] = {
				MEDIA_BUS_FMT_UYVY8_2X8,
				MEDIA_BUS_FMT_UYVY8_1_5X8,
			};

			return vfe_find_code(src_code, ARRAY_SIZE(src_code),
					     index, src_req_code);
		}
		case MEDIA_BUS_FMT_VYUY8_2X8:
		{
			u32 src_code[] = {
				MEDIA_BUS_FMT_VYUY8_2X8,
				MEDIA_BUS_FMT_VYUY8_1_5X8,
			};

			return vfe_find_code(src_code, ARRAY_SIZE(src_code),
					     index, src_req_code);
		}
		default:
			if (index > 0)
				return 0;

			return sink_code;
		}
	else if (vfe->camss->version == CAMSS_8x96)
		switch (sink_code) {
		case MEDIA_BUS_FMT_YUYV8_2X8:
		{
			u32 src_code[] = {
				MEDIA_BUS_FMT_YUYV8_2X8,
				MEDIA_BUS_FMT_YVYU8_2X8,
				MEDIA_BUS_FMT_UYVY8_2X8,
				MEDIA_BUS_FMT_VYUY8_2X8,
				MEDIA_BUS_FMT_YUYV8_1_5X8,
			};

			return vfe_find_code(src_code, ARRAY_SIZE(src_code),
					     index, src_req_code);
		}
		case MEDIA_BUS_FMT_YVYU8_2X8:
		{
			u32 src_code[] = {
				MEDIA_BUS_FMT_YVYU8_2X8,
				MEDIA_BUS_FMT_YUYV8_2X8,
				MEDIA_BUS_FMT_UYVY8_2X8,
				MEDIA_BUS_FMT_VYUY8_2X8,
				MEDIA_BUS_FMT_YVYU8_1_5X8,
			};

			return vfe_find_code(src_code, ARRAY_SIZE(src_code),
					     index, src_req_code);
		}
		case MEDIA_BUS_FMT_UYVY8_2X8:
		{
			u32 src_code[] = {
				MEDIA_BUS_FMT_UYVY8_2X8,
				MEDIA_BUS_FMT_YUYV8_2X8,
				MEDIA_BUS_FMT_YVYU8_2X8,
				MEDIA_BUS_FMT_VYUY8_2X8,
				MEDIA_BUS_FMT_UYVY8_1_5X8,
			};

			return vfe_find_code(src_code, ARRAY_SIZE(src_code),
					     index, src_req_code);
		}
		case MEDIA_BUS_FMT_VYUY8_2X8:
		{
			u32 src_code[] = {
				MEDIA_BUS_FMT_VYUY8_2X8,
				MEDIA_BUS_FMT_YUYV8_2X8,
				MEDIA_BUS_FMT_YVYU8_2X8,
				MEDIA_BUS_FMT_UYVY8_2X8,
				MEDIA_BUS_FMT_VYUY8_1_5X8,
			};

			return vfe_find_code(src_code, ARRAY_SIZE(src_code),
					     index, src_req_code);
		}
		default:
			if (index > 0)
				return 0;

			return sink_code;
		}
	else
		return 0;
}

/*
 * vfe_reset - Trigger reset on VFE module and wait to complete
 * @vfe: VFE device
 *
 * Return 0 on success or a negative error code otherwise
 */
static int vfe_reset(struct vfe_device *vfe)
{
	unsigned long time;

	reinit_completion(&vfe->reset_complete);

	vfe->ops->global_reset(vfe);

	time = wait_for_completion_timeout(&vfe->reset_complete,
		msecs_to_jiffies(VFE_RESET_TIMEOUT_MS));
	if (!time) {
		dev_err(vfe->camss->dev, "VFE reset timeout\n");
		return -EIO;
	}

	return 0;
}

/*
 * vfe_halt - Trigger halt on VFE module and wait to complete
 * @vfe: VFE device
 *
 * Return 0 on success or a negative error code otherwise
 */
static int vfe_halt(struct vfe_device *vfe)
{
	unsigned long time;

	reinit_completion(&vfe->halt_complete);

	vfe->ops->halt_request(vfe);

	time = wait_for_completion_timeout(&vfe->halt_complete,
		msecs_to_jiffies(VFE_HALT_TIMEOUT_MS));
	if (!time) {
		dev_err(vfe->camss->dev, "VFE halt timeout\n");
		return -EIO;
	}

	return 0;
}

static void vfe_init_outputs(struct vfe_device *vfe)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(vfe->line); i++) {
		struct vfe_output *output = &vfe->line[i].output;

		output->state = VFE_OUTPUT_OFF;
		output->buf[0] = NULL;
		output->buf[1] = NULL;
		INIT_LIST_HEAD(&output->pending_bufs);
	}
}

static void vfe_reset_output_maps(struct vfe_device *vfe)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(vfe->wm_output_map); i++)
		vfe->wm_output_map[i] = VFE_LINE_NONE;
}

static void vfe_output_init_addrs(struct vfe_device *vfe,
				  struct vfe_output *output, u8 sync)
{
	u32 ping_addr;
	u32 pong_addr;
	unsigned int i;

	output->active_buf = 0;

	for (i = 0; i < output->wm_num; i++) {
		if (output->buf[0])
			ping_addr = output->buf[0]->addr[i];
		else
			ping_addr = 0;

		if (output->buf[1])
			pong_addr = output->buf[1]->addr[i];
		else
			pong_addr = ping_addr;

		vfe->ops->wm_set_ping_addr(vfe, output->wm_idx[i], ping_addr);
		vfe->ops->wm_set_pong_addr(vfe, output->wm_idx[i], pong_addr);
		if (sync)
			vfe->ops->bus_reload_wm(vfe, output->wm_idx[i]);
	}
}

static void vfe_output_update_ping_addr(struct vfe_device *vfe,
					struct vfe_output *output, u8 sync)
{
	u32 addr;
	unsigned int i;

	for (i = 0; i < output->wm_num; i++) {
		if (output->buf[0])
			addr = output->buf[0]->addr[i];
		else
			addr = 0;

		vfe->ops->wm_set_ping_addr(vfe, output->wm_idx[i], addr);
		if (sync)
			vfe->ops->bus_reload_wm(vfe, output->wm_idx[i]);
	}
}

static void vfe_output_update_pong_addr(struct vfe_device *vfe,
					struct vfe_output *output, u8 sync)
{
	u32 addr;
	unsigned int i;

	for (i = 0; i < output->wm_num; i++) {
		if (output->buf[1])
			addr = output->buf[1]->addr[i];
		else
			addr = 0;

		vfe->ops->wm_set_pong_addr(vfe, output->wm_idx[i], addr);
		if (sync)
			vfe->ops->bus_reload_wm(vfe, output->wm_idx[i]);
	}

}

static int vfe_reserve_wm(struct vfe_device *vfe, enum vfe_line_id line_id)
{
	int ret = -EBUSY;
	int i;

	for (i = 0; i < ARRAY_SIZE(vfe->wm_output_map); i++) {
		if (vfe->wm_output_map[i] == VFE_LINE_NONE) {
			vfe->wm_output_map[i] = line_id;
			ret = i;
			break;
		}
	}

	return ret;
}

static int vfe_release_wm(struct vfe_device *vfe, u8 wm)
{
	if (wm >= ARRAY_SIZE(vfe->wm_output_map))
		return -EINVAL;

	vfe->wm_output_map[wm] = VFE_LINE_NONE;

	return 0;
}

static void vfe_output_frame_drop(struct vfe_device *vfe,
				  struct vfe_output *output,
				  u32 drop_pattern)
{
	u8 drop_period;
	unsigned int i;

	/* We need to toggle update period to be valid on next frame */
	output->drop_update_idx++;
	output->drop_update_idx %= VFE_FRAME_DROP_UPDATES;
	drop_period = VFE_FRAME_DROP_VAL + output->drop_update_idx;

	for (i = 0; i < output->wm_num; i++) {
		vfe->ops->wm_set_framedrop_period(vfe, output->wm_idx[i],
						  drop_period);
		vfe->ops->wm_set_framedrop_pattern(vfe, output->wm_idx[i],
						   drop_pattern);
	}
	vfe->ops->reg_update(vfe,
			     container_of(output, struct vfe_line, output)->id);
}

static struct camss_buffer *vfe_buf_get_pending(struct vfe_output *output)
{
	struct camss_buffer *buffer = NULL;

	if (!list_empty(&output->pending_bufs)) {
		buffer = list_first_entry(&output->pending_bufs,
					  struct camss_buffer,
					  queue);
		list_del(&buffer->queue);
	}

	return buffer;
}

/*
 * vfe_buf_add_pending - Add output buffer to list of pending
 * @output: VFE output
 * @buffer: Video buffer
 */
static void vfe_buf_add_pending(struct vfe_output *output,
				struct camss_buffer *buffer)
{
	INIT_LIST_HEAD(&buffer->queue);
	list_add_tail(&buffer->queue, &output->pending_bufs);
}

/*
 * vfe_buf_flush_pending - Flush all pending buffers.
 * @output: VFE output
 * @state: vb2 buffer state
 */
static void vfe_buf_flush_pending(struct vfe_output *output,
				  enum vb2_buffer_state state)
{
	struct camss_buffer *buf;
	struct camss_buffer *t;

	list_for_each_entry_safe(buf, t, &output->pending_bufs, queue) {
		vb2_buffer_done(&buf->vb.vb2_buf, state);
		list_del(&buf->queue);
	}
}

static void vfe_buf_update_wm_on_next(struct vfe_device *vfe,
				      struct vfe_output *output)
{
	switch (output->state) {
	case VFE_OUTPUT_CONTINUOUS:
		vfe_output_frame_drop(vfe, output, 3);
		break;
	case VFE_OUTPUT_SINGLE:
	default:
		dev_err_ratelimited(vfe->camss->dev,
				    "Next buf in wrong state! %d\n",
				    output->state);
		break;
	}
}

static void vfe_buf_update_wm_on_last(struct vfe_device *vfe,
				      struct vfe_output *output)
{
	switch (output->state) {
	case VFE_OUTPUT_CONTINUOUS:
		output->state = VFE_OUTPUT_SINGLE;
		vfe_output_frame_drop(vfe, output, 1);
		break;
	case VFE_OUTPUT_SINGLE:
		output->state = VFE_OUTPUT_STOPPING;
		vfe_output_frame_drop(vfe, output, 0);
		break;
	default:
		dev_err_ratelimited(vfe->camss->dev,
				    "Last buff in wrong state! %d\n",
				    output->state);
		break;
	}
}

static void vfe_buf_update_wm_on_new(struct vfe_device *vfe,
				     struct vfe_output *output,
				     struct camss_buffer *new_buf)
{
	int inactive_idx;

	switch (output->state) {
	case VFE_OUTPUT_SINGLE:
		inactive_idx = !output->active_buf;

		if (!output->buf[inactive_idx]) {
			output->buf[inactive_idx] = new_buf;

			if (inactive_idx)
				vfe_output_update_pong_addr(vfe, output, 0);
			else
				vfe_output_update_ping_addr(vfe, output, 0);

			vfe_output_frame_drop(vfe, output, 3);
			output->state = VFE_OUTPUT_CONTINUOUS;
		} else {
			vfe_buf_add_pending(output, new_buf);
			dev_err_ratelimited(vfe->camss->dev,
					    "Inactive buffer is busy\n");
		}
		break;

	case VFE_OUTPUT_IDLE:
		if (!output->buf[0]) {
			output->buf[0] = new_buf;

			vfe_output_init_addrs(vfe, output, 1);

			vfe_output_frame_drop(vfe, output, 1);
			output->state = VFE_OUTPUT_SINGLE;
		} else {
			vfe_buf_add_pending(output, new_buf);
			dev_err_ratelimited(vfe->camss->dev,
					    "Output idle with buffer set!\n");
		}
		break;

	case VFE_OUTPUT_CONTINUOUS:
	default:
		vfe_buf_add_pending(output, new_buf);
		break;
	}
}

static int vfe_get_output(struct vfe_line *line)
{
	struct vfe_device *vfe = to_vfe(line);
	struct vfe_output *output;
	struct v4l2_format *f = &line->video_out.active_fmt;
	unsigned long flags;
	int i;
	int wm_idx;

	spin_lock_irqsave(&vfe->output_lock, flags);

	output = &line->output;
	if (output->state != VFE_OUTPUT_OFF) {
		dev_err(vfe->camss->dev, "Output is running\n");
		goto error;
	}
	output->state = VFE_OUTPUT_RESERVED;

	output->active_buf = 0;

	switch (f->fmt.pix_mp.pixelformat) {
	case V4L2_PIX_FMT_NV12:
	case V4L2_PIX_FMT_NV21:
	case V4L2_PIX_FMT_NV16:
	case V4L2_PIX_FMT_NV61:
		output->wm_num = 2;
		break;
	default:
		output->wm_num = 1;
		break;
	}

	for (i = 0; i < output->wm_num; i++) {
		wm_idx = vfe_reserve_wm(vfe, line->id);
		if (wm_idx < 0) {
			dev_err(vfe->camss->dev, "Can not reserve wm\n");
			goto error_get_wm;
		}
		output->wm_idx[i] = wm_idx;
	}

	output->drop_update_idx = 0;

	spin_unlock_irqrestore(&vfe->output_lock, flags);

	return 0;

error_get_wm:
	for (i--; i >= 0; i--)
		vfe_release_wm(vfe, output->wm_idx[i]);
	output->state = VFE_OUTPUT_OFF;
error:
	spin_unlock_irqrestore(&vfe->output_lock, flags);

	return -EINVAL;
}

static int vfe_put_output(struct vfe_line *line)
{
	struct vfe_device *vfe = to_vfe(line);
	struct vfe_output *output = &line->output;
	unsigned long flags;
	unsigned int i;

	spin_lock_irqsave(&vfe->output_lock, flags);

	for (i = 0; i < output->wm_num; i++)
		vfe_release_wm(vfe, output->wm_idx[i]);

	output->state = VFE_OUTPUT_OFF;

	spin_unlock_irqrestore(&vfe->output_lock, flags);
	return 0;
}

static int vfe_enable_output(struct vfe_line *line)
{
	struct vfe_device *vfe = to_vfe(line);
	struct vfe_output *output = &line->output;
	const struct vfe_hw_ops *ops = vfe->ops;
	struct media_entity *sensor;
	unsigned long flags;
	unsigned int frame_skip = 0;
	unsigned int i;
	u16 ub_size;

	ub_size = ops->get_ub_size(vfe->id);
	if (!ub_size)
		return -EINVAL;

	sensor = camss_find_sensor(&line->subdev.entity);
	if (sensor) {
		struct v4l2_subdev *subdev =
					media_entity_to_v4l2_subdev(sensor);

		v4l2_subdev_call(subdev, sensor, g_skip_frames, &frame_skip);
		/* Max frame skip is 29 frames */
		if (frame_skip > VFE_FRAME_DROP_VAL - 1)
			frame_skip = VFE_FRAME_DROP_VAL - 1;
	}

	spin_lock_irqsave(&vfe->output_lock, flags);

	ops->reg_update_clear(vfe, line->id);

	if (output->state != VFE_OUTPUT_RESERVED) {
		dev_err(vfe->camss->dev, "Output is not in reserved state %d\n",
			output->state);
		spin_unlock_irqrestore(&vfe->output_lock, flags);
		return -EINVAL;
	}
	output->state = VFE_OUTPUT_IDLE;

	output->buf[0] = vfe_buf_get_pending(output);
	output->buf[1] = vfe_buf_get_pending(output);

	if (!output->buf[0] && output->buf[1]) {
		output->buf[0] = output->buf[1];
		output->buf[1] = NULL;
	}

	if (output->buf[0])
		output->state = VFE_OUTPUT_SINGLE;

	if (output->buf[1])
		output->state = VFE_OUTPUT_CONTINUOUS;

	switch (output->state) {
	case VFE_OUTPUT_SINGLE:
		vfe_output_frame_drop(vfe, output, 1 << frame_skip);
		break;
	case VFE_OUTPUT_CONTINUOUS:
		vfe_output_frame_drop(vfe, output, 3 << frame_skip);
		break;
	default:
		vfe_output_frame_drop(vfe, output, 0);
		break;
	}

	output->sequence = 0;
	output->wait_sof = 0;
	output->wait_reg_update = 0;
	reinit_completion(&output->sof);
	reinit_completion(&output->reg_update);

	vfe_output_init_addrs(vfe, output, 0);

	if (line->id != VFE_LINE_PIX) {
		ops->set_cgc_override(vfe, output->wm_idx[0], 1);
		ops->enable_irq_wm_line(vfe, output->wm_idx[0], line->id, 1);
		ops->bus_connect_wm_to_rdi(vfe, output->wm_idx[0], line->id);
		ops->wm_set_subsample(vfe, output->wm_idx[0]);
		ops->set_rdi_cid(vfe, line->id, 0);
		ops->wm_set_ub_cfg(vfe, output->wm_idx[0],
				   (ub_size + 1) * output->wm_idx[0], ub_size);
		ops->wm_frame_based(vfe, output->wm_idx[0], 1);
		ops->wm_enable(vfe, output->wm_idx[0], 1);
		ops->bus_reload_wm(vfe, output->wm_idx[0]);
	} else {
		ub_size /= output->wm_num;
		for (i = 0; i < output->wm_num; i++) {
			ops->set_cgc_override(vfe, output->wm_idx[i], 1);
			ops->wm_set_subsample(vfe, output->wm_idx[i]);
			ops->wm_set_ub_cfg(vfe, output->wm_idx[i],
					   (ub_size + 1) * output->wm_idx[i],
					   ub_size);
			ops->wm_line_based(vfe, output->wm_idx[i],
					&line->video_out.active_fmt.fmt.pix_mp,
					i, 1);
			ops->wm_enable(vfe, output->wm_idx[i], 1);
			ops->bus_reload_wm(vfe, output->wm_idx[i]);
		}
		ops->enable_irq_pix_line(vfe, 0, line->id, 1);
		ops->set_module_cfg(vfe, 1);
		ops->set_camif_cfg(vfe, line);
		ops->set_realign_cfg(vfe, line, 1);
		ops->set_xbar_cfg(vfe, output, 1);
		ops->set_demux_cfg(vfe, line);
		ops->set_scale_cfg(vfe, line);
		ops->set_crop_cfg(vfe, line);
		ops->set_clamp_cfg(vfe);
		ops->set_camif_cmd(vfe, 1);
	}

	ops->reg_update(vfe, line->id);

	spin_unlock_irqrestore(&vfe->output_lock, flags);

	return 0;
}

static int vfe_disable_output(struct vfe_line *line)
{
	struct vfe_device *vfe = to_vfe(line);
	struct vfe_output *output = &line->output;
	const struct vfe_hw_ops *ops = vfe->ops;
	unsigned long flags;
	unsigned long time;
	unsigned int i;

	spin_lock_irqsave(&vfe->output_lock, flags);

	output->wait_sof = 1;
	spin_unlock_irqrestore(&vfe->output_lock, flags);

	time = wait_for_completion_timeout(&output->sof,
					   msecs_to_jiffies(VFE_NEXT_SOF_MS));
	if (!time)
		dev_err(vfe->camss->dev, "VFE sof timeout\n");

	spin_lock_irqsave(&vfe->output_lock, flags);
	for (i = 0; i < output->wm_num; i++)
		ops->wm_enable(vfe, output->wm_idx[i], 0);

	ops->reg_update(vfe, line->id);
	output->wait_reg_update = 1;
	spin_unlock_irqrestore(&vfe->output_lock, flags);

	time = wait_for_completion_timeout(&output->reg_update,
					   msecs_to_jiffies(VFE_NEXT_SOF_MS));
	if (!time)
		dev_err(vfe->camss->dev, "VFE reg update timeout\n");

	spin_lock_irqsave(&vfe->output_lock, flags);

	if (line->id != VFE_LINE_PIX) {
		ops->wm_frame_based(vfe, output->wm_idx[0], 0);
		ops->bus_disconnect_wm_from_rdi(vfe, output->wm_idx[0],
						line->id);
		ops->enable_irq_wm_line(vfe, output->wm_idx[0], line->id, 0);
		ops->set_cgc_override(vfe, output->wm_idx[0], 0);
		spin_unlock_irqrestore(&vfe->output_lock, flags);
	} else {
		for (i = 0; i < output->wm_num; i++) {
			ops->wm_line_based(vfe, output->wm_idx[i], NULL, i, 0);
			ops->set_cgc_override(vfe, output->wm_idx[i], 0);
		}

		ops->enable_irq_pix_line(vfe, 0, line->id, 0);
		ops->set_module_cfg(vfe, 0);
		ops->set_realign_cfg(vfe, line, 0);
		ops->set_xbar_cfg(vfe, output, 0);

		ops->set_camif_cmd(vfe, 0);
		spin_unlock_irqrestore(&vfe->output_lock, flags);

		ops->camif_wait_for_stop(vfe, vfe->camss->dev);
	}

	return 0;
}

/*
 * vfe_enable - Enable streaming on VFE line
 * @line: VFE line
 *
 * Return 0 on success or a negative error code otherwise
 */
static int vfe_enable(struct vfe_line *line)
{
	struct vfe_device *vfe = to_vfe(line);
	int ret;

	mutex_lock(&vfe->stream_lock);

	if (!vfe->stream_count) {
		vfe->ops->enable_irq_common(vfe);

		vfe->ops->bus_enable_wr_if(vfe, 1);

		vfe->ops->set_qos(vfe);

		vfe->ops->set_ds(vfe);
	}

	vfe->stream_count++;

	mutex_unlock(&vfe->stream_lock);

	ret = vfe_get_output(line);
	if (ret < 0)
		goto error_get_output;

	ret = vfe_enable_output(line);
	if (ret < 0)
		goto error_enable_output;

	vfe->was_streaming = 1;

	return 0;


error_enable_output:
	vfe_put_output(line);

error_get_output:
	mutex_lock(&vfe->stream_lock);

	if (vfe->stream_count == 1)
		vfe->ops->bus_enable_wr_if(vfe, 0);

	vfe->stream_count--;

	mutex_unlock(&vfe->stream_lock);

	return ret;
}

/*
 * vfe_disable - Disable streaming on VFE line
 * @line: VFE line
 *
 * Return 0 on success or a negative error code otherwise
 */
static int vfe_disable(struct vfe_line *line)
{
	struct vfe_device *vfe = to_vfe(line);

	vfe_disable_output(line);

	vfe_put_output(line);

	mutex_lock(&vfe->stream_lock);

	if (vfe->stream_count == 1)
		vfe->ops->bus_enable_wr_if(vfe, 0);

	vfe->stream_count--;

	mutex_unlock(&vfe->stream_lock);

	return 0;
}

/*
 * vfe_isr_sof - Process start of frame interrupt
 * @vfe: VFE Device
 * @line_id: VFE line
 */
static void vfe_isr_sof(struct vfe_device *vfe, enum vfe_line_id line_id)
{
	struct vfe_output *output;
	unsigned long flags;

	spin_lock_irqsave(&vfe->output_lock, flags);
	output = &vfe->line[line_id].output;
	if (output->wait_sof) {
		output->wait_sof = 0;
		complete(&output->sof);
	}
	spin_unlock_irqrestore(&vfe->output_lock, flags);
}

/*
 * vfe_isr_reg_update - Process reg update interrupt
 * @vfe: VFE Device
 * @line_id: VFE line
 */
static void vfe_isr_reg_update(struct vfe_device *vfe, enum vfe_line_id line_id)
{
	struct vfe_output *output;
	unsigned long flags;

	spin_lock_irqsave(&vfe->output_lock, flags);
	vfe->ops->reg_update_clear(vfe, line_id);

	output = &vfe->line[line_id].output;

	if (output->wait_reg_update) {
		output->wait_reg_update = 0;
		complete(&output->reg_update);
		spin_unlock_irqrestore(&vfe->output_lock, flags);
		return;
	}

	if (output->state == VFE_OUTPUT_STOPPING) {
		/* Release last buffer when hw is idle */
		if (output->last_buffer) {
			vb2_buffer_done(&output->last_buffer->vb.vb2_buf,
					VB2_BUF_STATE_DONE);
			output->last_buffer = NULL;
		}
		output->state = VFE_OUTPUT_IDLE;

		/* Buffers received in stopping state are queued in */
		/* dma pending queue, start next capture here */

		output->buf[0] = vfe_buf_get_pending(output);
		output->buf[1] = vfe_buf_get_pending(output);

		if (!output->buf[0] && output->buf[1]) {
			output->buf[0] = output->buf[1];
			output->buf[1] = NULL;
		}

		if (output->buf[0])
			output->state = VFE_OUTPUT_SINGLE;

		if (output->buf[1])
			output->state = VFE_OUTPUT_CONTINUOUS;

		switch (output->state) {
		case VFE_OUTPUT_SINGLE:
			vfe_output_frame_drop(vfe, output, 2);
			break;
		case VFE_OUTPUT_CONTINUOUS:
			vfe_output_frame_drop(vfe, output, 3);
			break;
		default:
			vfe_output_frame_drop(vfe, output, 0);
			break;
		}

		vfe_output_init_addrs(vfe, output, 1);
	}

	spin_unlock_irqrestore(&vfe->output_lock, flags);
}

/*
 * vfe_isr_wm_done - Process write master done interrupt
 * @vfe: VFE Device
 * @wm: Write master id
 */
static void vfe_isr_wm_done(struct vfe_device *vfe, u8 wm)
{
	struct camss_buffer *ready_buf;
	struct vfe_output *output;
	dma_addr_t *new_addr;
	unsigned long flags;
	u32 active_index;
	u64 ts = ktime_get_ns();
	unsigned int i;

	active_index = vfe->ops->wm_get_ping_pong_status(vfe, wm);

	spin_lock_irqsave(&vfe->output_lock, flags);

	if (vfe->wm_output_map[wm] == VFE_LINE_NONE) {
		dev_err_ratelimited(vfe->camss->dev,
				    "Received wm done for unmapped index\n");
		goto out_unlock;
	}
	output = &vfe->line[vfe->wm_output_map[wm]].output;

	if (output->active_buf == active_index) {
		dev_err_ratelimited(vfe->camss->dev,
				    "Active buffer mismatch!\n");
		goto out_unlock;
	}
	output->active_buf = active_index;

	ready_buf = output->buf[!active_index];
	if (!ready_buf) {
		dev_err_ratelimited(vfe->camss->dev,
				    "Missing ready buf %d %d!\n",
				    !active_index, output->state);
		goto out_unlock;
	}

	ready_buf->vb.vb2_buf.timestamp = ts;
	ready_buf->vb.sequence = output->sequence++;

	/* Get next buffer */
	output->buf[!active_index] = vfe_buf_get_pending(output);
	if (!output->buf[!active_index]) {
		/* No next buffer - set same address */
		new_addr = ready_buf->addr;
		vfe_buf_update_wm_on_last(vfe, output);
	} else {
		new_addr = output->buf[!active_index]->addr;
		vfe_buf_update_wm_on_next(vfe, output);
	}

	if (active_index)
		for (i = 0; i < output->wm_num; i++)
			vfe->ops->wm_set_ping_addr(vfe, output->wm_idx[i],
						   new_addr[i]);
	else
		for (i = 0; i < output->wm_num; i++)
			vfe->ops->wm_set_pong_addr(vfe, output->wm_idx[i],
						   new_addr[i]);

	spin_unlock_irqrestore(&vfe->output_lock, flags);

	if (output->state == VFE_OUTPUT_STOPPING)
		output->last_buffer = ready_buf;
	else
		vb2_buffer_done(&ready_buf->vb.vb2_buf, VB2_BUF_STATE_DONE);

	return;

out_unlock:
	spin_unlock_irqrestore(&vfe->output_lock, flags);
}

/*
 * vfe_isr_wm_done - Process composite image done interrupt
 * @vfe: VFE Device
 * @comp: Composite image id
 */
static void vfe_isr_comp_done(struct vfe_device *vfe, u8 comp)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(vfe->wm_output_map); i++)
		if (vfe->wm_output_map[i] == VFE_LINE_PIX) {
			vfe_isr_wm_done(vfe, i);
			break;
		}
}

static inline void vfe_isr_reset_ack(struct vfe_device *vfe)
{
	complete(&vfe->reset_complete);
}

static inline void vfe_isr_halt_ack(struct vfe_device *vfe)
{
	complete(&vfe->halt_complete);
	vfe->ops->halt_clear(vfe);
}

/*
 * vfe_set_clock_rates - Calculate and set clock rates on VFE module
 * @vfe: VFE device
 *
 * Return 0 on success or a negative error code otherwise
 */
static int vfe_set_clock_rates(struct vfe_device *vfe)
{
	struct device *dev = vfe->camss->dev;
	u32 pixel_clock[MSM_VFE_LINE_NUM];
	int i, j;
	int ret;

	for (i = VFE_LINE_RDI0; i <= VFE_LINE_PIX; i++) {
		ret = camss_get_pixel_clock(&vfe->line[i].subdev.entity,
					    &pixel_clock[i]);
		if (ret)
			pixel_clock[i] = 0;
	}

	for (i = 0; i < vfe->nclocks; i++) {
		struct camss_clock *clock = &vfe->clock[i];

		if (!strcmp(clock->name, "vfe0") ||
		    !strcmp(clock->name, "vfe1")) {
			u64 min_rate = 0;
			long rate;

			for (j = VFE_LINE_RDI0; j <= VFE_LINE_PIX; j++) {
				u32 tmp;
				u8 bpp;

				if (j == VFE_LINE_PIX) {
					tmp = pixel_clock[j];
				} else {
					struct vfe_line *l = &vfe->line[j];

					bpp = vfe_get_bpp(l->formats,
						l->nformats,
						l->fmt[MSM_VFE_PAD_SINK].code);
					tmp = pixel_clock[j] * bpp / 64;
				}

				if (min_rate < tmp)
					min_rate = tmp;
			}

			camss_add_clock_margin(&min_rate);

			for (j = 0; j < clock->nfreqs; j++)
				if (min_rate < clock->freq[j])
					break;

			if (j == clock->nfreqs) {
				dev_err(dev,
					"Pixel clock is too high for VFE");
				return -EINVAL;
			}

			/* if sensor pixel clock is not available */
			/* set highest possible VFE clock rate */
			if (min_rate == 0)
				j = clock->nfreqs - 1;

			rate = clk_round_rate(clock->clk, clock->freq[j]);
			if (rate < 0) {
				dev_err(dev, "clk round rate failed: %ld\n",
					rate);
				return -EINVAL;
			}

			ret = clk_set_rate(clock->clk, rate);
			if (ret < 0) {
				dev_err(dev, "clk set rate failed: %d\n", ret);
				return ret;
			}
		}
	}

	return 0;
}

/*
 * vfe_check_clock_rates - Check current clock rates on VFE module
 * @vfe: VFE device
 *
 * Return 0 if current clock rates are suitable for a new pipeline
 * or a negative error code otherwise
 */
static int vfe_check_clock_rates(struct vfe_device *vfe)
{
	u32 pixel_clock[MSM_VFE_LINE_NUM];
	int i, j;
	int ret;

	for (i = VFE_LINE_RDI0; i <= VFE_LINE_PIX; i++) {
		ret = camss_get_pixel_clock(&vfe->line[i].subdev.entity,
					    &pixel_clock[i]);
		if (ret)
			pixel_clock[i] = 0;
	}

	for (i = 0; i < vfe->nclocks; i++) {
		struct camss_clock *clock = &vfe->clock[i];

		if (!strcmp(clock->name, "vfe0") ||
		    !strcmp(clock->name, "vfe1")) {
			u64 min_rate = 0;
			unsigned long rate;

			for (j = VFE_LINE_RDI0; j <= VFE_LINE_PIX; j++) {
				u32 tmp;
				u8 bpp;

				if (j == VFE_LINE_PIX) {
					tmp = pixel_clock[j];
				} else {
					struct vfe_line *l = &vfe->line[j];

					bpp = vfe_get_bpp(l->formats,
						l->nformats,
						l->fmt[MSM_VFE_PAD_SINK].code);
					tmp = pixel_clock[j] * bpp / 64;
				}

				if (min_rate < tmp)
					min_rate = tmp;
			}

			camss_add_clock_margin(&min_rate);

			rate = clk_get_rate(clock->clk);
			if (rate < min_rate)
				return -EBUSY;
		}
	}

	return 0;
}

/*
 * vfe_get - Power up and reset VFE module
 * @vfe: VFE Device
 *
 * Return 0 on success or a negative error code otherwise
 */
static int vfe_get(struct vfe_device *vfe)
{
	int ret;

	mutex_lock(&vfe->power_lock);

	if (vfe->power_count == 0) {
		ret = camss_pm_domain_on(vfe->camss, vfe->id);
		if (ret < 0)
			goto error_pm_domain;

		ret = pm_runtime_get_sync(vfe->camss->dev);
		if (ret < 0)
			goto error_pm_runtime_get;

		ret = vfe_set_clock_rates(vfe);
		if (ret < 0)
			goto error_pm_runtime_get;

		ret = camss_enable_clocks(vfe->nclocks, vfe->clock,
					  vfe->camss->dev);
		if (ret < 0)
			goto error_pm_runtime_get;

		ret = vfe_reset(vfe);
		if (ret < 0)
			goto error_reset;

		vfe_reset_output_maps(vfe);

		vfe_init_outputs(vfe);
	} else {
		ret = vfe_check_clock_rates(vfe);
		if (ret < 0)
			goto error_pm_runtime_get;
	}
	vfe->power_count++;

	mutex_unlock(&vfe->power_lock);

	return 0;

error_reset:
	camss_disable_clocks(vfe->nclocks, vfe->clock);

error_pm_runtime_get:
	pm_runtime_put_sync(vfe->camss->dev);
	camss_pm_domain_off(vfe->camss, vfe->id);

error_pm_domain:
	mutex_unlock(&vfe->power_lock);

	return ret;
}

/*
 * vfe_put - Power down VFE module
 * @vfe: VFE Device
 */
static void vfe_put(struct vfe_device *vfe)
{
	mutex_lock(&vfe->power_lock);

	if (vfe->power_count == 0) {
		dev_err(vfe->camss->dev, "vfe power off on power_count == 0\n");
		goto exit;
	} else if (vfe->power_count == 1) {
		if (vfe->was_streaming) {
			vfe->was_streaming = 0;
			vfe_halt(vfe);
		}
		camss_disable_clocks(vfe->nclocks, vfe->clock);
		pm_runtime_put_sync(vfe->camss->dev);
		camss_pm_domain_off(vfe->camss, vfe->id);
	}

	vfe->power_count--;

exit:
	mutex_unlock(&vfe->power_lock);
}

/*
 * vfe_queue_buffer - Add empty buffer
 * @vid: Video device structure
 * @buf: Buffer to be enqueued
 *
 * Add an empty buffer - depending on the current number of buffers it will be
 * put in pending buffer queue or directly given to the hardware to be filled.
 *
 * Return 0 on success or a negative error code otherwise
 */
static int vfe_queue_buffer(struct camss_video *vid,
			    struct camss_buffer *buf)
{
	struct vfe_line *line = container_of(vid, struct vfe_line, video_out);
	struct vfe_device *vfe = to_vfe(line);
	struct vfe_output *output;
	unsigned long flags;

	output = &line->output;

	spin_lock_irqsave(&vfe->output_lock, flags);

	vfe_buf_update_wm_on_new(vfe, output, buf);

	spin_unlock_irqrestore(&vfe->output_lock, flags);

	return 0;
}

/*
 * vfe_flush_buffers - Return all vb2 buffers
 * @vid: Video device structure
 * @state: vb2 buffer state of the returned buffers
 *
 * Return all buffers to vb2. This includes queued pending buffers (still
 * unused) and any buffers given to the hardware but again still not used.
 *
 * Return 0 on success or a negative error code otherwise
 */
static int vfe_flush_buffers(struct camss_video *vid,
			     enum vb2_buffer_state state)
{
	struct vfe_line *line = container_of(vid, struct vfe_line, video_out);
	struct vfe_device *vfe = to_vfe(line);
	struct vfe_output *output;
	unsigned long flags;

	output = &line->output;

	spin_lock_irqsave(&vfe->output_lock, flags);

	vfe_buf_flush_pending(output, state);

	if (output->buf[0])
		vb2_buffer_done(&output->buf[0]->vb.vb2_buf, state);

	if (output->buf[1])
		vb2_buffer_done(&output->buf[1]->vb.vb2_buf, state);

	if (output->last_buffer) {
		vb2_buffer_done(&output->last_buffer->vb.vb2_buf, state);
		output->last_buffer = NULL;
	}

	spin_unlock_irqrestore(&vfe->output_lock, flags);

	return 0;
}

/*
 * vfe_set_power - Power on/off VFE module
 * @sd: VFE V4L2 subdevice
 * @on: Requested power state
 *
 * Return 0 on success or a negative error code otherwise
 */
static int vfe_set_power(struct v4l2_subdev *sd, int on)
{
	struct vfe_line *line = v4l2_get_subdevdata(sd);
	struct vfe_device *vfe = to_vfe(line);
	int ret;

	if (on) {
		ret = vfe_get(vfe);
		if (ret < 0)
			return ret;

		vfe->ops->hw_version_read(vfe, vfe->camss->dev);
	} else {
		vfe_put(vfe);
	}

	return 0;
}

/*
 * vfe_set_stream - Enable/disable streaming on VFE module
 * @sd: VFE V4L2 subdevice
 * @enable: Requested streaming state
 *
 * Main configuration of VFE module is triggered here.
 *
 * Return 0 on success or a negative error code otherwise
 */
static int vfe_set_stream(struct v4l2_subdev *sd, int enable)
{
	struct vfe_line *line = v4l2_get_subdevdata(sd);
	struct vfe_device *vfe = to_vfe(line);
	int ret;

	if (enable) {
		ret = vfe_enable(line);
		if (ret < 0)
			dev_err(vfe->camss->dev,
				"Failed to enable vfe outputs\n");
	} else {
		ret = vfe_disable(line);
		if (ret < 0)
			dev_err(vfe->camss->dev,
				"Failed to disable vfe outputs\n");
	}

	return ret;
}

/*
 * __vfe_get_format - Get pointer to format structure
 * @line: VFE line
 * @cfg: V4L2 subdev pad configuration
 * @pad: pad from which format is requested
 * @which: TRY or ACTIVE format
 *
 * Return pointer to TRY or ACTIVE format structure
 */
static struct v4l2_mbus_framefmt *
__vfe_get_format(struct vfe_line *line,
		 struct v4l2_subdev_pad_config *cfg,
		 unsigned int pad,
		 enum v4l2_subdev_format_whence which)
{
	if (which == V4L2_SUBDEV_FORMAT_TRY)
		return v4l2_subdev_get_try_format(&line->subdev, cfg, pad);

	return &line->fmt[pad];
}

/*
 * __vfe_get_compose - Get pointer to compose selection structure
 * @line: VFE line
 * @cfg: V4L2 subdev pad configuration
 * @which: TRY or ACTIVE format
 *
 * Return pointer to TRY or ACTIVE compose rectangle structure
 */
static struct v4l2_rect *
__vfe_get_compose(struct vfe_line *line,
		  struct v4l2_subdev_pad_config *cfg,
		  enum v4l2_subdev_format_whence which)
{
	if (which == V4L2_SUBDEV_FORMAT_TRY)
		return v4l2_subdev_get_try_compose(&line->subdev, cfg,
						   MSM_VFE_PAD_SINK);

	return &line->compose;
}

/*
 * __vfe_get_crop - Get pointer to crop selection structure
 * @line: VFE line
 * @cfg: V4L2 subdev pad configuration
 * @which: TRY or ACTIVE format
 *
 * Return pointer to TRY or ACTIVE crop rectangle structure
 */
static struct v4l2_rect *
__vfe_get_crop(struct vfe_line *line,
	       struct v4l2_subdev_pad_config *cfg,
	       enum v4l2_subdev_format_whence which)
{
	if (which == V4L2_SUBDEV_FORMAT_TRY)
		return v4l2_subdev_get_try_crop(&line->subdev, cfg,
						MSM_VFE_PAD_SRC);

	return &line->crop;
}

/*
 * vfe_try_format - Handle try format by pad subdev method
 * @line: VFE line
 * @cfg: V4L2 subdev pad configuration
 * @pad: pad on which format is requested
 * @fmt: pointer to v4l2 format structure
 * @which: wanted subdev format
 */
static void vfe_try_format(struct vfe_line *line,
			   struct v4l2_subdev_pad_config *cfg,
			   unsigned int pad,
			   struct v4l2_mbus_framefmt *fmt,
			   enum v4l2_subdev_format_whence which)
{
	unsigned int i;
	u32 code;

	switch (pad) {
	case MSM_VFE_PAD_SINK:
		/* Set format on sink pad */

		for (i = 0; i < line->nformats; i++)
			if (fmt->code == line->formats[i].code)
				break;

		/* If not found, use UYVY as default */
		if (i >= line->nformats)
			fmt->code = MEDIA_BUS_FMT_UYVY8_2X8;

		fmt->width = clamp_t(u32, fmt->width, 1, 8191);
		fmt->height = clamp_t(u32, fmt->height, 1, 8191);

		fmt->field = V4L2_FIELD_NONE;
		fmt->colorspace = V4L2_COLORSPACE_SRGB;

		break;

	case MSM_VFE_PAD_SRC:
		/* Set and return a format same as sink pad */
		code = fmt->code;

		*fmt = *__vfe_get_format(line, cfg, MSM_VFE_PAD_SINK, which);

		fmt->code = vfe_src_pad_code(line, fmt->code, 0, code);

		if (line->id == VFE_LINE_PIX) {
			struct v4l2_rect *rect;

			rect = __vfe_get_crop(line, cfg, which);

			fmt->width = rect->width;
			fmt->height = rect->height;
		}

		break;
	}

	fmt->colorspace = V4L2_COLORSPACE_SRGB;
}

/*
 * vfe_try_compose - Handle try compose selection by pad subdev method
 * @line: VFE line
 * @cfg: V4L2 subdev pad configuration
 * @rect: pointer to v4l2 rect structure
 * @which: wanted subdev format
 */
static void vfe_try_compose(struct vfe_line *line,
			    struct v4l2_subdev_pad_config *cfg,
			    struct v4l2_rect *rect,
			    enum v4l2_subdev_format_whence which)
{
	struct v4l2_mbus_framefmt *fmt;

	fmt = __vfe_get_format(line, cfg, MSM_VFE_PAD_SINK, which);

	if (rect->width > fmt->width)
		rect->width = fmt->width;

	if (rect->height > fmt->height)
		rect->height = fmt->height;

	if (fmt->width > rect->width * SCALER_RATIO_MAX)
		rect->width = (fmt->width + SCALER_RATIO_MAX - 1) /
							SCALER_RATIO_MAX;

	rect->width &= ~0x1;

	if (fmt->height > rect->height * SCALER_RATIO_MAX)
		rect->height = (fmt->height + SCALER_RATIO_MAX - 1) /
							SCALER_RATIO_MAX;

	if (rect->width < 16)
		rect->width = 16;

	if (rect->height < 4)
		rect->height = 4;
}

/*
 * vfe_try_crop - Handle try crop selection by pad subdev method
 * @line: VFE line
 * @cfg: V4L2 subdev pad configuration
 * @rect: pointer to v4l2 rect structure
 * @which: wanted subdev format
 */
static void vfe_try_crop(struct vfe_line *line,
			 struct v4l2_subdev_pad_config *cfg,
			 struct v4l2_rect *rect,
			 enum v4l2_subdev_format_whence which)
{
	struct v4l2_rect *compose;

	compose = __vfe_get_compose(line, cfg, which);

	if (rect->width > compose->width)
		rect->width = compose->width;

	if (rect->width + rect->left > compose->width)
		rect->left = compose->width - rect->width;

	if (rect->height > compose->height)
		rect->height = compose->height;

	if (rect->height + rect->top > compose->height)
		rect->top = compose->height - rect->height;

	/* wm in line based mode writes multiple of 16 horizontally */
	rect->left += (rect->width & 0xf) >> 1;
	rect->width &= ~0xf;

	if (rect->width < 16) {
		rect->left = 0;
		rect->width = 16;
	}

	if (rect->height < 4) {
		rect->top = 0;
		rect->height = 4;
	}
}

/*
 * vfe_enum_mbus_code - Handle pixel format enumeration
 * @sd: VFE V4L2 subdevice
 * @cfg: V4L2 subdev pad configuration
 * @code: pointer to v4l2_subdev_mbus_code_enum structure
 *
 * return -EINVAL or zero on success
 */
static int vfe_enum_mbus_code(struct v4l2_subdev *sd,
			      struct v4l2_subdev_pad_config *cfg,
			      struct v4l2_subdev_mbus_code_enum *code)
{
	struct vfe_line *line = v4l2_get_subdevdata(sd);

	if (code->pad == MSM_VFE_PAD_SINK) {
		if (code->index >= line->nformats)
			return -EINVAL;

		code->code = line->formats[code->index].code;
	} else {
		struct v4l2_mbus_framefmt *sink_fmt;

		sink_fmt = __vfe_get_format(line, cfg, MSM_VFE_PAD_SINK,
					    code->which);

		code->code = vfe_src_pad_code(line, sink_fmt->code,
					      code->index, 0);
		if (!code->code)
			return -EINVAL;
	}

	return 0;
}

/*
 * vfe_enum_frame_size - Handle frame size enumeration
 * @sd: VFE V4L2 subdevice
 * @cfg: V4L2 subdev pad configuration
 * @fse: pointer to v4l2_subdev_frame_size_enum structure
 *
 * Return -EINVAL or zero on success
 */
static int vfe_enum_frame_size(struct v4l2_subdev *sd,
			       struct v4l2_subdev_pad_config *cfg,
			       struct v4l2_subdev_frame_size_enum *fse)
{
	struct vfe_line *line = v4l2_get_subdevdata(sd);
	struct v4l2_mbus_framefmt format;

	if (fse->index != 0)
		return -EINVAL;

	format.code = fse->code;
	format.width = 1;
	format.height = 1;
	vfe_try_format(line, cfg, fse->pad, &format, fse->which);
	fse->min_width = format.width;
	fse->min_height = format.height;

	if (format.code != fse->code)
		return -EINVAL;

	format.code = fse->code;
	format.width = -1;
	format.height = -1;
	vfe_try_format(line, cfg, fse->pad, &format, fse->which);
	fse->max_width = format.width;
	fse->max_height = format.height;

	return 0;
}

/*
 * vfe_get_format - Handle get format by pads subdev method
 * @sd: VFE V4L2 subdevice
 * @cfg: V4L2 subdev pad configuration
 * @fmt: pointer to v4l2 subdev format structure
 *
 * Return -EINVAL or zero on success
 */
static int vfe_get_format(struct v4l2_subdev *sd,
			  struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *fmt)
{
	struct vfe_line *line = v4l2_get_subdevdata(sd);
	struct v4l2_mbus_framefmt *format;

	format = __vfe_get_format(line, cfg, fmt->pad, fmt->which);
	if (format == NULL)
		return -EINVAL;

	fmt->format = *format;

	return 0;
}

static int vfe_set_selection(struct v4l2_subdev *sd,
			     struct v4l2_subdev_pad_config *cfg,
			     struct v4l2_subdev_selection *sel);

/*
 * vfe_set_format - Handle set format by pads subdev method
 * @sd: VFE V4L2 subdevice
 * @cfg: V4L2 subdev pad configuration
 * @fmt: pointer to v4l2 subdev format structure
 *
 * Return -EINVAL or zero on success
 */
static int vfe_set_format(struct v4l2_subdev *sd,
			  struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *fmt)
{
	struct vfe_line *line = v4l2_get_subdevdata(sd);
	struct v4l2_mbus_framefmt *format;

	format = __vfe_get_format(line, cfg, fmt->pad, fmt->which);
	if (format == NULL)
		return -EINVAL;

	vfe_try_format(line, cfg, fmt->pad, &fmt->format, fmt->which);
	*format = fmt->format;

	if (fmt->pad == MSM_VFE_PAD_SINK) {
		struct v4l2_subdev_selection sel = { 0 };
		int ret;

		/* Propagate the format from sink to source */
		format = __vfe_get_format(line, cfg, MSM_VFE_PAD_SRC,
					  fmt->which);

		*format = fmt->format;
		vfe_try_format(line, cfg, MSM_VFE_PAD_SRC, format,
			       fmt->which);

		if (line->id != VFE_LINE_PIX)
			return 0;

		/* Reset sink pad compose selection */
		sel.which = fmt->which;
		sel.pad = MSM_VFE_PAD_SINK;
		sel.target = V4L2_SEL_TGT_COMPOSE;
		sel.r.width = fmt->format.width;
		sel.r.height = fmt->format.height;
		ret = vfe_set_selection(sd, cfg, &sel);
		if (ret < 0)
			return ret;
	}

	return 0;
}

/*
 * vfe_get_selection - Handle get selection by pads subdev method
 * @sd: VFE V4L2 subdevice
 * @cfg: V4L2 subdev pad configuration
 * @sel: pointer to v4l2 subdev selection structure
 *
 * Return -EINVAL or zero on success
 */
static int vfe_get_selection(struct v4l2_subdev *sd,
			     struct v4l2_subdev_pad_config *cfg,
			     struct v4l2_subdev_selection *sel)
{
	struct vfe_line *line = v4l2_get_subdevdata(sd);
	struct v4l2_subdev_format fmt = { 0 };
	struct v4l2_rect *rect;
	int ret;

	if (line->id != VFE_LINE_PIX)
		return -EINVAL;

	if (sel->pad == MSM_VFE_PAD_SINK)
		switch (sel->target) {
		case V4L2_SEL_TGT_COMPOSE_BOUNDS:
			fmt.pad = sel->pad;
			fmt.which = sel->which;
			ret = vfe_get_format(sd, cfg, &fmt);
			if (ret < 0)
				return ret;

			sel->r.left = 0;
			sel->r.top = 0;
			sel->r.width = fmt.format.width;
			sel->r.height = fmt.format.height;
			break;
		case V4L2_SEL_TGT_COMPOSE:
			rect = __vfe_get_compose(line, cfg, sel->which);
			if (rect == NULL)
				return -EINVAL;

			sel->r = *rect;
			break;
		default:
			return -EINVAL;
		}
	else if (sel->pad == MSM_VFE_PAD_SRC)
		switch (sel->target) {
		case V4L2_SEL_TGT_CROP_BOUNDS:
			rect = __vfe_get_compose(line, cfg, sel->which);
			if (rect == NULL)
				return -EINVAL;

			sel->r.left = rect->left;
			sel->r.top = rect->top;
			sel->r.width = rect->width;
			sel->r.height = rect->height;
			break;
		case V4L2_SEL_TGT_CROP:
			rect = __vfe_get_crop(line, cfg, sel->which);
			if (rect == NULL)
				return -EINVAL;

			sel->r = *rect;
			break;
		default:
			return -EINVAL;
		}

	return 0;
}

/*
 * vfe_set_selection - Handle set selection by pads subdev method
 * @sd: VFE V4L2 subdevice
 * @cfg: V4L2 subdev pad configuration
 * @sel: pointer to v4l2 subdev selection structure
 *
 * Return -EINVAL or zero on success
 */
static int vfe_set_selection(struct v4l2_subdev *sd,
			     struct v4l2_subdev_pad_config *cfg,
			     struct v4l2_subdev_selection *sel)
{
	struct vfe_line *line = v4l2_get_subdevdata(sd);
	struct v4l2_rect *rect;
	int ret;

	if (line->id != VFE_LINE_PIX)
		return -EINVAL;

	if (sel->target == V4L2_SEL_TGT_COMPOSE &&
		sel->pad == MSM_VFE_PAD_SINK) {
		struct v4l2_subdev_selection crop = { 0 };

		rect = __vfe_get_compose(line, cfg, sel->which);
		if (rect == NULL)
			return -EINVAL;

		vfe_try_compose(line, cfg, &sel->r, sel->which);
		*rect = sel->r;

		/* Reset source crop selection */
		crop.which = sel->which;
		crop.pad = MSM_VFE_PAD_SRC;
		crop.target = V4L2_SEL_TGT_CROP;
		crop.r = *rect;
		ret = vfe_set_selection(sd, cfg, &crop);
	} else if (sel->target == V4L2_SEL_TGT_CROP &&
		sel->pad == MSM_VFE_PAD_SRC) {
		struct v4l2_subdev_format fmt = { 0 };

		rect = __vfe_get_crop(line, cfg, sel->which);
		if (rect == NULL)
			return -EINVAL;

		vfe_try_crop(line, cfg, &sel->r, sel->which);
		*rect = sel->r;

		/* Reset source pad format width and height */
		fmt.which = sel->which;
		fmt.pad = MSM_VFE_PAD_SRC;
		ret = vfe_get_format(sd, cfg, &fmt);
		if (ret < 0)
			return ret;

		fmt.format.width = rect->width;
		fmt.format.height = rect->height;
		ret = vfe_set_format(sd, cfg, &fmt);
	} else {
		ret = -EINVAL;
	}

	return ret;
}

/*
 * vfe_init_formats - Initialize formats on all pads
 * @sd: VFE V4L2 subdevice
 * @fh: V4L2 subdev file handle
 *
 * Initialize all pad formats with default values.
 *
 * Return 0 on success or a negative error code otherwise
 */
static int vfe_init_formats(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct v4l2_subdev_format format = {
		.pad = MSM_VFE_PAD_SINK,
		.which = fh ? V4L2_SUBDEV_FORMAT_TRY :
			      V4L2_SUBDEV_FORMAT_ACTIVE,
		.format = {
			.code = MEDIA_BUS_FMT_UYVY8_2X8,
			.width = 1920,
			.height = 1080
		}
	};

	return vfe_set_format(sd, fh ? fh->pad : NULL, &format);
}

/*
 * msm_vfe_subdev_init - Initialize VFE device structure and resources
 * @vfe: VFE device
 * @res: VFE module resources table
 *
 * Return 0 on success or a negative error code otherwise
 */
int msm_vfe_subdev_init(struct camss *camss, struct vfe_device *vfe,
			const struct resources *res, u8 id)
{
	struct device *dev = camss->dev;
	struct platform_device *pdev = to_platform_device(dev);
	struct resource *r;
	int i, j;
	int ret;

	vfe->isr_ops.reset_ack = vfe_isr_reset_ack;
	vfe->isr_ops.halt_ack = vfe_isr_halt_ack;
	vfe->isr_ops.reg_update = vfe_isr_reg_update;
	vfe->isr_ops.sof = vfe_isr_sof;
	vfe->isr_ops.comp_done = vfe_isr_comp_done;
	vfe->isr_ops.wm_done = vfe_isr_wm_done;

	if (camss->version == CAMSS_8x16)
		vfe->ops = &vfe_ops_4_1;
	else if (camss->version == CAMSS_8x96)
		vfe->ops = &vfe_ops_4_7;
	else
		return -EINVAL;

	/* Memory */

	r = platform_get_resource_byname(pdev, IORESOURCE_MEM, res->reg[0]);
	vfe->base = devm_ioremap_resource(dev, r);
	if (IS_ERR(vfe->base)) {
		dev_err(dev, "could not map memory\n");
		return PTR_ERR(vfe->base);
	}

	/* Interrupt */

	r = platform_get_resource_byname(pdev, IORESOURCE_IRQ,
					 res->interrupt[0]);
	if (!r) {
		dev_err(dev, "missing IRQ\n");
		return -EINVAL;
	}

	vfe->irq = r->start;
	snprintf(vfe->irq_name, sizeof(vfe->irq_name), "%s_%s%d",
		 dev_name(dev), MSM_VFE_NAME, vfe->id);
	ret = devm_request_irq(dev, vfe->irq, vfe->ops->isr,
			       IRQF_TRIGGER_RISING, vfe->irq_name, vfe);
	if (ret < 0) {
		dev_err(dev, "request_irq failed: %d\n", ret);
		return ret;
	}

	/* Clocks */

	vfe->nclocks = 0;
	while (res->clock[vfe->nclocks])
		vfe->nclocks++;

	vfe->clock = devm_kcalloc(dev, vfe->nclocks, sizeof(*vfe->clock),
				  GFP_KERNEL);
	if (!vfe->clock)
		return -ENOMEM;

	for (i = 0; i < vfe->nclocks; i++) {
		struct camss_clock *clock = &vfe->clock[i];

		clock->clk = devm_clk_get(dev, res->clock[i]);
		if (IS_ERR(clock->clk))
			return PTR_ERR(clock->clk);

		clock->name = res->clock[i];

		clock->nfreqs = 0;
		while (res->clock_rate[i][clock->nfreqs])
			clock->nfreqs++;

		if (!clock->nfreqs) {
			clock->freq = NULL;
			continue;
		}

		clock->freq = devm_kcalloc(dev,
					   clock->nfreqs,
					   sizeof(*clock->freq),
					   GFP_KERNEL);
		if (!clock->freq)
			return -ENOMEM;

		for (j = 0; j < clock->nfreqs; j++)
			clock->freq[j] = res->clock_rate[i][j];
	}

	mutex_init(&vfe->power_lock);
	vfe->power_count = 0;

	mutex_init(&vfe->stream_lock);
	vfe->stream_count = 0;

	spin_lock_init(&vfe->output_lock);

	vfe->camss = camss;
	vfe->id = id;
	vfe->reg_update = 0;

	for (i = VFE_LINE_RDI0; i <= VFE_LINE_PIX; i++) {
		struct vfe_line *l = &vfe->line[i];

		l->video_out.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
		l->video_out.camss = camss;
		l->id = i;
		init_completion(&l->output.sof);
		init_completion(&l->output.reg_update);

		if (camss->version == CAMSS_8x16) {
			if (i == VFE_LINE_PIX) {
				l->formats = formats_pix_8x16;
				l->nformats = ARRAY_SIZE(formats_pix_8x16);
			} else {
				l->formats = formats_rdi_8x16;
				l->nformats = ARRAY_SIZE(formats_rdi_8x16);
			}
		} else if (camss->version == CAMSS_8x96) {
			if (i == VFE_LINE_PIX) {
				l->formats = formats_pix_8x96;
				l->nformats = ARRAY_SIZE(formats_pix_8x96);
			} else {
				l->formats = formats_rdi_8x96;
				l->nformats = ARRAY_SIZE(formats_rdi_8x96);
			}
		} else {
			return -EINVAL;
		}
	}

	init_completion(&vfe->reset_complete);
	init_completion(&vfe->halt_complete);

	return 0;
}

/*
 * msm_vfe_get_vfe_id - Get VFE HW module id
 * @entity: Pointer to VFE media entity structure
 * @id: Return CSID HW module id here
 */
void msm_vfe_get_vfe_id(struct media_entity *entity, u8 *id)
{
	struct v4l2_subdev *sd;
	struct vfe_line *line;
	struct vfe_device *vfe;

	sd = media_entity_to_v4l2_subdev(entity);
	line = v4l2_get_subdevdata(sd);
	vfe = to_vfe(line);

	*id = vfe->id;
}

/*
 * msm_vfe_get_vfe_line_id - Get VFE line id by media entity
 * @entity: Pointer to VFE media entity structure
 * @id: Return VFE line id here
 */
void msm_vfe_get_vfe_line_id(struct media_entity *entity, enum vfe_line_id *id)
{
	struct v4l2_subdev *sd;
	struct vfe_line *line;

	sd = media_entity_to_v4l2_subdev(entity);
	line = v4l2_get_subdevdata(sd);

	*id = line->id;
}

/*
 * vfe_link_setup - Setup VFE connections
 * @entity: Pointer to media entity structure
 * @local: Pointer to local pad
 * @remote: Pointer to remote pad
 * @flags: Link flags
 *
 * Return 0 on success
 */
static int vfe_link_setup(struct media_entity *entity,
			  const struct media_pad *local,
			  const struct media_pad *remote, u32 flags)
{
	if (flags & MEDIA_LNK_FL_ENABLED)
		if (media_entity_remote_pad(local))
			return -EBUSY;

	return 0;
}

static const struct v4l2_subdev_core_ops vfe_core_ops = {
	.s_power = vfe_set_power,
};

static const struct v4l2_subdev_video_ops vfe_video_ops = {
	.s_stream = vfe_set_stream,
};

static const struct v4l2_subdev_pad_ops vfe_pad_ops = {
	.enum_mbus_code = vfe_enum_mbus_code,
	.enum_frame_size = vfe_enum_frame_size,
	.get_fmt = vfe_get_format,
	.set_fmt = vfe_set_format,
	.get_selection = vfe_get_selection,
	.set_selection = vfe_set_selection,
};

static const struct v4l2_subdev_ops vfe_v4l2_ops = {
	.core = &vfe_core_ops,
	.video = &vfe_video_ops,
	.pad = &vfe_pad_ops,
};

static const struct v4l2_subdev_internal_ops vfe_v4l2_internal_ops = {
	.open = vfe_init_formats,
};

static const struct media_entity_operations vfe_media_ops = {
	.link_setup = vfe_link_setup,
	.link_validate = v4l2_subdev_link_validate,
};

static const struct camss_video_ops camss_vfe_video_ops = {
	.queue_buffer = vfe_queue_buffer,
	.flush_buffers = vfe_flush_buffers,
};

/*
 * msm_vfe_register_entities - Register subdev node for VFE module
 * @vfe: VFE device
 * @v4l2_dev: V4L2 device
 *
 * Initialize and register a subdev node for the VFE module. Then
 * call msm_video_register() to register the video device node which
 * will be connected to this subdev node. Then actually create the
 * media link between them.
 *
 * Return 0 on success or a negative error code otherwise
 */
int msm_vfe_register_entities(struct vfe_device *vfe,
			      struct v4l2_device *v4l2_dev)
{
	struct device *dev = vfe->camss->dev;
	struct v4l2_subdev *sd;
	struct media_pad *pads;
	struct camss_video *video_out;
	int ret;
	int i;

	for (i = 0; i < ARRAY_SIZE(vfe->line); i++) {
		char name[32];

		sd = &vfe->line[i].subdev;
		pads = vfe->line[i].pads;
		video_out = &vfe->line[i].video_out;

		v4l2_subdev_init(sd, &vfe_v4l2_ops);
		sd->internal_ops = &vfe_v4l2_internal_ops;
		sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
		if (i == VFE_LINE_PIX)
			snprintf(sd->name, ARRAY_SIZE(sd->name), "%s%d_%s",
				 MSM_VFE_NAME, vfe->id, "pix");
		else
			snprintf(sd->name, ARRAY_SIZE(sd->name), "%s%d_%s%d",
				 MSM_VFE_NAME, vfe->id, "rdi", i);

		v4l2_set_subdevdata(sd, &vfe->line[i]);

		ret = vfe_init_formats(sd, NULL);
		if (ret < 0) {
			dev_err(dev, "Failed to init format: %d\n", ret);
			goto error_init;
		}

		pads[MSM_VFE_PAD_SINK].flags = MEDIA_PAD_FL_SINK;
		pads[MSM_VFE_PAD_SRC].flags = MEDIA_PAD_FL_SOURCE;

		sd->entity.function = MEDIA_ENT_F_PROC_VIDEO_PIXEL_FORMATTER;
		sd->entity.ops = &vfe_media_ops;
		ret = media_entity_pads_init(&sd->entity, MSM_VFE_PADS_NUM,
					     pads);
		if (ret < 0) {
			dev_err(dev, "Failed to init media entity: %d\n", ret);
			goto error_init;
		}

		ret = v4l2_device_register_subdev(v4l2_dev, sd);
		if (ret < 0) {
			dev_err(dev, "Failed to register subdev: %d\n", ret);
			goto error_reg_subdev;
		}

		video_out->ops = &camss_vfe_video_ops;
		video_out->bpl_alignment = 8;
		video_out->line_based = 0;
		if (i == VFE_LINE_PIX) {
			video_out->bpl_alignment = 16;
			video_out->line_based = 1;
		}
		snprintf(name, ARRAY_SIZE(name), "%s%d_%s%d",
			 MSM_VFE_NAME, vfe->id, "video", i);
		ret = msm_video_register(video_out, v4l2_dev, name,
					 i == VFE_LINE_PIX ? 1 : 0);
		if (ret < 0) {
			dev_err(dev, "Failed to register video node: %d\n",
				ret);
			goto error_reg_video;
		}

		ret = media_create_pad_link(
				&sd->entity, MSM_VFE_PAD_SRC,
				&video_out->vdev.entity, 0,
				MEDIA_LNK_FL_IMMUTABLE | MEDIA_LNK_FL_ENABLED);
		if (ret < 0) {
			dev_err(dev, "Failed to link %s->%s entities: %d\n",
				sd->entity.name, video_out->vdev.entity.name,
				ret);
			goto error_link;
		}
	}

	return 0;

error_link:
	msm_video_unregister(video_out);

error_reg_video:
	v4l2_device_unregister_subdev(sd);

error_reg_subdev:
	media_entity_cleanup(&sd->entity);

error_init:
	for (i--; i >= 0; i--) {
		sd = &vfe->line[i].subdev;
		video_out = &vfe->line[i].video_out;

		msm_video_unregister(video_out);
		v4l2_device_unregister_subdev(sd);
		media_entity_cleanup(&sd->entity);
	}

	return ret;
}

/*
 * msm_vfe_unregister_entities - Unregister VFE module subdev node
 * @vfe: VFE device
 */
void msm_vfe_unregister_entities(struct vfe_device *vfe)
{
	int i;

	mutex_destroy(&vfe->power_lock);
	mutex_destroy(&vfe->stream_lock);

	for (i = 0; i < ARRAY_SIZE(vfe->line); i++) {
		struct v4l2_subdev *sd = &vfe->line[i].subdev;
		struct camss_video *video_out = &vfe->line[i].video_out;

		msm_video_unregister(video_out);
		v4l2_device_unregister_subdev(sd);
		media_entity_cleanup(&sd->entity);
	}
}
