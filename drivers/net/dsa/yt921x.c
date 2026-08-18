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
#include <linux/iopoll.h>
#include <linux/mdio.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_mdio.h>
#include <linux/of_net.h>
#include <linux/sort.h>

#include <net/dsa.h>
#include <net/dscp.h>
#include <net/ieee8021q.h>
#include <net/pkt_cls.h>

#include "yt921x.h"

struct yt921x_mib_desc {
	unsigned int size;
	unsigned int offset;
	const char *name;
};

#define MIB_DESC(_size, _offset, _name) \
	{_size, _offset, _name}

/* Must agree with yt921x_mib
 *
 * Unstructured fields (name != NULL) will appear in get_ethtool_stats(),
 * structured go to their *_stats() methods, but we need their sizes and offsets
 * to perform 32bit MIB overflow wraparound.
 */
static const struct yt921x_mib_desc yt921x_mib_descs[] = {
	MIB_DESC(1, YT921X_MIB_DATA_RX_BROADCAST, NULL),
	MIB_DESC(1, YT921X_MIB_DATA_RX_PAUSE, NULL),
	MIB_DESC(1, YT921X_MIB_DATA_RX_MULTICAST, NULL),
	MIB_DESC(1, YT921X_MIB_DATA_RX_CRC_ERR, NULL),

	MIB_DESC(1, YT921X_MIB_DATA_RX_ALIGN_ERR, NULL),
	MIB_DESC(1, YT921X_MIB_DATA_RX_UNDERSIZE_ERR, NULL),
	MIB_DESC(1, YT921X_MIB_DATA_RX_FRAG_ERR, NULL),
	MIB_DESC(1, YT921X_MIB_DATA_RX_PKT_SZ_64, NULL),

	MIB_DESC(1, YT921X_MIB_DATA_RX_PKT_SZ_65_TO_127, NULL),
	MIB_DESC(1, YT921X_MIB_DATA_RX_PKT_SZ_128_TO_255, NULL),
	MIB_DESC(1, YT921X_MIB_DATA_RX_PKT_SZ_256_TO_511, NULL),
	MIB_DESC(1, YT921X_MIB_DATA_RX_PKT_SZ_512_TO_1023, NULL),

	MIB_DESC(1, YT921X_MIB_DATA_RX_PKT_SZ_1024_TO_1518, NULL),
	MIB_DESC(1, YT921X_MIB_DATA_RX_PKT_SZ_1519_TO_MAX, NULL),
	MIB_DESC(2, YT921X_MIB_DATA_RX_GOOD_BYTES, NULL),

	MIB_DESC(2, YT921X_MIB_DATA_RX_BAD_BYTES, "RxBadBytes"),
	MIB_DESC(1, YT921X_MIB_DATA_RX_OVERSIZE_ERR, NULL),

	MIB_DESC(1, YT921X_MIB_DATA_RX_DROPPED, NULL),
	MIB_DESC(1, YT921X_MIB_DATA_TX_BROADCAST, NULL),
	MIB_DESC(1, YT921X_MIB_DATA_TX_PAUSE, NULL),
	MIB_DESC(1, YT921X_MIB_DATA_TX_MULTICAST, NULL),

	MIB_DESC(1, YT921X_MIB_DATA_TX_UNDERSIZE_ERR, NULL),
	MIB_DESC(1, YT921X_MIB_DATA_TX_PKT_SZ_64, NULL),
	MIB_DESC(1, YT921X_MIB_DATA_TX_PKT_SZ_65_TO_127, NULL),
	MIB_DESC(1, YT921X_MIB_DATA_TX_PKT_SZ_128_TO_255, NULL),

	MIB_DESC(1, YT921X_MIB_DATA_TX_PKT_SZ_256_TO_511, NULL),
	MIB_DESC(1, YT921X_MIB_DATA_TX_PKT_SZ_512_TO_1023, NULL),
	MIB_DESC(1, YT921X_MIB_DATA_TX_PKT_SZ_1024_TO_1518, NULL),
	MIB_DESC(1, YT921X_MIB_DATA_TX_PKT_SZ_1519_TO_MAX, NULL),

	MIB_DESC(2, YT921X_MIB_DATA_TX_GOOD_BYTES, NULL),
	MIB_DESC(1, YT921X_MIB_DATA_TX_COLLISION, NULL),

	MIB_DESC(1, YT921X_MIB_DATA_TX_EXCESSIVE_COLLISION, NULL),
	MIB_DESC(1, YT921X_MIB_DATA_TX_MULTIPLE_COLLISION, NULL),
	MIB_DESC(1, YT921X_MIB_DATA_TX_SINGLE_COLLISION, NULL),
	MIB_DESC(1, YT921X_MIB_DATA_TX_PKT, NULL),

	MIB_DESC(1, YT921X_MIB_DATA_TX_DEFERRED, NULL),
	MIB_DESC(1, YT921X_MIB_DATA_TX_LATE_COLLISION, NULL),
	MIB_DESC(1, YT921X_MIB_DATA_RX_OAM, "RxOAM"),
	MIB_DESC(1, YT921X_MIB_DATA_TX_OAM, "TxOAM"),
};

struct yt921x_info {
	const char *name;
	u16 major;
	/* Unknown, seems to be plain enumeration */
	u8 mode;
	u8 extmode;
	/* Ports with integral GbE PHYs, not including MCU Port 10 */
	u16 internal_mask;
	/* TODO: see comments in yt921x_dsa_phylink_get_caps() */
	u16 external_mask;
};

#define YT921X_PORT_MASK_INTn(port)	BIT(port)
#define YT921X_PORT_MASK_INT0_n(n)	GENMASK((n) - 1, 0)
#define YT921X_PORT_MASK_EXT0		BIT(8)
#define YT921X_PORT_MASK_EXT1		BIT(9)

static const struct yt921x_info yt921x_infos[] = {
	{
		"YT9215SC", YT9215_MAJOR, 1, 0,
		YT921X_PORT_MASK_INT0_n(5),
		YT921X_PORT_MASK_EXT0 | YT921X_PORT_MASK_EXT1,
	},
	{
		"YT9215S", YT9215_MAJOR, 2, 0,
		YT921X_PORT_MASK_INT0_n(5),
		YT921X_PORT_MASK_EXT0 | YT921X_PORT_MASK_EXT1,
	},
	{
		"YT9215RB", YT9215_MAJOR, 3, 0,
		YT921X_PORT_MASK_INT0_n(5),
		YT921X_PORT_MASK_EXT0 | YT921X_PORT_MASK_EXT1,
	},
	{
		"YT9214NB", YT9215_MAJOR, 3, 2,
		YT921X_PORT_MASK_INTn(1) | YT921X_PORT_MASK_INTn(3),
		YT921X_PORT_MASK_EXT0 | YT921X_PORT_MASK_EXT1,
	},
	{
		"YT9213NB", YT9215_MAJOR, 3, 3,
		YT921X_PORT_MASK_INTn(1) | YT921X_PORT_MASK_INTn(3),
		YT921X_PORT_MASK_EXT1,
	},
	{
		"YT9218N", YT9218_MAJOR, 0, 0,
		YT921X_PORT_MASK_INT0_n(8),
		0,
	},
	{
		"YT9218MB", YT9218_MAJOR, 1, 0,
		YT921X_PORT_MASK_INT0_n(8),
		YT921X_PORT_MASK_EXT0 | YT921X_PORT_MASK_EXT1,
	},
	{}
};

#define YT921X_NAME	"yt921x"

#define YT921X_VID_UNWARE	4095

#define YT921X_POLL_SLEEP_US	10000
#define YT921X_POLL_TIMEOUT_US	100000

/* The interval should be small enough to avoid overflow of 32bit MIBs.
 *
 * Until we can read MIBs from stats64 call directly (i.e. sleep
 * there), we have to poll stats more frequently then it is actually needed.
 * For overflow protection, normally, 100 sec interval should have been OK.
 */
#define YT921X_STATS_INTERVAL_JIFFIES	(3 * HZ)

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

#define to_yt921x_priv(_ds) container_of_const(_ds, struct yt921x_priv, ds)
#define to_device(priv) ((priv)->ds.dev)

static u32 ethaddr_hi4_to_u32(const unsigned char *addr)
{
	return (addr[0] << 24) | (addr[1] << 16) | (addr[2] << 8) | addr[3];
}

static u32 ethaddr_lo2_to_u32(const unsigned char *addr)
{
	return (addr[4] << 8) | addr[5];
}

static int yt921x_reg_read(struct yt921x_priv *priv, u32 reg, u32 *valp)
{
	WARN_ON(!mutex_is_locked(&priv->reg_lock));

	return priv->reg_ops->read(priv->reg_ctx, reg, valp);
}

static int yt921x_reg_write(struct yt921x_priv *priv, u32 reg, u32 val)
{
	WARN_ON(!mutex_is_locked(&priv->reg_lock));

	return priv->reg_ops->write(priv->reg_ctx, reg, val);
}

static int
yt921x_reg_wait(struct yt921x_priv *priv, u32 reg, u32 mask, u32 *valp)
{
	u32 val;
	int res;
	int ret;

	ret = read_poll_timeout(yt921x_reg_read, res,
				res || (val & mask) == *valp,
				YT921X_POLL_SLEEP_US, YT921X_POLL_TIMEOUT_US,
				false, priv, reg, &val);
	if (ret)
		return ret;
	if (res)
		return res;

	*valp = val;
	return 0;
}

static int
yt921x_reg_update_bits(struct yt921x_priv *priv, u32 reg, u32 mask, u32 val)
{
	int res;
	u32 v;
	u32 u;

	res = yt921x_reg_read(priv, reg, &v);
	if (res)
		return res;

	u = v;
	u &= ~mask;
	u |= val;
	if (u == v)
		return 0;

	return yt921x_reg_write(priv, reg, u);
}

static int yt921x_reg_set_bits(struct yt921x_priv *priv, u32 reg, u32 mask)
{
	return yt921x_reg_update_bits(priv, reg, 0, mask);
}

static int yt921x_reg_clear_bits(struct yt921x_priv *priv, u32 reg, u32 mask)
{
	return yt921x_reg_update_bits(priv, reg, mask, 0);
}

static int
yt921x_reg_toggle_bits(struct yt921x_priv *priv, u32 reg, u32 mask, bool set)
{
	return yt921x_reg_update_bits(priv, reg, mask, !set ? 0 : mask);
}

/* Some multi-word registers, like VLANn_CTRL, should be treated as a single
 * long register. More specifically, writes to parts of its words won't become
 * visible, until the last word is written.
 *
 * Here we require full read and write operations over these registers to
 * eliminate potential issues, although partial reads/writes are also possible.
 */

static void update_ctrls_unaligned(u32 *lo, u32 *hi, u64 mask, u64 val)
{
	*lo &= ~lower_32_bits(mask);
	*hi &= ~upper_32_bits(mask);
	*lo |= lower_32_bits(val);
	*hi |= upper_32_bits(val);
}

static int
yt921x_regs_read(struct yt921x_priv *priv, u32 reg, u32 *vals,
		 unsigned int num_regs)
{
	int res;

	for (unsigned int i = 0; i < num_regs; i++) {
		res = yt921x_reg_read(priv, reg + 4 * i, &vals[i]);
		if (res)
			return res;
	}

	return 0;
}

static int
yt921x_regs_write(struct yt921x_priv *priv, u32 reg, const u32 *vals,
		  unsigned int num_regs)
{
	int res;

	for (unsigned int i = 0; i < num_regs; i++) {
		res = yt921x_reg_write(priv, reg + 4 * i, vals[i]);
		if (res)
			return res;
	}

	return 0;
}

static int
yt921x_regs_update_bits(struct yt921x_priv *priv, u32 reg, const u32 *masks,
			const u32 *vals, unsigned int num_regs)
{
	bool changed = false;
	u32 vs[4];
	int res;

	BUILD_BUG_ON(num_regs > ARRAY_SIZE(vs));

	res = yt921x_regs_read(priv, reg, vs, num_regs);
	if (res)
		return res;

	for (unsigned int i = 0; i < num_regs; i++) {
		u32 u = vs[i];

		u &= ~masks[i];
		u |= vals[i];
		if (u != vs[i])
			changed = true;

		vs[i] = u;
	}

	if (!changed)
		return 0;

	return yt921x_regs_write(priv, reg, vs, num_regs);
}

static int
yt921x_regs_clear_bits(struct yt921x_priv *priv, u32 reg, const u32 *masks,
		       unsigned int num_regs)
{
	bool changed = false;
	u32 vs[4];
	int res;

	BUILD_BUG_ON(num_regs > ARRAY_SIZE(vs));

	res = yt921x_regs_read(priv, reg, vs, num_regs);
	if (res)
		return res;

	for (unsigned int i = 0; i < num_regs; i++) {
		u32 u = vs[i];

		u &= ~masks[i];
		if (u != vs[i])
			changed = true;

		vs[i] = u;
	}

	if (!changed)
		return 0;

	return yt921x_regs_write(priv, reg, vs, num_regs);
}

static int
yt921x_reg64_write(struct yt921x_priv *priv, u32 reg, const u32 *vals)
{
	return yt921x_regs_write(priv, reg, vals, 2);
}

static int
yt921x_reg64_update_bits(struct yt921x_priv *priv, u32 reg, const u32 *masks,
			 const u32 *vals)
{
	return yt921x_regs_update_bits(priv, reg, masks, vals, 2);
}

static int
yt921x_reg64_clear_bits(struct yt921x_priv *priv, u32 reg, const u32 *masks)
{
	return yt921x_regs_clear_bits(priv, reg, masks, 2);
}

static int
yt921x_reg96_write(struct yt921x_priv *priv, u32 reg, const u32 *vals)
{
	return yt921x_regs_write(priv, reg, vals, 3);
}

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

static int yt921x_intif_wait(struct yt921x_priv *priv)
{
	u32 val = 0;

	return yt921x_reg_wait(priv, YT921X_INT_MBUS_OP, YT921X_MBUS_OP_START,
			       &val);
}

static int
yt921x_intif_read(struct yt921x_priv *priv, int port, int reg, u16 *valp)
{
	struct device *dev = to_device(priv);
	u32 mask;
	u32 ctrl;
	u32 val;
	int res;

	res = yt921x_intif_wait(priv);
	if (res)
		return res;

	mask = YT921X_MBUS_CTRL_PORT_M | YT921X_MBUS_CTRL_REG_M |
	       YT921X_MBUS_CTRL_OP_M;
	ctrl = YT921X_MBUS_CTRL_PORT(port) | YT921X_MBUS_CTRL_REG(reg) |
	       YT921X_MBUS_CTRL_READ;
	res = yt921x_reg_update_bits(priv, YT921X_INT_MBUS_CTRL, mask, ctrl);
	if (res)
		return res;
	res = yt921x_reg_write(priv, YT921X_INT_MBUS_OP, YT921X_MBUS_OP_START);
	if (res)
		return res;

	res = yt921x_intif_wait(priv);
	if (res)
		return res;
	res = yt921x_reg_read(priv, YT921X_INT_MBUS_DIN, &val);
	if (res)
		return res;

	if ((u16)val != val)
		dev_info(dev,
			 "%s: port %d, reg 0x%x: Expected u16, got 0x%08x\n",
			 __func__, port, reg, val);
	*valp = (u16)val;
	return 0;
}

static int
yt921x_intif_write(struct yt921x_priv *priv, int port, int reg, u16 val)
{
	u32 mask;
	u32 ctrl;
	int res;

	res = yt921x_intif_wait(priv);
	if (res)
		return res;

	mask = YT921X_MBUS_CTRL_PORT_M | YT921X_MBUS_CTRL_REG_M |
	       YT921X_MBUS_CTRL_OP_M;
	ctrl = YT921X_MBUS_CTRL_PORT(port) | YT921X_MBUS_CTRL_REG(reg) |
	       YT921X_MBUS_CTRL_WRITE;
	res = yt921x_reg_update_bits(priv, YT921X_INT_MBUS_CTRL, mask, ctrl);
	if (res)
		return res;
	res = yt921x_reg_write(priv, YT921X_INT_MBUS_DOUT, val);
	if (res)
		return res;
	res = yt921x_reg_write(priv, YT921X_INT_MBUS_OP, YT921X_MBUS_OP_START);
	if (res)
		return res;

	return yt921x_intif_wait(priv);
}

static int yt921x_mbus_int_read(struct mii_bus *mbus, int port, int reg)
{
	struct yt921x_priv *priv = mbus->priv;
	u16 val;
	int res;

	if (port >= YT921X_PORT_NUM)
		return U16_MAX;

	mutex_lock(&priv->reg_lock);
	res = yt921x_intif_read(priv, port, reg, &val);
	mutex_unlock(&priv->reg_lock);

	if (res)
		return res;
	return val;
}

static int
yt921x_mbus_int_write(struct mii_bus *mbus, int port, int reg, u16 data)
{
	struct yt921x_priv *priv = mbus->priv;
	int res;

	if (port >= YT921X_PORT_NUM)
		return -ENODEV;

	mutex_lock(&priv->reg_lock);
	res = yt921x_intif_write(priv, port, reg, data);
	mutex_unlock(&priv->reg_lock);

	return res;
}

static int
yt921x_mbus_int_init(struct yt921x_priv *priv, struct device_node *mnp)
{
	struct device *dev = to_device(priv);
	struct mii_bus *mbus;
	int res;

	mbus = devm_mdiobus_alloc(dev);
	if (!mbus)
		return -ENOMEM;

	mbus->name = "YT921x internal MDIO bus";
	snprintf(mbus->id, MII_BUS_ID_SIZE, "%s", dev_name(dev));
	mbus->priv = priv;
	mbus->read = yt921x_mbus_int_read;
	mbus->write = yt921x_mbus_int_write;
	mbus->parent = dev;
	mbus->phy_mask = (u32)~GENMASK(YT921X_PORT_NUM - 1, 0);

	res = devm_of_mdiobus_register(dev, mbus, mnp);
	if (res)
		return res;

	priv->mbus_int = mbus;

	return 0;
}

static int yt921x_extif_wait(struct yt921x_priv *priv)
{
	u32 val = 0;

	return yt921x_reg_wait(priv, YT921X_EXT_MBUS_OP, YT921X_MBUS_OP_START,
			       &val);
}

static int
yt921x_extif_read(struct yt921x_priv *priv, int port, int reg, u16 *valp)
{
	struct device *dev = to_device(priv);
	u32 mask;
	u32 ctrl;
	u32 val;
	int res;

	res = yt921x_extif_wait(priv);
	if (res)
		return res;

	mask = YT921X_MBUS_CTRL_PORT_M | YT921X_MBUS_CTRL_REG_M |
	       YT921X_MBUS_CTRL_TYPE_M | YT921X_MBUS_CTRL_OP_M;
	ctrl = YT921X_MBUS_CTRL_PORT(port) | YT921X_MBUS_CTRL_REG(reg) |
	       YT921X_MBUS_CTRL_TYPE_C22 | YT921X_MBUS_CTRL_READ;
	res = yt921x_reg_update_bits(priv, YT921X_EXT_MBUS_CTRL, mask, ctrl);
	if (res)
		return res;
	res = yt921x_reg_write(priv, YT921X_EXT_MBUS_OP, YT921X_MBUS_OP_START);
	if (res)
		return res;

	res = yt921x_extif_wait(priv);
	if (res)
		return res;
	res = yt921x_reg_read(priv, YT921X_EXT_MBUS_DIN, &val);
	if (res)
		return res;

	if ((u16)val != val)
		dev_info(dev,
			 "%s: port %d, reg 0x%x: Expected u16, got 0x%08x\n",
			 __func__, port, reg, val);
	*valp = (u16)val;
	return 0;
}

static int
yt921x_extif_write(struct yt921x_priv *priv, int port, int reg, u16 val)
{
	u32 mask;
	u32 ctrl;
	int res;

	res = yt921x_extif_wait(priv);
	if (res)
		return res;

	mask = YT921X_MBUS_CTRL_PORT_M | YT921X_MBUS_CTRL_REG_M |
	       YT921X_MBUS_CTRL_TYPE_M | YT921X_MBUS_CTRL_OP_M;
	ctrl = YT921X_MBUS_CTRL_PORT(port) | YT921X_MBUS_CTRL_REG(reg) |
	       YT921X_MBUS_CTRL_TYPE_C22 | YT921X_MBUS_CTRL_WRITE;
	res = yt921x_reg_update_bits(priv, YT921X_EXT_MBUS_CTRL, mask, ctrl);
	if (res)
		return res;
	res = yt921x_reg_write(priv, YT921X_EXT_MBUS_DOUT, val);
	if (res)
		return res;
	res = yt921x_reg_write(priv, YT921X_EXT_MBUS_OP, YT921X_MBUS_OP_START);
	if (res)
		return res;

	return yt921x_extif_wait(priv);
}

static int yt921x_mbus_ext_read(struct mii_bus *mbus, int port, int reg)
{
	struct yt921x_priv *priv = mbus->priv;
	u16 val;
	int res;

	mutex_lock(&priv->reg_lock);
	res = yt921x_extif_read(priv, port, reg, &val);
	mutex_unlock(&priv->reg_lock);

	if (res)
		return res;
	return val;
}

static int
yt921x_mbus_ext_write(struct mii_bus *mbus, int port, int reg, u16 data)
{
	struct yt921x_priv *priv = mbus->priv;
	int res;

	mutex_lock(&priv->reg_lock);
	res = yt921x_extif_write(priv, port, reg, data);
	mutex_unlock(&priv->reg_lock);

	return res;
}

static int
yt921x_mbus_ext_init(struct yt921x_priv *priv, struct device_node *mnp)
{
	struct device *dev = to_device(priv);
	struct mii_bus *mbus;
	int res;

	mbus = devm_mdiobus_alloc(dev);
	if (!mbus)
		return -ENOMEM;

	mbus->name = "YT921x external MDIO bus";
	snprintf(mbus->id, MII_BUS_ID_SIZE, "%s@ext", dev_name(dev));
	mbus->priv = priv;
	/* TODO: c45? */
	mbus->read = yt921x_mbus_ext_read;
	mbus->write = yt921x_mbus_ext_write;
	mbus->parent = dev;

	res = devm_of_mdiobus_register(dev, mbus, mnp);
	if (res)
		return res;

	priv->mbus_ext = mbus;

	return 0;
}

/* Read and handle overflow of 32bit MIBs. MIB buffer must be zeroed before. */
static int yt921x_read_mib(struct yt921x_priv *priv, int port)
{
	struct yt921x_port *pp = &priv->ports[port];
	struct device *dev = to_device(priv);
	struct yt921x_mib *mib = &pp->mib;
	int res = 0;

	/* Reading of yt921x_port::mib is not protected by a lock and it's vain
	 * to keep its consistency, since we have to read registers one by one
	 * and there is no way to make a snapshot of MIB stats.
	 *
	 * Writing (by this function only) is and should be protected by
	 * reg_lock.
	 */

	for (size_t i = 0; i < ARRAY_SIZE(yt921x_mib_descs); i++) {
		const struct yt921x_mib_desc *desc = &yt921x_mib_descs[i];
		u32 reg = YT921X_MIBn_DATA0(port) + desc->offset;
		u64 *valp = &((u64 *)mib)[i];
		u32 val0;
		u64 val;

		res = yt921x_reg_read(priv, reg, &val0);
		if (res)
			break;

		if (desc->size <= 1) {
			u64 old_val = *valp;

			val = (old_val & ~(u64)U32_MAX) | val0;
			if (val < old_val)
				val += 1ull << 32;
		} else {
			u32 val1;

			res = yt921x_reg_read(priv, reg + 4, &val1);
			if (res)
				break;
			val = ((u64)val1 << 32) | val0;
		}

		WRITE_ONCE(*valp, val);
	}

	pp->rx_frames = mib->rx_64byte + mib->rx_65_127byte +
			mib->rx_128_255byte + mib->rx_256_511byte +
			mib->rx_512_1023byte + mib->rx_1024_1518byte +
			mib->rx_jumbo;
	pp->tx_frames = mib->tx_64byte + mib->tx_65_127byte +
			mib->tx_128_255byte + mib->tx_256_511byte +
			mib->tx_512_1023byte + mib->tx_1024_1518byte +
			mib->tx_jumbo;

	if (res)
		dev_err(dev, "Failed to %s port %d: %i\n", "read stats for",
			port, res);
	return res;
}

static void yt921x_poll_mib(struct work_struct *work)
{
	struct yt921x_port *pp = container_of_const(work, struct yt921x_port,
						    mib_read.work);
	struct yt921x_priv *priv = (void *)(pp - pp->index) -
				   offsetof(struct yt921x_priv, ports);
	unsigned long delay = YT921X_STATS_INTERVAL_JIFFIES;
	int port = pp->index;
	int res;

	mutex_lock(&priv->reg_lock);
	res = yt921x_read_mib(priv, port);
	mutex_unlock(&priv->reg_lock);
	if (res)
		delay *= 4;

	schedule_delayed_work(&pp->mib_read, delay);
}

static void
yt921x_dsa_get_strings(struct dsa_switch *ds, int port, u32 stringset,
		       uint8_t *data)
{
	if (stringset != ETH_SS_STATS)
		return;

	for (size_t i = 0; i < ARRAY_SIZE(yt921x_mib_descs); i++) {
		const struct yt921x_mib_desc *desc = &yt921x_mib_descs[i];

		if (desc->name)
			ethtool_puts(&data, desc->name);
	}
}

static void
yt921x_dsa_get_ethtool_stats(struct dsa_switch *ds, int port, uint64_t *data)
{
	struct yt921x_priv *priv = to_yt921x_priv(ds);
	struct yt921x_port *pp = &priv->ports[port];
	struct yt921x_mib *mib = &pp->mib;
	size_t j;

	mutex_lock(&priv->reg_lock);
	yt921x_read_mib(priv, port);
	mutex_unlock(&priv->reg_lock);

	j = 0;
	for (size_t i = 0; i < ARRAY_SIZE(yt921x_mib_descs); i++) {
		const struct yt921x_mib_desc *desc = &yt921x_mib_descs[i];

		if (!desc->name)
			continue;

		data[j] = ((u64 *)mib)[i];
		j++;
	}
}

static int yt921x_dsa_get_sset_count(struct dsa_switch *ds, int port, int sset)
{
	int cnt = 0;

	if (sset != ETH_SS_STATS)
		return 0;

	for (size_t i = 0; i < ARRAY_SIZE(yt921x_mib_descs); i++) {
		const struct yt921x_mib_desc *desc = &yt921x_mib_descs[i];

		if (desc->name)
			cnt++;
	}

	return cnt;
}

static void
yt921x_dsa_get_eth_mac_stats(struct dsa_switch *ds, int port,
			     struct ethtool_eth_mac_stats *mac_stats)
{
	struct yt921x_priv *priv = to_yt921x_priv(ds);
	struct yt921x_port *pp = &priv->ports[port];
	struct yt921x_mib *mib = &pp->mib;

	mutex_lock(&priv->reg_lock);
	yt921x_read_mib(priv, port);
	mutex_unlock(&priv->reg_lock);

	mac_stats->FramesTransmittedOK = pp->tx_frames;
	mac_stats->SingleCollisionFrames = mib->tx_single_collisions;
	mac_stats->MultipleCollisionFrames = mib->tx_multiple_collisions;
	mac_stats->FramesReceivedOK = pp->rx_frames;
	mac_stats->FrameCheckSequenceErrors = mib->rx_crc_errors;
	mac_stats->AlignmentErrors = mib->rx_alignment_errors;
	mac_stats->OctetsTransmittedOK = mib->tx_good_bytes;
	mac_stats->FramesWithDeferredXmissions = mib->tx_deferred;
	mac_stats->LateCollisions = mib->tx_late_collisions;
	mac_stats->FramesAbortedDueToXSColls = mib->tx_aborted_errors;
	/* mac_stats->FramesLostDueToIntMACXmitError */
	/* mac_stats->CarrierSenseErrors */
	mac_stats->OctetsReceivedOK = mib->rx_good_bytes;
	/* mac_stats->FramesLostDueToIntMACRcvError */
	mac_stats->MulticastFramesXmittedOK = mib->tx_multicast;
	mac_stats->BroadcastFramesXmittedOK = mib->tx_broadcast;
	/* mac_stats->FramesWithExcessiveDeferral */
	mac_stats->MulticastFramesReceivedOK = mib->rx_multicast;
	mac_stats->BroadcastFramesReceivedOK = mib->rx_broadcast;
	/* mac_stats->InRangeLengthErrors */
	/* mac_stats->OutOfRangeLengthField */
	mac_stats->FrameTooLongErrors = mib->rx_oversize_errors;
}

static void
yt921x_dsa_get_eth_ctrl_stats(struct dsa_switch *ds, int port,
			      struct ethtool_eth_ctrl_stats *ctrl_stats)
{
	struct yt921x_priv *priv = to_yt921x_priv(ds);
	struct yt921x_port *pp = &priv->ports[port];
	struct yt921x_mib *mib = &pp->mib;

	mutex_lock(&priv->reg_lock);
	yt921x_read_mib(priv, port);
	mutex_unlock(&priv->reg_lock);

	ctrl_stats->MACControlFramesTransmitted = mib->tx_pause;
	ctrl_stats->MACControlFramesReceived = mib->rx_pause;
	/* ctrl_stats->UnsupportedOpcodesReceived */
}

static const struct ethtool_rmon_hist_range yt921x_rmon_ranges[] = {
	{ 0, 64 },
	{ 65, 127 },
	{ 128, 255 },
	{ 256, 511 },
	{ 512, 1023 },
	{ 1024, 1518 },
	{ 1519, YT921X_FRAME_SIZE_MAX },
	{}
};

static void
yt921x_dsa_get_rmon_stats(struct dsa_switch *ds, int port,
			  struct ethtool_rmon_stats *rmon_stats,
			  const struct ethtool_rmon_hist_range **ranges)
{
	struct yt921x_priv *priv = to_yt921x_priv(ds);
	struct yt921x_port *pp = &priv->ports[port];
	struct yt921x_mib *mib = &pp->mib;

	mutex_lock(&priv->reg_lock);
	yt921x_read_mib(priv, port);
	mutex_unlock(&priv->reg_lock);

	*ranges = yt921x_rmon_ranges;

	rmon_stats->undersize_pkts = mib->rx_undersize_errors;
	rmon_stats->oversize_pkts = mib->rx_oversize_errors;
	rmon_stats->fragments = mib->rx_alignment_errors;
	/* rmon_stats->jabbers */

	rmon_stats->hist[0] = mib->rx_64byte;
	rmon_stats->hist[1] = mib->rx_65_127byte;
	rmon_stats->hist[2] = mib->rx_128_255byte;
	rmon_stats->hist[3] = mib->rx_256_511byte;
	rmon_stats->hist[4] = mib->rx_512_1023byte;
	rmon_stats->hist[5] = mib->rx_1024_1518byte;
	rmon_stats->hist[6] = mib->rx_jumbo;

	rmon_stats->hist_tx[0] = mib->tx_64byte;
	rmon_stats->hist_tx[1] = mib->tx_65_127byte;
	rmon_stats->hist_tx[2] = mib->tx_128_255byte;
	rmon_stats->hist_tx[3] = mib->tx_256_511byte;
	rmon_stats->hist_tx[4] = mib->tx_512_1023byte;
	rmon_stats->hist_tx[5] = mib->tx_1024_1518byte;
	rmon_stats->hist_tx[6] = mib->tx_jumbo;
}

static void
yt921x_dsa_get_stats64(struct dsa_switch *ds, int port,
		       struct rtnl_link_stats64 *stats)
{
	struct yt921x_priv *priv = to_yt921x_priv(ds);
	struct yt921x_port *pp = &priv->ports[port];
	struct yt921x_mib *mib = &pp->mib;

	stats->rx_length_errors = mib->rx_undersize_errors +
				  mib->rx_fragment_errors;
	stats->rx_over_errors = mib->rx_oversize_errors;
	stats->rx_crc_errors = mib->rx_crc_errors;
	stats->rx_frame_errors = mib->rx_alignment_errors;
	/* stats->rx_fifo_errors */
	/* stats->rx_missed_errors */

	stats->tx_aborted_errors = mib->tx_aborted_errors;
	/* stats->tx_carrier_errors */
	stats->tx_fifo_errors = mib->tx_undersize_errors;
	/* stats->tx_heartbeat_errors */
	stats->tx_window_errors = mib->tx_late_collisions;

	stats->rx_packets = pp->rx_frames;
	stats->tx_packets = pp->tx_frames;
	stats->rx_bytes = mib->rx_good_bytes - ETH_FCS_LEN * stats->rx_packets;
	stats->tx_bytes = mib->tx_good_bytes - ETH_FCS_LEN * stats->tx_packets;
	stats->rx_errors = stats->rx_length_errors + stats->rx_over_errors +
			   stats->rx_crc_errors + stats->rx_frame_errors;
	stats->tx_errors = stats->tx_aborted_errors + stats->tx_fifo_errors +
			   stats->tx_window_errors;
	stats->rx_dropped = mib->rx_dropped;
	/* stats->tx_dropped */
	stats->multicast = mib->rx_multicast;
	stats->collisions = mib->tx_collisions;
}

static void
yt921x_dsa_get_pause_stats(struct dsa_switch *ds, int port,
			   struct ethtool_pause_stats *pause_stats)
{
	struct yt921x_priv *priv = to_yt921x_priv(ds);
	struct yt921x_port *pp = &priv->ports[port];
	struct yt921x_mib *mib = &pp->mib;

	mutex_lock(&priv->reg_lock);
	yt921x_read_mib(priv, port);
	mutex_unlock(&priv->reg_lock);

	pause_stats->tx_pause_frames = mib->tx_pause;
	pause_stats->rx_pause_frames = mib->rx_pause;
}

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
	struct yt921x_priv *priv = to_yt921x_priv(ds);
	int res;

	mutex_lock(&priv->reg_lock);
	res = yt921x_set_eee(priv, port, e);
	mutex_unlock(&priv->reg_lock);

	return res;
}

static int yt921x_mtu_fetch(struct yt921x_priv *priv, int port)
{
	struct dsa_port *dp = dsa_to_port(&priv->ds, port);

	return dp->user ? READ_ONCE(dp->user->mtu) : ETH_DATA_LEN;
}

static int
yt921x_dsa_port_change_mtu(struct dsa_switch *ds, int port, int new_mtu)
{
	/* Only serves as packet filter, since the frame size is always set to
	 * maximum after reset
	 */

	struct yt921x_priv *priv = to_yt921x_priv(ds);
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

/* v * 2^e */
static u64 ldexpu64(u64 v, int e)
{
	return e >= 0 ? v << e : v >> -e;
}

/* slot (ns) * rate (/s) / 10^9 (ns/s) = 2^C * token * 4^unit */
static u32 rate2token(u64 rate, unsigned int slot_ns, int unit, int C)
{
	int e = 2 * unit + C + YT921X_TOKEN_RATE_C;

	return div_u64(ldexpu64(slot_ns * rate, -e), 1000000000);
}

static u64 token2rate(u32 token, unsigned int slot_ns, int unit, int C)
{
	int e = 2 * unit + C + YT921X_TOKEN_RATE_C;

	return div_u64(ldexpu64(mul_u32_u32(1000000000, token), e), slot_ns);
}

/* burst = 2^C * token * 4^unit */
static u32 burst2token(u64 burst, int unit, int C)
{
	return ldexpu64(burst, -(2 * unit + C));
}

static u64 token2burst(u32 token, int unit, int C)
{
	return ldexpu64(token, 2 * unit + C);
}

struct yt921x_marker {
	u32 cir;
	u32 cbs;
	u32 ebs;
	int unit;
	bool pkt_mode;
};

#define YT921X_MARKER_PKT_MODE		BIT(0)
#define YT921X_MARKER_SINGLE_BUCKET	BIT(1)

static int
yt921x_marker_tfm(struct yt921x_marker *marker, u64 rate, u64 burst,
		  unsigned int flags, unsigned int slot_ns, u32 cir_max,
		  u32 cbs_max, int unit_max, struct yt921x_priv *priv, int port,
		  struct netlink_ext_ack *extack)
{
	const int C = flags & YT921X_MARKER_PKT_MODE ? YT921X_TOKEN_PKT_C :
		      YT921X_TOKEN_BYTE_C;
	struct device *dev = to_device(priv);
	struct yt921x_marker m;
	u64 burst_est;
	u64 burst_sug;
	u64 burst_max;
	u64 rate_max;

	m.unit = unit_max;
	rate_max = token2rate(cir_max, slot_ns, m.unit, C);
	burst_max = token2burst(cbs_max, m.unit, C);

	/* Check for unusual values */
	if (rate > rate_max || burst > burst_max) {
		NL_SET_ERR_MSG_MOD(extack, "Unexpected tremendous rate");
		return -ERANGE;
	}

	/* Check for matching burst */
	burst_est = div_u64(slot_ns * rate, 1000000000);
	burst_sug = burst_est;
	if (flags & YT921X_MARKER_PKT_MODE)
		burst_sug++;
	else
		burst_sug += ETH_HLEN + yt921x_mtu_fetch(priv, port) +
			     ETH_FCS_LEN;
	if (burst_sug > burst)
		NL_SET_ERR_MSG_FMT_MOD(extack,
				       "Consider match rate %llu with burst at least %llu",
				       rate, burst_sug);

	/* Select unit */
	for (; m.unit > 0; m.unit--) {
		if (rate > (rate_max >> 2) || burst > (burst_max >> 2))
			break;
		rate_max >>= 2;
		burst_max >>= 2;
	}

	/* Calculate information rate and bucket size */
	m.cir = rate2token(rate, slot_ns, m.unit, C);
	if (!m.cir)
		m.cir = 1;
	else if (WARN_ON(m.cir > cir_max))
		m.cir = cir_max;
	m.cbs = burst2token(burst, m.unit, C);
	if (!m.cbs)
		m.cbs = 1;
	else if (WARN_ON(m.cbs > cbs_max))
		m.cbs = cbs_max;

	/* Cut EBS */
	m.ebs = 0;
	if (!(flags & YT921X_MARKER_SINGLE_BUCKET)) {
		/* We don't have a chance to adjust rate when MTU is changed */
		if (flags & YT921X_MARKER_PKT_MODE)
			burst_est++;
		else
			burst_est += YT921X_FRAME_SIZE_MAX;

		if (burst_est < burst) {
			u32 pbs = m.cbs;

			m.cbs = burst2token(burst_est, m.unit, C);
			if (!m.cbs)
				m.cbs = 1;
			else if (WARN_ON(m.cbs > cbs_max))
				m.cbs = cbs_max;

			if (pbs > m.cbs)
				m.ebs = pbs - m.cbs;
		}
	}

	dev_dbg(dev,
		"slot %u ns, rate %llu, burst %llu -> unit %d, cir %u, cbs %u, ebs %u\n",
		slot_ns, rate, burst, m.unit, m.cir, m.cbs, m.ebs);

	m.pkt_mode = flags & YT921X_MARKER_PKT_MODE;
	*marker = m;
	return 0;
}

static int
yt921x_marker_tfm_police(struct yt921x_marker *marker,
			 const struct flow_action_police *police,
			 unsigned int flags, struct yt921x_priv *priv, int port,
			 struct netlink_ext_ack *extack)
{
	bool pkt_mode = !!police->rate_pkt_ps;
	u64 burst;
	u64 rate;

	rate = pkt_mode ? police->rate_pkt_ps : police->rate_bytes_ps;
	burst = pkt_mode ? police->burst_pkt : police->burst;
	if (pkt_mode)
		flags |= YT921X_MARKER_PKT_MODE;

	return yt921x_marker_tfm(marker, rate, burst, flags,
				 priv->meter_slot_ns, YT921X_METER_CIR_MAX,
				 YT921X_METER_CBS_MAX, YT921X_METER_UNIT_MAX,
				 priv, port, extack);
}

static int
yt921x_marker_tfm_shape(struct yt921x_marker *marker, u64 rate, u64 burst,
			unsigned int flags, struct yt921x_priv *priv, int port,
			struct netlink_ext_ack *extack)
{
	return yt921x_marker_tfm(marker, rate, burst, flags,
				 priv->port_shape_slot_ns, YT921X_SHAPE_CIR_MAX,
				 YT921X_SHAPE_CBS_MAX, YT921X_SHAPE_UNIT_MAX,
				 priv, port, extack);
}

static int
yt921x_police_validate(const struct flow_action_police *police,
		       const struct flow_action *action,
		       const struct flow_action_entry *act,
		       struct netlink_ext_ack *extack)
{
	if (police->exceed.act_id != FLOW_ACTION_DROP) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Offload not supported when exceed action is not drop");
		return -EOPNOTSUPP;
	}

	if (police->notexceed.act_id != FLOW_ACTION_PIPE &&
	    police->notexceed.act_id != FLOW_ACTION_ACCEPT) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Offload not supported when conform action is not pipe or ok");
		return -EOPNOTSUPP;
	}

	if (police->notexceed.act_id == FLOW_ACTION_ACCEPT && action && act &&
	    !flow_action_is_last_entry(action, act)) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Offload not supported when conform action is ok, but action is not last");
		return -EOPNOTSUPP;
	}

	/* mtu defaults to unlimited but we got 2040 here, don't know why */
	if (police->peakrate_bytes_ps || police->avrate || police->overhead) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Offload not supported when peakrate/avrate/overhead is configured");
		return -EOPNOTSUPP;
	}

	return 0;
}

static int
yt921x_meter_config(struct yt921x_priv *priv, unsigned int id,
		    const struct yt921x_marker *marker)
{
	u32 ctrls[3];

	ctrls[0] = 0;
	ctrls[1] = YT921X_METER_CTRLb_CIR(marker->cir);
	ctrls[2] = YT921X_METER_CTRLc_UNIT(marker->unit) |
		   YT921X_METER_CTRLc_DROP_R |
		   YT921X_METER_CTRLc_TOKEN_OVERFLOW_EN |
		   YT921X_METER_CTRLc_METER_EN;
	if (marker->pkt_mode)
		ctrls[2] |= YT921X_METER_CTRLc_PKT_MODE;
	update_ctrls_unaligned(&ctrls[0], &ctrls[1],
			       YT921X_METER_CTRLab_EBS_M,
			       YT921X_METER_CTRLab_EBS(marker->ebs));
	update_ctrls_unaligned(&ctrls[1], &ctrls[2],
			       YT921X_METER_CTRLbc_CBS_M,
			       YT921X_METER_CTRLbc_CBS(marker->cbs));

	return yt921x_reg96_write(priv, YT921X_METERn_CTRL(id), ctrls);
}

static void yt921x_dsa_port_policer_del(struct dsa_switch *ds, int port)
{
	struct yt921x_priv *priv = to_yt921x_priv(ds);
	struct device *dev = to_device(priv);
	int res;

	mutex_lock(&priv->reg_lock);
	res = yt921x_reg_write(priv, YT921X_PORTn_METER(port), 0);
	mutex_unlock(&priv->reg_lock);

	if (res)
		dev_err(dev, "Failed to %s port %d: %i\n", "delete policer on",
			port, res);
}

static int
yt921x_dsa_port_policer_add(struct dsa_switch *ds, int port,
			    const struct flow_action_police *police,
			    struct netlink_ext_ack *extack)
{
	struct yt921x_priv *priv = to_yt921x_priv(ds);
	struct yt921x_marker marker;
	u32 ctrl;
	int res;

	res = yt921x_police_validate(police, NULL, NULL, extack);
	if (res)
		return res;

	res = yt921x_marker_tfm_police(&marker, police, 0, priv, port, extack);
	if (res)
		return res;

	mutex_lock(&priv->reg_lock);
	res = yt921x_meter_config(priv, port + YT921X_METER_NUM, &marker);
	if (res)
		goto end;

	ctrl = YT921X_PORT_METER_ID(port) | YT921X_PORT_METER_EN;
	res = yt921x_reg_write(priv, YT921X_PORTn_METER(port), ctrl);
end:
	mutex_unlock(&priv->reg_lock);

	return res;
}

static int
yt921x_dsa_port_setup_tc_tbf_port(struct dsa_switch *ds, int port,
				  const struct tc_tbf_qopt_offload *qopt)
{
	struct yt921x_priv *priv = to_yt921x_priv(ds);
	struct netlink_ext_ack *extack = qopt->extack;
	u32 ctrls[2];
	int res;

	if (qopt->parent != TC_H_ROOT)
		return -EOPNOTSUPP;

	switch (qopt->command) {
	case TC_TBF_STATS:
		/* Unfortunately the convention for TC_*_STATS is a mess,
		 * neither 0 nor -EOPNOTSUPP is perfect here.
		 */
		return -EOPNOTSUPP;
	case TC_TBF_DESTROY:
		ctrls[0] = 0;
		ctrls[1] = 0;
		break;
	case TC_TBF_REPLACE: {
		const struct tc_tbf_qopt_offload_replace_params *p;
		struct yt921x_marker marker;

		p = &qopt->replace_params;

		res = yt921x_marker_tfm_shape(&marker, p->rate.rate_bytes_ps,
					      p->max_size,
					      YT921X_MARKER_SINGLE_BUCKET,
					      priv, port, extack);
		if (res)
			return res;

		ctrls[0] = YT921X_PORT_SHAPE_CTRLa_CIR(marker.cir) |
			   YT921X_PORT_SHAPE_CTRLa_CBS(marker.cbs);
		ctrls[1] = YT921X_PORT_SHAPE_CTRLb_UNIT(marker.unit) |
			   YT921X_PORT_SHAPE_CTRLb_EN;
		break;
	}
	default:
		return -EOPNOTSUPP;
	}

	mutex_lock(&priv->reg_lock);
	res = yt921x_reg64_write(priv, YT921X_PORTn_SHAPE_CTRL(port), ctrls);
	mutex_unlock(&priv->reg_lock);

	return res;
}

static int
yt921x_dsa_port_setup_tc(struct dsa_switch *ds, int port,
			 enum tc_setup_type type, void *type_data)
{
	switch (type) {
	case TC_SETUP_QDISC_TBF: {
		const struct tc_tbf_qopt_offload *qopt = type_data;

		return yt921x_dsa_port_setup_tc_tbf_port(ds, port, qopt);
	}
	default:
		return -EOPNOTSUPP;
	}
}

/* ACL: 48 blocks * 8 entries
 *
 * One rule can span multiple entries, but within a block.
 */

static void
yt921x_acl_entry_set(struct yt921x_acl_entry *entry, unsigned int offset,
		     u32 flags, bool set)
{
	if (set)
		entry->key[offset] |= flags;
	entry->mask[offset] |= flags;
}

static unsigned int
yt921x_acl_entries_set_is_fragment(struct yt921x_acl_entry *entries,
				   unsigned int size, bool set)
{
	for (unsigned int i = 0; i < size; i++)
		switch (FIELD_GET(YT921X_ACL_KEYb_TYPE_M, entries[i].key[1])) {
		case YT921X_ACL_TYPE_IPV4_DA:
		case YT921X_ACL_TYPE_IPV4_SA:
			yt921x_acl_entry_set(&entries[i], 1,
					     YT921X_ACL_BINb_IPV4_FRAG, set);
			return size;
		case YT921X_ACL_TYPE_IPV6_DA3:
		case YT921X_ACL_TYPE_IPV6_SA3:
			yt921x_acl_entry_set(&entries[i], 1,
					     YT921X_ACL_BINb_IPV6_xA3_FRAG,
					     set);
			return size;
		case YT921X_ACL_TYPE_MISC:
			yt921x_acl_entry_set(&entries[i], 1,
					     YT921X_ACL_BINb_MISC_FRAG, set);
			return size;
		case YT921X_ACL_TYPE_L4:
			yt921x_acl_entry_set(&entries[i], 1,
					     YT921X_ACL_BINb_L4_FRAG, set);
			return size;
		}

	if (size >= YT921X_ACL_ENT_PER_BLK)
		return 0;

	entries[size] = (typeof(*entries)){};
	entries[size].key[1] = YT921X_ACL_KEYb_TYPE(YT921X_ACL_TYPE_MISC);
	yt921x_acl_entry_set(&entries[size], 1, YT921X_ACL_BINb_MISC_FRAG, set);

	return size + 1;
}

static unsigned int
yt921x_acl_entries_set_first_frag(struct yt921x_acl_entry *entries,
				  unsigned int size, bool set)
{
	for (unsigned int i = 0; i < size; i++)
		switch (FIELD_GET(YT921X_ACL_KEYb_TYPE_M, entries[i].key[1])) {
		case YT921X_ACL_TYPE_IPV6_DA2:
		case YT921X_ACL_TYPE_IPV6_SA2:
			yt921x_acl_entry_set(&entries[i], 1,
					     YT921X_ACL_BINb_IPV6_xA2_FIRST_FRAG,
					     set);
			return size;
		case YT921X_ACL_TYPE_MISC:
			yt921x_acl_entry_set(&entries[i], 0,
					     YT921X_ACL_BINa_MISC_FIRST_FRAG,
					     set);
			return size;
		}

	if (size >= YT921X_ACL_ENT_PER_BLK)
		return 0;

	entries[size] = (typeof(*entries)){};
	entries[size].key[1] = YT921X_ACL_KEYb_TYPE(YT921X_ACL_TYPE_MISC);
	yt921x_acl_entry_set(&entries[size], 0,
			     YT921X_ACL_BINa_MISC_FIRST_FRAG, set);

	return size + 1;
}

static unsigned int
yt921x_acl_entries_set_l3_type(struct yt921x_acl_entry *entries,
			       unsigned int size, enum yt921x_l3_type type)
{
	for (unsigned int i = 0; i < size; i++)
		switch (FIELD_GET(YT921X_ACL_KEYb_TYPE_M, entries[i].key[1])) {
		case YT921X_ACL_TYPE_MAC_DA0:
		case YT921X_ACL_TYPE_MAC_SA0:
			entries[i].key[1] |= YT921X_ACL_BINb_MAC_xA0_L3_TYPE(type);
			entries[i].mask[1] |= YT921X_ACL_BINb_MAC_xA0_L3_TYPE_M;
			return size;
		case YT921X_ACL_TYPE_MISC:
			entries[i].key[0] |= YT921X_ACL_BINa_MISC_L3_TYPE(type);
			entries[i].mask[0] |= YT921X_ACL_BINa_MISC_L3_TYPE_M;
			return size;
		}

	if (size >= YT921X_ACL_ENT_PER_BLK)
		return 0;

	entries[size] = (typeof(*entries)){};
	entries[size].key[0] = YT921X_ACL_BINa_MISC_L3_TYPE(type);
	entries[size].key[1] = YT921X_ACL_KEYb_TYPE(YT921X_ACL_TYPE_MISC);
	entries[size].mask[0] = YT921X_ACL_BINa_MISC_L3_TYPE_M;

	return size + 1;
}

static unsigned int
yt921x_acl_entries_set_l4_type(struct yt921x_acl_entry *entries,
			       unsigned int size, enum yt921x_l4_type type)
{
	for (unsigned int i = 0; i < size; i++)
		switch (FIELD_GET(YT921X_ACL_KEYb_TYPE_M, entries[i].key[1])) {
		case YT921X_ACL_TYPE_IPV4_DA:
		case YT921X_ACL_TYPE_IPV4_SA:
			entries[i].key[1] |= YT921X_ACL_BINb_IPV4_L4_TYPE(type);
			entries[i].mask[1] |= YT921X_ACL_BINb_IPV4_L4_TYPE_M;
			return size;
		case YT921X_ACL_TYPE_IPV6_DA0:
		case YT921X_ACL_TYPE_IPV6_DA1:
		case YT921X_ACL_TYPE_IPV6_DA2:
		case YT921X_ACL_TYPE_IPV6_DA3:
		case YT921X_ACL_TYPE_IPV6_SA0:
		case YT921X_ACL_TYPE_IPV6_SA1:
		case YT921X_ACL_TYPE_IPV6_SA2:
		case YT921X_ACL_TYPE_IPV6_SA3:
			entries[i].key[1] |= YT921X_ACL_BINb_IPV6_L4_TYPE(type);
			entries[i].mask[1] |= YT921X_ACL_BINb_IPV6_L4_TYPE_M;
			return size;
		case YT921X_ACL_TYPE_L4:
			entries[i].key[1] |= YT921X_ACL_BINb_L4_TYPE(type);
			entries[i].mask[1] |= YT921X_ACL_BINb_L4_TYPE_M;
			return size;
		case YT921X_ACL_TYPE_MISC:
			entries[i].key[1] |= YT921X_ACL_BINb_MISC_L4_TYPE(type);
			entries[i].mask[1] |= YT921X_ACL_BINb_MISC_L4_TYPE_M;
			return size;
		}

	if (size >= YT921X_ACL_ENT_PER_BLK)
		return 0;

	entries[size] = (typeof(*entries)){};
	entries[size].key[1] = YT921X_ACL_BINb_MISC_L4_TYPE(type) |
			       YT921X_ACL_KEYb_TYPE(YT921X_ACL_TYPE_MISC);
	entries[size].mask[1] = YT921X_ACL_BINb_MISC_L4_TYPE_M;

	return size + 1;
}

static struct yt921x_acl_entry *
yt921x_acl_entries_new(struct yt921x_acl_entry *entries, unsigned int *sizep,
		       u32 type)
{
	unsigned int size = *sizep;

	if (size >= YT921X_ACL_ENT_PER_BLK)
		return NULL;

	entries[size] = (typeof(*entries)){};
	entries[size].key[1] = YT921X_ACL_KEYb_TYPE(type);

	(*sizep)++;
	return &entries[size];
}

static struct yt921x_acl_entry *
yt921x_acl_entries_find(struct yt921x_acl_entry *entries, unsigned int *sizep,
			u32 type)
{
	for (unsigned int i = 0; i < *sizep; i++)
		if (FIELD_GET(YT921X_ACL_KEYb_TYPE_M, entries[i].key[1]) ==
		    type)
			return &entries[i];
	return yt921x_acl_entries_new(entries, sizep, type);
}

static void
yt921x_acl_rule_set_ports(struct yt921x_acl_rule *aclrule, u16 ord,
			  u16 ports_mask)
{
	struct yt921x_acl_entry *entries = aclrule->entries;

	for (unsigned int i = 0; i < hweight8(aclrule->mask); i++) {
		entries[i].key[1] |= YT921X_ACL_KEYb_SPORTS(ports_mask) |
				     YT921X_ACL_KEYb_ORD(ord);
	}
}

struct yt921x_acl_rule_ext {
	struct yt921x_acl_rule r;

	struct yt921x_marker marker;
};

static int
yt921x_acl_rule_ext_parse_flow_entries(struct yt921x_acl_rule_ext *ruleext,
				       const struct flow_cls_offload *cls)
{
	const struct flow_rule *rule = flow_cls_offload_flow_rule(cls);
	struct yt921x_acl_entry *entries = ruleext->r.entries;
	struct netlink_ext_ack *extack = cls->common.extack;
	const struct flow_dissector *dissector;
	struct yt921x_acl_entry *entry;
	unsigned int size = 0;
	bool use_dport;
	bool use_sport;

	/* Incomplete and probably won't, since it supports custom u32 filters.
	 * New adapters are welcome.
	 */
	dissector = rule->match.dissector;
	if (dissector->used_keys &
	    ~(BIT_ULL(FLOW_DISSECTOR_KEY_CONTROL) |
	      BIT_ULL(FLOW_DISSECTOR_KEY_BASIC) |
	      BIT_ULL(FLOW_DISSECTOR_KEY_IPV4_ADDRS) |
	      BIT_ULL(FLOW_DISSECTOR_KEY_IPV6_ADDRS) |
	      BIT_ULL(FLOW_DISSECTOR_KEY_PORTS) |
	      BIT_ULL(FLOW_DISSECTOR_KEY_PORTS_RANGE) |
	      BIT_ULL(FLOW_DISSECTOR_KEY_ETH_ADDRS) |
	      BIT_ULL(FLOW_DISSECTOR_KEY_IP) |
	      BIT_ULL(FLOW_DISSECTOR_KEY_TCP))) {
		NL_SET_ERR_MSG_MOD(extack, "Unsupported keys used");
		return -EOPNOTSUPP;
	}

	/* Entries */
	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_IPV4_ADDRS)) {
		struct flow_match_ipv4_addrs match;

		flow_rule_match_ipv4_addrs(rule, &match);

		if (match.mask->dst) {
			entry = yt921x_acl_entries_new(entries, &size,
						       YT921X_ACL_TYPE_IPV4_DA);
			if (!entry)
				goto err;

			entry->key[0] |= ntohl(match.key->dst);
			entry->mask[0] |= ntohl(match.mask->dst);
		}

		if (match.mask->src) {
			entry = yt921x_acl_entries_new(entries, &size,
						       YT921X_ACL_TYPE_IPV4_SA);
			if (!entry)
				goto err;

			entry->key[0] |= ntohl(match.key->src);
			entry->mask[0] |= ntohl(match.mask->src);
		}
	}

	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_IPV6_ADDRS)) {
		struct flow_match_ipv6_addrs match;

		flow_rule_match_ipv6_addrs(rule, &match);

		for (unsigned int i = 0; i < 4; i++) {
			if (!match.mask->dst.s6_addr32[i])
				continue;

			entry = yt921x_acl_entries_new(entries, &size,
						       YT921X_ACL_TYPE_IPV6_DA0 + i);
			if (!entry)
				goto err;

			entry->key[0] |= ntohl(match.key->dst.s6_addr32[i]);
			entry->mask[0] |= ntohl(match.mask->dst.s6_addr32[i]);
		}

		for (unsigned int i = 0; i < 4; i++) {
			if (!match.mask->src.s6_addr32[i])
				continue;

			entry = yt921x_acl_entries_new(entries, &size,
						       YT921X_ACL_TYPE_IPV6_SA0 + i);
			if (!entry)
				goto err;

			entry->key[0] |= ntohl(match.key->src.s6_addr32[i]);
			entry->mask[0] |= ntohl(match.mask->src.s6_addr32[i]);
		}
	}

	use_dport = false;
	use_sport = false;
	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_PORTS)) {
		struct flow_match_ports match;

		entry = yt921x_acl_entries_new(entries, &size,
					       YT921X_ACL_TYPE_L4);
		if (!entry)
			goto err;

		flow_rule_match_ports(rule, &match);

		use_dport = !!match.mask->dst;
		use_sport = !!match.mask->src;

		entry->key[0] |= (ntohs(match.key->dst) << 16) |
				 ntohs(match.key->src);
		entry->mask[0] |= (ntohs(match.mask->dst) << 16) |
				  ntohs(match.mask->src);
	}

	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_PORTS_RANGE)) {
		struct flow_match_ports_range match;

		entry = yt921x_acl_entries_find(entries, &size,
						YT921X_ACL_TYPE_L4);
		if (!entry)
			goto err;

		flow_rule_match_ports_range(rule, &match);

		if ((use_dport && match.mask->tp.dst) ||
		    (use_sport && match.mask->tp.src)) {
			NL_SET_ERR_MSG_MOD(extack,
					   "Port mask and range are mutually exclusive");
			return -EINVAL;
		}

		if (match.mask->tp.dst) {
			entry->key[0] |= ntohs(match.key->tp_min.dst) << 16;
			entry->key[1] |= YT921X_ACL_KEYb_L4_DPORT_RANGE_EN;
			entry->mask[0] |= ntohs(match.key->tp_max.dst) << 16;
		}

		if (match.mask->tp.src) {
			entry->key[0] |= ntohs(match.key->tp_min.src);
			entry->key[1] |= YT921X_ACL_KEYb_L4_SPORT_RANGE_EN;
			entry->mask[0] |= ntohs(match.key->tp_max.src);
		}
	}

	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_ETH_ADDRS)) {
		struct flow_match_eth_addrs match;
		u32 mask;

		flow_rule_match_eth_addrs(rule, &match);

		mask = ethaddr_hi4_to_u32(match.mask->dst);
		if (mask) {
			entry = yt921x_acl_entries_new(entries, &size,
						       YT921X_ACL_TYPE_MAC_DA0);
			if (!entry)
				goto err;

			entry->key[0] |= ethaddr_hi4_to_u32(match.key->dst);
			entry->mask[0] |= mask;
		}

		mask = ethaddr_hi4_to_u32(match.mask->src);
		if (mask) {
			entry = yt921x_acl_entries_new(entries, &size,
						       YT921X_ACL_TYPE_MAC_SA0);
			if (!entry)
				goto err;

			entry->key[0] |= ethaddr_hi4_to_u32(match.key->src);
			entry->mask[0] |= mask;
		}

		mask = (ethaddr_lo2_to_u32(match.mask->dst) << 16) |
		       ethaddr_lo2_to_u32(match.mask->src);
		if (mask) {
			entry = yt921x_acl_entries_new(entries, &size,
						       YT921X_ACL_TYPE_MAC_DA1_SA1);
			if (!entry)
				goto err;

			entry->key[0] |= (ethaddr_lo2_to_u32(match.key->dst) << 16) |
					 ethaddr_lo2_to_u32(match.key->src);
			entry->mask[0] |= mask;
		}
	}

	/* Entries + Misc */
	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_BASIC)) {
		struct flow_match_basic match;

		flow_rule_match_basic(rule, &match);

		if (match.mask->n_proto) {
			enum yt921x_l3_type l3type = YT921X_L3_TYPE_OTHER;

			if (match.mask->n_proto == htons(~0))
				switch (match.key->n_proto) {
				case htons(ETH_P_IP):
					l3type = YT921X_L3_TYPE_IPV4;
					break;
				case htons(ETH_P_IPV6):
					l3type = YT921X_L3_TYPE_IPV6;
					break;
				case htons(ETH_P_ARP):
					l3type = YT921X_L3_TYPE_ARP;
					break;
				case htons(ETH_P_LLDP):
					l3type = YT921X_L3_TYPE_LLDP;
					break;
				case htons(ETH_P_PAE):
					l3type = YT921X_L3_TYPE_PAE;
					break;
				case htons(ETH_P_CFM):
					l3type = YT921X_L3_TYPE_ERP;
					break;
				}

			if (l3type != YT921X_L3_TYPE_OTHER) {
				size = yt921x_acl_entries_set_l3_type(entries,
								      size,
								      l3type);
				if (!size)
					goto err;
			} else {
				entry = yt921x_acl_entries_new(entries, &size,
							       YT921X_ACL_TYPE_ETHERTYPE);
				if (!entry)
					goto err;

				entry->key[0] |= ntohs(match.key->n_proto);
				entry->mask[0] |= ntohs(match.mask->n_proto);
			}
		}

		if (match.mask->ip_proto) {
			enum yt921x_l4_type l4type = YT921X_L4_TYPE_OTHER;

			if (match.mask->ip_proto == (u8)~0)
				switch (match.key->ip_proto) {
				case IPPROTO_TCP:
					l4type = YT921X_L4_TYPE_TCP;
					break;
				case IPPROTO_UDP:
					l4type = YT921X_L4_TYPE_UDP;
					break;
				case IPPROTO_UDPLITE:
					l4type = YT921X_L4_TYPE_UDPLITE;
					break;
				case IPPROTO_ICMP:
					l4type = YT921X_L4_TYPE_ICMP;
					break;
				case IPPROTO_IGMP:
					l4type = YT921X_L4_TYPE_IGMP;
					break;
				}

			if (l4type != YT921X_L4_TYPE_OTHER) {
				size = yt921x_acl_entries_set_l4_type(entries,
								      size,
								      l4type);
				if (!size)
					goto err;
			} else {
				entry = yt921x_acl_entries_find(entries, &size,
								YT921X_ACL_TYPE_MISC);
				if (!entry)
					goto err;

				entry->key[0] |= YT921X_ACL_BINa_MISC_IP_PROTO(match.key->ip_proto);
				entry->mask[0] |= YT921X_ACL_BINa_MISC_IP_PROTO(match.mask->ip_proto);
			}
		}
	}

	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_CONTROL)) {
		u32 supp_flags = FLOW_DIS_IS_FRAGMENT | FLOW_DIS_FIRST_FRAG;
		struct flow_match_control match;

		flow_rule_match_control(rule, &match);
		if (!flow_rule_is_supp_control_flags(supp_flags,
						     match.mask->flags, extack))
			return -EOPNOTSUPP;

		if (match.mask->flags & FLOW_DIS_IS_FRAGMENT) {
			bool set = match.key->flags & FLOW_DIS_IS_FRAGMENT;

			size = yt921x_acl_entries_set_is_fragment(entries, size,
								  set);
			if (!size)
				goto err;
		}
		if (match.mask->flags & FLOW_DIS_FIRST_FRAG) {
			bool set = match.key->flags & FLOW_DIS_FIRST_FRAG;

			size = yt921x_acl_entries_set_first_frag(entries, size,
								 set);
			if (!size)
				goto err;
		}
	}

	/* Misc only */
	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_IP)) {
		struct flow_match_ip match;

		flow_rule_match_ip(rule, &match);
		if (match.mask->ttl) {
			NL_SET_ERR_MSG_MOD(extack,
					   "Matching on TTL not supported");
			return -EOPNOTSUPP;
		}

		if (match.mask->tos) {
			entry = yt921x_acl_entries_find(entries, &size,
							YT921X_ACL_TYPE_MISC);
			if (!entry)
				goto err;

			entry->key[0] |= YT921X_ACL_BINa_MISC_TOS(match.key->tos);
			entry->mask[0] |= YT921X_ACL_BINa_MISC_TOS(match.mask->tos);
		}
	}

	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_TCP)) {
		struct flow_match_tcp match;

		flow_rule_match_tcp(rule, &match);
		if (match.mask->flags & htons(~0xff)) {
			NL_SET_ERR_MSG_MOD(extack, "Unsupported TCP flags");
			return -EOPNOTSUPP;
		}

		if (match.mask->flags) {
			entry = yt921x_acl_entries_find(entries, &size,
							YT921X_ACL_TYPE_MISC);
			if (!entry)
				goto err;

			entry->key[0] |= YT921X_ACL_BINa_MISC_TCP_FLAGS(ntohs(match.key->flags));
			entry->mask[0] |= YT921X_ACL_BINa_MISC_TCP_FLAGS(ntohs(match.mask->flags));
		}
	}

	if (!size) {
		NL_SET_ERR_MSG_MOD(extack, "Empty rule generated, this should not happen");
		return -EOPNOTSUPP;
	}

	ruleext->r.mask = (1 << size) - 1;
	return 0;

err:
	NL_SET_ERR_MSG_MOD(extack, "Rule too complex");
	return -EOPNOTSUPP;
}

static int
yt921x_acl_rule_ext_parse_flow_action(struct yt921x_acl_rule_ext *ruleext,
				      const struct flow_cls_offload *cls,
				      struct yt921x_priv *priv, int port)
{
	const struct flow_rule *rule = flow_cls_offload_flow_rule(cls);
	const struct flow_action *flow_action = &rule->action;
	struct netlink_ext_ack *extack = cls->common.extack;
	enum flow_action_id redir_act = NUM_FLOW_ACTIONS;
	const struct flow_action_entry *act;
	u32 *action = ruleext->r.action;
	bool seen_priority = false;
	const char *reason = NULL;
	bool seen_police = false;
	unsigned int i;
	int res;

	memset(action, 0, 3 * sizeof(*action));
	flow_action_for_each(i, act, flow_action)
		switch (act->id) {
		case FLOW_ACTION_ACCEPT:
		case FLOW_ACTION_DROP:
		case FLOW_ACTION_REDIRECT:
			if (redir_act != NUM_FLOW_ACTIONS &&
			    redir_act != act->id) {
				reason = "Different redirect actions";
				goto fallback;
			}
			redir_act = act->id;

			switch (act->id) {
			case FLOW_ACTION_ACCEPT:
				action[2] |= YT921X_ACL_ACTc_FWD_EN |
					     YT921X_ACL_ACTc_FWD_FWD;
				break;
			case FLOW_ACTION_DROP:
				action[2] |= YT921X_ACL_ACTc_FWD_EN |
					     YT921X_ACL_ACTc_FWD_REDIR;
				break;
			case FLOW_ACTION_REDIRECT: {
				struct dsa_port *to_dp;

				to_dp = dsa_port_from_netdev(act->dev);
				if (IS_ERR(to_dp) || to_dp->ds != &priv->ds) {
					reason = "Redirect to non-local port";
					goto fallback;
				}

				action[2] |= YT921X_ACL_ACTc_FWD_EN |
					     YT921X_ACL_ACTc_FWD_REDIR |
					     YT921X_ACL_ACTc_FWD_REDIR_DPORTn(to_dp->index);
				break;
			}
			default:
				break;
			}
			break;
		case FLOW_ACTION_PRIORITY:
			if (seen_priority) {
				action[0] &= ~YT921X_ACL_ACTa_PRIO_EN;
				action[1] &= ~YT921X_ACL_ACTb_PRIO_M;

				reason = "Multiple priority actions";
				goto fallback;
			}
			seen_priority = true;

			if (act->priority >= YT921X_PRIO_NUM) {
				NL_SET_ERR_MSG_MOD(extack,
						   "Priority value is too high");
				return -EOPNOTSUPP;
			}
			action[0] |= YT921X_ACL_ACTa_PRIO_EN;
			action[1] |= YT921X_ACL_ACTb_PRIO(act->priority);
			break;
		case FLOW_ACTION_POLICE: {
			const struct flow_action_police *police = &act->police;

			if (seen_police) {
				action[0] &= ~YT921X_ACL_ACTa_METER_EN;

				reason = "Multiple police actions";
				goto fallback;
			}
			seen_police = true;

			res = yt921x_police_validate(police, flow_action, act,
						     extack);
			if (res)
				return res;

			res = yt921x_marker_tfm_police(&ruleext->marker, police,
						       0, priv, port, extack);
			if (res)
				return res;

			action[0] |= YT921X_ACL_ACTa_METER_EN;
			break;
		}
		default:
fallback:
			if (cls->common.skip_sw) {
				NL_SET_ERR_MSG_FMT_MOD(extack,
						       "Action not supported when skip_sw: %s",
						       reason);
				return -EOPNOTSUPP;
			}
			fallthrough;
		case FLOW_ACTION_TRAP:
			redir_act = FLOW_ACTION_TRAP;

			action[2] &= ~YT921X_ACL_ACTc_FWD_REDIR_DPORTS_M &
				     ~YT921X_ACL_ACTc_FWD_M;
			action[2] |= YT921X_ACL_ACTc_FWD_EN |
				     YT921X_ACL_ACTc_FWD_TRAP;
			break;
		}

	ruleext->r.sw_assisted = !cls->common.skip_sw;
	return 0;
}

static int
yt921x_acl_rule_ext_parse_flow(struct yt921x_acl_rule_ext *ruleext, int port,
			       const struct flow_cls_offload *cls, bool ingress,
			       struct yt921x_priv *priv)
{
	struct netlink_ext_ack *extack = cls->common.extack;
	int res;

	if (!ingress) {
		NL_SET_ERR_MSG_MOD(extack, "Only ingress is supported");
		return -EOPNOTSUPP;
	}

	if (cls->common.chain_index) {
		NL_SET_ERR_MSG(extack, "Only chain 0 is supported");
		return -EOPNOTSUPP;
	}

	res = yt921x_acl_rule_ext_parse_flow_action(ruleext, cls, priv, port);
	if (res)
		return res;
	res = yt921x_acl_rule_ext_parse_flow_entries(ruleext, cls);
	if (res)
		return res;

	yt921x_acl_rule_set_ports(&ruleext->r, 0, BIT(port));
	ruleext->r.tag = cls->cookie;
	ruleext->r.type = TC_SETUP_CLSFLOWER;
	return 0;
}

static unsigned int
yt921x_acl_find(const struct yt921x_priv *priv, enum tc_setup_type type,
		unsigned long tag)
{
	for (unsigned int blkid = 0; blkid < YT921X_ACL_BLK_NUM; blkid++) {
		const struct yt921x_acl_blk *aclblk = priv->acl_blks[blkid];

		if (!aclblk)
			continue;

		for (unsigned int i = 0; i < YT921X_ACL_ENT_PER_BLK; i++)
			if (aclblk->rules[i] && aclblk->rules[i]->tag == tag &&
			    aclblk->rules[i]->type == type)
				return YT921X_ACL_ENT_PER_BLK * blkid + i;
	}

	return UINT_MAX;
}

static unsigned int
yt921x_acl_reserve(struct yt921x_priv *priv, unsigned int entscnt,
		   struct netlink_ext_ack *extack)
{
	int candidates[YT921X_ACL_ENT_PER_BLK + 1];
	unsigned int acl_used_cnt = 0;

	if (WARN_ON(entscnt > YT921X_ACL_ENT_PER_BLK))
		return UINT_MAX;

	for (unsigned int i = 0; i < ARRAY_SIZE(candidates); i++)
		candidates[i] = -1;
	for (unsigned int i = YT921X_ACL_BLK_NUM; i-- > 0;) {
		unsigned int blk_used_cnt = hweight8(priv->acl_masks[i]);

		candidates[blk_used_cnt] = i;
		acl_used_cnt += blk_used_cnt;
	}

	if (acl_used_cnt >= YT921X_ACL_NUM) {
		NL_SET_ERR_MSG_MOD(extack, "ACL entry limit reached");
		return UINT_MAX;
	}
	if (acl_used_cnt + entscnt <= YT921X_ACL_NUM)
		for (unsigned int i = YT921X_ACL_ENT_PER_BLK - entscnt + 1;
		     i-- > 0;)
			if (candidates[i] >= 0)
				return YT921X_ACL_ENT_PER_BLK * candidates[i] +
				       ffz(priv->acl_masks[candidates[i]]);

	NL_SET_ERR_MSG_MOD(extack,
			   "ACL entry allocation failed, simplify your rules or remove existing rules");
	return UINT_MAX;
}

static int
yt921x_acl_commit(struct yt921x_priv *priv, unsigned int entid, u8 entsmask)
{
	const struct yt921x_acl_rule *aclrule;
	const struct yt921x_acl_blk *aclblk;
	unsigned int blkid;
	unsigned int binid;
	unsigned long mask;
	u32 zeros[3] = {};
	unsigned int i;
	unsigned int o;
	u32 ctrl;
	int res;

	blkid = entid / YT921X_ACL_ENT_PER_BLK;
	binid = entid % YT921X_ACL_ENT_PER_BLK;
	aclblk = priv->acl_blks[blkid];
	aclrule = aclblk->rules[binid];

	/* Write actions */
	res = yt921x_reg96_write(priv, YT921X_ACLn_ACT(entid),
				 aclrule ? aclrule->action : zeros);
	if (res)
		return res;

	/* Select the block */
	ctrl = YT921X_ACL_BLK_CMD_MODIFY | YT921X_ACL_BLK_CMD_BLKID(blkid);
	res = yt921x_reg_write(priv, YT921X_ACL_BLK_CMD, ctrl);
	if (res)
		return res;

	/* Write keys and masks */
	ctrl = 0;
	for (unsigned int i = 0; i < YT921X_ACL_ENT_PER_BLK; i++)
		ctrl |= YT921X_ACL_BLK_KEEP_KEEPn(i);

	mask = entsmask;
	i = 0;
	for_each_set_bit(o, &mask, YT921X_ACL_ENT_PER_BLK) {
		res = yt921x_reg64_write(priv, YT921X_ACLn_KEYm(blkid, o),
					 aclrule ? aclrule->entries[i].key :
					 zeros);
		if (res)
			return res;

		res = yt921x_reg64_write(priv, YT921X_ACLn_MASKm(blkid, o),
					 aclrule ? aclrule->entries[i].mask :
					 zeros);
		if (res)
			return res;

		ctrl &= ~YT921X_ACL_BLK_KEEP_KEEPn(o);
		i++;
	}

	res = yt921x_reg_write(priv, YT921X_ACL_BLK_KEEP, ctrl);
	if (res)
		return res;

	ctrl = 0;
	for (unsigned int i = 0; i < YT921X_ACL_ENT_PER_BLK; i++) {
		const struct yt921x_acl_rule *other = aclblk->rules[i];

		if (!other)
			continue;

		mask = other->mask;
		for_each_set_bit(o, &mask, YT921X_ACL_ENT_PER_BLK)
			ctrl |= YT921X_ACL_ENTRY_ENm(o) |
				YT921X_ACL_ENTRY_GRPIDm(o, i);
	}
	res = yt921x_reg_write(priv, YT921X_ACLn_ENTRY(blkid), ctrl);
	if (res)
		return res;

	/* Commit the block */
	ctrl = YT921X_ACL_BLK_CMD_BLKID(blkid);
	res = yt921x_reg_write(priv, YT921X_ACL_BLK_CMD, ctrl);
	if (res)
		return res;

	return 0;
}

static int
yt921x_acl_del(struct yt921x_priv *priv, enum tc_setup_type type,
	       unsigned long tag)
{
	struct yt921x_acl_rule *aclrule;
	struct yt921x_acl_blk *aclblk;
	unsigned int binid;
	unsigned int blkid;
	unsigned int entid;
	int res;

	entid = yt921x_acl_find(priv, type, tag);
	if (entid == UINT_MAX)
		return -ENOENT;

	blkid = entid / YT921X_ACL_ENT_PER_BLK;
	binid = entid % YT921X_ACL_ENT_PER_BLK;
	aclblk = priv->acl_blks[blkid];
	aclrule = aclblk->rules[binid];

	aclblk->rules[binid] = NULL;
	res = yt921x_acl_commit(priv, entid, aclrule->mask);
	/* the kernel never rolls back on failure */

	if (aclrule->action[0] & YT921X_ACL_ACTa_METER_EN)
		clear_bit(FIELD_GET(YT921X_ACL_ACTa_METER_ID_M,
				    aclrule->action[0]),
			  priv->meters_map);
	priv->acl_masks[blkid] &= ~aclrule->mask;
	kvfree(aclrule);
	if (!priv->acl_masks[blkid]) {
		kvfree(aclblk);
		priv->acl_blks[blkid] = NULL;
	}
	return res;
}

static int
yt921x_acl_add(struct yt921x_priv *priv,
	       const struct yt921x_acl_rule_ext *ruleext,
	       struct netlink_ext_ack *extack)
{
	unsigned int entscnt = hweight8(ruleext->r.mask);
	struct yt921x_acl_rule *aclrule;
	struct yt921x_acl_blk *aclblk;
	bool use_trap = false;
	unsigned int meterid;
	unsigned long mask;
	unsigned int binid;
	unsigned int blkid;
	unsigned int entid;
	unsigned int o;
	int res;

	/* Allocate resources */
	entid = yt921x_acl_reserve(priv, entscnt, extack);
	if (entid == UINT_MAX)
		return -EOPNOTSUPP;

	if (!(ruleext->r.action[0] & YT921X_ACL_ACTa_METER_EN)) {
		meterid = YT921X_METER_NUM;
	} else {
		meterid = find_first_zero_bit(priv->meters_map,
					      YT921X_METER_NUM);
		if (meterid < YT921X_METER_NUM) {
			res = yt921x_meter_config(priv, meterid,
						  &ruleext->marker);
			if (res)
				return res;
		} else if (ruleext->r.sw_assisted) {
			use_trap = true;
		} else {
			NL_SET_ERR_MSG_MOD(extack,
					   "No more meters available");
			return -EOPNOTSUPP;
		}
	}

	/* Prepare acl block ctrlblk */
	blkid = entid / YT921X_ACL_ENT_PER_BLK;
	binid = entid % YT921X_ACL_ENT_PER_BLK;
	aclblk = priv->acl_blks[blkid];
	if (!aclblk) {
		aclblk = kvzalloc_obj(*aclblk);
		if (!aclblk)
			return -ENOMEM;
		priv->acl_blks[blkid] = aclblk;
	}

	/* Prepare acl rule ctrlblk */
	aclrule = kvmemdup(&ruleext->r,
			   offsetof(struct yt921x_acl_rule, entries[entscnt]),
			   GFP_KERNEL);
	if (!aclrule) {
		res = -ENOMEM;
		goto err;
	}

	/* Replace the placeholder resource IDs */
	aclrule->mask = 0;
	mask = priv->acl_masks[blkid];
	for_each_clear_bit(o, &mask, YT921X_ACL_ENT_PER_BLK) {
		aclrule->mask |= BIT(o);
		entscnt--;
		if (!entscnt)
			break;
	}

	if (use_trap) {
		aclrule->action[2] &= ~YT921X_ACL_ACTc_FWD_REDIR_DPORTS_M &
				      ~YT921X_ACL_ACTc_FWD_M;
		aclrule->action[2] |= YT921X_ACL_ACTc_FWD_EN |
				      YT921X_ACL_ACTc_FWD_TRAP;
	}
	if (meterid < YT921X_METER_NUM)
		aclrule->action[0] |= YT921X_ACL_ACTa_METER_ID(meterid);
	else
		aclrule->action[0] &= ~YT921X_ACL_ACTa_METER_EN;

	/* Write rules */
	aclblk->rules[binid] = aclrule;
	res = yt921x_acl_commit(priv, entid, aclrule->mask);
	if (res) {
		aclblk->rules[binid] = NULL;
		kvfree(aclrule);
		goto err;
	}

	if (meterid < YT921X_METER_NUM)
		set_bit(meterid, priv->meters_map);
	priv->acl_masks[blkid] |= aclrule->mask;
	return 0;

err:
	if (!priv->acl_masks[blkid]) {
		kvfree(aclblk);
		priv->acl_blks[blkid] = NULL;
	}
	return res;
}

static int
yt921x_dsa_cls_flower_del(struct dsa_switch *ds, int port,
			  struct flow_cls_offload *cls, bool ingress)
{
	struct yt921x_priv *priv = to_yt921x_priv(ds);
	int res;

	mutex_lock(&priv->reg_lock);
	res = yt921x_acl_del(priv, TC_SETUP_CLSFLOWER, cls->cookie);
	mutex_unlock(&priv->reg_lock);

	return res;
}

static int
yt921x_dsa_cls_flower_add(struct dsa_switch *ds, int port,
			  struct flow_cls_offload *cls, bool ingress)
{
	struct netlink_ext_ack *extack = cls->common.extack;
	struct yt921x_priv *priv = to_yt921x_priv(ds);
	struct yt921x_acl_rule_ext ruleext;
	int res;

	res = yt921x_acl_rule_ext_parse_flow(&ruleext, port, cls, ingress,
					     priv);
	if (res)
		return res;

	mutex_lock(&priv->reg_lock);
	res = yt921x_acl_add(priv, &ruleext, extack);
	mutex_unlock(&priv->reg_lock);

	return res;
}

static int
yt921x_mirror_del(struct yt921x_priv *priv, int port, bool ingress)
{
	u32 mask;

	if (ingress)
		mask = YT921X_MIRROR_IGR_PORTn(port);
	else
		mask = YT921X_MIRROR_EGR_PORTn(port);
	return yt921x_reg_clear_bits(priv, YT921X_MIRROR, mask);
}

static int
yt921x_mirror_add(struct yt921x_priv *priv, int port, bool ingress,
		  int to_local_port, struct netlink_ext_ack *extack)
{
	u32 srcs;
	u32 ctrl;
	u32 val;
	u32 dst;
	int res;

	if (ingress)
		srcs = YT921X_MIRROR_IGR_PORTn(port);
	else
		srcs = YT921X_MIRROR_EGR_PORTn(port);
	dst = YT921X_MIRROR_PORT(to_local_port);

	res = yt921x_reg_read(priv, YT921X_MIRROR, &val);
	if (res)
		return res;

	/* other mirror tasks & different dst port -> conflict */
	if ((val & ~srcs & (YT921X_MIRROR_EGR_PORTS_M |
			    YT921X_MIRROR_IGR_PORTS_M)) &&
	    (val & YT921X_MIRROR_PORT_M) != dst) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Sniffer port is already configured, delete existing rules & retry");
		return -EBUSY;
	}

	ctrl = val & ~YT921X_MIRROR_PORT_M;
	ctrl |= srcs;
	ctrl |= dst;

	if (ctrl == val)
		return 0;

	return yt921x_reg_write(priv, YT921X_MIRROR, ctrl);
}

static void
yt921x_dsa_port_mirror_del(struct dsa_switch *ds, int port,
			   struct dsa_mall_mirror_tc_entry *mirror)
{
	struct yt921x_priv *priv = to_yt921x_priv(ds);
	struct device *dev = to_device(priv);
	int res;

	mutex_lock(&priv->reg_lock);
	res = yt921x_mirror_del(priv, port, mirror->ingress);
	mutex_unlock(&priv->reg_lock);

	if (res)
		dev_err(dev, "Failed to %s port %d: %i\n", "unmirror",
			port, res);
}

static int
yt921x_dsa_port_mirror_add(struct dsa_switch *ds, int port,
			   struct dsa_mall_mirror_tc_entry *mirror,
			   bool ingress, struct netlink_ext_ack *extack)
{
	struct yt921x_priv *priv = to_yt921x_priv(ds);
	int res;

	mutex_lock(&priv->reg_lock);
	res = yt921x_mirror_add(priv, port, ingress,
				mirror->to_local_port, extack);
	mutex_unlock(&priv->reg_lock);

	return res;
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
	struct yt921x_priv *priv = to_yt921x_priv(ds);
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
	struct yt921x_priv *priv = to_yt921x_priv(ds);
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
	struct device *dev = to_device(priv);
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
	struct device *dev = to_device(priv);
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
	struct yt921x_priv *priv = to_yt921x_priv(ds);
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
	struct yt921x_priv *priv = to_yt921x_priv(ds);
	struct device *dev = to_device(priv);
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
	struct yt921x_priv *priv = to_yt921x_priv(ds);
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
	struct yt921x_priv *priv = to_yt921x_priv(ds);
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
	struct yt921x_priv *priv = to_yt921x_priv(ds);
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
	struct yt921x_priv *priv = to_yt921x_priv(ds);
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
	struct yt921x_priv *priv = to_yt921x_priv(ds);
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
	struct yt921x_priv *priv = to_yt921x_priv(ds);
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
	struct yt921x_priv *priv = to_yt921x_priv(ds);
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
	struct yt921x_priv *priv = to_yt921x_priv(ds);
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
	struct yt921x_priv *priv = to_yt921x_priv(ds);
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
	struct yt921x_priv *priv = to_yt921x_priv(ds);
	struct device *dev = to_device(priv);
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
	struct yt921x_priv *priv = to_yt921x_priv(ds);
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
	struct yt921x_priv *priv = to_yt921x_priv(ds);
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
	struct yt921x_priv *priv = to_yt921x_priv(ds);
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
	struct yt921x_priv *priv = to_yt921x_priv(ds);
	struct dsa_port *dp = dsa_to_port(ds, port);
	struct device *dev = to_device(priv);
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
	struct yt921x_priv *priv = to_yt921x_priv(ds);
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
	struct yt921x_priv *priv = to_yt921x_priv(ds);
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
	struct yt921x_priv *priv = to_yt921x_priv(ds);
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
	struct yt921x_priv *priv = to_yt921x_priv(ds);
	struct device *dev = to_device(priv);
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
	u32 mask;
	int res;

	mask = YT921X_PORT_LINK | YT921X_PORT_RX_MAC_EN | YT921X_PORT_TX_MAC_EN;
	res = yt921x_reg_clear_bits(priv, YT921X_PORTn_CTRL(port), mask);
	if (res)
		return res;

	if (yt921x_port_is_external(port)) {
		mask = YT921X_SERDES_LINK;
		res = yt921x_reg_clear_bits(priv, YT921X_SERDESn(port), mask);
		if (res)
			return res;

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
	u32 mask;
	u32 ctrl;
	int res;

	switch (speed) {
	case SPEED_10:
		ctrl = YT921X_PORT_SPEED_10;
		break;
	case SPEED_100:
		ctrl = YT921X_PORT_SPEED_100;
		break;
	case SPEED_1000:
		ctrl = YT921X_PORT_SPEED_1000;
		break;
	case SPEED_2500:
		ctrl = YT921X_PORT_SPEED_2500;
		break;
	case SPEED_10000:
		ctrl = YT921X_PORT_SPEED_10000;
		break;
	default:
		return -EINVAL;
	}
	if (duplex == DUPLEX_FULL)
		ctrl |= YT921X_PORT_DUPLEX_FULL;
	if (tx_pause)
		ctrl |= YT921X_PORT_TX_PAUSE;
	if (rx_pause)
		ctrl |= YT921X_PORT_RX_PAUSE;
	ctrl |= YT921X_PORT_RX_MAC_EN | YT921X_PORT_TX_MAC_EN;
	res = yt921x_reg_write(priv, YT921X_PORTn_CTRL(port), ctrl);
	if (res)
		return res;

	if (yt921x_port_is_external(port)) {
		mask = YT921X_SERDES_SPEED_M;
		switch (speed) {
		case SPEED_10:
			ctrl = YT921X_SERDES_SPEED_10;
			break;
		case SPEED_100:
			ctrl = YT921X_SERDES_SPEED_100;
			break;
		case SPEED_1000:
			ctrl = YT921X_SERDES_SPEED_1000;
			break;
		case SPEED_2500:
			ctrl = YT921X_SERDES_SPEED_2500;
			break;
		case SPEED_10000:
			ctrl = YT921X_SERDES_SPEED_10000;
			break;
		default:
			return -EINVAL;
		}
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
		ctrl |= YT921X_SERDES_LINK;
		res = yt921x_reg_update_bits(priv, YT921X_SERDESn(port),
					     mask, ctrl);
		if (res)
			return res;

		mask = YT921X_XMII_LINK;
		res = yt921x_reg_set_bits(priv, YT921X_XMIIn(port), mask);
		if (res)
			return res;

		switch (speed) {
		case SPEED_10:
			ctrl = YT921X_MDIO_POLLING_SPEED_10;
			break;
		case SPEED_100:
			ctrl = YT921X_MDIO_POLLING_SPEED_100;
			break;
		case SPEED_1000:
			ctrl = YT921X_MDIO_POLLING_SPEED_1000;
			break;
		case SPEED_2500:
			ctrl = YT921X_MDIO_POLLING_SPEED_2500;
			break;
		case SPEED_10000:
			ctrl = YT921X_MDIO_POLLING_SPEED_10000;
			break;
		default:
			return -EINVAL;
		}
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
	struct device *dev = to_device(priv);
	u32 mask;
	u32 ctrl;
	int res;

	if (!yt921x_port_is_external(port)) {
		if (interface != PHY_INTERFACE_MODE_INTERNAL) {
			dev_err(dev, "Wrong mode %d on port %d\n",
				interface, port);
			return -EINVAL;
		}
		return 0;
	}

	switch (interface) {
	/* SERDES */
	case PHY_INTERFACE_MODE_SGMII:
	case PHY_INTERFACE_MODE_100BASEX:
	case PHY_INTERFACE_MODE_1000BASEX:
	case PHY_INTERFACE_MODE_2500BASEX:
		mask = YT921X_SERDES_CTRL_PORTn(port);
		res = yt921x_reg_set_bits(priv, YT921X_SERDES_CTRL, mask);
		if (res)
			return res;

		mask = YT921X_XMII_CTRL_PORTn(port);
		res = yt921x_reg_clear_bits(priv, YT921X_XMII_CTRL, mask);
		if (res)
			return res;

		mask = YT921X_SERDES_MODE_M;
		switch (interface) {
		case PHY_INTERFACE_MODE_SGMII:
			ctrl = YT921X_SERDES_MODE_SGMII;
			break;
		case PHY_INTERFACE_MODE_100BASEX:
			ctrl = YT921X_SERDES_MODE_100BASEX;
			break;
		case PHY_INTERFACE_MODE_1000BASEX:
			ctrl = YT921X_SERDES_MODE_1000BASEX;
			break;
		case PHY_INTERFACE_MODE_2500BASEX:
			ctrl = YT921X_SERDES_MODE_2500BASEX;
			break;
		default:
			return -EINVAL;
		}
		res = yt921x_reg_update_bits(priv, YT921X_SERDESn(port),
					     mask, ctrl);
		if (res)
			return res;

		break;
	/* add XMII support here */
	default:
		return -EINVAL;
	}

	return 0;
}

static void
yt921x_phylink_mac_link_down(struct phylink_config *config, unsigned int mode,
			     phy_interface_t interface)
{
	struct dsa_port *dp = dsa_phylink_to_port(config);
	struct yt921x_priv *priv = to_yt921x_priv(dp->ds);
	int port = dp->index;
	int res;

	/* No need to sync; port control block is hold until device remove */
	cancel_delayed_work(&priv->ports[port].mib_read);

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
	struct yt921x_priv *priv = to_yt921x_priv(dp->ds);
	int port = dp->index;
	int res;

	mutex_lock(&priv->reg_lock);
	res = yt921x_port_up(priv, port, mode, interface, speed, duplex,
			     tx_pause, rx_pause);
	mutex_unlock(&priv->reg_lock);

	if (res)
		dev_err(dp->ds->dev, "Failed to %s port %d: %i\n", "bring up",
			port, res);

	schedule_delayed_work(&priv->ports[port].mib_read, 0);
}

static void
yt921x_phylink_mac_config(struct phylink_config *config, unsigned int mode,
			  const struct phylink_link_state *state)
{
	struct dsa_port *dp = dsa_phylink_to_port(config);
	struct yt921x_priv *priv = to_yt921x_priv(dp->ds);
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
	struct yt921x_priv *priv = to_yt921x_priv(ds);
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
	} else if (info->external_mask & BIT(port)) {
		/* TODO: external ports may support SERDES only, XMII only, or
		 * SERDES + XMII depending on the chip. However, we can't get
		 * the accurate config table due to lack of document, thus
		 * we simply declare SERDES + XMII and rely on the correctness
		 * of devicetree for now.
		 */

		/* SERDES */
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

		/* XMII */

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

static int yt921x_dsa_port_setup(struct dsa_switch *ds, int port)
{
	struct yt921x_priv *priv = to_yt921x_priv(ds);
	int res;

	mutex_lock(&priv->reg_lock);
	res = yt921x_port_setup(priv, port);
	mutex_unlock(&priv->reg_lock);

	return res;
}

/* Not "port" - DSCP mapping is global */
static int __maybe_unused
yt921x_dsa_port_get_dscp_prio(struct dsa_switch *ds, int port, u8 dscp)
{
	struct yt921x_priv *priv = to_yt921x_priv(ds);
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
	struct yt921x_priv *priv = to_yt921x_priv(ds);
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
	struct yt921x_priv *priv = to_yt921x_priv(ds);
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

static int yt921x_chip_detect(struct yt921x_priv *priv)
{
	struct device *dev = to_device(priv);
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
	struct device *dev = to_device(priv);
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

static int yt921x_chip_setup_tc(struct yt921x_priv *priv)
{
	unsigned int op_ns;
	u32 ctrl;
	int res;

	op_ns = 8 * priv->cycle_ns;

	ctrl = max(priv->meter_slot_ns / op_ns, YT921X_METER_SLOT_MIN);
	res = yt921x_reg_write(priv, YT921X_METER_SLOT, ctrl);
	if (res)
		return res;
	priv->meter_slot_ns = ctrl * op_ns;

	ctrl = max(priv->port_shape_slot_ns / op_ns,
		   YT921X_PORT_SHAPE_SLOT_MIN);
	res = yt921x_reg_write(priv, YT921X_PORT_SHAPE_SLOT, ctrl);
	if (res)
		return res;
	priv->port_shape_slot_ns = ctrl * op_ns;

	return 0;
}

static int yt921x_chip_setup_acl(struct yt921x_priv *priv)
{
	u32 ctrl;
	int res;

	ctrl = YT921X_ACL_PERMIT_UNMATCH_PORTS_M;
	res = yt921x_reg_write(priv, YT921X_ACL_PERMIT_UNMATCH, ctrl);
	if (res)
		return res;

	ctrl = YT921X_ACL_PORT_PORTS_M;
	res = yt921x_reg_write(priv, YT921X_ACL_PORT, ctrl);
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

	return 0;
}

static int yt921x_dsa_setup(struct dsa_switch *ds)
{
	struct yt921x_priv *priv = to_yt921x_priv(ds);
	struct device *dev = to_device(priv);
	struct device_node *np = dev->of_node;
	struct device_node *child;
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

	mutex_lock(&priv->reg_lock);
	res = yt921x_chip_setup(priv);
	mutex_unlock(&priv->reg_lock);

	if (res)
		return res;

	return 0;
}

static const struct phylink_mac_ops yt921x_phylink_mac_ops = {
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
	.port_setup		= yt921x_dsa_port_setup,
#if IS_ENABLED(CONFIG_DCB)
	/* dscp */
	.port_get_dscp_prio	= yt921x_dsa_port_get_dscp_prio,
	.port_del_dscp_prio	= yt921x_dsa_port_del_dscp_prio,
	.port_add_dscp_prio	= yt921x_dsa_port_add_dscp_prio,
#endif
	/* chip */
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

	for (size_t i = ARRAY_SIZE(priv->ports); i-- > 0; ) {
		struct yt921x_port *pp = &priv->ports[i];

		disable_delayed_work_sync(&pp->mib_read);
	}

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
		INIT_DELAYED_WORK(&pp->mib_read, yt921x_poll_mib);
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
