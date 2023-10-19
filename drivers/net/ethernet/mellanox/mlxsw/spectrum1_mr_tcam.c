// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/* Copyright (c) 2017-2018 Mellanox Technologies. All rights reserved */

#include <linux/kernel.h>
#include <linux/parman.h>

#include "reg.h"
#include "spectrum.h"
#include "core_acl_flex_actions.h"
#include "spectrum_mr.h"

struct mlxsw_sp1_mr_tcam_region {
	struct mlxsw_sp *mlxsw_sp;
	enum mlxsw_reg_rtar_key_type rtar_key_type;
	struct parman *parman;
	struct parman_prio *parman_prios;
};

struct mlxsw_sp1_mr_tcam {
	struct mlxsw_sp1_mr_tcam_region tcam_regions[MLXSW_SP_L3_PROTO_MAX];
};

struct mlxsw_sp1_mr_tcam_route {
	struct parman_item parman_item;
	struct parman_prio *parman_prio;
};

static int mlxsw_sp1_mr_tcam_route_replace(struct mlxsw_sp *mlxsw_sp,
					   struct parman_item *parman_item,
					   struct mlxsw_sp_mr_route_key *key,
					   struct mlxsw_afa_block *afa_block)
{
	char rmft2_pl[MLXSW_REG_RMFT2_LEN];

	switch (key->proto) {
	case MLXSW_SP_L3_PROTO_IPV4:
		mlxsw_reg_rmft2_ipv4_pack(rmft2_pl, true, parman_item->index,
					  key->vrid,
					  MLXSW_REG_RMFT2_IRIF_MASK_IGNORE, 0,
					  ntohl(key->group.addr4),
					  ntohl(key->group_mask.addr4),
					  ntohl(key->source.addr4),
					  ntohl(key->source_mask.addr4),
					  mlxsw_afa_block_first_set(afa_block));
		break;
	case MLXSW_SP_L3_PROTO_IPV6:
		mlxsw_reg_rmft2_ipv6_pack(rmft2_pl, true, parman_item->index,
					  key->vrid,
					  MLXSW_REG_RMFT2_IRIF_MASK_IGNORE, 0,
					  key->group.addr6,
					  key->group_mask.addr6,
					  key->source.addr6,
					  key->source_mask.addr6,
					  mlxsw_afa_block_first_set(afa_block));
	}

	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(rmft2), rmft2_pl);
}

static int mlxsw_sp1_mr_tcam_route_remove(struct mlxsw_sp *mlxsw_sp,
					  struct parman_item *parman_item,
					  struct mlxsw_sp_mr_route_key *key)
{
	struct in6_addr zero_addr = IN6ADDR_ANY_INIT;
	char rmft2_pl[MLXSW_REG_RMFT2_LEN];

	switch (key->proto) {
	case MLXSW_SP_L3_PROTO_IPV4:
		mlxsw_reg_rmft2_ipv4_pack(rmft2_pl, false, parman_item->index,
					  key->vrid, 0, 0, 0, 0, 0, 0, NULL);
		break;
	case MLXSW_SP_L3_PROTO_IPV6:
		mlxsw_reg_rmft2_ipv6_pack(rmft2_pl, false, parman_item->index,
					  key->vrid, 0, 0, zero_addr, zero_addr,
					  zero_addr, zero_addr, NULL);
		break;
	}

	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(rmft2), rmft2_pl);
}

static struct mlxsw_sp1_mr_tcam_region *
mlxsw_sp1_mr_tcam_protocol_region(struct mlxsw_sp1_mr_tcam *mr_tcam,
				  enum mlxsw_sp_l3proto proto)
{
	return &mr_tcam->tcam_regions[proto];
}

static int
mlxsw_sp1_mr_tcam_route_parman_item_add(struct mlxsw_sp1_mr_tcam *mr_tcam,
					struct mlxsw_sp1_mr_tcam_route *route,
					struct mlxsw_sp_mr_route_key *key,
					enum mlxsw_sp_mr_route_prio prio)
{
	struct mlxsw_sp1_mr_tcam_region *tcam_region;
	int err;

	tcam_region = mlxsw_sp1_mr_tcam_protocol_region(mr_tcam, key->proto);
	err = parman_item_add(tcam_region->parman,
			      &tcam_region->parman_prios[prio],
			      &route->parman_item);
	if (err)
		return err;

	route->parman_prio = &tcam_region->parman_prios[prio];
	return 0;
}

static void
mlxsw_sp1_mr_tcam_route_parman_item_remove(struct mlxsw_sp1_mr_tcam *mr_tcam,
					   struct mlxsw_sp1_mr_tcam_route *route,
					   struct mlxsw_sp_mr_route_key *key)
{
	struct mlxsw_sp1_mr_tcam_region *tcam_region;

	tcam_region = mlxsw_sp1_mr_tcam_protocol_region(mr_tcam, key->proto);
	parman_item_remove(tcam_region->parman,
			   route->parman_prio, &route->parman_item);
}

static int
mlxsw_sp1_mr_tcam_route_create(struct mlxsw_sp *mlxsw_sp, void *priv,
			       void *route_priv,
			       struct mlxsw_sp_mr_route_key *key,
			       struct mlxsw_afa_block *afa_block,
			       enum mlxsw_sp_mr_route_prio prio)
{
	struct mlxsw_sp1_mr_tcam_route *route = route_priv;
	struct mlxsw_sp1_mr_tcam *mr_tcam = priv;
	int err;

	err = mlxsw_sp1_mr_tcam_route_parman_item_add(mr_tcam, route,
						      key, prio);
	if (err)
		return err;

	err = mlxsw_sp1_mr_tcam_route_replace(mlxsw_sp, &route->parman_item,
					      key, afa_block);
	if (err)
		goto err_route_replace;
	return 0;

err_route_replace:
	mlxsw_sp1_mr_tcam_route_parman_item_remove(mr_tcam, route, key);
	return err;
}

static void
mlxsw_sp1_mr_tcam_route_destroy(struct mlxsw_sp *mlxsw_sp, void *priv,
				void *route_priv,
				struct mlxsw_sp_mr_route_key *key)
{
	struct mlxsw_sp1_mr_tcam_route *route = route_priv;
	struct mlxsw_sp1_mr_tcam *mr_tcam = priv;

	mlxsw_sp1_mr_tcam_route_remove(mlxsw_sp, &route->parman_item, key);
	mlxsw_sp1_mr_tcam_route_parman_item_remove(mr_tcam, route, key);
}

static int
mlxsw_sp1_mr_tcam_route_update(struct mlxsw_sp *mlxsw_sp,
			       void *route_priv,
			       struct mlxsw_sp_mr_route_key *key,
			       struct mlxsw_afa_block *afa_block)
{
	struct mlxsw_sp1_mr_tcam_route *route = route_priv;

	return mlxsw_sp1_mr_tcam_route_replace(mlxsw_sp, &route->parman_item,
					       key, afa_block);
}

#define MLXSW_SP1_MR_TCAM_REGION_BASE_COUNT 16
#define MLXSW_SP1_MR_TCAM_REGION_RESIZE_STEP 16

static int
mlxsw_sp1_mr_tcam_region_alloc(struct mlxsw_sp1_mr_tcam_region *mr_tcam_region)
{
	struct mlxsw_sp *mlxsw_sp = mr_tcam_region->mlxsw_sp;
	char rtar_pl[MLXSW_REG_RTAR_LEN];

	mlxsw_reg_rtar_pack(rtar_pl, MLXSW_REG_RTAR_OP_ALLOCATE,
			    mr_tcam_region->rtar_key_type,
			    MLXSW_SP1_MR_TCAM_REGION_BASE_COUNT);
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(rtar), rtar_pl);
}

static void
mlxsw_sp1_mr_tcam_region_free(struct mlxsw_sp1_mr_tcam_region *mr_tcam_region)
{
	struct mlxsw_sp *mlxsw_sp = mr_tcam_region->mlxsw_sp;
	char rtar_pl[MLXSW_REG_RTAR_LEN];

	mlxsw_reg_rtar_pack(rtar_pl, MLXSW_REG_RTAR_OP_DEALLOCATE,
			    mr_tcam_region->rtar_key_type, 0);
	mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(rtar), rtar_pl);
}

static int mlxsw_sp1_mr_tcam_region_parman_resize(void *priv,
						  unsigned long new_count)
{
	struct mlxsw_sp1_mr_tcam_region *mr_tcam_region = priv;
	struct mlxsw_sp *mlxsw_sp = mr_tcam_region->mlxsw_sp;
	char rtar_pl[MLXSW_REG_RTAR_LEN];
	u64 max_tcam_rules;

	max_tcam_rules = MLXSW_CORE_RES_GET(mlxsw_sp->core, ACL_MAX_TCAM_RULES);
	if (new_count > max_tcam_rules)
		return -EINVAL;
	mlxsw_reg_rtar_pack(rtar_pl, MLXSW_REG_RTAR_OP_RESIZE,
			    mr_tcam_region->rtar_key_type, new_count);
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(rtar), rtar_pl);
}

static void mlxsw_sp1_mr_tcam_region_parman_move(void *priv,
						 unsigned long from_index,
						 unsigned long to_index,
						 unsigned long count)
{
	struct mlxsw_sp1_mr_tcam_region *mr_tcam_region = priv;
	struct mlxsw_sp *mlxsw_sp = mr_tcam_region->mlxsw_sp;
	char rrcr_pl[MLXSW_REG_RRCR_LEN];

	mlxsw_reg_rrcr_pack(rrcr_pl, MLXSW_REG_RRCR_OP_MOVE,
			    from_index, count,
			    mr_tcam_region->rtar_key_type, to_index);
	mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(rrcr), rrcr_pl);
}

static const struct parman_ops mlxsw_sp1_mr_tcam_region_parman_ops = {
	.base_count	= MLXSW_SP1_MR_TCAM_REGION_BASE_COUNT,
	.resize_step	= MLXSW_SP1_MR_TCAM_REGION_RESIZE_STEP,
	.resize		= mlxsw_sp1_mr_tcam_region_parman_resize,
	.move		= mlxsw_sp1_mr_tcam_region_parman_move,
	.algo		= PARMAN_ALGO_TYPE_LSORT,
};

static int
mlxsw_sp1_mr_tcam_region_init(struct mlxsw_sp *mlxsw_sp,
			      struct mlxsw_sp1_mr_tcam_region *mr_tcam_region,
			      enum mlxsw_reg_rtar_key_type rtar_key_type)
{
	struct parman_prio *parman_prios;
	struct parman *parman;
	int err;
	int i;

	mr_tcam_region->rtar_key_type = rtar_key_type;
	mr_tcam_region->mlxsw_sp = mlxsw_sp;

	err = mlxsw_sp1_mr_tcam_region_alloc(mr_tcam_region);
	if (err)
		return err;

	parman = parman_create(&mlxsw_sp1_mr_tcam_region_parman_ops,
			       mr_tcam_region);
	if (!parman) {
		err = -ENOMEM;
		goto err_parman_create;
	}
	mr_tcam_region->parman = parman;

	parman_prios = kmalloc_array(MLXSW_SP_MR_ROUTE_PRIO_MAX + 1,
				     sizeof(*parman_prios), GFP_KERNEL);
	if (!parman_prios) {
		err = -ENOMEM;
		goto err_parman_prios_alloc;
	}
	mr_tcam_region->parman_prios = parman_prios;

	for (i = 0; i < MLXSW_SP_MR_ROUTE_PRIO_MAX + 1; i++)
		parman_prio_init(mr_tcam_region->parman,
				 &mr_tcam_region->parman_prios[i], i);
	return 0;

err_parman_prios_alloc:
	parman_destroy(parman);
err_parman_create:
	mlxsw_sp1_mr_tcam_region_free(mr_tcam_region);
	return err;
}

static void
mlxsw_sp1_mr_tcam_region_fini(struct mlxsw_sp1_mr_tcam_region *mr_tcam_region)
{
	int i;

	for (i = 0; i < MLXSW_SP_MR_ROUTE_PRIO_MAX + 1; i++)
		parman_prio_fini(&mr_tcam_region->parman_prios[i]);
	kfree(mr_tcam_region->parman_prios);
	parman_destroy(mr_tcam_region->parman);
	mlxsw_sp1_mr_tcam_region_free(mr_tcam_region);
}

static int mlxsw_sp1_mr_tcam_init(struct mlxsw_sp *mlxsw_sp, void *priv)
{
	struct mlxsw_sp1_mr_tcam *mr_tcam = priv;
	struct mlxsw_sp1_mr_tcam_region *region = &mr_tcam->tcam_regions[0];
	u32 rtar_key;
	int err;

	if (!MLXSW_CORE_RES_VALID(mlxsw_sp->core, ACL_MAX_TCAM_RULES))
		return -EIO;

	rtar_key = MLXSW_REG_RTAR_KEY_TYPE_IPV4_MULTICAST;
	err = mlxsw_sp1_mr_tcam_region_init(mlxsw_sp,
					    &region[MLXSW_SP_L3_PROTO_IPV4],
					    rtar_key);
	if (err)
		return err;

	rtar_key = MLXSW_REG_RTAR_KEY_TYPE_IPV6_MULTICAST;
	err = mlxsw_sp1_mr_tcam_region_init(mlxsw_sp,
					    &region[MLXSW_SP_L3_PROTO_IPV6],
					    rtar_key);
	if (err)
		goto err_ipv6_region_init;

	return 0;

err_ipv6_region_init:
	mlxsw_sp1_mr_tcam_region_fini(&region[MLXSW_SP_L3_PROTO_IPV4]);
	return err;
}

static void mlxsw_sp1_mr_tcam_fini(void *priv)
{
	struct mlxsw_sp1_mr_tcam *mr_tcam = priv;
	struct mlxsw_sp1_mr_tcam_region *region = &mr_tcam->tcam_regions[0];

	mlxsw_sp1_mr_tcam_region_fini(&region[MLXSW_SP_L3_PROTO_IPV6]);
	mlxsw_sp1_mr_tcam_region_fini(&region[MLXSW_SP_L3_PROTO_IPV4]);
}

const struct mlxsw_sp_mr_tcam_ops mlxsw_sp1_mr_tcam_ops = {
	.priv_size = sizeof(struct mlxsw_sp1_mr_tcam),
	.init = mlxsw_sp1_mr_tcam_init,
	.fini = mlxsw_sp1_mr_tcam_fini,
	.route_priv_size = sizeof(struct mlxsw_sp1_mr_tcam_route),
	.route_create = mlxsw_sp1_mr_tcam_route_create,
	.route_destroy = mlxsw_sp1_mr_tcam_route_destroy,
	.route_update = mlxsw_sp1_mr_tcam_route_update,
};
