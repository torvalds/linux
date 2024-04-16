// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2020 Mellanox Technologies Ltd */
#include <linux/mlx5/driver.h>
#include "vhca_event.h"
#include "priv.h"
#include "sf.h"
#include "mlx5_ifc_vhca_event.h"
#include "ecpf.h"
#include "mlx5_core.h"
#include "eswitch.h"
#include "diag/sf_tracepoint.h"

struct mlx5_sf_hw {
	u32 usr_sfnum;
	u8 allocated: 1;
	u8 pending_delete: 1;
};

struct mlx5_sf_hwc_table {
	struct mlx5_sf_hw *sfs;
	int max_fn;
	u16 start_fn_id;
};

enum mlx5_sf_hwc_index {
	MLX5_SF_HWC_LOCAL,
	MLX5_SF_HWC_EXTERNAL,
	MLX5_SF_HWC_MAX,
};

struct mlx5_sf_hw_table {
	struct mlx5_core_dev *dev;
	struct mutex table_lock; /* Serializes sf deletion and vhca state change handler. */
	struct notifier_block vhca_nb;
	struct mlx5_sf_hwc_table hwc[MLX5_SF_HWC_MAX];
};

static struct mlx5_sf_hwc_table *
mlx5_sf_controller_to_hwc(struct mlx5_core_dev *dev, u32 controller)
{
	int idx = !!controller;

	return &dev->priv.sf_hw_table->hwc[idx];
}

u16 mlx5_sf_sw_to_hw_id(struct mlx5_core_dev *dev, u32 controller, u16 sw_id)
{
	struct mlx5_sf_hwc_table *hwc;

	hwc = mlx5_sf_controller_to_hwc(dev, controller);
	return hwc->start_fn_id + sw_id;
}

static u16 mlx5_sf_hw_to_sw_id(struct mlx5_sf_hwc_table *hwc, u16 hw_id)
{
	return hw_id - hwc->start_fn_id;
}

static struct mlx5_sf_hwc_table *
mlx5_sf_table_fn_to_hwc(struct mlx5_sf_hw_table *table, u16 fn_id)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(table->hwc); i++) {
		if (table->hwc[i].max_fn &&
		    fn_id >= table->hwc[i].start_fn_id &&
		    fn_id < (table->hwc[i].start_fn_id + table->hwc[i].max_fn))
			return &table->hwc[i];
	}
	return NULL;
}

static int mlx5_sf_hw_table_id_alloc(struct mlx5_sf_hw_table *table, u32 controller,
				     u32 usr_sfnum)
{
	struct mlx5_sf_hwc_table *hwc;
	int free_idx = -1;
	int i;

	hwc = mlx5_sf_controller_to_hwc(table->dev, controller);
	if (!hwc->sfs)
		return -ENOSPC;

	for (i = 0; i < hwc->max_fn; i++) {
		if (!hwc->sfs[i].allocated && free_idx == -1) {
			free_idx = i;
			continue;
		}

		if (hwc->sfs[i].allocated && hwc->sfs[i].usr_sfnum == usr_sfnum)
			return -EEXIST;
	}

	if (free_idx == -1)
		return -ENOSPC;

	hwc->sfs[free_idx].usr_sfnum = usr_sfnum;
	hwc->sfs[free_idx].allocated = true;
	return free_idx;
}

static void mlx5_sf_hw_table_id_free(struct mlx5_sf_hw_table *table, u32 controller, int id)
{
	struct mlx5_sf_hwc_table *hwc;

	hwc = mlx5_sf_controller_to_hwc(table->dev, controller);
	hwc->sfs[id].allocated = false;
	hwc->sfs[id].pending_delete = false;
}

int mlx5_sf_hw_table_sf_alloc(struct mlx5_core_dev *dev, u32 controller, u32 usr_sfnum)
{
	struct mlx5_sf_hw_table *table = dev->priv.sf_hw_table;
	u16 hw_fn_id;
	int sw_id;
	int err;

	if (!table)
		return -EOPNOTSUPP;

	mutex_lock(&table->table_lock);
	sw_id = mlx5_sf_hw_table_id_alloc(table, controller, usr_sfnum);
	if (sw_id < 0) {
		err = sw_id;
		goto exist_err;
	}

	hw_fn_id = mlx5_sf_sw_to_hw_id(dev, controller, sw_id);
	err = mlx5_cmd_alloc_sf(dev, hw_fn_id);
	if (err)
		goto err;

	err = mlx5_modify_vhca_sw_id(dev, hw_fn_id, usr_sfnum);
	if (err)
		goto vhca_err;

	if (controller) {
		/* If this SF is for external controller, SF manager
		 * needs to arm firmware to receive the events.
		 */
		err = mlx5_vhca_event_arm(dev, hw_fn_id);
		if (err)
			goto vhca_err;
	}

	trace_mlx5_sf_hwc_alloc(dev, controller, hw_fn_id, usr_sfnum);
	mutex_unlock(&table->table_lock);
	return sw_id;

vhca_err:
	mlx5_cmd_dealloc_sf(dev, hw_fn_id);
err:
	mlx5_sf_hw_table_id_free(table, controller, sw_id);
exist_err:
	mutex_unlock(&table->table_lock);
	return err;
}

void mlx5_sf_hw_table_sf_free(struct mlx5_core_dev *dev, u32 controller, u16 id)
{
	struct mlx5_sf_hw_table *table = dev->priv.sf_hw_table;
	u16 hw_fn_id;

	mutex_lock(&table->table_lock);
	hw_fn_id = mlx5_sf_sw_to_hw_id(dev, controller, id);
	mlx5_cmd_dealloc_sf(dev, hw_fn_id);
	mlx5_sf_hw_table_id_free(table, controller, id);
	mutex_unlock(&table->table_lock);
}

static void mlx5_sf_hw_table_hwc_sf_free(struct mlx5_core_dev *dev,
					 struct mlx5_sf_hwc_table *hwc, int idx)
{
	mlx5_cmd_dealloc_sf(dev, hwc->start_fn_id + idx);
	hwc->sfs[idx].allocated = false;
	hwc->sfs[idx].pending_delete = false;
	trace_mlx5_sf_hwc_free(dev, hwc->start_fn_id + idx);
}

void mlx5_sf_hw_table_sf_deferred_free(struct mlx5_core_dev *dev, u32 controller, u16 id)
{
	struct mlx5_sf_hw_table *table = dev->priv.sf_hw_table;
	u32 out[MLX5_ST_SZ_DW(query_vhca_state_out)] = {};
	struct mlx5_sf_hwc_table *hwc;
	u16 hw_fn_id;
	u8 state;
	int err;

	hw_fn_id = mlx5_sf_sw_to_hw_id(dev, controller, id);
	hwc = mlx5_sf_controller_to_hwc(dev, controller);
	mutex_lock(&table->table_lock);
	err = mlx5_cmd_query_vhca_state(dev, hw_fn_id, out, sizeof(out));
	if (err)
		goto err;
	state = MLX5_GET(query_vhca_state_out, out, vhca_state_context.vhca_state);
	if (state == MLX5_VHCA_STATE_ALLOCATED) {
		mlx5_cmd_dealloc_sf(dev, hw_fn_id);
		hwc->sfs[id].allocated = false;
	} else {
		hwc->sfs[id].pending_delete = true;
		trace_mlx5_sf_hwc_deferred_free(dev, hw_fn_id);
	}
err:
	mutex_unlock(&table->table_lock);
}

static void mlx5_sf_hw_table_hwc_dealloc_all(struct mlx5_core_dev *dev,
					     struct mlx5_sf_hwc_table *hwc)
{
	int i;

	for (i = 0; i < hwc->max_fn; i++) {
		if (hwc->sfs[i].allocated)
			mlx5_sf_hw_table_hwc_sf_free(dev, hwc, i);
	}
}

static void mlx5_sf_hw_table_dealloc_all(struct mlx5_sf_hw_table *table)
{
	mlx5_sf_hw_table_hwc_dealloc_all(table->dev, &table->hwc[MLX5_SF_HWC_EXTERNAL]);
	mlx5_sf_hw_table_hwc_dealloc_all(table->dev, &table->hwc[MLX5_SF_HWC_LOCAL]);
}

static int mlx5_sf_hw_table_hwc_init(struct mlx5_sf_hwc_table *hwc, u16 max_fn, u16 base_id)
{
	struct mlx5_sf_hw *sfs;

	if (!max_fn)
		return 0;

	sfs = kcalloc(max_fn, sizeof(*sfs), GFP_KERNEL);
	if (!sfs)
		return -ENOMEM;

	hwc->sfs = sfs;
	hwc->max_fn = max_fn;
	hwc->start_fn_id = base_id;
	return 0;
}

static void mlx5_sf_hw_table_hwc_cleanup(struct mlx5_sf_hwc_table *hwc)
{
	kfree(hwc->sfs);
}

int mlx5_sf_hw_table_init(struct mlx5_core_dev *dev)
{
	struct mlx5_sf_hw_table *table;
	u16 max_ext_fn = 0;
	u16 ext_base_id = 0;
	u16 max_fn = 0;
	u16 base_id;
	int err;

	if (!mlx5_vhca_event_supported(dev))
		return 0;

	if (mlx5_sf_supported(dev))
		max_fn = mlx5_sf_max_functions(dev);

	err = mlx5_esw_sf_max_hpf_functions(dev, &max_ext_fn, &ext_base_id);
	if (err)
		return err;

	if (!max_fn && !max_ext_fn)
		return 0;

	table = kzalloc(sizeof(*table), GFP_KERNEL);
	if (!table)
		return -ENOMEM;

	mutex_init(&table->table_lock);
	table->dev = dev;
	dev->priv.sf_hw_table = table;

	base_id = mlx5_sf_start_function_id(dev);
	err = mlx5_sf_hw_table_hwc_init(&table->hwc[MLX5_SF_HWC_LOCAL], max_fn, base_id);
	if (err)
		goto table_err;

	err = mlx5_sf_hw_table_hwc_init(&table->hwc[MLX5_SF_HWC_EXTERNAL],
					max_ext_fn, ext_base_id);
	if (err)
		goto ext_err;

	mlx5_core_dbg(dev, "SF HW table: max sfs = %d, ext sfs = %d\n", max_fn, max_ext_fn);
	return 0;

ext_err:
	mlx5_sf_hw_table_hwc_cleanup(&table->hwc[MLX5_SF_HWC_LOCAL]);
table_err:
	mutex_destroy(&table->table_lock);
	kfree(table);
	return err;
}

void mlx5_sf_hw_table_cleanup(struct mlx5_core_dev *dev)
{
	struct mlx5_sf_hw_table *table = dev->priv.sf_hw_table;

	if (!table)
		return;

	mutex_destroy(&table->table_lock);
	mlx5_sf_hw_table_hwc_cleanup(&table->hwc[MLX5_SF_HWC_EXTERNAL]);
	mlx5_sf_hw_table_hwc_cleanup(&table->hwc[MLX5_SF_HWC_LOCAL]);
	kfree(table);
}

static int mlx5_sf_hw_vhca_event(struct notifier_block *nb, unsigned long opcode, void *data)
{
	struct mlx5_sf_hw_table *table = container_of(nb, struct mlx5_sf_hw_table, vhca_nb);
	const struct mlx5_vhca_state_event *event = data;
	struct mlx5_sf_hwc_table *hwc;
	struct mlx5_sf_hw *sf_hw;
	u16 sw_id;

	if (event->new_vhca_state != MLX5_VHCA_STATE_ALLOCATED)
		return 0;

	hwc = mlx5_sf_table_fn_to_hwc(table, event->function_id);
	if (!hwc)
		return 0;

	sw_id = mlx5_sf_hw_to_sw_id(hwc, event->function_id);
	sf_hw = &hwc->sfs[sw_id];

	mutex_lock(&table->table_lock);
	/* SF driver notified through firmware that SF is finally detached.
	 * Hence recycle the sf hardware id for reuse.
	 */
	if (sf_hw->allocated && sf_hw->pending_delete)
		mlx5_sf_hw_table_hwc_sf_free(table->dev, hwc, sw_id);
	mutex_unlock(&table->table_lock);
	return 0;
}

int mlx5_sf_hw_table_create(struct mlx5_core_dev *dev)
{
	struct mlx5_sf_hw_table *table = dev->priv.sf_hw_table;

	if (!table)
		return 0;

	table->vhca_nb.notifier_call = mlx5_sf_hw_vhca_event;
	return mlx5_vhca_event_notifier_register(dev, &table->vhca_nb);
}

void mlx5_sf_hw_table_destroy(struct mlx5_core_dev *dev)
{
	struct mlx5_sf_hw_table *table = dev->priv.sf_hw_table;

	if (!table)
		return;

	mlx5_vhca_event_notifier_unregister(dev, &table->vhca_nb);
	/* Dealloc SFs whose firmware event has been missed. */
	mlx5_sf_hw_table_dealloc_all(table);
}

bool mlx5_sf_hw_table_supported(const struct mlx5_core_dev *dev)
{
	return !!dev->priv.sf_hw_table;
}
