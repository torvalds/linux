// SPDX-License-Identifier: GPL-2.0
/*
 * Renesas USB driver R-Car Gen. 3 initialization and power control
 *
 * Copyright (C) 2016 Renesas Electronics Corporation
 */

#include <linux/delay.h>
#include <linux/io.h>
#include "common.h"
#include "rcar3.h"

#define LPSTS		0x102
#define UGCTRL		0x180	/* 32-bit register */
#define UGCTRL2		0x184	/* 32-bit register */
#define UGSTS		0x188	/* 32-bit register */

/* Low Power Status register (LPSTS) */
#define LPSTS_SUSPM	0x4000

/* R-Car D3 only: USB General control register (UGCTRL) */
#define UGCTRL_PLLRESET		0x00000001
#define UGCTRL_CONNECT		0x00000004

/*
 * USB General control register 2 (UGCTRL2)
 * Remarks: bit[31:11] and bit[9:6] should be 0
 */
#define UGCTRL2_RESERVED_3	0x00000001	/* bit[3:0] should be B'0001 */
#define UGCTRL2_USB0SEL_HSUSB	0x00000020
#define UGCTRL2_USB0SEL_OTG	0x00000030
#define UGCTRL2_VBUSSEL		0x00000400

/* R-Car D3 only: USB General status register (UGSTS) */
#define UGSTS_LOCK		0x00000100

static void usbhs_write32(struct usbhs_priv *priv, u32 reg, u32 data)
{
	iowrite32(data, priv->base + reg);
}

static u32 usbhs_read32(struct usbhs_priv *priv, u32 reg)
{
	return ioread32(priv->base + reg);
}

static void usbhs_rcar3_set_ugctrl2(struct usbhs_priv *priv, u32 val)
{
	usbhs_write32(priv, UGCTRL2, val | UGCTRL2_RESERVED_3);
}

static int usbhs_rcar3_power_ctrl(struct platform_device *pdev,
				void __iomem *base, int enable)
{
	struct usbhs_priv *priv = usbhs_pdev_to_priv(pdev);

	usbhs_rcar3_set_ugctrl2(priv, UGCTRL2_USB0SEL_OTG | UGCTRL2_VBUSSEL);

	if (enable) {
		usbhs_bset(priv, LPSTS, LPSTS_SUSPM, LPSTS_SUSPM);
		/* The controller on R-Car Gen3 needs to wait up to 45 usec */
		usleep_range(45, 90);
	} else {
		usbhs_bset(priv, LPSTS, LPSTS_SUSPM, 0);
	}

	return 0;
}

/* R-Car D3 needs to release UGCTRL.PLLRESET */
static int usbhs_rcar3_power_and_pll_ctrl(struct platform_device *pdev,
					  void __iomem *base, int enable)
{
	struct usbhs_priv *priv = usbhs_pdev_to_priv(pdev);
	u32 val;
	int timeout = 1000;

	if (enable) {
		usbhs_write32(priv, UGCTRL, 0);	/* release PLLRESET */
		usbhs_rcar3_set_ugctrl2(priv,
					UGCTRL2_USB0SEL_OTG | UGCTRL2_VBUSSEL);

		usbhs_bset(priv, LPSTS, LPSTS_SUSPM, LPSTS_SUSPM);
		do {
			val = usbhs_read32(priv, UGSTS);
			udelay(1);
		} while (!(val & UGSTS_LOCK) && timeout--);
		usbhs_write32(priv, UGCTRL, UGCTRL_CONNECT);
	} else {
		usbhs_write32(priv, UGCTRL, 0);
		usbhs_bset(priv, LPSTS, LPSTS_SUSPM, 0);
		usbhs_write32(priv, UGCTRL, UGCTRL_PLLRESET);
	}

	return 0;
}

static int usbhs_rcar3_get_id(struct platform_device *pdev)
{
	return USBHS_GADGET;
}

const struct renesas_usbhs_platform_callback usbhs_rcar3_ops = {
	.power_ctrl = usbhs_rcar3_power_ctrl,
	.get_id = usbhs_rcar3_get_id,
};

const struct renesas_usbhs_platform_callback usbhs_rcar3_with_pll_ops = {
	.power_ctrl = usbhs_rcar3_power_and_pll_ctrl,
	.get_id = usbhs_rcar3_get_id,
};
