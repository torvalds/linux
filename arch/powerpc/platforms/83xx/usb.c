/*
 * Freescale 83xx USB SOC setup code
 *
 * Copyright (C) 2007 Freescale Semiconductor, Inc.
 * Author: Li Yang
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */


#include <linux/stddef.h>
#include <linux/kernel.h>
#include <linux/errno.h>

#include <asm/io.h>
#include <asm/prom.h>
#include <sysdev/fsl_soc.h>

#include "mpc83xx.h"


#ifdef CONFIG_MPC834x
int mpc834x_usb_cfg(void)
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

	np = of_find_compatible_node(NULL, "usb", "fsl-usb2-dr");
	if (np) {
		sccr |= MPC83XX_SCCR_USB_DRCM_11;  /* 1:3 */

		prop = of_get_property(np, "phy_type", NULL);
		if (prop && (!strcmp(prop, "utmi") ||
					!strcmp(prop, "utmi_wide"))) {
			sicrl |= MPC834X_SICRL_USB0 | MPC834X_SICRL_USB1;
			sicrh |= MPC834X_SICRH_USB_UTMI;
			port1_is_dr = 1;
		} else if (prop && !strcmp(prop, "serial")) {
			dr_mode = of_get_property(np, "dr_mode", NULL);
			if (dr_mode && !strcmp(dr_mode, "otg")) {
				sicrl |= MPC834X_SICRL_USB0 | MPC834X_SICRL_USB1;
				port1_is_dr = 1;
			} else {
				sicrl |= MPC834X_SICRL_USB0;
			}
		} else if (prop && !strcmp(prop, "ulpi")) {
			sicrl |= MPC834X_SICRL_USB0;
		} else {
			printk(KERN_WARNING "834x USB PHY type not supported\n");
		}
		port0_is_dr = 1;
		of_node_put(np);
	}
	np = of_find_compatible_node(NULL, "usb", "fsl-usb2-mph");
	if (np) {
		sccr |= MPC83XX_SCCR_USB_MPHCM_11; /* 1:3 */

		prop = of_get_property(np, "port0", NULL);
		if (prop) {
			if (port0_is_dr)
				printk(KERN_WARNING
					"834x USB port0 can't be used by both DR and MPH!\n");
			sicrl &= ~MPC834X_SICRL_USB0;
		}
		prop = of_get_property(np, "port1", NULL);
		if (prop) {
			if (port1_is_dr)
				printk(KERN_WARNING
					"834x USB port1 can't be used by both DR and MPH!\n");
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
#endif /* CONFIG_MPC834x */

#ifdef CONFIG_PPC_MPC831x
int mpc831x_usb_cfg(void)
{
	u32 temp;
	void __iomem *immap, *usb_regs;
	struct device_node *np = NULL;
	const void *prop;
	struct resource res;
	int ret = 0;
#ifdef CONFIG_USB_OTG
	const void *dr_mode;
#endif

	np = of_find_compatible_node(NULL, "usb", "fsl-usb2-dr");
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
	temp = in_be32(immap + MPC83XX_SCCR_OFFS);
	temp &= ~MPC83XX_SCCR_USB_MASK;
	temp |= MPC83XX_SCCR_USB_DRCM_11;  /* 1:3 */
	out_be32(immap + MPC83XX_SCCR_OFFS, temp);

	/* Configure pin mux for ULPI.  There is no pin mux for UTMI */
	if (!strcmp(prop, "ulpi")) {
		temp = in_be32(immap + MPC83XX_SICRL_OFFS);
		temp &= ~MPC831X_SICRL_USB_MASK;
		temp |= MPC831X_SICRL_USB_ULPI;
		out_be32(immap + MPC83XX_SICRL_OFFS, temp);

		temp = in_be32(immap + MPC83XX_SICRH_OFFS);
		temp &= ~MPC831X_SICRH_USB_MASK;
		temp |= MPC831X_SICRH_USB_ULPI;
		out_be32(immap + MPC83XX_SICRH_OFFS, temp);
	}

	iounmap(immap);

	/* Map USB SOC space */
	ret = of_address_to_resource(np, 0, &res);
	if (ret) {
		of_node_put(np);
		return ret;
	}
	usb_regs = ioremap(res.start, res.end - res.start + 1);

	/* Using on-chip PHY */
	if (!strcmp(prop, "utmi_wide") ||
			!strcmp(prop, "utmi")) {
		/* Set UTMI_PHY_EN, REFSEL to 48MHZ */
		out_be32(usb_regs + FSL_USB2_CONTROL_OFFS,
				CONTROL_UTMI_PHY_EN | CONTROL_REFSEL_48MHZ);
	/* Using external UPLI PHY */
	} else if (!strcmp(prop, "ulpi")) {
		/* Set PHY_CLK_SEL to ULPI */
		temp = CONTROL_PHY_CLK_SEL_ULPI;
#ifdef CONFIG_USB_OTG
		/* Set OTG_PORT */
		dr_mode = of_get_property(np, "dr_mode", NULL);
		if (dr_mode && !strcmp(dr_mode, "otg"))
			temp |= CONTROL_OTG_PORT;
#endif /* CONFIG_USB_OTG */
		out_be32(usb_regs + FSL_USB2_CONTROL_OFFS, temp);
	} else {
		printk(KERN_WARNING "831x USB PHY type not supported\n");
		ret = -EINVAL;
	}

	iounmap(usb_regs);
	of_node_put(np);
	return ret;
}
#endif /* CONFIG_PPC_MPC831x */
