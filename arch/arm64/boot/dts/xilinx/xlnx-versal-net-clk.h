/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2022, Xilinx, Inc.
 * Copyright (C) 2022 - 2026, Advanced Micro Devices, Inc.
 */

#ifndef _XLNX_VERSAL_NET_CLK_H
#define _XLNX_VERSAL_NET_CLK_H

#include "xlnx-versal-clk.h"

#define CAN0_REF_2X	0x9e
#define CAN1_REF_2X	0xac
#define FPD_WWDT0	0xb5
#define FPD_WWDT1	0xb6
#define FPD_WWDT2	0xb7
#define FPD_WWDT3	0xb8
#define LPD_WWDT0	0xb9
#define LPD_WWDT1	0xba
#define ACPU_0		0x98
#define ACPU_1		0x9b
#define ACPU_2		0x9a
#define ACPU_3		0x99
#define I3C0_REF	0x9d
#define I3C1_REF	0x9f
#define USB1_BUS_REF	0xae
#define LPD_WWDT	0xad

/* Remove Versal specific node IDs */
#undef APU_PLL
#undef RPU_PLL
#undef CPM_PLL
#undef APU_PRESRC
#undef APU_POSTCLK
#undef APU_PLL_OUT
#undef APLL
#undef RPU_PRESRC
#undef RPU_POSTCLK
#undef RPU_PLL_OUT
#undef RPLL
#undef CPM_PRESRC
#undef CPM_POSTCLK
#undef CPM_PLL_OUT
#undef CPLL
#undef APLL_TO_XPD
#undef RPLL_TO_XPD
#undef RCLK_PMC
#undef RCLK_LPD
#undef WDT
#undef MUXED_IRO_DIV2
#undef MUXED_IRO_DIV4
#undef PSM_REF
#undef CPM_CORE_REF
#undef CPM_LSBUS_REF
#undef CPM_DBG_REF
#undef CPM_AUX0_REF
#undef CPM_AUX1_REF
#undef CPU_R5
#undef CPU_R5_CORE
#undef CPU_R5_OCM
#undef CPU_R5_OCM2
#undef CAN0_REF
#undef CAN1_REF
#undef I2C0_REF
#undef I2C1_REF
#undef CPM_TOPSW_REF
#undef USB3_DUAL_REF
#undef MUXED_IRO
#undef PL_EXT
#undef PL_LB
#undef MIO_50_OR_51
#undef MIO_24_OR_25

#endif
