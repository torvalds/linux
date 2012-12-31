/* linux/arch/arm/mach-exynos/include/mach/regs-fimg2d3x.h
 *
 * Copyright (c) 2010 Samsung Electronics Co., Ltd.
 *	http://www.samsung.com/
 *
 * Register Definitions for Samsung Graphics 2D Hardware
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifndef __ASM_ARCH_REGS_FIMG2D3X_H
#define __ASM_ARCH_REGS_FIMG2D3X_H __FILE__

/* Macros */
#define FIMG2D_ADDR(v)			((v) << 0)
#define FIMG2D_STRIDE(v)		(((v) & (0xffff)) << 0)
#define FIMG2D_OFFSET(x, y)		((((y) & 0x1fff) << 16) | (((x) & 0x1fff) << 0))
#define FIMG2D_SIZE(w, h)		((((h) & 0x1fff) << 16) | (((w) & 0x1fff) << 0))
#define FIMG2D_COLOR(v)			((v) << 0)

/* Registers */
#define FIMG2D_SOFT_RESET_REG		(0x000)
#define FIMG2D_INTEN_REG		(0x004)
#define FIMG2D_INTC_PEND_REG		(0x00c)
#define FIMG2D_FIFO_STAT_REG		(0x010)
#define FIMG2D_AXI_ID_MODE_REG		(0x014)
#define FIMG2D_CACHECTL_REG		(0x018)
#define FIMG2D_BITBLT_START_REG		(0x100)
#define FIMG2D_BITBLT_COMMAND_REG	(0x104)
#define FIMG2D_ROTATE_REG		(0x200)
#define FIMG2D_SRC_MSK_DIRECT_REG	(0x204)
#define FIMG2D_DST_PAT_DIRECT_REG	(0x208)
#define FIMG2D_SRC_SELECT_REG		(0x300)
#define FIMG2D_SRC_BASE_ADDR_REG	(0x304)
#define FIMG2D_SRC_STRIDE_REG		(0x308)
#define FIMG2D_SRC_COLOR_MODE_REG	(0x30c)
#define FIMG2D_SRC_LEFT_TOP_REG		(0x310)
#define FIMG2D_SRC_RIGHT_BOTTOM_REG	(0x314)
#define FIMG2D_DST_SELECT_REG		(0x400)
#define FIMG2D_DST_BASE_ADDR_REG	(0x404)
#define FIMG2D_DST_STRIDE_REG		(0x408)
#define FIMG2D_DST_COLOR_MODE_REG	(0x40c)
#define FIMG2D_DST_LEFT_TOP_REG		(0x410)
#define FIMG2D_DST_RIGHT_BOTTOM_REG	(0x414)
#define FIMG2D_PAT_BASE_ADDR_REG	(0x500)
#define FIMG2D_PAT_SIZE_REG		(0x504)
#define FIMG2D_PAT_COLOR_MODE_REG	(0x508)
#define FIMG2D_PAT_OFFSET_REG		(0x50c)
#define FIMG2D_PAT_STRIDE_REG		(0x510)
#define FIMG2D_CW_LT_REG		(0x600)
#define FIMG2D_CW_RB_REG		(0x604)
#define FIMG2D_THIRD_OPERAND_REG	(0x610)
#define FIMG2D_ROP4_REG			(0x614)
#define FIMG2D_ALPHA_REG		(0x618)
#define FIMG2D_FG_COLOR_REG		(0x700)
#define FIMG2D_BG_COLOR_REG		(0x704)
#define FIMG2D_BS_COLOR_REG		(0x708)
#define FIMG2D_SRC_COLORKEY_CTRL_REG	(0x710)
#define FIMG2D_SRC_COLORKEY_DR_MIN_REG	(0x714)
#define FIMG2D_SRC_COLORKEY_DR_MAX_REG	(0x718)
#define FIMG2D_DST_COLORKEY_CTRL_REG	(0x71c)
#define FIMG2D_DST_COLORKEY_DR_MIN_REG	(0x720)
#define FIMG2D_DST_COLORKEY_DR_MAX_REG	(0x724)

/* Bit Definitions */

/* SOFT_RESET_REG */
#define FIMG2D_SOFT_RESET		(1 << 0)

/* INTEN_REG */
#define FIMG2D_INT_EN			(1 << 0)

/* INTC_PEND_REG */
#define FIMG2D_INTP_CMD_FIN		(1 << 0)

/* FIFO_STAT_REG */
#define FIMG2D_CMD_FIN			(1 << 0)

/* CACHECTL_REG */
#define FIMG2D_PATCACHE_CLEAR		(1 << 2)
#define FIMG2D_SRCBUFFER_CLEAR		(1 << 1)
#define FIMG2D_MASKBUFFER_CLEAR		(1 << 0)

/* BITBLT_START_REG */
#define FIMG2D_START_BITBLT		(1 << 0)

/* BITBLT_COMMAND_REG */
#define FIMG2D_NONPREBLEND_DISABLE	(0 << 22)
#define FIMG2D_NONPREBLEND_CONSTANT	(1 << 22)
#define FIMG2D_NONPREBLEND_PERPIXEL	(2 << 22)
#define FIMG2D_NONPREBLEND_MASK		(3 << 22)

#define FIMG2D_ALPHA_MODE_NONE		(0 << 20)
#define FIMG2D_ALPHA_MODE_ALPHA		(1 << 20)
#define FIMG2D_ALPHA_MODE_FADING	(2 << 20)
#define FIMG2D_ALPHA_MODE_MASK		(3 << 20)

#define FIMG2D_ENABLE_CW		(1 << 8)
#define FIMG2D_ENABLE_STRETCH		(1 << 4)
#define FIMG2D_ENABLE_MASK		(1 << 0)

/* ROTATE_REG */
#define FIMG2D_ROTATE_90_ENABLE		(1 << 0)

/* SRC_MSK_DIRECT_REG */
#define FIMG2D_MSK_X_DIR_NEGATIVE	(1 << 4)
#define FIMG2D_MSK_Y_DIR_NEGATIVE	(1 << 5)
#define FIMG2D_MSK_DIR_MASK		(3 << 4)

#define FIMG2D_SRC_X_DIR_NEGATIVE	(1 << 0)
#define FIMG2D_SRC_Y_DIR_NEGATIVE	(1 << 1)
#define FIMG2D_SRC_DIR_MASK		(3 << 0)

/* DST_PAT_DIRECT_REG */
#define FIMG2D_PAT_X_DIR_NEGATIVE	(1 << 4)
#define FIMG2D_PAT_Y_DIR_NEGATIVE	(1 << 5)
#define FIMG2D_PAT_DIR_MASK		(3 << 4)

#define FIMG2D_DST_X_DIR_NEGATIVE	(1 << 0)
#define FIMG2D_DST_Y_DIR_NEGATIVE	(1 << 1)
#define FIMG2D_DST_DIR_MASK		(3 << 0)

/* XXX_SELECT_REG */
#define FIMG2D_IMG_TYPE_MEMORY		(0 << 0)
#define FIMG2D_IMG_TYPE_FGCOLOR		(1 << 0)
#define FIMG2D_IMG_TYPE_BGCOLOR		(2 << 0)
#define FIMG2D_IMG_TYPE_MASK		(3 << 0)

/* XXX_COLOR_MODE_REG */
#define FIMG2D_CHANNEL_ORDER_SHIFT	(4)
#define FIMG2D_COLOR_FORMAT_SHIFT	(0)

/* XXX_LEFT_TOP_REG & XXX_RIGHT_BOTTOM_REG */
#define FIMG2D_COORD_TOP_Y_SHIFT	(16)
#define FIMG2D_COORD_LEFT_X_SHIFT	(0)
#define FIMG2D_COORD_BOTTOM_Y_SHIFT	(16)
#define FIMG2D_COORD_RIGHT_X_SHIFT	(0)

/* THIRD_OPERAND_REG */
#define FIMG2D_MASKED_OPR3_PATTERN	(0 << 4)
#define FIMG2D_MASKED_OPR3_FGCOLOR	(1 << 4)
#define FIMG2D_MASKED_OPR3_BGCOLOR	(2 << 4)
#define FIMG2D_MASKED_OPR3_MASK		(3 << 4)

#define FIMG2D_UNMASKED_OPR3_PATTERN	(0 << 0)
#define FIMG2D_UNMASKED_OPR3_FGCOLOR	(1 << 0)
#define FIMG2D_UNMASKED_OPR3_BGCOLOR	(2 << 0)
#define FIMG2D_UNMASKED_OPR3_MASK	(3 << 0)

/* ROP4_REG */
#define FIMG2D_MASKED_ROP3_SHIFT	(8)
#define FIMG2D_UNMASKED_ROP3_SHIFT	(0)

/* ALPHA_REG */
#define FIMG2D_FADING_OFFSET_SHIFT	(8)
#define FIMG2D_FADING_OFFSET_MASK	(0xff)
#define FIMG2D_ALPHA_VALUE_SHIFT	(0)
#define FIMG2D_ALPHA_VALUE_MASK		(0xff)

#endif /* __ASM_MACH_REGS_FIMG2D3X_H */
