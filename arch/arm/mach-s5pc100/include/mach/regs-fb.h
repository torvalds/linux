/* arch/arm/mach-s5pc100/include/mach/regs-fb.h
 *
 * Copyright 2009 Samsung Electronics Co.
 *   Pawel Osciak <p.osciak@samsung.com>
 *
 * Framebuffer register definitions for Samsung S5PC100.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifndef __ASM_ARCH_REGS_FB_H
#define __ASM_ARCH_REGS_FB_H __FILE__

#include <plat/regs-fb-v4.h>

/* VP1 interface timing control */
#define VP1CON0						(0x118)
#define VP1_RATECON_EN					(1 << 31)
#define VP1_CLKRATE_MASK				(0xff)

#define VP1CON1						(0x11c)
#define VP1_VTREGCON_EN					(1 << 31)
#define VP1_VBPD_MASK					(0xfff)
#define VP1_VBPD_SHIFT					(16)


#define WPALCON_H					(0x19c)
#define WPALCON_L					(0x1a0)

/* Pallete contro for WPAL0 and WPAL1 is the same as in S3C64xx, but
 * different for WPAL2-4
 */
/* In WPALCON_L (aka WPALCON) */
#define WPALCON_W1PAL_32BPP_A888			(0x7 << 3)
#define WPALCON_W0PAL_32BPP_A888			(0x7 << 0)

/* To set W2PAL-W4PAL consist of one bit from WPALCON_L and two from WPALCON_H,
 * e.g. W2PAL[2..0] is made of (WPALCON_H[10..9], WPALCON_L[6]).
 */
#define WPALCON_L_WxPAL_L_MASK				(0x1)
#define WPALCON_L_W2PAL_L_SHIFT				(6)
#define WPALCON_L_W3PAL_L_SHIFT				(7)
#define WPALCON_L_W4PAL_L_SHIFT				(8)

#define WPALCON_L_WxPAL_H_MASK				(0x3)
#define WPALCON_H_W2PAL_H_SHIFT				(9)
#define WPALCON_H_W3PAL_H_SHIFT				(13)
#define WPALCON_H_W4PAL_H_SHIFT				(17)

/* Per-window alpha value registers */
/* For window 0 8-bit alpha values are in VIDW0ALPHAx,
 * for windows 1-4 alpha values consist of two parts, the 4 low bits are
 * taken from VIDWxALPHAx and 4 high bits are from VIDOSDxC,
 * e.g. WIN1_ALPHA0_B[7..0] = (VIDOSD1C[3..0], VIDW1ALPHA0[3..0])
 */
#define VIDWxALPHA0(_win)				(0x200 + (_win * 8))
#define VIDWxALPHA1(_win)				(0x204 + (_win * 8))

/* Only for window 0 in VIDW0ALPHAx. */
#define VIDW0ALPHAx_R(_x)				((_x) << 16)
#define VIDW0ALPHAx_R_MASK				(0xff << 16)
#define VIDW0ALPHAx_R_SHIFT				(16)
#define VIDW0ALPHAx_G(_x)				((_x) << 8)
#define VIDW0ALPHAx_G_MASK				(0xff << 8)
#define VIDW0ALPHAx_G_SHIFT				(8)
#define VIDW0ALPHAx_B(_x)				((_x) << 0)
#define VIDW0ALPHAx_B_MASK				(0xff << 0)
#define VIDW0ALPHAx_B_SHIFT				(0)

/* Low 4 bits of alpha0-1 for windows 1-4 */
#define VIDW14ALPHAx_R_L(_x)				((_x) << 16)
#define VIDW14ALPHAx_R_L_MASK				(0xf << 16)
#define VIDW14ALPHAx_R_L_SHIFT				(16)
#define VIDW14ALPHAx_G_L(_x)				((_x) << 8)
#define VIDW14ALPHAx_G_L_MASK				(0xf << 8)
#define VIDW14ALPHAx_G_L_SHIFT				(8)
#define VIDW14ALPHAx_B_L(_x)				((_x) << 0)
#define VIDW14ALPHAx_B_L_MASK				(0xf << 0)
#define VIDW14ALPHAx_B_L_SHIFT				(0)


/* Per-window blending equation control registers */
#define BLENDEQx(_win)					(0x244 + ((_win) * 4))
#define BLENDEQ1					(0x244)
#define BLENDEQ2					(0x248)
#define BLENDEQ3					(0x24c)
#define BLENDEQ4					(0x250)

#define BLENDEQx_Q_FUNC(_x)				((_x) << 18)
#define BLENDEQx_Q_FUNC_MASK				(0xf << 18)
#define BLENDEQx_P_FUNC(_x)				((_x) << 12)
#define BLENDEQx_P_FUNC_MASK				(0xf << 12)
#define BLENDEQx_B_FUNC(_x)				((_x) << 6)
#define BLENDEQx_B_FUNC_MASK				(0xf << 6)
#define BLENDEQx_A_FUNC(_x)				((_x) << 0)
#define BLENDEQx_A_FUNC_MASK				(0xf << 0)

#define BLENDCON					(0x260)
#define BLENDCON_8BIT_ALPHA				(1 << 0)

/* Per-window palette base addresses (start of palette memory).
 * Each window palette area consists of 256 32-bit entries.
 * START is the first address (entry 0th), END is the address of 255th entry.
 */
#define WIN0_PAL_BASE					(0x2400)
#define WIN0_PAL_END					(0x27fc)
#define WIN1_PAL_BASE					(0x2800)
#define WIN1_PAL_END					(0x2bfc)
#define WIN2_PAL_BASE					(0x2c00)
#define WIN2_PAL_END					(0x2ffc)
#define WIN3_PAL_BASE					(0x3000)
#define WIN3_PAL_END					(0x33fc)
#define WIN4_PAL_BASE					(0x3400)
#define WIN4_PAL_END					(0x37fc)

#define WIN0_PAL(_entry)			(WIN0_PAL_BASE + ((_entry) * 4))
#define WIN1_PAL(_entry)			(WIN1_PAL_BASE + ((_entry) * 4))
#define WIN2_PAL(_entry)			(WIN2_PAL_BASE + ((_entry) * 4))
#define WIN3_PAL(_entry)			(WIN3_PAL_BASE + ((_entry) * 4))
#define WIN4_PAL(_entry)			(WIN4_PAL_BASE + ((_entry) * 4))

static inline unsigned int s3c_fb_pal_reg(unsigned int window, int reg)
{
	switch (window) {
	case 0: return WIN0_PAL(reg);
	case 1: return WIN1_PAL(reg);
	case 2: return WIN2_PAL(reg);
	case 3: return WIN3_PAL(reg);
	case 4: return WIN4_PAL(reg);
	}

	BUG();
}


#endif /* __ASM_ARCH_REGS_FB_H */

