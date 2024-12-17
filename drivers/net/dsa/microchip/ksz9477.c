// SPDX-License-Identifier: GPL-2.0
/*
 * Microchip KSZ9477 switch driver main logic
 *
 * Copyright (C) 2017-2019 Microchip Technology Inc.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/iopoll.h>
#include <linux/platform_data/microchip-ksz.h>
#include <linux/phy.h>
#include <linux/if_bridge.h>
#include <linux/if_vlan.h>
#include <net/dsa.h>
#include <net/switchdev.h>

#include "ksz9477_reg.h"
#include "ksz_common.h"
#include "ksz9477.h"

static void ksz_cfg(struct ksz_device *dev, u32 addr, u8 bits, bool set)
{
	regmap_update_bits(ksz_regmap_8(dev), addr, bits, set ? bits : 0);
}

static void ksz_port_cfg(struct ksz_device *dev, int port, int offset, u8 bits,
			 bool set)
{
	regmap_update_bits(ksz_regmap_8(dev), PORT_CTRL_ADDR(port, offset),
			   bits, set ? bits : 0);
}

static void ksz9477_cfg32(struct ksz_device *dev, u32 addr, u32 bits, bool set)
{
	regmap_update_bits(ksz_regmap_32(dev), addr, bits, set ? bits : 0);
}

static void ksz9477_port_cfg32(struct ksz_device *dev, int port, int offset,
			       u32 bits, bool set)
{
	regmap_update_bits(ksz_regmap_32(dev), PORT_CTRL_ADDR(port, offset),
			   bits, set ? bits : 0);
}

int ksz9477_change_mtu(struct ksz_device *dev, int port, int mtu)
{
	u16 frame_size;

	if (!dsa_is_cpu_port(dev->ds, port))
		return 0;

	frame_size = mtu + VLAN_ETH_HLEN + ETH_FCS_LEN;

	return regmap_update_bits(ksz_regmap_16(dev), REG_SW_MTU__2,
				  REG_SW_MTU_MASK, frame_size);
}

static int ksz9477_wait_vlan_ctrl_ready(struct ksz_device *dev)
{
	unsigned int val;

	return regmap_read_poll_timeout(ksz_regmap_8(dev), REG_SW_VLAN_CTRL,
					val, !(val & VLAN_START), 10, 1000);
}

static int ksz9477_get_vlan_table(struct ksz_device *dev, u16 vid,
				  u32 *vlan_table)
{
	int ret;

	mutex_lock(&dev->vlan_mutex);

	ksz_write16(dev, REG_SW_VLAN_ENTRY_INDEX__2, vid & VLAN_INDEX_M);
	ksz_write8(dev, REG_SW_VLAN_CTRL, VLAN_READ | VLAN_START);

	/* wait to be cleared */
	ret = ksz9477_wait_vlan_ctrl_ready(dev);
	if (ret) {
		dev_dbg(dev->dev, "Failed to read vlan table\n");
		goto exit;
	}

	ksz_read32(dev, REG_SW_VLAN_ENTRY__4, &vlan_table[0]);
	ksz_read32(dev, REG_SW_VLAN_ENTRY_UNTAG__4, &vlan_table[1]);
	ksz_read32(dev, REG_SW_VLAN_ENTRY_PORTS__4, &vlan_table[2]);

	ksz_write8(dev, REG_SW_VLAN_CTRL, 0);

exit:
	mutex_unlock(&dev->vlan_mutex);

	return ret;
}

static int ksz9477_set_vlan_table(struct ksz_device *dev, u16 vid,
				  u32 *vlan_table)
{
	int ret;

	mutex_lock(&dev->vlan_mutex);

	ksz_write32(dev, REG_SW_VLAN_ENTRY__4, vlan_table[0]);
	ksz_write32(dev, REG_SW_VLAN_ENTRY_UNTAG__4, vlan_table[1]);
	ksz_write32(dev, REG_SW_VLAN_ENTRY_PORTS__4, vlan_table[2]);

	ksz_write16(dev, REG_SW_VLAN_ENTRY_INDEX__2, vid & VLAN_INDEX_M);
	ksz_write8(dev, REG_SW_VLAN_CTRL, VLAN_START | VLAN_WRITE);

	/* wait to be cleared */
	ret = ksz9477_wait_vlan_ctrl_ready(dev);
	if (ret) {
		dev_dbg(dev->dev, "Failed to write vlan table\n");
		goto exit;
	}

	ksz_write8(dev, REG_SW_VLAN_CTRL, 0);

	/* update vlan cache table */
	dev->vlan_cache[vid].table[0] = vlan_table[0];
	dev->vlan_cache[vid].table[1] = vlan_table[1];
	dev->vlan_cache[vid].table[2] = vlan_table[2];

exit:
	mutex_unlock(&dev->vlan_mutex);

	return ret;
}

static void ksz9477_read_table(struct ksz_device *dev, u32 *table)
{
	ksz_read32(dev, REG_SW_ALU_VAL_A, &table[0]);
	ksz_read32(dev, REG_SW_ALU_VAL_B, &table[1]);
	ksz_read32(dev, REG_SW_ALU_VAL_C, &table[2]);
	ksz_read32(dev, REG_SW_ALU_VAL_D, &table[3]);
}

static void ksz9477_write_table(struct ksz_device *dev, u32 *table)
{
	ksz_write32(dev, REG_SW_ALU_VAL_A, table[0]);
	ksz_write32(dev, REG_SW_ALU_VAL_B, table[1]);
	ksz_write32(dev, REG_SW_ALU_VAL_C, table[2]);
	ksz_write32(dev, REG_SW_ALU_VAL_D, table[3]);
}

static int ksz9477_wait_alu_ready(struct ksz_device *dev)
{
	unsigned int val;

	return regmap_read_poll_timeout(ksz_regmap_32(dev), REG_SW_ALU_CTRL__4,
					val, !(val & ALU_START), 10, 1000);
}

static int ksz9477_wait_alu_sta_ready(struct ksz_device *dev)
{
	unsigned int val;

	return regmap_read_poll_timeout(ksz_regmap_32(dev),
					REG_SW_ALU_STAT_CTRL__4,
					val, !(val & ALU_STAT_START),
					10, 1000);
}

int ksz9477_reset_switch(struct ksz_device *dev)
{
	u8 data8;
	u32 data32;

	/* reset switch */
	ksz_cfg(dev, REG_SW_OPERATION, SW_RESET, true);

	/* turn off SPI DO Edge select */
	regmap_update_bits(ksz_regmap_8(dev), REG_SW_GLOBAL_SERIAL_CTRL_0,
			   SPI_AUTO_EDGE_DETECTION, 0);

	/* default configuration */
	ksz_write8(dev, REG_SW_LUE_CTRL_1,
		   SW_AGING_ENABLE | SW_LINK_AUTO_AGING | SW_SRC_ADDR_FILTER);

	/* disable interrupts */
	ksz_write32(dev, REG_SW_INT_MASK__4, SWITCH_INT_MASK);
	ksz_write32(dev, REG_SW_PORT_INT_MASK__4, 0x7F);
	ksz_read32(dev, REG_SW_PORT_INT_STATUS__4, &data32);

	/* KSZ9893 compatible chips do not support refclk configuration */
	if (dev->chip_id == KSZ9893_CHIP_ID ||
	    dev->chip_id == KSZ8563_CHIP_ID ||
	    dev->chip_id == KSZ9563_CHIP_ID)
		return 0;

	data8 = SW_ENABLE_REFCLKO;
	if (dev->synclko_disable)
		data8 = 0;
	else if (dev->synclko_125)
		data8 = SW_ENABLE_REFCLKO | SW_REFCLKO_IS_125MHZ;
	ksz_write8(dev, REG_SW_GLOBAL_OUTPUT_CTRL__1, data8);

	return 0;
}

void ksz9477_r_mib_cnt(struct ksz_device *dev, int port, u16 addr, u64 *cnt)
{
	struct ksz_port *p = &dev->ports[port];
	unsigned int val;
	u32 data;
	int ret;

	/* retain the flush/freeze bit */
	data = p->freeze ? MIB_COUNTER_FLUSH_FREEZE : 0;
	data |= MIB_COUNTER_READ;
	data |= (addr << MIB_COUNTER_INDEX_S);
	ksz_pwrite32(dev, port, REG_PORT_MIB_CTRL_STAT__4, data);

	ret = regmap_read_poll_timeout(ksz_regmap_32(dev),
			PORT_CTRL_ADDR(port, REG_PORT_MIB_CTRL_STAT__4),
			val, !(val & MIB_COUNTER_READ), 10, 1000);
	/* failed to read MIB. get out of loop */
	if (ret) {
		dev_dbg(dev->dev, "Failed to get MIB\n");
		return;
	}

	/* count resets upon read */
	ksz_pread32(dev, port, REG_PORT_MIB_DATA, &data);
	*cnt += data;
}

void ksz9477_r_mib_pkt(struct ksz_device *dev, int port, u16 addr,
		       u64 *dropped, u64 *cnt)
{
	addr = dev->info->mib_names[addr].index;
	ksz9477_r_mib_cnt(dev, port, addr, cnt);
}

void ksz9477_freeze_mib(struct ksz_device *dev, int port, bool freeze)
{
	u32 val = freeze ? MIB_COUNTER_FLUSH_FREEZE : 0;
	struct ksz_port *p = &dev->ports[port];

	/* enable/disable the port for flush/freeze function */
	mutex_lock(&p->mib.cnt_mutex);
	ksz_pwrite32(dev, port, REG_PORT_MIB_CTRL_STAT__4, val);

	/* used by MIB counter reading code to know freeze is enabled */
	p->freeze = freeze;
	mutex_unlock(&p->mib.cnt_mutex);
}

static int ksz9477_half_duplex_monitor(struct ksz_device *dev, int port,
				       u64 tx_late_col)
{
	u8 lue_ctrl;
	u32 pmavbc;
	u16 pqm;
	int ret;

	/* Errata DS80000754 recommends monitoring potential faults in
	 * half-duplex mode. The switch might not be able to communicate anymore
	 * in these states. If you see this message, please read the
	 * errata-sheet for more information:
	 * https://ww1.microchip.com/downloads/aemDocuments/documents/UNG/ProductDocuments/Errata/KSZ9477S-Errata-DS80000754.pdf
	 * To workaround this issue, half-duplex mode should be avoided.
	 * A software reset could be implemented to recover from this state.
	 */
	dev_warn_once(dev->dev,
		      "Half-duplex detected on port %d, transmission halt may occur\n",
		      port);
	if (tx_late_col != 0) {
		/* Transmission halt with late collisions */
		dev_crit_once(dev->dev,
			      "TX late collisions detected, transmission may be halted on port %d\n",
			      port);
	}
	ret = ksz_read8(dev, REG_SW_LUE_CTRL_0, &lue_ctrl);
	if (ret)
		return ret;
	if (lue_ctrl & SW_VLAN_ENABLE) {
		ret = ksz_pread16(dev, port, REG_PORT_QM_TX_CNT_0__4, &pqm);
		if (ret)
			return ret;

		ret = ksz_read32(dev, REG_PMAVBC, &pmavbc);
		if (ret)
			return ret;

		if ((FIELD_GET(PMAVBC_MASK, pmavbc) <= PMAVBC_MIN) ||
		    (FIELD_GET(PORT_QM_TX_CNT_M, pqm) >= PORT_QM_TX_CNT_MAX)) {
			/* Transmission halt with Half-Duplex and VLAN */
			dev_crit_once(dev->dev,
				      "resources out of limits, transmission may be halted\n");
		}
	}

	return ret;
}

int ksz9477_errata_monitor(struct ksz_device *dev, int port,
			   u64 tx_late_col)
{
	u8 status;
	int ret;

	ret = ksz_pread8(dev, port, REG_PORT_STATUS_0, &status);
	if (ret)
		return ret;

	if (!(FIELD_GET(PORT_INTF_SPEED_MASK, status)
	      == PORT_INTF_SPEED_NONE) &&
	    !(status & PORT_INTF_FULL_DUPLEX)) {
		ret = ksz9477_half_duplex_monitor(dev, port, tx_late_col);
	}

	return ret;
}

void ksz9477_port_init_cnt(struct ksz_device *dev, int port)
{
	struct ksz_port_mib *mib = &dev->ports[port].mib;

	/* flush all enabled port MIB counters */
	mutex_lock(&mib->cnt_mutex);
	ksz_pwrite32(dev, port, REG_PORT_MIB_CTRL_STAT__4,
		     MIB_COUNTER_FLUSH_FREEZE);
	ksz_write8(dev, REG_SW_MAC_CTRL_6, SW_MIB_COUNTER_FLUSH);
	ksz_pwrite32(dev, port, REG_PORT_MIB_CTRL_STAT__4, 0);
	mutex_unlock(&mib->cnt_mutex);
}

static void ksz9477_r_phy_quirks(struct ksz_device *dev, u16 addr, u16 reg,
				 u16 *data)
{
	/* KSZ8563R do not have extended registers but BMSR_ESTATEN and
	 * BMSR_ERCAP bits are set.
	 */
	if (dev->chip_id == KSZ8563_CHIP_ID && reg == MII_BMSR)
		*data &= ~(BMSR_ESTATEN | BMSR_ERCAP);
}

int ksz9477_r_phy(struct ksz_device *dev, u16 addr, u16 reg, u16 *data)
{
	u16 val = 0xffff;
	int ret;

	/* No real PHY after this. Simulate the PHY.
	 * A fixed PHY can be setup in the device tree, but this function is
	 * still called for that port during initialization.
	 * For RGMII PHY there is no way to access it so the fixed PHY should
	 * be used.  For SGMII PHY the supporting code will be added later.
	 */
	if (!dev->info->internal_phy[addr]) {
		struct ksz_port *p = &dev->ports[addr];

		switch (reg) {
		case MII_BMCR:
			val = 0x1140;
			break;
		case MII_BMSR:
			val = 0x796d;
			break;
		case MII_PHYSID1:
			val = 0x0022;
			break;
		case MII_PHYSID2:
			val = 0x1631;
			break;
		case MII_ADVERTISE:
			val = 0x05e1;
			break;
		case MII_LPA:
			val = 0xc5e1;
			break;
		case MII_CTRL1000:
			val = 0x0700;
			break;
		case MII_STAT1000:
			if (p->phydev.speed == SPEED_1000)
				val = 0x3800;
			else
				val = 0;
			break;
		}
	} else {
		ret = ksz_pread16(dev, addr, 0x100 + (reg << 1), &val);
		if (ret)
			return ret;

		ksz9477_r_phy_quirks(dev, addr, reg, &val);
	}

	*data = val;

	return 0;
}

int ksz9477_w_phy(struct ksz_device *dev, u16 addr, u16 reg, u16 val)
{
	u32 mask, val32;

	/* No real PHY after this. */
	if (!dev->info->internal_phy[addr])
		return 0;

	if (reg < 0x10)
		return ksz_pwrite16(dev, addr, 0x100 + (reg << 1), val);

	/* Errata: When using SPI, I2C, or in-band register access,
	 * writes to certain PHY registers should be performed as
	 * 32-bit writes instead of 16-bit writes.
	 */
	val32 = val;
	mask = 0xffff;
	if ((reg & 1) == 0) {
		val32 <<= 16;
		mask <<= 16;
	}
	reg &= ~1;
	return ksz_prmw32(dev, addr, 0x100 + (reg << 1), mask, val32);
}

void ksz9477_cfg_port_member(struct ksz_device *dev, int port, u8 member)
{
	ksz_pwrite32(dev, port, REG_PORT_VLAN_MEMBERSHIP__4, member);
}

void ksz9477_flush_dyn_mac_table(struct ksz_device *dev, int port)
{
	const u16 *regs = dev->info->regs;
	u8 data;

	regmap_update_bits(ksz_regmap_8(dev), REG_SW_LUE_CTRL_2,
			   SW_FLUSH_OPTION_M << SW_FLUSH_OPTION_S,
			   SW_FLUSH_OPTION_DYN_MAC << SW_FLUSH_OPTION_S);

	if (port < dev->info->port_cnt) {
		/* flush individual port */
		ksz_pread8(dev, port, regs[P_STP_CTRL], &data);
		if (!(data & PORT_LEARN_DISABLE))
			ksz_pwrite8(dev, port, regs[P_STP_CTRL],
				    data | PORT_LEARN_DISABLE);
		ksz_cfg(dev, S_FLUSH_TABLE_CTRL, SW_FLUSH_DYN_MAC_TABLE, true);
		ksz_pwrite8(dev, port, regs[P_STP_CTRL], data);
	} else {
		/* flush all */
		ksz_cfg(dev, S_FLUSH_TABLE_CTRL, SW_FLUSH_STP_TABLE, true);
	}
}

int ksz9477_port_vlan_filtering(struct ksz_device *dev, int port,
				bool flag, struct netlink_ext_ack *extack)
{
	if (flag) {
		ksz_port_cfg(dev, port, REG_PORT_LUE_CTRL,
			     PORT_VLAN_LOOKUP_VID_0, true);
		ksz_cfg(dev, REG_SW_LUE_CTRL_0, SW_VLAN_ENABLE, true);
	} else {
		ksz_cfg(dev, REG_SW_LUE_CTRL_0, SW_VLAN_ENABLE, false);
		ksz_port_cfg(dev, port, REG_PORT_LUE_CTRL,
			     PORT_VLAN_LOOKUP_VID_0, false);
	}

	return 0;
}

int ksz9477_port_vlan_add(struct ksz_device *dev, int port,
			  const struct switchdev_obj_port_vlan *vlan,
			  struct netlink_ext_ack *extack)
{
	u32 vlan_table[3];
	bool untagged = vlan->flags & BRIDGE_VLAN_INFO_UNTAGGED;
	int err;

	err = ksz9477_get_vlan_table(dev, vlan->vid, vlan_table);
	if (err) {
		NL_SET_ERR_MSG_MOD(extack, "Failed to get vlan table");
		return err;
	}

	vlan_table[0] = VLAN_VALID | (vlan->vid & VLAN_FID_M);
	if (untagged)
		vlan_table[1] |= BIT(port);
	else
		vlan_table[1] &= ~BIT(port);
	vlan_table[1] &= ~(BIT(dev->cpu_port));

	vlan_table[2] |= BIT(port) | BIT(dev->cpu_port);

	err = ksz9477_set_vlan_table(dev, vlan->vid, vlan_table);
	if (err) {
		NL_SET_ERR_MSG_MOD(extack, "Failed to set vlan table");
		return err;
	}

	/* change PVID */
	if (vlan->flags & BRIDGE_VLAN_INFO_PVID)
		ksz_pwrite16(dev, port, REG_PORT_DEFAULT_VID, vlan->vid);

	return 0;
}

int ksz9477_port_vlan_del(struct ksz_device *dev, int port,
			  const struct switchdev_obj_port_vlan *vlan)
{
	bool untagged = vlan->flags & BRIDGE_VLAN_INFO_UNTAGGED;
	u32 vlan_table[3];
	u16 pvid;

	ksz_pread16(dev, port, REG_PORT_DEFAULT_VID, &pvid);
	pvid = pvid & 0xFFF;

	if (ksz9477_get_vlan_table(dev, vlan->vid, vlan_table)) {
		dev_dbg(dev->dev, "Failed to get vlan table\n");
		return -ETIMEDOUT;
	}

	vlan_table[2] &= ~BIT(port);

	if (pvid == vlan->vid)
		pvid = 1;

	if (untagged)
		vlan_table[1] &= ~BIT(port);

	if (ksz9477_set_vlan_table(dev, vlan->vid, vlan_table)) {
		dev_dbg(dev->dev, "Failed to set vlan table\n");
		return -ETIMEDOUT;
	}

	ksz_pwrite16(dev, port, REG_PORT_DEFAULT_VID, pvid);

	return 0;
}

int ksz9477_fdb_add(struct ksz_device *dev, int port,
		    const unsigned char *addr, u16 vid, struct dsa_db db)
{
	u32 alu_table[4];
	u32 data;
	int ret = 0;

	mutex_lock(&dev->alu_mutex);

	/* find any entry with mac & vid */
	data = vid << ALU_FID_INDEX_S;
	data |= ((addr[0] << 8) | addr[1]);
	ksz_write32(dev, REG_SW_ALU_INDEX_0, data);

	data = ((addr[2] << 24) | (addr[3] << 16));
	data |= ((addr[4] << 8) | addr[5]);
	ksz_write32(dev, REG_SW_ALU_INDEX_1, data);

	/* start read operation */
	ksz_write32(dev, REG_SW_ALU_CTRL__4, ALU_READ | ALU_START);

	/* wait to be finished */
	ret = ksz9477_wait_alu_ready(dev);
	if (ret) {
		dev_dbg(dev->dev, "Failed to read ALU\n");
		goto exit;
	}

	/* read ALU entry */
	ksz9477_read_table(dev, alu_table);

	/* update ALU entry */
	alu_table[0] = ALU_V_STATIC_VALID;
	alu_table[1] |= BIT(port);
	if (vid)
		alu_table[1] |= ALU_V_USE_FID;
	alu_table[2] = (vid << ALU_V_FID_S);
	alu_table[2] |= ((addr[0] << 8) | addr[1]);
	alu_table[3] = ((addr[2] << 24) | (addr[3] << 16));
	alu_table[3] |= ((addr[4] << 8) | addr[5]);

	ksz9477_write_table(dev, alu_table);

	ksz_write32(dev, REG_SW_ALU_CTRL__4, ALU_WRITE | ALU_START);

	/* wait to be finished */
	ret = ksz9477_wait_alu_ready(dev);
	if (ret)
		dev_dbg(dev->dev, "Failed to write ALU\n");

exit:
	mutex_unlock(&dev->alu_mutex);

	return ret;
}

int ksz9477_fdb_del(struct ksz_device *dev, int port,
		    const unsigned char *addr, u16 vid, struct dsa_db db)
{
	u32 alu_table[4];
	u32 data;
	int ret = 0;

	mutex_lock(&dev->alu_mutex);

	/* read any entry with mac & vid */
	data = vid << ALU_FID_INDEX_S;
	data |= ((addr[0] << 8) | addr[1]);
	ksz_write32(dev, REG_SW_ALU_INDEX_0, data);

	data = ((addr[2] << 24) | (addr[3] << 16));
	data |= ((addr[4] << 8) | addr[5]);
	ksz_write32(dev, REG_SW_ALU_INDEX_1, data);

	/* start read operation */
	ksz_write32(dev, REG_SW_ALU_CTRL__4, ALU_READ | ALU_START);

	/* wait to be finished */
	ret = ksz9477_wait_alu_ready(dev);
	if (ret) {
		dev_dbg(dev->dev, "Failed to read ALU\n");
		goto exit;
	}

	ksz_read32(dev, REG_SW_ALU_VAL_A, &alu_table[0]);
	if (alu_table[0] & ALU_V_STATIC_VALID) {
		ksz_read32(dev, REG_SW_ALU_VAL_B, &alu_table[1]);
		ksz_read32(dev, REG_SW_ALU_VAL_C, &alu_table[2]);
		ksz_read32(dev, REG_SW_ALU_VAL_D, &alu_table[3]);

		/* clear forwarding port */
		alu_table[1] &= ~BIT(port);

		/* if there is no port to forward, clear table */
		if ((alu_table[1] & ALU_V_PORT_MAP) == 0) {
			alu_table[0] = 0;
			alu_table[1] = 0;
			alu_table[2] = 0;
			alu_table[3] = 0;
		}
	} else {
		alu_table[0] = 0;
		alu_table[1] = 0;
		alu_table[2] = 0;
		alu_table[3] = 0;
	}

	ksz9477_write_table(dev, alu_table);

	ksz_write32(dev, REG_SW_ALU_CTRL__4, ALU_WRITE | ALU_START);

	/* wait to be finished */
	ret = ksz9477_wait_alu_ready(dev);
	if (ret)
		dev_dbg(dev->dev, "Failed to write ALU\n");

exit:
	mutex_unlock(&dev->alu_mutex);

	return ret;
}

static void ksz9477_convert_alu(struct alu_struct *alu, u32 *alu_table)
{
	alu->is_static = !!(alu_table[0] & ALU_V_STATIC_VALID);
	alu->is_src_filter = !!(alu_table[0] & ALU_V_SRC_FILTER);
	alu->is_dst_filter = !!(alu_table[0] & ALU_V_DST_FILTER);
	alu->prio_age = (alu_table[0] >> ALU_V_PRIO_AGE_CNT_S) &
			ALU_V_PRIO_AGE_CNT_M;
	alu->mstp = alu_table[0] & ALU_V_MSTP_M;

	alu->is_override = !!(alu_table[1] & ALU_V_OVERRIDE);
	alu->is_use_fid = !!(alu_table[1] & ALU_V_USE_FID);
	alu->port_forward = alu_table[1] & ALU_V_PORT_MAP;

	alu->fid = (alu_table[2] >> ALU_V_FID_S) & ALU_V_FID_M;

	alu->mac[0] = (alu_table[2] >> 8) & 0xFF;
	alu->mac[1] = alu_table[2] & 0xFF;
	alu->mac[2] = (alu_table[3] >> 24) & 0xFF;
	alu->mac[3] = (alu_table[3] >> 16) & 0xFF;
	alu->mac[4] = (alu_table[3] >> 8) & 0xFF;
	alu->mac[5] = alu_table[3] & 0xFF;
}

int ksz9477_fdb_dump(struct ksz_device *dev, int port,
		     dsa_fdb_dump_cb_t *cb, void *data)
{
	int ret = 0;
	u32 ksz_data;
	u32 alu_table[4];
	struct alu_struct alu;
	int timeout;

	mutex_lock(&dev->alu_mutex);

	/* start ALU search */
	ksz_write32(dev, REG_SW_ALU_CTRL__4, ALU_START | ALU_SEARCH);

	do {
		timeout = 1000;
		do {
			ksz_read32(dev, REG_SW_ALU_CTRL__4, &ksz_data);
			if ((ksz_data & ALU_VALID) || !(ksz_data & ALU_START))
				break;
			usleep_range(1, 10);
		} while (timeout-- > 0);

		if (!timeout) {
			dev_dbg(dev->dev, "Failed to search ALU\n");
			ret = -ETIMEDOUT;
			goto exit;
		}

		if (!(ksz_data & ALU_VALID))
			continue;

		/* read ALU table */
		ksz9477_read_table(dev, alu_table);

		ksz9477_convert_alu(&alu, alu_table);

		if (alu.port_forward & BIT(port)) {
			ret = cb(alu.mac, alu.fid, alu.is_static, data);
			if (ret)
				goto exit;
		}
	} while (ksz_data & ALU_START);

exit:

	/* stop ALU search */
	ksz_write32(dev, REG_SW_ALU_CTRL__4, 0);

	mutex_unlock(&dev->alu_mutex);

	return ret;
}

int ksz9477_mdb_add(struct ksz_device *dev, int port,
		    const struct switchdev_obj_port_mdb *mdb, struct dsa_db db)
{
	u32 static_table[4];
	const u8 *shifts;
	const u32 *masks;
	u32 data;
	int index;
	u32 mac_hi, mac_lo;
	int err = 0;

	shifts = dev->info->shifts;
	masks = dev->info->masks;

	mac_hi = ((mdb->addr[0] << 8) | mdb->addr[1]);
	mac_lo = ((mdb->addr[2] << 24) | (mdb->addr[3] << 16));
	mac_lo |= ((mdb->addr[4] << 8) | mdb->addr[5]);

	mutex_lock(&dev->alu_mutex);

	for (index = 0; index < dev->info->num_statics; index++) {
		/* find empty slot first */
		data = (index << shifts[ALU_STAT_INDEX]) |
			masks[ALU_STAT_READ] | ALU_STAT_START;
		ksz_write32(dev, REG_SW_ALU_STAT_CTRL__4, data);

		/* wait to be finished */
		err = ksz9477_wait_alu_sta_ready(dev);
		if (err) {
			dev_dbg(dev->dev, "Failed to read ALU STATIC\n");
			goto exit;
		}

		/* read ALU static table */
		ksz9477_read_table(dev, static_table);

		if (static_table[0] & ALU_V_STATIC_VALID) {
			/* check this has same vid & mac address */
			if (((static_table[2] >> ALU_V_FID_S) == mdb->vid) &&
			    ((static_table[2] & ALU_V_MAC_ADDR_HI) == mac_hi) &&
			    static_table[3] == mac_lo) {
				/* found matching one */
				break;
			}
		} else {
			/* found empty one */
			break;
		}
	}

	/* no available entry */
	if (index == dev->info->num_statics) {
		err = -ENOSPC;
		goto exit;
	}

	/* add entry */
	static_table[0] = ALU_V_STATIC_VALID;
	static_table[1] |= BIT(port);
	if (mdb->vid)
		static_table[1] |= ALU_V_USE_FID;
	static_table[2] = (mdb->vid << ALU_V_FID_S);
	static_table[2] |= mac_hi;
	static_table[3] = mac_lo;

	ksz9477_write_table(dev, static_table);

	data = (index << shifts[ALU_STAT_INDEX]) | ALU_STAT_START;
	ksz_write32(dev, REG_SW_ALU_STAT_CTRL__4, data);

	/* wait to be finished */
	if (ksz9477_wait_alu_sta_ready(dev))
		dev_dbg(dev->dev, "Failed to read ALU STATIC\n");

exit:
	mutex_unlock(&dev->alu_mutex);
	return err;
}

int ksz9477_mdb_del(struct ksz_device *dev, int port,
		    const struct switchdev_obj_port_mdb *mdb, struct dsa_db db)
{
	u32 static_table[4];
	const u8 *shifts;
	const u32 *masks;
	u32 data;
	int index;
	int ret = 0;
	u32 mac_hi, mac_lo;

	shifts = dev->info->shifts;
	masks = dev->info->masks;

	mac_hi = ((mdb->addr[0] << 8) | mdb->addr[1]);
	mac_lo = ((mdb->addr[2] << 24) | (mdb->addr[3] << 16));
	mac_lo |= ((mdb->addr[4] << 8) | mdb->addr[5]);

	mutex_lock(&dev->alu_mutex);

	for (index = 0; index < dev->info->num_statics; index++) {
		/* find empty slot first */
		data = (index << shifts[ALU_STAT_INDEX]) |
			masks[ALU_STAT_READ] | ALU_STAT_START;
		ksz_write32(dev, REG_SW_ALU_STAT_CTRL__4, data);

		/* wait to be finished */
		ret = ksz9477_wait_alu_sta_ready(dev);
		if (ret) {
			dev_dbg(dev->dev, "Failed to read ALU STATIC\n");
			goto exit;
		}

		/* read ALU static table */
		ksz9477_read_table(dev, static_table);

		if (static_table[0] & ALU_V_STATIC_VALID) {
			/* check this has same vid & mac address */

			if (((static_table[2] >> ALU_V_FID_S) == mdb->vid) &&
			    ((static_table[2] & ALU_V_MAC_ADDR_HI) == mac_hi) &&
			    static_table[3] == mac_lo) {
				/* found matching one */
				break;
			}
		}
	}

	/* no available entry */
	if (index == dev->info->num_statics)
		goto exit;

	/* clear port */
	static_table[1] &= ~BIT(port);

	if ((static_table[1] & ALU_V_PORT_MAP) == 0) {
		/* delete entry */
		static_table[0] = 0;
		static_table[1] = 0;
		static_table[2] = 0;
		static_table[3] = 0;
	}

	ksz9477_write_table(dev, static_table);

	data = (index << shifts[ALU_STAT_INDEX]) | ALU_STAT_START;
	ksz_write32(dev, REG_SW_ALU_STAT_CTRL__4, data);

	/* wait to be finished */
	ret = ksz9477_wait_alu_sta_ready(dev);
	if (ret)
		dev_dbg(dev->dev, "Failed to read ALU STATIC\n");

exit:
	mutex_unlock(&dev->alu_mutex);

	return ret;
}

int ksz9477_port_mirror_add(struct ksz_device *dev, int port,
			    struct dsa_mall_mirror_tc_entry *mirror,
			    bool ingress, struct netlink_ext_ack *extack)
{
	u8 data;
	int p;

	/* Limit to one sniffer port
	 * Check if any of the port is already set for sniffing
	 * If yes, instruct the user to remove the previous entry & exit
	 */
	for (p = 0; p < dev->info->port_cnt; p++) {
		/* Skip the current sniffing port */
		if (p == mirror->to_local_port)
			continue;

		ksz_pread8(dev, p, P_MIRROR_CTRL, &data);

		if (data & PORT_MIRROR_SNIFFER) {
			NL_SET_ERR_MSG_MOD(extack,
					   "Sniffer port is already configured, delete existing rules & retry");
			return -EBUSY;
		}
	}

	if (ingress)
		ksz_port_cfg(dev, port, P_MIRROR_CTRL, PORT_MIRROR_RX, true);
	else
		ksz_port_cfg(dev, port, P_MIRROR_CTRL, PORT_MIRROR_TX, true);

	/* configure mirror port */
	ksz_port_cfg(dev, mirror->to_local_port, P_MIRROR_CTRL,
		     PORT_MIRROR_SNIFFER, true);

	ksz_cfg(dev, S_MIRROR_CTRL, SW_MIRROR_RX_TX, false);

	return 0;
}

void ksz9477_port_mirror_del(struct ksz_device *dev, int port,
			     struct dsa_mall_mirror_tc_entry *mirror)
{
	bool in_use = false;
	u8 data;
	int p;

	if (mirror->ingress)
		ksz_port_cfg(dev, port, P_MIRROR_CTRL, PORT_MIRROR_RX, false);
	else
		ksz_port_cfg(dev, port, P_MIRROR_CTRL, PORT_MIRROR_TX, false);


	/* Check if any of the port is still referring to sniffer port */
	for (p = 0; p < dev->info->port_cnt; p++) {
		ksz_pread8(dev, p, P_MIRROR_CTRL, &data);

		if ((data & (PORT_MIRROR_RX | PORT_MIRROR_TX))) {
			in_use = true;
			break;
		}
	}

	/* delete sniffing if there are no other mirroring rules */
	if (!in_use)
		ksz_port_cfg(dev, mirror->to_local_port, P_MIRROR_CTRL,
			     PORT_MIRROR_SNIFFER, false);
}

static phy_interface_t ksz9477_get_interface(struct ksz_device *dev, int port)
{
	phy_interface_t interface;
	bool gbit;

	if (dev->info->internal_phy[port])
		return PHY_INTERFACE_MODE_NA;

	gbit = ksz_get_gbit(dev, port);

	interface = ksz_get_xmii(dev, port, gbit);

	return interface;
}

void ksz9477_get_caps(struct ksz_device *dev, int port,
		      struct phylink_config *config)
{
	config->mac_capabilities = MAC_10 | MAC_100 | MAC_ASYM_PAUSE |
				   MAC_SYM_PAUSE;

	if (dev->info->gbit_capable[port])
		config->mac_capabilities |= MAC_1000FD;
}

int ksz9477_set_ageing_time(struct ksz_device *dev, unsigned int msecs)
{
	u32 secs = msecs / 1000;
	u8 value;
	u8 data;
	int ret;

	value = FIELD_GET(SW_AGE_PERIOD_7_0_M, secs);

	ret = ksz_write8(dev, REG_SW_LUE_CTRL_3, value);
	if (ret < 0)
		return ret;

	data = FIELD_GET(SW_AGE_PERIOD_10_8_M, secs);

	ret = ksz_read8(dev, REG_SW_LUE_CTRL_0, &value);
	if (ret < 0)
		return ret;

	value &= ~SW_AGE_CNT_M;
	value |= FIELD_PREP(SW_AGE_CNT_M, data);

	return ksz_write8(dev, REG_SW_LUE_CTRL_0, value);
}

void ksz9477_port_queue_split(struct ksz_device *dev, int port)
{
	u8 data;

	if (dev->info->num_tx_queues == 8)
		data = PORT_EIGHT_QUEUE;
	else if (dev->info->num_tx_queues == 4)
		data = PORT_FOUR_QUEUE;
	else if (dev->info->num_tx_queues == 2)
		data = PORT_TWO_QUEUE;
	else
		data = PORT_SINGLE_QUEUE;

	ksz_prmw8(dev, port, REG_PORT_CTRL_0, PORT_QUEUE_SPLIT_MASK, data);
}

void ksz9477_port_setup(struct ksz_device *dev, int port, bool cpu_port)
{
	const u16 *regs = dev->info->regs;
	struct dsa_switch *ds = dev->ds;
	u16 data16;
	u8 member;

	/* enable tag tail for host port */
	if (cpu_port)
		ksz_port_cfg(dev, port, REG_PORT_CTRL_0, PORT_TAIL_TAG_ENABLE,
			     true);

	ksz9477_port_queue_split(dev, port);

	ksz_port_cfg(dev, port, REG_PORT_CTRL_0, PORT_MAC_LOOPBACK, false);

	/* set back pressure */
	ksz_port_cfg(dev, port, REG_PORT_MAC_CTRL_1, PORT_BACK_PRESSURE, true);

	/* enable broadcast storm limit */
	ksz_port_cfg(dev, port, P_BCAST_STORM_CTRL, PORT_BROADCAST_STORM, true);

	/* replace priority */
	ksz_port_cfg(dev, port, REG_PORT_MRI_MAC_CTRL, PORT_USER_PRIO_CEILING,
		     false);
	ksz9477_port_cfg32(dev, port, REG_PORT_MTI_QUEUE_CTRL_0__4,
			   MTI_PVID_REPLACE, false);

	/* force flow control for non-PHY ports only */
	ksz_port_cfg(dev, port, REG_PORT_CTRL_0,
		     PORT_FORCE_TX_FLOW_CTRL | PORT_FORCE_RX_FLOW_CTRL,
		     !dev->info->internal_phy[port]);

	if (cpu_port)
		member = dsa_user_ports(ds);
	else
		member = BIT(dsa_upstream_port(ds, port));

	ksz9477_cfg_port_member(dev, port, member);

	/* clear pending interrupts */
	if (dev->info->internal_phy[port])
		ksz_pread16(dev, port, REG_PORT_PHY_INT_ENABLE, &data16);

	ksz9477_port_acl_init(dev, port);

	/* clear pending wake flags */
	ksz_handle_wake_reason(dev, port);

	/* Disable all WoL options by default. Otherwise
	 * ksz_switch_macaddr_get/put logic will not work properly.
	 */
	ksz_pwrite8(dev, port, regs[REG_PORT_PME_CTRL], 0);
}

void ksz9477_config_cpu_port(struct dsa_switch *ds)
{
	struct ksz_device *dev = ds->priv;
	struct ksz_port *p;
	int i;

	for (i = 0; i < dev->info->port_cnt; i++) {
		if (dsa_is_cpu_port(ds, i) &&
		    (dev->info->cpu_ports & (1 << i))) {
			phy_interface_t interface;
			const char *prev_msg;
			const char *prev_mode;

			dev->cpu_port = i;
			p = &dev->ports[i];

			/* Read from XMII register to determine host port
			 * interface.  If set specifically in device tree
			 * note the difference to help debugging.
			 */
			interface = ksz9477_get_interface(dev, i);
			if (!p->interface) {
				if (dev->compat_interface) {
					dev_warn(dev->dev,
						 "Using legacy switch \"phy-mode\" property, because it is missing on port %d node. "
						 "Please update your device tree.\n",
						 i);
					p->interface = dev->compat_interface;
				} else {
					p->interface = interface;
				}
			}
			if (interface && interface != p->interface) {
				prev_msg = " instead of ";
				prev_mode = phy_modes(interface);
			} else {
				prev_msg = "";
				prev_mode = "";
			}
			dev_info(dev->dev,
				 "Port%d: using phy mode %s%s%s\n",
				 i,
				 phy_modes(p->interface),
				 prev_msg,
				 prev_mode);

			/* enable cpu port */
			ksz9477_port_setup(dev, i, true);
		}
	}

	for (i = 0; i < dev->info->port_cnt; i++) {
		if (i == dev->cpu_port)
			continue;
		ksz_port_stp_state_set(ds, i, BR_STATE_DISABLED);

		/* Power down the internal PHY if port is unused. */
		if (dsa_is_unused_port(ds, i) && dev->info->internal_phy[i])
			ksz_pwrite16(dev, i, 0x100, BMCR_PDOWN);
	}
}

int ksz9477_enable_stp_addr(struct ksz_device *dev)
{
	const u32 *masks;
	u32 data;
	int ret;

	masks = dev->info->masks;

	/* Enable Reserved multicast table */
	ksz_cfg(dev, REG_SW_LUE_CTRL_0, SW_RESV_MCAST_ENABLE, true);

	/* Set the Override bit for forwarding BPDU packet to CPU */
	ret = ksz_write32(dev, REG_SW_ALU_VAL_B,
			  ALU_V_OVERRIDE | BIT(dev->cpu_port));
	if (ret < 0)
		return ret;

	data = ALU_STAT_START | ALU_RESV_MCAST_ADDR | masks[ALU_STAT_WRITE];

	ret = ksz_write32(dev, REG_SW_ALU_STAT_CTRL__4, data);
	if (ret < 0)
		return ret;

	/* wait to be finished */
	ret = ksz9477_wait_alu_sta_ready(dev);
	if (ret < 0) {
		dev_err(dev->dev, "Failed to update Reserved Multicast table\n");
		return ret;
	}

	return 0;
}

int ksz9477_setup(struct dsa_switch *ds)
{
	struct ksz_device *dev = ds->priv;
	const u16 *regs = dev->info->regs;
	int ret = 0;

	ds->mtu_enforcement_ingress = true;

	/* Required for port partitioning. */
	ksz9477_cfg32(dev, REG_SW_QM_CTRL__4, UNICAST_VLAN_BOUNDARY,
		      true);

	/* Do not work correctly with tail tagging. */
	ksz_cfg(dev, REG_SW_MAC_CTRL_0, SW_CHECK_LENGTH, false);

	/* Enable REG_SW_MTU__2 reg by setting SW_JUMBO_PACKET */
	ksz_cfg(dev, REG_SW_MAC_CTRL_1, SW_JUMBO_PACKET, true);

	/* Use collision based back pressure mode. */
	ksz_cfg(dev, REG_SW_MAC_CTRL_1, SW_BACK_PRESSURE,
		SW_BACK_PRESSURE_COLLISION);

	/* Now we can configure default MTU value */
	ret = regmap_update_bits(ksz_regmap_16(dev), REG_SW_MTU__2, REG_SW_MTU_MASK,
				 VLAN_ETH_FRAME_LEN + ETH_FCS_LEN);
	if (ret)
		return ret;

	/* queue based egress rate limit */
	ksz_cfg(dev, REG_SW_MAC_CTRL_5, SW_OUT_RATE_LIMIT_QUEUE_BASED, true);

	/* enable global MIB counter freeze function */
	ksz_cfg(dev, REG_SW_MAC_CTRL_6, SW_MIB_COUNTER_FREEZE, true);

	/* Make sure PME (WoL) is not enabled. If requested, it will
	 * be enabled by ksz_wol_pre_shutdown(). Otherwise, some PMICs
	 * do not like PME events changes before shutdown.
	 */
	return ksz_write8(dev, regs[REG_SW_PME_CTRL], 0);
}

u32 ksz9477_get_port_addr(int port, int offset)
{
	return PORT_CTRL_ADDR(port, offset);
}

int ksz9477_tc_cbs_set_cinc(struct ksz_device *dev, int port, u32 val)
{
	val = val >> 8;

	return ksz_pwrite16(dev, port, REG_PORT_MTI_CREDIT_INCREMENT, val);
}

/* The KSZ9477 provides following HW features to accelerate
 * HSR frames handling:
 *
 * 1. TX PACKET DUPLICATION FROM HOST TO SWITCH
 * 2. RX PACKET DUPLICATION DISCARDING
 * 3. PREVENTING PACKET LOOP IN THE RING BY SELF-ADDRESS FILTERING
 *
 * Only one from point 1. has the NETIF_F* flag available.
 *
 * Ones from point 2 and 3 are "best effort" - i.e. those will
 * work correctly most of the time, but it may happen that some
 * frames will not be caught - to be more specific; there is a race
 * condition in hardware such that, when duplicate packets are received
 * on member ports very close in time to each other, the hardware fails
 * to detect that they are duplicates.
 *
 * Hence, the SW needs to handle those special cases. However, the speed
 * up gain is considerable when above features are used.
 *
 * Moreover, the NETIF_F_HW_HSR_FWD feature is also enabled, as HSR frames
 * can be forwarded in the switch fabric between HSR ports.
 */
#define KSZ9477_SUPPORTED_HSR_FEATURES (NETIF_F_HW_HSR_DUP | NETIF_F_HW_HSR_FWD)

void ksz9477_hsr_join(struct dsa_switch *ds, int port, struct net_device *hsr)
{
	struct ksz_device *dev = ds->priv;
	struct net_device *user;
	struct dsa_port *hsr_dp;
	u8 data, hsr_ports = 0;

	/* Program which port(s) shall support HSR */
	ksz_rmw32(dev, REG_HSR_PORT_MAP__4, BIT(port), BIT(port));

	/* Forward frames between HSR ports (i.e. bridge together HSR ports) */
	if (dev->hsr_ports) {
		dsa_hsr_foreach_port(hsr_dp, ds, hsr)
			hsr_ports |= BIT(hsr_dp->index);

		hsr_ports |= BIT(dsa_upstream_port(ds, port));
		dsa_hsr_foreach_port(hsr_dp, ds, hsr)
			ksz9477_cfg_port_member(dev, hsr_dp->index, hsr_ports);
	}

	if (!dev->hsr_ports) {
		/* Enable discarding of received HSR frames */
		ksz_read8(dev, REG_HSR_ALU_CTRL_0__1, &data);
		data |= HSR_DUPLICATE_DISCARD;
		data &= ~HSR_NODE_UNICAST;
		ksz_write8(dev, REG_HSR_ALU_CTRL_0__1, data);
	}

	/* Enable per port self-address filtering.
	 * The global self-address filtering has already been enabled in the
	 * ksz9477_reset_switch() function.
	 */
	ksz_port_cfg(dev, port, REG_PORT_LUE_CTRL, PORT_SRC_ADDR_FILTER, true);

	/* Setup HW supported features for lan HSR ports */
	user = dsa_to_port(ds, port)->user;
	user->features |= KSZ9477_SUPPORTED_HSR_FEATURES;
}

void ksz9477_hsr_leave(struct dsa_switch *ds, int port, struct net_device *hsr)
{
	struct ksz_device *dev = ds->priv;

	/* Clear port HSR support */
	ksz_rmw32(dev, REG_HSR_PORT_MAP__4, BIT(port), 0);

	/* Disable forwarding frames between HSR ports */
	ksz9477_cfg_port_member(dev, port, BIT(dsa_upstream_port(ds, port)));

	/* Disable per port self-address filtering */
	ksz_port_cfg(dev, port, REG_PORT_LUE_CTRL, PORT_SRC_ADDR_FILTER, false);
}

int ksz9477_switch_init(struct ksz_device *dev)
{
	u8 data8;
	int ret;

	dev->port_mask = (1 << dev->info->port_cnt) - 1;

	/* turn off SPI DO Edge select */
	ret = ksz_read8(dev, REG_SW_GLOBAL_SERIAL_CTRL_0, &data8);
	if (ret)
		return ret;

	data8 &= ~SPI_AUTO_EDGE_DETECTION;
	ret = ksz_write8(dev, REG_SW_GLOBAL_SERIAL_CTRL_0, data8);
	if (ret)
		return ret;

	return 0;
}

void ksz9477_switch_exit(struct ksz_device *dev)
{
	ksz9477_reset_switch(dev);
}

MODULE_AUTHOR("Woojung Huh <Woojung.Huh@microchip.com>");
MODULE_DESCRIPTION("Microchip KSZ9477 Series Switch DSA Driver");
MODULE_LICENSE("GPL");
