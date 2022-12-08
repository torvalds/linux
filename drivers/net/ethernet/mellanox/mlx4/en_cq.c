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

#include <linux/mlx4/cq.h>
#include <linux/mlx4/qp.h>
#include <linux/mlx4/cmd.h>

#include "mlx4_en.h"

static void mlx4_en_cq_event(struct mlx4_cq *cq, enum mlx4_event event)
{
	return;
}


int mlx4_en_create_cq(struct mlx4_en_priv *priv,
		      struct mlx4_en_cq **pcq,
		      int entries, int ring, enum cq_type mode,
		      int node)
{
	struct mlx4_en_dev *mdev = priv->mdev;
	struct mlx4_en_cq *cq;
	int err;

	cq = kzalloc_node(sizeof(*cq), GFP_KERNEL, node);
	if (!cq) {
		en_err(priv, "Failed to allocate CQ structure\n");
		return -ENOMEM;
	}

	cq->size = entries;
	cq->buf_size = cq->size * mdev->dev->caps.cqe_size;

	cq->ring = ring;
	cq->type = mode;
	cq->vector = mdev->dev->caps.num_comp_vectors;

	/* Allocate HW buffers on provided NUMA node.
	 * dev->numa_node is used in mtt range allocation flow.
	 */
	set_dev_node(&mdev->dev->persist->pdev->dev, node);
	err = mlx4_alloc_hwq_res(mdev->dev, &cq->wqres,
				cq->buf_size);
	set_dev_node(&mdev->dev->persist->pdev->dev, mdev->dev->numa_node);
	if (err)
		goto err_cq;

	cq->buf = (struct mlx4_cqe *)cq->wqres.buf.direct.buf;
	*pcq = cq;

	return 0;

err_cq:
	kfree(cq);
	*pcq = NULL;
	return err;
}

int mlx4_en_activate_cq(struct mlx4_en_priv *priv, struct mlx4_en_cq *cq,
			int cq_idx)
{
	struct mlx4_en_dev *mdev = priv->mdev;
	int irq, err = 0;
	int timestamp_en = 0;
	bool assigned_eq = false;

	cq->dev = mdev->pndev[priv->port];
	cq->mcq.set_ci_db  = cq->wqres.db.db;
	cq->mcq.arm_db     = cq->wqres.db.db + 1;
	*cq->mcq.set_ci_db = 0;
	*cq->mcq.arm_db    = 0;
	memset(cq->buf, 0, cq->buf_size);

	if (cq->type == RX) {
		if (!mlx4_is_eq_vector_valid(mdev->dev, priv->port,
					     cq->vector)) {
			cq->vector = cpumask_first(priv->rx_ring[cq->ring]->affinity_mask);

			err = mlx4_assign_eq(mdev->dev, priv->port,
					     &cq->vector);
			if (err) {
				mlx4_err(mdev, "Failed assigning an EQ to CQ vector %d\n",
					 cq->vector);
				goto free_eq;
			}

			assigned_eq = true;
		}
		irq = mlx4_eq_get_irq(mdev->dev, cq->vector);
		cq->aff_mask = irq_get_effective_affinity_mask(irq);
	} else {
		/* For TX we use the same irq per
		ring we assigned for the RX    */
		struct mlx4_en_cq *rx_cq;

		cq_idx = cq_idx % priv->rx_ring_num;
		rx_cq = priv->rx_cq[cq_idx];
		cq->vector = rx_cq->vector;
	}

	if (cq->type == RX)
		cq->size = priv->rx_ring[cq->ring]->actual_size;

	if ((cq->type != RX && priv->hwtstamp_config.tx_type) ||
	    (cq->type == RX && priv->hwtstamp_config.rx_filter))
		timestamp_en = 1;

	cq->mcq.usage = MLX4_RES_USAGE_DRIVER;
	err = mlx4_cq_alloc(mdev->dev, cq->size, &cq->wqres.mtt,
			    &mdev->priv_uar, cq->wqres.db.dma, &cq->mcq,
			    cq->vector, 0, timestamp_en, &cq->wqres.buf, false);
	if (err)
		goto free_eq;

	cq->mcq.event = mlx4_en_cq_event;

	switch (cq->type) {
	case TX:
		cq->mcq.comp = mlx4_en_tx_irq;
		netif_napi_add_tx(cq->dev, &cq->napi, mlx4_en_poll_tx_cq);
		napi_enable(&cq->napi);
		break;
	case RX:
		cq->mcq.comp = mlx4_en_rx_irq;
		netif_napi_add(cq->dev, &cq->napi, mlx4_en_poll_rx_cq);
		napi_enable(&cq->napi);
		break;
	case TX_XDP:
		/* nothing regarding napi, it's shared with rx ring */
		cq->xdp_busy = false;
		break;
	}

	return 0;

free_eq:
	if (assigned_eq)
		mlx4_release_eq(mdev->dev, cq->vector);
	cq->vector = mdev->dev->caps.num_comp_vectors;
	return err;
}

void mlx4_en_destroy_cq(struct mlx4_en_priv *priv, struct mlx4_en_cq **pcq)
{
	struct mlx4_en_dev *mdev = priv->mdev;
	struct mlx4_en_cq *cq = *pcq;

	mlx4_free_hwq_res(mdev->dev, &cq->wqres, cq->buf_size);
	if (mlx4_is_eq_vector_valid(mdev->dev, priv->port, cq->vector) &&
	    cq->type == RX)
		mlx4_release_eq(priv->mdev->dev, cq->vector);
	cq->vector = 0;
	cq->buf_size = 0;
	cq->buf = NULL;
	kfree(cq);
	*pcq = NULL;
}

void mlx4_en_deactivate_cq(struct mlx4_en_priv *priv, struct mlx4_en_cq *cq)
{
	if (cq->type != TX_XDP) {
		napi_disable(&cq->napi);
		netif_napi_del(&cq->napi);
	}

	mlx4_cq_free(priv->mdev->dev, &cq->mcq);
}

/* Set rx cq moderation parameters */
int mlx4_en_set_cq_moder(struct mlx4_en_priv *priv, struct mlx4_en_cq *cq)
{
	return mlx4_cq_modify(priv->mdev->dev, &cq->mcq,
			      cq->moder_cnt, cq->moder_time);
}

void mlx4_en_arm_cq(struct mlx4_en_priv *priv, struct mlx4_en_cq *cq)
{
	mlx4_cq_arm(&cq->mcq, MLX4_CQ_DB_REQ_NOT, priv->mdev->uar_map,
		    &priv->mdev->uar_lock);
}


