/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2020 Rockchip Electronics Co. Ltd.
 *
 * Author: Guochun Huang<hero.huang@rock-chips.com>
 */

#ifndef RK628_DSI_H
#define RK628_DSI_H

#include <linux/delay.h>
#include <linux/videodev2.h>

#include "rk628.h"

#define DSI0_BASE           0x50000
#define DSI1_BASE           0x60000

#define DSI_VERSION			0x0000
#define DSI_PWR_UP			0x0004
#define RESET				0
#define POWER_UP			BIT(0)
#define DSI_CLKMGR_CFG			0x0008
#define TO_CLK_DIVISION(x)		UPDATE(x, 15,  8)
#define TX_ESC_CLK_DIVISION(x)		UPDATE(x,  7,  0)
#define DSI_DPI_VCID			0x000c
#define DPI_VID(x)			UPDATE(x,  1,  0)
#define DSI_DPI_COLOR_CODING		0x0010
#define LOOSELY18_EN			BIT(8)
#define DPI_COLOR_CODING(x)		UPDATE(x,  3,  0)
#define DSI_DPI_CFG_POL			0x0014
#define COLORM_ACTIVE_LOW		BIT(4)
#define SHUTD_ACTIVE_LOW		BIT(3)
#define HSYNC_ACTIVE_LOW		BIT(2)
#define VSYNC_ACTIVE_LOW		BIT(1)
#define DATAEN_ACTIVE_LOW		BIT(0)
#define DSI_DPI_LP_CMD_TIM		0x0018
#define OUTVACT_LPCMD_TIME(x)		UPDATE(x, 23, 16)
#define INVACT_LPCMD_TIME(x)		UPDATE(x,  7,  0)
#define DSI_PCKHDL_CFG			0x002c
#define CRC_RX_EN			BIT(4)
#define ECC_RX_EN			BIT(3)
#define BTA_EN				BIT(2)
#define EOTP_RX_EN			BIT(1)
#define EOTP_TX_EN			BIT(0)
#define DSI_GEN_VCID			0x0030
#define DSI_MODE_CFG			0x0034
#define CMD_VIDEO_MODE(x)		UPDATE(x,  0,  0)
#define DSI_VID_MODE_CFG		0x0038
#define VPG_EN				BIT(16)
#define LP_CMD_EN			BIT(15)
#define FRAME_BTA_ACK_EN		BIT(14)
#define LP_HFP_EN			BIT(13)
#define LP_HBP_EN			BIT(12)
#define LP_VACT_EN			BIT(11)
#define LP_VFP_EN			BIT(10)
#define LP_VBP_EN			BIT(9)
#define LP_VSA_EN			BIT(8)
#define VID_MODE_TYPE(x)		UPDATE(x,  1,  0)
#define DSI_VID_PKT_SIZE		0x003c
#define VID_PKT_SIZE(x)			UPDATE(x, 13,  0)
#define DSI_VID_NUM_CHUNKS		0x0040
#define DSI_VID_NULL_SIZE		0x0044
#define DSI_VID_HSA_TIME		0x0048
#define VID_HSA_TIME(x)			UPDATE(x, 11,  0)
#define DSI_VID_HBP_TIME		0x004c
#define VID_HBP_TIME(x)			UPDATE(x, 11,  0)
#define DSI_VID_HLINE_TIME		0x0050
#define VID_HLINE_TIME(x)		UPDATE(x, 14,  0)
#define DSI_VID_VSA_LINES		0x0054
#define VSA_LINES(x)			UPDATE(x,  9,  0)
#define DSI_VID_VBP_LINES		0x0058
#define VBP_LINES(x)			UPDATE(x,  9,  0)
#define DSI_VID_VFP_LINES		0x005c
#define VFP_LINES(x)			UPDATE(x,  9,  0)
#define DSI_VID_VACTIVE_LINES		0x0060
#define V_ACTIVE_LINES(x)		UPDATE(x, 13,  0)
#define DSI_EDPI_CMD_SIZE		0x0064
#define EDPI_ALLOWED_CMD_SIZE(x)	UPDATE(x, 15,  0)
#define DSI_CMD_MODE_CFG		0x0068
#define MAX_RD_PKT_SIZE			BIT(24)
#define DCS_LW_TX			BIT(19)
#define DCS_SR_0P_TX			BIT(18)
#define DCS_SW_1P_TX			BIT(17)
#define DCS_SW_0P_TX			BIT(16)
#define GEN_LW_TX			BIT(14)
#define GEN_SR_2P_TX			BIT(13)
#define GEN_SR_1P_TX			BIT(12)
#define GEN_SR_0P_TX			BIT(11)
#define GEN_SW_2P_TX			BIT(10)
#define GEN_SW_1P_TX			BIT(9)
#define GEN_SW_0P_TX			BIT(8)
#define ACK_RQST_EN			BIT(1)
#define TEAR_FX_EN			BIT(0)
#define DSI_GEN_HDR			0x006c
#define GEN_WC_MSBYTE(x)		UPDATE(x, 23, 16)
#define GEN_WC_LSBYTE(x)		UPDATE(x, 15,  8)
#define GEN_VC(x)			UPDATE(x,  7,  6)
#define GEN_DT(x)			UPDATE(x,  5,  0)
#define DSI_GEN_PLD_DATA		0x0070
#define DSI_CMD_PKT_STATUS		0x0074
#define GEN_RD_CMD_BUSY			BIT(6)
#define GEN_PLD_R_FULL			BIT(5)
#define GEN_PLD_R_EMPTY			BIT(4)
#define GEN_PLD_W_FULL			BIT(3)
#define GEN_PLD_W_EMPTY			BIT(2)
#define GEN_CMD_FULL			BIT(1)
#define GEN_CMD_EMPTY			BIT(0)
#define DSI_TO_CNT_CFG			0x0078
#define HSTX_TO_CNT(x)			UPDATE(x, 31, 16)
#define LPRX_TO_CNT(x)			UPDATE(x, 15,  0)
#define DSI_HS_RD_TO_CNT		0x007c
#define HS_RD_TO_CNT(x)			UPDATE(x, 15,  0)
#define DSI_LP_RD_TO_CNT		0x0080
#define LP_RD_TO_CNT(x)			UPDATE(x, 15,  0)
#define DSI_HS_WR_TO_CNT		0x0084
#define HS_WR_TO_CNT(x)			UPDATE(x, 15,  0)
#define DSI_LP_WR_TO_CNT		0x0088
#define LP_WR_TO_CNT(x)			UPDATE(x, 15,  0)
#define DSI_BTA_TO_CNT			0x008c
#define BTA_TO_CNT(x)			UPDATE(x, 15,  0)
#define DSI_SDF_3D			0x0090
#define DSI_LPCLK_CTRL			0x0094
#define AUTO_CLKLANE_CTRL		BIT(1)
#define PHY_TXREQUESTCLKHS		BIT(0)
#define DSI_PHY_TMR_LPCLK_CFG		0x0098
#define PHY_CLKHS2LP_TIME(x)		UPDATE(x, 25, 16)
#define PHY_CLKLP2HS_TIME(x)		UPDATE(x,  9,  0)
#define DSI_PHY_TMR_CFG			0x009c
#define PHY_HS2LP_TIME(x)		UPDATE(x, 31, 24)
#define PHY_LP2HS_TIME(x)		UPDATE(x, 23, 16)
#define MAX_RD_TIME(x)			UPDATE(x, 14,  0)
#define DSI_PHY_RSTZ			0x00a0
#define PHY_FORCEPLL			BIT(3)
#define PHY_ENABLECLK			BIT(2)
#define PHY_RSTZ			BIT(1)
#define PHY_SHUTDOWNZ			BIT(0)
#define DSI_PHY_IF_CFG			0x00a4
#define PHY_STOP_WAIT_TIME(x)		UPDATE(x, 15,  8)
#define N_LANES(x)			UPDATE(x,  1,  0)
#define DSI_PHY_STATUS			0x00b0
#define PHY_STOPSTATE3LANE		BIT(11)
#define PHY_STOPSTATE2LANE		BIT(9)
#define PHY_STOPSTATE1LANE		BIT(7)
#define PHY_STOPSTATE0LANE		BIT(4)
#define PHY_STOPSTATECLKLANE		BIT(2)
#define PHY_LOCK			BIT(0)
#define PHY_STOPSTATELANE		(PHY_STOPSTATE0LANE | \
					 PHY_STOPSTATECLKLANE)
#define DSI_INT_ST0			0x00bc
#define DSI_INT_ST1			0x00c0
#define DSI_INT_MSK0			0x00c4
#define DSI_INT_MSK1			0x00c8
#define DSI_INT_FORCE0			0x00d8
#define DSI_INT_FORCE1			0x00dc
#define DSI_MAX_REGISTER		DSI_INT_FORCE1

enum vid_mode_type {
	VIDEO_MODE,
	COMMAND_MODE,
};

struct rk628_dsi {
	struct rk628 *rk628;
	struct v4l2_dv_timings timings;
	u64 lane_mbps;
	int vid_mode;
	int mode_flags;
};

void rk628_mipi_dsi_power_on(struct rk628_dsi *dsi);

#endif
