// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2023, NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#include "lib/sd.h"
#include "mlx5_core.h"
#include "lib/mlx5.h"
#include "fs_cmd.h"
#include <linux/mlx5/vport.h>
#include <linux/debugfs.h>

#define sd_info(__dev, format, ...) \
	dev_info((__dev)->device, "Socket-Direct: " format, ##__VA_ARGS__)
#define sd_warn(__dev, format, ...) \
	dev_warn((__dev)->device, "Socket-Direct: " format, ##__VA_ARGS__)

struct mlx5_sd {
	u32 group_id;
	u8 host_buses;
	struct mlx5_devcom_comp_dev *devcom;
	struct dentry *dfs;
	bool primary;
	union {
		struct { /* primary */
			struct mlx5_core_dev *secondaries[MLX5_SD_MAX_GROUP_SZ - 1];
			struct mlx5_flow_table *tx_ft;
		};
		struct { /* secondary */
			struct mlx5_core_dev *primary_dev;
			u32 alias_obj_id;
		};
	};
};

static int mlx5_sd_get_host_buses(struct mlx5_core_dev *dev)
{
	struct mlx5_sd *sd = mlx5_get_sd(dev);

	if (!sd)
		return 1;

	return sd->host_buses;
}

static struct mlx5_core_dev *mlx5_sd_get_primary(struct mlx5_core_dev *dev)
{
	struct mlx5_sd *sd = mlx5_get_sd(dev);

	if (!sd)
		return dev;

	return sd->primary ? dev : sd->primary_dev;
}

struct mlx5_core_dev *
mlx5_sd_primary_get_peer(struct mlx5_core_dev *primary, int idx)
{
	struct mlx5_sd *sd;

	if (idx == 0)
		return primary;

	if (idx >= mlx5_sd_get_host_buses(primary))
		return NULL;

	sd = mlx5_get_sd(primary);
	return sd->secondaries[idx - 1];
}

int mlx5_sd_ch_ix_get_dev_ix(struct mlx5_core_dev *dev, int ch_ix)
{
	return ch_ix % mlx5_sd_get_host_buses(dev);
}

int mlx5_sd_ch_ix_get_vec_ix(struct mlx5_core_dev *dev, int ch_ix)
{
	return ch_ix / mlx5_sd_get_host_buses(dev);
}

struct mlx5_core_dev *mlx5_sd_ch_ix_get_dev(struct mlx5_core_dev *primary, int ch_ix)
{
	int mdev_idx = mlx5_sd_ch_ix_get_dev_ix(primary, ch_ix);

	return mlx5_sd_primary_get_peer(primary, mdev_idx);
}

static bool ft_create_alias_supported(struct mlx5_core_dev *dev)
{
	u64 obj_allowed = MLX5_CAP_GEN_2_64(dev, allowed_object_for_other_vhca_access);
	u32 obj_supp = MLX5_CAP_GEN_2(dev, cross_vhca_object_to_object_supported);

	if (!(obj_supp &
	    MLX5_CROSS_VHCA_OBJ_TO_OBJ_SUPPORTED_LOCAL_FLOW_TABLE_ROOT_TO_REMOTE_FLOW_TABLE))
		return false;

	if (!(obj_allowed & MLX5_ALLOWED_OBJ_FOR_OTHER_VHCA_ACCESS_FLOW_TABLE))
		return false;

	return true;
}

static bool mlx5_sd_is_supported(struct mlx5_core_dev *dev, u8 host_buses)
{
	/* Honor the SW implementation limit */
	if (host_buses > MLX5_SD_MAX_GROUP_SZ)
		return false;

	/* Disconnect secondaries from the network */
	if (!MLX5_CAP_GEN(dev, eswitch_manager))
		return false;
	if (!MLX5_CAP_GEN(dev, silent_mode))
		return false;

	/* RX steering from primary to secondaries */
	if (!MLX5_CAP_GEN(dev, cross_vhca_rqt))
		return false;
	if (host_buses > MLX5_CAP_GEN_2(dev, max_rqt_vhca_id))
		return false;

	/* TX steering from secondaries to primary */
	if (!ft_create_alias_supported(dev))
		return false;
	if (!MLX5_CAP_FLOWTABLE_NIC_TX(dev, reset_root_to_default))
		return false;

	return true;
}

static int mlx5_query_sd(struct mlx5_core_dev *dev, bool *sdm,
			 u8 *host_buses)
{
	u32 out[MLX5_ST_SZ_DW(mpir_reg)];
	int err;

	err = mlx5_query_mpir_reg(dev, out);
	if (err)
		return err;

	*sdm = MLX5_GET(mpir_reg, out, sdm);
	*host_buses = MLX5_GET(mpir_reg, out, host_buses);

	return 0;
}

static u32 mlx5_sd_group_id(struct mlx5_core_dev *dev, u8 sd_group)
{
	return (u32)((MLX5_CAP_GEN(dev, native_port_num) << 8) | sd_group);
}

static int sd_init(struct mlx5_core_dev *dev)
{
	u8 host_buses, sd_group;
	struct mlx5_sd *sd;
	u32 group_id;
	bool sdm;
	int err;

	/* Feature is currently implemented for PFs only */
	if (!mlx5_core_is_pf(dev))
		return 0;

	/* Block on embedded CPU PFs */
	if (mlx5_core_is_ecpf(dev))
		return 0;

	err = mlx5_query_nic_vport_sd_group(dev, &sd_group);
	if (err)
		return err;

	if (!sd_group)
		return 0;

	if (!MLX5_CAP_MCAM_REG(dev, mpir))
		return 0;

	err = mlx5_query_sd(dev, &sdm, &host_buses);
	if (err)
		return err;

	if (!sdm)
		return 0;

	group_id = mlx5_sd_group_id(dev, sd_group);

	if (!mlx5_sd_is_supported(dev, host_buses)) {
		sd_warn(dev, "can't support requested netdev combining for group id 0x%x), skipping\n",
			group_id);
		return 0;
	}

	sd = kzalloc(sizeof(*sd), GFP_KERNEL);
	if (!sd)
		return -ENOMEM;

	sd->host_buses = host_buses;
	sd->group_id = group_id;

	mlx5_set_sd(dev, sd);

	return 0;
}

static void sd_cleanup(struct mlx5_core_dev *dev)
{
	struct mlx5_sd *sd = mlx5_get_sd(dev);

	mlx5_set_sd(dev, NULL);
	kfree(sd);
}

static int sd_register(struct mlx5_core_dev *dev)
{
	struct mlx5_devcom_comp_dev *devcom, *pos;
	struct mlx5_devcom_match_attr attr = {};
	struct mlx5_core_dev *peer, *primary;
	struct mlx5_sd *sd, *primary_sd;
	int err, i;

	sd = mlx5_get_sd(dev);
	attr.key.val = sd->group_id;
	attr.flags = MLX5_DEVCOM_MATCH_FLAGS_NS;
	attr.net = mlx5_core_net(dev);
	devcom = mlx5_devcom_register_component(dev->priv.devc, MLX5_DEVCOM_SD_GROUP,
						&attr, NULL, dev);
	if (!devcom)
		return -EINVAL;

	sd->devcom = devcom;

	if (mlx5_devcom_comp_get_size(devcom) != sd->host_buses)
		return 0;

	mlx5_devcom_comp_lock(devcom);
	mlx5_devcom_comp_set_ready(devcom, true);
	mlx5_devcom_comp_unlock(devcom);

	if (!mlx5_devcom_for_each_peer_begin(devcom)) {
		err = -ENODEV;
		goto err_devcom_unreg;
	}

	primary = dev;
	mlx5_devcom_for_each_peer_entry(devcom, peer, pos)
		if (peer->pdev->bus->number < primary->pdev->bus->number)
			primary = peer;

	primary_sd = mlx5_get_sd(primary);
	primary_sd->primary = true;
	i = 0;
	/* loop the secondaries */
	mlx5_devcom_for_each_peer_entry(primary_sd->devcom, peer, pos) {
		struct mlx5_sd *peer_sd = mlx5_get_sd(peer);

		primary_sd->secondaries[i++] = peer;
		peer_sd->primary = false;
		peer_sd->primary_dev = primary;
	}

	mlx5_devcom_for_each_peer_end(devcom);
	return 0;

err_devcom_unreg:
	mlx5_devcom_comp_lock(sd->devcom);
	mlx5_devcom_comp_set_ready(sd->devcom, false);
	mlx5_devcom_comp_unlock(sd->devcom);
	mlx5_devcom_unregister_component(sd->devcom);
	return err;
}

static void sd_unregister(struct mlx5_core_dev *dev)
{
	struct mlx5_sd *sd = mlx5_get_sd(dev);

	mlx5_devcom_comp_lock(sd->devcom);
	mlx5_devcom_comp_set_ready(sd->devcom, false);
	mlx5_devcom_comp_unlock(sd->devcom);
	mlx5_devcom_unregister_component(sd->devcom);
}

static int sd_cmd_set_primary(struct mlx5_core_dev *primary, u8 *alias_key)
{
	struct mlx5_cmd_allow_other_vhca_access_attr allow_attr = {};
	struct mlx5_sd *sd = mlx5_get_sd(primary);
	struct mlx5_flow_table_attr ft_attr = {};
	struct mlx5_flow_namespace *nic_ns;
	struct mlx5_flow_table *ft;
	int err;

	nic_ns = mlx5_get_flow_namespace(primary, MLX5_FLOW_NAMESPACE_EGRESS);
	if (!nic_ns)
		return -EOPNOTSUPP;

	ft = mlx5_create_flow_table(nic_ns, &ft_attr);
	if (IS_ERR(ft)) {
		err = PTR_ERR(ft);
		return err;
	}
	sd->tx_ft = ft;
	memcpy(allow_attr.access_key, alias_key, ACCESS_KEY_LEN);
	allow_attr.obj_type = MLX5_GENERAL_OBJECT_TYPES_FLOW_TABLE_ALIAS;
	allow_attr.obj_id = (ft->type << FT_ID_FT_TYPE_OFFSET) | ft->id;

	err = mlx5_cmd_allow_other_vhca_access(primary, &allow_attr);
	if (err) {
		mlx5_core_err(primary, "Failed to allow other vhca access err=%d\n",
			      err);
		mlx5_destroy_flow_table(ft);
		return err;
	}

	return 0;
}

static void sd_cmd_unset_primary(struct mlx5_core_dev *primary)
{
	struct mlx5_sd *sd = mlx5_get_sd(primary);

	mlx5_destroy_flow_table(sd->tx_ft);
}

static int sd_secondary_create_alias_ft(struct mlx5_core_dev *secondary,
					struct mlx5_core_dev *primary,
					struct mlx5_flow_table *ft,
					u32 *obj_id, u8 *alias_key)
{
	u32 aliased_object_id = (ft->type << FT_ID_FT_TYPE_OFFSET) | ft->id;
	u16 vhca_id_to_be_accessed = MLX5_CAP_GEN(primary, vhca_id);
	struct mlx5_cmd_alias_obj_create_attr alias_attr = {};
	int ret;

	memcpy(alias_attr.access_key, alias_key, ACCESS_KEY_LEN);
	alias_attr.obj_id = aliased_object_id;
	alias_attr.obj_type = MLX5_GENERAL_OBJECT_TYPES_FLOW_TABLE_ALIAS;
	alias_attr.vhca_id = vhca_id_to_be_accessed;
	ret = mlx5_cmd_alias_obj_create(secondary, &alias_attr, obj_id);
	if (ret) {
		mlx5_core_err(secondary, "Failed to create alias object err=%d\n",
			      ret);
		return ret;
	}

	return 0;
}

static void sd_secondary_destroy_alias_ft(struct mlx5_core_dev *secondary)
{
	struct mlx5_sd *sd = mlx5_get_sd(secondary);

	mlx5_cmd_alias_obj_destroy(secondary, sd->alias_obj_id,
				   MLX5_GENERAL_OBJECT_TYPES_FLOW_TABLE_ALIAS);
}

static int sd_cmd_set_secondary(struct mlx5_core_dev *secondary,
				struct mlx5_core_dev *primary,
				u8 *alias_key)
{
	struct mlx5_sd *primary_sd = mlx5_get_sd(primary);
	struct mlx5_sd *sd = mlx5_get_sd(secondary);
	int err;

	err = mlx5_fs_cmd_set_l2table_entry_silent(secondary, 1);
	if (err)
		return err;

	err = sd_secondary_create_alias_ft(secondary, primary, primary_sd->tx_ft,
					   &sd->alias_obj_id, alias_key);
	if (err)
		goto err_unset_silent;

	err = mlx5_fs_cmd_set_tx_flow_table_root(secondary, sd->alias_obj_id, false);
	if (err)
		goto err_destroy_alias_ft;

	return 0;

err_destroy_alias_ft:
	sd_secondary_destroy_alias_ft(secondary);
err_unset_silent:
	mlx5_fs_cmd_set_l2table_entry_silent(secondary, 0);
	return err;
}

static void sd_cmd_unset_secondary(struct mlx5_core_dev *secondary)
{
	mlx5_fs_cmd_set_tx_flow_table_root(secondary, 0, true);
	sd_secondary_destroy_alias_ft(secondary);
	mlx5_fs_cmd_set_l2table_entry_silent(secondary, 0);
}

static void sd_print_group(struct mlx5_core_dev *primary)
{
	struct mlx5_sd *sd = mlx5_get_sd(primary);
	struct mlx5_core_dev *pos;
	int i;

	sd_info(primary, "group id %#x, primary %s, vhca %#x\n",
		sd->group_id, pci_name(primary->pdev),
		MLX5_CAP_GEN(primary, vhca_id));
	mlx5_sd_for_each_secondary(i, primary, pos)
		sd_info(primary, "group id %#x, secondary_%d %s, vhca %#x\n",
			sd->group_id, i - 1, pci_name(pos->pdev),
			MLX5_CAP_GEN(pos, vhca_id));
}

static ssize_t dev_read(struct file *filp, char __user *buf, size_t count,
			loff_t *pos)
{
	struct mlx5_core_dev *dev;
	char tbuf[32];
	int ret;

	dev = filp->private_data;
	ret = snprintf(tbuf, sizeof(tbuf), "%s vhca %#x\n", pci_name(dev->pdev),
		       MLX5_CAP_GEN(dev, vhca_id));

	return simple_read_from_buffer(buf, count, pos, tbuf, ret);
}

static const struct file_operations dev_fops = {
	.owner	= THIS_MODULE,
	.open	= simple_open,
	.read	= dev_read,
};

int mlx5_sd_init(struct mlx5_core_dev *dev)
{
	struct mlx5_core_dev *primary, *pos, *to;
	struct mlx5_sd *sd = mlx5_get_sd(dev);
	u8 alias_key[ACCESS_KEY_LEN];
	int err, i;

	err = sd_init(dev);
	if (err)
		return err;

	sd = mlx5_get_sd(dev);
	if (!sd)
		return 0;

	err = sd_register(dev);
	if (err)
		goto err_sd_cleanup;

	if (!mlx5_devcom_comp_is_ready(sd->devcom))
		return 0;

	primary = mlx5_sd_get_primary(dev);

	for (i = 0; i < ACCESS_KEY_LEN; i++)
		alias_key[i] = get_random_u8();

	err = sd_cmd_set_primary(primary, alias_key);
	if (err)
		goto err_sd_unregister;

	sd->dfs = debugfs_create_dir("multi-pf", mlx5_debugfs_get_dev_root(primary));
	debugfs_create_x32("group_id", 0400, sd->dfs, &sd->group_id);
	debugfs_create_file("primary", 0400, sd->dfs, primary, &dev_fops);

	mlx5_sd_for_each_secondary(i, primary, pos) {
		char name[32];

		err = sd_cmd_set_secondary(pos, primary, alias_key);
		if (err)
			goto err_unset_secondaries;

		snprintf(name, sizeof(name), "secondary_%d", i - 1);
		debugfs_create_file(name, 0400, sd->dfs, pos, &dev_fops);

	}

	sd_info(primary, "group id %#x, size %d, combined\n",
		sd->group_id, mlx5_devcom_comp_get_size(sd->devcom));
	sd_print_group(primary);

	return 0;

err_unset_secondaries:
	to = pos;
	mlx5_sd_for_each_secondary_to(i, primary, to, pos)
		sd_cmd_unset_secondary(pos);
	sd_cmd_unset_primary(primary);
	debugfs_remove_recursive(sd->dfs);
err_sd_unregister:
	sd_unregister(dev);
err_sd_cleanup:
	sd_cleanup(dev);
	return err;
}

void mlx5_sd_cleanup(struct mlx5_core_dev *dev)
{
	struct mlx5_sd *sd = mlx5_get_sd(dev);
	struct mlx5_core_dev *primary, *pos;
	int i;

	if (!sd)
		return;

	if (!mlx5_devcom_comp_is_ready(sd->devcom))
		goto out;

	primary = mlx5_sd_get_primary(dev);
	mlx5_sd_for_each_secondary(i, primary, pos)
		sd_cmd_unset_secondary(pos);
	sd_cmd_unset_primary(primary);
	debugfs_remove_recursive(sd->dfs);

	sd_info(primary, "group id %#x, uncombined\n", sd->group_id);
out:
	sd_unregister(dev);
	sd_cleanup(dev);
}

struct auxiliary_device *mlx5_sd_get_adev(struct mlx5_core_dev *dev,
					  struct auxiliary_device *adev,
					  int idx)
{
	struct mlx5_sd *sd = mlx5_get_sd(dev);
	struct mlx5_core_dev *primary;

	if (!sd)
		return adev;

	if (!mlx5_devcom_comp_is_ready(sd->devcom))
		return NULL;

	primary = mlx5_sd_get_primary(dev);
	if (dev == primary)
		return adev;

	return &primary->priv.adev[idx]->adev;
}
