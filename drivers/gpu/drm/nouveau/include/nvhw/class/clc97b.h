/* SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2025, NVIDIA CORPORATION. All rights reserved.
 */
#ifndef _clc97b_h_
#define _clc97b_h_

// dma opcode instructions
#define NVC97B_DMA
#define NVC97B_DMA_OPCODE                                                        31:29
#define NVC97B_DMA_OPCODE_METHOD                                            0x00000000
#define NVC97B_DMA_OPCODE_JUMP                                              0x00000001
#define NVC97B_DMA_OPCODE_NONINC_METHOD                                     0x00000002
#define NVC97B_DMA_OPCODE_SET_SUBDEVICE_MASK                                0x00000003
#define NVC97B_DMA_METHOD_COUNT                                                  27:18
#define NVC97B_DMA_METHOD_OFFSET                                                  15:2
#define NVC97B_DMA_DATA                                                           31:0
#define NVC97B_DMA_DATA_NOP                                                 0x00000000
#define NVC97B_DMA_JUMP_OFFSET                                                    15:2
#define NVC97B_DMA_SET_SUBDEVICE_MASK_VALUE                                       11:0

#endif // _clc97b_h
