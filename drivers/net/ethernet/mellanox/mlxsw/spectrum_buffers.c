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
#include <linux/list.h>

#include "spectrum.h"
#include "core.h"
#include "port.h"
#include "reg.h"

struct mlxsw_sp_sb_pr {
	enum mlxsw_reg_sbpr_mode mode;
	u32 size;
};

struct mlxsw_cp_sb_occ {
	u32 cur;
	u32 max;
};

struct mlxsw_sp_sb_cm {
	u32 min_buff;
	u32 max_buff;
	u8 pool;
	struct mlxsw_cp_sb_occ occ;
};

struct mlxsw_sp_sb_pm {
	u32 min_buff;
	u32 max_buff;
	struct mlxsw_cp_sb_occ occ;
};

#define MLXSW_SP_SB_POOL_COUNT	4
#define MLXSW_SP_SB_TC_COUNT	8

struct mlxsw_sp_sb_port {
	struct mlxsw_sp_sb_cm cms[2][MLXSW_SP_SB_TC_COUNT];
	struct mlxsw_sp_sb_pm pms[2][MLXSW_SP_SB_POOL_COUNT];
};

struct mlxsw_sp_sb {
	struct mlxsw_sp_sb_pr prs[2][MLXSW_SP_SB_POOL_COUNT];
	struct mlxsw_sp_sb_port *ports;
	u32 cell_size;
};

u32 mlxsw_sp_cells_bytes(const struct mlxsw_sp *mlxsw_sp, u32 cells)
{
	return mlxsw_sp->sb->cell_size * cells;
}

u32 mlxsw_sp_bytes_cells(const struct mlxsw_sp *mlxsw_sp, u32 bytes)
{
	return DIV_ROUND_UP(bytes, mlxsw_sp->sb->cell_size);
}

static struct mlxsw_sp_sb_pr *mlxsw_sp_sb_pr_get(struct mlxsw_sp *mlxsw_sp,
						 u8 pool,
						 enum mlxsw_reg_sbxx_dir dir)
{
	return &mlxsw_sp->sb->prs[dir][pool];
}

static struct mlxsw_sp_sb_cm *mlxsw_sp_sb_cm_get(struct mlxsw_sp *mlxsw_sp,
						 u8 local_port, u8 pg_buff,
						 enum mlxsw_reg_sbxx_dir dir)
{
	return &mlxsw_sp->sb->ports[local_port].cms[dir][pg_buff];
}

static struct mlxsw_sp_sb_pm *mlxsw_sp_sb_pm_get(struct mlxsw_sp *mlxsw_sp,
						 u8 local_port, u8 pool,
						 enum mlxsw_reg_sbxx_dir dir)
{
	return &mlxsw_sp->sb->ports[local_port].pms[dir][pool];
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

	mlxsw_reg_sbpm_pack(sbpm_pl, local_port, pool, dir, false,
			    min_buff, max_buff);
	err = mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(sbpm), sbpm_pl);
	if (err)
		return err;

	pm = mlxsw_sp_sb_pm_get(mlxsw_sp, local_port, pool, dir);
	pm->min_buff = min_buff;
	pm->max_buff = max_buff;
	return 0;
}

static int mlxsw_sp_sb_pm_occ_clear(struct mlxsw_sp *mlxsw_sp, u8 local_port,
				    u8 pool, enum mlxsw_reg_sbxx_dir dir,
				    struct list_head *bulk_list)
{
	char sbpm_pl[MLXSW_REG_SBPM_LEN];

	mlxsw_reg_sbpm_pack(sbpm_pl, local_port, pool, dir, true, 0, 0);
	return mlxsw_reg_trans_query(mlxsw_sp->core, MLXSW_REG(sbpm), sbpm_pl,
				     bulk_list, NULL, 0);
}

static void mlxsw_sp_sb_pm_occ_query_cb(struct mlxsw_core *mlxsw_core,
					char *sbpm_pl, size_t sbpm_pl_len,
					unsigned long cb_priv)
{
	struct mlxsw_sp_sb_pm *pm = (struct mlxsw_sp_sb_pm *) cb_priv;

	mlxsw_reg_sbpm_unpack(sbpm_pl, &pm->occ.cur, &pm->occ.max);
}

static int mlxsw_sp_sb_pm_occ_query(struct mlxsw_sp *mlxsw_sp, u8 local_port,
				    u8 pool, enum mlxsw_reg_sbxx_dir dir,
				    struct list_head *bulk_list)
{
	char sbpm_pl[MLXSW_REG_SBPM_LEN];
	struct mlxsw_sp_sb_pm *pm;

	pm = mlxsw_sp_sb_pm_get(mlxsw_sp, local_port, pool, dir);
	mlxsw_reg_sbpm_pack(sbpm_pl, local_port, pool, dir, false, 0, 0);
	return mlxsw_reg_trans_query(mlxsw_sp->core, MLXSW_REG(sbpm), sbpm_pl,
				     bulk_list,
				     mlxsw_sp_sb_pm_occ_query_cb,
				     (unsigned long) pm);
}

static const u16 mlxsw_sp_pbs[] = {
	[0] = 2 * ETH_FRAME_LEN,
	[9] = 2 * MLXSW_PORT_MAX_MTU,
};

#define MLXSW_SP_PBS_LEN ARRAY_SIZE(mlxsw_sp_pbs)
#define MLXSW_SP_PB_UNUSED 8

static int mlxsw_sp_port_pb_init(struct mlxsw_sp_port *mlxsw_sp_port)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	char pbmc_pl[MLXSW_REG_PBMC_LEN];
	int i;

	mlxsw_reg_pbmc_pack(pbmc_pl, mlxsw_sp_port->local_port,
			    0xffff, 0xffff / 2);
	for (i = 0; i < MLXSW_SP_PBS_LEN; i++) {
		u16 size = mlxsw_sp_bytes_cells(mlxsw_sp, mlxsw_sp_pbs[i]);

		if (i == MLXSW_SP_PB_UNUSED)
			continue;
		mlxsw_reg_pbmc_lossy_buffer_pack(pbmc_pl, i, size);
	}
	mlxsw_reg_pbmc_lossy_buffer_pack(pbmc_pl,
					 MLXSW_REG_PBMC_PORT_SHARED_BUF_IDX, 0);
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(pbmc), pbmc_pl);
}

static int mlxsw_sp_port_pb_prio_init(struct mlxsw_sp_port *mlxsw_sp_port)
{
	char pptb_pl[MLXSW_REG_PPTB_LEN];
	int i;

	mlxsw_reg_pptb_pack(pptb_pl, mlxsw_sp_port->local_port);
	for (i = 0; i < IEEE_8021QAZ_MAX_TCS; i++)
		mlxsw_reg_pptb_prio_to_buff_pack(pptb_pl, i, 0);
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

static int mlxsw_sp_sb_ports_init(struct mlxsw_sp *mlxsw_sp)
{
	unsigned int max_ports = mlxsw_core_max_ports(mlxsw_sp->core);

	mlxsw_sp->sb->ports = kcalloc(max_ports,
				      sizeof(struct mlxsw_sp_sb_port),
				      GFP_KERNEL);
	if (!mlxsw_sp->sb->ports)
		return -ENOMEM;
	return 0;
}

static void mlxsw_sp_sb_ports_fini(struct mlxsw_sp *mlxsw_sp)
{
	kfree(mlxsw_sp->sb->ports);
}

#define MLXSW_SP_SB_PR_INGRESS_SIZE	12440000
#define MLXSW_SP_SB_PR_INGRESS_MNG_SIZE (200 * 1000)
#define MLXSW_SP_SB_PR_EGRESS_SIZE	13232000

#define MLXSW_SP_SB_PR(_mode, _size)	\
	{				\
		.mode = _mode,		\
		.size = _size,		\
	}

static const struct mlxsw_sp_sb_pr mlxsw_sp_sb_prs_ingress[] = {
	MLXSW_SP_SB_PR(MLXSW_REG_SBPR_MODE_DYNAMIC,
		       MLXSW_SP_SB_PR_INGRESS_SIZE),
	MLXSW_SP_SB_PR(MLXSW_REG_SBPR_MODE_DYNAMIC, 0),
	MLXSW_SP_SB_PR(MLXSW_REG_SBPR_MODE_DYNAMIC, 0),
	MLXSW_SP_SB_PR(MLXSW_REG_SBPR_MODE_DYNAMIC,
		       MLXSW_SP_SB_PR_INGRESS_MNG_SIZE),
};

#define MLXSW_SP_SB_PRS_INGRESS_LEN ARRAY_SIZE(mlxsw_sp_sb_prs_ingress)

static const struct mlxsw_sp_sb_pr mlxsw_sp_sb_prs_egress[] = {
	MLXSW_SP_SB_PR(MLXSW_REG_SBPR_MODE_DYNAMIC, MLXSW_SP_SB_PR_EGRESS_SIZE),
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
		u32 size = mlxsw_sp_bytes_cells(mlxsw_sp, prs[i].size);

		err = mlxsw_sp_sb_pr_write(mlxsw_sp, i, dir, prs[i].mode, size);
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
	MLXSW_SP_SB_CM(10000, 8, 0),
	MLXSW_SP_SB_CM(0, MLXSW_REG_SBXX_DYN_MAX_BUFF_MIN, 0),
	MLXSW_SP_SB_CM(0, MLXSW_REG_SBXX_DYN_MAX_BUFF_MIN, 0),
	MLXSW_SP_SB_CM(0, MLXSW_REG_SBXX_DYN_MAX_BUFF_MIN, 0),
	MLXSW_SP_SB_CM(0, MLXSW_REG_SBXX_DYN_MAX_BUFF_MIN, 0),
	MLXSW_SP_SB_CM(0, MLXSW_REG_SBXX_DYN_MAX_BUFF_MIN, 0),
	MLXSW_SP_SB_CM(0, MLXSW_REG_SBXX_DYN_MAX_BUFF_MIN, 0),
	MLXSW_SP_SB_CM(0, MLXSW_REG_SBXX_DYN_MAX_BUFF_MIN, 0),
	MLXSW_SP_SB_CM(0, 0, 0), /* dummy, this PG does not exist */
	MLXSW_SP_SB_CM(20000, 1, 3),
};

#define MLXSW_SP_SB_CMS_INGRESS_LEN ARRAY_SIZE(mlxsw_sp_sb_cms_ingress)

static const struct mlxsw_sp_sb_cm mlxsw_sp_sb_cms_egress[] = {
	MLXSW_SP_SB_CM(1500, 9, 0),
	MLXSW_SP_SB_CM(1500, 9, 0),
	MLXSW_SP_SB_CM(1500, 9, 0),
	MLXSW_SP_SB_CM(1500, 9, 0),
	MLXSW_SP_SB_CM(1500, 9, 0),
	MLXSW_SP_SB_CM(1500, 9, 0),
	MLXSW_SP_SB_CM(1500, 9, 0),
	MLXSW_SP_SB_CM(1500, 9, 0),
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
	MLXSW_SP_SB_CM(MLXSW_PORT_MAX_MTU, 0, 0),
	MLXSW_SP_SB_CM(MLXSW_PORT_MAX_MTU, 0, 0),
	MLXSW_SP_SB_CM(MLXSW_PORT_MAX_MTU, 0, 0),
	MLXSW_SP_SB_CM(MLXSW_PORT_MAX_MTU, 0, 0),
	MLXSW_SP_SB_CM(MLXSW_PORT_MAX_MTU, 0, 0),
	MLXSW_SP_CPU_PORT_SB_CM,
	MLXSW_SP_SB_CM(MLXSW_PORT_MAX_MTU, 0, 0),
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
		u32 min_buff;

		if (i == 8 && dir == MLXSW_REG_SBXX_DIR_INGRESS)
			continue; /* PG number 8 does not exist, skip it */
		cm = &cms[i];
		/* All pools are initialized using dynamic thresholds,
		 * therefore 'max_buff' isn't specified in cells.
		 */
		min_buff = mlxsw_sp_bytes_cells(mlxsw_sp, cm->min_buff);
		err = mlxsw_sp_sb_cm_write(mlxsw_sp, local_port, i, dir,
					   min_buff, cm->max_buff, cm->pool);
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
	MLXSW_SP_SB_MM(20000, 0xff, 0),
	MLXSW_SP_SB_MM(20000, 0xff, 0),
	MLXSW_SP_SB_MM(20000, 0xff, 0),
	MLXSW_SP_SB_MM(20000, 0xff, 0),
	MLXSW_SP_SB_MM(20000, 0xff, 0),
	MLXSW_SP_SB_MM(20000, 0xff, 0),
	MLXSW_SP_SB_MM(20000, 0xff, 0),
	MLXSW_SP_SB_MM(20000, 0xff, 0),
	MLXSW_SP_SB_MM(20000, 0xff, 0),
	MLXSW_SP_SB_MM(20000, 0xff, 0),
	MLXSW_SP_SB_MM(20000, 0xff, 0),
	MLXSW_SP_SB_MM(20000, 0xff, 0),
	MLXSW_SP_SB_MM(20000, 0xff, 0),
	MLXSW_SP_SB_MM(20000, 0xff, 0),
	MLXSW_SP_SB_MM(20000, 0xff, 0),
};

#define MLXSW_SP_SB_MMS_LEN ARRAY_SIZE(mlxsw_sp_sb_mms)

static int mlxsw_sp_sb_mms_init(struct mlxsw_sp *mlxsw_sp)
{
	char sbmm_pl[MLXSW_REG_SBMM_LEN];
	int i;
	int err;

	for (i = 0; i < MLXSW_SP_SB_MMS_LEN; i++) {
		const struct mlxsw_sp_sb_mm *mc;
		u32 min_buff;

		mc = &mlxsw_sp_sb_mms[i];
		/* All pools are initialized using dynamic thresholds,
		 * therefore 'max_buff' isn't specified in cells.
		 */
		min_buff = mlxsw_sp_bytes_cells(mlxsw_sp, mc->min_buff);
		mlxsw_reg_sbmm_pack(sbmm_pl, i, min_buff, mc->max_buff,
				    mc->pool);
		err = mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(sbmm), sbmm_pl);
		if (err)
			return err;
	}
	return 0;
}

int mlxsw_sp_buffers_init(struct mlxsw_sp *mlxsw_sp)
{
	u64 sb_size;
	int err;

	if (!MLXSW_CORE_RES_VALID(mlxsw_sp->core, CELL_SIZE))
		return -EIO;

	if (!MLXSW_CORE_RES_VALID(mlxsw_sp->core, MAX_BUFFER_SIZE))
		return -EIO;
	sb_size = MLXSW_CORE_RES_GET(mlxsw_sp->core, MAX_BUFFER_SIZE);

	mlxsw_sp->sb = kzalloc(sizeof(*mlxsw_sp->sb), GFP_KERNEL);
	if (!mlxsw_sp->sb)
		return -ENOMEM;
	mlxsw_sp->sb->cell_size = MLXSW_CORE_RES_GET(mlxsw_sp->core, CELL_SIZE);

	err = mlxsw_sp_sb_ports_init(mlxsw_sp);
	if (err)
		goto err_sb_ports_init;
	err = mlxsw_sp_sb_prs_init(mlxsw_sp);
	if (err)
		goto err_sb_prs_init;
	err = mlxsw_sp_cpu_port_sb_cms_init(mlxsw_sp);
	if (err)
		goto err_sb_cpu_port_sb_cms_init;
	err = mlxsw_sp_sb_mms_init(mlxsw_sp);
	if (err)
		goto err_sb_mms_init;
	err = devlink_sb_register(priv_to_devlink(mlxsw_sp->core), 0, sb_size,
				  MLXSW_SP_SB_POOL_COUNT,
				  MLXSW_SP_SB_POOL_COUNT,
				  MLXSW_SP_SB_TC_COUNT,
				  MLXSW_SP_SB_TC_COUNT);
	if (err)
		goto err_devlink_sb_register;

	return 0;

err_devlink_sb_register:
err_sb_mms_init:
err_sb_cpu_port_sb_cms_init:
err_sb_prs_init:
	mlxsw_sp_sb_ports_fini(mlxsw_sp);
err_sb_ports_init:
	kfree(mlxsw_sp->sb);
	return err;
}

void mlxsw_sp_buffers_fini(struct mlxsw_sp *mlxsw_sp)
{
	devlink_sb_unregister(priv_to_devlink(mlxsw_sp->core), 0);
	mlxsw_sp_sb_ports_fini(mlxsw_sp);
	kfree(mlxsw_sp->sb);
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

static u8 pool_get(u16 pool_index)
{
	return pool_index % MLXSW_SP_SB_POOL_COUNT;
}

static u16 pool_index_get(u8 pool, enum mlxsw_reg_sbxx_dir dir)
{
	u16 pool_index;

	pool_index = pool;
	if (dir == MLXSW_REG_SBXX_DIR_EGRESS)
		pool_index += MLXSW_SP_SB_POOL_COUNT;
	return pool_index;
}

static enum mlxsw_reg_sbxx_dir dir_get(u16 pool_index)
{
	return pool_index < MLXSW_SP_SB_POOL_COUNT ?
	       MLXSW_REG_SBXX_DIR_INGRESS : MLXSW_REG_SBXX_DIR_EGRESS;
}

int mlxsw_sp_sb_pool_get(struct mlxsw_core *mlxsw_core,
			 unsigned int sb_index, u16 pool_index,
			 struct devlink_sb_pool_info *pool_info)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_core_driver_priv(mlxsw_core);
	u8 pool = pool_get(pool_index);
	enum mlxsw_reg_sbxx_dir dir = dir_get(pool_index);
	struct mlxsw_sp_sb_pr *pr = mlxsw_sp_sb_pr_get(mlxsw_sp, pool, dir);

	pool_info->pool_type = (enum devlink_sb_pool_type) dir;
	pool_info->size = mlxsw_sp_cells_bytes(mlxsw_sp, pr->size);
	pool_info->threshold_type = (enum devlink_sb_threshold_type) pr->mode;
	return 0;
}

int mlxsw_sp_sb_pool_set(struct mlxsw_core *mlxsw_core,
			 unsigned int sb_index, u16 pool_index, u32 size,
			 enum devlink_sb_threshold_type threshold_type)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_core_driver_priv(mlxsw_core);
	u32 pool_size = mlxsw_sp_bytes_cells(mlxsw_sp, size);
	u8 pool = pool_get(pool_index);
	enum mlxsw_reg_sbxx_dir dir = dir_get(pool_index);
	enum mlxsw_reg_sbpr_mode mode;

	if (size > MLXSW_CORE_RES_GET(mlxsw_sp->core, MAX_BUFFER_SIZE))
		return -EINVAL;

	mode = (enum mlxsw_reg_sbpr_mode) threshold_type;
	return mlxsw_sp_sb_pr_write(mlxsw_sp, pool, dir, mode, pool_size);
}

#define MLXSW_SP_SB_THRESHOLD_TO_ALPHA_OFFSET (-2) /* 3->1, 16->14 */

static u32 mlxsw_sp_sb_threshold_out(struct mlxsw_sp *mlxsw_sp, u8 pool,
				     enum mlxsw_reg_sbxx_dir dir, u32 max_buff)
{
	struct mlxsw_sp_sb_pr *pr = mlxsw_sp_sb_pr_get(mlxsw_sp, pool, dir);

	if (pr->mode == MLXSW_REG_SBPR_MODE_DYNAMIC)
		return max_buff - MLXSW_SP_SB_THRESHOLD_TO_ALPHA_OFFSET;
	return mlxsw_sp_cells_bytes(mlxsw_sp, max_buff);
}

static int mlxsw_sp_sb_threshold_in(struct mlxsw_sp *mlxsw_sp, u8 pool,
				    enum mlxsw_reg_sbxx_dir dir, u32 threshold,
				    u32 *p_max_buff)
{
	struct mlxsw_sp_sb_pr *pr = mlxsw_sp_sb_pr_get(mlxsw_sp, pool, dir);

	if (pr->mode == MLXSW_REG_SBPR_MODE_DYNAMIC) {
		int val;

		val = threshold + MLXSW_SP_SB_THRESHOLD_TO_ALPHA_OFFSET;
		if (val < MLXSW_REG_SBXX_DYN_MAX_BUFF_MIN ||
		    val > MLXSW_REG_SBXX_DYN_MAX_BUFF_MAX)
			return -EINVAL;
		*p_max_buff = val;
	} else {
		*p_max_buff = mlxsw_sp_bytes_cells(mlxsw_sp, threshold);
	}
	return 0;
}

int mlxsw_sp_sb_port_pool_get(struct mlxsw_core_port *mlxsw_core_port,
			      unsigned int sb_index, u16 pool_index,
			      u32 *p_threshold)
{
	struct mlxsw_sp_port *mlxsw_sp_port =
			mlxsw_core_port_driver_priv(mlxsw_core_port);
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	u8 local_port = mlxsw_sp_port->local_port;
	u8 pool = pool_get(pool_index);
	enum mlxsw_reg_sbxx_dir dir = dir_get(pool_index);
	struct mlxsw_sp_sb_pm *pm = mlxsw_sp_sb_pm_get(mlxsw_sp, local_port,
						       pool, dir);

	*p_threshold = mlxsw_sp_sb_threshold_out(mlxsw_sp, pool, dir,
						 pm->max_buff);
	return 0;
}

int mlxsw_sp_sb_port_pool_set(struct mlxsw_core_port *mlxsw_core_port,
			      unsigned int sb_index, u16 pool_index,
			      u32 threshold)
{
	struct mlxsw_sp_port *mlxsw_sp_port =
			mlxsw_core_port_driver_priv(mlxsw_core_port);
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	u8 local_port = mlxsw_sp_port->local_port;
	u8 pool = pool_get(pool_index);
	enum mlxsw_reg_sbxx_dir dir = dir_get(pool_index);
	u32 max_buff;
	int err;

	err = mlxsw_sp_sb_threshold_in(mlxsw_sp, pool, dir,
				       threshold, &max_buff);
	if (err)
		return err;

	return mlxsw_sp_sb_pm_write(mlxsw_sp, local_port, pool, dir,
				    0, max_buff);
}

int mlxsw_sp_sb_tc_pool_bind_get(struct mlxsw_core_port *mlxsw_core_port,
				 unsigned int sb_index, u16 tc_index,
				 enum devlink_sb_pool_type pool_type,
				 u16 *p_pool_index, u32 *p_threshold)
{
	struct mlxsw_sp_port *mlxsw_sp_port =
			mlxsw_core_port_driver_priv(mlxsw_core_port);
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	u8 local_port = mlxsw_sp_port->local_port;
	u8 pg_buff = tc_index;
	enum mlxsw_reg_sbxx_dir dir = (enum mlxsw_reg_sbxx_dir) pool_type;
	struct mlxsw_sp_sb_cm *cm = mlxsw_sp_sb_cm_get(mlxsw_sp, local_port,
						       pg_buff, dir);

	*p_threshold = mlxsw_sp_sb_threshold_out(mlxsw_sp, cm->pool, dir,
						 cm->max_buff);
	*p_pool_index = pool_index_get(cm->pool, dir);
	return 0;
}

int mlxsw_sp_sb_tc_pool_bind_set(struct mlxsw_core_port *mlxsw_core_port,
				 unsigned int sb_index, u16 tc_index,
				 enum devlink_sb_pool_type pool_type,
				 u16 pool_index, u32 threshold)
{
	struct mlxsw_sp_port *mlxsw_sp_port =
			mlxsw_core_port_driver_priv(mlxsw_core_port);
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	u8 local_port = mlxsw_sp_port->local_port;
	u8 pg_buff = tc_index;
	enum mlxsw_reg_sbxx_dir dir = (enum mlxsw_reg_sbxx_dir) pool_type;
	u8 pool = pool_get(pool_index);
	u32 max_buff;
	int err;

	if (dir != dir_get(pool_index))
		return -EINVAL;

	err = mlxsw_sp_sb_threshold_in(mlxsw_sp, pool, dir,
				       threshold, &max_buff);
	if (err)
		return err;

	return mlxsw_sp_sb_cm_write(mlxsw_sp, local_port, pg_buff, dir,
				    0, max_buff, pool);
}

#define MASKED_COUNT_MAX \
	(MLXSW_REG_SBSR_REC_MAX_COUNT / (MLXSW_SP_SB_TC_COUNT * 2))

struct mlxsw_sp_sb_sr_occ_query_cb_ctx {
	u8 masked_count;
	u8 local_port_1;
};

static void mlxsw_sp_sb_sr_occ_query_cb(struct mlxsw_core *mlxsw_core,
					char *sbsr_pl, size_t sbsr_pl_len,
					unsigned long cb_priv)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_core_driver_priv(mlxsw_core);
	struct mlxsw_sp_sb_sr_occ_query_cb_ctx cb_ctx;
	u8 masked_count;
	u8 local_port;
	int rec_index = 0;
	struct mlxsw_sp_sb_cm *cm;
	int i;

	memcpy(&cb_ctx, &cb_priv, sizeof(cb_ctx));

	masked_count = 0;
	for (local_port = cb_ctx.local_port_1;
	     local_port < mlxsw_core_max_ports(mlxsw_core); local_port++) {
		if (!mlxsw_sp->ports[local_port])
			continue;
		for (i = 0; i < MLXSW_SP_SB_TC_COUNT; i++) {
			cm = mlxsw_sp_sb_cm_get(mlxsw_sp, local_port, i,
						MLXSW_REG_SBXX_DIR_INGRESS);
			mlxsw_reg_sbsr_rec_unpack(sbsr_pl, rec_index++,
						  &cm->occ.cur, &cm->occ.max);
		}
		if (++masked_count == cb_ctx.masked_count)
			break;
	}
	masked_count = 0;
	for (local_port = cb_ctx.local_port_1;
	     local_port < mlxsw_core_max_ports(mlxsw_core); local_port++) {
		if (!mlxsw_sp->ports[local_port])
			continue;
		for (i = 0; i < MLXSW_SP_SB_TC_COUNT; i++) {
			cm = mlxsw_sp_sb_cm_get(mlxsw_sp, local_port, i,
						MLXSW_REG_SBXX_DIR_EGRESS);
			mlxsw_reg_sbsr_rec_unpack(sbsr_pl, rec_index++,
						  &cm->occ.cur, &cm->occ.max);
		}
		if (++masked_count == cb_ctx.masked_count)
			break;
	}
}

int mlxsw_sp_sb_occ_snapshot(struct mlxsw_core *mlxsw_core,
			     unsigned int sb_index)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_core_driver_priv(mlxsw_core);
	struct mlxsw_sp_sb_sr_occ_query_cb_ctx cb_ctx;
	unsigned long cb_priv;
	LIST_HEAD(bulk_list);
	char *sbsr_pl;
	u8 masked_count;
	u8 local_port_1;
	u8 local_port = 0;
	int i;
	int err;
	int err2;

	sbsr_pl = kmalloc(MLXSW_REG_SBSR_LEN, GFP_KERNEL);
	if (!sbsr_pl)
		return -ENOMEM;

next_batch:
	local_port++;
	local_port_1 = local_port;
	masked_count = 0;
	mlxsw_reg_sbsr_pack(sbsr_pl, false);
	for (i = 0; i < MLXSW_SP_SB_TC_COUNT; i++) {
		mlxsw_reg_sbsr_pg_buff_mask_set(sbsr_pl, i, 1);
		mlxsw_reg_sbsr_tclass_mask_set(sbsr_pl, i, 1);
	}
	for (; local_port < mlxsw_core_max_ports(mlxsw_core); local_port++) {
		if (!mlxsw_sp->ports[local_port])
			continue;
		mlxsw_reg_sbsr_ingress_port_mask_set(sbsr_pl, local_port, 1);
		mlxsw_reg_sbsr_egress_port_mask_set(sbsr_pl, local_port, 1);
		for (i = 0; i < MLXSW_SP_SB_POOL_COUNT; i++) {
			err = mlxsw_sp_sb_pm_occ_query(mlxsw_sp, local_port, i,
						       MLXSW_REG_SBXX_DIR_INGRESS,
						       &bulk_list);
			if (err)
				goto out;
			err = mlxsw_sp_sb_pm_occ_query(mlxsw_sp, local_port, i,
						       MLXSW_REG_SBXX_DIR_EGRESS,
						       &bulk_list);
			if (err)
				goto out;
		}
		if (++masked_count == MASKED_COUNT_MAX)
			goto do_query;
	}

do_query:
	cb_ctx.masked_count = masked_count;
	cb_ctx.local_port_1 = local_port_1;
	memcpy(&cb_priv, &cb_ctx, sizeof(cb_ctx));
	err = mlxsw_reg_trans_query(mlxsw_core, MLXSW_REG(sbsr), sbsr_pl,
				    &bulk_list, mlxsw_sp_sb_sr_occ_query_cb,
				    cb_priv);
	if (err)
		goto out;
	if (local_port < mlxsw_core_max_ports(mlxsw_core))
		goto next_batch;

out:
	err2 = mlxsw_reg_trans_bulk_wait(&bulk_list);
	if (!err)
		err = err2;
	kfree(sbsr_pl);
	return err;
}

int mlxsw_sp_sb_occ_max_clear(struct mlxsw_core *mlxsw_core,
			      unsigned int sb_index)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_core_driver_priv(mlxsw_core);
	LIST_HEAD(bulk_list);
	char *sbsr_pl;
	unsigned int masked_count;
	u8 local_port = 0;
	int i;
	int err;
	int err2;

	sbsr_pl = kmalloc(MLXSW_REG_SBSR_LEN, GFP_KERNEL);
	if (!sbsr_pl)
		return -ENOMEM;

next_batch:
	local_port++;
	masked_count = 0;
	mlxsw_reg_sbsr_pack(sbsr_pl, true);
	for (i = 0; i < MLXSW_SP_SB_TC_COUNT; i++) {
		mlxsw_reg_sbsr_pg_buff_mask_set(sbsr_pl, i, 1);
		mlxsw_reg_sbsr_tclass_mask_set(sbsr_pl, i, 1);
	}
	for (; local_port < mlxsw_core_max_ports(mlxsw_core); local_port++) {
		if (!mlxsw_sp->ports[local_port])
			continue;
		mlxsw_reg_sbsr_ingress_port_mask_set(sbsr_pl, local_port, 1);
		mlxsw_reg_sbsr_egress_port_mask_set(sbsr_pl, local_port, 1);
		for (i = 0; i < MLXSW_SP_SB_POOL_COUNT; i++) {
			err = mlxsw_sp_sb_pm_occ_clear(mlxsw_sp, local_port, i,
						       MLXSW_REG_SBXX_DIR_INGRESS,
						       &bulk_list);
			if (err)
				goto out;
			err = mlxsw_sp_sb_pm_occ_clear(mlxsw_sp, local_port, i,
						       MLXSW_REG_SBXX_DIR_EGRESS,
						       &bulk_list);
			if (err)
				goto out;
		}
		if (++masked_count == MASKED_COUNT_MAX)
			goto do_query;
	}

do_query:
	err = mlxsw_reg_trans_query(mlxsw_core, MLXSW_REG(sbsr), sbsr_pl,
				    &bulk_list, NULL, 0);
	if (err)
		goto out;
	if (local_port < mlxsw_core_max_ports(mlxsw_core))
		goto next_batch;

out:
	err2 = mlxsw_reg_trans_bulk_wait(&bulk_list);
	if (!err)
		err = err2;
	kfree(sbsr_pl);
	return err;
}

int mlxsw_sp_sb_occ_port_pool_get(struct mlxsw_core_port *mlxsw_core_port,
				  unsigned int sb_index, u16 pool_index,
				  u32 *p_cur, u32 *p_max)
{
	struct mlxsw_sp_port *mlxsw_sp_port =
			mlxsw_core_port_driver_priv(mlxsw_core_port);
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	u8 local_port = mlxsw_sp_port->local_port;
	u8 pool = pool_get(pool_index);
	enum mlxsw_reg_sbxx_dir dir = dir_get(pool_index);
	struct mlxsw_sp_sb_pm *pm = mlxsw_sp_sb_pm_get(mlxsw_sp, local_port,
						       pool, dir);

	*p_cur = mlxsw_sp_cells_bytes(mlxsw_sp, pm->occ.cur);
	*p_max = mlxsw_sp_cells_bytes(mlxsw_sp, pm->occ.max);
	return 0;
}

int mlxsw_sp_sb_occ_tc_port_bind_get(struct mlxsw_core_port *mlxsw_core_port,
				     unsigned int sb_index, u16 tc_index,
				     enum devlink_sb_pool_type pool_type,
				     u32 *p_cur, u32 *p_max)
{
	struct mlxsw_sp_port *mlxsw_sp_port =
			mlxsw_core_port_driver_priv(mlxsw_core_port);
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	u8 local_port = mlxsw_sp_port->local_port;
	u8 pg_buff = tc_index;
	enum mlxsw_reg_sbxx_dir dir = (enum mlxsw_reg_sbxx_dir) pool_type;
	struct mlxsw_sp_sb_cm *cm = mlxsw_sp_sb_cm_get(mlxsw_sp, local_port,
						       pg_buff, dir);

	*p_cur = mlxsw_sp_cells_bytes(mlxsw_sp, cm->occ.cur);
	*p_max = mlxsw_sp_cells_bytes(mlxsw_sp, cm->occ.max);
	return 0;
}
