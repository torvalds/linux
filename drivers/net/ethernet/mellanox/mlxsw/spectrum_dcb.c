// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/* Copyright (c) 2016-2018 Mellanox Technologies. All rights reserved */

#include <linux/netdevice.h>
#include <linux/string.h>
#include <linux/bitops.h>
#include <net/dcbnl.h>

#include "spectrum.h"
#include "reg.h"

static u8 mlxsw_sp_dcbnl_getdcbx(struct net_device __always_unused *dev)
{
	return DCB_CAP_DCBX_HOST | DCB_CAP_DCBX_VER_IEEE;
}

static u8 mlxsw_sp_dcbnl_setdcbx(struct net_device __always_unused *dev,
				 u8 mode)
{
	return (mode != (DCB_CAP_DCBX_HOST | DCB_CAP_DCBX_VER_IEEE)) ? 1 : 0;
}

static int mlxsw_sp_dcbnl_ieee_getets(struct net_device *dev,
				      struct ieee_ets *ets)
{
	struct mlxsw_sp_port *mlxsw_sp_port = netdev_priv(dev);

	memcpy(ets, mlxsw_sp_port->dcb.ets, sizeof(*ets));

	return 0;
}

static int mlxsw_sp_port_ets_validate(struct mlxsw_sp_port *mlxsw_sp_port,
				      struct ieee_ets *ets)
{
	struct net_device *dev = mlxsw_sp_port->dev;
	bool has_ets_tc = false;
	int i, tx_bw_sum = 0;

	for (i = 0; i < IEEE_8021QAZ_MAX_TCS; i++) {
		switch (ets->tc_tsa[i]) {
		case IEEE_8021QAZ_TSA_STRICT:
			break;
		case IEEE_8021QAZ_TSA_ETS:
			has_ets_tc = true;
			tx_bw_sum += ets->tc_tx_bw[i];
			break;
		default:
			netdev_err(dev, "Only strict priority and ETS are supported\n");
			return -EINVAL;
		}

		if (ets->prio_tc[i] >= IEEE_8021QAZ_MAX_TCS) {
			netdev_err(dev, "Invalid TC\n");
			return -EINVAL;
		}
	}

	if (has_ets_tc && tx_bw_sum != 100) {
		netdev_err(dev, "Total ETS bandwidth should equal 100\n");
		return -EINVAL;
	}

	return 0;
}

static int mlxsw_sp_port_pg_prio_map(struct mlxsw_sp_port *mlxsw_sp_port,
				     u8 *prio_tc)
{
	char pptb_pl[MLXSW_REG_PPTB_LEN];
	int i;

	mlxsw_reg_pptb_pack(pptb_pl, mlxsw_sp_port->local_port);
	for (i = 0; i < IEEE_8021QAZ_MAX_TCS; i++)
		mlxsw_reg_pptb_prio_to_buff_pack(pptb_pl, i, prio_tc[i]);

	return mlxsw_reg_write(mlxsw_sp_port->mlxsw_sp->core, MLXSW_REG(pptb),
			       pptb_pl);
}

static bool mlxsw_sp_ets_has_pg(u8 *prio_tc, u8 pg)
{
	int i;

	for (i = 0; i < IEEE_8021QAZ_MAX_TCS; i++)
		if (prio_tc[i] == pg)
			return true;
	return false;
}

static int mlxsw_sp_port_pg_destroy(struct mlxsw_sp_port *mlxsw_sp_port,
				    u8 *old_prio_tc, u8 *new_prio_tc)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	char pbmc_pl[MLXSW_REG_PBMC_LEN];
	int err, i;

	mlxsw_reg_pbmc_pack(pbmc_pl, mlxsw_sp_port->local_port, 0, 0);
	err = mlxsw_reg_query(mlxsw_sp->core, MLXSW_REG(pbmc), pbmc_pl);
	if (err)
		return err;

	for (i = 0; i < IEEE_8021QAZ_MAX_TCS; i++) {
		u8 pg = old_prio_tc[i];

		if (!mlxsw_sp_ets_has_pg(new_prio_tc, pg))
			mlxsw_reg_pbmc_lossy_buffer_pack(pbmc_pl, pg, 0);
	}

	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(pbmc), pbmc_pl);
}

static int mlxsw_sp_port_headroom_set(struct mlxsw_sp_port *mlxsw_sp_port,
				      struct ieee_ets *ets)
{
	bool pause_en = mlxsw_sp_port_is_pause_en(mlxsw_sp_port);
	struct ieee_ets *my_ets = mlxsw_sp_port->dcb.ets;
	struct net_device *dev = mlxsw_sp_port->dev;
	int err;

	/* Create the required PGs, but don't destroy existing ones, as
	 * traffic is still directed to them.
	 */
	err = __mlxsw_sp_port_headroom_set(mlxsw_sp_port, dev->mtu,
					   ets->prio_tc, pause_en,
					   mlxsw_sp_port->dcb.pfc);
	if (err) {
		netdev_err(dev, "Failed to configure port's headroom\n");
		return err;
	}

	err = mlxsw_sp_port_pg_prio_map(mlxsw_sp_port, ets->prio_tc);
	if (err) {
		netdev_err(dev, "Failed to set PG-priority mapping\n");
		goto err_port_prio_pg_map;
	}

	err = mlxsw_sp_port_pg_destroy(mlxsw_sp_port, my_ets->prio_tc,
				       ets->prio_tc);
	if (err)
		netdev_warn(dev, "Failed to remove ununsed PGs\n");

	return 0;

err_port_prio_pg_map:
	mlxsw_sp_port_pg_destroy(mlxsw_sp_port, ets->prio_tc, my_ets->prio_tc);
	return err;
}

static int __mlxsw_sp_dcbnl_ieee_setets(struct mlxsw_sp_port *mlxsw_sp_port,
					struct ieee_ets *ets)
{
	struct ieee_ets *my_ets = mlxsw_sp_port->dcb.ets;
	struct net_device *dev = mlxsw_sp_port->dev;
	int i, err;

	/* Egress configuration. */
	for (i = 0; i < IEEE_8021QAZ_MAX_TCS; i++) {
		bool dwrr = ets->tc_tsa[i] == IEEE_8021QAZ_TSA_ETS;
		u8 weight = ets->tc_tx_bw[i];

		err = mlxsw_sp_port_ets_set(mlxsw_sp_port,
					    MLXSW_REG_QEEC_HR_SUBGROUP, i,
					    0, dwrr, weight);
		if (err) {
			netdev_err(dev, "Failed to link subgroup ETS element %d to group\n",
				   i);
			goto err_port_ets_set;
		}
	}

	for (i = 0; i < IEEE_8021QAZ_MAX_TCS; i++) {
		err = mlxsw_sp_port_prio_tc_set(mlxsw_sp_port, i,
						ets->prio_tc[i]);
		if (err) {
			netdev_err(dev, "Failed to map prio %d to TC %d\n", i,
				   ets->prio_tc[i]);
			goto err_port_prio_tc_set;
		}
	}

	/* Ingress configuration. */
	err = mlxsw_sp_port_headroom_set(mlxsw_sp_port, ets);
	if (err)
		goto err_port_headroom_set;

	return 0;

err_port_headroom_set:
	i = IEEE_8021QAZ_MAX_TCS;
err_port_prio_tc_set:
	for (i--; i >= 0; i--)
		mlxsw_sp_port_prio_tc_set(mlxsw_sp_port, i, my_ets->prio_tc[i]);
	i = IEEE_8021QAZ_MAX_TCS;
err_port_ets_set:
	for (i--; i >= 0; i--) {
		bool dwrr = my_ets->tc_tsa[i] == IEEE_8021QAZ_TSA_ETS;
		u8 weight = my_ets->tc_tx_bw[i];

		err = mlxsw_sp_port_ets_set(mlxsw_sp_port,
					    MLXSW_REG_QEEC_HR_SUBGROUP, i,
					    0, dwrr, weight);
	}
	return err;
}

static int mlxsw_sp_dcbnl_ieee_setets(struct net_device *dev,
				      struct ieee_ets *ets)
{
	struct mlxsw_sp_port *mlxsw_sp_port = netdev_priv(dev);
	int err;

	err = mlxsw_sp_port_ets_validate(mlxsw_sp_port, ets);
	if (err)
		return err;

	err = __mlxsw_sp_dcbnl_ieee_setets(mlxsw_sp_port, ets);
	if (err)
		return err;

	memcpy(mlxsw_sp_port->dcb.ets, ets, sizeof(*ets));
	mlxsw_sp_port->dcb.ets->ets_cap = IEEE_8021QAZ_MAX_TCS;

	return 0;
}

static int mlxsw_sp_dcbnl_app_validate(struct net_device *dev,
				       struct dcb_app *app)
{
	int prio;

	if (app->priority >= IEEE_8021QAZ_MAX_TCS) {
		netdev_err(dev, "APP entry with priority value %u is invalid\n",
			   app->priority);
		return -EINVAL;
	}

	switch (app->selector) {
	case IEEE_8021QAZ_APP_SEL_DSCP:
		if (app->protocol >= 64) {
			netdev_err(dev, "DSCP APP entry with protocol value %u is invalid\n",
				   app->protocol);
			return -EINVAL;
		}

		/* Warn about any DSCP APP entries with the same PID. */
		prio = fls(dcb_ieee_getapp_mask(dev, app));
		if (prio--) {
			if (prio < app->priority)
				netdev_warn(dev, "Choosing priority %d for DSCP %d in favor of previously-active value of %d\n",
					    app->priority, app->protocol, prio);
			else if (prio > app->priority)
				netdev_warn(dev, "Ignoring new priority %d for DSCP %d in favor of current value of %d\n",
					    app->priority, app->protocol, prio);
		}
		break;

	case IEEE_8021QAZ_APP_SEL_ETHERTYPE:
		if (app->protocol) {
			netdev_err(dev, "EtherType APP entries with protocol value != 0 not supported\n");
			return -EINVAL;
		}
		break;

	default:
		netdev_err(dev, "APP entries with selector %u not supported\n",
			   app->selector);
		return -EINVAL;
	}

	return 0;
}

static u8
mlxsw_sp_port_dcb_app_default_prio(struct mlxsw_sp_port *mlxsw_sp_port)
{
	u8 prio_mask;

	prio_mask = dcb_ieee_getapp_default_prio_mask(mlxsw_sp_port->dev);
	if (prio_mask)
		/* Take the highest configured priority. */
		return fls(prio_mask) - 1;

	return 0;
}

static void
mlxsw_sp_port_dcb_app_dscp_prio_map(struct mlxsw_sp_port *mlxsw_sp_port,
				    u8 default_prio,
				    struct dcb_ieee_app_dscp_map *map)
{
	int i;

	dcb_ieee_getapp_dscp_prio_mask_map(mlxsw_sp_port->dev, map);
	for (i = 0; i < ARRAY_SIZE(map->map); ++i) {
		if (map->map[i])
			map->map[i] = fls(map->map[i]) - 1;
		else
			map->map[i] = default_prio;
	}
}

static bool
mlxsw_sp_port_dcb_app_prio_dscp_map(struct mlxsw_sp_port *mlxsw_sp_port,
				    struct dcb_ieee_app_prio_map *map)
{
	bool have_dscp = false;
	int i;

	dcb_ieee_getapp_prio_dscp_mask_map(mlxsw_sp_port->dev, map);
	for (i = 0; i < ARRAY_SIZE(map->map); ++i) {
		if (map->map[i]) {
			map->map[i] = fls64(map->map[i]) - 1;
			have_dscp = true;
		}
	}

	return have_dscp;
}

static int
mlxsw_sp_port_dcb_app_update_qpts(struct mlxsw_sp_port *mlxsw_sp_port,
				  enum mlxsw_reg_qpts_trust_state ts)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	char qpts_pl[MLXSW_REG_QPTS_LEN];

	mlxsw_reg_qpts_pack(qpts_pl, mlxsw_sp_port->local_port, ts);
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(qpts), qpts_pl);
}

static int
mlxsw_sp_port_dcb_app_update_qrwe(struct mlxsw_sp_port *mlxsw_sp_port,
				  bool rewrite_dscp)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	char qrwe_pl[MLXSW_REG_QRWE_LEN];

	mlxsw_reg_qrwe_pack(qrwe_pl, mlxsw_sp_port->local_port,
			    false, rewrite_dscp);
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(qrwe), qrwe_pl);
}

static int
mlxsw_sp_port_dcb_toggle_trust(struct mlxsw_sp_port *mlxsw_sp_port,
			       enum mlxsw_reg_qpts_trust_state ts)
{
	bool rewrite_dscp = ts == MLXSW_REG_QPTS_TRUST_STATE_DSCP;
	int err;

	if (mlxsw_sp_port->dcb.trust_state == ts)
		return 0;

	err = mlxsw_sp_port_dcb_app_update_qpts(mlxsw_sp_port, ts);
	if (err)
		return err;

	err = mlxsw_sp_port_dcb_app_update_qrwe(mlxsw_sp_port, rewrite_dscp);
	if (err)
		goto err_update_qrwe;

	mlxsw_sp_port->dcb.trust_state = ts;
	return 0;

err_update_qrwe:
	mlxsw_sp_port_dcb_app_update_qpts(mlxsw_sp_port,
					  mlxsw_sp_port->dcb.trust_state);
	return err;
}

static int
mlxsw_sp_port_dcb_app_update_qpdp(struct mlxsw_sp_port *mlxsw_sp_port,
				  u8 default_prio)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	char qpdp_pl[MLXSW_REG_QPDP_LEN];

	mlxsw_reg_qpdp_pack(qpdp_pl, mlxsw_sp_port->local_port, default_prio);
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(qpdp), qpdp_pl);
}

static int
mlxsw_sp_port_dcb_app_update_qpdpm(struct mlxsw_sp_port *mlxsw_sp_port,
				   struct dcb_ieee_app_dscp_map *map)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	char qpdpm_pl[MLXSW_REG_QPDPM_LEN];
	short int i;

	mlxsw_reg_qpdpm_pack(qpdpm_pl, mlxsw_sp_port->local_port);
	for (i = 0; i < ARRAY_SIZE(map->map); ++i)
		mlxsw_reg_qpdpm_dscp_pack(qpdpm_pl, i, map->map[i]);
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(qpdpm), qpdpm_pl);
}

static int
mlxsw_sp_port_dcb_app_update_qpdsm(struct mlxsw_sp_port *mlxsw_sp_port,
				   struct dcb_ieee_app_prio_map *map)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	char qpdsm_pl[MLXSW_REG_QPDSM_LEN];
	short int i;

	mlxsw_reg_qpdsm_pack(qpdsm_pl, mlxsw_sp_port->local_port);
	for (i = 0; i < ARRAY_SIZE(map->map); ++i)
		mlxsw_reg_qpdsm_prio_pack(qpdsm_pl, i, map->map[i]);
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(qpdsm), qpdsm_pl);
}

static int mlxsw_sp_port_dcb_app_update(struct mlxsw_sp_port *mlxsw_sp_port)
{
	struct dcb_ieee_app_prio_map prio_map;
	struct dcb_ieee_app_dscp_map dscp_map;
	u8 default_prio;
	bool have_dscp;
	int err;

	default_prio = mlxsw_sp_port_dcb_app_default_prio(mlxsw_sp_port);
	err = mlxsw_sp_port_dcb_app_update_qpdp(mlxsw_sp_port, default_prio);
	if (err) {
		netdev_err(mlxsw_sp_port->dev, "Couldn't configure port default priority\n");
		return err;
	}

	have_dscp = mlxsw_sp_port_dcb_app_prio_dscp_map(mlxsw_sp_port,
							&prio_map);

	mlxsw_sp_port_dcb_app_dscp_prio_map(mlxsw_sp_port, default_prio,
					    &dscp_map);
	err = mlxsw_sp_port_dcb_app_update_qpdpm(mlxsw_sp_port,
						 &dscp_map);
	if (err) {
		netdev_err(mlxsw_sp_port->dev, "Couldn't configure priority map\n");
		return err;
	}

	err = mlxsw_sp_port_dcb_app_update_qpdsm(mlxsw_sp_port,
						 &prio_map);
	if (err) {
		netdev_err(mlxsw_sp_port->dev, "Couldn't configure DSCP rewrite map\n");
		return err;
	}

	if (!have_dscp) {
		err = mlxsw_sp_port_dcb_toggle_trust(mlxsw_sp_port,
					MLXSW_REG_QPTS_TRUST_STATE_PCP);
		if (err)
			netdev_err(mlxsw_sp_port->dev, "Couldn't switch to trust L2\n");
		return err;
	}

	err = mlxsw_sp_port_dcb_toggle_trust(mlxsw_sp_port,
					     MLXSW_REG_QPTS_TRUST_STATE_DSCP);
	if (err) {
		/* A failure to set trust DSCP means that the QPDPM and QPDSM
		 * maps installed above are not in effect. And since we are here
		 * attempting to set trust DSCP, we couldn't have attempted to
		 * switch trust to PCP. Thus no cleanup is necessary.
		 */
		netdev_err(mlxsw_sp_port->dev, "Couldn't switch to trust L3\n");
		return err;
	}

	return 0;
}

static int mlxsw_sp_dcbnl_ieee_setapp(struct net_device *dev,
				      struct dcb_app *app)
{
	struct mlxsw_sp_port *mlxsw_sp_port = netdev_priv(dev);
	int err;

	err = mlxsw_sp_dcbnl_app_validate(dev, app);
	if (err)
		return err;

	err = dcb_ieee_setapp(dev, app);
	if (err)
		return err;

	err = mlxsw_sp_port_dcb_app_update(mlxsw_sp_port);
	if (err)
		goto err_update;

	return 0;

err_update:
	dcb_ieee_delapp(dev, app);
	return err;
}

static int mlxsw_sp_dcbnl_ieee_delapp(struct net_device *dev,
				      struct dcb_app *app)
{
	struct mlxsw_sp_port *mlxsw_sp_port = netdev_priv(dev);
	int err;

	err = dcb_ieee_delapp(dev, app);
	if (err)
		return err;

	err = mlxsw_sp_port_dcb_app_update(mlxsw_sp_port);
	if (err)
		netdev_err(dev, "Failed to update DCB APP configuration\n");
	return 0;
}

static int mlxsw_sp_dcbnl_ieee_getmaxrate(struct net_device *dev,
					  struct ieee_maxrate *maxrate)
{
	struct mlxsw_sp_port *mlxsw_sp_port = netdev_priv(dev);

	memcpy(maxrate, mlxsw_sp_port->dcb.maxrate, sizeof(*maxrate));

	return 0;
}

static int mlxsw_sp_dcbnl_ieee_setmaxrate(struct net_device *dev,
					  struct ieee_maxrate *maxrate)
{
	struct mlxsw_sp_port *mlxsw_sp_port = netdev_priv(dev);
	struct ieee_maxrate *my_maxrate = mlxsw_sp_port->dcb.maxrate;
	int err, i;

	for (i = 0; i < IEEE_8021QAZ_MAX_TCS; i++) {
		err = mlxsw_sp_port_ets_maxrate_set(mlxsw_sp_port,
						    MLXSW_REG_QEEC_HR_SUBGROUP,
						    i, 0,
						    maxrate->tc_maxrate[i]);
		if (err) {
			netdev_err(dev, "Failed to set maxrate for TC %d\n", i);
			goto err_port_ets_maxrate_set;
		}
	}

	memcpy(mlxsw_sp_port->dcb.maxrate, maxrate, sizeof(*maxrate));

	return 0;

err_port_ets_maxrate_set:
	for (i--; i >= 0; i--)
		mlxsw_sp_port_ets_maxrate_set(mlxsw_sp_port,
					      MLXSW_REG_QEEC_HR_SUBGROUP,
					      i, 0, my_maxrate->tc_maxrate[i]);
	return err;
}

static int mlxsw_sp_port_pfc_cnt_get(struct mlxsw_sp_port *mlxsw_sp_port,
				     u8 prio)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	struct ieee_pfc *my_pfc = mlxsw_sp_port->dcb.pfc;
	char ppcnt_pl[MLXSW_REG_PPCNT_LEN];
	int err;

	mlxsw_reg_ppcnt_pack(ppcnt_pl, mlxsw_sp_port->local_port,
			     MLXSW_REG_PPCNT_PRIO_CNT, prio);
	err = mlxsw_reg_query(mlxsw_sp->core, MLXSW_REG(ppcnt), ppcnt_pl);
	if (err)
		return err;

	my_pfc->requests[prio] = mlxsw_reg_ppcnt_tx_pause_get(ppcnt_pl);
	my_pfc->indications[prio] = mlxsw_reg_ppcnt_rx_pause_get(ppcnt_pl);

	return 0;
}

static int mlxsw_sp_dcbnl_ieee_getpfc(struct net_device *dev,
				      struct ieee_pfc *pfc)
{
	struct mlxsw_sp_port *mlxsw_sp_port = netdev_priv(dev);
	int err, i;

	for (i = 0; i < IEEE_8021QAZ_MAX_TCS; i++) {
		err = mlxsw_sp_port_pfc_cnt_get(mlxsw_sp_port, i);
		if (err) {
			netdev_err(dev, "Failed to get PFC count for priority %d\n",
				   i);
			return err;
		}
	}

	memcpy(pfc, mlxsw_sp_port->dcb.pfc, sizeof(*pfc));

	return 0;
}

static int mlxsw_sp_port_pfc_set(struct mlxsw_sp_port *mlxsw_sp_port,
				 struct ieee_pfc *pfc)
{
	char pfcc_pl[MLXSW_REG_PFCC_LEN];

	mlxsw_reg_pfcc_pack(pfcc_pl, mlxsw_sp_port->local_port);
	mlxsw_reg_pfcc_pprx_set(pfcc_pl, mlxsw_sp_port->link.rx_pause);
	mlxsw_reg_pfcc_pptx_set(pfcc_pl, mlxsw_sp_port->link.tx_pause);
	mlxsw_reg_pfcc_prio_pack(pfcc_pl, pfc->pfc_en);

	return mlxsw_reg_write(mlxsw_sp_port->mlxsw_sp->core, MLXSW_REG(pfcc),
			       pfcc_pl);
}

static int mlxsw_sp_dcbnl_ieee_setpfc(struct net_device *dev,
				      struct ieee_pfc *pfc)
{
	struct mlxsw_sp_port *mlxsw_sp_port = netdev_priv(dev);
	bool pause_en = mlxsw_sp_port_is_pause_en(mlxsw_sp_port);
	int err;

	if (pause_en && pfc->pfc_en) {
		netdev_err(dev, "PAUSE frames already enabled on port\n");
		return -EINVAL;
	}

	err = __mlxsw_sp_port_headroom_set(mlxsw_sp_port, dev->mtu,
					   mlxsw_sp_port->dcb.ets->prio_tc,
					   pause_en, pfc);
	if (err) {
		netdev_err(dev, "Failed to configure port's headroom for PFC\n");
		return err;
	}

	err = mlxsw_sp_port_pfc_set(mlxsw_sp_port, pfc);
	if (err) {
		netdev_err(dev, "Failed to configure PFC\n");
		goto err_port_pfc_set;
	}

	memcpy(mlxsw_sp_port->dcb.pfc, pfc, sizeof(*pfc));
	mlxsw_sp_port->dcb.pfc->pfc_cap = IEEE_8021QAZ_MAX_TCS;

	return 0;

err_port_pfc_set:
	__mlxsw_sp_port_headroom_set(mlxsw_sp_port, dev->mtu,
				     mlxsw_sp_port->dcb.ets->prio_tc, pause_en,
				     mlxsw_sp_port->dcb.pfc);
	return err;
}

static const struct dcbnl_rtnl_ops mlxsw_sp_dcbnl_ops = {
	.ieee_getets		= mlxsw_sp_dcbnl_ieee_getets,
	.ieee_setets		= mlxsw_sp_dcbnl_ieee_setets,
	.ieee_getmaxrate	= mlxsw_sp_dcbnl_ieee_getmaxrate,
	.ieee_setmaxrate	= mlxsw_sp_dcbnl_ieee_setmaxrate,
	.ieee_getpfc		= mlxsw_sp_dcbnl_ieee_getpfc,
	.ieee_setpfc		= mlxsw_sp_dcbnl_ieee_setpfc,
	.ieee_setapp		= mlxsw_sp_dcbnl_ieee_setapp,
	.ieee_delapp		= mlxsw_sp_dcbnl_ieee_delapp,

	.getdcbx		= mlxsw_sp_dcbnl_getdcbx,
	.setdcbx		= mlxsw_sp_dcbnl_setdcbx,
};

static int mlxsw_sp_port_ets_init(struct mlxsw_sp_port *mlxsw_sp_port)
{
	mlxsw_sp_port->dcb.ets = kzalloc(sizeof(*mlxsw_sp_port->dcb.ets),
					 GFP_KERNEL);
	if (!mlxsw_sp_port->dcb.ets)
		return -ENOMEM;

	mlxsw_sp_port->dcb.ets->ets_cap = IEEE_8021QAZ_MAX_TCS;

	return 0;
}

static void mlxsw_sp_port_ets_fini(struct mlxsw_sp_port *mlxsw_sp_port)
{
	kfree(mlxsw_sp_port->dcb.ets);
}

static int mlxsw_sp_port_maxrate_init(struct mlxsw_sp_port *mlxsw_sp_port)
{
	int i;

	mlxsw_sp_port->dcb.maxrate = kmalloc(sizeof(*mlxsw_sp_port->dcb.maxrate),
					     GFP_KERNEL);
	if (!mlxsw_sp_port->dcb.maxrate)
		return -ENOMEM;

	for (i = 0; i < IEEE_8021QAZ_MAX_TCS; i++)
		mlxsw_sp_port->dcb.maxrate->tc_maxrate[i] = MLXSW_REG_QEEC_MAS_DIS;

	return 0;
}

static void mlxsw_sp_port_maxrate_fini(struct mlxsw_sp_port *mlxsw_sp_port)
{
	kfree(mlxsw_sp_port->dcb.maxrate);
}

static int mlxsw_sp_port_pfc_init(struct mlxsw_sp_port *mlxsw_sp_port)
{
	mlxsw_sp_port->dcb.pfc = kzalloc(sizeof(*mlxsw_sp_port->dcb.pfc),
					 GFP_KERNEL);
	if (!mlxsw_sp_port->dcb.pfc)
		return -ENOMEM;

	mlxsw_sp_port->dcb.pfc->pfc_cap = IEEE_8021QAZ_MAX_TCS;

	return 0;
}

static void mlxsw_sp_port_pfc_fini(struct mlxsw_sp_port *mlxsw_sp_port)
{
	kfree(mlxsw_sp_port->dcb.pfc);
}

int mlxsw_sp_port_dcb_init(struct mlxsw_sp_port *mlxsw_sp_port)
{
	int err;

	err = mlxsw_sp_port_ets_init(mlxsw_sp_port);
	if (err)
		return err;
	err = mlxsw_sp_port_maxrate_init(mlxsw_sp_port);
	if (err)
		goto err_port_maxrate_init;
	err = mlxsw_sp_port_pfc_init(mlxsw_sp_port);
	if (err)
		goto err_port_pfc_init;

	mlxsw_sp_port->dcb.trust_state = MLXSW_REG_QPTS_TRUST_STATE_PCP;
	mlxsw_sp_port->dev->dcbnl_ops = &mlxsw_sp_dcbnl_ops;

	return 0;

err_port_pfc_init:
	mlxsw_sp_port_maxrate_fini(mlxsw_sp_port);
err_port_maxrate_init:
	mlxsw_sp_port_ets_fini(mlxsw_sp_port);
	return err;
}

void mlxsw_sp_port_dcb_fini(struct mlxsw_sp_port *mlxsw_sp_port)
{
	mlxsw_sp_port_pfc_fini(mlxsw_sp_port);
	mlxsw_sp_port_maxrate_fini(mlxsw_sp_port);
	mlxsw_sp_port_ets_fini(mlxsw_sp_port);
}
