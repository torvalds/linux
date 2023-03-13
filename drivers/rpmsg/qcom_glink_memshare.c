// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/list.h>
#include <linux/types.h>
#include <linux/slab.h>

struct qcom_glink_mem_entry {
	struct device *dev;
	void *va;
	dma_addr_t dma;
	size_t len;
	u32 da;
	struct list_head node;
};

static DEFINE_SPINLOCK(qcom_glink_mem_entry_lock);
static LIST_HEAD(qcom_glink_mem_entries);

struct qcom_glink_mem_entry *
qcom_glink_mem_entry_init(struct device *dev, void *va, dma_addr_t dma, size_t len, u32 da)
{
	struct qcom_glink_mem_entry *mem = NULL;
	unsigned long flags;

	mem = kzalloc(sizeof(*mem), GFP_KERNEL);
	if (!mem)
		return mem;

	mem->dev = dev;
	mem->va = va;
	mem->dma = dma;
	mem->da = da;
	mem->len = len;
	INIT_LIST_HEAD(&mem->node);

	spin_lock_irqsave(&qcom_glink_mem_entry_lock, flags);
	list_add_tail(&mem->node, &qcom_glink_mem_entries);
	spin_unlock_irqrestore(&qcom_glink_mem_entry_lock, flags);

	return mem;
}
EXPORT_SYMBOL(qcom_glink_mem_entry_init);

void qcom_glink_mem_entry_free(struct qcom_glink_mem_entry *mem)
{
	struct qcom_glink_mem_entry *entry, *tmp;
	unsigned long flags;

	spin_lock_irqsave(&qcom_glink_mem_entry_lock, flags);
	list_for_each_entry_safe(entry, tmp, &qcom_glink_mem_entries, node) {
		if (entry == mem) {
			list_del(&mem->node);
			break;
		}
	}
	spin_unlock_irqrestore(&qcom_glink_mem_entry_lock, flags);

	kfree(mem);
}
EXPORT_SYMBOL(qcom_glink_mem_entry_free);

void *qcom_glink_prepare_da_for_cpu(u64 da, size_t len)
{
	struct qcom_glink_mem_entry *mem;
	unsigned long flags;
	void *ptr = NULL;

	spin_lock_irqsave(&qcom_glink_mem_entry_lock, flags);
	list_for_each_entry(mem, &qcom_glink_mem_entries, node) {
		int offset = da - mem->da;

		if (!mem->va)
			continue;

		if (offset < 0)
			continue;

		if (offset + len > mem->len)
			continue;

		ptr = mem->va + offset;
		dma_sync_single_for_cpu(mem->dev, da, len, DMA_FROM_DEVICE);

		break;
	}
	spin_unlock_irqrestore(&qcom_glink_mem_entry_lock, flags);

	return ptr;
}
EXPORT_SYMBOL(qcom_glink_prepare_da_for_cpu);
