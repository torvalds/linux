/*
 * stmp378x: TVENC register definitions
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
#define REGS_TVENC_BASE	(STMP3XXX_REGS_BASE + 0x38000)
#define REGS_TVENC_PHYS	0x80038000
#define REGS_TVENC_SIZE	0x2000

#define HW_TVENC_CTRL		0x0
#define BM_TVENC_CTRL_CLKGATE	0x40000000
#define BM_TVENC_CTRL_SFTRST	0x80000000

#define HW_TVENC_CONFIG		0x10
#define BM_TVENC_CONFIG_ENCD_MODE	0x00000007
#define BP_TVENC_CONFIG_ENCD_MODE	0
#define BM_TVENC_CONFIG_SYNC_MODE	0x00000070
#define BP_TVENC_CONFIG_SYNC_MODE	4
#define BM_TVENC_CONFIG_FSYNC_PHS	0x00000200
#define BM_TVENC_CONFIG_CGAIN	0x0000C000
#define BP_TVENC_CONFIG_CGAIN	14
#define BM_TVENC_CONFIG_YGAIN_SEL	0x00030000
#define BP_TVENC_CONFIG_YGAIN_SEL	16
#define BM_TVENC_CONFIG_PAL_SHAPE	0x00100000

#define HW_TVENC_SYNCOFFSET	0x30

#define HW_TVENC_COLORSUB0	0xC0

#define HW_TVENC_COLORBURST	0x140
#define BM_TVENC_COLORBURST_PBA	0x00FF0000
#define BP_TVENC_COLORBURST_PBA	16
#define BM_TVENC_COLORBURST_NBA	0xFF000000
#define BP_TVENC_COLORBURST_NBA	24

#define HW_TVENC_MACROVISION0	0x150

#define HW_TVENC_MACROVISION1	0x160

#define HW_TVENC_MACROVISION2	0x170

#define HW_TVENC_MACROVISION3	0x180

#define HW_TVENC_MACROVISION4	0x190

#define HW_TVENC_DACCTRL	0x1A0
#define BM_TVENC_DACCTRL_RVAL	0x00000070
#define BP_TVENC_DACCTRL_RVAL	4
#define BM_TVENC_DACCTRL_DUMP_TOVDD1	0x00000100
#define BM_TVENC_DACCTRL_PWRUP1	0x00001000
#define BM_TVENC_DACCTRL_GAINUP	0x00040000
#define BM_TVENC_DACCTRL_GAINDN	0x00080000
