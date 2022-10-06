/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef UDL_PROTO_H
#define UDL_PROTO_H

#define UDL_COLORDEPTH_16BPP		0

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

#endif
