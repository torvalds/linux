/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2010 - 2015, Intel Corporation.
 */

#ifndef __IA_CSS_SPCTRL_H__
#define __IA_CSS_SPCTRL_H__

#include <system_global.h>
#include <ia_css_err.h>
#include "ia_css_spctrl_comm.h"

typedef struct {
	u32        ddr_data_offset;       /**  posistion of data in DDR */
	u32        dmem_data_addr;        /** data segment address in dmem */
	u32        dmem_bss_addr;         /** bss segment address in dmem  */
	u32        data_size;             /** data segment size            */
	u32        bss_size;              /** bss segment size             */
	u32        spctrl_config_dmem_addr; /* <location of dmem_cfg  in SP dmem */
	u32        spctrl_state_dmem_addr;  /* < location of state  in SP dmem */
	unsigned int    sp_entry;                /* < entry function ptr on SP */
	const void      *code;                   /** location of firmware */
	u32         code_size;
	char      *program_name;    /** not used on hardware, only for simulation */
} ia_css_spctrl_cfg;

/* Get the code addr in DDR of SP */
ia_css_ptr get_sp_code_addr(sp_ID_t  sp_id);

/* ! Load firmware on to specfied SP
*/
int ia_css_spctrl_load_fw(sp_ID_t sp_id,
				      ia_css_spctrl_cfg *spctrl_cfg);

/* ISP2401 */
/*! Setup registers for reloading FW */
void sh_css_spctrl_reload_fw(sp_ID_t sp_id);

/*!  Unload/release any memory allocated to hold the firmware
*/
int ia_css_spctrl_unload_fw(sp_ID_t sp_id);

/*! Intilaize dmem_cfg in SP dmem  and  start SP program
*/
int ia_css_spctrl_start(sp_ID_t sp_id);

/*! stop spctrl
*/
int ia_css_spctrl_stop(sp_ID_t sp_id);

/*! Query the state of SP
*/
ia_css_spctrl_sp_sw_state ia_css_spctrl_get_state(sp_ID_t sp_id);

/*! Check if SP is idle/ready
*/
int ia_css_spctrl_is_idle(sp_ID_t sp_id);

#endif /* __IA_CSS_SPCTRL_H__ */
