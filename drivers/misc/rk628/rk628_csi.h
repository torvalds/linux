/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2020 Rockchip Electronics Co. Ltd.
 *
 * Author: Chen Shunqing <csq@rock-chips.com>
 */

#ifndef RK628_CSI_H
#define RK628_CSI_H

#include "rk628.h"

#define CSI_REG(x)			((x) + 0x40000)

#define CSITX_CONFIG_DONE		CSI_REG(0x0000)
#define CONFIG_DONE_IMD			BIT(4)
#define CONFIG_DONE			BIT(0)
#define CSITX_CSITX_EN			CSI_REG(0x0004)
#define VOP_YU_SWAP_MASK		BIT(14)
#define VOP_YU_SWAP(x)			UPDATE(x, 14, 14)
#define VOP_UV_SWAP_MASK		BIT(13)
#define VOP_UV_SWAP(x)			UPDATE(x, 13, 13)
#define VOP_YUV422_EN_MASK		BIT(12)
#define VOP_YUV422_EN(x)		UPDATE(x, 12, 12)
#define VOP_P2_EN_MASK			BIT(8)
#define VOP_P2_EN(x)			UPDATE(x, 8, 8)
#define LANE_NUM_MASK			GENMASK(5, 4)
#define LANE_NUM(x)			UPDATE(x, 5, 4)
#define DPHY_EN_MASK			BIT(2)
#define DPHY_EN(x)			UPDATE(x, 2, 2)
#define CSITX_EN_MASK			BIT(0)
#define CSITX_EN(x)			UPDATE(x, 0, 0)
#define CSITX_CSITX_VERSION		CSI_REG(0x0008)
#define CSITX_SYS_CTRL0_IMD		CSI_REG(0x0010)
#define CSITX_SYS_CTRL1			CSI_REG(0x0014)
#define BYPASS_SELECT_MASK		BIT(0)
#define BYPASS_SELECT(x)		UPDATE(x, 0, 0)
#define CSITX_SYS_CTRL2			CSI_REG(0x0018)
#define VOP_WHOLE_FRM_EN		BIT(5)
#define VSYNC_ENABLE			BIT(0)
#define CSITX_SYS_CTRL3_IMD		CSI_REG(0x001c)
#define CONT_MODE_CLK_CLR_MASK		BIT(8)
#define CONT_MODE_CLK_CLR(x)		UPDATE(x, 8, 8)
#define CONT_MODE_CLK_SET_MASK		BIT(4)
#define CONT_MODE_CLK_SET(x)		UPDATE(x, 4, 4)
#define NON_CONTINOUS_MODE_MASK		BIT(0)
#define NON_CONTINOUS_MODE(x)		UPDATE(x, 0, 0)
#define CSITX_TIMING_HPW_PADDING_NUM	CSI_REG(0x0030)
#define CSITX_VOP_PATH_CTRL		CSI_REG(0x0040)
#define VOP_WC_USERDEFINE_MASK		GENMASK(31, 16)
#define VOP_WC_USERDEFINE(x)		UPDATE(x, 31, 16)
#define VOP_DT_USERDEFINE_MASK		GENMASK(13, 8)
#define VOP_DT_USERDEFINE(x)		UPDATE(x, 13, 8)
#define VOP_PIXEL_FORMAT_MASK		GENMASK(7, 4)
#define VOP_PIXEL_FORMAT(x)		UPDATE(x, 7, 4)
#define VOP_WC_USERDEFINE_EN_MASK	BIT(3)
#define VOP_WC_USERDEFINE_EN(x)		UPDATE(x, 3, 3)
#define VOP_DT_USERDEFINE_EN_MASK	BIT(1)
#define VOP_DT_USERDEFINE_EN(x)		UPDATE(x, 1, 1)
#define VOP_PATH_EN_MASK		BIT(0)
#define VOP_PATH_EN(x)			UPDATE(x, 0, 0)
#define CSITX_VOP_PATH_PKT_CTRL		CSI_REG(0x0050)
#define CSITX_CSITX_STATUS0		CSI_REG(0x0070)
#define CSITX_CSITX_STATUS1		CSI_REG(0x0074)
#define STOPSTATE_LANE3			BIT(7)
#define STOPSTATE_LANE2			BIT(6)
#define STOPSTATE_LANE1			BIT(5)
#define STOPSTATE_LANE0			BIT(4)
#define STOPSTATE_CLK			BIT(1)
#define DPHY_PLL_LOCK			BIT(0)
#define CSITX_ERR_INTR_EN_IMD		CSI_REG(0x0090)
#define CSITX_ERR_INTR_CLR_IMD		CSI_REG(0x0094)
#define CSITX_ERR_INTR_STATUS_IMD	CSI_REG(0x0098)
#define CSITX_ERR_INTR_RAW_STATUS_IMD	CSI_REG(0x009c)
#define CSITX_LPDT_DATA_IMD		CSI_REG(0x00a8)
#define CSITX_DPHY_CTRL			CSI_REG(0x00b0)
#define CSI_DPHY_EN_MASK		GENMASK(7, 3)
#define CSI_DPHY_EN(x)			UPDATE(x, 7, 3)
#define DPHY_ENABLECLK			BIT(3)
#define CSI_MAX_REGISTER		CSITX_DPHY_CTRL

void rk628_csi_init(struct rk628 *rk628);
void rk628_csi_enable(struct rk628 *rk628);
void rk628_csi_disable(struct rk628 *rk628);

#endif
