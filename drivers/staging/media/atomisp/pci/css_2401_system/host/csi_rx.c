// SPDX-License-Identifier: GPL-2.0
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 */

#include "system_global.h"
#include "csi_rx_global.h"

const u32 N_SHORT_PACKET_LUT_ENTRIES[N_CSI_RX_BACKEND_ID] = {
	4,	/* 4 entries at CSI_RX_BACKEND0_ID*/
	4,	/* 4 entries at CSI_RX_BACKEND1_ID*/
	4	/* 4 entries at CSI_RX_BACKEND2_ID*/
};

const u32 N_LONG_PACKET_LUT_ENTRIES[N_CSI_RX_BACKEND_ID] = {
	8,	/* 8 entries at CSI_RX_BACKEND0_ID*/
	4,	/* 4 entries at CSI_RX_BACKEND1_ID*/
	4	/* 4 entries at CSI_RX_BACKEND2_ID*/
};

const u32 N_CSI_RX_FE_CTRL_DLANES[N_CSI_RX_FRONTEND_ID] = {
	N_CSI_RX_DLANE_ID,	/* 4 dlanes for CSI_RX_FR0NTEND0_ID */
	N_CSI_RX_DLANE_ID,	/* 4 dlanes for CSI_RX_FR0NTEND1_ID */
	N_CSI_RX_DLANE_ID	/* 4 dlanes for CSI_RX_FR0NTEND2_ID */
};

/* sid_width for CSI_RX_BACKEND<N>_ID */
const u32 N_CSI_RX_BE_SID_WIDTH[N_CSI_RX_BACKEND_ID] = {
	3,
	2,
	2
};
