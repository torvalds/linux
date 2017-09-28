/*
 * Copyright (c) 2016, Mellanox Technologies. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <linux/module.h>
#include <linux/mlx5/vport.h>
#include "mlx5_ib.h"

static inline u32 mlx_to_net_policy(enum port_state_policy mlx_policy)
{
	switch (mlx_policy) {
	case MLX5_POLICY_DOWN:
		return IFLA_VF_LINK_STATE_DISABLE;
	case MLX5_POLICY_UP:
		return IFLA_VF_LINK_STATE_ENABLE;
	case MLX5_POLICY_FOLLOW:
		return IFLA_VF_LINK_STATE_AUTO;
	default:
		return __IFLA_VF_LINK_STATE_MAX;
	}
}

int mlx5_ib_get_vf_config(struct ib_device *device, int vf, u8 port,
			  struct ifla_vf_info *info)
{
	struct mlx5_ib_dev *dev = to_mdev(device);
	struct mlx5_core_dev *mdev = dev->mdev;
	struct mlx5_hca_vport_context *rep;
	int err;

	rep = kzalloc(sizeof(*rep), GFP_KERNEL);
	if (!rep)
		return -ENOMEM;

	err = mlx5_query_hca_vport_context(mdev, 1, 1,  vf + 1, rep);
	if (err) {
		mlx5_ib_warn(dev, "failed to query port policy for vf %d (%d)\n",
			     vf, err);
		goto free;
	}
	memset(info, 0, sizeof(*info));
	info->linkstate = mlx_to_net_policy(rep->policy);
	if (info->linkstate == __IFLA_VF_LINK_STATE_MAX)
		err = -EINVAL;

free:
	kfree(rep);
	return err;
}

static inline enum port_state_policy net_to_mlx_policy(int policy)
{
	switch (policy) {
	case IFLA_VF_LINK_STATE_DISABLE:
		return MLX5_POLICY_DOWN;
	case IFLA_VF_LINK_STATE_ENABLE:
		return MLX5_POLICY_UP;
	case IFLA_VF_LINK_STATE_AUTO:
		return MLX5_POLICY_FOLLOW;
	default:
		return MLX5_POLICY_INVALID;
	}
}

int mlx5_ib_set_vf_link_state(struct ib_device *device, int vf,
			      u8 port, int state)
{
	struct mlx5_ib_dev *dev = to_mdev(device);
	struct mlx5_core_dev *mdev = dev->mdev;
	struct mlx5_hca_vport_context *in;
	struct mlx5_vf_context *vfs_ctx = mdev->priv.sriov.vfs_ctx;
	int err;

	in = kzalloc(sizeof(*in), GFP_KERNEL);
	if (!in)
		return -ENOMEM;

	in->policy = net_to_mlx_policy(state);
	if (in->policy == MLX5_POLICY_INVALID) {
		err = -EINVAL;
		goto out;
	}
	in->field_select = MLX5_HCA_VPORT_SEL_STATE_POLICY;
	err = mlx5_core_modify_hca_vport_context(mdev, 1, 1, vf + 1, in);
	if (!err)
		vfs_ctx[vf].policy = in->policy;

out:
	kfree(in);
	return err;
}

int mlx5_ib_get_vf_stats(struct ib_device *device, int vf,
			 u8 port, struct ifla_vf_stats *stats)
{
	int out_sz = MLX5_ST_SZ_BYTES(query_vport_counter_out);
	struct mlx5_core_dev *mdev;
	struct mlx5_ib_dev *dev;
	void *out;
	int err;

	dev = to_mdev(device);
	mdev = dev->mdev;

	out = kzalloc(out_sz, GFP_KERNEL);
	if (!out)
		return -ENOMEM;

	err = mlx5_core_query_vport_counter(mdev, true, vf, port, out, out_sz);
	if (err)
		goto ex;

	stats->rx_packets = MLX5_GET64_PR(query_vport_counter_out, out, received_ib_unicast.packets);
	stats->tx_packets = MLX5_GET64_PR(query_vport_counter_out, out, transmitted_ib_unicast.packets);
	stats->rx_bytes = MLX5_GET64_PR(query_vport_counter_out, out, received_ib_unicast.octets);
	stats->tx_bytes = MLX5_GET64_PR(query_vport_counter_out, out, transmitted_ib_unicast.octets);
	stats->multicast = MLX5_GET64_PR(query_vport_counter_out, out, received_ib_multicast.packets);

ex:
	kfree(out);
	return err;
}

static int set_vf_node_guid(struct ib_device *device, int vf, u8 port, u64 guid)
{
	struct mlx5_ib_dev *dev = to_mdev(device);
	struct mlx5_core_dev *mdev = dev->mdev;
	struct mlx5_hca_vport_context *in;
	struct mlx5_vf_context *vfs_ctx = mdev->priv.sriov.vfs_ctx;
	int err;

	in = kzalloc(sizeof(*in), GFP_KERNEL);
	if (!in)
		return -ENOMEM;

	in->field_select = MLX5_HCA_VPORT_SEL_NODE_GUID;
	in->node_guid = guid;
	err = mlx5_core_modify_hca_vport_context(mdev, 1, 1, vf + 1, in);
	if (!err)
		vfs_ctx[vf].node_guid = guid;
	kfree(in);
	return err;
}

static int set_vf_port_guid(struct ib_device *device, int vf, u8 port, u64 guid)
{
	struct mlx5_ib_dev *dev = to_mdev(device);
	struct mlx5_core_dev *mdev = dev->mdev;
	struct mlx5_hca_vport_context *in;
	struct mlx5_vf_context *vfs_ctx = mdev->priv.sriov.vfs_ctx;
	int err;

	in = kzalloc(sizeof(*in), GFP_KERNEL);
	if (!in)
		return -ENOMEM;

	in->field_select = MLX5_HCA_VPORT_SEL_PORT_GUID;
	in->port_guid = guid;
	err = mlx5_core_modify_hca_vport_context(mdev, 1, 1, vf + 1, in);
	if (!err)
		vfs_ctx[vf].port_guid = guid;
	kfree(in);
	return err;
}

int mlx5_ib_set_vf_guid(struct ib_device *device, int vf, u8 port,
			u64 guid, int type)
{
	if (type == IFLA_VF_IB_NODE_GUID)
		return set_vf_node_guid(device, vf, port, guid);
	else if (type == IFLA_VF_IB_PORT_GUID)
		return set_vf_port_guid(device, vf, port, guid);

	return -EINVAL;
}
