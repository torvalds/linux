// SPDX-License-Identifier: GPL-2.0+
/* Faraday FOTG210 EHCI driver */
#include <linux/bits.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/usb.h>
#include <linux/usb/hcd.h>

#include "../host/ehci.h"
#include "fotg210.h"

#define FOTG210_PORTSC			0x30
#define FOTG210_MISC			0x40
#define FOTG200_MISC_SPEED_MASK		GENMASK(10, 9)
#define FOTG200_MISC_SPEED_SHIFT	9
#define FOTG210_MISC_EOF1_MASK		GENMASK(3, 2)
#define FOTG210_MISC_EOF1_720_21000_4000	(3 << 2)
#define FOTG210_MISC_AS_SLP_MASK	GENMASK(1, 0)
#define FOTG210_MISC_AS_SLP_10US	BIT(0)
#define FOTG210_OTGCSR			0x80
#define FOTG210_OTGCSR_SPEED_MASK	GENMASK(23, 22)
#define FOTG210_OTGCSR_SPEED_SHIFT	22

struct fotg210_hcd {
	struct fotg210 *fotg;
};

#define hcd_to_fotg210_priv(hcd) \
	((struct fotg210_hcd *)hcd_to_ehci(hcd)->priv)

static struct hc_driver __read_mostly fotg210_hc_driver;

static unsigned int fotg210_get_port_speed(struct ehci_hcd *ehci,
					   unsigned int port)
{
	struct fotg210 *fotg = hcd_to_fotg210_priv(ehci_to_hcd(ehci))->fotg;
	u32 speed;

	if (fotg->is_fotg210)
		speed = (readl(fotg->base + FOTG210_OTGCSR) &
			 FOTG210_OTGCSR_SPEED_MASK) >> FOTG210_OTGCSR_SPEED_SHIFT;
	else
		speed = (readl(fotg->base + FOTG210_MISC) &
			 FOTG200_MISC_SPEED_MASK) >> FOTG200_MISC_SPEED_SHIFT;

	switch (speed) {
	case 0:
		return 0;
	case 1:
		return USB_PORT_STAT_LOW_SPEED;
	case 2:
	default:
		return USB_PORT_STAT_HIGH_SPEED;
	}
}

static int fotg210_pre_port_reset(struct ehci_hcd *ehci, unsigned int port)
{
	u32 command;

	/* FOTG210 requires Run/Stop to be clear while PORTSC.Reset is set. */
	command = ehci_readl(ehci, &ehci->regs->command);
	ehci_writel(ehci, command & ~CMD_RUN, &ehci->regs->command);

	return ehci_handshake(ehci, &ehci->regs->status,
			      STS_HALT, STS_HALT, 16 * 125);
}

static int fotg210_post_port_reset(struct ehci_hcd *ehci, unsigned int port)
{
	ehci_writel(ehci, ehci->command | CMD_RUN, &ehci->regs->command);

	return ehci_handshake(ehci, &ehci->regs->status, STS_HALT, 0,
			      16 * 125);
}

static int fotg210_hcd_reset(struct usb_hcd *hcd)
{
	struct fotg210 *fotg = hcd_to_fotg210_priv(hcd)->fotg;
	struct ehci_hcd *ehci = hcd_to_ehci(hcd);
	u32 val;
	int ret;

	/* FOTG210 embeds an EHCI core but moves and omits some registers. */
	ehci->port_status = hcd->regs + FOTG210_PORTSC;
	ehci->get_port_speed = fotg210_get_port_speed;
	ehci->pre_port_reset = fotg210_pre_port_reset;
	ehci->post_port_reset = fotg210_post_port_reset;
	ehci->no_configured_flag = 1;
	ehci->no_tdi_mode = 1;
	ehci->no_fsls_isoc = 1;
	hcd->has_tt = 1;

	ret = ehci_setup(hcd);
	if (ret)
		return ret;

	if (fotg->port != GEMINI_PORT_NONE) {
		/* Work around the Gemini full-speed EOF1 timing erratum. */
		val = readl(fotg->base + FOTG210_MISC);
		val &= ~(FOTG210_MISC_EOF1_MASK | FOTG210_MISC_AS_SLP_MASK);
		val |= FOTG210_MISC_EOF1_720_21000_4000 |
		       FOTG210_MISC_AS_SLP_10US;
		writel(val, fotg->base + FOTG210_MISC);
	}

	return 0;
}

static const struct ehci_driver_overrides fotg210_hc_overrides __initconst = {
	.reset = fotg210_hcd_reset,
	.extra_priv_size = sizeof(struct fotg210_hcd),
};

/*
 * fotg210_hcd_probe - initialize faraday FOTG210 HCDs
 *
 * Allocates basic resources for this USB host controller, and
 * then invokes the start() method for the HCD associated with it
 * through the hotplug entry's driver_data.
 */
int fotg210_hcd_probe(struct platform_device *pdev, struct fotg210 *fotg)
{
	struct device *dev = &pdev->dev;
	struct usb_hcd *hcd;
	int irq;
	int ret;

	if (usb_disabled())
		return -ENODEV;

	hcd = usb_create_hcd(&fotg210_hc_driver, dev, dev_name(dev));
	if (!hcd)
		return dev_err_probe(dev, -ENOMEM, "failed to create hcd\n");

	hcd->rsrc_start = fotg->res->start;
	hcd->rsrc_len = resource_size(fotg->res);
	hcd->regs = fotg->base;
	hcd_to_fotg210_priv(hcd)->fotg = fotg;
	hcd_to_ehci(hcd)->caps = fotg->base;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		ret = irq;
		goto failed_put_hcd;
	}

	ret = usb_add_hcd(hcd, irq, 0);
	if (ret) {
		dev_err_probe(dev, ret, "failed to add HCD\n");
		goto failed_put_hcd;
	}
	device_wakeup_enable(hcd->self.controller);

	return ret;

failed_put_hcd:
	usb_put_hcd(hcd);
	return ret;
}

/*
 * fotg210_hcd_remove - shutdown processing for EHCI HCDs
 * @dev: USB Host Controller being removed
 *
 */
int fotg210_hcd_remove(struct platform_device *pdev)
{
	struct usb_hcd *hcd = platform_get_drvdata(pdev);
	struct fotg210 *fotg = hcd_to_fotg210_priv(hcd)->fotg;

	usb_remove_hcd(hcd);
	usb_put_hcd(hcd);
	fotg210_vbus(fotg, false);

	return 0;
}

int fotg210_hcd_suspend(struct device *dev)
{
#ifdef CONFIG_PM
	struct usb_hcd *hcd = dev_get_drvdata(dev);

	return ehci_suspend(hcd, device_may_wakeup(dev));
#else
	return 0;
#endif
}

int fotg210_hcd_resume(struct device *dev)
{
#ifdef CONFIG_PM
	struct usb_hcd *hcd = dev_get_drvdata(dev);

	return ehci_resume(hcd, false);
#else
	return 0;
#endif
}

int __init fotg210_hcd_init(void)
{
	ehci_init_driver(&fotg210_hc_driver, &fotg210_hc_overrides);
	return 0;
}

void fotg210_hcd_cleanup(void)
{
}
