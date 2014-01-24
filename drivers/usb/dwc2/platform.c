/*
 * platform.c - DesignWare HS OTG Controller platform driver
 *
 * Copyright (C) Matthijs Kooijman <matthijs@stdin.nl>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The names of the above-listed copyright holders may not be used
 *    to endorse or promote products derived from this software without
 *    specific prior written permission.
 *
 * ALTERNATIVELY, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") as published by the Free Software
 * Foundation; either version 2 of the License, or (at your option) any
 * later version.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
 * IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/usb/otg.h>

#include "core.h"
#include "hcd.h"

static const char dwc2_driver_name[] = "dwc2";

/**
 * dwc2_driver_remove() - Called when the DWC_otg core is unregistered with the
 * DWC_otg driver
 *
 * @dev: Platform device
 *
 * This routine is called, for example, when the rmmod command is executed. The
 * device may or may not be electrically present. If it is present, the driver
 * stops device processing. Any resources used on behalf of this device are
 * freed.
 */
static int dwc2_driver_remove(struct platform_device *dev)
{
	struct dwc2_hsotg *hsotg = platform_get_drvdata(dev);

	if (IS_ENABLED(CONFIG_USB_DWC2_GADGET))
		s3c_hsotg_remove(hsotg);
	else if (IS_ENABLED(CONFIG_USB_DWC2_HOST))
		dwc2_hcd_remove(hsotg);
	else { /* dual role */
		s3c_hsotg_remove(hsotg);
		dwc2_hcd_remove(hsotg);
	}

	return 0;
}

/**
 * dwc2_driver_probe() - Called when the DWC_otg core is bound to the DWC_otg
 * driver
 *
 * @dev: Platform device
 *
 * This routine creates the driver components required to control the device
 * (core, HCD, and PCD) and initializes the device. The driver components are
 * stored in a dwc2_hsotg structure. A reference to the dwc2_hsotg is saved
 * in the device private data. This allows the driver to access the dwc2_hsotg
 * structure on subsequent calls to driver methods for this device.
 */
static int dwc2_driver_probe(struct platform_device *dev)
{
	struct dwc2_hsotg *hsotg;
	struct resource *res;
	int retval;
	int irq;
	struct dwc2_core_params params;
	u32 prop;

	/* Default all params to autodetect */
	dwc2_set_all_params(&params, -1);

	hsotg = devm_kzalloc(&dev->dev, sizeof(*hsotg), GFP_KERNEL);
	if (!hsotg)
		return -ENOMEM;

	hsotg->core_params = kzalloc(sizeof(*hsotg->core_params), GFP_KERNEL);
	if (!hsotg->core_params)
		return -ENOMEM;

	hsotg->dev = &dev->dev;

	/*
	 * Use reasonable defaults so platforms don't have to provide these.
	 */
	if (!dev->dev.dma_mask)
		dev->dev.dma_mask = &dev->dev.coherent_dma_mask;
	if (!dev->dev.coherent_dma_mask)
		dev->dev.coherent_dma_mask = DMA_BIT_MASK(32);

	irq = platform_get_irq(dev, 0);
	if (irq < 0) {
		dev_err(&dev->dev, "missing IRQ resource\n");
		return -EINVAL;
	}

	res = platform_get_resource(dev, IORESOURCE_MEM, 0);
	hsotg->regs = devm_ioremap_resource(&dev->dev, res);
	if (IS_ERR(hsotg->regs))
		return PTR_ERR(hsotg->regs);

	dev_dbg(&dev->dev, "mapped PA %08lx to VA %p\n",
		(unsigned long)res->start, hsotg->regs);

	if (!of_property_read_u32(dev->dev.of_node,
		"enable-dynamic-fifo", &prop)) {
		params.enable_dynamic_fifo = prop;

		if (!of_property_read_u32(dev->dev.of_node,
			"host-rx-fifo-size", &prop)) {
			params.host_rx_fifo_size = prop;
		}

		if (!of_property_read_u32(dev->dev.of_node,
			"host-perio-tx-fifo-size", &prop)) {
			params.host_perio_tx_fifo_size = prop;
		}

		if (!of_property_read_u32(dev->dev.of_node,
			"host-nperio-tx-fifo-size", &prop)) {
			params.host_nperio_tx_fifo_size = prop;
		}
	}

	if (!of_property_read_u32(dev->dev.of_node,
		"dma-desc-enable", &prop)) {
		params.dma_desc_enable = prop;
	}

	if (IS_ENABLED(CONFIG_USB_DWC2_HOST)) {
		retval = dwc2_hcd_init(hsotg, irq, &params);
		if (retval)
			return retval;
	} else if (IS_ENABLED(CONFIG_USB_DWC2_GADGET)) {
		retval = dwc2_gadget_init(hsotg, irq);
		if (retval)
			return retval;
		retval = dwc2_core_init(hsotg, true, irq);
		if (retval)
			return retval;
	} else { /* dual role */
		retval = dwc2_gadget_init(hsotg, irq);
		if (retval)
			return retval;
		retval = dwc2_hcd_init(hsotg, irq, &params);
		if (retval)
			return retval;
	}
	spin_lock_init(&hsotg->lock);

	platform_set_drvdata(dev, hsotg);
	return retval;
}

static const struct of_device_id dwc2_of_match_table[] = {
	{ .compatible = "snps,dwc2" },
	{},
};
MODULE_DEVICE_TABLE(of, dwc2_of_match_table);

static struct platform_driver dwc2_platform_driver = {
	.driver = {
		.name = (char *)dwc2_driver_name,
		.of_match_table = dwc2_of_match_table,
	},
	.probe = dwc2_driver_probe,
	.remove = dwc2_driver_remove,
};

module_platform_driver(dwc2_platform_driver);

MODULE_DESCRIPTION("DESIGNWARE HS OTG Platform Glue");
MODULE_AUTHOR("Matthijs Kooijman <matthijs@stdin.nl>");
MODULE_LICENSE("Dual BSD/GPL");
