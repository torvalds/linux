/*
 * drivers/net/ibm_emac/ibm_emac_rgmii.c
 *
 * Driver for PowerPC 4xx on-chip ethernet controller, RGMII bridge support.
 *
 * Based on ocp_zmii.h/ibm_emac_zmii.h
 * Armin Kuster akuster@mvista.com
 *
 * Copyright 2004 MontaVista Software, Inc.
 * Matt Porter <mporter@kernel.crashing.org>
 *
 * Copyright (c) 2004, 2005 Zultys Technologies.
 * Eugene Surovegin <eugene.surovegin@zultys.com> or <ebs@ebshome.net>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#ifndef _IBM_EMAC_RGMII_H_
#define _IBM_EMAC_RGMII_H_

#include <linux/config.h>

/* RGMII bridge */
struct rgmii_regs {
	u32 fer;		/* Function enable register */
	u32 ssr;		/* Speed select register */
};

/* RGMII device */
struct ibm_ocp_rgmii {
	struct rgmii_regs *base;
	int users;		/* number of EMACs using this RGMII bridge */
};

#ifdef CONFIG_IBM_EMAC_RGMII
int rgmii_attach(void *emac) __init;

void __rgmii_fini(struct ocp_device *ocpdev, int input) __exit;
static inline void rgmii_fini(struct ocp_device *ocpdev, int input)
{
	if (ocpdev)
		__rgmii_fini(ocpdev, input);
}

void rgmii_set_speed(struct ocp_device *ocpdev, int input, int speed);

int __rgmii_get_regs_len(struct ocp_device *ocpdev);
static inline int rgmii_get_regs_len(struct ocp_device *ocpdev)
{
	return ocpdev ? __rgmii_get_regs_len(ocpdev) : 0;
}

void *rgmii_dump_regs(struct ocp_device *ocpdev, void *buf);
#else
# define rgmii_attach(x)	0
# define rgmii_fini(x,y)	((void)0)
# define rgmii_set_speed(x,y,z)	((void)0)
# define rgmii_get_regs_len(x)	0
# define rgmii_dump_regs(x,buf)	(buf)
#endif				/* !CONFIG_IBM_EMAC_RGMII */

#endif				/* _IBM_EMAC_RGMII_H_ */
