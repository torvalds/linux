// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * Copyright (C) 2020-2021 Intel Corporation
 */
#include <net/tso.h>
#include <linux/tcp.h>

#include "iwl-debug.h"
#include "iwl-io.h"
#include "fw/api/tx.h"
#include "queue/tx.h"
#include "iwl-fh.h"
#include "iwl-scd.h"
#include <linux/dmapool.h>

/*
 * iwl_txq_update_byte_tbl - Set up entry in Tx byte-count array
 */
static void iwl_pcie_gen2_update_byte_tbl(struct iwl_trans *trans,
					  struct iwl_txq *txq, u16 byte_cnt,
					  int num_tbs)
{
	int idx = iwl_txq_get_cmd_index(txq, txq->write_ptr);
	u8 filled_tfd_size, num_fetch_chunks;
	u16 len = byte_cnt;
	__le16 bc_ent;

	if (WARN(idx >= txq->n_window, "%d >= %d\n", idx, txq->n_window))
		return;

	filled_tfd_size = offsetof(struct iwl_tfh_tfd, tbs) +
			  num_tbs * sizeof(struct iwl_tfh_tb);
	/*
	 * filled_tfd_size contains the number of filled bytes in the TFD.
	 * Dividing it by 64 will give the number of chunks to fetch
	 * to SRAM- 0 for one chunk, 1 for 2 and so on.
	 * If, for example, TFD contains only 3 TBs then 32 bytes
	 * of the TFD are used, and only one chunk of 64 bytes should
	 * be fetched
	 */
	num_fetch_chunks = DIV_ROUND_UP(filled_tfd_size, 64) - 1;

	if (trans->trans_cfg->device_family >= IWL_DEVICE_FAMILY_AX210) {
		struct iwl_gen3_bc_tbl_entry *scd_bc_tbl_gen3 = txq->bc_tbl.addr;

		/* Starting from AX210, the HW expects bytes */
		WARN_ON(trans->txqs.bc_table_dword);
		WARN_ON(len > 0x3FFF);
		bc_ent = cpu_to_le16(len | (num_fetch_chunks << 14));
		scd_bc_tbl_gen3[idx].tfd_offset = bc_ent;
	} else {
		struct iwlagn_scd_bc_tbl *scd_bc_tbl = txq->bc_tbl.addr;

		/* Before AX210, the HW expects DW */
		WARN_ON(!trans->txqs.bc_table_dword);
		len = DIV_ROUND_UP(len, 4);
		WARN_ON(len > 0xFFF);
		bc_ent = cpu_to_le16(len | (num_fetch_chunks << 12));
		scd_bc_tbl->tfd_offset[idx] = bc_ent;
	}
}

/*
 * iwl_txq_inc_wr_ptr - Send new write index to hardware
 */
void iwl_txq_inc_wr_ptr(struct iwl_trans *trans, struct iwl_txq *txq)
{
	lockdep_assert_held(&txq->lock);

	IWL_DEBUG_TX(trans, "Q:%d WR: 0x%x\n", txq->id, txq->write_ptr);

	/*
	 * if not in power-save mode, uCode will never sleep when we're
	 * trying to tx (during RFKILL, we're not trying to tx).
	 */
	iwl_write32(trans, HBUS_TARG_WRPTR, txq->write_ptr | (txq->id << 16));
}

static u8 iwl_txq_gen2_get_num_tbs(struct iwl_trans *trans,
				   struct iwl_tfh_tfd *tfd)
{
	return le16_to_cpu(tfd->num_tbs) & 0x1f;
}

void iwl_txq_gen2_tfd_unmap(struct iwl_trans *trans, struct iwl_cmd_meta *meta,
			    struct iwl_tfh_tfd *tfd)
{
	int i, num_tbs;

	/* Sanity check on number of chunks */
	num_tbs = iwl_txq_gen2_get_num_tbs(trans, tfd);

	if (num_tbs > trans->txqs.tfd.max_tbs) {
		IWL_ERR(trans, "Too many chunks: %i\n", num_tbs);
		return;
	}

	/* first TB is never freed - it's the bidirectional DMA data */
	for (i = 1; i < num_tbs; i++) {
		if (meta->tbs & BIT(i))
			dma_unmap_page(trans->dev,
				       le64_to_cpu(tfd->tbs[i].addr),
				       le16_to_cpu(tfd->tbs[i].tb_len),
				       DMA_TO_DEVICE);
		else
			dma_unmap_single(trans->dev,
					 le64_to_cpu(tfd->tbs[i].addr),
					 le16_to_cpu(tfd->tbs[i].tb_len),
					 DMA_TO_DEVICE);
	}

	tfd->num_tbs = 0;
}

void iwl_txq_gen2_free_tfd(struct iwl_trans *trans, struct iwl_txq *txq)
{
	/* rd_ptr is bounded by TFD_QUEUE_SIZE_MAX and
	 * idx is bounded by n_window
	 */
	int idx = iwl_txq_get_cmd_index(txq, txq->read_ptr);
	struct sk_buff *skb;

	lockdep_assert_held(&txq->lock);

	if (!txq->entries)
		return;

	iwl_txq_gen2_tfd_unmap(trans, &txq->entries[idx].meta,
			       iwl_txq_get_tfd(trans, txq, idx));

	skb = txq->entries[idx].skb;

	/* Can be called from irqs-disabled context
	 * If skb is not NULL, it means that the whole queue is being
	 * freed and that the queue is not empty - free the skb
	 */
	if (skb) {
		iwl_op_mode_free_skb(trans->op_mode, skb);
		txq->entries[idx].skb = NULL;
	}
}

int iwl_txq_gen2_set_tb(struct iwl_trans *trans, struct iwl_tfh_tfd *tfd,
			dma_addr_t addr, u16 len)
{
	int idx = iwl_txq_gen2_get_num_tbs(trans, tfd);
	struct iwl_tfh_tb *tb;

	/*
	 * Only WARN here so we know about the issue, but we mess up our
	 * unmap path because not every place currently checks for errors
	 * returned from this function - it can only return an error if
	 * there's no more space, and so when we know there is enough we
	 * don't always check ...
	 */
	WARN(iwl_txq_crosses_4g_boundary(addr, len),
	     "possible DMA problem with iova:0x%llx, len:%d\n",
	     (unsigned long long)addr, len);

	if (WARN_ON(idx >= IWL_TFH_NUM_TBS))
		return -EINVAL;
	tb = &tfd->tbs[idx];

	/* Each TFD can point to a maximum max_tbs Tx buffers */
	if (le16_to_cpu(tfd->num_tbs) >= trans->txqs.tfd.max_tbs) {
		IWL_ERR(trans, "Error can not send more than %d chunks\n",
			trans->txqs.tfd.max_tbs);
		return -EINVAL;
	}

	put_unaligned_le64(addr, &tb->addr);
	tb->tb_len = cpu_to_le16(len);

	tfd->num_tbs = cpu_to_le16(idx + 1);

	return idx;
}

static struct page *get_workaround_page(struct iwl_trans *trans,
					struct sk_buff *skb)
{
	struct page **page_ptr;
	struct page *ret;

	page_ptr = (void *)((u8 *)skb->cb + trans->txqs.page_offs);

	ret = alloc_page(GFP_ATOMIC);
	if (!ret)
		return NULL;

	/* set the chaining pointer to the previous page if there */
	*(void **)((u8 *)page_address(ret) + PAGE_SIZE - sizeof(void *)) = *page_ptr;
	*page_ptr = ret;

	return ret;
}

/*
 * Add a TB and if needed apply the FH HW bug workaround;
 * meta != NULL indicates that it's a page mapping and we
 * need to dma_unmap_page() and set the meta->tbs bit in
 * this case.
 */
static int iwl_txq_gen2_set_tb_with_wa(struct iwl_trans *trans,
				       struct sk_buff *skb,
				       struct iwl_tfh_tfd *tfd,
				       dma_addr_t phys, void *virt,
				       u16 len, struct iwl_cmd_meta *meta)
{
	dma_addr_t oldphys = phys;
	struct page *page;
	int ret;

	if (unlikely(dma_mapping_error(trans->dev, phys)))
		return -ENOMEM;

	if (likely(!iwl_txq_crosses_4g_boundary(phys, len))) {
		ret = iwl_txq_gen2_set_tb(trans, tfd, phys, len);

		if (ret < 0)
			goto unmap;

		if (meta)
			meta->tbs |= BIT(ret);

		ret = 0;
		goto trace;
	}

	/*
	 * Work around a hardware bug. If (as expressed in the
	 * condition above) the TB ends on a 32-bit boundary,
	 * then the next TB may be accessed with the wrong
	 * address.
	 * To work around it, copy the data elsewhere and make
	 * a new mapping for it so the device will not fail.
	 */

	if (WARN_ON(len > PAGE_SIZE - sizeof(void *))) {
		ret = -ENOBUFS;
		goto unmap;
	}

	page = get_workaround_page(trans, skb);
	if (!page) {
		ret = -ENOMEM;
		goto unmap;
	}

	memcpy(page_address(page), virt, len);

	phys = dma_map_single(trans->dev, page_address(page), len,
			      DMA_TO_DEVICE);
	if (unlikely(dma_mapping_error(trans->dev, phys)))
		return -ENOMEM;
	ret = iwl_txq_gen2_set_tb(trans, tfd, phys, len);
	if (ret < 0) {
		/* unmap the new allocation as single */
		oldphys = phys;
		meta = NULL;
		goto unmap;
	}
	IWL_WARN(trans,
		 "TB bug workaround: copied %d bytes from 0x%llx to 0x%llx\n",
		 len, (unsigned long long)oldphys, (unsigned long long)phys);

	ret = 0;
unmap:
	if (meta)
		dma_unmap_page(trans->dev, oldphys, len, DMA_TO_DEVICE);
	else
		dma_unmap_single(trans->dev, oldphys, len, DMA_TO_DEVICE);
trace:
	trace_iwlwifi_dev_tx_tb(trans->dev, skb, virt, phys, len);

	return ret;
}

#ifdef CONFIG_INET
struct iwl_tso_hdr_page *get_page_hdr(struct iwl_trans *trans, size_t len,
				      struct sk_buff *skb)
{
	struct iwl_tso_hdr_page *p = this_cpu_ptr(trans->txqs.tso_hdr_page);
	struct page **page_ptr;

	page_ptr = (void *)((u8 *)skb->cb + trans->txqs.page_offs);

	if (WARN_ON(*page_ptr))
		return NULL;

	if (!p->page)
		goto alloc;

	/*
	 * Check if there's enough room on this page
	 *
	 * Note that we put a page chaining pointer *last* in the
	 * page - we need it somewhere, and if it's there then we
	 * avoid DMA mapping the last bits of the page which may
	 * trigger the 32-bit boundary hardware bug.
	 *
	 * (see also get_workaround_page() in tx-gen2.c)
	 */
	if (p->pos + len < (u8 *)page_address(p->page) + PAGE_SIZE -
			   sizeof(void *))
		goto out;

	/* We don't have enough room on this page, get a new one. */
	__free_page(p->page);

alloc:
	p->page = alloc_page(GFP_ATOMIC);
	if (!p->page)
		return NULL;
	p->pos = page_address(p->page);
	/* set the chaining pointer to NULL */
	*(void **)((u8 *)page_address(p->page) + PAGE_SIZE - sizeof(void *)) = NULL;
out:
	*page_ptr = p->page;
	get_page(p->page);
	return p;
}
#endif

static int iwl_txq_gen2_build_amsdu(struct iwl_trans *trans,
				    struct sk_buff *skb,
				    struct iwl_tfh_tfd *tfd, int start_len,
				    u8 hdr_len,
				    struct iwl_device_tx_cmd *dev_cmd)
{
#ifdef CONFIG_INET
	struct iwl_tx_cmd_gen2 *tx_cmd = (void *)dev_cmd->payload;
	struct ieee80211_hdr *hdr = (void *)skb->data;
	unsigned int snap_ip_tcp_hdrlen, ip_hdrlen, total_len, hdr_room;
	unsigned int mss = skb_shinfo(skb)->gso_size;
	u16 length, amsdu_pad;
	u8 *start_hdr;
	struct iwl_tso_hdr_page *hdr_page;
	struct tso_t tso;

	trace_iwlwifi_dev_tx(trans->dev, skb, tfd, sizeof(*tfd),
			     &dev_cmd->hdr, start_len, 0);

	ip_hdrlen = skb_transport_header(skb) - skb_network_header(skb);
	snap_ip_tcp_hdrlen = 8 + ip_hdrlen + tcp_hdrlen(skb);
	total_len = skb->len - snap_ip_tcp_hdrlen - hdr_len;
	amsdu_pad = 0;

	/* total amount of header we may need for this A-MSDU */
	hdr_room = DIV_ROUND_UP(total_len, mss) *
		(3 + snap_ip_tcp_hdrlen + sizeof(struct ethhdr));

	/* Our device supports 9 segments at most, it will fit in 1 page */
	hdr_page = get_page_hdr(trans, hdr_room, skb);
	if (!hdr_page)
		return -ENOMEM;

	start_hdr = hdr_page->pos;

	/*
	 * Pull the ieee80211 header to be able to use TSO core,
	 * we will restore it for the tx_status flow.
	 */
	skb_pull(skb, hdr_len);

	/*
	 * Remove the length of all the headers that we don't actually
	 * have in the MPDU by themselves, but that we duplicate into
	 * all the different MSDUs inside the A-MSDU.
	 */
	le16_add_cpu(&tx_cmd->len, -snap_ip_tcp_hdrlen);

	tso_start(skb, &tso);

	while (total_len) {
		/* this is the data left for this subframe */
		unsigned int data_left = min_t(unsigned int, mss, total_len);
		unsigned int tb_len;
		dma_addr_t tb_phys;
		u8 *subf_hdrs_start = hdr_page->pos;

		total_len -= data_left;

		memset(hdr_page->pos, 0, amsdu_pad);
		hdr_page->pos += amsdu_pad;
		amsdu_pad = (4 - (sizeof(struct ethhdr) + snap_ip_tcp_hdrlen +
				  data_left)) & 0x3;
		ether_addr_copy(hdr_page->pos, ieee80211_get_DA(hdr));
		hdr_page->pos += ETH_ALEN;
		ether_addr_copy(hdr_page->pos, ieee80211_get_SA(hdr));
		hdr_page->pos += ETH_ALEN;

		length = snap_ip_tcp_hdrlen + data_left;
		*((__be16 *)hdr_page->pos) = cpu_to_be16(length);
		hdr_page->pos += sizeof(length);

		/*
		 * This will copy the SNAP as well which will be considered
		 * as MAC header.
		 */
		tso_build_hdr(skb, hdr_page->pos, &tso, data_left, !total_len);

		hdr_page->pos += snap_ip_tcp_hdrlen;

		tb_len = hdr_page->pos - start_hdr;
		tb_phys = dma_map_single(trans->dev, start_hdr,
					 tb_len, DMA_TO_DEVICE);
		if (unlikely(dma_mapping_error(trans->dev, tb_phys)))
			goto out_err;
		/*
		 * No need for _with_wa, this is from the TSO page and
		 * we leave some space at the end of it so can't hit
		 * the buggy scenario.
		 */
		iwl_txq_gen2_set_tb(trans, tfd, tb_phys, tb_len);
		trace_iwlwifi_dev_tx_tb(trans->dev, skb, start_hdr,
					tb_phys, tb_len);
		/* add this subframe's headers' length to the tx_cmd */
		le16_add_cpu(&tx_cmd->len, hdr_page->pos - subf_hdrs_start);

		/* prepare the start_hdr for the next subframe */
		start_hdr = hdr_page->pos;

		/* put the payload */
		while (data_left) {
			int ret;

			tb_len = min_t(unsigned int, tso.size, data_left);
			tb_phys = dma_map_single(trans->dev, tso.data,
						 tb_len, DMA_TO_DEVICE);
			ret = iwl_txq_gen2_set_tb_with_wa(trans, skb, tfd,
							  tb_phys, tso.data,
							  tb_len, NULL);
			if (ret)
				goto out_err;

			data_left -= tb_len;
			tso_build_data(skb, &tso, tb_len);
		}
	}

	/* re -add the WiFi header */
	skb_push(skb, hdr_len);

	return 0;

out_err:
#endif
	return -EINVAL;
}

static struct
iwl_tfh_tfd *iwl_txq_gen2_build_tx_amsdu(struct iwl_trans *trans,
					 struct iwl_txq *txq,
					 struct iwl_device_tx_cmd *dev_cmd,
					 struct sk_buff *skb,
					 struct iwl_cmd_meta *out_meta,
					 int hdr_len,
					 int tx_cmd_len)
{
	int idx = iwl_txq_get_cmd_index(txq, txq->write_ptr);
	struct iwl_tfh_tfd *tfd = iwl_txq_get_tfd(trans, txq, idx);
	dma_addr_t tb_phys;
	int len;
	void *tb1_addr;

	tb_phys = iwl_txq_get_first_tb_dma(txq, idx);

	/*
	 * No need for _with_wa, the first TB allocation is aligned up
	 * to a 64-byte boundary and thus can't be at the end or cross
	 * a page boundary (much less a 2^32 boundary).
	 */
	iwl_txq_gen2_set_tb(trans, tfd, tb_phys, IWL_FIRST_TB_SIZE);

	/*
	 * The second TB (tb1) points to the remainder of the TX command
	 * and the 802.11 header - dword aligned size
	 * (This calculation modifies the TX command, so do it before the
	 * setup of the first TB)
	 */
	len = tx_cmd_len + sizeof(struct iwl_cmd_header) + hdr_len -
	      IWL_FIRST_TB_SIZE;

	/* do not align A-MSDU to dword as the subframe header aligns it */

	/* map the data for TB1 */
	tb1_addr = ((u8 *)&dev_cmd->hdr) + IWL_FIRST_TB_SIZE;
	tb_phys = dma_map_single(trans->dev, tb1_addr, len, DMA_TO_DEVICE);
	if (unlikely(dma_mapping_error(trans->dev, tb_phys)))
		goto out_err;
	/*
	 * No need for _with_wa(), we ensure (via alignment) that the data
	 * here can never cross or end at a page boundary.
	 */
	iwl_txq_gen2_set_tb(trans, tfd, tb_phys, len);

	if (iwl_txq_gen2_build_amsdu(trans, skb, tfd, len + IWL_FIRST_TB_SIZE,
				     hdr_len, dev_cmd))
		goto out_err;

	/* building the A-MSDU might have changed this data, memcpy it now */
	memcpy(&txq->first_tb_bufs[idx], dev_cmd, IWL_FIRST_TB_SIZE);
	return tfd;

out_err:
	iwl_txq_gen2_tfd_unmap(trans, out_meta, tfd);
	return NULL;
}

static int iwl_txq_gen2_tx_add_frags(struct iwl_trans *trans,
				     struct sk_buff *skb,
				     struct iwl_tfh_tfd *tfd,
				     struct iwl_cmd_meta *out_meta)
{
	int i;

	for (i = 0; i < skb_shinfo(skb)->nr_frags; i++) {
		const skb_frag_t *frag = &skb_shinfo(skb)->frags[i];
		dma_addr_t tb_phys;
		unsigned int fragsz = skb_frag_size(frag);
		int ret;

		if (!fragsz)
			continue;

		tb_phys = skb_frag_dma_map(trans->dev, frag, 0,
					   fragsz, DMA_TO_DEVICE);
		ret = iwl_txq_gen2_set_tb_with_wa(trans, skb, tfd, tb_phys,
						  skb_frag_address(frag),
						  fragsz, out_meta);
		if (ret)
			return ret;
	}

	return 0;
}

static struct
iwl_tfh_tfd *iwl_txq_gen2_build_tx(struct iwl_trans *trans,
				   struct iwl_txq *txq,
				   struct iwl_device_tx_cmd *dev_cmd,
				   struct sk_buff *skb,
				   struct iwl_cmd_meta *out_meta,
				   int hdr_len,
				   int tx_cmd_len,
				   bool pad)
{
	int idx = iwl_txq_get_cmd_index(txq, txq->write_ptr);
	struct iwl_tfh_tfd *tfd = iwl_txq_get_tfd(trans, txq, idx);
	dma_addr_t tb_phys;
	int len, tb1_len, tb2_len;
	void *tb1_addr;
	struct sk_buff *frag;

	tb_phys = iwl_txq_get_first_tb_dma(txq, idx);

	/* The first TB points to bi-directional DMA data */
	memcpy(&txq->first_tb_bufs[idx], dev_cmd, IWL_FIRST_TB_SIZE);

	/*
	 * No need for _with_wa, the first TB allocation is aligned up
	 * to a 64-byte boundary and thus can't be at the end or cross
	 * a page boundary (much less a 2^32 boundary).
	 */
	iwl_txq_gen2_set_tb(trans, tfd, tb_phys, IWL_FIRST_TB_SIZE);

	/*
	 * The second TB (tb1) points to the remainder of the TX command
	 * and the 802.11 header - dword aligned size
	 * (This calculation modifies the TX command, so do it before the
	 * setup of the first TB)
	 */
	len = tx_cmd_len + sizeof(struct iwl_cmd_header) + hdr_len -
	      IWL_FIRST_TB_SIZE;

	if (pad)
		tb1_len = ALIGN(len, 4);
	else
		tb1_len = len;

	/* map the data for TB1 */
	tb1_addr = ((u8 *)&dev_cmd->hdr) + IWL_FIRST_TB_SIZE;
	tb_phys = dma_map_single(trans->dev, tb1_addr, tb1_len, DMA_TO_DEVICE);
	if (unlikely(dma_mapping_error(trans->dev, tb_phys)))
		goto out_err;
	/*
	 * No need for _with_wa(), we ensure (via alignment) that the data
	 * here can never cross or end at a page boundary.
	 */
	iwl_txq_gen2_set_tb(trans, tfd, tb_phys, tb1_len);
	trace_iwlwifi_dev_tx(trans->dev, skb, tfd, sizeof(*tfd), &dev_cmd->hdr,
			     IWL_FIRST_TB_SIZE + tb1_len, hdr_len);

	/* set up TFD's third entry to point to remainder of skb's head */
	tb2_len = skb_headlen(skb) - hdr_len;

	if (tb2_len > 0) {
		int ret;

		tb_phys = dma_map_single(trans->dev, skb->data + hdr_len,
					 tb2_len, DMA_TO_DEVICE);
		ret = iwl_txq_gen2_set_tb_with_wa(trans, skb, tfd, tb_phys,
						  skb->data + hdr_len, tb2_len,
						  NULL);
		if (ret)
			goto out_err;
	}

	if (iwl_txq_gen2_tx_add_frags(trans, skb, tfd, out_meta))
		goto out_err;

	skb_walk_frags(skb, frag) {
		int ret;

		tb_phys = dma_map_single(trans->dev, frag->data,
					 skb_headlen(frag), DMA_TO_DEVICE);
		ret = iwl_txq_gen2_set_tb_with_wa(trans, skb, tfd, tb_phys,
						  frag->data,
						  skb_headlen(frag), NULL);
		if (ret)
			goto out_err;
		if (iwl_txq_gen2_tx_add_frags(trans, frag, tfd, out_meta))
			goto out_err;
	}

	return tfd;

out_err:
	iwl_txq_gen2_tfd_unmap(trans, out_meta, tfd);
	return NULL;
}

static
struct iwl_tfh_tfd *iwl_txq_gen2_build_tfd(struct iwl_trans *trans,
					   struct iwl_txq *txq,
					   struct iwl_device_tx_cmd *dev_cmd,
					   struct sk_buff *skb,
					   struct iwl_cmd_meta *out_meta)
{
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;
	int idx = iwl_txq_get_cmd_index(txq, txq->write_ptr);
	struct iwl_tfh_tfd *tfd = iwl_txq_get_tfd(trans, txq, idx);
	int len, hdr_len;
	bool amsdu;

	/* There must be data left over for TB1 or this code must be changed */
	BUILD_BUG_ON(sizeof(struct iwl_tx_cmd_gen2) < IWL_FIRST_TB_SIZE);

	memset(tfd, 0, sizeof(*tfd));

	if (trans->trans_cfg->device_family < IWL_DEVICE_FAMILY_AX210)
		len = sizeof(struct iwl_tx_cmd_gen2);
	else
		len = sizeof(struct iwl_tx_cmd_gen3);

	amsdu = ieee80211_is_data_qos(hdr->frame_control) &&
			(*ieee80211_get_qos_ctl(hdr) &
			 IEEE80211_QOS_CTL_A_MSDU_PRESENT);

	hdr_len = ieee80211_hdrlen(hdr->frame_control);

	/*
	 * Only build A-MSDUs here if doing so by GSO, otherwise it may be
	 * an A-MSDU for other reasons, e.g. NAN or an A-MSDU having been
	 * built in the higher layers already.
	 */
	if (amsdu && skb_shinfo(skb)->gso_size)
		return iwl_txq_gen2_build_tx_amsdu(trans, txq, dev_cmd, skb,
						    out_meta, hdr_len, len);
	return iwl_txq_gen2_build_tx(trans, txq, dev_cmd, skb, out_meta,
				      hdr_len, len, !amsdu);
}

int iwl_txq_space(struct iwl_trans *trans, const struct iwl_txq *q)
{
	unsigned int max;
	unsigned int used;

	/*
	 * To avoid ambiguity between empty and completely full queues, there
	 * should always be less than max_tfd_queue_size elements in the queue.
	 * If q->n_window is smaller than max_tfd_queue_size, there is no need
	 * to reserve any queue entries for this purpose.
	 */
	if (q->n_window < trans->trans_cfg->base_params->max_tfd_queue_size)
		max = q->n_window;
	else
		max = trans->trans_cfg->base_params->max_tfd_queue_size - 1;

	/*
	 * max_tfd_queue_size is a power of 2, so the following is equivalent to
	 * modulo by max_tfd_queue_size and is well defined.
	 */
	used = (q->write_ptr - q->read_ptr) &
		(trans->trans_cfg->base_params->max_tfd_queue_size - 1);

	if (WARN_ON(used > max))
		return 0;

	return max - used;
}

int iwl_txq_gen2_tx(struct iwl_trans *trans, struct sk_buff *skb,
		    struct iwl_device_tx_cmd *dev_cmd, int txq_id)
{
	struct iwl_cmd_meta *out_meta;
	struct iwl_txq *txq = trans->txqs.txq[txq_id];
	u16 cmd_len;
	int idx;
	void *tfd;

	if (WARN_ONCE(txq_id >= IWL_MAX_TVQM_QUEUES,
		      "queue %d out of range", txq_id))
		return -EINVAL;

	if (WARN_ONCE(!test_bit(txq_id, trans->txqs.queue_used),
		      "TX on unused queue %d\n", txq_id))
		return -EINVAL;

	if (skb_is_nonlinear(skb) &&
	    skb_shinfo(skb)->nr_frags > IWL_TRANS_MAX_FRAGS(trans) &&
	    __skb_linearize(skb))
		return -ENOMEM;

	spin_lock(&txq->lock);

	if (iwl_txq_space(trans, txq) < txq->high_mark) {
		iwl_txq_stop(trans, txq);

		/* don't put the packet on the ring, if there is no room */
		if (unlikely(iwl_txq_space(trans, txq) < 3)) {
			struct iwl_device_tx_cmd **dev_cmd_ptr;

			dev_cmd_ptr = (void *)((u8 *)skb->cb +
					       trans->txqs.dev_cmd_offs);

			*dev_cmd_ptr = dev_cmd;
			__skb_queue_tail(&txq->overflow_q, skb);
			spin_unlock(&txq->lock);
			return 0;
		}
	}

	idx = iwl_txq_get_cmd_index(txq, txq->write_ptr);

	/* Set up driver data for this TFD */
	txq->entries[idx].skb = skb;
	txq->entries[idx].cmd = dev_cmd;

	dev_cmd->hdr.sequence =
		cpu_to_le16((u16)(QUEUE_TO_SEQ(txq_id) |
			    INDEX_TO_SEQ(idx)));

	/* Set up first empty entry in queue's array of Tx/cmd buffers */
	out_meta = &txq->entries[idx].meta;
	out_meta->flags = 0;

	tfd = iwl_txq_gen2_build_tfd(trans, txq, dev_cmd, skb, out_meta);
	if (!tfd) {
		spin_unlock(&txq->lock);
		return -1;
	}

	if (trans->trans_cfg->device_family >= IWL_DEVICE_FAMILY_AX210) {
		struct iwl_tx_cmd_gen3 *tx_cmd_gen3 =
			(void *)dev_cmd->payload;

		cmd_len = le16_to_cpu(tx_cmd_gen3->len);
	} else {
		struct iwl_tx_cmd_gen2 *tx_cmd_gen2 =
			(void *)dev_cmd->payload;

		cmd_len = le16_to_cpu(tx_cmd_gen2->len);
	}

	/* Set up entry for this TFD in Tx byte-count array */
	iwl_pcie_gen2_update_byte_tbl(trans, txq, cmd_len,
				      iwl_txq_gen2_get_num_tbs(trans, tfd));

	/* start timer if queue currently empty */
	if (txq->read_ptr == txq->write_ptr && txq->wd_timeout)
		mod_timer(&txq->stuck_timer, jiffies + txq->wd_timeout);

	/* Tell device the write index *just past* this latest filled TFD */
	txq->write_ptr = iwl_txq_inc_wrap(trans, txq->write_ptr);
	iwl_txq_inc_wr_ptr(trans, txq);
	/*
	 * At this point the frame is "transmitted" successfully
	 * and we will get a TX status notification eventually.
	 */
	spin_unlock(&txq->lock);
	return 0;
}

/*************** HOST COMMAND QUEUE FUNCTIONS   *****/

/*
 * iwl_txq_gen2_unmap -  Unmap any remaining DMA mappings and free skb's
 */
void iwl_txq_gen2_unmap(struct iwl_trans *trans, int txq_id)
{
	struct iwl_txq *txq = trans->txqs.txq[txq_id];

	spin_lock_bh(&txq->lock);
	while (txq->write_ptr != txq->read_ptr) {
		IWL_DEBUG_TX_REPLY(trans, "Q %d Free %d\n",
				   txq_id, txq->read_ptr);

		if (txq_id != trans->txqs.cmd.q_id) {
			int idx = iwl_txq_get_cmd_index(txq, txq->read_ptr);
			struct sk_buff *skb = txq->entries[idx].skb;

			if (!WARN_ON_ONCE(!skb))
				iwl_txq_free_tso_page(trans, skb);
		}
		iwl_txq_gen2_free_tfd(trans, txq);
		txq->read_ptr = iwl_txq_inc_wrap(trans, txq->read_ptr);
	}

	while (!skb_queue_empty(&txq->overflow_q)) {
		struct sk_buff *skb = __skb_dequeue(&txq->overflow_q);

		iwl_op_mode_free_skb(trans->op_mode, skb);
	}

	spin_unlock_bh(&txq->lock);

	/* just in case - this queue may have been stopped */
	iwl_wake_queue(trans, txq);
}

static void iwl_txq_gen2_free_memory(struct iwl_trans *trans,
				     struct iwl_txq *txq)
{
	struct device *dev = trans->dev;

	/* De-alloc circular buffer of TFDs */
	if (txq->tfds) {
		dma_free_coherent(dev,
				  trans->txqs.tfd.size * txq->n_window,
				  txq->tfds, txq->dma_addr);
		dma_free_coherent(dev,
				  sizeof(*txq->first_tb_bufs) * txq->n_window,
				  txq->first_tb_bufs, txq->first_tb_dma);
	}

	kfree(txq->entries);
	if (txq->bc_tbl.addr)
		dma_pool_free(trans->txqs.bc_pool,
			      txq->bc_tbl.addr, txq->bc_tbl.dma);
	kfree(txq);
}

/*
 * iwl_pcie_txq_free - Deallocate DMA queue.
 * @txq: Transmit queue to deallocate.
 *
 * Empty queue by removing and destroying all BD's.
 * Free all buffers.
 * 0-fill, but do not free "txq" descriptor structure.
 */
static void iwl_txq_gen2_free(struct iwl_trans *trans, int txq_id)
{
	struct iwl_txq *txq;
	int i;

	if (WARN_ONCE(txq_id >= IWL_MAX_TVQM_QUEUES,
		      "queue %d out of range", txq_id))
		return;

	txq = trans->txqs.txq[txq_id];

	if (WARN_ON(!txq))
		return;

	iwl_txq_gen2_unmap(trans, txq_id);

	/* De-alloc array of command/tx buffers */
	if (txq_id == trans->txqs.cmd.q_id)
		for (i = 0; i < txq->n_window; i++) {
			kfree_sensitive(txq->entries[i].cmd);
			kfree_sensitive(txq->entries[i].free_buf);
		}
	del_timer_sync(&txq->stuck_timer);

	iwl_txq_gen2_free_memory(trans, txq);

	trans->txqs.txq[txq_id] = NULL;

	clear_bit(txq_id, trans->txqs.queue_used);
}

/*
 * iwl_queue_init - Initialize queue's high/low-water and read/write indexes
 */
static int iwl_queue_init(struct iwl_txq *q, int slots_num)
{
	q->n_window = slots_num;

	/* slots_num must be power-of-two size, otherwise
	 * iwl_txq_get_cmd_index is broken. */
	if (WARN_ON(!is_power_of_2(slots_num)))
		return -EINVAL;

	q->low_mark = q->n_window / 4;
	if (q->low_mark < 4)
		q->low_mark = 4;

	q->high_mark = q->n_window / 8;
	if (q->high_mark < 2)
		q->high_mark = 2;

	q->write_ptr = 0;
	q->read_ptr = 0;

	return 0;
}

int iwl_txq_init(struct iwl_trans *trans, struct iwl_txq *txq, int slots_num,
		 bool cmd_queue)
{
	int ret;
	u32 tfd_queue_max_size =
		trans->trans_cfg->base_params->max_tfd_queue_size;

	txq->need_update = false;

	/* max_tfd_queue_size must be power-of-two size, otherwise
	 * iwl_txq_inc_wrap and iwl_txq_dec_wrap are broken. */
	if (WARN_ONCE(tfd_queue_max_size & (tfd_queue_max_size - 1),
		      "Max tfd queue size must be a power of two, but is %d",
		      tfd_queue_max_size))
		return -EINVAL;

	/* Initialize queue's high/low-water marks, and head/tail indexes */
	ret = iwl_queue_init(txq, slots_num);
	if (ret)
		return ret;

	spin_lock_init(&txq->lock);

	if (cmd_queue) {
		static struct lock_class_key iwl_txq_cmd_queue_lock_class;

		lockdep_set_class(&txq->lock, &iwl_txq_cmd_queue_lock_class);
	}

	__skb_queue_head_init(&txq->overflow_q);

	return 0;
}

void iwl_txq_free_tso_page(struct iwl_trans *trans, struct sk_buff *skb)
{
	struct page **page_ptr;
	struct page *next;

	page_ptr = (void *)((u8 *)skb->cb + trans->txqs.page_offs);
	next = *page_ptr;
	*page_ptr = NULL;

	while (next) {
		struct page *tmp = next;

		next = *(void **)((u8 *)page_address(next) + PAGE_SIZE -
				  sizeof(void *));
		__free_page(tmp);
	}
}

void iwl_txq_log_scd_error(struct iwl_trans *trans, struct iwl_txq *txq)
{
	u32 txq_id = txq->id;
	u32 status;
	bool active;
	u8 fifo;

	if (trans->trans_cfg->use_tfh) {
		IWL_ERR(trans, "Queue %d is stuck %d %d\n", txq_id,
			txq->read_ptr, txq->write_ptr);
		/* TODO: access new SCD registers and dump them */
		return;
	}

	status = iwl_read_prph(trans, SCD_QUEUE_STATUS_BITS(txq_id));
	fifo = (status >> SCD_QUEUE_STTS_REG_POS_TXF) & 0x7;
	active = !!(status & BIT(SCD_QUEUE_STTS_REG_POS_ACTIVE));

	IWL_ERR(trans,
		"Queue %d is %sactive on fifo %d and stuck for %u ms. SW [%d, %d] HW [%d, %d] FH TRB=0x0%x\n",
		txq_id, active ? "" : "in", fifo,
		jiffies_to_msecs(txq->wd_timeout),
		txq->read_ptr, txq->write_ptr,
		iwl_read_prph(trans, SCD_QUEUE_RDPTR(txq_id)) &
			(trans->trans_cfg->base_params->max_tfd_queue_size - 1),
			iwl_read_prph(trans, SCD_QUEUE_WRPTR(txq_id)) &
			(trans->trans_cfg->base_params->max_tfd_queue_size - 1),
			iwl_read_direct32(trans, FH_TX_TRB_REG(fifo)));
}

static void iwl_txq_stuck_timer(struct timer_list *t)
{
	struct iwl_txq *txq = from_timer(txq, t, stuck_timer);
	struct iwl_trans *trans = txq->trans;

	spin_lock(&txq->lock);
	/* check if triggered erroneously */
	if (txq->read_ptr == txq->write_ptr) {
		spin_unlock(&txq->lock);
		return;
	}
	spin_unlock(&txq->lock);

	iwl_txq_log_scd_error(trans, txq);

	iwl_force_nmi(trans);
}

int iwl_txq_alloc(struct iwl_trans *trans, struct iwl_txq *txq, int slots_num,
		  bool cmd_queue)
{
	size_t tfd_sz = trans->txqs.tfd.size *
		trans->trans_cfg->base_params->max_tfd_queue_size;
	size_t tb0_buf_sz;
	int i;

	if (WARN_ON(txq->entries || txq->tfds))
		return -EINVAL;

	if (trans->trans_cfg->use_tfh)
		tfd_sz = trans->txqs.tfd.size * slots_num;

	timer_setup(&txq->stuck_timer, iwl_txq_stuck_timer, 0);
	txq->trans = trans;

	txq->n_window = slots_num;

	txq->entries = kcalloc(slots_num,
			       sizeof(struct iwl_pcie_txq_entry),
			       GFP_KERNEL);

	if (!txq->entries)
		goto error;

	if (cmd_queue)
		for (i = 0; i < slots_num; i++) {
			txq->entries[i].cmd =
				kmalloc(sizeof(struct iwl_device_cmd),
					GFP_KERNEL);
			if (!txq->entries[i].cmd)
				goto error;
		}

	/* Circular buffer of transmit frame descriptors (TFDs),
	 * shared with device */
	txq->tfds = dma_alloc_coherent(trans->dev, tfd_sz,
				       &txq->dma_addr, GFP_KERNEL);
	if (!txq->tfds)
		goto error;

	BUILD_BUG_ON(sizeof(*txq->first_tb_bufs) != IWL_FIRST_TB_SIZE_ALIGN);

	tb0_buf_sz = sizeof(*txq->first_tb_bufs) * slots_num;

	txq->first_tb_bufs = dma_alloc_coherent(trans->dev, tb0_buf_sz,
						&txq->first_tb_dma,
						GFP_KERNEL);
	if (!txq->first_tb_bufs)
		goto err_free_tfds;

	return 0;
err_free_tfds:
	dma_free_coherent(trans->dev, tfd_sz, txq->tfds, txq->dma_addr);
	txq->tfds = NULL;
error:
	if (txq->entries && cmd_queue)
		for (i = 0; i < slots_num; i++)
			kfree(txq->entries[i].cmd);
	kfree(txq->entries);
	txq->entries = NULL;

	return -ENOMEM;
}

static int iwl_txq_dyn_alloc_dma(struct iwl_trans *trans,
				 struct iwl_txq **intxq, int size,
				 unsigned int timeout)
{
	size_t bc_tbl_size, bc_tbl_entries;
	struct iwl_txq *txq;
	int ret;

	WARN_ON(!trans->txqs.bc_tbl_size);

	bc_tbl_size = trans->txqs.bc_tbl_size;
	bc_tbl_entries = bc_tbl_size / sizeof(u16);

	if (WARN_ON(size > bc_tbl_entries))
		return -EINVAL;

	txq = kzalloc(sizeof(*txq), GFP_KERNEL);
	if (!txq)
		return -ENOMEM;

	txq->bc_tbl.addr = dma_pool_alloc(trans->txqs.bc_pool, GFP_KERNEL,
					  &txq->bc_tbl.dma);
	if (!txq->bc_tbl.addr) {
		IWL_ERR(trans, "Scheduler BC Table allocation failed\n");
		kfree(txq);
		return -ENOMEM;
	}

	ret = iwl_txq_alloc(trans, txq, size, false);
	if (ret) {
		IWL_ERR(trans, "Tx queue alloc failed\n");
		goto error;
	}
	ret = iwl_txq_init(trans, txq, size, false);
	if (ret) {
		IWL_ERR(trans, "Tx queue init failed\n");
		goto error;
	}

	txq->wd_timeout = msecs_to_jiffies(timeout);

	*intxq = txq;
	return 0;

error:
	iwl_txq_gen2_free_memory(trans, txq);
	return ret;
}

static int iwl_txq_alloc_response(struct iwl_trans *trans, struct iwl_txq *txq,
				  struct iwl_host_cmd *hcmd)
{
	struct iwl_tx_queue_cfg_rsp *rsp;
	int ret, qid;
	u32 wr_ptr;

	if (WARN_ON(iwl_rx_packet_payload_len(hcmd->resp_pkt) !=
		    sizeof(*rsp))) {
		ret = -EINVAL;
		goto error_free_resp;
	}

	rsp = (void *)hcmd->resp_pkt->data;
	qid = le16_to_cpu(rsp->queue_number);
	wr_ptr = le16_to_cpu(rsp->write_pointer);

	if (qid >= ARRAY_SIZE(trans->txqs.txq)) {
		WARN_ONCE(1, "queue index %d unsupported", qid);
		ret = -EIO;
		goto error_free_resp;
	}

	if (test_and_set_bit(qid, trans->txqs.queue_used)) {
		WARN_ONCE(1, "queue %d already used", qid);
		ret = -EIO;
		goto error_free_resp;
	}

	if (WARN_ONCE(trans->txqs.txq[qid],
		      "queue %d already allocated\n", qid)) {
		ret = -EIO;
		goto error_free_resp;
	}

	txq->id = qid;
	trans->txqs.txq[qid] = txq;
	wr_ptr &= (trans->trans_cfg->base_params->max_tfd_queue_size - 1);

	/* Place first TFD at index corresponding to start sequence number */
	txq->read_ptr = wr_ptr;
	txq->write_ptr = wr_ptr;

	IWL_DEBUG_TX_QUEUES(trans, "Activate queue %d\n", qid);

	iwl_free_resp(hcmd);
	return qid;

error_free_resp:
	iwl_free_resp(hcmd);
	iwl_txq_gen2_free_memory(trans, txq);
	return ret;
}

int iwl_txq_dyn_alloc(struct iwl_trans *trans, __le16 flags, u8 sta_id, u8 tid,
		      int cmd_id, int size, unsigned int timeout)
{
	struct iwl_txq *txq = NULL;
	struct iwl_tx_queue_cfg_cmd cmd = {
		.flags = flags,
		.sta_id = sta_id,
		.tid = tid,
	};
	struct iwl_host_cmd hcmd = {
		.id = cmd_id,
		.len = { sizeof(cmd) },
		.data = { &cmd, },
		.flags = CMD_WANT_SKB,
	};
	int ret;

	ret = iwl_txq_dyn_alloc_dma(trans, &txq, size, timeout);
	if (ret)
		return ret;

	cmd.tfdq_addr = cpu_to_le64(txq->dma_addr);
	cmd.byte_cnt_addr = cpu_to_le64(txq->bc_tbl.dma);
	cmd.cb_size = cpu_to_le32(TFD_QUEUE_CB_SIZE(size));

	ret = iwl_trans_send_cmd(trans, &hcmd);
	if (ret)
		goto error;

	return iwl_txq_alloc_response(trans, txq, &hcmd);

error:
	iwl_txq_gen2_free_memory(trans, txq);
	return ret;
}

void iwl_txq_dyn_free(struct iwl_trans *trans, int queue)
{
	if (WARN(queue >= IWL_MAX_TVQM_QUEUES,
		 "queue %d out of range", queue))
		return;

	/*
	 * Upon HW Rfkill - we stop the device, and then stop the queues
	 * in the op_mode. Just for the sake of the simplicity of the op_mode,
	 * allow the op_mode to call txq_disable after it already called
	 * stop_device.
	 */
	if (!test_and_clear_bit(queue, trans->txqs.queue_used)) {
		WARN_ONCE(test_bit(STATUS_DEVICE_ENABLED, &trans->status),
			  "queue %d not used", queue);
		return;
	}

	iwl_txq_gen2_free(trans, queue);

	IWL_DEBUG_TX_QUEUES(trans, "Deactivate queue %d\n", queue);
}

void iwl_txq_gen2_tx_free(struct iwl_trans *trans)
{
	int i;

	memset(trans->txqs.queue_used, 0, sizeof(trans->txqs.queue_used));

	/* Free all TX queues */
	for (i = 0; i < ARRAY_SIZE(trans->txqs.txq); i++) {
		if (!trans->txqs.txq[i])
			continue;

		iwl_txq_gen2_free(trans, i);
	}
}

int iwl_txq_gen2_init(struct iwl_trans *trans, int txq_id, int queue_size)
{
	struct iwl_txq *queue;
	int ret;

	/* alloc and init the tx queue */
	if (!trans->txqs.txq[txq_id]) {
		queue = kzalloc(sizeof(*queue), GFP_KERNEL);
		if (!queue) {
			IWL_ERR(trans, "Not enough memory for tx queue\n");
			return -ENOMEM;
		}
		trans->txqs.txq[txq_id] = queue;
		ret = iwl_txq_alloc(trans, queue, queue_size, true);
		if (ret) {
			IWL_ERR(trans, "Tx %d queue init failed\n", txq_id);
			goto error;
		}
	} else {
		queue = trans->txqs.txq[txq_id];
	}

	ret = iwl_txq_init(trans, queue, queue_size,
			   (txq_id == trans->txqs.cmd.q_id));
	if (ret) {
		IWL_ERR(trans, "Tx %d queue alloc failed\n", txq_id);
		goto error;
	}
	trans->txqs.txq[txq_id]->id = txq_id;
	set_bit(txq_id, trans->txqs.queue_used);

	return 0;

error:
	iwl_txq_gen2_tx_free(trans);
	return ret;
}

static inline dma_addr_t iwl_txq_gen1_tfd_tb_get_addr(struct iwl_trans *trans,
						      void *_tfd, u8 idx)
{
	struct iwl_tfd *tfd;
	struct iwl_tfd_tb *tb;
	dma_addr_t addr;
	dma_addr_t hi_len;

	if (trans->trans_cfg->use_tfh) {
		struct iwl_tfh_tfd *tfh_tfd = _tfd;
		struct iwl_tfh_tb *tfh_tb = &tfh_tfd->tbs[idx];

		return (dma_addr_t)(le64_to_cpu(tfh_tb->addr));
	}

	tfd = _tfd;
	tb = &tfd->tbs[idx];
	addr = get_unaligned_le32(&tb->lo);

	if (sizeof(dma_addr_t) <= sizeof(u32))
		return addr;

	hi_len = le16_to_cpu(tb->hi_n_len) & 0xF;

	/*
	 * shift by 16 twice to avoid warnings on 32-bit
	 * (where this code never runs anyway due to the
	 * if statement above)
	 */
	return addr | ((hi_len << 16) << 16);
}

void iwl_txq_gen1_tfd_unmap(struct iwl_trans *trans,
			    struct iwl_cmd_meta *meta,
			    struct iwl_txq *txq, int index)
{
	int i, num_tbs;
	void *tfd = iwl_txq_get_tfd(trans, txq, index);

	/* Sanity check on number of chunks */
	num_tbs = iwl_txq_gen1_tfd_get_num_tbs(trans, tfd);

	if (num_tbs > trans->txqs.tfd.max_tbs) {
		IWL_ERR(trans, "Too many chunks: %i\n", num_tbs);
		/* @todo issue fatal error, it is quite serious situation */
		return;
	}

	/* first TB is never freed - it's the bidirectional DMA data */

	for (i = 1; i < num_tbs; i++) {
		if (meta->tbs & BIT(i))
			dma_unmap_page(trans->dev,
				       iwl_txq_gen1_tfd_tb_get_addr(trans,
								    tfd, i),
				       iwl_txq_gen1_tfd_tb_get_len(trans,
								   tfd, i),
				       DMA_TO_DEVICE);
		else
			dma_unmap_single(trans->dev,
					 iwl_txq_gen1_tfd_tb_get_addr(trans,
								      tfd, i),
					 iwl_txq_gen1_tfd_tb_get_len(trans,
								     tfd, i),
					 DMA_TO_DEVICE);
	}

	meta->tbs = 0;

	if (trans->trans_cfg->use_tfh) {
		struct iwl_tfh_tfd *tfd_fh = (void *)tfd;

		tfd_fh->num_tbs = 0;
	} else {
		struct iwl_tfd *tfd_fh = (void *)tfd;

		tfd_fh->num_tbs = 0;
	}
}

#define IWL_TX_CRC_SIZE 4
#define IWL_TX_DELIMITER_SIZE 4

/*
 * iwl_txq_gen1_update_byte_cnt_tbl - Set up entry in Tx byte-count array
 */
void iwl_txq_gen1_update_byte_cnt_tbl(struct iwl_trans *trans,
				      struct iwl_txq *txq, u16 byte_cnt,
				      int num_tbs)
{
	struct iwlagn_scd_bc_tbl *scd_bc_tbl;
	int write_ptr = txq->write_ptr;
	int txq_id = txq->id;
	u8 sec_ctl = 0;
	u16 len = byte_cnt + IWL_TX_CRC_SIZE + IWL_TX_DELIMITER_SIZE;
	__le16 bc_ent;
	struct iwl_device_tx_cmd *dev_cmd = txq->entries[txq->write_ptr].cmd;
	struct iwl_tx_cmd *tx_cmd = (void *)dev_cmd->payload;
	u8 sta_id = tx_cmd->sta_id;

	scd_bc_tbl = trans->txqs.scd_bc_tbls.addr;

	sec_ctl = tx_cmd->sec_ctl;

	switch (sec_ctl & TX_CMD_SEC_MSK) {
	case TX_CMD_SEC_CCM:
		len += IEEE80211_CCMP_MIC_LEN;
		break;
	case TX_CMD_SEC_TKIP:
		len += IEEE80211_TKIP_ICV_LEN;
		break;
	case TX_CMD_SEC_WEP:
		len += IEEE80211_WEP_IV_LEN + IEEE80211_WEP_ICV_LEN;
		break;
	}
	if (trans->txqs.bc_table_dword)
		len = DIV_ROUND_UP(len, 4);

	if (WARN_ON(len > 0xFFF || write_ptr >= TFD_QUEUE_SIZE_MAX))
		return;

	bc_ent = cpu_to_le16(len | (sta_id << 12));

	scd_bc_tbl[txq_id].tfd_offset[write_ptr] = bc_ent;

	if (write_ptr < TFD_QUEUE_SIZE_BC_DUP)
		scd_bc_tbl[txq_id].tfd_offset[TFD_QUEUE_SIZE_MAX + write_ptr] =
			bc_ent;
}

void iwl_txq_gen1_inval_byte_cnt_tbl(struct iwl_trans *trans,
				     struct iwl_txq *txq)
{
	struct iwlagn_scd_bc_tbl *scd_bc_tbl = trans->txqs.scd_bc_tbls.addr;
	int txq_id = txq->id;
	int read_ptr = txq->read_ptr;
	u8 sta_id = 0;
	__le16 bc_ent;
	struct iwl_device_tx_cmd *dev_cmd = txq->entries[read_ptr].cmd;
	struct iwl_tx_cmd *tx_cmd = (void *)dev_cmd->payload;

	WARN_ON(read_ptr >= TFD_QUEUE_SIZE_MAX);

	if (txq_id != trans->txqs.cmd.q_id)
		sta_id = tx_cmd->sta_id;

	bc_ent = cpu_to_le16(1 | (sta_id << 12));

	scd_bc_tbl[txq_id].tfd_offset[read_ptr] = bc_ent;

	if (read_ptr < TFD_QUEUE_SIZE_BC_DUP)
		scd_bc_tbl[txq_id].tfd_offset[TFD_QUEUE_SIZE_MAX + read_ptr] =
			bc_ent;
}

/*
 * iwl_txq_free_tfd - Free all chunks referenced by TFD [txq->q.read_ptr]
 * @trans - transport private data
 * @txq - tx queue
 * @dma_dir - the direction of the DMA mapping
 *
 * Does NOT advance any TFD circular buffer read/write indexes
 * Does NOT free the TFD itself (which is within circular buffer)
 */
void iwl_txq_free_tfd(struct iwl_trans *trans, struct iwl_txq *txq)
{
	/* rd_ptr is bounded by TFD_QUEUE_SIZE_MAX and
	 * idx is bounded by n_window
	 */
	int rd_ptr = txq->read_ptr;
	int idx = iwl_txq_get_cmd_index(txq, rd_ptr);
	struct sk_buff *skb;

	lockdep_assert_held(&txq->lock);

	if (!txq->entries)
		return;

	/* We have only q->n_window txq->entries, but we use
	 * TFD_QUEUE_SIZE_MAX tfds
	 */
	iwl_txq_gen1_tfd_unmap(trans, &txq->entries[idx].meta, txq, rd_ptr);

	/* free SKB */
	skb = txq->entries[idx].skb;

	/* Can be called from irqs-disabled context
	 * If skb is not NULL, it means that the whole queue is being
	 * freed and that the queue is not empty - free the skb
	 */
	if (skb) {
		iwl_op_mode_free_skb(trans->op_mode, skb);
		txq->entries[idx].skb = NULL;
	}
}

void iwl_txq_progress(struct iwl_txq *txq)
{
	lockdep_assert_held(&txq->lock);

	if (!txq->wd_timeout)
		return;

	/*
	 * station is asleep and we send data - that must
	 * be uAPSD or PS-Poll. Don't rearm the timer.
	 */
	if (txq->frozen)
		return;

	/*
	 * if empty delete timer, otherwise move timer forward
	 * since we're making progress on this queue
	 */
	if (txq->read_ptr == txq->write_ptr)
		del_timer(&txq->stuck_timer);
	else
		mod_timer(&txq->stuck_timer, jiffies + txq->wd_timeout);
}

/* Frees buffers until index _not_ inclusive */
void iwl_txq_reclaim(struct iwl_trans *trans, int txq_id, int ssn,
		     struct sk_buff_head *skbs)
{
	struct iwl_txq *txq = trans->txqs.txq[txq_id];
	int tfd_num = iwl_txq_get_cmd_index(txq, ssn);
	int read_ptr = iwl_txq_get_cmd_index(txq, txq->read_ptr);
	int last_to_free;

	/* This function is not meant to release cmd queue*/
	if (WARN_ON(txq_id == trans->txqs.cmd.q_id))
		return;

	spin_lock_bh(&txq->lock);

	if (!test_bit(txq_id, trans->txqs.queue_used)) {
		IWL_DEBUG_TX_QUEUES(trans, "Q %d inactive - ignoring idx %d\n",
				    txq_id, ssn);
		goto out;
	}

	if (read_ptr == tfd_num)
		goto out;

	IWL_DEBUG_TX_REPLY(trans, "[Q %d] %d -> %d (%d)\n",
			   txq_id, txq->read_ptr, tfd_num, ssn);

	/*Since we free until index _not_ inclusive, the one before index is
	 * the last we will free. This one must be used */
	last_to_free = iwl_txq_dec_wrap(trans, tfd_num);

	if (!iwl_txq_used(txq, last_to_free)) {
		IWL_ERR(trans,
			"%s: Read index for txq id (%d), last_to_free %d is out of range [0-%d] %d %d.\n",
			__func__, txq_id, last_to_free,
			trans->trans_cfg->base_params->max_tfd_queue_size,
			txq->write_ptr, txq->read_ptr);

		iwl_op_mode_time_point(trans->op_mode,
				       IWL_FW_INI_TIME_POINT_FAKE_TX,
				       NULL);
		goto out;
	}

	if (WARN_ON(!skb_queue_empty(skbs)))
		goto out;

	for (;
	     read_ptr != tfd_num;
	     txq->read_ptr = iwl_txq_inc_wrap(trans, txq->read_ptr),
	     read_ptr = iwl_txq_get_cmd_index(txq, txq->read_ptr)) {
		struct sk_buff *skb = txq->entries[read_ptr].skb;

		if (WARN_ON_ONCE(!skb))
			continue;

		iwl_txq_free_tso_page(trans, skb);

		__skb_queue_tail(skbs, skb);

		txq->entries[read_ptr].skb = NULL;

		if (!trans->trans_cfg->use_tfh)
			iwl_txq_gen1_inval_byte_cnt_tbl(trans, txq);

		iwl_txq_free_tfd(trans, txq);
	}

	iwl_txq_progress(txq);

	if (iwl_txq_space(trans, txq) > txq->low_mark &&
	    test_bit(txq_id, trans->txqs.queue_stopped)) {
		struct sk_buff_head overflow_skbs;

		__skb_queue_head_init(&overflow_skbs);
		skb_queue_splice_init(&txq->overflow_q, &overflow_skbs);

		/*
		 * We are going to transmit from the overflow queue.
		 * Remember this state so that wait_for_txq_empty will know we
		 * are adding more packets to the TFD queue. It cannot rely on
		 * the state of &txq->overflow_q, as we just emptied it, but
		 * haven't TXed the content yet.
		 */
		txq->overflow_tx = true;

		/*
		 * This is tricky: we are in reclaim path which is non
		 * re-entrant, so noone will try to take the access the
		 * txq data from that path. We stopped tx, so we can't
		 * have tx as well. Bottom line, we can unlock and re-lock
		 * later.
		 */
		spin_unlock_bh(&txq->lock);

		while (!skb_queue_empty(&overflow_skbs)) {
			struct sk_buff *skb = __skb_dequeue(&overflow_skbs);
			struct iwl_device_tx_cmd *dev_cmd_ptr;

			dev_cmd_ptr = *(void **)((u8 *)skb->cb +
						 trans->txqs.dev_cmd_offs);

			/*
			 * Note that we can very well be overflowing again.
			 * In that case, iwl_txq_space will be small again
			 * and we won't wake mac80211's queue.
			 */
			iwl_trans_tx(trans, skb, dev_cmd_ptr, txq_id);
		}

		if (iwl_txq_space(trans, txq) > txq->low_mark)
			iwl_wake_queue(trans, txq);

		spin_lock_bh(&txq->lock);
		txq->overflow_tx = false;
	}

out:
	spin_unlock_bh(&txq->lock);
}

/* Set wr_ptr of specific device and txq  */
void iwl_txq_set_q_ptrs(struct iwl_trans *trans, int txq_id, int ptr)
{
	struct iwl_txq *txq = trans->txqs.txq[txq_id];

	spin_lock_bh(&txq->lock);

	txq->write_ptr = ptr;
	txq->read_ptr = txq->write_ptr;

	spin_unlock_bh(&txq->lock);
}

void iwl_trans_txq_freeze_timer(struct iwl_trans *trans, unsigned long txqs,
				bool freeze)
{
	int queue;

	for_each_set_bit(queue, &txqs, BITS_PER_LONG) {
		struct iwl_txq *txq = trans->txqs.txq[queue];
		unsigned long now;

		spin_lock_bh(&txq->lock);

		now = jiffies;

		if (txq->frozen == freeze)
			goto next_queue;

		IWL_DEBUG_TX_QUEUES(trans, "%s TXQ %d\n",
				    freeze ? "Freezing" : "Waking", queue);

		txq->frozen = freeze;

		if (txq->read_ptr == txq->write_ptr)
			goto next_queue;

		if (freeze) {
			if (unlikely(time_after(now,
						txq->stuck_timer.expires))) {
				/*
				 * The timer should have fired, maybe it is
				 * spinning right now on the lock.
				 */
				goto next_queue;
			}
			/* remember how long until the timer fires */
			txq->frozen_expiry_remainder =
				txq->stuck_timer.expires - now;
			del_timer(&txq->stuck_timer);
			goto next_queue;
		}

		/*
		 * Wake a non-empty queue -> arm timer with the
		 * remainder before it froze
		 */
		mod_timer(&txq->stuck_timer,
			  now + txq->frozen_expiry_remainder);

next_queue:
		spin_unlock_bh(&txq->lock);
	}
}

#define HOST_COMPLETE_TIMEOUT	(2 * HZ)

static int iwl_trans_txq_send_hcmd_sync(struct iwl_trans *trans,
					struct iwl_host_cmd *cmd)
{
	const char *cmd_str = iwl_get_cmd_string(trans, cmd->id);
	struct iwl_txq *txq = trans->txqs.txq[trans->txqs.cmd.q_id];
	int cmd_idx;
	int ret;

	IWL_DEBUG_INFO(trans, "Attempting to send sync command %s\n", cmd_str);

	if (WARN(test_and_set_bit(STATUS_SYNC_HCMD_ACTIVE,
				  &trans->status),
		 "Command %s: a command is already active!\n", cmd_str))
		return -EIO;

	IWL_DEBUG_INFO(trans, "Setting HCMD_ACTIVE for command %s\n", cmd_str);

	cmd_idx = trans->ops->send_cmd(trans, cmd);
	if (cmd_idx < 0) {
		ret = cmd_idx;
		clear_bit(STATUS_SYNC_HCMD_ACTIVE, &trans->status);
		IWL_ERR(trans, "Error sending %s: enqueue_hcmd failed: %d\n",
			cmd_str, ret);
		return ret;
	}

	ret = wait_event_timeout(trans->wait_command_queue,
				 !test_bit(STATUS_SYNC_HCMD_ACTIVE,
					   &trans->status),
				 HOST_COMPLETE_TIMEOUT);
	if (!ret) {
		IWL_ERR(trans, "Error sending %s: time out after %dms.\n",
			cmd_str, jiffies_to_msecs(HOST_COMPLETE_TIMEOUT));

		IWL_ERR(trans, "Current CMD queue read_ptr %d write_ptr %d\n",
			txq->read_ptr, txq->write_ptr);

		clear_bit(STATUS_SYNC_HCMD_ACTIVE, &trans->status);
		IWL_DEBUG_INFO(trans, "Clearing HCMD_ACTIVE for command %s\n",
			       cmd_str);
		ret = -ETIMEDOUT;

		iwl_trans_sync_nmi(trans);
		goto cancel;
	}

	if (test_bit(STATUS_FW_ERROR, &trans->status)) {
		if (!test_and_clear_bit(STATUS_SUPPRESS_CMD_ERROR_ONCE,
					&trans->status)) {
			IWL_ERR(trans, "FW error in SYNC CMD %s\n", cmd_str);
			dump_stack();
		}
		ret = -EIO;
		goto cancel;
	}

	if (!(cmd->flags & CMD_SEND_IN_RFKILL) &&
	    test_bit(STATUS_RFKILL_OPMODE, &trans->status)) {
		IWL_DEBUG_RF_KILL(trans, "RFKILL in SYNC CMD... no rsp\n");
		ret = -ERFKILL;
		goto cancel;
	}

	if ((cmd->flags & CMD_WANT_SKB) && !cmd->resp_pkt) {
		IWL_ERR(trans, "Error: Response NULL in '%s'\n", cmd_str);
		ret = -EIO;
		goto cancel;
	}

	return 0;

cancel:
	if (cmd->flags & CMD_WANT_SKB) {
		/*
		 * Cancel the CMD_WANT_SKB flag for the cmd in the
		 * TX cmd queue. Otherwise in case the cmd comes
		 * in later, it will possibly set an invalid
		 * address (cmd->meta.source).
		 */
		txq->entries[cmd_idx].meta.flags &= ~CMD_WANT_SKB;
	}

	if (cmd->resp_pkt) {
		iwl_free_resp(cmd);
		cmd->resp_pkt = NULL;
	}

	return ret;
}

int iwl_trans_txq_send_hcmd(struct iwl_trans *trans,
			    struct iwl_host_cmd *cmd)
{
	/* Make sure the NIC is still alive in the bus */
	if (test_bit(STATUS_TRANS_DEAD, &trans->status))
		return -ENODEV;

	if (!(cmd->flags & CMD_SEND_IN_RFKILL) &&
	    test_bit(STATUS_RFKILL_OPMODE, &trans->status)) {
		IWL_DEBUG_RF_KILL(trans, "Dropping CMD 0x%x: RF KILL\n",
				  cmd->id);
		return -ERFKILL;
	}

	if (unlikely(trans->system_pm_mode == IWL_PLAT_PM_MODE_D3 &&
		     !(cmd->flags & CMD_SEND_IN_D3))) {
		IWL_DEBUG_WOWLAN(trans, "Dropping CMD 0x%x: D3\n", cmd->id);
		return -EHOSTDOWN;
	}

	if (cmd->flags & CMD_ASYNC) {
		int ret;

		/* An asynchronous command can not expect an SKB to be set. */
		if (WARN_ON(cmd->flags & CMD_WANT_SKB))
			return -EINVAL;

		ret = trans->ops->send_cmd(trans, cmd);
		if (ret < 0) {
			IWL_ERR(trans,
				"Error sending %s: enqueue_hcmd failed: %d\n",
				iwl_get_cmd_string(trans, cmd->id), ret);
			return ret;
		}
		return 0;
	}

	return iwl_trans_txq_send_hcmd_sync(trans, cmd);
}

