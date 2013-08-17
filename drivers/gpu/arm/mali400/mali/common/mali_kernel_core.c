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
#include "mali_session.h"
#include "mali_osk.h"
#include "mali_osk_mali.h"
#include "mali_ukk.h"
#include "mali_kernel_core.h"
#include "mali_memory.h"
#include "mali_mem_validation.h"
#include "mali_mmu.h"
#include "mali_mmu_page_directory.h"
#include "mali_dlbu.h"
#include "mali_broadcast.h"
#include "mali_gp.h"
#include "mali_pp.h"
#include "mali_gp_scheduler.h"
#include "mali_pp_scheduler.h"
#include "mali_group.h"
#include "mali_pm.h"
#include "mali_pmu.h"
#include "mali_scheduler.h"
#include "mali_kernel_utilization.h"
#include "mali_l2_cache.h"
#if defined(CONFIG_MALI400_PROFILING)
#include "mali_osk_profiling.h"
#endif
#if defined(CONFIG_MALI400_INTERNAL_PROFILING)
#include "mali_profiling_internal.h"
#endif


/* Mali GPU memory. Real values come from module parameter or from device specific data */
int mali_dedicated_mem_start = 0;
int mali_dedicated_mem_size = 0;
int mali_shared_mem_size = 0;

/* Frame buffer memory to be accessible by Mali GPU */
int mali_fb_start = 0;
int mali_fb_size = 0;

/** Start profiling from module load? */
int mali_boot_profiling = 0;

/** Limits for the number of PP cores behind each L2 cache. */
int mali_max_pp_cores_group_1 = 0xFF;
int mali_max_pp_cores_group_2 = 0xFF;

int mali_inited_pp_cores_group_1 = 0;
int mali_inited_pp_cores_group_2 = 0;

static _mali_product_id_t global_product_id = _MALI_PRODUCT_ID_UNKNOWN;
static u32 global_gpu_base_address = 0;
static u32 global_gpu_major_version = 0;
static u32 global_gpu_minor_version = 0;

/* MALI_SEC */
static u32 first_pp_offset = 0;

#define HANG_CHECK_MSECS_DEFAULT 500 /* 500 ms */
#define WATCHDOG_MSECS_DEFAULT 4000 /* 4 s */

/* timer related */
int mali_max_job_runtime = WATCHDOG_MSECS_DEFAULT;
int mali_hang_check_interval = HANG_CHECK_MSECS_DEFAULT;

static _mali_osk_errcode_t mali_parse_product_info(void)
{
	/*
	 * Mali-200 has the PP core first, while Mali-300, Mali-400 and Mali-450 have the GP core first.
	 * Look at the version register for the first PP core in order to determine the GPU HW revision.
	 */

	u32 first_pp_offset;
	_mali_osk_resource_t first_pp_resource;

	global_gpu_base_address = _mali_osk_resource_base_address();
	if (0 == global_gpu_base_address)
	{
		return _MALI_OSK_ERR_ITEM_NOT_FOUND;
	}

	/* Find out where the first PP core is located */
	if (_MALI_OSK_ERR_OK == _mali_osk_resource_find(global_gpu_base_address + 0x8000, NULL))
	{
		/* Mali-300/400/450 */
		first_pp_offset = 0x8000;
	}
	else
	{
		/* Mali-200 */
		first_pp_offset = 0x0000;
	}

	/* Find the first PP core resource (again) */
	if (_MALI_OSK_ERR_OK == _mali_osk_resource_find(global_gpu_base_address + first_pp_offset, &first_pp_resource))
	{
		/* Create a dummy PP object for this core so that we can read the version register */
		struct mali_group *group = mali_group_create(NULL, NULL, NULL);
		if (NULL != group)
		{
			struct mali_pp_core *pp_core = mali_pp_create(&first_pp_resource, group, MALI_FALSE);
			if (NULL != pp_core)
			{
				u32 pp_version = mali_pp_core_get_version(pp_core);
				mali_group_delete(group);

				global_gpu_major_version = (pp_version >> 8) & 0xFF;
				global_gpu_minor_version = pp_version & 0xFF;

				switch (pp_version >> 16)
				{
					case MALI200_PP_PRODUCT_ID:
						global_product_id = _MALI_PRODUCT_ID_MALI200;
						MALI_DEBUG_PRINT(2, ("Found Mali GPU Mali-200 r%up%u\n", global_gpu_major_version, global_gpu_minor_version));
						break;
					case MALI300_PP_PRODUCT_ID:
						global_product_id = _MALI_PRODUCT_ID_MALI300;
						MALI_DEBUG_PRINT(2, ("Found Mali GPU Mali-300 r%up%u\n", global_gpu_major_version, global_gpu_minor_version));
						break;
					case MALI400_PP_PRODUCT_ID:
						global_product_id = _MALI_PRODUCT_ID_MALI400;
						MALI_DEBUG_PRINT(2, ("Found Mali GPU Mali-400 MP r%up%u\n", global_gpu_major_version, global_gpu_minor_version));
						break;
					case MALI450_PP_PRODUCT_ID:
						global_product_id = _MALI_PRODUCT_ID_MALI450;
						MALI_DEBUG_PRINT(2, ("Found Mali GPU Mali-450 MP r%up%u\n", global_gpu_major_version, global_gpu_minor_version));
						break;
					default:
						MALI_DEBUG_PRINT(2, ("Found unknown Mali GPU (r%up%u)\n", global_gpu_major_version, global_gpu_minor_version));
						return _MALI_OSK_ERR_FAULT;
				}

				return _MALI_OSK_ERR_OK;
			}
			else
			{
				MALI_PRINT_ERROR(("Failed to create initial PP object\n"));
			}
		}
		else
		{
			MALI_PRINT_ERROR(("Failed to create initial group object\n"));
		}
	}
	else
	{
		MALI_PRINT_ERROR(("First PP core not specified in config file\n"));
	}

	return _MALI_OSK_ERR_FAULT;
}


void mali_resource_count(u32 *pp_count, u32 *l2_count)
{
	*pp_count = 0;
	*l2_count = 0;

	if (_MALI_OSK_ERR_OK == _mali_osk_resource_find(global_gpu_base_address + 0x08000, NULL))
	{
		++(*pp_count);
	}
	if (_MALI_OSK_ERR_OK == _mali_osk_resource_find(global_gpu_base_address + 0x0A000, NULL))
	{
		++(*pp_count);
	}
	if (_MALI_OSK_ERR_OK == _mali_osk_resource_find(global_gpu_base_address + 0x0C000, NULL))
	{
		++(*pp_count);
	}
	if (_MALI_OSK_ERR_OK == _mali_osk_resource_find(global_gpu_base_address + 0x0E000, NULL))
	{
		++(*pp_count);
	}
	if (_MALI_OSK_ERR_OK == _mali_osk_resource_find(global_gpu_base_address + 0x28000, NULL))
	{
		++(*pp_count);
	}
	if (_MALI_OSK_ERR_OK == _mali_osk_resource_find(global_gpu_base_address + 0x2A000, NULL))
	{
		++(*pp_count);
	}
	if (_MALI_OSK_ERR_OK == _mali_osk_resource_find(global_gpu_base_address + 0x2C000, NULL))
	{
		++(*pp_count);
	}
	if (_MALI_OSK_ERR_OK == _mali_osk_resource_find(global_gpu_base_address + 0x2E000, NULL))
	{
		++(*pp_count);
	}

	if (_MALI_OSK_ERR_OK == _mali_osk_resource_find(global_gpu_base_address + 0x1000, NULL))
	{
		++(*l2_count);
	}
	if (_MALI_OSK_ERR_OK == _mali_osk_resource_find(global_gpu_base_address + 0x10000, NULL))
	{
		++(*l2_count);
	}
	if (_MALI_OSK_ERR_OK == _mali_osk_resource_find(global_gpu_base_address + 0x11000, NULL))
	{
		++(*l2_count);
	}
}

static void mali_delete_l2_cache_cores(void)
{
	u32 i;
	u32 number_of_l2_ccores = mali_l2_cache_core_get_glob_num_l2_cores();

	for (i = 0; i < number_of_l2_ccores; i++)
	{
		mali_l2_cache_delete(mali_l2_cache_core_get_glob_l2_core(i));
	}
}

static _mali_osk_errcode_t mali_create_l2_cache_core(_mali_osk_resource_t *resource)
{
	if (NULL != resource)
	{
		struct mali_l2_cache_core *l2_cache;

		MALI_DEBUG_PRINT(3, ("Found L2 cache %s\n", resource->description));

		l2_cache = mali_l2_cache_create(resource);
		if (NULL == l2_cache)
		{
			MALI_PRINT_ERROR(("Failed to create L2 cache object\n"));
			return _MALI_OSK_ERR_FAULT;
		}
	}
	MALI_DEBUG_PRINT(3, ("Created L2 cache core object\n"));

	return _MALI_OSK_ERR_OK;
}

static _mali_osk_errcode_t mali_parse_config_l2_cache(void)
{
	if (_MALI_PRODUCT_ID_MALI200 == global_product_id)
	{
		/* Create dummy L2 cache - nothing happens here!!! */
		return mali_create_l2_cache_core(NULL);
	}
	else if (_MALI_PRODUCT_ID_MALI300 == global_product_id || _MALI_PRODUCT_ID_MALI400 == global_product_id)
	{
		_mali_osk_resource_t l2_resource;
		if (_MALI_OSK_ERR_OK != _mali_osk_resource_find(global_gpu_base_address + 0x1000, &l2_resource))
		{
			MALI_DEBUG_PRINT(3, ("Did not find required Mali L2 cache in config file\n"));
			return _MALI_OSK_ERR_FAULT;
		}

		return mali_create_l2_cache_core(&l2_resource);
	}
	else if (_MALI_PRODUCT_ID_MALI450 == global_product_id)
	{
		/*
		 * L2 for GP    at 0x10000
		 * L2 for PP0-3 at 0x01000
		 * L2 for PP4-7 at 0x11000 (optional)
		 */

		_mali_osk_resource_t l2_gp_resource;
		_mali_osk_resource_t l2_pp_grp0_resource;
		_mali_osk_resource_t l2_pp_grp1_resource;

		/* Make cluster for GP's L2 */
		if (_MALI_OSK_ERR_OK == _mali_osk_resource_find(global_gpu_base_address + 0x10000, &l2_gp_resource))
		{
			_mali_osk_errcode_t ret;
			MALI_DEBUG_PRINT(3, ("Creating Mali-450 L2 cache core for GP\n"));
			ret = mali_create_l2_cache_core(&l2_gp_resource);
			if (_MALI_OSK_ERR_OK != ret)
			{
				return ret;
			}
		}
		else
		{
			MALI_DEBUG_PRINT(3, ("Did not find required Mali L2 cache for GP in config file\n"));
			return _MALI_OSK_ERR_FAULT;
		}

		/* Make cluster for first PP core group */
		if (_MALI_OSK_ERR_OK == _mali_osk_resource_find(global_gpu_base_address + 0x1000, &l2_pp_grp0_resource))
		{
			_mali_osk_errcode_t ret;
			MALI_DEBUG_PRINT(3, ("Creating Mali-450 L2 cache core for PP group 0\n"));
			ret = mali_create_l2_cache_core(&l2_pp_grp0_resource);
			if (_MALI_OSK_ERR_OK != ret)
			{
				return ret;
			}
		}
		else
		{
			MALI_DEBUG_PRINT(3, ("Did not find required Mali L2 cache for PP group 0 in config file\n"));
			return _MALI_OSK_ERR_FAULT;
		}

		/* Second PP core group is optional, don't fail if we don't find it */
		if (_MALI_OSK_ERR_OK == _mali_osk_resource_find(global_gpu_base_address + 0x11000, &l2_pp_grp1_resource))
		{
			_mali_osk_errcode_t ret;
			MALI_DEBUG_PRINT(3, ("Creating Mali-450 L2 cache core for PP group 1\n"));
			ret = mali_create_l2_cache_core(&l2_pp_grp1_resource);
			if (_MALI_OSK_ERR_OK != ret)
			{
				return ret;
			}
		}
	}

	return _MALI_OSK_ERR_OK;
}

static _mali_osk_errcode_t mali_create_group(struct mali_l2_cache_core *cache,
                                             _mali_osk_resource_t *resource_mmu,
                                             _mali_osk_resource_t *resource_gp,
                                             _mali_osk_resource_t *resource_pp)
{
	struct mali_mmu_core *mmu;
	struct mali_group *group;

	MALI_DEBUG_PRINT(3, ("Starting new group for MMU %s\n", resource_mmu->description));

	/* Create the group object */
	group = mali_group_create(cache, NULL, NULL);
	if (NULL == group)
	{
		MALI_PRINT_ERROR(("Failed to create group object for MMU %s\n", resource_mmu->description));
		return _MALI_OSK_ERR_FAULT;
	}

	/* Create the MMU object inside group */
	mmu = mali_mmu_create(resource_mmu, group, MALI_FALSE);
	if (NULL == mmu)
	{
		MALI_PRINT_ERROR(("Failed to create MMU object\n"));
		mali_group_delete(group);
		return _MALI_OSK_ERR_FAULT;
	}

	if (NULL != resource_gp)
	{
		/* Create the GP core object inside this group */
		struct mali_gp_core *gp_core = mali_gp_create(resource_gp, group);
		if (NULL == gp_core)
		{
			/* No need to clean up now, as we will clean up everything linked in from the cluster when we fail this function */
			MALI_PRINT_ERROR(("Failed to create GP object\n"));
			mali_group_delete(group);
			return _MALI_OSK_ERR_FAULT;
		}
	}

	if (NULL != resource_pp)
	{
		struct mali_pp_core *pp_core;

		/* Create the PP core object inside this group */
		pp_core = mali_pp_create(resource_pp, group, MALI_FALSE);
		if (NULL == pp_core)
		{
			/* No need to clean up now, as we will clean up everything linked in from the cluster when we fail this function */
			MALI_PRINT_ERROR(("Failed to create PP object\n"));
			mali_group_delete(group);
			return _MALI_OSK_ERR_FAULT;
		}
	}

	/* Reset group */
	mali_group_reset(group);

	return _MALI_OSK_ERR_OK;
}

static _mali_osk_errcode_t mali_create_virtual_group(_mali_osk_resource_t *resource_mmu_pp_bcast,
                                                    _mali_osk_resource_t *resource_pp_bcast,
                                                    _mali_osk_resource_t *resource_dlbu,
                                                    _mali_osk_resource_t *resource_bcast)
{
	struct mali_mmu_core *mmu_pp_bcast_core;
	struct mali_pp_core *pp_bcast_core;
	struct mali_dlbu_core *dlbu_core;
	struct mali_bcast_unit *bcast_core;
	struct mali_group *group;

	MALI_DEBUG_PRINT(2, ("Starting new virtual group for MMU PP broadcast core %s\n", resource_mmu_pp_bcast->description));

	/* Create the DLBU core object */
	dlbu_core = mali_dlbu_create(resource_dlbu);
	if (NULL == dlbu_core)
	{
		MALI_PRINT_ERROR(("Failed to create DLBU object \n"));
		return _MALI_OSK_ERR_FAULT;
	}

	/* Create the Broadcast unit core */
	bcast_core = mali_bcast_unit_create(resource_bcast);
	if (NULL == bcast_core)
	{
		MALI_PRINT_ERROR(("Failed to create Broadcast unit object!\n"));
		mali_dlbu_delete(dlbu_core);
		return _MALI_OSK_ERR_FAULT;
	}

	/* Create the group object */
	group = mali_group_create(NULL, dlbu_core, bcast_core);
	if (NULL == group)
	{
		MALI_PRINT_ERROR(("Failed to create group object for MMU PP broadcast core %s\n", resource_mmu_pp_bcast->description));
		mali_bcast_unit_delete(bcast_core);
		mali_dlbu_delete(dlbu_core);
		return _MALI_OSK_ERR_FAULT;
	}

	/* Create the MMU object inside group */
	mmu_pp_bcast_core = mali_mmu_create(resource_mmu_pp_bcast, group, MALI_TRUE);
	if (NULL == mmu_pp_bcast_core)
	{
		MALI_PRINT_ERROR(("Failed to create MMU PP broadcast object\n"));
		mali_group_delete(group);
		return _MALI_OSK_ERR_FAULT;
	}

	/* Create the PP core object inside this group */
	pp_bcast_core = mali_pp_create(resource_pp_bcast, group, MALI_TRUE);
	if (NULL == pp_bcast_core)
	{
		/* No need to clean up now, as we will clean up everything linked in from the cluster when we fail this function */
		MALI_PRINT_ERROR(("Failed to create PP object\n"));
		mali_group_delete(group);
		return _MALI_OSK_ERR_FAULT;
	}

	return _MALI_OSK_ERR_OK;
}

static _mali_osk_errcode_t mali_parse_config_groups(void)
{
	if (_MALI_PRODUCT_ID_MALI200 == global_product_id)
	{
		_mali_osk_errcode_t err;
		_mali_osk_resource_t resource_gp;
		_mali_osk_resource_t resource_pp;
		_mali_osk_resource_t resource_mmu;

		MALI_DEBUG_ASSERT(1 == mali_l2_cache_core_get_glob_num_l2_cores());

		if (_MALI_OSK_ERR_OK != _mali_osk_resource_find(global_gpu_base_address + 0x02000, &resource_gp) ||
		    _MALI_OSK_ERR_OK != _mali_osk_resource_find(global_gpu_base_address + 0x00000, &resource_pp) ||
		    _MALI_OSK_ERR_OK != _mali_osk_resource_find(global_gpu_base_address + 0x03000, &resource_mmu))
		{
			/* Missing mandatory core(s) */
			return _MALI_OSK_ERR_FAULT;
		}

		err = mali_create_group(mali_l2_cache_core_get_glob_l2_core(0), &resource_mmu, &resource_gp, &resource_pp);
		if (err == _MALI_OSK_ERR_OK)
		{
			mali_inited_pp_cores_group_1++;
			mali_max_pp_cores_group_1 = mali_inited_pp_cores_group_1; /* always 1 */
			mali_max_pp_cores_group_2 = mali_inited_pp_cores_group_2; /* always zero */
		}

		return err;
	}
	else if (_MALI_PRODUCT_ID_MALI300 == global_product_id ||
	         _MALI_PRODUCT_ID_MALI400 == global_product_id ||
	         _MALI_PRODUCT_ID_MALI450 == global_product_id)
	{
		_mali_osk_errcode_t err;
		int cluster_id_gp = 0;
		int cluster_id_pp_grp0 = 0;
		int cluster_id_pp_grp1 = 0;
		int i;

		_mali_osk_resource_t resource_gp;
		_mali_osk_resource_t resource_gp_mmu;
		_mali_osk_resource_t resource_pp[8];
		_mali_osk_resource_t resource_pp_mmu[8];
		_mali_osk_resource_t resource_pp_mmu_bcast;
		_mali_osk_resource_t resource_pp_bcast;
		_mali_osk_resource_t resource_dlbu;
		_mali_osk_resource_t resource_bcast;
		_mali_osk_errcode_t resource_gp_found;
		_mali_osk_errcode_t resource_gp_mmu_found;
		_mali_osk_errcode_t resource_pp_found[8];
		_mali_osk_errcode_t resource_pp_mmu_found[8];
		_mali_osk_errcode_t resource_pp_mmu_bcast_found;
		_mali_osk_errcode_t resource_pp_bcast_found;
		_mali_osk_errcode_t resource_dlbu_found;
		_mali_osk_errcode_t resource_bcast_found;

		if (_MALI_PRODUCT_ID_MALI450 == global_product_id)
		{
			/* Mali-450 have separate L2s for GP, and PP core group(s) */
			cluster_id_pp_grp0 = 1;
			cluster_id_pp_grp1 = 2;
		}

		resource_gp_found = _mali_osk_resource_find(global_gpu_base_address + 0x00000, &resource_gp);
		resource_gp_mmu_found = _mali_osk_resource_find(global_gpu_base_address + 0x03000, &resource_gp_mmu);
		resource_pp_found[0] = _mali_osk_resource_find(global_gpu_base_address + 0x08000, &(resource_pp[0]));
		resource_pp_found[1] = _mali_osk_resource_find(global_gpu_base_address + 0x0A000, &(resource_pp[1]));
		resource_pp_found[2] = _mali_osk_resource_find(global_gpu_base_address + 0x0C000, &(resource_pp[2]));
		resource_pp_found[3] = _mali_osk_resource_find(global_gpu_base_address + 0x0E000, &(resource_pp[3]));
		resource_pp_found[4] = _mali_osk_resource_find(global_gpu_base_address + 0x28000, &(resource_pp[4]));
		resource_pp_found[5] = _mali_osk_resource_find(global_gpu_base_address + 0x2A000, &(resource_pp[5]));
		resource_pp_found[6] = _mali_osk_resource_find(global_gpu_base_address + 0x2C000, &(resource_pp[6]));
		resource_pp_found[7] = _mali_osk_resource_find(global_gpu_base_address + 0x2E000, &(resource_pp[7]));
		resource_pp_mmu_found[0] = _mali_osk_resource_find(global_gpu_base_address + 0x04000, &(resource_pp_mmu[0]));
		resource_pp_mmu_found[1] = _mali_osk_resource_find(global_gpu_base_address + 0x05000, &(resource_pp_mmu[1]));
		resource_pp_mmu_found[2] = _mali_osk_resource_find(global_gpu_base_address + 0x06000, &(resource_pp_mmu[2]));
		resource_pp_mmu_found[3] = _mali_osk_resource_find(global_gpu_base_address + 0x07000, &(resource_pp_mmu[3]));
		resource_pp_mmu_found[4] = _mali_osk_resource_find(global_gpu_base_address + 0x1C000, &(resource_pp_mmu[4]));
		resource_pp_mmu_found[5] = _mali_osk_resource_find(global_gpu_base_address + 0x1D000, &(resource_pp_mmu[5]));
		resource_pp_mmu_found[6] = _mali_osk_resource_find(global_gpu_base_address + 0x1E000, &(resource_pp_mmu[6]));
		resource_pp_mmu_found[7] = _mali_osk_resource_find(global_gpu_base_address + 0x1F000, &(resource_pp_mmu[7]));


		if (_MALI_PRODUCT_ID_MALI450 == global_product_id)
		{
			resource_bcast_found = _mali_osk_resource_find(global_gpu_base_address + 0x13000, &resource_bcast);
			resource_dlbu_found = _mali_osk_resource_find(global_gpu_base_address + 0x14000, &resource_dlbu);
			resource_pp_mmu_bcast_found = _mali_osk_resource_find(global_gpu_base_address + 0x15000, &resource_pp_mmu_bcast);
			resource_pp_bcast_found = _mali_osk_resource_find(global_gpu_base_address + 0x16000, &resource_pp_bcast);

			if (_MALI_OSK_ERR_OK != resource_bcast_found ||
			    _MALI_OSK_ERR_OK != resource_dlbu_found ||
			    _MALI_OSK_ERR_OK != resource_pp_mmu_bcast_found ||
			    _MALI_OSK_ERR_OK != resource_pp_bcast_found)
			{
				/* Missing mandatory core(s) for Mali-450 */
				MALI_DEBUG_PRINT(2, ("Missing mandatory resources, Mali-450 needs DLBU, Broadcast unit, virtual PP core and virtual MMU\n"));
				return _MALI_OSK_ERR_FAULT;
			}
		}

		if (_MALI_OSK_ERR_OK != resource_gp_found ||
		    _MALI_OSK_ERR_OK != resource_gp_mmu_found ||
		    _MALI_OSK_ERR_OK != resource_pp_found[0] ||
		    _MALI_OSK_ERR_OK != resource_pp_mmu_found[0])
		{
			/* Missing mandatory core(s) */
			MALI_DEBUG_PRINT(2, ("Missing mandatory resource, need at least one GP and one PP, both with a separate MMU\n"));
			return _MALI_OSK_ERR_FAULT;
		}

		MALI_DEBUG_ASSERT(1 <= mali_l2_cache_core_get_glob_num_l2_cores());
		err = mali_create_group(mali_l2_cache_core_get_glob_l2_core(cluster_id_gp), &resource_gp_mmu, &resource_gp, NULL);
		if (err != _MALI_OSK_ERR_OK)
		{
			return err;
		}

		/* Create group for first (and mandatory) PP core */
		MALI_DEBUG_ASSERT(mali_l2_cache_core_get_glob_num_l2_cores() >= (cluster_id_pp_grp0 + 1)); /* >= 1 on Mali-300 and Mali-400, >= 2 on Mali-450 */
		err = mali_create_group(mali_l2_cache_core_get_glob_l2_core(cluster_id_pp_grp0), &resource_pp_mmu[0], NULL, &resource_pp[0]);
		if (err != _MALI_OSK_ERR_OK)
		{
			return err;
		}

		mali_inited_pp_cores_group_1++;

		/* Create groups for rest of the cores in the first PP core group */
		for (i = 1; i < 4; i++) /* First half of the PP cores belong to first core group */
		{
			if (mali_inited_pp_cores_group_1 < mali_max_pp_cores_group_1)
			{
				if (_MALI_OSK_ERR_OK == resource_pp_found[i] && _MALI_OSK_ERR_OK == resource_pp_mmu_found[i])
				{
					err = mali_create_group(mali_l2_cache_core_get_glob_l2_core(cluster_id_pp_grp0), &resource_pp_mmu[i], NULL, &resource_pp[i]);
					if (err != _MALI_OSK_ERR_OK)
					{
						return err;
					}
					mali_inited_pp_cores_group_1++;
				}
			}
		}

		/* Create groups for cores in the second PP core group */
		for (i = 4; i < 8; i++) /* Second half of the PP cores belong to second core group */
		{
			if (mali_inited_pp_cores_group_2 < mali_max_pp_cores_group_2)
			{
				if (_MALI_OSK_ERR_OK == resource_pp_found[i] && _MALI_OSK_ERR_OK == resource_pp_mmu_found[i])
				{
					MALI_DEBUG_ASSERT(mali_l2_cache_core_get_glob_num_l2_cores() >= 2); /* Only Mali-450 have a second core group */
					err = mali_create_group(mali_l2_cache_core_get_glob_l2_core(cluster_id_pp_grp1), &resource_pp_mmu[i], NULL, &resource_pp[i]);
					if (err != _MALI_OSK_ERR_OK)
					{
						return err;
					}
					mali_inited_pp_cores_group_2++;
				}
			}
		}

		if(_MALI_PRODUCT_ID_MALI450 == global_product_id)
		{
			err = mali_create_virtual_group(&resource_pp_mmu_bcast, &resource_pp_bcast, &resource_dlbu, &resource_bcast);
			if (_MALI_OSK_ERR_OK != err)
			{
				return err;
			}
		}

		mali_max_pp_cores_group_1 = mali_inited_pp_cores_group_1;
		mali_max_pp_cores_group_2 = mali_inited_pp_cores_group_2;
		MALI_DEBUG_PRINT(2, ("%d+%d PP cores initialized\n", mali_inited_pp_cores_group_1, mali_inited_pp_cores_group_2));

		return _MALI_OSK_ERR_OK;
	}

	/* No known HW core */
	return _MALI_OSK_ERR_FAULT;
}

static _mali_osk_errcode_t mali_parse_config_pmu(void)
{
	_mali_osk_resource_t resource_pmu;

	if (_MALI_OSK_ERR_OK == _mali_osk_resource_find(global_gpu_base_address + 0x02000, &resource_pmu))
	{
		u32 number_of_pp_cores = 0;
		u32 number_of_l2_caches = 0;

		mali_resource_count(&number_of_pp_cores, &number_of_l2_caches);

		if (NULL == mali_pmu_create(&resource_pmu, number_of_pp_cores, number_of_l2_caches))
		{
			return _MALI_OSK_ERR_FAULT;
		}
	}

	/* It's ok if the PMU doesn't exist */
	return _MALI_OSK_ERR_OK;
}

static _mali_osk_errcode_t mali_parse_config_memory(void)
{
	_mali_osk_errcode_t ret;

	if (0 == mali_dedicated_mem_start && 0 == mali_dedicated_mem_size && 0 == mali_shared_mem_size)
	{
		/* Memory settings are not overridden by module parameters, so use device settings */
		struct _mali_osk_device_data data = { 0, };

		if (_MALI_OSK_ERR_OK == _mali_osk_device_data_get(&data))
		{
			/* Use device specific settings (if defined) */
			mali_dedicated_mem_start = data.dedicated_mem_start;
			mali_dedicated_mem_size = data.dedicated_mem_size;
			mali_shared_mem_size = data.shared_mem_size;
		}

		if (0 == mali_dedicated_mem_start && 0 == mali_dedicated_mem_size && 0 == mali_shared_mem_size)
		{
			/* No GPU memory specified */
			return _MALI_OSK_ERR_INVALID_ARGS;
		}

		MALI_DEBUG_PRINT(2, ("Using device defined memory settings (dedicated: 0x%08X@0x%08X, shared: 0x%08X)\n",
		                     mali_dedicated_mem_size, mali_dedicated_mem_start, mali_shared_mem_size));
	}
	else
	{
		MALI_DEBUG_PRINT(2, ("Using module defined memory settings (dedicated: 0x%08X@0x%08X, shared: 0x%08X)\n",
		                     mali_dedicated_mem_size, mali_dedicated_mem_start, mali_shared_mem_size));
	}

	if (0 < mali_dedicated_mem_size && 0 != mali_dedicated_mem_start)
	{
		/* Dedicated memory */
		ret = mali_memory_core_resource_dedicated_memory(mali_dedicated_mem_start, mali_dedicated_mem_size);
		if (_MALI_OSK_ERR_OK != ret)
		{
			MALI_PRINT_ERROR(("Failed to register dedicated memory\n"));
			mali_memory_terminate();
			return ret;
		}
	}

	if (0 < mali_shared_mem_size)
	{
		/* Shared OS memory */
		ret = mali_memory_core_resource_os_memory(mali_shared_mem_size);
		if (_MALI_OSK_ERR_OK != ret)
		{
			MALI_PRINT_ERROR(("Failed to register shared OS memory\n"));
			mali_memory_terminate();
			return ret;
		}
	}

	if (0 == mali_fb_start && 0 == mali_fb_size)
	{
		/* Frame buffer settings are not overridden by module parameters, so use device settings */
		struct _mali_osk_device_data data = { 0, };

		if (_MALI_OSK_ERR_OK == _mali_osk_device_data_get(&data))
		{
			/* Use device specific settings (if defined) */
			mali_fb_start = data.fb_start;
			mali_fb_size = data.fb_size;
		}

		MALI_DEBUG_PRINT(2, ("Using device defined frame buffer settings (0x%08X@0x%08X)\n",
		                     mali_fb_size, mali_fb_start));
	}
	else
	{
		MALI_DEBUG_PRINT(2, ("Using module defined frame buffer settings (0x%08X@0x%08X)\n",
		                     mali_fb_size, mali_fb_start));
	}

	if (0 != mali_fb_size)
	{
		/* Register frame buffer */
		ret = mali_mem_validation_add_range(mali_fb_start, mali_fb_size);
		if (_MALI_OSK_ERR_OK != ret)
		{
			MALI_PRINT_ERROR(("Failed to register frame buffer memory region\n"));
			mali_memory_terminate();
			return ret;
		}
	}

	return _MALI_OSK_ERR_OK;
}

_mali_osk_errcode_t mali_initialize_subsystems(void)
{
	_mali_osk_errcode_t err;

	err = mali_session_initialize();
	if (_MALI_OSK_ERR_OK != err) goto session_init_failed;

#if defined(CONFIG_MALI400_PROFILING)
	err = _mali_osk_profiling_init(mali_boot_profiling ? MALI_TRUE : MALI_FALSE);
	if (_MALI_OSK_ERR_OK != err)
	{
		/* No biggie if we wheren't able to initialize the profiling */
		MALI_PRINT_ERROR(("Failed to initialize profiling, feature will be unavailable\n"));
	}
#endif

	err = mali_memory_initialize();
	if (_MALI_OSK_ERR_OK != err) goto memory_init_failed;

	/* Configure memory early. Memory allocation needed for mali_mmu_initialize. */
	err = mali_parse_config_memory();
	if (_MALI_OSK_ERR_OK != err) goto parse_memory_config_failed;

	/* Initialize the MALI PMU */
	err = mali_parse_config_pmu();
	if (_MALI_OSK_ERR_OK != err) goto parse_pmu_config_failed;

	/* Initialize the power management module */
	err = mali_pm_initialize();
	if (_MALI_OSK_ERR_OK != err) goto pm_init_failed;

	/* Make sure the power stays on for the rest of this function */
	err = _mali_osk_pm_dev_ref_add();
	if (_MALI_OSK_ERR_OK != err) goto pm_always_on_failed;

	/* Detect which Mali GPU we are dealing with */
	err = mali_parse_product_info();
	if (_MALI_OSK_ERR_OK != err) goto product_info_parsing_failed;

	/* The global_product_id is now populated with the correct Mali GPU */

	/* Initialize MMU module */
	err = mali_mmu_initialize();
	if (_MALI_OSK_ERR_OK != err) goto mmu_init_failed;

	if (_MALI_PRODUCT_ID_MALI450 == global_product_id)
	{
		err = mali_dlbu_initialize();
		if (_MALI_OSK_ERR_OK != err) goto dlbu_init_failed;
	}

	/* Start configuring the actual Mali hardware. */
	err = mali_parse_config_l2_cache();
	if (_MALI_OSK_ERR_OK != err) goto config_parsing_failed;
	err = mali_parse_config_groups();
	if (_MALI_OSK_ERR_OK != err) goto config_parsing_failed;

	/* Initialize the schedulers */
	err = mali_scheduler_initialize();
	if (_MALI_OSK_ERR_OK != err) goto scheduler_init_failed;
	err = mali_gp_scheduler_initialize();
	if (_MALI_OSK_ERR_OK != err) goto gp_scheduler_init_failed;
	err = mali_pp_scheduler_initialize();
	if (_MALI_OSK_ERR_OK != err) goto pp_scheduler_init_failed;

	/* Initialize the GPU utilization tracking */
	err = mali_utilization_init();
	if (_MALI_OSK_ERR_OK != err) goto utilization_init_failed;

	/* Allowing the system to be turned off */
	_mali_osk_pm_dev_ref_dec();

	MALI_SUCCESS; /* all ok */

	/* Error handling */

utilization_init_failed:
	mali_pp_scheduler_terminate();
pp_scheduler_init_failed:
	mali_gp_scheduler_terminate();
gp_scheduler_init_failed:
	mali_scheduler_terminate();
scheduler_init_failed:
config_parsing_failed:
	mali_delete_l2_cache_cores(); /* Delete L2 cache cores even if config parsing failed. */
dlbu_init_failed:
	mali_dlbu_terminate();
mmu_init_failed:
	mali_mmu_terminate();
	/* Nothing to roll back */
product_info_parsing_failed:
	/* Allowing the system to be turned off */
	_mali_osk_pm_dev_ref_dec();
pm_always_on_failed:
	mali_pm_terminate();
pm_init_failed:
	{
		struct mali_pmu_core *pmu = mali_pmu_get_global_pmu_core();
		if (NULL != pmu)
		{
			mali_pmu_delete(pmu);
		}
	}
parse_pmu_config_failed:
	/* undoing mali_parse_config_memory() is done by mali_memory_terminate() */
parse_memory_config_failed:
	mali_memory_terminate();
memory_init_failed:
#if defined(CONFIG_MALI400_PROFILING)
	_mali_osk_profiling_term();
#endif
	mali_session_terminate();
session_init_failed:
	return err;
}

void mali_terminate_subsystems(void)
{
	struct mali_pmu_core *pmu = mali_pmu_get_global_pmu_core();

	MALI_DEBUG_PRINT(2, ("terminate_subsystems() called\n"));

	/* shut down subsystems in reverse order from startup */

	/* We need the GPU to be powered up for the terminate sequence */
	_mali_osk_pm_dev_ref_add();

	mali_utilization_term();
	mali_pp_scheduler_terminate();
	mali_gp_scheduler_terminate();
	mali_scheduler_terminate();
	mali_delete_l2_cache_cores();
	if (_MALI_PRODUCT_ID_MALI450 == global_product_id)
	{
		mali_dlbu_terminate();
	}
	mali_mmu_terminate();
	if (NULL != pmu)
	{
		mali_pmu_delete(pmu);
	}
	mali_pm_terminate();
	mali_memory_terminate();
#if defined(CONFIG_MALI400_PROFILING)
	_mali_osk_profiling_term();
#endif

	/* Allowing the system to be turned off */
	_mali_osk_pm_dev_ref_dec();

	mali_session_terminate();
}

_mali_product_id_t mali_kernel_core_get_product_id(void)
{
	return global_product_id;
}

_mali_osk_errcode_t _mali_ukk_get_api_version( _mali_uk_get_api_version_s *args )
{
	MALI_DEBUG_ASSERT_POINTER(args);
	MALI_CHECK_NON_NULL(args->ctx, _MALI_OSK_ERR_INVALID_ARGS);

	/* check compatability */
	if ( args->version == _MALI_UK_API_VERSION )
	{
		args->compatible = 1;
	}
	else
	{
		args->compatible = 0;
	}

	args->version = _MALI_UK_API_VERSION; /* report our version */

	/* success regardless of being compatible or not */
	MALI_SUCCESS;
}

_mali_osk_errcode_t _mali_ukk_wait_for_notification( _mali_uk_wait_for_notification_s *args )
{
	_mali_osk_errcode_t err;
	_mali_osk_notification_t * notification;
	_mali_osk_notification_queue_t *queue;

	/* check input */
	MALI_DEBUG_ASSERT_POINTER(args);
	MALI_CHECK_NON_NULL(args->ctx, _MALI_OSK_ERR_INVALID_ARGS);

	queue = ((struct mali_session_data *)args->ctx)->ioctl_queue;

	/* if the queue does not exist we're currently shutting down */
	if (NULL == queue)
	{
		MALI_DEBUG_PRINT(1, ("No notification queue registered with the session. Asking userspace to stop querying\n"));
		args->type = _MALI_NOTIFICATION_CORE_SHUTDOWN_IN_PROGRESS;
		MALI_SUCCESS;
	}

	/* receive a notification, might sleep */
	err = _mali_osk_notification_queue_receive(queue, &notification);
	if (_MALI_OSK_ERR_OK != err)
	{
		MALI_ERROR(err); /* errcode returned, pass on to caller */
	}

	/* copy the buffer to the user */
	args->type = (_mali_uk_notification_type)notification->notification_type;
	_mali_osk_memcpy(&args->data, notification->result_buffer, notification->result_buffer_size);

	/* finished with the notification */
	_mali_osk_notification_delete( notification );

	MALI_SUCCESS; /* all ok */
}

_mali_osk_errcode_t _mali_ukk_post_notification( _mali_uk_post_notification_s *args )
{
	_mali_osk_notification_t * notification;
	_mali_osk_notification_queue_t *queue;

	/* check input */
	MALI_DEBUG_ASSERT_POINTER(args);
	MALI_CHECK_NON_NULL(args->ctx, _MALI_OSK_ERR_INVALID_ARGS);

	queue = ((struct mali_session_data *)args->ctx)->ioctl_queue;

	/* if the queue does not exist we're currently shutting down */
	if (NULL == queue)
	{
		MALI_DEBUG_PRINT(1, ("No notification queue registered with the session. Asking userspace to stop querying\n"));
		MALI_SUCCESS;
	}

	notification = _mali_osk_notification_create(args->type, 0);
	if (NULL == notification)
	{
		MALI_PRINT_ERROR( ("Failed to create notification object\n"));
		return _MALI_OSK_ERR_NOMEM;
	}

	_mali_osk_notification_queue_send(queue, notification);

	MALI_SUCCESS; /* all ok */
}

_mali_osk_errcode_t _mali_ukk_open(void **context)
{
	struct mali_session_data *session;

	/* allocated struct to track this session */
	session = (struct mali_session_data *)_mali_osk_calloc(1, sizeof(struct mali_session_data));
	MALI_CHECK_NON_NULL(session, _MALI_OSK_ERR_NOMEM);

	MALI_DEBUG_PRINT(3, ("Session starting\n"));

	/* create a response queue for this session */
	session->ioctl_queue = _mali_osk_notification_queue_init();
	if (NULL == session->ioctl_queue)
	{
		_mali_osk_free(session);
		MALI_ERROR(_MALI_OSK_ERR_NOMEM);
	}

	session->page_directory = mali_mmu_pagedir_alloc();
	if (NULL == session->page_directory)
	{
		_mali_osk_notification_queue_term(session->ioctl_queue);
		_mali_osk_free(session);
		MALI_ERROR(_MALI_OSK_ERR_NOMEM);
	}

	if (_MALI_OSK_ERR_OK != mali_mmu_pagedir_map(session->page_directory, MALI_DLBU_VIRT_ADDR, _MALI_OSK_MALI_PAGE_SIZE))
	{
		MALI_PRINT_ERROR(("Failed to map DLBU page into session\n"));
		_mali_osk_notification_queue_term(session->ioctl_queue);
		_mali_osk_free(session);
		MALI_ERROR(_MALI_OSK_ERR_NOMEM);
	}

	if (0 != mali_dlbu_phys_addr)
	{
		mali_mmu_pagedir_update(session->page_directory, MALI_DLBU_VIRT_ADDR, mali_dlbu_phys_addr,
		                        _MALI_OSK_MALI_PAGE_SIZE, MALI_CACHE_STANDARD);
	}

	if (_MALI_OSK_ERR_OK != mali_memory_session_begin(session))
	{
		mali_mmu_pagedir_free(session->page_directory);
		_mali_osk_notification_queue_term(session->ioctl_queue);
		_mali_osk_free(session);
		MALI_ERROR(_MALI_OSK_ERR_NOMEM);
	}

#ifdef CONFIG_SYNC
/* MALI_SEC */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,4,0)
	_mali_osk_list_init(&session->pending_jobs);
	session->pending_jobs_lock = _mali_osk_lock_init(_MALI_OSK_LOCKFLAG_NONINTERRUPTABLE | _MALI_OSK_LOCKFLAG_ORDERED | _MALI_OSK_LOCKFLAG_SPINLOCK,
	                                                 0, _MALI_OSK_LOCK_ORDER_SESSION_PENDING_JOBS);
	if (NULL == session->pending_jobs_lock)
	{
		MALI_PRINT_ERROR(("Failed to create pending jobs lock\n"));
		mali_memory_session_end(session);
		mali_mmu_pagedir_free(session->page_directory);
		_mali_osk_notification_queue_term(session->ioctl_queue);
		_mali_osk_free(session);
		MALI_ERROR(_MALI_OSK_ERR_NOMEM);
	}
#endif
#endif

	*context = (void*)session;

	/* Add session to the list of all sessions. */
	mali_session_add(session);

	/* Initialize list of jobs on this session */
	_MALI_OSK_INIT_LIST_HEAD(&session->job_list);

	MALI_DEBUG_PRINT(2, ("Session started\n"));
	MALI_SUCCESS;
}

_mali_osk_errcode_t _mali_ukk_close(void **context)
{
	struct mali_session_data *session;
	MALI_CHECK_NON_NULL(context, _MALI_OSK_ERR_INVALID_ARGS);
	session = (struct mali_session_data *)*context;

	MALI_DEBUG_PRINT(3, ("Session ending\n"));

	/* Remove session from list of all sessions. */
	mali_session_remove(session);

	/* Abort pending jobs */
#ifdef CONFIG_SYNC
/* MALI_SEC */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,4,0)
	{
		_mali_osk_list_t tmp_job_list;
		struct mali_pp_job *job, *tmp;
		_MALI_OSK_INIT_LIST_HEAD(&tmp_job_list);

		_mali_osk_lock_wait(session->pending_jobs_lock, _MALI_OSK_LOCKMODE_RW);
		/* Abort asynchronous wait on fence. */
		_MALI_OSK_LIST_FOREACHENTRY(job, tmp, &session->pending_jobs, struct mali_pp_job, list)
		{
			MALI_DEBUG_PRINT(2, ("Sync: Aborting wait for session %x job %x\n", session, job));
			if (sync_fence_cancel_async(job->pre_fence, &job->sync_waiter))
			{
				MALI_DEBUG_PRINT(2, ("Sync: Failed to abort job %x\n", job));
			}
			_mali_osk_list_add(&job->list, &tmp_job_list);
		}
		_mali_osk_lock_signal(session->pending_jobs_lock, _MALI_OSK_LOCKMODE_RW);

		_mali_osk_wq_flush();

		_mali_osk_lock_term(session->pending_jobs_lock);

		/* Delete jobs */
		_MALI_OSK_LIST_FOREACHENTRY(job, tmp, &tmp_job_list, struct mali_pp_job, list)
		{
			mali_pp_job_delete(job);
		}
	}
#endif
#endif

	/* Abort queued and running jobs */
	mali_gp_scheduler_abort_session(session);
	mali_pp_scheduler_abort_session(session);

	/* Flush pending work.
	 * Needed to make sure all bottom half processing related to this
	 * session has been completed, before we free internal data structures.
	 */
	_mali_osk_wq_flush();

	/* Free remaining memory allocated to this session */
	mali_memory_session_end(session);

	/* Free session data structures */
	mali_mmu_pagedir_free(session->page_directory);
	_mali_osk_notification_queue_term(session->ioctl_queue);
	_mali_osk_free(session);

	*context = NULL;

	MALI_DEBUG_PRINT(2, ("Session has ended\n"));

	MALI_SUCCESS;
}

#if MALI_STATE_TRACKING
u32 _mali_kernel_core_dump_state(char* buf, u32 size)
{
	int n = 0; /* Number of bytes written to buf */

	n += mali_gp_scheduler_dump_state(buf + n, size - n);
	n += mali_pp_scheduler_dump_state(buf + n, size - n);

	return n;
}
#endif
