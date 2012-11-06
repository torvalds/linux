/*
 * Copyright (C) 2005-2007 by Texas Instruments
 * Some code has been taken from tusb6010.c
 * Copyrights for that are attributable to:
 * Copyright (C) 2006 Nokia Corporation
 * Tony Lindgren <tony@atomide.com>
 *
 * This file is part of the Inventra Controller Driver for Linux.
 *
 * The Inventra Controller Driver for Linux is free software; you
 * can redistribute it and/or modify it under the terms of the GNU
 * General Public License version 2 as published by the Free Software
 * Foundation.
 *
 * The Inventra Controller Driver for Linux is distributed in
 * the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public
 * License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with The Inventra Controller Driver for Linux ; if not,
 * write to the Free Software Foundation, Inc., 59 Temple Place,
 * Suite 330, Boston, MA  02111-1307  USA
 *
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
#include <linux/usb/musb-omap.h>

#include "musb_core.h"
#include "omap2430.h"

struct omap2430_glue {
	struct device		*dev;
	struct platform_device	*musb;
	enum omap_musb_vbus_id_status status;
	struct work_struct	omap_musb_mailbox_work;
	u32 __iomem		*control_otghs;
};
#define glue_to_musb(g)		platform_get_drvdata(g->musb)

struct omap2430_glue		*_glue;

static struct timer_list musb_idle_timer;

/**
 * omap4_usb_phy_mailbox - write to usb otg mailbox
 * @glue: struct omap2430_glue *
 * @val: the value to be written to the mailbox
 *
 * On detection of a device (ID pin is grounded), this API should be called
 * to set AVALID, VBUSVALID and ID pin is grounded.
 *
 * When OMAP is connected to a host (OMAP in device mode), this API
 * is called to set AVALID, VBUSVALID and ID pin in high impedance.
 *
 * XXX: This function will be removed once we have a seperate driver for
 * control module
 */
static void omap4_usb_phy_mailbox(struct omap2430_glue *glue, u32 val)
{
	if (glue->control_otghs)
		writel(val, glue->control_otghs);
}

static void musb_do_idle(unsigned long _musb)
{
	struct musb	*musb = (void *)_musb;
	unsigned long	flags;
	u8	power;
	u8	devctl;

	spin_lock_irqsave(&musb->lock, flags);

	switch (musb->xceiv->state) {
	case OTG_STATE_A_WAIT_BCON:

		devctl = musb_readb(musb->mregs, MUSB_DEVCTL);
		if (devctl & MUSB_DEVCTL_BDEVICE) {
			musb->xceiv->state = OTG_STATE_B_IDLE;
			MUSB_DEV_MODE(musb);
		} else {
			musb->xceiv->state = OTG_STATE_A_IDLE;
			MUSB_HST_MODE(musb);
		}
		break;
	case OTG_STATE_A_SUSPEND:
		/* finish RESUME signaling? */
		if (musb->port1_status & MUSB_PORT_STAT_RESUME) {
			power = musb_readb(musb->mregs, MUSB_POWER);
			power &= ~MUSB_POWER_RESUME;
			dev_dbg(musb->controller, "root port resume stopped, power %02x\n", power);
			musb_writeb(musb->mregs, MUSB_POWER, power);
			musb->is_active = 1;
			musb->port1_status &= ~(USB_PORT_STAT_SUSPEND
						| MUSB_PORT_STAT_RESUME);
			musb->port1_status |= USB_PORT_STAT_C_SUSPEND << 16;
			usb_hcd_poll_rh_status(musb_to_hcd(musb));
			/* NOTE: it might really be A_WAIT_BCON ... */
			musb->xceiv->state = OTG_STATE_A_HOST;
		}
		break;
	case OTG_STATE_A_HOST:
		devctl = musb_readb(musb->mregs, MUSB_DEVCTL);
		if (devctl &  MUSB_DEVCTL_BDEVICE)
			musb->xceiv->state = OTG_STATE_B_IDLE;
		else
			musb->xceiv->state = OTG_STATE_A_WAIT_BCON;
	default:
		break;
	}
	spin_unlock_irqrestore(&musb->lock, flags);
}


static void omap2430_musb_try_idle(struct musb *musb, unsigned long timeout)
{
	unsigned long		default_timeout = jiffies + msecs_to_jiffies(3);
	static unsigned long	last_timer;

	if (timeout == 0)
		timeout = default_timeout;

	/* Never idle if active, or when VBUS timeout is not set as host */
	if (musb->is_active || ((musb->a_wait_bcon == 0)
			&& (musb->xceiv->state == OTG_STATE_A_WAIT_BCON))) {
		dev_dbg(musb->controller, "%s active, deleting timer\n",
			otg_state_string(musb->xceiv->state));
		del_timer(&musb_idle_timer);
		last_timer = jiffies;
		return;
	}

	if (time_after(last_timer, timeout)) {
		if (!timer_pending(&musb_idle_timer))
			last_timer = timeout;
		else {
			dev_dbg(musb->controller, "Longer idle timer already pending, ignoring\n");
			return;
		}
	}
	last_timer = timeout;

	dev_dbg(musb->controller, "%s inactive, for idle timer for %lu ms\n",
		otg_state_string(musb->xceiv->state),
		(unsigned long)jiffies_to_msecs(timeout - jiffies));
	mod_timer(&musb_idle_timer, timeout);
}

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
		if (musb->xceiv->state == OTG_STATE_A_IDLE) {
			int loops = 100;
			/* start the session */
			devctl |= MUSB_DEVCTL_SESSION;
			musb_writeb(musb->mregs, MUSB_DEVCTL, devctl);
			/*
			 * Wait for the musb to set as A device to enable the
			 * VBUS
			 */
			while (musb_readb(musb->mregs, MUSB_DEVCTL) & 0x80) {

				mdelay(5);
				cpu_relax();

				if (time_after(jiffies, timeout)
				    || loops-- <= 0) {
					dev_err(musb->controller,
					"configured as A device timeout");
					break;
				}
			}

			if (otg->set_vbus)
				otg_set_vbus(otg, 1);
		} else {
			musb->is_active = 1;
			otg->default_a = 1;
			musb->xceiv->state = OTG_STATE_A_WAIT_VRISE;
			devctl |= MUSB_DEVCTL_SESSION;
			MUSB_HST_MODE(musb);
		}
	} else {
		musb->is_active = 0;

		/* NOTE:  we're skipping A_WAIT_VFALL -> A_IDLE and
		 * jumping right to B_IDLE...
		 */

		otg->default_a = 0;
		musb->xceiv->state = OTG_STATE_B_IDLE;
		devctl &= ~MUSB_DEVCTL_SESSION;

		MUSB_DEV_MODE(musb);
	}
	musb_writeb(musb->mregs, MUSB_DEVCTL, devctl);

	dev_dbg(musb->controller, "VBUS %s, devctl %02x "
		/* otg %3x conf %08x prcm %08x */ "\n",
		otg_state_string(musb->xceiv->state),
		musb_readb(musb->mregs, MUSB_DEVCTL));
}

static int omap2430_musb_set_mode(struct musb *musb, u8 musb_mode)
{
	u8	devctl = musb_readb(musb->mregs, MUSB_DEVCTL);

	devctl |= MUSB_DEVCTL_SESSION;
	musb_writeb(musb->mregs, MUSB_DEVCTL, devctl);

	return 0;
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

void omap_musb_mailbox(enum omap_musb_vbus_id_status status)
{
	struct omap2430_glue	*glue = _glue;
	struct musb		*musb = glue_to_musb(glue);

	glue->status = status;
	if (!musb) {
		dev_err(glue->dev, "musb core is not yet ready\n");
		return;
	}

	schedule_work(&glue->omap_musb_mailbox_work);
}
EXPORT_SYMBOL_GPL(omap_musb_mailbox);

static void omap_musb_set_mailbox(struct omap2430_glue *glue)
{
	u32 val;
	struct musb *musb = glue_to_musb(glue);
	struct device *dev = musb->controller;
	struct musb_hdrc_platform_data *pdata = dev->platform_data;
	struct omap_musb_board_data *data = pdata->board_data;
	struct usb_otg *otg = musb->xceiv->otg;

	switch (glue->status) {
	case OMAP_MUSB_ID_GROUND:
		dev_dbg(dev, "ID GND\n");

		otg->default_a = true;
		musb->xceiv->state = OTG_STATE_A_IDLE;
		musb->xceiv->last_event = USB_EVENT_ID;
		if (musb->gadget_driver) {
			pm_runtime_get_sync(dev);
			val = AVALID | VBUSVALID;
			omap4_usb_phy_mailbox(glue, val);
			omap2430_musb_set_vbus(musb, 1);
		}
		break;

	case OMAP_MUSB_VBUS_VALID:
		dev_dbg(dev, "VBUS Connect\n");

		otg->default_a = false;
		musb->xceiv->state = OTG_STATE_B_IDLE;
		musb->xceiv->last_event = USB_EVENT_VBUS;
		if (musb->gadget_driver)
			pm_runtime_get_sync(dev);
		val = IDDIG | AVALID | VBUSVALID;
		omap4_usb_phy_mailbox(glue, val);
		break;

	case OMAP_MUSB_ID_FLOAT:
	case OMAP_MUSB_VBUS_OFF:
		dev_dbg(dev, "VBUS Disconnect\n");

		musb->xceiv->last_event = USB_EVENT_NONE;
		if (musb->gadget_driver) {
			pm_runtime_mark_last_busy(dev);
			pm_runtime_put_autosuspend(dev);
		}

		if (data->interface_type == MUSB_INTERFACE_UTMI) {
			if (musb->xceiv->otg->set_vbus)
				otg_set_vbus(musb->xceiv->otg, 0);
		}
		val = SESSEND | IDDIG;
		omap4_usb_phy_mailbox(glue, val);
		break;
	default:
		dev_dbg(dev, "ID float\n");
	}
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
	struct musb_hdrc_platform_data *plat = dev->platform_data;
	struct omap_musb_board_data *data = plat->board_data;

	/* We require some kind of external transceiver, hooked
	 * up through ULPI.  TWL4030-family PMICs include one,
	 * which needs a driver, drivers aren't always needed.
	 */
	musb->xceiv = devm_usb_get_phy(dev, USB_PHY_TYPE_USB2);
	if (IS_ERR_OR_NULL(musb->xceiv)) {
		pr_err("HS USB OTG: no transceiver configured\n");
		return -ENODEV;
	}

	musb->isr = omap2430_musb_interrupt;

	status = pm_runtime_get_sync(dev);
	if (status < 0) {
		dev_err(dev, "pm_runtime_get_sync FAILED %d\n", status);
		goto err1;
	}

	l = musb_readl(musb->mregs, OTG_INTERFSEL);

	if (data->interface_type == MUSB_INTERFACE_UTMI) {
		/* OMAP4 uses Internal PHY GS70 which uses UTMI interface */
		l &= ~ULPI_12PIN;       /* Disable ULPI */
		l |= UTMI_8BIT;         /* Enable UTMI  */
	} else {
		l |= ULPI_12PIN;
	}

	musb_writel(musb->mregs, OTG_INTERFSEL, l);

	pr_debug("HS USB OTG: revision 0x%x, sysconfig 0x%02x, "
			"sysstatus 0x%x, intrfsel 0x%x, simenable  0x%x\n",
			musb_readl(musb->mregs, OTG_REVISION),
			musb_readl(musb->mregs, OTG_SYSCONFIG),
			musb_readl(musb->mregs, OTG_SYSSTATUS),
			musb_readl(musb->mregs, OTG_INTERFSEL),
			musb_readl(musb->mregs, OTG_SIMENABLE));

	setup_timer(&musb_idle_timer, musb_do_idle, (unsigned long) musb);

	if (glue->status != OMAP_MUSB_UNKNOWN)
		omap_musb_set_mailbox(glue);

	pm_runtime_put_noidle(musb->controller);
	return 0;

err1:
	return status;
}

static void omap2430_musb_enable(struct musb *musb)
{
	u8		devctl;
	u32		val;
	unsigned long timeout = jiffies + msecs_to_jiffies(1000);
	struct device *dev = musb->controller;
	struct omap2430_glue *glue = dev_get_drvdata(dev->parent);
	struct musb_hdrc_platform_data *pdata = dev->platform_data;
	struct omap_musb_board_data *data = pdata->board_data;

	switch (glue->status) {

	case OMAP_MUSB_ID_GROUND:
		val = AVALID | VBUSVALID;
		omap4_usb_phy_mailbox(glue, val);
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

	case OMAP_MUSB_VBUS_VALID:
		val = IDDIG | AVALID | VBUSVALID;
		omap4_usb_phy_mailbox(glue, val);
		break;

	default:
		break;
	}
}

static void omap2430_musb_disable(struct musb *musb)
{
	u32 val;
	struct device *dev = musb->controller;
	struct omap2430_glue *glue = dev_get_drvdata(dev->parent);

	if (glue->status != OMAP_MUSB_UNKNOWN) {
		val = SESSEND | IDDIG;
		omap4_usb_phy_mailbox(glue, val);
	}
}

static int omap2430_musb_exit(struct musb *musb)
{
	del_timer_sync(&musb_idle_timer);

	omap2430_low_level_exit(musb);

	return 0;
}

static const struct musb_platform_ops omap2430_ops = {
	.init		= omap2430_musb_init,
	.exit		= omap2430_musb_exit,

	.set_mode	= omap2430_musb_set_mode,
	.try_idle	= omap2430_musb_try_idle,

	.set_vbus	= omap2430_musb_set_vbus,

	.enable		= omap2430_musb_enable,
	.disable	= omap2430_musb_disable,
};

static u64 omap2430_dmamask = DMA_BIT_MASK(32);

static int __devinit omap2430_probe(struct platform_device *pdev)
{
	struct musb_hdrc_platform_data	*pdata = pdev->dev.platform_data;
	struct omap_musb_board_data	*data;
	struct platform_device		*musb;
	struct omap2430_glue		*glue;
	struct device_node		*np = pdev->dev.of_node;
	struct musb_hdrc_config		*config;
	struct resource			*res;
	int				ret = -ENOMEM;

	glue = devm_kzalloc(&pdev->dev, sizeof(*glue), GFP_KERNEL);
	if (!glue) {
		dev_err(&pdev->dev, "failed to allocate glue context\n");
		goto err0;
	}

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
	glue->status			= OMAP_MUSB_UNKNOWN;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 1);

	glue->control_otghs = devm_request_and_ioremap(&pdev->dev, res);
	if (glue->control_otghs == NULL)
		dev_dbg(&pdev->dev, "Failed to obtain control memory\n");

	if (np) {
		pdata = devm_kzalloc(&pdev->dev, sizeof(*pdata), GFP_KERNEL);
		if (!pdata) {
			dev_err(&pdev->dev,
				"failed to allocate musb platfrom data\n");
			ret = -ENOMEM;
			goto err2;
		}

		data = devm_kzalloc(&pdev->dev, sizeof(*data), GFP_KERNEL);
		if (!data) {
			dev_err(&pdev->dev,
					"failed to allocate musb board data\n");
			ret = -ENOMEM;
			goto err2;
		}

		config = devm_kzalloc(&pdev->dev, sizeof(*config), GFP_KERNEL);
		if (!data) {
			dev_err(&pdev->dev,
				"failed to allocate musb hdrc config\n");
			goto err2;
		}

		of_property_read_u32(np, "mode", (u32 *)&pdata->mode);
		of_property_read_u32(np, "interface_type",
						(u32 *)&data->interface_type);
		of_property_read_u32(np, "num_eps", (u32 *)&config->num_eps);
		of_property_read_u32(np, "ram_bits", (u32 *)&config->ram_bits);
		of_property_read_u32(np, "power", (u32 *)&pdata->power);
		config->multipoint = of_property_read_bool(np, "multipoint");

		pdata->board_data	= data;
		pdata->config		= config;
	}

	pdata->platform_ops		= &omap2430_ops;

	platform_set_drvdata(pdev, glue);

	/*
	 * REVISIT if we ever have two instances of the wrapper, we will be
	 * in big trouble
	 */
	_glue	= glue;

	INIT_WORK(&glue->omap_musb_mailbox_work, omap_musb_mailbox_work);

	ret = platform_device_add_resources(musb, pdev->resource,
			pdev->num_resources);
	if (ret) {
		dev_err(&pdev->dev, "failed to add resources\n");
		goto err2;
	}

	ret = platform_device_add_data(musb, pdata, sizeof(*pdata));
	if (ret) {
		dev_err(&pdev->dev, "failed to add platform_data\n");
		goto err2;
	}

	pm_runtime_enable(&pdev->dev);

	ret = platform_device_add(musb);
	if (ret) {
		dev_err(&pdev->dev, "failed to register musb device\n");
		goto err2;
	}

	return 0;

err2:
	platform_device_put(musb);

err0:
	return ret;
}

static int __devexit omap2430_remove(struct platform_device *pdev)
{
	struct omap2430_glue		*glue = platform_get_drvdata(pdev);

	cancel_work_sync(&glue->omap_musb_mailbox_work);
	platform_device_unregister(glue->musb);

	return 0;
}

#ifdef CONFIG_PM

static int omap2430_runtime_suspend(struct device *dev)
{
	struct omap2430_glue		*glue = dev_get_drvdata(dev);
	struct musb			*musb = glue_to_musb(glue);

	if (musb) {
		musb->context.otg_interfsel = musb_readl(musb->mregs,
				OTG_INTERFSEL);

		omap2430_low_level_exit(musb);
		usb_phy_set_suspend(musb->xceiv, 1);
	}

	return 0;
}

static int omap2430_runtime_resume(struct device *dev)
{
	struct omap2430_glue		*glue = dev_get_drvdata(dev);
	struct musb			*musb = glue_to_musb(glue);

	if (musb) {
		omap2430_low_level_init(musb);
		musb_writel(musb->mregs, OTG_INTERFSEL,
				musb->context.otg_interfsel);

		usb_phy_set_suspend(musb->xceiv, 0);
	}

	return 0;
}

static struct dev_pm_ops omap2430_pm_ops = {
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
	.remove		= __devexit_p(omap2430_remove),
	.driver		= {
		.name	= "musb-omap2430",
		.pm	= DEV_PM_OPS,
		.of_match_table = of_match_ptr(omap2430_id_table),
	},
};

MODULE_DESCRIPTION("OMAP2PLUS MUSB Glue Layer");
MODULE_AUTHOR("Felipe Balbi <balbi@ti.com>");
MODULE_LICENSE("GPL v2");

static int __init omap2430_init(void)
{
	return platform_driver_register(&omap2430_driver);
}
subsys_initcall(omap2430_init);

static void __exit omap2430_exit(void)
{
	platform_driver_unregister(&omap2430_driver);
}
module_exit(omap2430_exit);
