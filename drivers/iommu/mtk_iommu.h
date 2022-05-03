/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2015-2016 MediaTek Inc.
 * Author: Honghui Zhang <honghui.zhang@mediatek.com>
 */

#ifndef _MTK_IOMMU_H_
#define _MTK_IOMMU_H_

#include <linux/device.h>
#include <linux/io.h>
#include <linux/io-pgtable.h>
#include <linux/iommu.h>
#include <linux/spinlock.h>
#include <soc/mediatek/smi.h>
#include <dt-bindings/memory/mtk-memory-port.h>

struct mtk_iommu_suspend_reg {
	union {
		u32			standard_axi_mode;/* v1 */
		u32			misc_ctrl;/* v2 */
	};
	u32				dcm_dis;
	u32				ctrl_reg;
	u32				int_control0;
	u32				int_main_control;
	u32				ivrp_paddr;
	u32				vld_pa_rng;
	u32				wr_len_ctrl;
};

#endif
