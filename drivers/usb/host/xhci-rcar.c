// SPDX-License-Identifier: GPL-2.0
/*
 * xHCI host controller driver for R-Car SoCs
 *
 * Copyright (C) 2014 Renesas Electronics Corporation
 */

#include <linux/firmware.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/usb/phy.h>

#include "xhci.h"
#include "xhci-plat.h"
#include "xhci-rzv2m.h"

#define XHCI_RCAR_FIRMWARE_NAME_V1	"r8a779x_usb3_v1.dlmem"
#define XHCI_RCAR_FIRMWARE_NAME_V3	"r8a779x_usb3_v3.dlmem"

/*
* - The V3 firmware is for all R-Car Gen3
* - The V2 firmware is possible to use on R-Car Gen2. However, the V2 causes
*   performance degradation. So, this driver continues to use the V1 if R-Car
*   Gen2.
* - The V1 firmware is impossible to use on R-Car Gen3.
*/
MODULE_FIRMWARE(XHCI_RCAR_FIRMWARE_NAME_V1);
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

static void xhci_rcar_start(struct usb_hcd *hcd)
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
	int retval, index, j;
	u32 data, val, temp;

	/*
	 * According to the datasheet, "Upon the completion of FW Download,
	 * there is no need to write or reload FW".
	 */
	if (readl(regs + RCAR_USB3_DL_CTRL) & RCAR_USB3_DL_CTRL_FW_SUCCESS)
		return 0;

	/* request R-Car USB3.0 firmware */
	retval = request_firmware(&fw, priv->firmware_name, dev);
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

		retval = readl_poll_timeout_atomic(regs + RCAR_USB3_DL_CTRL,
				val, !(val & RCAR_USB3_DL_CTRL_FW_SET_DATA0),
				1, 10000);
		if (retval < 0)
			break;
	}

	temp = readl(regs + RCAR_USB3_DL_CTRL);
	temp &= ~RCAR_USB3_DL_CTRL_ENABLE;
	writel(temp, regs + RCAR_USB3_DL_CTRL);

	retval = readl_poll_timeout_atomic((regs + RCAR_USB3_DL_CTRL),
			val, val & RCAR_USB3_DL_CTRL_FW_SUCCESS, 1, 10000);

	release_firmware(fw);

	return retval;
}

static bool xhci_rcar_wait_for_pll_active(struct usb_hcd *hcd)
{
	int retval;
	u32 val, mask = RCAR_USB3_AXH_STA_PLL_ACTIVE_MASK;

	retval = readl_poll_timeout_atomic(hcd->regs + RCAR_USB3_AXH_STA,
			val, (val & mask) == mask, 1, 1000);
	return !retval;
}

/* This function needs to initialize a "phy" of usb before */
static int xhci_rcar_init_quirk(struct usb_hcd *hcd)
{
	/* If hcd->regs is NULL, we don't just call the following function */
	if (!hcd->regs)
		return 0;

	if (!xhci_rcar_wait_for_pll_active(hcd))
		return -ETIMEDOUT;

	return xhci_rcar_download_firmware(hcd);
}

static int xhci_rcar_resume_quirk(struct usb_hcd *hcd)
{
	int ret;

	ret = xhci_rcar_download_firmware(hcd);
	if (!ret)
		xhci_rcar_start(hcd);

	return ret;
}

/*
 * On R-Car Gen2 and Gen3, the AC64 bit (bit 0) of HCCPARAMS1 is set
 * to 1. However, these SoCs don't support 64-bit address memory
 * pointers. So, this driver clears the AC64 bit of xhci->hcc_params
 * to call dma_set_coherent_mask(dev, DMA_BIT_MASK(32)) in
 * xhci_gen_setup() by using the XHCI_NO_64BIT_SUPPORT quirk.
 *
 * And, since the firmware/internal CPU control the USBSTS.STS_HALT
 * and the process speed is down when the roothub port enters U3,
 * long delay for the handshake of STS_HALT is neeed in xhci_suspend()
 * by using the XHCI_SLOW_SUSPEND quirk.
 */
#define SET_XHCI_PLAT_PRIV_FOR_RCAR(firmware)				\
	.firmware_name = firmware,					\
	.quirks = XHCI_NO_64BIT_SUPPORT | XHCI_TRUST_TX_LENGTH |	\
		  XHCI_SLOW_SUSPEND,					\
	.init_quirk = xhci_rcar_init_quirk,				\
	.plat_start = xhci_rcar_start,					\
	.resume_quirk = xhci_rcar_resume_quirk,

static const struct xhci_plat_priv xhci_plat_renesas_rcar_gen2 = {
	SET_XHCI_PLAT_PRIV_FOR_RCAR(XHCI_RCAR_FIRMWARE_NAME_V1)
};

static const struct xhci_plat_priv xhci_plat_renesas_rcar_gen3 = {
	SET_XHCI_PLAT_PRIV_FOR_RCAR(XHCI_RCAR_FIRMWARE_NAME_V3)
};

static const struct xhci_plat_priv xhci_plat_renesas_rzv2m = {
	.quirks = XHCI_NO_64BIT_SUPPORT | XHCI_TRUST_TX_LENGTH |
		  XHCI_SLOW_SUSPEND,
	.init_quirk = xhci_rzv2m_init_quirk,
	.plat_start = xhci_rzv2m_start,
};

static const struct of_device_id usb_xhci_of_match[] = {
	{
		.compatible = "renesas,xhci-r8a7790",
		.data = &xhci_plat_renesas_rcar_gen2,
	}, {
		.compatible = "renesas,xhci-r8a7791",
		.data = &xhci_plat_renesas_rcar_gen2,
	}, {
		.compatible = "renesas,xhci-r8a7793",
		.data = &xhci_plat_renesas_rcar_gen2,
	}, {
		.compatible = "renesas,xhci-r8a7795",
		.data = &xhci_plat_renesas_rcar_gen3,
	}, {
		.compatible = "renesas,xhci-r8a7796",
		.data = &xhci_plat_renesas_rcar_gen3,
	}, {
		.compatible = "renesas,rcar-gen2-xhci",
		.data = &xhci_plat_renesas_rcar_gen2,
	}, {
		.compatible = "renesas,rcar-gen3-xhci",
		.data = &xhci_plat_renesas_rcar_gen3,
	}, {
		.compatible = "renesas,rzv2m-xhci",
		.data = &xhci_plat_renesas_rzv2m,
	},
	{ },
};
MODULE_DEVICE_TABLE(of, usb_xhci_of_match);

static int xhci_renesas_probe(struct platform_device *pdev)
{
	const struct xhci_plat_priv *priv_match;

	priv_match = of_device_get_match_data(&pdev->dev);

	return xhci_plat_probe(pdev, NULL, priv_match);
}

static struct platform_driver usb_xhci_renesas_driver = {
	.probe	= xhci_renesas_probe,
	.remove	= xhci_plat_remove,
	.shutdown = usb_hcd_platform_shutdown,
	.driver	= {
		.name = "xhci-renesas-hcd",
		.pm = &xhci_plat_pm_ops,
		.of_match_table = usb_xhci_of_match,
	},
};
module_platform_driver(usb_xhci_renesas_driver);

MODULE_DESCRIPTION("xHCI Platform Host Controller Driver for Renesas R-Car and RZ");
MODULE_LICENSE("GPL");
