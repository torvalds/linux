// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2020 Mellanox Technologies Ltd */

#include <linux/mlx5/driver.h>
#include <linux/mlx5/device.h>
#include "mlx5_core.h"
#include "dev.h"
#include "sf/vhca_event.h"
#include "sf/sf.h"
#include "sf/mlx5_ifc_vhca_event.h"
#include "ecpf.h"

struct mlx5_sf_dev_table {
	struct xarray devices;
	unsigned int max_sfs;
	phys_addr_t base_address;
	u64 sf_bar_length;
	struct notifier_block nb;
	struct mlx5_core_dev *dev;
};

static bool mlx5_sf_dev_supported(const struct mlx5_core_dev *dev)
{
	return MLX5_CAP_GEN(dev, sf) && mlx5_vhca_event_supported(dev);
}

bool mlx5_sf_dev_allocated(const struct mlx5_core_dev *dev)
{
	struct mlx5_sf_dev_table *table = dev->priv.sf_dev_table;

	if (!mlx5_sf_dev_supported(dev))
		return false;

	return !xa_empty(&table->devices);
}

static ssize_t sfnum_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct auxiliary_device *adev = container_of(dev, struct auxiliary_device, dev);
	struct mlx5_sf_dev *sf_dev = container_of(adev, struct mlx5_sf_dev, adev);

	return scnprintf(buf, PAGE_SIZE, "%u\n", sf_dev->sfnum);
}
static DEVICE_ATTR_RO(sfnum);

static struct attribute *sf_device_attrs[] = {
	&dev_attr_sfnum.attr,
	NULL,
};

static const struct attribute_group sf_attr_group = {
	.attrs = sf_device_attrs,
};

static const struct attribute_group *sf_attr_groups[2] = {
	&sf_attr_group,
	NULL
};

static void mlx5_sf_dev_release(struct device *device)
{
	struct auxiliary_device *adev = container_of(device, struct auxiliary_device, dev);
	struct mlx5_sf_dev *sf_dev = container_of(adev, struct mlx5_sf_dev, adev);

	mlx5_adev_idx_free(adev->id);
	kfree(sf_dev);
}

static void mlx5_sf_dev_remove(struct mlx5_sf_dev *sf_dev)
{
	auxiliary_device_delete(&sf_dev->adev);
	auxiliary_device_uninit(&sf_dev->adev);
}

static void mlx5_sf_dev_add(struct mlx5_core_dev *dev, u16 sf_index, u32 sfnum)
{
	struct mlx5_sf_dev_table *table = dev->priv.sf_dev_table;
	struct mlx5_sf_dev *sf_dev;
	struct pci_dev *pdev;
	int err;
	int id;

	id = mlx5_adev_idx_alloc();
	if (id < 0) {
		err = id;
		goto add_err;
	}

	sf_dev = kzalloc(sizeof(*sf_dev), GFP_KERNEL);
	if (!sf_dev) {
		mlx5_adev_idx_free(id);
		err = -ENOMEM;
		goto add_err;
	}
	pdev = dev->pdev;
	sf_dev->adev.id = id;
	sf_dev->adev.name = MLX5_SF_DEV_ID_NAME;
	sf_dev->adev.dev.release = mlx5_sf_dev_release;
	sf_dev->adev.dev.parent = &pdev->dev;
	sf_dev->adev.dev.groups = sf_attr_groups;
	sf_dev->sfnum = sfnum;
	sf_dev->parent_mdev = dev;

	if (!table->max_sfs) {
		mlx5_adev_idx_free(id);
		kfree(sf_dev);
		err = -EOPNOTSUPP;
		goto add_err;
	}
	sf_dev->bar_base_addr = table->base_address + (sf_index * table->sf_bar_length);

	err = auxiliary_device_init(&sf_dev->adev);
	if (err) {
		mlx5_adev_idx_free(id);
		kfree(sf_dev);
		goto add_err;
	}

	err = auxiliary_device_add(&sf_dev->adev);
	if (err) {
		put_device(&sf_dev->adev.dev);
		goto add_err;
	}

	err = xa_insert(&table->devices, sf_index, sf_dev, GFP_KERNEL);
	if (err)
		goto xa_err;
	return;

xa_err:
	mlx5_sf_dev_remove(sf_dev);
add_err:
	mlx5_core_err(dev, "SF DEV: fail device add for index=%d sfnum=%d err=%d\n",
		      sf_index, sfnum, err);
}

static void mlx5_sf_dev_del(struct mlx5_core_dev *dev, struct mlx5_sf_dev *sf_dev, u16 sf_index)
{
	struct mlx5_sf_dev_table *table = dev->priv.sf_dev_table;

	xa_erase(&table->devices, sf_index);
	mlx5_sf_dev_remove(sf_dev);
}

static int
mlx5_sf_dev_state_change_handler(struct notifier_block *nb, unsigned long event_code, void *data)
{
	struct mlx5_sf_dev_table *table = container_of(nb, struct mlx5_sf_dev_table, nb);
	const struct mlx5_vhca_state_event *event = data;
	struct mlx5_sf_dev *sf_dev;
	u16 max_functions;
	u16 sf_index;
	u16 base_id;

	max_functions = mlx5_sf_max_functions(table->dev);
	if (!max_functions)
		return 0;

	base_id = MLX5_CAP_GEN(table->dev, sf_base_id);
	if (event->function_id < base_id || event->function_id >= (base_id + max_functions))
		return 0;

	sf_index = event->function_id - base_id;
	sf_dev = xa_load(&table->devices, sf_index);
	switch (event->new_vhca_state) {
	case MLX5_VHCA_STATE_ALLOCATED:
		if (sf_dev)
			mlx5_sf_dev_del(table->dev, sf_dev, sf_index);
		break;
	case MLX5_VHCA_STATE_TEARDOWN_REQUEST:
		if (sf_dev)
			mlx5_sf_dev_del(table->dev, sf_dev, sf_index);
		else
			mlx5_core_err(table->dev,
				      "SF DEV: teardown state for invalid dev index=%d fn_id=0x%x\n",
				      sf_index, event->sw_function_id);
		break;
	case MLX5_VHCA_STATE_ACTIVE:
		if (!sf_dev)
			mlx5_sf_dev_add(table->dev, sf_index, event->sw_function_id);
		break;
	default:
		break;
	}
	return 0;
}

static int mlx5_sf_dev_vhca_arm_all(struct mlx5_sf_dev_table *table)
{
	struct mlx5_core_dev *dev = table->dev;
	u16 max_functions;
	u16 function_id;
	int err = 0;
	int i;

	max_functions = mlx5_sf_max_functions(dev);
	function_id = MLX5_CAP_GEN(dev, sf_base_id);
	/* Arm the vhca context as the vhca event notifier */
	for (i = 0; i < max_functions; i++) {
		err = mlx5_vhca_event_arm(dev, function_id);
		if (err)
			return err;

		function_id++;
	}
	return 0;
}

void mlx5_sf_dev_table_create(struct mlx5_core_dev *dev)
{
	struct mlx5_sf_dev_table *table;
	unsigned int max_sfs;
	int err;

	if (!mlx5_sf_dev_supported(dev) || !mlx5_vhca_event_supported(dev))
		return;

	table = kzalloc(sizeof(*table), GFP_KERNEL);
	if (!table) {
		err = -ENOMEM;
		goto table_err;
	}

	table->nb.notifier_call = mlx5_sf_dev_state_change_handler;
	table->dev = dev;
	if (MLX5_CAP_GEN(dev, max_num_sf))
		max_sfs = MLX5_CAP_GEN(dev, max_num_sf);
	else
		max_sfs = 1 << MLX5_CAP_GEN(dev, log_max_sf);
	table->sf_bar_length = 1 << (MLX5_CAP_GEN(dev, log_min_sf_size) + 12);
	table->base_address = pci_resource_start(dev->pdev, 2);
	table->max_sfs = max_sfs;
	xa_init(&table->devices);
	dev->priv.sf_dev_table = table;

	err = mlx5_vhca_event_notifier_register(dev, &table->nb);
	if (err)
		goto vhca_err;
	err = mlx5_sf_dev_vhca_arm_all(table);
	if (err)
		goto arm_err;
	mlx5_core_dbg(dev, "SF DEV: max sf devices=%d\n", max_sfs);
	return;

arm_err:
	mlx5_vhca_event_notifier_unregister(dev, &table->nb);
vhca_err:
	table->max_sfs = 0;
	kfree(table);
	dev->priv.sf_dev_table = NULL;
table_err:
	mlx5_core_err(dev, "SF DEV table create err = %d\n", err);
}

static void mlx5_sf_dev_destroy_all(struct mlx5_sf_dev_table *table)
{
	struct mlx5_sf_dev *sf_dev;
	unsigned long index;

	xa_for_each(&table->devices, index, sf_dev) {
		xa_erase(&table->devices, index);
		mlx5_sf_dev_remove(sf_dev);
	}
}

void mlx5_sf_dev_table_destroy(struct mlx5_core_dev *dev)
{
	struct mlx5_sf_dev_table *table = dev->priv.sf_dev_table;

	if (!table)
		return;

	mlx5_vhca_event_notifier_unregister(dev, &table->nb);

	/* Now that event handler is not running, it is safe to destroy
	 * the sf device without race.
	 */
	mlx5_sf_dev_destroy_all(table);

	WARN_ON(!xa_empty(&table->devices));
	kfree(table);
	dev->priv.sf_dev_table = NULL;
}
