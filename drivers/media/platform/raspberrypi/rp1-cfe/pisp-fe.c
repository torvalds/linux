// SPDX-License-Identifier: GPL-2.0
/*
 * PiSP Front End Driver
 *
 * Copyright (c) 2021-2024 Raspberry Pi Ltd.
 */

#include <linux/bitops.h>
#include <linux/delay.h>
#include <linux/moduleparam.h>
#include <linux/pm_runtime.h>
#include <linux/seq_file.h>

#include <media/videobuf2-dma-contig.h>

#include "cfe.h"
#include "pisp-fe.h"

#include "cfe-trace.h"

#define FE_VERSION		0x000
#define FE_CONTROL		0x004
#define FE_STATUS		0x008
#define FE_FRAME_STATUS		0x00c
#define FE_ERROR_STATUS		0x010
#define FE_OUTPUT_STATUS	0x014
#define FE_INT_EN		0x018
#define FE_INT_STATUS		0x01c

/* CONTROL */
#define FE_CONTROL_QUEUE	BIT(0)
#define FE_CONTROL_ABORT	BIT(1)
#define FE_CONTROL_RESET	BIT(2)
#define FE_CONTROL_LATCH_REGS	BIT(3)

/* INT_EN / INT_STATUS */
#define FE_INT_EOF		BIT(0)
#define FE_INT_SOF		BIT(1)
#define FE_INT_LINES0		BIT(8)
#define FE_INT_LINES1		BIT(9)
#define FE_INT_STATS		BIT(16)
#define FE_INT_QREADY		BIT(24)

/* STATUS */
#define FE_STATUS_QUEUED	BIT(0)
#define FE_STATUS_WAITING	BIT(1)
#define FE_STATUS_ACTIVE	BIT(2)

#define PISP_FE_CONFIG_BASE_OFFSET	0x0040

#define PISP_FE_ENABLE_STATS_CLUSTER \
	(PISP_FE_ENABLE_STATS_CROP | PISP_FE_ENABLE_DECIMATE    | \
	 PISP_FE_ENABLE_BLC        | PISP_FE_ENABLE_CDAF_STATS  | \
	 PISP_FE_ENABLE_AWB_STATS  | PISP_FE_ENABLE_RGBY        | \
	 PISP_FE_ENABLE_LSC        | PISP_FE_ENABLE_AGC_STATS)

#define PISP_FE_ENABLE_OUTPUT_CLUSTER(i)				\
	((PISP_FE_ENABLE_CROP0     | PISP_FE_ENABLE_DOWNSCALE0 |	\
	  PISP_FE_ENABLE_COMPRESS0 | PISP_FE_ENABLE_OUTPUT0) << (4 * (i)))

struct pisp_fe_config_param {
	u32 dirty_flags;
	u32 dirty_flags_extra;
	size_t offset;
	size_t size;
};

static const struct pisp_fe_config_param pisp_fe_config_map[] = {
	/* *_dirty_flag_extra types */
	{ 0, PISP_FE_DIRTY_GLOBAL,
		offsetof(struct pisp_fe_config, global),
		sizeof(struct pisp_fe_global_config) },
	{ 0, PISP_FE_DIRTY_FLOATING,
		offsetof(struct pisp_fe_config, floating_stats),
		sizeof(struct pisp_fe_floating_stats_config) },
	{ 0, PISP_FE_DIRTY_OUTPUT_AXI,
		offsetof(struct pisp_fe_config, output_axi),
		sizeof(struct pisp_fe_output_axi_config) },
	/* *_dirty_flag types */
	{ PISP_FE_ENABLE_INPUT, 0,
		offsetof(struct pisp_fe_config, input),
		sizeof(struct pisp_fe_input_config) },
	{ PISP_FE_ENABLE_DECOMPRESS, 0,
		offsetof(struct pisp_fe_config, decompress),
		sizeof(struct pisp_decompress_config) },
	{ PISP_FE_ENABLE_DECOMPAND, 0,
		offsetof(struct pisp_fe_config, decompand),
		sizeof(struct pisp_fe_decompand_config) },
	{ PISP_FE_ENABLE_BLA, 0,
		offsetof(struct pisp_fe_config, bla),
		sizeof(struct pisp_bla_config) },
	{ PISP_FE_ENABLE_DPC, 0,
		offsetof(struct pisp_fe_config, dpc),
		sizeof(struct pisp_fe_dpc_config) },
	{ PISP_FE_ENABLE_STATS_CROP, 0,
		offsetof(struct pisp_fe_config, stats_crop),
		sizeof(struct pisp_fe_crop_config) },
	{ PISP_FE_ENABLE_BLC, 0,
		offsetof(struct pisp_fe_config, blc),
		sizeof(struct pisp_bla_config) },
	{ PISP_FE_ENABLE_CDAF_STATS, 0,
		offsetof(struct pisp_fe_config, cdaf_stats),
		sizeof(struct pisp_fe_cdaf_stats_config) },
	{ PISP_FE_ENABLE_AWB_STATS, 0,
		offsetof(struct pisp_fe_config, awb_stats),
		sizeof(struct pisp_fe_awb_stats_config) },
	{ PISP_FE_ENABLE_RGBY, 0,
		offsetof(struct pisp_fe_config, rgby),
		sizeof(struct pisp_fe_rgby_config) },
	{ PISP_FE_ENABLE_LSC, 0,
		offsetof(struct pisp_fe_config, lsc),
		sizeof(struct pisp_fe_lsc_config) },
	{ PISP_FE_ENABLE_AGC_STATS, 0,
		offsetof(struct pisp_fe_config, agc_stats),
		sizeof(struct pisp_agc_statistics) },
	{ PISP_FE_ENABLE_CROP0, 0,
		offsetof(struct pisp_fe_config, ch[0].crop),
		sizeof(struct pisp_fe_crop_config) },
	{ PISP_FE_ENABLE_DOWNSCALE0, 0,
		offsetof(struct pisp_fe_config, ch[0].downscale),
		sizeof(struct pisp_fe_downscale_config) },
	{ PISP_FE_ENABLE_COMPRESS0, 0,
		offsetof(struct pisp_fe_config, ch[0].compress),
		sizeof(struct pisp_compress_config) },
	{ PISP_FE_ENABLE_OUTPUT0, 0,
		offsetof(struct pisp_fe_config, ch[0].output),
		sizeof(struct pisp_fe_output_config) },
	{ PISP_FE_ENABLE_CROP1, 0,
		offsetof(struct pisp_fe_config, ch[1].crop),
		sizeof(struct pisp_fe_crop_config) },
	{ PISP_FE_ENABLE_DOWNSCALE1, 0,
		offsetof(struct pisp_fe_config, ch[1].downscale),
		sizeof(struct pisp_fe_downscale_config) },
	{ PISP_FE_ENABLE_COMPRESS1, 0,
		offsetof(struct pisp_fe_config, ch[1].compress),
		sizeof(struct pisp_compress_config) },
	{ PISP_FE_ENABLE_OUTPUT1, 0,
		offsetof(struct pisp_fe_config, ch[1].output),
		sizeof(struct pisp_fe_output_config) },
};

#define pisp_fe_dbg(fe, fmt, arg...) dev_dbg((fe)->v4l2_dev->dev, fmt, ##arg)
#define pisp_fe_info(fe, fmt, arg...) dev_info((fe)->v4l2_dev->dev, fmt, ##arg)
#define pisp_fe_err(fe, fmt, arg...) dev_err((fe)->v4l2_dev->dev, fmt, ##arg)

static inline u32 pisp_fe_reg_read(struct pisp_fe_device *fe, u32 offset)
{
	return readl(fe->base + offset);
}

static inline void pisp_fe_reg_write(struct pisp_fe_device *fe, u32 offset,
				     u32 val)
{
	writel(val, fe->base + offset);
}

static inline void pisp_fe_reg_write_relaxed(struct pisp_fe_device *fe,
					     u32 offset, u32 val)
{
	writel_relaxed(val, fe->base + offset);
}

static int pisp_fe_regs_show(struct seq_file *s, void *data)
{
	struct pisp_fe_device *fe = s->private;
	int ret;

	ret = pm_runtime_resume_and_get(fe->v4l2_dev->dev);
	if (ret)
		return ret;

	pisp_fe_reg_write(fe, FE_CONTROL, FE_CONTROL_LATCH_REGS);

#define DUMP(reg) seq_printf(s, #reg " \t0x%08x\n", pisp_fe_reg_read(fe, reg))
	DUMP(FE_VERSION);
	DUMP(FE_CONTROL);
	DUMP(FE_STATUS);
	DUMP(FE_FRAME_STATUS);
	DUMP(FE_ERROR_STATUS);
	DUMP(FE_OUTPUT_STATUS);
	DUMP(FE_INT_EN);
	DUMP(FE_INT_STATUS);
#undef DUMP

	pm_runtime_put(fe->v4l2_dev->dev);

	return 0;
}

DEFINE_SHOW_ATTRIBUTE(pisp_fe_regs);

static void pisp_fe_config_write(struct pisp_fe_device *fe,
				 struct pisp_fe_config *config,
				 unsigned int start_offset, unsigned int size)
{
	const unsigned int max_offset =
		offsetof(struct pisp_fe_config, ch[PISP_FE_NUM_OUTPUTS]);
	unsigned int end_offset;
	u32 *cfg = (u32 *)config;

	start_offset = min(start_offset, max_offset);
	end_offset = min(start_offset + size, max_offset);

	cfg += start_offset >> 2;
	for (unsigned int i = start_offset; i < end_offset; i += 4, cfg++)
		pisp_fe_reg_write_relaxed(fe, PISP_FE_CONFIG_BASE_OFFSET + i,
					  *cfg);
}

void pisp_fe_isr(struct pisp_fe_device *fe, bool *sof, bool *eof)
{
	u32 status, int_status, out_status, frame_status, error_status;

	pisp_fe_reg_write(fe, FE_CONTROL, FE_CONTROL_LATCH_REGS);
	status = pisp_fe_reg_read(fe, FE_STATUS);
	out_status = pisp_fe_reg_read(fe, FE_OUTPUT_STATUS);
	frame_status = pisp_fe_reg_read(fe, FE_FRAME_STATUS);
	error_status = pisp_fe_reg_read(fe, FE_ERROR_STATUS);

	int_status = pisp_fe_reg_read(fe, FE_INT_STATUS);
	pisp_fe_reg_write(fe, FE_INT_STATUS, int_status);

	trace_fe_irq(status, out_status, frame_status, error_status,
		     int_status);

	/* We do not report interrupts for the input/stream pad. */
	for (unsigned int i = 0; i < FE_NUM_PADS - 1; i++) {
		sof[i] = !!(int_status & FE_INT_SOF);
		eof[i] = !!(int_status & FE_INT_EOF);
	}
}

static bool pisp_fe_validate_output(struct pisp_fe_config const *cfg,
				    unsigned int c, struct v4l2_format const *f)
{
	unsigned int wbytes;

	wbytes = cfg->ch[c].output.format.width;
	if (cfg->ch[c].output.format.format & PISP_IMAGE_FORMAT_BPS_MASK)
		wbytes *= 2;

	/* Check output image dimensions are nonzero and not too big */
	if (cfg->ch[c].output.format.width < 2 ||
	    cfg->ch[c].output.format.height < 2 ||
	    cfg->ch[c].output.format.height > f->fmt.pix.height ||
	    cfg->ch[c].output.format.stride > f->fmt.pix.bytesperline ||
	    wbytes > f->fmt.pix.bytesperline)
		return false;

	/* Check for zero-sized crops, which could cause lockup */
	if ((cfg->global.enables & PISP_FE_ENABLE_CROP(c)) &&
	    ((cfg->ch[c].crop.offset_x >= (cfg->input.format.width & ~1) ||
	      cfg->ch[c].crop.offset_y >= cfg->input.format.height ||
	      cfg->ch[c].crop.width < 2 || cfg->ch[c].crop.height < 2)))
		return false;

	if ((cfg->global.enables & PISP_FE_ENABLE_DOWNSCALE(c)) &&
	    (cfg->ch[c].downscale.output_width < 2 ||
	     cfg->ch[c].downscale.output_height < 2))
		return false;

	return true;
}

static bool pisp_fe_validate_stats(struct pisp_fe_config const *cfg)
{
	/* Check for zero-sized crop, which could cause lockup */
	return (!(cfg->global.enables & PISP_FE_ENABLE_STATS_CROP) ||
		(cfg->stats_crop.offset_x < (cfg->input.format.width & ~1) &&
		 cfg->stats_crop.offset_y < cfg->input.format.height &&
		 cfg->stats_crop.width >= 2 && cfg->stats_crop.height >= 2));
}

int pisp_fe_validate_config(struct pisp_fe_device *fe,
			    struct pisp_fe_config *cfg,
			    struct v4l2_format const *f0,
			    struct v4l2_format const *f1)
{
	/*
	 * Check the input is enabled, streaming and has nonzero size;
	 * to avoid cases where the hardware might lock up or try to
	 * read inputs from memory (which this driver doesn't support).
	 */
	if (!(cfg->global.enables & PISP_FE_ENABLE_INPUT) ||
	    cfg->input.streaming != 1 || cfg->input.format.width < 2 ||
	    cfg->input.format.height < 2) {
		pisp_fe_err(fe, "%s: Input config not valid", __func__);
		return -EINVAL;
	}

	for (unsigned int i = 0; i < PISP_FE_NUM_OUTPUTS; i++) {
		if (!(cfg->global.enables & PISP_FE_ENABLE_OUTPUT(i))) {
			if (cfg->global.enables &
					PISP_FE_ENABLE_OUTPUT_CLUSTER(i)) {
				pisp_fe_err(fe, "%s: Output %u not valid",
					    __func__, i);
				return -EINVAL;
			}
			continue;
		}

		if (!pisp_fe_validate_output(cfg, i, i ? f1 : f0))
			return -EINVAL;
	}

	if ((cfg->global.enables & PISP_FE_ENABLE_STATS_CLUSTER) &&
	    !pisp_fe_validate_stats(cfg)) {
		pisp_fe_err(fe, "%s: Stats config not valid", __func__);
		return -EINVAL;
	}

	return 0;
}

void pisp_fe_submit_job(struct pisp_fe_device *fe, struct vb2_buffer **vb2_bufs,
			struct pisp_fe_config *cfg)
{
	u64 addr;
	u32 status;

	/*
	 * Check output buffers exist and outputs are correctly configured.
	 * If valid, set the buffer's DMA address; otherwise disable.
	 */
	for (unsigned int i = 0; i < PISP_FE_NUM_OUTPUTS; i++) {
		struct vb2_buffer *buf = vb2_bufs[FE_OUTPUT0_PAD + i];

		if (!(cfg->global.enables & PISP_FE_ENABLE_OUTPUT(i)))
			continue;

		addr = vb2_dma_contig_plane_dma_addr(buf, 0);
		cfg->output_buffer[i].addr_lo = addr & 0xffffffff;
		cfg->output_buffer[i].addr_hi = addr >> 32;
	}

	if (vb2_bufs[FE_STATS_PAD]) {
		addr = vb2_dma_contig_plane_dma_addr(vb2_bufs[FE_STATS_PAD], 0);
		cfg->stats_buffer.addr_lo = addr & 0xffffffff;
		cfg->stats_buffer.addr_hi = addr >> 32;
	}

	/* Set up ILINES interrupts 3/4 of the way down each output */
	cfg->ch[0].output.ilines =
		max(0x80u, (3u * cfg->ch[0].output.format.height) >> 2);
	cfg->ch[1].output.ilines =
		max(0x80u, (3u * cfg->ch[1].output.format.height) >> 2);

	/*
	 * The hardware must have consumed the previous config by now.
	 * This read of status also serves as a memory barrier before the
	 * sequence of relaxed writes which follow.
	 */
	status = pisp_fe_reg_read(fe, FE_STATUS);
	if (WARN_ON(status & FE_STATUS_QUEUED))
		return;

	/*
	 * Unconditionally write buffers, global and input parameters.
	 * Write cropping and output parameters whenever they are enabled.
	 * Selectively write other parameters that have been marked as
	 * changed through the dirty flags.
	 */
	pisp_fe_config_write(fe, cfg, 0,
			     offsetof(struct pisp_fe_config, decompress));
	cfg->dirty_flags_extra &= ~PISP_FE_DIRTY_GLOBAL;
	cfg->dirty_flags &= ~PISP_FE_ENABLE_INPUT;
	cfg->dirty_flags |= (cfg->global.enables &
			     (PISP_FE_ENABLE_STATS_CROP        |
			      PISP_FE_ENABLE_OUTPUT_CLUSTER(0) |
			      PISP_FE_ENABLE_OUTPUT_CLUSTER(1)));
	for (unsigned int i = 0; i < ARRAY_SIZE(pisp_fe_config_map); i++) {
		const struct pisp_fe_config_param *p = &pisp_fe_config_map[i];

		if (cfg->dirty_flags & p->dirty_flags ||
		    cfg->dirty_flags_extra & p->dirty_flags_extra)
			pisp_fe_config_write(fe, cfg, p->offset, p->size);
	}

	/* This final non-relaxed write serves as a memory barrier */
	pisp_fe_reg_write(fe, FE_CONTROL, FE_CONTROL_QUEUE);
}

void pisp_fe_start(struct pisp_fe_device *fe)
{
	pisp_fe_reg_write(fe, FE_CONTROL, FE_CONTROL_RESET);
	pisp_fe_reg_write(fe, FE_INT_STATUS, ~0);
	pisp_fe_reg_write(fe, FE_INT_EN, FE_INT_EOF | FE_INT_SOF |
					 FE_INT_LINES0 | FE_INT_LINES1);
	fe->inframe_count = 0;
}

void pisp_fe_stop(struct pisp_fe_device *fe)
{
	pisp_fe_reg_write(fe, FE_INT_EN, 0);
	pisp_fe_reg_write(fe, FE_CONTROL, FE_CONTROL_ABORT);
	usleep_range(1000, 2000);
	WARN_ON(pisp_fe_reg_read(fe, FE_STATUS));
	pisp_fe_reg_write(fe, FE_INT_STATUS, ~0);
}

static int pisp_fe_init_state(struct v4l2_subdev *sd,
			      struct v4l2_subdev_state *state)
{
	struct v4l2_mbus_framefmt *fmt;

	fmt = v4l2_subdev_state_get_format(state, FE_STREAM_PAD);
	*fmt = cfe_default_format;
	fmt->code = MEDIA_BUS_FMT_SRGGB16_1X16;

	fmt = v4l2_subdev_state_get_format(state, FE_CONFIG_PAD);
	fmt->code = MEDIA_BUS_FMT_FIXED;
	fmt->width = sizeof(struct pisp_fe_config);
	fmt->height = 1;

	fmt = v4l2_subdev_state_get_format(state, FE_OUTPUT0_PAD);
	*fmt = cfe_default_format;
	fmt->code = MEDIA_BUS_FMT_SRGGB16_1X16;

	fmt = v4l2_subdev_state_get_format(state, FE_OUTPUT1_PAD);
	*fmt = cfe_default_format;
	fmt->code = MEDIA_BUS_FMT_SRGGB16_1X16;

	fmt = v4l2_subdev_state_get_format(state, FE_STATS_PAD);
	fmt->code = MEDIA_BUS_FMT_FIXED;
	fmt->width = sizeof(struct pisp_statistics);
	fmt->height = 1;

	return 0;
}

static int pisp_fe_pad_set_fmt(struct v4l2_subdev *sd,
			       struct v4l2_subdev_state *state,
			       struct v4l2_subdev_format *format)
{
	struct v4l2_mbus_framefmt *fmt;
	const struct cfe_fmt *cfe_fmt;

	/* TODO: format propagation to source pads */
	/* TODO: format validation */

	switch (format->pad) {
	case FE_STREAM_PAD:
		cfe_fmt = find_format_by_code(format->format.code);
		if (!cfe_fmt || !(cfe_fmt->flags & CFE_FORMAT_FLAG_FE_OUT))
			cfe_fmt = find_format_by_code(MEDIA_BUS_FMT_SRGGB16_1X16);

		format->format.code = cfe_fmt->code;
		format->format.field = V4L2_FIELD_NONE;

		fmt = v4l2_subdev_state_get_format(state, FE_STREAM_PAD);
		*fmt = format->format;

		fmt = v4l2_subdev_state_get_format(state, FE_OUTPUT0_PAD);
		*fmt = format->format;

		fmt = v4l2_subdev_state_get_format(state, FE_OUTPUT1_PAD);
		*fmt = format->format;

		return 0;

	case FE_OUTPUT0_PAD:
	case FE_OUTPUT1_PAD: {
		/*
		 * TODO: we should allow scaling and cropping by allowing the
		 * user to set the size here.
		 */
		struct v4l2_mbus_framefmt *sink_fmt, *source_fmt;
		u32 sink_code;
		u32 code;

		cfe_fmt = find_format_by_code(format->format.code);
		if (!cfe_fmt || !(cfe_fmt->flags & CFE_FORMAT_FLAG_FE_OUT))
			cfe_fmt = find_format_by_code(MEDIA_BUS_FMT_SRGGB16_1X16);

		format->format.code = cfe_fmt->code;

		sink_fmt = v4l2_subdev_state_get_format(state, FE_STREAM_PAD);
		if (!sink_fmt)
			return -EINVAL;

		source_fmt = v4l2_subdev_state_get_format(state, format->pad);
		if (!source_fmt)
			return -EINVAL;

		sink_code = sink_fmt->code;
		code = format->format.code;

		/*
		 * If the source code from the user does not match the code in
		 * the sink pad, check that the source code matches the
		 * compressed version of the sink code.
		 */

		if (code != sink_code &&
		    code == cfe_find_compressed_code(sink_code))
			source_fmt->code = code;

		return 0;
	}

	case FE_CONFIG_PAD:
	case FE_STATS_PAD:
	default:
		return v4l2_subdev_get_fmt(sd, state, format);
	}
}

static const struct v4l2_subdev_pad_ops pisp_fe_subdev_pad_ops = {
	.get_fmt = v4l2_subdev_get_fmt,
	.set_fmt = pisp_fe_pad_set_fmt,
	.link_validate = v4l2_subdev_link_validate_default,
};

static int pisp_fe_link_validate(struct media_link *link)
{
	struct v4l2_subdev *sd = media_entity_to_v4l2_subdev(link->sink->entity);
	struct pisp_fe_device *fe = container_of(sd, struct pisp_fe_device, sd);

	pisp_fe_dbg(fe, "%s: link \"%s\":%u -> \"%s\":%u\n", __func__,
		    link->source->entity->name, link->source->index,
		    link->sink->entity->name, link->sink->index);

	if (link->sink->index == FE_STREAM_PAD)
		return v4l2_subdev_link_validate(link);

	if (link->sink->index == FE_CONFIG_PAD)
		return 0;

	return -EINVAL;
}

static const struct media_entity_operations pisp_fe_entity_ops = {
	.link_validate = pisp_fe_link_validate,
};

static const struct v4l2_subdev_ops pisp_fe_subdev_ops = {
	.pad = &pisp_fe_subdev_pad_ops,
};

static const struct v4l2_subdev_internal_ops pisp_fe_internal_ops = {
	.init_state = pisp_fe_init_state,
};

int pisp_fe_init(struct pisp_fe_device *fe, struct dentry *debugfs)
{
	int ret;

	debugfs_create_file("fe_regs", 0440, debugfs, fe, &pisp_fe_regs_fops);

	fe->hw_revision = pisp_fe_reg_read(fe, FE_VERSION);
	pisp_fe_info(fe, "PiSP FE HW v%u.%u\n",
		     (fe->hw_revision >> 24) & 0xff,
		     (fe->hw_revision >> 20) & 0x0f);

	fe->pad[FE_STREAM_PAD].flags =
		MEDIA_PAD_FL_SINK | MEDIA_PAD_FL_MUST_CONNECT;
	fe->pad[FE_CONFIG_PAD].flags = MEDIA_PAD_FL_SINK;
	fe->pad[FE_OUTPUT0_PAD].flags = MEDIA_PAD_FL_SOURCE;
	fe->pad[FE_OUTPUT1_PAD].flags = MEDIA_PAD_FL_SOURCE;
	fe->pad[FE_STATS_PAD].flags = MEDIA_PAD_FL_SOURCE;

	ret = media_entity_pads_init(&fe->sd.entity, ARRAY_SIZE(fe->pad),
				     fe->pad);
	if (ret)
		return ret;

	/* Initialize subdev */
	v4l2_subdev_init(&fe->sd, &pisp_fe_subdev_ops);
	fe->sd.internal_ops = &pisp_fe_internal_ops;
	fe->sd.entity.function = MEDIA_ENT_F_PROC_VIDEO_SCALER;
	fe->sd.entity.ops = &pisp_fe_entity_ops;
	fe->sd.entity.name = "pisp-fe";
	fe->sd.flags = V4L2_SUBDEV_FL_HAS_DEVNODE;
	fe->sd.owner = THIS_MODULE;
	snprintf(fe->sd.name, sizeof(fe->sd.name), "pisp-fe");

	ret = v4l2_subdev_init_finalize(&fe->sd);
	if (ret)
		goto err_entity_cleanup;

	ret = v4l2_device_register_subdev(fe->v4l2_dev, &fe->sd);
	if (ret) {
		pisp_fe_err(fe, "Failed register pisp fe subdev (%d)\n", ret);
		goto err_subdev_cleanup;
	}

	/* Must be in IDLE state (STATUS == 0) here. */
	WARN_ON(pisp_fe_reg_read(fe, FE_STATUS));

	return 0;

err_subdev_cleanup:
	v4l2_subdev_cleanup(&fe->sd);
err_entity_cleanup:
	media_entity_cleanup(&fe->sd.entity);

	return ret;
}

void pisp_fe_uninit(struct pisp_fe_device *fe)
{
	v4l2_device_unregister_subdev(&fe->sd);
	v4l2_subdev_cleanup(&fe->sd);
	media_entity_cleanup(&fe->sd.entity);
}
