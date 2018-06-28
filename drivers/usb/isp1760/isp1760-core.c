// SPDX-License-Identifier: GPL-2.0
/*
 * Driver for the NXP ISP1760 chip
 *
 * Copyright 2014 Laurent Pinchart
 * Copyright 2007 Sebastian Siewior
 *
 * Contacts:
 *	Sebastian Siewior <bigeasy@linutronix.de>
 *	Laurent Pinchart <laurent.pinchart@ideasonboard.com>
 */

#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/usb.h>

#include "isp1760-core.h"
#include "isp1760-hcd.h"
#include "isp1760-regs.h"
#include "isp1760-udc.h"

static void isp1760_init_core(struct isp1760_device *isp)
{
	u32 otgctrl;
	u32 hwmode;

	/* Low-level chip reset */
	if (isp->rst_gpio) {
		gpiod_set_value_cansleep(isp->rst_gpio, 1);
		msleep(50);
		gpiod_set_value_cansleep(isp->rst_gpio, 0);
	}

	/*
	 * Reset the host controller, including the CPU interface
	 * configuration.
	 */
	isp1760_write32(isp->regs, HC_RESET_REG, SW_RESET_RESET_ALL);
	msleep(100);

	/* Setup HW Mode Control: This assumes a level active-low interrupt */
	hwmode = HW_DATA_BUS_32BIT;

	if (isp->devflags & ISP1760_FLAG_BUS_WIDTH_16)
		hwmode &= ~HW_DATA_BUS_32BIT;
	if (isp->devflags & ISP1760_FLAG_ANALOG_OC)
		hwmode |= HW_ANA_DIGI_OC;
	if (isp->devflags & ISP1760_FLAG_DACK_POL_HIGH)
		hwmode |= HW_DACK_POL_HIGH;
	if (isp->devflags & ISP1760_FLAG_DREQ_POL_HIGH)
		hwmode |= HW_DREQ_POL_HIGH;
	if (isp->devflags & ISP1760_FLAG_INTR_POL_HIGH)
		hwmode |= HW_INTR_HIGH_ACT;
	if (isp->devflags & ISP1760_FLAG_INTR_EDGE_TRIG)
		hwmode |= HW_INTR_EDGE_TRIG;

	/*
	 * The ISP1761 has a dedicated DC IRQ line but supports sharing the HC
	 * IRQ line for both the host and device controllers. Hardcode IRQ
	 * sharing for now and disable the DC interrupts globally to avoid
	 * spurious interrupts during HCD registration.
	 */
	if (isp->devflags & ISP1760_FLAG_ISP1761) {
		isp1760_write32(isp->regs, DC_MODE, 0);
		hwmode |= HW_COMN_IRQ;
	}

	/*
	 * We have to set this first in case we're in 16-bit mode.
	 * Write it twice to ensure correct upper bits if switching
	 * to 16-bit mode.
	 */
	isp1760_write32(isp->regs, HC_HW_MODE_CTRL, hwmode);
	isp1760_write32(isp->regs, HC_HW_MODE_CTRL, hwmode);

	/*
	 * PORT 1 Control register of the ISP1760 is the OTG control register
	 * on ISP1761.
	 *
	 * TODO: Really support OTG. For now we configure port 1 in device mode
	 * when OTG is requested.
	 */
	if ((isp->devflags & ISP1760_FLAG_ISP1761) &&
	    (isp->devflags & ISP1760_FLAG_OTG_EN))
		otgctrl = ((HW_DM_PULLDOWN | HW_DP_PULLDOWN) << 16)
			| HW_OTG_DISABLE;
	else
		otgctrl = (HW_SW_SEL_HC_DC << 16)
			| (HW_VBUS_DRV | HW_SEL_CP_EXT);

	isp1760_write32(isp->regs, HC_PORT1_CTRL, otgctrl);

	dev_info(isp->dev, "bus width: %u, oc: %s\n",
		 isp->devflags & ISP1760_FLAG_BUS_WIDTH_16 ? 16 : 32,
		 isp->devflags & ISP1760_FLAG_ANALOG_OC ? "analog" : "digital");
}

void isp1760_set_pullup(struct isp1760_device *isp, bool enable)
{
	isp1760_write32(isp->regs, HW_OTG_CTRL_SET,
			enable ? HW_DP_PULLUP : HW_DP_PULLUP << 16);
}

int isp1760_register(struct resource *mem, int irq, unsigned long irqflags,
		     struct device *dev, unsigned int devflags)
{
	struct isp1760_device *isp;
	bool udc_disabled = !(devflags & ISP1760_FLAG_ISP1761);
	int ret;

	/*
	 * If neither the HCD not the UDC is enabled return an error, as no
	 * device would be registered.
	 */
	if ((!IS_ENABLED(CONFIG_USB_ISP1760_HCD) || usb_disabled()) &&
	    (!IS_ENABLED(CONFIG_USB_ISP1761_UDC) || udc_disabled))
		return -ENODEV;

	/* prevent usb-core allocating DMA pages */
	dev->dma_mask = NULL;

	isp = devm_kzalloc(dev, sizeof(*isp), GFP_KERNEL);
	if (!isp)
		return -ENOMEM;

	isp->dev = dev;
	isp->devflags = devflags;

	isp->rst_gpio = devm_gpiod_get_optional(dev, NULL, GPIOD_OUT_HIGH);
	if (IS_ERR(isp->rst_gpio))
		return PTR_ERR(isp->rst_gpio);

	isp->regs = devm_ioremap_resource(dev, mem);
	if (IS_ERR(isp->regs))
		return PTR_ERR(isp->regs);

	isp1760_init_core(isp);

	if (IS_ENABLED(CONFIG_USB_ISP1760_HCD) && !usb_disabled()) {
		ret = isp1760_hcd_register(&isp->hcd, isp->regs, mem, irq,
					   irqflags | IRQF_SHARED, dev);
		if (ret < 0)
			return ret;
	}

	if (IS_ENABLED(CONFIG_USB_ISP1761_UDC) && !udc_disabled) {
		ret = isp1760_udc_register(isp, irq, irqflags);
		if (ret < 0) {
			isp1760_hcd_unregister(&isp->hcd);
			return ret;
		}
	}

	dev_set_drvdata(dev, isp);

	return 0;
}

void isp1760_unregister(struct device *dev)
{
	struct isp1760_device *isp = dev_get_drvdata(dev);

	isp1760_udc_unregister(isp);
	isp1760_hcd_unregister(&isp->hcd);
}

MODULE_DESCRIPTION("Driver for the ISP1760 USB-controller from NXP");
MODULE_AUTHOR("Sebastian Siewior <bigeasy@linuxtronix.de>");
MODULE_LICENSE("GPL v2");
