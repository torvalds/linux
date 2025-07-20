// SPDX-License-Identifier: GPL-2.0-only
/*
 * Tegra host1x Interrupt Management
 *
 * Copyright (c) 2010-2021, NVIDIA Corporation.
 */

#include <linux/clk.h>
#include <linux/interrupt.h>
#include "dev.h"
#include "fence.h"
#include "intr.h"

static void host1x_intr_add_fence_to_list(struct host1x_fence_list *list,
					  struct host1x_syncpt_fence *fence)
{
	struct host1x_syncpt_fence *fence_in_list;

	list_for_each_entry_reverse(fence_in_list, &list->list, list) {
		if ((s32)(fence_in_list->threshold - fence->threshold) <= 0) {
			/* Fence in list is before us, we can insert here */
			list_add(&fence->list, &fence_in_list->list);
			return;
		}
	}

	/* Add as first in list */
	list_add(&fence->list, &list->list);
}

static void host1x_intr_update_hw_state(struct host1x *host, struct host1x_syncpt *sp)
{
	struct host1x_syncpt_fence *fence;

	if (!list_empty(&sp->fences.list)) {
		fence = list_first_entry(&sp->fences.list, struct host1x_syncpt_fence, list);

		host1x_hw_intr_set_syncpt_threshold(host, sp->id, fence->threshold);
		host1x_hw_intr_enable_syncpt_intr(host, sp->id);
	} else {
		host1x_hw_intr_disable_syncpt_intr(host, sp->id);
	}
}

void host1x_intr_add_fence_locked(struct host1x *host, struct host1x_syncpt_fence *fence)
{
	struct host1x_fence_list *fence_list = &fence->sp->fences;

	INIT_LIST_HEAD(&fence->list);

	host1x_intr_add_fence_to_list(fence_list, fence);
	host1x_intr_update_hw_state(host, fence->sp);
}

bool host1x_intr_remove_fence(struct host1x *host, struct host1x_syncpt_fence *fence)
{
	struct host1x_fence_list *fence_list = &fence->sp->fences;
	unsigned long irqflags;

	spin_lock_irqsave(&fence_list->lock, irqflags);

	if (list_empty(&fence->list)) {
		spin_unlock_irqrestore(&fence_list->lock, irqflags);
		return false;
	}

	list_del_init(&fence->list);
	host1x_intr_update_hw_state(host, fence->sp);

	spin_unlock_irqrestore(&fence_list->lock, irqflags);

	return true;
}

void host1x_intr_handle_interrupt(struct host1x *host, unsigned int id)
{
	struct host1x_syncpt *sp = &host->syncpt[id];
	struct host1x_syncpt_fence *fence, *tmp;
	unsigned int value;

	value = host1x_syncpt_load(sp);

	spin_lock(&sp->fences.lock);

	list_for_each_entry_safe(fence, tmp, &sp->fences.list, list) {
		if (((value - fence->threshold) & 0x80000000U) != 0U) {
			/* Fence is not yet expired, we are done */
			break;
		}

		list_del_init(&fence->list);
		host1x_fence_signal(fence);
	}

	/* Re-enable interrupt if necessary */
	host1x_intr_update_hw_state(host, sp);

	spin_unlock(&sp->fences.lock);
}

int host1x_intr_init(struct host1x *host)
{
	struct host1x_intr_irq_data *irq_data;
	unsigned int id;
	int i, err;

	for (id = 0; id < host1x_syncpt_nb_pts(host); ++id) {
		struct host1x_syncpt *syncpt = &host->syncpt[id];

		spin_lock_init(&syncpt->fences.lock);
		INIT_LIST_HEAD(&syncpt->fences.list);
	}

	irq_data = devm_kcalloc(host->dev, host->num_syncpt_irqs, sizeof(irq_data[0]), GFP_KERNEL);
	if (!irq_data)
		return -ENOMEM;

	host1x_hw_intr_disable_all_syncpt_intrs(host);

	for (i = 0; i < host->num_syncpt_irqs; i++) {
		irq_data[i].host = host;
		irq_data[i].offset = i;

		err = devm_request_irq(host->dev, host->syncpt_irqs[i],
				       host->intr_op->isr, IRQF_SHARED,
				       "host1x_syncpt", &irq_data[i]);
		if (err < 0)
			return err;
	}

	return 0;
}

void host1x_intr_deinit(struct host1x *host)
{
}

void host1x_intr_start(struct host1x *host)
{
	u32 hz = clk_get_rate(host->clk);
	int err;

	mutex_lock(&host->intr_mutex);
	err = host1x_hw_intr_init_host_sync(host, DIV_ROUND_UP(hz, 1000000));
	if (err) {
		mutex_unlock(&host->intr_mutex);
		return;
	}
	mutex_unlock(&host->intr_mutex);
}

void host1x_intr_stop(struct host1x *host)
{
	host1x_hw_intr_disable_all_syncpt_intrs(host);
}
