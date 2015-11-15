/*
 * drivers/net/ethernet/mellanox/mlxsw/spectrum_buffers.c
 * Copyright (c) 2015 Mellanox Technologies. All rights reserved.
 * Copyright (c) 2015 Jiri Pirko <jiri@mellanox.com>
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
#include <linux/types.h>

#include "spectrum.h"
#include "core.h"
#include "port.h"
#include "reg.h"

struct mlxsw_sp_pb {
	u8 index;
	u16 size;
};

#define MLXSW_SP_PB(_index, _size)	\
	{				\
		.index = _index,	\
		.size = _size,		\
	}

static const struct mlxsw_sp_pb mlxsw_sp_pbs[] = {
	MLXSW_SP_PB(0, 208),
	MLXSW_SP_PB(1, 208),
	MLXSW_SP_PB(2, 208),
	MLXSW_SP_PB(3, 208),
	MLXSW_SP_PB(4, 208),
	MLXSW_SP_PB(5, 208),
	MLXSW_SP_PB(6, 208),
	MLXSW_SP_PB(7, 208),
	MLXSW_SP_PB(9, 208),
};

#define MLXSW_SP_PBS_LEN ARRAY_SIZE(mlxsw_sp_pbs)

static int mlxsw_sp_port_pb_init(struct mlxsw_sp_port *mlxsw_sp_port)
{
	char pbmc_pl[MLXSW_REG_PBMC_LEN];
	int i;

	mlxsw_reg_pbmc_pack(pbmc_pl, mlxsw_sp_port->local_port,
			    0xffff, 0xffff / 2);
	for (i = 0; i < MLXSW_SP_PBS_LEN; i++) {
		const struct mlxsw_sp_pb *pb;

		pb = &mlxsw_sp_pbs[i];
		mlxsw_reg_pbmc_lossy_buffer_pack(pbmc_pl, pb->index, pb->size);
	}
	return mlxsw_reg_write(mlxsw_sp_port->mlxsw_sp->core,
			       MLXSW_REG(pbmc), pbmc_pl);
}

#define MLXSW_SP_SB_BYTES_PER_CELL 96

struct mlxsw_sp_sb_pool {
	u8 pool;
	enum mlxsw_reg_sbpr_dir dir;
	enum mlxsw_reg_sbpr_mode mode;
	u32 size;
};

#define MLXSW_SP_SB_POOL_INGRESS_SIZE				\
	((15000000 - (2 * 20000 * MLXSW_PORT_MAX_PORTS)) /	\
	 MLXSW_SP_SB_BYTES_PER_CELL)
#define MLXSW_SP_SB_POOL_EGRESS_SIZE				\
	((14000000 - (8 * 1500 * MLXSW_PORT_MAX_PORTS)) /	\
	 MLXSW_SP_SB_BYTES_PER_CELL)

#define MLXSW_SP_SB_POOL(_pool, _dir, _mode, _size)		\
	{							\
		.pool = _pool,					\
		.dir = _dir,					\
		.mode = _mode,					\
		.size = _size,					\
	}

#define MLXSW_SP_SB_POOL_INGRESS(_pool, _size)			\
	MLXSW_SP_SB_POOL(_pool, MLXSW_REG_SBPR_DIR_INGRESS,	\
			 MLXSW_REG_SBPR_MODE_DYNAMIC, _size)

#define MLXSW_SP_SB_POOL_EGRESS(_pool, _size)			\
	MLXSW_SP_SB_POOL(_pool, MLXSW_REG_SBPR_DIR_EGRESS,	\
			 MLXSW_REG_SBPR_MODE_DYNAMIC, _size)

static const struct mlxsw_sp_sb_pool mlxsw_sp_sb_pools[] = {
	MLXSW_SP_SB_POOL_INGRESS(0, MLXSW_SP_SB_POOL_INGRESS_SIZE),
	MLXSW_SP_SB_POOL_INGRESS(1, 0),
	MLXSW_SP_SB_POOL_INGRESS(2, 0),
	MLXSW_SP_SB_POOL_INGRESS(3, 0),
	MLXSW_SP_SB_POOL_EGRESS(0, MLXSW_SP_SB_POOL_EGRESS_SIZE),
	MLXSW_SP_SB_POOL_EGRESS(1, 0),
	MLXSW_SP_SB_POOL_EGRESS(2, 0),
	MLXSW_SP_SB_POOL_EGRESS(2, MLXSW_SP_SB_POOL_EGRESS_SIZE),
};

#define MLXSW_SP_SB_POOLS_LEN ARRAY_SIZE(mlxsw_sp_sb_pools)

static int mlxsw_sp_sb_pools_init(struct mlxsw_sp *mlxsw_sp)
{
	char sbpr_pl[MLXSW_REG_SBPR_LEN];
	int i;
	int err;

	for (i = 0; i < MLXSW_SP_SB_POOLS_LEN; i++) {
		const struct mlxsw_sp_sb_pool *pool;

		pool = &mlxsw_sp_sb_pools[i];
		mlxsw_reg_sbpr_pack(sbpr_pl, pool->pool, pool->dir,
				    pool->mode, pool->size);
		err = mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(sbpr), sbpr_pl);
		if (err)
			return err;
	}
	return 0;
}

struct mlxsw_sp_sb_cm {
	union {
		u8 pg;
		u8 tc;
	} u;
	enum mlxsw_reg_sbcm_dir dir;
	u32 min_buff;
	u32 max_buff;
	u8 pool;
};

#define MLXSW_SP_SB_CM(_pg_tc, _dir, _min_buff, _max_buff, _pool)	\
	{								\
		.u.pg = _pg_tc,						\
		.dir = _dir,						\
		.min_buff = _min_buff,					\
		.max_buff = _max_buff,					\
		.pool = _pool,						\
	}

#define MLXSW_SP_SB_CM_INGRESS(_pg, _min_buff, _max_buff)		\
	MLXSW_SP_SB_CM(_pg, MLXSW_REG_SBCM_DIR_INGRESS,			\
		       _min_buff, _max_buff, 0)

#define MLXSW_SP_SB_CM_EGRESS(_tc, _min_buff, _max_buff)		\
	MLXSW_SP_SB_CM(_tc, MLXSW_REG_SBCM_DIR_EGRESS,			\
		       _min_buff, _max_buff, 0)

#define MLXSW_SP_CPU_PORT_SB_CM_EGRESS(_tc)				\
	MLXSW_SP_SB_CM(_tc, MLXSW_REG_SBCM_DIR_EGRESS, 104, 2, 3)

static const struct mlxsw_sp_sb_cm mlxsw_sp_sb_cms[] = {
	MLXSW_SP_SB_CM_INGRESS(0, 10000 / MLXSW_SP_SB_BYTES_PER_CELL, 8),
	MLXSW_SP_SB_CM_INGRESS(1, 0, 0),
	MLXSW_SP_SB_CM_INGRESS(2, 0, 0),
	MLXSW_SP_SB_CM_INGRESS(3, 0, 0),
	MLXSW_SP_SB_CM_INGRESS(4, 0, 0),
	MLXSW_SP_SB_CM_INGRESS(5, 0, 0),
	MLXSW_SP_SB_CM_INGRESS(6, 0, 0),
	MLXSW_SP_SB_CM_INGRESS(7, 0, 0),
	MLXSW_SP_SB_CM_INGRESS(9, 20000 / MLXSW_SP_SB_BYTES_PER_CELL, 0xff),
	MLXSW_SP_SB_CM_EGRESS(0, 1500 / MLXSW_SP_SB_BYTES_PER_CELL, 9),
	MLXSW_SP_SB_CM_EGRESS(1, 1500 / MLXSW_SP_SB_BYTES_PER_CELL, 9),
	MLXSW_SP_SB_CM_EGRESS(2, 1500 / MLXSW_SP_SB_BYTES_PER_CELL, 9),
	MLXSW_SP_SB_CM_EGRESS(3, 1500 / MLXSW_SP_SB_BYTES_PER_CELL, 9),
	MLXSW_SP_SB_CM_EGRESS(4, 1500 / MLXSW_SP_SB_BYTES_PER_CELL, 9),
	MLXSW_SP_SB_CM_EGRESS(5, 1500 / MLXSW_SP_SB_BYTES_PER_CELL, 9),
	MLXSW_SP_SB_CM_EGRESS(6, 1500 / MLXSW_SP_SB_BYTES_PER_CELL, 9),
	MLXSW_SP_SB_CM_EGRESS(7, 1500 / MLXSW_SP_SB_BYTES_PER_CELL, 9),
	MLXSW_SP_SB_CM_EGRESS(8, 0, 0),
	MLXSW_SP_SB_CM_EGRESS(9, 0, 0),
	MLXSW_SP_SB_CM_EGRESS(10, 0, 0),
	MLXSW_SP_SB_CM_EGRESS(11, 0, 0),
	MLXSW_SP_SB_CM_EGRESS(12, 0, 0),
	MLXSW_SP_SB_CM_EGRESS(13, 0, 0),
	MLXSW_SP_SB_CM_EGRESS(14, 0, 0),
	MLXSW_SP_SB_CM_EGRESS(15, 0, 0),
	MLXSW_SP_SB_CM_EGRESS(16, 1, 0xff),
};

#define MLXSW_SP_SB_CMS_LEN ARRAY_SIZE(mlxsw_sp_sb_cms)

static const struct mlxsw_sp_sb_cm mlxsw_sp_cpu_port_sb_cms[] = {
	MLXSW_SP_CPU_PORT_SB_CM_EGRESS(0),
	MLXSW_SP_CPU_PORT_SB_CM_EGRESS(1),
	MLXSW_SP_CPU_PORT_SB_CM_EGRESS(2),
	MLXSW_SP_CPU_PORT_SB_CM_EGRESS(3),
	MLXSW_SP_CPU_PORT_SB_CM_EGRESS(4),
	MLXSW_SP_CPU_PORT_SB_CM_EGRESS(5),
	MLXSW_SP_CPU_PORT_SB_CM_EGRESS(6),
	MLXSW_SP_CPU_PORT_SB_CM_EGRESS(7),
	MLXSW_SP_CPU_PORT_SB_CM_EGRESS(8),
	MLXSW_SP_CPU_PORT_SB_CM_EGRESS(9),
	MLXSW_SP_CPU_PORT_SB_CM_EGRESS(10),
	MLXSW_SP_CPU_PORT_SB_CM_EGRESS(11),
	MLXSW_SP_CPU_PORT_SB_CM_EGRESS(12),
	MLXSW_SP_CPU_PORT_SB_CM_EGRESS(13),
	MLXSW_SP_CPU_PORT_SB_CM_EGRESS(14),
	MLXSW_SP_CPU_PORT_SB_CM_EGRESS(15),
	MLXSW_SP_CPU_PORT_SB_CM_EGRESS(16),
	MLXSW_SP_CPU_PORT_SB_CM_EGRESS(17),
	MLXSW_SP_CPU_PORT_SB_CM_EGRESS(18),
	MLXSW_SP_CPU_PORT_SB_CM_EGRESS(19),
	MLXSW_SP_CPU_PORT_SB_CM_EGRESS(20),
	MLXSW_SP_CPU_PORT_SB_CM_EGRESS(21),
	MLXSW_SP_CPU_PORT_SB_CM_EGRESS(22),
	MLXSW_SP_CPU_PORT_SB_CM_EGRESS(23),
	MLXSW_SP_CPU_PORT_SB_CM_EGRESS(24),
	MLXSW_SP_CPU_PORT_SB_CM_EGRESS(25),
	MLXSW_SP_CPU_PORT_SB_CM_EGRESS(26),
	MLXSW_SP_CPU_PORT_SB_CM_EGRESS(27),
	MLXSW_SP_CPU_PORT_SB_CM_EGRESS(28),
	MLXSW_SP_CPU_PORT_SB_CM_EGRESS(29),
	MLXSW_SP_CPU_PORT_SB_CM_EGRESS(30),
	MLXSW_SP_CPU_PORT_SB_CM_EGRESS(31),
};

#define MLXSW_SP_CPU_PORT_SB_MCS_LEN \
	ARRAY_SIZE(mlxsw_sp_cpu_port_sb_cms)

static int mlxsw_sp_sb_cms_init(struct mlxsw_sp *mlxsw_sp, u8 local_port,
				const struct mlxsw_sp_sb_cm *cms,
				size_t cms_len)
{
	char sbcm_pl[MLXSW_REG_SBCM_LEN];
	int i;
	int err;

	for (i = 0; i < cms_len; i++) {
		const struct mlxsw_sp_sb_cm *cm;

		cm = &cms[i];
		mlxsw_reg_sbcm_pack(sbcm_pl, local_port, cm->u.pg, cm->dir,
				    cm->min_buff, cm->max_buff, cm->pool);
		err = mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(sbcm), sbcm_pl);
		if (err)
			return err;
	}
	return 0;
}

static int mlxsw_sp_port_sb_cms_init(struct mlxsw_sp_port *mlxsw_sp_port)
{
	return mlxsw_sp_sb_cms_init(mlxsw_sp_port->mlxsw_sp,
				    mlxsw_sp_port->local_port, mlxsw_sp_sb_cms,
				    MLXSW_SP_SB_CMS_LEN);
}

static int mlxsw_sp_cpu_port_sb_cms_init(struct mlxsw_sp *mlxsw_sp)
{
	return mlxsw_sp_sb_cms_init(mlxsw_sp, 0, mlxsw_sp_cpu_port_sb_cms,
				    MLXSW_SP_CPU_PORT_SB_MCS_LEN);
}

struct mlxsw_sp_sb_pm {
	u8 pool;
	enum mlxsw_reg_sbpm_dir dir;
	u32 min_buff;
	u32 max_buff;
};

#define MLXSW_SP_SB_PM(_pool, _dir, _min_buff, _max_buff)	\
	{							\
		.pool = _pool,					\
		.dir = _dir,					\
		.min_buff = _min_buff,				\
		.max_buff = _max_buff,				\
	}

#define MLXSW_SP_SB_PM_INGRESS(_pool, _min_buff, _max_buff)	\
	MLXSW_SP_SB_PM(_pool, MLXSW_REG_SBPM_DIR_INGRESS,	\
		       _min_buff, _max_buff)

#define MLXSW_SP_SB_PM_EGRESS(_pool, _min_buff, _max_buff)	\
	MLXSW_SP_SB_PM(_pool, MLXSW_REG_SBPM_DIR_EGRESS,	\
		       _min_buff, _max_buff)

static const struct mlxsw_sp_sb_pm mlxsw_sp_sb_pms[] = {
	MLXSW_SP_SB_PM_INGRESS(0, 0, 0xff),
	MLXSW_SP_SB_PM_INGRESS(1, 0, 0),
	MLXSW_SP_SB_PM_INGRESS(2, 0, 0),
	MLXSW_SP_SB_PM_INGRESS(3, 0, 0),
	MLXSW_SP_SB_PM_EGRESS(0, 0, 7),
	MLXSW_SP_SB_PM_EGRESS(1, 0, 0),
	MLXSW_SP_SB_PM_EGRESS(2, 0, 0),
	MLXSW_SP_SB_PM_EGRESS(3, 0, 0),
};

#define MLXSW_SP_SB_PMS_LEN ARRAY_SIZE(mlxsw_sp_sb_pms)

static int mlxsw_sp_port_sb_pms_init(struct mlxsw_sp_port *mlxsw_sp_port)
{
	char sbpm_pl[MLXSW_REG_SBPM_LEN];
	int i;
	int err;

	for (i = 0; i < MLXSW_SP_SB_PMS_LEN; i++) {
		const struct mlxsw_sp_sb_pm *pm;

		pm = &mlxsw_sp_sb_pms[i];
		mlxsw_reg_sbpm_pack(sbpm_pl, mlxsw_sp_port->local_port,
				    pm->pool, pm->dir,
				    pm->min_buff, pm->max_buff);
		err = mlxsw_reg_write(mlxsw_sp_port->mlxsw_sp->core,
				      MLXSW_REG(sbpm), sbpm_pl);
		if (err)
			return err;
	}
	return 0;
}

struct mlxsw_sp_sb_mm {
	u8 prio;
	u32 min_buff;
	u32 max_buff;
	u8 pool;
};

#define MLXSW_SP_SB_MM(_prio, _min_buff, _max_buff, _pool)	\
	{							\
		.prio = _prio,					\
		.min_buff = _min_buff,				\
		.max_buff = _max_buff,				\
		.pool = _pool,					\
	}

static const struct mlxsw_sp_sb_mm mlxsw_sp_sb_mms[] = {
	MLXSW_SP_SB_MM(0, 20000 / MLXSW_SP_SB_BYTES_PER_CELL, 0xff, 0),
	MLXSW_SP_SB_MM(1, 20000 / MLXSW_SP_SB_BYTES_PER_CELL, 0xff, 0),
	MLXSW_SP_SB_MM(2, 20000 / MLXSW_SP_SB_BYTES_PER_CELL, 0xff, 0),
	MLXSW_SP_SB_MM(3, 20000 / MLXSW_SP_SB_BYTES_PER_CELL, 0xff, 0),
	MLXSW_SP_SB_MM(4, 20000 / MLXSW_SP_SB_BYTES_PER_CELL, 0xff, 0),
	MLXSW_SP_SB_MM(5, 20000 / MLXSW_SP_SB_BYTES_PER_CELL, 0xff, 0),
	MLXSW_SP_SB_MM(6, 20000 / MLXSW_SP_SB_BYTES_PER_CELL, 0xff, 0),
	MLXSW_SP_SB_MM(7, 20000 / MLXSW_SP_SB_BYTES_PER_CELL, 0xff, 0),
	MLXSW_SP_SB_MM(8, 20000 / MLXSW_SP_SB_BYTES_PER_CELL, 0xff, 0),
	MLXSW_SP_SB_MM(9, 20000 / MLXSW_SP_SB_BYTES_PER_CELL, 0xff, 0),
	MLXSW_SP_SB_MM(10, 20000 / MLXSW_SP_SB_BYTES_PER_CELL, 0xff, 0),
	MLXSW_SP_SB_MM(11, 20000 / MLXSW_SP_SB_BYTES_PER_CELL, 0xff, 0),
	MLXSW_SP_SB_MM(12, 20000 / MLXSW_SP_SB_BYTES_PER_CELL, 0xff, 0),
	MLXSW_SP_SB_MM(13, 20000 / MLXSW_SP_SB_BYTES_PER_CELL, 0xff, 0),
	MLXSW_SP_SB_MM(14, 20000 / MLXSW_SP_SB_BYTES_PER_CELL, 0xff, 0),
};

#define MLXSW_SP_SB_MMS_LEN ARRAY_SIZE(mlxsw_sp_sb_mms)

static int mlxsw_sp_sb_mms_init(struct mlxsw_sp *mlxsw_sp)
{
	char sbmm_pl[MLXSW_REG_SBMM_LEN];
	int i;
	int err;

	for (i = 0; i < MLXSW_SP_SB_MMS_LEN; i++) {
		const struct mlxsw_sp_sb_mm *mc;

		mc = &mlxsw_sp_sb_mms[i];
		mlxsw_reg_sbmm_pack(sbmm_pl, mc->prio, mc->min_buff,
				    mc->max_buff, mc->pool);
		err = mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(sbmm), sbmm_pl);
		if (err)
			return err;
	}
	return 0;
}

int mlxsw_sp_buffers_init(struct mlxsw_sp *mlxsw_sp)
{
	int err;

	err = mlxsw_sp_sb_pools_init(mlxsw_sp);
	if (err)
		return err;
	err = mlxsw_sp_cpu_port_sb_cms_init(mlxsw_sp);
	if (err)
		return err;
	err = mlxsw_sp_sb_mms_init(mlxsw_sp);

	return err;
}

int mlxsw_sp_port_buffers_init(struct mlxsw_sp_port *mlxsw_sp_port)
{
	int err;

	err = mlxsw_sp_port_pb_init(mlxsw_sp_port);
	if (err)
		return err;
	err = mlxsw_sp_port_sb_cms_init(mlxsw_sp_port);
	if (err)
		return err;
	err = mlxsw_sp_port_sb_pms_init(mlxsw_sp_port);

	return err;
}
