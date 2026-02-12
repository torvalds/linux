// SPDX-License-Identifier: BSD-3-Clause-Clear
/*
 * Copyright (c) 2018-2021 The Linux Foundation. All rights reserved.
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <crypto/hash.h>
#include "core.h"
#include "dp_tx.h"
#include "hif.h"
#include "hal.h"
#include "debug.h"
#include "peer.h"
#include "dp_cmn.h"

enum ath12k_dp_desc_type {
	ATH12K_DP_TX_DESC,
	ATH12K_DP_RX_DESC,
};

void ath12k_dp_peer_cleanup(struct ath12k *ar, int vdev_id, const u8 *addr)
{
	struct ath12k_base *ab = ar->ab;
	struct ath12k_dp_link_peer *peer;
	struct ath12k_dp *dp = ath12k_ab_to_dp(ab);

	/* TODO: Any other peer specific DP cleanup */

	spin_lock_bh(&dp->dp_lock);
	peer = ath12k_dp_link_peer_find_by_vdev_and_addr(dp, vdev_id, addr);
	if (!peer || !peer->dp_peer) {
		ath12k_warn(ab, "failed to lookup peer %pM on vdev %d\n",
			    addr, vdev_id);
		spin_unlock_bh(&dp->dp_lock);
		return;
	}

	if (!peer->primary_link) {
		spin_unlock_bh(&dp->dp_lock);
		return;
	}

	ath12k_dp_rx_peer_tid_cleanup(ar, peer);
	crypto_free_shash(peer->dp_peer->tfm_mmic);
	peer->dp_peer->dp_setup_done = false;
	spin_unlock_bh(&dp->dp_lock);
}

int ath12k_dp_peer_setup(struct ath12k *ar, int vdev_id, const u8 *addr)
{
	struct ath12k_base *ab = ar->ab;
	struct ath12k_dp_link_peer *peer;
	u32 reo_dest;
	int ret = 0, tid;
	struct ath12k_dp *dp = ath12k_ab_to_dp(ab);

	/* NOTE: reo_dest ring id starts from 1 unlike mac_id which starts from 0 */
	reo_dest = ar->dp.mac_id + 1;
	ret = ath12k_wmi_set_peer_param(ar, addr, vdev_id,
					WMI_PEER_SET_DEFAULT_ROUTING,
					DP_RX_HASH_ENABLE | (reo_dest << 1));

	if (ret) {
		ath12k_warn(ab, "failed to set default routing %d peer :%pM vdev_id :%d\n",
			    ret, addr, vdev_id);
		return ret;
	}

	for (tid = 0; tid <= IEEE80211_NUM_TIDS; tid++) {
		ret = ath12k_dp_rx_peer_tid_setup(ar, addr, vdev_id, tid, 1, 0,
						  HAL_PN_TYPE_NONE);
		if (ret) {
			ath12k_warn(ab, "failed to setup rxd tid queue for tid %d: %d\n",
				    tid, ret);
			goto peer_clean;
		}
	}

	ret = ath12k_dp_rx_peer_frag_setup(ar, addr, vdev_id);
	if (ret) {
		ath12k_warn(ab, "failed to setup rx defrag context\n");
		goto peer_clean;
	}

	/* TODO: Setup other peer specific resource used in data path */

	return 0;

peer_clean:
	spin_lock_bh(&dp->dp_lock);

	peer = ath12k_dp_link_peer_find_by_vdev_and_addr(dp, vdev_id, addr);
	if (!peer) {
		ath12k_warn(ab, "failed to find the peer to del rx tid\n");
		spin_unlock_bh(&dp->dp_lock);
		return -ENOENT;
	}

	for (tid--; tid >= 0; tid--)
		ath12k_dp_arch_rx_peer_tid_delete(dp, peer, tid);

	spin_unlock_bh(&dp->dp_lock);

	return ret;
}

void ath12k_dp_srng_cleanup(struct ath12k_base *ab, struct dp_srng *ring)
{
	if (!ring->vaddr_unaligned)
		return;

	dma_free_coherent(ab->dev, ring->size, ring->vaddr_unaligned,
			  ring->paddr_unaligned);

	ring->vaddr_unaligned = NULL;
}

static int ath12k_dp_srng_find_ring_in_mask(int ring_num, const u8 *grp_mask)
{
	int ext_group_num;
	u8 mask = 1 << ring_num;

	for (ext_group_num = 0; ext_group_num < ATH12K_EXT_IRQ_GRP_NUM_MAX;
	     ext_group_num++) {
		if (mask & grp_mask[ext_group_num])
			return ext_group_num;
	}

	return -ENOENT;
}

static int ath12k_dp_srng_calculate_msi_group(struct ath12k_base *ab,
					      enum hal_ring_type type, int ring_num)
{
	const struct ath12k_hal_tcl_to_wbm_rbm_map *map;
	const u8 *grp_mask;
	int i;

	switch (type) {
	case HAL_WBM2SW_RELEASE:
		if (ring_num == HAL_WBM2SW_REL_ERR_RING_NUM) {
			grp_mask = &ab->hw_params->ring_mask->rx_wbm_rel[0];
			ring_num = 0;
		} else {
			map = ab->hal.tcl_to_wbm_rbm_map;
			for (i = 0; i < ab->hw_params->max_tx_ring; i++) {
				if (ring_num == map[i].wbm_ring_num) {
					ring_num = i;
					break;
				}
			}

			grp_mask = &ab->hw_params->ring_mask->tx[0];
		}
		break;
	case HAL_REO_EXCEPTION:
		grp_mask = &ab->hw_params->ring_mask->rx_err[0];
		break;
	case HAL_REO_DST:
		grp_mask = &ab->hw_params->ring_mask->rx[0];
		break;
	case HAL_REO_STATUS:
		grp_mask = &ab->hw_params->ring_mask->reo_status[0];
		break;
	case HAL_RXDMA_MONITOR_STATUS:
		grp_mask = &ab->hw_params->ring_mask->rx_mon_status[0];
		break;
	case HAL_RXDMA_MONITOR_DST:
		grp_mask = &ab->hw_params->ring_mask->rx_mon_dest[0];
		break;
	case HAL_TX_MONITOR_DST:
		grp_mask = &ab->hw_params->ring_mask->tx_mon_dest[0];
		break;
	case HAL_RXDMA_BUF:
		grp_mask = &ab->hw_params->ring_mask->host2rxdma[0];
		break;
	case HAL_RXDMA_MONITOR_BUF:
	case HAL_TCL_DATA:
	case HAL_TCL_CMD:
	case HAL_REO_CMD:
	case HAL_SW2WBM_RELEASE:
	case HAL_WBM_IDLE_LINK:
	case HAL_TCL_STATUS:
	case HAL_REO_REINJECT:
	case HAL_CE_SRC:
	case HAL_CE_DST:
	case HAL_CE_DST_STATUS:
	default:
		return -ENOENT;
	}

	return ath12k_dp_srng_find_ring_in_mask(ring_num, grp_mask);
}

static void ath12k_dp_srng_msi_setup(struct ath12k_base *ab,
				     struct hal_srng_params *ring_params,
				     enum hal_ring_type type, int ring_num)
{
	int msi_group_number, msi_data_count;
	u32 msi_data_start, msi_irq_start, addr_lo, addr_hi;
	int ret;

	ret = ath12k_hif_get_user_msi_vector(ab, "DP",
					     &msi_data_count, &msi_data_start,
					     &msi_irq_start);
	if (ret)
		return;

	msi_group_number = ath12k_dp_srng_calculate_msi_group(ab, type,
							      ring_num);
	if (msi_group_number < 0) {
		ath12k_dbg(ab, ATH12K_DBG_PCI,
			   "ring not part of an ext_group; ring_type: %d,ring_num %d",
			   type, ring_num);
		ring_params->msi_addr = 0;
		ring_params->msi_data = 0;
		return;
	}

	if (msi_group_number > msi_data_count) {
		ath12k_dbg(ab, ATH12K_DBG_PCI,
			   "multiple msi_groups share one msi, msi_group_num %d",
			   msi_group_number);
	}

	ath12k_hif_get_msi_address(ab, &addr_lo, &addr_hi);

	ring_params->msi_addr = addr_lo;
	ring_params->msi_addr |= (dma_addr_t)(((uint64_t)addr_hi) << 32);
	ring_params->msi_data = (msi_group_number % msi_data_count)
		+ msi_data_start;
	ring_params->flags |= HAL_SRNG_FLAGS_MSI_INTR;
}

int ath12k_dp_srng_setup(struct ath12k_base *ab, struct dp_srng *ring,
			 enum hal_ring_type type, int ring_num,
			 int mac_id, int num_entries)
{
	struct hal_srng_params params = {};
	int entry_sz = ath12k_hal_srng_get_entrysize(ab, type);
	int max_entries = ath12k_hal_srng_get_max_entries(ab, type);
	int ret;

	if (max_entries < 0 || entry_sz < 0)
		return -EINVAL;

	if (num_entries > max_entries)
		num_entries = max_entries;

	ring->size = (num_entries * entry_sz) + HAL_RING_BASE_ALIGN - 1;
	ring->vaddr_unaligned = dma_alloc_coherent(ab->dev, ring->size,
						   &ring->paddr_unaligned,
						   GFP_KERNEL);
	if (!ring->vaddr_unaligned)
		return -ENOMEM;

	ring->vaddr = PTR_ALIGN(ring->vaddr_unaligned, HAL_RING_BASE_ALIGN);
	ring->paddr = ring->paddr_unaligned + ((unsigned long)ring->vaddr -
		      (unsigned long)ring->vaddr_unaligned);

	params.ring_base_vaddr = ring->vaddr;
	params.ring_base_paddr = ring->paddr;
	params.num_entries = num_entries;
	ath12k_dp_srng_msi_setup(ab, &params, type, ring_num + mac_id);

	switch (type) {
	case HAL_REO_DST:
		params.intr_batch_cntr_thres_entries =
					HAL_SRNG_INT_BATCH_THRESHOLD_RX;
		params.intr_timer_thres_us = HAL_SRNG_INT_TIMER_THRESHOLD_RX;
		break;
	case HAL_RXDMA_BUF:
	case HAL_RXDMA_MONITOR_BUF:
		params.low_threshold = num_entries >> 3;
		params.flags |= HAL_SRNG_FLAGS_LOW_THRESH_INTR_EN;
		params.intr_batch_cntr_thres_entries = 0;
		params.intr_timer_thres_us = HAL_SRNG_INT_TIMER_THRESHOLD_RX;
		break;
	case HAL_RXDMA_MONITOR_STATUS:
		params.low_threshold = num_entries >> 3;
		params.flags |= HAL_SRNG_FLAGS_LOW_THRESH_INTR_EN;
		params.intr_batch_cntr_thres_entries = 1;
		params.intr_timer_thres_us = HAL_SRNG_INT_TIMER_THRESHOLD_RX;
		break;
	case HAL_TX_MONITOR_DST:
		params.low_threshold = DP_TX_MONITOR_BUF_SIZE_MAX >> 3;
		params.flags |= HAL_SRNG_FLAGS_LOW_THRESH_INTR_EN;
		params.intr_batch_cntr_thres_entries = 0;
		params.intr_timer_thres_us = HAL_SRNG_INT_TIMER_THRESHOLD_RX;
		break;
	case HAL_WBM2SW_RELEASE:
		if (ab->hw_params->hw_ops->dp_srng_is_tx_comp_ring(ring_num)) {
			params.intr_batch_cntr_thres_entries =
					HAL_SRNG_INT_BATCH_THRESHOLD_TX;
			params.intr_timer_thres_us =
					HAL_SRNG_INT_TIMER_THRESHOLD_TX;
			break;
		}
		/* follow through when ring_num != HAL_WBM2SW_REL_ERR_RING_NUM */
		fallthrough;
	case HAL_REO_EXCEPTION:
	case HAL_REO_REINJECT:
	case HAL_REO_CMD:
	case HAL_REO_STATUS:
	case HAL_TCL_DATA:
	case HAL_TCL_CMD:
	case HAL_TCL_STATUS:
	case HAL_WBM_IDLE_LINK:
	case HAL_SW2WBM_RELEASE:
	case HAL_RXDMA_DST:
	case HAL_RXDMA_MONITOR_DST:
	case HAL_RXDMA_MONITOR_DESC:
		params.intr_batch_cntr_thres_entries =
					HAL_SRNG_INT_BATCH_THRESHOLD_OTHER;
		params.intr_timer_thres_us = HAL_SRNG_INT_TIMER_THRESHOLD_OTHER;
		break;
	case HAL_RXDMA_DIR_BUF:
		break;
	default:
		ath12k_warn(ab, "Not a valid ring type in dp :%d\n", type);
		return -EINVAL;
	}

	ret = ath12k_hal_srng_setup(ab, type, ring_num, mac_id, &params);
	if (ret < 0) {
		ath12k_warn(ab, "failed to setup srng: %d ring_id %d\n",
			    ret, ring_num);
		return ret;
	}

	ring->ring_id = ret;

	return 0;
}

static int ath12k_dp_tx_get_bank_profile(struct ath12k_base *ab,
					 struct ath12k_link_vif *arvif,
					 struct ath12k_dp *dp)
{
	int bank_id = DP_INVALID_BANK_ID;
	int i;
	u32 bank_config;
	bool configure_register = false;

	/* convert vdev params into hal_tx_bank_config */
	bank_config = ath12k_dp_arch_tx_get_vdev_bank_config(dp, arvif);

	spin_lock_bh(&dp->tx_bank_lock);
	/* TODO: implement using idr kernel framework*/
	for (i = 0; i < dp->num_bank_profiles; i++) {
		if (dp->bank_profiles[i].is_configured &&
		    (dp->bank_profiles[i].bank_config ^ bank_config) == 0) {
			bank_id = i;
			goto inc_ref_and_return;
		}
		if (!dp->bank_profiles[i].is_configured ||
		    !dp->bank_profiles[i].num_users) {
			bank_id = i;
			goto configure_and_return;
		}
	}

	if (bank_id == DP_INVALID_BANK_ID) {
		spin_unlock_bh(&dp->tx_bank_lock);
		ath12k_err(ab, "unable to find TX bank!");
		return bank_id;
	}

configure_and_return:
	dp->bank_profiles[bank_id].is_configured = true;
	dp->bank_profiles[bank_id].bank_config = bank_config;
	configure_register = true;
inc_ref_and_return:
	dp->bank_profiles[bank_id].num_users++;
	spin_unlock_bh(&dp->tx_bank_lock);

	if (configure_register)
		ath12k_hal_tx_configure_bank_register(ab,
						      bank_config, bank_id);

	ath12k_dbg(ab, ATH12K_DBG_DP_HTT, "dp_htt tcl bank_id %d input 0x%x match 0x%x num_users %u",
		   bank_id, bank_config, dp->bank_profiles[bank_id].bank_config,
		   dp->bank_profiles[bank_id].num_users);

	return bank_id;
}

void ath12k_dp_tx_put_bank_profile(struct ath12k_dp *dp, u8 bank_id)
{
	spin_lock_bh(&dp->tx_bank_lock);
	dp->bank_profiles[bank_id].num_users--;
	spin_unlock_bh(&dp->tx_bank_lock);
}

static void ath12k_dp_deinit_bank_profiles(struct ath12k_base *ab)
{
	struct ath12k_dp *dp = ath12k_ab_to_dp(ab);

	kfree(dp->bank_profiles);
	dp->bank_profiles = NULL;
}

static int ath12k_dp_init_bank_profiles(struct ath12k_base *ab)
{
	struct ath12k_dp *dp = ath12k_ab_to_dp(ab);
	u32 num_tcl_banks = ab->hw_params->num_tcl_banks;
	int i;

	dp->num_bank_profiles = num_tcl_banks;
	dp->bank_profiles = kmalloc_array(num_tcl_banks,
					  sizeof(struct ath12k_dp_tx_bank_profile),
					  GFP_KERNEL);
	if (!dp->bank_profiles)
		return -ENOMEM;

	spin_lock_init(&dp->tx_bank_lock);

	for (i = 0; i < num_tcl_banks; i++) {
		dp->bank_profiles[i].is_configured = false;
		dp->bank_profiles[i].num_users = 0;
	}

	return 0;
}

static void ath12k_dp_srng_common_cleanup(struct ath12k_base *ab)
{
	struct ath12k_dp *dp = ath12k_ab_to_dp(ab);
	int i;

	ath12k_dp_srng_cleanup(ab, &dp->reo_status_ring);
	ath12k_dp_srng_cleanup(ab, &dp->reo_cmd_ring);
	ath12k_dp_srng_cleanup(ab, &dp->reo_except_ring);
	ath12k_dp_srng_cleanup(ab, &dp->rx_rel_ring);
	ath12k_dp_srng_cleanup(ab, &dp->reo_reinject_ring);
	for (i = 0; i < ab->hw_params->max_tx_ring; i++) {
		ath12k_dp_srng_cleanup(ab, &dp->tx_ring[i].tcl_comp_ring);
		ath12k_dp_srng_cleanup(ab, &dp->tx_ring[i].tcl_data_ring);
	}
	ath12k_dp_srng_cleanup(ab, &dp->wbm_desc_rel_ring);
}

static int ath12k_dp_srng_common_setup(struct ath12k_base *ab)
{
	struct ath12k_dp *dp = ath12k_ab_to_dp(ab);
	const struct ath12k_hal_tcl_to_wbm_rbm_map *map;
	struct hal_srng *srng;
	int i, ret, tx_comp_ring_num;
	u32 ring_hash_map;

	ret = ath12k_dp_srng_setup(ab, &dp->wbm_desc_rel_ring,
				   HAL_SW2WBM_RELEASE, 0, 0,
				   DP_WBM_RELEASE_RING_SIZE);
	if (ret) {
		ath12k_warn(ab, "failed to set up wbm2sw_release ring :%d\n",
			    ret);
		goto err;
	}

	for (i = 0; i < ab->hw_params->max_tx_ring; i++) {
		map = ab->hal.tcl_to_wbm_rbm_map;
		tx_comp_ring_num = map[i].wbm_ring_num;

		ret = ath12k_dp_srng_setup(ab, &dp->tx_ring[i].tcl_data_ring,
					   HAL_TCL_DATA, i, 0,
					   DP_TCL_DATA_RING_SIZE);
		if (ret) {
			ath12k_warn(ab, "failed to set up tcl_data ring (%d) :%d\n",
				    i, ret);
			goto err;
		}

		ret = ath12k_dp_srng_setup(ab, &dp->tx_ring[i].tcl_comp_ring,
					   HAL_WBM2SW_RELEASE, tx_comp_ring_num, 0,
					   DP_TX_COMP_RING_SIZE(ab));
		if (ret) {
			ath12k_warn(ab, "failed to set up tcl_comp ring (%d) :%d\n",
				    tx_comp_ring_num, ret);
			goto err;
		}
	}

	ret = ath12k_dp_srng_setup(ab, &dp->reo_reinject_ring, HAL_REO_REINJECT,
				   0, 0, DP_REO_REINJECT_RING_SIZE);
	if (ret) {
		ath12k_warn(ab, "failed to set up reo_reinject ring :%d\n",
			    ret);
		goto err;
	}

	ret = ath12k_dp_srng_setup(ab, &dp->rx_rel_ring, HAL_WBM2SW_RELEASE,
				   HAL_WBM2SW_REL_ERR_RING_NUM, 0,
				   DP_RX_RELEASE_RING_SIZE);
	if (ret) {
		ath12k_warn(ab, "failed to set up rx_rel ring :%d\n", ret);
		goto err;
	}

	ret = ath12k_dp_srng_setup(ab, &dp->reo_except_ring, HAL_REO_EXCEPTION,
				   0, 0, DP_REO_EXCEPTION_RING_SIZE);
	if (ret) {
		ath12k_warn(ab, "failed to set up reo_exception ring :%d\n",
			    ret);
		goto err;
	}

	ret = ath12k_dp_srng_setup(ab, &dp->reo_cmd_ring, HAL_REO_CMD,
				   0, 0, DP_REO_CMD_RING_SIZE);
	if (ret) {
		ath12k_warn(ab, "failed to set up reo_cmd ring :%d\n", ret);
		goto err;
	}

	srng = &ab->hal.srng_list[dp->reo_cmd_ring.ring_id];
	ath12k_hal_reo_init_cmd_ring(ab, srng);

	ret = ath12k_dp_srng_setup(ab, &dp->reo_status_ring, HAL_REO_STATUS,
				   0, 0, DP_REO_STATUS_RING_SIZE);
	if (ret) {
		ath12k_warn(ab, "failed to set up reo_status ring :%d\n", ret);
		goto err;
	}

	/* When hash based routing of rx packet is enabled, 32 entries to map
	 * the hash values to the ring will be configured. Each hash entry uses
	 * four bits to map to a particular ring. The ring mapping will be
	 * 0:TCL, 1:SW1, 2:SW2, 3:SW3, 4:SW4, 5:Release, 6:FW and 7:SW5
	 * 8:SW6, 9:SW7, 10:SW8, 11:Not used.
	 */
	ring_hash_map = HAL_HASH_ROUTING_RING_SW1 |
			HAL_HASH_ROUTING_RING_SW2 << 4 |
			HAL_HASH_ROUTING_RING_SW3 << 8 |
			HAL_HASH_ROUTING_RING_SW4 << 12 |
			HAL_HASH_ROUTING_RING_SW1 << 16 |
			HAL_HASH_ROUTING_RING_SW2 << 20 |
			HAL_HASH_ROUTING_RING_SW3 << 24 |
			HAL_HASH_ROUTING_RING_SW4 << 28;

	ath12k_hal_reo_hw_setup(ab, ring_hash_map);

	return 0;

err:
	ath12k_dp_srng_common_cleanup(ab);

	return ret;
}

static void ath12k_dp_scatter_idle_link_desc_cleanup(struct ath12k_base *ab)
{
	struct ath12k_dp *dp = ath12k_ab_to_dp(ab);
	struct hal_wbm_idle_scatter_list *slist = dp->scatter_list;
	int i;

	for (i = 0; i < DP_IDLE_SCATTER_BUFS_MAX; i++) {
		if (!slist[i].vaddr)
			continue;

		dma_free_coherent(ab->dev, HAL_WBM_IDLE_SCATTER_BUF_SIZE_MAX,
				  slist[i].vaddr, slist[i].paddr);
		slist[i].vaddr = NULL;
	}
}

static int ath12k_dp_scatter_idle_link_desc_setup(struct ath12k_base *ab,
						  int size,
						  u32 n_link_desc_bank,
						  u32 n_link_desc,
						  u32 last_bank_sz)
{
	struct ath12k_dp *dp = ath12k_ab_to_dp(ab);
	struct dp_link_desc_bank *link_desc_banks = dp->link_desc_banks;
	struct hal_wbm_idle_scatter_list *slist = dp->scatter_list;
	u32 n_entries_per_buf;
	int num_scatter_buf, scatter_idx;
	struct hal_wbm_link_desc *scatter_buf;
	int align_bytes, n_entries;
	dma_addr_t paddr;
	int rem_entries;
	int i;
	int ret = 0;
	u32 end_offset, cookie;
	enum hal_rx_buf_return_buf_manager rbm = dp->idle_link_rbm;

	n_entries_per_buf = HAL_WBM_IDLE_SCATTER_BUF_SIZE /
		ath12k_hal_srng_get_entrysize(ab, HAL_WBM_IDLE_LINK);
	num_scatter_buf = DIV_ROUND_UP(size, HAL_WBM_IDLE_SCATTER_BUF_SIZE);

	if (num_scatter_buf > DP_IDLE_SCATTER_BUFS_MAX)
		return -EINVAL;

	for (i = 0; i < num_scatter_buf; i++) {
		slist[i].vaddr = dma_alloc_coherent(ab->dev,
						    HAL_WBM_IDLE_SCATTER_BUF_SIZE_MAX,
						    &slist[i].paddr, GFP_KERNEL);
		if (!slist[i].vaddr) {
			ret = -ENOMEM;
			goto err;
		}
	}

	scatter_idx = 0;
	scatter_buf = slist[scatter_idx].vaddr;
	rem_entries = n_entries_per_buf;

	for (i = 0; i < n_link_desc_bank; i++) {
		align_bytes = link_desc_banks[i].vaddr -
			      link_desc_banks[i].vaddr_unaligned;
		n_entries = (DP_LINK_DESC_ALLOC_SIZE_THRESH - align_bytes) /
			     HAL_LINK_DESC_SIZE;
		paddr = link_desc_banks[i].paddr;
		while (n_entries) {
			cookie = DP_LINK_DESC_COOKIE_SET(n_entries, i);
			ath12k_hal_set_link_desc_addr(dp->hal, scatter_buf, cookie,
						      paddr, rbm);
			n_entries--;
			paddr += HAL_LINK_DESC_SIZE;
			if (rem_entries) {
				rem_entries--;
				scatter_buf++;
				continue;
			}

			rem_entries = n_entries_per_buf;
			scatter_idx++;
			scatter_buf = slist[scatter_idx].vaddr;
		}
	}

	end_offset = (scatter_buf - slist[scatter_idx].vaddr) *
		     sizeof(struct hal_wbm_link_desc);
	ath12k_hal_setup_link_idle_list(ab, slist, num_scatter_buf,
					n_link_desc, end_offset);

	return 0;

err:
	ath12k_dp_scatter_idle_link_desc_cleanup(ab);

	return ret;
}

static void
ath12k_dp_link_desc_bank_free(struct ath12k_base *ab,
			      struct dp_link_desc_bank *link_desc_banks)
{
	int i;

	for (i = 0; i < DP_LINK_DESC_BANKS_MAX; i++) {
		if (link_desc_banks[i].vaddr_unaligned) {
			dma_free_coherent(ab->dev,
					  link_desc_banks[i].size,
					  link_desc_banks[i].vaddr_unaligned,
					  link_desc_banks[i].paddr_unaligned);
			link_desc_banks[i].vaddr_unaligned = NULL;
		}
	}
}

static int ath12k_dp_link_desc_bank_alloc(struct ath12k_base *ab,
					  struct dp_link_desc_bank *desc_bank,
					  int n_link_desc_bank,
					  int last_bank_sz)
{
	struct ath12k_dp *dp = ath12k_ab_to_dp(ab);
	int i;
	int ret = 0;
	int desc_sz = DP_LINK_DESC_ALLOC_SIZE_THRESH;

	for (i = 0; i < n_link_desc_bank; i++) {
		if (i == (n_link_desc_bank - 1) && last_bank_sz)
			desc_sz = last_bank_sz;

		desc_bank[i].vaddr_unaligned =
					dma_alloc_coherent(ab->dev, desc_sz,
							   &desc_bank[i].paddr_unaligned,
							   GFP_KERNEL);
		if (!desc_bank[i].vaddr_unaligned) {
			ret = -ENOMEM;
			goto err;
		}

		desc_bank[i].vaddr = PTR_ALIGN(desc_bank[i].vaddr_unaligned,
					       HAL_LINK_DESC_ALIGN);
		desc_bank[i].paddr = desc_bank[i].paddr_unaligned +
				     ((unsigned long)desc_bank[i].vaddr -
				      (unsigned long)desc_bank[i].vaddr_unaligned);
		desc_bank[i].size = desc_sz;
	}

	return 0;

err:
	ath12k_dp_link_desc_bank_free(ab, dp->link_desc_banks);

	return ret;
}

void ath12k_dp_link_desc_cleanup(struct ath12k_base *ab,
				 struct dp_link_desc_bank *desc_bank,
				 u32 ring_type, struct dp_srng *ring)
{
	ath12k_dp_link_desc_bank_free(ab, desc_bank);

	if (ring_type != HAL_RXDMA_MONITOR_DESC) {
		ath12k_dp_srng_cleanup(ab, ring);
		ath12k_dp_scatter_idle_link_desc_cleanup(ab);
	}
}

static int ath12k_wbm_idle_ring_setup(struct ath12k_base *ab, u32 *n_link_desc)
{
	struct ath12k_dp *dp = ath12k_ab_to_dp(ab);
	u32 n_mpdu_link_desc, n_mpdu_queue_desc;
	u32 n_tx_msdu_link_desc, n_rx_msdu_link_desc;
	int ret = 0;

	n_mpdu_link_desc = (DP_NUM_TIDS_MAX * DP_AVG_MPDUS_PER_TID_MAX) /
			   HAL_NUM_MPDUS_PER_LINK_DESC;

	n_mpdu_queue_desc = n_mpdu_link_desc /
			    HAL_NUM_MPDU_LINKS_PER_QUEUE_DESC;

	n_tx_msdu_link_desc = (DP_NUM_TIDS_MAX * DP_AVG_FLOWS_PER_TID *
			       DP_AVG_MSDUS_PER_FLOW) /
			      HAL_NUM_TX_MSDUS_PER_LINK_DESC;

	n_rx_msdu_link_desc = (DP_NUM_TIDS_MAX * DP_AVG_MPDUS_PER_TID_MAX *
			       DP_AVG_MSDUS_PER_MPDU) /
			      HAL_NUM_RX_MSDUS_PER_LINK_DESC;

	*n_link_desc = n_mpdu_link_desc + n_mpdu_queue_desc +
		      n_tx_msdu_link_desc + n_rx_msdu_link_desc;

	if (*n_link_desc & (*n_link_desc - 1))
		*n_link_desc = 1 << fls(*n_link_desc);

	ret = ath12k_dp_srng_setup(ab, &dp->wbm_idle_ring,
				   HAL_WBM_IDLE_LINK, 0, 0, *n_link_desc);
	if (ret) {
		ath12k_warn(ab, "failed to setup wbm_idle_ring: %d\n", ret);
		return ret;
	}
	return ret;
}

int ath12k_dp_link_desc_setup(struct ath12k_base *ab,
			      struct dp_link_desc_bank *link_desc_banks,
			      u32 ring_type, struct hal_srng *srng,
			      u32 n_link_desc)
{
	struct ath12k_dp *dp = ath12k_ab_to_dp(ab);
	u32 tot_mem_sz;
	u32 n_link_desc_bank, last_bank_sz;
	u32 entry_sz, align_bytes, n_entries;
	struct hal_wbm_link_desc *desc;
	u32 paddr;
	int i, ret;
	u32 cookie;
	enum hal_rx_buf_return_buf_manager rbm = dp->idle_link_rbm;

	tot_mem_sz = n_link_desc * HAL_LINK_DESC_SIZE;
	tot_mem_sz += HAL_LINK_DESC_ALIGN;

	if (tot_mem_sz <= DP_LINK_DESC_ALLOC_SIZE_THRESH) {
		n_link_desc_bank = 1;
		last_bank_sz = tot_mem_sz;
	} else {
		n_link_desc_bank = tot_mem_sz /
				   (DP_LINK_DESC_ALLOC_SIZE_THRESH -
				    HAL_LINK_DESC_ALIGN);
		last_bank_sz = tot_mem_sz %
			       (DP_LINK_DESC_ALLOC_SIZE_THRESH -
				HAL_LINK_DESC_ALIGN);

		if (last_bank_sz)
			n_link_desc_bank += 1;
	}

	if (n_link_desc_bank > DP_LINK_DESC_BANKS_MAX)
		return -EINVAL;

	ret = ath12k_dp_link_desc_bank_alloc(ab, link_desc_banks,
					     n_link_desc_bank, last_bank_sz);
	if (ret)
		return ret;

	/* Setup link desc idle list for HW internal usage */
	entry_sz = ath12k_hal_srng_get_entrysize(ab, ring_type);
	tot_mem_sz = entry_sz * n_link_desc;

	/* Setup scatter desc list when the total memory requirement is more */
	if (tot_mem_sz > DP_LINK_DESC_ALLOC_SIZE_THRESH &&
	    ring_type != HAL_RXDMA_MONITOR_DESC) {
		ret = ath12k_dp_scatter_idle_link_desc_setup(ab, tot_mem_sz,
							     n_link_desc_bank,
							     n_link_desc,
							     last_bank_sz);
		if (ret) {
			ath12k_warn(ab, "failed to setup scatting idle list descriptor :%d\n",
				    ret);
			goto fail_desc_bank_free;
		}

		return 0;
	}

	spin_lock_bh(&srng->lock);

	ath12k_hal_srng_access_begin(ab, srng);

	for (i = 0; i < n_link_desc_bank; i++) {
		align_bytes = link_desc_banks[i].vaddr -
			      link_desc_banks[i].vaddr_unaligned;
		n_entries = (link_desc_banks[i].size - align_bytes) /
			    HAL_LINK_DESC_SIZE;
		paddr = link_desc_banks[i].paddr;
		while (n_entries &&
		       (desc = ath12k_hal_srng_src_get_next_entry(ab, srng))) {
			cookie = DP_LINK_DESC_COOKIE_SET(n_entries, i);
			ath12k_hal_set_link_desc_addr(dp->hal, desc, cookie, paddr,
						      rbm);
			n_entries--;
			paddr += HAL_LINK_DESC_SIZE;
		}
	}

	ath12k_hal_srng_access_end(ab, srng);

	spin_unlock_bh(&srng->lock);

	return 0;

fail_desc_bank_free:
	ath12k_dp_link_desc_bank_free(ab, link_desc_banks);

	return ret;
}

void ath12k_dp_pdev_free(struct ath12k_base *ab)
{
	struct ath12k_dp *dp = ath12k_ab_to_dp(ab);
	struct ath12k *ar;
	int i;

	for (i = 0; i < ab->num_radios; i++) {
		ar = ab->pdevs[i].ar;
		rcu_assign_pointer(dp->dp_pdevs[ar->pdev_idx], NULL);
	}

	synchronize_rcu();

	for (i = 0; i < ab->num_radios; i++)
		ath12k_dp_rx_pdev_free(ab, i);
}

void ath12k_dp_pdev_pre_alloc(struct ath12k *ar)
{
	struct ath12k_pdev_dp *dp = &ar->dp;

	dp->mac_id = ar->pdev_idx;
	atomic_set(&dp->num_tx_pending, 0);
	init_waitqueue_head(&dp->tx_empty_waitq);
	/* TODO: Add any RXDMA setup required per pdev */
}

int ath12k_dp_pdev_alloc(struct ath12k_base *ab)
{
	struct ath12k_dp *dp = ath12k_ab_to_dp(ab);
	struct ath12k_pdev_dp *dp_pdev;
	struct ath12k *ar;
	int ret;
	int i;

	ret = ath12k_dp_rx_htt_setup(ab);
	if (ret)
		goto out;

	/* TODO: Per-pdev rx ring unlike tx ring which is mapped to different AC's */
	for (i = 0; i < ab->num_radios; i++) {
		ar = ab->pdevs[i].ar;

		dp_pdev = &ar->dp;

		dp_pdev->hw = ar->ah->hw;
		dp_pdev->dp = dp;
		dp_pdev->hw_link_id = ar->hw_link_id;
		dp_pdev->dp_hw = &ar->ah->dp_hw;

		ret = ath12k_dp_rx_pdev_alloc(ab, i);
		if (ret) {
			ath12k_warn(ab, "failed to allocate pdev rx for pdev_id :%d\n",
				    i);
			goto err;
		}
		ret = ath12k_dp_rx_pdev_mon_attach(ar);
		if (ret) {
			ath12k_warn(ab, "failed to initialize mon pdev %d\n", i);
			goto err;
		}
	}

	for (i = 0; i < ab->num_radios; i++) {
		ar = ab->pdevs[i].ar;
		rcu_assign_pointer(dp->dp_pdevs[ar->pdev_idx], &ar->dp);
	}

	return 0;
err:
	ath12k_dp_pdev_free(ab);
out:
	return ret;
}

static void ath12k_dp_update_vdev_search(struct ath12k_link_vif *arvif)
{
	u8 link_id = arvif->link_id;
	struct ath12k_vif *ahvif = arvif->ahvif;
	struct ath12k_dp_link_vif *dp_link_vif;

	dp_link_vif = ath12k_dp_vif_to_dp_link_vif(&ahvif->dp_vif, link_id);

	switch (arvif->ahvif->vdev_type) {
	case WMI_VDEV_TYPE_STA:
		dp_link_vif->hal_addr_search_flags = HAL_TX_ADDRY_EN;
		dp_link_vif->search_type = HAL_TX_ADDR_SEARCH_DEFAULT;
		break;
	case WMI_VDEV_TYPE_AP:
	case WMI_VDEV_TYPE_IBSS:
		dp_link_vif->hal_addr_search_flags = HAL_TX_ADDRX_EN;
		dp_link_vif->search_type = HAL_TX_ADDR_SEARCH_DEFAULT;
		break;
	case WMI_VDEV_TYPE_MONITOR:
	default:
		return;
	}
}

void ath12k_dp_vdev_tx_attach(struct ath12k *ar, struct ath12k_link_vif *arvif)
{
	struct ath12k_base *ab = ar->ab;
	struct ath12k_vif *ahvif = arvif->ahvif;
	u8 link_id = arvif->link_id;
	int bank_id;
	struct ath12k_dp_link_vif *dp_link_vif;

	dp_link_vif = ath12k_dp_vif_to_dp_link_vif(&ahvif->dp_vif, link_id);

	dp_link_vif->tcl_metadata |= u32_encode_bits(1, HTT_TCL_META_DATA_TYPE) |
				     u32_encode_bits(arvif->vdev_id,
						     HTT_TCL_META_DATA_VDEV_ID) |
				     u32_encode_bits(ar->pdev->pdev_id,
						     HTT_TCL_META_DATA_PDEV_ID);

	/* set HTT extension valid bit to 0 by default */
	dp_link_vif->tcl_metadata &= ~HTT_TCL_META_DATA_VALID_HTT;

	ath12k_dp_update_vdev_search(arvif);
	dp_link_vif->vdev_id_check_en = true;
	bank_id = ath12k_dp_tx_get_bank_profile(ab, arvif, ath12k_ab_to_dp(ab));
	dp_link_vif->bank_id = bank_id;

	/* TODO: error path for bank id failure */
	if (bank_id == DP_INVALID_BANK_ID) {
		ath12k_err(ar->ab, "Failed to initialize DP TX Banks");
		return;
	}
}

static void ath12k_dp_cc_cleanup(struct ath12k_base *ab)
{
	struct ath12k_rx_desc_info *desc_info;
	struct ath12k_tx_desc_info *tx_desc_info, *tmp1;
	struct ath12k_dp *dp = ath12k_ab_to_dp(ab);
	struct ath12k_skb_cb *skb_cb;
	struct sk_buff *skb;
	struct ath12k *ar;
	int i, j;
	u32 pool_id, tx_spt_page;

	if (!dp->spt_info)
		return;

	/* RX Descriptor cleanup */
	spin_lock_bh(&dp->rx_desc_lock);

	if (dp->rxbaddr) {
		for (i = 0; i < ATH12K_NUM_RX_SPT_PAGES(ab); i++) {
			if (!dp->rxbaddr[i])
				continue;

			desc_info = dp->rxbaddr[i];

			for (j = 0; j < ATH12K_MAX_SPT_ENTRIES; j++) {
				if (!desc_info[j].in_use) {
					list_del(&desc_info[j].list);
					continue;
				}

				skb = desc_info[j].skb;
				if (!skb)
					continue;

				dma_unmap_single(ab->dev,
						 ATH12K_SKB_RXCB(skb)->paddr,
						 skb->len + skb_tailroom(skb),
						 DMA_FROM_DEVICE);
				dev_kfree_skb_any(skb);
			}

			kfree(dp->rxbaddr[i]);
			dp->rxbaddr[i] = NULL;
		}

		kfree(dp->rxbaddr);
		dp->rxbaddr = NULL;
	}

	spin_unlock_bh(&dp->rx_desc_lock);

	/* TX Descriptor cleanup */
	for (i = 0; i < ATH12K_HW_MAX_QUEUES; i++) {
		spin_lock_bh(&dp->tx_desc_lock[i]);

		list_for_each_entry_safe(tx_desc_info, tmp1,
					 &dp->tx_desc_used_list[i], list) {
			list_del(&tx_desc_info->list);
			skb = tx_desc_info->skb;

			if (!skb)
				continue;

			skb_cb = ATH12K_SKB_CB(skb);
			if (skb_cb->paddr_ext_desc) {
				dma_unmap_single(ab->dev,
						 skb_cb->paddr_ext_desc,
						 tx_desc_info->skb_ext_desc->len,
						 DMA_TO_DEVICE);
				dev_kfree_skb_any(tx_desc_info->skb_ext_desc);
			}

			/* if we are unregistering, hw would've been destroyed and
			 * ar is no longer valid.
			 */
			if (!(test_bit(ATH12K_FLAG_UNREGISTERING, &ab->dev_flags))) {
				ar = skb_cb->ar;

				if (atomic_dec_and_test(&ar->dp.num_tx_pending))
					wake_up(&ar->dp.tx_empty_waitq);
			}

			dma_unmap_single(ab->dev, ATH12K_SKB_CB(skb)->paddr,
					 skb->len, DMA_TO_DEVICE);
			dev_kfree_skb_any(skb);
		}

		spin_unlock_bh(&dp->tx_desc_lock[i]);
	}

	if (dp->txbaddr) {
		for (pool_id = 0; pool_id < ATH12K_HW_MAX_QUEUES; pool_id++) {
			spin_lock_bh(&dp->tx_desc_lock[pool_id]);

			for (i = 0; i < ATH12K_TX_SPT_PAGES_PER_POOL(ab); i++) {
				tx_spt_page = i + pool_id *
					      ATH12K_TX_SPT_PAGES_PER_POOL(ab);
				if (!dp->txbaddr[tx_spt_page])
					continue;

				kfree(dp->txbaddr[tx_spt_page]);
				dp->txbaddr[tx_spt_page] = NULL;
			}

			spin_unlock_bh(&dp->tx_desc_lock[pool_id]);
		}

		kfree(dp->txbaddr);
		dp->txbaddr = NULL;
	}

	/* unmap SPT pages */
	for (i = 0; i < dp->num_spt_pages; i++) {
		if (!dp->spt_info[i].vaddr)
			continue;

		dma_free_coherent(ab->dev, ATH12K_PAGE_SIZE,
				  dp->spt_info[i].vaddr, dp->spt_info[i].paddr);
		dp->spt_info[i].vaddr = NULL;
	}

	kfree(dp->spt_info);
	dp->spt_info = NULL;
}

static void ath12k_dp_reoq_lut_cleanup(struct ath12k_base *ab)
{
	struct ath12k_dp *dp = ath12k_ab_to_dp(ab);

	if (!ab->hw_params->reoq_lut_support)
		return;

	if (dp->reoq_lut.vaddr_unaligned) {
		ath12k_hal_write_reoq_lut_addr(ab, 0);
		dma_free_coherent(ab->dev, dp->reoq_lut.size,
				  dp->reoq_lut.vaddr_unaligned,
				  dp->reoq_lut.paddr_unaligned);
		dp->reoq_lut.vaddr_unaligned = NULL;
	}

	if (dp->ml_reoq_lut.vaddr_unaligned) {
		ath12k_hal_write_ml_reoq_lut_addr(ab, 0);
		dma_free_coherent(ab->dev, dp->ml_reoq_lut.size,
				  dp->ml_reoq_lut.vaddr_unaligned,
				  dp->ml_reoq_lut.paddr_unaligned);
		dp->ml_reoq_lut.vaddr_unaligned = NULL;
	}
}

static void ath12k_dp_cleanup(struct ath12k_base *ab)
{
	struct ath12k_dp *dp = ath12k_ab_to_dp(ab);
	int i;

	ath12k_dp_link_peer_rhash_tbl_destroy(dp);

	if (!dp->ab)
		return;

	ath12k_dp_link_desc_cleanup(ab, dp->link_desc_banks,
				    HAL_WBM_IDLE_LINK, &dp->wbm_idle_ring);

	ath12k_dp_cc_cleanup(ab);
	ath12k_dp_reoq_lut_cleanup(ab);
	ath12k_dp_deinit_bank_profiles(ab);
	ath12k_dp_srng_common_cleanup(ab);

	ath12k_dp_rx_reo_cmd_list_cleanup(ab);

	for (i = 0; i < ab->hw_params->max_tx_ring; i++) {
		kfree(dp->tx_ring[i].tx_status);
		dp->tx_ring[i].tx_status = NULL;
	}

	ath12k_dp_rx_free(ab);
	/* Deinit any SOC level resource */
}

static u32 ath12k_dp_cc_cookie_gen(u16 ppt_idx, u16 spt_idx)
{
	return (u32)ppt_idx << ATH12K_CC_PPT_SHIFT | spt_idx;
}

static void *ath12k_dp_cc_get_desc_addr_ptr(struct ath12k_dp *dp,
					    u16 ppt_idx, u16 spt_idx)
{
	return dp->spt_info[ppt_idx].vaddr + spt_idx;
}

struct ath12k_rx_desc_info *ath12k_dp_get_rx_desc(struct ath12k_dp *dp,
						  u32 cookie)
{
	struct ath12k_rx_desc_info **desc_addr_ptr;
	u16 start_ppt_idx, end_ppt_idx, ppt_idx, spt_idx;

	ppt_idx = u32_get_bits(cookie, ATH12K_DP_CC_COOKIE_PPT);
	spt_idx = u32_get_bits(cookie, ATH12K_DP_CC_COOKIE_SPT);

	start_ppt_idx = dp->rx_ppt_base + ATH12K_RX_SPT_PAGE_OFFSET(dp->ab);
	end_ppt_idx = start_ppt_idx + ATH12K_NUM_RX_SPT_PAGES(dp->ab);

	if (ppt_idx < start_ppt_idx ||
	    ppt_idx >= end_ppt_idx ||
	    spt_idx > ATH12K_MAX_SPT_ENTRIES)
		return NULL;

	ppt_idx = ppt_idx - dp->rx_ppt_base;
	desc_addr_ptr = ath12k_dp_cc_get_desc_addr_ptr(dp, ppt_idx, spt_idx);

	return *desc_addr_ptr;
}
EXPORT_SYMBOL(ath12k_dp_get_rx_desc);

struct ath12k_tx_desc_info *ath12k_dp_get_tx_desc(struct ath12k_dp *dp,
						  u32 cookie)
{
	struct ath12k_tx_desc_info **desc_addr_ptr;
	u16 start_ppt_idx, end_ppt_idx, ppt_idx, spt_idx;

	ppt_idx = u32_get_bits(cookie, ATH12K_DP_CC_COOKIE_PPT);
	spt_idx = u32_get_bits(cookie, ATH12K_DP_CC_COOKIE_SPT);

	start_ppt_idx = ATH12K_TX_SPT_PAGE_OFFSET;
	end_ppt_idx = start_ppt_idx +
		      (ATH12K_TX_SPT_PAGES_PER_POOL(dp->ab) * ATH12K_HW_MAX_QUEUES);

	if (ppt_idx < start_ppt_idx ||
	    ppt_idx >= end_ppt_idx ||
	    spt_idx > ATH12K_MAX_SPT_ENTRIES)
		return NULL;

	desc_addr_ptr = ath12k_dp_cc_get_desc_addr_ptr(dp, ppt_idx, spt_idx);

	return *desc_addr_ptr;
}
EXPORT_SYMBOL(ath12k_dp_get_tx_desc);

static int ath12k_dp_cc_desc_init(struct ath12k_base *ab)
{
	struct ath12k_dp *dp = ath12k_ab_to_dp(ab);
	struct ath12k_rx_desc_info *rx_descs, **rx_desc_addr;
	struct ath12k_tx_desc_info *tx_descs, **tx_desc_addr;
	u32 num_rx_spt_pages = ATH12K_NUM_RX_SPT_PAGES(ab);
	u32 i, j, pool_id, tx_spt_page;
	u32 ppt_idx, cookie_ppt_idx;

	spin_lock_bh(&dp->rx_desc_lock);

	dp->rxbaddr = kcalloc(num_rx_spt_pages,
			      sizeof(struct ath12k_rx_desc_info *), GFP_ATOMIC);

	if (!dp->rxbaddr) {
		spin_unlock_bh(&dp->rx_desc_lock);
		return -ENOMEM;
	}

	/* First ATH12K_NUM_RX_SPT_PAGES(ab) of allocated SPT pages are used for
	 * RX
	 */
	for (i = 0; i < num_rx_spt_pages; i++) {
		rx_descs = kcalloc(ATH12K_MAX_SPT_ENTRIES, sizeof(*rx_descs),
				   GFP_ATOMIC);

		if (!rx_descs) {
			spin_unlock_bh(&dp->rx_desc_lock);
			return -ENOMEM;
		}

		ppt_idx = ATH12K_RX_SPT_PAGE_OFFSET(ab) + i;
		cookie_ppt_idx = dp->rx_ppt_base + ppt_idx;
		dp->rxbaddr[i] = &rx_descs[0];

		for (j = 0; j < ATH12K_MAX_SPT_ENTRIES; j++) {
			rx_descs[j].cookie = ath12k_dp_cc_cookie_gen(cookie_ppt_idx, j);
			rx_descs[j].magic = ATH12K_DP_RX_DESC_MAGIC;
			rx_descs[j].device_id = ab->device_id;
			list_add_tail(&rx_descs[j].list, &dp->rx_desc_free_list);

			/* Update descriptor VA in SPT */
			rx_desc_addr = ath12k_dp_cc_get_desc_addr_ptr(dp, ppt_idx, j);
			*rx_desc_addr = &rx_descs[j];
		}
	}

	spin_unlock_bh(&dp->rx_desc_lock);

	dp->txbaddr = kcalloc(ATH12K_NUM_TX_SPT_PAGES(ab),
			      sizeof(struct ath12k_tx_desc_info *), GFP_ATOMIC);

	if (!dp->txbaddr)
		return -ENOMEM;

	for (pool_id = 0; pool_id < ATH12K_HW_MAX_QUEUES; pool_id++) {
		spin_lock_bh(&dp->tx_desc_lock[pool_id]);
		for (i = 0; i < ATH12K_TX_SPT_PAGES_PER_POOL(ab); i++) {
			tx_descs = kcalloc(ATH12K_MAX_SPT_ENTRIES, sizeof(*tx_descs),
					   GFP_ATOMIC);

			if (!tx_descs) {
				spin_unlock_bh(&dp->tx_desc_lock[pool_id]);
				/* Caller takes care of TX pending and RX desc cleanup */
				return -ENOMEM;
			}

			tx_spt_page = i + pool_id *
				      ATH12K_TX_SPT_PAGES_PER_POOL(ab);
			ppt_idx = ATH12K_TX_SPT_PAGE_OFFSET + tx_spt_page;

			dp->txbaddr[tx_spt_page] = &tx_descs[0];

			for (j = 0; j < ATH12K_MAX_SPT_ENTRIES; j++) {
				tx_descs[j].desc_id = ath12k_dp_cc_cookie_gen(ppt_idx, j);
				tx_descs[j].pool_id = pool_id;
				list_add_tail(&tx_descs[j].list,
					      &dp->tx_desc_free_list[pool_id]);

				/* Update descriptor VA in SPT */
				tx_desc_addr =
					ath12k_dp_cc_get_desc_addr_ptr(dp, ppt_idx, j);
				*tx_desc_addr = &tx_descs[j];
			}
		}
		spin_unlock_bh(&dp->tx_desc_lock[pool_id]);
	}
	return 0;
}

static int ath12k_dp_cmem_init(struct ath12k_base *ab,
			       struct ath12k_dp *dp,
			       enum ath12k_dp_desc_type type)
{
	u32 cmem_base;
	int i, start, end;

	cmem_base = ab->qmi.dev_mem[ATH12K_QMI_DEVMEM_CMEM_INDEX].start;

	switch (type) {
	case ATH12K_DP_TX_DESC:
		start = ATH12K_TX_SPT_PAGE_OFFSET;
		end = start + ATH12K_NUM_TX_SPT_PAGES(ab);
		break;
	case ATH12K_DP_RX_DESC:
		cmem_base += ATH12K_PPT_ADDR_OFFSET(dp->rx_ppt_base);
		start = ATH12K_RX_SPT_PAGE_OFFSET(ab);
		end = start + ATH12K_NUM_RX_SPT_PAGES(ab);
		break;
	default:
		ath12k_err(ab, "invalid descriptor type %d in cmem init\n", type);
		return -EINVAL;
	}

	/* Write to PPT in CMEM */
	for (i = start; i < end; i++)
		ath12k_hif_write32(ab, cmem_base + ATH12K_PPT_ADDR_OFFSET(i),
				   dp->spt_info[i].paddr >> ATH12K_SPT_4K_ALIGN_OFFSET);

	return 0;
}

void ath12k_dp_partner_cc_init(struct ath12k_base *ab)
{
	struct ath12k_hw_group *ag = ab->ag;
	int i;

	for (i = 0; i < ag->num_devices; i++) {
		if (ag->ab[i] == ab)
			continue;

		ath12k_dp_cmem_init(ab, ath12k_ab_to_dp(ag->ab[i]), ATH12K_DP_RX_DESC);
	}
}

static u32 ath12k_dp_get_num_spt_pages(struct ath12k_base *ab)
{
	return ATH12K_NUM_RX_SPT_PAGES(ab) + ATH12K_NUM_TX_SPT_PAGES(ab);
}

static int ath12k_dp_cc_init(struct ath12k_base *ab)
{
	struct ath12k_dp *dp = ath12k_ab_to_dp(ab);
	int i, ret = 0;

	INIT_LIST_HEAD(&dp->rx_desc_free_list);
	spin_lock_init(&dp->rx_desc_lock);

	for (i = 0; i < ATH12K_HW_MAX_QUEUES; i++) {
		INIT_LIST_HEAD(&dp->tx_desc_free_list[i]);
		INIT_LIST_HEAD(&dp->tx_desc_used_list[i]);
		spin_lock_init(&dp->tx_desc_lock[i]);
	}

	dp->num_spt_pages = ath12k_dp_get_num_spt_pages(ab);
	if (dp->num_spt_pages > ATH12K_MAX_PPT_ENTRIES)
		dp->num_spt_pages = ATH12K_MAX_PPT_ENTRIES;

	dp->spt_info = kcalloc(dp->num_spt_pages, sizeof(struct ath12k_spt_info),
			       GFP_KERNEL);

	if (!dp->spt_info) {
		ath12k_warn(ab, "SPT page allocation failure");
		return -ENOMEM;
	}

	dp->rx_ppt_base = ab->device_id * ATH12K_NUM_RX_SPT_PAGES(ab);

	for (i = 0; i < dp->num_spt_pages; i++) {
		dp->spt_info[i].vaddr = dma_alloc_coherent(ab->dev,
							   ATH12K_PAGE_SIZE,
							   &dp->spt_info[i].paddr,
							   GFP_KERNEL);

		if (!dp->spt_info[i].vaddr) {
			ret = -ENOMEM;
			goto free;
		}

		if (dp->spt_info[i].paddr & ATH12K_SPT_4K_ALIGN_CHECK) {
			ath12k_warn(ab, "SPT allocated memory is not 4K aligned");
			ret = -EINVAL;
			goto free;
		}
	}

	ret = ath12k_dp_cmem_init(ab, dp, ATH12K_DP_TX_DESC);
	if (ret) {
		ath12k_warn(ab, "HW CC Tx cmem init failed %d", ret);
		goto free;
	}

	ret = ath12k_dp_cmem_init(ab, dp, ATH12K_DP_RX_DESC);
	if (ret) {
		ath12k_warn(ab, "HW CC Rx cmem init failed %d", ret);
		goto free;
	}

	ret = ath12k_dp_cc_desc_init(ab);
	if (ret) {
		ath12k_warn(ab, "HW CC desc init failed %d", ret);
		goto free;
	}

	return 0;
free:
	ath12k_dp_cc_cleanup(ab);
	return ret;
}

static int ath12k_dp_alloc_reoq_lut(struct ath12k_base *ab,
				    struct ath12k_reo_q_addr_lut *lut)
{
	lut->size =  DP_REOQ_LUT_SIZE + HAL_REO_QLUT_ADDR_ALIGN - 1;
	lut->vaddr_unaligned = dma_alloc_coherent(ab->dev, lut->size,
						  &lut->paddr_unaligned,
						  GFP_KERNEL | __GFP_ZERO);
	if (!lut->vaddr_unaligned)
		return -ENOMEM;

	lut->vaddr = PTR_ALIGN(lut->vaddr_unaligned, HAL_REO_QLUT_ADDR_ALIGN);
	lut->paddr = lut->paddr_unaligned +
		     ((unsigned long)lut->vaddr - (unsigned long)lut->vaddr_unaligned);
	return 0;
}

static int ath12k_dp_reoq_lut_setup(struct ath12k_base *ab)
{
	struct ath12k_dp *dp = ath12k_ab_to_dp(ab);
	int ret;

	if (!ab->hw_params->reoq_lut_support)
		return 0;

	ret = ath12k_dp_alloc_reoq_lut(ab, &dp->reoq_lut);
	if (ret) {
		ath12k_warn(ab, "failed to allocate memory for reoq table");
		return ret;
	}

	ret = ath12k_dp_alloc_reoq_lut(ab, &dp->ml_reoq_lut);
	if (ret) {
		ath12k_warn(ab, "failed to allocate memory for ML reoq table");
		dma_free_coherent(ab->dev, dp->reoq_lut.size,
				  dp->reoq_lut.vaddr_unaligned,
				  dp->reoq_lut.paddr_unaligned);
		dp->reoq_lut.vaddr_unaligned = NULL;
		return ret;
	}

	/* Bits in the register have address [39:8] LUT base address to be
	 * allocated such that LSBs are assumed to be zero. Also, current
	 * design supports paddr up to 4 GB max hence it fits in 32 bit
	 * register only
	 */

	ath12k_hal_write_reoq_lut_addr(ab, dp->reoq_lut.paddr >> 8);
	ath12k_hal_write_ml_reoq_lut_addr(ab, dp->ml_reoq_lut.paddr >> 8);
	ath12k_hal_reoq_lut_addr_read_enable(ab);
	ath12k_hal_reoq_lut_set_max_peerid(ab);

	return 0;
}

static int ath12k_dp_setup(struct ath12k_base *ab)
{
	struct ath12k_dp *dp;
	struct hal_srng *srng = NULL;
	size_t size = 0;
	u32 n_link_desc = 0;
	int ret;
	int i;

	dp = ath12k_ab_to_dp(ab);
	dp->ab = ab;

	INIT_LIST_HEAD(&dp->reo_cmd_list);
	INIT_LIST_HEAD(&dp->reo_cmd_cache_flush_list);
	INIT_LIST_HEAD(&dp->reo_cmd_update_rx_queue_list);
	spin_lock_init(&dp->reo_cmd_lock);
	spin_lock_init(&dp->reo_rxq_flush_lock);

	spin_lock_init(&dp->dp_lock);
	INIT_LIST_HEAD(&dp->peers);

	mutex_init(&dp->link_peer_rhash_tbl_lock);

	dp->reo_cmd_cache_flush_count = 0;
	dp->idle_link_rbm =
			ath12k_hal_get_idle_link_rbm(&ab->hal, ab->device_id);

	ret = ath12k_dp_link_peer_rhash_tbl_init(dp);
	if (ret) {
		ath12k_warn(ab, "failed to init link_peer rhash table: %d\n", ret);
		return ret;
	}

	ret = ath12k_wbm_idle_ring_setup(ab, &n_link_desc);
	if (ret) {
		ath12k_warn(ab, "failed to setup wbm_idle_ring: %d\n", ret);
		goto rhash_destroy;
	}

	srng = &ab->hal.srng_list[dp->wbm_idle_ring.ring_id];

	ret = ath12k_dp_link_desc_setup(ab, dp->link_desc_banks,
					HAL_WBM_IDLE_LINK, srng, n_link_desc);
	if (ret) {
		ath12k_warn(ab, "failed to setup link desc: %d\n", ret);
		goto rhash_destroy;
	}

	ret = ath12k_dp_cc_init(ab);

	if (ret) {
		ath12k_warn(ab, "failed to setup cookie converter %d\n", ret);
		goto fail_link_desc_cleanup;
	}
	ret = ath12k_dp_init_bank_profiles(ab);
	if (ret) {
		ath12k_warn(ab, "failed to setup bank profiles %d\n", ret);
		goto fail_hw_cc_cleanup;
	}

	ret = ath12k_dp_srng_common_setup(ab);
	if (ret)
		goto fail_dp_bank_profiles_cleanup;

	size = ab->hal.hal_wbm_release_ring_tx_size *
	       DP_TX_COMP_RING_SIZE(ab);

	ret = ath12k_dp_reoq_lut_setup(ab);
	if (ret) {
		ath12k_warn(ab, "failed to setup reoq table %d\n", ret);
		goto fail_cmn_srng_cleanup;
	}

	for (i = 0; i < ab->hw_params->max_tx_ring; i++) {
		dp->tx_ring[i].tcl_data_ring_id = i;

		dp->tx_ring[i].tx_status_head = 0;
		dp->tx_ring[i].tx_status_tail = DP_TX_COMP_RING_SIZE(ab) - 1;
		dp->tx_ring[i].tx_status = kmalloc(size, GFP_KERNEL);
		if (!dp->tx_ring[i].tx_status) {
			ret = -ENOMEM;
			/* FIXME: The allocated tx status is not freed
			 * properly here
			 */
			goto fail_cmn_reoq_cleanup;
		}
	}

	for (i = 0; i < HAL_DSCP_TID_MAP_TBL_NUM_ENTRIES_MAX; i++)
		ath12k_hal_tx_set_dscp_tid_map(ab, i);

	ret = ath12k_dp_rx_alloc(ab);
	if (ret)
		goto fail_dp_rx_free;

	/* Init any SOC level resource for DP */

	return 0;

fail_dp_rx_free:
	ath12k_dp_rx_free(ab);

fail_cmn_reoq_cleanup:
	ath12k_dp_reoq_lut_cleanup(ab);

fail_cmn_srng_cleanup:
	ath12k_dp_srng_common_cleanup(ab);

fail_dp_bank_profiles_cleanup:
	ath12k_dp_deinit_bank_profiles(ab);

fail_hw_cc_cleanup:
	ath12k_dp_cc_cleanup(ab);

fail_link_desc_cleanup:
	ath12k_dp_link_desc_cleanup(ab, dp->link_desc_banks,
				    HAL_WBM_IDLE_LINK, &dp->wbm_idle_ring);
rhash_destroy:
	ath12k_dp_link_peer_rhash_tbl_destroy(dp);

	return ret;
}

void ath12k_dp_cmn_device_deinit(struct ath12k_dp *dp)
{
	ath12k_dp_cleanup(dp->ab);
}

int ath12k_dp_cmn_device_init(struct ath12k_dp *dp)
{
	int ret;

	ret = ath12k_dp_setup(dp->ab);
	if (ret)
		return ret;

	return 0;
}

void ath12k_dp_cmn_hw_group_unassign(struct ath12k_dp *dp,
				     struct ath12k_hw_group *ag)
{
	struct ath12k_dp_hw_group *dp_hw_grp = &ag->dp_hw_grp;

	lockdep_assert_held(&ag->mutex);

	dp_hw_grp->dp[dp->device_id] = NULL;

	dp->ag = NULL;
	dp->device_id = ATH12K_INVALID_DEVICE_ID;
}

void ath12k_dp_cmn_hw_group_assign(struct ath12k_dp *dp,
				   struct ath12k_hw_group *ag)
{
	struct ath12k_base *ab = dp->ab;
	struct ath12k_dp_hw_group *dp_hw_grp = &ag->dp_hw_grp;

	dp->ag = ag;
	dp->device_id = ab->device_id;
	dp_hw_grp->dp[dp->device_id] = dp;
}
