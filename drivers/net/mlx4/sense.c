/*
 * Copyright (c) 2007 Mellanox Technologies. All rights reserved.
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
 *
 */

#include <linux/errno.h>
#include <linux/if_ether.h>

#include <linux/mlx4/cmd.h>

#include "mlx4.h"

static int mlx4_SENSE_PORT(struct mlx4_dev *dev, int port,
			   enum mlx4_port_type *type)
{
	u64 out_param;
	int err = 0;

	err = mlx4_cmd_imm(dev, 0, &out_param, port, 0,
			   MLX4_CMD_SENSE_PORT, MLX4_CMD_TIME_CLASS_B);
	if (err) {
		mlx4_err(dev, "Sense command failed for port: %d\n", port);
		return err;
	}

	if (out_param > 2) {
		mlx4_err(dev, "Sense returned illegal value: 0x%llx\n", out_param);
		return -EINVAL;
	}

	*type = out_param;
	return 0;
}

void mlx4_do_sense_ports(struct mlx4_dev *dev,
			 enum mlx4_port_type *stype,
			 enum mlx4_port_type *defaults)
{
	struct mlx4_sense *sense = &mlx4_priv(dev)->sense;
	int err;
	int i;

	for (i = 1; i <= dev->caps.num_ports; i++) {
		stype[i - 1] = 0;
		if (sense->do_sense_port[i] && sense->sense_allowed[i] &&
		    dev->caps.possible_type[i] == MLX4_PORT_TYPE_AUTO) {
			err = mlx4_SENSE_PORT(dev, i, &stype[i - 1]);
			if (err)
				stype[i - 1] = defaults[i - 1];
		} else
			stype[i - 1] = defaults[i - 1];
	}

	/*
	 * Adjust port configuration:
	 * If port 1 sensed nothing and port 2 is IB, set both as IB
	 * If port 2 sensed nothing and port 1 is Eth, set both as Eth
	 */
	if (stype[0] == MLX4_PORT_TYPE_ETH) {
		for (i = 1; i < dev->caps.num_ports; i++)
			stype[i] = stype[i] ? stype[i] : MLX4_PORT_TYPE_ETH;
	}
	if (stype[dev->caps.num_ports - 1] == MLX4_PORT_TYPE_IB) {
		for (i = 0; i < dev->caps.num_ports - 1; i++)
			stype[i] = stype[i] ? stype[i] : MLX4_PORT_TYPE_IB;
	}

	/*
	 * If sensed nothing, remain in current configuration.
	 */
	for (i = 0; i < dev->caps.num_ports; i++)
		stype[i] = stype[i] ? stype[i] : defaults[i];

}

static void mlx4_sense_port(struct work_struct *work)
{
	struct delayed_work *delay = to_delayed_work(work);
	struct mlx4_sense *sense = container_of(delay, struct mlx4_sense,
						sense_poll);
	struct mlx4_dev *dev = sense->dev;
	struct mlx4_priv *priv = mlx4_priv(dev);
	enum mlx4_port_type stype[MLX4_MAX_PORTS];

	mutex_lock(&priv->port_mutex);
	mlx4_do_sense_ports(dev, stype, &dev->caps.port_type[1]);

	if (mlx4_check_port_params(dev, stype))
		goto sense_again;

	if (mlx4_change_port_types(dev, stype))
		mlx4_err(dev, "Failed to change port_types\n");

sense_again:
	mutex_unlock(&priv->port_mutex);
	queue_delayed_work(mlx4_wq , &sense->sense_poll,
			   round_jiffies_relative(MLX4_SENSE_RANGE));
}

void mlx4_start_sense(struct mlx4_dev *dev)
{
	struct mlx4_priv *priv = mlx4_priv(dev);
	struct mlx4_sense *sense = &priv->sense;

	if (!(dev->caps.flags & MLX4_DEV_CAP_FLAG_DPDP))
		return;

	queue_delayed_work(mlx4_wq , &sense->sense_poll,
			   round_jiffies_relative(MLX4_SENSE_RANGE));
}

void mlx4_stop_sense(struct mlx4_dev *dev)
{
	cancel_delayed_work_sync(&mlx4_priv(dev)->sense.sense_poll);
}

void  mlx4_sense_init(struct mlx4_dev *dev)
{
	struct mlx4_priv *priv = mlx4_priv(dev);
	struct mlx4_sense *sense = &priv->sense;
	int port;

	sense->dev = dev;
	for (port = 1; port <= dev->caps.num_ports; port++)
		sense->do_sense_port[port] = 1;

	INIT_DELAYED_WORK_DEFERRABLE(&sense->sense_poll, mlx4_sense_port);
}
