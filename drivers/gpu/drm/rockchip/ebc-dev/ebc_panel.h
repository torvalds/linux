// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2020 Rockchip Electronics Co. Ltd.
 *
 * Author: Zorro Liu <zorro.liu@rock-chips.com>
 */

#ifndef _EBC_PANEL_H_
#define _EBC_PANEL_H_

#include <linux/dma-mapping.h>

#define DIRECT_FB_NUM	2

struct panel_buffer {
	void *virt_addr;
	unsigned long phy_addr;
	size_t size;
};

struct ebc_panel {
	struct device *dev;
	struct ebc_tcon *tcon;
	struct ebc_pmic *pmic;
	struct panel_buffer fb[DIRECT_FB_NUM]; //for direct mode, one pixel 2bit
	int current_buffer;

	u32 width;
	u32 height;
	u32 vir_width;
	u32 vir_height;
	u32 width_mm;
	u32 height_mm;
	u32 direct_mode;
	u32 sdck;
	u32 lsl;
	u32 lbl;
	u32 ldl;
	u32 lel;
	u32 gdck_sta;
	u32 lgonl;
	u32 fsl;
	u32 fbl;
	u32 fdl;
	u32 fel;
	u32 panel_16bit;
	u32 panel_color;
	u32 mirror;
};
#endif
