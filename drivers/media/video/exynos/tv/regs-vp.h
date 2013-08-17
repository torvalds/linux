/*
 * Copyright (c) 2010-2011 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * Video processor register header file for Samsung Mixer driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef SAMSUNG_REGS_VP_H
#define SAMSUNG_REGS_VP_H

/*
 * Register part
 */

#define VP_ENABLE			0x0000
#define VP_SRESET			0x0004
#define VP_SHADOW_UPDATE		0x0008
#define VP_FIELD_ID			0x000C
#define VP_MODE				0x0010
#define VP_IMG_SIZE_Y			0x0014
#define VP_IMG_SIZE_C			0x0018
#define VP_PER_RATE_CTRL		0x001C
#define VP_TOP_Y_PTR			0x0028
#define VP_BOT_Y_PTR			0x002C
#define VP_TOP_C_PTR			0x0030
#define VP_BOT_C_PTR			0x0034
#define VP_ENDIAN_MODE			0x03CC
#define VP_SRC_H_POSITION		0x0044
#define VP_SRC_V_POSITION		0x0048
#define VP_SRC_WIDTH			0x004C
#define VP_SRC_HEIGHT			0x0050
#define VP_DST_H_POSITION		0x0054
#define VP_DST_V_POSITION		0x0058
#define VP_DST_WIDTH			0x005C
#define VP_DST_HEIGHT			0x0060
#define VP_H_RATIO			0x0064
#define VP_V_RATIO			0x0068
#define VP_POLY8_Y0_LL			0x006C
#define VP_POLY4_Y0_LL			0x00EC
#define VP_POLY4_C0_LL			0x012C

/*
 * Bit definition part
 */

/* generates mask for range of bits */

#define VP_MASK(high_bit, low_bit) \
	(((2 << ((high_bit) - (low_bit))) - 1) << (low_bit))

#define VP_MASK_VAL(val, high_bit, low_bit) \
	(((val) << (low_bit)) & VP_MASK(high_bit, low_bit))

 /* VP_ENABLE */
#define VP_ENABLE_ON			(1 << 0)

/* VP_SRESET */
#define VP_SRESET_PROCESSING		(1 << 0)

/* VP_SHADOW_UPDATE */
#define VP_SHADOW_UPDATE_ENABLE		(1 << 0)

/* VP_MODE */
#define VP_MODE_NV12			(0 << 6)
#define VP_MODE_NV21			(1 << 6)
#define VP_MODE_LINE_SKIP		(1 << 5)
#define VP_MODE_MEM_LINEAR		(0 << 4)
#define VP_MODE_MEM_TILED		(1 << 4)
#define VP_MODE_FMT_MASK		(5 << 4)
#define VP_MODE_FIELD_ID_AUTO_TOGGLING	(1 << 2)
#define VP_MODE_2D_IPC			(1 << 1)

/* VP_IMG_SIZE_Y */
/* VP_IMG_SIZE_C */
#define VP_IMG_HSIZE(x)			VP_MASK_VAL(x, 29, 16)
#define VP_IMG_VSIZE(x)			VP_MASK_VAL(x, 13, 0)

/* VP_SRC_H_POSITION */
#define VP_SRC_H_POSITION_VAL(x)	VP_MASK_VAL(x, 14, 4)

/* VP_ENDIAN_MODE */
#define VP_ENDIAN_MODE_LITTLE		(1 << 0)

#endif /* SAMSUNG_REGS_VP_H */
