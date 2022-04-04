/*
 * Copyright (c) 2018 Mellanox Technologies. All rights reserved.
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

#include <linux/netdevice.h>
#include <net/ipv6.h>
#include "en_accel/tls.h"

void mlx5e_tls_build_netdev(struct mlx5e_priv *priv)
{
	if (!mlx5e_accel_is_ktls_device(priv->mdev))
		return;

	mlx5e_ktls_build_netdev(priv);
}

int mlx5e_tls_init(struct mlx5e_priv *priv)
{
	struct mlx5e_tls *tls;

	if (!mlx5e_accel_is_tls_device(priv->mdev))
		return 0;

	tls = kzalloc(sizeof(*tls), GFP_KERNEL);
	if (!tls)
		return -ENOMEM;

	priv->tls = tls;
	return 0;
}

void mlx5e_tls_cleanup(struct mlx5e_priv *priv)
{
	struct mlx5e_tls *tls = priv->tls;

	if (!tls)
		return;

	kfree(tls);
	priv->tls = NULL;
}
