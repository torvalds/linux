/*
 * stmp378x: PXP register definitions
 *
 * Copyright (c) 2008 Freescale Semiconductor
 * Copyright 2008 Embedded Alley Solutions, Inc All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */
#define REGS_PXP_BASE	(STMP3XXX_REGS_BASE + 0x2A000)
#define REGS_PXP_PHYS	0x8002A000
#define REGS_PXP_SIZE	0x2000

#define HW_PXP_CTRL		0x0
#define BM_PXP_CTRL_ENABLE	0x00000001
#define BP_PXP_CTRL_ENABLE	0
#define BM_PXP_CTRL_IRQ_ENABLE	0x00000002
#define BM_PXP_CTRL_OUTPUT_RGB_FORMAT	0x000000F0
#define BP_PXP_CTRL_OUTPUT_RGB_FORMAT	4
#define BM_PXP_CTRL_ROTATE	0x00000300
#define BP_PXP_CTRL_ROTATE	8
#define BM_PXP_CTRL_HFLIP	0x00000400
#define BM_PXP_CTRL_VFLIP	0x00000800
#define BM_PXP_CTRL_S0_FORMAT	0x0000F000
#define BP_PXP_CTRL_S0_FORMAT	12
#define BM_PXP_CTRL_SCALE	0x00040000
#define BM_PXP_CTRL_CROP	0x00080000

#define HW_PXP_STAT		0x10
#define BM_PXP_STAT_IRQ		0x00000001
#define BP_PXP_STAT_IRQ		0

#define HW_PXP_RGBBUF		0x20

#define HW_PXP_RGBSIZE		0x40
#define BM_PXP_RGBSIZE_HEIGHT	0x00000FFF
#define BP_PXP_RGBSIZE_HEIGHT	0
#define BM_PXP_RGBSIZE_WIDTH	0x00FFF000
#define BP_PXP_RGBSIZE_WIDTH	12

#define HW_PXP_S0BUF		0x50

#define HW_PXP_S0UBUF		0x60

#define HW_PXP_S0VBUF		0x70

#define HW_PXP_S0PARAM		0x80
#define BM_PXP_S0PARAM_HEIGHT	0x000000FF
#define BP_PXP_S0PARAM_HEIGHT	0
#define BM_PXP_S0PARAM_WIDTH	0x0000FF00
#define BP_PXP_S0PARAM_WIDTH	8
#define BM_PXP_S0PARAM_YBASE	0x00FF0000
#define BP_PXP_S0PARAM_YBASE	16
#define BM_PXP_S0PARAM_XBASE	0xFF000000
#define BP_PXP_S0PARAM_XBASE	24

#define HW_PXP_S0BACKGROUND	0x90

#define HW_PXP_S0CROP		0xA0
#define BM_PXP_S0CROP_HEIGHT	0x000000FF
#define BP_PXP_S0CROP_HEIGHT	0
#define BM_PXP_S0CROP_WIDTH	0x0000FF00
#define BP_PXP_S0CROP_WIDTH	8
#define BM_PXP_S0CROP_YBASE	0x00FF0000
#define BP_PXP_S0CROP_YBASE	16
#define BM_PXP_S0CROP_XBASE	0xFF000000
#define BP_PXP_S0CROP_XBASE	24

#define HW_PXP_S0SCALE		0xB0
#define BM_PXP_S0SCALE_XSCALE	0x00003FFF
#define BP_PXP_S0SCALE_XSCALE	0
#define BM_PXP_S0SCALE_YSCALE	0x3FFF0000
#define BP_PXP_S0SCALE_YSCALE	16

#define HW_PXP_CSCCOEFF0	0xD0

#define HW_PXP_CSCCOEFF1	0xE0

#define HW_PXP_CSCCOEFF2	0xF0

#define HW_PXP_S0COLORKEYLOW	0x180

#define HW_PXP_S0COLORKEYHIGH	0x190

#define HW_PXP_OL0		(0x200 + 0 * 0x40)
#define HW_PXP_OL1		(0x200 + 1 * 0x40)
#define HW_PXP_OL2		(0x200 + 2 * 0x40)
#define HW_PXP_OL3		(0x200 + 3 * 0x40)
#define HW_PXP_OL4		(0x200 + 4 * 0x40)
#define HW_PXP_OL5		(0x200 + 5 * 0x40)
#define HW_PXP_OL6		(0x200 + 6 * 0x40)
#define HW_PXP_OL7		(0x200 + 7 * 0x40)

#define HW_PXP_OLn		0x200

#define HW_PXP_OL0SIZE		(0x210 + 0 * 0x40)
#define HW_PXP_OL1SIZE		(0x210 + 1 * 0x40)
#define HW_PXP_OL2SIZE		(0x210 + 2 * 0x40)
#define HW_PXP_OL3SIZE		(0x210 + 3 * 0x40)
#define HW_PXP_OL4SIZE		(0x210 + 4 * 0x40)
#define HW_PXP_OL5SIZE		(0x210 + 5 * 0x40)
#define HW_PXP_OL6SIZE		(0x210 + 6 * 0x40)
#define HW_PXP_OL7SIZE		(0x210 + 7 * 0x40)

#define HW_PXP_OLnSIZE		0x210
#define BM_PXP_OLnSIZE_HEIGHT	0x000000FF
#define BP_PXP_OLnSIZE_HEIGHT	0
#define BM_PXP_OLnSIZE_WIDTH	0x0000FF00
#define BP_PXP_OLnSIZE_WIDTH	8

#define HW_PXP_OL0PARAM		(0x220 + 0 * 0x40)
#define HW_PXP_OL1PARAM		(0x220 + 1 * 0x40)
#define HW_PXP_OL2PARAM		(0x220 + 2 * 0x40)
#define HW_PXP_OL3PARAM		(0x220 + 3 * 0x40)
#define HW_PXP_OL4PARAM		(0x220 + 4 * 0x40)
#define HW_PXP_OL5PARAM		(0x220 + 5 * 0x40)
#define HW_PXP_OL6PARAM		(0x220 + 6 * 0x40)
#define HW_PXP_OL7PARAM		(0x220 + 7 * 0x40)

#define HW_PXP_OLnPARAM		0x220
#define BM_PXP_OLnPARAM_ENABLE	0x00000001
#define BP_PXP_OLnPARAM_ENABLE	0
#define BM_PXP_OLnPARAM_ALPHA_CNTL	0x00000006
#define BP_PXP_OLnPARAM_ALPHA_CNTL	1
#define BM_PXP_OLnPARAM_ENABLE_COLORKEY	0x00000008
#define BM_PXP_OLnPARAM_FORMAT	0x000000F0
#define BP_PXP_OLnPARAM_FORMAT	4
#define BM_PXP_OLnPARAM_ALPHA	0x0000FF00
#define BP_PXP_OLnPARAM_ALPHA	8
