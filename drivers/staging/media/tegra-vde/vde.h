/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * NVIDIA Tegra Video decoder driver
 *
 * Copyright (C) 2016-2019 GRATE-DRIVER project
 */

#ifndef TEGRA_VDE_H
#define TEGRA_VDE_H

#include <linux/completion.h>
#include <linux/dma-direction.h>
#include <linux/iova.h>
#include <linux/list.h>
#include <linux/miscdevice.h>
#include <linux/mutex.h>
#include <linux/types.h>

#define ICMDQUE_WR		0x00
#define CMDQUE_CONTROL		0x08
#define INTR_STATUS		0x18
#define BSE_INT_ENB		0x40
#define BSE_CONFIG		0x44

#define BSE_ICMDQUE_EMPTY	BIT(3)
#define BSE_DMA_BUSY		BIT(23)

struct clk;
struct dma_buf;
struct gen_pool;
struct iommu_group;
struct iommu_domain;
struct reset_control;
struct dma_buf_attachment;
struct tegra_vde_h264_frame;
struct tegra_vde_h264_decoder_ctx;

struct tegra_video_frame {
	struct dma_buf_attachment *y_dmabuf_attachment;
	struct dma_buf_attachment *cb_dmabuf_attachment;
	struct dma_buf_attachment *cr_dmabuf_attachment;
	struct dma_buf_attachment *aux_dmabuf_attachment;
	dma_addr_t y_addr;
	dma_addr_t cb_addr;
	dma_addr_t cr_addr;
	dma_addr_t aux_addr;
	u32 frame_num;
	u32 flags;
};

struct tegra_vde_soc {
	bool supports_ref_pic_marking;
};

struct tegra_vde_bo {
	struct iova *iova;
	struct sg_table sgt;
	struct tegra_vde *vde;
	enum dma_data_direction dma_dir;
	unsigned long dma_attrs;
	dma_addr_t dma_handle;
	dma_addr_t dma_addr;
	void *dma_cookie;
	size_t size;
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
	struct device *dev;
	struct mutex lock;
	struct mutex map_lock;
	struct list_head map_list;
	struct miscdevice miscdev;
	struct reset_control *rst;
	struct reset_control *rst_mc;
	struct gen_pool *iram_pool;
	struct completion decode_completion;
	struct clk *clk;
	struct iommu_domain *domain;
	struct iommu_group *group;
	struct iova_domain iova;
	struct iova *iova_resv_static_addresses;
	struct iova *iova_resv_last_page;
	const struct tegra_vde_soc *soc;
	struct tegra_vde_bo *secure_bo;
	dma_addr_t bitstream_data_addr;
	dma_addr_t iram_lists_addr;
	u32 *iram;
};

void tegra_vde_writel(struct tegra_vde *vde, u32 value, void __iomem *base,
		      u32 offset);
u32 tegra_vde_readl(struct tegra_vde *vde, void __iomem *base, u32 offset);
void tegra_vde_set_bits(struct tegra_vde *vde, u32 mask, void __iomem *base,
			u32 offset);

int tegra_vde_validate_h264_frame(struct device *dev,
				  struct tegra_vde_h264_frame *frame);
int tegra_vde_validate_h264_ctx(struct device *dev,
				struct tegra_vde_h264_decoder_ctx *ctx);
int tegra_vde_decode_h264(struct tegra_vde *vde,
			  struct tegra_vde_h264_decoder_ctx *ctx,
			  struct tegra_video_frame *dpb_frames,
			  dma_addr_t bitstream_data_addr,
			  size_t bitstream_data_size);

int tegra_vde_iommu_init(struct tegra_vde *vde);
void tegra_vde_iommu_deinit(struct tegra_vde *vde);
int tegra_vde_iommu_map(struct tegra_vde *vde,
			struct sg_table *sgt,
			struct iova **iovap,
			size_t size);
void tegra_vde_iommu_unmap(struct tegra_vde *vde, struct iova *iova);

int tegra_vde_dmabuf_cache_map(struct tegra_vde *vde,
			       struct dma_buf *dmabuf,
			       enum dma_data_direction dma_dir,
			       struct dma_buf_attachment **ap,
			       dma_addr_t *addrp);
void tegra_vde_dmabuf_cache_unmap(struct tegra_vde *vde,
				  struct dma_buf_attachment *a,
				  bool release);
void tegra_vde_dmabuf_cache_unmap_sync(struct tegra_vde *vde);
void tegra_vde_dmabuf_cache_unmap_all(struct tegra_vde *vde);

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

#endif /* TEGRA_VDE_H */
