/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef UDL_PROTO_H
#define UDL_PROTO_H

#include <linux/bits.h>

/* Color depth */
#define UDL_REG_COLORDEPTH		0x00
#define UDL_COLORDEPTH_16BPP		0
#define UDL_COLORDEPTH_24BPP		1

/* Display-mode settings */
#define UDL_REG_XDISPLAYSTART		0x01
#define UDL_REG_XDISPLAYEND		0x03
#define UDL_REG_YDISPLAYSTART		0x05
#define UDL_REG_YDISPLAYEND		0x07
#define UDL_REG_XENDCOUNT		0x09
#define UDL_REG_HSYNCSTART		0x0b
#define UDL_REG_HSYNCEND		0x0d
#define UDL_REG_HPIXELS			0x0f
#define UDL_REG_YENDCOUNT		0x11
#define UDL_REG_VSYNCSTART		0x13
#define UDL_REG_VSYNCEND		0x15
#define UDL_REG_VPIXELS			0x17
#define UDL_REG_PIXELCLOCK5KHZ		0x1b

/* On/Off for driving the DisplayLink framebuffer to the display */
#define UDL_REG_BLANKMODE		0x1f
#define UDL_BLANKMODE_ON		0x00 /* hsync and vsync on, visible */
#define UDL_BLANKMODE_BLANKED		0x01 /* hsync and vsync on, blanked */
#define UDL_BLANKMODE_VSYNC_OFF		0x03 /* vsync off, blanked */
#define UDL_BLANKMODE_HSYNC_OFF		0x05 /* hsync off, blanked */
#define UDL_BLANKMODE_POWERDOWN		0x07 /* powered off; requires modeset */

/* Framebuffer address */
#define UDL_REG_BASE16BPP_ADDR2		0x20
#define UDL_REG_BASE16BPP_ADDR1		0x21
#define UDL_REG_BASE16BPP_ADDR0		0x22
#define UDL_REG_BASE8BPP_ADDR2		0x26
#define UDL_REG_BASE8BPP_ADDR1		0x27
#define UDL_REG_BASE8BPP_ADDR0		0x28

#define UDL_BASE_ADDR0_MASK		GENMASK(7, 0)
#define UDL_BASE_ADDR1_MASK		GENMASK(15, 8)
#define UDL_BASE_ADDR2_MASK		GENMASK(23, 16)

/* Lock/unlock video registers */
#define UDL_REG_VIDREG			0xff
#define UDL_VIDREG_LOCK			0x00
#define UDL_VIDREG_UNLOCK		0xff

#endif
