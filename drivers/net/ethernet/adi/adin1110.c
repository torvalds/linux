// SPDX-License-Identifier: GPL-2.0 OR BSD-2-Clause
/* ADIN1110 Low Power 10BASE-T1L Ethernet MAC-PHY
 * ADIN2111 2-Port Ethernet Switch with Integrated 10BASE-T1L PHY
 *
 * Copyright 2021 Analog Devices Inc.
 */

#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/cache.h>
#include <linux/crc8.h>
#include <linux/etherdevice.h>
#include <linux/ethtool.h>
#include <linux/if_bridge.h>
#include <linux/interrupt.h>
#include <linux/iopoll.h>
#include <linux/gpio.h>
#include <linux/kernel.h>
#include <linux/mii.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/regulator/consumer.h>
#include <linux/phy.h>
#include <linux/property.h>
#include <linux/spi/spi.h>

#include <net/switchdev.h>

#include <asm/unaligned.h>

#define ADIN1110_PHY_ID				0x1

#define ADIN1110_RESET				0x03
#define   ADIN1110_SWRESET			BIT(0)

#define ADIN1110_CONFIG1			0x04
#define   ADIN1110_CONFIG1_SYNC			BIT(15)

#define ADIN1110_CONFIG2			0x06
#define   ADIN2111_P2_FWD_UNK2HOST		BIT(12)
#define   ADIN2111_PORT_CUT_THRU_EN		BIT(11)
#define   ADIN1110_CRC_APPEND			BIT(5)
#define   ADIN1110_FWD_UNK2HOST			BIT(2)

#define ADIN1110_STATUS0			0x08

#define ADIN1110_STATUS1			0x09
#define   ADIN2111_P2_RX_RDY			BIT(17)
#define   ADIN1110_SPI_ERR			BIT(10)
#define   ADIN1110_RX_RDY			BIT(4)

#define ADIN1110_IMASK1				0x0D
#define   ADIN2111_RX_RDY_IRQ			BIT(17)
#define   ADIN1110_SPI_ERR_IRQ			BIT(10)
#define   ADIN1110_RX_RDY_IRQ			BIT(4)
#define   ADIN1110_TX_RDY_IRQ			BIT(3)

#define ADIN1110_MDIOACC			0x20
#define   ADIN1110_MDIO_TRDONE			BIT(31)
#define   ADIN1110_MDIO_ST			GENMASK(29, 28)
#define   ADIN1110_MDIO_OP			GENMASK(27, 26)
#define   ADIN1110_MDIO_PRTAD			GENMASK(25, 21)
#define   ADIN1110_MDIO_DEVAD			GENMASK(20, 16)
#define   ADIN1110_MDIO_DATA			GENMASK(15, 0)

#define ADIN1110_TX_FSIZE			0x30
#define ADIN1110_TX				0x31
#define ADIN1110_TX_SPACE			0x32

#define ADIN1110_MAC_ADDR_FILTER_UPR		0x50
#define   ADIN2111_MAC_ADDR_APPLY2PORT2		BIT(31)
#define   ADIN1110_MAC_ADDR_APPLY2PORT		BIT(30)
#define   ADIN2111_MAC_ADDR_TO_OTHER_PORT	BIT(17)
#define   ADIN1110_MAC_ADDR_TO_HOST		BIT(16)

#define ADIN1110_MAC_ADDR_FILTER_LWR		0x51

#define ADIN1110_MAC_ADDR_MASK_UPR		0x70
#define ADIN1110_MAC_ADDR_MASK_LWR		0x71

#define ADIN1110_RX_FSIZE			0x90
#define ADIN1110_RX				0x91

#define ADIN2111_RX_P2_FSIZE			0xC0
#define ADIN2111_RX_P2				0xC1

#define ADIN1110_CLEAR_STATUS0			0xFFF

/* MDIO_OP codes */
#define ADIN1110_MDIO_OP_WR			0x1
#define ADIN1110_MDIO_OP_RD			0x3

#define ADIN1110_CD				BIT(7)
#define ADIN1110_WRITE				BIT(5)

#define ADIN1110_MAX_BUFF			2048
#define ADIN1110_MAX_FRAMES_READ		64
#define ADIN1110_WR_HEADER_LEN			2
#define ADIN1110_FRAME_HEADER_LEN		2
#define ADIN1110_INTERNAL_SIZE_HEADER_LEN	2
#define ADIN1110_RD_HEADER_LEN			3
#define ADIN1110_REG_LEN			4
#define ADIN1110_FEC_LEN			4

#define ADIN1110_PHY_ID_VAL			0x0283BC91
#define ADIN2111_PHY_ID_VAL			0x0283BCA1

#define ADIN_MAC_MAX_PORTS			2
#define ADIN_MAC_MAX_ADDR_SLOTS			16

#define ADIN_MAC_MULTICAST_ADDR_SLOT		0
#define ADIN_MAC_BROADCAST_ADDR_SLOT		1
#define ADIN_MAC_P1_ADDR_SLOT			2
#define ADIN_MAC_P2_ADDR_SLOT			3
#define ADIN_MAC_FDB_ADDR_SLOT			4

DECLARE_CRC8_TABLE(adin1110_crc_table);

enum adin1110_chips_id {
	ADIN1110_MAC = 0,
	ADIN2111_MAC,
};

struct adin1110_cfg {
	enum adin1110_chips_id	id;
	char			name[MDIO_NAME_SIZE];
	u32			phy_ids[PHY_MAX_ADDR];
	u32			ports_nr;
	u32			phy_id_val;
};

struct adin1110_port_priv {
	struct adin1110_priv		*priv;
	struct net_device		*netdev;
	struct net_device		*bridge;
	struct phy_device		*phydev;
	struct work_struct		tx_work;
	u64				rx_packets;
	u64				tx_packets;
	u64				rx_bytes;
	u64				tx_bytes;
	struct work_struct		rx_mode_work;
	u32				flags;
	struct sk_buff_head		txq;
	u32				nr;
	u32				state;
	struct adin1110_cfg		*cfg;
};

struct adin1110_priv {
	struct mutex			lock; /* protect spi */
	spinlock_t			state_lock; /* protect RX mode */
	struct mii_bus			*mii_bus;
	struct spi_device		*spidev;
	bool				append_crc;
	struct adin1110_cfg		*cfg;
	u32				tx_space;
	u32				irq_mask;
	bool				forwarding;
	int				irq;
	struct adin1110_port_priv	*ports[ADIN_MAC_MAX_PORTS];
	char				mii_bus_name[MII_BUS_ID_SIZE];
	u8				data[ADIN1110_MAX_BUFF] ____cacheline_aligned;
};

struct adin1110_switchdev_event_work {
	struct work_struct work;
	struct switchdev_notifier_fdb_info fdb_info;
	struct adin1110_port_priv *port_priv;
	unsigned long event;
};

static struct adin1110_cfg adin1110_cfgs[] = {
	{
		.id = ADIN1110_MAC,
		.name = "adin1110",
		.phy_ids = {1},
		.ports_nr = 1,
		.phy_id_val = ADIN1110_PHY_ID_VAL,
	},
	{
		.id = ADIN2111_MAC,
		.name = "adin2111",
		.phy_ids = {1, 2},
		.ports_nr = 2,
		.phy_id_val = ADIN2111_PHY_ID_VAL,
	},
};

static u8 adin1110_crc_data(u8 *data, u32 len)
{
	return crc8(adin1110_crc_table, data, len, 0);
}

static int adin1110_read_reg(struct adin1110_priv *priv, u16 reg, u32 *val)
{
	u32 header_len = ADIN1110_RD_HEADER_LEN;
	u32 read_len = ADIN1110_REG_LEN;
	struct spi_transfer t[2] = {0};
	int ret;

	priv->data[0] = ADIN1110_CD | FIELD_GET(GENMASK(12, 8), reg);
	priv->data[1] = FIELD_GET(GENMASK(7, 0), reg);
	priv->data[2] = 0x00;

	if (priv->append_crc) {
		priv->data[2] = adin1110_crc_data(&priv->data[0], 2);
		priv->data[3] = 0x00;
		header_len++;
	}

	t[0].tx_buf = &priv->data[0];
	t[0].len = header_len;

	if (priv->append_crc)
		read_len++;

	memset(&priv->data[header_len], 0, read_len);
	t[1].rx_buf = &priv->data[header_len];
	t[1].len = read_len;

	ret = spi_sync_transfer(priv->spidev, t, 2);
	if (ret)
		return ret;

	if (priv->append_crc) {
		u8 recv_crc;
		u8 crc;

		crc = adin1110_crc_data(&priv->data[header_len],
					ADIN1110_REG_LEN);
		recv_crc = priv->data[header_len + ADIN1110_REG_LEN];

		if (crc != recv_crc) {
			dev_err_ratelimited(&priv->spidev->dev, "CRC error.");
			return -EBADMSG;
		}
	}

	*val = get_unaligned_be32(&priv->data[header_len]);

	return ret;
}

static int adin1110_write_reg(struct adin1110_priv *priv, u16 reg, u32 val)
{
	u32 header_len = ADIN1110_WR_HEADER_LEN;
	u32 write_len = ADIN1110_REG_LEN;

	priv->data[0] = ADIN1110_CD | ADIN1110_WRITE | FIELD_GET(GENMASK(12, 8), reg);
	priv->data[1] = FIELD_GET(GENMASK(7, 0), reg);

	if (priv->append_crc) {
		priv->data[2] = adin1110_crc_data(&priv->data[0], header_len);
		header_len++;
	}

	put_unaligned_be32(val, &priv->data[header_len]);
	if (priv->append_crc) {
		priv->data[header_len + write_len] = adin1110_crc_data(&priv->data[header_len],
								       write_len);
		write_len++;
	}

	return spi_write(priv->spidev, &priv->data[0], header_len + write_len);
}

static int adin1110_set_bits(struct adin1110_priv *priv, u16 reg,
			     unsigned long mask, unsigned long val)
{
	u32 write_val;
	int ret;

	ret = adin1110_read_reg(priv, reg, &write_val);
	if (ret < 0)
		return ret;

	set_mask_bits(&write_val, mask, val);

	return adin1110_write_reg(priv, reg, write_val);
}

static int adin1110_round_len(int len)
{
	/* can read/write only mutiples of 4 bytes of payload */
	len = ALIGN(len, 4);

	/* NOTE: ADIN1110_WR_HEADER_LEN should be used for write ops. */
	if (len + ADIN1110_RD_HEADER_LEN > ADIN1110_MAX_BUFF)
		return -EINVAL;

	return len;
}

static int adin1110_read_fifo(struct adin1110_port_priv *port_priv)
{
	struct adin1110_priv *priv = port_priv->priv;
	u32 header_len = ADIN1110_RD_HEADER_LEN;
	struct spi_transfer t[2] = {0};
	u32 frame_size_no_fcs;
	struct sk_buff *rxb;
	u32 frame_size;
	int round_len;
	u16 reg;
	int ret;

	if (!port_priv->nr) {
		reg = ADIN1110_RX;
		ret = adin1110_read_reg(priv, ADIN1110_RX_FSIZE, &frame_size);
	} else {
		reg = ADIN2111_RX_P2;
		ret = adin1110_read_reg(priv, ADIN2111_RX_P2_FSIZE,
					&frame_size);
	}

	if (ret < 0)
		return ret;

	/* The read frame size includes the extra 2 bytes
	 * from the  ADIN1110 frame header.
	 */
	if (frame_size < ADIN1110_FRAME_HEADER_LEN + ADIN1110_FEC_LEN)
		return ret;

	round_len = adin1110_round_len(frame_size);
	if (round_len < 0)
		return ret;

	frame_size_no_fcs = frame_size - ADIN1110_FRAME_HEADER_LEN - ADIN1110_FEC_LEN;

	rxb = netdev_alloc_skb(port_priv->netdev, round_len);
	if (!rxb)
		return -ENOMEM;

	memset(priv->data, 0, round_len + ADIN1110_RD_HEADER_LEN);

	priv->data[0] = ADIN1110_CD | FIELD_GET(GENMASK(12, 8), reg);
	priv->data[1] = FIELD_GET(GENMASK(7, 0), reg);

	if (priv->append_crc) {
		priv->data[2] = adin1110_crc_data(&priv->data[0], 2);
		header_len++;
	}

	skb_put(rxb, frame_size_no_fcs + ADIN1110_FRAME_HEADER_LEN);

	t[0].tx_buf = &priv->data[0];
	t[0].len = header_len;

	t[1].rx_buf = &rxb->data[0];
	t[1].len = round_len;

	ret = spi_sync_transfer(priv->spidev, t, 2);
	if (ret) {
		kfree_skb(rxb);
		return ret;
	}

	skb_pull(rxb, ADIN1110_FRAME_HEADER_LEN);
	rxb->protocol = eth_type_trans(rxb, port_priv->netdev);

	if ((port_priv->flags & IFF_ALLMULTI && rxb->pkt_type == PACKET_MULTICAST) ||
	    (port_priv->flags & IFF_BROADCAST && rxb->pkt_type == PACKET_BROADCAST))
		rxb->offload_fwd_mark = 1;

	netif_rx(rxb);

	port_priv->rx_bytes += frame_size - ADIN1110_FRAME_HEADER_LEN;
	port_priv->rx_packets++;

	return 0;
}

static int adin1110_write_fifo(struct adin1110_port_priv *port_priv,
			       struct sk_buff *txb)
{
	struct adin1110_priv *priv = port_priv->priv;
	u32 header_len = ADIN1110_WR_HEADER_LEN;
	__be16 frame_header;
	int padding = 0;
	int padded_len;
	int round_len;
	int ret;

	/* Pad frame to 64 byte length,
	 * MAC nor PHY will otherwise add the
	 * required padding.
	 * The FEC will be added by the MAC internally.
	 */
	if (txb->len + ADIN1110_FEC_LEN < 64)
		padding = 64 - (txb->len + ADIN1110_FEC_LEN);

	padded_len = txb->len + padding + ADIN1110_FRAME_HEADER_LEN;

	round_len = adin1110_round_len(padded_len);
	if (round_len < 0)
		return round_len;

	ret = adin1110_write_reg(priv, ADIN1110_TX_FSIZE, padded_len);
	if (ret < 0)
		return ret;

	memset(priv->data, 0, round_len + ADIN1110_WR_HEADER_LEN);

	priv->data[0] = ADIN1110_CD | ADIN1110_WRITE;
	priv->data[0] |= FIELD_GET(GENMASK(12, 8), ADIN1110_TX);
	priv->data[1] = FIELD_GET(GENMASK(7, 0), ADIN1110_TX);
	if (priv->append_crc) {
		priv->data[2] = adin1110_crc_data(&priv->data[0], 2);
		header_len++;
	}

	/* mention the port on which to send the frame in the frame header */
	frame_header = cpu_to_be16(port_priv->nr);
	memcpy(&priv->data[header_len], &frame_header,
	       ADIN1110_FRAME_HEADER_LEN);

	memcpy(&priv->data[header_len + ADIN1110_FRAME_HEADER_LEN],
	       txb->data, txb->len);

	ret = spi_write(priv->spidev, &priv->data[0], round_len + header_len);
	if (ret < 0)
		return ret;

	port_priv->tx_bytes += txb->len;
	port_priv->tx_packets++;

	return 0;
}

static int adin1110_read_mdio_acc(struct adin1110_priv *priv)
{
	u32 val;
	int ret;

	mutex_lock(&priv->lock);
	ret = adin1110_read_reg(priv, ADIN1110_MDIOACC, &val);
	mutex_unlock(&priv->lock);
	if (ret < 0)
		return 0;

	return val;
}

static int adin1110_mdio_read(struct mii_bus *bus, int phy_id, int reg)
{
	struct adin1110_priv *priv = bus->priv;
	u32 val = 0;
	int ret;

	if (mdio_phy_id_is_c45(phy_id))
		return -EOPNOTSUPP;

	val |= FIELD_PREP(ADIN1110_MDIO_OP, ADIN1110_MDIO_OP_RD);
	val |= FIELD_PREP(ADIN1110_MDIO_ST, 0x1);
	val |= FIELD_PREP(ADIN1110_MDIO_PRTAD, phy_id);
	val |= FIELD_PREP(ADIN1110_MDIO_DEVAD, reg);

	/* write the clause 22 read command to the chip */
	mutex_lock(&priv->lock);
	ret = adin1110_write_reg(priv, ADIN1110_MDIOACC, val);
	mutex_unlock(&priv->lock);
	if (ret < 0)
		return ret;

	/* ADIN1110_MDIO_TRDONE BIT of the ADIN1110_MDIOACC
	 * register is set when the read is done.
	 * After the transaction is done, ADIN1110_MDIO_DATA
	 * bitfield of ADIN1110_MDIOACC register will contain
	 * the requested register value.
	 */
	ret = readx_poll_timeout(adin1110_read_mdio_acc, priv, val,
				 (val & ADIN1110_MDIO_TRDONE), 10000, 30000);
	if (ret < 0)
		return ret;

	return (val & ADIN1110_MDIO_DATA);
}

static int adin1110_mdio_write(struct mii_bus *bus, int phy_id,
			       int reg, u16 reg_val)
{
	struct adin1110_priv *priv = bus->priv;
	u32 val = 0;
	int ret;

	if (mdio_phy_id_is_c45(phy_id))
		return -EOPNOTSUPP;

	val |= FIELD_PREP(ADIN1110_MDIO_OP, ADIN1110_MDIO_OP_WR);
	val |= FIELD_PREP(ADIN1110_MDIO_ST, 0x1);
	val |= FIELD_PREP(ADIN1110_MDIO_PRTAD, phy_id);
	val |= FIELD_PREP(ADIN1110_MDIO_DEVAD, reg);
	val |= FIELD_PREP(ADIN1110_MDIO_DATA, reg_val);

	/* write the clause 22 write command to the chip */
	mutex_lock(&priv->lock);
	ret = adin1110_write_reg(priv, ADIN1110_MDIOACC, val);
	mutex_unlock(&priv->lock);
	if (ret < 0)
		return ret;

	return readx_poll_timeout(adin1110_read_mdio_acc, priv, val,
				  (val & ADIN1110_MDIO_TRDONE), 10000, 30000);
}

/* ADIN1110 MAC-PHY contains an ADIN1100 PHY.
 * ADIN2111 MAC-PHY contains two ADIN1100 PHYs.
 * By registering a new MDIO bus we allow the PAL to discover
 * the encapsulated PHY and probe the ADIN1100 driver.
 */
static int adin1110_register_mdiobus(struct adin1110_priv *priv,
				     struct device *dev)
{
	struct mii_bus *mii_bus;
	int ret;

	mii_bus = devm_mdiobus_alloc(dev);
	if (!mii_bus)
		return -ENOMEM;

	snprintf(priv->mii_bus_name, MII_BUS_ID_SIZE, "%s-%u",
		 priv->cfg->name, priv->spidev->chip_select);

	mii_bus->name = priv->mii_bus_name;
	mii_bus->read = adin1110_mdio_read;
	mii_bus->write = adin1110_mdio_write;
	mii_bus->priv = priv;
	mii_bus->parent = dev;
	mii_bus->phy_mask = ~((u32)GENMASK(2, 0));
	mii_bus->probe_capabilities = MDIOBUS_C22;
	snprintf(mii_bus->id, MII_BUS_ID_SIZE, "%s", dev_name(dev));

	ret = devm_mdiobus_register(dev, mii_bus);
	if (ret)
		return ret;

	priv->mii_bus = mii_bus;

	return 0;
}

static bool adin1110_port_rx_ready(struct adin1110_port_priv *port_priv,
				   u32 status)
{
	if (!netif_oper_up(port_priv->netdev))
		return false;

	if (!port_priv->nr)
		return !!(status & ADIN1110_RX_RDY);
	else
		return !!(status & ADIN2111_P2_RX_RDY);
}

static void adin1110_read_frames(struct adin1110_port_priv *port_priv,
				 unsigned int budget)
{
	struct adin1110_priv *priv = port_priv->priv;
	u32 status1;
	int ret;

	while (budget) {
		ret = adin1110_read_reg(priv, ADIN1110_STATUS1, &status1);
		if (ret < 0)
			return;

		if (!adin1110_port_rx_ready(port_priv, status1))
			break;

		ret = adin1110_read_fifo(port_priv);
		if (ret < 0)
			return;

		budget--;
	}
}

static void adin1110_wake_queues(struct adin1110_priv *priv)
{
	int i;

	for (i = 0; i < priv->cfg->ports_nr; i++)
		netif_wake_queue(priv->ports[i]->netdev);
}

static irqreturn_t adin1110_irq(int irq, void *p)
{
	struct adin1110_priv *priv = p;
	u32 status1;
	u32 val;
	int ret;
	int i;

	mutex_lock(&priv->lock);

	ret = adin1110_read_reg(priv, ADIN1110_STATUS1, &status1);
	if (ret < 0)
		goto out;

	if (priv->append_crc && (status1 & ADIN1110_SPI_ERR))
		dev_warn_ratelimited(&priv->spidev->dev,
				     "SPI CRC error on write.\n");

	ret = adin1110_read_reg(priv, ADIN1110_TX_SPACE, &val);
	if (ret < 0)
		goto out;

	/* TX FIFO space is expressed in half-words */
	priv->tx_space = 2 * val;

	for (i = 0; i < priv->cfg->ports_nr; i++) {
		if (adin1110_port_rx_ready(priv->ports[i], status1))
			adin1110_read_frames(priv->ports[i],
					     ADIN1110_MAX_FRAMES_READ);
	}

	/* clear IRQ sources */
	adin1110_write_reg(priv, ADIN1110_STATUS0, ADIN1110_CLEAR_STATUS0);
	adin1110_write_reg(priv, ADIN1110_STATUS1, priv->irq_mask);

out:
	mutex_unlock(&priv->lock);

	if (priv->tx_space > 0 && ret >= 0)
		adin1110_wake_queues(priv);

	return IRQ_HANDLED;
}

/* ADIN1110 can filter up to 16 MAC addresses, mac_nr here is the slot used */
static int adin1110_write_mac_address(struct adin1110_port_priv *port_priv,
				      int mac_nr, const u8 *addr,
				      u8 *mask, u32 port_rules)
{
	struct adin1110_priv *priv = port_priv->priv;
	u32 offset = mac_nr * 2;
	u32 port_rules_mask;
	int ret;
	u32 val;

	if (!port_priv->nr)
		port_rules_mask = ADIN1110_MAC_ADDR_APPLY2PORT;
	else
		port_rules_mask = ADIN2111_MAC_ADDR_APPLY2PORT2;

	if (port_rules & port_rules_mask)
		port_rules_mask |= ADIN1110_MAC_ADDR_TO_HOST | ADIN2111_MAC_ADDR_TO_OTHER_PORT;

	port_rules_mask |= GENMASK(15, 0);
	val = port_rules | get_unaligned_be16(&addr[0]);
	ret = adin1110_set_bits(priv, ADIN1110_MAC_ADDR_FILTER_UPR + offset,
				port_rules_mask, val);
	if (ret < 0)
		return ret;

	val = get_unaligned_be32(&addr[2]);
	ret =  adin1110_write_reg(priv,
				  ADIN1110_MAC_ADDR_FILTER_LWR + offset, val);
	if (ret < 0)
		return ret;

	/* Only the first two MAC address slots support masking. */
	if (mac_nr < ADIN_MAC_P1_ADDR_SLOT) {
		val = get_unaligned_be16(&mask[0]);
		ret = adin1110_write_reg(priv,
					 ADIN1110_MAC_ADDR_MASK_UPR + offset,
					 val);
		if (ret < 0)
			return ret;

		val = get_unaligned_be32(&mask[2]);
		return adin1110_write_reg(priv,
					  ADIN1110_MAC_ADDR_MASK_LWR + offset,
					  val);
	}

	return 0;
}

static int adin1110_clear_mac_address(struct adin1110_priv *priv, int mac_nr)
{
	u32 offset = mac_nr * 2;
	int ret;

	ret = adin1110_write_reg(priv, ADIN1110_MAC_ADDR_FILTER_UPR + offset, 0);
	if (ret < 0)
		return ret;

	ret =  adin1110_write_reg(priv, ADIN1110_MAC_ADDR_FILTER_LWR + offset, 0);
	if (ret < 0)
		return ret;

	/* only the first two MAC address slots are maskable */
	if (mac_nr <= 1) {
		ret = adin1110_write_reg(priv, ADIN1110_MAC_ADDR_MASK_UPR + offset, 0);
		if (ret < 0)
			return ret;

		ret = adin1110_write_reg(priv, ADIN1110_MAC_ADDR_MASK_LWR + offset, 0);
	}

	return ret;
}

static u32 adin1110_port_rules(struct adin1110_port_priv *port_priv,
			       bool fw_to_host,
			       bool fw_to_other_port)
{
	u32 port_rules = 0;

	if (!port_priv->nr)
		port_rules |= ADIN1110_MAC_ADDR_APPLY2PORT;
	else
		port_rules |= ADIN2111_MAC_ADDR_APPLY2PORT2;

	if (fw_to_host)
		port_rules |= ADIN1110_MAC_ADDR_TO_HOST;

	if (fw_to_other_port && port_priv->priv->forwarding)
		port_rules |= ADIN2111_MAC_ADDR_TO_OTHER_PORT;

	return port_rules;
}

static int adin1110_multicast_filter(struct adin1110_port_priv *port_priv,
				     int mac_nr, bool accept_multicast)
{
	u8 mask[ETH_ALEN] = {0};
	u8 mac[ETH_ALEN] = {0};
	u32 port_rules = 0;

	mask[0] = BIT(0);
	mac[0] = BIT(0);

	if (accept_multicast && port_priv->state == BR_STATE_FORWARDING)
		port_rules = adin1110_port_rules(port_priv, true, true);

	return adin1110_write_mac_address(port_priv, mac_nr, mac,
					  mask, port_rules);
}

static int adin1110_broadcasts_filter(struct adin1110_port_priv *port_priv,
				      int mac_nr, bool accept_broadcast)
{
	u32 port_rules = 0;
	u8 mask[ETH_ALEN];

	memset(mask, 0xFF, ETH_ALEN);

	if (accept_broadcast && port_priv->state == BR_STATE_FORWARDING)
		port_rules = adin1110_port_rules(port_priv, true, true);

	return adin1110_write_mac_address(port_priv, mac_nr, mask,
					  mask, port_rules);
}

static int adin1110_set_mac_address(struct net_device *netdev,
				    const unsigned char *dev_addr)
{
	struct adin1110_port_priv *port_priv = netdev_priv(netdev);
	u8 mask[ETH_ALEN];
	u32 port_rules;
	u32 mac_slot;

	if (!is_valid_ether_addr(dev_addr))
		return -EADDRNOTAVAIL;

	eth_hw_addr_set(netdev, dev_addr);
	memset(mask, 0xFF, ETH_ALEN);

	mac_slot = (!port_priv->nr) ?  ADIN_MAC_P1_ADDR_SLOT : ADIN_MAC_P2_ADDR_SLOT;
	port_rules = adin1110_port_rules(port_priv, true, false);

	return adin1110_write_mac_address(port_priv, mac_slot, netdev->dev_addr,
					  mask, port_rules);
}

static int adin1110_ndo_set_mac_address(struct net_device *netdev, void *addr)
{
	struct sockaddr *sa = addr;
	int ret;

	ret = eth_prepare_mac_addr_change(netdev, addr);
	if (ret < 0)
		return ret;

	return adin1110_set_mac_address(netdev, sa->sa_data);
}

static int adin1110_ioctl(struct net_device *netdev, struct ifreq *rq, int cmd)
{
	if (!netif_running(netdev))
		return -EINVAL;

	return phy_do_ioctl(netdev, rq, cmd);
}

static int adin1110_set_promisc_mode(struct adin1110_port_priv *port_priv,
				     bool promisc)
{
	struct adin1110_priv *priv = port_priv->priv;
	u32 mask;

	if (port_priv->state != BR_STATE_FORWARDING)
		promisc = false;

	if (!port_priv->nr)
		mask = ADIN1110_FWD_UNK2HOST;
	else
		mask = ADIN2111_P2_FWD_UNK2HOST;

	return adin1110_set_bits(priv, ADIN1110_CONFIG2,
				 mask, promisc ? mask : 0);
}

static int adin1110_setup_rx_mode(struct adin1110_port_priv *port_priv)
{
	int ret;

	ret = adin1110_set_promisc_mode(port_priv,
					!!(port_priv->flags & IFF_PROMISC));
	if (ret < 0)
		return ret;

	ret = adin1110_multicast_filter(port_priv, ADIN_MAC_MULTICAST_ADDR_SLOT,
					!!(port_priv->flags & IFF_ALLMULTI));
	if (ret < 0)
		return ret;

	ret = adin1110_broadcasts_filter(port_priv,
					 ADIN_MAC_BROADCAST_ADDR_SLOT,
					 !!(port_priv->flags & IFF_BROADCAST));
	if (ret < 0)
		return ret;

	return adin1110_set_bits(port_priv->priv, ADIN1110_CONFIG1,
				 ADIN1110_CONFIG1_SYNC, ADIN1110_CONFIG1_SYNC);
}

static bool adin1110_can_offload_forwarding(struct adin1110_priv *priv)
{
	int i;

	if (priv->cfg->id != ADIN2111_MAC)
		return false;

	/* Can't enable forwarding if ports do not belong to the same bridge */
	if (priv->ports[0]->bridge != priv->ports[1]->bridge || !priv->ports[0]->bridge)
		return false;

	/* Can't enable forwarding if there is a port
	 * that has been blocked by STP.
	 */
	for (i = 0; i < priv->cfg->ports_nr; i++) {
		if (priv->ports[i]->state != BR_STATE_FORWARDING)
			return false;
	}

	return true;
}

static void adin1110_rx_mode_work(struct work_struct *work)
{
	struct adin1110_port_priv *port_priv;
	struct adin1110_priv *priv;

	port_priv = container_of(work, struct adin1110_port_priv, rx_mode_work);
	priv = port_priv->priv;

	mutex_lock(&priv->lock);
	adin1110_setup_rx_mode(port_priv);
	mutex_unlock(&priv->lock);
}

static void adin1110_set_rx_mode(struct net_device *dev)
{
	struct adin1110_port_priv *port_priv = netdev_priv(dev);
	struct adin1110_priv *priv = port_priv->priv;

	spin_lock(&priv->state_lock);

	port_priv->flags = dev->flags;
	schedule_work(&port_priv->rx_mode_work);

	spin_unlock(&priv->state_lock);
}

static int adin1110_net_open(struct net_device *net_dev)
{
	struct adin1110_port_priv *port_priv = netdev_priv(net_dev);
	struct adin1110_priv *priv = port_priv->priv;
	u32 val;
	int ret;

	mutex_lock(&priv->lock);

	/* Configure MAC to compute and append the FCS itself. */
	ret = adin1110_write_reg(priv, ADIN1110_CONFIG2, ADIN1110_CRC_APPEND);
	if (ret < 0)
		goto out;

	val = ADIN1110_TX_RDY_IRQ | ADIN1110_RX_RDY_IRQ | ADIN1110_SPI_ERR_IRQ;
	if (priv->cfg->id == ADIN2111_MAC)
		val |= ADIN2111_RX_RDY_IRQ;

	priv->irq_mask = val;
	ret = adin1110_write_reg(priv, ADIN1110_IMASK1, ~val);
	if (ret < 0) {
		netdev_err(net_dev, "Failed to enable chip IRQs: %d\n", ret);
		goto out;
	}

	ret = adin1110_read_reg(priv, ADIN1110_TX_SPACE, &val);
	if (ret < 0) {
		netdev_err(net_dev, "Failed to read TX FIFO space: %d\n", ret);
		goto out;
	}

	priv->tx_space = 2 * val;

	port_priv->state = BR_STATE_FORWARDING;
	ret = adin1110_set_mac_address(net_dev, net_dev->dev_addr);
	if (ret < 0) {
		netdev_err(net_dev, "Could not set MAC address: %pM, %d\n",
			   net_dev->dev_addr, ret);
		goto out;
	}

	ret = adin1110_set_bits(priv, ADIN1110_CONFIG1, ADIN1110_CONFIG1_SYNC,
				ADIN1110_CONFIG1_SYNC);

out:
	mutex_unlock(&priv->lock);

	if (ret < 0)
		return ret;

	phy_start(port_priv->phydev);

	netif_start_queue(net_dev);

	return 0;
}

static int adin1110_net_stop(struct net_device *net_dev)
{
	struct adin1110_port_priv *port_priv = netdev_priv(net_dev);
	struct adin1110_priv *priv = port_priv->priv;
	u32 mask;
	int ret;

	mask = !port_priv->nr ? ADIN2111_RX_RDY_IRQ : ADIN1110_RX_RDY_IRQ;

	/* Disable RX RDY IRQs */
	mutex_lock(&priv->lock);
	ret = adin1110_set_bits(priv, ADIN1110_IMASK1, mask, mask);
	mutex_unlock(&priv->lock);
	if (ret < 0)
		return ret;

	netif_stop_queue(port_priv->netdev);
	flush_work(&port_priv->tx_work);
	phy_stop(port_priv->phydev);

	return 0;
}

static void adin1110_tx_work(struct work_struct *work)
{
	struct adin1110_port_priv *port_priv;
	struct adin1110_priv *priv;
	struct sk_buff *txb;
	int ret;

	port_priv = container_of(work, struct adin1110_port_priv, tx_work);
	priv = port_priv->priv;

	mutex_lock(&priv->lock);

	while ((txb = skb_dequeue(&port_priv->txq))) {
		ret = adin1110_write_fifo(port_priv, txb);
		if (ret < 0)
			dev_err_ratelimited(&priv->spidev->dev,
					    "Frame write error: %d\n", ret);

		dev_kfree_skb(txb);
	}

	mutex_unlock(&priv->lock);
}

static netdev_tx_t adin1110_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct adin1110_port_priv *port_priv = netdev_priv(dev);
	struct adin1110_priv *priv = port_priv->priv;
	netdev_tx_t netdev_ret = NETDEV_TX_OK;
	u32 tx_space_needed;

	tx_space_needed = skb->len + ADIN1110_FRAME_HEADER_LEN + ADIN1110_INTERNAL_SIZE_HEADER_LEN;
	if (tx_space_needed > priv->tx_space) {
		netif_stop_queue(dev);
		netdev_ret = NETDEV_TX_BUSY;
	} else {
		priv->tx_space -= tx_space_needed;
		skb_queue_tail(&port_priv->txq, skb);
	}

	schedule_work(&port_priv->tx_work);

	return netdev_ret;
}

static void adin1110_ndo_get_stats64(struct net_device *dev,
				     struct rtnl_link_stats64 *storage)
{
	struct adin1110_port_priv *port_priv = netdev_priv(dev);

	storage->rx_packets = port_priv->rx_packets;
	storage->tx_packets = port_priv->tx_packets;

	storage->rx_bytes = port_priv->rx_bytes;
	storage->tx_bytes = port_priv->tx_bytes;
}

static int adin1110_port_get_port_parent_id(struct net_device *dev,
					    struct netdev_phys_item_id *ppid)
{
	struct adin1110_port_priv *port_priv = netdev_priv(dev);
	struct adin1110_priv *priv = port_priv->priv;

	ppid->id_len = strnlen(priv->mii_bus_name, MAX_PHYS_ITEM_ID_LEN);
	memcpy(ppid->id, priv->mii_bus_name, ppid->id_len);

	return 0;
}

static int adin1110_ndo_get_phys_port_name(struct net_device *dev,
					   char *name, size_t len)
{
	struct adin1110_port_priv *port_priv = netdev_priv(dev);
	int err;

	err = snprintf(name, len, "p%d", port_priv->nr);
	if (err >= len)
		return -EINVAL;

	return 0;
}

static const struct net_device_ops adin1110_netdev_ops = {
	.ndo_open		= adin1110_net_open,
	.ndo_stop		= adin1110_net_stop,
	.ndo_eth_ioctl		= adin1110_ioctl,
	.ndo_start_xmit		= adin1110_start_xmit,
	.ndo_set_mac_address	= adin1110_ndo_set_mac_address,
	.ndo_set_rx_mode	= adin1110_set_rx_mode,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_get_stats64	= adin1110_ndo_get_stats64,
	.ndo_get_port_parent_id	= adin1110_port_get_port_parent_id,
	.ndo_get_phys_port_name	= adin1110_ndo_get_phys_port_name,
};

static void adin1110_get_drvinfo(struct net_device *dev,
				 struct ethtool_drvinfo *di)
{
	strscpy(di->driver, "ADIN1110", sizeof(di->driver));
	strscpy(di->bus_info, dev_name(dev->dev.parent), sizeof(di->bus_info));
}

static const struct ethtool_ops adin1110_ethtool_ops = {
	.get_drvinfo		= adin1110_get_drvinfo,
	.get_link		= ethtool_op_get_link,
	.get_link_ksettings	= phy_ethtool_get_link_ksettings,
	.set_link_ksettings	= phy_ethtool_set_link_ksettings,
};

static void adin1110_adjust_link(struct net_device *dev)
{
	struct phy_device *phydev = dev->phydev;

	if (!phydev->link)
		phy_print_status(phydev);
}

/* PHY ID is stored in the MAC registers too,
 * check spi connection by reading it.
 */
static int adin1110_check_spi(struct adin1110_priv *priv)
{
	int ret;
	u32 val;

	ret = adin1110_read_reg(priv, ADIN1110_PHY_ID, &val);
	if (ret < 0)
		return ret;

	if (val != priv->cfg->phy_id_val) {
		dev_err(&priv->spidev->dev, "PHY ID expected: %x, read: %x\n",
			priv->cfg->phy_id_val, val);
		return -EIO;
	}

	return 0;
}

static int adin1110_hw_forwarding(struct adin1110_priv *priv, bool enable)
{
	int ret;
	int i;

	priv->forwarding = enable;

	if (!priv->forwarding) {
		for (i = ADIN_MAC_FDB_ADDR_SLOT; i < ADIN_MAC_MAX_ADDR_SLOTS; i++) {
			ret = adin1110_clear_mac_address(priv, i);
			if (ret < 0)
				return ret;
		}
	}

	/* Forwarding is optimised when MAC runs in Cut Through mode. */
	ret = adin1110_set_bits(priv, ADIN1110_CONFIG2,
				ADIN2111_PORT_CUT_THRU_EN,
				priv->forwarding ? ADIN2111_PORT_CUT_THRU_EN : 0);
	if (ret < 0)
		return ret;

	for (i = 0; i < priv->cfg->ports_nr; i++) {
		ret = adin1110_setup_rx_mode(priv->ports[i]);
		if (ret < 0)
			return ret;
	}

	return ret;
}

static int adin1110_port_bridge_join(struct adin1110_port_priv *port_priv,
				     struct net_device *bridge)
{
	struct adin1110_priv *priv = port_priv->priv;
	int ret;

	port_priv->bridge = bridge;

	if (adin1110_can_offload_forwarding(priv)) {
		mutex_lock(&priv->lock);
		ret = adin1110_hw_forwarding(priv, true);
		mutex_unlock(&priv->lock);

		if (ret < 0)
			return ret;
	}

	return adin1110_set_mac_address(port_priv->netdev, bridge->dev_addr);
}

static int adin1110_port_bridge_leave(struct adin1110_port_priv *port_priv,
				      struct net_device *bridge)
{
	struct adin1110_priv *priv = port_priv->priv;
	int ret;

	port_priv->bridge = NULL;

	mutex_lock(&priv->lock);
	ret = adin1110_hw_forwarding(priv, false);
	mutex_unlock(&priv->lock);

	return ret;
}

static bool adin1110_port_dev_check(const struct net_device *dev)
{
	return dev->netdev_ops == &adin1110_netdev_ops;
}

static int adin1110_netdevice_event(struct notifier_block *unused,
				    unsigned long event, void *ptr)
{
	struct net_device *dev = netdev_notifier_info_to_dev(ptr);
	struct adin1110_port_priv *port_priv = netdev_priv(dev);
	struct netdev_notifier_changeupper_info *info = ptr;
	int ret = 0;

	if (!adin1110_port_dev_check(dev))
		return NOTIFY_DONE;

	switch (event) {
	case NETDEV_CHANGEUPPER:
		if (netif_is_bridge_master(info->upper_dev)) {
			if (info->linking)
				ret = adin1110_port_bridge_join(port_priv, info->upper_dev);
			else
				ret = adin1110_port_bridge_leave(port_priv, info->upper_dev);
		}
		break;
	default:
		break;
	}

	return notifier_from_errno(ret);
}

static struct notifier_block adin1110_netdevice_nb = {
	.notifier_call = adin1110_netdevice_event,
};

static void adin1110_disconnect_phy(void *data)
{
	phy_disconnect(data);
}

static int adin1110_port_set_forwarding_state(struct adin1110_port_priv *port_priv)
{
	struct adin1110_priv *priv = port_priv->priv;
	int ret;

	port_priv->state = BR_STATE_FORWARDING;

	mutex_lock(&priv->lock);
	ret = adin1110_set_mac_address(port_priv->netdev,
				       port_priv->netdev->dev_addr);
	if (ret < 0)
		goto out;

	if (adin1110_can_offload_forwarding(priv))
		ret = adin1110_hw_forwarding(priv, true);
	else
		ret = adin1110_setup_rx_mode(port_priv);
out:
	mutex_unlock(&priv->lock);

	return ret;
}

static int adin1110_port_set_blocking_state(struct adin1110_port_priv *port_priv)
{
	u8 mac[ETH_ALEN] = {0x01, 0x80, 0xC2, 0x00, 0x00, 0x00};
	struct adin1110_priv *priv = port_priv->priv;
	u8 mask[ETH_ALEN];
	u32 port_rules;
	int mac_slot;
	int ret;

	port_priv->state = BR_STATE_BLOCKING;

	mutex_lock(&priv->lock);

	mac_slot = (!port_priv->nr) ?  ADIN_MAC_P1_ADDR_SLOT : ADIN_MAC_P2_ADDR_SLOT;
	ret = adin1110_clear_mac_address(priv, mac_slot);
	if (ret < 0)
		goto out;

	ret = adin1110_hw_forwarding(priv, false);
	if (ret < 0)
		goto out;

	/* Allow only BPDUs to be passed to the CPU */
	memset(mask, 0xFF, ETH_ALEN);
	port_rules = adin1110_port_rules(port_priv, true, false);
	ret = adin1110_write_mac_address(port_priv, mac_slot, mac,
					 mask, port_rules);
out:
	mutex_unlock(&priv->lock);

	return ret;
}

/* ADIN1110/2111 does not have any native STP support.
 * Listen for bridge core state changes and
 * allow all frames to pass or only the BPDUs.
 */
static int adin1110_port_attr_stp_state_set(struct adin1110_port_priv *port_priv,
					    u8 state)
{
	switch (state) {
	case BR_STATE_FORWARDING:
		return adin1110_port_set_forwarding_state(port_priv);
	case BR_STATE_LEARNING:
	case BR_STATE_LISTENING:
	case BR_STATE_DISABLED:
	case BR_STATE_BLOCKING:
		return adin1110_port_set_blocking_state(port_priv);
	default:
		return -EINVAL;
	}
}

static int adin1110_port_attr_set(struct net_device *dev, const void *ctx,
				  const struct switchdev_attr *attr,
				  struct netlink_ext_ack *extack)
{
	struct adin1110_port_priv *port_priv = netdev_priv(dev);

	switch (attr->id) {
	case SWITCHDEV_ATTR_ID_PORT_STP_STATE:
		return adin1110_port_attr_stp_state_set(port_priv,
							attr->u.stp_state);
	default:
		return -EOPNOTSUPP;
	}
}

static int adin1110_switchdev_blocking_event(struct notifier_block *unused,
					     unsigned long event,
					     void *ptr)
{
	struct net_device *netdev = switchdev_notifier_info_to_dev(ptr);
	int ret;

	if (event == SWITCHDEV_PORT_ATTR_SET) {
		ret = switchdev_handle_port_attr_set(netdev, ptr,
						     adin1110_port_dev_check,
						     adin1110_port_attr_set);

		return notifier_from_errno(ret);
	}

	return NOTIFY_DONE;
}

static struct notifier_block adin1110_switchdev_blocking_notifier = {
	.notifier_call = adin1110_switchdev_blocking_event,
};

static void adin1110_fdb_offload_notify(struct net_device *netdev,
					struct switchdev_notifier_fdb_info *rcv)
{
	struct switchdev_notifier_fdb_info info = {};

	info.addr = rcv->addr;
	info.vid = rcv->vid;
	info.offloaded = true;
	call_switchdev_notifiers(SWITCHDEV_FDB_OFFLOADED,
				 netdev, &info.info, NULL);
}

static int adin1110_fdb_add(struct adin1110_port_priv *port_priv,
			    struct switchdev_notifier_fdb_info *fdb)
{
	struct adin1110_priv *priv = port_priv->priv;
	struct adin1110_port_priv *other_port;
	u8 mask[ETH_ALEN];
	u32 port_rules;
	int mac_nr;
	u32 val;
	int ret;

	netdev_dbg(port_priv->netdev,
		   "DEBUG: %s: MACID = %pM vid = %u flags = %u %u -- port %d\n",
		    __func__, fdb->addr, fdb->vid, fdb->added_by_user,
		    fdb->offloaded, port_priv->nr);

	if (!priv->forwarding)
		return 0;

	if (fdb->is_local)
		return -EINVAL;

	/* Find free FDB slot on device. */
	for (mac_nr = ADIN_MAC_FDB_ADDR_SLOT; mac_nr < ADIN_MAC_MAX_ADDR_SLOTS; mac_nr++) {
		ret = adin1110_read_reg(priv, ADIN1110_MAC_ADDR_FILTER_UPR + (mac_nr * 2), &val);
		if (ret < 0)
			return ret;
		if (!val)
			break;
	}

	if (mac_nr == ADIN_MAC_MAX_ADDR_SLOTS)
		return -ENOMEM;

	other_port = priv->ports[!port_priv->nr];
	port_rules = adin1110_port_rules(port_priv, false, true);
	memset(mask, 0xFF, ETH_ALEN);

	return adin1110_write_mac_address(other_port, mac_nr, (u8 *)fdb->addr,
					  mask, port_rules);
}

static int adin1110_read_mac(struct adin1110_priv *priv, int mac_nr, u8 *addr)
{
	u32 val;
	int ret;

	ret = adin1110_read_reg(priv, ADIN1110_MAC_ADDR_FILTER_UPR + (mac_nr * 2), &val);
	if (ret < 0)
		return ret;

	put_unaligned_be16(val, addr);

	ret = adin1110_read_reg(priv, ADIN1110_MAC_ADDR_FILTER_LWR + (mac_nr * 2), &val);
	if (ret < 0)
		return ret;

	put_unaligned_be32(val, addr + 2);

	return 0;
}

static int adin1110_fdb_del(struct adin1110_port_priv *port_priv,
			    struct switchdev_notifier_fdb_info *fdb)
{
	struct adin1110_priv *priv = port_priv->priv;
	u8 addr[ETH_ALEN];
	int mac_nr;
	int ret;

	netdev_dbg(port_priv->netdev,
		   "DEBUG: %s: MACID = %pM vid = %u flags = %u %u -- port %d\n",
		   __func__, fdb->addr, fdb->vid, fdb->added_by_user,
		   fdb->offloaded, port_priv->nr);

	if (fdb->is_local)
		return -EINVAL;

	for (mac_nr = ADIN_MAC_FDB_ADDR_SLOT; mac_nr < ADIN_MAC_MAX_ADDR_SLOTS; mac_nr++) {
		ret = adin1110_read_mac(priv, mac_nr, addr);
		if (ret < 0)
			return ret;

		if (ether_addr_equal(addr, fdb->addr)) {
			ret = adin1110_clear_mac_address(priv, mac_nr);
			if (ret < 0)
				return ret;
		}
	}

	return 0;
}

static void adin1110_switchdev_event_work(struct work_struct *work)
{
	struct adin1110_switchdev_event_work *switchdev_work;
	struct adin1110_port_priv *port_priv;
	int ret;

	switchdev_work = container_of(work, struct adin1110_switchdev_event_work, work);
	port_priv = switchdev_work->port_priv;

	mutex_lock(&port_priv->priv->lock);

	switch (switchdev_work->event) {
	case SWITCHDEV_FDB_ADD_TO_DEVICE:
		ret = adin1110_fdb_add(port_priv, &switchdev_work->fdb_info);
		if (!ret)
			adin1110_fdb_offload_notify(port_priv->netdev,
						    &switchdev_work->fdb_info);
		break;
	case SWITCHDEV_FDB_DEL_TO_DEVICE:
		adin1110_fdb_del(port_priv, &switchdev_work->fdb_info);
		break;
	default:
		break;
	}

	mutex_unlock(&port_priv->priv->lock);

	kfree(switchdev_work->fdb_info.addr);
	kfree(switchdev_work);
	dev_put(port_priv->netdev);
}

/* called under rcu_read_lock() */
static int adin1110_switchdev_event(struct notifier_block *unused,
				    unsigned long event, void *ptr)
{
	struct net_device *netdev = switchdev_notifier_info_to_dev(ptr);
	struct adin1110_port_priv *port_priv = netdev_priv(netdev);
	struct adin1110_switchdev_event_work *switchdev_work;
	struct switchdev_notifier_fdb_info *fdb_info = ptr;

	if (!adin1110_port_dev_check(netdev))
		return NOTIFY_DONE;

	switchdev_work = kzalloc(sizeof(*switchdev_work), GFP_ATOMIC);
	if (WARN_ON(!switchdev_work))
		return NOTIFY_BAD;

	INIT_WORK(&switchdev_work->work, adin1110_switchdev_event_work);
	switchdev_work->port_priv = port_priv;
	switchdev_work->event = event;

	switch (event) {
	case SWITCHDEV_FDB_ADD_TO_DEVICE:
	case SWITCHDEV_FDB_DEL_TO_DEVICE:
		memcpy(&switchdev_work->fdb_info, ptr,
		       sizeof(switchdev_work->fdb_info));
		switchdev_work->fdb_info.addr = kzalloc(ETH_ALEN, GFP_ATOMIC);

		if (!switchdev_work->fdb_info.addr)
			goto err_addr_alloc;

		ether_addr_copy((u8 *)switchdev_work->fdb_info.addr,
				fdb_info->addr);
		dev_hold(netdev);
		break;
	default:
		kfree(switchdev_work);
		return NOTIFY_DONE;
	}

	queue_work(system_long_wq, &switchdev_work->work);

	return NOTIFY_DONE;

err_addr_alloc:
	kfree(switchdev_work);
	return NOTIFY_BAD;
}

static struct notifier_block adin1110_switchdev_notifier = {
	.notifier_call = adin1110_switchdev_event,
};

static void adin1110_unregister_notifiers(void *data)
{
	unregister_switchdev_blocking_notifier(&adin1110_switchdev_blocking_notifier);
	unregister_switchdev_notifier(&adin1110_switchdev_notifier);
	unregister_netdevice_notifier(&adin1110_netdevice_nb);
}

static int adin1110_setup_notifiers(struct adin1110_priv *priv)
{
	struct device *dev = &priv->spidev->dev;
	int ret;

	ret = register_netdevice_notifier(&adin1110_netdevice_nb);
	if (ret < 0)
		return ret;

	ret = register_switchdev_notifier(&adin1110_switchdev_notifier);
	if (ret < 0)
		goto err_netdev;

	ret = register_switchdev_blocking_notifier(&adin1110_switchdev_blocking_notifier);
	if (ret < 0)
		goto err_sdev;

	return devm_add_action_or_reset(dev, adin1110_unregister_notifiers, NULL);

err_sdev:
	unregister_switchdev_notifier(&adin1110_switchdev_notifier);

err_netdev:
	unregister_netdevice_notifier(&adin1110_netdevice_nb);
	return ret;
}

static int adin1110_probe_netdevs(struct adin1110_priv *priv)
{
	struct device *dev = &priv->spidev->dev;
	struct adin1110_port_priv *port_priv;
	struct net_device *netdev;
	int ret;
	int i;

	for (i = 0; i < priv->cfg->ports_nr; i++) {
		netdev = devm_alloc_etherdev(dev, sizeof(*port_priv));
		if (!netdev)
			return -ENOMEM;

		port_priv = netdev_priv(netdev);
		port_priv->netdev = netdev;
		port_priv->priv = priv;
		port_priv->cfg = priv->cfg;
		port_priv->nr = i;
		priv->ports[i] = port_priv;
		SET_NETDEV_DEV(netdev, dev);

		ret = device_get_ethdev_address(dev, netdev);
		if (ret < 0)
			return ret;

		netdev->irq = priv->spidev->irq;
		INIT_WORK(&port_priv->tx_work, adin1110_tx_work);
		INIT_WORK(&port_priv->rx_mode_work, adin1110_rx_mode_work);
		skb_queue_head_init(&port_priv->txq);

		netif_carrier_off(netdev);

		netdev->if_port = IF_PORT_10BASET;
		netdev->netdev_ops = &adin1110_netdev_ops;
		netdev->ethtool_ops = &adin1110_ethtool_ops;
		netdev->priv_flags |= IFF_UNICAST_FLT;
		netdev->features |= NETIF_F_NETNS_LOCAL;

		port_priv->phydev = get_phy_device(priv->mii_bus, i + 1, false);
		if (IS_ERR(port_priv->phydev)) {
			netdev_err(netdev, "Could not find PHY with device address: %d.\n", i);
			return PTR_ERR(port_priv->phydev);
		}

		port_priv->phydev = phy_connect(netdev,
						phydev_name(port_priv->phydev),
						adin1110_adjust_link,
						PHY_INTERFACE_MODE_INTERNAL);
		if (IS_ERR(port_priv->phydev)) {
			netdev_err(netdev, "Could not connect PHY with device address: %d.\n", i);
			return PTR_ERR(port_priv->phydev);
		}

		ret = devm_add_action_or_reset(dev, adin1110_disconnect_phy,
					       port_priv->phydev);
		if (ret < 0)
			return ret;
	}

	/* ADIN1110 INT_N pin will be used to signal the host */
	ret = devm_request_threaded_irq(dev, priv->spidev->irq, NULL,
					adin1110_irq,
					IRQF_TRIGGER_LOW | IRQF_ONESHOT,
					dev_name(dev), priv);
	if (ret < 0)
		return ret;

	ret = adin1110_setup_notifiers(priv);
	if (ret < 0)
		return ret;

	for (i = 0; i < priv->cfg->ports_nr; i++) {
		ret = devm_register_netdev(dev, priv->ports[i]->netdev);
		if (ret < 0) {
			dev_err(dev, "Failed to register network device.\n");
			return ret;
		}
	}

	return 0;
}

static int adin1110_probe(struct spi_device *spi)
{
	const struct spi_device_id *dev_id = spi_get_device_id(spi);
	struct device *dev = &spi->dev;
	struct adin1110_priv *priv;
	int ret;

	priv = devm_kzalloc(dev, sizeof(struct adin1110_priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->spidev = spi;
	priv->cfg = &adin1110_cfgs[dev_id->driver_data];
	spi->bits_per_word = 8;
	spi->mode = SPI_MODE_0;

	mutex_init(&priv->lock);
	spin_lock_init(&priv->state_lock);

	/* use of CRC on control and data transactions is pin dependent */
	priv->append_crc = device_property_read_bool(dev, "adi,spi-crc");
	if (priv->append_crc)
		crc8_populate_msb(adin1110_crc_table, 0x7);

	ret = adin1110_check_spi(priv);
	if (ret < 0) {
		dev_err(dev, "Probe SPI Read check failed: %d\n", ret);
		return ret;
	}

	ret = adin1110_write_reg(priv, ADIN1110_RESET, ADIN1110_SWRESET);
	if (ret < 0)
		return ret;

	ret = adin1110_register_mdiobus(priv, dev);
	if (ret < 0) {
		dev_err(dev, "Could not register MDIO bus %d\n", ret);
		return ret;
	}

	return adin1110_probe_netdevs(priv);
}

static const struct of_device_id adin1110_match_table[] = {
	{ .compatible = "adi,adin1110" },
	{ .compatible = "adi,adin2111" },
	{ }
};
MODULE_DEVICE_TABLE(of, adin1110_match_table);

static const struct spi_device_id adin1110_spi_id[] = {
	{ .name = "adin1110", .driver_data = ADIN1110_MAC },
	{ .name = "adin2111", .driver_data = ADIN2111_MAC },
	{ }
};
MODULE_DEVICE_TABLE(spi, adin1110_spi_id);

static struct spi_driver adin1110_driver = {
	.driver = {
		.name = "adin1110",
		.of_match_table = adin1110_match_table,
	},
	.probe = adin1110_probe,
	.id_table = adin1110_spi_id,
};
module_spi_driver(adin1110_driver);

MODULE_DESCRIPTION("ADIN1110 Network driver");
MODULE_AUTHOR("Alexandru Tachici <alexandru.tachici@analog.com>");
MODULE_LICENSE("Dual BSD/GPL");
