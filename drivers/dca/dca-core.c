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

#define DCA_VERSION "1.4"

MODULE_VERSION(DCA_VERSION);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Intel Corporation");

static DEFINE_SPINLOCK(dca_lock);

static LIST_HEAD(dca_providers);

static struct dca_provider *dca_find_provider_by_dev(struct device *dev)
{
	struct dca_provider *dca, *ret = NULL;

	list_for_each_entry(dca, &dca_providers, node) {
		if ((!dev) || (dca->ops->dev_managed(dca, dev))) {
			ret = dca;
			break;
		}
	}

	return ret;
}

/**
 * dca_add_requester - add a dca client to the list
 * @dev - the device that wants dca service
 */
int dca_add_requester(struct device *dev)
{
	struct dca_provider *dca;
	int err, slot = -ENODEV;

	if (!dev)
		return -EFAULT;

	spin_lock(&dca_lock);

	/* check if the requester has not been added already */
	dca = dca_find_provider_by_dev(dev);
	if (dca) {
		spin_unlock(&dca_lock);
		return -EEXIST;
	}

	list_for_each_entry(dca, &dca_providers, node) {
		slot = dca->ops->add_requester(dca, dev);
		if (slot >= 0)
			break;
	}
	if (slot < 0) {
		spin_unlock(&dca_lock);
		return slot;
	}

	err = dca_sysfs_add_req(dca, dev, slot);
	if (err) {
		dca->ops->remove_requester(dca, dev);
		spin_unlock(&dca_lock);
		return err;
	}

	spin_unlock(&dca_lock);
	return 0;
}
EXPORT_SYMBOL_GPL(dca_add_requester);

/**
 * dca_remove_requester - remove a dca client from the list
 * @dev - the device that wants dca service
 */
int dca_remove_requester(struct device *dev)
{
	struct dca_provider *dca;
	int slot;

	if (!dev)
		return -EFAULT;

	spin_lock(&dca_lock);
	dca = dca_find_provider_by_dev(dev);
	if (!dca) {
		spin_unlock(&dca_lock);
		return -ENODEV;
	}
	slot = dca->ops->remove_requester(dca, dev);
	if (slot < 0) {
		spin_unlock(&dca_lock);
		return slot;
	}

	dca_sysfs_remove_req(dca, slot);

	spin_unlock(&dca_lock);
	return 0;
}
EXPORT_SYMBOL_GPL(dca_remove_requester);

/**
 * dca_common_get_tag - return the dca tag (serves both new and old api)
 * @dev - the device that wants dca service
 * @cpu - the cpuid as returned by get_cpu()
 */
u8 dca_common_get_tag(struct device *dev, int cpu)
{
	struct dca_provider *dca;
	u8 tag;

	spin_lock(&dca_lock);

	dca = dca_find_provider_by_dev(dev);
	if (!dca) {
		spin_unlock(&dca_lock);
		return -ENODEV;
	}
	tag = dca->ops->get_tag(dca, dev, cpu);

	spin_unlock(&dca_lock);
	return tag;
}

/**
 * dca3_get_tag - return the dca tag to the requester device
 *                for the given cpu (new api)
 * @dev - the device that wants dca service
 * @cpu - the cpuid as returned by get_cpu()
 */
u8 dca3_get_tag(struct device *dev, int cpu)
{
	if (!dev)
		return -EFAULT;

	return dca_common_get_tag(dev, cpu);
}
EXPORT_SYMBOL_GPL(dca3_get_tag);

/**
 * dca_get_tag - return the dca tag for the given cpu (old api)
 * @cpu - the cpuid as returned by get_cpu()
 */
u8 dca_get_tag(int cpu)
{
	struct device *dev = NULL;

	return dca_common_get_tag(dev, cpu);
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

	err = dca_sysfs_add_provider(dca, dev);
	if (err)
		return err;
	list_add(&dca->node, &dca_providers);
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
	blocking_notifier_call_chain(&dca_provider_chain,
				     DCA_PROVIDER_REMOVE, NULL);
	list_del(&dca->node);
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
	printk(KERN_ERR "dca service started, version %s\n", DCA_VERSION);
	return dca_sysfs_init();
}

static void __exit dca_exit(void)
{
	dca_sysfs_exit();
}

module_init(dca_init);
module_exit(dca_exit);

