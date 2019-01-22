// SPDX-License-Identifier: GPL-2.0
/*
 * (C) COPYRIGHT 2018 ARM Limited. All rights reserved.
 * Author: James.Qian.Wang <james.qian.wang@arm.com>
 *
 */

#include <drm/drm_print.h>
#include "d71_dev.h"
#include "komeda_kms.h"
#include "malidp_io.h"

static int d71_layer_init(struct d71_dev *d71,
			  struct block_header *blk, u32 __iomem *reg)
{
	DRM_DEBUG("Detect D71_Layer.\n");

	return 0;
}

static int d71_wb_layer_init(struct d71_dev *d71,
			     struct block_header *blk, u32 __iomem *reg)
{
	DRM_DEBUG("Detect D71_Wb_Layer.\n");

	return 0;
}

static int d71_compiz_init(struct d71_dev *d71,
			   struct block_header *blk, u32 __iomem *reg)
{
	DRM_DEBUG("Detect D71_compiz.\n");

	return 0;
}

static int d71_improc_init(struct d71_dev *d71,
			   struct block_header *blk, u32 __iomem *reg)
{
	DRM_DEBUG("Detect D71_improc.\n");

	return 0;
}

static int d71_timing_ctrlr_init(struct d71_dev *d71,
				 struct block_header *blk, u32 __iomem *reg)
{
	DRM_DEBUG("Detect D71_timing_ctrlr.\n");

	return 0;
}

int d71_probe_block(struct d71_dev *d71,
		    struct block_header *blk, u32 __iomem *reg)
{
	struct d71_pipeline *pipe;
	int blk_id = BLOCK_INFO_BLK_ID(blk->block_info);

	int err = 0;

	switch (BLOCK_INFO_BLK_TYPE(blk->block_info)) {
	case D71_BLK_TYPE_GCU:
		break;

	case D71_BLK_TYPE_LPU:
		pipe = d71->pipes[blk_id];
		pipe->lpu_addr = reg;
		break;

	case D71_BLK_TYPE_LPU_LAYER:
		err = d71_layer_init(d71, blk, reg);
		break;

	case D71_BLK_TYPE_LPU_WB_LAYER:
		err = d71_wb_layer_init(d71, blk, reg);
		break;

	case D71_BLK_TYPE_CU:
		pipe = d71->pipes[blk_id];
		pipe->cu_addr = reg;
		err = d71_compiz_init(d71, blk, reg);
		break;

	case D71_BLK_TYPE_CU_SPLITTER:
	case D71_BLK_TYPE_CU_SCALER:
	case D71_BLK_TYPE_CU_MERGER:
		break;

	case D71_BLK_TYPE_DOU:
		pipe = d71->pipes[blk_id];
		pipe->dou_addr = reg;
		break;

	case D71_BLK_TYPE_DOU_IPS:
		err = d71_improc_init(d71, blk, reg);
		break;

	case D71_BLK_TYPE_DOU_FT_COEFF:
		pipe = d71->pipes[blk_id];
		pipe->dou_ft_coeff_addr = reg;
		break;

	case D71_BLK_TYPE_DOU_BS:
		err = d71_timing_ctrlr_init(d71, blk, reg);
		break;

	case D71_BLK_TYPE_GLB_LT_COEFF:
		break;

	case D71_BLK_TYPE_GLB_SCL_COEFF:
		d71->glb_scl_coeff_addr[blk_id] = reg;
		break;

	default:
		DRM_ERROR("Unknown block (block_info: 0x%x) is found\n",
			  blk->block_info);
		err = -EINVAL;
		break;
	}

	return err;
}
