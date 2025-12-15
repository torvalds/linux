/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * camss-vfe-vbif.h
 *
 * Qualcomm MSM Camera Subsystem - VFE VBIF Module
 *
 * Copyright (c) 2025, The Linux Foundation. All rights reserved.
 *
 */
#ifndef QC_MSM_CAMSS_VFE_VBIF_H
#define QC_MSM_CAMSS_VFE_VBIF_H

#include "camss-vfe.h"

void vfe_vbif_write_reg(struct vfe_device *vfe, u32 reg, u32 val);

int vfe_vbif_apply_settings(struct vfe_device *vfe);

#endif /* QC_MSM_CAMSS_VFE_VBIF_H */
