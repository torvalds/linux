// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Driver for Motorcomm YT921x Switch
 *
 * Should work on YT9213/YT9214/YT9215/YT9218, but only tested on YT9215+SGMII,
 * be sure to do your own checks before porting to another chip.
 *
 * Copyright (c) 2025 David Yang
 */

#include <linux/dcbnl.h>
#include <linux/etherdevice.h>
#include <linux/if_bridge.h>
#include <linux/if_hsr.h>
#include <linux/if_vlan.h>
#include <linux/mdio.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_mdio.h>
#include <linux/of_net.h>
#include <linux/sort.h>
#include <linux/u64_stats_sync.h>

#include <net/dsa.h>
#include <net/dscp.h>
#include <net/ieee8021q.h>

#include "chip.h"
#include "leds.h"
#include "mdio_bus.h"
#include "mib.h"
#include "pcs.h"
#include "smi.h"
#include "tc.h"

static const struct yt921x_info yt921x_infos[] = {
	{
		"YT9215SC", YT9215_MAJOR, 1, 0,
		GENMASK(4, 0),
		BIT(9),
		BIT(8) | BIT(9),
	},
	{
		"YT9215S", YT9215_MAJOR, 2, 0,
		GENMASK(4, 0),
		BIT(9),
		BIT(8),
	},
	{
		"YT9215RB", YT9215_MAJOR, 3, 0,
		GENMASK(4, 0),
		BIT(8) | BIT(9),
		0,
	},
	{
		"YT9214NB", YT9215_MAJOR, 3, 2,
		BIT(1) | BIT(3),
		BIT(9),
		BIT(8),
	},
	{
		"YT9213NB", YT9215_MAJOR, 3, 3,
		BIT(1) | BIT(3),
		BIT(9),
		BIT(9),
	},
	{
		"YT9218N", YT9218_MAJOR, 0, 0,
		GENMASK(7, 0),
		0,
		0,
	},
	{
		"YT9218MB", YT9218_MAJOR, 1, 0,
		GENMASK(7, 0),
		BIT(8) | BIT(9),
		BIT(8) | BIT(9),
	},
	{}
};

#define YT921X_VID_UNWARE	4095

struct yt921x_reg_mdio {
	struct mii_bus *bus;
	int addr;
	/* SWITCH_ID_1 / SWITCH_ID_0 of the device
	 *
	 * This is a way to multiplex multiple devices on the same MII phyaddr
	 * and should be configurable in DT. However, MDIO core simply doesn't
	 * allow multiple devices over one reg addr, so this is a fixed value
	 * for now until a solution is found.
	 *
	 * Keep this because we need switchid to form MII regaddrs anyway.
	 */
	unsigned char switchid;
};

/* TODO: SPI/I2C */

static int yt921x_reg_mdio_read(void *context, u32 reg, u32 *valp)
{
	struct yt921x_reg_mdio *mdio = context;
	struct mii_bus *bus = mdio->bus;
	int addr = mdio->addr;
	u32 reg_addr;
	u32 reg_data;
	u32 val;
	int res;

	/* Hold the mdio bus lock to avoid (un)locking for 4 times */
	mutex_lock_nested(&bus->mdio_lock, MDIO_MUTEX_NESTED);

	reg_addr = YT921X_SMI_SWITCHID(mdio->switchid) | YT921X_SMI_ADDR |
		   YT921X_SMI_READ;
	res = __mdiobus_write(bus, addr, reg_addr, (u16)(reg >> 16));
	if (res)
		goto end;
	res = __mdiobus_write(bus, addr, reg_addr, (u16)reg);
	if (res)
		goto end;

	reg_data = YT921X_SMI_SWITCHID(mdio->switchid) | YT921X_SMI_DATA |
		   YT921X_SMI_READ;
	res = __mdiobus_read(bus, addr, reg_data);
	if (res < 0)
		goto end;
	val = (u16)res;
	res = __mdiobus_read(bus, addr, reg_data);
	if (res < 0)
		goto end;
	val = (val << 16) | (u16)res;

	*valp = val;
	res = 0;

end:
	mutex_unlock(&bus->mdio_lock);
	return res;
}

static int yt921x_reg_mdio_write(void *context, u32 reg, u32 val)
{
	struct yt921x_reg_mdio *mdio = context;
	struct mii_bus *bus = mdio->bus;
	int addr = mdio->addr;
	u32 reg_addr;
	u32 reg_data;
	int res;

	mutex_lock_nested(&bus->mdio_lock, MDIO_MUTEX_NESTED);

	reg_addr = YT921X_SMI_SWITCHID(mdio->switchid) | YT921X_SMI_ADDR |
		   YT921X_SMI_WRITE;
	res = __mdiobus_write(bus, addr, reg_addr, (u16)(reg >> 16));
	if (res)
		goto end;
	res = __mdiobus_write(bus, addr, reg_addr, (u16)reg);
	if (res)
		goto end;

	reg_data = YT921X_SMI_SWITCHID(mdio->switchid) | YT921X_SMI_DATA |
		   YT921X_SMI_WRITE;
	res = __mdiobus_write(bus, addr, reg_data, (u16)(val >> 16));
	if (res)
		goto end;
	res = __mdiobus_write(bus, addr, reg_data, (u16)val);
	if (res)
		goto end;

	res = 0;

end:
	mutex_unlock(&bus->mdio_lock);
	return res;
}

static const struct yt921x_reg_ops yt921x_reg_ops_mdio = {
	.read = yt921x_reg_mdio_read,
	.write = yt921x_reg_mdio_write,
};

/* TODO: SPI/I2C */

static int
yt921x_set_eee(struct yt921x_priv *priv, int port, struct ethtool_keee *e)
{
	/* Poor datasheet for EEE operations; don't ask if you are confused */

	bool enable = e->eee_enabled;
	u16 new_mask;
	int res;

	/* Enable / disable global EEE */
	new_mask = priv->eee_ports_mask;
	new_mask &= ~BIT(port);
	new_mask |= !enable ? 0 : BIT(port);

	if (!!new_mask != !!priv->eee_ports_mask) {
		res = yt921x_reg_toggle_bits(priv, YT921X_PON_STRAP_FUNC,
					     YT921X_PON_STRAP_EEE, !!new_mask);
		if (res)
			return res;
		res = yt921x_reg_toggle_bits(priv, YT921X_PON_STRAP_VAL,
					     YT921X_PON_STRAP_EEE, !!new_mask);
		if (res)
			return res;
	}

	priv->eee_ports_mask = new_mask;

	/* Enable / disable port EEE */
	res = yt921x_reg_toggle_bits(priv, YT921X_EEE_CTRL,
				     YT921X_EEE_CTRL_ENn(port), enable);
	if (res)
		return res;
	res = yt921x_reg_toggle_bits(priv, YT921X_EEEn_VAL(port),
				     YT921X_EEE_VAL_DATA, enable);
	if (res)
		return res;

	return 0;
}

static int
yt921x_dsa_set_mac_eee(struct dsa_switch *ds, int port, struct ethtool_keee *e)
{
	struct yt921x_priv *priv = dsa_to_yt921x_priv(ds);
	int res;

	mutex_lock(&priv->reg_lock);
	res = yt921x_set_eee(priv, port, e);
	mutex_unlock(&priv->reg_lock);

	return res;
}

static int
yt921x_dsa_port_change_mtu(struct dsa_switch *ds, int port, int new_mtu)
{
	/* Only serves as packet filter, since the frame size is always set to
	 * maximum after reset
	 */

	struct yt921x_priv *priv = dsa_to_yt921x_priv(ds);
	struct dsa_port *dp = dsa_to_port(ds, port);
	int frame_size;
	int res;

	frame_size = new_mtu + ETH_HLEN + ETH_FCS_LEN;
	if (dsa_port_is_cpu(dp))
		frame_size += YT921X_TAG_LEN;

	mutex_lock(&priv->reg_lock);
	res = yt921x_reg_update_bits(priv, YT921X_MACn_FRAME(port),
				     YT921X_MAC_FRAME_SIZE_M,
				     YT921X_MAC_FRAME_SIZE(frame_size));
	mutex_unlock(&priv->reg_lock);

	return res;
}

static int yt921x_dsa_port_max_mtu(struct dsa_switch *ds, int port)
{
	/* Only called for user ports, exclude tag len here */
	return YT921X_FRAME_SIZE_MAX - ETH_HLEN - ETH_FCS_LEN - YT921X_TAG_LEN;
}

static int yt921x_lag_hash(struct yt921x_priv *priv, u32 ctrl, bool unique_lag,
			   struct netlink_ext_ack *extack)
{
	u32 val;
	int res;

	/* Hash Mode is global. Make sure the same Hash Mode is set to all the
	 * 2 possible lags.
	 * If we are the unique LAG we can set whatever hash mode we want.
	 * To change hash mode it's needed to remove all LAG and change the mode
	 * with the latest.
	 */
	if (unique_lag) {
		res = yt921x_reg_write(priv, YT921X_LAG_HASH, ctrl);
		if (res)
			return res;
	} else {
		res = yt921x_reg_read(priv, YT921X_LAG_HASH, &val);
		if (res)
			return res;

		if (val != ctrl) {
			NL_SET_ERR_MSG_MOD(extack,
					   "Mismatched Hash Mode across different lags is not supported");
			return -EOPNOTSUPP;
		}
	}

	return 0;
}

static int yt921x_lag_set(struct yt921x_priv *priv, u8 index, u16 ports_mask)
{
	unsigned long targets_mask = ports_mask;
	unsigned int cnt;
	u32 ctrl;
	int port;
	int res;

	cnt = 0;
	for_each_set_bit(port, &targets_mask, YT921X_PORT_NUM) {
		ctrl = YT921X_LAG_MEMBER_PORT(port);
		res = yt921x_reg_write(priv, YT921X_LAG_MEMBERnm(index, cnt),
				       ctrl);
		if (res)
			return res;

		cnt++;
	}

	ctrl = YT921X_LAG_GROUP_PORTS(ports_mask) |
	       YT921X_LAG_GROUP_MEMBER_NUM(cnt);
	return yt921x_reg_write(priv, YT921X_LAG_GROUPn(index), ctrl);
}

static int
yt921x_dsa_port_lag_leave(struct dsa_switch *ds, int port, struct dsa_lag lag)
{
	struct yt921x_priv *priv = dsa_to_yt921x_priv(ds);
	struct dsa_port *dp;
	u32 ctrl;
	int res;

	if (!lag.id)
		return -EINVAL;

	ctrl = 0;
	dsa_lag_foreach_port(dp, ds->dst, &lag)
		ctrl |= BIT(dp->index);

	mutex_lock(&priv->reg_lock);
	res = yt921x_lag_set(priv, lag.id - 1, ctrl);
	mutex_unlock(&priv->reg_lock);

	return res;
}

static int
yt921x_dsa_port_lag_check(struct dsa_switch *ds, struct dsa_lag lag,
			  struct netdev_lag_upper_info *info,
			  struct netlink_ext_ack *extack)
{
	unsigned int members;
	struct dsa_port *dp;

	if (!lag.id)
		return -EINVAL;

	members = 0;
	dsa_lag_foreach_port(dp, ds->dst, &lag)
		/* Includes the port joining the LAG */
		members++;

	if (members > YT921X_LAG_PORT_NUM) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Cannot offload more than 4 LAG ports");
		return -EOPNOTSUPP;
	}

	if (info->tx_type != NETDEV_LAG_TX_TYPE_HASH) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Can only offload LAG using hash TX type");
		return -EOPNOTSUPP;
	}

	if (info->hash_type != NETDEV_LAG_HASH_L2 &&
	    info->hash_type != NETDEV_LAG_HASH_L23 &&
	    info->hash_type != NETDEV_LAG_HASH_L34) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Can only offload L2 or L2+L3 or L3+L4 TX hash");
		return -EOPNOTSUPP;
	}

	return 0;
}

static int
yt921x_dsa_port_lag_join(struct dsa_switch *ds, int port, struct dsa_lag lag,
			 struct netdev_lag_upper_info *info,
			 struct netlink_ext_ack *extack)
{
	struct yt921x_priv *priv = dsa_to_yt921x_priv(ds);
	struct dsa_port *dp;
	bool unique_lag;
	unsigned int i;
	u32 ctrl;
	int res;

	res = yt921x_dsa_port_lag_check(ds, lag, info, extack);
	if (res)
		return res;

	ctrl = 0;
	switch (info->hash_type) {
	case NETDEV_LAG_HASH_L34:
		ctrl |= YT921X_LAG_HASH_IP_DST;
		ctrl |= YT921X_LAG_HASH_IP_SRC;
		ctrl |= YT921X_LAG_HASH_IP_PROTO;

		ctrl |= YT921X_LAG_HASH_L4_DPORT;
		ctrl |= YT921X_LAG_HASH_L4_SPORT;
		break;
	case NETDEV_LAG_HASH_L23:
		ctrl |= YT921X_LAG_HASH_MAC_DA;
		ctrl |= YT921X_LAG_HASH_MAC_SA;

		ctrl |= YT921X_LAG_HASH_IP_DST;
		ctrl |= YT921X_LAG_HASH_IP_SRC;
		ctrl |= YT921X_LAG_HASH_IP_PROTO;
		break;
	case NETDEV_LAG_HASH_L2:
		ctrl |= YT921X_LAG_HASH_MAC_DA;
		ctrl |= YT921X_LAG_HASH_MAC_SA;
		break;
	default:
		return -EOPNOTSUPP;
	}

	/* Check if we are the unique configured LAG */
	unique_lag = true;
	dsa_lags_foreach_id(i, ds->dst)
		if (i != lag.id && dsa_lag_by_id(ds->dst, i)) {
			unique_lag = false;
			break;
		}

	mutex_lock(&priv->reg_lock);
	do {
		res = yt921x_lag_hash(priv, ctrl, unique_lag, extack);
		if (res)
			break;

		ctrl = 0;
		dsa_lag_foreach_port(dp, ds->dst, &lag)
			ctrl |= BIT(dp->index);
		res = yt921x_lag_set(priv, lag.id - 1, ctrl);
	} while (0);
	mutex_unlock(&priv->reg_lock);

	return res;
}

static int yt921x_fdb_wait(struct yt921x_priv *priv, u32 *valp)
{
	struct device *dev = yt921x_priv_to_device(priv);
	u32 val = YT921X_FDB_RESULT_DONE;
	int res;

	res = yt921x_reg_wait(priv, YT921X_FDB_RESULT, YT921X_FDB_RESULT_DONE,
			      &val);
	if (res) {
		dev_err(dev, "FDB probably stuck\n");
		return res;
	}

	*valp = val;
	return 0;
}

static int
yt921x_fdb_in01(struct yt921x_priv *priv, const unsigned char *addr,
		u16 vid, u32 ctrl1)
{
	u32 ctrl;
	int res;

	ctrl = ethaddr_hi4_to_u32(addr);
	res = yt921x_reg_write(priv, YT921X_FDB_IN0, ctrl);
	if (res)
		return res;

	ctrl = ctrl1 | YT921X_FDB_IO1_FID(vid) | ethaddr_lo2_to_u32(addr);
	return yt921x_reg_write(priv, YT921X_FDB_IN1, ctrl);
}

static int
yt921x_fdb_has(struct yt921x_priv *priv, const unsigned char *addr, u16 vid,
	       u16 *indexp)
{
	u32 ctrl;
	u32 val;
	int res;

	res = yt921x_fdb_in01(priv, addr, vid, 0);
	if (res)
		return res;

	ctrl = 0;
	res = yt921x_reg_write(priv, YT921X_FDB_IN2, ctrl);
	if (res)
		return res;

	ctrl = YT921X_FDB_OP_OP_GET_ONE | YT921X_FDB_OP_START;
	res = yt921x_reg_write(priv, YT921X_FDB_OP, ctrl);
	if (res)
		return res;

	res = yt921x_fdb_wait(priv, &val);
	if (res)
		return res;
	if (val & YT921X_FDB_RESULT_NOTFOUND) {
		*indexp = YT921X_FDB_NUM;
		return 0;
	}

	*indexp = FIELD_GET(YT921X_FDB_RESULT_INDEX_M, val);
	return 0;
}

static int
yt921x_fdb_read(struct yt921x_priv *priv, unsigned char *addr, u16 *vidp,
		u16 *ports_maskp, u16 *indexp, u8 *statusp)
{
	struct device *dev = yt921x_priv_to_device(priv);
	u16 index;
	u32 data0;
	u32 data1;
	u32 data2;
	u32 val;
	int res;

	res = yt921x_fdb_wait(priv, &val);
	if (res)
		return res;
	if (val & YT921X_FDB_RESULT_NOTFOUND) {
		*ports_maskp = 0;
		return 0;
	}
	index = FIELD_GET(YT921X_FDB_RESULT_INDEX_M, val);

	res = yt921x_reg_read(priv, YT921X_FDB_OUT1, &data1);
	if (res)
		return res;
	if ((data1 & YT921X_FDB_IO1_STATUS_M) ==
	    YT921X_FDB_IO1_STATUS_INVALID) {
		*ports_maskp = 0;
		return 0;
	}

	res = yt921x_reg_read(priv, YT921X_FDB_OUT0, &data0);
	if (res)
		return res;
	res = yt921x_reg_read(priv, YT921X_FDB_OUT2, &data2);
	if (res)
		return res;

	addr[0] = data0 >> 24;
	addr[1] = data0 >> 16;
	addr[2] = data0 >> 8;
	addr[3] = data0;
	addr[4] = data1 >> 8;
	addr[5] = data1;
	*vidp = FIELD_GET(YT921X_FDB_IO1_FID_M, data1);
	*indexp = index;
	*ports_maskp = FIELD_GET(YT921X_FDB_IO2_EGR_PORTS_M, data2);
	*statusp = FIELD_GET(YT921X_FDB_IO1_STATUS_M, data1);

	dev_dbg(dev,
		"%s: index 0x%x, mac %02x:%02x:%02x:%02x:%02x:%02x, vid %d, ports 0x%x, status %d\n",
		__func__, *indexp, addr[0], addr[1], addr[2], addr[3],
		addr[4], addr[5], *vidp, *ports_maskp, *statusp);
	return 0;
}

static int
yt921x_fdb_dump(struct yt921x_priv *priv, u16 ports_mask,
		dsa_fdb_dump_cb_t *cb, void *data)
{
	unsigned char addr[ETH_ALEN];
	u8 status;
	u16 pmask;
	u16 index;
	u32 ctrl;
	u16 vid;
	int res;

	ctrl = YT921X_FDB_OP_INDEX(0) | YT921X_FDB_OP_MODE_INDEX |
	       YT921X_FDB_OP_OP_GET_ONE | YT921X_FDB_OP_START;
	res = yt921x_reg_write(priv, YT921X_FDB_OP, ctrl);
	if (res)
		return res;
	res = yt921x_fdb_read(priv, addr, &vid, &pmask, &index, &status);
	if (res)
		return res;
	if ((pmask & ports_mask) && !is_multicast_ether_addr(addr)) {
		res = cb(addr, vid,
			 status == YT921X_FDB_ENTRY_STATUS_STATIC, data);
		if (res)
			return res;
	}

	ctrl = YT921X_FDB_IO2_EGR_PORTS(ports_mask);
	res = yt921x_reg_write(priv, YT921X_FDB_IN2, ctrl);
	if (res)
		return res;

	index = 0;
	do {
		ctrl = YT921X_FDB_OP_INDEX(index) | YT921X_FDB_OP_MODE_INDEX |
		       YT921X_FDB_OP_NEXT_TYPE_UCAST_PORT |
		       YT921X_FDB_OP_OP_GET_NEXT | YT921X_FDB_OP_START;
		res = yt921x_reg_write(priv, YT921X_FDB_OP, ctrl);
		if (res)
			return res;

		res = yt921x_fdb_read(priv, addr, &vid, &pmask, &index,
				      &status);
		if (res)
			return res;
		if (!pmask)
			break;

		if ((pmask & ports_mask) && !is_multicast_ether_addr(addr)) {
			res = cb(addr, vid,
				 status == YT921X_FDB_ENTRY_STATUS_STATIC,
				 data);
			if (res)
				return res;
		}

		/* Never call GET_NEXT with 4095, otherwise it will hang
		 * forever until a reset!
		 */
	} while (index < YT921X_FDB_NUM - 1);

	return 0;
}

static int
yt921x_fdb_flush_raw(struct yt921x_priv *priv, u16 ports_mask, u16 vid,
		     bool flush_static)
{
	u32 ctrl;
	u32 val;
	int res;

	if (vid < 4096) {
		ctrl = YT921X_FDB_IO1_FID(vid);
		res = yt921x_reg_write(priv, YT921X_FDB_IN1, ctrl);
		if (res)
			return res;
	}

	ctrl = YT921X_FDB_IO2_EGR_PORTS(ports_mask);
	res = yt921x_reg_write(priv, YT921X_FDB_IN2, ctrl);
	if (res)
		return res;

	ctrl = YT921X_FDB_OP_OP_FLUSH | YT921X_FDB_OP_START;
	if (vid >= 4096)
		ctrl |= YT921X_FDB_OP_FLUSH_PORT;
	else
		ctrl |= YT921X_FDB_OP_FLUSH_PORT_VID;
	if (flush_static)
		ctrl |= YT921X_FDB_OP_FLUSH_STATIC;
	res = yt921x_reg_write(priv, YT921X_FDB_OP, ctrl);
	if (res)
		return res;

	res = yt921x_fdb_wait(priv, &val);
	if (res)
		return res;

	return 0;
}

static int
yt921x_fdb_flush_port(struct yt921x_priv *priv, int port, bool flush_static)
{
	return yt921x_fdb_flush_raw(priv, BIT(port), 4096, flush_static);
}

static int
yt921x_fdb_add_index_in12(struct yt921x_priv *priv, u16 index, u16 ctrl1,
			  u16 ctrl2)
{
	u32 ctrl;
	u32 val;
	int res;

	res = yt921x_reg_write(priv, YT921X_FDB_IN1, ctrl1);
	if (res)
		return res;
	res = yt921x_reg_write(priv, YT921X_FDB_IN2, ctrl2);
	if (res)
		return res;

	ctrl = YT921X_FDB_OP_INDEX(index) | YT921X_FDB_OP_MODE_INDEX |
	       YT921X_FDB_OP_OP_ADD | YT921X_FDB_OP_START;
	res = yt921x_reg_write(priv, YT921X_FDB_OP, ctrl);
	if (res)
		return res;

	return yt921x_fdb_wait(priv, &val);
}

static int
yt921x_fdb_add(struct yt921x_priv *priv, const unsigned char *addr, u16 vid,
	       u16 ports_mask)
{
	u32 ctrl;
	u32 val;
	int res;

	ctrl = YT921X_FDB_IO1_STATUS_STATIC;
	res = yt921x_fdb_in01(priv, addr, vid, ctrl);
	if (res)
		return res;

	ctrl = YT921X_FDB_IO2_EGR_PORTS(ports_mask);
	res = yt921x_reg_write(priv, YT921X_FDB_IN2, ctrl);
	if (res)
		return res;

	ctrl = YT921X_FDB_OP_OP_ADD | YT921X_FDB_OP_START;
	res = yt921x_reg_write(priv, YT921X_FDB_OP, ctrl);
	if (res)
		return res;

	return yt921x_fdb_wait(priv, &val);
}

static int
yt921x_fdb_leave(struct yt921x_priv *priv, const unsigned char *addr,
		 u16 vid, u16 ports_mask)
{
	u16 index;
	u32 ctrl1;
	u32 ctrl2;
	u32 ctrl;
	u32 val2;
	u32 val;
	int res;

	/* Check for presence */
	res = yt921x_fdb_has(priv, addr, vid, &index);
	if (res)
		return res;
	if (index >= YT921X_FDB_NUM)
		return 0;

	/* Check if action required */
	res = yt921x_reg_read(priv, YT921X_FDB_OUT2, &val2);
	if (res)
		return res;

	ctrl2 = val2 & ~YT921X_FDB_IO2_EGR_PORTS(ports_mask);
	if (ctrl2 == val2)
		return 0;
	if (!(ctrl2 & YT921X_FDB_IO2_EGR_PORTS_M)) {
		ctrl = YT921X_FDB_OP_OP_DEL | YT921X_FDB_OP_START;
		res = yt921x_reg_write(priv, YT921X_FDB_OP, ctrl);
		if (res)
			return res;

		return yt921x_fdb_wait(priv, &val);
	}

	res = yt921x_reg_read(priv, YT921X_FDB_OUT1, &ctrl1);
	if (res)
		return res;

	return yt921x_fdb_add_index_in12(priv, index, ctrl1, ctrl2);
}

static int
yt921x_fdb_join(struct yt921x_priv *priv, const unsigned char *addr, u16 vid,
		u16 ports_mask)
{
	u16 index;
	u32 ctrl1;
	u32 ctrl2;
	u32 val1;
	u32 val2;
	int res;

	/* Check for presence */
	res = yt921x_fdb_has(priv, addr, vid, &index);
	if (res)
		return res;
	if (index >= YT921X_FDB_NUM)
		return yt921x_fdb_add(priv, addr, vid, ports_mask);

	/* Check if action required */
	res = yt921x_reg_read(priv, YT921X_FDB_OUT1, &val1);
	if (res)
		return res;
	res = yt921x_reg_read(priv, YT921X_FDB_OUT2, &val2);
	if (res)
		return res;

	ctrl1 = val1 & ~YT921X_FDB_IO1_STATUS_M;
	ctrl1 |= YT921X_FDB_IO1_STATUS_STATIC;
	ctrl2 = val2 | YT921X_FDB_IO2_EGR_PORTS(ports_mask);
	if (ctrl1 == val1 && ctrl2 == val2)
		return 0;

	return yt921x_fdb_add_index_in12(priv, index, ctrl1, ctrl2);
}

static int
yt921x_dsa_port_fdb_dump(struct dsa_switch *ds, int port,
			 dsa_fdb_dump_cb_t *cb, void *data)
{
	struct yt921x_priv *priv = dsa_to_yt921x_priv(ds);
	int res;

	mutex_lock(&priv->reg_lock);
	/* Hardware FDB is shared for fdb and mdb, "bridge fdb show"
	 * only wants to see unicast
	 */
	res = yt921x_fdb_dump(priv, BIT(port), cb, data);
	mutex_unlock(&priv->reg_lock);

	return res;
}

static void yt921x_dsa_port_fast_age(struct dsa_switch *ds, int port)
{
	struct yt921x_priv *priv = dsa_to_yt921x_priv(ds);
	struct device *dev = yt921x_priv_to_device(priv);
	int res;

	mutex_lock(&priv->reg_lock);
	res = yt921x_fdb_flush_port(priv, port, false);
	mutex_unlock(&priv->reg_lock);

	if (res)
		dev_err(dev, "Failed to %s port %d: %i\n", "clear FDB for",
			port, res);
}

static int
yt921x_dsa_set_ageing_time(struct dsa_switch *ds, unsigned int msecs)
{
	struct yt921x_priv *priv = dsa_to_yt921x_priv(ds);
	u32 ctrl;
	int res;

	/* AGEING reg is set in 5s step */
	ctrl = clamp(msecs / 5000, 1, U16_MAX);

	mutex_lock(&priv->reg_lock);
	res = yt921x_reg_write(priv, YT921X_AGEING, ctrl);
	mutex_unlock(&priv->reg_lock);

	return res;
}

static int
yt921x_dsa_port_fdb_del(struct dsa_switch *ds, int port,
			const unsigned char *addr, u16 vid, struct dsa_db db)
{
	struct yt921x_priv *priv = dsa_to_yt921x_priv(ds);
	int res;

	mutex_lock(&priv->reg_lock);
	res = yt921x_fdb_leave(priv, addr, vid, BIT(port));
	mutex_unlock(&priv->reg_lock);

	return res;
}

static int
yt921x_dsa_port_fdb_add(struct dsa_switch *ds, int port,
			const unsigned char *addr, u16 vid, struct dsa_db db)
{
	struct yt921x_priv *priv = dsa_to_yt921x_priv(ds);
	int res;

	mutex_lock(&priv->reg_lock);
	res = yt921x_fdb_join(priv, addr, vid, BIT(port));
	mutex_unlock(&priv->reg_lock);

	return res;
}

static int
yt921x_dsa_port_mdb_del(struct dsa_switch *ds, int port,
			const struct switchdev_obj_port_mdb *mdb,
			struct dsa_db db)
{
	struct yt921x_priv *priv = dsa_to_yt921x_priv(ds);
	const unsigned char *addr = mdb->addr;
	u16 vid = mdb->vid;
	int res;

	mutex_lock(&priv->reg_lock);
	res = yt921x_fdb_leave(priv, addr, vid, BIT(port));
	mutex_unlock(&priv->reg_lock);

	return res;
}

static int
yt921x_dsa_port_mdb_add(struct dsa_switch *ds, int port,
			const struct switchdev_obj_port_mdb *mdb,
			struct dsa_db db)
{
	struct yt921x_priv *priv = dsa_to_yt921x_priv(ds);
	const unsigned char *addr = mdb->addr;
	u16 vid = mdb->vid;
	int res;

	mutex_lock(&priv->reg_lock);
	res = yt921x_fdb_join(priv, addr, vid, BIT(port));
	mutex_unlock(&priv->reg_lock);

	return res;
}

static int
yt921x_vlan_aware_set(struct yt921x_priv *priv, int port, bool vlan_aware)
{
	u32 ctrl;

	/* Abuse SVLAN for PCP parsing without polluting the FDB - it just works
	 * despite YT921X_VLAN_CTRL_SVLAN_EN never being set
	 */
	if (!vlan_aware)
		ctrl = YT921X_PORT_IGR_TPIDn_STAG(0);
	else
		ctrl = YT921X_PORT_IGR_TPIDn_CTAG(0);
	return yt921x_reg_write(priv, YT921X_PORTn_IGR_TPID(port), ctrl);
}

static int
yt921x_port_set_pvid(struct yt921x_priv *priv, int port, u16 vid)
{
	u32 mask;
	u32 ctrl;

	mask = YT921X_PORT_VLAN_CTRL_CVID_M;
	ctrl = YT921X_PORT_VLAN_CTRL_CVID(vid);
	return yt921x_reg_update_bits(priv, YT921X_PORTn_VLAN_CTRL(port),
				      mask, ctrl);
}

static int
yt921x_vlan_filtering(struct yt921x_priv *priv, int port, bool vlan_filtering)
{
	struct dsa_port *dp = dsa_to_port(&priv->ds, port);
	struct net_device *bdev;
	u16 pvid;
	u32 mask;
	u32 ctrl;
	int res;

	bdev = dsa_port_bridge_dev_get(dp);

	if (!bdev || !vlan_filtering)
		pvid = YT921X_VID_UNWARE;
	else
		br_vlan_get_pvid(bdev, &pvid);
	res = yt921x_port_set_pvid(priv, port, pvid);
	if (res)
		return res;

	mask = YT921X_PORT_VLAN_CTRL1_CVLAN_DROP_TAGGED |
	       YT921X_PORT_VLAN_CTRL1_CVLAN_DROP_UNTAGGED;
	ctrl = 0;
	/* Do not drop tagged frames here; let VLAN_IGR_FILTER do it */
	if (vlan_filtering && !pvid)
		ctrl |= YT921X_PORT_VLAN_CTRL1_CVLAN_DROP_UNTAGGED;
	res = yt921x_reg_update_bits(priv, YT921X_PORTn_VLAN_CTRL1(port),
				     mask, ctrl);
	if (res)
		return res;

	res = yt921x_reg_toggle_bits(priv, YT921X_VLAN_IGR_FILTER,
				     YT921X_VLAN_IGR_FILTER_PORTn(port),
				     vlan_filtering);
	if (res)
		return res;

	res = yt921x_vlan_aware_set(priv, port, vlan_filtering);
	if (res)
		return res;

	return 0;
}

static int yt921x_vlan_del(struct yt921x_priv *priv, int port, u16 vid)
{
	u32 masks[2];

	masks[0] = YT921X_VLAN_CTRLa_PORTn(port);
	masks[1] = YT921X_VLAN_CTRLb_UNTAG_PORTn(port);

	return yt921x_reg64_clear_bits(priv, YT921X_VLANn_CTRL(vid), masks);
}

static int
yt921x_vlan_add(struct yt921x_priv *priv, int port, u16 vid, bool untagged)
{
	u32 masks[2];
	u32 ctrls[2];

	masks[0] = YT921X_VLAN_CTRLa_PORTn(port) |
		   YT921X_VLAN_CTRLa_PORTS(priv->cpu_ports_mask);
	ctrls[0] = masks[0];

	masks[1] = YT921X_VLAN_CTRLb_UNTAG_PORTn(port);
	ctrls[1] = untagged ? masks[1] : 0;

	return yt921x_reg64_update_bits(priv, YT921X_VLANn_CTRL(vid),
					masks, ctrls);
}

static int
yt921x_pvid_clear(struct yt921x_priv *priv, int port)
{
	struct dsa_port *dp = dsa_to_port(&priv->ds, port);
	bool vlan_filtering;
	u32 mask;
	int res;

	vlan_filtering = dsa_port_is_vlan_filtering(dp);

	res = yt921x_port_set_pvid(priv, port,
				   vlan_filtering ? 0 : YT921X_VID_UNWARE);
	if (res)
		return res;

	if (vlan_filtering) {
		mask = YT921X_PORT_VLAN_CTRL1_CVLAN_DROP_UNTAGGED;
		res = yt921x_reg_set_bits(priv, YT921X_PORTn_VLAN_CTRL1(port),
					  mask);
		if (res)
			return res;
	}

	return 0;
}

static int
yt921x_pvid_set(struct yt921x_priv *priv, int port, u16 vid)
{
	struct dsa_port *dp = dsa_to_port(&priv->ds, port);
	bool vlan_filtering;
	u32 mask;
	int res;

	vlan_filtering = dsa_port_is_vlan_filtering(dp);

	if (vlan_filtering) {
		res = yt921x_port_set_pvid(priv, port, vid);
		if (res)
			return res;
	}

	mask = YT921X_PORT_VLAN_CTRL1_CVLAN_DROP_UNTAGGED;
	res = yt921x_reg_clear_bits(priv, YT921X_PORTn_VLAN_CTRL1(port), mask);
	if (res)
		return res;

	return 0;
}

static int
yt921x_dsa_port_vlan_filtering(struct dsa_switch *ds, int port,
			       bool vlan_filtering,
			       struct netlink_ext_ack *extack)
{
	struct yt921x_priv *priv = dsa_to_yt921x_priv(ds);
	int res;

	if (dsa_is_cpu_port(ds, port))
		return 0;

	mutex_lock(&priv->reg_lock);
	res = yt921x_vlan_filtering(priv, port, vlan_filtering);
	mutex_unlock(&priv->reg_lock);

	return res;
}

static int
yt921x_dsa_port_vlan_del(struct dsa_switch *ds, int port,
			 const struct switchdev_obj_port_vlan *vlan)
{
	struct yt921x_priv *priv = dsa_to_yt921x_priv(ds);
	u16 vid = vlan->vid;
	u16 pvid;
	int res;

	if (dsa_is_cpu_port(ds, port))
		return 0;

	mutex_lock(&priv->reg_lock);
	do {
		struct dsa_port *dp = dsa_to_port(ds, port);
		struct net_device *bdev;

		res = yt921x_vlan_del(priv, port, vid);
		if (res)
			break;

		bdev = dsa_port_bridge_dev_get(dp);
		if (bdev) {
			br_vlan_get_pvid(bdev, &pvid);
			if (pvid == vid)
				res = yt921x_pvid_clear(priv, port);
		}
	} while (0);
	mutex_unlock(&priv->reg_lock);

	return res;
}

static int
yt921x_dsa_port_vlan_add(struct dsa_switch *ds, int port,
			 const struct switchdev_obj_port_vlan *vlan,
			 struct netlink_ext_ack *extack)
{
	struct yt921x_priv *priv = dsa_to_yt921x_priv(ds);
	u16 vid = vlan->vid;
	u16 pvid;
	int res;

	/* CPU port is supposed to be a member of every VLAN; see
	 * yt921x_vlan_add() and yt921x_port_setup()
	 */
	if (dsa_is_cpu_port(ds, port))
		return 0;

	mutex_lock(&priv->reg_lock);
	do {
		struct dsa_port *dp = dsa_to_port(ds, port);
		struct net_device *bdev;

		res = yt921x_vlan_add(priv, port, vid,
				      vlan->flags & BRIDGE_VLAN_INFO_UNTAGGED);
		if (res)
			break;

		bdev = dsa_port_bridge_dev_get(dp);
		if (bdev) {
			if (vlan->flags & BRIDGE_VLAN_INFO_PVID) {
				res = yt921x_pvid_set(priv, port, vid);
			} else {
				br_vlan_get_pvid(bdev, &pvid);
				if (pvid == vid)
					res = yt921x_pvid_clear(priv, port);
			}
		}
	} while (0);
	mutex_unlock(&priv->reg_lock);

	return res;
}

static int yt921x_userport_standalone(struct yt921x_priv *priv, int port)
{
	u32 mask;
	u32 ctrl;
	int res;

	ctrl = ~priv->cpu_ports_mask;
	res = yt921x_reg_write(priv, YT921X_PORTn_ISOLATION(port), ctrl);
	if (res)
		return res;

	/* Turn off FDB learning to prevent FDB pollution */
	mask = YT921X_PORT_LEARN_DIS;
	res = yt921x_reg_set_bits(priv, YT921X_PORTn_LEARN(port), mask);
	if (res)
		return res;

	/* Turn off VLAN awareness */
	res = yt921x_vlan_aware_set(priv, port, false);
	if (res)
		return res;

	/* Unrelated since learning is off and all packets are trapped;
	 * set it anyway
	 */
	res = yt921x_port_set_pvid(priv, port, YT921X_VID_UNWARE);
	if (res)
		return res;

	return 0;
}

static int yt921x_userport_bridge(struct yt921x_priv *priv, int port)
{
	u32 mask;
	int res;

	mask = YT921X_PORT_LEARN_DIS;
	res = yt921x_reg_clear_bits(priv, YT921X_PORTn_LEARN(port), mask);
	if (res)
		return res;

	return 0;
}

static int yt921x_isolate(struct yt921x_priv *priv, int port)
{
	u32 mask;
	int res;

	mask = BIT(port);
	for (int i = 0; i < YT921X_PORT_NUM; i++) {
		if ((BIT(i) & priv->cpu_ports_mask) || i == port)
			continue;

		res = yt921x_reg_set_bits(priv, YT921X_PORTn_ISOLATION(i),
					  mask);
		if (res)
			return res;
	}

	return 0;
}

/* Make sure to include the CPU port in ports_mask, or your bridge will
 * not have it.
 */
static int yt921x_bridge(struct yt921x_priv *priv, u16 ports_mask)
{
	unsigned long targets_mask = ports_mask & ~priv->cpu_ports_mask;
	u32 isolated_mask;
	u32 ctrl;
	int port;
	int res;

	isolated_mask = 0;
	for_each_set_bit(port, &targets_mask, YT921X_PORT_NUM) {
		struct yt921x_port *pp = &priv->ports[port];

		if (pp->isolated)
			isolated_mask |= BIT(port);
	}

	/* Block from non-cpu bridge ports ... */
	for_each_set_bit(port, &targets_mask, YT921X_PORT_NUM) {
		struct yt921x_port *pp = &priv->ports[port];

		/* to non-bridge ports */
		ctrl = ~ports_mask;
		/* to isolated ports when isolated */
		if (pp->isolated)
			ctrl |= isolated_mask;
		/* to itself when non-hairpin */
		if (!pp->hairpin)
			ctrl |= BIT(port);
		else
			ctrl &= ~BIT(port);

		res = yt921x_reg_write(priv, YT921X_PORTn_ISOLATION(port),
				       ctrl);
		if (res)
			return res;
	}

	return 0;
}

static int yt921x_bridge_leave(struct yt921x_priv *priv, int port)
{
	int res;

	res = yt921x_userport_standalone(priv, port);
	if (res)
		return res;

	res = yt921x_isolate(priv, port);
	if (res)
		return res;

	return 0;
}

static int
yt921x_bridge_join(struct yt921x_priv *priv, int port, u16 ports_mask)
{
	int res;

	res = yt921x_userport_bridge(priv, port);
	if (res)
		return res;

	res = yt921x_bridge(priv, ports_mask);
	if (res)
		return res;

	return 0;
}

static int
yt921x_bridge_flags(struct yt921x_priv *priv, int port,
		    struct switchdev_brport_flags flags)
{
	struct yt921x_port *pp = &priv->ports[port];
	bool do_flush;
	u32 mask;
	int res;

	if (flags.mask & BR_LEARNING) {
		bool learning = flags.val & BR_LEARNING;

		mask = YT921X_PORT_LEARN_DIS;
		res = yt921x_reg_toggle_bits(priv, YT921X_PORTn_LEARN(port),
					     mask, !learning);
		if (res)
			return res;
	}

	/* BR_FLOOD, BR_MCAST_FLOOD: see the comment where ACT_UNK_ACTn_TRAP
	 * is set
	 */

	/* BR_BCAST_FLOOD: we can filter bcast, but cannot trap them */

	do_flush = false;
	if (flags.mask & BR_HAIRPIN_MODE) {
		pp->hairpin = flags.val & BR_HAIRPIN_MODE;
		do_flush = true;
	}
	if (flags.mask & BR_ISOLATED) {
		pp->isolated = flags.val & BR_ISOLATED;
		do_flush = true;
	}
	if (do_flush) {
		struct dsa_switch *ds = &priv->ds;
		struct dsa_port *dp = dsa_to_port(ds, port);
		struct net_device *bdev;

		bdev = dsa_port_bridge_dev_get(dp);
		if (bdev) {
			u32 ports_mask;

			ports_mask = dsa_bridge_ports(ds, bdev);
			ports_mask |= priv->cpu_ports_mask;
			res = yt921x_bridge(priv, ports_mask);
			if (res)
				return res;
		}
	}

	return 0;
}

static int
yt921x_dsa_port_pre_bridge_flags(struct dsa_switch *ds, int port,
				 struct switchdev_brport_flags flags,
				 struct netlink_ext_ack *extack)
{
	if (flags.mask & ~(BR_HAIRPIN_MODE | BR_LEARNING | BR_FLOOD |
			   BR_MCAST_FLOOD | BR_ISOLATED))
		return -EINVAL;
	return 0;
}

static int
yt921x_dsa_port_bridge_flags(struct dsa_switch *ds, int port,
			     struct switchdev_brport_flags flags,
			     struct netlink_ext_ack *extack)
{
	struct yt921x_priv *priv = dsa_to_yt921x_priv(ds);
	int res;

	if (dsa_is_cpu_port(ds, port))
		return 0;

	mutex_lock(&priv->reg_lock);
	res = yt921x_bridge_flags(priv, port, flags);
	mutex_unlock(&priv->reg_lock);

	return res;
}

static void
yt921x_dsa_port_bridge_leave(struct dsa_switch *ds, int port,
			     struct dsa_bridge bridge)
{
	struct yt921x_priv *priv = dsa_to_yt921x_priv(ds);
	struct device *dev = yt921x_priv_to_device(priv);
	int res;

	if (dsa_is_cpu_port(ds, port))
		return;

	mutex_lock(&priv->reg_lock);
	res = yt921x_bridge_leave(priv, port);
	mutex_unlock(&priv->reg_lock);

	if (res)
		dev_err(dev, "Failed to %s port %d: %i\n", "unbridge",
			port, res);
}

static int
yt921x_dsa_port_bridge_join(struct dsa_switch *ds, int port,
			    struct dsa_bridge bridge, bool *tx_fwd_offload,
			    struct netlink_ext_ack *extack)
{
	struct yt921x_priv *priv = dsa_to_yt921x_priv(ds);
	u16 ports_mask;
	int res;

	if (dsa_is_cpu_port(ds, port))
		return 0;

	ports_mask = dsa_bridge_ports(ds, bridge.dev);
	ports_mask |= priv->cpu_ports_mask;

	mutex_lock(&priv->reg_lock);
	res = yt921x_bridge_join(priv, port, ports_mask);
	mutex_unlock(&priv->reg_lock);

	return res;
}

static int
yt921x_dsa_port_mst_state_set(struct dsa_switch *ds, int port,
			      const struct switchdev_mst_state *st)
{
	struct yt921x_priv *priv = dsa_to_yt921x_priv(ds);
	u32 mask;
	u32 ctrl;
	int res;

	mask = YT921X_STP_PORTn_M(port);
	switch (st->state) {
	case BR_STATE_DISABLED:
		ctrl = YT921X_STP_PORTn_DISABLED(port);
		break;
	case BR_STATE_LISTENING:
	case BR_STATE_LEARNING:
		ctrl = YT921X_STP_PORTn_LEARNING(port);
		break;
	case BR_STATE_FORWARDING:
	default:
		ctrl = YT921X_STP_PORTn_FORWARD(port);
		break;
	case BR_STATE_BLOCKING:
		ctrl = YT921X_STP_PORTn_BLOCKING(port);
		break;
	}

	mutex_lock(&priv->reg_lock);
	res = yt921x_reg_update_bits(priv, YT921X_STPn(st->msti), mask, ctrl);
	mutex_unlock(&priv->reg_lock);

	return res;
}

static int
yt921x_dsa_vlan_msti_set(struct dsa_switch *ds, struct dsa_bridge bridge,
			 const struct switchdev_vlan_msti *msti)
{
	struct yt921x_priv *priv = dsa_to_yt921x_priv(ds);
	u32 masks[2];
	u32 ctrls[2];
	int res;

	if (!msti->vid)
		return -EINVAL;
	if (!msti->msti || msti->msti >= YT921X_MSTI_NUM)
		return -EINVAL;

	masks[0] = 0;
	ctrls[0] = 0;
	masks[1] = YT921X_VLAN_CTRLb_STP_ID_M;
	ctrls[1] = YT921X_VLAN_CTRLb_STP_ID(msti->msti);

	mutex_lock(&priv->reg_lock);
	res = yt921x_reg64_update_bits(priv, YT921X_VLANn_CTRL(msti->vid),
				       masks, ctrls);
	mutex_unlock(&priv->reg_lock);

	return res;
}

static void
yt921x_dsa_port_stp_state_set(struct dsa_switch *ds, int port, u8 state)
{
	struct yt921x_priv *priv = dsa_to_yt921x_priv(ds);
	struct device *dev = yt921x_priv_to_device(priv);
	struct dsa_port *dp = dsa_to_port(ds, port);
	bool learning;
	u32 mask;
	u32 ctrl;
	int res;

	mask = YT921X_STP_PORTn_M(port);
	learning = false;
	switch (state) {
	case BR_STATE_DISABLED:
		ctrl = YT921X_STP_PORTn_DISABLED(port);
		break;
	case BR_STATE_LISTENING:
		ctrl = YT921X_STP_PORTn_LEARNING(port);
		break;
	case BR_STATE_LEARNING:
		ctrl = YT921X_STP_PORTn_LEARNING(port);
		learning = dp->learning;
		break;
	case BR_STATE_FORWARDING:
	default:
		ctrl = YT921X_STP_PORTn_FORWARD(port);
		learning = dp->learning;
		break;
	case BR_STATE_BLOCKING:
		ctrl = YT921X_STP_PORTn_BLOCKING(port);
		break;
	}

	mutex_lock(&priv->reg_lock);
	do {
		res = yt921x_reg_update_bits(priv, YT921X_STPn(0), mask, ctrl);
		if (res)
			break;

		mask = YT921X_PORT_LEARN_DIS;
		ctrl = !learning ? YT921X_PORT_LEARN_DIS : 0;
		res = yt921x_reg_update_bits(priv, YT921X_PORTn_LEARN(port),
					     mask, ctrl);
	} while (0);
	mutex_unlock(&priv->reg_lock);

	if (res)
		dev_err(dev, "Failed to %s port %d: %i\n", "set STP state for",
			port, res);
}

static int __maybe_unused
yt921x_dsa_port_get_default_prio(struct dsa_switch *ds, int port)
{
	struct yt921x_priv *priv = dsa_to_yt921x_priv(ds);
	u32 val;
	int res;

	mutex_lock(&priv->reg_lock);
	res = yt921x_reg_read(priv, YT921X_PORTn_QOS(port), &val);
	mutex_unlock(&priv->reg_lock);

	if (res)
		return res;

	return FIELD_GET(YT921X_PORT_QOS_PRIO_M, val);
}

static int __maybe_unused
yt921x_dsa_port_set_default_prio(struct dsa_switch *ds, int port, u8 prio)
{
	struct yt921x_priv *priv = dsa_to_yt921x_priv(ds);
	u32 mask;
	u32 ctrl;
	int res;

	if (prio >= YT921X_PRIO_NUM)
		return -EINVAL;

	mutex_lock(&priv->reg_lock);
	mask = YT921X_PORT_QOS_PRIO_M | YT921X_PORT_QOS_PRIO_EN;
	ctrl = YT921X_PORT_QOS_PRIO(prio) | YT921X_PORT_QOS_PRIO_EN;
	res = yt921x_reg_update_bits(priv, YT921X_PORTn_QOS(port), mask, ctrl);
	mutex_unlock(&priv->reg_lock);

	return res;
}

static int __maybe_unused appprios_cmp(const void *a, const void *b)
{
	return ((const u8 *)b)[1] - ((const u8 *)a)[1];
}

static int __maybe_unused
yt921x_dsa_port_get_apptrust(struct dsa_switch *ds, int port, u8 *sel,
			     int *nselp)
{
	struct yt921x_priv *priv = dsa_to_yt921x_priv(ds);
	u8 appprios[2][2] = {};
	int nsel;
	u32 val;
	int res;

	mutex_lock(&priv->reg_lock);
	res = yt921x_reg_read(priv, YT921X_PORTn_PRIO_ORD(port), &val);
	mutex_unlock(&priv->reg_lock);

	if (res)
		return res;

	appprios[0][0] = IEEE_8021QAZ_APP_SEL_DSCP;
	appprios[0][1] = (val >> (3 * YT921X_APP_SEL_DSCP)) & 7;
	appprios[1][0] = DCB_APP_SEL_PCP;
	appprios[1][1] = (val >> (3 * YT921X_APP_SEL_CVLAN_PCP)) & 7;
	sort(appprios, ARRAY_SIZE(appprios), sizeof(appprios[0]), appprios_cmp,
	     NULL);

	nsel = 0;
	for (int i = 0; i < ARRAY_SIZE(appprios) && appprios[i][1]; i++) {
		sel[nsel] = appprios[i][0];
		nsel++;
	}
	*nselp = nsel;

	return 0;
}

static int __maybe_unused
yt921x_dsa_port_set_apptrust(struct dsa_switch *ds, int port, const u8 *sel,
			     int nsel)
{
	struct yt921x_priv *priv = dsa_to_yt921x_priv(ds);
	struct device *dev = yt921x_priv_to_device(priv);
	u32 ctrl;
	int res;

	if (nsel > YT921X_APP_SEL_NUM)
		return -EINVAL;

	ctrl = 0;
	for (int i = 0; i < nsel; i++) {
		switch (sel[i]) {
		case IEEE_8021QAZ_APP_SEL_DSCP:
			ctrl |= YT921X_PORT_PRIO_ORD_APPm(YT921X_APP_SEL_DSCP,
							  7 - i);
			break;
		case DCB_APP_SEL_PCP:
			ctrl |= YT921X_PORT_PRIO_ORD_APPm(YT921X_APP_SEL_CVLAN_PCP,
							  7 - i);
			ctrl |= YT921X_PORT_PRIO_ORD_APPm(YT921X_APP_SEL_SVLAN_PCP,
							  7 - i);
			break;
		default:
			dev_err(dev,
				"Invalid apptrust selector (at %d-th). Supported: dscp, pcp\n",
				i + 1);
			return -EOPNOTSUPP;
		}
	}

	mutex_lock(&priv->reg_lock);
	res = yt921x_reg_write(priv, YT921X_PORTn_PRIO_ORD(port), ctrl);
	mutex_unlock(&priv->reg_lock);

	return res;
}

static int yt921x_port_down(struct yt921x_priv *priv, int port)
{
	const struct yt921x_info *info = priv->info;
	u32 mask;
	int res;

	mask = YT921X_PORT_CTRL_LINK_AN | YT921X_PORT_RX_MAC_EN |
	       YT921X_PORT_TX_MAC_EN;
	res = yt921x_reg_clear_bits(priv, YT921X_PORTn_CTRL(port), mask);
	if (res)
		return res;

	if (BIT(port) & info->serdes_mask) {
		mask = YT921X_SERDES_LINK;
		res = yt921x_reg_clear_bits(priv, YT921X_SERDESn(port), mask);
		if (res)
			return res;
	}
	if (BIT(port) & info->xmii_mask) {
		mask = YT921X_XMII_LINK;
		res = yt921x_reg_clear_bits(priv, YT921X_XMIIn(port), mask);
		if (res)
			return res;
	}

	return 0;
}

static int
yt921x_port_up(struct yt921x_priv *priv, int port, unsigned int mode,
	       phy_interface_t interface, int speed, int duplex,
	       bool tx_pause, bool rx_pause)
{
	const struct yt921x_info *info = priv->info;
	struct yt921x_port *pp = &priv->ports[port];
	int ps = ethtool_speed_to_yt921x(speed);
	u32 mask;
	u32 ctrl;
	int res;

	if (ps == YT921X_SPEED_INVALID)
		return -EINVAL;

	mask = YT921X_PORT_SPEED_M | YT921X_PORT_TX_MAC_EN |
	       YT921X_PORT_RX_MAC_EN | YT921X_PORT_TX_PAUSE |
	       YT921X_PORT_RX_PAUSE | YT921X_PORT_DUPLEX_FULL |
	       YT921X_PORT_CTRL_LINK_AN;
	ctrl = YT921X_PORT_SPEED(ps);
	if (duplex == DUPLEX_FULL)
		ctrl |= YT921X_PORT_DUPLEX_FULL;
	if (tx_pause)
		ctrl |= YT921X_PORT_TX_PAUSE;
	if (rx_pause)
		ctrl |= YT921X_PORT_RX_PAUSE;
	ctrl |= YT921X_PORT_RX_MAC_EN | YT921X_PORT_TX_MAC_EN;
	if (pp->serdes && pp->inband)
		ctrl |= YT921X_PORT_CTRL_LINK_AN;
	res = yt921x_reg_update_bits(priv, YT921X_PORTn_CTRL(port), mask, ctrl);
	if (res)
		return res;

	if (!(BIT(port) & (info->serdes_mask | info->xmii_mask)))
		return 0;

	if (pp->serdes) {
		mask = YT921X_SERDES_SPEED_M;
		ctrl = YT921X_SERDES_SPEED(ps);
		mask |= YT921X_SERDES_DUPLEX_FULL;
		if (duplex == DUPLEX_FULL)
			ctrl |= YT921X_SERDES_DUPLEX_FULL;
		mask |= YT921X_SERDES_TX_PAUSE;
		if (tx_pause)
			ctrl |= YT921X_SERDES_TX_PAUSE;
		mask |= YT921X_SERDES_RX_PAUSE;
		if (rx_pause)
			ctrl |= YT921X_SERDES_RX_PAUSE;
		mask |= YT921X_SERDES_LINK;
		if (!pp->inband)
			ctrl |= YT921X_SERDES_LINK;
		res = yt921x_reg_update_bits(priv, YT921X_SERDESn(port),
					     mask, ctrl);
		if (res)
			return res;
	} else {
		mask = YT921X_XMII_LINK;
		res = yt921x_reg_set_bits(priv, YT921X_XMIIn(port), mask);
		if (res)
			return res;

		ctrl = YT921X_MDIO_POLLING_SPEED(ps);
		if (duplex == DUPLEX_FULL)
			ctrl |= YT921X_MDIO_POLLING_DUPLEX_FULL;
		ctrl |= YT921X_MDIO_POLLING_LINK;
		res = yt921x_reg_write(priv, YT921X_MDIO_POLLINGn(port), ctrl);
		if (res)
			return res;
	}

	return 0;
}

static int
yt921x_port_config(struct yt921x_priv *priv, int port, unsigned int mode,
		   phy_interface_t interface)
{
	struct device *dev = yt921x_priv_to_device(priv);
	const struct yt921x_info *info = priv->info;
	struct yt921x_port *pp = &priv->ports[port];
	u32 mask;
	int res;

	if (BIT(port) & info->internal_mask) {
		if (interface != PHY_INTERFACE_MODE_INTERNAL)
			goto err;
		return 0;
	}

	switch (interface) {
	/* SERDES */
	case PHY_INTERFACE_MODE_SGMII:
	case PHY_INTERFACE_MODE_100BASEX:
	case PHY_INTERFACE_MODE_1000BASEX:
	case PHY_INTERFACE_MODE_2500BASEX:
		if (!(BIT(port) & info->serdes_mask))
			goto err;

		mask = YT921X_SERDES_CTRL_PORTn(port);
		res = yt921x_reg_set_bits(priv, YT921X_SERDES_CTRL, mask);
		if (res)
			return res;

		mask = YT921X_XMII_CTRL_PORTn(port);
		res = yt921x_reg_clear_bits(priv, YT921X_XMII_CTRL, mask);
		if (res)
			return res;

		/* The order is quite arbitrary - we can't return to a safe
		 * state on IO errors.
		 */
		pp->serdes = true;

		break;
	/* add XMII support here */
	default:
		goto err;
	}

	return 0;

err:
	dev_err(dev, "Wrong mode %d on port %d\n", interface, port);
	return -EINVAL;
}

static struct phylink_pcs *
yt921x_phylink_mac_select_pcs(struct phylink_config *config,
			      phy_interface_t interface)
{
	struct dsa_port *dp = dsa_phylink_to_port(config);
	struct yt921x_priv *priv = dsa_to_yt921x_priv(dp->ds);
	const struct yt921x_info *info = priv->info;
	int port = dp->index;

	if (!(BIT(port) & info->serdes_mask))
		return NULL;

	switch (interface) {
	case PHY_INTERFACE_MODE_SGMII:
	case PHY_INTERFACE_MODE_100BASEX:
	case PHY_INTERFACE_MODE_1000BASEX:
	case PHY_INTERFACE_MODE_2500BASEX:
		return &priv->ports[port].pcs;
	default:
		return NULL;
	}
}

static void
yt921x_phylink_mac_link_down(struct phylink_config *config, unsigned int mode,
			     phy_interface_t interface)
{
	struct dsa_port *dp = dsa_phylink_to_port(config);
	struct yt921x_priv *priv = dsa_to_yt921x_priv(dp->ds);
	struct yt921x_mib *pm;
	int port = dp->index;
	int res;

	/* No need to sync; port control block is hold until device remove */
	pm = priv->ports[port].mib;
	if (pm)
		cancel_delayed_work(&pm->work);

	mutex_lock(&priv->reg_lock);
	res = yt921x_port_down(priv, port);
	mutex_unlock(&priv->reg_lock);

	if (res)
		dev_err(dp->ds->dev, "Failed to %s port %d: %i\n", "bring down",
			port, res);
}

static void
yt921x_phylink_mac_link_up(struct phylink_config *config,
			   struct phy_device *phydev, unsigned int mode,
			   phy_interface_t interface, int speed, int duplex,
			   bool tx_pause, bool rx_pause)
{
	struct dsa_port *dp = dsa_phylink_to_port(config);
	struct yt921x_priv *priv = dsa_to_yt921x_priv(dp->ds);
	struct yt921x_mib *pm;
	int port = dp->index;
	int res;

	mutex_lock(&priv->reg_lock);
	res = yt921x_port_up(priv, port, mode, interface, speed, duplex,
			     tx_pause, rx_pause);
	mutex_unlock(&priv->reg_lock);

	if (res)
		dev_err(dp->ds->dev, "Failed to %s port %d: %i\n", "bring up",
			port, res);

	pm = priv->ports[port].mib;
	if (pm)
		schedule_delayed_work(&pm->work, 0);
}

static void
yt921x_phylink_mac_config(struct phylink_config *config, unsigned int mode,
			  const struct phylink_link_state *state)
{
	struct dsa_port *dp = dsa_phylink_to_port(config);
	struct yt921x_priv *priv = dsa_to_yt921x_priv(dp->ds);
	int port = dp->index;
	int res;

	mutex_lock(&priv->reg_lock);
	res = yt921x_port_config(priv, port, mode, state->interface);
	mutex_unlock(&priv->reg_lock);

	if (res)
		dev_err(dp->ds->dev, "Failed to %s port %d: %i\n", "config",
			port, res);
}

static void
yt921x_dsa_phylink_get_caps(struct dsa_switch *ds, int port,
			    struct phylink_config *config)
{
	struct yt921x_priv *priv = dsa_to_yt921x_priv(ds);
	const struct yt921x_info *info = priv->info;

	config->mac_capabilities = MAC_ASYM_PAUSE | MAC_SYM_PAUSE |
				   MAC_10 | MAC_100 | MAC_1000;

	if (info->internal_mask & BIT(port)) {
		/* Port 10 for MCU should probably go here too. But since that
		 * is untested yet, turn it down for the moment by letting it
		 * fall to the default branch.
		 */
		__set_bit(PHY_INTERFACE_MODE_INTERNAL,
			  config->supported_interfaces);
		return;
	}

	if (BIT(port) & info->serdes_mask) {
		__set_bit(PHY_INTERFACE_MODE_SGMII,
			  config->supported_interfaces);
		/* REVSGMII (SGMII in PHY role) should go here, once
		 * PHY_INTERFACE_MODE_REVSGMII is introduced.
		 */
		__set_bit(PHY_INTERFACE_MODE_100BASEX,
			  config->supported_interfaces);
		__set_bit(PHY_INTERFACE_MODE_1000BASEX,
			  config->supported_interfaces);
		__set_bit(PHY_INTERFACE_MODE_2500BASEX,
			  config->supported_interfaces);
		config->mac_capabilities |= MAC_2500FD;
	}
	if (BIT(port) & info->xmii_mask) {
		/* Not tested. To add support for XMII:
		 *   - Add proper interface modes below
		 *   - Handle them in yt921x_port_config()
		 */
	}
	/* no such port: empty supported_interfaces causes phylink to turn it
	 * down
	 */
}

static int yt921x_port_setup(struct yt921x_priv *priv, int port)
{
	struct dsa_switch *ds = &priv->ds;
	u32 ctrl;
	int res;

	res = yt921x_userport_standalone(priv, port);
	if (res)
		return res;

	/* Clear prio order (even if DCB is not enabled) to avoid unsolicited
	 * priorities
	 */
	res = yt921x_reg_write(priv, YT921X_PORTn_PRIO_ORD(port), 0);
	if (res)
		return res;

	if (dsa_is_cpu_port(ds, port)) {
		/* Egress of CPU port is supposed to be completely controlled
		 * via tagging, so set to oneway isolated (drop all packets
		 * without tag).
		 */
		ctrl = ~(u32)0;
		res = yt921x_reg_write(priv, YT921X_PORTn_ISOLATION(port),
				       ctrl);
		if (res)
			return res;

		/* To simplify FDB "isolation" simulation, we also disable
		 * learning on the CPU port, and let software identify packets
		 * towarding CPU (either trapped or a static FDB entry is
		 * matched, no matter which bridge that entry is for), which is
		 * already done by yt921x_userport_standalone(). As a result,
		 * VLAN-awareness becomes unrelated on the CPU port (set to
		 * VLAN-unaware by the way).
		 */
	}

	return 0;
}

static enum dsa_tag_protocol
yt921x_dsa_get_tag_protocol(struct dsa_switch *ds, int port,
			    enum dsa_tag_protocol m)
{
	return DSA_TAG_PROTO_YT921X;
}

static void yt921x_dsa_port_teardown(struct dsa_switch *ds, int port)
{
	struct yt921x_priv *priv = dsa_to_yt921x_priv(ds);
	struct yt921x_port *pp = &priv->ports[port];
	struct yt921x_mib *pm = pp->mib;

	/* get_stats64() might not be synchronized with port_teardown(), hold
	 * the MIB block till the device teardown
	 */
	if (pm)
		disable_delayed_work_sync(&pm->work);
}

static int yt921x_dsa_port_setup(struct dsa_switch *ds, int port)
{
	struct yt921x_priv *priv = dsa_to_yt921x_priv(ds);
	struct yt921x_port *pp = &priv->ports[port];
	const struct yt921x_info *info = priv->info;
	struct device *dev = yt921x_priv_to_device(priv);
	struct yt921x_mib *pm = pp->mib;
	int res;

	if (!pm && (BIT(port) & yt921x_info_ports_mask(info))) {
		pm = devm_kzalloc(dev, sizeof(*pm), GFP_KERNEL);
		if (!pm)
			return -ENOMEM;
		pp->mib = pm;

		pm->port = pp;
		u64_stats_init(&pm->syncp);
		INIT_DELAYED_WORK(&pm->work, yt921x_mib_poll);
		/* Start disabled, so that the enable below always pairs with
		 * either this disable or the one from a previous teardown
		 */
		disable_delayed_work_sync(&pm->work);
	}

	mutex_lock(&priv->reg_lock);
	res = yt921x_port_setup(priv, port);
	mutex_unlock(&priv->reg_lock);
	if (res)
		return res;

	if (pm)
		enable_delayed_work(&pm->work);

	return 0;
}

/* Not "port" - DSCP mapping is global */
static int __maybe_unused
yt921x_dsa_port_get_dscp_prio(struct dsa_switch *ds, int port, u8 dscp)
{
	struct yt921x_priv *priv = dsa_to_yt921x_priv(ds);
	u32 val;
	int res;

	mutex_lock(&priv->reg_lock);
	res = yt921x_reg_read(priv, YT921X_IPM_DSCPn(dscp), &val);
	mutex_unlock(&priv->reg_lock);

	if (res)
		return res;

	return FIELD_GET(YT921X_IPM_PRIO_M, val);
}

static int __maybe_unused
yt921x_dsa_port_del_dscp_prio(struct dsa_switch *ds, int port, u8 dscp, u8 prio)
{
	struct yt921x_priv *priv = dsa_to_yt921x_priv(ds);
	u32 val;
	int res;

	mutex_lock(&priv->reg_lock);
	/* During a "dcb app replace" command, the new app table entry will be
	 * added first, then the old one will be deleted. But the hardware only
	 * supports one QoS class per DSCP value (duh), so if we blindly delete
	 * the app table entry for this DSCP value, we end up deleting the
	 * entry with the new priority. Avoid that by checking whether user
	 * space wants to delete the priority which is currently configured, or
	 * something else which is no longer current.
	 */
	res = yt921x_reg_read(priv, YT921X_IPM_DSCPn(dscp), &val);
	if (!res && FIELD_GET(YT921X_IPM_PRIO_M, val) == prio)
		res = yt921x_reg_write(priv, YT921X_IPM_DSCPn(dscp),
				       YT921X_IPM_PRIO(IEEE8021Q_TT_BK));
	mutex_unlock(&priv->reg_lock);

	return res;
}

static int __maybe_unused
yt921x_dsa_port_add_dscp_prio(struct dsa_switch *ds, int port, u8 dscp, u8 prio)
{
	struct yt921x_priv *priv = dsa_to_yt921x_priv(ds);
	int res;

	if (prio >= YT921X_PRIO_NUM)
		return -EINVAL;

	mutex_lock(&priv->reg_lock);
	res = yt921x_reg_write(priv, YT921X_IPM_DSCPn(dscp),
			       YT921X_IPM_PRIO(prio));
	mutex_unlock(&priv->reg_lock);

	return res;
}

static int yt921x_edata_wait(struct yt921x_priv *priv, u32 *valp)
{
	u32 val = YT921X_EDATA_DATA_IDLE;
	int res;

	res = yt921x_reg_wait(priv, YT921X_EDATA_DATA,
			      YT921X_EDATA_DATA_STATUS_M, &val);
	if (res)
		return res;

	*valp = val;
	return 0;
}

static int
yt921x_edata_read_cont(struct yt921x_priv *priv, u8 addr, u8 *valp)
{
	u32 ctrl;
	u32 val;
	int res;

	ctrl = YT921X_EDATA_CTRL_ADDR(addr) | YT921X_EDATA_CTRL_READ;
	res = yt921x_reg_write(priv, YT921X_EDATA_CTRL, ctrl);
	if (res)
		return res;
	res = yt921x_edata_wait(priv, &val);
	if (res)
		return res;

	*valp = FIELD_GET(YT921X_EDATA_DATA_DATA_M, val);
	return 0;
}

static int yt921x_edata_read(struct yt921x_priv *priv, u8 addr, u8 *valp)
{
	u32 val;
	int res;

	res = yt921x_edata_wait(priv, &val);
	if (res)
		return res;
	return yt921x_edata_read_cont(priv, addr, valp);
}

static void yt921x_dsa_teardown(struct dsa_switch *ds)
{
#if IS_ENABLED(CONFIG_NET_DSA_YT921X_LEDS)
	struct yt921x_priv *priv = dsa_to_yt921x_priv(ds);

	yt921x_leds_remove(priv);
#endif
}

static int yt921x_chip_detect(struct yt921x_priv *priv)
{
	struct device *dev = yt921x_priv_to_device(priv);
	const struct yt921x_info *info;
	u8 extmode;
	u32 chipid;
	u32 major;
	u32 mode;
	u32 val;
	int res;

	res = yt921x_reg_read(priv, YT921X_CHIP_ID, &chipid);
	if (res)
		return res;

	major = FIELD_GET(YT921X_CHIP_ID_MAJOR, chipid);

	for (info = yt921x_infos; info->name; info++)
		if (info->major == major)
			break;
	if (!info->name) {
		dev_err(dev, "Unexpected chipid 0x%x\n", chipid);
		return -ENODEV;
	}

	res = yt921x_reg_read(priv, YT921X_CHIP_MODE, &mode);
	if (res)
		return res;
	res = yt921x_edata_read(priv, YT921X_EDATA_EXTMODE, &extmode);
	if (res)
		return res;

	for (; info->name; info++)
		if (info->major == major && info->mode == mode &&
		    info->extmode == extmode)
			break;
	if (!info->name) {
		dev_err(dev,
			"Unsupported chipid 0x%x with chipmode 0x%x 0x%x\n",
			chipid, mode, extmode);
		return -ENODEV;
	}

	res = yt921x_reg_read(priv, YT921X_SYS_CLK, &val);
	if (res)
		return res;
	switch (FIELD_GET(YT921X_SYS_CLK_SEL_M, val)) {
	case 0:
		priv->cycle_ns = info->major == YT9215_MAJOR ? 8 : 6;
		break;
	case YT921X_SYS_CLK_143M:
		priv->cycle_ns = 7;
		break;
	default:
		priv->cycle_ns = 8;
	}

	/* Print chipid here since we are interested in lower 16 bits */
	dev_info(dev,
		 "Motorcomm %s ethernet switch, chipid: 0x%x, chipmode: 0x%x 0x%x\n",
		 info->name, chipid, mode, extmode);

	priv->info = info;

	return 0;
}

static int yt921x_chip_reset(struct yt921x_priv *priv)
{
	struct device *dev = yt921x_priv_to_device(priv);
	u16 eth_p_tag;
	u32 val;
	int res;

	res = yt921x_chip_detect(priv);
	if (res)
		return res;

	/* Reset */
	res = yt921x_reg_write(priv, YT921X_RST, YT921X_RST_HW);
	if (res)
		return res;

	/* RST_HW is almost same as GPIO hard reset, so we need this delay. */
	fsleep(YT921X_RST_DELAY_US);

	val = 0;
	res = yt921x_reg_wait(priv, YT921X_RST, ~0, &val);
	if (res)
		return res;

	/* Check for tag EtherType; do it after reset in case you messed it up
	 * before.
	 */
	res = yt921x_reg_read(priv, YT921X_CPU_TAG_TPID, &val);
	if (res)
		return res;
	eth_p_tag = FIELD_GET(YT921X_CPU_TAG_TPID_TPID_M, val);
	if (eth_p_tag != ETH_P_YT921X) {
		dev_err(dev, "Tag type 0x%x != 0x%x\n", eth_p_tag,
			ETH_P_YT921X);
		/* Despite being possible, we choose not to set CPU_TAG_TPID,
		 * since there is no way it can be different unless you have the
		 * wrong chip.
		 */
		return -EINVAL;
	}

	return 0;
}

static int yt921x_chip_setup_dsa(struct yt921x_priv *priv)
{
	struct dsa_switch *ds = &priv->ds;
	unsigned long cpu_ports_mask;
	u32 ctrls[2];
	u32 ctrl;
	int port;
	int res;

	/* Enable DSA */
	priv->cpu_ports_mask = dsa_cpu_ports(ds);

	ctrl = YT921X_EXT_CPU_PORT_TAG_EN | YT921X_EXT_CPU_PORT_PORT_EN |
	       YT921X_EXT_CPU_PORT_PORT(__ffs(priv->cpu_ports_mask));
	res = yt921x_reg_write(priv, YT921X_EXT_CPU_PORT, ctrl);
	if (res)
		return res;

	/* Setup software switch */
	ctrl = YT921X_CPU_COPY_TO_EXT_CPU;
	res = yt921x_reg_write(priv, YT921X_CPU_COPY, ctrl);
	if (res)
		return res;

	ctrl = GENMASK(10, 0);
	res = yt921x_reg_write(priv, YT921X_FILTER_UNK_UCAST, ctrl);
	if (res)
		return res;
	res = yt921x_reg_write(priv, YT921X_FILTER_UNK_MCAST, ctrl);
	if (res)
		return res;

	/* YT921x does not support native DSA port bridging, so we use port
	 * isolation to emulate it. However, be especially careful that port
	 * isolation takes _after_ FDB lookups, i.e. if an FDB entry (from
	 * another bridge) is matched and the destination port (in another
	 * bridge) is blocked, the packet will be dropped instead of flooding to
	 * the "bridged" ports, thus we need to trap and handle those packets by
	 * software.
	 *
	 * If there is no more than one bridge, we might be able to drop them
	 * directly given some conditions are met, but we trap them in all cases
	 * for now.
	 */
	ctrl = 0;
	for (int i = 0; i < YT921X_PORT_NUM; i++)
		ctrl |= YT921X_ACT_UNK_ACTn_TRAP(i);
	/* Except for CPU ports, if any packets are sent via CPU ports without
	 * tag, they should be dropped.
	 */
	cpu_ports_mask = priv->cpu_ports_mask;
	for_each_set_bit(port, &cpu_ports_mask, YT921X_PORT_NUM) {
		ctrl &= ~YT921X_ACT_UNK_ACTn_M(port);
		ctrl |= YT921X_ACT_UNK_ACTn_DROP(port);
	}
	res = yt921x_reg_write(priv, YT921X_ACT_UNK_UCAST, ctrl);
	if (res)
		return res;
	res = yt921x_reg_write(priv, YT921X_ACT_UNK_MCAST, ctrl);
	if (res)
		return res;

	/* Tagged VID 0 should be treated as untagged, which confuses the
	 * hardware a lot
	 */
	ctrls[0] = YT921X_VLAN_CTRLa_LEARN_DIS | YT921X_VLAN_CTRLa_PORTS_M;
	ctrls[1] = 0;
	res = yt921x_reg64_write(priv, YT921X_VLANn_CTRL(0), ctrls);
	if (res)
		return res;

	return 0;
}

static int __maybe_unused yt921x_chip_setup_qos(struct yt921x_priv *priv)
{
	u32 ctrl;
	int res;

	/* DSCP to internal priorities */
	for (u8 dscp = 0; dscp < DSCP_MAX; dscp++) {
		int prio = ietf_dscp_to_ieee8021q_tt(dscp);

		if (prio < 0)
			return prio;

		res = yt921x_reg_write(priv, YT921X_IPM_DSCPn(dscp),
				       YT921X_IPM_PRIO(prio));
		if (res)
			return res;
	}

	/* 802.1Q QoS to internal priorities */
	for (u8 pcp = 0; pcp < 8; pcp++)
		for (u8 dei = 0; dei < 2; dei++) {
			ctrl = YT921X_IPM_PRIO(pcp);
			if (dei)
				/* "Red" almost means drop, so it's not that
				 * useful. Note that tc police does not support
				 * Three-Color very well
				 */
				ctrl |= YT921X_IPM_COLOR_YELLOW;

			for (u8 svlan = 0; svlan < 2; svlan++) {
				u32 reg = YT921X_IPM_PCPn(svlan, dei, pcp);

				res = yt921x_reg_write(priv, reg, ctrl);
				if (res)
					return res;
			}
		}

	return 0;
}

static int yt921x_chip_setup(struct yt921x_priv *priv)
{
	u32 ctrl;
	int res;

	ctrl = YT921X_FUNC_MIB | YT921X_FUNC_ACL | YT921X_FUNC_METER;
	res = yt921x_reg_set_bits(priv, YT921X_FUNC, ctrl);
	if (res)
		return res;

	res = yt921x_chip_setup_dsa(priv);
	if (res)
		return res;

	res = yt921x_chip_setup_tc(priv);
	if (res)
		return res;

	res = yt921x_chip_setup_acl(priv);
	if (res)
		return res;

#if IS_ENABLED(CONFIG_DCB)
	res = yt921x_chip_setup_qos(priv);
	if (res)
		return res;
#endif

	/* Clear MIB */
	ctrl = YT921X_MIB_CTRL_CLEAN | YT921X_MIB_CTRL_ALL_PORT;
	res = yt921x_reg_write(priv, YT921X_MIB_CTRL, ctrl);
	if (res)
		return res;

	/* Miscellaneous */
	res = yt921x_reg_set_bits(priv, YT921X_SENSOR, YT921X_SENSOR_TEMP);
	if (res)
		return res;

	res = yt921x_reg_clear_bits(priv, YT921X_SERDES_CTRL,
				    YT921X_SERDES_CTRL_TEST);
	if (res)
		return res;

	return 0;
}

static int yt921x_dsa_setup(struct dsa_switch *ds)
{
	struct yt921x_priv *priv = dsa_to_yt921x_priv(ds);
	struct device *dev = yt921x_priv_to_device(priv);
	struct device_node *np = dev->of_node;
	struct device_node *child;
	unsigned long mask;
	int port;
	int res;

	mutex_lock(&priv->reg_lock);
	res = yt921x_chip_reset(priv);
	mutex_unlock(&priv->reg_lock);

	if (res)
		return res;

	/* Register the internal mdio bus. Nodes for internal ports should have
	 * proper phy-handle pointing to their PHYs. Not enabling the internal
	 * bus is possible, though pretty wired, if internal ports are not used.
	 */
	child = of_get_child_by_name(np, "mdio");
	if (child) {
		res = yt921x_mbus_int_init(priv, child);
		of_node_put(child);
		if (res)
			return res;
	}

	/* External mdio bus is optional */
	child = of_get_child_by_name(np, "mdio-external");
	if (child) {
		res = yt921x_mbus_ext_init(priv, child);
		of_node_put(child);
		if (res)
			return res;

		dev_err(dev, "Untested external mdio bus\n");
		return -ENODEV;
	}

	mask = priv->info->serdes_mask;
	for_each_set_bit(port, &mask, YT921X_PORT_NUM) {
		struct yt921x_port *pp = &priv->ports[port];

		pp->pcs.ops = &yt921x_phylink_pcs_ops;
		pp->pcs.poll = true;

		__set_bit(PHY_INTERFACE_MODE_SGMII,
			  pp->pcs.supported_interfaces);
		__set_bit(PHY_INTERFACE_MODE_100BASEX,
			  pp->pcs.supported_interfaces);
		__set_bit(PHY_INTERFACE_MODE_1000BASEX,
			  pp->pcs.supported_interfaces);
		__set_bit(PHY_INTERFACE_MODE_2500BASEX,
			  pp->pcs.supported_interfaces);
	}

	mutex_lock(&priv->reg_lock);
	res = yt921x_chip_setup(priv);
	mutex_unlock(&priv->reg_lock);

	if (res)
		return res;

#if IS_ENABLED(CONFIG_NET_DSA_YT921X_LEDS)
	res = yt921x_leds_setup(priv);
	if (res)
		dev_warn(dev, "Failed to setup LEDs: %d\n", res);
#endif

	return 0;
}

static const struct phylink_mac_ops yt921x_phylink_mac_ops = {
	.mac_select_pcs	= yt921x_phylink_mac_select_pcs,
	.mac_link_down	= yt921x_phylink_mac_link_down,
	.mac_link_up	= yt921x_phylink_mac_link_up,
	.mac_config	= yt921x_phylink_mac_config,
};

static const struct dsa_switch_ops yt921x_dsa_switch_ops = {
	/* mib */
	.get_strings		= yt921x_dsa_get_strings,
	.get_ethtool_stats	= yt921x_dsa_get_ethtool_stats,
	.get_sset_count		= yt921x_dsa_get_sset_count,
	.get_eth_mac_stats	= yt921x_dsa_get_eth_mac_stats,
	.get_eth_ctrl_stats	= yt921x_dsa_get_eth_ctrl_stats,
	.get_rmon_stats		= yt921x_dsa_get_rmon_stats,
	.get_stats64		= yt921x_dsa_get_stats64,
	.get_pause_stats	= yt921x_dsa_get_pause_stats,
	/* eee */
	.support_eee		= dsa_supports_eee,
	.set_mac_eee		= yt921x_dsa_set_mac_eee,
	/* mtu */
	.port_change_mtu	= yt921x_dsa_port_change_mtu,
	.port_max_mtu		= yt921x_dsa_port_max_mtu,
	/* rate */
	.port_policer_del	= yt921x_dsa_port_policer_del,
	.port_policer_add	= yt921x_dsa_port_policer_add,
	.port_setup_tc		= yt921x_dsa_port_setup_tc,
	/* acl */
	.cls_flower_del		= yt921x_dsa_cls_flower_del,
	.cls_flower_add		= yt921x_dsa_cls_flower_add,
	/* hsr */
	.port_hsr_leave		= dsa_port_simple_hsr_leave,
	.port_hsr_join		= dsa_port_simple_hsr_join,
	/* mirror */
	.port_mirror_del	= yt921x_dsa_port_mirror_del,
	.port_mirror_add	= yt921x_dsa_port_mirror_add,
	/* lag */
	.port_lag_leave		= yt921x_dsa_port_lag_leave,
	.port_lag_join		= yt921x_dsa_port_lag_join,
	/* fdb */
	.port_fdb_dump		= yt921x_dsa_port_fdb_dump,
	.port_fast_age		= yt921x_dsa_port_fast_age,
	.set_ageing_time	= yt921x_dsa_set_ageing_time,
	.port_fdb_del		= yt921x_dsa_port_fdb_del,
	.port_fdb_add		= yt921x_dsa_port_fdb_add,
	.port_mdb_del		= yt921x_dsa_port_mdb_del,
	.port_mdb_add		= yt921x_dsa_port_mdb_add,
	/* vlan */
	.port_vlan_filtering	= yt921x_dsa_port_vlan_filtering,
	.port_vlan_del		= yt921x_dsa_port_vlan_del,
	.port_vlan_add		= yt921x_dsa_port_vlan_add,
	/* bridge */
	.port_pre_bridge_flags	= yt921x_dsa_port_pre_bridge_flags,
	.port_bridge_flags	= yt921x_dsa_port_bridge_flags,
	.port_bridge_leave	= yt921x_dsa_port_bridge_leave,
	.port_bridge_join	= yt921x_dsa_port_bridge_join,
	/* mst */
	.port_mst_state_set	= yt921x_dsa_port_mst_state_set,
	.vlan_msti_set		= yt921x_dsa_vlan_msti_set,
	.port_stp_state_set	= yt921x_dsa_port_stp_state_set,
#if IS_ENABLED(CONFIG_DCB)
	/* dcb */
	.port_get_default_prio	= yt921x_dsa_port_get_default_prio,
	.port_set_default_prio	= yt921x_dsa_port_set_default_prio,
	.port_get_apptrust	= yt921x_dsa_port_get_apptrust,
	.port_set_apptrust	= yt921x_dsa_port_set_apptrust,
#endif
	/* port */
	.get_tag_protocol	= yt921x_dsa_get_tag_protocol,
	.phylink_get_caps	= yt921x_dsa_phylink_get_caps,
	.port_teardown		= yt921x_dsa_port_teardown,
	.port_setup		= yt921x_dsa_port_setup,
#if IS_ENABLED(CONFIG_DCB)
	/* dscp */
	.port_get_dscp_prio	= yt921x_dsa_port_get_dscp_prio,
	.port_del_dscp_prio	= yt921x_dsa_port_del_dscp_prio,
	.port_add_dscp_prio	= yt921x_dsa_port_add_dscp_prio,
#endif
	/* chip */
	.teardown		= yt921x_dsa_teardown,
	.setup			= yt921x_dsa_setup,
};

static void yt921x_mdio_shutdown(struct mdio_device *mdiodev)
{
	struct yt921x_priv *priv = mdiodev_get_drvdata(mdiodev);

	if (!priv)
		return;

	dsa_switch_shutdown(&priv->ds);
}

static void yt921x_mdio_remove(struct mdio_device *mdiodev)
{
	struct yt921x_priv *priv = mdiodev_get_drvdata(mdiodev);

	if (!priv)
		return;

	dsa_unregister_switch(&priv->ds);

	for (unsigned int i = 0; i < ARRAY_SIZE(priv->acl_blks); i++) {
		struct yt921x_acl_blk *aclblk = priv->acl_blks[i];

		if (!aclblk)
			continue;
		for (unsigned int j = 0; j < ARRAY_SIZE(aclblk->rules); j++) {
			struct yt921x_acl_rule *aclrule = aclblk->rules[j];

			if (!aclrule)
				continue;
			kvfree(aclrule);
		}
		kvfree(aclblk);
	}
	mutex_destroy(&priv->reg_lock);
}

static int yt921x_mdio_probe(struct mdio_device *mdiodev)
{
	struct device *dev = &mdiodev->dev;
	struct yt921x_reg_mdio *mdio;
	struct yt921x_priv *priv;
	struct dsa_switch *ds;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	mdio = devm_kzalloc(dev, sizeof(*mdio), GFP_KERNEL);
	if (!mdio)
		return -ENOMEM;

	mdio->bus = mdiodev->bus;
	mdio->addr = mdiodev->addr;
	mdio->switchid = 0;

	mutex_init(&priv->reg_lock);

	priv->reg_ops = &yt921x_reg_ops_mdio;
	priv->reg_ctx = mdio;

	for (size_t i = 0; i < ARRAY_SIZE(priv->ports); i++) {
		struct yt921x_port *pp = &priv->ports[i];

		pp->index = i;
	}

	ds = &priv->ds;
	ds->dev = dev;
	ds->assisted_learning_on_cpu_port = true;
	ds->dscp_prio_mapping_is_global = true;
	ds->priv = priv;
	ds->ops = &yt921x_dsa_switch_ops;
	ds->ageing_time_min = 1 * 5000;
	ds->ageing_time_max = U16_MAX * 5000;
	ds->phylink_mac_ops = &yt921x_phylink_mac_ops;
	ds->num_lag_ids = YT921X_LAG_NUM;
	ds->num_ports = YT921X_PORT_NUM;

	mdiodev_set_drvdata(mdiodev, priv);

	return dsa_register_switch(ds);
}

static const struct of_device_id yt921x_of_match[] = {
	{ .compatible = "motorcomm,yt9215" },
	{}
};
MODULE_DEVICE_TABLE(of, yt921x_of_match);

static struct mdio_driver yt921x_mdio_driver = {
	.probe = yt921x_mdio_probe,
	.remove = yt921x_mdio_remove,
	.shutdown = yt921x_mdio_shutdown,
	.mdiodrv.driver = {
		.name = YT921X_NAME,
		.of_match_table = yt921x_of_match,
	},
};

mdio_module_driver(yt921x_mdio_driver);

MODULE_AUTHOR("David Yang <mmyangfl@gmail.com>");
MODULE_DESCRIPTION("Driver for Motorcomm YT921x Switch");
MODULE_LICENSE("GPL");
