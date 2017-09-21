/*
 * Renesas USB driver R-Car Gen. 3 initialization and power control
 *
 * Copyright (C) 2016 Renesas Electronics Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/delay.h>
#include <linux/io.h>
#include "common.h"
#include "rcar3.h"

#define LPSTS		0x102
#define UGCTRL2		0x184	/* 32-bit register */

/* Low Power Status register (LPSTS) */
#define LPSTS_SUSPM	0x4000

/*
 * USB General control register 2 (UGCTRL2)
 * Remarks: bit[31:11] and bit[9:6] should be 0
 */
#define UGCTRL2_RESERVED_3	0x00000001	/* bit[3:0] should be B'0001 */
#define UGCTRL2_USB0SEL_OTG	0x00000030
#define UGCTRL2_VBUSSEL		0x00000400

static void usbhs_write32(struct usbhs_priv *priv, u32 reg, u32 data)
{
	iowrite32(data, priv->base + reg);
}

static int usbhs_rcar3_power_ctrl(struct platform_device *pdev,
				void __iomem *base, int enable)
{
	struct usbhs_priv *priv = usbhs_pdev_to_priv(pdev);

	usbhs_write32(priv, UGCTRL2, UGCTRL2_RESERVED_3 | UGCTRL2_USB0SEL_OTG |
		      UGCTRL2_VBUSSEL);

	if (enable) {
		usbhs_bset(priv, LPSTS, LPSTS_SUSPM, LPSTS_SUSPM);
		/* The controller on R-Car Gen3 needs to wait up to 45 usec */
		udelay(45);
	} else {
		usbhs_bset(priv, LPSTS, LPSTS_SUSPM, 0);
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
