/*
 * Marvell 88e6xxx Ethernet switch single-chip support
 *
 * Copyright (c) 2008 Marvell Semiconductor
 *
 * Copyright (c) 2015 CMC Electronics, Inc.
 *	Added support for VLAN Table Unit operations
 *
 * Copyright (c) 2016 Andrew Lunn <andrew@lunn.ch>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/delay.h>
#include <linux/etherdevice.h>
#include <linux/ethtool.h>
#include <linux/if_bridge.h>
#include <linux/jiffies.h>
#include <linux/list.h>
#include <linux/mdio.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/of_mdio.h>
#include <linux/netdevice.h>
#include <linux/gpio/consumer.h>
#include <linux/phy.h>
#include <net/dsa.h>
#include <net/switchdev.h>
#include "mv88e6xxx.h"

static void assert_reg_lock(struct mv88e6xxx_chip *chip)
{
	if (unlikely(!mutex_is_locked(&chip->reg_lock))) {
		dev_err(chip->dev, "Switch registers lock not held!\n");
		dump_stack();
	}
}

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

static int mv88e6xxx_smi_read(struct mv88e6xxx_chip *chip,
			      int addr, int reg, u16 *val)
{
	if (!chip->smi_ops)
		return -EOPNOTSUPP;

	return chip->smi_ops->read(chip, addr, reg, val);
}

static int mv88e6xxx_smi_write(struct mv88e6xxx_chip *chip,
			       int addr, int reg, u16 val)
{
	if (!chip->smi_ops)
		return -EOPNOTSUPP;

	return chip->smi_ops->write(chip, addr, reg, val);
}

static int mv88e6xxx_smi_single_chip_read(struct mv88e6xxx_chip *chip,
					  int addr, int reg, u16 *val)
{
	int ret;

	ret = mdiobus_read_nested(chip->bus, addr, reg);
	if (ret < 0)
		return ret;

	*val = ret & 0xffff;

	return 0;
}

static int mv88e6xxx_smi_single_chip_write(struct mv88e6xxx_chip *chip,
					   int addr, int reg, u16 val)
{
	int ret;

	ret = mdiobus_write_nested(chip->bus, addr, reg, val);
	if (ret < 0)
		return ret;

	return 0;
}

static const struct mv88e6xxx_ops mv88e6xxx_smi_single_chip_ops = {
	.read = mv88e6xxx_smi_single_chip_read,
	.write = mv88e6xxx_smi_single_chip_write,
};

static int mv88e6xxx_smi_multi_chip_wait(struct mv88e6xxx_chip *chip)
{
	int ret;
	int i;

	for (i = 0; i < 16; i++) {
		ret = mdiobus_read_nested(chip->bus, chip->sw_addr, SMI_CMD);
		if (ret < 0)
			return ret;

		if ((ret & SMI_CMD_BUSY) == 0)
			return 0;
	}

	return -ETIMEDOUT;
}

static int mv88e6xxx_smi_multi_chip_read(struct mv88e6xxx_chip *chip,
					 int addr, int reg, u16 *val)
{
	int ret;

	/* Wait for the bus to become free. */
	ret = mv88e6xxx_smi_multi_chip_wait(chip);
	if (ret < 0)
		return ret;

	/* Transmit the read command. */
	ret = mdiobus_write_nested(chip->bus, chip->sw_addr, SMI_CMD,
				   SMI_CMD_OP_22_READ | (addr << 5) | reg);
	if (ret < 0)
		return ret;

	/* Wait for the read command to complete. */
	ret = mv88e6xxx_smi_multi_chip_wait(chip);
	if (ret < 0)
		return ret;

	/* Read the data. */
	ret = mdiobus_read_nested(chip->bus, chip->sw_addr, SMI_DATA);
	if (ret < 0)
		return ret;

	*val = ret & 0xffff;

	return 0;
}

static int mv88e6xxx_smi_multi_chip_write(struct mv88e6xxx_chip *chip,
					  int addr, int reg, u16 val)
{
	int ret;

	/* Wait for the bus to become free. */
	ret = mv88e6xxx_smi_multi_chip_wait(chip);
	if (ret < 0)
		return ret;

	/* Transmit the data to write. */
	ret = mdiobus_write_nested(chip->bus, chip->sw_addr, SMI_DATA, val);
	if (ret < 0)
		return ret;

	/* Transmit the write command. */
	ret = mdiobus_write_nested(chip->bus, chip->sw_addr, SMI_CMD,
				   SMI_CMD_OP_22_WRITE | (addr << 5) | reg);
	if (ret < 0)
		return ret;

	/* Wait for the write command to complete. */
	ret = mv88e6xxx_smi_multi_chip_wait(chip);
	if (ret < 0)
		return ret;

	return 0;
}

static const struct mv88e6xxx_ops mv88e6xxx_smi_multi_chip_ops = {
	.read = mv88e6xxx_smi_multi_chip_read,
	.write = mv88e6xxx_smi_multi_chip_write,
};

static int mv88e6xxx_read(struct mv88e6xxx_chip *chip,
			  int addr, int reg, u16 *val)
{
	int err;

	assert_reg_lock(chip);

	err = mv88e6xxx_smi_read(chip, addr, reg, val);
	if (err)
		return err;

	dev_dbg(chip->dev, "<- addr: 0x%.2x reg: 0x%.2x val: 0x%.4x\n",
		addr, reg, *val);

	return 0;
}

static int mv88e6xxx_write(struct mv88e6xxx_chip *chip,
			   int addr, int reg, u16 val)
{
	int err;

	assert_reg_lock(chip);

	err = mv88e6xxx_smi_write(chip, addr, reg, val);
	if (err)
		return err;

	dev_dbg(chip->dev, "-> addr: 0x%.2x reg: 0x%.2x val: 0x%.4x\n",
		addr, reg, val);

	return 0;
}

/* Indirect write to single pointer-data register with an Update bit */
static int mv88e6xxx_update(struct mv88e6xxx_chip *chip, int addr, int reg,
			    u16 update)
{
	u16 val;
	int i, err;

	/* Wait until the previous operation is completed */
	for (i = 0; i < 16; ++i) {
		err = mv88e6xxx_read(chip, addr, reg, &val);
		if (err)
			return err;

		if (!(val & BIT(15)))
			break;
	}

	if (i == 16)
		return -ETIMEDOUT;

	/* Set the Update bit to trigger a write operation */
	val = BIT(15) | update;

	return mv88e6xxx_write(chip, addr, reg, val);
}

static int _mv88e6xxx_reg_read(struct mv88e6xxx_chip *chip, int addr, int reg)
{
	u16 val;
	int err;

	err = mv88e6xxx_read(chip, addr, reg, &val);
	if (err)
		return err;

	return val;
}

static int mv88e6xxx_reg_read(struct mv88e6xxx_chip *chip, int addr, int reg)
{
	int ret;

	mutex_lock(&chip->reg_lock);
	ret = _mv88e6xxx_reg_read(chip, addr, reg);
	mutex_unlock(&chip->reg_lock);

	return ret;
}

static int _mv88e6xxx_reg_write(struct mv88e6xxx_chip *chip, int addr,
				int reg, u16 val)
{
	return mv88e6xxx_write(chip, addr, reg, val);
}

static int mv88e6xxx_reg_write(struct mv88e6xxx_chip *chip, int addr,
			       int reg, u16 val)
{
	int ret;

	mutex_lock(&chip->reg_lock);
	ret = _mv88e6xxx_reg_write(chip, addr, reg, val);
	mutex_unlock(&chip->reg_lock);

	return ret;
}

static int mv88e6xxx_mdio_read_direct(struct mv88e6xxx_chip *chip,
				      int addr, int regnum)
{
	if (addr >= 0)
		return _mv88e6xxx_reg_read(chip, addr, regnum);
	return 0xffff;
}

static int mv88e6xxx_mdio_write_direct(struct mv88e6xxx_chip *chip,
				       int addr, int regnum, u16 val)
{
	if (addr >= 0)
		return _mv88e6xxx_reg_write(chip, addr, regnum, val);
	return 0;
}

static int mv88e6xxx_ppu_disable(struct mv88e6xxx_chip *chip)
{
	int ret;
	unsigned long timeout;

	ret = _mv88e6xxx_reg_read(chip, REG_GLOBAL, GLOBAL_CONTROL);
	if (ret < 0)
		return ret;

	ret = _mv88e6xxx_reg_write(chip, REG_GLOBAL, GLOBAL_CONTROL,
				   ret & ~GLOBAL_CONTROL_PPU_ENABLE);
	if (ret)
		return ret;

	timeout = jiffies + 1 * HZ;
	while (time_before(jiffies, timeout)) {
		ret = _mv88e6xxx_reg_read(chip, REG_GLOBAL, GLOBAL_STATUS);
		if (ret < 0)
			return ret;

		usleep_range(1000, 2000);
		if ((ret & GLOBAL_STATUS_PPU_MASK) !=
		    GLOBAL_STATUS_PPU_POLLING)
			return 0;
	}

	return -ETIMEDOUT;
}

static int mv88e6xxx_ppu_enable(struct mv88e6xxx_chip *chip)
{
	int ret, err;
	unsigned long timeout;

	ret = _mv88e6xxx_reg_read(chip, REG_GLOBAL, GLOBAL_CONTROL);
	if (ret < 0)
		return ret;

	err = _mv88e6xxx_reg_write(chip, REG_GLOBAL, GLOBAL_CONTROL,
				   ret | GLOBAL_CONTROL_PPU_ENABLE);
	if (err)
		return err;

	timeout = jiffies + 1 * HZ;
	while (time_before(jiffies, timeout)) {
		ret = _mv88e6xxx_reg_read(chip, REG_GLOBAL, GLOBAL_STATUS);
		if (ret < 0)
			return ret;

		usleep_range(1000, 2000);
		if ((ret & GLOBAL_STATUS_PPU_MASK) ==
		    GLOBAL_STATUS_PPU_POLLING)
			return 0;
	}

	return -ETIMEDOUT;
}

static void mv88e6xxx_ppu_reenable_work(struct work_struct *ugly)
{
	struct mv88e6xxx_chip *chip;

	chip = container_of(ugly, struct mv88e6xxx_chip, ppu_work);

	mutex_lock(&chip->reg_lock);

	if (mutex_trylock(&chip->ppu_mutex)) {
		if (mv88e6xxx_ppu_enable(chip) == 0)
			chip->ppu_disabled = 0;
		mutex_unlock(&chip->ppu_mutex);
	}

	mutex_unlock(&chip->reg_lock);
}

static void mv88e6xxx_ppu_reenable_timer(unsigned long _ps)
{
	struct mv88e6xxx_chip *chip = (void *)_ps;

	schedule_work(&chip->ppu_work);
}

static int mv88e6xxx_ppu_access_get(struct mv88e6xxx_chip *chip)
{
	int ret;

	mutex_lock(&chip->ppu_mutex);

	/* If the PHY polling unit is enabled, disable it so that
	 * we can access the PHY registers.  If it was already
	 * disabled, cancel the timer that is going to re-enable
	 * it.
	 */
	if (!chip->ppu_disabled) {
		ret = mv88e6xxx_ppu_disable(chip);
		if (ret < 0) {
			mutex_unlock(&chip->ppu_mutex);
			return ret;
		}
		chip->ppu_disabled = 1;
	} else {
		del_timer(&chip->ppu_timer);
		ret = 0;
	}

	return ret;
}

static void mv88e6xxx_ppu_access_put(struct mv88e6xxx_chip *chip)
{
	/* Schedule a timer to re-enable the PHY polling unit. */
	mod_timer(&chip->ppu_timer, jiffies + msecs_to_jiffies(10));
	mutex_unlock(&chip->ppu_mutex);
}

static void mv88e6xxx_ppu_state_init(struct mv88e6xxx_chip *chip)
{
	mutex_init(&chip->ppu_mutex);
	INIT_WORK(&chip->ppu_work, mv88e6xxx_ppu_reenable_work);
	init_timer(&chip->ppu_timer);
	chip->ppu_timer.data = (unsigned long)chip;
	chip->ppu_timer.function = mv88e6xxx_ppu_reenable_timer;
}

static int mv88e6xxx_mdio_read_ppu(struct mv88e6xxx_chip *chip, int addr,
				   int regnum)
{
	int ret;

	ret = mv88e6xxx_ppu_access_get(chip);
	if (ret >= 0) {
		ret = _mv88e6xxx_reg_read(chip, addr, regnum);
		mv88e6xxx_ppu_access_put(chip);
	}

	return ret;
}

static int mv88e6xxx_mdio_write_ppu(struct mv88e6xxx_chip *chip, int addr,
				    int regnum, u16 val)
{
	int ret;

	ret = mv88e6xxx_ppu_access_get(chip);
	if (ret >= 0) {
		ret = _mv88e6xxx_reg_write(chip, addr, regnum, val);
		mv88e6xxx_ppu_access_put(chip);
	}

	return ret;
}

static bool mv88e6xxx_6065_family(struct mv88e6xxx_chip *chip)
{
	return chip->info->family == MV88E6XXX_FAMILY_6065;
}

static bool mv88e6xxx_6095_family(struct mv88e6xxx_chip *chip)
{
	return chip->info->family == MV88E6XXX_FAMILY_6095;
}

static bool mv88e6xxx_6097_family(struct mv88e6xxx_chip *chip)
{
	return chip->info->family == MV88E6XXX_FAMILY_6097;
}

static bool mv88e6xxx_6165_family(struct mv88e6xxx_chip *chip)
{
	return chip->info->family == MV88E6XXX_FAMILY_6165;
}

static bool mv88e6xxx_6185_family(struct mv88e6xxx_chip *chip)
{
	return chip->info->family == MV88E6XXX_FAMILY_6185;
}

static bool mv88e6xxx_6320_family(struct mv88e6xxx_chip *chip)
{
	return chip->info->family == MV88E6XXX_FAMILY_6320;
}

static bool mv88e6xxx_6351_family(struct mv88e6xxx_chip *chip)
{
	return chip->info->family == MV88E6XXX_FAMILY_6351;
}

static bool mv88e6xxx_6352_family(struct mv88e6xxx_chip *chip)
{
	return chip->info->family == MV88E6XXX_FAMILY_6352;
}

static unsigned int mv88e6xxx_num_databases(struct mv88e6xxx_chip *chip)
{
	return chip->info->num_databases;
}

static bool mv88e6xxx_has_fid_reg(struct mv88e6xxx_chip *chip)
{
	/* Does the device have dedicated FID registers for ATU and VTU ops? */
	if (mv88e6xxx_6097_family(chip) || mv88e6xxx_6165_family(chip) ||
	    mv88e6xxx_6351_family(chip) || mv88e6xxx_6352_family(chip))
		return true;

	return false;
}

/* We expect the switch to perform auto negotiation if there is a real
 * phy. However, in the case of a fixed link phy, we force the port
 * settings from the fixed link settings.
 */
static void mv88e6xxx_adjust_link(struct dsa_switch *ds, int port,
				  struct phy_device *phydev)
{
	struct mv88e6xxx_chip *chip = ds_to_priv(ds);
	u32 reg;
	int ret;

	if (!phy_is_pseudo_fixed_link(phydev))
		return;

	mutex_lock(&chip->reg_lock);

	ret = _mv88e6xxx_reg_read(chip, REG_PORT(port), PORT_PCS_CTRL);
	if (ret < 0)
		goto out;

	reg = ret & ~(PORT_PCS_CTRL_LINK_UP |
		      PORT_PCS_CTRL_FORCE_LINK |
		      PORT_PCS_CTRL_DUPLEX_FULL |
		      PORT_PCS_CTRL_FORCE_DUPLEX |
		      PORT_PCS_CTRL_UNFORCED);

	reg |= PORT_PCS_CTRL_FORCE_LINK;
	if (phydev->link)
		reg |= PORT_PCS_CTRL_LINK_UP;

	if (mv88e6xxx_6065_family(chip) && phydev->speed > SPEED_100)
		goto out;

	switch (phydev->speed) {
	case SPEED_1000:
		reg |= PORT_PCS_CTRL_1000;
		break;
	case SPEED_100:
		reg |= PORT_PCS_CTRL_100;
		break;
	case SPEED_10:
		reg |= PORT_PCS_CTRL_10;
		break;
	default:
		pr_info("Unknown speed");
		goto out;
	}

	reg |= PORT_PCS_CTRL_FORCE_DUPLEX;
	if (phydev->duplex == DUPLEX_FULL)
		reg |= PORT_PCS_CTRL_DUPLEX_FULL;

	if ((mv88e6xxx_6352_family(chip) || mv88e6xxx_6351_family(chip)) &&
	    (port >= chip->info->num_ports - 2)) {
		if (phydev->interface == PHY_INTERFACE_MODE_RGMII_RXID)
			reg |= PORT_PCS_CTRL_RGMII_DELAY_RXCLK;
		if (phydev->interface == PHY_INTERFACE_MODE_RGMII_TXID)
			reg |= PORT_PCS_CTRL_RGMII_DELAY_TXCLK;
		if (phydev->interface == PHY_INTERFACE_MODE_RGMII_ID)
			reg |= (PORT_PCS_CTRL_RGMII_DELAY_RXCLK |
				PORT_PCS_CTRL_RGMII_DELAY_TXCLK);
	}
	_mv88e6xxx_reg_write(chip, REG_PORT(port), PORT_PCS_CTRL, reg);

out:
	mutex_unlock(&chip->reg_lock);
}

static int _mv88e6xxx_stats_wait(struct mv88e6xxx_chip *chip)
{
	int ret;
	int i;

	for (i = 0; i < 10; i++) {
		ret = _mv88e6xxx_reg_read(chip, REG_GLOBAL, GLOBAL_STATS_OP);
		if ((ret & GLOBAL_STATS_OP_BUSY) == 0)
			return 0;
	}

	return -ETIMEDOUT;
}

static int _mv88e6xxx_stats_snapshot(struct mv88e6xxx_chip *chip, int port)
{
	int ret;

	if (mv88e6xxx_6320_family(chip) || mv88e6xxx_6352_family(chip))
		port = (port + 1) << 5;

	/* Snapshot the hardware statistics counters for this port. */
	ret = _mv88e6xxx_reg_write(chip, REG_GLOBAL, GLOBAL_STATS_OP,
				   GLOBAL_STATS_OP_CAPTURE_PORT |
				   GLOBAL_STATS_OP_HIST_RX_TX | port);
	if (ret < 0)
		return ret;

	/* Wait for the snapshotting to complete. */
	ret = _mv88e6xxx_stats_wait(chip);
	if (ret < 0)
		return ret;

	return 0;
}

static void _mv88e6xxx_stats_read(struct mv88e6xxx_chip *chip,
				  int stat, u32 *val)
{
	u32 _val;
	int ret;

	*val = 0;

	ret = _mv88e6xxx_reg_write(chip, REG_GLOBAL, GLOBAL_STATS_OP,
				   GLOBAL_STATS_OP_READ_CAPTURED |
				   GLOBAL_STATS_OP_HIST_RX_TX | stat);
	if (ret < 0)
		return;

	ret = _mv88e6xxx_stats_wait(chip);
	if (ret < 0)
		return;

	ret = _mv88e6xxx_reg_read(chip, REG_GLOBAL, GLOBAL_STATS_COUNTER_32);
	if (ret < 0)
		return;

	_val = ret << 16;

	ret = _mv88e6xxx_reg_read(chip, REG_GLOBAL, GLOBAL_STATS_COUNTER_01);
	if (ret < 0)
		return;

	*val = _val | ret;
}

static struct mv88e6xxx_hw_stat mv88e6xxx_hw_stats[] = {
	{ "in_good_octets",	8, 0x00, BANK0, },
	{ "in_bad_octets",	4, 0x02, BANK0, },
	{ "in_unicast",		4, 0x04, BANK0, },
	{ "in_broadcasts",	4, 0x06, BANK0, },
	{ "in_multicasts",	4, 0x07, BANK0, },
	{ "in_pause",		4, 0x16, BANK0, },
	{ "in_undersize",	4, 0x18, BANK0, },
	{ "in_fragments",	4, 0x19, BANK0, },
	{ "in_oversize",	4, 0x1a, BANK0, },
	{ "in_jabber",		4, 0x1b, BANK0, },
	{ "in_rx_error",	4, 0x1c, BANK0, },
	{ "in_fcs_error",	4, 0x1d, BANK0, },
	{ "out_octets",		8, 0x0e, BANK0, },
	{ "out_unicast",	4, 0x10, BANK0, },
	{ "out_broadcasts",	4, 0x13, BANK0, },
	{ "out_multicasts",	4, 0x12, BANK0, },
	{ "out_pause",		4, 0x15, BANK0, },
	{ "excessive",		4, 0x11, BANK0, },
	{ "collisions",		4, 0x1e, BANK0, },
	{ "deferred",		4, 0x05, BANK0, },
	{ "single",		4, 0x14, BANK0, },
	{ "multiple",		4, 0x17, BANK0, },
	{ "out_fcs_error",	4, 0x03, BANK0, },
	{ "late",		4, 0x1f, BANK0, },
	{ "hist_64bytes",	4, 0x08, BANK0, },
	{ "hist_65_127bytes",	4, 0x09, BANK0, },
	{ "hist_128_255bytes",	4, 0x0a, BANK0, },
	{ "hist_256_511bytes",	4, 0x0b, BANK0, },
	{ "hist_512_1023bytes", 4, 0x0c, BANK0, },
	{ "hist_1024_max_bytes", 4, 0x0d, BANK0, },
	{ "sw_in_discards",	4, 0x10, PORT, },
	{ "sw_in_filtered",	2, 0x12, PORT, },
	{ "sw_out_filtered",	2, 0x13, PORT, },
	{ "in_discards",	4, 0x00 | GLOBAL_STATS_OP_BANK_1, BANK1, },
	{ "in_filtered",	4, 0x01 | GLOBAL_STATS_OP_BANK_1, BANK1, },
	{ "in_accepted",	4, 0x02 | GLOBAL_STATS_OP_BANK_1, BANK1, },
	{ "in_bad_accepted",	4, 0x03 | GLOBAL_STATS_OP_BANK_1, BANK1, },
	{ "in_good_avb_class_a", 4, 0x04 | GLOBAL_STATS_OP_BANK_1, BANK1, },
	{ "in_good_avb_class_b", 4, 0x05 | GLOBAL_STATS_OP_BANK_1, BANK1, },
	{ "in_bad_avb_class_a", 4, 0x06 | GLOBAL_STATS_OP_BANK_1, BANK1, },
	{ "in_bad_avb_class_b", 4, 0x07 | GLOBAL_STATS_OP_BANK_1, BANK1, },
	{ "tcam_counter_0",	4, 0x08 | GLOBAL_STATS_OP_BANK_1, BANK1, },
	{ "tcam_counter_1",	4, 0x09 | GLOBAL_STATS_OP_BANK_1, BANK1, },
	{ "tcam_counter_2",	4, 0x0a | GLOBAL_STATS_OP_BANK_1, BANK1, },
	{ "tcam_counter_3",	4, 0x0b | GLOBAL_STATS_OP_BANK_1, BANK1, },
	{ "in_da_unknown",	4, 0x0e | GLOBAL_STATS_OP_BANK_1, BANK1, },
	{ "in_management",	4, 0x0f | GLOBAL_STATS_OP_BANK_1, BANK1, },
	{ "out_queue_0",	4, 0x10 | GLOBAL_STATS_OP_BANK_1, BANK1, },
	{ "out_queue_1",	4, 0x11 | GLOBAL_STATS_OP_BANK_1, BANK1, },
	{ "out_queue_2",	4, 0x12 | GLOBAL_STATS_OP_BANK_1, BANK1, },
	{ "out_queue_3",	4, 0x13 | GLOBAL_STATS_OP_BANK_1, BANK1, },
	{ "out_queue_4",	4, 0x14 | GLOBAL_STATS_OP_BANK_1, BANK1, },
	{ "out_queue_5",	4, 0x15 | GLOBAL_STATS_OP_BANK_1, BANK1, },
	{ "out_queue_6",	4, 0x16 | GLOBAL_STATS_OP_BANK_1, BANK1, },
	{ "out_queue_7",	4, 0x17 | GLOBAL_STATS_OP_BANK_1, BANK1, },
	{ "out_cut_through",	4, 0x18 | GLOBAL_STATS_OP_BANK_1, BANK1, },
	{ "out_octets_a",	4, 0x1a | GLOBAL_STATS_OP_BANK_1, BANK1, },
	{ "out_octets_b",	4, 0x1b | GLOBAL_STATS_OP_BANK_1, BANK1, },
	{ "out_management",	4, 0x1f | GLOBAL_STATS_OP_BANK_1, BANK1, },
};

static bool mv88e6xxx_has_stat(struct mv88e6xxx_chip *chip,
			       struct mv88e6xxx_hw_stat *stat)
{
	switch (stat->type) {
	case BANK0:
		return true;
	case BANK1:
		return mv88e6xxx_6320_family(chip);
	case PORT:
		return mv88e6xxx_6095_family(chip) ||
			mv88e6xxx_6185_family(chip) ||
			mv88e6xxx_6097_family(chip) ||
			mv88e6xxx_6165_family(chip) ||
			mv88e6xxx_6351_family(chip) ||
			mv88e6xxx_6352_family(chip);
	}
	return false;
}

static uint64_t _mv88e6xxx_get_ethtool_stat(struct mv88e6xxx_chip *chip,
					    struct mv88e6xxx_hw_stat *s,
					    int port)
{
	u32 low;
	u32 high = 0;
	int ret;
	u64 value;

	switch (s->type) {
	case PORT:
		ret = _mv88e6xxx_reg_read(chip, REG_PORT(port), s->reg);
		if (ret < 0)
			return UINT64_MAX;

		low = ret;
		if (s->sizeof_stat == 4) {
			ret = _mv88e6xxx_reg_read(chip, REG_PORT(port),
						  s->reg + 1);
			if (ret < 0)
				return UINT64_MAX;
			high = ret;
		}
		break;
	case BANK0:
	case BANK1:
		_mv88e6xxx_stats_read(chip, s->reg, &low);
		if (s->sizeof_stat == 8)
			_mv88e6xxx_stats_read(chip, s->reg + 1, &high);
	}
	value = (((u64)high) << 16) | low;
	return value;
}

static void mv88e6xxx_get_strings(struct dsa_switch *ds, int port,
				  uint8_t *data)
{
	struct mv88e6xxx_chip *chip = ds_to_priv(ds);
	struct mv88e6xxx_hw_stat *stat;
	int i, j;

	for (i = 0, j = 0; i < ARRAY_SIZE(mv88e6xxx_hw_stats); i++) {
		stat = &mv88e6xxx_hw_stats[i];
		if (mv88e6xxx_has_stat(chip, stat)) {
			memcpy(data + j * ETH_GSTRING_LEN, stat->string,
			       ETH_GSTRING_LEN);
			j++;
		}
	}
}

static int mv88e6xxx_get_sset_count(struct dsa_switch *ds)
{
	struct mv88e6xxx_chip *chip = ds_to_priv(ds);
	struct mv88e6xxx_hw_stat *stat;
	int i, j;

	for (i = 0, j = 0; i < ARRAY_SIZE(mv88e6xxx_hw_stats); i++) {
		stat = &mv88e6xxx_hw_stats[i];
		if (mv88e6xxx_has_stat(chip, stat))
			j++;
	}
	return j;
}

static void mv88e6xxx_get_ethtool_stats(struct dsa_switch *ds, int port,
					uint64_t *data)
{
	struct mv88e6xxx_chip *chip = ds_to_priv(ds);
	struct mv88e6xxx_hw_stat *stat;
	int ret;
	int i, j;

	mutex_lock(&chip->reg_lock);

	ret = _mv88e6xxx_stats_snapshot(chip, port);
	if (ret < 0) {
		mutex_unlock(&chip->reg_lock);
		return;
	}
	for (i = 0, j = 0; i < ARRAY_SIZE(mv88e6xxx_hw_stats); i++) {
		stat = &mv88e6xxx_hw_stats[i];
		if (mv88e6xxx_has_stat(chip, stat)) {
			data[j] = _mv88e6xxx_get_ethtool_stat(chip, stat, port);
			j++;
		}
	}

	mutex_unlock(&chip->reg_lock);
}

static int mv88e6xxx_get_regs_len(struct dsa_switch *ds, int port)
{
	return 32 * sizeof(u16);
}

static void mv88e6xxx_get_regs(struct dsa_switch *ds, int port,
			       struct ethtool_regs *regs, void *_p)
{
	struct mv88e6xxx_chip *chip = ds_to_priv(ds);
	u16 *p = _p;
	int i;

	regs->version = 0;

	memset(p, 0xff, 32 * sizeof(u16));

	mutex_lock(&chip->reg_lock);

	for (i = 0; i < 32; i++) {
		int ret;

		ret = _mv88e6xxx_reg_read(chip, REG_PORT(port), i);
		if (ret >= 0)
			p[i] = ret;
	}

	mutex_unlock(&chip->reg_lock);
}

static int _mv88e6xxx_wait(struct mv88e6xxx_chip *chip, int reg, int offset,
			   u16 mask)
{
	unsigned long timeout = jiffies + HZ / 10;

	while (time_before(jiffies, timeout)) {
		int ret;

		ret = _mv88e6xxx_reg_read(chip, reg, offset);
		if (ret < 0)
			return ret;
		if (!(ret & mask))
			return 0;

		usleep_range(1000, 2000);
	}
	return -ETIMEDOUT;
}

static int mv88e6xxx_wait(struct mv88e6xxx_chip *chip, int reg,
			  int offset, u16 mask)
{
	int ret;

	mutex_lock(&chip->reg_lock);
	ret = _mv88e6xxx_wait(chip, reg, offset, mask);
	mutex_unlock(&chip->reg_lock);

	return ret;
}

static int mv88e6xxx_mdio_wait(struct mv88e6xxx_chip *chip)
{
	return _mv88e6xxx_wait(chip, REG_GLOBAL2, GLOBAL2_SMI_OP,
			       GLOBAL2_SMI_OP_BUSY);
}

static int mv88e6xxx_eeprom_load_wait(struct dsa_switch *ds)
{
	struct mv88e6xxx_chip *chip = ds_to_priv(ds);

	return mv88e6xxx_wait(chip, REG_GLOBAL2, GLOBAL2_EEPROM_OP,
			      GLOBAL2_EEPROM_OP_LOAD);
}

static int mv88e6xxx_eeprom_busy_wait(struct dsa_switch *ds)
{
	struct mv88e6xxx_chip *chip = ds_to_priv(ds);

	return mv88e6xxx_wait(chip, REG_GLOBAL2, GLOBAL2_EEPROM_OP,
			      GLOBAL2_EEPROM_OP_BUSY);
}

static int mv88e6xxx_read_eeprom_word(struct dsa_switch *ds, int addr)
{
	struct mv88e6xxx_chip *chip = ds_to_priv(ds);
	int ret;

	mutex_lock(&chip->eeprom_mutex);

	ret = mv88e6xxx_reg_write(chip, REG_GLOBAL2, GLOBAL2_EEPROM_OP,
				  GLOBAL2_EEPROM_OP_READ |
				  (addr & GLOBAL2_EEPROM_OP_ADDR_MASK));
	if (ret < 0)
		goto error;

	ret = mv88e6xxx_eeprom_busy_wait(ds);
	if (ret < 0)
		goto error;

	ret = mv88e6xxx_reg_read(chip, REG_GLOBAL2, GLOBAL2_EEPROM_DATA);
error:
	mutex_unlock(&chip->eeprom_mutex);
	return ret;
}

static int mv88e6xxx_get_eeprom_len(struct dsa_switch *ds)
{
	struct mv88e6xxx_chip *chip = ds_to_priv(ds);

	if (mv88e6xxx_has(chip, MV88E6XXX_FLAG_EEPROM))
		return chip->eeprom_len;

	return 0;
}

static int mv88e6xxx_get_eeprom(struct dsa_switch *ds,
				struct ethtool_eeprom *eeprom, u8 *data)
{
	struct mv88e6xxx_chip *chip = ds_to_priv(ds);
	int offset;
	int len;
	int ret;

	if (!mv88e6xxx_has(chip, MV88E6XXX_FLAG_EEPROM))
		return -EOPNOTSUPP;

	offset = eeprom->offset;
	len = eeprom->len;
	eeprom->len = 0;

	eeprom->magic = 0xc3ec4951;

	ret = mv88e6xxx_eeprom_load_wait(ds);
	if (ret < 0)
		return ret;

	if (offset & 1) {
		int word;

		word = mv88e6xxx_read_eeprom_word(ds, offset >> 1);
		if (word < 0)
			return word;

		*data++ = (word >> 8) & 0xff;

		offset++;
		len--;
		eeprom->len++;
	}

	while (len >= 2) {
		int word;

		word = mv88e6xxx_read_eeprom_word(ds, offset >> 1);
		if (word < 0)
			return word;

		*data++ = word & 0xff;
		*data++ = (word >> 8) & 0xff;

		offset += 2;
		len -= 2;
		eeprom->len += 2;
	}

	if (len) {
		int word;

		word = mv88e6xxx_read_eeprom_word(ds, offset >> 1);
		if (word < 0)
			return word;

		*data++ = word & 0xff;

		offset++;
		len--;
		eeprom->len++;
	}

	return 0;
}

static int mv88e6xxx_eeprom_is_readonly(struct dsa_switch *ds)
{
	struct mv88e6xxx_chip *chip = ds_to_priv(ds);
	int ret;

	ret = mv88e6xxx_reg_read(chip, REG_GLOBAL2, GLOBAL2_EEPROM_OP);
	if (ret < 0)
		return ret;

	if (!(ret & GLOBAL2_EEPROM_OP_WRITE_EN))
		return -EROFS;

	return 0;
}

static int mv88e6xxx_write_eeprom_word(struct dsa_switch *ds, int addr,
				       u16 data)
{
	struct mv88e6xxx_chip *chip = ds_to_priv(ds);
	int ret;

	mutex_lock(&chip->eeprom_mutex);

	ret = mv88e6xxx_reg_write(chip, REG_GLOBAL2, GLOBAL2_EEPROM_DATA, data);
	if (ret < 0)
		goto error;

	ret = mv88e6xxx_reg_write(chip, REG_GLOBAL2, GLOBAL2_EEPROM_OP,
				  GLOBAL2_EEPROM_OP_WRITE |
				  (addr & GLOBAL2_EEPROM_OP_ADDR_MASK));
	if (ret < 0)
		goto error;

	ret = mv88e6xxx_eeprom_busy_wait(ds);
error:
	mutex_unlock(&chip->eeprom_mutex);
	return ret;
}

static int mv88e6xxx_set_eeprom(struct dsa_switch *ds,
				struct ethtool_eeprom *eeprom, u8 *data)
{
	struct mv88e6xxx_chip *chip = ds_to_priv(ds);
	int offset;
	int ret;
	int len;

	if (!mv88e6xxx_has(chip, MV88E6XXX_FLAG_EEPROM))
		return -EOPNOTSUPP;

	if (eeprom->magic != 0xc3ec4951)
		return -EINVAL;

	ret = mv88e6xxx_eeprom_is_readonly(ds);
	if (ret)
		return ret;

	offset = eeprom->offset;
	len = eeprom->len;
	eeprom->len = 0;

	ret = mv88e6xxx_eeprom_load_wait(ds);
	if (ret < 0)
		return ret;

	if (offset & 1) {
		int word;

		word = mv88e6xxx_read_eeprom_word(ds, offset >> 1);
		if (word < 0)
			return word;

		word = (*data++ << 8) | (word & 0xff);

		ret = mv88e6xxx_write_eeprom_word(ds, offset >> 1, word);
		if (ret < 0)
			return ret;

		offset++;
		len--;
		eeprom->len++;
	}

	while (len >= 2) {
		int word;

		word = *data++;
		word |= *data++ << 8;

		ret = mv88e6xxx_write_eeprom_word(ds, offset >> 1, word);
		if (ret < 0)
			return ret;

		offset += 2;
		len -= 2;
		eeprom->len += 2;
	}

	if (len) {
		int word;

		word = mv88e6xxx_read_eeprom_word(ds, offset >> 1);
		if (word < 0)
			return word;

		word = (word & 0xff00) | *data++;

		ret = mv88e6xxx_write_eeprom_word(ds, offset >> 1, word);
		if (ret < 0)
			return ret;

		offset++;
		len--;
		eeprom->len++;
	}

	return 0;
}

static int _mv88e6xxx_atu_wait(struct mv88e6xxx_chip *chip)
{
	return _mv88e6xxx_wait(chip, REG_GLOBAL, GLOBAL_ATU_OP,
			       GLOBAL_ATU_OP_BUSY);
}

static int mv88e6xxx_mdio_read_indirect(struct mv88e6xxx_chip *chip,
					int addr, int regnum)
{
	int ret;

	ret = _mv88e6xxx_reg_write(chip, REG_GLOBAL2, GLOBAL2_SMI_OP,
				   GLOBAL2_SMI_OP_22_READ | (addr << 5) |
				   regnum);
	if (ret < 0)
		return ret;

	ret = mv88e6xxx_mdio_wait(chip);
	if (ret < 0)
		return ret;

	ret = _mv88e6xxx_reg_read(chip, REG_GLOBAL2, GLOBAL2_SMI_DATA);

	return ret;
}

static int mv88e6xxx_mdio_write_indirect(struct mv88e6xxx_chip *chip,
					 int addr, int regnum, u16 val)
{
	int ret;

	ret = _mv88e6xxx_reg_write(chip, REG_GLOBAL2, GLOBAL2_SMI_DATA, val);
	if (ret < 0)
		return ret;

	ret = _mv88e6xxx_reg_write(chip, REG_GLOBAL2, GLOBAL2_SMI_OP,
				   GLOBAL2_SMI_OP_22_WRITE | (addr << 5) |
				   regnum);

	return mv88e6xxx_mdio_wait(chip);
}

static int mv88e6xxx_get_eee(struct dsa_switch *ds, int port,
			     struct ethtool_eee *e)
{
	struct mv88e6xxx_chip *chip = ds_to_priv(ds);
	int reg;

	if (!mv88e6xxx_has(chip, MV88E6XXX_FLAG_EEE))
		return -EOPNOTSUPP;

	mutex_lock(&chip->reg_lock);

	reg = mv88e6xxx_mdio_read_indirect(chip, port, 16);
	if (reg < 0)
		goto out;

	e->eee_enabled = !!(reg & 0x0200);
	e->tx_lpi_enabled = !!(reg & 0x0100);

	reg = _mv88e6xxx_reg_read(chip, REG_PORT(port), PORT_STATUS);
	if (reg < 0)
		goto out;

	e->eee_active = !!(reg & PORT_STATUS_EEE);
	reg = 0;

out:
	mutex_unlock(&chip->reg_lock);
	return reg;
}

static int mv88e6xxx_set_eee(struct dsa_switch *ds, int port,
			     struct phy_device *phydev, struct ethtool_eee *e)
{
	struct mv88e6xxx_chip *chip = ds_to_priv(ds);
	int reg;
	int ret;

	if (!mv88e6xxx_has(chip, MV88E6XXX_FLAG_EEE))
		return -EOPNOTSUPP;

	mutex_lock(&chip->reg_lock);

	ret = mv88e6xxx_mdio_read_indirect(chip, port, 16);
	if (ret < 0)
		goto out;

	reg = ret & ~0x0300;
	if (e->eee_enabled)
		reg |= 0x0200;
	if (e->tx_lpi_enabled)
		reg |= 0x0100;

	ret = mv88e6xxx_mdio_write_indirect(chip, port, 16, reg);
out:
	mutex_unlock(&chip->reg_lock);

	return ret;
}

static int _mv88e6xxx_atu_cmd(struct mv88e6xxx_chip *chip, u16 fid, u16 cmd)
{
	int ret;

	if (mv88e6xxx_has_fid_reg(chip)) {
		ret = _mv88e6xxx_reg_write(chip, REG_GLOBAL, GLOBAL_ATU_FID,
					   fid);
		if (ret < 0)
			return ret;
	} else if (mv88e6xxx_num_databases(chip) == 256) {
		/* ATU DBNum[7:4] are located in ATU Control 15:12 */
		ret = _mv88e6xxx_reg_read(chip, REG_GLOBAL, GLOBAL_ATU_CONTROL);
		if (ret < 0)
			return ret;

		ret = _mv88e6xxx_reg_write(chip, REG_GLOBAL, GLOBAL_ATU_CONTROL,
					   (ret & 0xfff) |
					   ((fid << 8) & 0xf000));
		if (ret < 0)
			return ret;

		/* ATU DBNum[3:0] are located in ATU Operation 3:0 */
		cmd |= fid & 0xf;
	}

	ret = _mv88e6xxx_reg_write(chip, REG_GLOBAL, GLOBAL_ATU_OP, cmd);
	if (ret < 0)
		return ret;

	return _mv88e6xxx_atu_wait(chip);
}

static int _mv88e6xxx_atu_data_write(struct mv88e6xxx_chip *chip,
				     struct mv88e6xxx_atu_entry *entry)
{
	u16 data = entry->state & GLOBAL_ATU_DATA_STATE_MASK;

	if (entry->state != GLOBAL_ATU_DATA_STATE_UNUSED) {
		unsigned int mask, shift;

		if (entry->trunk) {
			data |= GLOBAL_ATU_DATA_TRUNK;
			mask = GLOBAL_ATU_DATA_TRUNK_ID_MASK;
			shift = GLOBAL_ATU_DATA_TRUNK_ID_SHIFT;
		} else {
			mask = GLOBAL_ATU_DATA_PORT_VECTOR_MASK;
			shift = GLOBAL_ATU_DATA_PORT_VECTOR_SHIFT;
		}

		data |= (entry->portv_trunkid << shift) & mask;
	}

	return _mv88e6xxx_reg_write(chip, REG_GLOBAL, GLOBAL_ATU_DATA, data);
}

static int _mv88e6xxx_atu_flush_move(struct mv88e6xxx_chip *chip,
				     struct mv88e6xxx_atu_entry *entry,
				     bool static_too)
{
	int op;
	int err;

	err = _mv88e6xxx_atu_wait(chip);
	if (err)
		return err;

	err = _mv88e6xxx_atu_data_write(chip, entry);
	if (err)
		return err;

	if (entry->fid) {
		op = static_too ? GLOBAL_ATU_OP_FLUSH_MOVE_ALL_DB :
			GLOBAL_ATU_OP_FLUSH_MOVE_NON_STATIC_DB;
	} else {
		op = static_too ? GLOBAL_ATU_OP_FLUSH_MOVE_ALL :
			GLOBAL_ATU_OP_FLUSH_MOVE_NON_STATIC;
	}

	return _mv88e6xxx_atu_cmd(chip, entry->fid, op);
}

static int _mv88e6xxx_atu_flush(struct mv88e6xxx_chip *chip,
				u16 fid, bool static_too)
{
	struct mv88e6xxx_atu_entry entry = {
		.fid = fid,
		.state = 0, /* EntryState bits must be 0 */
	};

	return _mv88e6xxx_atu_flush_move(chip, &entry, static_too);
}

static int _mv88e6xxx_atu_move(struct mv88e6xxx_chip *chip, u16 fid,
			       int from_port, int to_port, bool static_too)
{
	struct mv88e6xxx_atu_entry entry = {
		.trunk = false,
		.fid = fid,
	};

	/* EntryState bits must be 0xF */
	entry.state = GLOBAL_ATU_DATA_STATE_MASK;

	/* ToPort and FromPort are respectively in PortVec bits 7:4 and 3:0 */
	entry.portv_trunkid = (to_port & 0x0f) << 4;
	entry.portv_trunkid |= from_port & 0x0f;

	return _mv88e6xxx_atu_flush_move(chip, &entry, static_too);
}

static int _mv88e6xxx_atu_remove(struct mv88e6xxx_chip *chip, u16 fid,
				 int port, bool static_too)
{
	/* Destination port 0xF means remove the entries */
	return _mv88e6xxx_atu_move(chip, fid, port, 0x0f, static_too);
}

static const char * const mv88e6xxx_port_state_names[] = {
	[PORT_CONTROL_STATE_DISABLED] = "Disabled",
	[PORT_CONTROL_STATE_BLOCKING] = "Blocking/Listening",
	[PORT_CONTROL_STATE_LEARNING] = "Learning",
	[PORT_CONTROL_STATE_FORWARDING] = "Forwarding",
};

static int _mv88e6xxx_port_state(struct mv88e6xxx_chip *chip, int port,
				 u8 state)
{
	struct dsa_switch *ds = chip->ds;
	int reg, ret = 0;
	u8 oldstate;

	reg = _mv88e6xxx_reg_read(chip, REG_PORT(port), PORT_CONTROL);
	if (reg < 0)
		return reg;

	oldstate = reg & PORT_CONTROL_STATE_MASK;

	if (oldstate != state) {
		/* Flush forwarding database if we're moving a port
		 * from Learning or Forwarding state to Disabled or
		 * Blocking or Listening state.
		 */
		if ((oldstate == PORT_CONTROL_STATE_LEARNING ||
		     oldstate == PORT_CONTROL_STATE_FORWARDING) &&
		    (state == PORT_CONTROL_STATE_DISABLED ||
		     state == PORT_CONTROL_STATE_BLOCKING)) {
			ret = _mv88e6xxx_atu_remove(chip, 0, port, false);
			if (ret)
				return ret;
		}

		reg = (reg & ~PORT_CONTROL_STATE_MASK) | state;
		ret = _mv88e6xxx_reg_write(chip, REG_PORT(port), PORT_CONTROL,
					   reg);
		if (ret)
			return ret;

		netdev_dbg(ds->ports[port].netdev, "PortState %s (was %s)\n",
			   mv88e6xxx_port_state_names[state],
			   mv88e6xxx_port_state_names[oldstate]);
	}

	return ret;
}

static int _mv88e6xxx_port_based_vlan_map(struct mv88e6xxx_chip *chip, int port)
{
	struct net_device *bridge = chip->ports[port].bridge_dev;
	const u16 mask = (1 << chip->info->num_ports) - 1;
	struct dsa_switch *ds = chip->ds;
	u16 output_ports = 0;
	int reg;
	int i;

	/* allow CPU port or DSA link(s) to send frames to every port */
	if (dsa_is_cpu_port(ds, port) || dsa_is_dsa_port(ds, port)) {
		output_ports = mask;
	} else {
		for (i = 0; i < chip->info->num_ports; ++i) {
			/* allow sending frames to every group member */
			if (bridge && chip->ports[i].bridge_dev == bridge)
				output_ports |= BIT(i);

			/* allow sending frames to CPU port and DSA link(s) */
			if (dsa_is_cpu_port(ds, i) || dsa_is_dsa_port(ds, i))
				output_ports |= BIT(i);
		}
	}

	/* prevent frames from going back out of the port they came in on */
	output_ports &= ~BIT(port);

	reg = _mv88e6xxx_reg_read(chip, REG_PORT(port), PORT_BASE_VLAN);
	if (reg < 0)
		return reg;

	reg &= ~mask;
	reg |= output_ports & mask;

	return _mv88e6xxx_reg_write(chip, REG_PORT(port), PORT_BASE_VLAN, reg);
}

static void mv88e6xxx_port_stp_state_set(struct dsa_switch *ds, int port,
					 u8 state)
{
	struct mv88e6xxx_chip *chip = ds_to_priv(ds);
	int stp_state;
	int err;

	switch (state) {
	case BR_STATE_DISABLED:
		stp_state = PORT_CONTROL_STATE_DISABLED;
		break;
	case BR_STATE_BLOCKING:
	case BR_STATE_LISTENING:
		stp_state = PORT_CONTROL_STATE_BLOCKING;
		break;
	case BR_STATE_LEARNING:
		stp_state = PORT_CONTROL_STATE_LEARNING;
		break;
	case BR_STATE_FORWARDING:
	default:
		stp_state = PORT_CONTROL_STATE_FORWARDING;
		break;
	}

	mutex_lock(&chip->reg_lock);
	err = _mv88e6xxx_port_state(chip, port, stp_state);
	mutex_unlock(&chip->reg_lock);

	if (err)
		netdev_err(ds->ports[port].netdev,
			   "failed to update state to %s\n",
			   mv88e6xxx_port_state_names[stp_state]);
}

static int _mv88e6xxx_port_pvid(struct mv88e6xxx_chip *chip, int port,
				u16 *new, u16 *old)
{
	struct dsa_switch *ds = chip->ds;
	u16 pvid;
	int ret;

	ret = _mv88e6xxx_reg_read(chip, REG_PORT(port), PORT_DEFAULT_VLAN);
	if (ret < 0)
		return ret;

	pvid = ret & PORT_DEFAULT_VLAN_MASK;

	if (new) {
		ret &= ~PORT_DEFAULT_VLAN_MASK;
		ret |= *new & PORT_DEFAULT_VLAN_MASK;

		ret = _mv88e6xxx_reg_write(chip, REG_PORT(port),
					   PORT_DEFAULT_VLAN, ret);
		if (ret < 0)
			return ret;

		netdev_dbg(ds->ports[port].netdev,
			   "DefaultVID %d (was %d)\n", *new, pvid);
	}

	if (old)
		*old = pvid;

	return 0;
}

static int _mv88e6xxx_port_pvid_get(struct mv88e6xxx_chip *chip,
				    int port, u16 *pvid)
{
	return _mv88e6xxx_port_pvid(chip, port, NULL, pvid);
}

static int _mv88e6xxx_port_pvid_set(struct mv88e6xxx_chip *chip,
				    int port, u16 pvid)
{
	return _mv88e6xxx_port_pvid(chip, port, &pvid, NULL);
}

static int _mv88e6xxx_vtu_wait(struct mv88e6xxx_chip *chip)
{
	return _mv88e6xxx_wait(chip, REG_GLOBAL, GLOBAL_VTU_OP,
			       GLOBAL_VTU_OP_BUSY);
}

static int _mv88e6xxx_vtu_cmd(struct mv88e6xxx_chip *chip, u16 op)
{
	int ret;

	ret = _mv88e6xxx_reg_write(chip, REG_GLOBAL, GLOBAL_VTU_OP, op);
	if (ret < 0)
		return ret;

	return _mv88e6xxx_vtu_wait(chip);
}

static int _mv88e6xxx_vtu_stu_flush(struct mv88e6xxx_chip *chip)
{
	int ret;

	ret = _mv88e6xxx_vtu_wait(chip);
	if (ret < 0)
		return ret;

	return _mv88e6xxx_vtu_cmd(chip, GLOBAL_VTU_OP_FLUSH_ALL);
}

static int _mv88e6xxx_vtu_stu_data_read(struct mv88e6xxx_chip *chip,
					struct mv88e6xxx_vtu_stu_entry *entry,
					unsigned int nibble_offset)
{
	u16 regs[3];
	int i;
	int ret;

	for (i = 0; i < 3; ++i) {
		ret = _mv88e6xxx_reg_read(chip, REG_GLOBAL,
					  GLOBAL_VTU_DATA_0_3 + i);
		if (ret < 0)
			return ret;

		regs[i] = ret;
	}

	for (i = 0; i < chip->info->num_ports; ++i) {
		unsigned int shift = (i % 4) * 4 + nibble_offset;
		u16 reg = regs[i / 4];

		entry->data[i] = (reg >> shift) & GLOBAL_VTU_STU_DATA_MASK;
	}

	return 0;
}

static int mv88e6xxx_vtu_data_read(struct mv88e6xxx_chip *chip,
				   struct mv88e6xxx_vtu_stu_entry *entry)
{
	return _mv88e6xxx_vtu_stu_data_read(chip, entry, 0);
}

static int mv88e6xxx_stu_data_read(struct mv88e6xxx_chip *chip,
				   struct mv88e6xxx_vtu_stu_entry *entry)
{
	return _mv88e6xxx_vtu_stu_data_read(chip, entry, 2);
}

static int _mv88e6xxx_vtu_stu_data_write(struct mv88e6xxx_chip *chip,
					 struct mv88e6xxx_vtu_stu_entry *entry,
					 unsigned int nibble_offset)
{
	u16 regs[3] = { 0 };
	int i;
	int ret;

	for (i = 0; i < chip->info->num_ports; ++i) {
		unsigned int shift = (i % 4) * 4 + nibble_offset;
		u8 data = entry->data[i];

		regs[i / 4] |= (data & GLOBAL_VTU_STU_DATA_MASK) << shift;
	}

	for (i = 0; i < 3; ++i) {
		ret = _mv88e6xxx_reg_write(chip, REG_GLOBAL,
					   GLOBAL_VTU_DATA_0_3 + i, regs[i]);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static int mv88e6xxx_vtu_data_write(struct mv88e6xxx_chip *chip,
				    struct mv88e6xxx_vtu_stu_entry *entry)
{
	return _mv88e6xxx_vtu_stu_data_write(chip, entry, 0);
}

static int mv88e6xxx_stu_data_write(struct mv88e6xxx_chip *chip,
				    struct mv88e6xxx_vtu_stu_entry *entry)
{
	return _mv88e6xxx_vtu_stu_data_write(chip, entry, 2);
}

static int _mv88e6xxx_vtu_vid_write(struct mv88e6xxx_chip *chip, u16 vid)
{
	return _mv88e6xxx_reg_write(chip, REG_GLOBAL, GLOBAL_VTU_VID,
				    vid & GLOBAL_VTU_VID_MASK);
}

static int _mv88e6xxx_vtu_getnext(struct mv88e6xxx_chip *chip,
				  struct mv88e6xxx_vtu_stu_entry *entry)
{
	struct mv88e6xxx_vtu_stu_entry next = { 0 };
	int ret;

	ret = _mv88e6xxx_vtu_wait(chip);
	if (ret < 0)
		return ret;

	ret = _mv88e6xxx_vtu_cmd(chip, GLOBAL_VTU_OP_VTU_GET_NEXT);
	if (ret < 0)
		return ret;

	ret = _mv88e6xxx_reg_read(chip, REG_GLOBAL, GLOBAL_VTU_VID);
	if (ret < 0)
		return ret;

	next.vid = ret & GLOBAL_VTU_VID_MASK;
	next.valid = !!(ret & GLOBAL_VTU_VID_VALID);

	if (next.valid) {
		ret = mv88e6xxx_vtu_data_read(chip, &next);
		if (ret < 0)
			return ret;

		if (mv88e6xxx_has_fid_reg(chip)) {
			ret = _mv88e6xxx_reg_read(chip, REG_GLOBAL,
						  GLOBAL_VTU_FID);
			if (ret < 0)
				return ret;

			next.fid = ret & GLOBAL_VTU_FID_MASK;
		} else if (mv88e6xxx_num_databases(chip) == 256) {
			/* VTU DBNum[7:4] are located in VTU Operation 11:8, and
			 * VTU DBNum[3:0] are located in VTU Operation 3:0
			 */
			ret = _mv88e6xxx_reg_read(chip, REG_GLOBAL,
						  GLOBAL_VTU_OP);
			if (ret < 0)
				return ret;

			next.fid = (ret & 0xf00) >> 4;
			next.fid |= ret & 0xf;
		}

		if (mv88e6xxx_has(chip, MV88E6XXX_FLAG_STU)) {
			ret = _mv88e6xxx_reg_read(chip, REG_GLOBAL,
						  GLOBAL_VTU_SID);
			if (ret < 0)
				return ret;

			next.sid = ret & GLOBAL_VTU_SID_MASK;
		}
	}

	*entry = next;
	return 0;
}

static int mv88e6xxx_port_vlan_dump(struct dsa_switch *ds, int port,
				    struct switchdev_obj_port_vlan *vlan,
				    int (*cb)(struct switchdev_obj *obj))
{
	struct mv88e6xxx_chip *chip = ds_to_priv(ds);
	struct mv88e6xxx_vtu_stu_entry next;
	u16 pvid;
	int err;

	if (!mv88e6xxx_has(chip, MV88E6XXX_FLAG_VTU))
		return -EOPNOTSUPP;

	mutex_lock(&chip->reg_lock);

	err = _mv88e6xxx_port_pvid_get(chip, port, &pvid);
	if (err)
		goto unlock;

	err = _mv88e6xxx_vtu_vid_write(chip, GLOBAL_VTU_VID_MASK);
	if (err)
		goto unlock;

	do {
		err = _mv88e6xxx_vtu_getnext(chip, &next);
		if (err)
			break;

		if (!next.valid)
			break;

		if (next.data[port] == GLOBAL_VTU_DATA_MEMBER_TAG_NON_MEMBER)
			continue;

		/* reinit and dump this VLAN obj */
		vlan->vid_begin = next.vid;
		vlan->vid_end = next.vid;
		vlan->flags = 0;

		if (next.data[port] == GLOBAL_VTU_DATA_MEMBER_TAG_UNTAGGED)
			vlan->flags |= BRIDGE_VLAN_INFO_UNTAGGED;

		if (next.vid == pvid)
			vlan->flags |= BRIDGE_VLAN_INFO_PVID;

		err = cb(&vlan->obj);
		if (err)
			break;
	} while (next.vid < GLOBAL_VTU_VID_MASK);

unlock:
	mutex_unlock(&chip->reg_lock);

	return err;
}

static int _mv88e6xxx_vtu_loadpurge(struct mv88e6xxx_chip *chip,
				    struct mv88e6xxx_vtu_stu_entry *entry)
{
	u16 op = GLOBAL_VTU_OP_VTU_LOAD_PURGE;
	u16 reg = 0;
	int ret;

	ret = _mv88e6xxx_vtu_wait(chip);
	if (ret < 0)
		return ret;

	if (!entry->valid)
		goto loadpurge;

	/* Write port member tags */
	ret = mv88e6xxx_vtu_data_write(chip, entry);
	if (ret < 0)
		return ret;

	if (mv88e6xxx_has(chip, MV88E6XXX_FLAG_STU)) {
		reg = entry->sid & GLOBAL_VTU_SID_MASK;
		ret = _mv88e6xxx_reg_write(chip, REG_GLOBAL, GLOBAL_VTU_SID,
					   reg);
		if (ret < 0)
			return ret;
	}

	if (mv88e6xxx_has_fid_reg(chip)) {
		reg = entry->fid & GLOBAL_VTU_FID_MASK;
		ret = _mv88e6xxx_reg_write(chip, REG_GLOBAL, GLOBAL_VTU_FID,
					   reg);
		if (ret < 0)
			return ret;
	} else if (mv88e6xxx_num_databases(chip) == 256) {
		/* VTU DBNum[7:4] are located in VTU Operation 11:8, and
		 * VTU DBNum[3:0] are located in VTU Operation 3:0
		 */
		op |= (entry->fid & 0xf0) << 8;
		op |= entry->fid & 0xf;
	}

	reg = GLOBAL_VTU_VID_VALID;
loadpurge:
	reg |= entry->vid & GLOBAL_VTU_VID_MASK;
	ret = _mv88e6xxx_reg_write(chip, REG_GLOBAL, GLOBAL_VTU_VID, reg);
	if (ret < 0)
		return ret;

	return _mv88e6xxx_vtu_cmd(chip, op);
}

static int _mv88e6xxx_stu_getnext(struct mv88e6xxx_chip *chip, u8 sid,
				  struct mv88e6xxx_vtu_stu_entry *entry)
{
	struct mv88e6xxx_vtu_stu_entry next = { 0 };
	int ret;

	ret = _mv88e6xxx_vtu_wait(chip);
	if (ret < 0)
		return ret;

	ret = _mv88e6xxx_reg_write(chip, REG_GLOBAL, GLOBAL_VTU_SID,
				   sid & GLOBAL_VTU_SID_MASK);
	if (ret < 0)
		return ret;

	ret = _mv88e6xxx_vtu_cmd(chip, GLOBAL_VTU_OP_STU_GET_NEXT);
	if (ret < 0)
		return ret;

	ret = _mv88e6xxx_reg_read(chip, REG_GLOBAL, GLOBAL_VTU_SID);
	if (ret < 0)
		return ret;

	next.sid = ret & GLOBAL_VTU_SID_MASK;

	ret = _mv88e6xxx_reg_read(chip, REG_GLOBAL, GLOBAL_VTU_VID);
	if (ret < 0)
		return ret;

	next.valid = !!(ret & GLOBAL_VTU_VID_VALID);

	if (next.valid) {
		ret = mv88e6xxx_stu_data_read(chip, &next);
		if (ret < 0)
			return ret;
	}

	*entry = next;
	return 0;
}

static int _mv88e6xxx_stu_loadpurge(struct mv88e6xxx_chip *chip,
				    struct mv88e6xxx_vtu_stu_entry *entry)
{
	u16 reg = 0;
	int ret;

	ret = _mv88e6xxx_vtu_wait(chip);
	if (ret < 0)
		return ret;

	if (!entry->valid)
		goto loadpurge;

	/* Write port states */
	ret = mv88e6xxx_stu_data_write(chip, entry);
	if (ret < 0)
		return ret;

	reg = GLOBAL_VTU_VID_VALID;
loadpurge:
	ret = _mv88e6xxx_reg_write(chip, REG_GLOBAL, GLOBAL_VTU_VID, reg);
	if (ret < 0)
		return ret;

	reg = entry->sid & GLOBAL_VTU_SID_MASK;
	ret = _mv88e6xxx_reg_write(chip, REG_GLOBAL, GLOBAL_VTU_SID, reg);
	if (ret < 0)
		return ret;

	return _mv88e6xxx_vtu_cmd(chip, GLOBAL_VTU_OP_STU_LOAD_PURGE);
}

static int _mv88e6xxx_port_fid(struct mv88e6xxx_chip *chip, int port,
			       u16 *new, u16 *old)
{
	struct dsa_switch *ds = chip->ds;
	u16 upper_mask;
	u16 fid;
	int ret;

	if (mv88e6xxx_num_databases(chip) == 4096)
		upper_mask = 0xff;
	else if (mv88e6xxx_num_databases(chip) == 256)
		upper_mask = 0xf;
	else
		return -EOPNOTSUPP;

	/* Port's default FID bits 3:0 are located in reg 0x06, offset 12 */
	ret = _mv88e6xxx_reg_read(chip, REG_PORT(port), PORT_BASE_VLAN);
	if (ret < 0)
		return ret;

	fid = (ret & PORT_BASE_VLAN_FID_3_0_MASK) >> 12;

	if (new) {
		ret &= ~PORT_BASE_VLAN_FID_3_0_MASK;
		ret |= (*new << 12) & PORT_BASE_VLAN_FID_3_0_MASK;

		ret = _mv88e6xxx_reg_write(chip, REG_PORT(port), PORT_BASE_VLAN,
					   ret);
		if (ret < 0)
			return ret;
	}

	/* Port's default FID bits 11:4 are located in reg 0x05, offset 0 */
	ret = _mv88e6xxx_reg_read(chip, REG_PORT(port), PORT_CONTROL_1);
	if (ret < 0)
		return ret;

	fid |= (ret & upper_mask) << 4;

	if (new) {
		ret &= ~upper_mask;
		ret |= (*new >> 4) & upper_mask;

		ret = _mv88e6xxx_reg_write(chip, REG_PORT(port), PORT_CONTROL_1,
					   ret);
		if (ret < 0)
			return ret;

		netdev_dbg(ds->ports[port].netdev,
			   "FID %d (was %d)\n", *new, fid);
	}

	if (old)
		*old = fid;

	return 0;
}

static int _mv88e6xxx_port_fid_get(struct mv88e6xxx_chip *chip,
				   int port, u16 *fid)
{
	return _mv88e6xxx_port_fid(chip, port, NULL, fid);
}

static int _mv88e6xxx_port_fid_set(struct mv88e6xxx_chip *chip,
				   int port, u16 fid)
{
	return _mv88e6xxx_port_fid(chip, port, &fid, NULL);
}

static int _mv88e6xxx_fid_new(struct mv88e6xxx_chip *chip, u16 *fid)
{
	DECLARE_BITMAP(fid_bitmap, MV88E6XXX_N_FID);
	struct mv88e6xxx_vtu_stu_entry vlan;
	int i, err;

	bitmap_zero(fid_bitmap, MV88E6XXX_N_FID);

	/* Set every FID bit used by the (un)bridged ports */
	for (i = 0; i < chip->info->num_ports; ++i) {
		err = _mv88e6xxx_port_fid_get(chip, i, fid);
		if (err)
			return err;

		set_bit(*fid, fid_bitmap);
	}

	/* Set every FID bit used by the VLAN entries */
	err = _mv88e6xxx_vtu_vid_write(chip, GLOBAL_VTU_VID_MASK);
	if (err)
		return err;

	do {
		err = _mv88e6xxx_vtu_getnext(chip, &vlan);
		if (err)
			return err;

		if (!vlan.valid)
			break;

		set_bit(vlan.fid, fid_bitmap);
	} while (vlan.vid < GLOBAL_VTU_VID_MASK);

	/* The reset value 0x000 is used to indicate that multiple address
	 * databases are not needed. Return the next positive available.
	 */
	*fid = find_next_zero_bit(fid_bitmap, MV88E6XXX_N_FID, 1);
	if (unlikely(*fid >= mv88e6xxx_num_databases(chip)))
		return -ENOSPC;

	/* Clear the database */
	return _mv88e6xxx_atu_flush(chip, *fid, true);
}

static int _mv88e6xxx_vtu_new(struct mv88e6xxx_chip *chip, u16 vid,
			      struct mv88e6xxx_vtu_stu_entry *entry)
{
	struct dsa_switch *ds = chip->ds;
	struct mv88e6xxx_vtu_stu_entry vlan = {
		.valid = true,
		.vid = vid,
	};
	int i, err;

	err = _mv88e6xxx_fid_new(chip, &vlan.fid);
	if (err)
		return err;

	/* exclude all ports except the CPU and DSA ports */
	for (i = 0; i < chip->info->num_ports; ++i)
		vlan.data[i] = dsa_is_cpu_port(ds, i) || dsa_is_dsa_port(ds, i)
			? GLOBAL_VTU_DATA_MEMBER_TAG_UNMODIFIED
			: GLOBAL_VTU_DATA_MEMBER_TAG_NON_MEMBER;

	if (mv88e6xxx_6097_family(chip) || mv88e6xxx_6165_family(chip) ||
	    mv88e6xxx_6351_family(chip) || mv88e6xxx_6352_family(chip)) {
		struct mv88e6xxx_vtu_stu_entry vstp;

		/* Adding a VTU entry requires a valid STU entry. As VSTP is not
		 * implemented, only one STU entry is needed to cover all VTU
		 * entries. Thus, validate the SID 0.
		 */
		vlan.sid = 0;
		err = _mv88e6xxx_stu_getnext(chip, GLOBAL_VTU_SID_MASK, &vstp);
		if (err)
			return err;

		if (vstp.sid != vlan.sid || !vstp.valid) {
			memset(&vstp, 0, sizeof(vstp));
			vstp.valid = true;
			vstp.sid = vlan.sid;

			err = _mv88e6xxx_stu_loadpurge(chip, &vstp);
			if (err)
				return err;
		}
	}

	*entry = vlan;
	return 0;
}

static int _mv88e6xxx_vtu_get(struct mv88e6xxx_chip *chip, u16 vid,
			      struct mv88e6xxx_vtu_stu_entry *entry, bool creat)
{
	int err;

	if (!vid)
		return -EINVAL;

	err = _mv88e6xxx_vtu_vid_write(chip, vid - 1);
	if (err)
		return err;

	err = _mv88e6xxx_vtu_getnext(chip, entry);
	if (err)
		return err;

	if (entry->vid != vid || !entry->valid) {
		if (!creat)
			return -EOPNOTSUPP;
		/* -ENOENT would've been more appropriate, but switchdev expects
		 * -EOPNOTSUPP to inform bridge about an eventual software VLAN.
		 */

		err = _mv88e6xxx_vtu_new(chip, vid, entry);
	}

	return err;
}

static int mv88e6xxx_port_check_hw_vlan(struct dsa_switch *ds, int port,
					u16 vid_begin, u16 vid_end)
{
	struct mv88e6xxx_chip *chip = ds_to_priv(ds);
	struct mv88e6xxx_vtu_stu_entry vlan;
	int i, err;

	if (!vid_begin)
		return -EOPNOTSUPP;

	mutex_lock(&chip->reg_lock);

	err = _mv88e6xxx_vtu_vid_write(chip, vid_begin - 1);
	if (err)
		goto unlock;

	do {
		err = _mv88e6xxx_vtu_getnext(chip, &vlan);
		if (err)
			goto unlock;

		if (!vlan.valid)
			break;

		if (vlan.vid > vid_end)
			break;

		for (i = 0; i < chip->info->num_ports; ++i) {
			if (dsa_is_dsa_port(ds, i) || dsa_is_cpu_port(ds, i))
				continue;

			if (vlan.data[i] ==
			    GLOBAL_VTU_DATA_MEMBER_TAG_NON_MEMBER)
				continue;

			if (chip->ports[i].bridge_dev ==
			    chip->ports[port].bridge_dev)
				break; /* same bridge, check next VLAN */

			netdev_warn(ds->ports[port].netdev,
				    "hardware VLAN %d already used by %s\n",
				    vlan.vid,
				    netdev_name(chip->ports[i].bridge_dev));
			err = -EOPNOTSUPP;
			goto unlock;
		}
	} while (vlan.vid < vid_end);

unlock:
	mutex_unlock(&chip->reg_lock);

	return err;
}

static const char * const mv88e6xxx_port_8021q_mode_names[] = {
	[PORT_CONTROL_2_8021Q_DISABLED] = "Disabled",
	[PORT_CONTROL_2_8021Q_FALLBACK] = "Fallback",
	[PORT_CONTROL_2_8021Q_CHECK] = "Check",
	[PORT_CONTROL_2_8021Q_SECURE] = "Secure",
};

static int mv88e6xxx_port_vlan_filtering(struct dsa_switch *ds, int port,
					 bool vlan_filtering)
{
	struct mv88e6xxx_chip *chip = ds_to_priv(ds);
	u16 old, new = vlan_filtering ? PORT_CONTROL_2_8021Q_SECURE :
		PORT_CONTROL_2_8021Q_DISABLED;
	int ret;

	if (!mv88e6xxx_has(chip, MV88E6XXX_FLAG_VTU))
		return -EOPNOTSUPP;

	mutex_lock(&chip->reg_lock);

	ret = _mv88e6xxx_reg_read(chip, REG_PORT(port), PORT_CONTROL_2);
	if (ret < 0)
		goto unlock;

	old = ret & PORT_CONTROL_2_8021Q_MASK;

	if (new != old) {
		ret &= ~PORT_CONTROL_2_8021Q_MASK;
		ret |= new & PORT_CONTROL_2_8021Q_MASK;

		ret = _mv88e6xxx_reg_write(chip, REG_PORT(port), PORT_CONTROL_2,
					   ret);
		if (ret < 0)
			goto unlock;

		netdev_dbg(ds->ports[port].netdev, "802.1Q Mode %s (was %s)\n",
			   mv88e6xxx_port_8021q_mode_names[new],
			   mv88e6xxx_port_8021q_mode_names[old]);
	}

	ret = 0;
unlock:
	mutex_unlock(&chip->reg_lock);

	return ret;
}

static int
mv88e6xxx_port_vlan_prepare(struct dsa_switch *ds, int port,
			    const struct switchdev_obj_port_vlan *vlan,
			    struct switchdev_trans *trans)
{
	struct mv88e6xxx_chip *chip = ds_to_priv(ds);
	int err;

	if (!mv88e6xxx_has(chip, MV88E6XXX_FLAG_VTU))
		return -EOPNOTSUPP;

	/* If the requested port doesn't belong to the same bridge as the VLAN
	 * members, do not support it (yet) and fallback to software VLAN.
	 */
	err = mv88e6xxx_port_check_hw_vlan(ds, port, vlan->vid_begin,
					   vlan->vid_end);
	if (err)
		return err;

	/* We don't need any dynamic resource from the kernel (yet),
	 * so skip the prepare phase.
	 */
	return 0;
}

static int _mv88e6xxx_port_vlan_add(struct mv88e6xxx_chip *chip, int port,
				    u16 vid, bool untagged)
{
	struct mv88e6xxx_vtu_stu_entry vlan;
	int err;

	err = _mv88e6xxx_vtu_get(chip, vid, &vlan, true);
	if (err)
		return err;

	vlan.data[port] = untagged ?
		GLOBAL_VTU_DATA_MEMBER_TAG_UNTAGGED :
		GLOBAL_VTU_DATA_MEMBER_TAG_TAGGED;

	return _mv88e6xxx_vtu_loadpurge(chip, &vlan);
}

static void mv88e6xxx_port_vlan_add(struct dsa_switch *ds, int port,
				    const struct switchdev_obj_port_vlan *vlan,
				    struct switchdev_trans *trans)
{
	struct mv88e6xxx_chip *chip = ds_to_priv(ds);
	bool untagged = vlan->flags & BRIDGE_VLAN_INFO_UNTAGGED;
	bool pvid = vlan->flags & BRIDGE_VLAN_INFO_PVID;
	u16 vid;

	if (!mv88e6xxx_has(chip, MV88E6XXX_FLAG_VTU))
		return;

	mutex_lock(&chip->reg_lock);

	for (vid = vlan->vid_begin; vid <= vlan->vid_end; ++vid)
		if (_mv88e6xxx_port_vlan_add(chip, port, vid, untagged))
			netdev_err(ds->ports[port].netdev,
				   "failed to add VLAN %d%c\n",
				   vid, untagged ? 'u' : 't');

	if (pvid && _mv88e6xxx_port_pvid_set(chip, port, vlan->vid_end))
		netdev_err(ds->ports[port].netdev, "failed to set PVID %d\n",
			   vlan->vid_end);

	mutex_unlock(&chip->reg_lock);
}

static int _mv88e6xxx_port_vlan_del(struct mv88e6xxx_chip *chip,
				    int port, u16 vid)
{
	struct dsa_switch *ds = chip->ds;
	struct mv88e6xxx_vtu_stu_entry vlan;
	int i, err;

	err = _mv88e6xxx_vtu_get(chip, vid, &vlan, false);
	if (err)
		return err;

	/* Tell switchdev if this VLAN is handled in software */
	if (vlan.data[port] == GLOBAL_VTU_DATA_MEMBER_TAG_NON_MEMBER)
		return -EOPNOTSUPP;

	vlan.data[port] = GLOBAL_VTU_DATA_MEMBER_TAG_NON_MEMBER;

	/* keep the VLAN unless all ports are excluded */
	vlan.valid = false;
	for (i = 0; i < chip->info->num_ports; ++i) {
		if (dsa_is_cpu_port(ds, i) || dsa_is_dsa_port(ds, i))
			continue;

		if (vlan.data[i] != GLOBAL_VTU_DATA_MEMBER_TAG_NON_MEMBER) {
			vlan.valid = true;
			break;
		}
	}

	err = _mv88e6xxx_vtu_loadpurge(chip, &vlan);
	if (err)
		return err;

	return _mv88e6xxx_atu_remove(chip, vlan.fid, port, false);
}

static int mv88e6xxx_port_vlan_del(struct dsa_switch *ds, int port,
				   const struct switchdev_obj_port_vlan *vlan)
{
	struct mv88e6xxx_chip *chip = ds_to_priv(ds);
	u16 pvid, vid;
	int err = 0;

	if (!mv88e6xxx_has(chip, MV88E6XXX_FLAG_VTU))
		return -EOPNOTSUPP;

	mutex_lock(&chip->reg_lock);

	err = _mv88e6xxx_port_pvid_get(chip, port, &pvid);
	if (err)
		goto unlock;

	for (vid = vlan->vid_begin; vid <= vlan->vid_end; ++vid) {
		err = _mv88e6xxx_port_vlan_del(chip, port, vid);
		if (err)
			goto unlock;

		if (vid == pvid) {
			err = _mv88e6xxx_port_pvid_set(chip, port, 0);
			if (err)
				goto unlock;
		}
	}

unlock:
	mutex_unlock(&chip->reg_lock);

	return err;
}

static int _mv88e6xxx_atu_mac_write(struct mv88e6xxx_chip *chip,
				    const unsigned char *addr)
{
	int i, ret;

	for (i = 0; i < 3; i++) {
		ret = _mv88e6xxx_reg_write(
			chip, REG_GLOBAL, GLOBAL_ATU_MAC_01 + i,
			(addr[i * 2] << 8) | addr[i * 2 + 1]);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static int _mv88e6xxx_atu_mac_read(struct mv88e6xxx_chip *chip,
				   unsigned char *addr)
{
	int i, ret;

	for (i = 0; i < 3; i++) {
		ret = _mv88e6xxx_reg_read(chip, REG_GLOBAL,
					  GLOBAL_ATU_MAC_01 + i);
		if (ret < 0)
			return ret;
		addr[i * 2] = ret >> 8;
		addr[i * 2 + 1] = ret & 0xff;
	}

	return 0;
}

static int _mv88e6xxx_atu_load(struct mv88e6xxx_chip *chip,
			       struct mv88e6xxx_atu_entry *entry)
{
	int ret;

	ret = _mv88e6xxx_atu_wait(chip);
	if (ret < 0)
		return ret;

	ret = _mv88e6xxx_atu_mac_write(chip, entry->mac);
	if (ret < 0)
		return ret;

	ret = _mv88e6xxx_atu_data_write(chip, entry);
	if (ret < 0)
		return ret;

	return _mv88e6xxx_atu_cmd(chip, entry->fid, GLOBAL_ATU_OP_LOAD_DB);
}

static int _mv88e6xxx_port_fdb_load(struct mv88e6xxx_chip *chip, int port,
				    const unsigned char *addr, u16 vid,
				    u8 state)
{
	struct mv88e6xxx_atu_entry entry = { 0 };
	struct mv88e6xxx_vtu_stu_entry vlan;
	int err;

	/* Null VLAN ID corresponds to the port private database */
	if (vid == 0)
		err = _mv88e6xxx_port_fid_get(chip, port, &vlan.fid);
	else
		err = _mv88e6xxx_vtu_get(chip, vid, &vlan, false);
	if (err)
		return err;

	entry.fid = vlan.fid;
	entry.state = state;
	ether_addr_copy(entry.mac, addr);
	if (state != GLOBAL_ATU_DATA_STATE_UNUSED) {
		entry.trunk = false;
		entry.portv_trunkid = BIT(port);
	}

	return _mv88e6xxx_atu_load(chip, &entry);
}

static int mv88e6xxx_port_fdb_prepare(struct dsa_switch *ds, int port,
				      const struct switchdev_obj_port_fdb *fdb,
				      struct switchdev_trans *trans)
{
	/* We don't need any dynamic resource from the kernel (yet),
	 * so skip the prepare phase.
	 */
	return 0;
}

static void mv88e6xxx_port_fdb_add(struct dsa_switch *ds, int port,
				   const struct switchdev_obj_port_fdb *fdb,
				   struct switchdev_trans *trans)
{
	int state = is_multicast_ether_addr(fdb->addr) ?
		GLOBAL_ATU_DATA_STATE_MC_STATIC :
		GLOBAL_ATU_DATA_STATE_UC_STATIC;
	struct mv88e6xxx_chip *chip = ds_to_priv(ds);

	mutex_lock(&chip->reg_lock);
	if (_mv88e6xxx_port_fdb_load(chip, port, fdb->addr, fdb->vid, state))
		netdev_err(ds->ports[port].netdev,
			   "failed to load MAC address\n");
	mutex_unlock(&chip->reg_lock);
}

static int mv88e6xxx_port_fdb_del(struct dsa_switch *ds, int port,
				  const struct switchdev_obj_port_fdb *fdb)
{
	struct mv88e6xxx_chip *chip = ds_to_priv(ds);
	int ret;

	mutex_lock(&chip->reg_lock);
	ret = _mv88e6xxx_port_fdb_load(chip, port, fdb->addr, fdb->vid,
				       GLOBAL_ATU_DATA_STATE_UNUSED);
	mutex_unlock(&chip->reg_lock);

	return ret;
}

static int _mv88e6xxx_atu_getnext(struct mv88e6xxx_chip *chip, u16 fid,
				  struct mv88e6xxx_atu_entry *entry)
{
	struct mv88e6xxx_atu_entry next = { 0 };
	int ret;

	next.fid = fid;

	ret = _mv88e6xxx_atu_wait(chip);
	if (ret < 0)
		return ret;

	ret = _mv88e6xxx_atu_cmd(chip, fid, GLOBAL_ATU_OP_GET_NEXT_DB);
	if (ret < 0)
		return ret;

	ret = _mv88e6xxx_atu_mac_read(chip, next.mac);
	if (ret < 0)
		return ret;

	ret = _mv88e6xxx_reg_read(chip, REG_GLOBAL, GLOBAL_ATU_DATA);
	if (ret < 0)
		return ret;

	next.state = ret & GLOBAL_ATU_DATA_STATE_MASK;
	if (next.state != GLOBAL_ATU_DATA_STATE_UNUSED) {
		unsigned int mask, shift;

		if (ret & GLOBAL_ATU_DATA_TRUNK) {
			next.trunk = true;
			mask = GLOBAL_ATU_DATA_TRUNK_ID_MASK;
			shift = GLOBAL_ATU_DATA_TRUNK_ID_SHIFT;
		} else {
			next.trunk = false;
			mask = GLOBAL_ATU_DATA_PORT_VECTOR_MASK;
			shift = GLOBAL_ATU_DATA_PORT_VECTOR_SHIFT;
		}

		next.portv_trunkid = (ret & mask) >> shift;
	}

	*entry = next;
	return 0;
}

static int _mv88e6xxx_port_fdb_dump_one(struct mv88e6xxx_chip *chip,
					u16 fid, u16 vid, int port,
					struct switchdev_obj_port_fdb *fdb,
					int (*cb)(struct switchdev_obj *obj))
{
	struct mv88e6xxx_atu_entry addr = {
		.mac = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff },
	};
	int err;

	err = _mv88e6xxx_atu_mac_write(chip, addr.mac);
	if (err)
		return err;

	do {
		err = _mv88e6xxx_atu_getnext(chip, fid, &addr);
		if (err)
			break;

		if (addr.state == GLOBAL_ATU_DATA_STATE_UNUSED)
			break;

		if (!addr.trunk && addr.portv_trunkid & BIT(port)) {
			bool is_static = addr.state ==
				(is_multicast_ether_addr(addr.mac) ?
				 GLOBAL_ATU_DATA_STATE_MC_STATIC :
				 GLOBAL_ATU_DATA_STATE_UC_STATIC);

			fdb->vid = vid;
			ether_addr_copy(fdb->addr, addr.mac);
			fdb->ndm_state = is_static ? NUD_NOARP : NUD_REACHABLE;

			err = cb(&fdb->obj);
			if (err)
				break;
		}
	} while (!is_broadcast_ether_addr(addr.mac));

	return err;
}

static int mv88e6xxx_port_fdb_dump(struct dsa_switch *ds, int port,
				   struct switchdev_obj_port_fdb *fdb,
				   int (*cb)(struct switchdev_obj *obj))
{
	struct mv88e6xxx_chip *chip = ds_to_priv(ds);
	struct mv88e6xxx_vtu_stu_entry vlan = {
		.vid = GLOBAL_VTU_VID_MASK, /* all ones */
	};
	u16 fid;
	int err;

	mutex_lock(&chip->reg_lock);

	/* Dump port's default Filtering Information Database (VLAN ID 0) */
	err = _mv88e6xxx_port_fid_get(chip, port, &fid);
	if (err)
		goto unlock;

	err = _mv88e6xxx_port_fdb_dump_one(chip, fid, 0, port, fdb, cb);
	if (err)
		goto unlock;

	/* Dump VLANs' Filtering Information Databases */
	err = _mv88e6xxx_vtu_vid_write(chip, vlan.vid);
	if (err)
		goto unlock;

	do {
		err = _mv88e6xxx_vtu_getnext(chip, &vlan);
		if (err)
			break;

		if (!vlan.valid)
			break;

		err = _mv88e6xxx_port_fdb_dump_one(chip, vlan.fid, vlan.vid,
						   port, fdb, cb);
		if (err)
			break;
	} while (vlan.vid < GLOBAL_VTU_VID_MASK);

unlock:
	mutex_unlock(&chip->reg_lock);

	return err;
}

static int mv88e6xxx_port_bridge_join(struct dsa_switch *ds, int port,
				      struct net_device *bridge)
{
	struct mv88e6xxx_chip *chip = ds_to_priv(ds);
	int i, err = 0;

	mutex_lock(&chip->reg_lock);

	/* Assign the bridge and remap each port's VLANTable */
	chip->ports[port].bridge_dev = bridge;

	for (i = 0; i < chip->info->num_ports; ++i) {
		if (chip->ports[i].bridge_dev == bridge) {
			err = _mv88e6xxx_port_based_vlan_map(chip, i);
			if (err)
				break;
		}
	}

	mutex_unlock(&chip->reg_lock);

	return err;
}

static void mv88e6xxx_port_bridge_leave(struct dsa_switch *ds, int port)
{
	struct mv88e6xxx_chip *chip = ds_to_priv(ds);
	struct net_device *bridge = chip->ports[port].bridge_dev;
	int i;

	mutex_lock(&chip->reg_lock);

	/* Unassign the bridge and remap each port's VLANTable */
	chip->ports[port].bridge_dev = NULL;

	for (i = 0; i < chip->info->num_ports; ++i)
		if (i == port || chip->ports[i].bridge_dev == bridge)
			if (_mv88e6xxx_port_based_vlan_map(chip, i))
				netdev_warn(ds->ports[i].netdev,
					    "failed to remap\n");

	mutex_unlock(&chip->reg_lock);
}

static int _mv88e6xxx_mdio_page_write(struct mv88e6xxx_chip *chip,
				      int port, int page, int reg, int val)
{
	int ret;

	ret = mv88e6xxx_mdio_write_indirect(chip, port, 0x16, page);
	if (ret < 0)
		goto restore_page_0;

	ret = mv88e6xxx_mdio_write_indirect(chip, port, reg, val);
restore_page_0:
	mv88e6xxx_mdio_write_indirect(chip, port, 0x16, 0x0);

	return ret;
}

static int _mv88e6xxx_mdio_page_read(struct mv88e6xxx_chip *chip,
				     int port, int page, int reg)
{
	int ret;

	ret = mv88e6xxx_mdio_write_indirect(chip, port, 0x16, page);
	if (ret < 0)
		goto restore_page_0;

	ret = mv88e6xxx_mdio_read_indirect(chip, port, reg);
restore_page_0:
	mv88e6xxx_mdio_write_indirect(chip, port, 0x16, 0x0);

	return ret;
}

static int mv88e6xxx_switch_reset(struct mv88e6xxx_chip *chip)
{
	bool ppu_active = mv88e6xxx_has(chip, MV88E6XXX_FLAG_PPU_ACTIVE);
	u16 is_reset = (ppu_active ? 0x8800 : 0xc800);
	struct gpio_desc *gpiod = chip->reset;
	unsigned long timeout;
	int ret;
	int i;

	/* Set all ports to the disabled state. */
	for (i = 0; i < chip->info->num_ports; i++) {
		ret = _mv88e6xxx_reg_read(chip, REG_PORT(i), PORT_CONTROL);
		if (ret < 0)
			return ret;

		ret = _mv88e6xxx_reg_write(chip, REG_PORT(i), PORT_CONTROL,
					   ret & 0xfffc);
		if (ret)
			return ret;
	}

	/* Wait for transmit queues to drain. */
	usleep_range(2000, 4000);

	/* If there is a gpio connected to the reset pin, toggle it */
	if (gpiod) {
		gpiod_set_value_cansleep(gpiod, 1);
		usleep_range(10000, 20000);
		gpiod_set_value_cansleep(gpiod, 0);
		usleep_range(10000, 20000);
	}

	/* Reset the switch. Keep the PPU active if requested. The PPU
	 * needs to be active to support indirect phy register access
	 * through global registers 0x18 and 0x19.
	 */
	if (ppu_active)
		ret = _mv88e6xxx_reg_write(chip, REG_GLOBAL, 0x04, 0xc000);
	else
		ret = _mv88e6xxx_reg_write(chip, REG_GLOBAL, 0x04, 0xc400);
	if (ret)
		return ret;

	/* Wait up to one second for reset to complete. */
	timeout = jiffies + 1 * HZ;
	while (time_before(jiffies, timeout)) {
		ret = _mv88e6xxx_reg_read(chip, REG_GLOBAL, 0x00);
		if (ret < 0)
			return ret;

		if ((ret & is_reset) == is_reset)
			break;
		usleep_range(1000, 2000);
	}
	if (time_after(jiffies, timeout))
		ret = -ETIMEDOUT;
	else
		ret = 0;

	return ret;
}

static int mv88e6xxx_power_on_serdes(struct mv88e6xxx_chip *chip)
{
	int ret;

	ret = _mv88e6xxx_mdio_page_read(chip, REG_FIBER_SERDES,
					PAGE_FIBER_SERDES, MII_BMCR);
	if (ret < 0)
		return ret;

	if (ret & BMCR_PDOWN) {
		ret &= ~BMCR_PDOWN;
		ret = _mv88e6xxx_mdio_page_write(chip, REG_FIBER_SERDES,
						 PAGE_FIBER_SERDES, MII_BMCR,
						 ret);
	}

	return ret;
}

static int mv88e6xxx_setup_port(struct mv88e6xxx_chip *chip, int port)
{
	struct dsa_switch *ds = chip->ds;
	int ret;
	u16 reg;

	if (mv88e6xxx_6352_family(chip) || mv88e6xxx_6351_family(chip) ||
	    mv88e6xxx_6165_family(chip) || mv88e6xxx_6097_family(chip) ||
	    mv88e6xxx_6185_family(chip) || mv88e6xxx_6095_family(chip) ||
	    mv88e6xxx_6065_family(chip) || mv88e6xxx_6320_family(chip)) {
		/* MAC Forcing register: don't force link, speed,
		 * duplex or flow control state to any particular
		 * values on physical ports, but force the CPU port
		 * and all DSA ports to their maximum bandwidth and
		 * full duplex.
		 */
		reg = _mv88e6xxx_reg_read(chip, REG_PORT(port), PORT_PCS_CTRL);
		if (dsa_is_cpu_port(ds, port) || dsa_is_dsa_port(ds, port)) {
			reg &= ~PORT_PCS_CTRL_UNFORCED;
			reg |= PORT_PCS_CTRL_FORCE_LINK |
				PORT_PCS_CTRL_LINK_UP |
				PORT_PCS_CTRL_DUPLEX_FULL |
				PORT_PCS_CTRL_FORCE_DUPLEX;
			if (mv88e6xxx_6065_family(chip))
				reg |= PORT_PCS_CTRL_100;
			else
				reg |= PORT_PCS_CTRL_1000;
		} else {
			reg |= PORT_PCS_CTRL_UNFORCED;
		}

		ret = _mv88e6xxx_reg_write(chip, REG_PORT(port),
					   PORT_PCS_CTRL, reg);
		if (ret)
			return ret;
	}

	/* Port Control: disable Drop-on-Unlock, disable Drop-on-Lock,
	 * disable Header mode, enable IGMP/MLD snooping, disable VLAN
	 * tunneling, determine priority by looking at 802.1p and IP
	 * priority fields (IP prio has precedence), and set STP state
	 * to Forwarding.
	 *
	 * If this is the CPU link, use DSA or EDSA tagging depending
	 * on which tagging mode was configured.
	 *
	 * If this is a link to another switch, use DSA tagging mode.
	 *
	 * If this is the upstream port for this switch, enable
	 * forwarding of unknown unicasts and multicasts.
	 */
	reg = 0;
	if (mv88e6xxx_6352_family(chip) || mv88e6xxx_6351_family(chip) ||
	    mv88e6xxx_6165_family(chip) || mv88e6xxx_6097_family(chip) ||
	    mv88e6xxx_6095_family(chip) || mv88e6xxx_6065_family(chip) ||
	    mv88e6xxx_6185_family(chip) || mv88e6xxx_6320_family(chip))
		reg = PORT_CONTROL_IGMP_MLD_SNOOP |
		PORT_CONTROL_USE_TAG | PORT_CONTROL_USE_IP |
		PORT_CONTROL_STATE_FORWARDING;
	if (dsa_is_cpu_port(ds, port)) {
		if (mv88e6xxx_6095_family(chip) || mv88e6xxx_6185_family(chip))
			reg |= PORT_CONTROL_DSA_TAG;
		if (mv88e6xxx_6352_family(chip) ||
		    mv88e6xxx_6351_family(chip) ||
		    mv88e6xxx_6165_family(chip) ||
		    mv88e6xxx_6097_family(chip) ||
		    mv88e6xxx_6320_family(chip)) {
			reg |= PORT_CONTROL_FRAME_ETHER_TYPE_DSA |
				PORT_CONTROL_FORWARD_UNKNOWN |
				PORT_CONTROL_FORWARD_UNKNOWN_MC;
		}

		if (mv88e6xxx_6352_family(chip) ||
		    mv88e6xxx_6351_family(chip) ||
		    mv88e6xxx_6165_family(chip) ||
		    mv88e6xxx_6097_family(chip) ||
		    mv88e6xxx_6095_family(chip) ||
		    mv88e6xxx_6065_family(chip) ||
		    mv88e6xxx_6185_family(chip) ||
		    mv88e6xxx_6320_family(chip)) {
			reg |= PORT_CONTROL_EGRESS_ADD_TAG;
		}
	}
	if (dsa_is_dsa_port(ds, port)) {
		if (mv88e6xxx_6095_family(chip) ||
		    mv88e6xxx_6185_family(chip))
			reg |= PORT_CONTROL_DSA_TAG;
		if (mv88e6xxx_6352_family(chip) ||
		    mv88e6xxx_6351_family(chip) ||
		    mv88e6xxx_6165_family(chip) ||
		    mv88e6xxx_6097_family(chip) ||
		    mv88e6xxx_6320_family(chip)) {
			reg |= PORT_CONTROL_FRAME_MODE_DSA;
		}

		if (port == dsa_upstream_port(ds))
			reg |= PORT_CONTROL_FORWARD_UNKNOWN |
				PORT_CONTROL_FORWARD_UNKNOWN_MC;
	}
	if (reg) {
		ret = _mv88e6xxx_reg_write(chip, REG_PORT(port),
					   PORT_CONTROL, reg);
		if (ret)
			return ret;
	}

	/* If this port is connected to a SerDes, make sure the SerDes is not
	 * powered down.
	 */
	if (mv88e6xxx_6352_family(chip)) {
		ret = _mv88e6xxx_reg_read(chip, REG_PORT(port), PORT_STATUS);
		if (ret < 0)
			return ret;
		ret &= PORT_STATUS_CMODE_MASK;
		if ((ret == PORT_STATUS_CMODE_100BASE_X) ||
		    (ret == PORT_STATUS_CMODE_1000BASE_X) ||
		    (ret == PORT_STATUS_CMODE_SGMII)) {
			ret = mv88e6xxx_power_on_serdes(chip);
			if (ret < 0)
				return ret;
		}
	}

	/* Port Control 2: don't force a good FCS, set the maximum frame size to
	 * 10240 bytes, disable 802.1q tags checking, don't discard tagged or
	 * untagged frames on this port, do a destination address lookup on all
	 * received packets as usual, disable ARP mirroring and don't send a
	 * copy of all transmitted/received frames on this port to the CPU.
	 */
	reg = 0;
	if (mv88e6xxx_6352_family(chip) || mv88e6xxx_6351_family(chip) ||
	    mv88e6xxx_6165_family(chip) || mv88e6xxx_6097_family(chip) ||
	    mv88e6xxx_6095_family(chip) || mv88e6xxx_6320_family(chip) ||
	    mv88e6xxx_6185_family(chip))
		reg = PORT_CONTROL_2_MAP_DA;

	if (mv88e6xxx_6352_family(chip) || mv88e6xxx_6351_family(chip) ||
	    mv88e6xxx_6165_family(chip) || mv88e6xxx_6320_family(chip))
		reg |= PORT_CONTROL_2_JUMBO_10240;

	if (mv88e6xxx_6095_family(chip) || mv88e6xxx_6185_family(chip)) {
		/* Set the upstream port this port should use */
		reg |= dsa_upstream_port(ds);
		/* enable forwarding of unknown multicast addresses to
		 * the upstream port
		 */
		if (port == dsa_upstream_port(ds))
			reg |= PORT_CONTROL_2_FORWARD_UNKNOWN;
	}

	reg |= PORT_CONTROL_2_8021Q_DISABLED;

	if (reg) {
		ret = _mv88e6xxx_reg_write(chip, REG_PORT(port),
					   PORT_CONTROL_2, reg);
		if (ret)
			return ret;
	}

	/* Port Association Vector: when learning source addresses
	 * of packets, add the address to the address database using
	 * a port bitmap that has only the bit for this port set and
	 * the other bits clear.
	 */
	reg = 1 << port;
	/* Disable learning for CPU port */
	if (dsa_is_cpu_port(ds, port))
		reg = 0;

	ret = _mv88e6xxx_reg_write(chip, REG_PORT(port), PORT_ASSOC_VECTOR,
				   reg);
	if (ret)
		return ret;

	/* Egress rate control 2: disable egress rate control. */
	ret = _mv88e6xxx_reg_write(chip, REG_PORT(port), PORT_RATE_CONTROL_2,
				   0x0000);
	if (ret)
		return ret;

	if (mv88e6xxx_6352_family(chip) || mv88e6xxx_6351_family(chip) ||
	    mv88e6xxx_6165_family(chip) || mv88e6xxx_6097_family(chip) ||
	    mv88e6xxx_6320_family(chip)) {
		/* Do not limit the period of time that this port can
		 * be paused for by the remote end or the period of
		 * time that this port can pause the remote end.
		 */
		ret = _mv88e6xxx_reg_write(chip, REG_PORT(port),
					   PORT_PAUSE_CTRL, 0x0000);
		if (ret)
			return ret;

		/* Port ATU control: disable limiting the number of
		 * address database entries that this port is allowed
		 * to use.
		 */
		ret = _mv88e6xxx_reg_write(chip, REG_PORT(port),
					   PORT_ATU_CONTROL, 0x0000);
		/* Priority Override: disable DA, SA and VTU priority
		 * override.
		 */
		ret = _mv88e6xxx_reg_write(chip, REG_PORT(port),
					   PORT_PRI_OVERRIDE, 0x0000);
		if (ret)
			return ret;

		/* Port Ethertype: use the Ethertype DSA Ethertype
		 * value.
		 */
		ret = _mv88e6xxx_reg_write(chip, REG_PORT(port),
					   PORT_ETH_TYPE, ETH_P_EDSA);
		if (ret)
			return ret;
		/* Tag Remap: use an identity 802.1p prio -> switch
		 * prio mapping.
		 */
		ret = _mv88e6xxx_reg_write(chip, REG_PORT(port),
					   PORT_TAG_REGMAP_0123, 0x3210);
		if (ret)
			return ret;

		/* Tag Remap 2: use an identity 802.1p prio -> switch
		 * prio mapping.
		 */
		ret = _mv88e6xxx_reg_write(chip, REG_PORT(port),
					   PORT_TAG_REGMAP_4567, 0x7654);
		if (ret)
			return ret;
	}

	if (mv88e6xxx_6352_family(chip) || mv88e6xxx_6351_family(chip) ||
	    mv88e6xxx_6165_family(chip) || mv88e6xxx_6097_family(chip) ||
	    mv88e6xxx_6185_family(chip) || mv88e6xxx_6095_family(chip) ||
	    mv88e6xxx_6320_family(chip)) {
		/* Rate Control: disable ingress rate limiting. */
		ret = _mv88e6xxx_reg_write(chip, REG_PORT(port),
					   PORT_RATE_CONTROL, 0x0001);
		if (ret)
			return ret;
	}

	/* Port Control 1: disable trunking, disable sending
	 * learning messages to this port.
	 */
	ret = _mv88e6xxx_reg_write(chip, REG_PORT(port), PORT_CONTROL_1,
				   0x0000);
	if (ret)
		return ret;

	/* Port based VLAN map: give each port the same default address
	 * database, and allow bidirectional communication between the
	 * CPU and DSA port(s), and the other ports.
	 */
	ret = _mv88e6xxx_port_fid_set(chip, port, 0);
	if (ret)
		return ret;

	ret = _mv88e6xxx_port_based_vlan_map(chip, port);
	if (ret)
		return ret;

	/* Default VLAN ID and priority: don't set a default VLAN
	 * ID, and set the default packet priority to zero.
	 */
	ret = _mv88e6xxx_reg_write(chip, REG_PORT(port), PORT_DEFAULT_VLAN,
				   0x0000);
	if (ret)
		return ret;

	return 0;
}

static int mv88e6xxx_g1_set_switch_mac(struct mv88e6xxx_chip *chip, u8 *addr)
{
	int err;

	err = mv88e6xxx_write(chip, REG_GLOBAL, GLOBAL_MAC_01,
			      (addr[0] << 8) | addr[1]);
	if (err)
		return err;

	err = mv88e6xxx_write(chip, REG_GLOBAL, GLOBAL_MAC_23,
			      (addr[2] << 8) | addr[3]);
	if (err)
		return err;

	return mv88e6xxx_write(chip, REG_GLOBAL, GLOBAL_MAC_45,
			       (addr[4] << 8) | addr[5]);
}

static int mv88e6xxx_g1_setup(struct mv88e6xxx_chip *chip)
{
	struct dsa_switch *ds = chip->ds;
	u32 upstream_port = dsa_upstream_port(ds);
	u16 reg;
	int err;

	/* Enable the PHY Polling Unit if present, don't discard any packets,
	 * and mask all interrupt sources.
	 */
	reg = 0;
	if (mv88e6xxx_has(chip, MV88E6XXX_FLAG_PPU) ||
	    mv88e6xxx_has(chip, MV88E6XXX_FLAG_PPU_ACTIVE))
		reg |= GLOBAL_CONTROL_PPU_ENABLE;

	err = _mv88e6xxx_reg_write(chip, REG_GLOBAL, GLOBAL_CONTROL, reg);
	if (err)
		return err;

	/* Configure the upstream port, and configure it as the port to which
	 * ingress and egress and ARP monitor frames are to be sent.
	 */
	reg = upstream_port << GLOBAL_MONITOR_CONTROL_INGRESS_SHIFT |
		upstream_port << GLOBAL_MONITOR_CONTROL_EGRESS_SHIFT |
		upstream_port << GLOBAL_MONITOR_CONTROL_ARP_SHIFT;
	err = _mv88e6xxx_reg_write(chip, REG_GLOBAL, GLOBAL_MONITOR_CONTROL,
				   reg);
	if (err)
		return err;

	/* Disable remote management, and set the switch's DSA device number. */
	err = _mv88e6xxx_reg_write(chip, REG_GLOBAL, GLOBAL_CONTROL_2,
				   GLOBAL_CONTROL_2_MULTIPLE_CASCADE |
				   (ds->index & 0x1f));
	if (err)
		return err;

	/* Set the default address aging time to 5 minutes, and
	 * enable address learn messages to be sent to all message
	 * ports.
	 */
	err = _mv88e6xxx_reg_write(chip, REG_GLOBAL, GLOBAL_ATU_CONTROL,
				   0x0140 | GLOBAL_ATU_CONTROL_LEARN2ALL);
	if (err)
		return err;

	/* Clear all the VTU and STU entries */
	err = _mv88e6xxx_vtu_stu_flush(chip);
	if (err < 0)
		return err;

	/* Clear all ATU entries */
	err = _mv88e6xxx_atu_flush(chip, 0, true);
	if (err)
		return err;

	/* Configure the IP ToS mapping registers. */
	err = _mv88e6xxx_reg_write(chip, REG_GLOBAL, GLOBAL_IP_PRI_0, 0x0000);
	if (err)
		return err;
	err = _mv88e6xxx_reg_write(chip, REG_GLOBAL, GLOBAL_IP_PRI_1, 0x0000);
	if (err)
		return err;
	err = _mv88e6xxx_reg_write(chip, REG_GLOBAL, GLOBAL_IP_PRI_2, 0x5555);
	if (err)
		return err;
	err = _mv88e6xxx_reg_write(chip, REG_GLOBAL, GLOBAL_IP_PRI_3, 0x5555);
	if (err)
		return err;
	err = _mv88e6xxx_reg_write(chip, REG_GLOBAL, GLOBAL_IP_PRI_4, 0xaaaa);
	if (err)
		return err;
	err = _mv88e6xxx_reg_write(chip, REG_GLOBAL, GLOBAL_IP_PRI_5, 0xaaaa);
	if (err)
		return err;
	err = _mv88e6xxx_reg_write(chip, REG_GLOBAL, GLOBAL_IP_PRI_6, 0xffff);
	if (err)
		return err;
	err = _mv88e6xxx_reg_write(chip, REG_GLOBAL, GLOBAL_IP_PRI_7, 0xffff);
	if (err)
		return err;

	/* Configure the IEEE 802.1p priority mapping register. */
	err = _mv88e6xxx_reg_write(chip, REG_GLOBAL, GLOBAL_IEEE_PRI, 0xfa41);
	if (err)
		return err;

	/* Clear the statistics counters for all ports */
	err = _mv88e6xxx_reg_write(chip, REG_GLOBAL, GLOBAL_STATS_OP,
				   GLOBAL_STATS_OP_FLUSH_ALL);
	if (err)
		return err;

	/* Wait for the flush to complete. */
	err = _mv88e6xxx_stats_wait(chip);
	if (err)
		return err;

	return 0;
}

static int mv88e6xxx_g2_device_mapping_write(struct mv88e6xxx_chip *chip,
					     int target, int port)
{
	u16 val = (target << 8) | (port & 0xf);

	return mv88e6xxx_update(chip, REG_GLOBAL2, GLOBAL2_DEVICE_MAPPING, val);
}

static int mv88e6xxx_g2_set_device_mapping(struct mv88e6xxx_chip *chip)
{
	int target, port;
	int err;

	/* Initialize the routing port to the 32 possible target devices */
	for (target = 0; target < 32; ++target) {
		port = 0xf;

		if (target < DSA_MAX_SWITCHES) {
			port = chip->ds->rtable[target];
			if (port == DSA_RTABLE_NONE)
				port = 0xf;
		}

		err = mv88e6xxx_g2_device_mapping_write(chip, target, port);
		if (err)
			break;
	}

	return err;
}

static int mv88e6xxx_g2_trunk_mask_write(struct mv88e6xxx_chip *chip, int num,
					 bool hask, u16 mask)
{
	const u16 port_mask = BIT(chip->info->num_ports) - 1;
	u16 val = (num << 12) | (mask & port_mask);

	if (hask)
		val |= GLOBAL2_TRUNK_MASK_HASK;

	return mv88e6xxx_update(chip, REG_GLOBAL2, GLOBAL2_TRUNK_MASK, val);
}

static int mv88e6xxx_g2_trunk_mapping_write(struct mv88e6xxx_chip *chip, int id,
					    u16 map)
{
	const u16 port_mask = BIT(chip->info->num_ports) - 1;
	u16 val = (id << 11) | (map & port_mask);

	return mv88e6xxx_update(chip, REG_GLOBAL2, GLOBAL2_TRUNK_MAPPING, val);
}

static int mv88e6xxx_g2_clear_trunk(struct mv88e6xxx_chip *chip)
{
	const u16 port_mask = BIT(chip->info->num_ports) - 1;
	int i, err;

	/* Clear all eight possible Trunk Mask vectors */
	for (i = 0; i < 8; ++i) {
		err = mv88e6xxx_g2_trunk_mask_write(chip, i, false, port_mask);
		if (err)
			return err;
	}

	/* Clear all sixteen possible Trunk ID routing vectors */
	for (i = 0; i < 16; ++i) {
		err = mv88e6xxx_g2_trunk_mapping_write(chip, i, 0);
		if (err)
			return err;
	}

	return 0;
}

/* Indirect write to the Switch MAC/WoL/WoF register */
static int mv88e6xxx_g2_switch_mac_write(struct mv88e6xxx_chip *chip,
					 unsigned int pointer, u8 data)
{
	u16 val = (pointer << 8) | data;

	return mv88e6xxx_update(chip, REG_GLOBAL2, GLOBAL2_SWITCH_MAC, val);
}

static int mv88e6xxx_g2_set_switch_mac(struct mv88e6xxx_chip *chip, u8 *addr)
{
	int i, err;

	for (i = 0; i < 6; i++) {
		err = mv88e6xxx_g2_switch_mac_write(chip, i, addr[i]);
		if (err)
			break;
	}

	return err;
}

static int mv88e6xxx_g2_setup(struct mv88e6xxx_chip *chip)
{
	u16 reg;
	int err;
	int i;

	if (mv88e6xxx_has(chip, MV88E6XXX_FLAG_G2_MGMT_EN_2X)) {
		/* Consider the frames with reserved multicast destination
		 * addresses matching 01:80:c2:00:00:2x as MGMT.
		 */
		err = mv88e6xxx_write(chip, REG_GLOBAL2, GLOBAL2_MGMT_EN_2X,
				      0xffff);
		if (err)
			return err;
	}

	if (mv88e6xxx_has(chip, MV88E6XXX_FLAG_G2_MGMT_EN_0X)) {
		/* Consider the frames with reserved multicast destination
		 * addresses matching 01:80:c2:00:00:0x as MGMT.
		 */
		err = mv88e6xxx_write(chip, REG_GLOBAL2, GLOBAL2_MGMT_EN_0X,
				      0xffff);
		if (err)
			return err;
	}

	/* Ignore removed tag data on doubly tagged packets, disable
	 * flow control messages, force flow control priority to the
	 * highest, and send all special multicast frames to the CPU
	 * port at the highest priority.
	 */
	reg = GLOBAL2_SWITCH_MGMT_FORCE_FLOW_CTRL_PRI | (0x7 << 4);
	if (mv88e6xxx_has(chip, MV88E6XXX_FLAG_G2_MGMT_EN_0X) ||
	    mv88e6xxx_has(chip, MV88E6XXX_FLAG_G2_MGMT_EN_2X))
		reg |= GLOBAL2_SWITCH_MGMT_RSVD2CPU | 0x7;
	err = mv88e6xxx_write(chip, REG_GLOBAL2, GLOBAL2_SWITCH_MGMT, reg);
	if (err)
		return err;

	/* Program the DSA routing table. */
	err = mv88e6xxx_g2_set_device_mapping(chip);
	if (err)
		return err;

	/* Clear all trunk masks and mapping. */
	err = mv88e6xxx_g2_clear_trunk(chip);
	if (err)
		return err;

	if (mv88e6xxx_has(chip, MV88E6XXX_FLAGS_PVT)) {
		/* Initialize Cross-chip Port VLAN Table to reset defaults */
		err = mv88e6xxx_write(chip, REG_GLOBAL2, GLOBAL2_PVT_ADDR,
				      GLOBAL2_PVT_ADDR_OP_INIT_ONES);
		if (err)
			return err;
	}

	if (mv88e6xxx_6352_family(chip) || mv88e6xxx_6351_family(chip) ||
	    mv88e6xxx_6165_family(chip) || mv88e6xxx_6097_family(chip) ||
	    mv88e6xxx_6320_family(chip)) {
		/* Clear the priority override table. */
		for (i = 0; i < 16; i++) {
			err = _mv88e6xxx_reg_write(chip, REG_GLOBAL2,
						   GLOBAL2_PRIO_OVERRIDE,
						   0x8000 | (i << 8));
			if (err)
				return err;
		}
	}

	if (mv88e6xxx_6352_family(chip) || mv88e6xxx_6351_family(chip) ||
	    mv88e6xxx_6165_family(chip) || mv88e6xxx_6097_family(chip) ||
	    mv88e6xxx_6185_family(chip) || mv88e6xxx_6095_family(chip) ||
	    mv88e6xxx_6320_family(chip)) {
		/* Disable ingress rate limiting by resetting all
		 * ingress rate limit registers to their initial
		 * state.
		 */
		for (i = 0; i < chip->info->num_ports; i++) {
			err = _mv88e6xxx_reg_write(chip, REG_GLOBAL2,
						   GLOBAL2_INGRESS_OP,
						   0x9000 | (i << 8));
			if (err)
				return err;
		}
	}

	return 0;
}

static int mv88e6xxx_setup(struct dsa_switch *ds)
{
	struct mv88e6xxx_chip *chip = ds_to_priv(ds);
	int err;
	int i;

	chip->ds = ds;
	ds->slave_mii_bus = chip->mdio_bus;

	if (mv88e6xxx_has(chip, MV88E6XXX_FLAG_EEPROM))
		mutex_init(&chip->eeprom_mutex);

	mutex_lock(&chip->reg_lock);

	err = mv88e6xxx_switch_reset(chip);
	if (err)
		goto unlock;

	/* Setup Switch Port Registers */
	for (i = 0; i < chip->info->num_ports; i++) {
		err = mv88e6xxx_setup_port(chip, i);
		if (err)
			goto unlock;
	}

	/* Setup Switch Global 1 Registers */
	err = mv88e6xxx_g1_setup(chip);
	if (err)
		goto unlock;

	/* Setup Switch Global 2 Registers */
	if (mv88e6xxx_has(chip, MV88E6XXX_FLAG_GLOBAL2)) {
		err = mv88e6xxx_g2_setup(chip);
		if (err)
			goto unlock;
	}

unlock:
	mutex_unlock(&chip->reg_lock);

	return err;
}

static int mv88e6xxx_set_addr(struct dsa_switch *ds, u8 *addr)
{
	struct mv88e6xxx_chip *chip = ds_to_priv(ds);
	int err;

	mutex_lock(&chip->reg_lock);

	/* Has an indirect Switch MAC/WoL/WoF register in Global 2? */
	if (mv88e6xxx_has(chip, MV88E6XXX_FLAG_G2_SWITCH_MAC))
		err = mv88e6xxx_g2_set_switch_mac(chip, addr);
	else
		err = mv88e6xxx_g1_set_switch_mac(chip, addr);

	mutex_unlock(&chip->reg_lock);

	return err;
}

static int mv88e6xxx_mdio_page_read(struct dsa_switch *ds, int port, int page,
				    int reg)
{
	struct mv88e6xxx_chip *chip = ds_to_priv(ds);
	int ret;

	mutex_lock(&chip->reg_lock);
	ret = _mv88e6xxx_mdio_page_read(chip, port, page, reg);
	mutex_unlock(&chip->reg_lock);

	return ret;
}

static int mv88e6xxx_mdio_page_write(struct dsa_switch *ds, int port, int page,
				     int reg, int val)
{
	struct mv88e6xxx_chip *chip = ds_to_priv(ds);
	int ret;

	mutex_lock(&chip->reg_lock);
	ret = _mv88e6xxx_mdio_page_write(chip, port, page, reg, val);
	mutex_unlock(&chip->reg_lock);

	return ret;
}

static int mv88e6xxx_port_to_mdio_addr(struct mv88e6xxx_chip *chip, int port)
{
	if (port >= 0 && port < chip->info->num_ports)
		return port;
	return -EINVAL;
}

static int mv88e6xxx_mdio_read(struct mii_bus *bus, int port, int regnum)
{
	struct mv88e6xxx_chip *chip = bus->priv;
	int addr = mv88e6xxx_port_to_mdio_addr(chip, port);
	int ret;

	if (addr < 0)
		return 0xffff;

	mutex_lock(&chip->reg_lock);

	if (mv88e6xxx_has(chip, MV88E6XXX_FLAG_PPU))
		ret = mv88e6xxx_mdio_read_ppu(chip, addr, regnum);
	else if (mv88e6xxx_has(chip, MV88E6XXX_FLAG_SMI_PHY))
		ret = mv88e6xxx_mdio_read_indirect(chip, addr, regnum);
	else
		ret = mv88e6xxx_mdio_read_direct(chip, addr, regnum);

	mutex_unlock(&chip->reg_lock);
	return ret;
}

static int mv88e6xxx_mdio_write(struct mii_bus *bus, int port, int regnum,
				u16 val)
{
	struct mv88e6xxx_chip *chip = bus->priv;
	int addr = mv88e6xxx_port_to_mdio_addr(chip, port);
	int ret;

	if (addr < 0)
		return 0xffff;

	mutex_lock(&chip->reg_lock);

	if (mv88e6xxx_has(chip, MV88E6XXX_FLAG_PPU))
		ret = mv88e6xxx_mdio_write_ppu(chip, addr, regnum, val);
	else if (mv88e6xxx_has(chip, MV88E6XXX_FLAG_SMI_PHY))
		ret = mv88e6xxx_mdio_write_indirect(chip, addr, regnum, val);
	else
		ret = mv88e6xxx_mdio_write_direct(chip, addr, regnum, val);

	mutex_unlock(&chip->reg_lock);
	return ret;
}

static int mv88e6xxx_mdio_register(struct mv88e6xxx_chip *chip,
				   struct device_node *np)
{
	static int index;
	struct mii_bus *bus;
	int err;

	if (mv88e6xxx_has(chip, MV88E6XXX_FLAG_PPU))
		mv88e6xxx_ppu_state_init(chip);

	if (np)
		chip->mdio_np = of_get_child_by_name(np, "mdio");

	bus = devm_mdiobus_alloc(chip->dev);
	if (!bus)
		return -ENOMEM;

	bus->priv = (void *)chip;
	if (np) {
		bus->name = np->full_name;
		snprintf(bus->id, MII_BUS_ID_SIZE, "%s", np->full_name);
	} else {
		bus->name = "mv88e6xxx SMI";
		snprintf(bus->id, MII_BUS_ID_SIZE, "mv88e6xxx-%d", index++);
	}

	bus->read = mv88e6xxx_mdio_read;
	bus->write = mv88e6xxx_mdio_write;
	bus->parent = chip->dev;

	if (chip->mdio_np)
		err = of_mdiobus_register(bus, chip->mdio_np);
	else
		err = mdiobus_register(bus);
	if (err) {
		dev_err(chip->dev, "Cannot register MDIO bus (%d)\n", err);
		goto out;
	}
	chip->mdio_bus = bus;

	return 0;

out:
	if (chip->mdio_np)
		of_node_put(chip->mdio_np);

	return err;
}

static void mv88e6xxx_mdio_unregister(struct mv88e6xxx_chip *chip)

{
	struct mii_bus *bus = chip->mdio_bus;

	mdiobus_unregister(bus);

	if (chip->mdio_np)
		of_node_put(chip->mdio_np);
}

#ifdef CONFIG_NET_DSA_HWMON

static int mv88e61xx_get_temp(struct dsa_switch *ds, int *temp)
{
	struct mv88e6xxx_chip *chip = ds_to_priv(ds);
	int ret;
	int val;

	*temp = 0;

	mutex_lock(&chip->reg_lock);

	ret = mv88e6xxx_mdio_write_direct(chip, 0x0, 0x16, 0x6);
	if (ret < 0)
		goto error;

	/* Enable temperature sensor */
	ret = mv88e6xxx_mdio_read_direct(chip, 0x0, 0x1a);
	if (ret < 0)
		goto error;

	ret = mv88e6xxx_mdio_write_direct(chip, 0x0, 0x1a, ret | (1 << 5));
	if (ret < 0)
		goto error;

	/* Wait for temperature to stabilize */
	usleep_range(10000, 12000);

	val = mv88e6xxx_mdio_read_direct(chip, 0x0, 0x1a);
	if (val < 0) {
		ret = val;
		goto error;
	}

	/* Disable temperature sensor */
	ret = mv88e6xxx_mdio_write_direct(chip, 0x0, 0x1a, ret & ~(1 << 5));
	if (ret < 0)
		goto error;

	*temp = ((val & 0x1f) - 5) * 5;

error:
	mv88e6xxx_mdio_write_direct(chip, 0x0, 0x16, 0x0);
	mutex_unlock(&chip->reg_lock);
	return ret;
}

static int mv88e63xx_get_temp(struct dsa_switch *ds, int *temp)
{
	struct mv88e6xxx_chip *chip = ds_to_priv(ds);
	int phy = mv88e6xxx_6320_family(chip) ? 3 : 0;
	int ret;

	*temp = 0;

	ret = mv88e6xxx_mdio_page_read(ds, phy, 6, 27);
	if (ret < 0)
		return ret;

	*temp = (ret & 0xff) - 25;

	return 0;
}

static int mv88e6xxx_get_temp(struct dsa_switch *ds, int *temp)
{
	struct mv88e6xxx_chip *chip = ds_to_priv(ds);

	if (!mv88e6xxx_has(chip, MV88E6XXX_FLAG_TEMP))
		return -EOPNOTSUPP;

	if (mv88e6xxx_6320_family(chip) || mv88e6xxx_6352_family(chip))
		return mv88e63xx_get_temp(ds, temp);

	return mv88e61xx_get_temp(ds, temp);
}

static int mv88e6xxx_get_temp_limit(struct dsa_switch *ds, int *temp)
{
	struct mv88e6xxx_chip *chip = ds_to_priv(ds);
	int phy = mv88e6xxx_6320_family(chip) ? 3 : 0;
	int ret;

	if (!mv88e6xxx_has(chip, MV88E6XXX_FLAG_TEMP_LIMIT))
		return -EOPNOTSUPP;

	*temp = 0;

	ret = mv88e6xxx_mdio_page_read(ds, phy, 6, 26);
	if (ret < 0)
		return ret;

	*temp = (((ret >> 8) & 0x1f) * 5) - 25;

	return 0;
}

static int mv88e6xxx_set_temp_limit(struct dsa_switch *ds, int temp)
{
	struct mv88e6xxx_chip *chip = ds_to_priv(ds);
	int phy = mv88e6xxx_6320_family(chip) ? 3 : 0;
	int ret;

	if (!mv88e6xxx_has(chip, MV88E6XXX_FLAG_TEMP_LIMIT))
		return -EOPNOTSUPP;

	ret = mv88e6xxx_mdio_page_read(ds, phy, 6, 26);
	if (ret < 0)
		return ret;
	temp = clamp_val(DIV_ROUND_CLOSEST(temp, 5) + 5, 0, 0x1f);
	return mv88e6xxx_mdio_page_write(ds, phy, 6, 26,
					 (ret & 0xe0ff) | (temp << 8));
}

static int mv88e6xxx_get_temp_alarm(struct dsa_switch *ds, bool *alarm)
{
	struct mv88e6xxx_chip *chip = ds_to_priv(ds);
	int phy = mv88e6xxx_6320_family(chip) ? 3 : 0;
	int ret;

	if (!mv88e6xxx_has(chip, MV88E6XXX_FLAG_TEMP_LIMIT))
		return -EOPNOTSUPP;

	*alarm = false;

	ret = mv88e6xxx_mdio_page_read(ds, phy, 6, 26);
	if (ret < 0)
		return ret;

	*alarm = !!(ret & 0x40);

	return 0;
}
#endif /* CONFIG_NET_DSA_HWMON */

static const struct mv88e6xxx_info mv88e6xxx_table[] = {
	[MV88E6085] = {
		.prod_num = PORT_SWITCH_ID_PROD_NUM_6085,
		.family = MV88E6XXX_FAMILY_6097,
		.name = "Marvell 88E6085",
		.num_databases = 4096,
		.num_ports = 10,
		.port_base_addr = 0x10,
		.flags = MV88E6XXX_FLAGS_FAMILY_6097,
	},

	[MV88E6095] = {
		.prod_num = PORT_SWITCH_ID_PROD_NUM_6095,
		.family = MV88E6XXX_FAMILY_6095,
		.name = "Marvell 88E6095/88E6095F",
		.num_databases = 256,
		.num_ports = 11,
		.port_base_addr = 0x10,
		.flags = MV88E6XXX_FLAGS_FAMILY_6095,
	},

	[MV88E6123] = {
		.prod_num = PORT_SWITCH_ID_PROD_NUM_6123,
		.family = MV88E6XXX_FAMILY_6165,
		.name = "Marvell 88E6123",
		.num_databases = 4096,
		.num_ports = 3,
		.port_base_addr = 0x10,
		.flags = MV88E6XXX_FLAGS_FAMILY_6165,
	},

	[MV88E6131] = {
		.prod_num = PORT_SWITCH_ID_PROD_NUM_6131,
		.family = MV88E6XXX_FAMILY_6185,
		.name = "Marvell 88E6131",
		.num_databases = 256,
		.num_ports = 8,
		.port_base_addr = 0x10,
		.flags = MV88E6XXX_FLAGS_FAMILY_6185,
	},

	[MV88E6161] = {
		.prod_num = PORT_SWITCH_ID_PROD_NUM_6161,
		.family = MV88E6XXX_FAMILY_6165,
		.name = "Marvell 88E6161",
		.num_databases = 4096,
		.num_ports = 6,
		.port_base_addr = 0x10,
		.flags = MV88E6XXX_FLAGS_FAMILY_6165,
	},

	[MV88E6165] = {
		.prod_num = PORT_SWITCH_ID_PROD_NUM_6165,
		.family = MV88E6XXX_FAMILY_6165,
		.name = "Marvell 88E6165",
		.num_databases = 4096,
		.num_ports = 6,
		.port_base_addr = 0x10,
		.flags = MV88E6XXX_FLAGS_FAMILY_6165,
	},

	[MV88E6171] = {
		.prod_num = PORT_SWITCH_ID_PROD_NUM_6171,
		.family = MV88E6XXX_FAMILY_6351,
		.name = "Marvell 88E6171",
		.num_databases = 4096,
		.num_ports = 7,
		.port_base_addr = 0x10,
		.flags = MV88E6XXX_FLAGS_FAMILY_6351,
	},

	[MV88E6172] = {
		.prod_num = PORT_SWITCH_ID_PROD_NUM_6172,
		.family = MV88E6XXX_FAMILY_6352,
		.name = "Marvell 88E6172",
		.num_databases = 4096,
		.num_ports = 7,
		.port_base_addr = 0x10,
		.flags = MV88E6XXX_FLAGS_FAMILY_6352,
	},

	[MV88E6175] = {
		.prod_num = PORT_SWITCH_ID_PROD_NUM_6175,
		.family = MV88E6XXX_FAMILY_6351,
		.name = "Marvell 88E6175",
		.num_databases = 4096,
		.num_ports = 7,
		.port_base_addr = 0x10,
		.flags = MV88E6XXX_FLAGS_FAMILY_6351,
	},

	[MV88E6176] = {
		.prod_num = PORT_SWITCH_ID_PROD_NUM_6176,
		.family = MV88E6XXX_FAMILY_6352,
		.name = "Marvell 88E6176",
		.num_databases = 4096,
		.num_ports = 7,
		.port_base_addr = 0x10,
		.flags = MV88E6XXX_FLAGS_FAMILY_6352,
	},

	[MV88E6185] = {
		.prod_num = PORT_SWITCH_ID_PROD_NUM_6185,
		.family = MV88E6XXX_FAMILY_6185,
		.name = "Marvell 88E6185",
		.num_databases = 256,
		.num_ports = 10,
		.port_base_addr = 0x10,
		.flags = MV88E6XXX_FLAGS_FAMILY_6185,
	},

	[MV88E6240] = {
		.prod_num = PORT_SWITCH_ID_PROD_NUM_6240,
		.family = MV88E6XXX_FAMILY_6352,
		.name = "Marvell 88E6240",
		.num_databases = 4096,
		.num_ports = 7,
		.port_base_addr = 0x10,
		.flags = MV88E6XXX_FLAGS_FAMILY_6352,
	},

	[MV88E6320] = {
		.prod_num = PORT_SWITCH_ID_PROD_NUM_6320,
		.family = MV88E6XXX_FAMILY_6320,
		.name = "Marvell 88E6320",
		.num_databases = 4096,
		.num_ports = 7,
		.port_base_addr = 0x10,
		.flags = MV88E6XXX_FLAGS_FAMILY_6320,
	},

	[MV88E6321] = {
		.prod_num = PORT_SWITCH_ID_PROD_NUM_6321,
		.family = MV88E6XXX_FAMILY_6320,
		.name = "Marvell 88E6321",
		.num_databases = 4096,
		.num_ports = 7,
		.port_base_addr = 0x10,
		.flags = MV88E6XXX_FLAGS_FAMILY_6320,
	},

	[MV88E6350] = {
		.prod_num = PORT_SWITCH_ID_PROD_NUM_6350,
		.family = MV88E6XXX_FAMILY_6351,
		.name = "Marvell 88E6350",
		.num_databases = 4096,
		.num_ports = 7,
		.port_base_addr = 0x10,
		.flags = MV88E6XXX_FLAGS_FAMILY_6351,
	},

	[MV88E6351] = {
		.prod_num = PORT_SWITCH_ID_PROD_NUM_6351,
		.family = MV88E6XXX_FAMILY_6351,
		.name = "Marvell 88E6351",
		.num_databases = 4096,
		.num_ports = 7,
		.port_base_addr = 0x10,
		.flags = MV88E6XXX_FLAGS_FAMILY_6351,
	},

	[MV88E6352] = {
		.prod_num = PORT_SWITCH_ID_PROD_NUM_6352,
		.family = MV88E6XXX_FAMILY_6352,
		.name = "Marvell 88E6352",
		.num_databases = 4096,
		.num_ports = 7,
		.port_base_addr = 0x10,
		.flags = MV88E6XXX_FLAGS_FAMILY_6352,
	},
};

static const struct mv88e6xxx_info *mv88e6xxx_lookup_info(unsigned int prod_num)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(mv88e6xxx_table); ++i)
		if (mv88e6xxx_table[i].prod_num == prod_num)
			return &mv88e6xxx_table[i];

	return NULL;
}

static int mv88e6xxx_detect(struct mv88e6xxx_chip *chip)
{
	const struct mv88e6xxx_info *info;
	int id, prod_num, rev;

	id = mv88e6xxx_reg_read(chip, chip->info->port_base_addr,
				PORT_SWITCH_ID);
	if (id < 0)
		return id;

	prod_num = (id & 0xfff0) >> 4;
	rev = id & 0x000f;

	info = mv88e6xxx_lookup_info(prod_num);
	if (!info)
		return -ENODEV;

	/* Update the compatible info with the probed one */
	chip->info = info;

	dev_info(chip->dev, "switch 0x%x detected: %s, revision %u\n",
		 chip->info->prod_num, chip->info->name, rev);

	return 0;
}

static struct mv88e6xxx_chip *mv88e6xxx_alloc_chip(struct device *dev)
{
	struct mv88e6xxx_chip *chip;

	chip = devm_kzalloc(dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return NULL;

	chip->dev = dev;

	mutex_init(&chip->reg_lock);

	return chip;
}

static int mv88e6xxx_smi_init(struct mv88e6xxx_chip *chip,
			      struct mii_bus *bus, int sw_addr)
{
	/* ADDR[0] pin is unavailable externally and considered zero */
	if (sw_addr & 0x1)
		return -EINVAL;

	if (sw_addr == 0)
		chip->smi_ops = &mv88e6xxx_smi_single_chip_ops;
	else if (mv88e6xxx_has(chip, MV88E6XXX_FLAG_MULTI_CHIP))
		chip->smi_ops = &mv88e6xxx_smi_multi_chip_ops;
	else
		return -EINVAL;

	chip->bus = bus;
	chip->sw_addr = sw_addr;

	return 0;
}

static const char *mv88e6xxx_drv_probe(struct device *dsa_dev,
				       struct device *host_dev, int sw_addr,
				       void **priv)
{
	struct mv88e6xxx_chip *chip;
	struct mii_bus *bus;
	int err;

	bus = dsa_host_dev_to_mii_bus(host_dev);
	if (!bus)
		return NULL;

	chip = mv88e6xxx_alloc_chip(dsa_dev);
	if (!chip)
		return NULL;

	/* Legacy SMI probing will only support chips similar to 88E6085 */
	chip->info = &mv88e6xxx_table[MV88E6085];

	err = mv88e6xxx_smi_init(chip, bus, sw_addr);
	if (err)
		goto free;

	err = mv88e6xxx_detect(chip);
	if (err)
		goto free;

	err = mv88e6xxx_mdio_register(chip, NULL);
	if (err)
		goto free;

	*priv = chip;

	return chip->info->name;
free:
	devm_kfree(dsa_dev, chip);

	return NULL;
}

static struct dsa_switch_driver mv88e6xxx_switch_driver = {
	.tag_protocol		= DSA_TAG_PROTO_EDSA,
	.probe			= mv88e6xxx_drv_probe,
	.setup			= mv88e6xxx_setup,
	.set_addr		= mv88e6xxx_set_addr,
	.adjust_link		= mv88e6xxx_adjust_link,
	.get_strings		= mv88e6xxx_get_strings,
	.get_ethtool_stats	= mv88e6xxx_get_ethtool_stats,
	.get_sset_count		= mv88e6xxx_get_sset_count,
	.set_eee		= mv88e6xxx_set_eee,
	.get_eee		= mv88e6xxx_get_eee,
#ifdef CONFIG_NET_DSA_HWMON
	.get_temp		= mv88e6xxx_get_temp,
	.get_temp_limit		= mv88e6xxx_get_temp_limit,
	.set_temp_limit		= mv88e6xxx_set_temp_limit,
	.get_temp_alarm		= mv88e6xxx_get_temp_alarm,
#endif
	.get_eeprom_len		= mv88e6xxx_get_eeprom_len,
	.get_eeprom		= mv88e6xxx_get_eeprom,
	.set_eeprom		= mv88e6xxx_set_eeprom,
	.get_regs_len		= mv88e6xxx_get_regs_len,
	.get_regs		= mv88e6xxx_get_regs,
	.port_bridge_join	= mv88e6xxx_port_bridge_join,
	.port_bridge_leave	= mv88e6xxx_port_bridge_leave,
	.port_stp_state_set	= mv88e6xxx_port_stp_state_set,
	.port_vlan_filtering	= mv88e6xxx_port_vlan_filtering,
	.port_vlan_prepare	= mv88e6xxx_port_vlan_prepare,
	.port_vlan_add		= mv88e6xxx_port_vlan_add,
	.port_vlan_del		= mv88e6xxx_port_vlan_del,
	.port_vlan_dump		= mv88e6xxx_port_vlan_dump,
	.port_fdb_prepare       = mv88e6xxx_port_fdb_prepare,
	.port_fdb_add           = mv88e6xxx_port_fdb_add,
	.port_fdb_del           = mv88e6xxx_port_fdb_del,
	.port_fdb_dump          = mv88e6xxx_port_fdb_dump,
};

static int mv88e6xxx_register_switch(struct mv88e6xxx_chip *chip,
				     struct device_node *np)
{
	struct device *dev = chip->dev;
	struct dsa_switch *ds;

	ds = devm_kzalloc(dev, sizeof(*ds), GFP_KERNEL);
	if (!ds)
		return -ENOMEM;

	ds->dev = dev;
	ds->priv = chip;
	ds->drv = &mv88e6xxx_switch_driver;

	dev_set_drvdata(dev, ds);

	return dsa_register_switch(ds, np);
}

static void mv88e6xxx_unregister_switch(struct mv88e6xxx_chip *chip)
{
	dsa_unregister_switch(chip->ds);
}

static int mv88e6xxx_probe(struct mdio_device *mdiodev)
{
	struct device *dev = &mdiodev->dev;
	struct device_node *np = dev->of_node;
	const struct mv88e6xxx_info *compat_info;
	struct mv88e6xxx_chip *chip;
	u32 eeprom_len;
	int err;

	compat_info = of_device_get_match_data(dev);
	if (!compat_info)
		return -EINVAL;

	chip = mv88e6xxx_alloc_chip(dev);
	if (!chip)
		return -ENOMEM;

	chip->info = compat_info;

	err = mv88e6xxx_smi_init(chip, mdiodev->bus, mdiodev->addr);
	if (err)
		return err;

	err = mv88e6xxx_detect(chip);
	if (err)
		return err;

	chip->reset = devm_gpiod_get_optional(dev, "reset", GPIOD_ASIS);
	if (IS_ERR(chip->reset))
		return PTR_ERR(chip->reset);

	if (mv88e6xxx_has(chip, MV88E6XXX_FLAG_EEPROM) &&
	    !of_property_read_u32(np, "eeprom-length", &eeprom_len))
		chip->eeprom_len = eeprom_len;

	err = mv88e6xxx_mdio_register(chip, np);
	if (err)
		return err;

	err = mv88e6xxx_register_switch(chip, np);
	if (err) {
		mv88e6xxx_mdio_unregister(chip);
		return err;
	}

	return 0;
}

static void mv88e6xxx_remove(struct mdio_device *mdiodev)
{
	struct dsa_switch *ds = dev_get_drvdata(&mdiodev->dev);
	struct mv88e6xxx_chip *chip = ds_to_priv(ds);

	mv88e6xxx_unregister_switch(chip);
	mv88e6xxx_mdio_unregister(chip);
}

static const struct of_device_id mv88e6xxx_of_match[] = {
	{
		.compatible = "marvell,mv88e6085",
		.data = &mv88e6xxx_table[MV88E6085],
	},
	{ /* sentinel */ },
};

MODULE_DEVICE_TABLE(of, mv88e6xxx_of_match);

static struct mdio_driver mv88e6xxx_driver = {
	.probe	= mv88e6xxx_probe,
	.remove = mv88e6xxx_remove,
	.mdiodrv.driver = {
		.name = "mv88e6085",
		.of_match_table = mv88e6xxx_of_match,
	},
};

static int __init mv88e6xxx_init(void)
{
	register_switch_driver(&mv88e6xxx_switch_driver);
	return mdio_driver_register(&mv88e6xxx_driver);
}
module_init(mv88e6xxx_init);

static void __exit mv88e6xxx_cleanup(void)
{
	mdio_driver_unregister(&mv88e6xxx_driver);
	unregister_switch_driver(&mv88e6xxx_switch_driver);
}
module_exit(mv88e6xxx_cleanup);

MODULE_AUTHOR("Lennert Buytenhek <buytenh@wantstofly.org>");
MODULE_DESCRIPTION("Driver for Marvell 88E6XXX ethernet switch chips");
MODULE_LICENSE("GPL");
