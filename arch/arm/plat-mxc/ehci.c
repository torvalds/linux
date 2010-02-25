/*
 * Copyright (c) 2009 Daniel Mack <daniel@caiaq.de>
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
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
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

int mxc_set_usbcontrol(int port, unsigned int flags)
{
	unsigned int v;
#ifdef CONFIG_ARCH_MX3
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
	printk(KERN_WARNING
		"%s() unable to setup USBCONTROL for this CPU\n", __func__);
	return -EINVAL;
}
EXPORT_SYMBOL(mxc_set_usbcontrol);

