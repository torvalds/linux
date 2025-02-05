// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2023, STMicroelectronics - All Rights Reserved
 */

#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/bus/stm32_firewall_device.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/types.h>
#include <linux/slab.h>

#include "stm32_firewall.h"

/* Corresponds to STM32_FIREWALL_MAX_EXTRA_ARGS + firewall ID */
#define STM32_FIREWALL_MAX_ARGS		(STM32_FIREWALL_MAX_EXTRA_ARGS + 1)

static LIST_HEAD(firewall_controller_list);
static DEFINE_MUTEX(firewall_controller_list_lock);

/* Firewall device API */

int stm32_firewall_get_firewall(struct device_node *np, struct stm32_firewall *firewall,
				unsigned int nb_firewall)
{
	struct stm32_firewall_controller *ctrl;
	struct of_phandle_iterator it;
	unsigned int i, j = 0;
	int err;

	if (!firewall || !nb_firewall)
		return -EINVAL;

	/* Parse property with phandle parsed out */
	of_for_each_phandle(&it, err, np, "access-controllers", "#access-controller-cells", 0) {
		struct of_phandle_args provider_args;
		struct device_node *provider = it.node;
		const char *fw_entry;
		bool match = false;

		if (err) {
			pr_err("Unable to get access-controllers property for node %s\n, err: %d",
			       np->full_name, err);
			of_node_put(provider);
			return err;
		}

		if (j >= nb_firewall) {
			pr_err("Too many firewall controllers");
			of_node_put(provider);
			return -EINVAL;
		}

		provider_args.args_count = of_phandle_iterator_args(&it, provider_args.args,
								    STM32_FIREWALL_MAX_ARGS);

		/* Check if the parsed phandle corresponds to a registered firewall controller */
		mutex_lock(&firewall_controller_list_lock);
		list_for_each_entry(ctrl, &firewall_controller_list, entry) {
			if (ctrl->dev->of_node->phandle == it.phandle) {
				match = true;
				firewall[j].firewall_ctrl = ctrl;
				break;
			}
		}
		mutex_unlock(&firewall_controller_list_lock);

		if (!match) {
			firewall[j].firewall_ctrl = NULL;
			pr_err("No firewall controller registered for %s\n", np->full_name);
			of_node_put(provider);
			return -ENODEV;
		}

		err = of_property_read_string_index(np, "access-controller-names", j, &fw_entry);
		if (err == 0)
			firewall[j].entry = fw_entry;

		/* Handle the case when there are no arguments given along with the phandle */
		if (provider_args.args_count < 0 ||
		    provider_args.args_count > STM32_FIREWALL_MAX_ARGS) {
			of_node_put(provider);
			return -EINVAL;
		} else if (provider_args.args_count == 0) {
			firewall[j].extra_args_size = 0;
			firewall[j].firewall_id = U32_MAX;
			j++;
			continue;
		}

		/* The firewall ID is always the first argument */
		firewall[j].firewall_id = provider_args.args[0];

		/* Extra args start at the second argument */
		for (i = 0; i < provider_args.args_count - 1; i++)
			firewall[j].extra_args[i] = provider_args.args[i + 1];

		/* Remove the firewall ID arg that is not an extra argument */
		firewall[j].extra_args_size = provider_args.args_count - 1;

		j++;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(stm32_firewall_get_firewall);

int stm32_firewall_grant_access(struct stm32_firewall *firewall)
{
	struct stm32_firewall_controller *firewall_controller;

	if (!firewall || firewall->firewall_id == U32_MAX)
		return -EINVAL;

	firewall_controller = firewall->firewall_ctrl;

	if (!firewall_controller)
		return -ENODEV;

	return firewall_controller->grant_access(firewall_controller, firewall->firewall_id);
}
EXPORT_SYMBOL_GPL(stm32_firewall_grant_access);

int stm32_firewall_grant_access_by_id(struct stm32_firewall *firewall, u32 subsystem_id)
{
	struct stm32_firewall_controller *firewall_controller;

	if (!firewall || subsystem_id == U32_MAX || firewall->firewall_id == U32_MAX)
		return -EINVAL;

	firewall_controller = firewall->firewall_ctrl;

	if (!firewall_controller)
		return -ENODEV;

	return firewall_controller->grant_access(firewall_controller, subsystem_id);
}
EXPORT_SYMBOL_GPL(stm32_firewall_grant_access_by_id);

void stm32_firewall_release_access(struct stm32_firewall *firewall)
{
	struct stm32_firewall_controller *firewall_controller;

	if (!firewall || firewall->firewall_id == U32_MAX) {
		pr_debug("Incorrect arguments when releasing a firewall access\n");
		return;
	}

	firewall_controller = firewall->firewall_ctrl;

	if (!firewall_controller) {
		pr_debug("No firewall controller to release\n");
		return;
	}

	firewall_controller->release_access(firewall_controller, firewall->firewall_id);
}
EXPORT_SYMBOL_GPL(stm32_firewall_release_access);

void stm32_firewall_release_access_by_id(struct stm32_firewall *firewall, u32 subsystem_id)
{
	struct stm32_firewall_controller *firewall_controller;

	if (!firewall || subsystem_id == U32_MAX || firewall->firewall_id == U32_MAX) {
		pr_debug("Incorrect arguments when releasing a firewall access");
		return;
	}

	firewall_controller = firewall->firewall_ctrl;

	if (!firewall_controller) {
		pr_debug("No firewall controller to release");
		return;
	}

	firewall_controller->release_access(firewall_controller, subsystem_id);
}
EXPORT_SYMBOL_GPL(stm32_firewall_release_access_by_id);

/* Firewall controller API */

int stm32_firewall_controller_register(struct stm32_firewall_controller *firewall_controller)
{
	struct stm32_firewall_controller *ctrl;

	if (!firewall_controller)
		return -ENODEV;

	pr_info("Registering %s firewall controller\n", firewall_controller->name);

	mutex_lock(&firewall_controller_list_lock);
	list_for_each_entry(ctrl, &firewall_controller_list, entry) {
		if (ctrl == firewall_controller) {
			pr_debug("%s firewall controller already registered\n",
				 firewall_controller->name);
			mutex_unlock(&firewall_controller_list_lock);
			return 0;
		}
	}
	list_add_tail(&firewall_controller->entry, &firewall_controller_list);
	mutex_unlock(&firewall_controller_list_lock);

	return 0;
}
EXPORT_SYMBOL_GPL(stm32_firewall_controller_register);

void stm32_firewall_controller_unregister(struct stm32_firewall_controller *firewall_controller)
{
	struct stm32_firewall_controller *ctrl;
	bool controller_removed = false;

	if (!firewall_controller) {
		pr_debug("Null reference while unregistering firewall controller\n");
		return;
	}

	mutex_lock(&firewall_controller_list_lock);
	list_for_each_entry(ctrl, &firewall_controller_list, entry) {
		if (ctrl == firewall_controller) {
			controller_removed = true;
			list_del_init(&ctrl->entry);
			break;
		}
	}
	mutex_unlock(&firewall_controller_list_lock);

	if (!controller_removed)
		pr_debug("There was no firewall controller named %s to unregister\n",
			 firewall_controller->name);
}
EXPORT_SYMBOL_GPL(stm32_firewall_controller_unregister);

int stm32_firewall_populate_bus(struct stm32_firewall_controller *firewall_controller)
{
	struct stm32_firewall *firewalls;
	struct device_node *child;
	struct device *parent;
	unsigned int i;
	int len;
	int err;

	parent = firewall_controller->dev;

	dev_dbg(parent, "Populating %s system bus\n", dev_name(firewall_controller->dev));

	for_each_available_child_of_node(dev_of_node(parent), child) {
		/* The access-controllers property is mandatory for firewall bus devices */
		len = of_count_phandle_with_args(child, "access-controllers",
						 "#access-controller-cells");
		if (len <= 0) {
			of_node_put(child);
			return -EINVAL;
		}

		firewalls = kcalloc(len, sizeof(*firewalls), GFP_KERNEL);
		if (!firewalls) {
			of_node_put(child);
			return -ENOMEM;
		}

		err = stm32_firewall_get_firewall(child, firewalls, (unsigned int)len);
		if (err) {
			kfree(firewalls);
			of_node_put(child);
			return err;
		}

		for (i = 0; i < len; i++) {
			if (firewall_controller->grant_access(firewall_controller,
							      firewalls[i].firewall_id)) {
				/*
				 * Peripheral access not allowed or not defined.
				 * Mark the node as populated so platform bus won't probe it
				 */
				of_detach_node(child);
				dev_err(parent, "%s: Device driver will not be probed\n",
					child->full_name);
			}
		}

		kfree(firewalls);
	}

	return 0;
}
EXPORT_SYMBOL_GPL(stm32_firewall_populate_bus);
