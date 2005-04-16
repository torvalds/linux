/* 
 * drivers/net/gianfar_phy.h
 *
 * Gianfar Ethernet Driver -- PHY handling
 * Driver for FEC on MPC8540 and TSEC on MPC8540/MPC8560
 * Based on 8260_io/fcc_enet.c
 *
 * Author: Andy Fleming
 * Maintainer: Kumar Gala (kumar.gala@freescale.com)
 *
 * Copyright (c) 2002-2004 Freescale Semiconductor, Inc.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 */
#ifndef __GIANFAR_PHY_H
#define __GIANFAR_PHY_H

#define MII_end ((u32)-2)
#define MII_read ((u32)-1)

#define MIIMIND_BUSY            0x00000001
#define MIIMIND_NOTVALID        0x00000004

#define GFAR_AN_TIMEOUT         2000

/* 1000BT control (Marvell & BCM54xx at least) */
#define MII_1000BASETCONTROL			0x09
#define MII_1000BASETCONTROL_FULLDUPLEXCAP	0x0200
#define MII_1000BASETCONTROL_HALFDUPLEXCAP	0x0100

/* Cicada Extended Control Register 1 */
#define MII_CIS8201_EXT_CON1           0x17
#define MII_CIS8201_EXTCON1_INIT       0x0000

/* Cicada Interrupt Mask Register */
#define MII_CIS8201_IMASK		0x19
#define MII_CIS8201_IMASK_IEN		0x8000
#define MII_CIS8201_IMASK_SPEED	0x4000
#define MII_CIS8201_IMASK_LINK		0x2000
#define MII_CIS8201_IMASK_DUPLEX	0x1000
#define MII_CIS8201_IMASK_MASK		0xf000

/* Cicada Interrupt Status Register */
#define MII_CIS8201_ISTAT		0x1a
#define MII_CIS8201_ISTAT_STATUS	0x8000
#define MII_CIS8201_ISTAT_SPEED	0x4000
#define MII_CIS8201_ISTAT_LINK		0x2000
#define MII_CIS8201_ISTAT_DUPLEX	0x1000

/* Cicada Auxiliary Control/Status Register */
#define MII_CIS8201_AUX_CONSTAT        0x1c
#define MII_CIS8201_AUXCONSTAT_INIT    0x0004
#define MII_CIS8201_AUXCONSTAT_DUPLEX  0x0020
#define MII_CIS8201_AUXCONSTAT_SPEED   0x0018
#define MII_CIS8201_AUXCONSTAT_GBIT    0x0010
#define MII_CIS8201_AUXCONSTAT_100     0x0008
                                                                                
/* 88E1011 PHY Status Register */
#define MII_M1011_PHY_SPEC_STATUS		0x11
#define MII_M1011_PHY_SPEC_STATUS_1000		0x8000
#define MII_M1011_PHY_SPEC_STATUS_100		0x4000
#define MII_M1011_PHY_SPEC_STATUS_SPD_MASK	0xc000
#define MII_M1011_PHY_SPEC_STATUS_FULLDUPLEX	0x2000
#define MII_M1011_PHY_SPEC_STATUS_RESOLVED	0x0800
#define MII_M1011_PHY_SPEC_STATUS_LINK		0x0400

#define MII_M1011_IEVENT		0x13
#define MII_M1011_IEVENT_CLEAR		0x0000

#define MII_M1011_IMASK			0x12
#define MII_M1011_IMASK_INIT		0x6400
#define MII_M1011_IMASK_CLEAR		0x0000

#define MII_DM9161_SCR		0x10
#define MII_DM9161_SCR_INIT	0x0610

/* DM9161 Specified Configuration and Status Register */
#define MII_DM9161_SCSR	0x11
#define MII_DM9161_SCSR_100F	0x8000
#define MII_DM9161_SCSR_100H	0x4000
#define MII_DM9161_SCSR_10F	0x2000
#define MII_DM9161_SCSR_10H	0x1000

/* DM9161 Interrupt Register */
#define MII_DM9161_INTR	0x15
#define MII_DM9161_INTR_PEND		0x8000
#define MII_DM9161_INTR_DPLX_MASK	0x0800
#define MII_DM9161_INTR_SPD_MASK	0x0400
#define MII_DM9161_INTR_LINK_MASK	0x0200
#define MII_DM9161_INTR_MASK		0x0100
#define MII_DM9161_INTR_DPLX_CHANGE	0x0010
#define MII_DM9161_INTR_SPD_CHANGE	0x0008
#define MII_DM9161_INTR_LINK_CHANGE	0x0004
#define MII_DM9161_INTR_INIT 		0x0000
#define MII_DM9161_INTR_STOP	\
(MII_DM9161_INTR_DPLX_MASK | MII_DM9161_INTR_SPD_MASK \
 | MII_DM9161_INTR_LINK_MASK | MII_DM9161_INTR_MASK)

/* DM9161 10BT Configuration/Status */
#define MII_DM9161_10BTCSR	0x12
#define MII_DM9161_10BTCSR_INIT	0x7800

#define MII_BASIC_FEATURES	(SUPPORTED_10baseT_Half | \
				 SUPPORTED_10baseT_Full | \
				 SUPPORTED_100baseT_Half | \
				 SUPPORTED_100baseT_Full | \
				 SUPPORTED_Autoneg | \
				 SUPPORTED_TP | \
				 SUPPORTED_MII)

#define MII_GBIT_FEATURES	(MII_BASIC_FEATURES | \
				 SUPPORTED_1000baseT_Half | \
				 SUPPORTED_1000baseT_Full)

#define MII_READ_COMMAND       0x00000001

#define MII_INTERRUPT_DISABLED 0x0
#define MII_INTERRUPT_ENABLED 0x1
/* Taken from mii_if_info and sungem_phy.h */
struct gfar_mii_info {
	/* Information about the PHY type */
	/* And management functions */
	struct phy_info *phyinfo;

	/* forced speed & duplex (no autoneg)
	 * partner speed & duplex & pause (autoneg)
	 */
	int speed;
	int duplex;
	int pause;

	/* The most recently read link state */
	int link;

	/* Enabled Interrupts */
	u32 interrupts;

	u32 advertising;
	int autoneg;
	int mii_id;

	/* private data pointer */
	/* For use by PHYs to maintain extra state */
	void *priv;

	/* Provided by host chip */
	struct net_device *dev;

	/* A lock to ensure that only one thing can read/write
	 * the MDIO bus at a time */
	spinlock_t mdio_lock;

	/* Provided by ethernet driver */
	int (*mdio_read) (struct net_device *dev, int mii_id, int reg);
	void (*mdio_write) (struct net_device *dev, int mii_id, int reg, int val);
};

/* struct phy_info: a structure which defines attributes for a PHY
 *
 * id will contain a number which represents the PHY.  During
 * startup, the driver will poll the PHY to find out what its
 * UID--as defined by registers 2 and 3--is.  The 32-bit result
 * gotten from the PHY will be ANDed with phy_id_mask to
 * discard any bits which may change based on revision numbers
 * unimportant to functionality
 *
 * There are 6 commands which take a gfar_mii_info structure.
 * Each PHY must declare config_aneg, and read_status.
 */
struct phy_info {
	u32 phy_id;
	char *name;
	unsigned int phy_id_mask;
	u32 features;

	/* Called to initialize the PHY */
	int (*init)(struct gfar_mii_info *mii_info);

	/* Called to suspend the PHY for power */
	int (*suspend)(struct gfar_mii_info *mii_info);

	/* Reconfigures autonegotiation (or disables it) */
	int (*config_aneg)(struct gfar_mii_info *mii_info);

	/* Determines the negotiated speed and duplex */
	int (*read_status)(struct gfar_mii_info *mii_info);

	/* Clears any pending interrupts */
	int (*ack_interrupt)(struct gfar_mii_info *mii_info);

	/* Enables or disables interrupts */
	int (*config_intr)(struct gfar_mii_info *mii_info);

	/* Clears up any memory if needed */
	void (*close)(struct gfar_mii_info *mii_info);
};

struct phy_info *get_phy_info(struct gfar_mii_info *mii_info);
int read_phy_reg(struct net_device *dev, int mii_id, int regnum);
void write_phy_reg(struct net_device *dev, int mii_id, int regnum, int value);
void mii_clear_phy_interrupt(struct gfar_mii_info *mii_info);
void mii_configure_phy_interrupt(struct gfar_mii_info *mii_info, u32 interrupts);

struct dm9161_private {
	struct timer_list timer;
	int resetdone;
};

#endif /* GIANFAR_PHY_H */
