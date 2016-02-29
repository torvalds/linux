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

#include "mlx5_ib.h"

struct mlx5_ib_gsi_qp {
	struct ib_qp ibqp;
	struct ib_qp *rx_qp;
	u8 port_num;
	struct ib_qp_cap cap;
	enum ib_sig_type sq_sig_type;
	/* Serialize qp state modifications */
	struct mutex mutex;
};

static struct mlx5_ib_gsi_qp *gsi_qp(struct ib_qp *qp)
{
	return container_of(qp, struct mlx5_ib_gsi_qp, ibqp);
}

struct ib_qp *mlx5_ib_gsi_create_qp(struct ib_pd *pd,
				    struct ib_qp_init_attr *init_attr)
{
	struct mlx5_ib_dev *dev = to_mdev(pd->device);
	struct mlx5_ib_gsi_qp *gsi;
	struct ib_qp_init_attr hw_init_attr = *init_attr;
	const u8 port_num = init_attr->port_num;
	int ret;

	mlx5_ib_dbg(dev, "creating GSI QP\n");

	if (port_num > ARRAY_SIZE(dev->devr.ports) || port_num < 1) {
		mlx5_ib_warn(dev,
			     "invalid port number %d during GSI QP creation\n",
			     port_num);
		return ERR_PTR(-EINVAL);
	}

	gsi = kzalloc(sizeof(*gsi), GFP_KERNEL);
	if (!gsi)
		return ERR_PTR(-ENOMEM);

	mutex_init(&gsi->mutex);

	mutex_lock(&dev->devr.mutex);

	if (dev->devr.ports[port_num - 1].gsi) {
		mlx5_ib_warn(dev, "GSI QP already exists on port %d\n",
			     port_num);
		ret = -EBUSY;
		goto err_free;
	}

	gsi->cap = init_attr->cap;
	gsi->sq_sig_type = init_attr->sq_sig_type;
	gsi->ibqp.qp_num = 1;
	gsi->port_num = port_num;

	hw_init_attr.qp_type = MLX5_IB_QPT_HW_GSI;
	gsi->rx_qp = ib_create_qp(pd, &hw_init_attr);
	if (IS_ERR(gsi->rx_qp)) {
		mlx5_ib_warn(dev, "unable to create hardware GSI QP. error %ld\n",
			     PTR_ERR(gsi->rx_qp));
		ret = PTR_ERR(gsi->rx_qp);
		goto err_free;
	}

	dev->devr.ports[init_attr->port_num - 1].gsi = gsi;

	mutex_unlock(&dev->devr.mutex);

	return &gsi->ibqp;

err_free:
	mutex_unlock(&dev->devr.mutex);
	kfree(gsi);
	return ERR_PTR(ret);
}

int mlx5_ib_gsi_destroy_qp(struct ib_qp *qp)
{
	struct mlx5_ib_dev *dev = to_mdev(qp->device);
	struct mlx5_ib_gsi_qp *gsi = gsi_qp(qp);
	const int port_num = gsi->port_num;
	int ret;

	mlx5_ib_dbg(dev, "destroying GSI QP\n");

	mutex_lock(&dev->devr.mutex);
	ret = ib_destroy_qp(gsi->rx_qp);
	if (ret) {
		mlx5_ib_warn(dev, "unable to destroy hardware GSI QP. error %d\n",
			     ret);
		mutex_unlock(&dev->devr.mutex);
		return ret;
	}
	dev->devr.ports[port_num - 1].gsi = NULL;
	mutex_unlock(&dev->devr.mutex);

	kfree(gsi);

	return 0;
}

int mlx5_ib_gsi_modify_qp(struct ib_qp *qp, struct ib_qp_attr *attr,
			  int attr_mask)
{
	struct mlx5_ib_dev *dev = to_mdev(qp->device);
	struct mlx5_ib_gsi_qp *gsi = gsi_qp(qp);
	int ret;

	mlx5_ib_dbg(dev, "modifying GSI QP to state %d\n", attr->qp_state);

	mutex_lock(&gsi->mutex);
	ret = ib_modify_qp(gsi->rx_qp, attr, attr_mask);
	mutex_unlock(&gsi->mutex);

	return ret;
}

int mlx5_ib_gsi_query_qp(struct ib_qp *qp, struct ib_qp_attr *qp_attr,
			 int qp_attr_mask,
			 struct ib_qp_init_attr *qp_init_attr)
{
	struct mlx5_ib_gsi_qp *gsi = gsi_qp(qp);
	int ret;

	mutex_lock(&gsi->mutex);
	ret = ib_query_qp(gsi->rx_qp, qp_attr, qp_attr_mask, qp_init_attr);
	qp_init_attr->cap = gsi->cap;
	mutex_unlock(&gsi->mutex);

	return ret;
}

int mlx5_ib_gsi_post_send(struct ib_qp *qp, struct ib_send_wr *wr,
			  struct ib_send_wr **bad_wr)
{
	struct mlx5_ib_gsi_qp *gsi = gsi_qp(qp);

	return ib_post_send(gsi->rx_qp, wr, bad_wr);
}

int mlx5_ib_gsi_post_recv(struct ib_qp *qp, struct ib_recv_wr *wr,
			  struct ib_recv_wr **bad_wr)
{
	struct mlx5_ib_gsi_qp *gsi = gsi_qp(qp);

	return ib_post_recv(gsi->rx_qp, wr, bad_wr);
}
