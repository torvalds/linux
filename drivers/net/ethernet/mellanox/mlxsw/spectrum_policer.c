// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/* Copyright (c) 2020 Mellanox Technologies. All rights reserved */

#include <linux/idr.h>
#include <linux/log2.h>
#include <linux/mutex.h>
#include <linux/netlink.h>
#include <net/devlink.h>

#include "spectrum.h"

struct mlxsw_sp_policer_family {
	enum mlxsw_sp_policer_type type;
	enum mlxsw_reg_qpcr_g qpcr_type;
	struct mlxsw_sp *mlxsw_sp;
	u16 start_index; /* Inclusive */
	u16 end_index; /* Exclusive */
	struct idr policer_idr;
	struct mutex lock; /* Protects policer_idr */
	atomic_t policers_count;
	const struct mlxsw_sp_policer_family_ops *ops;
};

struct mlxsw_sp_policer {
	struct mlxsw_sp_policer_params params;
	u16 index;
};

struct mlxsw_sp_policer_family_ops {
	int (*init)(struct mlxsw_sp_policer_family *family);
	void (*fini)(struct mlxsw_sp_policer_family *family);
	int (*policer_index_alloc)(struct mlxsw_sp_policer_family *family,
				   struct mlxsw_sp_policer *policer);
	struct mlxsw_sp_policer * (*policer_index_free)(struct mlxsw_sp_policer_family *family,
							u16 policer_index);
	int (*policer_init)(struct mlxsw_sp_policer_family *family,
			    const struct mlxsw_sp_policer *policer);
	int (*policer_params_check)(const struct mlxsw_sp_policer_family *family,
				    const struct mlxsw_sp_policer_params *params,
				    struct netlink_ext_ack *extack);
};

struct mlxsw_sp_policer_core {
	struct mlxsw_sp_policer_family *family_arr[MLXSW_SP_POLICER_TYPE_MAX + 1];
	const struct mlxsw_sp_policer_core_ops *ops;
	u8 lowest_bs_bits;
	u8 highest_bs_bits;
};

struct mlxsw_sp_policer_core_ops {
	int (*init)(struct mlxsw_sp_policer_core *policer_core);
};

static u64 mlxsw_sp_policer_rate_bytes_ps_kbps(u64 rate_bytes_ps)
{
	return div_u64(rate_bytes_ps, 1000) * BITS_PER_BYTE;
}

static u8 mlxsw_sp_policer_burst_bytes_hw_units(u64 burst_bytes)
{
	/* Provided burst size is in bytes. The ASIC burst size value is
	 * (2 ^ bs) * 512 bits. Convert the provided size to 512-bit units.
	 */
	u64 bs512 = div_u64(burst_bytes, 64);

	if (!bs512)
		return 0;

	return fls64(bs512) - 1;
}

static u64 mlxsw_sp_policer_single_rate_occ_get(void *priv)
{
	struct mlxsw_sp_policer_family *family = priv;

	return atomic_read(&family->policers_count);
}

static int
mlxsw_sp_policer_single_rate_family_init(struct mlxsw_sp_policer_family *family)
{
	struct mlxsw_core *core = family->mlxsw_sp->core;
	struct devlink *devlink;

	/* CPU policers are allocated from the first N policers in the global
	 * range, so skip them.
	 */
	if (!MLXSW_CORE_RES_VALID(core, MAX_GLOBAL_POLICERS) ||
	    !MLXSW_CORE_RES_VALID(core, MAX_CPU_POLICERS))
		return -EIO;

	family->start_index = MLXSW_CORE_RES_GET(core, MAX_CPU_POLICERS);
	family->end_index = MLXSW_CORE_RES_GET(core, MAX_GLOBAL_POLICERS);

	atomic_set(&family->policers_count, 0);
	devlink = priv_to_devlink(core);
	devlink_resource_occ_get_register(devlink,
					  MLXSW_SP_RESOURCE_SINGLE_RATE_POLICERS,
					  mlxsw_sp_policer_single_rate_occ_get,
					  family);

	return 0;
}

static void
mlxsw_sp_policer_single_rate_family_fini(struct mlxsw_sp_policer_family *family)
{
	struct devlink *devlink = priv_to_devlink(family->mlxsw_sp->core);

	devlink_resource_occ_get_unregister(devlink,
					    MLXSW_SP_RESOURCE_SINGLE_RATE_POLICERS);
	WARN_ON(atomic_read(&family->policers_count) != 0);
}

static int
mlxsw_sp_policer_single_rate_index_alloc(struct mlxsw_sp_policer_family *family,
					 struct mlxsw_sp_policer *policer)
{
	int id;

	mutex_lock(&family->lock);
	id = idr_alloc(&family->policer_idr, policer, family->start_index,
		       family->end_index, GFP_KERNEL);
	mutex_unlock(&family->lock);

	if (id < 0)
		return id;

	atomic_inc(&family->policers_count);
	policer->index = id;

	return 0;
}

static struct mlxsw_sp_policer *
mlxsw_sp_policer_single_rate_index_free(struct mlxsw_sp_policer_family *family,
					u16 policer_index)
{
	struct mlxsw_sp_policer *policer;

	atomic_dec(&family->policers_count);

	mutex_lock(&family->lock);
	policer = idr_remove(&family->policer_idr, policer_index);
	mutex_unlock(&family->lock);

	WARN_ON(!policer);

	return policer;
}

static int
mlxsw_sp_policer_single_rate_init(struct mlxsw_sp_policer_family *family,
				  const struct mlxsw_sp_policer *policer)
{
	u64 rate_kbps = mlxsw_sp_policer_rate_bytes_ps_kbps(policer->params.rate);
	u8 bs = mlxsw_sp_policer_burst_bytes_hw_units(policer->params.burst);
	struct mlxsw_sp *mlxsw_sp = family->mlxsw_sp;
	char qpcr_pl[MLXSW_REG_QPCR_LEN];

	mlxsw_reg_qpcr_pack(qpcr_pl, policer->index, MLXSW_REG_QPCR_IR_UNITS_K,
			    true, rate_kbps, bs);
	mlxsw_reg_qpcr_clear_counter_set(qpcr_pl, true);

	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(qpcr), qpcr_pl);
}

static int
mlxsw_sp_policer_single_rate_params_check(const struct mlxsw_sp_policer_family *family,
					  const struct mlxsw_sp_policer_params *params,
					  struct netlink_ext_ack *extack)
{
	struct mlxsw_sp_policer_core *policer_core = family->mlxsw_sp->policer_core;
	u64 rate_bps = params->rate * BITS_PER_BYTE;
	u8 bs;

	if (!params->bytes) {
		NL_SET_ERR_MSG_MOD(extack, "Only bandwidth policing is currently supported by single rate policers");
		return -EINVAL;
	}

	if (!is_power_of_2(params->burst)) {
		NL_SET_ERR_MSG_MOD(extack, "Policer burst size is not power of two");
		return -EINVAL;
	}

	bs = mlxsw_sp_policer_burst_bytes_hw_units(params->burst);

	if (bs < policer_core->lowest_bs_bits) {
		NL_SET_ERR_MSG_MOD(extack, "Policer burst size lower than limit");
		return -EINVAL;
	}

	if (bs > policer_core->highest_bs_bits) {
		NL_SET_ERR_MSG_MOD(extack, "Policer burst size higher than limit");
		return -EINVAL;
	}

	if (rate_bps < MLXSW_REG_QPCR_LOWEST_CIR_BITS) {
		NL_SET_ERR_MSG_MOD(extack, "Policer rate lower than limit");
		return -EINVAL;
	}

	if (rate_bps > MLXSW_REG_QPCR_HIGHEST_CIR_BITS) {
		NL_SET_ERR_MSG_MOD(extack, "Policer rate higher than limit");
		return -EINVAL;
	}

	return 0;
}

static const struct mlxsw_sp_policer_family_ops mlxsw_sp_policer_single_rate_ops = {
	.init			= mlxsw_sp_policer_single_rate_family_init,
	.fini			= mlxsw_sp_policer_single_rate_family_fini,
	.policer_index_alloc	= mlxsw_sp_policer_single_rate_index_alloc,
	.policer_index_free	= mlxsw_sp_policer_single_rate_index_free,
	.policer_init		= mlxsw_sp_policer_single_rate_init,
	.policer_params_check	= mlxsw_sp_policer_single_rate_params_check,
};

static const struct mlxsw_sp_policer_family mlxsw_sp_policer_single_rate_family = {
	.type		= MLXSW_SP_POLICER_TYPE_SINGLE_RATE,
	.qpcr_type	= MLXSW_REG_QPCR_G_GLOBAL,
	.ops		= &mlxsw_sp_policer_single_rate_ops,
};

static const struct mlxsw_sp_policer_family *mlxsw_sp_policer_family_arr[] = {
	[MLXSW_SP_POLICER_TYPE_SINGLE_RATE]	= &mlxsw_sp_policer_single_rate_family,
};

int mlxsw_sp_policer_add(struct mlxsw_sp *mlxsw_sp,
			 enum mlxsw_sp_policer_type type,
			 const struct mlxsw_sp_policer_params *params,
			 struct netlink_ext_ack *extack, u16 *p_policer_index)
{
	struct mlxsw_sp_policer_family *family;
	struct mlxsw_sp_policer *policer;
	int err;

	family = mlxsw_sp->policer_core->family_arr[type];

	err = family->ops->policer_params_check(family, params, extack);
	if (err)
		return err;

	policer = kmalloc(sizeof(*policer), GFP_KERNEL);
	if (!policer)
		return -ENOMEM;
	policer->params = *params;

	err = family->ops->policer_index_alloc(family, policer);
	if (err) {
		NL_SET_ERR_MSG_MOD(extack, "Failed to allocate policer index");
		goto err_policer_index_alloc;
	}

	err = family->ops->policer_init(family, policer);
	if (err) {
		NL_SET_ERR_MSG_MOD(extack, "Failed to initialize policer");
		goto err_policer_init;
	}

	*p_policer_index = policer->index;

	return 0;

err_policer_init:
	family->ops->policer_index_free(family, policer->index);
err_policer_index_alloc:
	kfree(policer);
	return err;
}

void mlxsw_sp_policer_del(struct mlxsw_sp *mlxsw_sp,
			  enum mlxsw_sp_policer_type type, u16 policer_index)
{
	struct mlxsw_sp_policer_family *family;
	struct mlxsw_sp_policer *policer;

	family = mlxsw_sp->policer_core->family_arr[type];
	policer = family->ops->policer_index_free(family, policer_index);
	kfree(policer);
}

int mlxsw_sp_policer_drops_counter_get(struct mlxsw_sp *mlxsw_sp,
				       enum mlxsw_sp_policer_type type,
				       u16 policer_index, u64 *p_drops)
{
	struct mlxsw_sp_policer_family *family;
	char qpcr_pl[MLXSW_REG_QPCR_LEN];
	int err;

	family = mlxsw_sp->policer_core->family_arr[type];

	MLXSW_REG_ZERO(qpcr, qpcr_pl);
	mlxsw_reg_qpcr_pid_set(qpcr_pl, policer_index);
	mlxsw_reg_qpcr_g_set(qpcr_pl, family->qpcr_type);
	err = mlxsw_reg_query(mlxsw_sp->core, MLXSW_REG(qpcr), qpcr_pl);
	if (err)
		return err;

	*p_drops = mlxsw_reg_qpcr_violate_count_get(qpcr_pl);

	return 0;
}

static int
mlxsw_sp_policer_family_register(struct mlxsw_sp *mlxsw_sp,
				 const struct mlxsw_sp_policer_family *tmpl)
{
	struct mlxsw_sp_policer_family *family;
	int err;

	family = kmemdup(tmpl, sizeof(*family), GFP_KERNEL);
	if (!family)
		return -ENOMEM;

	family->mlxsw_sp = mlxsw_sp;
	idr_init(&family->policer_idr);
	mutex_init(&family->lock);

	err = family->ops->init(family);
	if (err)
		goto err_family_init;

	if (WARN_ON(family->start_index >= family->end_index)) {
		err = -EINVAL;
		goto err_index_check;
	}

	mlxsw_sp->policer_core->family_arr[tmpl->type] = family;

	return 0;

err_index_check:
	family->ops->fini(family);
err_family_init:
	mutex_destroy(&family->lock);
	idr_destroy(&family->policer_idr);
	kfree(family);
	return err;
}

static void
mlxsw_sp_policer_family_unregister(struct mlxsw_sp *mlxsw_sp,
				   struct mlxsw_sp_policer_family *family)
{
	family->ops->fini(family);
	mutex_destroy(&family->lock);
	WARN_ON(!idr_is_empty(&family->policer_idr));
	idr_destroy(&family->policer_idr);
	kfree(family);
}

int mlxsw_sp_policers_init(struct mlxsw_sp *mlxsw_sp)
{
	struct mlxsw_sp_policer_core *policer_core;
	int i, err;

	policer_core = kzalloc(sizeof(*policer_core), GFP_KERNEL);
	if (!policer_core)
		return -ENOMEM;
	mlxsw_sp->policer_core = policer_core;
	policer_core->ops = mlxsw_sp->policer_core_ops;

	err = policer_core->ops->init(policer_core);
	if (err)
		goto err_init;

	for (i = 0; i < MLXSW_SP_POLICER_TYPE_MAX + 1; i++) {
		err = mlxsw_sp_policer_family_register(mlxsw_sp, mlxsw_sp_policer_family_arr[i]);
		if (err)
			goto err_family_register;
	}

	return 0;

err_family_register:
	for (i--; i >= 0; i--) {
		struct mlxsw_sp_policer_family *family;

		family = mlxsw_sp->policer_core->family_arr[i];
		mlxsw_sp_policer_family_unregister(mlxsw_sp, family);
	}
err_init:
	kfree(mlxsw_sp->policer_core);
	return err;
}

void mlxsw_sp_policers_fini(struct mlxsw_sp *mlxsw_sp)
{
	int i;

	for (i = MLXSW_SP_POLICER_TYPE_MAX; i >= 0; i--) {
		struct mlxsw_sp_policer_family *family;

		family = mlxsw_sp->policer_core->family_arr[i];
		mlxsw_sp_policer_family_unregister(mlxsw_sp, family);
	}

	kfree(mlxsw_sp->policer_core);
}

int mlxsw_sp_policer_resources_register(struct mlxsw_core *mlxsw_core)
{
	u64 global_policers, cpu_policers, single_rate_policers;
	struct devlink *devlink = priv_to_devlink(mlxsw_core);
	struct devlink_resource_size_params size_params;
	int err;

	if (!MLXSW_CORE_RES_VALID(mlxsw_core, MAX_GLOBAL_POLICERS) ||
	    !MLXSW_CORE_RES_VALID(mlxsw_core, MAX_CPU_POLICERS))
		return -EIO;

	global_policers = MLXSW_CORE_RES_GET(mlxsw_core, MAX_GLOBAL_POLICERS);
	cpu_policers = MLXSW_CORE_RES_GET(mlxsw_core, MAX_CPU_POLICERS);
	single_rate_policers = global_policers - cpu_policers;

	devlink_resource_size_params_init(&size_params, global_policers,
					  global_policers, 1,
					  DEVLINK_RESOURCE_UNIT_ENTRY);
	err = devlink_resource_register(devlink, "global_policers",
					global_policers,
					MLXSW_SP_RESOURCE_GLOBAL_POLICERS,
					DEVLINK_RESOURCE_ID_PARENT_TOP,
					&size_params);
	if (err)
		return err;

	devlink_resource_size_params_init(&size_params, single_rate_policers,
					  single_rate_policers, 1,
					  DEVLINK_RESOURCE_UNIT_ENTRY);
	err = devlink_resource_register(devlink, "single_rate_policers",
					single_rate_policers,
					MLXSW_SP_RESOURCE_SINGLE_RATE_POLICERS,
					MLXSW_SP_RESOURCE_GLOBAL_POLICERS,
					&size_params);
	if (err)
		return err;

	return 0;
}

static int
mlxsw_sp1_policer_core_init(struct mlxsw_sp_policer_core *policer_core)
{
	policer_core->lowest_bs_bits = MLXSW_REG_QPCR_LOWEST_CBS_BITS_SP1;
	policer_core->highest_bs_bits = MLXSW_REG_QPCR_HIGHEST_CBS_BITS_SP1;

	return 0;
}

const struct mlxsw_sp_policer_core_ops mlxsw_sp1_policer_core_ops = {
	.init = mlxsw_sp1_policer_core_init,
};

static int
mlxsw_sp2_policer_core_init(struct mlxsw_sp_policer_core *policer_core)
{
	policer_core->lowest_bs_bits = MLXSW_REG_QPCR_LOWEST_CBS_BITS_SP2;
	policer_core->highest_bs_bits = MLXSW_REG_QPCR_HIGHEST_CBS_BITS_SP2;

	return 0;
}

const struct mlxsw_sp_policer_core_ops mlxsw_sp2_policer_core_ops = {
	.init = mlxsw_sp2_policer_core_init,
};
