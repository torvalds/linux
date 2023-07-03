// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2021-2023 StarFive Technology Co., Ltd.
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/dma-mapping.h>
#include <linux/pm_runtime.h>
#include <media/v4l2-event.h>

#include "stfcamss.h"

#define vin_line_array(ptr_line) \
		((const struct vin_line (*)[]) &(ptr_line[-(ptr_line->id)]))

#define line_to_vin2_dev(ptr_line) \
		container_of(vin_line_array(ptr_line), struct stf_vin2_dev, line)

#define VIN_FRAME_DROP_MAX_VAL 90
#define VIN_FRAME_DROP_MIN_VAL 4
#define VIN_FRAME_PER_SEC_MAX_VAL 90

/* ISP ctrl need 1 sec to let frames become stable. */
#define VIN_FRAME_DROP_SEC_FOR_ISP_CTRL 1


// #define VIN_TWO_BUFFER

static const struct vin2_format vin2_formats_st7110[] = {
	{ MEDIA_BUS_FMT_YUYV8_2X8, 16},
	{ MEDIA_BUS_FMT_RGB565_2X8_LE, 16},
	{ MEDIA_BUS_FMT_SRGGB8_1X8, 8},
	{ MEDIA_BUS_FMT_SGRBG8_1X8, 8},
	{ MEDIA_BUS_FMT_SGBRG8_1X8, 8},
	{ MEDIA_BUS_FMT_SBGGR8_1X8, 8},
	{ MEDIA_BUS_FMT_SRGGB10_1X10, 10},
	{ MEDIA_BUS_FMT_SGRBG10_1X10, 10},
	{ MEDIA_BUS_FMT_SGBRG10_1X10, 10},
	{ MEDIA_BUS_FMT_SBGGR10_1X10, 10},
	{ MEDIA_BUS_FMT_SRGGB12_1X12, 12},
	{ MEDIA_BUS_FMT_SGRBG12_1X12, 12},
	{ MEDIA_BUS_FMT_SGBRG12_1X12, 12},
	{ MEDIA_BUS_FMT_SBGGR12_1X12, 12},
	{ MEDIA_BUS_FMT_Y12_1X12, 8},
	{ MEDIA_BUS_FMT_YUV8_1X24, 8},
	{ MEDIA_BUS_FMT_AYUV8_1X32, 32},
};

static const struct vin2_format isp_formats_st7110_raw[] = {
	{ MEDIA_BUS_FMT_SBGGR12_1X12, 12},
	{ MEDIA_BUS_FMT_SRGGB12_1X12, 12},
	{ MEDIA_BUS_FMT_SGRBG12_1X12, 12},
	{ MEDIA_BUS_FMT_SGBRG12_1X12, 12},
};

static const struct vin2_format isp_formats_st7110_uo[] = {
	{ MEDIA_BUS_FMT_Y12_1X12, 8},
};

static const struct vin2_format isp_formats_st7110_iti[] = {
	{ MEDIA_BUS_FMT_SRGGB10_1X10, 10},
	{ MEDIA_BUS_FMT_SGRBG10_1X10, 10},
	{ MEDIA_BUS_FMT_SGBRG10_1X10, 10},
	{ MEDIA_BUS_FMT_SBGGR10_1X10, 10},
	{ MEDIA_BUS_FMT_SRGGB12_1X12, 12},
	{ MEDIA_BUS_FMT_SGRBG12_1X12, 12},
	{ MEDIA_BUS_FMT_SGBRG12_1X12, 12},
	{ MEDIA_BUS_FMT_SBGGR12_1X12, 12},
	{ MEDIA_BUS_FMT_Y12_1X12, 8},
	{ MEDIA_BUS_FMT_YUV8_1X24, 8},
};

static const struct vin2_format_table vin2_formats_table[] = {
	/* VIN_LINE_WR */
	{ vin2_formats_st7110, ARRAY_SIZE(vin2_formats_st7110) },
	/* VIN_LINE_ISP */
	{ isp_formats_st7110_uo, ARRAY_SIZE(isp_formats_st7110_uo) },
	/* VIN_LINE_ISP_SS0 */
	{ isp_formats_st7110_uo, ARRAY_SIZE(isp_formats_st7110_uo) },
	/* VIN_LINE_ISP_SS1 */
	{ isp_formats_st7110_uo, ARRAY_SIZE(isp_formats_st7110_uo) },
	/* VIN_LINE_ISP_ITIW */
	{ isp_formats_st7110_iti, ARRAY_SIZE(isp_formats_st7110_iti) },
	/* VIN_LINE_ISP_ITIR */
	{ isp_formats_st7110_iti, ARRAY_SIZE(isp_formats_st7110_iti) },
	/* VIN_LINE_ISP_RAW */
	{ isp_formats_st7110_raw, ARRAY_SIZE(isp_formats_st7110_raw) },
	/* VIN_LINE_ISP_SCD_Y */
	{ isp_formats_st7110_raw, ARRAY_SIZE(isp_formats_st7110_raw) },
};

static void vin_buffer_done(struct vin_line *line, struct vin_params *params);
static void vin_change_buffer(struct vin_line *line);
static struct stfcamss_buffer *vin_buf_get_pending(struct vin_output *output);
static void vin_output_init_addrs(struct vin_line *line);
static void vin_init_outputs(struct vin_line *line);
static struct v4l2_mbus_framefmt *
__vin_get_format(struct vin_line *line,
		struct v4l2_subdev_state *state,
		unsigned int pad,
		enum v4l2_subdev_format_whence which);

static char *get_line_subdevname(int line_id)
{
	char *name = NULL;

	switch (line_id) {
	case VIN_LINE_WR:
		name = "wr";
		break;
	case VIN_LINE_ISP:
		name = "isp0";
		break;
	case VIN_LINE_ISP_SS0:
		name = "isp0_ss0";
		break;
	case VIN_LINE_ISP_SS1:
		name = "isp0_ss1";
		break;
	case VIN_LINE_ISP_ITIW:
		name = "isp0_itiw";
		break;
	case VIN_LINE_ISP_ITIR:
		name = "isp0_itir";
		break;
	case VIN_LINE_ISP_RAW:
		name = "isp0_raw";
		break;
	case VIN_LINE_ISP_SCD_Y:
		name = "isp0_scd_y";
		break;
	default:
		name = "unknow";
		break;
	}
	return name;
}

static enum isp_line_id stf_vin_map_isp_line(enum vin_line_id line)
{
	enum isp_line_id line_id;

	if ((line > VIN_LINE_WR) && (line < VIN_LINE_MAX)) {
		line_id = line % STF_ISP_LINE_MAX;
		if (line_id == 0)
			line_id = STF_ISP_LINE_SRC_SCD_Y;
	} else
		line_id = STF_ISP_LINE_INVALID;

	return line_id;
}

enum isp_pad_id stf_vin_map_isp_pad(enum vin_line_id line, enum isp_pad_id def)
{
	enum isp_pad_id pad_id;

	if (line == VIN_LINE_WR)
		pad_id = STF_ISP_PAD_SINK;
	else if ((line > VIN_LINE_WR) && (line < VIN_LINE_MAX))
		pad_id = stf_vin_map_isp_line(line);
	else
		pad_id = def;

	return pad_id;
}

int stf_vin_subdev_init(struct stfcamss *stfcamss)
{
	struct stf_vin_dev *vin;
	struct device *dev = stfcamss->dev;
	struct stf_vin2_dev *vin_dev = stfcamss->vin_dev;
	int i, ret = 0;

	vin_dev->stfcamss = stfcamss;
	vin_dev->hw_ops = &vin_ops;
	vin_dev->hw_ops->isr_buffer_done = vin_buffer_done;
	vin_dev->hw_ops->isr_change_buffer = vin_change_buffer;

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
			vin->isp_irq, vin_dev->hw_ops->vin_isp_irq_handler,
			0, "vin_isp_irq", vin_dev);
	if (ret) {
		st_err(ST_VIN, "failed to request isp irq\n");
		goto out;
	}

	st_info(ST_CAMSS, "%s, %d!\n", __func__, __LINE__);
#ifdef ISP_USE_CSI_AND_SC_DONE_INTERRUPT
	ret = devm_request_irq(dev,
			vin->isp_csi_irq, vin_dev->hw_ops->vin_isp_csi_irq_handler,
			0, "vin_isp_csi_irq", vin_dev);
	if (ret) {
		st_err(ST_VIN, "failed to request isp raw irq\n");
		goto out;
	}

	ret = devm_request_irq(dev,
			vin->isp_scd_irq, vin_dev->hw_ops->vin_isp_scd_irq_handler,
			0, "vin_isp_scd_irq", vin_dev);
	if (ret) {
		st_err(ST_VIN, "failed to request isp scd irq\n");
		goto out;
	}
#endif

	ret = devm_request_irq(dev,
			vin->isp_irq_csiline, vin_dev->hw_ops->vin_isp_irq_csiline_handler,
			0, "vin_isp_irq_csiline", vin_dev);
	if (ret) {
		st_err(ST_VIN, "failed to request isp irq csiline\n");
		goto out;
	}

	mutex_init(&vin_dev->power_lock);
	vin_dev->power_count = 0;

	for (i = 0; i < STF_DUMMY_MODULE_NUMS; i++) {
		struct dummy_buffer *dummy_buffer = &vin_dev->dummy_buffer[i];

		mutex_init(&dummy_buffer->stream_lock);
		dummy_buffer->nums = i == 0 ? VIN_DUMMY_BUFFER_NUMS : ISP_DUMMY_BUFFER_NUMS;
		dummy_buffer->stream_count = 0;
		dummy_buffer->buffer = devm_kzalloc(dev,
			dummy_buffer->nums * sizeof(struct vin_dummy_buffer), GFP_KERNEL);
		atomic_set(&dummy_buffer->frame_skip, 0);
	}

	for (i = VIN_LINE_WR;
		i < STF_ISP_LINE_MAX + 1; i++) {
		struct vin_line *l = &vin_dev->line[i];
		int is_mp;

		is_mp = i == VIN_LINE_WR ? false : true;
		is_mp = false;
		if (stf_vin_map_isp_line(i) == STF_ISP_LINE_SRC_ITIR)
			l->video_out.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
		else
			l->video_out.type = is_mp ? V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE :
				V4L2_BUF_TYPE_VIDEO_CAPTURE;
		l->video_out.stfcamss = stfcamss;
		l->id = i;
		l->sdev_type = VIN_DEV_TYPE;
		l->formats = vin2_formats_table[i].fmts;
		l->nformats = vin2_formats_table[i].nfmts;
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
	struct stfcamss *stfcamss = vin_dev->stfcamss;

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
			pm_runtime_get_sync(stfcamss->dev);
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
			pm_runtime_put_sync(stfcamss->dev);
		}
		vin_dev->power_count--;
	}
exit:

	mutex_unlock(&vin_dev->power_lock);

	return 0;
}

static unsigned int get_frame_skip(struct vin_line *line)
{
	unsigned int frame_skip = 0;
	unsigned int isp_ctrl_skip_frames = 0;
	struct media_entity *sensor;
	struct v4l2_subdev_frame_interval fi;

	sensor = stfcamss_find_sensor(&line->subdev.entity);
	if (sensor) {
		int fps = 0;
		struct v4l2_subdev *subdev =
					media_entity_to_v4l2_subdev(sensor);

		if (subdev->ops->video->g_frame_interval) {
			if (!subdev->ops->video->g_frame_interval(subdev, &fi))
				fps = fi.interval.denominator;

			if (fps > 0 && fps <= 90)
				isp_ctrl_skip_frames = fps * VIN_FRAME_DROP_SEC_FOR_ISP_CTRL;
		}
		if (!fps)
			st_debug(ST_VIN, "%s, Failed to get sensor fps !\n", __func__);

		if (isp_ctrl_skip_frames <= VIN_FRAME_DROP_MIN_VAL)
			isp_ctrl_skip_frames = VIN_FRAME_DROP_MIN_VAL;

		v4l2_subdev_call(subdev, sensor, g_skip_frames, &frame_skip);

		frame_skip += isp_ctrl_skip_frames;

		if (frame_skip > VIN_FRAME_DROP_MAX_VAL)
			frame_skip = VIN_FRAME_DROP_MAX_VAL;
		st_debug(ST_VIN, "%s, frame_skip %d\n", __func__, frame_skip);
	}

	return frame_skip;
}

static void vin_buf_l2cache_flush(struct vin_output *output)
{
	struct stfcamss_buffer *buffer = NULL;

	if (!list_empty(&output->pending_bufs)) {
		list_for_each_entry(buffer, &output->pending_bufs, queue) {
			sifive_l2_flush64_range(buffer->addr[0], buffer->sizeimage);
		}
	}
}

static int vin_enable_output(struct vin_line *line)
{
	struct vin_output *output = &line->output;
	unsigned long flags;

	spin_lock_irqsave(&line->output_lock, flags);

	output->state = VIN_OUTPUT_IDLE;

	vin_buf_l2cache_flush(output);

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

static u32 line_to_dummy_module(struct vin_line *line)
{
	u32 dummy_module = 0;

	switch (line->id) {
	case VIN_LINE_WR:
		dummy_module = STF_DUMMY_VIN;
		break;
	case VIN_LINE_ISP:
	case VIN_LINE_ISP_SS0:
	case VIN_LINE_ISP_SS1:
	case VIN_LINE_ISP_ITIW:
	case VIN_LINE_ISP_ITIR:
	case VIN_LINE_ISP_RAW:
	case VIN_LINE_ISP_SCD_Y:
		dummy_module = STF_DUMMY_ISP;
		break;
	default:
		dummy_module = STF_DUMMY_VIN;
		break;
	}

	return dummy_module;
}

static int vin_alloc_dummy_buffer(struct stf_vin2_dev *vin_dev,
		struct v4l2_mbus_framefmt *fmt, int dummy_module)
{
	struct device *dev = vin_dev->stfcamss->dev;
	struct dummy_buffer *dummy_buffer = &vin_dev->dummy_buffer[dummy_module];
	struct vin_dummy_buffer *buffer = NULL;
	int ret = 0, i;
	u32 aligns;

	for (i = 0; i < dummy_buffer->nums; i++) {
		buffer = &vin_dev->dummy_buffer[dummy_module].buffer[i];
		buffer->width = fmt->width;
		buffer->height = fmt->height;
		buffer->mcode = fmt->code;
		if (i == STF_VIN_PAD_SINK) {
			aligns = ALIGN(fmt->width * 4, STFCAMSS_FRAME_WIDTH_ALIGN_8);
			buffer->buffer_size = PAGE_ALIGN(aligns * fmt->height);
		} else if (i == STF_ISP_PAD_SRC
			|| i == STF_ISP_PAD_SRC_SS0
			|| i == STF_ISP_PAD_SRC_SS1) {
			aligns = ALIGN(fmt->width, STFCAMSS_FRAME_WIDTH_ALIGN_8);
			buffer->buffer_size = PAGE_ALIGN(aligns * fmt->height * 3 / 2);
		} else if (i == STF_ISP_PAD_SRC_ITIW
			|| i == STF_ISP_PAD_SRC_ITIR) {
			aligns = ALIGN(fmt->width, STFCAMSS_FRAME_WIDTH_ALIGN_8);
			buffer->buffer_size = PAGE_ALIGN(aligns * fmt->height * 3);
		} else if (i == STF_ISP_PAD_SRC_RAW) {
			aligns = ALIGN(fmt->width * ISP_RAW_DATA_BITS / 8,
					STFCAMSS_FRAME_WIDTH_ALIGN_128);
			buffer->buffer_size = PAGE_ALIGN(aligns * fmt->height);
		} else if (i == STF_ISP_PAD_SRC_SCD_Y)
			buffer->buffer_size = PAGE_ALIGN(ISP_SCD_Y_BUFFER_SIZE);
		else
			continue;

		buffer->vaddr = dma_alloc_coherent(dev, buffer->buffer_size,
				&buffer->paddr[0], GFP_DMA | GFP_KERNEL);

		if (buffer->vaddr) {
			if (i == STF_ISP_PAD_SRC
				|| i == STF_ISP_PAD_SRC_SS0
				|| i == STF_ISP_PAD_SRC_SS1
				|| i == STF_ISP_PAD_SRC_ITIW
				|| i == STF_ISP_PAD_SRC_ITIR)
				buffer->paddr[1] = (dma_addr_t)(buffer->paddr[0] +
									aligns * fmt->height);
			else if (i == STF_ISP_PAD_SRC_SCD_Y)
				buffer->paddr[1] = (dma_addr_t)(buffer->paddr[0] +
									ISP_YHIST_BUFFER_SIZE);
			else
				st_debug(ST_VIN, "signal plane\n");
		}
		{
			char szPadName[][32] = {
				"VIN_PAD_SINK",
				"ISP_PAD_SRC",
				"ISP_PAD_SRC_SS0",
				"ISP_PAD_SRC_SS1",
				"ISP_PAD_SRC_ITIW",
				"ISP_PAD_SRC_ITIR",
				"ISP_PAD_SRC_RAW",
				"ISP_PAD_SRC_SCD_Y",
				"Unknown Pad"
			};

			st_debug(ST_VIN, "%s: i = %d(%s) addr[0] = %llx, addr[1] = %llx, size = %u bytes\n",
				__func__,
				i,
				szPadName[i],
				buffer->paddr[0],
				buffer->paddr[1],
				buffer->buffer_size
				);
		}
	}

	return ret;
}

static void vin_free_dummy_buffer(struct stf_vin2_dev *vin_dev, int dummy_module)
{
	struct device *dev = vin_dev->stfcamss->dev;
	struct dummy_buffer *dummy_buffer = &vin_dev->dummy_buffer[dummy_module];
	struct vin_dummy_buffer *buffer = NULL;
	int i;

	for (i = 0; i < dummy_buffer->nums; i++) {
		buffer = &dummy_buffer->buffer[i];
		if (buffer->vaddr)
			dma_free_coherent(dev, buffer->buffer_size,
						buffer->vaddr, buffer->paddr[0]);
		memset(buffer, 0, sizeof(struct vin_dummy_buffer));
	}
}

static void vin_set_dummy_buffer(struct vin_line *line, u32 pad)
{
	struct stf_vin2_dev *vin_dev = line_to_vin2_dev(line);
	int dummy_module = line_to_dummy_module(line);
	struct dummy_buffer *dummy_buffer = &vin_dev->dummy_buffer[dummy_module];
	struct vin_dummy_buffer *buffer = NULL;

	switch (pad) {
	case STF_VIN_PAD_SINK:
		if (line->id == VIN_LINE_WR) {
			buffer = &dummy_buffer->buffer[STF_VIN_PAD_SINK];
			vin_dev->hw_ops->vin_wr_set_ping_addr(vin_dev, buffer->paddr[0]);
			vin_dev->hw_ops->vin_wr_set_pong_addr(vin_dev, buffer->paddr[0]);
		} else {
			buffer = &dummy_buffer->buffer[STF_ISP_PAD_SRC];
			vin_dev->hw_ops->vin_isp_set_yuv_addr(vin_dev,
				buffer->paddr[0], buffer->paddr[1]);

			buffer = &dummy_buffer->buffer[STF_ISP_PAD_SRC_SS0];
			vin_dev->hw_ops->vin_isp_set_ss0_addr(vin_dev,
				buffer->paddr[0], buffer->paddr[1]);

			buffer = &dummy_buffer->buffer[STF_ISP_PAD_SRC_SS1];
			vin_dev->hw_ops->vin_isp_set_ss1_addr(vin_dev,
				buffer->paddr[0], buffer->paddr[1]);

			buffer = &dummy_buffer->buffer[STF_ISP_PAD_SRC_ITIW];
			vin_dev->hw_ops->vin_isp_set_itiw_addr(vin_dev,
				buffer->paddr[0], buffer->paddr[1]);

			buffer = &dummy_buffer->buffer[STF_ISP_PAD_SRC_ITIR];
			vin_dev->hw_ops->vin_isp_set_itir_addr(vin_dev,
				buffer->paddr[0], buffer->paddr[1]);

			buffer = &dummy_buffer->buffer[STF_ISP_PAD_SRC_RAW];
			vin_dev->hw_ops->vin_isp_set_raw_addr(vin_dev, buffer->paddr[0]);

			buffer = &dummy_buffer->buffer[STF_ISP_PAD_SRC_SCD_Y];
			vin_dev->hw_ops->vin_isp_set_scd_addr(vin_dev,
				buffer->paddr[0], buffer->paddr[1], AWB_TYPE);
		}
		break;
	case STF_ISP_PAD_SRC:
		buffer = &dummy_buffer->buffer[STF_ISP_PAD_SRC];
		vin_dev->hw_ops->vin_isp_set_yuv_addr(vin_dev,
			buffer->paddr[0], buffer->paddr[1]);
		break;
	case STF_ISP_PAD_SRC_SS0:
		buffer = &dummy_buffer->buffer[STF_ISP_PAD_SRC_SS0];
		vin_dev->hw_ops->vin_isp_set_ss0_addr(vin_dev,
			buffer->paddr[0], buffer->paddr[1]);
		break;
	case STF_ISP_PAD_SRC_SS1:
		buffer = &dummy_buffer->buffer[STF_ISP_PAD_SRC_SS1];
		vin_dev->hw_ops->vin_isp_set_ss1_addr(vin_dev,
			buffer->paddr[0], buffer->paddr[1]);
		break;
	case STF_ISP_PAD_SRC_ITIW:
		buffer = &dummy_buffer->buffer[STF_ISP_PAD_SRC_ITIW];
		vin_dev->hw_ops->vin_isp_set_itiw_addr(vin_dev,
			buffer->paddr[0], buffer->paddr[1]);
		break;
	case STF_ISP_PAD_SRC_ITIR:
		buffer = &dummy_buffer->buffer[STF_ISP_PAD_SRC_ITIR];
		vin_dev->hw_ops->vin_isp_set_itir_addr(vin_dev,
			buffer->paddr[0], buffer->paddr[1]);
		break;
	case STF_ISP_PAD_SRC_RAW:
		buffer = &dummy_buffer->buffer[STF_ISP_PAD_SRC_RAW];
		vin_dev->hw_ops->vin_isp_set_raw_addr(vin_dev, buffer->paddr[0]);
		break;
	case STF_ISP_PAD_SRC_SCD_Y:
		buffer = &dummy_buffer->buffer[STF_ISP_PAD_SRC_SCD_Y];
		vin_dev->hw_ops->vin_isp_set_scd_addr(vin_dev,
			buffer->paddr[0], buffer->paddr[1], AWB_TYPE);
		break;
	default:
		break;
	}
}

static int vin_set_stream(struct v4l2_subdev *sd, int enable)
{
	struct vin_line *line = v4l2_get_subdevdata(sd);
	struct stf_vin2_dev *vin_dev = line_to_vin2_dev(line);
	int dummy_module = line_to_dummy_module(line);
	struct dummy_buffer *dummy_buffer = &vin_dev->dummy_buffer[dummy_module];
	struct v4l2_mbus_framefmt *fmt;

	st_debug(ST_VIN, "%s, %d\n", __func__, __LINE__);
	fmt = __vin_get_format(line, NULL, STF_VIN_PAD_SINK, V4L2_SUBDEV_FORMAT_ACTIVE);
	mutex_lock(&dummy_buffer->stream_lock);
	if (enable) {
		if (dummy_buffer->stream_count == 0) {
			vin_alloc_dummy_buffer(vin_dev, fmt, dummy_module);
			vin_set_dummy_buffer(line, STF_VIN_PAD_SINK);
			atomic_set(&dummy_buffer->frame_skip, get_frame_skip(line));
		}
		dummy_buffer->stream_count++;
	} else {
		if (dummy_buffer->stream_count == 1) {
			vin_free_dummy_buffer(vin_dev, dummy_module);
			// set buffer addr to zero
			vin_set_dummy_buffer(line, STF_VIN_PAD_SINK);
		} else
			vin_set_dummy_buffer(line,
					stf_vin_map_isp_pad(line->id, STF_ISP_PAD_SINK));

		dummy_buffer->stream_count--;
	}
	mutex_unlock(&dummy_buffer->stream_lock);

	mutex_lock(&line->stream_lock);
	if (enable) {
		if (line->stream_count == 0) {
			if (line->id == VIN_LINE_WR) {
				vin_dev->hw_ops->vin_wr_irq_enable(vin_dev, 1);
				vin_dev->hw_ops->vin_wr_stream_set(vin_dev, 1);
			}
		}
		line->stream_count++;
	} else {
		if (line->stream_count == 1) {
			if (line->id == VIN_LINE_WR) {
				vin_dev->hw_ops->vin_wr_irq_enable(vin_dev, 0);
				vin_dev->hw_ops->vin_wr_stream_set(vin_dev, 0);
			}
		}
		line->stream_count--;
	}
	mutex_unlock(&line->stream_lock);

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
			fmt->code = line->formats[0].code;

		fmt->width = clamp_t(u32,
				fmt->width, STFCAMSS_FRAME_MIN_WIDTH, STFCAMSS_FRAME_MAX_WIDTH);
		fmt->height = clamp_t(u32,
				fmt->height, STFCAMSS_FRAME_MIN_HEIGHT, STFCAMSS_FRAME_MAX_HEIGHT);

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

	st_debug(ST_VIDEO, "%s, pad %d, fmt code  %x\n",
			__func__, fmt->pad, fmt->format.code);

	format = __vin_get_format(line, state, fmt->pad, fmt->which);
	if (format == NULL)
		return -EINVAL;

	mutex_lock(&line->stream_lock);
	if (line->stream_count) {
		fmt->format = *format;
		mutex_unlock(&line->stream_lock);
		goto out;
	} else {
		vin_try_format(line, state, fmt->pad, &fmt->format, fmt->which);
		*format = fmt->format;
	}
	mutex_unlock(&line->stream_lock);

	if (fmt->pad == STF_VIN_PAD_SINK) {
		/* Propagate the format from sink to source */
		format = __vin_get_format(line, state, STF_VIN_PAD_SRC,
					fmt->which);

		*format = fmt->format;
		vin_try_format(line, state, STF_VIN_PAD_SRC, format,
					fmt->which);
	}

out:
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
		return;

	if (output->buf[1])
		pong_addr = output->buf[1]->addr[0];
	else
		pong_addr = ping_addr;

	switch (stf_vin_map_isp_line(line->id)) {
	case STF_ISP_LINE_SRC:
		vin_dev->hw_ops->vin_isp_set_yuv_addr(vin_dev,
			y_addr, uv_addr);
		break;
	case STF_ISP_LINE_SRC_SS0:
		vin_dev->hw_ops->vin_isp_set_ss0_addr(vin_dev,
			y_addr, uv_addr);
		break;
	case STF_ISP_LINE_SRC_SS1:
		vin_dev->hw_ops->vin_isp_set_ss1_addr(vin_dev,
			y_addr, uv_addr);
		break;
	case STF_ISP_LINE_SRC_ITIW:
		vin_dev->hw_ops->vin_isp_set_itiw_addr(vin_dev,
			y_addr, uv_addr);
		break;
	case STF_ISP_LINE_SRC_ITIR:
		vin_dev->hw_ops->vin_isp_set_itir_addr(vin_dev,
			y_addr, uv_addr);
		break;
	case STF_ISP_LINE_SRC_RAW:
		vin_dev->hw_ops->vin_isp_set_raw_addr(vin_dev, y_addr);
		break;
	case STF_ISP_LINE_SRC_SCD_Y:
		output->frame_skip = ISP_AWB_OECF_SKIP_FRAME;
		vin_dev->hw_ops->vin_isp_set_scd_addr(vin_dev,
			y_addr, uv_addr, AWB_TYPE);
		break;
	default:
		if (line->id == VIN_LINE_WR) {
			vin_dev->hw_ops->vin_wr_set_ping_addr(vin_dev, ping_addr);
#ifdef VIN_TWO_BUFFER
			vin_dev->hw_ops->vin_wr_set_pong_addr(vin_dev, pong_addr);
#else
			vin_dev->hw_ops->vin_wr_set_pong_addr(vin_dev, ping_addr);
#endif
		}
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
	INIT_LIST_HEAD(&output->ready_bufs);
}

static void vin_buf_add_ready(struct vin_output *output,
				struct stfcamss_buffer *buffer)
{
	INIT_LIST_HEAD(&buffer->queue);
	list_add_tail(&buffer->queue, &output->ready_bufs);
}

static struct stfcamss_buffer *vin_buf_get_ready(struct vin_output *output)
{
	struct stfcamss_buffer *buffer = NULL;

	if (!list_empty(&output->ready_bufs)) {
		buffer = list_first_entry(&output->ready_bufs,
					struct stfcamss_buffer,
					queue);
		list_del(&buffer->queue);
	}

	return buffer;
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

#ifdef UNUSED_CODE
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
#ifdef VIN_TWO_BUFFER
	struct stf_vin2_dev *vin_dev = line_to_vin2_dev(line);
	int inactive_idx;
#endif

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

static void vin_buf_flush(struct vin_output *output,
				enum vb2_buffer_state state)
{
	struct stfcamss_buffer *buf;
	struct stfcamss_buffer *t;

	list_for_each_entry_safe(buf, t, &output->pending_bufs, queue) {
		vb2_buffer_done(&buf->vb.vb2_buf, state);
		list_del(&buf->queue);
	}
	list_for_each_entry_safe(buf, t, &output->ready_bufs, queue) {
		vb2_buffer_done(&buf->vb.vb2_buf, state);
		list_del(&buf->queue);
	}
}

static void vin_buffer_done(struct vin_line *line, struct vin_params *params)
{
	struct stfcamss_buffer *ready_buf;
	struct vin_output *output = &line->output;
	unsigned long flags;
	u64 ts = ktime_get_ns();
	struct v4l2_event event = {
		.type = V4L2_EVENT_FRAME_SYNC,
	};

	if (output->state == VIN_OUTPUT_OFF
		|| output->state == VIN_OUTPUT_RESERVED)
		return;

	spin_lock_irqsave(&line->output_lock, flags);

	while ((ready_buf = vin_buf_get_ready(output))) {
		//if (line->id >= VIN_LINE_ISP && line->id <= VIN_LINE_ISP_SS1) {
		if (line->id == VIN_LINE_ISP_SCD_Y) {
			event.u.frame_sync.frame_sequence = output->sequence;
			v4l2_event_queue(&(line->video_out.vdev), &event);
			//v4l2_event_queue(line->subdev.devnode, &event);
			//pr_info("----------frame sync-----------\n");
		}

		ready_buf->vb.vb2_buf.timestamp = ts;
		ready_buf->vb.sequence = output->sequence++;

		/* The stf_isp_ctrl currently buffered with mmap,
		 * which will not update cache by default.
		 * Flush L2 cache to make sure data is updated.
		 */
		if (ready_buf->vb.vb2_buf.memory == VB2_MEMORY_MMAP)
			sifive_l2_flush64_range(ready_buf->addr[0], ready_buf->sizeimage);

		vb2_buffer_done(&ready_buf->vb.vb2_buf, VB2_BUF_STATE_DONE);
	}

	spin_unlock_irqrestore(&line->output_lock, flags);
}

static void vin_change_buffer(struct vin_line *line)
{
	struct stfcamss_buffer *ready_buf;
	struct vin_output *output = &line->output;
	struct stf_vin2_dev *vin_dev = line_to_vin2_dev(line);
	dma_addr_t *new_addr;
	unsigned long flags;
	u32 active_index;
	int scd_type;

	if (output->state == VIN_OUTPUT_OFF
		|| output->state == VIN_OUTPUT_STOPPING
		|| output->state == VIN_OUTPUT_RESERVED
		|| output->state == VIN_OUTPUT_IDLE)
		return;

	spin_lock_irqsave(&line->output_lock, flags);

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

	if (output->state == VIN_OUTPUT_STOPPING)
		output->last_buffer = ready_buf;
	else {
		switch (stf_vin_map_isp_line(line->id)) {
		case STF_ISP_LINE_SRC:
			vin_dev->hw_ops->vin_isp_set_yuv_addr(vin_dev,
				new_addr[0], new_addr[1]);
			break;
		case STF_ISP_LINE_SRC_SS0:
			vin_dev->hw_ops->vin_isp_set_ss0_addr(vin_dev,
				new_addr[0], new_addr[1]);
			break;
		case STF_ISP_LINE_SRC_SS1:
			vin_dev->hw_ops->vin_isp_set_ss1_addr(vin_dev,
				new_addr[0], new_addr[1]);
			break;
		case STF_ISP_LINE_SRC_ITIW:
			vin_dev->hw_ops->vin_isp_set_itiw_addr(vin_dev,
				new_addr[0], new_addr[1]);
			break;
		case STF_ISP_LINE_SRC_ITIR:
			vin_dev->hw_ops->vin_isp_set_itir_addr(vin_dev,
				new_addr[0], new_addr[1]);
			break;
		case STF_ISP_LINE_SRC_RAW:
			vin_dev->hw_ops->vin_isp_set_raw_addr(vin_dev, new_addr[0]);
			break;
		case STF_ISP_LINE_SRC_SCD_Y:
			scd_type = vin_dev->hw_ops->vin_isp_get_scd_type(vin_dev);
			ready_buf->vb.flags &= ~(V4L2_BUF_FLAG_PFRAME | V4L2_BUF_FLAG_BFRAME);
			if (scd_type == AWB_TYPE)
				ready_buf->vb.flags |= V4L2_BUF_FLAG_PFRAME;
			else
				ready_buf->vb.flags |= V4L2_BUF_FLAG_BFRAME;
			if (!output->frame_skip) {
				output->frame_skip = ISP_AWB_OECF_SKIP_FRAME;
				scd_type = scd_type == AWB_TYPE ? OECF_TYPE : AWB_TYPE;
			} else {
				output->frame_skip--;
				scd_type = scd_type == AWB_TYPE ? AWB_TYPE : OECF_TYPE;
			}
			vin_dev->hw_ops->vin_isp_set_scd_addr(vin_dev,
				new_addr[0], new_addr[1], scd_type);
			break;
		default:
			if (line->id == VIN_LINE_WR) {
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
			}
			break;
		}

		vin_buf_add_ready(output, ready_buf);
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

	vin_buf_flush(output, state);
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

static int stf_vin_subscribe_event(struct v4l2_subdev *sd,
				   struct v4l2_fh *fh,
				   struct v4l2_event_subscription *sub)
{
	switch (sub->type) {
	case V4L2_EVENT_FRAME_SYNC:
		//return v4l2_event_subscribe(fh, sub, 2, NULL);
		int ret = v4l2_event_subscribe(fh, sub, 2, NULL);
		pr_info("subscribe ret: %d\n", ret);
		return ret;
	default:
		st_debug(ST_VIN, "unsupport subscribe_event\n");
		return -EINVAL;
	}
}

static const struct v4l2_subdev_core_ops vin_core_ops = {
	.s_power = vin_set_power,
	//.subscribe_event = stf_vin_subscribe_event,
	//.unsubscribe_event = v4l2_event_subdev_unsubscribe,
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

	for (i = 0; i < STF_ISP_LINE_MAX + 1; i++) {
		char name[32];
		char *sub_name = get_line_subdevname(i);
		int is_mp;

#ifdef	STF_CAMSS_SKIP_ITI
		if ((stf_vin_map_isp_line(i) == STF_ISP_LINE_SRC_ITIW) ||
			(stf_vin_map_isp_line(i) == STF_ISP_LINE_SRC_ITIR))
			continue;
#endif
		is_mp = (stf_vin_map_isp_line(i) == STF_ISP_LINE_SRC) ? true : false;
		is_mp = false;
		sd = &vin_dev->line[i].subdev;
		pads = vin_dev->line[i].pads;
		video_out = &vin_dev->line[i].video_out;
		video_out->id = i;

		v4l2_subdev_init(sd, &vin_v4l2_ops);
		sd->internal_ops = &vin_v4l2_internal_ops;
		sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE | V4L2_SUBDEV_FL_HAS_EVENTS;
		snprintf(sd->name, ARRAY_SIZE(sd->name), "%s%d_%s",
			STF_VIN_NAME, 0, sub_name);
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
	for (i = 0; i < STF_DUMMY_MODULE_NUMS; i++)
		mutex_destroy(&vin_dev->dummy_buffer[i].stream_lock);

	for (i = 0; i < STF_ISP_LINE_MAX + 1; i++) {
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
