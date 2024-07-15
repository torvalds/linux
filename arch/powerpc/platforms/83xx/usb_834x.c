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

int __init mpc834x_usb_cfg(void)
{
	unsigned long sccr, sicrl, sicrh;
	void __iomem *immap;
	struct device_node *np = NULL;
	int port0_is_dr = 0, port1_is_dr = 0;
	const void *prop, *dr_mode;

	immap = ioremap(get_immrbase(), 0x1000);
	if (!immap)
		return -ENOMEM;

	/* Read registers */
	/* Note: DR and MPH must use the same clock setting in SCCR */
	sccr = in_be32(immap + MPC83XX_SCCR_OFFS) & ~MPC83XX_SCCR_USB_MASK;
	sicrl = in_be32(immap + MPC83XX_SICRL_OFFS) & ~MPC834X_SICRL_USB_MASK;
	sicrh = in_be32(immap + MPC83XX_SICRH_OFFS) & ~MPC834X_SICRH_USB_UTMI;

	np = of_find_compatible_node(NULL, NULL, "fsl-usb2-dr");
	if (np) {
		sccr |= MPC83XX_SCCR_USB_DRCM_11;  /* 1:3 */

		prop = of_get_property(np, "phy_type", NULL);
		port1_is_dr = 1;
		if (prop &&
		    (!strcmp(prop, "utmi") || !strcmp(prop, "utmi_wide"))) {
			sicrl |= MPC834X_SICRL_USB0 | MPC834X_SICRL_USB1;
			sicrh |= MPC834X_SICRH_USB_UTMI;
			port0_is_dr = 1;
		} else if (prop && !strcmp(prop, "serial")) {
			dr_mode = of_get_property(np, "dr_mode", NULL);
			if (dr_mode && !strcmp(dr_mode, "otg")) {
				sicrl |= MPC834X_SICRL_USB0 | MPC834X_SICRL_USB1;
				port0_is_dr = 1;
			} else {
				sicrl |= MPC834X_SICRL_USB1;
			}
		} else if (prop && !strcmp(prop, "ulpi")) {
			sicrl |= MPC834X_SICRL_USB1;
		} else {
			pr_warn("834x USB PHY type not supported\n");
		}
		of_node_put(np);
	}
	np = of_find_compatible_node(NULL, NULL, "fsl-usb2-mph");
	if (np) {
		sccr |= MPC83XX_SCCR_USB_MPHCM_11; /* 1:3 */

		prop = of_get_property(np, "port0", NULL);
		if (prop) {
			if (port0_is_dr)
				pr_warn("834x USB port0 can't be used by both DR and MPH!\n");
			sicrl &= ~MPC834X_SICRL_USB0;
		}
		prop = of_get_property(np, "port1", NULL);
		if (prop) {
			if (port1_is_dr)
				pr_warn("834x USB port1 can't be used by both DR and MPH!\n");
			sicrl &= ~MPC834X_SICRL_USB1;
		}
		of_node_put(np);
	}

	/* Write back */
	out_be32(immap + MPC83XX_SCCR_OFFS, sccr);
	out_be32(immap + MPC83XX_SICRL_OFFS, sicrl);
	out_be32(immap + MPC83XX_SICRH_OFFS, sicrh);

	iounmap(immap);
	return 0;
}
