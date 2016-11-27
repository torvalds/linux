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

#include "en.h"

enum {
	MLX5E_ST_LINK_STATE,
	MLX5E_ST_LINK_SPEED,
	MLX5E_ST_HEALTH_INFO,
	MLX5E_ST_NUM,
};

const char mlx5e_self_tests[MLX5E_ST_NUM][ETH_GSTRING_LEN] = {
	"Link Test",
	"Speed Test",
	"Health Test",
};

int mlx5e_self_test_num(struct mlx5e_priv *priv)
{
	return ARRAY_SIZE(mlx5e_self_tests);
}

static int mlx5e_test_health_info(struct mlx5e_priv *priv)
{
	struct mlx5_core_health *health = &priv->mdev->priv.health;

	return health->sick ? 1 : 0;
}

static int mlx5e_test_link_state(struct mlx5e_priv *priv)
{
	u8 port_state;

	if (!netif_carrier_ok(priv->netdev))
		return 1;

	port_state = mlx5_query_vport_state(priv->mdev, MLX5_QUERY_VPORT_STATE_IN_OP_MOD_VNIC_VPORT, 0);
	return port_state == VPORT_STATE_UP ? 0 : 1;
}

static int mlx5e_test_link_speed(struct mlx5e_priv *priv)
{
	u32 out[MLX5_ST_SZ_DW(ptys_reg)];
	u32 eth_proto_oper;
	int i;

	if (!netif_carrier_ok(priv->netdev))
		return 1;

	if (mlx5_query_port_ptys(priv->mdev, out, sizeof(out), MLX5_PTYS_EN, 1))
		return 1;

	eth_proto_oper = MLX5_GET(ptys_reg, out, eth_proto_oper);
	for (i = 0; i < MLX5E_LINK_MODES_NUMBER; i++) {
		if (eth_proto_oper & MLX5E_PROT_MASK(i))
			return 0;
	}
	return 1;
}

static int (*mlx5e_st_func[MLX5E_ST_NUM])(struct mlx5e_priv *) = {
	mlx5e_test_link_state,
	mlx5e_test_link_speed,
	mlx5e_test_health_info,
};

void mlx5e_self_test(struct net_device *ndev, struct ethtool_test *etest,
		     u64 *buf)
{
	struct mlx5e_priv *priv = netdev_priv(ndev);
	int i;

	memset(buf, 0, sizeof(u64) * MLX5E_ST_NUM);

	mutex_lock(&priv->state_lock);
	netdev_info(ndev, "Self test begin..\n");

	for (i = 0; i < MLX5E_ST_NUM; i++) {
		netdev_info(ndev, "\t[%d] %s start..\n",
			    i, mlx5e_self_tests[i]);
		buf[i] = mlx5e_st_func[i](priv);
		netdev_info(ndev, "\t[%d] %s end: result(%lld)\n",
			    i, mlx5e_self_tests[i], buf[i]);
	}

	mutex_unlock(&priv->state_lock);

	for (i = 0; i < MLX5E_ST_NUM; i++) {
		if (buf[i]) {
			etest->flags |= ETH_TEST_FL_FAILED;
			break;
		}
	}
	netdev_info(ndev, "Self test out: status flags(0x%x)\n",
		    etest->flags);
}
