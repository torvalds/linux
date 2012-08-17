/*
 * Copyright (C) 2011-2012 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 *
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include "mali_kernel_common.h"
#include "mali_cluster.h"
#include "mali_osk.h"
#include "mali_group.h"
#include "mali_l2_cache.h"
#include "mali_scheduler.h"

static struct mali_cluster *mali_global_clusters[MALI_MAX_NUMBER_OF_CLUSTERS] = { NULL, NULL, NULL };
static u32 mali_global_num_clusters = 0;

/**
 * The structure represents a render cluster
 * A render cluster is defined by all the cores that share the same Mali L2 cache
 */
struct mali_cluster
{
	struct mali_l2_cache_core *l2;
	u32 number_of_groups;
	struct mali_group* groups[MALI_MAX_NUMBER_OF_GROUPS_PER_CLUSTER];
	u32 last_invalidated_id;
	mali_bool power_is_enabled;
};

struct mali_cluster *mali_cluster_create(struct mali_l2_cache_core *l2_cache)
{
	struct mali_cluster *cluster = NULL;

	if (mali_global_num_clusters >= MALI_MAX_NUMBER_OF_CLUSTERS)
	{
		MALI_PRINT_ERROR(("Mali cluster: Too many cluster objects created\n"));
		return NULL;
	}

	cluster = _mali_osk_malloc(sizeof(struct mali_cluster));
	if (NULL != cluster)
	{
		_mali_osk_memset(cluster, 0, sizeof(struct mali_cluster));
		cluster->l2 = l2_cache; /* This cluster now owns this L2 cache object */
		cluster->last_invalidated_id = 0;
		cluster->power_is_enabled = MALI_TRUE;

		mali_global_clusters[mali_global_num_clusters] = cluster;
		mali_global_num_clusters++;

		return cluster;
	}

	return NULL;
}

void mali_cluster_power_is_enabled_set(struct mali_cluster * cluster, mali_bool power_is_enabled)
{
	cluster->power_is_enabled = power_is_enabled;
}

mali_bool mali_cluster_power_is_enabled_get(struct mali_cluster * cluster)
{
	return cluster->power_is_enabled;
}


void mali_cluster_add_group(struct mali_cluster *cluster, struct mali_group *group)
{
	MALI_DEBUG_ASSERT_POINTER(cluster);

	if (cluster->number_of_groups < MALI_MAX_NUMBER_OF_GROUPS_PER_CLUSTER)
	{
		/* This cluster now owns the group object */
		cluster->groups[cluster->number_of_groups] = group;
		cluster->number_of_groups++;
	}
}

void mali_cluster_delete(struct mali_cluster *cluster)
{
	u32 i;

	MALI_DEBUG_ASSERT_POINTER(cluster);

	/* Free all the resources we own */
	for (i = 0; i < cluster->number_of_groups; i++)
	{
		mali_group_delete(cluster->groups[i]);
	}

	if (NULL != cluster->l2)
	{
		mali_l2_cache_delete(cluster->l2);
	}

	for (i = 0; i < mali_global_num_clusters; i++)
	{
		if (mali_global_clusters[i] == cluster)
		{
			mali_global_clusters[i] = NULL;
			mali_global_num_clusters--;
			break;
		}
	}

	_mali_osk_free(cluster);
}

void mali_cluster_reset(struct mali_cluster *cluster)
{
	u32 i;

	MALI_DEBUG_ASSERT_POINTER(cluster);

	/* Free all the resources we own */
	for (i = 0; i < cluster->number_of_groups; i++)
	{
		struct mali_group *group = cluster->groups[i];

		mali_group_reset(group);
	}

	if (NULL != cluster->l2)
	{
		mali_l2_cache_reset(cluster->l2);
	}
}

struct mali_l2_cache_core* mali_cluster_get_l2_cache_core(struct mali_cluster *cluster)
{
	MALI_DEBUG_ASSERT_POINTER(cluster);
	return cluster->l2;
}

struct mali_group *mali_cluster_get_group(struct mali_cluster *cluster, u32 index)
{
	MALI_DEBUG_ASSERT_POINTER(cluster);

	if (index <  cluster->number_of_groups)
	{
		return cluster->groups[index];
	}

	return NULL;
}

struct mali_cluster *mali_cluster_get_global_cluster(u32 index)
{
	if (MALI_MAX_NUMBER_OF_CLUSTERS > index)
	{
		return mali_global_clusters[index];
	}

	return NULL;
}

u32 mali_cluster_get_glob_num_clusters(void)
{
	return mali_global_num_clusters;
}

mali_bool mali_cluster_l2_cache_invalidate_all(struct mali_cluster *cluster, u32 id)
{
	MALI_DEBUG_ASSERT_POINTER(cluster);

	if (NULL != cluster->l2)
	{
		/* If the last cache invalidation was done by a job with a higher id we
		 * don't have to flush. Since user space will store jobs w/ their
		 * corresponding memory in sequence (first job #0, then job #1, ...),
		 * we don't have to flush for job n-1 if job n has already invalidated
		 * the cache since we know for sure that job n-1's memory was already
		 * written when job n was started. */
		if (((s32)id) <= ((s32)cluster->last_invalidated_id))
		{
			return MALI_FALSE;
		}
		else
		{
			cluster->last_invalidated_id = mali_scheduler_get_new_id();
		}

		mali_l2_cache_invalidate_all(cluster->l2);
	}
	return MALI_TRUE;
}

void mali_cluster_l2_cache_invalidate_all_force(struct mali_cluster *cluster)
{
	MALI_DEBUG_ASSERT_POINTER(cluster);

	if (NULL != cluster->l2)
	{
		cluster->last_invalidated_id = mali_scheduler_get_new_id();
		mali_l2_cache_invalidate_all(cluster->l2);
	}
}

void mali_cluster_invalidate_pages(u32 *pages, u32 num_pages)
{
	u32 i;

	for (i = 0; i < mali_global_num_clusters; i++)
	{
		/*additional check for cluster*/
		if (MALI_TRUE == mali_l2_cache_lock_power_state(mali_global_clusters[i]->l2))
		{
			mali_l2_cache_invalidate_pages(mali_global_clusters[i]->l2, pages, num_pages);
		}
		mali_l2_cache_unlock_power_state(mali_global_clusters[i]->l2);
		/*check for failed power locking???*/
	}
}
