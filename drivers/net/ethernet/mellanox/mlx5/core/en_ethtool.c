/*
 * Copyright (c) 2015, Mellanox Technologies. All rights reserved.
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

#include <linux/ethtool_netlink.h>

#include "en.h"
#include "en/port.h"
#include "en/params.h"
#include "en/ptp.h"
#include "lib/clock.h"
#include "en/fs_ethtool.h"

void mlx5e_ethtool_get_drvinfo(struct mlx5e_priv *priv,
			       struct ethtool_drvinfo *drvinfo)
{
	struct mlx5_core_dev *mdev = priv->mdev;
	int count;

	strscpy(drvinfo->driver, KBUILD_MODNAME, sizeof(drvinfo->driver));
	count = snprintf(drvinfo->fw_version, sizeof(drvinfo->fw_version),
			 "%d.%d.%04d (%.16s)", fw_rev_maj(mdev),
			 fw_rev_min(mdev), fw_rev_sub(mdev), mdev->board_id);
	if (count == sizeof(drvinfo->fw_version))
		snprintf(drvinfo->fw_version, sizeof(drvinfo->fw_version),
			 "%d.%d.%04d", fw_rev_maj(mdev),
			 fw_rev_min(mdev), fw_rev_sub(mdev));

	strscpy(drvinfo->bus_info, dev_name(mdev->device),
		sizeof(drvinfo->bus_info));
}

static void mlx5e_get_drvinfo(struct net_device *dev,
			      struct ethtool_drvinfo *drvinfo)
{
	struct mlx5e_priv *priv = netdev_priv(dev);

	mlx5e_ethtool_get_drvinfo(priv, drvinfo);
}

struct ptys2ethtool_config {
	__ETHTOOL_DECLARE_LINK_MODE_MASK(supported);
	__ETHTOOL_DECLARE_LINK_MODE_MASK(advertised);
};

static
struct ptys2ethtool_config ptys2legacy_ethtool_table[MLX5E_LINK_MODES_NUMBER];
static
struct ptys2ethtool_config ptys2ext_ethtool_table[MLX5E_EXT_LINK_MODES_NUMBER];

#define MLX5_BUILD_PTYS2ETHTOOL_CONFIG(reg_, table, ...)                  \
	({                                                              \
		struct ptys2ethtool_config *cfg;                        \
		const unsigned int modes[] = { __VA_ARGS__ };           \
		unsigned int i, bit, idx;                               \
		cfg = &ptys2##table##_ethtool_table[reg_];		\
		bitmap_zero(cfg->supported,                             \
			    __ETHTOOL_LINK_MODE_MASK_NBITS);            \
		bitmap_zero(cfg->advertised,                            \
			    __ETHTOOL_LINK_MODE_MASK_NBITS);            \
		for (i = 0 ; i < ARRAY_SIZE(modes) ; ++i) {             \
			bit = modes[i] % 64;                            \
			idx = modes[i] / 64;                            \
			__set_bit(bit, &cfg->supported[idx]);           \
			__set_bit(bit, &cfg->advertised[idx]);          \
		}                                                       \
	})

void mlx5e_build_ptys2ethtool_map(void)
{
	memset(ptys2legacy_ethtool_table, 0, sizeof(ptys2legacy_ethtool_table));
	memset(ptys2ext_ethtool_table, 0, sizeof(ptys2ext_ethtool_table));
	MLX5_BUILD_PTYS2ETHTOOL_CONFIG(MLX5E_1000BASE_CX_SGMII, legacy,
				       ETHTOOL_LINK_MODE_1000baseKX_Full_BIT);
	MLX5_BUILD_PTYS2ETHTOOL_CONFIG(MLX5E_1000BASE_KX, legacy,
				       ETHTOOL_LINK_MODE_1000baseKX_Full_BIT);
	MLX5_BUILD_PTYS2ETHTOOL_CONFIG(MLX5E_10GBASE_CX4, legacy,
				       ETHTOOL_LINK_MODE_10000baseKX4_Full_BIT);
	MLX5_BUILD_PTYS2ETHTOOL_CONFIG(MLX5E_10GBASE_KX4, legacy,
				       ETHTOOL_LINK_MODE_10000baseKX4_Full_BIT);
	MLX5_BUILD_PTYS2ETHTOOL_CONFIG(MLX5E_10GBASE_KR, legacy,
				       ETHTOOL_LINK_MODE_10000baseKR_Full_BIT);
	MLX5_BUILD_PTYS2ETHTOOL_CONFIG(MLX5E_20GBASE_KR2, legacy,
				       ETHTOOL_LINK_MODE_20000baseKR2_Full_BIT);
	MLX5_BUILD_PTYS2ETHTOOL_CONFIG(MLX5E_40GBASE_CR4, legacy,
				       ETHTOOL_LINK_MODE_40000baseCR4_Full_BIT);
	MLX5_BUILD_PTYS2ETHTOOL_CONFIG(MLX5E_40GBASE_KR4, legacy,
				       ETHTOOL_LINK_MODE_40000baseKR4_Full_BIT);
	MLX5_BUILD_PTYS2ETHTOOL_CONFIG(MLX5E_56GBASE_R4, legacy,
				       ETHTOOL_LINK_MODE_56000baseKR4_Full_BIT);
	MLX5_BUILD_PTYS2ETHTOOL_CONFIG(MLX5E_10GBASE_CR, legacy,
				       ETHTOOL_LINK_MODE_10000baseKR_Full_BIT);
	MLX5_BUILD_PTYS2ETHTOOL_CONFIG(MLX5E_10GBASE_SR, legacy,
				       ETHTOOL_LINK_MODE_10000baseKR_Full_BIT);
	MLX5_BUILD_PTYS2ETHTOOL_CONFIG(MLX5E_10GBASE_ER, legacy,
				       ETHTOOL_LINK_MODE_10000baseKR_Full_BIT);
	MLX5_BUILD_PTYS2ETHTOOL_CONFIG(MLX5E_40GBASE_SR4, legacy,
				       ETHTOOL_LINK_MODE_40000baseSR4_Full_BIT);
	MLX5_BUILD_PTYS2ETHTOOL_CONFIG(MLX5E_40GBASE_LR4, legacy,
				       ETHTOOL_LINK_MODE_40000baseLR4_Full_BIT);
	MLX5_BUILD_PTYS2ETHTOOL_CONFIG(MLX5E_50GBASE_SR2, legacy,
				       ETHTOOL_LINK_MODE_50000baseSR2_Full_BIT);
	MLX5_BUILD_PTYS2ETHTOOL_CONFIG(MLX5E_100GBASE_CR4, legacy,
				       ETHTOOL_LINK_MODE_100000baseCR4_Full_BIT);
	MLX5_BUILD_PTYS2ETHTOOL_CONFIG(MLX5E_100GBASE_SR4, legacy,
				       ETHTOOL_LINK_MODE_100000baseSR4_Full_BIT);
	MLX5_BUILD_PTYS2ETHTOOL_CONFIG(MLX5E_100GBASE_KR4, legacy,
				       ETHTOOL_LINK_MODE_100000baseKR4_Full_BIT);
	MLX5_BUILD_PTYS2ETHTOOL_CONFIG(MLX5E_100GBASE_LR4, legacy,
				       ETHTOOL_LINK_MODE_100000baseLR4_ER4_Full_BIT);
	MLX5_BUILD_PTYS2ETHTOOL_CONFIG(MLX5E_10GBASE_T, legacy,
				       ETHTOOL_LINK_MODE_10000baseT_Full_BIT);
	MLX5_BUILD_PTYS2ETHTOOL_CONFIG(MLX5E_25GBASE_CR, legacy,
				       ETHTOOL_LINK_MODE_25000baseCR_Full_BIT);
	MLX5_BUILD_PTYS2ETHTOOL_CONFIG(MLX5E_25GBASE_KR, legacy,
				       ETHTOOL_LINK_MODE_25000baseKR_Full_BIT);
	MLX5_BUILD_PTYS2ETHTOOL_CONFIG(MLX5E_25GBASE_SR, legacy,
				       ETHTOOL_LINK_MODE_25000baseSR_Full_BIT);
	MLX5_BUILD_PTYS2ETHTOOL_CONFIG(MLX5E_50GBASE_CR2, legacy,
				       ETHTOOL_LINK_MODE_50000baseCR2_Full_BIT);
	MLX5_BUILD_PTYS2ETHTOOL_CONFIG(MLX5E_50GBASE_KR2, legacy,
				       ETHTOOL_LINK_MODE_50000baseKR2_Full_BIT);
	MLX5_BUILD_PTYS2ETHTOOL_CONFIG(MLX5E_SGMII_100M, ext,
				       ETHTOOL_LINK_MODE_100baseT_Full_BIT);
	MLX5_BUILD_PTYS2ETHTOOL_CONFIG(MLX5E_1000BASE_X_SGMII, ext,
				       ETHTOOL_LINK_MODE_1000baseT_Full_BIT,
				       ETHTOOL_LINK_MODE_1000baseKX_Full_BIT,
				       ETHTOOL_LINK_MODE_1000baseX_Full_BIT);
	MLX5_BUILD_PTYS2ETHTOOL_CONFIG(MLX5E_5GBASE_R, ext,
				       ETHTOOL_LINK_MODE_5000baseT_Full_BIT);
	MLX5_BUILD_PTYS2ETHTOOL_CONFIG(MLX5E_10GBASE_XFI_XAUI_1, ext,
				       ETHTOOL_LINK_MODE_10000baseT_Full_BIT,
				       ETHTOOL_LINK_MODE_10000baseKR_Full_BIT,
				       ETHTOOL_LINK_MODE_10000baseR_FEC_BIT,
				       ETHTOOL_LINK_MODE_10000baseCR_Full_BIT,
				       ETHTOOL_LINK_MODE_10000baseSR_Full_BIT,
				       ETHTOOL_LINK_MODE_10000baseLR_Full_BIT,
				       ETHTOOL_LINK_MODE_10000baseER_Full_BIT);
	MLX5_BUILD_PTYS2ETHTOOL_CONFIG(MLX5E_40GBASE_XLAUI_4_XLPPI_4, ext,
				       ETHTOOL_LINK_MODE_40000baseKR4_Full_BIT,
				       ETHTOOL_LINK_MODE_40000baseCR4_Full_BIT,
				       ETHTOOL_LINK_MODE_40000baseSR4_Full_BIT,
				       ETHTOOL_LINK_MODE_40000baseLR4_Full_BIT);
	MLX5_BUILD_PTYS2ETHTOOL_CONFIG(MLX5E_25GAUI_1_25GBASE_CR_KR, ext,
				       ETHTOOL_LINK_MODE_25000baseCR_Full_BIT,
				       ETHTOOL_LINK_MODE_25000baseKR_Full_BIT,
				       ETHTOOL_LINK_MODE_25000baseSR_Full_BIT);
	MLX5_BUILD_PTYS2ETHTOOL_CONFIG(MLX5E_50GAUI_2_LAUI_2_50GBASE_CR2_KR2,
				       ext,
				       ETHTOOL_LINK_MODE_50000baseCR2_Full_BIT,
				       ETHTOOL_LINK_MODE_50000baseKR2_Full_BIT,
				       ETHTOOL_LINK_MODE_50000baseSR2_Full_BIT);
	MLX5_BUILD_PTYS2ETHTOOL_CONFIG(MLX5E_50GAUI_1_LAUI_1_50GBASE_CR_KR, ext,
				       ETHTOOL_LINK_MODE_50000baseKR_Full_BIT,
				       ETHTOOL_LINK_MODE_50000baseSR_Full_BIT,
				       ETHTOOL_LINK_MODE_50000baseCR_Full_BIT,
				       ETHTOOL_LINK_MODE_50000baseLR_ER_FR_Full_BIT,
				       ETHTOOL_LINK_MODE_50000baseDR_Full_BIT);
	MLX5_BUILD_PTYS2ETHTOOL_CONFIG(MLX5E_CAUI_4_100GBASE_CR4_KR4, ext,
				       ETHTOOL_LINK_MODE_100000baseKR4_Full_BIT,
				       ETHTOOL_LINK_MODE_100000baseSR4_Full_BIT,
				       ETHTOOL_LINK_MODE_100000baseCR4_Full_BIT,
				       ETHTOOL_LINK_MODE_100000baseLR4_ER4_Full_BIT);
	MLX5_BUILD_PTYS2ETHTOOL_CONFIG(MLX5E_100GAUI_2_100GBASE_CR2_KR2, ext,
				       ETHTOOL_LINK_MODE_100000baseKR2_Full_BIT,
				       ETHTOOL_LINK_MODE_100000baseSR2_Full_BIT,
				       ETHTOOL_LINK_MODE_100000baseCR2_Full_BIT,
				       ETHTOOL_LINK_MODE_100000baseLR2_ER2_FR2_Full_BIT,
				       ETHTOOL_LINK_MODE_100000baseDR2_Full_BIT);
	MLX5_BUILD_PTYS2ETHTOOL_CONFIG(MLX5E_200GAUI_4_200GBASE_CR4_KR4, ext,
				       ETHTOOL_LINK_MODE_200000baseKR4_Full_BIT,
				       ETHTOOL_LINK_MODE_200000baseSR4_Full_BIT,
				       ETHTOOL_LINK_MODE_200000baseLR4_ER4_FR4_Full_BIT,
				       ETHTOOL_LINK_MODE_200000baseDR4_Full_BIT,
				       ETHTOOL_LINK_MODE_200000baseCR4_Full_BIT);
	MLX5_BUILD_PTYS2ETHTOOL_CONFIG(MLX5E_100GAUI_1_100GBASE_CR_KR, ext,
				       ETHTOOL_LINK_MODE_100000baseKR_Full_BIT,
				       ETHTOOL_LINK_MODE_100000baseSR_Full_BIT,
				       ETHTOOL_LINK_MODE_100000baseLR_ER_FR_Full_BIT,
				       ETHTOOL_LINK_MODE_100000baseDR_Full_BIT,
				       ETHTOOL_LINK_MODE_100000baseCR_Full_BIT);
	MLX5_BUILD_PTYS2ETHTOOL_CONFIG(MLX5E_200GAUI_2_200GBASE_CR2_KR2, ext,
				       ETHTOOL_LINK_MODE_200000baseKR2_Full_BIT,
				       ETHTOOL_LINK_MODE_200000baseSR2_Full_BIT,
				       ETHTOOL_LINK_MODE_200000baseLR2_ER2_FR2_Full_BIT,
				       ETHTOOL_LINK_MODE_200000baseDR2_Full_BIT,
				       ETHTOOL_LINK_MODE_200000baseCR2_Full_BIT);
	MLX5_BUILD_PTYS2ETHTOOL_CONFIG(MLX5E_400GAUI_4_400GBASE_CR4_KR4, ext,
				       ETHTOOL_LINK_MODE_400000baseKR4_Full_BIT,
				       ETHTOOL_LINK_MODE_400000baseSR4_Full_BIT,
				       ETHTOOL_LINK_MODE_400000baseLR4_ER4_FR4_Full_BIT,
				       ETHTOOL_LINK_MODE_400000baseDR4_Full_BIT,
				       ETHTOOL_LINK_MODE_400000baseCR4_Full_BIT);
}

static void mlx5e_ethtool_get_speed_arr(struct mlx5_core_dev *mdev,
					struct ptys2ethtool_config **arr,
					u32 *size)
{
	bool ext = mlx5_ptys_ext_supported(mdev);

	*arr = ext ? ptys2ext_ethtool_table : ptys2legacy_ethtool_table;
	*size = ext ? ARRAY_SIZE(ptys2ext_ethtool_table) :
		      ARRAY_SIZE(ptys2legacy_ethtool_table);
}

typedef int (*mlx5e_pflag_handler)(struct net_device *netdev, bool enable);

struct pflag_desc {
	char name[ETH_GSTRING_LEN];
	mlx5e_pflag_handler handler;
};

static const struct pflag_desc mlx5e_priv_flags[MLX5E_NUM_PFLAGS];

int mlx5e_ethtool_get_sset_count(struct mlx5e_priv *priv, int sset)
{
	switch (sset) {
	case ETH_SS_STATS:
		return mlx5e_stats_total_num(priv);
	case ETH_SS_PRIV_FLAGS:
		return MLX5E_NUM_PFLAGS;
	case ETH_SS_TEST:
		return mlx5e_self_test_num(priv);
	default:
		return -EOPNOTSUPP;
	}
}

static int mlx5e_get_sset_count(struct net_device *dev, int sset)
{
	struct mlx5e_priv *priv = netdev_priv(dev);

	return mlx5e_ethtool_get_sset_count(priv, sset);
}

void mlx5e_ethtool_get_strings(struct mlx5e_priv *priv, u32 stringset, u8 *data)
{
	int i;

	switch (stringset) {
	case ETH_SS_PRIV_FLAGS:
		for (i = 0; i < MLX5E_NUM_PFLAGS; i++)
			strcpy(data + i * ETH_GSTRING_LEN,
			       mlx5e_priv_flags[i].name);
		break;

	case ETH_SS_TEST:
		mlx5e_self_test_fill_strings(priv, data);
		break;

	case ETH_SS_STATS:
		mlx5e_stats_fill_strings(priv, data);
		break;
	}
}

static void mlx5e_get_strings(struct net_device *dev, u32 stringset, u8 *data)
{
	struct mlx5e_priv *priv = netdev_priv(dev);

	mlx5e_ethtool_get_strings(priv, stringset, data);
}

void mlx5e_ethtool_get_ethtool_stats(struct mlx5e_priv *priv,
				     struct ethtool_stats *stats, u64 *data)
{
	int idx = 0;

	mutex_lock(&priv->state_lock);
	mlx5e_stats_update(priv);
	mutex_unlock(&priv->state_lock);

	mlx5e_stats_fill(priv, data, idx);
}

static void mlx5e_get_ethtool_stats(struct net_device *dev,
				    struct ethtool_stats *stats,
				    u64 *data)
{
	struct mlx5e_priv *priv = netdev_priv(dev);

	mlx5e_ethtool_get_ethtool_stats(priv, stats, data);
}

void mlx5e_ethtool_get_ringparam(struct mlx5e_priv *priv,
				 struct ethtool_ringparam *param,
				 struct kernel_ethtool_ringparam *kernel_param)
{
	/* Limitation for regular RQ. XSK RQ may clamp the queue length in
	 * mlx5e_mpwqe_get_log_rq_size.
	 */
	u8 max_log_mpwrq_pkts = mlx5e_mpwrq_max_log_rq_pkts(priv->mdev,
							    PAGE_SHIFT,
							    MLX5E_MPWRQ_UMR_MODE_ALIGNED);

	param->rx_max_pending = 1 << min_t(u8, MLX5E_PARAMS_MAXIMUM_LOG_RQ_SIZE,
					   max_log_mpwrq_pkts);
	param->tx_max_pending = 1 << MLX5E_PARAMS_MAXIMUM_LOG_SQ_SIZE;
	param->rx_pending     = 1 << priv->channels.params.log_rq_mtu_frames;
	param->tx_pending     = 1 << priv->channels.params.log_sq_size;

	kernel_param->tcp_data_split =
		(priv->channels.params.packet_merge.type == MLX5E_PACKET_MERGE_SHAMPO) ?
		ETHTOOL_TCP_DATA_SPLIT_ENABLED :
		ETHTOOL_TCP_DATA_SPLIT_DISABLED;
}

static void mlx5e_get_ringparam(struct net_device *dev,
				struct ethtool_ringparam *param,
				struct kernel_ethtool_ringparam *kernel_param,
				struct netlink_ext_ack *extack)
{
	struct mlx5e_priv *priv = netdev_priv(dev);

	mlx5e_ethtool_get_ringparam(priv, param, kernel_param);
}

int mlx5e_ethtool_set_ringparam(struct mlx5e_priv *priv,
				struct ethtool_ringparam *param)
{
	struct mlx5e_params new_params;
	u8 log_rq_size;
	u8 log_sq_size;
	int err = 0;

	if (param->rx_jumbo_pending) {
		netdev_info(priv->netdev, "%s: rx_jumbo_pending not supported\n",
			    __func__);
		return -EINVAL;
	}
	if (param->rx_mini_pending) {
		netdev_info(priv->netdev, "%s: rx_mini_pending not supported\n",
			    __func__);
		return -EINVAL;
	}

	if (param->rx_pending < (1 << MLX5E_PARAMS_MINIMUM_LOG_RQ_SIZE)) {
		netdev_info(priv->netdev, "%s: rx_pending (%d) < min (%d)\n",
			    __func__, param->rx_pending,
			    1 << MLX5E_PARAMS_MINIMUM_LOG_RQ_SIZE);
		return -EINVAL;
	}

	if (param->tx_pending < (1 << MLX5E_PARAMS_MINIMUM_LOG_SQ_SIZE)) {
		netdev_info(priv->netdev, "%s: tx_pending (%d) < min (%d)\n",
			    __func__, param->tx_pending,
			    1 << MLX5E_PARAMS_MINIMUM_LOG_SQ_SIZE);
		return -EINVAL;
	}

	log_rq_size = order_base_2(param->rx_pending);
	log_sq_size = order_base_2(param->tx_pending);

	if (log_rq_size == priv->channels.params.log_rq_mtu_frames &&
	    log_sq_size == priv->channels.params.log_sq_size)
		return 0;

	mutex_lock(&priv->state_lock);

	new_params = priv->channels.params;
	new_params.log_rq_mtu_frames = log_rq_size;
	new_params.log_sq_size = log_sq_size;

	err = mlx5e_validate_params(priv->mdev, &new_params);
	if (err)
		goto unlock;

	err = mlx5e_safe_switch_params(priv, &new_params, NULL, NULL, true);

unlock:
	mutex_unlock(&priv->state_lock);

	return err;
}

static int mlx5e_set_ringparam(struct net_device *dev,
			       struct ethtool_ringparam *param,
			       struct kernel_ethtool_ringparam *kernel_param,
			       struct netlink_ext_ack *extack)
{
	struct mlx5e_priv *priv = netdev_priv(dev);

	return mlx5e_ethtool_set_ringparam(priv, param);
}

void mlx5e_ethtool_get_channels(struct mlx5e_priv *priv,
				struct ethtool_channels *ch)
{
	mutex_lock(&priv->state_lock);
	ch->max_combined   = priv->max_nch;
	ch->combined_count = priv->channels.params.num_channels;
	mutex_unlock(&priv->state_lock);
}

static void mlx5e_get_channels(struct net_device *dev,
			       struct ethtool_channels *ch)
{
	struct mlx5e_priv *priv = netdev_priv(dev);

	mlx5e_ethtool_get_channels(priv, ch);
}

int mlx5e_ethtool_set_channels(struct mlx5e_priv *priv,
			       struct ethtool_channels *ch)
{
	struct mlx5e_params *cur_params = &priv->channels.params;
	unsigned int count = ch->combined_count;
	struct mlx5e_params new_params;
	bool arfs_enabled;
	int rss_cnt;
	bool opened;
	int err = 0;

	if (!count) {
		netdev_info(priv->netdev, "%s: combined_count=0 not supported\n",
			    __func__);
		return -EINVAL;
	}

	if (cur_params->num_channels == count)
		return 0;

	mutex_lock(&priv->state_lock);

	/* Don't allow changing the number of channels if HTB offload is active,
	 * because the numeration of the QoS SQs will change, while per-queue
	 * qdiscs are attached.
	 */
	if (mlx5e_selq_is_htb_enabled(&priv->selq)) {
		err = -EINVAL;
		netdev_err(priv->netdev, "%s: HTB offload is active, cannot change the number of channels\n",
			   __func__);
		goto out;
	}

	/* Don't allow changing the number of channels if non-default RSS contexts exist,
	 * the kernel doesn't protect against set_channels operations that break them.
	 */
	rss_cnt = mlx5e_rx_res_rss_cnt(priv->rx_res) - 1;
	if (rss_cnt) {
		err = -EINVAL;
		netdev_err(priv->netdev, "%s: Non-default RSS contexts exist (%d), cannot change the number of channels\n",
			   __func__, rss_cnt);
		goto out;
	}

	/* Don't allow changing the number of channels if MQPRIO mode channel offload is active,
	 * because it defines a partition over the channels queues.
	 */
	if (cur_params->mqprio.mode == TC_MQPRIO_MODE_CHANNEL) {
		err = -EINVAL;
		netdev_err(priv->netdev, "%s: MQPRIO mode channel offload is active, cannot change the number of channels\n",
			   __func__);
		goto out;
	}

	new_params = *cur_params;
	new_params.num_channels = count;

	opened = test_bit(MLX5E_STATE_OPENED, &priv->state);

	arfs_enabled = opened && (priv->netdev->features & NETIF_F_NTUPLE);
	if (arfs_enabled)
		mlx5e_arfs_disable(priv->fs);

	/* Switch to new channels, set new parameters and close old ones */
	err = mlx5e_safe_switch_params(priv, &new_params,
				       mlx5e_num_channels_changed_ctx, NULL, true);

	if (arfs_enabled) {
		int err2 = mlx5e_arfs_enable(priv->fs);

		if (err2)
			netdev_err(priv->netdev, "%s: mlx5e_arfs_enable failed: %d\n",
				   __func__, err2);
	}

out:
	mutex_unlock(&priv->state_lock);

	return err;
}

static int mlx5e_set_channels(struct net_device *dev,
			      struct ethtool_channels *ch)
{
	struct mlx5e_priv *priv = netdev_priv(dev);

	return mlx5e_ethtool_set_channels(priv, ch);
}

int mlx5e_ethtool_get_coalesce(struct mlx5e_priv *priv,
			       struct ethtool_coalesce *coal,
			       struct kernel_ethtool_coalesce *kernel_coal)
{
	struct dim_cq_moder *rx_moder, *tx_moder;

	if (!MLX5_CAP_GEN(priv->mdev, cq_moderation))
		return -EOPNOTSUPP;

	rx_moder = &priv->channels.params.rx_cq_moderation;
	coal->rx_coalesce_usecs		= rx_moder->usec;
	coal->rx_max_coalesced_frames	= rx_moder->pkts;
	coal->use_adaptive_rx_coalesce	= priv->channels.params.rx_dim_enabled;

	tx_moder = &priv->channels.params.tx_cq_moderation;
	coal->tx_coalesce_usecs		= tx_moder->usec;
	coal->tx_max_coalesced_frames	= tx_moder->pkts;
	coal->use_adaptive_tx_coalesce	= priv->channels.params.tx_dim_enabled;

	kernel_coal->use_cqe_mode_rx =
		MLX5E_GET_PFLAG(&priv->channels.params, MLX5E_PFLAG_RX_CQE_BASED_MODER);
	kernel_coal->use_cqe_mode_tx =
		MLX5E_GET_PFLAG(&priv->channels.params, MLX5E_PFLAG_TX_CQE_BASED_MODER);

	return 0;
}

static int mlx5e_get_coalesce(struct net_device *netdev,
			      struct ethtool_coalesce *coal,
			      struct kernel_ethtool_coalesce *kernel_coal,
			      struct netlink_ext_ack *extack)
{
	struct mlx5e_priv *priv = netdev_priv(netdev);

	return mlx5e_ethtool_get_coalesce(priv, coal, kernel_coal);
}

#define MLX5E_MAX_COAL_TIME		MLX5_MAX_CQ_PERIOD
#define MLX5E_MAX_COAL_FRAMES		MLX5_MAX_CQ_COUNT

static void
mlx5e_set_priv_channels_tx_coalesce(struct mlx5e_priv *priv, struct ethtool_coalesce *coal)
{
	struct mlx5_core_dev *mdev = priv->mdev;
	int tc;
	int i;

	for (i = 0; i < priv->channels.num; ++i) {
		struct mlx5e_channel *c = priv->channels.c[i];

		for (tc = 0; tc < c->num_tc; tc++) {
			mlx5_core_modify_cq_moderation(mdev,
						&c->sq[tc].cq.mcq,
						coal->tx_coalesce_usecs,
						coal->tx_max_coalesced_frames);
		}
	}
}

static void
mlx5e_set_priv_channels_rx_coalesce(struct mlx5e_priv *priv, struct ethtool_coalesce *coal)
{
	struct mlx5_core_dev *mdev = priv->mdev;
	int i;

	for (i = 0; i < priv->channels.num; ++i) {
		struct mlx5e_channel *c = priv->channels.c[i];

		mlx5_core_modify_cq_moderation(mdev, &c->rq.cq.mcq,
					       coal->rx_coalesce_usecs,
					       coal->rx_max_coalesced_frames);
	}
}

/* convert a boolean value of cq_mode to mlx5 period mode
 * true  : MLX5_CQ_PERIOD_MODE_START_FROM_CQE
 * false : MLX5_CQ_PERIOD_MODE_START_FROM_EQE
 */
static int cqe_mode_to_period_mode(bool val)
{
	return val ? MLX5_CQ_PERIOD_MODE_START_FROM_CQE : MLX5_CQ_PERIOD_MODE_START_FROM_EQE;
}

int mlx5e_ethtool_set_coalesce(struct mlx5e_priv *priv,
			       struct ethtool_coalesce *coal,
			       struct kernel_ethtool_coalesce *kernel_coal,
			       struct netlink_ext_ack *extack)
{
	struct dim_cq_moder *rx_moder, *tx_moder;
	struct mlx5_core_dev *mdev = priv->mdev;
	struct mlx5e_params new_params;
	bool reset_rx, reset_tx;
	bool reset = true;
	u8 cq_period_mode;
	int err = 0;

	if (!MLX5_CAP_GEN(mdev, cq_moderation))
		return -EOPNOTSUPP;

	if (coal->tx_coalesce_usecs > MLX5E_MAX_COAL_TIME ||
	    coal->rx_coalesce_usecs > MLX5E_MAX_COAL_TIME) {
		netdev_info(priv->netdev, "%s: maximum coalesce time supported is %lu usecs\n",
			    __func__, MLX5E_MAX_COAL_TIME);
		return -ERANGE;
	}

	if (coal->tx_max_coalesced_frames > MLX5E_MAX_COAL_FRAMES ||
	    coal->rx_max_coalesced_frames > MLX5E_MAX_COAL_FRAMES) {
		netdev_info(priv->netdev, "%s: maximum coalesced frames supported is %lu\n",
			    __func__, MLX5E_MAX_COAL_FRAMES);
		return -ERANGE;
	}

	if ((kernel_coal->use_cqe_mode_rx || kernel_coal->use_cqe_mode_tx) &&
	    !MLX5_CAP_GEN(priv->mdev, cq_period_start_from_cqe)) {
		NL_SET_ERR_MSG_MOD(extack, "cqe_mode_rx/tx is not supported on this device");
		return -EOPNOTSUPP;
	}

	mutex_lock(&priv->state_lock);
	new_params = priv->channels.params;

	rx_moder          = &new_params.rx_cq_moderation;
	rx_moder->usec    = coal->rx_coalesce_usecs;
	rx_moder->pkts    = coal->rx_max_coalesced_frames;
	new_params.rx_dim_enabled = !!coal->use_adaptive_rx_coalesce;

	tx_moder          = &new_params.tx_cq_moderation;
	tx_moder->usec    = coal->tx_coalesce_usecs;
	tx_moder->pkts    = coal->tx_max_coalesced_frames;
	new_params.tx_dim_enabled = !!coal->use_adaptive_tx_coalesce;

	reset_rx = !!coal->use_adaptive_rx_coalesce != priv->channels.params.rx_dim_enabled;
	reset_tx = !!coal->use_adaptive_tx_coalesce != priv->channels.params.tx_dim_enabled;

	cq_period_mode = cqe_mode_to_period_mode(kernel_coal->use_cqe_mode_rx);
	if (cq_period_mode != rx_moder->cq_period_mode) {
		mlx5e_set_rx_cq_mode_params(&new_params, cq_period_mode);
		reset_rx = true;
	}

	cq_period_mode = cqe_mode_to_period_mode(kernel_coal->use_cqe_mode_tx);
	if (cq_period_mode != tx_moder->cq_period_mode) {
		mlx5e_set_tx_cq_mode_params(&new_params, cq_period_mode);
		reset_tx = true;
	}

	if (reset_rx) {
		u8 mode = MLX5E_GET_PFLAG(&new_params,
					  MLX5E_PFLAG_RX_CQE_BASED_MODER);

		mlx5e_reset_rx_moderation(&new_params, mode);
	}
	if (reset_tx) {
		u8 mode = MLX5E_GET_PFLAG(&new_params,
					  MLX5E_PFLAG_TX_CQE_BASED_MODER);

		mlx5e_reset_tx_moderation(&new_params, mode);
	}

	/* If DIM state hasn't changed, it's possible to modify interrupt
	 * moderation parameters on the fly, even if the channels are open.
	 */
	if (!reset_rx && !reset_tx && test_bit(MLX5E_STATE_OPENED, &priv->state)) {
		if (!coal->use_adaptive_rx_coalesce)
			mlx5e_set_priv_channels_rx_coalesce(priv, coal);
		if (!coal->use_adaptive_tx_coalesce)
			mlx5e_set_priv_channels_tx_coalesce(priv, coal);
		reset = false;
	}

	err = mlx5e_safe_switch_params(priv, &new_params, NULL, NULL, reset);

	mutex_unlock(&priv->state_lock);
	return err;
}

static int mlx5e_set_coalesce(struct net_device *netdev,
			      struct ethtool_coalesce *coal,
			      struct kernel_ethtool_coalesce *kernel_coal,
			      struct netlink_ext_ack *extack)
{
	struct mlx5e_priv *priv = netdev_priv(netdev);

	return mlx5e_ethtool_set_coalesce(priv, coal, kernel_coal, extack);
}

static void ptys2ethtool_supported_link(struct mlx5_core_dev *mdev,
					unsigned long *supported_modes,
					u32 eth_proto_cap)
{
	unsigned long proto_cap = eth_proto_cap;
	struct ptys2ethtool_config *table;
	u32 max_size;
	int proto;

	mlx5e_ethtool_get_speed_arr(mdev, &table, &max_size);
	for_each_set_bit(proto, &proto_cap, max_size)
		bitmap_or(supported_modes, supported_modes,
			  table[proto].supported,
			  __ETHTOOL_LINK_MODE_MASK_NBITS);
}

static void ptys2ethtool_adver_link(unsigned long *advertising_modes,
				    u32 eth_proto_cap, bool ext)
{
	unsigned long proto_cap = eth_proto_cap;
	struct ptys2ethtool_config *table;
	u32 max_size;
	int proto;

	table = ext ? ptys2ext_ethtool_table : ptys2legacy_ethtool_table;
	max_size = ext ? ARRAY_SIZE(ptys2ext_ethtool_table) :
			 ARRAY_SIZE(ptys2legacy_ethtool_table);

	for_each_set_bit(proto, &proto_cap, max_size)
		bitmap_or(advertising_modes, advertising_modes,
			  table[proto].advertised,
			  __ETHTOOL_LINK_MODE_MASK_NBITS);
}

static const u32 pplm_fec_2_ethtool[] = {
	[MLX5E_FEC_NOFEC] = ETHTOOL_FEC_OFF,
	[MLX5E_FEC_FIRECODE] = ETHTOOL_FEC_BASER,
	[MLX5E_FEC_RS_528_514] = ETHTOOL_FEC_RS,
	[MLX5E_FEC_RS_544_514] = ETHTOOL_FEC_RS,
	[MLX5E_FEC_LLRS_272_257_1] = ETHTOOL_FEC_LLRS,
};

static u32 pplm2ethtool_fec(u_long fec_mode, unsigned long size)
{
	int mode = 0;

	if (!fec_mode)
		return ETHTOOL_FEC_AUTO;

	mode = find_first_bit(&fec_mode, size);

	if (mode < ARRAY_SIZE(pplm_fec_2_ethtool))
		return pplm_fec_2_ethtool[mode];

	return 0;
}

#define MLX5E_ADVERTISE_SUPPORTED_FEC(mlx5_fec, ethtool_fec)		\
	do {								\
		if (mlx5e_fec_in_caps(dev, 1 << (mlx5_fec)))		\
			__set_bit(ethtool_fec,				\
				  link_ksettings->link_modes.supported);\
	} while (0)

static const u32 pplm_fec_2_ethtool_linkmodes[] = {
	[MLX5E_FEC_NOFEC] = ETHTOOL_LINK_MODE_FEC_NONE_BIT,
	[MLX5E_FEC_FIRECODE] = ETHTOOL_LINK_MODE_FEC_BASER_BIT,
	[MLX5E_FEC_RS_528_514] = ETHTOOL_LINK_MODE_FEC_RS_BIT,
	[MLX5E_FEC_RS_544_514] = ETHTOOL_LINK_MODE_FEC_RS_BIT,
	[MLX5E_FEC_LLRS_272_257_1] = ETHTOOL_LINK_MODE_FEC_LLRS_BIT,
};

static int get_fec_supported_advertised(struct mlx5_core_dev *dev,
					struct ethtool_link_ksettings *link_ksettings)
{
	unsigned long active_fec_long;
	u32 active_fec;
	u32 bitn;
	int err;

	err = mlx5e_get_fec_mode(dev, &active_fec, NULL);
	if (err)
		return (err == -EOPNOTSUPP) ? 0 : err;

	MLX5E_ADVERTISE_SUPPORTED_FEC(MLX5E_FEC_NOFEC,
				      ETHTOOL_LINK_MODE_FEC_NONE_BIT);
	MLX5E_ADVERTISE_SUPPORTED_FEC(MLX5E_FEC_FIRECODE,
				      ETHTOOL_LINK_MODE_FEC_BASER_BIT);
	MLX5E_ADVERTISE_SUPPORTED_FEC(MLX5E_FEC_RS_528_514,
				      ETHTOOL_LINK_MODE_FEC_RS_BIT);
	MLX5E_ADVERTISE_SUPPORTED_FEC(MLX5E_FEC_LLRS_272_257_1,
				      ETHTOOL_LINK_MODE_FEC_LLRS_BIT);

	active_fec_long = active_fec;
	/* active fec is a bit set, find out which bit is set and
	 * advertise the corresponding ethtool bit
	 */
	bitn = find_first_bit(&active_fec_long, sizeof(active_fec_long) * BITS_PER_BYTE);
	if (bitn < ARRAY_SIZE(pplm_fec_2_ethtool_linkmodes))
		__set_bit(pplm_fec_2_ethtool_linkmodes[bitn],
			  link_ksettings->link_modes.advertising);

	return 0;
}

static void ptys2ethtool_supported_advertised_port(struct mlx5_core_dev *mdev,
						   struct ethtool_link_ksettings *link_ksettings,
						   u32 eth_proto_cap, u8 connector_type)
{
	if (!MLX5_CAP_PCAM_FEATURE(mdev, ptys_connector_type)) {
		if (eth_proto_cap & (MLX5E_PROT_MASK(MLX5E_10GBASE_CR)
				   | MLX5E_PROT_MASK(MLX5E_10GBASE_SR)
				   | MLX5E_PROT_MASK(MLX5E_40GBASE_CR4)
				   | MLX5E_PROT_MASK(MLX5E_40GBASE_SR4)
				   | MLX5E_PROT_MASK(MLX5E_100GBASE_SR4)
				   | MLX5E_PROT_MASK(MLX5E_1000BASE_CX_SGMII))) {
			ethtool_link_ksettings_add_link_mode(link_ksettings,
							     supported,
							     FIBRE);
			ethtool_link_ksettings_add_link_mode(link_ksettings,
							     advertising,
							     FIBRE);
		}

		if (eth_proto_cap & (MLX5E_PROT_MASK(MLX5E_100GBASE_KR4)
				   | MLX5E_PROT_MASK(MLX5E_40GBASE_KR4)
				   | MLX5E_PROT_MASK(MLX5E_10GBASE_KR)
				   | MLX5E_PROT_MASK(MLX5E_10GBASE_KX4)
				   | MLX5E_PROT_MASK(MLX5E_1000BASE_KX))) {
			ethtool_link_ksettings_add_link_mode(link_ksettings,
							     supported,
							     Backplane);
			ethtool_link_ksettings_add_link_mode(link_ksettings,
							     advertising,
							     Backplane);
		}
		return;
	}

	switch (connector_type) {
	case MLX5E_PORT_TP:
		ethtool_link_ksettings_add_link_mode(link_ksettings,
						     supported, TP);
		ethtool_link_ksettings_add_link_mode(link_ksettings,
						     advertising, TP);
		break;
	case MLX5E_PORT_AUI:
		ethtool_link_ksettings_add_link_mode(link_ksettings,
						     supported, AUI);
		ethtool_link_ksettings_add_link_mode(link_ksettings,
						     advertising, AUI);
		break;
	case MLX5E_PORT_BNC:
		ethtool_link_ksettings_add_link_mode(link_ksettings,
						     supported, BNC);
		ethtool_link_ksettings_add_link_mode(link_ksettings,
						     advertising, BNC);
		break;
	case MLX5E_PORT_MII:
		ethtool_link_ksettings_add_link_mode(link_ksettings,
						     supported, MII);
		ethtool_link_ksettings_add_link_mode(link_ksettings,
						     advertising, MII);
		break;
	case MLX5E_PORT_FIBRE:
		ethtool_link_ksettings_add_link_mode(link_ksettings,
						     supported, FIBRE);
		ethtool_link_ksettings_add_link_mode(link_ksettings,
						     advertising, FIBRE);
		break;
	case MLX5E_PORT_DA:
		ethtool_link_ksettings_add_link_mode(link_ksettings,
						     supported, Backplane);
		ethtool_link_ksettings_add_link_mode(link_ksettings,
						     advertising, Backplane);
		break;
	case MLX5E_PORT_NONE:
	case MLX5E_PORT_OTHER:
	default:
		break;
	}
}

static void get_speed_duplex(struct net_device *netdev,
			     u32 eth_proto_oper, bool force_legacy,
			     u16 data_rate_oper,
			     struct ethtool_link_ksettings *link_ksettings)
{
	struct mlx5e_priv *priv = netdev_priv(netdev);
	u32 speed = SPEED_UNKNOWN;
	u8 duplex = DUPLEX_UNKNOWN;

	if (!netif_carrier_ok(netdev))
		goto out;

	speed = mlx5_port_ptys2speed(priv->mdev, eth_proto_oper, force_legacy);
	if (!speed) {
		if (data_rate_oper)
			speed = 100 * data_rate_oper;
		else
			speed = SPEED_UNKNOWN;
		goto out;
	}

	duplex = DUPLEX_FULL;

out:
	link_ksettings->base.speed = speed;
	link_ksettings->base.duplex = duplex;
}

static void get_supported(struct mlx5_core_dev *mdev, u32 eth_proto_cap,
			  struct ethtool_link_ksettings *link_ksettings)
{
	unsigned long *supported = link_ksettings->link_modes.supported;
	ptys2ethtool_supported_link(mdev, supported, eth_proto_cap);

	ethtool_link_ksettings_add_link_mode(link_ksettings, supported, Pause);
}

static void get_advertising(u32 eth_proto_cap, u8 tx_pause, u8 rx_pause,
			    struct ethtool_link_ksettings *link_ksettings,
			    bool ext)
{
	unsigned long *advertising = link_ksettings->link_modes.advertising;
	ptys2ethtool_adver_link(advertising, eth_proto_cap, ext);

	if (rx_pause)
		ethtool_link_ksettings_add_link_mode(link_ksettings, advertising, Pause);
	if (tx_pause ^ rx_pause)
		ethtool_link_ksettings_add_link_mode(link_ksettings, advertising, Asym_Pause);
}

static int ptys2connector_type[MLX5E_CONNECTOR_TYPE_NUMBER] = {
		[MLX5E_PORT_UNKNOWN]            = PORT_OTHER,
		[MLX5E_PORT_NONE]               = PORT_NONE,
		[MLX5E_PORT_TP]                 = PORT_TP,
		[MLX5E_PORT_AUI]                = PORT_AUI,
		[MLX5E_PORT_BNC]                = PORT_BNC,
		[MLX5E_PORT_MII]                = PORT_MII,
		[MLX5E_PORT_FIBRE]              = PORT_FIBRE,
		[MLX5E_PORT_DA]                 = PORT_DA,
		[MLX5E_PORT_OTHER]              = PORT_OTHER,
	};

static u8 get_connector_port(struct mlx5_core_dev *mdev, u32 eth_proto, u8 connector_type)
{
	if (MLX5_CAP_PCAM_FEATURE(mdev, ptys_connector_type))
		return ptys2connector_type[connector_type];

	if (eth_proto &
	    (MLX5E_PROT_MASK(MLX5E_10GBASE_SR)   |
	     MLX5E_PROT_MASK(MLX5E_40GBASE_SR4)  |
	     MLX5E_PROT_MASK(MLX5E_100GBASE_SR4) |
	     MLX5E_PROT_MASK(MLX5E_1000BASE_CX_SGMII))) {
		return PORT_FIBRE;
	}

	if (eth_proto &
	    (MLX5E_PROT_MASK(MLX5E_40GBASE_CR4) |
	     MLX5E_PROT_MASK(MLX5E_10GBASE_CR)  |
	     MLX5E_PROT_MASK(MLX5E_100GBASE_CR4))) {
		return PORT_DA;
	}

	if (eth_proto &
	    (MLX5E_PROT_MASK(MLX5E_10GBASE_KX4) |
	     MLX5E_PROT_MASK(MLX5E_10GBASE_KR)  |
	     MLX5E_PROT_MASK(MLX5E_40GBASE_KR4) |
	     MLX5E_PROT_MASK(MLX5E_100GBASE_KR4))) {
		return PORT_NONE;
	}

	return PORT_OTHER;
}

static void get_lp_advertising(struct mlx5_core_dev *mdev, u32 eth_proto_lp,
			       struct ethtool_link_ksettings *link_ksettings)
{
	unsigned long *lp_advertising = link_ksettings->link_modes.lp_advertising;
	bool ext = mlx5_ptys_ext_supported(mdev);

	ptys2ethtool_adver_link(lp_advertising, eth_proto_lp, ext);
}

int mlx5e_ethtool_get_link_ksettings(struct mlx5e_priv *priv,
				     struct ethtool_link_ksettings *link_ksettings)
{
	struct mlx5_core_dev *mdev = priv->mdev;
	u32 out[MLX5_ST_SZ_DW(ptys_reg)] = {};
	u32 eth_proto_admin;
	u8 an_disable_admin;
	u16 data_rate_oper;
	u32 eth_proto_oper;
	u32 eth_proto_cap;
	u8 connector_type;
	u32 rx_pause = 0;
	u32 tx_pause = 0;
	u32 eth_proto_lp;
	bool admin_ext;
	u8 an_status;
	bool ext;
	int err;

	err = mlx5_query_port_ptys(mdev, out, sizeof(out), MLX5_PTYS_EN, 1);
	if (err) {
		netdev_err(priv->netdev, "%s: query port ptys failed: %d\n",
			   __func__, err);
		goto err_query_regs;
	}
	ext = !!MLX5_GET_ETH_PROTO(ptys_reg, out, true, eth_proto_capability);
	eth_proto_cap    = MLX5_GET_ETH_PROTO(ptys_reg, out, ext,
					      eth_proto_capability);
	eth_proto_admin  = MLX5_GET_ETH_PROTO(ptys_reg, out, ext,
					      eth_proto_admin);
	/* Fields: eth_proto_admin and ext_eth_proto_admin  are
	 * mutually exclusive. Hence try reading legacy advertising
	 * when extended advertising is zero.
	 * admin_ext indicates which proto_admin (ext vs. legacy)
	 * should be read and interpreted
	 */
	admin_ext = ext;
	if (ext && !eth_proto_admin) {
		eth_proto_admin  = MLX5_GET_ETH_PROTO(ptys_reg, out, false,
						      eth_proto_admin);
		admin_ext = false;
	}

	eth_proto_oper   = MLX5_GET_ETH_PROTO(ptys_reg, out, admin_ext,
					      eth_proto_oper);
	eth_proto_lp	    = MLX5_GET(ptys_reg, out, eth_proto_lp_advertise);
	an_disable_admin    = MLX5_GET(ptys_reg, out, an_disable_admin);
	an_status	    = MLX5_GET(ptys_reg, out, an_status);
	connector_type	    = MLX5_GET(ptys_reg, out, connector_type);
	data_rate_oper	    = MLX5_GET(ptys_reg, out, data_rate_oper);

	mlx5_query_port_pause(mdev, &rx_pause, &tx_pause);

	ethtool_link_ksettings_zero_link_mode(link_ksettings, supported);
	ethtool_link_ksettings_zero_link_mode(link_ksettings, advertising);

	get_supported(mdev, eth_proto_cap, link_ksettings);
	get_advertising(eth_proto_admin, tx_pause, rx_pause, link_ksettings,
			admin_ext);
	get_speed_duplex(priv->netdev, eth_proto_oper, !admin_ext,
			 data_rate_oper, link_ksettings);

	eth_proto_oper = eth_proto_oper ? eth_proto_oper : eth_proto_cap;
	connector_type = connector_type < MLX5E_CONNECTOR_TYPE_NUMBER ?
			 connector_type : MLX5E_PORT_UNKNOWN;
	link_ksettings->base.port = get_connector_port(mdev, eth_proto_oper, connector_type);
	ptys2ethtool_supported_advertised_port(mdev, link_ksettings, eth_proto_admin,
					       connector_type);
	get_lp_advertising(mdev, eth_proto_lp, link_ksettings);

	if (an_status == MLX5_AN_COMPLETE)
		ethtool_link_ksettings_add_link_mode(link_ksettings,
						     lp_advertising, Autoneg);

	link_ksettings->base.autoneg = an_disable_admin ? AUTONEG_DISABLE :
							  AUTONEG_ENABLE;
	ethtool_link_ksettings_add_link_mode(link_ksettings, supported,
					     Autoneg);

	err = get_fec_supported_advertised(mdev, link_ksettings);
	if (err) {
		netdev_dbg(priv->netdev, "%s: FEC caps query failed: %d\n",
			   __func__, err);
		err = 0; /* don't fail caps query because of FEC error */
	}

	if (!an_disable_admin)
		ethtool_link_ksettings_add_link_mode(link_ksettings,
						     advertising, Autoneg);

err_query_regs:
	return err;
}

static int mlx5e_get_link_ksettings(struct net_device *netdev,
				    struct ethtool_link_ksettings *link_ksettings)
{
	struct mlx5e_priv *priv = netdev_priv(netdev);

	return mlx5e_ethtool_get_link_ksettings(priv, link_ksettings);
}

static int mlx5e_speed_validate(struct net_device *netdev, bool ext,
				const unsigned long link_modes, u8 autoneg)
{
	/* Extended link-mode has no speed limitations. */
	if (ext)
		return 0;

	if ((link_modes & MLX5E_PROT_MASK(MLX5E_56GBASE_R4)) &&
	    autoneg != AUTONEG_ENABLE) {
		netdev_err(netdev, "%s: 56G link speed requires autoneg enabled\n",
			   __func__);
		return -EINVAL;
	}
	return 0;
}

static u32 mlx5e_ethtool2ptys_adver_link(const unsigned long *link_modes)
{
	u32 i, ptys_modes = 0;

	for (i = 0; i < MLX5E_LINK_MODES_NUMBER; ++i) {
		if (*ptys2legacy_ethtool_table[i].advertised == 0)
			continue;
		if (bitmap_intersects(ptys2legacy_ethtool_table[i].advertised,
				      link_modes,
				      __ETHTOOL_LINK_MODE_MASK_NBITS))
			ptys_modes |= MLX5E_PROT_MASK(i);
	}

	return ptys_modes;
}

static u32 mlx5e_ethtool2ptys_ext_adver_link(const unsigned long *link_modes)
{
	u32 i, ptys_modes = 0;
	unsigned long modes[2];

	for (i = 0; i < MLX5E_EXT_LINK_MODES_NUMBER; ++i) {
		if (ptys2ext_ethtool_table[i].advertised[0] == 0 &&
		    ptys2ext_ethtool_table[i].advertised[1] == 0)
			continue;
		memset(modes, 0, sizeof(modes));
		bitmap_and(modes, ptys2ext_ethtool_table[i].advertised,
			   link_modes, __ETHTOOL_LINK_MODE_MASK_NBITS);

		if (modes[0] == ptys2ext_ethtool_table[i].advertised[0] &&
		    modes[1] == ptys2ext_ethtool_table[i].advertised[1])
			ptys_modes |= MLX5E_PROT_MASK(i);
	}
	return ptys_modes;
}

static bool ext_link_mode_requested(const unsigned long *adver)
{
#define MLX5E_MIN_PTYS_EXT_LINK_MODE_BIT ETHTOOL_LINK_MODE_50000baseKR_Full_BIT
	int size = __ETHTOOL_LINK_MODE_MASK_NBITS - MLX5E_MIN_PTYS_EXT_LINK_MODE_BIT;
	__ETHTOOL_DECLARE_LINK_MODE_MASK(modes) = {0,};

	bitmap_set(modes, MLX5E_MIN_PTYS_EXT_LINK_MODE_BIT, size);
	return bitmap_intersects(modes, adver, __ETHTOOL_LINK_MODE_MASK_NBITS);
}

static bool ext_requested(u8 autoneg, const unsigned long *adver, bool ext_supported)
{
	bool ext_link_mode = ext_link_mode_requested(adver);

	return  autoneg == AUTONEG_ENABLE ? ext_link_mode : ext_supported;
}

int mlx5e_ethtool_set_link_ksettings(struct mlx5e_priv *priv,
				     const struct ethtool_link_ksettings *link_ksettings)
{
	struct mlx5_core_dev *mdev = priv->mdev;
	struct mlx5_port_eth_proto eproto;
	const unsigned long *adver;
	bool an_changes = false;
	u8 an_disable_admin;
	bool ext_supported;
	u8 an_disable_cap;
	bool an_disable;
	u32 link_modes;
	u8 an_status;
	u8 autoneg;
	u32 speed;
	bool ext;
	int err;

	u32 (*ethtool2ptys_adver_func)(const unsigned long *adver);

	adver = link_ksettings->link_modes.advertising;
	autoneg = link_ksettings->base.autoneg;
	speed = link_ksettings->base.speed;

	ext_supported = mlx5_ptys_ext_supported(mdev);
	ext = ext_requested(autoneg, adver, ext_supported);
	if (!ext_supported && ext)
		return -EOPNOTSUPP;

	ethtool2ptys_adver_func = ext ? mlx5e_ethtool2ptys_ext_adver_link :
				  mlx5e_ethtool2ptys_adver_link;
	err = mlx5_port_query_eth_proto(mdev, 1, ext, &eproto);
	if (err) {
		netdev_err(priv->netdev, "%s: query port eth proto failed: %d\n",
			   __func__, err);
		goto out;
	}
	link_modes = autoneg == AUTONEG_ENABLE ? ethtool2ptys_adver_func(adver) :
		mlx5_port_speed2linkmodes(mdev, speed, !ext);

	err = mlx5e_speed_validate(priv->netdev, ext, link_modes, autoneg);
	if (err)
		goto out;

	link_modes = link_modes & eproto.cap;
	if (!link_modes) {
		netdev_err(priv->netdev, "%s: Not supported link mode(s) requested",
			   __func__);
		err = -EINVAL;
		goto out;
	}

	mlx5_port_query_eth_autoneg(mdev, &an_status, &an_disable_cap,
				    &an_disable_admin);

	an_disable = autoneg == AUTONEG_DISABLE;
	an_changes = ((!an_disable && an_disable_admin) ||
		      (an_disable && !an_disable_admin));

	if (!an_changes && link_modes == eproto.admin)
		goto out;

	mlx5_port_set_eth_ptys(mdev, an_disable, link_modes, ext);
	mlx5_toggle_port_link(mdev);

out:
	return err;
}

static int mlx5e_set_link_ksettings(struct net_device *netdev,
				    const struct ethtool_link_ksettings *link_ksettings)
{
	struct mlx5e_priv *priv = netdev_priv(netdev);

	return mlx5e_ethtool_set_link_ksettings(priv, link_ksettings);
}

u32 mlx5e_ethtool_get_rxfh_key_size(struct mlx5e_priv *priv)
{
	return sizeof_field(struct mlx5e_rss_params_hash, toeplitz_hash_key);
}

static u32 mlx5e_get_rxfh_key_size(struct net_device *netdev)
{
	struct mlx5e_priv *priv = netdev_priv(netdev);

	return mlx5e_ethtool_get_rxfh_key_size(priv);
}

u32 mlx5e_ethtool_get_rxfh_indir_size(struct mlx5e_priv *priv)
{
	return mlx5e_rqt_size(priv->mdev, priv->channels.params.num_channels);
}

static u32 mlx5e_get_rxfh_indir_size(struct net_device *netdev)
{
	struct mlx5e_priv *priv = netdev_priv(netdev);

	return mlx5e_ethtool_get_rxfh_indir_size(priv);
}

static int mlx5e_get_rxfh_context(struct net_device *dev, u32 *indir,
				  u8 *key, u8 *hfunc, u32 rss_context)
{
	struct mlx5e_priv *priv = netdev_priv(dev);
	int err;

	mutex_lock(&priv->state_lock);
	err = mlx5e_rx_res_rss_get_rxfh(priv->rx_res, rss_context, indir, key, hfunc);
	mutex_unlock(&priv->state_lock);
	return err;
}

static int mlx5e_set_rxfh_context(struct net_device *dev, const u32 *indir,
				  const u8 *key, const u8 hfunc,
				  u32 *rss_context, bool delete)
{
	struct mlx5e_priv *priv = netdev_priv(dev);
	int err;

	mutex_lock(&priv->state_lock);
	if (delete) {
		err = mlx5e_rx_res_rss_destroy(priv->rx_res, *rss_context);
		goto unlock;
	}

	if (*rss_context == ETH_RXFH_CONTEXT_ALLOC) {
		unsigned int count = priv->channels.params.num_channels;

		err = mlx5e_rx_res_rss_init(priv->rx_res, rss_context, count);
		if (err)
			goto unlock;
	}

	err = mlx5e_rx_res_rss_set_rxfh(priv->rx_res, *rss_context, indir, key,
					hfunc == ETH_RSS_HASH_NO_CHANGE ? NULL : &hfunc);

unlock:
	mutex_unlock(&priv->state_lock);
	return err;
}

int mlx5e_get_rxfh(struct net_device *netdev, u32 *indir, u8 *key,
		   u8 *hfunc)
{
	return mlx5e_get_rxfh_context(netdev, indir, key, hfunc, 0);
}

int mlx5e_set_rxfh(struct net_device *dev, const u32 *indir,
		   const u8 *key, const u8 hfunc)
{
	struct mlx5e_priv *priv = netdev_priv(dev);
	int err;

	mutex_lock(&priv->state_lock);
	err = mlx5e_rx_res_rss_set_rxfh(priv->rx_res, 0, indir, key,
					hfunc == ETH_RSS_HASH_NO_CHANGE ? NULL : &hfunc);
	mutex_unlock(&priv->state_lock);
	return err;
}

#define MLX5E_PFC_PREVEN_AUTO_TOUT_MSEC		100
#define MLX5E_PFC_PREVEN_TOUT_MAX_MSEC		8000
#define MLX5E_PFC_PREVEN_MINOR_PRECENT		85
#define MLX5E_PFC_PREVEN_TOUT_MIN_MSEC		80
#define MLX5E_DEVICE_STALL_MINOR_WATERMARK(critical_tout) \
	max_t(u16, MLX5E_PFC_PREVEN_TOUT_MIN_MSEC, \
	      (critical_tout * MLX5E_PFC_PREVEN_MINOR_PRECENT) / 100)

static int mlx5e_get_pfc_prevention_tout(struct net_device *netdev,
					 u16 *pfc_prevention_tout)
{
	struct mlx5e_priv *priv    = netdev_priv(netdev);
	struct mlx5_core_dev *mdev = priv->mdev;

	if (!MLX5_CAP_PCAM_FEATURE((priv)->mdev, pfcc_mask) ||
	    !MLX5_CAP_DEBUG((priv)->mdev, stall_detect))
		return -EOPNOTSUPP;

	return mlx5_query_port_stall_watermark(mdev, pfc_prevention_tout, NULL);
}

static int mlx5e_set_pfc_prevention_tout(struct net_device *netdev,
					 u16 pfc_preven)
{
	struct mlx5e_priv *priv = netdev_priv(netdev);
	struct mlx5_core_dev *mdev = priv->mdev;
	u16 critical_tout;
	u16 minor;

	if (!MLX5_CAP_PCAM_FEATURE((priv)->mdev, pfcc_mask) ||
	    !MLX5_CAP_DEBUG((priv)->mdev, stall_detect))
		return -EOPNOTSUPP;

	critical_tout = (pfc_preven == PFC_STORM_PREVENTION_AUTO) ?
			MLX5E_PFC_PREVEN_AUTO_TOUT_MSEC :
			pfc_preven;

	if (critical_tout != PFC_STORM_PREVENTION_DISABLE &&
	    (critical_tout > MLX5E_PFC_PREVEN_TOUT_MAX_MSEC ||
	     critical_tout < MLX5E_PFC_PREVEN_TOUT_MIN_MSEC)) {
		netdev_info(netdev, "%s: pfc prevention tout not in range (%d-%d)\n",
			    __func__, MLX5E_PFC_PREVEN_TOUT_MIN_MSEC,
			    MLX5E_PFC_PREVEN_TOUT_MAX_MSEC);
		return -EINVAL;
	}

	minor = MLX5E_DEVICE_STALL_MINOR_WATERMARK(critical_tout);
	return mlx5_set_port_stall_watermark(mdev, critical_tout,
					     minor);
}

static int mlx5e_get_tunable(struct net_device *dev,
			     const struct ethtool_tunable *tuna,
			     void *data)
{
	int err;

	switch (tuna->id) {
	case ETHTOOL_PFC_PREVENTION_TOUT:
		err = mlx5e_get_pfc_prevention_tout(dev, data);
		break;
	default:
		err = -EINVAL;
		break;
	}

	return err;
}

static int mlx5e_set_tunable(struct net_device *dev,
			     const struct ethtool_tunable *tuna,
			     const void *data)
{
	struct mlx5e_priv *priv = netdev_priv(dev);
	int err;

	mutex_lock(&priv->state_lock);

	switch (tuna->id) {
	case ETHTOOL_PFC_PREVENTION_TOUT:
		err = mlx5e_set_pfc_prevention_tout(dev, *(u16 *)data);
		break;
	default:
		err = -EINVAL;
		break;
	}

	mutex_unlock(&priv->state_lock);
	return err;
}

static void mlx5e_get_pause_stats(struct net_device *netdev,
				  struct ethtool_pause_stats *pause_stats)
{
	struct mlx5e_priv *priv = netdev_priv(netdev);

	mlx5e_stats_pause_get(priv, pause_stats);
}

void mlx5e_ethtool_get_pauseparam(struct mlx5e_priv *priv,
				  struct ethtool_pauseparam *pauseparam)
{
	struct mlx5_core_dev *mdev = priv->mdev;
	int err;

	err = mlx5_query_port_pause(mdev, &pauseparam->rx_pause,
				    &pauseparam->tx_pause);
	if (err) {
		netdev_err(priv->netdev, "%s: mlx5_query_port_pause failed:0x%x\n",
			   __func__, err);
	}
}

static void mlx5e_get_pauseparam(struct net_device *netdev,
				 struct ethtool_pauseparam *pauseparam)
{
	struct mlx5e_priv *priv = netdev_priv(netdev);

	mlx5e_ethtool_get_pauseparam(priv, pauseparam);
}

int mlx5e_ethtool_set_pauseparam(struct mlx5e_priv *priv,
				 struct ethtool_pauseparam *pauseparam)
{
	struct mlx5_core_dev *mdev = priv->mdev;
	int err;

	if (!MLX5_CAP_GEN(mdev, vport_group_manager))
		return -EOPNOTSUPP;

	if (pauseparam->autoneg)
		return -EINVAL;

	err = mlx5_set_port_pause(mdev,
				  pauseparam->rx_pause ? 1 : 0,
				  pauseparam->tx_pause ? 1 : 0);
	if (err) {
		netdev_err(priv->netdev, "%s: mlx5_set_port_pause failed:0x%x\n",
			   __func__, err);
	}

	return err;
}

static int mlx5e_set_pauseparam(struct net_device *netdev,
				struct ethtool_pauseparam *pauseparam)
{
	struct mlx5e_priv *priv = netdev_priv(netdev);

	return mlx5e_ethtool_set_pauseparam(priv, pauseparam);
}

int mlx5e_ethtool_get_ts_info(struct mlx5e_priv *priv,
			      struct ethtool_ts_info *info)
{
	struct mlx5_core_dev *mdev = priv->mdev;

	info->phc_index = mlx5_clock_get_ptp_index(mdev);

	if (!MLX5_CAP_GEN(priv->mdev, device_frequency_khz) ||
	    info->phc_index == -1)
		return 0;

	info->so_timestamping = SOF_TIMESTAMPING_TX_HARDWARE |
				SOF_TIMESTAMPING_RX_HARDWARE |
				SOF_TIMESTAMPING_RAW_HARDWARE;

	info->tx_types = BIT(HWTSTAMP_TX_OFF) |
			 BIT(HWTSTAMP_TX_ON);

	info->rx_filters = BIT(HWTSTAMP_FILTER_NONE) |
			   BIT(HWTSTAMP_FILTER_ALL);

	return 0;
}

static int mlx5e_get_ts_info(struct net_device *dev,
			     struct ethtool_ts_info *info)
{
	struct mlx5e_priv *priv = netdev_priv(dev);

	return mlx5e_ethtool_get_ts_info(priv, info);
}

static __u32 mlx5e_get_wol_supported(struct mlx5_core_dev *mdev)
{
	__u32 ret = 0;

	if (MLX5_CAP_GEN(mdev, wol_g))
		ret |= WAKE_MAGIC;

	if (MLX5_CAP_GEN(mdev, wol_s))
		ret |= WAKE_MAGICSECURE;

	if (MLX5_CAP_GEN(mdev, wol_a))
		ret |= WAKE_ARP;

	if (MLX5_CAP_GEN(mdev, wol_b))
		ret |= WAKE_BCAST;

	if (MLX5_CAP_GEN(mdev, wol_m))
		ret |= WAKE_MCAST;

	if (MLX5_CAP_GEN(mdev, wol_u))
		ret |= WAKE_UCAST;

	if (MLX5_CAP_GEN(mdev, wol_p))
		ret |= WAKE_PHY;

	return ret;
}

static __u32 mlx5e_reformat_wol_mode_mlx5_to_linux(u8 mode)
{
	__u32 ret = 0;

	if (mode & MLX5_WOL_MAGIC)
		ret |= WAKE_MAGIC;

	if (mode & MLX5_WOL_SECURED_MAGIC)
		ret |= WAKE_MAGICSECURE;

	if (mode & MLX5_WOL_ARP)
		ret |= WAKE_ARP;

	if (mode & MLX5_WOL_BROADCAST)
		ret |= WAKE_BCAST;

	if (mode & MLX5_WOL_MULTICAST)
		ret |= WAKE_MCAST;

	if (mode & MLX5_WOL_UNICAST)
		ret |= WAKE_UCAST;

	if (mode & MLX5_WOL_PHY_ACTIVITY)
		ret |= WAKE_PHY;

	return ret;
}

static u8 mlx5e_reformat_wol_mode_linux_to_mlx5(__u32 mode)
{
	u8 ret = 0;

	if (mode & WAKE_MAGIC)
		ret |= MLX5_WOL_MAGIC;

	if (mode & WAKE_MAGICSECURE)
		ret |= MLX5_WOL_SECURED_MAGIC;

	if (mode & WAKE_ARP)
		ret |= MLX5_WOL_ARP;

	if (mode & WAKE_BCAST)
		ret |= MLX5_WOL_BROADCAST;

	if (mode & WAKE_MCAST)
		ret |= MLX5_WOL_MULTICAST;

	if (mode & WAKE_UCAST)
		ret |= MLX5_WOL_UNICAST;

	if (mode & WAKE_PHY)
		ret |= MLX5_WOL_PHY_ACTIVITY;

	return ret;
}

static void mlx5e_get_wol(struct net_device *netdev,
			  struct ethtool_wolinfo *wol)
{
	struct mlx5e_priv *priv = netdev_priv(netdev);
	struct mlx5_core_dev *mdev = priv->mdev;
	u8 mlx5_wol_mode;
	int err;

	memset(wol, 0, sizeof(*wol));

	wol->supported = mlx5e_get_wol_supported(mdev);
	if (!wol->supported)
		return;

	err = mlx5_query_port_wol(mdev, &mlx5_wol_mode);
	if (err)
		return;

	wol->wolopts = mlx5e_reformat_wol_mode_mlx5_to_linux(mlx5_wol_mode);
}

static int mlx5e_set_wol(struct net_device *netdev, struct ethtool_wolinfo *wol)
{
	struct mlx5e_priv *priv = netdev_priv(netdev);
	struct mlx5_core_dev *mdev = priv->mdev;
	__u32 wol_supported = mlx5e_get_wol_supported(mdev);
	u32 mlx5_wol_mode;

	if (!wol_supported)
		return -EOPNOTSUPP;

	if (wol->wolopts & ~wol_supported)
		return -EINVAL;

	mlx5_wol_mode = mlx5e_reformat_wol_mode_linux_to_mlx5(wol->wolopts);

	return mlx5_set_port_wol(mdev, mlx5_wol_mode);
}

static void mlx5e_get_fec_stats(struct net_device *netdev,
				struct ethtool_fec_stats *fec_stats)
{
	struct mlx5e_priv *priv = netdev_priv(netdev);

	mlx5e_stats_fec_get(priv, fec_stats);
}

static int mlx5e_get_fecparam(struct net_device *netdev,
			      struct ethtool_fecparam *fecparam)
{
	struct mlx5e_priv *priv = netdev_priv(netdev);
	struct mlx5_core_dev *mdev = priv->mdev;
	u16 fec_configured;
	u32 fec_active;
	int err;

	err = mlx5e_get_fec_mode(mdev, &fec_active, &fec_configured);

	if (err)
		return err;

	fecparam->active_fec = pplm2ethtool_fec((unsigned long)fec_active,
						sizeof(unsigned long) * BITS_PER_BYTE);

	if (!fecparam->active_fec)
		return -EOPNOTSUPP;

	fecparam->fec = pplm2ethtool_fec((unsigned long)fec_configured,
					 sizeof(unsigned long) * BITS_PER_BYTE);

	return 0;
}

static int mlx5e_set_fecparam(struct net_device *netdev,
			      struct ethtool_fecparam *fecparam)
{
	struct mlx5e_priv *priv = netdev_priv(netdev);
	struct mlx5_core_dev *mdev = priv->mdev;
	unsigned long fec_bitmap;
	u16 fec_policy = 0;
	int mode;
	int err;

	bitmap_from_arr32(&fec_bitmap, &fecparam->fec, sizeof(fecparam->fec) * BITS_PER_BYTE);
	if (bitmap_weight(&fec_bitmap, ETHTOOL_FEC_LLRS_BIT + 1) > 1)
		return -EOPNOTSUPP;

	for (mode = 0; mode < ARRAY_SIZE(pplm_fec_2_ethtool); mode++) {
		if (!(pplm_fec_2_ethtool[mode] & fecparam->fec))
			continue;
		fec_policy |= (1 << mode);
		break;
	}

	err = mlx5e_set_fec_mode(mdev, fec_policy);

	if (err)
		return err;

	mlx5_toggle_port_link(mdev);

	return 0;
}

static int mlx5e_set_phys_id(struct net_device *dev,
			     enum ethtool_phys_id_state state)
{
	struct mlx5e_priv *priv = netdev_priv(dev);
	struct mlx5_core_dev *mdev = priv->mdev;
	u16 beacon_duration;

	if (!MLX5_CAP_GEN(mdev, beacon_led))
		return -EOPNOTSUPP;

	switch (state) {
	case ETHTOOL_ID_ACTIVE:
		beacon_duration = MLX5_BEACON_DURATION_INF;
		break;
	case ETHTOOL_ID_INACTIVE:
		beacon_duration = MLX5_BEACON_DURATION_OFF;
		break;
	default:
		return -EOPNOTSUPP;
	}

	return mlx5_set_port_beacon(mdev, beacon_duration);
}

static int mlx5e_get_module_info(struct net_device *netdev,
				 struct ethtool_modinfo *modinfo)
{
	struct mlx5e_priv *priv = netdev_priv(netdev);
	struct mlx5_core_dev *dev = priv->mdev;
	int size_read = 0;
	u8 data[4] = {0};

	size_read = mlx5_query_module_eeprom(dev, 0, 2, data);
	if (size_read < 2)
		return -EIO;

	/* data[0] = identifier byte */
	switch (data[0]) {
	case MLX5_MODULE_ID_QSFP:
		modinfo->type       = ETH_MODULE_SFF_8436;
		modinfo->eeprom_len = ETH_MODULE_SFF_8436_MAX_LEN;
		break;
	case MLX5_MODULE_ID_QSFP_PLUS:
	case MLX5_MODULE_ID_QSFP28:
		/* data[1] = revision id */
		if (data[0] == MLX5_MODULE_ID_QSFP28 || data[1] >= 0x3) {
			modinfo->type       = ETH_MODULE_SFF_8636;
			modinfo->eeprom_len = ETH_MODULE_SFF_8636_MAX_LEN;
		} else {
			modinfo->type       = ETH_MODULE_SFF_8436;
			modinfo->eeprom_len = ETH_MODULE_SFF_8436_MAX_LEN;
		}
		break;
	case MLX5_MODULE_ID_SFP:
		modinfo->type       = ETH_MODULE_SFF_8472;
		modinfo->eeprom_len = ETH_MODULE_SFF_8472_LEN;
		break;
	default:
		netdev_err(priv->netdev, "%s: cable type not recognized:0x%x\n",
			   __func__, data[0]);
		return -EINVAL;
	}

	return 0;
}

static int mlx5e_get_module_eeprom(struct net_device *netdev,
				   struct ethtool_eeprom *ee,
				   u8 *data)
{
	struct mlx5e_priv *priv = netdev_priv(netdev);
	struct mlx5_core_dev *mdev = priv->mdev;
	int offset = ee->offset;
	int size_read;
	int i = 0;

	if (!ee->len)
		return -EINVAL;

	memset(data, 0, ee->len);

	while (i < ee->len) {
		size_read = mlx5_query_module_eeprom(mdev, offset, ee->len - i,
						     data + i);

		if (!size_read)
			/* Done reading */
			return 0;

		if (size_read < 0) {
			netdev_err(priv->netdev, "%s: mlx5_query_eeprom failed:0x%x\n",
				   __func__, size_read);
			return size_read;
		}

		i += size_read;
		offset += size_read;
	}

	return 0;
}

static int mlx5e_get_module_eeprom_by_page(struct net_device *netdev,
					   const struct ethtool_module_eeprom *page_data,
					   struct netlink_ext_ack *extack)
{
	struct mlx5e_priv *priv = netdev_priv(netdev);
	struct mlx5_module_eeprom_query_params query;
	struct mlx5_core_dev *mdev = priv->mdev;
	u8 *data = page_data->data;
	int size_read;
	int i = 0;

	if (!page_data->length)
		return -EINVAL;

	memset(data, 0, page_data->length);

	query.offset = page_data->offset;
	query.i2c_address = page_data->i2c_address;
	query.bank = page_data->bank;
	query.page = page_data->page;
	while (i < page_data->length) {
		query.size = page_data->length - i;
		size_read = mlx5_query_module_eeprom_by_page(mdev, &query, data + i);

		/* Done reading, return how many bytes was read */
		if (!size_read)
			return i;

		if (size_read == -EINVAL)
			return -EINVAL;
		if (size_read < 0) {
			netdev_err(priv->netdev, "%s: mlx5_query_module_eeprom_by_page failed:0x%x\n",
				   __func__, size_read);
			return i;
		}

		i += size_read;
		query.offset += size_read;
	}

	return i;
}

int mlx5e_ethtool_flash_device(struct mlx5e_priv *priv,
			       struct ethtool_flash *flash)
{
	struct mlx5_core_dev *mdev = priv->mdev;
	struct net_device *dev = priv->netdev;
	const struct firmware *fw;
	int err;

	if (flash->region != ETHTOOL_FLASH_ALL_REGIONS)
		return -EOPNOTSUPP;

	err = request_firmware_direct(&fw, flash->data, &dev->dev);
	if (err)
		return err;

	dev_hold(dev);
	rtnl_unlock();

	err = mlx5_firmware_flash(mdev, fw, NULL);
	release_firmware(fw);

	rtnl_lock();
	dev_put(dev);
	return err;
}

static int mlx5e_flash_device(struct net_device *dev,
			      struct ethtool_flash *flash)
{
	struct mlx5e_priv *priv = netdev_priv(dev);

	return mlx5e_ethtool_flash_device(priv, flash);
}

static int set_pflag_cqe_based_moder(struct net_device *netdev, bool enable,
				     bool is_rx_cq)
{
	struct mlx5e_priv *priv = netdev_priv(netdev);
	u8 cq_period_mode, current_cq_period_mode;
	struct mlx5e_params new_params;

	if (enable && !MLX5_CAP_GEN(priv->mdev, cq_period_start_from_cqe))
		return -EOPNOTSUPP;

	cq_period_mode = cqe_mode_to_period_mode(enable);

	current_cq_period_mode = is_rx_cq ?
		priv->channels.params.rx_cq_moderation.cq_period_mode :
		priv->channels.params.tx_cq_moderation.cq_period_mode;

	if (cq_period_mode == current_cq_period_mode)
		return 0;

	new_params = priv->channels.params;
	if (is_rx_cq)
		mlx5e_set_rx_cq_mode_params(&new_params, cq_period_mode);
	else
		mlx5e_set_tx_cq_mode_params(&new_params, cq_period_mode);

	return mlx5e_safe_switch_params(priv, &new_params, NULL, NULL, true);
}

static int set_pflag_tx_cqe_based_moder(struct net_device *netdev, bool enable)
{
	return set_pflag_cqe_based_moder(netdev, enable, false);
}

static int set_pflag_rx_cqe_based_moder(struct net_device *netdev, bool enable)
{
	return set_pflag_cqe_based_moder(netdev, enable, true);
}

int mlx5e_modify_rx_cqe_compression_locked(struct mlx5e_priv *priv, bool new_val, bool rx_filter)
{
	bool curr_val = MLX5E_GET_PFLAG(&priv->channels.params, MLX5E_PFLAG_RX_CQE_COMPRESS);
	struct mlx5e_params new_params;
	int err = 0;

	if (!MLX5_CAP_GEN(priv->mdev, cqe_compression))
		return new_val ? -EOPNOTSUPP : 0;

	if (curr_val == new_val)
		return 0;

	if (new_val && !mlx5e_profile_feature_cap(priv->profile, PTP_RX) && rx_filter) {
		netdev_err(priv->netdev,
			   "Profile doesn't support enabling of CQE compression while hardware time-stamping is enabled.\n");
		return -EINVAL;
	}

	if (priv->channels.params.packet_merge.type == MLX5E_PACKET_MERGE_SHAMPO) {
		netdev_warn(priv->netdev, "Can't set CQE compression with HW-GRO, disable it first.\n");
		return -EINVAL;
	}

	new_params = priv->channels.params;
	MLX5E_SET_PFLAG(&new_params, MLX5E_PFLAG_RX_CQE_COMPRESS, new_val);
	if (rx_filter)
		new_params.ptp_rx = new_val;

	if (new_params.ptp_rx == priv->channels.params.ptp_rx)
		err = mlx5e_safe_switch_params(priv, &new_params, NULL, NULL, true);
	else
		err = mlx5e_safe_switch_params(priv, &new_params, mlx5e_ptp_rx_manage_fs_ctx,
					       &new_params.ptp_rx, true);
	if (err)
		return err;

	netdev_dbg(priv->netdev, "MLX5E: RxCqeCmprss was turned %s\n",
		   MLX5E_GET_PFLAG(&priv->channels.params,
				   MLX5E_PFLAG_RX_CQE_COMPRESS) ? "ON" : "OFF");

	return 0;
}

static int set_pflag_rx_cqe_compress(struct net_device *netdev,
				     bool enable)
{
	struct mlx5e_priv *priv = netdev_priv(netdev);
	struct mlx5_core_dev *mdev = priv->mdev;
	bool rx_filter;
	int err;

	if (!MLX5_CAP_GEN(mdev, cqe_compression))
		return -EOPNOTSUPP;

	rx_filter = priv->tstamp.rx_filter != HWTSTAMP_FILTER_NONE;
	err = mlx5e_modify_rx_cqe_compression_locked(priv, enable, rx_filter);
	if (err)
		return err;

	priv->channels.params.rx_cqe_compress_def = enable;

	return 0;
}

static int set_pflag_rx_striding_rq(struct net_device *netdev, bool enable)
{
	struct mlx5e_priv *priv = netdev_priv(netdev);
	struct mlx5_core_dev *mdev = priv->mdev;
	struct mlx5e_params new_params;
	int err;

	if (enable) {
		/* Checking the regular RQ here; mlx5e_validate_xsk_param called
		 * from mlx5e_open_xsk will check for each XSK queue, and
		 * mlx5e_safe_switch_params will be reverted if any check fails.
		 */
		int err = mlx5e_mpwrq_validate_regular(mdev, &priv->channels.params);

		if (err)
			return err;
	} else if (priv->channels.params.packet_merge.type != MLX5E_PACKET_MERGE_NONE) {
		netdev_warn(netdev, "Can't set legacy RQ with HW-GRO/LRO, disable them first\n");
		return -EINVAL;
	}

	new_params = priv->channels.params;

	MLX5E_SET_PFLAG(&new_params, MLX5E_PFLAG_RX_STRIDING_RQ, enable);
	mlx5e_set_rq_type(mdev, &new_params);

	err = mlx5e_safe_switch_params(priv, &new_params, NULL, NULL, true);
	if (err)
		return err;

	/* update XDP supported features */
	mlx5e_set_xdp_feature(netdev);

	return 0;
}

static int set_pflag_rx_no_csum_complete(struct net_device *netdev, bool enable)
{
	struct mlx5e_priv *priv = netdev_priv(netdev);
	struct mlx5e_channels *channels = &priv->channels;
	struct mlx5e_channel *c;
	int i;

	if (!test_bit(MLX5E_STATE_OPENED, &priv->state) ||
	    priv->channels.params.xdp_prog)
		return 0;

	for (i = 0; i < channels->num; i++) {
		c = channels->c[i];
		if (enable)
			__set_bit(MLX5E_RQ_STATE_NO_CSUM_COMPLETE, &c->rq.state);
		else
			__clear_bit(MLX5E_RQ_STATE_NO_CSUM_COMPLETE, &c->rq.state);
	}

	return 0;
}

static int set_pflag_tx_mpwqe_common(struct net_device *netdev, u32 flag, bool enable)
{
	struct mlx5e_priv *priv = netdev_priv(netdev);
	struct mlx5_core_dev *mdev = priv->mdev;
	struct mlx5e_params new_params;

	if (enable && !mlx5e_tx_mpwqe_supported(mdev))
		return -EOPNOTSUPP;

	new_params = priv->channels.params;

	MLX5E_SET_PFLAG(&new_params, flag, enable);

	return mlx5e_safe_switch_params(priv, &new_params, NULL, NULL, true);
}

static int set_pflag_xdp_tx_mpwqe(struct net_device *netdev, bool enable)
{
	return set_pflag_tx_mpwqe_common(netdev, MLX5E_PFLAG_XDP_TX_MPWQE, enable);
}

static int set_pflag_skb_tx_mpwqe(struct net_device *netdev, bool enable)
{
	return set_pflag_tx_mpwqe_common(netdev, MLX5E_PFLAG_SKB_TX_MPWQE, enable);
}

static int set_pflag_tx_port_ts(struct net_device *netdev, bool enable)
{
	struct mlx5e_priv *priv = netdev_priv(netdev);
	struct mlx5_core_dev *mdev = priv->mdev;
	struct mlx5e_params new_params;
	int err;

	if (!MLX5_CAP_GEN(mdev, ts_cqe_to_dest_cqn) ||
	    !MLX5_CAP_GEN_2(mdev, ts_cqe_metadata_size2wqe_counter))
		return -EOPNOTSUPP;

	/* Don't allow changing the PTP state if HTB offload is active, because
	 * the numeration of the QoS SQs will change, while per-queue qdiscs are
	 * attached.
	 */
	if (mlx5e_selq_is_htb_enabled(&priv->selq)) {
		netdev_err(priv->netdev, "%s: HTB offload is active, cannot change the PTP state\n",
			   __func__);
		return -EINVAL;
	}

	new_params = priv->channels.params;
	/* Don't allow enabling TX-port-TS if MQPRIO mode channel  offload is
	 * active, since it defines explicitly which TC accepts the packet.
	 * This conflicts with TX-port-TS hijacking the PTP traffic to a specific
	 * HW TX-queue.
	 */
	if (enable && new_params.mqprio.mode == TC_MQPRIO_MODE_CHANNEL) {
		netdev_err(priv->netdev,
			   "%s: MQPRIO mode channel offload is active, cannot set the TX-port-TS\n",
			   __func__);
		return -EINVAL;
	}
	MLX5E_SET_PFLAG(&new_params, MLX5E_PFLAG_TX_PORT_TS, enable);
	/* No need to verify SQ stop room as
	 * ptpsq.txqsq.stop_room <= generic_sq->stop_room, and both
	 * has the same log_sq_size.
	 */

	err = mlx5e_safe_switch_params(priv, &new_params,
				       mlx5e_num_channels_changed_ctx, NULL, true);
	if (!err)
		priv->tx_ptp_opened = true;

	return err;
}

static const struct pflag_desc mlx5e_priv_flags[MLX5E_NUM_PFLAGS] = {
	{ "rx_cqe_moder",        set_pflag_rx_cqe_based_moder },
	{ "tx_cqe_moder",        set_pflag_tx_cqe_based_moder },
	{ "rx_cqe_compress",     set_pflag_rx_cqe_compress },
	{ "rx_striding_rq",      set_pflag_rx_striding_rq },
	{ "rx_no_csum_complete", set_pflag_rx_no_csum_complete },
	{ "xdp_tx_mpwqe",        set_pflag_xdp_tx_mpwqe },
	{ "skb_tx_mpwqe",        set_pflag_skb_tx_mpwqe },
	{ "tx_port_ts",          set_pflag_tx_port_ts },
};

static int mlx5e_handle_pflag(struct net_device *netdev,
			      u32 wanted_flags,
			      enum mlx5e_priv_flag flag)
{
	struct mlx5e_priv *priv = netdev_priv(netdev);
	bool enable = !!(wanted_flags & BIT(flag));
	u32 changes = wanted_flags ^ priv->channels.params.pflags;
	int err;

	if (!(changes & BIT(flag)))
		return 0;

	err = mlx5e_priv_flags[flag].handler(netdev, enable);
	if (err) {
		netdev_err(netdev, "%s private flag '%s' failed err %d\n",
			   enable ? "Enable" : "Disable", mlx5e_priv_flags[flag].name, err);
		return err;
	}

	MLX5E_SET_PFLAG(&priv->channels.params, flag, enable);
	return 0;
}

static int mlx5e_set_priv_flags(struct net_device *netdev, u32 pflags)
{
	struct mlx5e_priv *priv = netdev_priv(netdev);
	enum mlx5e_priv_flag pflag;
	int err;

	mutex_lock(&priv->state_lock);

	for (pflag = 0; pflag < MLX5E_NUM_PFLAGS; pflag++) {
		err = mlx5e_handle_pflag(netdev, pflags, pflag);
		if (err)
			break;
	}

	mutex_unlock(&priv->state_lock);

	/* Need to fix some features.. */
	netdev_update_features(netdev);

	return err;
}

static u32 mlx5e_get_priv_flags(struct net_device *netdev)
{
	struct mlx5e_priv *priv = netdev_priv(netdev);

	return priv->channels.params.pflags;
}

static int mlx5e_get_rxnfc(struct net_device *dev, struct ethtool_rxnfc *info,
			   u32 *rule_locs)
{
	struct mlx5e_priv *priv = netdev_priv(dev);

	/* ETHTOOL_GRXRINGS is needed by ethtool -x which is not part
	 * of rxnfc. We keep this logic out of mlx5e_ethtool_get_rxnfc,
	 * to avoid breaking "ethtool -x" when mlx5e_ethtool_get_rxnfc
	 * is compiled out via CONFIG_MLX5_EN_RXNFC=n.
	 */
	if (info->cmd == ETHTOOL_GRXRINGS) {
		info->data = priv->channels.params.num_channels;
		return 0;
	}

	return mlx5e_ethtool_get_rxnfc(priv, info, rule_locs);
}

static int mlx5e_set_rxnfc(struct net_device *dev, struct ethtool_rxnfc *cmd)
{
	struct mlx5e_priv *priv = netdev_priv(dev);

	return mlx5e_ethtool_set_rxnfc(priv, cmd);
}

static int query_port_status_opcode(struct mlx5_core_dev *mdev, u32 *status_opcode)
{
	struct mlx5_ifc_pddr_troubleshooting_page_bits *pddr_troubleshooting_page;
	u32 in[MLX5_ST_SZ_DW(pddr_reg)] = {};
	u32 out[MLX5_ST_SZ_DW(pddr_reg)];
	int err;

	MLX5_SET(pddr_reg, in, local_port, 1);
	MLX5_SET(pddr_reg, in, page_select,
		 MLX5_PDDR_REG_PAGE_SELECT_TROUBLESHOOTING_INFO_PAGE);

	pddr_troubleshooting_page = MLX5_ADDR_OF(pddr_reg, in, page_data);
	MLX5_SET(pddr_troubleshooting_page, pddr_troubleshooting_page,
		 group_opcode, MLX5_PDDR_REG_TRBLSH_GROUP_OPCODE_MONITOR);
	err = mlx5_core_access_reg(mdev, in, sizeof(in), out,
				   sizeof(out), MLX5_REG_PDDR, 0, 0);
	if (err)
		return err;

	pddr_troubleshooting_page = MLX5_ADDR_OF(pddr_reg, out, page_data);
	*status_opcode = MLX5_GET(pddr_troubleshooting_page, pddr_troubleshooting_page,
				  status_opcode);
	return 0;
}

struct mlx5e_ethtool_link_ext_state_opcode_mapping {
	u32 status_opcode;
	enum ethtool_link_ext_state link_ext_state;
	u8 link_ext_substate;
};

static const struct mlx5e_ethtool_link_ext_state_opcode_mapping
mlx5e_link_ext_state_opcode_map[] = {
	/* States relating to the autonegotiation or issues therein */
	{2, ETHTOOL_LINK_EXT_STATE_AUTONEG,
		ETHTOOL_LINK_EXT_SUBSTATE_AN_NO_PARTNER_DETECTED},
	{3, ETHTOOL_LINK_EXT_STATE_AUTONEG,
		ETHTOOL_LINK_EXT_SUBSTATE_AN_ACK_NOT_RECEIVED},
	{4, ETHTOOL_LINK_EXT_STATE_AUTONEG,
		ETHTOOL_LINK_EXT_SUBSTATE_AN_NEXT_PAGE_EXCHANGE_FAILED},
	{36, ETHTOOL_LINK_EXT_STATE_AUTONEG,
		ETHTOOL_LINK_EXT_SUBSTATE_AN_NO_PARTNER_DETECTED_FORCE_MODE},
	{38, ETHTOOL_LINK_EXT_STATE_AUTONEG,
		ETHTOOL_LINK_EXT_SUBSTATE_AN_FEC_MISMATCH_DURING_OVERRIDE},
	{39, ETHTOOL_LINK_EXT_STATE_AUTONEG,
		ETHTOOL_LINK_EXT_SUBSTATE_AN_NO_HCD},

	/* Failure during link training */
	{5, ETHTOOL_LINK_EXT_STATE_LINK_TRAINING_FAILURE,
		ETHTOOL_LINK_EXT_SUBSTATE_LT_KR_FRAME_LOCK_NOT_ACQUIRED},
	{6, ETHTOOL_LINK_EXT_STATE_LINK_TRAINING_FAILURE,
		ETHTOOL_LINK_EXT_SUBSTATE_LT_KR_LINK_INHIBIT_TIMEOUT},
	{7, ETHTOOL_LINK_EXT_STATE_LINK_TRAINING_FAILURE,
		ETHTOOL_LINK_EXT_SUBSTATE_LT_KR_LINK_PARTNER_DID_NOT_SET_RECEIVER_READY},
	{8, ETHTOOL_LINK_EXT_STATE_LINK_TRAINING_FAILURE, 0},
	{14, ETHTOOL_LINK_EXT_STATE_LINK_TRAINING_FAILURE,
		ETHTOOL_LINK_EXT_SUBSTATE_LT_REMOTE_FAULT},

	/* Logical mismatch in physical coding sublayer or forward error correction sublayer */
	{9, ETHTOOL_LINK_EXT_STATE_LINK_LOGICAL_MISMATCH,
		ETHTOOL_LINK_EXT_SUBSTATE_LLM_PCS_DID_NOT_ACQUIRE_BLOCK_LOCK},
	{10, ETHTOOL_LINK_EXT_STATE_LINK_LOGICAL_MISMATCH,
		ETHTOOL_LINK_EXT_SUBSTATE_LLM_PCS_DID_NOT_ACQUIRE_AM_LOCK},
	{11, ETHTOOL_LINK_EXT_STATE_LINK_LOGICAL_MISMATCH,
		ETHTOOL_LINK_EXT_SUBSTATE_LLM_PCS_DID_NOT_GET_ALIGN_STATUS},
	{12, ETHTOOL_LINK_EXT_STATE_LINK_LOGICAL_MISMATCH,
		ETHTOOL_LINK_EXT_SUBSTATE_LLM_FC_FEC_IS_NOT_LOCKED},
	{13, ETHTOOL_LINK_EXT_STATE_LINK_LOGICAL_MISMATCH,
		ETHTOOL_LINK_EXT_SUBSTATE_LLM_RS_FEC_IS_NOT_LOCKED},

	/* Signal integrity issues */
	{15, ETHTOOL_LINK_EXT_STATE_BAD_SIGNAL_INTEGRITY, 0},
	{17, ETHTOOL_LINK_EXT_STATE_BAD_SIGNAL_INTEGRITY,
		ETHTOOL_LINK_EXT_SUBSTATE_BSI_LARGE_NUMBER_OF_PHYSICAL_ERRORS},
	{42, ETHTOOL_LINK_EXT_STATE_BAD_SIGNAL_INTEGRITY,
		ETHTOOL_LINK_EXT_SUBSTATE_BSI_UNSUPPORTED_RATE},

	/* No cable connected */
	{1024, ETHTOOL_LINK_EXT_STATE_NO_CABLE, 0},

	/* Failure is related to cable, e.g., unsupported cable */
	{16, ETHTOOL_LINK_EXT_STATE_CABLE_ISSUE,
		ETHTOOL_LINK_EXT_SUBSTATE_CI_UNSUPPORTED_CABLE},
	{20, ETHTOOL_LINK_EXT_STATE_CABLE_ISSUE,
		ETHTOOL_LINK_EXT_SUBSTATE_CI_UNSUPPORTED_CABLE},
	{29, ETHTOOL_LINK_EXT_STATE_CABLE_ISSUE,
		ETHTOOL_LINK_EXT_SUBSTATE_CI_UNSUPPORTED_CABLE},
	{1025, ETHTOOL_LINK_EXT_STATE_CABLE_ISSUE,
		ETHTOOL_LINK_EXT_SUBSTATE_CI_UNSUPPORTED_CABLE},
	{1029, ETHTOOL_LINK_EXT_STATE_CABLE_ISSUE,
		ETHTOOL_LINK_EXT_SUBSTATE_CI_UNSUPPORTED_CABLE},
	{1031, ETHTOOL_LINK_EXT_STATE_CABLE_ISSUE, 0},

	/* Failure is related to EEPROM, e.g., failure during reading or parsing the data */
	{1027, ETHTOOL_LINK_EXT_STATE_EEPROM_ISSUE, 0},

	/* Failure during calibration algorithm */
	{23, ETHTOOL_LINK_EXT_STATE_CALIBRATION_FAILURE, 0},

	/* The hardware is not able to provide the power required from cable or module */
	{1032, ETHTOOL_LINK_EXT_STATE_POWER_BUDGET_EXCEEDED, 0},

	/* The module is overheated */
	{1030, ETHTOOL_LINK_EXT_STATE_OVERHEAT, 0},
};

static void
mlx5e_set_link_ext_state(struct mlx5e_ethtool_link_ext_state_opcode_mapping
			 link_ext_state_mapping,
			 struct ethtool_link_ext_state_info *link_ext_state_info)
{
	switch (link_ext_state_mapping.link_ext_state) {
	case ETHTOOL_LINK_EXT_STATE_AUTONEG:
		link_ext_state_info->autoneg =
			link_ext_state_mapping.link_ext_substate;
		break;
	case ETHTOOL_LINK_EXT_STATE_LINK_TRAINING_FAILURE:
		link_ext_state_info->link_training =
			link_ext_state_mapping.link_ext_substate;
		break;
	case ETHTOOL_LINK_EXT_STATE_LINK_LOGICAL_MISMATCH:
		link_ext_state_info->link_logical_mismatch =
			link_ext_state_mapping.link_ext_substate;
		break;
	case ETHTOOL_LINK_EXT_STATE_BAD_SIGNAL_INTEGRITY:
		link_ext_state_info->bad_signal_integrity =
			link_ext_state_mapping.link_ext_substate;
		break;
	case ETHTOOL_LINK_EXT_STATE_CABLE_ISSUE:
		link_ext_state_info->cable_issue =
			link_ext_state_mapping.link_ext_substate;
		break;
	default:
		break;
	}

	link_ext_state_info->link_ext_state = link_ext_state_mapping.link_ext_state;
}

static int
mlx5e_get_link_ext_state(struct net_device *dev,
			 struct ethtool_link_ext_state_info *link_ext_state_info)
{
	struct mlx5e_ethtool_link_ext_state_opcode_mapping link_ext_state_mapping;
	struct mlx5e_priv *priv = netdev_priv(dev);
	u32 status_opcode = 0;
	int i;

	/* Exit without data if the interface state is OK, since no extended data is
	 * available in such case
	 */
	if (netif_carrier_ok(dev))
		return -ENODATA;

	if (query_port_status_opcode(priv->mdev, &status_opcode) ||
	    !status_opcode)
		return -ENODATA;

	for (i = 0; i < ARRAY_SIZE(mlx5e_link_ext_state_opcode_map); i++) {
		link_ext_state_mapping = mlx5e_link_ext_state_opcode_map[i];
		if (link_ext_state_mapping.status_opcode == status_opcode) {
			mlx5e_set_link_ext_state(link_ext_state_mapping,
						 link_ext_state_info);
			return 0;
		}
	}

	return -ENODATA;
}

static void mlx5e_get_eth_phy_stats(struct net_device *netdev,
				    struct ethtool_eth_phy_stats *phy_stats)
{
	struct mlx5e_priv *priv = netdev_priv(netdev);

	mlx5e_stats_eth_phy_get(priv, phy_stats);
}

static void mlx5e_get_eth_mac_stats(struct net_device *netdev,
				    struct ethtool_eth_mac_stats *mac_stats)
{
	struct mlx5e_priv *priv = netdev_priv(netdev);

	mlx5e_stats_eth_mac_get(priv, mac_stats);
}

static void mlx5e_get_eth_ctrl_stats(struct net_device *netdev,
				     struct ethtool_eth_ctrl_stats *ctrl_stats)
{
	struct mlx5e_priv *priv = netdev_priv(netdev);

	mlx5e_stats_eth_ctrl_get(priv, ctrl_stats);
}

static void mlx5e_get_rmon_stats(struct net_device *netdev,
				 struct ethtool_rmon_stats *rmon_stats,
				 const struct ethtool_rmon_hist_range **ranges)
{
	struct mlx5e_priv *priv = netdev_priv(netdev);

	mlx5e_stats_rmon_get(priv, rmon_stats, ranges);
}

const struct ethtool_ops mlx5e_ethtool_ops = {
	.supported_coalesce_params = ETHTOOL_COALESCE_USECS |
				     ETHTOOL_COALESCE_MAX_FRAMES |
				     ETHTOOL_COALESCE_USE_ADAPTIVE |
				     ETHTOOL_COALESCE_USE_CQE,
	.get_drvinfo       = mlx5e_get_drvinfo,
	.get_link          = ethtool_op_get_link,
	.get_link_ext_state  = mlx5e_get_link_ext_state,
	.get_strings       = mlx5e_get_strings,
	.get_sset_count    = mlx5e_get_sset_count,
	.get_ethtool_stats = mlx5e_get_ethtool_stats,
	.get_ringparam     = mlx5e_get_ringparam,
	.set_ringparam     = mlx5e_set_ringparam,
	.get_channels      = mlx5e_get_channels,
	.set_channels      = mlx5e_set_channels,
	.get_coalesce      = mlx5e_get_coalesce,
	.set_coalesce      = mlx5e_set_coalesce,
	.get_link_ksettings  = mlx5e_get_link_ksettings,
	.set_link_ksettings  = mlx5e_set_link_ksettings,
	.get_rxfh_key_size   = mlx5e_get_rxfh_key_size,
	.get_rxfh_indir_size = mlx5e_get_rxfh_indir_size,
	.get_rxfh          = mlx5e_get_rxfh,
	.set_rxfh          = mlx5e_set_rxfh,
	.get_rxfh_context  = mlx5e_get_rxfh_context,
	.set_rxfh_context  = mlx5e_set_rxfh_context,
	.get_rxnfc         = mlx5e_get_rxnfc,
	.set_rxnfc         = mlx5e_set_rxnfc,
	.get_tunable       = mlx5e_get_tunable,
	.set_tunable       = mlx5e_set_tunable,
	.get_pause_stats   = mlx5e_get_pause_stats,
	.get_pauseparam    = mlx5e_get_pauseparam,
	.set_pauseparam    = mlx5e_set_pauseparam,
	.get_ts_info       = mlx5e_get_ts_info,
	.set_phys_id       = mlx5e_set_phys_id,
	.get_wol	   = mlx5e_get_wol,
	.set_wol	   = mlx5e_set_wol,
	.get_module_info   = mlx5e_get_module_info,
	.get_module_eeprom = mlx5e_get_module_eeprom,
	.get_module_eeprom_by_page = mlx5e_get_module_eeprom_by_page,
	.flash_device      = mlx5e_flash_device,
	.get_priv_flags    = mlx5e_get_priv_flags,
	.set_priv_flags    = mlx5e_set_priv_flags,
	.self_test         = mlx5e_self_test,
	.get_fec_stats     = mlx5e_get_fec_stats,
	.get_fecparam      = mlx5e_get_fecparam,
	.set_fecparam      = mlx5e_set_fecparam,
	.get_eth_phy_stats = mlx5e_get_eth_phy_stats,
	.get_eth_mac_stats = mlx5e_get_eth_mac_stats,
	.get_eth_ctrl_stats = mlx5e_get_eth_ctrl_stats,
	.get_rmon_stats    = mlx5e_get_rmon_stats,
	.get_link_ext_stats = mlx5e_get_link_ext_stats
};
