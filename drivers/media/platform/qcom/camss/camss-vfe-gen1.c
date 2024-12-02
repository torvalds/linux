// SPDX-License-Identifier: GPL-2.0
/*
 * camss-vfe-gen1.c
 *
 * Qualcomm MSM Camera Subsystem - VFE Common functionality for Gen 1 versions of hw (4.1, 4.7..)
 *
 * Copyright (C) 2020 Linaro Ltd.
 */

#include "camss.h"
#include "camss-vfe.h"
#include "camss-vfe-gen1.h"

/* Max number of frame drop updates per frame */
#define VFE_FRAME_DROP_UPDATES 2
#define VFE_NEXT_SOF_MS 500

int vfe_gen1_halt(struct vfe_device *vfe)
{
	unsigned long time;

	reinit_completion(&vfe->halt_complete);

	vfe->ops_gen1->halt_request(vfe);

	time = wait_for_completion_timeout(&vfe->halt_complete,
					   msecs_to_jiffies(VFE_HALT_TIMEOUT_MS));
	if (!time) {
		dev_err(vfe->camss->dev, "VFE halt timeout\n");
		return -EIO;
	}

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

	output->gen1.wait_sof = 1;
	spin_unlock_irqrestore(&vfe->output_lock, flags);

	time = wait_for_completion_timeout(&output->sof, msecs_to_jiffies(VFE_NEXT_SOF_MS));
	if (!time)
		dev_err(vfe->camss->dev, "VFE sof timeout\n");

	spin_lock_irqsave(&vfe->output_lock, flags);
	for (i = 0; i < output->wm_num; i++)
		vfe->ops_gen1->wm_enable(vfe, output->wm_idx[i], 0);

	ops->reg_update(vfe, line->id);
	output->wait_reg_update = 1;
	spin_unlock_irqrestore(&vfe->output_lock, flags);

	time = wait_for_completion_timeout(&output->reg_update, msecs_to_jiffies(VFE_NEXT_SOF_MS));
	if (!time)
		dev_err(vfe->camss->dev, "VFE reg update timeout\n");

	spin_lock_irqsave(&vfe->output_lock, flags);

	if (line->id != VFE_LINE_PIX) {
		vfe->ops_gen1->wm_frame_based(vfe, output->wm_idx[0], 0);
		vfe->ops_gen1->bus_disconnect_wm_from_rdi(vfe, output->wm_idx[0], line->id);
		vfe->ops_gen1->enable_irq_wm_line(vfe, output->wm_idx[0], line->id, 0);
		vfe->ops_gen1->set_cgc_override(vfe, output->wm_idx[0], 0);
		spin_unlock_irqrestore(&vfe->output_lock, flags);
	} else {
		for (i = 0; i < output->wm_num; i++) {
			vfe->ops_gen1->wm_line_based(vfe, output->wm_idx[i], NULL, i, 0);
			vfe->ops_gen1->set_cgc_override(vfe, output->wm_idx[i], 0);
		}

		vfe->ops_gen1->enable_irq_pix_line(vfe, 0, line->id, 0);
		vfe->ops_gen1->set_module_cfg(vfe, 0);
		vfe->ops_gen1->set_realign_cfg(vfe, line, 0);
		vfe->ops_gen1->set_xbar_cfg(vfe, output, 0);
		vfe->ops_gen1->set_camif_cmd(vfe, 0);

		spin_unlock_irqrestore(&vfe->output_lock, flags);

		vfe->ops_gen1->camif_wait_for_stop(vfe, vfe->camss->dev);
	}

	return 0;
}

/*
 * vfe_gen1_disable - Disable streaming on VFE line
 * @line: VFE line
 *
 * Return 0 on success or a negative error code otherwise
 */
int vfe_gen1_disable(struct vfe_line *line)
{
	struct vfe_device *vfe = to_vfe(line);

	vfe_disable_output(line);

	vfe_put_output(line);

	mutex_lock(&vfe->stream_lock);

	if (vfe->stream_count == 1)
		vfe->ops_gen1->bus_enable_wr_if(vfe, 0);

	vfe->stream_count--;

	mutex_unlock(&vfe->stream_lock);

	return 0;
}

static void vfe_output_init_addrs(struct vfe_device *vfe,
				  struct vfe_output *output, u8 sync,
				  struct vfe_line *line)
{
	u32 ping_addr;
	u32 pong_addr;
	unsigned int i;

	output->gen1.active_buf = 0;

	for (i = 0; i < output->wm_num; i++) {
		if (output->buf[0])
			ping_addr = output->buf[0]->addr[i];
		else
			ping_addr = 0;

		if (output->buf[1])
			pong_addr = output->buf[1]->addr[i];
		else
			pong_addr = ping_addr;

		vfe->ops_gen1->wm_set_ping_addr(vfe, output->wm_idx[i], ping_addr);
		vfe->ops_gen1->wm_set_pong_addr(vfe, output->wm_idx[i], pong_addr);
		if (sync)
			vfe->ops_gen1->bus_reload_wm(vfe, output->wm_idx[i]);
	}
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
		vfe->ops_gen1->wm_set_framedrop_period(vfe, output->wm_idx[i], drop_period);
		vfe->ops_gen1->wm_set_framedrop_pattern(vfe, output->wm_idx[i], drop_pattern);
	}

	vfe->ops->reg_update(vfe, container_of(output, struct vfe_line, output)->id);
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

	ub_size = vfe->ops_gen1->get_ub_size(vfe->id);
	if (!ub_size)
		return -EINVAL;

	sensor = camss_find_sensor(&line->subdev.entity);
	if (sensor) {
		struct v4l2_subdev *subdev = media_entity_to_v4l2_subdev(sensor);

		v4l2_subdev_call(subdev, sensor, g_skip_frames, &frame_skip);
		/* Max frame skip is 29 frames */
		if (frame_skip > VFE_FRAME_DROP_VAL - 1)
			frame_skip = VFE_FRAME_DROP_VAL - 1;
	}

	spin_lock_irqsave(&vfe->output_lock, flags);

	ops->reg_update_clear(vfe, line->id);

	if (output->state != VFE_OUTPUT_RESERVED) {
		dev_err(vfe->camss->dev, "Output is not in reserved state %d\n", output->state);
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
	output->gen1.wait_sof = 0;
	output->wait_reg_update = 0;
	reinit_completion(&output->sof);
	reinit_completion(&output->reg_update);

	vfe_output_init_addrs(vfe, output, 0, line);

	if (line->id != VFE_LINE_PIX) {
		vfe->ops_gen1->set_cgc_override(vfe, output->wm_idx[0], 1);
		vfe->ops_gen1->enable_irq_wm_line(vfe, output->wm_idx[0], line->id, 1);
		vfe->ops_gen1->bus_connect_wm_to_rdi(vfe, output->wm_idx[0], line->id);
		vfe->ops_gen1->wm_set_subsample(vfe, output->wm_idx[0]);
		vfe->ops_gen1->set_rdi_cid(vfe, line->id, 0);
		vfe->ops_gen1->wm_set_ub_cfg(vfe, output->wm_idx[0],
					    (ub_size + 1) * output->wm_idx[0], ub_size);
		vfe->ops_gen1->wm_frame_based(vfe, output->wm_idx[0], 1);
		vfe->ops_gen1->wm_enable(vfe, output->wm_idx[0], 1);
		vfe->ops_gen1->bus_reload_wm(vfe, output->wm_idx[0]);
	} else {
		ub_size /= output->wm_num;
		for (i = 0; i < output->wm_num; i++) {
			vfe->ops_gen1->set_cgc_override(vfe, output->wm_idx[i], 1);
			vfe->ops_gen1->wm_set_subsample(vfe, output->wm_idx[i]);
			vfe->ops_gen1->wm_set_ub_cfg(vfe, output->wm_idx[i],
						     (ub_size + 1) * output->wm_idx[i], ub_size);
			vfe->ops_gen1->wm_line_based(vfe, output->wm_idx[i],
						     &line->video_out.active_fmt.fmt.pix_mp, i, 1);
			vfe->ops_gen1->wm_enable(vfe, output->wm_idx[i], 1);
			vfe->ops_gen1->bus_reload_wm(vfe, output->wm_idx[i]);
		}
		vfe->ops_gen1->enable_irq_pix_line(vfe, 0, line->id, 1);
		vfe->ops_gen1->set_module_cfg(vfe, 1);
		vfe->ops_gen1->set_camif_cfg(vfe, line);
		vfe->ops_gen1->set_realign_cfg(vfe, line, 1);
		vfe->ops_gen1->set_xbar_cfg(vfe, output, 1);
		vfe->ops_gen1->set_demux_cfg(vfe, line);
		vfe->ops_gen1->set_scale_cfg(vfe, line);
		vfe->ops_gen1->set_crop_cfg(vfe, line);
		vfe->ops_gen1->set_clamp_cfg(vfe);
		vfe->ops_gen1->set_camif_cmd(vfe, 1);
	}

	ops->reg_update(vfe, line->id);

	spin_unlock_irqrestore(&vfe->output_lock, flags);

	return 0;
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

	output->gen1.active_buf = 0;

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

int vfe_gen1_enable(struct vfe_line *line)
{
	struct vfe_device *vfe = to_vfe(line);
	int ret;

	mutex_lock(&vfe->stream_lock);

	if (!vfe->stream_count) {
		vfe->ops_gen1->enable_irq_common(vfe);
		vfe->ops_gen1->bus_enable_wr_if(vfe, 1);
		vfe->ops_gen1->set_qos(vfe);
		vfe->ops_gen1->set_ds(vfe);
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
		vfe->ops_gen1->bus_enable_wr_if(vfe, 0);

	vfe->stream_count--;

	mutex_unlock(&vfe->stream_lock);

	return ret;
}

static void vfe_output_update_ping_addr(struct vfe_device *vfe,
					struct vfe_output *output, u8 sync,
					struct vfe_line *line)
{
	u32 addr;
	unsigned int i;

	for (i = 0; i < output->wm_num; i++) {
		if (output->buf[0])
			addr = output->buf[0]->addr[i];
		else
			addr = 0;

		vfe->ops_gen1->wm_set_ping_addr(vfe, output->wm_idx[i], addr);
		if (sync)
			vfe->ops_gen1->bus_reload_wm(vfe, output->wm_idx[i]);
	}
}

static void vfe_output_update_pong_addr(struct vfe_device *vfe,
					struct vfe_output *output, u8 sync,
					struct vfe_line *line)
{
	u32 addr;
	unsigned int i;

	for (i = 0; i < output->wm_num; i++) {
		if (output->buf[1])
			addr = output->buf[1]->addr[i];
		else
			addr = 0;

		vfe->ops_gen1->wm_set_pong_addr(vfe, output->wm_idx[i], addr);
		if (sync)
			vfe->ops_gen1->bus_reload_wm(vfe, output->wm_idx[i]);
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
				     struct camss_buffer *new_buf,
				     struct vfe_line *line)
{
	int inactive_idx;

	switch (output->state) {
	case VFE_OUTPUT_SINGLE:
		inactive_idx = !output->gen1.active_buf;

		if (!output->buf[inactive_idx]) {
			output->buf[inactive_idx] = new_buf;

			if (inactive_idx)
				vfe_output_update_pong_addr(vfe, output, 0, line);
			else
				vfe_output_update_ping_addr(vfe, output, 0, line);

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

			vfe_output_init_addrs(vfe, output, 1, line);
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

/*
 * vfe_isr_halt_ack - Process halt ack
 * @vfe: VFE Device
 */
static void vfe_isr_halt_ack(struct vfe_device *vfe)
{
	complete(&vfe->halt_complete);
	vfe->ops_gen1->halt_clear(vfe);
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
	if (output->gen1.wait_sof) {
		output->gen1.wait_sof = 0;
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
	struct vfe_line *line = &vfe->line[line_id];
	unsigned long flags;

	spin_lock_irqsave(&vfe->output_lock, flags);
	vfe->ops->reg_update_clear(vfe, line_id);

	output = &line->output;

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

		vfe_output_init_addrs(vfe, output, 1, &vfe->line[line_id]);
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

	active_index = vfe->ops_gen1->wm_get_ping_pong_status(vfe, wm);

	spin_lock_irqsave(&vfe->output_lock, flags);

	if (vfe->wm_output_map[wm] == VFE_LINE_NONE) {
		dev_err_ratelimited(vfe->camss->dev,
				    "Received wm done for unmapped index\n");
		goto out_unlock;
	}
	output = &vfe->line[vfe->wm_output_map[wm]].output;

	if (output->gen1.active_buf == active_index && 0) {
		dev_err_ratelimited(vfe->camss->dev,
				    "Active buffer mismatch!\n");
		goto out_unlock;
	}
	output->gen1.active_buf = active_index;

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
			vfe->ops_gen1->wm_set_ping_addr(vfe, output->wm_idx[i], new_addr[i]);
	else
		for (i = 0; i < output->wm_num; i++)
			vfe->ops_gen1->wm_set_pong_addr(vfe, output->wm_idx[i], new_addr[i]);

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
 * vfe_queue_buffer - Add empty buffer
 * @vid: Video device structure
 * @buf: Buffer to be enqueued
 *
 * Add an empty buffer - depending on the current number of buffers it will be
 * put in pending buffer queue or directly given to the hardware to be filled.
 *
 * Return 0 on success or a negative error code otherwise
 */
static int vfe_queue_buffer(struct camss_video *vid, struct camss_buffer *buf)
{
	struct vfe_line *line = container_of(vid, struct vfe_line, video_out);
	struct vfe_device *vfe = to_vfe(line);
	struct vfe_output *output;
	unsigned long flags;

	output = &line->output;

	spin_lock_irqsave(&vfe->output_lock, flags);

	vfe_buf_update_wm_on_new(vfe, output, buf, line);

	spin_unlock_irqrestore(&vfe->output_lock, flags);

	return 0;
}

#define CALC_WORD(width, M, N) (((width) * (M) + (N) - 1) / (N))

int vfe_word_per_line(u32 format, u32 width)
{
	int val = 0;

	switch (format) {
	case V4L2_PIX_FMT_NV12:
	case V4L2_PIX_FMT_NV21:
	case V4L2_PIX_FMT_NV16:
	case V4L2_PIX_FMT_NV61:
		val = CALC_WORD(width, 1, 8);
		break;
	case V4L2_PIX_FMT_YUYV:
	case V4L2_PIX_FMT_YVYU:
	case V4L2_PIX_FMT_UYVY:
	case V4L2_PIX_FMT_VYUY:
		val = CALC_WORD(width, 2, 8);
		break;
	}

	return val;
}

const struct vfe_isr_ops vfe_isr_ops_gen1 = {
	.reset_ack = vfe_isr_reset_ack,
	.halt_ack = vfe_isr_halt_ack,
	.reg_update = vfe_isr_reg_update,
	.sof = vfe_isr_sof,
	.comp_done = vfe_isr_comp_done,
	.wm_done = vfe_isr_wm_done,
};

const struct camss_video_ops vfe_video_ops_gen1 = {
	.queue_buffer = vfe_queue_buffer,
	.flush_buffers = vfe_flush_buffers,
};
