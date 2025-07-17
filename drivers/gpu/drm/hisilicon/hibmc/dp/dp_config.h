/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (c) 2024 Hisilicon Limited. */

#ifndef DP_CONFIG_H
#define DP_CONFIG_H

#define HIBMC_DP_BPP			24
#define HIBMC_DP_SYMBOL_PER_FCLK	4
#define HIBMC_DP_MSA1			0x20
#define HIBMC_DP_MSA2			0x845c00
#define HIBMC_DP_OFFSET			0x1e0000
#define HIBMC_DP_HDCP			0x2
#define HIBMC_DP_INT_RST		0xffff
#define HIBMC_DP_DPTX_RST		0x3ff
#define HIBMC_DP_CLK_EN			0x7
#define HIBMC_DP_SYNC_EN_MASK		0x3
#define HIBMC_DP_LINK_RATE_CAL		27
#define HIBMC_DP_SYNC_DELAY(lanes)	((lanes) == 0x2 ? 86 : 46)
#define HIBMC_DP_INT_ENABLE		0xc

#endif
