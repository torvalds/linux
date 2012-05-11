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

/* HCCPARAMS */
#define HCCPARAMS_LEN         BIT(17)

/* DCCPARAMS */
#define DCCPARAMS_DEN         (0x1F << 0)
#define DCCPARAMS_DC          BIT(7)

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

/* DEVLC */
#define DEVLC_PSPD            (0x03UL << 25)
#define    DEVLC_PSPD_HS      (0x02UL << 25)

/* USBMODE */
#define USBMODE_CM            (0x03UL <<  0)
#define    USBMODE_CM_IDLE    (0x00UL <<  0)
#define    USBMODE_CM_DEVICE  (0x02UL <<  0)
#define    USBMODE_CM_HOST    (0x03UL <<  0)
#define USBMODE_SLOM          BIT(3)
#define USBMODE_SDIS          BIT(4)

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
