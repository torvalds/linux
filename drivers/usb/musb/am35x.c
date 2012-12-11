/*
 * Texas Instruments AM35x "glue layer"
 *
 * Copyright (c) 2010, by Texas Instruments
 *
 * Based on the DA8xx "glue layer" code.
 * Copyright (c) 2008-2009, MontaVista Software, Inc. <source@mvista.com>
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

#include <linux/init.h>
#include <linux/module.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/usb/nop-usb-xceiv.h>

#include <plat/usb.h>

#include "musb_core.h"

/*
 * AM35x specific definitions
 */
/* USB 2.0 OTG module registers */
#define USB_REVISION_REG	0x00
#define USB_CTRL_REG		0x04
#define USB_STAT_REG		0x08
#define USB_EMULATION_REG	0x0c
/* 0x10 Reserved */
#define USB_AUTOREQ_REG		0x14
#define USB_SRP_FIX_TIME_REG	0x18
#define USB_TEARDOWN_REG	0x1c
#define EP_INTR_SRC_REG		0x20
#define EP_INTR_SRC_SET_REG	0x24
#define EP_INTR_SRC_CLEAR_REG	0x28
#define EP_INTR_MASK_REG	0x2c
#define EP_INTR_MASK_SET_REG	0x30
#define EP_INTR_MASK_CLEAR_REG	0x34
#define EP_INTR_SRC_MASKED_REG	0x38
#define CORE_INTR_SRC_REG	0x40
#define CORE_INTR_SRC_SET_REG	0x44
#define CORE_INTR_SRC_CLEAR_REG	0x48
#define CORE_INTR_MASK_REG	0x4c
#define CORE_INTR_MASK_SET_REG	0x50
#define CORE_INTR_MASK_CLEAR_REG 0x54
#define CORE_INTR_SRC_MASKED_REG 0x58
/* 0x5c Reserved */
#define USB_END_OF_INTR_REG	0x60

/* Control register bits */
#define AM35X_SOFT_RESET_MASK	1

/* USB interrupt register bits */
#define AM35X_INTR_USB_SHIFT	16
#define AM35X_INTR_USB_MASK	(0x1ff << AM35X_INTR_USB_SHIFT)
#define AM35X_INTR_DRVVBUS	0x100
#define AM35X_INTR_RX_SHIFT	16
#define AM35X_INTR_TX_SHIFT	0
#define AM35X_TX_EP_MASK	0xffff		/* EP0 + 15 Tx EPs */
#define AM35X_RX_EP_MASK	0xfffe		/* 15 Rx EPs */
#define AM35X_TX_INTR_MASK	(AM35X_TX_EP_MASK << AM35X_INTR_TX_SHIFT)
#define AM35X_RX_INTR_MASK	(AM35X_RX_EP_MASK << AM35X_INTR_RX_SHIFT)

#define USB_MENTOR_CORE_OFFSET	0x400

struct am35x_glue {
	struct device		*dev;
	struct platform_device	*musb;
	struct clk		*phy_clk;
	struct clk		*clk;
};
#define glue_to_musb(g)		platform_get_drvdata(g->musb)

/*
 * am35x_musb_enable - enable interrupts
 */
static void am35x_musb_enable(struct musb *musb)
{
	void __iomem *reg_base = musb->ctrl_base;
	u32 epmask;

	/* Workaround: setup IRQs through both register sets. */
	epmask = ((musb->epmask & AM35X_TX_EP_MASK) << AM35X_INTR_TX_SHIFT) |
	       ((musb->epmask & AM35X_RX_EP_MASK) << AM35X_INTR_RX_SHIFT);

	musb_writel(reg_base, EP_INTR_MASK_SET_REG, epmask);
	musb_writel(reg_base, CORE_INTR_MASK_SET_REG, AM35X_INTR_USB_MASK);

	/* Force the DRVVBUS IRQ so we can start polling for ID change. */
	musb_writel(reg_base, CORE_INTR_SRC_SET_REG,
			AM35X_INTR_DRVVBUS << AM35X_INTR_USB_SHIFT);
}

/*
 * am35x_musb_disable - disable HDRC and flush interrupts
 */
static void am35x_musb_disable(struct musb *musb)
{
	void __iomem *reg_base = musb->ctrl_base;

	musb_writel(reg_base, CORE_INTR_MASK_CLEAR_REG, AM35X_INTR_USB_MASK);
	musb_writel(reg_base, EP_INTR_MASK_CLEAR_REG,
			 AM35X_TX_INTR_MASK | AM35X_RX_INTR_MASK);
	musb_writeb(musb->mregs, MUSB_DEVCTL, 0);
	musb_writel(reg_base, USB_END_OF_INTR_REG, 0);
}

#define portstate(stmt)		stmt

static void am35x_musb_set_vbus(struct musb *musb, int is_on)
{
	WARN_ON(is_on && is_peripheral_active(musb));
}

#define	POLL_SECONDS	2

static struct timer_list otg_workaround;

static void otg_timer(unsigned long _musb)
{
	struct musb		*musb = (void *)_musb;
	void __iomem		*mregs = musb->mregs;
	u8			devctl;
	unsigned long		flags;

	/*
	 * We poll because AM35x's won't expose several OTG-critical
	 * status change events (from the transceiver) otherwise.
	 */
	devctl = musb_readb(mregs, MUSB_DEVCTL);
	dev_dbg(musb->controller, "Poll devctl %02x (%s)\n", devctl,
		otg_state_string(musb->xceiv->state));

	spin_lock_irqsave(&musb->lock, flags);
	switch (musb->xceiv->state) {
	case OTG_STATE_A_WAIT_BCON:
		devctl &= ~MUSB_DEVCTL_SESSION;
		musb_writeb(musb->mregs, MUSB_DEVCTL, devctl);

		devctl = musb_readb(musb->mregs, MUSB_DEVCTL);
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
		musb_writel(musb->ctrl_base, CORE_INTR_SRC_SET_REG,
			    MUSB_INTR_VBUSERROR << AM35X_INTR_USB_SHIFT);
		break;
	case OTG_STATE_B_IDLE:
		devctl = musb_readb(mregs, MUSB_DEVCTL);
		if (devctl & MUSB_DEVCTL_BDEVICE)
			mod_timer(&otg_workaround, jiffies + POLL_SECONDS * HZ);
		else
			musb->xceiv->state = OTG_STATE_A_IDLE;
		break;
	default:
		break;
	}
	spin_unlock_irqrestore(&musb->lock, flags);
}

static void am35x_musb_try_idle(struct musb *musb, unsigned long timeout)
{
	static unsigned long last_timer;

	if (timeout == 0)
		timeout = jiffies + msecs_to_jiffies(3);

	/* Never idle if active, or when VBUS timeout is not set as host */
	if (musb->is_active || (musb->a_wait_bcon == 0 &&
				musb->xceiv->state == OTG_STATE_A_WAIT_BCON)) {
		dev_dbg(musb->controller, "%s active, deleting timer\n",
			otg_state_string(musb->xceiv->state));
		del_timer(&otg_workaround);
		last_timer = jiffies;
		return;
	}

	if (time_after(last_timer, timeout) && timer_pending(&otg_workaround)) {
		dev_dbg(musb->controller, "Longer idle timer already pending, ignoring...\n");
		return;
	}
	last_timer = timeout;

	dev_dbg(musb->controller, "%s inactive, starting idle timer for %u ms\n",
		otg_state_string(musb->xceiv->state),
		jiffies_to_msecs(timeout - jiffies));
	mod_timer(&otg_workaround, timeout);
}

static irqreturn_t am35x_musb_interrupt(int irq, void *hci)
{
	struct musb  *musb = hci;
	void __iomem *reg_base = musb->ctrl_base;
	struct device *dev = musb->controller;
	struct musb_hdrc_platform_data *plat = dev->platform_data;
	struct omap_musb_board_data *data = plat->board_data;
	struct usb_otg *otg = musb->xceiv->otg;
	unsigned long flags;
	irqreturn_t ret = IRQ_NONE;
	u32 epintr, usbintr;

	spin_lock_irqsave(&musb->lock, flags);

	/* Get endpoint interrupts */
	epintr = musb_readl(reg_base, EP_INTR_SRC_MASKED_REG);

	if (epintr) {
		musb_writel(reg_base, EP_INTR_SRC_CLEAR_REG, epintr);

		musb->int_rx =
			(epintr & AM35X_RX_INTR_MASK) >> AM35X_INTR_RX_SHIFT;
		musb->int_tx =
			(epintr & AM35X_TX_INTR_MASK) >> AM35X_INTR_TX_SHIFT;
	}

	/* Get usb core interrupts */
	usbintr = musb_readl(reg_base, CORE_INTR_SRC_MASKED_REG);
	if (!usbintr && !epintr)
		goto eoi;

	if (usbintr) {
		musb_writel(reg_base, CORE_INTR_SRC_CLEAR_REG, usbintr);

		musb->int_usb =
			(usbintr & AM35X_INTR_USB_MASK) >> AM35X_INTR_USB_SHIFT;
	}
	/*
	 * DRVVBUS IRQs are the only proxy we have (a very poor one!) for
	 * AM35x's missing ID change IRQ.  We need an ID change IRQ to
	 * switch appropriately between halves of the OTG state machine.
	 * Managing DEVCTL.SESSION per Mentor docs requires that we know its
	 * value but DEVCTL.BDEVICE is invalid without DEVCTL.SESSION set.
	 * Also, DRVVBUS pulses for SRP (but not at 5V) ...
	 */
	if (usbintr & (AM35X_INTR_DRVVBUS << AM35X_INTR_USB_SHIFT)) {
		int drvvbus = musb_readl(reg_base, USB_STAT_REG);
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
			musb->xceiv->state = OTG_STATE_A_WAIT_VFALL;
			mod_timer(&otg_workaround, jiffies + POLL_SECONDS * HZ);
			WARNING("VBUS error workaround (delay coming)\n");
		} else if (drvvbus) {
			MUSB_HST_MODE(musb);
			otg->default_a = 1;
			musb->xceiv->state = OTG_STATE_A_WAIT_VRISE;
			portstate(musb->port1_status |= USB_PORT_STAT_POWER);
			del_timer(&otg_workaround);
		} else {
			musb->is_active = 0;
			MUSB_DEV_MODE(musb);
			otg->default_a = 0;
			musb->xceiv->state = OTG_STATE_B_IDLE;
			portstate(musb->port1_status &= ~USB_PORT_STAT_POWER);
		}

		/* NOTE: this must complete power-on within 100 ms. */
		dev_dbg(musb->controller, "VBUS %s (%s)%s, devctl %02x\n",
				drvvbus ? "on" : "off",
				otg_state_string(musb->xceiv->state),
				err ? " ERROR" : "",
				devctl);
		ret = IRQ_HANDLED;
	}

	/* Drop spurious RX and TX if device is disconnected */
	if (musb->int_usb & MUSB_INTR_DISCONNECT) {
		musb->int_tx = 0;
		musb->int_rx = 0;
	}

	if (musb->int_tx || musb->int_rx || musb->int_usb)
		ret |= musb_interrupt(musb);

eoi:
	/* EOI needs to be written for the IRQ to be re-asserted. */
	if (ret == IRQ_HANDLED || epintr || usbintr) {
		/* clear level interrupt */
		if (data->clear_irq)
			data->clear_irq();
		/* write EOI */
		musb_writel(reg_base, USB_END_OF_INTR_REG, 0);
	}

	/* Poll for ID change */
	if (musb->xceiv->state == OTG_STATE_B_IDLE)
		mod_timer(&otg_workaround, jiffies + POLL_SECONDS * HZ);

	spin_unlock_irqrestore(&musb->lock, flags);

	return ret;
}

static int am35x_musb_set_mode(struct musb *musb, u8 musb_mode)
{
	struct device *dev = musb->controller;
	struct musb_hdrc_platform_data *plat = dev->platform_data;
	struct omap_musb_board_data *data = plat->board_data;
	int     retval = 0;

	if (data->set_mode)
		data->set_mode(musb_mode);
	else
		retval = -EIO;

	return retval;
}

static int am35x_musb_init(struct musb *musb)
{
	struct device *dev = musb->controller;
	struct musb_hdrc_platform_data *plat = dev->platform_data;
	struct omap_musb_board_data *data = plat->board_data;
	void __iomem *reg_base = musb->ctrl_base;
	u32 rev;

	musb->mregs += USB_MENTOR_CORE_OFFSET;

	/* Returns zero if e.g. not clocked */
	rev = musb_readl(reg_base, USB_REVISION_REG);
	if (!rev)
		return -ENODEV;

	usb_nop_xceiv_register();
	musb->xceiv = usb_get_phy(USB_PHY_TYPE_USB2);
	if (IS_ERR_OR_NULL(musb->xceiv))
		return -ENODEV;

	setup_timer(&otg_workaround, otg_timer, (unsigned long) musb);

	/* Reset the musb */
	if (data->reset)
		data->reset();

	/* Reset the controller */
	musb_writel(reg_base, USB_CTRL_REG, AM35X_SOFT_RESET_MASK);

	/* Start the on-chip PHY and its PLL. */
	if (data->set_phy_power)
		data->set_phy_power(1);

	msleep(5);

	musb->isr = am35x_musb_interrupt;

	/* clear level interrupt */
	if (data->clear_irq)
		data->clear_irq();

	return 0;
}

static int am35x_musb_exit(struct musb *musb)
{
	struct device *dev = musb->controller;
	struct musb_hdrc_platform_data *plat = dev->platform_data;
	struct omap_musb_board_data *data = plat->board_data;

	del_timer_sync(&otg_workaround);

	/* Shutdown the on-chip PHY and its PLL. */
	if (data->set_phy_power)
		data->set_phy_power(0);

	usb_put_phy(musb->xceiv);
	usb_nop_xceiv_unregister();

	return 0;
}

/* AM35x supports only 32bit read operation */
void musb_read_fifo(struct musb_hw_ep *hw_ep, u16 len, u8 *dst)
{
	void __iomem *fifo = hw_ep->fifo;
	u32		val;
	int		i;

	/* Read for 32bit-aligned destination address */
	if (likely((0x03 & (unsigned long) dst) == 0) && len >= 4) {
		readsl(fifo, dst, len >> 2);
		dst += len & ~0x03;
		len &= 0x03;
	}
	/*
	 * Now read the remaining 1 to 3 byte or complete length if
	 * unaligned address.
	 */
	if (len > 4) {
		for (i = 0; i < (len >> 2); i++) {
			*(u32 *) dst = musb_readl(fifo, 0);
			dst += 4;
		}
		len &= 0x03;
	}
	if (len > 0) {
		val = musb_readl(fifo, 0);
		memcpy(dst, &val, len);
	}
}

static const struct musb_platform_ops am35x_ops = {
	.init		= am35x_musb_init,
	.exit		= am35x_musb_exit,

	.enable		= am35x_musb_enable,
	.disable	= am35x_musb_disable,

	.set_mode	= am35x_musb_set_mode,
	.try_idle	= am35x_musb_try_idle,

	.set_vbus	= am35x_musb_set_vbus,
};

static u64 am35x_dmamask = DMA_BIT_MASK(32);

static int __devinit am35x_probe(struct platform_device *pdev)
{
	struct musb_hdrc_platform_data	*pdata = pdev->dev.platform_data;
	struct platform_device		*musb;
	struct am35x_glue		*glue;

	struct clk			*phy_clk;
	struct clk			*clk;

	int				ret = -ENOMEM;
	int				musbid;

	glue = kzalloc(sizeof(*glue), GFP_KERNEL);
	if (!glue) {
		dev_err(&pdev->dev, "failed to allocate glue context\n");
		goto err0;
	}

	/* get the musb id */
	musbid = musb_get_id(&pdev->dev, GFP_KERNEL);
	if (musbid < 0) {
		dev_err(&pdev->dev, "failed to allocate musb id\n");
		ret = -ENOMEM;
		goto err1;
	}

	musb = platform_device_alloc("musb-hdrc", musbid);
	if (!musb) {
		dev_err(&pdev->dev, "failed to allocate musb device\n");
		goto err2;
	}

	phy_clk = clk_get(&pdev->dev, "fck");
	if (IS_ERR(phy_clk)) {
		dev_err(&pdev->dev, "failed to get PHY clock\n");
		ret = PTR_ERR(phy_clk);
		goto err3;
	}

	clk = clk_get(&pdev->dev, "ick");
	if (IS_ERR(clk)) {
		dev_err(&pdev->dev, "failed to get clock\n");
		ret = PTR_ERR(clk);
		goto err4;
	}

	ret = clk_enable(phy_clk);
	if (ret) {
		dev_err(&pdev->dev, "failed to enable PHY clock\n");
		goto err5;
	}

	ret = clk_enable(clk);
	if (ret) {
		dev_err(&pdev->dev, "failed to enable clock\n");
		goto err6;
	}

	musb->id			= musbid;
	musb->dev.parent		= &pdev->dev;
	musb->dev.dma_mask		= &am35x_dmamask;
	musb->dev.coherent_dma_mask	= am35x_dmamask;

	glue->dev			= &pdev->dev;
	glue->musb			= musb;
	glue->phy_clk			= phy_clk;
	glue->clk			= clk;

	pdata->platform_ops		= &am35x_ops;

	platform_set_drvdata(pdev, glue);

	ret = platform_device_add_resources(musb, pdev->resource,
			pdev->num_resources);
	if (ret) {
		dev_err(&pdev->dev, "failed to add resources\n");
		goto err7;
	}

	ret = platform_device_add_data(musb, pdata, sizeof(*pdata));
	if (ret) {
		dev_err(&pdev->dev, "failed to add platform_data\n");
		goto err7;
	}

	ret = platform_device_add(musb);
	if (ret) {
		dev_err(&pdev->dev, "failed to register musb device\n");
		goto err7;
	}

	return 0;

err7:
	clk_disable(clk);

err6:
	clk_disable(phy_clk);

err5:
	clk_put(clk);

err4:
	clk_put(phy_clk);

err3:
	platform_device_put(musb);

err2:
	musb_put_id(&pdev->dev, musbid);

err1:
	kfree(glue);

err0:
	return ret;
}

static int __devexit am35x_remove(struct platform_device *pdev)
{
	struct am35x_glue	*glue = platform_get_drvdata(pdev);

	musb_put_id(&pdev->dev, glue->musb->id);
	platform_device_del(glue->musb);
	platform_device_put(glue->musb);
	clk_disable(glue->clk);
	clk_disable(glue->phy_clk);
	clk_put(glue->clk);
	clk_put(glue->phy_clk);
	kfree(glue);

	return 0;
}

#ifdef CONFIG_PM
static int am35x_suspend(struct device *dev)
{
	struct am35x_glue	*glue = dev_get_drvdata(dev);
	struct musb_hdrc_platform_data *plat = dev->platform_data;
	struct omap_musb_board_data *data = plat->board_data;

	/* Shutdown the on-chip PHY and its PLL. */
	if (data->set_phy_power)
		data->set_phy_power(0);

	clk_disable(glue->phy_clk);
	clk_disable(glue->clk);

	return 0;
}

static int am35x_resume(struct device *dev)
{
	struct am35x_glue	*glue = dev_get_drvdata(dev);
	struct musb_hdrc_platform_data *plat = dev->platform_data;
	struct omap_musb_board_data *data = plat->board_data;
	int			ret;

	/* Start the on-chip PHY and its PLL. */
	if (data->set_phy_power)
		data->set_phy_power(1);

	ret = clk_enable(glue->phy_clk);
	if (ret) {
		dev_err(dev, "failed to enable PHY clock\n");
		return ret;
	}

	ret = clk_enable(glue->clk);
	if (ret) {
		dev_err(dev, "failed to enable clock\n");
		return ret;
	}

	return 0;
}

static struct dev_pm_ops am35x_pm_ops = {
	.suspend	= am35x_suspend,
	.resume		= am35x_resume,
};

#define DEV_PM_OPS	&am35x_pm_ops
#else
#define DEV_PM_OPS	NULL
#endif

static struct platform_driver am35x_driver = {
	.probe		= am35x_probe,
	.remove		= __devexit_p(am35x_remove),
	.driver		= {
		.name	= "musb-am35x",
		.pm	= DEV_PM_OPS,
	},
};

MODULE_DESCRIPTION("AM35x MUSB Glue Layer");
MODULE_AUTHOR("Ajay Kumar Gupta <ajay.gupta@ti.com>");
MODULE_LICENSE("GPL v2");

static int __init am35x_init(void)
{
	return platform_driver_register(&am35x_driver);
}
module_init(am35x_init);

static void __exit am35x_exit(void)
{
	platform_driver_unregister(&am35x_driver);
}
module_exit(am35x_exit);
