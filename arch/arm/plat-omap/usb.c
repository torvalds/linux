 /*
 * arch/arm/plat-omap/usb.c -- platform level USB initialization
 *
 * Copyright (C) 2004 Texas Instruments, Inc.
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

#undef	DEBUG

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/io.h>

#include <plat/usb.h>
#include <plat/board.h>

#include <mach/hardware.h>

#ifdef	CONFIG_ARCH_OMAP_OTG

void __init
omap_otg_init(struct omap_usb_config *config)
{
	u32		syscon;
	int		status;
	int		alt_pingroup = 0;

	/* NOTE:  no bus or clock setup (yet?) */

	syscon = omap_readl(OTG_SYSCON_1) & 0xffff;
	if (!(syscon & OTG_RESET_DONE))
		pr_debug("USB resets not complete?\n");

	//omap_writew(0, OTG_IRQ_EN);

	/* pin muxing and transceiver pinouts */
	if (config->pins[0] > 2)	/* alt pingroup 2 */
		alt_pingroup = 1;
	syscon |= config->usb0_init(config->pins[0], is_usb0_device(config));
	syscon |= config->usb1_init(config->pins[1]);
	syscon |= config->usb2_init(config->pins[2], alt_pingroup);
	pr_debug("OTG_SYSCON_1 = %08x\n", omap_readl(OTG_SYSCON_1));
	omap_writel(syscon, OTG_SYSCON_1);

	syscon = config->hmc_mode;
	syscon |= USBX_SYNCHRO | (4 << 16) /* B_ASE0_BRST */;
#ifdef	CONFIG_USB_OTG
	if (config->otg)
		syscon |= OTG_EN;
#endif
	if (cpu_class_is_omap1())
		pr_debug("USB_TRANSCEIVER_CTRL = %03x\n",
			 omap_readl(USB_TRANSCEIVER_CTRL));
	pr_debug("OTG_SYSCON_2 = %08x\n", omap_readl(OTG_SYSCON_2));
	omap_writel(syscon, OTG_SYSCON_2);

	printk("USB: hmc %d", config->hmc_mode);
	if (!alt_pingroup)
		printk(", usb2 alt %d wires", config->pins[2]);
	else if (config->pins[0])
		printk(", usb0 %d wires%s", config->pins[0],
			is_usb0_device(config) ? " (dev)" : "");
	if (config->pins[1])
		printk(", usb1 %d wires", config->pins[1]);
	if (!alt_pingroup && config->pins[2])
		printk(", usb2 %d wires", config->pins[2]);
	if (config->otg)
		printk(", Mini-AB on usb%d", config->otg - 1);
	printk("\n");

	if (cpu_class_is_omap1()) {
		u16 w;

		/* leave USB clocks/controllers off until needed */
		w = omap_readw(ULPD_SOFT_REQ);
		w &= ~SOFT_USB_CLK_REQ;
		omap_writew(w, ULPD_SOFT_REQ);

		w = omap_readw(ULPD_CLOCK_CTRL);
		w &= ~USB_MCLK_EN;
		w |= DIS_USB_PVCI_CLK;
		omap_writew(w, ULPD_CLOCK_CTRL);
	}
	syscon = omap_readl(OTG_SYSCON_1);
	syscon |= HST_IDLE_EN|DEV_IDLE_EN|OTG_IDLE_EN;

#ifdef	CONFIG_USB_GADGET_OMAP
	if (config->otg || config->register_dev) {
		struct platform_device *udc_device = config->udc_device;

		syscon &= ~DEV_IDLE_EN;
		udc_device->dev.platform_data = config;
		status = platform_device_register(udc_device);
		if (status)
			pr_debug("can't register UDC device, %d\n", status);
	}
#endif

#if	defined(CONFIG_USB_OHCI_HCD) || defined(CONFIG_USB_OHCI_HCD_MODULE)
	if (config->otg || config->register_host) {
		struct platform_device *ohci_device = config->ohci_device;

		syscon &= ~HST_IDLE_EN;
		ohci_device->dev.platform_data = config;
		status = platform_device_register(ohci_device);
		if (status)
			pr_debug("can't register OHCI device, %d\n", status);
	}
#endif

#ifdef	CONFIG_USB_OTG
	if (config->otg) {
		struct platform_device *otg_device = config->otg_device;

		syscon &= ~OTG_IDLE_EN;
		otg_device->dev.platform_data = config;
		status = platform_device_register(otg_device);
		if (status)
			pr_debug("can't register OTG device, %d\n", status);
	}
#endif
	pr_debug("OTG_SYSCON_1 = %08x\n", omap_readl(OTG_SYSCON_1));
	omap_writel(syscon, OTG_SYSCON_1);
}

#else
void omap_otg_init(struct omap_usb_config *config) {}
#endif
