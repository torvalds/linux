/* SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2025, NVIDIA CORPORATION. All rights reserved.
 */
#ifndef __gh100_dev_fsp_pri_h__
#define __gh100_dev_fsp_pri_h__

#define NV_PFSP             0x8F3FFF:0x8F0000 /* RW--D */

#define NV_PFSP_MSGQ_HEAD(i)                                                                             (0x008F2c80+(i)*8) /* RW-4A */
#define NV_PFSP_MSGQ_HEAD__SIZE_1                                                                        8              /*       */
#define NV_PFSP_MSGQ_HEAD_VAL                                                                            31:0           /* RWIUF */
#define NV_PFSP_MSGQ_HEAD_VAL_INIT                                                                       0x00000000     /* RWI-V */
#define NV_PFSP_MSGQ_TAIL(i)                                                                             (0x008F2c84+(i)*8) /* RW-4A */
#define NV_PFSP_MSGQ_TAIL__SIZE_1                                                                        8              /*       */
#define NV_PFSP_MSGQ_TAIL_VAL                                                                            31:0           /* RWIUF */
#define NV_PFSP_MSGQ_TAIL_VAL_INIT                                                                       0x00000000     /* RWI-V */

#define NV_PFSP_QUEUE_HEAD(i)                                                                            (0x008F2c00+(i)*8) /* RW-4A */
#define NV_PFSP_QUEUE_HEAD__SIZE_1                                                                       8              /*       */
#define NV_PFSP_QUEUE_HEAD_ADDRESS                                                                       31:0           /* RWIVF */
#define NV_PFSP_QUEUE_HEAD_ADDRESS_INIT                                                                  0x00000000     /* RWI-V */
#define NV_PFSP_QUEUE_TAIL(i)                                                                            (0x008F2c04+(i)*8) /* RW-4A */
#define NV_PFSP_QUEUE_TAIL__SIZE_1                                                                       8              /*       */
#define NV_PFSP_QUEUE_TAIL_ADDRESS                                                                       31:0           /* RWIVF */
#define NV_PFSP_QUEUE_TAIL_ADDRESS_INIT                                                                  0x00000000     /* RWI-V */

#endif // __gh100_dev_fsp_pri_h__
