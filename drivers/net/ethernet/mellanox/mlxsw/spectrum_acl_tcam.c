// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/* Copyright (c) 2017-2018 Mellanox Technologies. All rights reserved */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/bitops.h>
#include <linux/list.h>
#include <linux/rhashtable.h>
#include <linux/netdevice.h>
#include <linux/mutex.h>
#include <trace/events/mlxsw.h>

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

#define MLXSW_SP_ACL_TCAM_VREGION_REHASH_INTRVL_DFLT 5000 /* ms */
#define MLXSW_SP_ACL_TCAM_VREGION_REHASH_INTRVL_MIN 3000 /* ms */
#define MLXSW_SP_ACL_TCAM_VREGION_REHASH_CREDITS 100 /* number of entries */

int mlxsw_sp_acl_tcam_init(struct mlxsw_sp *mlxsw_sp,
			   struct mlxsw_sp_acl_tcam *tcam)
{
	const struct mlxsw_sp_acl_tcam_ops *ops = mlxsw_sp->acl_tcam_ops;
	u64 max_tcam_regions;
	u64 max_regions;
	u64 max_groups;
	int err;

	mutex_init(&tcam->lock);
	tcam->vregion_rehash_intrvl =
			MLXSW_SP_ACL_TCAM_VREGION_REHASH_INTRVL_DFLT;
	INIT_LIST_HEAD(&tcam->vregion_list);

	max_tcam_regions = MLXSW_CORE_RES_GET(mlxsw_sp->core,
					      ACL_MAX_TCAM_REGIONS);
	max_regions = MLXSW_CORE_RES_GET(mlxsw_sp->core, ACL_MAX_REGIONS);

	/* Use 1:1 mapping between ACL region and TCAM region */
	if (max_tcam_regions < max_regions)
		max_regions = max_tcam_regions;

	tcam->used_regions = bitmap_zalloc(max_regions, GFP_KERNEL);
	if (!tcam->used_regions)
		return -ENOMEM;
	tcam->max_regions = max_regions;

	max_groups = MLXSW_CORE_RES_GET(mlxsw_sp->core, ACL_MAX_GROUPS);
	tcam->used_groups = bitmap_zalloc(max_groups, GFP_KERNEL);
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
	bitmap_free(tcam->used_groups);
err_alloc_used_groups:
	bitmap_free(tcam->used_regions);
	return err;
}

void mlxsw_sp_acl_tcam_fini(struct mlxsw_sp *mlxsw_sp,
			    struct mlxsw_sp_acl_tcam *tcam)
{
	const struct mlxsw_sp_acl_tcam_ops *ops = mlxsw_sp->acl_tcam_ops;

	mutex_destroy(&tcam->lock);
	ops->fini(mlxsw_sp, tcam->priv);
	bitmap_free(tcam->used_groups);
	bitmap_free(tcam->used_regions);
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

	/* Priority range is 1..cap_kvd_size-1. */
	max_priority = MLXSW_CORE_RES_GET(mlxsw_sp->core, KVD_SIZE) - 1;
	if (rulei->priority >= max_priority)
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
	struct mutex lock; /* guards region list updates */
	struct list_head region_list;
	unsigned int region_count;
};

struct mlxsw_sp_acl_tcam_vgroup {
	struct mlxsw_sp_acl_tcam_group group;
	struct list_head vregion_list;
	struct rhashtable vchunk_ht;
	const struct mlxsw_sp_acl_tcam_pattern *patterns;
	unsigned int patterns_count;
	bool tmplt_elusage_set;
	struct mlxsw_afk_element_usage tmplt_elusage;
	bool vregion_rehash_enabled;
	unsigned int *p_min_prio;
	unsigned int *p_max_prio;
};

struct mlxsw_sp_acl_tcam_rehash_ctx {
	void *hints_priv;
	bool this_is_rollback;
	struct mlxsw_sp_acl_tcam_vchunk *current_vchunk; /* vchunk being
							  * currently migrated.
							  */
	struct mlxsw_sp_acl_tcam_ventry *start_ventry; /* ventry to start
							* migration from in
							* a vchunk being
							* currently migrated.
							*/
	struct mlxsw_sp_acl_tcam_ventry *stop_ventry; /* ventry to stop
						       * migration at
						       * a vchunk being
						       * currently migrated.
						       */
};

struct mlxsw_sp_acl_tcam_vregion {
	struct mutex lock; /* Protects consistency of region, region2 pointers
			    * and vchunk_list.
			    */
	struct mlxsw_sp_acl_tcam_region *region;
	struct mlxsw_sp_acl_tcam_region *region2; /* Used during migration */
	struct list_head list; /* Member of a TCAM group */
	struct list_head tlist; /* Member of a TCAM */
	struct list_head vchunk_list; /* List of vchunks under this vregion */
	struct mlxsw_afk_key_info *key_info;
	struct mlxsw_sp_acl_tcam *tcam;
	struct mlxsw_sp_acl_tcam_vgroup *vgroup;
	struct {
		struct delayed_work dw;
		struct mlxsw_sp_acl_tcam_rehash_ctx ctx;
	} rehash;
	struct mlxsw_sp *mlxsw_sp;
	unsigned int ref_count;
};

struct mlxsw_sp_acl_tcam_vchunk;

struct mlxsw_sp_acl_tcam_chunk {
	struct mlxsw_sp_acl_tcam_vchunk *vchunk;
	struct mlxsw_sp_acl_tcam_region *region;
	unsigned long priv[];
	/* priv has to be always the last item */
};

struct mlxsw_sp_acl_tcam_vchunk {
	struct mlxsw_sp_acl_tcam_chunk *chunk;
	struct mlxsw_sp_acl_tcam_chunk *chunk2; /* Used during migration */
	struct list_head list; /* Member of a TCAM vregion */
	struct rhash_head ht_node; /* Member of a chunk HT */
	struct list_head ventry_list;
	unsigned int priority; /* Priority within the vregion and group */
	struct mlxsw_sp_acl_tcam_vgroup *vgroup;
	struct mlxsw_sp_acl_tcam_vregion *vregion;
	unsigned int ref_count;
};

struct mlxsw_sp_acl_tcam_entry {
	struct mlxsw_sp_acl_tcam_ventry *ventry;
	struct mlxsw_sp_acl_tcam_chunk *chunk;
	unsigned long priv[];
	/* priv has to be always the last item */
};

struct mlxsw_sp_acl_tcam_ventry {
	struct mlxsw_sp_acl_tcam_entry *entry;
	struct list_head list; /* Member of a TCAM vchunk */
	struct mlxsw_sp_acl_tcam_vchunk *vchunk;
	struct mlxsw_sp_acl_rule_info *rulei;
};

static const struct rhashtable_params mlxsw_sp_acl_tcam_vchunk_ht_params = {
	.key_len = sizeof(unsigned int),
	.key_offset = offsetof(struct mlxsw_sp_acl_tcam_vchunk, priority),
	.head_offset = offsetof(struct mlxsw_sp_acl_tcam_vchunk, ht_node),
	.automatic_shrinking = true,
};

static int mlxsw_sp_acl_tcam_group_update(struct mlxsw_sp *mlxsw_sp,
					  struct mlxsw_sp_acl_tcam_group *group)
{
	struct mlxsw_sp_acl_tcam_region *region;
	char pagt_pl[MLXSW_REG_PAGT_LEN];
	int acl_index = 0;

	mlxsw_reg_pagt_pack(pagt_pl, group->id);
	list_for_each_entry(region, &group->region_list, list) {
		bool multi = false;

		/* Check if the next entry in the list has the same vregion. */
		if (region->list.next != &group->region_list &&
		    list_next_entry(region, list)->vregion == region->vregion)
			multi = true;
		mlxsw_reg_pagt_acl_id_pack(pagt_pl, acl_index++,
					   region->id, multi);
	}
	mlxsw_reg_pagt_size_set(pagt_pl, acl_index);
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(pagt), pagt_pl);
}

static int
mlxsw_sp_acl_tcam_group_add(struct mlxsw_sp_acl_tcam *tcam,
			    struct mlxsw_sp_acl_tcam_group *group)
{
	int err;

	group->tcam = tcam;
	INIT_LIST_HEAD(&group->region_list);

	err = mlxsw_sp_acl_tcam_group_id_get(tcam, &group->id);
	if (err)
		return err;

	mutex_init(&group->lock);

	return 0;
}

static void mlxsw_sp_acl_tcam_group_del(struct mlxsw_sp_acl_tcam_group *group)
{
	struct mlxsw_sp_acl_tcam *tcam = group->tcam;

	mutex_destroy(&group->lock);
	mlxsw_sp_acl_tcam_group_id_put(tcam, group->id);
	WARN_ON(!list_empty(&group->region_list));
}

static int
mlxsw_sp_acl_tcam_vgroup_add(struct mlxsw_sp *mlxsw_sp,
			     struct mlxsw_sp_acl_tcam *tcam,
			     struct mlxsw_sp_acl_tcam_vgroup *vgroup,
			     const struct mlxsw_sp_acl_tcam_pattern *patterns,
			     unsigned int patterns_count,
			     struct mlxsw_afk_element_usage *tmplt_elusage,
			     bool vregion_rehash_enabled,
			     unsigned int *p_min_prio,
			     unsigned int *p_max_prio)
{
	int err;

	vgroup->patterns = patterns;
	vgroup->patterns_count = patterns_count;
	vgroup->vregion_rehash_enabled = vregion_rehash_enabled;
	vgroup->p_min_prio = p_min_prio;
	vgroup->p_max_prio = p_max_prio;

	if (tmplt_elusage) {
		vgroup->tmplt_elusage_set = true;
		memcpy(&vgroup->tmplt_elusage, tmplt_elusage,
		       sizeof(vgroup->tmplt_elusage));
	}
	INIT_LIST_HEAD(&vgroup->vregion_list);

	err = mlxsw_sp_acl_tcam_group_add(tcam, &vgroup->group);
	if (err)
		return err;

	err = rhashtable_init(&vgroup->vchunk_ht,
			      &mlxsw_sp_acl_tcam_vchunk_ht_params);
	if (err)
		goto err_rhashtable_init;

	return 0;

err_rhashtable_init:
	mlxsw_sp_acl_tcam_group_del(&vgroup->group);
	return err;
}

static void
mlxsw_sp_acl_tcam_vgroup_del(struct mlxsw_sp_acl_tcam_vgroup *vgroup)
{
	rhashtable_destroy(&vgroup->vchunk_ht);
	mlxsw_sp_acl_tcam_group_del(&vgroup->group);
	WARN_ON(!list_empty(&vgroup->vregion_list));
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
mlxsw_sp_acl_tcam_vregion_prio(struct mlxsw_sp_acl_tcam_vregion *vregion)
{
	struct mlxsw_sp_acl_tcam_vchunk *vchunk;

	if (list_empty(&vregion->vchunk_list))
		return 0;
	/* As a priority of a vregion, return priority of the first vchunk */
	vchunk = list_first_entry(&vregion->vchunk_list,
				  typeof(*vchunk), list);
	return vchunk->priority;
}

static unsigned int
mlxsw_sp_acl_tcam_vregion_max_prio(struct mlxsw_sp_acl_tcam_vregion *vregion)
{
	struct mlxsw_sp_acl_tcam_vchunk *vchunk;

	if (list_empty(&vregion->vchunk_list))
		return 0;
	vchunk = list_last_entry(&vregion->vchunk_list,
				 typeof(*vchunk), list);
	return vchunk->priority;
}

static void
mlxsw_sp_acl_tcam_vgroup_prio_update(struct mlxsw_sp_acl_tcam_vgroup *vgroup)
{
	struct mlxsw_sp_acl_tcam_vregion *vregion;

	if (list_empty(&vgroup->vregion_list))
		return;
	vregion = list_first_entry(&vgroup->vregion_list,
				   typeof(*vregion), list);
	*vgroup->p_min_prio = mlxsw_sp_acl_tcam_vregion_prio(vregion);
	vregion = list_last_entry(&vgroup->vregion_list,
				  typeof(*vregion), list);
	*vgroup->p_max_prio = mlxsw_sp_acl_tcam_vregion_max_prio(vregion);
}

static int
mlxsw_sp_acl_tcam_group_region_attach(struct mlxsw_sp *mlxsw_sp,
				      struct mlxsw_sp_acl_tcam_group *group,
				      struct mlxsw_sp_acl_tcam_region *region,
				      unsigned int priority,
				      struct mlxsw_sp_acl_tcam_region *next_region)
{
	struct mlxsw_sp_acl_tcam_region *region2;
	struct list_head *pos;
	int err;

	mutex_lock(&group->lock);
	if (group->region_count == group->tcam->max_group_size) {
		err = -ENOBUFS;
		goto err_region_count_check;
	}

	if (next_region) {
		/* If the next region is defined, place the new one
		 * before it. The next one is a sibling.
		 */
		pos = &next_region->list;
	} else {
		/* Position the region inside the list according to priority */
		list_for_each(pos, &group->region_list) {
			region2 = list_entry(pos, typeof(*region2), list);
			if (mlxsw_sp_acl_tcam_vregion_prio(region2->vregion) >
			    priority)
				break;
		}
	}
	list_add_tail(&region->list, pos);
	region->group = group;

	err = mlxsw_sp_acl_tcam_group_update(mlxsw_sp, group);
	if (err)
		goto err_group_update;

	group->region_count++;
	mutex_unlock(&group->lock);
	return 0;

err_group_update:
	list_del(&region->list);
err_region_count_check:
	mutex_unlock(&group->lock);
	return err;
}

static void
mlxsw_sp_acl_tcam_group_region_detach(struct mlxsw_sp *mlxsw_sp,
				      struct mlxsw_sp_acl_tcam_region *region)
{
	struct mlxsw_sp_acl_tcam_group *group = region->group;

	mutex_lock(&group->lock);
	list_del(&region->list);
	group->region_count--;
	mlxsw_sp_acl_tcam_group_update(mlxsw_sp, group);
	mutex_unlock(&group->lock);
}

static int
mlxsw_sp_acl_tcam_vgroup_vregion_attach(struct mlxsw_sp *mlxsw_sp,
					struct mlxsw_sp_acl_tcam_vgroup *vgroup,
					struct mlxsw_sp_acl_tcam_vregion *vregion,
					unsigned int priority)
{
	struct mlxsw_sp_acl_tcam_vregion *vregion2;
	struct list_head *pos;
	int err;

	/* Position the vregion inside the list according to priority */
	list_for_each(pos, &vgroup->vregion_list) {
		vregion2 = list_entry(pos, typeof(*vregion2), list);
		if (mlxsw_sp_acl_tcam_vregion_prio(vregion2) > priority)
			break;
	}
	list_add_tail(&vregion->list, pos);

	err = mlxsw_sp_acl_tcam_group_region_attach(mlxsw_sp, &vgroup->group,
						    vregion->region,
						    priority, NULL);
	if (err)
		goto err_region_attach;

	return 0;

err_region_attach:
	list_del(&vregion->list);
	return err;
}

static void
mlxsw_sp_acl_tcam_vgroup_vregion_detach(struct mlxsw_sp *mlxsw_sp,
					struct mlxsw_sp_acl_tcam_vregion *vregion)
{
	list_del(&vregion->list);
	if (vregion->region2)
		mlxsw_sp_acl_tcam_group_region_detach(mlxsw_sp,
						      vregion->region2);
	mlxsw_sp_acl_tcam_group_region_detach(mlxsw_sp, vregion->region);
}

static struct mlxsw_sp_acl_tcam_vregion *
mlxsw_sp_acl_tcam_vgroup_vregion_find(struct mlxsw_sp_acl_tcam_vgroup *vgroup,
				      unsigned int priority,
				      struct mlxsw_afk_element_usage *elusage,
				      bool *p_need_split)
{
	struct mlxsw_sp_acl_tcam_vregion *vregion, *vregion2;
	struct list_head *pos;
	bool issubset;

	list_for_each(pos, &vgroup->vregion_list) {
		vregion = list_entry(pos, typeof(*vregion), list);

		/* First, check if the requested priority does not rather belong
		 * under some of the next vregions.
		 */
		if (pos->next != &vgroup->vregion_list) { /* not last */
			vregion2 = list_entry(pos->next, typeof(*vregion2),
					      list);
			if (priority >=
			    mlxsw_sp_acl_tcam_vregion_prio(vregion2))
				continue;
		}

		issubset = mlxsw_afk_key_info_subset(vregion->key_info,
						     elusage);

		/* If requested element usage would not fit and the priority
		 * is lower than the currently inspected vregion we cannot
		 * use this region, so return NULL to indicate new vregion has
		 * to be created.
		 */
		if (!issubset &&
		    priority < mlxsw_sp_acl_tcam_vregion_prio(vregion))
			return NULL;

		/* If requested element usage would not fit and the priority
		 * is higher than the currently inspected vregion we cannot
		 * use this vregion. There is still some hope that the next
		 * vregion would be the fit. So let it be processed and
		 * eventually break at the check right above this.
		 */
		if (!issubset &&
		    priority > mlxsw_sp_acl_tcam_vregion_max_prio(vregion))
			continue;

		/* Indicate if the vregion needs to be split in order to add
		 * the requested priority. Split is needed when requested
		 * element usage won't fit into the found vregion.
		 */
		*p_need_split = !issubset;
		return vregion;
	}
	return NULL; /* New vregion has to be created. */
}

static void
mlxsw_sp_acl_tcam_vgroup_use_patterns(struct mlxsw_sp_acl_tcam_vgroup *vgroup,
				      struct mlxsw_afk_element_usage *elusage,
				      struct mlxsw_afk_element_usage *out)
{
	const struct mlxsw_sp_acl_tcam_pattern *pattern;
	int i;

	/* In case the template is set, we don't have to look up the pattern
	 * and just use the template.
	 */
	if (vgroup->tmplt_elusage_set) {
		memcpy(out, &vgroup->tmplt_elusage, sizeof(*out));
		WARN_ON(!mlxsw_afk_element_usage_subset(elusage, out));
		return;
	}

	for (i = 0; i < vgroup->patterns_count; i++) {
		pattern = &vgroup->patterns[i];
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
				struct mlxsw_sp_acl_tcam_vregion *vregion,
				void *hints_priv)
{
	const struct mlxsw_sp_acl_tcam_ops *ops = mlxsw_sp->acl_tcam_ops;
	struct mlxsw_sp_acl_tcam_region *region;
	int err;

	region = kzalloc(sizeof(*region) + ops->region_priv_size, GFP_KERNEL);
	if (!region)
		return ERR_PTR(-ENOMEM);
	region->mlxsw_sp = mlxsw_sp;
	region->vregion = vregion;
	region->key_info = vregion->key_info;

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

	err = ops->region_init(mlxsw_sp, region->priv, tcam->priv,
			       region, hints_priv);
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
	mlxsw_sp_acl_tcam_region_id_put(region->group->tcam,
					region->id);
	kfree(region);
}

static void
mlxsw_sp_acl_tcam_vregion_rehash_work_schedule(struct mlxsw_sp_acl_tcam_vregion *vregion)
{
	unsigned long interval = vregion->tcam->vregion_rehash_intrvl;

	if (!interval)
		return;
	mlxsw_core_schedule_dw(&vregion->rehash.dw,
			       msecs_to_jiffies(interval));
}

static void
mlxsw_sp_acl_tcam_vregion_rehash(struct mlxsw_sp *mlxsw_sp,
				 struct mlxsw_sp_acl_tcam_vregion *vregion,
				 int *credits);

static void mlxsw_sp_acl_tcam_vregion_rehash_work(struct work_struct *work)
{
	struct mlxsw_sp_acl_tcam_vregion *vregion =
		container_of(work, struct mlxsw_sp_acl_tcam_vregion,
			     rehash.dw.work);
	int credits = MLXSW_SP_ACL_TCAM_VREGION_REHASH_CREDITS;

	mlxsw_sp_acl_tcam_vregion_rehash(vregion->mlxsw_sp, vregion, &credits);
	if (credits < 0)
		/* Rehash gone out of credits so it was interrupted.
		 * Schedule the work as soon as possible to continue.
		 */
		mlxsw_core_schedule_dw(&vregion->rehash.dw, 0);
	else
		mlxsw_sp_acl_tcam_vregion_rehash_work_schedule(vregion);
}

static void
mlxsw_sp_acl_tcam_rehash_ctx_vchunk_changed(struct mlxsw_sp_acl_tcam_vchunk *vchunk)
{
	struct mlxsw_sp_acl_tcam_vregion *vregion = vchunk->vregion;

	/* If a rule was added or deleted from vchunk which is currently
	 * under rehash migration, we have to reset the ventry pointers
	 * to make sure all rules are properly migrated.
	 */
	if (vregion->rehash.ctx.current_vchunk == vchunk) {
		vregion->rehash.ctx.start_ventry = NULL;
		vregion->rehash.ctx.stop_ventry = NULL;
	}
}

static void
mlxsw_sp_acl_tcam_rehash_ctx_vregion_changed(struct mlxsw_sp_acl_tcam_vregion *vregion)
{
	/* If a chunk was added or deleted from vregion we have to reset
	 * the current chunk pointer to make sure all chunks
	 * are properly migrated.
	 */
	vregion->rehash.ctx.current_vchunk = NULL;
}

static struct mlxsw_sp_acl_tcam_vregion *
mlxsw_sp_acl_tcam_vregion_create(struct mlxsw_sp *mlxsw_sp,
				 struct mlxsw_sp_acl_tcam_vgroup *vgroup,
				 unsigned int priority,
				 struct mlxsw_afk_element_usage *elusage)
{
	const struct mlxsw_sp_acl_tcam_ops *ops = mlxsw_sp->acl_tcam_ops;
	struct mlxsw_afk *afk = mlxsw_sp_acl_afk(mlxsw_sp->acl);
	struct mlxsw_sp_acl_tcam *tcam = vgroup->group.tcam;
	struct mlxsw_sp_acl_tcam_vregion *vregion;
	int err;

	vregion = kzalloc(sizeof(*vregion), GFP_KERNEL);
	if (!vregion)
		return ERR_PTR(-ENOMEM);
	INIT_LIST_HEAD(&vregion->vchunk_list);
	mutex_init(&vregion->lock);
	vregion->tcam = tcam;
	vregion->mlxsw_sp = mlxsw_sp;
	vregion->vgroup = vgroup;
	vregion->ref_count = 1;

	vregion->key_info = mlxsw_afk_key_info_get(afk, elusage);
	if (IS_ERR(vregion->key_info)) {
		err = PTR_ERR(vregion->key_info);
		goto err_key_info_get;
	}

	vregion->region = mlxsw_sp_acl_tcam_region_create(mlxsw_sp, tcam,
							  vregion, NULL);
	if (IS_ERR(vregion->region)) {
		err = PTR_ERR(vregion->region);
		goto err_region_create;
	}

	err = mlxsw_sp_acl_tcam_vgroup_vregion_attach(mlxsw_sp, vgroup, vregion,
						      priority);
	if (err)
		goto err_vgroup_vregion_attach;

	if (vgroup->vregion_rehash_enabled && ops->region_rehash_hints_get) {
		/* Create the delayed work for vregion periodic rehash */
		INIT_DELAYED_WORK(&vregion->rehash.dw,
				  mlxsw_sp_acl_tcam_vregion_rehash_work);
		mlxsw_sp_acl_tcam_vregion_rehash_work_schedule(vregion);
		mutex_lock(&tcam->lock);
		list_add_tail(&vregion->tlist, &tcam->vregion_list);
		mutex_unlock(&tcam->lock);
	}

	return vregion;

err_vgroup_vregion_attach:
	mlxsw_sp_acl_tcam_region_destroy(mlxsw_sp, vregion->region);
err_region_create:
	mlxsw_afk_key_info_put(vregion->key_info);
err_key_info_get:
	kfree(vregion);
	return ERR_PTR(err);
}

static void
mlxsw_sp_acl_tcam_vregion_destroy(struct mlxsw_sp *mlxsw_sp,
				  struct mlxsw_sp_acl_tcam_vregion *vregion)
{
	const struct mlxsw_sp_acl_tcam_ops *ops = mlxsw_sp->acl_tcam_ops;
	struct mlxsw_sp_acl_tcam_vgroup *vgroup = vregion->vgroup;
	struct mlxsw_sp_acl_tcam *tcam = vregion->tcam;

	if (vgroup->vregion_rehash_enabled && ops->region_rehash_hints_get) {
		mutex_lock(&tcam->lock);
		list_del(&vregion->tlist);
		mutex_unlock(&tcam->lock);
		cancel_delayed_work_sync(&vregion->rehash.dw);
	}
	mlxsw_sp_acl_tcam_vgroup_vregion_detach(mlxsw_sp, vregion);
	if (vregion->region2)
		mlxsw_sp_acl_tcam_region_destroy(mlxsw_sp, vregion->region2);
	mlxsw_sp_acl_tcam_region_destroy(mlxsw_sp, vregion->region);
	mlxsw_afk_key_info_put(vregion->key_info);
	mutex_destroy(&vregion->lock);
	kfree(vregion);
}

u32 mlxsw_sp_acl_tcam_vregion_rehash_intrvl_get(struct mlxsw_sp *mlxsw_sp,
						struct mlxsw_sp_acl_tcam *tcam)
{
	const struct mlxsw_sp_acl_tcam_ops *ops = mlxsw_sp->acl_tcam_ops;
	u32 vregion_rehash_intrvl;

	if (WARN_ON(!ops->region_rehash_hints_get))
		return 0;
	vregion_rehash_intrvl = tcam->vregion_rehash_intrvl;
	return vregion_rehash_intrvl;
}

int mlxsw_sp_acl_tcam_vregion_rehash_intrvl_set(struct mlxsw_sp *mlxsw_sp,
						struct mlxsw_sp_acl_tcam *tcam,
						u32 val)
{
	const struct mlxsw_sp_acl_tcam_ops *ops = mlxsw_sp->acl_tcam_ops;
	struct mlxsw_sp_acl_tcam_vregion *vregion;

	if (val < MLXSW_SP_ACL_TCAM_VREGION_REHASH_INTRVL_MIN && val)
		return -EINVAL;
	if (WARN_ON(!ops->region_rehash_hints_get))
		return -EOPNOTSUPP;
	tcam->vregion_rehash_intrvl = val;
	mutex_lock(&tcam->lock);
	list_for_each_entry(vregion, &tcam->vregion_list, tlist) {
		if (val)
			mlxsw_core_schedule_dw(&vregion->rehash.dw, 0);
		else
			cancel_delayed_work_sync(&vregion->rehash.dw);
	}
	mutex_unlock(&tcam->lock);
	return 0;
}

static struct mlxsw_sp_acl_tcam_vregion *
mlxsw_sp_acl_tcam_vregion_get(struct mlxsw_sp *mlxsw_sp,
			      struct mlxsw_sp_acl_tcam_vgroup *vgroup,
			      unsigned int priority,
			      struct mlxsw_afk_element_usage *elusage)
{
	struct mlxsw_afk_element_usage vregion_elusage;
	struct mlxsw_sp_acl_tcam_vregion *vregion;
	bool need_split;

	vregion = mlxsw_sp_acl_tcam_vgroup_vregion_find(vgroup, priority,
							elusage, &need_split);
	if (vregion) {
		if (need_split) {
			/* According to priority, new vchunk should belong to
			 * an existing vregion. However, this vchunk needs
			 * elements that vregion does not contain. We need
			 * to split the existing vregion into two and create
			 * a new vregion for the new vchunk in between.
			 * This is not supported now.
			 */
			return ERR_PTR(-EOPNOTSUPP);
		}
		vregion->ref_count++;
		return vregion;
	}

	mlxsw_sp_acl_tcam_vgroup_use_patterns(vgroup, elusage,
					      &vregion_elusage);

	return mlxsw_sp_acl_tcam_vregion_create(mlxsw_sp, vgroup, priority,
						&vregion_elusage);
}

static void
mlxsw_sp_acl_tcam_vregion_put(struct mlxsw_sp *mlxsw_sp,
			      struct mlxsw_sp_acl_tcam_vregion *vregion)
{
	if (--vregion->ref_count)
		return;
	mlxsw_sp_acl_tcam_vregion_destroy(mlxsw_sp, vregion);
}

static struct mlxsw_sp_acl_tcam_chunk *
mlxsw_sp_acl_tcam_chunk_create(struct mlxsw_sp *mlxsw_sp,
			       struct mlxsw_sp_acl_tcam_vchunk *vchunk,
			       struct mlxsw_sp_acl_tcam_region *region)
{
	const struct mlxsw_sp_acl_tcam_ops *ops = mlxsw_sp->acl_tcam_ops;
	struct mlxsw_sp_acl_tcam_chunk *chunk;

	chunk = kzalloc(sizeof(*chunk) + ops->chunk_priv_size, GFP_KERNEL);
	if (!chunk)
		return ERR_PTR(-ENOMEM);
	chunk->vchunk = vchunk;
	chunk->region = region;

	ops->chunk_init(region->priv, chunk->priv, vchunk->priority);
	return chunk;
}

static void
mlxsw_sp_acl_tcam_chunk_destroy(struct mlxsw_sp *mlxsw_sp,
				struct mlxsw_sp_acl_tcam_chunk *chunk)
{
	const struct mlxsw_sp_acl_tcam_ops *ops = mlxsw_sp->acl_tcam_ops;

	ops->chunk_fini(chunk->priv);
	kfree(chunk);
}

static struct mlxsw_sp_acl_tcam_vchunk *
mlxsw_sp_acl_tcam_vchunk_create(struct mlxsw_sp *mlxsw_sp,
				struct mlxsw_sp_acl_tcam_vgroup *vgroup,
				unsigned int priority,
				struct mlxsw_afk_element_usage *elusage)
{
	struct mlxsw_sp_acl_tcam_vchunk *vchunk, *vchunk2;
	struct mlxsw_sp_acl_tcam_vregion *vregion;
	struct list_head *pos;
	int err;

	if (priority == MLXSW_SP_ACL_TCAM_CATCHALL_PRIO)
		return ERR_PTR(-EINVAL);

	vchunk = kzalloc(sizeof(*vchunk), GFP_KERNEL);
	if (!vchunk)
		return ERR_PTR(-ENOMEM);
	INIT_LIST_HEAD(&vchunk->ventry_list);
	vchunk->priority = priority;
	vchunk->vgroup = vgroup;
	vchunk->ref_count = 1;

	vregion = mlxsw_sp_acl_tcam_vregion_get(mlxsw_sp, vgroup,
						priority, elusage);
	if (IS_ERR(vregion)) {
		err = PTR_ERR(vregion);
		goto err_vregion_get;
	}

	vchunk->vregion = vregion;

	err = rhashtable_insert_fast(&vgroup->vchunk_ht, &vchunk->ht_node,
				     mlxsw_sp_acl_tcam_vchunk_ht_params);
	if (err)
		goto err_rhashtable_insert;

	mutex_lock(&vregion->lock);
	vchunk->chunk = mlxsw_sp_acl_tcam_chunk_create(mlxsw_sp, vchunk,
						       vchunk->vregion->region);
	if (IS_ERR(vchunk->chunk)) {
		mutex_unlock(&vregion->lock);
		err = PTR_ERR(vchunk->chunk);
		goto err_chunk_create;
	}

	mlxsw_sp_acl_tcam_rehash_ctx_vregion_changed(vregion);

	/* Position the vchunk inside the list according to priority */
	list_for_each(pos, &vregion->vchunk_list) {
		vchunk2 = list_entry(pos, typeof(*vchunk2), list);
		if (vchunk2->priority > priority)
			break;
	}
	list_add_tail(&vchunk->list, pos);
	mutex_unlock(&vregion->lock);
	mlxsw_sp_acl_tcam_vgroup_prio_update(vgroup);

	return vchunk;

err_chunk_create:
	rhashtable_remove_fast(&vgroup->vchunk_ht, &vchunk->ht_node,
			       mlxsw_sp_acl_tcam_vchunk_ht_params);
err_rhashtable_insert:
	mlxsw_sp_acl_tcam_vregion_put(mlxsw_sp, vregion);
err_vregion_get:
	kfree(vchunk);
	return ERR_PTR(err);
}

static void
mlxsw_sp_acl_tcam_vchunk_destroy(struct mlxsw_sp *mlxsw_sp,
				 struct mlxsw_sp_acl_tcam_vchunk *vchunk)
{
	struct mlxsw_sp_acl_tcam_vregion *vregion = vchunk->vregion;
	struct mlxsw_sp_acl_tcam_vgroup *vgroup = vchunk->vgroup;

	mutex_lock(&vregion->lock);
	mlxsw_sp_acl_tcam_rehash_ctx_vregion_changed(vregion);
	list_del(&vchunk->list);
	if (vchunk->chunk2)
		mlxsw_sp_acl_tcam_chunk_destroy(mlxsw_sp, vchunk->chunk2);
	mlxsw_sp_acl_tcam_chunk_destroy(mlxsw_sp, vchunk->chunk);
	mutex_unlock(&vregion->lock);
	rhashtable_remove_fast(&vgroup->vchunk_ht, &vchunk->ht_node,
			       mlxsw_sp_acl_tcam_vchunk_ht_params);
	mlxsw_sp_acl_tcam_vregion_put(mlxsw_sp, vchunk->vregion);
	kfree(vchunk);
	mlxsw_sp_acl_tcam_vgroup_prio_update(vgroup);
}

static struct mlxsw_sp_acl_tcam_vchunk *
mlxsw_sp_acl_tcam_vchunk_get(struct mlxsw_sp *mlxsw_sp,
			     struct mlxsw_sp_acl_tcam_vgroup *vgroup,
			     unsigned int priority,
			     struct mlxsw_afk_element_usage *elusage)
{
	struct mlxsw_sp_acl_tcam_vchunk *vchunk;

	vchunk = rhashtable_lookup_fast(&vgroup->vchunk_ht, &priority,
					mlxsw_sp_acl_tcam_vchunk_ht_params);
	if (vchunk) {
		if (WARN_ON(!mlxsw_afk_key_info_subset(vchunk->vregion->key_info,
						       elusage)))
			return ERR_PTR(-EINVAL);
		vchunk->ref_count++;
		return vchunk;
	}
	return mlxsw_sp_acl_tcam_vchunk_create(mlxsw_sp, vgroup,
					       priority, elusage);
}

static void
mlxsw_sp_acl_tcam_vchunk_put(struct mlxsw_sp *mlxsw_sp,
			     struct mlxsw_sp_acl_tcam_vchunk *vchunk)
{
	if (--vchunk->ref_count)
		return;
	mlxsw_sp_acl_tcam_vchunk_destroy(mlxsw_sp, vchunk);
}

static struct mlxsw_sp_acl_tcam_entry *
mlxsw_sp_acl_tcam_entry_create(struct mlxsw_sp *mlxsw_sp,
			       struct mlxsw_sp_acl_tcam_ventry *ventry,
			       struct mlxsw_sp_acl_tcam_chunk *chunk)
{
	const struct mlxsw_sp_acl_tcam_ops *ops = mlxsw_sp->acl_tcam_ops;
	struct mlxsw_sp_acl_tcam_entry *entry;
	int err;

	entry = kzalloc(sizeof(*entry) + ops->entry_priv_size, GFP_KERNEL);
	if (!entry)
		return ERR_PTR(-ENOMEM);
	entry->ventry = ventry;
	entry->chunk = chunk;

	err = ops->entry_add(mlxsw_sp, chunk->region->priv, chunk->priv,
			     entry->priv, ventry->rulei);
	if (err)
		goto err_entry_add;

	return entry;

err_entry_add:
	kfree(entry);
	return ERR_PTR(err);
}

static void mlxsw_sp_acl_tcam_entry_destroy(struct mlxsw_sp *mlxsw_sp,
					    struct mlxsw_sp_acl_tcam_entry *entry)
{
	const struct mlxsw_sp_acl_tcam_ops *ops = mlxsw_sp->acl_tcam_ops;

	ops->entry_del(mlxsw_sp, entry->chunk->region->priv,
		       entry->chunk->priv, entry->priv);
	kfree(entry);
}

static int
mlxsw_sp_acl_tcam_entry_action_replace(struct mlxsw_sp *mlxsw_sp,
				       struct mlxsw_sp_acl_tcam_region *region,
				       struct mlxsw_sp_acl_tcam_entry *entry,
				       struct mlxsw_sp_acl_rule_info *rulei)
{
	const struct mlxsw_sp_acl_tcam_ops *ops = mlxsw_sp->acl_tcam_ops;

	return ops->entry_action_replace(mlxsw_sp, region->priv,
					 entry->priv, rulei);
}

static int
mlxsw_sp_acl_tcam_entry_activity_get(struct mlxsw_sp *mlxsw_sp,
				     struct mlxsw_sp_acl_tcam_entry *entry,
				     bool *activity)
{
	const struct mlxsw_sp_acl_tcam_ops *ops = mlxsw_sp->acl_tcam_ops;

	return ops->entry_activity_get(mlxsw_sp, entry->chunk->region->priv,
				       entry->priv, activity);
}

static int mlxsw_sp_acl_tcam_ventry_add(struct mlxsw_sp *mlxsw_sp,
					struct mlxsw_sp_acl_tcam_vgroup *vgroup,
					struct mlxsw_sp_acl_tcam_ventry *ventry,
					struct mlxsw_sp_acl_rule_info *rulei)
{
	struct mlxsw_sp_acl_tcam_vregion *vregion;
	struct mlxsw_sp_acl_tcam_vchunk *vchunk;
	int err;

	vchunk = mlxsw_sp_acl_tcam_vchunk_get(mlxsw_sp, vgroup, rulei->priority,
					      &rulei->values.elusage);
	if (IS_ERR(vchunk))
		return PTR_ERR(vchunk);

	ventry->vchunk = vchunk;
	ventry->rulei = rulei;
	vregion = vchunk->vregion;

	mutex_lock(&vregion->lock);
	ventry->entry = mlxsw_sp_acl_tcam_entry_create(mlxsw_sp, ventry,
						       vchunk->chunk);
	if (IS_ERR(ventry->entry)) {
		mutex_unlock(&vregion->lock);
		err = PTR_ERR(ventry->entry);
		goto err_entry_create;
	}

	list_add_tail(&ventry->list, &vchunk->ventry_list);
	mlxsw_sp_acl_tcam_rehash_ctx_vchunk_changed(vchunk);
	mutex_unlock(&vregion->lock);

	return 0;

err_entry_create:
	mlxsw_sp_acl_tcam_vchunk_put(mlxsw_sp, vchunk);
	return err;
}

static void mlxsw_sp_acl_tcam_ventry_del(struct mlxsw_sp *mlxsw_sp,
					 struct mlxsw_sp_acl_tcam_ventry *ventry)
{
	struct mlxsw_sp_acl_tcam_vchunk *vchunk = ventry->vchunk;
	struct mlxsw_sp_acl_tcam_vregion *vregion = vchunk->vregion;

	mutex_lock(&vregion->lock);
	mlxsw_sp_acl_tcam_rehash_ctx_vchunk_changed(vchunk);
	list_del(&ventry->list);
	mlxsw_sp_acl_tcam_entry_destroy(mlxsw_sp, ventry->entry);
	mutex_unlock(&vregion->lock);
	mlxsw_sp_acl_tcam_vchunk_put(mlxsw_sp, vchunk);
}

static int
mlxsw_sp_acl_tcam_ventry_action_replace(struct mlxsw_sp *mlxsw_sp,
					struct mlxsw_sp_acl_tcam_ventry *ventry,
					struct mlxsw_sp_acl_rule_info *rulei)
{
	struct mlxsw_sp_acl_tcam_vchunk *vchunk = ventry->vchunk;

	return mlxsw_sp_acl_tcam_entry_action_replace(mlxsw_sp,
						      vchunk->vregion->region,
						      ventry->entry, rulei);
}

static int
mlxsw_sp_acl_tcam_ventry_activity_get(struct mlxsw_sp *mlxsw_sp,
				      struct mlxsw_sp_acl_tcam_ventry *ventry,
				      bool *activity)
{
	return mlxsw_sp_acl_tcam_entry_activity_get(mlxsw_sp,
						    ventry->entry, activity);
}

static int
mlxsw_sp_acl_tcam_ventry_migrate(struct mlxsw_sp *mlxsw_sp,
				 struct mlxsw_sp_acl_tcam_ventry *ventry,
				 struct mlxsw_sp_acl_tcam_chunk *chunk,
				 int *credits)
{
	struct mlxsw_sp_acl_tcam_entry *new_entry;

	/* First check if the entry is not already where we want it to be. */
	if (ventry->entry->chunk == chunk)
		return 0;

	if (--(*credits) < 0)
		return 0;

	new_entry = mlxsw_sp_acl_tcam_entry_create(mlxsw_sp, ventry, chunk);
	if (IS_ERR(new_entry))
		return PTR_ERR(new_entry);
	mlxsw_sp_acl_tcam_entry_destroy(mlxsw_sp, ventry->entry);
	ventry->entry = new_entry;
	return 0;
}

static int
mlxsw_sp_acl_tcam_vchunk_migrate_start(struct mlxsw_sp *mlxsw_sp,
				       struct mlxsw_sp_acl_tcam_vchunk *vchunk,
				       struct mlxsw_sp_acl_tcam_region *region,
				       struct mlxsw_sp_acl_tcam_rehash_ctx *ctx)
{
	struct mlxsw_sp_acl_tcam_chunk *new_chunk;

	new_chunk = mlxsw_sp_acl_tcam_chunk_create(mlxsw_sp, vchunk, region);
	if (IS_ERR(new_chunk))
		return PTR_ERR(new_chunk);
	vchunk->chunk2 = vchunk->chunk;
	vchunk->chunk = new_chunk;
	ctx->current_vchunk = vchunk;
	ctx->start_ventry = NULL;
	ctx->stop_ventry = NULL;
	return 0;
}

static void
mlxsw_sp_acl_tcam_vchunk_migrate_end(struct mlxsw_sp *mlxsw_sp,
				     struct mlxsw_sp_acl_tcam_vchunk *vchunk,
				     struct mlxsw_sp_acl_tcam_rehash_ctx *ctx)
{
	mlxsw_sp_acl_tcam_chunk_destroy(mlxsw_sp, vchunk->chunk2);
	vchunk->chunk2 = NULL;
	ctx->current_vchunk = NULL;
}

static int
mlxsw_sp_acl_tcam_vchunk_migrate_one(struct mlxsw_sp *mlxsw_sp,
				     struct mlxsw_sp_acl_tcam_vchunk *vchunk,
				     struct mlxsw_sp_acl_tcam_region *region,
				     struct mlxsw_sp_acl_tcam_rehash_ctx *ctx,
				     int *credits)
{
	struct mlxsw_sp_acl_tcam_ventry *ventry;
	int err;

	if (vchunk->chunk->region != region) {
		err = mlxsw_sp_acl_tcam_vchunk_migrate_start(mlxsw_sp, vchunk,
							     region, ctx);
		if (err)
			return err;
	} else if (!vchunk->chunk2) {
		/* The chunk is already as it should be, nothing to do. */
		return 0;
	}

	/* If the migration got interrupted, we have the ventry to start from
	 * stored in context.
	 */
	if (ctx->start_ventry)
		ventry = ctx->start_ventry;
	else
		ventry = list_first_entry(&vchunk->ventry_list,
					  typeof(*ventry), list);

	list_for_each_entry_from(ventry, &vchunk->ventry_list, list) {
		/* During rollback, once we reach the ventry that failed
		 * to migrate, we are done.
		 */
		if (ventry == ctx->stop_ventry)
			break;

		err = mlxsw_sp_acl_tcam_ventry_migrate(mlxsw_sp, ventry,
						       vchunk->chunk, credits);
		if (err) {
			if (ctx->this_is_rollback) {
				/* Save the ventry which we ended with and try
				 * to continue later on.
				 */
				ctx->start_ventry = ventry;
				return err;
			}
			/* Swap the chunk and chunk2 pointers so the follow-up
			 * rollback call will see the original chunk pointer
			 * in vchunk->chunk.
			 */
			swap(vchunk->chunk, vchunk->chunk2);
			/* The rollback has to be done from beginning of the
			 * chunk, that is why we have to null the start_ventry.
			 * However, we know where to stop the rollback,
			 * at the current ventry.
			 */
			ctx->start_ventry = NULL;
			ctx->stop_ventry = ventry;
			return err;
		} else if (*credits < 0) {
			/* We are out of credits, the rest of the ventries
			 * will be migrated later. Save the ventry
			 * which we ended with.
			 */
			ctx->start_ventry = ventry;
			return 0;
		}
	}

	mlxsw_sp_acl_tcam_vchunk_migrate_end(mlxsw_sp, vchunk, ctx);
	return 0;
}

static int
mlxsw_sp_acl_tcam_vchunk_migrate_all(struct mlxsw_sp *mlxsw_sp,
				     struct mlxsw_sp_acl_tcam_vregion *vregion,
				     struct mlxsw_sp_acl_tcam_rehash_ctx *ctx,
				     int *credits)
{
	struct mlxsw_sp_acl_tcam_vchunk *vchunk;
	int err;

	/* If the migration got interrupted, we have the vchunk
	 * we are working on stored in context.
	 */
	if (ctx->current_vchunk)
		vchunk = ctx->current_vchunk;
	else
		vchunk = list_first_entry(&vregion->vchunk_list,
					  typeof(*vchunk), list);

	list_for_each_entry_from(vchunk, &vregion->vchunk_list, list) {
		err = mlxsw_sp_acl_tcam_vchunk_migrate_one(mlxsw_sp, vchunk,
							   vregion->region,
							   ctx, credits);
		if (err || *credits < 0)
			return err;
	}
	return 0;
}

static int
mlxsw_sp_acl_tcam_vregion_migrate(struct mlxsw_sp *mlxsw_sp,
				  struct mlxsw_sp_acl_tcam_vregion *vregion,
				  struct mlxsw_sp_acl_tcam_rehash_ctx *ctx,
				  int *credits)
{
	int err, err2;

	trace_mlxsw_sp_acl_tcam_vregion_migrate(mlxsw_sp, vregion);
	mutex_lock(&vregion->lock);
	err = mlxsw_sp_acl_tcam_vchunk_migrate_all(mlxsw_sp, vregion,
						   ctx, credits);
	if (err) {
		/* In case migration was not successful, we need to swap
		 * so the original region pointer is assigned again
		 * to vregion->region.
		 */
		swap(vregion->region, vregion->region2);
		ctx->current_vchunk = NULL;
		ctx->this_is_rollback = true;
		err2 = mlxsw_sp_acl_tcam_vchunk_migrate_all(mlxsw_sp, vregion,
							    ctx, credits);
		if (err2) {
			trace_mlxsw_sp_acl_tcam_vregion_rehash_rollback_failed(mlxsw_sp,
									       vregion);
			dev_err(mlxsw_sp->bus_info->dev, "Failed to rollback during vregion migration fail\n");
			/* Let the rollback to be continued later on. */
		}
	}
	mutex_unlock(&vregion->lock);
	trace_mlxsw_sp_acl_tcam_vregion_migrate_end(mlxsw_sp, vregion);
	return err;
}

static bool
mlxsw_sp_acl_tcam_vregion_rehash_in_progress(const struct mlxsw_sp_acl_tcam_rehash_ctx *ctx)
{
	return ctx->hints_priv;
}

static int
mlxsw_sp_acl_tcam_vregion_rehash_start(struct mlxsw_sp *mlxsw_sp,
				       struct mlxsw_sp_acl_tcam_vregion *vregion,
				       struct mlxsw_sp_acl_tcam_rehash_ctx *ctx)
{
	const struct mlxsw_sp_acl_tcam_ops *ops = mlxsw_sp->acl_tcam_ops;
	unsigned int priority = mlxsw_sp_acl_tcam_vregion_prio(vregion);
	struct mlxsw_sp_acl_tcam_region *new_region;
	void *hints_priv;
	int err;

	trace_mlxsw_sp_acl_tcam_vregion_rehash(mlxsw_sp, vregion);

	hints_priv = ops->region_rehash_hints_get(vregion->region->priv);
	if (IS_ERR(hints_priv))
		return PTR_ERR(hints_priv);

	new_region = mlxsw_sp_acl_tcam_region_create(mlxsw_sp, vregion->tcam,
						     vregion, hints_priv);
	if (IS_ERR(new_region)) {
		err = PTR_ERR(new_region);
		goto err_region_create;
	}

	/* vregion->region contains the pointer to the new region
	 * we are going to migrate to.
	 */
	vregion->region2 = vregion->region;
	vregion->region = new_region;
	err = mlxsw_sp_acl_tcam_group_region_attach(mlxsw_sp,
						    vregion->region2->group,
						    new_region, priority,
						    vregion->region2);
	if (err)
		goto err_group_region_attach;

	ctx->hints_priv = hints_priv;
	ctx->this_is_rollback = false;

	return 0;

err_group_region_attach:
	vregion->region = vregion->region2;
	vregion->region2 = NULL;
	mlxsw_sp_acl_tcam_region_destroy(mlxsw_sp, new_region);
err_region_create:
	ops->region_rehash_hints_put(hints_priv);
	return err;
}

static void
mlxsw_sp_acl_tcam_vregion_rehash_end(struct mlxsw_sp *mlxsw_sp,
				     struct mlxsw_sp_acl_tcam_vregion *vregion,
				     struct mlxsw_sp_acl_tcam_rehash_ctx *ctx)
{
	struct mlxsw_sp_acl_tcam_region *unused_region = vregion->region2;
	const struct mlxsw_sp_acl_tcam_ops *ops = mlxsw_sp->acl_tcam_ops;

	vregion->region2 = NULL;
	mlxsw_sp_acl_tcam_group_region_detach(mlxsw_sp, unused_region);
	mlxsw_sp_acl_tcam_region_destroy(mlxsw_sp, unused_region);
	ops->region_rehash_hints_put(ctx->hints_priv);
	ctx->hints_priv = NULL;
}

static void
mlxsw_sp_acl_tcam_vregion_rehash(struct mlxsw_sp *mlxsw_sp,
				 struct mlxsw_sp_acl_tcam_vregion *vregion,
				 int *credits)
{
	struct mlxsw_sp_acl_tcam_rehash_ctx *ctx = &vregion->rehash.ctx;
	int err;

	/* Check if the previous rehash work was interrupted
	 * which means we have to continue it now.
	 * If not, start a new rehash.
	 */
	if (!mlxsw_sp_acl_tcam_vregion_rehash_in_progress(ctx)) {
		err = mlxsw_sp_acl_tcam_vregion_rehash_start(mlxsw_sp,
							     vregion, ctx);
		if (err) {
			if (err != -EAGAIN)
				dev_err(mlxsw_sp->bus_info->dev, "Failed get rehash hints\n");
			return;
		}
	}

	err = mlxsw_sp_acl_tcam_vregion_migrate(mlxsw_sp, vregion,
						ctx, credits);
	if (err) {
		dev_err(mlxsw_sp->bus_info->dev, "Failed to migrate vregion\n");
	}

	if (*credits >= 0)
		mlxsw_sp_acl_tcam_vregion_rehash_end(mlxsw_sp, vregion, ctx);
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
	struct mlxsw_sp_acl_tcam_vgroup vgroup;
};

struct mlxsw_sp_acl_tcam_flower_rule {
	struct mlxsw_sp_acl_tcam_ventry ventry;
};

static int
mlxsw_sp_acl_tcam_flower_ruleset_add(struct mlxsw_sp *mlxsw_sp,
				     struct mlxsw_sp_acl_tcam *tcam,
				     void *ruleset_priv,
				     struct mlxsw_afk_element_usage *tmplt_elusage,
				     unsigned int *p_min_prio,
				     unsigned int *p_max_prio)
{
	struct mlxsw_sp_acl_tcam_flower_ruleset *ruleset = ruleset_priv;

	return mlxsw_sp_acl_tcam_vgroup_add(mlxsw_sp, tcam, &ruleset->vgroup,
					    mlxsw_sp_acl_tcam_patterns,
					    MLXSW_SP_ACL_TCAM_PATTERNS_COUNT,
					    tmplt_elusage, true,
					    p_min_prio, p_max_prio);
}

static void
mlxsw_sp_acl_tcam_flower_ruleset_del(struct mlxsw_sp *mlxsw_sp,
				     void *ruleset_priv)
{
	struct mlxsw_sp_acl_tcam_flower_ruleset *ruleset = ruleset_priv;

	mlxsw_sp_acl_tcam_vgroup_del(&ruleset->vgroup);
}

static int
mlxsw_sp_acl_tcam_flower_ruleset_bind(struct mlxsw_sp *mlxsw_sp,
				      void *ruleset_priv,
				      struct mlxsw_sp_port *mlxsw_sp_port,
				      bool ingress)
{
	struct mlxsw_sp_acl_tcam_flower_ruleset *ruleset = ruleset_priv;

	return mlxsw_sp_acl_tcam_group_bind(mlxsw_sp, &ruleset->vgroup.group,
					    mlxsw_sp_port, ingress);
}

static void
mlxsw_sp_acl_tcam_flower_ruleset_unbind(struct mlxsw_sp *mlxsw_sp,
					void *ruleset_priv,
					struct mlxsw_sp_port *mlxsw_sp_port,
					bool ingress)
{
	struct mlxsw_sp_acl_tcam_flower_ruleset *ruleset = ruleset_priv;

	mlxsw_sp_acl_tcam_group_unbind(mlxsw_sp, &ruleset->vgroup.group,
				       mlxsw_sp_port, ingress);
}

static u16
mlxsw_sp_acl_tcam_flower_ruleset_group_id(void *ruleset_priv)
{
	struct mlxsw_sp_acl_tcam_flower_ruleset *ruleset = ruleset_priv;

	return mlxsw_sp_acl_tcam_group_id(&ruleset->vgroup.group);
}

static int
mlxsw_sp_acl_tcam_flower_rule_add(struct mlxsw_sp *mlxsw_sp,
				  void *ruleset_priv, void *rule_priv,
				  struct mlxsw_sp_acl_rule_info *rulei)
{
	struct mlxsw_sp_acl_tcam_flower_ruleset *ruleset = ruleset_priv;
	struct mlxsw_sp_acl_tcam_flower_rule *rule = rule_priv;

	return mlxsw_sp_acl_tcam_ventry_add(mlxsw_sp, &ruleset->vgroup,
					    &rule->ventry, rulei);
}

static void
mlxsw_sp_acl_tcam_flower_rule_del(struct mlxsw_sp *mlxsw_sp, void *rule_priv)
{
	struct mlxsw_sp_acl_tcam_flower_rule *rule = rule_priv;

	mlxsw_sp_acl_tcam_ventry_del(mlxsw_sp, &rule->ventry);
}

static int
mlxsw_sp_acl_tcam_flower_rule_action_replace(struct mlxsw_sp *mlxsw_sp,
					     void *rule_priv,
					     struct mlxsw_sp_acl_rule_info *rulei)
{
	return -EOPNOTSUPP;
}

static int
mlxsw_sp_acl_tcam_flower_rule_activity_get(struct mlxsw_sp *mlxsw_sp,
					   void *rule_priv, bool *activity)
{
	struct mlxsw_sp_acl_tcam_flower_rule *rule = rule_priv;

	return mlxsw_sp_acl_tcam_ventry_activity_get(mlxsw_sp, &rule->ventry,
						     activity);
}

static const struct mlxsw_sp_acl_profile_ops mlxsw_sp_acl_tcam_flower_ops = {
	.ruleset_priv_size	= sizeof(struct mlxsw_sp_acl_tcam_flower_ruleset),
	.ruleset_add		= mlxsw_sp_acl_tcam_flower_ruleset_add,
	.ruleset_del		= mlxsw_sp_acl_tcam_flower_ruleset_del,
	.ruleset_bind		= mlxsw_sp_acl_tcam_flower_ruleset_bind,
	.ruleset_unbind		= mlxsw_sp_acl_tcam_flower_ruleset_unbind,
	.ruleset_group_id	= mlxsw_sp_acl_tcam_flower_ruleset_group_id,
	.rule_priv_size		= sizeof(struct mlxsw_sp_acl_tcam_flower_rule),
	.rule_add		= mlxsw_sp_acl_tcam_flower_rule_add,
	.rule_del		= mlxsw_sp_acl_tcam_flower_rule_del,
	.rule_action_replace	= mlxsw_sp_acl_tcam_flower_rule_action_replace,
	.rule_activity_get	= mlxsw_sp_acl_tcam_flower_rule_activity_get,
};

struct mlxsw_sp_acl_tcam_mr_ruleset {
	struct mlxsw_sp_acl_tcam_vchunk *vchunk;
	struct mlxsw_sp_acl_tcam_vgroup vgroup;
};

struct mlxsw_sp_acl_tcam_mr_rule {
	struct mlxsw_sp_acl_tcam_ventry ventry;
};

static int
mlxsw_sp_acl_tcam_mr_ruleset_add(struct mlxsw_sp *mlxsw_sp,
				 struct mlxsw_sp_acl_tcam *tcam,
				 void *ruleset_priv,
				 struct mlxsw_afk_element_usage *tmplt_elusage,
				 unsigned int *p_min_prio,
				 unsigned int *p_max_prio)
{
	struct mlxsw_sp_acl_tcam_mr_ruleset *ruleset = ruleset_priv;
	int err;

	err = mlxsw_sp_acl_tcam_vgroup_add(mlxsw_sp, tcam, &ruleset->vgroup,
					   mlxsw_sp_acl_tcam_patterns,
					   MLXSW_SP_ACL_TCAM_PATTERNS_COUNT,
					   tmplt_elusage, false,
					   p_min_prio, p_max_prio);
	if (err)
		return err;

	/* For most of the TCAM clients it would make sense to take a tcam chunk
	 * only when the first rule is written. This is not the case for
	 * multicast router as it is required to bind the multicast router to a
	 * specific ACL Group ID which must exist in HW before multicast router
	 * is initialized.
	 */
	ruleset->vchunk = mlxsw_sp_acl_tcam_vchunk_get(mlxsw_sp,
						       &ruleset->vgroup, 1,
						       tmplt_elusage);
	if (IS_ERR(ruleset->vchunk)) {
		err = PTR_ERR(ruleset->vchunk);
		goto err_chunk_get;
	}

	return 0;

err_chunk_get:
	mlxsw_sp_acl_tcam_vgroup_del(&ruleset->vgroup);
	return err;
}

static void
mlxsw_sp_acl_tcam_mr_ruleset_del(struct mlxsw_sp *mlxsw_sp, void *ruleset_priv)
{
	struct mlxsw_sp_acl_tcam_mr_ruleset *ruleset = ruleset_priv;

	mlxsw_sp_acl_tcam_vchunk_put(mlxsw_sp, ruleset->vchunk);
	mlxsw_sp_acl_tcam_vgroup_del(&ruleset->vgroup);
}

static int
mlxsw_sp_acl_tcam_mr_ruleset_bind(struct mlxsw_sp *mlxsw_sp, void *ruleset_priv,
				  struct mlxsw_sp_port *mlxsw_sp_port,
				  bool ingress)
{
	/* Binding is done when initializing multicast router */
	return 0;
}

static void
mlxsw_sp_acl_tcam_mr_ruleset_unbind(struct mlxsw_sp *mlxsw_sp,
				    void *ruleset_priv,
				    struct mlxsw_sp_port *mlxsw_sp_port,
				    bool ingress)
{
}

static u16
mlxsw_sp_acl_tcam_mr_ruleset_group_id(void *ruleset_priv)
{
	struct mlxsw_sp_acl_tcam_mr_ruleset *ruleset = ruleset_priv;

	return mlxsw_sp_acl_tcam_group_id(&ruleset->vgroup.group);
}

static int
mlxsw_sp_acl_tcam_mr_rule_add(struct mlxsw_sp *mlxsw_sp, void *ruleset_priv,
			      void *rule_priv,
			      struct mlxsw_sp_acl_rule_info *rulei)
{
	struct mlxsw_sp_acl_tcam_mr_ruleset *ruleset = ruleset_priv;
	struct mlxsw_sp_acl_tcam_mr_rule *rule = rule_priv;

	return mlxsw_sp_acl_tcam_ventry_add(mlxsw_sp, &ruleset->vgroup,
					   &rule->ventry, rulei);
}

static void
mlxsw_sp_acl_tcam_mr_rule_del(struct mlxsw_sp *mlxsw_sp, void *rule_priv)
{
	struct mlxsw_sp_acl_tcam_mr_rule *rule = rule_priv;

	mlxsw_sp_acl_tcam_ventry_del(mlxsw_sp, &rule->ventry);
}

static int
mlxsw_sp_acl_tcam_mr_rule_action_replace(struct mlxsw_sp *mlxsw_sp,
					 void *rule_priv,
					 struct mlxsw_sp_acl_rule_info *rulei)
{
	struct mlxsw_sp_acl_tcam_mr_rule *rule = rule_priv;

	return mlxsw_sp_acl_tcam_ventry_action_replace(mlxsw_sp, &rule->ventry,
						       rulei);
}

static int
mlxsw_sp_acl_tcam_mr_rule_activity_get(struct mlxsw_sp *mlxsw_sp,
				       void *rule_priv, bool *activity)
{
	struct mlxsw_sp_acl_tcam_mr_rule *rule = rule_priv;

	return mlxsw_sp_acl_tcam_ventry_activity_get(mlxsw_sp, &rule->ventry,
						     activity);
}

static const struct mlxsw_sp_acl_profile_ops mlxsw_sp_acl_tcam_mr_ops = {
	.ruleset_priv_size	= sizeof(struct mlxsw_sp_acl_tcam_mr_ruleset),
	.ruleset_add		= mlxsw_sp_acl_tcam_mr_ruleset_add,
	.ruleset_del		= mlxsw_sp_acl_tcam_mr_ruleset_del,
	.ruleset_bind		= mlxsw_sp_acl_tcam_mr_ruleset_bind,
	.ruleset_unbind		= mlxsw_sp_acl_tcam_mr_ruleset_unbind,
	.ruleset_group_id	= mlxsw_sp_acl_tcam_mr_ruleset_group_id,
	.rule_priv_size		= sizeof(struct mlxsw_sp_acl_tcam_mr_rule),
	.rule_add		= mlxsw_sp_acl_tcam_mr_rule_add,
	.rule_del		= mlxsw_sp_acl_tcam_mr_rule_del,
	.rule_action_replace	= mlxsw_sp_acl_tcam_mr_rule_action_replace,
	.rule_activity_get	= mlxsw_sp_acl_tcam_mr_rule_activity_get,
};

static const struct mlxsw_sp_acl_profile_ops *
mlxsw_sp_acl_tcam_profile_ops_arr[] = {
	[MLXSW_SP_ACL_PROFILE_FLOWER] = &mlxsw_sp_acl_tcam_flower_ops,
	[MLXSW_SP_ACL_PROFILE_MR] = &mlxsw_sp_acl_tcam_mr_ops,
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
