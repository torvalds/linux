/*
 * Copyright (C) 2012 ARM Limited. All rights reserved.
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

u32 mali_dlbu_phys_addr = 0;
static mali_io_address mali_dlbu_cpu_addr = 0;

static u32 mali_dlbu_tile_position;

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
	MALI_DLBU_REGISTER_TLLIST_VBASEADDR        = 0x0008, /**< Tile list virtual base address;
	                                                     31:12 Virtual address to the tile list. This address is used when
	                                                     calculating the call address sent to PP.*/
	MALI_DLBU_REGISTER_FB_DIM                  = 0x000C, /**< Framebuffer dimension;
	                                                     23:16 Number of tiles in Y direction-1
	                                                     7:0 Number of tiles in X direction-1 */
	MALI_DLBU_REGISTER_TLLIST_CONF             = 0x0010, /**< Tile list configuration;
	                                                     29:28 select the size of each allocated block: 0=128 bytes, 1=256, 2=512, 3=1024
	                                                     21:16 2^n number of tiles to be binned to one tile list in Y direction
	                                                     5:0 2^n number of tiles to be binned to one tile list in X direction */
	MALI_DLBU_REGISTER_START_TILE_POS          = 0x0014, /**< Start tile positions;
	                                                     31:24 start position in Y direction for group 1
	                                                     23:16 start position in X direction for group 1
	                                                     15:8 start position in Y direction for group 0
	                                                     7:0 start position in X direction for group 0 */
	MALI_DLBU_REGISTER_PP_ENABLE_MASK          = 0x0018, /**< PP enable mask;
	                                                     7 enable PP7 for load balancing
	                                                     6 enable PP6 for load balancing
	                                                     5 enable PP5 for load balancing
	                                                     4 enable PP4 for load balancing
	                                                     3 enable PP3 for load balancing
	                                                     2 enable PP2 for load balancing
	                                                     1 enable PP1 for load balancing
	                                                     0 enable PP0 for load balancing */
} mali_dlbu_register;

typedef enum
{
	PP0ENABLE = 0,
	PP1ENABLE,
	PP2ENABLE,
	PP3ENABLE,
	PP4ENABLE,
	PP5ENABLE,
	PP6ENABLE,
	PP7ENABLE
} mali_dlbu_pp_enable;

struct mali_dlbu_core
{
	struct mali_hw_core     hw_core;           /**< Common for all HW cores */
	u32                     pp_cores_mask;     /**< This is a mask for the PP cores whose operation will be controlled by LBU
	                                              see MALI_DLBU_REGISTER_PP_ENABLE_MASK register */
};

_mali_osk_errcode_t mali_dlbu_initialize(void)
{

	MALI_DEBUG_PRINT(2, ("Dynamic Load Balancing Unit initializing\n"));

	if (_MALI_OSK_ERR_OK == mali_mmu_get_table_page(&mali_dlbu_phys_addr, &mali_dlbu_cpu_addr))
	{
		MALI_SUCCESS;
	}

	return _MALI_OSK_ERR_FAULT;
}

void mali_dlbu_terminate(void)
{
	MALI_DEBUG_PRINT(3, ("Mali DLBU: terminating\n"));

	mali_mmu_release_table_page(mali_dlbu_phys_addr);
}

struct mali_dlbu_core *mali_dlbu_create(const _mali_osk_resource_t * resource)
{
	struct mali_dlbu_core *core = NULL;

	MALI_DEBUG_PRINT(2, ("Mali DLBU: Creating Mali dynamic load balancing unit: %s\n", resource->description));

	core = _mali_osk_malloc(sizeof(struct mali_dlbu_core));
	if (NULL != core)
	{
		if (_MALI_OSK_ERR_OK == mali_hw_core_create(&core->hw_core, resource, MALI_DLBU_SIZE))
		{
			if (_MALI_OSK_ERR_OK == mali_dlbu_reset(core))
			{
				mali_hw_core_register_write(&core->hw_core, MALI_DLBU_REGISTER_MASTER_TLLIST_VADDR, MALI_DLB_VIRT_ADDR);

				return core;
			}
			MALI_PRINT_ERROR(("Failed to reset DLBU %s\n", core->hw_core.description));
			mali_hw_core_delete(&core->hw_core);
		}

		_mali_osk_free(core);
	}
	else
	{
		MALI_PRINT_ERROR(("Mali DLBU: Failed to allocate memory for DLBU core\n"));
	}

	return NULL;
}

void mali_dlbu_delete(struct mali_dlbu_core *dlbu)
{
	mali_dlbu_reset(dlbu);
	mali_hw_core_delete(&dlbu->hw_core);
	_mali_osk_free(dlbu);
}

void mali_dlbu_enable(struct mali_dlbu_core *dlbu)
{
	u32 wval = mali_hw_core_register_read(&dlbu->hw_core, MALI_DLBU_REGISTER_MASTER_TLLIST_PHYS_ADDR);

	wval |= 0x1;
	mali_hw_core_register_write(&dlbu->hw_core, MALI_DLBU_REGISTER_MASTER_TLLIST_PHYS_ADDR, wval);
}

void mali_dlbu_disable(struct mali_dlbu_core *dlbu)
{
	u32 wval = mali_hw_core_register_read(&dlbu->hw_core, MALI_DLBU_REGISTER_MASTER_TLLIST_PHYS_ADDR);

	wval |= (wval & 0xFFFFFFFE);
	mali_hw_core_register_write(&dlbu->hw_core, MALI_DLBU_REGISTER_MASTER_TLLIST_PHYS_ADDR, wval);
}

_mali_osk_errcode_t mali_dlbu_enable_pp_core(struct mali_dlbu_core *dlbu, u32 pp_core_enable, u32 val)
{
	u32 wval = mali_hw_core_register_read(&dlbu->hw_core, MALI_DLBU_REGISTER_PP_ENABLE_MASK);

	if((pp_core_enable < mali_pp_get_glob_num_pp_cores()) && ((0 == val) || (1 == val))) /* check for valid input parameters */
	{
		if (val == 1)
		{
			val = (wval | (pp_core_enable <<= 0x1));
		}
		if (val == 0)
		{
			val = (wval & ~(pp_core_enable << 0x1));
		}
		wval |= val;
		mali_hw_core_register_write(&dlbu->hw_core, MALI_DLBU_REGISTER_PP_ENABLE_MASK, wval);
		dlbu->pp_cores_mask = wval;

		return _MALI_OSK_ERR_OK;
	}

	return _MALI_OSK_ERR_FAULT;
}

void mali_dlbu_enable_all_pp_cores(struct mali_dlbu_core *dlbu)
{
	mali_hw_core_register_write(&dlbu->hw_core, MALI_DLBU_REGISTER_PP_ENABLE_MASK, dlbu->pp_cores_mask);
}

void mali_dlbu_disable_all_pp_cores(struct mali_dlbu_core *dlbu)
{
	mali_hw_core_register_write(&dlbu->hw_core, MALI_DLBU_REGISTER_PP_ENABLE_MASK, 0x0);
}

void mali_dlbu_setup(struct mali_dlbu_core *dlbu, u8 fb_xdim, u8 fb_ydim, u8 xtilesize, u8 ytilesize, u8 blocksize, u8 xgr0, u8 ygr0, u8 xgr1, u8 ygr1)
{
	u32 wval = 0x0;

	/* write the framebuffer dimensions */
	wval = (16 << (u32)fb_ydim) | (u32)fb_xdim;
	mali_hw_core_register_write(&dlbu->hw_core, MALI_DLBU_REGISTER_FB_DIM, wval);

	/* write the tile list configuration */
	wval = 0x0;
	wval = (28 << (u32)blocksize) | (16 << (u32)ytilesize) | ((u32)xtilesize);
	mali_hw_core_register_write(&dlbu->hw_core, MALI_DLBU_REGISTER_TLLIST_CONF, wval);

	/* write the start tile position */
	wval = 0x0;
	wval = (24 << (u32)ygr1 | (16 << (u32)xgr1) | 8 << (u32)ygr0) | (u32)xgr0;
	mali_hw_core_register_write(&dlbu->hw_core, MALI_DLBU_REGISTER_START_TILE_POS, wval);
}

_mali_osk_errcode_t mali_dlbu_reset(struct mali_dlbu_core *dlbu)
{
	_mali_osk_errcode_t err = _MALI_OSK_ERR_FAULT;
	MALI_DEBUG_ASSERT_POINTER(dlbu);

	MALI_DEBUG_PRINT(4, ("Mali DLBU: mali_dlbu_reset: %s\n", dlbu->hw_core.description));

	mali_hw_core_register_write(&dlbu->hw_core, MALI_DLBU_REGISTER_MASTER_TLLIST_PHYS_ADDR, mali_dlbu_phys_addr);
	mali_hw_core_register_write(&dlbu->hw_core, MALI_DLBU_REGISTER_TLLIST_VBASEADDR, 0x00);
	mali_hw_core_register_write(&dlbu->hw_core, MALI_DLBU_REGISTER_FB_DIM, 0x00);
	mali_hw_core_register_write(&dlbu->hw_core, MALI_DLBU_REGISTER_TLLIST_CONF, 0x00);
	mali_hw_core_register_write(&dlbu->hw_core, MALI_DLBU_REGISTER_START_TILE_POS, 0x00);

	mali_hw_core_register_write(&dlbu->hw_core, MALI_DLBU_REGISTER_PP_ENABLE_MASK, dlbu->pp_cores_mask);

	err = _MALI_OSK_ERR_OK;

	return err;
}

_mali_osk_errcode_t mali_dlbu_add_group(struct mali_dlbu_core *dlbu, struct mali_group *group)
{
	_mali_osk_errcode_t err = _MALI_OSK_ERR_FAULT;
	u32 wval, rval;
	struct mali_pp_core *pp_core = mali_group_get_pp_core(group);

	/* find the core id and set the mask */

	if (NULL != pp_core)
	{
		wval = mali_pp_core_get_id(pp_core);
		rval = mali_hw_core_register_read(&dlbu->hw_core, MALI_DLBU_REGISTER_PP_ENABLE_MASK);
		mali_hw_core_register_write(&dlbu->hw_core, MALI_DLBU_REGISTER_PP_ENABLE_MASK, (wval << 0x1) | rval);
		err = _MALI_OSK_ERR_OK;
	}

	return err;
}

void mali_dlbu_set_tllist_base_address(struct mali_dlbu_core *dlbu, u32 val)
{
	mali_hw_core_register_write(&dlbu->hw_core, MALI_DLBU_REGISTER_TLLIST_VBASEADDR, val);
}

void mali_dlbu_pp_jobs_stop(struct mali_dlbu_core *dlbu)
{
	/* this function to implement (see documentation):
	 * 1) clear all bits in the enable register
	 * 2) wait until all PPs have finished - mali_pp_scheduler.c code - this done in interrupts call?
	 * 3) read the current tile position registers to get current tile positions -
	 * note that current tile position register is the same as start tile position - perhaps the name should be changed!!! */

	/* 1) */
	mali_dlbu_disable_all_pp_cores(dlbu);

	/* 3) */
	mali_dlbu_tile_position = mali_hw_core_register_read(&dlbu->hw_core, MALI_DLBU_REGISTER_START_TILE_POS);
}

void mali_dlbu_pp_jobs_restart(struct mali_dlbu_core *dlbu)
{
	/* this function to implement (see the document):
	 * 1) configure the dynamic load balancing unit as normal
	 * 2) set the current tile position registers as read when stopping the job
	 * 3) configure the PPs to start the job as normal - done by another part of the system - scheduler */

	/* 1) */
	mali_dlbu_reset(dlbu);
	/* ++ setup the needed values - see this */

	/* 2)  */
	mali_hw_core_register_write(&dlbu->hw_core, MALI_DLBU_REGISTER_START_TILE_POS, mali_dlbu_tile_position);
}
