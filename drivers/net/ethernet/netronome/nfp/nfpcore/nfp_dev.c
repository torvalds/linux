// SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause)
/* Copyright (C) 2019 Netronome Systems, Inc. */

#include <linux/dma-mapping.h>
#include <linux/kernel.h>
#include <linux/sizes.h>

#include "nfp_dev.h"

const struct nfp_dev_info nfp_dev_info[NFP_DEV_CNT] = {
	[NFP_DEV_NFP3800] = {
		.dma_mask		= DMA_BIT_MASK(48),
		.qc_idx_mask		= GENMASK(8, 0),
		.qc_addr_offset		= 0x400000,
		.min_qc_size		= 512,
		.max_qc_size		= SZ_64K,

		.chip_names		= "NFP3800",
		.pcie_cfg_expbar_offset	= 0x0a00,
		.pcie_expl_offset	= 0xd000,
		.qc_area_sz		= 0x100000,
	},
	[NFP_DEV_NFP3800_VF] = {
		.dma_mask		= DMA_BIT_MASK(48),
		.qc_idx_mask		= GENMASK(8, 0),
		.qc_addr_offset		= 0,
		.min_qc_size		= 512,
		.max_qc_size		= SZ_64K,
	},
	[NFP_DEV_NFP6000] = {
		.dma_mask		= DMA_BIT_MASK(40),
		.qc_idx_mask		= GENMASK(7, 0),
		.qc_addr_offset		= 0x80000,
		.min_qc_size		= 256,
		.max_qc_size		= SZ_256K,

		.chip_names		= "NFP4000/NFP5000/NFP6000",
		.pcie_cfg_expbar_offset	= 0x0400,
		.pcie_expl_offset	= 0x1000,
		.qc_area_sz		= 0x80000,
	},
	[NFP_DEV_NFP6000_VF] = {
		.dma_mask		= DMA_BIT_MASK(40),
		.qc_idx_mask		= GENMASK(7, 0),
		.qc_addr_offset		= 0,
		.min_qc_size		= 256,
		.max_qc_size		= SZ_256K,
	},
};
