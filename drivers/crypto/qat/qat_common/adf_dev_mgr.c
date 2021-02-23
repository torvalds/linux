// SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only)
/* Copyright(c) 2014 - 2020 Intel Corporation */
#include <linux/mutex.h>
#include <linux/list.h>
#include "adf_cfg.h"
#include "adf_common_drv.h"

static LIST_HEAD(accel_table);
static LIST_HEAD(vfs_table);
static DEFINE_MUTEX(table_lock);
static u32 num_devices;
static u8 id_map[ADF_MAX_DEVICES];

struct vf_id_map {
	u32 bdf;
	u32 id;
	u32 fake_id;
	bool attached;
	struct list_head list;
};

static int adf_get_vf_id(struct adf_accel_dev *vf)
{
	return ((7 * (PCI_SLOT(accel_to_pci_dev(vf)->devfn) - 1)) +
		PCI_FUNC(accel_to_pci_dev(vf)->devfn) +
		(PCI_SLOT(accel_to_pci_dev(vf)->devfn) - 1));
}

static int adf_get_vf_num(struct adf_accel_dev *vf)
{
	return (accel_to_pci_dev(vf)->bus->number << 8) | adf_get_vf_id(vf);
}

static struct vf_id_map *adf_find_vf(u32 bdf)
{
	struct list_head *itr;

	list_for_each(itr, &vfs_table) {
		struct vf_id_map *ptr =
			list_entry(itr, struct vf_id_map, list);

		if (ptr->bdf == bdf)
			return ptr;
	}
	return NULL;
}

static int adf_get_vf_real_id(u32 fake)
{
	struct list_head *itr;

	list_for_each(itr, &vfs_table) {
		struct vf_id_map *ptr =
			list_entry(itr, struct vf_id_map, list);
		if (ptr->fake_id == fake)
			return ptr->id;
	}
	return -1;
}

/**
 * adf_clean_vf_map() - Cleans VF id mapings
 *
 * Function cleans internal ids for virtual functions.
 * @vf: flag indicating whether mappings is cleaned
 *	for vfs only or for vfs and pfs
 */
void adf_clean_vf_map(bool vf)
{
	struct vf_id_map *map;
	struct list_head *ptr, *tmp;

	mutex_lock(&table_lock);
	list_for_each_safe(ptr, tmp, &vfs_table) {
		map = list_entry(ptr, struct vf_id_map, list);
		if (map->bdf != -1) {
			id_map[map->id] = 0;
			num_devices--;
		}

		if (vf && map->bdf == -1)
			continue;

		list_del(ptr);
		kfree(map);
	}
	mutex_unlock(&table_lock);
}
EXPORT_SYMBOL_GPL(adf_clean_vf_map);

/**
 * adf_devmgr_update_class_index() - Update internal index
 * @hw_data:  Pointer to internal device data.
 *
 * Function updates internal dev index for VFs
 */
void adf_devmgr_update_class_index(struct adf_hw_device_data *hw_data)
{
	struct adf_hw_device_class *class = hw_data->dev_class;
	struct list_head *itr;
	int i = 0;

	list_for_each(itr, &accel_table) {
		struct adf_accel_dev *ptr =
				list_entry(itr, struct adf_accel_dev, list);

		if (ptr->hw_device->dev_class == class)
			ptr->hw_device->instance_id = i++;

		if (i == class->instances)
			break;
	}
}
EXPORT_SYMBOL_GPL(adf_devmgr_update_class_index);

static unsigned int adf_find_free_id(void)
{
	unsigned int i;

	for (i = 0; i < ADF_MAX_DEVICES; i++) {
		if (!id_map[i]) {
			id_map[i] = 1;
			return i;
		}
	}
	return ADF_MAX_DEVICES + 1;
}

/**
 * adf_devmgr_add_dev() - Add accel_dev to the acceleration framework
 * @accel_dev:  Pointer to acceleration device.
 * @pf:		Corresponding PF if the accel_dev is a VF
 *
 * Function adds acceleration device to the acceleration framework.
 * To be used by QAT device specific drivers.
 *
 * Return: 0 on success, error code otherwise.
 */
int adf_devmgr_add_dev(struct adf_accel_dev *accel_dev,
		       struct adf_accel_dev *pf)
{
	struct list_head *itr;
	int ret = 0;

	if (num_devices == ADF_MAX_DEVICES) {
		dev_err(&GET_DEV(accel_dev), "Only support up to %d devices\n",
			ADF_MAX_DEVICES);
		return -EFAULT;
	}

	mutex_lock(&table_lock);
	atomic_set(&accel_dev->ref_count, 0);

	/* PF on host or VF on guest - optimized to remove redundant is_vf */
	if (!accel_dev->is_vf || !pf) {
		struct vf_id_map *map;

		list_for_each(itr, &accel_table) {
			struct adf_accel_dev *ptr =
				list_entry(itr, struct adf_accel_dev, list);

			if (ptr == accel_dev) {
				ret = -EEXIST;
				goto unlock;
			}
		}

		list_add_tail(&accel_dev->list, &accel_table);
		accel_dev->accel_id = adf_find_free_id();
		if (accel_dev->accel_id > ADF_MAX_DEVICES) {
			ret = -EFAULT;
			goto unlock;
		}
		num_devices++;
		map = kzalloc(sizeof(*map), GFP_KERNEL);
		if (!map) {
			ret = -ENOMEM;
			goto unlock;
		}
		map->bdf = ~0;
		map->id = accel_dev->accel_id;
		map->fake_id = map->id;
		map->attached = true;
		list_add_tail(&map->list, &vfs_table);
	} else if (accel_dev->is_vf && pf) {
		/* VF on host */
		struct vf_id_map *map;

		map = adf_find_vf(adf_get_vf_num(accel_dev));
		if (map) {
			struct vf_id_map *next;

			accel_dev->accel_id = map->id;
			list_add_tail(&accel_dev->list, &accel_table);
			map->fake_id++;
			map->attached = true;
			next = list_next_entry(map, list);
			while (next && &next->list != &vfs_table) {
				next->fake_id++;
				next = list_next_entry(next, list);
			}

			ret = 0;
			goto unlock;
		}

		map = kzalloc(sizeof(*map), GFP_KERNEL);
		if (!map) {
			ret = -ENOMEM;
			goto unlock;
		}
		accel_dev->accel_id = adf_find_free_id();
		if (accel_dev->accel_id > ADF_MAX_DEVICES) {
			kfree(map);
			ret = -EFAULT;
			goto unlock;
		}
		num_devices++;
		list_add_tail(&accel_dev->list, &accel_table);
		map->bdf = adf_get_vf_num(accel_dev);
		map->id = accel_dev->accel_id;
		map->fake_id = map->id;
		map->attached = true;
		list_add_tail(&map->list, &vfs_table);
	}
unlock:
	mutex_unlock(&table_lock);
	return ret;
}
EXPORT_SYMBOL_GPL(adf_devmgr_add_dev);

struct list_head *adf_devmgr_get_head(void)
{
	return &accel_table;
}

/**
 * adf_devmgr_rm_dev() - Remove accel_dev from the acceleration framework.
 * @accel_dev:  Pointer to acceleration device.
 * @pf:		Corresponding PF if the accel_dev is a VF
 *
 * Function removes acceleration device from the acceleration framework.
 * To be used by QAT device specific drivers.
 *
 * Return: void
 */
void adf_devmgr_rm_dev(struct adf_accel_dev *accel_dev,
		       struct adf_accel_dev *pf)
{
	mutex_lock(&table_lock);
	/* PF on host or VF on guest - optimized to remove redundant is_vf */
	if (!accel_dev->is_vf || !pf) {
		id_map[accel_dev->accel_id] = 0;
		num_devices--;
	} else if (accel_dev->is_vf && pf) {
		struct vf_id_map *map, *next;

		map = adf_find_vf(adf_get_vf_num(accel_dev));
		if (!map) {
			dev_err(&GET_DEV(accel_dev), "Failed to find VF map\n");
			goto unlock;
		}
		map->fake_id--;
		map->attached = false;
		next = list_next_entry(map, list);
		while (next && &next->list != &vfs_table) {
			next->fake_id--;
			next = list_next_entry(next, list);
		}
	}
unlock:
	list_del(&accel_dev->list);
	mutex_unlock(&table_lock);
}
EXPORT_SYMBOL_GPL(adf_devmgr_rm_dev);

struct adf_accel_dev *adf_devmgr_get_first(void)
{
	struct adf_accel_dev *dev = NULL;

	if (!list_empty(&accel_table))
		dev = list_first_entry(&accel_table, struct adf_accel_dev,
				       list);
	return dev;
}

/**
 * adf_devmgr_pci_to_accel_dev() - Get accel_dev associated with the pci_dev.
 * @pci_dev:  Pointer to PCI device.
 *
 * Function returns acceleration device associated with the given PCI device.
 * To be used by QAT device specific drivers.
 *
 * Return: pointer to accel_dev or NULL if not found.
 */
struct adf_accel_dev *adf_devmgr_pci_to_accel_dev(struct pci_dev *pci_dev)
{
	struct list_head *itr;

	mutex_lock(&table_lock);
	list_for_each(itr, &accel_table) {
		struct adf_accel_dev *ptr =
				list_entry(itr, struct adf_accel_dev, list);

		if (ptr->accel_pci_dev.pci_dev == pci_dev) {
			mutex_unlock(&table_lock);
			return ptr;
		}
	}
	mutex_unlock(&table_lock);
	return NULL;
}
EXPORT_SYMBOL_GPL(adf_devmgr_pci_to_accel_dev);

struct adf_accel_dev *adf_devmgr_get_dev_by_id(u32 id)
{
	struct list_head *itr;
	int real_id;

	mutex_lock(&table_lock);
	real_id = adf_get_vf_real_id(id);
	if (real_id < 0)
		goto unlock;

	id = real_id;

	list_for_each(itr, &accel_table) {
		struct adf_accel_dev *ptr =
				list_entry(itr, struct adf_accel_dev, list);
		if (ptr->accel_id == id) {
			mutex_unlock(&table_lock);
			return ptr;
		}
	}
unlock:
	mutex_unlock(&table_lock);
	return NULL;
}

int adf_devmgr_verify_id(u32 id)
{
	if (id == ADF_CFG_ALL_DEVICES)
		return 0;

	if (adf_devmgr_get_dev_by_id(id))
		return 0;

	return -ENODEV;
}

static int adf_get_num_dettached_vfs(void)
{
	struct list_head *itr;
	int vfs = 0;

	mutex_lock(&table_lock);
	list_for_each(itr, &vfs_table) {
		struct vf_id_map *ptr =
			list_entry(itr, struct vf_id_map, list);
		if (ptr->bdf != ~0 && !ptr->attached)
			vfs++;
	}
	mutex_unlock(&table_lock);
	return vfs;
}

void adf_devmgr_get_num_dev(u32 *num)
{
	*num = num_devices - adf_get_num_dettached_vfs();
}

/**
 * adf_dev_in_use() - Check whether accel_dev is currently in use
 * @accel_dev: Pointer to acceleration device.
 *
 * To be used by QAT device specific drivers.
 *
 * Return: 1 when device is in use, 0 otherwise.
 */
int adf_dev_in_use(struct adf_accel_dev *accel_dev)
{
	return atomic_read(&accel_dev->ref_count) != 0;
}
EXPORT_SYMBOL_GPL(adf_dev_in_use);

/**
 * adf_dev_get() - Increment accel_dev reference count
 * @accel_dev: Pointer to acceleration device.
 *
 * Increment the accel_dev refcount and if this is the first time
 * incrementing it during this period the accel_dev is in use,
 * increment the module refcount too.
 * To be used by QAT device specific drivers.
 *
 * Return: 0 when successful, EFAULT when fail to bump module refcount
 */
int adf_dev_get(struct adf_accel_dev *accel_dev)
{
	if (atomic_add_return(1, &accel_dev->ref_count) == 1)
		if (!try_module_get(accel_dev->owner))
			return -EFAULT;
	return 0;
}
EXPORT_SYMBOL_GPL(adf_dev_get);

/**
 * adf_dev_put() - Decrement accel_dev reference count
 * @accel_dev: Pointer to acceleration device.
 *
 * Decrement the accel_dev refcount and if this is the last time
 * decrementing it during this period the accel_dev is in use,
 * decrement the module refcount too.
 * To be used by QAT device specific drivers.
 *
 * Return: void
 */
void adf_dev_put(struct adf_accel_dev *accel_dev)
{
	if (atomic_sub_return(1, &accel_dev->ref_count) == 0)
		module_put(accel_dev->owner);
}
EXPORT_SYMBOL_GPL(adf_dev_put);

/**
 * adf_devmgr_in_reset() - Check whether device is in reset
 * @accel_dev: Pointer to acceleration device.
 *
 * To be used by QAT device specific drivers.
 *
 * Return: 1 when the device is being reset, 0 otherwise.
 */
int adf_devmgr_in_reset(struct adf_accel_dev *accel_dev)
{
	return test_bit(ADF_STATUS_RESTARTING, &accel_dev->status);
}
EXPORT_SYMBOL_GPL(adf_devmgr_in_reset);

/**
 * adf_dev_started() - Check whether device has started
 * @accel_dev: Pointer to acceleration device.
 *
 * To be used by QAT device specific drivers.
 *
 * Return: 1 when the device has started, 0 otherwise
 */
int adf_dev_started(struct adf_accel_dev *accel_dev)
{
	return test_bit(ADF_STATUS_STARTED, &accel_dev->status);
}
EXPORT_SYMBOL_GPL(adf_dev_started);
