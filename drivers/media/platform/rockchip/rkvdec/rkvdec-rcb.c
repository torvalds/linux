// SPDX-License-Identifier: GPL-2.0
/*
 * Rockchip video decoder Rows and Cols Buffers manager
 *
 * Copyright (C) 2025 Collabora, Ltd.
 *  Detlev Casanova <detlev.casanova@collabora.com>
 */

#include "rkvdec.h"
#include "rkvdec-rcb.h"

#include <linux/iommu.h>
#include <linux/genalloc.h>
#include <linux/sizes.h>
#include <linux/types.h>

struct rkvdec_rcb_config {
	struct rkvdec_aux_buf *rcb_bufs;
	size_t rcb_count;
};

static size_t rkvdec_rcb_size(const struct rcb_size_info *size_info,
			      unsigned int width, unsigned int height)
{
	return size_info->multiplier * (size_info->axis == PIC_HEIGHT ? height : width);
}

dma_addr_t rkvdec_rcb_buf_dma_addr(struct rkvdec_ctx *ctx, int id)
{
	return ctx->rcb_config->rcb_bufs[id].dma;
}

size_t rkvdec_rcb_buf_size(struct rkvdec_ctx *ctx, int id)
{
	return ctx->rcb_config->rcb_bufs[id].size;
}

int rkvdec_rcb_buf_count(struct rkvdec_ctx *ctx)
{
	return ctx->rcb_config->rcb_count;
}

void rkvdec_free_rcb(struct rkvdec_ctx *ctx)
{
	struct rkvdec_dev *dev = ctx->dev;
	struct rkvdec_rcb_config *cfg = ctx->rcb_config;
	unsigned long virt_addr;
	int i;

	if (!cfg)
		return;

	for (i = 0; i < cfg->rcb_count; i++) {
		size_t rcb_size = cfg->rcb_bufs[i].size;

		if (!cfg->rcb_bufs[i].cpu)
			continue;

		switch (cfg->rcb_bufs[i].type) {
		case RKVDEC_ALLOC_SRAM:
			virt_addr = (unsigned long)cfg->rcb_bufs[i].cpu;

			if (dev->iommu_domain)
				iommu_unmap(dev->iommu_domain, virt_addr, rcb_size);
			gen_pool_free(dev->sram_pool, virt_addr, rcb_size);
			break;
		case RKVDEC_ALLOC_DMA:
			dma_free_coherent(dev->dev,
					  rcb_size,
					  cfg->rcb_bufs[i].cpu,
					  cfg->rcb_bufs[i].dma);
			break;
		}
	}

	if (cfg->rcb_bufs)
		devm_kfree(dev->dev, cfg->rcb_bufs);

	devm_kfree(dev->dev, cfg);
}

int rkvdec_allocate_rcb(struct rkvdec_ctx *ctx,
			const struct rcb_size_info *size_info,
			size_t rcb_count)
{
	int ret, i;
	u32 width, height;
	struct rkvdec_dev *rkvdec = ctx->dev;
	struct rkvdec_rcb_config *cfg;

	if (!size_info || !rcb_count) {
		ctx->rcb_config = NULL;
		return 0;
	}

	ctx->rcb_config = devm_kzalloc(rkvdec->dev, sizeof(*ctx->rcb_config), GFP_KERNEL);
	if (!ctx->rcb_config)
		return -ENOMEM;

	cfg = ctx->rcb_config;

	cfg->rcb_bufs = devm_kzalloc(rkvdec->dev, sizeof(*cfg->rcb_bufs) * rcb_count, GFP_KERNEL);
	if (!cfg->rcb_bufs) {
		ret = -ENOMEM;
		goto err_alloc;
	}

	width = ctx->decoded_fmt.fmt.pix_mp.width;
	height = ctx->decoded_fmt.fmt.pix_mp.height;

	for (i = 0; i < rcb_count; i++) {
		void *cpu = NULL;
		dma_addr_t dma;
		size_t rcb_size = rkvdec_rcb_size(&size_info[i], width, height);
		enum rkvdec_alloc_type alloc_type = RKVDEC_ALLOC_SRAM;

		/* Try allocating an SRAM buffer */
		if (ctx->dev->sram_pool) {
			if (rkvdec->iommu_domain)
				rcb_size = ALIGN(rcb_size, SZ_4K);

			cpu = gen_pool_dma_zalloc_align(ctx->dev->sram_pool,
							rcb_size,
							&dma,
							SZ_4K);
		}

		/* If an IOMMU is used, map the SRAM address through it */
		if (cpu && rkvdec->iommu_domain) {
			unsigned long virt_addr = (unsigned long)cpu;
			phys_addr_t phys_addr = dma;

			ret = iommu_map(rkvdec->iommu_domain, virt_addr, phys_addr,
					rcb_size, IOMMU_READ | IOMMU_WRITE, 0);
			if (ret) {
				gen_pool_free(ctx->dev->sram_pool,
					      (unsigned long)cpu,
					      rcb_size);
				cpu = NULL;
				goto ram_fallback;
			}

			/*
			 * The registers will be configured with the virtual
			 * address so that it goes through the IOMMU
			 */
			dma = virt_addr;
		}

ram_fallback:
		/* Fallback to RAM */
		if (!cpu) {
			cpu = dma_alloc_coherent(ctx->dev->dev,
						 rcb_size,
						 &dma,
						 GFP_KERNEL);
			alloc_type = RKVDEC_ALLOC_DMA;
		}

		if (!cpu) {
			ret = -ENOMEM;
			goto err_alloc;
		}

		cfg->rcb_bufs[i].cpu = cpu;
		cfg->rcb_bufs[i].dma = dma;
		cfg->rcb_bufs[i].size = rcb_size;
		cfg->rcb_bufs[i].type = alloc_type;

		cfg->rcb_count += 1;
	}

	return 0;

err_alloc:
	rkvdec_free_rcb(ctx);

	return ret;
}
