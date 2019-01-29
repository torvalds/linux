/*
 * Applied Micro X-Gene SoC Ethernet v2 Driver
 *
 * Copyright (c) 2017, Applied Micro Circuits Corporation
 * Author(s): Iyappan Subramanian <isubramanian@apm.com>
 *	      Keyur Chudgar <kchudgar@apm.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "main.h"

static const struct acpi_device_id xge_acpi_match[];

static int xge_get_resources(struct xge_pdata *pdata)
{
	struct platform_device *pdev;
	struct net_device *ndev;
	int phy_mode, ret = 0;
	struct resource *res;
	struct device *dev;

	pdev = pdata->pdev;
	dev = &pdev->dev;
	ndev = pdata->ndev;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(dev, "Resource enet_csr not defined\n");
		return -ENODEV;
	}

	pdata->resources.base_addr = devm_ioremap(dev, res->start,
						  resource_size(res));
	if (!pdata->resources.base_addr) {
		dev_err(dev, "Unable to retrieve ENET Port CSR region\n");
		return -ENOMEM;
	}

	if (!device_get_mac_address(dev, ndev->dev_addr, ETH_ALEN))
		eth_hw_addr_random(ndev);

	memcpy(ndev->perm_addr, ndev->dev_addr, ndev->addr_len);

	phy_mode = device_get_phy_mode(dev);
	if (phy_mode < 0) {
		dev_err(dev, "Unable to get phy-connection-type\n");
		return phy_mode;
	}
	pdata->resources.phy_mode = phy_mode;

	if (pdata->resources.phy_mode != PHY_INTERFACE_MODE_RGMII) {
		dev_err(dev, "Incorrect phy-connection-type specified\n");
		return -ENODEV;
	}

	ret = platform_get_irq(pdev, 0);
	if (ret < 0) {
		dev_err(dev, "Unable to get irq\n");
		return ret;
	}
	pdata->resources.irq = ret;

	return 0;
}

static int xge_refill_buffers(struct net_device *ndev, u32 nbuf)
{
	struct xge_pdata *pdata = netdev_priv(ndev);
	struct xge_desc_ring *ring = pdata->rx_ring;
	const u8 slots = XGENE_ENET_NUM_DESC - 1;
	struct device *dev = &pdata->pdev->dev;
	struct xge_raw_desc *raw_desc;
	u64 addr_lo, addr_hi;
	u8 tail = ring->tail;
	struct sk_buff *skb;
	dma_addr_t dma_addr;
	u16 len;
	int i;

	for (i = 0; i < nbuf; i++) {
		raw_desc = &ring->raw_desc[tail];

		len = XGENE_ENET_STD_MTU;
		skb = netdev_alloc_skb(ndev, len);
		if (unlikely(!skb))
			return -ENOMEM;

		dma_addr = dma_map_single(dev, skb->data, len, DMA_FROM_DEVICE);
		if (dma_mapping_error(dev, dma_addr)) {
			netdev_err(ndev, "DMA mapping error\n");
			dev_kfree_skb_any(skb);
			return -EINVAL;
		}

		ring->pkt_info[tail].skb = skb;
		ring->pkt_info[tail].dma_addr = dma_addr;

		addr_hi = GET_BITS(NEXT_DESC_ADDRH, le64_to_cpu(raw_desc->m1));
		addr_lo = GET_BITS(NEXT_DESC_ADDRL, le64_to_cpu(raw_desc->m1));
		raw_desc->m1 = cpu_to_le64(SET_BITS(NEXT_DESC_ADDRL, addr_lo) |
					   SET_BITS(NEXT_DESC_ADDRH, addr_hi) |
					   SET_BITS(PKT_ADDRH,
						    upper_32_bits(dma_addr)));

		dma_wmb();
		raw_desc->m0 = cpu_to_le64(SET_BITS(PKT_ADDRL, dma_addr) |
					   SET_BITS(E, 1));
		tail = (tail + 1) & slots;
	}

	ring->tail = tail;

	return 0;
}

static int xge_init_hw(struct net_device *ndev)
{
	struct xge_pdata *pdata = netdev_priv(ndev);
	int ret;

	ret = xge_port_reset(ndev);
	if (ret)
		return ret;

	xge_port_init(ndev);
	pdata->nbufs = NUM_BUFS;

	return 0;
}

static irqreturn_t xge_irq(const int irq, void *data)
{
	struct xge_pdata *pdata = data;

	if (napi_schedule_prep(&pdata->napi)) {
		xge_intr_disable(pdata);
		__napi_schedule(&pdata->napi);
	}

	return IRQ_HANDLED;
}

static int xge_request_irq(struct net_device *ndev)
{
	struct xge_pdata *pdata = netdev_priv(ndev);
	int ret;

	snprintf(pdata->irq_name, IRQ_ID_SIZE, "%s", ndev->name);

	ret = request_irq(pdata->resources.irq, xge_irq, 0, pdata->irq_name,
			  pdata);
	if (ret)
		netdev_err(ndev, "Failed to request irq %s\n", pdata->irq_name);

	return ret;
}

static void xge_free_irq(struct net_device *ndev)
{
	struct xge_pdata *pdata = netdev_priv(ndev);

	free_irq(pdata->resources.irq, pdata);
}

static bool is_tx_slot_available(struct xge_raw_desc *raw_desc)
{
	if (GET_BITS(E, le64_to_cpu(raw_desc->m0)) &&
	    (GET_BITS(PKT_SIZE, le64_to_cpu(raw_desc->m0)) == SLOT_EMPTY))
		return true;

	return false;
}

static netdev_tx_t xge_start_xmit(struct sk_buff *skb, struct net_device *ndev)
{
	struct xge_pdata *pdata = netdev_priv(ndev);
	struct device *dev = &pdata->pdev->dev;
	struct xge_desc_ring *tx_ring;
	struct xge_raw_desc *raw_desc;
	static dma_addr_t dma_addr;
	u64 addr_lo, addr_hi;
	void *pkt_buf;
	u8 tail;
	u16 len;

	tx_ring = pdata->tx_ring;
	tail = tx_ring->tail;
	len = skb_headlen(skb);
	raw_desc = &tx_ring->raw_desc[tail];

	if (!is_tx_slot_available(raw_desc)) {
		netif_stop_queue(ndev);
		return NETDEV_TX_BUSY;
	}

	/* Packet buffers should be 64B aligned */
	pkt_buf = dma_alloc_coherent(dev, XGENE_ENET_STD_MTU, &dma_addr,
				     GFP_ATOMIC);
	if (unlikely(!pkt_buf)) {
		dev_kfree_skb_any(skb);
		return NETDEV_TX_OK;
	}
	memcpy(pkt_buf, skb->data, len);

	addr_hi = GET_BITS(NEXT_DESC_ADDRH, le64_to_cpu(raw_desc->m1));
	addr_lo = GET_BITS(NEXT_DESC_ADDRL, le64_to_cpu(raw_desc->m1));
	raw_desc->m1 = cpu_to_le64(SET_BITS(NEXT_DESC_ADDRL, addr_lo) |
				   SET_BITS(NEXT_DESC_ADDRH, addr_hi) |
				   SET_BITS(PKT_ADDRH,
					    upper_32_bits(dma_addr)));

	tx_ring->pkt_info[tail].skb = skb;
	tx_ring->pkt_info[tail].dma_addr = dma_addr;
	tx_ring->pkt_info[tail].pkt_buf = pkt_buf;

	dma_wmb();

	raw_desc->m0 = cpu_to_le64(SET_BITS(PKT_ADDRL, dma_addr) |
				   SET_BITS(PKT_SIZE, len) |
				   SET_BITS(E, 0));
	skb_tx_timestamp(skb);
	xge_wr_csr(pdata, DMATXCTRL, 1);

	tx_ring->tail = (tail + 1) & (XGENE_ENET_NUM_DESC - 1);

	return NETDEV_TX_OK;
}

static bool is_tx_hw_done(struct xge_raw_desc *raw_desc)
{
	if (GET_BITS(E, le64_to_cpu(raw_desc->m0)) &&
	    !GET_BITS(PKT_SIZE, le64_to_cpu(raw_desc->m0)))
		return true;

	return false;
}

static void xge_txc_poll(struct net_device *ndev)
{
	struct xge_pdata *pdata = netdev_priv(ndev);
	struct device *dev = &pdata->pdev->dev;
	struct xge_desc_ring *tx_ring;
	struct xge_raw_desc *raw_desc;
	dma_addr_t dma_addr;
	struct sk_buff *skb;
	void *pkt_buf;
	u32 data;
	u8 head;

	tx_ring = pdata->tx_ring;
	head = tx_ring->head;

	data = xge_rd_csr(pdata, DMATXSTATUS);
	if (!GET_BITS(TXPKTCOUNT, data))
		return;

	while (1) {
		raw_desc = &tx_ring->raw_desc[head];

		if (!is_tx_hw_done(raw_desc))
			break;

		dma_rmb();

		skb = tx_ring->pkt_info[head].skb;
		dma_addr = tx_ring->pkt_info[head].dma_addr;
		pkt_buf = tx_ring->pkt_info[head].pkt_buf;
		pdata->stats.tx_packets++;
		pdata->stats.tx_bytes += skb->len;
		dma_free_coherent(dev, XGENE_ENET_STD_MTU, pkt_buf, dma_addr);
		dev_kfree_skb_any(skb);

		/* clear pktstart address and pktsize */
		raw_desc->m0 = cpu_to_le64(SET_BITS(E, 1) |
					   SET_BITS(PKT_SIZE, SLOT_EMPTY));
		xge_wr_csr(pdata, DMATXSTATUS, 1);

		head = (head + 1) & (XGENE_ENET_NUM_DESC - 1);
	}

	if (netif_queue_stopped(ndev))
		netif_wake_queue(ndev);

	tx_ring->head = head;
}

static int xge_rx_poll(struct net_device *ndev, unsigned int budget)
{
	struct xge_pdata *pdata = netdev_priv(ndev);
	struct device *dev = &pdata->pdev->dev;
	struct xge_desc_ring *rx_ring;
	struct xge_raw_desc *raw_desc;
	struct sk_buff *skb;
	dma_addr_t dma_addr;
	int processed = 0;
	u8 head, rx_error;
	int i, ret;
	u32 data;
	u16 len;

	rx_ring = pdata->rx_ring;
	head = rx_ring->head;

	data = xge_rd_csr(pdata, DMARXSTATUS);
	if (!GET_BITS(RXPKTCOUNT, data))
		return 0;

	for (i = 0; i < budget; i++) {
		raw_desc = &rx_ring->raw_desc[head];

		if (GET_BITS(E, le64_to_cpu(raw_desc->m0)))
			break;

		dma_rmb();

		skb = rx_ring->pkt_info[head].skb;
		rx_ring->pkt_info[head].skb = NULL;
		dma_addr = rx_ring->pkt_info[head].dma_addr;
		len = GET_BITS(PKT_SIZE, le64_to_cpu(raw_desc->m0));
		dma_unmap_single(dev, dma_addr, XGENE_ENET_STD_MTU,
				 DMA_FROM_DEVICE);

		rx_error = GET_BITS(D, le64_to_cpu(raw_desc->m2));
		if (unlikely(rx_error)) {
			pdata->stats.rx_errors++;
			dev_kfree_skb_any(skb);
			goto out;
		}

		skb_put(skb, len);
		skb->protocol = eth_type_trans(skb, ndev);

		pdata->stats.rx_packets++;
		pdata->stats.rx_bytes += len;
		napi_gro_receive(&pdata->napi, skb);
out:
		ret = xge_refill_buffers(ndev, 1);
		xge_wr_csr(pdata, DMARXSTATUS, 1);
		xge_wr_csr(pdata, DMARXCTRL, 1);

		if (ret)
			break;

		head = (head + 1) & (XGENE_ENET_NUM_DESC - 1);
		processed++;
	}

	rx_ring->head = head;

	return processed;
}

static void xge_delete_desc_ring(struct net_device *ndev,
				 struct xge_desc_ring *ring)
{
	struct xge_pdata *pdata = netdev_priv(ndev);
	struct device *dev = &pdata->pdev->dev;
	u16 size;

	if (!ring)
		return;

	size = XGENE_ENET_DESC_SIZE * XGENE_ENET_NUM_DESC;
	if (ring->desc_addr)
		dma_free_coherent(dev, size, ring->desc_addr, ring->dma_addr);

	kfree(ring->pkt_info);
	kfree(ring);
}

static void xge_free_buffers(struct net_device *ndev)
{
	struct xge_pdata *pdata = netdev_priv(ndev);
	struct xge_desc_ring *ring = pdata->rx_ring;
	struct device *dev = &pdata->pdev->dev;
	struct sk_buff *skb;
	dma_addr_t dma_addr;
	int i;

	for (i = 0; i < XGENE_ENET_NUM_DESC; i++) {
		skb = ring->pkt_info[i].skb;
		dma_addr = ring->pkt_info[i].dma_addr;

		if (!skb)
			continue;

		dma_unmap_single(dev, dma_addr, XGENE_ENET_STD_MTU,
				 DMA_FROM_DEVICE);
		dev_kfree_skb_any(skb);
	}
}

static void xge_delete_desc_rings(struct net_device *ndev)
{
	struct xge_pdata *pdata = netdev_priv(ndev);

	xge_txc_poll(ndev);
	xge_delete_desc_ring(ndev, pdata->tx_ring);

	xge_rx_poll(ndev, 64);
	xge_free_buffers(ndev);
	xge_delete_desc_ring(ndev, pdata->rx_ring);
}

static struct xge_desc_ring *xge_create_desc_ring(struct net_device *ndev)
{
	struct xge_pdata *pdata = netdev_priv(ndev);
	struct device *dev = &pdata->pdev->dev;
	struct xge_desc_ring *ring;
	u16 size;

	ring = kzalloc(sizeof(*ring), GFP_KERNEL);
	if (!ring)
		return NULL;

	ring->ndev = ndev;

	size = XGENE_ENET_DESC_SIZE * XGENE_ENET_NUM_DESC;
	ring->desc_addr = dma_alloc_coherent(dev, size, &ring->dma_addr,
					     GFP_KERNEL);
	if (!ring->desc_addr)
		goto err;

	ring->pkt_info = kcalloc(XGENE_ENET_NUM_DESC, sizeof(*ring->pkt_info),
				 GFP_KERNEL);
	if (!ring->pkt_info)
		goto err;

	xge_setup_desc(ring);

	return ring;

err:
	xge_delete_desc_ring(ndev, ring);

	return NULL;
}

static int xge_create_desc_rings(struct net_device *ndev)
{
	struct xge_pdata *pdata = netdev_priv(ndev);
	struct xge_desc_ring *ring;
	int ret;

	/* create tx ring */
	ring = xge_create_desc_ring(ndev);
	if (!ring)
		goto err;

	pdata->tx_ring = ring;
	xge_update_tx_desc_addr(pdata);

	/* create rx ring */
	ring = xge_create_desc_ring(ndev);
	if (!ring)
		goto err;

	pdata->rx_ring = ring;
	xge_update_rx_desc_addr(pdata);

	ret = xge_refill_buffers(ndev, XGENE_ENET_NUM_DESC);
	if (ret)
		goto err;

	return 0;
err:
	xge_delete_desc_rings(ndev);

	return -ENOMEM;
}

static int xge_open(struct net_device *ndev)
{
	struct xge_pdata *pdata = netdev_priv(ndev);
	int ret;

	ret = xge_create_desc_rings(ndev);
	if (ret)
		return ret;

	napi_enable(&pdata->napi);
	ret = xge_request_irq(ndev);
	if (ret)
		return ret;

	xge_intr_enable(pdata);
	xge_wr_csr(pdata, DMARXCTRL, 1);

	phy_start(ndev->phydev);
	xge_mac_enable(pdata);
	netif_start_queue(ndev);

	return 0;
}

static int xge_close(struct net_device *ndev)
{
	struct xge_pdata *pdata = netdev_priv(ndev);

	netif_stop_queue(ndev);
	xge_mac_disable(pdata);
	phy_stop(ndev->phydev);

	xge_intr_disable(pdata);
	xge_free_irq(ndev);
	napi_disable(&pdata->napi);
	xge_delete_desc_rings(ndev);

	return 0;
}

static int xge_napi(struct napi_struct *napi, const int budget)
{
	struct net_device *ndev = napi->dev;
	struct xge_pdata *pdata;
	int processed;

	pdata = netdev_priv(ndev);

	xge_txc_poll(ndev);
	processed = xge_rx_poll(ndev, budget);

	if (processed < budget) {
		napi_complete_done(napi, processed);
		xge_intr_enable(pdata);
	}

	return processed;
}

static int xge_set_mac_addr(struct net_device *ndev, void *addr)
{
	struct xge_pdata *pdata = netdev_priv(ndev);
	int ret;

	ret = eth_mac_addr(ndev, addr);
	if (ret)
		return ret;

	xge_mac_set_station_addr(pdata);

	return 0;
}

static bool is_tx_pending(struct xge_raw_desc *raw_desc)
{
	if (!GET_BITS(E, le64_to_cpu(raw_desc->m0)))
		return true;

	return false;
}

static void xge_free_pending_skb(struct net_device *ndev)
{
	struct xge_pdata *pdata = netdev_priv(ndev);
	struct device *dev = &pdata->pdev->dev;
	struct xge_desc_ring *tx_ring;
	struct xge_raw_desc *raw_desc;
	dma_addr_t dma_addr;
	struct sk_buff *skb;
	void *pkt_buf;
	int i;

	tx_ring = pdata->tx_ring;

	for (i = 0; i < XGENE_ENET_NUM_DESC; i++) {
		raw_desc = &tx_ring->raw_desc[i];

		if (!is_tx_pending(raw_desc))
			continue;

		skb = tx_ring->pkt_info[i].skb;
		dma_addr = tx_ring->pkt_info[i].dma_addr;
		pkt_buf = tx_ring->pkt_info[i].pkt_buf;
		dma_free_coherent(dev, XGENE_ENET_STD_MTU, pkt_buf, dma_addr);
		dev_kfree_skb_any(skb);
	}
}

static void xge_timeout(struct net_device *ndev)
{
	struct xge_pdata *pdata = netdev_priv(ndev);

	rtnl_lock();

	if (!netif_running(ndev))
		goto out;

	netif_stop_queue(ndev);
	xge_intr_disable(pdata);
	napi_disable(&pdata->napi);

	xge_wr_csr(pdata, DMATXCTRL, 0);
	xge_txc_poll(ndev);
	xge_free_pending_skb(ndev);
	xge_wr_csr(pdata, DMATXSTATUS, ~0U);

	xge_setup_desc(pdata->tx_ring);
	xge_update_tx_desc_addr(pdata);
	xge_mac_init(pdata);

	napi_enable(&pdata->napi);
	xge_intr_enable(pdata);
	xge_mac_enable(pdata);
	netif_start_queue(ndev);

out:
	rtnl_unlock();
}

static void xge_get_stats64(struct net_device *ndev,
			    struct rtnl_link_stats64 *storage)
{
	struct xge_pdata *pdata = netdev_priv(ndev);
	struct xge_stats *stats = &pdata->stats;

	storage->tx_packets += stats->tx_packets;
	storage->tx_bytes += stats->tx_bytes;

	storage->rx_packets += stats->rx_packets;
	storage->rx_bytes += stats->rx_bytes;
	storage->rx_errors += stats->rx_errors;
}

static const struct net_device_ops xgene_ndev_ops = {
	.ndo_open = xge_open,
	.ndo_stop = xge_close,
	.ndo_start_xmit = xge_start_xmit,
	.ndo_set_mac_address = xge_set_mac_addr,
	.ndo_tx_timeout = xge_timeout,
	.ndo_get_stats64 = xge_get_stats64,
};

static int xge_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct net_device *ndev;
	struct xge_pdata *pdata;
	int ret;

	ndev = alloc_etherdev(sizeof(*pdata));
	if (!ndev)
		return -ENOMEM;

	pdata = netdev_priv(ndev);

	pdata->pdev = pdev;
	pdata->ndev = ndev;
	SET_NETDEV_DEV(ndev, dev);
	platform_set_drvdata(pdev, pdata);
	ndev->netdev_ops = &xgene_ndev_ops;

	ndev->features |= NETIF_F_GSO |
			  NETIF_F_GRO;

	ret = xge_get_resources(pdata);
	if (ret)
		goto err;

	ndev->hw_features = ndev->features;
	xge_set_ethtool_ops(ndev);

	ret = dma_coerce_mask_and_coherent(dev, DMA_BIT_MASK(64));
	if (ret) {
		netdev_err(ndev, "No usable DMA configuration\n");
		goto err;
	}

	ret = xge_init_hw(ndev);
	if (ret)
		goto err;

	ret = xge_mdio_config(ndev);
	if (ret)
		goto err;

	netif_napi_add(ndev, &pdata->napi, xge_napi, NAPI_POLL_WEIGHT);

	ret = register_netdev(ndev);
	if (ret) {
		netdev_err(ndev, "Failed to register netdev\n");
		goto err;
	}

	return 0;

err:
	free_netdev(ndev);

	return ret;
}

static int xge_remove(struct platform_device *pdev)
{
	struct xge_pdata *pdata;
	struct net_device *ndev;

	pdata = platform_get_drvdata(pdev);
	ndev = pdata->ndev;

	rtnl_lock();
	if (netif_running(ndev))
		dev_close(ndev);
	rtnl_unlock();

	xge_mdio_remove(ndev);
	unregister_netdev(ndev);
	free_netdev(ndev);

	return 0;
}

static void xge_shutdown(struct platform_device *pdev)
{
	struct xge_pdata *pdata;

	pdata = platform_get_drvdata(pdev);
	if (!pdata)
		return;

	if (!pdata->ndev)
		return;

	xge_remove(pdev);
}

static const struct acpi_device_id xge_acpi_match[] = {
	{ "APMC0D80" },
	{ }
};
MODULE_DEVICE_TABLE(acpi, xge_acpi_match);

static struct platform_driver xge_driver = {
	.driver = {
		   .name = "xgene-enet-v2",
		   .acpi_match_table = ACPI_PTR(xge_acpi_match),
	},
	.probe = xge_probe,
	.remove = xge_remove,
	.shutdown = xge_shutdown,
};
module_platform_driver(xge_driver);

MODULE_DESCRIPTION("APM X-Gene SoC Ethernet v2 driver");
MODULE_AUTHOR("Iyappan Subramanian <isubramanian@apm.com>");
MODULE_VERSION(XGENE_ENET_V2_VERSION);
MODULE_LICENSE("GPL");
