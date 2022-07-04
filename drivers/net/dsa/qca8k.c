// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2009 Felix Fietkau <nbd@nbd.name>
 * Copyright (C) 2011-2012 Gabor Juhos <juhosg@openwrt.org>
 * Copyright (c) 2015, 2019, The Linux Foundation. All rights reserved.
 * Copyright (c) 2016 John Crispin <john@phrozen.org>
 */

#include <linux/module.h>
#include <linux/phy.h>
#include <linux/netdevice.h>
#include <linux/bitfield.h>
#include <linux/regmap.h>
#include <net/dsa.h>
#include <linux/of_net.h>
#include <linux/of_mdio.h>
#include <linux/of_platform.h>
#include <linux/if_bridge.h>
#include <linux/mdio.h>
#include <linux/phylink.h>
#include <linux/gpio/consumer.h>
#include <linux/etherdevice.h>
#include <linux/dsa/tag_qca.h>

#include "qca8k.h"

#define MIB_DESC(_s, _o, _n)	\
	{			\
		.size = (_s),	\
		.offset = (_o),	\
		.name = (_n),	\
	}

static const struct qca8k_mib_desc ar8327_mib[] = {
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

static void
qca8k_split_addr(u32 regaddr, u16 *r1, u16 *r2, u16 *page)
{
	regaddr >>= 1;
	*r1 = regaddr & 0x1e;

	regaddr >>= 5;
	*r2 = regaddr & 0x7;

	regaddr >>= 3;
	*page = regaddr & 0x3ff;
}

static int
qca8k_set_lo(struct qca8k_priv *priv, int phy_id, u32 regnum, u16 lo)
{
	u16 *cached_lo = &priv->mdio_cache.lo;
	struct mii_bus *bus = priv->bus;
	int ret;

	if (lo == *cached_lo)
		return 0;

	ret = bus->write(bus, phy_id, regnum, lo);
	if (ret < 0)
		dev_err_ratelimited(&bus->dev,
				    "failed to write qca8k 32bit lo register\n");

	*cached_lo = lo;
	return 0;
}

static int
qca8k_set_hi(struct qca8k_priv *priv, int phy_id, u32 regnum, u16 hi)
{
	u16 *cached_hi = &priv->mdio_cache.hi;
	struct mii_bus *bus = priv->bus;
	int ret;

	if (hi == *cached_hi)
		return 0;

	ret = bus->write(bus, phy_id, regnum, hi);
	if (ret < 0)
		dev_err_ratelimited(&bus->dev,
				    "failed to write qca8k 32bit hi register\n");

	*cached_hi = hi;
	return 0;
}

static int
qca8k_mii_read32(struct mii_bus *bus, int phy_id, u32 regnum, u32 *val)
{
	int ret;

	ret = bus->read(bus, phy_id, regnum);
	if (ret >= 0) {
		*val = ret;
		ret = bus->read(bus, phy_id, regnum + 1);
		*val |= ret << 16;
	}

	if (ret < 0) {
		dev_err_ratelimited(&bus->dev,
				    "failed to read qca8k 32bit register\n");
		*val = 0;
		return ret;
	}

	return 0;
}

static void
qca8k_mii_write32(struct qca8k_priv *priv, int phy_id, u32 regnum, u32 val)
{
	u16 lo, hi;
	int ret;

	lo = val & 0xffff;
	hi = (u16)(val >> 16);

	ret = qca8k_set_lo(priv, phy_id, regnum, lo);
	if (ret >= 0)
		ret = qca8k_set_hi(priv, phy_id, regnum + 1, hi);
}

static int
qca8k_set_page(struct qca8k_priv *priv, u16 page)
{
	u16 *cached_page = &priv->mdio_cache.page;
	struct mii_bus *bus = priv->bus;
	int ret;

	if (page == *cached_page)
		return 0;

	ret = bus->write(bus, 0x18, 0, page);
	if (ret < 0) {
		dev_err_ratelimited(&bus->dev,
				    "failed to set qca8k page\n");
		return ret;
	}

	*cached_page = page;
	usleep_range(1000, 2000);
	return 0;
}

static int
qca8k_read(struct qca8k_priv *priv, u32 reg, u32 *val)
{
	return regmap_read(priv->regmap, reg, val);
}

static int
qca8k_write(struct qca8k_priv *priv, u32 reg, u32 val)
{
	return regmap_write(priv->regmap, reg, val);
}

static int
qca8k_rmw(struct qca8k_priv *priv, u32 reg, u32 mask, u32 write_val)
{
	return regmap_update_bits(priv->regmap, reg, mask, write_val);
}

static void qca8k_rw_reg_ack_handler(struct dsa_switch *ds, struct sk_buff *skb)
{
	struct qca8k_mgmt_eth_data *mgmt_eth_data;
	struct qca8k_priv *priv = ds->priv;
	struct qca_mgmt_ethhdr *mgmt_ethhdr;
	u8 len, cmd;

	mgmt_ethhdr = (struct qca_mgmt_ethhdr *)skb_mac_header(skb);
	mgmt_eth_data = &priv->mgmt_eth_data;

	cmd = FIELD_GET(QCA_HDR_MGMT_CMD, mgmt_ethhdr->command);
	len = FIELD_GET(QCA_HDR_MGMT_LENGTH, mgmt_ethhdr->command);

	/* Make sure the seq match the requested packet */
	if (mgmt_ethhdr->seq == mgmt_eth_data->seq)
		mgmt_eth_data->ack = true;

	if (cmd == MDIO_READ) {
		mgmt_eth_data->data[0] = mgmt_ethhdr->mdio_data;

		/* Get the rest of the 12 byte of data.
		 * The read/write function will extract the requested data.
		 */
		if (len > QCA_HDR_MGMT_DATA1_LEN)
			memcpy(mgmt_eth_data->data + 1, skb->data,
			       QCA_HDR_MGMT_DATA2_LEN);
	}

	complete(&mgmt_eth_data->rw_done);
}

static struct sk_buff *qca8k_alloc_mdio_header(enum mdio_cmd cmd, u32 reg, u32 *val,
					       int priority, unsigned int len)
{
	struct qca_mgmt_ethhdr *mgmt_ethhdr;
	unsigned int real_len;
	struct sk_buff *skb;
	u32 *data2;
	u16 hdr;

	skb = dev_alloc_skb(QCA_HDR_MGMT_PKT_LEN);
	if (!skb)
		return NULL;

	/* Max value for len reg is 15 (0xf) but the switch actually return 16 byte
	 * Actually for some reason the steps are:
	 * 0: nothing
	 * 1-4: first 4 byte
	 * 5-6: first 12 byte
	 * 7-15: all 16 byte
	 */
	if (len == 16)
		real_len = 15;
	else
		real_len = len;

	skb_reset_mac_header(skb);
	skb_set_network_header(skb, skb->len);

	mgmt_ethhdr = skb_push(skb, QCA_HDR_MGMT_HEADER_LEN + QCA_HDR_LEN);

	hdr = FIELD_PREP(QCA_HDR_XMIT_VERSION, QCA_HDR_VERSION);
	hdr |= FIELD_PREP(QCA_HDR_XMIT_PRIORITY, priority);
	hdr |= QCA_HDR_XMIT_FROM_CPU;
	hdr |= FIELD_PREP(QCA_HDR_XMIT_DP_BIT, BIT(0));
	hdr |= FIELD_PREP(QCA_HDR_XMIT_CONTROL, QCA_HDR_XMIT_TYPE_RW_REG);

	mgmt_ethhdr->command = FIELD_PREP(QCA_HDR_MGMT_ADDR, reg);
	mgmt_ethhdr->command |= FIELD_PREP(QCA_HDR_MGMT_LENGTH, real_len);
	mgmt_ethhdr->command |= FIELD_PREP(QCA_HDR_MGMT_CMD, cmd);
	mgmt_ethhdr->command |= FIELD_PREP(QCA_HDR_MGMT_CHECK_CODE,
					   QCA_HDR_MGMT_CHECK_CODE_VAL);

	if (cmd == MDIO_WRITE)
		mgmt_ethhdr->mdio_data = *val;

	mgmt_ethhdr->hdr = htons(hdr);

	data2 = skb_put_zero(skb, QCA_HDR_MGMT_DATA2_LEN + QCA_HDR_MGMT_PADDING_LEN);
	if (cmd == MDIO_WRITE && len > QCA_HDR_MGMT_DATA1_LEN)
		memcpy(data2, val + 1, len - QCA_HDR_MGMT_DATA1_LEN);

	return skb;
}

static void qca8k_mdio_header_fill_seq_num(struct sk_buff *skb, u32 seq_num)
{
	struct qca_mgmt_ethhdr *mgmt_ethhdr;

	mgmt_ethhdr = (struct qca_mgmt_ethhdr *)skb->data;
	mgmt_ethhdr->seq = FIELD_PREP(QCA_HDR_MGMT_SEQ_NUM, seq_num);
}

static int qca8k_read_eth(struct qca8k_priv *priv, u32 reg, u32 *val, int len)
{
	struct qca8k_mgmt_eth_data *mgmt_eth_data = &priv->mgmt_eth_data;
	struct sk_buff *skb;
	bool ack;
	int ret;

	skb = qca8k_alloc_mdio_header(MDIO_READ, reg, NULL,
				      QCA8K_ETHERNET_MDIO_PRIORITY, len);
	if (!skb)
		return -ENOMEM;

	mutex_lock(&mgmt_eth_data->mutex);

	/* Check mgmt_master if is operational */
	if (!priv->mgmt_master) {
		kfree_skb(skb);
		mutex_unlock(&mgmt_eth_data->mutex);
		return -EINVAL;
	}

	skb->dev = priv->mgmt_master;

	reinit_completion(&mgmt_eth_data->rw_done);

	/* Increment seq_num and set it in the mdio pkt */
	mgmt_eth_data->seq++;
	qca8k_mdio_header_fill_seq_num(skb, mgmt_eth_data->seq);
	mgmt_eth_data->ack = false;

	dev_queue_xmit(skb);

	ret = wait_for_completion_timeout(&mgmt_eth_data->rw_done,
					  msecs_to_jiffies(QCA8K_ETHERNET_TIMEOUT));

	*val = mgmt_eth_data->data[0];
	if (len > QCA_HDR_MGMT_DATA1_LEN)
		memcpy(val + 1, mgmt_eth_data->data + 1, len - QCA_HDR_MGMT_DATA1_LEN);

	ack = mgmt_eth_data->ack;

	mutex_unlock(&mgmt_eth_data->mutex);

	if (ret <= 0)
		return -ETIMEDOUT;

	if (!ack)
		return -EINVAL;

	return 0;
}

static int qca8k_write_eth(struct qca8k_priv *priv, u32 reg, u32 *val, int len)
{
	struct qca8k_mgmt_eth_data *mgmt_eth_data = &priv->mgmt_eth_data;
	struct sk_buff *skb;
	bool ack;
	int ret;

	skb = qca8k_alloc_mdio_header(MDIO_WRITE, reg, val,
				      QCA8K_ETHERNET_MDIO_PRIORITY, len);
	if (!skb)
		return -ENOMEM;

	mutex_lock(&mgmt_eth_data->mutex);

	/* Check mgmt_master if is operational */
	if (!priv->mgmt_master) {
		kfree_skb(skb);
		mutex_unlock(&mgmt_eth_data->mutex);
		return -EINVAL;
	}

	skb->dev = priv->mgmt_master;

	reinit_completion(&mgmt_eth_data->rw_done);

	/* Increment seq_num and set it in the mdio pkt */
	mgmt_eth_data->seq++;
	qca8k_mdio_header_fill_seq_num(skb, mgmt_eth_data->seq);
	mgmt_eth_data->ack = false;

	dev_queue_xmit(skb);

	ret = wait_for_completion_timeout(&mgmt_eth_data->rw_done,
					  msecs_to_jiffies(QCA8K_ETHERNET_TIMEOUT));

	ack = mgmt_eth_data->ack;

	mutex_unlock(&mgmt_eth_data->mutex);

	if (ret <= 0)
		return -ETIMEDOUT;

	if (!ack)
		return -EINVAL;

	return 0;
}

static int
qca8k_regmap_update_bits_eth(struct qca8k_priv *priv, u32 reg, u32 mask, u32 write_val)
{
	u32 val = 0;
	int ret;

	ret = qca8k_read_eth(priv, reg, &val, sizeof(val));
	if (ret)
		return ret;

	val &= ~mask;
	val |= write_val;

	return qca8k_write_eth(priv, reg, &val, sizeof(val));
}

static int
qca8k_bulk_read(struct qca8k_priv *priv, u32 reg, u32 *val, int len)
{
	int i, count = len / sizeof(u32), ret;

	if (priv->mgmt_master && !qca8k_read_eth(priv, reg, val, len))
		return 0;

	for (i = 0; i < count; i++) {
		ret = regmap_read(priv->regmap, reg + (i * 4), val + i);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static int
qca8k_bulk_write(struct qca8k_priv *priv, u32 reg, u32 *val, int len)
{
	int i, count = len / sizeof(u32), ret;
	u32 tmp;

	if (priv->mgmt_master && !qca8k_write_eth(priv, reg, val, len))
		return 0;

	for (i = 0; i < count; i++) {
		tmp = val[i];

		ret = regmap_write(priv->regmap, reg + (i * 4), tmp);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static int
qca8k_regmap_read(void *ctx, uint32_t reg, uint32_t *val)
{
	struct qca8k_priv *priv = (struct qca8k_priv *)ctx;
	struct mii_bus *bus = priv->bus;
	u16 r1, r2, page;
	int ret;

	if (!qca8k_read_eth(priv, reg, val, sizeof(*val)))
		return 0;

	qca8k_split_addr(reg, &r1, &r2, &page);

	mutex_lock_nested(&bus->mdio_lock, MDIO_MUTEX_NESTED);

	ret = qca8k_set_page(priv, page);
	if (ret < 0)
		goto exit;

	ret = qca8k_mii_read32(bus, 0x10 | r2, r1, val);

exit:
	mutex_unlock(&bus->mdio_lock);
	return ret;
}

static int
qca8k_regmap_write(void *ctx, uint32_t reg, uint32_t val)
{
	struct qca8k_priv *priv = (struct qca8k_priv *)ctx;
	struct mii_bus *bus = priv->bus;
	u16 r1, r2, page;
	int ret;

	if (!qca8k_write_eth(priv, reg, &val, sizeof(val)))
		return 0;

	qca8k_split_addr(reg, &r1, &r2, &page);

	mutex_lock_nested(&bus->mdio_lock, MDIO_MUTEX_NESTED);

	ret = qca8k_set_page(priv, page);
	if (ret < 0)
		goto exit;

	qca8k_mii_write32(priv, 0x10 | r2, r1, val);

exit:
	mutex_unlock(&bus->mdio_lock);
	return ret;
}

static int
qca8k_regmap_update_bits(void *ctx, uint32_t reg, uint32_t mask, uint32_t write_val)
{
	struct qca8k_priv *priv = (struct qca8k_priv *)ctx;
	struct mii_bus *bus = priv->bus;
	u16 r1, r2, page;
	u32 val;
	int ret;

	if (!qca8k_regmap_update_bits_eth(priv, reg, mask, write_val))
		return 0;

	qca8k_split_addr(reg, &r1, &r2, &page);

	mutex_lock_nested(&bus->mdio_lock, MDIO_MUTEX_NESTED);

	ret = qca8k_set_page(priv, page);
	if (ret < 0)
		goto exit;

	ret = qca8k_mii_read32(bus, 0x10 | r2, r1, &val);
	if (ret < 0)
		goto exit;

	val &= ~mask;
	val |= write_val;
	qca8k_mii_write32(priv, 0x10 | r2, r1, val);

exit:
	mutex_unlock(&bus->mdio_lock);

	return ret;
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

static const struct regmap_access_table qca8k_readable_table = {
	.yes_ranges = qca8k_readable_ranges,
	.n_yes_ranges = ARRAY_SIZE(qca8k_readable_ranges),
};

static struct regmap_config qca8k_regmap_config = {
	.reg_bits = 16,
	.val_bits = 32,
	.reg_stride = 4,
	.max_register = 0x16ac, /* end MIB - Port6 range */
	.reg_read = qca8k_regmap_read,
	.reg_write = qca8k_regmap_write,
	.reg_update_bits = qca8k_regmap_update_bits,
	.rd_table = &qca8k_readable_table,
	.disable_locking = true, /* Locking is handled by qca8k read/write */
	.cache_type = REGCACHE_NONE, /* Explicitly disable CACHE */
};

static int
qca8k_busy_wait(struct qca8k_priv *priv, u32 reg, u32 mask)
{
	u32 val;

	return regmap_read_poll_timeout(priv->regmap, reg, val, !(val & mask), 0,
				       QCA8K_BUSY_WAIT_TIMEOUT * USEC_PER_MSEC);
}

static int
qca8k_fdb_read(struct qca8k_priv *priv, struct qca8k_fdb *fdb)
{
	u32 reg[3];
	int ret;

	/* load the ARL table into an array */
	ret = qca8k_bulk_read(priv, QCA8K_REG_ATU_DATA0, reg, sizeof(reg));
	if (ret)
		return ret;

	/* vid - 83:72 */
	fdb->vid = FIELD_GET(QCA8K_ATU_VID_MASK, reg[2]);
	/* aging - 67:64 */
	fdb->aging = FIELD_GET(QCA8K_ATU_STATUS_MASK, reg[2]);
	/* portmask - 54:48 */
	fdb->port_mask = FIELD_GET(QCA8K_ATU_PORT_MASK, reg[1]);
	/* mac - 47:0 */
	fdb->mac[0] = FIELD_GET(QCA8K_ATU_ADDR0_MASK, reg[1]);
	fdb->mac[1] = FIELD_GET(QCA8K_ATU_ADDR1_MASK, reg[1]);
	fdb->mac[2] = FIELD_GET(QCA8K_ATU_ADDR2_MASK, reg[0]);
	fdb->mac[3] = FIELD_GET(QCA8K_ATU_ADDR3_MASK, reg[0]);
	fdb->mac[4] = FIELD_GET(QCA8K_ATU_ADDR4_MASK, reg[0]);
	fdb->mac[5] = FIELD_GET(QCA8K_ATU_ADDR5_MASK, reg[0]);

	return 0;
}

static void
qca8k_fdb_write(struct qca8k_priv *priv, u16 vid, u8 port_mask, const u8 *mac,
		u8 aging)
{
	u32 reg[3] = { 0 };

	/* vid - 83:72 */
	reg[2] = FIELD_PREP(QCA8K_ATU_VID_MASK, vid);
	/* aging - 67:64 */
	reg[2] |= FIELD_PREP(QCA8K_ATU_STATUS_MASK, aging);
	/* portmask - 54:48 */
	reg[1] = FIELD_PREP(QCA8K_ATU_PORT_MASK, port_mask);
	/* mac - 47:0 */
	reg[1] |= FIELD_PREP(QCA8K_ATU_ADDR0_MASK, mac[0]);
	reg[1] |= FIELD_PREP(QCA8K_ATU_ADDR1_MASK, mac[1]);
	reg[0] |= FIELD_PREP(QCA8K_ATU_ADDR2_MASK, mac[2]);
	reg[0] |= FIELD_PREP(QCA8K_ATU_ADDR3_MASK, mac[3]);
	reg[0] |= FIELD_PREP(QCA8K_ATU_ADDR4_MASK, mac[4]);
	reg[0] |= FIELD_PREP(QCA8K_ATU_ADDR5_MASK, mac[5]);

	/* load the array into the ARL table */
	qca8k_bulk_write(priv, QCA8K_REG_ATU_DATA0, reg, sizeof(reg));
}

static int
qca8k_fdb_access(struct qca8k_priv *priv, enum qca8k_fdb_cmd cmd, int port)
{
	u32 reg;
	int ret;

	/* Set the command and FDB index */
	reg = QCA8K_ATU_FUNC_BUSY;
	reg |= cmd;
	if (port >= 0) {
		reg |= QCA8K_ATU_FUNC_PORT_EN;
		reg |= FIELD_PREP(QCA8K_ATU_FUNC_PORT_MASK, port);
	}

	/* Write the function register triggering the table access */
	ret = qca8k_write(priv, QCA8K_REG_ATU_FUNC, reg);
	if (ret)
		return ret;

	/* wait for completion */
	ret = qca8k_busy_wait(priv, QCA8K_REG_ATU_FUNC, QCA8K_ATU_FUNC_BUSY);
	if (ret)
		return ret;

	/* Check for table full violation when adding an entry */
	if (cmd == QCA8K_FDB_LOAD) {
		ret = qca8k_read(priv, QCA8K_REG_ATU_FUNC, &reg);
		if (ret < 0)
			return ret;
		if (reg & QCA8K_ATU_FUNC_FULL)
			return -1;
	}

	return 0;
}

static int
qca8k_fdb_next(struct qca8k_priv *priv, struct qca8k_fdb *fdb, int port)
{
	int ret;

	qca8k_fdb_write(priv, fdb->vid, fdb->port_mask, fdb->mac, fdb->aging);
	ret = qca8k_fdb_access(priv, QCA8K_FDB_NEXT, port);
	if (ret < 0)
		return ret;

	return qca8k_fdb_read(priv, fdb);
}

static int
qca8k_fdb_add(struct qca8k_priv *priv, const u8 *mac, u16 port_mask,
	      u16 vid, u8 aging)
{
	int ret;

	mutex_lock(&priv->reg_mutex);
	qca8k_fdb_write(priv, vid, port_mask, mac, aging);
	ret = qca8k_fdb_access(priv, QCA8K_FDB_LOAD, -1);
	mutex_unlock(&priv->reg_mutex);

	return ret;
}

static int
qca8k_fdb_del(struct qca8k_priv *priv, const u8 *mac, u16 port_mask, u16 vid)
{
	int ret;

	mutex_lock(&priv->reg_mutex);
	qca8k_fdb_write(priv, vid, port_mask, mac, 0);
	ret = qca8k_fdb_access(priv, QCA8K_FDB_PURGE, -1);
	mutex_unlock(&priv->reg_mutex);

	return ret;
}

static void
qca8k_fdb_flush(struct qca8k_priv *priv)
{
	mutex_lock(&priv->reg_mutex);
	qca8k_fdb_access(priv, QCA8K_FDB_FLUSH, -1);
	mutex_unlock(&priv->reg_mutex);
}

static int
qca8k_fdb_search_and_insert(struct qca8k_priv *priv, u8 port_mask,
			    const u8 *mac, u16 vid)
{
	struct qca8k_fdb fdb = { 0 };
	int ret;

	mutex_lock(&priv->reg_mutex);

	qca8k_fdb_write(priv, vid, 0, mac, 0);
	ret = qca8k_fdb_access(priv, QCA8K_FDB_SEARCH, -1);
	if (ret < 0)
		goto exit;

	ret = qca8k_fdb_read(priv, &fdb);
	if (ret < 0)
		goto exit;

	/* Rule exist. Delete first */
	if (!fdb.aging) {
		ret = qca8k_fdb_access(priv, QCA8K_FDB_PURGE, -1);
		if (ret)
			goto exit;
	}

	/* Add port to fdb portmask */
	fdb.port_mask |= port_mask;

	qca8k_fdb_write(priv, vid, fdb.port_mask, mac, fdb.aging);
	ret = qca8k_fdb_access(priv, QCA8K_FDB_LOAD, -1);

exit:
	mutex_unlock(&priv->reg_mutex);
	return ret;
}

static int
qca8k_fdb_search_and_del(struct qca8k_priv *priv, u8 port_mask,
			 const u8 *mac, u16 vid)
{
	struct qca8k_fdb fdb = { 0 };
	int ret;

	mutex_lock(&priv->reg_mutex);

	qca8k_fdb_write(priv, vid, 0, mac, 0);
	ret = qca8k_fdb_access(priv, QCA8K_FDB_SEARCH, -1);
	if (ret < 0)
		goto exit;

	/* Rule doesn't exist. Why delete? */
	if (!fdb.aging) {
		ret = -EINVAL;
		goto exit;
	}

	ret = qca8k_fdb_access(priv, QCA8K_FDB_PURGE, -1);
	if (ret)
		goto exit;

	/* Only port in the rule is this port. Don't re insert */
	if (fdb.port_mask == port_mask)
		goto exit;

	/* Remove port from port mask */
	fdb.port_mask &= ~port_mask;

	qca8k_fdb_write(priv, vid, fdb.port_mask, mac, fdb.aging);
	ret = qca8k_fdb_access(priv, QCA8K_FDB_LOAD, -1);

exit:
	mutex_unlock(&priv->reg_mutex);
	return ret;
}

static int
qca8k_vlan_access(struct qca8k_priv *priv, enum qca8k_vlan_cmd cmd, u16 vid)
{
	u32 reg;
	int ret;

	/* Set the command and VLAN index */
	reg = QCA8K_VTU_FUNC1_BUSY;
	reg |= cmd;
	reg |= FIELD_PREP(QCA8K_VTU_FUNC1_VID_MASK, vid);

	/* Write the function register triggering the table access */
	ret = qca8k_write(priv, QCA8K_REG_VTU_FUNC1, reg);
	if (ret)
		return ret;

	/* wait for completion */
	ret = qca8k_busy_wait(priv, QCA8K_REG_VTU_FUNC1, QCA8K_VTU_FUNC1_BUSY);
	if (ret)
		return ret;

	/* Check for table full violation when adding an entry */
	if (cmd == QCA8K_VLAN_LOAD) {
		ret = qca8k_read(priv, QCA8K_REG_VTU_FUNC1, &reg);
		if (ret < 0)
			return ret;
		if (reg & QCA8K_VTU_FUNC1_FULL)
			return -ENOMEM;
	}

	return 0;
}

static int
qca8k_vlan_add(struct qca8k_priv *priv, u8 port, u16 vid, bool untagged)
{
	u32 reg;
	int ret;

	/*
	   We do the right thing with VLAN 0 and treat it as untagged while
	   preserving the tag on egress.
	 */
	if (vid == 0)
		return 0;

	mutex_lock(&priv->reg_mutex);
	ret = qca8k_vlan_access(priv, QCA8K_VLAN_READ, vid);
	if (ret < 0)
		goto out;

	ret = qca8k_read(priv, QCA8K_REG_VTU_FUNC0, &reg);
	if (ret < 0)
		goto out;
	reg |= QCA8K_VTU_FUNC0_VALID | QCA8K_VTU_FUNC0_IVL_EN;
	reg &= ~QCA8K_VTU_FUNC0_EG_MODE_PORT_MASK(port);
	if (untagged)
		reg |= QCA8K_VTU_FUNC0_EG_MODE_PORT_UNTAG(port);
	else
		reg |= QCA8K_VTU_FUNC0_EG_MODE_PORT_TAG(port);

	ret = qca8k_write(priv, QCA8K_REG_VTU_FUNC0, reg);
	if (ret)
		goto out;
	ret = qca8k_vlan_access(priv, QCA8K_VLAN_LOAD, vid);

out:
	mutex_unlock(&priv->reg_mutex);

	return ret;
}

static int
qca8k_vlan_del(struct qca8k_priv *priv, u8 port, u16 vid)
{
	u32 reg, mask;
	int ret, i;
	bool del;

	mutex_lock(&priv->reg_mutex);
	ret = qca8k_vlan_access(priv, QCA8K_VLAN_READ, vid);
	if (ret < 0)
		goto out;

	ret = qca8k_read(priv, QCA8K_REG_VTU_FUNC0, &reg);
	if (ret < 0)
		goto out;
	reg &= ~QCA8K_VTU_FUNC0_EG_MODE_PORT_MASK(port);
	reg |= QCA8K_VTU_FUNC0_EG_MODE_PORT_NOT(port);

	/* Check if we're the last member to be removed */
	del = true;
	for (i = 0; i < QCA8K_NUM_PORTS; i++) {
		mask = QCA8K_VTU_FUNC0_EG_MODE_PORT_NOT(i);

		if ((reg & mask) != mask) {
			del = false;
			break;
		}
	}

	if (del) {
		ret = qca8k_vlan_access(priv, QCA8K_VLAN_PURGE, vid);
	} else {
		ret = qca8k_write(priv, QCA8K_REG_VTU_FUNC0, reg);
		if (ret)
			goto out;
		ret = qca8k_vlan_access(priv, QCA8K_VLAN_LOAD, vid);
	}

out:
	mutex_unlock(&priv->reg_mutex);

	return ret;
}

static int
qca8k_mib_init(struct qca8k_priv *priv)
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

static void
qca8k_port_set_status(struct qca8k_priv *priv, int port, int enable)
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

static int
qca8k_phy_eth_busy_wait(struct qca8k_mgmt_eth_data *mgmt_eth_data,
			struct sk_buff *read_skb, u32 *val)
{
	struct sk_buff *skb = skb_copy(read_skb, GFP_KERNEL);
	bool ack;
	int ret;

	reinit_completion(&mgmt_eth_data->rw_done);

	/* Increment seq_num and set it in the copy pkt */
	mgmt_eth_data->seq++;
	qca8k_mdio_header_fill_seq_num(skb, mgmt_eth_data->seq);
	mgmt_eth_data->ack = false;

	dev_queue_xmit(skb);

	ret = wait_for_completion_timeout(&mgmt_eth_data->rw_done,
					  QCA8K_ETHERNET_TIMEOUT);

	ack = mgmt_eth_data->ack;

	if (ret <= 0)
		return -ETIMEDOUT;

	if (!ack)
		return -EINVAL;

	*val = mgmt_eth_data->data[0];

	return 0;
}

static int
qca8k_phy_eth_command(struct qca8k_priv *priv, bool read, int phy,
		      int regnum, u16 data)
{
	struct sk_buff *write_skb, *clear_skb, *read_skb;
	struct qca8k_mgmt_eth_data *mgmt_eth_data;
	u32 write_val, clear_val = 0, val;
	struct net_device *mgmt_master;
	int ret, ret1;
	bool ack;

	if (regnum >= QCA8K_MDIO_MASTER_MAX_REG)
		return -EINVAL;

	mgmt_eth_data = &priv->mgmt_eth_data;

	write_val = QCA8K_MDIO_MASTER_BUSY | QCA8K_MDIO_MASTER_EN |
		    QCA8K_MDIO_MASTER_PHY_ADDR(phy) |
		    QCA8K_MDIO_MASTER_REG_ADDR(regnum);

	if (read) {
		write_val |= QCA8K_MDIO_MASTER_READ;
	} else {
		write_val |= QCA8K_MDIO_MASTER_WRITE;
		write_val |= QCA8K_MDIO_MASTER_DATA(data);
	}

	/* Prealloc all the needed skb before the lock */
	write_skb = qca8k_alloc_mdio_header(MDIO_WRITE, QCA8K_MDIO_MASTER_CTRL, &write_val,
					    QCA8K_ETHERNET_PHY_PRIORITY, sizeof(write_val));
	if (!write_skb)
		return -ENOMEM;

	clear_skb = qca8k_alloc_mdio_header(MDIO_WRITE, QCA8K_MDIO_MASTER_CTRL, &clear_val,
					    QCA8K_ETHERNET_PHY_PRIORITY, sizeof(clear_val));
	if (!clear_skb) {
		ret = -ENOMEM;
		goto err_clear_skb;
	}

	read_skb = qca8k_alloc_mdio_header(MDIO_READ, QCA8K_MDIO_MASTER_CTRL, &clear_val,
					   QCA8K_ETHERNET_PHY_PRIORITY, sizeof(clear_val));
	if (!read_skb) {
		ret = -ENOMEM;
		goto err_read_skb;
	}

	/* Actually start the request:
	 * 1. Send mdio master packet
	 * 2. Busy Wait for mdio master command
	 * 3. Get the data if we are reading
	 * 4. Reset the mdio master (even with error)
	 */
	mutex_lock(&mgmt_eth_data->mutex);

	/* Check if mgmt_master is operational */
	mgmt_master = priv->mgmt_master;
	if (!mgmt_master) {
		mutex_unlock(&mgmt_eth_data->mutex);
		ret = -EINVAL;
		goto err_mgmt_master;
	}

	read_skb->dev = mgmt_master;
	clear_skb->dev = mgmt_master;
	write_skb->dev = mgmt_master;

	reinit_completion(&mgmt_eth_data->rw_done);

	/* Increment seq_num and set it in the write pkt */
	mgmt_eth_data->seq++;
	qca8k_mdio_header_fill_seq_num(write_skb, mgmt_eth_data->seq);
	mgmt_eth_data->ack = false;

	dev_queue_xmit(write_skb);

	ret = wait_for_completion_timeout(&mgmt_eth_data->rw_done,
					  QCA8K_ETHERNET_TIMEOUT);

	ack = mgmt_eth_data->ack;

	if (ret <= 0) {
		ret = -ETIMEDOUT;
		kfree_skb(read_skb);
		goto exit;
	}

	if (!ack) {
		ret = -EINVAL;
		kfree_skb(read_skb);
		goto exit;
	}

	ret = read_poll_timeout(qca8k_phy_eth_busy_wait, ret1,
				!(val & QCA8K_MDIO_MASTER_BUSY), 0,
				QCA8K_BUSY_WAIT_TIMEOUT * USEC_PER_MSEC, false,
				mgmt_eth_data, read_skb, &val);

	if (ret < 0 && ret1 < 0) {
		ret = ret1;
		goto exit;
	}

	if (read) {
		reinit_completion(&mgmt_eth_data->rw_done);

		/* Increment seq_num and set it in the read pkt */
		mgmt_eth_data->seq++;
		qca8k_mdio_header_fill_seq_num(read_skb, mgmt_eth_data->seq);
		mgmt_eth_data->ack = false;

		dev_queue_xmit(read_skb);

		ret = wait_for_completion_timeout(&mgmt_eth_data->rw_done,
						  QCA8K_ETHERNET_TIMEOUT);

		ack = mgmt_eth_data->ack;

		if (ret <= 0) {
			ret = -ETIMEDOUT;
			goto exit;
		}

		if (!ack) {
			ret = -EINVAL;
			goto exit;
		}

		ret = mgmt_eth_data->data[0] & QCA8K_MDIO_MASTER_DATA_MASK;
	} else {
		kfree_skb(read_skb);
	}
exit:
	reinit_completion(&mgmt_eth_data->rw_done);

	/* Increment seq_num and set it in the clear pkt */
	mgmt_eth_data->seq++;
	qca8k_mdio_header_fill_seq_num(clear_skb, mgmt_eth_data->seq);
	mgmt_eth_data->ack = false;

	dev_queue_xmit(clear_skb);

	wait_for_completion_timeout(&mgmt_eth_data->rw_done,
				    QCA8K_ETHERNET_TIMEOUT);

	mutex_unlock(&mgmt_eth_data->mutex);

	return ret;

	/* Error handling before lock */
err_mgmt_master:
	kfree_skb(read_skb);
err_read_skb:
	kfree_skb(clear_skb);
err_clear_skb:
	kfree_skb(write_skb);

	return ret;
}

static u32
qca8k_port_to_phy(int port)
{
	/* From Andrew Lunn:
	 * Port 0 has no internal phy.
	 * Port 1 has an internal PHY at MDIO address 0.
	 * Port 2 has an internal PHY at MDIO address 1.
	 * ...
	 * Port 5 has an internal PHY at MDIO address 4.
	 * Port 6 has no internal PHY.
	 */

	return port - 1;
}

static int
qca8k_mdio_busy_wait(struct mii_bus *bus, u32 reg, u32 mask)
{
	u16 r1, r2, page;
	u32 val;
	int ret, ret1;

	qca8k_split_addr(reg, &r1, &r2, &page);

	ret = read_poll_timeout(qca8k_mii_read32, ret1, !(val & mask), 0,
				QCA8K_BUSY_WAIT_TIMEOUT * USEC_PER_MSEC, false,
				bus, 0x10 | r2, r1, &val);

	/* Check if qca8k_read has failed for a different reason
	 * before returnting -ETIMEDOUT
	 */
	if (ret < 0 && ret1 < 0)
		return ret1;

	return ret;
}

static int
qca8k_mdio_write(struct qca8k_priv *priv, int phy, int regnum, u16 data)
{
	struct mii_bus *bus = priv->bus;
	u16 r1, r2, page;
	u32 val;
	int ret;

	if (regnum >= QCA8K_MDIO_MASTER_MAX_REG)
		return -EINVAL;

	val = QCA8K_MDIO_MASTER_BUSY | QCA8K_MDIO_MASTER_EN |
	      QCA8K_MDIO_MASTER_WRITE | QCA8K_MDIO_MASTER_PHY_ADDR(phy) |
	      QCA8K_MDIO_MASTER_REG_ADDR(regnum) |
	      QCA8K_MDIO_MASTER_DATA(data);

	qca8k_split_addr(QCA8K_MDIO_MASTER_CTRL, &r1, &r2, &page);

	mutex_lock_nested(&bus->mdio_lock, MDIO_MUTEX_NESTED);

	ret = qca8k_set_page(priv, page);
	if (ret)
		goto exit;

	qca8k_mii_write32(priv, 0x10 | r2, r1, val);

	ret = qca8k_mdio_busy_wait(bus, QCA8K_MDIO_MASTER_CTRL,
				   QCA8K_MDIO_MASTER_BUSY);

exit:
	/* even if the busy_wait timeouts try to clear the MASTER_EN */
	qca8k_mii_write32(priv, 0x10 | r2, r1, 0);

	mutex_unlock(&bus->mdio_lock);

	return ret;
}

static int
qca8k_mdio_read(struct qca8k_priv *priv, int phy, int regnum)
{
	struct mii_bus *bus = priv->bus;
	u16 r1, r2, page;
	u32 val;
	int ret;

	if (regnum >= QCA8K_MDIO_MASTER_MAX_REG)
		return -EINVAL;

	val = QCA8K_MDIO_MASTER_BUSY | QCA8K_MDIO_MASTER_EN |
	      QCA8K_MDIO_MASTER_READ | QCA8K_MDIO_MASTER_PHY_ADDR(phy) |
	      QCA8K_MDIO_MASTER_REG_ADDR(regnum);

	qca8k_split_addr(QCA8K_MDIO_MASTER_CTRL, &r1, &r2, &page);

	mutex_lock_nested(&bus->mdio_lock, MDIO_MUTEX_NESTED);

	ret = qca8k_set_page(priv, page);
	if (ret)
		goto exit;

	qca8k_mii_write32(priv, 0x10 | r2, r1, val);

	ret = qca8k_mdio_busy_wait(bus, QCA8K_MDIO_MASTER_CTRL,
				   QCA8K_MDIO_MASTER_BUSY);
	if (ret)
		goto exit;

	ret = qca8k_mii_read32(bus, 0x10 | r2, r1, &val);

exit:
	/* even if the busy_wait timeouts try to clear the MASTER_EN */
	qca8k_mii_write32(priv, 0x10 | r2, r1, 0);

	mutex_unlock(&bus->mdio_lock);

	if (ret >= 0)
		ret = val & QCA8K_MDIO_MASTER_DATA_MASK;

	return ret;
}

static int
qca8k_internal_mdio_write(struct mii_bus *slave_bus, int phy, int regnum, u16 data)
{
	struct qca8k_priv *priv = slave_bus->priv;
	int ret;

	/* Use mdio Ethernet when available, fallback to legacy one on error */
	ret = qca8k_phy_eth_command(priv, false, phy, regnum, data);
	if (!ret)
		return 0;

	return qca8k_mdio_write(priv, phy, regnum, data);
}

static int
qca8k_internal_mdio_read(struct mii_bus *slave_bus, int phy, int regnum)
{
	struct qca8k_priv *priv = slave_bus->priv;
	int ret;

	/* Use mdio Ethernet when available, fallback to legacy one on error */
	ret = qca8k_phy_eth_command(priv, true, phy, regnum, 0);
	if (ret >= 0)
		return ret;

	ret = qca8k_mdio_read(priv, phy, regnum);

	if (ret < 0)
		return 0xffff;

	return ret;
}

static int
qca8k_legacy_mdio_write(struct mii_bus *slave_bus, int port, int regnum, u16 data)
{
	port = qca8k_port_to_phy(port) % PHY_MAX_ADDR;

	return qca8k_internal_mdio_write(slave_bus, port, regnum, data);
}

static int
qca8k_legacy_mdio_read(struct mii_bus *slave_bus, int port, int regnum)
{
	port = qca8k_port_to_phy(port) % PHY_MAX_ADDR;

	return qca8k_internal_mdio_read(slave_bus, port, regnum);
}

static int
qca8k_mdio_register(struct qca8k_priv *priv)
{
	struct dsa_switch *ds = priv->ds;
	struct device_node *mdio;
	struct mii_bus *bus;

	bus = devm_mdiobus_alloc(ds->dev);
	if (!bus)
		return -ENOMEM;

	bus->priv = (void *)priv;
	snprintf(bus->id, MII_BUS_ID_SIZE, "qca8k-%d.%d",
		 ds->dst->index, ds->index);
	bus->parent = ds->dev;
	bus->phy_mask = ~ds->phys_mii_mask;
	ds->slave_mii_bus = bus;

	/* Check if the devicetree declare the port:phy mapping */
	mdio = of_get_child_by_name(priv->dev->of_node, "mdio");
	if (of_device_is_available(mdio)) {
		bus->name = "qca8k slave mii";
		bus->read = qca8k_internal_mdio_read;
		bus->write = qca8k_internal_mdio_write;
		return devm_of_mdiobus_register(priv->dev, bus, mdio);
	}

	/* If a mapping can't be found the legacy mapping is used,
	 * using the qca8k_port_to_phy function
	 */
	bus->name = "qca8k-legacy slave mii";
	bus->read = qca8k_legacy_mdio_read;
	bus->write = qca8k_legacy_mdio_write;
	return devm_mdiobus_register(priv->dev, bus);
}

static int
qca8k_setup_mdio_bus(struct qca8k_priv *priv)
{
	u32 internal_mdio_mask = 0, external_mdio_mask = 0, reg;
	struct device_node *ports, *port;
	phy_interface_t mode;
	int err;

	ports = of_get_child_by_name(priv->dev->of_node, "ports");
	if (!ports)
		ports = of_get_child_by_name(priv->dev->of_node, "ethernet-ports");

	if (!ports)
		return -EINVAL;

	for_each_available_child_of_node(ports, port) {
		err = of_property_read_u32(port, "reg", &reg);
		if (err) {
			of_node_put(port);
			of_node_put(ports);
			return err;
		}

		if (!dsa_is_user_port(priv->ds, reg))
			continue;

		of_get_phy_mode(port, &mode);

		if (of_property_read_bool(port, "phy-handle") &&
		    mode != PHY_INTERFACE_MODE_INTERNAL)
			external_mdio_mask |= BIT(reg);
		else
			internal_mdio_mask |= BIT(reg);
	}

	of_node_put(ports);
	if (!external_mdio_mask && !internal_mdio_mask) {
		dev_err(priv->dev, "no PHYs are defined.\n");
		return -EINVAL;
	}

	/* The QCA8K_MDIO_MASTER_EN Bit, which grants access to PHYs through
	 * the MDIO_MASTER register also _disconnects_ the external MDC
	 * passthrough to the internal PHYs. It's not possible to use both
	 * configurations at the same time!
	 *
	 * Because this came up during the review process:
	 * If the external mdio-bus driver is capable magically disabling
	 * the QCA8K_MDIO_MASTER_EN and mutex/spin-locking out the qca8k's
	 * accessors for the time being, it would be possible to pull this
	 * off.
	 */
	if (!!external_mdio_mask && !!internal_mdio_mask) {
		dev_err(priv->dev, "either internal or external mdio bus configuration is supported.\n");
		return -EINVAL;
	}

	if (external_mdio_mask) {
		/* Make sure to disable the internal mdio bus in cases
		 * a dt-overlay and driver reload changed the configuration
		 */

		return regmap_clear_bits(priv->regmap, QCA8K_MDIO_MASTER_CTRL,
					 QCA8K_MDIO_MASTER_EN);
	}

	return qca8k_mdio_register(priv);
}

static int
qca8k_setup_mac_pwr_sel(struct qca8k_priv *priv)
{
	u32 mask = 0;
	int ret = 0;

	/* SoC specific settings for ipq8064.
	 * If more device require this consider adding
	 * a dedicated binding.
	 */
	if (of_machine_is_compatible("qcom,ipq8064"))
		mask |= QCA8K_MAC_PWR_RGMII0_1_8V;

	/* SoC specific settings for ipq8065 */
	if (of_machine_is_compatible("qcom,ipq8065"))
		mask |= QCA8K_MAC_PWR_RGMII1_1_8V;

	if (mask) {
		ret = qca8k_rmw(priv, QCA8K_REG_MAC_PWR_SEL,
				QCA8K_MAC_PWR_RGMII0_1_8V |
				QCA8K_MAC_PWR_RGMII1_1_8V,
				mask);
	}

	return ret;
}

static int qca8k_find_cpu_port(struct dsa_switch *ds)
{
	struct qca8k_priv *priv = ds->priv;

	/* Find the connected cpu port. Valid port are 0 or 6 */
	if (dsa_is_cpu_port(ds, 0))
		return 0;

	dev_dbg(priv->dev, "port 0 is not the CPU port. Checking port 6");

	if (dsa_is_cpu_port(ds, 6))
		return 6;

	return -EINVAL;
}

static int
qca8k_setup_of_pws_reg(struct qca8k_priv *priv)
{
	struct device_node *node = priv->dev->of_node;
	const struct qca8k_match_data *data;
	u32 val = 0;
	int ret;

	/* QCA8327 require to set to the correct mode.
	 * His bigger brother QCA8328 have the 172 pin layout.
	 * Should be applied by default but we set this just to make sure.
	 */
	if (priv->switch_id == QCA8K_ID_QCA8327) {
		data = of_device_get_match_data(priv->dev);

		/* Set the correct package of 148 pin for QCA8327 */
		if (data->reduced_package)
			val |= QCA8327_PWS_PACKAGE148_EN;

		ret = qca8k_rmw(priv, QCA8K_REG_PWS, QCA8327_PWS_PACKAGE148_EN,
				val);
		if (ret)
			return ret;
	}

	if (of_property_read_bool(node, "qca,ignore-power-on-sel"))
		val |= QCA8K_PWS_POWER_ON_SEL;

	if (of_property_read_bool(node, "qca,led-open-drain")) {
		if (!(val & QCA8K_PWS_POWER_ON_SEL)) {
			dev_err(priv->dev, "qca,led-open-drain require qca,ignore-power-on-sel to be set.");
			return -EINVAL;
		}

		val |= QCA8K_PWS_LED_OPEN_EN_CSR;
	}

	return qca8k_rmw(priv, QCA8K_REG_PWS,
			QCA8K_PWS_LED_OPEN_EN_CSR | QCA8K_PWS_POWER_ON_SEL,
			val);
}

static int
qca8k_parse_port_config(struct qca8k_priv *priv)
{
	int port, cpu_port_index = -1, ret;
	struct device_node *port_dn;
	phy_interface_t mode;
	struct dsa_port *dp;
	u32 delay;

	/* We have 2 CPU port. Check them */
	for (port = 0; port < QCA8K_NUM_PORTS; port++) {
		/* Skip every other port */
		if (port != 0 && port != 6)
			continue;

		dp = dsa_to_port(priv->ds, port);
		port_dn = dp->dn;
		cpu_port_index++;

		if (!of_device_is_available(port_dn))
			continue;

		ret = of_get_phy_mode(port_dn, &mode);
		if (ret)
			continue;

		switch (mode) {
		case PHY_INTERFACE_MODE_RGMII:
		case PHY_INTERFACE_MODE_RGMII_ID:
		case PHY_INTERFACE_MODE_RGMII_TXID:
		case PHY_INTERFACE_MODE_RGMII_RXID:
		case PHY_INTERFACE_MODE_SGMII:
			delay = 0;

			if (!of_property_read_u32(port_dn, "tx-internal-delay-ps", &delay))
				/* Switch regs accept value in ns, convert ps to ns */
				delay = delay / 1000;
			else if (mode == PHY_INTERFACE_MODE_RGMII_ID ||
				 mode == PHY_INTERFACE_MODE_RGMII_TXID)
				delay = 1;

			if (!FIELD_FIT(QCA8K_PORT_PAD_RGMII_TX_DELAY_MASK, delay)) {
				dev_err(priv->dev, "rgmii tx delay is limited to a max value of 3ns, setting to the max value");
				delay = 3;
			}

			priv->ports_config.rgmii_tx_delay[cpu_port_index] = delay;

			delay = 0;

			if (!of_property_read_u32(port_dn, "rx-internal-delay-ps", &delay))
				/* Switch regs accept value in ns, convert ps to ns */
				delay = delay / 1000;
			else if (mode == PHY_INTERFACE_MODE_RGMII_ID ||
				 mode == PHY_INTERFACE_MODE_RGMII_RXID)
				delay = 2;

			if (!FIELD_FIT(QCA8K_PORT_PAD_RGMII_RX_DELAY_MASK, delay)) {
				dev_err(priv->dev, "rgmii rx delay is limited to a max value of 3ns, setting to the max value");
				delay = 3;
			}

			priv->ports_config.rgmii_rx_delay[cpu_port_index] = delay;

			/* Skip sgmii parsing for rgmii* mode */
			if (mode == PHY_INTERFACE_MODE_RGMII ||
			    mode == PHY_INTERFACE_MODE_RGMII_ID ||
			    mode == PHY_INTERFACE_MODE_RGMII_TXID ||
			    mode == PHY_INTERFACE_MODE_RGMII_RXID)
				break;

			if (of_property_read_bool(port_dn, "qca,sgmii-txclk-falling-edge"))
				priv->ports_config.sgmii_tx_clk_falling_edge = true;

			if (of_property_read_bool(port_dn, "qca,sgmii-rxclk-falling-edge"))
				priv->ports_config.sgmii_rx_clk_falling_edge = true;

			if (of_property_read_bool(port_dn, "qca,sgmii-enable-pll")) {
				priv->ports_config.sgmii_enable_pll = true;

				if (priv->switch_id == QCA8K_ID_QCA8327) {
					dev_err(priv->dev, "SGMII PLL should NOT be enabled for qca8327. Aborting enabling");
					priv->ports_config.sgmii_enable_pll = false;
				}

				if (priv->switch_revision < 2)
					dev_warn(priv->dev, "SGMII PLL should NOT be enabled for qca8337 with revision 2 or more.");
			}

			break;
		default:
			continue;
		}
	}

	return 0;
}

static void
qca8k_mac_config_setup_internal_delay(struct qca8k_priv *priv, int cpu_port_index,
				      u32 reg)
{
	u32 delay, val = 0;
	int ret;

	/* Delay can be declared in 3 different way.
	 * Mode to rgmii and internal-delay standard binding defined
	 * rgmii-id or rgmii-tx/rx phy mode set.
	 * The parse logic set a delay different than 0 only when one
	 * of the 3 different way is used. In all other case delay is
	 * not enabled. With ID or TX/RXID delay is enabled and set
	 * to the default and recommended value.
	 */
	if (priv->ports_config.rgmii_tx_delay[cpu_port_index]) {
		delay = priv->ports_config.rgmii_tx_delay[cpu_port_index];

		val |= QCA8K_PORT_PAD_RGMII_TX_DELAY(delay) |
			QCA8K_PORT_PAD_RGMII_TX_DELAY_EN;
	}

	if (priv->ports_config.rgmii_rx_delay[cpu_port_index]) {
		delay = priv->ports_config.rgmii_rx_delay[cpu_port_index];

		val |= QCA8K_PORT_PAD_RGMII_RX_DELAY(delay) |
			QCA8K_PORT_PAD_RGMII_RX_DELAY_EN;
	}

	/* Set RGMII delay based on the selected values */
	ret = qca8k_rmw(priv, reg,
			QCA8K_PORT_PAD_RGMII_TX_DELAY_MASK |
			QCA8K_PORT_PAD_RGMII_RX_DELAY_MASK |
			QCA8K_PORT_PAD_RGMII_TX_DELAY_EN |
			QCA8K_PORT_PAD_RGMII_RX_DELAY_EN,
			val);
	if (ret)
		dev_err(priv->dev, "Failed to set internal delay for CPU port%d",
			cpu_port_index == QCA8K_CPU_PORT0 ? 0 : 6);
}

static struct phylink_pcs *
qca8k_phylink_mac_select_pcs(struct dsa_switch *ds, int port,
			     phy_interface_t interface)
{
	struct qca8k_priv *priv = ds->priv;
	struct phylink_pcs *pcs = NULL;

	switch (interface) {
	case PHY_INTERFACE_MODE_SGMII:
	case PHY_INTERFACE_MODE_1000BASEX:
		switch (port) {
		case 0:
			pcs = &priv->pcs_port_0.pcs;
			break;

		case 6:
			pcs = &priv->pcs_port_6.pcs;
			break;
		}
		break;

	default:
		break;
	}

	return pcs;
}

static void
qca8k_phylink_mac_config(struct dsa_switch *ds, int port, unsigned int mode,
			 const struct phylink_link_state *state)
{
	struct qca8k_priv *priv = ds->priv;
	int cpu_port_index;
	u32 reg;

	switch (port) {
	case 0: /* 1st CPU port */
		if (state->interface != PHY_INTERFACE_MODE_RGMII &&
		    state->interface != PHY_INTERFACE_MODE_RGMII_ID &&
		    state->interface != PHY_INTERFACE_MODE_RGMII_TXID &&
		    state->interface != PHY_INTERFACE_MODE_RGMII_RXID &&
		    state->interface != PHY_INTERFACE_MODE_SGMII)
			return;

		reg = QCA8K_REG_PORT0_PAD_CTRL;
		cpu_port_index = QCA8K_CPU_PORT0;
		break;
	case 1:
	case 2:
	case 3:
	case 4:
	case 5:
		/* Internal PHY, nothing to do */
		return;
	case 6: /* 2nd CPU port / external PHY */
		if (state->interface != PHY_INTERFACE_MODE_RGMII &&
		    state->interface != PHY_INTERFACE_MODE_RGMII_ID &&
		    state->interface != PHY_INTERFACE_MODE_RGMII_TXID &&
		    state->interface != PHY_INTERFACE_MODE_RGMII_RXID &&
		    state->interface != PHY_INTERFACE_MODE_SGMII &&
		    state->interface != PHY_INTERFACE_MODE_1000BASEX)
			return;

		reg = QCA8K_REG_PORT6_PAD_CTRL;
		cpu_port_index = QCA8K_CPU_PORT6;
		break;
	default:
		dev_err(ds->dev, "%s: unsupported port: %i\n", __func__, port);
		return;
	}

	if (port != 6 && phylink_autoneg_inband(mode)) {
		dev_err(ds->dev, "%s: in-band negotiation unsupported\n",
			__func__);
		return;
	}

	switch (state->interface) {
	case PHY_INTERFACE_MODE_RGMII:
	case PHY_INTERFACE_MODE_RGMII_ID:
	case PHY_INTERFACE_MODE_RGMII_TXID:
	case PHY_INTERFACE_MODE_RGMII_RXID:
		qca8k_write(priv, reg, QCA8K_PORT_PAD_RGMII_EN);

		/* Configure rgmii delay */
		qca8k_mac_config_setup_internal_delay(priv, cpu_port_index, reg);

		/* QCA8337 requires to set rgmii rx delay for all ports.
		 * This is enabled through PORT5_PAD_CTRL for all ports,
		 * rather than individual port registers.
		 */
		if (priv->switch_id == QCA8K_ID_QCA8337)
			qca8k_write(priv, QCA8K_REG_PORT5_PAD_CTRL,
				    QCA8K_PORT_PAD_RGMII_RX_DELAY_EN);
		break;
	case PHY_INTERFACE_MODE_SGMII:
	case PHY_INTERFACE_MODE_1000BASEX:
		/* Enable SGMII on the port */
		qca8k_write(priv, reg, QCA8K_PORT_PAD_SGMII_EN);
		break;
	default:
		dev_err(ds->dev, "xMII mode %s not supported for port %d\n",
			phy_modes(state->interface), port);
		return;
	}
}

static void qca8k_phylink_get_caps(struct dsa_switch *ds, int port,
				   struct phylink_config *config)
{
	switch (port) {
	case 0: /* 1st CPU port */
		phy_interface_set_rgmii(config->supported_interfaces);
		__set_bit(PHY_INTERFACE_MODE_SGMII,
			  config->supported_interfaces);
		break;

	case 1:
	case 2:
	case 3:
	case 4:
	case 5:
		/* Internal PHY */
		__set_bit(PHY_INTERFACE_MODE_GMII,
			  config->supported_interfaces);
		__set_bit(PHY_INTERFACE_MODE_INTERNAL,
			  config->supported_interfaces);
		break;

	case 6: /* 2nd CPU port / external PHY */
		phy_interface_set_rgmii(config->supported_interfaces);
		__set_bit(PHY_INTERFACE_MODE_SGMII,
			  config->supported_interfaces);
		__set_bit(PHY_INTERFACE_MODE_1000BASEX,
			  config->supported_interfaces);
		break;
	}

	config->mac_capabilities = MAC_ASYM_PAUSE | MAC_SYM_PAUSE |
		MAC_10 | MAC_100 | MAC_1000FD;

	config->legacy_pre_march2020 = false;
}

static void
qca8k_phylink_mac_link_down(struct dsa_switch *ds, int port, unsigned int mode,
			    phy_interface_t interface)
{
	struct qca8k_priv *priv = ds->priv;

	qca8k_port_set_status(priv, port, 0);
}

static void
qca8k_phylink_mac_link_up(struct dsa_switch *ds, int port, unsigned int mode,
			  phy_interface_t interface, struct phy_device *phydev,
			  int speed, int duplex, bool tx_pause, bool rx_pause)
{
	struct qca8k_priv *priv = ds->priv;
	u32 reg;

	if (phylink_autoneg_inband(mode)) {
		reg = QCA8K_PORT_STATUS_LINK_AUTO;
	} else {
		switch (speed) {
		case SPEED_10:
			reg = QCA8K_PORT_STATUS_SPEED_10;
			break;
		case SPEED_100:
			reg = QCA8K_PORT_STATUS_SPEED_100;
			break;
		case SPEED_1000:
			reg = QCA8K_PORT_STATUS_SPEED_1000;
			break;
		default:
			reg = QCA8K_PORT_STATUS_LINK_AUTO;
			break;
		}

		if (duplex == DUPLEX_FULL)
			reg |= QCA8K_PORT_STATUS_DUPLEX;

		if (rx_pause || dsa_is_cpu_port(ds, port))
			reg |= QCA8K_PORT_STATUS_RXFLOW;

		if (tx_pause || dsa_is_cpu_port(ds, port))
			reg |= QCA8K_PORT_STATUS_TXFLOW;
	}

	reg |= QCA8K_PORT_STATUS_TXMAC | QCA8K_PORT_STATUS_RXMAC;

	qca8k_write(priv, QCA8K_REG_PORT_STATUS(port), reg);
}

static struct qca8k_pcs *pcs_to_qca8k_pcs(struct phylink_pcs *pcs)
{
	return container_of(pcs, struct qca8k_pcs, pcs);
}

static void qca8k_pcs_get_state(struct phylink_pcs *pcs,
				struct phylink_link_state *state)
{
	struct qca8k_priv *priv = pcs_to_qca8k_pcs(pcs)->priv;
	int port = pcs_to_qca8k_pcs(pcs)->port;
	u32 reg;
	int ret;

	ret = qca8k_read(priv, QCA8K_REG_PORT_STATUS(port), &reg);
	if (ret < 0) {
		state->link = false;
		return;
	}

	state->link = !!(reg & QCA8K_PORT_STATUS_LINK_UP);
	state->an_complete = state->link;
	state->an_enabled = !!(reg & QCA8K_PORT_STATUS_LINK_AUTO);
	state->duplex = (reg & QCA8K_PORT_STATUS_DUPLEX) ? DUPLEX_FULL :
							   DUPLEX_HALF;

	switch (reg & QCA8K_PORT_STATUS_SPEED) {
	case QCA8K_PORT_STATUS_SPEED_10:
		state->speed = SPEED_10;
		break;
	case QCA8K_PORT_STATUS_SPEED_100:
		state->speed = SPEED_100;
		break;
	case QCA8K_PORT_STATUS_SPEED_1000:
		state->speed = SPEED_1000;
		break;
	default:
		state->speed = SPEED_UNKNOWN;
		break;
	}

	if (reg & QCA8K_PORT_STATUS_RXFLOW)
		state->pause |= MLO_PAUSE_RX;
	if (reg & QCA8K_PORT_STATUS_TXFLOW)
		state->pause |= MLO_PAUSE_TX;
}

static int qca8k_pcs_config(struct phylink_pcs *pcs, unsigned int mode,
			    phy_interface_t interface,
			    const unsigned long *advertising,
			    bool permit_pause_to_mac)
{
	struct qca8k_priv *priv = pcs_to_qca8k_pcs(pcs)->priv;
	int cpu_port_index, ret, port;
	u32 reg, val;

	port = pcs_to_qca8k_pcs(pcs)->port;
	switch (port) {
	case 0:
		reg = QCA8K_REG_PORT0_PAD_CTRL;
		cpu_port_index = QCA8K_CPU_PORT0;
		break;

	case 6:
		reg = QCA8K_REG_PORT6_PAD_CTRL;
		cpu_port_index = QCA8K_CPU_PORT6;
		break;

	default:
		WARN_ON(1);
		return -EINVAL;
	}

	/* Enable/disable SerDes auto-negotiation as necessary */
	ret = qca8k_read(priv, QCA8K_REG_PWS, &val);
	if (ret)
		return ret;
	if (phylink_autoneg_inband(mode))
		val &= ~QCA8K_PWS_SERDES_AEN_DIS;
	else
		val |= QCA8K_PWS_SERDES_AEN_DIS;
	qca8k_write(priv, QCA8K_REG_PWS, val);

	/* Configure the SGMII parameters */
	ret = qca8k_read(priv, QCA8K_REG_SGMII_CTRL, &val);
	if (ret)
		return ret;

	val |= QCA8K_SGMII_EN_SD;

	if (priv->ports_config.sgmii_enable_pll)
		val |= QCA8K_SGMII_EN_PLL | QCA8K_SGMII_EN_RX |
		       QCA8K_SGMII_EN_TX;

	if (dsa_is_cpu_port(priv->ds, port)) {
		/* CPU port, we're talking to the CPU MAC, be a PHY */
		val &= ~QCA8K_SGMII_MODE_CTRL_MASK;
		val |= QCA8K_SGMII_MODE_CTRL_PHY;
	} else if (interface == PHY_INTERFACE_MODE_SGMII) {
		val &= ~QCA8K_SGMII_MODE_CTRL_MASK;
		val |= QCA8K_SGMII_MODE_CTRL_MAC;
	} else if (interface == PHY_INTERFACE_MODE_1000BASEX) {
		val &= ~QCA8K_SGMII_MODE_CTRL_MASK;
		val |= QCA8K_SGMII_MODE_CTRL_BASEX;
	}

	qca8k_write(priv, QCA8K_REG_SGMII_CTRL, val);

	/* From original code is reported port instability as SGMII also
	 * require delay set. Apply advised values here or take them from DT.
	 */
	if (interface == PHY_INTERFACE_MODE_SGMII)
		qca8k_mac_config_setup_internal_delay(priv, cpu_port_index, reg);
	/* For qca8327/qca8328/qca8334/qca8338 sgmii is unique and
	 * falling edge is set writing in the PORT0 PAD reg
	 */
	if (priv->switch_id == QCA8K_ID_QCA8327 ||
	    priv->switch_id == QCA8K_ID_QCA8337)
		reg = QCA8K_REG_PORT0_PAD_CTRL;

	val = 0;

	/* SGMII Clock phase configuration */
	if (priv->ports_config.sgmii_rx_clk_falling_edge)
		val |= QCA8K_PORT0_PAD_SGMII_RXCLK_FALLING_EDGE;

	if (priv->ports_config.sgmii_tx_clk_falling_edge)
		val |= QCA8K_PORT0_PAD_SGMII_TXCLK_FALLING_EDGE;

	if (val)
		ret = qca8k_rmw(priv, reg,
				QCA8K_PORT0_PAD_SGMII_RXCLK_FALLING_EDGE |
				QCA8K_PORT0_PAD_SGMII_TXCLK_FALLING_EDGE,
				val);

	return 0;
}

static void qca8k_pcs_an_restart(struct phylink_pcs *pcs)
{
}

static const struct phylink_pcs_ops qca8k_pcs_ops = {
	.pcs_get_state = qca8k_pcs_get_state,
	.pcs_config = qca8k_pcs_config,
	.pcs_an_restart = qca8k_pcs_an_restart,
};

static void qca8k_setup_pcs(struct qca8k_priv *priv, struct qca8k_pcs *qpcs,
			    int port)
{
	qpcs->pcs.ops = &qca8k_pcs_ops;

	/* We don't have interrupts for link changes, so we need to poll */
	qpcs->pcs.poll = true;
	qpcs->priv = priv;
	qpcs->port = port;
}

static void
qca8k_get_strings(struct dsa_switch *ds, int port, u32 stringset, uint8_t *data)
{
	const struct qca8k_match_data *match_data;
	struct qca8k_priv *priv = ds->priv;
	int i;

	if (stringset != ETH_SS_STATS)
		return;

	match_data = of_device_get_match_data(priv->dev);

	for (i = 0; i < match_data->mib_count; i++)
		strncpy(data + i * ETH_GSTRING_LEN, ar8327_mib[i].name,
			ETH_GSTRING_LEN);
}

static void qca8k_mib_autocast_handler(struct dsa_switch *ds, struct sk_buff *skb)
{
	const struct qca8k_match_data *match_data;
	struct qca8k_mib_eth_data *mib_eth_data;
	struct qca8k_priv *priv = ds->priv;
	const struct qca8k_mib_desc *mib;
	struct mib_ethhdr *mib_ethhdr;
	int i, mib_len, offset = 0;
	u64 *data;
	u8 port;

	mib_ethhdr = (struct mib_ethhdr *)skb_mac_header(skb);
	mib_eth_data = &priv->mib_eth_data;

	/* The switch autocast every port. Ignore other packet and
	 * parse only the requested one.
	 */
	port = FIELD_GET(QCA_HDR_RECV_SOURCE_PORT, ntohs(mib_ethhdr->hdr));
	if (port != mib_eth_data->req_port)
		goto exit;

	match_data = device_get_match_data(priv->dev);
	data = mib_eth_data->data;

	for (i = 0; i < match_data->mib_count; i++) {
		mib = &ar8327_mib[i];

		/* First 3 mib are present in the skb head */
		if (i < 3) {
			data[i] = mib_ethhdr->data[i];
			continue;
		}

		mib_len = sizeof(uint32_t);

		/* Some mib are 64 bit wide */
		if (mib->size == 2)
			mib_len = sizeof(uint64_t);

		/* Copy the mib value from packet to the */
		memcpy(data + i, skb->data + offset, mib_len);

		/* Set the offset for the next mib */
		offset += mib_len;
	}

exit:
	/* Complete on receiving all the mib packet */
	if (refcount_dec_and_test(&mib_eth_data->port_parsed))
		complete(&mib_eth_data->rw_done);
}

static int
qca8k_get_ethtool_stats_eth(struct dsa_switch *ds, int port, u64 *data)
{
	struct dsa_port *dp = dsa_to_port(ds, port);
	struct qca8k_mib_eth_data *mib_eth_data;
	struct qca8k_priv *priv = ds->priv;
	int ret;

	mib_eth_data = &priv->mib_eth_data;

	mutex_lock(&mib_eth_data->mutex);

	reinit_completion(&mib_eth_data->rw_done);

	mib_eth_data->req_port = dp->index;
	mib_eth_data->data = data;
	refcount_set(&mib_eth_data->port_parsed, QCA8K_NUM_PORTS);

	mutex_lock(&priv->reg_mutex);

	/* Send mib autocast request */
	ret = regmap_update_bits(priv->regmap, QCA8K_REG_MIB,
				 QCA8K_MIB_FUNC | QCA8K_MIB_BUSY,
				 FIELD_PREP(QCA8K_MIB_FUNC, QCA8K_MIB_CAST) |
				 QCA8K_MIB_BUSY);

	mutex_unlock(&priv->reg_mutex);

	if (ret)
		goto exit;

	ret = wait_for_completion_timeout(&mib_eth_data->rw_done, QCA8K_ETHERNET_TIMEOUT);

exit:
	mutex_unlock(&mib_eth_data->mutex);

	return ret;
}

static void
qca8k_get_ethtool_stats(struct dsa_switch *ds, int port,
			uint64_t *data)
{
	struct qca8k_priv *priv = (struct qca8k_priv *)ds->priv;
	const struct qca8k_match_data *match_data;
	const struct qca8k_mib_desc *mib;
	u32 reg, i, val;
	u32 hi = 0;
	int ret;

	if (priv->mgmt_master &&
	    qca8k_get_ethtool_stats_eth(ds, port, data) > 0)
		return;

	match_data = of_device_get_match_data(priv->dev);

	for (i = 0; i < match_data->mib_count; i++) {
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

static int
qca8k_get_sset_count(struct dsa_switch *ds, int port, int sset)
{
	const struct qca8k_match_data *match_data;
	struct qca8k_priv *priv = ds->priv;

	if (sset != ETH_SS_STATS)
		return 0;

	match_data = of_device_get_match_data(priv->dev);

	return match_data->mib_count;
}

static int
qca8k_set_mac_eee(struct dsa_switch *ds, int port, struct ethtool_eee *eee)
{
	struct qca8k_priv *priv = (struct qca8k_priv *)ds->priv;
	u32 lpi_en = QCA8K_REG_EEE_CTRL_LPI_EN(port);
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

static int
qca8k_get_mac_eee(struct dsa_switch *ds, int port, struct ethtool_eee *e)
{
	/* Nothing to do on the port's MAC */
	return 0;
}

static void
qca8k_port_stp_state_set(struct dsa_switch *ds, int port, u8 state)
{
	struct qca8k_priv *priv = (struct qca8k_priv *)ds->priv;
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

static int qca8k_port_bridge_join(struct dsa_switch *ds, int port,
				  struct dsa_bridge bridge,
				  bool *tx_fwd_offload,
				  struct netlink_ext_ack *extack)
{
	struct qca8k_priv *priv = (struct qca8k_priv *)ds->priv;
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

static void qca8k_port_bridge_leave(struct dsa_switch *ds, int port,
				    struct dsa_bridge bridge)
{
	struct qca8k_priv *priv = (struct qca8k_priv *)ds->priv;
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

static void
qca8k_port_fast_age(struct dsa_switch *ds, int port)
{
	struct qca8k_priv *priv = ds->priv;

	mutex_lock(&priv->reg_mutex);
	qca8k_fdb_access(priv, QCA8K_FDB_FLUSH_PORT, port);
	mutex_unlock(&priv->reg_mutex);
}

static int
qca8k_set_ageing_time(struct dsa_switch *ds, unsigned int msecs)
{
	struct qca8k_priv *priv = ds->priv;
	unsigned int secs = msecs / 1000;
	u32 val;

	/* AGE_TIME reg is set in 7s step */
	val = secs / 7;

	/* Handle case with 0 as val to NOT disable
	 * learning
	 */
	if (!val)
		val = 1;

	return regmap_update_bits(priv->regmap, QCA8K_REG_ATU_CTRL, QCA8K_ATU_AGE_TIME_MASK,
				  QCA8K_ATU_AGE_TIME(val));
}

static int
qca8k_port_enable(struct dsa_switch *ds, int port,
		  struct phy_device *phy)
{
	struct qca8k_priv *priv = (struct qca8k_priv *)ds->priv;

	qca8k_port_set_status(priv, port, 1);
	priv->port_enabled_map |= BIT(port);

	if (dsa_is_user_port(ds, port))
		phy_support_asym_pause(phy);

	return 0;
}

static void
qca8k_port_disable(struct dsa_switch *ds, int port)
{
	struct qca8k_priv *priv = (struct qca8k_priv *)ds->priv;

	qca8k_port_set_status(priv, port, 0);
	priv->port_enabled_map &= ~BIT(port);
}

static int
qca8k_port_change_mtu(struct dsa_switch *ds, int port, int new_mtu)
{
	struct qca8k_priv *priv = ds->priv;

	/* We have only have a general MTU setting.
	 * DSA always set the CPU port's MTU to the largest MTU of the slave
	 * ports.
	 * Setting MTU just for the CPU port is sufficient to correctly set a
	 * value for every port.
	 */
	if (!dsa_is_cpu_port(ds, port))
		return 0;

	/* Include L2 header / FCS length */
	return qca8k_write(priv, QCA8K_MAX_FRAME_SIZE, new_mtu + ETH_HLEN + ETH_FCS_LEN);
}

static int
qca8k_port_max_mtu(struct dsa_switch *ds, int port)
{
	return QCA8K_MAX_MTU;
}

static int
qca8k_port_fdb_insert(struct qca8k_priv *priv, const u8 *addr,
		      u16 port_mask, u16 vid)
{
	/* Set the vid to the port vlan id if no vid is set */
	if (!vid)
		vid = QCA8K_PORT_VID_DEF;

	return qca8k_fdb_add(priv, addr, port_mask, vid,
			     QCA8K_ATU_STATUS_STATIC);
}

static int
qca8k_port_fdb_add(struct dsa_switch *ds, int port,
		   const unsigned char *addr, u16 vid,
		   struct dsa_db db)
{
	struct qca8k_priv *priv = (struct qca8k_priv *)ds->priv;
	u16 port_mask = BIT(port);

	return qca8k_port_fdb_insert(priv, addr, port_mask, vid);
}

static int
qca8k_port_fdb_del(struct dsa_switch *ds, int port,
		   const unsigned char *addr, u16 vid,
		   struct dsa_db db)
{
	struct qca8k_priv *priv = (struct qca8k_priv *)ds->priv;
	u16 port_mask = BIT(port);

	if (!vid)
		vid = QCA8K_PORT_VID_DEF;

	return qca8k_fdb_del(priv, addr, port_mask, vid);
}

static int
qca8k_port_fdb_dump(struct dsa_switch *ds, int port,
		    dsa_fdb_dump_cb_t *cb, void *data)
{
	struct qca8k_priv *priv = (struct qca8k_priv *)ds->priv;
	struct qca8k_fdb _fdb = { 0 };
	int cnt = QCA8K_NUM_FDB_RECORDS;
	bool is_static;
	int ret = 0;

	mutex_lock(&priv->reg_mutex);
	while (cnt-- && !qca8k_fdb_next(priv, &_fdb, port)) {
		if (!_fdb.aging)
			break;
		is_static = (_fdb.aging == QCA8K_ATU_STATUS_STATIC);
		ret = cb(_fdb.mac, _fdb.vid, is_static, data);
		if (ret)
			break;
	}
	mutex_unlock(&priv->reg_mutex);

	return 0;
}

static int
qca8k_port_mdb_add(struct dsa_switch *ds, int port,
		   const struct switchdev_obj_port_mdb *mdb,
		   struct dsa_db db)
{
	struct qca8k_priv *priv = ds->priv;
	const u8 *addr = mdb->addr;
	u16 vid = mdb->vid;

	return qca8k_fdb_search_and_insert(priv, BIT(port), addr, vid);
}

static int
qca8k_port_mdb_del(struct dsa_switch *ds, int port,
		   const struct switchdev_obj_port_mdb *mdb,
		   struct dsa_db db)
{
	struct qca8k_priv *priv = ds->priv;
	const u8 *addr = mdb->addr;
	u16 vid = mdb->vid;

	return qca8k_fdb_search_and_del(priv, BIT(port), addr, vid);
}

static int
qca8k_port_mirror_add(struct dsa_switch *ds, int port,
		      struct dsa_mall_mirror_tc_entry *mirror,
		      bool ingress, struct netlink_ext_ack *extack)
{
	struct qca8k_priv *priv = ds->priv;
	int monitor_port, ret;
	u32 reg, val;

	/* Check for existent entry */
	if ((ingress ? priv->mirror_rx : priv->mirror_tx) & BIT(port))
		return -EEXIST;

	ret = regmap_read(priv->regmap, QCA8K_REG_GLOBAL_FW_CTRL0, &val);
	if (ret)
		return ret;

	/* QCA83xx can have only one port set to mirror mode.
	 * Check that the correct port is requested and return error otherwise.
	 * When no mirror port is set, the values is set to 0xF
	 */
	monitor_port = FIELD_GET(QCA8K_GLOBAL_FW_CTRL0_MIRROR_PORT_NUM, val);
	if (monitor_port != 0xF && monitor_port != mirror->to_local_port)
		return -EEXIST;

	/* Set the monitor port */
	val = FIELD_PREP(QCA8K_GLOBAL_FW_CTRL0_MIRROR_PORT_NUM,
			 mirror->to_local_port);
	ret = regmap_update_bits(priv->regmap, QCA8K_REG_GLOBAL_FW_CTRL0,
				 QCA8K_GLOBAL_FW_CTRL0_MIRROR_PORT_NUM, val);
	if (ret)
		return ret;

	if (ingress) {
		reg = QCA8K_PORT_LOOKUP_CTRL(port);
		val = QCA8K_PORT_LOOKUP_ING_MIRROR_EN;
	} else {
		reg = QCA8K_REG_PORT_HOL_CTRL1(port);
		val = QCA8K_PORT_HOL_CTRL1_EG_MIRROR_EN;
	}

	ret = regmap_update_bits(priv->regmap, reg, val, val);
	if (ret)
		return ret;

	/* Track mirror port for tx and rx to decide when the
	 * mirror port has to be disabled.
	 */
	if (ingress)
		priv->mirror_rx |= BIT(port);
	else
		priv->mirror_tx |= BIT(port);

	return 0;
}

static void
qca8k_port_mirror_del(struct dsa_switch *ds, int port,
		      struct dsa_mall_mirror_tc_entry *mirror)
{
	struct qca8k_priv *priv = ds->priv;
	u32 reg, val;
	int ret;

	if (mirror->ingress) {
		reg = QCA8K_PORT_LOOKUP_CTRL(port);
		val = QCA8K_PORT_LOOKUP_ING_MIRROR_EN;
	} else {
		reg = QCA8K_REG_PORT_HOL_CTRL1(port);
		val = QCA8K_PORT_HOL_CTRL1_EG_MIRROR_EN;
	}

	ret = regmap_clear_bits(priv->regmap, reg, val);
	if (ret)
		goto err;

	if (mirror->ingress)
		priv->mirror_rx &= ~BIT(port);
	else
		priv->mirror_tx &= ~BIT(port);

	/* No port set to send packet to mirror port. Disable mirror port */
	if (!priv->mirror_rx && !priv->mirror_tx) {
		val = FIELD_PREP(QCA8K_GLOBAL_FW_CTRL0_MIRROR_PORT_NUM, 0xF);
		ret = regmap_update_bits(priv->regmap, QCA8K_REG_GLOBAL_FW_CTRL0,
					 QCA8K_GLOBAL_FW_CTRL0_MIRROR_PORT_NUM, val);
		if (ret)
			goto err;
	}
err:
	dev_err(priv->dev, "Failed to del mirror port from %d", port);
}

static int
qca8k_port_vlan_filtering(struct dsa_switch *ds, int port, bool vlan_filtering,
			  struct netlink_ext_ack *extack)
{
	struct qca8k_priv *priv = ds->priv;
	int ret;

	if (vlan_filtering) {
		ret = qca8k_rmw(priv, QCA8K_PORT_LOOKUP_CTRL(port),
				QCA8K_PORT_LOOKUP_VLAN_MODE_MASK,
				QCA8K_PORT_LOOKUP_VLAN_MODE_SECURE);
	} else {
		ret = qca8k_rmw(priv, QCA8K_PORT_LOOKUP_CTRL(port),
				QCA8K_PORT_LOOKUP_VLAN_MODE_MASK,
				QCA8K_PORT_LOOKUP_VLAN_MODE_NONE);
	}

	return ret;
}

static int
qca8k_port_vlan_add(struct dsa_switch *ds, int port,
		    const struct switchdev_obj_port_vlan *vlan,
		    struct netlink_ext_ack *extack)
{
	bool untagged = vlan->flags & BRIDGE_VLAN_INFO_UNTAGGED;
	bool pvid = vlan->flags & BRIDGE_VLAN_INFO_PVID;
	struct qca8k_priv *priv = ds->priv;
	int ret;

	ret = qca8k_vlan_add(priv, port, vlan->vid, untagged);
	if (ret) {
		dev_err(priv->dev, "Failed to add VLAN to port %d (%d)", port, ret);
		return ret;
	}

	if (pvid) {
		ret = qca8k_rmw(priv, QCA8K_EGRESS_VLAN(port),
				QCA8K_EGREES_VLAN_PORT_MASK(port),
				QCA8K_EGREES_VLAN_PORT(port, vlan->vid));
		if (ret)
			return ret;

		ret = qca8k_write(priv, QCA8K_REG_PORT_VLAN_CTRL0(port),
				  QCA8K_PORT_VLAN_CVID(vlan->vid) |
				  QCA8K_PORT_VLAN_SVID(vlan->vid));
	}

	return ret;
}

static int
qca8k_port_vlan_del(struct dsa_switch *ds, int port,
		    const struct switchdev_obj_port_vlan *vlan)
{
	struct qca8k_priv *priv = ds->priv;
	int ret;

	ret = qca8k_vlan_del(priv, port, vlan->vid);
	if (ret)
		dev_err(priv->dev, "Failed to delete VLAN from port %d (%d)", port, ret);

	return ret;
}

static u32 qca8k_get_phy_flags(struct dsa_switch *ds, int port)
{
	struct qca8k_priv *priv = ds->priv;

	/* Communicate to the phy internal driver the switch revision.
	 * Based on the switch revision different values needs to be
	 * set to the dbg and mmd reg on the phy.
	 * The first 2 bit are used to communicate the switch revision
	 * to the phy driver.
	 */
	if (port > 0 && port < 6)
		return priv->switch_revision;

	return 0;
}

static enum dsa_tag_protocol
qca8k_get_tag_protocol(struct dsa_switch *ds, int port,
		       enum dsa_tag_protocol mp)
{
	return DSA_TAG_PROTO_QCA;
}

static bool
qca8k_lag_can_offload(struct dsa_switch *ds, struct dsa_lag lag,
		      struct netdev_lag_upper_info *info)
{
	struct dsa_port *dp;
	int members = 0;

	if (!lag.id)
		return false;

	dsa_lag_foreach_port(dp, ds->dst, &lag)
		/* Includes the port joining the LAG */
		members++;

	if (members > QCA8K_NUM_PORTS_FOR_LAG)
		return false;

	if (info->tx_type != NETDEV_LAG_TX_TYPE_HASH)
		return false;

	if (info->hash_type != NETDEV_LAG_HASH_L2 &&
	    info->hash_type != NETDEV_LAG_HASH_L23)
		return false;

	return true;
}

static int
qca8k_lag_setup_hash(struct dsa_switch *ds, struct dsa_lag lag,
		     struct netdev_lag_upper_info *info)
{
	struct net_device *lag_dev = lag.dev;
	struct qca8k_priv *priv = ds->priv;
	bool unique_lag = true;
	unsigned int i;
	u32 hash = 0;

	switch (info->hash_type) {
	case NETDEV_LAG_HASH_L23:
		hash |= QCA8K_TRUNK_HASH_SIP_EN;
		hash |= QCA8K_TRUNK_HASH_DIP_EN;
		fallthrough;
	case NETDEV_LAG_HASH_L2:
		hash |= QCA8K_TRUNK_HASH_SA_EN;
		hash |= QCA8K_TRUNK_HASH_DA_EN;
		break;
	default: /* We should NEVER reach this */
		return -EOPNOTSUPP;
	}

	/* Check if we are the unique configured LAG */
	dsa_lags_foreach_id(i, ds->dst)
		if (i != lag.id && dsa_lag_by_id(ds->dst, i)) {
			unique_lag = false;
			break;
		}

	/* Hash Mode is global. Make sure the same Hash Mode
	 * is set to all the 4 possible lag.
	 * If we are the unique LAG we can set whatever hash
	 * mode we want.
	 * To change hash mode it's needed to remove all LAG
	 * and change the mode with the latest.
	 */
	if (unique_lag) {
		priv->lag_hash_mode = hash;
	} else if (priv->lag_hash_mode != hash) {
		netdev_err(lag_dev, "Error: Mismatched Hash Mode across different lag is not supported\n");
		return -EOPNOTSUPP;
	}

	return regmap_update_bits(priv->regmap, QCA8K_TRUNK_HASH_EN_CTRL,
				  QCA8K_TRUNK_HASH_MASK, hash);
}

static int
qca8k_lag_refresh_portmap(struct dsa_switch *ds, int port,
			  struct dsa_lag lag, bool delete)
{
	struct qca8k_priv *priv = ds->priv;
	int ret, id, i;
	u32 val;

	/* DSA LAG IDs are one-based, hardware is zero-based */
	id = lag.id - 1;

	/* Read current port member */
	ret = regmap_read(priv->regmap, QCA8K_REG_GOL_TRUNK_CTRL0, &val);
	if (ret)
		return ret;

	/* Shift val to the correct trunk */
	val >>= QCA8K_REG_GOL_TRUNK_SHIFT(id);
	val &= QCA8K_REG_GOL_TRUNK_MEMBER_MASK;
	if (delete)
		val &= ~BIT(port);
	else
		val |= BIT(port);

	/* Update port member. With empty portmap disable trunk */
	ret = regmap_update_bits(priv->regmap, QCA8K_REG_GOL_TRUNK_CTRL0,
				 QCA8K_REG_GOL_TRUNK_MEMBER(id) |
				 QCA8K_REG_GOL_TRUNK_EN(id),
				 !val << QCA8K_REG_GOL_TRUNK_SHIFT(id) |
				 val << QCA8K_REG_GOL_TRUNK_SHIFT(id));

	/* Search empty member if adding or port on deleting */
	for (i = 0; i < QCA8K_NUM_PORTS_FOR_LAG; i++) {
		ret = regmap_read(priv->regmap, QCA8K_REG_GOL_TRUNK_CTRL(id), &val);
		if (ret)
			return ret;

		val >>= QCA8K_REG_GOL_TRUNK_ID_MEM_ID_SHIFT(id, i);
		val &= QCA8K_REG_GOL_TRUNK_ID_MEM_ID_MASK;

		if (delete) {
			/* If port flagged to be disabled assume this member is
			 * empty
			 */
			if (val != QCA8K_REG_GOL_TRUNK_ID_MEM_ID_EN_MASK)
				continue;

			val &= QCA8K_REG_GOL_TRUNK_ID_MEM_ID_PORT_MASK;
			if (val != port)
				continue;
		} else {
			/* If port flagged to be enabled assume this member is
			 * already set
			 */
			if (val == QCA8K_REG_GOL_TRUNK_ID_MEM_ID_EN_MASK)
				continue;
		}

		/* We have found the member to add/remove */
		break;
	}

	/* Set port in the correct port mask or disable port if in delete mode */
	return regmap_update_bits(priv->regmap, QCA8K_REG_GOL_TRUNK_CTRL(id),
				  QCA8K_REG_GOL_TRUNK_ID_MEM_ID_EN(id, i) |
				  QCA8K_REG_GOL_TRUNK_ID_MEM_ID_PORT(id, i),
				  !delete << QCA8K_REG_GOL_TRUNK_ID_MEM_ID_SHIFT(id, i) |
				  port << QCA8K_REG_GOL_TRUNK_ID_MEM_ID_SHIFT(id, i));
}

static int
qca8k_port_lag_join(struct dsa_switch *ds, int port, struct dsa_lag lag,
		    struct netdev_lag_upper_info *info)
{
	int ret;

	if (!qca8k_lag_can_offload(ds, lag, info))
		return -EOPNOTSUPP;

	ret = qca8k_lag_setup_hash(ds, lag, info);
	if (ret)
		return ret;

	return qca8k_lag_refresh_portmap(ds, port, lag, false);
}

static int
qca8k_port_lag_leave(struct dsa_switch *ds, int port,
		     struct dsa_lag lag)
{
	return qca8k_lag_refresh_portmap(ds, port, lag, true);
}

static void
qca8k_master_change(struct dsa_switch *ds, const struct net_device *master,
		    bool operational)
{
	struct dsa_port *dp = master->dsa_ptr;
	struct qca8k_priv *priv = ds->priv;

	/* Ethernet MIB/MDIO is only supported for CPU port 0 */
	if (dp->index != 0)
		return;

	mutex_lock(&priv->mgmt_eth_data.mutex);
	mutex_lock(&priv->mib_eth_data.mutex);

	priv->mgmt_master = operational ? (struct net_device *)master : NULL;

	mutex_unlock(&priv->mib_eth_data.mutex);
	mutex_unlock(&priv->mgmt_eth_data.mutex);
}

static int qca8k_connect_tag_protocol(struct dsa_switch *ds,
				      enum dsa_tag_protocol proto)
{
	struct qca_tagger_data *tagger_data;

	switch (proto) {
	case DSA_TAG_PROTO_QCA:
		tagger_data = ds->tagger_data;

		tagger_data->rw_reg_ack_handler = qca8k_rw_reg_ack_handler;
		tagger_data->mib_autocast_handler = qca8k_mib_autocast_handler;

		break;
	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

static int
qca8k_setup(struct dsa_switch *ds)
{
	struct qca8k_priv *priv = (struct qca8k_priv *)ds->priv;
	int cpu_port, ret, i;
	u32 mask;

	cpu_port = qca8k_find_cpu_port(ds);
	if (cpu_port < 0) {
		dev_err(priv->dev, "No cpu port configured in both cpu port0 and port6");
		return cpu_port;
	}

	/* Parse CPU port config to be later used in phy_link mac_config */
	ret = qca8k_parse_port_config(priv);
	if (ret)
		return ret;

	ret = qca8k_setup_mdio_bus(priv);
	if (ret)
		return ret;

	ret = qca8k_setup_of_pws_reg(priv);
	if (ret)
		return ret;

	ret = qca8k_setup_mac_pwr_sel(priv);
	if (ret)
		return ret;

	qca8k_setup_pcs(priv, &priv->pcs_port_0, 0);
	qca8k_setup_pcs(priv, &priv->pcs_port_6, 6);

	/* Make sure MAC06 is disabled */
	ret = regmap_clear_bits(priv->regmap, QCA8K_REG_PORT0_PAD_CTRL,
				QCA8K_PORT0_PAD_MAC06_EXCHANGE_EN);
	if (ret) {
		dev_err(priv->dev, "failed disabling MAC06 exchange");
		return ret;
	}

	/* Enable CPU Port */
	ret = regmap_set_bits(priv->regmap, QCA8K_REG_GLOBAL_FW_CTRL0,
			      QCA8K_GLOBAL_FW_CTRL0_CPU_PORT_EN);
	if (ret) {
		dev_err(priv->dev, "failed enabling CPU port");
		return ret;
	}

	/* Enable MIB counters */
	ret = qca8k_mib_init(priv);
	if (ret)
		dev_warn(priv->dev, "mib init failed");

	/* Initial setup of all ports */
	for (i = 0; i < QCA8K_NUM_PORTS; i++) {
		/* Disable forwarding by default on all ports */
		ret = qca8k_rmw(priv, QCA8K_PORT_LOOKUP_CTRL(i),
				QCA8K_PORT_LOOKUP_MEMBER, 0);
		if (ret)
			return ret;

		/* Enable QCA header mode on all cpu ports */
		if (dsa_is_cpu_port(ds, i)) {
			ret = qca8k_write(priv, QCA8K_REG_PORT_HDR_CTRL(i),
					  FIELD_PREP(QCA8K_PORT_HDR_CTRL_TX_MASK, QCA8K_PORT_HDR_CTRL_ALL) |
					  FIELD_PREP(QCA8K_PORT_HDR_CTRL_RX_MASK, QCA8K_PORT_HDR_CTRL_ALL));
			if (ret) {
				dev_err(priv->dev, "failed enabling QCA header mode");
				return ret;
			}
		}

		/* Disable MAC by default on all user ports */
		if (dsa_is_user_port(ds, i))
			qca8k_port_set_status(priv, i, 0);
	}

	/* Forward all unknown frames to CPU port for Linux processing
	 * Notice that in multi-cpu config only one port should be set
	 * for igmp, unknown, multicast and broadcast packet
	 */
	ret = qca8k_write(priv, QCA8K_REG_GLOBAL_FW_CTRL1,
			  FIELD_PREP(QCA8K_GLOBAL_FW_CTRL1_IGMP_DP_MASK, BIT(cpu_port)) |
			  FIELD_PREP(QCA8K_GLOBAL_FW_CTRL1_BC_DP_MASK, BIT(cpu_port)) |
			  FIELD_PREP(QCA8K_GLOBAL_FW_CTRL1_MC_DP_MASK, BIT(cpu_port)) |
			  FIELD_PREP(QCA8K_GLOBAL_FW_CTRL1_UC_DP_MASK, BIT(cpu_port)));
	if (ret)
		return ret;

	/* Setup connection between CPU port & user ports
	 * Configure specific switch configuration for ports
	 */
	for (i = 0; i < QCA8K_NUM_PORTS; i++) {
		/* CPU port gets connected to all user ports of the switch */
		if (dsa_is_cpu_port(ds, i)) {
			ret = qca8k_rmw(priv, QCA8K_PORT_LOOKUP_CTRL(i),
					QCA8K_PORT_LOOKUP_MEMBER, dsa_user_ports(ds));
			if (ret)
				return ret;
		}

		/* Individual user ports get connected to CPU port only */
		if (dsa_is_user_port(ds, i)) {
			ret = qca8k_rmw(priv, QCA8K_PORT_LOOKUP_CTRL(i),
					QCA8K_PORT_LOOKUP_MEMBER,
					BIT(cpu_port));
			if (ret)
				return ret;

			/* Enable ARP Auto-learning by default */
			ret = regmap_set_bits(priv->regmap, QCA8K_PORT_LOOKUP_CTRL(i),
					      QCA8K_PORT_LOOKUP_LEARN);
			if (ret)
				return ret;

			/* For port based vlans to work we need to set the
			 * default egress vid
			 */
			ret = qca8k_rmw(priv, QCA8K_EGRESS_VLAN(i),
					QCA8K_EGREES_VLAN_PORT_MASK(i),
					QCA8K_EGREES_VLAN_PORT(i, QCA8K_PORT_VID_DEF));
			if (ret)
				return ret;

			ret = qca8k_write(priv, QCA8K_REG_PORT_VLAN_CTRL0(i),
					  QCA8K_PORT_VLAN_CVID(QCA8K_PORT_VID_DEF) |
					  QCA8K_PORT_VLAN_SVID(QCA8K_PORT_VID_DEF));
			if (ret)
				return ret;
		}

		/* The port 5 of the qca8337 have some problem in flood condition. The
		 * original legacy driver had some specific buffer and priority settings
		 * for the different port suggested by the QCA switch team. Add this
		 * missing settings to improve switch stability under load condition.
		 * This problem is limited to qca8337 and other qca8k switch are not affected.
		 */
		if (priv->switch_id == QCA8K_ID_QCA8337) {
			switch (i) {
			/* The 2 CPU port and port 5 requires some different
			 * priority than any other ports.
			 */
			case 0:
			case 5:
			case 6:
				mask = QCA8K_PORT_HOL_CTRL0_EG_PRI0(0x3) |
					QCA8K_PORT_HOL_CTRL0_EG_PRI1(0x4) |
					QCA8K_PORT_HOL_CTRL0_EG_PRI2(0x4) |
					QCA8K_PORT_HOL_CTRL0_EG_PRI3(0x4) |
					QCA8K_PORT_HOL_CTRL0_EG_PRI4(0x6) |
					QCA8K_PORT_HOL_CTRL0_EG_PRI5(0x8) |
					QCA8K_PORT_HOL_CTRL0_EG_PORT(0x1e);
				break;
			default:
				mask = QCA8K_PORT_HOL_CTRL0_EG_PRI0(0x3) |
					QCA8K_PORT_HOL_CTRL0_EG_PRI1(0x4) |
					QCA8K_PORT_HOL_CTRL0_EG_PRI2(0x6) |
					QCA8K_PORT_HOL_CTRL0_EG_PRI3(0x8) |
					QCA8K_PORT_HOL_CTRL0_EG_PORT(0x19);
			}
			qca8k_write(priv, QCA8K_REG_PORT_HOL_CTRL0(i), mask);

			mask = QCA8K_PORT_HOL_CTRL1_ING(0x6) |
			QCA8K_PORT_HOL_CTRL1_EG_PRI_BUF_EN |
			QCA8K_PORT_HOL_CTRL1_EG_PORT_BUF_EN |
			QCA8K_PORT_HOL_CTRL1_WRED_EN;
			qca8k_rmw(priv, QCA8K_REG_PORT_HOL_CTRL1(i),
				  QCA8K_PORT_HOL_CTRL1_ING_BUF_MASK |
				  QCA8K_PORT_HOL_CTRL1_EG_PRI_BUF_EN |
				  QCA8K_PORT_HOL_CTRL1_EG_PORT_BUF_EN |
				  QCA8K_PORT_HOL_CTRL1_WRED_EN,
				  mask);
		}
	}

	/* Special GLOBAL_FC_THRESH value are needed for ar8327 switch */
	if (priv->switch_id == QCA8K_ID_QCA8327) {
		mask = QCA8K_GLOBAL_FC_GOL_XON_THRES(288) |
		       QCA8K_GLOBAL_FC_GOL_XOFF_THRES(496);
		qca8k_rmw(priv, QCA8K_REG_GLOBAL_FC_THRESH,
			  QCA8K_GLOBAL_FC_GOL_XON_THRES_MASK |
			  QCA8K_GLOBAL_FC_GOL_XOFF_THRES_MASK,
			  mask);
	}

	/* Setup our port MTUs to match power on defaults */
	ret = qca8k_write(priv, QCA8K_MAX_FRAME_SIZE, ETH_FRAME_LEN + ETH_FCS_LEN);
	if (ret)
		dev_warn(priv->dev, "failed setting MTU settings");

	/* Flush the FDB table */
	qca8k_fdb_flush(priv);

	/* Set min a max ageing value supported */
	ds->ageing_time_min = 7000;
	ds->ageing_time_max = 458745000;

	/* Set max number of LAGs supported */
	ds->num_lag_ids = QCA8K_NUM_LAGS;

	return 0;
}

static const struct dsa_switch_ops qca8k_switch_ops = {
	.get_tag_protocol	= qca8k_get_tag_protocol,
	.setup			= qca8k_setup,
	.get_strings		= qca8k_get_strings,
	.get_ethtool_stats	= qca8k_get_ethtool_stats,
	.get_sset_count		= qca8k_get_sset_count,
	.set_ageing_time	= qca8k_set_ageing_time,
	.get_mac_eee		= qca8k_get_mac_eee,
	.set_mac_eee		= qca8k_set_mac_eee,
	.port_enable		= qca8k_port_enable,
	.port_disable		= qca8k_port_disable,
	.port_change_mtu	= qca8k_port_change_mtu,
	.port_max_mtu		= qca8k_port_max_mtu,
	.port_stp_state_set	= qca8k_port_stp_state_set,
	.port_bridge_join	= qca8k_port_bridge_join,
	.port_bridge_leave	= qca8k_port_bridge_leave,
	.port_fast_age		= qca8k_port_fast_age,
	.port_fdb_add		= qca8k_port_fdb_add,
	.port_fdb_del		= qca8k_port_fdb_del,
	.port_fdb_dump		= qca8k_port_fdb_dump,
	.port_mdb_add		= qca8k_port_mdb_add,
	.port_mdb_del		= qca8k_port_mdb_del,
	.port_mirror_add	= qca8k_port_mirror_add,
	.port_mirror_del	= qca8k_port_mirror_del,
	.port_vlan_filtering	= qca8k_port_vlan_filtering,
	.port_vlan_add		= qca8k_port_vlan_add,
	.port_vlan_del		= qca8k_port_vlan_del,
	.phylink_get_caps	= qca8k_phylink_get_caps,
	.phylink_mac_select_pcs	= qca8k_phylink_mac_select_pcs,
	.phylink_mac_config	= qca8k_phylink_mac_config,
	.phylink_mac_link_down	= qca8k_phylink_mac_link_down,
	.phylink_mac_link_up	= qca8k_phylink_mac_link_up,
	.get_phy_flags		= qca8k_get_phy_flags,
	.port_lag_join		= qca8k_port_lag_join,
	.port_lag_leave		= qca8k_port_lag_leave,
	.master_state_change	= qca8k_master_change,
	.connect_tag_protocol	= qca8k_connect_tag_protocol,
};

static int qca8k_read_switch_id(struct qca8k_priv *priv)
{
	const struct qca8k_match_data *data;
	u32 val;
	u8 id;
	int ret;

	/* get the switches ID from the compatible */
	data = of_device_get_match_data(priv->dev);
	if (!data)
		return -ENODEV;

	ret = qca8k_read(priv, QCA8K_REG_MASK_CTRL, &val);
	if (ret < 0)
		return -ENODEV;

	id = QCA8K_MASK_CTRL_DEVICE_ID(val);
	if (id != data->id) {
		dev_err(priv->dev, "Switch id detected %x but expected %x", id, data->id);
		return -ENODEV;
	}

	priv->switch_id = id;

	/* Save revision to communicate to the internal PHY driver */
	priv->switch_revision = QCA8K_MASK_CTRL_REV_ID(val);

	return 0;
}

static int
qca8k_sw_probe(struct mdio_device *mdiodev)
{
	struct qca8k_priv *priv;
	int ret;

	/* allocate the private data struct so that we can probe the switches
	 * ID register
	 */
	priv = devm_kzalloc(&mdiodev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->bus = mdiodev->bus;
	priv->dev = &mdiodev->dev;

	priv->reset_gpio = devm_gpiod_get_optional(priv->dev, "reset",
						   GPIOD_ASIS);
	if (IS_ERR(priv->reset_gpio))
		return PTR_ERR(priv->reset_gpio);

	if (priv->reset_gpio) {
		gpiod_set_value_cansleep(priv->reset_gpio, 1);
		/* The active low duration must be greater than 10 ms
		 * and checkpatch.pl wants 20 ms.
		 */
		msleep(20);
		gpiod_set_value_cansleep(priv->reset_gpio, 0);
	}

	/* Start by setting up the register mapping */
	priv->regmap = devm_regmap_init(&mdiodev->dev, NULL, priv,
					&qca8k_regmap_config);
	if (IS_ERR(priv->regmap)) {
		dev_err(priv->dev, "regmap initialization failed");
		return PTR_ERR(priv->regmap);
	}

	priv->mdio_cache.page = 0xffff;
	priv->mdio_cache.lo = 0xffff;
	priv->mdio_cache.hi = 0xffff;

	/* Check the detected switch id */
	ret = qca8k_read_switch_id(priv);
	if (ret)
		return ret;

	priv->ds = devm_kzalloc(&mdiodev->dev, sizeof(*priv->ds), GFP_KERNEL);
	if (!priv->ds)
		return -ENOMEM;

	mutex_init(&priv->mgmt_eth_data.mutex);
	init_completion(&priv->mgmt_eth_data.rw_done);

	mutex_init(&priv->mib_eth_data.mutex);
	init_completion(&priv->mib_eth_data.rw_done);

	priv->ds->dev = &mdiodev->dev;
	priv->ds->num_ports = QCA8K_NUM_PORTS;
	priv->ds->priv = priv;
	priv->ds->ops = &qca8k_switch_ops;
	mutex_init(&priv->reg_mutex);
	dev_set_drvdata(&mdiodev->dev, priv);

	return dsa_register_switch(priv->ds);
}

static void
qca8k_sw_remove(struct mdio_device *mdiodev)
{
	struct qca8k_priv *priv = dev_get_drvdata(&mdiodev->dev);
	int i;

	if (!priv)
		return;

	for (i = 0; i < QCA8K_NUM_PORTS; i++)
		qca8k_port_set_status(priv, i, 0);

	dsa_unregister_switch(priv->ds);

	dev_set_drvdata(&mdiodev->dev, NULL);
}

static void qca8k_sw_shutdown(struct mdio_device *mdiodev)
{
	struct qca8k_priv *priv = dev_get_drvdata(&mdiodev->dev);

	if (!priv)
		return;

	dsa_switch_shutdown(priv->ds);

	dev_set_drvdata(&mdiodev->dev, NULL);
}

#ifdef CONFIG_PM_SLEEP
static void
qca8k_set_pm(struct qca8k_priv *priv, int enable)
{
	int port;

	for (port = 0; port < QCA8K_NUM_PORTS; port++) {
		/* Do not enable on resume if the port was
		 * disabled before.
		 */
		if (!(priv->port_enabled_map & BIT(port)))
			continue;

		qca8k_port_set_status(priv, port, enable);
	}
}

static int qca8k_suspend(struct device *dev)
{
	struct qca8k_priv *priv = dev_get_drvdata(dev);

	qca8k_set_pm(priv, 0);

	return dsa_switch_suspend(priv->ds);
}

static int qca8k_resume(struct device *dev)
{
	struct qca8k_priv *priv = dev_get_drvdata(dev);

	qca8k_set_pm(priv, 1);

	return dsa_switch_resume(priv->ds);
}
#endif /* CONFIG_PM_SLEEP */

static SIMPLE_DEV_PM_OPS(qca8k_pm_ops,
			 qca8k_suspend, qca8k_resume);

static const struct qca8k_match_data qca8327 = {
	.id = QCA8K_ID_QCA8327,
	.reduced_package = true,
	.mib_count = QCA8K_QCA832X_MIB_COUNT,
};

static const struct qca8k_match_data qca8328 = {
	.id = QCA8K_ID_QCA8327,
	.mib_count = QCA8K_QCA832X_MIB_COUNT,
};

static const struct qca8k_match_data qca833x = {
	.id = QCA8K_ID_QCA8337,
	.mib_count = QCA8K_QCA833X_MIB_COUNT,
};

static const struct of_device_id qca8k_of_match[] = {
	{ .compatible = "qca,qca8327", .data = &qca8327 },
	{ .compatible = "qca,qca8328", .data = &qca8328 },
	{ .compatible = "qca,qca8334", .data = &qca833x },
	{ .compatible = "qca,qca8337", .data = &qca833x },
	{ /* sentinel */ },
};

static struct mdio_driver qca8kmdio_driver = {
	.probe  = qca8k_sw_probe,
	.remove = qca8k_sw_remove,
	.shutdown = qca8k_sw_shutdown,
	.mdiodrv.driver = {
		.name = "qca8k",
		.of_match_table = qca8k_of_match,
		.pm = &qca8k_pm_ops,
	},
};

mdio_module_driver(qca8kmdio_driver);

MODULE_AUTHOR("Mathieu Olivari, John Crispin <john@phrozen.org>");
MODULE_DESCRIPTION("Driver for QCA8K ethernet switch family");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:qca8k");
