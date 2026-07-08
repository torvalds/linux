/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2022, Xilinx, Inc.
 * Copyright (C) 2022 - 2026, Advanced Micro Devices, Inc.
 */

#ifndef _XLNX_VERSAL_NET_POWER_H
#define _XLNX_VERSAL_NET_POWER_H

#include "xlnx-versal-power.h"

#define PM_DEV_USB_1				(0x182240d7U)
#define PM_DEV_FPD_SWDT_0			(0x182240dbU)
#define PM_DEV_FPD_SWDT_1			(0x182240dcU)
#define PM_DEV_FPD_SWDT_2			(0x182240ddU)
#define PM_DEV_FPD_SWDT_3			(0x182240deU)
#define PM_DEV_TCM_A_0A				(0x183180cbU)
#define PM_DEV_TCM_A_0B				(0x183180ccU)
#define PM_DEV_TCM_A_0C				(0x183180cdU)
#define PM_DEV_RPU_A_0				(0x181100bfU)
#define PM_DEV_LPD_SWDT_0			(0x182240d9U)
#define PM_DEV_LPD_SWDT_1			(0x182240daU)

/* Remove Versal specific node IDs */
#undef PM_DEV_RPU0_0
#undef PM_DEV_RPU0_1
#undef PM_DEV_OCM_0
#undef PM_DEV_OCM_1
#undef PM_DEV_OCM_2
#undef PM_DEV_OCM_3
#undef PM_DEV_TCM_0_A
#undef PM_DEV_TCM_1_A
#undef PM_DEV_TCM_0_B
#undef PM_DEV_TCM_1_B
#undef PM_DEV_SWDT_FPD
#undef PM_DEV_AI

#endif
