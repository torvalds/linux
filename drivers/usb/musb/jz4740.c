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
#include <linux/usb/usb_phy_generic.h>

#include "musb_core.h"

struct jz4740_glue {
	struct platform_device	*pdev;
	struct clk		*clk;
};

static irqreturn_t jz4740_musb_interrupt(int irq, void *__hci)
{
	unsigned long	flags;
	irqreturn_t	retval = IRQ_NONE;
	struct musb	*musb = __hci;

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

	return retval;
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

static int jz4740_musb_init(struct musb *musb)
{
	struct device *dev = musb->controller->parent;
	int err;

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

	/*
	 * Silicon does not implement ConfigData register.
	 * Set dyn_fifo to avoid reading EP config from hardware.
	 */
	musb->dyn_fifo = true;

	musb->isr = jz4740_musb_interrupt;

	return 0;
}

/*
 * DMA has not been confirmed to work with CONFIG_USB_INVENTRA_DMA,
 * so let's not set up the dma function pointers yet.
 */
static const struct musb_platform_ops jz4740_musb_ops = {
	.quirks		= MUSB_DMA_INVENTRA | MUSB_INDEXED_EP,
	.fifo_mode	= 2,
	.init		= jz4740_musb_init,
};

static const struct musb_hdrc_platform_data jz4740_musb_pdata = {
	.mode		= MUSB_PERIPHERAL,
	.config		= &jz4740_musb_config,
	.platform_ops	= &jz4740_musb_ops,
};

static int jz4740_probe(struct platform_device *pdev)
{
	struct device			*dev = &pdev->dev;
	const struct musb_hdrc_platform_data *pdata = &jz4740_musb_pdata;
	struct platform_device		*musb;
	struct jz4740_glue		*glue;
	struct clk			*clk;
	int				ret;

	glue = devm_kzalloc(dev, sizeof(*glue), GFP_KERNEL);
	if (!glue)
		return -ENOMEM;

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

#ifdef CONFIG_OF
static const struct of_device_id jz4740_musb_of_match[] = {
	{ .compatible = "ingenic,jz4740-musb" },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, jz4740_musb_of_match);
#endif

static struct platform_driver jz4740_driver = {
	.probe		= jz4740_probe,
	.remove		= jz4740_remove,
	.driver		= {
		.name	= "musb-jz4740",
		.of_match_table = of_match_ptr(jz4740_musb_of_match),
	},
};

MODULE_DESCRIPTION("JZ4740 MUSB Glue Layer");
MODULE_AUTHOR("Apelete Seketeli <apelete@seketeli.net>");
MODULE_LICENSE("GPL v2");
module_platform_driver(jz4740_driver);
