// SPDX-License-Identifier: GPL-2.0
/*
 * Texas Instruments DA8xx/OMAP-L1x "glue layer"
 *
 * Copyright (c) 2008-2009 MontaVista Software, Inc. <source@mvista.com>
 *
 * Based on the DaVinci "glue layer" code.
 * Copyright (C) 2005-2006 by Texas Instruments
 *
 * DT support
 * Copyright (c) 2016 Petr Kulhavy <petr@barix.com>
 *
 * This file is part of the Inventra Controller Driver for Linux.
 */

#include <linux/module.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/string_choices.h>
#include <linux/dma-mapping.h>
#include <linux/usb/usb_phy_generic.h>

#include "musb_core.h"

/*
 * DA8XX specific definitions
 */

/* USB 2.0 OTG module registers */
#define DA8XX_USB_REVISION_REG	0x00
#define DA8XX_USB_CTRL_REG	0x04
#define DA8XX_USB_STAT_REG	0x08
#define DA8XX_USB_EMULATION_REG 0x0c
#define DA8XX_USB_SRP_FIX_TIME_REG 0x18
#define DA8XX_USB_INTR_SRC_REG	0x20
#define DA8XX_USB_INTR_SRC_SET_REG 0x24
#define DA8XX_USB_INTR_SRC_CLEAR_REG 0x28
#define DA8XX_USB_INTR_MASK_REG 0x2c
#define DA8XX_USB_INTR_MASK_SET_REG 0x30
#define DA8XX_USB_INTR_MASK_CLEAR_REG 0x34
#define DA8XX_USB_INTR_SRC_MASKED_REG 0x38
#define DA8XX_USB_END_OF_INTR_REG 0x3c
#define DA8XX_USB_GENERIC_RNDIS_EP_SIZE_REG(n) (0x50 + (((n) - 1) << 2))

/* Control register bits */
#define DA8XX_SOFT_RESET_MASK	1

#define DA8XX_USB_TX_EP_MASK	0x1f		/* EP0 + 4 Tx EPs */
#define DA8XX_USB_RX_EP_MASK	0x1e		/* 4 Rx EPs */

/* USB interrupt register bits */
#define DA8XX_INTR_USB_SHIFT	16
#define DA8XX_INTR_USB_MASK	(0x1ff << DA8XX_INTR_USB_SHIFT) /* 8 Mentor */
					/* interrupts and DRVVBUS interrupt */
#define DA8XX_INTR_DRVVBUS	0x100
#define DA8XX_INTR_RX_SHIFT	8
#define DA8XX_INTR_RX_MASK	(DA8XX_USB_RX_EP_MASK << DA8XX_INTR_RX_SHIFT)
#define DA8XX_INTR_TX_SHIFT	0
#define DA8XX_INTR_TX_MASK	(DA8XX_USB_TX_EP_MASK << DA8XX_INTR_TX_SHIFT)

#define DA8XX_MENTOR_CORE_OFFSET 0x400

struct da8xx_glue {
	struct device		*dev;
	struct platform_device	*musb;
	struct platform_device	*usb_phy;
	struct clk		*clk;
	struct phy		*phy;
};

/*
 * Because we don't set CTRL.UINT, it's "important" to:
 *	- not read/write INTRUSB/INTRUSBE (except during
 *	  initial setup, as a workaround);
 *	- use INTSET/INTCLR instead.
 */

/**
 * da8xx_musb_enable - enable interrupts
 */
static void da8xx_musb_enable(struct musb *musb)
{
	void __iomem *reg_base = musb->ctrl_base;
	u32 mask;

	/* Workaround: setup IRQs through both register sets. */
	mask = ((musb->epmask & DA8XX_USB_TX_EP_MASK) << DA8XX_INTR_TX_SHIFT) |
	       ((musb->epmask & DA8XX_USB_RX_EP_MASK) << DA8XX_INTR_RX_SHIFT) |
	       DA8XX_INTR_USB_MASK;
	musb_writel(reg_base, DA8XX_USB_INTR_MASK_SET_REG, mask);

	/* Force the DRVVBUS IRQ so we can start polling for ID change. */
	musb_writel(reg_base, DA8XX_USB_INTR_SRC_SET_REG,
			DA8XX_INTR_DRVVBUS << DA8XX_INTR_USB_SHIFT);
}

/**
 * da8xx_musb_disable - disable HDRC and flush interrupts
 */
static void da8xx_musb_disable(struct musb *musb)
{
	void __iomem *reg_base = musb->ctrl_base;

	musb_writel(reg_base, DA8XX_USB_INTR_MASK_CLEAR_REG,
		    DA8XX_INTR_USB_MASK |
		    DA8XX_INTR_TX_MASK | DA8XX_INTR_RX_MASK);
	musb_writel(reg_base, DA8XX_USB_END_OF_INTR_REG, 0);
}

#define portstate(stmt)		stmt

static void da8xx_musb_set_vbus(struct musb *musb, int is_on)
{
	WARN_ON(is_on && is_peripheral_active(musb));
}

#define	POLL_SECONDS	2

static void otg_timer(struct timer_list *t)
{
	struct musb		*musb = from_timer(musb, t, dev_timer);
	void __iomem		*mregs = musb->mregs;
	u8			devctl;
	unsigned long		flags;

	/*
	 * We poll because DaVinci's won't expose several OTG-critical
	 * status change events (from the transceiver) otherwise.
	 */
	devctl = musb_readb(mregs, MUSB_DEVCTL);
	dev_dbg(musb->controller, "Poll devctl %02x (%s)\n", devctl,
		usb_otg_state_string(musb->xceiv->otg->state));

	spin_lock_irqsave(&musb->lock, flags);
	switch (musb->xceiv->otg->state) {
	case OTG_STATE_A_WAIT_BCON:
		devctl &= ~MUSB_DEVCTL_SESSION;
		musb_writeb(musb->mregs, MUSB_DEVCTL, devctl);

		devctl = musb_readb(musb->mregs, MUSB_DEVCTL);
		if (devctl & MUSB_DEVCTL_BDEVICE) {
			musb->xceiv->otg->state = OTG_STATE_B_IDLE;
			MUSB_DEV_MODE(musb);
		} else {
			musb->xceiv->otg->state = OTG_STATE_A_IDLE;
			MUSB_HST_MODE(musb);
		}
		break;
	case OTG_STATE_A_WAIT_VFALL:
		/*
		 * Wait till VBUS falls below SessionEnd (~0.2 V); the 1.3
		 * RTL seems to mis-handle session "start" otherwise (or in
		 * our case "recover"), in routine "VBUS was valid by the time
		 * VBUSERR got reported during enumeration" cases.
		 */
		if (devctl & MUSB_DEVCTL_VBUS) {
			mod_timer(&musb->dev_timer, jiffies + POLL_SECONDS * HZ);
			break;
		}
		musb->xceiv->otg->state = OTG_STATE_A_WAIT_VRISE;
		musb_writel(musb->ctrl_base, DA8XX_USB_INTR_SRC_SET_REG,
			    MUSB_INTR_VBUSERROR << DA8XX_INTR_USB_SHIFT);
		break;
	case OTG_STATE_B_IDLE:
		/*
		 * There's no ID-changed IRQ, so we have no good way to tell
		 * when to switch to the A-Default state machine (by setting
		 * the DEVCTL.Session bit).
		 *
		 * Workaround:  whenever we're in B_IDLE, try setting the
		 * session flag every few seconds.  If it works, ID was
		 * grounded and we're now in the A-Default state machine.
		 *
		 * NOTE: setting the session flag is _supposed_ to trigger
		 * SRP but clearly it doesn't.
		 */
		musb_writeb(mregs, MUSB_DEVCTL, devctl | MUSB_DEVCTL_SESSION);
		devctl = musb_readb(mregs, MUSB_DEVCTL);
		if (devctl & MUSB_DEVCTL_BDEVICE)
			mod_timer(&musb->dev_timer, jiffies + POLL_SECONDS * HZ);
		else
			musb->xceiv->otg->state = OTG_STATE_A_IDLE;
		break;
	default:
		break;
	}
	spin_unlock_irqrestore(&musb->lock, flags);
}

static void __maybe_unused da8xx_musb_try_idle(struct musb *musb, unsigned long timeout)
{
	static unsigned long last_timer;

	if (timeout == 0)
		timeout = jiffies + msecs_to_jiffies(3);

	/* Never idle if active, or when VBUS timeout is not set as host */
	if (musb->is_active || (musb->a_wait_bcon == 0 &&
				musb->xceiv->otg->state == OTG_STATE_A_WAIT_BCON)) {
		dev_dbg(musb->controller, "%s active, deleting timer\n",
			usb_otg_state_string(musb->xceiv->otg->state));
		timer_delete(&musb->dev_timer);
		last_timer = jiffies;
		return;
	}

	if (time_after(last_timer, timeout) && timer_pending(&musb->dev_timer)) {
		dev_dbg(musb->controller, "Longer idle timer already pending, ignoring...\n");
		return;
	}
	last_timer = timeout;

	dev_dbg(musb->controller, "%s inactive, starting idle timer for %u ms\n",
		usb_otg_state_string(musb->xceiv->otg->state),
		jiffies_to_msecs(timeout - jiffies));
	mod_timer(&musb->dev_timer, timeout);
}

static int da8xx_babble_recover(struct musb *musb)
{
	dev_dbg(musb->controller, "resetting controller to recover from babble\n");
	musb_writel(musb->ctrl_base, DA8XX_USB_CTRL_REG, DA8XX_SOFT_RESET_MASK);
	return 0;
}

static irqreturn_t da8xx_musb_interrupt(int irq, void *hci)
{
	struct musb		*musb = hci;
	void __iomem		*reg_base = musb->ctrl_base;
	unsigned long		flags;
	irqreturn_t		ret = IRQ_NONE;
	u32			status;

	spin_lock_irqsave(&musb->lock, flags);

	/*
	 * NOTE: DA8XX shadows the Mentor IRQs.  Don't manage them through
	 * the Mentor registers (except for setup), use the TI ones and EOI.
	 */

	/* Acknowledge and handle non-CPPI interrupts */
	status = musb_readl(reg_base, DA8XX_USB_INTR_SRC_MASKED_REG);
	if (!status)
		goto eoi;

	musb_writel(reg_base, DA8XX_USB_INTR_SRC_CLEAR_REG, status);
	dev_dbg(musb->controller, "USB IRQ %08x\n", status);

	musb->int_rx = (status & DA8XX_INTR_RX_MASK) >> DA8XX_INTR_RX_SHIFT;
	musb->int_tx = (status & DA8XX_INTR_TX_MASK) >> DA8XX_INTR_TX_SHIFT;
	musb->int_usb = (status & DA8XX_INTR_USB_MASK) >> DA8XX_INTR_USB_SHIFT;

	/*
	 * DRVVBUS IRQs are the only proxy we have (a very poor one!) for
	 * DA8xx's missing ID change IRQ.  We need an ID change IRQ to
	 * switch appropriately between halves of the OTG state machine.
	 * Managing DEVCTL.Session per Mentor docs requires that we know its
	 * value but DEVCTL.BDevice is invalid without DEVCTL.Session set.
	 * Also, DRVVBUS pulses for SRP (but not at 5 V)...
	 */
	if (status & (DA8XX_INTR_DRVVBUS << DA8XX_INTR_USB_SHIFT)) {
		int drvvbus = musb_readl(reg_base, DA8XX_USB_STAT_REG);
		void __iomem *mregs = musb->mregs;
		u8 devctl = musb_readb(mregs, MUSB_DEVCTL);
		int err;

		err = musb->int_usb & MUSB_INTR_VBUSERROR;
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
			musb->xceiv->otg->state = OTG_STATE_A_WAIT_VFALL;
			mod_timer(&musb->dev_timer, jiffies + POLL_SECONDS * HZ);
			WARNING("VBUS error workaround (delay coming)\n");
		} else if (drvvbus) {
			MUSB_HST_MODE(musb);
			musb->xceiv->otg->state = OTG_STATE_A_WAIT_VRISE;
			portstate(musb->port1_status |= USB_PORT_STAT_POWER);
			timer_delete(&musb->dev_timer);
		} else if (!(musb->int_usb & MUSB_INTR_BABBLE)) {
			/*
			 * When babble condition happens, drvvbus interrupt
			 * is also generated. Ignore this drvvbus interrupt
			 * and let babble interrupt handler recovers the
			 * controller; otherwise, the host-mode flag is lost
			 * due to the MUSB_DEV_MODE() call below and babble
			 * recovery logic will not be called.
			 */
			musb->is_active = 0;
			MUSB_DEV_MODE(musb);
			musb->xceiv->otg->state = OTG_STATE_B_IDLE;
			portstate(musb->port1_status &= ~USB_PORT_STAT_POWER);
		}

		dev_dbg(musb->controller, "VBUS %s (%s)%s, devctl %02x\n",
				str_on_off(drvvbus),
				usb_otg_state_string(musb->xceiv->otg->state),
				err ? " ERROR" : "",
				devctl);
		ret = IRQ_HANDLED;
	}

	if (musb->int_tx || musb->int_rx || musb->int_usb)
		ret |= musb_interrupt(musb);

 eoi:
	/* EOI needs to be written for the IRQ to be re-asserted. */
	if (ret == IRQ_HANDLED || status)
		musb_writel(reg_base, DA8XX_USB_END_OF_INTR_REG, 0);

	/* Poll for ID change */
	if (musb->xceiv->otg->state == OTG_STATE_B_IDLE)
		mod_timer(&musb->dev_timer, jiffies + POLL_SECONDS * HZ);

	spin_unlock_irqrestore(&musb->lock, flags);

	return ret;
}

static int da8xx_musb_set_mode(struct musb *musb, u8 musb_mode)
{
	struct da8xx_glue *glue = dev_get_drvdata(musb->controller->parent);
	enum phy_mode phy_mode;

	switch (musb_mode) {
	case MUSB_HOST:		/* Force VBUS valid, ID = 0 */
		phy_mode = PHY_MODE_USB_HOST;
		break;
	case MUSB_PERIPHERAL:	/* Force VBUS valid, ID = 1 */
		phy_mode = PHY_MODE_USB_DEVICE;
		break;
	case MUSB_OTG:		/* Don't override the VBUS/ID comparators */
		phy_mode = PHY_MODE_USB_OTG;
		break;
	default:
		return -EINVAL;
	}

	return phy_set_mode(glue->phy, phy_mode);
}

static int da8xx_musb_init(struct musb *musb)
{
	struct da8xx_glue *glue = dev_get_drvdata(musb->controller->parent);
	void __iomem *reg_base = musb->ctrl_base;
	u32 rev;
	int ret = -ENODEV;

	musb->mregs += DA8XX_MENTOR_CORE_OFFSET;

	ret = clk_prepare_enable(glue->clk);
	if (ret) {
		dev_err(glue->dev, "failed to enable clock\n");
		return ret;
	}

	/* Returns zero if e.g. not clocked */
	rev = musb_readl(reg_base, DA8XX_USB_REVISION_REG);
	if (!rev) {
		ret = -ENODEV;
		goto fail;
	}

	musb->xceiv = usb_get_phy(USB_PHY_TYPE_USB2);
	if (IS_ERR_OR_NULL(musb->xceiv)) {
		ret = -EPROBE_DEFER;
		goto fail;
	}

	timer_setup(&musb->dev_timer, otg_timer, 0);

	/* Reset the controller */
	musb_writel(reg_base, DA8XX_USB_CTRL_REG, DA8XX_SOFT_RESET_MASK);

	/* Start the on-chip PHY and its PLL. */
	ret = phy_init(glue->phy);
	if (ret) {
		dev_err(glue->dev, "Failed to init phy.\n");
		goto fail;
	}

	ret = phy_power_on(glue->phy);
	if (ret) {
		dev_err(glue->dev, "Failed to power on phy.\n");
		goto err_phy_power_on;
	}

	msleep(5);

	/* NOTE: IRQs are in mixed mode, not bypass to pure MUSB */
	pr_debug("DA8xx OTG revision %08x, control %02x\n", rev,
		 musb_readb(reg_base, DA8XX_USB_CTRL_REG));

	musb->isr = da8xx_musb_interrupt;
	return 0;

err_phy_power_on:
	phy_exit(glue->phy);
fail:
	clk_disable_unprepare(glue->clk);
	return ret;
}

static int da8xx_musb_exit(struct musb *musb)
{
	struct da8xx_glue *glue = dev_get_drvdata(musb->controller->parent);

	timer_delete_sync(&musb->dev_timer);

	phy_power_off(glue->phy);
	phy_exit(glue->phy);
	clk_disable_unprepare(glue->clk);

	usb_put_phy(musb->xceiv);

	return 0;
}

static inline u8 get_vbus_power(struct device *dev)
{
	struct regulator *vbus_supply;
	int current_uA;

	vbus_supply = regulator_get_optional(dev, "vbus");
	if (IS_ERR(vbus_supply))
		return 255;
	current_uA = regulator_get_current_limit(vbus_supply);
	regulator_put(vbus_supply);
	if (current_uA <= 0 || current_uA > 510000)
		return 255;
	return current_uA / 1000 / 2;
}

#ifdef CONFIG_USB_TI_CPPI41_DMA
static void da8xx_dma_controller_callback(struct dma_controller *c)
{
	struct musb *musb = c->musb;
	void __iomem *reg_base = musb->ctrl_base;

	musb_writel(reg_base, DA8XX_USB_END_OF_INTR_REG, 0);
}

static struct dma_controller *
da8xx_dma_controller_create(struct musb *musb, void __iomem *base)
{
	struct dma_controller *controller;

	controller = cppi41_dma_controller_create(musb, base);
	if (IS_ERR_OR_NULL(controller))
		return controller;

	controller->dma_callback = da8xx_dma_controller_callback;

	return controller;
}
#endif

static const struct musb_platform_ops da8xx_ops = {
	.quirks		= MUSB_INDEXED_EP | MUSB_PRESERVE_SESSION |
			  MUSB_DMA_CPPI41 | MUSB_DA8XX,
	.init		= da8xx_musb_init,
	.exit		= da8xx_musb_exit,

	.fifo_mode	= 2,
#ifdef CONFIG_USB_TI_CPPI41_DMA
	.dma_init	= da8xx_dma_controller_create,
	.dma_exit	= cppi41_dma_controller_destroy,
#endif
	.enable		= da8xx_musb_enable,
	.disable	= da8xx_musb_disable,

	.set_mode	= da8xx_musb_set_mode,

#ifndef CONFIG_USB_MUSB_HOST
	.try_idle	= da8xx_musb_try_idle,
#endif
	.recover	= da8xx_babble_recover,

	.set_vbus	= da8xx_musb_set_vbus,
};

static const struct platform_device_info da8xx_dev_info = {
	.name		= "musb-hdrc",
	.id		= PLATFORM_DEVID_AUTO,
	.dma_mask	= DMA_BIT_MASK(32),
};

static const struct musb_hdrc_config da8xx_config = {
	.ram_bits = 10,
	.num_eps = 5,
	.multipoint = 1,
};

static struct of_dev_auxdata da8xx_auxdata_lookup[] = {
	OF_DEV_AUXDATA("ti,da830-cppi41", 0x01e01000, "cppi41-dmaengine",
		       NULL),
	{}
};

static int da8xx_probe(struct platform_device *pdev)
{
	struct musb_hdrc_platform_data	*pdata = dev_get_platdata(&pdev->dev);
	struct da8xx_glue		*glue;
	struct platform_device_info	pinfo;
	struct clk			*clk;
	struct device_node		*np = pdev->dev.of_node;
	int				ret;

	glue = devm_kzalloc(&pdev->dev, sizeof(*glue), GFP_KERNEL);
	if (!glue)
		return -ENOMEM;

	clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(clk)) {
		dev_err(&pdev->dev, "failed to get clock\n");
		return PTR_ERR(clk);
	}

	glue->phy = devm_phy_get(&pdev->dev, "usb-phy");
	if (IS_ERR(glue->phy))
		return dev_err_probe(&pdev->dev, PTR_ERR(glue->phy),
				     "failed to get phy\n");

	glue->dev			= &pdev->dev;
	glue->clk			= clk;

	if (IS_ENABLED(CONFIG_OF) && np) {
		pdata = devm_kzalloc(&pdev->dev, sizeof(*pdata), GFP_KERNEL);
		if (!pdata)
			return -ENOMEM;

		pdata->config	= &da8xx_config;
		pdata->mode	= musb_get_mode(&pdev->dev);
		pdata->power	= get_vbus_power(&pdev->dev);
	}

	pdata->platform_ops		= &da8xx_ops;

	glue->usb_phy = usb_phy_generic_register();
	ret = PTR_ERR_OR_ZERO(glue->usb_phy);
	if (ret) {
		dev_err(&pdev->dev, "failed to register usb_phy\n");
		return ret;
	}
	platform_set_drvdata(pdev, glue);

	ret = of_platform_populate(pdev->dev.of_node, NULL,
				   da8xx_auxdata_lookup, &pdev->dev);
	if (ret)
		goto err_unregister_phy;

	pinfo = da8xx_dev_info;
	pinfo.parent = &pdev->dev;
	pinfo.res = pdev->resource;
	pinfo.num_res = pdev->num_resources;
	pinfo.data = pdata;
	pinfo.size_data = sizeof(*pdata);
	pinfo.fwnode = of_fwnode_handle(np);
	pinfo.of_node_reused = true;

	glue->musb = platform_device_register_full(&pinfo);
	ret = PTR_ERR_OR_ZERO(glue->musb);
	if (ret) {
		dev_err(&pdev->dev, "failed to register musb device: %d\n", ret);
		goto err_unregister_phy;
	}

	return 0;

err_unregister_phy:
	usb_phy_generic_unregister(glue->usb_phy);
	return ret;
}

static void da8xx_remove(struct platform_device *pdev)
{
	struct da8xx_glue		*glue = platform_get_drvdata(pdev);

	platform_device_unregister(glue->musb);
	usb_phy_generic_unregister(glue->usb_phy);
}

#ifdef CONFIG_PM_SLEEP
static int da8xx_suspend(struct device *dev)
{
	int ret;
	struct da8xx_glue *glue = dev_get_drvdata(dev);

	ret = phy_power_off(glue->phy);
	if (ret)
		return ret;
	clk_disable_unprepare(glue->clk);

	return 0;
}

static int da8xx_resume(struct device *dev)
{
	int ret;
	struct da8xx_glue *glue = dev_get_drvdata(dev);

	ret = clk_prepare_enable(glue->clk);
	if (ret)
		return ret;
	return phy_power_on(glue->phy);
}
#endif

static SIMPLE_DEV_PM_OPS(da8xx_pm_ops, da8xx_suspend, da8xx_resume);

#ifdef CONFIG_OF
static const struct of_device_id da8xx_id_table[] = {
	{
		.compatible = "ti,da830-musb",
	},
	{},
};
MODULE_DEVICE_TABLE(of, da8xx_id_table);
#endif

static struct platform_driver da8xx_driver = {
	.probe		= da8xx_probe,
	.remove		= da8xx_remove,
	.driver		= {
		.name	= "musb-da8xx",
		.pm = &da8xx_pm_ops,
		.of_match_table = of_match_ptr(da8xx_id_table),
	},
};

MODULE_DESCRIPTION("DA8xx/OMAP-L1x MUSB Glue Layer");
MODULE_AUTHOR("Sergei Shtylyov <sshtylyov@ru.mvista.com>");
MODULE_LICENSE("GPL v2");
module_platform_driver(da8xx_driver);
