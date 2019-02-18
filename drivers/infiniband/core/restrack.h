/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/*
 * Copyright (c) 2017-2019 Mellanox Technologies. All rights reserved.
 */

#ifndef _RDMA_CORE_RESTRACK_H_
#define _RDMA_CORE_RESTRACK_H_

#include <linux/mutex.h>
#include <linux/rwsem.h>

/**
 * struct rdma_restrack_root - main resource tracking management
 * entity, per-device
 */
struct rdma_restrack_root {
	/*
	 * @rwsem: Read/write lock to protect erase of entry.
	 * Lists and insertions are protected by XArray internal lock.
	 */
	struct rw_semaphore	rwsem;
	/**
	 * @xa: Array of XArray structures to hold restrack entries.
	 * We want to use array of XArrays because insertion is type
	 * dependent. For types with xisiting unique ID (like QPN),
	 * we will insert to that unique index. For other types,
	 * we insert based on pointers and auto-allocate unique index.
	 */
	struct xarray xa[RDMA_RESTRACK_MAX];
	/**
	 * @next_id: Next ID to support cyclic allocation
	 */
	u32 next_id[RDMA_RESTRACK_MAX];
};


int rdma_restrack_init(struct ib_device *dev);
void rdma_restrack_clean(struct ib_device *dev);
#endif /* _RDMA_CORE_RESTRACK_H_ */
