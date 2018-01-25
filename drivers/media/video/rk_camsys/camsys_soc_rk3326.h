/*
 *************************************************************************
 * Copyright (C) 2018 Fuzhou Rockchip Electronics Co., Ltd.
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *************************************************************************
 */

#ifndef __RKCAMSYS_SOC_RK3326_H__
#define __RKCAMSYS_SOC_RK3326_H__

#include "camsys_internal.h"

/* MARVIN REGISTER */
#define MRV_MIPI_BASE                            0x1C00
#define MRV_MIPI_CTRL                            0x00

#define GRF_IO_VSEL_OFFSET                       (0x0180)
#define GRF_IO_VSEL_VCCIO3_MASK                  (0x1 << 20)
#define GRF_IO_VSEL_VCCIO3_BITS                  (4)
#define GRF_PD_VI_CON_OFFSET                     (0x0430)
/* bit 13-14 */
#define ISP_CIF_IF_DATAWIDTH_MASK                (0x3 << 29)
#define ISP_CIF_IF_DATAWIDTH_8B                  (0x0 << 13)
#define ISP_CIF_IF_DATAWIDTH_10B                 (0x1 << 13)
#define ISP_CIF_IF_DATAWIDTH_12B                 (0x2 << 13)

/* bit 9 */
#define DPHY_CSIPHY_CLK_INV_SEL_MASK              (0x1 << 25)
#define DPHY_CSIPHY_CLK_INV_SEL                   (0x1 << 9)
/* bit 8 */
#define DPHY_CSIPHY_CLKLANE_EN_OFFSET_MASK        (0x1 << 24)//????
#define DPHY_CSIPHY_CLKLANE_EN_OFFSET_BITS        (8)
/* bit 4-7 */
#define DPHY_CSIPHY_DATALANE_EN_OFFSET_MASK       (0xF << 20)//?????
#define DPHY_CSIPHY_DATALANE_EN_OFFSET_BITS       (4)
/* bit 0-3 */
#define DPHY_CSIPHY_FORCERXMODE_OFFSET_MASK       (0xF << 16)
#define DPHY_CSIPHY_FORCERXMODE_OFFSET_BITS       (0)

/* LOW POWER MODE SET */
/* base */
#define MIPI_CSI_DPHY_CTRL_LANE_ENABLE_OFFSET      (0x00)
#define MIPI_CSI_DPHY_CTRL_DATALANE_ENABLE_OFFSET_BIT  (2)
#define MIPI_CSI_DPHY_CTRL_CLKLANE_ENABLE_OFFSET_BIT   (6)
#define MIPI_CSI_DPHY_CTRL_PWRCTL_OFFSET           (0x04)
#define MIPI_CSI_DPHY_CTRL_DIG_RST_OFFSET          (0x80)
#define MIPI_CSI_DPHY_CTRL_SIG_INV_OFFSET          (0x84)

/* Configure the count time of the THS-SETTLE by protocol. */
#define MIPI_CSI_DPHY_LANEX_THS_SETTLE_OFFSET      (0x00)
/* MSB enable for pin_rxdatahs_
 * 1: enable
 * 0: disable
 */
#define MIPI_CSI_DPHY_LANEX_MSB_EN_OFFSET          (0x38)

#define write_grf_reg(addr, val)           \
	__raw_writel(val, (void *)(addr + para->camsys_dev->rk_grf_base))
#define read_grf_reg(addr)                 \
	__raw_readl((void *)(addr + para->camsys_dev->rk_grf_base))
#define mask_grf_reg(addr, msk, val)       \
	write_grf_reg(addr, (val) | ((~(msk)) & read_grf_reg(addr)))

#define write_cru_reg(addr, val)           \
	__raw_writel(val, (void *)(addr + para->camsys_dev->rk_cru_base))
/* csi phy */
#define write_csiphy_reg(addr, val)       \
	__raw_writel(val, (void *)(addr + csiphy_virt))
#define read_csiphy_reg(addr)             \
	__raw_readl((void *)(addr + csiphy_virt))

#endif
