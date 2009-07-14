/*
 * stmp378x: USBCTRL register definitions
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
#define REGS_USBCTRL_BASE	(STMP3XXX_REGS_BASE + 0x80000)
#define REGS_USBCTRL_PHYS	0x80080000
#define REGS_USBCTRL_SIZE	0x2000

#define HW_USBCTRL_USBCMD	0x140
#define BM_USBCTRL_USBCMD_RS	0x00000001
#define BP_USBCTRL_USBCMD_RS	0
#define BM_USBCTRL_USBCMD_RST	0x00000002

#define HW_USBCTRL_USBINTR	0x148
#define BM_USBCTRL_USBINTR_UE	0x00000001
#define BP_USBCTRL_USBINTR_UE	0

#define HW_USBCTRL_PORTSC1	0x184
#define BM_USBCTRL_PORTSC1_PHCD	0x00800000

#define HW_USBCTRL_OTGSC	0x1A4
#define BM_USBCTRL_OTGSC_ID	0x00000100
#define BM_USBCTRL_OTGSC_IDIS	0x00010000
#define BM_USBCTRL_OTGSC_IDIE	0x01000000
