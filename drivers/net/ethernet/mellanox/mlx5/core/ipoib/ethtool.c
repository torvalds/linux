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
	strlcpy(drvinfo->driver, KBUILD_MODNAME "[ib_ipoib]",
		sizeof(drvinfo->driver));
}

static void mlx5i_get_strings(struct net_device *dev, u32 stringset, u8 *data)
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
			      struct ethtool_coalesce *coal,
			      struct kernel_ethtool_coalesce *kernel_coal,
			      struct netlink_ext_ack *extack)
{
	struct mlx5e_priv *priv = mlx5i_epriv(netdev);

	return mlx5e_ethtool_set_coalesce(priv, coal, kernel_coal, extack);
}

static int mlx5i_get_coalesce(struct net_device *netdev,
			      struct ethtool_coalesce *coal,
			      struct kernel_ethtool_coalesce *kernel_coal,
			      struct netlink_ext_ack *extack)
{
	struct mlx5e_priv *priv = mlx5i_epriv(netdev);

	return mlx5e_ethtool_get_coalesce(priv, coal, kernel_coal);
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

static inline int mlx5_ptys_width_enum_to_int(enum mlx5_ptys_width width)
{
	switch (width) {
	case MLX5_PTYS_WIDTH_1X:  return  1;
	case MLX5_PTYS_WIDTH_2X:  return  2;
	case MLX5_PTYS_WIDTH_4X:  return  4;
	case MLX5_PTYS_WIDTH_8X:  return  8;
	case MLX5_PTYS_WIDTH_12X: return 12;
	default:		  return -1;
	}
}

enum mlx5_ptys_rate {
	MLX5_PTYS_RATE_SDR	= 1 << 0,
	MLX5_PTYS_RATE_DDR	= 1 << 1,
	MLX5_PTYS_RATE_QDR	= 1 << 2,
	MLX5_PTYS_RATE_FDR10	= 1 << 3,
	MLX5_PTYS_RATE_FDR	= 1 << 4,
	MLX5_PTYS_RATE_EDR	= 1 << 5,
	MLX5_PTYS_RATE_HDR	= 1 << 6,
	MLX5_PTYS_RATE_NDR	= 1 << 7,
};

static inline int mlx5_ptys_rate_enum_to_int(enum mlx5_ptys_rate rate)
{
	switch (rate) {
	case MLX5_PTYS_RATE_SDR:   return 2500;
	case MLX5_PTYS_RATE_DDR:   return 5000;
	case MLX5_PTYS_RATE_QDR:
	case MLX5_PTYS_RATE_FDR10: return 10000;
	case MLX5_PTYS_RATE_FDR:   return 14000;
	case MLX5_PTYS_RATE_EDR:   return 25000;
	case MLX5_PTYS_RATE_HDR:   return 50000;
	case MLX5_PTYS_RATE_NDR:   return 100000;
	default:		   return -1;
	}
}

static int mlx5i_get_speed_settings(u16 ib_link_width_oper, u16 ib_proto_oper)
{
	int rate, width;

	rate = mlx5_ptys_rate_enum_to_int(ib_proto_oper);
	if (rate < 0)
		return -EINVAL;
	width = mlx5_ptys_width_enum_to_int(ib_link_width_oper);
	if (width < 0)
		return -EINVAL;

	return rate * width;
}

static int mlx5i_get_link_ksettings(struct net_device *netdev,
				    struct ethtool_link_ksettings *link_ksettings)
{
	struct mlx5e_priv *priv = mlx5i_epriv(netdev);
	struct mlx5_core_dev *mdev = priv->mdev;
	u16 ib_link_width_oper;
	u16 ib_proto_oper;
	int speed, ret;

	ret = mlx5_query_ib_port_oper(mdev, &ib_link_width_oper, &ib_proto_oper,
				      1);
	if (ret)
		return ret;

	ethtool_link_ksettings_zero_link_mode(link_ksettings, supported);
	ethtool_link_ksettings_zero_link_mode(link_ksettings, advertising);

	speed = mlx5i_get_speed_settings(ib_link_width_oper, ib_proto_oper);
	if (speed < 0)
		return -EINVAL;

	link_ksettings->base.duplex = DUPLEX_FULL;
	link_ksettings->base.port = PORT_OTHER;

	link_ksettings->base.autoneg = AUTONEG_DISABLE;

	link_ksettings->base.speed = speed;

	return 0;
}

#ifdef CONFIG_MLX5_EN_RXNFC
static u32 mlx5i_flow_type_mask(u32 flow_type)
{
	return flow_type & ~(FLOW_EXT | FLOW_MAC_EXT | FLOW_RSS);
}

static int mlx5i_set_rxnfc(struct net_device *dev, struct ethtool_rxnfc *cmd)
{
	struct mlx5e_priv *priv = mlx5i_epriv(dev);
	struct ethtool_rx_flow_spec *fs = &cmd->fs;

	if (mlx5i_flow_type_mask(fs->flow_type) == ETHER_FLOW)
		return -EINVAL;

	return mlx5e_ethtool_set_rxnfc(priv, cmd);
}

static int mlx5i_get_rxnfc(struct net_device *dev, struct ethtool_rxnfc *info,
			   u32 *rule_locs)
{
	struct mlx5e_priv *priv = mlx5i_epriv(dev);

	return mlx5e_ethtool_get_rxnfc(priv, info, rule_locs);
}
#endif

const struct ethtool_ops mlx5i_ethtool_ops = {
	.supported_coalesce_params = ETHTOOL_COALESCE_USECS |
				     ETHTOOL_COALESCE_MAX_FRAMES |
				     ETHTOOL_COALESCE_USE_ADAPTIVE,
	.get_drvinfo        = mlx5i_get_drvinfo,
	.get_strings        = mlx5i_get_strings,
	.get_sset_count     = mlx5i_get_sset_count,
	.get_ethtool_stats  = mlx5i_get_ethtool_stats,
	.get_ringparam      = mlx5i_get_ringparam,
	.set_ringparam      = mlx5i_set_ringparam,
	.flash_device       = mlx5i_flash_device,
	.get_channels       = mlx5i_get_channels,
	.set_channels       = mlx5i_set_channels,
	.get_coalesce       = mlx5i_get_coalesce,
	.set_coalesce       = mlx5i_set_coalesce,
	.get_ts_info        = mlx5i_get_ts_info,
#ifdef CONFIG_MLX5_EN_RXNFC
	.get_rxnfc          = mlx5i_get_rxnfc,
	.set_rxnfc          = mlx5i_set_rxnfc,
#endif
	.get_link_ksettings = mlx5i_get_link_ksettings,
	.get_link           = ethtool_op_get_link,
};

const struct ethtool_ops mlx5i_pkey_ethtool_ops = {
	.get_drvinfo        = mlx5i_get_drvinfo,
	.get_link           = ethtool_op_get_link,
	.get_ts_info        = mlx5i_get_ts_info,
};
