/* SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2025, NVIDIA CORPORATION. All rights reserved.
 */
#ifndef __gb202_dev_therm_h__
#define __gb202_dev_therm_h__

#define NV_THERM_I2CS_SCRATCH                                                  0x00ad00bc /* RW-4R */
#define NV_THERM_I2CS_SCRATCH_DATA                                                   31:0 /* RWIVF */
#define NV_THERM_I2CS_SCRATCH_DATA_INIT                                        0x00000000 /* RWI-V */
#define NV_THERM_I2CS_SCRATCH_FSP_BOOT_COMPLETE                                   NV_THERM_I2CS_SCRATCH
#define NV_THERM_I2CS_SCRATCH_FSP_BOOT_COMPLETE_STATUS                                             31:0
#define NV_THERM_I2CS_SCRATCH_FSP_BOOT_COMPLETE_STATUS_SUCCESS                               0x000000FF
#define NV_THERM_I2CS_SCRATCH_FSP_BOOT_COMPLETE_STATUS_FAILED                                0x00000000

#endif // __gb202_dev_therm_h__

