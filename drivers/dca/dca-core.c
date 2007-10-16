/*
 * Copyright(c) 2007 Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 59
 * Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 *
 * The full GNU General Public License is included in this distribution in the
 * file called COPYING.
 */

/*
 * This driver supports an interface for DCA clients and providers to meet.
 */

#include <linux/kernel.h>
#include <linux/notifier.h>
#include <linux/device.h>
#include <linux/dca.h>

MODULE_LICENSE("GPL");

/* For now we're assuming a single, global, DCA provider for the system. */

static DEFINE_SPINLOCK(dca_lock);

static struct dca_provider *global_dca = NULL;

/**
 * dca_add_requester - add a dca client to the list
 * @dev - the device that wants dca service
 */
int dca_add_requester(struct device *dev)
{
	int err, slot;

	if (!global_dca)
		return -ENODEV;

	spin_lock(&dca_lock);
	slot = global_dca->ops->add_requester(global_dca, dev);
	spin_unlock(&dca_lock);
	if (slot < 0)
		return slot;

	err = dca_sysfs_add_req(global_dca, dev, slot);
	if (err) {
		spin_lock(&dca_lock);
		global_dca->ops->remove_requester(global_dca, dev);
		spin_unlock(&dca_lock);
		return err;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(dca_add_requester);

/**
 * dca_remove_requester - remove a dca client from the list
 * @dev - the device that wants dca service
 */
int dca_remove_requester(struct device *dev)
{
	int slot;
	if (!global_dca)
		return -ENODEV;

	spin_lock(&dca_lock);
	slot = global_dca->ops->remove_requester(global_dca, dev);
	spin_unlock(&dca_lock);
	if (slot < 0)
		return slot;

	dca_sysfs_remove_req(global_dca, slot);
	return 0;
}
EXPORT_SYMBOL_GPL(dca_remove_requester);

/**
 * dca_get_tag - return the dca tag for the given cpu
 * @cpu - the cpuid as returned by get_cpu()
 */
u8 dca_get_tag(int cpu)
{
	if (!global_dca)
		return -ENODEV;
	return global_dca->ops->get_tag(global_dca, cpu);
}
EXPORT_SYMBOL_GPL(dca_get_tag);

/**
 * alloc_dca_provider - get data struct for describing a dca provider
 * @ops - pointer to struct of dca operation function pointers
 * @priv_size - size of extra mem to be added for provider's needs
 */
struct dca_provider *alloc_dca_provider(struct dca_ops *ops, int priv_size)
{
	struct dca_provider *dca;
	int alloc_size;

	alloc_size = (sizeof(*dca) + priv_size);
	dca = kzalloc(alloc_size, GFP_KERNEL);
	if (!dca)
		return NULL;
	dca->ops = ops;

	return dca;
}
EXPORT_SYMBOL_GPL(alloc_dca_provider);

/**
 * free_dca_provider - release the dca provider data struct
 * @ops - pointer to struct of dca operation function pointers
 * @priv_size - size of extra mem to be added for provider's needs
 */
void free_dca_provider(struct dca_provider *dca)
{
	kfree(dca);
}
EXPORT_SYMBOL_GPL(free_dca_provider);

static BLOCKING_NOTIFIER_HEAD(dca_provider_chain);

/**
 * register_dca_provider - register a dca provider
 * @dca - struct created by alloc_dca_provider()
 * @dev - device providing dca services
 */
int register_dca_provider(struct dca_provider *dca, struct device *dev)
{
	int err;

	if (global_dca)
		return -EEXIST;
	err = dca_sysfs_add_provider(dca, dev);
	if (err)
		return err;
	global_dca = dca;
	blocking_notifier_call_chain(&dca_provider_chain,
				     DCA_PROVIDER_ADD, NULL);
	return 0;
}
EXPORT_SYMBOL_GPL(register_dca_provider);

/**
 * unregister_dca_provider - remove a dca provider
 * @dca - struct created by alloc_dca_provider()
 */
void unregister_dca_provider(struct dca_provider *dca)
{
	if (!global_dca)
		return;
	blocking_notifier_call_chain(&dca_provider_chain,
				     DCA_PROVIDER_REMOVE, NULL);
	global_dca = NULL;
	dca_sysfs_remove_provider(dca);
}
EXPORT_SYMBOL_GPL(unregister_dca_provider);

/**
 * dca_register_notify - register a client's notifier callback
 */
void dca_register_notify(struct notifier_block *nb)
{
	blocking_notifier_chain_register(&dca_provider_chain, nb);
}
EXPORT_SYMBOL_GPL(dca_register_notify);

/**
 * dca_unregister_notify - remove a client's notifier callback
 */
void dca_unregister_notify(struct notifier_block *nb)
{
	blocking_notifier_chain_unregister(&dca_provider_chain, nb);
}
EXPORT_SYMBOL_GPL(dca_unregister_notify);

static int __init dca_init(void)
{
	return dca_sysfs_init();
}

static void __exit dca_exit(void)
{
	dca_sysfs_exit();
}

module_init(dca_init);
module_exit(dca_exit);

