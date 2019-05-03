/*
 * NVIDIA Tegra Video decoder driver
 *
 * Copyright (C) 2016-2017 Dmitry Osipenko <digetx@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/clk.h>
#include <linux/dma-buf.h>
#include <linux/genalloc.h>
#include <linux/interrupt.h>
#include <linux/iopoll.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/pm_runtime.h>
#include <linux/reset.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

#include <soc/tegra/pmc.h>

#include "uapi.h"

#define ICMDQUE_WR		0x00
#define CMDQUE_CONTROL		0x08
#define INTR_STATUS		0x18
#define BSE_INT_ENB		0x40
#define BSE_CONFIG		0x44

#define BSE_ICMDQUE_EMPTY	BIT(3)
#define BSE_DMA_BUSY		BIT(23)

struct video_frame {
	struct dma_buf_attachment *y_dmabuf_attachment;
	struct dma_buf_attachment *cb_dmabuf_attachment;
	struct dma_buf_attachment *cr_dmabuf_attachment;
	struct dma_buf_attachment *aux_dmabuf_attachment;
	struct sg_table *y_sgt;
	struct sg_table *cb_sgt;
	struct sg_table *cr_sgt;
	struct sg_table *aux_sgt;
	dma_addr_t y_addr;
	dma_addr_t cb_addr;
	dma_addr_t cr_addr;
	dma_addr_t aux_addr;
	u32 frame_num;
	u32 flags;
};

struct tegra_vde {
	void __iomem *sxe;
	void __iomem *bsev;
	void __iomem *mbe;
	void __iomem *ppe;
	void __iomem *mce;
	void __iomem *tfe;
	void __iomem *ppb;
	void __iomem *vdma;
	void __iomem *frameid;
	struct mutex lock;
	struct miscdevice miscdev;
	struct reset_control *rst;
	struct reset_control *rst_mc;
	struct gen_pool *iram_pool;
	struct completion decode_completion;
	struct clk *clk;
	dma_addr_t iram_lists_addr;
	u32 *iram;
};

static __maybe_unused char const *
tegra_vde_reg_base_name(struct tegra_vde *vde, void __iomem *base)
{
	if (vde->sxe == base)
		return "SXE";

	if (vde->bsev == base)
		return "BSEV";

	if (vde->mbe == base)
		return "MBE";

	if (vde->ppe == base)
		return "PPE";

	if (vde->mce == base)
		return "MCE";

	if (vde->tfe == base)
		return "TFE";

	if (vde->ppb == base)
		return "PPB";

	if (vde->vdma == base)
		return "VDMA";

	if (vde->frameid == base)
		return "FRAMEID";

	return "???";
}

#define CREATE_TRACE_POINTS
#include "trace.h"

static void tegra_vde_writel(struct tegra_vde *vde,
			     u32 value, void __iomem *base, u32 offset)
{
	trace_vde_writel(vde, base, offset, value);

	writel_relaxed(value, base + offset);
}

static u32 tegra_vde_readl(struct tegra_vde *vde,
			   void __iomem *base, u32 offset)
{
	u32 value = readl_relaxed(base + offset);

	trace_vde_readl(vde, base, offset, value);

	return value;
}

static void tegra_vde_set_bits(struct tegra_vde *vde,
			       u32 mask, void __iomem *base, u32 offset)
{
	u32 value = tegra_vde_readl(vde, base, offset);

	tegra_vde_writel(vde, value | mask, base, offset);
}

static int tegra_vde_wait_mbe(struct tegra_vde *vde)
{
	u32 tmp;

	return readl_relaxed_poll_timeout(vde->mbe + 0x8C, tmp,
					  (tmp >= 0x10), 1, 100);
}

static int tegra_vde_setup_mbe_frame_idx(struct tegra_vde *vde,
					 unsigned int refs_nb,
					 bool setup_refs)
{
	u32 frame_idx_enb_mask = 0;
	u32 value;
	unsigned int frame_idx;
	unsigned int idx;
	int err;

	tegra_vde_writel(vde, 0xD0000000 | (0 << 23), vde->mbe, 0x80);
	tegra_vde_writel(vde, 0xD0200000 | (0 << 23), vde->mbe, 0x80);

	err = tegra_vde_wait_mbe(vde);
	if (err)
		return err;

	if (!setup_refs)
		return 0;

	for (idx = 0, frame_idx = 1; idx < refs_nb; idx++, frame_idx++) {
		tegra_vde_writel(vde, 0xD0000000 | (frame_idx << 23),
				 vde->mbe, 0x80);
		tegra_vde_writel(vde, 0xD0200000 | (frame_idx << 23),
				 vde->mbe, 0x80);

		frame_idx_enb_mask |= frame_idx << (6 * (idx % 4));

		if (idx % 4 == 3 || idx == refs_nb - 1) {
			value = 0xC0000000;
			value |= (idx >> 2) << 24;
			value |= frame_idx_enb_mask;

			tegra_vde_writel(vde, value, vde->mbe, 0x80);

			err = tegra_vde_wait_mbe(vde);
			if (err)
				return err;

			frame_idx_enb_mask = 0;
		}
	}

	return 0;
}

static void tegra_vde_mbe_set_0xa_reg(struct tegra_vde *vde, int reg, u32 val)
{
	tegra_vde_writel(vde, 0xA0000000 | (reg << 24) | (val & 0xFFFF),
			 vde->mbe, 0x80);
	tegra_vde_writel(vde, 0xA0000000 | ((reg + 1) << 24) | (val >> 16),
			 vde->mbe, 0x80);
}

static int tegra_vde_wait_bsev(struct tegra_vde *vde, bool wait_dma)
{
	struct device *dev = vde->miscdev.parent;
	u32 value;
	int err;

	err = readl_relaxed_poll_timeout(vde->bsev + INTR_STATUS, value,
					 !(value & BIT(2)), 1, 100);
	if (err) {
		dev_err(dev, "BSEV unknown bit timeout\n");
		return err;
	}

	err = readl_relaxed_poll_timeout(vde->bsev + INTR_STATUS, value,
					 (value & BSE_ICMDQUE_EMPTY), 1, 100);
	if (err) {
		dev_err(dev, "BSEV ICMDQUE flush timeout\n");
		return err;
	}

	if (!wait_dma)
		return 0;

	err = readl_relaxed_poll_timeout(vde->bsev + INTR_STATUS, value,
					 !(value & BSE_DMA_BUSY), 1, 100);
	if (err) {
		dev_err(dev, "BSEV DMA timeout\n");
		return err;
	}

	return 0;
}

static int tegra_vde_push_to_bsev_icmdqueue(struct tegra_vde *vde,
					    u32 value, bool wait_dma)
{
	tegra_vde_writel(vde, value, vde->bsev, ICMDQUE_WR);

	return tegra_vde_wait_bsev(vde, wait_dma);
}

static void tegra_vde_setup_frameid(struct tegra_vde *vde,
				    struct video_frame *frame,
				    unsigned int frameid,
				    u32 mbs_width, u32 mbs_height)
{
	u32 y_addr  = frame ? frame->y_addr  : 0x6CDEAD00;
	u32 cb_addr = frame ? frame->cb_addr : 0x6CDEAD00;
	u32 cr_addr = frame ? frame->cr_addr : 0x6CDEAD00;
	u32 value1 = frame ? ((mbs_width << 16) | mbs_height) : 0;
	u32 value2 = frame ? ((((mbs_width + 1) >> 1) << 6) | 1) : 0;

	tegra_vde_writel(vde, y_addr  >> 8, vde->frameid, 0x000 + frameid * 4);
	tegra_vde_writel(vde, cb_addr >> 8, vde->frameid, 0x100 + frameid * 4);
	tegra_vde_writel(vde, cr_addr >> 8, vde->frameid, 0x180 + frameid * 4);
	tegra_vde_writel(vde, value1,       vde->frameid, 0x080 + frameid * 4);
	tegra_vde_writel(vde, value2,       vde->frameid, 0x280 + frameid * 4);
}

static void tegra_setup_frameidx(struct tegra_vde *vde,
				 struct video_frame *frames,
				 unsigned int frames_nb,
				 u32 mbs_width, u32 mbs_height)
{
	unsigned int idx;

	for (idx = 0; idx < frames_nb; idx++)
		tegra_vde_setup_frameid(vde, &frames[idx], idx,
					mbs_width, mbs_height);

	for (; idx < 17; idx++)
		tegra_vde_setup_frameid(vde, NULL, idx, 0, 0);
}

static void tegra_vde_setup_iram_entry(struct tegra_vde *vde,
				       unsigned int table,
				       unsigned int row,
				       u32 value1, u32 value2)
{
	u32 *iram_tables = vde->iram;

	trace_vde_setup_iram_entry(table, row, value1, value2);

	iram_tables[0x20 * table + row * 2] = value1;
	iram_tables[0x20 * table + row * 2 + 1] = value2;
}

static void tegra_vde_setup_iram_tables(struct tegra_vde *vde,
					struct video_frame *dpb_frames,
					unsigned int ref_frames_nb,
					unsigned int with_earlier_poc_nb)
{
	struct video_frame *frame;
	u32 value, aux_addr;
	int with_later_poc_nb;
	unsigned int i, k;

	trace_vde_ref_l0(dpb_frames[0].frame_num);

	for (i = 0; i < 16; i++) {
		if (i < ref_frames_nb) {
			frame = &dpb_frames[i + 1];

			aux_addr = frame->aux_addr;

			value  = (i + 1) << 26;
			value |= !(frame->flags & FLAG_B_FRAME) << 25;
			value |= 1 << 24;
			value |= frame->frame_num;
		} else {
			aux_addr = 0x6ADEAD00;
			value = 0;
		}

		tegra_vde_setup_iram_entry(vde, 0, i, value, aux_addr);
		tegra_vde_setup_iram_entry(vde, 1, i, value, aux_addr);
		tegra_vde_setup_iram_entry(vde, 2, i, value, aux_addr);
		tegra_vde_setup_iram_entry(vde, 3, i, value, aux_addr);
	}

	if (!(dpb_frames[0].flags & FLAG_B_FRAME))
		return;

	if (with_earlier_poc_nb >= ref_frames_nb)
		return;

	with_later_poc_nb = ref_frames_nb - with_earlier_poc_nb;

	trace_vde_ref_l1(with_later_poc_nb, with_earlier_poc_nb);

	for (i = 0, k = with_earlier_poc_nb; i < with_later_poc_nb; i++, k++) {
		frame = &dpb_frames[k + 1];

		aux_addr = frame->aux_addr;

		value  = (k + 1) << 26;
		value |= !(frame->flags & FLAG_B_FRAME) << 25;
		value |= 1 << 24;
		value |= frame->frame_num;

		tegra_vde_setup_iram_entry(vde, 2, i, value, aux_addr);
	}

	for (k = 0; i < ref_frames_nb; i++, k++) {
		frame = &dpb_frames[k + 1];

		aux_addr = frame->aux_addr;

		value  = (k + 1) << 26;
		value |= !(frame->flags & FLAG_B_FRAME) << 25;
		value |= 1 << 24;
		value |= frame->frame_num;

		tegra_vde_setup_iram_entry(vde, 2, i, value, aux_addr);
	}
}

static int tegra_vde_setup_hw_context(struct tegra_vde *vde,
				      struct tegra_vde_h264_decoder_ctx *ctx,
				      struct video_frame *dpb_frames,
				      dma_addr_t bitstream_data_addr,
				      size_t bitstream_data_size,
				      unsigned int macroblocks_nb)
{
	struct device *dev = vde->miscdev.parent;
	u32 value;
	int err;

	tegra_vde_set_bits(vde, 0x000A, vde->sxe, 0xF0);
	tegra_vde_set_bits(vde, 0x000B, vde->bsev, CMDQUE_CONTROL);
	tegra_vde_set_bits(vde, 0x8002, vde->mbe, 0x50);
	tegra_vde_set_bits(vde, 0x000A, vde->mbe, 0xA0);
	tegra_vde_set_bits(vde, 0x000A, vde->ppe, 0x14);
	tegra_vde_set_bits(vde, 0x000A, vde->ppe, 0x28);
	tegra_vde_set_bits(vde, 0x0A00, vde->mce, 0x08);
	tegra_vde_set_bits(vde, 0x000A, vde->tfe, 0x00);
	tegra_vde_set_bits(vde, 0x0005, vde->vdma, 0x04);

	tegra_vde_writel(vde, 0x00000000, vde->vdma, 0x1C);
	tegra_vde_writel(vde, 0x00000000, vde->vdma, 0x00);
	tegra_vde_writel(vde, 0x00000007, vde->vdma, 0x04);
	tegra_vde_writel(vde, 0x00000007, vde->frameid, 0x200);
	tegra_vde_writel(vde, 0x00000005, vde->tfe, 0x04);
	tegra_vde_writel(vde, 0x00000000, vde->mbe, 0x84);
	tegra_vde_writel(vde, 0x00000010, vde->sxe, 0x08);
	tegra_vde_writel(vde, 0x00000150, vde->sxe, 0x54);
	tegra_vde_writel(vde, 0x0000054C, vde->sxe, 0x58);
	tegra_vde_writel(vde, 0x00000E34, vde->sxe, 0x5C);
	tegra_vde_writel(vde, 0x063C063C, vde->mce, 0x10);
	tegra_vde_writel(vde, 0x0003FC00, vde->bsev, INTR_STATUS);
	tegra_vde_writel(vde, 0x0000150D, vde->bsev, BSE_CONFIG);
	tegra_vde_writel(vde, 0x00000100, vde->bsev, BSE_INT_ENB);
	tegra_vde_writel(vde, 0x00000000, vde->bsev, 0x98);
	tegra_vde_writel(vde, 0x00000060, vde->bsev, 0x9C);

	memset(vde->iram + 128, 0, macroblocks_nb / 2);

	tegra_setup_frameidx(vde, dpb_frames, ctx->dpb_frames_nb,
			     ctx->pic_width_in_mbs, ctx->pic_height_in_mbs);

	tegra_vde_setup_iram_tables(vde, dpb_frames,
				    ctx->dpb_frames_nb - 1,
				    ctx->dpb_ref_frames_with_earlier_poc_nb);

	/*
	 * The IRAM mapping is write-combine, ensure that CPU buffers have
	 * been flushed at this point.
	 */
	wmb();

	tegra_vde_writel(vde, 0x00000000, vde->bsev, 0x8C);
	tegra_vde_writel(vde, bitstream_data_addr + bitstream_data_size,
			 vde->bsev, 0x54);

	value = ctx->pic_width_in_mbs << 11 | ctx->pic_height_in_mbs << 3;

	tegra_vde_writel(vde, value, vde->bsev, 0x88);

	err = tegra_vde_wait_bsev(vde, false);
	if (err)
		return err;

	err = tegra_vde_push_to_bsev_icmdqueue(vde, 0x800003FC, false);
	if (err)
		return err;

	value = 0x01500000;
	value |= ((vde->iram_lists_addr + 512) >> 2) & 0xFFFF;

	err = tegra_vde_push_to_bsev_icmdqueue(vde, value, true);
	if (err)
		return err;

	err = tegra_vde_push_to_bsev_icmdqueue(vde, 0x840F054C, false);
	if (err)
		return err;

	err = tegra_vde_push_to_bsev_icmdqueue(vde, 0x80000080, false);
	if (err)
		return err;

	value = 0x0E340000 | ((vde->iram_lists_addr >> 2) & 0xFFFF);

	err = tegra_vde_push_to_bsev_icmdqueue(vde, value, true);
	if (err)
		return err;

	value = 0x00800005;
	value |= ctx->pic_width_in_mbs << 11;
	value |= ctx->pic_height_in_mbs << 3;

	tegra_vde_writel(vde, value, vde->sxe, 0x10);

	value = !ctx->baseline_profile << 17;
	value |= ctx->level_idc << 13;
	value |= ctx->log2_max_pic_order_cnt_lsb << 7;
	value |= ctx->pic_order_cnt_type << 5;
	value |= ctx->log2_max_frame_num;

	tegra_vde_writel(vde, value, vde->sxe, 0x40);

	value = ctx->pic_init_qp << 25;
	value |= !!(ctx->deblocking_filter_control_present_flag) << 2;
	value |= !!ctx->pic_order_present_flag;

	tegra_vde_writel(vde, value, vde->sxe, 0x44);

	value = ctx->chroma_qp_index_offset;
	value |= ctx->num_ref_idx_l0_active_minus1 << 5;
	value |= ctx->num_ref_idx_l1_active_minus1 << 10;
	value |= !!ctx->constrained_intra_pred_flag << 15;

	tegra_vde_writel(vde, value, vde->sxe, 0x48);

	value = 0x0C000000;
	value |= !!(dpb_frames[0].flags & FLAG_B_FRAME) << 24;

	tegra_vde_writel(vde, value, vde->sxe, 0x4C);

	value = 0x03800000;
	value |= bitstream_data_size & GENMASK(19, 15);

	tegra_vde_writel(vde, value, vde->sxe, 0x68);

	tegra_vde_writel(vde, bitstream_data_addr, vde->sxe, 0x6C);

	value = 0x10000005;
	value |= ctx->pic_width_in_mbs << 11;
	value |= ctx->pic_height_in_mbs << 3;

	tegra_vde_writel(vde, value, vde->mbe, 0x80);

	value = 0x26800000;
	value |= ctx->level_idc << 4;
	value |= !ctx->baseline_profile << 1;
	value |= !!ctx->direct_8x8_inference_flag;

	tegra_vde_writel(vde, value, vde->mbe, 0x80);

	tegra_vde_writel(vde, 0xF4000001, vde->mbe, 0x80);
	tegra_vde_writel(vde, 0x20000000, vde->mbe, 0x80);
	tegra_vde_writel(vde, 0xF4000101, vde->mbe, 0x80);

	value = 0x20000000;
	value |= ctx->chroma_qp_index_offset << 8;

	tegra_vde_writel(vde, value, vde->mbe, 0x80);

	err = tegra_vde_setup_mbe_frame_idx(vde,
					    ctx->dpb_frames_nb - 1,
					    ctx->pic_order_cnt_type == 0);
	if (err) {
		dev_err(dev, "MBE frames setup failed %d\n", err);
		return err;
	}

	tegra_vde_mbe_set_0xa_reg(vde, 0, 0x000009FC);
	tegra_vde_mbe_set_0xa_reg(vde, 2, 0x61DEAD00);
	tegra_vde_mbe_set_0xa_reg(vde, 4, 0x62DEAD00);
	tegra_vde_mbe_set_0xa_reg(vde, 6, 0x63DEAD00);
	tegra_vde_mbe_set_0xa_reg(vde, 8, dpb_frames[0].aux_addr);

	value = 0xFC000000;
	value |= !!(dpb_frames[0].flags & FLAG_B_FRAME) << 2;

	if (!ctx->baseline_profile)
		value |= !!(dpb_frames[0].flags & FLAG_REFERENCE) << 1;

	tegra_vde_writel(vde, value, vde->mbe, 0x80);

	err = tegra_vde_wait_mbe(vde);
	if (err) {
		dev_err(dev, "MBE programming failed %d\n", err);
		return err;
	}

	return 0;
}

static void tegra_vde_decode_frame(struct tegra_vde *vde,
				   unsigned int macroblocks_nb)
{
	reinit_completion(&vde->decode_completion);

	tegra_vde_writel(vde, 0x00000001, vde->bsev, 0x8C);
	tegra_vde_writel(vde, 0x20000000 | (macroblocks_nb - 1),
			 vde->sxe, 0x00);
}

static void tegra_vde_detach_and_put_dmabuf(struct dma_buf_attachment *a,
					    struct sg_table *sgt,
					    enum dma_data_direction dma_dir)
{
	struct dma_buf *dmabuf = a->dmabuf;

	dma_buf_unmap_attachment(a, sgt, dma_dir);
	dma_buf_detach(dmabuf, a);
	dma_buf_put(dmabuf);
}

static int tegra_vde_attach_dmabuf(struct device *dev,
				   int fd,
				   unsigned long offset,
				   size_t min_size,
				   size_t align_size,
				   struct dma_buf_attachment **a,
				   dma_addr_t *addr,
				   struct sg_table **s,
				   size_t *size,
				   enum dma_data_direction dma_dir)
{
	struct dma_buf_attachment *attachment;
	struct dma_buf *dmabuf;
	struct sg_table *sgt;
	int err;

	dmabuf = dma_buf_get(fd);
	if (IS_ERR(dmabuf)) {
		dev_err(dev, "Invalid dmabuf FD\n");
		return PTR_ERR(dmabuf);
	}

	if (dmabuf->size & (align_size - 1)) {
		dev_err(dev, "Unaligned dmabuf 0x%zX, should be aligned to 0x%zX\n",
			dmabuf->size, align_size);
		return -EINVAL;
	}

	if ((u64)offset + min_size > dmabuf->size) {
		dev_err(dev, "Too small dmabuf size %zu @0x%lX, should be at least %zu\n",
			dmabuf->size, offset, min_size);
		return -EINVAL;
	}

	attachment = dma_buf_attach(dmabuf, dev);
	if (IS_ERR(attachment)) {
		dev_err(dev, "Failed to attach dmabuf\n");
		err = PTR_ERR(attachment);
		goto err_put;
	}

	sgt = dma_buf_map_attachment(attachment, dma_dir);
	if (IS_ERR(sgt)) {
		dev_err(dev, "Failed to get dmabufs sg_table\n");
		err = PTR_ERR(sgt);
		goto err_detach;
	}

	if (sgt->nents != 1) {
		dev_err(dev, "Sparse DMA region is unsupported\n");
		err = -EINVAL;
		goto err_unmap;
	}

	*addr = sg_dma_address(sgt->sgl) + offset;
	*a = attachment;
	*s = sgt;

	if (size)
		*size = dmabuf->size - offset;

	return 0;

err_unmap:
	dma_buf_unmap_attachment(attachment, sgt, dma_dir);
err_detach:
	dma_buf_detach(dmabuf, attachment);
err_put:
	dma_buf_put(dmabuf);

	return err;
}

static int tegra_vde_attach_dmabufs_to_frame(struct device *dev,
					     struct video_frame *frame,
					     struct tegra_vde_h264_frame *src,
					     enum dma_data_direction dma_dir,
					     bool baseline_profile,
					     size_t lsize, size_t csize)
{
	int err;

	err = tegra_vde_attach_dmabuf(dev, src->y_fd,
				      src->y_offset, lsize, SZ_256,
				      &frame->y_dmabuf_attachment,
				      &frame->y_addr,
				      &frame->y_sgt,
				      NULL, dma_dir);
	if (err)
		return err;

	err = tegra_vde_attach_dmabuf(dev, src->cb_fd,
				      src->cb_offset, csize, SZ_256,
				      &frame->cb_dmabuf_attachment,
				      &frame->cb_addr,
				      &frame->cb_sgt,
				      NULL, dma_dir);
	if (err)
		goto err_release_y;

	err = tegra_vde_attach_dmabuf(dev, src->cr_fd,
				      src->cr_offset, csize, SZ_256,
				      &frame->cr_dmabuf_attachment,
				      &frame->cr_addr,
				      &frame->cr_sgt,
				      NULL, dma_dir);
	if (err)
		goto err_release_cb;

	if (baseline_profile) {
		frame->aux_addr = 0x64DEAD00;
		return 0;
	}

	err = tegra_vde_attach_dmabuf(dev, src->aux_fd,
				      src->aux_offset, csize, SZ_256,
				      &frame->aux_dmabuf_attachment,
				      &frame->aux_addr,
				      &frame->aux_sgt,
				      NULL, dma_dir);
	if (err)
		goto err_release_cr;

	return 0;

err_release_cr:
	tegra_vde_detach_and_put_dmabuf(frame->cr_dmabuf_attachment,
					frame->cr_sgt, dma_dir);
err_release_cb:
	tegra_vde_detach_and_put_dmabuf(frame->cb_dmabuf_attachment,
					frame->cb_sgt, dma_dir);
err_release_y:
	tegra_vde_detach_and_put_dmabuf(frame->y_dmabuf_attachment,
					frame->y_sgt, dma_dir);

	return err;
}

static void tegra_vde_release_frame_dmabufs(struct video_frame *frame,
					    enum dma_data_direction dma_dir,
					    bool baseline_profile)
{
	if (!baseline_profile)
		tegra_vde_detach_and_put_dmabuf(frame->aux_dmabuf_attachment,
						frame->aux_sgt, dma_dir);

	tegra_vde_detach_and_put_dmabuf(frame->cr_dmabuf_attachment,
					frame->cr_sgt, dma_dir);

	tegra_vde_detach_and_put_dmabuf(frame->cb_dmabuf_attachment,
					frame->cb_sgt, dma_dir);

	tegra_vde_detach_and_put_dmabuf(frame->y_dmabuf_attachment,
					frame->y_sgt, dma_dir);
}

static int tegra_vde_validate_frame(struct device *dev,
				    struct tegra_vde_h264_frame *frame)
{
	if (frame->frame_num > 0x7FFFFF) {
		dev_err(dev, "Bad frame_num %u\n", frame->frame_num);
		return -EINVAL;
	}

	return 0;
}

static int tegra_vde_validate_h264_ctx(struct device *dev,
				       struct tegra_vde_h264_decoder_ctx *ctx)
{
	if (ctx->dpb_frames_nb == 0 || ctx->dpb_frames_nb > 17) {
		dev_err(dev, "Bad DPB size %u\n", ctx->dpb_frames_nb);
		return -EINVAL;
	}

	if (ctx->level_idc > 15) {
		dev_err(dev, "Bad level value %u\n", ctx->level_idc);
		return -EINVAL;
	}

	if (ctx->pic_init_qp > 52) {
		dev_err(dev, "Bad pic_init_qp value %u\n", ctx->pic_init_qp);
		return -EINVAL;
	}

	if (ctx->log2_max_pic_order_cnt_lsb > 16) {
		dev_err(dev, "Bad log2_max_pic_order_cnt_lsb value %u\n",
			ctx->log2_max_pic_order_cnt_lsb);
		return -EINVAL;
	}

	if (ctx->log2_max_frame_num > 16) {
		dev_err(dev, "Bad log2_max_frame_num value %u\n",
			ctx->log2_max_frame_num);
		return -EINVAL;
	}

	if (ctx->chroma_qp_index_offset > 31) {
		dev_err(dev, "Bad chroma_qp_index_offset value %u\n",
			ctx->chroma_qp_index_offset);
		return -EINVAL;
	}

	if (ctx->pic_order_cnt_type > 2) {
		dev_err(dev, "Bad pic_order_cnt_type value %u\n",
			ctx->pic_order_cnt_type);
		return -EINVAL;
	}

	if (ctx->num_ref_idx_l0_active_minus1 > 15) {
		dev_err(dev, "Bad num_ref_idx_l0_active_minus1 value %u\n",
			ctx->num_ref_idx_l0_active_minus1);
		return -EINVAL;
	}

	if (ctx->num_ref_idx_l1_active_minus1 > 15) {
		dev_err(dev, "Bad num_ref_idx_l1_active_minus1 value %u\n",
			ctx->num_ref_idx_l1_active_minus1);
		return -EINVAL;
	}

	if (!ctx->pic_width_in_mbs || ctx->pic_width_in_mbs > 127) {
		dev_err(dev, "Bad pic_width_in_mbs value %u\n",
			ctx->pic_width_in_mbs);
		return -EINVAL;
	}

	if (!ctx->pic_height_in_mbs || ctx->pic_height_in_mbs > 127) {
		dev_err(dev, "Bad pic_height_in_mbs value %u\n",
			ctx->pic_height_in_mbs);
		return -EINVAL;
	}

	return 0;
}

static int tegra_vde_ioctl_decode_h264(struct tegra_vde *vde,
				       unsigned long vaddr)
{
	struct device *dev = vde->miscdev.parent;
	struct tegra_vde_h264_decoder_ctx ctx;
	struct tegra_vde_h264_frame frames[17];
	struct tegra_vde_h264_frame __user *frames_user;
	struct video_frame *dpb_frames;
	struct dma_buf_attachment *bitstream_data_dmabuf_attachment;
	struct sg_table *bitstream_sgt;
	enum dma_data_direction dma_dir;
	dma_addr_t bitstream_data_addr;
	dma_addr_t bsev_ptr;
	size_t lsize, csize;
	size_t bitstream_data_size;
	unsigned int macroblocks_nb;
	unsigned int read_bytes;
	unsigned int cstride;
	unsigned int i;
	long timeout;
	int ret, err;

	if (copy_from_user(&ctx, (void __user *)vaddr, sizeof(ctx)))
		return -EFAULT;

	ret = tegra_vde_validate_h264_ctx(dev, &ctx);
	if (ret)
		return ret;

	ret = tegra_vde_attach_dmabuf(dev, ctx.bitstream_data_fd,
				      ctx.bitstream_data_offset,
				      SZ_16K, SZ_16K,
				      &bitstream_data_dmabuf_attachment,
				      &bitstream_data_addr,
				      &bitstream_sgt,
				      &bitstream_data_size,
				      DMA_TO_DEVICE);
	if (ret)
		return ret;

	dpb_frames = kcalloc(ctx.dpb_frames_nb, sizeof(*dpb_frames),
			     GFP_KERNEL);
	if (!dpb_frames) {
		ret = -ENOMEM;
		goto release_bitstream_dmabuf;
	}

	macroblocks_nb = ctx.pic_width_in_mbs * ctx.pic_height_in_mbs;
	frames_user = u64_to_user_ptr(ctx.dpb_frames_ptr);

	if (copy_from_user(frames, frames_user,
			   ctx.dpb_frames_nb * sizeof(*frames))) {
		ret = -EFAULT;
		goto free_dpb_frames;
	}

	cstride = ALIGN(ctx.pic_width_in_mbs * 8, 16);
	csize = cstride * ctx.pic_height_in_mbs * 8;
	lsize = macroblocks_nb * 256;

	for (i = 0; i < ctx.dpb_frames_nb; i++) {
		ret = tegra_vde_validate_frame(dev, &frames[i]);
		if (ret)
			goto release_dpb_frames;

		dpb_frames[i].flags = frames[i].flags;
		dpb_frames[i].frame_num = frames[i].frame_num;

		dma_dir = (i == 0) ? DMA_FROM_DEVICE : DMA_TO_DEVICE;

		ret = tegra_vde_attach_dmabufs_to_frame(dev, &dpb_frames[i],
							&frames[i], dma_dir,
							ctx.baseline_profile,
							lsize, csize);
		if (ret)
			goto release_dpb_frames;
	}

	ret = mutex_lock_interruptible(&vde->lock);
	if (ret)
		goto release_dpb_frames;

	ret = pm_runtime_get_sync(dev);
	if (ret < 0)
		goto unlock;

	/*
	 * We rely on the VDE registers reset value, otherwise VDE
	 * causes bus lockup.
	 */
	ret = reset_control_assert(vde->rst_mc);
	if (ret) {
		dev_err(dev, "DEC start: Failed to assert MC reset: %d\n",
			ret);
		goto put_runtime_pm;
	}

	ret = reset_control_reset(vde->rst);
	if (ret) {
		dev_err(dev, "DEC start: Failed to reset HW: %d\n", ret);
		goto put_runtime_pm;
	}

	ret = reset_control_deassert(vde->rst_mc);
	if (ret) {
		dev_err(dev, "DEC start: Failed to deassert MC reset: %d\n",
			ret);
		goto put_runtime_pm;
	}

	ret = tegra_vde_setup_hw_context(vde, &ctx, dpb_frames,
					 bitstream_data_addr,
					 bitstream_data_size,
					 macroblocks_nb);
	if (ret)
		goto put_runtime_pm;

	tegra_vde_decode_frame(vde, macroblocks_nb);

	timeout = wait_for_completion_interruptible_timeout(
			&vde->decode_completion, msecs_to_jiffies(1000));
	if (timeout == 0) {
		bsev_ptr = tegra_vde_readl(vde, vde->bsev, 0x10);
		macroblocks_nb = tegra_vde_readl(vde, vde->sxe, 0xC8) & 0x1FFF;
		read_bytes = bsev_ptr ? bsev_ptr - bitstream_data_addr : 0;

		dev_err(dev, "Decoding failed: read 0x%X bytes, %u macroblocks parsed\n",
			read_bytes, macroblocks_nb);

		ret = -EIO;
	} else if (timeout < 0) {
		ret = timeout;
	}

	/*
	 * At first reset memory client to avoid resetting VDE HW in the
	 * middle of DMA which could result into memory corruption or hang
	 * the whole system.
	 */
	err = reset_control_assert(vde->rst_mc);
	if (err)
		dev_err(dev, "DEC end: Failed to assert MC reset: %d\n", err);

	err = reset_control_assert(vde->rst);
	if (err)
		dev_err(dev, "DEC end: Failed to assert HW reset: %d\n", err);

put_runtime_pm:
	pm_runtime_mark_last_busy(dev);
	pm_runtime_put_autosuspend(dev);

unlock:
	mutex_unlock(&vde->lock);

release_dpb_frames:
	while (i--) {
		dma_dir = (i == 0) ? DMA_FROM_DEVICE : DMA_TO_DEVICE;

		tegra_vde_release_frame_dmabufs(&dpb_frames[i], dma_dir,
						ctx.baseline_profile);
	}

free_dpb_frames:
	kfree(dpb_frames);

release_bitstream_dmabuf:
	tegra_vde_detach_and_put_dmabuf(bitstream_data_dmabuf_attachment,
					bitstream_sgt, DMA_TO_DEVICE);

	return ret;
}

static long tegra_vde_unlocked_ioctl(struct file *filp,
				     unsigned int cmd, unsigned long arg)
{
	struct miscdevice *miscdev = filp->private_data;
	struct tegra_vde *vde = container_of(miscdev, struct tegra_vde,
					     miscdev);

	switch (cmd) {
	case TEGRA_VDE_IOCTL_DECODE_H264:
		return tegra_vde_ioctl_decode_h264(vde, arg);
	}

	dev_err(miscdev->parent, "Invalid IOCTL command %u\n", cmd);

	return -ENOTTY;
}

static const struct file_operations tegra_vde_fops = {
	.owner		= THIS_MODULE,
	.unlocked_ioctl	= tegra_vde_unlocked_ioctl,
};

static irqreturn_t tegra_vde_isr(int irq, void *data)
{
	struct tegra_vde *vde = data;

	if (completion_done(&vde->decode_completion))
		return IRQ_NONE;

	tegra_vde_set_bits(vde, 0, vde->frameid, 0x208);
	complete(&vde->decode_completion);

	return IRQ_HANDLED;
}

static int tegra_vde_runtime_suspend(struct device *dev)
{
	struct tegra_vde *vde = dev_get_drvdata(dev);
	int err;

	err = tegra_powergate_power_off(TEGRA_POWERGATE_VDEC);
	if (err) {
		dev_err(dev, "Failed to power down HW: %d\n", err);
		return err;
	}

	clk_disable_unprepare(vde->clk);

	return 0;
}

static int tegra_vde_runtime_resume(struct device *dev)
{
	struct tegra_vde *vde = dev_get_drvdata(dev);
	int err;

	err = tegra_powergate_sequence_power_up(TEGRA_POWERGATE_VDEC,
						vde->clk, vde->rst);
	if (err) {
		dev_err(dev, "Failed to power up HW : %d\n", err);
		return err;
	}

	return 0;
}

static int tegra_vde_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct resource *regs;
	struct tegra_vde *vde;
	int irq, err;

	vde = devm_kzalloc(dev, sizeof(*vde), GFP_KERNEL);
	if (!vde)
		return -ENOMEM;

	platform_set_drvdata(pdev, vde);

	regs = platform_get_resource_byname(pdev, IORESOURCE_MEM, "sxe");
	if (!regs)
		return -ENODEV;

	vde->sxe = devm_ioremap_resource(dev, regs);
	if (IS_ERR(vde->sxe))
		return PTR_ERR(vde->sxe);

	regs = platform_get_resource_byname(pdev, IORESOURCE_MEM, "bsev");
	if (!regs)
		return -ENODEV;

	vde->bsev = devm_ioremap_resource(dev, regs);
	if (IS_ERR(vde->bsev))
		return PTR_ERR(vde->bsev);

	regs = platform_get_resource_byname(pdev, IORESOURCE_MEM, "mbe");
	if (!regs)
		return -ENODEV;

	vde->mbe = devm_ioremap_resource(dev, regs);
	if (IS_ERR(vde->mbe))
		return PTR_ERR(vde->mbe);

	regs = platform_get_resource_byname(pdev, IORESOURCE_MEM, "ppe");
	if (!regs)
		return -ENODEV;

	vde->ppe = devm_ioremap_resource(dev, regs);
	if (IS_ERR(vde->ppe))
		return PTR_ERR(vde->ppe);

	regs = platform_get_resource_byname(pdev, IORESOURCE_MEM, "mce");
	if (!regs)
		return -ENODEV;

	vde->mce = devm_ioremap_resource(dev, regs);
	if (IS_ERR(vde->mce))
		return PTR_ERR(vde->mce);

	regs = platform_get_resource_byname(pdev, IORESOURCE_MEM, "tfe");
	if (!regs)
		return -ENODEV;

	vde->tfe = devm_ioremap_resource(dev, regs);
	if (IS_ERR(vde->tfe))
		return PTR_ERR(vde->tfe);

	regs = platform_get_resource_byname(pdev, IORESOURCE_MEM, "ppb");
	if (!regs)
		return -ENODEV;

	vde->ppb = devm_ioremap_resource(dev, regs);
	if (IS_ERR(vde->ppb))
		return PTR_ERR(vde->ppb);

	regs = platform_get_resource_byname(pdev, IORESOURCE_MEM, "vdma");
	if (!regs)
		return -ENODEV;

	vde->vdma = devm_ioremap_resource(dev, regs);
	if (IS_ERR(vde->vdma))
		return PTR_ERR(vde->vdma);

	regs = platform_get_resource_byname(pdev, IORESOURCE_MEM, "frameid");
	if (!regs)
		return -ENODEV;

	vde->frameid = devm_ioremap_resource(dev, regs);
	if (IS_ERR(vde->frameid))
		return PTR_ERR(vde->frameid);

	vde->clk = devm_clk_get(dev, NULL);
	if (IS_ERR(vde->clk)) {
		err = PTR_ERR(vde->clk);
		dev_err(dev, "Could not get VDE clk %d\n", err);
		return err;
	}

	vde->rst = devm_reset_control_get(dev, NULL);
	if (IS_ERR(vde->rst)) {
		err = PTR_ERR(vde->rst);
		dev_err(dev, "Could not get VDE reset %d\n", err);
		return err;
	}

	vde->rst_mc = devm_reset_control_get_optional(dev, "mc");
	if (IS_ERR(vde->rst_mc)) {
		err = PTR_ERR(vde->rst_mc);
		dev_err(dev, "Could not get MC reset %d\n", err);
		return err;
	}

	irq = platform_get_irq_byname(pdev, "sync-token");
	if (irq < 0)
		return irq;

	err = devm_request_irq(dev, irq, tegra_vde_isr, 0,
			       dev_name(dev), vde);
	if (err) {
		dev_err(dev, "Could not request IRQ %d\n", err);
		return err;
	}

	vde->iram_pool = of_gen_pool_get(dev->of_node, "iram", 0);
	if (!vde->iram_pool) {
		dev_err(dev, "Could not get IRAM pool\n");
		return -EPROBE_DEFER;
	}

	vde->iram = gen_pool_dma_alloc(vde->iram_pool,
				       gen_pool_size(vde->iram_pool),
				       &vde->iram_lists_addr);
	if (!vde->iram) {
		dev_err(dev, "Could not reserve IRAM\n");
		return -ENOMEM;
	}

	mutex_init(&vde->lock);
	init_completion(&vde->decode_completion);

	vde->miscdev.minor = MISC_DYNAMIC_MINOR;
	vde->miscdev.name = "tegra_vde";
	vde->miscdev.fops = &tegra_vde_fops;
	vde->miscdev.parent = dev;

	err = misc_register(&vde->miscdev);
	if (err) {
		dev_err(dev, "Failed to register misc device: %d\n", err);
		goto err_gen_free;
	}

	pm_runtime_enable(dev);
	pm_runtime_use_autosuspend(dev);
	pm_runtime_set_autosuspend_delay(dev, 300);

	if (!pm_runtime_enabled(dev)) {
		err = tegra_vde_runtime_resume(dev);
		if (err)
			goto err_misc_unreg;
	}

	return 0;

err_misc_unreg:
	misc_deregister(&vde->miscdev);

err_gen_free:
	gen_pool_free(vde->iram_pool, (unsigned long)vde->iram,
		      gen_pool_size(vde->iram_pool));

	return err;
}

static int tegra_vde_remove(struct platform_device *pdev)
{
	struct tegra_vde *vde = platform_get_drvdata(pdev);
	struct device *dev = &pdev->dev;
	int err;

	if (!pm_runtime_enabled(dev)) {
		err = tegra_vde_runtime_suspend(dev);
		if (err)
			return err;
	}

	pm_runtime_dont_use_autosuspend(dev);
	pm_runtime_disable(dev);

	misc_deregister(&vde->miscdev);

	gen_pool_free(vde->iram_pool, (unsigned long)vde->iram,
		      gen_pool_size(vde->iram_pool));

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int tegra_vde_pm_suspend(struct device *dev)
{
	struct tegra_vde *vde = dev_get_drvdata(dev);
	int err;

	mutex_lock(&vde->lock);

	err = pm_runtime_force_suspend(dev);
	if (err < 0)
		return err;

	return 0;
}

static int tegra_vde_pm_resume(struct device *dev)
{
	struct tegra_vde *vde = dev_get_drvdata(dev);
	int err;

	err = pm_runtime_force_resume(dev);
	if (err < 0)
		return err;

	mutex_unlock(&vde->lock);

	return 0;
}
#endif

static const struct dev_pm_ops tegra_vde_pm_ops = {
	SET_RUNTIME_PM_OPS(tegra_vde_runtime_suspend,
			   tegra_vde_runtime_resume,
			   NULL)
	SET_SYSTEM_SLEEP_PM_OPS(tegra_vde_pm_suspend,
				tegra_vde_pm_resume)
};

static const struct of_device_id tegra_vde_of_match[] = {
	{ .compatible = "nvidia,tegra20-vde", },
	{ },
};
MODULE_DEVICE_TABLE(of, tegra_vde_of_match);

static struct platform_driver tegra_vde_driver = {
	.probe		= tegra_vde_probe,
	.remove		= tegra_vde_remove,
	.driver		= {
		.name		= "tegra-vde",
		.of_match_table = tegra_vde_of_match,
		.pm		= &tegra_vde_pm_ops,
	},
};
module_platform_driver(tegra_vde_driver);

MODULE_DESCRIPTION("NVIDIA Tegra Video Decoder driver");
MODULE_AUTHOR("Dmitry Osipenko <digetx@gmail.com>");
MODULE_LICENSE("GPL");
