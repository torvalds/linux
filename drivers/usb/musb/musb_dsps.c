// SPDX-License-Identifier: GPL-2.0
/*
 * Texas Instruments DSPS platforms "glue layer"
 *
 * Copyright (C) 2012, by Texas Instruments
 *
 * Based on the am35x "glue layer" code.
 *
 * This file is part of the Inventra Controller Driver for Linux.
 *
 * musb_dsps.c will be a common file for all the TI DSPS platforms
 * such as dm64x, dm36x, dm35x, da8x, am35x and ti81x.
 * For now only ti81x is using this and in future davinci.c, am35x.c
 * da8xx.c would be merged to this file after testing.
 */

#include <linux/io.h>
#include <linux/irq.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/pm_runtime.h>
#include <linux/module.h>
#include <linux/usb/usb_phy_generic.h>
#include <linux/platform_data/usb-omap.h>
#include <linux/sizes.h>

#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/usb/of.h>

#include <linux/debugfs.h>

#include "musb_core.h"

static const struct of_device_id musb_dsps_of_match[];

/*
 * DSPS musb wrapper register offset.
 * FIXME: This should be expanded to have all the wrapper registers from TI DSPS
 * musb ips.
 */
struct dsps_musb_wrapper {
	u16	revision;
	u16	control;
	u16	status;
	u16	epintr_set;
	u16	epintr_clear;
	u16	epintr_status;
	u16	coreintr_set;
	u16	coreintr_clear;
	u16	coreintr_status;
	u16	phy_utmi;
	u16	mode;
	u16	tx_mode;
	u16	rx_mode;

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
	unsigned	iddig_mux:5;
	/* miscellaneous stuff */
	unsigned	poll_timeout;
};

/*
 * register shadow for suspend
 */
struct dsps_context {
	u32 control;
	u32 epintr;
	u32 coreintr;
	u32 phy_utmi;
	u32 mode;
	u32 tx_mode;
	u32 rx_mode;
};

/*
 * DSPS glue structure.
 */
struct dsps_glue {
	struct device *dev;
	struct platform_device *musb;	/* child musb pdev */
	const struct dsps_musb_wrapper *wrp; /* wrapper register offsets */
	int vbus_irq;			/* optional vbus irq */
	unsigned long last_timer;    /* last timer data for each instance */
	bool sw_babble_enabled;
	void __iomem *usbss_base;

	struct dsps_context context;
	struct debugfs_regset32 regset;
	struct dentry *dbgfs_root;
};

static const struct debugfs_reg32 dsps_musb_regs[] = {
	{ "revision",		0x00 },
	{ "control",		0x14 },
	{ "status",		0x18 },
	{ "eoi",		0x24 },
	{ "intr0_stat",		0x30 },
	{ "intr1_stat",		0x34 },
	{ "intr0_set",		0x38 },
	{ "intr1_set",		0x3c },
	{ "txmode",		0x70 },
	{ "rxmode",		0x74 },
	{ "autoreq",		0xd0 },
	{ "srpfixtime",		0xd4 },
	{ "tdown",		0xd8 },
	{ "phy_utmi",		0xe0 },
	{ "mode",		0xe8 },
};

static void dsps_mod_timer(struct dsps_glue *glue, int wait_ms)
{
	struct musb *musb = platform_get_drvdata(glue->musb);
	int wait;

	if (wait_ms < 0)
		wait = msecs_to_jiffies(glue->wrp->poll_timeout);
	else
		wait = msecs_to_jiffies(wait_ms);

	mod_timer(&musb->dev_timer, jiffies + wait);
}

/*
 * If no vbus irq from the PMIC is configured, we need to poll VBUS status.
 */
static void dsps_mod_timer_optional(struct dsps_glue *glue)
{
	if (glue->vbus_irq)
		return;

	dsps_mod_timer(glue, -1);
}

/* USBSS  / USB AM335x */
#define USBSS_IRQ_STATUS	0x28
#define USBSS_IRQ_ENABLER	0x2c
#define USBSS_IRQ_CLEARR	0x30

#define USBSS_IRQ_PD_COMP	(1 << 2)

/*
 * dsps_musb_enable - enable interrupts
 */
static void dsps_musb_enable(struct musb *musb)
{
	struct device *dev = musb->controller;
	struct dsps_glue *glue = dev_get_drvdata(dev->parent);
	const struct dsps_musb_wrapper *wrp = glue->wrp;
	void __iomem *reg_base = musb->ctrl_base;
	u32 epmask, coremask;

	/* Workaround: setup IRQs through both register sets. */
	epmask = ((musb->epmask & wrp->txep_mask) << wrp->txep_shift) |
	       ((musb->epmask & wrp->rxep_mask) << wrp->rxep_shift);
	coremask = (wrp->usb_bitmap & ~MUSB_INTR_SOF);

	musb_writel(reg_base, wrp->epintr_set, epmask);
	musb_writel(reg_base, wrp->coreintr_set, coremask);
	/*
	 * start polling for runtime PM active and idle,
	 * and for ID change in dual-role idle mode.
	 */
	if (musb->xceiv->otg->state == OTG_STATE_B_IDLE)
		dsps_mod_timer(glue, -1);
}

/*
 * dsps_musb_disable - disable HDRC and flush interrupts
 */
static void dsps_musb_disable(struct musb *musb)
{
	struct device *dev = musb->controller;
	struct dsps_glue *glue = dev_get_drvdata(dev->parent);
	const struct dsps_musb_wrapper *wrp = glue->wrp;
	void __iomem *reg_base = musb->ctrl_base;

	musb_writel(reg_base, wrp->coreintr_clear, wrp->usb_bitmap);
	musb_writel(reg_base, wrp->epintr_clear,
			 wrp->txep_bitmap | wrp->rxep_bitmap);
	del_timer_sync(&musb->dev_timer);
}

/* Caller must take musb->lock */
static int dsps_check_status(struct musb *musb, void *unused)
{
	void __iomem *mregs = musb->mregs;
	struct device *dev = musb->controller;
	struct dsps_glue *glue = dev_get_drvdata(dev->parent);
	const struct dsps_musb_wrapper *wrp = glue->wrp;
	u8 devctl;
	int skip_session = 0;

	if (glue->vbus_irq)
		del_timer(&musb->dev_timer);

	/*
	 * We poll because DSPS IP's won't expose several OTG-critical
	 * status change events (from the transceiver) otherwise.
	 */
	devctl = musb_readb(mregs, MUSB_DEVCTL);
	dev_dbg(musb->controller, "Poll devctl %02x (%s)\n", devctl,
				usb_otg_state_string(musb->xceiv->otg->state));

	switch (musb->xceiv->otg->state) {
	case OTG_STATE_A_WAIT_VRISE:
		if (musb->port_mode == MUSB_HOST) {
			musb->xceiv->otg->state = OTG_STATE_A_WAIT_BCON;
			dsps_mod_timer_optional(glue);
			break;
		}
		fallthrough;

	case OTG_STATE_A_WAIT_BCON:
		/* keep VBUS on for host-only mode */
		if (musb->port_mode == MUSB_HOST) {
			dsps_mod_timer_optional(glue);
			break;
		}
		musb_writeb(musb->mregs, MUSB_DEVCTL, 0);
		skip_session = 1;
		fallthrough;

	case OTG_STATE_A_IDLE:
	case OTG_STATE_B_IDLE:
		if (!glue->vbus_irq) {
			if (devctl & MUSB_DEVCTL_BDEVICE) {
				musb->xceiv->otg->state = OTG_STATE_B_IDLE;
				MUSB_DEV_MODE(musb);
			} else {
				musb->xceiv->otg->state = OTG_STATE_A_IDLE;
				MUSB_HST_MODE(musb);
			}

			if (musb->port_mode == MUSB_PERIPHERAL)
				skip_session = 1;

			if (!(devctl & MUSB_DEVCTL_SESSION) && !skip_session)
				musb_writeb(mregs, MUSB_DEVCTL,
					    MUSB_DEVCTL_SESSION);
		}
		dsps_mod_timer_optional(glue);
		break;
	case OTG_STATE_A_WAIT_VFALL:
		musb->xceiv->otg->state = OTG_STATE_A_WAIT_VRISE;
		musb_writel(musb->ctrl_base, wrp->coreintr_set,
			    MUSB_INTR_VBUSERROR << wrp->usb_shift);
		break;
	default:
		break;
	}

	return 0;
}

static void otg_timer(struct timer_list *t)
{
	struct musb *musb = from_timer(musb, t, dev_timer);
	struct device *dev = musb->controller;
	unsigned long flags;
	int err;

	err = pm_runtime_get(dev);
	if ((err != -EINPROGRESS) && err < 0) {
		dev_err(dev, "Poll could not pm_runtime_get: %i\n", err);
		pm_runtime_put_noidle(dev);

		return;
	}

	spin_lock_irqsave(&musb->lock, flags);
	err = musb_queue_resume_work(musb, dsps_check_status, NULL);
	if (err < 0)
		dev_err(dev, "%s resume work: %i\n", __func__, err);
	spin_unlock_irqrestore(&musb->lock, flags);
	pm_runtime_mark_last_busy(dev);
	pm_runtime_put_autosuspend(dev);
}

static void dsps_musb_clear_ep_rxintr(struct musb *musb, int epnum)
{
	u32 epintr;
	struct dsps_glue *glue = dev_get_drvdata(musb->controller->parent);
	const struct dsps_musb_wrapper *wrp = glue->wrp;

	/* musb->lock might already been held */
	epintr = (1 << epnum) << wrp->rxep_shift;
	musb_writel(musb->ctrl_base, wrp->epintr_status, epintr);
}

static irqreturn_t dsps_interrupt(int irq, void *hci)
{
	struct musb  *musb = hci;
	void __iomem *reg_base = musb->ctrl_base;
	struct device *dev = musb->controller;
	struct dsps_glue *glue = dev_get_drvdata(dev->parent);
	const struct dsps_musb_wrapper *wrp = glue->wrp;
	unsigned long flags;
	irqreturn_t ret = IRQ_NONE;
	u32 epintr, usbintr;

	spin_lock_irqsave(&musb->lock, flags);

	/* Get endpoint interrupts */
	epintr = musb_readl(reg_base, wrp->epintr_status);
	musb->int_rx = (epintr & wrp->rxep_bitmap) >> wrp->rxep_shift;
	musb->int_tx = (epintr & wrp->txep_bitmap) >> wrp->txep_shift;

	if (epintr)
		musb_writel(reg_base, wrp->epintr_status, epintr);

	/* Get usb core interrupts */
	usbintr = musb_readl(reg_base, wrp->coreintr_status);
	if (!usbintr && !epintr)
		goto out;

	musb->int_usb =	(usbintr & wrp->usb_bitmap) >> wrp->usb_shift;
	if (usbintr)
		musb_writel(reg_base, wrp->coreintr_status, usbintr);

	dev_dbg(musb->controller, "usbintr (%x) epintr(%x)\n",
			usbintr, epintr);

	if (usbintr & ((1 << wrp->drvvbus) << wrp->usb_shift)) {
		int drvvbus = musb_readl(reg_base, wrp->status);
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
			dsps_mod_timer_optional(glue);
			WARNING("VBUS error workaround (delay coming)\n");
		} else if (drvvbus) {
			MUSB_HST_MODE(musb);
			musb->xceiv->otg->state = OTG_STATE_A_WAIT_VRISE;
			dsps_mod_timer_optional(glue);
		} else {
			musb->is_active = 0;
			MUSB_DEV_MODE(musb);
			musb->xceiv->otg->state = OTG_STATE_B_IDLE;
		}

		/* NOTE: this must complete power-on within 100 ms. */
		dev_dbg(musb->controller, "VBUS %s (%s)%s, devctl %02x\n",
				drvvbus ? "on" : "off",
				usb_otg_state_string(musb->xceiv->otg->state),
				err ? " ERROR" : "",
				devctl);
		ret = IRQ_HANDLED;
	}

	if (musb->int_tx || musb->int_rx || musb->int_usb)
		ret |= musb_interrupt(musb);

	/* Poll for ID change and connect */
	switch (musb->xceiv->otg->state) {
	case OTG_STATE_B_IDLE:
	case OTG_STATE_A_WAIT_BCON:
		dsps_mod_timer_optional(glue);
		break;
	default:
		break;
	}

out:
	spin_unlock_irqrestore(&musb->lock, flags);

	return ret;
}

static int dsps_musb_dbg_init(struct musb *musb, struct dsps_glue *glue)
{
	struct dentry *root;
	char buf[128];

	sprintf(buf, "%s.dsps", dev_name(musb->controller));
	root = debugfs_create_dir(buf, usb_debug_root);
	glue->dbgfs_root = root;

	glue->regset.regs = dsps_musb_regs;
	glue->regset.nregs = ARRAY_SIZE(dsps_musb_regs);
	glue->regset.base = musb->ctrl_base;

	debugfs_create_regset32("regdump", S_IRUGO, root, &glue->regset);
	return 0;
}

static int dsps_musb_init(struct musb *musb)
{
	struct device *dev = musb->controller;
	struct dsps_glue *glue = dev_get_drvdata(dev->parent);
	struct platform_device *parent = to_platform_device(dev->parent);
	const struct dsps_musb_wrapper *wrp = glue->wrp;
	void __iomem *reg_base;
	struct resource *r;
	u32 rev, val;
	int ret;

	r = platform_get_resource_byname(parent, IORESOURCE_MEM, "control");
	reg_base = devm_ioremap_resource(dev, r);
	if (IS_ERR(reg_base))
		return PTR_ERR(reg_base);
	musb->ctrl_base = reg_base;

	/* NOP driver needs change if supporting dual instance */
	musb->xceiv = devm_usb_get_phy_by_phandle(dev->parent, "phys", 0);
	if (IS_ERR(musb->xceiv))
		return PTR_ERR(musb->xceiv);

	musb->phy = devm_phy_get(dev->parent, "usb2-phy");

	/* Returns zero if e.g. not clocked */
	rev = musb_readl(reg_base, wrp->revision);
	if (!rev)
		return -ENODEV;

	if (IS_ERR(musb->phy))  {
		musb->phy = NULL;
	} else {
		ret = phy_init(musb->phy);
		if (ret < 0)
			return ret;
		ret = phy_power_on(musb->phy);
		if (ret) {
			phy_exit(musb->phy);
			return ret;
		}
	}

	timer_setup(&musb->dev_timer, otg_timer, 0);

	/* Reset the musb */
	musb_writel(reg_base, wrp->control, (1 << wrp->reset));

	musb->isr = dsps_interrupt;

	/* reset the otgdisable bit, needed for host mode to work */
	val = musb_readl(reg_base, wrp->phy_utmi);
	val &= ~(1 << wrp->otg_disable);
	musb_writel(musb->ctrl_base, wrp->phy_utmi, val);

	/*
	 *  Check whether the dsps version has babble control enabled.
	 * In latest silicon revision the babble control logic is enabled.
	 * If MUSB_BABBLE_CTL returns 0x4 then we have the babble control
	 * logic enabled.
	 */
	val = musb_readb(musb->mregs, MUSB_BABBLE_CTL);
	if (val & MUSB_BABBLE_RCV_DISABLE) {
		glue->sw_babble_enabled = true;
		val |= MUSB_BABBLE_SW_SESSION_CTRL;
		musb_writeb(musb->mregs, MUSB_BABBLE_CTL, val);
	}

	dsps_mod_timer(glue, -1);

	return dsps_musb_dbg_init(musb, glue);
}

static int dsps_musb_exit(struct musb *musb)
{
	struct device *dev = musb->controller;
	struct dsps_glue *glue = dev_get_drvdata(dev->parent);

	del_timer_sync(&musb->dev_timer);
	phy_power_off(musb->phy);
	phy_exit(musb->phy);
	debugfs_remove_recursive(glue->dbgfs_root);

	return 0;
}

static int dsps_musb_set_mode(struct musb *musb, u8 mode)
{
	struct device *dev = musb->controller;
	struct dsps_glue *glue = dev_get_drvdata(dev->parent);
	const struct dsps_musb_wrapper *wrp = glue->wrp;
	void __iomem *ctrl_base = musb->ctrl_base;
	u32 reg;

	reg = musb_readl(ctrl_base, wrp->mode);

	switch (mode) {
	case MUSB_HOST:
		reg &= ~(1 << wrp->iddig);

		/*
		 * if we're setting mode to host-only or device-only, we're
		 * going to ignore whatever the PHY sends us and just force
		 * ID pin status by SW
		 */
		reg |= (1 << wrp->iddig_mux);

		musb_writel(ctrl_base, wrp->mode, reg);
		musb_writel(ctrl_base, wrp->phy_utmi, 0x02);
		break;
	case MUSB_PERIPHERAL:
		reg |= (1 << wrp->iddig);

		/*
		 * if we're setting mode to host-only or device-only, we're
		 * going to ignore whatever the PHY sends us and just force
		 * ID pin status by SW
		 */
		reg |= (1 << wrp->iddig_mux);

		musb_writel(ctrl_base, wrp->mode, reg);
		break;
	case MUSB_OTG:
		musb_writel(ctrl_base, wrp->phy_utmi, 0x02);
		break;
	default:
		dev_err(glue->dev, "unsupported mode %d\n", mode);
		return -EINVAL;
	}

	return 0;
}

static bool dsps_sw_babble_control(struct musb *musb)
{
	u8 babble_ctl;
	bool session_restart =  false;

	babble_ctl = musb_readb(musb->mregs, MUSB_BABBLE_CTL);
	dev_dbg(musb->controller, "babble: MUSB_BABBLE_CTL value %x\n",
		babble_ctl);
	/*
	 * check line monitor flag to check whether babble is
	 * due to noise
	 */
	dev_dbg(musb->controller, "STUCK_J is %s\n",
		babble_ctl & MUSB_BABBLE_STUCK_J ? "set" : "reset");

	if (babble_ctl & MUSB_BABBLE_STUCK_J) {
		int timeout = 10;

		/*
		 * babble is due to noise, then set transmit idle (d7 bit)
		 * to resume normal operation
		 */
		babble_ctl = musb_readb(musb->mregs, MUSB_BABBLE_CTL);
		babble_ctl |= MUSB_BABBLE_FORCE_TXIDLE;
		musb_writeb(musb->mregs, MUSB_BABBLE_CTL, babble_ctl);

		/* wait till line monitor flag cleared */
		dev_dbg(musb->controller, "Set TXIDLE, wait J to clear\n");
		do {
			babble_ctl = musb_readb(musb->mregs, MUSB_BABBLE_CTL);
			udelay(1);
		} while ((babble_ctl & MUSB_BABBLE_STUCK_J) && timeout--);

		/* check whether stuck_at_j bit cleared */
		if (babble_ctl & MUSB_BABBLE_STUCK_J) {
			/*
			 * real babble condition has occurred
			 * restart the controller to start the
			 * session again
			 */
			dev_dbg(musb->controller, "J not cleared, misc (%x)\n",
				babble_ctl);
			session_restart = true;
		}
	} else {
		session_restart = true;
	}

	return session_restart;
}

static int dsps_musb_recover(struct musb *musb)
{
	struct device *dev = musb->controller;
	struct dsps_glue *glue = dev_get_drvdata(dev->parent);
	int session_restart = 0;

	if (glue->sw_babble_enabled)
		session_restart = dsps_sw_babble_control(musb);
	else
		session_restart = 1;

	return session_restart ? 0 : -EPIPE;
}

/* Similar to am35x, dm81xx support only 32-bit read operation */
static void dsps_read_fifo32(struct musb_hw_ep *hw_ep, u16 len, u8 *dst)
{
	void __iomem *fifo = hw_ep->fifo;

	if (len >= 4) {
		ioread32_rep(fifo, dst, len >> 2);
		dst += len & ~0x03;
		len &= 0x03;
	}

	/* Read any remaining 1 to 3 bytes */
	if (len > 0) {
		u32 val = musb_readl(fifo, 0);
		memcpy(dst, &val, len);
	}
}

#ifdef CONFIG_USB_TI_CPPI41_DMA
static void dsps_dma_controller_callback(struct dma_controller *c)
{
	struct musb *musb = c->musb;
	struct dsps_glue *glue = dev_get_drvdata(musb->controller->parent);
	void __iomem *usbss_base = glue->usbss_base;
	u32 status;

	status = musb_readl(usbss_base, USBSS_IRQ_STATUS);
	if (status & USBSS_IRQ_PD_COMP)
		musb_writel(usbss_base, USBSS_IRQ_STATUS, USBSS_IRQ_PD_COMP);
}

static struct dma_controller *
dsps_dma_controller_create(struct musb *musb, void __iomem *base)
{
	struct dma_controller *controller;
	struct dsps_glue *glue = dev_get_drvdata(musb->controller->parent);
	void __iomem *usbss_base = glue->usbss_base;

	controller = cppi41_dma_controller_create(musb, base);
	if (IS_ERR_OR_NULL(controller))
		return controller;

	musb_writel(usbss_base, USBSS_IRQ_ENABLER, USBSS_IRQ_PD_COMP);
	controller->dma_callback = dsps_dma_controller_callback;

	return controller;
}

#ifdef CONFIG_PM_SLEEP
static void dsps_dma_controller_suspend(struct dsps_glue *glue)
{
	void __iomem *usbss_base = glue->usbss_base;

	musb_writel(usbss_base, USBSS_IRQ_CLEARR, USBSS_IRQ_PD_COMP);
}

static void dsps_dma_controller_resume(struct dsps_glue *glue)
{
	void __iomem *usbss_base = glue->usbss_base;

	musb_writel(usbss_base, USBSS_IRQ_ENABLER, USBSS_IRQ_PD_COMP);
}
#endif
#else /* CONFIG_USB_TI_CPPI41_DMA */
#ifdef CONFIG_PM_SLEEP
static void dsps_dma_controller_suspend(struct dsps_glue *glue) {}
static void dsps_dma_controller_resume(struct dsps_glue *glue) {}
#endif
#endif /* CONFIG_USB_TI_CPPI41_DMA */

static struct musb_platform_ops dsps_ops = {
	.quirks		= MUSB_DMA_CPPI41 | MUSB_INDEXED_EP,
	.init		= dsps_musb_init,
	.exit		= dsps_musb_exit,

#ifdef CONFIG_USB_TI_CPPI41_DMA
	.dma_init	= dsps_dma_controller_create,
	.dma_exit	= cppi41_dma_controller_destroy,
#endif
	.enable		= dsps_musb_enable,
	.disable	= dsps_musb_disable,

	.set_mode	= dsps_musb_set_mode,
	.recover	= dsps_musb_recover,
	.clear_ep_rxintr = dsps_musb_clear_ep_rxintr,
};

static u64 musb_dmamask = DMA_BIT_MASK(32);

static int get_int_prop(struct device_node *dn, const char *s)
{
	int ret;
	u32 val;

	ret = of_property_read_u32(dn, s, &val);
	if (ret)
		return 0;
	return val;
}

static int dsps_create_musb_pdev(struct dsps_glue *glue,
		struct platform_device *parent)
{
	struct musb_hdrc_platform_data pdata;
	struct resource	resources[2];
	struct resource	*res;
	struct device *dev = &parent->dev;
	struct musb_hdrc_config	*config;
	struct platform_device *musb;
	struct device_node *dn = parent->dev.of_node;
	int ret, val;

	memset(resources, 0, sizeof(resources));
	res = platform_get_resource_byname(parent, IORESOURCE_MEM, "mc");
	if (!res) {
		dev_err(dev, "failed to get memory.\n");
		return -EINVAL;
	}
	resources[0] = *res;

	ret = platform_get_irq_byname(parent, "mc");
	if (ret < 0)
		return ret;

	resources[1].start = ret;
	resources[1].end = ret;
	resources[1].flags = IORESOURCE_IRQ | irq_get_trigger_type(ret);
	resources[1].name = "mc";

	/* allocate the child platform device */
	musb = platform_device_alloc("musb-hdrc",
			(resources[0].start & 0xFFF) == 0x400 ? 0 : 1);
	if (!musb) {
		dev_err(dev, "failed to allocate musb device\n");
		return -ENOMEM;
	}

	musb->dev.parent		= dev;
	musb->dev.dma_mask		= &musb_dmamask;
	musb->dev.coherent_dma_mask	= musb_dmamask;
	device_set_of_node_from_dev(&musb->dev, &parent->dev);

	glue->musb = musb;

	ret = platform_device_add_resources(musb, resources,
			ARRAY_SIZE(resources));
	if (ret) {
		dev_err(dev, "failed to add resources\n");
		goto err;
	}

	config = devm_kzalloc(&parent->dev, sizeof(*config), GFP_KERNEL);
	if (!config) {
		ret = -ENOMEM;
		goto err;
	}
	pdata.config = config;
	pdata.platform_ops = &dsps_ops;

	config->num_eps = get_int_prop(dn, "mentor,num-eps");
	config->ram_bits = get_int_prop(dn, "mentor,ram-bits");
	config->host_port_deassert_reset_at_resume = 1;
	pdata.mode = musb_get_mode(dev);
	/* DT keeps this entry in mA, musb expects it as per USB spec */
	pdata.power = get_int_prop(dn, "mentor,power") / 2;

	ret = of_property_read_u32(dn, "mentor,multipoint", &val);
	if (!ret && val)
		config->multipoint = true;

	config->maximum_speed = usb_get_maximum_speed(&parent->dev);
	switch (config->maximum_speed) {
	case USB_SPEED_LOW:
	case USB_SPEED_FULL:
		break;
	case USB_SPEED_SUPER:
		dev_warn(dev, "ignore incorrect maximum_speed "
				"(super-speed) setting in dts");
		fallthrough;
	default:
		config->maximum_speed = USB_SPEED_HIGH;
	}

	ret = platform_device_add_data(musb, &pdata, sizeof(pdata));
	if (ret) {
		dev_err(dev, "failed to add platform_data\n");
		goto err;
	}

	ret = platform_device_add(musb);
	if (ret) {
		dev_err(dev, "failed to register musb device\n");
		goto err;
	}
	return 0;

err:
	platform_device_put(musb);
	return ret;
}

static irqreturn_t dsps_vbus_threaded_irq(int irq, void *priv)
{
	struct dsps_glue *glue = priv;
	struct musb *musb = platform_get_drvdata(glue->musb);

	if (!musb)
		return IRQ_NONE;

	dev_dbg(glue->dev, "VBUS interrupt\n");
	dsps_mod_timer(glue, 0);

	return IRQ_HANDLED;
}

static int dsps_setup_optional_vbus_irq(struct platform_device *pdev,
					struct dsps_glue *glue)
{
	int error;

	glue->vbus_irq = platform_get_irq_byname(pdev, "vbus");
	if (glue->vbus_irq == -EPROBE_DEFER)
		return -EPROBE_DEFER;

	if (glue->vbus_irq <= 0) {
		glue->vbus_irq = 0;
		return 0;
	}

	error = devm_request_threaded_irq(glue->dev, glue->vbus_irq,
					  NULL, dsps_vbus_threaded_irq,
					  IRQF_ONESHOT,
					  "vbus", glue);
	if (error) {
		glue->vbus_irq = 0;
		return error;
	}
	dev_dbg(glue->dev, "VBUS irq %i configured\n", glue->vbus_irq);

	return 0;
}

static int dsps_probe(struct platform_device *pdev)
{
	const struct of_device_id *match;
	const struct dsps_musb_wrapper *wrp;
	struct dsps_glue *glue;
	int ret;

	if (!strcmp(pdev->name, "musb-hdrc"))
		return -ENODEV;

	match = of_match_node(musb_dsps_of_match, pdev->dev.of_node);
	if (!match) {
		dev_err(&pdev->dev, "fail to get matching of_match struct\n");
		return -EINVAL;
	}
	wrp = match->data;

	if (of_device_is_compatible(pdev->dev.of_node, "ti,musb-dm816"))
		dsps_ops.read_fifo = dsps_read_fifo32;

	/* allocate glue */
	glue = devm_kzalloc(&pdev->dev, sizeof(*glue), GFP_KERNEL);
	if (!glue)
		return -ENOMEM;

	glue->dev = &pdev->dev;
	glue->wrp = wrp;
	glue->usbss_base = of_iomap(pdev->dev.parent->of_node, 0);
	if (!glue->usbss_base)
		return -ENXIO;

	platform_set_drvdata(pdev, glue);
	pm_runtime_enable(&pdev->dev);
	ret = dsps_create_musb_pdev(glue, pdev);
	if (ret)
		goto err;

	if (usb_get_dr_mode(&pdev->dev) == USB_DR_MODE_PERIPHERAL) {
		ret = dsps_setup_optional_vbus_irq(pdev, glue);
		if (ret)
			goto unregister_pdev;
	}

	return 0;

unregister_pdev:
	platform_device_unregister(glue->musb);
err:
	pm_runtime_disable(&pdev->dev);
	iounmap(glue->usbss_base);
	return ret;
}

static void dsps_remove(struct platform_device *pdev)
{
	struct dsps_glue *glue = platform_get_drvdata(pdev);

	platform_device_unregister(glue->musb);

	pm_runtime_disable(&pdev->dev);
	iounmap(glue->usbss_base);
}

static const struct dsps_musb_wrapper am33xx_driver_data = {
	.revision		= 0x00,
	.control		= 0x14,
	.status			= 0x18,
	.epintr_set		= 0x38,
	.epintr_clear		= 0x40,
	.epintr_status		= 0x30,
	.coreintr_set		= 0x3c,
	.coreintr_clear		= 0x44,
	.coreintr_status	= 0x34,
	.phy_utmi		= 0xe0,
	.mode			= 0xe8,
	.tx_mode		= 0x70,
	.rx_mode		= 0x74,
	.reset			= 0,
	.otg_disable		= 21,
	.iddig			= 8,
	.iddig_mux		= 7,
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
	.poll_timeout		= 2000, /* ms */
};

static const struct of_device_id musb_dsps_of_match[] = {
	{ .compatible = "ti,musb-am33xx",
		.data = &am33xx_driver_data, },
	{ .compatible = "ti,musb-dm816",
		.data = &am33xx_driver_data, },
	{  },
};
MODULE_DEVICE_TABLE(of, musb_dsps_of_match);

#ifdef CONFIG_PM_SLEEP
static int dsps_suspend(struct device *dev)
{
	struct dsps_glue *glue = dev_get_drvdata(dev);
	const struct dsps_musb_wrapper *wrp = glue->wrp;
	struct musb *musb = platform_get_drvdata(glue->musb);
	void __iomem *mbase;
	int ret;

	if (!musb)
		/* This can happen if the musb device is in -EPROBE_DEFER */
		return 0;

	ret = pm_runtime_get_sync(dev);
	if (ret < 0) {
		pm_runtime_put_noidle(dev);
		return ret;
	}

	del_timer_sync(&musb->dev_timer);

	mbase = musb->ctrl_base;
	glue->context.control = musb_readl(mbase, wrp->control);
	glue->context.epintr = musb_readl(mbase, wrp->epintr_set);
	glue->context.coreintr = musb_readl(mbase, wrp->coreintr_set);
	glue->context.phy_utmi = musb_readl(mbase, wrp->phy_utmi);
	glue->context.mode = musb_readl(mbase, wrp->mode);
	glue->context.tx_mode = musb_readl(mbase, wrp->tx_mode);
	glue->context.rx_mode = musb_readl(mbase, wrp->rx_mode);

	dsps_dma_controller_suspend(glue);

	return 0;
}

static int dsps_resume(struct device *dev)
{
	struct dsps_glue *glue = dev_get_drvdata(dev);
	const struct dsps_musb_wrapper *wrp = glue->wrp;
	struct musb *musb = platform_get_drvdata(glue->musb);
	void __iomem *mbase;

	if (!musb)
		return 0;

	dsps_dma_controller_resume(glue);

	mbase = musb->ctrl_base;
	musb_writel(mbase, wrp->control, glue->context.control);
	musb_writel(mbase, wrp->epintr_set, glue->context.epintr);
	musb_writel(mbase, wrp->coreintr_set, glue->context.coreintr);
	musb_writel(mbase, wrp->phy_utmi, glue->context.phy_utmi);
	musb_writel(mbase, wrp->mode, glue->context.mode);
	musb_writel(mbase, wrp->tx_mode, glue->context.tx_mode);
	musb_writel(mbase, wrp->rx_mode, glue->context.rx_mode);
	if (musb->xceiv->otg->state == OTG_STATE_B_IDLE &&
	    musb->port_mode == MUSB_OTG)
		dsps_mod_timer(glue, -1);

	pm_runtime_put(dev);

	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(dsps_pm_ops, dsps_suspend, dsps_resume);

static struct platform_driver dsps_usbss_driver = {
	.probe		= dsps_probe,
	.remove_new     = dsps_remove,
	.driver         = {
		.name   = "musb-dsps",
		.pm	= &dsps_pm_ops,
		.of_match_table	= musb_dsps_of_match,
	},
};

MODULE_DESCRIPTION("TI DSPS MUSB Glue Layer");
MODULE_AUTHOR("Ravi B <ravibabu@ti.com>");
MODULE_AUTHOR("Ajay Kumar Gupta <ajay.gupta@ti.com>");
MODULE_LICENSE("GPL v2");

module_platform_driver(dsps_usbss_driver);
