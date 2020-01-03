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

#define PHY_LOCK_TIMEOUT_MS 200

/* Register definitions for syscon piarbctl registers */
#define PIARBCTL_CAM			0x00
#define PIARBCTL_SPLITTER		0x04
#define PIARBCTL_MISC			0x08
#define   PIARBCTL_MISC_SECURE_MASK			0x80000000
#define   PIARBCTL_MISC_USB_SELECT_MASK			0x40000000
#define   PIARBCTL_MISC_USB_4G_SDRAM_MASK		0x20000000
#define   PIARBCTL_MISC_USB_PRIORITY_MASK		0x000f0000
#define   PIARBCTL_MISC_USB_MEM_PAGE_MASK		0x0000f000
#define   PIARBCTL_MISC_CAM1_MEM_PAGE_MASK		0x00000f00
#define   PIARBCTL_MISC_CAM0_MEM_PAGE_MASK		0x000000f0
#define   PIARBCTL_MISC_SATA_PRIORITY_MASK		0x0000000f
#define PIARBCTL_USB_M_ASB_CTRL		0x10

#define PIARBCTL_MISC_USB_ONLY_MASK		\
	(PIARBCTL_MISC_USB_SELECT_MASK |	\
	 PIARBCTL_MISC_USB_4G_SDRAM_MASK |	\
	 PIARBCTL_MISC_USB_PRIORITY_MASK |	\
	 PIARBCTL_MISC_USB_MEM_PAGE_MASK)

/* Register definitions for the USB CTRL block */
#define USB_CTRL_SETUP			0x00
#define   USB_CTRL_SETUP_STRAP_IPP_SEL_MASK		0x02000000
#define   USB_CTRL_SETUP_SCB2_EN_MASK			0x00008000
#define   USB_CTRL_SETUP_tca_drv_sel_MASK		0x01000000
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

/* Register definitions for the USB_PHY block in 7211b0 */
#define USB_PHY_PLL_LDO_CTL		0x08
#define   USB_PHY_PLL_LDO_CTL_AFE_CORERDY_MASK		0x00000004
#define USB_PHY_UTMI_CTL_1		0x04
#define   USB_PHY_UTMI_CTL_1_PHY_MODE_MASK		0x0000000c
#define   USB_PHY_UTMI_CTL_1_PHY_MODE_SHIFT		2
#define USB_PHY_STATUS			0x20
#define   USB_PHY_STATUS_pll_lock_MASK			0x00000001

/* Register definitions for the MDIO registers in the DWC2 block of
 * the 7211b0.
 * NOTE: The PHY's MDIO registers are only accessible through the
 * legacy DesignWare USB controller even though it's not being used.
 */
#define USB_GMDIOCSR	0
#define USB_GMDIOGEN	4


static void usb_mdio_write_7211b0(struct brcm_usb_init_params *params,
				  uint8_t addr, uint16_t data)
{
	void __iomem *usb_mdio = params->regs[BRCM_REGS_USB_MDIO];

	addr &= 0x1f; /* 5-bit address */
	brcm_usb_writel(0xffffffff, usb_mdio + USB_GMDIOGEN);
	while (brcm_usb_readl(usb_mdio + USB_GMDIOCSR) & (1<<31))
		;
	brcm_usb_writel(0x59020000 | (addr << 18) | data,
			usb_mdio + USB_GMDIOGEN);
	while (brcm_usb_readl(usb_mdio + USB_GMDIOCSR) & (1<<31))
		;
	brcm_usb_writel(0x00000000, usb_mdio + USB_GMDIOGEN);
	while (brcm_usb_readl(usb_mdio + USB_GMDIOCSR) & (1<<31))
		;
}

static uint16_t __maybe_unused usb_mdio_read_7211b0(
	struct brcm_usb_init_params *params, uint8_t addr)
{
	void __iomem *usb_mdio = params->regs[BRCM_REGS_USB_MDIO];

	addr &= 0x1f; /* 5-bit address */
	brcm_usb_writel(0xffffffff, usb_mdio + USB_GMDIOGEN);
	while (brcm_usb_readl(usb_mdio + USB_GMDIOCSR) & (1<<31))
		;
	brcm_usb_writel(0x69020000 | (addr << 18), usb_mdio + USB_GMDIOGEN);
	while (brcm_usb_readl(usb_mdio + USB_GMDIOCSR) & (1<<31))
		;
	brcm_usb_writel(0x00000000, usb_mdio + USB_GMDIOGEN);
	while (brcm_usb_readl(usb_mdio + USB_GMDIOCSR) & (1<<31))
		;
	return brcm_usb_readl(usb_mdio + USB_GMDIOCSR) & 0xffff;
}

static void usb2_eye_fix_7211b0(struct brcm_usb_init_params *params)
{
	/* select bank */
	usb_mdio_write_7211b0(params, 0x1f, 0x80a0);

	/* Set the eye */
	usb_mdio_write_7211b0(params, 0x0a, 0xc6a0);
}

static void xhci_soft_reset(struct brcm_usb_init_params *params,
			int on_off)
{
	void __iomem *ctrl = params->regs[BRCM_REGS_CTRL];

	/* Assert reset */
	if (on_off)
		USB_CTRL_UNSET(ctrl, USB_PM, XHC_SOFT_RESETB);
	/* De-assert reset */
	else
		USB_CTRL_SET(ctrl, USB_PM, XHC_SOFT_RESETB);
}

static void usb_init_ipp(struct brcm_usb_init_params *params)
{
	void __iomem *ctrl = params->regs[BRCM_REGS_CTRL];
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

static void syscon_piarbctl_init(struct regmap *rmap)
{
	/* Switch from legacy USB OTG controller to new STB USB controller */
	regmap_update_bits(rmap, PIARBCTL_MISC, PIARBCTL_MISC_USB_ONLY_MASK,
			   PIARBCTL_MISC_USB_SELECT_MASK |
			   PIARBCTL_MISC_USB_4G_SDRAM_MASK);
}

static void usb_init_common(struct brcm_usb_init_params *params)
{
	u32 reg;
	void __iomem *ctrl = params->regs[BRCM_REGS_CTRL];

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

static void usb_init_common_7211b0(struct brcm_usb_init_params *params)
{
	void __iomem *ctrl = params->regs[BRCM_REGS_CTRL];
	void __iomem *usb_phy = params->regs[BRCM_REGS_USB_PHY];
	int timeout_ms = PHY_LOCK_TIMEOUT_MS;
	u32 reg;

	if (params->syscon_piarbctl)
		syscon_piarbctl_init(params->syscon_piarbctl);

	/* Init the PHY */
	reg = brcm_usb_readl(usb_phy + USB_PHY_PLL_LDO_CTL);
	reg |= USB_PHY_PLL_LDO_CTL_AFE_CORERDY_MASK;
	brcm_usb_writel(reg, usb_phy + USB_PHY_PLL_LDO_CTL);

	/* wait for lock */
	while (timeout_ms-- > 0) {
		reg = brcm_usb_readl(usb_phy + USB_PHY_STATUS);
		if (reg & USB_PHY_STATUS_pll_lock_MASK)
			break;
		usleep_range(1000, 2000);
	}

	/* Set the PHY_MODE */
	reg = brcm_usb_readl(usb_phy + USB_PHY_UTMI_CTL_1);
	reg &= ~USB_PHY_UTMI_CTL_1_PHY_MODE_MASK;
	reg |= params->mode << USB_PHY_UTMI_CTL_1_PHY_MODE_SHIFT;
	brcm_usb_writel(reg, usb_phy + USB_PHY_UTMI_CTL_1);

	/* Fix the incorrect default */
	reg = brcm_usb_readl(ctrl + USB_CTRL_SETUP);
	reg &= ~USB_CTRL_SETUP_tca_drv_sel_MASK;
	brcm_usb_writel(reg, ctrl + USB_CTRL_SETUP);

	usb_init_common(params);

	usb2_eye_fix_7211b0(params);
}

static void usb_init_xhci(struct brcm_usb_init_params *params)
{
	pr_debug("%s\n", __func__);

	xhci_soft_reset(params, 0);
}

static void usb_uninit_common(struct brcm_usb_init_params *params)
{
	void __iomem *ctrl = params->regs[BRCM_REGS_CTRL];

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
	void __iomem *ctrl = params->regs[BRCM_REGS_CTRL];
	u32 reg = 0;

	pr_debug("%s\n", __func__);

	reg = brcm_usb_readl(USB_CTRL_REG(ctrl, USB_DEVICE_CTL1));
	reg &= USB_CTRL_MASK(USB_DEVICE_CTL1, PORT_MODE);
	return reg;
}

static void usb_set_dual_select(struct brcm_usb_init_params *params, int mode)
{
	void __iomem *ctrl = params->regs[BRCM_REGS_CTRL];
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

static const struct brcm_usb_init_ops bcm7211b0_ops = {
	.init_ipp = usb_init_ipp,
	.init_common = usb_init_common_7211b0,
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

void brcm_usb_dvr_init_7211b0(struct brcm_usb_init_params *params)
{

	pr_debug("%s\n", __func__);

	params->family_name = "7211";
	params->ops = &bcm7211b0_ops;
}
