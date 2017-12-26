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

static void mlx5e_vxlan_add_port(struct mlx5e_priv *priv, u16 port);

void mlx5e_vxlan_init(struct mlx5e_priv *priv)
{
	struct mlx5e_vxlan_db *vxlan_db = &priv->vxlan;

	spin_lock_init(&vxlan_db->lock);
	hash_init(vxlan_db->htable);

	if (mlx5e_vxlan_allowed(priv->mdev))
		/* Hardware adds 4789 by default.
		 * Lockless since we are the only hash table consumers, wq and TX are disabled.
		 */
		mlx5e_vxlan_add_port(priv, 4789);
}

static inline u8 mlx5e_vxlan_max_udp_ports(struct mlx5_core_dev *mdev)
{
	return MLX5_CAP_ETH(mdev, max_vxlan_udp_ports) ?: 4;
}

static int mlx5e_vxlan_core_add_port_cmd(struct mlx5_core_dev *mdev, u16 port)
{
	u32 in[MLX5_ST_SZ_DW(add_vxlan_udp_dport_in)]   = {0};
	u32 out[MLX5_ST_SZ_DW(add_vxlan_udp_dport_out)] = {0};

	MLX5_SET(add_vxlan_udp_dport_in, in, opcode,
		 MLX5_CMD_OP_ADD_VXLAN_UDP_DPORT);
	MLX5_SET(add_vxlan_udp_dport_in, in, vxlan_udp_port, port);
	return mlx5_cmd_exec(mdev, in, sizeof(in), out, sizeof(out));
}

static int mlx5e_vxlan_core_del_port_cmd(struct mlx5_core_dev *mdev, u16 port)
{
	u32 in[MLX5_ST_SZ_DW(delete_vxlan_udp_dport_in)]   = {0};
	u32 out[MLX5_ST_SZ_DW(delete_vxlan_udp_dport_out)] = {0};

	MLX5_SET(delete_vxlan_udp_dport_in, in, opcode,
		 MLX5_CMD_OP_DELETE_VXLAN_UDP_DPORT);
	MLX5_SET(delete_vxlan_udp_dport_in, in, vxlan_udp_port, port);
	return mlx5_cmd_exec(mdev, in, sizeof(in), out, sizeof(out));
}

static struct mlx5e_vxlan *mlx5e_vxlan_lookup_port_locked(struct mlx5e_priv *priv,
							  u16 port)
{
	struct mlx5e_vxlan_db *vxlan_db = &priv->vxlan;
	struct mlx5e_vxlan    *vxlan;

	hash_for_each_possible(vxlan_db->htable, vxlan, hlist, port) {
		if (vxlan->udp_port == port)
			return vxlan;
	}

	return NULL;
}

struct mlx5e_vxlan *mlx5e_vxlan_lookup_port(struct mlx5e_priv *priv, u16 port)
{
	struct mlx5e_vxlan_db *vxlan_db = &priv->vxlan;
	struct mlx5e_vxlan *vxlan;

	spin_lock_bh(&vxlan_db->lock);
	vxlan = mlx5e_vxlan_lookup_port_locked(priv, port);
	spin_unlock_bh(&vxlan_db->lock);

	return vxlan;
}

static void mlx5e_vxlan_add_port(struct mlx5e_priv *priv, u16 port)
{
	struct mlx5e_vxlan_db *vxlan_db = &priv->vxlan;
	struct mlx5e_vxlan *vxlan;

	vxlan = mlx5e_vxlan_lookup_port(priv, port);
	if (vxlan) {
		atomic_inc(&vxlan->refcount);
		return;
	}

	if (vxlan_db->num_ports >= mlx5e_vxlan_max_udp_ports(priv->mdev)) {
		netdev_info(priv->netdev,
			    "UDP port (%d) not offloaded, max number of UDP ports (%d) are already offloaded\n",
			    port, mlx5e_vxlan_max_udp_ports(priv->mdev));
		return;
	}

	if (mlx5e_vxlan_core_add_port_cmd(priv->mdev, port))
		return;

	vxlan = kzalloc(sizeof(*vxlan), GFP_KERNEL);
	if (!vxlan)
		goto err_delete_port;

	vxlan->udp_port = port;
	atomic_set(&vxlan->refcount, 1);

	spin_lock_bh(&vxlan_db->lock);
	hash_add(vxlan_db->htable, &vxlan->hlist, port);
	spin_unlock_bh(&vxlan_db->lock);

	vxlan_db->num_ports++;
	return;

err_delete_port:
	mlx5e_vxlan_core_del_port_cmd(priv->mdev, port);
}

static void mlx5e_vxlan_add_work(struct work_struct *work)
{
	struct mlx5e_vxlan_work *vxlan_work =
		container_of(work, struct mlx5e_vxlan_work, work);
	struct mlx5e_priv *priv = vxlan_work->priv;
	u16 port = vxlan_work->port;

	mutex_lock(&priv->state_lock);
	mlx5e_vxlan_add_port(priv, port);
	mutex_unlock(&priv->state_lock);

	kfree(vxlan_work);
}

static void mlx5e_vxlan_del_work(struct work_struct *work)
{
	struct mlx5e_vxlan_work *vxlan_work =
		container_of(work, struct mlx5e_vxlan_work, work);
	struct mlx5e_priv *priv         = vxlan_work->priv;
	struct mlx5e_vxlan_db *vxlan_db = &priv->vxlan;
	u16 port = vxlan_work->port;
	struct mlx5e_vxlan *vxlan;
	bool remove = false;

	mutex_lock(&priv->state_lock);
	spin_lock_bh(&vxlan_db->lock);
	vxlan = mlx5e_vxlan_lookup_port_locked(priv, port);
	if (!vxlan)
		goto out_unlock;

	if (atomic_dec_and_test(&vxlan->refcount)) {
		hash_del(&vxlan->hlist);
		remove = true;
	}

out_unlock:
	spin_unlock_bh(&vxlan_db->lock);

	if (remove) {
		mlx5e_vxlan_core_del_port_cmd(priv->mdev, port);
		kfree(vxlan);
		vxlan_db->num_ports--;
	}
	mutex_unlock(&priv->state_lock);
	kfree(vxlan_work);
}

void mlx5e_vxlan_queue_work(struct mlx5e_priv *priv, sa_family_t sa_family,
			    u16 port, int add)
{
	struct mlx5e_vxlan_work *vxlan_work;

	vxlan_work = kmalloc(sizeof(*vxlan_work), GFP_ATOMIC);
	if (!vxlan_work)
		return;

	if (add)
		INIT_WORK(&vxlan_work->work, mlx5e_vxlan_add_work);
	else
		INIT_WORK(&vxlan_work->work, mlx5e_vxlan_del_work);

	vxlan_work->priv = priv;
	vxlan_work->port = port;
	vxlan_work->sa_family = sa_family;
	queue_work(priv->wq, &vxlan_work->work);
}

void mlx5e_vxlan_cleanup(struct mlx5e_priv *priv)
{
	struct mlx5e_vxlan_db *vxlan_db = &priv->vxlan;
	struct mlx5e_vxlan *vxlan;
	struct hlist_node *tmp;
	int bkt;

	/* Lockless since we are the only hash table consumers, wq and TX are disabled */
	hash_for_each_safe(vxlan_db->htable, bkt, tmp, vxlan, hlist) {
		hash_del(&vxlan->hlist);
		mlx5e_vxlan_core_del_port_cmd(priv->mdev, vxlan->udp_port);
		kfree(vxlan);
	}
}
