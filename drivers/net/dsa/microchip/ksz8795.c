// SPDX-License-Identifier: GPL-2.0
/*
 * Microchip KSZ8795 switch driver
 *
 * Copyright (C) 2017 Microchip Technology Inc.
 *	Tristram Ha <Tristram.Ha@microchip.com>
 */

#include <linux/bitfield.h>
#include <linux/delay.h>
#include <linux/export.h>
#include <linux/gpio.h>
#include <linux/if_vlan.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_data/microchip-ksz.h>
#include <linux/phy.h>
#include <linux/etherdevice.h>
#include <linux/if_bridge.h>
#include <linux/micrel_phy.h>
#include <net/dsa.h>
#include <net/switchdev.h>
#include <linux/phylink.h>

#include "ksz_common.h"
#include "ksz8795_reg.h"
#include "ksz8.h"

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

static int ksz8_ind_write8(struct ksz_device *dev, u8 table, u16 addr, u8 data)
{
	const u16 *regs;
	u16 ctrl_addr;
	int ret = 0;

	regs = dev->info->regs;

	mutex_lock(&dev->alu_mutex);

	ctrl_addr = IND_ACC_TABLE(table) | addr;
	ret = ksz_write16(dev, regs[REG_IND_CTRL_0], ctrl_addr);
	if (!ret)
		ret = ksz_write8(dev, regs[REG_IND_BYTE], data);

	mutex_unlock(&dev->alu_mutex);

	return ret;
}

int ksz8_reset_switch(struct ksz_device *dev)
{
	if (ksz_is_ksz88x3(dev)) {
		/* reset switch */
		ksz_cfg(dev, KSZ8863_REG_SW_RESET,
			KSZ8863_GLOBAL_SOFTWARE_RESET | KSZ8863_PCS_RESET, true);
		ksz_cfg(dev, KSZ8863_REG_SW_RESET,
			KSZ8863_GLOBAL_SOFTWARE_RESET | KSZ8863_PCS_RESET, false);
	} else {
		/* reset switch */
		ksz_write8(dev, REG_POWER_MANAGEMENT_1,
			   SW_SOFTWARE_POWER_DOWN << SW_POWER_MANAGEMENT_MODE_S);
		ksz_write8(dev, REG_POWER_MANAGEMENT_1, 0);
	}

	return 0;
}

static int ksz8863_change_mtu(struct ksz_device *dev, int frame_size)
{
	u8 ctrl2 = 0;

	if (frame_size <= KSZ8_LEGAL_PACKET_SIZE)
		ctrl2 |= KSZ8863_LEGAL_PACKET_ENABLE;
	else if (frame_size > KSZ8863_NORMAL_PACKET_SIZE)
		ctrl2 |= KSZ8863_HUGE_PACKET_ENABLE;

	return ksz_rmw8(dev, REG_SW_CTRL_2, KSZ8863_LEGAL_PACKET_ENABLE |
			KSZ8863_HUGE_PACKET_ENABLE, ctrl2);
}

static int ksz8795_change_mtu(struct ksz_device *dev, int frame_size)
{
	u8 ctrl1 = 0, ctrl2 = 0;
	int ret;

	if (frame_size > KSZ8_LEGAL_PACKET_SIZE)
		ctrl2 |= SW_LEGAL_PACKET_DISABLE;
	if (frame_size > KSZ8863_NORMAL_PACKET_SIZE)
		ctrl1 |= SW_HUGE_PACKET;

	ret = ksz_rmw8(dev, REG_SW_CTRL_1, SW_HUGE_PACKET, ctrl1);
	if (ret)
		return ret;

	return ksz_rmw8(dev, REG_SW_CTRL_2, SW_LEGAL_PACKET_DISABLE, ctrl2);
}

int ksz8_change_mtu(struct ksz_device *dev, int port, int mtu)
{
	u16 frame_size;

	if (!dsa_is_cpu_port(dev->ds, port))
		return 0;

	frame_size = mtu + VLAN_ETH_HLEN + ETH_FCS_LEN;

	switch (dev->chip_id) {
	case KSZ8795_CHIP_ID:
	case KSZ8794_CHIP_ID:
	case KSZ8765_CHIP_ID:
		return ksz8795_change_mtu(dev, frame_size);
	case KSZ8830_CHIP_ID:
		return ksz8863_change_mtu(dev, frame_size);
	}

	return -EOPNOTSUPP;
}

static void ksz8795_set_prio_queue(struct ksz_device *dev, int port, int queue)
{
	u8 hi, lo;

	/* Number of queues can only be 1, 2, or 4. */
	switch (queue) {
	case 4:
	case 3:
		queue = PORT_QUEUE_SPLIT_4;
		break;
	case 2:
		queue = PORT_QUEUE_SPLIT_2;
		break;
	default:
		queue = PORT_QUEUE_SPLIT_1;
	}
	ksz_pread8(dev, port, REG_PORT_CTRL_0, &lo);
	ksz_pread8(dev, port, P_DROP_TAG_CTRL, &hi);
	lo &= ~PORT_QUEUE_SPLIT_L;
	if (queue & PORT_QUEUE_SPLIT_2)
		lo |= PORT_QUEUE_SPLIT_L;
	hi &= ~PORT_QUEUE_SPLIT_H;
	if (queue & PORT_QUEUE_SPLIT_4)
		hi |= PORT_QUEUE_SPLIT_H;
	ksz_pwrite8(dev, port, REG_PORT_CTRL_0, lo);
	ksz_pwrite8(dev, port, P_DROP_TAG_CTRL, hi);

	/* Default is port based for egress rate limit. */
	if (queue != PORT_QUEUE_SPLIT_1)
		ksz_cfg(dev, REG_SW_CTRL_19, SW_OUT_RATE_LIMIT_QUEUE_BASED,
			true);
}

void ksz8_r_mib_cnt(struct ksz_device *dev, int port, u16 addr, u64 *cnt)
{
	const u32 *masks;
	const u16 *regs;
	u16 ctrl_addr;
	u32 data;
	u8 check;
	int loop;

	masks = dev->info->masks;
	regs = dev->info->regs;

	ctrl_addr = addr + dev->info->reg_mib_cnt * port;
	ctrl_addr |= IND_ACC_TABLE(TABLE_MIB | TABLE_READ);

	mutex_lock(&dev->alu_mutex);
	ksz_write16(dev, regs[REG_IND_CTRL_0], ctrl_addr);

	/* It is almost guaranteed to always read the valid bit because of
	 * slow SPI speed.
	 */
	for (loop = 2; loop > 0; loop--) {
		ksz_read8(dev, regs[REG_IND_MIB_CHECK], &check);

		if (check & masks[MIB_COUNTER_VALID]) {
			ksz_read32(dev, regs[REG_IND_DATA_LO], &data);
			if (check & masks[MIB_COUNTER_OVERFLOW])
				*cnt += MIB_COUNTER_VALUE + 1;
			*cnt += data & MIB_COUNTER_VALUE;
			break;
		}
	}
	mutex_unlock(&dev->alu_mutex);
}

static void ksz8795_r_mib_pkt(struct ksz_device *dev, int port, u16 addr,
			      u64 *dropped, u64 *cnt)
{
	const u32 *masks;
	const u16 *regs;
	u16 ctrl_addr;
	u32 data;
	u8 check;
	int loop;

	masks = dev->info->masks;
	regs = dev->info->regs;

	addr -= dev->info->reg_mib_cnt;
	ctrl_addr = (KSZ8795_MIB_TOTAL_RX_1 - KSZ8795_MIB_TOTAL_RX_0) * port;
	ctrl_addr += addr + KSZ8795_MIB_TOTAL_RX_0;
	ctrl_addr |= IND_ACC_TABLE(TABLE_MIB | TABLE_READ);

	mutex_lock(&dev->alu_mutex);
	ksz_write16(dev, regs[REG_IND_CTRL_0], ctrl_addr);

	/* It is almost guaranteed to always read the valid bit because of
	 * slow SPI speed.
	 */
	for (loop = 2; loop > 0; loop--) {
		ksz_read8(dev, regs[REG_IND_MIB_CHECK], &check);

		if (check & masks[MIB_COUNTER_VALID]) {
			ksz_read32(dev, regs[REG_IND_DATA_LO], &data);
			if (addr < 2) {
				u64 total;

				total = check & MIB_TOTAL_BYTES_H;
				total <<= 32;
				*cnt += total;
				*cnt += data;
				if (check & masks[MIB_COUNTER_OVERFLOW]) {
					total = MIB_TOTAL_BYTES_H + 1;
					total <<= 32;
					*cnt += total;
				}
			} else {
				if (check & masks[MIB_COUNTER_OVERFLOW])
					*cnt += MIB_PACKET_DROPPED + 1;
				*cnt += data & MIB_PACKET_DROPPED;
			}
			break;
		}
	}
	mutex_unlock(&dev->alu_mutex);
}

static void ksz8863_r_mib_pkt(struct ksz_device *dev, int port, u16 addr,
			      u64 *dropped, u64 *cnt)
{
	u32 *last = (u32 *)dropped;
	const u16 *regs;
	u16 ctrl_addr;
	u32 data;
	u32 cur;

	regs = dev->info->regs;

	addr -= dev->info->reg_mib_cnt;
	ctrl_addr = addr ? KSZ8863_MIB_PACKET_DROPPED_TX_0 :
			   KSZ8863_MIB_PACKET_DROPPED_RX_0;
	ctrl_addr += port;
	ctrl_addr |= IND_ACC_TABLE(TABLE_MIB | TABLE_READ);

	mutex_lock(&dev->alu_mutex);
	ksz_write16(dev, regs[REG_IND_CTRL_0], ctrl_addr);
	ksz_read32(dev, regs[REG_IND_DATA_LO], &data);
	mutex_unlock(&dev->alu_mutex);

	data &= MIB_PACKET_DROPPED;
	cur = last[addr];
	if (data != cur) {
		last[addr] = data;
		if (data < cur)
			data += MIB_PACKET_DROPPED + 1;
		data -= cur;
		*cnt += data;
	}
}

void ksz8_r_mib_pkt(struct ksz_device *dev, int port, u16 addr,
		    u64 *dropped, u64 *cnt)
{
	if (ksz_is_ksz88x3(dev))
		ksz8863_r_mib_pkt(dev, port, addr, dropped, cnt);
	else
		ksz8795_r_mib_pkt(dev, port, addr, dropped, cnt);
}

void ksz8_freeze_mib(struct ksz_device *dev, int port, bool freeze)
{
	if (ksz_is_ksz88x3(dev))
		return;

	/* enable the port for flush/freeze function */
	if (freeze)
		ksz_cfg(dev, REG_SW_CTRL_6, BIT(port), true);
	ksz_cfg(dev, REG_SW_CTRL_6, SW_MIB_COUNTER_FREEZE, freeze);

	/* disable the port after freeze is done */
	if (!freeze)
		ksz_cfg(dev, REG_SW_CTRL_6, BIT(port), false);
}

void ksz8_port_init_cnt(struct ksz_device *dev, int port)
{
	struct ksz_port_mib *mib = &dev->ports[port].mib;
	u64 *dropped;

	if (!ksz_is_ksz88x3(dev)) {
		/* flush all enabled port MIB counters */
		ksz_cfg(dev, REG_SW_CTRL_6, BIT(port), true);
		ksz_cfg(dev, REG_SW_CTRL_6, SW_MIB_COUNTER_FLUSH, true);
		ksz_cfg(dev, REG_SW_CTRL_6, BIT(port), false);
	}

	mib->cnt_ptr = 0;

	/* Some ports may not have MIB counters before SWITCH_COUNTER_NUM. */
	while (mib->cnt_ptr < dev->info->reg_mib_cnt) {
		dev->dev_ops->r_mib_cnt(dev, port, mib->cnt_ptr,
					&mib->counters[mib->cnt_ptr]);
		++mib->cnt_ptr;
	}

	/* last one in storage */
	dropped = &mib->counters[dev->info->mib_cnt];

	/* Some ports may not have MIB counters after SWITCH_COUNTER_NUM. */
	while (mib->cnt_ptr < dev->info->mib_cnt) {
		dev->dev_ops->r_mib_pkt(dev, port, mib->cnt_ptr,
					dropped, &mib->counters[mib->cnt_ptr]);
		++mib->cnt_ptr;
	}
}

static int ksz8_r_table(struct ksz_device *dev, int table, u16 addr, u64 *data)
{
	const u16 *regs;
	u16 ctrl_addr;
	int ret;

	regs = dev->info->regs;

	ctrl_addr = IND_ACC_TABLE(table | TABLE_READ) | addr;

	mutex_lock(&dev->alu_mutex);
	ret = ksz_write16(dev, regs[REG_IND_CTRL_0], ctrl_addr);
	if (ret)
		goto unlock_alu;

	ret = ksz_read64(dev, regs[REG_IND_DATA_HI], data);
unlock_alu:
	mutex_unlock(&dev->alu_mutex);

	return ret;
}

static int ksz8_w_table(struct ksz_device *dev, int table, u16 addr, u64 data)
{
	const u16 *regs;
	u16 ctrl_addr;
	int ret;

	regs = dev->info->regs;

	ctrl_addr = IND_ACC_TABLE(table) | addr;

	mutex_lock(&dev->alu_mutex);
	ret = ksz_write64(dev, regs[REG_IND_DATA_HI], data);
	if (ret)
		goto unlock_alu;

	ret = ksz_write16(dev, regs[REG_IND_CTRL_0], ctrl_addr);
unlock_alu:
	mutex_unlock(&dev->alu_mutex);

	return ret;
}

static int ksz8_valid_dyn_entry(struct ksz_device *dev, u8 *data)
{
	int timeout = 100;
	const u32 *masks;
	const u16 *regs;

	masks = dev->info->masks;
	regs = dev->info->regs;

	do {
		ksz_read8(dev, regs[REG_IND_DATA_CHECK], data);
		timeout--;
	} while ((*data & masks[DYNAMIC_MAC_TABLE_NOT_READY]) && timeout);

	/* Entry is not ready for accessing. */
	if (*data & masks[DYNAMIC_MAC_TABLE_NOT_READY]) {
		return -EAGAIN;
	/* Entry is ready for accessing. */
	} else {
		ksz_read8(dev, regs[REG_IND_DATA_8], data);

		/* There is no valid entry in the table. */
		if (*data & masks[DYNAMIC_MAC_TABLE_MAC_EMPTY])
			return -ENXIO;
	}
	return 0;
}

int ksz8_r_dyn_mac_table(struct ksz_device *dev, u16 addr, u8 *mac_addr,
			 u8 *fid, u8 *src_port, u8 *timestamp, u16 *entries)
{
	u32 data_hi, data_lo;
	const u8 *shifts;
	const u32 *masks;
	const u16 *regs;
	u16 ctrl_addr;
	u8 data;
	int rc;

	shifts = dev->info->shifts;
	masks = dev->info->masks;
	regs = dev->info->regs;

	ctrl_addr = IND_ACC_TABLE(TABLE_DYNAMIC_MAC | TABLE_READ) | addr;

	mutex_lock(&dev->alu_mutex);
	ksz_write16(dev, regs[REG_IND_CTRL_0], ctrl_addr);

	rc = ksz8_valid_dyn_entry(dev, &data);
	if (rc == -EAGAIN) {
		if (addr == 0)
			*entries = 0;
	} else if (rc == -ENXIO) {
		*entries = 0;
	/* At least one valid entry in the table. */
	} else {
		u64 buf = 0;
		int cnt;

		ksz_read64(dev, regs[REG_IND_DATA_HI], &buf);
		data_hi = (u32)(buf >> 32);
		data_lo = (u32)buf;

		/* Check out how many valid entry in the table. */
		cnt = data & masks[DYNAMIC_MAC_TABLE_ENTRIES_H];
		cnt <<= shifts[DYNAMIC_MAC_ENTRIES_H];
		cnt |= (data_hi & masks[DYNAMIC_MAC_TABLE_ENTRIES]) >>
			shifts[DYNAMIC_MAC_ENTRIES];
		*entries = cnt + 1;

		*fid = (data_hi & masks[DYNAMIC_MAC_TABLE_FID]) >>
			shifts[DYNAMIC_MAC_FID];
		*src_port = (data_hi & masks[DYNAMIC_MAC_TABLE_SRC_PORT]) >>
			shifts[DYNAMIC_MAC_SRC_PORT];
		*timestamp = (data_hi & masks[DYNAMIC_MAC_TABLE_TIMESTAMP]) >>
			shifts[DYNAMIC_MAC_TIMESTAMP];

		mac_addr[5] = (u8)data_lo;
		mac_addr[4] = (u8)(data_lo >> 8);
		mac_addr[3] = (u8)(data_lo >> 16);
		mac_addr[2] = (u8)(data_lo >> 24);

		mac_addr[1] = (u8)data_hi;
		mac_addr[0] = (u8)(data_hi >> 8);
		rc = 0;
	}
	mutex_unlock(&dev->alu_mutex);

	return rc;
}

static int ksz8_r_sta_mac_table(struct ksz_device *dev, u16 addr,
				struct alu_struct *alu, bool *valid)
{
	u32 data_hi, data_lo;
	const u8 *shifts;
	const u32 *masks;
	u64 data;
	int ret;

	shifts = dev->info->shifts;
	masks = dev->info->masks;

	ret = ksz8_r_table(dev, TABLE_STATIC_MAC, addr, &data);
	if (ret)
		return ret;

	data_hi = data >> 32;
	data_lo = (u32)data;

	if (!(data_hi & (masks[STATIC_MAC_TABLE_VALID] |
			 masks[STATIC_MAC_TABLE_OVERRIDE]))) {
		*valid = false;
		return 0;
	}

	alu->mac[5] = (u8)data_lo;
	alu->mac[4] = (u8)(data_lo >> 8);
	alu->mac[3] = (u8)(data_lo >> 16);
	alu->mac[2] = (u8)(data_lo >> 24);
	alu->mac[1] = (u8)data_hi;
	alu->mac[0] = (u8)(data_hi >> 8);
	alu->port_forward =
		(data_hi & masks[STATIC_MAC_TABLE_FWD_PORTS]) >>
			shifts[STATIC_MAC_FWD_PORTS];
	alu->is_override = (data_hi & masks[STATIC_MAC_TABLE_OVERRIDE]) ? 1 : 0;

	/* KSZ8795 family switches have STATIC_MAC_TABLE_USE_FID and
	 * STATIC_MAC_TABLE_FID definitions off by 1 when doing read on the
	 * static MAC table compared to doing write.
	 */
	if (ksz_is_ksz87xx(dev))
		data_hi >>= 1;
	alu->is_static = true;
	alu->is_use_fid = (data_hi & masks[STATIC_MAC_TABLE_USE_FID]) ? 1 : 0;
	alu->fid = (data_hi & masks[STATIC_MAC_TABLE_FID]) >>
		shifts[STATIC_MAC_FID];

	*valid = true;

	return 0;
}

static int ksz8_w_sta_mac_table(struct ksz_device *dev, u16 addr,
				struct alu_struct *alu)
{
	u32 data_hi, data_lo;
	const u8 *shifts;
	const u32 *masks;
	u64 data;

	shifts = dev->info->shifts;
	masks = dev->info->masks;

	data_lo = ((u32)alu->mac[2] << 24) |
		((u32)alu->mac[3] << 16) |
		((u32)alu->mac[4] << 8) | alu->mac[5];
	data_hi = ((u32)alu->mac[0] << 8) | alu->mac[1];
	data_hi |= (u32)alu->port_forward << shifts[STATIC_MAC_FWD_PORTS];

	if (alu->is_override)
		data_hi |= masks[STATIC_MAC_TABLE_OVERRIDE];
	if (alu->is_use_fid) {
		data_hi |= masks[STATIC_MAC_TABLE_USE_FID];
		data_hi |= (u32)alu->fid << shifts[STATIC_MAC_FID];
	}
	if (alu->is_static)
		data_hi |= masks[STATIC_MAC_TABLE_VALID];
	else
		data_hi &= ~masks[STATIC_MAC_TABLE_OVERRIDE];

	data = (u64)data_hi << 32 | data_lo;

	return ksz8_w_table(dev, TABLE_STATIC_MAC, addr, data);
}

static void ksz8_from_vlan(struct ksz_device *dev, u32 vlan, u8 *fid,
			   u8 *member, u8 *valid)
{
	const u8 *shifts;
	const u32 *masks;

	shifts = dev->info->shifts;
	masks = dev->info->masks;

	*fid = vlan & masks[VLAN_TABLE_FID];
	*member = (vlan & masks[VLAN_TABLE_MEMBERSHIP]) >>
			shifts[VLAN_TABLE_MEMBERSHIP_S];
	*valid = !!(vlan & masks[VLAN_TABLE_VALID]);
}

static void ksz8_to_vlan(struct ksz_device *dev, u8 fid, u8 member, u8 valid,
			 u16 *vlan)
{
	const u8 *shifts;
	const u32 *masks;

	shifts = dev->info->shifts;
	masks = dev->info->masks;

	*vlan = fid;
	*vlan |= (u16)member << shifts[VLAN_TABLE_MEMBERSHIP_S];
	if (valid)
		*vlan |= masks[VLAN_TABLE_VALID];
}

static void ksz8_r_vlan_entries(struct ksz_device *dev, u16 addr)
{
	const u8 *shifts;
	u64 data;
	int i;

	shifts = dev->info->shifts;

	ksz8_r_table(dev, TABLE_VLAN, addr, &data);
	addr *= 4;
	for (i = 0; i < 4; i++) {
		dev->vlan_cache[addr + i].table[0] = (u16)data;
		data >>= shifts[VLAN_TABLE];
	}
}

static void ksz8_r_vlan_table(struct ksz_device *dev, u16 vid, u16 *vlan)
{
	int index;
	u16 *data;
	u16 addr;
	u64 buf;

	data = (u16 *)&buf;
	addr = vid / 4;
	index = vid & 3;
	ksz8_r_table(dev, TABLE_VLAN, addr, &buf);
	*vlan = data[index];
}

static void ksz8_w_vlan_table(struct ksz_device *dev, u16 vid, u16 vlan)
{
	int index;
	u16 *data;
	u16 addr;
	u64 buf;

	data = (u16 *)&buf;
	addr = vid / 4;
	index = vid & 3;
	ksz8_r_table(dev, TABLE_VLAN, addr, &buf);
	data[index] = vlan;
	dev->vlan_cache[vid].table[0] = vlan;
	ksz8_w_table(dev, TABLE_VLAN, addr, buf);
}

int ksz8_r_phy(struct ksz_device *dev, u16 phy, u16 reg, u16 *val)
{
	u8 restart, speed, ctrl, link;
	int processed = true;
	const u16 *regs;
	u8 val1, val2;
	u16 data = 0;
	u8 p = phy;
	int ret;

	regs = dev->info->regs;

	switch (reg) {
	case MII_BMCR:
		ret = ksz_pread8(dev, p, regs[P_NEG_RESTART_CTRL], &restart);
		if (ret)
			return ret;

		ret = ksz_pread8(dev, p, regs[P_SPEED_STATUS], &speed);
		if (ret)
			return ret;

		ret = ksz_pread8(dev, p, regs[P_FORCE_CTRL], &ctrl);
		if (ret)
			return ret;

		if (restart & PORT_PHY_LOOPBACK)
			data |= BMCR_LOOPBACK;
		if (ctrl & PORT_FORCE_100_MBIT)
			data |= BMCR_SPEED100;
		if (ksz_is_ksz88x3(dev)) {
			if ((ctrl & PORT_AUTO_NEG_ENABLE))
				data |= BMCR_ANENABLE;
		} else {
			if (!(ctrl & PORT_AUTO_NEG_DISABLE))
				data |= BMCR_ANENABLE;
		}
		if (restart & PORT_POWER_DOWN)
			data |= BMCR_PDOWN;
		if (restart & PORT_AUTO_NEG_RESTART)
			data |= BMCR_ANRESTART;
		if (ctrl & PORT_FORCE_FULL_DUPLEX)
			data |= BMCR_FULLDPLX;
		if (speed & PORT_HP_MDIX)
			data |= KSZ886X_BMCR_HP_MDIX;
		if (restart & PORT_FORCE_MDIX)
			data |= KSZ886X_BMCR_FORCE_MDI;
		if (restart & PORT_AUTO_MDIX_DISABLE)
			data |= KSZ886X_BMCR_DISABLE_AUTO_MDIX;
		if (restart & PORT_TX_DISABLE)
			data |= KSZ886X_BMCR_DISABLE_TRANSMIT;
		if (restart & PORT_LED_OFF)
			data |= KSZ886X_BMCR_DISABLE_LED;
		break;
	case MII_BMSR:
		ret = ksz_pread8(dev, p, regs[P_LINK_STATUS], &link);
		if (ret)
			return ret;

		data = BMSR_100FULL |
		       BMSR_100HALF |
		       BMSR_10FULL |
		       BMSR_10HALF |
		       BMSR_ANEGCAPABLE;
		if (link & PORT_AUTO_NEG_COMPLETE)
			data |= BMSR_ANEGCOMPLETE;
		if (link & PORT_STAT_LINK_GOOD)
			data |= BMSR_LSTATUS;
		break;
	case MII_PHYSID1:
		data = KSZ8795_ID_HI;
		break;
	case MII_PHYSID2:
		if (ksz_is_ksz88x3(dev))
			data = KSZ8863_ID_LO;
		else
			data = KSZ8795_ID_LO;
		break;
	case MII_ADVERTISE:
		ret = ksz_pread8(dev, p, regs[P_LOCAL_CTRL], &ctrl);
		if (ret)
			return ret;

		data = ADVERTISE_CSMA;
		if (ctrl & PORT_AUTO_NEG_SYM_PAUSE)
			data |= ADVERTISE_PAUSE_CAP;
		if (ctrl & PORT_AUTO_NEG_100BTX_FD)
			data |= ADVERTISE_100FULL;
		if (ctrl & PORT_AUTO_NEG_100BTX)
			data |= ADVERTISE_100HALF;
		if (ctrl & PORT_AUTO_NEG_10BT_FD)
			data |= ADVERTISE_10FULL;
		if (ctrl & PORT_AUTO_NEG_10BT)
			data |= ADVERTISE_10HALF;
		break;
	case MII_LPA:
		ret = ksz_pread8(dev, p, regs[P_REMOTE_STATUS], &link);
		if (ret)
			return ret;

		data = LPA_SLCT;
		if (link & PORT_REMOTE_SYM_PAUSE)
			data |= LPA_PAUSE_CAP;
		if (link & PORT_REMOTE_100BTX_FD)
			data |= LPA_100FULL;
		if (link & PORT_REMOTE_100BTX)
			data |= LPA_100HALF;
		if (link & PORT_REMOTE_10BT_FD)
			data |= LPA_10FULL;
		if (link & PORT_REMOTE_10BT)
			data |= LPA_10HALF;
		if (data & ~LPA_SLCT)
			data |= LPA_LPACK;
		break;
	case PHY_REG_LINK_MD:
		ret = ksz_pread8(dev, p, REG_PORT_LINK_MD_CTRL, &val1);
		if (ret)
			return ret;

		ret = ksz_pread8(dev, p, REG_PORT_LINK_MD_RESULT, &val2);
		if (ret)
			return ret;

		if (val1 & PORT_START_CABLE_DIAG)
			data |= PHY_START_CABLE_DIAG;

		if (val1 & PORT_CABLE_10M_SHORT)
			data |= PHY_CABLE_10M_SHORT;

		data |= FIELD_PREP(PHY_CABLE_DIAG_RESULT_M,
				FIELD_GET(PORT_CABLE_DIAG_RESULT_M, val1));

		data |= FIELD_PREP(PHY_CABLE_FAULT_COUNTER_M,
				(FIELD_GET(PORT_CABLE_FAULT_COUNTER_H, val1) << 8) |
				FIELD_GET(PORT_CABLE_FAULT_COUNTER_L, val2));
		break;
	case PHY_REG_PHY_CTRL:
		ret = ksz_pread8(dev, p, regs[P_LINK_STATUS], &link);
		if (ret)
			return ret;

		if (link & PORT_MDIX_STATUS)
			data |= KSZ886X_CTRL_MDIX_STAT;
		break;
	default:
		processed = false;
		break;
	}
	if (processed)
		*val = data;

	return 0;
}

int ksz8_w_phy(struct ksz_device *dev, u16 phy, u16 reg, u16 val)
{
	u8 restart, speed, ctrl, data;
	const u16 *regs;
	u8 p = phy;
	int ret;

	regs = dev->info->regs;

	switch (reg) {
	case MII_BMCR:

		/* Do not support PHY reset function. */
		if (val & BMCR_RESET)
			break;
		ret = ksz_pread8(dev, p, regs[P_SPEED_STATUS], &speed);
		if (ret)
			return ret;

		data = speed;
		if (val & KSZ886X_BMCR_HP_MDIX)
			data |= PORT_HP_MDIX;
		else
			data &= ~PORT_HP_MDIX;

		if (data != speed) {
			ret = ksz_pwrite8(dev, p, regs[P_SPEED_STATUS], data);
			if (ret)
				return ret;
		}

		ret = ksz_pread8(dev, p, regs[P_FORCE_CTRL], &ctrl);
		if (ret)
			return ret;

		data = ctrl;
		if (ksz_is_ksz88x3(dev)) {
			if ((val & BMCR_ANENABLE))
				data |= PORT_AUTO_NEG_ENABLE;
			else
				data &= ~PORT_AUTO_NEG_ENABLE;
		} else {
			if (!(val & BMCR_ANENABLE))
				data |= PORT_AUTO_NEG_DISABLE;
			else
				data &= ~PORT_AUTO_NEG_DISABLE;

			/* Fiber port does not support auto-negotiation. */
			if (dev->ports[p].fiber)
				data |= PORT_AUTO_NEG_DISABLE;
		}

		if (val & BMCR_SPEED100)
			data |= PORT_FORCE_100_MBIT;
		else
			data &= ~PORT_FORCE_100_MBIT;
		if (val & BMCR_FULLDPLX)
			data |= PORT_FORCE_FULL_DUPLEX;
		else
			data &= ~PORT_FORCE_FULL_DUPLEX;

		if (data != ctrl) {
			ret = ksz_pwrite8(dev, p, regs[P_FORCE_CTRL], data);
			if (ret)
				return ret;
		}

		ret = ksz_pread8(dev, p, regs[P_NEG_RESTART_CTRL], &restart);
		if (ret)
			return ret;

		data = restart;
		if (val & KSZ886X_BMCR_DISABLE_LED)
			data |= PORT_LED_OFF;
		else
			data &= ~PORT_LED_OFF;
		if (val & KSZ886X_BMCR_DISABLE_TRANSMIT)
			data |= PORT_TX_DISABLE;
		else
			data &= ~PORT_TX_DISABLE;
		if (val & BMCR_ANRESTART)
			data |= PORT_AUTO_NEG_RESTART;
		else
			data &= ~(PORT_AUTO_NEG_RESTART);
		if (val & BMCR_PDOWN)
			data |= PORT_POWER_DOWN;
		else
			data &= ~PORT_POWER_DOWN;
		if (val & KSZ886X_BMCR_DISABLE_AUTO_MDIX)
			data |= PORT_AUTO_MDIX_DISABLE;
		else
			data &= ~PORT_AUTO_MDIX_DISABLE;
		if (val & KSZ886X_BMCR_FORCE_MDI)
			data |= PORT_FORCE_MDIX;
		else
			data &= ~PORT_FORCE_MDIX;
		if (val & BMCR_LOOPBACK)
			data |= PORT_PHY_LOOPBACK;
		else
			data &= ~PORT_PHY_LOOPBACK;

		if (data != restart) {
			ret = ksz_pwrite8(dev, p, regs[P_NEG_RESTART_CTRL],
					  data);
			if (ret)
				return ret;
		}
		break;
	case MII_ADVERTISE:
		ret = ksz_pread8(dev, p, regs[P_LOCAL_CTRL], &ctrl);
		if (ret)
			return ret;

		data = ctrl;
		data &= ~(PORT_AUTO_NEG_SYM_PAUSE |
			  PORT_AUTO_NEG_100BTX_FD |
			  PORT_AUTO_NEG_100BTX |
			  PORT_AUTO_NEG_10BT_FD |
			  PORT_AUTO_NEG_10BT);
		if (val & ADVERTISE_PAUSE_CAP)
			data |= PORT_AUTO_NEG_SYM_PAUSE;
		if (val & ADVERTISE_100FULL)
			data |= PORT_AUTO_NEG_100BTX_FD;
		if (val & ADVERTISE_100HALF)
			data |= PORT_AUTO_NEG_100BTX;
		if (val & ADVERTISE_10FULL)
			data |= PORT_AUTO_NEG_10BT_FD;
		if (val & ADVERTISE_10HALF)
			data |= PORT_AUTO_NEG_10BT;

		if (data != ctrl) {
			ret = ksz_pwrite8(dev, p, regs[P_LOCAL_CTRL], data);
			if (ret)
				return ret;
		}
		break;
	case PHY_REG_LINK_MD:
		if (val & PHY_START_CABLE_DIAG)
			ksz_port_cfg(dev, p, REG_PORT_LINK_MD_CTRL, PORT_START_CABLE_DIAG, true);
		break;
	default:
		break;
	}

	return 0;
}

void ksz8_cfg_port_member(struct ksz_device *dev, int port, u8 member)
{
	u8 data;

	ksz_pread8(dev, port, P_MIRROR_CTRL, &data);
	data &= ~PORT_VLAN_MEMBERSHIP;
	data |= (member & dev->port_mask);
	ksz_pwrite8(dev, port, P_MIRROR_CTRL, data);
}

void ksz8_flush_dyn_mac_table(struct ksz_device *dev, int port)
{
	u8 learn[DSA_MAX_PORTS];
	int first, index, cnt;
	const u16 *regs;

	regs = dev->info->regs;

	if ((uint)port < dev->info->port_cnt) {
		first = port;
		cnt = port + 1;
	} else {
		/* Flush all ports. */
		first = 0;
		cnt = dev->info->port_cnt;
	}
	for (index = first; index < cnt; index++) {
		ksz_pread8(dev, index, regs[P_STP_CTRL], &learn[index]);
		if (!(learn[index] & PORT_LEARN_DISABLE))
			ksz_pwrite8(dev, index, regs[P_STP_CTRL],
				    learn[index] | PORT_LEARN_DISABLE);
	}
	ksz_cfg(dev, S_FLUSH_TABLE_CTRL, SW_FLUSH_DYN_MAC_TABLE, true);
	for (index = first; index < cnt; index++) {
		if (!(learn[index] & PORT_LEARN_DISABLE))
			ksz_pwrite8(dev, index, regs[P_STP_CTRL], learn[index]);
	}
}

int ksz8_fdb_dump(struct ksz_device *dev, int port,
		  dsa_fdb_dump_cb_t *cb, void *data)
{
	int ret = 0;
	u16 i = 0;
	u16 entries = 0;
	u8 timestamp = 0;
	u8 fid;
	u8 src_port;
	u8 mac[ETH_ALEN];

	do {
		ret = ksz8_r_dyn_mac_table(dev, i, mac, &fid, &src_port,
					   &timestamp, &entries);
		if (!ret && port == src_port) {
			ret = cb(mac, fid, false, data);
			if (ret)
				break;
		}
		i++;
	} while (i < entries);
	if (i >= entries)
		ret = 0;

	return ret;
}

static int ksz8_add_sta_mac(struct ksz_device *dev, int port,
			    const unsigned char *addr, u16 vid)
{
	struct alu_struct alu;
	int index, ret;
	int empty = 0;

	alu.port_forward = 0;
	for (index = 0; index < dev->info->num_statics; index++) {
		bool valid;

		ret = ksz8_r_sta_mac_table(dev, index, &alu, &valid);
		if (ret)
			return ret;
		if (!valid) {
			/* Remember the first empty entry. */
			if (!empty)
				empty = index + 1;
			continue;
		}

		if (!memcmp(alu.mac, addr, ETH_ALEN) && alu.fid == vid)
			break;
	}

	/* no available entry */
	if (index == dev->info->num_statics && !empty)
		return -ENOSPC;

	/* add entry */
	if (index == dev->info->num_statics) {
		index = empty - 1;
		memset(&alu, 0, sizeof(alu));
		memcpy(alu.mac, addr, ETH_ALEN);
		alu.is_static = true;
	}
	alu.port_forward |= BIT(port);
	if (vid) {
		alu.is_use_fid = true;

		/* Need a way to map VID to FID. */
		alu.fid = vid;
	}

	return ksz8_w_sta_mac_table(dev, index, &alu);
}

static int ksz8_del_sta_mac(struct ksz_device *dev, int port,
			    const unsigned char *addr, u16 vid)
{
	struct alu_struct alu;
	int index, ret;

	for (index = 0; index < dev->info->num_statics; index++) {
		bool valid;

		ret = ksz8_r_sta_mac_table(dev, index, &alu, &valid);
		if (ret)
			return ret;
		if (!valid)
			continue;

		if (!memcmp(alu.mac, addr, ETH_ALEN) && alu.fid == vid)
			break;
	}

	/* no available entry */
	if (index == dev->info->num_statics)
		return 0;

	/* clear port */
	alu.port_forward &= ~BIT(port);
	if (!alu.port_forward)
		alu.is_static = false;

	return ksz8_w_sta_mac_table(dev, index, &alu);
}

int ksz8_mdb_add(struct ksz_device *dev, int port,
		 const struct switchdev_obj_port_mdb *mdb, struct dsa_db db)
{
	return ksz8_add_sta_mac(dev, port, mdb->addr, mdb->vid);
}

int ksz8_mdb_del(struct ksz_device *dev, int port,
		 const struct switchdev_obj_port_mdb *mdb, struct dsa_db db)
{
	return ksz8_del_sta_mac(dev, port, mdb->addr, mdb->vid);
}

int ksz8_fdb_add(struct ksz_device *dev, int port, const unsigned char *addr,
		 u16 vid, struct dsa_db db)
{
	return ksz8_add_sta_mac(dev, port, addr, vid);
}

int ksz8_fdb_del(struct ksz_device *dev, int port, const unsigned char *addr,
		 u16 vid, struct dsa_db db)
{
	return ksz8_del_sta_mac(dev, port, addr, vid);
}

int ksz8_port_vlan_filtering(struct ksz_device *dev, int port, bool flag,
			     struct netlink_ext_ack *extack)
{
	if (ksz_is_ksz88x3(dev))
		return -ENOTSUPP;

	/* Discard packets with VID not enabled on the switch */
	ksz_cfg(dev, S_MIRROR_CTRL, SW_VLAN_ENABLE, flag);

	/* Discard packets with VID not enabled on the ingress port */
	for (port = 0; port < dev->phy_port_cnt; ++port)
		ksz_port_cfg(dev, port, REG_PORT_CTRL_2, PORT_INGRESS_FILTER,
			     flag);

	return 0;
}

static void ksz8_port_enable_pvid(struct ksz_device *dev, int port, bool state)
{
	if (ksz_is_ksz88x3(dev)) {
		ksz_cfg(dev, REG_SW_INSERT_SRC_PVID,
			0x03 << (4 - 2 * port), state);
	} else {
		ksz_pwrite8(dev, port, REG_PORT_CTRL_12, state ? 0x0f : 0x00);
	}
}

int ksz8_port_vlan_add(struct ksz_device *dev, int port,
		       const struct switchdev_obj_port_vlan *vlan,
		       struct netlink_ext_ack *extack)
{
	bool untagged = vlan->flags & BRIDGE_VLAN_INFO_UNTAGGED;
	struct ksz_port *p = &dev->ports[port];
	u16 data, new_pvid = 0;
	u8 fid, member, valid;

	if (ksz_is_ksz88x3(dev))
		return -ENOTSUPP;

	/* If a VLAN is added with untagged flag different from the
	 * port's Remove Tag flag, we need to change the latter.
	 * Ignore VID 0, which is always untagged.
	 * Ignore CPU port, which will always be tagged.
	 */
	if (untagged != p->remove_tag && vlan->vid != 0 &&
	    port != dev->cpu_port) {
		unsigned int vid;

		/* Reject attempts to add a VLAN that requires the
		 * Remove Tag flag to be changed, unless there are no
		 * other VLANs currently configured.
		 */
		for (vid = 1; vid < dev->info->num_vlans; ++vid) {
			/* Skip the VID we are going to add or reconfigure */
			if (vid == vlan->vid)
				continue;

			ksz8_from_vlan(dev, dev->vlan_cache[vid].table[0],
				       &fid, &member, &valid);
			if (valid && (member & BIT(port)))
				return -EINVAL;
		}

		ksz_port_cfg(dev, port, P_TAG_CTRL, PORT_REMOVE_TAG, untagged);
		p->remove_tag = untagged;
	}

	ksz8_r_vlan_table(dev, vlan->vid, &data);
	ksz8_from_vlan(dev, data, &fid, &member, &valid);

	/* First time to setup the VLAN entry. */
	if (!valid) {
		/* Need to find a way to map VID to FID. */
		fid = 1;
		valid = 1;
	}
	member |= BIT(port);

	ksz8_to_vlan(dev, fid, member, valid, &data);
	ksz8_w_vlan_table(dev, vlan->vid, data);

	/* change PVID */
	if (vlan->flags & BRIDGE_VLAN_INFO_PVID)
		new_pvid = vlan->vid;

	if (new_pvid) {
		u16 vid;

		ksz_pread16(dev, port, REG_PORT_CTRL_VID, &vid);
		vid &= ~VLAN_VID_MASK;
		vid |= new_pvid;
		ksz_pwrite16(dev, port, REG_PORT_CTRL_VID, vid);

		ksz8_port_enable_pvid(dev, port, true);
	}

	return 0;
}

int ksz8_port_vlan_del(struct ksz_device *dev, int port,
		       const struct switchdev_obj_port_vlan *vlan)
{
	u16 data, pvid;
	u8 fid, member, valid;

	if (ksz_is_ksz88x3(dev))
		return -ENOTSUPP;

	ksz_pread16(dev, port, REG_PORT_CTRL_VID, &pvid);
	pvid = pvid & 0xFFF;

	ksz8_r_vlan_table(dev, vlan->vid, &data);
	ksz8_from_vlan(dev, data, &fid, &member, &valid);

	member &= ~BIT(port);

	/* Invalidate the entry if no more member. */
	if (!member) {
		fid = 0;
		valid = 0;
	}

	ksz8_to_vlan(dev, fid, member, valid, &data);
	ksz8_w_vlan_table(dev, vlan->vid, data);

	if (pvid == vlan->vid)
		ksz8_port_enable_pvid(dev, port, false);

	return 0;
}

int ksz8_port_mirror_add(struct ksz_device *dev, int port,
			 struct dsa_mall_mirror_tc_entry *mirror,
			 bool ingress, struct netlink_ext_ack *extack)
{
	if (ingress) {
		ksz_port_cfg(dev, port, P_MIRROR_CTRL, PORT_MIRROR_RX, true);
		dev->mirror_rx |= BIT(port);
	} else {
		ksz_port_cfg(dev, port, P_MIRROR_CTRL, PORT_MIRROR_TX, true);
		dev->mirror_tx |= BIT(port);
	}

	ksz_port_cfg(dev, port, P_MIRROR_CTRL, PORT_MIRROR_SNIFFER, false);

	/* configure mirror port */
	if (dev->mirror_rx || dev->mirror_tx)
		ksz_port_cfg(dev, mirror->to_local_port, P_MIRROR_CTRL,
			     PORT_MIRROR_SNIFFER, true);

	return 0;
}

void ksz8_port_mirror_del(struct ksz_device *dev, int port,
			  struct dsa_mall_mirror_tc_entry *mirror)
{
	u8 data;

	if (mirror->ingress) {
		ksz_port_cfg(dev, port, P_MIRROR_CTRL, PORT_MIRROR_RX, false);
		dev->mirror_rx &= ~BIT(port);
	} else {
		ksz_port_cfg(dev, port, P_MIRROR_CTRL, PORT_MIRROR_TX, false);
		dev->mirror_tx &= ~BIT(port);
	}

	ksz_pread8(dev, port, P_MIRROR_CTRL, &data);

	if (!dev->mirror_rx && !dev->mirror_tx)
		ksz_port_cfg(dev, mirror->to_local_port, P_MIRROR_CTRL,
			     PORT_MIRROR_SNIFFER, false);
}

static void ksz8795_cpu_interface_select(struct ksz_device *dev, int port)
{
	struct ksz_port *p = &dev->ports[port];

	if (!p->interface && dev->compat_interface) {
		dev_warn(dev->dev,
			 "Using legacy switch \"phy-mode\" property, because it is missing on port %d node. "
			 "Please update your device tree.\n",
			 port);
		p->interface = dev->compat_interface;
	}
}

void ksz8_port_setup(struct ksz_device *dev, int port, bool cpu_port)
{
	struct dsa_switch *ds = dev->ds;
	const u32 *masks;
	u8 member;

	masks = dev->info->masks;

	/* enable broadcast storm limit */
	ksz_port_cfg(dev, port, P_BCAST_STORM_CTRL, PORT_BROADCAST_STORM, true);

	if (!ksz_is_ksz88x3(dev))
		ksz8795_set_prio_queue(dev, port, 4);

	/* disable DiffServ priority */
	ksz_port_cfg(dev, port, P_PRIO_CTRL, PORT_DIFFSERV_ENABLE, false);

	/* replace priority */
	ksz_port_cfg(dev, port, P_802_1P_CTRL,
		     masks[PORT_802_1P_REMAPPING], false);

	/* enable 802.1p priority */
	ksz_port_cfg(dev, port, P_PRIO_CTRL, PORT_802_1P_ENABLE, true);

	if (cpu_port) {
		if (!ksz_is_ksz88x3(dev))
			ksz8795_cpu_interface_select(dev, port);

		member = dsa_user_ports(ds);
	} else {
		member = BIT(dsa_upstream_port(ds, port));
	}

	ksz8_cfg_port_member(dev, port, member);
}

void ksz8_config_cpu_port(struct dsa_switch *ds)
{
	struct ksz_device *dev = ds->priv;
	struct ksz_port *p;
	const u32 *masks;
	const u16 *regs;
	u8 remote;
	int i;

	masks = dev->info->masks;
	regs = dev->info->regs;

	ksz_cfg(dev, regs[S_TAIL_TAG_CTRL], masks[SW_TAIL_TAG_ENABLE], true);

	ksz8_port_setup(dev, dev->cpu_port, true);

	for (i = 0; i < dev->phy_port_cnt; i++) {
		ksz_port_stp_state_set(ds, i, BR_STATE_DISABLED);
	}
	for (i = 0; i < dev->phy_port_cnt; i++) {
		p = &dev->ports[i];

		if (!ksz_is_ksz88x3(dev)) {
			ksz_pread8(dev, i, regs[P_REMOTE_STATUS], &remote);
			if (remote & KSZ8_PORT_FIBER_MODE)
				p->fiber = 1;
		}
		if (p->fiber)
			ksz_port_cfg(dev, i, regs[P_STP_CTRL],
				     PORT_FORCE_FLOW_CTRL, true);
		else
			ksz_port_cfg(dev, i, regs[P_STP_CTRL],
				     PORT_FORCE_FLOW_CTRL, false);
	}
}

static int ksz8_handle_global_errata(struct dsa_switch *ds)
{
	struct ksz_device *dev = ds->priv;
	int ret = 0;

	/* KSZ87xx Errata DS80000687C.
	 * Module 2: Link drops with some EEE link partners.
	 *   An issue with the EEE next page exchange between the
	 *   KSZ879x/KSZ877x/KSZ876x and some EEE link partners may result in
	 *   the link dropping.
	 */
	if (dev->info->ksz87xx_eee_link_erratum)
		ret = ksz8_ind_write8(dev, TABLE_EEE, REG_IND_EEE_GLOB2_HI, 0);

	return ret;
}

int ksz8_enable_stp_addr(struct ksz_device *dev)
{
	struct alu_struct alu;

	/* Setup STP address for STP operation. */
	memset(&alu, 0, sizeof(alu));
	ether_addr_copy(alu.mac, eth_stp_addr);
	alu.is_static = true;
	alu.is_override = true;
	alu.port_forward = dev->info->cpu_ports;

	return ksz8_w_sta_mac_table(dev, 0, &alu);
}

int ksz8_setup(struct dsa_switch *ds)
{
	struct ksz_device *dev = ds->priv;
	int i;

	ds->mtu_enforcement_ingress = true;

	/* We rely on software untagging on the CPU port, so that we
	 * can support both tagged and untagged VLANs
	 */
	ds->untag_bridge_pvid = true;

	/* VLAN filtering is partly controlled by the global VLAN
	 * Enable flag
	 */
	ds->vlan_filtering_is_global = true;

	ksz_cfg(dev, S_REPLACE_VID_CTRL, SW_FLOW_CTRL, true);

	/* Enable automatic fast aging when link changed detected. */
	ksz_cfg(dev, S_LINK_AGING_CTRL, SW_LINK_AUTO_AGING, true);

	/* Enable aggressive back off algorithm in half duplex mode. */
	regmap_update_bits(ksz_regmap_8(dev), REG_SW_CTRL_1,
			   SW_AGGR_BACKOFF, SW_AGGR_BACKOFF);

	/*
	 * Make sure unicast VLAN boundary is set as default and
	 * enable no excessive collision drop.
	 */
	regmap_update_bits(ksz_regmap_8(dev), REG_SW_CTRL_2,
			   UNICAST_VLAN_BOUNDARY | NO_EXC_COLLISION_DROP,
			   UNICAST_VLAN_BOUNDARY | NO_EXC_COLLISION_DROP);

	ksz_cfg(dev, S_REPLACE_VID_CTRL, SW_REPLACE_VID, false);

	ksz_cfg(dev, S_MIRROR_CTRL, SW_MIRROR_RX_TX, false);

	if (!ksz_is_ksz88x3(dev))
		ksz_cfg(dev, REG_SW_CTRL_19, SW_INS_TAG_ENABLE, true);

	for (i = 0; i < (dev->info->num_vlans / 4); i++)
		ksz8_r_vlan_entries(dev, i);

	return ksz8_handle_global_errata(ds);
}

void ksz8_get_caps(struct ksz_device *dev, int port,
		   struct phylink_config *config)
{
	config->mac_capabilities = MAC_10 | MAC_100;

	/* Silicon Errata Sheet (DS80000830A):
	 * "Port 1 does not respond to received flow control PAUSE frames"
	 * So, disable Pause support on "Port 1" (port == 0) for all ksz88x3
	 * switches.
	 */
	if (!ksz_is_ksz88x3(dev) || port)
		config->mac_capabilities |= MAC_SYM_PAUSE;

	/* Asym pause is not supported on KSZ8863 and KSZ8873 */
	if (!ksz_is_ksz88x3(dev))
		config->mac_capabilities |= MAC_ASYM_PAUSE;
}

u32 ksz8_get_port_addr(int port, int offset)
{
	return PORT_CTRL_ADDR(port, offset);
}

int ksz8_switch_init(struct ksz_device *dev)
{
	dev->cpu_port = fls(dev->info->cpu_ports) - 1;
	dev->phy_port_cnt = dev->info->port_cnt - 1;
	dev->port_mask = (BIT(dev->phy_port_cnt) - 1) | dev->info->cpu_ports;

	return 0;
}

void ksz8_switch_exit(struct ksz_device *dev)
{
	ksz8_reset_switch(dev);
}

MODULE_AUTHOR("Tristram Ha <Tristram.Ha@microchip.com>");
MODULE_DESCRIPTION("Microchip KSZ8795 Series Switch DSA Driver");
MODULE_LICENSE("GPL");
