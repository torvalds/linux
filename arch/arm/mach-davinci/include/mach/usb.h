/*
 * USB related definitions
 *
 * Copyright (C) 2009 MontaVista Software, Inc. <source@mvista.com>
 *
 * This file is licensed under the terms of the GNU General Public License
 * version 2. This program is licensed "as is" without any warranty of any
 * kind, whether express or implied.
 */

#ifndef __ASM_ARCH_USB_H
#define __ASM_ARCH_USB_H

/* DA8xx CFGCHIP2 (USB 2.0 PHY Control) register bits */
#define CFGCHIP2_PHYCLKGD	(1 << 17)
#define CFGCHIP2_VBUSSENSE	(1 << 16)
#define CFGCHIP2_RESET		(1 << 15)
#define CFGCHIP2_OTGMODE	(3 << 13)
#define CFGCHIP2_NO_OVERRIDE	(0 << 13)
#define CFGCHIP2_FORCE_HOST	(1 << 13)
#define CFGCHIP2_FORCE_DEVICE 	(2 << 13)
#define CFGCHIP2_FORCE_HOST_VBUS_LOW (3 << 13)
#define CFGCHIP2_USB1PHYCLKMUX	(1 << 12)
#define CFGCHIP2_USB2PHYCLKMUX	(1 << 11)
#define CFGCHIP2_PHYPWRDN	(1 << 10)
#define CFGCHIP2_OTGPWRDN	(1 << 9)
#define CFGCHIP2_DATPOL 	(1 << 8)
#define CFGCHIP2_USB1SUSPENDM	(1 << 7)
#define CFGCHIP2_PHY_PLLON	(1 << 6)	/* override PLL suspend */
#define CFGCHIP2_SESENDEN	(1 << 5)	/* Vsess_end comparator */
#define CFGCHIP2_VBDTCTEN	(1 << 4)	/* Vbus comparator */
#define CFGCHIP2_REFFREQ	(0xf << 0)
#define CFGCHIP2_REFFREQ_12MHZ	(1 << 0)
#define CFGCHIP2_REFFREQ_24MHZ	(2 << 0)
#define CFGCHIP2_REFFREQ_48MHZ	(3 << 0)

struct	da8xx_ohci_root_hub;

typedef void (*da8xx_ocic_handler_t)(struct da8xx_ohci_root_hub *hub,
				     unsigned port);

/* Passed as the platform data to the OHCI driver */
struct	da8xx_ohci_root_hub {
	/* Switch the port power on/off */
	int	(*set_power)(unsigned port, int on);
	/* Read the port power status */
	int	(*get_power)(unsigned port);
	/* Read the port over-current indicator */
	int	(*get_oci)(unsigned port);
	/* Over-current indicator change notification (pass NULL to disable) */
	int	(*ocic_notify)(da8xx_ocic_handler_t handler);

	/* Time from power on to power good (in 2 ms units) */
	u8	potpgt;
};

void davinci_setup_usb(unsigned mA, unsigned potpgt_ms);

#endif	/* ifndef __ASM_ARCH_USB_H */
