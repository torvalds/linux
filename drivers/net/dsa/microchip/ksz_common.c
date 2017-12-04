/*
 * Microchip switch driver main logic
 *
 * Copyright (C) 2017
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <linux/delay.h>
#include <linux/export.h>
#include <linux/gpio.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_data/microchip-ksz.h>
#include <linux/phy.h>
#include <linux/etherdevice.h>
#include <linux/if_bridge.h>
#include <net/dsa.h>
#include <net/switchdev.h>

#include "ksz_priv.h"

static const struct {
	int index;
	char string[ETH_GSTRING_LEN];
} mib_names[TOTAL_SWITCH_COUNTER_NUM] = {
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

static void ksz_cfg(struct ksz_device *dev, u32 addr, u8 bits, bool set)
{
	u8 data;

	ksz_read8(dev, addr, &data);
	if (set)
		data |= bits;
	else
		data &= ~bits;
	ksz_write8(dev, addr, data);
}

static void ksz_cfg32(struct ksz_device *dev, u32 addr, u32 bits, bool set)
{
	u32 data;

	ksz_read32(dev, addr, &data);
	if (set)
		data |= bits;
	else
		data &= ~bits;
	ksz_write32(dev, addr, data);
}

static void ksz_port_cfg(struct ksz_device *dev, int port, int offset, u8 bits,
			 bool set)
{
	u32 addr;
	u8 data;

	addr = PORT_CTRL_ADDR(port, offset);
	ksz_read8(dev, addr, &data);

	if (set)
		data |= bits;
	else
		data &= ~bits;

	ksz_write8(dev, addr, data);
}

static void ksz_port_cfg32(struct ksz_device *dev, int port, int offset,
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

static int wait_vlan_ctrl_ready(struct ksz_device *dev, u32 waiton, int timeout)
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

static int get_vlan_table(struct dsa_switch *ds, u16 vid, u32 *vlan_table)
{
	struct ksz_device *dev = ds->priv;
	int ret;

	mutex_lock(&dev->vlan_mutex);

	ksz_write16(dev, REG_SW_VLAN_ENTRY_INDEX__2, vid & VLAN_INDEX_M);
	ksz_write8(dev, REG_SW_VLAN_CTRL, VLAN_READ | VLAN_START);

	/* wait to be cleared */
	ret = wait_vlan_ctrl_ready(dev, VLAN_START, 1000);
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

static int set_vlan_table(struct dsa_switch *ds, u16 vid, u32 *vlan_table)
{
	struct ksz_device *dev = ds->priv;
	int ret;

	mutex_lock(&dev->vlan_mutex);

	ksz_write32(dev, REG_SW_VLAN_ENTRY__4, vlan_table[0]);
	ksz_write32(dev, REG_SW_VLAN_ENTRY_UNTAG__4, vlan_table[1]);
	ksz_write32(dev, REG_SW_VLAN_ENTRY_PORTS__4, vlan_table[2]);

	ksz_write16(dev, REG_SW_VLAN_ENTRY_INDEX__2, vid & VLAN_INDEX_M);
	ksz_write8(dev, REG_SW_VLAN_CTRL, VLAN_START | VLAN_WRITE);

	/* wait to be cleared */
	ret = wait_vlan_ctrl_ready(dev, VLAN_START, 1000);
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

static void read_table(struct dsa_switch *ds, u32 *table)
{
	struct ksz_device *dev = ds->priv;

	ksz_read32(dev, REG_SW_ALU_VAL_A, &table[0]);
	ksz_read32(dev, REG_SW_ALU_VAL_B, &table[1]);
	ksz_read32(dev, REG_SW_ALU_VAL_C, &table[2]);
	ksz_read32(dev, REG_SW_ALU_VAL_D, &table[3]);
}

static void write_table(struct dsa_switch *ds, u32 *table)
{
	struct ksz_device *dev = ds->priv;

	ksz_write32(dev, REG_SW_ALU_VAL_A, table[0]);
	ksz_write32(dev, REG_SW_ALU_VAL_B, table[1]);
	ksz_write32(dev, REG_SW_ALU_VAL_C, table[2]);
	ksz_write32(dev, REG_SW_ALU_VAL_D, table[3]);
}

static int wait_alu_ready(struct ksz_device *dev, u32 waiton, int timeout)
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

static int wait_alu_sta_ready(struct ksz_device *dev, u32 waiton, int timeout)
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

static int ksz_reset_switch(struct dsa_switch *ds)
{
	struct ksz_device *dev = ds->priv;
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

static void port_setup(struct ksz_device *dev, int port, bool cpu_port)
{
	u8 data8;
	u16 data16;

	/* enable tag tail for host port */
	if (cpu_port)
		ksz_port_cfg(dev, port, REG_PORT_CTRL_0, PORT_TAIL_TAG_ENABLE,
			     true);

	ksz_port_cfg(dev, port, REG_PORT_CTRL_0, PORT_MAC_LOOPBACK, false);

	/* set back pressure */
	ksz_port_cfg(dev, port, REG_PORT_MAC_CTRL_1, PORT_BACK_PRESSURE, true);

	/* set flow control */
	ksz_port_cfg(dev, port, REG_PORT_CTRL_0,
		     PORT_FORCE_TX_FLOW_CTRL | PORT_FORCE_RX_FLOW_CTRL, true);

	/* enable broadcast storm limit */
	ksz_port_cfg(dev, port, P_BCAST_STORM_CTRL, PORT_BROADCAST_STORM, true);

	/* disable DiffServ priority */
	ksz_port_cfg(dev, port, P_PRIO_CTRL, PORT_DIFFSERV_PRIO_ENABLE, false);

	/* replace priority */
	ksz_port_cfg(dev, port, REG_PORT_MRI_MAC_CTRL, PORT_USER_PRIO_CEILING,
		     false);
	ksz_port_cfg32(dev, port, REG_PORT_MTI_QUEUE_CTRL_0__4,
		       MTI_PVID_REPLACE, false);

	/* enable 802.1p priority */
	ksz_port_cfg(dev, port, P_PRIO_CTRL, PORT_802_1P_PRIO_ENABLE, true);

	/* configure MAC to 1G & RGMII mode */
	ksz_pread8(dev, port, REG_PORT_XMII_CTRL_1, &data8);
	data8 |= PORT_RGMII_ID_EG_ENABLE;
	data8 &= ~PORT_MII_NOT_1GBIT;
	data8 &= ~PORT_MII_SEL_M;
	data8 |= PORT_RGMII_SEL;
	ksz_pwrite8(dev, port, REG_PORT_XMII_CTRL_1, data8);

	/* clear pending interrupts */
	ksz_pread16(dev, port, REG_PORT_PHY_INT_ENABLE, &data16);
}

static void ksz_config_cpu_port(struct dsa_switch *ds)
{
	struct ksz_device *dev = ds->priv;
	int i;

	ds->num_ports = dev->port_cnt;

	for (i = 0; i < ds->num_ports; i++) {
		if (dsa_is_cpu_port(ds, i) && (dev->cpu_ports & (1 << i))) {
			dev->cpu_port = i;

			/* enable cpu port */
			port_setup(dev, i, true);
		}
	}
}

static int ksz_setup(struct dsa_switch *ds)
{
	struct ksz_device *dev = ds->priv;
	int ret = 0;

	dev->vlan_cache = devm_kcalloc(dev->dev, sizeof(struct vlan_table),
				       dev->num_vlans, GFP_KERNEL);
	if (!dev->vlan_cache)
		return -ENOMEM;

	ret = ksz_reset_switch(ds);
	if (ret) {
		dev_err(ds->dev, "failed to reset switch\n");
		return ret;
	}

	/* accept packet up to 2000bytes */
	ksz_cfg(dev, REG_SW_MAC_CTRL_1, SW_LEGAL_PACKET_DISABLE, true);

	ksz_config_cpu_port(ds);

	ksz_cfg(dev, REG_SW_MAC_CTRL_1, MULTICAST_STORM_DISABLE, true);

	/* queue based egress rate limit */
	ksz_cfg(dev, REG_SW_MAC_CTRL_5, SW_OUT_RATE_LIMIT_QUEUE_BASED, true);

	/* start switch */
	ksz_cfg(dev, REG_SW_OPERATION, SW_START, true);

	return 0;
}

static enum dsa_tag_protocol ksz_get_tag_protocol(struct dsa_switch *ds,
						  int port)
{
	return DSA_TAG_PROTO_KSZ;
}

static int ksz_phy_read16(struct dsa_switch *ds, int addr, int reg)
{
	struct ksz_device *dev = ds->priv;
	u16 val = 0;

	ksz_pread16(dev, addr, 0x100 + (reg << 1), &val);

	return val;
}

static int ksz_phy_write16(struct dsa_switch *ds, int addr, int reg, u16 val)
{
	struct ksz_device *dev = ds->priv;

	ksz_pwrite16(dev, addr, 0x100 + (reg << 1), val);

	return 0;
}

static int ksz_enable_port(struct dsa_switch *ds, int port,
			   struct phy_device *phy)
{
	struct ksz_device *dev = ds->priv;

	/* setup slave port */
	port_setup(dev, port, false);

	return 0;
}

static void ksz_disable_port(struct dsa_switch *ds, int port,
			     struct phy_device *phy)
{
	struct ksz_device *dev = ds->priv;

	/* there is no port disable */
	ksz_port_cfg(dev, port, REG_PORT_CTRL_0, PORT_MAC_LOOPBACK, true);
}

static int ksz_sset_count(struct dsa_switch *ds)
{
	return TOTAL_SWITCH_COUNTER_NUM;
}

static void ksz_get_strings(struct dsa_switch *ds, int port, uint8_t *buf)
{
	int i;

	for (i = 0; i < TOTAL_SWITCH_COUNTER_NUM; i++) {
		memcpy(buf + i * ETH_GSTRING_LEN, mib_names[i].string,
		       ETH_GSTRING_LEN);
	}
}

static void ksz_get_ethtool_stats(struct dsa_switch *ds, int port,
				  uint64_t *buf)
{
	struct ksz_device *dev = ds->priv;
	int i;
	u32 data;
	int timeout;

	mutex_lock(&dev->stats_mutex);

	for (i = 0; i < TOTAL_SWITCH_COUNTER_NUM; i++) {
		data = MIB_COUNTER_READ;
		data |= ((mib_names[i].index & 0xFF) << MIB_COUNTER_INDEX_S);
		ksz_pwrite32(dev, port, REG_PORT_MIB_CTRL_STAT__4, data);

		timeout = 1000;
		do {
			ksz_pread32(dev, port, REG_PORT_MIB_CTRL_STAT__4,
				    &data);
			usleep_range(1, 10);
			if (!(data & MIB_COUNTER_READ))
				break;
		} while (timeout-- > 0);

		/* failed to read MIB. get out of loop */
		if (!timeout) {
			dev_dbg(dev->dev, "Failed to get MIB\n");
			break;
		}

		/* count resets upon read */
		ksz_pread32(dev, port, REG_PORT_MIB_DATA, &data);

		dev->mib_value[i] += (uint64_t)data;
		buf[i] = dev->mib_value[i];
	}

	mutex_unlock(&dev->stats_mutex);
}

static void ksz_port_stp_state_set(struct dsa_switch *ds, int port, u8 state)
{
	struct ksz_device *dev = ds->priv;
	u8 data;

	ksz_pread8(dev, port, P_STP_CTRL, &data);
	data &= ~(PORT_TX_ENABLE | PORT_RX_ENABLE | PORT_LEARN_DISABLE);

	switch (state) {
	case BR_STATE_DISABLED:
		data |= PORT_LEARN_DISABLE;
		break;
	case BR_STATE_LISTENING:
		data |= (PORT_RX_ENABLE | PORT_LEARN_DISABLE);
		break;
	case BR_STATE_LEARNING:
		data |= PORT_RX_ENABLE;
		break;
	case BR_STATE_FORWARDING:
		data |= (PORT_TX_ENABLE | PORT_RX_ENABLE);
		break;
	case BR_STATE_BLOCKING:
		data |= PORT_LEARN_DISABLE;
		break;
	default:
		dev_err(ds->dev, "invalid STP state: %d\n", state);
		return;
	}

	ksz_pwrite8(dev, port, P_STP_CTRL, data);
}

static void ksz_port_fast_age(struct dsa_switch *ds, int port)
{
	struct ksz_device *dev = ds->priv;
	u8 data8;

	ksz_read8(dev, REG_SW_LUE_CTRL_1, &data8);
	data8 |= SW_FAST_AGING;
	ksz_write8(dev, REG_SW_LUE_CTRL_1, data8);

	data8 &= ~SW_FAST_AGING;
	ksz_write8(dev, REG_SW_LUE_CTRL_1, data8);
}

static int ksz_port_vlan_filtering(struct dsa_switch *ds, int port, bool flag)
{
	struct ksz_device *dev = ds->priv;

	if (flag) {
		ksz_port_cfg(dev, port, REG_PORT_LUE_CTRL,
			     PORT_VLAN_LOOKUP_VID_0, true);
		ksz_cfg32(dev, REG_SW_QM_CTRL__4, UNICAST_VLAN_BOUNDARY, true);
		ksz_cfg(dev, REG_SW_LUE_CTRL_0, SW_VLAN_ENABLE, true);
	} else {
		ksz_cfg(dev, REG_SW_LUE_CTRL_0, SW_VLAN_ENABLE, false);
		ksz_cfg32(dev, REG_SW_QM_CTRL__4, UNICAST_VLAN_BOUNDARY, false);
		ksz_port_cfg(dev, port, REG_PORT_LUE_CTRL,
			     PORT_VLAN_LOOKUP_VID_0, false);
	}

	return 0;
}

static int ksz_port_vlan_prepare(struct dsa_switch *ds, int port,
				 const struct switchdev_obj_port_vlan *vlan,
				 struct switchdev_trans *trans)
{
	/* nothing needed */

	return 0;
}

static void ksz_port_vlan_add(struct dsa_switch *ds, int port,
			      const struct switchdev_obj_port_vlan *vlan,
			      struct switchdev_trans *trans)
{
	struct ksz_device *dev = ds->priv;
	u32 vlan_table[3];
	u16 vid;
	bool untagged = vlan->flags & BRIDGE_VLAN_INFO_UNTAGGED;

	for (vid = vlan->vid_begin; vid <= vlan->vid_end; vid++) {
		if (get_vlan_table(ds, vid, vlan_table)) {
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

		if (set_vlan_table(ds, vid, vlan_table)) {
			dev_dbg(dev->dev, "Failed to set vlan table\n");
			return;
		}

		/* change PVID */
		if (vlan->flags & BRIDGE_VLAN_INFO_PVID)
			ksz_pwrite16(dev, port, REG_PORT_DEFAULT_VID, vid);
	}
}

static int ksz_port_vlan_del(struct dsa_switch *ds, int port,
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
		if (get_vlan_table(ds, vid, vlan_table)) {
			dev_dbg(dev->dev, "Failed to get vlan table\n");
			return -ETIMEDOUT;
		}

		vlan_table[2] &= ~BIT(port);

		if (pvid == vid)
			pvid = 1;

		if (untagged)
			vlan_table[1] &= ~BIT(port);

		if (set_vlan_table(ds, vid, vlan_table)) {
			dev_dbg(dev->dev, "Failed to set vlan table\n");
			return -ETIMEDOUT;
		}
	}

	ksz_pwrite16(dev, port, REG_PORT_DEFAULT_VID, pvid);

	return 0;
}

struct alu_struct {
	/* entry 1 */
	u8	is_static:1;
	u8	is_src_filter:1;
	u8	is_dst_filter:1;
	u8	prio_age:3;
	u32	_reserv_0_1:23;
	u8	mstp:3;
	/* entry 2 */
	u8	is_override:1;
	u8	is_use_fid:1;
	u32	_reserv_1_1:23;
	u8	port_forward:7;
	/* entry 3 & 4*/
	u32	_reserv_2_1:9;
	u8	fid:7;
	u8	mac[ETH_ALEN];
};

static int ksz_port_fdb_add(struct dsa_switch *ds, int port,
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
	ret = wait_alu_ready(dev, ALU_START, 1000);
	if (ret < 0) {
		dev_dbg(dev->dev, "Failed to read ALU\n");
		goto exit;
	}

	/* read ALU entry */
	read_table(ds, alu_table);

	/* update ALU entry */
	alu_table[0] = ALU_V_STATIC_VALID;
	alu_table[1] |= BIT(port);
	if (vid)
		alu_table[1] |= ALU_V_USE_FID;
	alu_table[2] = (vid << ALU_V_FID_S);
	alu_table[2] |= ((addr[0] << 8) | addr[1]);
	alu_table[3] = ((addr[2] << 24) | (addr[3] << 16));
	alu_table[3] |= ((addr[4] << 8) | addr[5]);

	write_table(ds, alu_table);

	ksz_write32(dev, REG_SW_ALU_CTRL__4, ALU_WRITE | ALU_START);

	/* wait to be finished */
	ret = wait_alu_ready(dev, ALU_START, 1000);
	if (ret < 0)
		dev_dbg(dev->dev, "Failed to write ALU\n");

exit:
	mutex_unlock(&dev->alu_mutex);

	return ret;
}

static int ksz_port_fdb_del(struct dsa_switch *ds, int port,
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
	ret = wait_alu_ready(dev, ALU_START, 1000);
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

	write_table(ds, alu_table);

	ksz_write32(dev, REG_SW_ALU_CTRL__4, ALU_WRITE | ALU_START);

	/* wait to be finished */
	ret = wait_alu_ready(dev, ALU_START, 1000);
	if (ret < 0)
		dev_dbg(dev->dev, "Failed to write ALU\n");

exit:
	mutex_unlock(&dev->alu_mutex);

	return ret;
}

static void convert_alu(struct alu_struct *alu, u32 *alu_table)
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

static int ksz_port_fdb_dump(struct dsa_switch *ds, int port,
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
		read_table(ds, alu_table);

		convert_alu(&alu, alu_table);

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

static int ksz_port_mdb_prepare(struct dsa_switch *ds, int port,
				const struct switchdev_obj_port_mdb *mdb,
				struct switchdev_trans *trans)
{
	/* nothing to do */
	return 0;
}

static void ksz_port_mdb_add(struct dsa_switch *ds, int port,
			     const struct switchdev_obj_port_mdb *mdb,
			     struct switchdev_trans *trans)
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
		if (wait_alu_sta_ready(dev, ALU_STAT_START, 1000) < 0) {
			dev_dbg(dev->dev, "Failed to read ALU STATIC\n");
			goto exit;
		}

		/* read ALU static table */
		read_table(ds, static_table);

		if (static_table[0] & ALU_V_STATIC_VALID) {
			/* check this has same vid & mac address */
			if (((static_table[2] >> ALU_V_FID_S) == (mdb->vid)) &&
			    ((static_table[2] & ALU_V_MAC_ADDR_HI) == mac_hi) &&
			    (static_table[3] == mac_lo)) {
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

	write_table(ds, static_table);

	data = (index << ALU_STAT_INDEX_S) | ALU_STAT_START;
	ksz_write32(dev, REG_SW_ALU_STAT_CTRL__4, data);

	/* wait to be finished */
	if (wait_alu_sta_ready(dev, ALU_STAT_START, 1000) < 0)
		dev_dbg(dev->dev, "Failed to read ALU STATIC\n");

exit:
	mutex_unlock(&dev->alu_mutex);
}

static int ksz_port_mdb_del(struct dsa_switch *ds, int port,
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
		ret = wait_alu_sta_ready(dev, ALU_STAT_START, 1000);
		if (ret < 0) {
			dev_dbg(dev->dev, "Failed to read ALU STATIC\n");
			goto exit;
		}

		/* read ALU static table */
		read_table(ds, static_table);

		if (static_table[0] & ALU_V_STATIC_VALID) {
			/* check this has same vid & mac address */

			if (((static_table[2] >> ALU_V_FID_S) == (mdb->vid)) &&
			    ((static_table[2] & ALU_V_MAC_ADDR_HI) == mac_hi) &&
			    (static_table[3] == mac_lo)) {
				/* found matching one */
				break;
			}
		}
	}

	/* no available entry */
	if (index == dev->num_statics) {
		ret = -EINVAL;
		goto exit;
	}

	/* clear port */
	static_table[1] &= ~BIT(port);

	if ((static_table[1] & ALU_V_PORT_MAP) == 0) {
		/* delete entry */
		static_table[0] = 0;
		static_table[1] = 0;
		static_table[2] = 0;
		static_table[3] = 0;
	}

	write_table(ds, static_table);

	data = (index << ALU_STAT_INDEX_S) | ALU_STAT_START;
	ksz_write32(dev, REG_SW_ALU_STAT_CTRL__4, data);

	/* wait to be finished */
	ret = wait_alu_sta_ready(dev, ALU_STAT_START, 1000);
	if (ret < 0)
		dev_dbg(dev->dev, "Failed to read ALU STATIC\n");

exit:
	mutex_unlock(&dev->alu_mutex);

	return ret;
}

static int ksz_port_mirror_add(struct dsa_switch *ds, int port,
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

static void ksz_port_mirror_del(struct dsa_switch *ds, int port,
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

static const struct dsa_switch_ops ksz_switch_ops = {
	.get_tag_protocol	= ksz_get_tag_protocol,
	.setup			= ksz_setup,
	.phy_read		= ksz_phy_read16,
	.phy_write		= ksz_phy_write16,
	.port_enable		= ksz_enable_port,
	.port_disable		= ksz_disable_port,
	.get_strings		= ksz_get_strings,
	.get_ethtool_stats	= ksz_get_ethtool_stats,
	.get_sset_count		= ksz_sset_count,
	.port_stp_state_set	= ksz_port_stp_state_set,
	.port_fast_age		= ksz_port_fast_age,
	.port_vlan_filtering	= ksz_port_vlan_filtering,
	.port_vlan_prepare	= ksz_port_vlan_prepare,
	.port_vlan_add		= ksz_port_vlan_add,
	.port_vlan_del		= ksz_port_vlan_del,
	.port_fdb_dump		= ksz_port_fdb_dump,
	.port_fdb_add		= ksz_port_fdb_add,
	.port_fdb_del		= ksz_port_fdb_del,
	.port_mdb_prepare       = ksz_port_mdb_prepare,
	.port_mdb_add           = ksz_port_mdb_add,
	.port_mdb_del           = ksz_port_mdb_del,
	.port_mirror_add	= ksz_port_mirror_add,
	.port_mirror_del	= ksz_port_mirror_del,
};

struct ksz_chip_data {
	u32 chip_id;
	const char *dev_name;
	int num_vlans;
	int num_alus;
	int num_statics;
	int cpu_ports;
	int port_cnt;
};

static const struct ksz_chip_data ksz_switch_chips[] = {
	{
		.chip_id = 0x00947700,
		.dev_name = "KSZ9477",
		.num_vlans = 4096,
		.num_alus = 4096,
		.num_statics = 16,
		.cpu_ports = 0x7F,	/* can be configured as cpu port */
		.port_cnt = 7,		/* total physical port count */
	},
};

static int ksz_switch_init(struct ksz_device *dev)
{
	int i;

	mutex_init(&dev->reg_mutex);
	mutex_init(&dev->stats_mutex);
	mutex_init(&dev->alu_mutex);
	mutex_init(&dev->vlan_mutex);

	dev->ds->ops = &ksz_switch_ops;

	for (i = 0; i < ARRAY_SIZE(ksz_switch_chips); i++) {
		const struct ksz_chip_data *chip = &ksz_switch_chips[i];

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

	return 0;
}

struct ksz_device *ksz_switch_alloc(struct device *base,
				    const struct ksz_io_ops *ops,
				    void *priv)
{
	struct dsa_switch *ds;
	struct ksz_device *swdev;

	ds = dsa_switch_alloc(base, DSA_MAX_PORTS);
	if (!ds)
		return NULL;

	swdev = devm_kzalloc(base, sizeof(*swdev), GFP_KERNEL);
	if (!swdev)
		return NULL;

	ds->priv = swdev;
	swdev->dev = base;

	swdev->ds = ds;
	swdev->priv = priv;
	swdev->ops = ops;

	return swdev;
}
EXPORT_SYMBOL(ksz_switch_alloc);

int ksz_switch_detect(struct ksz_device *dev)
{
	u8 data8;
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

	dev->chip_id = id32;

	return 0;
}
EXPORT_SYMBOL(ksz_switch_detect);

int ksz_switch_register(struct ksz_device *dev)
{
	int ret;

	if (dev->pdata)
		dev->chip_id = dev->pdata->chip_id;

	if (ksz_switch_detect(dev))
		return -EINVAL;

	ret = ksz_switch_init(dev);
	if (ret)
		return ret;

	return dsa_register_switch(dev->ds);
}
EXPORT_SYMBOL(ksz_switch_register);

void ksz_switch_remove(struct ksz_device *dev)
{
	dsa_unregister_switch(dev->ds);
}
EXPORT_SYMBOL(ksz_switch_remove);

MODULE_AUTHOR("Woojung Huh <Woojung.Huh@microchip.com>");
MODULE_DESCRIPTION("Microchip KSZ Series Switch DSA Driver");
MODULE_LICENSE("GPL");
