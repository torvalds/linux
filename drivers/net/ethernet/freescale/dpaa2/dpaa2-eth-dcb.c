// SPDX-License-Identifier: (GPL-2.0+ OR BSD-3-Clause)
/* Copyright 2020 NXP */

#include "dpaa2-eth.h"

static int dpaa2_eth_dcbnl_ieee_getpfc(struct net_device *net_dev,
				       struct ieee_pfc *pfc)
{
	struct dpaa2_eth_priv *priv = netdev_priv(net_dev);

	if (!(priv->link_state.options & DPNI_LINK_OPT_PFC_PAUSE))
		return 0;

	memcpy(pfc, &priv->pfc, sizeof(priv->pfc));
	pfc->pfc_cap = dpaa2_eth_tc_count(priv);

	return 0;
}

static inline bool is_prio_enabled(u8 pfc_en, u8 tc)
{
	return !!(pfc_en & (1 << tc));
}

static int set_pfc_cn(struct dpaa2_eth_priv *priv, u8 pfc_en)
{
	struct dpni_congestion_notification_cfg cfg = {0};
	int i, err;

	cfg.notification_mode = DPNI_CONG_OPT_FLOW_CONTROL;
	cfg.units = DPNI_CONGESTION_UNIT_FRAMES;
	cfg.message_iova = 0ULL;
	cfg.message_ctx = 0ULL;

	for (i = 0; i < dpaa2_eth_tc_count(priv); i++) {
		if (is_prio_enabled(pfc_en, i)) {
			cfg.threshold_entry = DPAA2_ETH_CN_THRESH_ENTRY(priv);
			cfg.threshold_exit = DPAA2_ETH_CN_THRESH_EXIT(priv);
		} else {
			/* For priorities not set in the pfc_en mask, we leave
			 * the congestion thresholds at zero, which effectively
			 * disables generation of PFC frames for them
			 */
			cfg.threshold_entry = 0;
			cfg.threshold_exit = 0;
		}

		err = dpni_set_congestion_notification(priv->mc_io, 0,
						       priv->mc_token,
						       DPNI_QUEUE_RX, i, &cfg);
		if (err) {
			netdev_err(priv->net_dev,
				   "dpni_set_congestion_notification failed\n");
			return err;
		}
	}

	return 0;
}

static int dpaa2_eth_dcbnl_ieee_setpfc(struct net_device *net_dev,
				       struct ieee_pfc *pfc)
{
	struct dpaa2_eth_priv *priv = netdev_priv(net_dev);
	struct dpni_link_cfg link_cfg = {0};
	bool tx_pause;
	int err;

	if (pfc->mbc || pfc->delay)
		return -EOPNOTSUPP;

	/* If same PFC enabled mask, nothing to do */
	if (priv->pfc.pfc_en == pfc->pfc_en)
		return 0;

	/* We allow PFC configuration even if it won't have any effect until
	 * general pause frames are enabled
	 */
	tx_pause = dpaa2_eth_tx_pause_enabled(priv->link_state.options);
	if (!dpaa2_eth_rx_pause_enabled(priv->link_state.options) || !tx_pause)
		netdev_warn(net_dev, "Pause support must be enabled in order for PFC to work!\n");

	link_cfg.rate = priv->link_state.rate;
	link_cfg.options = priv->link_state.options;
	if (pfc->pfc_en)
		link_cfg.options |= DPNI_LINK_OPT_PFC_PAUSE;
	else
		link_cfg.options &= ~DPNI_LINK_OPT_PFC_PAUSE;
	err = dpni_set_link_cfg(priv->mc_io, 0, priv->mc_token, &link_cfg);
	if (err) {
		netdev_err(net_dev, "dpni_set_link_cfg failed\n");
		return err;
	}

	/* Configure congestion notifications for the enabled priorities */
	err = set_pfc_cn(priv, pfc->pfc_en);
	if (err)
		return err;

	memcpy(&priv->pfc, pfc, sizeof(priv->pfc));
	priv->pfc_enabled = !!pfc->pfc_en;

	dpaa2_eth_set_rx_taildrop(priv, tx_pause, priv->pfc_enabled);

	return 0;
}

static u8 dpaa2_eth_dcbnl_getdcbx(struct net_device *net_dev)
{
	struct dpaa2_eth_priv *priv = netdev_priv(net_dev);

	return priv->dcbx_mode;
}

static u8 dpaa2_eth_dcbnl_setdcbx(struct net_device *net_dev, u8 mode)
{
	struct dpaa2_eth_priv *priv = netdev_priv(net_dev);

	return (mode != (priv->dcbx_mode)) ? 1 : 0;
}

static u8 dpaa2_eth_dcbnl_getcap(struct net_device *net_dev, int capid, u8 *cap)
{
	struct dpaa2_eth_priv *priv = netdev_priv(net_dev);

	switch (capid) {
	case DCB_CAP_ATTR_PFC:
		*cap = true;
		break;
	case DCB_CAP_ATTR_PFC_TCS:
		*cap = 1 << (dpaa2_eth_tc_count(priv) - 1);
		break;
	case DCB_CAP_ATTR_DCBX:
		*cap = priv->dcbx_mode;
		break;
	default:
		*cap = false;
		break;
	}

	return 0;
}

const struct dcbnl_rtnl_ops dpaa2_eth_dcbnl_ops = {
	.ieee_getpfc	= dpaa2_eth_dcbnl_ieee_getpfc,
	.ieee_setpfc	= dpaa2_eth_dcbnl_ieee_setpfc,
	.getdcbx	= dpaa2_eth_dcbnl_getdcbx,
	.setdcbx	= dpaa2_eth_dcbnl_setdcbx,
	.getcap		= dpaa2_eth_dcbnl_getcap,
};
