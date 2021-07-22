// SPDX-License-Identifier: BSD-3-Clause-Clear
/*
 * Copyright (c) 2019-2020 The Linux Foundation. All rights reserved.
 */

#include "core.h"
#include "debug.h"

static int ath11k_dbring_bufs_replenish(struct ath11k *ar,
					struct ath11k_dbring *ring,
					struct ath11k_dbring_element *buff,
					gfp_t gfp)
{
	struct ath11k_base *ab = ar->ab;
	struct hal_srng *srng;
	dma_addr_t paddr;
	void *ptr_aligned, *ptr_unaligned, *desc;
	int ret;
	int buf_id;
	u32 cookie;

	srng = &ab->hal.srng_list[ring->refill_srng.ring_id];

	lockdep_assert_held(&srng->lock);

	ath11k_hal_srng_access_begin(ab, srng);

	ptr_unaligned = buff->payload;
	ptr_aligned = PTR_ALIGN(ptr_unaligned, ring->buf_align);
	paddr = dma_map_single(ab->dev, ptr_aligned, ring->buf_sz,
			       DMA_FROM_DEVICE);

	ret = dma_mapping_error(ab->dev, paddr);
	if (ret)
		goto err;

	spin_lock_bh(&ring->idr_lock);
	buf_id = idr_alloc(&ring->bufs_idr, buff, 0, ring->bufs_max, gfp);
	spin_unlock_bh(&ring->idr_lock);
	if (buf_id < 0) {
		ret = -ENOBUFS;
		goto err_dma_unmap;
	}

	desc = ath11k_hal_srng_src_get_next_entry(ab, srng);
	if (!desc) {
		ret = -ENOENT;
		goto err_idr_remove;
	}

	buff->paddr = paddr;

	cookie = FIELD_PREP(DP_RXDMA_BUF_COOKIE_PDEV_ID, ar->pdev_idx) |
		 FIELD_PREP(DP_RXDMA_BUF_COOKIE_BUF_ID, buf_id);

	ath11k_hal_rx_buf_addr_info_set(desc, paddr, cookie, 0);

	ath11k_hal_srng_access_end(ab, srng);

	return 0;

err_idr_remove:
	spin_lock_bh(&ring->idr_lock);
	idr_remove(&ring->bufs_idr, buf_id);
	spin_unlock_bh(&ring->idr_lock);
err_dma_unmap:
	dma_unmap_single(ab->dev, paddr, ring->buf_sz,
			 DMA_FROM_DEVICE);
err:
	ath11k_hal_srng_access_end(ab, srng);
	return ret;
}

static int ath11k_dbring_fill_bufs(struct ath11k *ar,
				   struct ath11k_dbring *ring,
				   gfp_t gfp)
{
	struct ath11k_dbring_element *buff;
	struct hal_srng *srng;
	int num_remain, req_entries, num_free;
	u32 align;
	int size, ret;

	srng = &ar->ab->hal.srng_list[ring->refill_srng.ring_id];

	spin_lock_bh(&srng->lock);

	num_free = ath11k_hal_srng_src_num_free(ar->ab, srng, true);
	req_entries = min(num_free, ring->bufs_max);
	num_remain = req_entries;
	align = ring->buf_align;
	size = sizeof(*buff) + ring->buf_sz + align - 1;

	while (num_remain > 0) {
		buff = kzalloc(size, gfp);
		if (!buff)
			break;

		ret = ath11k_dbring_bufs_replenish(ar, ring, buff, gfp);
		if (ret) {
			ath11k_warn(ar->ab, "failed to replenish db ring num_remain %d req_ent %d\n",
				    num_remain, req_entries);
			kfree(buff);
			break;
		}
		num_remain--;
	}

	spin_unlock_bh(&srng->lock);

	return num_remain;
}

int ath11k_dbring_wmi_cfg_setup(struct ath11k *ar,
				struct ath11k_dbring *ring,
				enum wmi_direct_buffer_module id)
{
	struct ath11k_wmi_pdev_dma_ring_cfg_req_cmd param = {0};
	int ret;

	if (id >= WMI_DIRECT_BUF_MAX)
		return -EINVAL;

	param.pdev_id		= DP_SW2HW_MACID(ring->pdev_id);
	param.module_id		= id;
	param.base_paddr_lo	= lower_32_bits(ring->refill_srng.paddr);
	param.base_paddr_hi	= upper_32_bits(ring->refill_srng.paddr);
	param.head_idx_paddr_lo	= lower_32_bits(ring->hp_addr);
	param.head_idx_paddr_hi = upper_32_bits(ring->hp_addr);
	param.tail_idx_paddr_lo = lower_32_bits(ring->tp_addr);
	param.tail_idx_paddr_hi = upper_32_bits(ring->tp_addr);
	param.num_elems		= ring->bufs_max;
	param.buf_size		= ring->buf_sz;
	param.num_resp_per_event = ring->num_resp_per_event;
	param.event_timeout_ms	= ring->event_timeout_ms;

	ret = ath11k_wmi_pdev_dma_ring_cfg(ar, &param);
	if (ret) {
		ath11k_warn(ar->ab, "failed to setup db ring cfg\n");
		return ret;
	}

	return 0;
}

int ath11k_dbring_set_cfg(struct ath11k *ar, struct ath11k_dbring *ring,
			  u32 num_resp_per_event, u32 event_timeout_ms,
			  int (*handler)(struct ath11k *,
					 struct ath11k_dbring_data *))
{
	if (WARN_ON(!ring))
		return -EINVAL;

	ring->num_resp_per_event = num_resp_per_event;
	ring->event_timeout_ms = event_timeout_ms;
	ring->handler = handler;

	return 0;
}

int ath11k_dbring_buf_setup(struct ath11k *ar,
			    struct ath11k_dbring *ring,
			    struct ath11k_dbring_cap *db_cap)
{
	struct ath11k_base *ab = ar->ab;
	struct hal_srng *srng;
	int ret;

	srng = &ab->hal.srng_list[ring->refill_srng.ring_id];
	ring->bufs_max = ring->refill_srng.size /
		ath11k_hal_srng_get_entrysize(ab, HAL_RXDMA_DIR_BUF);

	ring->buf_sz = db_cap->min_buf_sz;
	ring->buf_align = db_cap->min_buf_align;
	ring->pdev_id = db_cap->pdev_id;
	ring->hp_addr = ath11k_hal_srng_get_hp_addr(ar->ab, srng);
	ring->tp_addr = ath11k_hal_srng_get_tp_addr(ar->ab, srng);

	ret = ath11k_dbring_fill_bufs(ar, ring, GFP_KERNEL);

	return ret;
}

int ath11k_dbring_srng_setup(struct ath11k *ar, struct ath11k_dbring *ring,
			     int ring_num, int num_entries)
{
	int ret;

	ret = ath11k_dp_srng_setup(ar->ab, &ring->refill_srng, HAL_RXDMA_DIR_BUF,
				   ring_num, ar->pdev_idx, num_entries);
	if (ret < 0) {
		ath11k_warn(ar->ab, "failed to setup srng: %d ring_id %d\n",
			    ret, ring_num);
		goto err;
	}

	return 0;
err:
	ath11k_dp_srng_cleanup(ar->ab, &ring->refill_srng);
	return ret;
}

int ath11k_dbring_get_cap(struct ath11k_base *ab,
			  u8 pdev_idx,
			  enum wmi_direct_buffer_module id,
			  struct ath11k_dbring_cap *db_cap)
{
	int i;

	if (!ab->num_db_cap || !ab->db_caps)
		return -ENOENT;

	if (id >= WMI_DIRECT_BUF_MAX)
		return -EINVAL;

	for (i = 0; i < ab->num_db_cap; i++) {
		if (pdev_idx == ab->db_caps[i].pdev_id &&
		    id == ab->db_caps[i].id) {
			*db_cap = ab->db_caps[i];

			return 0;
		}
	}

	return -ENOENT;
}

int ath11k_dbring_buffer_release_event(struct ath11k_base *ab,
				       struct ath11k_dbring_buf_release_event *ev)
{
	struct ath11k_dbring *ring;
	struct hal_srng *srng;
	struct ath11k *ar;
	struct ath11k_dbring_element *buff;
	struct ath11k_dbring_data handler_data;
	struct ath11k_buffer_addr desc;
	u8 *vaddr_unalign;
	u32 num_entry, num_buff_reaped;
	u8 pdev_idx, rbm;
	u32 cookie;
	int buf_id;
	int size;
	dma_addr_t paddr;
	int ret = 0;

	pdev_idx = ev->fixed.pdev_id;

	if (pdev_idx >= ab->num_radios) {
		ath11k_warn(ab, "Invalid pdev id %d\n", pdev_idx);
		return -EINVAL;
	}

	if (ev->fixed.num_buf_release_entry !=
	    ev->fixed.num_meta_data_entry) {
		ath11k_warn(ab, "Buffer entry %d mismatch meta entry %d\n",
			    ev->fixed.num_buf_release_entry,
			    ev->fixed.num_meta_data_entry);
		return -EINVAL;
	}

	ar = ab->pdevs[pdev_idx].ar;

	rcu_read_lock();
	if (!rcu_dereference(ab->pdevs_active[pdev_idx])) {
		ret = -EINVAL;
		goto rcu_unlock;
	}

	switch (ev->fixed.module_id) {
	case WMI_DIRECT_BUF_SPECTRAL:
		ring = ath11k_spectral_get_dbring(ar);
		break;
	default:
		ring = NULL;
		ath11k_warn(ab, "Recv dma buffer release ev on unsupp module %d\n",
			    ev->fixed.module_id);
		break;
	}

	if (!ring) {
		ret = -EINVAL;
		goto rcu_unlock;
	}

	srng = &ab->hal.srng_list[ring->refill_srng.ring_id];
	num_entry = ev->fixed.num_buf_release_entry;
	size = sizeof(*buff) + ring->buf_sz + ring->buf_align - 1;
	num_buff_reaped = 0;

	spin_lock_bh(&srng->lock);

	while (num_buff_reaped < num_entry) {
		desc.info0 = ev->buf_entry[num_buff_reaped].paddr_lo;
		desc.info1 = ev->buf_entry[num_buff_reaped].paddr_hi;
		handler_data.meta = ev->meta_data[num_buff_reaped];

		num_buff_reaped++;

		ath11k_hal_rx_buf_addr_info_get(&desc, &paddr, &cookie, &rbm);

		buf_id = FIELD_GET(DP_RXDMA_BUF_COOKIE_BUF_ID, cookie);

		spin_lock_bh(&ring->idr_lock);
		buff = idr_find(&ring->bufs_idr, buf_id);
		if (!buff) {
			spin_unlock_bh(&ring->idr_lock);
			continue;
		}
		idr_remove(&ring->bufs_idr, buf_id);
		spin_unlock_bh(&ring->idr_lock);

		dma_unmap_single(ab->dev, buff->paddr, ring->buf_sz,
				 DMA_FROM_DEVICE);

		if (ring->handler) {
			vaddr_unalign = buff->payload;
			handler_data.data = PTR_ALIGN(vaddr_unalign,
						      ring->buf_align);
			handler_data.data_sz = ring->buf_sz;

			ring->handler(ar, &handler_data);
		}

		memset(buff, 0, size);
		ath11k_dbring_bufs_replenish(ar, ring, buff, GFP_ATOMIC);
	}

	spin_unlock_bh(&srng->lock);

rcu_unlock:
	rcu_read_unlock();

	return ret;
}

void ath11k_dbring_srng_cleanup(struct ath11k *ar, struct ath11k_dbring *ring)
{
	ath11k_dp_srng_cleanup(ar->ab, &ring->refill_srng);
}

void ath11k_dbring_buf_cleanup(struct ath11k *ar, struct ath11k_dbring *ring)
{
	struct ath11k_dbring_element *buff;
	int buf_id;

	spin_lock_bh(&ring->idr_lock);
	idr_for_each_entry(&ring->bufs_idr, buff, buf_id) {
		idr_remove(&ring->bufs_idr, buf_id);
		dma_unmap_single(ar->ab->dev, buff->paddr,
				 ring->buf_sz, DMA_FROM_DEVICE);
		kfree(buff);
	}

	idr_destroy(&ring->bufs_idr);
	spin_unlock_bh(&ring->idr_lock);
}
