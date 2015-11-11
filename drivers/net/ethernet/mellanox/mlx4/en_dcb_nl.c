/*
 * Copyright (c) 2011 Mellanox Technologies. All rights reserved.
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

#include <linux/dcbnl.h>
#include <linux/math64.h>

#include "mlx4_en.h"
#include "fw_qos.h"

/* Definitions for QCN
 */

struct mlx4_congestion_control_mb_prio_802_1_qau_params {
	__be32 modify_enable_high;
	__be32 modify_enable_low;
	__be32 reserved1;
	__be32 extended_enable;
	__be32 rppp_max_rps;
	__be32 rpg_time_reset;
	__be32 rpg_byte_reset;
	__be32 rpg_threshold;
	__be32 rpg_max_rate;
	__be32 rpg_ai_rate;
	__be32 rpg_hai_rate;
	__be32 rpg_gd;
	__be32 rpg_min_dec_fac;
	__be32 rpg_min_rate;
	__be32 max_time_rise;
	__be32 max_byte_rise;
	__be32 max_qdelta;
	__be32 min_qoffset;
	__be32 gd_coefficient;
	__be32 reserved2[5];
	__be32 cp_sample_base;
	__be32 reserved3[39];
};

struct mlx4_congestion_control_mb_prio_802_1_qau_statistics {
	__be64 rppp_rp_centiseconds;
	__be32 reserved1;
	__be32 ignored_cnm;
	__be32 rppp_created_rps;
	__be32 estimated_total_rate;
	__be32 max_active_rate_limiter_index;
	__be32 dropped_cnms_busy_fw;
	__be32 reserved2;
	__be32 cnms_handled_successfully;
	__be32 min_total_limiters_rate;
	__be32 max_total_limiters_rate;
	__be32 reserved3[4];
};

static int mlx4_en_dcbnl_ieee_getets(struct net_device *dev,
				   struct ieee_ets *ets)
{
	struct mlx4_en_priv *priv = netdev_priv(dev);
	struct ieee_ets *my_ets = &priv->ets;

	/* No IEEE PFC settings available */
	if (!my_ets)
		return -EINVAL;

	ets->ets_cap = IEEE_8021QAZ_MAX_TCS;
	ets->cbs = my_ets->cbs;
	memcpy(ets->tc_tx_bw, my_ets->tc_tx_bw, sizeof(ets->tc_tx_bw));
	memcpy(ets->tc_tsa, my_ets->tc_tsa, sizeof(ets->tc_tsa));
	memcpy(ets->prio_tc, my_ets->prio_tc, sizeof(ets->prio_tc));

	return 0;
}

static int mlx4_en_ets_validate(struct mlx4_en_priv *priv, struct ieee_ets *ets)
{
	int i;
	int total_ets_bw = 0;
	int has_ets_tc = 0;

	for (i = 0; i < IEEE_8021QAZ_MAX_TCS; i++) {
		if (ets->prio_tc[i] >= MLX4_EN_NUM_UP) {
			en_err(priv, "Bad priority in UP <=> TC mapping. TC: %d, UP: %d\n",
					i, ets->prio_tc[i]);
			return -EINVAL;
		}

		switch (ets->tc_tsa[i]) {
		case IEEE_8021QAZ_TSA_STRICT:
			break;
		case IEEE_8021QAZ_TSA_ETS:
			has_ets_tc = 1;
			total_ets_bw += ets->tc_tx_bw[i];
			break;
		default:
			en_err(priv, "TC[%d]: Not supported TSA: %d\n",
					i, ets->tc_tsa[i]);
			return -ENOTSUPP;
		}
	}

	if (has_ets_tc && total_ets_bw != MLX4_EN_BW_MAX) {
		en_err(priv, "Bad ETS BW sum: %d. Should be exactly 100%%\n",
				total_ets_bw);
		return -EINVAL;
	}

	return 0;
}

static int mlx4_en_config_port_scheduler(struct mlx4_en_priv *priv,
		struct ieee_ets *ets, u16 *ratelimit)
{
	struct mlx4_en_dev *mdev = priv->mdev;
	int num_strict = 0;
	int i;
	__u8 tc_tx_bw[IEEE_8021QAZ_MAX_TCS] = { 0 };
	__u8 pg[IEEE_8021QAZ_MAX_TCS] = { 0 };

	ets = ets ?: &priv->ets;
	ratelimit = ratelimit ?: priv->maxrate;

	/* higher TC means higher priority => lower pg */
	for (i = IEEE_8021QAZ_MAX_TCS - 1; i >= 0; i--) {
		switch (ets->tc_tsa[i]) {
		case IEEE_8021QAZ_TSA_STRICT:
			pg[i] = num_strict++;
			tc_tx_bw[i] = MLX4_EN_BW_MAX;
			break;
		case IEEE_8021QAZ_TSA_ETS:
			pg[i] = MLX4_EN_TC_ETS;
			tc_tx_bw[i] = ets->tc_tx_bw[i] ?: MLX4_EN_BW_MIN;
			break;
		}
	}

	return mlx4_SET_PORT_SCHEDULER(mdev->dev, priv->port, tc_tx_bw, pg,
			ratelimit);
}

static int
mlx4_en_dcbnl_ieee_setets(struct net_device *dev, struct ieee_ets *ets)
{
	struct mlx4_en_priv *priv = netdev_priv(dev);
	struct mlx4_en_dev *mdev = priv->mdev;
	int err;

	err = mlx4_en_ets_validate(priv, ets);
	if (err)
		return err;

	err = mlx4_SET_PORT_PRIO2TC(mdev->dev, priv->port, ets->prio_tc);
	if (err)
		return err;

	err = mlx4_en_config_port_scheduler(priv, ets, NULL);
	if (err)
		return err;

	memcpy(&priv->ets, ets, sizeof(priv->ets));

	return 0;
}

static int mlx4_en_dcbnl_ieee_getpfc(struct net_device *dev,
		struct ieee_pfc *pfc)
{
	struct mlx4_en_priv *priv = netdev_priv(dev);

	pfc->pfc_cap = IEEE_8021QAZ_MAX_TCS;
	pfc->pfc_en = priv->prof->tx_ppp;

	return 0;
}

static int mlx4_en_dcbnl_ieee_setpfc(struct net_device *dev,
		struct ieee_pfc *pfc)
{
	struct mlx4_en_priv *priv = netdev_priv(dev);
	struct mlx4_en_port_profile *prof = priv->prof;
	struct mlx4_en_dev *mdev = priv->mdev;
	int err;

	en_dbg(DRV, priv, "cap: 0x%x en: 0x%x mbc: 0x%x delay: %d\n",
			pfc->pfc_cap,
			pfc->pfc_en,
			pfc->mbc,
			pfc->delay);

	prof->rx_pause = !pfc->pfc_en;
	prof->tx_pause = !pfc->pfc_en;
	prof->rx_ppp = pfc->pfc_en;
	prof->tx_ppp = pfc->pfc_en;

	err = mlx4_SET_PORT_general(mdev->dev, priv->port,
				    priv->rx_skb_size + ETH_FCS_LEN,
				    prof->tx_pause,
				    prof->tx_ppp,
				    prof->rx_pause,
				    prof->rx_ppp);
	if (err)
		en_err(priv, "Failed setting pause params\n");
	else
		mlx4_en_update_pfc_stats_bitmap(mdev->dev, &priv->stats_bitmap,
						prof->rx_ppp, prof->rx_pause,
						prof->tx_ppp, prof->tx_pause);

	return err;
}

static u8 mlx4_en_dcbnl_getdcbx(struct net_device *dev)
{
	return DCB_CAP_DCBX_HOST | DCB_CAP_DCBX_VER_IEEE;
}

static u8 mlx4_en_dcbnl_setdcbx(struct net_device *dev, u8 mode)
{
	if ((mode & DCB_CAP_DCBX_LLD_MANAGED) ||
	    (mode & DCB_CAP_DCBX_VER_CEE) ||
	    !(mode & DCB_CAP_DCBX_VER_IEEE) ||
	    !(mode & DCB_CAP_DCBX_HOST))
		return 1;

	return 0;
}

#define MLX4_RATELIMIT_UNITS_IN_KB 100000 /* rate-limit HW unit in Kbps */
static int mlx4_en_dcbnl_ieee_getmaxrate(struct net_device *dev,
				   struct ieee_maxrate *maxrate)
{
	struct mlx4_en_priv *priv = netdev_priv(dev);
	int i;

	for (i = 0; i < IEEE_8021QAZ_MAX_TCS; i++)
		maxrate->tc_maxrate[i] =
			priv->maxrate[i] * MLX4_RATELIMIT_UNITS_IN_KB;

	return 0;
}

static int mlx4_en_dcbnl_ieee_setmaxrate(struct net_device *dev,
		struct ieee_maxrate *maxrate)
{
	struct mlx4_en_priv *priv = netdev_priv(dev);
	u16 tmp[IEEE_8021QAZ_MAX_TCS];
	int i, err;

	for (i = 0; i < IEEE_8021QAZ_MAX_TCS; i++) {
		/* Convert from Kbps into HW units, rounding result up.
		 * Setting to 0, means unlimited BW.
		 */
		tmp[i] = div_u64(maxrate->tc_maxrate[i] +
				 MLX4_RATELIMIT_UNITS_IN_KB - 1,
				 MLX4_RATELIMIT_UNITS_IN_KB);
	}

	err = mlx4_en_config_port_scheduler(priv, NULL, tmp);
	if (err)
		return err;

	memcpy(priv->maxrate, tmp, sizeof(priv->maxrate));

	return 0;
}

#define RPG_ENABLE_BIT	31
#define CN_TAG_BIT	30

static int mlx4_en_dcbnl_ieee_getqcn(struct net_device *dev,
				     struct ieee_qcn *qcn)
{
	struct mlx4_en_priv *priv = netdev_priv(dev);
	struct mlx4_congestion_control_mb_prio_802_1_qau_params *hw_qcn;
	struct mlx4_cmd_mailbox *mailbox_out = NULL;
	u64 mailbox_in_dma = 0;
	u32 inmod = 0;
	int i, err;

	if (!(priv->mdev->dev->caps.flags2 & MLX4_DEV_CAP_FLAG2_QCN))
		return -EOPNOTSUPP;

	mailbox_out = mlx4_alloc_cmd_mailbox(priv->mdev->dev);
	if (IS_ERR(mailbox_out))
		return -ENOMEM;
	hw_qcn =
	(struct mlx4_congestion_control_mb_prio_802_1_qau_params *)
	mailbox_out->buf;

	for (i = 0; i < IEEE_8021QAZ_MAX_TCS; i++) {
		inmod = priv->port | ((1 << i) << 8) |
			 (MLX4_CTRL_ALGO_802_1_QAU_REACTION_POINT << 16);
		err = mlx4_cmd_box(priv->mdev->dev, mailbox_in_dma,
				   mailbox_out->dma,
				   inmod, MLX4_CONGESTION_CONTROL_GET_PARAMS,
				   MLX4_CMD_CONGESTION_CTRL_OPCODE,
				   MLX4_CMD_TIME_CLASS_C,
				   MLX4_CMD_NATIVE);
		if (err) {
			mlx4_free_cmd_mailbox(priv->mdev->dev, mailbox_out);
			return err;
		}

		qcn->rpg_enable[i] =
			be32_to_cpu(hw_qcn->extended_enable) >> RPG_ENABLE_BIT;
		qcn->rppp_max_rps[i] =
			be32_to_cpu(hw_qcn->rppp_max_rps);
		qcn->rpg_time_reset[i] =
			be32_to_cpu(hw_qcn->rpg_time_reset);
		qcn->rpg_byte_reset[i] =
			be32_to_cpu(hw_qcn->rpg_byte_reset);
		qcn->rpg_threshold[i] =
			be32_to_cpu(hw_qcn->rpg_threshold);
		qcn->rpg_max_rate[i] =
			be32_to_cpu(hw_qcn->rpg_max_rate);
		qcn->rpg_ai_rate[i] =
			be32_to_cpu(hw_qcn->rpg_ai_rate);
		qcn->rpg_hai_rate[i] =
			be32_to_cpu(hw_qcn->rpg_hai_rate);
		qcn->rpg_gd[i] =
			be32_to_cpu(hw_qcn->rpg_gd);
		qcn->rpg_min_dec_fac[i] =
			be32_to_cpu(hw_qcn->rpg_min_dec_fac);
		qcn->rpg_min_rate[i] =
			be32_to_cpu(hw_qcn->rpg_min_rate);
		qcn->cndd_state_machine[i] =
			priv->cndd_state[i];
	}
	mlx4_free_cmd_mailbox(priv->mdev->dev, mailbox_out);
	return 0;
}

static int mlx4_en_dcbnl_ieee_setqcn(struct net_device *dev,
				     struct ieee_qcn *qcn)
{
	struct mlx4_en_priv *priv = netdev_priv(dev);
	struct mlx4_congestion_control_mb_prio_802_1_qau_params *hw_qcn;
	struct mlx4_cmd_mailbox *mailbox_in = NULL;
	u64 mailbox_in_dma = 0;
	u32 inmod = 0;
	int i, err;
#define MODIFY_ENABLE_HIGH_MASK 0xc0000000
#define MODIFY_ENABLE_LOW_MASK 0xffc00000

	if (!(priv->mdev->dev->caps.flags2 & MLX4_DEV_CAP_FLAG2_QCN))
		return -EOPNOTSUPP;

	mailbox_in = mlx4_alloc_cmd_mailbox(priv->mdev->dev);
	if (IS_ERR(mailbox_in))
		return -ENOMEM;

	mailbox_in_dma = mailbox_in->dma;
	hw_qcn =
	(struct mlx4_congestion_control_mb_prio_802_1_qau_params *)mailbox_in->buf;
	for (i = 0; i < IEEE_8021QAZ_MAX_TCS; i++) {
		inmod = priv->port | ((1 << i) << 8) |
			 (MLX4_CTRL_ALGO_802_1_QAU_REACTION_POINT << 16);

		/* Before updating QCN parameter,
		 * need to set it's modify enable bit to 1
		 */

		hw_qcn->modify_enable_high = cpu_to_be32(
						MODIFY_ENABLE_HIGH_MASK);
		hw_qcn->modify_enable_low = cpu_to_be32(MODIFY_ENABLE_LOW_MASK);

		hw_qcn->extended_enable = cpu_to_be32(qcn->rpg_enable[i] << RPG_ENABLE_BIT);
		hw_qcn->rppp_max_rps = cpu_to_be32(qcn->rppp_max_rps[i]);
		hw_qcn->rpg_time_reset = cpu_to_be32(qcn->rpg_time_reset[i]);
		hw_qcn->rpg_byte_reset = cpu_to_be32(qcn->rpg_byte_reset[i]);
		hw_qcn->rpg_threshold = cpu_to_be32(qcn->rpg_threshold[i]);
		hw_qcn->rpg_max_rate = cpu_to_be32(qcn->rpg_max_rate[i]);
		hw_qcn->rpg_ai_rate = cpu_to_be32(qcn->rpg_ai_rate[i]);
		hw_qcn->rpg_hai_rate = cpu_to_be32(qcn->rpg_hai_rate[i]);
		hw_qcn->rpg_gd = cpu_to_be32(qcn->rpg_gd[i]);
		hw_qcn->rpg_min_dec_fac = cpu_to_be32(qcn->rpg_min_dec_fac[i]);
		hw_qcn->rpg_min_rate = cpu_to_be32(qcn->rpg_min_rate[i]);
		priv->cndd_state[i] = qcn->cndd_state_machine[i];
		if (qcn->cndd_state_machine[i] == DCB_CNDD_INTERIOR_READY)
			hw_qcn->extended_enable |= cpu_to_be32(1 << CN_TAG_BIT);

		err = mlx4_cmd(priv->mdev->dev, mailbox_in_dma, inmod,
			       MLX4_CONGESTION_CONTROL_SET_PARAMS,
			       MLX4_CMD_CONGESTION_CTRL_OPCODE,
			       MLX4_CMD_TIME_CLASS_C,
			       MLX4_CMD_NATIVE);
		if (err) {
			mlx4_free_cmd_mailbox(priv->mdev->dev, mailbox_in);
			return err;
		}
	}
	mlx4_free_cmd_mailbox(priv->mdev->dev, mailbox_in);
	return 0;
}

static int mlx4_en_dcbnl_ieee_getqcnstats(struct net_device *dev,
					  struct ieee_qcn_stats *qcn_stats)
{
	struct mlx4_en_priv *priv = netdev_priv(dev);
	struct mlx4_congestion_control_mb_prio_802_1_qau_statistics *hw_qcn_stats;
	struct mlx4_cmd_mailbox *mailbox_out = NULL;
	u64 mailbox_in_dma = 0;
	u32 inmod = 0;
	int i, err;

	if (!(priv->mdev->dev->caps.flags2 & MLX4_DEV_CAP_FLAG2_QCN))
		return -EOPNOTSUPP;

	mailbox_out = mlx4_alloc_cmd_mailbox(priv->mdev->dev);
	if (IS_ERR(mailbox_out))
		return -ENOMEM;

	hw_qcn_stats =
	(struct mlx4_congestion_control_mb_prio_802_1_qau_statistics *)
	mailbox_out->buf;

	for (i = 0; i < IEEE_8021QAZ_MAX_TCS; i++) {
		inmod = priv->port | ((1 << i) << 8) |
			 (MLX4_CTRL_ALGO_802_1_QAU_REACTION_POINT << 16);
		err = mlx4_cmd_box(priv->mdev->dev, mailbox_in_dma,
				   mailbox_out->dma, inmod,
				   MLX4_CONGESTION_CONTROL_GET_STATISTICS,
				   MLX4_CMD_CONGESTION_CTRL_OPCODE,
				   MLX4_CMD_TIME_CLASS_C,
				   MLX4_CMD_NATIVE);
		if (err) {
			mlx4_free_cmd_mailbox(priv->mdev->dev, mailbox_out);
			return err;
		}
		qcn_stats->rppp_rp_centiseconds[i] =
			be64_to_cpu(hw_qcn_stats->rppp_rp_centiseconds);
		qcn_stats->rppp_created_rps[i] =
			be32_to_cpu(hw_qcn_stats->rppp_created_rps);
	}
	mlx4_free_cmd_mailbox(priv->mdev->dev, mailbox_out);
	return 0;
}

const struct dcbnl_rtnl_ops mlx4_en_dcbnl_ops = {
	.ieee_getets	= mlx4_en_dcbnl_ieee_getets,
	.ieee_setets	= mlx4_en_dcbnl_ieee_setets,
	.ieee_getmaxrate = mlx4_en_dcbnl_ieee_getmaxrate,
	.ieee_setmaxrate = mlx4_en_dcbnl_ieee_setmaxrate,
	.ieee_getpfc	= mlx4_en_dcbnl_ieee_getpfc,
	.ieee_setpfc	= mlx4_en_dcbnl_ieee_setpfc,

	.getdcbx	= mlx4_en_dcbnl_getdcbx,
	.setdcbx	= mlx4_en_dcbnl_setdcbx,
	.ieee_getqcn	= mlx4_en_dcbnl_ieee_getqcn,
	.ieee_setqcn	= mlx4_en_dcbnl_ieee_setqcn,
	.ieee_getqcnstats = mlx4_en_dcbnl_ieee_getqcnstats,
};

const struct dcbnl_rtnl_ops mlx4_en_dcbnl_pfc_ops = {
	.ieee_getpfc	= mlx4_en_dcbnl_ieee_getpfc,
	.ieee_setpfc	= mlx4_en_dcbnl_ieee_setpfc,

	.getdcbx	= mlx4_en_dcbnl_getdcbx,
	.setdcbx	= mlx4_en_dcbnl_setdcbx,
};
