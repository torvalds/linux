/*
 * Marvell 88E6xxx Switch Global 2 Registers support
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
#include <linux/interrupt.h>
#include <linux/irqdomain.h>

#include "chip.h"
#include "global1.h" /* for MV88E6XXX_G1_STS_IRQ_DEVICE */
#include "global2.h"

int mv88e6xxx_g2_read(struct mv88e6xxx_chip *chip, int reg, u16 *val)
{
	return mv88e6xxx_read(chip, chip->info->global2_addr, reg, val);
}

int mv88e6xxx_g2_write(struct mv88e6xxx_chip *chip, int reg, u16 val)
{
	return mv88e6xxx_write(chip, chip->info->global2_addr, reg, val);
}

int mv88e6xxx_g2_update(struct mv88e6xxx_chip *chip, int reg, u16 update)
{
	return mv88e6xxx_update(chip, chip->info->global2_addr, reg, update);
}

int mv88e6xxx_g2_wait(struct mv88e6xxx_chip *chip, int reg, u16 mask)
{
	return mv88e6xxx_wait(chip, chip->info->global2_addr, reg, mask);
}

/* Offset 0x00: Interrupt Source Register */

static int mv88e6xxx_g2_int_source(struct mv88e6xxx_chip *chip, u16 *src)
{
	/* Read (and clear most of) the Interrupt Source bits */
	return mv88e6xxx_g2_read(chip, MV88E6XXX_G2_INT_SRC, src);
}

/* Offset 0x01: Interrupt Mask Register */

static int mv88e6xxx_g2_int_mask(struct mv88e6xxx_chip *chip, u16 mask)
{
	return mv88e6xxx_g2_write(chip, MV88E6XXX_G2_INT_MASK, mask);
}

/* Offset 0x02: Management Enable 2x */

static int mv88e6xxx_g2_mgmt_enable_2x(struct mv88e6xxx_chip *chip, u16 en2x)
{
	return mv88e6xxx_g2_write(chip, MV88E6XXX_G2_MGMT_EN_2X, en2x);
}

/* Offset 0x03: Management Enable 0x */

static int mv88e6xxx_g2_mgmt_enable_0x(struct mv88e6xxx_chip *chip, u16 en0x)
{
	return mv88e6xxx_g2_write(chip, MV88E6XXX_G2_MGMT_EN_0X, en0x);
}

/* Offset 0x05: Switch Management Register */

static int mv88e6xxx_g2_switch_mgmt_rsvd2cpu(struct mv88e6xxx_chip *chip,
					     bool enable)
{
	u16 val;
	int err;

	err = mv88e6xxx_g2_read(chip, MV88E6XXX_G2_SWITCH_MGMT, &val);
	if (err)
		return err;

	if (enable)
		val |= MV88E6XXX_G2_SWITCH_MGMT_RSVD2CPU;
	else
		val &= ~MV88E6XXX_G2_SWITCH_MGMT_RSVD2CPU;

	return mv88e6xxx_g2_write(chip, MV88E6XXX_G2_SWITCH_MGMT, val);
}

int mv88e6185_g2_mgmt_rsvd2cpu(struct mv88e6xxx_chip *chip)
{
	int err;

	/* Consider the frames with reserved multicast destination
	 * addresses matching 01:80:c2:00:00:0x as MGMT.
	 */
	err = mv88e6xxx_g2_mgmt_enable_0x(chip, 0xffff);
	if (err)
		return err;

	return mv88e6xxx_g2_switch_mgmt_rsvd2cpu(chip, true);
}

int mv88e6352_g2_mgmt_rsvd2cpu(struct mv88e6xxx_chip *chip)
{
	int err;

	/* Consider the frames with reserved multicast destination
	 * addresses matching 01:80:c2:00:00:2x as MGMT.
	 */
	err = mv88e6xxx_g2_mgmt_enable_2x(chip, 0xffff);
	if (err)
		return err;

	return mv88e6185_g2_mgmt_rsvd2cpu(chip);
}

/* Offset 0x06: Device Mapping Table register */

static int mv88e6xxx_g2_device_mapping_write(struct mv88e6xxx_chip *chip,
					     int target, int port)
{
	u16 val = (target << 8) | (port & 0xf);

	return mv88e6xxx_g2_update(chip, MV88E6XXX_G2_DEVICE_MAPPING, val);
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

/* Offset 0x07: Trunk Mask Table register */

static int mv88e6xxx_g2_trunk_mask_write(struct mv88e6xxx_chip *chip, int num,
					 bool hash, u16 mask)
{
	u16 val = (num << 12) | (mask & mv88e6xxx_port_mask(chip));

	if (hash)
		val |= MV88E6XXX_G2_TRUNK_MASK_HASH;

	return mv88e6xxx_g2_update(chip, MV88E6XXX_G2_TRUNK_MASK, val);
}

/* Offset 0x08: Trunk Mapping Table register */

static int mv88e6xxx_g2_trunk_mapping_write(struct mv88e6xxx_chip *chip, int id,
					    u16 map)
{
	const u16 port_mask = BIT(mv88e6xxx_num_ports(chip)) - 1;
	u16 val = (id << 11) | (map & port_mask);

	return mv88e6xxx_g2_update(chip, MV88E6XXX_G2_TRUNK_MAPPING, val);
}

static int mv88e6xxx_g2_clear_trunk(struct mv88e6xxx_chip *chip)
{
	const u16 port_mask = BIT(mv88e6xxx_num_ports(chip)) - 1;
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

/* Offset 0x09: Ingress Rate Command register
 * Offset 0x0A: Ingress Rate Data register
 */

static int mv88e6xxx_g2_irl_wait(struct mv88e6xxx_chip *chip)
{
	return mv88e6xxx_g2_wait(chip, MV88E6XXX_G2_IRL_CMD,
				 MV88E6XXX_G2_IRL_CMD_BUSY);
}

static int mv88e6xxx_g2_irl_op(struct mv88e6xxx_chip *chip, u16 op, int port,
			       int res, int reg)
{
	int err;

	err = mv88e6xxx_g2_write(chip, MV88E6XXX_G2_IRL_CMD,
				 MV88E6XXX_G2_IRL_CMD_BUSY | op | (port << 8) |
				 (res << 5) | reg);
	if (err)
		return err;

	return mv88e6xxx_g2_irl_wait(chip);
}

int mv88e6352_g2_irl_init_all(struct mv88e6xxx_chip *chip, int port)
{
	return mv88e6xxx_g2_irl_op(chip, MV88E6352_G2_IRL_CMD_OP_INIT_ALL, port,
				   0, 0);
}

int mv88e6390_g2_irl_init_all(struct mv88e6xxx_chip *chip, int port)
{
	return mv88e6xxx_g2_irl_op(chip, MV88E6390_G2_IRL_CMD_OP_INIT_ALL, port,
				   0, 0);
}

/* Offset 0x0B: Cross-chip Port VLAN (Addr) Register
 * Offset 0x0C: Cross-chip Port VLAN Data Register
 */

static int mv88e6xxx_g2_pvt_op_wait(struct mv88e6xxx_chip *chip)
{
	return mv88e6xxx_g2_wait(chip, MV88E6XXX_G2_PVT_ADDR,
				 MV88E6XXX_G2_PVT_ADDR_BUSY);
}

static int mv88e6xxx_g2_pvt_op(struct mv88e6xxx_chip *chip, int src_dev,
			       int src_port, u16 op)
{
	int err;

	/* 9-bit Cross-chip PVT pointer: with MV88E6XXX_G2_MISC_5_BIT_PORT
	 * cleared, source device is 5-bit, source port is 4-bit.
	 */
	op |= MV88E6XXX_G2_PVT_ADDR_BUSY;
	op |= (src_dev & 0x1f) << 4;
	op |= (src_port & 0xf);

	err = mv88e6xxx_g2_write(chip, MV88E6XXX_G2_PVT_ADDR, op);
	if (err)
		return err;

	return mv88e6xxx_g2_pvt_op_wait(chip);
}

int mv88e6xxx_g2_pvt_write(struct mv88e6xxx_chip *chip, int src_dev,
			   int src_port, u16 data)
{
	int err;

	err = mv88e6xxx_g2_pvt_op_wait(chip);
	if (err)
		return err;

	err = mv88e6xxx_g2_write(chip, MV88E6XXX_G2_PVT_DATA, data);
	if (err)
		return err;

	return mv88e6xxx_g2_pvt_op(chip, src_dev, src_port,
				   MV88E6XXX_G2_PVT_ADDR_OP_WRITE_PVLAN);
}

/* Offset 0x0D: Switch MAC/WoL/WoF register */

static int mv88e6xxx_g2_switch_mac_write(struct mv88e6xxx_chip *chip,
					 unsigned int pointer, u8 data)
{
	u16 val = (pointer << 8) | data;

	return mv88e6xxx_g2_update(chip, MV88E6XXX_G2_SWITCH_MAC, val);
}

int mv88e6xxx_g2_set_switch_mac(struct mv88e6xxx_chip *chip, u8 *addr)
{
	int i, err;

	for (i = 0; i < 6; i++) {
		err = mv88e6xxx_g2_switch_mac_write(chip, i, addr[i]);
		if (err)
			break;
	}

	return err;
}

/* Offset 0x0F: Priority Override Table */

static int mv88e6xxx_g2_pot_write(struct mv88e6xxx_chip *chip, int pointer,
				  u8 data)
{
	u16 val = (pointer << 8) | (data & 0x7);

	return mv88e6xxx_g2_update(chip, MV88E6XXX_G2_PRIO_OVERRIDE, val);
}

int mv88e6xxx_g2_pot_clear(struct mv88e6xxx_chip *chip)
{
	int i, err;

	/* Clear all sixteen possible Priority Override entries */
	for (i = 0; i < 16; i++) {
		err = mv88e6xxx_g2_pot_write(chip, i, 0);
		if (err)
			break;
	}

	return err;
}

/* Offset 0x14: EEPROM Command
 * Offset 0x15: EEPROM Data (for 16-bit data access)
 * Offset 0x15: EEPROM Addr (for 8-bit data access)
 */

static int mv88e6xxx_g2_eeprom_wait(struct mv88e6xxx_chip *chip)
{
	return mv88e6xxx_g2_wait(chip, MV88E6XXX_G2_EEPROM_CMD,
				 MV88E6XXX_G2_EEPROM_CMD_BUSY |
				 MV88E6XXX_G2_EEPROM_CMD_RUNNING);
}

static int mv88e6xxx_g2_eeprom_cmd(struct mv88e6xxx_chip *chip, u16 cmd)
{
	int err;

	err = mv88e6xxx_g2_write(chip, MV88E6XXX_G2_EEPROM_CMD,
				 MV88E6XXX_G2_EEPROM_CMD_BUSY | cmd);
	if (err)
		return err;

	return mv88e6xxx_g2_eeprom_wait(chip);
}

static int mv88e6xxx_g2_eeprom_read8(struct mv88e6xxx_chip *chip,
				     u16 addr, u8 *data)
{
	u16 cmd = MV88E6XXX_G2_EEPROM_CMD_OP_READ;
	int err;

	err = mv88e6xxx_g2_eeprom_wait(chip);
	if (err)
		return err;

	err = mv88e6xxx_g2_write(chip, MV88E6390_G2_EEPROM_ADDR, addr);
	if (err)
		return err;

	err = mv88e6xxx_g2_eeprom_cmd(chip, cmd);
	if (err)
		return err;

	err = mv88e6xxx_g2_read(chip, MV88E6XXX_G2_EEPROM_CMD, &cmd);
	if (err)
		return err;

	*data = cmd & 0xff;

	return 0;
}

static int mv88e6xxx_g2_eeprom_write8(struct mv88e6xxx_chip *chip,
				      u16 addr, u8 data)
{
	u16 cmd = MV88E6XXX_G2_EEPROM_CMD_OP_WRITE |
		MV88E6XXX_G2_EEPROM_CMD_WRITE_EN;
	int err;

	err = mv88e6xxx_g2_eeprom_wait(chip);
	if (err)
		return err;

	err = mv88e6xxx_g2_write(chip, MV88E6390_G2_EEPROM_ADDR, addr);
	if (err)
		return err;

	return mv88e6xxx_g2_eeprom_cmd(chip, cmd | data);
}

static int mv88e6xxx_g2_eeprom_read16(struct mv88e6xxx_chip *chip,
				      u8 addr, u16 *data)
{
	u16 cmd = MV88E6XXX_G2_EEPROM_CMD_OP_READ | addr;
	int err;

	err = mv88e6xxx_g2_eeprom_wait(chip);
	if (err)
		return err;

	err = mv88e6xxx_g2_eeprom_cmd(chip, cmd);
	if (err)
		return err;

	return mv88e6xxx_g2_read(chip, MV88E6352_G2_EEPROM_DATA, data);
}

static int mv88e6xxx_g2_eeprom_write16(struct mv88e6xxx_chip *chip,
				       u8 addr, u16 data)
{
	u16 cmd = MV88E6XXX_G2_EEPROM_CMD_OP_WRITE | addr;
	int err;

	err = mv88e6xxx_g2_eeprom_wait(chip);
	if (err)
		return err;

	err = mv88e6xxx_g2_write(chip, MV88E6352_G2_EEPROM_DATA, data);
	if (err)
		return err;

	return mv88e6xxx_g2_eeprom_cmd(chip, cmd);
}

int mv88e6xxx_g2_get_eeprom8(struct mv88e6xxx_chip *chip,
			     struct ethtool_eeprom *eeprom, u8 *data)
{
	unsigned int offset = eeprom->offset;
	unsigned int len = eeprom->len;
	int err;

	eeprom->len = 0;

	while (len) {
		err = mv88e6xxx_g2_eeprom_read8(chip, offset, data);
		if (err)
			return err;

		eeprom->len++;
		offset++;
		data++;
		len--;
	}

	return 0;
}

int mv88e6xxx_g2_set_eeprom8(struct mv88e6xxx_chip *chip,
			     struct ethtool_eeprom *eeprom, u8 *data)
{
	unsigned int offset = eeprom->offset;
	unsigned int len = eeprom->len;
	int err;

	eeprom->len = 0;

	while (len) {
		err = mv88e6xxx_g2_eeprom_write8(chip, offset, *data);
		if (err)
			return err;

		eeprom->len++;
		offset++;
		data++;
		len--;
	}

	return 0;
}

int mv88e6xxx_g2_get_eeprom16(struct mv88e6xxx_chip *chip,
			      struct ethtool_eeprom *eeprom, u8 *data)
{
	unsigned int offset = eeprom->offset;
	unsigned int len = eeprom->len;
	u16 val;
	int err;

	eeprom->len = 0;

	if (offset & 1) {
		err = mv88e6xxx_g2_eeprom_read16(chip, offset >> 1, &val);
		if (err)
			return err;

		*data++ = (val >> 8) & 0xff;

		offset++;
		len--;
		eeprom->len++;
	}

	while (len >= 2) {
		err = mv88e6xxx_g2_eeprom_read16(chip, offset >> 1, &val);
		if (err)
			return err;

		*data++ = val & 0xff;
		*data++ = (val >> 8) & 0xff;

		offset += 2;
		len -= 2;
		eeprom->len += 2;
	}

	if (len) {
		err = mv88e6xxx_g2_eeprom_read16(chip, offset >> 1, &val);
		if (err)
			return err;

		*data++ = val & 0xff;

		offset++;
		len--;
		eeprom->len++;
	}

	return 0;
}

int mv88e6xxx_g2_set_eeprom16(struct mv88e6xxx_chip *chip,
			      struct ethtool_eeprom *eeprom, u8 *data)
{
	unsigned int offset = eeprom->offset;
	unsigned int len = eeprom->len;
	u16 val;
	int err;

	/* Ensure the RO WriteEn bit is set */
	err = mv88e6xxx_g2_read(chip, MV88E6XXX_G2_EEPROM_CMD, &val);
	if (err)
		return err;

	if (!(val & MV88E6XXX_G2_EEPROM_CMD_WRITE_EN))
		return -EROFS;

	eeprom->len = 0;

	if (offset & 1) {
		err = mv88e6xxx_g2_eeprom_read16(chip, offset >> 1, &val);
		if (err)
			return err;

		val = (*data++ << 8) | (val & 0xff);

		err = mv88e6xxx_g2_eeprom_write16(chip, offset >> 1, val);
		if (err)
			return err;

		offset++;
		len--;
		eeprom->len++;
	}

	while (len >= 2) {
		val = *data++;
		val |= *data++ << 8;

		err = mv88e6xxx_g2_eeprom_write16(chip, offset >> 1, val);
		if (err)
			return err;

		offset += 2;
		len -= 2;
		eeprom->len += 2;
	}

	if (len) {
		err = mv88e6xxx_g2_eeprom_read16(chip, offset >> 1, &val);
		if (err)
			return err;

		val = (val & 0xff00) | *data++;

		err = mv88e6xxx_g2_eeprom_write16(chip, offset >> 1, val);
		if (err)
			return err;

		offset++;
		len--;
		eeprom->len++;
	}

	return 0;
}

/* Offset 0x18: SMI PHY Command Register
 * Offset 0x19: SMI PHY Data Register
 */

static int mv88e6xxx_g2_smi_phy_wait(struct mv88e6xxx_chip *chip)
{
	return mv88e6xxx_g2_wait(chip, MV88E6XXX_G2_SMI_PHY_CMD,
				 MV88E6XXX_G2_SMI_PHY_CMD_BUSY);
}

static int mv88e6xxx_g2_smi_phy_cmd(struct mv88e6xxx_chip *chip, u16 cmd)
{
	int err;

	err = mv88e6xxx_g2_write(chip, MV88E6XXX_G2_SMI_PHY_CMD,
				 MV88E6XXX_G2_SMI_PHY_CMD_BUSY | cmd);
	if (err)
		return err;

	return mv88e6xxx_g2_smi_phy_wait(chip);
}

static int mv88e6xxx_g2_smi_phy_access(struct mv88e6xxx_chip *chip,
				       bool external, bool c45, u16 op, int dev,
				       int reg)
{
	u16 cmd = op;

	if (external)
		cmd |= MV88E6390_G2_SMI_PHY_CMD_FUNC_EXTERNAL;
	else
		cmd |= MV88E6390_G2_SMI_PHY_CMD_FUNC_INTERNAL; /* empty mask */

	if (c45)
		cmd |= MV88E6XXX_G2_SMI_PHY_CMD_MODE_45; /* empty mask */
	else
		cmd |= MV88E6XXX_G2_SMI_PHY_CMD_MODE_22;

	dev <<= __bf_shf(MV88E6XXX_G2_SMI_PHY_CMD_DEV_ADDR_MASK);
	cmd |= dev & MV88E6XXX_G2_SMI_PHY_CMD_DEV_ADDR_MASK;
	cmd |= reg & MV88E6XXX_G2_SMI_PHY_CMD_REG_ADDR_MASK;

	return mv88e6xxx_g2_smi_phy_cmd(chip, cmd);
}

static int mv88e6xxx_g2_smi_phy_access_c22(struct mv88e6xxx_chip *chip,
					   bool external, u16 op, int dev,
					   int reg)
{
	return mv88e6xxx_g2_smi_phy_access(chip, external, false, op, dev, reg);
}

/* IEEE 802.3 Clause 22 Read Data Register */
static int mv88e6xxx_g2_smi_phy_read_data_c22(struct mv88e6xxx_chip *chip,
					      bool external, int dev, int reg,
					      u16 *data)
{
	u16 op = MV88E6XXX_G2_SMI_PHY_CMD_OP_22_READ_DATA;
	int err;

	err = mv88e6xxx_g2_smi_phy_wait(chip);
	if (err)
		return err;

	err = mv88e6xxx_g2_smi_phy_access_c22(chip, external, op, dev, reg);
	if (err)
		return err;

	return mv88e6xxx_g2_read(chip, MV88E6XXX_G2_SMI_PHY_DATA, data);
}

/* IEEE 802.3 Clause 22 Write Data Register */
static int mv88e6xxx_g2_smi_phy_write_data_c22(struct mv88e6xxx_chip *chip,
					       bool external, int dev, int reg,
					       u16 data)
{
	u16 op = MV88E6XXX_G2_SMI_PHY_CMD_OP_22_WRITE_DATA;
	int err;

	err = mv88e6xxx_g2_smi_phy_wait(chip);
	if (err)
		return err;

	err = mv88e6xxx_g2_write(chip, MV88E6XXX_G2_SMI_PHY_DATA, data);
	if (err)
		return err;

	return mv88e6xxx_g2_smi_phy_access_c22(chip, external, op, dev, reg);
}

static int mv88e6xxx_g2_smi_phy_access_c45(struct mv88e6xxx_chip *chip,
					   bool external, u16 op, int port,
					   int dev)
{
	return mv88e6xxx_g2_smi_phy_access(chip, external, true, op, port, dev);
}

/* IEEE 802.3 Clause 45 Write Address Register */
static int mv88e6xxx_g2_smi_phy_write_addr_c45(struct mv88e6xxx_chip *chip,
					       bool external, int port, int dev,
					       int addr)
{
	u16 op = MV88E6XXX_G2_SMI_PHY_CMD_OP_45_WRITE_ADDR;
	int err;

	err = mv88e6xxx_g2_smi_phy_wait(chip);
	if (err)
		return err;

	err = mv88e6xxx_g2_write(chip, MV88E6XXX_G2_SMI_PHY_DATA, addr);
	if (err)
		return err;

	return mv88e6xxx_g2_smi_phy_access_c45(chip, external, op, port, dev);
}

/* IEEE 802.3 Clause 45 Read Data Register */
static int mv88e6xxx_g2_smi_phy_read_data_c45(struct mv88e6xxx_chip *chip,
					      bool external, int port, int dev,
					      u16 *data)
{
	u16 op = MV88E6XXX_G2_SMI_PHY_CMD_OP_45_READ_DATA;
	int err;

	err = mv88e6xxx_g2_smi_phy_access_c45(chip, external, op, port, dev);
	if (err)
		return err;

	return mv88e6xxx_g2_read(chip, MV88E6XXX_G2_SMI_PHY_DATA, data);
}

static int mv88e6xxx_g2_smi_phy_read_c45(struct mv88e6xxx_chip *chip,
					 bool external, int port, int reg,
					 u16 *data)
{
	int dev = (reg >> 16) & 0x1f;
	int addr = reg & 0xffff;
	int err;

	err = mv88e6xxx_g2_smi_phy_write_addr_c45(chip, external, port, dev,
						  addr);
	if (err)
		return err;

	return mv88e6xxx_g2_smi_phy_read_data_c45(chip, external, port, dev,
						  data);
}

/* IEEE 802.3 Clause 45 Write Data Register */
static int mv88e6xxx_g2_smi_phy_write_data_c45(struct mv88e6xxx_chip *chip,
					       bool external, int port, int dev,
					       u16 data)
{
	u16 op = MV88E6XXX_G2_SMI_PHY_CMD_OP_45_WRITE_DATA;
	int err;

	err = mv88e6xxx_g2_write(chip, MV88E6XXX_G2_SMI_PHY_DATA, data);
	if (err)
		return err;

	return mv88e6xxx_g2_smi_phy_access_c45(chip, external, op, port, dev);
}

static int mv88e6xxx_g2_smi_phy_write_c45(struct mv88e6xxx_chip *chip,
					  bool external, int port, int reg,
					  u16 data)
{
	int dev = (reg >> 16) & 0x1f;
	int addr = reg & 0xffff;
	int err;

	err = mv88e6xxx_g2_smi_phy_write_addr_c45(chip, external, port, dev,
						  addr);
	if (err)
		return err;

	return mv88e6xxx_g2_smi_phy_write_data_c45(chip, external, port, dev,
						   data);
}

int mv88e6xxx_g2_smi_phy_read(struct mv88e6xxx_chip *chip, struct mii_bus *bus,
			      int addr, int reg, u16 *val)
{
	struct mv88e6xxx_mdio_bus *mdio_bus = bus->priv;
	bool external = mdio_bus->external;

	if (reg & MII_ADDR_C45)
		return mv88e6xxx_g2_smi_phy_read_c45(chip, external, addr, reg,
						     val);

	return mv88e6xxx_g2_smi_phy_read_data_c22(chip, external, addr, reg,
						  val);
}

int mv88e6xxx_g2_smi_phy_write(struct mv88e6xxx_chip *chip, struct mii_bus *bus,
			       int addr, int reg, u16 val)
{
	struct mv88e6xxx_mdio_bus *mdio_bus = bus->priv;
	bool external = mdio_bus->external;

	if (reg & MII_ADDR_C45)
		return mv88e6xxx_g2_smi_phy_write_c45(chip, external, addr, reg,
						      val);

	return mv88e6xxx_g2_smi_phy_write_data_c22(chip, external, addr, reg,
						   val);
}

/* Offset 0x1B: Watchdog Control */
static int mv88e6097_watchdog_action(struct mv88e6xxx_chip *chip, int irq)
{
	u16 reg;

	mv88e6xxx_g2_read(chip, MV88E6352_G2_WDOG_CTL, &reg);

	dev_info(chip->dev, "Watchdog event: 0x%04x", reg);

	return IRQ_HANDLED;
}

static void mv88e6097_watchdog_free(struct mv88e6xxx_chip *chip)
{
	u16 reg;

	mv88e6xxx_g2_read(chip, MV88E6352_G2_WDOG_CTL, &reg);

	reg &= ~(MV88E6352_G2_WDOG_CTL_EGRESS_ENABLE |
		 MV88E6352_G2_WDOG_CTL_QC_ENABLE);

	mv88e6xxx_g2_write(chip, MV88E6352_G2_WDOG_CTL, reg);
}

static int mv88e6097_watchdog_setup(struct mv88e6xxx_chip *chip)
{
	return mv88e6xxx_g2_write(chip, MV88E6352_G2_WDOG_CTL,
				  MV88E6352_G2_WDOG_CTL_EGRESS_ENABLE |
				  MV88E6352_G2_WDOG_CTL_QC_ENABLE |
				  MV88E6352_G2_WDOG_CTL_SWRESET);
}

const struct mv88e6xxx_irq_ops mv88e6097_watchdog_ops = {
	.irq_action = mv88e6097_watchdog_action,
	.irq_setup = mv88e6097_watchdog_setup,
	.irq_free = mv88e6097_watchdog_free,
};

static int mv88e6390_watchdog_setup(struct mv88e6xxx_chip *chip)
{
	return mv88e6xxx_g2_update(chip, MV88E6390_G2_WDOG_CTL,
				   MV88E6390_G2_WDOG_CTL_PTR_INT_ENABLE |
				   MV88E6390_G2_WDOG_CTL_CUT_THROUGH |
				   MV88E6390_G2_WDOG_CTL_QUEUE_CONTROLLER |
				   MV88E6390_G2_WDOG_CTL_EGRESS |
				   MV88E6390_G2_WDOG_CTL_FORCE_IRQ);
}

static int mv88e6390_watchdog_action(struct mv88e6xxx_chip *chip, int irq)
{
	int err;
	u16 reg;

	mv88e6xxx_g2_write(chip, MV88E6390_G2_WDOG_CTL,
			   MV88E6390_G2_WDOG_CTL_PTR_EVENT);
	err = mv88e6xxx_g2_read(chip, MV88E6390_G2_WDOG_CTL, &reg);

	dev_info(chip->dev, "Watchdog event: 0x%04x",
		 reg & MV88E6390_G2_WDOG_CTL_DATA_MASK);

	mv88e6xxx_g2_write(chip, MV88E6390_G2_WDOG_CTL,
			   MV88E6390_G2_WDOG_CTL_PTR_HISTORY);
	err = mv88e6xxx_g2_read(chip, MV88E6390_G2_WDOG_CTL, &reg);

	dev_info(chip->dev, "Watchdog history: 0x%04x",
		 reg & MV88E6390_G2_WDOG_CTL_DATA_MASK);

	/* Trigger a software reset to try to recover the switch */
	if (chip->info->ops->reset)
		chip->info->ops->reset(chip);

	mv88e6390_watchdog_setup(chip);

	return IRQ_HANDLED;
}

static void mv88e6390_watchdog_free(struct mv88e6xxx_chip *chip)
{
	mv88e6xxx_g2_update(chip, MV88E6390_G2_WDOG_CTL,
			    MV88E6390_G2_WDOG_CTL_PTR_INT_ENABLE);
}

const struct mv88e6xxx_irq_ops mv88e6390_watchdog_ops = {
	.irq_action = mv88e6390_watchdog_action,
	.irq_setup = mv88e6390_watchdog_setup,
	.irq_free = mv88e6390_watchdog_free,
};

static irqreturn_t mv88e6xxx_g2_watchdog_thread_fn(int irq, void *dev_id)
{
	struct mv88e6xxx_chip *chip = dev_id;
	irqreturn_t ret = IRQ_NONE;

	mutex_lock(&chip->reg_lock);
	if (chip->info->ops->watchdog_ops->irq_action)
		ret = chip->info->ops->watchdog_ops->irq_action(chip, irq);
	mutex_unlock(&chip->reg_lock);

	return ret;
}

static void mv88e6xxx_g2_watchdog_free(struct mv88e6xxx_chip *chip)
{
	mutex_lock(&chip->reg_lock);
	if (chip->info->ops->watchdog_ops->irq_free)
		chip->info->ops->watchdog_ops->irq_free(chip);
	mutex_unlock(&chip->reg_lock);

	free_irq(chip->watchdog_irq, chip);
	irq_dispose_mapping(chip->watchdog_irq);
}

static int mv88e6xxx_g2_watchdog_setup(struct mv88e6xxx_chip *chip)
{
	int err;

	chip->watchdog_irq = irq_find_mapping(chip->g2_irq.domain,
					      MV88E6XXX_G2_INT_SOURCE_WATCHDOG);
	if (chip->watchdog_irq < 0)
		return chip->watchdog_irq;

	err = request_threaded_irq(chip->watchdog_irq, NULL,
				   mv88e6xxx_g2_watchdog_thread_fn,
				   IRQF_ONESHOT | IRQF_TRIGGER_FALLING,
				   "mv88e6xxx-watchdog", chip);
	if (err)
		return err;

	mutex_lock(&chip->reg_lock);
	if (chip->info->ops->watchdog_ops->irq_setup)
		err = chip->info->ops->watchdog_ops->irq_setup(chip);
	mutex_unlock(&chip->reg_lock);

	return err;
}

/* Offset 0x1D: Misc Register */

static int mv88e6xxx_g2_misc_5_bit_port(struct mv88e6xxx_chip *chip,
					bool port_5_bit)
{
	u16 val;
	int err;

	err = mv88e6xxx_g2_read(chip, MV88E6XXX_G2_MISC, &val);
	if (err)
		return err;

	if (port_5_bit)
		val |= MV88E6XXX_G2_MISC_5_BIT_PORT;
	else
		val &= ~MV88E6XXX_G2_MISC_5_BIT_PORT;

	return mv88e6xxx_g2_write(chip, MV88E6XXX_G2_MISC, val);
}

int mv88e6xxx_g2_misc_4_bit_port(struct mv88e6xxx_chip *chip)
{
	return mv88e6xxx_g2_misc_5_bit_port(chip, false);
}

static void mv88e6xxx_g2_irq_mask(struct irq_data *d)
{
	struct mv88e6xxx_chip *chip = irq_data_get_irq_chip_data(d);
	unsigned int n = d->hwirq;

	chip->g2_irq.masked |= (1 << n);
}

static void mv88e6xxx_g2_irq_unmask(struct irq_data *d)
{
	struct mv88e6xxx_chip *chip = irq_data_get_irq_chip_data(d);
	unsigned int n = d->hwirq;

	chip->g2_irq.masked &= ~(1 << n);
}

static irqreturn_t mv88e6xxx_g2_irq_thread_fn(int irq, void *dev_id)
{
	struct mv88e6xxx_chip *chip = dev_id;
	unsigned int nhandled = 0;
	unsigned int sub_irq;
	unsigned int n;
	int err;
	u16 reg;

	mutex_lock(&chip->reg_lock);
	err = mv88e6xxx_g2_int_source(chip, &reg);
	mutex_unlock(&chip->reg_lock);
	if (err)
		goto out;

	for (n = 0; n < 16; ++n) {
		if (reg & (1 << n)) {
			sub_irq = irq_find_mapping(chip->g2_irq.domain, n);
			handle_nested_irq(sub_irq);
			++nhandled;
		}
	}
out:
	return (nhandled > 0 ? IRQ_HANDLED : IRQ_NONE);
}

static void mv88e6xxx_g2_irq_bus_lock(struct irq_data *d)
{
	struct mv88e6xxx_chip *chip = irq_data_get_irq_chip_data(d);

	mutex_lock(&chip->reg_lock);
}

static void mv88e6xxx_g2_irq_bus_sync_unlock(struct irq_data *d)
{
	struct mv88e6xxx_chip *chip = irq_data_get_irq_chip_data(d);
	int err;

	err = mv88e6xxx_g2_int_mask(chip, ~chip->g2_irq.masked);
	if (err)
		dev_err(chip->dev, "failed to mask interrupts\n");

	mutex_unlock(&chip->reg_lock);
}

static const struct irq_chip mv88e6xxx_g2_irq_chip = {
	.name			= "mv88e6xxx-g2",
	.irq_mask		= mv88e6xxx_g2_irq_mask,
	.irq_unmask		= mv88e6xxx_g2_irq_unmask,
	.irq_bus_lock		= mv88e6xxx_g2_irq_bus_lock,
	.irq_bus_sync_unlock	= mv88e6xxx_g2_irq_bus_sync_unlock,
};

static int mv88e6xxx_g2_irq_domain_map(struct irq_domain *d,
				       unsigned int irq,
				       irq_hw_number_t hwirq)
{
	struct mv88e6xxx_chip *chip = d->host_data;

	irq_set_chip_data(irq, d->host_data);
	irq_set_chip_and_handler(irq, &chip->g2_irq.chip, handle_level_irq);
	irq_set_noprobe(irq);

	return 0;
}

static const struct irq_domain_ops mv88e6xxx_g2_irq_domain_ops = {
	.map	= mv88e6xxx_g2_irq_domain_map,
	.xlate	= irq_domain_xlate_twocell,
};

void mv88e6xxx_g2_irq_free(struct mv88e6xxx_chip *chip)
{
	int irq, virq;

	mv88e6xxx_g2_watchdog_free(chip);

	free_irq(chip->device_irq, chip);
	irq_dispose_mapping(chip->device_irq);

	for (irq = 0; irq < 16; irq++) {
		virq = irq_find_mapping(chip->g2_irq.domain, irq);
		irq_dispose_mapping(virq);
	}

	irq_domain_remove(chip->g2_irq.domain);
}

int mv88e6xxx_g2_irq_setup(struct mv88e6xxx_chip *chip)
{
	int err, irq, virq;

	if (!chip->dev->of_node)
		return -EINVAL;

	chip->g2_irq.domain = irq_domain_add_simple(
		chip->dev->of_node, 16, 0, &mv88e6xxx_g2_irq_domain_ops, chip);
	if (!chip->g2_irq.domain)
		return -ENOMEM;

	for (irq = 0; irq < 16; irq++)
		irq_create_mapping(chip->g2_irq.domain, irq);

	chip->g2_irq.chip = mv88e6xxx_g2_irq_chip;
	chip->g2_irq.masked = ~0;

	chip->device_irq = irq_find_mapping(chip->g1_irq.domain,
					    MV88E6XXX_G1_STS_IRQ_DEVICE);
	if (chip->device_irq < 0) {
		err = chip->device_irq;
		goto out;
	}

	err = request_threaded_irq(chip->device_irq, NULL,
				   mv88e6xxx_g2_irq_thread_fn,
				   IRQF_ONESHOT, "mv88e6xxx-g2", chip);
	if (err)
		goto out;

	return mv88e6xxx_g2_watchdog_setup(chip);

out:
	for (irq = 0; irq < 16; irq++) {
		virq = irq_find_mapping(chip->g2_irq.domain, irq);
		irq_dispose_mapping(virq);
	}

	irq_domain_remove(chip->g2_irq.domain);

	return err;
}

int mv88e6xxx_g2_irq_mdio_setup(struct mv88e6xxx_chip *chip,
				struct mii_bus *bus)
{
	int phy, irq, err, err_phy;

	for (phy = 0; phy < chip->info->num_internal_phys; phy++) {
		irq = irq_find_mapping(chip->g2_irq.domain, phy);
		if (irq < 0) {
			err = irq;
			goto out;
		}
		bus->irq[chip->info->phy_base_addr + phy] = irq;
	}
	return 0;
out:
	err_phy = phy;

	for (phy = 0; phy < err_phy; phy++)
		irq_dispose_mapping(bus->irq[phy]);

	return err;
}

void mv88e6xxx_g2_irq_mdio_free(struct mv88e6xxx_chip *chip,
				struct mii_bus *bus)
{
	int phy;

	for (phy = 0; phy < chip->info->num_internal_phys; phy++)
		irq_dispose_mapping(bus->irq[phy]);
}

int mv88e6xxx_g2_setup(struct mv88e6xxx_chip *chip)
{
	u16 reg;
	int err;

	/* Ignore removed tag data on doubly tagged packets, disable
	 * flow control messages, force flow control priority to the
	 * highest, and send all special multicast frames to the CPU
	 * port at the highest priority.
	 */
	reg = MV88E6XXX_G2_SWITCH_MGMT_FORCE_FLOW_CTL_PRI | (0x7 << 4);
	err = mv88e6xxx_g2_write(chip, MV88E6XXX_G2_SWITCH_MGMT, reg);
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

	return 0;
}
