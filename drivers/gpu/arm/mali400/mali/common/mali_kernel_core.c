/*
 * This confidential and proprietary software may be used only as
 * authorised by a licensing agreement from ARM Limited
 * (C) COPYRIGHT 2007-2013 ARM Limited
 * ALL RIGHTS RESERVED
 * The entire notice above must be reproduced on all authorised
 * copies and copies may only be made to the extent permitted
 * by a licensing agreement from ARM Limited.
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
#include "mali_pp_job.h"
#include "mali_group.h"
#include "mali_pm.h"
#include "mali_pmu.h"
#include "mali_scheduler.h"
#include "mali_kernel_utilization.h"
#include "mali_l2_cache.h"
#include "mali_dma.h"
#include "mali_timeline.h"
#include "mali_soft_job.h"
#include "mali_pm_domain.h"
#if defined(CONFIG_MALI400_PROFILING)
#include "mali_osk_profiling.h"
#endif
#if defined(CONFIG_MALI400_INTERNAL_PROFILING)
#include "mali_profiling_internal.h"
#endif
//extern unsigned long totalram_pages;
#include <linux/mm.h>


/* Mali GPU memory. Real values come from module parameter or from device specific data */
unsigned int mali_dedicated_mem_start = 0;
unsigned int mali_dedicated_mem_size = 0;
unsigned long mali_shared_mem_size = 0;

/* Frame buffer memory to be accessible by Mali GPU */
int mali_fb_start = 0;
int mali_fb_size = 0;

/* Mali max job runtime */
extern int mali_max_job_runtime;

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

mali_bool mali_gpu_class_is_mali450 = MALI_FALSE;

static _mali_osk_errcode_t mali_set_global_gpu_base_address(void)
{
	global_gpu_base_address = _mali_osk_resource_base_address();
	if (0 == global_gpu_base_address) {
		return _MALI_OSK_ERR_ITEM_NOT_FOUND;
	}

	return _MALI_OSK_ERR_OK;
}

static u32 mali_get_bcast_id(_mali_osk_resource_t *resource_pp)
{
	switch (resource_pp->base - global_gpu_base_address) {
	case 0x08000:
	case 0x20000: /* fall-through for aliased mapping */
		return 0x01;
	case 0x0A000:
	case 0x22000: /* fall-through for aliased mapping */
		return 0x02;
	case 0x0C000:
	case 0x24000: /* fall-through for aliased mapping */
		return 0x04;
	case 0x0E000:
	case 0x26000: /* fall-through for aliased mapping */
		return 0x08;
	case 0x28000:
		return 0x10;
	case 0x2A000:
		return 0x20;
	case 0x2C000:
		return 0x40;
	case 0x2E000:
		return 0x80;
	default:
		return 0;
	}
}

static _mali_osk_errcode_t mali_parse_product_info(void)
{
	/*
	 * Mali-200 has the PP core first, while Mali-300, Mali-400 and Mali-450 have the GP core first.
	 * Look at the version register for the first PP core in order to determine the GPU HW revision.
	 */

	u32 first_pp_offset;
	_mali_osk_resource_t first_pp_resource;

	/* Find out where the first PP core is located */
	if (_MALI_OSK_ERR_OK == _mali_osk_resource_find(global_gpu_base_address + 0x8000, NULL)) {
		/* Mali-300/400/450 */
		first_pp_offset = 0x8000;
	} else {
		/* Mali-200 */
		first_pp_offset = 0x0000;
	}

	/* Find the first PP core resource (again) */
	if (_MALI_OSK_ERR_OK == _mali_osk_resource_find(global_gpu_base_address + first_pp_offset, &first_pp_resource)) {
		/* Create a dummy PP object for this core so that we can read the version register */
		struct mali_group *group = mali_group_create(NULL, NULL, NULL);
		if (NULL != group) {
			struct mali_pp_core *pp_core = mali_pp_create(&first_pp_resource, group, MALI_FALSE, mali_get_bcast_id(&first_pp_resource));
			if (NULL != pp_core) {
				u32 pp_version = mali_pp_core_get_version(pp_core);
				mali_group_delete(group);

				global_gpu_major_version = (pp_version >> 8) & 0xFF;
				global_gpu_minor_version = pp_version & 0xFF;

				switch (pp_version >> 16) {
				case MALI200_PP_PRODUCT_ID:
					global_product_id = _MALI_PRODUCT_ID_MALI200;
					MALI_DEBUG_PRINT(2, ("Found Mali GPU Mali-200 r%up%u\n", global_gpu_major_version, global_gpu_minor_version));
					MALI_PRINT_ERROR(("Mali-200 is not supported by this driver.\n"));
					_mali_osk_abort();
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
			} else {
				MALI_PRINT_ERROR(("Failed to create initial PP object\n"));
			}
		} else {
			MALI_PRINT_ERROR(("Failed to create initial group object\n"));
		}
	} else {
		MALI_PRINT_ERROR(("First PP core not specified in config file\n"));
	}

	return _MALI_OSK_ERR_FAULT;
}


static void mali_resource_count(u32 *pp_count, u32 *l2_count)
{
	*pp_count = 0;
	*l2_count = 0;

	if (_MALI_OSK_ERR_OK == _mali_osk_resource_find(global_gpu_base_address + 0x08000, NULL)) {
		++(*pp_count);
	}
	if (_MALI_OSK_ERR_OK == _mali_osk_resource_find(global_gpu_base_address + 0x0A000, NULL)) {
		++(*pp_count);
	}
	if (_MALI_OSK_ERR_OK == _mali_osk_resource_find(global_gpu_base_address + 0x0C000, NULL)) {
		++(*pp_count);
	}
	if (_MALI_OSK_ERR_OK == _mali_osk_resource_find(global_gpu_base_address + 0x0E000, NULL)) {
		++(*pp_count);
	}
	if (_MALI_OSK_ERR_OK == _mali_osk_resource_find(global_gpu_base_address + 0x28000, NULL)) {
		++(*pp_count);
	}
	if (_MALI_OSK_ERR_OK == _mali_osk_resource_find(global_gpu_base_address + 0x2A000, NULL)) {
		++(*pp_count);
	}
	if (_MALI_OSK_ERR_OK == _mali_osk_resource_find(global_gpu_base_address + 0x2C000, NULL)) {
		++(*pp_count);
	}
	if (_MALI_OSK_ERR_OK == _mali_osk_resource_find(global_gpu_base_address + 0x2E000, NULL)) {
		++(*pp_count);
	}

	if (_MALI_OSK_ERR_OK == _mali_osk_resource_find(global_gpu_base_address + 0x1000, NULL)) {
		++(*l2_count);
	}
	if (_MALI_OSK_ERR_OK == _mali_osk_resource_find(global_gpu_base_address + 0x10000, NULL)) {
		++(*l2_count);
	}
	if (_MALI_OSK_ERR_OK == _mali_osk_resource_find(global_gpu_base_address + 0x11000, NULL)) {
		++(*l2_count);
	}
}

static void mali_delete_groups(void)
{
	struct mali_group *group;

	group = mali_group_get_glob_group(0);
	while (NULL != group) {
		mali_group_delete(group);
		group = mali_group_get_glob_group(0);
	}

	MALI_DEBUG_ASSERT(0 == mali_group_get_glob_num_groups());
}

static void mali_delete_l2_cache_cores(void)
{
	struct mali_l2_cache_core *l2;

	l2 = mali_l2_cache_core_get_glob_l2_core(0);
	while (NULL != l2) {
		mali_l2_cache_delete(l2);
		l2 = mali_l2_cache_core_get_glob_l2_core(0);
	}

	MALI_DEBUG_ASSERT(0 == mali_l2_cache_core_get_glob_num_l2_cores());
}

static struct mali_l2_cache_core *mali_create_l2_cache_core(_mali_osk_resource_t *resource)
{
	struct mali_l2_cache_core *l2_cache = NULL;

	if (NULL != resource) {

		MALI_DEBUG_PRINT(3, ("Found L2 cache %s\n", resource->description));

		l2_cache = mali_l2_cache_create(resource);
		if (NULL == l2_cache) {
			MALI_PRINT_ERROR(("Failed to create L2 cache object\n"));
			return NULL;
		}
	}
	MALI_DEBUG_PRINT(3, ("Created L2 cache core object\n"));

	return l2_cache;
}

static _mali_osk_errcode_t mali_parse_config_l2_cache(void)
{
	struct mali_l2_cache_core *l2_cache = NULL;

	if (mali_is_mali400()) {
		_mali_osk_resource_t l2_resource;
		if (_MALI_OSK_ERR_OK != _mali_osk_resource_find(global_gpu_base_address + 0x1000, &l2_resource)) {
			MALI_DEBUG_PRINT(3, ("Did not find required Mali L2 cache in config file\n"));
			return _MALI_OSK_ERR_FAULT;
		}

		l2_cache = mali_create_l2_cache_core(&l2_resource);
		if (NULL == l2_cache) {
			return _MALI_OSK_ERR_FAULT;
		}
		mali_pm_domain_add_l2(mali_pmu_get_domain_mask(MALI_L20_DOMAIN_INDEX), l2_cache);
	} else if (mali_is_mali450()) {
		/*
		 * L2 for GP    at 0x10000
		 * L2 for PP0-3 at 0x01000
		 * L2 for PP4-7 at 0x11000 (optional)
		 */

		_mali_osk_resource_t l2_gp_resource;
		_mali_osk_resource_t l2_pp_grp0_resource;
		_mali_osk_resource_t l2_pp_grp1_resource;

		/* Make cluster for GP's L2 */
		if (_MALI_OSK_ERR_OK == _mali_osk_resource_find(global_gpu_base_address + 0x10000, &l2_gp_resource)) {
			MALI_DEBUG_PRINT(3, ("Creating Mali-450 L2 cache core for GP\n"));
			l2_cache = mali_create_l2_cache_core(&l2_gp_resource);
			if (NULL == l2_cache) {
				return _MALI_OSK_ERR_FAULT;
			}
			mali_pm_domain_add_l2(mali_pmu_get_domain_mask(MALI_L20_DOMAIN_INDEX), l2_cache);
		} else {
			MALI_DEBUG_PRINT(3, ("Did not find required Mali L2 cache for GP in config file\n"));
			return _MALI_OSK_ERR_FAULT;
		}

		/* Find corresponding l2 domain */
		if (_MALI_OSK_ERR_OK == _mali_osk_resource_find(global_gpu_base_address + 0x1000, &l2_pp_grp0_resource)) {
			MALI_DEBUG_PRINT(3, ("Creating Mali-450 L2 cache core for PP group 0\n"));
			l2_cache = mali_create_l2_cache_core(&l2_pp_grp0_resource);
			if (NULL == l2_cache) {
				return _MALI_OSK_ERR_FAULT;
			}
			mali_pm_domain_add_l2(mali_pmu_get_domain_mask(MALI_L21_DOMAIN_INDEX), l2_cache);
		} else {
			MALI_DEBUG_PRINT(3, ("Did not find required Mali L2 cache for PP group 0 in config file\n"));
			return _MALI_OSK_ERR_FAULT;
		}

		/* Second PP core group is optional, don't fail if we don't find it */
		if (_MALI_OSK_ERR_OK == _mali_osk_resource_find(global_gpu_base_address + 0x11000, &l2_pp_grp1_resource)) {
			MALI_DEBUG_PRINT(3, ("Creating Mali-450 L2 cache core for PP group 1\n"));
			l2_cache = mali_create_l2_cache_core(&l2_pp_grp1_resource);
			if (NULL == l2_cache) {
				return _MALI_OSK_ERR_FAULT;
			}
			mali_pm_domain_add_l2(mali_pmu_get_domain_mask(MALI_L22_DOMAIN_INDEX), l2_cache);
		}
	}

	return _MALI_OSK_ERR_OK;
}

static struct mali_group *mali_create_group(struct mali_l2_cache_core *cache,
        _mali_osk_resource_t *resource_mmu,
        _mali_osk_resource_t *resource_gp,
        _mali_osk_resource_t *resource_pp)
{
	struct mali_mmu_core *mmu;
	struct mali_group *group;

	MALI_DEBUG_PRINT(3, ("Starting new group for MMU %s\n", resource_mmu->description));

	/* Create the group object */
	group = mali_group_create(cache, NULL, NULL);
	if (NULL == group) {
		MALI_PRINT_ERROR(("Failed to create group object for MMU %s\n", resource_mmu->description));
		return NULL;
	}

	/* Create the MMU object inside group */
	mmu = mali_mmu_create(resource_mmu, group, MALI_FALSE);
	if (NULL == mmu) {
		MALI_PRINT_ERROR(("Failed to create MMU object\n"));
		mali_group_delete(group);
		return NULL;
	}

	if (NULL != resource_gp) {
		/* Create the GP core object inside this group */
		struct mali_gp_core *gp_core = mali_gp_create(resource_gp, group);
		if (NULL == gp_core) {
			/* No need to clean up now, as we will clean up everything linked in from the cluster when we fail this function */
			MALI_PRINT_ERROR(("Failed to create GP object\n"));
			mali_group_delete(group);
			return NULL;
		}
	}

	if (NULL != resource_pp) {
		struct mali_pp_core *pp_core;

		/* Create the PP core object inside this group */
		pp_core = mali_pp_create(resource_pp, group, MALI_FALSE, mali_get_bcast_id(resource_pp));
		if (NULL == pp_core) {
			/* No need to clean up now, as we will clean up everything linked in from the cluster when we fail this function */
			MALI_PRINT_ERROR(("Failed to create PP object\n"));
			mali_group_delete(group);
			return NULL;
		}
	}

	/* Reset group */
	mali_group_lock(group);
	mali_group_reset(group);
	mali_group_unlock(group);

	return group;
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
	if (NULL == dlbu_core) {
		MALI_PRINT_ERROR(("Failed to create DLBU object \n"));
		return _MALI_OSK_ERR_FAULT;
	}

	/* Create the Broadcast unit core */
	bcast_core = mali_bcast_unit_create(resource_bcast);
	if (NULL == bcast_core) {
		MALI_PRINT_ERROR(("Failed to create Broadcast unit object!\n"));
		mali_dlbu_delete(dlbu_core);
		return _MALI_OSK_ERR_FAULT;
	}

	/* Create the group object */
	group = mali_group_create(NULL, dlbu_core, bcast_core);
	if (NULL == group) {
		MALI_PRINT_ERROR(("Failed to create group object for MMU PP broadcast core %s\n", resource_mmu_pp_bcast->description));
		mali_bcast_unit_delete(bcast_core);
		mali_dlbu_delete(dlbu_core);
		return _MALI_OSK_ERR_FAULT;
	}

	/* Create the MMU object inside group */
	mmu_pp_bcast_core = mali_mmu_create(resource_mmu_pp_bcast, group, MALI_TRUE);
	if (NULL == mmu_pp_bcast_core) {
		MALI_PRINT_ERROR(("Failed to create MMU PP broadcast object\n"));
		mali_group_delete(group);
		return _MALI_OSK_ERR_FAULT;
	}

	/* Create the PP core object inside this group */
	pp_bcast_core = mali_pp_create(resource_pp_bcast, group, MALI_TRUE, 0);
	if (NULL == pp_bcast_core) {
		/* No need to clean up now, as we will clean up everything linked in from the cluster when we fail this function */
		MALI_PRINT_ERROR(("Failed to create PP object\n"));
		mali_group_delete(group);
		return _MALI_OSK_ERR_FAULT;
	}

	return _MALI_OSK_ERR_OK;
}

static _mali_osk_errcode_t mali_parse_config_groups(void)
{
	struct mali_group *group;
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

	if (!(mali_is_mali400() || mali_is_mali450())) {
		/* No known HW core */
		return _MALI_OSK_ERR_FAULT;
	}

	if (MALI_MAX_JOB_RUNTIME_DEFAULT == mali_max_job_runtime) {
		/* Group settings are not overridden by module parameters, so use device settings */
		struct _mali_osk_device_data data = { 0, };

		if (_MALI_OSK_ERR_OK == _mali_osk_device_data_get(&data)) {
			/* Use device specific settings (if defined) */
			if (0 != data.max_job_runtime) {
				mali_max_job_runtime = data.max_job_runtime;
			}
		}
	}

	if (mali_is_mali450()) {
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


	if (mali_is_mali450()) {
		resource_bcast_found = _mali_osk_resource_find(global_gpu_base_address + 0x13000, &resource_bcast);
		resource_dlbu_found = _mali_osk_resource_find(global_gpu_base_address + 0x14000, &resource_dlbu);
		resource_pp_mmu_bcast_found = _mali_osk_resource_find(global_gpu_base_address + 0x15000, &resource_pp_mmu_bcast);
		resource_pp_bcast_found = _mali_osk_resource_find(global_gpu_base_address + 0x16000, &resource_pp_bcast);

		if (_MALI_OSK_ERR_OK != resource_bcast_found ||
		    _MALI_OSK_ERR_OK != resource_dlbu_found ||
		    _MALI_OSK_ERR_OK != resource_pp_mmu_bcast_found ||
		    _MALI_OSK_ERR_OK != resource_pp_bcast_found) {
			/* Missing mandatory core(s) for Mali-450 */
			MALI_DEBUG_PRINT(2, ("Missing mandatory resources, Mali-450 needs DLBU, Broadcast unit, virtual PP core and virtual MMU\n"));
			return _MALI_OSK_ERR_FAULT;
		}
	}

	if (_MALI_OSK_ERR_OK != resource_gp_found ||
	    _MALI_OSK_ERR_OK != resource_gp_mmu_found ||
	    _MALI_OSK_ERR_OK != resource_pp_found[0] ||
	    _MALI_OSK_ERR_OK != resource_pp_mmu_found[0]) {
		/* Missing mandatory core(s) */
		MALI_DEBUG_PRINT(2, ("Missing mandatory resource, need at least one GP and one PP, both with a separate MMU\n"));
		return _MALI_OSK_ERR_FAULT;
	}

	MALI_DEBUG_ASSERT(1 <= mali_l2_cache_core_get_glob_num_l2_cores());
	group = mali_create_group(mali_l2_cache_core_get_glob_l2_core(cluster_id_gp), &resource_gp_mmu, &resource_gp, NULL);
	if (NULL == group) {
		return _MALI_OSK_ERR_FAULT;
	}

	/* Add GP in group, for PMU ref count */
	mali_pm_domain_add_group(mali_pmu_get_domain_mask(MALI_GP_DOMAIN_INDEX), group);

	/* Create group for first (and mandatory) PP core */
	MALI_DEBUG_ASSERT(mali_l2_cache_core_get_glob_num_l2_cores() >= (cluster_id_pp_grp0 + 1)); /* >= 1 on Mali-300 and Mali-400, >= 2 on Mali-450 */
	group = mali_create_group(mali_l2_cache_core_get_glob_l2_core(cluster_id_pp_grp0), &resource_pp_mmu[0], NULL, &resource_pp[0]);
	if (NULL == group) {
		return _MALI_OSK_ERR_FAULT;
	}

	/* Find corresponding pp domain */
	mali_pm_domain_add_group(mali_pmu_get_domain_mask(MALI_PP0_DOMAIN_INDEX), group);

	mali_inited_pp_cores_group_1++;

	/* Create groups for rest of the cores in the first PP core group */
	for (i = 1; i < 4; i++) { /* First half of the PP cores belong to first core group */
		if (mali_inited_pp_cores_group_1 < mali_max_pp_cores_group_1) {
			if (_MALI_OSK_ERR_OK == resource_pp_found[i] && _MALI_OSK_ERR_OK == resource_pp_mmu_found[i]) {
				group = mali_create_group(mali_l2_cache_core_get_glob_l2_core(cluster_id_pp_grp0), &resource_pp_mmu[i], NULL, &resource_pp[i]);
				if (NULL == group) {
					return _MALI_OSK_ERR_FAULT;
				}

				mali_pm_domain_add_group(mali_pmu_get_domain_mask(i + MALI_PP0_DOMAIN_INDEX), group);

				mali_inited_pp_cores_group_1++;
			}
		}
	}

	/* Create groups for cores in the second PP core group */
	for (i = 4; i < 8; i++) { /* Second half of the PP cores belong to second core group */
		if (mali_inited_pp_cores_group_2 < mali_max_pp_cores_group_2) {
			if (_MALI_OSK_ERR_OK == resource_pp_found[i] && _MALI_OSK_ERR_OK == resource_pp_mmu_found[i]) {
				MALI_DEBUG_ASSERT(mali_l2_cache_core_get_glob_num_l2_cores() >= 2); /* Only Mali-450 have a second core group */
				group = mali_create_group(mali_l2_cache_core_get_glob_l2_core(cluster_id_pp_grp1), &resource_pp_mmu[i], NULL, &resource_pp[i]);
				if (NULL == group) {
					return _MALI_OSK_ERR_FAULT;
				}
				mali_pm_domain_add_group(mali_pmu_get_domain_mask(i + MALI_PP0_DOMAIN_INDEX), group);
				mali_inited_pp_cores_group_2++;
			}
		}
	}

	if(mali_is_mali450()) {
		_mali_osk_errcode_t err = mali_create_virtual_group(&resource_pp_mmu_bcast, &resource_pp_bcast, &resource_dlbu, &resource_bcast);
		if (_MALI_OSK_ERR_OK != err) {
			return err;
		}
	}

	mali_max_pp_cores_group_1 = mali_inited_pp_cores_group_1;
	mali_max_pp_cores_group_2 = mali_inited_pp_cores_group_2;
	MALI_DEBUG_PRINT(2, ("%d+%d PP cores initialized\n", mali_inited_pp_cores_group_1, mali_inited_pp_cores_group_2));

	return _MALI_OSK_ERR_OK;
}

static _mali_osk_errcode_t mali_check_shared_interrupts(void)
{
#if !defined(CONFIG_MALI_SHARED_INTERRUPTS)
	if (MALI_TRUE == _mali_osk_shared_interrupts()) {
		MALI_PRINT_ERROR(("Shared interrupts detected, but driver support is not enabled\n"));
		return _MALI_OSK_ERR_FAULT;
	}
#endif /* !defined(CONFIG_MALI_SHARED_INTERRUPTS) */

	/* It is OK to compile support for shared interrupts even if Mali is not using it. */
	return _MALI_OSK_ERR_OK;
}

static _mali_osk_errcode_t mali_create_pm_domains(void)
{
	int i;

	for (i = 0; i < MALI_MAX_NUMBER_OF_DOMAINS; i++) {
		if (0x0 == mali_pmu_get_domain_mask(i)) continue;

		if (NULL == mali_pm_domain_create(mali_pmu_get_domain_mask(i))) {
			return _MALI_OSK_ERR_NOMEM;
		}
	}

	return _MALI_OSK_ERR_OK;
}

static void mali_use_default_pm_domain_config(void)
{
	u32 pp_count_gr1 = 0;
	u32 pp_count_gr2 = 0;
	u32 l2_count = 0;

	MALI_DEBUG_ASSERT(0 != global_gpu_base_address);

	/* GP core */
	if (_MALI_OSK_ERR_OK == _mali_osk_resource_find(global_gpu_base_address + 0x00000, NULL)) {
		mali_pmu_set_domain_mask(MALI_GP_DOMAIN_INDEX, 0x01);
	}

	/* PP0 - PP3 core */
	if (_MALI_OSK_ERR_OK == _mali_osk_resource_find(global_gpu_base_address + 0x08000, NULL)) {
		++pp_count_gr1;

		if (mali_is_mali400()) {
			mali_pmu_set_domain_mask(MALI_PP0_DOMAIN_INDEX, 0x01<<2);
		} else if (mali_is_mali450()) {
			mali_pmu_set_domain_mask(MALI_PP0_DOMAIN_INDEX, 0x01<<1);
		}
	}

	if (_MALI_OSK_ERR_OK == _mali_osk_resource_find(global_gpu_base_address + 0x0A000, NULL)) {
		++pp_count_gr1;

		if (mali_is_mali400()) {
			mali_pmu_set_domain_mask(MALI_PP1_DOMAIN_INDEX, 0x01<<3);
		} else if (mali_is_mali450()) {
			mali_pmu_set_domain_mask(MALI_PP1_DOMAIN_INDEX, 0x01<<2);
		}
	}

	if (_MALI_OSK_ERR_OK == _mali_osk_resource_find(global_gpu_base_address + 0x0C000, NULL)) {
		++pp_count_gr1;

		if (mali_is_mali400()) {
			mali_pmu_set_domain_mask(MALI_PP2_DOMAIN_INDEX, 0x01<<4);
		} else if (mali_is_mali450()) {
			mali_pmu_set_domain_mask(MALI_PP2_DOMAIN_INDEX, 0x01<<2);
		}
	}

	if (_MALI_OSK_ERR_OK == _mali_osk_resource_find(global_gpu_base_address + 0x0E000, NULL)) {
		++pp_count_gr1;

		if (mali_is_mali400()) {
			mali_pmu_set_domain_mask(MALI_PP3_DOMAIN_INDEX, 0x01<<5);
		} else if (mali_is_mali450()) {
			mali_pmu_set_domain_mask(MALI_PP3_DOMAIN_INDEX, 0x01<<2);
		}
	}

	/* PP4 - PP7 */
	if (_MALI_OSK_ERR_OK == _mali_osk_resource_find(global_gpu_base_address + 0x28000, NULL)) {
		++pp_count_gr2;

		mali_pmu_set_domain_mask(MALI_PP4_DOMAIN_INDEX, 0x01<<3);
	}

	if (_MALI_OSK_ERR_OK == _mali_osk_resource_find(global_gpu_base_address + 0x2A000, NULL)) {
		++pp_count_gr2;

		mali_pmu_set_domain_mask(MALI_PP5_DOMAIN_INDEX, 0x01<<3);
	}

	if (_MALI_OSK_ERR_OK == _mali_osk_resource_find(global_gpu_base_address + 0x2C000, NULL)) {
		++pp_count_gr2;

		mali_pmu_set_domain_mask(MALI_PP6_DOMAIN_INDEX, 0x01<<3);
	}

	if (_MALI_OSK_ERR_OK == _mali_osk_resource_find(global_gpu_base_address + 0x2E000, NULL)) {
		++pp_count_gr2;

		mali_pmu_set_domain_mask(MALI_PP7_DOMAIN_INDEX, 0x01<<3);
	}

	/* L2gp/L2PP0/L2PP4 */
	if (_MALI_OSK_ERR_OK == _mali_osk_resource_find(global_gpu_base_address + 0x10000, NULL)) {
		++l2_count;

		if (mali_is_mali400()) {
			mali_pmu_set_domain_mask(MALI_L20_DOMAIN_INDEX, 0x01<<1);
		} else if (mali_is_mali450()) {
			mali_pmu_set_domain_mask(MALI_L20_DOMAIN_INDEX, 0x01<<0);
		}
	}

	if (_MALI_OSK_ERR_OK == _mali_osk_resource_find(global_gpu_base_address + 0x1000, NULL)) {
		++l2_count;

		mali_pmu_set_domain_mask(MALI_L21_DOMAIN_INDEX, 0x01<<1);
	}

	if (_MALI_OSK_ERR_OK == _mali_osk_resource_find(global_gpu_base_address + 0x11000, NULL)) {
		++l2_count;

		mali_pmu_set_domain_mask(MALI_L22_DOMAIN_INDEX, 0x01<<3);
	}

	MALI_DEBUG_PRINT(2, ("Using default PMU domain config: (%d) gr1_pp_cores, (%d) gr2_pp_cores, (%d) l2_count. \n", pp_count_gr1, pp_count_gr2, l2_count));
}

static void mali_set_pmu_global_domain_config(void)
{
	struct _mali_osk_device_data data = { 0, };
	int i = 0;

	if (_MALI_OSK_ERR_OK == _mali_osk_device_data_get(&data)) {
		/* Check whether has customized pmu domain configure */
		for (i = 0; i < MALI_MAX_NUMBER_OF_DOMAINS; i++) {
			if (0 != data.pmu_domain_config[i]) break;
		}

		if (MALI_MAX_NUMBER_OF_DOMAINS == i) {
			mali_use_default_pm_domain_config();
		} else {
			/* Copy the customer config to global config */
			mali_pmu_copy_domain_mask(data.pmu_domain_config, sizeof(data.pmu_domain_config));
		}
	}
}

static _mali_osk_errcode_t mali_parse_config_pmu(void)
{
	_mali_osk_resource_t resource_pmu;

	MALI_DEBUG_ASSERT(0 != global_gpu_base_address);

	if (_MALI_OSK_ERR_OK == _mali_osk_resource_find(global_gpu_base_address + 0x02000, &resource_pmu)) {
		struct mali_pmu_core *pmu;

		mali_set_pmu_global_domain_config();

		pmu = mali_pmu_create(&resource_pmu);
		if (NULL == pmu) {
			MALI_PRINT_ERROR(("Failed to create PMU\n"));
			return _MALI_OSK_ERR_FAULT;
		}
	}

	/* It's ok if the PMU doesn't exist */
	return _MALI_OSK_ERR_OK;
}
/*extern u32 ddr_get_cap(void);*/
static _mali_osk_errcode_t mali_parse_config_dma(void)
{
	_mali_osk_resource_t resource_dma;

	if (_MALI_OSK_ERR_OK == _mali_osk_resource_find(global_gpu_base_address + 0x12000, &resource_dma)) {
		if (NULL == mali_dma_create(&resource_dma)) {
			return _MALI_OSK_ERR_FAULT;
		}
		return _MALI_OSK_ERR_OK;
	} else {
		return _MALI_OSK_ERR_ITEM_NOT_FOUND;
	}
}

static _mali_osk_errcode_t mali_parse_config_memory(void)
{
	_mali_osk_errcode_t ret;

	if (0 == mali_dedicated_mem_start && 0 == mali_dedicated_mem_size && 0 == mali_shared_mem_size) {
		/* Memory settings are not overridden by module parameters, so use device settings */
		struct _mali_osk_device_data data = { 0, };

		if (_MALI_OSK_ERR_OK == _mali_osk_device_data_get(&data)) {
			/* Use device specific settings (if defined) */
			mali_dedicated_mem_start = data.dedicated_mem_start;
			mali_dedicated_mem_size = data.dedicated_mem_size;
/*
			mali_shared_mem_size = data.shared_mem_size;
*/
			mali_shared_mem_size = totalram_pages * 4 * 1024;
			/*ddr_get_cap();*/
		}

		if (0 == mali_dedicated_mem_start && 0 == mali_dedicated_mem_size && 0 == mali_shared_mem_size) {
			/* No GPU memory specified */
			return _MALI_OSK_ERR_INVALID_ARGS;
		}

		MALI_DEBUG_PRINT(2, ("Using device defined memory settings (dedicated: 0x%08X@0x%08X, shared: 0x%08X)\n",
		                     mali_dedicated_mem_size, mali_dedicated_mem_start, mali_shared_mem_size));
	} else {
		MALI_DEBUG_PRINT(2, ("Using module defined memory settings (dedicated: 0x%08X@0x%08X, shared: 0x%08X)\n",
		                     mali_dedicated_mem_size, mali_dedicated_mem_start, mali_shared_mem_size));
	}

	if (0 < mali_dedicated_mem_size && 0 != mali_dedicated_mem_start) {
		/* Dedicated memory */
		ret = mali_memory_core_resource_dedicated_memory(mali_dedicated_mem_start, mali_dedicated_mem_size);
		if (_MALI_OSK_ERR_OK != ret) {
			MALI_PRINT_ERROR(("Failed to register dedicated memory\n"));
			mali_memory_terminate();
			return ret;
		}
	}

	if (0 < mali_shared_mem_size) {
		/* Shared OS memory */
		ret = mali_memory_core_resource_os_memory(mali_shared_mem_size);
		if (_MALI_OSK_ERR_OK != ret) {
			MALI_PRINT_ERROR(("Failed to register shared OS memory\n"));
			mali_memory_terminate();
			return ret;
		}
	}

	if (0 == mali_fb_start && 0 == mali_fb_size) {
		/* Frame buffer settings are not overridden by module parameters, so use device settings */
		struct _mali_osk_device_data data = { 0, };

		if (_MALI_OSK_ERR_OK == _mali_osk_device_data_get(&data)) {
			/* Use device specific settings (if defined) */
			mali_fb_start = data.fb_start;
			mali_fb_size = data.fb_size;
		}

		MALI_DEBUG_PRINT(2, ("Using device defined frame buffer settings (0x%08X@0x%08X)\n",
		                     mali_fb_size, mali_fb_start));
	} else {
		MALI_DEBUG_PRINT(2, ("Using module defined frame buffer settings (0x%08X@0x%08X)\n",
		                     mali_fb_size, mali_fb_start));
	}

	if (0 != mali_fb_size) {
		/* Register frame buffer */
		ret = mali_mem_validation_add_range(mali_fb_start, mali_fb_size);
		if (_MALI_OSK_ERR_OK != ret) {
			MALI_PRINT_ERROR(("Failed to register frame buffer memory region\n"));
			mali_memory_terminate();
			return ret;
		}
	}

	return _MALI_OSK_ERR_OK;
}

static void mali_detect_gpu_class(void)
{
	u32 number_of_pp_cores = 0;
	u32 number_of_l2_caches = 0;

	mali_resource_count(&number_of_pp_cores, &number_of_l2_caches);
	if (number_of_l2_caches > 1) {
		mali_gpu_class_is_mali450 = MALI_TRUE;
	}
}

_mali_osk_errcode_t mali_initialize_subsystems(void)
{
	_mali_osk_errcode_t err;
	struct mali_pmu_core *pmu;

	mali_pp_job_initialize();

	err = mali_session_initialize();
	if (_MALI_OSK_ERR_OK != err) goto session_init_failed;

#if defined(CONFIG_MALI400_PROFILING)
	err = _mali_osk_profiling_init(mali_boot_profiling ? MALI_TRUE : MALI_FALSE);
	if (_MALI_OSK_ERR_OK != err) {
		/* No biggie if we weren't able to initialize the profiling */
		MALI_PRINT_ERROR(("Failed to initialize profiling, feature will be unavailable\n"));
	}
#endif

	err = mali_memory_initialize();
	if (_MALI_OSK_ERR_OK != err) goto memory_init_failed;

	/* Configure memory early. Memory allocation needed for mali_mmu_initialize. */
	err = mali_parse_config_memory();
	if (_MALI_OSK_ERR_OK != err) goto parse_memory_config_failed;

	err = mali_set_global_gpu_base_address();
	if (_MALI_OSK_ERR_OK != err) goto set_global_gpu_base_address_failed;

	/* Detect gpu class according to l2 cache number */
	mali_detect_gpu_class();

	err = mali_check_shared_interrupts();
	if (_MALI_OSK_ERR_OK != err) goto check_shared_interrupts_failed;

	err = mali_pp_scheduler_initialize();
	if (_MALI_OSK_ERR_OK != err) goto pp_scheduler_init_failed;

	/* Initialize the power management module */
	err = mali_pm_initialize();
	if (_MALI_OSK_ERR_OK != err) goto pm_init_failed;

	/* Initialize the MALI PMU */
	err = mali_parse_config_pmu();
	if (_MALI_OSK_ERR_OK != err) goto parse_pmu_config_failed;

	/* Make sure the power stays on for the rest of this function */
	err = _mali_osk_pm_dev_ref_add();
	if (_MALI_OSK_ERR_OK != err) goto pm_always_on_failed;

	/*
	 * If run-time PM is used, then the mali_pm module has now already been
	 * notified that the power now is on (through the resume callback functions).
	 * However, if run-time PM is not used, then there will probably not be any
	 * calls to the resume callback functions, so we need to explicitly tell it
	 * that the power is on.
	 */
	mali_pm_set_power_is_on();

	/* Reset PMU HW and ensure all Mali power domains are on */
	pmu = mali_pmu_get_global_pmu_core();
	if (NULL != pmu) {
		err = mali_pmu_reset(pmu);
		if (_MALI_OSK_ERR_OK != err) goto pmu_reset_failed;
	}

	/* Detect which Mali GPU we are dealing with */
	err = mali_parse_product_info();
	if (_MALI_OSK_ERR_OK != err) goto product_info_parsing_failed;

	/* The global_product_id is now populated with the correct Mali GPU */

	/* Create PM domains only if PMU exists */
	if (NULL != pmu) {
		err = mali_create_pm_domains();
		if (_MALI_OSK_ERR_OK != err) goto pm_domain_failed;
	}

	/* Initialize MMU module */
	err = mali_mmu_initialize();
	if (_MALI_OSK_ERR_OK != err) goto mmu_init_failed;

	if (mali_is_mali450()) {
		err = mali_dlbu_initialize();
		if (_MALI_OSK_ERR_OK != err) goto dlbu_init_failed;

		err = mali_parse_config_dma();
		if (_MALI_OSK_ERR_OK != err) goto dma_parsing_failed;
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

	/* PP scheduler population can't fail */
	mali_pp_scheduler_populate();

	/* Initialize the GPU utilization tracking */
	err = mali_utilization_init();
	if (_MALI_OSK_ERR_OK != err) goto utilization_init_failed;

	/* Allowing the system to be turned off */
	_mali_osk_pm_dev_ref_dec();

	MALI_SUCCESS; /* all ok */

	/* Error handling */

utilization_init_failed:
	mali_pp_scheduler_depopulate();
	mali_gp_scheduler_terminate();
gp_scheduler_init_failed:
	mali_scheduler_terminate();
scheduler_init_failed:
config_parsing_failed:
	mali_delete_groups(); /* Delete any groups not (yet) owned by a scheduler */
	mali_delete_l2_cache_cores(); /* Delete L2 cache cores even if config parsing failed. */
	{
		struct mali_dma_core *dma = mali_dma_get_global_dma_core();
		if (NULL != dma) mali_dma_delete(dma);
	}
dma_parsing_failed:
	mali_dlbu_terminate();
dlbu_init_failed:
	mali_mmu_terminate();
mmu_init_failed:
	mali_pm_domain_terminate();
pm_domain_failed:
	/* Nothing to roll back */
product_info_parsing_failed:
	/* Nothing to roll back */
pmu_reset_failed:
	/* Allowing the system to be turned off */
	_mali_osk_pm_dev_ref_dec();
pm_always_on_failed:
	pmu = mali_pmu_get_global_pmu_core();
	if (NULL != pmu) {
		mali_pmu_delete(pmu);
	}
parse_pmu_config_failed:
	mali_pm_terminate();
pm_init_failed:
	mali_pp_scheduler_terminate();
pp_scheduler_init_failed:
check_shared_interrupts_failed:
	global_gpu_base_address = 0;
set_global_gpu_base_address_failed:
	/* undoing mali_parse_config_memory() is done by mali_memory_terminate() */
parse_memory_config_failed:
	mali_memory_terminate();
memory_init_failed:
#if defined(CONFIG_MALI400_PROFILING)
	_mali_osk_profiling_term();
#endif
	mali_session_terminate();
session_init_failed:
	mali_pp_job_terminate();
	return err;
}

void mali_terminate_subsystems(void)
{
	struct mali_pmu_core *pmu = mali_pmu_get_global_pmu_core();
	struct mali_dma_core *dma = mali_dma_get_global_dma_core();

	MALI_DEBUG_PRINT(2, ("terminate_subsystems() called\n"));

	/* shut down subsystems in reverse order from startup */

	/* We need the GPU to be powered up for the terminate sequence */
	_mali_osk_pm_dev_ref_add();

	mali_utilization_term();
	mali_pp_scheduler_depopulate();
	mali_gp_scheduler_terminate();
	mali_scheduler_terminate();
	mali_delete_l2_cache_cores();
	if (mali_is_mali450()) {
		mali_dlbu_terminate();
	}
	mali_mmu_terminate();
	if (NULL != pmu) {
		mali_pmu_delete(pmu);
	}
	if (NULL != dma) {
		mali_dma_delete(dma);
	}
	mali_pm_terminate();
	mali_memory_terminate();
#if defined(CONFIG_MALI400_PROFILING)
	_mali_osk_profiling_term();
#endif

	/* Allowing the system to be turned off */
	_mali_osk_pm_dev_ref_dec();

	mali_pp_scheduler_terminate();
	mali_session_terminate();

	mali_pp_job_terminate();
}

_mali_product_id_t mali_kernel_core_get_product_id(void)
{
	return global_product_id;
}

u32 mali_kernel_core_get_gpu_major_version(void)
{
	return global_gpu_major_version;
}

u32 mali_kernel_core_get_gpu_minor_version(void)
{
	return global_gpu_minor_version;
}

_mali_osk_errcode_t _mali_ukk_get_api_version( _mali_uk_get_api_version_s *args )
{
	MALI_DEBUG_ASSERT_POINTER(args);
	MALI_CHECK_NON_NULL(args->ctx, _MALI_OSK_ERR_INVALID_ARGS);

	/* check compatability */
	if ( args->version == _MALI_UK_API_VERSION ) {
		args->compatible = 1;
	} else {
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
	if (NULL == queue) {
		MALI_DEBUG_PRINT(1, ("No notification queue registered with the session. Asking userspace to stop querying\n"));
		args->type = _MALI_NOTIFICATION_CORE_SHUTDOWN_IN_PROGRESS;
		MALI_SUCCESS;
	}

	/* receive a notification, might sleep */
	err = _mali_osk_notification_queue_receive(queue, &notification);
	if (_MALI_OSK_ERR_OK != err) {
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
	if (NULL == queue) {
		MALI_DEBUG_PRINT(1, ("No notification queue registered with the session. Asking userspace to stop querying\n"));
		MALI_SUCCESS;
	}

	notification = _mali_osk_notification_create(args->type, 0);
	if (NULL == notification) {
		MALI_PRINT_ERROR( ("Failed to create notification object\n"));
		return _MALI_OSK_ERR_NOMEM;
	}

	_mali_osk_notification_queue_send(queue, notification);

	MALI_SUCCESS; /* all ok */
}

_mali_osk_errcode_t _mali_ukk_request_high_priority( _mali_uk_request_high_priority_s *args )
{
	struct mali_session_data *session;

	MALI_DEBUG_ASSERT_POINTER(args);
	MALI_CHECK_NON_NULL(args->ctx, _MALI_OSK_ERR_INVALID_ARGS);

	session = (struct mali_session_data *) args->ctx;

	if (!session->use_high_priority_job_queue) {
		session->use_high_priority_job_queue = MALI_TRUE;
		MALI_DEBUG_PRINT(2, ("Session 0x%08X with pid %d was granted higher priority.\n", session, _mali_osk_get_pid()));
	}

	MALI_SUCCESS;
}

_mali_osk_errcode_t _mali_ukk_open(void **context)
{
	u32 i;
	struct mali_session_data *session;

	/* allocated struct to track this session */
	session = (struct mali_session_data *)_mali_osk_calloc(1, sizeof(struct mali_session_data));
	MALI_CHECK_NON_NULL(session, _MALI_OSK_ERR_NOMEM);

	MALI_DEBUG_PRINT(3, ("Session starting\n"));

	/* create a response queue for this session */
	session->ioctl_queue = _mali_osk_notification_queue_init();
	if (NULL == session->ioctl_queue) {
		_mali_osk_free(session);
		MALI_ERROR(_MALI_OSK_ERR_NOMEM);
	}

	session->page_directory = mali_mmu_pagedir_alloc();
	if (NULL == session->page_directory) {
		_mali_osk_notification_queue_term(session->ioctl_queue);
		_mali_osk_free(session);
		MALI_ERROR(_MALI_OSK_ERR_NOMEM);
	}

	if (_MALI_OSK_ERR_OK != mali_mmu_pagedir_map(session->page_directory, MALI_DLBU_VIRT_ADDR, _MALI_OSK_MALI_PAGE_SIZE)) {
		MALI_PRINT_ERROR(("Failed to map DLBU page into session\n"));
		_mali_osk_notification_queue_term(session->ioctl_queue);
		_mali_osk_free(session);
		MALI_ERROR(_MALI_OSK_ERR_NOMEM);
	}

	if (0 != mali_dlbu_phys_addr) {
		mali_mmu_pagedir_update(session->page_directory, MALI_DLBU_VIRT_ADDR, mali_dlbu_phys_addr,
		                        _MALI_OSK_MALI_PAGE_SIZE, MALI_MMU_FLAGS_DEFAULT);
	}

	if (_MALI_OSK_ERR_OK != mali_memory_session_begin(session)) {
		mali_mmu_pagedir_free(session->page_directory);
		_mali_osk_notification_queue_term(session->ioctl_queue);
		_mali_osk_free(session);
		MALI_ERROR(_MALI_OSK_ERR_NOMEM);
	}

	/* Create soft system. */
	session->soft_job_system = mali_soft_job_system_create(session);
	if (NULL == session->soft_job_system) {
		mali_memory_session_end(session);
		mali_mmu_pagedir_free(session->page_directory);
		_mali_osk_notification_queue_term(session->ioctl_queue);
		_mali_osk_free(session);
		MALI_ERROR(_MALI_OSK_ERR_NOMEM);
	}

	/* Create timeline system. */
	session->timeline_system = mali_timeline_system_create(session);
	if (NULL == session->timeline_system) {
		mali_soft_job_system_destroy(session->soft_job_system);
		mali_memory_session_end(session);
		mali_mmu_pagedir_free(session->page_directory);
		_mali_osk_notification_queue_term(session->ioctl_queue);
		_mali_osk_free(session);
		MALI_ERROR(_MALI_OSK_ERR_NOMEM);
	}

#if defined(CONFIG_MALI400_POWER_PERFORMANCE_POLICY)
	if ( _MALI_OSK_ERR_OK != _mali_osk_atomic_init(&session->number_of_window_jobs, 0)) {
		MALI_DEBUG_PRINT_ERROR(("Initialization of atomic number_of_window_jobs failed.\n"));
		mali_timeline_system_destroy(session->timeline_system);
		mali_soft_job_system_destroy(session->soft_job_system);
		mali_memory_session_end(session);
		mali_mmu_pagedir_free(session->page_directory);
		_mali_osk_notification_queue_term(session->ioctl_queue);
		_mali_osk_free(session);
		return _MALI_OSK_ERR_FAULT;
	}
#endif

	session->use_high_priority_job_queue = MALI_FALSE;

	/* Initialize list of PP jobs on this session. */
	_MALI_OSK_INIT_LIST_HEAD(&session->pp_job_list);

	/* Initialize the pp_job_fb_lookup_list array used to quickly lookup jobs from a given frame builder */
	for (i = 0; i < MALI_PP_JOB_FB_LOOKUP_LIST_SIZE; ++i) {
		_MALI_OSK_INIT_LIST_HEAD(&session->pp_job_fb_lookup_list[i]);
	}

	*context = (void*)session;

	/* Add session to the list of all sessions. */
	mali_session_add(session);

	MALI_DEBUG_PRINT(2, ("Session started\n"));
	MALI_SUCCESS;
}

_mali_osk_errcode_t _mali_ukk_close(void **context)
{
	struct mali_session_data *session;
	MALI_CHECK_NON_NULL(context, _MALI_OSK_ERR_INVALID_ARGS);
	session = (struct mali_session_data *)*context;

	MALI_DEBUG_PRINT(3, ("Session ending\n"));

	MALI_DEBUG_ASSERT_POINTER(session->soft_job_system);
	MALI_DEBUG_ASSERT_POINTER(session->timeline_system);

	/* Remove session from list of all sessions. */
	mali_session_remove(session);

	/* This flag is used to prevent queueing of jobs due to activation. */
	session->is_aborting = MALI_TRUE;

	/* Stop the soft job timer. */
	mali_timeline_system_stop_timer(session->timeline_system);

	/* Abort queued and running GP and PP jobs. */
	mali_gp_scheduler_abort_session(session);
	mali_pp_scheduler_abort_session(session);

	/* Abort the soft job system. */
	mali_soft_job_system_abort(session->soft_job_system);

	/* Force execution of all pending bottom half processing for GP and PP. */
	_mali_osk_wq_flush();

	/* The session PP list should now be empty. */
	MALI_DEBUG_ASSERT(_mali_osk_list_empty(&session->pp_job_list));

	/* At this point the GP and PP scheduler no longer has any jobs queued or running from this
	 * session, and all soft jobs in the soft job system has been destroyed. */

	/* Any trackers left in the timeline system are directly or indirectly waiting on external
	 * sync fences.  Cancel all sync fence waiters to trigger activation of all remaining
	 * trackers.  This call will sleep until all timelines are empty. */
	mali_timeline_system_abort(session->timeline_system);

	/* Flush pending work.
	 * Needed to make sure all bottom half processing related to this
	 * session has been completed, before we free internal data structures.
	 */
	_mali_osk_wq_flush();

	/* Destroy timeline system. */
	mali_timeline_system_destroy(session->timeline_system);
	session->timeline_system = NULL;

	/* Destroy soft system. */
	mali_soft_job_system_destroy(session->soft_job_system);
	session->soft_job_system = NULL;

	MALI_DEBUG_CODE( {
		/* Check that the pp_job_fb_lookup_list array is empty. */
		u32 i;
		for (i = 0; i < MALI_PP_JOB_FB_LOOKUP_LIST_SIZE; ++i)
		{
			MALI_DEBUG_ASSERT(_mali_osk_list_empty(&session->pp_job_fb_lookup_list[i]));
		}
	});

	/* Free remaining memory allocated to this session */
	mali_memory_session_end(session);

#if defined(CONFIG_MALI400_POWER_PERFORMANCE_POLICY)
	_mali_osk_atomic_term(&session->number_of_window_jobs);
#endif

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
