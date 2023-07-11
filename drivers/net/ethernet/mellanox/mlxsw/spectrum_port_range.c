// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/* Copyright (c) 2023, NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#include <linux/bits.h>
#include <linux/netlink.h>
#include <linux/refcount.h>
#include <linux/xarray.h>
#include <net/devlink.h>

#include "spectrum.h"

struct mlxsw_sp_port_range_reg {
	struct mlxsw_sp_port_range range;
	refcount_t refcount;
	u32 index;
};

struct mlxsw_sp_port_range_core {
	struct xarray prr_xa;
	struct xa_limit prr_ids;
	atomic_t prr_count;
};

static int
mlxsw_sp_port_range_reg_configure(struct mlxsw_sp *mlxsw_sp,
				  const struct mlxsw_sp_port_range_reg *prr)
{
	char pprr_pl[MLXSW_REG_PPRR_LEN];

	/* We do not care if packet is IPv4/IPv6 and TCP/UDP, so set all four
	 * fields.
	 */
	mlxsw_reg_pprr_pack(pprr_pl, prr->index);
	mlxsw_reg_pprr_ipv4_set(pprr_pl, true);
	mlxsw_reg_pprr_ipv6_set(pprr_pl, true);
	mlxsw_reg_pprr_src_set(pprr_pl, prr->range.source);
	mlxsw_reg_pprr_dst_set(pprr_pl, !prr->range.source);
	mlxsw_reg_pprr_tcp_set(pprr_pl, true);
	mlxsw_reg_pprr_udp_set(pprr_pl, true);
	mlxsw_reg_pprr_port_range_min_set(pprr_pl, prr->range.min);
	mlxsw_reg_pprr_port_range_max_set(pprr_pl, prr->range.max);

	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(pprr), pprr_pl);
}

static struct mlxsw_sp_port_range_reg *
mlxsw_sp_port_range_reg_create(struct mlxsw_sp *mlxsw_sp,
			       const struct mlxsw_sp_port_range *range,
			       struct netlink_ext_ack *extack)
{
	struct mlxsw_sp_port_range_core *pr_core = mlxsw_sp->pr_core;
	struct mlxsw_sp_port_range_reg *prr;
	int err;

	prr = kzalloc(sizeof(*prr), GFP_KERNEL);
	if (!prr)
		return ERR_PTR(-ENOMEM);

	prr->range = *range;
	refcount_set(&prr->refcount, 1);

	err = xa_alloc(&pr_core->prr_xa, &prr->index, prr, pr_core->prr_ids,
		       GFP_KERNEL);
	if (err) {
		if (err == -EBUSY)
			NL_SET_ERR_MSG_MOD(extack, "Exceeded number of port range registers");
		goto err_xa_alloc;
	}

	err = mlxsw_sp_port_range_reg_configure(mlxsw_sp, prr);
	if (err) {
		NL_SET_ERR_MSG_MOD(extack, "Failed to configure port range register");
		goto err_reg_configure;
	}

	atomic_inc(&pr_core->prr_count);

	return prr;

err_reg_configure:
	xa_erase(&pr_core->prr_xa, prr->index);
err_xa_alloc:
	kfree(prr);
	return ERR_PTR(err);
}

static void mlxsw_sp_port_range_reg_destroy(struct mlxsw_sp *mlxsw_sp,
					    struct mlxsw_sp_port_range_reg *prr)
{
	struct mlxsw_sp_port_range_core *pr_core = mlxsw_sp->pr_core;

	atomic_dec(&pr_core->prr_count);
	xa_erase(&pr_core->prr_xa, prr->index);
	kfree(prr);
}

static struct mlxsw_sp_port_range_reg *
mlxsw_sp_port_range_reg_find(struct mlxsw_sp *mlxsw_sp,
			     const struct mlxsw_sp_port_range *range)
{
	struct mlxsw_sp_port_range_core *pr_core = mlxsw_sp->pr_core;
	struct mlxsw_sp_port_range_reg *prr;
	unsigned long index;

	xa_for_each(&pr_core->prr_xa, index, prr) {
		if (prr->range.min == range->min &&
		    prr->range.max == range->max &&
		    prr->range.source == range->source)
			return prr;
	}

	return NULL;
}

int mlxsw_sp_port_range_reg_get(struct mlxsw_sp *mlxsw_sp,
				const struct mlxsw_sp_port_range *range,
				struct netlink_ext_ack *extack,
				u8 *p_prr_index)
{
	struct mlxsw_sp_port_range_reg *prr;

	prr = mlxsw_sp_port_range_reg_find(mlxsw_sp, range);
	if (prr) {
		refcount_inc(&prr->refcount);
		*p_prr_index = prr->index;
		return 0;
	}

	prr = mlxsw_sp_port_range_reg_create(mlxsw_sp, range, extack);
	if (IS_ERR(prr))
		return PTR_ERR(prr);

	*p_prr_index = prr->index;

	return 0;
}

void mlxsw_sp_port_range_reg_put(struct mlxsw_sp *mlxsw_sp, u8 prr_index)
{
	struct mlxsw_sp_port_range_core *pr_core = mlxsw_sp->pr_core;
	struct mlxsw_sp_port_range_reg *prr;

	prr = xa_load(&pr_core->prr_xa, prr_index);
	if (WARN_ON(!prr))
		return;

	if (!refcount_dec_and_test(&prr->refcount))
		return;

	mlxsw_sp_port_range_reg_destroy(mlxsw_sp, prr);
}

static u64 mlxsw_sp_port_range_reg_occ_get(void *priv)
{
	struct mlxsw_sp_port_range_core *pr_core = priv;

	return atomic_read(&pr_core->prr_count);
}

int mlxsw_sp_port_range_init(struct mlxsw_sp *mlxsw_sp)
{
	struct mlxsw_sp_port_range_core *pr_core;
	struct mlxsw_core *core = mlxsw_sp->core;
	u64 max;

	if (!MLXSW_CORE_RES_VALID(core, ACL_MAX_L4_PORT_RANGE))
		return -EIO;
	max = MLXSW_CORE_RES_GET(core, ACL_MAX_L4_PORT_RANGE);

	/* Each port range register is represented using a single bit in the
	 * two bytes "l4_port_range" ACL key element.
	 */
	WARN_ON(max > BITS_PER_BYTE * sizeof(u16));

	pr_core = kzalloc(sizeof(*mlxsw_sp->pr_core), GFP_KERNEL);
	if (!pr_core)
		return -ENOMEM;
	mlxsw_sp->pr_core = pr_core;

	pr_core->prr_ids.max = max - 1;
	xa_init_flags(&pr_core->prr_xa, XA_FLAGS_ALLOC);

	devl_resource_occ_get_register(priv_to_devlink(core),
				       MLXSW_SP_RESOURCE_PORT_RANGE_REGISTERS,
				       mlxsw_sp_port_range_reg_occ_get,
				       pr_core);

	return 0;
}

void mlxsw_sp_port_range_fini(struct mlxsw_sp *mlxsw_sp)
{
	struct mlxsw_sp_port_range_core *pr_core = mlxsw_sp->pr_core;

	devl_resource_occ_get_unregister(priv_to_devlink(mlxsw_sp->core),
					 MLXSW_SP_RESOURCE_PORT_RANGE_REGISTERS);
	WARN_ON(!xa_empty(&pr_core->prr_xa));
	xa_destroy(&pr_core->prr_xa);
	kfree(pr_core);
}
