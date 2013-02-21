/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * Copyright (C) 2012 ARM Limited
 */

#define pr_fmt(fmt) "vexpress-config: " fmt

#include <linux/bitops.h>
#include <linux/completion.h>
#include <linux/export.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/vexpress.h>


#define VEXPRESS_CONFIG_MAX_BRIDGES 2

struct vexpress_config_bridge {
	struct device_node *node;
	struct vexpress_config_bridge_info *info;
	struct list_head transactions;
	spinlock_t transactions_lock;
} vexpress_config_bridges[VEXPRESS_CONFIG_MAX_BRIDGES];

static DECLARE_BITMAP(vexpress_config_bridges_map,
		ARRAY_SIZE(vexpress_config_bridges));
static DEFINE_MUTEX(vexpress_config_bridges_mutex);

struct vexpress_config_bridge *vexpress_config_bridge_register(
		struct device_node *node,
		struct vexpress_config_bridge_info *info)
{
	struct vexpress_config_bridge *bridge;
	int i;

	pr_debug("Registering bridge '%s'\n", info->name);

	mutex_lock(&vexpress_config_bridges_mutex);
	i = find_first_zero_bit(vexpress_config_bridges_map,
			ARRAY_SIZE(vexpress_config_bridges));
	if (i >= ARRAY_SIZE(vexpress_config_bridges)) {
		pr_err("Can't register more bridges!\n");
		mutex_unlock(&vexpress_config_bridges_mutex);
		return NULL;
	}
	__set_bit(i, vexpress_config_bridges_map);
	bridge = &vexpress_config_bridges[i];

	bridge->node = node;
	bridge->info = info;
	INIT_LIST_HEAD(&bridge->transactions);
	spin_lock_init(&bridge->transactions_lock);

	mutex_unlock(&vexpress_config_bridges_mutex);

	return bridge;
}
EXPORT_SYMBOL(vexpress_config_bridge_register);

void vexpress_config_bridge_unregister(struct vexpress_config_bridge *bridge)
{
	struct vexpress_config_bridge __bridge = *bridge;
	int i;

	mutex_lock(&vexpress_config_bridges_mutex);
	for (i = 0; i < ARRAY_SIZE(vexpress_config_bridges); i++)
		if (&vexpress_config_bridges[i] == bridge)
			__clear_bit(i, vexpress_config_bridges_map);
	mutex_unlock(&vexpress_config_bridges_mutex);

	WARN_ON(!list_empty(&__bridge.transactions));
	while (!list_empty(&__bridge.transactions))
		cpu_relax();
}
EXPORT_SYMBOL(vexpress_config_bridge_unregister);


struct vexpress_config_func {
	struct vexpress_config_bridge *bridge;
	void *func;
};

struct vexpress_config_func *__vexpress_config_func_get(struct device *dev,
		struct device_node *node)
{
	struct device_node *bridge_node;
	struct vexpress_config_func *func;
	int i;

	if (WARN_ON(dev && node && dev->of_node != node))
		return NULL;
	if (dev && !node)
		node = dev->of_node;

	func = kzalloc(sizeof(*func), GFP_KERNEL);
	if (!func)
		return NULL;

	bridge_node = of_node_get(node);
	while (bridge_node) {
		const __be32 *prop = of_get_property(bridge_node,
				"arm,vexpress,config-bridge", NULL);

		if (prop) {
			bridge_node = of_find_node_by_phandle(
					be32_to_cpup(prop));
			break;
		}

		bridge_node = of_get_next_parent(bridge_node);
	}

	mutex_lock(&vexpress_config_bridges_mutex);
	for (i = 0; i < ARRAY_SIZE(vexpress_config_bridges); i++) {
		struct vexpress_config_bridge *bridge =
				&vexpress_config_bridges[i];

		if (test_bit(i, vexpress_config_bridges_map) &&
				bridge->node == bridge_node) {
			func->bridge = bridge;
			func->func = bridge->info->func_get(dev, node);
			break;
		}
	}
	mutex_unlock(&vexpress_config_bridges_mutex);

	if (!func->func) {
		of_node_put(node);
		kfree(func);
		return NULL;
	}

	return func;
}
EXPORT_SYMBOL(__vexpress_config_func_get);

void vexpress_config_func_put(struct vexpress_config_func *func)
{
	func->bridge->info->func_put(func->func);
	of_node_put(func->bridge->node);
	kfree(func);
}
EXPORT_SYMBOL(vexpress_config_func_put);

struct vexpress_config_trans {
	struct vexpress_config_func *func;
	int offset;
	bool write;
	u32 *data;
	int status;
	struct completion completion;
	struct list_head list;
};

static void vexpress_config_dump_trans(const char *what,
		struct vexpress_config_trans *trans)
{
	pr_debug("%s %s trans %p func 0x%p offset %d data 0x%x status %d\n",
			what, trans->write ? "write" : "read", trans,
			trans->func->func, trans->offset,
			trans->data ? *trans->data : 0, trans->status);
}

static int vexpress_config_schedule(struct vexpress_config_trans *trans)
{
	int status;
	struct vexpress_config_bridge *bridge = trans->func->bridge;
	unsigned long flags;

	init_completion(&trans->completion);
	trans->status = -EFAULT;

	spin_lock_irqsave(&bridge->transactions_lock, flags);

	vexpress_config_dump_trans("Executing", trans);

	if (list_empty(&bridge->transactions))
		status = bridge->info->func_exec(trans->func->func,
				trans->offset, trans->write, trans->data);
	else
		status = VEXPRESS_CONFIG_STATUS_WAIT;

	switch (status) {
	case VEXPRESS_CONFIG_STATUS_DONE:
		vexpress_config_dump_trans("Finished", trans);
		trans->status = status;
		break;
	case VEXPRESS_CONFIG_STATUS_WAIT:
		list_add_tail(&trans->list, &bridge->transactions);
		break;
	}

	spin_unlock_irqrestore(&bridge->transactions_lock, flags);

	return status;
}

void vexpress_config_complete(struct vexpress_config_bridge *bridge,
		int status)
{
	struct vexpress_config_trans *trans;
	unsigned long flags;

	spin_lock_irqsave(&bridge->transactions_lock, flags);

	trans = list_first_entry(&bridge->transactions,
			struct vexpress_config_trans, list);
	vexpress_config_dump_trans("Completed", trans);

	trans->status = status;
	list_del(&trans->list);

	if (!list_empty(&bridge->transactions)) {
		vexpress_config_dump_trans("Pending", trans);

		bridge->info->func_exec(trans->func->func, trans->offset,
				trans->write, trans->data);
	}
	spin_unlock_irqrestore(&bridge->transactions_lock, flags);

	complete(&trans->completion);
}
EXPORT_SYMBOL(vexpress_config_complete);

int vexpress_config_wait(struct vexpress_config_trans *trans)
{
	wait_for_completion(&trans->completion);

	return trans->status;
}
EXPORT_SYMBOL(vexpress_config_wait);

int vexpress_config_read(struct vexpress_config_func *func, int offset,
		u32 *data)
{
	struct vexpress_config_trans trans = {
		.func = func,
		.offset = offset,
		.write = false,
		.data = data,
		.status = 0,
	};
	int status = vexpress_config_schedule(&trans);

	if (status == VEXPRESS_CONFIG_STATUS_WAIT)
		status = vexpress_config_wait(&trans);

	return status;
}
EXPORT_SYMBOL(vexpress_config_read);

int vexpress_config_write(struct vexpress_config_func *func, int offset,
		u32 data)
{
	struct vexpress_config_trans trans = {
		.func = func,
		.offset = offset,
		.write = true,
		.data = &data,
		.status = 0,
	};
	int status = vexpress_config_schedule(&trans);

	if (status == VEXPRESS_CONFIG_STATUS_WAIT)
		status = vexpress_config_wait(&trans);

	return status;
}
EXPORT_SYMBOL(vexpress_config_write);
