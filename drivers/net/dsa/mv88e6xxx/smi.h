/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Marvell 88E6xxx System Management Interface (SMI) support
 *
 * Copyright (c) 2008 Marvell Semiconductor
 *
 * Copyright (c) 2019 Vivien Didelot <vivien.didelot@gmail.com>
 */

#ifndef _MV88E6XXX_SMI_H
#define _MV88E6XXX_SMI_H

#include "chip.h"

/* Offset 0x00: SMI Command Register */
#define MV88E6XXX_SMI_CMD			0x00
#define MV88E6XXX_SMI_CMD_BUSY			0x8000
#define MV88E6XXX_SMI_CMD_MODE_MASK		0x1000
#define MV88E6XXX_SMI_CMD_MODE_45		0x0000
#define MV88E6XXX_SMI_CMD_MODE_22		0x1000
#define MV88E6XXX_SMI_CMD_OP_MASK		0x0c00
#define MV88E6XXX_SMI_CMD_OP_22_WRITE		0x0400
#define MV88E6XXX_SMI_CMD_OP_22_READ		0x0800
#define MV88E6XXX_SMI_CMD_OP_45_WRITE_ADDR	0x0000
#define MV88E6XXX_SMI_CMD_OP_45_WRITE_DATA	0x0400
#define MV88E6XXX_SMI_CMD_OP_45_READ_DATA	0x0800
#define MV88E6XXX_SMI_CMD_OP_45_READ_DATA_INC	0x0c00
#define MV88E6XXX_SMI_CMD_DEV_ADDR_MASK		0x003e
#define MV88E6XXX_SMI_CMD_REG_ADDR_MASK		0x001f

/* Offset 0x01: SMI Data Register */
#define MV88E6XXX_SMI_DATA			0x01

int mv88e6xxx_smi_init(struct mv88e6xxx_chip *chip,
		       struct mii_bus *bus, int sw_addr);

static inline int mv88e6xxx_smi_read(struct mv88e6xxx_chip *chip,
				     int dev, int reg, u16 *data)
{
	if (chip->smi_ops && chip->smi_ops->read)
		return chip->smi_ops->read(chip, dev, reg, data);

	return -EOPNOTSUPP;
}

static inline int mv88e6xxx_smi_write(struct mv88e6xxx_chip *chip,
				      int dev, int reg, u16 data)
{
	if (chip->smi_ops && chip->smi_ops->write)
		return chip->smi_ops->write(chip, dev, reg, data);

	return -EOPNOTSUPP;
}

#endif /* _MV88E6XXX_SMI_H */
