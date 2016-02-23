/*
 * arch/arm/mach-w90x900/include/mach/regs-usb.h
 *
 * Copyright (c) 2008 Nuvoton technology corporation.
 *
 * Wan ZongShun <mcuos.com@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation;version 2 of the License.
 *
 */

#ifndef __ASM_ARCH_REGS_USB_H
#define __ASM_ARCH_REGS_USB_H

/* usb Control Registers  */
#define USBH_BA		W90X900_VA_USBEHCIHOST
#define USBD_BA		W90X900_VA_USBDEV
#define USBO_BA		W90X900_VA_USBOHCIHOST

/* USB Host Control Registers */
#define REG_UPSCR0	(USBH_BA+0x064)
#define REG_UPSCR1	(USBH_BA+0x068)
#define REG_USBPCR0	(USBH_BA+0x0C4)
#define REG_USBPCR1	(USBH_BA+0x0C8)

/* USBH OHCI Control Registers */
#define REG_OpModEn	(USBO_BA+0x204)
/*This bit controls the polarity of over
*current flag from external power IC.
*/
#define OCALow		0x08

#endif /*  __ASM_ARCH_REGS_USB_H */
