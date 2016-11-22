/*
 * Marvell 88E6xxx Switch Global (1) Registers support
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

#include "mv88e6xxx.h"
#include "global1.h"

int mv88e6xxx_g1_read(struct mv88e6xxx_chip *chip, int reg, u16 *val)
{
	int addr = chip->info->global1_addr;

	return mv88e6xxx_read(chip, addr, reg, val);
}

int mv88e6xxx_g1_write(struct mv88e6xxx_chip *chip, int reg, u16 val)
{
	int addr = chip->info->global1_addr;

	return mv88e6xxx_write(chip, addr, reg, val);
}

int mv88e6xxx_g1_wait(struct mv88e6xxx_chip *chip, int reg, u16 mask)
{
	return mv88e6xxx_wait(chip, chip->info->global1_addr, reg, mask);
}

/* Offset 0x1c: Global Control 2 */

int mv88e6390_g1_stats_set_histogram(struct mv88e6xxx_chip *chip)
{
	u16 val;
	int err;

	err = mv88e6xxx_g1_read(chip, GLOBAL_CONTROL_2, &val);
	if (err)
		return err;

	val |= GLOBAL_CONTROL_2_HIST_RX_TX;

	err = mv88e6xxx_g1_write(chip, GLOBAL_CONTROL_2, val);

	return err;
}

/* Offset 0x1d: Statistics Operation 2 */

int mv88e6xxx_g1_stats_wait(struct mv88e6xxx_chip *chip)
{
	return mv88e6xxx_g1_wait(chip, GLOBAL_STATS_OP, GLOBAL_STATS_OP_BUSY);
}

int mv88e6xxx_g1_stats_snapshot(struct mv88e6xxx_chip *chip, int port)
{
	int err;

	/* Snapshot the hardware statistics counters for this port. */
	err = mv88e6xxx_g1_write(chip, GLOBAL_STATS_OP,
				 GLOBAL_STATS_OP_CAPTURE_PORT |
				 GLOBAL_STATS_OP_HIST_RX_TX | port);
	if (err)
		return err;

	/* Wait for the snapshotting to complete. */
	return mv88e6xxx_g1_stats_wait(chip);
}

int mv88e6320_g1_stats_snapshot(struct mv88e6xxx_chip *chip, int port)
{
	port = (port + 1) << 5;

	return mv88e6xxx_g1_stats_snapshot(chip, port);
}

int mv88e6390_g1_stats_snapshot(struct mv88e6xxx_chip *chip, int port)
{
	int err;

	port = (port + 1) << 5;

	/* Snapshot the hardware statistics counters for this port. */
	err = mv88e6xxx_g1_write(chip, GLOBAL_STATS_OP,
				 GLOBAL_STATS_OP_CAPTURE_PORT | port);
	if (err)
		return err;

	/* Wait for the snapshotting to complete. */
	return mv88e6xxx_g1_stats_wait(chip);
}

void mv88e6xxx_g1_stats_read(struct mv88e6xxx_chip *chip, int stat, u32 *val)
{
	u32 value;
	u16 reg;
	int err;

	*val = 0;

	err = mv88e6xxx_g1_write(chip, GLOBAL_STATS_OP,
				 GLOBAL_STATS_OP_READ_CAPTURED | stat);
	if (err)
		return;

	err = mv88e6xxx_g1_stats_wait(chip);
	if (err)
		return;

	err = mv88e6xxx_g1_read(chip, GLOBAL_STATS_COUNTER_32, &reg);
	if (err)
		return;

	value = reg << 16;

	err = mv88e6xxx_g1_read(chip, GLOBAL_STATS_COUNTER_01, &reg);
	if (err)
		return;

	*val = value | reg;
}
