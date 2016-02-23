/*
 * This file contains the processor specific definitions of the TI OMAP34XX.
 *
 * Copyright (C) 2007 Texas Instruments.
 * Copyright (C) 2007 Nokia Corporation.
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef __ASM_ARCH_OMAP3_H
#define __ASM_ARCH_OMAP3_H

/*
 * Please place only base defines here and put the rest in device
 * specific headers.
 */

#define L4_34XX_BASE		0x48000000
#define L4_WK_34XX_BASE		0x48300000
#define L4_PER_34XX_BASE	0x49000000
#define L4_EMU_34XX_BASE	0x54000000
#define L3_34XX_BASE		0x68000000

#define L4_WK_AM33XX_BASE	0x44C00000

#define OMAP3430_32KSYNCT_BASE	0x48320000
#define OMAP3430_CM_BASE	0x48004800
#define OMAP3430_PRM_BASE	0x48306800
#define OMAP343X_SMS_BASE	0x6C000000
#define OMAP343X_SDRC_BASE	0x6D000000
#define OMAP34XX_GPMC_BASE	0x6E000000
#define OMAP343X_SCM_BASE	0x48002000
#define OMAP343X_CTRL_BASE	OMAP343X_SCM_BASE

#define OMAP34XX_IC_BASE	0x48200000

#define OMAP3430_ISP_BASE	(L4_34XX_BASE + 0xBC000)
#define OMAP3430_ISP_MMU_BASE	(OMAP3430_ISP_BASE + 0x1400)
#define OMAP3430_ISP_BASE2	(OMAP3430_ISP_BASE + 0x1800)

#define OMAP34XX_HSUSB_OTG_BASE	(L4_34XX_BASE + 0xAB000)
#define OMAP34XX_USBTLL_BASE	(L4_34XX_BASE + 0x62000)
#define OMAP34XX_UHH_CONFIG_BASE	(L4_34XX_BASE + 0x64000)
#define OMAP34XX_OHCI_BASE	(L4_34XX_BASE + 0x64400)
#define OMAP34XX_EHCI_BASE	(L4_34XX_BASE + 0x64800)
#define OMAP34XX_SR1_BASE	0x480C9000
#define OMAP34XX_SR2_BASE	0x480CB000

#define OMAP34XX_MAILBOX_BASE		(L4_34XX_BASE + 0x94000)

/* Security */
#define OMAP34XX_SEC_BASE	(L4_34XX_BASE + 0xA0000)
#define OMAP34XX_SEC_SHA1MD5_BASE	(OMAP34XX_SEC_BASE + 0x23000)
#define OMAP34XX_SEC_AES_BASE	(OMAP34XX_SEC_BASE + 0x25000)

#endif /* __ASM_ARCH_OMAP3_H */

