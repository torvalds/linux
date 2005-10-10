/*
 * drivers/net/ibm_emac/ibm_emac_phy.h
 *
 * Driver for PowerPC 4xx on-chip ethernet controller, PHY support
 *
 * Benjamin Herrenschmidt <benh@kernel.crashing.org>
 * February 2003
 *
 * Minor additions by Eugene Surovegin <ebs@ebshome.net>, 2004
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 * This file basically duplicates sungem_phy.{c,h} with different PHYs
 * supported. I'm looking into merging that in a single mii layer more
 * flexible than mii.c 
 */

#ifndef _IBM_OCP_PHY_H_
#define _IBM_OCP_PHY_H_

struct mii_phy;

/* Operations supported by any kind of PHY */
struct mii_phy_ops {
	int (*init) (struct mii_phy * phy);
	int (*suspend) (struct mii_phy * phy, int wol_options);
	int (*setup_aneg) (struct mii_phy * phy, u32 advertise);
	int (*setup_forced) (struct mii_phy * phy, int speed, int fd);
	int (*poll_link) (struct mii_phy * phy);
	int (*read_link) (struct mii_phy * phy);
};

/* Structure used to statically define an mii/gii based PHY */
struct mii_phy_def {
	u32 phy_id;		/* Concatenated ID1 << 16 | ID2 */
	u32 phy_id_mask;	/* Significant bits */
	u32 features;		/* Ethtool SUPPORTED_* defines or 
				   0 for autodetect */
	int magic_aneg;		/* Autoneg does all speed test for us */
	const char *name;
	const struct mii_phy_ops *ops;
};

/* An instance of a PHY, partially borrowed from mii_if_info */
struct mii_phy {
	struct mii_phy_def *def;
	u32 advertising;	/* Ethtool ADVERTISED_* defines */
	u32 features;		/* Copied from mii_phy_def.features 
				   or determined automaticaly */
	int address;		/* PHY address */
	int mode;		/* PHY mode */

	/* 1: autoneg enabled, 0: disabled */
	int autoneg;

	/* forced speed & duplex (no autoneg)
	 * partner speed & duplex & pause (autoneg)
	 */
	int speed;
	int duplex;
	int pause;
	int asym_pause;

	/* Provided by host chip */
	struct net_device *dev;
	int (*mdio_read) (struct net_device * dev, int addr, int reg);
	void (*mdio_write) (struct net_device * dev, int addr, int reg,
			    int val);
};

/* Pass in a struct mii_phy with dev, mdio_read and mdio_write
 * filled, the remaining fields will be filled on return
 */
int mii_phy_probe(struct mii_phy *phy, int address);
int mii_reset_phy(struct mii_phy *phy);

#endif				/* _IBM_OCP_PHY_H_ */
