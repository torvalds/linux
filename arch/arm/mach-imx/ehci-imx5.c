/*
 * Copyright (c) 2009 Daniel Mack <daniel@caiaq.de>
 * Copyright (C) 2010 Freescale Semiconductor, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 */

#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/platform_data/usb-ehci-mxc.h>

#include "ehci.h"
#include "hardware.h"

#define MXC_OTG_OFFSET			0
#define MXC_H1_OFFSET			0x200
#define MXC_H2_OFFSET			0x400

/* USB_CTRL */
#define MXC_OTG_UCTRL_OWIE_BIT		(1 << 27)	/* OTG wakeup intr enable */
#define MXC_OTG_UCTRL_OPM_BIT		(1 << 24)	/* OTG power mask */
#define MXC_H1_UCTRL_H1UIE_BIT		(1 << 12)	/* Host1 ULPI interrupt enable */
#define MXC_H1_UCTRL_H1WIE_BIT		(1 << 11)	/* HOST1 wakeup intr enable */
#define MXC_H1_UCTRL_H1PM_BIT		(1 <<  8)	/* HOST1 power mask */

/* USB_PHY_CTRL_FUNC */
#define MXC_OTG_PHYCTRL_OC_POL_BIT	(1 << 9)	/* OTG Polarity of Overcurrent */
#define MXC_OTG_PHYCTRL_OC_DIS_BIT	(1 << 8)	/* OTG Disable Overcurrent Event */
#define MXC_H1_OC_POL_BIT		(1 << 6)	/* UH1 Polarity of Overcurrent */
#define MXC_H1_OC_DIS_BIT		(1 << 5)	/* UH1 Disable Overcurrent Event */
#define MXC_OTG_PHYCTRL_PWR_POL_BIT	(1 << 3)	/* OTG Power Pin Polarity */

/* USBH2CTRL */
#define MXC_H2_UCTRL_H2UIE_BIT		(1 << 8)
#define MXC_H2_UCTRL_H2WIE_BIT		(1 << 7)
#define MXC_H2_UCTRL_H2PM_BIT		(1 << 4)

#define MXC_USBCMD_OFFSET		0x140

/* USBCMD */
#define MXC_UCMD_ITC_NO_THRESHOLD_MASK	(~(0xff << 16))	/* Interrupt Threshold Control */

int mx51_initialize_usb_hw(int port, unsigned int flags)
{
	unsigned int v;
	void __iomem *usb_base;
	void __iomem *usbotg_base;
	void __iomem *usbother_base;
	int ret = 0;

	usb_base = ioremap(MX51_USB_OTG_BASE_ADDR, SZ_4K);
	if (!usb_base) {
		printk(KERN_ERR "%s(): ioremap failed\n", __func__);
		return -ENOMEM;
	}

	switch (port) {
	case 0:	/* OTG port */
		usbotg_base = usb_base + MXC_OTG_OFFSET;
		break;
	case 1:	/* Host 1 port */
		usbotg_base = usb_base + MXC_H1_OFFSET;
		break;
	case 2: /* Host 2 port */
		usbotg_base = usb_base + MXC_H2_OFFSET;
		break;
	default:
		printk(KERN_ERR"%s no such port %d\n", __func__, port);
		ret = -ENOENT;
		goto error;
	}
	usbother_base = usb_base + MX5_USBOTHER_REGS_OFFSET;

	switch (port) {
	case 0:	/*OTG port */
		if (flags & MXC_EHCI_INTERNAL_PHY) {
			v = __raw_readl(usbother_base + MXC_USB_PHY_CTR_FUNC_OFFSET);

			if (flags & MXC_EHCI_OC_PIN_ACTIVE_LOW)
				v |= MXC_OTG_PHYCTRL_OC_POL_BIT;
			else
				v &= ~MXC_OTG_PHYCTRL_OC_POL_BIT;
			if (flags & MXC_EHCI_POWER_PINS_ENABLED) {
				/* OC/USBPWR is used */
				v &= ~MXC_OTG_PHYCTRL_OC_DIS_BIT;
			} else {
				/* OC/USBPWR is not used */
				v |= MXC_OTG_PHYCTRL_OC_DIS_BIT;
			}
			if (flags & MXC_EHCI_PWR_PIN_ACTIVE_HIGH)
				v |= MXC_OTG_PHYCTRL_PWR_POL_BIT;
			else
				v &= ~MXC_OTG_PHYCTRL_PWR_POL_BIT;
			__raw_writel(v, usbother_base + MXC_USB_PHY_CTR_FUNC_OFFSET);

			v = __raw_readl(usbother_base + MXC_USBCTRL_OFFSET);
			if (flags & MXC_EHCI_WAKEUP_ENABLED)
				v |= MXC_OTG_UCTRL_OWIE_BIT;/* OTG wakeup enable */
			else
				v &= ~MXC_OTG_UCTRL_OWIE_BIT;/* OTG wakeup disable */
			if (flags & MXC_EHCI_POWER_PINS_ENABLED)
				v &= ~MXC_OTG_UCTRL_OPM_BIT;
			else
				v |= MXC_OTG_UCTRL_OPM_BIT;
			__raw_writel(v, usbother_base + MXC_USBCTRL_OFFSET);
		}
		break;
	case 1:	/* Host 1 */
		/*Host ULPI */
		v = __raw_readl(usbother_base + MXC_USBCTRL_OFFSET);
		if (flags & MXC_EHCI_WAKEUP_ENABLED) {
			/* HOST1 wakeup/ULPI intr enable */
			v |= (MXC_H1_UCTRL_H1WIE_BIT | MXC_H1_UCTRL_H1UIE_BIT);
		} else {
			/* HOST1 wakeup/ULPI intr disable */
			v &= ~(MXC_H1_UCTRL_H1WIE_BIT | MXC_H1_UCTRL_H1UIE_BIT);
		}

		if (flags & MXC_EHCI_POWER_PINS_ENABLED)
			v &= ~MXC_H1_UCTRL_H1PM_BIT; /* HOST1 power mask unused*/
		else
			v |= MXC_H1_UCTRL_H1PM_BIT; /* HOST1 power mask used*/
		__raw_writel(v, usbother_base + MXC_USBCTRL_OFFSET);

		v = __raw_readl(usbother_base + MXC_USB_PHY_CTR_FUNC_OFFSET);
		if (flags & MXC_EHCI_OC_PIN_ACTIVE_LOW)
			v |= MXC_H1_OC_POL_BIT;
		else
			v &= ~MXC_H1_OC_POL_BIT;
		if (flags & MXC_EHCI_POWER_PINS_ENABLED)
			v &= ~MXC_H1_OC_DIS_BIT; /* OC is used */
		else
			v |= MXC_H1_OC_DIS_BIT; /* OC is not used */
		__raw_writel(v, usbother_base + MXC_USB_PHY_CTR_FUNC_OFFSET);

		v = __raw_readl(usbotg_base + MXC_USBCMD_OFFSET);
		if (flags & MXC_EHCI_ITC_NO_THRESHOLD)
			/* Interrupt Threshold Control:Immediate (no threshold) */
			v &= MXC_UCMD_ITC_NO_THRESHOLD_MASK;
		__raw_writel(v, usbotg_base + MXC_USBCMD_OFFSET);
		break;
	case 2: /* Host 2 ULPI */
		v = __raw_readl(usbother_base + MXC_USBH2CTRL_OFFSET);
		if (flags & MXC_EHCI_WAKEUP_ENABLED) {
			/* HOST1 wakeup/ULPI intr enable */
			v |= (MXC_H2_UCTRL_H2WIE_BIT | MXC_H2_UCTRL_H2UIE_BIT);
		} else {
			/* HOST1 wakeup/ULPI intr disable */
			v &= ~(MXC_H2_UCTRL_H2WIE_BIT | MXC_H2_UCTRL_H2UIE_BIT);
		}

		if (flags & MXC_EHCI_POWER_PINS_ENABLED)
			v &= ~MXC_H2_UCTRL_H2PM_BIT; /* HOST2 power mask unused*/
		else
			v |= MXC_H2_UCTRL_H2PM_BIT; /* HOST2 power mask used*/
		__raw_writel(v, usbother_base + MXC_USBH2CTRL_OFFSET);
		break;
	}

error:
	iounmap(usb_base);
	return ret;
}

