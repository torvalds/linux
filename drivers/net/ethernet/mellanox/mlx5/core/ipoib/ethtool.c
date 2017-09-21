/*
 * Copyright (c) 2017, Mellanox Technologies. All rights reserved.
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
#include "ipoib.h"

static void mlx5i_get_drvinfo(struct net_device *dev,
			      struct ethtool_drvinfo *drvinfo)
{
	struct mlx5e_priv *priv = mlx5i_epriv(dev);

	mlx5e_ethtool_get_drvinfo(priv, drvinfo);
}

static void mlx5i_get_strings(struct net_device *dev,
			      uint32_t stringset, uint8_t *data)
{
	struct mlx5e_priv *priv  = mlx5i_epriv(dev);

	mlx5e_ethtool_get_strings(priv, stringset, data);
}

static int mlx5i_get_sset_count(struct net_device *dev, int sset)
{
	struct mlx5e_priv *priv = mlx5i_epriv(dev);

	return mlx5e_ethtool_get_sset_count(priv, sset);
}

static void mlx5i_get_ethtool_stats(struct net_device *dev,
				    struct ethtool_stats *stats,
				    u64 *data)
{
	struct mlx5e_priv *priv = mlx5i_epriv(dev);

	mlx5e_ethtool_get_ethtool_stats(priv, stats, data);
}

static int mlx5i_set_ringparam(struct net_device *dev,
			       struct ethtool_ringparam *param)
{
	struct mlx5e_priv *priv = mlx5i_epriv(dev);

	return mlx5e_ethtool_set_ringparam(priv, param);
}

static void mlx5i_get_ringparam(struct net_device *dev,
				struct ethtool_ringparam *param)
{
	struct mlx5e_priv *priv = mlx5i_epriv(dev);

	mlx5e_ethtool_get_ringparam(priv, param);
}

static int mlx5i_set_channels(struct net_device *dev,
			      struct ethtool_channels *ch)
{
	struct mlx5e_priv *priv = mlx5i_epriv(dev);

	return mlx5e_ethtool_set_channels(priv, ch);
}

static void mlx5i_get_channels(struct net_device *dev,
			       struct ethtool_channels *ch)
{
	struct mlx5e_priv *priv = mlx5i_epriv(dev);

	mlx5e_ethtool_get_channels(priv, ch);
}

static int mlx5i_set_coalesce(struct net_device *netdev,
			      struct ethtool_coalesce *coal)
{
	struct mlx5e_priv *priv = mlx5i_epriv(netdev);

	return mlx5e_ethtool_set_coalesce(priv, coal);
}

static int mlx5i_get_coalesce(struct net_device *netdev,
			      struct ethtool_coalesce *coal)
{
	struct mlx5e_priv *priv = mlx5i_epriv(netdev);

	return mlx5e_ethtool_get_coalesce(priv, coal);
}

static int mlx5i_get_ts_info(struct net_device *netdev,
			     struct ethtool_ts_info *info)
{
	struct mlx5e_priv *priv = mlx5i_epriv(netdev);

	return mlx5e_ethtool_get_ts_info(priv, info);
}

static int mlx5i_flash_device(struct net_device *netdev,
			      struct ethtool_flash *flash)
{
	struct mlx5e_priv *priv = mlx5i_epriv(netdev);

	return mlx5e_ethtool_flash_device(priv, flash);
}

const struct ethtool_ops mlx5i_ethtool_ops = {
	.get_drvinfo       = mlx5i_get_drvinfo,
	.get_strings       = mlx5i_get_strings,
	.get_sset_count    = mlx5i_get_sset_count,
	.get_ethtool_stats = mlx5i_get_ethtool_stats,
	.get_ringparam     = mlx5i_get_ringparam,
	.set_ringparam     = mlx5i_set_ringparam,
	.flash_device      = mlx5i_flash_device,
	.get_channels      = mlx5i_get_channels,
	.set_channels      = mlx5i_set_channels,
	.get_coalesce      = mlx5i_get_coalesce,
	.set_coalesce      = mlx5i_set_coalesce,
	.get_ts_info       = mlx5i_get_ts_info,
};
