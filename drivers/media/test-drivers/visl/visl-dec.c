// SPDX-License-Identifier: GPL-2.0
/*
 * Contains the virtual decoder logic. The functions here control the
 * tracing/TPG on a per-frame basis
 */

#include "visl.h"
#include "visl-debugfs.h"
#include "visl-dec.h"
#include "visl-trace-fwht.h"
#include "visl-trace-mpeg2.h"
#include "visl-trace-vp8.h"
#include "visl-trace-vp9.h"
#include "visl-trace-h264.h"
#include "visl-trace-hevc.h"
#include "visl-trace-av1.h"

#include <linux/delay.h>
#include <linux/workqueue.h>
#include <media/v4l2-mem2mem.h>
#include <media/tpg/v4l2-tpg.h>

#define LAST_BUF_IDX (V4L2_AV1_REF_LAST_FRAME - V4L2_AV1_REF_LAST_FRAME)
#define LAST2_BUF_IDX (V4L2_AV1_REF_LAST2_FRAME - V4L2_AV1_REF_LAST_FRAME)
#define LAST3_BUF_IDX (V4L2_AV1_REF_LAST3_FRAME - V4L2_AV1_REF_LAST_FRAME)
#define GOLDEN_BUF_IDX (V4L2_AV1_REF_GOLDEN_FRAME - V4L2_AV1_REF_LAST_FRAME)
#define BWD_BUF_IDX (V4L2_AV1_REF_BWDREF_FRAME - V4L2_AV1_REF_LAST_FRAME)
#define ALT2_BUF_IDX (V4L2_AV1_REF_ALTREF2_FRAME - V4L2_AV1_REF_LAST_FRAME)
#define ALT_BUF_IDX (V4L2_AV1_REF_ALTREF_FRAME - V4L2_AV1_REF_LAST_FRAME)

static void *plane_vaddr(struct tpg_data *tpg, struct vb2_buffer *buf,
			 u32 p, u32 bpl[TPG_MAX_PLANES], u32 h)
{
	u32 i;
	void *vbuf;

	if (p == 0 || tpg_g_buffers(tpg) > 1)
		return vb2_plane_vaddr(buf, p);
	vbuf = vb2_plane_vaddr(buf, 0);
	for (i = 0; i < p; i++)
		vbuf += bpl[i] * h / tpg->vdownsampling[i];
	return vbuf;
}

static void visl_print_ts_idx(u8 **buf, __kernel_size_t *buflen, const char *name,
			      u64 ts, struct vb2_buffer *vb2_buf)
{
	u32 len;

	if (tpg_verbose && vb2_buf) {
		len = scnprintf(*buf, *buflen, "%s: %lld, vb2_idx: %d\n", name,
				ts, vb2_buf->index);
	} else {
		len = scnprintf(*buf, *buflen, "%s: %lld\n", name, ts);
	}

	*buf += len;
	*buflen -= len;
}

static void visl_get_ref_frames(struct visl_ctx *ctx, u8 *buf,
				__kernel_size_t buflen, struct visl_run *run)
{
	struct vb2_queue *cap_q = &ctx->fh.m2m_ctx->cap_q_ctx.q;
	char header[] = "Reference frames:\n";
	u32 i;
	u32 len;

	len = scnprintf(buf, buflen, header);
	buf += len;
	buflen -= len;

	switch (ctx->current_codec) {
	case VISL_CODEC_NONE:
		break;

	case VISL_CODEC_FWHT: {
		struct vb2_buffer *vb2_buf;

		vb2_buf = vb2_find_buffer(cap_q, run->fwht.params->backward_ref_ts);

		visl_print_ts_idx(&buf, &buflen, "backwards_ref_ts",
				  run->fwht.params->backward_ref_ts, vb2_buf);

		break;
	}

	case VISL_CODEC_MPEG2: {
		struct vb2_buffer *b_ref;
		struct vb2_buffer *f_ref;

		b_ref = vb2_find_buffer(cap_q, run->mpeg2.pic->backward_ref_ts);
		f_ref = vb2_find_buffer(cap_q, run->mpeg2.pic->forward_ref_ts);

		visl_print_ts_idx(&buf, &buflen, "backward_ref_ts",
				  run->mpeg2.pic->backward_ref_ts, b_ref);
		visl_print_ts_idx(&buf, &buflen, "forward_ref_ts",
				  run->mpeg2.pic->forward_ref_ts, f_ref);

		break;
	}

	case VISL_CODEC_VP8: {
		struct vb2_buffer *last;
		struct vb2_buffer *golden;
		struct vb2_buffer *alt;

		last = vb2_find_buffer(cap_q, run->vp8.frame->last_frame_ts);
		golden = vb2_find_buffer(cap_q, run->vp8.frame->golden_frame_ts);
		alt = vb2_find_buffer(cap_q, run->vp8.frame->alt_frame_ts);

		visl_print_ts_idx(&buf, &buflen, "last_ref_ts",
				  run->vp8.frame->last_frame_ts, last);
		visl_print_ts_idx(&buf, &buflen, "golden_ref_ts",
				  run->vp8.frame->golden_frame_ts, golden);
		visl_print_ts_idx(&buf, &buflen, "alt_ref_ts",
				  run->vp8.frame->alt_frame_ts, alt);

		break;
	}

	case VISL_CODEC_VP9: {
		struct vb2_buffer *last;
		struct vb2_buffer *golden;
		struct vb2_buffer *alt;

		last = vb2_find_buffer(cap_q, run->vp9.frame->last_frame_ts);
		golden = vb2_find_buffer(cap_q, run->vp9.frame->golden_frame_ts);
		alt = vb2_find_buffer(cap_q, run->vp9.frame->alt_frame_ts);

		visl_print_ts_idx(&buf, &buflen, "last_ref_ts",
				  run->vp9.frame->last_frame_ts, last);
		visl_print_ts_idx(&buf, &buflen, "golden_ref_ts",
				  run->vp9.frame->golden_frame_ts, golden);
		visl_print_ts_idx(&buf, &buflen, "alt_ref_ts",
				  run->vp9.frame->alt_frame_ts, alt);

		break;
	}

	case VISL_CODEC_H264: {
		char entry[] = "dpb[%d]:%u, vb2_index: %d\n";
		char entry_stable[] = "dpb[%d]:%u\n";
		struct vb2_buffer *vb2_buf;

		for (i = 0; i < ARRAY_SIZE(run->h264.dpram->dpb); i++) {
			vb2_buf = vb2_find_buffer(cap_q,
						  run->h264.dpram->dpb[i].reference_ts);
			if (tpg_verbose && vb2_buf) {
				len = scnprintf(buf, buflen, entry, i,
						run->h264.dpram->dpb[i].reference_ts,
						vb2_buf->index);
			} else {
				len = scnprintf(buf, buflen, entry_stable, i,
						run->h264.dpram->dpb[i].reference_ts);
			}
			buf += len;
			buflen -= len;
		}

		break;
	}

	case VISL_CODEC_HEVC: {
		char entry[] = "dpb[%d]:%u, vb2_index: %d\n";
		char entry_stable[] = "dpb[%d]:%u\n";
		struct vb2_buffer *vb2_buf;

		for (i = 0; i < ARRAY_SIZE(run->hevc.dpram->dpb); i++) {
			vb2_buf = vb2_find_buffer(cap_q, run->hevc.dpram->dpb[i].timestamp);
			if (tpg_verbose && vb2_buf) {
				len = scnprintf(buf, buflen, entry, i,
						run->hevc.dpram->dpb[i].timestamp,
						vb2_buf->index);
			} else {
				len = scnprintf(buf, buflen, entry_stable, i,
						run->hevc.dpram->dpb[i].timestamp);
			}

			buf += len;
			buflen -= len;
		}

		break;
	}

	case VISL_CODEC_AV1: {
		int idx_last = run->av1.frame->ref_frame_idx[LAST_BUF_IDX];
		int idx_last2 = run->av1.frame->ref_frame_idx[LAST2_BUF_IDX];
		int idx_last3 = run->av1.frame->ref_frame_idx[LAST3_BUF_IDX];
		int idx_golden = run->av1.frame->ref_frame_idx[GOLDEN_BUF_IDX];
		int idx_bwd = run->av1.frame->ref_frame_idx[BWD_BUF_IDX];
		int idx_alt2 = run->av1.frame->ref_frame_idx[ALT2_BUF_IDX];
		int idx_alt = run->av1.frame->ref_frame_idx[ALT_BUF_IDX];

		const u64 *reference_frame_ts = run->av1.frame->reference_frame_ts;

		struct vb2_buffer *ref_last =
			vb2_find_buffer(cap_q, reference_frame_ts[idx_last]);
		struct vb2_buffer *ref_last2 =
			vb2_find_buffer(cap_q, reference_frame_ts[idx_last2]);
		struct vb2_buffer *ref_last3 =
			vb2_find_buffer(cap_q, reference_frame_ts[idx_last3]);
		struct vb2_buffer *ref_golden =
			vb2_find_buffer(cap_q, reference_frame_ts[idx_golden]);
		struct vb2_buffer *ref_bwd =
			vb2_find_buffer(cap_q, reference_frame_ts[idx_bwd]);
		struct vb2_buffer *ref_alt2 =
			vb2_find_buffer(cap_q, reference_frame_ts[idx_alt2]);
		struct vb2_buffer *ref_alt =
			vb2_find_buffer(cap_q, reference_frame_ts[idx_alt]);

		visl_print_ts_idx(&buf, &buflen, "ref_last_ts",
				  reference_frame_ts[idx_last], ref_last);
		visl_print_ts_idx(&buf, &buflen, "ref_last2_ts",
				  reference_frame_ts[idx_last2], ref_last2);
		visl_print_ts_idx(&buf, &buflen, "ref_last3_ts",
				  reference_frame_ts[idx_last3], ref_last3);
		visl_print_ts_idx(&buf, &buflen, "ref_golden_ts",
				  reference_frame_ts[idx_golden], ref_golden);
		visl_print_ts_idx(&buf, &buflen, "ref_bwd_ts",
				  reference_frame_ts[idx_bwd], ref_bwd);
		visl_print_ts_idx(&buf, &buflen, "ref_alt2_ts",
				  reference_frame_ts[idx_alt2], ref_alt2);
		visl_print_ts_idx(&buf, &buflen, "ref_alt_ts",
				  reference_frame_ts[idx_alt], ref_alt);

		break;
	}
	}
}

static char *visl_get_vb2_state(enum vb2_buffer_state state)
{
	switch (state) {
	case VB2_BUF_STATE_DEQUEUED:
		return "Dequeued";
	case VB2_BUF_STATE_IN_REQUEST:
		return "In request";
	case VB2_BUF_STATE_PREPARING:
		return "Preparing";
	case VB2_BUF_STATE_QUEUED:
		return "Queued";
	case VB2_BUF_STATE_ACTIVE:
		return "Active";
	case VB2_BUF_STATE_DONE:
		return "Done";
	case VB2_BUF_STATE_ERROR:
		return "Error";
	default:
		return "";
	}
}

static int visl_fill_bytesused(struct vb2_v4l2_buffer *v4l2_vb2_buf, char *buf, size_t bufsz)
{
	int len = 0;
	u32 i;

	for (i = 0; i < v4l2_vb2_buf->vb2_buf.num_planes; i++)
		len += scnprintf(buf, bufsz,
				"bytesused[%u]: %u length[%u]: %u data_offset[%u]: %u",
				i, v4l2_vb2_buf->planes[i].bytesused,
				i, v4l2_vb2_buf->planes[i].length,
				i, v4l2_vb2_buf->planes[i].data_offset);

	return len;
}

static void visl_tpg_fill_sequence(struct visl_ctx *ctx,
				   struct visl_run *run, char buf[], size_t bufsz)
{
	u32 stream_ms;
	int len;

	if (tpg_verbose) {
		stream_ms = jiffies_to_msecs(get_jiffies_64() - ctx->capture_streamon_jiffies);

		len = scnprintf(buf, bufsz,
				"stream time: %02d:%02d:%02d:%03d ",
				(stream_ms / (60 * 60 * 1000)) % 24,
				(stream_ms / (60 * 1000)) % 60,
				(stream_ms / 1000) % 60,
				stream_ms % 1000);
		buf += len;
		bufsz -= len;
	}

	scnprintf(buf, bufsz,
		  "sequence:%u timestamp:%lld field:%s",
		  run->dst->sequence,
		  run->dst->vb2_buf.timestamp,
		  (run->dst->field == V4L2_FIELD_ALTERNATE) ?
		  (run->dst->field == V4L2_FIELD_TOP ?
		  " top" : " bottom") : "none");
}

static bool visl_tpg_fill_codec_specific(struct visl_ctx *ctx,
					 struct visl_run *run,
					 char buf[], size_t bufsz)
{
	/*
	 * To add variability, we need a value that is stable for a given
	 * input but is different than already shown fields.
	 * The pic order count value defines the display order of the frames
	 * (which can be different than the decoding order that is shown with
	 * the sequence number).
	 * Therefore it is stable for a given input and will add a different
	 * value that is more specific to the way the input is encoded.
	 */
	switch (ctx->current_codec) {
	case VISL_CODEC_H264:
		scnprintf(buf, bufsz,
			  "H264: %u", run->h264.dpram->pic_order_cnt_lsb);
		break;
	case VISL_CODEC_HEVC:
		scnprintf(buf, bufsz,
			  "HEVC: %d", run->hevc.dpram->pic_order_cnt_val);
		break;
	default:
		return false;
	}

	return true;
}

static void visl_tpg_fill(struct visl_ctx *ctx, struct visl_run *run)
{
	u8 *basep[TPG_MAX_PLANES][2];
	char *buf = ctx->tpg_str_buf;
	char *tmp = buf;
	char *line_str;
	u32 line = 1;
	const u32 line_height = 16;
	u32 len;
	struct vb2_queue *out_q = &ctx->fh.m2m_ctx->out_q_ctx.q;
	struct vb2_queue *cap_q = &ctx->fh.m2m_ctx->cap_q_ctx.q;
	struct v4l2_pix_format_mplane *coded_fmt = &ctx->coded_fmt.fmt.pix_mp;
	struct v4l2_pix_format_mplane *decoded_fmt = &ctx->decoded_fmt.fmt.pix_mp;
	u32 p;
	u32 i;

	for (p = 0; p < tpg_g_planes(&ctx->tpg); p++) {
		void *vbuf = plane_vaddr(&ctx->tpg,
					 &run->dst->vb2_buf, p,
					 ctx->tpg.bytesperline,
					 ctx->tpg.buf_height);

		tpg_calc_text_basep(&ctx->tpg, basep, p, vbuf);
		tpg_fill_plane_buffer(&ctx->tpg, 0, p, vbuf);
	}

	visl_tpg_fill_sequence(ctx, run, buf, TPG_STR_BUF_SZ);
	tpg_gen_text(&ctx->tpg, basep, line++ * line_height, 16, buf);
	frame_dprintk(ctx->dev, run->dst->sequence, "%s\n", buf);
	frame_dprintk(ctx->dev, run->dst->sequence, "");
	line++;

	if (visl_tpg_fill_codec_specific(ctx, run, buf, TPG_STR_BUF_SZ)) {
		tpg_gen_text(&ctx->tpg, basep, line++ * line_height, 16, buf);
		frame_dprintk(ctx->dev, run->dst->sequence, "%s\n", buf);
		frame_dprintk(ctx->dev, run->dst->sequence, "");
		line++;
	}

	visl_get_ref_frames(ctx, buf, TPG_STR_BUF_SZ, run);

	while ((line_str = strsep(&tmp, "\n")) && strlen(line_str)) {
		tpg_gen_text(&ctx->tpg, basep, line++ * line_height, 16, line_str);
		frame_dprintk(ctx->dev, run->dst->sequence, "%s\n", line_str);
	}

	frame_dprintk(ctx->dev, run->dst->sequence, "");
	line++;

	scnprintf(buf,
		  TPG_STR_BUF_SZ,
		  "OUTPUT pixelformat: %c%c%c%c, resolution: %dx%d, num_planes: %d",
		  coded_fmt->pixelformat,
		  (coded_fmt->pixelformat >> 8) & 0xff,
		  (coded_fmt->pixelformat >> 16) & 0xff,
		  (coded_fmt->pixelformat >> 24) & 0xff,
		  coded_fmt->width,
		  coded_fmt->height,
		  coded_fmt->num_planes);

	tpg_gen_text(&ctx->tpg, basep, line++ * line_height, 16, buf);
	frame_dprintk(ctx->dev, run->dst->sequence, "%s\n", buf);

	for (i = 0; i < coded_fmt->num_planes; i++) {
		scnprintf(buf,
			  TPG_STR_BUF_SZ,
			  "plane[%d]: bytesperline: %d, sizeimage: %d",
			  i,
			  coded_fmt->plane_fmt[i].bytesperline,
			  coded_fmt->plane_fmt[i].sizeimage);

		tpg_gen_text(&ctx->tpg, basep, line++ * line_height, 16, buf);
		frame_dprintk(ctx->dev, run->dst->sequence, "%s\n", buf);
	}

	if (tpg_verbose) {
		line++;
		frame_dprintk(ctx->dev, run->dst->sequence, "");
		scnprintf(buf, TPG_STR_BUF_SZ, "Output queue status:");
		tpg_gen_text(&ctx->tpg, basep, line++ * line_height, 16, buf);
		frame_dprintk(ctx->dev, run->dst->sequence, "%s\n", buf);

		len = 0;
		for (i = 0; i < vb2_get_num_buffers(out_q); i++) {
			char entry[] = "index: %u, state: %s, request_fd: %d, ";
			u32 old_len = len;
			struct vb2_buffer *vb2;
			char *q_status;

			vb2 = vb2_get_buffer(out_q, i);
			if (!vb2)
				continue;

			q_status = visl_get_vb2_state(vb2->state);

			len += scnprintf(&buf[len], TPG_STR_BUF_SZ - len,
					 entry, i, q_status,
					 to_vb2_v4l2_buffer(vb2)->request_fd);

			len += visl_fill_bytesused(to_vb2_v4l2_buffer(vb2),
						   &buf[len],
						   TPG_STR_BUF_SZ - len);

			tpg_gen_text(&ctx->tpg, basep, line++ * line_height, 16, &buf[old_len]);
			frame_dprintk(ctx->dev, run->dst->sequence, "%s", &buf[old_len]);
		}
	}

	line++;
	frame_dprintk(ctx->dev, run->dst->sequence, "");

	scnprintf(buf,
		  TPG_STR_BUF_SZ,
		  "CAPTURE pixelformat: %c%c%c%c, resolution: %dx%d, num_planes: %d",
		  decoded_fmt->pixelformat,
		  (decoded_fmt->pixelformat >> 8) & 0xff,
		  (decoded_fmt->pixelformat >> 16) & 0xff,
		  (decoded_fmt->pixelformat >> 24) & 0xff,
		  decoded_fmt->width,
		  decoded_fmt->height,
		  decoded_fmt->num_planes);

	tpg_gen_text(&ctx->tpg, basep, line++ * line_height, 16, buf);
	frame_dprintk(ctx->dev, run->dst->sequence, "%s\n", buf);

	for (i = 0; i < decoded_fmt->num_planes; i++) {
		scnprintf(buf,
			  TPG_STR_BUF_SZ,
			  "plane[%d]: bytesperline: %d, sizeimage: %d",
			  i,
			  decoded_fmt->plane_fmt[i].bytesperline,
			  decoded_fmt->plane_fmt[i].sizeimage);

		tpg_gen_text(&ctx->tpg, basep, line++ * line_height, 16, buf);
		frame_dprintk(ctx->dev, run->dst->sequence, "%s\n", buf);
	}

	if (tpg_verbose) {
		line++;
		frame_dprintk(ctx->dev, run->dst->sequence, "");
		scnprintf(buf, TPG_STR_BUF_SZ, "Capture queue status:");
		tpg_gen_text(&ctx->tpg, basep, line++ * line_height, 16, buf);
		frame_dprintk(ctx->dev, run->dst->sequence, "%s\n", buf);

		len = 0;
		for (i = 0; i < vb2_get_num_buffers(cap_q); i++) {
			u32 old_len = len;
			struct vb2_buffer *vb2;
			char *q_status;

			vb2 = vb2_get_buffer(cap_q, i);
			if (!vb2)
				continue;

			q_status = visl_get_vb2_state(vb2->state);

			len += scnprintf(&buf[len], TPG_STR_BUF_SZ - len,
					 "index: %u, status: %s, timestamp: %llu, is_held: %d",
					 vb2->index, q_status,
					 vb2->timestamp,
					 to_vb2_v4l2_buffer(vb2)->is_held);

			tpg_gen_text(&ctx->tpg, basep, line++ * line_height, 16, &buf[old_len]);
			frame_dprintk(ctx->dev, run->dst->sequence, "%s", &buf[old_len]);
		}
	}
}

static void visl_trace_ctrls(struct visl_ctx *ctx, struct visl_run *run)
{
	int i;

	switch (ctx->current_codec) {
	default:
	case VISL_CODEC_NONE:
		break;
	case VISL_CODEC_FWHT:
		trace_v4l2_ctrl_fwht_params(run->fwht.params);
		break;
	case VISL_CODEC_MPEG2:
		trace_v4l2_ctrl_mpeg2_sequence(run->mpeg2.seq);
		trace_v4l2_ctrl_mpeg2_picture(run->mpeg2.pic);
		trace_v4l2_ctrl_mpeg2_quantisation(run->mpeg2.quant);
		break;
	case VISL_CODEC_VP8:
		trace_v4l2_ctrl_vp8_frame(run->vp8.frame);
		trace_v4l2_ctrl_vp8_entropy(run->vp8.frame);
		break;
	case VISL_CODEC_VP9:
		trace_v4l2_ctrl_vp9_frame(run->vp9.frame);
		trace_v4l2_ctrl_vp9_compressed_hdr(run->vp9.probs);
		trace_v4l2_ctrl_vp9_compressed_coeff(run->vp9.probs);
		trace_v4l2_vp9_mv_probs(&run->vp9.probs->mv);
		break;
	case VISL_CODEC_H264:
		trace_v4l2_ctrl_h264_sps(run->h264.sps);
		trace_v4l2_ctrl_h264_pps(run->h264.pps);
		trace_v4l2_ctrl_h264_scaling_matrix(run->h264.sm);
		trace_v4l2_ctrl_h264_slice_params(run->h264.spram);

		for (i = 0; i < ARRAY_SIZE(run->h264.spram->ref_pic_list0); i++)
			trace_v4l2_h264_ref_pic_list0(&run->h264.spram->ref_pic_list0[i], i);
		for (i = 0; i < ARRAY_SIZE(run->h264.spram->ref_pic_list0); i++)
			trace_v4l2_h264_ref_pic_list1(&run->h264.spram->ref_pic_list1[i], i);

		trace_v4l2_ctrl_h264_decode_params(run->h264.dpram);

		for (i = 0; i < ARRAY_SIZE(run->h264.dpram->dpb); i++)
			trace_v4l2_h264_dpb_entry(&run->h264.dpram->dpb[i], i);

		trace_v4l2_ctrl_h264_pred_weights(run->h264.pwht);
		break;
	case VISL_CODEC_HEVC:
		trace_v4l2_ctrl_hevc_sps(run->hevc.sps);
		trace_v4l2_ctrl_hevc_pps(run->hevc.pps);
		trace_v4l2_ctrl_hevc_slice_params(run->hevc.spram);
		trace_v4l2_ctrl_hevc_scaling_matrix(run->hevc.sm);
		trace_v4l2_ctrl_hevc_decode_params(run->hevc.dpram);

		for (i = 0; i < ARRAY_SIZE(run->hevc.dpram->dpb); i++)
			trace_v4l2_hevc_dpb_entry(&run->hevc.dpram->dpb[i]);

		trace_v4l2_hevc_pred_weight_table(&run->hevc.spram->pred_weight_table);
		break;
	case VISL_CODEC_AV1:
		trace_v4l2_ctrl_av1_sequence(run->av1.seq);
		trace_v4l2_ctrl_av1_frame(run->av1.frame);
		trace_v4l2_ctrl_av1_film_grain(run->av1.grain);
		trace_v4l2_ctrl_av1_tile_group_entry(run->av1.tge);
		break;
	}
}

void visl_device_run(void *priv)
{
	struct visl_ctx *ctx = priv;
	struct visl_run run = {};
	struct media_request *src_req;

	run.src = v4l2_m2m_next_src_buf(ctx->fh.m2m_ctx);
	run.dst = v4l2_m2m_next_dst_buf(ctx->fh.m2m_ctx);

	/* Apply request(s) controls if needed. */
	src_req = run.src->vb2_buf.req_obj.req;

	if (src_req)
		v4l2_ctrl_request_setup(src_req, &ctx->hdl);

	v4l2_m2m_buf_copy_metadata(run.src, run.dst, true);
	run.dst->sequence = ctx->q_data[V4L2_M2M_DST].sequence++;
	run.src->sequence = ctx->q_data[V4L2_M2M_SRC].sequence++;
	run.dst->field = ctx->decoded_fmt.fmt.pix.field;

	switch (ctx->current_codec) {
	default:
	case VISL_CODEC_NONE:
		break;
	case VISL_CODEC_FWHT:
		run.fwht.params = visl_find_control_data(ctx, V4L2_CID_STATELESS_FWHT_PARAMS);
		break;
	case VISL_CODEC_MPEG2:
		run.mpeg2.seq = visl_find_control_data(ctx, V4L2_CID_STATELESS_MPEG2_SEQUENCE);
		run.mpeg2.pic = visl_find_control_data(ctx, V4L2_CID_STATELESS_MPEG2_PICTURE);
		run.mpeg2.quant = visl_find_control_data(ctx,
							 V4L2_CID_STATELESS_MPEG2_QUANTISATION);
		break;
	case VISL_CODEC_VP8:
		run.vp8.frame = visl_find_control_data(ctx, V4L2_CID_STATELESS_VP8_FRAME);
		break;
	case VISL_CODEC_VP9:
		run.vp9.frame = visl_find_control_data(ctx, V4L2_CID_STATELESS_VP9_FRAME);
		run.vp9.probs = visl_find_control_data(ctx, V4L2_CID_STATELESS_VP9_COMPRESSED_HDR);
		break;
	case VISL_CODEC_H264:
		run.h264.sps = visl_find_control_data(ctx, V4L2_CID_STATELESS_H264_SPS);
		run.h264.pps = visl_find_control_data(ctx, V4L2_CID_STATELESS_H264_PPS);
		run.h264.sm = visl_find_control_data(ctx, V4L2_CID_STATELESS_H264_SCALING_MATRIX);
		run.h264.spram = visl_find_control_data(ctx, V4L2_CID_STATELESS_H264_SLICE_PARAMS);
		run.h264.dpram = visl_find_control_data(ctx, V4L2_CID_STATELESS_H264_DECODE_PARAMS);
		run.h264.pwht = visl_find_control_data(ctx, V4L2_CID_STATELESS_H264_PRED_WEIGHTS);
		break;
	case VISL_CODEC_HEVC:
		run.hevc.sps = visl_find_control_data(ctx, V4L2_CID_STATELESS_HEVC_SPS);
		run.hevc.pps = visl_find_control_data(ctx, V4L2_CID_STATELESS_HEVC_PPS);
		run.hevc.spram = visl_find_control_data(ctx, V4L2_CID_STATELESS_HEVC_SLICE_PARAMS);
		run.hevc.sm = visl_find_control_data(ctx, V4L2_CID_STATELESS_HEVC_SCALING_MATRIX);
		run.hevc.dpram = visl_find_control_data(ctx, V4L2_CID_STATELESS_HEVC_DECODE_PARAMS);
		break;
	case VISL_CODEC_AV1:
		run.av1.seq = visl_find_control_data(ctx, V4L2_CID_STATELESS_AV1_SEQUENCE);
		run.av1.frame = visl_find_control_data(ctx, V4L2_CID_STATELESS_AV1_FRAME);
		run.av1.tge = visl_find_control_data(ctx, V4L2_CID_STATELESS_AV1_TILE_GROUP_ENTRY);
		run.av1.grain = visl_find_control_data(ctx, V4L2_CID_STATELESS_AV1_FILM_GRAIN);
		break;
	}

	frame_dprintk(ctx->dev, run.dst->sequence,
		      "Got OUTPUT buffer sequence %d, timestamp %llu\n",
		      run.src->sequence, run.src->vb2_buf.timestamp);

	frame_dprintk(ctx->dev, run.dst->sequence,
		      "Got CAPTURE buffer sequence %d, timestamp %llu\n",
		      run.dst->sequence, run.dst->vb2_buf.timestamp);

	visl_tpg_fill(ctx, &run);
	visl_trace_ctrls(ctx, &run);

	if (bitstream_trace_frame_start > -1 &&
	    run.dst->sequence >= bitstream_trace_frame_start &&
	    run.dst->sequence < bitstream_trace_frame_start + bitstream_trace_nframes)
		visl_trace_bitstream(ctx, &run);

	/* Complete request(s) controls if needed. */
	if (src_req)
		v4l2_ctrl_request_complete(src_req, &ctx->hdl);

	if (visl_transtime_ms)
		usleep_range(visl_transtime_ms * 1000, 2 * visl_transtime_ms * 1000);

	v4l2_m2m_buf_done_and_job_finish(ctx->dev->m2m_dev,
					 ctx->fh.m2m_ctx, VB2_BUF_STATE_DONE);
}
