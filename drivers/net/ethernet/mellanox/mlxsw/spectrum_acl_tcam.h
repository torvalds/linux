/* SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0 */
/* Copyright (c) 2017-2018 Mellanox Technologies. All rights reserved */

#ifndef _MLXSW_SPECTRUM_ACL_TCAM_H
#define _MLXSW_SPECTRUM_ACL_TCAM_H

#include <linux/list.h>
#include <linux/parman.h>

#include "reg.h"
#include "spectrum.h"
#include "core_acl_flex_keys.h"

struct mlxsw_sp_acl_tcam {
	unsigned long *used_regions; /* bit array */
	unsigned int max_regions;
	unsigned long *used_groups;  /* bit array */
	unsigned int max_groups;
	unsigned int max_group_size;
	unsigned long priv[0];
	/* priv has to be always the last item */
};

size_t mlxsw_sp_acl_tcam_priv_size(struct mlxsw_sp *mlxsw_sp);
int mlxsw_sp_acl_tcam_init(struct mlxsw_sp *mlxsw_sp,
			   struct mlxsw_sp_acl_tcam *tcam);
void mlxsw_sp_acl_tcam_fini(struct mlxsw_sp *mlxsw_sp,
			    struct mlxsw_sp_acl_tcam *tcam);
int mlxsw_sp_acl_tcam_priority_get(struct mlxsw_sp *mlxsw_sp,
				   struct mlxsw_sp_acl_rule_info *rulei,
				   u32 *priority, bool fillup_priority);

struct mlxsw_sp_acl_profile_ops {
	size_t ruleset_priv_size;
	int (*ruleset_add)(struct mlxsw_sp *mlxsw_sp,
			   struct mlxsw_sp_acl_tcam *tcam, void *ruleset_priv,
			   struct mlxsw_afk_element_usage *tmplt_elusage);
	void (*ruleset_del)(struct mlxsw_sp *mlxsw_sp, void *ruleset_priv);
	int (*ruleset_bind)(struct mlxsw_sp *mlxsw_sp, void *ruleset_priv,
			    struct mlxsw_sp_port *mlxsw_sp_port,
			    bool ingress);
	void (*ruleset_unbind)(struct mlxsw_sp *mlxsw_sp, void *ruleset_priv,
			       struct mlxsw_sp_port *mlxsw_sp_port,
			       bool ingress);
	u16 (*ruleset_group_id)(void *ruleset_priv);
	size_t (*rule_priv_size)(struct mlxsw_sp *mlxsw_sp);
	int (*rule_add)(struct mlxsw_sp *mlxsw_sp,
			void *ruleset_priv, void *rule_priv,
			struct mlxsw_sp_acl_rule_info *rulei);
	void (*rule_del)(struct mlxsw_sp *mlxsw_sp, void *rule_priv);
	int (*rule_action_replace)(struct mlxsw_sp *mlxsw_sp, void *rule_priv,
				   struct mlxsw_sp_acl_rule_info *rulei);
	int (*rule_activity_get)(struct mlxsw_sp *mlxsw_sp, void *rule_priv,
				 bool *activity);
};

const struct mlxsw_sp_acl_profile_ops *
mlxsw_sp_acl_tcam_profile_ops(struct mlxsw_sp *mlxsw_sp,
			      enum mlxsw_sp_acl_profile profile);

#define MLXSW_SP_ACL_TCAM_REGION_BASE_COUNT 16
#define MLXSW_SP_ACL_TCAM_REGION_RESIZE_STEP 16

#define MLXSW_SP_ACL_TCAM_CATCHALL_PRIO (~0U)

#define MLXSW_SP_ACL_TCAM_MASK_LEN \
	(MLXSW_REG_PTCEX_FLEX_KEY_BLOCKS_LEN * BITS_PER_BYTE)

struct mlxsw_sp_acl_tcam_group;

struct mlxsw_sp_acl_tcam_region {
	struct list_head list; /* Member of a TCAM group */
	struct list_head chunk_list; /* List of chunks under this region */
	struct mlxsw_sp_acl_tcam_group *group;
	enum mlxsw_reg_ptar_key_type key_type;
	u16 id; /* ACL ID and region ID - they are same */
	char tcam_region_info[MLXSW_REG_PXXX_TCAM_REGION_INFO_LEN];
	struct mlxsw_afk_key_info *key_info;
	struct mlxsw_sp *mlxsw_sp;
	unsigned long priv[0];
	/* priv has to be always the last item */
};

struct mlxsw_sp_acl_ctcam_region {
	struct parman *parman;
	const struct mlxsw_sp_acl_ctcam_region_ops *ops;
	struct mlxsw_sp_acl_tcam_region *region;
};

struct mlxsw_sp_acl_ctcam_chunk {
	struct parman_prio parman_prio;
};

struct mlxsw_sp_acl_ctcam_entry {
	struct parman_item parman_item;
};

struct mlxsw_sp_acl_ctcam_region_ops {
	int (*entry_insert)(struct mlxsw_sp_acl_ctcam_region *cregion,
			    struct mlxsw_sp_acl_ctcam_entry *centry,
			    const char *mask);
	void (*entry_remove)(struct mlxsw_sp_acl_ctcam_region *cregion,
			     struct mlxsw_sp_acl_ctcam_entry *centry);
};

int
mlxsw_sp_acl_ctcam_region_init(struct mlxsw_sp *mlxsw_sp,
			       struct mlxsw_sp_acl_ctcam_region *cregion,
			       struct mlxsw_sp_acl_tcam_region *region,
			       const struct mlxsw_sp_acl_ctcam_region_ops *ops);
void mlxsw_sp_acl_ctcam_region_fini(struct mlxsw_sp_acl_ctcam_region *cregion);
void mlxsw_sp_acl_ctcam_chunk_init(struct mlxsw_sp_acl_ctcam_region *cregion,
				   struct mlxsw_sp_acl_ctcam_chunk *cchunk,
				   unsigned int priority);
void mlxsw_sp_acl_ctcam_chunk_fini(struct mlxsw_sp_acl_ctcam_chunk *cchunk);
int mlxsw_sp_acl_ctcam_entry_add(struct mlxsw_sp *mlxsw_sp,
				 struct mlxsw_sp_acl_ctcam_region *cregion,
				 struct mlxsw_sp_acl_ctcam_chunk *cchunk,
				 struct mlxsw_sp_acl_ctcam_entry *centry,
				 struct mlxsw_sp_acl_rule_info *rulei,
				 bool fillup_priority);
void mlxsw_sp_acl_ctcam_entry_del(struct mlxsw_sp *mlxsw_sp,
				  struct mlxsw_sp_acl_ctcam_region *cregion,
				  struct mlxsw_sp_acl_ctcam_chunk *cchunk,
				  struct mlxsw_sp_acl_ctcam_entry *centry);
int mlxsw_sp_acl_ctcam_entry_action_replace(struct mlxsw_sp *mlxsw_sp,
					    struct mlxsw_sp_acl_ctcam_region *cregion,
					    struct mlxsw_sp_acl_ctcam_entry *centry,
					    struct mlxsw_sp_acl_rule_info *rulei);
static inline unsigned int
mlxsw_sp_acl_ctcam_entry_offset(struct mlxsw_sp_acl_ctcam_entry *centry)
{
	return centry->parman_item.index;
}

enum mlxsw_sp_acl_atcam_region_type {
	MLXSW_SP_ACL_ATCAM_REGION_TYPE_2KB,
	MLXSW_SP_ACL_ATCAM_REGION_TYPE_4KB,
	MLXSW_SP_ACL_ATCAM_REGION_TYPE_8KB,
	MLXSW_SP_ACL_ATCAM_REGION_TYPE_12KB,
	__MLXSW_SP_ACL_ATCAM_REGION_TYPE_MAX,
};

#define MLXSW_SP_ACL_ATCAM_REGION_TYPE_MAX \
	(__MLXSW_SP_ACL_ATCAM_REGION_TYPE_MAX - 1)

struct mlxsw_sp_acl_atcam {
	struct mlxsw_sp_acl_erp_core *erp_core;
};

struct mlxsw_sp_acl_atcam_region {
	struct rhashtable entries_ht; /* A-TCAM only */
	struct list_head entries_list; /* A-TCAM only */
	struct mlxsw_sp_acl_ctcam_region cregion;
	const struct mlxsw_sp_acl_atcam_region_ops *ops;
	struct mlxsw_sp_acl_tcam_region *region;
	struct mlxsw_sp_acl_atcam *atcam;
	enum mlxsw_sp_acl_atcam_region_type type;
	struct mlxsw_sp_acl_erp_table *erp_table;
	void *priv;
};

struct mlxsw_sp_acl_atcam_entry_ht_key {
	char full_enc_key[MLXSW_REG_PTCEX_FLEX_KEY_BLOCKS_LEN]; /* Encoded
								 * key.
								 */
	u8 erp_id;
};

struct mlxsw_sp_acl_atcam_chunk {
	struct mlxsw_sp_acl_ctcam_chunk cchunk;
};

struct mlxsw_sp_acl_atcam_entry {
	struct rhash_head ht_node;
	struct list_head list; /* Member in entries_list */
	struct mlxsw_sp_acl_atcam_entry_ht_key ht_key;
	char enc_key[MLXSW_REG_PTCEX_FLEX_KEY_BLOCKS_LEN]; /* Encoded key,
							    * minus delta bits.
							    */
	struct {
		u16 start;
		u8 mask;
		u8 value;
	} delta_info;
	struct mlxsw_sp_acl_ctcam_entry centry;
	struct mlxsw_sp_acl_atcam_lkey_id *lkey_id;
	struct mlxsw_sp_acl_erp_mask *erp_mask;
};

static inline struct mlxsw_sp_acl_atcam_region *
mlxsw_sp_acl_tcam_cregion_aregion(struct mlxsw_sp_acl_ctcam_region *cregion)
{
	return container_of(cregion, struct mlxsw_sp_acl_atcam_region, cregion);
}

static inline struct mlxsw_sp_acl_atcam_entry *
mlxsw_sp_acl_tcam_centry_aentry(struct mlxsw_sp_acl_ctcam_entry *centry)
{
	return container_of(centry, struct mlxsw_sp_acl_atcam_entry, centry);
}

int mlxsw_sp_acl_atcam_region_associate(struct mlxsw_sp *mlxsw_sp,
					u16 region_id);
int
mlxsw_sp_acl_atcam_region_init(struct mlxsw_sp *mlxsw_sp,
			       struct mlxsw_sp_acl_atcam *atcam,
			       struct mlxsw_sp_acl_atcam_region *aregion,
			       struct mlxsw_sp_acl_tcam_region *region,
			       const struct mlxsw_sp_acl_ctcam_region_ops *ops);
void mlxsw_sp_acl_atcam_region_fini(struct mlxsw_sp_acl_atcam_region *aregion);
void mlxsw_sp_acl_atcam_chunk_init(struct mlxsw_sp_acl_atcam_region *aregion,
				   struct mlxsw_sp_acl_atcam_chunk *achunk,
				   unsigned int priority);
void mlxsw_sp_acl_atcam_chunk_fini(struct mlxsw_sp_acl_atcam_chunk *achunk);
int mlxsw_sp_acl_atcam_entry_add(struct mlxsw_sp *mlxsw_sp,
				 struct mlxsw_sp_acl_atcam_region *aregion,
				 struct mlxsw_sp_acl_atcam_chunk *achunk,
				 struct mlxsw_sp_acl_atcam_entry *aentry,
				 struct mlxsw_sp_acl_rule_info *rulei);
void mlxsw_sp_acl_atcam_entry_del(struct mlxsw_sp *mlxsw_sp,
				  struct mlxsw_sp_acl_atcam_region *aregion,
				  struct mlxsw_sp_acl_atcam_chunk *achunk,
				  struct mlxsw_sp_acl_atcam_entry *aentry);
int mlxsw_sp_acl_atcam_entry_action_replace(struct mlxsw_sp *mlxsw_sp,
					    struct mlxsw_sp_acl_atcam_region *aregion,
					    struct mlxsw_sp_acl_atcam_entry *aentry,
					    struct mlxsw_sp_acl_rule_info *rulei);
int mlxsw_sp_acl_atcam_init(struct mlxsw_sp *mlxsw_sp,
			    struct mlxsw_sp_acl_atcam *atcam);
void mlxsw_sp_acl_atcam_fini(struct mlxsw_sp *mlxsw_sp,
			     struct mlxsw_sp_acl_atcam *atcam);

struct mlxsw_sp_acl_erp_delta;

u16 mlxsw_sp_acl_erp_delta_start(const struct mlxsw_sp_acl_erp_delta *delta);
u8 mlxsw_sp_acl_erp_delta_mask(const struct mlxsw_sp_acl_erp_delta *delta);
u8 mlxsw_sp_acl_erp_delta_value(const struct mlxsw_sp_acl_erp_delta *delta,
				const char *enc_key);
void mlxsw_sp_acl_erp_delta_clear(const struct mlxsw_sp_acl_erp_delta *delta,
				  const char *enc_key);

struct mlxsw_sp_acl_erp_mask;

bool
mlxsw_sp_acl_erp_mask_is_ctcam(const struct mlxsw_sp_acl_erp_mask *erp_mask);
u8 mlxsw_sp_acl_erp_mask_erp_id(const struct mlxsw_sp_acl_erp_mask *erp_mask);
const struct mlxsw_sp_acl_erp_delta *
mlxsw_sp_acl_erp_delta(const struct mlxsw_sp_acl_erp_mask *erp_mask);
struct mlxsw_sp_acl_erp_mask *
mlxsw_sp_acl_erp_mask_get(struct mlxsw_sp_acl_atcam_region *aregion,
			  const char *mask, bool ctcam);
void mlxsw_sp_acl_erp_mask_put(struct mlxsw_sp_acl_atcam_region *aregion,
			       struct mlxsw_sp_acl_erp_mask *erp_mask);
int mlxsw_sp_acl_erp_bf_insert(struct mlxsw_sp *mlxsw_sp,
			       struct mlxsw_sp_acl_atcam_region *aregion,
			       struct mlxsw_sp_acl_erp_mask *erp_mask,
			       struct mlxsw_sp_acl_atcam_entry *aentry);
void mlxsw_sp_acl_erp_bf_remove(struct mlxsw_sp *mlxsw_sp,
				struct mlxsw_sp_acl_atcam_region *aregion,
				struct mlxsw_sp_acl_erp_mask *erp_mask,
				struct mlxsw_sp_acl_atcam_entry *aentry);
int mlxsw_sp_acl_erp_region_init(struct mlxsw_sp_acl_atcam_region *aregion);
void mlxsw_sp_acl_erp_region_fini(struct mlxsw_sp_acl_atcam_region *aregion);
int mlxsw_sp_acl_erps_init(struct mlxsw_sp *mlxsw_sp,
			   struct mlxsw_sp_acl_atcam *atcam);
void mlxsw_sp_acl_erps_fini(struct mlxsw_sp *mlxsw_sp,
			    struct mlxsw_sp_acl_atcam *atcam);

struct mlxsw_sp_acl_bf;

int
mlxsw_sp_acl_bf_entry_add(struct mlxsw_sp *mlxsw_sp,
			  struct mlxsw_sp_acl_bf *bf,
			  struct mlxsw_sp_acl_atcam_region *aregion,
			  unsigned int erp_bank,
			  struct mlxsw_sp_acl_atcam_entry *aentry);
void
mlxsw_sp_acl_bf_entry_del(struct mlxsw_sp *mlxsw_sp,
			  struct mlxsw_sp_acl_bf *bf,
			  struct mlxsw_sp_acl_atcam_region *aregion,
			  unsigned int erp_bank,
			  struct mlxsw_sp_acl_atcam_entry *aentry);
struct mlxsw_sp_acl_bf *
mlxsw_sp_acl_bf_init(struct mlxsw_sp *mlxsw_sp, unsigned int num_erp_banks);
void mlxsw_sp_acl_bf_fini(struct mlxsw_sp_acl_bf *bf);

#endif
