// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2009 Felix Fietkau <nbd@nbd.name>
 * Copyright (C) 2011-2012 Gabor Juhos <juhosg@openwrt.org>
 * Copyright (c) 2015, 2019, The Linux Foundation. All rights reserved.
 * Copyright (c) 2016 John Crispin <john@phrozen.org>
 */

#include <linux/netdevice.h>
#include <net/dsa.h>
#include <linux/if_bridge.h>

#include "qca8k.h"

#define MIB_DESC(_s, _o, _n)	\
	{			\
		.size = (_s),	\
		.offset = (_o),	\
		.name = (_n),	\
	}

const struct qca8k_mib_desc ar8327_mib[] = {
	MIB_DESC(1, 0x00, "RxBroad"),
	MIB_DESC(1, 0x04, "RxPause"),
	MIB_DESC(1, 0x08, "RxMulti"),
	MIB_DESC(1, 0x0c, "RxFcsErr"),
	MIB_DESC(1, 0x10, "RxAlignErr"),
	MIB_DESC(1, 0x14, "RxRunt"),
	MIB_DESC(1, 0x18, "RxFragment"),
	MIB_DESC(1, 0x1c, "Rx64Byte"),
	MIB_DESC(1, 0x20, "Rx128Byte"),
	MIB_DESC(1, 0x24, "Rx256Byte"),
	MIB_DESC(1, 0x28, "Rx512Byte"),
	MIB_DESC(1, 0x2c, "Rx1024Byte"),
	MIB_DESC(1, 0x30, "Rx1518Byte"),
	MIB_DESC(1, 0x34, "RxMaxByte"),
	MIB_DESC(1, 0x38, "RxTooLong"),
	MIB_DESC(2, 0x3c, "RxGoodByte"),
	MIB_DESC(2, 0x44, "RxBadByte"),
	MIB_DESC(1, 0x4c, "RxOverFlow"),
	MIB_DESC(1, 0x50, "Filtered"),
	MIB_DESC(1, 0x54, "TxBroad"),
	MIB_DESC(1, 0x58, "TxPause"),
	MIB_DESC(1, 0x5c, "TxMulti"),
	MIB_DESC(1, 0x60, "TxUnderRun"),
	MIB_DESC(1, 0x64, "Tx64Byte"),
	MIB_DESC(1, 0x68, "Tx128Byte"),
	MIB_DESC(1, 0x6c, "Tx256Byte"),
	MIB_DESC(1, 0x70, "Tx512Byte"),
	MIB_DESC(1, 0x74, "Tx1024Byte"),
	MIB_DESC(1, 0x78, "Tx1518Byte"),
	MIB_DESC(1, 0x7c, "TxMaxByte"),
	MIB_DESC(1, 0x80, "TxOverSize"),
	MIB_DESC(2, 0x84, "TxByte"),
	MIB_DESC(1, 0x8c, "TxCollision"),
	MIB_DESC(1, 0x90, "TxAbortCol"),
	MIB_DESC(1, 0x94, "TxMultiCol"),
	MIB_DESC(1, 0x98, "TxSingleCol"),
	MIB_DESC(1, 0x9c, "TxExcDefer"),
	MIB_DESC(1, 0xa0, "TxDefer"),
	MIB_DESC(1, 0xa4, "TxLateCol"),
	MIB_DESC(1, 0xa8, "RXUnicast"),
	MIB_DESC(1, 0xac, "TXUnicast"),
};

int qca8k_read(struct qca8k_priv *priv, u32 reg, u32 *val)
{
	return regmap_read(priv->regmap, reg, val);
}

int qca8k_write(struct qca8k_priv *priv, u32 reg, u32 val)
{
	return regmap_write(priv->regmap, reg, val);
}

int qca8k_rmw(struct qca8k_priv *priv, u32 reg, u32 mask, u32 write_val)
{
	return regmap_update_bits(priv->regmap, reg, mask, write_val);
}

static const struct regmap_range qca8k_readable_ranges[] = {
	regmap_reg_range(0x0000, 0x00e4), /* Global control */
	regmap_reg_range(0x0100, 0x0168), /* EEE control */
	regmap_reg_range(0x0200, 0x0270), /* Parser control */
	regmap_reg_range(0x0400, 0x0454), /* ACL */
	regmap_reg_range(0x0600, 0x0718), /* Lookup */
	regmap_reg_range(0x0800, 0x0b70), /* QM */
	regmap_reg_range(0x0c00, 0x0c80), /* PKT */
	regmap_reg_range(0x0e00, 0x0e98), /* L3 */
	regmap_reg_range(0x1000, 0x10ac), /* MIB - Port0 */
	regmap_reg_range(0x1100, 0x11ac), /* MIB - Port1 */
	regmap_reg_range(0x1200, 0x12ac), /* MIB - Port2 */
	regmap_reg_range(0x1300, 0x13ac), /* MIB - Port3 */
	regmap_reg_range(0x1400, 0x14ac), /* MIB - Port4 */
	regmap_reg_range(0x1500, 0x15ac), /* MIB - Port5 */
	regmap_reg_range(0x1600, 0x16ac), /* MIB - Port6 */
};

const struct regmap_access_table qca8k_readable_table = {
	.yes_ranges = qca8k_readable_ranges,
	.n_yes_ranges = ARRAY_SIZE(qca8k_readable_ranges),
};

/* TODO: remove these extra ops when we can support regmap bulk read/write */
int qca8k_bulk_read(struct qca8k_priv *priv, u32 reg, u32 *val, int len)
{
	int i, count = len / sizeof(u32), ret;

	if (priv->mgmt_master && priv->info->ops->read_eth &&
	    !priv->info->ops->read_eth(priv, reg, val, len))
		return 0;

	for (i = 0; i < count; i++) {
		ret = regmap_read(priv->regmap, reg + (i * 4), val + i);
		if (ret < 0)
			return ret;
	}

	return 0;
}

/* TODO: remove these extra ops when we can support regmap bulk read/write */
int qca8k_bulk_write(struct qca8k_priv *priv, u32 reg, u32 *val, int len)
{
	int i, count = len / sizeof(u32), ret;
	u32 tmp;

	if (priv->mgmt_master && priv->info->ops->write_eth &&
	    !priv->info->ops->write_eth(priv, reg, val, len))
		return 0;

	for (i = 0; i < count; i++) {
		tmp = val[i];

		ret = regmap_write(priv->regmap, reg + (i * 4), tmp);
		if (ret < 0)
			return ret;
	}

	return 0;
}

int qca8k_busy_wait(struct qca8k_priv *priv, u32 reg, u32 mask)
{
	u32 val;

	return regmap_read_poll_timeout(priv->regmap, reg, val, !(val & mask), 0,
				       QCA8K_BUSY_WAIT_TIMEOUT * USEC_PER_MSEC);
}

int qca8k_mib_init(struct qca8k_priv *priv)
{
	int ret;

	mutex_lock(&priv->reg_mutex);
	ret = regmap_update_bits(priv->regmap, QCA8K_REG_MIB,
				 QCA8K_MIB_FUNC | QCA8K_MIB_BUSY,
				 FIELD_PREP(QCA8K_MIB_FUNC, QCA8K_MIB_FLUSH) |
				 QCA8K_MIB_BUSY);
	if (ret)
		goto exit;

	ret = qca8k_busy_wait(priv, QCA8K_REG_MIB, QCA8K_MIB_BUSY);
	if (ret)
		goto exit;

	ret = regmap_set_bits(priv->regmap, QCA8K_REG_MIB, QCA8K_MIB_CPU_KEEP);
	if (ret)
		goto exit;

	ret = qca8k_write(priv, QCA8K_REG_MODULE_EN, QCA8K_MODULE_EN_MIB);

exit:
	mutex_unlock(&priv->reg_mutex);
	return ret;
}

void qca8k_port_set_status(struct qca8k_priv *priv, int port, int enable)
{
	u32 mask = QCA8K_PORT_STATUS_TXMAC | QCA8K_PORT_STATUS_RXMAC;

	/* Port 0 and 6 have no internal PHY */
	if (port > 0 && port < 6)
		mask |= QCA8K_PORT_STATUS_LINK_AUTO;

	if (enable)
		regmap_set_bits(priv->regmap, QCA8K_REG_PORT_STATUS(port), mask);
	else
		regmap_clear_bits(priv->regmap, QCA8K_REG_PORT_STATUS(port), mask);
}

void qca8k_get_strings(struct dsa_switch *ds, int port, u32 stringset,
		       uint8_t *data)
{
	struct qca8k_priv *priv = ds->priv;
	int i;

	if (stringset != ETH_SS_STATS)
		return;

	for (i = 0; i < priv->info->mib_count; i++)
		strncpy(data + i * ETH_GSTRING_LEN, ar8327_mib[i].name,
			ETH_GSTRING_LEN);
}

void qca8k_get_ethtool_stats(struct dsa_switch *ds, int port,
			     uint64_t *data)
{
	struct qca8k_priv *priv = ds->priv;
	const struct qca8k_mib_desc *mib;
	u32 reg, i, val;
	u32 hi = 0;
	int ret;

	if (priv->mgmt_master && priv->info->ops->autocast_mib &&
	    priv->info->ops->autocast_mib(ds, port, data) > 0)
		return;

	for (i = 0; i < priv->info->mib_count; i++) {
		mib = &ar8327_mib[i];
		reg = QCA8K_PORT_MIB_COUNTER(port) + mib->offset;

		ret = qca8k_read(priv, reg, &val);
		if (ret < 0)
			continue;

		if (mib->size == 2) {
			ret = qca8k_read(priv, reg + 4, &hi);
			if (ret < 0)
				continue;
		}

		data[i] = val;
		if (mib->size == 2)
			data[i] |= (u64)hi << 32;
	}
}

int qca8k_get_sset_count(struct dsa_switch *ds, int port, int sset)
{
	struct qca8k_priv *priv = ds->priv;

	if (sset != ETH_SS_STATS)
		return 0;

	return priv->info->mib_count;
}

int qca8k_set_mac_eee(struct dsa_switch *ds, int port,
		      struct ethtool_eee *eee)
{
	u32 lpi_en = QCA8K_REG_EEE_CTRL_LPI_EN(port);
	struct qca8k_priv *priv = ds->priv;
	u32 reg;
	int ret;

	mutex_lock(&priv->reg_mutex);
	ret = qca8k_read(priv, QCA8K_REG_EEE_CTRL, &reg);
	if (ret < 0)
		goto exit;

	if (eee->eee_enabled)
		reg |= lpi_en;
	else
		reg &= ~lpi_en;
	ret = qca8k_write(priv, QCA8K_REG_EEE_CTRL, reg);

exit:
	mutex_unlock(&priv->reg_mutex);
	return ret;
}

int qca8k_get_mac_eee(struct dsa_switch *ds, int port,
		      struct ethtool_eee *e)
{
	/* Nothing to do on the port's MAC */
	return 0;
}

void qca8k_port_stp_state_set(struct dsa_switch *ds, int port, u8 state)
{
	struct qca8k_priv *priv = ds->priv;
	u32 stp_state;

	switch (state) {
	case BR_STATE_DISABLED:
		stp_state = QCA8K_PORT_LOOKUP_STATE_DISABLED;
		break;
	case BR_STATE_BLOCKING:
		stp_state = QCA8K_PORT_LOOKUP_STATE_BLOCKING;
		break;
	case BR_STATE_LISTENING:
		stp_state = QCA8K_PORT_LOOKUP_STATE_LISTENING;
		break;
	case BR_STATE_LEARNING:
		stp_state = QCA8K_PORT_LOOKUP_STATE_LEARNING;
		break;
	case BR_STATE_FORWARDING:
	default:
		stp_state = QCA8K_PORT_LOOKUP_STATE_FORWARD;
		break;
	}

	qca8k_rmw(priv, QCA8K_PORT_LOOKUP_CTRL(port),
		  QCA8K_PORT_LOOKUP_STATE_MASK, stp_state);
}

int qca8k_port_bridge_join(struct dsa_switch *ds, int port,
			   struct dsa_bridge bridge,
			   bool *tx_fwd_offload,
			   struct netlink_ext_ack *extack)
{
	struct qca8k_priv *priv = ds->priv;
	int port_mask, cpu_port;
	int i, ret;

	cpu_port = dsa_to_port(ds, port)->cpu_dp->index;
	port_mask = BIT(cpu_port);

	for (i = 0; i < QCA8K_NUM_PORTS; i++) {
		if (dsa_is_cpu_port(ds, i))
			continue;
		if (!dsa_port_offloads_bridge(dsa_to_port(ds, i), &bridge))
			continue;
		/* Add this port to the portvlan mask of the other ports
		 * in the bridge
		 */
		ret = regmap_set_bits(priv->regmap,
				      QCA8K_PORT_LOOKUP_CTRL(i),
				      BIT(port));
		if (ret)
			return ret;
		if (i != port)
			port_mask |= BIT(i);
	}

	/* Add all other ports to this ports portvlan mask */
	ret = qca8k_rmw(priv, QCA8K_PORT_LOOKUP_CTRL(port),
			QCA8K_PORT_LOOKUP_MEMBER, port_mask);

	return ret;
}

void qca8k_port_bridge_leave(struct dsa_switch *ds, int port,
			     struct dsa_bridge bridge)
{
	struct qca8k_priv *priv = ds->priv;
	int cpu_port, i;

	cpu_port = dsa_to_port(ds, port)->cpu_dp->index;

	for (i = 0; i < QCA8K_NUM_PORTS; i++) {
		if (dsa_is_cpu_port(ds, i))
			continue;
		if (!dsa_port_offloads_bridge(dsa_to_port(ds, i), &bridge))
			continue;
		/* Remove this port to the portvlan mask of the other ports
		 * in the bridge
		 */
		regmap_clear_bits(priv->regmap,
				  QCA8K_PORT_LOOKUP_CTRL(i),
				  BIT(port));
	}

	/* Set the cpu port to be the only one in the portvlan mask of
	 * this port
	 */
	qca8k_rmw(priv, QCA8K_PORT_LOOKUP_CTRL(port),
		  QCA8K_PORT_LOOKUP_MEMBER, BIT(cpu_port));
}
