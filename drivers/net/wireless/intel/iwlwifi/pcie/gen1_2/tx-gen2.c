// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * Copyright (C) 2017 Intel Deutschland GmbH
 * Copyright (C) 2018-2020, 2023-2025 Intel Corporation
 */
#include <net/tso.h>
#include <linux/tcp.h>

#include "iwl-debug.h"
#include "iwl-csr.h"
#include "iwl-io.h"
#include "internal.h"
#include "fw/api/tx.h"
#include "fw/api/commands.h"
#include "fw/api/datapath.h"
#include "iwl-scd.h"

static struct page *get_workaround_page(struct iwl_trans *trans,
					struct sk_buff *skb)
{
	struct iwl_tso_page_info *info;
	struct page **page_ptr;
	struct page *ret;
	dma_addr_t phys;

	page_ptr = (void *)((u8 *)skb->cb + trans->conf.cb_data_offs);

	ret = alloc_page(GFP_ATOMIC);
	if (!ret)
		return NULL;

	info = IWL_TSO_PAGE_INFO(page_address(ret));

	/* Create a DMA mapping for the page */
	phys = dma_map_page_attrs(trans->dev, ret, 0, PAGE_SIZE,
				  DMA_TO_DEVICE, DMA_ATTR_SKIP_CPU_SYNC);
	if (unlikely(dma_mapping_error(trans->dev, phys))) {
		__free_page(ret);
		return NULL;
	}

	/* Store physical address and set use count */
	info->dma_addr = phys;
	refcount_set(&info->use_count, 1);

	/* set the chaining pointer to the previous page if there */
	info->next = *page_ptr;
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
				       u16 len, struct iwl_cmd_meta *meta,
				       bool unmap)
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

	if (WARN_ON(len > IWL_TSO_PAGE_DATA_SIZE)) {
		ret = -ENOBUFS;
		goto unmap;
	}

	page = get_workaround_page(trans, skb);
	if (!page) {
		ret = -ENOMEM;
		goto unmap;
	}

	memcpy(page_address(page), virt, len);

	/*
	 * This is a bit odd, but performance does not matter here, what
	 * matters are the expectations of the calling code and TB cleanup
	 * function.
	 *
	 * As such, if unmap is set, then create another mapping for the TB
	 * entry as it will be unmapped later. On the other hand, if it is not
	 * set, then the TB entry will not be unmapped and instead we simply
	 * reference and sync the mapping that get_workaround_page() created.
	 */
	if (unmap) {
		phys = dma_map_single(trans->dev, page_address(page), len,
				      DMA_TO_DEVICE);
		if (unlikely(dma_mapping_error(trans->dev, phys)))
			return -ENOMEM;
	} else {
		phys = iwl_pcie_get_tso_page_phys(page_address(page));
		dma_sync_single_for_device(trans->dev, phys, len,
					   DMA_TO_DEVICE);
	}

	ret = iwl_txq_gen2_set_tb(trans, tfd, phys, len);
	if (ret < 0) {
		/* unmap the new allocation as single */
		oldphys = phys;
		meta = NULL;
		goto unmap;
	}

	IWL_DEBUG_TX(trans,
		     "TB bug workaround: copied %d bytes from 0x%llx to 0x%llx\n",
		     len, (unsigned long long)oldphys,
		     (unsigned long long)phys);

	ret = 0;
unmap:
	if (!unmap)
		goto trace;

	if (meta)
		dma_unmap_page(trans->dev, oldphys, len, DMA_TO_DEVICE);
	else
		dma_unmap_single(trans->dev, oldphys, len, DMA_TO_DEVICE);
trace:
	trace_iwlwifi_dev_tx_tb(trans->dev, skb, virt, phys, len);

	return ret;
}

static int iwl_txq_gen2_build_amsdu(struct iwl_trans *trans,
				    struct sk_buff *skb,
				    struct iwl_tfh_tfd *tfd,
				    struct iwl_cmd_meta *out_meta,
				    int start_len,
				    u8 hdr_len,
				    struct iwl_device_tx_cmd *dev_cmd)
{
#ifdef CONFIG_INET
	struct iwl_tx_cmd_v9 *tx_cmd = (void *)dev_cmd->payload;
	struct ieee80211_hdr *hdr = (void *)skb->data;
	unsigned int snap_ip_tcp_hdrlen, ip_hdrlen, total_len, hdr_room;
	unsigned int mss = skb_shinfo(skb)->gso_size;
	unsigned int data_offset = 0;
	dma_addr_t start_hdr_phys;
	u16 length, amsdu_pad;
	u8 *start_hdr;
	struct sg_table *sgt;
	struct tso_t tso;

	trace_iwlwifi_dev_tx(trans->dev, skb, tfd, sizeof(*tfd),
			     &dev_cmd->hdr, start_len, 0);

	ip_hdrlen = skb_network_header_len(skb);
	snap_ip_tcp_hdrlen = 8 + ip_hdrlen + tcp_hdrlen(skb);
	total_len = skb->len - snap_ip_tcp_hdrlen - hdr_len;
	amsdu_pad = 0;

	/* total amount of header we may need for this A-MSDU */
	hdr_room = DIV_ROUND_UP(total_len, mss) *
		(3 + snap_ip_tcp_hdrlen + sizeof(struct ethhdr));

	/* Our device supports 9 segments at most, it will fit in 1 page */
	sgt = iwl_pcie_prep_tso(trans, skb, out_meta, &start_hdr, hdr_room,
				snap_ip_tcp_hdrlen + hdr_len);
	if (!sgt)
		return -ENOMEM;

	start_hdr_phys = iwl_pcie_get_tso_page_phys(start_hdr);

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
		u8 *pos_hdr = start_hdr;

		total_len -= data_left;

		memset(pos_hdr, 0, amsdu_pad);
		pos_hdr += amsdu_pad;
		amsdu_pad = (4 - (sizeof(struct ethhdr) + snap_ip_tcp_hdrlen +
				  data_left)) & 0x3;
		ether_addr_copy(pos_hdr, ieee80211_get_DA(hdr));
		pos_hdr += ETH_ALEN;
		ether_addr_copy(pos_hdr, ieee80211_get_SA(hdr));
		pos_hdr += ETH_ALEN;

		length = snap_ip_tcp_hdrlen + data_left;
		*((__be16 *)pos_hdr) = cpu_to_be16(length);
		pos_hdr += sizeof(length);

		/*
		 * This will copy the SNAP as well which will be considered
		 * as MAC header.
		 */
		tso_build_hdr(skb, pos_hdr, &tso, data_left, !total_len);

		pos_hdr += snap_ip_tcp_hdrlen;

		tb_len = pos_hdr - start_hdr;
		tb_phys = iwl_pcie_get_tso_page_phys(start_hdr);

		/*
		 * No need for _with_wa, this is from the TSO page and
		 * we leave some space at the end of it so can't hit
		 * the buggy scenario.
		 */
		iwl_txq_gen2_set_tb(trans, tfd, tb_phys, tb_len);
		trace_iwlwifi_dev_tx_tb(trans->dev, skb, start_hdr,
					tb_phys, tb_len);
		/* add this subframe's headers' length to the tx_cmd */
		le16_add_cpu(&tx_cmd->len, tb_len);

		/* prepare the start_hdr for the next subframe */
		start_hdr = pos_hdr;

		/* put the payload */
		while (data_left) {
			int ret;

			tb_len = min_t(unsigned int, tso.size, data_left);
			tb_phys = iwl_pcie_get_sgt_tb_phys(sgt, data_offset,
							   tb_len);
			/* Not a real mapping error, use direct comparison */
			if (unlikely(tb_phys == DMA_MAPPING_ERROR))
				goto out_err;

			ret = iwl_txq_gen2_set_tb_with_wa(trans, skb, tfd,
							  tb_phys, tso.data,
							  tb_len, NULL, false);
			if (ret)
				goto out_err;

			data_left -= tb_len;
			data_offset += tb_len;
			tso_build_data(skb, &tso, tb_len);
		}
	}

	dma_sync_single_for_device(trans->dev, start_hdr_phys, hdr_room,
				   DMA_TO_DEVICE);

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

	if (iwl_txq_gen2_build_amsdu(trans, skb, tfd, out_meta,
				     len + IWL_FIRST_TB_SIZE, hdr_len, dev_cmd))
		goto out_err;

	/* building the A-MSDU might have changed this data, memcpy it now */
	memcpy(&txq->first_tb_bufs[idx], dev_cmd, IWL_FIRST_TB_SIZE);
	return tfd;

out_err:
	iwl_pcie_free_tso_pages(trans, skb, out_meta);
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
						  fragsz, out_meta, true);
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
						  NULL, true);
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
						  skb_headlen(frag), NULL,
						  true);
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
	BUILD_BUG_ON(sizeof(struct iwl_tx_cmd_v9) < IWL_FIRST_TB_SIZE);
	BUILD_BUG_ON(sizeof(struct iwl_cmd_header) +
		     offsetofend(struct iwl_tx_cmd_v9, dram_info) >
		     IWL_FIRST_TB_SIZE);
	BUILD_BUG_ON(sizeof(struct iwl_tx_cmd) < IWL_FIRST_TB_SIZE);
	BUILD_BUG_ON(sizeof(struct iwl_cmd_header) +
		     offsetofend(struct iwl_tx_cmd, dram_info) >
		     IWL_FIRST_TB_SIZE);

	memset(tfd, 0, sizeof(*tfd));

	if (trans->mac_cfg->device_family < IWL_DEVICE_FAMILY_AX210)
		len = sizeof(struct iwl_tx_cmd_v9);
	else
		len = sizeof(struct iwl_tx_cmd);

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
	if (q->n_window < trans->mac_cfg->base->max_tfd_queue_size)
		max = q->n_window;
	else
		max = trans->mac_cfg->base->max_tfd_queue_size - 1;

	/*
	 * max_tfd_queue_size is a power of 2, so the following is equivalent to
	 * modulo by max_tfd_queue_size and is well defined.
	 */
	used = (q->write_ptr - q->read_ptr) &
		(trans->mac_cfg->base->max_tfd_queue_size - 1);

	if (WARN_ON(used > max))
		return 0;

	return max - used;
}

/*
 * iwl_pcie_gen2_update_byte_tbl - Set up entry in Tx byte-count array
 */
static void iwl_pcie_gen2_update_byte_tbl(struct iwl_trans *trans,
					  struct iwl_txq *txq, u16 byte_cnt,
					  int num_tbs)
{
	int idx = iwl_txq_get_cmd_index(txq, txq->write_ptr);
	struct iwl_bc_tbl_entry *scd_bc_tbl = txq->bc_tbl.addr;
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

	if (trans->mac_cfg->device_family >= IWL_DEVICE_FAMILY_AX210) {
		WARN_ON(len > 0x3FFF);
		bc_ent = cpu_to_le16(len | (num_fetch_chunks << 14));
	} else {
		len = DIV_ROUND_UP(len, 4);
		WARN_ON(len > 0xFFF);
		bc_ent = cpu_to_le16(len | (num_fetch_chunks << 12));
	}

	scd_bc_tbl[idx].tfd_offset = bc_ent;
}

static u8 iwl_txq_gen2_get_num_tbs(struct iwl_tfh_tfd *tfd)
{
	return le16_to_cpu(tfd->num_tbs) & 0x1f;
}

int iwl_txq_gen2_set_tb(struct iwl_trans *trans, struct iwl_tfh_tfd *tfd,
			dma_addr_t addr, u16 len)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
	int idx = iwl_txq_gen2_get_num_tbs(tfd);
	struct iwl_tfh_tb *tb;

	/* Only WARN here so we know about the issue, but we mess up our
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
	if (le16_to_cpu(tfd->num_tbs) >= trans_pcie->txqs.tfd.max_tbs) {
		IWL_ERR(trans, "Error can not send more than %d chunks\n",
			trans_pcie->txqs.tfd.max_tbs);
		return -EINVAL;
	}

	put_unaligned_le64(addr, &tb->addr);
	tb->tb_len = cpu_to_le16(len);

	tfd->num_tbs = cpu_to_le16(idx + 1);

	return idx;
}

void iwl_txq_gen2_tfd_unmap(struct iwl_trans *trans,
			    struct iwl_cmd_meta *meta,
			    struct iwl_tfh_tfd *tfd)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
	int i, num_tbs;

	/* Sanity check on number of chunks */
	num_tbs = iwl_txq_gen2_get_num_tbs(tfd);

	if (num_tbs > trans_pcie->txqs.tfd.max_tbs) {
		IWL_ERR(trans, "Too many chunks: %i\n", num_tbs);
		return;
	}

	/* TB1 is mapped directly, the rest is the TSO page and SG list. */
	if (meta->sg_offset)
		num_tbs = 2;

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

	iwl_txq_set_tfd_invalid_gen2(trans, tfd);
}

static void iwl_txq_gen2_free_tfd(struct iwl_trans *trans, struct iwl_txq *txq)
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

/*
 * iwl_txq_inc_wr_ptr - Send new write index to hardware
 */
static void iwl_txq_inc_wr_ptr(struct iwl_trans *trans, struct iwl_txq *txq)
{
	lockdep_assert_held(&txq->lock);

	IWL_DEBUG_TX(trans, "Q:%d WR: 0x%x\n", txq->id, txq->write_ptr);

	/*
	 * if not in power-save mode, uCode will never sleep when we're
	 * trying to tx (during RFKILL, we're not trying to tx).
	 */
	iwl_write32(trans, HBUS_TARG_WRPTR, txq->write_ptr | (txq->id << 16));
}

int iwl_txq_gen2_tx(struct iwl_trans *trans, struct sk_buff *skb,
		    struct iwl_device_tx_cmd *dev_cmd, int txq_id)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
	struct iwl_cmd_meta *out_meta;
	struct iwl_txq *txq = trans_pcie->txqs.txq[txq_id];
	u16 cmd_len;
	int idx;
	void *tfd;

	if (WARN_ONCE(txq_id >= IWL_MAX_TVQM_QUEUES,
		      "queue %d out of range", txq_id))
		return -EINVAL;

	if (WARN_ONCE(!test_bit(txq_id, trans_pcie->txqs.queue_used),
		      "TX on unused queue %d\n", txq_id))
		return -EINVAL;

	if (skb_is_nonlinear(skb) &&
	    skb_shinfo(skb)->nr_frags > IWL_TRANS_PCIE_MAX_FRAGS(trans_pcie) &&
	    __skb_linearize(skb))
		return -ENOMEM;

	spin_lock(&txq->lock);

	if (iwl_txq_space(trans, txq) < txq->high_mark) {
		iwl_txq_stop(trans, txq);

		/* don't put the packet on the ring, if there is no room */
		if (unlikely(iwl_txq_space(trans, txq) < 3)) {
			struct iwl_device_tx_cmd **dev_cmd_ptr;

			dev_cmd_ptr = (void *)((u8 *)skb->cb +
					       trans->conf.cb_data_offs +
					       sizeof(void *));

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
	memset(out_meta, 0, sizeof(*out_meta));

	tfd = iwl_txq_gen2_build_tfd(trans, txq, dev_cmd, skb, out_meta);
	if (!tfd) {
		spin_unlock(&txq->lock);
		return -1;
	}

	if (trans->mac_cfg->device_family >= IWL_DEVICE_FAMILY_AX210) {
		struct iwl_tx_cmd *tx_cmd =
			(void *)dev_cmd->payload;

		cmd_len = le16_to_cpu(tx_cmd->len);
	} else {
		struct iwl_tx_cmd_v9 *tx_cmd_v9 =
			(void *)dev_cmd->payload;

		cmd_len = le16_to_cpu(tx_cmd_v9->len);
	}

	/* Set up entry for this TFD in Tx byte-count array */
	iwl_pcie_gen2_update_byte_tbl(trans, txq, cmd_len,
				      iwl_txq_gen2_get_num_tbs(tfd));

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
static void iwl_txq_gen2_unmap(struct iwl_trans *trans, int txq_id)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
	struct iwl_txq *txq = trans_pcie->txqs.txq[txq_id];

	spin_lock_bh(&txq->reclaim_lock);
	spin_lock(&txq->lock);
	while (txq->write_ptr != txq->read_ptr) {
		IWL_DEBUG_TX_REPLY(trans, "Q %d Free %d\n",
				   txq_id, txq->read_ptr);

		if (txq_id != trans->conf.cmd_queue) {
			int idx = iwl_txq_get_cmd_index(txq, txq->read_ptr);
			struct iwl_cmd_meta *cmd_meta = &txq->entries[idx].meta;
			struct sk_buff *skb = txq->entries[idx].skb;

			if (!WARN_ON_ONCE(!skb))
				iwl_pcie_free_tso_pages(trans, skb, cmd_meta);
		}
		iwl_txq_gen2_free_tfd(trans, txq);
		txq->read_ptr = iwl_txq_inc_wrap(trans, txq->read_ptr);
	}

	while (!skb_queue_empty(&txq->overflow_q)) {
		struct sk_buff *skb = __skb_dequeue(&txq->overflow_q);

		iwl_op_mode_free_skb(trans->op_mode, skb);
	}

	spin_unlock(&txq->lock);
	spin_unlock_bh(&txq->reclaim_lock);

	/* just in case - this queue may have been stopped */
	iwl_trans_pcie_wake_queue(trans, txq);
}

static void iwl_txq_gen2_free_memory(struct iwl_trans *trans,
				     struct iwl_txq *txq)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
	struct device *dev = trans->dev;

	/* De-alloc circular buffer of TFDs */
	if (txq->tfds) {
		dma_free_coherent(dev,
				  trans_pcie->txqs.tfd.size * txq->n_window,
				  txq->tfds, txq->dma_addr);
		dma_free_coherent(dev,
				  sizeof(*txq->first_tb_bufs) * txq->n_window,
				  txq->first_tb_bufs, txq->first_tb_dma);
	}

	kfree(txq->entries);
	if (txq->bc_tbl.addr)
		dma_pool_free(trans_pcie->txqs.bc_pool,
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
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
	struct iwl_txq *txq;
	int i;

	if (WARN_ONCE(txq_id >= IWL_MAX_TVQM_QUEUES,
		      "queue %d out of range", txq_id))
		return;

	txq = trans_pcie->txqs.txq[txq_id];

	if (WARN_ON(!txq))
		return;

	iwl_txq_gen2_unmap(trans, txq_id);

	/* De-alloc array of command/tx buffers */
	if (txq_id == trans->conf.cmd_queue)
		for (i = 0; i < txq->n_window; i++) {
			kfree_sensitive(txq->entries[i].cmd);
			kfree_sensitive(txq->entries[i].free_buf);
		}
	timer_delete_sync(&txq->stuck_timer);

	iwl_txq_gen2_free_memory(trans, txq);

	trans_pcie->txqs.txq[txq_id] = NULL;

	clear_bit(txq_id, trans_pcie->txqs.queue_used);
}

static struct iwl_txq *
iwl_txq_dyn_alloc_dma(struct iwl_trans *trans, int size, unsigned int timeout)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
	size_t bc_tbl_size, bc_tbl_entries;
	struct iwl_txq *txq;
	int ret;

	WARN_ON(!trans_pcie->txqs.bc_tbl_size);

	bc_tbl_size = trans_pcie->txqs.bc_tbl_size;
	bc_tbl_entries = bc_tbl_size / sizeof(u16);

	if (WARN_ON(size > bc_tbl_entries))
		return ERR_PTR(-EINVAL);

	txq = kzalloc(sizeof(*txq), GFP_KERNEL);
	if (!txq)
		return ERR_PTR(-ENOMEM);

	txq->bc_tbl.addr = dma_pool_alloc(trans_pcie->txqs.bc_pool, GFP_KERNEL,
					  &txq->bc_tbl.dma);
	if (!txq->bc_tbl.addr) {
		IWL_ERR(trans, "Scheduler BC Table allocation failed\n");
		kfree(txq);
		return ERR_PTR(-ENOMEM);
	}

	ret = iwl_pcie_txq_alloc(trans, txq, size, false);
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

	return txq;

error:
	iwl_txq_gen2_free_memory(trans, txq);
	return ERR_PTR(ret);
}

static int iwl_pcie_txq_alloc_response(struct iwl_trans *trans,
				       struct iwl_txq *txq,
				       struct iwl_host_cmd *hcmd)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
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

	if (qid >= ARRAY_SIZE(trans_pcie->txqs.txq)) {
		WARN_ONCE(1, "queue index %d unsupported", qid);
		ret = -EIO;
		goto error_free_resp;
	}

	if (test_and_set_bit(qid, trans_pcie->txqs.queue_used)) {
		WARN_ONCE(1, "queue %d already used", qid);
		ret = -EIO;
		goto error_free_resp;
	}

	if (WARN_ONCE(trans_pcie->txqs.txq[qid],
		      "queue %d already allocated\n", qid)) {
		ret = -EIO;
		goto error_free_resp;
	}

	txq->id = qid;
	trans_pcie->txqs.txq[qid] = txq;
	wr_ptr &= (trans->mac_cfg->base->max_tfd_queue_size - 1);

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

int iwl_txq_dyn_alloc(struct iwl_trans *trans, u32 flags, u32 sta_mask,
		      u8 tid, int size, unsigned int timeout)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
	struct iwl_txq *txq;
	union {
		struct iwl_tx_queue_cfg_cmd old;
		struct iwl_scd_queue_cfg_cmd new;
	} cmd;
	struct iwl_host_cmd hcmd = {
		.flags = CMD_WANT_SKB,
	};
	int ret;

	/* take the min with bytecount table entries allowed */
	size = min_t(u32, size, trans_pcie->txqs.bc_tbl_size / sizeof(u16));
	/* but must be power of 2 values for calculating read/write pointers */
	size = rounddown_pow_of_two(size);

	if (trans->mac_cfg->device_family == IWL_DEVICE_FAMILY_BZ &&
	    trans->info.hw_rev_step == SILICON_A_STEP) {
		size = 4096;
		txq = iwl_txq_dyn_alloc_dma(trans, size, timeout);
	} else {
		do {
			txq = iwl_txq_dyn_alloc_dma(trans, size, timeout);
			if (!IS_ERR(txq))
				break;

			IWL_DEBUG_TX_QUEUES(trans,
					    "Failed allocating TXQ of size %d for sta mask %x tid %d, ret: %ld\n",
					    size, sta_mask, tid,
					    PTR_ERR(txq));
			size /= 2;
		} while (size >= 16);
	}

	if (IS_ERR(txq))
		return PTR_ERR(txq);

	if (trans->conf.queue_alloc_cmd_ver == 0) {
		memset(&cmd.old, 0, sizeof(cmd.old));
		cmd.old.tfdq_addr = cpu_to_le64(txq->dma_addr);
		cmd.old.byte_cnt_addr = cpu_to_le64(txq->bc_tbl.dma);
		cmd.old.cb_size = cpu_to_le32(TFD_QUEUE_CB_SIZE(size));
		cmd.old.flags = cpu_to_le16(flags | TX_QUEUE_CFG_ENABLE_QUEUE);
		cmd.old.tid = tid;

		if (hweight32(sta_mask) != 1) {
			ret = -EINVAL;
			goto error;
		}
		cmd.old.sta_id = ffs(sta_mask) - 1;

		hcmd.id = SCD_QUEUE_CFG;
		hcmd.len[0] = sizeof(cmd.old);
		hcmd.data[0] = &cmd.old;
	} else if (trans->conf.queue_alloc_cmd_ver == 3) {
		memset(&cmd.new, 0, sizeof(cmd.new));
		cmd.new.operation = cpu_to_le32(IWL_SCD_QUEUE_ADD);
		cmd.new.u.add.tfdq_dram_addr = cpu_to_le64(txq->dma_addr);
		cmd.new.u.add.bc_dram_addr = cpu_to_le64(txq->bc_tbl.dma);
		cmd.new.u.add.cb_size = cpu_to_le32(TFD_QUEUE_CB_SIZE(size));
		cmd.new.u.add.flags = cpu_to_le32(flags);
		cmd.new.u.add.sta_mask = cpu_to_le32(sta_mask);
		cmd.new.u.add.tid = tid;

		hcmd.id = WIDE_ID(DATA_PATH_GROUP, SCD_QUEUE_CONFIG_CMD);
		hcmd.len[0] = sizeof(cmd.new);
		hcmd.data[0] = &cmd.new;
	} else {
		ret = -EOPNOTSUPP;
		goto error;
	}

	ret = iwl_trans_send_cmd(trans, &hcmd);
	if (ret)
		goto error;

	return iwl_pcie_txq_alloc_response(trans, txq, &hcmd);

error:
	iwl_txq_gen2_free_memory(trans, txq);
	return ret;
}

void iwl_txq_dyn_free(struct iwl_trans *trans, int queue)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);

	if (WARN(queue >= IWL_MAX_TVQM_QUEUES,
		 "queue %d out of range", queue))
		return;

	/*
	 * Upon HW Rfkill - we stop the device, and then stop the queues
	 * in the op_mode. Just for the sake of the simplicity of the op_mode,
	 * allow the op_mode to call txq_disable after it already called
	 * stop_device.
	 */
	if (!test_and_clear_bit(queue, trans_pcie->txqs.queue_used)) {
		WARN_ONCE(test_bit(STATUS_DEVICE_ENABLED, &trans->status),
			  "queue %d not used", queue);
		return;
	}

	iwl_txq_gen2_free(trans, queue);

	IWL_DEBUG_TX_QUEUES(trans, "Deactivate queue %d\n", queue);
}

void iwl_txq_gen2_tx_free(struct iwl_trans *trans)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
	int i;

	memset(trans_pcie->txqs.queue_used, 0,
	       sizeof(trans_pcie->txqs.queue_used));

	/* Free all TX queues */
	for (i = 0; i < ARRAY_SIZE(trans_pcie->txqs.txq); i++) {
		if (!trans_pcie->txqs.txq[i])
			continue;

		iwl_txq_gen2_free(trans, i);
	}
}

int iwl_txq_gen2_init(struct iwl_trans *trans, int txq_id, int queue_size)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
	struct iwl_txq *queue;
	int ret;

	/* alloc and init the tx queue */
	if (!trans_pcie->txqs.txq[txq_id]) {
		queue = kzalloc(sizeof(*queue), GFP_KERNEL);
		if (!queue) {
			IWL_ERR(trans, "Not enough memory for tx queue\n");
			return -ENOMEM;
		}
		trans_pcie->txqs.txq[txq_id] = queue;
		ret = iwl_pcie_txq_alloc(trans, queue, queue_size, true);
		if (ret) {
			IWL_ERR(trans, "Tx %d queue init failed\n", txq_id);
			goto error;
		}
	} else {
		queue = trans_pcie->txqs.txq[txq_id];
	}

	ret = iwl_txq_init(trans, queue, queue_size,
			   (txq_id == trans->conf.cmd_queue));
	if (ret) {
		IWL_ERR(trans, "Tx %d queue alloc failed\n", txq_id);
		goto error;
	}
	trans_pcie->txqs.txq[txq_id]->id = txq_id;
	set_bit(txq_id, trans_pcie->txqs.queue_used);

	return 0;

error:
	iwl_txq_gen2_tx_free(trans);
	return ret;
}

/*************** HOST COMMAND QUEUE FUNCTIONS   *****/

/*
 * iwl_pcie_gen2_enqueue_hcmd - enqueue a uCode command
 * @priv: device private data point
 * @cmd: a pointer to the ucode command structure
 *
 * The function returns < 0 values to indicate the operation
 * failed. On success, it returns the index (>= 0) of command in the
 * command queue.
 */
int iwl_pcie_gen2_enqueue_hcmd(struct iwl_trans *trans,
			       struct iwl_host_cmd *cmd)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
	struct iwl_txq *txq = trans_pcie->txqs.txq[trans->conf.cmd_queue];
	struct iwl_device_cmd *out_cmd;
	struct iwl_cmd_meta *out_meta;
	void *dup_buf = NULL;
	dma_addr_t phys_addr;
	int i, cmd_pos, idx;
	u16 copy_size, cmd_size, tb0_size;
	bool had_nocopy = false;
	u8 group_id = iwl_cmd_groupid(cmd->id);
	const u8 *cmddata[IWL_MAX_CMD_TBS_PER_TFD];
	u16 cmdlen[IWL_MAX_CMD_TBS_PER_TFD];
	struct iwl_tfh_tfd *tfd;
	unsigned long flags;

	if (WARN_ON(cmd->flags & CMD_BLOCK_TXQS))
		return -EINVAL;

	copy_size = sizeof(struct iwl_cmd_header_wide);
	cmd_size = sizeof(struct iwl_cmd_header_wide);

	for (i = 0; i < IWL_MAX_CMD_TBS_PER_TFD; i++) {
		cmddata[i] = cmd->data[i];
		cmdlen[i] = cmd->len[i];

		if (!cmd->len[i])
			continue;

		/* need at least IWL_FIRST_TB_SIZE copied */
		if (copy_size < IWL_FIRST_TB_SIZE) {
			int copy = IWL_FIRST_TB_SIZE - copy_size;

			if (copy > cmdlen[i])
				copy = cmdlen[i];
			cmdlen[i] -= copy;
			cmddata[i] += copy;
			copy_size += copy;
		}

		if (cmd->dataflags[i] & IWL_HCMD_DFL_NOCOPY) {
			had_nocopy = true;
			if (WARN_ON(cmd->dataflags[i] & IWL_HCMD_DFL_DUP)) {
				idx = -EINVAL;
				goto free_dup_buf;
			}
		} else if (cmd->dataflags[i] & IWL_HCMD_DFL_DUP) {
			/*
			 * This is also a chunk that isn't copied
			 * to the static buffer so set had_nocopy.
			 */
			had_nocopy = true;

			/* only allowed once */
			if (WARN_ON(dup_buf)) {
				idx = -EINVAL;
				goto free_dup_buf;
			}

			dup_buf = kmemdup(cmddata[i], cmdlen[i],
					  GFP_ATOMIC);
			if (!dup_buf)
				return -ENOMEM;
		} else {
			/* NOCOPY must not be followed by normal! */
			if (WARN_ON(had_nocopy)) {
				idx = -EINVAL;
				goto free_dup_buf;
			}
			copy_size += cmdlen[i];
		}
		cmd_size += cmd->len[i];
	}

	/*
	 * If any of the command structures end up being larger than the
	 * TFD_MAX_PAYLOAD_SIZE and they aren't dynamically allocated into
	 * separate TFDs, then we will need to increase the size of the buffers
	 */
	if (WARN(copy_size > TFD_MAX_PAYLOAD_SIZE,
		 "Command %s (%#x) is too large (%d bytes)\n",
		 iwl_get_cmd_string(trans, cmd->id), cmd->id, copy_size)) {
		idx = -EINVAL;
		goto free_dup_buf;
	}

	spin_lock_irqsave(&txq->lock, flags);

	idx = iwl_txq_get_cmd_index(txq, txq->write_ptr);
	tfd = iwl_txq_get_tfd(trans, txq, txq->write_ptr);
	memset(tfd, 0, sizeof(*tfd));

	if (iwl_txq_space(trans, txq) < ((cmd->flags & CMD_ASYNC) ? 2 : 1)) {
		spin_unlock_irqrestore(&txq->lock, flags);

		IWL_ERR(trans, "No space in command queue\n");
		iwl_op_mode_nic_error(trans->op_mode,
				      IWL_ERR_TYPE_CMD_QUEUE_FULL);
		iwl_trans_schedule_reset(trans, IWL_ERR_TYPE_CMD_QUEUE_FULL);
		idx = -ENOSPC;
		goto free_dup_buf;
	}

	out_cmd = txq->entries[idx].cmd;
	out_meta = &txq->entries[idx].meta;

	/* re-initialize, this also marks the SG list as unused */
	memset(out_meta, 0, sizeof(*out_meta));
	if (cmd->flags & CMD_WANT_SKB)
		out_meta->source = cmd;

	/* set up the header */
	out_cmd->hdr_wide.cmd = iwl_cmd_opcode(cmd->id);
	out_cmd->hdr_wide.group_id = group_id;
	out_cmd->hdr_wide.version = iwl_cmd_version(cmd->id);
	out_cmd->hdr_wide.length =
		cpu_to_le16(cmd_size - sizeof(struct iwl_cmd_header_wide));
	out_cmd->hdr_wide.reserved = 0;
	out_cmd->hdr_wide.sequence =
		cpu_to_le16(QUEUE_TO_SEQ(trans->conf.cmd_queue) |
					 INDEX_TO_SEQ(txq->write_ptr));

	cmd_pos = sizeof(struct iwl_cmd_header_wide);
	copy_size = sizeof(struct iwl_cmd_header_wide);

	/* and copy the data that needs to be copied */
	for (i = 0; i < IWL_MAX_CMD_TBS_PER_TFD; i++) {
		int copy;

		if (!cmd->len[i])
			continue;

		/* copy everything if not nocopy/dup */
		if (!(cmd->dataflags[i] & (IWL_HCMD_DFL_NOCOPY |
					   IWL_HCMD_DFL_DUP))) {
			copy = cmd->len[i];

			memcpy((u8 *)out_cmd + cmd_pos, cmd->data[i], copy);
			cmd_pos += copy;
			copy_size += copy;
			continue;
		}

		/*
		 * Otherwise we need at least IWL_FIRST_TB_SIZE copied
		 * in total (for bi-directional DMA), but copy up to what
		 * we can fit into the payload for debug dump purposes.
		 */
		copy = min_t(int, TFD_MAX_PAYLOAD_SIZE - cmd_pos, cmd->len[i]);

		memcpy((u8 *)out_cmd + cmd_pos, cmd->data[i], copy);
		cmd_pos += copy;

		/* However, treat copy_size the proper way, we need it below */
		if (copy_size < IWL_FIRST_TB_SIZE) {
			copy = IWL_FIRST_TB_SIZE - copy_size;

			if (copy > cmd->len[i])
				copy = cmd->len[i];
			copy_size += copy;
		}
	}

	IWL_DEBUG_HC(trans,
		     "Sending command %s (%.2x.%.2x), seq: 0x%04X, %d bytes at %d[%d]:%d\n",
		     iwl_get_cmd_string(trans, cmd->id), group_id,
		     out_cmd->hdr.cmd, le16_to_cpu(out_cmd->hdr.sequence),
		     cmd_size, txq->write_ptr, idx, trans->conf.cmd_queue);

	/* start the TFD with the minimum copy bytes */
	tb0_size = min_t(int, copy_size, IWL_FIRST_TB_SIZE);
	memcpy(&txq->first_tb_bufs[idx], out_cmd, tb0_size);
	iwl_txq_gen2_set_tb(trans, tfd, iwl_txq_get_first_tb_dma(txq, idx),
			    tb0_size);

	/* map first command fragment, if any remains */
	if (copy_size > tb0_size) {
		phys_addr = dma_map_single(trans->dev,
					   (u8 *)out_cmd + tb0_size,
					   copy_size - tb0_size,
					   DMA_TO_DEVICE);
		if (dma_mapping_error(trans->dev, phys_addr)) {
			idx = -ENOMEM;
			iwl_txq_gen2_tfd_unmap(trans, out_meta, tfd);
			goto out;
		}
		iwl_txq_gen2_set_tb(trans, tfd, phys_addr,
				    copy_size - tb0_size);
	}

	/* map the remaining (adjusted) nocopy/dup fragments */
	for (i = 0; i < IWL_MAX_CMD_TBS_PER_TFD; i++) {
		void *data = (void *)(uintptr_t)cmddata[i];

		if (!cmdlen[i])
			continue;
		if (!(cmd->dataflags[i] & (IWL_HCMD_DFL_NOCOPY |
					   IWL_HCMD_DFL_DUP)))
			continue;
		if (cmd->dataflags[i] & IWL_HCMD_DFL_DUP)
			data = dup_buf;
		phys_addr = dma_map_single(trans->dev, data,
					   cmdlen[i], DMA_TO_DEVICE);
		if (dma_mapping_error(trans->dev, phys_addr)) {
			idx = -ENOMEM;
			iwl_txq_gen2_tfd_unmap(trans, out_meta, tfd);
			goto out;
		}
		iwl_txq_gen2_set_tb(trans, tfd, phys_addr, cmdlen[i]);
	}

	BUILD_BUG_ON(IWL_TFH_NUM_TBS > sizeof(out_meta->tbs) * BITS_PER_BYTE);
	out_meta->flags = cmd->flags;
	if (WARN_ON_ONCE(txq->entries[idx].free_buf))
		kfree_sensitive(txq->entries[idx].free_buf);
	txq->entries[idx].free_buf = dup_buf;

	trace_iwlwifi_dev_hcmd(trans->dev, cmd, cmd_size, &out_cmd->hdr_wide);

	/* start timer if queue currently empty */
	if (txq->read_ptr == txq->write_ptr && txq->wd_timeout)
		mod_timer(&txq->stuck_timer, jiffies + txq->wd_timeout);

	spin_lock(&trans_pcie->reg_lock);
	/* Increment and update queue's write index */
	txq->write_ptr = iwl_txq_inc_wrap(trans, txq->write_ptr);
	iwl_txq_inc_wr_ptr(trans, txq);
	spin_unlock(&trans_pcie->reg_lock);

out:
	spin_unlock_irqrestore(&txq->lock, flags);
free_dup_buf:
	if (idx < 0)
		kfree(dup_buf);
	return idx;
}
