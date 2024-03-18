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
#include <linux/mdio.h>
#include <linux/phylink.h>
#include <linux/gpio/consumer.h>
#include <linux/etherdevice.h>
#include <linux/dsa/tag_qca.h>

#include "qca8k.h"
#include "qca8k_leds.h"

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
qca8k_mii_write_lo(struct mii_bus *bus, int phy_id, u32 regnum, u32 val)
{
	int ret;
	u16 lo;

	lo = val & 0xffff;
	ret = bus->write(bus, phy_id, regnum, lo);
	if (ret < 0)
		dev_err_ratelimited(&bus->dev,
				    "failed to write qca8k 32bit lo register\n");

	return ret;
}

static int
qca8k_mii_write_hi(struct mii_bus *bus, int phy_id, u32 regnum, u32 val)
{
	int ret;
	u16 hi;

	hi = (u16)(val >> 16);
	ret = bus->write(bus, phy_id, regnum, hi);
	if (ret < 0)
		dev_err_ratelimited(&bus->dev,
				    "failed to write qca8k 32bit hi register\n");

	return ret;
}

static int
qca8k_mii_read_lo(struct mii_bus *bus, int phy_id, u32 regnum, u32 *val)
{
	int ret;

	ret = bus->read(bus, phy_id, regnum);
	if (ret < 0)
		goto err;

	*val = ret & 0xffff;
	return 0;

err:
	dev_err_ratelimited(&bus->dev,
			    "failed to read qca8k 32bit lo register\n");
	*val = 0;

	return ret;
}

static int
qca8k_mii_read_hi(struct mii_bus *bus, int phy_id, u32 regnum, u32 *val)
{
	int ret;

	ret = bus->read(bus, phy_id, regnum);
	if (ret < 0)
		goto err;

	*val = ret << 16;
	return 0;

err:
	dev_err_ratelimited(&bus->dev,
			    "failed to read qca8k 32bit hi register\n");
	*val = 0;

	return ret;
}

static int
qca8k_mii_read32(struct mii_bus *bus, int phy_id, u32 regnum, u32 *val)
{
	u32 hi, lo;
	int ret;

	*val = 0;

	ret = qca8k_mii_read_lo(bus, phy_id, regnum, &lo);
	if (ret < 0)
		goto err;

	ret = qca8k_mii_read_hi(bus, phy_id, regnum + 1, &hi);
	if (ret < 0)
		goto err;

	*val = lo | hi;

err:
	return ret;
}

static void
qca8k_mii_write32(struct mii_bus *bus, int phy_id, u32 regnum, u32 val)
{
	if (qca8k_mii_write_lo(bus, phy_id, regnum, val) < 0)
		return;

	qca8k_mii_write_hi(bus, phy_id, regnum + 1, val);
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

static void qca8k_rw_reg_ack_handler(struct dsa_switch *ds, struct sk_buff *skb)
{
	struct qca8k_mgmt_eth_data *mgmt_eth_data;
	struct qca8k_priv *priv = ds->priv;
	struct qca_mgmt_ethhdr *mgmt_ethhdr;
	u32 command;
	u8 len, cmd;
	int i;

	mgmt_ethhdr = (struct qca_mgmt_ethhdr *)skb_mac_header(skb);
	mgmt_eth_data = &priv->mgmt_eth_data;

	command = get_unaligned_le32(&mgmt_ethhdr->command);
	cmd = FIELD_GET(QCA_HDR_MGMT_CMD, command);

	len = FIELD_GET(QCA_HDR_MGMT_LENGTH, command);
	/* Special case for len of 15 as this is the max value for len and needs to
	 * be increased before converting it from word to dword.
	 */
	if (len == 15)
		len++;

	/* We can ignore odd value, we always round up them in the alloc function. */
	len *= sizeof(u16);

	/* Make sure the seq match the requested packet */
	if (get_unaligned_le32(&mgmt_ethhdr->seq) == mgmt_eth_data->seq)
		mgmt_eth_data->ack = true;

	if (cmd == MDIO_READ) {
		u32 *val = mgmt_eth_data->data;

		*val = get_unaligned_le32(&mgmt_ethhdr->mdio_data);

		/* Get the rest of the 12 byte of data.
		 * The read/write function will extract the requested data.
		 */
		if (len > QCA_HDR_MGMT_DATA1_LEN) {
			__le32 *data2 = (__le32 *)skb->data;
			int data_len = min_t(int, QCA_HDR_MGMT_DATA2_LEN,
					     len - QCA_HDR_MGMT_DATA1_LEN);

			val++;

			for (i = sizeof(u32); i <= data_len; i += sizeof(u32)) {
				*val = get_unaligned_le32(data2);
				val++;
				data2++;
			}
		}
	}

	complete(&mgmt_eth_data->rw_done);
}

static struct sk_buff *qca8k_alloc_mdio_header(enum mdio_cmd cmd, u32 reg, u32 *val,
					       int priority, unsigned int len)
{
	struct qca_mgmt_ethhdr *mgmt_ethhdr;
	unsigned int real_len;
	struct sk_buff *skb;
	__le32 *data2;
	u32 command;
	u16 hdr;
	int i;

	skb = dev_alloc_skb(QCA_HDR_MGMT_PKT_LEN);
	if (!skb)
		return NULL;

	/* Hdr mgmt length value is in step of word size.
	 * As an example to process 4 byte of data the correct length to set is 2.
	 * To process 8 byte 4, 12 byte 6, 16 byte 8...
	 *
	 * Odd values will always return the next size on the ack packet.
	 * (length of 3 (6 byte) will always return 8 bytes of data)
	 *
	 * This means that a value of 15 (0xf) actually means reading/writing 32 bytes
	 * of data.
	 *
	 * To correctly calculate the length we devide the requested len by word and
	 * round up.
	 * On the ack function we can skip the odd check as we already handle the
	 * case here.
	 */
	real_len = DIV_ROUND_UP(len, sizeof(u16));

	/* We check if the result len is odd and we round up another time to
	 * the next size. (length of 3 will be increased to 4 as switch will always
	 * return 8 bytes)
	 */
	if (real_len % sizeof(u16) != 0)
		real_len++;

	/* Max reg value is 0xf(15) but switch will always return the next size (32 byte) */
	if (real_len == 16)
		real_len--;

	skb_reset_mac_header(skb);
	skb_set_network_header(skb, skb->len);

	mgmt_ethhdr = skb_push(skb, QCA_HDR_MGMT_HEADER_LEN + QCA_HDR_LEN);

	hdr = FIELD_PREP(QCA_HDR_XMIT_VERSION, QCA_HDR_VERSION);
	hdr |= FIELD_PREP(QCA_HDR_XMIT_PRIORITY, priority);
	hdr |= QCA_HDR_XMIT_FROM_CPU;
	hdr |= FIELD_PREP(QCA_HDR_XMIT_DP_BIT, BIT(0));
	hdr |= FIELD_PREP(QCA_HDR_XMIT_CONTROL, QCA_HDR_XMIT_TYPE_RW_REG);

	command = FIELD_PREP(QCA_HDR_MGMT_ADDR, reg);
	command |= FIELD_PREP(QCA_HDR_MGMT_LENGTH, real_len);
	command |= FIELD_PREP(QCA_HDR_MGMT_CMD, cmd);
	command |= FIELD_PREP(QCA_HDR_MGMT_CHECK_CODE,
					   QCA_HDR_MGMT_CHECK_CODE_VAL);

	put_unaligned_le32(command, &mgmt_ethhdr->command);

	if (cmd == MDIO_WRITE)
		put_unaligned_le32(*val, &mgmt_ethhdr->mdio_data);

	mgmt_ethhdr->hdr = htons(hdr);

	data2 = skb_put_zero(skb, QCA_HDR_MGMT_DATA2_LEN + QCA_HDR_MGMT_PADDING_LEN);
	if (cmd == MDIO_WRITE && len > QCA_HDR_MGMT_DATA1_LEN) {
		int data_len = min_t(int, QCA_HDR_MGMT_DATA2_LEN,
				     len - QCA_HDR_MGMT_DATA1_LEN);

		val++;

		for (i = sizeof(u32); i <= data_len; i += sizeof(u32)) {
			put_unaligned_le32(*val, data2);
			data2++;
			val++;
		}
	}

	return skb;
}

static void qca8k_mdio_header_fill_seq_num(struct sk_buff *skb, u32 seq_num)
{
	struct qca_mgmt_ethhdr *mgmt_ethhdr;
	u32 seq;

	seq = FIELD_PREP(QCA_HDR_MGMT_SEQ_NUM, seq_num);
	mgmt_ethhdr = (struct qca_mgmt_ethhdr *)skb->data;
	put_unaligned_le32(seq, &mgmt_ethhdr->seq);
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

	/* Check if the mgmt_conduit if is operational */
	if (!priv->mgmt_conduit) {
		kfree_skb(skb);
		mutex_unlock(&mgmt_eth_data->mutex);
		return -EINVAL;
	}

	skb->dev = priv->mgmt_conduit;

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

	/* Check if the mgmt_conduit if is operational */
	if (!priv->mgmt_conduit) {
		kfree_skb(skb);
		mutex_unlock(&mgmt_eth_data->mutex);
		return -EINVAL;
	}

	skb->dev = priv->mgmt_conduit;

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
qca8k_read_mii(struct qca8k_priv *priv, uint32_t reg, uint32_t *val)
{
	struct mii_bus *bus = priv->bus;
	u16 r1, r2, page;
	int ret;

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
qca8k_write_mii(struct qca8k_priv *priv, uint32_t reg, uint32_t val)
{
	struct mii_bus *bus = priv->bus;
	u16 r1, r2, page;
	int ret;

	qca8k_split_addr(reg, &r1, &r2, &page);

	mutex_lock_nested(&bus->mdio_lock, MDIO_MUTEX_NESTED);

	ret = qca8k_set_page(priv, page);
	if (ret < 0)
		goto exit;

	qca8k_mii_write32(bus, 0x10 | r2, r1, val);

exit:
	mutex_unlock(&bus->mdio_lock);
	return ret;
}

static int
qca8k_regmap_update_bits_mii(struct qca8k_priv *priv, uint32_t reg,
			     uint32_t mask, uint32_t write_val)
{
	struct mii_bus *bus = priv->bus;
	u16 r1, r2, page;
	u32 val;
	int ret;

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
	qca8k_mii_write32(bus, 0x10 | r2, r1, val);

exit:
	mutex_unlock(&bus->mdio_lock);

	return ret;
}

static int
qca8k_bulk_read(void *ctx, const void *reg_buf, size_t reg_len,
		void *val_buf, size_t val_len)
{
	int i, count = val_len / sizeof(u32), ret;
	struct qca8k_priv *priv = ctx;
	u32 reg = *(u16 *)reg_buf;

	if (priv->mgmt_conduit &&
	    !qca8k_read_eth(priv, reg, val_buf, val_len))
		return 0;

	/* loop count times and increment reg of 4 */
	for (i = 0; i < count; i++, reg += sizeof(u32)) {
		ret = qca8k_read_mii(priv, reg, val_buf + i);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static int
qca8k_bulk_gather_write(void *ctx, const void *reg_buf, size_t reg_len,
			const void *val_buf, size_t val_len)
{
	int i, count = val_len / sizeof(u32), ret;
	struct qca8k_priv *priv = ctx;
	u32 reg = *(u16 *)reg_buf;
	u32 *val = (u32 *)val_buf;

	if (priv->mgmt_conduit &&
	    !qca8k_write_eth(priv, reg, val, val_len))
		return 0;

	/* loop count times, increment reg of 4 and increment val ptr to
	 * the next value
	 */
	for (i = 0; i < count; i++, reg += sizeof(u32), val++) {
		ret = qca8k_write_mii(priv, reg, *val);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static int
qca8k_bulk_write(void *ctx, const void *data, size_t bytes)
{
	return qca8k_bulk_gather_write(ctx, data, sizeof(u16), data + sizeof(u16),
				       bytes - sizeof(u16));
}

static int
qca8k_regmap_update_bits(void *ctx, uint32_t reg, uint32_t mask, uint32_t write_val)
{
	struct qca8k_priv *priv = ctx;

	if (!qca8k_regmap_update_bits_eth(priv, reg, mask, write_val))
		return 0;

	return qca8k_regmap_update_bits_mii(priv, reg, mask, write_val);
}

static struct regmap_config qca8k_regmap_config = {
	.reg_bits = 16,
	.val_bits = 32,
	.reg_stride = 4,
	.max_register = 0x16ac, /* end MIB - Port6 range */
	.read = qca8k_bulk_read,
	.write = qca8k_bulk_write,
	.reg_update_bits = qca8k_regmap_update_bits,
	.rd_table = &qca8k_readable_table,
	.disable_locking = true, /* Locking is handled by qca8k read/write */
	.cache_type = REGCACHE_NONE, /* Explicitly disable CACHE */
	.max_raw_read = 32, /* mgmt eth can read up to 8 registers at time */
	/* ATU regs suffer from a bug where some data are not correctly
	 * written. Disable bulk write to correctly write ATU entry.
	 */
	.use_single_write = true,
};

static int
qca8k_phy_eth_busy_wait(struct qca8k_mgmt_eth_data *mgmt_eth_data,
			struct sk_buff *read_skb, u32 *val)
{
	struct sk_buff *skb = skb_copy(read_skb, GFP_KERNEL);
	bool ack;
	int ret;

	if (!skb)
		return -ENOMEM;

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
	struct net_device *mgmt_conduit;
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

	/* It seems that accessing the switch's internal PHYs via management
	 * packets still uses the MDIO bus within the switch internally, and
	 * these accesses can conflict with external MDIO accesses to other
	 * devices on the MDIO bus.
	 * We therefore need to lock the MDIO bus onto which the switch is
	 * connected.
	 */
	mutex_lock(&priv->bus->mdio_lock);

	/* Actually start the request:
	 * 1. Send mdio master packet
	 * 2. Busy Wait for mdio master command
	 * 3. Get the data if we are reading
	 * 4. Reset the mdio master (even with error)
	 */
	mutex_lock(&mgmt_eth_data->mutex);

	/* Check if mgmt_conduit is operational */
	mgmt_conduit = priv->mgmt_conduit;
	if (!mgmt_conduit) {
		mutex_unlock(&mgmt_eth_data->mutex);
		mutex_unlock(&priv->bus->mdio_lock);
		ret = -EINVAL;
		goto err_mgmt_conduit;
	}

	read_skb->dev = mgmt_conduit;
	clear_skb->dev = mgmt_conduit;
	write_skb->dev = mgmt_conduit;

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
	mutex_unlock(&priv->bus->mdio_lock);

	return ret;

	/* Error handling before lock */
err_mgmt_conduit:
	kfree_skb(read_skb);
err_read_skb:
	kfree_skb(clear_skb);
err_clear_skb:
	kfree_skb(write_skb);

	return ret;
}

static int
qca8k_mdio_busy_wait(struct mii_bus *bus, u32 reg, u32 mask)
{
	u16 r1, r2, page;
	u32 val;
	int ret, ret1;

	qca8k_split_addr(reg, &r1, &r2, &page);

	ret = read_poll_timeout(qca8k_mii_read_hi, ret1, !(val & mask), 0,
				QCA8K_BUSY_WAIT_TIMEOUT * USEC_PER_MSEC, false,
				bus, 0x10 | r2, r1 + 1, &val);

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

	qca8k_mii_write32(bus, 0x10 | r2, r1, val);

	ret = qca8k_mdio_busy_wait(bus, QCA8K_MDIO_MASTER_CTRL,
				   QCA8K_MDIO_MASTER_BUSY);

exit:
	/* even if the busy_wait timeouts try to clear the MASTER_EN */
	qca8k_mii_write_hi(bus, 0x10 | r2, r1 + 1, 0);

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

	qca8k_mii_write_hi(bus, 0x10 | r2, r1 + 1, val);

	ret = qca8k_mdio_busy_wait(bus, QCA8K_MDIO_MASTER_CTRL,
				   QCA8K_MDIO_MASTER_BUSY);
	if (ret)
		goto exit;

	ret = qca8k_mii_read_lo(bus, 0x10 | r2, r1, &val);

exit:
	/* even if the busy_wait timeouts try to clear the MASTER_EN */
	qca8k_mii_write_hi(bus, 0x10 | r2, r1 + 1, 0);

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
	struct device *dev = ds->dev;
	struct device_node *mdio;
	struct mii_bus *bus;
	int ret = 0;

	mdio = of_get_child_by_name(dev->of_node, "mdio");
	if (mdio && !of_device_is_available(mdio))
		goto out_put_node;

	bus = devm_mdiobus_alloc(dev);
	if (!bus) {
		ret = -ENOMEM;
		goto out_put_node;
	}

	priv->internal_mdio_bus = bus;
	bus->priv = (void *)priv;
	snprintf(bus->id, MII_BUS_ID_SIZE, "qca8k-%d.%d",
		 ds->dst->index, ds->index);
	bus->parent = dev;

	if (mdio) {
		/* Check if the device tree declares the port:phy mapping */
		bus->name = "qca8k user mii";
		bus->read = qca8k_internal_mdio_read;
		bus->write = qca8k_internal_mdio_write;
	} else {
		/* If a mapping can't be found, the legacy mapping is used,
		 * using qca8k_port_to_phy()
		 */
		ds->user_mii_bus = bus;
		bus->phy_mask = ~ds->phys_mii_mask;
		bus->name = "qca8k-legacy user mii";
		bus->read = qca8k_legacy_mdio_read;
		bus->write = qca8k_legacy_mdio_write;
	}

	ret = devm_of_mdiobus_register(dev, bus, mdio);

out_put_node:
	of_node_put(mdio);
	return ret;
}

static int
qca8k_setup_mdio_bus(struct qca8k_priv *priv)
{
	u32 internal_mdio_mask = 0, external_mdio_mask = 0, reg;
	struct device_node *ports, *port;
	phy_interface_t mode;
	int ret;

	ports = of_get_child_by_name(priv->dev->of_node, "ports");
	if (!ports)
		ports = of_get_child_by_name(priv->dev->of_node, "ethernet-ports");

	if (!ports)
		return -EINVAL;

	for_each_available_child_of_node(ports, port) {
		ret = of_property_read_u32(port, "reg", &reg);
		if (ret) {
			of_node_put(port);
			of_node_put(ports);
			return ret;
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
	const struct qca8k_match_data *data = priv->info;
	struct device_node *node = priv->dev->of_node;
	u32 val = 0;
	int ret;

	/* QCA8327 require to set to the correct mode.
	 * His bigger brother QCA8328 have the 172 pin layout.
	 * Should be applied by default but we set this just to make sure.
	 */
	if (priv->switch_id == QCA8K_ID_QCA8327) {
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

static int qca8k_pcs_config(struct phylink_pcs *pcs, unsigned int neg_mode,
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
	val = neg_mode == PHYLINK_PCS_NEG_INBAND_ENABLED ?
		0 : QCA8K_PWS_SERDES_AEN_DIS;

	ret = qca8k_rmw(priv, QCA8K_REG_PWS, QCA8K_PWS_SERDES_AEN_DIS, val);
	if (ret)
		return ret;

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
	qpcs->pcs.neg_mode = true;

	/* We don't have interrupts for link changes, so we need to poll */
	qpcs->pcs.poll = true;
	qpcs->priv = priv;
	qpcs->port = port;
}

static void qca8k_mib_autocast_handler(struct dsa_switch *ds, struct sk_buff *skb)
{
	struct qca8k_mib_eth_data *mib_eth_data;
	struct qca8k_priv *priv = ds->priv;
	const struct qca8k_mib_desc *mib;
	struct mib_ethhdr *mib_ethhdr;
	__le32 *data2;
	u8 port;
	int i;

	mib_ethhdr = (struct mib_ethhdr *)skb_mac_header(skb);
	mib_eth_data = &priv->mib_eth_data;

	/* The switch autocast every port. Ignore other packet and
	 * parse only the requested one.
	 */
	port = FIELD_GET(QCA_HDR_RECV_SOURCE_PORT, ntohs(mib_ethhdr->hdr));
	if (port != mib_eth_data->req_port)
		goto exit;

	data2 = (__le32 *)skb->data;

	for (i = 0; i < priv->info->mib_count; i++) {
		mib = &ar8327_mib[i];

		/* First 3 mib are present in the skb head */
		if (i < 3) {
			mib_eth_data->data[i] = get_unaligned_le32(mib_ethhdr->data + i);
			continue;
		}

		/* Some mib are 64 bit wide */
		if (mib->size == 2)
			mib_eth_data->data[i] = get_unaligned_le64((__le64 *)data2);
		else
			mib_eth_data->data[i] = get_unaligned_le32(data2);

		data2 += mib->size;
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

static void
qca8k_conduit_change(struct dsa_switch *ds, const struct net_device *conduit,
		     bool operational)
{
	struct dsa_port *dp = conduit->dsa_ptr;
	struct qca8k_priv *priv = ds->priv;

	/* Ethernet MIB/MDIO is only supported for CPU port 0 */
	if (dp->index != 0)
		return;

	mutex_lock(&priv->mgmt_eth_data.mutex);
	mutex_lock(&priv->mib_eth_data.mutex);

	priv->mgmt_conduit = operational ? (struct net_device *)conduit : NULL;

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

static void qca8k_setup_hol_fixup(struct qca8k_priv *priv, int port)
{
	u32 mask;

	switch (port) {
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
	regmap_write(priv->regmap, QCA8K_REG_PORT_HOL_CTRL0(port), mask);

	mask = QCA8K_PORT_HOL_CTRL1_ING(0x6) |
	       QCA8K_PORT_HOL_CTRL1_EG_PRI_BUF_EN |
	       QCA8K_PORT_HOL_CTRL1_EG_PORT_BUF_EN |
	       QCA8K_PORT_HOL_CTRL1_WRED_EN;
	regmap_update_bits(priv->regmap, QCA8K_REG_PORT_HOL_CTRL1(port),
			   QCA8K_PORT_HOL_CTRL1_ING_BUF_MASK |
			   QCA8K_PORT_HOL_CTRL1_EG_PRI_BUF_EN |
			   QCA8K_PORT_HOL_CTRL1_EG_PORT_BUF_EN |
			   QCA8K_PORT_HOL_CTRL1_WRED_EN,
			   mask);
}

static int
qca8k_setup(struct dsa_switch *ds)
{
	struct qca8k_priv *priv = ds->priv;
	struct dsa_port *dp;
	int cpu_port, ret;
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

	ret = qca8k_setup_led_ctrl(priv);
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
	dsa_switch_for_each_port(dp, ds) {
		/* Disable forwarding by default on all ports */
		ret = qca8k_rmw(priv, QCA8K_PORT_LOOKUP_CTRL(dp->index),
				QCA8K_PORT_LOOKUP_MEMBER, 0);
		if (ret)
			return ret;
	}

	/* Disable MAC by default on all user ports */
	dsa_switch_for_each_user_port(dp, ds)
		qca8k_port_set_status(priv, dp->index, 0);

	/* Enable QCA header mode on all cpu ports */
	dsa_switch_for_each_cpu_port(dp, ds) {
		ret = qca8k_write(priv, QCA8K_REG_PORT_HDR_CTRL(dp->index),
				  FIELD_PREP(QCA8K_PORT_HDR_CTRL_TX_MASK, QCA8K_PORT_HDR_CTRL_ALL) |
				  FIELD_PREP(QCA8K_PORT_HDR_CTRL_RX_MASK, QCA8K_PORT_HDR_CTRL_ALL));
		if (ret) {
			dev_err(priv->dev, "failed enabling QCA header mode on port %d", dp->index);
			return ret;
		}
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

	/* CPU port gets connected to all user ports of the switch */
	ret = qca8k_rmw(priv, QCA8K_PORT_LOOKUP_CTRL(cpu_port),
			QCA8K_PORT_LOOKUP_MEMBER, dsa_user_ports(ds));
	if (ret)
		return ret;

	/* Setup connection between CPU port & user ports
	 * Individual user ports get connected to CPU port only
	 */
	dsa_switch_for_each_user_port(dp, ds) {
		u8 port = dp->index;

		ret = qca8k_rmw(priv, QCA8K_PORT_LOOKUP_CTRL(port),
				QCA8K_PORT_LOOKUP_MEMBER,
				BIT(cpu_port));
		if (ret)
			return ret;

		ret = regmap_clear_bits(priv->regmap, QCA8K_PORT_LOOKUP_CTRL(port),
					QCA8K_PORT_LOOKUP_LEARN);
		if (ret)
			return ret;

		/* For port based vlans to work we need to set the
		 * default egress vid
		 */
		ret = qca8k_rmw(priv, QCA8K_EGRESS_VLAN(port),
				QCA8K_EGREES_VLAN_PORT_MASK(port),
				QCA8K_EGREES_VLAN_PORT(port, QCA8K_PORT_VID_DEF));
		if (ret)
			return ret;

		ret = qca8k_write(priv, QCA8K_REG_PORT_VLAN_CTRL0(port),
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
	if (priv->switch_id == QCA8K_ID_QCA8337)
		dsa_switch_for_each_available_port(dp, ds)
			qca8k_setup_hol_fixup(priv, dp->index);

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
	.port_pre_bridge_flags	= qca8k_port_pre_bridge_flags,
	.port_bridge_flags	= qca8k_port_bridge_flags,
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
	.conduit_state_change	= qca8k_conduit_change,
	.connect_tag_protocol	= qca8k_connect_tag_protocol,
};

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
	priv->info = of_device_get_match_data(priv->dev);

	priv->reset_gpio = devm_gpiod_get_optional(priv->dev, "reset",
						   GPIOD_OUT_HIGH);
	if (IS_ERR(priv->reset_gpio))
		return PTR_ERR(priv->reset_gpio);

	if (priv->reset_gpio) {
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

static const struct qca8k_info_ops qca8xxx_ops = {
	.autocast_mib = qca8k_get_ethtool_stats_eth,
};

static const struct qca8k_match_data qca8327 = {
	.id = QCA8K_ID_QCA8327,
	.reduced_package = true,
	.mib_count = QCA8K_QCA832X_MIB_COUNT,
	.ops = &qca8xxx_ops,
};

static const struct qca8k_match_data qca8328 = {
	.id = QCA8K_ID_QCA8327,
	.mib_count = QCA8K_QCA832X_MIB_COUNT,
	.ops = &qca8xxx_ops,
};

static const struct qca8k_match_data qca833x = {
	.id = QCA8K_ID_QCA8337,
	.mib_count = QCA8K_QCA833X_MIB_COUNT,
	.ops = &qca8xxx_ops,
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
