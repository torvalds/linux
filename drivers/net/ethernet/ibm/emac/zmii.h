/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * drivers/net/ethernet/ibm/emac/zmii.h
 *
 * Driver for PowerPC 4xx on-chip ethernet controller, ZMII bridge support.
 *
 * Copyright 2007 Benjamin Herrenschmidt, IBM Corp.
 *                <benh@kernel.crashing.org>
 *
 * Based on the arch/ppc version of the driver:
 *
 * Copyright (c) 2004, 2005 Zultys Technologies.
 * Eugene Surovegin <eugene.surovegin@zultys.com> or <ebs@ebshome.net>
 *
 * Based on original work by
 *      Armin Kuster <akuster@mvista.com>
 * 	Copyright 2001 MontaVista Softare Inc.
 */
#ifndef __IBM_NEWEMAC_ZMII_H
#define __IBM_NEWEMAC_ZMII_H

/* ZMII bridge registers */
struct zmii_regs {
	u32 fer;		/* Function enable reg */
	u32 ssr;		/* Speed select reg */
	u32 smiirs;		/* SMII status reg */
};

/* ZMII device */
struct zmii_instance {
	struct zmii_regs __iomem	*base;

	/* Only one EMAC whacks us at a time */
	struct mutex			lock;

	/* subset of PHY_MODE_XXXX */
	int				mode;

	/* number of EMACs using this ZMII bridge */
	int				users;

	/* FER value left by firmware */
	u32				fer_save;

	/* OF device instance */
	struct platform_device		*ofdev;
};

#ifdef CONFIG_IBM_EMAC_ZMII

int zmii_init(void);
void zmii_exit(void);
int zmii_attach(struct platform_device *ofdev, int input,
		phy_interface_t *mode);
void zmii_detach(struct platform_device *ofdev, int input);
void zmii_get_mdio(struct platform_device *ofdev, int input);
void zmii_put_mdio(struct platform_device *ofdev, int input);
void zmii_set_speed(struct platform_device *ofdev, int input, int speed);
int zmii_get_regs_len(struct platform_device *ocpdev);
void *zmii_dump_regs(struct platform_device *ofdev, void *buf);

#else
# define zmii_init()		0
# define zmii_exit()		do { } while(0)
# define zmii_attach(x,y,z)	(-ENXIO)
# define zmii_detach(x,y)	do { } while(0)
# define zmii_get_mdio(x,y)	do { } while(0)
# define zmii_put_mdio(x,y)	do { } while(0)
# define zmii_set_speed(x,y,z)	do { } while(0)
# define zmii_get_regs_len(x)	0
# define zmii_dump_regs(x,buf)	(buf)
#endif				/* !CONFIG_IBM_EMAC_ZMII */

#endif /* __IBM_NEWEMAC_ZMII_H */
