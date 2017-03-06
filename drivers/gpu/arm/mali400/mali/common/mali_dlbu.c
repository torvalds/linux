/*
 * Copyright (C) 2012-2014, 2016-2017 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include "mali_dlbu.h"
#include "mali_memory.h"
#include "mali_pp.h"
#include "mali_group.h"
#include "mali_osk.h"
#include "mali_hw_core.h"

/**
 * Size of DLBU registers in bytes
 */
#define MALI_DLBU_SIZE 0x400

mali_dma_addr mali_dlbu_phys_addr = 0;
static mali_io_address mali_dlbu_cpu_addr = NULL;

/**
 * DLBU register numbers
 * Used in the register read/write routines.
 * See the hardware documentation for more information about each register
 */
typedef enum mali_dlbu_register {
	MALI_DLBU_REGISTER_MASTER_TLLIST_PHYS_ADDR = 0x0000, /**< Master tile list physical base address;
                                                             31:12 Physical address to the page used for the DLBU
                                                             0 DLBU enable - set this bit to 1 enables the AXI bus
                                                             between PPs and L2s, setting to 0 disables the router and
                                                             no further transactions are sent to DLBU */
	MALI_DLBU_REGISTER_MASTER_TLLIST_VADDR     = 0x0004, /**< Master tile list virtual base address;
                                                             31:12 Virtual address to the page used for the DLBU */
	MALI_DLBU_REGISTER_TLLIST_VBASEADDR     = 0x0008, /**< Tile list virtual base address;
                                                             31:12 Virtual address to the tile list. This address is used when
                                                             calculating the call address sent to PP.*/
	MALI_DLBU_REGISTER_FB_DIM                 = 0x000C, /**< Framebuffer dimension;
                                                             23:16 Number of tiles in Y direction-1
                                                             7:0 Number of tiles in X direction-1 */
	MALI_DLBU_REGISTER_TLLIST_CONF       = 0x0010, /**< Tile list configuration;
                                                             29:28 select the size of each allocated block: 0=128 bytes, 1=256, 2=512, 3=1024
                                                             21:16 2^n number of tiles to be binned to one tile list in Y direction
                                                             5:0 2^n number of tiles to be binned to one tile list in X direction */
	MALI_DLBU_REGISTER_START_TILE_POS         = 0x0014, /**< Start tile positions;
                                                             31:24 start position in Y direction for group 1
                                                             23:16 start position in X direction for group 1
                                                             15:8 start position in Y direction for group 0
                                                             7:0 start position in X direction for group 0 */
	MALI_DLBU_REGISTER_PP_ENABLE_MASK         = 0x0018, /**< PP enable mask;
                                                             7 enable PP7 for load balancing
                                                             6 enable PP6 for load balancing
                                                             5 enable PP5 for load balancing
                                                             4 enable PP4 for load balancing
                                                             3 enable PP3 for load balancing
                                                             2 enable PP2 for load balancing
                                                             1 enable PP1 for load balancing
                                                             0 enable PP0 for load balancing */
} mali_dlbu_register;

typedef enum {
	PP0ENABLE = 0,
	PP1ENABLE,
	PP2ENABLE,
	PP3ENABLE,
	PP4ENABLE,
	PP5ENABLE,
	PP6ENABLE,
	PP7ENABLE
} mali_dlbu_pp_enable;

struct mali_dlbu_core {
	struct mali_hw_core     hw_core;           /**< Common for all HW cores */
	u32                     pp_cores_mask;     /**< This is a mask for the PP cores whose operation will be controlled by LBU
                                                      see MALI_DLBU_REGISTER_PP_ENABLE_MASK register */
};

_mali_osk_errcode_t mali_dlbu_initialize(void)
{
	MALI_DEBUG_PRINT(2, ("Mali DLBU: Initializing\n"));

	if (_MALI_OSK_ERR_OK ==
	    mali_mmu_get_table_page(&mali_dlbu_phys_addr,
				    &mali_dlbu_cpu_addr)) {
		return _MALI_OSK_ERR_OK;
	}

	return _MALI_OSK_ERR_FAULT;
}

void mali_dlbu_terminate(void)
{
	MALI_DEBUG_PRINT(3, ("Mali DLBU: terminating\n"));

	if (0 != mali_dlbu_phys_addr && 0 != mali_dlbu_cpu_addr) {
		mali_mmu_release_table_page(mali_dlbu_phys_addr,
					    mali_dlbu_cpu_addr);
		mali_dlbu_phys_addr = 0;
		mali_dlbu_cpu_addr = 0;
	}
}

struct mali_dlbu_core *mali_dlbu_create(const _mali_osk_resource_t *resource)
{
	struct mali_dlbu_core *core = NULL;

	MALI_DEBUG_PRINT(2, ("Mali DLBU: Creating Mali dynamic load balancing unit: %s\n", resource->description));

	core = _mali_osk_malloc(sizeof(struct mali_dlbu_core));
	if (NULL != core) {
		if (_MALI_OSK_ERR_OK == mali_hw_core_create(&core->hw_core, resource, MALI_DLBU_SIZE)) {
			core->pp_cores_mask = 0;
			if (_MALI_OSK_ERR_OK == mali_dlbu_reset(core)) {
				return core;
			}
			MALI_PRINT_ERROR(("Failed to reset DLBU %s\n", core->hw_core.description));
			mali_hw_core_delete(&core->hw_core);
		}

		_mali_osk_free(core);
	} else {
		MALI_PRINT_ERROR(("Mali DLBU: Failed to allocate memory for DLBU core\n"));
	}

	return NULL;
}

void mali_dlbu_delete(struct mali_dlbu_core *dlbu)
{
	MALI_DEBUG_ASSERT_POINTER(dlbu);
	mali_hw_core_delete(&dlbu->hw_core);
	_mali_osk_free(dlbu);
}

_mali_osk_errcode_t mali_dlbu_reset(struct mali_dlbu_core *dlbu)
{
	u32 dlbu_registers[7];
	_mali_osk_errcode_t err = _MALI_OSK_ERR_FAULT;
	MALI_DEBUG_ASSERT_POINTER(dlbu);

	MALI_DEBUG_PRINT(4, ("Mali DLBU: mali_dlbu_reset: %s\n", dlbu->hw_core.description));

	dlbu_registers[0] = mali_dlbu_phys_addr | 1; /* bit 0 enables the whole core */
	dlbu_registers[1] = MALI_DLBU_VIRT_ADDR;
	dlbu_registers[2] = 0;
	dlbu_registers[3] = 0;
	dlbu_registers[4] = 0;
	dlbu_registers[5] = 0;
	dlbu_registers[6] = dlbu->pp_cores_mask;

	/* write reset values to core registers */
	mali_hw_core_register_write_array_relaxed(&dlbu->hw_core, MALI_DLBU_REGISTER_MASTER_TLLIST_PHYS_ADDR, dlbu_registers, 7);

	err = _MALI_OSK_ERR_OK;

	return err;
}

void mali_dlbu_update_mask(struct mali_dlbu_core *dlbu)
{
	MALI_DEBUG_ASSERT_POINTER(dlbu);

	mali_hw_core_register_write(&dlbu->hw_core, MALI_DLBU_REGISTER_PP_ENABLE_MASK, dlbu->pp_cores_mask);
}

void mali_dlbu_add_group(struct mali_dlbu_core *dlbu, struct mali_group *group)
{
	struct mali_pp_core *pp_core;
	u32 bcast_id;

	MALI_DEBUG_ASSERT_POINTER(dlbu);
	MALI_DEBUG_ASSERT_POINTER(group);

	pp_core = mali_group_get_pp_core(group);
	bcast_id = mali_pp_core_get_bcast_id(pp_core);

	dlbu->pp_cores_mask |= bcast_id;
	MALI_DEBUG_PRINT(3, ("Mali DLBU: Adding core[%d] New mask= 0x%02x\n", bcast_id , dlbu->pp_cores_mask));
}

/* Remove a group from the DLBU */
void mali_dlbu_remove_group(struct mali_dlbu_core *dlbu, struct mali_group *group)
{
	struct mali_pp_core *pp_core;
	u32 bcast_id;

	MALI_DEBUG_ASSERT_POINTER(dlbu);
	MALI_DEBUG_ASSERT_POINTER(group);

	pp_core = mali_group_get_pp_core(group);
	bcast_id = mali_pp_core_get_bcast_id(pp_core);

	dlbu->pp_cores_mask &= ~bcast_id;
	MALI_DEBUG_PRINT(3, ("Mali DLBU: Removing core[%d] New mask= 0x%02x\n", bcast_id, dlbu->pp_cores_mask));
}

/* Configure the DLBU for \a job. This needs to be done before the job is started on the groups in the DLBU. */
void mali_dlbu_config_job(struct mali_dlbu_core *dlbu, struct mali_pp_job *job)
{
	u32 *registers;
	MALI_DEBUG_ASSERT(job);
	registers = mali_pp_job_get_dlbu_registers(job);
	MALI_DEBUG_PRINT(4, ("Mali DLBU: Starting job\n"));

	/* Writing 4 registers:
	 * DLBU registers except the first two (written once at DLBU initialisation / reset) and the PP_ENABLE_MASK register */
	mali_hw_core_register_write_array_relaxed(&dlbu->hw_core, MALI_DLBU_REGISTER_TLLIST_VBASEADDR, registers, 4);

}
