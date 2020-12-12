// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2020 Mellanox Technologies Ltd */
#include <linux/mlx5/driver.h>
#include "vhca_event.h"
#include "priv.h"
#include "sf.h"
#include "ecpf.h"

struct mlx5_sf_hw {
	u32 usr_sfnum;
	u8 allocated: 1;
};

struct mlx5_sf_hw_table {
	struct mlx5_core_dev *dev;
	struct mlx5_sf_hw *sfs;
	int max_local_functions;
	u8 ecpu: 1;
};

u16 mlx5_sf_sw_to_hw_id(const struct mlx5_core_dev *dev, u16 sw_id)
{
	return sw_id + mlx5_sf_start_function_id(dev);
}

int mlx5_sf_hw_table_sf_alloc(struct mlx5_core_dev *dev, u32 usr_sfnum)
{
	struct mlx5_sf_hw_table *table = dev->priv.sf_hw_table;
	int sw_id = -ENOSPC;
	u16 hw_fn_id;
	int err;
	int i;

	if (!table->max_local_functions)
		return -EOPNOTSUPP;

	/* Check if sf with same sfnum already exists or not. */
	for (i = 0; i < table->max_local_functions; i++) {
		if (table->sfs[i].allocated && table->sfs[i].usr_sfnum == usr_sfnum)
			return -EEXIST;
	}

	/* Find the free entry and allocate the entry from the array */
	for (i = 0; i < table->max_local_functions; i++) {
		if (!table->sfs[i].allocated) {
			table->sfs[i].usr_sfnum = usr_sfnum;
			table->sfs[i].allocated = true;
			sw_id = i;
			break;
		}
	}
	if (sw_id == -ENOSPC) {
		err = -ENOSPC;
		goto err;
	}

	hw_fn_id = mlx5_sf_sw_to_hw_id(table->dev, sw_id);
	err = mlx5_cmd_alloc_sf(table->dev, hw_fn_id);
	if (err)
		goto err;

	err = mlx5_modify_vhca_sw_id(dev, hw_fn_id, table->ecpu, usr_sfnum);
	if (err)
		goto vhca_err;

	return sw_id;

vhca_err:
	mlx5_cmd_dealloc_sf(table->dev, hw_fn_id);
err:
	table->sfs[i].allocated = false;
	return err;
}

void mlx5_sf_hw_table_sf_free(struct mlx5_core_dev *dev, u16 id)
{
	struct mlx5_sf_hw_table *table = dev->priv.sf_hw_table;
	u16 hw_fn_id;

	hw_fn_id = mlx5_sf_sw_to_hw_id(table->dev, id);
	mlx5_cmd_dealloc_sf(table->dev, hw_fn_id);
	table->sfs[id].allocated = false;
}

int mlx5_sf_hw_table_init(struct mlx5_core_dev *dev)
{
	struct mlx5_sf_hw_table *table;
	struct mlx5_sf_hw *sfs;
	int max_functions;

	if (!mlx5_sf_supported(dev))
		return 0;

	max_functions = mlx5_sf_max_functions(dev);
	table = kzalloc(sizeof(*table), GFP_KERNEL);
	if (!table)
		return -ENOMEM;

	sfs = kcalloc(max_functions, sizeof(*sfs), GFP_KERNEL);
	if (!sfs)
		goto table_err;

	table->dev = dev;
	table->sfs = sfs;
	table->max_local_functions = max_functions;
	table->ecpu = mlx5_read_embedded_cpu(dev);
	dev->priv.sf_hw_table = table;
	mlx5_core_dbg(dev, "SF HW table: max sfs = %d\n", max_functions);
	return 0;

table_err:
	kfree(table);
	return -ENOMEM;
}

void mlx5_sf_hw_table_cleanup(struct mlx5_core_dev *dev)
{
	struct mlx5_sf_hw_table *table = dev->priv.sf_hw_table;

	if (!table)
		return;

	kfree(table->sfs);
	kfree(table);
}
