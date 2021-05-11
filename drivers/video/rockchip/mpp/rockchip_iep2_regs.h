/* SPDX-License-Identifier: (GPL-2.0+ OR MIT) */
/*
 * Copyright (c) 2020 Fuzhou Rockchip Electronics Co., Ltd
 *
 * author:
 *	Alpha Lin, alpha.lin@rock-chips.com
 *
 */

#ifndef __ROCKCHIP_IEP2_REGS_H__
#define __ROCKCHIP_IEP2_REGS_H__

#define IEP2_REG_FRM_START			0x0000
#define IEP2_REG_IEP_CONFIG0			0x0004
#define     IEP2_REG_CCLK_SRESET_P			BIT(22)
#define     IEP2_REG_ACLK_SRESET_P			BIT(21)
#define     IEP2_REG_HANDSAVE_P				BIT(20)
#define     IEP2_REG_RST_PROTECT_EN			BIT(19)
#define     IEP2_REG_DEBUG_DATA_EN			BIT(16)
#define     IEP2_REG_DST_YUV_SWAP(x)			(((x) & 3) << 12)
#define     IEP2_REG_DST_FMT(x)				(((x) & 3) << 8)
#define     IEP2_REG_SRC_YUV_SWAP(x)			(((x) & 3) << 4)
#define     IEP2_REG_SRC_FMT(x)				((x) & 3)
#define IEP2_REG_GATING_CTRL			0x0010
#define     IEP2_REG_REG_CLK_ON				BIT(11)
#define     IEP2_REG_DMA_CLK_ON				BIT(10)
#define     IEP2_REG_RAM_CLK_ON				BIT(9)
#define     IEP2_REG_CTRL_CLK_ON			BIT(8)
#define     IEP2_REG_OUT_CLK_ON				BIT(7)
#define     IEP2_REG_BLE_CLK_ON				BIT(6)
#define     IEP2_REG_EEDI_CLK_ON			BIT(5)
#define     IEP2_REG_MC_CLK_ON				BIT(4)
#define     IEP2_REG_ME_CLK_ON				BIT(3)
#define     IEP2_REG_DECT_CLK_ON			BIT(2)
#define     IEP2_REG_MD_CLK_ON				BIT(1)
#define     IEP2_REG_CLK_ON				BIT(0)
#define IEP2_REG_STATUS				0x0014
#define IEP2_REG_INT_EN				0x0020
#define     IEP2_REG_BUS_ERROR_EN			BIT(4)
#define     IEP2_REG_OSD_MAX_EN				BIT(1)
#define     IEP2_REG_FRM_DONE_EN			BIT(0)
#define IEP2_REG_INT_CLR			0x0024
#define     IEP2_REG_BUS_ERROR_CLR			BIT(4)
#define     IEP2_REG_OSD_MAX_CLR			BIT(1)
#define     IEP2_REG_FRM_DONE_CLR			BIT(0)
#define IEP2_REG_INT_STS			0x0028
#define     IEP2_REG_RO_BUS_ERROR_STS(x)		((x) & BIT(4))
#define     IEP2_REG_RO_OSD_MAX_STS(x)			((x) & BIT(1))
#define     IEP2_REG_RO_FRM_DONE_STS(x)			((x) & BIT(0))
#define     IEP2_REG_RO_VALID_INT_STS(x)		((x) & (BIT(5) | BIT(4) | BIT(0)))
#define IEP2_REG_INT_RAW_STS			0x002c
#define IEP2_REG_VIR_SRC_IMG_WIDTH		0x0030
#define     IEP2_REG_SRC_VIR_UV_STRIDE(x)		(((x) & 0xffff) << 16)
#define     IEP2_REG_SRC_VIR_Y_STRIDE(x)		((x) & 0xffff)
#define IEP2_REG_VIR_DST_IMG_WIDTH		0x0034
#define     IEP2_REG_DST_VIR_STRIDE(x)			((x) & 0xffff)
#define IEP2_REG_SRC_IMG_SIZE			0x0038
#define     IEP2_REG_SRC_PIC_HEIGHT(x)			(((x) & 0x7ff) << 16)
#define     IEP2_REG_SRC_PIC_WIDTH(x)			((x) & 0x7ff)
#define IEP2_REG_DIL_CONFIG0			0x0040
#define     IEP2_REG_DIL_MV_HIST_EN			BIT(17)
#define     IEP2_REG_DIL_ROI_EN				BIT(16)
#define     IEP2_REG_DIL_COMB_EN			BIT(15)
#define     IEP2_REG_DIL_BLE_EN				BIT(14)
#define     IEP2_REG_DIL_EEDI_EN			BIT(13)
#define     IEP2_REG_DIL_MEMC_EN			BIT(12)
#define     IEP2_REG_DIL_OSD_EN				BIT(11)
#define     IEP2_REG_DIL_PD_EN				BIT(10)
#define     IEP2_REG_DIL_FF_EN				BIT(9)
#define     IEP2_REG_DIL_MD_PRE_EN			BIT(8)
#define     IEP2_REG_DIL_FIELD_ORDER(x)			(((x) & 1) << 5)
#define     IEP2_REG_DIL_OUT_MODE(x)			(((x) & 1) << 4)
#define     IEP2_REG_DIL_MODE(x)			((x) & 0xf)
#define IEP2_REG_DBG_FRM_CNT			0x0058
#define IEP2_REG_DBG_TIMEOUT_CNT		0x005c
#define IEP2_REG_SRC_ADDR_CURY			0x0060
#define IEP2_REG_SRC_ADDR_NXTY			0x0064
#define IEP2_REG_SRC_ADDR_PREY			0x0068
#define IEP2_REG_SRC_ADDR_CURUV			0x006c
#define IEP2_REG_SRC_ADDR_CURV			0x0070
#define IEP2_REG_SRC_ADDR_NXTUV			0x0074
#define IEP2_REG_SRC_ADDR_NXTV			0x0078
#define IEP2_REG_SRC_ADDR_PREUV			0x007c
#define IEP2_REG_SRC_ADDR_PREV			0x0080
#define IEP2_REG_SRC_ADDR_MD			0x0084
#define IEP2_REG_SRC_ADDR_MV			0x0088
#define IEP2_REG_ROI_ADDR			0x008c
#define IEP2_REG_DST_ADDR_TOPY			0x00b0
#define IEP2_REG_DST_ADDR_BOTY			0x00b4
#define IEP2_REG_DST_ADDR_TOPC			0x00b8
#define IEP2_REG_DST_ADDR_BOTC			0x00bc
#define IEP2_REG_DST_ADDR_MD			0x00c0
#define IEP2_REG_DST_ADDR_MV			0x00c4
#define IEP2_REG_MD_CONFIG0			0x00e0
#define     IEP2_REG_MD_THETA(x)			(((x) & 3) << 8)
#define     IEP2_REG_MD_R(x)				(((x) & 0xf) << 4)
#define     IEP2_REG_MD_LAMBDA(x)			((x) & 0xf)
#define IEP2_REG_DECT_CONFIG0			0x00e4
#define     IEP2_REG_OSD_GRADV_THR(x)			(((x) & 0xff) << 24)
#define     IEP2_REG_OSD_GRADH_THR(x)			(((x) & 0xff) << 16)
#define     IEP2_REG_OSD_AREA_NUM(x)			(((x) & 0xf) << 8)
#define     IEP2_REG_DECT_RESI_THR(x)			((x) & 0xff)
#define IEP2_REG_OSD_LIMIT_CONFIG		0x00f0
#define     IEP2_REG_OSD_POS_LIMIT_NUM(x)		(((x) & 7) << 4)
#define     IEP2_REG_OSD_POS_LIMIT_EN			BIT(0)
#define IEP2_REG_OSD_LIMIT_AREA(i)		(0x00f4 + ((i) * 4))
#define IEP2_REG_OSD_CONFIG0			0x00fc
#define     IEP2_REG_OSD_LINE_NUM(x)			(((x) & 0x1ff) << 16)
#define     IEP2_REG_OSD_PEC_THR(x)			((x) & 0x7ff)
#define IEP2_REG_OSD_AREA_CONF(i)		(0x0100 + ((i) * 4))
#define     IEP2_REG_OSD_Y_END(x)			(((x) & 0x1ff) << 23)
#define     IEP2_REG_OSD_Y_STA(x)			(((x) & 0x1ff) << 14)
#define     IEP2_REG_OSD_X_END(x)			(((x) & 0x7f) << 7)
#define     IEP2_REG_OSD_X_STA(x)			((x) & 0x7f)
#define IEP2_REG_ME_CONFIG0			0x0120
#define     IEP2_REG_ME_THR_OFFSET(x)			(((x) & 0xff) << 16)
#define     IEP2_REG_MV_SIMILAR_NUM_THR0(x)		(((x) & 0xf) << 12)
#define     IEP2_REG_MV_SIMILAR_THR(x)			(((x) & 0xf) << 8)
#define     IEP2_REG_MV_BONUS(x)			(((x) & 0xf) << 4)
#define     IEP2_REG_ME_PENA(x)				((x) & 0xf)
#define IEP2_REG_ME_LIMIT_CONFIG		0x0124
#define     IEP2_REG_MV_RIGHT_LIMIT(x)			(((x) & 0x3f) << 8)
#define     IEP2_REG_MV_LEFT_LIMIT(x)			((x) & 0x3f)
#define IEP2_REG_MV_TRU_LIST(i)			(0x0128 + ((i) * 4))
#define     IEP2_REG_MV_TRU_LIST3_7(x)			(((x) & 0x3f) << 26)
#define     IEP2_REG_MV_TRU_LIST3_7_VLD			BIT(24)
#define     IEP2_REG_MV_TRU_LIST2_6(x)			(((x) & 0x3f) << 18)
#define     IEP2_REG_MV_TRU_LIST2_6_VLD			BIT(16)
#define     IEP2_REG_MV_TRU_LIST1_5(x)			(((x) & 0x3f) << 10)
#define     IEP2_REG_MV_TRU_LIST1_5_VLD			BIT(8)
#define     IEP2_REG_MV_TRU_LIST0_4(x)			(((x) & 0x3f) << 2)
#define     IEP2_REG_MV_TRU_LIST0_4_VLD			BIT(0)
#define IEP2_REG_EEDI_CONFIG0			0x0130
#define     IEP2_REG_EEDI_THR0(x)			((x) & 0x1f)
#define IEP2_REG_BLE_CONFIG0			0x0134
#define     IEP2_REG_BLE_BACKTOMA_NUM(x)		((x) & 7)
#define IEP2_REG_COMB_CONFIG0			0x0138
#define     IEP2_REG_COMB_CNT_THR(x)			(((x) & 0xf) << 24)
#define     IEP2_REG_COMB_FEATRUE_THR(x)		(((x) & 0x3f) << 16)
#define     IEP2_REG_COMB_T_THR(x)			(((x) & 0xff) << 8)
#define     IEP2_REG_COMB_OSD_VLD(i)			BIT(i)
#define IEP2_REG_DIL_MTN_TAB(i)			(0x0140 + ((i) * 4))
#define     IEP2_REG_MTN_SUB_TAB3_7_11_15(x)		(((x) & 0x7f) << 24)
#define     IEP2_REG_MTN_SUB_TAB2_6_10_14(x)		(((x) & 0x7f) << 16)
#define     IEP2_REG_MTN_SUB_TAB1_5_9_13(x)		(((x) & 0x7f) << 8)
#define     IEP2_REG_MTN_SUB_TAB0_4_8_12(x)		((x) & 0x7f)
#define IEP2_REG_RO_PD_TCNT			0x0400
#define IEP2_REG_RO_PD_BCNT			0x0404
#define IEP2_REG_RO_FF_CUR_TCNT			0x0408
#define IEP2_REG_RO_FF_CUR_BCNT			0x040c
#define IEP2_REG_RO_FF_NXT_TCNT			0x0410
#define IEP2_REG_RO_FF_NXT_BCNT			0x0414
#define IEP2_REG_RO_FF_BLE_TCNT			0x0418
#define IEP2_REG_RO_FF_BLE_BCNT			0x041c
#define IEP2_REG_RO_FF_COMB_NZ			0x0420
#define IEP2_REG_RO_FF_COMB_F			0x0424
#define IEP2_REG_RO_OSD_NUM			0x0428
#define IEP2_REG_RO_COMB_CNT			0x042c
#define     IEP2_REG_RO_OUT_OSD_COMB_CNT(x)		((x) >> 16)
#define     IEP2_REG_RO_OUT_COMB_CNT(x)			((x) & 0xffff)
#define IEP2_REG_RO_FF_GRADT_TCNT		0x0430
#define IEP2_REG_RO_FF_GRADT_BCNT		0x0434
#define IEP2_REG_RO_OSD_AREA_X(i)		(0x0440 + ((i) * 8))
#define     IEP2_REG_RO_X_END(x)			(((x) >> 16) & 0x7ff)
#define     IEP2_REG_RO_X_STA(x)			((x) & 0x7ff)
#define IEP2_REG_RO_OSD_AREA_Y(i)		(0x0444 + ((i) * 8))
#define     IEP2_REG_RO_Y_END(x)			(((x) >> 16) & 0x7ff)
#define     IEP2_REG_RO_Y_STA(x)			((x) & 0x7ff)
#define IEP2_REG_RO_MV_HIST_BIN(i)		(0x480 + ((i) * 4))
#define     IEP2_REG_RO_MV_HIST_ODD(x)			((x) >> 16)
#define     IEP2_REG_RO_MV_HIST_EVEN(x)			((x) & 0xffff)

#endif

