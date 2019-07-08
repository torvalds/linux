// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Marvell 88E6xxx System Management Interface (SMI) support
 *
 * Copyright (c) 2008 Marvell Semiconductor
 *
 * Copyright (c) 2019 Vivien Didelot <vivien.didelot@gmail.com>
 */

#include "chip.h"
#include "smi.h"

/* The switch ADDR[4:1] configuration pins define the chip SMI device address
 * (ADDR[0] is always zero, thus only even SMI addresses can be strapped).
 *
 * When ADDR is all zero, the chip uses Single-chip Addressing Mode, assuming it
 * is the only device connected to the SMI master. In this mode it responds to
 * all 32 possible SMI addresses, and thus maps directly the internal devices.
 *
 * When ADDR is non-zero, the chip uses Multi-chip Addressing Mode, allowing
 * multiple devices to share the SMI interface. In this mode it responds to only
 * 2 registers, used to indirectly access the internal SMI devices.
 */

static int mv88e6xxx_smi_direct_read(struct mv88e6xxx_chip *chip,
				     int dev, int reg, u16 *data)
{
	int ret;

	ret = mdiobus_read_nested(chip->bus, dev, reg);
	if (ret < 0)
		return ret;

	*data = ret & 0xffff;

	return 0;
}

static int mv88e6xxx_smi_direct_write(struct mv88e6xxx_chip *chip,
				      int dev, int reg, u16 data)
{
	int ret;

	ret = mdiobus_write_nested(chip->bus, dev, reg, data);
	if (ret < 0)
		return ret;

	return 0;
}

static int mv88e6xxx_smi_direct_wait(struct mv88e6xxx_chip *chip,
				     int dev, int reg, int bit, int val)
{
	u16 data;
	int err;
	int i;

	for (i = 0; i < 16; i++) {
		err = mv88e6xxx_smi_direct_read(chip, dev, reg, &data);
		if (err)
			return err;

		if (!!(data >> bit) == !!val)
			return 0;
	}

	return -ETIMEDOUT;
}

static const struct mv88e6xxx_bus_ops mv88e6xxx_smi_direct_ops = {
	.read = mv88e6xxx_smi_direct_read,
	.write = mv88e6xxx_smi_direct_write,
};

/* Offset 0x00: SMI Command Register
 * Offset 0x01: SMI Data Register
 */

static int mv88e6xxx_smi_indirect_read(struct mv88e6xxx_chip *chip,
				       int dev, int reg, u16 *data)
{
	int err;

	err = mv88e6xxx_smi_direct_wait(chip, chip->sw_addr,
					MV88E6XXX_SMI_CMD, 15, 0);
	if (err)
		return err;

	err = mv88e6xxx_smi_direct_write(chip, chip->sw_addr,
					 MV88E6XXX_SMI_CMD,
					 MV88E6XXX_SMI_CMD_BUSY |
					 MV88E6XXX_SMI_CMD_MODE_22 |
					 MV88E6XXX_SMI_CMD_OP_22_READ |
					 (dev << 5) | reg);
	if (err)
		return err;

	err = mv88e6xxx_smi_direct_wait(chip, chip->sw_addr,
					MV88E6XXX_SMI_CMD, 15, 0);
	if (err)
		return err;

	return mv88e6xxx_smi_direct_read(chip, chip->sw_addr,
					 MV88E6XXX_SMI_DATA, data);
}

static int mv88e6xxx_smi_indirect_write(struct mv88e6xxx_chip *chip,
					int dev, int reg, u16 data)
{
	int err;

	err = mv88e6xxx_smi_direct_wait(chip, chip->sw_addr,
					MV88E6XXX_SMI_CMD, 15, 0);
	if (err)
		return err;

	err = mv88e6xxx_smi_direct_write(chip, chip->sw_addr,
					 MV88E6XXX_SMI_DATA, data);
	if (err)
		return err;

	err = mv88e6xxx_smi_direct_write(chip, chip->sw_addr,
					 MV88E6XXX_SMI_CMD,
					 MV88E6XXX_SMI_CMD_BUSY |
					 MV88E6XXX_SMI_CMD_MODE_22 |
					 MV88E6XXX_SMI_CMD_OP_22_WRITE |
					 (dev << 5) | reg);
	if (err)
		return err;

	return mv88e6xxx_smi_direct_wait(chip, chip->sw_addr,
					 MV88E6XXX_SMI_CMD, 15, 0);
}

static const struct mv88e6xxx_bus_ops mv88e6xxx_smi_indirect_ops = {
	.read = mv88e6xxx_smi_indirect_read,
	.write = mv88e6xxx_smi_indirect_write,
};

int mv88e6xxx_smi_init(struct mv88e6xxx_chip *chip,
		       struct mii_bus *bus, int sw_addr)
{
	if (sw_addr == 0)
		chip->smi_ops = &mv88e6xxx_smi_direct_ops;
	else if (chip->info->multi_chip)
		chip->smi_ops = &mv88e6xxx_smi_indirect_ops;
	else
		return -EINVAL;

	chip->bus = bus;
	chip->sw_addr = sw_addr;

	return 0;
}
