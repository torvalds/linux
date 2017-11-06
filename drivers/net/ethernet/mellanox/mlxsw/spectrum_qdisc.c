/*
 * drivers/net/ethernet/mellanox/mlxsw/spectrum_qdisc.c
 * Copyright (c) 2017 Mellanox Technologies. All rights reserved.
 * Copyright (c) 2017 Nogah Frankel <nogahf@mellanox.com>
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

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/netdevice.h>
#include <net/pkt_cls.h>
#include <net/red.h>

#include "spectrum.h"
#include "reg.h"

static int
mlxsw_sp_tclass_congestion_enable(struct mlxsw_sp_port *mlxsw_sp_port,
				  int tclass_num, u32 min, u32 max,
				  u32 probability, bool is_ecn)
{
	char cwtp_cmd[max_t(u8, MLXSW_REG_CWTP_LEN, MLXSW_REG_CWTPM_LEN)];
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	int err;

	mlxsw_reg_cwtp_pack(cwtp_cmd, mlxsw_sp_port->local_port, tclass_num);
	mlxsw_reg_cwtp_profile_pack(cwtp_cmd, MLXSW_REG_CWTP_DEFAULT_PROFILE,
				    roundup(min, MLXSW_REG_CWTP_MIN_VALUE),
				    roundup(max, MLXSW_REG_CWTP_MIN_VALUE),
				    probability);

	err = mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(cwtp), cwtp_cmd);
	if (err)
		return err;

	mlxsw_reg_cwtpm_pack(cwtp_cmd, mlxsw_sp_port->local_port, tclass_num,
			     MLXSW_REG_CWTP_DEFAULT_PROFILE, true, is_ecn);

	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(cwtpm), cwtp_cmd);
}

static int
mlxsw_sp_tclass_congestion_disable(struct mlxsw_sp_port *mlxsw_sp_port,
				   int tclass_num)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	char cwtpm_cmd[MLXSW_REG_CWTPM_LEN];

	mlxsw_reg_cwtpm_pack(cwtpm_cmd, mlxsw_sp_port->local_port, tclass_num,
			     MLXSW_REG_CWTPM_RESET_PROFILE, false, false);
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(cwtpm), cwtpm_cmd);
}

static void
mlxsw_sp_setup_tc_qdisc_clean_stats(struct mlxsw_sp_port *mlxsw_sp_port,
				    struct mlxsw_sp_qdisc *mlxsw_sp_qdisc,
				    int tclass_num)
{
	struct red_stats *xstats_base = &mlxsw_sp_qdisc->xstats_base;
	struct mlxsw_sp_port_xstats *xstats;
	struct rtnl_link_stats64 *stats;

	xstats = &mlxsw_sp_port->periodic_hw_stats.xstats;
	stats = &mlxsw_sp_port->periodic_hw_stats.stats;

	mlxsw_sp_qdisc->tx_packets = stats->tx_packets;
	mlxsw_sp_qdisc->tx_bytes = stats->tx_bytes;

	switch (mlxsw_sp_qdisc->type) {
	case MLXSW_SP_QDISC_RED:
		xstats_base->prob_mark = xstats->ecn;
		xstats_base->prob_drop = xstats->wred_drop[tclass_num];
		xstats_base->pdrop = xstats->tail_drop[tclass_num];

		mlxsw_sp_qdisc->overlimits = xstats_base->prob_drop +
					     xstats_base->prob_mark;
		mlxsw_sp_qdisc->drops = xstats_base->prob_drop +
					xstats_base->pdrop;
		break;
	default:
		break;
	}
}

static int
mlxsw_sp_qdisc_red_destroy(struct mlxsw_sp_port *mlxsw_sp_port, u32 handle,
			   struct mlxsw_sp_qdisc *mlxsw_sp_qdisc,
			   int tclass_num)
{
	int err;

	if (mlxsw_sp_qdisc->handle != handle)
		return 0;

	err = mlxsw_sp_tclass_congestion_disable(mlxsw_sp_port, tclass_num);
	mlxsw_sp_qdisc->handle = TC_H_UNSPEC;
	mlxsw_sp_qdisc->type = MLXSW_SP_QDISC_NO_QDISC;

	return err;
}

static int
mlxsw_sp_qdisc_red_replace(struct mlxsw_sp_port *mlxsw_sp_port, u32 handle,
			   struct mlxsw_sp_qdisc *mlxsw_sp_qdisc,
			   int tclass_num,
			   struct tc_red_qopt_offload_params *p)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	u32 min, max;
	u64 prob;
	int err = 0;

	if (p->min > p->max) {
		dev_err(mlxsw_sp->bus_info->dev,
			"spectrum: RED: min %u is bigger then max %u\n", p->min,
			p->max);
		goto err_bad_param;
	}
	if (p->max > MLXSW_CORE_RES_GET(mlxsw_sp->core, MAX_BUFFER_SIZE)) {
		dev_err(mlxsw_sp->bus_info->dev,
			"spectrum: RED: max value %u is too big\n", p->max);
		goto err_bad_param;
	}
	if (p->min == 0 || p->max == 0) {
		dev_err(mlxsw_sp->bus_info->dev,
			"spectrum: RED: 0 value is illegal for min and max\n");
		goto err_bad_param;
	}

	/* calculate probability in percentage */
	prob = p->probability;
	prob *= 100;
	prob = DIV_ROUND_UP(prob, 1 << 16);
	prob = DIV_ROUND_UP(prob, 1 << 16);
	min = mlxsw_sp_bytes_cells(mlxsw_sp, p->min);
	max = mlxsw_sp_bytes_cells(mlxsw_sp, p->max);
	err = mlxsw_sp_tclass_congestion_enable(mlxsw_sp_port, tclass_num, min,
						max, prob, p->is_ecn);
	if (err)
		goto err_config;

	mlxsw_sp_qdisc->type = MLXSW_SP_QDISC_RED;
	if (mlxsw_sp_qdisc->handle != handle)
		mlxsw_sp_setup_tc_qdisc_clean_stats(mlxsw_sp_port,
						    mlxsw_sp_qdisc,
						    tclass_num);

	mlxsw_sp_qdisc->handle = handle;
	return 0;

err_bad_param:
	err = -EINVAL;
err_config:
	mlxsw_sp_qdisc_red_destroy(mlxsw_sp_port, mlxsw_sp_qdisc->handle,
				   mlxsw_sp_qdisc, tclass_num);
	return err;
}

static int
mlxsw_sp_qdisc_get_red_xstats(struct mlxsw_sp_port *mlxsw_sp_port, u32 handle,
			      struct mlxsw_sp_qdisc *mlxsw_sp_qdisc,
			      int tclass_num, struct red_stats *res)
{
	struct red_stats *xstats_base = &mlxsw_sp_qdisc->xstats_base;
	struct mlxsw_sp_port_xstats *xstats;

	if (mlxsw_sp_qdisc->handle != handle ||
	    mlxsw_sp_qdisc->type != MLXSW_SP_QDISC_RED)
		return -EOPNOTSUPP;

	xstats = &mlxsw_sp_port->periodic_hw_stats.xstats;

	res->prob_drop = xstats->wred_drop[tclass_num] - xstats_base->prob_drop;
	res->prob_mark = xstats->ecn - xstats_base->prob_mark;
	res->pdrop = xstats->tail_drop[tclass_num] - xstats_base->pdrop;
	return 0;
}

static int
mlxsw_sp_qdisc_get_red_stats(struct mlxsw_sp_port *mlxsw_sp_port, u32 handle,
			     struct mlxsw_sp_qdisc *mlxsw_sp_qdisc,
			     int tclass_num,
			     struct tc_red_qopt_offload_stats *res)
{
	u64 tx_bytes, tx_packets, overlimits, drops;
	struct mlxsw_sp_port_xstats *xstats;
	struct rtnl_link_stats64 *stats;

	if (mlxsw_sp_qdisc->handle != handle ||
	    mlxsw_sp_qdisc->type != MLXSW_SP_QDISC_RED)
		return -EOPNOTSUPP;

	xstats = &mlxsw_sp_port->periodic_hw_stats.xstats;
	stats = &mlxsw_sp_port->periodic_hw_stats.stats;

	tx_bytes = stats->tx_bytes - mlxsw_sp_qdisc->tx_bytes;
	tx_packets = stats->tx_packets - mlxsw_sp_qdisc->tx_packets;
	overlimits = xstats->wred_drop[tclass_num] + xstats->ecn -
		     mlxsw_sp_qdisc->overlimits;
	drops = xstats->wred_drop[tclass_num] + xstats->tail_drop[tclass_num] -
		mlxsw_sp_qdisc->drops;

	_bstats_update(res->bstats, tx_bytes, tx_packets);
	res->qstats->overlimits += overlimits;
	res->qstats->drops += drops;
	res->qstats->backlog += mlxsw_sp_cells_bytes(mlxsw_sp_port->mlxsw_sp,
						xstats->backlog[tclass_num]);

	mlxsw_sp_qdisc->drops +=  drops;
	mlxsw_sp_qdisc->overlimits += overlimits;
	mlxsw_sp_qdisc->tx_bytes += tx_bytes;
	mlxsw_sp_qdisc->tx_packets += tx_packets;
	return 0;
}

#define MLXSW_SP_PORT_DEFAULT_TCLASS 0

int mlxsw_sp_setup_tc_red(struct mlxsw_sp_port *mlxsw_sp_port,
			  struct tc_red_qopt_offload *p)
{
	struct mlxsw_sp_qdisc *mlxsw_sp_qdisc;
	int tclass_num;

	if (p->parent != TC_H_ROOT)
		return -EOPNOTSUPP;

	mlxsw_sp_qdisc = &mlxsw_sp_port->root_qdisc;
	tclass_num = MLXSW_SP_PORT_DEFAULT_TCLASS;

	switch (p->command) {
	case TC_RED_REPLACE:
		return mlxsw_sp_qdisc_red_replace(mlxsw_sp_port, p->handle,
						  mlxsw_sp_qdisc, tclass_num,
						  &p->set);
	case TC_RED_DESTROY:
		return mlxsw_sp_qdisc_red_destroy(mlxsw_sp_port, p->handle,
						  mlxsw_sp_qdisc, tclass_num);
	case TC_RED_XSTATS:
		return mlxsw_sp_qdisc_get_red_xstats(mlxsw_sp_port, p->handle,
						     mlxsw_sp_qdisc, tclass_num,
						     p->xstats);
	case TC_RED_STATS:
		return mlxsw_sp_qdisc_get_red_stats(mlxsw_sp_port, p->handle,
						    mlxsw_sp_qdisc, tclass_num,
						    &p->stats);
	default:
		return -EOPNOTSUPP;
	}
}
