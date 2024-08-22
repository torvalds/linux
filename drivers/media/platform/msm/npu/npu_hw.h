/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2018-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2023-2024, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef NPU_HW_H
#define NPU_HW_H

/* -------------------------------------------------------------------------
 * Defines
 * -------------------------------------------------------------------------
 */
#define NPU_HW_VERSION (0x00000000)
#define NPU_MASTERn_IPC_IRQ_OUT(n) (0x00001004+0x1000*(n))
#define NPU_CACHE_ATTR_IDn___POR 0x00011100
#define NPU_CACHE_ATTR_IDn(n) (0x00000800+0x4*(n))
#define NPU_MASTERn_IPC_IRQ_IN_CTRL(n) (0x00001008+0x1000*(n))
#define NPU_MASTER0_IPC_IRQ_IN_CTRL__IRQ_SOURCE_SELECT___S 4
#define NPU_MASTERn_IPC_IRQ_OUT_CTRL(n) (0x00001004+0x1000*(n))
#define NPU_MASTER0_IPC_IRQ_OUT_CTRL__IRQ_TYPE_PULSE 4
#define NPU_GPR0 (0x00000100)
#define NPU_MASTERn_ERROR_IRQ_STATUS(n) (0x00001010+0x1000*(n))
#define NPU_MASTERn_ERROR_IRQ_INCLUDE(n) (0x00001014+0x1000*(n))
#define NPU_MASTERn_ERROR_IRQ_ENABLE(n) (0x00001018+0x1000*(n))
#define NPU_MASTERn_ERROR_IRQ_CLEAR(n) (0x0000101C+0x1000*(n))
#define NPU_MASTERn_ERROR_IRQ_SET(n) (0x00001020+0x1000*(n))
#define NPU_MASTERn_ERROR_IRQ_OWNER(n) (0x00007000+4*(n))
#define NPU_ERROR_IRQ_MASK 0x000000E3
#define NPU_MASTERn_WDOG_IRQ_STATUS(n) (0x00001030+0x1000*(n))
#define NPU_WDOG_BITE_IRQ_STATUS (1 << 1)
#define NPU_MASTERn_WDOG_IRQ_INCLUDE(n) (0x00001034+0x1000*(n))
#define NPU_WDOG_BITE_IRQ_INCLUDE (1 << 1)
#define NPU_MASTERn_WDOG_IRQ_OWNER(n) (0x00007010+4*(n))
#define NPU_WDOG_IRQ_MASK 0x00000002


#define NPU_GPR1 (0x00000104)
#define NPU_GPR2 (0x00000108)
#define NPU_GPR3 (0x0000010C)
#define NPU_GPR4 (0x00000110)
#define NPU_GPR13 (0x00000134)
#define NPU_GPR14 (0x00000138)
#define NPU_GPR15 (0x0000013C)

#define BWMON2_SAMPLING_WINDOW (0x000003A8)
#define BWMON2_BYTE_COUNT_THRESHOLD_HIGH (0x000003AC)
#define BWMON2_BYTE_COUNT_THRESHOLD_MEDIUM (0x000003B0)
#define BWMON2_BYTE_COUNT_THRESHOLD_LOW (0x000003B4)
#define BWMON2_ZONE_ACTIONS (0x000003B8)
#define BWMON2_ZONE_COUNT_THRESHOLD (0x000003BC)

#endif /* NPU_HW_H */
