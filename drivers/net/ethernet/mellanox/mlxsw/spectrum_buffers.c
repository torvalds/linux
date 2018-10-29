// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/* Copyright (c) 2015-2018 Mellanox Technologies. All rights reserved */

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
	u16 pool_index;
	struct mlxsw_cp_sb_occ occ;
};

#define MLXSW_SP_SB_INFI -1U

struct mlxsw_sp_sb_pm {
	u32 min_buff;
	u32 max_buff;
	struct mlxsw_cp_sb_occ occ;
};

struct mlxsw_sp_sb_pool_des {
	enum mlxsw_reg_sbxx_dir dir;
	u8 pool;
};

/* Order ingress pools before egress pools. */
static const struct mlxsw_sp_sb_pool_des mlxsw_sp_sb_pool_dess[] = {
	{MLXSW_REG_SBXX_DIR_INGRESS, 0},
	{MLXSW_REG_SBXX_DIR_INGRESS, 1},
	{MLXSW_REG_SBXX_DIR_INGRESS, 2},
	{MLXSW_REG_SBXX_DIR_INGRESS, 3},
	{MLXSW_REG_SBXX_DIR_EGRESS, 0},
	{MLXSW_REG_SBXX_DIR_EGRESS, 1},
	{MLXSW_REG_SBXX_DIR_EGRESS, 2},
	{MLXSW_REG_SBXX_DIR_EGRESS, 3},
	{MLXSW_REG_SBXX_DIR_EGRESS, 15},
};

#define MLXSW_SP_SB_POOL_DESS_LEN ARRAY_SIZE(mlxsw_sp_sb_pool_dess)

#define MLXSW_SP_SB_ING_TC_COUNT 8
#define MLXSW_SP_SB_EG_TC_COUNT 16

struct mlxsw_sp_sb_port {
	struct mlxsw_sp_sb_cm ing_cms[MLXSW_SP_SB_ING_TC_COUNT];
	struct mlxsw_sp_sb_cm eg_cms[MLXSW_SP_SB_EG_TC_COUNT];
	struct mlxsw_sp_sb_pm pms[MLXSW_SP_SB_POOL_DESS_LEN];
};

struct mlxsw_sp_sb {
	struct mlxsw_sp_sb_pr prs[MLXSW_SP_SB_POOL_DESS_LEN];
	struct mlxsw_sp_sb_port *ports;
	u32 cell_size;
	u64 sb_size;
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
						 u16 pool_index)
{
	return &mlxsw_sp->sb->prs[pool_index];
}

static bool mlxsw_sp_sb_cm_exists(u8 pg_buff, enum mlxsw_reg_sbxx_dir dir)
{
	if (dir == MLXSW_REG_SBXX_DIR_INGRESS)
		return pg_buff < MLXSW_SP_SB_ING_TC_COUNT;
	else
		return pg_buff < MLXSW_SP_SB_EG_TC_COUNT;
}

static struct mlxsw_sp_sb_cm *mlxsw_sp_sb_cm_get(struct mlxsw_sp *mlxsw_sp,
						 u8 local_port, u8 pg_buff,
						 enum mlxsw_reg_sbxx_dir dir)
{
	struct mlxsw_sp_sb_port *sb_port = &mlxsw_sp->sb->ports[local_port];

	WARN_ON(!mlxsw_sp_sb_cm_exists(pg_buff, dir));
	if (dir == MLXSW_REG_SBXX_DIR_INGRESS)
		return &sb_port->ing_cms[pg_buff];
	else
		return &sb_port->eg_cms[pg_buff];
}

static struct mlxsw_sp_sb_pm *mlxsw_sp_sb_pm_get(struct mlxsw_sp *mlxsw_sp,
						 u8 local_port, u16 pool_index)
{
	return &mlxsw_sp->sb->ports[local_port].pms[pool_index];
}

static int mlxsw_sp_sb_pr_write(struct mlxsw_sp *mlxsw_sp, u16 pool_index,
				enum mlxsw_reg_sbpr_mode mode,
				u32 size, bool infi_size)
{
	const struct mlxsw_sp_sb_pool_des *des =
		&mlxsw_sp_sb_pool_dess[pool_index];
	char sbpr_pl[MLXSW_REG_SBPR_LEN];
	struct mlxsw_sp_sb_pr *pr;
	int err;

	mlxsw_reg_sbpr_pack(sbpr_pl, des->pool, des->dir, mode,
			    size, infi_size);
	err = mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(sbpr), sbpr_pl);
	if (err)
		return err;

	if (infi_size)
		size = mlxsw_sp_bytes_cells(mlxsw_sp, mlxsw_sp->sb->sb_size);
	pr = mlxsw_sp_sb_pr_get(mlxsw_sp, pool_index);
	pr->mode = mode;
	pr->size = size;
	return 0;
}

static int mlxsw_sp_sb_cm_write(struct mlxsw_sp *mlxsw_sp, u8 local_port,
				u8 pg_buff, u32 min_buff, u32 max_buff,
				bool infi_max, u16 pool_index)
{
	const struct mlxsw_sp_sb_pool_des *des =
		&mlxsw_sp_sb_pool_dess[pool_index];
	char sbcm_pl[MLXSW_REG_SBCM_LEN];
	struct mlxsw_sp_sb_cm *cm;
	int err;

	mlxsw_reg_sbcm_pack(sbcm_pl, local_port, pg_buff, des->dir,
			    min_buff, max_buff, infi_max, des->pool);
	err = mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(sbcm), sbcm_pl);
	if (err)
		return err;

	if (mlxsw_sp_sb_cm_exists(pg_buff, des->dir)) {
		if (infi_max)
			max_buff = mlxsw_sp_bytes_cells(mlxsw_sp,
							mlxsw_sp->sb->sb_size);

		cm = mlxsw_sp_sb_cm_get(mlxsw_sp, local_port, pg_buff,
					des->dir);
		cm->min_buff = min_buff;
		cm->max_buff = max_buff;
		cm->pool_index = pool_index;
	}
	return 0;
}

static int mlxsw_sp_sb_pm_write(struct mlxsw_sp *mlxsw_sp, u8 local_port,
				u16 pool_index, u32 min_buff, u32 max_buff)
{
	const struct mlxsw_sp_sb_pool_des *des =
		&mlxsw_sp_sb_pool_dess[pool_index];
	char sbpm_pl[MLXSW_REG_SBPM_LEN];
	struct mlxsw_sp_sb_pm *pm;
	int err;

	mlxsw_reg_sbpm_pack(sbpm_pl, local_port, des->pool, des->dir, false,
			    min_buff, max_buff);
	err = mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(sbpm), sbpm_pl);
	if (err)
		return err;

	pm = mlxsw_sp_sb_pm_get(mlxsw_sp, local_port, pool_index);
	pm->min_buff = min_buff;
	pm->max_buff = max_buff;
	return 0;
}

static int mlxsw_sp_sb_pm_occ_clear(struct mlxsw_sp *mlxsw_sp, u8 local_port,
				    u16 pool_index, struct list_head *bulk_list)
{
	const struct mlxsw_sp_sb_pool_des *des =
		&mlxsw_sp_sb_pool_dess[pool_index];
	char sbpm_pl[MLXSW_REG_SBPM_LEN];

	mlxsw_reg_sbpm_pack(sbpm_pl, local_port, des->pool, des->dir,
			    true, 0, 0);
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
				    u16 pool_index, struct list_head *bulk_list)
{
	const struct mlxsw_sp_sb_pool_des *des =
		&mlxsw_sp_sb_pool_dess[pool_index];
	char sbpm_pl[MLXSW_REG_SBPM_LEN];
	struct mlxsw_sp_sb_pm *pm;

	pm = mlxsw_sp_sb_pm_get(mlxsw_sp, local_port, pool_index);
	mlxsw_reg_sbpm_pack(sbpm_pl, local_port, des->pool, des->dir,
			    false, 0, 0);
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

static const struct mlxsw_sp_sb_pr mlxsw_sp_sb_prs[] = {
	/* Ingress pools. */
	MLXSW_SP_SB_PR(MLXSW_REG_SBPR_MODE_DYNAMIC,
		       MLXSW_SP_SB_PR_INGRESS_SIZE),
	MLXSW_SP_SB_PR(MLXSW_REG_SBPR_MODE_DYNAMIC, 0),
	MLXSW_SP_SB_PR(MLXSW_REG_SBPR_MODE_DYNAMIC, 0),
	MLXSW_SP_SB_PR(MLXSW_REG_SBPR_MODE_DYNAMIC,
		       MLXSW_SP_SB_PR_INGRESS_MNG_SIZE),
	/* Egress pools. */
	MLXSW_SP_SB_PR(MLXSW_REG_SBPR_MODE_DYNAMIC, MLXSW_SP_SB_PR_EGRESS_SIZE),
	MLXSW_SP_SB_PR(MLXSW_REG_SBPR_MODE_DYNAMIC, 0),
	MLXSW_SP_SB_PR(MLXSW_REG_SBPR_MODE_DYNAMIC, 0),
	MLXSW_SP_SB_PR(MLXSW_REG_SBPR_MODE_DYNAMIC, 0),
	MLXSW_SP_SB_PR(MLXSW_REG_SBPR_MODE_STATIC, MLXSW_SP_SB_INFI),
};

#define MLXSW_SP_SB_PRS_LEN ARRAY_SIZE(mlxsw_sp_sb_prs)

static int mlxsw_sp_sb_prs_init(struct mlxsw_sp *mlxsw_sp,
				const struct mlxsw_sp_sb_pr *prs,
				size_t prs_len)
{
	int i;
	int err;

	for (i = 0; i < prs_len; i++) {
		u32 size = prs[i].size;
		u32 size_cells;

		if (size == MLXSW_SP_SB_INFI) {
			err = mlxsw_sp_sb_pr_write(mlxsw_sp, i, prs[i].mode,
						   0, true);
		} else {
			size_cells = mlxsw_sp_bytes_cells(mlxsw_sp, size);
			err = mlxsw_sp_sb_pr_write(mlxsw_sp, i, prs[i].mode,
						   size_cells, false);
		}
		if (err)
			return err;
	}
	return 0;
}

#define MLXSW_SP_SB_CM(_min_buff, _max_buff, _pool)	\
	{						\
		.min_buff = _min_buff,			\
		.max_buff = _max_buff,			\
		.pool_index = _pool,			\
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
	MLXSW_SP_SB_CM(1500, 9, 4),
	MLXSW_SP_SB_CM(1500, 9, 4),
	MLXSW_SP_SB_CM(1500, 9, 4),
	MLXSW_SP_SB_CM(1500, 9, 4),
	MLXSW_SP_SB_CM(1500, 9, 4),
	MLXSW_SP_SB_CM(1500, 9, 4),
	MLXSW_SP_SB_CM(1500, 9, 4),
	MLXSW_SP_SB_CM(1500, 9, 4),
	MLXSW_SP_SB_CM(0, MLXSW_SP_SB_INFI, 8),
	MLXSW_SP_SB_CM(0, MLXSW_SP_SB_INFI, 8),
	MLXSW_SP_SB_CM(0, MLXSW_SP_SB_INFI, 8),
	MLXSW_SP_SB_CM(0, MLXSW_SP_SB_INFI, 8),
	MLXSW_SP_SB_CM(0, MLXSW_SP_SB_INFI, 8),
	MLXSW_SP_SB_CM(0, MLXSW_SP_SB_INFI, 8),
	MLXSW_SP_SB_CM(0, MLXSW_SP_SB_INFI, 8),
	MLXSW_SP_SB_CM(0, MLXSW_SP_SB_INFI, 8),
	MLXSW_SP_SB_CM(1, 0xff, 4),
};

#define MLXSW_SP_SB_CMS_EGRESS_LEN ARRAY_SIZE(mlxsw_sp_sb_cms_egress)

#define MLXSW_SP_CPU_PORT_SB_CM MLXSW_SP_SB_CM(0, 0, 4)

static const struct mlxsw_sp_sb_cm mlxsw_sp_cpu_port_sb_cms[] = {
	MLXSW_SP_CPU_PORT_SB_CM,
	MLXSW_SP_SB_CM(MLXSW_PORT_MAX_MTU, 0, 4),
	MLXSW_SP_SB_CM(MLXSW_PORT_MAX_MTU, 0, 4),
	MLXSW_SP_SB_CM(MLXSW_PORT_MAX_MTU, 0, 4),
	MLXSW_SP_SB_CM(MLXSW_PORT_MAX_MTU, 0, 4),
	MLXSW_SP_SB_CM(MLXSW_PORT_MAX_MTU, 0, 4),
	MLXSW_SP_CPU_PORT_SB_CM,
	MLXSW_SP_SB_CM(MLXSW_PORT_MAX_MTU, 0, 4),
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

static bool
mlxsw_sp_sb_pool_is_static(struct mlxsw_sp *mlxsw_sp, u16 pool_index)
{
	struct mlxsw_sp_sb_pr *pr = mlxsw_sp_sb_pr_get(mlxsw_sp, pool_index);

	return pr->mode == MLXSW_REG_SBPR_MODE_STATIC;
}

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
		u32 max_buff;

		if (i == 8 && dir == MLXSW_REG_SBXX_DIR_INGRESS)
			continue; /* PG number 8 does not exist, skip it */
		cm = &cms[i];
		if (WARN_ON(mlxsw_sp_sb_pool_dess[cm->pool_index].dir != dir))
			continue;

		min_buff = mlxsw_sp_bytes_cells(mlxsw_sp, cm->min_buff);
		max_buff = cm->max_buff;
		if (max_buff == MLXSW_SP_SB_INFI) {
			err = mlxsw_sp_sb_cm_write(mlxsw_sp, local_port, i,
						   min_buff, 0,
						   true, cm->pool_index);
		} else {
			if (mlxsw_sp_sb_pool_is_static(mlxsw_sp,
						       cm->pool_index))
				max_buff = mlxsw_sp_bytes_cells(mlxsw_sp,
								max_buff);
			err = mlxsw_sp_sb_cm_write(mlxsw_sp, local_port, i,
						   min_buff, max_buff,
						   false, cm->pool_index);
		}
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

static const struct mlxsw_sp_sb_pm mlxsw_sp_sb_pms[] = {
	/* Ingress pools. */
	MLXSW_SP_SB_PM(0, MLXSW_REG_SBXX_DYN_MAX_BUFF_MAX),
	MLXSW_SP_SB_PM(0, MLXSW_REG_SBXX_DYN_MAX_BUFF_MIN),
	MLXSW_SP_SB_PM(0, MLXSW_REG_SBXX_DYN_MAX_BUFF_MIN),
	MLXSW_SP_SB_PM(0, MLXSW_REG_SBXX_DYN_MAX_BUFF_MAX),
	/* Egress pools. */
	MLXSW_SP_SB_PM(0, 7),
	MLXSW_SP_SB_PM(0, MLXSW_REG_SBXX_DYN_MAX_BUFF_MIN),
	MLXSW_SP_SB_PM(0, MLXSW_REG_SBXX_DYN_MAX_BUFF_MIN),
	MLXSW_SP_SB_PM(0, MLXSW_REG_SBXX_DYN_MAX_BUFF_MIN),
	MLXSW_SP_SB_PM(10000, 90000),
};

#define MLXSW_SP_SB_PMS_LEN ARRAY_SIZE(mlxsw_sp_sb_pms)

static int mlxsw_sp_port_sb_pms_init(struct mlxsw_sp_port *mlxsw_sp_port)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	int i;
	int err;

	for (i = 0; i < MLXSW_SP_SB_PMS_LEN; i++) {
		const struct mlxsw_sp_sb_pm *pm = &mlxsw_sp_sb_pms[i];
		u32 max_buff;
		u32 min_buff;

		min_buff = mlxsw_sp_bytes_cells(mlxsw_sp, pm->min_buff);
		max_buff = pm->max_buff;
		if (mlxsw_sp_sb_pool_is_static(mlxsw_sp, i))
			max_buff = mlxsw_sp_bytes_cells(mlxsw_sp, max_buff);
		err = mlxsw_sp_sb_pm_write(mlxsw_sp, mlxsw_sp_port->local_port,
					   i, min_buff, max_buff);
		if (err)
			return err;
	}
	return 0;
}

struct mlxsw_sp_sb_mm {
	u32 min_buff;
	u32 max_buff;
	u16 pool_index;
};

#define MLXSW_SP_SB_MM(_min_buff, _max_buff, _pool)	\
	{						\
		.min_buff = _min_buff,			\
		.max_buff = _max_buff,			\
		.pool_index = _pool,			\
	}

static const struct mlxsw_sp_sb_mm mlxsw_sp_sb_mms[] = {
	MLXSW_SP_SB_MM(0, 6, 4),
	MLXSW_SP_SB_MM(0, 6, 4),
	MLXSW_SP_SB_MM(0, 6, 4),
	MLXSW_SP_SB_MM(0, 6, 4),
	MLXSW_SP_SB_MM(0, 6, 4),
	MLXSW_SP_SB_MM(0, 6, 4),
	MLXSW_SP_SB_MM(0, 6, 4),
	MLXSW_SP_SB_MM(0, 6, 4),
	MLXSW_SP_SB_MM(0, 6, 4),
	MLXSW_SP_SB_MM(0, 6, 4),
	MLXSW_SP_SB_MM(0, 6, 4),
	MLXSW_SP_SB_MM(0, 6, 4),
	MLXSW_SP_SB_MM(0, 6, 4),
	MLXSW_SP_SB_MM(0, 6, 4),
	MLXSW_SP_SB_MM(0, 6, 4),
};

#define MLXSW_SP_SB_MMS_LEN ARRAY_SIZE(mlxsw_sp_sb_mms)

static int mlxsw_sp_sb_mms_init(struct mlxsw_sp *mlxsw_sp)
{
	char sbmm_pl[MLXSW_REG_SBMM_LEN];
	int i;
	int err;

	for (i = 0; i < MLXSW_SP_SB_MMS_LEN; i++) {
		const struct mlxsw_sp_sb_pool_des *des;
		const struct mlxsw_sp_sb_mm *mc;
		u32 min_buff;

		mc = &mlxsw_sp_sb_mms[i];
		des = &mlxsw_sp_sb_pool_dess[mc->pool_index];
		/* All pools used by sb_mm's are initialized using dynamic
		 * thresholds, therefore 'max_buff' isn't specified in cells.
		 */
		min_buff = mlxsw_sp_bytes_cells(mlxsw_sp, mc->min_buff);
		mlxsw_reg_sbmm_pack(sbmm_pl, i, min_buff, mc->max_buff,
				    des->pool);
		err = mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(sbmm), sbmm_pl);
		if (err)
			return err;
	}
	return 0;
}

static void mlxsw_sp_pool_count(u16 *p_ingress_len, u16 *p_egress_len)
{
	int i;

	for (i = 0; i < MLXSW_SP_SB_POOL_DESS_LEN; ++i)
		if (mlxsw_sp_sb_pool_dess[i].dir == MLXSW_REG_SBXX_DIR_EGRESS)
			goto out;
	WARN(1, "No egress pools\n");

out:
	*p_ingress_len = i;
	*p_egress_len = MLXSW_SP_SB_POOL_DESS_LEN - i;
}

int mlxsw_sp_buffers_init(struct mlxsw_sp *mlxsw_sp)
{
	u16 ing_pool_count;
	u16 eg_pool_count;
	int err;

	if (!MLXSW_CORE_RES_VALID(mlxsw_sp->core, CELL_SIZE))
		return -EIO;

	if (!MLXSW_CORE_RES_VALID(mlxsw_sp->core, MAX_BUFFER_SIZE))
		return -EIO;

	mlxsw_sp->sb = kzalloc(sizeof(*mlxsw_sp->sb), GFP_KERNEL);
	if (!mlxsw_sp->sb)
		return -ENOMEM;
	mlxsw_sp->sb->cell_size = MLXSW_CORE_RES_GET(mlxsw_sp->core, CELL_SIZE);
	mlxsw_sp->sb->sb_size = MLXSW_CORE_RES_GET(mlxsw_sp->core,
						   MAX_BUFFER_SIZE);

	err = mlxsw_sp_sb_ports_init(mlxsw_sp);
	if (err)
		goto err_sb_ports_init;
	err = mlxsw_sp_sb_prs_init(mlxsw_sp, mlxsw_sp_sb_prs,
				   MLXSW_SP_SB_PRS_LEN);
	if (err)
		goto err_sb_prs_init;
	err = mlxsw_sp_cpu_port_sb_cms_init(mlxsw_sp);
	if (err)
		goto err_sb_cpu_port_sb_cms_init;
	err = mlxsw_sp_sb_mms_init(mlxsw_sp);
	if (err)
		goto err_sb_mms_init;
	mlxsw_sp_pool_count(&ing_pool_count, &eg_pool_count);
	err = devlink_sb_register(priv_to_devlink(mlxsw_sp->core), 0,
				  mlxsw_sp->sb->sb_size,
				  ing_pool_count,
				  eg_pool_count,
				  MLXSW_SP_SB_ING_TC_COUNT,
				  MLXSW_SP_SB_EG_TC_COUNT);
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

int mlxsw_sp_sb_pool_get(struct mlxsw_core *mlxsw_core,
			 unsigned int sb_index, u16 pool_index,
			 struct devlink_sb_pool_info *pool_info)
{
	enum mlxsw_reg_sbxx_dir dir = mlxsw_sp_sb_pool_dess[pool_index].dir;
	struct mlxsw_sp *mlxsw_sp = mlxsw_core_driver_priv(mlxsw_core);
	struct mlxsw_sp_sb_pr *pr;

	pr = mlxsw_sp_sb_pr_get(mlxsw_sp, pool_index);
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
	enum mlxsw_reg_sbpr_mode mode;

	if (size > MLXSW_CORE_RES_GET(mlxsw_sp->core, MAX_BUFFER_SIZE))
		return -EINVAL;

	mode = (enum mlxsw_reg_sbpr_mode) threshold_type;
	return mlxsw_sp_sb_pr_write(mlxsw_sp, pool_index, mode,
				    pool_size, false);
}

#define MLXSW_SP_SB_THRESHOLD_TO_ALPHA_OFFSET (-2) /* 3->1, 16->14 */

static u32 mlxsw_sp_sb_threshold_out(struct mlxsw_sp *mlxsw_sp, u16 pool_index,
				     u32 max_buff)
{
	struct mlxsw_sp_sb_pr *pr = mlxsw_sp_sb_pr_get(mlxsw_sp, pool_index);

	if (pr->mode == MLXSW_REG_SBPR_MODE_DYNAMIC)
		return max_buff - MLXSW_SP_SB_THRESHOLD_TO_ALPHA_OFFSET;
	return mlxsw_sp_cells_bytes(mlxsw_sp, max_buff);
}

static int mlxsw_sp_sb_threshold_in(struct mlxsw_sp *mlxsw_sp, u16 pool_index,
				    u32 threshold, u32 *p_max_buff)
{
	struct mlxsw_sp_sb_pr *pr = mlxsw_sp_sb_pr_get(mlxsw_sp, pool_index);

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
	struct mlxsw_sp_sb_pm *pm = mlxsw_sp_sb_pm_get(mlxsw_sp, local_port,
						       pool_index);

	*p_threshold = mlxsw_sp_sb_threshold_out(mlxsw_sp, pool_index,
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
	u32 max_buff;
	int err;

	err = mlxsw_sp_sb_threshold_in(mlxsw_sp, pool_index,
				       threshold, &max_buff);
	if (err)
		return err;

	return mlxsw_sp_sb_pm_write(mlxsw_sp, local_port, pool_index,
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

	*p_threshold = mlxsw_sp_sb_threshold_out(mlxsw_sp, cm->pool_index,
						 cm->max_buff);
	*p_pool_index = cm->pool_index;
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
	u32 max_buff;
	int err;

	if (dir != mlxsw_sp_sb_pool_dess[pool_index].dir)
		return -EINVAL;

	err = mlxsw_sp_sb_threshold_in(mlxsw_sp, pool_index,
				       threshold, &max_buff);
	if (err)
		return err;

	return mlxsw_sp_sb_cm_write(mlxsw_sp, local_port, pg_buff,
				    0, max_buff, false, pool_index);
}

#define MASKED_COUNT_MAX \
	(MLXSW_REG_SBSR_REC_MAX_COUNT / \
	 (MLXSW_SP_SB_ING_TC_COUNT + MLXSW_SP_SB_EG_TC_COUNT))

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
		for (i = 0; i < MLXSW_SP_SB_ING_TC_COUNT; i++) {
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
		for (i = 0; i < MLXSW_SP_SB_EG_TC_COUNT; i++) {
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
	for (i = 0; i < MLXSW_SP_SB_ING_TC_COUNT; i++)
		mlxsw_reg_sbsr_pg_buff_mask_set(sbsr_pl, i, 1);
	for (i = 0; i < MLXSW_SP_SB_EG_TC_COUNT; i++)
		mlxsw_reg_sbsr_tclass_mask_set(sbsr_pl, i, 1);
	for (; local_port < mlxsw_core_max_ports(mlxsw_core); local_port++) {
		if (!mlxsw_sp->ports[local_port])
			continue;
		mlxsw_reg_sbsr_ingress_port_mask_set(sbsr_pl, local_port, 1);
		mlxsw_reg_sbsr_egress_port_mask_set(sbsr_pl, local_port, 1);
		for (i = 0; i < MLXSW_SP_SB_POOL_DESS_LEN; i++) {
			err = mlxsw_sp_sb_pm_occ_query(mlxsw_sp, local_port, i,
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
	for (i = 0; i < MLXSW_SP_SB_ING_TC_COUNT; i++)
		mlxsw_reg_sbsr_pg_buff_mask_set(sbsr_pl, i, 1);
	for (i = 0; i < MLXSW_SP_SB_EG_TC_COUNT; i++)
		mlxsw_reg_sbsr_tclass_mask_set(sbsr_pl, i, 1);
	for (; local_port < mlxsw_core_max_ports(mlxsw_core); local_port++) {
		if (!mlxsw_sp->ports[local_port])
			continue;
		mlxsw_reg_sbsr_ingress_port_mask_set(sbsr_pl, local_port, 1);
		mlxsw_reg_sbsr_egress_port_mask_set(sbsr_pl, local_port, 1);
		for (i = 0; i < MLXSW_SP_SB_POOL_DESS_LEN; i++) {
			err = mlxsw_sp_sb_pm_occ_clear(mlxsw_sp, local_port, i,
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
	struct mlxsw_sp_sb_pm *pm = mlxsw_sp_sb_pm_get(mlxsw_sp, local_port,
						       pool_index);

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
