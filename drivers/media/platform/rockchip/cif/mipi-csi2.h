/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2020 Rockchip Electronics Co., Ltd. */
#ifndef _RKCIF_MIPI_CSI2_H_
#define _RKCIF_MIPI_CSI2_H_

#include <linux/notifier.h>

#define CSI2_ERR_FSFE_MASK	(0xff << 8)
#define CSI2_ERR_COUNT_ALL_MASK	(0xff)

u32 rkcif_csi2_get_sof(void);
void rkcif_csi2_set_sof(u32 seq);
void rkcif_csi2_event_inc_sof(void);
int __init rkcif_csi2_plat_drv_init(void);
void __exit rkcif_csi2_plat_drv_exit(void);
int rkcif_csi2_register_notifier(struct notifier_block *nb);
int rkcif_csi2_unregister_notifier(struct notifier_block *nb);

#endif
