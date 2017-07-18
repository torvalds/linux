/*
 * Marvell 88E6xxx PHY access
 *
 * Copyright (c) 2008 Marvell Semiconductor
 *
 * Copyright (c) 2017 Andrew Lunn <andrew@lunn.ch>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef _MV88E6XXX_PHY_H
#define _MV88E6XXX_PHY_H

#define MV88E6XXX_PHY_PAGE		0x16
#define MV88E6XXX_PHY_PAGE_COPPER	0x00

/* Page 0, Register 16: Copper Specific Control Register 1 */
#define MV88E6XXX_PHY_CSCTL1					16
#define MV88E6352_PHY_CSCTL1_ENERGY_DETECT_MASK			0x0300
#define MV88E6352_PHY_CSCTL1_ENERGY_DETECT_OFF_MASK		0x0100 /* 0x */
#define MV88E6352_PHY_CSCTL1_ENERGY_DETECT_SENSE_RCV		0x0200
#define MV88E6352_PHY_CSCTL1_ENERGY_DETECT_SENSE_NLP		0x0300
#define MV88E6390_PHY_CSCTL1_ENERGY_DETECT_MASK			0x0380
#define MV88E6390_PHY_CSCTL1_ENERGY_DETECT_OFF_MASK		0x0180 /* 0xx */
#define MV88E6390_PHY_CSCTL1_ENERGY_DETECT_SENSE_RCV_AUTO	0x0200
#define MV88E6390_PHY_CSCTL1_ENERGY_DETECT_SENSE_RCV_SW		0x0280
#define MV88E6390_PHY_CSCTL1_ENERGY_DETECT_SENSE_NLP_AUTO	0x0300
#define MV88E6390_PHY_CSCTL1_ENERGY_DETECT_SENSE_NLP_SW		0x0380

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

int mv88e6352_phy_energy_detect_read(struct mv88e6xxx_chip *chip, int phy,
				     struct ethtool_eee *eee);
int mv88e6352_phy_energy_detect_write(struct mv88e6xxx_chip *chip, int phy,
				      struct ethtool_eee *eee);
int mv88e6390_phy_energy_detect_read(struct mv88e6xxx_chip *chip, int phy,
				     struct ethtool_eee *eee);
int mv88e6390_phy_energy_detect_write(struct mv88e6xxx_chip *chip, int phy,
				      struct ethtool_eee *eee);

#endif /*_MV88E6XXX_PHY_H */
