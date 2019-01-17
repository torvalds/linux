// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2018 Maxime Jourdan <mjourdan@baylibre.com>
 */

#include <media/v4l2-mem2mem.h>
#include <media/videobuf2-dma-contig.h>

#include "codec_hevc_common.h"
#include "vdec_helpers.h"
#include "hevc_regs.h"

/* Configure decode head read mode */
void codec_hevc_setup_decode_head(struct amvdec_session *sess, int is_10bit)
{
	struct amvdec_core *core = sess->core;
	u32 body_size = amvdec_am21c_body_size(sess->width, sess->height);
	u32 head_size = amvdec_am21c_head_size(sess->width, sess->height);

	if (!codec_hevc_use_fbc(sess->pixfmt_cap, is_10bit)) {
		/* Enable 2-plane reference read mode */
		amvdec_write_dos(core, HEVCD_MPP_DECOMP_CTL1, BIT(31));
		return;
	}

	amvdec_write_dos(core, HEVCD_MPP_DECOMP_CTL1, 0);
	amvdec_write_dos(core, HEVCD_MPP_DECOMP_CTL2, body_size / 32);
	amvdec_write_dos(core, HEVC_CM_BODY_LENGTH, body_size);
	amvdec_write_dos(core, HEVC_CM_HEADER_OFFSET, body_size);
	amvdec_write_dos(core, HEVC_CM_HEADER_LENGTH, head_size);
}
EXPORT_SYMBOL_GPL(codec_hevc_setup_decode_head);

static void
codec_hevc_setup_buffers_gxbb(struct amvdec_session *sess, int is_10bit)
{
	struct amvdec_core *core = sess->core;
	struct v4l2_m2m_buffer *buf;
	u32 buf_num = v4l2_m2m_num_dst_bufs_ready(sess->m2m_ctx);
	dma_addr_t buf_y_paddr = 0;
	dma_addr_t buf_uv_paddr = 0;
	u32 idx = 0;
	u32 val;
	int i;

	amvdec_write_dos(core, HEVCD_MPP_ANC2AXI_TBL_CONF_ADDR, 0);

	v4l2_m2m_for_each_dst_buf(sess->m2m_ctx, buf) {
		idx = buf->vb.vb2_buf.index;

		if (codec_hevc_use_downsample(sess->pixfmt_cap, is_10bit))
			buf_y_paddr = sess->fbc_buffer_paddr[idx];
		else
			buf_y_paddr = vb2_dma_contig_plane_dma_addr(&buf->vb.vb2_buf, 0);

		if (codec_hevc_use_fbc(sess->pixfmt_cap, is_10bit)) {
			val = buf_y_paddr | (idx << 8) | 1;
			amvdec_write_dos(core, HEVCD_MPP_ANC2AXI_TBL_CMD_ADDR, val);
		} else {
			buf_uv_paddr = vb2_dma_contig_plane_dma_addr(&buf->vb.vb2_buf, 1);
			val = buf_y_paddr | ((idx * 2) << 8) | 1;
			amvdec_write_dos(core, HEVCD_MPP_ANC2AXI_TBL_CMD_ADDR, val);
			val = buf_uv_paddr | ((idx * 2 + 1) << 8) | 1;
			amvdec_write_dos(core, HEVCD_MPP_ANC2AXI_TBL_CMD_ADDR, val);
		}
	}

	if (codec_hevc_use_fbc(sess->pixfmt_cap, is_10bit))
		val = buf_y_paddr | (idx << 8) | 1;
	else
		val = buf_y_paddr | ((idx * 2) << 8) | 1;

	/* Fill the remaining unused slots with the last buffer's Y addr */
	for (i = buf_num; i < MAX_REF_PIC_NUM; ++i)
		amvdec_write_dos(core, HEVCD_MPP_ANC2AXI_TBL_CMD_ADDR, val);

	amvdec_write_dos(core, HEVCD_MPP_ANC2AXI_TBL_CONF_ADDR, 1);
	amvdec_write_dos(core, HEVCD_MPP_ANC_CANVAS_ACCCONFIG_ADDR, 1);
	for (i = 0; i < 32; ++i)
		amvdec_write_dos(core, HEVCD_MPP_ANC_CANVAS_DATA_ADDR, 0);
}

static void
codec_hevc_setup_buffers_gxl(struct amvdec_session *sess, int is_10bit)
{
	struct amvdec_core *core = sess->core;
	struct v4l2_m2m_buffer *buf;
	u32 buf_num = v4l2_m2m_num_dst_bufs_ready(sess->m2m_ctx);
	dma_addr_t buf_y_paddr = 0;
	dma_addr_t buf_uv_paddr = 0;
	int i;

	amvdec_write_dos(core, HEVCD_MPP_ANC2AXI_TBL_CONF_ADDR,
			 BIT(2) | BIT(1));

	v4l2_m2m_for_each_dst_buf(sess->m2m_ctx, buf) {
		u32 idx = buf->vb.vb2_buf.index;

		if (codec_hevc_use_downsample(sess->pixfmt_cap, is_10bit))
			buf_y_paddr = sess->fbc_buffer_paddr[idx];
		else
			buf_y_paddr =
			    vb2_dma_contig_plane_dma_addr(&buf->vb.vb2_buf, 0);

		amvdec_write_dos(core, HEVCD_MPP_ANC2AXI_TBL_DATA,
				 buf_y_paddr >> 5);
		if (!codec_hevc_use_fbc(sess->pixfmt_cap, is_10bit)) {
			buf_uv_paddr = vb2_dma_contig_plane_dma_addr(&buf->vb.vb2_buf, 1);
			amvdec_write_dos(core, HEVCD_MPP_ANC2AXI_TBL_DATA,
					 buf_uv_paddr >> 5);
		}
	}

	/* Fill the remaining unused slots with the last buffer's Y addr */
	for (i = buf_num; i < MAX_REF_PIC_NUM; ++i) {
		amvdec_write_dos(core, HEVCD_MPP_ANC2AXI_TBL_DATA,
				 buf_y_paddr >> 5);
		if (!codec_hevc_use_fbc(sess->pixfmt_cap, is_10bit))
			amvdec_write_dos(core, HEVCD_MPP_ANC2AXI_TBL_DATA,
					 buf_uv_paddr >> 5);
	}

	amvdec_write_dos(core, HEVCD_MPP_ANC2AXI_TBL_CONF_ADDR, 1);
	amvdec_write_dos(core, HEVCD_MPP_ANC_CANVAS_ACCCONFIG_ADDR, 1);
	for (i = 0; i < 32; ++i)
		amvdec_write_dos(core, HEVCD_MPP_ANC_CANVAS_DATA_ADDR, 0);
}

void codec_hevc_free_fbc_buffers(struct amvdec_session *sess)
{
	struct device *dev = sess->core->dev;
	u32 am21_size = amvdec_am21c_size(sess->width, sess->height);
	int i;

	for (i = 0; i < MAX_REF_PIC_NUM; ++i) {
		if (sess->fbc_buffer_vaddr[i]) {
			dma_free_coherent(dev, am21_size,
					  sess->fbc_buffer_vaddr[i],
					  sess->fbc_buffer_paddr[i]);
			sess->fbc_buffer_vaddr[i] = NULL;
		}
	}
}
EXPORT_SYMBOL_GPL(codec_hevc_free_fbc_buffers);

static int codec_hevc_alloc_fbc_buffers(struct amvdec_session *sess)
{
	struct device *dev = sess->core->dev;
	struct v4l2_m2m_buffer *buf;
	u32 am21_size = amvdec_am21c_size(sess->width, sess->height);

	v4l2_m2m_for_each_dst_buf(sess->m2m_ctx, buf) {
		u32 idx = buf->vb.vb2_buf.index;
		dma_addr_t paddr;
		void *vaddr = dma_alloc_coherent(dev, am21_size, &paddr,
						 GFP_KERNEL);
		if (!vaddr) {
			dev_err(dev, "Couldn't allocate FBC buffer %u\n", idx);
			codec_hevc_free_fbc_buffers(sess);
			return -ENOMEM;
		}

		sess->fbc_buffer_vaddr[idx] = vaddr;
		sess->fbc_buffer_paddr[idx] = paddr;
	}

	return 0;
}

int codec_hevc_setup_buffers(struct amvdec_session *sess, int is_10bit)
{
	struct amvdec_core *core = sess->core;
	int ret;

	if (codec_hevc_use_downsample(sess->pixfmt_cap, is_10bit)) {
		ret = codec_hevc_alloc_fbc_buffers(sess);
		if (ret)
			return ret;
	}

	if (core->platform->revision == VDEC_REVISION_GXBB)
		codec_hevc_setup_buffers_gxbb(sess, is_10bit);
	else
		codec_hevc_setup_buffers_gxl(sess, is_10bit);

	return 0;
}
EXPORT_SYMBOL_GPL(codec_hevc_setup_buffers);