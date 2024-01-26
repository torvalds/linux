/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Copyright (c) 2019-2020 The Linux Foundation. All rights reserved.
 * Copyright (c) 2021 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef ATH11K_DBRING_H
#define ATH11K_DBRING_H

#include <linux/types.h>
#include <linux/idr.h>
#include <linux/spinlock.h>
#include "dp.h"

struct ath11k_dbring_element {
	dma_addr_t paddr;
	u8 *payload;
};

struct ath11k_dbring_data {
	void *data;
	u32 data_sz;
	struct wmi_dma_buf_release_meta_data meta;
};

struct ath11k_dbring_buf_release_event {
	struct ath11k_wmi_dma_buf_release_fixed_param fixed;
	struct wmi_dma_buf_release_entry *buf_entry;
	struct wmi_dma_buf_release_meta_data *meta_data;
	u32 num_buf_entry;
	u32 num_meta;
};

struct ath11k_dbring_cap {
	u32 pdev_id;
	enum wmi_direct_buffer_module id;
	u32 min_elem;
	u32 min_buf_sz;
	u32 min_buf_align;
};

struct ath11k_dbring {
	struct dp_srng refill_srng;
	struct idr bufs_idr;
	/* Protects bufs_idr */
	spinlock_t idr_lock;
	dma_addr_t tp_addr;
	dma_addr_t hp_addr;
	int bufs_max;
	u32 pdev_id;
	u32 buf_sz;
	u32 buf_align;
	u32 num_resp_per_event;
	u32 event_timeout_ms;
	int (*handler)(struct ath11k *, struct ath11k_dbring_data *);
};

int ath11k_dbring_set_cfg(struct ath11k *ar,
			  struct ath11k_dbring *ring,
			  u32 num_resp_per_event,
			  u32 event_timeout_ms,
			  int (*handler)(struct ath11k *,
					 struct ath11k_dbring_data *));
int ath11k_dbring_wmi_cfg_setup(struct ath11k *ar,
				struct ath11k_dbring *ring,
				enum wmi_direct_buffer_module id);
int ath11k_dbring_buf_setup(struct ath11k *ar,
			    struct ath11k_dbring *ring,
			    struct ath11k_dbring_cap *db_cap);
int ath11k_dbring_srng_setup(struct ath11k *ar, struct ath11k_dbring *ring,
			     int ring_num, int num_entries);
int ath11k_dbring_buffer_release_event(struct ath11k_base *ab,
				       struct ath11k_dbring_buf_release_event *ev);
int ath11k_dbring_get_cap(struct ath11k_base *ab,
			  u8 pdev_idx,
			  enum wmi_direct_buffer_module id,
			  struct ath11k_dbring_cap *db_cap);
void ath11k_dbring_srng_cleanup(struct ath11k *ar, struct ath11k_dbring *ring);
void ath11k_dbring_buf_cleanup(struct ath11k *ar, struct ath11k_dbring *ring);
int ath11k_dbring_validate_buffer(struct ath11k *ar, void *data, u32 size);

#endif /* ATH11K_DBRING_H */
