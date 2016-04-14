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
#include <linux/dcbnl.h>
#include <linux/if_ether.h>

#include "spectrum.h"
#include "core.h"
#include "port.h"
#include "reg.h"

static struct mlxsw_sp_sb_pr *mlxsw_sp_sb_pr_get(struct mlxsw_sp *mlxsw_sp,
						 u8 pool,
						 enum mlxsw_reg_sbxx_dir dir)
{
	return &mlxsw_sp->sb.prs[dir][pool];
}

static struct mlxsw_sp_sb_cm *mlxsw_sp_sb_cm_get(struct mlxsw_sp *mlxsw_sp,
						 u8 local_port, u8 pg_buff,
						 enum mlxsw_reg_sbxx_dir dir)
{
	return &mlxsw_sp->sb.ports[local_port].cms[dir][pg_buff];
}

static struct mlxsw_sp_sb_pm *mlxsw_sp_sb_pm_get(struct mlxsw_sp *mlxsw_sp,
						 u8 local_port, u8 pool,
						 enum mlxsw_reg_sbxx_dir dir)
{
	return &mlxsw_sp->sb.ports[local_port].pms[dir][pool];
}

static int mlxsw_sp_sb_pr_write(struct mlxsw_sp *mlxsw_sp, u8 pool,
				enum mlxsw_reg_sbxx_dir dir,
				enum mlxsw_reg_sbpr_mode mode, u32 size)
{
	char sbpr_pl[MLXSW_REG_SBPR_LEN];
	struct mlxsw_sp_sb_pr *pr;
	int err;

	mlxsw_reg_sbpr_pack(sbpr_pl, pool, dir, mode, size);
	err = mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(sbpr), sbpr_pl);
	if (err)
		return err;

	pr = mlxsw_sp_sb_pr_get(mlxsw_sp, pool, dir);
	pr->mode = mode;
	pr->size = size;
	return 0;
}

static int mlxsw_sp_sb_cm_write(struct mlxsw_sp *mlxsw_sp, u8 local_port,
				u8 pg_buff, enum mlxsw_reg_sbxx_dir dir,
				u32 min_buff, u32 max_buff, u8 pool)
{
	char sbcm_pl[MLXSW_REG_SBCM_LEN];
	int err;

	mlxsw_reg_sbcm_pack(sbcm_pl, local_port, pg_buff, dir,
			    min_buff, max_buff, pool);
	err = mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(sbcm), sbcm_pl);
	if (err)
		return err;
	if (pg_buff < MLXSW_SP_SB_TC_COUNT) {
		struct mlxsw_sp_sb_cm *cm;

		cm = mlxsw_sp_sb_cm_get(mlxsw_sp, local_port, pg_buff, dir);
		cm->min_buff = min_buff;
		cm->max_buff = max_buff;
		cm->pool = pool;
	}
	return 0;
}

static int mlxsw_sp_sb_pm_write(struct mlxsw_sp *mlxsw_sp, u8 local_port,
				u8 pool, enum mlxsw_reg_sbxx_dir dir,
				u32 min_buff, u32 max_buff)
{
	char sbpm_pl[MLXSW_REG_SBPM_LEN];
	struct mlxsw_sp_sb_pm *pm;
	int err;

	mlxsw_reg_sbpm_pack(sbpm_pl, local_port, pool, dir, min_buff, max_buff);
	err = mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(sbpm), sbpm_pl);
	if (err)
		return err;

	pm = mlxsw_sp_sb_pm_get(mlxsw_sp, local_port, pool, dir);
	pm->min_buff = min_buff;
	pm->max_buff = max_buff;
	return 0;
}

static const u16 mlxsw_sp_pbs[] = {
	2 * MLXSW_SP_BYTES_TO_CELLS(ETH_FRAME_LEN),
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0, /* Unused */
	2 * MLXSW_SP_BYTES_TO_CELLS(MLXSW_PORT_MAX_MTU),
};

#define MLXSW_SP_PBS_LEN ARRAY_SIZE(mlxsw_sp_pbs)

static int mlxsw_sp_port_pb_init(struct mlxsw_sp_port *mlxsw_sp_port)
{
	char pbmc_pl[MLXSW_REG_PBMC_LEN];
	int i;

	mlxsw_reg_pbmc_pack(pbmc_pl, mlxsw_sp_port->local_port,
			    0xffff, 0xffff / 2);
	for (i = 0; i < MLXSW_SP_PBS_LEN; i++) {
		if (i == 8)
			continue;
		mlxsw_reg_pbmc_lossy_buffer_pack(pbmc_pl, i, mlxsw_sp_pbs[i]);
	}
	mlxsw_reg_pbmc_lossy_buffer_pack(pbmc_pl,
					 MLXSW_REG_PBMC_PORT_SHARED_BUF_IDX, 0);
	return mlxsw_reg_write(mlxsw_sp_port->mlxsw_sp->core,
			       MLXSW_REG(pbmc), pbmc_pl);
}

static int mlxsw_sp_port_pb_prio_init(struct mlxsw_sp_port *mlxsw_sp_port)
{
	char pptb_pl[MLXSW_REG_PPTB_LEN];
	int i;

	mlxsw_reg_pptb_pack(pptb_pl, mlxsw_sp_port->local_port);
	for (i = 0; i < IEEE_8021QAZ_MAX_TCS; i++)
		mlxsw_reg_pptb_prio_to_buff_set(pptb_pl, i, 0);
	return mlxsw_reg_write(mlxsw_sp_port->mlxsw_sp->core, MLXSW_REG(pptb),
			       pptb_pl);
}

static int mlxsw_sp_port_headroom_init(struct mlxsw_sp_port *mlxsw_sp_port)
{
	int err;

	err = mlxsw_sp_port_pb_init(mlxsw_sp_port);
	if (err)
		return err;
	return mlxsw_sp_port_pb_prio_init(mlxsw_sp_port);
}

#define MLXSW_SP_SB_PR_INGRESS_SIZE				\
	(15000000 - (2 * 20000 * MLXSW_PORT_MAX_PORTS))
#define MLXSW_SP_SB_PR_INGRESS_MNG_SIZE (200 * 1000)
#define MLXSW_SP_SB_PR_EGRESS_SIZE				\
	(14000000 - (8 * 1500 * MLXSW_PORT_MAX_PORTS))

#define MLXSW_SP_SB_PR(_mode, _size)	\
	{				\
		.mode = _mode,		\
		.size = _size,		\
	}

static const struct mlxsw_sp_sb_pr mlxsw_sp_sb_prs_ingress[] = {
	MLXSW_SP_SB_PR(MLXSW_REG_SBPR_MODE_DYNAMIC,
		       MLXSW_SP_BYTES_TO_CELLS(MLXSW_SP_SB_PR_INGRESS_SIZE)),
	MLXSW_SP_SB_PR(MLXSW_REG_SBPR_MODE_DYNAMIC, 0),
	MLXSW_SP_SB_PR(MLXSW_REG_SBPR_MODE_DYNAMIC, 0),
	MLXSW_SP_SB_PR(MLXSW_REG_SBPR_MODE_DYNAMIC,
		       MLXSW_SP_BYTES_TO_CELLS(MLXSW_SP_SB_PR_INGRESS_MNG_SIZE)),
};

#define MLXSW_SP_SB_PRS_INGRESS_LEN ARRAY_SIZE(mlxsw_sp_sb_prs_ingress)

static const struct mlxsw_sp_sb_pr mlxsw_sp_sb_prs_egress[] = {
	MLXSW_SP_SB_PR(MLXSW_REG_SBPR_MODE_DYNAMIC,
		       MLXSW_SP_BYTES_TO_CELLS(MLXSW_SP_SB_PR_EGRESS_SIZE)),
	MLXSW_SP_SB_PR(MLXSW_REG_SBPR_MODE_DYNAMIC, 0),
	MLXSW_SP_SB_PR(MLXSW_REG_SBPR_MODE_DYNAMIC, 0),
	MLXSW_SP_SB_PR(MLXSW_REG_SBPR_MODE_DYNAMIC, 0),
};

#define MLXSW_SP_SB_PRS_EGRESS_LEN ARRAY_SIZE(mlxsw_sp_sb_prs_egress)

static int __mlxsw_sp_sb_prs_init(struct mlxsw_sp *mlxsw_sp,
				  enum mlxsw_reg_sbxx_dir dir,
				  const struct mlxsw_sp_sb_pr *prs,
				  size_t prs_len)
{
	int i;
	int err;

	for (i = 0; i < prs_len; i++) {
		const struct mlxsw_sp_sb_pr *pr;

		pr = &prs[i];
		err = mlxsw_sp_sb_pr_write(mlxsw_sp, i, dir,
					   pr->mode, pr->size);
		if (err)
			return err;
	}
	return 0;
}

static int mlxsw_sp_sb_prs_init(struct mlxsw_sp *mlxsw_sp)
{
	int err;

	err = __mlxsw_sp_sb_prs_init(mlxsw_sp, MLXSW_REG_SBXX_DIR_INGRESS,
				     mlxsw_sp_sb_prs_ingress,
				     MLXSW_SP_SB_PRS_INGRESS_LEN);
	if (err)
		return err;
	return __mlxsw_sp_sb_prs_init(mlxsw_sp, MLXSW_REG_SBXX_DIR_EGRESS,
				      mlxsw_sp_sb_prs_egress,
				      MLXSW_SP_SB_PRS_EGRESS_LEN);
}

#define MLXSW_SP_SB_CM(_min_buff, _max_buff, _pool)	\
	{						\
		.min_buff = _min_buff,			\
		.max_buff = _max_buff,			\
		.pool = _pool,				\
	}

static const struct mlxsw_sp_sb_cm mlxsw_sp_sb_cms_ingress[] = {
	MLXSW_SP_SB_CM(MLXSW_SP_BYTES_TO_CELLS(10000), 8, 0),
	MLXSW_SP_SB_CM(0, MLXSW_REG_SBXX_DYN_MAX_BUFF_MIN, 0),
	MLXSW_SP_SB_CM(0, MLXSW_REG_SBXX_DYN_MAX_BUFF_MIN, 0),
	MLXSW_SP_SB_CM(0, MLXSW_REG_SBXX_DYN_MAX_BUFF_MIN, 0),
	MLXSW_SP_SB_CM(0, MLXSW_REG_SBXX_DYN_MAX_BUFF_MIN, 0),
	MLXSW_SP_SB_CM(0, MLXSW_REG_SBXX_DYN_MAX_BUFF_MIN, 0),
	MLXSW_SP_SB_CM(0, MLXSW_REG_SBXX_DYN_MAX_BUFF_MIN, 0),
	MLXSW_SP_SB_CM(0, MLXSW_REG_SBXX_DYN_MAX_BUFF_MIN, 0),
	MLXSW_SP_SB_CM(0, 0, 0), /* dummy, this PG does not exist */
	MLXSW_SP_SB_CM(MLXSW_SP_BYTES_TO_CELLS(20000), 1, 3),
};

#define MLXSW_SP_SB_CMS_INGRESS_LEN ARRAY_SIZE(mlxsw_sp_sb_cms_ingress)

static const struct mlxsw_sp_sb_cm mlxsw_sp_sb_cms_egress[] = {
	MLXSW_SP_SB_CM(MLXSW_SP_BYTES_TO_CELLS(1500), 9, 0),
	MLXSW_SP_SB_CM(MLXSW_SP_BYTES_TO_CELLS(1500), 9, 0),
	MLXSW_SP_SB_CM(MLXSW_SP_BYTES_TO_CELLS(1500), 9, 0),
	MLXSW_SP_SB_CM(MLXSW_SP_BYTES_TO_CELLS(1500), 9, 0),
	MLXSW_SP_SB_CM(MLXSW_SP_BYTES_TO_CELLS(1500), 9, 0),
	MLXSW_SP_SB_CM(MLXSW_SP_BYTES_TO_CELLS(1500), 9, 0),
	MLXSW_SP_SB_CM(MLXSW_SP_BYTES_TO_CELLS(1500), 9, 0),
	MLXSW_SP_SB_CM(MLXSW_SP_BYTES_TO_CELLS(1500), 9, 0),
	MLXSW_SP_SB_CM(0, 0, 0),
	MLXSW_SP_SB_CM(0, 0, 0),
	MLXSW_SP_SB_CM(0, 0, 0),
	MLXSW_SP_SB_CM(0, 0, 0),
	MLXSW_SP_SB_CM(0, 0, 0),
	MLXSW_SP_SB_CM(0, 0, 0),
	MLXSW_SP_SB_CM(0, 0, 0),
	MLXSW_SP_SB_CM(0, 0, 0),
	MLXSW_SP_SB_CM(1, 0xff, 0),
};

#define MLXSW_SP_SB_CMS_EGRESS_LEN ARRAY_SIZE(mlxsw_sp_sb_cms_egress)

#define MLXSW_SP_CPU_PORT_SB_CM MLXSW_SP_SB_CM(0, 0, 0)

static const struct mlxsw_sp_sb_cm mlxsw_sp_cpu_port_sb_cms[] = {
	MLXSW_SP_CPU_PORT_SB_CM,
	MLXSW_SP_CPU_PORT_SB_CM,
	MLXSW_SP_CPU_PORT_SB_CM,
	MLXSW_SP_CPU_PORT_SB_CM,
	MLXSW_SP_CPU_PORT_SB_CM,
	MLXSW_SP_CPU_PORT_SB_CM,
	MLXSW_SP_CPU_PORT_SB_CM,
	MLXSW_SP_CPU_PORT_SB_CM,
	MLXSW_SP_CPU_PORT_SB_CM,
	MLXSW_SP_CPU_PORT_SB_CM,
	MLXSW_SP_CPU_PORT_SB_CM,
	MLXSW_SP_CPU_PORT_SB_CM,
	MLXSW_SP_CPU_PORT_SB_CM,
	MLXSW_SP_CPU_PORT_SB_CM,
	MLXSW_SP_CPU_PORT_SB_CM,
	MLXSW_SP_CPU_PORT_SB_CM,
	MLXSW_SP_CPU_PORT_SB_CM,
	MLXSW_SP_CPU_PORT_SB_CM,
	MLXSW_SP_CPU_PORT_SB_CM,
	MLXSW_SP_CPU_PORT_SB_CM,
	MLXSW_SP_CPU_PORT_SB_CM,
	MLXSW_SP_CPU_PORT_SB_CM,
	MLXSW_SP_CPU_PORT_SB_CM,
	MLXSW_SP_CPU_PORT_SB_CM,
	MLXSW_SP_CPU_PORT_SB_CM,
	MLXSW_SP_CPU_PORT_SB_CM,
	MLXSW_SP_CPU_PORT_SB_CM,
	MLXSW_SP_CPU_PORT_SB_CM,
	MLXSW_SP_CPU_PORT_SB_CM,
	MLXSW_SP_CPU_PORT_SB_CM,
	MLXSW_SP_CPU_PORT_SB_CM,
	MLXSW_SP_CPU_PORT_SB_CM,
};

#define MLXSW_SP_CPU_PORT_SB_MCS_LEN \
	ARRAY_SIZE(mlxsw_sp_cpu_port_sb_cms)

static int __mlxsw_sp_sb_cms_init(struct mlxsw_sp *mlxsw_sp, u8 local_port,
				  enum mlxsw_reg_sbxx_dir dir,
				  const struct mlxsw_sp_sb_cm *cms,
				  size_t cms_len)
{
	int i;
	int err;

	for (i = 0; i < cms_len; i++) {
		const struct mlxsw_sp_sb_cm *cm;

		if (i == 8 && dir == MLXSW_REG_SBXX_DIR_INGRESS)
			continue; /* PG number 8 does not exist, skip it */
		cm = &cms[i];
		err = mlxsw_sp_sb_cm_write(mlxsw_sp, local_port, i, dir,
					   cm->min_buff, cm->max_buff,
					   cm->pool);
		if (err)
			return err;
	}
	return 0;
}

static int mlxsw_sp_port_sb_cms_init(struct mlxsw_sp_port *mlxsw_sp_port)
{
	int err;

	err = __mlxsw_sp_sb_cms_init(mlxsw_sp_port->mlxsw_sp,
				     mlxsw_sp_port->local_port,
				     MLXSW_REG_SBXX_DIR_INGRESS,
				     mlxsw_sp_sb_cms_ingress,
				     MLXSW_SP_SB_CMS_INGRESS_LEN);
	if (err)
		return err;
	return __mlxsw_sp_sb_cms_init(mlxsw_sp_port->mlxsw_sp,
				      mlxsw_sp_port->local_port,
				      MLXSW_REG_SBXX_DIR_EGRESS,
				      mlxsw_sp_sb_cms_egress,
				      MLXSW_SP_SB_CMS_EGRESS_LEN);
}

static int mlxsw_sp_cpu_port_sb_cms_init(struct mlxsw_sp *mlxsw_sp)
{
	return __mlxsw_sp_sb_cms_init(mlxsw_sp, 0, MLXSW_REG_SBXX_DIR_EGRESS,
				      mlxsw_sp_cpu_port_sb_cms,
				      MLXSW_SP_CPU_PORT_SB_MCS_LEN);
}

#define MLXSW_SP_SB_PM(_min_buff, _max_buff)	\
	{					\
		.min_buff = _min_buff,		\
		.max_buff = _max_buff,		\
	}

static const struct mlxsw_sp_sb_pm mlxsw_sp_sb_pms_ingress[] = {
	MLXSW_SP_SB_PM(0, MLXSW_REG_SBXX_DYN_MAX_BUFF_MAX),
	MLXSW_SP_SB_PM(0, MLXSW_REG_SBXX_DYN_MAX_BUFF_MIN),
	MLXSW_SP_SB_PM(0, MLXSW_REG_SBXX_DYN_MAX_BUFF_MIN),
	MLXSW_SP_SB_PM(0, MLXSW_REG_SBXX_DYN_MAX_BUFF_MAX),
};

#define MLXSW_SP_SB_PMS_INGRESS_LEN ARRAY_SIZE(mlxsw_sp_sb_pms_ingress)

static const struct mlxsw_sp_sb_pm mlxsw_sp_sb_pms_egress[] = {
	MLXSW_SP_SB_PM(0, 7),
	MLXSW_SP_SB_PM(0, MLXSW_REG_SBXX_DYN_MAX_BUFF_MIN),
	MLXSW_SP_SB_PM(0, MLXSW_REG_SBXX_DYN_MAX_BUFF_MIN),
	MLXSW_SP_SB_PM(0, MLXSW_REG_SBXX_DYN_MAX_BUFF_MIN),
};

#define MLXSW_SP_SB_PMS_EGRESS_LEN ARRAY_SIZE(mlxsw_sp_sb_pms_egress)

static int __mlxsw_sp_port_sb_pms_init(struct mlxsw_sp *mlxsw_sp, u8 local_port,
				       enum mlxsw_reg_sbxx_dir dir,
				       const struct mlxsw_sp_sb_pm *pms,
				       size_t pms_len)
{
	int i;
	int err;

	for (i = 0; i < pms_len; i++) {
		const struct mlxsw_sp_sb_pm *pm;

		pm = &pms[i];
		err = mlxsw_sp_sb_pm_write(mlxsw_sp, local_port, i, dir,
					   pm->min_buff, pm->max_buff);
		if (err)
			return err;
	}
	return 0;
}

static int mlxsw_sp_port_sb_pms_init(struct mlxsw_sp_port *mlxsw_sp_port)
{
	int err;

	err = __mlxsw_sp_port_sb_pms_init(mlxsw_sp_port->mlxsw_sp,
					  mlxsw_sp_port->local_port,
					  MLXSW_REG_SBXX_DIR_INGRESS,
					  mlxsw_sp_sb_pms_ingress,
					  MLXSW_SP_SB_PMS_INGRESS_LEN);
	if (err)
		return err;
	return __mlxsw_sp_port_sb_pms_init(mlxsw_sp_port->mlxsw_sp,
					   mlxsw_sp_port->local_port,
					   MLXSW_REG_SBXX_DIR_EGRESS,
					   mlxsw_sp_sb_pms_egress,
					   MLXSW_SP_SB_PMS_EGRESS_LEN);
}

struct mlxsw_sp_sb_mm {
	u32 min_buff;
	u32 max_buff;
	u8 pool;
};

#define MLXSW_SP_SB_MM(_min_buff, _max_buff, _pool)	\
	{						\
		.min_buff = _min_buff,			\
		.max_buff = _max_buff,			\
		.pool = _pool,				\
	}

static const struct mlxsw_sp_sb_mm mlxsw_sp_sb_mms[] = {
	MLXSW_SP_SB_MM(MLXSW_SP_BYTES_TO_CELLS(20000), 0xff, 0),
	MLXSW_SP_SB_MM(MLXSW_SP_BYTES_TO_CELLS(20000), 0xff, 0),
	MLXSW_SP_SB_MM(MLXSW_SP_BYTES_TO_CELLS(20000), 0xff, 0),
	MLXSW_SP_SB_MM(MLXSW_SP_BYTES_TO_CELLS(20000), 0xff, 0),
	MLXSW_SP_SB_MM(MLXSW_SP_BYTES_TO_CELLS(20000), 0xff, 0),
	MLXSW_SP_SB_MM(MLXSW_SP_BYTES_TO_CELLS(20000), 0xff, 0),
	MLXSW_SP_SB_MM(MLXSW_SP_BYTES_TO_CELLS(20000), 0xff, 0),
	MLXSW_SP_SB_MM(MLXSW_SP_BYTES_TO_CELLS(20000), 0xff, 0),
	MLXSW_SP_SB_MM(MLXSW_SP_BYTES_TO_CELLS(20000), 0xff, 0),
	MLXSW_SP_SB_MM(MLXSW_SP_BYTES_TO_CELLS(20000), 0xff, 0),
	MLXSW_SP_SB_MM(MLXSW_SP_BYTES_TO_CELLS(20000), 0xff, 0),
	MLXSW_SP_SB_MM(MLXSW_SP_BYTES_TO_CELLS(20000), 0xff, 0),
	MLXSW_SP_SB_MM(MLXSW_SP_BYTES_TO_CELLS(20000), 0xff, 0),
	MLXSW_SP_SB_MM(MLXSW_SP_BYTES_TO_CELLS(20000), 0xff, 0),
	MLXSW_SP_SB_MM(MLXSW_SP_BYTES_TO_CELLS(20000), 0xff, 0),
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
		mlxsw_reg_sbmm_pack(sbmm_pl, i, mc->min_buff,
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

	err = mlxsw_sp_sb_prs_init(mlxsw_sp);
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

	err = mlxsw_sp_port_headroom_init(mlxsw_sp_port);
	if (err)
		return err;
	err = mlxsw_sp_port_sb_cms_init(mlxsw_sp_port);
	if (err)
		return err;
	err = mlxsw_sp_port_sb_pms_init(mlxsw_sp_port);

	return err;
}
