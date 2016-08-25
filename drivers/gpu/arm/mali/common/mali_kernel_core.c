/*
 * Copyright (C) 2010-2016 ARM Limited. All rights reserved.
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
#include "mali_executor.h"
#include "mali_pp_job.h"
#include "mali_group.h"
#include "mali_pm.h"
#include "mali_pmu.h"
#include "mali_scheduler.h"
#include "mali_kernel_utilization.h"
#include "mali_l2_cache.h"
#include "mali_timeline.h"
#include "mali_soft_job.h"
#include "mali_pm_domain.h"
#if defined(CONFIG_MALI400_PROFILING)
#include "mali_osk_profiling.h"
#endif
#if defined(CONFIG_MALI400_INTERNAL_PROFILING)
#include "mali_profiling_internal.h"
#endif
#include "mali_control_timer.h"
#include "mali_dvfs_policy.h"
#include <linux/sched.h>
#include <linux/atomic.h>
#if defined(CONFIG_MALI_DMA_BUF_FENCE)
#include <linux/fence.h>
#endif

#define MALI_SHARED_MEMORY_DEFAULT_SIZE 0xffffffff

/* Mali GPU memory. Real values come from module parameter or from device specific data */
unsigned int mali_dedicated_mem_start = 0;
unsigned int mali_dedicated_mem_size = 0;

/* Default shared memory size is set to 4G. */
unsigned int mali_shared_mem_size = MALI_SHARED_MEMORY_DEFAULT_SIZE;

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
static uintptr_t global_gpu_base_address = 0;
static u32 global_gpu_major_version = 0;
static u32 global_gpu_minor_version = 0;

mali_bool mali_gpu_class_is_mali450 = MALI_FALSE;
mali_bool mali_gpu_class_is_mali470 = MALI_FALSE;

static _mali_osk_errcode_t mali_set_global_gpu_base_address(void)
{
	_mali_osk_errcode_t err = _MALI_OSK_ERR_OK;

	global_gpu_base_address = _mali_osk_resource_base_address();
	if (0 == global_gpu_base_address) {
		err = _MALI_OSK_ERR_ITEM_NOT_FOUND;
	}

	return err;
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
	_mali_osk_resource_t first_pp_resource;

	/* Find the first PP core resource (again) */
	if (_MALI_OSK_ERR_OK == _mali_osk_resource_find(MALI_OFFSET_PP0, &first_pp_resource)) {
		/* Create a dummy PP object for this core so that we can read the version register */
		struct mali_group *group = mali_group_create(NULL, NULL, NULL, MALI_DOMAIN_INDEX_PP0);
		if (NULL != group) {
			struct mali_pp_core *pp_core = mali_pp_create(&first_pp_resource, group, MALI_FALSE, mali_get_bcast_id(&first_pp_resource));
			if (NULL != pp_core) {
				u32 pp_version;

				pp_version = mali_pp_core_get_version(pp_core);

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
				case MALI470_PP_PRODUCT_ID:
					global_product_id = _MALI_PRODUCT_ID_MALI470;
					MALI_DEBUG_PRINT(2, ("Found Mali GPU Mali-470 MP r%up%u\n", global_gpu_major_version, global_gpu_minor_version));
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

static struct mali_l2_cache_core *mali_create_l2_cache_core(_mali_osk_resource_t *resource, u32 domain_index)
{
	struct mali_l2_cache_core *l2_cache = NULL;

	if (NULL != resource) {

		MALI_DEBUG_PRINT(3, ("Found L2 cache %s\n", resource->description));

		l2_cache = mali_l2_cache_create(resource, domain_index);
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
		if (_MALI_OSK_ERR_OK != _mali_osk_resource_find(MALI400_OFFSET_L2_CACHE0, &l2_resource)) {
			MALI_DEBUG_PRINT(3, ("Did not find required Mali L2 cache in config file\n"));
			return _MALI_OSK_ERR_FAULT;
		}

		l2_cache = mali_create_l2_cache_core(&l2_resource, MALI_DOMAIN_INDEX_L20);
		if (NULL == l2_cache) {
			return _MALI_OSK_ERR_FAULT;
		}
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
		if (_MALI_OSK_ERR_OK == _mali_osk_resource_find(MALI450_OFFSET_L2_CACHE0, &l2_gp_resource)) {
			MALI_DEBUG_PRINT(3, ("Creating Mali-450 L2 cache core for GP\n"));
			l2_cache = mali_create_l2_cache_core(&l2_gp_resource, MALI_DOMAIN_INDEX_L20);
			if (NULL == l2_cache) {
				return _MALI_OSK_ERR_FAULT;
			}
		} else {
			MALI_DEBUG_PRINT(3, ("Did not find required Mali L2 cache for GP in config file\n"));
			return _MALI_OSK_ERR_FAULT;
		}

		/* Find corresponding l2 domain */
		if (_MALI_OSK_ERR_OK == _mali_osk_resource_find(MALI450_OFFSET_L2_CACHE1, &l2_pp_grp0_resource)) {
			MALI_DEBUG_PRINT(3, ("Creating Mali-450 L2 cache core for PP group 0\n"));
			l2_cache = mali_create_l2_cache_core(&l2_pp_grp0_resource, MALI_DOMAIN_INDEX_L21);
			if (NULL == l2_cache) {
				return _MALI_OSK_ERR_FAULT;
			}
		} else {
			MALI_DEBUG_PRINT(3, ("Did not find required Mali L2 cache for PP group 0 in config file\n"));
			return _MALI_OSK_ERR_FAULT;
		}

		/* Second PP core group is optional, don't fail if we don't find it */
		if (_MALI_OSK_ERR_OK == _mali_osk_resource_find(MALI450_OFFSET_L2_CACHE2, &l2_pp_grp1_resource)) {
			MALI_DEBUG_PRINT(3, ("Creating Mali-450 L2 cache core for PP group 1\n"));
			l2_cache = mali_create_l2_cache_core(&l2_pp_grp1_resource, MALI_DOMAIN_INDEX_L22);
			if (NULL == l2_cache) {
				return _MALI_OSK_ERR_FAULT;
			}
		}
	} else if (mali_is_mali470()) {
		_mali_osk_resource_t l2c1_resource;

		/* Make cluster for L2C1 */
		if (_MALI_OSK_ERR_OK == _mali_osk_resource_find(MALI470_OFFSET_L2_CACHE1, &l2c1_resource)) {
			MALI_DEBUG_PRINT(3, ("Creating Mali-470 L2 cache 1\n"));
			l2_cache = mali_create_l2_cache_core(&l2c1_resource, MALI_DOMAIN_INDEX_L21);
			if (NULL == l2_cache) {
				return _MALI_OSK_ERR_FAULT;
			}
		} else {
			MALI_DEBUG_PRINT(3, ("Did not find required Mali L2 cache for L2C1\n"));
			return _MALI_OSK_ERR_FAULT;
		}
	}

	return _MALI_OSK_ERR_OK;
}

static struct mali_group *mali_create_group(struct mali_l2_cache_core *cache,
		_mali_osk_resource_t *resource_mmu,
		_mali_osk_resource_t *resource_gp,
		_mali_osk_resource_t *resource_pp,
		u32 domain_index)
{
	struct mali_mmu_core *mmu;
	struct mali_group *group;

	MALI_DEBUG_PRINT(3, ("Starting new group for MMU %s\n", resource_mmu->description));

	/* Create the group object */
	group = mali_group_create(cache, NULL, NULL, domain_index);
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
#if defined(DEBUG)
	/* Get a physical PP group to temporarily add to broadcast unit.  IRQ
	 * verification needs a physical group in the broadcast unit to test
	 * the broadcast unit interrupt line. */
	{
		struct mali_group *phys_group = NULL;
		int i;
		for (i = 0; i < mali_group_get_glob_num_groups(); i++) {
			phys_group = mali_group_get_glob_group(i);
			if (NULL != mali_group_get_pp_core(phys_group)) break;
		}
		MALI_DEBUG_ASSERT(NULL != mali_group_get_pp_core(phys_group));

		/* Add the group temporarily to the broadcast, and update the
		 * broadcast HW. Since the HW is not updated when removing the
		 * group the IRQ check will work when the virtual PP is created
		 * later.
		 *
		 * When the virtual group gets populated, the actually used
		 * groups will be added to the broadcast unit and the HW will
		 * be updated.
		 */
		mali_bcast_add_group(bcast_core, phys_group);
		mali_bcast_reset(bcast_core);
		mali_bcast_remove_group(bcast_core, phys_group);
	}
#endif /* DEBUG */
	group = mali_group_create(NULL, dlbu_core, bcast_core, MALI_DOMAIN_INDEX_DUMMY);
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

	if (!(mali_is_mali400() || mali_is_mali450() || mali_is_mali470())) {
		/* No known HW core */
		return _MALI_OSK_ERR_FAULT;
	}

	if (MALI_MAX_JOB_RUNTIME_DEFAULT == mali_max_job_runtime) {
		/* Group settings are not overridden by module parameters, so use device settings */
		_mali_osk_device_data data = { 0, };

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

	resource_gp_found = _mali_osk_resource_find(MALI_OFFSET_GP, &resource_gp);
	resource_gp_mmu_found = _mali_osk_resource_find(MALI_OFFSET_GP_MMU, &resource_gp_mmu);
	resource_pp_found[0] = _mali_osk_resource_find(MALI_OFFSET_PP0, &(resource_pp[0]));
	resource_pp_found[1] = _mali_osk_resource_find(MALI_OFFSET_PP1, &(resource_pp[1]));
	resource_pp_found[2] = _mali_osk_resource_find(MALI_OFFSET_PP2, &(resource_pp[2]));
	resource_pp_found[3] = _mali_osk_resource_find(MALI_OFFSET_PP3, &(resource_pp[3]));
	resource_pp_found[4] = _mali_osk_resource_find(MALI_OFFSET_PP4, &(resource_pp[4]));
	resource_pp_found[5] = _mali_osk_resource_find(MALI_OFFSET_PP5, &(resource_pp[5]));
	resource_pp_found[6] = _mali_osk_resource_find(MALI_OFFSET_PP6, &(resource_pp[6]));
	resource_pp_found[7] = _mali_osk_resource_find(MALI_OFFSET_PP7, &(resource_pp[7]));
	resource_pp_mmu_found[0] = _mali_osk_resource_find(MALI_OFFSET_PP0_MMU, &(resource_pp_mmu[0]));
	resource_pp_mmu_found[1] = _mali_osk_resource_find(MALI_OFFSET_PP1_MMU, &(resource_pp_mmu[1]));
	resource_pp_mmu_found[2] = _mali_osk_resource_find(MALI_OFFSET_PP2_MMU, &(resource_pp_mmu[2]));
	resource_pp_mmu_found[3] = _mali_osk_resource_find(MALI_OFFSET_PP3_MMU, &(resource_pp_mmu[3]));
	resource_pp_mmu_found[4] = _mali_osk_resource_find(MALI_OFFSET_PP4_MMU, &(resource_pp_mmu[4]));
	resource_pp_mmu_found[5] = _mali_osk_resource_find(MALI_OFFSET_PP5_MMU, &(resource_pp_mmu[5]));
	resource_pp_mmu_found[6] = _mali_osk_resource_find(MALI_OFFSET_PP6_MMU, &(resource_pp_mmu[6]));
	resource_pp_mmu_found[7] = _mali_osk_resource_find(MALI_OFFSET_PP7_MMU, &(resource_pp_mmu[7]));


	if (mali_is_mali450() || mali_is_mali470()) {
		resource_bcast_found = _mali_osk_resource_find(MALI_OFFSET_BCAST, &resource_bcast);
		resource_dlbu_found = _mali_osk_resource_find(MALI_OFFSET_DLBU, &resource_dlbu);
		resource_pp_mmu_bcast_found = _mali_osk_resource_find(MALI_OFFSET_PP_BCAST_MMU, &resource_pp_mmu_bcast);
		resource_pp_bcast_found = _mali_osk_resource_find(MALI_OFFSET_PP_BCAST, &resource_pp_bcast);

		if (_MALI_OSK_ERR_OK != resource_bcast_found ||
		    _MALI_OSK_ERR_OK != resource_dlbu_found ||
		    _MALI_OSK_ERR_OK != resource_pp_mmu_bcast_found ||
		    _MALI_OSK_ERR_OK != resource_pp_bcast_found) {
			/* Missing mandatory core(s) for Mali-450 or Mali-470 */
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
	group = mali_create_group(mali_l2_cache_core_get_glob_l2_core(cluster_id_gp), &resource_gp_mmu, &resource_gp, NULL, MALI_DOMAIN_INDEX_GP);
	if (NULL == group) {
		return _MALI_OSK_ERR_FAULT;
	}

	/* Create group for first (and mandatory) PP core */
	MALI_DEBUG_ASSERT(mali_l2_cache_core_get_glob_num_l2_cores() >= (cluster_id_pp_grp0 + 1)); /* >= 1 on Mali-300 and Mali-400, >= 2 on Mali-450 */
	group = mali_create_group(mali_l2_cache_core_get_glob_l2_core(cluster_id_pp_grp0), &resource_pp_mmu[0], NULL, &resource_pp[0], MALI_DOMAIN_INDEX_PP0);
	if (NULL == group) {
		return _MALI_OSK_ERR_FAULT;
	}

	mali_inited_pp_cores_group_1++;

	/* Create groups for rest of the cores in the first PP core group */
	for (i = 1; i < 4; i++) { /* First half of the PP cores belong to first core group */
		if (mali_inited_pp_cores_group_1 < mali_max_pp_cores_group_1) {
			if (_MALI_OSK_ERR_OK == resource_pp_found[i] && _MALI_OSK_ERR_OK == resource_pp_mmu_found[i]) {
				group = mali_create_group(mali_l2_cache_core_get_glob_l2_core(cluster_id_pp_grp0), &resource_pp_mmu[i], NULL, &resource_pp[i], MALI_DOMAIN_INDEX_PP0 + i);
				if (NULL == group) {
					return _MALI_OSK_ERR_FAULT;
				}

				mali_inited_pp_cores_group_1++;
			}
		}
	}

	/* Create groups for cores in the second PP core group */
	for (i = 4; i < 8; i++) { /* Second half of the PP cores belong to second core group */
		if (mali_inited_pp_cores_group_2 < mali_max_pp_cores_group_2) {
			if (_MALI_OSK_ERR_OK == resource_pp_found[i] && _MALI_OSK_ERR_OK == resource_pp_mmu_found[i]) {
				MALI_DEBUG_ASSERT(mali_l2_cache_core_get_glob_num_l2_cores() >= 2); /* Only Mali-450 have a second core group */
				group = mali_create_group(mali_l2_cache_core_get_glob_l2_core(cluster_id_pp_grp1), &resource_pp_mmu[i], NULL, &resource_pp[i], MALI_DOMAIN_INDEX_PP0 + i);
				if (NULL == group) {
					return _MALI_OSK_ERR_FAULT;
				}

				mali_inited_pp_cores_group_2++;
			}
		}
	}

	if (mali_is_mali450() || mali_is_mali470()) {
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

static _mali_osk_errcode_t mali_parse_config_pmu(void)
{
	_mali_osk_resource_t resource_pmu;

	MALI_DEBUG_ASSERT(0 != global_gpu_base_address);

	if (_MALI_OSK_ERR_OK == _mali_osk_resource_find(MALI_OFFSET_PMU, &resource_pmu)) {
		struct mali_pmu_core *pmu;

		pmu = mali_pmu_create(&resource_pmu);
		if (NULL == pmu) {
			MALI_PRINT_ERROR(("Failed to create PMU\n"));
			return _MALI_OSK_ERR_FAULT;
		}
	}

	/* It's ok if the PMU doesn't exist */
	return _MALI_OSK_ERR_OK;
}

static _mali_osk_errcode_t mali_parse_config_memory(void)
{
	_mali_osk_device_data data = { 0, };
	_mali_osk_errcode_t ret;

	/* The priority of setting the value of mali_shared_mem_size,
	 * mali_dedicated_mem_start and mali_dedicated_mem_size:
	 * 1. module parameter;
	 * 2. platform data;
	 * 3. default value;
	 **/
	if (_MALI_OSK_ERR_OK == _mali_osk_device_data_get(&data)) {
		/* Memory settings are not overridden by module parameters, so use device settings */
		if (0 == mali_dedicated_mem_start && 0 == mali_dedicated_mem_size) {
			/* Use device specific settings (if defined) */
			mali_dedicated_mem_start = data.dedicated_mem_start;
			mali_dedicated_mem_size = data.dedicated_mem_size;
		}

		if (MALI_SHARED_MEMORY_DEFAULT_SIZE == mali_shared_mem_size &&
		    0 != data.shared_mem_size) {
			mali_shared_mem_size = data.shared_mem_size;
		}
	}

	if (0 < mali_dedicated_mem_size && 0 != mali_dedicated_mem_start) {
		MALI_DEBUG_PRINT(2, ("Mali memory settings (dedicated: 0x%08X@0x%08X)\n",
				     mali_dedicated_mem_size, mali_dedicated_mem_start));

		/* Dedicated memory */
		ret = mali_memory_core_resource_dedicated_memory(mali_dedicated_mem_start, mali_dedicated_mem_size);
		if (_MALI_OSK_ERR_OK != ret) {
			MALI_PRINT_ERROR(("Failed to register dedicated memory\n"));
			mali_memory_terminate();
			return ret;
		}
	}

	if (0 < mali_shared_mem_size) {
		MALI_DEBUG_PRINT(2, ("Mali memory settings (shared: 0x%08X)\n", mali_shared_mem_size));

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
		_mali_osk_device_data data = { 0, };

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
	if (_mali_osk_identify_gpu_resource() == 0x450)
		mali_gpu_class_is_mali450 = MALI_TRUE;

	if (_mali_osk_identify_gpu_resource() == 0x470)
		mali_gpu_class_is_mali470 = MALI_TRUE;
}

static _mali_osk_errcode_t mali_init_hw_reset(void)
{
#if (defined(CONFIG_MALI450) || defined(CONFIG_MALI470))
	_mali_osk_resource_t resource_bcast;

	/* Ensure broadcast unit is in a good state before we start creating
	 * groups and cores.
	 */
	if (_MALI_OSK_ERR_OK == _mali_osk_resource_find(MALI_OFFSET_BCAST, &resource_bcast)) {
		struct mali_bcast_unit *bcast_core;

		bcast_core = mali_bcast_unit_create(&resource_bcast);
		if (NULL == bcast_core) {
			MALI_PRINT_ERROR(("Failed to create Broadcast unit object!\n"));
			return _MALI_OSK_ERR_FAULT;
		}
		mali_bcast_unit_delete(bcast_core);
	}
#endif /* (defined(CONFIG_MALI450) || defined(CONFIG_MALI470)) */

	return _MALI_OSK_ERR_OK;
}

_mali_osk_errcode_t mali_initialize_subsystems(void)
{
	_mali_osk_errcode_t err;

#ifdef CONFIG_MALI_DT
	err = _mali_osk_resource_initialize();
	if (_MALI_OSK_ERR_OK != err) {
		mali_terminate_subsystems();
		return err;
	}
#endif

	mali_pp_job_initialize();

	mali_timeline_initialize();

	err = mali_session_initialize();
	if (_MALI_OSK_ERR_OK != err) {
		mali_terminate_subsystems();
		return err;
	}

	/*Try to init gpu secure mode */
	_mali_osk_gpu_secure_mode_init();

#if defined(CONFIG_MALI400_PROFILING)
	err = _mali_osk_profiling_init(mali_boot_profiling ? MALI_TRUE : MALI_FALSE);
	if (_MALI_OSK_ERR_OK != err) {
		/* No biggie if we weren't able to initialize the profiling */
		MALI_PRINT_ERROR(("Failed to initialize profiling, feature will be unavailable\n"));
	}
#endif

	err = mali_memory_initialize();
	if (_MALI_OSK_ERR_OK != err) {
		mali_terminate_subsystems();
		return err;
	}

	err = mali_executor_initialize();
	if (_MALI_OSK_ERR_OK != err) {
		mali_terminate_subsystems();
		return err;
	}

	err = mali_scheduler_initialize();
	if (_MALI_OSK_ERR_OK != err) {
		mali_terminate_subsystems();
		return err;
	}

	/* Configure memory early, needed by mali_mmu_initialize. */
	err = mali_parse_config_memory();
	if (_MALI_OSK_ERR_OK != err) {
		mali_terminate_subsystems();
		return err;
	}

	err = mali_set_global_gpu_base_address();
	if (_MALI_OSK_ERR_OK != err) {
		mali_terminate_subsystems();
		return err;
	}

	/* Detect GPU class (uses L2 cache count) */
	mali_detect_gpu_class();

	err = mali_check_shared_interrupts();
	if (_MALI_OSK_ERR_OK != err) {
		mali_terminate_subsystems();
		return err;
	}

	/* Initialize the MALI PMU (will not touch HW!) */
	err = mali_parse_config_pmu();
	if (_MALI_OSK_ERR_OK != err) {
		mali_terminate_subsystems();
		return err;
	}

	/* Initialize the power management module */
	err = mali_pm_initialize();
	if (_MALI_OSK_ERR_OK != err) {
		mali_terminate_subsystems();
		return err;
	}

	/* Make sure the entire GPU stays on for the rest of this function */
	mali_pm_init_begin();

	/* Ensure HW is in a good state before starting to access cores. */
	err = mali_init_hw_reset();
	if (_MALI_OSK_ERR_OK != err) {
		mali_terminate_subsystems();
		return err;
	}

	/* Detect which Mali GPU we are dealing with */
	err = mali_parse_product_info();
	if (_MALI_OSK_ERR_OK != err) {
		mali_pm_init_end();
		mali_terminate_subsystems();
		return err;
	}

	/* The global_product_id is now populated with the correct Mali GPU */

	/* Start configuring the actual Mali hardware. */

	err = mali_mmu_initialize();
	if (_MALI_OSK_ERR_OK != err) {
		mali_pm_init_end();
		mali_terminate_subsystems();
		return err;
	}

	if (mali_is_mali450() || mali_is_mali470()) {
		err = mali_dlbu_initialize();
		if (_MALI_OSK_ERR_OK != err) {
			mali_pm_init_end();
			mali_terminate_subsystems();
			return err;
		}
	}

	err = mali_parse_config_l2_cache();
	if (_MALI_OSK_ERR_OK != err) {
		mali_pm_init_end();
		mali_terminate_subsystems();
		return err;
	}

	err = mali_parse_config_groups();
	if (_MALI_OSK_ERR_OK != err) {
		mali_pm_init_end();
		mali_terminate_subsystems();
		return err;
	}

	/* Move groups into executor */
	mali_executor_populate();

	/* Need call after all group has assigned a domain */
	mali_pm_power_cost_setup();

	/* Initialize the GPU timer */
	err = mali_control_timer_init();
	if (_MALI_OSK_ERR_OK != err) {
		mali_pm_init_end();
		mali_terminate_subsystems();
		return err;
	}

	/* Initialize the GPU utilization tracking */
	err = mali_utilization_init();
	if (_MALI_OSK_ERR_OK != err) {
		mali_pm_init_end();
		mali_terminate_subsystems();
		return err;
	}

#if defined(CONFIG_MALI_DVFS)
	err = mali_dvfs_policy_init();
	if (_MALI_OSK_ERR_OK != err) {
		mali_pm_init_end();
		mali_terminate_subsystems();
		return err;
	}
#endif

	/* Allowing the system to be turned off */
	mali_pm_init_end();

	return _MALI_OSK_ERR_OK; /* all ok */
}

void mali_terminate_subsystems(void)
{
	struct mali_pmu_core *pmu = mali_pmu_get_global_pmu_core();

	MALI_DEBUG_PRINT(2, ("terminate_subsystems() called\n"));

	mali_utilization_term();
	mali_control_timer_term();

	mali_executor_depopulate();
	mali_delete_groups(); /* Delete groups not added to executor */
	mali_executor_terminate();

	mali_scheduler_terminate();
	mali_pp_job_terminate();
	mali_delete_l2_cache_cores();
	mali_mmu_terminate();

	if (mali_is_mali450() || mali_is_mali470()) {
		mali_dlbu_terminate();
	}

	mali_pm_terminate();

	if (NULL != pmu) {
		mali_pmu_delete(pmu);
	}

#if defined(CONFIG_MALI400_PROFILING)
	_mali_osk_profiling_term();
#endif

	_mali_osk_gpu_secure_mode_deinit();

	mali_memory_terminate();

	mali_session_terminate();

	mali_timeline_terminate();

	global_gpu_base_address = 0;
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

_mali_osk_errcode_t _mali_ukk_get_api_version(_mali_uk_get_api_version_s *args)
{
	MALI_DEBUG_ASSERT_POINTER(args);
	MALI_DEBUG_ASSERT(NULL != (void *)(uintptr_t)args->ctx);

	/* check compatability */
	if (args->version == _MALI_UK_API_VERSION) {
		args->compatible = 1;
	} else {
		args->compatible = 0;
	}

	args->version = _MALI_UK_API_VERSION; /* report our version */

	/* success regardless of being compatible or not */
	MALI_SUCCESS;
}

_mali_osk_errcode_t _mali_ukk_get_api_version_v2(_mali_uk_get_api_version_v2_s *args)
{
	MALI_DEBUG_ASSERT_POINTER(args);
	MALI_DEBUG_ASSERT(NULL != (void *)(uintptr_t)args->ctx);

	/* check compatability */
	if (args->version == _MALI_UK_API_VERSION) {
		args->compatible = 1;
	} else {
		args->compatible = 0;
	}

	args->version = _MALI_UK_API_VERSION; /* report our version */

	/* success regardless of being compatible or not */
	return _MALI_OSK_ERR_OK;
}

_mali_osk_errcode_t _mali_ukk_wait_for_notification(_mali_uk_wait_for_notification_s *args)
{
	_mali_osk_errcode_t err;
	_mali_osk_notification_t *notification;
	_mali_osk_notification_queue_t *queue;
	struct mali_session_data *session;

	/* check input */
	MALI_DEBUG_ASSERT_POINTER(args);
	MALI_DEBUG_ASSERT(NULL != (void *)(uintptr_t)args->ctx);

	session = (struct mali_session_data *)(uintptr_t)args->ctx;
	queue = session->ioctl_queue;

	/* if the queue does not exist we're currently shutting down */
	if (NULL == queue) {
		MALI_DEBUG_PRINT(1, ("No notification queue registered with the session. Asking userspace to stop querying\n"));
		args->type = _MALI_NOTIFICATION_CORE_SHUTDOWN_IN_PROGRESS;
		return _MALI_OSK_ERR_OK;
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
	_mali_osk_notification_delete(notification);

	return _MALI_OSK_ERR_OK; /* all ok */
}

_mali_osk_errcode_t _mali_ukk_post_notification(_mali_uk_post_notification_s *args)
{
	_mali_osk_notification_t *notification;
	_mali_osk_notification_queue_t *queue;
	struct mali_session_data *session;

	/* check input */
	MALI_DEBUG_ASSERT_POINTER(args);
	MALI_DEBUG_ASSERT(NULL != (void *)(uintptr_t)args->ctx);

	session = (struct mali_session_data *)(uintptr_t)args->ctx;
	queue = session->ioctl_queue;

	/* if the queue does not exist we're currently shutting down */
	if (NULL == queue) {
		MALI_DEBUG_PRINT(1, ("No notification queue registered with the session. Asking userspace to stop querying\n"));
		return _MALI_OSK_ERR_OK;
	}

	notification = _mali_osk_notification_create(args->type, 0);
	if (NULL == notification) {
		MALI_PRINT_ERROR(("Failed to create notification object\n"));
		return _MALI_OSK_ERR_NOMEM;
	}

	_mali_osk_notification_queue_send(queue, notification);

	return _MALI_OSK_ERR_OK; /* all ok */
}

_mali_osk_errcode_t _mali_ukk_pending_submit(_mali_uk_pending_submit_s *args)
{
	wait_queue_head_t *queue;

	/* check input */
	MALI_DEBUG_ASSERT_POINTER(args);
	MALI_DEBUG_ASSERT(NULL != (void *)(uintptr_t)args->ctx);

	queue = mali_session_get_wait_queue();

	/* check pending big job number, might sleep if larger than MAX allowed number */
	if (wait_event_interruptible(*queue, MALI_MAX_PENDING_BIG_JOB > mali_scheduler_job_gp_big_job_count())) {
		return _MALI_OSK_ERR_RESTARTSYSCALL;
	}

	return _MALI_OSK_ERR_OK; /* all ok */
}


_mali_osk_errcode_t _mali_ukk_request_high_priority(_mali_uk_request_high_priority_s *args)
{
	struct mali_session_data *session;

	MALI_DEBUG_ASSERT_POINTER(args);
	MALI_DEBUG_ASSERT(NULL != (void *)(uintptr_t)args->ctx);

	session = (struct mali_session_data *)(uintptr_t)args->ctx;

	if (!session->use_high_priority_job_queue) {
		session->use_high_priority_job_queue = MALI_TRUE;
		MALI_DEBUG_PRINT(2, ("Session 0x%08X with pid %d was granted higher priority.\n", session, _mali_osk_get_pid()));
	}

	return _MALI_OSK_ERR_OK;
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
		goto err;
	}

	session->page_directory = mali_mmu_pagedir_alloc();
	if (NULL == session->page_directory) {
		goto err_mmu;
	}

	if (_MALI_OSK_ERR_OK != mali_mmu_pagedir_map(session->page_directory, MALI_DLBU_VIRT_ADDR, _MALI_OSK_MALI_PAGE_SIZE)) {
		MALI_PRINT_ERROR(("Failed to map DLBU page into session\n"));
		goto err_mmu;
	}

	if (0 != mali_dlbu_phys_addr) {
		mali_mmu_pagedir_update(session->page_directory, MALI_DLBU_VIRT_ADDR, mali_dlbu_phys_addr,
					_MALI_OSK_MALI_PAGE_SIZE, MALI_MMU_FLAGS_DEFAULT);
	}

	if (_MALI_OSK_ERR_OK != mali_memory_session_begin(session)) {
		goto err_session;
	}

	/* Create soft system. */
	session->soft_job_system = mali_soft_job_system_create(session);
	if (NULL == session->soft_job_system) {
		goto err_soft;
	}

	/* Initialize the dma fence context.*/
#if defined(CONFIG_MALI_DMA_BUF_FENCE)
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 17, 0)
	session->fence_context = fence_context_alloc(1);
	_mali_osk_atomic_init(&session->fence_seqno, 0);
#else
	MALI_PRINT_ERROR(("The kernel version not support dma fence!\n"));
	goto err_time_line;
#endif
#endif

	/* Create timeline system. */
	session->timeline_system = mali_timeline_system_create(session);
	if (NULL == session->timeline_system) {
		goto err_time_line;
	}

#if defined(CONFIG_MALI_DVFS)
	_mali_osk_atomic_init(&session->number_of_window_jobs, 0);
#endif

	session->use_high_priority_job_queue = MALI_FALSE;

	/* Initialize list of PP jobs on this session. */
	_MALI_OSK_INIT_LIST_HEAD(&session->pp_job_list);

	/* Initialize the pp_job_fb_lookup_list array used to quickly lookup jobs from a given frame builder */
	for (i = 0; i < MALI_PP_JOB_FB_LOOKUP_LIST_SIZE; ++i) {
		_MALI_OSK_INIT_LIST_HEAD(&session->pp_job_fb_lookup_list[i]);
	}

	session->pid = _mali_osk_get_pid();
	session->comm = _mali_osk_get_comm();
	session->max_mali_mem_allocated_size = 0;
	for (i = 0; i < MALI_MEM_TYPE_MAX; i ++) {
		atomic_set(&session->mali_mem_array[i], 0);
	}
	atomic_set(&session->mali_mem_allocated_pages, 0);
	*context = (void *)session;

	/* Add session to the list of all sessions. */
	mali_session_add(session);

	MALI_DEBUG_PRINT(3, ("Session started\n"));
	return _MALI_OSK_ERR_OK;

err_time_line:
	mali_soft_job_system_destroy(session->soft_job_system);
err_soft:
	mali_memory_session_end(session);
err_session:
	mali_mmu_pagedir_free(session->page_directory);
err_mmu:
	_mali_osk_notification_queue_term(session->ioctl_queue);
err:
	_mali_osk_free(session);
	MALI_ERROR(_MALI_OSK_ERR_NOMEM);

}

#if defined(DEBUG)
/* parameter used for debug */
extern u32 num_pm_runtime_resume;
extern u32 num_pm_updates;
extern u32 num_pm_updates_up;
extern u32 num_pm_updates_down;
#endif

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

	/* Abort queued jobs */
	mali_scheduler_abort_session(session);

	/* Abort executing jobs */
	mali_executor_abort_session(session);

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

	MALI_DEBUG_CODE({
		/* Check that the pp_job_fb_lookup_list array is empty. */
		u32 i;
		for (i = 0; i < MALI_PP_JOB_FB_LOOKUP_LIST_SIZE; ++i)
		{
			MALI_DEBUG_ASSERT(_mali_osk_list_empty(&session->pp_job_fb_lookup_list[i]));
		}
	});

	/* Free remaining memory allocated to this session */
	mali_memory_session_end(session);

#if defined(CONFIG_MALI_DVFS)
	_mali_osk_atomic_term(&session->number_of_window_jobs);
#endif

#if defined(CONFIG_MALI400_PROFILING)
	_mali_osk_profiling_stop_sampling(session->pid);
#endif

	/* Free session data structures */
	mali_mmu_pagedir_unmap(session->page_directory, MALI_DLBU_VIRT_ADDR, _MALI_OSK_MALI_PAGE_SIZE);
	mali_mmu_pagedir_free(session->page_directory);
	_mali_osk_notification_queue_term(session->ioctl_queue);
	_mali_osk_free(session);

	*context = NULL;

	MALI_DEBUG_PRINT(3, ("Session has ended\n"));

#if defined(DEBUG)
	MALI_DEBUG_PRINT(3, ("Stats: # runtime resumes: %u\n", num_pm_runtime_resume));
	MALI_DEBUG_PRINT(3, ("       # PM updates: .... %u (up %u, down %u)\n", num_pm_updates, num_pm_updates_up, num_pm_updates_down));

	num_pm_runtime_resume = 0;
	num_pm_updates = 0;
	num_pm_updates_up = 0;
	num_pm_updates_down = 0;
#endif

	return _MALI_OSK_ERR_OK;;
}

#if MALI_STATE_TRACKING
u32 _mali_kernel_core_dump_state(char *buf, u32 size)
{
	int n = 0; /* Number of bytes written to buf */

	n += mali_scheduler_dump_state(buf + n, size - n);
	n += mali_executor_dump_state(buf + n, size - n);

	return n;
}
#endif
