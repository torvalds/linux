/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
 */
#ifndef __GH_HCALL_VIRTIO_H
#define __GH_HCALL_VIRTIO_H

#include <linux/err.h>
#include <linux/types.h>

#include <linux/gunyah/hcall_common.h>
#include <linux/gunyah/gh_common.h>
#include <asm/gunyah/hcall.h>

static inline int
gh_hcall_virtio_mmio_backend_assert_virq(gh_capid_t capid, u64 int_status)
{
	struct gh_hcall_resp _resp = {0};

	return _gh_hcall(0x604e, (struct gh_hcall_args){ capid, int_status, 0 },
			 &_resp);
}

static inline int
gh_hcall_virtio_mmio_backend_set_dev_features(gh_capid_t capid,
				u64 features_sel, u64 features)
{
	struct gh_hcall_resp _resp = {0};

	return _gh_hcall(0x604f,
			 (struct gh_hcall_args){ capid, features_sel, features,
						 0 },
			 &_resp);
}

static inline int
gh_hcall_virtio_mmio_backend_set_queue_num_max(gh_capid_t capid,
				u64 queue_sel, u64 queue_num_max)
{
	struct gh_hcall_resp _resp = {0};

	return _gh_hcall(0x6050,
			 (struct gh_hcall_args){ capid, queue_sel,
						 queue_num_max, 0 },
			 &_resp);
}

static inline int
gh_hcall_virtio_mmio_backend_get_drv_features(gh_capid_t capid,
			u64 features_sel, u64 *features)
{
	int ret;
	struct gh_hcall_resp _resp = {0};

	ret = _gh_hcall(0x6051,
			(struct gh_hcall_args){ capid, features_sel, 0},
			&_resp);

	if (!ret && features)
		*features = _resp.resp1;

	return ret;
}

struct gh_hcall_virtio_queue_info {
	u64 queue_num;
	u64 queue_ready;
	u64 queue_desc;
	u64 queue_driver;
	u64 queue_device;
};

static inline int
gh_hcall_virtio_mmio_backend_get_queue_info(gh_capid_t capid,
		u64 queue_sel, struct gh_hcall_virtio_queue_info *queue_info)
{
	int ret;
	struct gh_hcall_resp _resp = {0};

	ret = _gh_hcall(0x6052,
			(struct gh_hcall_args){ capid, queue_sel, 0},
			&_resp);

	if (!ret && queue_info) {
		queue_info->queue_num		= _resp.resp1;
		queue_info->queue_ready		= _resp.resp2;
		queue_info->queue_desc		= _resp.resp3;
		queue_info->queue_driver	= _resp.resp4;
		queue_info->queue_device	= _resp.resp5;
	}

	return ret;
}

static inline int
gh_hcall_virtio_mmio_backend_get_event(gh_capid_t capid,
			u64 *event_data, u64 *event)
{
	int ret;
	struct gh_hcall_resp _resp = {0};

	ret = _gh_hcall(0x6053,
			(struct gh_hcall_args){ capid, 0},
			&_resp);

	if (!ret && event_data)
		*event_data = _resp.resp1;
	if (!ret && event)
		*event = _resp.resp2;

	return ret;
}

static inline int
gh_hcall_virtio_mmio_backend_ack_reset(gh_capid_t capid)
{
	struct gh_hcall_resp _resp = {0};

	return _gh_hcall(0x6054,
			(struct gh_hcall_args){ capid, 0},
			&_resp);
}

#endif
