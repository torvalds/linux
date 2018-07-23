/*
 * drivers/net/ethernet/mellanox/mlxsw/spectrum_acl_tcam.c
 * Copyright (c) 2017-2018 Mellanox Technologies. All rights reserved.
 * Copyright (c) 2017-2018 Jiri Pirko <jiri@mellanox.com>
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
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/bitops.h>
#include <linux/list.h>
#include <linux/rhashtable.h>
#include <linux/netdevice.h>

#include "reg.h"
#include "core.h"
#include "resources.h"
#include "spectrum.h"
#include "spectrum_acl_tcam.h"
#include "core_acl_flex_keys.h"

size_t mlxsw_sp_acl_tcam_priv_size(struct mlxsw_sp *mlxsw_sp)
{
	const struct mlxsw_sp_acl_tcam_ops *ops = mlxsw_sp->acl_tcam_ops;

	return ops->priv_size;
}

int mlxsw_sp_acl_tcam_init(struct mlxsw_sp *mlxsw_sp,
			   struct mlxsw_sp_acl_tcam *tcam)
{
	const struct mlxsw_sp_acl_tcam_ops *ops = mlxsw_sp->acl_tcam_ops;
	u64 max_tcam_regions;
	u64 max_regions;
	u64 max_groups;
	size_t alloc_size;
	int err;

	max_tcam_regions = MLXSW_CORE_RES_GET(mlxsw_sp->core,
					      ACL_MAX_TCAM_REGIONS);
	max_regions = MLXSW_CORE_RES_GET(mlxsw_sp->core, ACL_MAX_REGIONS);

	/* Use 1:1 mapping between ACL region and TCAM region */
	if (max_tcam_regions < max_regions)
		max_regions = max_tcam_regions;

	alloc_size = sizeof(tcam->used_regions[0]) * BITS_TO_LONGS(max_regions);
	tcam->used_regions = kzalloc(alloc_size, GFP_KERNEL);
	if (!tcam->used_regions)
		return -ENOMEM;
	tcam->max_regions = max_regions;

	max_groups = MLXSW_CORE_RES_GET(mlxsw_sp->core, ACL_MAX_GROUPS);
	alloc_size = sizeof(tcam->used_groups[0]) * BITS_TO_LONGS(max_groups);
	tcam->used_groups = kzalloc(alloc_size, GFP_KERNEL);
	if (!tcam->used_groups) {
		err = -ENOMEM;
		goto err_alloc_used_groups;
	}
	tcam->max_groups = max_groups;
	tcam->max_group_size = MLXSW_CORE_RES_GET(mlxsw_sp->core,
						 ACL_MAX_GROUP_SIZE);

	err = ops->init(mlxsw_sp, tcam->priv, tcam);
	if (err)
		goto err_tcam_init;

	return 0;

err_tcam_init:
	kfree(tcam->used_groups);
err_alloc_used_groups:
	kfree(tcam->used_regions);
	return err;
}

void mlxsw_sp_acl_tcam_fini(struct mlxsw_sp *mlxsw_sp,
			    struct mlxsw_sp_acl_tcam *tcam)
{
	const struct mlxsw_sp_acl_tcam_ops *ops = mlxsw_sp->acl_tcam_ops;

	ops->fini(mlxsw_sp, tcam->priv);
	kfree(tcam->used_groups);
	kfree(tcam->used_regions);
}

int mlxsw_sp_acl_tcam_priority_get(struct mlxsw_sp *mlxsw_sp,
				   struct mlxsw_sp_acl_rule_info *rulei,
				   u32 *priority, bool fillup_priority)
{
	u64 max_priority;

	if (!fillup_priority) {
		*priority = 0;
		return 0;
	}

	if (!MLXSW_CORE_RES_VALID(mlxsw_sp->core, KVD_SIZE))
		return -EIO;

	max_priority = MLXSW_CORE_RES_GET(mlxsw_sp->core, KVD_SIZE);
	if (rulei->priority > max_priority)
		return -EINVAL;

	/* Unlike in TC, in HW, higher number means higher priority. */
	*priority = max_priority - rulei->priority;
	return 0;
}

static int mlxsw_sp_acl_tcam_region_id_get(struct mlxsw_sp_acl_tcam *tcam,
					   u16 *p_id)
{
	u16 id;

	id = find_first_zero_bit(tcam->used_regions, tcam->max_regions);
	if (id < tcam->max_regions) {
		__set_bit(id, tcam->used_regions);
		*p_id = id;
		return 0;
	}
	return -ENOBUFS;
}

static void mlxsw_sp_acl_tcam_region_id_put(struct mlxsw_sp_acl_tcam *tcam,
					    u16 id)
{
	__clear_bit(id, tcam->used_regions);
}

static int mlxsw_sp_acl_tcam_group_id_get(struct mlxsw_sp_acl_tcam *tcam,
					  u16 *p_id)
{
	u16 id;

	id = find_first_zero_bit(tcam->used_groups, tcam->max_groups);
	if (id < tcam->max_groups) {
		__set_bit(id, tcam->used_groups);
		*p_id = id;
		return 0;
	}
	return -ENOBUFS;
}

static void mlxsw_sp_acl_tcam_group_id_put(struct mlxsw_sp_acl_tcam *tcam,
					   u16 id)
{
	__clear_bit(id, tcam->used_groups);
}

struct mlxsw_sp_acl_tcam_pattern {
	const enum mlxsw_afk_element *elements;
	unsigned int elements_count;
};

struct mlxsw_sp_acl_tcam_group {
	struct mlxsw_sp_acl_tcam *tcam;
	u16 id;
	struct list_head region_list;
	unsigned int region_count;
	struct rhashtable chunk_ht;
	struct mlxsw_sp_acl_tcam_group_ops *ops;
	const struct mlxsw_sp_acl_tcam_pattern *patterns;
	unsigned int patterns_count;
	bool tmplt_elusage_set;
	struct mlxsw_afk_element_usage tmplt_elusage;
};

struct mlxsw_sp_acl_tcam_chunk {
	struct list_head list; /* Member of a TCAM region */
	struct rhash_head ht_node; /* Member of a chunk HT */
	unsigned int priority; /* Priority within the region and group */
	struct mlxsw_sp_acl_tcam_group *group;
	struct mlxsw_sp_acl_tcam_region *region;
	unsigned int ref_count;
	unsigned long priv[0];
	/* priv has to be always the last item */
};

struct mlxsw_sp_acl_tcam_entry {
	struct mlxsw_sp_acl_tcam_chunk *chunk;
	unsigned long priv[0];
	/* priv has to be always the last item */
};

static const struct rhashtable_params mlxsw_sp_acl_tcam_chunk_ht_params = {
	.key_len = sizeof(unsigned int),
	.key_offset = offsetof(struct mlxsw_sp_acl_tcam_chunk, priority),
	.head_offset = offsetof(struct mlxsw_sp_acl_tcam_chunk, ht_node),
	.automatic_shrinking = true,
};

static int mlxsw_sp_acl_tcam_group_update(struct mlxsw_sp *mlxsw_sp,
					  struct mlxsw_sp_acl_tcam_group *group)
{
	struct mlxsw_sp_acl_tcam_region *region;
	char pagt_pl[MLXSW_REG_PAGT_LEN];
	int acl_index = 0;

	mlxsw_reg_pagt_pack(pagt_pl, group->id);
	list_for_each_entry(region, &group->region_list, list)
		mlxsw_reg_pagt_acl_id_pack(pagt_pl, acl_index++, region->id);
	mlxsw_reg_pagt_size_set(pagt_pl, acl_index);
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(pagt), pagt_pl);
}

static int
mlxsw_sp_acl_tcam_group_add(struct mlxsw_sp *mlxsw_sp,
			    struct mlxsw_sp_acl_tcam *tcam,
			    struct mlxsw_sp_acl_tcam_group *group,
			    const struct mlxsw_sp_acl_tcam_pattern *patterns,
			    unsigned int patterns_count,
			    struct mlxsw_afk_element_usage *tmplt_elusage)
{
	int err;

	group->tcam = tcam;
	group->patterns = patterns;
	group->patterns_count = patterns_count;
	if (tmplt_elusage) {
		group->tmplt_elusage_set = true;
		memcpy(&group->tmplt_elusage, tmplt_elusage,
		       sizeof(group->tmplt_elusage));
	}
	INIT_LIST_HEAD(&group->region_list);
	err = mlxsw_sp_acl_tcam_group_id_get(tcam, &group->id);
	if (err)
		return err;

	err = rhashtable_init(&group->chunk_ht,
			      &mlxsw_sp_acl_tcam_chunk_ht_params);
	if (err)
		goto err_rhashtable_init;

	return 0;

err_rhashtable_init:
	mlxsw_sp_acl_tcam_group_id_put(tcam, group->id);
	return err;
}

static void mlxsw_sp_acl_tcam_group_del(struct mlxsw_sp *mlxsw_sp,
					struct mlxsw_sp_acl_tcam_group *group)
{
	struct mlxsw_sp_acl_tcam *tcam = group->tcam;

	rhashtable_destroy(&group->chunk_ht);
	mlxsw_sp_acl_tcam_group_id_put(tcam, group->id);
	WARN_ON(!list_empty(&group->region_list));
}

static int
mlxsw_sp_acl_tcam_group_bind(struct mlxsw_sp *mlxsw_sp,
			     struct mlxsw_sp_acl_tcam_group *group,
			     struct mlxsw_sp_port *mlxsw_sp_port,
			     bool ingress)
{
	char ppbt_pl[MLXSW_REG_PPBT_LEN];

	mlxsw_reg_ppbt_pack(ppbt_pl, ingress ? MLXSW_REG_PXBT_E_IACL :
					       MLXSW_REG_PXBT_E_EACL,
			    MLXSW_REG_PXBT_OP_BIND, mlxsw_sp_port->local_port,
			    group->id);
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(ppbt), ppbt_pl);
}

static void
mlxsw_sp_acl_tcam_group_unbind(struct mlxsw_sp *mlxsw_sp,
			       struct mlxsw_sp_acl_tcam_group *group,
			       struct mlxsw_sp_port *mlxsw_sp_port,
			       bool ingress)
{
	char ppbt_pl[MLXSW_REG_PPBT_LEN];

	mlxsw_reg_ppbt_pack(ppbt_pl, ingress ? MLXSW_REG_PXBT_E_IACL :
					       MLXSW_REG_PXBT_E_EACL,
			    MLXSW_REG_PXBT_OP_UNBIND, mlxsw_sp_port->local_port,
			    group->id);
	mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(ppbt), ppbt_pl);
}

static u16
mlxsw_sp_acl_tcam_group_id(struct mlxsw_sp_acl_tcam_group *group)
{
	return group->id;
}

static unsigned int
mlxsw_sp_acl_tcam_region_prio(struct mlxsw_sp_acl_tcam_region *region)
{
	struct mlxsw_sp_acl_tcam_chunk *chunk;

	if (list_empty(&region->chunk_list))
		return 0;
	/* As a priority of a region, return priority of the first chunk */
	chunk = list_first_entry(&region->chunk_list, typeof(*chunk), list);
	return chunk->priority;
}

static unsigned int
mlxsw_sp_acl_tcam_region_max_prio(struct mlxsw_sp_acl_tcam_region *region)
{
	struct mlxsw_sp_acl_tcam_chunk *chunk;

	if (list_empty(&region->chunk_list))
		return 0;
	chunk = list_last_entry(&region->chunk_list, typeof(*chunk), list);
	return chunk->priority;
}

static void
mlxsw_sp_acl_tcam_group_list_add(struct mlxsw_sp_acl_tcam_group *group,
				 struct mlxsw_sp_acl_tcam_region *region)
{
	struct mlxsw_sp_acl_tcam_region *region2;
	struct list_head *pos;

	/* Position the region inside the list according to priority */
	list_for_each(pos, &group->region_list) {
		region2 = list_entry(pos, typeof(*region2), list);
		if (mlxsw_sp_acl_tcam_region_prio(region2) >
		    mlxsw_sp_acl_tcam_region_prio(region))
			break;
	}
	list_add_tail(&region->list, pos);
	group->region_count++;
}

static void
mlxsw_sp_acl_tcam_group_list_del(struct mlxsw_sp_acl_tcam_group *group,
				 struct mlxsw_sp_acl_tcam_region *region)
{
	group->region_count--;
	list_del(&region->list);
}

static int
mlxsw_sp_acl_tcam_group_region_attach(struct mlxsw_sp *mlxsw_sp,
				      struct mlxsw_sp_acl_tcam_group *group,
				      struct mlxsw_sp_acl_tcam_region *region)
{
	int err;

	if (group->region_count == group->tcam->max_group_size)
		return -ENOBUFS;

	mlxsw_sp_acl_tcam_group_list_add(group, region);

	err = mlxsw_sp_acl_tcam_group_update(mlxsw_sp, group);
	if (err)
		goto err_group_update;
	region->group = group;

	return 0;

err_group_update:
	mlxsw_sp_acl_tcam_group_list_del(group, region);
	mlxsw_sp_acl_tcam_group_update(mlxsw_sp, group);
	return err;
}

static void
mlxsw_sp_acl_tcam_group_region_detach(struct mlxsw_sp *mlxsw_sp,
				      struct mlxsw_sp_acl_tcam_region *region)
{
	struct mlxsw_sp_acl_tcam_group *group = region->group;

	mlxsw_sp_acl_tcam_group_list_del(group, region);
	mlxsw_sp_acl_tcam_group_update(mlxsw_sp, group);
}

static struct mlxsw_sp_acl_tcam_region *
mlxsw_sp_acl_tcam_group_region_find(struct mlxsw_sp_acl_tcam_group *group,
				    unsigned int priority,
				    struct mlxsw_afk_element_usage *elusage,
				    bool *p_need_split)
{
	struct mlxsw_sp_acl_tcam_region *region, *region2;
	struct list_head *pos;
	bool issubset;

	list_for_each(pos, &group->region_list) {
		region = list_entry(pos, typeof(*region), list);

		/* First, check if the requested priority does not rather belong
		 * under some of the next regions.
		 */
		if (pos->next != &group->region_list) { /* not last */
			region2 = list_entry(pos->next, typeof(*region2), list);
			if (priority >= mlxsw_sp_acl_tcam_region_prio(region2))
				continue;
		}

		issubset = mlxsw_afk_key_info_subset(region->key_info, elusage);

		/* If requested element usage would not fit and the priority
		 * is lower than the currently inspected region we cannot
		 * use this region, so return NULL to indicate new region has
		 * to be created.
		 */
		if (!issubset &&
		    priority < mlxsw_sp_acl_tcam_region_prio(region))
			return NULL;

		/* If requested element usage would not fit and the priority
		 * is higher than the currently inspected region we cannot
		 * use this region. There is still some hope that the next
		 * region would be the fit. So let it be processed and
		 * eventually break at the check right above this.
		 */
		if (!issubset &&
		    priority > mlxsw_sp_acl_tcam_region_max_prio(region))
			continue;

		/* Indicate if the region needs to be split in order to add
		 * the requested priority. Split is needed when requested
		 * element usage won't fit into the found region.
		 */
		*p_need_split = !issubset;
		return region;
	}
	return NULL; /* New region has to be created. */
}

static void
mlxsw_sp_acl_tcam_group_use_patterns(struct mlxsw_sp_acl_tcam_group *group,
				     struct mlxsw_afk_element_usage *elusage,
				     struct mlxsw_afk_element_usage *out)
{
	const struct mlxsw_sp_acl_tcam_pattern *pattern;
	int i;

	/* In case the template is set, we don't have to look up the pattern
	 * and just use the template.
	 */
	if (group->tmplt_elusage_set) {
		memcpy(out, &group->tmplt_elusage, sizeof(*out));
		WARN_ON(!mlxsw_afk_element_usage_subset(elusage, out));
		return;
	}

	for (i = 0; i < group->patterns_count; i++) {
		pattern = &group->patterns[i];
		mlxsw_afk_element_usage_fill(out, pattern->elements,
					     pattern->elements_count);
		if (mlxsw_afk_element_usage_subset(elusage, out))
			return;
	}
	memcpy(out, elusage, sizeof(*out));
}

static int
mlxsw_sp_acl_tcam_region_alloc(struct mlxsw_sp *mlxsw_sp,
			       struct mlxsw_sp_acl_tcam_region *region)
{
	struct mlxsw_afk_key_info *key_info = region->key_info;
	char ptar_pl[MLXSW_REG_PTAR_LEN];
	unsigned int encodings_count;
	int i;
	int err;

	mlxsw_reg_ptar_pack(ptar_pl, MLXSW_REG_PTAR_OP_ALLOC,
			    region->key_type,
			    MLXSW_SP_ACL_TCAM_REGION_BASE_COUNT,
			    region->id, region->tcam_region_info);
	encodings_count = mlxsw_afk_key_info_blocks_count_get(key_info);
	for (i = 0; i < encodings_count; i++) {
		u16 encoding;

		encoding = mlxsw_afk_key_info_block_encoding_get(key_info, i);
		mlxsw_reg_ptar_key_id_pack(ptar_pl, i, encoding);
	}
	err = mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(ptar), ptar_pl);
	if (err)
		return err;
	mlxsw_reg_ptar_unpack(ptar_pl, region->tcam_region_info);
	return 0;
}

static void
mlxsw_sp_acl_tcam_region_free(struct mlxsw_sp *mlxsw_sp,
			      struct mlxsw_sp_acl_tcam_region *region)
{
	char ptar_pl[MLXSW_REG_PTAR_LEN];

	mlxsw_reg_ptar_pack(ptar_pl, MLXSW_REG_PTAR_OP_FREE,
			    region->key_type, 0, region->id,
			    region->tcam_region_info);
	mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(ptar), ptar_pl);
}

static int
mlxsw_sp_acl_tcam_region_enable(struct mlxsw_sp *mlxsw_sp,
				struct mlxsw_sp_acl_tcam_region *region)
{
	char pacl_pl[MLXSW_REG_PACL_LEN];

	mlxsw_reg_pacl_pack(pacl_pl, region->id, true,
			    region->tcam_region_info);
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(pacl), pacl_pl);
}

static void
mlxsw_sp_acl_tcam_region_disable(struct mlxsw_sp *mlxsw_sp,
				 struct mlxsw_sp_acl_tcam_region *region)
{
	char pacl_pl[MLXSW_REG_PACL_LEN];

	mlxsw_reg_pacl_pack(pacl_pl, region->id, false,
			    region->tcam_region_info);
	mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(pacl), pacl_pl);
}

static struct mlxsw_sp_acl_tcam_region *
mlxsw_sp_acl_tcam_region_create(struct mlxsw_sp *mlxsw_sp,
				struct mlxsw_sp_acl_tcam *tcam,
				struct mlxsw_afk_element_usage *elusage)
{
	const struct mlxsw_sp_acl_tcam_ops *ops = mlxsw_sp->acl_tcam_ops;
	struct mlxsw_afk *afk = mlxsw_sp_acl_afk(mlxsw_sp->acl);
	struct mlxsw_sp_acl_tcam_region *region;
	int err;

	region = kzalloc(sizeof(*region) + ops->region_priv_size, GFP_KERNEL);
	if (!region)
		return ERR_PTR(-ENOMEM);
	INIT_LIST_HEAD(&region->chunk_list);
	region->mlxsw_sp = mlxsw_sp;

	region->key_info = mlxsw_afk_key_info_get(afk, elusage);
	if (IS_ERR(region->key_info)) {
		err = PTR_ERR(region->key_info);
		goto err_key_info_get;
	}

	err = mlxsw_sp_acl_tcam_region_id_get(tcam, &region->id);
	if (err)
		goto err_region_id_get;

	err = ops->region_associate(mlxsw_sp, region);
	if (err)
		goto err_tcam_region_associate;

	region->key_type = ops->key_type;
	err = mlxsw_sp_acl_tcam_region_alloc(mlxsw_sp, region);
	if (err)
		goto err_tcam_region_alloc;

	err = mlxsw_sp_acl_tcam_region_enable(mlxsw_sp, region);
	if (err)
		goto err_tcam_region_enable;

	err = ops->region_init(mlxsw_sp, region->priv, region);
	if (err)
		goto err_tcam_region_init;

	return region;

err_tcam_region_init:
	mlxsw_sp_acl_tcam_region_disable(mlxsw_sp, region);
err_tcam_region_enable:
	mlxsw_sp_acl_tcam_region_free(mlxsw_sp, region);
err_tcam_region_alloc:
err_tcam_region_associate:
	mlxsw_sp_acl_tcam_region_id_put(tcam, region->id);
err_region_id_get:
	mlxsw_afk_key_info_put(region->key_info);
err_key_info_get:
	kfree(region);
	return ERR_PTR(err);
}

static void
mlxsw_sp_acl_tcam_region_destroy(struct mlxsw_sp *mlxsw_sp,
				 struct mlxsw_sp_acl_tcam_region *region)
{
	const struct mlxsw_sp_acl_tcam_ops *ops = mlxsw_sp->acl_tcam_ops;

	ops->region_fini(mlxsw_sp, region->priv);
	mlxsw_sp_acl_tcam_region_disable(mlxsw_sp, region);
	mlxsw_sp_acl_tcam_region_free(mlxsw_sp, region);
	mlxsw_sp_acl_tcam_region_id_put(region->group->tcam, region->id);
	mlxsw_afk_key_info_put(region->key_info);
	kfree(region);
}

static int
mlxsw_sp_acl_tcam_chunk_assoc(struct mlxsw_sp *mlxsw_sp,
			      struct mlxsw_sp_acl_tcam_group *group,
			      unsigned int priority,
			      struct mlxsw_afk_element_usage *elusage,
			      struct mlxsw_sp_acl_tcam_chunk *chunk)
{
	struct mlxsw_sp_acl_tcam_region *region;
	bool region_created = false;
	bool need_split;
	int err;

	region = mlxsw_sp_acl_tcam_group_region_find(group, priority, elusage,
						     &need_split);
	if (region && need_split) {
		/* According to priority, the chunk should belong to an
		 * existing region. However, this chunk needs elements
		 * that region does not contain. We need to split the existing
		 * region into two and create a new region for this chunk
		 * in between. This is not supported now.
		 */
		return -EOPNOTSUPP;
	}
	if (!region) {
		struct mlxsw_afk_element_usage region_elusage;

		mlxsw_sp_acl_tcam_group_use_patterns(group, elusage,
						     &region_elusage);
		region = mlxsw_sp_acl_tcam_region_create(mlxsw_sp, group->tcam,
							 &region_elusage);
		if (IS_ERR(region))
			return PTR_ERR(region);
		region_created = true;
	}

	chunk->region = region;
	list_add_tail(&chunk->list, &region->chunk_list);

	if (!region_created)
		return 0;

	err = mlxsw_sp_acl_tcam_group_region_attach(mlxsw_sp, group, region);
	if (err)
		goto err_group_region_attach;

	return 0;

err_group_region_attach:
	mlxsw_sp_acl_tcam_region_destroy(mlxsw_sp, region);
	return err;
}

static void
mlxsw_sp_acl_tcam_chunk_deassoc(struct mlxsw_sp *mlxsw_sp,
				struct mlxsw_sp_acl_tcam_chunk *chunk)
{
	struct mlxsw_sp_acl_tcam_region *region = chunk->region;

	list_del(&chunk->list);
	if (list_empty(&region->chunk_list)) {
		mlxsw_sp_acl_tcam_group_region_detach(mlxsw_sp, region);
		mlxsw_sp_acl_tcam_region_destroy(mlxsw_sp, region);
	}
}

static struct mlxsw_sp_acl_tcam_chunk *
mlxsw_sp_acl_tcam_chunk_create(struct mlxsw_sp *mlxsw_sp,
			       struct mlxsw_sp_acl_tcam_group *group,
			       unsigned int priority,
			       struct mlxsw_afk_element_usage *elusage)
{
	const struct mlxsw_sp_acl_tcam_ops *ops = mlxsw_sp->acl_tcam_ops;
	struct mlxsw_sp_acl_tcam_chunk *chunk;
	int err;

	if (priority == MLXSW_SP_ACL_TCAM_CATCHALL_PRIO)
		return ERR_PTR(-EINVAL);

	chunk = kzalloc(sizeof(*chunk) + ops->chunk_priv_size, GFP_KERNEL);
	if (!chunk)
		return ERR_PTR(-ENOMEM);
	chunk->priority = priority;
	chunk->group = group;
	chunk->ref_count = 1;

	err = mlxsw_sp_acl_tcam_chunk_assoc(mlxsw_sp, group, priority,
					    elusage, chunk);
	if (err)
		goto err_chunk_assoc;

	ops->chunk_init(chunk->region->priv, chunk->priv, priority);

	err = rhashtable_insert_fast(&group->chunk_ht, &chunk->ht_node,
				     mlxsw_sp_acl_tcam_chunk_ht_params);
	if (err)
		goto err_rhashtable_insert;

	return chunk;

err_rhashtable_insert:
	ops->chunk_fini(chunk->priv);
	mlxsw_sp_acl_tcam_chunk_deassoc(mlxsw_sp, chunk);
err_chunk_assoc:
	kfree(chunk);
	return ERR_PTR(err);
}

static void
mlxsw_sp_acl_tcam_chunk_destroy(struct mlxsw_sp *mlxsw_sp,
				struct mlxsw_sp_acl_tcam_chunk *chunk)
{
	const struct mlxsw_sp_acl_tcam_ops *ops = mlxsw_sp->acl_tcam_ops;
	struct mlxsw_sp_acl_tcam_group *group = chunk->group;

	rhashtable_remove_fast(&group->chunk_ht, &chunk->ht_node,
			       mlxsw_sp_acl_tcam_chunk_ht_params);
	ops->chunk_fini(chunk->priv);
	mlxsw_sp_acl_tcam_chunk_deassoc(mlxsw_sp, chunk);
	kfree(chunk);
}

static struct mlxsw_sp_acl_tcam_chunk *
mlxsw_sp_acl_tcam_chunk_get(struct mlxsw_sp *mlxsw_sp,
			    struct mlxsw_sp_acl_tcam_group *group,
			    unsigned int priority,
			    struct mlxsw_afk_element_usage *elusage)
{
	struct mlxsw_sp_acl_tcam_chunk *chunk;

	chunk = rhashtable_lookup_fast(&group->chunk_ht, &priority,
				       mlxsw_sp_acl_tcam_chunk_ht_params);
	if (chunk) {
		if (WARN_ON(!mlxsw_afk_key_info_subset(chunk->region->key_info,
						       elusage)))
			return ERR_PTR(-EINVAL);
		chunk->ref_count++;
		return chunk;
	}
	return mlxsw_sp_acl_tcam_chunk_create(mlxsw_sp, group,
					      priority, elusage);
}

static void mlxsw_sp_acl_tcam_chunk_put(struct mlxsw_sp *mlxsw_sp,
					struct mlxsw_sp_acl_tcam_chunk *chunk)
{
	if (--chunk->ref_count)
		return;
	mlxsw_sp_acl_tcam_chunk_destroy(mlxsw_sp, chunk);
}

static size_t mlxsw_sp_acl_tcam_entry_priv_size(struct mlxsw_sp *mlxsw_sp)
{
	const struct mlxsw_sp_acl_tcam_ops *ops = mlxsw_sp->acl_tcam_ops;

	return ops->entry_priv_size;
}

static int mlxsw_sp_acl_tcam_entry_add(struct mlxsw_sp *mlxsw_sp,
				       struct mlxsw_sp_acl_tcam_group *group,
				       struct mlxsw_sp_acl_tcam_entry *entry,
				       struct mlxsw_sp_acl_rule_info *rulei)
{
	const struct mlxsw_sp_acl_tcam_ops *ops = mlxsw_sp->acl_tcam_ops;
	struct mlxsw_sp_acl_tcam_chunk *chunk;
	struct mlxsw_sp_acl_tcam_region *region;
	int err;

	chunk = mlxsw_sp_acl_tcam_chunk_get(mlxsw_sp, group, rulei->priority,
					    &rulei->values.elusage);
	if (IS_ERR(chunk))
		return PTR_ERR(chunk);

	region = chunk->region;

	err = ops->entry_add(mlxsw_sp, region->priv, chunk->priv,
			     entry->priv, rulei);
	if (err)
		goto err_entry_add;
	entry->chunk = chunk;

	return 0;

err_entry_add:
	mlxsw_sp_acl_tcam_chunk_put(mlxsw_sp, chunk);
	return err;
}

static void mlxsw_sp_acl_tcam_entry_del(struct mlxsw_sp *mlxsw_sp,
					struct mlxsw_sp_acl_tcam_entry *entry)
{
	const struct mlxsw_sp_acl_tcam_ops *ops = mlxsw_sp->acl_tcam_ops;
	struct mlxsw_sp_acl_tcam_chunk *chunk = entry->chunk;
	struct mlxsw_sp_acl_tcam_region *region = chunk->region;

	ops->entry_del(mlxsw_sp, region->priv, chunk->priv, entry->priv);
	mlxsw_sp_acl_tcam_chunk_put(mlxsw_sp, chunk);
}

static int
mlxsw_sp_acl_tcam_entry_activity_get(struct mlxsw_sp *mlxsw_sp,
				     struct mlxsw_sp_acl_tcam_entry *entry,
				     bool *activity)
{
	const struct mlxsw_sp_acl_tcam_ops *ops = mlxsw_sp->acl_tcam_ops;
	struct mlxsw_sp_acl_tcam_chunk *chunk = entry->chunk;
	struct mlxsw_sp_acl_tcam_region *region = chunk->region;

	return ops->entry_activity_get(mlxsw_sp, region->priv,
				       entry->priv, activity);
}

static const enum mlxsw_afk_element mlxsw_sp_acl_tcam_pattern_ipv4[] = {
	MLXSW_AFK_ELEMENT_SRC_SYS_PORT,
	MLXSW_AFK_ELEMENT_DMAC_32_47,
	MLXSW_AFK_ELEMENT_DMAC_0_31,
	MLXSW_AFK_ELEMENT_SMAC_32_47,
	MLXSW_AFK_ELEMENT_SMAC_0_31,
	MLXSW_AFK_ELEMENT_ETHERTYPE,
	MLXSW_AFK_ELEMENT_IP_PROTO,
	MLXSW_AFK_ELEMENT_SRC_IP_0_31,
	MLXSW_AFK_ELEMENT_DST_IP_0_31,
	MLXSW_AFK_ELEMENT_DST_L4_PORT,
	MLXSW_AFK_ELEMENT_SRC_L4_PORT,
	MLXSW_AFK_ELEMENT_VID,
	MLXSW_AFK_ELEMENT_PCP,
	MLXSW_AFK_ELEMENT_TCP_FLAGS,
	MLXSW_AFK_ELEMENT_IP_TTL_,
	MLXSW_AFK_ELEMENT_IP_ECN,
	MLXSW_AFK_ELEMENT_IP_DSCP,
};

static const enum mlxsw_afk_element mlxsw_sp_acl_tcam_pattern_ipv6[] = {
	MLXSW_AFK_ELEMENT_ETHERTYPE,
	MLXSW_AFK_ELEMENT_IP_PROTO,
	MLXSW_AFK_ELEMENT_SRC_IP_96_127,
	MLXSW_AFK_ELEMENT_SRC_IP_64_95,
	MLXSW_AFK_ELEMENT_SRC_IP_32_63,
	MLXSW_AFK_ELEMENT_SRC_IP_0_31,
	MLXSW_AFK_ELEMENT_DST_IP_96_127,
	MLXSW_AFK_ELEMENT_DST_IP_64_95,
	MLXSW_AFK_ELEMENT_DST_IP_32_63,
	MLXSW_AFK_ELEMENT_DST_IP_0_31,
	MLXSW_AFK_ELEMENT_DST_L4_PORT,
	MLXSW_AFK_ELEMENT_SRC_L4_PORT,
};

static const struct mlxsw_sp_acl_tcam_pattern mlxsw_sp_acl_tcam_patterns[] = {
	{
		.elements = mlxsw_sp_acl_tcam_pattern_ipv4,
		.elements_count = ARRAY_SIZE(mlxsw_sp_acl_tcam_pattern_ipv4),
	},
	{
		.elements = mlxsw_sp_acl_tcam_pattern_ipv6,
		.elements_count = ARRAY_SIZE(mlxsw_sp_acl_tcam_pattern_ipv6),
	},
};

#define MLXSW_SP_ACL_TCAM_PATTERNS_COUNT \
	ARRAY_SIZE(mlxsw_sp_acl_tcam_patterns)

struct mlxsw_sp_acl_tcam_flower_ruleset {
	struct mlxsw_sp_acl_tcam_group group;
};

struct mlxsw_sp_acl_tcam_flower_rule {
	struct mlxsw_sp_acl_tcam_entry entry;
};

static int
mlxsw_sp_acl_tcam_flower_ruleset_add(struct mlxsw_sp *mlxsw_sp,
				     struct mlxsw_sp_acl_tcam *tcam,
				     void *ruleset_priv,
				     struct mlxsw_afk_element_usage *tmplt_elusage)
{
	struct mlxsw_sp_acl_tcam_flower_ruleset *ruleset = ruleset_priv;

	return mlxsw_sp_acl_tcam_group_add(mlxsw_sp, tcam, &ruleset->group,
					   mlxsw_sp_acl_tcam_patterns,
					   MLXSW_SP_ACL_TCAM_PATTERNS_COUNT,
					   tmplt_elusage);
}

static void
mlxsw_sp_acl_tcam_flower_ruleset_del(struct mlxsw_sp *mlxsw_sp,
				     void *ruleset_priv)
{
	struct mlxsw_sp_acl_tcam_flower_ruleset *ruleset = ruleset_priv;

	mlxsw_sp_acl_tcam_group_del(mlxsw_sp, &ruleset->group);
}

static int
mlxsw_sp_acl_tcam_flower_ruleset_bind(struct mlxsw_sp *mlxsw_sp,
				      void *ruleset_priv,
				      struct mlxsw_sp_port *mlxsw_sp_port,
				      bool ingress)
{
	struct mlxsw_sp_acl_tcam_flower_ruleset *ruleset = ruleset_priv;

	return mlxsw_sp_acl_tcam_group_bind(mlxsw_sp, &ruleset->group,
					    mlxsw_sp_port, ingress);
}

static void
mlxsw_sp_acl_tcam_flower_ruleset_unbind(struct mlxsw_sp *mlxsw_sp,
					void *ruleset_priv,
					struct mlxsw_sp_port *mlxsw_sp_port,
					bool ingress)
{
	struct mlxsw_sp_acl_tcam_flower_ruleset *ruleset = ruleset_priv;

	mlxsw_sp_acl_tcam_group_unbind(mlxsw_sp, &ruleset->group,
				       mlxsw_sp_port, ingress);
}

static u16
mlxsw_sp_acl_tcam_flower_ruleset_group_id(void *ruleset_priv)
{
	struct mlxsw_sp_acl_tcam_flower_ruleset *ruleset = ruleset_priv;

	return mlxsw_sp_acl_tcam_group_id(&ruleset->group);
}

static size_t mlxsw_sp_acl_tcam_flower_rule_priv_size(struct mlxsw_sp *mlxsw_sp)
{
	return sizeof(struct mlxsw_sp_acl_tcam_flower_rule) +
	       mlxsw_sp_acl_tcam_entry_priv_size(mlxsw_sp);
}

static int
mlxsw_sp_acl_tcam_flower_rule_add(struct mlxsw_sp *mlxsw_sp,
				  void *ruleset_priv, void *rule_priv,
				  struct mlxsw_sp_acl_rule_info *rulei)
{
	struct mlxsw_sp_acl_tcam_flower_ruleset *ruleset = ruleset_priv;
	struct mlxsw_sp_acl_tcam_flower_rule *rule = rule_priv;

	return mlxsw_sp_acl_tcam_entry_add(mlxsw_sp, &ruleset->group,
					   &rule->entry, rulei);
}

static void
mlxsw_sp_acl_tcam_flower_rule_del(struct mlxsw_sp *mlxsw_sp, void *rule_priv)
{
	struct mlxsw_sp_acl_tcam_flower_rule *rule = rule_priv;

	mlxsw_sp_acl_tcam_entry_del(mlxsw_sp, &rule->entry);
}

static int
mlxsw_sp_acl_tcam_flower_rule_activity_get(struct mlxsw_sp *mlxsw_sp,
					   void *rule_priv, bool *activity)
{
	struct mlxsw_sp_acl_tcam_flower_rule *rule = rule_priv;

	return mlxsw_sp_acl_tcam_entry_activity_get(mlxsw_sp, &rule->entry,
						    activity);
}

static const struct mlxsw_sp_acl_profile_ops mlxsw_sp_acl_tcam_flower_ops = {
	.ruleset_priv_size	= sizeof(struct mlxsw_sp_acl_tcam_flower_ruleset),
	.ruleset_add		= mlxsw_sp_acl_tcam_flower_ruleset_add,
	.ruleset_del		= mlxsw_sp_acl_tcam_flower_ruleset_del,
	.ruleset_bind		= mlxsw_sp_acl_tcam_flower_ruleset_bind,
	.ruleset_unbind		= mlxsw_sp_acl_tcam_flower_ruleset_unbind,
	.ruleset_group_id	= mlxsw_sp_acl_tcam_flower_ruleset_group_id,
	.rule_priv_size		= mlxsw_sp_acl_tcam_flower_rule_priv_size,
	.rule_add		= mlxsw_sp_acl_tcam_flower_rule_add,
	.rule_del		= mlxsw_sp_acl_tcam_flower_rule_del,
	.rule_activity_get	= mlxsw_sp_acl_tcam_flower_rule_activity_get,
};

static const struct mlxsw_sp_acl_profile_ops *
mlxsw_sp_acl_tcam_profile_ops_arr[] = {
	[MLXSW_SP_ACL_PROFILE_FLOWER] = &mlxsw_sp_acl_tcam_flower_ops,
};

const struct mlxsw_sp_acl_profile_ops *
mlxsw_sp_acl_tcam_profile_ops(struct mlxsw_sp *mlxsw_sp,
			      enum mlxsw_sp_acl_profile profile)
{
	const struct mlxsw_sp_acl_profile_ops *ops;

	if (WARN_ON(profile >= ARRAY_SIZE(mlxsw_sp_acl_tcam_profile_ops_arr)))
		return NULL;
	ops = mlxsw_sp_acl_tcam_profile_ops_arr[profile];
	if (WARN_ON(!ops))
		return NULL;
	return ops;
}
