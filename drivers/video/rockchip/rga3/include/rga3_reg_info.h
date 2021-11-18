/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __REG3_INFO_H__
#define __REG3_INFO_H__

#include "rga_drv.h"

//General Registers
/* yqw: status和int寄存器尚不明了，无法进行修改。 */
//#define RGA2_STATUS			 0x00c
//#define RGA2_INT				 0x010

#define RGA3_SYS_CTRL			 0x000
#define RGA3_CMD_CTRL			 0x004
#define RGA3_CMD_ADDR			 0x008
#define RGA3_MI_GROUP_CTRL		0x00c
#define RGA3_ARQOS_CTRL		 0x010
#define RGA3_VERSION_NUM		 0x018
#define RGA3_VERSION_TIM		 0x01c
#define RGA3_INT_EN			 0x020
#define RGA3_INT_RAW			 0x024
#define RGA3_INT_MSK			 0x028
#define RGA3_INT_CLR			 0x02c
#define RGA3_RO_SRST			 0x030
#define RGA3_STATUS0			 0x034
#define RGA3_SCAN_CNT			 0x038
#define RGA3_STATUS1			 0x03c
#define RGA3_CMD_STATE			0x040

/* TODO: RGA_INT */

/* RGA3_WIN0_RD_CTRL */
#define m_RGA3_WIN0_RD_CTRL_SW_WIN0_ENABLE			 (0x1 << 0)
#define m_RGA3_WIN0_RD_CTRL_SW_WIN0_RD_MODE			 (0x3 << 1)
#define m_RGA3_WIN0_RD_CTRL_SW_WIN0_PIC_FORMAT		 (0xf << 4)
#define m_RGA3_WIN0_RD_CTRL_SW_WIN0_RD_FORMAT		 (0x3 << 8)
#define m_RGA3_WIN0_RD_CTRL_SW_WIN0_YUV10B_COMPACT	 (0x1 << 10)
#define m_RGA3_WIN0_RD_CTRL_SW_WIN0_ENDIAN_MODE		 (0x1 << 11)
#define m_RGA3_WIN0_RD_CTRL_SW_WIN0_PIX_SWAP			(0x1 << 12)
#define m_RGA3_WIN0_RD_CTRL_SW_WIN0_YC_SWAP			 (0x1 << 13)
#define m_RGA3_WIN0_RD_CTRL_SW_WIN0_ROT				 (0x1 << 16)
#define m_RGA3_WIN0_RD_CTRL_SW_WIN0_XMIRROR			 (0x1 << 17)
#define m_RGA3_WIN0_RD_CTRL_SW_WIN0_YMIRROR			 (0x1 << 18)
#define m_RGA3_WIN0_RD_CTRL_SW_WIN0_HOR_BY			 (0x1 << 20)
#define m_RGA3_WIN0_RD_CTRL_SW_WIN0_HOR_UP			 (0x1 << 21)
#define m_RGA3_WIN0_RD_CTRL_SW_WIN0_VER_BY			 (0x1 << 22)
#define m_RGA3_WIN0_RD_CTRL_SW_WIN0_VER_UP			 (0x1 << 23)
#define m_RGA3_WIN0_RD_CTRL_SW_WIN0_Y2R_EN			 (0x1 << 24)
#define m_RGA3_WIN0_RD_CTRL_SW_WIN0_R2Y_EN			 (0x1 << 25)
#define m_RGA3_WIN0_RD_CTRL_SW_WIN0_CSC_MODE			(0x3 << 26)
#define m_RGA3_WIN0_RD_CTRL_SW_WIN0_PERF_OPT_DIS		(0x1 << 29)
#define m_RGA3_WIN0_RD_CTRL_SW_WIN0_RD_ALIGN_DIS		(0x1 << 30)

#define s_RGA3_WIN0_RD_CTRL_SW_WIN0_ENABLE(x)		 ((x & 0x1) << 0)
#define s_RGA3_WIN0_RD_CTRL_SW_WIN0_RD_MODE(x)		 ((x & 0x3) << 1)
#define s_RGA3_WIN0_RD_CTRL_SW_WIN0_PIC_FORMAT(x)	 ((x & 0xf) << 4)
#define s_RGA3_WIN0_RD_CTRL_SW_WIN0_RD_FORMAT(x)		((x & 0x3) << 8)
#define s_RGA3_WIN0_RD_CTRL_SW_WIN0_YUV10B_COMPACT(x) ((x & 0x1) << 10)
#define s_RGA3_WIN0_RD_CTRL_SW_WIN0_ENDIAN_MODE(x)	 ((x & 0x1) << 11)
#define s_RGA3_WIN0_RD_CTRL_SW_WIN0_PIX_SWAP(x)		 ((x & 0x1) << 12)
#define s_RGA3_WIN0_RD_CTRL_SW_WIN0_YC_SWAP(x)		 ((x & 0x1) << 13)
#define s_RGA3_WIN0_RD_CTRL_SW_WIN0_ROT(x)		((x & 0x1) << 16)
#define s_RGA3_WIN0_RD_CTRL_SW_WIN0_XMIRROR(x)		 ((x & 0x1) << 17)
#define s_RGA3_WIN0_RD_CTRL_SW_WIN0_YMIRROR(x)		 ((x & 0x1) << 18)
#define s_RGA3_WIN0_RD_CTRL_SW_WIN0_HOR_BY(x)		 ((x & 0x1) << 20)
#define s_RGA3_WIN0_RD_CTRL_SW_WIN0_HOR_UP(x)		 ((x & 0x1) << 21)
#define s_RGA3_WIN0_RD_CTRL_SW_WIN0_VER_BY(x)		 ((x & 0x1) << 22)
#define s_RGA3_WIN0_RD_CTRL_SW_WIN0_VER_UP(x)		 ((x & 0x1) << 23)
#define s_RGA3_WIN0_RD_CTRL_SW_WIN0_Y2R_EN(x)		 ((x & 0x1) << 24)
#define s_RGA3_WIN0_RD_CTRL_SW_WIN0_R2Y_EN(x)		 ((x & 0x1) << 25)
#define s_RGA3_WIN0_RD_CTRL_SW_WIN0_CSC_MODE(x)		 ((x & 0x3) << 26)
#define s_RGA3_WIN0_RD_CTRL_SW_WIN0_PERF_OPT_DIS(x)	 ((x & 0x1) << 29)
#define s_RGA3_WIN0_RD_CTRL_SW_WIN0_RD_ALIGN_DIS(x)	 ((x & 0x1) << 30)

/* RGA3_WIN0_FBC_OFF */
#define m_RGA3_WIN0_FBC_OFF_SW_WIN0_FBC_XOFF			(0x1fff << 0)
#define m_RGA3_WIN0_FBC_OFF_SW_WIN0_FBC_YOFF			(0x1fff << 16)

#define s_RGA3_WIN0_FBC_OFF_SW_WIN0_FBC_XOFF(x)		 ((x & 0x1fff) << 0)
#define s_RGA3_WIN0_FBC_OFF_SW_WIN0_FBC_YOFF(x)		 ((x & 0x1fff) << 16)

/* RGA3_WIN0_SRC_SIZE */
#define m_RGA3_WIN0_SRC_SIZE_SW_WIN0_SRC_WIDTH		 (0x1fff << 0)
#define m_RGA3_WIN0_SRC_SIZE_SW_WIN0_SRC_HEIGHT		 (0x1fff << 16)

#define s_RGA3_WIN0_SRC_OFF_SW_WIN0_SRC_WIDTH(x)	((x & 0x1fff) << 0)
#define s_RGA3_WIN0_SRC_OFF_SW_WIN0_SRC_HEIGHT(x)	 ((x & 0x1fff) << 16)

/* RGA3_WIN0_ACT_OFF */
#define m_RGA3_WIN0_ACT_OFF_SW_WIN0_ACT_XOFF			(0x1fff << 0)
#define m_RGA3_WIN0_ACT_OFF_SW_WIN0_ACT_YOFF			(0x1fff << 16)

#define s_RGA3_WIN0_ACT_OFF_SW_WIN0_ACT_XOFF(x)		 ((x & 0x1fff) << 0)
#define s_RGA3_WIN0_ACT_OFF_SW_WIN0_ACT_YOFF(x)		 ((x & 0x1fff) << 16)

/* RGA3_WIN0_ACT_SIZE */
#define m_RGA3_WIN0_ACT_SIZE_SW_WIN0_ACT_WIDTH		 (0x1fff << 0)
#define m_RGA3_WIN0_ACT_SIZE_SW_WIN0_ACT_HEIGHT		 (0x1fff << 16)

#define s_RGA3_WIN0_ACT_SIZE_SW_WIN0_ACT_WIDTH(x)	 ((x & 0x1fff) << 0)
#define s_RGA3_WIN0_ACT_SIZE_SW_WIN0_ACT_HEIGHT(x)	 ((x & 0x1fff) << 16)

/* RGA3_WIN0_DST_SIZE */
#define m_RGA3_WIN0_DST_SIZE_SW_WIN0_DST_WIDTH		 (0x1fff << 0)
#define m_RGA3_WIN0_DST_SIZE_SW_WIN0_DST_HEIGHT		 (0x1fff << 16)

#define s_RGA3_WIN0_DST_SIZE_SW_WIN0_DST_WIDTH(x)	 ((x & 0x1fff) << 0)
#define s_RGA3_WIN0_DST_SIZE_SW_WIN0_DST_HEIGHT(x)	 ((x & 0x1fff) << 16)

/* RGA3_WIN0_SCL_FAC */
#define m_RGA3_WIN0_SCL_FAC_SW_WIN0_VER_FAC			 (0xffff << 0)
#define m_RGA3_WIN0_SCL_FAC_SW_WIN0_HOR_FAC			 (0xffff << 16)

#define s_RGA3_WIN0_SCL_FAC_SW_WIN0_VER_FAC(x)		 ((x & 0xffff) << 0)
#define s_RGA3_WIN0_SCL_FAC_SW_WIN0_HOR_FAC(x)		 ((x & 0xffff) << 16)

/* RGA3_WIN1_RD_CTRL */
#define m_RGA3_WIN1_RD_CTRL_SW_WIN1_ENABLE			 (0x1 << 0)
#define m_RGA3_WIN1_RD_CTRL_SW_WIN1_RD_MODE			 (0x3 << 1)
#define m_RGA3_WIN1_RD_CTRL_SW_WIN1_PIC_FORMAT		 (0xf << 4)
#define m_RGA3_WIN1_RD_CTRL_SW_WIN1_RD_FORMAT		 (0x3 << 8)
#define m_RGA3_WIN1_RD_CTRL_SW_WIN1_YUV10B_COMPACT	 (0x1 << 10)
#define m_RGA3_WIN1_RD_CTRL_SW_WIN1_ENDIAN_MODE		 (0x1 << 11)
#define m_RGA3_WIN1_RD_CTRL_SW_WIN1_PIX_SWAP			(0x1 << 12)
#define m_RGA3_WIN1_RD_CTRL_SW_WIN1_YC_SWAP			 (0x1 << 13)
#define m_RGA3_WIN1_RD_CTRL_SW_WIN1_ROT				 (0x1 << 16)
#define m_RGA3_WIN1_RD_CTRL_SW_WIN1_XMIRROR			 (0x1 << 17)
#define m_RGA3_WIN1_RD_CTRL_SW_WIN1_YMIRROR			 (0x1 << 18)
#define m_RGA3_WIN1_RD_CTRL_SW_WIN1_HOR_BY			 (0x1 << 20)
#define m_RGA3_WIN1_RD_CTRL_SW_WIN1_HOR_UP			 (0x1 << 21)
#define m_RGA3_WIN1_RD_CTRL_SW_WIN1_VER_BY			 (0x1 << 22)
#define m_RGA3_WIN1_RD_CTRL_SW_WIN1_VER_UP			 (0x1 << 23)
#define m_RGA3_WIN1_RD_CTRL_SW_WIN1_Y2R_EN			 (0x1 << 24)
#define m_RGA3_WIN1_RD_CTRL_SW_WIN1_R2Y_EN			 (0x1 << 25)
#define m_RGA3_WIN1_RD_CTRL_SW_WIN1_CSC_MODE			(0x3 << 26)
#define m_RGA3_WIN1_RD_CTRL_SW_WIN1_PERF_OPT_DIS		(0x1 << 29)
#define m_RGA3_WIN1_RD_CTRL_SW_WIN1_RD_ALIGN_DIS		(0x1 << 30)

#define s_RGA3_WIN1_RD_CTRL_SW_WIN1_ENABLE(x)		 ((x & 0x1) << 0)
#define s_RGA3_WIN1_RD_CTRL_SW_WIN1_RD_MODE(x)		 ((x & 0x3) << 1)
#define s_RGA3_WIN1_RD_CTRL_SW_WIN1_PIC_FORMAT(x)	 ((x & 0xf) << 4)
#define s_RGA3_WIN1_RD_CTRL_SW_WIN1_RD_FORMAT(x)		((x & 0x3) << 8)
#define s_RGA3_WIN1_RD_CTRL_SW_WIN1_YUV10B_COMPACT(x) ((x & 0x1) << 10)
#define s_RGA3_WIN1_RD_CTRL_SW_WIN1_ENDIAN_MODE(x)	 ((x & 0x1) << 11)
#define s_RGA3_WIN1_RD_CTRL_SW_WIN1_PIX_SWAP(x)		 ((x & 0x1) << 12)
#define s_RGA3_WIN1_RD_CTRL_SW_WIN1_YC_SWAP(x)		 ((x & 0x1) << 13)
#define s_RGA3_WIN1_RD_CTRL_SW_WIN1_ROT(x)		((x & 0x1) << 16)
#define s_RGA3_WIN1_RD_CTRL_SW_WIN1_XMIRROR(x)		 ((x & 0x1) << 17)
#define s_RGA3_WIN1_RD_CTRL_SW_WIN1_YMIRROR(x)		 ((x & 0x1) << 18)
#define s_RGA3_WIN1_RD_CTRL_SW_WIN1_HOR_BY(x)		 ((x & 0x1) << 20)
#define s_RGA3_WIN1_RD_CTRL_SW_WIN1_HOR_UP(x)		 ((x & 0x1) << 21)
#define s_RGA3_WIN1_RD_CTRL_SW_WIN1_VER_BY(x)		 ((x & 0x1) << 22)
#define s_RGA3_WIN1_RD_CTRL_SW_WIN1_VER_UP(x)		 ((x & 0x1) << 23)
#define s_RGA3_WIN1_RD_CTRL_SW_WIN1_Y2R_EN(x)		 ((x & 0x1) << 24)
#define s_RGA3_WIN1_RD_CTRL_SW_WIN1_R2Y_EN(x)		 ((x & 0x1) << 25)
#define s_RGA3_WIN1_RD_CTRL_SW_WIN1_CSC_MODE(x)		 ((x & 0x3) << 26)
#define s_RGA3_WIN1_RD_CTRL_SW_WIN1_PERF_OPT_DIS(x)	 ((x & 0x1) << 29)
#define s_RGA3_WIN1_RD_CTRL_SW_WIN1_RD_ALIGN_DIS(x)	 ((x & 0x1) << 30)

/* RGA3_WIN1_FBC_OFF */
#define m_RGA3_WIN1_FBC_OFF_SW_WIN1_FBC_XOFF			(0x1fff << 0)
#define m_RGA3_WIN1_FBC_OFF_SW_WIN1_FBC_YOFF			(0x1fff << 16)

#define s_RGA3_WIN1_FBC_OFF_SW_WIN1_FBC_XOFF(x)		 ((x & 0x1fff) << 0)
#define s_RGA3_WIN1_FBC_OFF_SW_WIN1_FBC_YOFF(x)		 ((x & 0x1fff) << 16)

/* RGA3_WIN1_SRC_SIZE */
#define m_RGA3_WIN1_SRC_SIZE_SW_WIN1_SRC_WIDTH		 (0x1fff << 0)
#define m_RGA3_WIN1_SRC_SIZE_SW_WIN1_SRC_HEIGHT		 (0x1fff << 16)

#define s_RGA3_WIN1_SRC_OFF_SW_WIN1_SRC_WIDTH(x)	((x & 0x1fff) << 0)
#define s_RGA3_WIN1_SRC_OFF_SW_WIN1_SRC_HEIGHT(x)	 ((x & 0x1fff) << 16)

/* RGA3_WIN1_ACT_OFF */
#define m_RGA3_WIN1_ACT_OFF_SW_WIN1_ACT_XOFF			(0x1fff << 0)
#define m_RGA3_WIN1_ACT_OFF_SW_WIN1_ACT_YOFF			(0x1fff << 16)

#define s_RGA3_WIN1_ACT_OFF_SW_WIN1_ACT_XOFF(x)		 ((x & 0x1fff) << 0)
#define s_RGA3_WIN1_ACT_OFF_SW_WIN1_ACT_YOFF(x)		 ((x & 0x1fff) << 16)

/* RGA3_WIN1_ACT_SIZE */
#define m_RGA3_WIN1_ACT_SIZE_SW_WIN1_ACT_WIDTH		 (0x1fff << 0)
#define m_RGA3_WIN1_ACT_SIZE_SW_WIN1_ACT_HEIGHT		 (0x1fff << 16)

#define s_RGA3_WIN1_ACT_SIZE_SW_WIN1_ACT_WIDTH(x)	 ((x & 0x1fff) << 0)
#define s_RGA3_WIN1_ACT_SIZE_SW_WIN1_ACT_HEIGHT(x)	 ((x & 0x1fff) << 16)

/* RGA3_WIN1_DST_SIZE */
#define m_RGA3_WIN1_DST_SIZE_SW_WIN1_DST_WIDTH		 (0x1fff << 0)
#define m_RGA3_WIN1_DST_SIZE_SW_WIN1_DST_HEIGHT		 (0x1fff << 16)

#define s_RGA3_WIN1_DST_SIZE_SW_WIN1_DST_WIDTH(x)	 ((x & 0x1fff) << 0)
#define s_RGA3_WIN1_DST_SIZE_SW_WIN1_DST_HEIGHT(x)	 ((x & 0x1fff) << 16)

/* RGA3_WIN1_SCL_FAC */
#define m_RGA3_WIN1_SCL_FAC_SW_WIN1_VER_FAC			 (0xffff << 0)
#define m_RGA3_WIN1_SCL_FAC_SW_WIN1_HOR_FAC			 (0xffff << 16)

#define s_RGA3_WIN1_SCL_FAC_SW_WIN1_VER_FAC(x)		 ((x & 0xffff) << 0)
#define s_RGA3_WIN1_SCL_FAC_SW_WIN1_HOR_FAC(x)		 ((x & 0xffff) << 16)

/* RGA3_OVLP_CTRL */
#define m_RGA3_OVLP_CTRL_SW_OVLP_MODE				 (0x3 << 0)
#define m_RGA3_OVLP_CTRL_SW_OVLP_FIELD				 (0x1 << 2)
#define m_RGA3_OVLP_CTRL_SW_TOP_SWAP			(0x1 << 3)
#define m_RGA3_OVLP_CTRL_SW_TOP_ALPHA_EN		(0x1 << 4)
#define m_RGA3_OVLP_CTRL_SW_TOP_KEY_EN				 (0x7FFF << 5)
#define m_RGA3_OVLP_CTRL_SW_OVLP_Y2R_EN				 (0x1 << 20)
#define m_RGA3_OVLP_CTRL_SW_OVLP_R2Y_EN				 (0x1 << 21)
#define m_RGA3_OVLP_CTRL_SW_OVLP_CSC_MODE			 (0x3 << 22)

#define s_RGA3_OVLP_CTRL_SW_OVLP_MODE(x)		((x & 0x3) << 0)
#define s_RGA3_OVLP_CTRL_SW_OVLP_FIELD(x)		((x & 0x1) << 2)
#define s_RGA3_OVLP_CTRL_SW_TOP_SWAP(x)			((x & 0x1) << 3)
#define s_RGA3_OVLP_CTRL_SW_TOP_ALPHA_EN(x)		((x & 0x1) << 4)
#define s_RGA3_OVLP_CTRL_SW_TOP_KEY_EN(x)		((x & 0x7FFF) << 5)
#define s_RGA3_OVLP_CTRL_SW_OVLP_Y2R_EN(x)		((x & 0x1) << 20)
#define s_RGA3_OVLP_CTRL_SW_OVLP_R2Y_EN(x)		((x & 0x1) << 21)
#define s_RGA3_OVLP_CTRL_SW_OVLP_CSC_MODE(x)	((x & 0x3) << 22)

/* RGA3_OVLP_OFF */
#define m_RGA3_OVLP_OFF_SW_OVLP_XOFF		(0x1fff << 0)
#define m_RGA3_OVLP_OFF_SW_OVLP_YOFF		(0x1fff << 16)

#define s_RGA3_OVLP_OFF_SW_OVLP_XOFF(x)		((x & 0x1fff) << 0)
#define s_RGA3_OVLP_OFF_SW_OVLP_YOFF(x)		((x & 0x1fff) << 16)

/* RGA3_OVLP_TOP_KEY_MIN */
#define m_RGA3_OVLP_TOP_KEY_MIN_SW_TOP_KEY_YG_MIN	 (0x3ff << 0)
#define m_RGA3_OVLP_TOP_KEY_MIN_SW_TOP_KEY_UB_MIN	 (0x3ff << 10)
#define m_RGA3_OVLP_TOP_KEY_MIN_SW_TOP_KEY_VR_MIN	 (0x3ff << 20)

#define s_RGA3_OVLP_TOP_KEY_MIN_SW_TOP_KEY_YG_MIN(x)	((x & 0x3f)f << 0)
#define s_RGA3_OVLP_TOP_KEY_MIN_SW_TOP_KEY_UB_MIN(x)	((x & 0x3ff) << 10)
#define s_RGA3_OVLP_TOP_KEY_MIN_SW_TOP_KEY_VR_MIN(x)	((x & 0x3ff) << 20)

/* RGA3_OVLP_TOP_KEY_MAX */
#define m_RGA3_OVLP_TOP_KEY_MAX_SW_TOP_KEY_YG_MAX	 (0x3ff << 0)
#define m_RGA3_OVLP_TOP_KEY_MAX_SW_TOP_KEY_UB_MAX	 (0x3ff << 10)
#define m_RGA3_OVLP_TOP_KEY_MAX_SW_TOP_KEY_VR_MAX	 (0x3ff << 20)

#define s_RGA3_OVLP_TOP_KEY_MAX_SW_TOP_KEY_YG_MAX(x)	((x & 0x3ff) << 0)
#define s_RGA3_OVLP_TOP_KEY_MAX_SW_TOP_KEY_UB_MAX(x)	((x & 0x3ff) << 10)
#define s_RGA3_OVLP_TOP_KEY_MAX_SW_TOP_KEY_VR_MAX(x)	((x & 0x3ff) << 20)

/* RGA3_OVLP_TOP_CTRL */
#define m_RGA3_OVLP_TOP_CTRL_SW_TOP_COLOR_M0			(0x1 << 0)
#define m_RGA3_OVLP_TOP_CTRL_SW_TOP_ALPHA_M0			(0x1 << 1)
#define m_RGA3_OVLP_TOP_CTRL_SW_TOP_BLEND_M0			(0x3 << 2)
#define m_RGA3_OVLP_TOP_CTRL_SW_TOP_ALPHA_CAL_M0		(0x1 << 4)
#define m_RGA3_OVLP_TOP_CTRL_SW_TOP_FACTOR_M0		 (0x7 << 5)
#define m_RGA3_OVLP_TOP_CTRL_SW_TOP_GLOBAL_ALPHA		(0xff << 16)

#define s_RGA3_OVLP_TOP_CTRL_SW_TOP_COLOR_M0(x)		 ((x & 0x1) << 0)
#define s_RGA3_OVLP_TOP_CTRL_SW_TOP_ALPHA_M0(x)		 ((x & 0x1) << 1)
#define s_RGA3_OVLP_TOP_CTRL_SW_TOP_BLEND_M0(x)		 ((x & 0x3) << 2)
#define s_RGA3_OVLP_TOP_CTRL_SW_TOP_ALPHA_CAL_M0(x)	 ((x & 0x1) << 4)
#define s_RGA3_OVLP_TOP_CTRL_SW_TOP_FACTOR_M0(x)		((x & 0x7) << 5)
#define s_RGA3_OVLP_TOP_CTRL_SW_TOP_GLOBAL_ALPHA(x)	 ((x & 0xff) << 16)

/* RGA3_OVLP_BOT_CTRL */
#define m_RGA3_OVLP_BOT_CTRL_SW_BOT_COLOR_M0			(0x1 << 0)
#define m_RGA3_OVLP_BOT_CTRL_SW_BOT_ALPHA_M0			(0x1 << 1)
#define m_RGA3_OVLP_BOT_CTRL_SW_BOT_BLEND_M0			(0x3 << 2)
#define m_RGA3_OVLP_BOT_CTRL_SW_BOT_ALPHA_CAL_M0		(0x1 << 4)
#define m_RGA3_OVLP_BOT_CTRL_SW_BOT_FACTOR_M0		 (0x7 << 5)
#define m_RGA3_OVLP_BOT_CTRL_SW_BOT_GLOBAL_ALPHA		(0xff << 16)

#define s_RGA3_OVLP_BOT_CTRL_SW_BOT_COLOR_M0(x)		 ((x & 0x1) << 0)
#define s_RGA3_OVLP_BOT_CTRL_SW_BOT_ALPHA_M0(x)		 ((x & 0x1) << 1)
#define s_RGA3_OVLP_BOT_CTRL_SW_BOT_BLEND_M0(x)		 ((x & 0x3) << 2)
#define s_RGA3_OVLP_BOT_CTRL_SW_BOT_ALPHA_CAL_M0(x)	 ((x & 0x1) << 4)
#define s_RGA3_OVLP_BOT_CTRL_SW_BOT_FACTOR_M0(x)		((x & 0x7) << 5)
#define s_RGA3_OVLP_BOT_CTRL_SW_BOT_GLOBAL_ALPHA(x)	 ((x & 0xff) << 16)

/* RGA3_OVLP_TOP_ALPHA */
#define m_RGA3_OVLP_TOP_ALPHA_SW_TOP_ALPHA_M1		 (0x1 << 1)
#define m_RGA3_OVLP_TOP_ALPHA_SW_TOP_BLEND_M1		 (0x3 << 2)
#define m_RGA3_OVLP_TOP_ALPHA_SW_TOP_ALPHA_CAL_M1	 (0x1 << 4)
#define m_RGA3_OVLP_TOP_ALPHA_SW_TOP_FACTOR_M1		 (0x7 << 5)

#define s_RGA3_OVLP_TOP_ALPHA_SW_TOP_ALPHA_M1(x)		((x & 0x1) << 1)
#define s_RGA3_OVLP_TOP_ALPHA_SW_TOP_BLEND_M1(x)		((x & 0x3) << 2)
#define s_RGA3_OVLP_TOP_ALPHA_SW_TOP_ALPHA_CAL_M1(x)	((x & 0x1) << 4)
#define s_RGA3_OVLP_TOP_ALPHA_SW_TOP_FACTOR_M1(x)	 ((x & 0x7) << 5)

/* RGA3_OVLP_BOT_ALPHA */
#define m_RGA3_OVLP_BOT_ALPHA_SW_BOT_ALPHA_M1		 (0x1 << 1)
#define m_RGA3_OVLP_BOT_ALPHA_SW_BOT_BLEND_M1		 (0x3 << 2)
#define m_RGA3_OVLP_BOT_ALPHA_SW_BOT_ALPHA_CAL_M1	 (0x1 << 4)
#define m_RGA3_OVLP_BOT_ALPHA_SW_BOT_FACTOR_M1		 (0x7 << 5)

#define s_RGA3_OVLP_BOT_ALPHA_SW_BOT_ALPHA_M1(x)		((x & 0x1) << 1)
#define s_RGA3_OVLP_BOT_ALPHA_SW_BOT_BLEND_M1(x)		((x & 0x3) << 2)
#define s_RGA3_OVLP_BOT_ALPHA_SW_BOT_ALPHA_CAL_M1(x)	((x & 0x1) << 4)
#define s_RGA3_OVLP_BOT_ALPHA_SW_BOT_FACTOR_M1(x)	 ((x & 0x7) << 5)

/* RGA3_WR_CTRL */
#define m_RGA3_WR_CTRL_SW_WR_MODE			(0x3 << 0)
#define m_RGA3_WR_CTRL_SW_WR_FBCE_SPARSE_EN			 (0x1 << 2)
#define m_RGA3_WR_CTRL_SW_WR_PIC_FORMAT				 (0xf << 4)
#define m_RGA3_WR_CTRL_SW_WR_FORMAT				(0x3 << 8)
#define m_RGA3_WR_CTRL_SW_WR_YUV10B_COMPACT			 (0x1 << 10)
#define m_RGA3_WR_CTRL_SW_WR_ENDIAN_MODE		(0x1 << 11)
#define m_RGA3_WR_CTRL_SW_WR_PIX_SWAP				 (0x1 << 12)
#define m_RGA3_WR_CTRL_SW_OUTSTANDING_MAX			 (0x3f << 13)
#define m_RGA3_WR_CTRL_SW_WR_YC_SWAP			(0x1 << 20)

#define s_RGA3_WR_CTRL_SW_WR_MODE(x)			((x & 0x3) << 0)
#define s_RGA3_WR_CTRL_SW_WR_FBCE_SPARSE_EN(x)		 ((x & 0x1) << 2)
#define s_RGA3_WR_CTRL_SW_WR_PIC_FORMAT(x)		((x & 0xf) << 4)
#define s_RGA3_WR_CTRL_SW_WR_FORMAT(x)			((x & 0x3) << 8)
#define s_RGA3_WR_CTRL_SW_WR_YUV10B_COMPACT(x)		 ((x & 0x1) << 10)
#define s_RGA3_WR_CTRL_SW_WR_ENDIAN_MODE(x)		((x & 0x1) << 11)
#define s_RGA3_WR_CTRL_SW_WR_PIX_SWAP(x)		((x & 0x1) << 12)
#define s_RGA3_WR_CTRL_SW_OUTSTANDING_MAX(x)	((x & 0x3f) << 13)
#define s_RGA3_WR_CTRL_SW_WR_YC_SWAP(x)			((x & 0x1) << 20)

/* RGA3_WR_FBCE_CTRL */
#define m_RGA3_WR_FBCE_CTRL_SW_WR_FBCE_BLKBD_OPT_DIS	(0x1 << 0)
#define m_RGA3_WR_FBCE_CTRL_SW_WR_FBCE_HOFF_DISS		(0x1 << 1)
#define m_RGA3_WR_FBCE_CTRL_SW_WR_FBCE_PL_FIFO0_WATERMARK	 (0x3f << 2)
#define m_RGA3_WR_FBCE_CTRL_SW_WR_FBCE_PL_FIFO1_WATERMARK	 (0x3f << 8)
#define m_RGA3_WR_FBCE_CTRL_SW_WR_FBCE_SIZE_ALIGN_DIS		 (0x1 << 31)

#define s_RGA3_WR_FBCE_CTRL_SW_WR_FBCE_BLKBD_OPT_DIS(x)	((x & 0x1) << 0)
#define s_RGA3_WR_FBCE_CTRL_SW_WR_FBCE_HOFF_DISS(x)		((x & 0x1) << 1)
#define s_RGA3_WR_FBCE_CTRL_SW_WR_FBCE_PL_FIFO0_WATERMARK(x) ((x & 0x3f) << 2)
#define s_RGA3_WR_FBCE_CTRL_SW_WR_FBCE_PL_FIFO1_WATERMARK(x) ((x & 0x3f) << 8)
#define s_RGA3_WR_FBCE_CTRL_SW_WR_FBCE_SIZE_ALIGN_DIS(x) ((x & 0x1) << 31)

/* RGA3_MMU_STATUS read_only */
#define m_RGA3_MMU_STATUS_PAGING_ENABLED		(0x1 << 0)
#define m_RGA3_MMU_STATUS_PAGE_FAULT_ACTIVE			 (0x1 << 1)
#define m_RGA3_MMU_STATUS_STAIL_ACTIVE				 (0x1 << 2)
#define m_RGA3_MMU_STATUS_MMU_IDLE			(0x1 << 3)
#define m_RGA3_MMU_STATUS_REPLAY_BUFFER_EMPTY		 (0x1 << 4)
#define m_RGA3_MMU_STATUS_PAGE_FAULT_IS_WRITE		 (0x1 << 5)
#define m_RGA3_MMU_STATUS_PAGE_FAULT_BUS_ID			 (0x1f << 6)

/* RGA3_MMU_INT_RAWSTAT read_only */
#define m_RGA3_MMU_INT_RAWSTAT_READ_BUS_ERROR		 (0x1 << 0)
#define m_RGA3_MMU_INT_RAWSTAT_PAGE_FAULT			 (0x1 << 1)

/* RGA3_MMU_INT_CLEAR write_only */
#define m_RGA3_MMU_INT_CLEAR_READ_BUS_ERROR			 (0x1 << 0)
#define m_RGA3_MMU_INT_CLEAR_PAGE_FAULT				 (0x1 << 1)

#define s_RGA3_MMU_INT_CLEAR_READ_BUS_ERROR(x)		 ((x & 0x1) << 0)
#define s_RGA3_MMU_INT_CLEAR_PAGE_FAULT(x)		((x & 0x1) << 1)

/* RGA3_MMU_INT_MASK */
#define m_RGA3_MMU_INT_MASK_READ_BUS_ERROR			 (0x1 << 0)
#define m_RGA3_MMU_INT_MASK_PAGE_FAULT				 (0x1 << 1)

#define s_RGA3_MMU_INT_MASK_READ_BUS_ERROR(x)		 ((x & 0x1) << 0)
#define s_RGA3_MMU_INT_MASK_PAGE_FAULT(x)		((x & 0x1) << 1)

/* RGA3_MMU_INT_STATUS read_only */
#define m_RGA3_MMU_INT_STATUS_READ_BUS_ERROR		(0x1 << 0)
#define m_RGA3_MMU_INT_STATUS_PAGE_FAULT			(0x1 << 1)

/* RGA3_MMU_AUTO_GATING */
#define m_RGA3_MMU_AUTO_GATING_MMU_AUTO_GATING		 (0x1 << 1)
#define m_RGA3_MMU_AUTO_GATING_MMU_CFG_MODE		(0x1 << 1)
#define m_RGA3_MMU_AUTO_GATING_MMU_BUG_FIXED_DISABLE	(0x1 << 31)

#define s_RGA3_MMU_AUTO_GATING_MMU_AUTO_GATING(x)	 ((x & 0x1) << 1)
#define s_RGA3_MMU_AUTO_GATING_MMU_BUG_FIXED_DISABLE(x) ((x & 0x1) << 31)

/* sys_reg */
#define RGA3_SYS_CTRL_OFFSET			 0x000
#define RGA3_CMD_CTRL_OFFSET			 0x004
#define RGA3_CMD_ADDR_OFFSET			 0x008
#define RGA3_MI_GROUP_CTRL_OFFSET		 0x00c
#define RGA3_ARQOS_CTRL_OFFSET			 0x010
#define RGA3_VERSION_NUM_OFFSET			 0x018
#define RGA3_VERSION_TIM_OFFSET			 0x01c
#define RGA3_INT_EN_OFFSET				 0x020
#define RGA3_INT_RAW_OFFSET				 0x024
#define RGA3_INT_MSK_OFFSET				 0x028
#define RGA3_INT_CLR_OFFSET				 0x02c
#define RGA3_RO_SRST_OFFSET				 0x030
#define RGA3_STATUS0_OFFSET				 0x034
#define RGA3_SCAN_CNT_OFFSET			 0x038
#define RGA3_STATUS1_OFFSET				 0x03c
#define RGA3_CMD_STATE_OFFSET			 0x040

/* op_reg */
#define RGA3_WIN0_RD_CTRL_OFFSET		 0x000
#define RGA3_WIN0_Y_BASE_OFFSET			 0x010
#define RGA3_WIN0_U_BASE_OFFSET			 0x014
#define RGA3_WIN0_V_BASE_OFFSET			 0x018
#define RGA3_WIN0_VIR_STRIDE_OFFSET		 0x01c
#define RGA3_WIN0_FBC_OFF_OFFSET		 0x020
#define RGA3_WIN0_SRC_SIZE_OFFSET		 0x024
#define RGA3_WIN0_ACT_OFF_OFFSET		 0x028
#define RGA3_WIN0_ACT_SIZE_OFFSET		 0x02c
#define RGA3_WIN0_DST_SIZE_OFFSET		 0x030
#define RGA3_WIN0_SCL_FAC_OFFSET		 0x034
#define RGA3_WIN0_UV_VIR_STRIDE_OFFSET	 0x038
#define RGA3_WIN1_RD_CTRL_OFFSET		 0x040
#define RGA3_WIN1_Y_BASE_OFFSET			 0x050
#define RGA3_WIN1_U_BASE_OFFSET			 0x054
#define RGA3_WIN1_V_BASE_OFFSET			 0x058
#define RGA3_WIN1_VIR_STRIDE_OFFSET		 0x05c
#define RGA3_WIN1_FBC_OFF_OFFSET		 0x060
#define RGA3_WIN1_SRC_SIZE_OFFSET		 0x064
#define RGA3_WIN1_ACT_OFF_OFFSET		 0x068
#define RGA3_WIN1_ACT_SIZE_OFFSET		 0x06c
#define RGA3_WIN1_DST_SIZE_OFFSET		 0x070
#define RGA3_WIN1_SCL_FAC_OFFSET		 0x074
#define RGA3_WIN1_UV_VIR_STRIDE_OFFSET	 0x078
#define RGA3_OVLP_CTRL_OFFSET			 0x080
#define RGA3_OVLP_OFF_OFFSET			 0x084
#define RGA3_OVLP_TOP_KEY_MIN_OFFSET	 0x088
#define RGA3_OVLP_TOP_KEY_MAX_OFFSET	 0x08c
#define RGA3_OVLP_TOP_CTRL_OFFSET		 0x090
#define RGA3_OVLP_BOT_CTRL_OFFSET		 0x094
#define RGA3_OVLP_TOP_ALPHA_OFFSET		 0x098
#define RGA3_OVLP_BOT_ALPHA_OFFSET		 0x09c
#define RGA3_WR_CTRL_OFFSET				 0x0a0
#define RGA3_WR_FBCE_CTRL_OFFSET		 0x0a4
#define RGA3_WR_VIR_STRIDE_OFFSET		 0x0a8
#define RGA3_WR_PL_VIR_STRIDE_OFFSET	 0x0ac
#define RGA3_WR_Y_BASE_OFFSET			 0x0b0
#define RGA3_WR_U_BASE_OFFSET			 0x0b4
#define RGA3_WR_V_BASE_OFFSET			 0x0b8
#define RGA3_MMU_DTE_ADDR_OFFSET		 0x0f00
#define RGA3_MMU_STATUS_OFFSET			 0x0f04
#define RGA3_MMU_COMMAND_OFFSET			 0x0f08
#define RGA3_MMU_PAGE_FAULT_ADDR_OFFSET	 0x0f0c
#define RGA3_MMU_ZAP_ONE_LINE_OFFSET	 0x0f10
#define RGA3_MMU_INT_RAWSTAT_OFFSET		 0x0f14
#define RGA3_MMU_INT_CLEAR_OFFSET		 0x0f18
#define RGA3_MMU_INT_MASK_OFFSET		 0x0f1c
#define RGA3_MMU_INT_STATUS_OFFSET		 0x0f20
#define RGA3_MMU_AUTO_GATING_OFFSET		 0x0f24
#define RGA3_MMU_REG_LOAD_EN_OFFSET		 0x0f28

int rga3_gen_reg_info(unsigned char *base, struct rga3_req *msg);
void rga_cmd_to_rga3_cmd(struct rga_req *req_rga, struct rga3_req *req);
//void RGA_MSG_2_RGA3_MSG_32(struct rga_req_32 *req_rga, struct rga3_req *req);

void rga3_soft_reset(struct rga_scheduler_t *scheduler);
int rga3_set_reg(struct rga_job *job, struct rga_scheduler_t *scheduler);
int rga3_init_reg(struct rga_job *job);
int rga3_get_version(struct rga_scheduler_t *scheduler);

#endif

