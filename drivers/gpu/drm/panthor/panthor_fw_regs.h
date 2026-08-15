/* SPDX-License-Identifier: GPL-2.0 or MIT */
/* Copyright 2026 ARM Limited. All rights reserved. */

#ifndef __PANTHOR_FW_REGS_H__
#define __PANTHOR_FW_REGS_H__

#define MCU_CONTROL_BASE				0x700

#define MCU_CONTROL					0x0
#define   MCU_CONTROL_ENABLE				1
#define   MCU_CONTROL_AUTO				2
#define   MCU_CONTROL_DISABLE				0

#define MCU_STATUS					0x4
#define   MCU_STATUS_DISABLED				0
#define   MCU_STATUS_ENABLED				1
#define   MCU_STATUS_HALT				2
#define   MCU_STATUS_FATAL				3

#define JOB_INT_BASE					0x1000
#define   JOB_INT_GLOBAL_IF				BIT(31)
#define   JOB_INT_CSG_IF(x)				BIT(x)

#define CSF_GPU_LATEST_FLUSH_ID				0x10000

#define CSF_DOORBELL(i)					(0x80000 + ((i) * 0x10000))
#define CSF_GLB_DOORBELL_ID				0

#endif /* __PANTHOR_FW_REGS_H__ */
