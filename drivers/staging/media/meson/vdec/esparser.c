// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2018 BayLibre, SAS
 * Author: Maxime Jourdan <mjourdan@baylibre.com>
 *
 * The Elementary Stream Parser is a HW bitstream parser.
 * It reads bitstream buffers and feeds them to the VIFIFO
 */

#include <linux/init.h>
#include <linux/ioctl.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/reset.h>
#include <linux/interrupt.h>
#include <media/videobuf2-dma-contig.h>
#include <media/v4l2-mem2mem.h>

#include "dos_regs.h"
#include "esparser.h"
#include "vdec_helpers.h"

/* PARSER REGS (CBUS) */
#define PARSER_CONTROL 0x00
	#define ES_PACK_SIZE_BIT	8
	#define ES_WRITE		BIT(5)
	#define ES_SEARCH		BIT(1)
	#define ES_PARSER_START		BIT(0)
#define PARSER_FETCH_ADDR	0x4
#define PARSER_FETCH_CMD	0x8
#define PARSER_CONFIG 0x14
	#define PS_CFG_MAX_FETCH_CYCLE_BIT	0
	#define PS_CFG_STARTCODE_WID_24_BIT	10
	#define PS_CFG_MAX_ES_WR_CYCLE_BIT	12
	#define PS_CFG_PFIFO_EMPTY_CNT_BIT	16
#define PFIFO_WR_PTR 0x18
#define PFIFO_RD_PTR 0x1c
#define PARSER_SEARCH_PATTERN 0x24
	#define ES_START_CODE_PATTERN 0x00000100
#define PARSER_SEARCH_MASK 0x28
	#define ES_START_CODE_MASK	0xffffff00
	#define FETCH_ENDIAN_BIT	27
#define PARSER_INT_ENABLE	0x2c
	#define PARSER_INT_HOST_EN_BIT	8
#define PARSER_INT_STATUS	0x30
	#define PARSER_INTSTAT_SC_FOUND	1
#define PARSER_ES_CONTROL	0x5c
#define PARSER_VIDEO_START_PTR	0x80
#define PARSER_VIDEO_END_PTR	0x84
#define PARSER_VIDEO_WP		0x88
#define PARSER_VIDEO_HOLE	0x90

#define SEARCH_PATTERN_LEN	512
#define VP9_HEADER_SIZE		16

static DECLARE_WAIT_QUEUE_HEAD(wq);
static int search_done;

static irqreturn_t esparser_isr(int irq, void *dev)
{
	int int_status;
	struct amvdec_core *core = dev;

	int_status = amvdec_read_parser(core, PARSER_INT_STATUS);
	amvdec_write_parser(core, PARSER_INT_STATUS, int_status);

	if (int_status & PARSER_INTSTAT_SC_FOUND) {
		amvdec_write_parser(core, PFIFO_RD_PTR, 0);
		amvdec_write_parser(core, PFIFO_WR_PTR, 0);
		search_done = 1;
		wake_up_interruptible(&wq);
	}

	return IRQ_HANDLED;
}

/*
 * VP9 frame headers need to be appended by a 16-byte long
 * Amlogic custom header
 */
static int vp9_update_header(struct amvdec_core *core, struct vb2_buffer *buf)
{
	u8 *dp;
	u8 marker;
	int dsize;
	int num_frames, cur_frame;
	int cur_mag, mag, mag_ptr;
	int frame_size[8], tot_frame_size[8];
	int total_datasize = 0;
	int new_frame_size;
	unsigned char *old_header = NULL;

	dp = (uint8_t *)vb2_plane_vaddr(buf, 0);
	dsize = vb2_get_plane_payload(buf, 0);

	if (dsize == vb2_plane_size(buf, 0)) {
		dev_warn(core->dev, "%s: unable to update header\n", __func__);
		return 0;
	}

	marker = dp[dsize - 1];
	if ((marker & 0xe0) == 0xc0) {
		num_frames = (marker & 0x7) + 1;
		mag = ((marker >> 3) & 0x3) + 1;
		mag_ptr = dsize - mag * num_frames - 2;
		if (dp[mag_ptr] != marker)
			return 0;

		mag_ptr++;
		for (cur_frame = 0; cur_frame < num_frames; cur_frame++) {
			frame_size[cur_frame] = 0;
			for (cur_mag = 0; cur_mag < mag; cur_mag++) {
				frame_size[cur_frame] |=
					(dp[mag_ptr] << (cur_mag * 8));
				mag_ptr++;
			}
			if (cur_frame == 0)
				tot_frame_size[cur_frame] =
					frame_size[cur_frame];
			else
				tot_frame_size[cur_frame] =
					tot_frame_size[cur_frame - 1] +
					frame_size[cur_frame];
			total_datasize += frame_size[cur_frame];
		}
	} else {
		num_frames = 1;
		frame_size[0] = dsize;
		tot_frame_size[0] = dsize;
		total_datasize = dsize;
	}

	new_frame_size = total_datasize + num_frames * VP9_HEADER_SIZE;

	if (new_frame_size >= vb2_plane_size(buf, 0)) {
		dev_warn(core->dev, "%s: unable to update header\n", __func__);
		return 0;
	}

	for (cur_frame = num_frames - 1; cur_frame >= 0; cur_frame--) {
		int framesize = frame_size[cur_frame];
		int framesize_header = framesize + 4;
		int oldframeoff = tot_frame_size[cur_frame] - framesize;
		int outheaderoff =  oldframeoff + cur_frame * VP9_HEADER_SIZE;
		u8 *fdata = dp + outheaderoff;
		u8 *old_framedata = dp + oldframeoff;

		memmove(fdata + VP9_HEADER_SIZE, old_framedata, framesize);

		fdata[0] = (framesize_header >> 24) & 0xff;
		fdata[1] = (framesize_header >> 16) & 0xff;
		fdata[2] = (framesize_header >> 8) & 0xff;
		fdata[3] = (framesize_header >> 0) & 0xff;
		fdata[4] = ((framesize_header >> 24) & 0xff) ^ 0xff;
		fdata[5] = ((framesize_header >> 16) & 0xff) ^ 0xff;
		fdata[6] = ((framesize_header >> 8) & 0xff) ^ 0xff;
		fdata[7] = ((framesize_header >> 0) & 0xff) ^ 0xff;
		fdata[8] = 0;
		fdata[9] = 0;
		fdata[10] = 0;
		fdata[11] = 1;
		fdata[12] = 'A';
		fdata[13] = 'M';
		fdata[14] = 'L';
		fdata[15] = 'V';

		if (!old_header) {
			/* nothing */
		} else if (old_header > fdata + 16 + framesize) {
			dev_dbg(core->dev, "%s: data has gaps, setting to 0\n",
				__func__);
			memset(fdata + 16 + framesize, 0,
			       (old_header - fdata + 16 + framesize));
		} else if (old_header < fdata + 16 + framesize) {
			dev_err(core->dev, "%s: data overwritten\n", __func__);
		}
		old_header = fdata;
	}

	return new_frame_size;
}

/* Pad the packet to at least 4KiB bytes otherwise the VDEC unit won't trigger
 * ISRs.
 * Also append a start code 000001ff at the end to trigger
 * the ESPARSER interrupt.
 */
static u32 esparser_pad_start_code(struct amvdec_core *core,
				   struct vb2_buffer *vb,
				   u32 payload_size)
{
	u32 pad_size = 0;
	u8 *vaddr = vb2_plane_vaddr(vb, 0);

	if (payload_size < ESPARSER_MIN_PACKET_SIZE) {
		pad_size = ESPARSER_MIN_PACKET_SIZE - payload_size;
		memset(vaddr + payload_size, 0, pad_size);
	}

	if ((payload_size + pad_size + SEARCH_PATTERN_LEN) >
						vb2_plane_size(vb, 0)) {
		dev_warn(core->dev, "%s: unable to pad start code\n", __func__);
		return pad_size;
	}

	memset(vaddr + payload_size + pad_size, 0, SEARCH_PATTERN_LEN);
	vaddr[payload_size + pad_size]     = 0x00;
	vaddr[payload_size + pad_size + 1] = 0x00;
	vaddr[payload_size + pad_size + 2] = 0x01;
	vaddr[payload_size + pad_size + 3] = 0xff;

	return pad_size;
}

static int
esparser_write_data(struct amvdec_core *core, dma_addr_t addr, u32 size)
{
	amvdec_write_parser(core, PFIFO_RD_PTR, 0);
	amvdec_write_parser(core, PFIFO_WR_PTR, 0);
	amvdec_write_parser(core, PARSER_CONTROL,
			    ES_WRITE |
			    ES_PARSER_START |
			    ES_SEARCH |
			    (size << ES_PACK_SIZE_BIT));

	amvdec_write_parser(core, PARSER_FETCH_ADDR, addr);
	amvdec_write_parser(core, PARSER_FETCH_CMD,
			    (7 << FETCH_ENDIAN_BIT) |
			    (size + SEARCH_PATTERN_LEN));

	search_done = 0;
	return wait_event_interruptible_timeout(wq, search_done, (HZ / 5));
}

static u32 esparser_vififo_get_free_space(struct amvdec_session *sess)
{
	u32 vififo_usage;
	struct amvdec_ops *vdec_ops = sess->fmt_out->vdec_ops;
	struct amvdec_core *core = sess->core;

	vififo_usage  = vdec_ops->vififo_level(sess);
	vififo_usage += amvdec_read_parser(core, PARSER_VIDEO_HOLE);
	vififo_usage += (6 * SZ_1K); // 6 KiB internal fifo

	if (vififo_usage > sess->vififo_size) {
		dev_warn(sess->core->dev,
			 "VIFIFO usage (%u) > VIFIFO size (%u)\n",
			 vififo_usage, sess->vififo_size);
		return 0;
	}

	return sess->vififo_size - vififo_usage;
}

int esparser_queue_eos(struct amvdec_core *core, const u8 *data, u32 len)
{
	struct device *dev = core->dev;
	void *eos_vaddr;
	dma_addr_t eos_paddr;
	int ret;

	eos_vaddr = dma_alloc_coherent(dev, len + SEARCH_PATTERN_LEN,
				       &eos_paddr, GFP_KERNEL);
	if (!eos_vaddr)
		return -ENOMEM;

	memcpy(eos_vaddr, data, len);
	ret = esparser_write_data(core, eos_paddr, len);
	dma_free_coherent(dev, len + SEARCH_PATTERN_LEN,
			  eos_vaddr, eos_paddr);

	return ret;
}

static u32 esparser_get_offset(struct amvdec_session *sess)
{
	struct amvdec_core *core = sess->core;
	u32 offset = amvdec_read_parser(core, PARSER_VIDEO_WP) -
		     sess->vififo_paddr;

	if (offset < sess->last_offset)
		sess->wrap_count++;

	sess->last_offset = offset;
	offset += (sess->wrap_count * sess->vififo_size);

	return offset;
}

static int
esparser_queue(struct amvdec_session *sess, struct vb2_v4l2_buffer *vbuf)
{
	int ret;
	struct vb2_buffer *vb = &vbuf->vb2_buf;
	struct amvdec_core *core = sess->core;
	struct amvdec_codec_ops *codec_ops = sess->fmt_out->codec_ops;
	u32 payload_size = vb2_get_plane_payload(vb, 0);
	dma_addr_t phy = vb2_dma_contig_plane_dma_addr(vb, 0);
	u32 num_dst_bufs = 0;
	u32 offset;
	u32 pad_size;

	/*
	 * When max ref frame is held by VP9, this should be -= 3 to prevent a
	 * shortage of CAPTURE buffers on the decoder side.
	 * For the future, a good enhancement of the way this is handled could
	 * be to notify new capture buffers to the decoding modules, so that
	 * they could pause when there is no capture buffer available and
	 * resume on this notification.
	 */
	if (sess->fmt_out->pixfmt == V4L2_PIX_FMT_VP9) {
		if (codec_ops->num_pending_bufs)
			num_dst_bufs = codec_ops->num_pending_bufs(sess);

		num_dst_bufs += v4l2_m2m_num_dst_bufs_ready(sess->m2m_ctx);
		num_dst_bufs -= 3;

		if (esparser_vififo_get_free_space(sess) < payload_size ||
		    atomic_read(&sess->esparser_queued_bufs) >= num_dst_bufs)
			return -EAGAIN;
	} else if (esparser_vififo_get_free_space(sess) < payload_size) {
		return -EAGAIN;
	}

	v4l2_m2m_src_buf_remove_by_buf(sess->m2m_ctx, vbuf);

	offset = esparser_get_offset(sess);

	ret = amvdec_add_ts(sess, vb->timestamp, vbuf->timecode, offset, vbuf->flags);
	if (ret) {
		v4l2_m2m_buf_done(vbuf, VB2_BUF_STATE_ERROR);
		return ret;
	}

	dev_dbg(core->dev, "esparser: ts = %llu pld_size = %u offset = %08X flags = %08X\n",
		vb->timestamp, payload_size, offset, vbuf->flags);

	vbuf->flags = 0;
	vbuf->field = V4L2_FIELD_NONE;
	vbuf->sequence = sess->sequence_out++;

	if (sess->fmt_out->pixfmt == V4L2_PIX_FMT_VP9) {
		payload_size = vp9_update_header(core, vb);

		/* If unable to alter buffer to add headers */
		if (payload_size == 0) {
			amvdec_remove_ts(sess, vb->timestamp);
			v4l2_m2m_buf_done(vbuf, VB2_BUF_STATE_ERROR);

			return 0;
		}
	}

	pad_size = esparser_pad_start_code(core, vb, payload_size);
	ret = esparser_write_data(core, phy, payload_size + pad_size);

	if (ret <= 0) {
		dev_warn(core->dev, "esparser: input parsing error\n");
		amvdec_remove_ts(sess, vb->timestamp);
		v4l2_m2m_buf_done(vbuf, VB2_BUF_STATE_ERROR);
		amvdec_write_parser(core, PARSER_FETCH_CMD, 0);

		return 0;
	}

	atomic_inc(&sess->esparser_queued_bufs);
	v4l2_m2m_buf_done(vbuf, VB2_BUF_STATE_DONE);

	return 0;
}

void esparser_queue_all_src(struct work_struct *work)
{
	struct v4l2_m2m_buffer *buf, *n;
	struct amvdec_session *sess =
		container_of(work, struct amvdec_session, esparser_queue_work);

	mutex_lock(&sess->lock);
	v4l2_m2m_for_each_src_buf_safe(sess->m2m_ctx, buf, n) {
		if (sess->should_stop)
			break;

		if (esparser_queue(sess, &buf->vb) < 0)
			break;
	}
	mutex_unlock(&sess->lock);
}

int esparser_power_up(struct amvdec_session *sess)
{
	struct amvdec_core *core = sess->core;
	struct amvdec_ops *vdec_ops = sess->fmt_out->vdec_ops;

	reset_control_reset(core->esparser_reset);
	amvdec_write_parser(core, PARSER_CONFIG,
			    (10 << PS_CFG_PFIFO_EMPTY_CNT_BIT) |
			    (1  << PS_CFG_MAX_ES_WR_CYCLE_BIT) |
			    (16 << PS_CFG_MAX_FETCH_CYCLE_BIT));

	amvdec_write_parser(core, PFIFO_RD_PTR, 0);
	amvdec_write_parser(core, PFIFO_WR_PTR, 0);

	amvdec_write_parser(core, PARSER_SEARCH_PATTERN,
			    ES_START_CODE_PATTERN);
	amvdec_write_parser(core, PARSER_SEARCH_MASK, ES_START_CODE_MASK);

	amvdec_write_parser(core, PARSER_CONFIG,
			    (10 << PS_CFG_PFIFO_EMPTY_CNT_BIT) |
			    (1  << PS_CFG_MAX_ES_WR_CYCLE_BIT) |
			    (16 << PS_CFG_MAX_FETCH_CYCLE_BIT) |
			    (2  << PS_CFG_STARTCODE_WID_24_BIT));

	amvdec_write_parser(core, PARSER_CONTROL,
			    (ES_SEARCH | ES_PARSER_START));

	amvdec_write_parser(core, PARSER_VIDEO_START_PTR, sess->vififo_paddr);
	amvdec_write_parser(core, PARSER_VIDEO_END_PTR,
			    sess->vififo_paddr + sess->vififo_size - 8);
	amvdec_write_parser(core, PARSER_ES_CONTROL,
			    amvdec_read_parser(core, PARSER_ES_CONTROL) & ~1);

	if (vdec_ops->conf_esparser)
		vdec_ops->conf_esparser(sess);

	amvdec_write_parser(core, PARSER_INT_STATUS, 0xffff);
	amvdec_write_parser(core, PARSER_INT_ENABLE,
			    BIT(PARSER_INT_HOST_EN_BIT));

	return 0;
}

int esparser_init(struct platform_device *pdev, struct amvdec_core *core)
{
	struct device *dev = &pdev->dev;
	int ret;
	int irq;

	irq = platform_get_irq_byname(pdev, "esparser");
	if (irq < 0)
		return irq;

	ret = devm_request_irq(dev, irq, esparser_isr, IRQF_SHARED,
			       "esparserirq", core);
	if (ret) {
		dev_err(dev, "Failed requesting ESPARSER IRQ\n");
		return ret;
	}

	core->esparser_reset =
		devm_reset_control_get_exclusive(dev, "esparser");
	if (IS_ERR(core->esparser_reset)) {
		dev_err(dev, "Failed to get esparser_reset\n");
		return PTR_ERR(core->esparser_reset);
	}

	return 0;
}
