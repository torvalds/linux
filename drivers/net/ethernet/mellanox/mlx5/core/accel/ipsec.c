/*
 * Copyright (c) 2017 Mellanox Technologies. All rights reserved.
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

#include <linux/mlx5/device.h>

#include "accel/ipsec.h"
#include "mlx5_core.h"
#include "accel/ipsec_offload.h"

void mlx5_accel_ipsec_init(struct mlx5_core_dev *mdev)
{
	const struct mlx5_accel_ipsec_ops *ipsec_ops;
	int err = 0;

	ipsec_ops = mlx5_ipsec_offload_ops(mdev);
	if (!ipsec_ops || !ipsec_ops->init) {
		mlx5_core_dbg(mdev, "IPsec ops is not supported\n");
		return;
	}

	err = ipsec_ops->init(mdev);
	if (err) {
		mlx5_core_warn_once(mdev, "Failed to start IPsec device, err = %d\n", err);
		return;
	}

	mdev->ipsec_ops = ipsec_ops;
}

void mlx5_accel_ipsec_cleanup(struct mlx5_core_dev *mdev)
{
	const struct mlx5_accel_ipsec_ops *ipsec_ops = mdev->ipsec_ops;

	if (!ipsec_ops || !ipsec_ops->cleanup)
		return;

	ipsec_ops->cleanup(mdev);
}

u32 mlx5_accel_ipsec_device_caps(struct mlx5_core_dev *mdev)
{
	const struct mlx5_accel_ipsec_ops *ipsec_ops = mdev->ipsec_ops;

	if (!ipsec_ops || !ipsec_ops->device_caps)
		return 0;

	return ipsec_ops->device_caps(mdev);
}
EXPORT_SYMBOL_GPL(mlx5_accel_ipsec_device_caps);

unsigned int mlx5_accel_ipsec_counters_count(struct mlx5_core_dev *mdev)
{
	const struct mlx5_accel_ipsec_ops *ipsec_ops = mdev->ipsec_ops;

	if (!ipsec_ops || !ipsec_ops->counters_count)
		return -EOPNOTSUPP;

	return ipsec_ops->counters_count(mdev);
}

int mlx5_accel_ipsec_counters_read(struct mlx5_core_dev *mdev, u64 *counters,
				   unsigned int count)
{
	const struct mlx5_accel_ipsec_ops *ipsec_ops = mdev->ipsec_ops;

	if (!ipsec_ops || !ipsec_ops->counters_read)
		return -EOPNOTSUPP;

	return ipsec_ops->counters_read(mdev, counters, count);
}

void *mlx5_accel_esp_create_hw_context(struct mlx5_core_dev *mdev,
				       struct mlx5_accel_esp_xfrm *xfrm,
				       u32 *sa_handle)
{
	const struct mlx5_accel_ipsec_ops *ipsec_ops = mdev->ipsec_ops;
	__be32 saddr[4] = {}, daddr[4] = {};

	if (!ipsec_ops || !ipsec_ops->create_hw_context)
		return  ERR_PTR(-EOPNOTSUPP);

	if (!xfrm->attrs.is_ipv6) {
		saddr[3] = xfrm->attrs.saddr.a4;
		daddr[3] = xfrm->attrs.daddr.a4;
	} else {
		memcpy(saddr, xfrm->attrs.saddr.a6, sizeof(saddr));
		memcpy(daddr, xfrm->attrs.daddr.a6, sizeof(daddr));
	}

	return ipsec_ops->create_hw_context(mdev, xfrm, saddr, daddr, xfrm->attrs.spi,
					    xfrm->attrs.is_ipv6, sa_handle);
}

void mlx5_accel_esp_free_hw_context(struct mlx5_core_dev *mdev, void *context)
{
	const struct mlx5_accel_ipsec_ops *ipsec_ops = mdev->ipsec_ops;

	if (!ipsec_ops || !ipsec_ops->free_hw_context)
		return;

	ipsec_ops->free_hw_context(context);
}

struct mlx5_accel_esp_xfrm *
mlx5_accel_esp_create_xfrm(struct mlx5_core_dev *mdev,
			   const struct mlx5_accel_esp_xfrm_attrs *attrs,
			   u32 flags)
{
	const struct mlx5_accel_ipsec_ops *ipsec_ops = mdev->ipsec_ops;
	struct mlx5_accel_esp_xfrm *xfrm;

	if (!ipsec_ops || !ipsec_ops->esp_create_xfrm)
		return ERR_PTR(-EOPNOTSUPP);

	xfrm = ipsec_ops->esp_create_xfrm(mdev, attrs, flags);
	if (IS_ERR(xfrm))
		return xfrm;

	xfrm->mdev = mdev;
	return xfrm;
}
EXPORT_SYMBOL_GPL(mlx5_accel_esp_create_xfrm);

void mlx5_accel_esp_destroy_xfrm(struct mlx5_accel_esp_xfrm *xfrm)
{
	const struct mlx5_accel_ipsec_ops *ipsec_ops = xfrm->mdev->ipsec_ops;

	if (!ipsec_ops || !ipsec_ops->esp_destroy_xfrm)
		return;

	ipsec_ops->esp_destroy_xfrm(xfrm);
}
EXPORT_SYMBOL_GPL(mlx5_accel_esp_destroy_xfrm);

int mlx5_accel_esp_modify_xfrm(struct mlx5_accel_esp_xfrm *xfrm,
			       const struct mlx5_accel_esp_xfrm_attrs *attrs)
{
	const struct mlx5_accel_ipsec_ops *ipsec_ops = xfrm->mdev->ipsec_ops;

	if (!ipsec_ops || !ipsec_ops->esp_modify_xfrm)
		return -EOPNOTSUPP;

	return ipsec_ops->esp_modify_xfrm(xfrm, attrs);
}
EXPORT_SYMBOL_GPL(mlx5_accel_esp_modify_xfrm);
