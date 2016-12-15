/*
 * Keystone Queue Manager subsystem driver
 *
 * Copyright (C) 2014 Texas Instruments Incorporated - http://www.ti.com
 * Authors:	Sandeep Nair <sandeep_n@ti.com>
 *		Cyril Chemparathy <cyril@ti.com>
 *		Santosh Shilimkar <santosh.shilimkar@ti.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/bitops.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_device.h>
#include <linux/of_address.h>
#include <linux/pm_runtime.h>
#include <linux/firmware.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/string.h>
#include <linux/soc/ti/knav_qmss.h>

#include "knav_qmss.h"

static struct knav_device *kdev;
static DEFINE_MUTEX(knav_dev_lock);

/* Queue manager register indices in DTS */
#define KNAV_QUEUE_PEEK_REG_INDEX	0
#define KNAV_QUEUE_STATUS_REG_INDEX	1
#define KNAV_QUEUE_CONFIG_REG_INDEX	2
#define KNAV_QUEUE_REGION_REG_INDEX	3
#define KNAV_QUEUE_PUSH_REG_INDEX	4
#define KNAV_QUEUE_POP_REG_INDEX	5

/* PDSP register indices in DTS */
#define KNAV_QUEUE_PDSP_IRAM_REG_INDEX	0
#define KNAV_QUEUE_PDSP_REGS_REG_INDEX	1
#define KNAV_QUEUE_PDSP_INTD_REG_INDEX	2
#define KNAV_QUEUE_PDSP_CMD_REG_INDEX	3

#define knav_queue_idx_to_inst(kdev, idx)			\
	(kdev->instances + (idx << kdev->inst_shift))

#define for_each_handle_rcu(qh, inst)			\
	list_for_each_entry_rcu(qh, &inst->handles, list)

#define for_each_instance(idx, inst, kdev)		\
	for (idx = 0, inst = kdev->instances;		\
	     idx < (kdev)->num_queues_in_use;			\
	     idx++, inst = knav_queue_idx_to_inst(kdev, idx))

/* All firmware file names end up here. List the firmware file names below.
 * Newest followed by older ones. Search is done from start of the array
 * until a firmware file is found.
 */
const char *knav_acc_firmwares[] = {"ks2_qmss_pdsp_acc48.bin"};

/**
 * knav_queue_notify: qmss queue notfier call
 *
 * @inst:		qmss queue instance like accumulator
 */
void knav_queue_notify(struct knav_queue_inst *inst)
{
	struct knav_queue *qh;

	if (!inst)
		return;

	rcu_read_lock();
	for_each_handle_rcu(qh, inst) {
		if (atomic_read(&qh->notifier_enabled) <= 0)
			continue;
		if (WARN_ON(!qh->notifier_fn))
			continue;
		atomic_inc(&qh->stats.notifies);
		qh->notifier_fn(qh->notifier_fn_arg);
	}
	rcu_read_unlock();
}
EXPORT_SYMBOL_GPL(knav_queue_notify);

static irqreturn_t knav_queue_int_handler(int irq, void *_instdata)
{
	struct knav_queue_inst *inst = _instdata;

	knav_queue_notify(inst);
	return IRQ_HANDLED;
}

static int knav_queue_setup_irq(struct knav_range_info *range,
			  struct knav_queue_inst *inst)
{
	unsigned queue = inst->id - range->queue_base;
	unsigned long cpu_map;
	int ret = 0, irq;

	if (range->flags & RANGE_HAS_IRQ) {
		irq = range->irqs[queue].irq;
		cpu_map = range->irqs[queue].cpu_map;
		ret = request_irq(irq, knav_queue_int_handler, 0,
					inst->irq_name, inst);
		if (ret)
			return ret;
		disable_irq(irq);
		if (cpu_map) {
			ret = irq_set_affinity_hint(irq, to_cpumask(&cpu_map));
			if (ret) {
				dev_warn(range->kdev->dev,
					 "Failed to set IRQ affinity\n");
				return ret;
			}
		}
	}
	return ret;
}

static void knav_queue_free_irq(struct knav_queue_inst *inst)
{
	struct knav_range_info *range = inst->range;
	unsigned queue = inst->id - inst->range->queue_base;
	int irq;

	if (range->flags & RANGE_HAS_IRQ) {
		irq = range->irqs[queue].irq;
		irq_set_affinity_hint(irq, NULL);
		free_irq(irq, inst);
	}
}

static inline bool knav_queue_is_busy(struct knav_queue_inst *inst)
{
	return !list_empty(&inst->handles);
}

static inline bool knav_queue_is_reserved(struct knav_queue_inst *inst)
{
	return inst->range->flags & RANGE_RESERVED;
}

static inline bool knav_queue_is_shared(struct knav_queue_inst *inst)
{
	struct knav_queue *tmp;

	rcu_read_lock();
	for_each_handle_rcu(tmp, inst) {
		if (tmp->flags & KNAV_QUEUE_SHARED) {
			rcu_read_unlock();
			return true;
		}
	}
	rcu_read_unlock();
	return false;
}

static inline bool knav_queue_match_type(struct knav_queue_inst *inst,
						unsigned type)
{
	if ((type == KNAV_QUEUE_QPEND) &&
	    (inst->range->flags & RANGE_HAS_IRQ)) {
		return true;
	} else if ((type == KNAV_QUEUE_ACC) &&
		(inst->range->flags & RANGE_HAS_ACCUMULATOR)) {
		return true;
	} else if ((type == KNAV_QUEUE_GP) &&
		!(inst->range->flags &
			(RANGE_HAS_ACCUMULATOR | RANGE_HAS_IRQ))) {
		return true;
	}
	return false;
}

static inline struct knav_queue_inst *
knav_queue_match_id_to_inst(struct knav_device *kdev, unsigned id)
{
	struct knav_queue_inst *inst;
	int idx;

	for_each_instance(idx, inst, kdev) {
		if (inst->id == id)
			return inst;
	}
	return NULL;
}

static inline struct knav_queue_inst *knav_queue_find_by_id(int id)
{
	if (kdev->base_id <= id &&
	    kdev->base_id + kdev->num_queues > id) {
		id -= kdev->base_id;
		return knav_queue_match_id_to_inst(kdev, id);
	}
	return NULL;
}

static struct knav_queue *__knav_queue_open(struct knav_queue_inst *inst,
				      const char *name, unsigned flags)
{
	struct knav_queue *qh;
	unsigned id;
	int ret = 0;

	qh = devm_kzalloc(inst->kdev->dev, sizeof(*qh), GFP_KERNEL);
	if (!qh)
		return ERR_PTR(-ENOMEM);

	qh->flags = flags;
	qh->inst = inst;
	id = inst->id - inst->qmgr->start_queue;
	qh->reg_push = &inst->qmgr->reg_push[id];
	qh->reg_pop = &inst->qmgr->reg_pop[id];
	qh->reg_peek = &inst->qmgr->reg_peek[id];

	/* first opener? */
	if (!knav_queue_is_busy(inst)) {
		struct knav_range_info *range = inst->range;

		inst->name = kstrndup(name, KNAV_NAME_SIZE, GFP_KERNEL);
		if (range->ops && range->ops->open_queue)
			ret = range->ops->open_queue(range, inst, flags);

		if (ret) {
			devm_kfree(inst->kdev->dev, qh);
			return ERR_PTR(ret);
		}
	}
	list_add_tail_rcu(&qh->list, &inst->handles);
	return qh;
}

static struct knav_queue *
knav_queue_open_by_id(const char *name, unsigned id, unsigned flags)
{
	struct knav_queue_inst *inst;
	struct knav_queue *qh;

	mutex_lock(&knav_dev_lock);

	qh = ERR_PTR(-ENODEV);
	inst = knav_queue_find_by_id(id);
	if (!inst)
		goto unlock_ret;

	qh = ERR_PTR(-EEXIST);
	if (!(flags & KNAV_QUEUE_SHARED) && knav_queue_is_busy(inst))
		goto unlock_ret;

	qh = ERR_PTR(-EBUSY);
	if ((flags & KNAV_QUEUE_SHARED) &&
	    (knav_queue_is_busy(inst) && !knav_queue_is_shared(inst)))
		goto unlock_ret;

	qh = __knav_queue_open(inst, name, flags);

unlock_ret:
	mutex_unlock(&knav_dev_lock);

	return qh;
}

static struct knav_queue *knav_queue_open_by_type(const char *name,
						unsigned type, unsigned flags)
{
	struct knav_queue_inst *inst;
	struct knav_queue *qh = ERR_PTR(-EINVAL);
	int idx;

	mutex_lock(&knav_dev_lock);

	for_each_instance(idx, inst, kdev) {
		if (knav_queue_is_reserved(inst))
			continue;
		if (!knav_queue_match_type(inst, type))
			continue;
		if (knav_queue_is_busy(inst))
			continue;
		qh = __knav_queue_open(inst, name, flags);
		goto unlock_ret;
	}

unlock_ret:
	mutex_unlock(&knav_dev_lock);
	return qh;
}

static void knav_queue_set_notify(struct knav_queue_inst *inst, bool enabled)
{
	struct knav_range_info *range = inst->range;

	if (range->ops && range->ops->set_notify)
		range->ops->set_notify(range, inst, enabled);
}

static int knav_queue_enable_notifier(struct knav_queue *qh)
{
	struct knav_queue_inst *inst = qh->inst;
	bool first;

	if (WARN_ON(!qh->notifier_fn))
		return -EINVAL;

	/* Adjust the per handle notifier count */
	first = (atomic_inc_return(&qh->notifier_enabled) == 1);
	if (!first)
		return 0; /* nothing to do */

	/* Now adjust the per instance notifier count */
	first = (atomic_inc_return(&inst->num_notifiers) == 1);
	if (first)
		knav_queue_set_notify(inst, true);

	return 0;
}

static int knav_queue_disable_notifier(struct knav_queue *qh)
{
	struct knav_queue_inst *inst = qh->inst;
	bool last;

	last = (atomic_dec_return(&qh->notifier_enabled) == 0);
	if (!last)
		return 0; /* nothing to do */

	last = (atomic_dec_return(&inst->num_notifiers) == 0);
	if (last)
		knav_queue_set_notify(inst, false);

	return 0;
}

static int knav_queue_set_notifier(struct knav_queue *qh,
				struct knav_queue_notify_config *cfg)
{
	knav_queue_notify_fn old_fn = qh->notifier_fn;

	if (!cfg)
		return -EINVAL;

	if (!(qh->inst->range->flags & (RANGE_HAS_ACCUMULATOR | RANGE_HAS_IRQ)))
		return -ENOTSUPP;

	if (!cfg->fn && old_fn)
		knav_queue_disable_notifier(qh);

	qh->notifier_fn = cfg->fn;
	qh->notifier_fn_arg = cfg->fn_arg;

	if (cfg->fn && !old_fn)
		knav_queue_enable_notifier(qh);

	return 0;
}

static int knav_gp_set_notify(struct knav_range_info *range,
			       struct knav_queue_inst *inst,
			       bool enabled)
{
	unsigned queue;

	if (range->flags & RANGE_HAS_IRQ) {
		queue = inst->id - range->queue_base;
		if (enabled)
			enable_irq(range->irqs[queue].irq);
		else
			disable_irq_nosync(range->irqs[queue].irq);
	}
	return 0;
}

static int knav_gp_open_queue(struct knav_range_info *range,
				struct knav_queue_inst *inst, unsigned flags)
{
	return knav_queue_setup_irq(range, inst);
}

static int knav_gp_close_queue(struct knav_range_info *range,
				struct knav_queue_inst *inst)
{
	knav_queue_free_irq(inst);
	return 0;
}

struct knav_range_ops knav_gp_range_ops = {
	.set_notify	= knav_gp_set_notify,
	.open_queue	= knav_gp_open_queue,
	.close_queue	= knav_gp_close_queue,
};


static int knav_queue_get_count(void *qhandle)
{
	struct knav_queue *qh = qhandle;
	struct knav_queue_inst *inst = qh->inst;

	return readl_relaxed(&qh->reg_peek[0].entry_count) +
		atomic_read(&inst->desc_count);
}

static void knav_queue_debug_show_instance(struct seq_file *s,
					struct knav_queue_inst *inst)
{
	struct knav_device *kdev = inst->kdev;
	struct knav_queue *qh;

	if (!knav_queue_is_busy(inst))
		return;

	seq_printf(s, "\tqueue id %d (%s)\n",
		   kdev->base_id + inst->id, inst->name);
	for_each_handle_rcu(qh, inst) {
		seq_printf(s, "\t\thandle %p: ", qh);
		seq_printf(s, "pushes %8d, ",
			   atomic_read(&qh->stats.pushes));
		seq_printf(s, "pops %8d, ",
			   atomic_read(&qh->stats.pops));
		seq_printf(s, "count %8d, ",
			   knav_queue_get_count(qh));
		seq_printf(s, "notifies %8d, ",
			   atomic_read(&qh->stats.notifies));
		seq_printf(s, "push errors %8d, ",
			   atomic_read(&qh->stats.push_errors));
		seq_printf(s, "pop errors %8d\n",
			   atomic_read(&qh->stats.pop_errors));
	}
}

static int knav_queue_debug_show(struct seq_file *s, void *v)
{
	struct knav_queue_inst *inst;
	int idx;

	mutex_lock(&knav_dev_lock);
	seq_printf(s, "%s: %u-%u\n",
		   dev_name(kdev->dev), kdev->base_id,
		   kdev->base_id + kdev->num_queues - 1);
	for_each_instance(idx, inst, kdev)
		knav_queue_debug_show_instance(s, inst);
	mutex_unlock(&knav_dev_lock);

	return 0;
}

static int knav_queue_debug_open(struct inode *inode, struct file *file)
{
	return single_open(file, knav_queue_debug_show, NULL);
}

static const struct file_operations knav_queue_debug_ops = {
	.open		= knav_queue_debug_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static inline int knav_queue_pdsp_wait(u32 * __iomem addr, unsigned timeout,
					u32 flags)
{
	unsigned long end;
	u32 val = 0;

	end = jiffies + msecs_to_jiffies(timeout);
	while (time_after(end, jiffies)) {
		val = readl_relaxed(addr);
		if (flags)
			val &= flags;
		if (!val)
			break;
		cpu_relax();
	}
	return val ? -ETIMEDOUT : 0;
}


static int knav_queue_flush(struct knav_queue *qh)
{
	struct knav_queue_inst *inst = qh->inst;
	unsigned id = inst->id - inst->qmgr->start_queue;

	atomic_set(&inst->desc_count, 0);
	writel_relaxed(0, &inst->qmgr->reg_push[id].ptr_size_thresh);
	return 0;
}

/**
 * knav_queue_open()	- open a hardware queue
 * @name		- name to give the queue handle
 * @id			- desired queue number if any or specifes the type
 *			  of queue
 * @flags		- the following flags are applicable to queues:
 *	KNAV_QUEUE_SHARED - allow the queue to be shared. Queues are
 *			     exclusive by default.
 *			     Subsequent attempts to open a shared queue should
 *			     also have this flag.
 *
 * Returns a handle to the open hardware queue if successful. Use IS_ERR()
 * to check the returned value for error codes.
 */
void *knav_queue_open(const char *name, unsigned id,
					unsigned flags)
{
	struct knav_queue *qh = ERR_PTR(-EINVAL);

	switch (id) {
	case KNAV_QUEUE_QPEND:
	case KNAV_QUEUE_ACC:
	case KNAV_QUEUE_GP:
		qh = knav_queue_open_by_type(name, id, flags);
		break;

	default:
		qh = knav_queue_open_by_id(name, id, flags);
		break;
	}
	return qh;
}
EXPORT_SYMBOL_GPL(knav_queue_open);

/**
 * knav_queue_close()	- close a hardware queue handle
 * @qh			- handle to close
 */
void knav_queue_close(void *qhandle)
{
	struct knav_queue *qh = qhandle;
	struct knav_queue_inst *inst = qh->inst;

	while (atomic_read(&qh->notifier_enabled) > 0)
		knav_queue_disable_notifier(qh);

	mutex_lock(&knav_dev_lock);
	list_del_rcu(&qh->list);
	mutex_unlock(&knav_dev_lock);
	synchronize_rcu();
	if (!knav_queue_is_busy(inst)) {
		struct knav_range_info *range = inst->range;

		if (range->ops && range->ops->close_queue)
			range->ops->close_queue(range, inst);
	}
	devm_kfree(inst->kdev->dev, qh);
}
EXPORT_SYMBOL_GPL(knav_queue_close);

/**
 * knav_queue_device_control()	- Perform control operations on a queue
 * @qh				- queue handle
 * @cmd				- control commands
 * @arg				- command argument
 *
 * Returns 0 on success, errno otherwise.
 */
int knav_queue_device_control(void *qhandle, enum knav_queue_ctrl_cmd cmd,
				unsigned long arg)
{
	struct knav_queue *qh = qhandle;
	struct knav_queue_notify_config *cfg;
	int ret;

	switch ((int)cmd) {
	case KNAV_QUEUE_GET_ID:
		ret = qh->inst->kdev->base_id + qh->inst->id;
		break;

	case KNAV_QUEUE_FLUSH:
		ret = knav_queue_flush(qh);
		break;

	case KNAV_QUEUE_SET_NOTIFIER:
		cfg = (void *)arg;
		ret = knav_queue_set_notifier(qh, cfg);
		break;

	case KNAV_QUEUE_ENABLE_NOTIFY:
		ret = knav_queue_enable_notifier(qh);
		break;

	case KNAV_QUEUE_DISABLE_NOTIFY:
		ret = knav_queue_disable_notifier(qh);
		break;

	case KNAV_QUEUE_GET_COUNT:
		ret = knav_queue_get_count(qh);
		break;

	default:
		ret = -ENOTSUPP;
		break;
	}
	return ret;
}
EXPORT_SYMBOL_GPL(knav_queue_device_control);



/**
 * knav_queue_push()	- push data (or descriptor) to the tail of a queue
 * @qh			- hardware queue handle
 * @data		- data to push
 * @size		- size of data to push
 * @flags		- can be used to pass additional information
 *
 * Returns 0 on success, errno otherwise.
 */
int knav_queue_push(void *qhandle, dma_addr_t dma,
					unsigned size, unsigned flags)
{
	struct knav_queue *qh = qhandle;
	u32 val;

	val = (u32)dma | ((size / 16) - 1);
	writel_relaxed(val, &qh->reg_push[0].ptr_size_thresh);

	atomic_inc(&qh->stats.pushes);
	return 0;
}
EXPORT_SYMBOL_GPL(knav_queue_push);

/**
 * knav_queue_pop()	- pop data (or descriptor) from the head of a queue
 * @qh			- hardware queue handle
 * @size		- (optional) size of the data pop'ed.
 *
 * Returns a DMA address on success, 0 on failure.
 */
dma_addr_t knav_queue_pop(void *qhandle, unsigned *size)
{
	struct knav_queue *qh = qhandle;
	struct knav_queue_inst *inst = qh->inst;
	dma_addr_t dma;
	u32 val, idx;

	/* are we accumulated? */
	if (inst->descs) {
		if (unlikely(atomic_dec_return(&inst->desc_count) < 0)) {
			atomic_inc(&inst->desc_count);
			return 0;
		}
		idx  = atomic_inc_return(&inst->desc_head);
		idx &= ACC_DESCS_MASK;
		val = inst->descs[idx];
	} else {
		val = readl_relaxed(&qh->reg_pop[0].ptr_size_thresh);
		if (unlikely(!val))
			return 0;
	}

	dma = val & DESC_PTR_MASK;
	if (size)
		*size = ((val & DESC_SIZE_MASK) + 1) * 16;

	atomic_inc(&qh->stats.pops);
	return dma;
}
EXPORT_SYMBOL_GPL(knav_queue_pop);

/* carve out descriptors and push into queue */
static void kdesc_fill_pool(struct knav_pool *pool)
{
	struct knav_region *region;
	int i;

	region = pool->region;
	pool->desc_size = region->desc_size;
	for (i = 0; i < pool->num_desc; i++) {
		int index = pool->region_offset + i;
		dma_addr_t dma_addr;
		unsigned dma_size;
		dma_addr = region->dma_start + (region->desc_size * index);
		dma_size = ALIGN(pool->desc_size, SMP_CACHE_BYTES);
		dma_sync_single_for_device(pool->dev, dma_addr, dma_size,
					   DMA_TO_DEVICE);
		knav_queue_push(pool->queue, dma_addr, dma_size, 0);
	}
}

/* pop out descriptors and close the queue */
static void kdesc_empty_pool(struct knav_pool *pool)
{
	dma_addr_t dma;
	unsigned size;
	void *desc;
	int i;

	if (!pool->queue)
		return;

	for (i = 0;; i++) {
		dma = knav_queue_pop(pool->queue, &size);
		if (!dma)
			break;
		desc = knav_pool_desc_dma_to_virt(pool, dma);
		if (!desc) {
			dev_dbg(pool->kdev->dev,
				"couldn't unmap desc, continuing\n");
			continue;
		}
	}
	WARN_ON(i != pool->num_desc);
	knav_queue_close(pool->queue);
}


/* Get the DMA address of a descriptor */
dma_addr_t knav_pool_desc_virt_to_dma(void *ph, void *virt)
{
	struct knav_pool *pool = ph;
	return pool->region->dma_start + (virt - pool->region->virt_start);
}
EXPORT_SYMBOL_GPL(knav_pool_desc_virt_to_dma);

void *knav_pool_desc_dma_to_virt(void *ph, dma_addr_t dma)
{
	struct knav_pool *pool = ph;
	return pool->region->virt_start + (dma - pool->region->dma_start);
}
EXPORT_SYMBOL_GPL(knav_pool_desc_dma_to_virt);

/**
 * knav_pool_create()	- Create a pool of descriptors
 * @name		- name to give the pool handle
 * @num_desc		- numbers of descriptors in the pool
 * @region_id		- QMSS region id from which the descriptors are to be
 *			  allocated.
 *
 * Returns a pool handle on success.
 * Use IS_ERR_OR_NULL() to identify error values on return.
 */
void *knav_pool_create(const char *name,
					int num_desc, int region_id)
{
	struct knav_region *reg_itr, *region = NULL;
	struct knav_pool *pool, *pi;
	struct list_head *node;
	unsigned last_offset;
	bool slot_found;
	int ret;

	if (!kdev->dev)
		return ERR_PTR(-ENODEV);

	pool = devm_kzalloc(kdev->dev, sizeof(*pool), GFP_KERNEL);
	if (!pool) {
		dev_err(kdev->dev, "out of memory allocating pool\n");
		return ERR_PTR(-ENOMEM);
	}

	for_each_region(kdev, reg_itr) {
		if (reg_itr->id != region_id)
			continue;
		region = reg_itr;
		break;
	}

	if (!region) {
		dev_err(kdev->dev, "region-id(%d) not found\n", region_id);
		ret = -EINVAL;
		goto err;
	}

	pool->queue = knav_queue_open(name, KNAV_QUEUE_GP, 0);
	if (IS_ERR_OR_NULL(pool->queue)) {
		dev_err(kdev->dev,
			"failed to open queue for pool(%s), error %ld\n",
			name, PTR_ERR(pool->queue));
		ret = PTR_ERR(pool->queue);
		goto err;
	}

	pool->name = kstrndup(name, KNAV_NAME_SIZE, GFP_KERNEL);
	pool->kdev = kdev;
	pool->dev = kdev->dev;

	mutex_lock(&knav_dev_lock);

	if (num_desc > (region->num_desc - region->used_desc)) {
		dev_err(kdev->dev, "out of descs in region(%d) for pool(%s)\n",
			region_id, name);
		ret = -ENOMEM;
		goto err_unlock;
	}

	/* Region maintains a sorted (by region offset) list of pools
	 * use the first free slot which is large enough to accomodate
	 * the request
	 */
	last_offset = 0;
	slot_found = false;
	node = &region->pools;
	list_for_each_entry(pi, &region->pools, region_inst) {
		if ((pi->region_offset - last_offset) >= num_desc) {
			slot_found = true;
			break;
		}
		last_offset = pi->region_offset + pi->num_desc;
	}
	node = &pi->region_inst;

	if (slot_found) {
		pool->region = region;
		pool->num_desc = num_desc;
		pool->region_offset = last_offset;
		region->used_desc += num_desc;
		list_add_tail(&pool->list, &kdev->pools);
		list_add_tail(&pool->region_inst, node);
	} else {
		dev_err(kdev->dev, "pool(%s) create failed: fragmented desc pool in region(%d)\n",
			name, region_id);
		ret = -ENOMEM;
		goto err_unlock;
	}

	mutex_unlock(&knav_dev_lock);
	kdesc_fill_pool(pool);
	return pool;

err_unlock:
	mutex_unlock(&knav_dev_lock);
err:
	kfree(pool->name);
	devm_kfree(kdev->dev, pool);
	return ERR_PTR(ret);
}
EXPORT_SYMBOL_GPL(knav_pool_create);

/**
 * knav_pool_destroy()	- Free a pool of descriptors
 * @pool		- pool handle
 */
void knav_pool_destroy(void *ph)
{
	struct knav_pool *pool = ph;

	if (!pool)
		return;

	if (!pool->region)
		return;

	kdesc_empty_pool(pool);
	mutex_lock(&knav_dev_lock);

	pool->region->used_desc -= pool->num_desc;
	list_del(&pool->region_inst);
	list_del(&pool->list);

	mutex_unlock(&knav_dev_lock);
	kfree(pool->name);
	devm_kfree(kdev->dev, pool);
}
EXPORT_SYMBOL_GPL(knav_pool_destroy);


/**
 * knav_pool_desc_get()	- Get a descriptor from the pool
 * @pool			- pool handle
 *
 * Returns descriptor from the pool.
 */
void *knav_pool_desc_get(void *ph)
{
	struct knav_pool *pool = ph;
	dma_addr_t dma;
	unsigned size;
	void *data;

	dma = knav_queue_pop(pool->queue, &size);
	if (unlikely(!dma))
		return ERR_PTR(-ENOMEM);
	data = knav_pool_desc_dma_to_virt(pool, dma);
	return data;
}
EXPORT_SYMBOL_GPL(knav_pool_desc_get);

/**
 * knav_pool_desc_put()	- return a descriptor to the pool
 * @pool			- pool handle
 */
void knav_pool_desc_put(void *ph, void *desc)
{
	struct knav_pool *pool = ph;
	dma_addr_t dma;
	dma = knav_pool_desc_virt_to_dma(pool, desc);
	knav_queue_push(pool->queue, dma, pool->region->desc_size, 0);
}
EXPORT_SYMBOL_GPL(knav_pool_desc_put);

/**
 * knav_pool_desc_map()	- Map descriptor for DMA transfer
 * @pool			- pool handle
 * @desc			- address of descriptor to map
 * @size			- size of descriptor to map
 * @dma				- DMA address return pointer
 * @dma_sz			- adjusted return pointer
 *
 * Returns 0 on success, errno otherwise.
 */
int knav_pool_desc_map(void *ph, void *desc, unsigned size,
					dma_addr_t *dma, unsigned *dma_sz)
{
	struct knav_pool *pool = ph;
	*dma = knav_pool_desc_virt_to_dma(pool, desc);
	size = min(size, pool->region->desc_size);
	size = ALIGN(size, SMP_CACHE_BYTES);
	*dma_sz = size;
	dma_sync_single_for_device(pool->dev, *dma, size, DMA_TO_DEVICE);

	/* Ensure the descriptor reaches to the memory */
	__iowmb();

	return 0;
}
EXPORT_SYMBOL_GPL(knav_pool_desc_map);

/**
 * knav_pool_desc_unmap()	- Unmap descriptor after DMA transfer
 * @pool			- pool handle
 * @dma				- DMA address of descriptor to unmap
 * @dma_sz			- size of descriptor to unmap
 *
 * Returns descriptor address on success, Use IS_ERR_OR_NULL() to identify
 * error values on return.
 */
void *knav_pool_desc_unmap(void *ph, dma_addr_t dma, unsigned dma_sz)
{
	struct knav_pool *pool = ph;
	unsigned desc_sz;
	void *desc;

	desc_sz = min(dma_sz, pool->region->desc_size);
	desc = knav_pool_desc_dma_to_virt(pool, dma);
	dma_sync_single_for_cpu(pool->dev, dma, desc_sz, DMA_FROM_DEVICE);
	prefetch(desc);
	return desc;
}
EXPORT_SYMBOL_GPL(knav_pool_desc_unmap);

/**
 * knav_pool_count()	- Get the number of descriptors in pool.
 * @pool		- pool handle
 * Returns number of elements in the pool.
 */
int knav_pool_count(void *ph)
{
	struct knav_pool *pool = ph;
	return knav_queue_get_count(pool->queue);
}
EXPORT_SYMBOL_GPL(knav_pool_count);

static void knav_queue_setup_region(struct knav_device *kdev,
					struct knav_region *region)
{
	unsigned hw_num_desc, hw_desc_size, size;
	struct knav_reg_region __iomem  *regs;
	struct knav_qmgr_info *qmgr;
	struct knav_pool *pool;
	int id = region->id;
	struct page *page;

	/* unused region? */
	if (!region->num_desc) {
		dev_warn(kdev->dev, "unused region %s\n", region->name);
		return;
	}

	/* get hardware descriptor value */
	hw_num_desc = ilog2(region->num_desc - 1) + 1;

	/* did we force fit ourselves into nothingness? */
	if (region->num_desc < 32) {
		region->num_desc = 0;
		dev_warn(kdev->dev, "too few descriptors in region %s\n",
			 region->name);
		return;
	}

	size = region->num_desc * region->desc_size;
	region->virt_start = alloc_pages_exact(size, GFP_KERNEL | GFP_DMA |
						GFP_DMA32);
	if (!region->virt_start) {
		region->num_desc = 0;
		dev_err(kdev->dev, "memory alloc failed for region %s\n",
			region->name);
		return;
	}
	region->virt_end = region->virt_start + size;
	page = virt_to_page(region->virt_start);

	region->dma_start = dma_map_page(kdev->dev, page, 0, size,
					 DMA_BIDIRECTIONAL);
	if (dma_mapping_error(kdev->dev, region->dma_start)) {
		dev_err(kdev->dev, "dma map failed for region %s\n",
			region->name);
		goto fail;
	}
	region->dma_end = region->dma_start + size;

	pool = devm_kzalloc(kdev->dev, sizeof(*pool), GFP_KERNEL);
	if (!pool) {
		dev_err(kdev->dev, "out of memory allocating dummy pool\n");
		goto fail;
	}
	pool->num_desc = 0;
	pool->region_offset = region->num_desc;
	list_add(&pool->region_inst, &region->pools);

	dev_dbg(kdev->dev,
		"region %s (%d): size:%d, link:%d@%d, dma:%pad-%pad, virt:%p-%p\n",
		region->name, id, region->desc_size, region->num_desc,
		region->link_index, &region->dma_start, &region->dma_end,
		region->virt_start, region->virt_end);

	hw_desc_size = (region->desc_size / 16) - 1;
	hw_num_desc -= 5;

	for_each_qmgr(kdev, qmgr) {
		regs = qmgr->reg_region + id;
		writel_relaxed((u32)region->dma_start, &regs->base);
		writel_relaxed(region->link_index, &regs->start_index);
		writel_relaxed(hw_desc_size << 16 | hw_num_desc,
			       &regs->size_count);
	}
	return;

fail:
	if (region->dma_start)
		dma_unmap_page(kdev->dev, region->dma_start, size,
				DMA_BIDIRECTIONAL);
	if (region->virt_start)
		free_pages_exact(region->virt_start, size);
	region->num_desc = 0;
	return;
}

static const char *knav_queue_find_name(struct device_node *node)
{
	const char *name;

	if (of_property_read_string(node, "label", &name) < 0)
		name = node->name;
	if (!name)
		name = "unknown";
	return name;
}

static int knav_queue_setup_regions(struct knav_device *kdev,
					struct device_node *regions)
{
	struct device *dev = kdev->dev;
	struct knav_region *region;
	struct device_node *child;
	u32 temp[2];
	int ret;

	for_each_child_of_node(regions, child) {
		region = devm_kzalloc(dev, sizeof(*region), GFP_KERNEL);
		if (!region) {
			dev_err(dev, "out of memory allocating region\n");
			return -ENOMEM;
		}

		region->name = knav_queue_find_name(child);
		of_property_read_u32(child, "id", &region->id);
		ret = of_property_read_u32_array(child, "region-spec", temp, 2);
		if (!ret) {
			region->num_desc  = temp[0];
			region->desc_size = temp[1];
		} else {
			dev_err(dev, "invalid region info %s\n", region->name);
			devm_kfree(dev, region);
			continue;
		}

		if (!of_get_property(child, "link-index", NULL)) {
			dev_err(dev, "No link info for %s\n", region->name);
			devm_kfree(dev, region);
			continue;
		}
		ret = of_property_read_u32(child, "link-index",
					   &region->link_index);
		if (ret) {
			dev_err(dev, "link index not found for %s\n",
				region->name);
			devm_kfree(dev, region);
			continue;
		}

		INIT_LIST_HEAD(&region->pools);
		list_add_tail(&region->list, &kdev->regions);
	}
	if (list_empty(&kdev->regions)) {
		dev_err(dev, "no valid region information found\n");
		return -ENODEV;
	}

	/* Next, we run through the regions and set things up */
	for_each_region(kdev, region)
		knav_queue_setup_region(kdev, region);

	return 0;
}

static int knav_get_link_ram(struct knav_device *kdev,
				       const char *name,
				       struct knav_link_ram_block *block)
{
	struct platform_device *pdev = to_platform_device(kdev->dev);
	struct device_node *node = pdev->dev.of_node;
	u32 temp[2];

	/*
	 * Note: link ram resources are specified in "entry" sized units. In
	 * reality, although entries are ~40bits in hardware, we treat them as
	 * 64-bit entities here.
	 *
	 * For example, to specify the internal link ram for Keystone-I class
	 * devices, we would set the linkram0 resource to 0x80000-0x83fff.
	 *
	 * This gets a bit weird when other link rams are used.  For example,
	 * if the range specified is 0x0c000000-0x0c003fff (i.e., 16K entries
	 * in MSMC SRAM), the actual memory used is 0x0c000000-0x0c020000,
	 * which accounts for 64-bits per entry, for 16K entries.
	 */
	if (!of_property_read_u32_array(node, name , temp, 2)) {
		if (temp[0]) {
			/*
			 * queue_base specified => using internal or onchip
			 * link ram WARNING - we do not "reserve" this block
			 */
			block->dma = (dma_addr_t)temp[0];
			block->virt = NULL;
			block->size = temp[1];
		} else {
			block->size = temp[1];
			/* queue_base not specific => allocate requested size */
			block->virt = dmam_alloc_coherent(kdev->dev,
						  8 * block->size, &block->dma,
						  GFP_KERNEL);
			if (!block->virt) {
				dev_err(kdev->dev, "failed to alloc linkram\n");
				return -ENOMEM;
			}
		}
	} else {
		return -ENODEV;
	}
	return 0;
}

static int knav_queue_setup_link_ram(struct knav_device *kdev)
{
	struct knav_link_ram_block *block;
	struct knav_qmgr_info *qmgr;

	for_each_qmgr(kdev, qmgr) {
		block = &kdev->link_rams[0];
		dev_dbg(kdev->dev, "linkram0: dma:%pad, virt:%p, size:%x\n",
			&block->dma, block->virt, block->size);
		writel_relaxed((u32)block->dma, &qmgr->reg_config->link_ram_base0);
		writel_relaxed(block->size, &qmgr->reg_config->link_ram_size0);

		block++;
		if (!block->size)
			continue;

		dev_dbg(kdev->dev, "linkram1: dma:%pad, virt:%p, size:%x\n",
			&block->dma, block->virt, block->size);
		writel_relaxed(block->dma, &qmgr->reg_config->link_ram_base1);
	}

	return 0;
}

static int knav_setup_queue_range(struct knav_device *kdev,
					struct device_node *node)
{
	struct device *dev = kdev->dev;
	struct knav_range_info *range;
	struct knav_qmgr_info *qmgr;
	u32 temp[2], start, end, id, index;
	int ret, i;

	range = devm_kzalloc(dev, sizeof(*range), GFP_KERNEL);
	if (!range) {
		dev_err(dev, "out of memory allocating range\n");
		return -ENOMEM;
	}

	range->kdev = kdev;
	range->name = knav_queue_find_name(node);
	ret = of_property_read_u32_array(node, "qrange", temp, 2);
	if (!ret) {
		range->queue_base = temp[0] - kdev->base_id;
		range->num_queues = temp[1];
	} else {
		dev_err(dev, "invalid queue range %s\n", range->name);
		devm_kfree(dev, range);
		return -EINVAL;
	}

	for (i = 0; i < RANGE_MAX_IRQS; i++) {
		struct of_phandle_args oirq;

		if (of_irq_parse_one(node, i, &oirq))
			break;

		range->irqs[i].irq = irq_create_of_mapping(&oirq);
		if (range->irqs[i].irq == IRQ_NONE)
			break;

		range->num_irqs++;

		if (IS_ENABLED(CONFIG_SMP) && oirq.args_count == 3)
			range->irqs[i].cpu_map =
				(oirq.args[2] & 0x0000ff00) >> 8;
	}

	range->num_irqs = min(range->num_irqs, range->num_queues);
	if (range->num_irqs)
		range->flags |= RANGE_HAS_IRQ;

	if (of_get_property(node, "qalloc-by-id", NULL))
		range->flags |= RANGE_RESERVED;

	if (of_get_property(node, "accumulator", NULL)) {
		ret = knav_init_acc_range(kdev, node, range);
		if (ret < 0) {
			devm_kfree(dev, range);
			return ret;
		}
	} else {
		range->ops = &knav_gp_range_ops;
	}

	/* set threshold to 1, and flush out the queues */
	for_each_qmgr(kdev, qmgr) {
		start = max(qmgr->start_queue, range->queue_base);
		end   = min(qmgr->start_queue + qmgr->num_queues,
			    range->queue_base + range->num_queues);
		for (id = start; id < end; id++) {
			index = id - qmgr->start_queue;
			writel_relaxed(THRESH_GTE | 1,
				       &qmgr->reg_peek[index].ptr_size_thresh);
			writel_relaxed(0,
				       &qmgr->reg_push[index].ptr_size_thresh);
		}
	}

	list_add_tail(&range->list, &kdev->queue_ranges);
	dev_dbg(dev, "added range %s: %d-%d, %d irqs%s%s%s\n",
		range->name, range->queue_base,
		range->queue_base + range->num_queues - 1,
		range->num_irqs,
		(range->flags & RANGE_HAS_IRQ) ? ", has irq" : "",
		(range->flags & RANGE_RESERVED) ? ", reserved" : "",
		(range->flags & RANGE_HAS_ACCUMULATOR) ? ", acc" : "");
	kdev->num_queues_in_use += range->num_queues;
	return 0;
}

static int knav_setup_queue_pools(struct knav_device *kdev,
				   struct device_node *queue_pools)
{
	struct device_node *type, *range;
	int ret;

	for_each_child_of_node(queue_pools, type) {
		for_each_child_of_node(type, range) {
			ret = knav_setup_queue_range(kdev, range);
			/* return value ignored, we init the rest... */
		}
	}

	/* ... and barf if they all failed! */
	if (list_empty(&kdev->queue_ranges)) {
		dev_err(kdev->dev, "no valid queue range found\n");
		return -ENODEV;
	}
	return 0;
}

static void knav_free_queue_range(struct knav_device *kdev,
				  struct knav_range_info *range)
{
	if (range->ops && range->ops->free_range)
		range->ops->free_range(range);
	list_del(&range->list);
	devm_kfree(kdev->dev, range);
}

static void knav_free_queue_ranges(struct knav_device *kdev)
{
	struct knav_range_info *range;

	for (;;) {
		range = first_queue_range(kdev);
		if (!range)
			break;
		knav_free_queue_range(kdev, range);
	}
}

static void knav_queue_free_regions(struct knav_device *kdev)
{
	struct knav_region *region;
	struct knav_pool *pool, *tmp;
	unsigned size;

	for (;;) {
		region = first_region(kdev);
		if (!region)
			break;
		list_for_each_entry_safe(pool, tmp, &region->pools, region_inst)
			knav_pool_destroy(pool);

		size = region->virt_end - region->virt_start;
		if (size)
			free_pages_exact(region->virt_start, size);
		list_del(&region->list);
		devm_kfree(kdev->dev, region);
	}
}

static void __iomem *knav_queue_map_reg(struct knav_device *kdev,
					struct device_node *node, int index)
{
	struct resource res;
	void __iomem *regs;
	int ret;

	ret = of_address_to_resource(node, index, &res);
	if (ret) {
		dev_err(kdev->dev, "Can't translate of node(%s) address for index(%d)\n",
			node->name, index);
		return ERR_PTR(ret);
	}

	regs = devm_ioremap_resource(kdev->dev, &res);
	if (IS_ERR(regs))
		dev_err(kdev->dev, "Failed to map register base for index(%d) node(%s)\n",
			index, node->name);
	return regs;
}

static int knav_queue_init_qmgrs(struct knav_device *kdev,
					struct device_node *qmgrs)
{
	struct device *dev = kdev->dev;
	struct knav_qmgr_info *qmgr;
	struct device_node *child;
	u32 temp[2];
	int ret;

	for_each_child_of_node(qmgrs, child) {
		qmgr = devm_kzalloc(dev, sizeof(*qmgr), GFP_KERNEL);
		if (!qmgr) {
			dev_err(dev, "out of memory allocating qmgr\n");
			return -ENOMEM;
		}

		ret = of_property_read_u32_array(child, "managed-queues",
						 temp, 2);
		if (!ret) {
			qmgr->start_queue = temp[0];
			qmgr->num_queues = temp[1];
		} else {
			dev_err(dev, "invalid qmgr queue range\n");
			devm_kfree(dev, qmgr);
			continue;
		}

		dev_info(dev, "qmgr start queue %d, number of queues %d\n",
			 qmgr->start_queue, qmgr->num_queues);

		qmgr->reg_peek =
			knav_queue_map_reg(kdev, child,
					   KNAV_QUEUE_PEEK_REG_INDEX);
		qmgr->reg_status =
			knav_queue_map_reg(kdev, child,
					   KNAV_QUEUE_STATUS_REG_INDEX);
		qmgr->reg_config =
			knav_queue_map_reg(kdev, child,
					   KNAV_QUEUE_CONFIG_REG_INDEX);
		qmgr->reg_region =
			knav_queue_map_reg(kdev, child,
					   KNAV_QUEUE_REGION_REG_INDEX);
		qmgr->reg_push =
			knav_queue_map_reg(kdev, child,
					   KNAV_QUEUE_PUSH_REG_INDEX);
		qmgr->reg_pop =
			knav_queue_map_reg(kdev, child,
					   KNAV_QUEUE_POP_REG_INDEX);

		if (IS_ERR(qmgr->reg_peek) || IS_ERR(qmgr->reg_status) ||
		    IS_ERR(qmgr->reg_config) || IS_ERR(qmgr->reg_region) ||
		    IS_ERR(qmgr->reg_push) || IS_ERR(qmgr->reg_pop)) {
			dev_err(dev, "failed to map qmgr regs\n");
			if (!IS_ERR(qmgr->reg_peek))
				devm_iounmap(dev, qmgr->reg_peek);
			if (!IS_ERR(qmgr->reg_status))
				devm_iounmap(dev, qmgr->reg_status);
			if (!IS_ERR(qmgr->reg_config))
				devm_iounmap(dev, qmgr->reg_config);
			if (!IS_ERR(qmgr->reg_region))
				devm_iounmap(dev, qmgr->reg_region);
			if (!IS_ERR(qmgr->reg_push))
				devm_iounmap(dev, qmgr->reg_push);
			if (!IS_ERR(qmgr->reg_pop))
				devm_iounmap(dev, qmgr->reg_pop);
			devm_kfree(dev, qmgr);
			continue;
		}

		list_add_tail(&qmgr->list, &kdev->qmgrs);
		dev_info(dev, "added qmgr start queue %d, num of queues %d, reg_peek %p, reg_status %p, reg_config %p, reg_region %p, reg_push %p, reg_pop %p\n",
			 qmgr->start_queue, qmgr->num_queues,
			 qmgr->reg_peek, qmgr->reg_status,
			 qmgr->reg_config, qmgr->reg_region,
			 qmgr->reg_push, qmgr->reg_pop);
	}
	return 0;
}

static int knav_queue_init_pdsps(struct knav_device *kdev,
					struct device_node *pdsps)
{
	struct device *dev = kdev->dev;
	struct knav_pdsp_info *pdsp;
	struct device_node *child;

	for_each_child_of_node(pdsps, child) {
		pdsp = devm_kzalloc(dev, sizeof(*pdsp), GFP_KERNEL);
		if (!pdsp) {
			dev_err(dev, "out of memory allocating pdsp\n");
			return -ENOMEM;
		}
		pdsp->name = knav_queue_find_name(child);
		pdsp->iram =
			knav_queue_map_reg(kdev, child,
					   KNAV_QUEUE_PDSP_IRAM_REG_INDEX);
		pdsp->regs =
			knav_queue_map_reg(kdev, child,
					   KNAV_QUEUE_PDSP_REGS_REG_INDEX);
		pdsp->intd =
			knav_queue_map_reg(kdev, child,
					   KNAV_QUEUE_PDSP_INTD_REG_INDEX);
		pdsp->command =
			knav_queue_map_reg(kdev, child,
					   KNAV_QUEUE_PDSP_CMD_REG_INDEX);

		if (IS_ERR(pdsp->command) || IS_ERR(pdsp->iram) ||
		    IS_ERR(pdsp->regs) || IS_ERR(pdsp->intd)) {
			dev_err(dev, "failed to map pdsp %s regs\n",
				pdsp->name);
			if (!IS_ERR(pdsp->command))
				devm_iounmap(dev, pdsp->command);
			if (!IS_ERR(pdsp->iram))
				devm_iounmap(dev, pdsp->iram);
			if (!IS_ERR(pdsp->regs))
				devm_iounmap(dev, pdsp->regs);
			if (!IS_ERR(pdsp->intd))
				devm_iounmap(dev, pdsp->intd);
			devm_kfree(dev, pdsp);
			continue;
		}
		of_property_read_u32(child, "id", &pdsp->id);
		list_add_tail(&pdsp->list, &kdev->pdsps);
		dev_dbg(dev, "added pdsp %s: command %p, iram %p, regs %p, intd %p\n",
			pdsp->name, pdsp->command, pdsp->iram, pdsp->regs,
			pdsp->intd);
	}
	return 0;
}

static int knav_queue_stop_pdsp(struct knav_device *kdev,
			  struct knav_pdsp_info *pdsp)
{
	u32 val, timeout = 1000;
	int ret;

	val = readl_relaxed(&pdsp->regs->control) & ~PDSP_CTRL_ENABLE;
	writel_relaxed(val, &pdsp->regs->control);
	ret = knav_queue_pdsp_wait(&pdsp->regs->control, timeout,
					PDSP_CTRL_RUNNING);
	if (ret < 0) {
		dev_err(kdev->dev, "timed out on pdsp %s stop\n", pdsp->name);
		return ret;
	}
	pdsp->loaded = false;
	pdsp->started = false;
	return 0;
}

static int knav_queue_load_pdsp(struct knav_device *kdev,
			  struct knav_pdsp_info *pdsp)
{
	int i, ret, fwlen;
	const struct firmware *fw;
	bool found = false;
	u32 *fwdata;

	for (i = 0; i < ARRAY_SIZE(knav_acc_firmwares); i++) {
		if (knav_acc_firmwares[i]) {
			ret = request_firmware_direct(&fw,
						      knav_acc_firmwares[i],
						      kdev->dev);
			if (!ret) {
				found = true;
				break;
			}
		}
	}

	if (!found) {
		dev_err(kdev->dev, "failed to get firmware for pdsp\n");
		return -ENODEV;
	}

	dev_info(kdev->dev, "firmware file %s downloaded for PDSP\n",
		 knav_acc_firmwares[i]);

	writel_relaxed(pdsp->id + 1, pdsp->command + 0x18);
	/* download the firmware */
	fwdata = (u32 *)fw->data;
	fwlen = (fw->size + sizeof(u32) - 1) / sizeof(u32);
	for (i = 0; i < fwlen; i++)
		writel_relaxed(be32_to_cpu(fwdata[i]), pdsp->iram + i);

	release_firmware(fw);
	return 0;
}

static int knav_queue_start_pdsp(struct knav_device *kdev,
			   struct knav_pdsp_info *pdsp)
{
	u32 val, timeout = 1000;
	int ret;

	/* write a command for sync */
	writel_relaxed(0xffffffff, pdsp->command);
	while (readl_relaxed(pdsp->command) != 0xffffffff)
		cpu_relax();

	/* soft reset the PDSP */
	val  = readl_relaxed(&pdsp->regs->control);
	val &= ~(PDSP_CTRL_PC_MASK | PDSP_CTRL_SOFT_RESET);
	writel_relaxed(val, &pdsp->regs->control);

	/* enable pdsp */
	val = readl_relaxed(&pdsp->regs->control) | PDSP_CTRL_ENABLE;
	writel_relaxed(val, &pdsp->regs->control);

	/* wait for command register to clear */
	ret = knav_queue_pdsp_wait(pdsp->command, timeout, 0);
	if (ret < 0) {
		dev_err(kdev->dev,
			"timed out on pdsp %s command register wait\n",
			pdsp->name);
		return ret;
	}
	return 0;
}

static void knav_queue_stop_pdsps(struct knav_device *kdev)
{
	struct knav_pdsp_info *pdsp;

	/* disable all pdsps */
	for_each_pdsp(kdev, pdsp)
		knav_queue_stop_pdsp(kdev, pdsp);
}

static int knav_queue_start_pdsps(struct knav_device *kdev)
{
	struct knav_pdsp_info *pdsp;
	int ret;

	knav_queue_stop_pdsps(kdev);
	/* now load them all. We return success even if pdsp
	 * is not loaded as acc channels are optional on having
	 * firmware availability in the system. We set the loaded
	 * and stated flag and when initialize the acc range, check
	 * it and init the range only if pdsp is started.
	 */
	for_each_pdsp(kdev, pdsp) {
		ret = knav_queue_load_pdsp(kdev, pdsp);
		if (!ret)
			pdsp->loaded = true;
	}

	for_each_pdsp(kdev, pdsp) {
		if (pdsp->loaded) {
			ret = knav_queue_start_pdsp(kdev, pdsp);
			if (!ret)
				pdsp->started = true;
		}
	}
	return 0;
}

static inline struct knav_qmgr_info *knav_find_qmgr(unsigned id)
{
	struct knav_qmgr_info *qmgr;

	for_each_qmgr(kdev, qmgr) {
		if ((id >= qmgr->start_queue) &&
		    (id < qmgr->start_queue + qmgr->num_queues))
			return qmgr;
	}
	return NULL;
}

static int knav_queue_init_queue(struct knav_device *kdev,
					struct knav_range_info *range,
					struct knav_queue_inst *inst,
					unsigned id)
{
	char irq_name[KNAV_NAME_SIZE];
	inst->qmgr = knav_find_qmgr(id);
	if (!inst->qmgr)
		return -1;

	INIT_LIST_HEAD(&inst->handles);
	inst->kdev = kdev;
	inst->range = range;
	inst->irq_num = -1;
	inst->id = id;
	scnprintf(irq_name, sizeof(irq_name), "hwqueue-%d", id);
	inst->irq_name = kstrndup(irq_name, sizeof(irq_name), GFP_KERNEL);

	if (range->ops && range->ops->init_queue)
		return range->ops->init_queue(range, inst);
	else
		return 0;
}

static int knav_queue_init_queues(struct knav_device *kdev)
{
	struct knav_range_info *range;
	int size, id, base_idx;
	int idx = 0, ret = 0;

	/* how much do we need for instance data? */
	size = sizeof(struct knav_queue_inst);

	/* round this up to a power of 2, keep the index to instance
	 * arithmetic fast.
	 * */
	kdev->inst_shift = order_base_2(size);
	size = (1 << kdev->inst_shift) * kdev->num_queues_in_use;
	kdev->instances = devm_kzalloc(kdev->dev, size, GFP_KERNEL);
	if (!kdev->instances)
		return -ENOMEM;

	for_each_queue_range(kdev, range) {
		if (range->ops && range->ops->init_range)
			range->ops->init_range(range);
		base_idx = idx;
		for (id = range->queue_base;
		     id < range->queue_base + range->num_queues; id++, idx++) {
			ret = knav_queue_init_queue(kdev, range,
					knav_queue_idx_to_inst(kdev, idx), id);
			if (ret < 0)
				return ret;
		}
		range->queue_base_inst =
			knav_queue_idx_to_inst(kdev, base_idx);
	}
	return 0;
}

static int knav_queue_probe(struct platform_device *pdev)
{
	struct device_node *node = pdev->dev.of_node;
	struct device_node *qmgrs, *queue_pools, *regions, *pdsps;
	struct device *dev = &pdev->dev;
	u32 temp[2];
	int ret;

	if (!node) {
		dev_err(dev, "device tree info unavailable\n");
		return -ENODEV;
	}

	kdev = devm_kzalloc(dev, sizeof(struct knav_device), GFP_KERNEL);
	if (!kdev) {
		dev_err(dev, "memory allocation failed\n");
		return -ENOMEM;
	}

	platform_set_drvdata(pdev, kdev);
	kdev->dev = dev;
	INIT_LIST_HEAD(&kdev->queue_ranges);
	INIT_LIST_HEAD(&kdev->qmgrs);
	INIT_LIST_HEAD(&kdev->pools);
	INIT_LIST_HEAD(&kdev->regions);
	INIT_LIST_HEAD(&kdev->pdsps);

	pm_runtime_enable(&pdev->dev);
	ret = pm_runtime_get_sync(&pdev->dev);
	if (ret < 0) {
		dev_err(dev, "Failed to enable QMSS\n");
		return ret;
	}

	if (of_property_read_u32_array(node, "queue-range", temp, 2)) {
		dev_err(dev, "queue-range not specified\n");
		ret = -ENODEV;
		goto err;
	}
	kdev->base_id    = temp[0];
	kdev->num_queues = temp[1];

	/* Initialize queue managers using device tree configuration */
	qmgrs =  of_get_child_by_name(node, "qmgrs");
	if (!qmgrs) {
		dev_err(dev, "queue manager info not specified\n");
		ret = -ENODEV;
		goto err;
	}
	ret = knav_queue_init_qmgrs(kdev, qmgrs);
	of_node_put(qmgrs);
	if (ret)
		goto err;

	/* get pdsp configuration values from device tree */
	pdsps =  of_get_child_by_name(node, "pdsps");
	if (pdsps) {
		ret = knav_queue_init_pdsps(kdev, pdsps);
		if (ret)
			goto err;

		ret = knav_queue_start_pdsps(kdev);
		if (ret)
			goto err;
	}
	of_node_put(pdsps);

	/* get usable queue range values from device tree */
	queue_pools = of_get_child_by_name(node, "queue-pools");
	if (!queue_pools) {
		dev_err(dev, "queue-pools not specified\n");
		ret = -ENODEV;
		goto err;
	}
	ret = knav_setup_queue_pools(kdev, queue_pools);
	of_node_put(queue_pools);
	if (ret)
		goto err;

	ret = knav_get_link_ram(kdev, "linkram0", &kdev->link_rams[0]);
	if (ret) {
		dev_err(kdev->dev, "could not setup linking ram\n");
		goto err;
	}

	ret = knav_get_link_ram(kdev, "linkram1", &kdev->link_rams[1]);
	if (ret) {
		/*
		 * nothing really, we have one linking ram already, so we just
		 * live within our means
		 */
	}

	ret = knav_queue_setup_link_ram(kdev);
	if (ret)
		goto err;

	regions =  of_get_child_by_name(node, "descriptor-regions");
	if (!regions) {
		dev_err(dev, "descriptor-regions not specified\n");
		goto err;
	}
	ret = knav_queue_setup_regions(kdev, regions);
	of_node_put(regions);
	if (ret)
		goto err;

	ret = knav_queue_init_queues(kdev);
	if (ret < 0) {
		dev_err(dev, "hwqueue initialization failed\n");
		goto err;
	}

	debugfs_create_file("qmss", S_IFREG | S_IRUGO, NULL, NULL,
			    &knav_queue_debug_ops);
	return 0;

err:
	knav_queue_stop_pdsps(kdev);
	knav_queue_free_regions(kdev);
	knav_free_queue_ranges(kdev);
	pm_runtime_put_sync(&pdev->dev);
	pm_runtime_disable(&pdev->dev);
	return ret;
}

static int knav_queue_remove(struct platform_device *pdev)
{
	/* TODO: Free resources */
	pm_runtime_put_sync(&pdev->dev);
	pm_runtime_disable(&pdev->dev);
	return 0;
}

/* Match table for of_platform binding */
static struct of_device_id keystone_qmss_of_match[] = {
	{ .compatible = "ti,keystone-navigator-qmss", },
	{},
};
MODULE_DEVICE_TABLE(of, keystone_qmss_of_match);

static struct platform_driver keystone_qmss_driver = {
	.probe		= knav_queue_probe,
	.remove		= knav_queue_remove,
	.driver		= {
		.name	= "keystone-navigator-qmss",
		.of_match_table = keystone_qmss_of_match,
	},
};
module_platform_driver(keystone_qmss_driver);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("TI QMSS driver for Keystone SOCs");
MODULE_AUTHOR("Sandeep Nair <sandeep_n@ti.com>");
MODULE_AUTHOR("Santosh Shilimkar <santosh.shilimkar@ti.com>");
