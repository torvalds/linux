// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2018 BayLibre, SAS
 * Author: Maxime Jourdan <mjourdan@baylibre.com>
 */

#include <linux/gcd.h>
#include <media/v4l2-mem2mem.h>
#include <media/v4l2-event.h>
#include <media/videobuf2-dma-contig.h>

#include "vdec_helpers.h"

#define NUM_CANVAS_NV12 2
#define NUM_CANVAS_YUV420 3

u32 amvdec_read_dos(struct amvdec_core *core, u32 reg)
{
	return readl_relaxed(core->dos_base + reg);
}
EXPORT_SYMBOL_GPL(amvdec_read_dos);

void amvdec_write_dos(struct amvdec_core *core, u32 reg, u32 val)
{
	writel_relaxed(val, core->dos_base + reg);
}
EXPORT_SYMBOL_GPL(amvdec_write_dos);

void amvdec_write_dos_bits(struct amvdec_core *core, u32 reg, u32 val)
{
	amvdec_write_dos(core, reg, amvdec_read_dos(core, reg) | val);
}
EXPORT_SYMBOL_GPL(amvdec_write_dos_bits);

void amvdec_clear_dos_bits(struct amvdec_core *core, u32 reg, u32 val)
{
	amvdec_write_dos(core, reg, amvdec_read_dos(core, reg) & ~val);
}
EXPORT_SYMBOL_GPL(amvdec_clear_dos_bits);

u32 amvdec_read_parser(struct amvdec_core *core, u32 reg)
{
	return readl_relaxed(core->esparser_base + reg);
}
EXPORT_SYMBOL_GPL(amvdec_read_parser);

void amvdec_write_parser(struct amvdec_core *core, u32 reg, u32 val)
{
	writel_relaxed(val, core->esparser_base + reg);
}
EXPORT_SYMBOL_GPL(amvdec_write_parser);

static int canvas_alloc(struct amvdec_session *sess, u8 *canvas_id)
{
	int ret;

	if (sess->canvas_num >= MAX_CANVAS) {
		dev_err(sess->core->dev, "Reached max number of canvas\n");
		return -ENOMEM;
	}

	ret = meson_canvas_alloc(sess->core->canvas, canvas_id);
	if (ret)
		return ret;

	sess->canvas_alloc[sess->canvas_num++] = *canvas_id;
	return 0;
}

static int set_canvas_yuv420m(struct amvdec_session *sess,
			      struct vb2_buffer *vb, u32 width,
			      u32 height, u32 reg)
{
	struct amvdec_core *core = sess->core;
	u8 canvas_id[NUM_CANVAS_YUV420]; /* Y U V */
	dma_addr_t buf_paddr[NUM_CANVAS_YUV420]; /* Y U V */
	int ret, i;

	for (i = 0; i < NUM_CANVAS_YUV420; ++i) {
		ret = canvas_alloc(sess, &canvas_id[i]);
		if (ret)
			return ret;

		buf_paddr[i] =
		    vb2_dma_contig_plane_dma_addr(vb, i);
	}

	/* Y plane */
	meson_canvas_config(core->canvas, canvas_id[0], buf_paddr[0],
			    width, height, MESON_CANVAS_WRAP_NONE,
			    MESON_CANVAS_BLKMODE_LINEAR,
			    MESON_CANVAS_ENDIAN_SWAP64);

	/* U plane */
	meson_canvas_config(core->canvas, canvas_id[1], buf_paddr[1],
			    width / 2, height / 2, MESON_CANVAS_WRAP_NONE,
			    MESON_CANVAS_BLKMODE_LINEAR,
			    MESON_CANVAS_ENDIAN_SWAP64);

	/* V plane */
	meson_canvas_config(core->canvas, canvas_id[2], buf_paddr[2],
			    width / 2, height / 2, MESON_CANVAS_WRAP_NONE,
			    MESON_CANVAS_BLKMODE_LINEAR,
			    MESON_CANVAS_ENDIAN_SWAP64);

	amvdec_write_dos(core, reg,
			 ((canvas_id[2]) << 16) |
			 ((canvas_id[1]) << 8)  |
			 (canvas_id[0]));

	return 0;
}

static int set_canvas_nv12m(struct amvdec_session *sess,
			    struct vb2_buffer *vb, u32 width,
			    u32 height, u32 reg)
{
	struct amvdec_core *core = sess->core;
	u8 canvas_id[NUM_CANVAS_NV12]; /* Y U/V */
	dma_addr_t buf_paddr[NUM_CANVAS_NV12]; /* Y U/V */
	int ret, i;

	for (i = 0; i < NUM_CANVAS_NV12; ++i) {
		ret = canvas_alloc(sess, &canvas_id[i]);
		if (ret)
			return ret;

		buf_paddr[i] =
		    vb2_dma_contig_plane_dma_addr(vb, i);
	}

	/* Y plane */
	meson_canvas_config(core->canvas, canvas_id[0], buf_paddr[0],
			    width, height, MESON_CANVAS_WRAP_NONE,
			    MESON_CANVAS_BLKMODE_LINEAR,
			    MESON_CANVAS_ENDIAN_SWAP64);

	/* U/V plane */
	meson_canvas_config(core->canvas, canvas_id[1], buf_paddr[1],
			    width, height / 2, MESON_CANVAS_WRAP_NONE,
			    MESON_CANVAS_BLKMODE_LINEAR,
			    MESON_CANVAS_ENDIAN_SWAP64);

	amvdec_write_dos(core, reg,
			 ((canvas_id[1]) << 16) |
			 ((canvas_id[1]) << 8)  |
			 (canvas_id[0]));

	return 0;
}

int amvdec_set_canvases(struct amvdec_session *sess,
			u32 reg_base[], u32 reg_num[])
{
	struct v4l2_m2m_buffer *buf;
	u32 pixfmt = sess->pixfmt_cap;
	u32 width = ALIGN(sess->width, 64);
	u32 height = ALIGN(sess->height, 64);
	u32 reg_cur = reg_base[0];
	u32 reg_num_cur = 0;
	u32 reg_base_cur = 0;
	int i = 0;
	int ret;

	v4l2_m2m_for_each_dst_buf(sess->m2m_ctx, buf) {
		if (!reg_base[reg_base_cur])
			return -EINVAL;

		reg_cur = reg_base[reg_base_cur] + reg_num_cur * 4;

		switch (pixfmt) {
		case V4L2_PIX_FMT_NV12M:
			ret = set_canvas_nv12m(sess, &buf->vb.vb2_buf, width,
					       height, reg_cur);
			if (ret)
				return ret;
			break;
		case V4L2_PIX_FMT_YUV420M:
			ret = set_canvas_yuv420m(sess, &buf->vb.vb2_buf, width,
						 height, reg_cur);
			if (ret)
				return ret;
			break;
		default:
			dev_err(sess->core->dev, "Unsupported pixfmt %08X\n",
				pixfmt);
			return -EINVAL;
		}

		reg_num_cur++;
		if (reg_num_cur >= reg_num[reg_base_cur]) {
			reg_base_cur++;
			reg_num_cur = 0;
		}

		sess->fw_idx_to_vb2_idx[i++] = buf->vb.vb2_buf.index;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(amvdec_set_canvases);

void amvdec_add_ts_reorder(struct amvdec_session *sess, u64 ts, u32 offset)
{
	struct amvdec_timestamp *new_ts, *tmp;
	unsigned long flags;

	new_ts = kmalloc(sizeof(*new_ts), GFP_KERNEL);
	new_ts->ts = ts;
	new_ts->offset = offset;

	spin_lock_irqsave(&sess->ts_spinlock, flags);

	if (list_empty(&sess->timestamps))
		goto add_tail;

	list_for_each_entry(tmp, &sess->timestamps, list) {
		if (ts <= tmp->ts) {
			list_add_tail(&new_ts->list, &tmp->list);
			goto unlock;
		}
	}

add_tail:
	list_add_tail(&new_ts->list, &sess->timestamps);
unlock:
	spin_unlock_irqrestore(&sess->ts_spinlock, flags);
}
EXPORT_SYMBOL_GPL(amvdec_add_ts_reorder);

void amvdec_remove_ts(struct amvdec_session *sess, u64 ts)
{
	struct amvdec_timestamp *tmp;
	unsigned long flags;

	spin_lock_irqsave(&sess->ts_spinlock, flags);
	list_for_each_entry(tmp, &sess->timestamps, list) {
		if (tmp->ts == ts) {
			list_del(&tmp->list);
			kfree(tmp);
			goto unlock;
		}
	}
	dev_warn(sess->core->dev_dec,
		 "Couldn't remove buffer with timestamp %llu from list\n", ts);

unlock:
	spin_unlock_irqrestore(&sess->ts_spinlock, flags);
}
EXPORT_SYMBOL_GPL(amvdec_remove_ts);

static void dst_buf_done(struct amvdec_session *sess,
			 struct vb2_v4l2_buffer *vbuf,
			 u32 field,
			 u64 timestamp)
{
	struct device *dev = sess->core->dev_dec;
	u32 output_size = amvdec_get_output_size(sess);

	switch (sess->pixfmt_cap) {
	case V4L2_PIX_FMT_NV12M:
		vbuf->vb2_buf.planes[0].bytesused = output_size;
		vbuf->vb2_buf.planes[1].bytesused = output_size / 2;
		break;
	case V4L2_PIX_FMT_YUV420M:
		vbuf->vb2_buf.planes[0].bytesused = output_size;
		vbuf->vb2_buf.planes[1].bytesused = output_size / 4;
		vbuf->vb2_buf.planes[2].bytesused = output_size / 4;
		break;
	}

	vbuf->vb2_buf.timestamp = timestamp;
	vbuf->sequence = sess->sequence_cap++;

	if (sess->should_stop &&
	    atomic_read(&sess->esparser_queued_bufs) <= 2) {
		const struct v4l2_event ev = { .type = V4L2_EVENT_EOS };

		dev_dbg(dev, "Signaling EOS\n");
		v4l2_event_queue_fh(&sess->fh, &ev);
		vbuf->flags |= V4L2_BUF_FLAG_LAST;
	} else if (sess->should_stop)
		dev_dbg(dev, "should_stop, %u bufs remain\n",
			atomic_read(&sess->esparser_queued_bufs));

	dev_dbg(dev, "Buffer %u done\n", vbuf->vb2_buf.index);
	vbuf->field = field;
	v4l2_m2m_buf_done(vbuf, VB2_BUF_STATE_DONE);

	/* Buffer done probably means the vififo got freed */
	schedule_work(&sess->esparser_queue_work);
}

void amvdec_dst_buf_done(struct amvdec_session *sess,
			 struct vb2_v4l2_buffer *vbuf, u32 field)
{
	struct device *dev = sess->core->dev_dec;
	struct amvdec_timestamp *tmp;
	struct list_head *timestamps = &sess->timestamps;
	u64 timestamp;
	unsigned long flags;

	spin_lock_irqsave(&sess->ts_spinlock, flags);
	if (list_empty(timestamps)) {
		dev_err(dev, "Buffer %u done but list is empty\n",
			vbuf->vb2_buf.index);

		v4l2_m2m_buf_done(vbuf, VB2_BUF_STATE_ERROR);
		spin_unlock_irqrestore(&sess->ts_spinlock, flags);
		return;
	}

	tmp = list_first_entry(timestamps, struct amvdec_timestamp, list);
	timestamp = tmp->ts;
	list_del(&tmp->list);
	kfree(tmp);
	spin_unlock_irqrestore(&sess->ts_spinlock, flags);

	dst_buf_done(sess, vbuf, field, timestamp);
	atomic_dec(&sess->esparser_queued_bufs);
}
EXPORT_SYMBOL_GPL(amvdec_dst_buf_done);

void amvdec_dst_buf_done_offset(struct amvdec_session *sess,
				struct vb2_v4l2_buffer *vbuf,
				u32 offset, u32 field, bool allow_drop)
{
	struct device *dev = sess->core->dev_dec;
	struct amvdec_timestamp *match = NULL;
	struct amvdec_timestamp *tmp, *n;
	u64 timestamp = 0;
	unsigned long flags;

	spin_lock_irqsave(&sess->ts_spinlock, flags);

	/* Look for our vififo offset to get the corresponding timestamp. */
	list_for_each_entry_safe(tmp, n, &sess->timestamps, list) {
		s64 delta = (s64)offset - tmp->offset;

		/* Offsets reported by codecs usually differ slightly,
		 * so we need some wiggle room.
		 * 4KiB being the minimum packet size, there is no risk here.
		 */
		if (delta > (-1 * (s32)SZ_4K) && delta < SZ_4K) {
			match = tmp;
			break;
		}

		if (!allow_drop)
			continue;

		/* Delete any timestamp entry that appears before our target
		 * (not all src packets/timestamps lead to a frame)
		 */
		if (delta > 0 || delta < -1 * (s32)sess->vififo_size) {
			atomic_dec(&sess->esparser_queued_bufs);
			list_del(&tmp->list);
			kfree(tmp);
		}
	}

	if (!match) {
		dev_dbg(dev, "Buffer %u done but can't match offset (%08X)\n",
			vbuf->vb2_buf.index, offset);
	} else {
		timestamp = match->ts;
		list_del(&match->list);
		kfree(match);
	}
	spin_unlock_irqrestore(&sess->ts_spinlock, flags);

	dst_buf_done(sess, vbuf, field, timestamp);
	if (match)
		atomic_dec(&sess->esparser_queued_bufs);
}
EXPORT_SYMBOL_GPL(amvdec_dst_buf_done_offset);

void amvdec_dst_buf_done_idx(struct amvdec_session *sess,
			     u32 buf_idx, u32 offset, u32 field)
{
	struct vb2_v4l2_buffer *vbuf;
	struct device *dev = sess->core->dev_dec;

	vbuf = v4l2_m2m_dst_buf_remove_by_idx(sess->m2m_ctx,
					      sess->fw_idx_to_vb2_idx[buf_idx]);

	if (!vbuf) {
		dev_err(dev,
			"Buffer %u done but it doesn't exist in m2m_ctx\n",
			buf_idx);
		return;
	}

	if (offset != -1)
		amvdec_dst_buf_done_offset(sess, vbuf, offset, field, true);
	else
		amvdec_dst_buf_done(sess, vbuf, field);
}
EXPORT_SYMBOL_GPL(amvdec_dst_buf_done_idx);

void amvdec_set_par_from_dar(struct amvdec_session *sess,
			     u32 dar_num, u32 dar_den)
{
	u32 div;

	sess->pixelaspect.numerator = sess->height * dar_num;
	sess->pixelaspect.denominator = sess->width * dar_den;
	div = gcd(sess->pixelaspect.numerator, sess->pixelaspect.denominator);
	sess->pixelaspect.numerator /= div;
	sess->pixelaspect.denominator /= div;
}
EXPORT_SYMBOL_GPL(amvdec_set_par_from_dar);

void amvdec_src_change(struct amvdec_session *sess, u32 width,
		       u32 height, u32 dpb_size)
{
	static const struct v4l2_event ev = {
		.type = V4L2_EVENT_SOURCE_CHANGE,
		.u.src_change.changes = V4L2_EVENT_SRC_CH_RESOLUTION };

	v4l2_ctrl_s_ctrl(sess->ctrl_min_buf_capture, dpb_size);

	/* Check if the capture queue is already configured well for our
	 * usecase. If so, keep decoding with it and do not send the event
	 */
	if (sess->width == width &&
	    sess->height == height &&
	    dpb_size <= sess->num_dst_bufs) {
		sess->fmt_out->codec_ops->resume(sess);
		return;
	}

	sess->width = width;
	sess->height = height;
	sess->status = STATUS_NEEDS_RESUME;

	dev_dbg(sess->core->dev, "Res. changed (%ux%u), DPB size %u\n",
		width, height, dpb_size);
	v4l2_event_queue_fh(&sess->fh, &ev);
}
EXPORT_SYMBOL_GPL(amvdec_src_change);

void amvdec_abort(struct amvdec_session *sess)
{
	dev_info(sess->core->dev, "Aborting decoding session!\n");
	vb2_queue_error(&sess->m2m_ctx->cap_q_ctx.q);
	vb2_queue_error(&sess->m2m_ctx->out_q_ctx.q);
}
EXPORT_SYMBOL_GPL(amvdec_abort);
