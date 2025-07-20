/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef __IRIS_VPU_REGISTER_DEFINES_H__
#define __IRIS_VPU_REGISTER_DEFINES_H__

#define VCODEC_BASE_OFFS			0x00000000
#define CPU_BASE_OFFS				0x000A0000
#define WRAPPER_BASE_OFFS			0x000B0000

#define CPU_CS_BASE_OFFS			(CPU_BASE_OFFS)

#define WRAPPER_CORE_POWER_STATUS		(WRAPPER_BASE_OFFS + 0x80)

#endif
