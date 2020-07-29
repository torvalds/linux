/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2020 Rockchip Electronics Co., Ltd. */
#ifndef _RKCIF_MIPI_CSI2_H_
#define _RKCIF_MIPI_CSI2_H_

u32 rkcif_csi2_get_sof(void);
void rkcif_csi2_event_inc_sof(void);
int __init rkcif_csi2_plat_drv_init(void);

#endif
