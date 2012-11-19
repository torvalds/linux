/**
 * dwc3-omap.c - OMAP Specific Glue layer
 *
 * Copyright (C) 2010-2011 Texas Instruments Incorporated - http://www.ti.com
 *
 * Authors: Felipe Balbi <balbi@ti.com>,
 *	    Sebastian Andrzej Siewior <bigeasy@linutronix.de>
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
 * GNU General Public License ("GPL") version 2, as published by the Free
 * Software Foundation.
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

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/platform_device.h>
#include <linux/platform_data/dwc3-omap.h>
#include <linux/dma-mapping.h>
#include <linux/ioport.h>
#include <linux/io.h>
#include <linux/of.h>

#include <linux/usb/otg.h>
#include <linux/usb/nop-usb-xceiv.h>

#include "core.h"

/*
 * All these registers belong to OMAP's Wrapper around the
 * DesignWare USB3 Core.
 */

#define USBOTGSS_REVISION			0x0000
#define USBOTGSS_SYSCONFIG			0x0010
#define USBOTGSS_IRQ_EOI			0x0020
#define USBOTGSS_IRQSTATUS_RAW_0		0x0024
#define USBOTGSS_IRQSTATUS_0			0x0028
#define USBOTGSS_IRQENABLE_SET_0		0x002c
#define USBOTGSS_IRQENABLE_CLR_0		0x0030
#define USBOTGSS_IRQSTATUS_RAW_1		0x0034
#define USBOTGSS_IRQSTATUS_1			0x0038
#define USBOTGSS_IRQENABLE_SET_1		0x003c
#define USBOTGSS_IRQENABLE_CLR_1		0x0040
#define USBOTGSS_UTMI_OTG_CTRL			0x0080
#define USBOTGSS_UTMI_OTG_STATUS		0x0084
#define USBOTGSS_MMRAM_OFFSET			0x0100
#define USBOTGSS_FLADJ				0x0104
#define USBOTGSS_DEBUG_CFG			0x0108
#define USBOTGSS_DEBUG_DATA			0x010c

/* SYSCONFIG REGISTER */
#define USBOTGSS_SYSCONFIG_DMADISABLE		(1 << 16)
#define USBOTGSS_SYSCONFIG_STANDBYMODE(x)	((x) << 4)

#define USBOTGSS_STANDBYMODE_FORCE_STANDBY	0
#define USBOTGSS_STANDBYMODE_NO_STANDBY		1
#define USBOTGSS_STANDBYMODE_SMART_STANDBY	2
#define USBOTGSS_STANDBYMODE_SMART_WAKEUP	3

#define USBOTGSS_STANDBYMODE_MASK		(0x03 << 4)

#define USBOTGSS_SYSCONFIG_IDLEMODE(x)		((x) << 2)

#define USBOTGSS_IDLEMODE_FORCE_IDLE		0
#define USBOTGSS_IDLEMODE_NO_IDLE		1
#define USBOTGSS_IDLEMODE_SMART_IDLE		2
#define USBOTGSS_IDLEMODE_SMART_WAKEUP		3

#define USBOTGSS_IDLEMODE_MASK			(0x03 << 2)

/* IRQ_EOI REGISTER */
#define USBOTGSS_IRQ_EOI_LINE_NUMBER		(1 << 0)

/* IRQS0 BITS */
#define USBOTGSS_IRQO_COREIRQ_ST		(1 << 0)

/* IRQ1 BITS */
#define USBOTGSS_IRQ1_DMADISABLECLR		(1 << 17)
#define USBOTGSS_IRQ1_OEVT			(1 << 16)
#define USBOTGSS_IRQ1_DRVVBUS_RISE		(1 << 13)
#define USBOTGSS_IRQ1_CHRGVBUS_RISE		(1 << 12)
#define USBOTGSS_IRQ1_DISCHRGVBUS_RISE		(1 << 11)
#define USBOTGSS_IRQ1_IDPULLUP_RISE		(1 << 8)
#define USBOTGSS_IRQ1_DRVVBUS_FALL		(1 << 5)
#define USBOTGSS_IRQ1_CHRGVBUS_FALL		(1 << 4)
#define USBOTGSS_IRQ1_DISCHRGVBUS_FALL		(1 << 3)
#define USBOTGSS_IRQ1_IDPULLUP_FALL		(1 << 0)

/* UTMI_OTG_CTRL REGISTER */
#define USBOTGSS_UTMI_OTG_CTRL_DRVVBUS		(1 << 5)
#define USBOTGSS_UTMI_OTG_CTRL_CHRGVBUS		(1 << 4)
#define USBOTGSS_UTMI_OTG_CTRL_DISCHRGVBUS	(1 << 3)
#define USBOTGSS_UTMI_OTG_CTRL_IDPULLUP		(1 << 0)

/* UTMI_OTG_STATUS REGISTER */
#define USBOTGSS_UTMI_OTG_STATUS_SW_MODE	(1 << 31)
#define USBOTGSS_UTMI_OTG_STATUS_POWERPRESENT	(1 << 9)
#define USBOTGSS_UTMI_OTG_STATUS_TXBITSTUFFENABLE (1 << 8)
#define USBOTGSS_UTMI_OTG_STATUS_IDDIG		(1 << 4)
#define USBOTGSS_UTMI_OTG_STATUS_SESSEND	(1 << 3)
#define USBOTGSS_UTMI_OTG_STATUS_SESSVALID	(1 << 2)
#define USBOTGSS_UTMI_OTG_STATUS_VBUSVALID	(1 << 1)

struct dwc3_omap {
	/* device lock */
	spinlock_t		lock;

	struct platform_device	*dwc3;
	struct platform_device	*usb2_phy;
	struct platform_device	*usb3_phy;
	struct device		*dev;

	int			irq;
	void __iomem		*base;

	void			*context;
	u32			resource_size;

	u32			dma_status:1;
};

static inline u32 dwc3_omap_readl(void __iomem *base, u32 offset)
{
	return readl(base + offset);
}

static inline void dwc3_omap_writel(void __iomem *base, u32 offset, u32 value)
{
	writel(value, base + offset);
}

static int __devinit dwc3_omap_register_phys(struct dwc3_omap *omap)
{
	struct nop_usb_xceiv_platform_data pdata;
	struct platform_device	*pdev;
	int			ret;

	memset(&pdata, 0x00, sizeof(pdata));

	pdev = platform_device_alloc("nop_usb_xceiv", 0);
	if (!pdev)
		return -ENOMEM;

	omap->usb2_phy = pdev;
	pdata.type = USB_PHY_TYPE_USB2;

	ret = platform_device_add_data(omap->usb2_phy, &pdata, sizeof(pdata));
	if (ret)
		goto err1;

	pdev = platform_device_alloc("nop_usb_xceiv", 1);
	if (!pdev) {
		ret = -ENOMEM;
		goto err1;
	}

	omap->usb3_phy = pdev;
	pdata.type = USB_PHY_TYPE_USB3;

	ret = platform_device_add_data(omap->usb3_phy, &pdata, sizeof(pdata));
	if (ret)
		goto err2;

	ret = platform_device_add(omap->usb2_phy);
	if (ret)
		goto err2;

	ret = platform_device_add(omap->usb3_phy);
	if (ret)
		goto err3;

	return 0;

err3:
	platform_device_del(omap->usb2_phy);

err2:
	platform_device_put(omap->usb3_phy);

err1:
	platform_device_put(omap->usb2_phy);

	return ret;
}

static irqreturn_t dwc3_omap_interrupt(int irq, void *_omap)
{
	struct dwc3_omap	*omap = _omap;
	u32			reg;

	spin_lock(&omap->lock);

	reg = dwc3_omap_readl(omap->base, USBOTGSS_IRQSTATUS_1);

	if (reg & USBOTGSS_IRQ1_DMADISABLECLR) {
		dev_dbg(omap->dev, "DMA Disable was Cleared\n");
		omap->dma_status = false;
	}

	if (reg & USBOTGSS_IRQ1_OEVT)
		dev_dbg(omap->dev, "OTG Event\n");

	if (reg & USBOTGSS_IRQ1_DRVVBUS_RISE)
		dev_dbg(omap->dev, "DRVVBUS Rise\n");

	if (reg & USBOTGSS_IRQ1_CHRGVBUS_RISE)
		dev_dbg(omap->dev, "CHRGVBUS Rise\n");

	if (reg & USBOTGSS_IRQ1_DISCHRGVBUS_RISE)
		dev_dbg(omap->dev, "DISCHRGVBUS Rise\n");

	if (reg & USBOTGSS_IRQ1_IDPULLUP_RISE)
		dev_dbg(omap->dev, "IDPULLUP Rise\n");

	if (reg & USBOTGSS_IRQ1_DRVVBUS_FALL)
		dev_dbg(omap->dev, "DRVVBUS Fall\n");

	if (reg & USBOTGSS_IRQ1_CHRGVBUS_FALL)
		dev_dbg(omap->dev, "CHRGVBUS Fall\n");

	if (reg & USBOTGSS_IRQ1_DISCHRGVBUS_FALL)
		dev_dbg(omap->dev, "DISCHRGVBUS Fall\n");

	if (reg & USBOTGSS_IRQ1_IDPULLUP_FALL)
		dev_dbg(omap->dev, "IDPULLUP Fall\n");

	dwc3_omap_writel(omap->base, USBOTGSS_IRQSTATUS_1, reg);

	reg = dwc3_omap_readl(omap->base, USBOTGSS_IRQSTATUS_0);
	dwc3_omap_writel(omap->base, USBOTGSS_IRQSTATUS_0, reg);

	spin_unlock(&omap->lock);

	return IRQ_HANDLED;
}

static int __devinit dwc3_omap_probe(struct platform_device *pdev)
{
	struct dwc3_omap_data	*pdata = pdev->dev.platform_data;
	struct device_node	*node = pdev->dev.of_node;

	struct platform_device	*dwc3;
	struct dwc3_omap	*omap;
	struct resource		*res;
	struct device		*dev = &pdev->dev;

	int			size;
	int			ret = -ENOMEM;
	int			irq;

	const u32		*utmi_mode;
	u32			reg;

	void __iomem		*base;
	void			*context;

	omap = devm_kzalloc(dev, sizeof(*omap), GFP_KERNEL);
	if (!omap) {
		dev_err(dev, "not enough memory\n");
		return -ENOMEM;
	}

	platform_set_drvdata(pdev, omap);

	irq = platform_get_irq(pdev, 1);
	if (irq < 0) {
		dev_err(dev, "missing IRQ resource\n");
		return -EINVAL;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	if (!res) {
		dev_err(dev, "missing memory base resource\n");
		return -EINVAL;
	}

	base = devm_ioremap_nocache(dev, res->start, resource_size(res));
	if (!base) {
		dev_err(dev, "ioremap failed\n");
		return -ENOMEM;
	}

	ret = dwc3_omap_register_phys(omap);
	if (ret) {
		dev_err(dev, "couldn't register PHYs\n");
		return ret;
	}

	dwc3 = platform_device_alloc("dwc3", PLATFORM_DEVID_AUTO);
	if (!dwc3) {
		dev_err(dev, "couldn't allocate dwc3 device\n");
		return -ENOMEM;
	}

	context = devm_kzalloc(dev, resource_size(res), GFP_KERNEL);
	if (!context) {
		dev_err(dev, "couldn't allocate dwc3 context memory\n");
		goto err2;
	}

	spin_lock_init(&omap->lock);
	dma_set_coherent_mask(&dwc3->dev, dev->coherent_dma_mask);

	dwc3->dev.parent = dev;
	dwc3->dev.dma_mask = dev->dma_mask;
	dwc3->dev.dma_parms = dev->dma_parms;
	omap->resource_size = resource_size(res);
	omap->context	= context;
	omap->dev	= dev;
	omap->irq	= irq;
	omap->base	= base;
	omap->dwc3	= dwc3;

	reg = dwc3_omap_readl(omap->base, USBOTGSS_UTMI_OTG_STATUS);

	utmi_mode = of_get_property(node, "utmi-mode", &size);
	if (utmi_mode && size == sizeof(*utmi_mode)) {
		reg |= *utmi_mode;
	} else {
		if (!pdata) {
			dev_dbg(dev, "missing platform data\n");
		} else {
			switch (pdata->utmi_mode) {
			case DWC3_OMAP_UTMI_MODE_SW:
				reg |= USBOTGSS_UTMI_OTG_STATUS_SW_MODE;
				break;
			case DWC3_OMAP_UTMI_MODE_HW:
				reg &= ~USBOTGSS_UTMI_OTG_STATUS_SW_MODE;
				break;
			default:
				dev_dbg(dev, "UNKNOWN utmi mode %d\n",
						pdata->utmi_mode);
			}
		}
	}

	dwc3_omap_writel(omap->base, USBOTGSS_UTMI_OTG_STATUS, reg);

	/* check the DMA Status */
	reg = dwc3_omap_readl(omap->base, USBOTGSS_SYSCONFIG);
	omap->dma_status = !!(reg & USBOTGSS_SYSCONFIG_DMADISABLE);

	/* Set No-Idle and No-Standby */
	reg &= ~(USBOTGSS_STANDBYMODE_MASK
			| USBOTGSS_IDLEMODE_MASK);

	reg |= (USBOTGSS_SYSCONFIG_STANDBYMODE(USBOTGSS_STANDBYMODE_NO_STANDBY)
		| USBOTGSS_SYSCONFIG_IDLEMODE(USBOTGSS_IDLEMODE_NO_IDLE));

	dwc3_omap_writel(omap->base, USBOTGSS_SYSCONFIG, reg);

	ret = devm_request_irq(dev, omap->irq, dwc3_omap_interrupt, 0,
			"dwc3-omap", omap);
	if (ret) {
		dev_err(dev, "failed to request IRQ #%d --> %d\n",
				omap->irq, ret);
		goto err2;
	}

	/* enable all IRQs */
	reg = USBOTGSS_IRQO_COREIRQ_ST;
	dwc3_omap_writel(omap->base, USBOTGSS_IRQENABLE_SET_0, reg);

	reg = (USBOTGSS_IRQ1_OEVT |
			USBOTGSS_IRQ1_DRVVBUS_RISE |
			USBOTGSS_IRQ1_CHRGVBUS_RISE |
			USBOTGSS_IRQ1_DISCHRGVBUS_RISE |
			USBOTGSS_IRQ1_IDPULLUP_RISE |
			USBOTGSS_IRQ1_DRVVBUS_FALL |
			USBOTGSS_IRQ1_CHRGVBUS_FALL |
			USBOTGSS_IRQ1_DISCHRGVBUS_FALL |
			USBOTGSS_IRQ1_IDPULLUP_FALL);

	dwc3_omap_writel(omap->base, USBOTGSS_IRQENABLE_SET_1, reg);

	ret = platform_device_add_resources(dwc3, pdev->resource,
			pdev->num_resources);
	if (ret) {
		dev_err(dev, "couldn't add resources to dwc3 device\n");
		goto err2;
	}

	ret = platform_device_add(dwc3);
	if (ret) {
		dev_err(dev, "failed to register dwc3 device\n");
		goto err2;
	}

	return 0;

err2:
	platform_device_put(dwc3);
	return ret;
}

static int __devexit dwc3_omap_remove(struct platform_device *pdev)
{
	struct dwc3_omap	*omap = platform_get_drvdata(pdev);

	platform_device_unregister(omap->dwc3);
	platform_device_unregister(omap->usb2_phy);
	platform_device_unregister(omap->usb3_phy);
	return 0;
}

static const struct of_device_id of_dwc3_matach[] = {
	{
		"ti,dwc3",
	},
	{ },
};
MODULE_DEVICE_TABLE(of, of_dwc3_matach);

static struct platform_driver dwc3_omap_driver = {
	.probe		= dwc3_omap_probe,
	.remove		= dwc3_omap_remove,
	.driver		= {
		.name	= "omap-dwc3",
		.of_match_table	= of_dwc3_matach,
	},
};

module_platform_driver(dwc3_omap_driver);

MODULE_ALIAS("platform:omap-dwc3");
MODULE_AUTHOR("Felipe Balbi <balbi@ti.com>");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_DESCRIPTION("DesignWare USB3 OMAP Glue Layer");
