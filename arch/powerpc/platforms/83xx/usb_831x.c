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

int __init mpc831x_usb_cfg(void)
{
	u32 temp;
	void __iomem *immap, *usb_regs;
	struct device_node *np = NULL;
	struct device_node *immr_node = NULL;
	const void *prop;
	struct resource res;
	int ret = 0;
#ifdef CONFIG_USB_OTG
	const void *dr_mode;
#endif

	np = of_find_compatible_node(NULL, NULL, "fsl-usb2-dr");
	if (!np)
		return -ENODEV;
	prop = of_get_property(np, "phy_type", NULL);

	/* Map IMMR space for pin and clock settings */
	immap = ioremap(get_immrbase(), 0x1000);
	if (!immap) {
		of_node_put(np);
		return -ENOMEM;
	}

	/* Configure clock */
	immr_node = of_get_parent(np);
	if (immr_node && (of_device_is_compatible(immr_node, "fsl,mpc8315-immr") ||
			  of_device_is_compatible(immr_node, "fsl,mpc8308-immr")))
		clrsetbits_be32(immap + MPC83XX_SCCR_OFFS,
				MPC8315_SCCR_USB_MASK,
				MPC8315_SCCR_USB_DRCM_01);
	else
		clrsetbits_be32(immap + MPC83XX_SCCR_OFFS,
				MPC83XX_SCCR_USB_MASK,
				MPC83XX_SCCR_USB_DRCM_11);

	/* Configure pin mux for ULPI.  There is no pin mux for UTMI */
	if (prop && !strcmp(prop, "ulpi")) {
		if (of_device_is_compatible(immr_node, "fsl,mpc8308-immr")) {
			clrsetbits_be32(immap + MPC83XX_SICRH_OFFS,
					MPC8308_SICRH_USB_MASK,
					MPC8308_SICRH_USB_ULPI);
		} else if (of_device_is_compatible(immr_node, "fsl,mpc8315-immr")) {
			clrsetbits_be32(immap + MPC83XX_SICRL_OFFS,
					MPC8315_SICRL_USB_MASK,
					MPC8315_SICRL_USB_ULPI);
			clrsetbits_be32(immap + MPC83XX_SICRH_OFFS,
					MPC8315_SICRH_USB_MASK,
					MPC8315_SICRH_USB_ULPI);
		} else {
			clrsetbits_be32(immap + MPC83XX_SICRL_OFFS,
					MPC831X_SICRL_USB_MASK,
					MPC831X_SICRL_USB_ULPI);
			clrsetbits_be32(immap + MPC83XX_SICRH_OFFS,
					MPC831X_SICRH_USB_MASK,
					MPC831X_SICRH_USB_ULPI);
		}
	}

	iounmap(immap);

	of_node_put(immr_node);

	/* Map USB SOC space */
	ret = of_address_to_resource(np, 0, &res);
	if (ret) {
		of_node_put(np);
		return ret;
	}
	usb_regs = ioremap(res.start, resource_size(&res));

	/* Using on-chip PHY */
	if (prop && (!strcmp(prop, "utmi_wide") || !strcmp(prop, "utmi"))) {
		u32 refsel;

		if (of_device_is_compatible(immr_node, "fsl,mpc8308-immr"))
			goto out;

		if (of_device_is_compatible(immr_node, "fsl,mpc8315-immr"))
			refsel = CONTROL_REFSEL_24MHZ;
		else
			refsel = CONTROL_REFSEL_48MHZ;
		/* Set UTMI_PHY_EN and REFSEL */
		out_be32(usb_regs + FSL_USB2_CONTROL_OFFS,
			 CONTROL_UTMI_PHY_EN | refsel);
	/* Using external UPLI PHY */
	} else if (prop && !strcmp(prop, "ulpi")) {
		/* Set PHY_CLK_SEL to ULPI */
		temp = CONTROL_PHY_CLK_SEL_ULPI;
#ifdef CONFIG_USB_OTG
		/* Set OTG_PORT */
		if (!of_device_is_compatible(immr_node, "fsl,mpc8308-immr")) {
			dr_mode = of_get_property(np, "dr_mode", NULL);
			if (dr_mode && !strcmp(dr_mode, "otg"))
				temp |= CONTROL_OTG_PORT;
		}
#endif /* CONFIG_USB_OTG */
		out_be32(usb_regs + FSL_USB2_CONTROL_OFFS, temp);
	} else {
		pr_warn("831x USB PHY type not supported\n");
		ret = -EINVAL;
	}

out:
	iounmap(usb_regs);
	of_node_put(np);
	return ret;
}
