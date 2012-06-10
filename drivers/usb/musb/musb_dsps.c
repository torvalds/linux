/*
 * Texas Instruments DSPS platforms "glue layer"
 *
 * Copyright (C) 2012, by Texas Instruments
 *
 * Based on the am35x "glue layer" code.
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
 * musb_dsps.c will be a common file for all the TI DSPS platforms
 * such as dm64x, dm36x, dm35x, da8x, am35x and ti81x.
 * For now only ti81x is using this and in future davinci.c, am35x.c
 * da8xx.c would be merged to this file after testing.
 */

#include <linux/init.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/pm_runtime.h>
#include <linux/module.h>

#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_address.h>

#include <plat/usb.h>

#include "musb_core.h"

/**
 * avoid using musb_readx()/musb_writex() as glue layer should not be
 * dependent on musb core layer symbols.
 */
static inline u8 dsps_readb(const void __iomem *addr, unsigned offset)
	{ return __raw_readb(addr + offset); }

static inline u32 dsps_readl(const void __iomem *addr, unsigned offset)
	{ return __raw_readl(addr + offset); }

static inline void dsps_writeb(void __iomem *addr, unsigned offset, u8 data)
	{ __raw_writeb(data, addr + offset); }

static inline void dsps_writel(void __iomem *addr, unsigned offset, u32 data)
	{ __raw_writel(data, addr + offset); }

/**
 * DSPS musb wrapper register offset.
 * FIXME: This should be expanded to have all the wrapper registers from TI DSPS
 * musb ips.
 */
struct dsps_musb_wrapper {
	u16	revision;
	u16	control;
	u16	status;
	u16	eoi;
	u16	epintr_set;
	u16	epintr_clear;
	u16	epintr_status;
	u16	coreintr_set;
	u16	coreintr_clear;
	u16	coreintr_status;
	u16	phy_utmi;
	u16	mode;

	/* bit positions for control */
	unsigned	reset:5;

	/* bit positions for interrupt */
	unsigned	usb_shift:5;
	u32		usb_mask;
	u32		usb_bitmap;
	unsigned	drvvbus:5;

	unsigned	txep_shift:5;
	u32		txep_mask;
	u32		txep_bitmap;

	unsigned	rxep_shift:5;
	u32		rxep_mask;
	u32		rxep_bitmap;

	/* bit positions for phy_utmi */
	unsigned	otg_disable:5;

	/* bit positions for mode */
	unsigned	iddig:5;
	/* miscellaneous stuff */
	u32		musb_core_offset;
	u8		poll_seconds;
};

/**
 * DSPS glue structure.
 */
struct dsps_glue {
	struct device *dev;
	struct platform_device *musb;	/* child musb pdev */
	const struct dsps_musb_wrapper *wrp; /* wrapper register offsets */
	struct timer_list timer;	/* otg_workaround timer */
};

/**
 * dsps_musb_enable - enable interrupts
 */
static void dsps_musb_enable(struct musb *musb)
{
	struct device *dev = musb->controller;
	struct platform_device *pdev = to_platform_device(dev->parent);
	struct dsps_glue *glue = platform_get_drvdata(pdev);
	const struct dsps_musb_wrapper *wrp = glue->wrp;
	void __iomem *reg_base = musb->ctrl_base;
	u32 epmask, coremask;

	/* Workaround: setup IRQs through both register sets. */
	epmask = ((musb->epmask & wrp->txep_mask) << wrp->txep_shift) |
	       ((musb->epmask & wrp->rxep_mask) << wrp->rxep_shift);
	coremask = (wrp->usb_bitmap & ~MUSB_INTR_SOF);

	dsps_writel(reg_base, wrp->epintr_set, epmask);
	dsps_writel(reg_base, wrp->coreintr_set, coremask);
	/* Force the DRVVBUS IRQ so we can start polling for ID change. */
	if (is_otg_enabled(musb))
		dsps_writel(reg_base, wrp->coreintr_set,
			    (1 << wrp->drvvbus) << wrp->usb_shift);
}

/**
 * dsps_musb_disable - disable HDRC and flush interrupts
 */
static void dsps_musb_disable(struct musb *musb)
{
	struct device *dev = musb->controller;
	struct platform_device *pdev = to_platform_device(dev->parent);
	struct dsps_glue *glue = platform_get_drvdata(pdev);
	const struct dsps_musb_wrapper *wrp = glue->wrp;
	void __iomem *reg_base = musb->ctrl_base;

	dsps_writel(reg_base, wrp->coreintr_clear, wrp->usb_bitmap);
	dsps_writel(reg_base, wrp->epintr_clear,
			 wrp->txep_bitmap | wrp->rxep_bitmap);
	dsps_writeb(musb->mregs, MUSB_DEVCTL, 0);
	dsps_writel(reg_base, wrp->eoi, 0);
}

static void otg_timer(unsigned long _musb)
{
	struct musb *musb = (void *)_musb;
	void __iomem *mregs = musb->mregs;
	struct device *dev = musb->controller;
	struct platform_device *pdev = to_platform_device(dev->parent);
	struct dsps_glue *glue = platform_get_drvdata(pdev);
	const struct dsps_musb_wrapper *wrp = glue->wrp;
	u8 devctl;
	unsigned long flags;

	/*
	 * We poll because DSPS IP's won't expose several OTG-critical
	 * status change events (from the transceiver) otherwise.
	 */
	devctl = dsps_readb(mregs, MUSB_DEVCTL);
	dev_dbg(musb->controller, "Poll devctl %02x (%s)\n", devctl,
				otg_state_string(musb->xceiv->state));

	spin_lock_irqsave(&musb->lock, flags);
	switch (musb->xceiv->state) {
	case OTG_STATE_A_WAIT_BCON:
		devctl &= ~MUSB_DEVCTL_SESSION;
		dsps_writeb(musb->mregs, MUSB_DEVCTL, devctl);

		devctl = dsps_readb(musb->mregs, MUSB_DEVCTL);
		if (devctl & MUSB_DEVCTL_BDEVICE) {
			musb->xceiv->state = OTG_STATE_B_IDLE;
			MUSB_DEV_MODE(musb);
		} else {
			musb->xceiv->state = OTG_STATE_A_IDLE;
			MUSB_HST_MODE(musb);
		}
		break;
	case OTG_STATE_A_WAIT_VFALL:
		musb->xceiv->state = OTG_STATE_A_WAIT_VRISE;
		dsps_writel(musb->ctrl_base, wrp->coreintr_set,
			    MUSB_INTR_VBUSERROR << wrp->usb_shift);
		break;
	case OTG_STATE_B_IDLE:
		if (!is_peripheral_enabled(musb))
			break;

		devctl = dsps_readb(mregs, MUSB_DEVCTL);
		if (devctl & MUSB_DEVCTL_BDEVICE)
			mod_timer(&glue->timer,
					jiffies + wrp->poll_seconds * HZ);
		else
			musb->xceiv->state = OTG_STATE_A_IDLE;
		break;
	default:
		break;
	}
	spin_unlock_irqrestore(&musb->lock, flags);
}

static void dsps_musb_try_idle(struct musb *musb, unsigned long timeout)
{
	struct device *dev = musb->controller;
	struct platform_device *pdev = to_platform_device(dev->parent);
	struct dsps_glue *glue = platform_get_drvdata(pdev);
	static unsigned long last_timer;

	if (!is_otg_enabled(musb))
		return;

	if (timeout == 0)
		timeout = jiffies + msecs_to_jiffies(3);

	/* Never idle if active, or when VBUS timeout is not set as host */
	if (musb->is_active || (musb->a_wait_bcon == 0 &&
				musb->xceiv->state == OTG_STATE_A_WAIT_BCON)) {
		dev_dbg(musb->controller, "%s active, deleting timer\n",
				otg_state_string(musb->xceiv->state));
		del_timer(&glue->timer);
		last_timer = jiffies;
		return;
	}

	if (time_after(last_timer, timeout) && timer_pending(&glue->timer)) {
		dev_dbg(musb->controller,
			"Longer idle timer already pending, ignoring...\n");
		return;
	}
	last_timer = timeout;

	dev_dbg(musb->controller, "%s inactive, starting idle timer for %u ms\n",
		otg_state_string(musb->xceiv->state),
			jiffies_to_msecs(timeout - jiffies));
	mod_timer(&glue->timer, timeout);
}

static irqreturn_t dsps_interrupt(int irq, void *hci)
{
	struct musb  *musb = hci;
	void __iomem *reg_base = musb->ctrl_base;
	struct device *dev = musb->controller;
	struct platform_device *pdev = to_platform_device(dev->parent);
	struct dsps_glue *glue = platform_get_drvdata(pdev);
	const struct dsps_musb_wrapper *wrp = glue->wrp;
	unsigned long flags;
	irqreturn_t ret = IRQ_NONE;
	u32 epintr, usbintr;

	spin_lock_irqsave(&musb->lock, flags);

	/* Get endpoint interrupts */
	epintr = dsps_readl(reg_base, wrp->epintr_status);
	musb->int_rx = (epintr & wrp->rxep_bitmap) >> wrp->rxep_shift;
	musb->int_tx = (epintr & wrp->txep_bitmap) >> wrp->txep_shift;

	if (epintr)
		dsps_writel(reg_base, wrp->epintr_status, epintr);

	/* Get usb core interrupts */
	usbintr = dsps_readl(reg_base, wrp->coreintr_status);
	if (!usbintr && !epintr)
		goto eoi;

	musb->int_usb =	(usbintr & wrp->usb_bitmap) >> wrp->usb_shift;
	if (usbintr)
		dsps_writel(reg_base, wrp->coreintr_status, usbintr);

	dev_dbg(musb->controller, "usbintr (%x) epintr(%x)\n",
			usbintr, epintr);
	/*
	 * DRVVBUS IRQs are the only proxy we have (a very poor one!) for
	 * DSPS IP's missing ID change IRQ.  We need an ID change IRQ to
	 * switch appropriately between halves of the OTG state machine.
	 * Managing DEVCTL.SESSION per Mentor docs requires that we know its
	 * value but DEVCTL.BDEVICE is invalid without DEVCTL.SESSION set.
	 * Also, DRVVBUS pulses for SRP (but not at 5V) ...
	 */
	if ((usbintr & MUSB_INTR_BABBLE) && is_host_enabled(musb))
		pr_info("CAUTION: musb: Babble Interrupt Occured\n");

	if (usbintr & ((1 << wrp->drvvbus) << wrp->usb_shift)) {
		int drvvbus = dsps_readl(reg_base, wrp->status);
		void __iomem *mregs = musb->mregs;
		u8 devctl = dsps_readb(mregs, MUSB_DEVCTL);
		int err;

		err = is_host_enabled(musb) && (musb->int_usb &
						MUSB_INTR_VBUSERROR);
		if (err) {
			/*
			 * The Mentor core doesn't debounce VBUS as needed
			 * to cope with device connect current spikes. This
			 * means it's not uncommon for bus-powered devices
			 * to get VBUS errors during enumeration.
			 *
			 * This is a workaround, but newer RTL from Mentor
			 * seems to allow a better one: "re"-starting sessions
			 * without waiting for VBUS to stop registering in
			 * devctl.
			 */
			musb->int_usb &= ~MUSB_INTR_VBUSERROR;
			musb->xceiv->state = OTG_STATE_A_WAIT_VFALL;
			mod_timer(&glue->timer,
					jiffies + wrp->poll_seconds * HZ);
			WARNING("VBUS error workaround (delay coming)\n");
		} else if (is_host_enabled(musb) && drvvbus) {
			musb->is_active = 1;
			MUSB_HST_MODE(musb);
			musb->xceiv->otg->default_a = 1;
			musb->xceiv->state = OTG_STATE_A_WAIT_VRISE;
			del_timer(&glue->timer);
		} else {
			musb->is_active = 0;
			MUSB_DEV_MODE(musb);
			musb->xceiv->otg->default_a = 0;
			musb->xceiv->state = OTG_STATE_B_IDLE;
		}

		/* NOTE: this must complete power-on within 100 ms. */
		dev_dbg(musb->controller, "VBUS %s (%s)%s, devctl %02x\n",
				drvvbus ? "on" : "off",
				otg_state_string(musb->xceiv->state),
				err ? " ERROR" : "",
				devctl);
		ret = IRQ_HANDLED;
	}

	if (musb->int_tx || musb->int_rx || musb->int_usb)
		ret |= musb_interrupt(musb);

 eoi:
	/* EOI needs to be written for the IRQ to be re-asserted. */
	if (ret == IRQ_HANDLED || epintr || usbintr)
		dsps_writel(reg_base, wrp->eoi, 1);

	/* Poll for ID change */
	if (is_otg_enabled(musb) && musb->xceiv->state == OTG_STATE_B_IDLE)
		mod_timer(&glue->timer, jiffies + wrp->poll_seconds * HZ);

	spin_unlock_irqrestore(&musb->lock, flags);

	return ret;
}

static int dsps_musb_init(struct musb *musb)
{
	struct device *dev = musb->controller;
	struct musb_hdrc_platform_data *plat = dev->platform_data;
	struct platform_device *pdev = to_platform_device(dev->parent);
	struct dsps_glue *glue = platform_get_drvdata(pdev);
	const struct dsps_musb_wrapper *wrp = glue->wrp;
	struct omap_musb_board_data *data = plat->board_data;
	void __iomem *reg_base = musb->ctrl_base;
	u32 rev, val;
	int status;

	/* mentor core register starts at offset of 0x400 from musb base */
	musb->mregs += wrp->musb_core_offset;

	/* NOP driver needs change if supporting dual instance */
	usb_nop_xceiv_register();
	musb->xceiv = usb_get_transceiver();
	if (!musb->xceiv)
		return -ENODEV;

	/* Returns zero if e.g. not clocked */
	rev = dsps_readl(reg_base, wrp->revision);
	if (!rev) {
		status = -ENODEV;
		goto err0;
	}

	if (is_host_enabled(musb))
		setup_timer(&glue->timer, otg_timer, (unsigned long) musb);

	/* Reset the musb */
	dsps_writel(reg_base, wrp->control, (1 << wrp->reset));

	/* Start the on-chip PHY and its PLL. */
	if (data->set_phy_power)
		data->set_phy_power(1);

	musb->isr = dsps_interrupt;

	/* reset the otgdisable bit, needed for host mode to work */
	val = dsps_readl(reg_base, wrp->phy_utmi);
	val &= ~(1 << wrp->otg_disable);
	dsps_writel(musb->ctrl_base, wrp->phy_utmi, val);

	/* clear level interrupt */
	dsps_writel(reg_base, wrp->eoi, 0);

	return 0;
err0:
	usb_put_transceiver(musb->xceiv);
	usb_nop_xceiv_unregister();
	return status;
}

static int dsps_musb_exit(struct musb *musb)
{
	struct device *dev = musb->controller;
	struct musb_hdrc_platform_data *plat = dev->platform_data;
	struct omap_musb_board_data *data = plat->board_data;
	struct platform_device *pdev = to_platform_device(dev->parent);
	struct dsps_glue *glue = platform_get_drvdata(pdev);

	if (is_host_enabled(musb))
		del_timer_sync(&glue->timer);

	/* Shutdown the on-chip PHY and its PLL. */
	if (data->set_phy_power)
		data->set_phy_power(0);

	/* NOP driver needs change if supporting dual instance */
	usb_put_transceiver(musb->xceiv);
	usb_nop_xceiv_unregister();

	return 0;
}

static struct musb_platform_ops dsps_ops = {
	.init		= dsps_musb_init,
	.exit		= dsps_musb_exit,

	.enable		= dsps_musb_enable,
	.disable	= dsps_musb_disable,

	.try_idle	= dsps_musb_try_idle,
};

static u64 musb_dmamask = DMA_BIT_MASK(32);

static int __devinit dsps_create_musb_pdev(struct dsps_glue *glue, u8 id)
{
	struct device *dev = glue->dev;
	struct platform_device *pdev = to_platform_device(dev);
	struct musb_hdrc_platform_data  *pdata = dev->platform_data;
	struct platform_device	*musb;
	struct resource *res;
	struct resource	resources[2];
	char res_name[10];
	int ret;

	/* get memory resource */
	sprintf(res_name, "musb%d", id);
	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, res_name);
	if (!res) {
		dev_err(dev, "%s get mem resource failed\n", res_name);
		ret = -ENODEV;
		goto err0;
	}
	res->parent = NULL;
	resources[0] = *res;

	/* get irq resource */
	sprintf(res_name, "musb%d-irq", id);
	res = platform_get_resource_byname(pdev, IORESOURCE_IRQ, res_name);
	if (!res) {
		dev_err(dev, "%s get irq resource failed\n", res_name);
		ret = -ENODEV;
		goto err0;
	}
	strcpy((u8 *)res->name, "mc");
	res->parent = NULL;
	resources[1] = *res;

	/* allocate the child platform device */
	musb = platform_device_alloc("musb-hdrc", -1);
	if (!musb) {
		dev_err(dev, "failed to allocate musb device\n");
		ret = -ENOMEM;
		goto err0;
	}

	musb->dev.parent		= dev;
	musb->dev.dma_mask		= &musb_dmamask;
	musb->dev.coherent_dma_mask	= musb_dmamask;

	glue->musb			= musb;

	pdata->platform_ops		= &dsps_ops;

	ret = platform_device_add_resources(musb, resources, 2);
	if (ret) {
		dev_err(dev, "failed to add resources\n");
		goto err1;
	}

	ret = platform_device_add_data(musb, pdata, sizeof(*pdata));
	if (ret) {
		dev_err(dev, "failed to add platform_data\n");
		goto err1;
	}

	ret = platform_device_add(musb);
	if (ret) {
		dev_err(dev, "failed to register musb device\n");
		goto err1;
	}

	return 0;

err1:
	platform_device_put(musb);
err0:
	return ret;
}

static void __devexit dsps_delete_musb_pdev(struct dsps_glue *glue)
{
	platform_device_del(glue->musb);
	platform_device_put(glue->musb);
}

static int __devinit dsps_probe(struct platform_device *pdev)
{
	const struct platform_device_id *id = platform_get_device_id(pdev);
	const struct dsps_musb_wrapper *wrp =
				(struct dsps_musb_wrapper *)id->driver_data;
	struct dsps_glue *glue;
	struct resource *iomem;
	int ret;

	/* allocate glue */
	glue = kzalloc(sizeof(*glue), GFP_KERNEL);
	if (!glue) {
		dev_err(&pdev->dev, "unable to allocate glue memory\n");
		ret = -ENOMEM;
		goto err0;
	}

	/* get memory resource */
	iomem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!iomem) {
		dev_err(&pdev->dev, "failed to get usbss mem resourse\n");
		ret = -ENODEV;
		goto err1;
	}

	glue->dev = &pdev->dev;

	glue->wrp = kmemdup(wrp, sizeof(*wrp), GFP_KERNEL);
	if (!glue->wrp) {
		dev_err(&pdev->dev, "failed to duplicate wrapper struct memory\n");
		ret = -ENOMEM;
		goto err1;
	}
	platform_set_drvdata(pdev, glue);

	/* create the child platform device for first instances of musb */
	ret = dsps_create_musb_pdev(glue, 0);
	if (ret != 0) {
		dev_err(&pdev->dev, "failed to create child pdev\n");
		goto err2;
	}

	/* enable the usbss clocks */
	pm_runtime_enable(&pdev->dev);

	ret = pm_runtime_get_sync(&pdev->dev);
	if (ret < 0) {
		dev_err(&pdev->dev, "pm_runtime_get_sync FAILED");
		goto err3;
	}

	return 0;

err3:
	pm_runtime_disable(&pdev->dev);
err2:
	kfree(glue->wrp);
err1:
	kfree(glue);
err0:
	return ret;
}
static int __devexit dsps_remove(struct platform_device *pdev)
{
	struct dsps_glue *glue = platform_get_drvdata(pdev);

	/* delete the child platform device */
	dsps_delete_musb_pdev(glue);

	/* disable usbss clocks */
	pm_runtime_put(&pdev->dev);
	pm_runtime_disable(&pdev->dev);
	kfree(glue->wrp);
	kfree(glue);
	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int dsps_suspend(struct device *dev)
{
	struct musb_hdrc_platform_data *plat = dev->platform_data;
	struct omap_musb_board_data *data = plat->board_data;

	/* Shutdown the on-chip PHY and its PLL. */
	if (data->set_phy_power)
		data->set_phy_power(0);

	return 0;
}

static int dsps_resume(struct device *dev)
{
	struct musb_hdrc_platform_data *plat = dev->platform_data;
	struct omap_musb_board_data *data = plat->board_data;

	/* Start the on-chip PHY and its PLL. */
	if (data->set_phy_power)
		data->set_phy_power(1);

	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(dsps_pm_ops, dsps_suspend, dsps_resume);

static const struct dsps_musb_wrapper ti81xx_driver_data __devinitconst = {
	.revision		= 0x00,
	.control		= 0x14,
	.status			= 0x18,
	.eoi			= 0x24,
	.epintr_set		= 0x38,
	.epintr_clear		= 0x40,
	.epintr_status		= 0x30,
	.coreintr_set		= 0x3c,
	.coreintr_clear		= 0x44,
	.coreintr_status	= 0x34,
	.phy_utmi		= 0xe0,
	.mode			= 0xe8,
	.reset			= 0,
	.otg_disable		= 21,
	.iddig			= 8,
	.usb_shift		= 0,
	.usb_mask		= 0x1ff,
	.usb_bitmap		= (0x1ff << 0),
	.drvvbus		= 8,
	.txep_shift		= 0,
	.txep_mask		= 0xffff,
	.txep_bitmap		= (0xffff << 0),
	.rxep_shift		= 16,
	.rxep_mask		= 0xfffe,
	.rxep_bitmap		= (0xfffe << 16),
	.musb_core_offset	= 0x400,
	.poll_seconds		= 2,
};

static const struct platform_device_id musb_dsps_id_table[] __devinitconst = {
	{
		.name	= "musb-ti81xx",
		.driver_data	= (kernel_ulong_t) &ti81xx_driver_data,
	},
	{  },	/* Terminating Entry */
};
MODULE_DEVICE_TABLE(platform, musb_dsps_id_table);

static const struct of_device_id musb_dsps_of_match[] __devinitconst = {
	{ .compatible = "musb-ti81xx", },
	{ .compatible = "ti,ti81xx-musb", },
	{ .compatible = "ti,am335x-musb", },
	{  },
};
MODULE_DEVICE_TABLE(of, musb_dsps_of_match);

static struct platform_driver dsps_usbss_driver = {
	.probe		= dsps_probe,
	.remove         = __devexit_p(dsps_remove),
	.driver         = {
		.name   = "musb-dsps",
		.pm	= &dsps_pm_ops,
		.of_match_table	= musb_dsps_of_match,
	},
	.id_table	= musb_dsps_id_table,
};

MODULE_DESCRIPTION("TI DSPS MUSB Glue Layer");
MODULE_AUTHOR("Ravi B <ravibabu@ti.com>");
MODULE_AUTHOR("Ajay Kumar Gupta <ajay.gupta@ti.com>");
MODULE_LICENSE("GPL v2");

static int __init dsps_init(void)
{
	return platform_driver_register(&dsps_usbss_driver);
}
subsys_initcall(dsps_init);

static void __exit dsps_exit(void)
{
	platform_driver_unregister(&dsps_usbss_driver);
}
module_exit(dsps_exit);
