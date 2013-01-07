/*
 * Copyright (c) 2011 - 2012 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * Register definition file for Samsung G-Scaler driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef REGS_GSC_H_
#define REGS_GSC_H_

/* G-Scaler enable */
#define GSC_ENABLE			0x00
#define GSC_ENABLE_OP_STATUS		(1 << 2)
#define GSC_ENABLE_SFR_UPDATE		(1 << 1)
#define GSC_ENABLE_ON			(1 << 0)

/* G-Scaler S/W reset */
#define GSC_SW_RESET			0x04
#define GSC_SW_RESET_SRESET		(1 << 0)

/* G-Scaler IRQ */
#define GSC_IRQ				0x08
#define GSC_IRQ_STATUS_OR_IRQ		(1 << 17)
#define GSC_IRQ_STATUS_FRM_DONE_IRQ	(1 << 16)
#define GSC_IRQ_FRMDONE_MASK		(1 << 1)
#define GSC_IRQ_ENABLE			(1 << 0)

/* G-Scaler input control */
#define GSC_IN_CON			0x10
#define GSC_IN_ROT_MASK			(7 << 16)
#define GSC_IN_ROT_270			(7 << 16)
#define GSC_IN_ROT_90_YFLIP		(6 << 16)
#define GSC_IN_ROT_90_XFLIP		(5 << 16)
#define GSC_IN_ROT_90			(4 << 16)
#define GSC_IN_ROT_180			(3 << 16)
#define GSC_IN_ROT_YFLIP		(2 << 16)
#define GSC_IN_ROT_XFLIP		(1 << 16)
#define GSC_IN_RGB_TYPE_MASK		(3 << 14)
#define GSC_IN_RGB_HD_NARROW		(3 << 14)
#define GSC_IN_RGB_HD_WIDE		(2 << 14)
#define GSC_IN_RGB_SD_NARROW		(1 << 14)
#define GSC_IN_RGB_SD_WIDE		(0 << 14)
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
#define GSC_IN_TILE_MODE		(1 << 3)
#define GSC_IN_LOCAL_SEL_MASK		(3 << 1)
#define GSC_IN_PATH_MASK		(1 << 0)
#define GSC_IN_PATH_MEMORY		(0 << 0)

/* G-Scaler source image size */
#define GSC_SRCIMG_SIZE			0x14
#define GSC_SRCIMG_HEIGHT(x)		((x) << 16)
#define GSC_SRCIMG_WIDTH(x)		((x) << 0)

/* G-Scaler source image offset */
#define GSC_SRCIMG_OFFSET		0x18
#define GSC_SRCIMG_OFFSET_Y(x)		((x) << 16)
#define GSC_SRCIMG_OFFSET_X(x)		((x) << 0)

/* G-Scaler cropped source image size */
#define GSC_CROPPED_SIZE		0x1c
#define GSC_CROPPED_HEIGHT(x)		((x) << 16)
#define GSC_CROPPED_WIDTH(x)		((x) << 0)

/* G-Scaler output control */
#define GSC_OUT_CON			0x20
#define GSC_OUT_GLOBAL_ALPHA_MASK	(0xff << 24)
#define GSC_OUT_GLOBAL_ALPHA(x)		((x) << 24)
#define GSC_OUT_RGB_TYPE_MASK		(3 << 10)
#define GSC_OUT_RGB_HD_WIDE		(3 << 10)
#define GSC_OUT_RGB_HD_NARROW		(2 << 10)
#define GSC_OUT_RGB_SD_WIDE		(1 << 10)
#define GSC_OUT_RGB_SD_NARROW		(0 << 10)
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
#define GSC_OUT_TILE_MODE		(1 << 1)
#define GSC_OUT_PATH_MASK		(1 << 0)
#define GSC_OUT_PATH_LOCAL		(1 << 0)
#define GSC_OUT_PATH_MEMORY		(0 << 0)

/* G-Scaler scaled destination image size */
#define GSC_SCALED_SIZE			0x24
#define GSC_SCALED_HEIGHT(x)		((x) << 16)
#define GSC_SCALED_WIDTH(x)		((x) << 0)

/* G-Scaler pre scale ratio */
#define GSC_PRE_SCALE_RATIO		0x28
#define GSC_PRESC_SHFACTOR(x)		((x) << 28)
#define GSC_PRESC_V_RATIO(x)		((x) << 16)
#define GSC_PRESC_H_RATIO(x)		((x) << 0)

/* G-Scaler main scale horizontal ratio */
#define GSC_MAIN_H_RATIO		0x2c
#define GSC_MAIN_H_RATIO_VALUE(x)	((x) << 0)

/* G-Scaler main scale vertical ratio */
#define GSC_MAIN_V_RATIO		0x30
#define GSC_MAIN_V_RATIO_VALUE(x)	((x) << 0)

/* G-Scaler destination image size */
#define GSC_DSTIMG_SIZE			0x40
#define GSC_DSTIMG_HEIGHT(x)		((x) << 16)
#define GSC_DSTIMG_WIDTH(x)		((x) << 0)

/* G-Scaler destination image offset */
#define GSC_DSTIMG_OFFSET		0x44
#define GSC_DSTIMG_OFFSET_Y(x)		((x) << 16)
#define GSC_DSTIMG_OFFSET_X(x)		((x) << 0)

/* G-Scaler input y address mask */
#define GSC_IN_BASE_ADDR_Y_MASK		0x4c
/* G-Scaler input y base address */
#define GSC_IN_BASE_ADDR_Y(n)		(0x50 + (n) * 0x4)

/* G-Scaler input cb address mask */
#define GSC_IN_BASE_ADDR_CB_MASK	0x7c
/* G-Scaler input cb base address */
#define GSC_IN_BASE_ADDR_CB(n)		(0x80 + (n) * 0x4)

/* G-Scaler input cr address mask */
#define GSC_IN_BASE_ADDR_CR_MASK	0xac
/* G-Scaler input cr base address */
#define GSC_IN_BASE_ADDR_CR(n)		(0xb0 + (n) * 0x4)

/* G-Scaler output y address mask */
#define GSC_OUT_BASE_ADDR_Y_MASK	0x10c
/* G-Scaler output y base address */
#define GSC_OUT_BASE_ADDR_Y(n)		(0x110 + (n) * 0x4)

/* G-Scaler output cb address mask */
#define GSC_OUT_BASE_ADDR_CB_MASK	0x15c
/* G-Scaler output cb base address */
#define GSC_OUT_BASE_ADDR_CB(n)		(0x160 + (n) * 0x4)

/* G-Scaler output cr address mask */
#define GSC_OUT_BASE_ADDR_CR_MASK	0x1ac
/* G-Scaler output cr base address */
#define GSC_OUT_BASE_ADDR_CR(n)		(0x1b0 + (n) * 0x4)

#endif /* REGS_GSC_H_ */
