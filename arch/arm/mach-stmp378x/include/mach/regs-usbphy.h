/*
 * stmp378x: USBPHY register definitions
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
#define REGS_USBPHY_BASE	(STMP3XXX_REGS_BASE + 0x7C000)
#define REGS_USBPHY_PHYS	0x8007C000
#define REGS_USBPHY_SIZE	0x2000

#define HW_USBPHY_PWD		0x0

#define HW_USBPHY_CTRL		0x30
#define BM_USBPHY_CTRL_ENHOSTDISCONDETECT	0x00000002
#define BM_USBPHY_CTRL_ENDEVPLUGINDETECT	0x00000010
#define BM_USBPHY_CTRL_ENOTGIDDETECT	0x00000080
#define BM_USBPHY_CTRL_ENIRQDEVPLUGIN	0x00000800
#define BM_USBPHY_CTRL_CLKGATE	0x40000000
#define BM_USBPHY_CTRL_SFTRST	0x80000000

#define HW_USBPHY_STATUS	0x40
#define BM_USBPHY_STATUS_DEVPLUGIN_STATUS	0x00000040
#define BM_USBPHY_STATUS_OTGID_STATUS	0x00000100
