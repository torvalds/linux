/*
 * bits.h - register bits of the ChipIdea USB IP core
 *
 * Copyright (C) 2008 Chipidea - MIPS Technologies, Inc. All rights reserved.
 *
 * Author: David Lopo
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __DRIVERS_USB_CHIPIDEA_BITS_H
#define __DRIVERS_USB_CHIPIDEA_BITS_H

#include <linux/usb/ehci_def.h>

/* HCCPARAMS */
#define HCCPARAMS_LEN         BIT(17)

/* DCCPARAMS */
#define DCCPARAMS_DEN         (0x1F << 0)
#define DCCPARAMS_DC          BIT(7)
#define DCCPARAMS_HC          BIT(8)

/* TESTMODE */
#define TESTMODE_FORCE        BIT(0)

/* USBCMD */
#define USBCMD_RS             BIT(0)
#define USBCMD_RST            BIT(1)
#define USBCMD_SUTW           BIT(13)
#define USBCMD_ATDTW          BIT(14)

/* USBSTS & USBINTR */
#define USBi_UI               BIT(0)
#define USBi_UEI              BIT(1)
#define USBi_PCI              BIT(2)
#define USBi_URI              BIT(6)
#define USBi_SLI              BIT(8)

/* DEVICEADDR */
#define DEVICEADDR_USBADRA    BIT(24)
#define DEVICEADDR_USBADR     (0x7FUL << 25)

/* PORTSC */
#define PORTSC_FPR            BIT(6)
#define PORTSC_SUSP           BIT(7)
#define PORTSC_HSP            BIT(9)
#define PORTSC_PTC            (0x0FUL << 16)
/* PTS and PTW for non lpm version only */
#define PORTSC_PTS(d)						\
	(u32)((((d) & 0x3) << 30) | (((d) & 0x4) ? BIT(25) : 0))
#define PORTSC_PTW            BIT(28)
#define PORTSC_STS            BIT(29)

/* DEVLC */
#define DEVLC_PSPD            (0x03UL << 25)
#define DEVLC_PSPD_HS         (0x02UL << 25)
#define DEVLC_PTW             BIT(27)
#define DEVLC_STS             BIT(28)
#define DEVLC_PTS(d)          (u32)(((d) & 0x7) << 29)

/* Encoding for DEVLC_PTS and PORTSC_PTS */
#define PTS_UTMI              0
#define PTS_ULPI              2
#define PTS_SERIAL            3
#define PTS_HSIC              4

/* OTGSC */
#define OTGSC_IDPU	      BIT(5)
#define OTGSC_ID	      BIT(8)
#define OTGSC_AVV	      BIT(9)
#define OTGSC_ASV	      BIT(10)
#define OTGSC_BSV	      BIT(11)
#define OTGSC_BSE	      BIT(12)
#define OTGSC_IDIS	      BIT(16)
#define OTGSC_AVVIS	      BIT(17)
#define OTGSC_ASVIS	      BIT(18)
#define OTGSC_BSVIS	      BIT(19)
#define OTGSC_BSEIS	      BIT(20)
#define OTGSC_1MSIS	      BIT(21)
#define OTGSC_DPIS	      BIT(22)
#define OTGSC_IDIE	      BIT(24)
#define OTGSC_AVVIE	      BIT(25)
#define OTGSC_ASVIE	      BIT(26)
#define OTGSC_BSVIE	      BIT(27)
#define OTGSC_BSEIE	      BIT(28)
#define OTGSC_1MSIE	      BIT(29)
#define OTGSC_DPIE	      BIT(30)
#define OTGSC_INT_EN_BITS	(OTGSC_IDIE | OTGSC_AVVIE | OTGSC_ASVIE \
				| OTGSC_BSVIE | OTGSC_BSEIE | OTGSC_1MSIE \
				| OTGSC_DPIE)
#define OTGSC_INT_STATUS_BITS	(OTGSC_IDIS | OTGSC_AVVIS | OTGSC_ASVIS	\
				| OTGSC_BSVIS | OTGSC_BSEIS | OTGSC_1MSIS \
				| OTGSC_DPIS)

/* USBMODE */
#define USBMODE_CM            (0x03UL <<  0)
#define USBMODE_CM_DC         (0x02UL <<  0)
#define USBMODE_SLOM          BIT(3)
#define USBMODE_CI_SDIS       BIT(4)

/* ENDPTCTRL */
#define ENDPTCTRL_RXS         BIT(0)
#define ENDPTCTRL_RXT         (0x03UL <<  2)
#define ENDPTCTRL_RXR         BIT(6)         /* reserved for port 0 */
#define ENDPTCTRL_RXE         BIT(7)
#define ENDPTCTRL_TXS         BIT(16)
#define ENDPTCTRL_TXT         (0x03UL << 18)
#define ENDPTCTRL_TXR         BIT(22)        /* reserved for port 0 */
#define ENDPTCTRL_TXE         BIT(23)

#endif /* __DRIVERS_USB_CHIPIDEA_BITS_H */
