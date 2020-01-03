// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2018, Broadcom */

/*
 * This module contains USB PHY initialization for power up and S3 resume
 * for newer Synopsys based USB hardware first used on the bcm7216.
 */

#include <linux/delay.h>
#include <linux/io.h>

#include <linux/soc/brcmstb/brcmstb.h>
#include "phy-brcm-usb-init.h"

/* Register definitions for the USB CTRL block */
#define USB_CTRL_SETUP			0x00
#define   USB_CTRL_SETUP_STRAP_IPP_SEL_MASK		0x02000000
#define   USB_CTRL_SETUP_SCB2_EN_MASK			0x00008000
#define   USB_CTRL_SETUP_SCB1_EN_MASK			0x00004000
#define   USB_CTRL_SETUP_SOFT_SHUTDOWN_MASK		0x00000200
#define   USB_CTRL_SETUP_IPP_MASK			0x00000020
#define   USB_CTRL_SETUP_IOC_MASK			0x00000010
#define USB_CTRL_USB_PM			0x04
#define   USB_CTRL_USB_PM_USB_PWRDN_MASK		0x80000000
#define   USB_CTRL_USB_PM_SOFT_RESET_MASK		0x40000000
#define   USB_CTRL_USB_PM_BDC_SOFT_RESETB_MASK		0x00800000
#define   USB_CTRL_USB_PM_XHC_SOFT_RESETB_MASK		0x00400000
#define USB_CTRL_USB_PM_STATUS		0x08
#define USB_CTRL_USB_DEVICE_CTL1	0x10
#define   USB_CTRL_USB_DEVICE_CTL1_PORT_MODE_MASK	0x00000003


static void xhci_soft_reset(struct brcm_usb_init_params *params,
			int on_off)
{
	void __iomem *ctrl = params->ctrl_regs;

	/* Assert reset */
	if (on_off)
		USB_CTRL_UNSET(ctrl, USB_PM, XHC_SOFT_RESETB);
	/* De-assert reset */
	else
		USB_CTRL_SET(ctrl, USB_PM, XHC_SOFT_RESETB);
}

static void usb_init_ipp(struct brcm_usb_init_params *params)
{
	void __iomem *ctrl = params->ctrl_regs;
	u32 reg;
	u32 orig_reg;

	pr_debug("%s\n", __func__);

	orig_reg = reg = brcm_usb_readl(USB_CTRL_REG(ctrl, SETUP));
	if (params->ipp != 2)
		/* override ipp strap pin (if it exits) */
		reg &= ~(USB_CTRL_MASK(SETUP, STRAP_IPP_SEL));

	/* Override the default OC and PP polarity */
	reg &= ~(USB_CTRL_MASK(SETUP, IPP) | USB_CTRL_MASK(SETUP, IOC));
	if (params->ioc)
		reg |= USB_CTRL_MASK(SETUP, IOC);
	if (params->ipp == 1)
		reg |= USB_CTRL_MASK(SETUP, IPP);
	brcm_usb_writel(reg, USB_CTRL_REG(ctrl, SETUP));

	/*
	 * If we're changing IPP, make sure power is off long enough
	 * to turn off any connected devices.
	 */
	if ((reg ^ orig_reg) & USB_CTRL_MASK(SETUP, IPP))
		msleep(50);
}

static void usb_init_common(struct brcm_usb_init_params *params)
{
	u32 reg;
	void __iomem *ctrl = params->ctrl_regs;

	pr_debug("%s\n", __func__);

	USB_CTRL_UNSET(ctrl, USB_PM, USB_PWRDN);
	/* 1 millisecond - for USB clocks to settle down */
	usleep_range(1000, 2000);

	if (USB_CTRL_MASK(USB_DEVICE_CTL1, PORT_MODE)) {
		reg = brcm_usb_readl(USB_CTRL_REG(ctrl, USB_DEVICE_CTL1));
		reg &= ~USB_CTRL_MASK(USB_DEVICE_CTL1, PORT_MODE);
		reg |= params->mode;
		brcm_usb_writel(reg, USB_CTRL_REG(ctrl, USB_DEVICE_CTL1));
	}
	switch (params->mode) {
	case USB_CTLR_MODE_HOST:
		USB_CTRL_UNSET(ctrl, USB_PM, BDC_SOFT_RESETB);
		break;
	default:
		USB_CTRL_UNSET(ctrl, USB_PM, BDC_SOFT_RESETB);
		USB_CTRL_SET(ctrl, USB_PM, BDC_SOFT_RESETB);
		break;
	}
}

static void usb_init_xhci(struct brcm_usb_init_params *params)
{
	pr_debug("%s\n", __func__);

	xhci_soft_reset(params, 0);
}

static void usb_uninit_common(struct brcm_usb_init_params *params)
{
	void __iomem *ctrl = params->ctrl_regs;

	pr_debug("%s\n", __func__);

	USB_CTRL_SET(ctrl, USB_PM, USB_PWRDN);

}

static void usb_uninit_xhci(struct brcm_usb_init_params *params)
{

	pr_debug("%s\n", __func__);

	xhci_soft_reset(params, 1);
}

static int usb_get_dual_select(struct brcm_usb_init_params *params)
{
	void __iomem *ctrl = params->ctrl_regs;
	u32 reg = 0;

	pr_debug("%s\n", __func__);

	reg = brcm_usb_readl(USB_CTRL_REG(ctrl, USB_DEVICE_CTL1));
	reg &= USB_CTRL_MASK(USB_DEVICE_CTL1, PORT_MODE);
	return reg;
}

static void usb_set_dual_select(struct brcm_usb_init_params *params, int mode)
{
	void __iomem *ctrl = params->ctrl_regs;
	u32 reg;

	pr_debug("%s\n", __func__);

	reg = brcm_usb_readl(USB_CTRL_REG(ctrl, USB_DEVICE_CTL1));
	reg &= ~USB_CTRL_MASK(USB_DEVICE_CTL1, PORT_MODE);
	reg |= mode;
	brcm_usb_writel(reg, USB_CTRL_REG(ctrl, USB_DEVICE_CTL1));
}


static const struct brcm_usb_init_ops bcm7216_ops = {
	.init_ipp = usb_init_ipp,
	.init_common = usb_init_common,
	.init_xhci = usb_init_xhci,
	.uninit_common = usb_uninit_common,
	.uninit_xhci = usb_uninit_xhci,
	.get_dual_select = usb_get_dual_select,
	.set_dual_select = usb_set_dual_select,
};

void brcm_usb_dvr_init_7216(struct brcm_usb_init_params *params)
{

	pr_debug("%s\n", __func__);

	params->family_name = "7216";
	params->ops = &bcm7216_ops;
}
