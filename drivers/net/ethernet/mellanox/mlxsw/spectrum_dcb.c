/*
 * drivers/net/ethernet/mellanox/mlxsw/spectrum_dcb.c
 * Copyright (c) 2016 Mellanox Technologies. All rights reserved.
 * Copyright (c) 2016 Ido Schimmel <idosch@mellanox.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the names of the copyright holders nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2 as published by the Free
 * Software Foundation.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

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
		mlxsw_reg_pptb_prio_to_buff_set(pptb_pl, i, prio_tc[i]);
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
					    MLXSW_REG_QEEC_HIERARCY_SUBGROUP, i,
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
					    MLXSW_REG_QEEC_HIERARCY_SUBGROUP, i,
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
						    MLXSW_REG_QEEC_HIERARCY_SUBGROUP,
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
					      MLXSW_REG_QEEC_HIERARCY_SUBGROUP,
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
	mlxsw_reg_pfcc_prio_pack(pfcc_pl, pfc->pfc_en);

	return mlxsw_reg_write(mlxsw_sp_port->mlxsw_sp->core, MLXSW_REG(pfcc),
			       pfcc_pl);
}

static int mlxsw_sp_dcbnl_ieee_setpfc(struct net_device *dev,
				      struct ieee_pfc *pfc)
{
	struct mlxsw_sp_port *mlxsw_sp_port = netdev_priv(dev);
	int err;

	if (mlxsw_sp_port->link.tx_pause || mlxsw_sp_port->link.rx_pause) {
		netdev_err(dev, "PAUSE frames already enabled on port\n");
		return -EINVAL;
	}

	err = __mlxsw_sp_port_headroom_set(mlxsw_sp_port, dev->mtu,
					   mlxsw_sp_port->dcb.ets->prio_tc,
					   false, pfc);
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

	return 0;

err_port_pfc_set:
	__mlxsw_sp_port_headroom_set(mlxsw_sp_port, dev->mtu,
				     mlxsw_sp_port->dcb.ets->prio_tc, false,
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
