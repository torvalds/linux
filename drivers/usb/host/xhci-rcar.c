// SPDX-License-Identifier: GPL-2.0
/*
 * xHCI host controller driver for R-Car SoCs
 *
 * Copyright (C) 2014 Renesas Electronics Corporation
 */

#include <linux/firmware.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/usb/phy.h>
#include <linux/sys_soc.h>

#include "xhci.h"
#include "xhci-plat.h"
#include "xhci-rcar.h"

/*
* - The V3 firmware is for almost all R-Car Gen3 (except r8a7795 ES1.x)
* - The V2 firmware is for r8a7795 ES1.x.
* - The V2 firmware is possible to use on R-Car Gen2. However, the V2 causes
*   performance degradation. So, this driver continues to use the V1 if R-Car
*   Gen2.
* - The V1 firmware is impossible to use on R-Car Gen3.
*/
MODULE_FIRMWARE(XHCI_RCAR_FIRMWARE_NAME_V1);
MODULE_FIRMWARE(XHCI_RCAR_FIRMWARE_NAME_V2);
MODULE_FIRMWARE(XHCI_RCAR_FIRMWARE_NAME_V3);

/*** Register Offset ***/
#define RCAR_USB3_AXH_STA	0x104	/* AXI Host Control Status */
#define RCAR_USB3_INT_ENA	0x224	/* Interrupt Enable */
#define RCAR_USB3_DL_CTRL	0x250	/* FW Download Control & Status */
#define RCAR_USB3_FW_DATA0	0x258	/* FW Data0 */

#define RCAR_USB3_LCLK		0xa44	/* LCLK Select */
#define RCAR_USB3_CONF1		0xa48	/* USB3.0 Configuration1 */
#define RCAR_USB3_CONF2		0xa5c	/* USB3.0 Configuration2 */
#define RCAR_USB3_CONF3		0xaa8	/* USB3.0 Configuration3 */
#define RCAR_USB3_RX_POL	0xab0	/* USB3.0 RX Polarity */
#define RCAR_USB3_TX_POL	0xab8	/* USB3.0 TX Polarity */

/*** Register Settings ***/
/* AXI Host Control Status */
#define RCAR_USB3_AXH_STA_B3_PLL_ACTIVE		0x00010000
#define RCAR_USB3_AXH_STA_B2_PLL_ACTIVE		0x00000001
#define RCAR_USB3_AXH_STA_PLL_ACTIVE_MASK (RCAR_USB3_AXH_STA_B3_PLL_ACTIVE | \
					   RCAR_USB3_AXH_STA_B2_PLL_ACTIVE)

/* Interrupt Enable */
#define RCAR_USB3_INT_XHC_ENA	0x00000001
#define RCAR_USB3_INT_PME_ENA	0x00000002
#define RCAR_USB3_INT_HSE_ENA	0x00000004
#define RCAR_USB3_INT_ENA_VAL	(RCAR_USB3_INT_XHC_ENA | \
				RCAR_USB3_INT_PME_ENA | RCAR_USB3_INT_HSE_ENA)

/* FW Download Control & Status */
#define RCAR_USB3_DL_CTRL_ENABLE	0x00000001
#define RCAR_USB3_DL_CTRL_FW_SUCCESS	0x00000010
#define RCAR_USB3_DL_CTRL_FW_SET_DATA0	0x00000100

/* LCLK Select */
#define RCAR_USB3_LCLK_ENA_VAL	0x01030001

/* USB3.0 Configuration */
#define RCAR_USB3_CONF1_VAL	0x00030204
#define RCAR_USB3_CONF2_VAL	0x00030300
#define RCAR_USB3_CONF3_VAL	0x13802007

/* USB3.0 Polarity */
#define RCAR_USB3_RX_POL_VAL	BIT(21)
#define RCAR_USB3_TX_POL_VAL	BIT(4)

/* For soc_device_attribute */
#define RCAR_XHCI_FIRMWARE_V2   BIT(0) /* FIRMWARE V2 */
#define RCAR_XHCI_FIRMWARE_V3   BIT(1) /* FIRMWARE V3 */

static const struct soc_device_attribute rcar_quirks_match[]  = {
	{
		.soc_id = "r8a7795", .revision = "ES1.*",
		.data = (void *)RCAR_XHCI_FIRMWARE_V2,
	},
	{ /* sentinel */ },
};

static void xhci_rcar_start_gen2(struct usb_hcd *hcd)
{
	/* LCLK Select */
	writel(RCAR_USB3_LCLK_ENA_VAL, hcd->regs + RCAR_USB3_LCLK);
	/* USB3.0 Configuration */
	writel(RCAR_USB3_CONF1_VAL, hcd->regs + RCAR_USB3_CONF1);
	writel(RCAR_USB3_CONF2_VAL, hcd->regs + RCAR_USB3_CONF2);
	writel(RCAR_USB3_CONF3_VAL, hcd->regs + RCAR_USB3_CONF3);
	/* USB3.0 Polarity */
	writel(RCAR_USB3_RX_POL_VAL, hcd->regs + RCAR_USB3_RX_POL);
	writel(RCAR_USB3_TX_POL_VAL, hcd->regs + RCAR_USB3_TX_POL);
}

static int xhci_rcar_is_gen2(struct device *dev)
{
	struct device_node *node = dev->of_node;

	return of_device_is_compatible(node, "renesas,xhci-r8a7790") ||
		of_device_is_compatible(node, "renesas,xhci-r8a7791") ||
		of_device_is_compatible(node, "renesas,xhci-r8a7793") ||
		of_device_is_compatible(node, "renesas,rcar-gen2-xhci");
}

void xhci_rcar_start(struct usb_hcd *hcd)
{
	u32 temp;

	if (hcd->regs != NULL) {
		/* Interrupt Enable */
		temp = readl(hcd->regs + RCAR_USB3_INT_ENA);
		temp |= RCAR_USB3_INT_ENA_VAL;
		writel(temp, hcd->regs + RCAR_USB3_INT_ENA);
		if (xhci_rcar_is_gen2(hcd->self.controller))
			xhci_rcar_start_gen2(hcd);
	}
}

static int xhci_rcar_download_firmware(struct usb_hcd *hcd)
{
	struct device *dev = hcd->self.controller;
	void __iomem *regs = hcd->regs;
	struct xhci_plat_priv *priv = hcd_to_xhci_priv(hcd);
	const struct firmware *fw;
	int retval, index, j, time;
	int timeout = 10000;
	u32 data, val, temp;
	u32 quirks = 0;
	const struct soc_device_attribute *attr;
	const char *firmware_name;

	attr = soc_device_match(rcar_quirks_match);
	if (attr)
		quirks = (uintptr_t)attr->data;

	if (quirks & RCAR_XHCI_FIRMWARE_V2)
		firmware_name = XHCI_RCAR_FIRMWARE_NAME_V2;
	else if (quirks & RCAR_XHCI_FIRMWARE_V3)
		firmware_name = XHCI_RCAR_FIRMWARE_NAME_V3;
	else
		firmware_name = priv->firmware_name;

	/* request R-Car USB3.0 firmware */
	retval = request_firmware(&fw, firmware_name, dev);
	if (retval)
		return retval;

	/* download R-Car USB3.0 firmware */
	temp = readl(regs + RCAR_USB3_DL_CTRL);
	temp |= RCAR_USB3_DL_CTRL_ENABLE;
	writel(temp, regs + RCAR_USB3_DL_CTRL);

	for (index = 0; index < fw->size; index += 4) {
		/* to avoid reading beyond the end of the buffer */
		for (data = 0, j = 3; j >= 0; j--) {
			if ((j + index) < fw->size)
				data |= fw->data[index + j] << (8 * j);
		}
		writel(data, regs + RCAR_USB3_FW_DATA0);
		temp = readl(regs + RCAR_USB3_DL_CTRL);
		temp |= RCAR_USB3_DL_CTRL_FW_SET_DATA0;
		writel(temp, regs + RCAR_USB3_DL_CTRL);

		for (time = 0; time < timeout; time++) {
			val = readl(regs + RCAR_USB3_DL_CTRL);
			if ((val & RCAR_USB3_DL_CTRL_FW_SET_DATA0) == 0)
				break;
			udelay(1);
		}
		if (time == timeout) {
			retval = -ETIMEDOUT;
			break;
		}
	}

	temp = readl(regs + RCAR_USB3_DL_CTRL);
	temp &= ~RCAR_USB3_DL_CTRL_ENABLE;
	writel(temp, regs + RCAR_USB3_DL_CTRL);

	for (time = 0; time < timeout; time++) {
		val = readl(regs + RCAR_USB3_DL_CTRL);
		if (val & RCAR_USB3_DL_CTRL_FW_SUCCESS) {
			retval = 0;
			break;
		}
		udelay(1);
	}
	if (time == timeout)
		retval = -ETIMEDOUT;

	release_firmware(fw);

	return retval;
}

static bool xhci_rcar_wait_for_pll_active(struct usb_hcd *hcd)
{
	int timeout = 1000;
	u32 val, mask = RCAR_USB3_AXH_STA_PLL_ACTIVE_MASK;

	while (timeout > 0) {
		val = readl(hcd->regs + RCAR_USB3_AXH_STA);
		if ((val & mask) == mask)
			return true;
		udelay(1);
		timeout--;
	}

	return false;
}

/* This function needs to initialize a "phy" of usb before */
int xhci_rcar_init_quirk(struct usb_hcd *hcd)
{
	/* If hcd->regs is NULL, we don't just call the following function */
	if (!hcd->regs)
		return 0;

	if (!xhci_rcar_wait_for_pll_active(hcd))
		return -ETIMEDOUT;

	return xhci_rcar_download_firmware(hcd);
}

int xhci_rcar_resume_quirk(struct usb_hcd *hcd)
{
	int ret;

	ret = xhci_rcar_download_firmware(hcd);
	if (!ret)
		xhci_rcar_start(hcd);

	return ret;
}
