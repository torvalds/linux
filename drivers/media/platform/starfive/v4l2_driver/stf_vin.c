// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2021 StarFive Technology Co., Ltd.
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/dma-mapping.h>

#include "stfcamss.h"

#define STF_VIN_NAME "stf_vin"

#define vin_line_array(ptr_line)        \
        ((const struct vin_line (*)[]) &(ptr_line[-(ptr_line->id)]))

#define line_to_vin2_dev(ptr_line)        \
        container_of(vin_line_array(ptr_line), struct stf_vin2_dev, line)

#define VIN_FRAME_DROP_MAX_VAL 30
#define VIN_FRAME_DROP_MIN_VAL 4

// #define VIN_TWO_BUFFER

static const struct vin2_format vin2_formats_st7110[] = {
	{ MEDIA_BUS_FMT_YUYV8_2X8, 16},
	{ MEDIA_BUS_FMT_RGB565_2X8_LE, 16},
	{ MEDIA_BUS_FMT_SRGGB10_1X10, 12},
	{ MEDIA_BUS_FMT_SGRBG10_1X10, 12},
	{ MEDIA_BUS_FMT_SGBRG10_1X10, 12},
	{ MEDIA_BUS_FMT_SBGGR10_1X10, 12},
};

static void vin_buffer_done(struct vin_line *line, struct vin_params *params);
static struct stfcamss_buffer *vin_buf_get_pending(struct vin_output *output);
static void vin_output_init_addrs(struct vin_line *line);
static void vin_init_outputs(struct vin_line *line);

static char *get_line_subdevname(int line_id)
{
	char *name = NULL;

	switch (line_id) {
	case VIN_LINE_WR:
		name = "wr";
		break;
	case VIN_LINE_ISP0:
		name = "isp0";
		break;
	case VIN_LINE_ISP1:
		name = "isp1";
		break;
	case VIN_LINE_ISP0_RAW:
		name = "isp0_raw";
		break;
	case VIN_LINE_ISP1_RAW:
		name = "isp1_raw";
		break;
	default:
		name = "unknow";
		break;
	}
	return name;
}

int stf_vin_subdev_init(struct stfcamss *stfcamss)
{
	struct stf_vin_dev *vin;
	struct device *dev = stfcamss->dev;
	struct stf_vin2_dev *vin_dev = stfcamss->vin_dev;
	int ret = 0, i;

	vin_dev->stfcamss = stfcamss;
	vin_dev->hw_ops = &vin_ops;
	vin_dev->hw_ops->isr_buffer_done = vin_buffer_done;

	vin = stfcamss->vin;
	atomic_set(&vin_dev->ref_count, 0);

	ret = devm_request_irq(dev,
			vin->irq, vin_dev->hw_ops->vin_wr_irq_handler,
			0, "vin_axiwr_irq", vin_dev);
	if (ret) {
		st_err(ST_VIN, "failed to request irq\n");
		goto out;
	}

	ret = devm_request_irq(dev,
			vin->isp0_irq, vin_dev->hw_ops->vin_isp_irq_handler,
			0, "vin_isp0_irq", vin_dev);
	if (ret) {
		st_err(ST_VIN, "failed to request isp0 irq\n");
		goto out;
	}
#if 0 

	ret = devm_request_irq(dev,
			vin->isp1_irq, vin_dev->hw_ops->vin_isp_irq_handler,
			0, "vin_isp1_irq", vin_dev);
	if (ret) {
		st_err(ST_VIN, "failed to request isp1 irq\n");
		goto out;
	}

	vin_dev->hw_ops->vin_wr_irq_enable(vin_dev, 1);
	vin_dev->hw_ops->vin_wr_irq_enable(vin_dev, 0);

	/* Reset device */
	/*Do not configure the CLK before powering on the device,
	add vin_power_on() to vin_set_power() 2021 1111 */
	ret = vin_dev->hw_ops->vin_clk_init(vin_dev);
	if (ret) {
		st_err(ST_VIN, "Failed to reset device\n");
		goto out;
	}

	// /* set the sysctl config */
	// ret = vin_dev->hw_ops->vin_config_set(vin_dev);
//	 if (ret) {
//		st_err(ST_VIN, "Failed to config device\n");
//		goto out;
//	 }
#endif
	mutex_init(&vin_dev->power_lock);
	vin_dev->power_count = 0;

	for (i = VIN_LINE_WR; i < VIN_LINE_MAX; i++) {
		struct vin_line *l = &vin_dev->line[i];
		int is_mp;

		is_mp = i == VIN_LINE_WR ? false : true;
		is_mp = false;
		l->video_out.type = is_mp ? V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE :
				V4L2_BUF_TYPE_VIDEO_CAPTURE;
		l->video_out.stfcamss = stfcamss;
		l->id = i;
		l->sdev_type = VIN_DEV_TYPE;
		l->formats = vin2_formats_st7110;
		l->nformats = ARRAY_SIZE(vin2_formats_st7110);
		spin_lock_init(&l->output_lock);

		mutex_init(&l->stream_lock);
		l->stream_count = 0;
		mutex_init(&l->power_lock);
		l->power_count = 0;
	}

	return 0;
out:
	return ret;
}

static int vin_set_power(struct v4l2_subdev *sd, int on)
{
	struct vin_line *line = v4l2_get_subdevdata(sd);
	struct stf_vin2_dev *vin_dev = line_to_vin2_dev(line);

	mutex_lock(&line->power_lock);
	if (on) {
		if (line->power_count == 0)
			vin_init_outputs(line);
		line->power_count++;
	} else {
		if (line->power_count == 0) {
			st_err(ST_VIN,
				"line power off on power_count == 0\n");
			goto exit_line;
		}
		line->power_count--;
	}
exit_line:
	mutex_unlock(&line->power_lock);

	mutex_lock(&vin_dev->power_lock);
	if (on) {
		if (vin_dev->power_count == 0) {
			vin_dev->hw_ops->vin_clk_enable(vin_dev);
			vin_dev->hw_ops->vin_config_set(vin_dev);
		}
		vin_dev->power_count++;
	} else {
		if (vin_dev->power_count == 0) {
			st_err(ST_VIN,
				"vin_dev power off on power_count == 0\n");
			goto exit;
		}
		if (vin_dev->power_count == 1) {
			vin_dev->hw_ops->vin_clk_disable(vin_dev);
		}
		vin_dev->power_count--;
	}
exit:

	mutex_unlock(&vin_dev->power_lock);

	return 0;
}

static int vin_enable_output(struct vin_line *line)
{
	struct vin_output *output = &line->output;
	unsigned long flags;
	unsigned int frame_skip = 0;
	struct media_entity *sensor;

	sensor = stfcamss_find_sensor(&line->subdev.entity);
	if (sensor) {
		struct v4l2_subdev *subdev =
					media_entity_to_v4l2_subdev(sensor);

		v4l2_subdev_call(subdev, sensor, g_skip_frames, &frame_skip);
		frame_skip += VIN_FRAME_DROP_MIN_VAL;
		if (frame_skip > VIN_FRAME_DROP_MAX_VAL)
			frame_skip = VIN_FRAME_DROP_MAX_VAL;
		st_debug(ST_VIN, "%s, frame_skip %d\n", __func__, frame_skip);
	}

	spin_lock_irqsave(&line->output_lock, flags);
	output->frame_skip = frame_skip;

	output->state = VIN_OUTPUT_IDLE;

	output->buf[0] = vin_buf_get_pending(output);
#ifdef VIN_TWO_BUFFER
	if (line->id == VIN_LINE_WR)
		output->buf[1] = vin_buf_get_pending(output);
#endif
	if (!output->buf[0] && output->buf[1]) {
		output->buf[0] = output->buf[1];
		output->buf[1] = NULL;
	}

	if (output->buf[0])
		output->state = VIN_OUTPUT_SINGLE;

#ifdef VIN_TWO_BUFFER
	if (output->buf[1] && line->id == VIN_LINE_WR)
		output->state = VIN_OUTPUT_CONTINUOUS;
#endif
	output->sequence = 0;

	vin_output_init_addrs(line);
	spin_unlock_irqrestore(&line->output_lock, flags);
	return 0;
}

static int vin_disable_output(struct vin_line *line)
{
	struct vin_output *output = &line->output;
	unsigned long flags;

	spin_lock_irqsave(&line->output_lock, flags);

	output->state = VIN_OUTPUT_OFF;

	spin_unlock_irqrestore(&line->output_lock, flags);
	return 0;
}

static int vin_set_stream(struct v4l2_subdev *sd, int enable)
{
	struct vin_line *line = v4l2_get_subdevdata(sd);
	struct stf_vin2_dev *vin_dev = line_to_vin2_dev(line);

	if (line->id == VIN_LINE_WR) {
		mutex_lock(&line->stream_lock);
		if (enable) {
			if (line->stream_count == 0) {
				vin_dev->hw_ops->vin_wr_irq_enable(vin_dev, 1);
				vin_dev->hw_ops->vin_wr_stream_set(vin_dev, 1);
			}
			line->stream_count++;
		} else {
			if (line->stream_count == 1) {
				vin_dev->hw_ops->vin_wr_irq_enable(vin_dev, 0);
				vin_dev->hw_ops->vin_wr_stream_set(vin_dev, 0);
			}
			line->stream_count--;
		}
		mutex_unlock(&line->stream_lock);
	}

	if (enable)
		vin_enable_output(line);
	else
		vin_disable_output(line);

	return 0;
}

static struct v4l2_mbus_framefmt *
__vin_get_format(struct vin_line *line,
		struct v4l2_subdev_state *state,
		unsigned int pad,
		enum v4l2_subdev_format_whence which)
{
	if (which == V4L2_SUBDEV_FORMAT_TRY)
		return v4l2_subdev_get_try_format(&line->subdev, state, pad);
	return &line->fmt[pad];
}

static void vin_try_format(struct vin_line *line,
				struct v4l2_subdev_state *state,
				unsigned int pad,
				struct v4l2_mbus_framefmt *fmt,
				enum v4l2_subdev_format_whence which)
{
	unsigned int i;

	switch (pad) {
	case STF_VIN_PAD_SINK:
		/* Set format on sink pad */

		for (i = 0; i < line->nformats; i++)
			if (fmt->code == line->formats[i].code)
				break;

		/* If not found, use UYVY as default */
		if (i >= line->nformats)
			fmt->code = MEDIA_BUS_FMT_RGB565_2X8_LE;

		fmt->width = clamp_t(u32,
				fmt->width, 1, STFCAMSS_FRAME_MAX_WIDTH);
		fmt->height = clamp_t(u32,
				fmt->height, 1, STFCAMSS_FRAME_MAX_HEIGHT_PIX);

		fmt->field = V4L2_FIELD_NONE;
		fmt->colorspace = V4L2_COLORSPACE_SRGB;
		fmt->flags = 0;

		break;

	case STF_VIN_PAD_SRC:
		/* Set and return a format same as sink pad */
		*fmt = *__vin_get_format(line, state, STF_VIN_PAD_SINK, which);
		break;
	}

	fmt->colorspace = V4L2_COLORSPACE_SRGB;
}

static int vin_enum_mbus_code(struct v4l2_subdev *sd,
				struct v4l2_subdev_state *state,
				struct v4l2_subdev_mbus_code_enum *code)
{
	struct vin_line *line = v4l2_get_subdevdata(sd);

	if (code->index >= line->nformats)
		return -EINVAL;
	if (code->pad == STF_VIN_PAD_SINK) {
		code->code = line->formats[code->index].code;
	} else {
		struct v4l2_mbus_framefmt *sink_fmt;

		sink_fmt = __vin_get_format(line, state, STF_VIN_PAD_SINK,
					code->which);

		code->code = sink_fmt->code;
		if (!code->code)
			return -EINVAL;
	}
	code->flags = 0;

	return 0;
}

static int vin_enum_frame_size(struct v4l2_subdev *sd,
				struct v4l2_subdev_state *state,
				struct v4l2_subdev_frame_size_enum *fse)
{
	struct vin_line *line = v4l2_get_subdevdata(sd);
	struct v4l2_mbus_framefmt format;

	if (fse->index != 0)
		return -EINVAL;

	format.code = fse->code;
	format.width = 1;
	format.height = 1;
	vin_try_format(line, state, fse->pad, &format, fse->which);
	fse->min_width = format.width;
	fse->min_height = format.height;

	if (format.code != fse->code)
		return -EINVAL;

	format.code = fse->code;
	format.width = -1;
	format.height = -1;
	vin_try_format(line, state, fse->pad, &format, fse->which);
	fse->max_width = format.width;
	fse->max_height = format.height;

	return 0;
}

static int vin_get_format(struct v4l2_subdev *sd,
			struct v4l2_subdev_state *state,
			struct v4l2_subdev_format *fmt)
{
	struct vin_line *line = v4l2_get_subdevdata(sd);
	struct v4l2_mbus_framefmt *format;

	format = __vin_get_format(line, state, fmt->pad, fmt->which);
	if (format == NULL)
		return -EINVAL;

	fmt->format = *format;

	return 0;
}

static int vin_set_format(struct v4l2_subdev *sd,
			struct v4l2_subdev_state *state,
			struct v4l2_subdev_format *fmt)
{
	struct vin_line *line = v4l2_get_subdevdata(sd);
	struct v4l2_mbus_framefmt *format;

	format = __vin_get_format(line, state, fmt->pad, fmt->which);
	if (format == NULL)
		return -EINVAL;

	vin_try_format(line, state, fmt->pad, &fmt->format, fmt->which);
	*format = fmt->format;

	if (fmt->pad == STF_VIN_PAD_SINK) {
		/* Propagate the format from sink to source */
		format = __vin_get_format(line, state, STF_VIN_PAD_SRC,
					fmt->which);

		*format = fmt->format;
		vin_try_format(line, state, STF_VIN_PAD_SRC, format,
					fmt->which);
	}

	return 0;
}

static int vin_init_formats(struct v4l2_subdev *sd,
			struct v4l2_subdev_fh *fh)
{
	struct v4l2_subdev_format format = {
		.pad = STF_VIN_PAD_SINK,
		.which = fh ? V4L2_SUBDEV_FORMAT_TRY :
				V4L2_SUBDEV_FORMAT_ACTIVE,
		.format = {
			.code = MEDIA_BUS_FMT_RGB565_2X8_LE,
			.width = 1920,
			.height = 1080
		}
	};

	return vin_set_format(sd, fh ? fh->state : NULL, &format);
}

static void vin_output_init_addrs(struct vin_line *line)
{
	struct vin_output *output = &line->output;
	struct stf_vin2_dev *vin_dev = line_to_vin2_dev(line);
	dma_addr_t ping_addr;
	dma_addr_t pong_addr;
	dma_addr_t y_addr, uv_addr;

	output->active_buf = 0;

	if (output->buf[0]) {
		ping_addr = output->buf[0]->addr[0];
		y_addr = output->buf[0]->addr[0];
		uv_addr = output->buf[0]->addr[1];

	} else
		ping_addr = 0;

	if (output->buf[1])
		pong_addr = output->buf[1]->addr[0];
	else
		pong_addr = ping_addr;

	switch (line->id) {
	case VIN_LINE_WR:  // wr
		vin_dev->hw_ops->vin_wr_set_ping_addr(vin_dev, ping_addr);
#ifdef VIN_TWO_BUFFER
		vin_dev->hw_ops->vin_wr_set_pong_addr(vin_dev, pong_addr);
#else
		vin_dev->hw_ops->vin_wr_set_pong_addr(vin_dev, ping_addr);
#endif
		break;
	case VIN_LINE_ISP0: // isp0
	case VIN_LINE_ISP1: // isp1

		vin_dev->hw_ops->vin_isp_set_yuv_addr(vin_dev,
			line->id - VIN_LINE_ISP0,
			y_addr, uv_addr);

		break;
	case VIN_LINE_ISP0_RAW: // isp0_raw
	case VIN_LINE_ISP1_RAW: // isp1_raw
		vin_dev->hw_ops->vin_isp_set_raw_addr(vin_dev,
			line->id - VIN_LINE_ISP0_RAW, y_addr);
		break;
	default:
		break;
	}
}

static void vin_init_outputs(struct vin_line *line)
{
	struct vin_output *output = &line->output;

	output->state = VIN_OUTPUT_OFF;
	output->buf[0] = NULL;
	output->buf[1] = NULL;
	output->active_buf = 0;
	INIT_LIST_HEAD(&output->pending_bufs);
}

static void vin_buf_add_pending(struct vin_output *output,
				struct stfcamss_buffer *buffer)
{
	INIT_LIST_HEAD(&buffer->queue);
	list_add_tail(&buffer->queue, &output->pending_bufs);
}

static struct stfcamss_buffer *vin_buf_get_pending(struct vin_output *output)
{
	struct stfcamss_buffer *buffer = NULL;

	if (!list_empty(&output->pending_bufs)) {
		buffer = list_first_entry(&output->pending_bufs,
					struct stfcamss_buffer,
					queue);
		list_del(&buffer->queue);
	}

	return buffer;
}

#if 0
static void vin_output_checkpending(struct vin_line *line)
{
	struct vin_output *output = &line->output;

	if (output->state == VIN_OUTPUT_STOPPING) {
		/* Release last buffer when hw is idle */
		if (output->last_buffer) {
			// vb2_buffer_done(&output->last_buffer->vb.vb2_buf,
			//		VB2_BUF_STATE_DONE);
			vin_buf_add_pending(output, output->last_buffer);
			output->last_buffer = NULL;
		}
		output->state = VIN_OUTPUT_IDLE;

		/* Buffers received in stopping state are queued in */
		/* dma pending queue, start next capture here */
		output->buf[0] = vin_buf_get_pending(output);
#ifdef VIN_TWO_BUFFER
		if (line->id == VIN_LINE_WR)
			output->buf[1] = vin_buf_get_pending(output);
#endif

		if (!output->buf[0] && output->buf[1]) {
			output->buf[0] = output->buf[1];
			output->buf[1] = NULL;
		}

		if (output->buf[0])
			output->state = VIN_OUTPUT_SINGLE;

#ifdef VIN_TWO_BUFFER
		if (output->buf[1] && line->id == VIN_LINE_WR)
			output->state = VIN_OUTPUT_CONTINUOUS;
#endif
		vin_output_init_addrs(line);
	}
}
#endif

static void vin_buf_update_on_last(struct vin_line *line)
{
	struct vin_output *output = &line->output;

	switch (output->state) {
	case VIN_OUTPUT_CONTINUOUS:
		output->state = VIN_OUTPUT_SINGLE;
		output->active_buf = !output->active_buf;
		break;
	case VIN_OUTPUT_SINGLE:
		output->state = VIN_OUTPUT_STOPPING;
		break;
	default:
		st_err_ratelimited(ST_VIN,
				"Last buff in wrong state! %d\n",
				output->state);
		break;
	}
}

static void vin_buf_update_on_next(struct vin_line *line)
{
	struct vin_output *output = &line->output;

	switch (output->state) {
	case VIN_OUTPUT_CONTINUOUS:
		output->active_buf = !output->active_buf;
		break;
	case VIN_OUTPUT_SINGLE:
	default:
#ifdef VIN_TWO_BUFFER
		if (line->id == VIN_LINE_WR)
			st_err_ratelimited(ST_VIN,
				"Next buf in wrong state! %d\n",
				output->state);
#endif
		break;
	}
}

static void vin_buf_update_on_new(struct vin_line *line,
				struct vin_output *output,
				struct stfcamss_buffer *new_buf)
{
	switch (output->state) {
	case VIN_OUTPUT_SINGLE:
#ifdef VIN_TWO_BUFFER
		int inactive_idx = !output->active_buf;

		if (!output->buf[inactive_idx] && line->id == VIN_LINE_WR) {
			output->buf[inactive_idx] = new_buf;
			if (inactive_idx)
				vin_dev->hw_ops->vin_wr_set_pong_addr(vin_dev,
						output->buf[1]->addr[0]);
			else
				vin_dev->hw_ops->vin_wr_set_ping_addr(vin_dev,
						output->buf[0]->addr[0]);
			output->state = VIN_OUTPUT_CONTINUOUS;

		} else {
			vin_buf_add_pending(output, new_buf);
			if (line->id == VIN_LINE_WR)
				st_warn(ST_VIN, "Inactive buffer is busy\n");
		}
#else
		vin_buf_add_pending(output, new_buf);
#endif
		break;
	case VIN_OUTPUT_IDLE:
		st_warn(ST_VIN,	"Output idle buffer set!\n");
		if (!output->buf[0]) {
			output->buf[0] = new_buf;
			vin_output_init_addrs(line);
			output->state = VIN_OUTPUT_SINGLE;
		} else {
			vin_buf_add_pending(output, new_buf);
			st_warn(ST_VIN,	"Output idle with buffer set!\n");
		}
		break;
	case VIN_OUTPUT_STOPPING:
		if (output->last_buffer) {
			output->buf[output->active_buf] = output->last_buffer;
			output->last_buffer = NULL;
		} else
			st_err(ST_VIN,	"stop state lost lastbuffer!\n");
		output->state = VIN_OUTPUT_SINGLE;
		// vin_output_checkpending(line);
		vin_buf_add_pending(output, new_buf);
		break;
	case VIN_OUTPUT_CONTINUOUS:
	default:
		vin_buf_add_pending(output, new_buf);
		break;
	}
}

static void vin_buf_flush_pending(struct vin_output *output,
				enum vb2_buffer_state state)
{
	struct stfcamss_buffer *buf;
	struct stfcamss_buffer *t;

	list_for_each_entry_safe(buf, t, &output->pending_bufs, queue) {
		vb2_buffer_done(&buf->vb.vb2_buf, state);
		list_del(&buf->queue);
	}
}

static void vin_buffer_done(struct vin_line *line, struct vin_params *params)
{
	struct stfcamss_buffer *ready_buf;
	struct vin_output *output = &line->output;
	struct stf_vin2_dev *vin_dev = line_to_vin2_dev(line);
	dma_addr_t *new_addr;
	unsigned long flags;
	u32 active_index;
	u64 ts = ktime_get_ns();

	if (output->state == VIN_OUTPUT_OFF
		|| output->state == VIN_OUTPUT_STOPPING
		|| output->state == VIN_OUTPUT_RESERVED
		|| output->state == VIN_OUTPUT_IDLE) {
		st_warn(ST_VIN,
				"output state no ready %d!, %d\n",
				output->state, line->id);
		return;
	}

	spin_lock_irqsave(&line->output_lock, flags);

	if (output->frame_skip) {
		output->frame_skip--;
		goto out_unlock;
	}

	active_index = output->active_buf;

	ready_buf = output->buf[active_index];
	if (!ready_buf) {
		st_err_ratelimited(ST_VIN,
					"Missing ready buf %d %d!\n",
					active_index, output->state);
		active_index = !active_index;
		ready_buf = output->buf[active_index];
		if (!ready_buf) {
			st_err_ratelimited(ST_VIN,
					"Missing ready buf 2 %d %d!\n",
					active_index, output->state);
			goto out_unlock;
		}
	}

	/* Get next buffer */
	output->buf[active_index] = vin_buf_get_pending(output);
	if (!output->buf[active_index]) {
		/* No next buffer - set same address */
		new_addr = ready_buf->addr;
		vin_buf_update_on_last(line);
	} else {
		new_addr = output->buf[active_index]->addr;
		vin_buf_update_on_next(line);
	}

	switch (line->id) {
	case VIN_LINE_WR:  // wr
#ifdef VIN_TWO_BUFFER
		if (active_index)
			vin_dev->hw_ops->vin_wr_set_pong_addr(vin_dev,
					new_addr[0]);
		else
			vin_dev->hw_ops->vin_wr_set_ping_addr(vin_dev,
					new_addr[0]);
#else
		vin_dev->hw_ops->vin_wr_set_ping_addr(vin_dev,
				new_addr[0]);
		vin_dev->hw_ops->vin_wr_set_pong_addr(vin_dev,
				new_addr[0]);
#endif
		break;
	case VIN_LINE_ISP0: // isp0
	case VIN_LINE_ISP1: // isp1
		vin_dev->hw_ops->vin_isp_set_yuv_addr(vin_dev,
			line->id - VIN_LINE_ISP0,
			new_addr[0], new_addr[1]);
		break;
	case VIN_LINE_ISP0_RAW: // isp0_raw
	case VIN_LINE_ISP1_RAW: // isp1_raw
		vin_dev->hw_ops->vin_isp_set_raw_addr(vin_dev,
			line->id - VIN_LINE_ISP0_RAW, new_addr[0]);
		break;

	default:
		break;
	}

	if (output->state == VIN_OUTPUT_STOPPING)
		output->last_buffer = ready_buf;
	else {
		ready_buf->vb.vb2_buf.timestamp = ts;
		ready_buf->vb.sequence = output->sequence++;
		vb2_buffer_done(&ready_buf->vb.vb2_buf, VB2_BUF_STATE_DONE);
	}

	spin_unlock_irqrestore(&line->output_lock, flags);
	return;

out_unlock:
	spin_unlock_irqrestore(&line->output_lock, flags);
}

static int vin_queue_buffer(struct stfcamss_video *vid,
				struct stfcamss_buffer *buf)
{
	struct vin_line *line = container_of(vid, struct vin_line, video_out);
	struct vin_output *output;
	unsigned long flags;


	output = &line->output;

	spin_lock_irqsave(&line->output_lock, flags);

	vin_buf_update_on_new(line, output, buf);

	spin_unlock_irqrestore(&line->output_lock, flags);

	return 0;
}

static int vin_flush_buffers(struct stfcamss_video *vid,
				enum vb2_buffer_state state)
{
	struct vin_line *line = container_of(vid, struct vin_line, video_out);
	struct vin_output *output = &line->output;
	unsigned long flags;

	spin_lock_irqsave(&line->output_lock, flags);

	vin_buf_flush_pending(output, state);
	if (output->buf[0])
		vb2_buffer_done(&output->buf[0]->vb.vb2_buf, state);

	if (output->buf[1])
		vb2_buffer_done(&output->buf[1]->vb.vb2_buf, state);

	if (output->last_buffer) {
		vb2_buffer_done(&output->last_buffer->vb.vb2_buf, state);
		output->last_buffer = NULL;
	}
	output->buf[0] = output->buf[1] = NULL;

	spin_unlock_irqrestore(&line->output_lock, flags);
	return 0;
}

static int vin_link_setup(struct media_entity *entity,
			const struct media_pad *local,
			const struct media_pad *remote, u32 flags)
{
	if (flags & MEDIA_LNK_FL_ENABLED)
		if (media_entity_remote_pad(local))
			return -EBUSY;
	return 0;
}

static const struct v4l2_subdev_core_ops vin_core_ops = {
	.s_power = vin_set_power,
};

static const struct v4l2_subdev_video_ops vin_video_ops = {
	.s_stream = vin_set_stream,
};

static const struct v4l2_subdev_pad_ops vin_pad_ops = {
	.enum_mbus_code   = vin_enum_mbus_code,
	.enum_frame_size  = vin_enum_frame_size,
	.get_fmt          = vin_get_format,
	.set_fmt          = vin_set_format,
};

static const struct v4l2_subdev_ops vin_v4l2_ops = {
	.core = &vin_core_ops,
	.video = &vin_video_ops,
	.pad = &vin_pad_ops,
};

static const struct v4l2_subdev_internal_ops vin_v4l2_internal_ops = {
	.open = vin_init_formats,
};

static const struct stfcamss_video_ops stfcamss_vin_video_ops = {
	.queue_buffer = vin_queue_buffer,
	.flush_buffers = vin_flush_buffers,
};

static const struct media_entity_operations vin_media_ops = {
	.link_setup = vin_link_setup,
	.link_validate = v4l2_subdev_link_validate,
};

int stf_vin_register(struct stf_vin2_dev *vin_dev, struct v4l2_device *v4l2_dev)
{
	struct v4l2_subdev *sd;
	struct stfcamss_video *video_out;
	struct media_pad *pads;
	int ret;
	int i;

	for (i = 0; i < VIN_LINE_MAX; i++) {
		char name[32];
		char *sub_name = get_line_subdevname(i);
		int is_mp;

		is_mp = (i == VIN_LINE_ISP0) || (i == VIN_LINE_ISP1) ? true : false;
		is_mp = false;
		sd = &vin_dev->line[i].subdev;
		pads = vin_dev->line[i].pads;
		video_out = &vin_dev->line[i].video_out;
		video_out->id = i;

		v4l2_subdev_init(sd, &vin_v4l2_ops);
		sd->internal_ops = &vin_v4l2_internal_ops;
		sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
		snprintf(sd->name, ARRAY_SIZE(sd->name), "%s%d_%s",
			STF_VIN_NAME, vin_dev->id, sub_name);
		v4l2_set_subdevdata(sd, &vin_dev->line[i]);

		ret = vin_init_formats(sd, NULL);
		if (ret < 0) {
			st_err(ST_VIN, "Failed to init format: %d\n", ret);
			goto err_init;
		}

		pads[STF_VIN_PAD_SINK].flags = MEDIA_PAD_FL_SINK;
		pads[STF_VIN_PAD_SRC].flags = MEDIA_PAD_FL_SOURCE;

		sd->entity.function =
			MEDIA_ENT_F_PROC_VIDEO_PIXEL_FORMATTER;
		sd->entity.ops = &vin_media_ops;
		ret = media_entity_pads_init(&sd->entity,
				STF_VIN_PADS_NUM, pads);
		if (ret < 0) {
			st_err(ST_VIN, "Failed to init media entity: %d\n", ret);
			goto err_init;
		}

		ret = v4l2_device_register_subdev(v4l2_dev, sd);
		if (ret < 0) {
			st_err(ST_VIN, "Failed to register subdev: %d\n", ret);
			goto err_reg_subdev;
		}

		video_out->ops = &stfcamss_vin_video_ops;
		video_out->bpl_alignment = 16 * 8;

		snprintf(name, ARRAY_SIZE(name), "%s_%s%d",
			sd->name, "video", i);
		ret = stf_video_register(video_out, v4l2_dev, name, is_mp);
		if (ret < 0) {
			st_err(ST_VIN, "Failed to register video node: %d\n",
					ret);
			goto err_vid_reg;
		}

		ret = media_create_pad_link(
			&sd->entity, STF_VIN_PAD_SRC,
			&video_out->vdev.entity, 0,
			MEDIA_LNK_FL_IMMUTABLE | MEDIA_LNK_FL_ENABLED);
		if (ret < 0) {
			st_err(ST_VIN, "Failed to link %s->%s entities: %d\n",
				sd->entity.name, video_out->vdev.entity.name,
				ret);
			goto err_create_link;
		}
	}

	return 0;

err_create_link:
	stf_video_unregister(video_out);
err_vid_reg:
	v4l2_device_unregister_subdev(sd);
err_reg_subdev:
	media_entity_cleanup(&sd->entity);
err_init:
	for (i--; i >= 0; i--) {
		sd = &vin_dev->line[i].subdev;
		video_out = &vin_dev->line[i].video_out;

		stf_video_unregister(video_out);
		v4l2_device_unregister_subdev(sd);
		media_entity_cleanup(&sd->entity);
	}
	return ret;
}

int stf_vin_unregister(struct stf_vin2_dev *vin_dev)
{
	struct v4l2_subdev *sd;
	struct stfcamss_video *video_out;
	int i;

	mutex_destroy(&vin_dev->power_lock);
	for (i = 0; i < VIN_LINE_MAX; i++) {
		sd = &vin_dev->line[i].subdev;
		video_out = &vin_dev->line[i].video_out;

		stf_video_unregister(video_out);
		v4l2_device_unregister_subdev(sd);
		media_entity_cleanup(&sd->entity);
		mutex_destroy(&vin_dev->line[i].stream_lock);
		mutex_destroy(&vin_dev->line[i].power_lock);
	}
	return 0;
}
