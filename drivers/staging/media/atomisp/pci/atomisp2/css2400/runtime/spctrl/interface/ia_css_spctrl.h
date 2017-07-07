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

#ifndef __IA_CSS_SPCTRL_H__
#define __IA_CSS_SPCTRL_H__

#include <system_global.h>
#include <ia_css_err.h>
#include "ia_css_spctrl_comm.h"


typedef struct {
	uint32_t        ddr_data_offset;       /**<  posistion of data in DDR */
	uint32_t        dmem_data_addr;        /**< data segment address in dmem */
	uint32_t        dmem_bss_addr;         /**< bss segment address in dmem  */
	uint32_t        data_size;             /**< data segment size            */
	uint32_t        bss_size;              /**< bss segment size             */
	uint32_t        spctrl_config_dmem_addr; /** <location of dmem_cfg  in SP dmem */
	uint32_t        spctrl_state_dmem_addr;  /** < location of state  in SP dmem */
	unsigned int    sp_entry;                /** < entry function ptr on SP */
	const void      *code;                   /**< location of firmware */
	uint32_t         code_size;
	char      *program_name;    /**< not used on hardware, only for simulation */
} ia_css_spctrl_cfg;

/* Get the code addr in DDR of SP */
hrt_vaddress get_sp_code_addr(sp_ID_t  sp_id);

/* ! Load firmware on to specfied SP
*/
enum ia_css_err ia_css_spctrl_load_fw(sp_ID_t sp_id,
			ia_css_spctrl_cfg *spctrl_cfg);

#ifdef ISP2401
/*! Setup registers for reloading FW */
void sh_css_spctrl_reload_fw(sp_ID_t sp_id);

#endif
/*!  Unload/release any memory allocated to hold the firmware
*/
enum ia_css_err ia_css_spctrl_unload_fw(sp_ID_t sp_id);


/*! Intilaize dmem_cfg in SP dmem  and  start SP program
*/
enum ia_css_err ia_css_spctrl_start(sp_ID_t sp_id);

/*! stop spctrl
*/
enum ia_css_err ia_css_spctrl_stop(sp_ID_t sp_id);

/*! Query the state of SP
*/
ia_css_spctrl_sp_sw_state ia_css_spctrl_get_state(sp_ID_t sp_id);

/*! Check if SP is idle/ready
*/
int ia_css_spctrl_is_idle(sp_ID_t sp_id);

#endif /* __IA_CSS_SPCTRL_H__ */
