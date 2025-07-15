// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
// Copyright (c) 2025, NVIDIA CORPORATION & AFFILIATES.

#include "en.h"
#include "pcie_cong_event.h"

#define MLX5E_CONG_HIGH_STATE 0x7

enum {
	MLX5E_INBOUND_CONG  = BIT(0),
	MLX5E_OUTBOUND_CONG = BIT(1),
};

struct mlx5e_pcie_cong_thresh {
	u16 inbound_high;
	u16 inbound_low;
	u16 outbound_high;
	u16 outbound_low;
};

struct mlx5e_pcie_cong_stats {
	u32 pci_bw_inbound_high;
	u32 pci_bw_inbound_low;
	u32 pci_bw_outbound_high;
	u32 pci_bw_outbound_low;
};

struct mlx5e_pcie_cong_event {
	u64 obj_id;

	struct mlx5e_priv *priv;

	/* For event notifier and workqueue. */
	struct work_struct work;
	struct mlx5_nb nb;

	/* Stores last read state. */
	u8 state;

	/* For ethtool stats group. */
	struct mlx5e_pcie_cong_stats stats;
};

/* In units of 0.01 % */
static const struct mlx5e_pcie_cong_thresh default_thresh_config = {
	.inbound_high = 9000,
	.inbound_low = 7500,
	.outbound_high = 9000,
	.outbound_low = 7500,
};

static const struct counter_desc mlx5e_pcie_cong_stats_desc[] = {
	{ MLX5E_DECLARE_STAT(struct mlx5e_pcie_cong_stats,
			     pci_bw_inbound_high) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_pcie_cong_stats,
			     pci_bw_inbound_low) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_pcie_cong_stats,
			     pci_bw_outbound_high) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_pcie_cong_stats,
			     pci_bw_outbound_low) },
};

#define NUM_PCIE_CONG_COUNTERS ARRAY_SIZE(mlx5e_pcie_cong_stats_desc)

static MLX5E_DECLARE_STATS_GRP_OP_NUM_STATS(pcie_cong)
{
	return priv->cong_event ? NUM_PCIE_CONG_COUNTERS : 0;
}

static MLX5E_DECLARE_STATS_GRP_OP_UPDATE_STATS(pcie_cong) {}

static MLX5E_DECLARE_STATS_GRP_OP_FILL_STRS(pcie_cong)
{
	if (!priv->cong_event)
		return;

	for (int i = 0; i < NUM_PCIE_CONG_COUNTERS; i++)
		ethtool_puts(data, mlx5e_pcie_cong_stats_desc[i].format);
}

static MLX5E_DECLARE_STATS_GRP_OP_FILL_STATS(pcie_cong)
{
	if (!priv->cong_event)
		return;

	for (int i = 0; i < NUM_PCIE_CONG_COUNTERS; i++) {
		u32 ctr = MLX5E_READ_CTR32_CPU(&priv->cong_event->stats,
					       mlx5e_pcie_cong_stats_desc,
					       i);

		mlx5e_ethtool_put_stat(data, ctr);
	}
}

MLX5E_DEFINE_STATS_GRP(pcie_cong, 0);

static int
mlx5_cmd_pcie_cong_event_set(struct mlx5_core_dev *dev,
			     const struct mlx5e_pcie_cong_thresh *config,
			     u64 *obj_id)
{
	u32 in[MLX5_ST_SZ_DW(pcie_cong_event_cmd_in)] = {};
	u32 out[MLX5_ST_SZ_DW(general_obj_out_cmd_hdr)];
	void *cong_obj;
	void *hdr;
	int err;

	hdr = MLX5_ADDR_OF(pcie_cong_event_cmd_in, in, hdr);
	cong_obj = MLX5_ADDR_OF(pcie_cong_event_cmd_in, in, cong_obj);

	MLX5_SET(general_obj_in_cmd_hdr, hdr, opcode,
		 MLX5_CMD_OP_CREATE_GENERAL_OBJECT);

	MLX5_SET(general_obj_in_cmd_hdr, hdr, obj_type,
		 MLX5_GENERAL_OBJECT_TYPES_PCIE_CONG_EVENT);

	MLX5_SET(pcie_cong_event_obj, cong_obj, inbound_event_en, 1);
	MLX5_SET(pcie_cong_event_obj, cong_obj, outbound_event_en, 1);

	MLX5_SET(pcie_cong_event_obj, cong_obj,
		 inbound_cong_high_threshold, config->inbound_high);
	MLX5_SET(pcie_cong_event_obj, cong_obj,
		 inbound_cong_low_threshold, config->inbound_low);

	MLX5_SET(pcie_cong_event_obj, cong_obj,
		 outbound_cong_high_threshold, config->outbound_high);
	MLX5_SET(pcie_cong_event_obj, cong_obj,
		 outbound_cong_low_threshold, config->outbound_low);

	err = mlx5_cmd_exec(dev, in, sizeof(in), out, sizeof(out));
	if (err)
		return err;

	*obj_id = MLX5_GET(general_obj_out_cmd_hdr, out, obj_id);

	mlx5_core_dbg(dev, "PCIe congestion event (obj_id=%llu) created. Config: in: [%u, %u], out: [%u, %u]\n",
		      *obj_id,
		      config->inbound_high, config->inbound_low,
		      config->outbound_high, config->outbound_low);

	return 0;
}

static int mlx5_cmd_pcie_cong_event_destroy(struct mlx5_core_dev *dev,
					    u64 obj_id)
{
	u32 in[MLX5_ST_SZ_DW(pcie_cong_event_cmd_in)] = {};
	u32 out[MLX5_ST_SZ_DW(general_obj_out_cmd_hdr)];
	void *hdr;

	hdr = MLX5_ADDR_OF(pcie_cong_event_cmd_in, in, hdr);
	MLX5_SET(general_obj_in_cmd_hdr, hdr, opcode,
		 MLX5_CMD_OP_DESTROY_GENERAL_OBJECT);
	MLX5_SET(general_obj_in_cmd_hdr, hdr, obj_type,
		 MLX5_GENERAL_OBJECT_TYPES_PCIE_CONG_EVENT);
	MLX5_SET(general_obj_in_cmd_hdr, hdr, obj_id, obj_id);

	return mlx5_cmd_exec(dev, in, sizeof(in), out, sizeof(out));
}

static int mlx5_cmd_pcie_cong_event_query(struct mlx5_core_dev *dev,
					  u64 obj_id,
					  u32 *state)
{
	u32 in[MLX5_ST_SZ_DW(pcie_cong_event_cmd_in)] = {};
	u32 out[MLX5_ST_SZ_DW(pcie_cong_event_cmd_out)];
	void *obj;
	void *hdr;
	u8 cong;
	int err;

	hdr = MLX5_ADDR_OF(pcie_cong_event_cmd_in, in, hdr);

	MLX5_SET(general_obj_in_cmd_hdr, hdr, opcode,
		 MLX5_CMD_OP_QUERY_GENERAL_OBJECT);
	MLX5_SET(general_obj_in_cmd_hdr, hdr, obj_type,
		 MLX5_GENERAL_OBJECT_TYPES_PCIE_CONG_EVENT);
	MLX5_SET(general_obj_in_cmd_hdr, hdr, obj_id, obj_id);

	err = mlx5_cmd_exec(dev, in, sizeof(in), out, sizeof(out));
	if (err)
		return err;

	obj = MLX5_ADDR_OF(pcie_cong_event_cmd_out, out, cong_obj);

	if (state) {
		cong = MLX5_GET(pcie_cong_event_obj, obj, inbound_cong_state);
		if (cong == MLX5E_CONG_HIGH_STATE)
			*state |= MLX5E_INBOUND_CONG;

		cong = MLX5_GET(pcie_cong_event_obj, obj, outbound_cong_state);
		if (cong == MLX5E_CONG_HIGH_STATE)
			*state |= MLX5E_OUTBOUND_CONG;
	}

	return 0;
}

static void mlx5e_pcie_cong_event_work(struct work_struct *work)
{
	struct mlx5e_pcie_cong_event *cong_event;
	struct mlx5_core_dev *dev;
	struct mlx5e_priv *priv;
	u32 new_cong_state = 0;
	u32 changes;
	int err;

	cong_event = container_of(work, struct mlx5e_pcie_cong_event, work);
	priv = cong_event->priv;
	dev = priv->mdev;

	err = mlx5_cmd_pcie_cong_event_query(dev, cong_event->obj_id,
					     &new_cong_state);
	if (err) {
		mlx5_core_warn(dev, "Error %d when querying PCIe cong event object (obj_id=%llu).\n",
			       err, cong_event->obj_id);
		return;
	}

	changes = cong_event->state ^ new_cong_state;
	if (!changes)
		return;

	cong_event->state = new_cong_state;

	if (changes & MLX5E_INBOUND_CONG) {
		if (new_cong_state & MLX5E_INBOUND_CONG)
			cong_event->stats.pci_bw_inbound_high++;
		else
			cong_event->stats.pci_bw_inbound_low++;
	}

	if (changes & MLX5E_OUTBOUND_CONG) {
		if (new_cong_state & MLX5E_OUTBOUND_CONG)
			cong_event->stats.pci_bw_outbound_high++;
		else
			cong_event->stats.pci_bw_outbound_low++;
	}
}

static int mlx5e_pcie_cong_event_handler(struct notifier_block *nb,
					 unsigned long event, void *eqe)
{
	struct mlx5e_pcie_cong_event *cong_event;

	cong_event = mlx5_nb_cof(nb, struct mlx5e_pcie_cong_event, nb);
	queue_work(cong_event->priv->wq, &cong_event->work);

	return NOTIFY_OK;
}

int mlx5e_pcie_cong_event_init(struct mlx5e_priv *priv)
{
	struct mlx5e_pcie_cong_event *cong_event;
	struct mlx5_core_dev *mdev = priv->mdev;
	int err;

	if (!mlx5_pcie_cong_event_supported(mdev))
		return 0;

	cong_event = kvzalloc_node(sizeof(*cong_event), GFP_KERNEL,
				   mdev->priv.numa_node);
	if (!cong_event)
		return -ENOMEM;

	INIT_WORK(&cong_event->work, mlx5e_pcie_cong_event_work);
	MLX5_NB_INIT(&cong_event->nb, mlx5e_pcie_cong_event_handler,
		     OBJECT_CHANGE);

	cong_event->priv = priv;

	err = mlx5_cmd_pcie_cong_event_set(mdev, &default_thresh_config,
					   &cong_event->obj_id);
	if (err) {
		mlx5_core_warn(mdev, "Error creating a PCIe congestion event object\n");
		goto err_free;
	}

	err = mlx5_eq_notifier_register(mdev, &cong_event->nb);
	if (err) {
		mlx5_core_warn(mdev, "Error registering notifier for the PCIe congestion event\n");
		goto err_obj_destroy;
	}

	priv->cong_event = cong_event;

	return 0;

err_obj_destroy:
	mlx5_cmd_pcie_cong_event_destroy(mdev, cong_event->obj_id);
err_free:
	kvfree(cong_event);

	return err;
}

void mlx5e_pcie_cong_event_cleanup(struct mlx5e_priv *priv)
{
	struct mlx5e_pcie_cong_event *cong_event = priv->cong_event;
	struct mlx5_core_dev *mdev = priv->mdev;

	if (!cong_event)
		return;

	priv->cong_event = NULL;

	mlx5_eq_notifier_unregister(mdev, &cong_event->nb);
	cancel_work_sync(&cong_event->work);

	if (mlx5_cmd_pcie_cong_event_destroy(mdev, cong_event->obj_id))
		mlx5_core_warn(mdev, "Error destroying PCIe congestion event (obj_id=%llu)\n",
			       cong_event->obj_id);

	kvfree(cong_event);
}
