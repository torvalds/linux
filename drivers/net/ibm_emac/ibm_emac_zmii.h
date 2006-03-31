/*
 * drivers/net/ibm_emac/ibm_emac_zmii.h
 *
 * Driver for PowerPC 4xx on-chip ethernet controller, ZMII bridge support.
 *
 * Copyright (c) 2004, 2005 Zultys Technologies.
 * Eugene Surovegin <eugene.surovegin@zultys.com> or <ebs@ebshome.net>
 *
 * Based on original work by
 *      Armin Kuster <akuster@mvista.com>
 * 	Copyright 2001 MontaVista Softare Inc.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 */
#ifndef _IBM_EMAC_ZMII_H_
#define _IBM_EMAC_ZMII_H_

#include <linux/config.h>
#include <linux/init.h>
#include <asm/ocp.h>

/* ZMII bridge registers */
struct zmii_regs {
	u32 fer;		/* Function enable reg */
	u32 ssr;		/* Speed select reg */
	u32 smiirs;		/* SMII status reg */
};

/* ZMII device */
struct ibm_ocp_zmii {
	struct zmii_regs __iomem *base;
	int mode;		/* subset of PHY_MODE_XXXX */
	int users;		/* number of EMACs using this ZMII bridge */
	u32 fer_save;		/* FER value left by firmware */
};

#ifdef CONFIG_IBM_EMAC_ZMII
int zmii_attach(void *emac) __init;

void __zmii_fini(struct ocp_device *ocpdev, int input) __exit;
static inline void zmii_fini(struct ocp_device *ocpdev, int input)
{
	if (ocpdev)
		__zmii_fini(ocpdev, input);
}

void __zmii_enable_mdio(struct ocp_device *ocpdev, int input);
static inline void zmii_enable_mdio(struct ocp_device *ocpdev, int input)
{
	if (ocpdev)
		__zmii_enable_mdio(ocpdev, input);
}

void __zmii_set_speed(struct ocp_device *ocpdev, int input, int speed);
static inline void zmii_set_speed(struct ocp_device *ocpdev, int input,
				  int speed)
{
	if (ocpdev)
		__zmii_set_speed(ocpdev, input, speed);
}

int __zmii_get_regs_len(struct ocp_device *ocpdev);
static inline int zmii_get_regs_len(struct ocp_device *ocpdev)
{
	return ocpdev ? __zmii_get_regs_len(ocpdev) : 0;
}

void *zmii_dump_regs(struct ocp_device *ocpdev, void *buf);

#else
# define zmii_attach(x)		0
# define zmii_fini(x,y)		((void)0)
# define zmii_enable_mdio(x,y)	((void)0)
# define zmii_set_speed(x,y,z)	((void)0)
# define zmii_get_regs_len(x)	0
# define zmii_dump_regs(x,buf)	(buf)
#endif				/* !CONFIG_IBM_EMAC_ZMII */

#endif				/* _IBM_EMAC_ZMII_H_ */
