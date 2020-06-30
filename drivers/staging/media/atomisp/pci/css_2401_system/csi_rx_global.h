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

#ifndef __CSI_RX_GLOBAL_H_INCLUDED__
#define __CSI_RX_GLOBAL_H_INCLUDED__

#include <type_support.h>

typedef enum {
	CSI_MIPI_PACKET_TYPE_UNDEFINED = 0,
	CSI_MIPI_PACKET_TYPE_LONG,
	CSI_MIPI_PACKET_TYPE_SHORT,
	CSI_MIPI_PACKET_TYPE_RESERVED,
	N_CSI_MIPI_PACKET_TYPE
} csi_mipi_packet_type_t;

typedef struct csi_rx_backend_lut_entry_s	csi_rx_backend_lut_entry_t;
struct csi_rx_backend_lut_entry_s {
	u32	long_packet_entry;
	u32	short_packet_entry;
};

typedef struct csi_rx_backend_cfg_s csi_rx_backend_cfg_t;
struct csi_rx_backend_cfg_s {
	/* LUT entry for the packet */
	csi_rx_backend_lut_entry_t lut_entry;

	/* can be derived from the Data Type */
	csi_mipi_packet_type_t csi_mipi_packet_type;

	struct {
		bool     comp_enable;
		u32 virtual_channel;
		u32 data_type;
		u32 comp_scheme;
		u32 comp_predictor;
		u32 comp_bit_idx;
	} csi_mipi_cfg;
};

typedef struct csi_rx_frontend_cfg_s csi_rx_frontend_cfg_t;
struct csi_rx_frontend_cfg_s {
	u32 active_lanes;
};

extern const u32 N_SHORT_PACKET_LUT_ENTRIES[N_CSI_RX_BACKEND_ID];
extern const u32 N_LONG_PACKET_LUT_ENTRIES[N_CSI_RX_BACKEND_ID];
extern const u32 N_CSI_RX_FE_CTRL_DLANES[N_CSI_RX_FRONTEND_ID];
/* sid_width for CSI_RX_BACKEND<N>_ID */
extern const u32 N_CSI_RX_BE_SID_WIDTH[N_CSI_RX_BACKEND_ID];

#endif /* __CSI_RX_GLOBAL_H_INCLUDED__ */
