/* Applied Micro X-Gene SoC Ethernet Driver
 *
 * Copyright (c) 2014, Applied Micro Circuits Corporation
 * Authors: Iyappan Subramanian <isubramanian@apm.com>
 *	    Ravi Patel <rapatel@apm.com>
 *	    Keyur Chudgar <kchudgar@apm.com>
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

#include "xgene_enet_main.h"
#include "xgene_enet_hw.h"

static void xgene_enet_init_bufpool(struct xgene_enet_desc_ring *buf_pool)
{
	struct xgene_enet_raw_desc16 *raw_desc;
	int i;

	for (i = 0; i < buf_pool->slots; i++) {
		raw_desc = &buf_pool->raw_desc16[i];

		/* Hardware expects descriptor in little endian format */
		raw_desc->m0 = cpu_to_le64(i |
				SET_VAL(FPQNUM, buf_pool->dst_ring_num) |
				SET_VAL(STASH, 3));
	}
}

static int xgene_enet_refill_bufpool(struct xgene_enet_desc_ring *buf_pool,
				     u32 nbuf)
{
	struct sk_buff *skb;
	struct xgene_enet_raw_desc16 *raw_desc;
	struct net_device *ndev;
	struct device *dev;
	dma_addr_t dma_addr;
	u32 tail = buf_pool->tail;
	u32 slots = buf_pool->slots - 1;
	u16 bufdatalen, len;
	int i;

	ndev = buf_pool->ndev;
	dev = ndev_to_dev(buf_pool->ndev);
	bufdatalen = BUF_LEN_CODE_2K | (SKB_BUFFER_SIZE & GENMASK(11, 0));
	len = XGENE_ENET_MAX_MTU;

	for (i = 0; i < nbuf; i++) {
		raw_desc = &buf_pool->raw_desc16[tail];

		skb = netdev_alloc_skb_ip_align(ndev, len);
		if (unlikely(!skb))
			return -ENOMEM;
		buf_pool->rx_skb[tail] = skb;

		dma_addr = dma_map_single(dev, skb->data, len, DMA_FROM_DEVICE);
		if (dma_mapping_error(dev, dma_addr)) {
			netdev_err(ndev, "DMA mapping error\n");
			dev_kfree_skb_any(skb);
			return -EINVAL;
		}

		raw_desc->m1 = cpu_to_le64(SET_VAL(DATAADDR, dma_addr) |
					   SET_VAL(BUFDATALEN, bufdatalen) |
					   SET_BIT(COHERENT));
		tail = (tail + 1) & slots;
	}

	iowrite32(nbuf, buf_pool->cmd);
	buf_pool->tail = tail;

	return 0;
}

static u16 xgene_enet_dst_ring_num(struct xgene_enet_desc_ring *ring)
{
	struct xgene_enet_pdata *pdata = netdev_priv(ring->ndev);

	return ((u16)pdata->rm << 10) | ring->num;
}

static u8 xgene_enet_hdr_len(const void *data)
{
	const struct ethhdr *eth = data;

	return (eth->h_proto == htons(ETH_P_8021Q)) ? VLAN_ETH_HLEN : ETH_HLEN;
}

static u32 xgene_enet_ring_len(struct xgene_enet_desc_ring *ring)
{
	u32 __iomem *cmd_base = ring->cmd_base;
	u32 ring_state, num_msgs;

	ring_state = ioread32(&cmd_base[1]);
	num_msgs = ring_state & CREATE_MASK(NUMMSGSINQ_POS, NUMMSGSINQ_LEN);

	return num_msgs >> NUMMSGSINQ_POS;
}

static void xgene_enet_delete_bufpool(struct xgene_enet_desc_ring *buf_pool)
{
	struct xgene_enet_raw_desc16 *raw_desc;
	u32 slots = buf_pool->slots - 1;
	u32 tail = buf_pool->tail;
	u32 userinfo;
	int i, len;

	len = xgene_enet_ring_len(buf_pool);
	for (i = 0; i < len; i++) {
		tail = (tail - 1) & slots;
		raw_desc = &buf_pool->raw_desc16[tail];

		/* Hardware stores descriptor in little endian format */
		userinfo = GET_VAL(USERINFO, le64_to_cpu(raw_desc->m0));
		dev_kfree_skb_any(buf_pool->rx_skb[userinfo]);
	}

	iowrite32(-len, buf_pool->cmd);
	buf_pool->tail = tail;
}

static irqreturn_t xgene_enet_rx_irq(const int irq, void *data)
{
	struct xgene_enet_desc_ring *rx_ring = data;

	if (napi_schedule_prep(&rx_ring->napi)) {
		disable_irq_nosync(irq);
		__napi_schedule(&rx_ring->napi);
	}

	return IRQ_HANDLED;
}

static int xgene_enet_tx_completion(struct xgene_enet_desc_ring *cp_ring,
				    struct xgene_enet_raw_desc *raw_desc)
{
	struct sk_buff *skb;
	struct device *dev;
	u16 skb_index;
	u8 status;
	int ret = 0;

	skb_index = GET_VAL(USERINFO, le64_to_cpu(raw_desc->m0));
	skb = cp_ring->cp_skb[skb_index];

	dev = ndev_to_dev(cp_ring->ndev);
	dma_unmap_single(dev, GET_VAL(DATAADDR, le64_to_cpu(raw_desc->m1)),
			 GET_VAL(BUFDATALEN, le64_to_cpu(raw_desc->m1)),
			 DMA_TO_DEVICE);

	/* Checking for error */
	status = GET_VAL(LERR, le64_to_cpu(raw_desc->m0));
	if (unlikely(status > 2)) {
		xgene_enet_parse_error(cp_ring, netdev_priv(cp_ring->ndev),
				       status);
		ret = -EIO;
	}

	if (likely(skb)) {
		dev_kfree_skb_any(skb);
	} else {
		netdev_err(cp_ring->ndev, "completion skb is NULL\n");
		ret = -EIO;
	}

	return ret;
}

static u64 xgene_enet_work_msg(struct sk_buff *skb)
{
	struct iphdr *iph;
	u8 l3hlen, l4hlen = 0;
	u8 csum_enable = 0;
	u8 proto = 0;
	u8 ethhdr;
	u64 hopinfo;

	if (unlikely(skb->protocol != htons(ETH_P_IP)) &&
	    unlikely(skb->protocol != htons(ETH_P_8021Q)))
		goto out;

	if (unlikely(!(skb->dev->features & NETIF_F_IP_CSUM)))
		goto out;

	iph = ip_hdr(skb);
	if (unlikely(ip_is_fragment(iph)))
		goto out;

	if (likely(iph->protocol == IPPROTO_TCP)) {
		l4hlen = tcp_hdrlen(skb) >> 2;
		csum_enable = 1;
		proto = TSO_IPPROTO_TCP;
	} else if (iph->protocol == IPPROTO_UDP) {
		l4hlen = UDP_HDR_SIZE;
		csum_enable = 1;
	}
out:
	l3hlen = ip_hdrlen(skb) >> 2;
	ethhdr = xgene_enet_hdr_len(skb->data);
	hopinfo = SET_VAL(TCPHDR, l4hlen) |
		  SET_VAL(IPHDR, l3hlen) |
		  SET_VAL(ETHHDR, ethhdr) |
		  SET_VAL(EC, csum_enable) |
		  SET_VAL(IS, proto) |
		  SET_BIT(IC) |
		  SET_BIT(TYPE_ETH_WORK_MESSAGE);

	return hopinfo;
}

static int xgene_enet_setup_tx_desc(struct xgene_enet_desc_ring *tx_ring,
				    struct sk_buff *skb)
{
	struct device *dev = ndev_to_dev(tx_ring->ndev);
	struct xgene_enet_raw_desc *raw_desc;
	dma_addr_t dma_addr;
	u16 tail = tx_ring->tail;
	u64 hopinfo;

	raw_desc = &tx_ring->raw_desc[tail];
	memset(raw_desc, 0, sizeof(struct xgene_enet_raw_desc));

	dma_addr = dma_map_single(dev, skb->data, skb->len, DMA_TO_DEVICE);
	if (dma_mapping_error(dev, dma_addr)) {
		netdev_err(tx_ring->ndev, "DMA mapping error\n");
		return -EINVAL;
	}

	/* Hardware expects descriptor in little endian format */
	raw_desc->m0 = cpu_to_le64(tail);
	raw_desc->m1 = cpu_to_le64(SET_VAL(DATAADDR, dma_addr) |
				   SET_VAL(BUFDATALEN, skb->len) |
				   SET_BIT(COHERENT));
	hopinfo = xgene_enet_work_msg(skb);
	raw_desc->m3 = cpu_to_le64(SET_VAL(HENQNUM, tx_ring->dst_ring_num) |
				   hopinfo);
	tx_ring->cp_ring->cp_skb[tail] = skb;

	return 0;
}

static netdev_tx_t xgene_enet_start_xmit(struct sk_buff *skb,
					 struct net_device *ndev)
{
	struct xgene_enet_pdata *pdata = netdev_priv(ndev);
	struct xgene_enet_desc_ring *tx_ring = pdata->tx_ring;
	struct xgene_enet_desc_ring *cp_ring = tx_ring->cp_ring;
	u32 tx_level, cq_level;

	tx_level = xgene_enet_ring_len(tx_ring);
	cq_level = xgene_enet_ring_len(cp_ring);
	if (unlikely(tx_level > pdata->tx_qcnt_hi ||
		     cq_level > pdata->cp_qcnt_hi)) {
		netif_stop_queue(ndev);
		return NETDEV_TX_BUSY;
	}

	if (xgene_enet_setup_tx_desc(tx_ring, skb)) {
		dev_kfree_skb_any(skb);
		return NETDEV_TX_OK;
	}

	iowrite32(1, tx_ring->cmd);
	skb_tx_timestamp(skb);
	tx_ring->tail = (tx_ring->tail + 1) & (tx_ring->slots - 1);

	pdata->stats.tx_packets++;
	pdata->stats.tx_bytes += skb->len;

	return NETDEV_TX_OK;
}

static void xgene_enet_skip_csum(struct sk_buff *skb)
{
	struct iphdr *iph = ip_hdr(skb);

	if (!ip_is_fragment(iph) ||
	    (iph->protocol != IPPROTO_TCP && iph->protocol != IPPROTO_UDP)) {
		skb->ip_summed = CHECKSUM_UNNECESSARY;
	}
}

static int xgene_enet_rx_frame(struct xgene_enet_desc_ring *rx_ring,
			       struct xgene_enet_raw_desc *raw_desc)
{
	struct net_device *ndev;
	struct xgene_enet_pdata *pdata;
	struct device *dev;
	struct xgene_enet_desc_ring *buf_pool;
	u32 datalen, skb_index;
	struct sk_buff *skb;
	u8 status;
	int ret = 0;

	ndev = rx_ring->ndev;
	pdata = netdev_priv(ndev);
	dev = ndev_to_dev(rx_ring->ndev);
	buf_pool = rx_ring->buf_pool;

	dma_unmap_single(dev, GET_VAL(DATAADDR, le64_to_cpu(raw_desc->m1)),
			 XGENE_ENET_MAX_MTU, DMA_FROM_DEVICE);
	skb_index = GET_VAL(USERINFO, le64_to_cpu(raw_desc->m0));
	skb = buf_pool->rx_skb[skb_index];

	/* checking for error */
	status = GET_VAL(LERR, le64_to_cpu(raw_desc->m0));
	if (unlikely(status > 2)) {
		dev_kfree_skb_any(skb);
		xgene_enet_parse_error(rx_ring, netdev_priv(rx_ring->ndev),
				       status);
		pdata->stats.rx_dropped++;
		ret = -EIO;
		goto out;
	}

	/* strip off CRC as HW isn't doing this */
	datalen = GET_VAL(BUFDATALEN, le64_to_cpu(raw_desc->m1));
	datalen -= 4;
	prefetch(skb->data - NET_IP_ALIGN);
	skb_put(skb, datalen);

	skb_checksum_none_assert(skb);
	skb->protocol = eth_type_trans(skb, ndev);
	if (likely((ndev->features & NETIF_F_IP_CSUM) &&
		   skb->protocol == htons(ETH_P_IP))) {
		xgene_enet_skip_csum(skb);
	}

	pdata->stats.rx_packets++;
	pdata->stats.rx_bytes += datalen;
	napi_gro_receive(&rx_ring->napi, skb);
out:
	if (--rx_ring->nbufpool == 0) {
		ret = xgene_enet_refill_bufpool(buf_pool, NUM_BUFPOOL);
		rx_ring->nbufpool = NUM_BUFPOOL;
	}

	return ret;
}

static bool is_rx_desc(struct xgene_enet_raw_desc *raw_desc)
{
	return GET_VAL(FPQNUM, le64_to_cpu(raw_desc->m0)) ? true : false;
}

static int xgene_enet_process_ring(struct xgene_enet_desc_ring *ring,
				   int budget)
{
	struct xgene_enet_pdata *pdata = netdev_priv(ring->ndev);
	struct xgene_enet_raw_desc *raw_desc;
	u16 head = ring->head;
	u16 slots = ring->slots - 1;
	int ret, count = 0;

	do {
		raw_desc = &ring->raw_desc[head];
		if (unlikely(xgene_enet_is_desc_slot_empty(raw_desc)))
			break;

		if (is_rx_desc(raw_desc))
			ret = xgene_enet_rx_frame(ring, raw_desc);
		else
			ret = xgene_enet_tx_completion(ring, raw_desc);
		xgene_enet_mark_desc_slot_empty(raw_desc);

		head = (head + 1) & slots;
		count++;

		if (ret)
			break;
	} while (--budget);

	if (likely(count)) {
		iowrite32(-count, ring->cmd);
		ring->head = head;

		if (netif_queue_stopped(ring->ndev)) {
			if (xgene_enet_ring_len(ring) < pdata->cp_qcnt_low)
				netif_wake_queue(ring->ndev);
		}
	}

	return budget;
}

static int xgene_enet_napi(struct napi_struct *napi, const int budget)
{
	struct xgene_enet_desc_ring *ring;
	int processed;

	ring = container_of(napi, struct xgene_enet_desc_ring, napi);
	processed = xgene_enet_process_ring(ring, budget);

	if (processed != budget) {
		napi_complete(napi);
		enable_irq(ring->irq);
	}

	return processed;
}

static void xgene_enet_timeout(struct net_device *ndev)
{
	struct xgene_enet_pdata *pdata = netdev_priv(ndev);

	xgene_gmac_reset(pdata);
}

static int xgene_enet_register_irq(struct net_device *ndev)
{
	struct xgene_enet_pdata *pdata = netdev_priv(ndev);
	struct device *dev = ndev_to_dev(ndev);
	int ret;

	ret = devm_request_irq(dev, pdata->rx_ring->irq, xgene_enet_rx_irq,
			       IRQF_SHARED, ndev->name, pdata->rx_ring);
	if (ret) {
		netdev_err(ndev, "rx%d interrupt request failed\n",
			   pdata->rx_ring->irq);
	}

	return ret;
}

static void xgene_enet_free_irq(struct net_device *ndev)
{
	struct xgene_enet_pdata *pdata;
	struct device *dev;

	pdata = netdev_priv(ndev);
	dev = ndev_to_dev(ndev);
	devm_free_irq(dev, pdata->rx_ring->irq, pdata->rx_ring);
}

static int xgene_enet_open(struct net_device *ndev)
{
	struct xgene_enet_pdata *pdata = netdev_priv(ndev);
	int ret;

	xgene_gmac_tx_enable(pdata);
	xgene_gmac_rx_enable(pdata);

	ret = xgene_enet_register_irq(ndev);
	if (ret)
		return ret;
	napi_enable(&pdata->rx_ring->napi);

	if (pdata->phy_dev)
		phy_start(pdata->phy_dev);

	netif_start_queue(ndev);

	return ret;
}

static int xgene_enet_close(struct net_device *ndev)
{
	struct xgene_enet_pdata *pdata = netdev_priv(ndev);

	netif_stop_queue(ndev);

	if (pdata->phy_dev)
		phy_stop(pdata->phy_dev);

	napi_disable(&pdata->rx_ring->napi);
	xgene_enet_free_irq(ndev);
	xgene_enet_process_ring(pdata->rx_ring, -1);

	xgene_gmac_tx_disable(pdata);
	xgene_gmac_rx_disable(pdata);

	return 0;
}

static void xgene_enet_delete_ring(struct xgene_enet_desc_ring *ring)
{
	struct xgene_enet_pdata *pdata;
	struct device *dev;

	pdata = netdev_priv(ring->ndev);
	dev = ndev_to_dev(ring->ndev);

	xgene_enet_clear_ring(ring);
	dma_free_coherent(dev, ring->size, ring->desc_addr, ring->dma);
}

static void xgene_enet_delete_desc_rings(struct xgene_enet_pdata *pdata)
{
	struct xgene_enet_desc_ring *buf_pool;

	if (pdata->tx_ring) {
		xgene_enet_delete_ring(pdata->tx_ring);
		pdata->tx_ring = NULL;
	}

	if (pdata->rx_ring) {
		buf_pool = pdata->rx_ring->buf_pool;
		xgene_enet_delete_bufpool(buf_pool);
		xgene_enet_delete_ring(buf_pool);
		xgene_enet_delete_ring(pdata->rx_ring);
		pdata->rx_ring = NULL;
	}
}

static int xgene_enet_get_ring_size(struct device *dev,
				    enum xgene_enet_ring_cfgsize cfgsize)
{
	int size = -EINVAL;

	switch (cfgsize) {
	case RING_CFGSIZE_512B:
		size = 0x200;
		break;
	case RING_CFGSIZE_2KB:
		size = 0x800;
		break;
	case RING_CFGSIZE_16KB:
		size = 0x4000;
		break;
	case RING_CFGSIZE_64KB:
		size = 0x10000;
		break;
	case RING_CFGSIZE_512KB:
		size = 0x80000;
		break;
	default:
		dev_err(dev, "Unsupported cfg ring size %d\n", cfgsize);
		break;
	}

	return size;
}

static void xgene_enet_free_desc_ring(struct xgene_enet_desc_ring *ring)
{
	struct device *dev;

	if (!ring)
		return;

	dev = ndev_to_dev(ring->ndev);

	if (ring->desc_addr) {
		xgene_enet_clear_ring(ring);
		dma_free_coherent(dev, ring->size, ring->desc_addr, ring->dma);
	}
	devm_kfree(dev, ring);
}

static void xgene_enet_free_desc_rings(struct xgene_enet_pdata *pdata)
{
	struct device *dev = &pdata->pdev->dev;
	struct xgene_enet_desc_ring *ring;

	ring = pdata->tx_ring;
	if (ring && ring->cp_ring && ring->cp_ring->cp_skb)
		devm_kfree(dev, ring->cp_ring->cp_skb);
	xgene_enet_free_desc_ring(ring);

	ring = pdata->rx_ring;
	if (ring && ring->buf_pool && ring->buf_pool->rx_skb)
		devm_kfree(dev, ring->buf_pool->rx_skb);
	xgene_enet_free_desc_ring(ring->buf_pool);
	xgene_enet_free_desc_ring(ring);
}

static struct xgene_enet_desc_ring *xgene_enet_create_desc_ring(
			struct net_device *ndev, u32 ring_num,
			enum xgene_enet_ring_cfgsize cfgsize, u32 ring_id)
{
	struct xgene_enet_desc_ring *ring;
	struct xgene_enet_pdata *pdata = netdev_priv(ndev);
	struct device *dev = ndev_to_dev(ndev);
	u32 size;

	ring = devm_kzalloc(dev, sizeof(struct xgene_enet_desc_ring),
			    GFP_KERNEL);
	if (!ring)
		return NULL;

	ring->ndev = ndev;
	ring->num = ring_num;
	ring->cfgsize = cfgsize;
	ring->id = ring_id;

	size = xgene_enet_get_ring_size(dev, cfgsize);
	ring->desc_addr = dma_zalloc_coherent(dev, size, &ring->dma,
					      GFP_KERNEL);
	if (!ring->desc_addr) {
		devm_kfree(dev, ring);
		return NULL;
	}
	ring->size = size;

	ring->cmd_base = pdata->ring_cmd_addr + (ring->num << 6);
	ring->cmd = ring->cmd_base + INC_DEC_CMD_ADDR;
	pdata->rm = RM3;
	ring = xgene_enet_setup_ring(ring);
	netdev_dbg(ndev, "ring info: num=%d  size=%d  id=%d  slots=%d\n",
		   ring->num, ring->size, ring->id, ring->slots);

	return ring;
}

static u16 xgene_enet_get_ring_id(enum xgene_ring_owner owner, u8 bufnum)
{
	return (owner << 6) | (bufnum & GENMASK(5, 0));
}

static int xgene_enet_create_desc_rings(struct net_device *ndev)
{
	struct xgene_enet_pdata *pdata = netdev_priv(ndev);
	struct device *dev = ndev_to_dev(ndev);
	struct xgene_enet_desc_ring *rx_ring, *tx_ring, *cp_ring;
	struct xgene_enet_desc_ring *buf_pool = NULL;
	u8 cpu_bufnum = 0, eth_bufnum = 0;
	u8 bp_bufnum = 0x20;
	u16 ring_id, ring_num = 0;
	int ret;

	/* allocate rx descriptor ring */
	ring_id = xgene_enet_get_ring_id(RING_OWNER_CPU, cpu_bufnum++);
	rx_ring = xgene_enet_create_desc_ring(ndev, ring_num++,
					      RING_CFGSIZE_16KB, ring_id);
	if (!rx_ring) {
		ret = -ENOMEM;
		goto err;
	}

	/* allocate buffer pool for receiving packets */
	ring_id = xgene_enet_get_ring_id(RING_OWNER_ETH0, bp_bufnum++);
	buf_pool = xgene_enet_create_desc_ring(ndev, ring_num++,
					       RING_CFGSIZE_2KB, ring_id);
	if (!buf_pool) {
		ret = -ENOMEM;
		goto err;
	}

	rx_ring->nbufpool = NUM_BUFPOOL;
	rx_ring->buf_pool = buf_pool;
	rx_ring->irq = pdata->rx_irq;
	buf_pool->rx_skb = devm_kcalloc(dev, buf_pool->slots,
					sizeof(struct sk_buff *), GFP_KERNEL);
	if (!buf_pool->rx_skb) {
		ret = -ENOMEM;
		goto err;
	}

	buf_pool->dst_ring_num = xgene_enet_dst_ring_num(buf_pool);
	rx_ring->buf_pool = buf_pool;
	pdata->rx_ring = rx_ring;

	/* allocate tx descriptor ring */
	ring_id = xgene_enet_get_ring_id(RING_OWNER_ETH0, eth_bufnum++);
	tx_ring = xgene_enet_create_desc_ring(ndev, ring_num++,
					      RING_CFGSIZE_16KB, ring_id);
	if (!tx_ring) {
		ret = -ENOMEM;
		goto err;
	}
	pdata->tx_ring = tx_ring;

	cp_ring = pdata->rx_ring;
	cp_ring->cp_skb = devm_kcalloc(dev, tx_ring->slots,
				       sizeof(struct sk_buff *), GFP_KERNEL);
	if (!cp_ring->cp_skb) {
		ret = -ENOMEM;
		goto err;
	}
	pdata->tx_ring->cp_ring = cp_ring;
	pdata->tx_ring->dst_ring_num = xgene_enet_dst_ring_num(cp_ring);

	pdata->tx_qcnt_hi = pdata->tx_ring->slots / 2;
	pdata->cp_qcnt_hi = pdata->rx_ring->slots / 2;
	pdata->cp_qcnt_low = pdata->cp_qcnt_hi / 2;

	return 0;

err:
	xgene_enet_free_desc_rings(pdata);
	return ret;
}

static struct rtnl_link_stats64 *xgene_enet_get_stats64(
			struct net_device *ndev,
			struct rtnl_link_stats64 *storage)
{
	struct xgene_enet_pdata *pdata = netdev_priv(ndev);
	struct rtnl_link_stats64 *stats = &pdata->stats;

	stats->rx_errors += stats->rx_length_errors +
			    stats->rx_crc_errors +
			    stats->rx_frame_errors +
			    stats->rx_fifo_errors;
	memcpy(storage, &pdata->stats, sizeof(struct rtnl_link_stats64));

	return storage;
}

static int xgene_enet_set_mac_address(struct net_device *ndev, void *addr)
{
	struct xgene_enet_pdata *pdata = netdev_priv(ndev);
	int ret;

	ret = eth_mac_addr(ndev, addr);
	if (ret)
		return ret;
	xgene_gmac_set_mac_addr(pdata);

	return ret;
}

static const struct net_device_ops xgene_ndev_ops = {
	.ndo_open = xgene_enet_open,
	.ndo_stop = xgene_enet_close,
	.ndo_start_xmit = xgene_enet_start_xmit,
	.ndo_tx_timeout = xgene_enet_timeout,
	.ndo_get_stats64 = xgene_enet_get_stats64,
	.ndo_change_mtu = eth_change_mtu,
	.ndo_set_mac_address = xgene_enet_set_mac_address,
};

static int xgene_enet_get_resources(struct xgene_enet_pdata *pdata)
{
	struct platform_device *pdev;
	struct net_device *ndev;
	struct device *dev;
	struct resource *res;
	void __iomem *base_addr;
	const char *mac;
	int ret;

	pdev = pdata->pdev;
	dev = &pdev->dev;
	ndev = pdata->ndev;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "enet_csr");
	if (!res) {
		dev_err(dev, "Resource enet_csr not defined\n");
		return -ENODEV;
	}
	pdata->base_addr = devm_ioremap_resource(dev, res);
	if (IS_ERR(pdata->base_addr)) {
		dev_err(dev, "Unable to retrieve ENET Port CSR region\n");
		return PTR_ERR(pdata->base_addr);
	}

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "ring_csr");
	if (!res) {
		dev_err(dev, "Resource ring_csr not defined\n");
		return -ENODEV;
	}
	pdata->ring_csr_addr = devm_ioremap_resource(dev, res);
	if (IS_ERR(pdata->ring_csr_addr)) {
		dev_err(dev, "Unable to retrieve ENET Ring CSR region\n");
		return PTR_ERR(pdata->ring_csr_addr);
	}

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "ring_cmd");
	if (!res) {
		dev_err(dev, "Resource ring_cmd not defined\n");
		return -ENODEV;
	}
	pdata->ring_cmd_addr = devm_ioremap_resource(dev, res);
	if (IS_ERR(pdata->ring_cmd_addr)) {
		dev_err(dev, "Unable to retrieve ENET Ring command region\n");
		return PTR_ERR(pdata->ring_cmd_addr);
	}

	ret = platform_get_irq(pdev, 0);
	if (ret <= 0) {
		dev_err(dev, "Unable to get ENET Rx IRQ\n");
		ret = ret ? : -ENXIO;
		return ret;
	}
	pdata->rx_irq = ret;

	mac = of_get_mac_address(dev->of_node);
	if (mac)
		memcpy(ndev->dev_addr, mac, ndev->addr_len);
	else
		eth_hw_addr_random(ndev);
	memcpy(ndev->perm_addr, ndev->dev_addr, ndev->addr_len);

	pdata->phy_mode = of_get_phy_mode(pdev->dev.of_node);
	if (pdata->phy_mode < 0) {
		dev_err(dev, "Incorrect phy-connection-type in DTS\n");
		return -EINVAL;
	}

	pdata->clk = devm_clk_get(&pdev->dev, NULL);
	ret = IS_ERR(pdata->clk);
	if (IS_ERR(pdata->clk)) {
		dev_err(&pdev->dev, "can't get clock\n");
		ret = PTR_ERR(pdata->clk);
		return ret;
	}

	base_addr = pdata->base_addr;
	pdata->eth_csr_addr = base_addr + BLOCK_ETH_CSR_OFFSET;
	pdata->eth_ring_if_addr = base_addr + BLOCK_ETH_RING_IF_OFFSET;
	pdata->eth_diag_csr_addr = base_addr + BLOCK_ETH_DIAG_CSR_OFFSET;
	pdata->mcx_mac_addr = base_addr + BLOCK_ETH_MAC_OFFSET;
	pdata->mcx_stats_addr = base_addr + BLOCK_ETH_STATS_OFFSET;
	pdata->mcx_mac_csr_addr = base_addr + BLOCK_ETH_MAC_CSR_OFFSET;
	pdata->rx_buff_cnt = NUM_PKT_BUF;

	return ret;
}

static int xgene_enet_init_hw(struct xgene_enet_pdata *pdata)
{
	struct net_device *ndev = pdata->ndev;
	struct xgene_enet_desc_ring *buf_pool;
	u16 dst_ring_num;
	int ret;

	xgene_gmac_tx_disable(pdata);
	xgene_gmac_rx_disable(pdata);

	ret = xgene_enet_create_desc_rings(ndev);
	if (ret) {
		netdev_err(ndev, "Error in ring configuration\n");
		return ret;
	}

	/* setup buffer pool */
	buf_pool = pdata->rx_ring->buf_pool;
	xgene_enet_init_bufpool(buf_pool);
	ret = xgene_enet_refill_bufpool(buf_pool, pdata->rx_buff_cnt);
	if (ret) {
		xgene_enet_delete_desc_rings(pdata);
		return ret;
	}

	dst_ring_num = xgene_enet_dst_ring_num(pdata->rx_ring);
	xgene_enet_cle_bypass(pdata, dst_ring_num, buf_pool->id);

	return ret;
}

static int xgene_enet_probe(struct platform_device *pdev)
{
	struct net_device *ndev;
	struct xgene_enet_pdata *pdata;
	struct device *dev = &pdev->dev;
	struct napi_struct *napi;
	int ret;

	ndev = alloc_etherdev(sizeof(struct xgene_enet_pdata));
	if (!ndev)
		return -ENOMEM;

	pdata = netdev_priv(ndev);

	pdata->pdev = pdev;
	pdata->ndev = ndev;
	SET_NETDEV_DEV(ndev, dev);
	platform_set_drvdata(pdev, pdata);
	ndev->netdev_ops = &xgene_ndev_ops;
	xgene_enet_set_ethtool_ops(ndev);
	ndev->features |= NETIF_F_IP_CSUM |
			  NETIF_F_GSO |
			  NETIF_F_GRO;

	ret = xgene_enet_get_resources(pdata);
	if (ret)
		goto err;

	xgene_enet_reset(pdata);
	xgene_gmac_init(pdata, SPEED_1000);

	ret = register_netdev(ndev);
	if (ret) {
		netdev_err(ndev, "Failed to register netdev\n");
		goto err;
	}

	ret = dma_set_mask_and_coherent(dev, DMA_BIT_MASK(64));
	if (ret) {
		netdev_err(ndev, "No usable DMA configuration\n");
		goto err;
	}

	ret = xgene_enet_init_hw(pdata);
	if (ret)
		goto err;

	napi = &pdata->rx_ring->napi;
	netif_napi_add(ndev, napi, xgene_enet_napi, NAPI_POLL_WEIGHT);
	ret = xgene_enet_mdio_config(pdata);

	return ret;
err:
	free_netdev(ndev);
	return ret;
}

static int xgene_enet_remove(struct platform_device *pdev)
{
	struct xgene_enet_pdata *pdata;
	struct net_device *ndev;

	pdata = platform_get_drvdata(pdev);
	ndev = pdata->ndev;

	xgene_gmac_rx_disable(pdata);
	xgene_gmac_tx_disable(pdata);

	netif_napi_del(&pdata->rx_ring->napi);
	xgene_enet_mdio_remove(pdata);
	xgene_enet_delete_desc_rings(pdata);
	unregister_netdev(ndev);
	xgene_gport_shutdown(pdata);
	free_netdev(ndev);

	return 0;
}

static struct of_device_id xgene_enet_match[] = {
	{.compatible = "apm,xgene-enet",},
	{},
};

MODULE_DEVICE_TABLE(of, xgene_enet_match);

static struct platform_driver xgene_enet_driver = {
	.driver = {
		   .name = "xgene-enet",
		   .of_match_table = xgene_enet_match,
	},
	.probe = xgene_enet_probe,
	.remove = xgene_enet_remove,
};

module_platform_driver(xgene_enet_driver);

MODULE_DESCRIPTION("APM X-Gene SoC Ethernet driver");
MODULE_VERSION(XGENE_DRV_VERSION);
MODULE_AUTHOR("Keyur Chudgar <kchudgar@apm.com>");
MODULE_LICENSE("GPL");
