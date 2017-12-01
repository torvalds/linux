/*
 * Allwinner sun4i MUSB Glue Layer
 *
 * Copyright (C) 2015 Hans de Goede <hdegoede@redhat.com>
 *
 * Based on code from
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
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
 */

#include <linux/clk.h>
#include <linux/err.h>
#include <linux/extcon.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/phy/phy-sun4i-usb.h>
#include <linux/platform_device.h>
#include <linux/reset.h>
#include <linux/soc/sunxi/sunxi_sram.h>
#include <linux/usb/musb.h>
#include <linux/usb/of.h>
#include <linux/usb/usb_phy_generic.h>
#include <linux/workqueue.h>
#include "musb_core.h"

/*
 * Register offsets, note sunxi musb has a different layout then most
 * musb implementations, we translate the layout in musb_readb & friends.
 */
#define SUNXI_MUSB_POWER			0x0040
#define SUNXI_MUSB_DEVCTL			0x0041
#define SUNXI_MUSB_INDEX			0x0042
#define SUNXI_MUSB_VEND0			0x0043
#define SUNXI_MUSB_INTRTX			0x0044
#define SUNXI_MUSB_INTRRX			0x0046
#define SUNXI_MUSB_INTRTXE			0x0048
#define SUNXI_MUSB_INTRRXE			0x004a
#define SUNXI_MUSB_INTRUSB			0x004c
#define SUNXI_MUSB_INTRUSBE			0x0050
#define SUNXI_MUSB_FRAME			0x0054
#define SUNXI_MUSB_TXFIFOSZ			0x0090
#define SUNXI_MUSB_TXFIFOADD			0x0092
#define SUNXI_MUSB_RXFIFOSZ			0x0094
#define SUNXI_MUSB_RXFIFOADD			0x0096
#define SUNXI_MUSB_FADDR			0x0098
#define SUNXI_MUSB_TXFUNCADDR			0x0098
#define SUNXI_MUSB_TXHUBADDR			0x009a
#define SUNXI_MUSB_TXHUBPORT			0x009b
#define SUNXI_MUSB_RXFUNCADDR			0x009c
#define SUNXI_MUSB_RXHUBADDR			0x009e
#define SUNXI_MUSB_RXHUBPORT			0x009f
#define SUNXI_MUSB_CONFIGDATA			0x00c0

/* VEND0 bits */
#define SUNXI_MUSB_VEND0_PIO_MODE		0

/* flags */
#define SUNXI_MUSB_FL_ENABLED			0
#define SUNXI_MUSB_FL_HOSTMODE			1
#define SUNXI_MUSB_FL_HOSTMODE_PEND		2
#define SUNXI_MUSB_FL_VBUS_ON			3
#define SUNXI_MUSB_FL_PHY_ON			4
#define SUNXI_MUSB_FL_HAS_SRAM			5
#define SUNXI_MUSB_FL_HAS_RESET			6
#define SUNXI_MUSB_FL_NO_CONFIGDATA		7

/* Our read/write methods need access and do not get passed in a musb ref :| */
static struct musb *sunxi_musb;

struct sunxi_glue {
	struct device		*dev;
	struct platform_device	*musb;
	struct clk		*clk;
	struct reset_control	*rst;
	struct phy		*phy;
	struct platform_device	*usb_phy;
	struct usb_phy		*xceiv;
	unsigned long		flags;
	struct work_struct	work;
	struct extcon_dev	*extcon;
	struct notifier_block	host_nb;
};

/* phy_power_on / off may sleep, so we use a workqueue  */
static void sunxi_musb_work(struct work_struct *work)
{
	struct sunxi_glue *glue = container_of(work, struct sunxi_glue, work);
	bool vbus_on, phy_on;

	if (!test_bit(SUNXI_MUSB_FL_ENABLED, &glue->flags))
		return;

	if (test_and_clear_bit(SUNXI_MUSB_FL_HOSTMODE_PEND, &glue->flags)) {
		struct musb *musb = platform_get_drvdata(glue->musb);
		unsigned long flags;
		u8 devctl;

		spin_lock_irqsave(&musb->lock, flags);

		devctl = readb(musb->mregs + SUNXI_MUSB_DEVCTL);
		if (test_bit(SUNXI_MUSB_FL_HOSTMODE, &glue->flags)) {
			set_bit(SUNXI_MUSB_FL_VBUS_ON, &glue->flags);
			musb->xceiv->otg->default_a = 1;
			musb->xceiv->otg->state = OTG_STATE_A_IDLE;
			MUSB_HST_MODE(musb);
			devctl |= MUSB_DEVCTL_SESSION;
		} else {
			clear_bit(SUNXI_MUSB_FL_VBUS_ON, &glue->flags);
			musb->xceiv->otg->default_a = 0;
			musb->xceiv->otg->state = OTG_STATE_B_IDLE;
			MUSB_DEV_MODE(musb);
			devctl &= ~MUSB_DEVCTL_SESSION;
		}
		writeb(devctl, musb->mregs + SUNXI_MUSB_DEVCTL);

		spin_unlock_irqrestore(&musb->lock, flags);
	}

	vbus_on = test_bit(SUNXI_MUSB_FL_VBUS_ON, &glue->flags);
	phy_on = test_bit(SUNXI_MUSB_FL_PHY_ON, &glue->flags);

	if (phy_on != vbus_on) {
		if (vbus_on) {
			phy_power_on(glue->phy);
			set_bit(SUNXI_MUSB_FL_PHY_ON, &glue->flags);
		} else {
			phy_power_off(glue->phy);
			clear_bit(SUNXI_MUSB_FL_PHY_ON, &glue->flags);
		}
	}
}

static void sunxi_musb_set_vbus(struct musb *musb, int is_on)
{
	struct sunxi_glue *glue = dev_get_drvdata(musb->controller->parent);

	if (is_on)
		set_bit(SUNXI_MUSB_FL_VBUS_ON, &glue->flags);
	else
		clear_bit(SUNXI_MUSB_FL_VBUS_ON, &glue->flags);

	schedule_work(&glue->work);
}

static void sunxi_musb_pre_root_reset_end(struct musb *musb)
{
	struct sunxi_glue *glue = dev_get_drvdata(musb->controller->parent);

	sun4i_usb_phy_set_squelch_detect(glue->phy, false);
}

static void sunxi_musb_post_root_reset_end(struct musb *musb)
{
	struct sunxi_glue *glue = dev_get_drvdata(musb->controller->parent);

	sun4i_usb_phy_set_squelch_detect(glue->phy, true);
}

static irqreturn_t sunxi_musb_interrupt(int irq, void *__hci)
{
	struct musb *musb = __hci;
	unsigned long flags;

	spin_lock_irqsave(&musb->lock, flags);

	musb->int_usb = readb(musb->mregs + SUNXI_MUSB_INTRUSB);
	if (musb->int_usb)
		writeb(musb->int_usb, musb->mregs + SUNXI_MUSB_INTRUSB);

	/*
	 * sunxi musb often signals babble on low / full speed device
	 * disconnect, without ever raising MUSB_INTR_DISCONNECT, since
	 * normally babble never happens treat it as disconnect.
	 */
	if ((musb->int_usb & MUSB_INTR_BABBLE) && is_host_active(musb)) {
		musb->int_usb &= ~MUSB_INTR_BABBLE;
		musb->int_usb |= MUSB_INTR_DISCONNECT;
	}

	if ((musb->int_usb & MUSB_INTR_RESET) && !is_host_active(musb)) {
		/* ep0 FADDR must be 0 when (re)entering peripheral mode */
		musb_ep_select(musb->mregs, 0);
		musb_writeb(musb->mregs, MUSB_FADDR, 0);
	}

	musb->int_tx = readw(musb->mregs + SUNXI_MUSB_INTRTX);
	if (musb->int_tx)
		writew(musb->int_tx, musb->mregs + SUNXI_MUSB_INTRTX);

	musb->int_rx = readw(musb->mregs + SUNXI_MUSB_INTRRX);
	if (musb->int_rx)
		writew(musb->int_rx, musb->mregs + SUNXI_MUSB_INTRRX);

	musb_interrupt(musb);

	spin_unlock_irqrestore(&musb->lock, flags);

	return IRQ_HANDLED;
}

static int sunxi_musb_host_notifier(struct notifier_block *nb,
				    unsigned long event, void *ptr)
{
	struct sunxi_glue *glue = container_of(nb, struct sunxi_glue, host_nb);

	if (event)
		set_bit(SUNXI_MUSB_FL_HOSTMODE, &glue->flags);
	else
		clear_bit(SUNXI_MUSB_FL_HOSTMODE, &glue->flags);

	set_bit(SUNXI_MUSB_FL_HOSTMODE_PEND, &glue->flags);
	schedule_work(&glue->work);

	return NOTIFY_DONE;
}

static int sunxi_musb_init(struct musb *musb)
{
	struct sunxi_glue *glue = dev_get_drvdata(musb->controller->parent);
	int ret;

	sunxi_musb = musb;
	musb->phy = glue->phy;
	musb->xceiv = glue->xceiv;

	if (test_bit(SUNXI_MUSB_FL_HAS_SRAM, &glue->flags)) {
		ret = sunxi_sram_claim(musb->controller->parent);
		if (ret)
			return ret;
	}

	ret = clk_prepare_enable(glue->clk);
	if (ret)
		goto error_sram_release;

	if (test_bit(SUNXI_MUSB_FL_HAS_RESET, &glue->flags)) {
		ret = reset_control_deassert(glue->rst);
		if (ret)
			goto error_clk_disable;
	}

	writeb(SUNXI_MUSB_VEND0_PIO_MODE, musb->mregs + SUNXI_MUSB_VEND0);

	/* Register notifier before calling phy_init() */
	if (musb->port_mode == MUSB_PORT_MODE_DUAL_ROLE) {
		ret = extcon_register_notifier(glue->extcon, EXTCON_USB_HOST,
					       &glue->host_nb);
		if (ret)
			goto error_reset_assert;
	}

	ret = phy_init(glue->phy);
	if (ret)
		goto error_unregister_notifier;

	if (musb->port_mode == MUSB_PORT_MODE_HOST) {
		ret = phy_power_on(glue->phy);
		if (ret)
			goto error_phy_exit;
		set_bit(SUNXI_MUSB_FL_PHY_ON, &glue->flags);
		/* Stop musb work from turning vbus off again */
		set_bit(SUNXI_MUSB_FL_VBUS_ON, &glue->flags);
	}

	musb->isr = sunxi_musb_interrupt;

	/* Stop the musb-core from doing runtime pm (not supported on sunxi) */
	pm_runtime_get(musb->controller);

	return 0;

error_phy_exit:
	phy_exit(glue->phy);
error_unregister_notifier:
	if (musb->port_mode == MUSB_PORT_MODE_DUAL_ROLE)
		extcon_unregister_notifier(glue->extcon, EXTCON_USB_HOST,
					   &glue->host_nb);
error_reset_assert:
	if (test_bit(SUNXI_MUSB_FL_HAS_RESET, &glue->flags))
		reset_control_assert(glue->rst);
error_clk_disable:
	clk_disable_unprepare(glue->clk);
error_sram_release:
	if (test_bit(SUNXI_MUSB_FL_HAS_SRAM, &glue->flags))
		sunxi_sram_release(musb->controller->parent);
	return ret;
}

static int sunxi_musb_exit(struct musb *musb)
{
	struct sunxi_glue *glue = dev_get_drvdata(musb->controller->parent);

	pm_runtime_put(musb->controller);

	cancel_work_sync(&glue->work);
	if (test_bit(SUNXI_MUSB_FL_PHY_ON, &glue->flags))
		phy_power_off(glue->phy);

	phy_exit(glue->phy);

	if (musb->port_mode == MUSB_PORT_MODE_DUAL_ROLE)
		extcon_unregister_notifier(glue->extcon, EXTCON_USB_HOST,
					   &glue->host_nb);

	if (test_bit(SUNXI_MUSB_FL_HAS_RESET, &glue->flags))
		reset_control_assert(glue->rst);

	clk_disable_unprepare(glue->clk);
	if (test_bit(SUNXI_MUSB_FL_HAS_SRAM, &glue->flags))
		sunxi_sram_release(musb->controller->parent);

	devm_usb_put_phy(glue->dev, glue->xceiv);

	return 0;
}

static void sunxi_musb_enable(struct musb *musb)
{
	struct sunxi_glue *glue = dev_get_drvdata(musb->controller->parent);

	/* musb_core does not call us in a balanced manner */
	if (test_and_set_bit(SUNXI_MUSB_FL_ENABLED, &glue->flags))
		return;

	schedule_work(&glue->work);
}

static void sunxi_musb_disable(struct musb *musb)
{
	struct sunxi_glue *glue = dev_get_drvdata(musb->controller->parent);

	clear_bit(SUNXI_MUSB_FL_ENABLED, &glue->flags);
}

struct dma_controller *sunxi_musb_dma_controller_create(struct musb *musb,
						    void __iomem *base)
{
	return NULL;
}

void sunxi_musb_dma_controller_destroy(struct dma_controller *c)
{
}

/*
 * sunxi musb register layout
 * 0x00 - 0x17	fifo regs, 1 long per fifo
 * 0x40 - 0x57	generic control regs (power - frame)
 * 0x80 - 0x8f	ep control regs (addressed through hw_ep->regs, indexed)
 * 0x90 - 0x97	fifo control regs (indexed)
 * 0x98 - 0x9f	multipoint / busctl regs (indexed)
 * 0xc0		configdata reg
 */

static u32 sunxi_musb_fifo_offset(u8 epnum)
{
	return (epnum * 4);
}

static u32 sunxi_musb_ep_offset(u8 epnum, u16 offset)
{
	WARN_ONCE(offset != 0,
		  "sunxi_musb_ep_offset called with non 0 offset\n");

	return 0x80; /* indexed, so ignore epnum */
}

static u32 sunxi_musb_busctl_offset(u8 epnum, u16 offset)
{
	return SUNXI_MUSB_TXFUNCADDR + offset;
}

static u8 sunxi_musb_readb(const void __iomem *addr, unsigned offset)
{
	struct sunxi_glue *glue;

	if (addr == sunxi_musb->mregs) {
		/* generic control or fifo control reg access */
		switch (offset) {
		case MUSB_FADDR:
			return readb(addr + SUNXI_MUSB_FADDR);
		case MUSB_POWER:
			return readb(addr + SUNXI_MUSB_POWER);
		case MUSB_INTRUSB:
			return readb(addr + SUNXI_MUSB_INTRUSB);
		case MUSB_INTRUSBE:
			return readb(addr + SUNXI_MUSB_INTRUSBE);
		case MUSB_INDEX:
			return readb(addr + SUNXI_MUSB_INDEX);
		case MUSB_TESTMODE:
			return 0; /* No testmode on sunxi */
		case MUSB_DEVCTL:
			return readb(addr + SUNXI_MUSB_DEVCTL);
		case MUSB_TXFIFOSZ:
			return readb(addr + SUNXI_MUSB_TXFIFOSZ);
		case MUSB_RXFIFOSZ:
			return readb(addr + SUNXI_MUSB_RXFIFOSZ);
		case MUSB_CONFIGDATA + 0x10: /* See musb_read_configdata() */
			glue = dev_get_drvdata(sunxi_musb->controller->parent);
			/* A33 saves a reg, and we get to hardcode this */
			if (test_bit(SUNXI_MUSB_FL_NO_CONFIGDATA,
				     &glue->flags))
				return 0xde;

			return readb(addr + SUNXI_MUSB_CONFIGDATA);
		/* Offset for these is fixed by sunxi_musb_busctl_offset() */
		case SUNXI_MUSB_TXFUNCADDR:
		case SUNXI_MUSB_TXHUBADDR:
		case SUNXI_MUSB_TXHUBPORT:
		case SUNXI_MUSB_RXFUNCADDR:
		case SUNXI_MUSB_RXHUBADDR:
		case SUNXI_MUSB_RXHUBPORT:
			/* multipoint / busctl reg access */
			return readb(addr + offset);
		default:
			dev_err(sunxi_musb->controller->parent,
				"Error unknown readb offset %u\n", offset);
			return 0;
		}
	} else if (addr == (sunxi_musb->mregs + 0x80)) {
		/* ep control reg access */
		/* sunxi has a 2 byte hole before the txtype register */
		if (offset >= MUSB_TXTYPE)
			offset += 2;
		return readb(addr + offset);
	}

	dev_err(sunxi_musb->controller->parent,
		"Error unknown readb at 0x%x bytes offset\n",
		(int)(addr - sunxi_musb->mregs));
	return 0;
}

static void sunxi_musb_writeb(void __iomem *addr, unsigned offset, u8 data)
{
	if (addr == sunxi_musb->mregs) {
		/* generic control or fifo control reg access */
		switch (offset) {
		case MUSB_FADDR:
			return writeb(data, addr + SUNXI_MUSB_FADDR);
		case MUSB_POWER:
			return writeb(data, addr + SUNXI_MUSB_POWER);
		case MUSB_INTRUSB:
			return writeb(data, addr + SUNXI_MUSB_INTRUSB);
		case MUSB_INTRUSBE:
			return writeb(data, addr + SUNXI_MUSB_INTRUSBE);
		case MUSB_INDEX:
			return writeb(data, addr + SUNXI_MUSB_INDEX);
		case MUSB_TESTMODE:
			if (data)
				dev_warn(sunxi_musb->controller->parent,
					"sunxi-musb does not have testmode\n");
			return;
		case MUSB_DEVCTL:
			return writeb(data, addr + SUNXI_MUSB_DEVCTL);
		case MUSB_TXFIFOSZ:
			return writeb(data, addr + SUNXI_MUSB_TXFIFOSZ);
		case MUSB_RXFIFOSZ:
			return writeb(data, addr + SUNXI_MUSB_RXFIFOSZ);
		/* Offset for these is fixed by sunxi_musb_busctl_offset() */
		case SUNXI_MUSB_TXFUNCADDR:
		case SUNXI_MUSB_TXHUBADDR:
		case SUNXI_MUSB_TXHUBPORT:
		case SUNXI_MUSB_RXFUNCADDR:
		case SUNXI_MUSB_RXHUBADDR:
		case SUNXI_MUSB_RXHUBPORT:
			/* multipoint / busctl reg access */
			return writeb(data, addr + offset);
		default:
			dev_err(sunxi_musb->controller->parent,
				"Error unknown writeb offset %u\n", offset);
			return;
		}
	} else if (addr == (sunxi_musb->mregs + 0x80)) {
		/* ep control reg access */
		if (offset >= MUSB_TXTYPE)
			offset += 2;
		return writeb(data, addr + offset);
	}

	dev_err(sunxi_musb->controller->parent,
		"Error unknown writeb at 0x%x bytes offset\n",
		(int)(addr - sunxi_musb->mregs));
}

static u16 sunxi_musb_readw(const void __iomem *addr, unsigned offset)
{
	if (addr == sunxi_musb->mregs) {
		/* generic control or fifo control reg access */
		switch (offset) {
		case MUSB_INTRTX:
			return readw(addr + SUNXI_MUSB_INTRTX);
		case MUSB_INTRRX:
			return readw(addr + SUNXI_MUSB_INTRRX);
		case MUSB_INTRTXE:
			return readw(addr + SUNXI_MUSB_INTRTXE);
		case MUSB_INTRRXE:
			return readw(addr + SUNXI_MUSB_INTRRXE);
		case MUSB_FRAME:
			return readw(addr + SUNXI_MUSB_FRAME);
		case MUSB_TXFIFOADD:
			return readw(addr + SUNXI_MUSB_TXFIFOADD);
		case MUSB_RXFIFOADD:
			return readw(addr + SUNXI_MUSB_RXFIFOADD);
		case MUSB_HWVERS:
			return 0; /* sunxi musb version is not known */
		default:
			dev_err(sunxi_musb->controller->parent,
				"Error unknown readw offset %u\n", offset);
			return 0;
		}
	} else if (addr == (sunxi_musb->mregs + 0x80)) {
		/* ep control reg access */
		return readw(addr + offset);
	}

	dev_err(sunxi_musb->controller->parent,
		"Error unknown readw at 0x%x bytes offset\n",
		(int)(addr - sunxi_musb->mregs));
	return 0;
}

static void sunxi_musb_writew(void __iomem *addr, unsigned offset, u16 data)
{
	if (addr == sunxi_musb->mregs) {
		/* generic control or fifo control reg access */
		switch (offset) {
		case MUSB_INTRTX:
			return writew(data, addr + SUNXI_MUSB_INTRTX);
		case MUSB_INTRRX:
			return writew(data, addr + SUNXI_MUSB_INTRRX);
		case MUSB_INTRTXE:
			return writew(data, addr + SUNXI_MUSB_INTRTXE);
		case MUSB_INTRRXE:
			return writew(data, addr + SUNXI_MUSB_INTRRXE);
		case MUSB_FRAME:
			return writew(data, addr + SUNXI_MUSB_FRAME);
		case MUSB_TXFIFOADD:
			return writew(data, addr + SUNXI_MUSB_TXFIFOADD);
		case MUSB_RXFIFOADD:
			return writew(data, addr + SUNXI_MUSB_RXFIFOADD);
		default:
			dev_err(sunxi_musb->controller->parent,
				"Error unknown writew offset %u\n", offset);
			return;
		}
	} else if (addr == (sunxi_musb->mregs + 0x80)) {
		/* ep control reg access */
		return writew(data, addr + offset);
	}

	dev_err(sunxi_musb->controller->parent,
		"Error unknown writew at 0x%x bytes offset\n",
		(int)(addr - sunxi_musb->mregs));
}

static const struct musb_platform_ops sunxi_musb_ops = {
	.quirks		= MUSB_INDEXED_EP,
	.init		= sunxi_musb_init,
	.exit		= sunxi_musb_exit,
	.enable		= sunxi_musb_enable,
	.disable	= sunxi_musb_disable,
	.fifo_offset	= sunxi_musb_fifo_offset,
	.ep_offset	= sunxi_musb_ep_offset,
	.busctl_offset	= sunxi_musb_busctl_offset,
	.readb		= sunxi_musb_readb,
	.writeb		= sunxi_musb_writeb,
	.readw		= sunxi_musb_readw,
	.writew		= sunxi_musb_writew,
	.dma_init	= sunxi_musb_dma_controller_create,
	.dma_exit	= sunxi_musb_dma_controller_destroy,
	.set_vbus	= sunxi_musb_set_vbus,
	.pre_root_reset_end = sunxi_musb_pre_root_reset_end,
	.post_root_reset_end = sunxi_musb_post_root_reset_end,
};

/* Allwinner OTG supports up to 5 endpoints */
#define SUNXI_MUSB_MAX_EP_NUM	6
#define SUNXI_MUSB_RAM_BITS	11

static struct musb_fifo_cfg sunxi_musb_mode_cfg[] = {
	MUSB_EP_FIFO_SINGLE(1, FIFO_TX, 512),
	MUSB_EP_FIFO_SINGLE(1, FIFO_RX, 512),
	MUSB_EP_FIFO_SINGLE(2, FIFO_TX, 512),
	MUSB_EP_FIFO_SINGLE(2, FIFO_RX, 512),
	MUSB_EP_FIFO_SINGLE(3, FIFO_TX, 512),
	MUSB_EP_FIFO_SINGLE(3, FIFO_RX, 512),
	MUSB_EP_FIFO_SINGLE(4, FIFO_TX, 512),
	MUSB_EP_FIFO_SINGLE(4, FIFO_RX, 512),
	MUSB_EP_FIFO_SINGLE(5, FIFO_TX, 512),
	MUSB_EP_FIFO_SINGLE(5, FIFO_RX, 512),
};

static struct musb_hdrc_config sunxi_musb_hdrc_config = {
	.fifo_cfg       = sunxi_musb_mode_cfg,
	.fifo_cfg_size  = ARRAY_SIZE(sunxi_musb_mode_cfg),
	.multipoint	= true,
	.dyn_fifo	= true,
	.soft_con       = true,
	.num_eps	= SUNXI_MUSB_MAX_EP_NUM,
	.ram_bits	= SUNXI_MUSB_RAM_BITS,
	.dma		= 0,
};

static int sunxi_musb_probe(struct platform_device *pdev)
{
	struct musb_hdrc_platform_data	pdata;
	struct platform_device_info	pinfo;
	struct sunxi_glue		*glue;
	struct device_node		*np = pdev->dev.of_node;
	int ret;

	if (!np) {
		dev_err(&pdev->dev, "Error no device tree node found\n");
		return -EINVAL;
	}

	glue = devm_kzalloc(&pdev->dev, sizeof(*glue), GFP_KERNEL);
	if (!glue)
		return -ENOMEM;

	memset(&pdata, 0, sizeof(pdata));
	switch (usb_get_dr_mode(&pdev->dev)) {
#if defined CONFIG_USB_MUSB_DUAL_ROLE || defined CONFIG_USB_MUSB_HOST
	case USB_DR_MODE_HOST:
		pdata.mode = MUSB_PORT_MODE_HOST;
		break;
#endif
#ifdef CONFIG_USB_MUSB_DUAL_ROLE
	case USB_DR_MODE_OTG:
		glue->extcon = extcon_get_edev_by_phandle(&pdev->dev, 0);
		if (IS_ERR(glue->extcon)) {
			if (PTR_ERR(glue->extcon) == -EPROBE_DEFER)
				return -EPROBE_DEFER;
			dev_err(&pdev->dev, "Invalid or missing extcon\n");
			return PTR_ERR(glue->extcon);
		}
		pdata.mode = MUSB_PORT_MODE_DUAL_ROLE;
		break;
#endif
	default:
		dev_err(&pdev->dev, "Invalid or missing 'dr_mode' property\n");
		return -EINVAL;
	}
	pdata.platform_ops	= &sunxi_musb_ops;
	pdata.config		= &sunxi_musb_hdrc_config;

	glue->dev = &pdev->dev;
	INIT_WORK(&glue->work, sunxi_musb_work);
	glue->host_nb.notifier_call = sunxi_musb_host_notifier;

	if (of_device_is_compatible(np, "allwinner,sun4i-a10-musb"))
		set_bit(SUNXI_MUSB_FL_HAS_SRAM, &glue->flags);

	if (of_device_is_compatible(np, "allwinner,sun6i-a31-musb"))
		set_bit(SUNXI_MUSB_FL_HAS_RESET, &glue->flags);

	if (of_device_is_compatible(np, "allwinner,sun8i-a33-musb")) {
		set_bit(SUNXI_MUSB_FL_HAS_RESET, &glue->flags);
		set_bit(SUNXI_MUSB_FL_NO_CONFIGDATA, &glue->flags);
	}

	glue->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(glue->clk)) {
		dev_err(&pdev->dev, "Error getting clock: %ld\n",
			PTR_ERR(glue->clk));
		return PTR_ERR(glue->clk);
	}

	if (test_bit(SUNXI_MUSB_FL_HAS_RESET, &glue->flags)) {
		glue->rst = devm_reset_control_get(&pdev->dev, NULL);
		if (IS_ERR(glue->rst)) {
			if (PTR_ERR(glue->rst) == -EPROBE_DEFER)
				return -EPROBE_DEFER;
			dev_err(&pdev->dev, "Error getting reset %ld\n",
				PTR_ERR(glue->rst));
			return PTR_ERR(glue->rst);
		}
	}

	glue->phy = devm_phy_get(&pdev->dev, "usb");
	if (IS_ERR(glue->phy)) {
		if (PTR_ERR(glue->phy) == -EPROBE_DEFER)
			return -EPROBE_DEFER;
		dev_err(&pdev->dev, "Error getting phy %ld\n",
			PTR_ERR(glue->phy));
		return PTR_ERR(glue->phy);
	}

	glue->usb_phy = usb_phy_generic_register();
	if (IS_ERR(glue->usb_phy)) {
		dev_err(&pdev->dev, "Error registering usb-phy %ld\n",
			PTR_ERR(glue->usb_phy));
		return PTR_ERR(glue->usb_phy);
	}

	glue->xceiv = devm_usb_get_phy(&pdev->dev, USB_PHY_TYPE_USB2);
	if (IS_ERR(glue->xceiv)) {
		ret = PTR_ERR(glue->xceiv);
		dev_err(&pdev->dev, "Error getting usb-phy %d\n", ret);
		goto err_unregister_usb_phy;
	}

	platform_set_drvdata(pdev, glue);

	memset(&pinfo, 0, sizeof(pinfo));
	pinfo.name	 = "musb-hdrc";
	pinfo.id	= PLATFORM_DEVID_AUTO;
	pinfo.parent	= &pdev->dev;
	pinfo.res	= pdev->resource;
	pinfo.num_res	= pdev->num_resources;
	pinfo.data	= &pdata;
	pinfo.size_data = sizeof(pdata);

	glue->musb = platform_device_register_full(&pinfo);
	if (IS_ERR(glue->musb)) {
		ret = PTR_ERR(glue->musb);
		dev_err(&pdev->dev, "Error registering musb dev: %d\n", ret);
		goto err_unregister_usb_phy;
	}

	return 0;

err_unregister_usb_phy:
	usb_phy_generic_unregister(glue->usb_phy);
	return ret;
}

static int sunxi_musb_remove(struct platform_device *pdev)
{
	struct sunxi_glue *glue = platform_get_drvdata(pdev);
	struct platform_device *usb_phy = glue->usb_phy;

	platform_device_unregister(glue->musb); /* Frees glue ! */
	usb_phy_generic_unregister(usb_phy);

	return 0;
}

static const struct of_device_id sunxi_musb_match[] = {
	{ .compatible = "allwinner,sun4i-a10-musb", },
	{ .compatible = "allwinner,sun6i-a31-musb", },
	{ .compatible = "allwinner,sun8i-a33-musb", },
	{}
};

static struct platform_driver sunxi_musb_driver = {
	.probe = sunxi_musb_probe,
	.remove = sunxi_musb_remove,
	.driver = {
		.name = "musb-sunxi",
		.of_match_table = sunxi_musb_match,
	},
};
module_platform_driver(sunxi_musb_driver);

MODULE_DESCRIPTION("Allwinner sunxi MUSB Glue Layer");
MODULE_AUTHOR("Hans de Goede <hdegoede@redhat.com>");
MODULE_LICENSE("GPL v2");
