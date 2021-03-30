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
#include "en/port.h"
#include "en/port_buffer.h"

#define MLX5E_MAX_BW_ALLOC 100 /* Max percentage of BW allocation */

#define MLX5E_100MB (100000)
#define MLX5E_1GB   (1000000)

#define MLX5E_CEE_STATE_UP    1
#define MLX5E_CEE_STATE_DOWN  0

/* Max supported cable length is 1000 meters */
#define MLX5E_MAX_CABLE_LENGTH 1000

enum {
	MLX5E_VENDOR_TC_GROUP_NUM = 7,
	MLX5E_LOWEST_PRIO_GROUP   = 0,
};

enum {
	MLX5_DCB_CHG_RESET,
	MLX5_DCB_NO_CHG,
	MLX5_DCB_CHG_NO_RESET,
};

#define MLX5_DSCP_SUPPORTED(mdev) (MLX5_CAP_GEN(mdev, qcam_reg)  && \
				   MLX5_CAP_QCAM_REG(mdev, qpts) && \
				   MLX5_CAP_QCAM_REG(mdev, qpdpm))

static int mlx5e_set_trust_state(struct mlx5e_priv *priv, u8 trust_state);
static int mlx5e_set_dscp2prio(struct mlx5e_priv *priv, u8 dscp, u8 prio);

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
	u8 tc_group[IEEE_8021QAZ_MAX_TCS];
	bool is_tc_group_6_exist = false;
	bool is_zero_bw_ets_tc = false;
	int err = 0;
	int i;

	if (!MLX5_CAP_GEN(priv->mdev, ets))
		return -EOPNOTSUPP;

	ets->ets_cap = mlx5_max_tc(priv->mdev) + 1;
	for (i = 0; i < ets->ets_cap; i++) {
		err = mlx5_query_port_prio_tc(mdev, i, &ets->prio_tc[i]);
		if (err)
			return err;

		err = mlx5_query_port_tc_group(mdev, i, &tc_group[i]);
		if (err)
			return err;

		err = mlx5_query_port_tc_bw_alloc(mdev, i, &ets->tc_tx_bw[i]);
		if (err)
			return err;

		if (ets->tc_tx_bw[i] < MLX5E_MAX_BW_ALLOC &&
		    tc_group[i] == (MLX5E_LOWEST_PRIO_GROUP + 1))
			is_zero_bw_ets_tc = true;

		if (tc_group[i] == (MLX5E_VENDOR_TC_GROUP_NUM - 1))
			is_tc_group_6_exist = true;
	}

	/* Report 0% ets tc if exits*/
	if (is_zero_bw_ets_tc) {
		for (i = 0; i < ets->ets_cap; i++)
			if (tc_group[i] == MLX5E_LOWEST_PRIO_GROUP)
				ets->tc_tx_bw[i] = 0;
	}

	/* Update tc_tsa based on fw setting*/
	for (i = 0; i < ets->ets_cap; i++) {
		if (ets->tc_tx_bw[i] < MLX5E_MAX_BW_ALLOC)
			priv->dcbx.tc_tsa[i] = IEEE_8021QAZ_TSA_ETS;
		else if (tc_group[i] == MLX5E_VENDOR_TC_GROUP_NUM &&
			 !is_tc_group_6_exist)
			priv->dcbx.tc_tsa[i] = IEEE_8021QAZ_TSA_VENDOR;
	}
	memcpy(ets->tc_tsa, priv->dcbx.tc_tsa, sizeof(ets->tc_tsa));

	return err;
}

static void mlx5e_build_tc_group(struct ieee_ets *ets, u8 *tc_group, int max_tc)
{
	bool any_tc_mapped_to_ets = false;
	bool ets_zero_bw = false;
	int strict_group;
	int i;

	for (i = 0; i <= max_tc; i++) {
		if (ets->tc_tsa[i] == IEEE_8021QAZ_TSA_ETS) {
			any_tc_mapped_to_ets = true;
			if (!ets->tc_tx_bw[i])
				ets_zero_bw = true;
		}
	}

	/* strict group has higher priority than ets group */
	strict_group = MLX5E_LOWEST_PRIO_GROUP;
	if (any_tc_mapped_to_ets)
		strict_group++;
	if (ets_zero_bw)
		strict_group++;

	for (i = 0; i <= max_tc; i++) {
		switch (ets->tc_tsa[i]) {
		case IEEE_8021QAZ_TSA_VENDOR:
			tc_group[i] = MLX5E_VENDOR_TC_GROUP_NUM;
			break;
		case IEEE_8021QAZ_TSA_STRICT:
			tc_group[i] = strict_group++;
			break;
		case IEEE_8021QAZ_TSA_ETS:
			tc_group[i] = MLX5E_LOWEST_PRIO_GROUP;
			if (ets->tc_tx_bw[i] && ets_zero_bw)
				tc_group[i] = MLX5E_LOWEST_PRIO_GROUP + 1;
			break;
		}
	}
}

static void mlx5e_build_tc_tx_bw(struct ieee_ets *ets, u8 *tc_tx_bw,
				 u8 *tc_group, int max_tc)
{
	int bw_for_ets_zero_bw_tc = 0;
	int last_ets_zero_bw_tc = -1;
	int num_ets_zero_bw = 0;
	int i;

	for (i = 0; i <= max_tc; i++) {
		if (ets->tc_tsa[i] == IEEE_8021QAZ_TSA_ETS &&
		    !ets->tc_tx_bw[i]) {
			num_ets_zero_bw++;
			last_ets_zero_bw_tc = i;
		}
	}

	if (num_ets_zero_bw)
		bw_for_ets_zero_bw_tc = MLX5E_MAX_BW_ALLOC / num_ets_zero_bw;

	for (i = 0; i <= max_tc; i++) {
		switch (ets->tc_tsa[i]) {
		case IEEE_8021QAZ_TSA_VENDOR:
			tc_tx_bw[i] = MLX5E_MAX_BW_ALLOC;
			break;
		case IEEE_8021QAZ_TSA_STRICT:
			tc_tx_bw[i] = MLX5E_MAX_BW_ALLOC;
			break;
		case IEEE_8021QAZ_TSA_ETS:
			tc_tx_bw[i] = ets->tc_tx_bw[i] ?
				      ets->tc_tx_bw[i] :
				      bw_for_ets_zero_bw_tc;
			break;
		}
	}

	/* Make sure the total bw for ets zero bw group is 100% */
	if (last_ets_zero_bw_tc != -1)
		tc_tx_bw[last_ets_zero_bw_tc] +=
			MLX5E_MAX_BW_ALLOC % num_ets_zero_bw;
}

/* If there are ETS BW 0,
 *   Set ETS group # to 1 for all ETS non zero BW tcs. Their sum must be 100%.
 *   Set group #0 to all the ETS BW 0 tcs and
 *     equally splits the 100% BW between them
 *   Report both group #0 and #1 as ETS type.
 *     All the tcs in group #0 will be reported with 0% BW.
 */
static int mlx5e_dcbnl_ieee_setets_core(struct mlx5e_priv *priv, struct ieee_ets *ets)
{
	struct mlx5_core_dev *mdev = priv->mdev;
	u8 tc_tx_bw[IEEE_8021QAZ_MAX_TCS];
	u8 tc_group[IEEE_8021QAZ_MAX_TCS];
	int max_tc = mlx5_max_tc(mdev);
	int err, i;

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

	for (i = 0; i < IEEE_8021QAZ_MAX_TCS; i++) {
		mlx5e_dbg(HW, priv, "%s: prio_%d <=> tc_%d\n",
			  __func__, i, ets->prio_tc[i]);
		mlx5e_dbg(HW, priv, "%s: tc_%d <=> tx_bw_%d%%, group_%d\n",
			  __func__, i, tc_tx_bw[i], tc_group[i]);
	}

	return err;
}

static int mlx5e_dbcnl_validate_ets(struct net_device *netdev,
				    struct ieee_ets *ets,
				    bool zero_sum_allowed)
{
	bool have_ets_tc = false;
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
			have_ets_tc = true;
			bw_sum += ets->tc_tx_bw[i];
		}
	}

	if (have_ets_tc && bw_sum != 100) {
		if (bw_sum || (!bw_sum && !zero_sum_allowed))
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
		return -EOPNOTSUPP;

	err = mlx5e_dbcnl_validate_ets(netdev, ets, false);
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

	if (MLX5_BUFFER_SUPPORTED(mdev))
		pfc->delay = priv->dcbx.cable_len;

	return mlx5_query_port_pfc(mdev, &pfc->pfc_en, NULL);
}

static int mlx5e_dcbnl_ieee_setpfc(struct net_device *dev,
				   struct ieee_pfc *pfc)
{
	struct mlx5e_priv *priv = netdev_priv(dev);
	struct mlx5_core_dev *mdev = priv->mdev;
	u32 old_cable_len = priv->dcbx.cable_len;
	struct ieee_pfc pfc_new;
	u32 changed = 0;
	u8 curr_pfc_en;
	int ret = 0;

	/* pfc_en */
	mlx5_query_port_pfc(mdev, &curr_pfc_en, NULL);
	if (pfc->pfc_en != curr_pfc_en) {
		ret = mlx5_set_port_pfc(mdev, pfc->pfc_en, pfc->pfc_en);
		if (ret)
			return ret;
		mlx5_toggle_port_link(mdev);
		changed |= MLX5E_PORT_BUFFER_PFC;
	}

	if (pfc->delay &&
	    pfc->delay < MLX5E_MAX_CABLE_LENGTH &&
	    pfc->delay != priv->dcbx.cable_len) {
		priv->dcbx.cable_len = pfc->delay;
		changed |= MLX5E_PORT_BUFFER_CABLE_LEN;
	}

	if (MLX5_BUFFER_SUPPORTED(mdev)) {
		pfc_new.pfc_en = (changed & MLX5E_PORT_BUFFER_PFC) ? pfc->pfc_en : curr_pfc_en;
		if (priv->dcbx.manual_buffer)
			ret = mlx5e_port_manual_buffer_config(priv, changed,
							      dev->mtu, &pfc_new,
							      NULL, NULL);

		if (ret && (changed & MLX5E_PORT_BUFFER_CABLE_LEN))
			priv->dcbx.cable_len = old_cable_len;
	}

	if (!ret) {
		mlx5e_dbg(HW, priv,
			  "%s: PFC per priority bit mask: 0x%x\n",
			  __func__, pfc->pfc_en);
	}
	return ret;
}

static u8 mlx5e_dcbnl_getdcbx(struct net_device *dev)
{
	struct mlx5e_priv *priv = netdev_priv(dev);

	return priv->dcbx.cap;
}

static u8 mlx5e_dcbnl_setdcbx(struct net_device *dev, u8 mode)
{
	struct mlx5e_priv *priv = netdev_priv(dev);
	struct mlx5e_dcbx *dcbx = &priv->dcbx;

	if (mode & DCB_CAP_DCBX_LLD_MANAGED)
		return 1;

	if ((!mode) && MLX5_CAP_GEN(priv->mdev, dcbx)) {
		if (dcbx->mode == MLX5E_DCBX_PARAM_VER_OPER_AUTO)
			return 0;

		/* set dcbx to fw controlled */
		if (!mlx5e_dcbnl_set_dcbx_mode(priv, MLX5E_DCBX_PARAM_VER_OPER_AUTO)) {
			dcbx->mode = MLX5E_DCBX_PARAM_VER_OPER_AUTO;
			dcbx->cap &= ~DCB_CAP_DCBX_HOST;
			return 0;
		}

		return 1;
	}

	if (!(mode & DCB_CAP_DCBX_HOST))
		return 1;

	if (mlx5e_dcbnl_switch_to_host_mode(netdev_priv(dev)))
		return 1;

	dcbx->cap = mode;

	return 0;
}

static int mlx5e_dcbnl_ieee_setapp(struct net_device *dev, struct dcb_app *app)
{
	struct mlx5e_priv *priv = netdev_priv(dev);
	struct dcb_app temp;
	bool is_new;
	int err;

	if (!MLX5_CAP_GEN(priv->mdev, vport_group_manager) ||
	    !MLX5_DSCP_SUPPORTED(priv->mdev))
		return -EOPNOTSUPP;

	if ((app->selector != IEEE_8021QAZ_APP_SEL_DSCP) ||
	    (app->protocol >= MLX5E_MAX_DSCP))
		return -EINVAL;

	/* Save the old entry info */
	temp.selector = IEEE_8021QAZ_APP_SEL_DSCP;
	temp.protocol = app->protocol;
	temp.priority = priv->dcbx_dp.dscp2prio[app->protocol];

	/* Check if need to switch to dscp trust state */
	if (!priv->dcbx.dscp_app_cnt) {
		err =  mlx5e_set_trust_state(priv, MLX5_QPTS_TRUST_DSCP);
		if (err)
			return err;
	}

	/* Skip the fw command if new and old mapping are the same */
	if (app->priority != priv->dcbx_dp.dscp2prio[app->protocol]) {
		err = mlx5e_set_dscp2prio(priv, app->protocol, app->priority);
		if (err)
			goto fw_err;
	}

	/* Delete the old entry if exists */
	is_new = false;
	err = dcb_ieee_delapp(dev, &temp);
	if (err)
		is_new = true;

	/* Add new entry and update counter */
	err = dcb_ieee_setapp(dev, app);
	if (err)
		return err;

	if (is_new)
		priv->dcbx.dscp_app_cnt++;

	return err;

fw_err:
	mlx5e_set_trust_state(priv, MLX5_QPTS_TRUST_PCP);
	return err;
}

static int mlx5e_dcbnl_ieee_delapp(struct net_device *dev, struct dcb_app *app)
{
	struct mlx5e_priv *priv = netdev_priv(dev);
	int err;

	if  (!MLX5_CAP_GEN(priv->mdev, vport_group_manager) ||
	     !MLX5_DSCP_SUPPORTED(priv->mdev))
		return -EOPNOTSUPP;

	if ((app->selector != IEEE_8021QAZ_APP_SEL_DSCP) ||
	    (app->protocol >= MLX5E_MAX_DSCP))
		return -EINVAL;

	/* Skip if no dscp app entry */
	if (!priv->dcbx.dscp_app_cnt)
		return -ENOENT;

	/* Check if the entry matches fw setting */
	if (app->priority != priv->dcbx_dp.dscp2prio[app->protocol])
		return -ENOENT;

	/* Delete the app entry */
	err = dcb_ieee_delapp(dev, app);
	if (err)
		return err;

	/* Reset the priority mapping back to zero */
	err = mlx5e_set_dscp2prio(priv, app->protocol, 0);
	if (err)
		goto fw_err;

	priv->dcbx.dscp_app_cnt--;

	/* Check if need to switch to pcp trust state */
	if (!priv->dcbx.dscp_app_cnt)
		err = mlx5e_set_trust_state(priv, MLX5_QPTS_TRUST_PCP);

	return err;

fw_err:
	mlx5e_set_trust_state(priv, MLX5_QPTS_TRUST_PCP);
	return err;
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

	for (i = 0; i < IEEE_8021QAZ_MAX_TCS; i++) {
		mlx5e_dbg(HW, priv, "%s: tc_%d <=> max_bw %d Gbps\n",
			  __func__, i, max_bw_value[i]);
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
	int err = -EOPNOTSUPP;
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
		mlx5e_dbg(HW, priv,
			  "%s: Priority group %d: tx_bw %d, rx_bw %d, prio_tc %d\n",
			  __func__, i, ets.tc_tx_bw[i], ets.tc_rx_bw[i],
			  ets.prio_tc[i]);
	}

	err = mlx5e_dbcnl_validate_ets(netdev, &ets, true);
	if (err)
		goto out;

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

	memset(perm_addr, 0xff, MAX_ADDR_LEN);

	mlx5_query_mac_address(priv->mdev, perm_addr);
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

	if (!MLX5_CAP_GEN(priv->mdev, ets)) {
		netdev_err(netdev, "%s, ets is not supported\n", __func__);
		return;
	}

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
	struct ieee_ets ets;

	if (pgid >= CEE_DCBX_MAX_PGS) {
		netdev_err(netdev,
			   "%s, priority group is out of range\n", __func__);
		return;
	}

	mlx5e_dcbnl_ieee_getets(netdev, &ets);
	*bw_pct = ets.tc_tx_bw[pgid];
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
		*cap = priv->dcbx.cap |
		       DCB_CAP_DCBX_VER_CEE |
		       DCB_CAP_DCBX_VER_IEEE;
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

static int mlx5e_dcbnl_getbuffer(struct net_device *dev,
				 struct dcbnl_buffer *dcb_buffer)
{
	struct mlx5e_priv *priv = netdev_priv(dev);
	struct mlx5_core_dev *mdev = priv->mdev;
	struct mlx5e_port_buffer port_buffer;
	u8 buffer[MLX5E_MAX_PRIORITY];
	int i, err;

	if (!MLX5_BUFFER_SUPPORTED(mdev))
		return -EOPNOTSUPP;

	err = mlx5e_port_query_priority2buffer(mdev, buffer);
	if (err)
		return err;

	for (i = 0; i < MLX5E_MAX_PRIORITY; i++)
		dcb_buffer->prio2buffer[i] = buffer[i];

	err = mlx5e_port_query_buffer(priv, &port_buffer);
	if (err)
		return err;

	for (i = 0; i < MLX5E_MAX_BUFFER; i++)
		dcb_buffer->buffer_size[i] = port_buffer.buffer[i].size;
	dcb_buffer->total_size = port_buffer.port_buffer_size;

	return 0;
}

static int mlx5e_dcbnl_setbuffer(struct net_device *dev,
				 struct dcbnl_buffer *dcb_buffer)
{
	struct mlx5e_priv *priv = netdev_priv(dev);
	struct mlx5_core_dev *mdev = priv->mdev;
	struct mlx5e_port_buffer port_buffer;
	u8 old_prio2buffer[MLX5E_MAX_PRIORITY];
	u32 *buffer_size = NULL;
	u8 *prio2buffer = NULL;
	u32 changed = 0;
	int i, err;

	if (!MLX5_BUFFER_SUPPORTED(mdev))
		return -EOPNOTSUPP;

	for (i = 0; i < DCBX_MAX_BUFFERS; i++)
		mlx5_core_dbg(mdev, "buffer[%d]=%d\n", i, dcb_buffer->buffer_size[i]);

	for (i = 0; i < MLX5E_MAX_PRIORITY; i++)
		mlx5_core_dbg(mdev, "priority %d buffer%d\n", i, dcb_buffer->prio2buffer[i]);

	err = mlx5e_port_query_priority2buffer(mdev, old_prio2buffer);
	if (err)
		return err;

	for (i = 0; i < MLX5E_MAX_PRIORITY; i++) {
		if (dcb_buffer->prio2buffer[i] != old_prio2buffer[i]) {
			changed |= MLX5E_PORT_BUFFER_PRIO2BUFFER;
			prio2buffer = dcb_buffer->prio2buffer;
			break;
		}
	}

	err = mlx5e_port_query_buffer(priv, &port_buffer);
	if (err)
		return err;

	for (i = 0; i < MLX5E_MAX_BUFFER; i++) {
		if (port_buffer.buffer[i].size != dcb_buffer->buffer_size[i]) {
			changed |= MLX5E_PORT_BUFFER_SIZE;
			buffer_size = dcb_buffer->buffer_size;
			break;
		}
	}

	if (!changed)
		return 0;

	priv->dcbx.manual_buffer = true;
	err = mlx5e_port_manual_buffer_config(priv, changed, dev->mtu, NULL,
					      buffer_size, prio2buffer);
	return err;
}

static const struct dcbnl_rtnl_ops mlx5e_dcbnl_ops = {
	.ieee_getets	= mlx5e_dcbnl_ieee_getets,
	.ieee_setets	= mlx5e_dcbnl_ieee_setets,
	.ieee_getmaxrate = mlx5e_dcbnl_ieee_getmaxrate,
	.ieee_setmaxrate = mlx5e_dcbnl_ieee_setmaxrate,
	.ieee_getpfc	= mlx5e_dcbnl_ieee_getpfc,
	.ieee_setpfc	= mlx5e_dcbnl_ieee_setpfc,
	.ieee_setapp    = mlx5e_dcbnl_ieee_setapp,
	.ieee_delapp    = mlx5e_dcbnl_ieee_delapp,
	.getdcbx	= mlx5e_dcbnl_getdcbx,
	.setdcbx	= mlx5e_dcbnl_setdcbx,
	.dcbnl_getbuffer = mlx5e_dcbnl_getbuffer,
	.dcbnl_setbuffer = mlx5e_dcbnl_setbuffer,

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

void mlx5e_dcbnl_build_netdev(struct net_device *netdev)
{
	struct mlx5e_priv *priv = netdev_priv(netdev);
	struct mlx5_core_dev *mdev = priv->mdev;

	if (MLX5_CAP_GEN(mdev, vport_group_manager) && MLX5_CAP_GEN(mdev, qos))
		netdev->dcbnl_ops = &mlx5e_dcbnl_ops;
}

void mlx5e_dcbnl_build_rep_netdev(struct net_device *netdev)
{
	struct mlx5e_priv *priv = netdev_priv(netdev);
	struct mlx5_core_dev *mdev = priv->mdev;

	if (MLX5_CAP_GEN(mdev, qos))
		netdev->dcbnl_ops = &mlx5e_dcbnl_ops;
}

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
	struct ieee_ets ets;
	int err;
	int i;

	if (!MLX5_CAP_GEN(priv->mdev, ets))
		return;

	memset(&ets, 0, sizeof(ets));
	ets.ets_cap = mlx5_max_tc(priv->mdev) + 1;
	for (i = 0; i < ets.ets_cap; i++) {
		ets.tc_tx_bw[i] = MLX5E_MAX_BW_ALLOC;
		ets.tc_tsa[i] = IEEE_8021QAZ_TSA_VENDOR;
		ets.prio_tc[i] = i;
	}

	if (ets.ets_cap > 1) {
		/* tclass[prio=0]=1, tclass[prio=1]=0, tclass[prio=i]=i (for i>1) */
		ets.prio_tc[0] = 1;
		ets.prio_tc[1] = 0;
	}

	err = mlx5e_dcbnl_ieee_setets_core(priv, &ets);
	if (err)
		netdev_err(priv->netdev,
			   "%s, Failed to init ETS: %d\n", __func__, err);
}

enum {
	INIT,
	DELETE,
};

static void mlx5e_dcbnl_dscp_app(struct mlx5e_priv *priv, int action)
{
	struct dcb_app temp;
	int i;

	if (!MLX5_CAP_GEN(priv->mdev, vport_group_manager))
		return;

	if (!MLX5_DSCP_SUPPORTED(priv->mdev))
		return;

	/* No SEL_DSCP entry in non DSCP state */
	if (priv->dcbx_dp.trust_state != MLX5_QPTS_TRUST_DSCP)
		return;

	temp.selector = IEEE_8021QAZ_APP_SEL_DSCP;
	for (i = 0; i < MLX5E_MAX_DSCP; i++) {
		temp.protocol = i;
		temp.priority = priv->dcbx_dp.dscp2prio[i];
		if (action == INIT)
			dcb_ieee_setapp(priv->netdev, &temp);
		else
			dcb_ieee_delapp(priv->netdev, &temp);
	}

	priv->dcbx.dscp_app_cnt = (action == INIT) ? MLX5E_MAX_DSCP : 0;
}

void mlx5e_dcbnl_init_app(struct mlx5e_priv *priv)
{
	mlx5e_dcbnl_dscp_app(priv, INIT);
}

void mlx5e_dcbnl_delete_app(struct mlx5e_priv *priv)
{
	mlx5e_dcbnl_dscp_app(priv, DELETE);
}

static void mlx5e_params_calc_trust_tx_min_inline_mode(struct mlx5_core_dev *mdev,
						       struct mlx5e_params *params,
						       u8 trust_state)
{
	mlx5_query_min_inline(mdev, &params->tx_min_inline_mode);
	if (trust_state == MLX5_QPTS_TRUST_DSCP &&
	    params->tx_min_inline_mode == MLX5_INLINE_MODE_L2)
		params->tx_min_inline_mode = MLX5_INLINE_MODE_IP;
}

static int mlx5e_update_trust_state_hw(struct mlx5e_priv *priv, void *context)
{
	u8 *trust_state = context;
	int err;

	err = mlx5_set_trust_state(priv->mdev, *trust_state);
	if (err)
		return err;
	priv->dcbx_dp.trust_state = *trust_state;

	return 0;
}

static int mlx5e_set_trust_state(struct mlx5e_priv *priv, u8 trust_state)
{
	struct mlx5e_channels new_channels = {};
	bool reset_channels = true;
	bool opened;
	int err = 0;

	mutex_lock(&priv->state_lock);

	new_channels.params = priv->channels.params;
	mlx5e_params_calc_trust_tx_min_inline_mode(priv->mdev, &new_channels.params,
						   trust_state);

	opened = test_bit(MLX5E_STATE_OPENED, &priv->state);
	if (!opened)
		reset_channels = false;

	/* Skip if tx_min_inline is the same */
	if (new_channels.params.tx_min_inline_mode ==
	    priv->channels.params.tx_min_inline_mode)
		reset_channels = false;

	if (reset_channels) {
		err = mlx5e_safe_switch_channels(priv, &new_channels,
						 mlx5e_update_trust_state_hw,
						 &trust_state);
	} else {
		err = mlx5e_update_trust_state_hw(priv, &trust_state);
		if (!err && !opened)
			priv->channels.params = new_channels.params;
	}

	mutex_unlock(&priv->state_lock);

	return err;
}

static int mlx5e_set_dscp2prio(struct mlx5e_priv *priv, u8 dscp, u8 prio)
{
	int err;

	err = mlx5_set_dscp2prio(priv->mdev, dscp, prio);
	if (err)
		return err;

	priv->dcbx_dp.dscp2prio[dscp] = prio;
	return err;
}

static int mlx5e_trust_initialize(struct mlx5e_priv *priv)
{
	struct mlx5_core_dev *mdev = priv->mdev;
	int err;

	priv->dcbx_dp.trust_state = MLX5_QPTS_TRUST_PCP;

	if (!MLX5_DSCP_SUPPORTED(mdev))
		return 0;

	err = mlx5_query_trust_state(priv->mdev, &priv->dcbx_dp.trust_state);
	if (err)
		return err;

	mlx5e_params_calc_trust_tx_min_inline_mode(priv->mdev, &priv->channels.params,
						   priv->dcbx_dp.trust_state);

	err = mlx5_query_dscp2prio(priv->mdev, priv->dcbx_dp.dscp2prio);
	if (err)
		return err;

	return 0;
}

#define MLX5E_BUFFER_CELL_SHIFT 7

static u16 mlx5e_query_port_buffers_cell_size(struct mlx5e_priv *priv)
{
	struct mlx5_core_dev *mdev = priv->mdev;
	u32 out[MLX5_ST_SZ_DW(sbcam_reg)] = {};
	u32 in[MLX5_ST_SZ_DW(sbcam_reg)] = {};

	if (!MLX5_CAP_GEN(mdev, sbcam_reg))
		return (1 << MLX5E_BUFFER_CELL_SHIFT);

	if (mlx5_core_access_reg(mdev, in, sizeof(in), out, sizeof(out),
				 MLX5_REG_SBCAM, 0, 0))
		return (1 << MLX5E_BUFFER_CELL_SHIFT);

	return MLX5_GET(sbcam_reg, out, cap_cell_size);
}

void mlx5e_dcbnl_initialize(struct mlx5e_priv *priv)
{
	struct mlx5e_dcbx *dcbx = &priv->dcbx;

	mlx5e_trust_initialize(priv);

	if (!MLX5_CAP_GEN(priv->mdev, qos))
		return;

	if (MLX5_CAP_GEN(priv->mdev, dcbx))
		mlx5e_dcbnl_query_dcbx_mode(priv, &dcbx->mode);

	priv->dcbx.cap = DCB_CAP_DCBX_VER_CEE |
			 DCB_CAP_DCBX_VER_IEEE;
	if (priv->dcbx.mode == MLX5E_DCBX_PARAM_VER_OPER_HOST)
		priv->dcbx.cap |= DCB_CAP_DCBX_HOST;

	priv->dcbx.port_buff_cell_sz = mlx5e_query_port_buffers_cell_size(priv);
	priv->dcbx.manual_buffer = false;
	priv->dcbx.cable_len = MLX5E_DEFAULT_CABLE_LEN;

	mlx5e_ets_init(priv);
}
