/*
 * net/dsa/mv88e6xxx.c - Marvell 88e6xxx switch chip support
 * Copyright (c) 2008 Marvell Semiconductor
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/delay.h>
#include <linux/etherdevice.h>
#include <linux/if_bridge.h>
#include <linux/jiffies.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/phy.h>
#include <net/dsa.h>
#include "mv88e6xxx.h"

/* If the switch's ADDR[4:0] strap pins are strapped to zero, it will
 * use all 32 SMI bus addresses on its SMI bus, and all switch registers
 * will be directly accessible on some {device address,register address}
 * pair.  If the ADDR[4:0] pins are not strapped to zero, the switch
 * will only respond to SMI transactions to that specific address, and
 * an indirect addressing mechanism needs to be used to access its
 * registers.
 */
static int mv88e6xxx_reg_wait_ready(struct mii_bus *bus, int sw_addr)
{
	int ret;
	int i;

	for (i = 0; i < 16; i++) {
		ret = mdiobus_read(bus, sw_addr, SMI_CMD);
		if (ret < 0)
			return ret;

		if ((ret & SMI_CMD_BUSY) == 0)
			return 0;
	}

	return -ETIMEDOUT;
}

int __mv88e6xxx_reg_read(struct mii_bus *bus, int sw_addr, int addr, int reg)
{
	int ret;

	if (sw_addr == 0)
		return mdiobus_read(bus, addr, reg);

	/* Wait for the bus to become free. */
	ret = mv88e6xxx_reg_wait_ready(bus, sw_addr);
	if (ret < 0)
		return ret;

	/* Transmit the read command. */
	ret = mdiobus_write(bus, sw_addr, SMI_CMD,
			    SMI_CMD_OP_22_READ | (addr << 5) | reg);
	if (ret < 0)
		return ret;

	/* Wait for the read command to complete. */
	ret = mv88e6xxx_reg_wait_ready(bus, sw_addr);
	if (ret < 0)
		return ret;

	/* Read the data. */
	ret = mdiobus_read(bus, sw_addr, SMI_DATA);
	if (ret < 0)
		return ret;

	return ret & 0xffff;
}

/* Must be called with SMI mutex held */
static int _mv88e6xxx_reg_read(struct dsa_switch *ds, int addr, int reg)
{
	struct mii_bus *bus = dsa_host_dev_to_mii_bus(ds->master_dev);
	int ret;

	if (bus == NULL)
		return -EINVAL;

	ret = __mv88e6xxx_reg_read(bus, ds->pd->sw_addr, addr, reg);
	if (ret < 0)
		return ret;

	dev_dbg(ds->master_dev, "<- addr: 0x%.2x reg: 0x%.2x val: 0x%.4x\n",
		addr, reg, ret);

	return ret;
}

int mv88e6xxx_reg_read(struct dsa_switch *ds, int addr, int reg)
{
	struct mv88e6xxx_priv_state *ps = ds_to_priv(ds);
	int ret;

	mutex_lock(&ps->smi_mutex);
	ret = _mv88e6xxx_reg_read(ds, addr, reg);
	mutex_unlock(&ps->smi_mutex);

	return ret;
}

int __mv88e6xxx_reg_write(struct mii_bus *bus, int sw_addr, int addr,
			  int reg, u16 val)
{
	int ret;

	if (sw_addr == 0)
		return mdiobus_write(bus, addr, reg, val);

	/* Wait for the bus to become free. */
	ret = mv88e6xxx_reg_wait_ready(bus, sw_addr);
	if (ret < 0)
		return ret;

	/* Transmit the data to write. */
	ret = mdiobus_write(bus, sw_addr, SMI_DATA, val);
	if (ret < 0)
		return ret;

	/* Transmit the write command. */
	ret = mdiobus_write(bus, sw_addr, SMI_CMD,
			    SMI_CMD_OP_22_WRITE | (addr << 5) | reg);
	if (ret < 0)
		return ret;

	/* Wait for the write command to complete. */
	ret = mv88e6xxx_reg_wait_ready(bus, sw_addr);
	if (ret < 0)
		return ret;

	return 0;
}

/* Must be called with SMI mutex held */
static int _mv88e6xxx_reg_write(struct dsa_switch *ds, int addr, int reg,
				u16 val)
{
	struct mii_bus *bus = dsa_host_dev_to_mii_bus(ds->master_dev);

	if (bus == NULL)
		return -EINVAL;

	dev_dbg(ds->master_dev, "-> addr: 0x%.2x reg: 0x%.2x val: 0x%.4x\n",
		addr, reg, val);

	return __mv88e6xxx_reg_write(bus, ds->pd->sw_addr, addr, reg, val);
}

int mv88e6xxx_reg_write(struct dsa_switch *ds, int addr, int reg, u16 val)
{
	struct mv88e6xxx_priv_state *ps = ds_to_priv(ds);
	int ret;

	mutex_lock(&ps->smi_mutex);
	ret = _mv88e6xxx_reg_write(ds, addr, reg, val);
	mutex_unlock(&ps->smi_mutex);

	return ret;
}

int mv88e6xxx_config_prio(struct dsa_switch *ds)
{
	/* Configure the IP ToS mapping registers. */
	REG_WRITE(REG_GLOBAL, GLOBAL_IP_PRI_0, 0x0000);
	REG_WRITE(REG_GLOBAL, GLOBAL_IP_PRI_1, 0x0000);
	REG_WRITE(REG_GLOBAL, GLOBAL_IP_PRI_2, 0x5555);
	REG_WRITE(REG_GLOBAL, GLOBAL_IP_PRI_3, 0x5555);
	REG_WRITE(REG_GLOBAL, GLOBAL_IP_PRI_4, 0xaaaa);
	REG_WRITE(REG_GLOBAL, GLOBAL_IP_PRI_5, 0xaaaa);
	REG_WRITE(REG_GLOBAL, GLOBAL_IP_PRI_6, 0xffff);
	REG_WRITE(REG_GLOBAL, GLOBAL_IP_PRI_7, 0xffff);

	/* Configure the IEEE 802.1p priority mapping register. */
	REG_WRITE(REG_GLOBAL, GLOBAL_IEEE_PRI, 0xfa41);

	return 0;
}

int mv88e6xxx_set_addr_direct(struct dsa_switch *ds, u8 *addr)
{
	REG_WRITE(REG_GLOBAL, GLOBAL_MAC_01, (addr[0] << 8) | addr[1]);
	REG_WRITE(REG_GLOBAL, GLOBAL_MAC_23, (addr[2] << 8) | addr[3]);
	REG_WRITE(REG_GLOBAL, GLOBAL_MAC_45, (addr[4] << 8) | addr[5]);

	return 0;
}

int mv88e6xxx_set_addr_indirect(struct dsa_switch *ds, u8 *addr)
{
	int i;
	int ret;

	for (i = 0; i < 6; i++) {
		int j;

		/* Write the MAC address byte. */
		REG_WRITE(REG_GLOBAL2, GLOBAL2_SWITCH_MAC,
			  GLOBAL2_SWITCH_MAC_BUSY | (i << 8) | addr[i]);

		/* Wait for the write to complete. */
		for (j = 0; j < 16; j++) {
			ret = REG_READ(REG_GLOBAL2, GLOBAL2_SWITCH_MAC);
			if ((ret & GLOBAL2_SWITCH_MAC_BUSY) == 0)
				break;
		}
		if (j == 16)
			return -ETIMEDOUT;
	}

	return 0;
}

/* Must be called with phy mutex held */
static int _mv88e6xxx_phy_read(struct dsa_switch *ds, int addr, int regnum)
{
	if (addr >= 0)
		return mv88e6xxx_reg_read(ds, addr, regnum);
	return 0xffff;
}

/* Must be called with phy mutex held */
static int _mv88e6xxx_phy_write(struct dsa_switch *ds, int addr, int regnum,
				u16 val)
{
	if (addr >= 0)
		return mv88e6xxx_reg_write(ds, addr, regnum, val);
	return 0;
}

#ifdef CONFIG_NET_DSA_MV88E6XXX_NEED_PPU
static int mv88e6xxx_ppu_disable(struct dsa_switch *ds)
{
	int ret;
	unsigned long timeout;

	ret = REG_READ(REG_GLOBAL, GLOBAL_CONTROL);
	REG_WRITE(REG_GLOBAL, GLOBAL_CONTROL,
		  ret & ~GLOBAL_CONTROL_PPU_ENABLE);

	timeout = jiffies + 1 * HZ;
	while (time_before(jiffies, timeout)) {
		ret = REG_READ(REG_GLOBAL, GLOBAL_STATUS);
		usleep_range(1000, 2000);
		if ((ret & GLOBAL_STATUS_PPU_MASK) !=
		    GLOBAL_STATUS_PPU_POLLING)
			return 0;
	}

	return -ETIMEDOUT;
}

static int mv88e6xxx_ppu_enable(struct dsa_switch *ds)
{
	int ret;
	unsigned long timeout;

	ret = REG_READ(REG_GLOBAL, GLOBAL_CONTROL);
	REG_WRITE(REG_GLOBAL, GLOBAL_CONTROL, ret | GLOBAL_CONTROL_PPU_ENABLE);

	timeout = jiffies + 1 * HZ;
	while (time_before(jiffies, timeout)) {
		ret = REG_READ(REG_GLOBAL, GLOBAL_STATUS);
		usleep_range(1000, 2000);
		if ((ret & GLOBAL_STATUS_PPU_MASK) ==
		    GLOBAL_STATUS_PPU_POLLING)
			return 0;
	}

	return -ETIMEDOUT;
}

static void mv88e6xxx_ppu_reenable_work(struct work_struct *ugly)
{
	struct mv88e6xxx_priv_state *ps;

	ps = container_of(ugly, struct mv88e6xxx_priv_state, ppu_work);
	if (mutex_trylock(&ps->ppu_mutex)) {
		struct dsa_switch *ds = ((struct dsa_switch *)ps) - 1;

		if (mv88e6xxx_ppu_enable(ds) == 0)
			ps->ppu_disabled = 0;
		mutex_unlock(&ps->ppu_mutex);
	}
}

static void mv88e6xxx_ppu_reenable_timer(unsigned long _ps)
{
	struct mv88e6xxx_priv_state *ps = (void *)_ps;

	schedule_work(&ps->ppu_work);
}

static int mv88e6xxx_ppu_access_get(struct dsa_switch *ds)
{
	struct mv88e6xxx_priv_state *ps = ds_to_priv(ds);
	int ret;

	mutex_lock(&ps->ppu_mutex);

	/* If the PHY polling unit is enabled, disable it so that
	 * we can access the PHY registers.  If it was already
	 * disabled, cancel the timer that is going to re-enable
	 * it.
	 */
	if (!ps->ppu_disabled) {
		ret = mv88e6xxx_ppu_disable(ds);
		if (ret < 0) {
			mutex_unlock(&ps->ppu_mutex);
			return ret;
		}
		ps->ppu_disabled = 1;
	} else {
		del_timer(&ps->ppu_timer);
		ret = 0;
	}

	return ret;
}

static void mv88e6xxx_ppu_access_put(struct dsa_switch *ds)
{
	struct mv88e6xxx_priv_state *ps = ds_to_priv(ds);

	/* Schedule a timer to re-enable the PHY polling unit. */
	mod_timer(&ps->ppu_timer, jiffies + msecs_to_jiffies(10));
	mutex_unlock(&ps->ppu_mutex);
}

void mv88e6xxx_ppu_state_init(struct dsa_switch *ds)
{
	struct mv88e6xxx_priv_state *ps = ds_to_priv(ds);

	mutex_init(&ps->ppu_mutex);
	INIT_WORK(&ps->ppu_work, mv88e6xxx_ppu_reenable_work);
	init_timer(&ps->ppu_timer);
	ps->ppu_timer.data = (unsigned long)ps;
	ps->ppu_timer.function = mv88e6xxx_ppu_reenable_timer;
}

int mv88e6xxx_phy_read_ppu(struct dsa_switch *ds, int addr, int regnum)
{
	int ret;

	ret = mv88e6xxx_ppu_access_get(ds);
	if (ret >= 0) {
		ret = mv88e6xxx_reg_read(ds, addr, regnum);
		mv88e6xxx_ppu_access_put(ds);
	}

	return ret;
}

int mv88e6xxx_phy_write_ppu(struct dsa_switch *ds, int addr,
			    int regnum, u16 val)
{
	int ret;

	ret = mv88e6xxx_ppu_access_get(ds);
	if (ret >= 0) {
		ret = mv88e6xxx_reg_write(ds, addr, regnum, val);
		mv88e6xxx_ppu_access_put(ds);
	}

	return ret;
}
#endif

void mv88e6xxx_poll_link(struct dsa_switch *ds)
{
	int i;

	for (i = 0; i < DSA_MAX_PORTS; i++) {
		struct net_device *dev;
		int uninitialized_var(port_status);
		int link;
		int speed;
		int duplex;
		int fc;

		dev = ds->ports[i];
		if (dev == NULL)
			continue;

		link = 0;
		if (dev->flags & IFF_UP) {
			port_status = mv88e6xxx_reg_read(ds, REG_PORT(i),
							 PORT_STATUS);
			if (port_status < 0)
				continue;

			link = !!(port_status & PORT_STATUS_LINK);
		}

		if (!link) {
			if (netif_carrier_ok(dev)) {
				netdev_info(dev, "link down\n");
				netif_carrier_off(dev);
			}
			continue;
		}

		switch (port_status & PORT_STATUS_SPEED_MASK) {
		case PORT_STATUS_SPEED_10:
			speed = 10;
			break;
		case PORT_STATUS_SPEED_100:
			speed = 100;
			break;
		case PORT_STATUS_SPEED_1000:
			speed = 1000;
			break;
		default:
			speed = -1;
			break;
		}
		duplex = (port_status & PORT_STATUS_DUPLEX) ? 1 : 0;
		fc = (port_status & PORT_STATUS_PAUSE_EN) ? 1 : 0;

		if (!netif_carrier_ok(dev)) {
			netdev_info(dev,
				    "link up, %d Mb/s, %s duplex, flow control %sabled\n",
				    speed,
				    duplex ? "full" : "half",
				    fc ? "en" : "dis");
			netif_carrier_on(dev);
		}
	}
}

static bool mv88e6xxx_6352_family(struct dsa_switch *ds)
{
	struct mv88e6xxx_priv_state *ps = ds_to_priv(ds);

	switch (ps->id) {
	case PORT_SWITCH_ID_6352:
	case PORT_SWITCH_ID_6172:
	case PORT_SWITCH_ID_6176:
		return true;
	}
	return false;
}

static int mv88e6xxx_stats_wait(struct dsa_switch *ds)
{
	int ret;
	int i;

	for (i = 0; i < 10; i++) {
		ret = REG_READ(REG_GLOBAL, GLOBAL_STATS_OP);
		if ((ret & GLOBAL_STATS_OP_BUSY) == 0)
			return 0;
	}

	return -ETIMEDOUT;
}

static int mv88e6xxx_stats_snapshot(struct dsa_switch *ds, int port)
{
	int ret;

	if (mv88e6xxx_6352_family(ds))
		port = (port + 1) << 5;

	/* Snapshot the hardware statistics counters for this port. */
	REG_WRITE(REG_GLOBAL, GLOBAL_STATS_OP,
		  GLOBAL_STATS_OP_CAPTURE_PORT |
		  GLOBAL_STATS_OP_HIST_RX_TX | port);

	/* Wait for the snapshotting to complete. */
	ret = mv88e6xxx_stats_wait(ds);
	if (ret < 0)
		return ret;

	return 0;
}

static void mv88e6xxx_stats_read(struct dsa_switch *ds, int stat, u32 *val)
{
	u32 _val;
	int ret;

	*val = 0;

	ret = mv88e6xxx_reg_write(ds, REG_GLOBAL, GLOBAL_STATS_OP,
				  GLOBAL_STATS_OP_READ_CAPTURED |
				  GLOBAL_STATS_OP_HIST_RX_TX | stat);
	if (ret < 0)
		return;

	ret = mv88e6xxx_stats_wait(ds);
	if (ret < 0)
		return;

	ret = mv88e6xxx_reg_read(ds, REG_GLOBAL, GLOBAL_STATS_COUNTER_32);
	if (ret < 0)
		return;

	_val = ret << 16;

	ret = mv88e6xxx_reg_read(ds, REG_GLOBAL, GLOBAL_STATS_COUNTER_01);
	if (ret < 0)
		return;

	*val = _val | ret;
}

static struct mv88e6xxx_hw_stat mv88e6xxx_hw_stats[] = {
	{ "in_good_octets", 8, 0x00, },
	{ "in_bad_octets", 4, 0x02, },
	{ "in_unicast", 4, 0x04, },
	{ "in_broadcasts", 4, 0x06, },
	{ "in_multicasts", 4, 0x07, },
	{ "in_pause", 4, 0x16, },
	{ "in_undersize", 4, 0x18, },
	{ "in_fragments", 4, 0x19, },
	{ "in_oversize", 4, 0x1a, },
	{ "in_jabber", 4, 0x1b, },
	{ "in_rx_error", 4, 0x1c, },
	{ "in_fcs_error", 4, 0x1d, },
	{ "out_octets", 8, 0x0e, },
	{ "out_unicast", 4, 0x10, },
	{ "out_broadcasts", 4, 0x13, },
	{ "out_multicasts", 4, 0x12, },
	{ "out_pause", 4, 0x15, },
	{ "excessive", 4, 0x11, },
	{ "collisions", 4, 0x1e, },
	{ "deferred", 4, 0x05, },
	{ "single", 4, 0x14, },
	{ "multiple", 4, 0x17, },
	{ "out_fcs_error", 4, 0x03, },
	{ "late", 4, 0x1f, },
	{ "hist_64bytes", 4, 0x08, },
	{ "hist_65_127bytes", 4, 0x09, },
	{ "hist_128_255bytes", 4, 0x0a, },
	{ "hist_256_511bytes", 4, 0x0b, },
	{ "hist_512_1023bytes", 4, 0x0c, },
	{ "hist_1024_max_bytes", 4, 0x0d, },
	/* Not all devices have the following counters */
	{ "sw_in_discards", 4, 0x110, },
	{ "sw_in_filtered", 2, 0x112, },
	{ "sw_out_filtered", 2, 0x113, },

};

static bool have_sw_in_discards(struct dsa_switch *ds)
{
	struct mv88e6xxx_priv_state *ps = ds_to_priv(ds);

	switch (ps->id) {
	case PORT_SWITCH_ID_6095: case PORT_SWITCH_ID_6161:
	case PORT_SWITCH_ID_6165: case PORT_SWITCH_ID_6171:
	case PORT_SWITCH_ID_6172: case PORT_SWITCH_ID_6176:
	case PORT_SWITCH_ID_6182: case PORT_SWITCH_ID_6185:
	case PORT_SWITCH_ID_6352:
		return true;
	default:
		return false;
	}
}

static void _mv88e6xxx_get_strings(struct dsa_switch *ds,
				   int nr_stats,
				   struct mv88e6xxx_hw_stat *stats,
				   int port, uint8_t *data)
{
	int i;

	for (i = 0; i < nr_stats; i++) {
		memcpy(data + i * ETH_GSTRING_LEN,
		       stats[i].string, ETH_GSTRING_LEN);
	}
}

static void _mv88e6xxx_get_ethtool_stats(struct dsa_switch *ds,
					 int nr_stats,
					 struct mv88e6xxx_hw_stat *stats,
					 int port, uint64_t *data)
{
	struct mv88e6xxx_priv_state *ps = ds_to_priv(ds);
	int ret;
	int i;

	mutex_lock(&ps->stats_mutex);

	ret = mv88e6xxx_stats_snapshot(ds, port);
	if (ret < 0) {
		mutex_unlock(&ps->stats_mutex);
		return;
	}

	/* Read each of the counters. */
	for (i = 0; i < nr_stats; i++) {
		struct mv88e6xxx_hw_stat *s = stats + i;
		u32 low;
		u32 high = 0;

		if (s->reg >= 0x100) {
			ret = mv88e6xxx_reg_read(ds, REG_PORT(port),
						 s->reg - 0x100);
			if (ret < 0)
				goto error;
			low = ret;
			if (s->sizeof_stat == 4) {
				ret = mv88e6xxx_reg_read(ds, REG_PORT(port),
							 s->reg - 0x100 + 1);
				if (ret < 0)
					goto error;
				high = ret;
			}
			data[i] = (((u64)high) << 16) | low;
			continue;
		}
		mv88e6xxx_stats_read(ds, s->reg, &low);
		if (s->sizeof_stat == 8)
			mv88e6xxx_stats_read(ds, s->reg + 1, &high);

		data[i] = (((u64)high) << 32) | low;
	}
error:
	mutex_unlock(&ps->stats_mutex);
}

/* All the statistics in the table */
void
mv88e6xxx_get_strings(struct dsa_switch *ds, int port, uint8_t *data)
{
	if (have_sw_in_discards(ds))
		_mv88e6xxx_get_strings(ds, ARRAY_SIZE(mv88e6xxx_hw_stats),
				       mv88e6xxx_hw_stats, port, data);
	else
		_mv88e6xxx_get_strings(ds, ARRAY_SIZE(mv88e6xxx_hw_stats) - 3,
				       mv88e6xxx_hw_stats, port, data);
}

int mv88e6xxx_get_sset_count(struct dsa_switch *ds)
{
	if (have_sw_in_discards(ds))
		return ARRAY_SIZE(mv88e6xxx_hw_stats);
	return ARRAY_SIZE(mv88e6xxx_hw_stats) - 3;
}

void
mv88e6xxx_get_ethtool_stats(struct dsa_switch *ds,
			    int port, uint64_t *data)
{
	if (have_sw_in_discards(ds))
		_mv88e6xxx_get_ethtool_stats(
			ds, ARRAY_SIZE(mv88e6xxx_hw_stats),
			mv88e6xxx_hw_stats, port, data);
	else
		_mv88e6xxx_get_ethtool_stats(
			ds, ARRAY_SIZE(mv88e6xxx_hw_stats) - 3,
			mv88e6xxx_hw_stats, port, data);
}

int mv88e6xxx_get_regs_len(struct dsa_switch *ds, int port)
{
	return 32 * sizeof(u16);
}

void mv88e6xxx_get_regs(struct dsa_switch *ds, int port,
			struct ethtool_regs *regs, void *_p)
{
	u16 *p = _p;
	int i;

	regs->version = 0;

	memset(p, 0xff, 32 * sizeof(u16));

	for (i = 0; i < 32; i++) {
		int ret;

		ret = mv88e6xxx_reg_read(ds, REG_PORT(port), i);
		if (ret >= 0)
			p[i] = ret;
	}
}

#ifdef CONFIG_NET_DSA_HWMON

int  mv88e6xxx_get_temp(struct dsa_switch *ds, int *temp)
{
	struct mv88e6xxx_priv_state *ps = ds_to_priv(ds);
	int ret;
	int val;

	*temp = 0;

	mutex_lock(&ps->phy_mutex);

	ret = _mv88e6xxx_phy_write(ds, 0x0, 0x16, 0x6);
	if (ret < 0)
		goto error;

	/* Enable temperature sensor */
	ret = _mv88e6xxx_phy_read(ds, 0x0, 0x1a);
	if (ret < 0)
		goto error;

	ret = _mv88e6xxx_phy_write(ds, 0x0, 0x1a, ret | (1 << 5));
	if (ret < 0)
		goto error;

	/* Wait for temperature to stabilize */
	usleep_range(10000, 12000);

	val = _mv88e6xxx_phy_read(ds, 0x0, 0x1a);
	if (val < 0) {
		ret = val;
		goto error;
	}

	/* Disable temperature sensor */
	ret = _mv88e6xxx_phy_write(ds, 0x0, 0x1a, ret & ~(1 << 5));
	if (ret < 0)
		goto error;

	*temp = ((val & 0x1f) - 5) * 5;

error:
	_mv88e6xxx_phy_write(ds, 0x0, 0x16, 0x0);
	mutex_unlock(&ps->phy_mutex);
	return ret;
}
#endif /* CONFIG_NET_DSA_HWMON */

static int mv88e6xxx_wait(struct dsa_switch *ds, int reg, int offset, u16 mask)
{
	unsigned long timeout = jiffies + HZ / 10;

	while (time_before(jiffies, timeout)) {
		int ret;

		ret = REG_READ(reg, offset);
		if (!(ret & mask))
			return 0;

		usleep_range(1000, 2000);
	}
	return -ETIMEDOUT;
}

int mv88e6xxx_phy_wait(struct dsa_switch *ds)
{
	return mv88e6xxx_wait(ds, REG_GLOBAL2, GLOBAL2_SMI_OP,
			      GLOBAL2_SMI_OP_BUSY);
}

int mv88e6xxx_eeprom_load_wait(struct dsa_switch *ds)
{
	return mv88e6xxx_wait(ds, REG_GLOBAL2, GLOBAL2_EEPROM_OP,
			      GLOBAL2_EEPROM_OP_LOAD);
}

int mv88e6xxx_eeprom_busy_wait(struct dsa_switch *ds)
{
	return mv88e6xxx_wait(ds, REG_GLOBAL2, GLOBAL2_EEPROM_OP,
			      GLOBAL2_EEPROM_OP_BUSY);
}

/* Must be called with SMI lock held */
static int _mv88e6xxx_wait(struct dsa_switch *ds, int reg, int offset, u16 mask)
{
	unsigned long timeout = jiffies + HZ / 10;

	while (time_before(jiffies, timeout)) {
		int ret;

		ret = _mv88e6xxx_reg_read(ds, reg, offset);
		if (ret < 0)
			return ret;
		if (!(ret & mask))
			return 0;

		usleep_range(1000, 2000);
	}
	return -ETIMEDOUT;
}

/* Must be called with SMI lock held */
static int _mv88e6xxx_atu_wait(struct dsa_switch *ds)
{
	return _mv88e6xxx_wait(ds, REG_GLOBAL, GLOBAL_ATU_OP,
			       GLOBAL_ATU_OP_BUSY);
}

/* Must be called with phy mutex held */
static int _mv88e6xxx_phy_read_indirect(struct dsa_switch *ds, int addr,
					int regnum)
{
	int ret;

	REG_WRITE(REG_GLOBAL2, GLOBAL2_SMI_OP,
		  GLOBAL2_SMI_OP_22_READ | (addr << 5) | regnum);

	ret = mv88e6xxx_phy_wait(ds);
	if (ret < 0)
		return ret;

	return REG_READ(REG_GLOBAL2, GLOBAL2_SMI_DATA);
}

/* Must be called with phy mutex held */
static int _mv88e6xxx_phy_write_indirect(struct dsa_switch *ds, int addr,
					 int regnum, u16 val)
{
	REG_WRITE(REG_GLOBAL2, GLOBAL2_SMI_DATA, val);
	REG_WRITE(REG_GLOBAL2, GLOBAL2_SMI_OP,
		  GLOBAL2_SMI_OP_22_WRITE | (addr << 5) | regnum);

	return mv88e6xxx_phy_wait(ds);
}

int mv88e6xxx_get_eee(struct dsa_switch *ds, int port, struct ethtool_eee *e)
{
	struct mv88e6xxx_priv_state *ps = ds_to_priv(ds);
	int reg;

	mutex_lock(&ps->phy_mutex);

	reg = _mv88e6xxx_phy_read_indirect(ds, port, 16);
	if (reg < 0)
		goto out;

	e->eee_enabled = !!(reg & 0x0200);
	e->tx_lpi_enabled = !!(reg & 0x0100);

	reg = mv88e6xxx_reg_read(ds, REG_PORT(port), PORT_STATUS);
	if (reg < 0)
		goto out;

	e->eee_active = !!(reg & PORT_STATUS_EEE);
	reg = 0;

out:
	mutex_unlock(&ps->phy_mutex);
	return reg;
}

int mv88e6xxx_set_eee(struct dsa_switch *ds, int port,
		      struct phy_device *phydev, struct ethtool_eee *e)
{
	struct mv88e6xxx_priv_state *ps = ds_to_priv(ds);
	int reg;
	int ret;

	mutex_lock(&ps->phy_mutex);

	ret = _mv88e6xxx_phy_read_indirect(ds, port, 16);
	if (ret < 0)
		goto out;

	reg = ret & ~0x0300;
	if (e->eee_enabled)
		reg |= 0x0200;
	if (e->tx_lpi_enabled)
		reg |= 0x0100;

	ret = _mv88e6xxx_phy_write_indirect(ds, port, 16, reg);
out:
	mutex_unlock(&ps->phy_mutex);

	return ret;
}

static int _mv88e6xxx_atu_cmd(struct dsa_switch *ds, int fid, u16 cmd)
{
	int ret;

	ret = _mv88e6xxx_reg_write(ds, REG_GLOBAL, 0x01, fid);
	if (ret < 0)
		return ret;

	ret = _mv88e6xxx_reg_write(ds, REG_GLOBAL, GLOBAL_ATU_OP, cmd);
	if (ret < 0)
		return ret;

	return _mv88e6xxx_atu_wait(ds);
}

static int _mv88e6xxx_flush_fid(struct dsa_switch *ds, int fid)
{
	int ret;

	ret = _mv88e6xxx_atu_wait(ds);
	if (ret < 0)
		return ret;

	return _mv88e6xxx_atu_cmd(ds, fid, GLOBAL_ATU_OP_FLUSH_NON_STATIC_DB);
}

static int mv88e6xxx_set_port_state(struct dsa_switch *ds, int port, u8 state)
{
	struct mv88e6xxx_priv_state *ps = ds_to_priv(ds);
	int reg, ret = 0;
	u8 oldstate;

	mutex_lock(&ps->smi_mutex);

	reg = _mv88e6xxx_reg_read(ds, REG_PORT(port), PORT_CONTROL);
	if (reg < 0) {
		ret = reg;
		goto abort;
	}

	oldstate = reg & PORT_CONTROL_STATE_MASK;
	if (oldstate != state) {
		/* Flush forwarding database if we're moving a port
		 * from Learning or Forwarding state to Disabled or
		 * Blocking or Listening state.
		 */
		if (oldstate >= PORT_CONTROL_STATE_LEARNING &&
		    state <= PORT_CONTROL_STATE_BLOCKING) {
			ret = _mv88e6xxx_flush_fid(ds, ps->fid[port]);
			if (ret)
				goto abort;
		}
		reg = (reg & ~PORT_CONTROL_STATE_MASK) | state;
		ret = _mv88e6xxx_reg_write(ds, REG_PORT(port), PORT_CONTROL,
					   reg);
	}

abort:
	mutex_unlock(&ps->smi_mutex);
	return ret;
}

/* Must be called with smi lock held */
static int _mv88e6xxx_update_port_config(struct dsa_switch *ds, int port)
{
	struct mv88e6xxx_priv_state *ps = ds_to_priv(ds);
	u8 fid = ps->fid[port];
	u16 reg = fid << 12;

	if (dsa_is_cpu_port(ds, port))
		reg |= ds->phys_port_mask;
	else
		reg |= (ps->bridge_mask[fid] |
		       (1 << dsa_upstream_port(ds))) & ~(1 << port);

	return _mv88e6xxx_reg_write(ds, REG_PORT(port), PORT_BASE_VLAN, reg);
}

/* Must be called with smi lock held */
static int _mv88e6xxx_update_bridge_config(struct dsa_switch *ds, int fid)
{
	struct mv88e6xxx_priv_state *ps = ds_to_priv(ds);
	int port;
	u32 mask;
	int ret;

	mask = ds->phys_port_mask;
	while (mask) {
		port = __ffs(mask);
		mask &= ~(1 << port);
		if (ps->fid[port] != fid)
			continue;

		ret = _mv88e6xxx_update_port_config(ds, port);
		if (ret)
			return ret;
	}

	return _mv88e6xxx_flush_fid(ds, fid);
}

/* Bridge handling functions */

int mv88e6xxx_join_bridge(struct dsa_switch *ds, int port, u32 br_port_mask)
{
	struct mv88e6xxx_priv_state *ps = ds_to_priv(ds);
	int ret = 0;
	u32 nmask;
	int fid;

	/* If the bridge group is not empty, join that group.
	 * Otherwise create a new group.
	 */
	fid = ps->fid[port];
	nmask = br_port_mask & ~(1 << port);
	if (nmask)
		fid = ps->fid[__ffs(nmask)];

	nmask = ps->bridge_mask[fid] | (1 << port);
	if (nmask != br_port_mask) {
		netdev_err(ds->ports[port],
			   "join: Bridge port mask mismatch fid=%d mask=0x%x expected 0x%x\n",
			   fid, br_port_mask, nmask);
		return -EINVAL;
	}

	mutex_lock(&ps->smi_mutex);

	ps->bridge_mask[fid] = br_port_mask;

	if (fid != ps->fid[port]) {
		ps->fid_mask |= 1 << ps->fid[port];
		ps->fid[port] = fid;
		ret = _mv88e6xxx_update_bridge_config(ds, fid);
	}

	mutex_unlock(&ps->smi_mutex);

	return ret;
}

int mv88e6xxx_leave_bridge(struct dsa_switch *ds, int port, u32 br_port_mask)
{
	struct mv88e6xxx_priv_state *ps = ds_to_priv(ds);
	u8 fid, newfid;
	int ret;

	fid = ps->fid[port];

	if (ps->bridge_mask[fid] != br_port_mask) {
		netdev_err(ds->ports[port],
			   "leave: Bridge port mask mismatch fid=%d mask=0x%x expected 0x%x\n",
			   fid, br_port_mask, ps->bridge_mask[fid]);
		return -EINVAL;
	}

	/* If the port was the last port of a bridge, we are done.
	 * Otherwise assign a new fid to the port, and fix up
	 * the bridge configuration.
	 */
	if (br_port_mask == (1 << port))
		return 0;

	mutex_lock(&ps->smi_mutex);

	newfid = __ffs(ps->fid_mask);
	ps->fid[port] = newfid;
	ps->fid_mask &= (1 << newfid);
	ps->bridge_mask[fid] &= ~(1 << port);
	ps->bridge_mask[newfid] = 1 << port;

	ret = _mv88e6xxx_update_bridge_config(ds, fid);
	if (!ret)
		ret = _mv88e6xxx_update_bridge_config(ds, newfid);

	mutex_unlock(&ps->smi_mutex);

	return ret;
}

int mv88e6xxx_port_stp_update(struct dsa_switch *ds, int port, u8 state)
{
	struct mv88e6xxx_priv_state *ps = ds_to_priv(ds);
	int stp_state;

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

	netdev_dbg(ds->ports[port], "port state %d [%d]\n", state, stp_state);

	/* mv88e6xxx_port_stp_update may be called with softirqs disabled,
	 * so we can not update the port state directly but need to schedule it.
	 */
	ps->port_state[port] = stp_state;
	set_bit(port, &ps->port_state_update_mask);
	schedule_work(&ps->bridge_work);

	return 0;
}

static int __mv88e6xxx_write_addr(struct dsa_switch *ds,
				  const unsigned char *addr)
{
	int i, ret;

	for (i = 0; i < 3; i++) {
		ret = _mv88e6xxx_reg_write(
			ds, REG_GLOBAL, GLOBAL_ATU_MAC_01 + i,
			(addr[i * 2] << 8) | addr[i * 2 + 1]);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static int __mv88e6xxx_read_addr(struct dsa_switch *ds, unsigned char *addr)
{
	int i, ret;

	for (i = 0; i < 3; i++) {
		ret = _mv88e6xxx_reg_read(ds, REG_GLOBAL,
					  GLOBAL_ATU_MAC_01 + i);
		if (ret < 0)
			return ret;
		addr[i * 2] = ret >> 8;
		addr[i * 2 + 1] = ret & 0xff;
	}

	return 0;
}

static int __mv88e6xxx_port_fdb_cmd(struct dsa_switch *ds, int port,
				    const unsigned char *addr, int state)
{
	struct mv88e6xxx_priv_state *ps = ds_to_priv(ds);
	u8 fid = ps->fid[port];
	int ret;

	ret = _mv88e6xxx_atu_wait(ds);
	if (ret < 0)
		return ret;

	ret = __mv88e6xxx_write_addr(ds, addr);
	if (ret < 0)
		return ret;

	ret = _mv88e6xxx_reg_write(ds, REG_GLOBAL, GLOBAL_ATU_DATA,
				   (0x10 << port) | state);
	if (ret)
		return ret;

	ret = _mv88e6xxx_atu_cmd(ds, fid, GLOBAL_ATU_OP_LOAD_DB);

	return ret;
}

int mv88e6xxx_port_fdb_add(struct dsa_switch *ds, int port,
			   const unsigned char *addr, u16 vid)
{
	int state = is_multicast_ether_addr(addr) ?
		GLOBAL_ATU_DATA_STATE_MC_STATIC :
		GLOBAL_ATU_DATA_STATE_UC_STATIC;
	struct mv88e6xxx_priv_state *ps = ds_to_priv(ds);
	int ret;

	mutex_lock(&ps->smi_mutex);
	ret = __mv88e6xxx_port_fdb_cmd(ds, port, addr, state);
	mutex_unlock(&ps->smi_mutex);

	return ret;
}

int mv88e6xxx_port_fdb_del(struct dsa_switch *ds, int port,
			   const unsigned char *addr, u16 vid)
{
	struct mv88e6xxx_priv_state *ps = ds_to_priv(ds);
	int ret;

	mutex_lock(&ps->smi_mutex);
	ret = __mv88e6xxx_port_fdb_cmd(ds, port, addr,
				       GLOBAL_ATU_DATA_STATE_UNUSED);
	mutex_unlock(&ps->smi_mutex);

	return ret;
}

static int __mv88e6xxx_port_getnext(struct dsa_switch *ds, int port,
				    unsigned char *addr, bool *is_static)
{
	struct mv88e6xxx_priv_state *ps = ds_to_priv(ds);
	u8 fid = ps->fid[port];
	int ret, state;

	ret = _mv88e6xxx_atu_wait(ds);
	if (ret < 0)
		return ret;

	ret = __mv88e6xxx_write_addr(ds, addr);
	if (ret < 0)
		return ret;

	do {
		ret = _mv88e6xxx_atu_cmd(ds, fid,  GLOBAL_ATU_OP_GET_NEXT_DB);
		if (ret < 0)
			return ret;

		ret = _mv88e6xxx_reg_read(ds, REG_GLOBAL, GLOBAL_ATU_DATA);
		if (ret < 0)
			return ret;
		state = ret & GLOBAL_ATU_DATA_STATE_MASK;
		if (state == GLOBAL_ATU_DATA_STATE_UNUSED)
			return -ENOENT;
	} while (!(((ret >> 4) & 0xff) & (1 << port)));

	ret = __mv88e6xxx_read_addr(ds, addr);
	if (ret < 0)
		return ret;

	*is_static = state == (is_multicast_ether_addr(addr) ?
			       GLOBAL_ATU_DATA_STATE_MC_STATIC :
			       GLOBAL_ATU_DATA_STATE_UC_STATIC);

	return 0;
}

/* get next entry for port */
int mv88e6xxx_port_fdb_getnext(struct dsa_switch *ds, int port,
			       unsigned char *addr, bool *is_static)
{
	struct mv88e6xxx_priv_state *ps = ds_to_priv(ds);
	int ret;

	mutex_lock(&ps->smi_mutex);
	ret = __mv88e6xxx_port_getnext(ds, port, addr, is_static);
	mutex_unlock(&ps->smi_mutex);

	return ret;
}

static void mv88e6xxx_bridge_work(struct work_struct *work)
{
	struct mv88e6xxx_priv_state *ps;
	struct dsa_switch *ds;
	int port;

	ps = container_of(work, struct mv88e6xxx_priv_state, bridge_work);
	ds = ((struct dsa_switch *)ps) - 1;

	while (ps->port_state_update_mask) {
		port = __ffs(ps->port_state_update_mask);
		clear_bit(port, &ps->port_state_update_mask);
		mv88e6xxx_set_port_state(ds, port, ps->port_state[port]);
	}
}

int mv88e6xxx_setup_port_common(struct dsa_switch *ds, int port)
{
	struct mv88e6xxx_priv_state *ps = ds_to_priv(ds);
	int ret, fid;

	mutex_lock(&ps->smi_mutex);

	/* Port Control 1: disable trunking, disable sending
	 * learning messages to this port.
	 */
	ret = _mv88e6xxx_reg_write(ds, REG_PORT(port), PORT_CONTROL_1, 0x0000);
	if (ret)
		goto abort;

	/* Port based VLAN map: give each port its own address
	 * database, allow the CPU port to talk to each of the 'real'
	 * ports, and allow each of the 'real' ports to only talk to
	 * the upstream port.
	 */
	fid = __ffs(ps->fid_mask);
	ps->fid[port] = fid;
	ps->fid_mask &= ~(1 << fid);

	if (!dsa_is_cpu_port(ds, port))
		ps->bridge_mask[fid] = 1 << port;

	ret = _mv88e6xxx_update_port_config(ds, port);
	if (ret)
		goto abort;

	/* Default VLAN ID and priority: don't set a default VLAN
	 * ID, and set the default packet priority to zero.
	 */
	ret = _mv88e6xxx_reg_write(ds, REG_PORT(port), PORT_DEFAULT_VLAN,
				   0x0000);
abort:
	mutex_unlock(&ps->smi_mutex);
	return ret;
}

int mv88e6xxx_setup_common(struct dsa_switch *ds)
{
	struct mv88e6xxx_priv_state *ps = ds_to_priv(ds);

	mutex_init(&ps->smi_mutex);
	mutex_init(&ps->stats_mutex);
	mutex_init(&ps->phy_mutex);

	ps->id = REG_READ(REG_PORT(0), PORT_SWITCH_ID) & 0xfff0;

	ps->fid_mask = (1 << DSA_MAX_PORTS) - 1;

	INIT_WORK(&ps->bridge_work, mv88e6xxx_bridge_work);

	return 0;
}

int mv88e6xxx_switch_reset(struct dsa_switch *ds, bool ppu_active)
{
	struct mv88e6xxx_priv_state *ps = ds_to_priv(ds);
	u16 is_reset = (ppu_active ? 0x8800 : 0xc800);
	unsigned long timeout;
	int ret;
	int i;

	/* Set all ports to the disabled state. */
	for (i = 0; i < ps->num_ports; i++) {
		ret = REG_READ(REG_PORT(i), PORT_CONTROL);
		REG_WRITE(REG_PORT(i), PORT_CONTROL, ret & 0xfffc);
	}

	/* Wait for transmit queues to drain. */
	usleep_range(2000, 4000);

	/* Reset the switch. Keep the PPU active if requested. The PPU
	 * needs to be active to support indirect phy register access
	 * through global registers 0x18 and 0x19.
	 */
	if (ppu_active)
		REG_WRITE(REG_GLOBAL, 0x04, 0xc000);
	else
		REG_WRITE(REG_GLOBAL, 0x04, 0xc400);

	/* Wait up to one second for reset to complete. */
	timeout = jiffies + 1 * HZ;
	while (time_before(jiffies, timeout)) {
		ret = REG_READ(REG_GLOBAL, 0x00);
		if ((ret & is_reset) == is_reset)
			break;
		usleep_range(1000, 2000);
	}
	if (time_after(jiffies, timeout))
		return -ETIMEDOUT;

	return 0;
}

int mv88e6xxx_phy_page_read(struct dsa_switch *ds, int port, int page, int reg)
{
	struct mv88e6xxx_priv_state *ps = ds_to_priv(ds);
	int ret;

	mutex_lock(&ps->phy_mutex);
	ret = _mv88e6xxx_phy_write_indirect(ds, port, 0x16, page);
	if (ret < 0)
		goto error;
	ret = _mv88e6xxx_phy_read_indirect(ds, port, reg);
error:
	_mv88e6xxx_phy_write_indirect(ds, port, 0x16, 0x0);
	mutex_unlock(&ps->phy_mutex);
	return ret;
}

int mv88e6xxx_phy_page_write(struct dsa_switch *ds, int port, int page,
			     int reg, int val)
{
	struct mv88e6xxx_priv_state *ps = ds_to_priv(ds);
	int ret;

	mutex_lock(&ps->phy_mutex);
	ret = _mv88e6xxx_phy_write_indirect(ds, port, 0x16, page);
	if (ret < 0)
		goto error;

	ret = _mv88e6xxx_phy_write_indirect(ds, port, reg, val);
error:
	_mv88e6xxx_phy_write_indirect(ds, port, 0x16, 0x0);
	mutex_unlock(&ps->phy_mutex);
	return ret;
}

static int mv88e6xxx_port_to_phy_addr(struct dsa_switch *ds, int port)
{
	struct mv88e6xxx_priv_state *ps = ds_to_priv(ds);

	if (port >= 0 && port < ps->num_ports)
		return port;
	return -EINVAL;
}

int
mv88e6xxx_phy_read(struct dsa_switch *ds, int port, int regnum)
{
	struct mv88e6xxx_priv_state *ps = ds_to_priv(ds);
	int addr = mv88e6xxx_port_to_phy_addr(ds, port);
	int ret;

	if (addr < 0)
		return addr;

	mutex_lock(&ps->phy_mutex);
	ret = _mv88e6xxx_phy_read(ds, addr, regnum);
	mutex_unlock(&ps->phy_mutex);
	return ret;
}

int
mv88e6xxx_phy_write(struct dsa_switch *ds, int port, int regnum, u16 val)
{
	struct mv88e6xxx_priv_state *ps = ds_to_priv(ds);
	int addr = mv88e6xxx_port_to_phy_addr(ds, port);
	int ret;

	if (addr < 0)
		return addr;

	mutex_lock(&ps->phy_mutex);
	ret = _mv88e6xxx_phy_write(ds, addr, regnum, val);
	mutex_unlock(&ps->phy_mutex);
	return ret;
}

int
mv88e6xxx_phy_read_indirect(struct dsa_switch *ds, int port, int regnum)
{
	struct mv88e6xxx_priv_state *ps = ds_to_priv(ds);
	int addr = mv88e6xxx_port_to_phy_addr(ds, port);
	int ret;

	if (addr < 0)
		return addr;

	mutex_lock(&ps->phy_mutex);
	ret = _mv88e6xxx_phy_read_indirect(ds, addr, regnum);
	mutex_unlock(&ps->phy_mutex);
	return ret;
}

int
mv88e6xxx_phy_write_indirect(struct dsa_switch *ds, int port, int regnum,
			     u16 val)
{
	struct mv88e6xxx_priv_state *ps = ds_to_priv(ds);
	int addr = mv88e6xxx_port_to_phy_addr(ds, port);
	int ret;

	if (addr < 0)
		return addr;

	mutex_lock(&ps->phy_mutex);
	ret = _mv88e6xxx_phy_write_indirect(ds, addr, regnum, val);
	mutex_unlock(&ps->phy_mutex);
	return ret;
}

static int __init mv88e6xxx_init(void)
{
#if IS_ENABLED(CONFIG_NET_DSA_MV88E6131)
	register_switch_driver(&mv88e6131_switch_driver);
#endif
#if IS_ENABLED(CONFIG_NET_DSA_MV88E6123_61_65)
	register_switch_driver(&mv88e6123_61_65_switch_driver);
#endif
#if IS_ENABLED(CONFIG_NET_DSA_MV88E6352)
	register_switch_driver(&mv88e6352_switch_driver);
#endif
#if IS_ENABLED(CONFIG_NET_DSA_MV88E6171)
	register_switch_driver(&mv88e6171_switch_driver);
#endif
	return 0;
}
module_init(mv88e6xxx_init);

static void __exit mv88e6xxx_cleanup(void)
{
#if IS_ENABLED(CONFIG_NET_DSA_MV88E6171)
	unregister_switch_driver(&mv88e6171_switch_driver);
#endif
#if IS_ENABLED(CONFIG_NET_DSA_MV88E6352)
	unregister_switch_driver(&mv88e6352_switch_driver);
#endif
#if IS_ENABLED(CONFIG_NET_DSA_MV88E6123_61_65)
	unregister_switch_driver(&mv88e6123_61_65_switch_driver);
#endif
#if IS_ENABLED(CONFIG_NET_DSA_MV88E6131)
	unregister_switch_driver(&mv88e6131_switch_driver);
#endif
}
module_exit(mv88e6xxx_cleanup);

MODULE_AUTHOR("Lennert Buytenhek <buytenh@wantstofly.org>");
MODULE_DESCRIPTION("Driver for Marvell 88E6XXX ethernet switch chips");
MODULE_LICENSE("GPL");
