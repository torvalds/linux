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


#include "system_global.h"

const uint32_t N_SHORT_PACKET_LUT_ENTRIES[N_CSI_RX_BACKEND_ID] = {
	4,	/* 4 entries at CSI_RX_BACKEND0_ID*/
	4,	/* 4 entries at CSI_RX_BACKEND1_ID*/
	4	/* 4 entries at CSI_RX_BACKEND2_ID*/
};

const uint32_t N_LONG_PACKET_LUT_ENTRIES[N_CSI_RX_BACKEND_ID] = {
	8,	/* 8 entries at CSI_RX_BACKEND0_ID*/
	4,	/* 4 entries at CSI_RX_BACKEND1_ID*/
	4	/* 4 entries at CSI_RX_BACKEND2_ID*/
};

const uint32_t N_CSI_RX_FE_CTRL_DLANES[N_CSI_RX_FRONTEND_ID] = {
	N_CSI_RX_DLANE_ID,	/* 4 dlanes for CSI_RX_FR0NTEND0_ID */
	N_CSI_RX_DLANE_ID,	/* 4 dlanes for CSI_RX_FR0NTEND1_ID */
	N_CSI_RX_DLANE_ID	/* 4 dlanes for CSI_RX_FR0NTEND2_ID */
};

/* sid_width for CSI_RX_BACKEND<N>_ID */
const uint32_t N_CSI_RX_BE_SID_WIDTH[N_CSI_RX_BACKEND_ID] = {
	3,
	2,
	2
};
