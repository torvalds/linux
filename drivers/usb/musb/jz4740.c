// SPDX-License-Identifier: GPL-2.0+
/*
 * Ingenic JZ4740 "glue layer"
 *
 * Copyright (C) 2013, Apelete Seketeli <apelete@seketeli.net>
 */

#include <linux/clk.h>
#include <linux/dma-mapping.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/usb/role.h>
#include <linux/usb/usb_phy_generic.h>

#include "musb_core.h"

struct jz4740_glue {
	struct platform_device	*pdev;
	struct musb		*musb;
	struct clk		*clk;
	struct usb_role_switch	*role_sw;
};

static irqreturn_t jz4740_musb_interrupt(int irq, void *__hci)
{
	unsigned long	flags;
	irqreturn_t	retval = IRQ_NONE, retval_dma = IRQ_NONE;
	struct musb	*musb = __hci;

	if (IS_ENABLED(CONFIG_USB_INVENTRA_DMA) && musb->dma_controller)
		retval_dma = dma_controller_irq(irq, musb->dma_controller);

	spin_lock_irqsave(&musb->lock, flags);

	musb->int_usb = musb_readb(musb->mregs, MUSB_INTRUSB);
	musb->int_tx = musb_readw(musb->mregs, MUSB_INTRTX);
	musb->int_rx = musb_readw(musb->mregs, MUSB_INTRRX);

	/*
	 * The controller is gadget only, the state of the host mode IRQ bits is
	 * undefined. Mask them to make sure that the musb driver core will
	 * never see them set
	 */
	musb->int_usb &= MUSB_INTR_SUSPEND | MUSB_INTR_RESUME |
			 MUSB_INTR_RESET | MUSB_INTR_SOF;

	if (musb->int_usb || musb->int_tx || musb->int_rx)
		retval = musb_interrupt(musb);

	spin_unlock_irqrestore(&musb->lock, flags);

	if (retval == IRQ_HANDLED || retval_dma == IRQ_HANDLED)
		return IRQ_HANDLED;

	return IRQ_NONE;
}

static struct musb_fifo_cfg jz4740_musb_fifo_cfg[] = {
	{ .hw_ep_num = 1, .style = FIFO_TX, .maxpacket = 512, },
	{ .hw_ep_num = 1, .style = FIFO_RX, .maxpacket = 512, },
	{ .hw_ep_num = 2, .style = FIFO_TX, .maxpacket = 64, },
};

static const struct musb_hdrc_config jz4740_musb_config = {
	/* Silicon does not implement USB OTG. */
	.multipoint	= 0,
	/* Max EPs scanned, driver will decide which EP can be used. */
	.num_eps	= 4,
	/* RAMbits needed to configure EPs from table */
	.ram_bits	= 9,
	.fifo_cfg	= jz4740_musb_fifo_cfg,
	.fifo_cfg_size	= ARRAY_SIZE(jz4740_musb_fifo_cfg),
};

static int jz4740_musb_role_switch_set(struct usb_role_switch *sw,
				       enum usb_role role)
{
	struct jz4740_glue *glue = usb_role_switch_get_drvdata(sw);
	struct usb_phy *phy = glue->musb->xceiv;

	switch (role) {
	case USB_ROLE_NONE:
		atomic_notifier_call_chain(&phy->notifier, USB_EVENT_NONE, phy);
		break;
	case USB_ROLE_DEVICE:
		atomic_notifier_call_chain(&phy->notifier, USB_EVENT_VBUS, phy);
		break;
	case USB_ROLE_HOST:
		atomic_notifier_call_chain(&phy->notifier, USB_EVENT_ID, phy);
		break;
	}

	return 0;
}

static int jz4740_musb_init(struct musb *musb)
{
	struct device *dev = musb->controller->parent;
	struct jz4740_glue *glue = dev_get_drvdata(dev);
	struct usb_role_switch_desc role_sw_desc = {
		.set = jz4740_musb_role_switch_set,
		.driver_data = glue,
		.fwnode = dev_fwnode(dev),
	};
	int err;

	glue->musb = musb;

	if (dev->of_node)
		musb->xceiv = devm_usb_get_phy_by_phandle(dev, "phys", 0);
	else
		musb->xceiv = devm_usb_get_phy(dev, USB_PHY_TYPE_USB2);
	if (IS_ERR(musb->xceiv)) {
		err = PTR_ERR(musb->xceiv);
		if (err != -EPROBE_DEFER)
			dev_err(dev, "No transceiver configured: %d", err);
		return err;
	}

	glue->role_sw = usb_role_switch_register(dev, &role_sw_desc);
	if (IS_ERR(glue->role_sw)) {
		dev_err(dev, "Failed to register USB role switch");
		return PTR_ERR(glue->role_sw);
	}

	/*
	 * Silicon does not implement ConfigData register.
	 * Set dyn_fifo to avoid reading EP config from hardware.
	 */
	musb->dyn_fifo = true;

	musb->isr = jz4740_musb_interrupt;

	return 0;
}

static int jz4740_musb_exit(struct musb *musb)
{
	struct jz4740_glue *glue = dev_get_drvdata(musb->controller->parent);

	usb_role_switch_unregister(glue->role_sw);

	return 0;
}

static const struct musb_platform_ops jz4740_musb_ops = {
	.quirks		= MUSB_DMA_INVENTRA | MUSB_INDEXED_EP,
	.fifo_mode	= 2,
	.init		= jz4740_musb_init,
	.exit		= jz4740_musb_exit,
#ifdef CONFIG_USB_INVENTRA_DMA
	.dma_init	= musbhs_dma_controller_create_noirq,
	.dma_exit	= musbhs_dma_controller_destroy,
#endif
};

static const struct musb_hdrc_platform_data jz4740_musb_pdata = {
	.mode		= MUSB_PERIPHERAL,
	.config		= &jz4740_musb_config,
	.platform_ops	= &jz4740_musb_ops,
};

static struct musb_fifo_cfg jz4770_musb_fifo_cfg[] = {
	{ .hw_ep_num = 1, .style = FIFO_TX, .maxpacket = 512, },
	{ .hw_ep_num = 1, .style = FIFO_RX, .maxpacket = 512, },
	{ .hw_ep_num = 2, .style = FIFO_TX, .maxpacket = 512, },
	{ .hw_ep_num = 2, .style = FIFO_RX, .maxpacket = 512, },
	{ .hw_ep_num = 3, .style = FIFO_TX, .maxpacket = 512, },
	{ .hw_ep_num = 3, .style = FIFO_RX, .maxpacket = 512, },
	{ .hw_ep_num = 4, .style = FIFO_TX, .maxpacket = 512, },
	{ .hw_ep_num = 4, .style = FIFO_RX, .maxpacket = 512, },
	{ .hw_ep_num = 5, .style = FIFO_TX, .maxpacket = 512, },
	{ .hw_ep_num = 5, .style = FIFO_RX, .maxpacket = 512, },
};

static struct musb_hdrc_config jz4770_musb_config = {
	.multipoint	= 1,
	.num_eps	= 11,
	.ram_bits	= 11,
	.fifo_cfg	= jz4770_musb_fifo_cfg,
	.fifo_cfg_size	= ARRAY_SIZE(jz4770_musb_fifo_cfg),
};

static const struct musb_hdrc_platform_data jz4770_musb_pdata = {
	.mode		= MUSB_PERIPHERAL, /* TODO: support OTG */
	.config		= &jz4770_musb_config,
	.platform_ops	= &jz4740_musb_ops,
};

static int jz4740_probe(struct platform_device *pdev)
{
	struct device			*dev = &pdev->dev;
	const struct musb_hdrc_platform_data *pdata;
	struct platform_device		*musb;
	struct jz4740_glue		*glue;
	struct clk			*clk;
	int				ret;

	glue = devm_kzalloc(dev, sizeof(*glue), GFP_KERNEL);
	if (!glue)
		return -ENOMEM;

	pdata = of_device_get_match_data(dev);
	if (!pdata) {
		dev_err(dev, "missing platform data");
		return -EINVAL;
	}

	musb = platform_device_alloc("musb-hdrc", PLATFORM_DEVID_AUTO);
	if (!musb) {
		dev_err(dev, "failed to allocate musb device");
		return -ENOMEM;
	}

	clk = devm_clk_get(dev, "udc");
	if (IS_ERR(clk)) {
		dev_err(dev, "failed to get clock");
		ret = PTR_ERR(clk);
		goto err_platform_device_put;
	}

	ret = clk_prepare_enable(clk);
	if (ret) {
		dev_err(dev, "failed to enable clock");
		goto err_platform_device_put;
	}

	musb->dev.parent		= dev;
	musb->dev.dma_mask		= &musb->dev.coherent_dma_mask;
	musb->dev.coherent_dma_mask	= DMA_BIT_MASK(32);

	glue->pdev			= musb;
	glue->clk			= clk;

	platform_set_drvdata(pdev, glue);

	ret = platform_device_add_resources(musb, pdev->resource,
					    pdev->num_resources);
	if (ret) {
		dev_err(dev, "failed to add resources");
		goto err_clk_disable;
	}

	ret = platform_device_add_data(musb, pdata, sizeof(*pdata));
	if (ret) {
		dev_err(dev, "failed to add platform_data");
		goto err_clk_disable;
	}

	ret = platform_device_add(musb);
	if (ret) {
		dev_err(dev, "failed to register musb device");
		goto err_clk_disable;
	}

	return 0;

err_clk_disable:
	clk_disable_unprepare(clk);
err_platform_device_put:
	platform_device_put(musb);
	return ret;
}

static int jz4740_remove(struct platform_device *pdev)
{
	struct jz4740_glue *glue = platform_get_drvdata(pdev);

	platform_device_unregister(glue->pdev);
	clk_disable_unprepare(glue->clk);

	return 0;
}

static const struct of_device_id jz4740_musb_of_match[] = {
	{ .compatible = "ingenic,jz4740-musb", .data = &jz4740_musb_pdata },
	{ .compatible = "ingenic,jz4770-musb", .data = &jz4770_musb_pdata },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, jz4740_musb_of_match);

static struct platform_driver jz4740_driver = {
	.probe		= jz4740_probe,
	.remove		= jz4740_remove,
	.driver		= {
		.name	= "musb-jz4740",
		.of_match_table = jz4740_musb_of_match,
	},
};

MODULE_DESCRIPTION("JZ4740 MUSB Glue Layer");
MODULE_AUTHOR("Apelete Seketeli <apelete@seketeli.net>");
MODULE_LICENSE("GPL v2");
module_platform_driver(jz4740_driver);
