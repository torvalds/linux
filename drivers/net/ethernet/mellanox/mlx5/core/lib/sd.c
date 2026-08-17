// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2023, NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#include "lib/sd.h"
#include "../lag/lag.h"
#include "mlx5_core.h"
#include "lib/mlx5.h"
#include "devlink.h"
#include "eswitch.h"
#include "fs_cmd.h"
#include <linux/mlx5/eswitch.h>
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
	u8 state;
	bool primary;
	bool fw_silents_secondaries;
	union {
		struct { /* primary */
			struct mlx5_core_dev *secondaries[MLX5_SD_MAX_GROUP_SZ - 1];
			struct mlx5_flow_table *tx_ft;
			/* Next index for secondary registration */
			u8 next_secondary_idx;
		};
		struct { /* secondary */
			struct mlx5_core_dev *primary_dev;
			u32 alias_obj_id;
			/* TX flow table root in switchdev (silent) config */
			bool tx_root_silent;
		};
	};
};

enum mlx5_sd_state {
	MLX5_SD_STATE_DOWN = 0,
	MLX5_SD_STATE_UP,
};

static int mlx5_sd_get_host_buses(struct mlx5_core_dev *dev)
{
	struct mlx5_sd *sd = mlx5_get_sd(dev);

	if (!sd)
		return 1;

	return sd->host_buses;
}

struct mlx5_core_dev *mlx5_sd_get_primary(struct mlx5_core_dev *dev)
{
	struct mlx5_sd *sd = mlx5_get_sd(dev);

	if (!sd)
		return dev;

	if (!mlx5_devcom_comp_is_ready(sd->devcom))
		return NULL;

	return sd->primary ? dev : sd->primary_dev;
}

struct mlx5_devcom_comp_dev *mlx5_sd_get_devcom(struct mlx5_core_dev *dev)
{
	struct mlx5_sd *sd = mlx5_get_sd(dev);

	if (!sd)
		return NULL;

	return sd->devcom;
}

bool mlx5_sd_is_primary(struct mlx5_core_dev *dev)
{
	struct mlx5_sd *sd = mlx5_get_sd(dev);

	if (!sd)
		return true;

	return sd->primary;
}

int mlx5_sd_pf_num_get(struct mlx5_core_dev *dev)
{
	struct mlx5_sd *sd = mlx5_get_sd(dev);
	int pf_num = mlx5_get_dev_index(dev);
	struct mlx5_core_dev *pos;
	int i;

	if (!sd)
		return pf_num;

	mlx5_devcom_comp_assert_locked(sd->devcom);
	if (!mlx5_devcom_comp_is_ready(sd->devcom))
		return -ENODEV;

	mlx5_sd_for_each_dev(i, mlx5_sd_get_primary(dev), pos)
		if (pos == dev)
			break;

	return pf_num * sd->host_buses + i;
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
	if (is_mdev_switchdev_mode(dev))
		return 0;

	return ch_ix % mlx5_sd_get_host_buses(dev);
}

int mlx5_sd_ch_ix_get_vec_ix(struct mlx5_core_dev *dev, int ch_ix)
{
	if (is_mdev_switchdev_mode(dev))
		return ch_ix;

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

static bool mlx5_sd_caps_supported(struct mlx5_core_dev *dev, u8 host_buses)
{
	/* Honor the SW implementation limit */
	if (host_buses > MLX5_SD_MAX_GROUP_SZ)
		return false;

	/* Disconnect secondaries from the network */
	if (!MLX5_CAP_GEN(dev, eswitch_manager))
		return false;
	if (!MLX5_CAP_GEN(dev, silent_mode_set) &&
	    !MLX5_CAP_GEN(dev, silent_mode_query))
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

bool mlx5_sd_is_supported(struct mlx5_core_dev *dev)
{
	u8 host_buses, sd_group;
	bool sdm;
	int err;

	/* Feature is currently implemented for PFs only */
	if (!mlx5_core_is_pf(dev))
		return false;

	err = mlx5_query_nic_vport_sd_group(dev, &sd_group);
	if (err || !sd_group)
		return false;

	if (!MLX5_CAP_MCAM_REG(dev, mpir))
		return false;

	err = mlx5_query_sd(dev, &sdm, &host_buses);
	if (err || !sdm)
		return false;

	return mlx5_sd_caps_supported(dev, host_buses);
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

	if (!mlx5_sd_caps_supported(dev, host_buses)) {
		sd_warn(dev, "can't support requested netdev combining for group id 0x%x, skipping\n",
			group_id);
		return 0;
	}

	sd = kzalloc_obj(*sd);
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

static int sd_lag_state_show(struct seq_file *file, void *priv)
{
	struct mlx5_core_dev *dev = file->private;
	struct mlx5_lag *ldev;
	struct lag_func *pf;
	bool active = false;
	int i;

	ldev = mlx5_lag_dev(dev);
	if (!ldev)
		return -EINVAL;

	mutex_lock(&ldev->lock);
	mlx5_ldev_for_each(i, 0, ldev) {
		pf = mlx5_lag_pf(ldev, i);
		if (pf->dev == dev) {
			active = pf->sd_fdb_active;
			break;
		}
	}
	mutex_unlock(&ldev->lock);

	seq_printf(file, "%s\n", active ? "active" : "disabled");
	return 0;
}

DEFINE_SHOW_ATTRIBUTE(sd_lag_state);

/* SD LAG integration is optional. If LAG isn't available on this device
 * (e.g. lag caps are off), or registering secondaries fails, just warn
 * and continue - SD can operate without the LAG-side bookkeeping.
 */
static void sd_lag_init(struct mlx5_core_dev *dev)
{
	struct mlx5_core_dev *primary = mlx5_sd_get_primary(dev);
	struct mlx5_sd *sd = mlx5_get_sd(primary);
	struct mlx5_core_dev *pos, *to;
	struct mlx5_lag *ldev;
	struct lag_func *pf;
	int err;
	int i;

	ldev = mlx5_lag_dev(primary);
	if (!ldev) {
		sd_warn(primary, "%s: no ldev (LAG caps off?), skipping\n",
			__func__);
		return;
	}

	mutex_lock(&ldev->lock);
	pf = mlx5_lag_pf_by_dev(ldev, primary);
	if (!pf) {
		sd_warn(primary, "%s: primary not registered in ldev, skipping\n",
			__func__);
		goto out;
	}

	pf->group_id = sd->group_id;

	mlx5_sd_for_each_secondary(i, primary, pos) {
		err = mlx5_ldev_add_mdev(ldev, pos, sd->group_id);
		if (err) {
			sd_warn(primary, "%s: failed to add secondary %s to ldev: %d\n",
				__func__, dev_name(pos->device), err);
			goto err;
		}
	}

out:
	mutex_unlock(&ldev->lock);
	return;

err:
	to = pos;
	mlx5_sd_for_each_secondary_to(i, primary, to, pos)
		mlx5_ldev_remove_mdev(ldev, pos);
	pf->group_id = 0;
	mutex_unlock(&ldev->lock);
}

static void sd_lag_cleanup(struct mlx5_core_dev *dev)
{
	struct mlx5_core_dev *primary = mlx5_sd_get_primary(dev);
	struct mlx5_core_dev *pos;
	struct mlx5_lag *ldev;
	struct lag_func *pf;
	int i;

	ldev = mlx5_lag_dev(primary);
	if (!ldev)
		return;

	mutex_lock(&ldev->lock);
	mlx5_sd_for_each_secondary(i, primary, pos)
		mlx5_ldev_remove_mdev(ldev, pos);

	pf = mlx5_lag_pf_by_dev(ldev, primary);
	if (pf)
		pf->group_id = 0;
	mutex_unlock(&ldev->lock);
}

enum {
	SD_PRIMARY_SET,
	SD_SECONDARIES_SET,
	SD_FW_SILENT_CHECK,
};

static int sd_handle_fw_silent_check(struct mlx5_core_dev *dev,
				     struct mlx5_core_dev *peer)
{
	struct mlx5_sd *peer_sd = mlx5_get_sd(peer);
	struct mlx5_sd *sd = mlx5_get_sd(dev);
	u8 dev_silent = 0, peer_silent = 0;
	int err;

	if (peer_sd->fw_silents_secondaries) {
		sd->fw_silents_secondaries = true;
		return 0;
	}

	err = mlx5_fs_cmd_query_l2table_silent(dev, &dev_silent);
	if (err) {
		sd_warn(dev, "Failed to query silent mode for dev: %d\n", err);
		return err;
	}

	err = mlx5_fs_cmd_query_l2table_silent(peer, &peer_silent);
	if (err) {
		sd_warn(dev, "Failed to query silent mode for peer: %d\n", err);
		return err;
	}

	if (dev_silent || peer_silent) {
		sd->fw_silents_secondaries = true;
		peer_sd->fw_silents_secondaries = true;
		sd_info(dev, "FW indicates at least one device is silent\n");
	}
	return 0;
}

static int sd_handle_primary_set(struct mlx5_core_dev *dev,
				 struct mlx5_core_dev *peer)
{
	struct mlx5_sd *peer_sd = mlx5_get_sd(peer);
	struct mlx5_sd *sd = mlx5_get_sd(dev);
	struct mlx5_core_dev *candidate;
	struct mlx5_sd *candidate_sd;
	bool dev_should_be_primary;

	/* Peer is the device that being sent to all the other devices in the
	 * group. Hence, use peer to get the candidate device.
	 */
	candidate = peer_sd->primary ? peer : peer_sd->primary_dev;

	if (sd->fw_silents_secondaries) {
		u8 candidate_silent = 0;
		int err;

		err = mlx5_fs_cmd_query_l2table_silent(candidate,
						       &candidate_silent);
		if (err) {
			sd_warn(candidate, "Failed to query silent mode for dev: %d\n",
				err);
			return err;
		}
		/* Candidate is silent, dev should be primary */
		dev_should_be_primary = candidate_silent;
	} else {
		/* No FW silent mode, use bus number */
		dev_should_be_primary =
			dev->pdev->bus->number < candidate->pdev->bus->number;
	}

	if (!dev_should_be_primary)
		return 0;

	candidate_sd = mlx5_get_sd(candidate);

	sd->primary = true;
	candidate_sd->primary = false;
	candidate_sd->primary_dev = dev;
	peer_sd->primary = false;
	peer_sd->primary_dev = dev;
	return 0;
}

static void sd_handle_secondaries_set(struct mlx5_core_dev *dev,
				      struct mlx5_core_dev *peer)
{
	struct mlx5_sd *peer_sd = mlx5_get_sd(peer);
	struct mlx5_sd *sd = mlx5_get_sd(dev);
	u8 idx;

	/* Primary has nothing to register with itself. */
	if (sd->primary)
		return;

	/* dev is a secondary device, peer is the primary device.
	 * Secondary registers itself with the primary.
	 */
	idx = peer_sd->next_secondary_idx++;
	peer_sd->secondaries[idx] = dev;
	sd->primary_dev = peer;
}

static int mlx5_sd_devcom_event(int event, void *my_data, void *event_data)
{
	struct mlx5_core_dev *peer = event_data;
	struct mlx5_core_dev *dev = my_data;

	switch (event) {
	case SD_FW_SILENT_CHECK:
		return sd_handle_fw_silent_check(dev, peer);
	case SD_PRIMARY_SET:
		return sd_handle_primary_set(dev, peer);
	case SD_SECONDARIES_SET:
		sd_handle_secondaries_set(dev, peer);
		return 0;
	}

	return 0;
}

static int sd_register(struct mlx5_core_dev *dev)
{
	struct mlx5_devcom_match_attr attr = {};
	struct mlx5_devcom_comp_dev *devcom;
	struct mlx5_core_dev *primary;
	struct mlx5_sd *primary_sd;
	struct mlx5_sd *sd;
	int err;

	sd = mlx5_get_sd(dev);
	attr.key.val = sd->group_id;
	attr.flags = MLX5_DEVCOM_MATCH_FLAGS_NS;
	attr.net = mlx5_core_net(dev);
	devcom = mlx5_devcom_register_component(dev->priv.devc,
						MLX5_DEVCOM_SD_GROUP,
						&attr, mlx5_sd_devcom_event,
						dev);
	if (!devcom)
		return -EINVAL;

	sd->devcom = devcom;

	mlx5_devcom_comp_lock(devcom);
	if (mlx5_devcom_comp_get_size(devcom) != sd->host_buses ||
	    mlx5_devcom_comp_is_ready(devcom))
		goto out;

	/* If silent mode query is supported, ask each device whether it is
	 * silent and propagate the result to the whole group. In each group
	 * only one device is not silent
	 */
	if (MLX5_CAP_GEN(dev, silent_mode_query)) {
		err = mlx5_devcom_locked_send_event(devcom, SD_FW_SILENT_CHECK,
						    SD_FW_SILENT_CHECK, dev);
		if (err)
			goto err_devcom_unreg;
	}

	/* Send SD_PRIMARY_SET event with this device.
	 * All peers will receive this event and compare to this device.
	 * If fw_silents_secondaries is set, choose non-silent device.
	 * Otherwise use bus number.
	 */
	sd->primary = true;
	err = mlx5_devcom_locked_send_event(devcom, SD_PRIMARY_SET,
					    SD_PRIMARY_SET, dev);
	if (err)
		goto err_devcom_unreg;

	/* Broadcast SD_SECONDARIES_SET. Each non-sender peer's handler runs;
	 * the primary's handler returns early so only secondaries register.
	 */
	primary = sd->primary ? dev : sd->primary_dev;
	if (!sd->primary)
		sd_handle_secondaries_set(dev, primary);
	mlx5_devcom_locked_send_event(devcom, SD_SECONDARIES_SET,
				      DEVCOM_CANT_FAIL, primary);

	primary_sd = mlx5_get_sd(primary);
	if (primary_sd->next_secondary_idx + 1 == sd->host_buses)
		mlx5_devcom_comp_set_ready(devcom, true);
out:
	mlx5_devcom_comp_unlock(devcom);
	return 0;

err_devcom_unreg:
	mlx5_devcom_comp_unlock(devcom);
	mlx5_devcom_unregister_component(devcom);
	return err;
}

static void sd_unregister(struct mlx5_core_dev *dev)
{
	struct mlx5_sd *sd = mlx5_get_sd(dev);

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

static int mlx5_sd_secondary_conf_tx_root(struct mlx5_core_dev *secondary,
					  bool disconnect)
{
	struct mlx5_sd *sd = mlx5_get_sd(secondary);
	int err;

	/* Idempotent: skip if TX root is already in the requested state. */
	if (sd->tx_root_silent == disconnect)
		return 0;

	if (disconnect)
		err = mlx5_fs_cmd_set_tx_flow_table_root(secondary, 0, true);
	else
		err = mlx5_fs_cmd_set_tx_flow_table_root(secondary,
							 sd->alias_obj_id,
							 false);
	if (err)
		return err;

	sd->tx_root_silent = disconnect;
	return 0;
}

static int sd_cmd_set_secondary(struct mlx5_core_dev *secondary,
				struct mlx5_core_dev *primary,
				u8 *alias_key)
{
	struct mlx5_sd *primary_sd = mlx5_get_sd(primary);
	struct mlx5_sd *sd = mlx5_get_sd(secondary);
	int err;

	if (!primary_sd->fw_silents_secondaries) {
		err = mlx5_fs_cmd_set_l2table_entry_silent(secondary, 1);
		if (err)
			return err;
	}

	err = sd_secondary_create_alias_ft(secondary, primary, primary_sd->tx_ft,
					   &sd->alias_obj_id, alias_key);
	if (err)
		goto err_unset_silent;

	err = mlx5_fs_cmd_set_tx_flow_table_root(secondary, sd->alias_obj_id,
						 false);
	if (err)
		goto err_destroy_alias_ft;
	sd->tx_root_silent = false;

	return 0;

err_destroy_alias_ft:
	sd_secondary_destroy_alias_ft(secondary);
err_unset_silent:
	if (!primary_sd->fw_silents_secondaries)
		mlx5_fs_cmd_set_l2table_entry_silent(secondary, 0);
	return err;
}

static void sd_cmd_unset_secondary(struct mlx5_core_dev *secondary)
{
	struct mlx5_sd *primary_sd;

	primary_sd = mlx5_get_sd(mlx5_sd_get_primary(secondary));
	mlx5_sd_secondary_conf_tx_root(secondary, true);
	sd_secondary_destroy_alias_ft(secondary);
	if (!primary_sd->fw_silents_secondaries)
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
	struct mlx5_sd *primary_sd;
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

	mlx5_devcom_comp_lock(sd->devcom);
	if (!mlx5_devcom_comp_is_ready(sd->devcom))
		goto out;

	primary = mlx5_sd_get_primary(dev);
	if (!primary)
		goto out;

	primary_sd = mlx5_get_sd(primary);
	if (primary_sd->state != MLX5_SD_STATE_DOWN)
		goto out;

	for (i = 0; i < ACCESS_KEY_LEN; i++)
		alias_key[i] = get_random_u8();

	err = sd_cmd_set_primary(primary, alias_key);
	if (err)
		goto err_sd_unregister;

	mlx5_sd_for_each_secondary(i, primary, pos) {
		err = sd_cmd_set_secondary(pos, primary, alias_key);
		if (err)
			goto err_unset_secondaries;
	}

	sd_lag_init(primary);

	primary_sd->dfs =
		debugfs_create_dir("multi-pf",
				   mlx5_debugfs_get_dev_root(primary));
	mlx5_sd_for_each_secondary(i, primary, pos) {
		char name[32];

		snprintf(name, sizeof(name), "secondary_%d", i - 1);
		debugfs_create_file(name, 0400, primary_sd->dfs, pos,
				    &dev_fops);
	}

	debugfs_create_file("sd_lag_state", 0400, primary_sd->dfs, primary,
			    &sd_lag_state_fops);
	debugfs_create_x32("group_id", 0400, primary_sd->dfs,
			   &primary_sd->group_id);
	debugfs_create_file("primary", 0400, primary_sd->dfs, primary,
			    &dev_fops);

	sd_info(primary, "group id %#x, size %d, combined\n",
		sd->group_id, mlx5_devcom_comp_get_size(sd->devcom));
	sd_print_group(primary);

	primary_sd->state = MLX5_SD_STATE_UP;
out:
	mlx5_devcom_comp_unlock(sd->devcom);
	return 0;

err_unset_secondaries:
	to = pos;
	mlx5_sd_for_each_secondary_to(i, primary, to, pos)
		sd_cmd_unset_secondary(pos);
	sd_cmd_unset_primary(primary);
err_sd_unregister:
	mlx5_sd_for_each_secondary(i, primary, pos) {
		struct mlx5_sd *peer_sd = mlx5_get_sd(pos);

		primary_sd->secondaries[i - 1] = NULL;
		peer_sd->primary_dev = NULL;
	}
	primary_sd->primary = false;
	primary_sd->next_secondary_idx = 0;
	mlx5_devcom_comp_set_ready(sd->devcom, false);
	mlx5_devcom_comp_unlock(sd->devcom);
	sd_unregister(dev);
err_sd_cleanup:
	sd_cleanup(dev);
	return err;
}

void mlx5_sd_cleanup(struct mlx5_core_dev *dev)
{
	struct mlx5_sd *sd = mlx5_get_sd(dev);
	struct mlx5_core_dev *primary, *pos;
	struct mlx5_sd *primary_sd;
	int i;

	if (!sd)
		return;

	mlx5_devcom_comp_lock(sd->devcom);
	if (!mlx5_devcom_comp_is_ready(sd->devcom))
		goto out_unlock;

	primary = mlx5_sd_get_primary(dev);
	if (!primary)
		goto out_ready_false;

	primary_sd = mlx5_get_sd(primary);
	if (primary_sd->state != MLX5_SD_STATE_UP)
		goto out_clear_peers;

	debugfs_remove_recursive(primary_sd->dfs);
	primary_sd->dfs = NULL;
	sd_lag_cleanup(primary);
	mlx5_sd_for_each_secondary(i, primary, pos)
		sd_cmd_unset_secondary(pos);
	sd_cmd_unset_primary(primary);

	sd_info(primary, "group id %#x, uncombined\n", sd->group_id);
	primary_sd->state = MLX5_SD_STATE_DOWN;
out_clear_peers:
	mlx5_sd_for_each_secondary(i, primary, pos) {
		struct mlx5_sd *peer_sd = mlx5_get_sd(pos);

		primary_sd->secondaries[i - 1] = NULL;
		peer_sd->primary_dev = NULL;
	}
	primary_sd->primary = false;
	primary_sd->next_secondary_idx = 0;
out_ready_false:
	mlx5_devcom_comp_set_ready(sd->devcom, false);
out_unlock:
	mlx5_devcom_comp_unlock(sd->devcom);
	sd_unregister(dev);
	sd_cleanup(dev);
}

/* Lock order:
 *   primary:   actual_adev_lock -> SD devcom comp lock
 *   secondary: SD devcom comp lock -> (drop) -> actual_adev_lock
 * The two locks are never held together, so no ABBA.
 */
struct auxiliary_device *mlx5_sd_get_adev(struct mlx5_core_dev *dev,
					  struct auxiliary_device *adev,
					  int idx)
{
	struct mlx5_sd *sd = mlx5_get_sd(dev);
	struct mlx5_core_dev *primary;
	struct mlx5_adev *primary_adev;

	if (!sd)
		return adev;

	mlx5_devcom_comp_lock(sd->devcom);
	if (!mlx5_devcom_comp_is_ready(sd->devcom)) {
		mlx5_devcom_comp_unlock(sd->devcom);
		return NULL;
	}

	primary = mlx5_sd_get_primary(dev);
	if (!primary || dev == primary) {
		mlx5_devcom_comp_unlock(sd->devcom);
		return adev;
	}

	primary_adev = primary->priv.adev[idx];
	get_device(&primary_adev->adev.dev);
	mlx5_devcom_comp_unlock(sd->devcom);

	device_lock(&primary_adev->adev.dev);
	/* Primary may have completed remove between dropping devcom and
	 * acquiring device_lock; recheck.
	 */
	if (!mlx5_devcom_comp_is_ready(sd->devcom)) {
		device_unlock(&primary_adev->adev.dev);
		put_device(&primary_adev->adev.dev);
		return NULL;
	}
	return &primary_adev->adev;
}

#ifdef CONFIG_MLX5_ESWITCH
/* All SD members must have completed esw_offloads_enable (i.e., reached
 * mlx5_esw_offloads_devcom_init) and become eswitch-peers of the primary.
 * Until then, mlx5_eswitch_is_peer() returns false for the not-yet-paired
 * member and shared_fdb_supported_filter would reject. When all PFs transition
 * in parallel, only the last one to finish satisfies this gate; the earlier
 * ones return 0 silently here.
 */
static bool mlx5_sd_all_paired(struct mlx5_core_dev *primary)
{
	struct mlx5_eswitch *primary_esw = primary->priv.eswitch;
	struct mlx5_core_dev *pos;
	int i;

	mlx5_sd_for_each_secondary(i, primary, pos) {
		if (!mlx5_eswitch_is_peer(primary_esw, pos->priv.eswitch))
			return false;
	}
	return true;
}

static void mlx5_sd_activate_shared_fdb(struct mlx5_core_dev *primary)
{
	struct mlx5_sd *sd = mlx5_get_sd(primary);
	struct mlx5_core_dev *pos;
	struct mlx5_lag *ldev;
	struct lag_func *pf;
	int err;
	int i;

	ldev = mlx5_lag_dev(primary);
	if (!ldev) {
		sd_warn(primary, "Shared FDB MUST have ldev\n");
		return;
	}

	mutex_lock(&ldev->lock);

	if (ldev->mode_changes_in_progress)
		goto unlock;

	if (!mlx5_sd_all_paired(primary))
		goto unlock;

	/* Check if SD FDB is already active for this group */
	mlx5_lag_for_each(i, 0, ldev, sd->group_id) {
		pf = mlx5_lag_pf(ldev, i);
		if (pf->sd_fdb_active)
			goto unlock;
		break;
	}

	if (!mlx5_lag_shared_fdb_supported_filter(ldev, sd->group_id)) {
		sd_warn(primary, "Shared FDB not supported\n");
		goto unlock;
	}

	/* Initialize vport metadata for all group devices. This is deferred
	 * from esw_offloads_enable() because mlx5_sd_pf_num_get() requires
	 * the SD group to be ready.
	 */
	mlx5_sd_for_each_dev(i, primary, pos) {
		struct mlx5_eswitch *esw = pos->priv.eswitch;

		err = mlx5_esw_offloads_init_deferred_metadata(esw);
		if (err) {
			sd_warn(primary, "Failed to init metadata for %s: %d\n",
				dev_name(pos->device), err);
			goto unlock;
		}
	}

	err = mlx5_lag_shared_fdb_create(ldev, NULL, 0, sd->group_id);
	if (err)
		sd_warn(primary, "Failed to create shared FDB: %d\n", err);
	else
		sd_info(primary, "Shared FDB created\n");

unlock:
	mutex_unlock(&ldev->lock);
}

void mlx5_sd_eswitch_mode_set(struct mlx5_core_dev *dev, u16 mlx5_mode)
{
	struct mlx5_core_dev *primary;
	struct mlx5_sd *sd;
	int err;

	sd = mlx5_get_sd(dev);
	if (!sd || !mlx5_devcom_comp_is_ready(sd->devcom))
		return;

	mlx5_devcom_comp_lock(sd->devcom);
	if (!mlx5_devcom_comp_is_ready(sd->devcom))
		goto unlock;

	primary = mlx5_sd_get_primary(dev);

	/* Secondary devices need TX root reconfiguration */
	if (dev != primary) {
		bool disconnect = (mlx5_mode == MLX5_ESWITCH_OFFLOADS);

		err = mlx5_sd_secondary_conf_tx_root(dev, disconnect);
		if (err) {
			sd_warn(dev, "Failed to set TX root: %d\n", err);
			goto unlock;
		}
	}

	/* Try to activate shared FDB when all devices are in switchdev.
	 * Shared FDB is optional - failure here doesn't fail the transition.
	 */
	if (mlx5_mode == MLX5_ESWITCH_OFFLOADS)
		mlx5_sd_activate_shared_fdb(primary);

unlock:
	mlx5_devcom_comp_unlock(sd->devcom);
}

#endif /* CONFIG_MLX5_ESWITCH */

void mlx5_sd_put_adev(struct auxiliary_device *actual_adev,
		      struct auxiliary_device *adev)
{
	if (actual_adev != adev) {
		device_unlock(&actual_adev->dev);
		put_device(&actual_adev->dev);
	}
}
