/*
 * Marvell 88E6xxx Switch Global (1) Registers support
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

#include <linux/bitfield.h>

#include "chip.h"
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

/* Offset 0x00: Switch Global Status Register */

static int mv88e6185_g1_wait_ppu_disabled(struct mv88e6xxx_chip *chip)
{
	u16 state;
	int i, err;

	for (i = 0; i < 16; i++) {
		err = mv88e6xxx_g1_read(chip, MV88E6XXX_G1_STS, &state);
		if (err)
			return err;

		/* Check the value of the PPUState bits 15:14 */
		state &= MV88E6185_G1_STS_PPU_STATE_MASK;
		if (state != MV88E6185_G1_STS_PPU_STATE_POLLING)
			return 0;

		usleep_range(1000, 2000);
	}

	return -ETIMEDOUT;
}

static int mv88e6185_g1_wait_ppu_polling(struct mv88e6xxx_chip *chip)
{
	u16 state;
	int i, err;

	for (i = 0; i < 16; ++i) {
		err = mv88e6xxx_g1_read(chip, MV88E6XXX_G1_STS, &state);
		if (err)
			return err;

		/* Check the value of the PPUState bits 15:14 */
		state &= MV88E6185_G1_STS_PPU_STATE_MASK;
		if (state == MV88E6185_G1_STS_PPU_STATE_POLLING)
			return 0;

		usleep_range(1000, 2000);
	}

	return -ETIMEDOUT;
}

static int mv88e6352_g1_wait_ppu_polling(struct mv88e6xxx_chip *chip)
{
	u16 state;
	int i, err;

	for (i = 0; i < 16; ++i) {
		err = mv88e6xxx_g1_read(chip, MV88E6XXX_G1_STS, &state);
		if (err)
			return err;

		/* Check the value of the PPUState (or InitState) bit 15 */
		if (state & MV88E6352_G1_STS_PPU_STATE)
			return 0;

		usleep_range(1000, 2000);
	}

	return -ETIMEDOUT;
}

static int mv88e6xxx_g1_wait_init_ready(struct mv88e6xxx_chip *chip)
{
	const unsigned long timeout = jiffies + 1 * HZ;
	u16 val;
	int err;

	/* Wait up to 1 second for the switch to be ready. The InitReady bit 11
	 * is set to a one when all units inside the device (ATU, VTU, etc.)
	 * have finished their initialization and are ready to accept frames.
	 */
	while (time_before(jiffies, timeout)) {
		err = mv88e6xxx_g1_read(chip, MV88E6XXX_G1_STS, &val);
		if (err)
			return err;

		if (val & MV88E6XXX_G1_STS_INIT_READY)
			break;

		usleep_range(1000, 2000);
	}

	if (time_after(jiffies, timeout))
		return -ETIMEDOUT;

	return 0;
}

/* Offset 0x01: Switch MAC Address Register Bytes 0 & 1
 * Offset 0x02: Switch MAC Address Register Bytes 2 & 3
 * Offset 0x03: Switch MAC Address Register Bytes 4 & 5
 */
int mv88e6xxx_g1_set_switch_mac(struct mv88e6xxx_chip *chip, u8 *addr)
{
	u16 reg;
	int err;

	reg = (addr[0] << 8) | addr[1];
	err = mv88e6xxx_g1_write(chip, MV88E6XXX_G1_MAC_01, reg);
	if (err)
		return err;

	reg = (addr[2] << 8) | addr[3];
	err = mv88e6xxx_g1_write(chip, MV88E6XXX_G1_MAC_23, reg);
	if (err)
		return err;

	reg = (addr[4] << 8) | addr[5];
	err = mv88e6xxx_g1_write(chip, MV88E6XXX_G1_MAC_45, reg);
	if (err)
		return err;

	return 0;
}

/* Offset 0x04: Switch Global Control Register */

int mv88e6185_g1_reset(struct mv88e6xxx_chip *chip)
{
	u16 val;
	int err;

	/* Set the SWReset bit 15 along with the PPUEn bit 14, to also restart
	 * the PPU, including re-doing PHY detection and initialization
	 */
	err = mv88e6xxx_g1_read(chip, MV88E6XXX_G1_CTL1, &val);
	if (err)
		return err;

	val |= MV88E6XXX_G1_CTL1_SW_RESET;
	val |= MV88E6XXX_G1_CTL1_PPU_ENABLE;

	err = mv88e6xxx_g1_write(chip, MV88E6XXX_G1_CTL1, val);
	if (err)
		return err;

	err = mv88e6xxx_g1_wait_init_ready(chip);
	if (err)
		return err;

	return mv88e6185_g1_wait_ppu_polling(chip);
}

int mv88e6352_g1_reset(struct mv88e6xxx_chip *chip)
{
	u16 val;
	int err;

	/* Set the SWReset bit 15 */
	err = mv88e6xxx_g1_read(chip, MV88E6XXX_G1_CTL1, &val);
	if (err)
		return err;

	val |= MV88E6XXX_G1_CTL1_SW_RESET;

	err = mv88e6xxx_g1_write(chip, MV88E6XXX_G1_CTL1, val);
	if (err)
		return err;

	err = mv88e6xxx_g1_wait_init_ready(chip);
	if (err)
		return err;

	return mv88e6352_g1_wait_ppu_polling(chip);
}

int mv88e6185_g1_ppu_enable(struct mv88e6xxx_chip *chip)
{
	u16 val;
	int err;

	err = mv88e6xxx_g1_read(chip, MV88E6XXX_G1_CTL1, &val);
	if (err)
		return err;

	val |= MV88E6XXX_G1_CTL1_PPU_ENABLE;

	err = mv88e6xxx_g1_write(chip, MV88E6XXX_G1_CTL1, val);
	if (err)
		return err;

	return mv88e6185_g1_wait_ppu_polling(chip);
}

int mv88e6185_g1_ppu_disable(struct mv88e6xxx_chip *chip)
{
	u16 val;
	int err;

	err = mv88e6xxx_g1_read(chip, MV88E6XXX_G1_CTL1, &val);
	if (err)
		return err;

	val &= ~MV88E6XXX_G1_CTL1_PPU_ENABLE;

	err = mv88e6xxx_g1_write(chip, MV88E6XXX_G1_CTL1, val);
	if (err)
		return err;

	return mv88e6185_g1_wait_ppu_disabled(chip);
}

/* Offset 0x1a: Monitor Control */
/* Offset 0x1a: Monitor & MGMT Control on some devices */

int mv88e6095_g1_set_egress_port(struct mv88e6xxx_chip *chip, int port)
{
	u16 reg;
	int err;

	err = mv88e6xxx_g1_read(chip, MV88E6185_G1_MONITOR_CTL, &reg);
	if (err)
		return err;

	reg &= ~(MV88E6185_G1_MONITOR_CTL_INGRESS_DEST_MASK |
		 MV88E6185_G1_MONITOR_CTL_EGRESS_DEST_MASK);

	reg |= port << __bf_shf(MV88E6185_G1_MONITOR_CTL_INGRESS_DEST_MASK) |
		port << __bf_shf(MV88E6185_G1_MONITOR_CTL_EGRESS_DEST_MASK);

	return mv88e6xxx_g1_write(chip, MV88E6185_G1_MONITOR_CTL, reg);
}

/* Older generations also call this the ARP destination. It has been
 * generalized in more modern devices such that more than ARP can
 * egress it
 */
int mv88e6095_g1_set_cpu_port(struct mv88e6xxx_chip *chip, int port)
{
	u16 reg;
	int err;

	err = mv88e6xxx_g1_read(chip, MV88E6185_G1_MONITOR_CTL, &reg);
	if (err)
		return err;

	reg &= ~MV88E6185_G1_MONITOR_CTL_ARP_DEST_MASK;
	reg |= port << __bf_shf(MV88E6185_G1_MONITOR_CTL_ARP_DEST_MASK);

	return mv88e6xxx_g1_write(chip, MV88E6185_G1_MONITOR_CTL, reg);
}

static int mv88e6390_g1_monitor_write(struct mv88e6xxx_chip *chip,
				      u16 pointer, u8 data)
{
	u16 reg;

	reg = MV88E6390_G1_MONITOR_MGMT_CTL_UPDATE | pointer | data;

	return mv88e6xxx_g1_write(chip, MV88E6390_G1_MONITOR_MGMT_CTL, reg);
}

int mv88e6390_g1_set_egress_port(struct mv88e6xxx_chip *chip, int port)
{
	u16 ptr;
	int err;

	ptr = MV88E6390_G1_MONITOR_MGMT_CTL_PTR_INGRESS_DEST;
	err = mv88e6390_g1_monitor_write(chip, ptr, port);
	if (err)
		return err;

	ptr = MV88E6390_G1_MONITOR_MGMT_CTL_PTR_EGRESS_DEST;
	err = mv88e6390_g1_monitor_write(chip, ptr, port);
	if (err)
		return err;

	return 0;
}

int mv88e6390_g1_set_cpu_port(struct mv88e6xxx_chip *chip, int port)
{
	u16 ptr = MV88E6390_G1_MONITOR_MGMT_CTL_PTR_CPU_DEST;

	return mv88e6390_g1_monitor_write(chip, ptr, port);
}

int mv88e6390_g1_mgmt_rsvd2cpu(struct mv88e6xxx_chip *chip)
{
	u16 ptr;
	int err;

	/* 01:c2:80:00:00:00:00-01:c2:80:00:00:00:07 are Management */
	ptr = MV88E6390_G1_MONITOR_MGMT_CTL_PTR_0180C280000000XLO;
	err = mv88e6390_g1_monitor_write(chip, ptr, 0xff);
	if (err)
		return err;

	/* 01:c2:80:00:00:00:08-01:c2:80:00:00:00:0f are Management */
	ptr = MV88E6390_G1_MONITOR_MGMT_CTL_PTR_0180C280000000XHI;
	err = mv88e6390_g1_monitor_write(chip, ptr, 0xff);
	if (err)
		return err;

	/* 01:c2:80:00:00:00:20-01:c2:80:00:00:00:27 are Management */
	ptr = MV88E6390_G1_MONITOR_MGMT_CTL_PTR_0180C280000002XLO;
	err = mv88e6390_g1_monitor_write(chip, ptr, 0xff);
	if (err)
		return err;

	/* 01:c2:80:00:00:00:28-01:c2:80:00:00:00:2f are Management */
	ptr = MV88E6390_G1_MONITOR_MGMT_CTL_PTR_0180C280000002XHI;
	err = mv88e6390_g1_monitor_write(chip, ptr, 0xff);
	if (err)
		return err;

	return 0;
}

/* Offset 0x1c: Global Control 2 */

int mv88e6390_g1_stats_set_histogram(struct mv88e6xxx_chip *chip)
{
	u16 val;
	int err;

	err = mv88e6xxx_g1_read(chip, MV88E6XXX_G1_CTL2, &val);
	if (err)
		return err;

	val |= MV88E6XXX_G1_CTL2_HIST_RX_TX;

	err = mv88e6xxx_g1_write(chip, MV88E6XXX_G1_CTL2, val);

	return err;
}

/* Offset 0x1d: Statistics Operation 2 */

int mv88e6xxx_g1_stats_wait(struct mv88e6xxx_chip *chip)
{
	return mv88e6xxx_g1_wait(chip, MV88E6XXX_G1_STATS_OP,
				 MV88E6XXX_G1_STATS_OP_BUSY);
}

int mv88e6xxx_g1_stats_snapshot(struct mv88e6xxx_chip *chip, int port)
{
	int err;

	/* Snapshot the hardware statistics counters for this port. */
	err = mv88e6xxx_g1_write(chip, MV88E6XXX_G1_STATS_OP,
				 MV88E6XXX_G1_STATS_OP_BUSY |
				 MV88E6XXX_G1_STATS_OP_CAPTURE_PORT |
				 MV88E6XXX_G1_STATS_OP_HIST_RX_TX | port);
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
	err = mv88e6xxx_g1_write(chip, MV88E6XXX_G1_STATS_OP,
				 MV88E6XXX_G1_STATS_OP_BUSY |
				 MV88E6XXX_G1_STATS_OP_CAPTURE_PORT | port);
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

	err = mv88e6xxx_g1_write(chip, MV88E6XXX_G1_STATS_OP,
				 MV88E6XXX_G1_STATS_OP_BUSY |
				 MV88E6XXX_G1_STATS_OP_READ_CAPTURED | stat);
	if (err)
		return;

	err = mv88e6xxx_g1_stats_wait(chip);
	if (err)
		return;

	err = mv88e6xxx_g1_read(chip, MV88E6XXX_G1_STATS_COUNTER_32, &reg);
	if (err)
		return;

	value = reg << 16;

	err = mv88e6xxx_g1_read(chip, MV88E6XXX_G1_STATS_COUNTER_01, &reg);
	if (err)
		return;

	*val = value | reg;
}
