// SPDX-License-Identifier: BSD-3-Clause-Clear
/*
 * Copyright (c) 2019-2021 The Linux Foundation. All rights reserved.
 * Copyright (c) 2021-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include "core.h"
#include "debug.h"

static int ath12k_dbring_bufs_replenish(struct ath12k *ar,
					struct ath12k_dbring *ring,
					struct ath12k_dbring_element *buff,
					gfp_t gfp)
{
	struct ath12k_base *ab = ar->ab;
	struct hal_srng *srng;
	dma_addr_t paddr;
	void *ptr_aligned, *ptr_unaligned, *desc;
	int ret;
	int buf_id;
	u32 cookie;

	srng = &ab->hal.srng_list[ring->refill_srng.ring_id];

	lockdep_assert_held(&srng->lock);

	ath12k_hal_srng_access_begin(ab, srng);

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

	desc = ath12k_hal_srng_src_get_next_entry(ab, srng);
	if (!desc) {
		ret = -ENOENT;
		goto err_idr_remove;
	}

	buff->paddr = paddr;

	cookie = u32_encode_bits(ar->pdev_idx, DP_RXDMA_BUF_COOKIE_PDEV_ID) |
		 u32_encode_bits(buf_id, DP_RXDMA_BUF_COOKIE_BUF_ID);

	ath12k_hal_rx_buf_addr_info_set(desc, paddr, cookie, 0);

	ath12k_hal_srng_access_end(ab, srng);

	return 0;

err_idr_remove:
	spin_lock_bh(&ring->idr_lock);
	idr_remove(&ring->bufs_idr, buf_id);
	spin_unlock_bh(&ring->idr_lock);
err_dma_unmap:
	dma_unmap_single(ab->dev, paddr, ring->buf_sz,
			 DMA_FROM_DEVICE);
err:
	ath12k_hal_srng_access_end(ab, srng);
	return ret;
}

static int ath12k_dbring_fill_bufs(struct ath12k *ar,
				   struct ath12k_dbring *ring,
				   gfp_t gfp)
{
	struct ath12k_dbring_element *buff;
	struct hal_srng *srng;
	struct ath12k_base *ab = ar->ab;
	int num_remain, req_entries, num_free;
	u32 align;
	int size, ret;

	srng = &ab->hal.srng_list[ring->refill_srng.ring_id];

	spin_lock_bh(&srng->lock);

	num_free = ath12k_hal_srng_src_num_free(ab, srng, true);
	req_entries = min(num_free, ring->bufs_max);
	num_remain = req_entries;
	align = ring->buf_align;
	size = sizeof(*buff) + ring->buf_sz + align - 1;

	while (num_remain > 0) {
		buff = kzalloc(size, gfp);
		if (!buff)
			break;

		ret = ath12k_dbring_bufs_replenish(ar, ring, buff, gfp);
		if (ret) {
			ath12k_warn(ab, "failed to replenish db ring num_remain %d req_ent %d\n",
				    num_remain, req_entries);
			kfree(buff);
			break;
		}
		num_remain--;
	}

	spin_unlock_bh(&srng->lock);

	return num_remain;
}

int ath12k_dbring_wmi_cfg_setup(struct ath12k *ar,
				struct ath12k_dbring *ring,
				enum wmi_direct_buffer_module id)
{
	struct ath12k_wmi_pdev_dma_ring_cfg_arg arg = {};
	int ret;

	if (id >= WMI_DIRECT_BUF_MAX)
		return -EINVAL;

	arg.pdev_id = DP_SW2HW_MACID(ring->pdev_id);
	arg.module_id = id;
	arg.base_paddr_lo = lower_32_bits(ring->refill_srng.paddr);
	arg.base_paddr_hi = upper_32_bits(ring->refill_srng.paddr);
	arg.head_idx_paddr_lo = lower_32_bits(ring->hp_addr);
	arg.head_idx_paddr_hi = upper_32_bits(ring->hp_addr);
	arg.tail_idx_paddr_lo = lower_32_bits(ring->tp_addr);
	arg.tail_idx_paddr_hi = upper_32_bits(ring->tp_addr);
	arg.num_elems = ring->bufs_max;
	arg.buf_size = ring->buf_sz;
	arg.num_resp_per_event = ring->num_resp_per_event;
	arg.event_timeout_ms = ring->event_timeout_ms;

	ret = ath12k_wmi_pdev_dma_ring_cfg(ar, &arg);
	if (ret) {
		ath12k_warn(ar->ab, "failed to setup db ring cfg\n");
		return ret;
	}

	return 0;
}

int ath12k_dbring_set_cfg(struct ath12k *ar, struct ath12k_dbring *ring,
			  u32 num_resp_per_event, u32 event_timeout_ms,
			  int (*handler)(struct ath12k *,
					 struct ath12k_dbring_data *))
{
	if (WARN_ON(!ring))
		return -EINVAL;

	ring->num_resp_per_event = num_resp_per_event;
	ring->event_timeout_ms = event_timeout_ms;
	ring->handler = handler;

	return 0;
}

int ath12k_dbring_buf_setup(struct ath12k *ar,
			    struct ath12k_dbring *ring,
			    struct ath12k_dbring_cap *db_cap)
{
	struct ath12k_base *ab = ar->ab;
	struct hal_srng *srng;
	int ret;

	srng = &ab->hal.srng_list[ring->refill_srng.ring_id];
	ring->bufs_max = ring->refill_srng.size /
		ath12k_hal_srng_get_entrysize(ab, HAL_RXDMA_DIR_BUF);

	ring->buf_sz = db_cap->min_buf_sz;
	ring->buf_align = db_cap->min_buf_align;
	ring->pdev_id = db_cap->pdev_id;
	ring->hp_addr = ath12k_hal_srng_get_hp_addr(ab, srng);
	ring->tp_addr = ath12k_hal_srng_get_tp_addr(ab, srng);

	ret = ath12k_dbring_fill_bufs(ar, ring, GFP_KERNEL);

	return ret;
}

int ath12k_dbring_srng_setup(struct ath12k *ar, struct ath12k_dbring *ring,
			     int ring_num, int num_entries)
{
	int ret;

	ret = ath12k_dp_srng_setup(ar->ab, &ring->refill_srng, HAL_RXDMA_DIR_BUF,
				   ring_num, ar->pdev_idx, num_entries);
	if (ret < 0) {
		ath12k_warn(ar->ab, "failed to setup srng: %d ring_id %d\n",
			    ret, ring_num);
		goto err;
	}

	return 0;
err:
	ath12k_dp_srng_cleanup(ar->ab, &ring->refill_srng);
	return ret;
}

int ath12k_dbring_get_cap(struct ath12k_base *ab,
			  u8 pdev_idx,
			  enum wmi_direct_buffer_module id,
			  struct ath12k_dbring_cap *db_cap)
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

int ath12k_dbring_buffer_release_event(struct ath12k_base *ab,
				       struct ath12k_dbring_buf_release_event *ev)
{
	struct ath12k_dbring *ring = NULL;
	struct hal_srng *srng;
	struct ath12k *ar;
	struct ath12k_dbring_element *buff;
	struct ath12k_dbring_data handler_data;
	struct ath12k_buffer_addr desc;
	u8 *vaddr_unalign;
	u32 num_entry, num_buff_reaped;
	u8 pdev_idx, rbm;
	u32 cookie;
	int buf_id;
	int size;
	dma_addr_t paddr;
	int ret = 0;

	pdev_idx = le32_to_cpu(ev->fixed.pdev_id);

	if (pdev_idx >= ab->num_radios) {
		ath12k_warn(ab, "Invalid pdev id %d\n", pdev_idx);
		return -EINVAL;
	}

	if (ev->fixed.num_buf_release_entry !=
	    ev->fixed.num_meta_data_entry) {
		ath12k_warn(ab, "Buffer entry %d mismatch meta entry %d\n",
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
		break;
	default:
		ring = NULL;
		ath12k_warn(ab, "Recv dma buffer release ev on unsupp module %d\n",
			    ev->fixed.module_id);
		break;
	}

	if (!ring) {
		ret = -EINVAL;
		goto rcu_unlock;
	}

	srng = &ab->hal.srng_list[ring->refill_srng.ring_id];
	num_entry = le32_to_cpu(ev->fixed.num_buf_release_entry);
	size = sizeof(*buff) + ring->buf_sz + ring->buf_align - 1;
	num_buff_reaped = 0;

	spin_lock_bh(&srng->lock);

	while (num_buff_reaped < num_entry) {
		desc.info0 = ev->buf_entry[num_buff_reaped].paddr_lo;
		desc.info1 = ev->buf_entry[num_buff_reaped].paddr_hi;
		handler_data.meta = ev->meta_data[num_buff_reaped];

		num_buff_reaped++;

		ath12k_hal_rx_buf_addr_info_get(&desc, &paddr, &cookie, &rbm);

		buf_id = u32_get_bits(cookie, DP_RXDMA_BUF_COOKIE_BUF_ID);

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
		ath12k_dbring_bufs_replenish(ar, ring, buff, GFP_ATOMIC);
	}

	spin_unlock_bh(&srng->lock);

rcu_unlock:
	rcu_read_unlock();

	return ret;
}

void ath12k_dbring_srng_cleanup(struct ath12k *ar, struct ath12k_dbring *ring)
{
	ath12k_dp_srng_cleanup(ar->ab, &ring->refill_srng);
}

void ath12k_dbring_buf_cleanup(struct ath12k *ar, struct ath12k_dbring *ring)
{
	struct ath12k_dbring_element *buff;
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
