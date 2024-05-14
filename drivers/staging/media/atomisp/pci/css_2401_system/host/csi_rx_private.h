/* SPDX-License-Identifier: GPL-2.0 */
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

#ifndef __CSI_RX_PRIVATE_H_INCLUDED__
#define __CSI_RX_PRIVATE_H_INCLUDED__

#include "rx_csi_defs.h"
#include "mipi_backend_defs.h"
#include "csi_rx.h"

#include "device_access.h"	/* ia_css_device_load_uint32 */

#include "assert_support.h" /* assert */
#include "print_support.h" /* print */

/*****************************************************
 *
 * Device level interface (DLI).
 *
 *****************************************************/
/**
 * @brief Load the register value.
 * Refer to "csi_rx_public.h" for details.
 */
static inline hrt_data csi_rx_fe_ctrl_reg_load(
    const csi_rx_frontend_ID_t ID,
    const hrt_address reg)
{
	assert(ID < N_CSI_RX_FRONTEND_ID);
	assert(CSI_RX_FE_CTRL_BASE[ID] != (hrt_address)-1);
	return ia_css_device_load_uint32(CSI_RX_FE_CTRL_BASE[ID] + reg * sizeof(
					     hrt_data));
}

/**
 * @brief Store a value to the register.
 * Refer to "ibuf_ctrl_public.h" for details.
 */
static inline void csi_rx_fe_ctrl_reg_store(
    const csi_rx_frontend_ID_t ID,
    const hrt_address reg,
    const hrt_data value)
{
	assert(ID < N_CSI_RX_FRONTEND_ID);
	assert(CSI_RX_FE_CTRL_BASE[ID] != (hrt_address)-1);

	ia_css_device_store_uint32(CSI_RX_FE_CTRL_BASE[ID] + reg * sizeof(hrt_data),
				   value);
}

/**
 * @brief Load the register value.
 * Refer to "csi_rx_public.h" for details.
 */
static inline hrt_data csi_rx_be_ctrl_reg_load(
    const csi_rx_backend_ID_t ID,
    const hrt_address reg)
{
	assert(ID < N_CSI_RX_BACKEND_ID);
	assert(CSI_RX_BE_CTRL_BASE[ID] != (hrt_address)-1);
	return ia_css_device_load_uint32(CSI_RX_BE_CTRL_BASE[ID] + reg * sizeof(
					     hrt_data));
}

/**
 * @brief Store a value to the register.
 * Refer to "ibuf_ctrl_public.h" for details.
 */
static inline void csi_rx_be_ctrl_reg_store(
    const csi_rx_backend_ID_t ID,
    const hrt_address reg,
    const hrt_data value)
{
	assert(ID < N_CSI_RX_BACKEND_ID);
	assert(CSI_RX_BE_CTRL_BASE[ID] != (hrt_address)-1);

	ia_css_device_store_uint32(CSI_RX_BE_CTRL_BASE[ID] + reg * sizeof(hrt_data),
				   value);
}

/* end of DLI */

/*****************************************************
 *
 * Native command interface (NCI).
 *
 *****************************************************/
/**
 * @brief Get the state of the csi rx fe dlane process.
 * Refer to "csi_rx_public.h" for details.
 */
static inline void csi_rx_fe_ctrl_get_dlane_state(
    const csi_rx_frontend_ID_t ID,
    const u32 lane,
    csi_rx_fe_ctrl_lane_t *dlane_state)
{
	dlane_state->termen =
	    csi_rx_fe_ctrl_reg_load(ID, _HRT_CSI_RX_DLY_CNT_TERMEN_DLANE_REG_IDX(lane));
	dlane_state->settle =
	    csi_rx_fe_ctrl_reg_load(ID, _HRT_CSI_RX_DLY_CNT_SETTLE_DLANE_REG_IDX(lane));
}

/**
 * @brief Get the csi rx fe state.
 * Refer to "csi_rx_public.h" for details.
 */
static inline void csi_rx_fe_ctrl_get_state(
    const csi_rx_frontend_ID_t ID,
    csi_rx_fe_ctrl_state_t *state)
{
	u32 i;

	state->enable =
	    csi_rx_fe_ctrl_reg_load(ID, _HRT_CSI_RX_ENABLE_REG_IDX);
	state->nof_enable_lanes =
	    csi_rx_fe_ctrl_reg_load(ID, _HRT_CSI_RX_NOF_ENABLED_LANES_REG_IDX);
	state->error_handling =
	    csi_rx_fe_ctrl_reg_load(ID, _HRT_CSI_RX_ERROR_HANDLING_REG_IDX);
	state->status =
	    csi_rx_fe_ctrl_reg_load(ID, _HRT_CSI_RX_STATUS_REG_IDX);
	state->status_dlane_hs =
	    csi_rx_fe_ctrl_reg_load(ID, _HRT_CSI_RX_STATUS_DLANE_HS_REG_IDX);
	state->status_dlane_lp =
	    csi_rx_fe_ctrl_reg_load(ID, _HRT_CSI_RX_STATUS_DLANE_LP_REG_IDX);
	state->clane.termen =
	    csi_rx_fe_ctrl_reg_load(ID, _HRT_CSI_RX_DLY_CNT_TERMEN_CLANE_REG_IDX);
	state->clane.settle =
	    csi_rx_fe_ctrl_reg_load(ID, _HRT_CSI_RX_DLY_CNT_SETTLE_CLANE_REG_IDX);

	/*
	 * Get the values of the register-set per
	 * dlane.
	 */
	for (i = 0; i < N_CSI_RX_FE_CTRL_DLANES[ID]; i++) {
		csi_rx_fe_ctrl_get_dlane_state(
		    ID,
		    i,
		    &state->dlane[i]);
	}
}

/**
 * @brief dump the csi rx fe state.
 * Refer to "csi_rx_public.h" for details.
 */
static inline void csi_rx_fe_ctrl_dump_state(
    const csi_rx_frontend_ID_t ID,
    csi_rx_fe_ctrl_state_t *state)
{
	u32 i;

	ia_css_print("CSI RX FE STATE Controller %d Enable state 0x%x\n", ID,
		     state->enable);
	ia_css_print("CSI RX FE STATE Controller %d No Of enable lanes 0x%x\n", ID,
		     state->nof_enable_lanes);
	ia_css_print("CSI RX FE STATE Controller %d Error handling 0x%x\n", ID,
		     state->error_handling);
	ia_css_print("CSI RX FE STATE Controller %d Status 0x%x\n", ID, state->status);
	ia_css_print("CSI RX FE STATE Controller %d Status Dlane HS 0x%x\n", ID,
		     state->status_dlane_hs);
	ia_css_print("CSI RX FE STATE Controller %d Status Dlane LP 0x%x\n", ID,
		     state->status_dlane_lp);
	ia_css_print("CSI RX FE STATE Controller %d Status term enable LP 0x%x\n", ID,
		     state->clane.termen);
	ia_css_print("CSI RX FE STATE Controller %d Status term settle LP 0x%x\n", ID,
		     state->clane.settle);

	/*
	 * Get the values of the register-set per
	 * dlane.
	 */
	for (i = 0; i < N_CSI_RX_FE_CTRL_DLANES[ID]; i++) {
		ia_css_print("CSI RX FE STATE Controller %d DLANE ID %d termen 0x%x\n", ID, i,
			     state->dlane[i].termen);
		ia_css_print("CSI RX FE STATE Controller %d DLANE ID %d settle 0x%x\n", ID, i,
			     state->dlane[i].settle);
	}
}

/**
 * @brief Get the csi rx be state.
 * Refer to "csi_rx_public.h" for details.
 */
static inline void csi_rx_be_ctrl_get_state(
    const csi_rx_backend_ID_t ID,
    csi_rx_be_ctrl_state_t *state)
{
	u32 i;

	state->enable =
	    csi_rx_be_ctrl_reg_load(ID, _HRT_MIPI_BACKEND_ENABLE_REG_IDX);

	state->status =
	    csi_rx_be_ctrl_reg_load(ID, _HRT_MIPI_BACKEND_STATUS_REG_IDX);

	for (i = 0; i < N_CSI_RX_BE_MIPI_COMP_FMT_REG ; i++) {
		state->comp_format_reg[i] =
		    csi_rx_be_ctrl_reg_load(ID,
					    _HRT_MIPI_BACKEND_COMP_FORMAT_REG0_IDX + i);
	}

	state->raw16 =
	    csi_rx_be_ctrl_reg_load(ID, _HRT_MIPI_BACKEND_RAW16_CONFIG_REG_IDX);

	state->raw18 =
	    csi_rx_be_ctrl_reg_load(ID, _HRT_MIPI_BACKEND_RAW18_CONFIG_REG_IDX);
	state->force_raw8 =
	    csi_rx_be_ctrl_reg_load(ID, _HRT_MIPI_BACKEND_FORCE_RAW8_REG_IDX);
	state->irq_status =
	    csi_rx_be_ctrl_reg_load(ID, _HRT_MIPI_BACKEND_IRQ_STATUS_REG_IDX);
#if 0 /* device access error for these registers */
	/* ToDo: rootcause this failure */
	state->custom_mode_enable =
	    csi_rx_be_ctrl_reg_load(ID, _HRT_MIPI_BACKEND_CUST_EN_REG_IDX);

	state->custom_mode_data_state =
	    csi_rx_be_ctrl_reg_load(ID, _HRT_MIPI_BACKEND_CUST_DATA_STATE_REG_IDX);
	for (i = 0; i < N_CSI_RX_BE_MIPI_CUSTOM_PEC ; i++) {
		state->pec[i] =
		    csi_rx_be_ctrl_reg_load(ID, _HRT_MIPI_BACKEND_CUST_PIX_EXT_S0P0_REG_IDX + i);
	}
	state->custom_mode_valid_eop_config =
	    csi_rx_be_ctrl_reg_load(ID, _HRT_MIPI_BACKEND_CUST_PIX_VALID_EOP_REG_IDX);
#endif
	state->global_lut_disregard_reg =
	    csi_rx_be_ctrl_reg_load(ID, _HRT_MIPI_BACKEND_GLOBAL_LUT_DISREGARD_REG_IDX);
	state->packet_status_stall =
	    csi_rx_be_ctrl_reg_load(ID, _HRT_MIPI_BACKEND_PKT_STALL_STATUS_REG_IDX);
	/*
	 * Get the values of the register-set per
	 * lut.
	 */
	for (i = 0; i < N_SHORT_PACKET_LUT_ENTRIES[ID]; i++) {
		state->short_packet_lut_entry[i] =
		    csi_rx_be_ctrl_reg_load(ID, _HRT_MIPI_BACKEND_SP_LUT_ENTRY_0_REG_IDX + i);
	}
	for (i = 0; i < N_LONG_PACKET_LUT_ENTRIES[ID]; i++) {
		state->long_packet_lut_entry[i] =
		    csi_rx_be_ctrl_reg_load(ID, _HRT_MIPI_BACKEND_LP_LUT_ENTRY_0_REG_IDX + i);
	}
}

/**
 * @brief Dump the csi rx be state.
 * Refer to "csi_rx_public.h" for details.
 */
static inline void csi_rx_be_ctrl_dump_state(
    const csi_rx_backend_ID_t ID,
    csi_rx_be_ctrl_state_t *state)
{
	u32 i;

	ia_css_print("CSI RX BE STATE Controller %d Enable 0x%x\n", ID, state->enable);
	ia_css_print("CSI RX BE STATE Controller %d Status 0x%x\n", ID, state->status);

	for (i = 0; i < N_CSI_RX_BE_MIPI_COMP_FMT_REG ; i++) {
		ia_css_print("CSI RX BE STATE Controller %d comp format reg vc%d value 0x%x\n",
			     ID, i, state->status);
	}
	ia_css_print("CSI RX BE STATE Controller %d RAW16 0x%x\n", ID, state->raw16);
	ia_css_print("CSI RX BE STATE Controller %d RAW18 0x%x\n", ID, state->raw18);
	ia_css_print("CSI RX BE STATE Controller %d Force RAW8 0x%x\n", ID,
		     state->force_raw8);
	ia_css_print("CSI RX BE STATE Controller %d IRQ state 0x%x\n", ID,
		     state->irq_status);
#if 0   /* ToDo:Getting device access error for this register */
	for (i = 0; i < N_CSI_RX_BE_MIPI_CUSTOM_PEC ; i++) {
		ia_css_print("CSI RX BE STATE Controller %d PEC ID %d custom pec 0x%x\n", ID, i,
			     state->pec[i]);
	}
#endif
	ia_css_print("CSI RX BE STATE Controller %d Global LUT disregard reg 0x%x\n",
		     ID, state->global_lut_disregard_reg);
	ia_css_print("CSI RX BE STATE Controller %d packet stall reg 0x%x\n", ID,
		     state->packet_status_stall);
	/*
	 * Get the values of the register-set per
	 * lut.
	 */
	for (i = 0; i < N_SHORT_PACKET_LUT_ENTRIES[ID]; i++) {
		ia_css_print("CSI RX BE STATE Controller ID %d Short packet entry %d short packet lut id 0x%x\n",
			     ID, i,
			     state->short_packet_lut_entry[i]);
	}
	for (i = 0; i < N_LONG_PACKET_LUT_ENTRIES[ID]; i++) {
		ia_css_print("CSI RX BE STATE Controller ID %d Long packet entry %d long packet lut id 0x%x\n",
			     ID, i,
			     state->long_packet_lut_entry[i]);
	}
}

/* end of NCI */

#endif /* __CSI_RX_PRIVATE_H_INCLUDED__ */
