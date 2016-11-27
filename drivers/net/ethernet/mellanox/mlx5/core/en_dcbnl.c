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
#include <linux/device.h>
#include <linux/netdevice.h>
#include "en.h"

#define MLX5E_MAX_PRIORITY 8

#define MLX5E_100MB (100000)
#define MLX5E_1GB   (1000000)

#define MLX5E_CEE_STATE_UP    1
#define MLX5E_CEE_STATE_DOWN  0

/* If dcbx mode is non-host set the dcbx mode to host.
 */
static int mlx5e_dcbnl_set_dcbx_mode(struct mlx5e_priv *priv,
				     enum mlx5_dcbx_oper_mode mode)
{
	struct mlx5_core_dev *mdev = priv->mdev;
	u32 param[MLX5_ST_SZ_DW(dcbx_param)];
	int err;

	err = mlx5_query_port_dcbx_param(mdev, param);
	if (err)
		return err;

	MLX5_SET(dcbx_param, param, version_admin, mode);
	if (mode != MLX5E_DCBX_PARAM_VER_OPER_HOST)
		MLX5_SET(dcbx_param, param, willing_admin, 1);

	return mlx5_set_port_dcbx_param(mdev, param);
}

static int mlx5e_dcbnl_switch_to_host_mode(struct mlx5e_priv *priv)
{
	struct mlx5e_dcbx *dcbx = &priv->dcbx;
	int err;

	if (!MLX5_CAP_GEN(priv->mdev, dcbx))
		return 0;

	if (dcbx->mode == MLX5E_DCBX_PARAM_VER_OPER_HOST)
		return 0;

	err = mlx5e_dcbnl_set_dcbx_mode(priv, MLX5E_DCBX_PARAM_VER_OPER_HOST);
	if (err)
		return err;

	dcbx->mode = MLX5E_DCBX_PARAM_VER_OPER_HOST;
	return 0;
}

static int mlx5e_dcbnl_ieee_getets(struct net_device *netdev,
				   struct ieee_ets *ets)
{
	struct mlx5e_priv *priv = netdev_priv(netdev);
	struct mlx5_core_dev *mdev = priv->mdev;
	int err = 0;
	int i;

	if (!MLX5_CAP_GEN(priv->mdev, ets))
		return -ENOTSUPP;

	ets->ets_cap = mlx5_max_tc(priv->mdev) + 1;
	for (i = 0; i < ets->ets_cap; i++) {
		err = mlx5_query_port_prio_tc(mdev, i, &ets->prio_tc[i]);
		if (err)
			return err;
	}

	for (i = 0; i < ets->ets_cap; i++) {
		err = mlx5_query_port_tc_bw_alloc(mdev, i, &ets->tc_tx_bw[i]);
		if (err)
			return err;
		if (ets->tc_tx_bw[i] < MLX5E_MAX_BW_ALLOC)
			priv->dcbx.tc_tsa[i] = IEEE_8021QAZ_TSA_ETS;
	}

	memcpy(ets->tc_tsa, priv->dcbx.tc_tsa, sizeof(ets->tc_tsa));

	return err;
}

enum {
	MLX5E_VENDOR_TC_GROUP_NUM = 7,
	MLX5E_ETS_TC_GROUP_NUM    = 0,
};

static void mlx5e_build_tc_group(struct ieee_ets *ets, u8 *tc_group, int max_tc)
{
	bool any_tc_mapped_to_ets = false;
	int strict_group;
	int i;

	for (i = 0; i <= max_tc; i++)
		if (ets->tc_tsa[i] == IEEE_8021QAZ_TSA_ETS)
			any_tc_mapped_to_ets = true;

	strict_group = any_tc_mapped_to_ets ? 1 : 0;

	for (i = 0; i <= max_tc; i++) {
		switch (ets->tc_tsa[i]) {
		case IEEE_8021QAZ_TSA_VENDOR:
			tc_group[i] = MLX5E_VENDOR_TC_GROUP_NUM;
			break;
		case IEEE_8021QAZ_TSA_STRICT:
			tc_group[i] = strict_group++;
			break;
		case IEEE_8021QAZ_TSA_ETS:
			tc_group[i] = MLX5E_ETS_TC_GROUP_NUM;
			break;
		}
	}
}

static void mlx5e_build_tc_tx_bw(struct ieee_ets *ets, u8 *tc_tx_bw,
				 u8 *tc_group, int max_tc)
{
	int i;

	for (i = 0; i <= max_tc; i++) {
		switch (ets->tc_tsa[i]) {
		case IEEE_8021QAZ_TSA_VENDOR:
			tc_tx_bw[i] = MLX5E_MAX_BW_ALLOC;
			break;
		case IEEE_8021QAZ_TSA_STRICT:
			tc_tx_bw[i] = MLX5E_MAX_BW_ALLOC;
			break;
		case IEEE_8021QAZ_TSA_ETS:
			tc_tx_bw[i] = ets->tc_tx_bw[i];
			break;
		}
	}
}

int mlx5e_dcbnl_ieee_setets_core(struct mlx5e_priv *priv, struct ieee_ets *ets)
{
	struct mlx5_core_dev *mdev = priv->mdev;
	u8 tc_tx_bw[IEEE_8021QAZ_MAX_TCS];
	u8 tc_group[IEEE_8021QAZ_MAX_TCS];
	int max_tc = mlx5_max_tc(mdev);
	int err;

	mlx5e_build_tc_group(ets, tc_group, max_tc);
	mlx5e_build_tc_tx_bw(ets, tc_tx_bw, tc_group, max_tc);

	err = mlx5_set_port_prio_tc(mdev, ets->prio_tc);
	if (err)
		return err;

	err = mlx5_set_port_tc_group(mdev, tc_group);
	if (err)
		return err;

	err = mlx5_set_port_tc_bw_alloc(mdev, tc_tx_bw);

	if (err)
		return err;

	memcpy(priv->dcbx.tc_tsa, ets->tc_tsa, sizeof(ets->tc_tsa));

	return err;
}

static int mlx5e_dbcnl_validate_ets(struct net_device *netdev,
				    struct ieee_ets *ets)
{
	int bw_sum = 0;
	int i;

	/* Validate Priority */
	for (i = 0; i < IEEE_8021QAZ_MAX_TCS; i++) {
		if (ets->prio_tc[i] >= MLX5E_MAX_PRIORITY) {
			netdev_err(netdev,
				   "Failed to validate ETS: priority value greater than max(%d)\n",
				    MLX5E_MAX_PRIORITY);
			return -EINVAL;
		}
	}

	/* Validate Bandwidth Sum */
	for (i = 0; i < IEEE_8021QAZ_MAX_TCS; i++) {
		if (ets->tc_tsa[i] == IEEE_8021QAZ_TSA_ETS) {
			if (!ets->tc_tx_bw[i]) {
				netdev_err(netdev,
					   "Failed to validate ETS: BW 0 is illegal\n");
				return -EINVAL;
			}

			bw_sum += ets->tc_tx_bw[i];
		}
	}

	if (bw_sum != 0 && bw_sum != 100) {
		netdev_err(netdev,
			   "Failed to validate ETS: BW sum is illegal\n");
		return -EINVAL;
	}
	return 0;
}

static int mlx5e_dcbnl_ieee_setets(struct net_device *netdev,
				   struct ieee_ets *ets)
{
	struct mlx5e_priv *priv = netdev_priv(netdev);
	int err;

	if (!MLX5_CAP_GEN(priv->mdev, ets))
		return -ENOTSUPP;

	err = mlx5e_dbcnl_validate_ets(netdev, ets);
	if (err)
		return err;

	err = mlx5e_dcbnl_ieee_setets_core(priv, ets);
	if (err)
		return err;

	return 0;
}

static int mlx5e_dcbnl_ieee_getpfc(struct net_device *dev,
				   struct ieee_pfc *pfc)
{
	struct mlx5e_priv *priv = netdev_priv(dev);
	struct mlx5_core_dev *mdev = priv->mdev;
	struct mlx5e_pport_stats *pstats = &priv->stats.pport;
	int i;

	pfc->pfc_cap = mlx5_max_tc(mdev) + 1;
	for (i = 0; i < IEEE_8021QAZ_MAX_TCS; i++) {
		pfc->requests[i]    = PPORT_PER_PRIO_GET(pstats, i, tx_pause);
		pfc->indications[i] = PPORT_PER_PRIO_GET(pstats, i, rx_pause);
	}

	return mlx5_query_port_pfc(mdev, &pfc->pfc_en, NULL);
}

static int mlx5e_dcbnl_ieee_setpfc(struct net_device *dev,
				   struct ieee_pfc *pfc)
{
	struct mlx5e_priv *priv = netdev_priv(dev);
	struct mlx5_core_dev *mdev = priv->mdev;
	u8 curr_pfc_en;
	int ret;

	mlx5_query_port_pfc(mdev, &curr_pfc_en, NULL);

	if (pfc->pfc_en == curr_pfc_en)
		return 0;

	ret = mlx5_set_port_pfc(mdev, pfc->pfc_en, pfc->pfc_en);
	mlx5_toggle_port_link(mdev);

	return ret;
}

static u8 mlx5e_dcbnl_getdcbx(struct net_device *dev)
{
	struct mlx5e_priv *priv = netdev_priv(dev);
	struct mlx5e_dcbx *dcbx = &priv->dcbx;
	u8 mode = DCB_CAP_DCBX_VER_IEEE | DCB_CAP_DCBX_VER_CEE;

	if (dcbx->mode == MLX5E_DCBX_PARAM_VER_OPER_HOST)
		mode |= DCB_CAP_DCBX_HOST;

	return mode;
}

static u8 mlx5e_dcbnl_setdcbx(struct net_device *dev, u8 mode)
{
	struct mlx5e_priv *priv = netdev_priv(dev);
	struct mlx5e_dcbx *dcbx = &priv->dcbx;

	if ((!mode) && MLX5_CAP_GEN(priv->mdev, dcbx)) {
		if (dcbx->mode == MLX5E_DCBX_PARAM_VER_OPER_AUTO)
			return 0;

		/* set dcbx to fw controlled */
		if (!mlx5e_dcbnl_set_dcbx_mode(priv, MLX5E_DCBX_PARAM_VER_OPER_AUTO)) {
			dcbx->mode = MLX5E_DCBX_PARAM_VER_OPER_AUTO;
			return 0;
		}

		return 1;
	}

	if (mlx5e_dcbnl_switch_to_host_mode(netdev_priv(dev)))
		return 1;

	if ((mode & DCB_CAP_DCBX_LLD_MANAGED) ||
	    !(mode & DCB_CAP_DCBX_VER_CEE) ||
	    !(mode & DCB_CAP_DCBX_VER_IEEE) ||
	    !(mode & DCB_CAP_DCBX_HOST))
		return 1;

	return 0;
}

static int mlx5e_dcbnl_ieee_getmaxrate(struct net_device *netdev,
				       struct ieee_maxrate *maxrate)
{
	struct mlx5e_priv *priv    = netdev_priv(netdev);
	struct mlx5_core_dev *mdev = priv->mdev;
	u8 max_bw_value[IEEE_8021QAZ_MAX_TCS];
	u8 max_bw_unit[IEEE_8021QAZ_MAX_TCS];
	int err;
	int i;

	err = mlx5_query_port_ets_rate_limit(mdev, max_bw_value, max_bw_unit);
	if (err)
		return err;

	memset(maxrate->tc_maxrate, 0, sizeof(maxrate->tc_maxrate));

	for (i = 0; i <= mlx5_max_tc(mdev); i++) {
		switch (max_bw_unit[i]) {
		case MLX5_100_MBPS_UNIT:
			maxrate->tc_maxrate[i] = max_bw_value[i] * MLX5E_100MB;
			break;
		case MLX5_GBPS_UNIT:
			maxrate->tc_maxrate[i] = max_bw_value[i] * MLX5E_1GB;
			break;
		case MLX5_BW_NO_LIMIT:
			break;
		default:
			WARN(true, "non-supported BW unit");
			break;
		}
	}

	return 0;
}

static int mlx5e_dcbnl_ieee_setmaxrate(struct net_device *netdev,
				       struct ieee_maxrate *maxrate)
{
	struct mlx5e_priv *priv    = netdev_priv(netdev);
	struct mlx5_core_dev *mdev = priv->mdev;
	u8 max_bw_value[IEEE_8021QAZ_MAX_TCS];
	u8 max_bw_unit[IEEE_8021QAZ_MAX_TCS];
	__u64 upper_limit_mbps = roundup(255 * MLX5E_100MB, MLX5E_1GB);
	int i;

	memset(max_bw_value, 0, sizeof(max_bw_value));
	memset(max_bw_unit, 0, sizeof(max_bw_unit));

	for (i = 0; i <= mlx5_max_tc(mdev); i++) {
		if (!maxrate->tc_maxrate[i]) {
			max_bw_unit[i]  = MLX5_BW_NO_LIMIT;
			continue;
		}
		if (maxrate->tc_maxrate[i] < upper_limit_mbps) {
			max_bw_value[i] = div_u64(maxrate->tc_maxrate[i],
						  MLX5E_100MB);
			max_bw_value[i] = max_bw_value[i] ? max_bw_value[i] : 1;
			max_bw_unit[i]  = MLX5_100_MBPS_UNIT;
		} else {
			max_bw_value[i] = div_u64(maxrate->tc_maxrate[i],
						  MLX5E_1GB);
			max_bw_unit[i]  = MLX5_GBPS_UNIT;
		}
	}

	return mlx5_modify_port_ets_rate_limit(mdev, max_bw_value, max_bw_unit);
}

static u8 mlx5e_dcbnl_setall(struct net_device *netdev)
{
	struct mlx5e_priv *priv = netdev_priv(netdev);
	struct mlx5e_cee_config *cee_cfg = &priv->dcbx.cee_cfg;
	struct mlx5_core_dev *mdev = priv->mdev;
	struct ieee_ets ets;
	struct ieee_pfc pfc;
	int err = -ENOTSUPP;
	int i;

	if (!MLX5_CAP_GEN(mdev, ets))
		goto out;

	memset(&ets, 0, sizeof(ets));
	memset(&pfc, 0, sizeof(pfc));

	ets.ets_cap = IEEE_8021QAZ_MAX_TCS;
	for (i = 0; i < CEE_DCBX_MAX_PGS; i++) {
		ets.tc_tx_bw[i] = cee_cfg->pg_bw_pct[i];
		ets.tc_rx_bw[i] = cee_cfg->pg_bw_pct[i];
		ets.tc_tsa[i]   = IEEE_8021QAZ_TSA_ETS;
		ets.prio_tc[i]  = cee_cfg->prio_to_pg_map[i];
	}

	err = mlx5e_dbcnl_validate_ets(netdev, &ets);
	if (err) {
		netdev_err(netdev,
			   "%s, Failed to validate ETS: %d\n", __func__, err);
		goto out;
	}

	err = mlx5e_dcbnl_ieee_setets_core(priv, &ets);
	if (err) {
		netdev_err(netdev,
			   "%s, Failed to set ETS: %d\n", __func__, err);
		goto out;
	}

	/* Set PFC */
	pfc.pfc_cap = mlx5_max_tc(mdev) + 1;
	if (!cee_cfg->pfc_enable)
		pfc.pfc_en = 0;
	else
		for (i = 0; i < CEE_DCBX_MAX_PRIO; i++)
			pfc.pfc_en |= cee_cfg->pfc_setting[i] << i;

	err = mlx5e_dcbnl_ieee_setpfc(netdev, &pfc);
	if (err) {
		netdev_err(netdev,
			   "%s, Failed to set PFC: %d\n", __func__, err);
		goto out;
	}
out:
	return err ? MLX5_DCB_NO_CHG : MLX5_DCB_CHG_RESET;
}

static u8 mlx5e_dcbnl_getstate(struct net_device *netdev)
{
	return MLX5E_CEE_STATE_UP;
}

static void mlx5e_dcbnl_getpermhwaddr(struct net_device *netdev,
				      u8 *perm_addr)
{
	struct mlx5e_priv *priv = netdev_priv(netdev);

	if (!perm_addr)
		return;

	mlx5_query_nic_vport_mac_address(priv->mdev, 0, perm_addr);
}

static void mlx5e_dcbnl_setpgtccfgtx(struct net_device *netdev,
				     int priority, u8 prio_type,
				     u8 pgid, u8 bw_pct, u8 up_map)
{
	struct mlx5e_priv *priv = netdev_priv(netdev);
	struct mlx5e_cee_config *cee_cfg = &priv->dcbx.cee_cfg;

	if (priority >= CEE_DCBX_MAX_PRIO) {
		netdev_err(netdev,
			   "%s, priority is out of range\n", __func__);
		return;
	}

	if (pgid >= CEE_DCBX_MAX_PGS) {
		netdev_err(netdev,
			   "%s, priority group is out of range\n", __func__);
		return;
	}

	cee_cfg->prio_to_pg_map[priority] = pgid;
}

static void mlx5e_dcbnl_setpgbwgcfgtx(struct net_device *netdev,
				      int pgid, u8 bw_pct)
{
	struct mlx5e_priv *priv = netdev_priv(netdev);
	struct mlx5e_cee_config *cee_cfg = &priv->dcbx.cee_cfg;

	if (pgid >= CEE_DCBX_MAX_PGS) {
		netdev_err(netdev,
			   "%s, priority group is out of range\n", __func__);
		return;
	}

	cee_cfg->pg_bw_pct[pgid] = bw_pct;
}

static void mlx5e_dcbnl_getpgtccfgtx(struct net_device *netdev,
				     int priority, u8 *prio_type,
				     u8 *pgid, u8 *bw_pct, u8 *up_map)
{
	struct mlx5e_priv *priv = netdev_priv(netdev);
	struct mlx5_core_dev *mdev = priv->mdev;

	if (priority >= CEE_DCBX_MAX_PRIO) {
		netdev_err(netdev,
			   "%s, priority is out of range\n", __func__);
		return;
	}

	*prio_type = 0;
	*bw_pct = 0;
	*up_map = 0;

	if (mlx5_query_port_prio_tc(mdev, priority, pgid))
		*pgid = 0;
}

static void mlx5e_dcbnl_getpgbwgcfgtx(struct net_device *netdev,
				      int pgid, u8 *bw_pct)
{
	struct mlx5e_priv *priv = netdev_priv(netdev);
	struct mlx5_core_dev *mdev = priv->mdev;

	if (pgid >= CEE_DCBX_MAX_PGS) {
		netdev_err(netdev,
			   "%s, priority group is out of range\n", __func__);
		return;
	}

	if (mlx5_query_port_tc_bw_alloc(mdev, pgid, bw_pct))
		*bw_pct = 0;
}

static void mlx5e_dcbnl_setpfccfg(struct net_device *netdev,
				  int priority, u8 setting)
{
	struct mlx5e_priv *priv = netdev_priv(netdev);
	struct mlx5e_cee_config *cee_cfg = &priv->dcbx.cee_cfg;

	if (priority >= CEE_DCBX_MAX_PRIO) {
		netdev_err(netdev,
			   "%s, priority is out of range\n", __func__);
		return;
	}

	if (setting > 1)
		return;

	cee_cfg->pfc_setting[priority] = setting;
}

static int
mlx5e_dcbnl_get_priority_pfc(struct net_device *netdev,
			     int priority, u8 *setting)
{
	struct ieee_pfc pfc;
	int err;

	err = mlx5e_dcbnl_ieee_getpfc(netdev, &pfc);

	if (err)
		*setting = 0;
	else
		*setting = (pfc.pfc_en >> priority) & 0x01;

	return err;
}

static void mlx5e_dcbnl_getpfccfg(struct net_device *netdev,
				  int priority, u8 *setting)
{
	if (priority >= CEE_DCBX_MAX_PRIO) {
		netdev_err(netdev,
			   "%s, priority is out of range\n", __func__);
		return;
	}

	if (!setting)
		return;

	mlx5e_dcbnl_get_priority_pfc(netdev, priority, setting);
}

static u8 mlx5e_dcbnl_getcap(struct net_device *netdev,
			     int capid, u8 *cap)
{
	struct mlx5e_priv *priv = netdev_priv(netdev);
	struct mlx5_core_dev *mdev = priv->mdev;
	u8 rval = 0;

	switch (capid) {
	case DCB_CAP_ATTR_PG:
		*cap = true;
		break;
	case DCB_CAP_ATTR_PFC:
		*cap = true;
		break;
	case DCB_CAP_ATTR_UP2TC:
		*cap = false;
		break;
	case DCB_CAP_ATTR_PG_TCS:
		*cap = 1 << mlx5_max_tc(mdev);
		break;
	case DCB_CAP_ATTR_PFC_TCS:
		*cap = 1 << mlx5_max_tc(mdev);
		break;
	case DCB_CAP_ATTR_GSP:
		*cap = false;
		break;
	case DCB_CAP_ATTR_BCN:
		*cap = false;
		break;
	case DCB_CAP_ATTR_DCBX:
		*cap = (DCB_CAP_DCBX_LLD_MANAGED |
			DCB_CAP_DCBX_VER_CEE |
			DCB_CAP_DCBX_STATIC);
		break;
	default:
		*cap = 0;
		rval = 1;
		break;
	}

	return rval;
}

static int mlx5e_dcbnl_getnumtcs(struct net_device *netdev,
				 int tcs_id, u8 *num)
{
	struct mlx5e_priv *priv = netdev_priv(netdev);
	struct mlx5_core_dev *mdev = priv->mdev;

	switch (tcs_id) {
	case DCB_NUMTCS_ATTR_PG:
	case DCB_NUMTCS_ATTR_PFC:
		*num = mlx5_max_tc(mdev) + 1;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static u8 mlx5e_dcbnl_getpfcstate(struct net_device *netdev)
{
	struct ieee_pfc pfc;

	if (mlx5e_dcbnl_ieee_getpfc(netdev, &pfc))
		return MLX5E_CEE_STATE_DOWN;

	return pfc.pfc_en ? MLX5E_CEE_STATE_UP : MLX5E_CEE_STATE_DOWN;
}

static void mlx5e_dcbnl_setpfcstate(struct net_device *netdev, u8 state)
{
	struct mlx5e_priv *priv = netdev_priv(netdev);
	struct mlx5e_cee_config *cee_cfg = &priv->dcbx.cee_cfg;

	if ((state != MLX5E_CEE_STATE_UP) && (state != MLX5E_CEE_STATE_DOWN))
		return;

	cee_cfg->pfc_enable = state;
}

const struct dcbnl_rtnl_ops mlx5e_dcbnl_ops = {
	.ieee_getets	= mlx5e_dcbnl_ieee_getets,
	.ieee_setets	= mlx5e_dcbnl_ieee_setets,
	.ieee_getmaxrate = mlx5e_dcbnl_ieee_getmaxrate,
	.ieee_setmaxrate = mlx5e_dcbnl_ieee_setmaxrate,
	.ieee_getpfc	= mlx5e_dcbnl_ieee_getpfc,
	.ieee_setpfc	= mlx5e_dcbnl_ieee_setpfc,
	.getdcbx	= mlx5e_dcbnl_getdcbx,
	.setdcbx	= mlx5e_dcbnl_setdcbx,

/* CEE interfaces */
	.setall         = mlx5e_dcbnl_setall,
	.getstate       = mlx5e_dcbnl_getstate,
	.getpermhwaddr  = mlx5e_dcbnl_getpermhwaddr,

	.setpgtccfgtx   = mlx5e_dcbnl_setpgtccfgtx,
	.setpgbwgcfgtx  = mlx5e_dcbnl_setpgbwgcfgtx,
	.getpgtccfgtx   = mlx5e_dcbnl_getpgtccfgtx,
	.getpgbwgcfgtx  = mlx5e_dcbnl_getpgbwgcfgtx,

	.setpfccfg      = mlx5e_dcbnl_setpfccfg,
	.getpfccfg      = mlx5e_dcbnl_getpfccfg,
	.getcap         = mlx5e_dcbnl_getcap,
	.getnumtcs      = mlx5e_dcbnl_getnumtcs,
	.getpfcstate    = mlx5e_dcbnl_getpfcstate,
	.setpfcstate    = mlx5e_dcbnl_setpfcstate,
};

static void mlx5e_dcbnl_query_dcbx_mode(struct mlx5e_priv *priv,
					enum mlx5_dcbx_oper_mode *mode)
{
	u32 out[MLX5_ST_SZ_DW(dcbx_param)];

	*mode = MLX5E_DCBX_PARAM_VER_OPER_HOST;

	if (!mlx5_query_port_dcbx_param(priv->mdev, out))
		*mode = MLX5_GET(dcbx_param, out, version_oper);

	/* From driver's point of view, we only care if the mode
	 * is host (HOST) or non-host (AUTO)
	 */
	if (*mode != MLX5E_DCBX_PARAM_VER_OPER_HOST)
		*mode = MLX5E_DCBX_PARAM_VER_OPER_AUTO;
}

static void mlx5e_ets_init(struct mlx5e_priv *priv)
{
	int i;
	struct ieee_ets ets;

	memset(&ets, 0, sizeof(ets));
	ets.ets_cap = mlx5_max_tc(priv->mdev) + 1;
	for (i = 0; i < ets.ets_cap; i++) {
		ets.tc_tx_bw[i] = MLX5E_MAX_BW_ALLOC;
		ets.tc_tsa[i] = IEEE_8021QAZ_TSA_VENDOR;
		ets.prio_tc[i] = i;
	}

	memcpy(priv->dcbx.tc_tsa, ets.tc_tsa, sizeof(ets.tc_tsa));

	/* tclass[prio=0]=1, tclass[prio=1]=0, tclass[prio=i]=i (for i>1) */
	ets.prio_tc[0] = 1;
	ets.prio_tc[1] = 0;

	mlx5e_dcbnl_ieee_setets_core(priv, &ets);
}

void mlx5e_dcbnl_initialize(struct mlx5e_priv *priv)
{
	struct mlx5e_dcbx *dcbx = &priv->dcbx;

	if (MLX5_CAP_GEN(priv->mdev, dcbx))
		mlx5e_dcbnl_query_dcbx_mode(priv, &dcbx->mode);

	mlx5e_ets_init(priv);
}
