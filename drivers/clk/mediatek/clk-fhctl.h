/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022 MediaTek Inc.
 * Author: Edward-JW Yang <edward-jw.yang@mediatek.com>
 */

#ifndef __CLK_FHCTL_H
#define __CLK_FHCTL_H

#include "clk-pllfh.h"

enum fhctl_variant {
	FHCTL_PLLFH_V1,
	FHCTL_PLLFH_V2,
};

struct fhctl_offset {
	u32 offset_hp_en;
	u32 offset_clk_con;
	u32 offset_rst_con;
	u32 offset_slope0;
	u32 offset_slope1;
	u32 offset_cfg;
	u32 offset_updnlmt;
	u32 offset_dds;
	u32 offset_dvfs;
	u32 offset_mon;
};
const struct fhctl_offset *fhctl_get_offset_table(enum fhctl_variant v);
const struct fh_operation *fhctl_get_ops(void);
void fhctl_hw_init(struct mtk_fh *fh);

#endif
