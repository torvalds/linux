// SPDX-License-Identifier: GPL-2.0-only
/*
 * camss-vfe-vbif.c
 *
 * Qualcomm MSM Camera Subsystem - VFE VBIF Module
 *
 * Copyright (c) 2025, The Linux Foundation. All rights reserved.
 *
 */

#include <linux/io.h>

#include "camss.h"
#include "camss-vfe.h"
#include "camss-vfe-vbif.h"

#define VBIF_FIXED_SORT_EN	0x30
#define VBIF_FIXED_SORT_SEL0	0x34

void vfe_vbif_write_reg(struct vfe_device *vfe, u32 reg, u32 val)
{
	writel_relaxed(val, vfe->vbif_base + reg);
}

int vfe_vbif_apply_settings(struct vfe_device *vfe)
{
	vfe_vbif_write_reg(vfe, VBIF_FIXED_SORT_EN, 0xfff);
	vfe_vbif_write_reg(vfe, VBIF_FIXED_SORT_SEL0, 0x555000);

	return 0;
}
