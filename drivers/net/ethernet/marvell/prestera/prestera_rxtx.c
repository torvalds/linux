// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/* Copyright (c) 2019-2020 Marvell International Ltd. All rights reserved */

#include <linux/bitfield.h>
#include <linux/dmapool.h>
#include <linux/etherdevice.h>
#include <linux/if_vlan.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/of.h>
#include <linux/platform_device.h>

#include "prestera_dsa.h"
#include "prestera.h"
#include "prestera_hw.h"
#include "prestera_rxtx.h"
#include "prestera_devlink.h"

#define PRESTERA_SDMA_WAIT_MUL		10

struct prestera_sdma_desc {
	__le32 word1;
	__le32 word2;
	__le32 buff;
	__le32 next;
} __packed __aligned(16);

#define PRESTERA_SDMA_BUFF_SIZE_MAX	1544

#define PRESTERA_SDMA_RX_DESC_PKT_LEN(desc) \
	((le32_to_cpu((desc)->word2) >> 16) & GENMASK(13, 0))

#define PRESTERA_SDMA_RX_DESC_OWNER(desc) \
	((le32_to_cpu((desc)->word1) & BIT(31)) >> 31)

#define PRESTERA_SDMA_RX_DESC_IS_RCVD(desc) \
	(PRESTERA_SDMA_RX_DESC_OWNER(desc) == PRESTERA_SDMA_RX_DESC_CPU_OWN)

#define PRESTERA_SDMA_RX_DESC_CPU_OWN	0
#define PRESTERA_SDMA_RX_DESC_DMA_OWN	1

#define PRESTERA_SDMA_RX_QUEUE_NUM	8

#define PRESTERA_SDMA_RX_DESC_PER_Q	1000

#define PRESTERA_SDMA_TX_DESC_PER_Q	1000
#define PRESTERA_SDMA_TX_MAX_BURST	64

#define PRESTERA_SDMA_TX_DESC_OWNER(desc) \
	((le32_to_cpu((desc)->word1) & BIT(31)) >> 31)

#define PRESTERA_SDMA_TX_DESC_CPU_OWN	0
#define PRESTERA_SDMA_TX_DESC_DMA_OWN	1U

#define PRESTERA_SDMA_TX_DESC_IS_SENT(desc) \
	(PRESTERA_SDMA_TX_DESC_OWNER(desc) == PRESTERA_SDMA_TX_DESC_CPU_OWN)

#define PRESTERA_SDMA_TX_DESC_LAST	BIT(20)
#define PRESTERA_SDMA_TX_DESC_FIRST	BIT(21)
#define PRESTERA_SDMA_TX_DESC_CALC_CRC	BIT(12)

#define PRESTERA_SDMA_TX_DESC_SINGLE	\
	(PRESTERA_SDMA_TX_DESC_FIRST | PRESTERA_SDMA_TX_DESC_LAST)

#define PRESTERA_SDMA_TX_DESC_INIT	\
	(PRESTERA_SDMA_TX_DESC_SINGLE | PRESTERA_SDMA_TX_DESC_CALC_CRC)

#define PRESTERA_SDMA_RX_INTR_MASK_REG		0x2814
#define PRESTERA_SDMA_RX_QUEUE_STATUS_REG	0x2680
#define PRESTERA_SDMA_RX_QUEUE_DESC_REG(n)	(0x260C + (n) * 16)

#define PRESTERA_SDMA_TX_QUEUE_DESC_REG		0x26C0
#define PRESTERA_SDMA_TX_QUEUE_START_REG	0x2868

struct prestera_sdma_buf {
	struct prestera_sdma_desc *desc;
	dma_addr_t desc_dma;
	struct sk_buff *skb;
	dma_addr_t buf_dma;
	bool is_used;
};

struct prestera_rx_ring {
	struct prestera_sdma_buf *bufs;
	int next_rx;
};

struct prestera_tx_ring {
	struct prestera_sdma_buf *bufs;
	int next_tx;
	int max_burst;
	int burst;
};

struct prestera_sdma {
	struct prestera_rx_ring rx_ring[PRESTERA_SDMA_RX_QUEUE_NUM];
	struct prestera_tx_ring tx_ring;
	struct prestera_switch *sw;
	struct dma_pool *desc_pool;
	struct work_struct tx_work;
	struct napi_struct rx_napi;
	struct net_device napi_dev;
	u32 map_addr;
	u64 dma_mask;
	/* protect SDMA with concurrent access from multiple CPUs */
	spinlock_t tx_lock;
};

struct prestera_rxtx {
	struct prestera_sdma sdma;
};

static int prestera_sdma_buf_init(struct prestera_sdma *sdma,
				  struct prestera_sdma_buf *buf)
{
	struct prestera_sdma_desc *desc;
	dma_addr_t dma;

	desc = dma_pool_alloc(sdma->desc_pool, GFP_DMA | GFP_KERNEL, &dma);
	if (!desc)
		return -ENOMEM;

	buf->buf_dma = DMA_MAPPING_ERROR;
	buf->desc_dma = dma;
	buf->desc = desc;
	buf->skb = NULL;

	return 0;
}

static u32 prestera_sdma_map(struct prestera_sdma *sdma, dma_addr_t pa)
{
	return sdma->map_addr + pa;
}

static void prestera_sdma_rx_desc_init(struct prestera_sdma *sdma,
				       struct prestera_sdma_desc *desc,
				       dma_addr_t buf)
{
	u32 word = le32_to_cpu(desc->word2);

	u32p_replace_bits(&word, PRESTERA_SDMA_BUFF_SIZE_MAX, GENMASK(15, 0));
	desc->word2 = cpu_to_le32(word);

	desc->buff = cpu_to_le32(prestera_sdma_map(sdma, buf));

	/* make sure buffer is set before reset the descriptor */
	wmb();

	desc->word1 = cpu_to_le32(0xA0000000);
}

static void prestera_sdma_rx_desc_set_next(struct prestera_sdma *sdma,
					   struct prestera_sdma_desc *desc,
					   dma_addr_t next)
{
	desc->next = cpu_to_le32(prestera_sdma_map(sdma, next));
}

static int prestera_sdma_rx_skb_alloc(struct prestera_sdma *sdma,
				      struct prestera_sdma_buf *buf)
{
	struct device *dev = sdma->sw->dev->dev;
	struct sk_buff *skb;
	dma_addr_t dma;

	skb = alloc_skb(PRESTERA_SDMA_BUFF_SIZE_MAX, GFP_DMA | GFP_ATOMIC);
	if (!skb)
		return -ENOMEM;

	dma = dma_map_single(dev, skb->data, skb->len, DMA_FROM_DEVICE);
	if (dma_mapping_error(dev, dma))
		goto err_dma_map;

	if (buf->skb)
		dma_unmap_single(dev, buf->buf_dma, buf->skb->len,
				 DMA_FROM_DEVICE);

	buf->buf_dma = dma;
	buf->skb = skb;

	return 0;

err_dma_map:
	kfree_skb(skb);

	return -ENOMEM;
}

static struct sk_buff *prestera_sdma_rx_skb_get(struct prestera_sdma *sdma,
						struct prestera_sdma_buf *buf)
{
	dma_addr_t buf_dma = buf->buf_dma;
	struct sk_buff *skb = buf->skb;
	u32 len = skb->len;
	int err;

	err = prestera_sdma_rx_skb_alloc(sdma, buf);
	if (err) {
		buf->buf_dma = buf_dma;
		buf->skb = skb;

		skb = alloc_skb(skb->len, GFP_ATOMIC);
		if (skb) {
			skb_put(skb, len);
			skb_copy_from_linear_data(buf->skb, skb->data, len);
		}
	}

	prestera_sdma_rx_desc_init(sdma, buf->desc, buf->buf_dma);

	return skb;
}

static int prestera_rxtx_process_skb(struct prestera_sdma *sdma,
				     struct sk_buff *skb)
{
	struct prestera_port *port;
	struct prestera_dsa dsa;
	u32 hw_port, dev_id;
	u8 cpu_code;
	int err;

	skb_pull(skb, ETH_HLEN);

	/* ethertype field is part of the dsa header */
	err = prestera_dsa_parse(&dsa, skb->data - ETH_TLEN);
	if (err)
		return err;

	dev_id = dsa.hw_dev_num;
	hw_port = dsa.port_num;

	port = prestera_port_find_by_hwid(sdma->sw, dev_id, hw_port);
	if (unlikely(!port)) {
		dev_warn_ratelimited(prestera_dev(sdma->sw), "received pkt for non-existent port(%u, %u)\n",
				     dev_id, hw_port);
		return -ENOENT;
	}

	if (unlikely(!pskb_may_pull(skb, PRESTERA_DSA_HLEN)))
		return -EINVAL;

	/* remove DSA tag and update checksum */
	skb_pull_rcsum(skb, PRESTERA_DSA_HLEN);

	memmove(skb->data - ETH_HLEN, skb->data - ETH_HLEN - PRESTERA_DSA_HLEN,
		ETH_ALEN * 2);

	skb_push(skb, ETH_HLEN);

	skb->protocol = eth_type_trans(skb, port->dev);

	if (dsa.vlan.is_tagged) {
		u16 tci = dsa.vlan.vid & VLAN_VID_MASK;

		tci |= dsa.vlan.vpt << VLAN_PRIO_SHIFT;
		if (dsa.vlan.cfi_bit)
			tci |= VLAN_CFI_MASK;

		__vlan_hwaccel_put_tag(skb, htons(ETH_P_8021Q), tci);
	}

	cpu_code = dsa.cpu_code;
	prestera_devlink_trap_report(port, skb, cpu_code);

	return 0;
}

static int prestera_sdma_next_rx_buf_idx(int buf_idx)
{
	return (buf_idx + 1) % PRESTERA_SDMA_RX_DESC_PER_Q;
}

static int prestera_sdma_rx_poll(struct napi_struct *napi, int budget)
{
	int qnum = PRESTERA_SDMA_RX_QUEUE_NUM;
	unsigned int rxq_done_map = 0;
	struct prestera_sdma *sdma;
	struct list_head rx_list;
	unsigned int qmask;
	int pkts_done = 0;
	int q;

	qnum = PRESTERA_SDMA_RX_QUEUE_NUM;
	qmask = GENMASK(qnum - 1, 0);

	INIT_LIST_HEAD(&rx_list);

	sdma = container_of(napi, struct prestera_sdma, rx_napi);

	while (pkts_done < budget && rxq_done_map != qmask) {
		for (q = 0; q < qnum && pkts_done < budget; q++) {
			struct prestera_rx_ring *ring = &sdma->rx_ring[q];
			struct prestera_sdma_desc *desc;
			struct prestera_sdma_buf *buf;
			int buf_idx = ring->next_rx;
			struct sk_buff *skb;

			buf = &ring->bufs[buf_idx];
			desc = buf->desc;

			if (PRESTERA_SDMA_RX_DESC_IS_RCVD(desc)) {
				rxq_done_map &= ~BIT(q);
			} else {
				rxq_done_map |= BIT(q);
				continue;
			}

			pkts_done++;

			__skb_trim(buf->skb, PRESTERA_SDMA_RX_DESC_PKT_LEN(desc));

			skb = prestera_sdma_rx_skb_get(sdma, buf);
			if (!skb)
				goto rx_next_buf;

			if (unlikely(prestera_rxtx_process_skb(sdma, skb)))
				goto rx_next_buf;

			list_add_tail(&skb->list, &rx_list);
rx_next_buf:
			ring->next_rx = prestera_sdma_next_rx_buf_idx(buf_idx);
		}
	}

	if (pkts_done < budget && napi_complete_done(napi, pkts_done))
		prestera_write(sdma->sw, PRESTERA_SDMA_RX_INTR_MASK_REG,
			       GENMASK(9, 2));

	netif_receive_skb_list(&rx_list);

	return pkts_done;
}

static void prestera_sdma_rx_fini(struct prestera_sdma *sdma)
{
	int qnum = PRESTERA_SDMA_RX_QUEUE_NUM;
	int q, b;

	/* disable all rx queues */
	prestera_write(sdma->sw, PRESTERA_SDMA_RX_QUEUE_STATUS_REG,
		       GENMASK(15, 8));

	for (q = 0; q < qnum; q++) {
		struct prestera_rx_ring *ring = &sdma->rx_ring[q];

		if (!ring->bufs)
			break;

		for (b = 0; b < PRESTERA_SDMA_RX_DESC_PER_Q; b++) {
			struct prestera_sdma_buf *buf = &ring->bufs[b];

			if (buf->desc_dma)
				dma_pool_free(sdma->desc_pool, buf->desc,
					      buf->desc_dma);

			if (!buf->skb)
				continue;

			if (buf->buf_dma != DMA_MAPPING_ERROR)
				dma_unmap_single(sdma->sw->dev->dev,
						 buf->buf_dma, buf->skb->len,
						 DMA_FROM_DEVICE);
			kfree_skb(buf->skb);
		}
	}
}

static int prestera_sdma_rx_init(struct prestera_sdma *sdma)
{
	int bnum = PRESTERA_SDMA_RX_DESC_PER_Q;
	int qnum = PRESTERA_SDMA_RX_QUEUE_NUM;
	int err;
	int q;

	/* disable all rx queues */
	prestera_write(sdma->sw, PRESTERA_SDMA_RX_QUEUE_STATUS_REG,
		       GENMASK(15, 8));

	for (q = 0; q < qnum; q++) {
		struct prestera_sdma_buf *head, *tail, *next, *prev;
		struct prestera_rx_ring *ring = &sdma->rx_ring[q];

		ring->bufs = kmalloc_array(bnum, sizeof(*head), GFP_KERNEL);
		if (!ring->bufs)
			return -ENOMEM;

		ring->next_rx = 0;

		tail = &ring->bufs[bnum - 1];
		head = &ring->bufs[0];
		next = head;
		prev = next;

		do {
			err = prestera_sdma_buf_init(sdma, next);
			if (err)
				return err;

			err = prestera_sdma_rx_skb_alloc(sdma, next);
			if (err)
				return err;

			prestera_sdma_rx_desc_init(sdma, next->desc,
						   next->buf_dma);

			prestera_sdma_rx_desc_set_next(sdma, prev->desc,
						       next->desc_dma);

			prev = next;
			next++;
		} while (prev != tail);

		/* join tail with head to make a circular list */
		prestera_sdma_rx_desc_set_next(sdma, tail->desc, head->desc_dma);

		prestera_write(sdma->sw, PRESTERA_SDMA_RX_QUEUE_DESC_REG(q),
			       prestera_sdma_map(sdma, head->desc_dma));
	}

	/* make sure all rx descs are filled before enabling all rx queues */
	wmb();

	prestera_write(sdma->sw, PRESTERA_SDMA_RX_QUEUE_STATUS_REG,
		       GENMASK(7, 0));

	return 0;
}

static void prestera_sdma_tx_desc_init(struct prestera_sdma *sdma,
				       struct prestera_sdma_desc *desc)
{
	desc->word1 = cpu_to_le32(PRESTERA_SDMA_TX_DESC_INIT);
	desc->word2 = 0;
}

static void prestera_sdma_tx_desc_set_next(struct prestera_sdma *sdma,
					   struct prestera_sdma_desc *desc,
					   dma_addr_t next)
{
	desc->next = cpu_to_le32(prestera_sdma_map(sdma, next));
}

static void prestera_sdma_tx_desc_set_buf(struct prestera_sdma *sdma,
					  struct prestera_sdma_desc *desc,
					  dma_addr_t buf, size_t len)
{
	u32 word = le32_to_cpu(desc->word2);

	u32p_replace_bits(&word, len + ETH_FCS_LEN, GENMASK(30, 16));

	desc->buff = cpu_to_le32(prestera_sdma_map(sdma, buf));
	desc->word2 = cpu_to_le32(word);
}

static void prestera_sdma_tx_desc_xmit(struct prestera_sdma_desc *desc)
{
	u32 word = le32_to_cpu(desc->word1);

	word |= PRESTERA_SDMA_TX_DESC_DMA_OWN << 31;

	/* make sure everything is written before enable xmit */
	wmb();

	desc->word1 = cpu_to_le32(word);
}

static int prestera_sdma_tx_buf_map(struct prestera_sdma *sdma,
				    struct prestera_sdma_buf *buf,
				    struct sk_buff *skb)
{
	struct device *dma_dev = sdma->sw->dev->dev;
	dma_addr_t dma;

	dma = dma_map_single(dma_dev, skb->data, skb->len, DMA_TO_DEVICE);
	if (dma_mapping_error(dma_dev, dma))
		return -ENOMEM;

	buf->buf_dma = dma;
	buf->skb = skb;

	return 0;
}

static void prestera_sdma_tx_buf_unmap(struct prestera_sdma *sdma,
				       struct prestera_sdma_buf *buf)
{
	struct device *dma_dev = sdma->sw->dev->dev;

	dma_unmap_single(dma_dev, buf->buf_dma, buf->skb->len, DMA_TO_DEVICE);
}

static void prestera_sdma_tx_recycle_work_fn(struct work_struct *work)
{
	int bnum = PRESTERA_SDMA_TX_DESC_PER_Q;
	struct prestera_tx_ring *tx_ring;
	struct prestera_sdma *sdma;
	int b;

	sdma = container_of(work, struct prestera_sdma, tx_work);

	tx_ring = &sdma->tx_ring;

	for (b = 0; b < bnum; b++) {
		struct prestera_sdma_buf *buf = &tx_ring->bufs[b];

		if (!buf->is_used)
			continue;

		if (!PRESTERA_SDMA_TX_DESC_IS_SENT(buf->desc))
			continue;

		prestera_sdma_tx_buf_unmap(sdma, buf);
		dev_consume_skb_any(buf->skb);
		buf->skb = NULL;

		/* make sure everything is cleaned up */
		wmb();

		buf->is_used = false;
	}
}

static int prestera_sdma_tx_init(struct prestera_sdma *sdma)
{
	struct prestera_sdma_buf *head, *tail, *next, *prev;
	struct prestera_tx_ring *tx_ring = &sdma->tx_ring;
	int bnum = PRESTERA_SDMA_TX_DESC_PER_Q;
	int err;

	INIT_WORK(&sdma->tx_work, prestera_sdma_tx_recycle_work_fn);
	spin_lock_init(&sdma->tx_lock);

	tx_ring->bufs = kmalloc_array(bnum, sizeof(*head), GFP_KERNEL);
	if (!tx_ring->bufs)
		return -ENOMEM;

	tail = &tx_ring->bufs[bnum - 1];
	head = &tx_ring->bufs[0];
	next = head;
	prev = next;

	tx_ring->max_burst = PRESTERA_SDMA_TX_MAX_BURST;
	tx_ring->burst = tx_ring->max_burst;
	tx_ring->next_tx = 0;

	do {
		err = prestera_sdma_buf_init(sdma, next);
		if (err)
			return err;

		next->is_used = false;

		prestera_sdma_tx_desc_init(sdma, next->desc);

		prestera_sdma_tx_desc_set_next(sdma, prev->desc,
					       next->desc_dma);

		prev = next;
		next++;
	} while (prev != tail);

	/* join tail with head to make a circular list */
	prestera_sdma_tx_desc_set_next(sdma, tail->desc, head->desc_dma);

	/* make sure descriptors are written */
	wmb();

	prestera_write(sdma->sw, PRESTERA_SDMA_TX_QUEUE_DESC_REG,
		       prestera_sdma_map(sdma, head->desc_dma));

	return 0;
}

static void prestera_sdma_tx_fini(struct prestera_sdma *sdma)
{
	struct prestera_tx_ring *ring = &sdma->tx_ring;
	int bnum = PRESTERA_SDMA_TX_DESC_PER_Q;
	int b;

	cancel_work_sync(&sdma->tx_work);

	if (!ring->bufs)
		return;

	for (b = 0; b < bnum; b++) {
		struct prestera_sdma_buf *buf = &ring->bufs[b];

		if (buf->desc)
			dma_pool_free(sdma->desc_pool, buf->desc,
				      buf->desc_dma);

		if (!buf->skb)
			continue;

		dma_unmap_single(sdma->sw->dev->dev, buf->buf_dma,
				 buf->skb->len, DMA_TO_DEVICE);

		dev_consume_skb_any(buf->skb);
	}
}

static void prestera_rxtx_handle_event(struct prestera_switch *sw,
				       struct prestera_event *evt,
				       void *arg)
{
	struct prestera_sdma *sdma = arg;

	if (evt->id != PRESTERA_RXTX_EVENT_RCV_PKT)
		return;

	prestera_write(sdma->sw, PRESTERA_SDMA_RX_INTR_MASK_REG, 0);
	napi_schedule(&sdma->rx_napi);
}

static int prestera_sdma_switch_init(struct prestera_switch *sw)
{
	struct prestera_sdma *sdma = &sw->rxtx->sdma;
	struct device *dev = sw->dev->dev;
	struct prestera_rxtx_params p;
	int err;

	p.use_sdma = true;

	err = prestera_hw_rxtx_init(sw, &p);
	if (err) {
		dev_err(dev, "failed to init rxtx by hw\n");
		return err;
	}

	sdma->dma_mask = dma_get_mask(dev);
	sdma->map_addr = p.map_addr;
	sdma->sw = sw;

	sdma->desc_pool = dma_pool_create("desc_pool", dev,
					  sizeof(struct prestera_sdma_desc),
					  16, 0);
	if (!sdma->desc_pool)
		return -ENOMEM;

	err = prestera_sdma_rx_init(sdma);
	if (err) {
		dev_err(dev, "failed to init rx ring\n");
		goto err_rx_init;
	}

	err = prestera_sdma_tx_init(sdma);
	if (err) {
		dev_err(dev, "failed to init tx ring\n");
		goto err_tx_init;
	}

	err = prestera_hw_event_handler_register(sw, PRESTERA_EVENT_TYPE_RXTX,
						 prestera_rxtx_handle_event,
						 sdma);
	if (err)
		goto err_evt_register;

	init_dummy_netdev(&sdma->napi_dev);

	netif_napi_add(&sdma->napi_dev, &sdma->rx_napi, prestera_sdma_rx_poll, 64);
	napi_enable(&sdma->rx_napi);

	return 0;

err_evt_register:
err_tx_init:
	prestera_sdma_tx_fini(sdma);
err_rx_init:
	prestera_sdma_rx_fini(sdma);

	dma_pool_destroy(sdma->desc_pool);
	return err;
}

static void prestera_sdma_switch_fini(struct prestera_switch *sw)
{
	struct prestera_sdma *sdma = &sw->rxtx->sdma;

	napi_disable(&sdma->rx_napi);
	netif_napi_del(&sdma->rx_napi);
	prestera_hw_event_handler_unregister(sw, PRESTERA_EVENT_TYPE_RXTX,
					     prestera_rxtx_handle_event);
	prestera_sdma_tx_fini(sdma);
	prestera_sdma_rx_fini(sdma);
	dma_pool_destroy(sdma->desc_pool);
}

static bool prestera_sdma_is_ready(struct prestera_sdma *sdma)
{
	return !(prestera_read(sdma->sw, PRESTERA_SDMA_TX_QUEUE_START_REG) & 1);
}

static int prestera_sdma_tx_wait(struct prestera_sdma *sdma,
				 struct prestera_tx_ring *tx_ring)
{
	int tx_wait_num = PRESTERA_SDMA_WAIT_MUL * tx_ring->max_burst;

	do {
		if (prestera_sdma_is_ready(sdma))
			return 0;

		udelay(1);
	} while (--tx_wait_num);

	return -EBUSY;
}

static void prestera_sdma_tx_start(struct prestera_sdma *sdma)
{
	prestera_write(sdma->sw, PRESTERA_SDMA_TX_QUEUE_START_REG, 1);
	schedule_work(&sdma->tx_work);
}

static netdev_tx_t prestera_sdma_xmit(struct prestera_sdma *sdma,
				      struct sk_buff *skb)
{
	struct device *dma_dev = sdma->sw->dev->dev;
	struct net_device *dev = skb->dev;
	struct prestera_tx_ring *tx_ring;
	struct prestera_sdma_buf *buf;
	int err;

	spin_lock(&sdma->tx_lock);

	tx_ring = &sdma->tx_ring;

	buf = &tx_ring->bufs[tx_ring->next_tx];
	if (buf->is_used) {
		schedule_work(&sdma->tx_work);
		goto drop_skb;
	}

	if (unlikely(eth_skb_pad(skb)))
		goto drop_skb_nofree;

	err = prestera_sdma_tx_buf_map(sdma, buf, skb);
	if (err)
		goto drop_skb;

	prestera_sdma_tx_desc_set_buf(sdma, buf->desc, buf->buf_dma, skb->len);

	dma_sync_single_for_device(dma_dev, buf->buf_dma, skb->len,
				   DMA_TO_DEVICE);

	if (tx_ring->burst) {
		tx_ring->burst--;
	} else {
		tx_ring->burst = tx_ring->max_burst;

		err = prestera_sdma_tx_wait(sdma, tx_ring);
		if (err)
			goto drop_skb_unmap;
	}

	tx_ring->next_tx = (tx_ring->next_tx + 1) % PRESTERA_SDMA_TX_DESC_PER_Q;
	prestera_sdma_tx_desc_xmit(buf->desc);
	buf->is_used = true;

	prestera_sdma_tx_start(sdma);

	goto tx_done;

drop_skb_unmap:
	prestera_sdma_tx_buf_unmap(sdma, buf);
drop_skb:
	dev_consume_skb_any(skb);
drop_skb_nofree:
	dev->stats.tx_dropped++;
tx_done:
	spin_unlock(&sdma->tx_lock);
	return NETDEV_TX_OK;
}

int prestera_rxtx_switch_init(struct prestera_switch *sw)
{
	struct prestera_rxtx *rxtx;

	rxtx = kzalloc(sizeof(*rxtx), GFP_KERNEL);
	if (!rxtx)
		return -ENOMEM;

	sw->rxtx = rxtx;

	return prestera_sdma_switch_init(sw);
}

void prestera_rxtx_switch_fini(struct prestera_switch *sw)
{
	prestera_sdma_switch_fini(sw);
	kfree(sw->rxtx);
}

int prestera_rxtx_port_init(struct prestera_port *port)
{
	port->dev->needed_headroom = PRESTERA_DSA_HLEN;
	return 0;
}

netdev_tx_t prestera_rxtx_xmit(struct prestera_port *port, struct sk_buff *skb)
{
	struct prestera_dsa dsa;

	dsa.hw_dev_num = port->dev_id;
	dsa.port_num = port->hw_id;

	if (skb_cow_head(skb, PRESTERA_DSA_HLEN) < 0)
		return NET_XMIT_DROP;

	skb_push(skb, PRESTERA_DSA_HLEN);
	memmove(skb->data, skb->data + PRESTERA_DSA_HLEN, 2 * ETH_ALEN);

	if (prestera_dsa_build(&dsa, skb->data + 2 * ETH_ALEN) != 0)
		return NET_XMIT_DROP;

	return prestera_sdma_xmit(&port->sw->rxtx->sdma, skb);
}
