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

#include <linux/io.h>
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

/**
 * avoid using musb_readx()/musb_writex() as glue layer should not be
 * dependent on musb core layer symbols.
 */
static inline u8 dsps_readb(const void __iomem *addr, unsigned offset)
{
	return __raw_readb(addr + offset);
}

static inline u32 dsps_readl(const void __iomem *addr, unsigned offset)
{
	return __raw_readl(addr + offset);
}

static inline void dsps_writeb(void __iomem *addr, unsigned offset, u8 data)
{
	__raw_writeb(data, addr + offset);
}

static inline void dsps_writel(void __iomem *addr, unsigned offset, u32 data)
{
	__raw_writel(data, addr + offset);
}

/**
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

/**
 * DSPS glue structure.
 */
struct dsps_glue {
	struct device *dev;
	struct platform_device *musb;	/* child musb pdev */
	const struct dsps_musb_wrapper *wrp; /* wrapper register offsets */
	struct timer_list timer;	/* otg_workaround timer */
	unsigned long last_timer;    /* last timer data for each instance */
	bool sw_babble_enabled;

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

static void dsps_musb_try_idle(struct musb *musb, unsigned long timeout)
{
	struct device *dev = musb->controller;
	struct dsps_glue *glue = dev_get_drvdata(dev->parent);

	if (timeout == 0)
		timeout = jiffies + msecs_to_jiffies(3);

	/* Never idle if active, or when VBUS timeout is not set as host */
	if (musb->is_active || (musb->a_wait_bcon == 0 &&
			musb->xceiv->otg->state == OTG_STATE_A_WAIT_BCON)) {
		dev_dbg(musb->controller, "%s active, deleting timer\n",
				usb_otg_state_string(musb->xceiv->otg->state));
		del_timer(&glue->timer);
		glue->last_timer = jiffies;
		return;
	}
	if (musb->port_mode != MUSB_PORT_MODE_DUAL_ROLE)
		return;

	if (!musb->g.dev.driver)
		return;

	if (time_after(glue->last_timer, timeout) &&
				timer_pending(&glue->timer)) {
		dev_dbg(musb->controller,
			"Longer idle timer already pending, ignoring...\n");
		return;
	}
	glue->last_timer = timeout;

	dev_dbg(musb->controller, "%s inactive, starting idle timer for %u ms\n",
		usb_otg_state_string(musb->xceiv->otg->state),
			jiffies_to_msecs(timeout - jiffies));
	mod_timer(&glue->timer, timeout);
}

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
	/* start polling for ID change in dual-role idle mode */
	if (musb->xceiv->otg->state == OTG_STATE_B_IDLE &&
			musb->port_mode == MUSB_PORT_MODE_DUAL_ROLE)
		mod_timer(&glue->timer, jiffies +
				msecs_to_jiffies(wrp->poll_timeout));
	dsps_musb_try_idle(musb, 0);
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
}

static void otg_timer(unsigned long _musb)
{
	struct musb *musb = (void *)_musb;
	void __iomem *mregs = musb->mregs;
	struct device *dev = musb->controller;
	struct dsps_glue *glue = dev_get_drvdata(dev->parent);
	const struct dsps_musb_wrapper *wrp = glue->wrp;
	u8 devctl;
	unsigned long flags;
	int skip_session = 0;

	/*
	 * We poll because DSPS IP's won't expose several OTG-critical
	 * status change events (from the transceiver) otherwise.
	 */
	devctl = dsps_readb(mregs, MUSB_DEVCTL);
	dev_dbg(musb->controller, "Poll devctl %02x (%s)\n", devctl,
				usb_otg_state_string(musb->xceiv->otg->state));

	spin_lock_irqsave(&musb->lock, flags);
	switch (musb->xceiv->otg->state) {
	case OTG_STATE_A_WAIT_BCON:
		dsps_writeb(musb->mregs, MUSB_DEVCTL, 0);
		skip_session = 1;
		/* fall */

	case OTG_STATE_A_IDLE:
	case OTG_STATE_B_IDLE:
		if (devctl & MUSB_DEVCTL_BDEVICE) {
			musb->xceiv->otg->state = OTG_STATE_B_IDLE;
			MUSB_DEV_MODE(musb);
		} else {
			musb->xceiv->otg->state = OTG_STATE_A_IDLE;
			MUSB_HST_MODE(musb);
		}
		if (!(devctl & MUSB_DEVCTL_SESSION) && !skip_session)
			dsps_writeb(mregs, MUSB_DEVCTL, MUSB_DEVCTL_SESSION);
		mod_timer(&glue->timer, jiffies +
				msecs_to_jiffies(wrp->poll_timeout));
		break;
	case OTG_STATE_A_WAIT_VFALL:
		musb->xceiv->otg->state = OTG_STATE_A_WAIT_VRISE;
		dsps_writel(musb->ctrl_base, wrp->coreintr_set,
			    MUSB_INTR_VBUSERROR << wrp->usb_shift);
		break;
	default:
		break;
	}
	spin_unlock_irqrestore(&musb->lock, flags);
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
	epintr = dsps_readl(reg_base, wrp->epintr_status);
	musb->int_rx = (epintr & wrp->rxep_bitmap) >> wrp->rxep_shift;
	musb->int_tx = (epintr & wrp->txep_bitmap) >> wrp->txep_shift;

	if (epintr)
		dsps_writel(reg_base, wrp->epintr_status, epintr);

	/* Get usb core interrupts */
	usbintr = dsps_readl(reg_base, wrp->coreintr_status);
	if (!usbintr && !epintr)
		goto out;

	musb->int_usb =	(usbintr & wrp->usb_bitmap) >> wrp->usb_shift;
	if (usbintr)
		dsps_writel(reg_base, wrp->coreintr_status, usbintr);

	dev_dbg(musb->controller, "usbintr (%x) epintr(%x)\n",
			usbintr, epintr);

	if (usbintr & ((1 << wrp->drvvbus) << wrp->usb_shift)) {
		int drvvbus = dsps_readl(reg_base, wrp->status);
		void __iomem *mregs = musb->mregs;
		u8 devctl = dsps_readb(mregs, MUSB_DEVCTL);
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
			mod_timer(&glue->timer, jiffies +
					msecs_to_jiffies(wrp->poll_timeout));
			WARNING("VBUS error workaround (delay coming)\n");
		} else if (drvvbus) {
			MUSB_HST_MODE(musb);
			musb->xceiv->otg->default_a = 1;
			musb->xceiv->otg->state = OTG_STATE_A_WAIT_VRISE;
			del_timer(&glue->timer);
		} else {
			musb->is_active = 0;
			MUSB_DEV_MODE(musb);
			musb->xceiv->otg->default_a = 0;
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

	/* Poll for ID change in OTG port mode */
	if (musb->xceiv->otg->state == OTG_STATE_B_IDLE &&
			musb->port_mode == MUSB_PORT_MODE_DUAL_ROLE)
		mod_timer(&glue->timer, jiffies +
				msecs_to_jiffies(wrp->poll_timeout));
out:
	spin_unlock_irqrestore(&musb->lock, flags);

	return ret;
}

static int dsps_musb_dbg_init(struct musb *musb, struct dsps_glue *glue)
{
	struct dentry *root;
	struct dentry *file;
	char buf[128];

	sprintf(buf, "%s.dsps", dev_name(musb->controller));
	root = debugfs_create_dir(buf, NULL);
	if (!root)
		return -ENOMEM;
	glue->dbgfs_root = root;

	glue->regset.regs = dsps_musb_regs;
	glue->regset.nregs = ARRAY_SIZE(dsps_musb_regs);
	glue->regset.base = musb->ctrl_base;

	file = debugfs_create_regset32("regdump", S_IRUGO, root, &glue->regset);
	if (!file) {
		debugfs_remove_recursive(root);
		return -ENOMEM;
	}
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
	rev = dsps_readl(reg_base, wrp->revision);
	if (!rev)
		return -ENODEV;

	usb_phy_init(musb->xceiv);
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

	setup_timer(&glue->timer, otg_timer, (unsigned long) musb);

	/* Reset the musb */
	dsps_writel(reg_base, wrp->control, (1 << wrp->reset));

	musb->isr = dsps_interrupt;

	/* reset the otgdisable bit, needed for host mode to work */
	val = dsps_readl(reg_base, wrp->phy_utmi);
	val &= ~(1 << wrp->otg_disable);
	dsps_writel(musb->ctrl_base, wrp->phy_utmi, val);

	/*
	 *  Check whether the dsps version has babble control enabled.
	 * In latest silicon revision the babble control logic is enabled.
	 * If MUSB_BABBLE_CTL returns 0x4 then we have the babble control
	 * logic enabled.
	 */
	val = dsps_readb(musb->mregs, MUSB_BABBLE_CTL);
	if (val & MUSB_BABBLE_RCV_DISABLE) {
		glue->sw_babble_enabled = true;
		val |= MUSB_BABBLE_SW_SESSION_CTRL;
		dsps_writeb(musb->mregs, MUSB_BABBLE_CTL, val);
	}

	ret = dsps_musb_dbg_init(musb, glue);
	if (ret)
		return ret;

	return 0;
}

static int dsps_musb_exit(struct musb *musb)
{
	struct device *dev = musb->controller;
	struct dsps_glue *glue = dev_get_drvdata(dev->parent);

	del_timer_sync(&glue->timer);
	usb_phy_shutdown(musb->xceiv);
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

	reg = dsps_readl(ctrl_base, wrp->mode);

	switch (mode) {
	case MUSB_HOST:
		reg &= ~(1 << wrp->iddig);

		/*
		 * if we're setting mode to host-only or device-only, we're
		 * going to ignore whatever the PHY sends us and just force
		 * ID pin status by SW
		 */
		reg |= (1 << wrp->iddig_mux);

		dsps_writel(ctrl_base, wrp->mode, reg);
		dsps_writel(ctrl_base, wrp->phy_utmi, 0x02);
		break;
	case MUSB_PERIPHERAL:
		reg |= (1 << wrp->iddig);

		/*
		 * if we're setting mode to host-only or device-only, we're
		 * going to ignore whatever the PHY sends us and just force
		 * ID pin status by SW
		 */
		reg |= (1 << wrp->iddig_mux);

		dsps_writel(ctrl_base, wrp->mode, reg);
		break;
	case MUSB_OTG:
		dsps_writel(ctrl_base, wrp->phy_utmi, 0x02);
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

	babble_ctl = dsps_readb(musb->mregs, MUSB_BABBLE_CTL);
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
		babble_ctl = dsps_readb(musb->mregs, MUSB_BABBLE_CTL);
		babble_ctl |= MUSB_BABBLE_FORCE_TXIDLE;
		dsps_writeb(musb->mregs, MUSB_BABBLE_CTL, babble_ctl);

		/* wait till line monitor flag cleared */
		dev_dbg(musb->controller, "Set TXIDLE, wait J to clear\n");
		do {
			babble_ctl = dsps_readb(musb->mregs, MUSB_BABBLE_CTL);
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

static struct musb_platform_ops dsps_ops = {
	.quirks		= MUSB_INDEXED_EP,
	.init		= dsps_musb_init,
	.exit		= dsps_musb_exit,

	.enable		= dsps_musb_enable,
	.disable	= dsps_musb_disable,

	.try_idle	= dsps_musb_try_idle,
	.set_mode	= dsps_musb_set_mode,
	.recover	= dsps_musb_recover,
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

static int get_musb_port_mode(struct device *dev)
{
	enum usb_dr_mode mode;

	mode = of_usb_get_dr_mode(dev->of_node);
	switch (mode) {
	case USB_DR_MODE_HOST:
		return MUSB_PORT_MODE_HOST;

	case USB_DR_MODE_PERIPHERAL:
		return MUSB_PORT_MODE_GADGET;

	case USB_DR_MODE_UNKNOWN:
	case USB_DR_MODE_OTG:
	default:
		return MUSB_PORT_MODE_DUAL_ROLE;
	}
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

	res = platform_get_resource_byname(parent, IORESOURCE_IRQ, "mc");
	if (!res) {
		dev_err(dev, "failed to get irq.\n");
		return -EINVAL;
	}
	resources[1] = *res;

	/* allocate the child platform device */
	musb = platform_device_alloc("musb-hdrc", PLATFORM_DEVID_AUTO);
	if (!musb) {
		dev_err(dev, "failed to allocate musb device\n");
		return -ENOMEM;
	}

	musb->dev.parent		= dev;
	musb->dev.dma_mask		= &musb_dmamask;
	musb->dev.coherent_dma_mask	= musb_dmamask;

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
	pdata.mode = get_musb_port_mode(dev);
	/* DT keeps this entry in mA, musb expects it as per USB spec */
	pdata.power = get_int_prop(dn, "mentor,power") / 2;

	ret = of_property_read_u32(dn, "mentor,multipoint", &val);
	if (!ret && val)
		config->multipoint = true;

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

	platform_set_drvdata(pdev, glue);
	pm_runtime_enable(&pdev->dev);

	ret = pm_runtime_get_sync(&pdev->dev);
	if (ret < 0) {
		dev_err(&pdev->dev, "pm_runtime_get_sync FAILED");
		goto err2;
	}

	ret = dsps_create_musb_pdev(glue, pdev);
	if (ret)
		goto err3;

	return 0;

err3:
	pm_runtime_put(&pdev->dev);
err2:
	pm_runtime_disable(&pdev->dev);
	return ret;
}

static int dsps_remove(struct platform_device *pdev)
{
	struct dsps_glue *glue = platform_get_drvdata(pdev);

	platform_device_unregister(glue->musb);

	/* disable usbss clocks */
	pm_runtime_put(&pdev->dev);
	pm_runtime_disable(&pdev->dev);

	return 0;
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

	del_timer_sync(&glue->timer);

	if (!musb)
		/* This can happen if the musb device is in -EPROBE_DEFER */
		return 0;

	mbase = musb->ctrl_base;
	glue->context.control = dsps_readl(mbase, wrp->control);
	glue->context.epintr = dsps_readl(mbase, wrp->epintr_set);
	glue->context.coreintr = dsps_readl(mbase, wrp->coreintr_set);
	glue->context.phy_utmi = dsps_readl(mbase, wrp->phy_utmi);
	glue->context.mode = dsps_readl(mbase, wrp->mode);
	glue->context.tx_mode = dsps_readl(mbase, wrp->tx_mode);
	glue->context.rx_mode = dsps_readl(mbase, wrp->rx_mode);

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

	mbase = musb->ctrl_base;
	dsps_writel(mbase, wrp->control, glue->context.control);
	dsps_writel(mbase, wrp->epintr_set, glue->context.epintr);
	dsps_writel(mbase, wrp->coreintr_set, glue->context.coreintr);
	dsps_writel(mbase, wrp->phy_utmi, glue->context.phy_utmi);
	dsps_writel(mbase, wrp->mode, glue->context.mode);
	dsps_writel(mbase, wrp->tx_mode, glue->context.tx_mode);
	dsps_writel(mbase, wrp->rx_mode, glue->context.rx_mode);
	if (musb->xceiv->otg->state == OTG_STATE_B_IDLE &&
	    musb->port_mode == MUSB_PORT_MODE_DUAL_ROLE)
		mod_timer(&glue->timer, jiffies +
				msecs_to_jiffies(wrp->poll_timeout));

	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(dsps_pm_ops, dsps_suspend, dsps_resume);

static struct platform_driver dsps_usbss_driver = {
	.probe		= dsps_probe,
	.remove         = dsps_remove,
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
