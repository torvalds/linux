/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef UDL_PROTO_H
#define UDL_PROTO_H

#define UDL_COLORDEPTH_16BPP		0

/* On/Off for driving the DisplayLink framebuffer to the display */
#define UDL_REG_BLANKMODE		0x1f
#define UDL_BLANKMODE_ON		0x00 /* hsync and vsync on, visible */
#define UDL_BLANKMODE_BLANKED		0x01 /* hsync and vsync on, blanked */
#define UDL_BLANKMODE_VSYNC_OFF		0x03 /* vsync off, blanked */
#define UDL_BLANKMODE_HSYNC_OFF		0x05 /* hsync off, blanked */
#define UDL_BLANKMODE_POWERDOWN		0x07 /* powered off; requires modeset */

#endif
