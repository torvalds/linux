// SPDX-License-Identifier: GPL-2.0
/*
 * PolarFire SoC (MPFS) MUSB Glue Layer
 *
 * Copyright (c) 2020-2022 Microchip Corporation. All rights reserved.
 * Based on {omap2430,tusb6010,ux500}.c
 *
 */

#include <linux/clk.h>
#include <linux/dma-mapping.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/usb/usb_phy_generic.h>
#include "musb_core.h"
#include "musb_dma.h"

#define MPFS_MUSB_MAX_EP_NUM	8
#define MPFS_MUSB_RAM_BITS	12

struct mpfs_glue {
	struct device *dev;
	struct platform_device *musb;
	struct platform_device *phy;
	struct clk *clk;
};

static struct musb_fifo_cfg mpfs_musb_mode_cfg[] = {
	{ .hw_ep_num = 1, .style = FIFO_TX, .maxpacket = 512, },
	{ .hw_ep_num = 1, .style = FIFO_RX, .maxpacket = 512, },
	{ .hw_ep_num = 2, .style = FIFO_TX, .maxpacket = 512, },
	{ .hw_ep_num = 2, .style = FIFO_RX, .maxpacket = 512, },
	{ .hw_ep_num = 3, .style = FIFO_TX, .maxpacket = 512, },
	{ .hw_ep_num = 3, .style = FIFO_RX, .maxpacket = 512, },
	{ .hw_ep_num = 4, .style = FIFO_TX, .maxpacket = 1024, },
	{ .hw_ep_num = 4, .style = FIFO_RX, .maxpacket = 4096, },
};

static const struct musb_hdrc_config mpfs_musb_hdrc_config = {
	.fifo_cfg = mpfs_musb_mode_cfg,
	.fifo_cfg_size = ARRAY_SIZE(mpfs_musb_mode_cfg),
	.multipoint = true,
	.dyn_fifo = true,
	.num_eps = MPFS_MUSB_MAX_EP_NUM,
	.ram_bits = MPFS_MUSB_RAM_BITS,
};

static irqreturn_t mpfs_musb_interrupt(int irq, void *__hci)
{
	unsigned long flags;
	irqreturn_t ret = IRQ_NONE;
	struct musb *musb = __hci;

	spin_lock_irqsave(&musb->lock, flags);

	musb->int_usb = musb_readb(musb->mregs, MUSB_INTRUSB);
	musb->int_tx = musb_readw(musb->mregs, MUSB_INTRTX);
	musb->int_rx = musb_readw(musb->mregs, MUSB_INTRRX);

	if (musb->int_usb || musb->int_tx || musb->int_rx) {
		musb_writeb(musb->mregs, MUSB_INTRUSB, musb->int_usb);
		musb_writew(musb->mregs, MUSB_INTRTX, musb->int_tx);
		musb_writew(musb->mregs, MUSB_INTRRX, musb->int_rx);
		ret = musb_interrupt(musb);
	}

	spin_unlock_irqrestore(&musb->lock, flags);

	return ret;
}

static void mpfs_musb_set_vbus(struct musb *musb, int is_on)
{
	u8 devctl;

	/*
	 * HDRC controls CPEN, but beware current surges during device
	 * connect.  They can trigger transient overcurrent conditions
	 * that must be ignored.
	 */
	devctl = musb_readb(musb->mregs, MUSB_DEVCTL);

	if (is_on) {
		musb->is_active = 1;
		musb->xceiv->otg->default_a = 1;
		musb->xceiv->otg->state = OTG_STATE_A_WAIT_VRISE;
		devctl |= MUSB_DEVCTL_SESSION;
		MUSB_HST_MODE(musb);
	} else {
		musb->is_active = 0;

		/*
		 * NOTE:  skipping A_WAIT_VFALL -> A_IDLE and
		 * jumping right to B_IDLE...
		 */
		musb->xceiv->otg->default_a = 0;
		musb->xceiv->otg->state = OTG_STATE_B_IDLE;
		devctl &= ~MUSB_DEVCTL_SESSION;

		MUSB_DEV_MODE(musb);
	}

	musb_writeb(musb->mregs, MUSB_DEVCTL, devctl);

	dev_dbg(musb->controller, "VBUS %s, devctl %02x\n",
		usb_otg_state_string(musb->xceiv->otg->state),
		musb_readb(musb->mregs, MUSB_DEVCTL));
}

static int mpfs_musb_init(struct musb *musb)
{
	struct device *dev = musb->controller;

	musb->xceiv = devm_usb_get_phy(dev, USB_PHY_TYPE_USB2);
	if (IS_ERR(musb->xceiv)) {
		dev_err(dev, "HS UDC: no transceiver configured\n");
		return PTR_ERR(musb->xceiv);
	}

	musb->dyn_fifo = true;
	musb->isr = mpfs_musb_interrupt;

	musb_platform_set_vbus(musb, 1);

	return 0;
}

static const struct musb_platform_ops mpfs_ops = {
	.quirks		= MUSB_DMA_INVENTRA,
	.init		= mpfs_musb_init,
	.fifo_mode	= 2,
#ifdef CONFIG_USB_INVENTRA_DMA
	.dma_init	= musbhs_dma_controller_create,
	.dma_exit	= musbhs_dma_controller_destroy,
#endif
	.set_vbus	= mpfs_musb_set_vbus
};

static int mpfs_probe(struct platform_device *pdev)
{
	struct musb_hdrc_platform_data *pdata = dev_get_platdata(&pdev->dev);
	struct mpfs_glue *glue;
	struct platform_device *musb_pdev;
	struct device *dev = &pdev->dev;
	struct clk *clk;
	int ret;

	glue = devm_kzalloc(dev, sizeof(*glue), GFP_KERNEL);
	if (!glue)
		return -ENOMEM;

	musb_pdev = platform_device_alloc("musb-hdrc", PLATFORM_DEVID_AUTO);
	if (!musb_pdev) {
		dev_err(dev, "failed to allocate musb device\n");
		return -ENOMEM;
	}

	clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(clk)) {
		dev_err(&pdev->dev, "failed to get clock\n");
		ret = PTR_ERR(clk);
		goto err_phy_release;
	}

	ret = clk_prepare_enable(clk);
	if (ret) {
		dev_err(&pdev->dev, "failed to enable clock\n");
		goto err_phy_release;
	}

	musb_pdev->dev.parent = dev;
	musb_pdev->dev.coherent_dma_mask = DMA_BIT_MASK(39);
	musb_pdev->dev.dma_mask = &musb_pdev->dev.coherent_dma_mask;
	device_set_of_node_from_dev(&musb_pdev->dev, dev);

	glue->dev = dev;
	glue->musb = musb_pdev;
	glue->clk = clk;

	pdata = devm_kzalloc(dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata) {
		ret = -ENOMEM;
		goto err_clk_disable;
	}

	pdata->config = &mpfs_musb_hdrc_config;
	pdata->platform_ops = &mpfs_ops;

	pdata->mode = usb_get_dr_mode(dev);
	if (pdata->mode == USB_DR_MODE_UNKNOWN) {
		dev_info(dev, "No dr_mode property found, defaulting to otg\n");
		pdata->mode = USB_DR_MODE_OTG;
	}

	glue->phy = usb_phy_generic_register();
	if (IS_ERR(glue->phy)) {
		dev_err(dev, "failed to register usb-phy %ld\n",
			PTR_ERR(glue->phy));
		ret = PTR_ERR(glue->phy);
		goto err_clk_disable;
	}

	platform_set_drvdata(pdev, glue);

	ret = platform_device_add_resources(musb_pdev, pdev->resource, pdev->num_resources);
	if (ret) {
		dev_err(dev, "failed to add resources\n");
		goto err_clk_disable;
	}

	ret = platform_device_add_data(musb_pdev, pdata, sizeof(*pdata));
	if (ret) {
		dev_err(dev, "failed to add platform_data\n");
		goto err_clk_disable;
	}

	ret = platform_device_add(musb_pdev);
	if (ret) {
		dev_err(dev, "failed to register musb device\n");
		goto err_clk_disable;
	}

	dev_info(&pdev->dev, "Registered MPFS MUSB driver\n");
	return 0;

err_clk_disable:
	clk_disable_unprepare(clk);

err_phy_release:
	usb_phy_generic_unregister(glue->phy);
	platform_device_put(musb_pdev);
	return ret;
}

static void mpfs_remove(struct platform_device *pdev)
{
	struct mpfs_glue *glue = platform_get_drvdata(pdev);

	clk_disable_unprepare(glue->clk);
	platform_device_unregister(glue->musb);
	usb_phy_generic_unregister(pdev);
}

#ifdef CONFIG_OF
static const struct of_device_id mpfs_id_table[] = {
	{ .compatible = "microchip,mpfs-musb" },
	{ }
};
MODULE_DEVICE_TABLE(of, mpfs_id_table);
#endif

static struct platform_driver mpfs_musb_driver = {
	.probe = mpfs_probe,
	.remove_new = mpfs_remove,
	.driver = {
		.name = "mpfs-musb",
		.of_match_table = of_match_ptr(mpfs_id_table)
	},
};

module_platform_driver(mpfs_musb_driver);

MODULE_DESCRIPTION("PolarFire SoC MUSB Glue Layer");
MODULE_LICENSE("GPL");
