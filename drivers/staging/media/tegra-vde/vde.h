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

struct clk;
struct dma_buf;
struct gen_pool;
struct iommu_group;
struct iommu_domain;
struct reset_control;
struct dma_buf_attachment;

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
	dma_addr_t iram_lists_addr;
	u32 *iram;
};

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
