// SPDX-License-Identifier: GPL-2.0-or-later
/* Applied Micro X-Gene SoC Ethernet Driver
 *
 * Copyright (c) 2014, Applied Micro Circuits Corporation
 * Authors: Iyappan Subramanian <isubramanian@apm.com>
 *	    Ravi Patel <rapatel@apm.com>
 *	    Keyur Chudgar <kchudgar@apm.com>
 */

#include <linux/gpio.h>
#include "xgene_enet_main.h"
#include "xgene_enet_hw.h"
#include "xgene_enet_sgmac.h"
#include "xgene_enet_xgmac.h"

#define RES_ENET_CSR	0
#define RES_RING_CSR	1
#define RES_RING_CMD	2

static void xgene_enet_init_bufpool(struct xgene_enet_desc_ring *buf_pool)
{
	struct xgene_enet_raw_desc16 *raw_desc;
	int i;

	if (!buf_pool)
		return;

	for (i = 0; i < buf_pool->slots; i++) {
		raw_desc = &buf_pool->raw_desc16[i];

		/* Hardware expects descriptor in little endian format */
		raw_desc->m0 = cpu_to_le64(i |
				SET_VAL(FPQNUM, buf_pool->dst_ring_num) |
				SET_VAL(STASH, 3));
	}
}

static u16 xgene_enet_get_data_len(u64 bufdatalen)
{
	u16 hw_len, mask;

	hw_len = GET_VAL(BUFDATALEN, bufdatalen);

	if (unlikely(hw_len == 0x7800)) {
		return 0;
	} else if (!(hw_len & BIT(14))) {
		mask = GENMASK(13, 0);
		return (hw_len & mask) ? (hw_len & mask) : SIZE_16K;
	} else if (!(hw_len & GENMASK(13, 12))) {
		mask = GENMASK(11, 0);
		return (hw_len & mask) ? (hw_len & mask) : SIZE_4K;
	} else {
		mask = GENMASK(11, 0);
		return (hw_len & mask) ? (hw_len & mask) : SIZE_2K;
	}
}

static u16 xgene_enet_set_data_len(u32 size)
{
	u16 hw_len;

	hw_len =  (size == SIZE_4K) ? BIT(14) : 0;

	return hw_len;
}

static int xgene_enet_refill_pagepool(struct xgene_enet_desc_ring *buf_pool,
				      u32 nbuf)
{
	struct xgene_enet_raw_desc16 *raw_desc;
	struct xgene_enet_pdata *pdata;
	struct net_device *ndev;
	dma_addr_t dma_addr;
	struct device *dev;
	struct page *page;
	u32 slots, tail;
	u16 hw_len;
	int i;

	if (unlikely(!buf_pool))
		return 0;

	ndev = buf_pool->ndev;
	pdata = netdev_priv(ndev);
	dev = ndev_to_dev(ndev);
	slots = buf_pool->slots - 1;
	tail = buf_pool->tail;

	for (i = 0; i < nbuf; i++) {
		raw_desc = &buf_pool->raw_desc16[tail];

		page = dev_alloc_page();
		if (unlikely(!page))
			return -ENOMEM;

		dma_addr = dma_map_page(dev, page, 0,
					PAGE_SIZE, DMA_FROM_DEVICE);
		if (unlikely(dma_mapping_error(dev, dma_addr))) {
			put_page(page);
			return -ENOMEM;
		}

		hw_len = xgene_enet_set_data_len(PAGE_SIZE);
		raw_desc->m1 = cpu_to_le64(SET_VAL(DATAADDR, dma_addr) |
					   SET_VAL(BUFDATALEN, hw_len) |
					   SET_BIT(COHERENT));

		buf_pool->frag_page[tail] = page;
		tail = (tail + 1) & slots;
	}

	pdata->ring_ops->wr_cmd(buf_pool, nbuf);
	buf_pool->tail = tail;

	return 0;
}

static int xgene_enet_refill_bufpool(struct xgene_enet_desc_ring *buf_pool,
				     u32 nbuf)
{
	struct sk_buff *skb;
	struct xgene_enet_raw_desc16 *raw_desc;
	struct xgene_enet_pdata *pdata;
	struct net_device *ndev;
	struct device *dev;
	dma_addr_t dma_addr;
	u32 tail = buf_pool->tail;
	u32 slots = buf_pool->slots - 1;
	u16 bufdatalen, len;
	int i;

	ndev = buf_pool->ndev;
	dev = ndev_to_dev(buf_pool->ndev);
	pdata = netdev_priv(ndev);

	bufdatalen = BUF_LEN_CODE_2K | (SKB_BUFFER_SIZE & GENMASK(11, 0));
	len = XGENE_ENET_STD_MTU;

	for (i = 0; i < nbuf; i++) {
		raw_desc = &buf_pool->raw_desc16[tail];

		skb = netdev_alloc_skb_ip_align(ndev, len);
		if (unlikely(!skb))
			return -ENOMEM;

		dma_addr = dma_map_single(dev, skb->data, len, DMA_FROM_DEVICE);
		if (dma_mapping_error(dev, dma_addr)) {
			netdev_err(ndev, "DMA mapping error\n");
			dev_kfree_skb_any(skb);
			return -EINVAL;
		}

		buf_pool->rx_skb[tail] = skb;

		raw_desc->m1 = cpu_to_le64(SET_VAL(DATAADDR, dma_addr) |
					   SET_VAL(BUFDATALEN, bufdatalen) |
					   SET_BIT(COHERENT));
		tail = (tail + 1) & slots;
	}

	pdata->ring_ops->wr_cmd(buf_pool, nbuf);
	buf_pool->tail = tail;

	return 0;
}

static u8 xgene_enet_hdr_len(const void *data)
{
	const struct ethhdr *eth = data;

	return (eth->h_proto == htons(ETH_P_8021Q)) ? VLAN_ETH_HLEN : ETH_HLEN;
}

static void xgene_enet_delete_bufpool(struct xgene_enet_desc_ring *buf_pool)
{
	struct device *dev = ndev_to_dev(buf_pool->ndev);
	struct xgene_enet_raw_desc16 *raw_desc;
	dma_addr_t dma_addr;
	int i;

	/* Free up the buffers held by hardware */
	for (i = 0; i < buf_pool->slots; i++) {
		if (buf_pool->rx_skb[i]) {
			dev_kfree_skb_any(buf_pool->rx_skb[i]);

			raw_desc = &buf_pool->raw_desc16[i];
			dma_addr = GET_VAL(DATAADDR, le64_to_cpu(raw_desc->m1));
			dma_unmap_single(dev, dma_addr, XGENE_ENET_MAX_MTU,
					 DMA_FROM_DEVICE);
		}
	}
}

static void xgene_enet_delete_pagepool(struct xgene_enet_desc_ring *buf_pool)
{
	struct device *dev = ndev_to_dev(buf_pool->ndev);
	dma_addr_t dma_addr;
	struct page *page;
	int i;

	/* Free up the buffers held by hardware */
	for (i = 0; i < buf_pool->slots; i++) {
		page = buf_pool->frag_page[i];
		if (page) {
			dma_addr = buf_pool->frag_dma_addr[i];
			dma_unmap_page(dev, dma_addr, PAGE_SIZE,
				       DMA_FROM_DEVICE);
			put_page(page);
		}
	}
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
	struct xgene_enet_pdata *pdata = netdev_priv(cp_ring->ndev);
	struct sk_buff *skb;
	struct device *dev;
	skb_frag_t *frag;
	dma_addr_t *frag_dma_addr;
	u16 skb_index;
	u8 mss_index;
	u8 status;
	int i;

	skb_index = GET_VAL(USERINFO, le64_to_cpu(raw_desc->m0));
	skb = cp_ring->cp_skb[skb_index];
	frag_dma_addr = &cp_ring->frag_dma_addr[skb_index * MAX_SKB_FRAGS];

	dev = ndev_to_dev(cp_ring->ndev);
	dma_unmap_single(dev, GET_VAL(DATAADDR, le64_to_cpu(raw_desc->m1)),
			 skb_headlen(skb),
			 DMA_TO_DEVICE);

	for (i = 0; i < skb_shinfo(skb)->nr_frags; i++) {
		frag = &skb_shinfo(skb)->frags[i];
		dma_unmap_page(dev, frag_dma_addr[i], skb_frag_size(frag),
			       DMA_TO_DEVICE);
	}

	if (GET_BIT(ET, le64_to_cpu(raw_desc->m3))) {
		mss_index = GET_VAL(MSS, le64_to_cpu(raw_desc->m3));
		spin_lock(&pdata->mss_lock);
		pdata->mss_refcnt[mss_index]--;
		spin_unlock(&pdata->mss_lock);
	}

	/* Checking for error */
	status = GET_VAL(LERR, le64_to_cpu(raw_desc->m0));
	if (unlikely(status > 2)) {
		cp_ring->tx_dropped++;
		cp_ring->tx_errors++;
	}

	if (likely(skb)) {
		dev_kfree_skb_any(skb);
	} else {
		netdev_err(cp_ring->ndev, "completion skb is NULL\n");
	}

	return 0;
}

static int xgene_enet_setup_mss(struct net_device *ndev, u32 mss)
{
	struct xgene_enet_pdata *pdata = netdev_priv(ndev);
	int mss_index = -EBUSY;
	int i;

	spin_lock(&pdata->mss_lock);

	/* Reuse the slot if MSS matches */
	for (i = 0; mss_index < 0 && i < NUM_MSS_REG; i++) {
		if (pdata->mss[i] == mss) {
			pdata->mss_refcnt[i]++;
			mss_index = i;
		}
	}

	/* Overwrite the slot with ref_count = 0 */
	for (i = 0; mss_index < 0 && i < NUM_MSS_REG; i++) {
		if (!pdata->mss_refcnt[i]) {
			pdata->mss_refcnt[i]++;
			pdata->mac_ops->set_mss(pdata, mss, i);
			pdata->mss[i] = mss;
			mss_index = i;
		}
	}

	spin_unlock(&pdata->mss_lock);

	return mss_index;
}

static int xgene_enet_work_msg(struct sk_buff *skb, u64 *hopinfo)
{
	struct net_device *ndev = skb->dev;
	struct iphdr *iph;
	u8 l3hlen = 0, l4hlen = 0;
	u8 ethhdr, proto = 0, csum_enable = 0;
	u32 hdr_len, mss = 0;
	u32 i, len, nr_frags;
	int mss_index;

	ethhdr = xgene_enet_hdr_len(skb->data);

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
		if (ndev->features & NETIF_F_TSO) {
			hdr_len = ethhdr + ip_hdrlen(skb) + tcp_hdrlen(skb);
			mss = skb_shinfo(skb)->gso_size;

			if (skb_is_nonlinear(skb)) {
				len = skb_headlen(skb);
				nr_frags = skb_shinfo(skb)->nr_frags;

				for (i = 0; i < 2 && i < nr_frags; i++)
					len += skb_frag_size(
						&skb_shinfo(skb)->frags[i]);

				/* HW requires header must reside in 3 buffer */
				if (unlikely(hdr_len > len)) {
					if (skb_linearize(skb))
						return 0;
				}
			}

			if (!mss || ((skb->len - hdr_len) <= mss))
				goto out;

			mss_index = xgene_enet_setup_mss(ndev, mss);
			if (unlikely(mss_index < 0))
				return -EBUSY;

			*hopinfo |= SET_BIT(ET) | SET_VAL(MSS, mss_index);
		}
	} else if (iph->protocol == IPPROTO_UDP) {
		l4hlen = UDP_HDR_SIZE;
		csum_enable = 1;
	}
out:
	l3hlen = ip_hdrlen(skb) >> 2;
	*hopinfo |= SET_VAL(TCPHDR, l4hlen) |
		    SET_VAL(IPHDR, l3hlen) |
		    SET_VAL(ETHHDR, ethhdr) |
		    SET_VAL(EC, csum_enable) |
		    SET_VAL(IS, proto) |
		    SET_BIT(IC) |
		    SET_BIT(TYPE_ETH_WORK_MESSAGE);

	return 0;
}

static u16 xgene_enet_encode_len(u16 len)
{
	return (len == BUFLEN_16K) ? 0 : len;
}

static void xgene_set_addr_len(__le64 *desc, u32 idx, dma_addr_t addr, u32 len)
{
	desc[idx ^ 1] = cpu_to_le64(SET_VAL(DATAADDR, addr) |
				    SET_VAL(BUFDATALEN, len));
}

static __le64 *xgene_enet_get_exp_bufs(struct xgene_enet_desc_ring *ring)
{
	__le64 *exp_bufs;

	exp_bufs = &ring->exp_bufs[ring->exp_buf_tail * MAX_EXP_BUFFS];
	memset(exp_bufs, 0, sizeof(__le64) * MAX_EXP_BUFFS);
	ring->exp_buf_tail = (ring->exp_buf_tail + 1) & ((ring->slots / 2) - 1);

	return exp_bufs;
}

static dma_addr_t *xgene_get_frag_dma_array(struct xgene_enet_desc_ring *ring)
{
	return &ring->cp_ring->frag_dma_addr[ring->tail * MAX_SKB_FRAGS];
}

static int xgene_enet_setup_tx_desc(struct xgene_enet_desc_ring *tx_ring,
				    struct sk_buff *skb)
{
	struct device *dev = ndev_to_dev(tx_ring->ndev);
	struct xgene_enet_pdata *pdata = netdev_priv(tx_ring->ndev);
	struct xgene_enet_raw_desc *raw_desc;
	__le64 *exp_desc = NULL, *exp_bufs = NULL;
	dma_addr_t dma_addr, pbuf_addr, *frag_dma_addr;
	skb_frag_t *frag;
	u16 tail = tx_ring->tail;
	u64 hopinfo = 0;
	u32 len, hw_len;
	u8 ll = 0, nv = 0, idx = 0;
	bool split = false;
	u32 size, offset, ell_bytes = 0;
	u32 i, fidx, nr_frags, count = 1;
	int ret;

	raw_desc = &tx_ring->raw_desc[tail];
	tail = (tail + 1) & (tx_ring->slots - 1);
	memset(raw_desc, 0, sizeof(struct xgene_enet_raw_desc));

	ret = xgene_enet_work_msg(skb, &hopinfo);
	if (ret)
		return ret;

	raw_desc->m3 = cpu_to_le64(SET_VAL(HENQNUM, tx_ring->dst_ring_num) |
				   hopinfo);

	len = skb_headlen(skb);
	hw_len = xgene_enet_encode_len(len);

	dma_addr = dma_map_single(dev, skb->data, len, DMA_TO_DEVICE);
	if (dma_mapping_error(dev, dma_addr)) {
		netdev_err(tx_ring->ndev, "DMA mapping error\n");
		return -EINVAL;
	}

	/* Hardware expects descriptor in little endian format */
	raw_desc->m1 = cpu_to_le64(SET_VAL(DATAADDR, dma_addr) |
				   SET_VAL(BUFDATALEN, hw_len) |
				   SET_BIT(COHERENT));

	if (!skb_is_nonlinear(skb))
		goto out;

	/* scatter gather */
	nv = 1;
	exp_desc = (void *)&tx_ring->raw_desc[tail];
	tail = (tail + 1) & (tx_ring->slots - 1);
	memset(exp_desc, 0, sizeof(struct xgene_enet_raw_desc));

	nr_frags = skb_shinfo(skb)->nr_frags;
	for (i = nr_frags; i < 4 ; i++)
		exp_desc[i ^ 1] = cpu_to_le64(LAST_BUFFER);

	frag_dma_addr = xgene_get_frag_dma_array(tx_ring);

	for (i = 0, fidx = 0; split || (fidx < nr_frags); i++) {
		if (!split) {
			frag = &skb_shinfo(skb)->frags[fidx];
			size = skb_frag_size(frag);
			offset = 0;

			pbuf_addr = skb_frag_dma_map(dev, frag, 0, size,
						     DMA_TO_DEVICE);
			if (dma_mapping_error(dev, pbuf_addr))
				return -EINVAL;

			frag_dma_addr[fidx] = pbuf_addr;
			fidx++;

			if (size > BUFLEN_16K)
				split = true;
		}

		if (size > BUFLEN_16K) {
			len = BUFLEN_16K;
			size -= BUFLEN_16K;
		} else {
			len = size;
			split = false;
		}

		dma_addr = pbuf_addr + offset;
		hw_len = xgene_enet_encode_len(len);

		switch (i) {
		case 0:
		case 1:
		case 2:
			xgene_set_addr_len(exp_desc, i, dma_addr, hw_len);
			break;
		case 3:
			if (split || (fidx != nr_frags)) {
				exp_bufs = xgene_enet_get_exp_bufs(tx_ring);
				xgene_set_addr_len(exp_bufs, idx, dma_addr,
						   hw_len);
				idx++;
				ell_bytes += len;
			} else {
				xgene_set_addr_len(exp_desc, i, dma_addr,
						   hw_len);
			}
			break;
		default:
			xgene_set_addr_len(exp_bufs, idx, dma_addr, hw_len);
			idx++;
			ell_bytes += len;
			break;
		}

		if (split)
			offset += BUFLEN_16K;
	}
	count++;

	if (idx) {
		ll = 1;
		dma_addr = dma_map_single(dev, exp_bufs,
					  sizeof(u64) * MAX_EXP_BUFFS,
					  DMA_TO_DEVICE);
		if (dma_mapping_error(dev, dma_addr)) {
			dev_kfree_skb_any(skb);
			return -EINVAL;
		}
		i = ell_bytes >> LL_BYTES_LSB_LEN;
		exp_desc[2] = cpu_to_le64(SET_VAL(DATAADDR, dma_addr) |
					  SET_VAL(LL_BYTES_MSB, i) |
					  SET_VAL(LL_LEN, idx));
		raw_desc->m2 = cpu_to_le64(SET_VAL(LL_BYTES_LSB, ell_bytes));
	}

out:
	raw_desc->m0 = cpu_to_le64(SET_VAL(LL, ll) | SET_VAL(NV, nv) |
				   SET_VAL(USERINFO, tx_ring->tail));
	tx_ring->cp_ring->cp_skb[tx_ring->tail] = skb;
	pdata->tx_level[tx_ring->cp_ring->index] += count;
	tx_ring->tail = tail;

	return count;
}

static netdev_tx_t xgene_enet_start_xmit(struct sk_buff *skb,
					 struct net_device *ndev)
{
	struct xgene_enet_pdata *pdata = netdev_priv(ndev);
	struct xgene_enet_desc_ring *tx_ring;
	int index = skb->queue_mapping;
	u32 tx_level = pdata->tx_level[index];
	int count;

	tx_ring = pdata->tx_ring[index];
	if (tx_level < pdata->txc_level[index])
		tx_level += ((typeof(pdata->tx_level[index]))~0U);

	if ((tx_level - pdata->txc_level[index]) > pdata->tx_qcnt_hi) {
		netif_stop_subqueue(ndev, index);
		return NETDEV_TX_BUSY;
	}

	if (skb_padto(skb, XGENE_MIN_ENET_FRAME_SIZE))
		return NETDEV_TX_OK;

	count = xgene_enet_setup_tx_desc(tx_ring, skb);
	if (count == -EBUSY)
		return NETDEV_TX_BUSY;

	if (count <= 0) {
		dev_kfree_skb_any(skb);
		return NETDEV_TX_OK;
	}

	skb_tx_timestamp(skb);

	tx_ring->tx_packets++;
	tx_ring->tx_bytes += skb->len;

	pdata->ring_ops->wr_cmd(tx_ring, count);
	return NETDEV_TX_OK;
}

static void xgene_enet_rx_csum(struct sk_buff *skb)
{
	struct net_device *ndev = skb->dev;
	struct iphdr *iph = ip_hdr(skb);

	if (!(ndev->features & NETIF_F_RXCSUM))
		return;

	if (skb->protocol != htons(ETH_P_IP))
		return;

	if (ip_is_fragment(iph))
		return;

	if (iph->protocol != IPPROTO_TCP && iph->protocol != IPPROTO_UDP)
		return;

	skb->ip_summed = CHECKSUM_UNNECESSARY;
}

static void xgene_enet_free_pagepool(struct xgene_enet_desc_ring *buf_pool,
				     struct xgene_enet_raw_desc *raw_desc,
				     struct xgene_enet_raw_desc *exp_desc)
{
	__le64 *desc = (void *)exp_desc;
	dma_addr_t dma_addr;
	struct device *dev;
	struct page *page;
	u16 slots, head;
	u32 frag_size;
	int i;

	if (!buf_pool || !raw_desc || !exp_desc ||
	    (!GET_VAL(NV, le64_to_cpu(raw_desc->m0))))
		return;

	dev = ndev_to_dev(buf_pool->ndev);
	slots = buf_pool->slots - 1;
	head = buf_pool->head;

	for (i = 0; i < 4; i++) {
		frag_size = xgene_enet_get_data_len(le64_to_cpu(desc[i ^ 1]));
		if (!frag_size)
			break;

		dma_addr = GET_VAL(DATAADDR, le64_to_cpu(desc[i ^ 1]));
		dma_unmap_page(dev, dma_addr, PAGE_SIZE, DMA_FROM_DEVICE);

		page = buf_pool->frag_page[head];
		put_page(page);

		buf_pool->frag_page[head] = NULL;
		head = (head + 1) & slots;
	}
	buf_pool->head = head;
}

/* Errata 10GE_10 and ENET_15 - Fix duplicated HW statistic counters */
static bool xgene_enet_errata_10GE_10(struct sk_buff *skb, u32 len, u8 status)
{
	if (status == INGRESS_CRC &&
	    len >= (ETHER_STD_PACKET + 1) &&
	    len <= (ETHER_STD_PACKET + 4) &&
	    skb->protocol == htons(ETH_P_8021Q))
		return true;

	return false;
}

/* Errata 10GE_8 and ENET_11 - allow packet with length <=64B */
static bool xgene_enet_errata_10GE_8(struct sk_buff *skb, u32 len, u8 status)
{
	if (status == INGRESS_PKT_LEN && len == ETHER_MIN_PACKET) {
		if (ntohs(eth_hdr(skb)->h_proto) < 46)
			return true;
	}

	return false;
}

static int xgene_enet_rx_frame(struct xgene_enet_desc_ring *rx_ring,
			       struct xgene_enet_raw_desc *raw_desc,
			       struct xgene_enet_raw_desc *exp_desc)
{
	struct xgene_enet_desc_ring *buf_pool, *page_pool;
	u32 datalen, frag_size, skb_index;
	struct xgene_enet_pdata *pdata;
	struct net_device *ndev;
	dma_addr_t dma_addr;
	struct sk_buff *skb;
	struct device *dev;
	struct page *page;
	u16 slots, head;
	int i, ret = 0;
	__le64 *desc;
	u8 status;
	bool nv;

	ndev = rx_ring->ndev;
	pdata = netdev_priv(ndev);
	dev = ndev_to_dev(rx_ring->ndev);
	buf_pool = rx_ring->buf_pool;
	page_pool = rx_ring->page_pool;

	dma_unmap_single(dev, GET_VAL(DATAADDR, le64_to_cpu(raw_desc->m1)),
			 XGENE_ENET_STD_MTU, DMA_FROM_DEVICE);
	skb_index = GET_VAL(USERINFO, le64_to_cpu(raw_desc->m0));
	skb = buf_pool->rx_skb[skb_index];
	buf_pool->rx_skb[skb_index] = NULL;

	datalen = xgene_enet_get_data_len(le64_to_cpu(raw_desc->m1));

	/* strip off CRC as HW isn't doing this */
	nv = GET_VAL(NV, le64_to_cpu(raw_desc->m0));
	if (!nv)
		datalen -= 4;

	skb_put(skb, datalen);
	prefetch(skb->data - NET_IP_ALIGN);
	skb->protocol = eth_type_trans(skb, ndev);

	/* checking for error */
	status = (GET_VAL(ELERR, le64_to_cpu(raw_desc->m0)) << LERR_LEN) |
		  GET_VAL(LERR, le64_to_cpu(raw_desc->m0));
	if (unlikely(status)) {
		if (xgene_enet_errata_10GE_8(skb, datalen, status)) {
			pdata->false_rflr++;
		} else if (xgene_enet_errata_10GE_10(skb, datalen, status)) {
			pdata->vlan_rjbr++;
		} else {
			dev_kfree_skb_any(skb);
			xgene_enet_free_pagepool(page_pool, raw_desc, exp_desc);
			xgene_enet_parse_error(rx_ring, status);
			rx_ring->rx_dropped++;
			goto out;
		}
	}

	if (!nv)
		goto skip_jumbo;

	slots = page_pool->slots - 1;
	head = page_pool->head;
	desc = (void *)exp_desc;

	for (i = 0; i < 4; i++) {
		frag_size = xgene_enet_get_data_len(le64_to_cpu(desc[i ^ 1]));
		if (!frag_size)
			break;

		dma_addr = GET_VAL(DATAADDR, le64_to_cpu(desc[i ^ 1]));
		dma_unmap_page(dev, dma_addr, PAGE_SIZE, DMA_FROM_DEVICE);

		page = page_pool->frag_page[head];
		skb_add_rx_frag(skb, skb_shinfo(skb)->nr_frags, page, 0,
				frag_size, PAGE_SIZE);

		datalen += frag_size;

		page_pool->frag_page[head] = NULL;
		head = (head + 1) & slots;
	}

	page_pool->head = head;
	rx_ring->npagepool -= skb_shinfo(skb)->nr_frags;

skip_jumbo:
	skb_checksum_none_assert(skb);
	xgene_enet_rx_csum(skb);

	rx_ring->rx_packets++;
	rx_ring->rx_bytes += datalen;
	napi_gro_receive(&rx_ring->napi, skb);

out:
	if (rx_ring->npagepool <= 0) {
		ret = xgene_enet_refill_pagepool(page_pool, NUM_NXTBUFPOOL);
		rx_ring->npagepool = NUM_NXTBUFPOOL;
		if (ret)
			return ret;
	}

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
	struct net_device *ndev = ring->ndev;
	struct xgene_enet_pdata *pdata = netdev_priv(ndev);
	struct xgene_enet_raw_desc *raw_desc, *exp_desc;
	u16 head = ring->head;
	u16 slots = ring->slots - 1;
	int ret, desc_count, count = 0, processed = 0;
	bool is_completion;

	do {
		raw_desc = &ring->raw_desc[head];
		desc_count = 0;
		is_completion = false;
		exp_desc = NULL;
		if (unlikely(xgene_enet_is_desc_slot_empty(raw_desc)))
			break;

		/* read fpqnum field after dataaddr field */
		dma_rmb();
		if (GET_BIT(NV, le64_to_cpu(raw_desc->m0))) {
			head = (head + 1) & slots;
			exp_desc = &ring->raw_desc[head];

			if (unlikely(xgene_enet_is_desc_slot_empty(exp_desc))) {
				head = (head - 1) & slots;
				break;
			}
			dma_rmb();
			count++;
			desc_count++;
		}
		if (is_rx_desc(raw_desc)) {
			ret = xgene_enet_rx_frame(ring, raw_desc, exp_desc);
		} else {
			ret = xgene_enet_tx_completion(ring, raw_desc);
			is_completion = true;
		}
		xgene_enet_mark_desc_slot_empty(raw_desc);
		if (exp_desc)
			xgene_enet_mark_desc_slot_empty(exp_desc);

		head = (head + 1) & slots;
		count++;
		desc_count++;
		processed++;
		if (is_completion)
			pdata->txc_level[ring->index] += desc_count;

		if (ret)
			break;
	} while (--budget);

	if (likely(count)) {
		pdata->ring_ops->wr_cmd(ring, -count);
		ring->head = head;

		if (__netif_subqueue_stopped(ndev, ring->index))
			netif_start_subqueue(ndev, ring->index);
	}

	return processed;
}

static int xgene_enet_napi(struct napi_struct *napi, const int budget)
{
	struct xgene_enet_desc_ring *ring;
	int processed;

	ring = container_of(napi, struct xgene_enet_desc_ring, napi);
	processed = xgene_enet_process_ring(ring, budget);

	if (processed != budget) {
		napi_complete_done(napi, processed);
		enable_irq(ring->irq);
	}

	return processed;
}

static void xgene_enet_timeout(struct net_device *ndev, unsigned int txqueue)
{
	struct xgene_enet_pdata *pdata = netdev_priv(ndev);
	struct netdev_queue *txq;
	int i;

	pdata->mac_ops->reset(pdata);

	for (i = 0; i < pdata->txq_cnt; i++) {
		txq = netdev_get_tx_queue(ndev, i);
		txq_trans_cond_update(txq);
		netif_tx_start_queue(txq);
	}
}

static void xgene_enet_set_irq_name(struct net_device *ndev)
{
	struct xgene_enet_pdata *pdata = netdev_priv(ndev);
	struct xgene_enet_desc_ring *ring;
	int i;

	for (i = 0; i < pdata->rxq_cnt; i++) {
		ring = pdata->rx_ring[i];
		if (!pdata->cq_cnt) {
			snprintf(ring->irq_name, IRQ_ID_SIZE, "%s-rx-txc",
				 ndev->name);
		} else {
			snprintf(ring->irq_name, IRQ_ID_SIZE, "%s-rx-%d",
				 ndev->name, i);
		}
	}

	for (i = 0; i < pdata->cq_cnt; i++) {
		ring = pdata->tx_ring[i]->cp_ring;
		snprintf(ring->irq_name, IRQ_ID_SIZE, "%s-txc-%d",
			 ndev->name, i);
	}
}

static int xgene_enet_register_irq(struct net_device *ndev)
{
	struct xgene_enet_pdata *pdata = netdev_priv(ndev);
	struct device *dev = ndev_to_dev(ndev);
	struct xgene_enet_desc_ring *ring;
	int ret = 0, i;

	xgene_enet_set_irq_name(ndev);
	for (i = 0; i < pdata->rxq_cnt; i++) {
		ring = pdata->rx_ring[i];
		irq_set_status_flags(ring->irq, IRQ_DISABLE_UNLAZY);
		ret = devm_request_irq(dev, ring->irq, xgene_enet_rx_irq,
				       0, ring->irq_name, ring);
		if (ret) {
			netdev_err(ndev, "Failed to request irq %s\n",
				   ring->irq_name);
		}
	}

	for (i = 0; i < pdata->cq_cnt; i++) {
		ring = pdata->tx_ring[i]->cp_ring;
		irq_set_status_flags(ring->irq, IRQ_DISABLE_UNLAZY);
		ret = devm_request_irq(dev, ring->irq, xgene_enet_rx_irq,
				       0, ring->irq_name, ring);
		if (ret) {
			netdev_err(ndev, "Failed to request irq %s\n",
				   ring->irq_name);
		}
	}

	return ret;
}

static void xgene_enet_free_irq(struct net_device *ndev)
{
	struct xgene_enet_pdata *pdata;
	struct xgene_enet_desc_ring *ring;
	struct device *dev;
	int i;

	pdata = netdev_priv(ndev);
	dev = ndev_to_dev(ndev);

	for (i = 0; i < pdata->rxq_cnt; i++) {
		ring = pdata->rx_ring[i];
		irq_clear_status_flags(ring->irq, IRQ_DISABLE_UNLAZY);
		devm_free_irq(dev, ring->irq, ring);
	}

	for (i = 0; i < pdata->cq_cnt; i++) {
		ring = pdata->tx_ring[i]->cp_ring;
		irq_clear_status_flags(ring->irq, IRQ_DISABLE_UNLAZY);
		devm_free_irq(dev, ring->irq, ring);
	}
}

static void xgene_enet_napi_enable(struct xgene_enet_pdata *pdata)
{
	struct napi_struct *napi;
	int i;

	for (i = 0; i < pdata->rxq_cnt; i++) {
		napi = &pdata->rx_ring[i]->napi;
		napi_enable(napi);
	}

	for (i = 0; i < pdata->cq_cnt; i++) {
		napi = &pdata->tx_ring[i]->cp_ring->napi;
		napi_enable(napi);
	}
}

static void xgene_enet_napi_disable(struct xgene_enet_pdata *pdata)
{
	struct napi_struct *napi;
	int i;

	for (i = 0; i < pdata->rxq_cnt; i++) {
		napi = &pdata->rx_ring[i]->napi;
		napi_disable(napi);
	}

	for (i = 0; i < pdata->cq_cnt; i++) {
		napi = &pdata->tx_ring[i]->cp_ring->napi;
		napi_disable(napi);
	}
}

static int xgene_enet_open(struct net_device *ndev)
{
	struct xgene_enet_pdata *pdata = netdev_priv(ndev);
	const struct xgene_mac_ops *mac_ops = pdata->mac_ops;
	int ret;

	ret = netif_set_real_num_tx_queues(ndev, pdata->txq_cnt);
	if (ret)
		return ret;

	ret = netif_set_real_num_rx_queues(ndev, pdata->rxq_cnt);
	if (ret)
		return ret;

	xgene_enet_napi_enable(pdata);
	ret = xgene_enet_register_irq(ndev);
	if (ret) {
		xgene_enet_napi_disable(pdata);
		return ret;
	}

	if (ndev->phydev) {
		phy_start(ndev->phydev);
	} else {
		schedule_delayed_work(&pdata->link_work, PHY_POLL_LINK_OFF);
		netif_carrier_off(ndev);
	}

	mac_ops->tx_enable(pdata);
	mac_ops->rx_enable(pdata);
	netif_tx_start_all_queues(ndev);

	return ret;
}

static int xgene_enet_close(struct net_device *ndev)
{
	struct xgene_enet_pdata *pdata = netdev_priv(ndev);
	const struct xgene_mac_ops *mac_ops = pdata->mac_ops;
	int i;

	netif_tx_stop_all_queues(ndev);
	mac_ops->tx_disable(pdata);
	mac_ops->rx_disable(pdata);

	if (ndev->phydev)
		phy_stop(ndev->phydev);
	else
		cancel_delayed_work_sync(&pdata->link_work);

	xgene_enet_free_irq(ndev);
	xgene_enet_napi_disable(pdata);
	for (i = 0; i < pdata->rxq_cnt; i++)
		xgene_enet_process_ring(pdata->rx_ring[i], -1);

	return 0;
}
static void xgene_enet_delete_ring(struct xgene_enet_desc_ring *ring)
{
	struct xgene_enet_pdata *pdata;
	struct device *dev;

	pdata = netdev_priv(ring->ndev);
	dev = ndev_to_dev(ring->ndev);

	pdata->ring_ops->clear(ring);
	dmam_free_coherent(dev, ring->size, ring->desc_addr, ring->dma);
}

static void xgene_enet_delete_desc_rings(struct xgene_enet_pdata *pdata)
{
	struct xgene_enet_desc_ring *buf_pool, *page_pool;
	struct xgene_enet_desc_ring *ring;
	int i;

	for (i = 0; i < pdata->txq_cnt; i++) {
		ring = pdata->tx_ring[i];
		if (ring) {
			xgene_enet_delete_ring(ring);
			pdata->port_ops->clear(pdata, ring);
			if (pdata->cq_cnt)
				xgene_enet_delete_ring(ring->cp_ring);
			pdata->tx_ring[i] = NULL;
		}

	}

	for (i = 0; i < pdata->rxq_cnt; i++) {
		ring = pdata->rx_ring[i];
		if (ring) {
			page_pool = ring->page_pool;
			if (page_pool) {
				xgene_enet_delete_pagepool(page_pool);
				xgene_enet_delete_ring(page_pool);
				pdata->port_ops->clear(pdata, page_pool);
			}

			buf_pool = ring->buf_pool;
			xgene_enet_delete_bufpool(buf_pool);
			xgene_enet_delete_ring(buf_pool);
			pdata->port_ops->clear(pdata, buf_pool);

			xgene_enet_delete_ring(ring);
			pdata->rx_ring[i] = NULL;
		}

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
	struct xgene_enet_pdata *pdata;
	struct device *dev;

	if (!ring)
		return;

	dev = ndev_to_dev(ring->ndev);
	pdata = netdev_priv(ring->ndev);

	if (ring->desc_addr) {
		pdata->ring_ops->clear(ring);
		dmam_free_coherent(dev, ring->size, ring->desc_addr, ring->dma);
	}
	devm_kfree(dev, ring);
}

static void xgene_enet_free_desc_rings(struct xgene_enet_pdata *pdata)
{
	struct xgene_enet_desc_ring *page_pool;
	struct device *dev = &pdata->pdev->dev;
	struct xgene_enet_desc_ring *ring;
	void *p;
	int i;

	for (i = 0; i < pdata->txq_cnt; i++) {
		ring = pdata->tx_ring[i];
		if (ring) {
			if (ring->cp_ring && ring->cp_ring->cp_skb)
				devm_kfree(dev, ring->cp_ring->cp_skb);

			if (ring->cp_ring && pdata->cq_cnt)
				xgene_enet_free_desc_ring(ring->cp_ring);

			xgene_enet_free_desc_ring(ring);
		}

	}

	for (i = 0; i < pdata->rxq_cnt; i++) {
		ring = pdata->rx_ring[i];
		if (ring) {
			if (ring->buf_pool) {
				if (ring->buf_pool->rx_skb)
					devm_kfree(dev, ring->buf_pool->rx_skb);

				xgene_enet_free_desc_ring(ring->buf_pool);
			}

			page_pool = ring->page_pool;
			if (page_pool) {
				p = page_pool->frag_page;
				if (p)
					devm_kfree(dev, p);

				p = page_pool->frag_dma_addr;
				if (p)
					devm_kfree(dev, p);
			}

			xgene_enet_free_desc_ring(ring);
		}
	}
}

static bool is_irq_mbox_required(struct xgene_enet_pdata *pdata,
				 struct xgene_enet_desc_ring *ring)
{
	if ((pdata->enet_id == XGENE_ENET2) &&
	    (xgene_enet_ring_owner(ring->id) == RING_OWNER_CPU)) {
		return true;
	}

	return false;
}

static void __iomem *xgene_enet_ring_cmd_base(struct xgene_enet_pdata *pdata,
					      struct xgene_enet_desc_ring *ring)
{
	u8 num_ring_id_shift = pdata->ring_ops->num_ring_id_shift;

	return pdata->ring_cmd_addr + (ring->num << num_ring_id_shift);
}

static struct xgene_enet_desc_ring *xgene_enet_create_desc_ring(
			struct net_device *ndev, u32 ring_num,
			enum xgene_enet_ring_cfgsize cfgsize, u32 ring_id)
{
	struct xgene_enet_pdata *pdata = netdev_priv(ndev);
	struct device *dev = ndev_to_dev(ndev);
	struct xgene_enet_desc_ring *ring;
	void *irq_mbox_addr;
	int size;

	size = xgene_enet_get_ring_size(dev, cfgsize);
	if (size < 0)
		return NULL;

	ring = devm_kzalloc(dev, sizeof(struct xgene_enet_desc_ring),
			    GFP_KERNEL);
	if (!ring)
		return NULL;

	ring->ndev = ndev;
	ring->num = ring_num;
	ring->cfgsize = cfgsize;
	ring->id = ring_id;

	ring->desc_addr = dmam_alloc_coherent(dev, size, &ring->dma,
					      GFP_KERNEL | __GFP_ZERO);
	if (!ring->desc_addr) {
		devm_kfree(dev, ring);
		return NULL;
	}
	ring->size = size;

	if (is_irq_mbox_required(pdata, ring)) {
		irq_mbox_addr = dmam_alloc_coherent(dev, INTR_MBOX_SIZE,
						    &ring->irq_mbox_dma,
						    GFP_KERNEL | __GFP_ZERO);
		if (!irq_mbox_addr) {
			dmam_free_coherent(dev, size, ring->desc_addr,
					   ring->dma);
			devm_kfree(dev, ring);
			return NULL;
		}
		ring->irq_mbox_addr = irq_mbox_addr;
	}

	ring->cmd_base = xgene_enet_ring_cmd_base(pdata, ring);
	ring->cmd = ring->cmd_base + INC_DEC_CMD_ADDR;
	ring = pdata->ring_ops->setup(ring);
	netdev_dbg(ndev, "ring info: num=%d  size=%d  id=%d  slots=%d\n",
		   ring->num, ring->size, ring->id, ring->slots);

	return ring;
}

static u16 xgene_enet_get_ring_id(enum xgene_ring_owner owner, u8 bufnum)
{
	return (owner << 6) | (bufnum & GENMASK(5, 0));
}

static enum xgene_ring_owner xgene_derive_ring_owner(struct xgene_enet_pdata *p)
{
	enum xgene_ring_owner owner;

	if (p->enet_id == XGENE_ENET1) {
		switch (p->phy_mode) {
		case PHY_INTERFACE_MODE_SGMII:
			owner = RING_OWNER_ETH0;
			break;
		default:
			owner = (!p->port_id) ? RING_OWNER_ETH0 :
						RING_OWNER_ETH1;
			break;
		}
	} else {
		owner = (!p->port_id) ? RING_OWNER_ETH0 : RING_OWNER_ETH1;
	}

	return owner;
}

static u8 xgene_start_cpu_bufnum(struct xgene_enet_pdata *pdata)
{
	struct device *dev = &pdata->pdev->dev;
	u32 cpu_bufnum;
	int ret;

	ret = device_property_read_u32(dev, "channel", &cpu_bufnum);

	return (!ret) ? cpu_bufnum : pdata->cpu_bufnum;
}

static int xgene_enet_create_desc_rings(struct net_device *ndev)
{
	struct xgene_enet_desc_ring *rx_ring, *tx_ring, *cp_ring;
	struct xgene_enet_pdata *pdata = netdev_priv(ndev);
	struct xgene_enet_desc_ring *page_pool = NULL;
	struct xgene_enet_desc_ring *buf_pool = NULL;
	struct device *dev = ndev_to_dev(ndev);
	u8 eth_bufnum = pdata->eth_bufnum;
	u8 bp_bufnum = pdata->bp_bufnum;
	u16 ring_num = pdata->ring_num;
	enum xgene_ring_owner owner;
	dma_addr_t dma_exp_bufs;
	u16 ring_id, slots;
	__le64 *exp_bufs;
	int i, ret, size;
	u8 cpu_bufnum;

	cpu_bufnum = xgene_start_cpu_bufnum(pdata);

	for (i = 0; i < pdata->rxq_cnt; i++) {
		/* allocate rx descriptor ring */
		owner = xgene_derive_ring_owner(pdata);
		ring_id = xgene_enet_get_ring_id(RING_OWNER_CPU, cpu_bufnum++);
		rx_ring = xgene_enet_create_desc_ring(ndev, ring_num++,
						      RING_CFGSIZE_16KB,
						      ring_id);
		if (!rx_ring) {
			ret = -ENOMEM;
			goto err;
		}

		/* allocate buffer pool for receiving packets */
		owner = xgene_derive_ring_owner(pdata);
		ring_id = xgene_enet_get_ring_id(owner, bp_bufnum++);
		buf_pool = xgene_enet_create_desc_ring(ndev, ring_num++,
						       RING_CFGSIZE_16KB,
						       ring_id);
		if (!buf_pool) {
			ret = -ENOMEM;
			goto err;
		}

		rx_ring->nbufpool = NUM_BUFPOOL;
		rx_ring->npagepool = NUM_NXTBUFPOOL;
		rx_ring->irq = pdata->irqs[i];
		buf_pool->rx_skb = devm_kcalloc(dev, buf_pool->slots,
						sizeof(struct sk_buff *),
						GFP_KERNEL);
		if (!buf_pool->rx_skb) {
			ret = -ENOMEM;
			goto err;
		}

		buf_pool->dst_ring_num = xgene_enet_dst_ring_num(buf_pool);
		rx_ring->buf_pool = buf_pool;
		pdata->rx_ring[i] = rx_ring;

		if ((pdata->enet_id == XGENE_ENET1 &&  pdata->rxq_cnt > 4) ||
		    (pdata->enet_id == XGENE_ENET2 &&  pdata->rxq_cnt > 16)) {
			break;
		}

		/* allocate next buffer pool for jumbo packets */
		owner = xgene_derive_ring_owner(pdata);
		ring_id = xgene_enet_get_ring_id(owner, bp_bufnum++);
		page_pool = xgene_enet_create_desc_ring(ndev, ring_num++,
							RING_CFGSIZE_16KB,
							ring_id);
		if (!page_pool) {
			ret = -ENOMEM;
			goto err;
		}

		slots = page_pool->slots;
		page_pool->frag_page = devm_kcalloc(dev, slots,
						    sizeof(struct page *),
						    GFP_KERNEL);
		if (!page_pool->frag_page) {
			ret = -ENOMEM;
			goto err;
		}

		page_pool->frag_dma_addr = devm_kcalloc(dev, slots,
							sizeof(dma_addr_t),
							GFP_KERNEL);
		if (!page_pool->frag_dma_addr) {
			ret = -ENOMEM;
			goto err;
		}

		page_pool->dst_ring_num = xgene_enet_dst_ring_num(page_pool);
		rx_ring->page_pool = page_pool;
	}

	for (i = 0; i < pdata->txq_cnt; i++) {
		/* allocate tx descriptor ring */
		owner = xgene_derive_ring_owner(pdata);
		ring_id = xgene_enet_get_ring_id(owner, eth_bufnum++);
		tx_ring = xgene_enet_create_desc_ring(ndev, ring_num++,
						      RING_CFGSIZE_16KB,
						      ring_id);
		if (!tx_ring) {
			ret = -ENOMEM;
			goto err;
		}

		size = (tx_ring->slots / 2) * sizeof(__le64) * MAX_EXP_BUFFS;
		exp_bufs = dmam_alloc_coherent(dev, size, &dma_exp_bufs,
					       GFP_KERNEL | __GFP_ZERO);
		if (!exp_bufs) {
			ret = -ENOMEM;
			goto err;
		}
		tx_ring->exp_bufs = exp_bufs;

		pdata->tx_ring[i] = tx_ring;

		if (!pdata->cq_cnt) {
			cp_ring = pdata->rx_ring[i];
		} else {
			/* allocate tx completion descriptor ring */
			ring_id = xgene_enet_get_ring_id(RING_OWNER_CPU,
							 cpu_bufnum++);
			cp_ring = xgene_enet_create_desc_ring(ndev, ring_num++,
							      RING_CFGSIZE_16KB,
							      ring_id);
			if (!cp_ring) {
				ret = -ENOMEM;
				goto err;
			}

			cp_ring->irq = pdata->irqs[pdata->rxq_cnt + i];
			cp_ring->index = i;
		}

		cp_ring->cp_skb = devm_kcalloc(dev, tx_ring->slots,
					       sizeof(struct sk_buff *),
					       GFP_KERNEL);
		if (!cp_ring->cp_skb) {
			ret = -ENOMEM;
			goto err;
		}

		size = sizeof(dma_addr_t) * MAX_SKB_FRAGS;
		cp_ring->frag_dma_addr = devm_kcalloc(dev, tx_ring->slots,
						      size, GFP_KERNEL);
		if (!cp_ring->frag_dma_addr) {
			devm_kfree(dev, cp_ring->cp_skb);
			ret = -ENOMEM;
			goto err;
		}

		tx_ring->cp_ring = cp_ring;
		tx_ring->dst_ring_num = xgene_enet_dst_ring_num(cp_ring);
	}

	if (pdata->ring_ops->coalesce)
		pdata->ring_ops->coalesce(pdata->tx_ring[0]);
	pdata->tx_qcnt_hi = pdata->tx_ring[0]->slots - 128;

	return 0;

err:
	xgene_enet_free_desc_rings(pdata);
	return ret;
}

static void xgene_enet_get_stats64(
			struct net_device *ndev,
			struct rtnl_link_stats64 *stats)
{
	struct xgene_enet_pdata *pdata = netdev_priv(ndev);
	struct xgene_enet_desc_ring *ring;
	int i;

	for (i = 0; i < pdata->txq_cnt; i++) {
		ring = pdata->tx_ring[i];
		if (ring) {
			stats->tx_packets += ring->tx_packets;
			stats->tx_bytes += ring->tx_bytes;
			stats->tx_dropped += ring->tx_dropped;
			stats->tx_errors += ring->tx_errors;
		}
	}

	for (i = 0; i < pdata->rxq_cnt; i++) {
		ring = pdata->rx_ring[i];
		if (ring) {
			stats->rx_packets += ring->rx_packets;
			stats->rx_bytes += ring->rx_bytes;
			stats->rx_dropped += ring->rx_dropped;
			stats->rx_errors += ring->rx_errors +
				ring->rx_length_errors +
				ring->rx_crc_errors +
				ring->rx_frame_errors +
				ring->rx_fifo_errors;
			stats->rx_length_errors += ring->rx_length_errors;
			stats->rx_crc_errors += ring->rx_crc_errors;
			stats->rx_frame_errors += ring->rx_frame_errors;
			stats->rx_fifo_errors += ring->rx_fifo_errors;
		}
	}
}

static int xgene_enet_set_mac_address(struct net_device *ndev, void *addr)
{
	struct xgene_enet_pdata *pdata = netdev_priv(ndev);
	int ret;

	ret = eth_mac_addr(ndev, addr);
	if (ret)
		return ret;
	pdata->mac_ops->set_mac_addr(pdata);

	return ret;
}

static int xgene_change_mtu(struct net_device *ndev, int new_mtu)
{
	struct xgene_enet_pdata *pdata = netdev_priv(ndev);
	int frame_size;

	if (!netif_running(ndev))
		return 0;

	frame_size = (new_mtu > ETH_DATA_LEN) ? (new_mtu + 18) : 0x600;

	xgene_enet_close(ndev);
	ndev->mtu = new_mtu;
	pdata->mac_ops->set_framesize(pdata, frame_size);
	xgene_enet_open(ndev);

	return 0;
}

static const struct net_device_ops xgene_ndev_ops = {
	.ndo_open = xgene_enet_open,
	.ndo_stop = xgene_enet_close,
	.ndo_start_xmit = xgene_enet_start_xmit,
	.ndo_tx_timeout = xgene_enet_timeout,
	.ndo_get_stats64 = xgene_enet_get_stats64,
	.ndo_change_mtu = xgene_change_mtu,
	.ndo_set_mac_address = xgene_enet_set_mac_address,
};

#ifdef CONFIG_ACPI
static void xgene_get_port_id_acpi(struct device *dev,
				  struct xgene_enet_pdata *pdata)
{
	acpi_status status;
	u64 temp;

	status = acpi_evaluate_integer(ACPI_HANDLE(dev), "_SUN", NULL, &temp);
	if (ACPI_FAILURE(status)) {
		pdata->port_id = 0;
	} else {
		pdata->port_id = temp;
	}

	return;
}
#endif

static void xgene_get_port_id_dt(struct device *dev, struct xgene_enet_pdata *pdata)
{
	u32 id = 0;

	of_property_read_u32(dev->of_node, "port-id", &id);

	pdata->port_id = id & BIT(0);

	return;
}

static int xgene_get_tx_delay(struct xgene_enet_pdata *pdata)
{
	struct device *dev = &pdata->pdev->dev;
	int delay, ret;

	ret = device_property_read_u32(dev, "tx-delay", &delay);
	if (ret) {
		pdata->tx_delay = 4;
		return 0;
	}

	if (delay < 0 || delay > 7) {
		dev_err(dev, "Invalid tx-delay specified\n");
		return -EINVAL;
	}

	pdata->tx_delay = delay;

	return 0;
}

static int xgene_get_rx_delay(struct xgene_enet_pdata *pdata)
{
	struct device *dev = &pdata->pdev->dev;
	int delay, ret;

	ret = device_property_read_u32(dev, "rx-delay", &delay);
	if (ret) {
		pdata->rx_delay = 2;
		return 0;
	}

	if (delay < 0 || delay > 7) {
		dev_err(dev, "Invalid rx-delay specified\n");
		return -EINVAL;
	}

	pdata->rx_delay = delay;

	return 0;
}

static int xgene_enet_get_irqs(struct xgene_enet_pdata *pdata)
{
	struct platform_device *pdev = pdata->pdev;
	int i, ret, max_irqs;

	if (phy_interface_mode_is_rgmii(pdata->phy_mode))
		max_irqs = 1;
	else if (pdata->phy_mode == PHY_INTERFACE_MODE_SGMII)
		max_irqs = 2;
	else
		max_irqs = XGENE_MAX_ENET_IRQ;

	for (i = 0; i < max_irqs; i++) {
		ret = platform_get_irq(pdev, i);
		if (ret < 0) {
			if (pdata->phy_mode == PHY_INTERFACE_MODE_XGMII) {
				max_irqs = i;
				pdata->rxq_cnt = max_irqs / 2;
				pdata->txq_cnt = max_irqs / 2;
				pdata->cq_cnt = max_irqs / 2;
				break;
			}
			return ret;
		}
		pdata->irqs[i] = ret;
	}

	return 0;
}

static void xgene_enet_check_phy_handle(struct xgene_enet_pdata *pdata)
{
	int ret;

	if (pdata->phy_mode == PHY_INTERFACE_MODE_XGMII)
		return;

	if (!IS_ENABLED(CONFIG_MDIO_XGENE))
		return;

	ret = xgene_enet_phy_connect(pdata->ndev);
	if (!ret)
		pdata->mdio_driver = true;
}

static void xgene_enet_gpiod_get(struct xgene_enet_pdata *pdata)
{
	struct device *dev = &pdata->pdev->dev;

	pdata->sfp_gpio_en = false;
	if (pdata->phy_mode != PHY_INTERFACE_MODE_XGMII ||
	    (!device_property_present(dev, "sfp-gpios") &&
	     !device_property_present(dev, "rxlos-gpios")))
		return;

	pdata->sfp_gpio_en = true;
	pdata->sfp_rdy = gpiod_get(dev, "rxlos", GPIOD_IN);
	if (IS_ERR(pdata->sfp_rdy))
		pdata->sfp_rdy = gpiod_get(dev, "sfp", GPIOD_IN);
}

static int xgene_enet_get_resources(struct xgene_enet_pdata *pdata)
{
	struct platform_device *pdev;
	struct net_device *ndev;
	struct device *dev;
	struct resource *res;
	void __iomem *base_addr;
	u32 offset;
	int ret = 0;

	pdev = pdata->pdev;
	dev = &pdev->dev;
	ndev = pdata->ndev;

	res = platform_get_resource(pdev, IORESOURCE_MEM, RES_ENET_CSR);
	if (!res) {
		dev_err(dev, "Resource enet_csr not defined\n");
		return -ENODEV;
	}
	pdata->base_addr = devm_ioremap(dev, res->start, resource_size(res));
	if (!pdata->base_addr) {
		dev_err(dev, "Unable to retrieve ENET Port CSR region\n");
		return -ENOMEM;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, RES_RING_CSR);
	if (!res) {
		dev_err(dev, "Resource ring_csr not defined\n");
		return -ENODEV;
	}
	pdata->ring_csr_addr = devm_ioremap(dev, res->start,
							resource_size(res));
	if (!pdata->ring_csr_addr) {
		dev_err(dev, "Unable to retrieve ENET Ring CSR region\n");
		return -ENOMEM;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, RES_RING_CMD);
	if (!res) {
		dev_err(dev, "Resource ring_cmd not defined\n");
		return -ENODEV;
	}
	pdata->ring_cmd_addr = devm_ioremap(dev, res->start,
							resource_size(res));
	if (!pdata->ring_cmd_addr) {
		dev_err(dev, "Unable to retrieve ENET Ring command region\n");
		return -ENOMEM;
	}

	if (dev->of_node)
		xgene_get_port_id_dt(dev, pdata);
#ifdef CONFIG_ACPI
	else
		xgene_get_port_id_acpi(dev, pdata);
#endif

	if (device_get_ethdev_address(dev, ndev))
		eth_hw_addr_random(ndev);

	memcpy(ndev->perm_addr, ndev->dev_addr, ndev->addr_len);

	pdata->phy_mode = device_get_phy_mode(dev);
	if (pdata->phy_mode < 0) {
		dev_err(dev, "Unable to get phy-connection-type\n");
		return pdata->phy_mode;
	}
	if (!phy_interface_mode_is_rgmii(pdata->phy_mode) &&
	    pdata->phy_mode != PHY_INTERFACE_MODE_SGMII &&
	    pdata->phy_mode != PHY_INTERFACE_MODE_XGMII) {
		dev_err(dev, "Incorrect phy-connection-type specified\n");
		return -ENODEV;
	}

	ret = xgene_get_tx_delay(pdata);
	if (ret)
		return ret;

	ret = xgene_get_rx_delay(pdata);
	if (ret)
		return ret;

	ret = xgene_enet_get_irqs(pdata);
	if (ret)
		return ret;

	xgene_enet_gpiod_get(pdata);

	pdata->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(pdata->clk)) {
		if (pdata->phy_mode != PHY_INTERFACE_MODE_SGMII) {
			/* Abort if the clock is defined but couldn't be
			 * retrived. Always abort if the clock is missing on
			 * DT system as the driver can't cope with this case.
			 */
			if (PTR_ERR(pdata->clk) != -ENOENT || dev->of_node)
				return PTR_ERR(pdata->clk);
			/* Firmware may have set up the clock already. */
			dev_info(dev, "clocks have been setup already\n");
		}
	}

	if (pdata->phy_mode != PHY_INTERFACE_MODE_XGMII)
		base_addr = pdata->base_addr - (pdata->port_id * MAC_OFFSET);
	else
		base_addr = pdata->base_addr;
	pdata->eth_csr_addr = base_addr + BLOCK_ETH_CSR_OFFSET;
	pdata->cle.base = base_addr + BLOCK_ETH_CLE_CSR_OFFSET;
	pdata->eth_ring_if_addr = base_addr + BLOCK_ETH_RING_IF_OFFSET;
	pdata->eth_diag_csr_addr = base_addr + BLOCK_ETH_DIAG_CSR_OFFSET;
	if (phy_interface_mode_is_rgmii(pdata->phy_mode) ||
	    pdata->phy_mode == PHY_INTERFACE_MODE_SGMII) {
		pdata->mcx_mac_addr = pdata->base_addr + BLOCK_ETH_MAC_OFFSET;
		pdata->mcx_stats_addr =
			pdata->base_addr + BLOCK_ETH_STATS_OFFSET;
		offset = (pdata->enet_id == XGENE_ENET1) ?
			  BLOCK_ETH_MAC_CSR_OFFSET :
			  X2_BLOCK_ETH_MAC_CSR_OFFSET;
		pdata->mcx_mac_csr_addr = base_addr + offset;
	} else {
		pdata->mcx_mac_addr = base_addr + BLOCK_AXG_MAC_OFFSET;
		pdata->mcx_stats_addr = base_addr + BLOCK_AXG_STATS_OFFSET;
		pdata->mcx_mac_csr_addr = base_addr + BLOCK_AXG_MAC_CSR_OFFSET;
		pdata->pcs_addr = base_addr + BLOCK_PCS_OFFSET;
	}
	pdata->rx_buff_cnt = NUM_PKT_BUF;

	return 0;
}

static int xgene_enet_init_hw(struct xgene_enet_pdata *pdata)
{
	struct xgene_enet_cle *enet_cle = &pdata->cle;
	struct xgene_enet_desc_ring *page_pool;
	struct net_device *ndev = pdata->ndev;
	struct xgene_enet_desc_ring *buf_pool;
	u16 dst_ring_num, ring_id;
	int i, ret;
	u32 count;

	ret = pdata->port_ops->reset(pdata);
	if (ret)
		return ret;

	ret = xgene_enet_create_desc_rings(ndev);
	if (ret) {
		netdev_err(ndev, "Error in ring configuration\n");
		return ret;
	}

	/* setup buffer pool */
	for (i = 0; i < pdata->rxq_cnt; i++) {
		buf_pool = pdata->rx_ring[i]->buf_pool;
		xgene_enet_init_bufpool(buf_pool);
		page_pool = pdata->rx_ring[i]->page_pool;
		xgene_enet_init_bufpool(page_pool);

		count = pdata->rx_buff_cnt;
		ret = xgene_enet_refill_bufpool(buf_pool, count);
		if (ret)
			goto err;

		ret = xgene_enet_refill_pagepool(page_pool, count);
		if (ret)
			goto err;

	}

	dst_ring_num = xgene_enet_dst_ring_num(pdata->rx_ring[0]);
	buf_pool = pdata->rx_ring[0]->buf_pool;
	if (pdata->phy_mode == PHY_INTERFACE_MODE_XGMII) {
		/* Initialize and Enable  PreClassifier Tree */
		enet_cle->max_nodes = 512;
		enet_cle->max_dbptrs = 1024;
		enet_cle->parsers = 3;
		enet_cle->active_parser = PARSER_ALL;
		enet_cle->ptree.start_node = 0;
		enet_cle->ptree.start_dbptr = 0;
		enet_cle->jump_bytes = 8;
		ret = pdata->cle_ops->cle_init(pdata);
		if (ret) {
			netdev_err(ndev, "Preclass Tree init error\n");
			goto err;
		}

	} else {
		dst_ring_num = xgene_enet_dst_ring_num(pdata->rx_ring[0]);
		buf_pool = pdata->rx_ring[0]->buf_pool;
		page_pool = pdata->rx_ring[0]->page_pool;
		ring_id = (page_pool) ? page_pool->id : 0;
		pdata->port_ops->cle_bypass(pdata, dst_ring_num,
					    buf_pool->id, ring_id);
	}

	ndev->max_mtu = XGENE_ENET_MAX_MTU;
	pdata->phy_speed = SPEED_UNKNOWN;
	pdata->mac_ops->init(pdata);

	return ret;

err:
	xgene_enet_delete_desc_rings(pdata);
	return ret;
}

static void xgene_enet_setup_ops(struct xgene_enet_pdata *pdata)
{
	switch (pdata->phy_mode) {
	case PHY_INTERFACE_MODE_RGMII:
	case PHY_INTERFACE_MODE_RGMII_ID:
	case PHY_INTERFACE_MODE_RGMII_RXID:
	case PHY_INTERFACE_MODE_RGMII_TXID:
		pdata->mac_ops = &xgene_gmac_ops;
		pdata->port_ops = &xgene_gport_ops;
		pdata->rm = RM3;
		pdata->rxq_cnt = 1;
		pdata->txq_cnt = 1;
		pdata->cq_cnt = 0;
		break;
	case PHY_INTERFACE_MODE_SGMII:
		pdata->mac_ops = &xgene_sgmac_ops;
		pdata->port_ops = &xgene_sgport_ops;
		pdata->rm = RM1;
		pdata->rxq_cnt = 1;
		pdata->txq_cnt = 1;
		pdata->cq_cnt = 1;
		break;
	default:
		pdata->mac_ops = &xgene_xgmac_ops;
		pdata->port_ops = &xgene_xgport_ops;
		pdata->cle_ops = &xgene_cle3in_ops;
		pdata->rm = RM0;
		if (!pdata->rxq_cnt) {
			pdata->rxq_cnt = XGENE_NUM_RX_RING;
			pdata->txq_cnt = XGENE_NUM_TX_RING;
			pdata->cq_cnt = XGENE_NUM_TXC_RING;
		}
		break;
	}

	if (pdata->enet_id == XGENE_ENET1) {
		switch (pdata->port_id) {
		case 0:
			if (pdata->phy_mode == PHY_INTERFACE_MODE_XGMII) {
				pdata->cpu_bufnum = X2_START_CPU_BUFNUM_0;
				pdata->eth_bufnum = X2_START_ETH_BUFNUM_0;
				pdata->bp_bufnum = X2_START_BP_BUFNUM_0;
				pdata->ring_num = START_RING_NUM_0;
			} else {
				pdata->cpu_bufnum = START_CPU_BUFNUM_0;
				pdata->eth_bufnum = START_ETH_BUFNUM_0;
				pdata->bp_bufnum = START_BP_BUFNUM_0;
				pdata->ring_num = START_RING_NUM_0;
			}
			break;
		case 1:
			if (pdata->phy_mode == PHY_INTERFACE_MODE_XGMII) {
				pdata->cpu_bufnum = XG_START_CPU_BUFNUM_1;
				pdata->eth_bufnum = XG_START_ETH_BUFNUM_1;
				pdata->bp_bufnum = XG_START_BP_BUFNUM_1;
				pdata->ring_num = XG_START_RING_NUM_1;
			} else {
				pdata->cpu_bufnum = START_CPU_BUFNUM_1;
				pdata->eth_bufnum = START_ETH_BUFNUM_1;
				pdata->bp_bufnum = START_BP_BUFNUM_1;
				pdata->ring_num = START_RING_NUM_1;
			}
			break;
		default:
			break;
		}
		pdata->ring_ops = &xgene_ring1_ops;
	} else {
		switch (pdata->port_id) {
		case 0:
			pdata->cpu_bufnum = X2_START_CPU_BUFNUM_0;
			pdata->eth_bufnum = X2_START_ETH_BUFNUM_0;
			pdata->bp_bufnum = X2_START_BP_BUFNUM_0;
			pdata->ring_num = X2_START_RING_NUM_0;
			break;
		case 1:
			pdata->cpu_bufnum = X2_START_CPU_BUFNUM_1;
			pdata->eth_bufnum = X2_START_ETH_BUFNUM_1;
			pdata->bp_bufnum = X2_START_BP_BUFNUM_1;
			pdata->ring_num = X2_START_RING_NUM_1;
			break;
		default:
			break;
		}
		pdata->rm = RM0;
		pdata->ring_ops = &xgene_ring2_ops;
	}
}

static void xgene_enet_napi_add(struct xgene_enet_pdata *pdata)
{
	struct napi_struct *napi;
	int i;

	for (i = 0; i < pdata->rxq_cnt; i++) {
		napi = &pdata->rx_ring[i]->napi;
		netif_napi_add(pdata->ndev, napi, xgene_enet_napi);
	}

	for (i = 0; i < pdata->cq_cnt; i++) {
		napi = &pdata->tx_ring[i]->cp_ring->napi;
		netif_napi_add(pdata->ndev, napi, xgene_enet_napi);
	}
}

#ifdef CONFIG_ACPI
static const struct acpi_device_id xgene_enet_acpi_match[] = {
	{ "APMC0D05", XGENE_ENET1},
	{ "APMC0D30", XGENE_ENET1},
	{ "APMC0D31", XGENE_ENET1},
	{ "APMC0D3F", XGENE_ENET1},
	{ "APMC0D26", XGENE_ENET2},
	{ "APMC0D25", XGENE_ENET2},
	{ }
};
MODULE_DEVICE_TABLE(acpi, xgene_enet_acpi_match);
#endif

static const struct of_device_id xgene_enet_of_match[] = {
	{.compatible = "apm,xgene-enet",    .data = (void *)XGENE_ENET1},
	{.compatible = "apm,xgene1-sgenet", .data = (void *)XGENE_ENET1},
	{.compatible = "apm,xgene1-xgenet", .data = (void *)XGENE_ENET1},
	{.compatible = "apm,xgene2-sgenet", .data = (void *)XGENE_ENET2},
	{.compatible = "apm,xgene2-xgenet", .data = (void *)XGENE_ENET2},
	{},
};

MODULE_DEVICE_TABLE(of, xgene_enet_of_match);

static int xgene_enet_probe(struct platform_device *pdev)
{
	struct net_device *ndev;
	struct xgene_enet_pdata *pdata;
	struct device *dev = &pdev->dev;
	void (*link_state)(struct work_struct *);
	const struct of_device_id *of_id;
	int ret;

	ndev = alloc_etherdev_mqs(sizeof(struct xgene_enet_pdata),
				  XGENE_NUM_TX_RING, XGENE_NUM_RX_RING);
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
			  NETIF_F_GRO |
			  NETIF_F_SG;

	of_id = of_match_device(xgene_enet_of_match, &pdev->dev);
	if (of_id) {
		pdata->enet_id = (enum xgene_enet_id)of_id->data;
	}
#ifdef CONFIG_ACPI
	else {
		const struct acpi_device_id *acpi_id;

		acpi_id = acpi_match_device(xgene_enet_acpi_match, &pdev->dev);
		if (acpi_id)
			pdata->enet_id = (enum xgene_enet_id) acpi_id->driver_data;
	}
#endif
	if (!pdata->enet_id) {
		ret = -ENODEV;
		goto err;
	}

	ret = xgene_enet_get_resources(pdata);
	if (ret)
		goto err;

	xgene_enet_setup_ops(pdata);
	spin_lock_init(&pdata->mac_lock);

	if (pdata->phy_mode == PHY_INTERFACE_MODE_XGMII) {
		ndev->features |= NETIF_F_TSO | NETIF_F_RXCSUM;
		spin_lock_init(&pdata->mss_lock);
	}
	ndev->hw_features = ndev->features;

	ret = dma_coerce_mask_and_coherent(dev, DMA_BIT_MASK(64));
	if (ret) {
		netdev_err(ndev, "No usable DMA configuration\n");
		goto err;
	}

	xgene_enet_check_phy_handle(pdata);

	ret = xgene_enet_init_hw(pdata);
	if (ret)
		goto err2;

	link_state = pdata->mac_ops->link_state;
	if (pdata->phy_mode == PHY_INTERFACE_MODE_XGMII) {
		INIT_DELAYED_WORK(&pdata->link_work, link_state);
	} else if (!pdata->mdio_driver) {
		if (phy_interface_mode_is_rgmii(pdata->phy_mode))
			ret = xgene_enet_mdio_config(pdata);
		else
			INIT_DELAYED_WORK(&pdata->link_work, link_state);

		if (ret)
			goto err1;
	}

	spin_lock_init(&pdata->stats_lock);
	ret = xgene_extd_stats_init(pdata);
	if (ret)
		goto err1;

	xgene_enet_napi_add(pdata);
	ret = register_netdev(ndev);
	if (ret) {
		netdev_err(ndev, "Failed to register netdev\n");
		goto err1;
	}

	return 0;

err1:
	/*
	 * If necessary, free_netdev() will call netif_napi_del() and undo
	 * the effects of xgene_enet_napi_add()'s calls to netif_napi_add().
	 */

	xgene_enet_delete_desc_rings(pdata);

err2:
	if (pdata->mdio_driver)
		xgene_enet_phy_disconnect(pdata);
	else if (phy_interface_mode_is_rgmii(pdata->phy_mode))
		xgene_enet_mdio_remove(pdata);
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

	rtnl_lock();
	if (netif_running(ndev))
		dev_close(ndev);
	rtnl_unlock();

	if (pdata->mdio_driver)
		xgene_enet_phy_disconnect(pdata);
	else if (phy_interface_mode_is_rgmii(pdata->phy_mode))
		xgene_enet_mdio_remove(pdata);

	unregister_netdev(ndev);
	xgene_enet_delete_desc_rings(pdata);
	pdata->port_ops->shutdown(pdata);
	free_netdev(ndev);

	return 0;
}

static void xgene_enet_shutdown(struct platform_device *pdev)
{
	struct xgene_enet_pdata *pdata;

	pdata = platform_get_drvdata(pdev);
	if (!pdata)
		return;

	if (!pdata->ndev)
		return;

	xgene_enet_remove(pdev);
}

static struct platform_driver xgene_enet_driver = {
	.driver = {
		   .name = "xgene-enet",
		   .of_match_table = of_match_ptr(xgene_enet_of_match),
		   .acpi_match_table = ACPI_PTR(xgene_enet_acpi_match),
	},
	.probe = xgene_enet_probe,
	.remove = xgene_enet_remove,
	.shutdown = xgene_enet_shutdown,
};

module_platform_driver(xgene_enet_driver);

MODULE_DESCRIPTION("APM X-Gene SoC Ethernet driver");
MODULE_AUTHOR("Iyappan Subramanian <isubramanian@apm.com>");
MODULE_AUTHOR("Keyur Chudgar <kchudgar@apm.com>");
MODULE_LICENSE("GPL");
