/* SPDX-License-Identifier: GPL-2.0 */
/*
 * (C) COPYRIGHT 2018 ARM Limited. All rights reserved.
 * Author: James.Qian.Wang <james.qian.wang@arm.com>
 *
 */
#ifndef _D71_DEV_H_
#define _D71_DEV_H_

#include "komeda_dev.h"
#include "komeda_pipeline.h"
#include "d71_regs.h"

struct d71_pipeline {
	struct komeda_pipeline base;

	/* d71 private pipeline blocks */
	u32 __iomem	*lpu_addr;
	u32 __iomem	*cu_addr;
	u32 __iomem	*dou_addr;
	u32 __iomem	*dou_ft_coeff_addr; /* forward transform coeffs table */
};

struct d71_dev {
	struct komeda_dev *mdev;

	int	num_blocks;
	int	num_pipelines;
	int	num_rich_layers;
	u32	max_line_size;
	u32	max_vsize;
	u32	supports_dual_link : 1;
	u32	integrates_tbu : 1;

	/* global register blocks */
	u32 __iomem	*gcu_addr;
	/* scaling coeffs table */
	u32 __iomem	*glb_scl_coeff_addr[D71_MAX_GLB_SCL_COEFF];
	u32 __iomem	*periph_addr;

	struct d71_pipeline *pipes[D71_MAX_PIPELINE];
};

#define to_d71_pipeline(x)	container_of(x, struct d71_pipeline, base)

int d71_probe_block(struct d71_dev *d71,
		    struct block_header *blk, u32 __iomem *reg);
void d71_read_block_header(u32 __iomem *reg, struct block_header *blk);

#endif /* !_D71_DEV_H_ */
