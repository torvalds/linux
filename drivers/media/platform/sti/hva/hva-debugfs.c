// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) STMicroelectronics SA 2015
 * Authors: Yannick Fertre <yannick.fertre@st.com>
 *          Hugues Fruchet <hugues.fruchet@st.com>
 */

#include <linux/debugfs.h>

#include "hva.h"
#include "hva-hw.h"

static void format_ctx(struct seq_file *s, struct hva_ctx *ctx)
{
	struct hva_streaminfo *stream = &ctx->streaminfo;
	struct hva_frameinfo *frame = &ctx->frameinfo;
	struct hva_controls *ctrls = &ctx->ctrls;
	struct hva_ctx_dbg *dbg = &ctx->dbg;
	u32 bitrate_mode, aspect, entropy, vui_sar, sei_fp;

	seq_printf(s, "|-%s\n  |\n", ctx->name);

	seq_printf(s, "  |-[%sframe info]\n",
		   ctx->flags & HVA_FLAG_FRAMEINFO ? "" : "default ");
	seq_printf(s, "  | |- pixel format=%4.4s\n"
		      "  | |- wxh=%dx%d\n"
		      "  | |- wxh (w/ encoder alignment constraint)=%dx%d\n"
		      "  |\n",
		      (char *)&frame->pixelformat,
		      frame->width, frame->height,
		      frame->aligned_width, frame->aligned_height);

	seq_printf(s, "  |-[%sstream info]\n",
		   ctx->flags & HVA_FLAG_STREAMINFO ? "" : "default ");
	seq_printf(s, "  | |- stream format=%4.4s\n"
		      "  | |- wxh=%dx%d\n"
		      "  | |- %s\n"
		      "  | |- %s\n"
		      "  |\n",
		      (char *)&stream->streamformat,
		      stream->width, stream->height,
		      stream->profile, stream->level);

	bitrate_mode = V4L2_CID_MPEG_VIDEO_BITRATE_MODE;
	aspect = V4L2_CID_MPEG_VIDEO_ASPECT;
	seq_puts(s, "  |-[parameters]\n");
	seq_printf(s, "  | |- %s\n"
		      "  | |- bitrate=%d bps\n"
		      "  | |- GOP size=%d\n"
		      "  | |- video aspect=%s\n"
		      "  | |- framerate=%d/%d\n",
		      v4l2_ctrl_get_menu(bitrate_mode)[ctrls->bitrate_mode],
		      ctrls->bitrate,
		      ctrls->gop_size,
		      v4l2_ctrl_get_menu(aspect)[ctrls->aspect],
		      ctrls->time_per_frame.denominator,
		      ctrls->time_per_frame.numerator);

	entropy = V4L2_CID_MPEG_VIDEO_H264_ENTROPY_MODE;
	vui_sar = V4L2_CID_MPEG_VIDEO_H264_VUI_SAR_IDC;
	sei_fp =  V4L2_CID_MPEG_VIDEO_H264_SEI_FP_ARRANGEMENT_TYPE;
	if (stream->streamformat == V4L2_PIX_FMT_H264) {
		seq_printf(s, "  | |- %s entropy mode\n"
			      "  | |- CPB size=%d kB\n"
			      "  | |- DCT8x8 enable=%s\n"
			      "  | |- qpmin=%d\n"
			      "  | |- qpmax=%d\n"
			      "  | |- PAR enable=%s\n"
			      "  | |- PAR id=%s\n"
			      "  | |- SEI frame packing enable=%s\n"
			      "  | |- SEI frame packing type=%s\n",
			      v4l2_ctrl_get_menu(entropy)[ctrls->entropy_mode],
			      ctrls->cpb_size,
			      ctrls->dct8x8 ? "true" : "false",
			      ctrls->qpmin,
			      ctrls->qpmax,
			      ctrls->vui_sar ? "true" : "false",
			      v4l2_ctrl_get_menu(vui_sar)[ctrls->vui_sar_idc],
			      ctrls->sei_fp ? "true" : "false",
			      v4l2_ctrl_get_menu(sei_fp)[ctrls->sei_fp_type]);
	}

	if (ctx->sys_errors || ctx->encode_errors || ctx->frame_errors) {
		seq_puts(s, "  |\n  |-[errors]\n");
		seq_printf(s, "  | |- system=%d\n"
			      "  | |- encoding=%d\n"
			      "  | |- frame=%d\n",
			      ctx->sys_errors,
			      ctx->encode_errors,
			      ctx->frame_errors);
	}

	seq_puts(s, "  |\n  |-[performances]\n");
	seq_printf(s, "  | |- frames encoded=%d\n"
		      "  | |- avg HW processing duration (0.1ms)=%d [min=%d, max=%d]\n"
		      "  | |- avg encoding period (0.1ms)=%d [min=%d, max=%d]\n"
		      "  | |- avg fps (0.1Hz)=%d\n"
		      "  | |- max reachable fps (0.1Hz)=%d\n"
		      "  | |- avg bitrate (kbps)=%d [min=%d, max=%d]\n"
		      "  | |- last bitrate (kbps)=%d\n",
		      dbg->cnt_duration,
		      dbg->avg_duration,
		      dbg->min_duration,
		      dbg->max_duration,
		      dbg->avg_period,
		      dbg->min_period,
		      dbg->max_period,
		      dbg->avg_fps,
		      dbg->max_fps,
		      dbg->avg_bitrate,
		      dbg->min_bitrate,
		      dbg->max_bitrate,
		      dbg->last_bitrate);
}

/*
 * performance debug info
 */
void hva_dbg_perf_begin(struct hva_ctx *ctx)
{
	u64 div;
	u32 period;
	u32 bitrate;
	struct hva_ctx_dbg *dbg = &ctx->dbg;
	ktime_t prev = dbg->begin;

	dbg->begin = ktime_get();

	if (dbg->is_valid_period) {
		/* encoding period */
		div = (u64)ktime_us_delta(dbg->begin, prev);
		do_div(div, 100);
		period = (u32)div;
		dbg->min_period = min(period, dbg->min_period);
		dbg->max_period = max(period, dbg->max_period);
		dbg->total_period += period;
		dbg->cnt_period++;

		/*
		 * minimum and maximum bitrates are based on the
		 * encoding period values upon a window of 32 samples
		 */
		dbg->window_duration += period;
		dbg->cnt_window++;
		if (dbg->cnt_window >= 32) {
			/*
			 * bitrate in kbps = (size * 8 / 1000) /
			 *                   (duration / 10000)
			 *                 = size * 80 / duration
			 */
			if (dbg->window_duration > 0) {
				div = (u64)dbg->window_stream_size * 80;
				do_div(div, dbg->window_duration);
				bitrate = (u32)div;
				dbg->last_bitrate = bitrate;
				dbg->min_bitrate = min(bitrate,
						       dbg->min_bitrate);
				dbg->max_bitrate = max(bitrate,
						       dbg->max_bitrate);
			}
			dbg->window_stream_size = 0;
			dbg->window_duration = 0;
			dbg->cnt_window = 0;
		}
	}

	/*
	 * filter sequences valid for performance:
	 * - begin/begin (no stream available) is an invalid sequence
	 * - begin/end is a valid sequence
	 */
	dbg->is_valid_period = false;
}

void hva_dbg_perf_end(struct hva_ctx *ctx, struct hva_stream *stream)
{
	struct device *dev = ctx_to_dev(ctx);
	u64 div;
	u32 duration;
	u32 bytesused;
	u32 timestamp;
	struct hva_ctx_dbg *dbg = &ctx->dbg;
	ktime_t end = ktime_get();

	/* stream bytesused and timestamp in us */
	bytesused = vb2_get_plane_payload(&stream->vbuf.vb2_buf, 0);
	div = stream->vbuf.vb2_buf.timestamp;
	do_div(div, 1000);
	timestamp = (u32)div;

	/* encoding duration */
	div = (u64)ktime_us_delta(end, dbg->begin);

	dev_dbg(dev,
		"%s perf stream[%d] dts=%d encoded using %d bytes in %d us",
		ctx->name,
		stream->vbuf.sequence,
		timestamp,
		bytesused, (u32)div);

	do_div(div, 100);
	duration = (u32)div;

	dbg->min_duration = min(duration, dbg->min_duration);
	dbg->max_duration = max(duration, dbg->max_duration);
	dbg->total_duration += duration;
	dbg->cnt_duration++;

	/*
	 * the average bitrate is based on the total stream size
	 * and the total encoding periods
	 */
	dbg->total_stream_size += bytesused;
	dbg->window_stream_size += bytesused;

	dbg->is_valid_period = true;
}

static void hva_dbg_perf_compute(struct hva_ctx *ctx)
{
	u64 div;
	struct hva_ctx_dbg *dbg = &ctx->dbg;

	if (dbg->cnt_duration > 0) {
		div = (u64)dbg->total_duration;
		do_div(div, dbg->cnt_duration);
		dbg->avg_duration = (u32)div;
	} else {
		dbg->avg_duration = 0;
	}

	if (dbg->total_duration > 0) {
		div = (u64)dbg->cnt_duration * 100000;
		do_div(div, dbg->total_duration);
		dbg->max_fps = (u32)div;
	} else {
		dbg->max_fps = 0;
	}

	if (dbg->cnt_period > 0) {
		div = (u64)dbg->total_period;
		do_div(div, dbg->cnt_period);
		dbg->avg_period = (u32)div;
	} else {
		dbg->avg_period = 0;
	}

	if (dbg->total_period > 0) {
		div = (u64)dbg->cnt_period * 100000;
		do_div(div, dbg->total_period);
		dbg->avg_fps = (u32)div;
	} else {
		dbg->avg_fps = 0;
	}

	if (dbg->total_period > 0) {
		/*
		 * bitrate in kbps = (video size * 8 / 1000) /
		 *                   (video duration / 10000)
		 *                 = video size * 80 / video duration
		 */
		div = (u64)dbg->total_stream_size * 80;
		do_div(div, dbg->total_period);
		dbg->avg_bitrate = (u32)div;
	} else {
		dbg->avg_bitrate = 0;
	}
}

/*
 * device debug info
 */

static int device_show(struct seq_file *s, void *data)
{
	struct hva_dev *hva = s->private;

	seq_printf(s, "[%s]\n", hva->v4l2_dev.name);
	seq_printf(s, "registered as /dev/video%d\n", hva->vdev->num);

	return 0;
}

static int encoders_show(struct seq_file *s, void *data)
{
	struct hva_dev *hva = s->private;
	unsigned int i = 0;

	seq_printf(s, "[encoders]\n|- %d registered encoders:\n",
		   hva->nb_of_encoders);

	while (hva->encoders[i]) {
		seq_printf(s, "|- %s: %4.4s => %4.4s\n", hva->encoders[i]->name,
			   (char *)&hva->encoders[i]->pixelformat,
			   (char *)&hva->encoders[i]->streamformat);
		i++;
	}

	return 0;
}

static int last_show(struct seq_file *s, void *data)
{
	struct hva_dev *hva = s->private;
	struct hva_ctx *last_ctx = &hva->dbg.last_ctx;

	if (last_ctx->flags & HVA_FLAG_STREAMINFO) {
		seq_puts(s, "[last encoding]\n");

		hva_dbg_perf_compute(last_ctx);
		format_ctx(s, last_ctx);
	} else {
		seq_puts(s, "[no information recorded about last encoding]\n");
	}

	return 0;
}

static int regs_show(struct seq_file *s, void *data)
{
	struct hva_dev *hva = s->private;

	hva_hw_dump_regs(hva, s);

	return 0;
}

#define hva_dbg_create_entry(name)					 \
	debugfs_create_file(#name, 0444, hva->dbg.debugfs_entry, hva, \
			    &name##_fops)

DEFINE_SHOW_ATTRIBUTE(device);
DEFINE_SHOW_ATTRIBUTE(encoders);
DEFINE_SHOW_ATTRIBUTE(last);
DEFINE_SHOW_ATTRIBUTE(regs);

void hva_debugfs_create(struct hva_dev *hva)
{
	hva->dbg.debugfs_entry = debugfs_create_dir(HVA_NAME, NULL);
	if (!hva->dbg.debugfs_entry)
		goto err;

	if (!hva_dbg_create_entry(device))
		goto err;

	if (!hva_dbg_create_entry(encoders))
		goto err;

	if (!hva_dbg_create_entry(last))
		goto err;

	if (!hva_dbg_create_entry(regs))
		goto err;

	return;

err:
	hva_debugfs_remove(hva);
}

void hva_debugfs_remove(struct hva_dev *hva)
{
	debugfs_remove_recursive(hva->dbg.debugfs_entry);
	hva->dbg.debugfs_entry = NULL;
}

/*
 * context (instance) debug info
 */

static int ctx_show(struct seq_file *s, void *data)
{
	struct hva_ctx *ctx = s->private;

	seq_printf(s, "[running encoding %d]\n", ctx->id);

	hva_dbg_perf_compute(ctx);
	format_ctx(s, ctx);

	return 0;
}

DEFINE_SHOW_ATTRIBUTE(ctx);

void hva_dbg_ctx_create(struct hva_ctx *ctx)
{
	struct hva_dev *hva = ctx->hva_dev;
	char name[4] = "";

	ctx->dbg.min_duration = UINT_MAX;
	ctx->dbg.min_period = UINT_MAX;
	ctx->dbg.min_bitrate = UINT_MAX;

	snprintf(name, sizeof(name), "%d", hva->instance_id);

	ctx->dbg.debugfs_entry = debugfs_create_file(name, 0444,
						     hva->dbg.debugfs_entry,
						     ctx, &ctx_fops);
}

void hva_dbg_ctx_remove(struct hva_ctx *ctx)
{
	struct hva_dev *hva = ctx->hva_dev;

	if (ctx->flags & HVA_FLAG_STREAMINFO)
		/* save context before removing */
		memcpy(&hva->dbg.last_ctx, ctx, sizeof(*ctx));

	debugfs_remove(ctx->dbg.debugfs_entry);
}
