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

static const struct v4l2_isp_stats_block_type_info
rppx1_stats_blocks_info[] = {
	RPPX1_STATS_BLOCK_INFO(HIST_POST, hist),
	RPPX1_STATS_BLOCK_INFO(EXM_PRE1, exm),
	RPPX1_STATS_BLOCK_INFO(WBMEAS_POST, wbmeas),
};

#define rppx1_init_stats_block(rpp, buf, type)				\
	((union rppx1_stats_block *)					\
	v4l2_isp_stats_init_block((rpp)->dev, (buf),			\
				  rppx1_stats_blocks_info,		\
				  ARRAY_SIZE(rppx1_stats_blocks_info),	\
				  (type), RPPX1_STATS_MAX_SIZE))	\

void rppx1_stats_fill_isr(struct rppx1 *rpp, u32 isc, void *buf)
{
	struct v4l2_isp_buffer *stats = buf;
	union rppx1_stats_block *block;

	v4l2_isp_stats_init_buffer(stats, V4L2_ISP_VERSION_V1);

	if (isc & RPPX1_IRQ_ID_POST_HIST_MEAS) {
		block = rppx1_init_stats_block(rpp, stats,
					       RPPX1_STATS_BLOCK_TYPE_HIST_POST);
		if (IS_ERR(block))
			return;

		rpp_module_call(&rpp->post.hist, fill_stats, block);
	}

	if (isc & RPPX1_IRQ_ID_PRE1_EXM) {
		block = rppx1_init_stats_block(rpp, stats,
					       RPPX1_STATS_BLOCK_TYPE_EXM_PRE1);
		if (IS_ERR(block))
			return;

		rpp_module_call(&rpp->pre1.exm, fill_stats, block);
	}

	if (isc & RPPX1_IRQ_ID_POST_AWB_MEAS) {
		block = rppx1_init_stats_block(rpp, stats,
					       RPPX1_STATS_BLOCK_TYPE_WBMEAS_POST);
		if (IS_ERR(block))
			return;

		rpp_module_call(&rpp->post.wbmeas, fill_stats, block);
	}
}
EXPORT_SYMBOL_GPL(rppx1_stats_fill_isr);
