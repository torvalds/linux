/*
 * Copyright (C) 2010 ST-Ericsson AB
 * Mian Yousaf Kaukab <mian.yousaf.kaukab@stericsson.com>
 *
 * Based on omap2430.c
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
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/usb/musb-ux500.h>

#include "musb_core.h"

struct ux500_glue {
	struct device		*dev;
	struct platform_device	*musb;
	struct clk		*clk;
};
#define glue_to_musb(g)	platform_get_drvdata(g->musb)

static void ux500_musb_set_vbus(struct musb *musb, int is_on)
{
	u8            devctl;
	unsigned long timeout = jiffies + msecs_to_jiffies(1000);
	/* HDRC controls CPEN, but beware current surges during device
	 * connect.  They can trigger transient overcurrent conditions
	 * that must be ignored.
	 */

	devctl = musb_readb(musb->mregs, MUSB_DEVCTL);

	if (is_on) {
		if (musb->xceiv->state == OTG_STATE_A_IDLE) {
			/* start the session */
			devctl |= MUSB_DEVCTL_SESSION;
			musb_writeb(musb->mregs, MUSB_DEVCTL, devctl);
			/*
			 * Wait for the musb to set as A device to enable the
			 * VBUS
			 */
			while (musb_readb(musb->mregs, MUSB_DEVCTL) & 0x80) {

				if (time_after(jiffies, timeout)) {
					dev_err(musb->controller,
					"configured as A device timeout");
					break;
				}
			}

		} else {
			musb->is_active = 1;
			musb->xceiv->otg->default_a = 1;
			musb->xceiv->state = OTG_STATE_A_WAIT_VRISE;
			devctl |= MUSB_DEVCTL_SESSION;
			MUSB_HST_MODE(musb);
		}
	} else {
		musb->is_active = 0;

		/* NOTE: we're skipping A_WAIT_VFALL -> A_IDLE and jumping
		 * right to B_IDLE...
		 */
		musb->xceiv->otg->default_a = 0;
		devctl &= ~MUSB_DEVCTL_SESSION;
		MUSB_DEV_MODE(musb);
	}
	musb_writeb(musb->mregs, MUSB_DEVCTL, devctl);

	/*
	 * Devctl values will be updated after vbus goes below
	 * session_valid. The time taken depends on the capacitance
	 * on VBUS line. The max discharge time can be upto 1 sec
	 * as per the spec. Typically on our platform, it is 200ms
	 */
	if (!is_on)
		mdelay(200);

	dev_dbg(musb->controller, "VBUS %s, devctl %02x\n",
		usb_otg_state_string(musb->xceiv->state),
		musb_readb(musb->mregs, MUSB_DEVCTL));
}

static int musb_otg_notifications(struct notifier_block *nb,
		unsigned long event, void *unused)
{
	struct musb *musb = container_of(nb, struct musb, nb);

	dev_dbg(musb->controller, "musb_otg_notifications %ld %s\n",
			event, usb_otg_state_string(musb->xceiv->state));

	switch (event) {
	case UX500_MUSB_ID:
		dev_dbg(musb->controller, "ID GND\n");
		ux500_musb_set_vbus(musb, 1);
		break;
	case UX500_MUSB_VBUS:
		dev_dbg(musb->controller, "VBUS Connect\n");
		break;
	case UX500_MUSB_NONE:
		dev_dbg(musb->controller, "VBUS Disconnect\n");
		if (is_host_active(musb))
			ux500_musb_set_vbus(musb, 0);
		else
			musb->xceiv->state = OTG_STATE_B_IDLE;
		break;
	default:
		dev_dbg(musb->controller, "ID float\n");
		return NOTIFY_DONE;
	}
	return NOTIFY_OK;
}

static irqreturn_t ux500_musb_interrupt(int irq, void *__hci)
{
	unsigned long   flags;
	irqreturn_t     retval = IRQ_NONE;
	struct musb     *musb = __hci;

	spin_lock_irqsave(&musb->lock, flags);

	musb->int_usb = musb_readb(musb->mregs, MUSB_INTRUSB);
	musb->int_tx = musb_readw(musb->mregs, MUSB_INTRTX);
	musb->int_rx = musb_readw(musb->mregs, MUSB_INTRRX);

	if (musb->int_usb || musb->int_tx || musb->int_rx)
		retval = musb_interrupt(musb);

	spin_unlock_irqrestore(&musb->lock, flags);

	return retval;
}

static int ux500_musb_init(struct musb *musb)
{
	int status;

	musb->xceiv = usb_get_phy(USB_PHY_TYPE_USB2);
	if (IS_ERR_OR_NULL(musb->xceiv)) {
		pr_err("HS USB OTG: no transceiver configured\n");
		return -EPROBE_DEFER;
	}

	musb->nb.notifier_call = musb_otg_notifications;
	status = usb_register_notifier(musb->xceiv, &musb->nb);
	if (status < 0) {
		dev_dbg(musb->controller, "notification register failed\n");
		return status;
	}

	musb->isr = ux500_musb_interrupt;

	return 0;
}

static int ux500_musb_exit(struct musb *musb)
{
	usb_unregister_notifier(musb->xceiv, &musb->nb);

	usb_put_phy(musb->xceiv);

	return 0;
}

static const struct musb_platform_ops ux500_ops = {
	.init		= ux500_musb_init,
	.exit		= ux500_musb_exit,

	.set_vbus	= ux500_musb_set_vbus,
};

static int ux500_probe(struct platform_device *pdev)
{
	struct musb_hdrc_platform_data	*pdata = pdev->dev.platform_data;
	struct platform_device		*musb;
	struct ux500_glue		*glue;
	struct clk			*clk;
	int				ret = -ENOMEM;

	glue = kzalloc(sizeof(*glue), GFP_KERNEL);
	if (!glue) {
		dev_err(&pdev->dev, "failed to allocate glue context\n");
		goto err0;
	}

	musb = platform_device_alloc("musb-hdrc", PLATFORM_DEVID_AUTO);
	if (!musb) {
		dev_err(&pdev->dev, "failed to allocate musb device\n");
		goto err1;
	}

	clk = clk_get(&pdev->dev, "usb");
	if (IS_ERR(clk)) {
		dev_err(&pdev->dev, "failed to get clock\n");
		ret = PTR_ERR(clk);
		goto err3;
	}

	ret = clk_prepare_enable(clk);
	if (ret) {
		dev_err(&pdev->dev, "failed to enable clock\n");
		goto err4;
	}

	musb->dev.parent		= &pdev->dev;
	musb->dev.dma_mask		= pdev->dev.dma_mask;
	musb->dev.coherent_dma_mask	= pdev->dev.coherent_dma_mask;

	glue->dev			= &pdev->dev;
	glue->musb			= musb;
	glue->clk			= clk;

	pdata->platform_ops		= &ux500_ops;

	platform_set_drvdata(pdev, glue);

	ret = platform_device_add_resources(musb, pdev->resource,
			pdev->num_resources);
	if (ret) {
		dev_err(&pdev->dev, "failed to add resources\n");
		goto err5;
	}

	ret = platform_device_add_data(musb, pdata, sizeof(*pdata));
	if (ret) {
		dev_err(&pdev->dev, "failed to add platform_data\n");
		goto err5;
	}

	ret = platform_device_add(musb);
	if (ret) {
		dev_err(&pdev->dev, "failed to register musb device\n");
		goto err5;
	}

	return 0;

err5:
	clk_disable_unprepare(clk);

err4:
	clk_put(clk);

err3:
	platform_device_put(musb);

err1:
	kfree(glue);

err0:
	return ret;
}

static int ux500_remove(struct platform_device *pdev)
{
	struct ux500_glue	*glue = platform_get_drvdata(pdev);

	platform_device_unregister(glue->musb);
	clk_disable_unprepare(glue->clk);
	clk_put(glue->clk);
	kfree(glue);

	return 0;
}

#ifdef CONFIG_PM
static int ux500_suspend(struct device *dev)
{
	struct ux500_glue	*glue = dev_get_drvdata(dev);
	struct musb		*musb = glue_to_musb(glue);

	usb_phy_set_suspend(musb->xceiv, 1);
	clk_disable_unprepare(glue->clk);

	return 0;
}

static int ux500_resume(struct device *dev)
{
	struct ux500_glue	*glue = dev_get_drvdata(dev);
	struct musb		*musb = glue_to_musb(glue);
	int			ret;

	ret = clk_prepare_enable(glue->clk);
	if (ret) {
		dev_err(dev, "failed to enable clock\n");
		return ret;
	}

	usb_phy_set_suspend(musb->xceiv, 0);

	return 0;
}

static const struct dev_pm_ops ux500_pm_ops = {
	.suspend	= ux500_suspend,
	.resume		= ux500_resume,
};

#define DEV_PM_OPS	(&ux500_pm_ops)
#else
#define DEV_PM_OPS	NULL
#endif

static struct platform_driver ux500_driver = {
	.probe		= ux500_probe,
	.remove		= ux500_remove,
	.driver		= {
		.name	= "musb-ux500",
		.pm	= DEV_PM_OPS,
	},
};

MODULE_DESCRIPTION("UX500 MUSB Glue Layer");
MODULE_AUTHOR("Mian Yousaf Kaukab <mian.yousaf.kaukab@stericsson.com>");
MODULE_LICENSE("GPL v2");
module_platform_driver(ux500_driver);
