/*
 * Marvell 88E6xxx Switch Global 2 Registers support (device address 0x1C)
 *
 * Copyright (c) 2008 Marvell Semiconductor
 *
 * Copyright (c) 2016-2017 Savoir-faire Linux Inc.
 *	Vivien Didelot <vivien.didelot@savoirfairelinux.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef _MV88E6XXX_GLOBAL2_H
#define _MV88E6XXX_GLOBAL2_H

#include "mv88e6xxx.h"

#ifdef CONFIG_NET_DSA_MV88E6XXX_GLOBAL2

static inline int mv88e6xxx_g2_require(struct mv88e6xxx_chip *chip)
{
	return 0;
}

int mv88e6xxx_g2_smi_phy_read(struct mv88e6xxx_chip *chip,
			      struct mii_bus *bus,
			      int addr, int reg, u16 *val);
int mv88e6xxx_g2_smi_phy_write(struct mv88e6xxx_chip *chip,
			       struct mii_bus *bus,
			       int addr, int reg, u16 val);
int mv88e6xxx_g2_set_switch_mac(struct mv88e6xxx_chip *chip, u8 *addr);

int mv88e6xxx_g2_get_eeprom8(struct mv88e6xxx_chip *chip,
			     struct ethtool_eeprom *eeprom, u8 *data);
int mv88e6xxx_g2_set_eeprom8(struct mv88e6xxx_chip *chip,
			     struct ethtool_eeprom *eeprom, u8 *data);

int mv88e6xxx_g2_get_eeprom16(struct mv88e6xxx_chip *chip,
			      struct ethtool_eeprom *eeprom, u8 *data);
int mv88e6xxx_g2_set_eeprom16(struct mv88e6xxx_chip *chip,
			      struct ethtool_eeprom *eeprom, u8 *data);

int mv88e6xxx_g2_pvt_write(struct mv88e6xxx_chip *chip, int src_dev,
			   int src_port, u16 data);
int mv88e6xxx_g2_misc_4_bit_port(struct mv88e6xxx_chip *chip);

int mv88e6xxx_g2_setup(struct mv88e6xxx_chip *chip);
int mv88e6xxx_g2_irq_setup(struct mv88e6xxx_chip *chip);
void mv88e6xxx_g2_irq_free(struct mv88e6xxx_chip *chip);
int mv88e6095_g2_mgmt_rsvd2cpu(struct mv88e6xxx_chip *chip);

extern const struct mv88e6xxx_irq_ops mv88e6097_watchdog_ops;
extern const struct mv88e6xxx_irq_ops mv88e6390_watchdog_ops;

#else /* !CONFIG_NET_DSA_MV88E6XXX_GLOBAL2 */

static inline int mv88e6xxx_g2_require(struct mv88e6xxx_chip *chip)
{
	if (mv88e6xxx_has(chip, MV88E6XXX_FLAG_GLOBAL2)) {
		dev_err(chip->dev, "this chip requires CONFIG_NET_DSA_MV88E6XXX_GLOBAL2 enabled\n");
		return -EOPNOTSUPP;
	}

	return 0;
}

static inline int mv88e6xxx_g2_smi_phy_read(struct mv88e6xxx_chip *chip,
					    struct mii_bus *bus,
					    int addr, int reg, u16 *val)
{
	return -EOPNOTSUPP;
}

static inline int mv88e6xxx_g2_smi_phy_write(struct mv88e6xxx_chip *chip,
					     struct mii_bus *bus,
					     int addr, int reg, u16 val)
{
	return -EOPNOTSUPP;
}

static inline int mv88e6xxx_g2_set_switch_mac(struct mv88e6xxx_chip *chip,
					      u8 *addr)
{
	return -EOPNOTSUPP;
}

static inline int mv88e6xxx_g2_get_eeprom8(struct mv88e6xxx_chip *chip,
					   struct ethtool_eeprom *eeprom,
					   u8 *data)
{
	return -EOPNOTSUPP;
}

static inline int mv88e6xxx_g2_set_eeprom8(struct mv88e6xxx_chip *chip,
					   struct ethtool_eeprom *eeprom,
					   u8 *data)
{
	return -EOPNOTSUPP;
}

static inline int mv88e6xxx_g2_get_eeprom16(struct mv88e6xxx_chip *chip,
					    struct ethtool_eeprom *eeprom,
					    u8 *data)
{
	return -EOPNOTSUPP;
}

static inline int mv88e6xxx_g2_set_eeprom16(struct mv88e6xxx_chip *chip,
					    struct ethtool_eeprom *eeprom,
					    u8 *data)
{
	return -EOPNOTSUPP;
}

static inline int mv88e6xxx_g2_pvt_write(struct mv88e6xxx_chip *chip,
					 int src_dev, int src_port, u16 data)
{
	return -EOPNOTSUPP;
}

static inline int mv88e6xxx_g2_misc_4_bit_port(struct mv88e6xxx_chip *chip)
{
	return -EOPNOTSUPP;
}

static inline int mv88e6xxx_g2_setup(struct mv88e6xxx_chip *chip)
{
	return -EOPNOTSUPP;
}

static inline int mv88e6xxx_g2_irq_setup(struct mv88e6xxx_chip *chip)
{
	return -EOPNOTSUPP;
}

static inline void mv88e6xxx_g2_irq_free(struct mv88e6xxx_chip *chip)
{
}

static inline int mv88e6095_g2_mgmt_rsvd2cpu(struct mv88e6xxx_chip *chip)
{
	return -EOPNOTSUPP;
}

static const struct mv88e6xxx_irq_ops mv88e6097_watchdog_ops = {};
static const struct mv88e6xxx_irq_ops mv88e6390_watchdog_ops = {};

#endif /* CONFIG_NET_DSA_MV88E6XXX_GLOBAL2 */

#endif /* _MV88E6XXX_GLOBAL2_H */
