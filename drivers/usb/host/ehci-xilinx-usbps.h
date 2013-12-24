/*
 * Xilinx PS USB Host Controller Driver Header file.
 *
 * Copyright (C) 2011 Xilinx, Inc.
 *
 * This file is based on ehci-fsl.h file with few minor modifications
 * to support Xilinx PS USB controller.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 59 Temple
 * Place, Suite 330, Boston, MA  02111-1307  USA
 */
#ifndef _EHCI_XILINX_XUSBPS_H
#define _EHCI_XILINX_XUSBPS_H

#include <linux/usb/xilinx_usbps_otg.h>

/* offsets for the non-ehci registers in the XUSBPS SOC USB controller */
#define XUSBPS_SOC_USB_ULPIVP	0x170
#define XUSBPS_SOC_USB_PORTSC1	0x184
#define PORT_PTS_MSK		(3<<30)
#define PORT_PTS_UTMI		(0<<30)
#define PORT_PTS_ULPI		(2<<30)
#define	PORT_PTS_SERIAL		(3<<30)
#define PORT_PTS_PTW		(1<<28)
#define XUSBPS_SOC_USB_PORTSC2	0x188

#endif				/* _EHCI_XILINX_XUSBPS_H */
