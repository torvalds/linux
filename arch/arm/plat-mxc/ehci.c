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

#include <mach/hardware.h>
#include <mach/mxc_ehci.h>

#define USBCTRL_OTGBASE_OFFSET	0x600

#define MX31_OTG_SIC_SHIFT	29
#define MX31_OTG_SIC_MASK	(0x3 << MX31_OTG_SIC_SHIFT)
#define MX31_OTG_PM_BIT		(1 << 24)

#define MX31_H2_SIC_SHIFT	21
#define MX31_H2_SIC_MASK	(0x3 << MX31_H2_SIC_SHIFT)
#define MX31_H2_PM_BIT		(1 << 16)
#define MX31_H2_DT_BIT		(1 << 5)

#define MX31_H1_SIC_SHIFT	13
#define MX31_H1_SIC_MASK	(0x3 << MX31_H1_SIC_SHIFT)
#define MX31_H1_PM_BIT		(1 << 8)
#define MX31_H1_DT_BIT		(1 << 4)

#define MX35_OTG_SIC_SHIFT	29
#define MX35_OTG_SIC_MASK	(0x3 << MX35_OTG_SIC_SHIFT)
#define MX35_OTG_PM_BIT		(1 << 24)

#define MX35_H1_SIC_SHIFT	21
#define MX35_H1_SIC_MASK	(0x3 << MX35_H1_SIC_SHIFT)
#define MX35_H1_PM_BIT		(1 << 8)
#define MX35_H1_IPPUE_UP_BIT	(1 << 7)
#define MX35_H1_IPPUE_DOWN_BIT	(1 << 6)
#define MX35_H1_TLL_BIT		(1 << 5)
#define MX35_H1_USBTE_BIT	(1 << 4)

#define MXC_OTG_OFFSET		0
#define MXC_H1_OFFSET		0x200

/* USB_CTRL */
#define MXC_OTG_UCTRL_OWIE_BIT		(1 << 27)	/* OTG wakeup intr enable */
#define MXC_OTG_UCTRL_OPM_BIT		(1 << 24)	/* OTG power mask */
#define MXC_H1_UCTRL_H1UIE_BIT		(1 << 12)	/* Host1 ULPI interrupt enable */
#define MXC_H1_UCTRL_H1WIE_BIT		(1 << 11)	/* HOST1 wakeup intr enable */
#define MXC_H1_UCTRL_H1PM_BIT		(1 <<  8)		/* HOST1 power mask */

/* USB_PHY_CTRL_FUNC */
#define MXC_OTG_PHYCTRL_OC_DIS_BIT	(1 << 8)	/* OTG Disable Overcurrent Event */
#define MXC_H1_OC_DIS_BIT			(1 << 5)	/* UH1 Disable Overcurrent Event */

#define MXC_USBCMD_OFFSET			0x140

/* USBCMD */
#define MXC_UCMD_ITC_NO_THRESHOLD_MASK	(~(0xff << 16))	/* Interrupt Threshold Control */

int mxc_initialize_usb_hw(int port, unsigned int flags)
{
	unsigned int v;
#if defined(CONFIG_ARCH_MX25)
	if (cpu_is_mx25()) {
		v = readl(MX25_IO_ADDRESS(MX25_OTG_BASE_ADDR +
				     USBCTRL_OTGBASE_OFFSET));

		switch (port) {
		case 0:	/* OTG port */
			v &= ~(MX35_OTG_SIC_MASK | MX35_OTG_PM_BIT);
			v |= (flags & MXC_EHCI_INTERFACE_MASK)
					<< MX35_OTG_SIC_SHIFT;
			if (!(flags & MXC_EHCI_POWER_PINS_ENABLED))
				v |= MX35_OTG_PM_BIT;

			break;
		case 1: /* H1 port */
			v &= ~(MX35_H1_SIC_MASK | MX35_H1_PM_BIT | MX35_H1_TLL_BIT |
				MX35_H1_USBTE_BIT | MX35_H1_IPPUE_DOWN_BIT | MX35_H1_IPPUE_UP_BIT);
			v |= (flags & MXC_EHCI_INTERFACE_MASK)
						<< MX35_H1_SIC_SHIFT;
			if (!(flags & MXC_EHCI_POWER_PINS_ENABLED))
				v |= MX35_H1_PM_BIT;

			if (!(flags & MXC_EHCI_TTL_ENABLED))
				v |= MX35_H1_TLL_BIT;

			if (flags & MXC_EHCI_INTERNAL_PHY)
				v |= MX35_H1_USBTE_BIT;

			if (flags & MXC_EHCI_IPPUE_DOWN)
				v |= MX35_H1_IPPUE_DOWN_BIT;

			if (flags & MXC_EHCI_IPPUE_UP)
				v |= MX35_H1_IPPUE_UP_BIT;

			break;
		default:
			return -EINVAL;
		}

		writel(v, MX25_IO_ADDRESS(MX25_OTG_BASE_ADDR +
				     USBCTRL_OTGBASE_OFFSET));
		return 0;
	}
#endif /* CONFIG_ARCH_MX25 */
#if defined(CONFIG_ARCH_MX3)
	if (cpu_is_mx31()) {
		v = readl(MX31_IO_ADDRESS(MX31_OTG_BASE_ADDR +
				     USBCTRL_OTGBASE_OFFSET));

		switch (port) {
		case 0:	/* OTG port */
			v &= ~(MX31_OTG_SIC_MASK | MX31_OTG_PM_BIT);
			v |= (flags & MXC_EHCI_INTERFACE_MASK)
					<< MX31_OTG_SIC_SHIFT;
			if (!(flags & MXC_EHCI_POWER_PINS_ENABLED))
				v |= MX31_OTG_PM_BIT;

			break;
		case 1: /* H1 port */
			v &= ~(MX31_H1_SIC_MASK | MX31_H1_PM_BIT | MX31_H1_DT_BIT);
			v |= (flags & MXC_EHCI_INTERFACE_MASK)
						<< MX31_H1_SIC_SHIFT;
			if (!(flags & MXC_EHCI_POWER_PINS_ENABLED))
				v |= MX31_H1_PM_BIT;

			if (!(flags & MXC_EHCI_TTL_ENABLED))
				v |= MX31_H1_DT_BIT;

			break;
		case 2:	/* H2 port */
			v &= ~(MX31_H2_SIC_MASK | MX31_H2_PM_BIT | MX31_H2_DT_BIT);
			v |= (flags & MXC_EHCI_INTERFACE_MASK)
						<< MX31_H2_SIC_SHIFT;
			if (!(flags & MXC_EHCI_POWER_PINS_ENABLED))
				v |= MX31_H2_PM_BIT;

			if (!(flags & MXC_EHCI_TTL_ENABLED))
				v |= MX31_H2_DT_BIT;

			break;
		default:
			return -EINVAL;
		}

		writel(v, MX31_IO_ADDRESS(MX31_OTG_BASE_ADDR +
				     USBCTRL_OTGBASE_OFFSET));
		return 0;
	}

	if (cpu_is_mx35()) {
		v = readl(MX35_IO_ADDRESS(MX35_OTG_BASE_ADDR +
				     USBCTRL_OTGBASE_OFFSET));

		switch (port) {
		case 0:	/* OTG port */
			v &= ~(MX35_OTG_SIC_MASK | MX35_OTG_PM_BIT);
			v |= (flags & MXC_EHCI_INTERFACE_MASK)
					<< MX35_OTG_SIC_SHIFT;
			if (!(flags & MXC_EHCI_POWER_PINS_ENABLED))
				v |= MX35_OTG_PM_BIT;

			break;
		case 1: /* H1 port */
			v &= ~(MX35_H1_SIC_MASK | MX35_H1_PM_BIT | MX35_H1_TLL_BIT |
				MX35_H1_USBTE_BIT | MX35_H1_IPPUE_DOWN_BIT | MX35_H1_IPPUE_UP_BIT);
			v |= (flags & MXC_EHCI_INTERFACE_MASK)
						<< MX35_H1_SIC_SHIFT;
			if (!(flags & MXC_EHCI_POWER_PINS_ENABLED))
				v |= MX35_H1_PM_BIT;

			if (!(flags & MXC_EHCI_TTL_ENABLED))
				v |= MX35_H1_TLL_BIT;

			if (flags & MXC_EHCI_INTERNAL_PHY)
				v |= MX35_H1_USBTE_BIT;

			if (flags & MXC_EHCI_IPPUE_DOWN)
				v |= MX35_H1_IPPUE_DOWN_BIT;

			if (flags & MXC_EHCI_IPPUE_UP)
				v |= MX35_H1_IPPUE_UP_BIT;

			break;
		default:
			return -EINVAL;
		}

		writel(v, MX35_IO_ADDRESS(MX35_OTG_BASE_ADDR +
				     USBCTRL_OTGBASE_OFFSET));
		return 0;
	}
#endif /* CONFIG_ARCH_MX3 */
#ifdef CONFIG_MACH_MX27
	if (cpu_is_mx27()) {
		/* On i.MX27 we can use the i.MX31 USBCTRL bits, they
		 * are identical
		 */
		v = readl(MX27_IO_ADDRESS(MX27_OTG_BASE_ADDR +
				     USBCTRL_OTGBASE_OFFSET));
		switch (port) {
		case 0:	/* OTG port */
			v &= ~(MX31_OTG_SIC_MASK | MX31_OTG_PM_BIT);
			v |= (flags & MXC_EHCI_INTERFACE_MASK)
					<< MX31_OTG_SIC_SHIFT;
			if (!(flags & MXC_EHCI_POWER_PINS_ENABLED))
				v |= MX31_OTG_PM_BIT;
			break;
		case 1: /* H1 port */
			v &= ~(MX31_H1_SIC_MASK | MX31_H1_PM_BIT | MX31_H1_DT_BIT);
			v |= (flags & MXC_EHCI_INTERFACE_MASK)
						<< MX31_H1_SIC_SHIFT;
			if (!(flags & MXC_EHCI_POWER_PINS_ENABLED))
				v |= MX31_H1_PM_BIT;

			if (!(flags & MXC_EHCI_TTL_ENABLED))
				v |= MX31_H1_DT_BIT;

			break;
		case 2:	/* H2 port */
			v &= ~(MX31_H2_SIC_MASK | MX31_H2_PM_BIT | MX31_H2_DT_BIT);
			v |= (flags & MXC_EHCI_INTERFACE_MASK)
						<< MX31_H2_SIC_SHIFT;
			if (!(flags & MXC_EHCI_POWER_PINS_ENABLED))
				v |= MX31_H2_PM_BIT;

			if (!(flags & MXC_EHCI_TTL_ENABLED))
				v |= MX31_H2_DT_BIT;

			break;
		default:
			return -EINVAL;
		}
		writel(v, MX27_IO_ADDRESS(MX27_OTG_BASE_ADDR +
				     USBCTRL_OTGBASE_OFFSET));
		return 0;
	}
#endif /* CONFIG_MACH_MX27 */
#ifdef CONFIG_ARCH_MX51
	if (cpu_is_mx51()) {
		void __iomem *usb_base;
		void __iomem *usbotg_base;
		void __iomem *usbother_base;
		int ret = 0;

		usb_base = ioremap(MX51_OTG_BASE_ADDR, SZ_4K);

		switch (port) {
		case 0:	/* OTG port */
			usbotg_base = usb_base + MXC_OTG_OFFSET;
			break;
		case 1:	/* Host 1 port */
			usbotg_base = usb_base + MXC_H1_OFFSET;
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

				if (flags & MXC_EHCI_POWER_PINS_ENABLED)
					v |= (MXC_OTG_PHYCTRL_OC_DIS_BIT | MXC_OTG_UCTRL_OPM_BIT); /* OC/USBPWR is not used */
				else
					v &= ~(MXC_OTG_PHYCTRL_OC_DIS_BIT | MXC_OTG_UCTRL_OPM_BIT); /* OC/USBPWR is used */
				__raw_writel(v, usbother_base + MXC_USB_PHY_CTR_FUNC_OFFSET);

				v = __raw_readl(usbother_base + MXC_USBCTRL_OFFSET);
				if (flags & MXC_EHCI_WAKEUP_ENABLED)
					v |= MXC_OTG_UCTRL_OWIE_BIT;/* OTG wakeup enable */
				else
					v &= ~MXC_OTG_UCTRL_OWIE_BIT;/* OTG wakeup disable */
				__raw_writel(v, usbother_base + MXC_USBCTRL_OFFSET);
			}
			break;
		case 1:	/* Host 1 */
			/*Host ULPI */
			v = __raw_readl(usbother_base + MXC_USBCTRL_OFFSET);
			if (flags & MXC_EHCI_WAKEUP_ENABLED)
				v &= ~(MXC_H1_UCTRL_H1WIE_BIT | MXC_H1_UCTRL_H1UIE_BIT);/* HOST1 wakeup/ULPI intr disable */
			else
				v &= ~(MXC_H1_UCTRL_H1WIE_BIT | MXC_H1_UCTRL_H1UIE_BIT);/* HOST1 wakeup/ULPI intr disable */

			if (flags & MXC_EHCI_POWER_PINS_ENABLED)
				v &= ~MXC_H1_UCTRL_H1PM_BIT; /* HOST1 power mask used*/
			else
				v |= MXC_H1_UCTRL_H1PM_BIT; /* HOST1 power mask used*/
			__raw_writel(v, usbother_base + MXC_USBCTRL_OFFSET);

			v = __raw_readl(usbother_base + MXC_USB_PHY_CTR_FUNC_OFFSET);
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
		}

error:
		iounmap(usb_base);
		return ret;
	}
#endif
	printk(KERN_WARNING
		"%s() unable to setup USBCONTROL for this CPU\n", __func__);
	return -EINVAL;
}
EXPORT_SYMBOL(mxc_initialize_usb_hw);

