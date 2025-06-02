// SPDX-License-Identifier: GPL-2.0+
/* Copyright (c) Tehuti Networks Ltd. */

#include <linux/bitfield.h>
#include <linux/ethtool.h>
#include <linux/firmware.h>
#include <linux/if_vlan.h>
#include <linux/iopoll.h>
#include <linux/netdevice.h>
#include <linux/pci.h>
#include <linux/phylink.h>
#include <linux/vmalloc.h>
#include <net/netdev_queues.h>
#include <net/page_pool/helpers.h>

#include "tn40.h"

#define TN40_SHORT_PACKET_SIZE 60
#define TN40_FIRMWARE_NAME "tehuti/bdx.bin"

static void tn40_enable_interrupts(struct tn40_priv *priv)
{
	tn40_write_reg(priv, TN40_REG_IMR, priv->isr_mask);
}

static void tn40_disable_interrupts(struct tn40_priv *priv)
{
	tn40_write_reg(priv, TN40_REG_IMR, 0);
}

static int tn40_fifo_alloc(struct tn40_priv *priv, struct tn40_fifo *f,
			   int fsz_type,
			   u16 reg_cfg0, u16 reg_cfg1,
			   u16 reg_rptr, u16 reg_wptr)
{
	u16 memsz = TN40_FIFO_SIZE * (1 << fsz_type);
	u64 cfg_base;

	memset(f, 0, sizeof(struct tn40_fifo));
	/* 1K extra space is allocated at the end of the fifo to simplify
	 * processing of descriptors that wraps around fifo's end.
	 */
	f->va = dma_alloc_coherent(&priv->pdev->dev,
				   memsz + TN40_FIFO_EXTRA_SPACE, &f->da,
				   GFP_KERNEL);
	if (!f->va)
		return -ENOMEM;

	f->reg_cfg0 = reg_cfg0;
	f->reg_cfg1 = reg_cfg1;
	f->reg_rptr = reg_rptr;
	f->reg_wptr = reg_wptr;
	f->rptr = 0;
	f->wptr = 0;
	f->memsz = memsz;
	f->size_mask = memsz - 1;
	cfg_base = lower_32_bits((f->da & TN40_TX_RX_CFG0_BASE) | fsz_type);
	tn40_write_reg(priv, reg_cfg0, cfg_base);
	tn40_write_reg(priv, reg_cfg1, upper_32_bits(f->da));
	return 0;
}

static void tn40_fifo_free(struct tn40_priv *priv, struct tn40_fifo *f)
{
	dma_free_coherent(&priv->pdev->dev,
			  f->memsz + TN40_FIFO_EXTRA_SPACE, f->va, f->da);
}

static struct tn40_rxdb *tn40_rxdb_alloc(int nelem)
{
	size_t size = sizeof(struct tn40_rxdb) + (nelem * sizeof(int)) +
	    (nelem * sizeof(struct tn40_rx_map));
	struct tn40_rxdb *db;
	int i;

	db = vzalloc(size);
	if (db) {
		db->stack = (int *)(db + 1);
		db->elems = (void *)(db->stack + nelem);
		db->nelem = nelem;
		db->top = nelem;
		/* make the first alloc close to db struct */
		for (i = 0; i < nelem; i++)
			db->stack[i] = nelem - i - 1;
	}
	return db;
}

static void tn40_rxdb_free(struct tn40_rxdb *db)
{
	vfree(db);
}

static int tn40_rxdb_alloc_elem(struct tn40_rxdb *db)
{
	return db->stack[--db->top];
}

static void *tn40_rxdb_addr_elem(struct tn40_rxdb *db, unsigned int n)
{
	return db->elems + n;
}

static int tn40_rxdb_available(struct tn40_rxdb *db)
{
	return db->top;
}

static void tn40_rxdb_free_elem(struct tn40_rxdb *db, unsigned int n)
{
	db->stack[db->top++] = n;
}

/**
 * tn40_create_rx_ring - Initialize RX all related HW and SW resources
 * @priv: NIC private structure
 *
 * create_rx_ring creates rxf and rxd fifos, updates the relevant HW registers,
 * preallocates skbs for rx. It assumes that Rx is disabled in HW funcs are
 * grouped for better cache usage
 *
 * RxD fifo is smaller then RxF fifo by design. Upon high load, RxD will be
 * filled and packets will be dropped by the NIC without getting into the host
 * or generating interrupts. In this situation the host has no chance of
 * processing all the packets. Dropping packets by the NIC is cheaper, since it
 * takes 0 CPU cycles.
 *
 * Return: 0 on success and negative value on error.
 */
static int tn40_create_rx_ring(struct tn40_priv *priv)
{
	struct page_pool_params pp = {
		.dev = &priv->pdev->dev,
		.napi = &priv->napi,
		.dma_dir = DMA_FROM_DEVICE,
		.netdev = priv->ndev,
		.flags = PP_FLAG_DMA_MAP | PP_FLAG_DMA_SYNC_DEV,
		.max_len = PAGE_SIZE,
	};
	int ret, pkt_size, nr;

	priv->page_pool = page_pool_create(&pp);
	if (IS_ERR(priv->page_pool))
		return PTR_ERR(priv->page_pool);

	ret = tn40_fifo_alloc(priv, &priv->rxd_fifo0.m, priv->rxd_size,
			      TN40_REG_RXD_CFG0_0, TN40_REG_RXD_CFG1_0,
			      TN40_REG_RXD_RPTR_0, TN40_REG_RXD_WPTR_0);
	if (ret)
		goto err_destroy_page_pool;

	ret = tn40_fifo_alloc(priv, &priv->rxf_fifo0.m, priv->rxf_size,
			      TN40_REG_RXF_CFG0_0, TN40_REG_RXF_CFG1_0,
			      TN40_REG_RXF_RPTR_0, TN40_REG_RXF_WPTR_0);
	if (ret)
		goto err_free_rxd;

	pkt_size = priv->ndev->mtu + VLAN_ETH_HLEN;
	priv->rxf_fifo0.m.pktsz = pkt_size;
	nr = priv->rxf_fifo0.m.memsz / sizeof(struct tn40_rxf_desc);
	priv->rxdb0 = tn40_rxdb_alloc(nr);
	if (!priv->rxdb0) {
		ret = -ENOMEM;
		goto err_free_rxf;
	}
	return 0;
err_free_rxf:
	tn40_fifo_free(priv, &priv->rxf_fifo0.m);
err_free_rxd:
	tn40_fifo_free(priv, &priv->rxd_fifo0.m);
err_destroy_page_pool:
	page_pool_destroy(priv->page_pool);
	return ret;
}

static void tn40_rx_free_buffers(struct tn40_priv *priv)
{
	struct tn40_rxdb *db = priv->rxdb0;
	struct tn40_rx_map *dm;
	u16 i;

	netdev_dbg(priv->ndev, "total =%d free =%d busy =%d\n", db->nelem,
		   tn40_rxdb_available(db),
		   db->nelem - tn40_rxdb_available(db));

	for (i = 0; i < db->nelem; i++) {
		dm = tn40_rxdb_addr_elem(db, i);
		if (dm->page)
			page_pool_put_full_page(priv->page_pool, dm->page,
						false);
	}
}

static void tn40_destroy_rx_ring(struct tn40_priv *priv)
{
	if (priv->rxdb0) {
		tn40_rx_free_buffers(priv);
		tn40_rxdb_free(priv->rxdb0);
		priv->rxdb0 = NULL;
	}
	tn40_fifo_free(priv, &priv->rxf_fifo0.m);
	tn40_fifo_free(priv, &priv->rxd_fifo0.m);
	page_pool_destroy(priv->page_pool);
}

static void tn40_set_rx_desc(struct tn40_priv *priv, int idx, u64 dma)
{
	struct tn40_rxf_fifo *f = &priv->rxf_fifo0;
	struct tn40_rxf_desc *rxfd;
	int delta;

	rxfd = (struct tn40_rxf_desc *)(f->m.va + f->m.wptr);
	rxfd->info = cpu_to_le32(0x10003);	/* INFO =1 BC =3 */
	rxfd->va_lo = cpu_to_le32(idx);
	rxfd->pa_lo = cpu_to_le32(lower_32_bits(dma));
	rxfd->pa_hi = cpu_to_le32(upper_32_bits(dma));
	rxfd->len = cpu_to_le32(f->m.pktsz);
	f->m.wptr += sizeof(struct tn40_rxf_desc);
	delta = f->m.wptr - f->m.memsz;
	if (unlikely(delta >= 0)) {
		f->m.wptr = delta;
		if (delta > 0) {
			memcpy(f->m.va, f->m.va + f->m.memsz, delta);
			netdev_dbg(priv->ndev,
				   "wrapped rxd descriptor\n");
		}
	}
}

/**
 * tn40_rx_alloc_buffers - Fill rxf fifo with buffers.
 *
 * @priv: NIC's private structure
 *
 * rx_alloc_buffers allocates buffers via the page pool API, builds rxf descs
 * and pushes them (rxf descr) into the rxf fifo. The pages are stored in rxdb.
 * To calculate the free space, we uses the cached values of RPTR and WPTR
 * when needed. This function also updates RPTR and WPTR.
 */
static void tn40_rx_alloc_buffers(struct tn40_priv *priv)
{
	struct tn40_rxf_fifo *f = &priv->rxf_fifo0;
	struct tn40_rxdb *db = priv->rxdb0;
	struct tn40_rx_map *dm;
	struct page *page;
	int dno, i, idx;

	dno = tn40_rxdb_available(db) - 1;
	for (i = dno; i > 0; i--) {
		page = page_pool_dev_alloc_pages(priv->page_pool);
		if (!page)
			break;

		idx = tn40_rxdb_alloc_elem(db);
		tn40_set_rx_desc(priv, idx, page_pool_get_dma_addr(page));
		dm = tn40_rxdb_addr_elem(db, idx);
		dm->page = page;
	}
	if (i != dno)
		tn40_write_reg(priv, f->m.reg_wptr,
			       f->m.wptr & TN40_TXF_WPTR_WR_PTR);
	netdev_dbg(priv->ndev, "write_reg 0x%04x f->m.reg_wptr 0x%x\n",
		   f->m.reg_wptr, f->m.wptr & TN40_TXF_WPTR_WR_PTR);
	netdev_dbg(priv->ndev, "read_reg  0x%04x f->m.reg_rptr=0x%x\n",
		   f->m.reg_rptr, tn40_read_reg(priv, f->m.reg_rptr));
	netdev_dbg(priv->ndev, "write_reg 0x%04x f->m.reg_wptr=0x%x\n",
		   f->m.reg_wptr, tn40_read_reg(priv, f->m.reg_wptr));
}

static void tn40_recycle_rx_buffer(struct tn40_priv *priv,
				   struct tn40_rxd_desc *rxdd)
{
	struct tn40_rxf_fifo *f = &priv->rxf_fifo0;
	struct tn40_rx_map *dm;
	int idx;

	idx = le32_to_cpu(rxdd->va_lo);
	dm = tn40_rxdb_addr_elem(priv->rxdb0, idx);
	tn40_set_rx_desc(priv, idx, page_pool_get_dma_addr(dm->page));

	tn40_write_reg(priv, f->m.reg_wptr, f->m.wptr & TN40_TXF_WPTR_WR_PTR);
}

static int tn40_rx_receive(struct tn40_priv *priv, int budget)
{
	struct tn40_rxd_fifo *f = &priv->rxd_fifo0;
	u32 rxd_val1, rxd_err, pkt_id;
	int tmp_len, size, done = 0;
	struct tn40_rxdb *db = NULL;
	struct tn40_rxd_desc *rxdd;
	struct tn40_rx_map *dm;
	struct sk_buff *skb;
	u16 len, rxd_vlan;
	int idx;

	f->m.wptr = tn40_read_reg(priv, f->m.reg_wptr) & TN40_TXF_WPTR_WR_PTR;
	size = f->m.wptr - f->m.rptr;
	if (size < 0)
		size += f->m.memsz;	/* Size is negative :-) */

	while (size > 0) {
		rxdd = (struct tn40_rxd_desc *)(f->m.va + f->m.rptr);
		db = priv->rxdb0;

		/* We have a chicken and egg problem here. If the
		 * descriptor is wrapped we first need to copy the tail
		 * of the descriptor to the end of the buffer before
		 * extracting values from the descriptor. However in
		 * order to know if the descriptor is wrapped we need to
		 * obtain the length of the descriptor from (the
		 * wrapped) descriptor. Luckily the length is the first
		 * word of the descriptor. Descriptor lengths are
		 * multiples of 8 bytes so in case of a wrapped
		 * descriptor the first 8 bytes guaranteed to appear
		 * before the end of the buffer. We first obtain the
		 * length, we then copy the rest of the descriptor if
		 * needed and then extract the rest of the values from
		 * the descriptor.
		 *
		 * Do not change the order of operations as it will
		 * break the code!!!
		 */
		rxd_val1 = le32_to_cpu(rxdd->rxd_val1);
		tmp_len = TN40_GET_RXD_BC(rxd_val1) << 3;
		pkt_id = TN40_GET_RXD_PKT_ID(rxd_val1);
		size -= tmp_len;
		/* CHECK FOR A PARTIALLY ARRIVED DESCRIPTOR */
		if (size < 0) {
			netdev_dbg(priv->ndev,
				   "%s partially arrived desc tmp_len %d\n",
				   __func__, tmp_len);
			break;
		}
		/* make sure that the descriptor fully is arrived
		 * before reading the rest of the descriptor.
		 */
		rmb();

		/* A special treatment is given to non-contiguous
		 * descriptors that start near the end, wraps around
		 * and continue at the beginning. The second part is
		 * copied right after the first, and then descriptor
		 * is interpreted as normal. The fifo has an extra
		 * space to allow such operations.
		 */

		/* HAVE WE REACHED THE END OF THE QUEUE? */
		f->m.rptr += tmp_len;
		tmp_len = f->m.rptr - f->m.memsz;
		if (unlikely(tmp_len >= 0)) {
			f->m.rptr = tmp_len;
			if (tmp_len > 0) {
				/* COPY PARTIAL DESCRIPTOR
				 * TO THE END OF THE QUEUE
				 */
				netdev_dbg(priv->ndev,
					   "wrapped desc rptr=%d tmp_len=%d\n",
					   f->m.rptr, tmp_len);
				memcpy(f->m.va + f->m.memsz, f->m.va, tmp_len);
			}
		}
		idx = le32_to_cpu(rxdd->va_lo);
		dm = tn40_rxdb_addr_elem(db, idx);
		prefetch(dm);

		len = le16_to_cpu(rxdd->len);
		rxd_vlan = le16_to_cpu(rxdd->rxd_vlan);
		/* CHECK FOR ERRORS */
		rxd_err = TN40_GET_RXD_ERR(rxd_val1);
		if (unlikely(rxd_err)) {
			u64_stats_update_begin(&priv->syncp);
			priv->stats.rx_errors++;
			u64_stats_update_end(&priv->syncp);
			tn40_recycle_rx_buffer(priv, rxdd);
			continue;
		}

		skb = napi_build_skb(page_address(dm->page), PAGE_SIZE);
		if (!skb) {
			u64_stats_update_begin(&priv->syncp);
			priv->stats.rx_dropped++;
			priv->alloc_fail++;
			u64_stats_update_end(&priv->syncp);
			tn40_recycle_rx_buffer(priv, rxdd);
			break;
		}
		skb_mark_for_recycle(skb);
		skb_put(skb, len);
		skb->protocol = eth_type_trans(skb, priv->ndev);
		skb->ip_summed =
		    (pkt_id == 0) ? CHECKSUM_NONE : CHECKSUM_UNNECESSARY;
		if (TN40_GET_RXD_VTAG(rxd_val1))
			__vlan_hwaccel_put_tag(skb, htons(ETH_P_8021Q),
					       TN40_GET_RXD_VLAN_TCI(rxd_vlan));

		dm->page = NULL;
		tn40_rxdb_free_elem(db, idx);

		napi_gro_receive(&priv->napi, skb);

		u64_stats_update_begin(&priv->syncp);
		priv->stats.rx_bytes += len;
		u64_stats_update_end(&priv->syncp);

		if (unlikely(++done >= budget))
			break;
	}
	u64_stats_update_begin(&priv->syncp);
	priv->stats.rx_packets += done;
	u64_stats_update_end(&priv->syncp);
	/* FIXME: Do something to minimize pci accesses */
	tn40_write_reg(priv, f->m.reg_rptr, f->m.rptr & TN40_TXF_WPTR_WR_PTR);
	tn40_rx_alloc_buffers(priv);
	return done;
}

/* TX HW/SW interaction overview
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 * There are 2 types of TX communication channels between driver and NIC.
 * 1) TX Free Fifo - TXF - Holds ack descriptors for sent packets.
 * 2) TX Data Fifo - TXD - Holds descriptors of full buffers.
 *
 * Currently the NIC supports TSO, checksumming and gather DMA
 * UFO and IP fragmentation is on the way.
 *
 * RX SW Data Structures
 * ~~~~~~~~~~~~~~~~~~~~~
 * TXDB is used to keep track of all skbs owned by SW and their DMA addresses.
 * For TX case, ownership lasts from getting the packet via hard_xmit and
 * until the HW acknowledges sending the packet by TXF descriptors.
 * TXDB is implemented as a cyclic buffer.
 *
 * FIFO objects keep info about the fifo's size and location, relevant HW
 * registers, usage and skb db. Each RXD and RXF fifo has their own fifo
 * structure. Implemented as simple struct.
 *
 * TX SW Execution Flow
 * ~~~~~~~~~~~~~~~~~~~~
 * OS calls the driver's hard_xmit method with a packet to send. The driver
 * creates DMA mappings, builds TXD descriptors and kicks the HW by updating
 * TXD WPTR.
 *
 * When a packet is sent, The HW write a TXF descriptor and the SW
 * frees the original skb. To prevent TXD fifo overflow without
 * reading HW registers every time, the SW deploys "tx level"
 * technique. Upon startup, the tx level is initialized to TXD fifo
 * length. For every sent packet, the SW gets its TXD descriptor size
 * (from a pre-calculated array) and subtracts it from tx level.  The
 * size is also stored in txdb. When a TXF ack arrives, the SW fetched
 * the size of the original TXD descriptor from the txdb and adds it
 * to the tx level. When the Tx level drops below some predefined
 * threshold, the driver stops the TX queue. When the TX level rises
 * above that level, the tx queue is enabled again.
 *
 * This technique avoids excessive reading of RPTR and WPTR registers.
 * As our benchmarks shows, it adds 1.5 Gbit/sec to NIC's throughput.
 */
static void tn40_do_tx_db_ptr_next(struct tn40_txdb *db,
				   struct tn40_tx_map **pptr)
{
	++*pptr;
	if (unlikely(*pptr == db->end))
		*pptr = db->start;
}

static void tn40_tx_db_inc_rptr(struct tn40_txdb *db)
{
	tn40_do_tx_db_ptr_next(db, &db->rptr);
}

static void tn40_tx_db_inc_wptr(struct tn40_txdb *db)
{
	tn40_do_tx_db_ptr_next(db, &db->wptr);
}

static int tn40_tx_db_init(struct tn40_txdb *d, int sz_type)
{
	int memsz = TN40_FIFO_SIZE * (1 << (sz_type + 1));

	d->start = vzalloc(memsz);
	if (!d->start)
		return -ENOMEM;
	/* In order to differentiate between an empty db state and a full db
	 * state at least one element should always be empty in order to
	 * avoid rptr == wptr, which means that the db is empty.
	 */
	d->size = memsz / sizeof(struct tn40_tx_map) - 1;
	d->end = d->start + d->size + 1;	/* just after last element */

	/* All dbs are created empty */
	d->rptr = d->start;
	d->wptr = d->start;
	return 0;
}

static void tn40_tx_db_close(struct tn40_txdb *d)
{
	if (d->start) {
		vfree(d->start);
		d->start = NULL;
	}
}

/* Sizes of tx desc (including padding if needed) as function of the SKB's
 * frag number
 * 7 - is number of lwords in txd with one phys buffer
 * 3 - is number of lwords used for every additional phys buffer
 * for (i = 0; i < TN40_MAX_PBL; i++) {
 *	lwords = 7 + (i * 3);
 *	if (lwords & 1)
 *		lwords++;	pad it with 1 lword
 *	tn40_txd_sizes[i].bytes = lwords << 2;
 *	tn40_txd_sizes[i].qwords = lwords >> 1;
 * }
 */
static struct {
	u16 bytes;
	u16 qwords;	/* qword = 64 bit */
} tn40_txd_sizes[] = {
	{0x20, 0x04},
	{0x28, 0x05},
	{0x38, 0x07},
	{0x40, 0x08},
	{0x50, 0x0a},
	{0x58, 0x0b},
	{0x68, 0x0d},
	{0x70, 0x0e},
	{0x80, 0x10},
	{0x88, 0x11},
	{0x98, 0x13},
	{0xa0, 0x14},
	{0xb0, 0x16},
	{0xb8, 0x17},
	{0xc8, 0x19},
	{0xd0, 0x1a},
	{0xe0, 0x1c},
	{0xe8, 0x1d},
	{0xf8, 0x1f},
};

static void tn40_pbl_set(struct tn40_pbl *pbl, dma_addr_t dma, int len)
{
	pbl->len = cpu_to_le32(len);
	pbl->pa_lo = cpu_to_le32(lower_32_bits(dma));
	pbl->pa_hi = cpu_to_le32(upper_32_bits(dma));
}

static void tn40_txdb_set(struct tn40_txdb *db, dma_addr_t dma, int len)
{
	db->wptr->len = len;
	db->wptr->addr.dma = dma;
}

struct tn40_mapping_info {
	dma_addr_t dma;
	size_t size;
};

/**
 * tn40_tx_map_skb - create and store DMA mappings for skb's data blocks
 * @priv: NIC private structure
 * @skb: socket buffer to map
 * @txdd: pointer to tx descriptor to be updated
 * @pkt_len: pointer to unsigned long value
 *
 * This function creates DMA mappings for skb's data blocks and writes them to
 * PBL of a new tx descriptor. It also stores them in the tx db, so they could
 * be unmapped after the data has been sent. It is the responsibility of the
 * caller to make sure that there is enough space in the txdb. The last
 * element holds a pointer to skb itself and is marked with a zero length.
 *
 * Return: 0 on success and negative value on error.
 */
static int tn40_tx_map_skb(struct tn40_priv *priv, struct sk_buff *skb,
			   struct tn40_txd_desc *txdd, unsigned int *pkt_len)
{
	struct tn40_mapping_info info[TN40_MAX_PBL];
	int nr_frags = skb_shinfo(skb)->nr_frags;
	struct tn40_pbl *pbl = &txdd->pbl[0];
	struct tn40_txdb *db = &priv->txdb;
	unsigned int size;
	int i, len, ret;
	dma_addr_t dma;

	netdev_dbg(priv->ndev, "TX skb %p skbLen %d dataLen %d frags %d\n", skb,
		   skb->len, skb->data_len, nr_frags);
	if (nr_frags > TN40_MAX_PBL - 1) {
		ret = skb_linearize(skb);
		if (ret)
			return ret;
		nr_frags = skb_shinfo(skb)->nr_frags;
	}
	/* initial skb */
	len = skb->len - skb->data_len;
	dma = dma_map_single(&priv->pdev->dev, skb->data, len,
			     DMA_TO_DEVICE);
	ret = dma_mapping_error(&priv->pdev->dev, dma);
	if (ret)
		return ret;

	tn40_txdb_set(db, dma, len);
	tn40_pbl_set(pbl++, db->wptr->addr.dma, db->wptr->len);
	*pkt_len = db->wptr->len;

	for (i = 0; i < nr_frags; i++) {
		skb_frag_t *frag = &skb_shinfo(skb)->frags[i];

		size = skb_frag_size(frag);
		dma = skb_frag_dma_map(&priv->pdev->dev, frag, 0,
				       size, DMA_TO_DEVICE);

		ret = dma_mapping_error(&priv->pdev->dev, dma);
		if (ret)
			goto mapping_error;
		info[i].dma = dma;
		info[i].size = size;
	}

	for (i = 0; i < nr_frags; i++) {
		tn40_tx_db_inc_wptr(db);
		tn40_txdb_set(db, info[i].dma, info[i].size);
		tn40_pbl_set(pbl++, db->wptr->addr.dma, db->wptr->len);
		*pkt_len += db->wptr->len;
	}

	/* SHORT_PKT_FIX */
	if (skb->len < TN40_SHORT_PACKET_SIZE)
		++nr_frags;

	/* Add skb clean up info. */
	tn40_tx_db_inc_wptr(db);
	db->wptr->len = -tn40_txd_sizes[nr_frags].bytes;
	db->wptr->addr.skb = skb;
	tn40_tx_db_inc_wptr(db);

	return 0;
 mapping_error:
	dma_unmap_page(&priv->pdev->dev, db->wptr->addr.dma, db->wptr->len,
		       DMA_TO_DEVICE);
	for (; i > 0; i--)
		dma_unmap_page(&priv->pdev->dev, info[i - 1].dma,
			       info[i - 1].size, DMA_TO_DEVICE);
	return -ENOMEM;
}

static int tn40_create_tx_ring(struct tn40_priv *priv)
{
	int ret;

	ret = tn40_fifo_alloc(priv, &priv->txd_fifo0.m, priv->txd_size,
			      TN40_REG_TXD_CFG0_0, TN40_REG_TXD_CFG1_0,
			      TN40_REG_TXD_RPTR_0, TN40_REG_TXD_WPTR_0);
	if (ret)
		return ret;

	ret = tn40_fifo_alloc(priv, &priv->txf_fifo0.m, priv->txf_size,
			      TN40_REG_TXF_CFG0_0, TN40_REG_TXF_CFG1_0,
			      TN40_REG_TXF_RPTR_0, TN40_REG_TXF_WPTR_0);
	if (ret)
		goto err_free_txd;

	/* The TX db has to keep mappings for all packets sent (on
	 * TxD) and not yet reclaimed (on TxF).
	 */
	ret = tn40_tx_db_init(&priv->txdb, max(priv->txd_size, priv->txf_size));
	if (ret)
		goto err_free_txf;

	/* SHORT_PKT_FIX */
	priv->b0_len = 64;
	priv->b0_va = dma_alloc_coherent(&priv->pdev->dev, priv->b0_len,
					 &priv->b0_dma, GFP_KERNEL);
	if (!priv->b0_va)
		goto err_free_db;

	priv->tx_level = TN40_MAX_TX_LEVEL;
	priv->tx_update_mark = priv->tx_level - 1024;
	return 0;
err_free_db:
	tn40_tx_db_close(&priv->txdb);
err_free_txf:
	tn40_fifo_free(priv, &priv->txf_fifo0.m);
err_free_txd:
	tn40_fifo_free(priv, &priv->txd_fifo0.m);
	return -ENOMEM;
}

/**
 * tn40_tx_space - Calculate the available space in the TX fifo.
 * @priv: NIC private structure
 *
 * Return: available space in TX fifo in bytes
 */
static int tn40_tx_space(struct tn40_priv *priv)
{
	struct tn40_txd_fifo *f = &priv->txd_fifo0;
	int fsize;

	f->m.rptr = tn40_read_reg(priv, f->m.reg_rptr) & TN40_TXF_WPTR_WR_PTR;
	fsize = f->m.rptr - f->m.wptr;
	if (fsize <= 0)
		fsize = f->m.memsz + fsize;
	return fsize;
}

#define TN40_TXD_FULL_CHECKSUM 7

static netdev_tx_t tn40_start_xmit(struct sk_buff *skb, struct net_device *ndev)
{
	struct tn40_priv *priv = netdev_priv(ndev);
	struct tn40_txd_fifo *f = &priv->txd_fifo0;
	int txd_checksum = TN40_TXD_FULL_CHECKSUM;
	struct tn40_txd_desc *txdd;
	int nr_frags, len, err;
	unsigned int pkt_len;
	int txd_vlan_id = 0;
	int txd_lgsnd = 0;
	int txd_vtag = 0;
	int txd_mss = 0;

	/* Build tx descriptor */
	txdd = (struct tn40_txd_desc *)(f->m.va + f->m.wptr);
	err = tn40_tx_map_skb(priv, skb, txdd, &pkt_len);
	if (err) {
		u64_stats_update_begin(&priv->syncp);
		priv->stats.tx_dropped++;
		u64_stats_update_end(&priv->syncp);
		dev_kfree_skb(skb);
		return NETDEV_TX_OK;
	}
	nr_frags = skb_shinfo(skb)->nr_frags;
	if (unlikely(skb->ip_summed != CHECKSUM_PARTIAL))
		txd_checksum = 0;

	if (skb_shinfo(skb)->gso_size) {
		txd_mss = skb_shinfo(skb)->gso_size;
		txd_lgsnd = 1;
		netdev_dbg(priv->ndev, "skb %p pkt len %d gso size = %d\n", skb,
			   pkt_len, txd_mss);
	}
	if (skb_vlan_tag_present(skb)) {
		/* Don't cut VLAN ID to 12 bits */
		txd_vlan_id = skb_vlan_tag_get(skb);
		txd_vtag = 1;
	}
	txdd->va_hi = 0;
	txdd->va_lo = 0;
	txdd->length = cpu_to_le16(pkt_len);
	txdd->mss = cpu_to_le16(txd_mss);
	txdd->txd_val1 =
		cpu_to_le32(TN40_TXD_W1_VAL
			    (tn40_txd_sizes[nr_frags].qwords, txd_checksum,
			     txd_vtag, txd_lgsnd, txd_vlan_id));
	netdev_dbg(priv->ndev, "=== w1 qwords[%d] %d =====\n", nr_frags,
		   tn40_txd_sizes[nr_frags].qwords);
	netdev_dbg(priv->ndev, "=== TxD desc =====================\n");
	netdev_dbg(priv->ndev, "=== w1: 0x%x ================\n",
		   txdd->txd_val1);
	netdev_dbg(priv->ndev, "=== w2: mss 0x%x len 0x%x\n", txdd->mss,
		   txdd->length);
	/* SHORT_PKT_FIX */
	if (pkt_len < TN40_SHORT_PACKET_SIZE) {
		struct tn40_pbl *pbl = &txdd->pbl[++nr_frags];

		txdd->length = cpu_to_le16(TN40_SHORT_PACKET_SIZE);
		txdd->txd_val1 =
			cpu_to_le32(TN40_TXD_W1_VAL
				    (tn40_txd_sizes[nr_frags].qwords,
				     txd_checksum, txd_vtag, txd_lgsnd,
				     txd_vlan_id));
		pbl->len = cpu_to_le32(TN40_SHORT_PACKET_SIZE - pkt_len);
		pbl->pa_lo = cpu_to_le32(lower_32_bits(priv->b0_dma));
		pbl->pa_hi = cpu_to_le32(upper_32_bits(priv->b0_dma));
		netdev_dbg(priv->ndev, "=== SHORT_PKT_FIX   ==============\n");
		netdev_dbg(priv->ndev, "=== nr_frags : %d   ==============\n",
			   nr_frags);
	}

	/* Increment TXD write pointer. In case of fifo wrapping copy
	 * reminder of the descriptor to the beginning.
	 */
	f->m.wptr += tn40_txd_sizes[nr_frags].bytes;
	len = f->m.wptr - f->m.memsz;
	if (unlikely(len >= 0)) {
		f->m.wptr = len;
		if (len > 0)
			memcpy(f->m.va, f->m.va + f->m.memsz, len);
	}
	/* Force memory writes to complete before letting the HW know
	 * there are new descriptors to fetch.
	 */
	wmb();

	priv->tx_level -= tn40_txd_sizes[nr_frags].bytes;
	if (priv->tx_level > priv->tx_update_mark) {
		tn40_write_reg(priv, f->m.reg_wptr,
			       f->m.wptr & TN40_TXF_WPTR_WR_PTR);
	} else {
		if (priv->tx_noupd++ > TN40_NO_UPD_PACKETS) {
			priv->tx_noupd = 0;
			tn40_write_reg(priv, f->m.reg_wptr,
				       f->m.wptr & TN40_TXF_WPTR_WR_PTR);
		}
	}

	u64_stats_update_begin(&priv->syncp);
	priv->stats.tx_packets++;
	priv->stats.tx_bytes += pkt_len;
	u64_stats_update_end(&priv->syncp);
	if (priv->tx_level < TN40_MIN_TX_LEVEL) {
		netdev_dbg(priv->ndev, "TX Q STOP level %d\n", priv->tx_level);
		netif_stop_queue(ndev);
	}

	return NETDEV_TX_OK;
}

static void tn40_tx_cleanup(struct tn40_priv *priv)
{
	struct tn40_txf_fifo *f = &priv->txf_fifo0;
	struct tn40_txdb *db = &priv->txdb;
	int tx_level = 0;

	f->m.wptr = tn40_read_reg(priv, f->m.reg_wptr) & TN40_TXF_WPTR_MASK;

	netif_tx_lock(priv->ndev);
	while (f->m.wptr != f->m.rptr) {
		f->m.rptr += TN40_TXF_DESC_SZ;
		f->m.rptr &= f->m.size_mask;
		/* Unmap all fragments */
		/* First has to come tx_maps containing DMA */
		do {
			dma_addr_t addr = db->rptr->addr.dma;
			size_t size =  db->rptr->len;

			netif_tx_unlock(priv->ndev);
			dma_unmap_page(&priv->pdev->dev, addr,
				       size, DMA_TO_DEVICE);
			netif_tx_lock(priv->ndev);
			tn40_tx_db_inc_rptr(db);
		} while (db->rptr->len > 0);
		tx_level -= db->rptr->len; /* '-' Because the len is negative */

		/* Now should come skb pointer - free it */
		dev_kfree_skb_any(db->rptr->addr.skb);
		netdev_dbg(priv->ndev, "dev_kfree_skb_any %p %d\n",
			   db->rptr->addr.skb, -db->rptr->len);
		tn40_tx_db_inc_rptr(db);
	}

	/* Let the HW know which TXF descriptors were cleaned */
	tn40_write_reg(priv, f->m.reg_rptr, f->m.rptr & TN40_TXF_WPTR_WR_PTR);

	/* We reclaimed resources, so in case the Q is stopped by xmit
	 * callback, we resume the transmission and use tx_lock to
	 * synchronize with xmit.
	 */
	priv->tx_level += tx_level;
	if (priv->tx_noupd) {
		priv->tx_noupd = 0;
		tn40_write_reg(priv, priv->txd_fifo0.m.reg_wptr,
			       priv->txd_fifo0.m.wptr & TN40_TXF_WPTR_WR_PTR);
	}
	if (unlikely(netif_queue_stopped(priv->ndev) &&
		     netif_carrier_ok(priv->ndev) &&
		     (priv->tx_level >= TN40_MAX_TX_LEVEL / 2))) {
		netdev_dbg(priv->ndev, "TX Q WAKE level %d\n", priv->tx_level);
		netif_wake_queue(priv->ndev);
	}
	netif_tx_unlock(priv->ndev);
}

static void tn40_tx_free_skbs(struct tn40_priv *priv)
{
	struct tn40_txdb *db = &priv->txdb;

	while (db->rptr != db->wptr) {
		if (likely(db->rptr->len))
			dma_unmap_page(&priv->pdev->dev, db->rptr->addr.dma,
				       db->rptr->len, DMA_TO_DEVICE);
		else
			dev_kfree_skb(db->rptr->addr.skb);
		tn40_tx_db_inc_rptr(db);
	}
}

static void tn40_destroy_tx_ring(struct tn40_priv *priv)
{
	tn40_tx_free_skbs(priv);
	tn40_fifo_free(priv, &priv->txd_fifo0.m);
	tn40_fifo_free(priv, &priv->txf_fifo0.m);
	tn40_tx_db_close(&priv->txdb);
	/* SHORT_PKT_FIX */
	if (priv->b0_len) {
		dma_free_coherent(&priv->pdev->dev, priv->b0_len, priv->b0_va,
				  priv->b0_dma);
		priv->b0_len = 0;
	}
}

/**
 * tn40_tx_push_desc - Push a descriptor to TxD fifo.
 *
 * @priv: NIC private structure
 * @data: desc's data
 * @size: desc's size
 *
 * This function pushes desc to TxD fifo and overlaps it if needed.
 *
 * This function does not check for available space, nor does it check
 * that the data size is smaller than the fifo size. Checking for
 * space is the responsibility of the caller.
 */
static void tn40_tx_push_desc(struct tn40_priv *priv, void *data, int size)
{
	struct tn40_txd_fifo *f = &priv->txd_fifo0;
	int i = f->m.memsz - f->m.wptr;

	if (size == 0)
		return;

	if (i > size) {
		memcpy(f->m.va + f->m.wptr, data, size);
		f->m.wptr += size;
	} else {
		memcpy(f->m.va + f->m.wptr, data, i);
		f->m.wptr = size - i;
		memcpy(f->m.va, data + i, f->m.wptr);
	}
	tn40_write_reg(priv, f->m.reg_wptr, f->m.wptr & TN40_TXF_WPTR_WR_PTR);
}

/**
 * tn40_tx_push_desc_safe - push descriptor to TxD fifo in a safe way.
 *
 * @priv: NIC private structure
 * @data: descriptor data
 * @size: descriptor size
 *
 * This function does check for available space and, if necessary,
 * waits for the NIC to read existing data before writing new data.
 */
static void tn40_tx_push_desc_safe(struct tn40_priv *priv, void *data, int size)
{
	int timer = 0;

	while (size > 0) {
		/* We subtract 8 because when the fifo is full rptr ==
		 * wptr, which also means that fifo is empty, we can
		 * understand the difference, but could the HW do the
		 * same ???
		 */
		int avail = tn40_tx_space(priv) - 8;

		if (avail <= 0) {
			if (timer++ > 300) /* Prevent endless loop */
				break;
			/* Give the HW a chance to clean the fifo */
			usleep_range(50, 60);
			continue;
		}
		avail = min(avail, size);
		netdev_dbg(priv->ndev,
			   "about to push  %d bytes starting %p size %d\n",
			   avail, data, size);
		tn40_tx_push_desc(priv, data, avail);
		size -= avail;
		data += avail;
	}
}

int tn40_set_link_speed(struct tn40_priv *priv, u32 speed)
{
	u32 val;
	int i;

	netdev_dbg(priv->ndev, "speed %d\n", speed);
	switch (speed) {
	case SPEED_10000:
	case SPEED_5000:
	case SPEED_2500:
		netdev_dbg(priv->ndev, "link_speed %d\n", speed);

		tn40_write_reg(priv, 0x1010, 0x217);	/*ETHSD.REFCLK_CONF  */
		tn40_write_reg(priv, 0x104c, 0x4c);	/*ETHSD.L0_RX_PCNT  */
		tn40_write_reg(priv, 0x1050, 0x4c);	/*ETHSD.L1_RX_PCNT  */
		tn40_write_reg(priv, 0x1054, 0x4c);	/*ETHSD.L2_RX_PCNT  */
		tn40_write_reg(priv, 0x1058, 0x4c);	/*ETHSD.L3_RX_PCNT  */
		tn40_write_reg(priv, 0x102c, 0x434);	/*ETHSD.L0_TX_PCNT  */
		tn40_write_reg(priv, 0x1030, 0x434);	/*ETHSD.L1_TX_PCNT  */
		tn40_write_reg(priv, 0x1034, 0x434);	/*ETHSD.L2_TX_PCNT  */
		tn40_write_reg(priv, 0x1038, 0x434);	/*ETHSD.L3_TX_PCNT  */
		tn40_write_reg(priv, 0x6300, 0x0400);	/*MAC.PCS_CTRL */

		tn40_write_reg(priv, 0x1018, 0x00);	/*Mike2 */
		udelay(5);
		tn40_write_reg(priv, 0x1018, 0x04);	/*Mike2 */
		udelay(5);
		tn40_write_reg(priv, 0x1018, 0x06);	/*Mike2 */
		udelay(5);
		/*MikeFix1 */
		/*L0: 0x103c , L1: 0x1040 , L2: 0x1044 , L3: 0x1048 =0x81644 */
		tn40_write_reg(priv, 0x103c, 0x81644);	/*ETHSD.L0_TX_DCNT  */
		tn40_write_reg(priv, 0x1040, 0x81644);	/*ETHSD.L1_TX_DCNT  */
		tn40_write_reg(priv, 0x1044, 0x81644);	/*ETHSD.L2_TX_DCNT  */
		tn40_write_reg(priv, 0x1048, 0x81644);	/*ETHSD.L3_TX_DCNT  */
		tn40_write_reg(priv, 0x1014, 0x043);	/*ETHSD.INIT_STAT */
		for (i = 1000; i; i--) {
			usleep_range(50, 60);
			/*ETHSD.INIT_STAT */
			val = tn40_read_reg(priv, 0x1014);
			if (val & (1 << 9)) {
				/*ETHSD.INIT_STAT */
				tn40_write_reg(priv, 0x1014, 0x3);
				/*ETHSD.INIT_STAT */
				val = tn40_read_reg(priv, 0x1014);

				break;
			}
		}
		if (!i)
			netdev_err(priv->ndev, "MAC init timeout!\n");

		tn40_write_reg(priv, 0x6350, 0x0);	/*MAC.PCS_IF_MODE */
		tn40_write_reg(priv, TN40_REG_CTRLST, 0xC13);	/*0x93//0x13 */
		tn40_write_reg(priv, 0x111c, 0x7ff);	/*MAC.MAC_RST_CNT */
		usleep_range(2000, 2100);

		tn40_write_reg(priv, 0x111c, 0x0);	/*MAC.MAC_RST_CNT */
		break;

	case SPEED_1000:
	case SPEED_100:
		tn40_write_reg(priv, 0x1010, 0x613);	/*ETHSD.REFCLK_CONF */
		tn40_write_reg(priv, 0x104c, 0x4d);	/*ETHSD.L0_RX_PCNT  */
		tn40_write_reg(priv, 0x1050, 0x0);	/*ETHSD.L1_RX_PCNT  */
		tn40_write_reg(priv, 0x1054, 0x0);	/*ETHSD.L2_RX_PCNT  */
		tn40_write_reg(priv, 0x1058, 0x0);	/*ETHSD.L3_RX_PCNT  */
		tn40_write_reg(priv, 0x102c, 0x35);	/*ETHSD.L0_TX_PCNT  */
		tn40_write_reg(priv, 0x1030, 0x0);	/*ETHSD.L1_TX_PCNT  */
		tn40_write_reg(priv, 0x1034, 0x0);	/*ETHSD.L2_TX_PCNT  */
		tn40_write_reg(priv, 0x1038, 0x0);	/*ETHSD.L3_TX_PCNT  */
		tn40_write_reg(priv, 0x6300, 0x01140);	/*MAC.PCS_CTRL */

		tn40_write_reg(priv, 0x1014, 0x043);	/*ETHSD.INIT_STAT */
		for (i = 1000; i; i--) {
			usleep_range(50, 60);
			val = tn40_read_reg(priv, 0x1014); /*ETHSD.INIT_STAT */
			if (val & (1 << 9)) {
				/*ETHSD.INIT_STAT */
				tn40_write_reg(priv, 0x1014, 0x3);
				/*ETHSD.INIT_STAT */
				val = tn40_read_reg(priv, 0x1014);

				break;
			}
		}
		if (!i)
			netdev_err(priv->ndev, "MAC init timeout!\n");

		tn40_write_reg(priv, 0x6350, 0x2b);	/*MAC.PCS_IF_MODE 1g */
		tn40_write_reg(priv, 0x6310, 0x9801);	/*MAC.PCS_DEV_AB */

		tn40_write_reg(priv, 0x6314, 0x1);	/*MAC.PCS_PART_AB */
		tn40_write_reg(priv, 0x6348, 0xc8);	/*MAC.PCS_LINK_LO */
		tn40_write_reg(priv, 0x634c, 0xc8);	/*MAC.PCS_LINK_HI */
		usleep_range(50, 60);
		tn40_write_reg(priv, TN40_REG_CTRLST, 0xC13);	/*0x93//0x13 */
		tn40_write_reg(priv, 0x111c, 0x7ff);	/*MAC.MAC_RST_CNT */
		usleep_range(2000, 2100);

		tn40_write_reg(priv, 0x111c, 0x0);	/*MAC.MAC_RST_CNT */
		tn40_write_reg(priv, 0x6300, 0x1140);	/*MAC.PCS_CTRL */
		break;

	case 0:		/* Link down */
		tn40_write_reg(priv, 0x104c, 0x0);	/*ETHSD.L0_RX_PCNT  */
		tn40_write_reg(priv, 0x1050, 0x0);	/*ETHSD.L1_RX_PCNT  */
		tn40_write_reg(priv, 0x1054, 0x0);	/*ETHSD.L2_RX_PCNT  */
		tn40_write_reg(priv, 0x1058, 0x0);	/*ETHSD.L3_RX_PCNT  */
		tn40_write_reg(priv, 0x102c, 0x0);	/*ETHSD.L0_TX_PCNT  */
		tn40_write_reg(priv, 0x1030, 0x0);	/*ETHSD.L1_TX_PCNT  */
		tn40_write_reg(priv, 0x1034, 0x0);	/*ETHSD.L2_TX_PCNT  */
		tn40_write_reg(priv, 0x1038, 0x0);	/*ETHSD.L3_TX_PCNT  */

		tn40_write_reg(priv, TN40_REG_CTRLST, 0x800);
		tn40_write_reg(priv, 0x111c, 0x7ff);	/*MAC.MAC_RST_CNT */
		usleep_range(2000, 2100);

		tn40_write_reg(priv, 0x111c, 0x0);	/*MAC.MAC_RST_CNT */
		break;

	default:
		netdev_err(priv->ndev,
			   "Link speed was not identified yet (%d)\n", speed);
		speed = 0;
		break;
	}
	return speed;
}

static void tn40_link_changed(struct tn40_priv *priv)
{
	u32 link = tn40_read_reg(priv,
				 TN40_REG_MAC_LNK_STAT) & TN40_MAC_LINK_STAT;

	netdev_dbg(priv->ndev, "link changed %u\n", link);
}

static void tn40_isr_extra(struct tn40_priv *priv, u32 isr)
{
	if (isr & (TN40_IR_LNKCHG0 | TN40_IR_LNKCHG1 | TN40_IR_TMR0)) {
		netdev_dbg(priv->ndev, "isr = 0x%x\n", isr);
		tn40_link_changed(priv);
	}
}

static irqreturn_t tn40_isr_napi(int irq, void *dev)
{
	struct tn40_priv *priv = netdev_priv((struct net_device *)dev);
	u32 isr;

	isr = tn40_read_reg(priv, TN40_REG_ISR_MSK0);

	if (unlikely(!isr)) {
		tn40_enable_interrupts(priv);
		return IRQ_NONE;	/* Not our interrupt */
	}

	if (isr & TN40_IR_EXTRA)
		tn40_isr_extra(priv, isr);

	if (isr & (TN40_IR_RX_DESC_0 | TN40_IR_TX_FREE_0 | TN40_IR_TMR1)) {
		if (likely(napi_schedule_prep(&priv->napi))) {
			__napi_schedule(&priv->napi);
			return IRQ_HANDLED;
		}
		/* We get here if an interrupt has slept into the
		 * small time window between these lines in
		 * tn40_poll: tn40_enable_interrupts(priv); return 0;
		 *
		 * Currently interrupts are disabled (since we read
		 * the ISR register) and we have failed to register
		 * the next poll. So we read the regs to trigger the
		 * chip and allow further interrupts.
		 */
		tn40_read_reg(priv, TN40_REG_TXF_WPTR_0);
		tn40_read_reg(priv, TN40_REG_RXD_WPTR_0);
	}

	tn40_enable_interrupts(priv);
	return IRQ_HANDLED;
}

static int tn40_poll(struct napi_struct *napi, int budget)
{
	struct tn40_priv *priv = container_of(napi, struct tn40_priv, napi);
	int work_done;

	tn40_tx_cleanup(priv);

	if (!budget)
		return 0;

	work_done = tn40_rx_receive(priv, budget);
	if (work_done == budget)
		return budget;

	if (napi_complete_done(napi, work_done))
		tn40_enable_interrupts(priv);
	return work_done;
}

static int tn40_fw_load(struct tn40_priv *priv)
{
	const struct firmware *fw = NULL;
	int master, ret;
	u32 val;

	ret = request_firmware(&fw, TN40_FIRMWARE_NAME, &priv->pdev->dev);
	if (ret)
		return ret;

	master = tn40_read_reg(priv, TN40_REG_INIT_SEMAPHORE);
	if (!tn40_read_reg(priv, TN40_REG_INIT_STATUS) && master) {
		netdev_dbg(priv->ndev, "Loading FW...\n");
		tn40_tx_push_desc_safe(priv, (void *)fw->data, fw->size);
		msleep(100);
	}
	ret = read_poll_timeout(tn40_read_reg, val, val, 2000, 400000, false,
				priv, TN40_REG_INIT_STATUS);
	if (master)
		tn40_write_reg(priv, TN40_REG_INIT_SEMAPHORE, 1);

	if (ret) {
		netdev_err(priv->ndev, "firmware loading failed\n");
		netdev_dbg(priv->ndev, "VPC: 0x%x VIC: 0x%x STATUS: 0x%xd\n",
			   tn40_read_reg(priv, TN40_REG_VPC),
			   tn40_read_reg(priv, TN40_REG_VIC),
			   tn40_read_reg(priv, TN40_REG_INIT_STATUS));
		ret = -EIO;
	} else {
		netdev_dbg(priv->ndev, "firmware loading success\n");
	}
	release_firmware(fw);
	return ret;
}

static void tn40_restore_mac(struct net_device *ndev, struct tn40_priv *priv)
{
	u32 val;

	netdev_dbg(priv->ndev, "mac0 =%x mac1 =%x mac2 =%x\n",
		   tn40_read_reg(priv, TN40_REG_UNC_MAC0_A),
		   tn40_read_reg(priv, TN40_REG_UNC_MAC1_A),
		   tn40_read_reg(priv, TN40_REG_UNC_MAC2_A));

	val = (ndev->dev_addr[0] << 8) | (ndev->dev_addr[1]);
	tn40_write_reg(priv, TN40_REG_UNC_MAC2_A, val);
	val = (ndev->dev_addr[2] << 8) | (ndev->dev_addr[3]);
	tn40_write_reg(priv, TN40_REG_UNC_MAC1_A, val);
	val = (ndev->dev_addr[4] << 8) | (ndev->dev_addr[5]);
	tn40_write_reg(priv, TN40_REG_UNC_MAC0_A, val);

	/* More then IP MAC address */
	tn40_write_reg(priv, TN40_REG_MAC_ADDR_0,
		       (ndev->dev_addr[3] << 24) | (ndev->dev_addr[2] << 16) |
		       (ndev->dev_addr[1] << 8) | (ndev->dev_addr[0]));
	tn40_write_reg(priv, TN40_REG_MAC_ADDR_1,
		       (ndev->dev_addr[5] << 8) | (ndev->dev_addr[4]));

	netdev_dbg(priv->ndev, "mac0 =%x mac1 =%x mac2 =%x\n",
		   tn40_read_reg(priv, TN40_REG_UNC_MAC0_A),
		   tn40_read_reg(priv, TN40_REG_UNC_MAC1_A),
		   tn40_read_reg(priv, TN40_REG_UNC_MAC2_A));
}

static void tn40_hw_start(struct tn40_priv *priv)
{
	tn40_write_reg(priv, TN40_REG_FRM_LENGTH, 0X3FE0);
	tn40_write_reg(priv, TN40_REG_GMAC_RXF_A, 0X10fd);
	/*MikeFix1 */
	/*L0: 0x103c , L1: 0x1040 , L2: 0x1044 , L3: 0x1048 =0x81644 */
	tn40_write_reg(priv, 0x103c, 0x81644);	/*ETHSD.L0_TX_DCNT  */
	tn40_write_reg(priv, 0x1040, 0x81644);	/*ETHSD.L1_TX_DCNT  */
	tn40_write_reg(priv, 0x1044, 0x81644);	/*ETHSD.L2_TX_DCNT  */
	tn40_write_reg(priv, 0x1048, 0x81644);	/*ETHSD.L3_TX_DCNT  */
	tn40_write_reg(priv, TN40_REG_RX_FIFO_SECTION, 0x10);
	tn40_write_reg(priv, TN40_REG_TX_FIFO_SECTION, 0xE00010);
	tn40_write_reg(priv, TN40_REG_RX_FULLNESS, 0);
	tn40_write_reg(priv, TN40_REG_TX_FULLNESS, 0);

	tn40_write_reg(priv, TN40_REG_VGLB, 0);
	tn40_write_reg(priv, TN40_REG_MAX_FRAME_A,
		       priv->rxf_fifo0.m.pktsz & TN40_MAX_FRAME_AB_VAL);
	tn40_write_reg(priv, TN40_REG_RDINTCM0, priv->rdintcm);
	tn40_write_reg(priv, TN40_REG_RDINTCM2, 0);

	/* old val = 0x300064 */
	tn40_write_reg(priv, TN40_REG_TDINTCM0, priv->tdintcm);

	/* Enable timer interrupt once in 2 secs. */
	tn40_restore_mac(priv->ndev, priv);

	/* Pause frame */
	tn40_write_reg(priv, 0x12E0, 0x28);
	tn40_write_reg(priv, TN40_REG_PAUSE_QUANT, 0xFFFF);
	tn40_write_reg(priv, 0x6064, 0xF);

	tn40_write_reg(priv, TN40_REG_GMAC_RXF_A,
		       TN40_GMAC_RX_FILTER_OSEN | TN40_GMAC_RX_FILTER_TXFC |
		       TN40_GMAC_RX_FILTER_AM | TN40_GMAC_RX_FILTER_AB);

	tn40_enable_interrupts(priv);
}

static int tn40_hw_reset(struct tn40_priv *priv)
{
	u32 val;

	/* Reset sequences: read, write 1, read, write 0 */
	val = tn40_read_reg(priv, TN40_REG_CLKPLL);
	tn40_write_reg(priv, TN40_REG_CLKPLL, (val | TN40_CLKPLL_SFTRST) + 0x8);
	usleep_range(50, 60);
	val = tn40_read_reg(priv, TN40_REG_CLKPLL);
	tn40_write_reg(priv, TN40_REG_CLKPLL, val & ~TN40_CLKPLL_SFTRST);

	/* Check that the PLLs are locked and reset ended */
	val = read_poll_timeout(tn40_read_reg, val,
				(val & TN40_CLKPLL_LKD) == TN40_CLKPLL_LKD,
				10000, 700000, false, priv, TN40_REG_CLKPLL);
	if (val)
		return -EIO;

	usleep_range(50, 60);
	/* Do any PCI-E read transaction */
	tn40_read_reg(priv, TN40_REG_RXD_CFG0_0);
	return 0;
}

static void tn40_sw_reset(struct tn40_priv *priv)
{
	int i, ret;
	u32 val;

	/* 1. load MAC (obsolete) */
	/* 2. disable Rx (and Tx) */
	tn40_write_reg(priv, TN40_REG_GMAC_RXF_A, 0);
	msleep(100);
	/* 3. Disable port */
	tn40_write_reg(priv, TN40_REG_DIS_PORT, 1);
	/* 4. Disable queue */
	tn40_write_reg(priv, TN40_REG_DIS_QU, 1);
	/* 5. Wait until hw is disabled */
	ret = read_poll_timeout(tn40_read_reg, val, val & 1, 10000, 500000,
				false, priv, TN40_REG_RST_PORT);
	if (ret)
		netdev_err(priv->ndev, "SW reset timeout. continuing anyway\n");

	/* 6. Disable interrupts */
	tn40_write_reg(priv, TN40_REG_RDINTCM0, 0);
	tn40_write_reg(priv, TN40_REG_TDINTCM0, 0);
	tn40_write_reg(priv, TN40_REG_IMR, 0);
	tn40_read_reg(priv, TN40_REG_ISR);

	/* 7. Reset queue */
	tn40_write_reg(priv, TN40_REG_RST_QU, 1);
	/* 8. Reset port */
	tn40_write_reg(priv, TN40_REG_RST_PORT, 1);
	/* 9. Zero all read and write pointers */
	for (i = TN40_REG_TXD_WPTR_0; i <= TN40_REG_TXF_RPTR_3; i += 0x10)
		tn40_write_reg(priv, i, 0);
	/* 10. Unset port disable */
	tn40_write_reg(priv, TN40_REG_DIS_PORT, 0);
	/* 11. Unset queue disable */
	tn40_write_reg(priv, TN40_REG_DIS_QU, 0);
	/* 12. Unset queue reset */
	tn40_write_reg(priv, TN40_REG_RST_QU, 0);
	/* 13. Unset port reset */
	tn40_write_reg(priv, TN40_REG_RST_PORT, 0);
	/* 14. Enable Rx */
	/* Skipped. will be done later */
}

static int tn40_start(struct tn40_priv *priv)
{
	int ret;

	ret = tn40_create_tx_ring(priv);
	if (ret) {
		netdev_err(priv->ndev, "failed to tx init %d\n", ret);
		return ret;
	}

	ret = tn40_create_rx_ring(priv);
	if (ret) {
		netdev_err(priv->ndev, "failed to rx init %d\n", ret);
		goto err_tx_ring;
	}

	tn40_rx_alloc_buffers(priv);
	if (tn40_rxdb_available(priv->rxdb0) != 1) {
		ret = -ENOMEM;
		netdev_err(priv->ndev, "failed to allocate rx buffers\n");
		goto err_rx_ring;
	}

	ret = request_irq(priv->pdev->irq, &tn40_isr_napi, IRQF_SHARED,
			  priv->ndev->name, priv->ndev);
	if (ret) {
		netdev_err(priv->ndev, "failed to request irq %d\n", ret);
		goto err_rx_ring;
	}

	tn40_hw_start(priv);
	return 0;
err_rx_ring:
	tn40_destroy_rx_ring(priv);
err_tx_ring:
	tn40_destroy_tx_ring(priv);
	return ret;
}

static void tn40_stop(struct tn40_priv *priv)
{
	tn40_disable_interrupts(priv);
	free_irq(priv->pdev->irq, priv->ndev);
	tn40_sw_reset(priv);
	tn40_destroy_tx_ring(priv);
	tn40_destroy_rx_ring(priv);
}

static int tn40_close(struct net_device *ndev)
{
	struct tn40_priv *priv = netdev_priv(ndev);

	phylink_stop(priv->phylink);
	phylink_disconnect_phy(priv->phylink);

	napi_disable(&priv->napi);
	netif_napi_del(&priv->napi);
	tn40_stop(priv);
	return 0;
}

static int tn40_open(struct net_device *dev)
{
	struct tn40_priv *priv = netdev_priv(dev);
	int ret;

	ret = phylink_connect_phy(priv->phylink, priv->phydev);
	if (ret) {
		netdev_err(dev, "failed to connect to phy %d\n", ret);
		return ret;
	}
	tn40_sw_reset(priv);
	ret = tn40_start(priv);
	if (ret) {
		phylink_disconnect_phy(priv->phylink);
		netdev_err(dev, "failed to start %d\n", ret);
		return ret;
	}
	napi_enable(&priv->napi);
	phylink_start(priv->phylink);
	netif_start_queue(priv->ndev);
	return 0;
}

static void __tn40_vlan_rx_vid(struct net_device *ndev, uint16_t vid,
			       int enable)
{
	struct tn40_priv *priv = netdev_priv(ndev);
	u32 reg, bit, val;

	netdev_dbg(priv->ndev, "vid =%d value =%d\n", (int)vid, enable);
	reg = TN40_REG_VLAN_0 + (vid / 32) * 4;
	bit = 1 << vid % 32;
	val = tn40_read_reg(priv, reg);
	netdev_dbg(priv->ndev, "reg =%x, val =%x, bit =%d\n", reg, val, bit);
	if (enable)
		val |= bit;
	else
		val &= ~bit;
	netdev_dbg(priv->ndev, "new val %x\n", val);
	tn40_write_reg(priv, reg, val);
}

static int tn40_vlan_rx_add_vid(struct net_device *ndev,
				__always_unused __be16 proto, u16 vid)
{
	__tn40_vlan_rx_vid(ndev, vid, 1);
	return 0;
}

static int tn40_vlan_rx_kill_vid(struct net_device *ndev,
				 __always_unused __be16 proto, u16 vid)
{
	__tn40_vlan_rx_vid(ndev, vid, 0);
	return 0;
}

static void tn40_setmulti(struct net_device *ndev)
{
	u32 rxf_val = TN40_GMAC_RX_FILTER_AM | TN40_GMAC_RX_FILTER_AB |
		TN40_GMAC_RX_FILTER_OSEN | TN40_GMAC_RX_FILTER_TXFC;
	struct tn40_priv *priv = netdev_priv(ndev);
	int i;

	/* IMF - imperfect (hash) rx multicast filter */
	/* PMF - perfect rx multicast filter */

	/* FIXME: RXE(OFF) */
	if (ndev->flags & IFF_PROMISC) {
		rxf_val |= TN40_GMAC_RX_FILTER_PRM;
	} else if (ndev->flags & IFF_ALLMULTI) {
		/* set IMF to accept all multicast frames */
		for (i = 0; i < TN40_MAC_MCST_HASH_NUM; i++)
			tn40_write_reg(priv,
				       TN40_REG_RX_MCST_HASH0 + i * 4, ~0);
	} else if (netdev_mc_count(ndev)) {
		struct netdev_hw_addr *mclist;
		u32 reg, val;
		u8 hash;

		/* Set IMF to deny all multicast frames */
		for (i = 0; i < TN40_MAC_MCST_HASH_NUM; i++)
			tn40_write_reg(priv,
				       TN40_REG_RX_MCST_HASH0 + i * 4, 0);

		/* Set PMF to deny all multicast frames */
		for (i = 0; i < TN40_MAC_MCST_NUM; i++) {
			tn40_write_reg(priv,
				       TN40_REG_RX_MAC_MCST0 + i * 8, 0);
			tn40_write_reg(priv,
				       TN40_REG_RX_MAC_MCST1 + i * 8, 0);
		}
		/* Use PMF to accept first MAC_MCST_NUM (15) addresses */

		/* TBD: Sort the addresses and write them in ascending
		 * order into RX_MAC_MCST regs. we skip this phase now
		 * and accept ALL multicast frames through IMF. Accept
		 * the rest of addresses throw IMF.
		 */
		netdev_for_each_mc_addr(mclist, ndev) {
			hash = 0;
			for (i = 0; i < ETH_ALEN; i++)
				hash ^= mclist->addr[i];

			reg = TN40_REG_RX_MCST_HASH0 + ((hash >> 5) << 2);
			val = tn40_read_reg(priv, reg);
			val |= (1 << (hash % 32));
			tn40_write_reg(priv, reg, val);
		}
	} else {
		rxf_val |= TN40_GMAC_RX_FILTER_AB;
	}
	tn40_write_reg(priv, TN40_REG_GMAC_RXF_A, rxf_val);
	/* Enable RX */
	/* FIXME: RXE(ON) */
}

static int tn40_set_mac(struct net_device *ndev, void *p)
{
	struct tn40_priv *priv = netdev_priv(ndev);
	struct sockaddr *addr = p;

	eth_hw_addr_set(ndev, addr->sa_data);
	tn40_restore_mac(ndev, priv);
	return 0;
}

static void tn40_mac_init(struct tn40_priv *priv)
{
	u8 addr[ETH_ALEN];
	u64 val;

	val = (u64)tn40_read_reg(priv, TN40_REG_UNC_MAC0_A);
	val |= (u64)tn40_read_reg(priv, TN40_REG_UNC_MAC1_A) << 16;
	val |= (u64)tn40_read_reg(priv, TN40_REG_UNC_MAC2_A) << 32;

	u64_to_ether_addr(val, addr);
	eth_hw_addr_set(priv->ndev, addr);
}

static void tn40_get_stats(struct net_device *ndev,
			   struct rtnl_link_stats64 *stats)
{
	struct tn40_priv *priv = netdev_priv(ndev);
	unsigned int start;

	do {
		start = u64_stats_fetch_begin(&priv->syncp);
		stats->tx_packets = priv->stats.tx_packets;
		stats->tx_bytes = priv->stats.tx_bytes;
		stats->tx_dropped = priv->stats.tx_dropped;

		stats->rx_packets = priv->stats.rx_packets;
		stats->rx_bytes = priv->stats.rx_bytes;
		stats->rx_dropped = priv->stats.rx_dropped;
		stats->rx_errors = priv->stats.rx_errors;
	} while (u64_stats_fetch_retry(&priv->syncp, start));
}

static const struct net_device_ops tn40_netdev_ops = {
	.ndo_open = tn40_open,
	.ndo_stop = tn40_close,
	.ndo_start_xmit = tn40_start_xmit,
	.ndo_validate_addr = eth_validate_addr,
	.ndo_set_rx_mode = tn40_setmulti,
	.ndo_get_stats64 = tn40_get_stats,
	.ndo_set_mac_address = tn40_set_mac,
	.ndo_vlan_rx_add_vid = tn40_vlan_rx_add_vid,
	.ndo_vlan_rx_kill_vid = tn40_vlan_rx_kill_vid,
};

static int tn40_ethtool_get_link_ksettings(struct net_device *ndev,
					   struct ethtool_link_ksettings *cmd)
{
	struct tn40_priv *priv = netdev_priv(ndev);

	return phylink_ethtool_ksettings_get(priv->phylink, cmd);
}

static const struct ethtool_ops tn40_ethtool_ops = {
	.get_link = ethtool_op_get_link,
	.get_link_ksettings = tn40_ethtool_get_link_ksettings,
};

static void tn40_get_queue_stats_rx(struct net_device *ndev, int idx,
				    struct netdev_queue_stats_rx *stats)
{
	struct tn40_priv *priv = netdev_priv(ndev);
	unsigned int start;

	do {
		start = u64_stats_fetch_begin(&priv->syncp);

		stats->packets = priv->stats.rx_packets;
		stats->bytes = priv->stats.rx_bytes;
		stats->alloc_fail = priv->alloc_fail;
	} while (u64_stats_fetch_retry(&priv->syncp, start));
}

static void tn40_get_queue_stats_tx(struct net_device *ndev, int idx,
				    struct netdev_queue_stats_tx *stats)
{
	struct tn40_priv *priv = netdev_priv(ndev);
	unsigned int start;

	do {
		start = u64_stats_fetch_begin(&priv->syncp);

		stats->packets = priv->stats.tx_packets;
		stats->bytes = priv->stats.tx_bytes;
	} while (u64_stats_fetch_retry(&priv->syncp, start));
}

static void tn40_get_base_stats(struct net_device *ndev,
				struct netdev_queue_stats_rx *rx,
				struct netdev_queue_stats_tx *tx)
{
	rx->packets = 0;
	rx->bytes = 0;
	rx->alloc_fail = 0;

	tx->packets = 0;
	tx->bytes = 0;
}

static const struct netdev_stat_ops tn40_stat_ops = {
	.get_queue_stats_rx = tn40_get_queue_stats_rx,
	.get_queue_stats_tx = tn40_get_queue_stats_tx,
	.get_base_stats = tn40_get_base_stats,
};

static int tn40_priv_init(struct tn40_priv *priv)
{
	int ret;

	tn40_set_link_speed(priv, 0);

	/* Set GPIO[9:0] to output 0 */
	tn40_write_reg(priv, 0x51E0, 0x30010006);	/* GPIO_OE_ WR CMD */
	tn40_write_reg(priv, 0x51F0, 0x0);	/* GPIO_OE_ DATA */
	tn40_write_reg(priv, TN40_REG_MDIO_CMD_STAT, 0x3ec8);

	/* we use tx descriptors to load a firmware. */
	ret = tn40_create_tx_ring(priv);
	if (ret)
		return ret;
	ret = tn40_fw_load(priv);
	tn40_destroy_tx_ring(priv);
	return ret;
}

static struct net_device *tn40_netdev_alloc(struct pci_dev *pdev)
{
	struct net_device *ndev;

	ndev = devm_alloc_etherdev(&pdev->dev, sizeof(struct tn40_priv));
	if (!ndev)
		return NULL;
	ndev->netdev_ops = &tn40_netdev_ops;
	ndev->ethtool_ops = &tn40_ethtool_ops;
	ndev->stat_ops = &tn40_stat_ops;
	ndev->tx_queue_len = TN40_NDEV_TXQ_LEN;
	ndev->mem_start = pci_resource_start(pdev, 0);
	ndev->mem_end = pci_resource_end(pdev, 0);
	ndev->min_mtu = ETH_ZLEN;
	ndev->max_mtu = TN40_MAX_MTU;

	ndev->features = NETIF_F_IP_CSUM |
		NETIF_F_SG |
		NETIF_F_FRAGLIST |
		NETIF_F_TSO | NETIF_F_GRO |
		NETIF_F_RXCSUM |
		NETIF_F_RXHASH |
		NETIF_F_HW_VLAN_CTAG_TX |
		NETIF_F_HW_VLAN_CTAG_RX |
		NETIF_F_HW_VLAN_CTAG_FILTER;
	ndev->vlan_features = NETIF_F_IP_CSUM |
			       NETIF_F_SG |
			       NETIF_F_TSO | NETIF_F_GRO | NETIF_F_RXHASH;

	if (dma_get_mask(&pdev->dev) == DMA_BIT_MASK(64)) {
		ndev->features |= NETIF_F_HIGHDMA;
		ndev->vlan_features |= NETIF_F_HIGHDMA;
	}
	ndev->hw_features |= ndev->features;

	SET_NETDEV_DEV(ndev, &pdev->dev);
	netif_stop_queue(ndev);
	return ndev;
}

static int tn40_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	struct net_device *ndev;
	struct tn40_priv *priv;
	unsigned int nvec = 1;
	void __iomem *regs;
	int ret;

	ret = pci_enable_device(pdev);
	if (ret)
		return ret;
	ret = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(64));
	if (ret) {
		dev_err(&pdev->dev, "failed to set DMA mask.\n");
		goto err_disable_device;
	}

	ret = pci_request_regions(pdev, TN40_DRV_NAME);
	if (ret) {
		dev_err(&pdev->dev, "failed to request PCI regions.\n");
		goto err_disable_device;
	}

	pci_set_master(pdev);

	regs = pci_iomap(pdev, 0, TN40_REGS_SIZE);
	if (!regs) {
		ret = -EIO;
		dev_err(&pdev->dev, "failed to map PCI bar.\n");
		goto err_free_regions;
	}

	ndev = tn40_netdev_alloc(pdev);
	if (!ndev) {
		ret = -ENOMEM;
		dev_err(&pdev->dev, "failed to allocate netdev.\n");
		goto err_iounmap;
	}

	priv = netdev_priv(ndev);
	pci_set_drvdata(pdev, priv);
	netif_napi_add(ndev, &priv->napi, tn40_poll);

	priv->regs = regs;
	priv->pdev = pdev;
	priv->ndev = ndev;
	/* Initialize fifo sizes. */
	priv->txd_size = 3;
	priv->txf_size = 3;
	priv->rxd_size = 3;
	priv->rxf_size = 3;
	/* Initialize the initial coalescing registers. */
	priv->rdintcm = TN40_INT_REG_VAL(0x20, 1, 4, 12);
	priv->tdintcm = TN40_INT_REG_VAL(0x20, 1, 0, 12);

	ret = tn40_hw_reset(priv);
	if (ret) {
		dev_err(&pdev->dev, "failed to reset HW.\n");
		goto err_unset_drvdata;
	}

	ret = pci_alloc_irq_vectors(pdev, 1, nvec, PCI_IRQ_MSI);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to allocate irq.\n");
		goto err_unset_drvdata;
	}

	ret = tn40_mdiobus_init(priv);
	if (ret) {
		dev_err(&pdev->dev, "failed to initialize mdio bus.\n");
		goto err_free_irq;
	}

	priv->stats_flag =
		((tn40_read_reg(priv, TN40_FPGA_VER) & 0xFFF) != 308);
	u64_stats_init(&priv->syncp);

	priv->isr_mask = TN40_IR_RX_FREE_0 | TN40_IR_LNKCHG0 | TN40_IR_PSE |
		TN40_IR_TMR0 | TN40_IR_RX_DESC_0 | TN40_IR_TX_FREE_0 |
		TN40_IR_TMR1;

	tn40_mac_init(priv);
	ret = tn40_phy_register(priv);
	if (ret) {
		dev_err(&pdev->dev, "failed to set up PHY.\n");
		goto err_cleanup_swnodes;
	}

	ret = tn40_priv_init(priv);
	if (ret) {
		dev_err(&pdev->dev, "failed to initialize tn40_priv.\n");
		goto err_unregister_phydev;
	}

	ret = register_netdev(ndev);
	if (ret) {
		dev_err(&pdev->dev, "failed to register netdev.\n");
		goto err_unregister_phydev;
	}
	return 0;
err_unregister_phydev:
	tn40_phy_unregister(priv);
err_cleanup_swnodes:
	tn40_swnodes_cleanup(priv);
err_free_irq:
	pci_free_irq_vectors(pdev);
err_unset_drvdata:
	pci_set_drvdata(pdev, NULL);
err_iounmap:
	iounmap(regs);
err_free_regions:
	pci_release_regions(pdev);
err_disable_device:
	pci_disable_device(pdev);
	return ret;
}

static void tn40_remove(struct pci_dev *pdev)
{
	struct tn40_priv *priv = pci_get_drvdata(pdev);
	struct net_device *ndev = priv->ndev;

	unregister_netdev(ndev);

	tn40_phy_unregister(priv);
	tn40_swnodes_cleanup(priv);
	pci_free_irq_vectors(priv->pdev);
	pci_set_drvdata(pdev, NULL);
	iounmap(priv->regs);
	pci_release_regions(pdev);
	pci_disable_device(pdev);
}

static const struct pci_device_id tn40_id_table[] = {
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_TEHUTI, 0x4022,
			 PCI_VENDOR_ID_TEHUTI, 0x3015) },
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_TEHUTI, 0x4022,
			 PCI_VENDOR_ID_DLINK, 0x4d00) },
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_TEHUTI, 0x4022,
			 PCI_VENDOR_ID_ASUSTEK, 0x8709) },
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_TEHUTI, 0x4022,
			 PCI_VENDOR_ID_EDIMAX, 0x8103) },
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_TEHUTI, PCI_DEVICE_ID_TEHUTI_TN9510,
			 PCI_VENDOR_ID_TEHUTI, 0x3015) },
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_TEHUTI, PCI_DEVICE_ID_TEHUTI_TN9510,
			 PCI_VENDOR_ID_EDIMAX, 0x8102) },
	{ }
};

static struct pci_driver tn40_driver = {
	.name = TN40_DRV_NAME,
	.id_table = tn40_id_table,
	.probe = tn40_probe,
	.remove = tn40_remove,
};

module_pci_driver(tn40_driver);

MODULE_DEVICE_TABLE(pci, tn40_id_table);
MODULE_LICENSE("GPL");
MODULE_FIRMWARE(TN40_FIRMWARE_NAME);
MODULE_DESCRIPTION("Tehuti Network TN40xx Driver");
