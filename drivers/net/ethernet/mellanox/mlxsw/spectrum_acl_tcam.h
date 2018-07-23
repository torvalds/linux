/*
 * drivers/net/ethernet/mellanox/mlxsw/spectrum_acl_tcam.h
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
	int (*rule_activity_get)(struct mlxsw_sp *mlxsw_sp, void *rule_priv,
				 bool *activity);
};

const struct mlxsw_sp_acl_profile_ops *
mlxsw_sp_acl_tcam_profile_ops(struct mlxsw_sp *mlxsw_sp,
			      enum mlxsw_sp_acl_profile profile);

#define MLXSW_SP_ACL_TCAM_REGION_BASE_COUNT 16
#define MLXSW_SP_ACL_TCAM_REGION_RESIZE_STEP 16

#define MLXSW_SP_ACL_TCAM_CATCHALL_PRIO (~0U)

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
	struct mlxsw_sp_acl_tcam_region *region;
};

struct mlxsw_sp_acl_ctcam_chunk {
	struct parman_prio parman_prio;
};

struct mlxsw_sp_acl_ctcam_entry {
	struct parman_item parman_item;
};

int mlxsw_sp_acl_ctcam_region_init(struct mlxsw_sp *mlxsw_sp,
				   struct mlxsw_sp_acl_ctcam_region *cregion,
				   struct mlxsw_sp_acl_tcam_region *region);
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
static inline unsigned int
mlxsw_sp_acl_ctcam_entry_offset(struct mlxsw_sp_acl_ctcam_entry *centry)
{
	return centry->parman_item.index;
}

int mlxsw_sp_acl_atcam_region_associate(struct mlxsw_sp *mlxsw_sp,
					u16 region_id);
int mlxsw_sp_acl_atcam_region_init(struct mlxsw_sp *mlxsw_sp,
				   struct mlxsw_sp_acl_tcam_region *region);

#endif
