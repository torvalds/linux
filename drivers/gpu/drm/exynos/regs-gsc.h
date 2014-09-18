/* linux/drivers/gpu/drm/exynos/regs-gsc.h
 *
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * Register definition file for Samsung G-Scaler driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef EXYNOS_REGS_GSC_H_
#define EXYNOS_REGS_GSC_H_

/* G-Scaler enable */
#define GSC_ENABLE			0x00
#define GSC_ENABLE_PP_UPDATE_TIME_MASK	(1 << 9)
#define GSC_ENABLE_PP_UPDATE_TIME_CURR	(0 << 9)
#define GSC_ENABLE_PP_UPDATE_TIME_EOPAS	(1 << 9)
#define GSC_ENABLE_CLK_GATE_MODE_MASK	(1 << 8)
#define GSC_ENABLE_CLK_GATE_MODE_FREE	(1 << 8)
#define GSC_ENABLE_IPC_MODE_MASK	(1 << 7)
#define GSC_ENABLE_NORM_MODE		(0 << 7)
#define GSC_ENABLE_IPC_MODE		(1 << 7)
#define GSC_ENABLE_PP_UPDATE_MODE_MASK	(1 << 6)
#define GSC_ENABLE_PP_UPDATE_FIRE_MODE	(1 << 6)
#define GSC_ENABLE_IN_PP_UPDATE		(1 << 5)
#define GSC_ENABLE_ON_CLEAR_MASK	(1 << 4)
#define GSC_ENABLE_ON_CLEAR_ONESHOT	(1 << 4)
#define GSC_ENABLE_QOS_ENABLE		(1 << 3)
#define GSC_ENABLE_OP_STATUS		(1 << 2)
#define GSC_ENABLE_SFR_UPDATE		(1 << 1)
#define GSC_ENABLE_ON			(1 << 0)

/* G-Scaler S/W reset */
#define GSC_SW_RESET			0x04
#define GSC_SW_RESET_SRESET		(1 << 0)

/* G-Scaler IRQ */
#define GSC_IRQ				0x08
#define GSC_IRQ_STATUS_OR_IRQ		(1 << 17)
#define GSC_IRQ_STATUS_OR_FRM_DONE	(1 << 16)
#define GSC_IRQ_OR_MASK			(1 << 2)
#define GSC_IRQ_FRMDONE_MASK		(1 << 1)
#define GSC_IRQ_ENABLE			(1 << 0)

/* G-Scaler input control */
#define GSC_IN_CON			0x10
#define GSC_IN_CHROM_STRIDE_SEL_MASK	(1 << 20)
#define GSC_IN_CHROM_STRIDE_SEPAR	(1 << 20)
#define GSC_IN_RB_SWAP_MASK		(1 << 19)
#define GSC_IN_RB_SWAP			(1 << 19)
#define GSC_IN_ROT_MASK			(7 << 16)
#define GSC_IN_ROT_270			(7 << 16)
#define GSC_IN_ROT_90_YFLIP		(6 << 16)
#define GSC_IN_ROT_90_XFLIP		(5 << 16)
#define GSC_IN_ROT_90			(4 << 16)
#define GSC_IN_ROT_180			(3 << 16)
#define GSC_IN_ROT_YFLIP		(2 << 16)
#define GSC_IN_ROT_XFLIP		(1 << 16)
#define GSC_IN_RGB_TYPE_MASK		(3 << 14)
#define GSC_IN_RGB_HD_WIDE		(3 << 14)
#define GSC_IN_RGB_HD_NARROW		(2 << 14)
#define GSC_IN_RGB_SD_WIDE		(1 << 14)
#define GSC_IN_RGB_SD_NARROW		(0 << 14)
#define GSC_IN_YUV422_1P_ORDER_MASK	(1 << 13)
#define GSC_IN_YUV422_1P_ORDER_LSB_Y	(0 << 13)
#define GSC_IN_YUV422_1P_OEDER_LSB_C	(1 << 13)
#define GSC_IN_CHROMA_ORDER_MASK	(1 << 12)
#define GSC_IN_CHROMA_ORDER_CBCR	(0 << 12)
#define GSC_IN_CHROMA_ORDER_CRCB	(1 << 12)
#define GSC_IN_FORMAT_MASK		(7 << 8)
#define GSC_IN_XRGB8888			(0 << 8)
#define GSC_IN_RGB565			(1 << 8)
#define GSC_IN_YUV420_2P		(2 << 8)
#define GSC_IN_YUV420_3P		(3 << 8)
#define GSC_IN_YUV422_1P		(4 << 8)
#define GSC_IN_YUV422_2P		(5 << 8)
#define GSC_IN_YUV422_3P		(6 << 8)
#define GSC_IN_TILE_TYPE_MASK		(1 << 4)
#define GSC_IN_TILE_C_16x8		(0 << 4)
#define GSC_IN_TILE_C_16x16		(1 << 4)
#define GSC_IN_TILE_MODE		(1 << 3)
#define GSC_IN_LOCAL_SEL_MASK		(3 << 1)
#define GSC_IN_LOCAL_CAM3		(3 << 1)
#define GSC_IN_LOCAL_FIMD_WB		(2 << 1)
#define GSC_IN_LOCAL_CAM1		(1 << 1)
#define GSC_IN_LOCAL_CAM0		(0 << 1)
#define GSC_IN_PATH_MASK		(1 << 0)
#define GSC_IN_PATH_LOCAL		(1 << 0)
#define GSC_IN_PATH_MEMORY		(0 << 0)

/* G-Scaler source image size */
#define GSC_SRCIMG_SIZE			0x14
#define GSC_SRCIMG_HEIGHT_MASK		(0x1fff << 16)
#define GSC_SRCIMG_HEIGHT(x)		((x) << 16)
#define GSC_SRCIMG_WIDTH_MASK		(0x3fff << 0)
#define GSC_SRCIMG_WIDTH(x)		((x) << 0)

/* G-Scaler source image offset */
#define GSC_SRCIMG_OFFSET		0x18
#define GSC_SRCIMG_OFFSET_Y_MASK	(0x1fff << 16)
#define GSC_SRCIMG_OFFSET_Y(x)		((x) << 16)
#define GSC_SRCIMG_OFFSET_X_MASK	(0x1fff << 0)
#define GSC_SRCIMG_OFFSET_X(x)		((x) << 0)

/* G-Scaler cropped source image size */
#define GSC_CROPPED_SIZE		0x1C
#define GSC_CROPPED_HEIGHT_MASK		(0x1fff << 16)
#define GSC_CROPPED_HEIGHT(x)		((x) << 16)
#define GSC_CROPPED_WIDTH_MASK		(0x1fff << 0)
#define GSC_CROPPED_WIDTH(x)		((x) << 0)

/* G-Scaler output control */
#define GSC_OUT_CON			0x20
#define GSC_OUT_GLOBAL_ALPHA_MASK	(0xff << 24)
#define GSC_OUT_GLOBAL_ALPHA(x)		((x) << 24)
#define GSC_OUT_CHROM_STRIDE_SEL_MASK	(1 << 13)
#define GSC_OUT_CHROM_STRIDE_SEPAR	(1 << 13)
#define GSC_OUT_RB_SWAP_MASK		(1 << 12)
#define GSC_OUT_RB_SWAP			(1 << 12)
#define GSC_OUT_RGB_TYPE_MASK		(3 << 10)
#define GSC_OUT_RGB_HD_NARROW		(3 << 10)
#define GSC_OUT_RGB_HD_WIDE		(2 << 10)
#define GSC_OUT_RGB_SD_NARROW		(1 << 10)
#define GSC_OUT_RGB_SD_WIDE		(0 << 10)
#define GSC_OUT_YUV422_1P_ORDER_MASK	(1 << 9)
#define GSC_OUT_YUV422_1P_ORDER_LSB_Y	(0 << 9)
#define GSC_OUT_YUV422_1P_OEDER_LSB_C	(1 << 9)
#define GSC_OUT_CHROMA_ORDER_MASK	(1 << 8)
#define GSC_OUT_CHROMA_ORDER_CBCR	(0 << 8)
#define GSC_OUT_CHROMA_ORDER_CRCB	(1 << 8)
#define GSC_OUT_FORMAT_MASK		(7 << 4)
#define GSC_OUT_XRGB8888		(0 << 4)
#define GSC_OUT_RGB565			(1 << 4)
#define GSC_OUT_YUV420_2P		(2 << 4)
#define GSC_OUT_YUV420_3P		(3 << 4)
#define GSC_OUT_YUV422_1P		(4 << 4)
#define GSC_OUT_YUV422_2P		(5 << 4)
#define GSC_OUT_YUV444			(7 << 4)
#define GSC_OUT_TILE_TYPE_MASK		(1 << 2)
#define GSC_OUT_TILE_C_16x8		(0 << 2)
#define GSC_OUT_TILE_C_16x16		(1 << 2)
#define GSC_OUT_TILE_MODE		(1 << 1)
#define GSC_OUT_PATH_MASK		(1 << 0)
#define GSC_OUT_PATH_LOCAL		(1 << 0)
#define GSC_OUT_PATH_MEMORY		(0 << 0)

/* G-Scaler scaled destination image size */
#define GSC_SCALED_SIZE			0x24
#define GSC_SCALED_HEIGHT_MASK		(0x1fff << 16)
#define GSC_SCALED_HEIGHT(x)		((x) << 16)
#define GSC_SCALED_WIDTH_MASK		(0x1fff << 0)
#define GSC_SCALED_WIDTH(x)		((x) << 0)

/* G-Scaler pre scale ratio */
#define GSC_PRE_SCALE_RATIO		0x28
#define GSC_PRESC_SHFACTOR_MASK		(7 << 28)
#define GSC_PRESC_SHFACTOR(x)		((x) << 28)
#define GSC_PRESC_V_RATIO_MASK		(7 << 16)
#define GSC_PRESC_V_RATIO(x)		((x) << 16)
#define GSC_PRESC_H_RATIO_MASK		(7 << 0)
#define GSC_PRESC_H_RATIO(x)		((x) << 0)

/* G-Scaler main scale horizontal ratio */
#define GSC_MAIN_H_RATIO		0x2C
#define GSC_MAIN_H_RATIO_MASK		(0xfffff << 0)
#define GSC_MAIN_H_RATIO_VALUE(x)	((x) << 0)

/* G-Scaler main scale vertical ratio */
#define GSC_MAIN_V_RATIO		0x30
#define GSC_MAIN_V_RATIO_MASK		(0xfffff << 0)
#define GSC_MAIN_V_RATIO_VALUE(x)	((x) << 0)

/* G-Scaler input chrominance stride */
#define GSC_IN_CHROM_STRIDE		0x3C
#define GSC_IN_CHROM_STRIDE_MASK	(0x3fff << 0)
#define GSC_IN_CHROM_STRIDE_VALUE(x)	((x) << 0)

/* G-Scaler destination image size */
#define GSC_DSTIMG_SIZE			0x40
#define GSC_DSTIMG_HEIGHT_MASK		(0x1fff << 16)
#define GSC_DSTIMG_HEIGHT(x)		((x) << 16)
#define GSC_DSTIMG_WIDTH_MASK		(0x1fff << 0)
#define GSC_DSTIMG_WIDTH(x)		((x) << 0)

/* G-Scaler destination image offset */
#define GSC_DSTIMG_OFFSET		0x44
#define GSC_DSTIMG_OFFSET_Y_MASK	(0x1fff << 16)
#define GSC_DSTIMG_OFFSET_Y(x)		((x) << 16)
#define GSC_DSTIMG_OFFSET_X_MASK	(0x1fff << 0)
#define GSC_DSTIMG_OFFSET_X(x)		((x) << 0)

/* G-Scaler output chrominance stride */
#define GSC_OUT_CHROM_STRIDE		0x48
#define GSC_OUT_CHROM_STRIDE_MASK	(0x3fff << 0)
#define GSC_OUT_CHROM_STRIDE_VALUE(x)	((x) << 0)

/* G-Scaler input y address mask */
#define GSC_IN_BASE_ADDR_Y_MASK		0x4C
/* G-Scaler input y base address */
#define GSC_IN_BASE_ADDR_Y(n)		(0x50 + (n) * 0x4)
/* G-Scaler input y base current address */
#define GSC_IN_BASE_ADDR_Y_CUR(n)	(0x60 + (n) * 0x4)

/* G-Scaler input cb address mask */
#define GSC_IN_BASE_ADDR_CB_MASK	0x7C
/* G-Scaler input cb base address */
#define GSC_IN_BASE_ADDR_CB(n)		(0x80 + (n) * 0x4)
/* G-Scaler input cb base current address */
#define GSC_IN_BASE_ADDR_CB_CUR(n)	(0x90 + (n) * 0x4)

/* G-Scaler input cr address mask */
#define GSC_IN_BASE_ADDR_CR_MASK	0xAC
/* G-Scaler input cr base address */
#define GSC_IN_BASE_ADDR_CR(n)		(0xB0 + (n) * 0x4)
/* G-Scaler input cr base current address */
#define GSC_IN_BASE_ADDR_CR_CUR(n)	(0xC0 + (n) * 0x4)

/* G-Scaler input address mask */
#define GSC_IN_CURR_ADDR_INDEX	(0xf << 24)
#define GSC_IN_CURR_GET_INDEX(x)	((x) >> 24)
#define GSC_IN_BASE_ADDR_PINGPONG(x)	((x) << 16)
#define GSC_IN_BASE_ADDR_MASK		(0xff << 0)

/* G-Scaler output y address mask */
#define GSC_OUT_BASE_ADDR_Y_MASK	0x10C
/* G-Scaler output y base address */
#define GSC_OUT_BASE_ADDR_Y(n)		(0x110 + (n) * 0x4)

/* G-Scaler output cb address mask */
#define GSC_OUT_BASE_ADDR_CB_MASK	0x15C
/* G-Scaler output cb base address */
#define GSC_OUT_BASE_ADDR_CB(n)		(0x160 + (n) * 0x4)

/* G-Scaler output cr address mask */
#define GSC_OUT_BASE_ADDR_CR_MASK	0x1AC
/* G-Scaler output cr base address */
#define GSC_OUT_BASE_ADDR_CR(n)		(0x1B0 + (n) * 0x4)

/* G-Scaler output address mask */
#define GSC_OUT_CURR_ADDR_INDEX		(0xf << 24)
#define GSC_OUT_CURR_GET_INDEX(x)	((x) >> 24)
#define GSC_OUT_BASE_ADDR_PINGPONG(x)	((x) << 16)
#define GSC_OUT_BASE_ADDR_MASK		(0xffff << 0)

/* G-Scaler horizontal scaling filter */
#define GSC_HCOEF(n, s, x)	(0x300 + (n) * 0x4 + (s) * 0x30 + (x) * 0x300)

/* G-Scaler vertical scaling filter */
#define GSC_VCOEF(n, s, x)	(0x200 + (n) * 0x4 + (s) * 0x30 + (x) * 0x300)

/* G-Scaler BUS control */
#define GSC_BUSCON			0xA78
#define GSC_BUSCON_INT_TIME_MASK	(1 << 8)
#define GSC_BUSCON_INT_DATA_TRANS	(0 << 8)
#define GSC_BUSCON_INT_AXI_RESPONSE	(1 << 8)
#define GSC_BUSCON_AWCACHE(x)		((x) << 4)
#define GSC_BUSCON_ARCACHE(x)		((x) << 0)

/* G-Scaler V position */
#define GSC_VPOSITION			0xA7C
#define GSC_VPOS_F(x)			((x) << 0)


/* G-Scaler clock initial count */
#define GSC_CLK_INIT_COUNT		0xC00
#define GSC_CLK_GATE_MODE_INIT_CNT(x)	((x) << 0)

/* G-Scaler clock snoop count */
#define GSC_CLK_SNOOP_COUNT		0xC04
#define GSC_CLK_GATE_MODE_SNOOP_CNT(x)	((x) << 0)

/* SYSCON. GSCBLK_CFG */
#define SYSREG_GSCBLK_CFG1		(S3C_VA_SYS + 0x0224)
#define GSC_BLK_DISP1WB_DEST(x)		(x << 10)
#define GSC_BLK_SW_RESET_WB_DEST(x)	(1 << (18 + x))
#define GSC_BLK_PXLASYNC_LO_MASK_WB(x)	(0 << (14 + x))
#define GSC_BLK_GSCL_WB_IN_SRC_SEL(x)	(1 << (2 * x))
#define SYSREG_GSCBLK_CFG2		(S3C_VA_SYS + 0x2000)
#define PXLASYNC_LO_MASK_CAMIF_GSCL(x)	(1 << (x))

#endif /* EXYNOS_REGS_GSC_H_ */
