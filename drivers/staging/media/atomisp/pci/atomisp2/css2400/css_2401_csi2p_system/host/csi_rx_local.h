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

#ifndef __CSI_RX_LOCAL_H_INCLUDED__
#define __CSI_RX_LOCAL_H_INCLUDED__

#include "csi_rx_global.h"
#define N_CSI_RX_BE_MIPI_COMP_FMT_REG 		4
#define N_CSI_RX_BE_MIPI_CUSTOM_PEC		12
#define N_CSI_RX_BE_SHORT_PKT_LUT		4
#define N_CSI_RX_BE_LONG_PKT_LUT		8
typedef struct csi_rx_fe_ctrl_state_s		csi_rx_fe_ctrl_state_t;
typedef struct csi_rx_fe_ctrl_lane_s		csi_rx_fe_ctrl_lane_t;
typedef struct csi_rx_be_ctrl_state_s		csi_rx_be_ctrl_state_t;
/*mipi_backend_custom_mode_pixel_extraction_config*/
typedef struct csi_rx_be_ctrl_pec_s		csi_rx_be_ctrl_pec_t;


struct csi_rx_fe_ctrl_lane_s {
	hrt_data	termen;
	hrt_data	settle;
};
struct csi_rx_fe_ctrl_state_s {
	hrt_data 		enable;
	hrt_data 		nof_enable_lanes;
	hrt_data 		error_handling;
	hrt_data 		status;
	hrt_data 		status_dlane_hs;
	hrt_data 		status_dlane_lp;
	csi_rx_fe_ctrl_lane_t 	clane;
	csi_rx_fe_ctrl_lane_t 	dlane[N_CSI_RX_DLANE_ID];
};
struct csi_rx_be_ctrl_state_s {
	hrt_data 		enable;
	hrt_data 		status;
	hrt_data 		comp_format_reg[N_CSI_RX_BE_MIPI_COMP_FMT_REG];
	hrt_data 		raw16;
	hrt_data 		raw18;
	hrt_data 		force_raw8;
	hrt_data 		irq_status;
	hrt_data 		custom_mode_enable;
	hrt_data 		custom_mode_data_state;
	hrt_data 		pec[N_CSI_RX_BE_MIPI_CUSTOM_PEC];
	hrt_data 		custom_mode_valid_eop_config;
	hrt_data 		global_lut_disregard_reg;
	hrt_data 		packet_status_stall;
	hrt_data 		short_packet_lut_entry[N_CSI_RX_BE_SHORT_PKT_LUT];
	hrt_data 		long_packet_lut_entry[N_CSI_RX_BE_LONG_PKT_LUT];
};
#endif /* __CSI_RX_LOCAL_H_INCLUDED__ */
