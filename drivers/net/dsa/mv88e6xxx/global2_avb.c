// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Marvell 88E6xxx Switch Global 2 Registers support
 *
 * Copyright (c) 2008 Marvell Semiconductor
 *
 * Copyright (c) 2016-2017 Savoir-faire Linux Inc.
 *	Vivien Didelot <vivien.didelot@savoirfairelinux.com>
 *
 * Copyright (c) 2017 National Instruments
 *	Brandon Streiff <brandon.streiff@ni.com>
 */

#include <linux/bitfield.h>

#include "global2.h"

/* Offset 0x16: AVB Command Register
 * Offset 0x17: AVB Data Register
 *
 * There are two different versions of this register interface:
 *    "6352": 3-bit "op" field, 4-bit "port" field.
 *    "6390": 2-bit "op" field, 5-bit "port" field.
 *
 * The "op" codes are different between the two, as well as the special
 * port fields for global PTP and TAI configuration.
 */

/* mv88e6xxx_g2_avb_read -- Read one or multiple 16-bit words.
 * The hardware supports snapshotting up to four contiguous registers.
 */
static int mv88e6xxx_g2_avb_wait(struct mv88e6xxx_chip *chip)
{
	int bit = __bf_shf(MV88E6352_G2_AVB_CMD_BUSY);

	return mv88e6xxx_g2_wait_bit(chip, MV88E6352_G2_AVB_CMD, bit, 0);
}

static int mv88e6xxx_g2_avb_read(struct mv88e6xxx_chip *chip, u16 readop,
				 u16 *data, int len)
{
	int err;
	int i;

	err = mv88e6xxx_g2_avb_wait(chip);
	if (err)
		return err;

	/* Hardware can only snapshot four words. */
	if (len > 4)
		return -E2BIG;

	err = mv88e6xxx_g2_write(chip, MV88E6352_G2_AVB_CMD,
				 MV88E6352_G2_AVB_CMD_BUSY | readop);
	if (err)
		return err;

	err = mv88e6xxx_g2_avb_wait(chip);
	if (err)
		return err;

	for (i = 0; i < len; ++i) {
		err = mv88e6xxx_g2_read(chip, MV88E6352_G2_AVB_DATA,
					&data[i]);
		if (err)
			return err;
	}

	return 0;
}

/* mv88e6xxx_g2_avb_write -- Write one 16-bit word. */
static int mv88e6xxx_g2_avb_write(struct mv88e6xxx_chip *chip, u16 writeop,
				  u16 data)
{
	int err;

	err = mv88e6xxx_g2_avb_wait(chip);
	if (err)
		return err;

	err = mv88e6xxx_g2_write(chip, MV88E6352_G2_AVB_DATA, data);
	if (err)
		return err;

	err = mv88e6xxx_g2_write(chip, MV88E6352_G2_AVB_CMD,
				 MV88E6352_G2_AVB_CMD_BUSY | writeop);

	return mv88e6xxx_g2_avb_wait(chip);
}

static int mv88e6352_g2_avb_port_ptp_read(struct mv88e6xxx_chip *chip,
					  int port, int addr, u16 *data,
					  int len)
{
	u16 readop = (len == 1 ? MV88E6352_G2_AVB_CMD_OP_READ :
				 MV88E6352_G2_AVB_CMD_OP_READ_INCR) |
		     (port << 8) | (MV88E6352_G2_AVB_CMD_BLOCK_PTP << 5) |
		     addr;

	return mv88e6xxx_g2_avb_read(chip, readop, data, len);
}

static int mv88e6352_g2_avb_port_ptp_write(struct mv88e6xxx_chip *chip,
					   int port, int addr, u16 data)
{
	u16 writeop = MV88E6352_G2_AVB_CMD_OP_WRITE | (port << 8) |
		      (MV88E6352_G2_AVB_CMD_BLOCK_PTP << 5) | addr;

	return mv88e6xxx_g2_avb_write(chip, writeop, data);
}

static int mv88e6352_g2_avb_ptp_read(struct mv88e6xxx_chip *chip, int addr,
				     u16 *data, int len)
{
	return mv88e6352_g2_avb_port_ptp_read(chip,
					MV88E6352_G2_AVB_CMD_PORT_PTPGLOBAL,
					addr, data, len);
}

static int mv88e6352_g2_avb_ptp_write(struct mv88e6xxx_chip *chip, int addr,
				      u16 data)
{
	return mv88e6352_g2_avb_port_ptp_write(chip,
					MV88E6352_G2_AVB_CMD_PORT_PTPGLOBAL,
					addr, data);
}

static int mv88e6352_g2_avb_tai_read(struct mv88e6xxx_chip *chip, int addr,
				     u16 *data, int len)
{
	return mv88e6352_g2_avb_port_ptp_read(chip,
					MV88E6352_G2_AVB_CMD_PORT_TAIGLOBAL,
					addr, data, len);
}

static int mv88e6352_g2_avb_tai_write(struct mv88e6xxx_chip *chip, int addr,
				      u16 data)
{
	return mv88e6352_g2_avb_port_ptp_write(chip,
					MV88E6352_G2_AVB_CMD_PORT_TAIGLOBAL,
					addr, data);
}

const struct mv88e6xxx_avb_ops mv88e6352_avb_ops = {
	.port_ptp_read		= mv88e6352_g2_avb_port_ptp_read,
	.port_ptp_write		= mv88e6352_g2_avb_port_ptp_write,
	.ptp_read		= mv88e6352_g2_avb_ptp_read,
	.ptp_write		= mv88e6352_g2_avb_ptp_write,
	.tai_read		= mv88e6352_g2_avb_tai_read,
	.tai_write		= mv88e6352_g2_avb_tai_write,
};

static int mv88e6165_g2_avb_tai_read(struct mv88e6xxx_chip *chip, int addr,
				     u16 *data, int len)
{
	return mv88e6352_g2_avb_port_ptp_read(chip,
					MV88E6165_G2_AVB_CMD_PORT_PTPGLOBAL,
					addr, data, len);
}

static int mv88e6165_g2_avb_tai_write(struct mv88e6xxx_chip *chip, int addr,
				      u16 data)
{
	return mv88e6352_g2_avb_port_ptp_write(chip,
					MV88E6165_G2_AVB_CMD_PORT_PTPGLOBAL,
					addr, data);
}

const struct mv88e6xxx_avb_ops mv88e6165_avb_ops = {
	.port_ptp_read		= mv88e6352_g2_avb_port_ptp_read,
	.port_ptp_write		= mv88e6352_g2_avb_port_ptp_write,
	.ptp_read		= mv88e6352_g2_avb_ptp_read,
	.ptp_write		= mv88e6352_g2_avb_ptp_write,
	.tai_read		= mv88e6165_g2_avb_tai_read,
	.tai_write		= mv88e6165_g2_avb_tai_write,
};

static int mv88e6390_g2_avb_port_ptp_read(struct mv88e6xxx_chip *chip,
					  int port, int addr, u16 *data,
					  int len)
{
	u16 readop = (len == 1 ? MV88E6390_G2_AVB_CMD_OP_READ :
				 MV88E6390_G2_AVB_CMD_OP_READ_INCR) |
		     (port << 8) | (MV88E6352_G2_AVB_CMD_BLOCK_PTP << 5) |
		     addr;

	return mv88e6xxx_g2_avb_read(chip, readop, data, len);
}

static int mv88e6390_g2_avb_port_ptp_write(struct mv88e6xxx_chip *chip,
					   int port, int addr, u16 data)
{
	u16 writeop = MV88E6390_G2_AVB_CMD_OP_WRITE | (port << 8) |
		      (MV88E6352_G2_AVB_CMD_BLOCK_PTP << 5) | addr;

	return mv88e6xxx_g2_avb_write(chip, writeop, data);
}

static int mv88e6390_g2_avb_ptp_read(struct mv88e6xxx_chip *chip, int addr,
				     u16 *data, int len)
{
	return mv88e6390_g2_avb_port_ptp_read(chip,
					MV88E6390_G2_AVB_CMD_PORT_PTPGLOBAL,
					addr, data, len);
}

static int mv88e6390_g2_avb_ptp_write(struct mv88e6xxx_chip *chip, int addr,
				      u16 data)
{
	return mv88e6390_g2_avb_port_ptp_write(chip,
					MV88E6390_G2_AVB_CMD_PORT_PTPGLOBAL,
					addr, data);
}

static int mv88e6390_g2_avb_tai_read(struct mv88e6xxx_chip *chip, int addr,
				     u16 *data, int len)
{
	return mv88e6390_g2_avb_port_ptp_read(chip,
					MV88E6390_G2_AVB_CMD_PORT_TAIGLOBAL,
					addr, data, len);
}

static int mv88e6390_g2_avb_tai_write(struct mv88e6xxx_chip *chip, int addr,
				      u16 data)
{
	return mv88e6390_g2_avb_port_ptp_write(chip,
					MV88E6390_G2_AVB_CMD_PORT_TAIGLOBAL,
					addr, data);
}

const struct mv88e6xxx_avb_ops mv88e6390_avb_ops = {
	.port_ptp_read		= mv88e6390_g2_avb_port_ptp_read,
	.port_ptp_write		= mv88e6390_g2_avb_port_ptp_write,
	.ptp_read		= mv88e6390_g2_avb_ptp_read,
	.ptp_write		= mv88e6390_g2_avb_ptp_write,
	.tai_read		= mv88e6390_g2_avb_tai_read,
	.tai_write		= mv88e6390_g2_avb_tai_write,
};
