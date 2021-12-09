// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * LiteX Liteeth Ethernet
 *
 * Copyright 2017 Joel Stanley <joel@jms.id.au>
 *
 */

#include <linux/etherdevice.h>
#include <linux/interrupt.h>
#include <linux/litex.h>
#include <linux/module.h>
#include <linux/of_net.h>
#include <linux/platform_device.h>

#define LITEETH_WRITER_SLOT       0x00
#define LITEETH_WRITER_LENGTH     0x04
#define LITEETH_WRITER_ERRORS     0x08
#define LITEETH_WRITER_EV_STATUS  0x0C
#define LITEETH_WRITER_EV_PENDING 0x10
#define LITEETH_WRITER_EV_ENABLE  0x14
#define LITEETH_READER_START      0x18
#define LITEETH_READER_READY      0x1C
#define LITEETH_READER_LEVEL      0x20
#define LITEETH_READER_SLOT       0x24
#define LITEETH_READER_LENGTH     0x28
#define LITEETH_READER_EV_STATUS  0x2C
#define LITEETH_READER_EV_PENDING 0x30
#define LITEETH_READER_EV_ENABLE  0x34
#define LITEETH_PREAMBLE_CRC      0x38
#define LITEETH_PREAMBLE_ERRORS   0x3C
#define LITEETH_CRC_ERRORS        0x40

#define LITEETH_PHY_CRG_RESET     0x00
#define LITEETH_MDIO_W            0x04
#define LITEETH_MDIO_R            0x0C

#define DRV_NAME	"liteeth"

struct liteeth {
	void __iomem *base;
	struct net_device *netdev;
	struct device *dev;
	u32 slot_size;

	/* Tx */
	u32 tx_slot;
	u32 num_tx_slots;
	void __iomem *tx_base;

	/* Rx */
	u32 rx_slot;
	u32 num_rx_slots;
	void __iomem *rx_base;
};

static int liteeth_rx(struct net_device *netdev)
{
	struct liteeth *priv = netdev_priv(netdev);
	struct sk_buff *skb;
	unsigned char *data;
	u8 rx_slot;
	int len;

	rx_slot = litex_read8(priv->base + LITEETH_WRITER_SLOT);
	len = litex_read32(priv->base + LITEETH_WRITER_LENGTH);

	if (len == 0 || len > 2048)
		goto rx_drop;

	skb = netdev_alloc_skb_ip_align(netdev, len);
	if (!skb) {
		netdev_err(netdev, "couldn't get memory\n");
		goto rx_drop;
	}

	data = skb_put(skb, len);
	memcpy_fromio(data, priv->rx_base + rx_slot * priv->slot_size, len);
	skb->protocol = eth_type_trans(skb, netdev);

	netdev->stats.rx_packets++;
	netdev->stats.rx_bytes += len;

	return netif_rx(skb);

rx_drop:
	netdev->stats.rx_dropped++;
	netdev->stats.rx_errors++;

	return NET_RX_DROP;
}

static irqreturn_t liteeth_interrupt(int irq, void *dev_id)
{
	struct net_device *netdev = dev_id;
	struct liteeth *priv = netdev_priv(netdev);
	u8 reg;

	reg = litex_read8(priv->base + LITEETH_READER_EV_PENDING);
	if (reg) {
		if (netif_queue_stopped(netdev))
			netif_wake_queue(netdev);
		litex_write8(priv->base + LITEETH_READER_EV_PENDING, reg);
	}

	reg = litex_read8(priv->base + LITEETH_WRITER_EV_PENDING);
	if (reg) {
		liteeth_rx(netdev);
		litex_write8(priv->base + LITEETH_WRITER_EV_PENDING, reg);
	}

	return IRQ_HANDLED;
}

static int liteeth_open(struct net_device *netdev)
{
	struct liteeth *priv = netdev_priv(netdev);
	int err;

	/* Clear pending events */
	litex_write8(priv->base + LITEETH_WRITER_EV_PENDING, 1);
	litex_write8(priv->base + LITEETH_READER_EV_PENDING, 1);

	err = request_irq(netdev->irq, liteeth_interrupt, 0, netdev->name, netdev);
	if (err) {
		netdev_err(netdev, "failed to request irq %d\n", netdev->irq);
		return err;
	}

	/* Enable IRQs */
	litex_write8(priv->base + LITEETH_WRITER_EV_ENABLE, 1);
	litex_write8(priv->base + LITEETH_READER_EV_ENABLE, 1);

	netif_carrier_on(netdev);
	netif_start_queue(netdev);

	return 0;
}

static int liteeth_stop(struct net_device *netdev)
{
	struct liteeth *priv = netdev_priv(netdev);

	netif_stop_queue(netdev);
	netif_carrier_off(netdev);

	litex_write8(priv->base + LITEETH_WRITER_EV_ENABLE, 0);
	litex_write8(priv->base + LITEETH_READER_EV_ENABLE, 0);

	free_irq(netdev->irq, netdev);

	return 0;
}

static int liteeth_start_xmit(struct sk_buff *skb, struct net_device *netdev)
{
	struct liteeth *priv = netdev_priv(netdev);
	void __iomem *txbuffer;

	if (!litex_read8(priv->base + LITEETH_READER_READY)) {
		if (net_ratelimit())
			netdev_err(netdev, "LITEETH_READER_READY not ready\n");

		netif_stop_queue(netdev);

		return NETDEV_TX_BUSY;
	}

	/* Reject oversize packets */
	if (unlikely(skb->len > priv->slot_size)) {
		if (net_ratelimit())
			netdev_err(netdev, "tx packet too big\n");

		dev_kfree_skb_any(skb);
		netdev->stats.tx_dropped++;
		netdev->stats.tx_errors++;

		return NETDEV_TX_OK;
	}

	txbuffer = priv->tx_base + priv->tx_slot * priv->slot_size;
	memcpy_toio(txbuffer, skb->data, skb->len);
	litex_write8(priv->base + LITEETH_READER_SLOT, priv->tx_slot);
	litex_write16(priv->base + LITEETH_READER_LENGTH, skb->len);
	litex_write8(priv->base + LITEETH_READER_START, 1);

	netdev->stats.tx_bytes += skb->len;
	netdev->stats.tx_packets++;

	priv->tx_slot = (priv->tx_slot + 1) % priv->num_tx_slots;
	dev_kfree_skb_any(skb);

	return NETDEV_TX_OK;
}

static const struct net_device_ops liteeth_netdev_ops = {
	.ndo_open		= liteeth_open,
	.ndo_stop		= liteeth_stop,
	.ndo_start_xmit         = liteeth_start_xmit,
};

static void liteeth_setup_slots(struct liteeth *priv)
{
	struct device_node *np = priv->dev->of_node;
	int err;

	err = of_property_read_u32(np, "litex,rx-slots", &priv->num_rx_slots);
	if (err) {
		dev_dbg(priv->dev, "unable to get litex,rx-slots, using 2\n");
		priv->num_rx_slots = 2;
	}

	err = of_property_read_u32(np, "litex,tx-slots", &priv->num_tx_slots);
	if (err) {
		dev_dbg(priv->dev, "unable to get litex,tx-slots, using 2\n");
		priv->num_tx_slots = 2;
	}

	err = of_property_read_u32(np, "litex,slot-size", &priv->slot_size);
	if (err) {
		dev_dbg(priv->dev, "unable to get litex,slot-size, using 0x800\n");
		priv->slot_size = 0x800;
	}
}

static int liteeth_probe(struct platform_device *pdev)
{
	struct net_device *netdev;
	void __iomem *buf_base;
	struct liteeth *priv;
	int irq, err;

	netdev = devm_alloc_etherdev(&pdev->dev, sizeof(*priv));
	if (!netdev)
		return -ENOMEM;

	SET_NETDEV_DEV(netdev, &pdev->dev);
	platform_set_drvdata(pdev, netdev);

	priv = netdev_priv(netdev);
	priv->netdev = netdev;
	priv->dev = &pdev->dev;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;
	netdev->irq = irq;

	priv->base = devm_platform_ioremap_resource_byname(pdev, "mac");
	if (IS_ERR(priv->base))
		return PTR_ERR(priv->base);

	buf_base = devm_platform_ioremap_resource_byname(pdev, "buffer");
	if (IS_ERR(buf_base))
		return PTR_ERR(buf_base);

	liteeth_setup_slots(priv);

	/* Rx slots */
	priv->rx_base = buf_base;
	priv->rx_slot = 0;

	/* Tx slots come after Rx slots */
	priv->tx_base = buf_base + priv->num_rx_slots * priv->slot_size;
	priv->tx_slot = 0;

	err = of_get_ethdev_address(pdev->dev.of_node, netdev);
	if (err)
		eth_hw_addr_random(netdev);

	netdev->netdev_ops = &liteeth_netdev_ops;

	err = register_netdev(netdev);
	if (err) {
		dev_err(&pdev->dev, "Failed to register netdev %d\n", err);
		return err;
	}

	netdev_info(netdev, "irq %d slots: tx %d rx %d size %d\n",
		    netdev->irq, priv->num_tx_slots, priv->num_rx_slots, priv->slot_size);

	return 0;
}

static int liteeth_remove(struct platform_device *pdev)
{
	struct net_device *netdev = platform_get_drvdata(pdev);

	unregister_netdev(netdev);

	return 0;
}

static const struct of_device_id liteeth_of_match[] = {
	{ .compatible = "litex,liteeth" },
	{ }
};
MODULE_DEVICE_TABLE(of, liteeth_of_match);

static struct platform_driver liteeth_driver = {
	.probe = liteeth_probe,
	.remove = liteeth_remove,
	.driver = {
		.name = DRV_NAME,
		.of_match_table = liteeth_of_match,
	},
};
module_platform_driver(liteeth_driver);

MODULE_AUTHOR("Joel Stanley <joel@jms.id.au>");
MODULE_LICENSE("GPL");
