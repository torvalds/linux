// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2020 Mellanox Technologies Ltd */
#include <linux/mlx5/driver.h>
#include "vhca_event.h"
#include "priv.h"
#include "sf.h"
#include "mlx5_ifc_vhca_event.h"
#include "vhca_event.h"
#include "ecpf.h"

struct mlx5_sf_hw {
	u32 usr_sfnum;
	u8 allocated: 1;
	u8 pending_delete: 1;
};

struct mlx5_sf_hw_table {
	struct mlx5_core_dev *dev;
	struct mlx5_sf_hw *sfs;
	int max_local_functions;
	u8 ecpu: 1;
	struct mutex table_lock; /* Serializes sf deletion and vhca state change handler. */
	struct notifier_block vhca_nb;
};

u16 mlx5_sf_sw_to_hw_id(const struct mlx5_core_dev *dev, u16 sw_id)
{
	return sw_id + mlx5_sf_start_function_id(dev);
}

static u16 mlx5_sf_hw_to_sw_id(const struct mlx5_core_dev *dev, u16 hw_id)
{
	return hw_id - mlx5_sf_start_function_id(dev);
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

	mutex_lock(&table->table_lock);
	/* Check if sf with same sfnum already exists or not. */
	for (i = 0; i < table->max_local_functions; i++) {
		if (table->sfs[i].allocated && table->sfs[i].usr_sfnum == usr_sfnum) {
			err = -EEXIST;
			goto exist_err;
		}
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

	mutex_unlock(&table->table_lock);
	return sw_id;

vhca_err:
	mlx5_cmd_dealloc_sf(table->dev, hw_fn_id);
err:
	table->sfs[i].allocated = false;
exist_err:
	mutex_unlock(&table->table_lock);
	return err;
}

static void _mlx5_sf_hw_id_free(struct mlx5_core_dev *dev, u16 id)
{
	struct mlx5_sf_hw_table *table = dev->priv.sf_hw_table;
	u16 hw_fn_id;

	hw_fn_id = mlx5_sf_sw_to_hw_id(table->dev, id);
	mlx5_cmd_dealloc_sf(table->dev, hw_fn_id);
	table->sfs[id].allocated = false;
	table->sfs[id].pending_delete = false;
}

void mlx5_sf_hw_table_sf_free(struct mlx5_core_dev *dev, u16 id)
{
	struct mlx5_sf_hw_table *table = dev->priv.sf_hw_table;

	mutex_lock(&table->table_lock);
	_mlx5_sf_hw_id_free(dev, id);
	mutex_unlock(&table->table_lock);
}

void mlx5_sf_hw_table_sf_deferred_free(struct mlx5_core_dev *dev, u16 id)
{
	struct mlx5_sf_hw_table *table = dev->priv.sf_hw_table;
	u32 out[MLX5_ST_SZ_DW(query_vhca_state_out)] = {};
	u16 hw_fn_id;
	u8 state;
	int err;

	hw_fn_id = mlx5_sf_sw_to_hw_id(dev, id);
	mutex_lock(&table->table_lock);
	err = mlx5_cmd_query_vhca_state(dev, hw_fn_id, table->ecpu, out, sizeof(out));
	if (err)
		goto err;
	state = MLX5_GET(query_vhca_state_out, out, vhca_state_context.vhca_state);
	if (state == MLX5_VHCA_STATE_ALLOCATED) {
		mlx5_cmd_dealloc_sf(table->dev, hw_fn_id);
		table->sfs[id].allocated = false;
	} else {
		table->sfs[id].pending_delete = true;
	}
err:
	mutex_unlock(&table->table_lock);
}

static void mlx5_sf_hw_dealloc_all(struct mlx5_sf_hw_table *table)
{
	int i;

	for (i = 0; i < table->max_local_functions; i++) {
		if (table->sfs[i].allocated)
			_mlx5_sf_hw_id_free(table->dev, i);
	}
}

int mlx5_sf_hw_table_init(struct mlx5_core_dev *dev)
{
	struct mlx5_sf_hw_table *table;
	struct mlx5_sf_hw *sfs;
	int max_functions;

	if (!mlx5_sf_supported(dev) || !mlx5_vhca_event_supported(dev))
		return 0;

	max_functions = mlx5_sf_max_functions(dev);
	table = kzalloc(sizeof(*table), GFP_KERNEL);
	if (!table)
		return -ENOMEM;

	sfs = kcalloc(max_functions, sizeof(*sfs), GFP_KERNEL);
	if (!sfs)
		goto table_err;

	mutex_init(&table->table_lock);
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

	mutex_destroy(&table->table_lock);
	kfree(table->sfs);
	kfree(table);
}

static int mlx5_sf_hw_vhca_event(struct notifier_block *nb, unsigned long opcode, void *data)
{
	struct mlx5_sf_hw_table *table = container_of(nb, struct mlx5_sf_hw_table, vhca_nb);
	const struct mlx5_vhca_state_event *event = data;
	struct mlx5_sf_hw *sf_hw;
	u16 sw_id;

	if (event->new_vhca_state != MLX5_VHCA_STATE_ALLOCATED)
		return 0;

	sw_id = mlx5_sf_hw_to_sw_id(table->dev, event->function_id);
	sf_hw = &table->sfs[sw_id];

	mutex_lock(&table->table_lock);
	/* SF driver notified through firmware that SF is finally detached.
	 * Hence recycle the sf hardware id for reuse.
	 */
	if (sf_hw->allocated && sf_hw->pending_delete)
		_mlx5_sf_hw_id_free(table->dev, sw_id);
	mutex_unlock(&table->table_lock);
	return 0;
}

int mlx5_sf_hw_table_create(struct mlx5_core_dev *dev)
{
	struct mlx5_sf_hw_table *table = dev->priv.sf_hw_table;

	if (!table)
		return 0;

	table->vhca_nb.notifier_call = mlx5_sf_hw_vhca_event;
	return mlx5_vhca_event_notifier_register(table->dev, &table->vhca_nb);
}

void mlx5_sf_hw_table_destroy(struct mlx5_core_dev *dev)
{
	struct mlx5_sf_hw_table *table = dev->priv.sf_hw_table;

	if (!table)
		return;

	mlx5_vhca_event_notifier_unregister(table->dev, &table->vhca_nb);
	/* Dealloc SFs whose firmware event has been missed. */
	mlx5_sf_hw_dealloc_all(table);
}
