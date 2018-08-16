/*
 * Copyright (c) 2016, Mellanox Technologies, Ltd.  All rights reserved.
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

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mlx5/driver.h>
#include "mlx5_core.h"
#include "vxlan.h"

struct mlx5_vxlan {
	struct mlx5_core_dev		*mdev;
	spinlock_t			lock; /* protect vxlan table */
	/* max_num_ports is usuallly 4, 16 buckets is more than enough */
	DECLARE_HASHTABLE(htable, 4);
	int				num_ports;
	struct mutex                    sync_lock; /* sync add/del port HW operations */
};

struct mlx5_vxlan_port {
	struct hlist_node hlist;
	atomic_t refcount;
	u16 udp_port;
};

static inline u8 mlx5_vxlan_max_udp_ports(struct mlx5_core_dev *mdev)
{
	return MLX5_CAP_ETH(mdev, max_vxlan_udp_ports) ?: 4;
}

static int mlx5_vxlan_core_add_port_cmd(struct mlx5_core_dev *mdev, u16 port)
{
	u32 in[MLX5_ST_SZ_DW(add_vxlan_udp_dport_in)]   = {0};
	u32 out[MLX5_ST_SZ_DW(add_vxlan_udp_dport_out)] = {0};

	MLX5_SET(add_vxlan_udp_dport_in, in, opcode,
		 MLX5_CMD_OP_ADD_VXLAN_UDP_DPORT);
	MLX5_SET(add_vxlan_udp_dport_in, in, vxlan_udp_port, port);
	return mlx5_cmd_exec(mdev, in, sizeof(in), out, sizeof(out));
}

static int mlx5_vxlan_core_del_port_cmd(struct mlx5_core_dev *mdev, u16 port)
{
	u32 in[MLX5_ST_SZ_DW(delete_vxlan_udp_dport_in)]   = {0};
	u32 out[MLX5_ST_SZ_DW(delete_vxlan_udp_dport_out)] = {0};

	MLX5_SET(delete_vxlan_udp_dport_in, in, opcode,
		 MLX5_CMD_OP_DELETE_VXLAN_UDP_DPORT);
	MLX5_SET(delete_vxlan_udp_dport_in, in, vxlan_udp_port, port);
	return mlx5_cmd_exec(mdev, in, sizeof(in), out, sizeof(out));
}

static struct mlx5_vxlan_port*
mlx5_vxlan_lookup_port_locked(struct mlx5_vxlan *vxlan, u16 port)
{
	struct mlx5_vxlan_port *vxlanp;

	hash_for_each_possible(vxlan->htable, vxlanp, hlist, port) {
		if (vxlanp->udp_port == port)
			return vxlanp;
	}

	return NULL;
}

struct mlx5_vxlan_port *mlx5_vxlan_lookup_port(struct mlx5_vxlan *vxlan, u16 port)
{
	struct mlx5_vxlan_port *vxlanp;

	if (!mlx5_vxlan_allowed(vxlan))
		return NULL;

	spin_lock_bh(&vxlan->lock);
	vxlanp = mlx5_vxlan_lookup_port_locked(vxlan, port);
	spin_unlock_bh(&vxlan->lock);

	return vxlanp;
}

int mlx5_vxlan_add_port(struct mlx5_vxlan *vxlan, u16 port)
{
	struct mlx5_vxlan_port *vxlanp;
	int ret = -ENOSPC;

	vxlanp = mlx5_vxlan_lookup_port(vxlan, port);
	if (vxlanp) {
		atomic_inc(&vxlanp->refcount);
		return 0;
	}

	mutex_lock(&vxlan->sync_lock);
	if (vxlan->num_ports >= mlx5_vxlan_max_udp_ports(vxlan->mdev)) {
		mlx5_core_info(vxlan->mdev,
			       "UDP port (%d) not offloaded, max number of UDP ports (%d) are already offloaded\n",
			       port, mlx5_vxlan_max_udp_ports(vxlan->mdev));
		ret = -ENOSPC;
		goto unlock;
	}

	ret = mlx5_vxlan_core_add_port_cmd(vxlan->mdev, port);
	if (ret)
		goto unlock;

	vxlanp = kzalloc(sizeof(*vxlanp), GFP_KERNEL);
	if (!vxlanp) {
		ret = -ENOMEM;
		goto err_delete_port;
	}

	vxlanp->udp_port = port;
	atomic_set(&vxlanp->refcount, 1);

	spin_lock_bh(&vxlan->lock);
	hash_add(vxlan->htable, &vxlanp->hlist, port);
	spin_unlock_bh(&vxlan->lock);

	vxlan->num_ports++;
	mutex_unlock(&vxlan->sync_lock);
	return 0;

err_delete_port:
	mlx5_vxlan_core_del_port_cmd(vxlan->mdev, port);

unlock:
	mutex_unlock(&vxlan->sync_lock);
	return ret;
}

int mlx5_vxlan_del_port(struct mlx5_vxlan *vxlan, u16 port)
{
	struct mlx5_vxlan_port *vxlanp;
	bool remove = false;
	int ret = 0;

	mutex_lock(&vxlan->sync_lock);

	spin_lock_bh(&vxlan->lock);
	vxlanp = mlx5_vxlan_lookup_port_locked(vxlan, port);
	if (!vxlanp) {
		ret = -ENOENT;
		goto out_unlock;
	}

	if (atomic_dec_and_test(&vxlanp->refcount)) {
		hash_del(&vxlanp->hlist);
		remove = true;
	}

out_unlock:
	spin_unlock_bh(&vxlan->lock);

	if (remove) {
		mlx5_vxlan_core_del_port_cmd(vxlan->mdev, port);
		kfree(vxlanp);
		vxlan->num_ports--;
	}

	mutex_unlock(&vxlan->sync_lock);

	return ret;
}

struct mlx5_vxlan *mlx5_vxlan_create(struct mlx5_core_dev *mdev)
{
	struct mlx5_vxlan *vxlan;

	if (!MLX5_CAP_ETH(mdev, tunnel_stateless_vxlan) || !mlx5_core_is_pf(mdev))
		return ERR_PTR(-ENOTSUPP);

	vxlan = kzalloc(sizeof(*vxlan), GFP_KERNEL);
	if (!vxlan)
		return ERR_PTR(-ENOMEM);

	vxlan->mdev = mdev;
	mutex_init(&vxlan->sync_lock);
	spin_lock_init(&vxlan->lock);
	hash_init(vxlan->htable);

	/* Hardware adds 4789 by default */
	mlx5_vxlan_add_port(vxlan, 4789);

	return vxlan;
}

void mlx5_vxlan_destroy(struct mlx5_vxlan *vxlan)
{
	struct mlx5_vxlan_port *vxlanp;
	struct hlist_node *tmp;
	int bkt;

	if (!mlx5_vxlan_allowed(vxlan))
		return;

	/* Lockless since we are the only hash table consumers*/
	hash_for_each_safe(vxlan->htable, bkt, tmp, vxlanp, hlist) {
		hash_del(&vxlanp->hlist);
		mlx5_vxlan_core_del_port_cmd(vxlan->mdev, vxlanp->udp_port);
		kfree(vxlanp);
	}

	kfree(vxlan);
}
