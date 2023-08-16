// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Freescale 83xx USB SOC setup code
 *
 * Copyright (C) 2007 Freescale Semiconductor, Inc.
 * Author: Li Yang
 */

#include <linux/stddef.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/io.h>

#include <sysdev/fsl_soc.h>

#include "mpc83xx.h"

int __init mpc837x_usb_cfg(void)
{
	void __iomem *immap;
	struct device_node *np = NULL;
	const void *prop;
	int ret = 0;

	np = of_find_compatible_node(NULL, NULL, "fsl-usb2-dr");
	if (!np || !of_device_is_available(np)) {
		of_node_put(np);
		return -ENODEV;
	}
	prop = of_get_property(np, "phy_type", NULL);

	if (!prop || (strcmp(prop, "ulpi") && strcmp(prop, "serial"))) {
		pr_warn("837x USB PHY type not supported\n");
		of_node_put(np);
		return -EINVAL;
	}

	/* Map IMMR space for pin and clock settings */
	immap = ioremap(get_immrbase(), 0x1000);
	if (!immap) {
		of_node_put(np);
		return -ENOMEM;
	}

	/* Configure clock */
	clrsetbits_be32(immap + MPC83XX_SCCR_OFFS, MPC837X_SCCR_USB_DRCM_11,
			MPC837X_SCCR_USB_DRCM_11);

	/* Configure pin mux for ULPI/serial */
	clrsetbits_be32(immap + MPC83XX_SICRL_OFFS, MPC837X_SICRL_USB_MASK,
			MPC837X_SICRL_USB_ULPI);

	iounmap(immap);
	of_node_put(np);
	return ret;
}
