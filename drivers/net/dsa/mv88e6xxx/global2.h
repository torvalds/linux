/*
 * Marvell 88E6xxx Switch Global 2 Registers support (device address 0x1C)
 *
 * Copyright (c) 2008 Marvell Semiconductor
 *
 * Copyright (c) 2016 Vivien Didelot <vivien.didelot@savoirfairelinux.com>
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

int mv88e6xxx_g2_smi_phy_read(struct mv88e6xxx_chip *chip, int addr, int reg,
			      u16 *val);
int mv88e6xxx_g2_smi_phy_write(struct mv88e6xxx_chip *chip, int addr, int reg,
			       u16 val);
int mv88e6xxx_g2_set_switch_mac(struct mv88e6xxx_chip *chip, u8 *addr);
int mv88e6xxx_g2_get_eeprom16(struct mv88e6xxx_chip *chip,
			      struct ethtool_eeprom *eeprom, u8 *data);
int mv88e6xxx_g2_set_eeprom16(struct mv88e6xxx_chip *chip,
			      struct ethtool_eeprom *eeprom, u8 *data);
int mv88e6xxx_g2_setup(struct mv88e6xxx_chip *chip);
int mv88e6xxx_g2_irq_setup(struct mv88e6xxx_chip *chip);
void mv88e6xxx_g2_irq_free(struct mv88e6xxx_chip *chip);

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
					    int addr, int reg, u16 *val)
{
	return -EOPNOTSUPP;
}

static inline int mv88e6xxx_g2_smi_phy_write(struct mv88e6xxx_chip *chip,
					     int addr, int reg, u16 val)
{
	return -EOPNOTSUPP;
}

static inline int mv88e6xxx_g2_set_switch_mac(struct mv88e6xxx_chip *chip,
					      u8 *addr)
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

#endif /* CONFIG_NET_DSA_MV88E6XXX_GLOBAL2 */

#endif /* _MV88E6XXX_GLOBAL2_H */
