// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2005-2007 by Texas Instruments
 * Some code has been taken from tusb6010.c
 * Copyrights for that are attributable to:
 * Copyright (C) 2006 Nokia Corporation
 * Tony Lindgren <tony@atomide.com>
 *
 * This file is part of the Inventra Controller Driver for Linux.
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/pm_runtime.h>
#include <linux/err.h>
#include <linux/delay.h>
#include <linux/usb/musb.h>
#include <linux/phy/omap_control_phy.h>
#include <linux/of_platform.h>

#include "musb_core.h"
#include "omap2430.h"

struct omap2430_glue {
	struct device		*dev;
	struct platform_device	*musb;
	enum musb_vbus_id_status status;
	struct work_struct	omap_musb_mailbox_work;
	struct device		*control_otghs;
};
#define glue_to_musb(g)		platform_get_drvdata(g->musb)

static struct omap2430_glue	*_glue;

static void omap2430_musb_set_vbus(struct musb *musb, int is_on)
{
	struct usb_otg	*otg = musb->xceiv->otg;
	u8		devctl;
	unsigned long timeout = jiffies + msecs_to_jiffies(1000);
	/* HDRC controls CPEN, but beware current surges during device
	 * connect.  They can trigger transient overcurrent conditions
	 * that must be ignored.
	 */

	devctl = musb_readb(musb->mregs, MUSB_DEVCTL);

	if (is_on) {
		if (musb->xceiv->otg->state == OTG_STATE_A_IDLE) {
			int loops = 100;
			/* start the session */
			devctl |= MUSB_DEVCTL_SESSION;
			musb_writeb(musb->mregs, MUSB_DEVCTL, devctl);
			/*
			 * Wait for the musb to set as A device to enable the
			 * VBUS
			 */
			while (musb_readb(musb->mregs, MUSB_DEVCTL) &
			       MUSB_DEVCTL_BDEVICE) {

				mdelay(5);
				cpu_relax();

				if (time_after(jiffies, timeout)
				    || loops-- <= 0) {
					dev_err(musb->controller,
					"configured as A device timeout");
					break;
				}
			}

			otg_set_vbus(otg, 1);
		} else {
			musb->is_active = 1;
			musb->xceiv->otg->state = OTG_STATE_A_WAIT_VRISE;
			devctl |= MUSB_DEVCTL_SESSION;
			MUSB_HST_MODE(musb);
		}
	} else {
		musb->is_active = 0;

		/* NOTE:  we're skipping A_WAIT_VFALL -> A_IDLE and
		 * jumping right to B_IDLE...
		 */

		musb->xceiv->otg->state = OTG_STATE_B_IDLE;
		devctl &= ~MUSB_DEVCTL_SESSION;

		MUSB_DEV_MODE(musb);
	}
	musb_writeb(musb->mregs, MUSB_DEVCTL, devctl);

	dev_dbg(musb->controller, "VBUS %s, devctl %02x "
		/* otg %3x conf %08x prcm %08x */ "\n",
		usb_otg_state_string(musb->xceiv->otg->state),
		musb_readb(musb->mregs, MUSB_DEVCTL));
}

static inline void omap2430_low_level_exit(struct musb *musb)
{
	u32 l;

	/* in any role */
	l = musb_readl(musb->mregs, OTG_FORCESTDBY);
	l |= ENABLEFORCE;	/* enable MSTANDBY */
	musb_writel(musb->mregs, OTG_FORCESTDBY, l);
}

static inline void omap2430_low_level_init(struct musb *musb)
{
	u32 l;

	l = musb_readl(musb->mregs, OTG_FORCESTDBY);
	l &= ~ENABLEFORCE;	/* disable MSTANDBY */
	musb_writel(musb->mregs, OTG_FORCESTDBY, l);
}

static int omap2430_musb_mailbox(enum musb_vbus_id_status status)
{
	struct omap2430_glue	*glue = _glue;

	if (!glue) {
		pr_err("%s: musb core is not yet initialized\n", __func__);
		return -EPROBE_DEFER;
	}
	glue->status = status;

	if (!glue_to_musb(glue)) {
		pr_err("%s: musb core is not yet ready\n", __func__);
		return -EPROBE_DEFER;
	}

	schedule_work(&glue->omap_musb_mailbox_work);

	return 0;
}

static void omap_musb_set_mailbox(struct omap2430_glue *glue)
{
	struct musb *musb = glue_to_musb(glue);
	struct musb_hdrc_platform_data *pdata =
		dev_get_platdata(musb->controller);
	struct omap_musb_board_data *data = pdata->board_data;

	pm_runtime_get_sync(musb->controller);
	switch (glue->status) {
	case MUSB_ID_GROUND:
		dev_dbg(musb->controller, "ID GND\n");

		musb->xceiv->otg->state = OTG_STATE_A_IDLE;
		musb->xceiv->last_event = USB_EVENT_ID;
		if (musb->gadget_driver) {
			omap_control_usb_set_mode(glue->control_otghs,
				USB_MODE_HOST);
			omap2430_musb_set_vbus(musb, 1);
		}
		break;

	case MUSB_VBUS_VALID:
		dev_dbg(musb->controller, "VBUS Connect\n");

		musb->xceiv->otg->state = OTG_STATE_B_IDLE;
		musb->xceiv->last_event = USB_EVENT_VBUS;
		omap_control_usb_set_mode(glue->control_otghs, USB_MODE_DEVICE);
		break;

	case MUSB_ID_FLOAT:
	case MUSB_VBUS_OFF:
		dev_dbg(musb->controller, "VBUS Disconnect\n");

		musb->xceiv->last_event = USB_EVENT_NONE;
		if (musb->gadget_driver)
			omap2430_musb_set_vbus(musb, 0);

		if (data->interface_type == MUSB_INTERFACE_UTMI)
			otg_set_vbus(musb->xceiv->otg, 0);

		omap_control_usb_set_mode(glue->control_otghs,
			USB_MODE_DISCONNECT);
		break;
	default:
		dev_dbg(musb->controller, "ID float\n");
	}
	pm_runtime_mark_last_busy(musb->controller);
	pm_runtime_put_autosuspend(musb->controller);
	atomic_notifier_call_chain(&musb->xceiv->notifier,
			musb->xceiv->last_event, NULL);
}


static void omap_musb_mailbox_work(struct work_struct *mailbox_work)
{
	struct omap2430_glue *glue = container_of(mailbox_work,
				struct omap2430_glue, omap_musb_mailbox_work);

	omap_musb_set_mailbox(glue);
}

static irqreturn_t omap2430_musb_interrupt(int irq, void *__hci)
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

static int omap2430_musb_init(struct musb *musb)
{
	u32 l;
	int status = 0;
	struct device *dev = musb->controller;
	struct omap2430_glue *glue = dev_get_drvdata(dev->parent);
	struct musb_hdrc_platform_data *plat = dev_get_platdata(dev);
	struct omap_musb_board_data *data = plat->board_data;

	/* We require some kind of external transceiver, hooked
	 * up through ULPI.  TWL4030-family PMICs include one,
	 * which needs a driver, drivers aren't always needed.
	 */
	musb->phy = devm_phy_get(dev->parent, "usb2-phy");

	/* We can't totally remove musb->xceiv as of now because
	 * musb core uses xceiv.state and xceiv.otg. Once we have
	 * a separate state machine to handle otg, these can be moved
	 * out of xceiv and then we can start using the generic PHY
	 * framework
	 */
	musb->xceiv = devm_usb_get_phy_by_phandle(dev->parent, "usb-phy", 0);

	if (IS_ERR(musb->xceiv)) {
		status = PTR_ERR(musb->xceiv);

		if (status == -ENXIO)
			return status;

		dev_dbg(dev, "HS USB OTG: no transceiver configured\n");
		return -EPROBE_DEFER;
	}

	if (IS_ERR(musb->phy)) {
		dev_err(dev, "HS USB OTG: no PHY configured\n");
		return PTR_ERR(musb->phy);
	}
	musb->isr = omap2430_musb_interrupt;
	phy_init(musb->phy);
	phy_power_on(musb->phy);

	l = musb_readl(musb->mregs, OTG_INTERFSEL);

	if (data->interface_type == MUSB_INTERFACE_UTMI) {
		/* OMAP4 uses Internal PHY GS70 which uses UTMI interface */
		l &= ~ULPI_12PIN;       /* Disable ULPI */
		l |= UTMI_8BIT;         /* Enable UTMI  */
	} else {
		l |= ULPI_12PIN;
	}

	musb_writel(musb->mregs, OTG_INTERFSEL, l);

	dev_dbg(dev, "HS USB OTG: revision 0x%x, sysconfig 0x%02x, "
			"sysstatus 0x%x, intrfsel 0x%x, simenable  0x%x\n",
			musb_readl(musb->mregs, OTG_REVISION),
			musb_readl(musb->mregs, OTG_SYSCONFIG),
			musb_readl(musb->mregs, OTG_SYSSTATUS),
			musb_readl(musb->mregs, OTG_INTERFSEL),
			musb_readl(musb->mregs, OTG_SIMENABLE));

	if (glue->status != MUSB_UNKNOWN)
		omap_musb_set_mailbox(glue);

	return 0;
}

static void omap2430_musb_enable(struct musb *musb)
{
	u8		devctl;
	unsigned long timeout = jiffies + msecs_to_jiffies(1000);
	struct device *dev = musb->controller;
	struct omap2430_glue *glue = dev_get_drvdata(dev->parent);
	struct musb_hdrc_platform_data *pdata = dev_get_platdata(dev);
	struct omap_musb_board_data *data = pdata->board_data;


	switch (glue->status) {

	case MUSB_ID_GROUND:
		omap_control_usb_set_mode(glue->control_otghs, USB_MODE_HOST);
		if (data->interface_type != MUSB_INTERFACE_UTMI)
			break;
		devctl = musb_readb(musb->mregs, MUSB_DEVCTL);
		/* start the session */
		devctl |= MUSB_DEVCTL_SESSION;
		musb_writeb(musb->mregs, MUSB_DEVCTL, devctl);
		while (musb_readb(musb->mregs, MUSB_DEVCTL) &
				MUSB_DEVCTL_BDEVICE) {
			cpu_relax();

			if (time_after(jiffies, timeout)) {
				dev_err(dev, "configured as A device timeout");
				break;
			}
		}
		break;

	case MUSB_VBUS_VALID:
		omap_control_usb_set_mode(glue->control_otghs, USB_MODE_DEVICE);
		break;

	default:
		break;
	}
}

static void omap2430_musb_disable(struct musb *musb)
{
	struct device *dev = musb->controller;
	struct omap2430_glue *glue = dev_get_drvdata(dev->parent);

	if (glue->status != MUSB_UNKNOWN)
		omap_control_usb_set_mode(glue->control_otghs,
			USB_MODE_DISCONNECT);
}

static int omap2430_musb_exit(struct musb *musb)
{
	struct device *dev = musb->controller;
	struct omap2430_glue *glue = dev_get_drvdata(dev->parent);

	omap2430_low_level_exit(musb);
	phy_power_off(musb->phy);
	phy_exit(musb->phy);
	musb->phy = NULL;
	cancel_work_sync(&glue->omap_musb_mailbox_work);

	return 0;
}

static const struct musb_platform_ops omap2430_ops = {
	.quirks		= MUSB_DMA_INVENTRA,
#ifdef CONFIG_USB_INVENTRA_DMA
	.dma_init	= musbhs_dma_controller_create,
	.dma_exit	= musbhs_dma_controller_destroy,
#endif
	.init		= omap2430_musb_init,
	.exit		= omap2430_musb_exit,

	.set_vbus	= omap2430_musb_set_vbus,

	.enable		= omap2430_musb_enable,
	.disable	= omap2430_musb_disable,

	.phy_callback	= omap2430_musb_mailbox,
};

static u64 omap2430_dmamask = DMA_BIT_MASK(32);

static int omap2430_probe(struct platform_device *pdev)
{
	struct resource			musb_resources[3];
	struct musb_hdrc_platform_data	*pdata = dev_get_platdata(&pdev->dev);
	struct omap_musb_board_data	*data;
	struct platform_device		*musb;
	struct omap2430_glue		*glue;
	struct device_node		*np = pdev->dev.of_node;
	struct musb_hdrc_config		*config;
	struct device_node		*control_node;
	struct platform_device		*control_pdev;
	int				ret = -ENOMEM, val;

	if (!np)
		return -ENODEV;

	glue = devm_kzalloc(&pdev->dev, sizeof(*glue), GFP_KERNEL);
	if (!glue)
		goto err0;

	musb = platform_device_alloc("musb-hdrc", PLATFORM_DEVID_AUTO);
	if (!musb) {
		dev_err(&pdev->dev, "failed to allocate musb device\n");
		goto err0;
	}

	musb->dev.parent		= &pdev->dev;
	musb->dev.dma_mask		= &omap2430_dmamask;
	musb->dev.coherent_dma_mask	= omap2430_dmamask;

	glue->dev			= &pdev->dev;
	glue->musb			= musb;
	glue->status			= MUSB_UNKNOWN;
	glue->control_otghs = ERR_PTR(-ENODEV);

	pdata = devm_kzalloc(&pdev->dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		goto err2;

	data = devm_kzalloc(&pdev->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		goto err2;

	config = devm_kzalloc(&pdev->dev, sizeof(*config), GFP_KERNEL);
	if (!config)
		goto err2;

	of_property_read_u32(np, "mode", (u32 *)&pdata->mode);
	of_property_read_u32(np, "interface-type",
			(u32 *)&data->interface_type);
	of_property_read_u32(np, "num-eps", (u32 *)&config->num_eps);
	of_property_read_u32(np, "ram-bits", (u32 *)&config->ram_bits);
	of_property_read_u32(np, "power", (u32 *)&pdata->power);

	ret = of_property_read_u32(np, "multipoint", &val);
	if (!ret && val)
		config->multipoint = true;

	pdata->board_data	= data;
	pdata->config		= config;

	control_node = of_parse_phandle(np, "ctrl-module", 0);
	if (control_node) {
		control_pdev = of_find_device_by_node(control_node);
		if (!control_pdev) {
			dev_err(&pdev->dev, "Failed to get control device\n");
			ret = -EINVAL;
			goto err2;
		}
		glue->control_otghs = &control_pdev->dev;
	}

	pdata->platform_ops		= &omap2430_ops;

	platform_set_drvdata(pdev, glue);

	/*
	 * REVISIT if we ever have two instances of the wrapper, we will be
	 * in big trouble
	 */
	_glue	= glue;

	INIT_WORK(&glue->omap_musb_mailbox_work, omap_musb_mailbox_work);

	memset(musb_resources, 0x00, sizeof(*musb_resources) *
			ARRAY_SIZE(musb_resources));

	musb_resources[0].name = pdev->resource[0].name;
	musb_resources[0].start = pdev->resource[0].start;
	musb_resources[0].end = pdev->resource[0].end;
	musb_resources[0].flags = pdev->resource[0].flags;

	musb_resources[1].name = pdev->resource[1].name;
	musb_resources[1].start = pdev->resource[1].start;
	musb_resources[1].end = pdev->resource[1].end;
	musb_resources[1].flags = pdev->resource[1].flags;

	musb_resources[2].name = pdev->resource[2].name;
	musb_resources[2].start = pdev->resource[2].start;
	musb_resources[2].end = pdev->resource[2].end;
	musb_resources[2].flags = pdev->resource[2].flags;

	ret = platform_device_add_resources(musb, musb_resources,
			ARRAY_SIZE(musb_resources));
	if (ret) {
		dev_err(&pdev->dev, "failed to add resources\n");
		goto err2;
	}

	ret = platform_device_add_data(musb, pdata, sizeof(*pdata));
	if (ret) {
		dev_err(&pdev->dev, "failed to add platform_data\n");
		goto err2;
	}

	pm_runtime_enable(glue->dev);

	ret = platform_device_add(musb);
	if (ret) {
		dev_err(&pdev->dev, "failed to register musb device\n");
		goto err3;
	}

	return 0;

err3:
	pm_runtime_disable(glue->dev);

err2:
	platform_device_put(musb);

err0:
	return ret;
}

static int omap2430_remove(struct platform_device *pdev)
{
	struct omap2430_glue *glue = platform_get_drvdata(pdev);

	platform_device_unregister(glue->musb);
	pm_runtime_disable(glue->dev);

	return 0;
}

#ifdef CONFIG_PM

static int omap2430_runtime_suspend(struct device *dev)
{
	struct omap2430_glue		*glue = dev_get_drvdata(dev);
	struct musb			*musb = glue_to_musb(glue);

	if (!musb)
		return 0;

	musb->context.otg_interfsel = musb_readl(musb->mregs,
						 OTG_INTERFSEL);

	omap2430_low_level_exit(musb);

	return 0;
}

static int omap2430_runtime_resume(struct device *dev)
{
	struct omap2430_glue		*glue = dev_get_drvdata(dev);
	struct musb			*musb = glue_to_musb(glue);

	if (!musb)
		return 0;

	omap2430_low_level_init(musb);
	musb_writel(musb->mregs, OTG_INTERFSEL,
		    musb->context.otg_interfsel);

	return 0;
}

static const struct dev_pm_ops omap2430_pm_ops = {
	.runtime_suspend = omap2430_runtime_suspend,
	.runtime_resume = omap2430_runtime_resume,
};

#define DEV_PM_OPS	(&omap2430_pm_ops)
#else
#define DEV_PM_OPS	NULL
#endif

#ifdef CONFIG_OF
static const struct of_device_id omap2430_id_table[] = {
	{
		.compatible = "ti,omap4-musb"
	},
	{
		.compatible = "ti,omap3-musb"
	},
	{},
};
MODULE_DEVICE_TABLE(of, omap2430_id_table);
#endif

static struct platform_driver omap2430_driver = {
	.probe		= omap2430_probe,
	.remove		= omap2430_remove,
	.driver		= {
		.name	= "musb-omap2430",
		.pm	= DEV_PM_OPS,
		.of_match_table = of_match_ptr(omap2430_id_table),
	},
};

module_platform_driver(omap2430_driver);

MODULE_DESCRIPTION("OMAP2PLUS MUSB Glue Layer");
MODULE_AUTHOR("Felipe Balbi <balbi@ti.com>");
MODULE_LICENSE("GPL v2");
