/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Host1x context devices
 *
 * Copyright (c) 2020, NVIDIA Corporation.
 */

#ifndef __HOST1X_CONTEXT_H
#define __HOST1X_CONTEXT_H

#include <linux/mutex.h>
#include <linux/refcount.h>

struct host1x;
struct host1x_memory_context;

struct host1x_memory_context_list {
	struct mutex lock;
	struct host1x_hw_memory_context *devs;
	unsigned int len;
	struct list_head waiters;
};

struct host1x_hw_memory_context {
	struct host1x *host;

	refcount_t ref;
	struct pid *owner;

	struct device_dma_parameters dma_parms;
	struct device dev;
	u64 dma_mask;
	u32 stream_id;

	struct list_head owners;
	refcount_t active;
};

#ifdef CONFIG_IOMMU_API
int host1x_memory_context_list_init(struct host1x *host1x);
void host1x_memory_context_list_free(struct host1x_memory_context_list *cdl);
#else
static inline int host1x_memory_context_list_init(struct host1x *host1x)
{
	return 0;
}

static inline void host1x_memory_context_list_free(struct host1x_memory_context_list *cdl)
{
}
#endif

#endif
