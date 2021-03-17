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
#include <linux/refcount.h>
#include <linux/mlx5/driver.h>
#include <net/vxlan.h>
#include "mlx5_core.h"
#include "vxlan.h"

struct mlx5_vxlan {
	struct mlx5_core_dev		*mdev;
	/* max_num_ports is usuallly 4, 16 buckets is more than enough */
	DECLARE_HASHTABLE(htable, 4);
	struct mutex                    sync_lock; /* sync add/del port HW operations */
};

struct mlx5_vxlan_port {
	struct hlist_node hlist;
	u16 udp_port;
};

static int mlx5_vxlan_core_add_port_cmd(struct mlx5_core_dev *mdev, u16 port)
{
	u32 in[MLX5_ST_SZ_DW(add_vxlan_udp_dport_in)] = {};

	MLX5_SET(add_vxlan_udp_dport_in, in, opcode,
		 MLX5_CMD_OP_ADD_VXLAN_UDP_DPORT);
	MLX5_SET(add_vxlan_udp_dport_in, in, vxlan_udp_port, port);
	return mlx5_cmd_exec_in(mdev, add_vxlan_udp_dport, in);
}

static int mlx5_vxlan_core_del_port_cmd(struct mlx5_core_dev *mdev, u16 port)
{
	u32 in[MLX5_ST_SZ_DW(delete_vxlan_udp_dport_in)] = {};

	MLX5_SET(delete_vxlan_udp_dport_in, in, opcode,
		 MLX5_CMD_OP_DELETE_VXLAN_UDP_DPORT);
	MLX5_SET(delete_vxlan_udp_dport_in, in, vxlan_udp_port, port);
	return mlx5_cmd_exec_in(mdev, delete_vxlan_udp_dport, in);
}

bool mlx5_vxlan_lookup_port(struct mlx5_vxlan *vxlan, u16 port)
{
	struct mlx5_vxlan_port *vxlanp;
	bool found = false;

	if (!mlx5_vxlan_allowed(vxlan))
		return NULL;

	rcu_read_lock();
	hash_for_each_possible_rcu(vxlan->htable, vxlanp, hlist, port)
		if (vxlanp->udp_port == port) {
			found = true;
			break;
		}
	rcu_read_unlock();

	return found;
}

static struct mlx5_vxlan_port *vxlan_lookup_port(struct mlx5_vxlan *vxlan, u16 port)
{
	struct mlx5_vxlan_port *vxlanp;

	hash_for_each_possible(vxlan->htable, vxlanp, hlist, port)
		if (vxlanp->udp_port == port)
			return vxlanp;
	return NULL;
}

int mlx5_vxlan_add_port(struct mlx5_vxlan *vxlan, u16 port)
{
	struct mlx5_vxlan_port *vxlanp;
	int ret;

	vxlanp = kzalloc(sizeof(*vxlanp), GFP_KERNEL);
	if (!vxlanp)
		return -ENOMEM;
	vxlanp->udp_port = port;

	ret = mlx5_vxlan_core_add_port_cmd(vxlan->mdev, port);
	if (ret) {
		kfree(vxlanp);
		return ret;
	}

	mutex_lock(&vxlan->sync_lock);
	hash_add_rcu(vxlan->htable, &vxlanp->hlist, port);
	mutex_unlock(&vxlan->sync_lock);

	return 0;
}

int mlx5_vxlan_del_port(struct mlx5_vxlan *vxlan, u16 port)
{
	struct mlx5_vxlan_port *vxlanp;
	int ret = 0;

	mutex_lock(&vxlan->sync_lock);

	vxlanp = vxlan_lookup_port(vxlan, port);
	if (WARN_ON(!vxlanp)) {
		ret = -ENOENT;
		goto out_unlock;
	}

	hash_del_rcu(&vxlanp->hlist);
	synchronize_rcu();
	mlx5_vxlan_core_del_port_cmd(vxlan->mdev, port);
	kfree(vxlanp);

out_unlock:
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
	hash_init(vxlan->htable);

	/* Hardware adds 4789 (IANA_VXLAN_UDP_PORT) by default */
	mlx5_vxlan_add_port(vxlan, IANA_VXLAN_UDP_PORT);

	return vxlan;
}

void mlx5_vxlan_destroy(struct mlx5_vxlan *vxlan)
{
	if (!mlx5_vxlan_allowed(vxlan))
		return;

	mlx5_vxlan_del_port(vxlan, IANA_VXLAN_UDP_PORT);
	WARN_ON(!hash_empty(vxlan->htable));

	kfree(vxlan);
}

void mlx5_vxlan_reset_to_default(struct mlx5_vxlan *vxlan)
{
	struct mlx5_vxlan_port *vxlanp;
	struct hlist_node *tmp;
	int bkt;

	if (!mlx5_vxlan_allowed(vxlan))
		return;

	hash_for_each_safe(vxlan->htable, bkt, tmp, vxlanp, hlist) {
		/* Don't delete default UDP port added by the HW.
		 * Remove only user configured ports
		 */
		if (vxlanp->udp_port == IANA_VXLAN_UDP_PORT)
			continue;
		mlx5_vxlan_del_port(vxlan, vxlanp->udp_port);
	}
}
