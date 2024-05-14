/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Marvell 88E6xxx PHY access
 *
 * Copyright (c) 2008 Marvell Semiconductor
 *
 * Copyright (c) 2017 Andrew Lunn <andrew@lunn.ch>
 */

#ifndef _MV88E6XXX_PHY_H
#define _MV88E6XXX_PHY_H

#define MV88E6XXX_PHY_PAGE		0x16
#define MV88E6XXX_PHY_PAGE_COPPER	0x00

/* PHY Registers accesses implementations */
int mv88e6165_phy_read(struct mv88e6xxx_chip *chip, struct mii_bus *bus,
		       int addr, int reg, u16 *val);
int mv88e6165_phy_write(struct mv88e6xxx_chip *chip, struct mii_bus *bus,
			int addr, int reg, u16 val);
int mv88e6185_phy_ppu_read(struct mv88e6xxx_chip *chip, struct mii_bus *bus,
			   int addr, int reg, u16 *val);
int mv88e6185_phy_ppu_write(struct mv88e6xxx_chip *chip, struct mii_bus *bus,
			    int addr, int reg, u16 val);

/* Generic PHY operations */
int mv88e6xxx_phy_read(struct mv88e6xxx_chip *chip, int phy,
		       int reg, u16 *val);
int mv88e6xxx_phy_write(struct mv88e6xxx_chip *chip, int phy,
			int reg, u16 val);
int mv88e6xxx_phy_page_read(struct mv88e6xxx_chip *chip, int phy,
			    u8 page, int reg, u16 *val);
int mv88e6xxx_phy_page_write(struct mv88e6xxx_chip *chip, int phy,
			     u8 page, int reg, u16 val);
void mv88e6xxx_phy_init(struct mv88e6xxx_chip *chip);
void mv88e6xxx_phy_destroy(struct mv88e6xxx_chip *chip);
int mv88e6xxx_phy_setup(struct mv88e6xxx_chip *chip);

#endif /*_MV88E6XXX_PHY_H */
