/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2010 - 2015, Intel Corporation.
 */

#ifndef __IA_CSS_SPCTRL_COMM_H__
#define __IA_CSS_SPCTRL_COMM_H__

#include <linux/build_bug.h>

#include <type_support.h>

/* state of SP */
typedef enum {
	IA_CSS_SP_SW_TERMINATED = 0,
	IA_CSS_SP_SW_INITIALIZED,
	IA_CSS_SP_SW_CONNECTED,
	IA_CSS_SP_SW_RUNNING
} ia_css_spctrl_sp_sw_state;

/* Structure to encapsulate required arguments for
 * initialization of SP DMEM using the SP itself
 */
struct ia_css_sp_init_dmem_cfg {
	ia_css_ptr      ddr_data_addr;  /** data segment address in ddr  */
	u32        dmem_data_addr; /** data segment address in dmem */
	u32        dmem_bss_addr;  /** bss segment address in dmem  */
	u32        data_size;      /** data segment size            */
	u32        bss_size;       /** bss segment size             */
	sp_ID_t         sp_id;          /* <sp Id */
};

#define SIZE_OF_IA_CSS_SP_INIT_DMEM_CFG_STRUCT	\
	(1 * SIZE_OF_IA_CSS_PTR) +		\
	(4 * sizeof(uint32_t)) +		\
	(1 * sizeof(sp_ID_t))

static_assert(sizeof(struct ia_css_sp_init_dmem_cfg) == SIZE_OF_IA_CSS_SP_INIT_DMEM_CFG_STRUCT);

#endif /* __IA_CSS_SPCTRL_COMM_H__ */
