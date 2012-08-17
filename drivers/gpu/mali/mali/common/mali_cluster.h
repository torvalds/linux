/*
 * Copyright (C) 2011-2012 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 *
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#ifndef __MALI_CLUSTER_H__
#define __MALI_CLUSTER_H__

#include "mali_osk.h"
#include "mali_l2_cache.h"

/* Maximum 1 GP and 4 PP for a cluster (Mali-400 Quad-core) */
#define MALI_MAX_NUMBER_OF_GROUPS_PER_CLUSTER 5
#define MALI_MAX_NUMBER_OF_CLUSTERS           3

struct mali_cluster;
struct mali_group;

struct mali_cluster *mali_cluster_create(struct mali_l2_cache_core *l2_cache);
void mali_cluster_add_group(struct mali_cluster *cluster, struct mali_group *group);
void mali_cluster_delete(struct mali_cluster *cluster);

void mali_cluster_power_is_enabled_set(struct mali_cluster * cluster, mali_bool power_is_enabled);
mali_bool mali_cluster_power_is_enabled_get(struct mali_cluster * cluster);

void mali_cluster_reset(struct mali_cluster *cluster);

struct mali_l2_cache_core* mali_cluster_get_l2_cache_core(struct mali_cluster *cluster);
struct mali_group *mali_cluster_get_group(struct mali_cluster *cluster, u32 index);

struct mali_cluster *mali_cluster_get_global_cluster(u32 index);
u32 mali_cluster_get_glob_num_clusters(void);

/*  Returns MALI_TRUE if it did the flush */
mali_bool mali_cluster_l2_cache_invalidate_all(struct mali_cluster *cluster, u32 id);
void mali_cluster_l2_cache_invalidate_all_force(struct mali_cluster *cluster);
void mali_cluster_invalidate_pages(u32 *pages, u32 num_pages);

#endif /* __MALI_CLUSTER_H__ */
