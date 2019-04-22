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
#include <net/dsa.h>
#include <net/switchdev.h>

#include "ksz_priv.h"
#include "ksz9477_reg.h"
#include "ksz_common.h"

/* Used with variable features to indicate capabilities. */
#define GBIT_SUPPORT			BIT(0)
#define NEW_XMII			BIT(1)
#define IS_9893				BIT(2)

static const struct {
	int index;
	char string[ETH_GSTRING_LEN];
} ksz9477_mib_names[TOTAL_SWITCH_COUNTER_NUM] = {
	{ 0x00, "rx_hi" },
	{ 0x01, "rx_undersize" },
	{ 0x02, "rx_fragments" },
	{ 0x03, "rx_oversize" },
	{ 0x04, "rx_jabbers" },
	{ 0x05, "rx_symbol_err" },
	{ 0x06, "rx_crc_err" },
	{ 0x07, "rx_align_err" },
	{ 0x08, "rx_mac_ctrl" },
	{ 0x09, "rx_pause" },
	{ 0x0A, "rx_bcast" },
	{ 0x0B, "rx_mcast" },
	{ 0x0C, "rx_ucast" },
	{ 0x0D, "rx_64_or_less" },
	{ 0x0E, "rx_65_127" },
	{ 0x0F, "rx_128_255" },
	{ 0x10, "rx_256_511" },
	{ 0x11, "rx_512_1023" },
	{ 0x12, "rx_1024_1522" },
	{ 0x13, "rx_1523_2000" },
	{ 0x14, "rx_2001" },
	{ 0x15, "tx_hi" },
	{ 0x16, "tx_late_col" },
	{ 0x17, "tx_pause" },
	{ 0x18, "tx_bcast" },
	{ 0x19, "tx_mcast" },
	{ 0x1A, "tx_ucast" },
	{ 0x1B, "tx_deferred" },
	{ 0x1C, "tx_total_col" },
	{ 0x1D, "tx_exc_col" },
	{ 0x1E, "tx_single_col" },
	{ 0x1F, "tx_mult_col" },
	{ 0x80, "rx_total" },
	{ 0x81, "tx_total" },
	{ 0x82, "rx_discards" },
	{ 0x83, "tx_discards" },
};

static void ksz9477_cfg32(struct ksz_device *dev, u32 addr, u32 bits, bool set)
{
	u32 data;

	ksz_read32(dev, addr, &data);
	if (set)
		data |= bits;
	else
		data &= ~bits;
	ksz_write32(dev, addr, data);
}

static void ksz9477_port_cfg32(struct ksz_device *dev, int port, int offset,
			       u32 bits, bool set)
{
	u32 addr;
	u32 data;

	addr = PORT_CTRL_ADDR(port, offset);
	ksz_read32(dev, addr, &data);

	if (set)
		data |= bits;
	else
		data &= ~bits;

	ksz_write32(dev, addr, data);
}

static int ksz9477_wait_vlan_ctrl_ready(struct ksz_device *dev, u32 waiton,
					int timeout)
{
	u8 data;

	do {
		ksz_read8(dev, REG_SW_VLAN_CTRL, &data);
		if (!(data & waiton))
			break;
		usleep_range(1, 10);
	} while (timeout-- > 0);

	if (timeout <= 0)
		return -ETIMEDOUT;

	return 0;
}

static int ksz9477_get_vlan_table(struct ksz_device *dev, u16 vid,
				  u32 *vlan_table)
{
	int ret;

	mutex_lock(&dev->vlan_mutex);

	ksz_write16(dev, REG_SW_VLAN_ENTRY_INDEX__2, vid & VLAN_INDEX_M);
	ksz_write8(dev, REG_SW_VLAN_CTRL, VLAN_READ | VLAN_START);

	/* wait to be cleared */
	ret = ksz9477_wait_vlan_ctrl_ready(dev, VLAN_START, 1000);
	if (ret < 0) {
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
	ret = ksz9477_wait_vlan_ctrl_ready(dev, VLAN_START, 1000);
	if (ret < 0) {
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

static int ksz9477_wait_alu_ready(struct ksz_device *dev, u32 waiton,
				  int timeout)
{
	u32 data;

	do {
		ksz_read32(dev, REG_SW_ALU_CTRL__4, &data);
		if (!(data & waiton))
			break;
		usleep_range(1, 10);
	} while (timeout-- > 0);

	if (timeout <= 0)
		return -ETIMEDOUT;

	return 0;
}

static int ksz9477_wait_alu_sta_ready(struct ksz_device *dev, u32 waiton,
				      int timeout)
{
	u32 data;

	do {
		ksz_read32(dev, REG_SW_ALU_STAT_CTRL__4, &data);
		if (!(data & waiton))
			break;
		usleep_range(1, 10);
	} while (timeout-- > 0);

	if (timeout <= 0)
		return -ETIMEDOUT;

	return 0;
}

static int ksz9477_reset_switch(struct ksz_device *dev)
{
	u8 data8;
	u16 data16;
	u32 data32;

	/* reset switch */
	ksz_cfg(dev, REG_SW_OPERATION, SW_RESET, true);

	/* turn off SPI DO Edge select */
	ksz_read8(dev, REG_SW_GLOBAL_SERIAL_CTRL_0, &data8);
	data8 &= ~SPI_AUTO_EDGE_DETECTION;
	ksz_write8(dev, REG_SW_GLOBAL_SERIAL_CTRL_0, data8);

	/* default configuration */
	ksz_read8(dev, REG_SW_LUE_CTRL_1, &data8);
	data8 = SW_AGING_ENABLE | SW_LINK_AUTO_AGING |
	      SW_SRC_ADDR_FILTER | SW_FLUSH_STP_TABLE | SW_FLUSH_MSTP_TABLE;
	ksz_write8(dev, REG_SW_LUE_CTRL_1, data8);

	/* disable interrupts */
	ksz_write32(dev, REG_SW_INT_MASK__4, SWITCH_INT_MASK);
	ksz_write32(dev, REG_SW_PORT_INT_MASK__4, 0x7F);
	ksz_read32(dev, REG_SW_PORT_INT_STATUS__4, &data32);

	/* set broadcast storm protection 10% rate */
	ksz_read16(dev, REG_SW_MAC_CTRL_2, &data16);
	data16 &= ~BROADCAST_STORM_RATE;
	data16 |= (BROADCAST_STORM_VALUE * BROADCAST_STORM_PROT_RATE) / 100;
	ksz_write16(dev, REG_SW_MAC_CTRL_2, data16);

	return 0;
}

static void ksz9477_r_mib_cnt(struct ksz_device *dev, int port, u16 addr,
			      u64 *cnt)
{
	struct ksz_poll_ctx ctx = {
		.dev = dev,
		.port = port,
		.offset = REG_PORT_MIB_CTRL_STAT__4,
	};
	struct ksz_port *p = &dev->ports[port];
	u32 data;
	int ret;

	/* retain the flush/freeze bit */
	data = p->freeze ? MIB_COUNTER_FLUSH_FREEZE : 0;
	data |= MIB_COUNTER_READ;
	data |= (addr << MIB_COUNTER_INDEX_S);
	ksz_pwrite32(dev, port, REG_PORT_MIB_CTRL_STAT__4, data);

	ret = readx_poll_timeout(ksz_pread32_poll, &ctx, data,
				 !(data & MIB_COUNTER_READ), 10, 1000);

	/* failed to read MIB. get out of loop */
	if (ret < 0) {
		dev_dbg(dev->dev, "Failed to get MIB\n");
		return;
	}

	/* count resets upon read */
	ksz_pread32(dev, port, REG_PORT_MIB_DATA, &data);
	*cnt += data;
}

static void ksz9477_r_mib_pkt(struct ksz_device *dev, int port, u16 addr,
			      u64 *dropped, u64 *cnt)
{
	addr = ksz9477_mib_names[addr].index;
	ksz9477_r_mib_cnt(dev, port, addr, cnt);
}

static void ksz9477_freeze_mib(struct ksz_device *dev, int port, bool freeze)
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

static void ksz9477_port_init_cnt(struct ksz_device *dev, int port)
{
	struct ksz_port_mib *mib = &dev->ports[port].mib;

	/* flush all enabled port MIB counters */
	mutex_lock(&mib->cnt_mutex);
	ksz_pwrite32(dev, port, REG_PORT_MIB_CTRL_STAT__4,
		     MIB_COUNTER_FLUSH_FREEZE);
	ksz_write8(dev, REG_SW_MAC_CTRL_6, SW_MIB_COUNTER_FLUSH);
	ksz_pwrite32(dev, port, REG_PORT_MIB_CTRL_STAT__4, 0);
	mutex_unlock(&mib->cnt_mutex);

	mib->cnt_ptr = 0;
	memset(mib->counters, 0, dev->mib_cnt * sizeof(u64));
}

static enum dsa_tag_protocol ksz9477_get_tag_protocol(struct dsa_switch *ds,
						      int port)
{
	enum dsa_tag_protocol proto = DSA_TAG_PROTO_KSZ9477;
	struct ksz_device *dev = ds->priv;

	if (dev->features & IS_9893)
		proto = DSA_TAG_PROTO_KSZ9893;
	return proto;
}

static int ksz9477_phy_read16(struct dsa_switch *ds, int addr, int reg)
{
	struct ksz_device *dev = ds->priv;
	u16 val = 0xffff;

	/* No real PHY after this. Simulate the PHY.
	 * A fixed PHY can be setup in the device tree, but this function is
	 * still called for that port during initialization.
	 * For RGMII PHY there is no way to access it so the fixed PHY should
	 * be used.  For SGMII PHY the supporting code will be added later.
	 */
	if (addr >= dev->phy_port_cnt) {
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
		ksz_pread16(dev, addr, 0x100 + (reg << 1), &val);
	}

	return val;
}

static int ksz9477_phy_write16(struct dsa_switch *ds, int addr, int reg,
			       u16 val)
{
	struct ksz_device *dev = ds->priv;

	/* No real PHY after this. */
	if (addr >= dev->phy_port_cnt)
		return 0;

	/* No gigabit support.  Do not write to this register. */
	if (!(dev->features & GBIT_SUPPORT) && reg == MII_CTRL1000)
		return 0;
	ksz_pwrite16(dev, addr, 0x100 + (reg << 1), val);

	return 0;
}

static void ksz9477_get_strings(struct dsa_switch *ds, int port,
				u32 stringset, uint8_t *buf)
{
	int i;

	if (stringset != ETH_SS_STATS)
		return;

	for (i = 0; i < TOTAL_SWITCH_COUNTER_NUM; i++) {
		memcpy(buf + i * ETH_GSTRING_LEN, ksz9477_mib_names[i].string,
		       ETH_GSTRING_LEN);
	}
}

static void ksz9477_cfg_port_member(struct ksz_device *dev, int port,
				    u8 member)
{
	ksz_pwrite32(dev, port, REG_PORT_VLAN_MEMBERSHIP__4, member);
	dev->ports[port].member = member;
}

static void ksz9477_port_stp_state_set(struct dsa_switch *ds, int port,
				       u8 state)
{
	struct ksz_device *dev = ds->priv;
	struct ksz_port *p = &dev->ports[port];
	u8 data;
	int member = -1;
	int forward = dev->member;

	ksz_pread8(dev, port, P_STP_CTRL, &data);
	data &= ~(PORT_TX_ENABLE | PORT_RX_ENABLE | PORT_LEARN_DISABLE);

	switch (state) {
	case BR_STATE_DISABLED:
		data |= PORT_LEARN_DISABLE;
		if (port != dev->cpu_port)
			member = 0;
		break;
	case BR_STATE_LISTENING:
		data |= (PORT_RX_ENABLE | PORT_LEARN_DISABLE);
		if (port != dev->cpu_port &&
		    p->stp_state == BR_STATE_DISABLED)
			member = dev->host_mask | p->vid_member;
		break;
	case BR_STATE_LEARNING:
		data |= PORT_RX_ENABLE;
		break;
	case BR_STATE_FORWARDING:
		data |= (PORT_TX_ENABLE | PORT_RX_ENABLE);

		/* This function is also used internally. */
		if (port == dev->cpu_port)
			break;

		member = dev->host_mask | p->vid_member;
		mutex_lock(&dev->dev_mutex);

		/* Port is a member of a bridge. */
		if (dev->br_member & (1 << port)) {
			dev->member |= (1 << port);
			member = dev->member;
		}
		mutex_unlock(&dev->dev_mutex);
		break;
	case BR_STATE_BLOCKING:
		data |= PORT_LEARN_DISABLE;
		if (port != dev->cpu_port &&
		    p->stp_state == BR_STATE_DISABLED)
			member = dev->host_mask | p->vid_member;
		break;
	default:
		dev_err(ds->dev, "invalid STP state: %d\n", state);
		return;
	}

	ksz_pwrite8(dev, port, P_STP_CTRL, data);
	p->stp_state = state;
	mutex_lock(&dev->dev_mutex);
	if (data & PORT_RX_ENABLE)
		dev->rx_ports |= (1 << port);
	else
		dev->rx_ports &= ~(1 << port);
	if (data & PORT_TX_ENABLE)
		dev->tx_ports |= (1 << port);
	else
		dev->tx_ports &= ~(1 << port);

	/* Port membership may share register with STP state. */
	if (member >= 0 && member != p->member)
		ksz9477_cfg_port_member(dev, port, (u8)member);

	/* Check if forwarding needs to be updated. */
	if (state != BR_STATE_FORWARDING) {
		if (dev->br_member & (1 << port))
			dev->member &= ~(1 << port);
	}

	/* When topology has changed the function ksz_update_port_member
	 * should be called to modify port forwarding behavior.
	 */
	if (forward != dev->member)
		ksz_update_port_member(dev, port);
	mutex_unlock(&dev->dev_mutex);
}

static void ksz9477_flush_dyn_mac_table(struct ksz_device *dev, int port)
{
	u8 data;

	ksz_read8(dev, REG_SW_LUE_CTRL_2, &data);
	data &= ~(SW_FLUSH_OPTION_M << SW_FLUSH_OPTION_S);
	data |= (SW_FLUSH_OPTION_DYN_MAC << SW_FLUSH_OPTION_S);
	ksz_write8(dev, REG_SW_LUE_CTRL_2, data);
	if (port < dev->mib_port_cnt) {
		/* flush individual port */
		ksz_pread8(dev, port, P_STP_CTRL, &data);
		if (!(data & PORT_LEARN_DISABLE))
			ksz_pwrite8(dev, port, P_STP_CTRL,
				    data | PORT_LEARN_DISABLE);
		ksz_cfg(dev, S_FLUSH_TABLE_CTRL, SW_FLUSH_DYN_MAC_TABLE, true);
		ksz_pwrite8(dev, port, P_STP_CTRL, data);
	} else {
		/* flush all */
		ksz_cfg(dev, S_FLUSH_TABLE_CTRL, SW_FLUSH_STP_TABLE, true);
	}
}

static int ksz9477_port_vlan_filtering(struct dsa_switch *ds, int port,
				       bool flag)
{
	struct ksz_device *dev = ds->priv;

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

static void ksz9477_port_vlan_add(struct dsa_switch *ds, int port,
				  const struct switchdev_obj_port_vlan *vlan)
{
	struct ksz_device *dev = ds->priv;
	u32 vlan_table[3];
	u16 vid;
	bool untagged = vlan->flags & BRIDGE_VLAN_INFO_UNTAGGED;

	for (vid = vlan->vid_begin; vid <= vlan->vid_end; vid++) {
		if (ksz9477_get_vlan_table(dev, vid, vlan_table)) {
			dev_dbg(dev->dev, "Failed to get vlan table\n");
			return;
		}

		vlan_table[0] = VLAN_VALID | (vid & VLAN_FID_M);
		if (untagged)
			vlan_table[1] |= BIT(port);
		else
			vlan_table[1] &= ~BIT(port);
		vlan_table[1] &= ~(BIT(dev->cpu_port));

		vlan_table[2] |= BIT(port) | BIT(dev->cpu_port);

		if (ksz9477_set_vlan_table(dev, vid, vlan_table)) {
			dev_dbg(dev->dev, "Failed to set vlan table\n");
			return;
		}

		/* change PVID */
		if (vlan->flags & BRIDGE_VLAN_INFO_PVID)
			ksz_pwrite16(dev, port, REG_PORT_DEFAULT_VID, vid);
	}
}

static int ksz9477_port_vlan_del(struct dsa_switch *ds, int port,
				 const struct switchdev_obj_port_vlan *vlan)
{
	struct ksz_device *dev = ds->priv;
	bool untagged = vlan->flags & BRIDGE_VLAN_INFO_UNTAGGED;
	u32 vlan_table[3];
	u16 vid;
	u16 pvid;

	ksz_pread16(dev, port, REG_PORT_DEFAULT_VID, &pvid);
	pvid = pvid & 0xFFF;

	for (vid = vlan->vid_begin; vid <= vlan->vid_end; vid++) {
		if (ksz9477_get_vlan_table(dev, vid, vlan_table)) {
			dev_dbg(dev->dev, "Failed to get vlan table\n");
			return -ETIMEDOUT;
		}

		vlan_table[2] &= ~BIT(port);

		if (pvid == vid)
			pvid = 1;

		if (untagged)
			vlan_table[1] &= ~BIT(port);

		if (ksz9477_set_vlan_table(dev, vid, vlan_table)) {
			dev_dbg(dev->dev, "Failed to set vlan table\n");
			return -ETIMEDOUT;
		}
	}

	ksz_pwrite16(dev, port, REG_PORT_DEFAULT_VID, pvid);

	return 0;
}

static int ksz9477_port_fdb_add(struct dsa_switch *ds, int port,
				const unsigned char *addr, u16 vid)
{
	struct ksz_device *dev = ds->priv;
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
	ret = ksz9477_wait_alu_ready(dev, ALU_START, 1000);
	if (ret < 0) {
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
	ret = ksz9477_wait_alu_ready(dev, ALU_START, 1000);
	if (ret < 0)
		dev_dbg(dev->dev, "Failed to write ALU\n");

exit:
	mutex_unlock(&dev->alu_mutex);

	return ret;
}

static int ksz9477_port_fdb_del(struct dsa_switch *ds, int port,
				const unsigned char *addr, u16 vid)
{
	struct ksz_device *dev = ds->priv;
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
	ret = ksz9477_wait_alu_ready(dev, ALU_START, 1000);
	if (ret < 0) {
		dev_dbg(dev->dev, "Failed to read ALU\n");
		goto exit;
	}

	ksz_read32(dev, REG_SW_ALU_VAL_A, &alu_table[0]);
	if (alu_table[0] & ALU_V_STATIC_VALID) {
		ksz_read32(dev, REG_SW_ALU_VAL_B, &alu_table[1]);
		ksz_read32(dev, REG_SW_ALU_VAL_C, &alu_table[2]);
		ksz_read32(dev, REG_SW_ALU_VAL_D, &alu_table[3]);

		/* clear forwarding port */
		alu_table[2] &= ~BIT(port);

		/* if there is no port to forward, clear table */
		if ((alu_table[2] & ALU_V_PORT_MAP) == 0) {
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
	ret = ksz9477_wait_alu_ready(dev, ALU_START, 1000);
	if (ret < 0)
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

static int ksz9477_port_fdb_dump(struct dsa_switch *ds, int port,
				 dsa_fdb_dump_cb_t *cb, void *data)
{
	struct ksz_device *dev = ds->priv;
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

static void ksz9477_port_mdb_add(struct dsa_switch *ds, int port,
				 const struct switchdev_obj_port_mdb *mdb)
{
	struct ksz_device *dev = ds->priv;
	u32 static_table[4];
	u32 data;
	int index;
	u32 mac_hi, mac_lo;

	mac_hi = ((mdb->addr[0] << 8) | mdb->addr[1]);
	mac_lo = ((mdb->addr[2] << 24) | (mdb->addr[3] << 16));
	mac_lo |= ((mdb->addr[4] << 8) | mdb->addr[5]);

	mutex_lock(&dev->alu_mutex);

	for (index = 0; index < dev->num_statics; index++) {
		/* find empty slot first */
		data = (index << ALU_STAT_INDEX_S) |
			ALU_STAT_READ | ALU_STAT_START;
		ksz_write32(dev, REG_SW_ALU_STAT_CTRL__4, data);

		/* wait to be finished */
		if (ksz9477_wait_alu_sta_ready(dev, ALU_STAT_START, 1000) < 0) {
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
	if (index == dev->num_statics)
		goto exit;

	/* add entry */
	static_table[0] = ALU_V_STATIC_VALID;
	static_table[1] |= BIT(port);
	if (mdb->vid)
		static_table[1] |= ALU_V_USE_FID;
	static_table[2] = (mdb->vid << ALU_V_FID_S);
	static_table[2] |= mac_hi;
	static_table[3] = mac_lo;

	ksz9477_write_table(dev, static_table);

	data = (index << ALU_STAT_INDEX_S) | ALU_STAT_START;
	ksz_write32(dev, REG_SW_ALU_STAT_CTRL__4, data);

	/* wait to be finished */
	if (ksz9477_wait_alu_sta_ready(dev, ALU_STAT_START, 1000) < 0)
		dev_dbg(dev->dev, "Failed to read ALU STATIC\n");

exit:
	mutex_unlock(&dev->alu_mutex);
}

static int ksz9477_port_mdb_del(struct dsa_switch *ds, int port,
				const struct switchdev_obj_port_mdb *mdb)
{
	struct ksz_device *dev = ds->priv;
	u32 static_table[4];
	u32 data;
	int index;
	int ret = 0;
	u32 mac_hi, mac_lo;

	mac_hi = ((mdb->addr[0] << 8) | mdb->addr[1]);
	mac_lo = ((mdb->addr[2] << 24) | (mdb->addr[3] << 16));
	mac_lo |= ((mdb->addr[4] << 8) | mdb->addr[5]);

	mutex_lock(&dev->alu_mutex);

	for (index = 0; index < dev->num_statics; index++) {
		/* find empty slot first */
		data = (index << ALU_STAT_INDEX_S) |
			ALU_STAT_READ | ALU_STAT_START;
		ksz_write32(dev, REG_SW_ALU_STAT_CTRL__4, data);

		/* wait to be finished */
		ret = ksz9477_wait_alu_sta_ready(dev, ALU_STAT_START, 1000);
		if (ret < 0) {
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
	if (index == dev->num_statics)
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

	data = (index << ALU_STAT_INDEX_S) | ALU_STAT_START;
	ksz_write32(dev, REG_SW_ALU_STAT_CTRL__4, data);

	/* wait to be finished */
	ret = ksz9477_wait_alu_sta_ready(dev, ALU_STAT_START, 1000);
	if (ret < 0)
		dev_dbg(dev->dev, "Failed to read ALU STATIC\n");

exit:
	mutex_unlock(&dev->alu_mutex);

	return ret;
}

static int ksz9477_port_mirror_add(struct dsa_switch *ds, int port,
				   struct dsa_mall_mirror_tc_entry *mirror,
				   bool ingress)
{
	struct ksz_device *dev = ds->priv;

	if (ingress)
		ksz_port_cfg(dev, port, P_MIRROR_CTRL, PORT_MIRROR_RX, true);
	else
		ksz_port_cfg(dev, port, P_MIRROR_CTRL, PORT_MIRROR_TX, true);

	ksz_port_cfg(dev, port, P_MIRROR_CTRL, PORT_MIRROR_SNIFFER, false);

	/* configure mirror port */
	ksz_port_cfg(dev, mirror->to_local_port, P_MIRROR_CTRL,
		     PORT_MIRROR_SNIFFER, true);

	ksz_cfg(dev, S_MIRROR_CTRL, SW_MIRROR_RX_TX, false);

	return 0;
}

static void ksz9477_port_mirror_del(struct dsa_switch *ds, int port,
				    struct dsa_mall_mirror_tc_entry *mirror)
{
	struct ksz_device *dev = ds->priv;
	u8 data;

	if (mirror->ingress)
		ksz_port_cfg(dev, port, P_MIRROR_CTRL, PORT_MIRROR_RX, false);
	else
		ksz_port_cfg(dev, port, P_MIRROR_CTRL, PORT_MIRROR_TX, false);

	ksz_pread8(dev, port, P_MIRROR_CTRL, &data);

	if (!(data & (PORT_MIRROR_RX | PORT_MIRROR_TX)))
		ksz_port_cfg(dev, mirror->to_local_port, P_MIRROR_CTRL,
			     PORT_MIRROR_SNIFFER, false);
}

static void ksz9477_phy_setup(struct ksz_device *dev, int port,
			      struct phy_device *phy)
{
	/* Only apply to port with PHY. */
	if (port >= dev->phy_port_cnt)
		return;

	/* The MAC actually cannot run in 1000 half-duplex mode. */
	phy_remove_link_mode(phy,
			     ETHTOOL_LINK_MODE_1000baseT_Half_BIT);

	/* PHY does not support gigabit. */
	if (!(dev->features & GBIT_SUPPORT))
		phy_remove_link_mode(phy,
				     ETHTOOL_LINK_MODE_1000baseT_Full_BIT);
}

static bool ksz9477_get_gbit(struct ksz_device *dev, u8 data)
{
	bool gbit;

	if (dev->features & NEW_XMII)
		gbit = !(data & PORT_MII_NOT_1GBIT);
	else
		gbit = !!(data & PORT_MII_1000MBIT_S1);
	return gbit;
}

static void ksz9477_set_gbit(struct ksz_device *dev, bool gbit, u8 *data)
{
	if (dev->features & NEW_XMII) {
		if (gbit)
			*data &= ~PORT_MII_NOT_1GBIT;
		else
			*data |= PORT_MII_NOT_1GBIT;
	} else {
		if (gbit)
			*data |= PORT_MII_1000MBIT_S1;
		else
			*data &= ~PORT_MII_1000MBIT_S1;
	}
}

static int ksz9477_get_xmii(struct ksz_device *dev, u8 data)
{
	int mode;

	if (dev->features & NEW_XMII) {
		switch (data & PORT_MII_SEL_M) {
		case PORT_MII_SEL:
			mode = 0;
			break;
		case PORT_RMII_SEL:
			mode = 1;
			break;
		case PORT_GMII_SEL:
			mode = 2;
			break;
		default:
			mode = 3;
		}
	} else {
		switch (data & PORT_MII_SEL_M) {
		case PORT_MII_SEL_S1:
			mode = 0;
			break;
		case PORT_RMII_SEL_S1:
			mode = 1;
			break;
		case PORT_GMII_SEL_S1:
			mode = 2;
			break;
		default:
			mode = 3;
		}
	}
	return mode;
}

static void ksz9477_set_xmii(struct ksz_device *dev, int mode, u8 *data)
{
	u8 xmii;

	if (dev->features & NEW_XMII) {
		switch (mode) {
		case 0:
			xmii = PORT_MII_SEL;
			break;
		case 1:
			xmii = PORT_RMII_SEL;
			break;
		case 2:
			xmii = PORT_GMII_SEL;
			break;
		default:
			xmii = PORT_RGMII_SEL;
			break;
		}
	} else {
		switch (mode) {
		case 0:
			xmii = PORT_MII_SEL_S1;
			break;
		case 1:
			xmii = PORT_RMII_SEL_S1;
			break;
		case 2:
			xmii = PORT_GMII_SEL_S1;
			break;
		default:
			xmii = PORT_RGMII_SEL_S1;
			break;
		}
	}
	*data &= ~PORT_MII_SEL_M;
	*data |= xmii;
}

static phy_interface_t ksz9477_get_interface(struct ksz_device *dev, int port)
{
	phy_interface_t interface;
	bool gbit;
	int mode;
	u8 data8;

	if (port < dev->phy_port_cnt)
		return PHY_INTERFACE_MODE_NA;
	ksz_pread8(dev, port, REG_PORT_XMII_CTRL_1, &data8);
	gbit = ksz9477_get_gbit(dev, data8);
	mode = ksz9477_get_xmii(dev, data8);
	switch (mode) {
	case 2:
		interface = PHY_INTERFACE_MODE_GMII;
		if (gbit)
			break;
	case 0:
		interface = PHY_INTERFACE_MODE_MII;
		break;
	case 1:
		interface = PHY_INTERFACE_MODE_RMII;
		break;
	default:
		interface = PHY_INTERFACE_MODE_RGMII;
		if (data8 & PORT_RGMII_ID_EG_ENABLE)
			interface = PHY_INTERFACE_MODE_RGMII_TXID;
		if (data8 & PORT_RGMII_ID_IG_ENABLE) {
			interface = PHY_INTERFACE_MODE_RGMII_RXID;
			if (data8 & PORT_RGMII_ID_EG_ENABLE)
				interface = PHY_INTERFACE_MODE_RGMII_ID;
		}
		break;
	}
	return interface;
}

static void ksz9477_port_setup(struct ksz_device *dev, int port, bool cpu_port)
{
	u8 data8;
	u8 member;
	u16 data16;
	struct ksz_port *p = &dev->ports[port];

	/* enable tag tail for host port */
	if (cpu_port)
		ksz_port_cfg(dev, port, REG_PORT_CTRL_0, PORT_TAIL_TAG_ENABLE,
			     true);

	ksz_port_cfg(dev, port, REG_PORT_CTRL_0, PORT_MAC_LOOPBACK, false);

	/* set back pressure */
	ksz_port_cfg(dev, port, REG_PORT_MAC_CTRL_1, PORT_BACK_PRESSURE, true);

	/* enable broadcast storm limit */
	ksz_port_cfg(dev, port, P_BCAST_STORM_CTRL, PORT_BROADCAST_STORM, true);

	/* disable DiffServ priority */
	ksz_port_cfg(dev, port, P_PRIO_CTRL, PORT_DIFFSERV_PRIO_ENABLE, false);

	/* replace priority */
	ksz_port_cfg(dev, port, REG_PORT_MRI_MAC_CTRL, PORT_USER_PRIO_CEILING,
		     false);
	ksz9477_port_cfg32(dev, port, REG_PORT_MTI_QUEUE_CTRL_0__4,
			   MTI_PVID_REPLACE, false);

	/* enable 802.1p priority */
	ksz_port_cfg(dev, port, P_PRIO_CTRL, PORT_802_1P_PRIO_ENABLE, true);

	if (port < dev->phy_port_cnt) {
		/* do not force flow control */
		ksz_port_cfg(dev, port, REG_PORT_CTRL_0,
			     PORT_FORCE_TX_FLOW_CTRL | PORT_FORCE_RX_FLOW_CTRL,
			     false);

	} else {
		/* force flow control */
		ksz_port_cfg(dev, port, REG_PORT_CTRL_0,
			     PORT_FORCE_TX_FLOW_CTRL | PORT_FORCE_RX_FLOW_CTRL,
			     true);

		/* configure MAC to 1G & RGMII mode */
		ksz_pread8(dev, port, REG_PORT_XMII_CTRL_1, &data8);
		switch (dev->interface) {
		case PHY_INTERFACE_MODE_MII:
			ksz9477_set_xmii(dev, 0, &data8);
			ksz9477_set_gbit(dev, false, &data8);
			p->phydev.speed = SPEED_100;
			break;
		case PHY_INTERFACE_MODE_RMII:
			ksz9477_set_xmii(dev, 1, &data8);
			ksz9477_set_gbit(dev, false, &data8);
			p->phydev.speed = SPEED_100;
			break;
		case PHY_INTERFACE_MODE_GMII:
			ksz9477_set_xmii(dev, 2, &data8);
			ksz9477_set_gbit(dev, true, &data8);
			p->phydev.speed = SPEED_1000;
			break;
		default:
			ksz9477_set_xmii(dev, 3, &data8);
			ksz9477_set_gbit(dev, true, &data8);
			data8 &= ~PORT_RGMII_ID_IG_ENABLE;
			data8 &= ~PORT_RGMII_ID_EG_ENABLE;
			if (dev->interface == PHY_INTERFACE_MODE_RGMII_ID ||
			    dev->interface == PHY_INTERFACE_MODE_RGMII_RXID)
				data8 |= PORT_RGMII_ID_IG_ENABLE;
			if (dev->interface == PHY_INTERFACE_MODE_RGMII_ID ||
			    dev->interface == PHY_INTERFACE_MODE_RGMII_TXID)
				data8 |= PORT_RGMII_ID_EG_ENABLE;
			p->phydev.speed = SPEED_1000;
			break;
		}
		ksz_pwrite8(dev, port, REG_PORT_XMII_CTRL_1, data8);
		p->phydev.duplex = 1;
	}
	mutex_lock(&dev->dev_mutex);
	if (cpu_port) {
		member = dev->port_mask;
		dev->on_ports = dev->host_mask;
		dev->live_ports = dev->host_mask;
	} else {
		member = dev->host_mask | p->vid_member;
		dev->on_ports |= (1 << port);

		/* Link was detected before port is enabled. */
		if (p->phydev.link)
			dev->live_ports |= (1 << port);
	}
	mutex_unlock(&dev->dev_mutex);
	ksz9477_cfg_port_member(dev, port, member);

	/* clear pending interrupts */
	if (port < dev->phy_port_cnt)
		ksz_pread16(dev, port, REG_PORT_PHY_INT_ENABLE, &data16);
}

static void ksz9477_config_cpu_port(struct dsa_switch *ds)
{
	struct ksz_device *dev = ds->priv;
	struct ksz_port *p;
	int i;

	ds->num_ports = dev->port_cnt;

	for (i = 0; i < dev->port_cnt; i++) {
		if (dsa_is_cpu_port(ds, i) && (dev->cpu_ports & (1 << i))) {
			phy_interface_t interface;

			dev->cpu_port = i;
			dev->host_mask = (1 << dev->cpu_port);
			dev->port_mask |= dev->host_mask;

			/* Read from XMII register to determine host port
			 * interface.  If set specifically in device tree
			 * note the difference to help debugging.
			 */
			interface = ksz9477_get_interface(dev, i);
			if (!dev->interface)
				dev->interface = interface;
			if (interface && interface != dev->interface)
				dev_info(dev->dev,
					 "use %s instead of %s\n",
					  phy_modes(dev->interface),
					  phy_modes(interface));

			/* enable cpu port */
			ksz9477_port_setup(dev, i, true);
			p = &dev->ports[dev->cpu_port];
			p->vid_member = dev->port_mask;
			p->on = 1;
		}
	}

	dev->member = dev->host_mask;

	for (i = 0; i < dev->mib_port_cnt; i++) {
		if (i == dev->cpu_port)
			continue;
		p = &dev->ports[i];

		/* Initialize to non-zero so that ksz_cfg_port_member() will
		 * be called.
		 */
		p->vid_member = (1 << i);
		p->member = dev->port_mask;
		ksz9477_port_stp_state_set(ds, i, BR_STATE_DISABLED);
		p->on = 1;
		if (i < dev->phy_port_cnt)
			p->phy = 1;
		if (dev->chip_id == 0x00947700 && i == 6) {
			p->sgmii = 1;

			/* SGMII PHY detection code is not implemented yet. */
			p->phy = 0;
		}
	}
}

static int ksz9477_setup(struct dsa_switch *ds)
{
	struct ksz_device *dev = ds->priv;
	int ret = 0;

	dev->vlan_cache = devm_kcalloc(dev->dev, sizeof(struct vlan_table),
				       dev->num_vlans, GFP_KERNEL);
	if (!dev->vlan_cache)
		return -ENOMEM;

	ret = ksz9477_reset_switch(dev);
	if (ret) {
		dev_err(ds->dev, "failed to reset switch\n");
		return ret;
	}

	/* Required for port partitioning. */
	ksz9477_cfg32(dev, REG_SW_QM_CTRL__4, UNICAST_VLAN_BOUNDARY,
		      true);

	/* Do not work correctly with tail tagging. */
	ksz_cfg(dev, REG_SW_MAC_CTRL_0, SW_CHECK_LENGTH, false);

	/* accept packet up to 2000bytes */
	ksz_cfg(dev, REG_SW_MAC_CTRL_1, SW_LEGAL_PACKET_DISABLE, true);

	ksz9477_config_cpu_port(ds);

	ksz_cfg(dev, REG_SW_MAC_CTRL_1, MULTICAST_STORM_DISABLE, true);

	/* queue based egress rate limit */
	ksz_cfg(dev, REG_SW_MAC_CTRL_5, SW_OUT_RATE_LIMIT_QUEUE_BASED, true);

	/* enable global MIB counter freeze function */
	ksz_cfg(dev, REG_SW_MAC_CTRL_6, SW_MIB_COUNTER_FREEZE, true);

	/* start switch */
	ksz_cfg(dev, REG_SW_OPERATION, SW_START, true);

	ksz_init_mib_timer(dev);

	return 0;
}

static const struct dsa_switch_ops ksz9477_switch_ops = {
	.get_tag_protocol	= ksz9477_get_tag_protocol,
	.setup			= ksz9477_setup,
	.phy_read		= ksz9477_phy_read16,
	.phy_write		= ksz9477_phy_write16,
	.adjust_link		= ksz_adjust_link,
	.port_enable		= ksz_enable_port,
	.port_disable		= ksz_disable_port,
	.get_strings		= ksz9477_get_strings,
	.get_ethtool_stats	= ksz_get_ethtool_stats,
	.get_sset_count		= ksz_sset_count,
	.port_bridge_join	= ksz_port_bridge_join,
	.port_bridge_leave	= ksz_port_bridge_leave,
	.port_stp_state_set	= ksz9477_port_stp_state_set,
	.port_fast_age		= ksz_port_fast_age,
	.port_vlan_filtering	= ksz9477_port_vlan_filtering,
	.port_vlan_prepare	= ksz_port_vlan_prepare,
	.port_vlan_add		= ksz9477_port_vlan_add,
	.port_vlan_del		= ksz9477_port_vlan_del,
	.port_fdb_dump		= ksz9477_port_fdb_dump,
	.port_fdb_add		= ksz9477_port_fdb_add,
	.port_fdb_del		= ksz9477_port_fdb_del,
	.port_mdb_prepare       = ksz_port_mdb_prepare,
	.port_mdb_add           = ksz9477_port_mdb_add,
	.port_mdb_del           = ksz9477_port_mdb_del,
	.port_mirror_add	= ksz9477_port_mirror_add,
	.port_mirror_del	= ksz9477_port_mirror_del,
};

static u32 ksz9477_get_port_addr(int port, int offset)
{
	return PORT_CTRL_ADDR(port, offset);
}

static int ksz9477_switch_detect(struct ksz_device *dev)
{
	u8 data8;
	u8 id_hi;
	u8 id_lo;
	u32 id32;
	int ret;

	/* turn off SPI DO Edge select */
	ret = ksz_read8(dev, REG_SW_GLOBAL_SERIAL_CTRL_0, &data8);
	if (ret)
		return ret;

	data8 &= ~SPI_AUTO_EDGE_DETECTION;
	ret = ksz_write8(dev, REG_SW_GLOBAL_SERIAL_CTRL_0, data8);
	if (ret)
		return ret;

	/* read chip id */
	ret = ksz_read32(dev, REG_CHIP_ID0__1, &id32);
	if (ret)
		return ret;
	ret = ksz_read8(dev, REG_GLOBAL_OPTIONS, &data8);
	if (ret)
		return ret;

	/* Number of ports can be reduced depending on chip. */
	dev->mib_port_cnt = TOTAL_PORT_NUM;
	dev->phy_port_cnt = 5;

	/* Default capability is gigabit capable. */
	dev->features = GBIT_SUPPORT;

	id_hi = (u8)(id32 >> 16);
	id_lo = (u8)(id32 >> 8);
	if ((id_lo & 0xf) == 3) {
		/* Chip is from KSZ9893 design. */
		dev->features |= IS_9893;

		/* Chip does not support gigabit. */
		if (data8 & SW_QW_ABLE)
			dev->features &= ~GBIT_SUPPORT;
		dev->mib_port_cnt = 3;
		dev->phy_port_cnt = 2;
	} else {
		/* Chip uses new XMII register definitions. */
		dev->features |= NEW_XMII;

		/* Chip does not support gigabit. */
		if (!(data8 & SW_GIGABIT_ABLE))
			dev->features &= ~GBIT_SUPPORT;
	}

	/* Change chip id to known ones so it can be matched against them. */
	id32 = (id_hi << 16) | (id_lo << 8);

	dev->chip_id = id32;

	return 0;
}

struct ksz_chip_data {
	u32 chip_id;
	const char *dev_name;
	int num_vlans;
	int num_alus;
	int num_statics;
	int cpu_ports;
	int port_cnt;
};

static const struct ksz_chip_data ksz9477_switch_chips[] = {
	{
		.chip_id = 0x00947700,
		.dev_name = "KSZ9477",
		.num_vlans = 4096,
		.num_alus = 4096,
		.num_statics = 16,
		.cpu_ports = 0x7F,	/* can be configured as cpu port */
		.port_cnt = 7,		/* total physical port count */
	},
	{
		.chip_id = 0x00989700,
		.dev_name = "KSZ9897",
		.num_vlans = 4096,
		.num_alus = 4096,
		.num_statics = 16,
		.cpu_ports = 0x7F,	/* can be configured as cpu port */
		.port_cnt = 7,		/* total physical port count */
	},
	{
		.chip_id = 0x00989300,
		.dev_name = "KSZ9893",
		.num_vlans = 4096,
		.num_alus = 4096,
		.num_statics = 16,
		.cpu_ports = 0x07,	/* can be configured as cpu port */
		.port_cnt = 3,		/* total port count */
	},
};

static int ksz9477_switch_init(struct ksz_device *dev)
{
	int i;

	dev->ds->ops = &ksz9477_switch_ops;

	for (i = 0; i < ARRAY_SIZE(ksz9477_switch_chips); i++) {
		const struct ksz_chip_data *chip = &ksz9477_switch_chips[i];

		if (dev->chip_id == chip->chip_id) {
			dev->name = chip->dev_name;
			dev->num_vlans = chip->num_vlans;
			dev->num_alus = chip->num_alus;
			dev->num_statics = chip->num_statics;
			dev->port_cnt = chip->port_cnt;
			dev->cpu_ports = chip->cpu_ports;

			break;
		}
	}

	/* no switch found */
	if (!dev->port_cnt)
		return -ENODEV;

	dev->port_mask = (1 << dev->port_cnt) - 1;

	dev->reg_mib_cnt = SWITCH_COUNTER_NUM;
	dev->mib_cnt = TOTAL_SWITCH_COUNTER_NUM;

	i = dev->mib_port_cnt;
	dev->ports = devm_kzalloc(dev->dev, sizeof(struct ksz_port) * i,
				  GFP_KERNEL);
	if (!dev->ports)
		return -ENOMEM;
	for (i = 0; i < dev->mib_port_cnt; i++) {
		mutex_init(&dev->ports[i].mib.cnt_mutex);
		dev->ports[i].mib.counters =
			devm_kzalloc(dev->dev,
				     sizeof(u64) *
				     (TOTAL_SWITCH_COUNTER_NUM + 1),
				     GFP_KERNEL);
		if (!dev->ports[i].mib.counters)
			return -ENOMEM;
	}

	return 0;
}

static void ksz9477_switch_exit(struct ksz_device *dev)
{
	ksz9477_reset_switch(dev);
}

static const struct ksz_dev_ops ksz9477_dev_ops = {
	.get_port_addr = ksz9477_get_port_addr,
	.cfg_port_member = ksz9477_cfg_port_member,
	.flush_dyn_mac_table = ksz9477_flush_dyn_mac_table,
	.phy_setup = ksz9477_phy_setup,
	.port_setup = ksz9477_port_setup,
	.r_mib_cnt = ksz9477_r_mib_cnt,
	.r_mib_pkt = ksz9477_r_mib_pkt,
	.freeze_mib = ksz9477_freeze_mib,
	.port_init_cnt = ksz9477_port_init_cnt,
	.shutdown = ksz9477_reset_switch,
	.detect = ksz9477_switch_detect,
	.init = ksz9477_switch_init,
	.exit = ksz9477_switch_exit,
};

int ksz9477_switch_register(struct ksz_device *dev)
{
	return ksz_switch_register(dev, &ksz9477_dev_ops);
}
EXPORT_SYMBOL(ksz9477_switch_register);

MODULE_AUTHOR("Woojung Huh <Woojung.Huh@microchip.com>");
MODULE_DESCRIPTION("Microchip KSZ9477 Series Switch DSA Driver");
MODULE_LICENSE("GPL");
