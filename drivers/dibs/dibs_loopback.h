/* SPDX-License-Identifier: GPL-2.0 */
/*
 *  dibs loopback (aka loopback-ism) device structure definitions.
 *
 *  Copyright (c) 2024, Alibaba Inc.
 *
 *  Author: Wen Gu <guwen@linux.alibaba.com>
 *          Tony Lu <tonylu@linux.alibaba.com>
 *
 */

#ifndef _DIBS_LOOPBACK_H
#define _DIBS_LOOPBACK_H

#include <linux/dibs.h>
#include <linux/hashtable.h>
#include <linux/spinlock.h>
#include <linux/types.h>
#include <linux/wait.h>

#if IS_ENABLED(CONFIG_DIBS_LO)
#define DIBS_LO_DMBS_HASH_BITS	12
#define DIBS_LO_MAX_DMBS	5000

struct dibs_lo_dmb_node {
	struct hlist_node list;
	u64 token;
	u32 len;
	u32 sba_idx;
	void *cpu_addr;
	dma_addr_t dma_addr;
	refcount_t refcnt;
};

struct dibs_lo_dev {
	struct dibs_dev *dibs;
	atomic_t dmb_cnt;
	rwlock_t dmb_ht_lock;
	DECLARE_BITMAP(sba_idx_mask, DIBS_LO_MAX_DMBS);
	DECLARE_HASHTABLE(dmb_ht, DIBS_LO_DMBS_HASH_BITS);
	wait_queue_head_t ldev_release;
};

int dibs_loopback_init(void);
void dibs_loopback_exit(void);
#else
static inline int dibs_loopback_init(void)
{
	return 0;
}

static inline void dibs_loopback_exit(void)
{
}
#endif

#endif /* _DIBS_LOOPBACK_H */
