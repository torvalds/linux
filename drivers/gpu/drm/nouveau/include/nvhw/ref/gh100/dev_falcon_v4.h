/* SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2025, NVIDIA CORPORATION. All rights reserved.
 */
#ifndef __gh100_dev_falcon_v4_h__
#define __gh100_dev_falcon_v4_h__

#define NV_PFALCON_FALCON_MAILBOX0                                                                     0x00000040     /* RW-4R */
#define NV_PFALCON_FALCON_MAILBOX0_DATA                                                                31:0           /* RWIVF */
#define NV_PFALCON_FALCON_MAILBOX0_DATA_INIT                                                           0x00000000     /* RWI-V */
#define NV_PFALCON_FALCON_MAILBOX1                                                                     0x00000044     /* RW-4R */
#define NV_PFALCON_FALCON_MAILBOX1_DATA                                                                31:0           /* RWIVF */
#define NV_PFALCON_FALCON_MAILBOX1_DATA_INIT                                                           0x00000000     /* RWI-V */

#define NV_PFALCON_FALCON_HWCFG2                                                                       0x000000f4     /* R--4R */
#define NV_PFALCON_FALCON_HWCFG2_RISCV_BR_PRIV_LOCKDOWN                                                13:13          /* R--VF */
#define NV_PFALCON_FALCON_HWCFG2_RISCV_BR_PRIV_LOCKDOWN_LOCK                                           0x00000001     /* R---V */
#define NV_PFALCON_FALCON_HWCFG2_RISCV_BR_PRIV_LOCKDOWN_UNLOCK                                         0x00000000     /* R---V */

#endif // __gh100_dev_falcon_v4_h__
