/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * drivers/net/ethernet/ibm/emac/rgmii.h
 *
 * Driver for PowerPC 4xx on-chip ethernet controller, RGMII bridge support.
 *
 * Copyright 2007 Benjamin Herrenschmidt, IBM Corp.
 *                <benh@kernel.crashing.org>
 *
 * Based on the arch/ppc version of the driver:
 *
 * Based on ocp_zmii.h/ibm_emac_zmii.h
 * Armin Kuster akuster@mvista.com
 *
 * Copyright 2004 MontaVista Software, Inc.
 * Matt Porter <mporter@kernel.crashing.org>
 *
 * Copyright (c) 2004, 2005 Zultys Technologies.
 * Eugene Surovegin <eugene.surovegin@zultys.com> or <ebs@ebshome.net>
 */

#ifndef __IBM_NEWEMAC_RGMII_H
#define __IBM_NEWEMAC_RGMII_H

/* RGMII bridge type */
#define RGMII_STANDARD		0
#define RGMII_AXON		1

/* RGMII bridge */
struct rgmii_regs {
	u32 fer;		/* Function enable register */
	u32 ssr;		/* Speed select register */
};

/* RGMII device */
struct rgmii_instance {
	struct rgmii_regs __iomem	*base;

	/* RGMII bridge flags */
	int				flags;
#define EMAC_RGMII_FLAG_HAS_MDIO	0x00000001

	/* Only one EMAC whacks us at a time */
	struct mutex			lock;

	/* number of EMACs using this RGMII bridge */
	int				users;

	/* OF device instance */
	struct platform_device		*ofdev;
};

#ifdef CONFIG_IBM_EMAC_RGMII

int rgmii_init(void);
void rgmii_exit(void);
int rgmii_attach(struct platform_device *ofdev, int input, int mode);
void rgmii_detach(struct platform_device *ofdev, int input);
void rgmii_get_mdio(struct platform_device *ofdev, int input);
void rgmii_put_mdio(struct platform_device *ofdev, int input);
void rgmii_set_speed(struct platform_device *ofdev, int input, int speed);
int rgmii_get_regs_len(struct platform_device *ofdev);
void *rgmii_dump_regs(struct platform_device *ofdev, void *buf);

#else

# define rgmii_init()		0
# define rgmii_exit()		do { } while(0)
# define rgmii_attach(x,y,z)	(-ENXIO)
# define rgmii_detach(x,y)	do { } while(0)
# define rgmii_get_mdio(o,i)	do { } while (0)
# define rgmii_put_mdio(o,i)	do { } while (0)
# define rgmii_set_speed(x,y,z)	do { } while(0)
# define rgmii_get_regs_len(x)	0
# define rgmii_dump_regs(x,buf)	(buf)
#endif				/* !CONFIG_IBM_EMAC_RGMII */

#endif /* __IBM_NEWEMAC_RGMII_H */
