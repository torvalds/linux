// SPDX-License-Identifier: GPL-2.0-only
/*
 * Keystone accumulator queue manager
 *
 * Copyright (C) 2014 Texas Instruments Incorporated - http://www.ti.com
 * Author:	Sandeep Nair <sandeep_n@ti.com>
 *		Cyril Chemparathy <cyril@ti.com>
 *		Santosh Shilimkar <santosh.shilimkar@ti.com>
 */

#include <linux/dma-mapping.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/soc/ti/knav_qmss.h>

#include "knav_qmss.h"

#define knav_range_offset_to_inst(kdev, range, q)	\
	(range->queue_base_inst + (q << kdev->inst_shift))

static void __knav_acc_notify(struct knav_range_info *range,
				struct knav_acc_channel *acc)
{
	struct knav_device *kdev = range->kdev;
	struct knav_queue_inst *inst;
	int range_base, queue;

	range_base = kdev->base_id + range->queue_base;

	if (range->flags & RANGE_MULTI_QUEUE) {
		for (queue = 0; queue < range->num_queues; queue++) {
			inst = knav_range_offset_to_inst(kdev, range,
								queue);
			if (inst->notify_needed) {
				inst->notify_needed = 0;
				dev_dbg(kdev->dev, "acc-irq: notifying %d\n",
					range_base + queue);
				knav_queue_notify(inst);
			}
		}
	} else {
		queue = acc->channel - range->acc_info.start_channel;
		inst = knav_range_offset_to_inst(kdev, range, queue);
		dev_dbg(kdev->dev, "acc-irq: notifying %d\n",
			range_base + queue);
		knav_queue_notify(inst);
	}
}

static int knav_acc_set_notify(struct knav_range_info *range,
				struct knav_queue_inst *kq,
				bool enabled)
{
	struct knav_pdsp_info *pdsp = range->acc_info.pdsp;
	struct knav_device *kdev = range->kdev;
	u32 mask, offset;

	/*
	 * when enabling, we need to re-trigger an interrupt if we
	 * have descriptors pending
	 */
	if (!enabled || atomic_read(&kq->desc_count) <= 0)
		return 0;

	kq->notify_needed = 1;
	atomic_inc(&kq->acc->retrigger_count);
	mask = BIT(kq->acc->channel % 32);
	offset = ACC_INTD_OFFSET_STATUS(kq->acc->channel);
	dev_dbg(kdev->dev, "setup-notify: re-triggering irq for %s\n",
		kq->acc->name);
	writel_relaxed(mask, pdsp->intd + offset);
	return 0;
}

static irqreturn_t knav_acc_int_handler(int irq, void *_instdata)
{
	struct knav_acc_channel *acc;
	struct knav_queue_inst *kq = NULL;
	struct knav_range_info *range;
	struct knav_pdsp_info *pdsp;
	struct knav_acc_info *info;
	struct knav_device *kdev;

	u32 *list, *list_cpu, val, idx, notifies;
	int range_base, channel, queue = 0;
	dma_addr_t list_dma;

	range = _instdata;
	info  = &range->acc_info;
	kdev  = range->kdev;
	pdsp  = range->acc_info.pdsp;
	acc   = range->acc;

	range_base = kdev->base_id + range->queue_base;
	if ((range->flags & RANGE_MULTI_QUEUE) == 0) {
		for (queue = 0; queue < range->num_irqs; queue++)
			if (range->irqs[queue].irq == irq)
				break;
		kq = knav_range_offset_to_inst(kdev, range, queue);
		acc += queue;
	}

	channel = acc->channel;
	list_dma = acc->list_dma[acc->list_index];
	list_cpu = acc->list_cpu[acc->list_index];
	dev_dbg(kdev->dev, "acc-irq: channel %d, list %d, virt %p, dma %pad\n",
		channel, acc->list_index, list_cpu, &list_dma);
	if (atomic_read(&acc->retrigger_count)) {
		atomic_dec(&acc->retrigger_count);
		__knav_acc_notify(range, acc);
		writel_relaxed(1, pdsp->intd + ACC_INTD_OFFSET_COUNT(channel));
		/* ack the interrupt */
		writel_relaxed(ACC_CHANNEL_INT_BASE + channel,
			       pdsp->intd + ACC_INTD_OFFSET_EOI);

		return IRQ_HANDLED;
	}

	notifies = readl_relaxed(pdsp->intd + ACC_INTD_OFFSET_COUNT(channel));
	WARN_ON(!notifies);
	dma_sync_single_for_cpu(kdev->dev, list_dma, info->list_size,
				DMA_FROM_DEVICE);

	for (list = list_cpu; list < list_cpu + (info->list_size / sizeof(u32));
	     list += ACC_LIST_ENTRY_WORDS) {
		if (ACC_LIST_ENTRY_WORDS == 1) {
			dev_dbg(kdev->dev,
				"acc-irq: list %d, entry @%p, %08x\n",
				acc->list_index, list, list[0]);
		} else if (ACC_LIST_ENTRY_WORDS == 2) {
			dev_dbg(kdev->dev,
				"acc-irq: list %d, entry @%p, %08x %08x\n",
				acc->list_index, list, list[0], list[1]);
		} else if (ACC_LIST_ENTRY_WORDS == 4) {
			dev_dbg(kdev->dev,
				"acc-irq: list %d, entry @%p, %08x %08x %08x %08x\n",
				acc->list_index, list, list[0], list[1],
				list[2], list[3]);
		}

		val = list[ACC_LIST_ENTRY_DESC_IDX];
		if (!val)
			break;

		if (range->flags & RANGE_MULTI_QUEUE) {
			queue = list[ACC_LIST_ENTRY_QUEUE_IDX] >> 16;
			if (queue < range_base ||
			    queue >= range_base + range->num_queues) {
				dev_err(kdev->dev,
					"bad queue %d, expecting %d-%d\n",
					queue, range_base,
					range_base + range->num_queues);
				break;
			}
			queue -= range_base;
			kq = knav_range_offset_to_inst(kdev, range,
								queue);
		}

		if (atomic_inc_return(&kq->desc_count) >= ACC_DESCS_MAX) {
			atomic_dec(&kq->desc_count);
			dev_err(kdev->dev,
				"acc-irq: queue %d full, entry dropped\n",
				queue + range_base);
			continue;
		}

		idx = atomic_inc_return(&kq->desc_tail) & ACC_DESCS_MASK;
		kq->descs[idx] = val;
		kq->notify_needed = 1;
		dev_dbg(kdev->dev, "acc-irq: enqueue %08x at %d, queue %d\n",
			val, idx, queue + range_base);
	}

	__knav_acc_notify(range, acc);
	memset(list_cpu, 0, info->list_size);
	dma_sync_single_for_device(kdev->dev, list_dma, info->list_size,
				   DMA_TO_DEVICE);

	/* flip to the other list */
	acc->list_index ^= 1;

	/* reset the interrupt counter */
	writel_relaxed(1, pdsp->intd + ACC_INTD_OFFSET_COUNT(channel));

	/* ack the interrupt */
	writel_relaxed(ACC_CHANNEL_INT_BASE + channel,
		       pdsp->intd + ACC_INTD_OFFSET_EOI);

	return IRQ_HANDLED;
}

static int knav_range_setup_acc_irq(struct knav_range_info *range,
				int queue, bool enabled)
{
	struct knav_device *kdev = range->kdev;
	struct knav_acc_channel *acc;
	struct cpumask *cpu_mask;
	int ret = 0, irq;
	u32 old, new;

	if (range->flags & RANGE_MULTI_QUEUE) {
		acc = range->acc;
		irq = range->irqs[0].irq;
		cpu_mask = range->irqs[0].cpu_mask;
	} else {
		acc = range->acc + queue;
		irq = range->irqs[queue].irq;
		cpu_mask = range->irqs[queue].cpu_mask;
	}

	old = acc->open_mask;
	if (enabled)
		new = old | BIT(queue);
	else
		new = old & ~BIT(queue);
	acc->open_mask = new;

	dev_dbg(kdev->dev,
		"setup-acc-irq: open mask old %08x, new %08x, channel %s\n",
		old, new, acc->name);

	if (likely(new == old))
		return 0;

	if (new && !old) {
		dev_dbg(kdev->dev,
			"setup-acc-irq: requesting %s for channel %s\n",
			acc->name, acc->name);
		ret = request_irq(irq, knav_acc_int_handler, 0, acc->name,
				  range);
		if (!ret && cpu_mask) {
			ret = irq_set_affinity_hint(irq, cpu_mask);
			if (ret) {
				dev_warn(range->kdev->dev,
					 "Failed to set IRQ affinity\n");
				return ret;
			}
		}
	}

	if (old && !new) {
		dev_dbg(kdev->dev, "setup-acc-irq: freeing %s for channel %s\n",
			acc->name, acc->name);
		ret = irq_set_affinity_hint(irq, NULL);
		if (ret)
			dev_warn(range->kdev->dev,
				 "Failed to set IRQ affinity\n");
		free_irq(irq, range);
	}

	return ret;
}

static const char *knav_acc_result_str(enum knav_acc_result result)
{
	static const char * const result_str[] = {
		[ACC_RET_IDLE]			= "idle",
		[ACC_RET_SUCCESS]		= "success",
		[ACC_RET_INVALID_COMMAND]	= "invalid command",
		[ACC_RET_INVALID_CHANNEL]	= "invalid channel",
		[ACC_RET_INACTIVE_CHANNEL]	= "inactive channel",
		[ACC_RET_ACTIVE_CHANNEL]	= "active channel",
		[ACC_RET_INVALID_QUEUE]		= "invalid queue",
		[ACC_RET_INVALID_RET]		= "invalid return code",
	};

	if (result >= ARRAY_SIZE(result_str))
		return result_str[ACC_RET_INVALID_RET];
	else
		return result_str[result];
}

static enum knav_acc_result
knav_acc_write(struct knav_device *kdev, struct knav_pdsp_info *pdsp,
		struct knav_reg_acc_command *cmd)
{
	u32 result;

	dev_dbg(kdev->dev, "acc command %08x %08x %08x %08x %08x\n",
		cmd->command, cmd->queue_mask, cmd->list_dma,
		cmd->queue_num, cmd->timer_config);

	writel_relaxed(cmd->timer_config, &pdsp->acc_command->timer_config);
	writel_relaxed(cmd->queue_num, &pdsp->acc_command->queue_num);
	writel_relaxed(cmd->list_dma, &pdsp->acc_command->list_dma);
	writel_relaxed(cmd->queue_mask, &pdsp->acc_command->queue_mask);
	writel_relaxed(cmd->command, &pdsp->acc_command->command);

	/* wait for the command to clear */
	do {
		result = readl_relaxed(&pdsp->acc_command->command);
	} while ((result >> 8) & 0xff);

	return (result >> 24) & 0xff;
}

static void knav_acc_setup_cmd(struct knav_device *kdev,
				struct knav_range_info *range,
				struct knav_reg_acc_command *cmd,
				int queue)
{
	struct knav_acc_info *info = &range->acc_info;
	struct knav_acc_channel *acc;
	int queue_base;
	u32 queue_mask;

	if (range->flags & RANGE_MULTI_QUEUE) {
		acc = range->acc;
		queue_base = range->queue_base;
		queue_mask = BIT(range->num_queues) - 1;
	} else {
		acc = range->acc + queue;
		queue_base = range->queue_base + queue;
		queue_mask = 0;
	}

	memset(cmd, 0, sizeof(*cmd));
	cmd->command    = acc->channel;
	cmd->queue_mask = queue_mask;
	cmd->list_dma   = (u32)acc->list_dma[0];
	cmd->queue_num  = info->list_entries << 16;
	cmd->queue_num |= queue_base;

	cmd->timer_config = ACC_LIST_ENTRY_TYPE << 18;
	if (range->flags & RANGE_MULTI_QUEUE)
		cmd->timer_config |= ACC_CFG_MULTI_QUEUE;
	cmd->timer_config |= info->pacing_mode << 16;
	cmd->timer_config |= info->timer_count;
}

static void knav_acc_stop(struct knav_device *kdev,
				struct knav_range_info *range,
				int queue)
{
	struct knav_reg_acc_command cmd;
	struct knav_acc_channel *acc;
	enum knav_acc_result result;

	acc = range->acc + queue;

	knav_acc_setup_cmd(kdev, range, &cmd, queue);
	cmd.command |= ACC_CMD_DISABLE_CHANNEL << 8;
	result = knav_acc_write(kdev, range->acc_info.pdsp, &cmd);

	dev_dbg(kdev->dev, "stopped acc channel %s, result %s\n",
		acc->name, knav_acc_result_str(result));
}

static enum knav_acc_result knav_acc_start(struct knav_device *kdev,
						struct knav_range_info *range,
						int queue)
{
	struct knav_reg_acc_command cmd;
	struct knav_acc_channel *acc;
	enum knav_acc_result result;

	acc = range->acc + queue;

	knav_acc_setup_cmd(kdev, range, &cmd, queue);
	cmd.command |= ACC_CMD_ENABLE_CHANNEL << 8;
	result = knav_acc_write(kdev, range->acc_info.pdsp, &cmd);

	dev_dbg(kdev->dev, "started acc channel %s, result %s\n",
		acc->name, knav_acc_result_str(result));

	return result;
}

static int knav_acc_init_range(struct knav_range_info *range)
{
	struct knav_device *kdev = range->kdev;
	struct knav_acc_channel *acc;
	enum knav_acc_result result;
	int queue;

	for (queue = 0; queue < range->num_queues; queue++) {
		acc = range->acc + queue;

		knav_acc_stop(kdev, range, queue);
		acc->list_index = 0;
		result = knav_acc_start(kdev, range, queue);

		if (result != ACC_RET_SUCCESS)
			return -EIO;

		if (range->flags & RANGE_MULTI_QUEUE)
			return 0;
	}
	return 0;
}

static int knav_acc_init_queue(struct knav_range_info *range,
				struct knav_queue_inst *kq)
{
	unsigned id = kq->id - range->queue_base;

	kq->descs = devm_kcalloc(range->kdev->dev,
				 ACC_DESCS_MAX, sizeof(u32), GFP_KERNEL);
	if (!kq->descs)
		return -ENOMEM;

	kq->acc = range->acc;
	if ((range->flags & RANGE_MULTI_QUEUE) == 0)
		kq->acc += id;
	return 0;
}

static int knav_acc_open_queue(struct knav_range_info *range,
				struct knav_queue_inst *inst, unsigned flags)
{
	unsigned id = inst->id - range->queue_base;

	return knav_range_setup_acc_irq(range, id, true);
}

static int knav_acc_close_queue(struct knav_range_info *range,
					struct knav_queue_inst *inst)
{
	unsigned id = inst->id - range->queue_base;

	return knav_range_setup_acc_irq(range, id, false);
}

static int knav_acc_free_range(struct knav_range_info *range)
{
	struct knav_device *kdev = range->kdev;
	struct knav_acc_channel *acc;
	struct knav_acc_info *info;
	int channel, channels;

	info = &range->acc_info;

	if (range->flags & RANGE_MULTI_QUEUE)
		channels = 1;
	else
		channels = range->num_queues;

	for (channel = 0; channel < channels; channel++) {
		acc = range->acc + channel;
		if (!acc->list_cpu[0])
			continue;
		dma_unmap_single(kdev->dev, acc->list_dma[0],
				 info->mem_size, DMA_BIDIRECTIONAL);
		free_pages_exact(acc->list_cpu[0], info->mem_size);
	}
	devm_kfree(range->kdev->dev, range->acc);
	return 0;
}

static const struct knav_range_ops knav_acc_range_ops = {
	.set_notify	= knav_acc_set_notify,
	.init_queue	= knav_acc_init_queue,
	.open_queue	= knav_acc_open_queue,
	.close_queue	= knav_acc_close_queue,
	.init_range	= knav_acc_init_range,
	.free_range	= knav_acc_free_range,
};

/**
 * knav_init_acc_range: Initialise accumulator ranges
 *
 * @kdev:		qmss device
 * @node:		device node
 * @range:		qmms range information
 *
 * Return 0 on success or error
 */
int knav_init_acc_range(struct knav_device *kdev,
			struct device_node *node,
			struct knav_range_info *range)
{
	struct knav_acc_channel *acc;
	struct knav_pdsp_info *pdsp;
	struct knav_acc_info *info;
	int ret, channel, channels;
	int list_size, mem_size;
	dma_addr_t list_dma;
	void *list_mem;
	u32 config[5];

	range->flags |= RANGE_HAS_ACCUMULATOR;
	info = &range->acc_info;

	ret = of_property_read_u32_array(node, "accumulator", config, 5);
	if (ret)
		return ret;

	info->pdsp_id		= config[0];
	info->start_channel	= config[1];
	info->list_entries	= config[2];
	info->pacing_mode	= config[3];
	info->timer_count	= config[4] / ACC_DEFAULT_PERIOD;

	if (info->start_channel > ACC_MAX_CHANNEL) {
		dev_err(kdev->dev, "channel %d invalid for range %s\n",
			info->start_channel, range->name);
		return -EINVAL;
	}

	if (info->pacing_mode > 3) {
		dev_err(kdev->dev, "pacing mode %d invalid for range %s\n",
			info->pacing_mode, range->name);
		return -EINVAL;
	}

	pdsp = knav_find_pdsp(kdev, info->pdsp_id);
	if (!pdsp) {
		dev_err(kdev->dev, "pdsp id %d not found for range %s\n",
			info->pdsp_id, range->name);
		return -EINVAL;
	}

	if (!pdsp->started) {
		dev_err(kdev->dev, "pdsp id %d not started for range %s\n",
			info->pdsp_id, range->name);
		return -ENODEV;
	}

	info->pdsp = pdsp;
	channels = range->num_queues;
	if (of_property_read_bool(node, "multi-queue")) {
		range->flags |= RANGE_MULTI_QUEUE;
		channels = 1;
		if (range->queue_base & (32 - 1)) {
			dev_err(kdev->dev,
				"misaligned multi-queue accumulator range %s\n",
				range->name);
			return -EINVAL;
		}
		if (range->num_queues > 32) {
			dev_err(kdev->dev,
				"too many queues in accumulator range %s\n",
				range->name);
			return -EINVAL;
		}
	}

	/* figure out list size */
	list_size  = info->list_entries;
	list_size *= ACC_LIST_ENTRY_WORDS * sizeof(u32);
	info->list_size = list_size;
	mem_size   = PAGE_ALIGN(list_size * 2);
	info->mem_size  = mem_size;
	range->acc = devm_kcalloc(kdev->dev, channels, sizeof(*range->acc),
				  GFP_KERNEL);
	if (!range->acc)
		return -ENOMEM;

	for (channel = 0; channel < channels; channel++) {
		acc = range->acc + channel;
		acc->channel = info->start_channel + channel;

		/* allocate memory for the two lists */
		list_mem = alloc_pages_exact(mem_size, GFP_KERNEL | GFP_DMA);
		if (!list_mem)
			return -ENOMEM;

		list_dma = dma_map_single(kdev->dev, list_mem, mem_size,
					  DMA_BIDIRECTIONAL);
		if (dma_mapping_error(kdev->dev, list_dma)) {
			free_pages_exact(list_mem, mem_size);
			return -ENOMEM;
		}

		memset(list_mem, 0, mem_size);
		dma_sync_single_for_device(kdev->dev, list_dma, mem_size,
					   DMA_TO_DEVICE);
		scnprintf(acc->name, sizeof(acc->name), "hwqueue-acc-%d",
			  acc->channel);
		acc->list_cpu[0] = list_mem;
		acc->list_cpu[1] = list_mem + list_size;
		acc->list_dma[0] = list_dma;
		acc->list_dma[1] = list_dma + list_size;
		dev_dbg(kdev->dev, "%s: channel %d, dma %pad, virt %8p\n",
			acc->name, acc->channel, &list_dma, list_mem);
	}

	range->ops = &knav_acc_range_ops;
	return 0;
}
EXPORT_SYMBOL_GPL(knav_init_acc_range);
