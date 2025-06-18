/* SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2025, NVIDIA CORPORATION. All rights reserved.
 */
#ifndef __gh100_dev_riscv_pri_h__
#define __gh100_dev_riscv_pri_h__

#define NV_PRISCV_RISCV_CPUCTL                                                                         0x00000388     /* RW-4R */
#define NV_PRISCV_RISCV_CPUCTL_HALTED                                                                  4:4            /* R-IVF */
#define NV_PRISCV_RISCV_CPUCTL_HALTED_INIT                                                             0x00000001     /* R-I-V */
#define NV_PRISCV_RISCV_CPUCTL_HALTED_TRUE                                                             0x00000001     /* R---V */
#define NV_PRISCV_RISCV_CPUCTL_HALTED_FALSE                                                            0x00000000     /* R---V */

#endif // __gh100_dev_riscv_pri_h__
