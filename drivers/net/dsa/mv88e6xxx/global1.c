// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Marvell 88E6xxx Switch Global (1) Registers support
 *
 * Copyright (c) 2008 Marvell Semiconductor
 *
 * Copyright (c) 2016-2017 Savoir-faire Linux Inc.
 *	Vivien Didelot <vivien.didelot@savoirfairelinux.com>
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

int mv88e6xxx_g1_wait_bit(struct mv88e6xxx_chip *chip, int reg, int
			  bit, int val)
{
	return mv88e6xxx_wait_bit(chip, chip->info->global1_addr, reg,
				  bit, val);
}

int mv88e6xxx_g1_wait_mask(struct mv88e6xxx_chip *chip, int reg,
			   u16 mask, u16 val)
{
	return mv88e6xxx_wait_mask(chip, chip->info->global1_addr, reg,
				   mask, val);
}

/* Offset 0x00: Switch Global Status Register */

static int mv88e6185_g1_wait_ppu_disabled(struct mv88e6xxx_chip *chip)
{
	return mv88e6xxx_g1_wait_mask(chip, MV88E6XXX_G1_STS,
				      MV88E6185_G1_STS_PPU_STATE_MASK,
				      MV88E6185_G1_STS_PPU_STATE_DISABLED);
}

static int mv88e6185_g1_wait_ppu_polling(struct mv88e6xxx_chip *chip)
{
	return mv88e6xxx_g1_wait_mask(chip, MV88E6XXX_G1_STS,
				      MV88E6185_G1_STS_PPU_STATE_MASK,
				      MV88E6185_G1_STS_PPU_STATE_POLLING);
}

static int mv88e6352_g1_wait_ppu_polling(struct mv88e6xxx_chip *chip)
{
	int bit = __bf_shf(MV88E6352_G1_STS_PPU_STATE);

	return mv88e6xxx_g1_wait_bit(chip, MV88E6XXX_G1_STS, bit, 1);
}

static int mv88e6xxx_g1_wait_init_ready(struct mv88e6xxx_chip *chip)
{
	int bit = __bf_shf(MV88E6XXX_G1_STS_INIT_READY);

	/* Wait up to 1 second for the switch to be ready. The InitReady bit 11
	 * is set to a one when all units inside the device (ATU, VTU, etc.)
	 * have finished their initialization and are ready to accept frames.
	 */
	return mv88e6xxx_g1_wait_bit(chip, MV88E6XXX_G1_STS, bit, 1);
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

int mv88e6250_g1_reset(struct mv88e6xxx_chip *chip)
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

	return mv88e6xxx_g1_wait_init_ready(chip);
}

int mv88e6352_g1_reset(struct mv88e6xxx_chip *chip)
{
	int err;

	err = mv88e6250_g1_reset(chip);
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

int mv88e6185_g1_set_max_frame_size(struct mv88e6xxx_chip *chip, int mtu)
{
	u16 val;
	int err;

	mtu += ETH_HLEN + ETH_FCS_LEN;

	err = mv88e6xxx_g1_read(chip, MV88E6XXX_G1_CTL1, &val);
	if (err)
		return err;

	val &= ~MV88E6185_G1_CTL1_MAX_FRAME_1632;

	if (mtu > 1518)
		val |= MV88E6185_G1_CTL1_MAX_FRAME_1632;

	return mv88e6xxx_g1_write(chip, MV88E6XXX_G1_CTL1, val);
}

/* Offset 0x10: IP-PRI Mapping Register 0
 * Offset 0x11: IP-PRI Mapping Register 1
 * Offset 0x12: IP-PRI Mapping Register 2
 * Offset 0x13: IP-PRI Mapping Register 3
 * Offset 0x14: IP-PRI Mapping Register 4
 * Offset 0x15: IP-PRI Mapping Register 5
 * Offset 0x16: IP-PRI Mapping Register 6
 * Offset 0x17: IP-PRI Mapping Register 7
 */

int mv88e6085_g1_ip_pri_map(struct mv88e6xxx_chip *chip)
{
	int err;

	/* Reset the IP TOS/DiffServ/Traffic priorities to defaults */
	err = mv88e6xxx_g1_write(chip, MV88E6XXX_G1_IP_PRI_0, 0x0000);
	if (err)
		return err;

	err = mv88e6xxx_g1_write(chip, MV88E6XXX_G1_IP_PRI_1, 0x0000);
	if (err)
		return err;

	err = mv88e6xxx_g1_write(chip, MV88E6XXX_G1_IP_PRI_2, 0x5555);
	if (err)
		return err;

	err = mv88e6xxx_g1_write(chip, MV88E6XXX_G1_IP_PRI_3, 0x5555);
	if (err)
		return err;

	err = mv88e6xxx_g1_write(chip, MV88E6XXX_G1_IP_PRI_4, 0xaaaa);
	if (err)
		return err;

	err = mv88e6xxx_g1_write(chip, MV88E6XXX_G1_IP_PRI_5, 0xaaaa);
	if (err)
		return err;

	err = mv88e6xxx_g1_write(chip, MV88E6XXX_G1_IP_PRI_6, 0xffff);
	if (err)
		return err;

	err = mv88e6xxx_g1_write(chip, MV88E6XXX_G1_IP_PRI_7, 0xffff);
	if (err)
		return err;

	return 0;
}

/* Offset 0x18: IEEE-PRI Register */

int mv88e6085_g1_ieee_pri_map(struct mv88e6xxx_chip *chip)
{
	/* Reset the IEEE Tag priorities to defaults */
	return mv88e6xxx_g1_write(chip, MV88E6XXX_G1_IEEE_PRI, 0xfa41);
}

int mv88e6250_g1_ieee_pri_map(struct mv88e6xxx_chip *chip)
{
	/* Reset the IEEE Tag priorities to defaults */
	return mv88e6xxx_g1_write(chip, MV88E6XXX_G1_IEEE_PRI, 0xfa50);
}

/* Offset 0x1a: Monitor Control */
/* Offset 0x1a: Monitor & MGMT Control on some devices */

int mv88e6095_g1_set_egress_port(struct mv88e6xxx_chip *chip,
				 enum mv88e6xxx_egress_direction direction,
				 int port)
{
	u16 reg;
	int err;

	err = mv88e6xxx_g1_read(chip, MV88E6185_G1_MONITOR_CTL, &reg);
	if (err)
		return err;

	switch (direction) {
	case MV88E6XXX_EGRESS_DIR_INGRESS:
		reg &= ~MV88E6185_G1_MONITOR_CTL_INGRESS_DEST_MASK;
		reg |= port <<
		       __bf_shf(MV88E6185_G1_MONITOR_CTL_INGRESS_DEST_MASK);
		break;
	case MV88E6XXX_EGRESS_DIR_EGRESS:
		reg &= ~MV88E6185_G1_MONITOR_CTL_EGRESS_DEST_MASK;
		reg |= port <<
		       __bf_shf(MV88E6185_G1_MONITOR_CTL_EGRESS_DEST_MASK);
		break;
	default:
		return -EINVAL;
	}

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

int mv88e6390_g1_set_egress_port(struct mv88e6xxx_chip *chip,
				 enum mv88e6xxx_egress_direction direction,
				 int port)
{
	u16 ptr;

	switch (direction) {
	case MV88E6XXX_EGRESS_DIR_INGRESS:
		ptr = MV88E6390_G1_MONITOR_MGMT_CTL_PTR_INGRESS_DEST;
		break;
	case MV88E6XXX_EGRESS_DIR_EGRESS:
		ptr = MV88E6390_G1_MONITOR_MGMT_CTL_PTR_EGRESS_DEST;
		break;
	default:
		return -EINVAL;
	}

	return mv88e6390_g1_monitor_write(chip, ptr, port);
}

int mv88e6390_g1_set_cpu_port(struct mv88e6xxx_chip *chip, int port)
{
	u16 ptr = MV88E6390_G1_MONITOR_MGMT_CTL_PTR_CPU_DEST;

	/* Use the default high priority for management frames sent to
	 * the CPU.
	 */
	port |= MV88E6390_G1_MONITOR_MGMT_CTL_PTR_CPU_DEST_MGMTPRI;

	return mv88e6390_g1_monitor_write(chip, ptr, port);
}

int mv88e6390_g1_set_ptp_cpu_port(struct mv88e6xxx_chip *chip, int port)
{
	u16 ptr = MV88E6390_G1_MONITOR_MGMT_CTL_PTR_PTP_CPU_DEST;

	/* Use the default high priority for PTP frames sent to
	 * the CPU.
	 */
	port |= MV88E6390_G1_MONITOR_MGMT_CTL_PTR_CPU_DEST_MGMTPRI;

	return mv88e6390_g1_monitor_write(chip, ptr, port);
}

int mv88e6390_g1_mgmt_rsvd2cpu(struct mv88e6xxx_chip *chip)
{
	u16 ptr;
	int err;

	/* 01:80:c2:00:00:00-01:80:c2:00:00:07 are Management */
	ptr = MV88E6390_G1_MONITOR_MGMT_CTL_PTR_0180C200000XLO;
	err = mv88e6390_g1_monitor_write(chip, ptr, 0xff);
	if (err)
		return err;

	/* 01:80:c2:00:00:08-01:80:c2:00:00:0f are Management */
	ptr = MV88E6390_G1_MONITOR_MGMT_CTL_PTR_0180C200000XHI;
	err = mv88e6390_g1_monitor_write(chip, ptr, 0xff);
	if (err)
		return err;

	/* 01:80:c2:00:00:20-01:80:c2:00:00:27 are Management */
	ptr = MV88E6390_G1_MONITOR_MGMT_CTL_PTR_0180C200002XLO;
	err = mv88e6390_g1_monitor_write(chip, ptr, 0xff);
	if (err)
		return err;

	/* 01:80:c2:00:00:28-01:80:c2:00:00:2f are Management */
	ptr = MV88E6390_G1_MONITOR_MGMT_CTL_PTR_0180C200002XHI;
	err = mv88e6390_g1_monitor_write(chip, ptr, 0xff);
	if (err)
		return err;

	return 0;
}

/* Offset 0x1c: Global Control 2 */

static int mv88e6xxx_g1_ctl2_mask(struct mv88e6xxx_chip *chip, u16 mask,
				  u16 val)
{
	u16 reg;
	int err;

	err = mv88e6xxx_g1_read(chip, MV88E6XXX_G1_CTL2, &reg);
	if (err)
		return err;

	reg &= ~mask;
	reg |= val & mask;

	return mv88e6xxx_g1_write(chip, MV88E6XXX_G1_CTL2, reg);
}

int mv88e6185_g1_set_cascade_port(struct mv88e6xxx_chip *chip, int port)
{
	const u16 mask = MV88E6185_G1_CTL2_CASCADE_PORT_MASK;

	return mv88e6xxx_g1_ctl2_mask(chip, mask, port << __bf_shf(mask));
}

int mv88e6085_g1_rmu_disable(struct mv88e6xxx_chip *chip)
{
	return mv88e6xxx_g1_ctl2_mask(chip, MV88E6085_G1_CTL2_P10RM |
				      MV88E6085_G1_CTL2_RM_ENABLE, 0);
}

int mv88e6352_g1_rmu_disable(struct mv88e6xxx_chip *chip)
{
	return mv88e6xxx_g1_ctl2_mask(chip, MV88E6352_G1_CTL2_RMU_MODE_MASK,
				      MV88E6352_G1_CTL2_RMU_MODE_DISABLED);
}

int mv88e6390_g1_rmu_disable(struct mv88e6xxx_chip *chip)
{
	return mv88e6xxx_g1_ctl2_mask(chip, MV88E6390_G1_CTL2_RMU_MODE_MASK,
				      MV88E6390_G1_CTL2_RMU_MODE_DISABLED);
}

int mv88e6390_g1_stats_set_histogram(struct mv88e6xxx_chip *chip)
{
	return mv88e6xxx_g1_ctl2_mask(chip, MV88E6390_G1_CTL2_HIST_MODE_MASK,
				      MV88E6390_G1_CTL2_HIST_MODE_RX |
				      MV88E6390_G1_CTL2_HIST_MODE_TX);
}

int mv88e6xxx_g1_set_device_number(struct mv88e6xxx_chip *chip, int index)
{
	return mv88e6xxx_g1_ctl2_mask(chip,
				      MV88E6XXX_G1_CTL2_DEVICE_NUMBER_MASK,
				      index);
}

/* Offset 0x1d: Statistics Operation 2 */

static int mv88e6xxx_g1_stats_wait(struct mv88e6xxx_chip *chip)
{
	int bit = __bf_shf(MV88E6XXX_G1_STATS_OP_BUSY);

	return mv88e6xxx_g1_wait_bit(chip, MV88E6XXX_G1_STATS_OP, bit, 0);
}

int mv88e6095_g1_stats_set_histogram(struct mv88e6xxx_chip *chip)
{
	u16 val;
	int err;

	err = mv88e6xxx_g1_read(chip, MV88E6XXX_G1_STATS_OP, &val);
	if (err)
		return err;

	val |= MV88E6XXX_G1_STATS_OP_HIST_RX_TX;

	err = mv88e6xxx_g1_write(chip, MV88E6XXX_G1_STATS_OP, val);

	return err;
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

int mv88e6xxx_g1_stats_clear(struct mv88e6xxx_chip *chip)
{
	int err;
	u16 val;

	err = mv88e6xxx_g1_read(chip, MV88E6XXX_G1_STATS_OP, &val);
	if (err)
		return err;

	/* Keep the histogram mode bits */
	val &= MV88E6XXX_G1_STATS_OP_HIST_RX_TX;
	val |= MV88E6XXX_G1_STATS_OP_BUSY | MV88E6XXX_G1_STATS_OP_FLUSH_ALL;

	err = mv88e6xxx_g1_write(chip, MV88E6XXX_G1_STATS_OP, val);
	if (err)
		return err;

	/* Wait for the flush to complete. */
	return mv88e6xxx_g1_stats_wait(chip);
}
