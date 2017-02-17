#ifndef ISP2401
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */
#else
/**
Support for Intel Camera Imaging ISP subsystem.
Copyright (c) 2010 - 2015, Intel Corporation.

This program is free software; you can redistribute it and/or modify it
under the terms and conditions of the GNU General Public License,
version 2, as published by the Free Software Foundation.

This program is distributed in the hope it will be useful, but WITHOUT
ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
more details.
*/
#endif

#include "ia_css_types.h"
#define __INLINE_SP__
#include "sp.h"

#ifdef HRT_CSIM
#include <hive_isp_css_sp_hrt.h>
#endif

#include "memory_access.h"
#include "assert_support.h"
#include "ia_css_spctrl.h"
#include "ia_css_debug.h"

typedef struct {
	struct ia_css_sp_init_dmem_cfg dmem_config;
	uint32_t        spctrl_config_dmem_addr; /** location of dmem_cfg  in SP dmem */
	uint32_t        spctrl_state_dmem_addr;
	unsigned int    sp_entry;           /* entry function ptr on SP */
	hrt_vaddress    code_addr;          /* sp firmware location in host mem-DDR*/
	uint32_t        code_size;
	char           *program_name;       /* used in case of PLATFORM_SIM */
} spctrl_context_info;

static spctrl_context_info spctrl_cofig_info[N_SP_ID];
static bool spctrl_loaded[N_SP_ID] = {0};

/* Load firmware */
enum ia_css_err ia_css_spctrl_load_fw(sp_ID_t sp_id,
				ia_css_spctrl_cfg *spctrl_cfg)
{
	hrt_vaddress code_addr = mmgr_NULL;
	struct ia_css_sp_init_dmem_cfg *init_dmem_cfg;

	if ((sp_id >= N_SP_ID) || (spctrl_cfg == 0))
		return IA_CSS_ERR_INVALID_ARGUMENTS;

	spctrl_cofig_info[sp_id].code_addr = mmgr_NULL;

#if defined(C_RUN) || defined(HRT_UNSCHED)
	(void)init_dmem_cfg;
	code_addr = mmgr_malloc(1);
	if (code_addr == mmgr_NULL)
		return IA_CSS_ERR_CANNOT_ALLOCATE_MEMORY;
#else
	init_dmem_cfg = &spctrl_cofig_info[sp_id].dmem_config;
	init_dmem_cfg->dmem_data_addr = spctrl_cfg->dmem_data_addr;
	init_dmem_cfg->dmem_bss_addr  = spctrl_cfg->dmem_bss_addr;
	init_dmem_cfg->data_size      = spctrl_cfg->data_size;
	init_dmem_cfg->bss_size       = spctrl_cfg->bss_size;
	init_dmem_cfg->sp_id          = sp_id;

	spctrl_cofig_info[sp_id].spctrl_config_dmem_addr = spctrl_cfg->spctrl_config_dmem_addr;
	spctrl_cofig_info[sp_id].spctrl_state_dmem_addr = spctrl_cfg->spctrl_state_dmem_addr;

	/* store code (text + icache) and data to DDR
	 *
	 * Data used to be stored separately, because of access alignment constraints,
	 * fix the FW generation instead
	 */
	code_addr = mmgr_malloc(spctrl_cfg->code_size);
	if (code_addr == mmgr_NULL)
		return IA_CSS_ERR_CANNOT_ALLOCATE_MEMORY;
	mmgr_store(code_addr, spctrl_cfg->code, spctrl_cfg->code_size);

	if (sizeof(hrt_vaddress) > sizeof(hrt_data)) {
		ia_css_debug_dtrace(IA_CSS_DEBUG_ERROR,
				    "size of hrt_vaddress can not be greater than hrt_data\n");
		mmgr_free(code_addr);
		code_addr = mmgr_NULL;
		return IA_CSS_ERR_INTERNAL_ERROR;
	}

	init_dmem_cfg->ddr_data_addr  = code_addr + spctrl_cfg->ddr_data_offset;
	if ((init_dmem_cfg->ddr_data_addr % HIVE_ISP_DDR_WORD_BYTES) != 0) {
		ia_css_debug_dtrace(IA_CSS_DEBUG_ERROR,
				    "DDR address pointer is not properly aligned for DMA transfer\n");
		mmgr_free(code_addr);
		code_addr = mmgr_NULL;
		return IA_CSS_ERR_INTERNAL_ERROR;
	}
#endif
	spctrl_cofig_info[sp_id].sp_entry = spctrl_cfg->sp_entry;
	spctrl_cofig_info[sp_id].code_addr = code_addr;
	spctrl_cofig_info[sp_id].program_name = spctrl_cfg->program_name;

#ifdef HRT_CSIM
	/* Secondary SP is named as SP2 in SDK, however we are using secondary
	   SP as SP1 in the HSS and secondary SP Firmware */
	if (sp_id == SP0_ID) {
		hrt_cell_set_icache_base_address(SP, spctrl_cofig_info[sp_id].code_addr);
		hrt_cell_invalidate_icache(SP);
		hrt_cell_load_program(SP, spctrl_cofig_info[sp_id].program_name);
	}
#if defined(HAS_SEC_SP)
	else {
		hrt_cell_set_icache_base_address(SP2, spctrl_cofig_info[sp_id].code_addr);
		hrt_cell_invalidate_icache(SP2);
		hrt_cell_load_program(SP2, spctrl_cofig_info[sp_id].program_name);
	}
#endif /* HAS_SEC_SP */
#else
	/* now we program the base address into the icache and
	 * invalidate the cache.
	 */
	sp_ctrl_store(sp_id, SP_ICACHE_ADDR_REG, (hrt_data)spctrl_cofig_info[sp_id].code_addr);
	sp_ctrl_setbit(sp_id, SP_ICACHE_INV_REG, SP_ICACHE_INV_BIT);
#endif
	spctrl_loaded[sp_id] = true;
	return IA_CSS_SUCCESS;
}

#ifdef ISP2401
/* reload pre-loaded FW */
void sh_css_spctrl_reload_fw(sp_ID_t sp_id)
{
#ifdef HRT_CSIM
	/* Secondary SP is named as SP2 in SDK, however we are using secondary
	SP as SP1 in the HSS and secondary SP Firmware */
	if (sp_id == SP0_ID) {
		hrt_cell_set_icache_base_address(SP, spctrl_cofig_info[sp_id].code_addr);
		hrt_cell_invalidate_icache(SP);
		hrt_cell_load_program(SP, spctrl_cofig_info[sp_id].program_name);
	}
#if defined(HAS_SEC_SP)
	else {
		hrt_cell_set_icache_base_address(SP2, spctrl_cofig_info[sp_id].code_addr);
		hrt_cell_invalidate_icache(SP2);
		hrt_cell_load_program(SP2, spctrl_cofig_info[sp_id].program_name);
	}
#endif /* HAS_SEC_SP */
#else
	/* now we program the base address into the icache and
	* invalidate the cache.
	*/
	sp_ctrl_store(sp_id, SP_ICACHE_ADDR_REG, (hrt_data)spctrl_cofig_info[sp_id].code_addr);
	sp_ctrl_setbit(sp_id, SP_ICACHE_INV_REG, SP_ICACHE_INV_BIT);
#endif
	spctrl_loaded[sp_id] = true;
}

#endif
hrt_vaddress get_sp_code_addr(sp_ID_t  sp_id)
{
	return spctrl_cofig_info[sp_id].code_addr;
}

enum ia_css_err ia_css_spctrl_unload_fw(sp_ID_t sp_id)
{
	if ((sp_id >= N_SP_ID) || ((sp_id < N_SP_ID) && (!spctrl_loaded[sp_id])))
		return IA_CSS_ERR_INVALID_ARGUMENTS;

	/*  freeup the resource */
	if (spctrl_cofig_info[sp_id].code_addr)
		mmgr_free(spctrl_cofig_info[sp_id].code_addr);
	spctrl_loaded[sp_id] = false;
	return IA_CSS_SUCCESS;
}

#ifdef HRT_CSIM
enum ia_css_err ia_css_spctrl_start(sp_ID_t sp_id)
{
	unsigned int HIVE_ADDR_sp_start_isp_entry;
#if defined(HAS_SEC_SP)
	unsigned int HIVE_ADDR_sp1_start_entry;
#endif /* HAS_SEC_SP */
	if ((sp_id >= N_SP_ID) || ((sp_id < N_SP_ID) && (!spctrl_loaded[sp_id])))
		return IA_CSS_ERR_INVALID_ARGUMENTS;
if (sp_id == SP0_ID)
	HIVE_ADDR_sp_start_isp_entry = spctrl_cofig_info[sp_id].sp_entry;
#if defined(HAS_SEC_SP)
else
	HIVE_ADDR_sp1_start_entry = spctrl_cofig_info[sp_id].sp_entry;
#endif /* HAS_SEC_SP  */

#if !defined(C_RUN) && !defined(HRT_UNSCHED)
	sp_dmem_store(sp_id,
		spctrl_cofig_info[sp_id].spctrl_config_dmem_addr,
		&spctrl_cofig_info[sp_id].dmem_config,
		sizeof(spctrl_cofig_info[sp_id].dmem_config));
#endif
	if (sp_id == SP0_ID)
		hrt_cell_start_function(SP, sp_start_isp);
#if defined(HAS_SEC_SP)
	else
		/* Secondary SP is named as sp1 in the firmware however in
		   SDK secondary SP is named as SP2 */
		hrt_cell_start_function(SP2, sp1_start);
#endif /* HAS_SEC_SP */

	return IA_CSS_SUCCESS;
}
#else
/* Initialize dmem_cfg in SP dmem  and  start SP program*/
enum ia_css_err ia_css_spctrl_start(sp_ID_t sp_id)
{
	if ((sp_id >= N_SP_ID) || ((sp_id < N_SP_ID) && (!spctrl_loaded[sp_id])))
		return IA_CSS_ERR_INVALID_ARGUMENTS;

	/* Set descr in the SP to initialize the SP DMEM */
	/*
	 * The FW stores user-space pointers to the FW, the ISP pointer
	 * is only available here
	 *
	 */
	assert(sizeof(unsigned int) <= sizeof(hrt_data));

	sp_dmem_store(sp_id,
		spctrl_cofig_info[sp_id].spctrl_config_dmem_addr,
		&spctrl_cofig_info[sp_id].dmem_config,
		sizeof(spctrl_cofig_info[sp_id].dmem_config));
	/* set the start address */
	sp_ctrl_store(sp_id, SP_START_ADDR_REG, (hrt_data)spctrl_cofig_info[sp_id].sp_entry);
	sp_ctrl_setbit(sp_id, SP_SC_REG, SP_RUN_BIT);
	sp_ctrl_setbit(sp_id, SP_SC_REG, SP_START_BIT);
	return IA_CSS_SUCCESS;
}
#endif
/* Query the state of SP1 */
ia_css_spctrl_sp_sw_state ia_css_spctrl_get_state(sp_ID_t sp_id)
{
	ia_css_spctrl_sp_sw_state state = 0;
	unsigned int HIVE_ADDR_sp_sw_state;
	if (sp_id >= N_SP_ID)
		return IA_CSS_SP_SW_TERMINATED;

	HIVE_ADDR_sp_sw_state = spctrl_cofig_info[sp_id].spctrl_state_dmem_addr;
	(void)HIVE_ADDR_sp_sw_state; /* Suppres warnings in CRUN */
	if (sp_id == SP0_ID)
		state = sp_dmem_load_uint32(sp_id, (unsigned)sp_address_of(sp_sw_state));
#if defined(HAS_SEC_SP)
	else
		state = sp_dmem_load_uint32(sp_id, (unsigned)sp1_address_of(sp_sw_state));
#endif /* HAS_SEC_SP */

	return state;
}

int ia_css_spctrl_is_idle(sp_ID_t sp_id)
{
	int state = 0;
	assert (sp_id < N_SP_ID);

#ifdef HRT_CSIM
	if (sp_id == SP0_ID)
		state = (int)hrt_ctl_is_ready(SP);
#if defined(HAS_SEC_SP)
	else
		state = (int)hrt_ctl_is_ready(SP2);
#endif /* HAS_SEC_SP */
#else /* HRT_CSIM */
	state = sp_ctrl_getbit(sp_id, SP_SC_REG, SP_IDLE_BIT);
#endif /* HRT_CSIM */
	return state;
}

