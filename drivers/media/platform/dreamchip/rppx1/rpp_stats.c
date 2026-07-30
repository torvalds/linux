// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2026 Renesas Electronics Corp.
 * Copyright (C) 2026 Ideas on Board Oy
 * Copyright (C) 2026 Ragnatech AB
 */

#include "rppx1.h"
#include "rpp_module.h"

#include <media/v4l2-isp.h>

#define RPPX1_STATS_BLOCK_INFO(type, block) \
	[RPPX1_STATS_BLOCK_TYPE_ ## type] = { \
		.size = sizeof(struct rppx1_ ## block ## _stats), \
	}

#define rppx1_init_stats_block(rpp, buf, type)				\
	((union rppx1_stats_block *)					\
	v4l2_isp_stats_init_block((rpp)->dev, (buf),			\
				  rppx1_stats_blocks_info,		\
				  ARRAY_SIZE(rppx1_stats_blocks_info),	\
				  (type), RPPX1_STATS_MAX_SIZE))	\

void rppx1_stats_fill_isr(struct rppx1 *rpp, u32 isc, void *buf)
{
}
EXPORT_SYMBOL_GPL(rppx1_stats_fill_isr);
