/* SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2025, NVIDIA CORPORATION. All rights reserved.
 */
#ifndef _clc36f_h_
#define _clc36f_h_

#define NVC36F_NON_STALL_INTERRUPT                                 (0x00000020)
#define NVC36F_NON_STALL_INTERRUPT_HANDLE                                 31:0
#define NVC36F_SEM_ADDR_LO                                         (0x0000005c)
#define NVC36F_SEM_ADDR_LO_OFFSET                                         31:2
#define NVC36F_SEM_ADDR_HI                                         (0x00000060)
#define NVC36F_SEM_ADDR_HI_OFFSET                                          7:0
#define NVC36F_SEM_PAYLOAD_LO                                      (0x00000064)
#define NVC36F_SEM_PAYLOAD_LO_PAYLOAD                                     31:0
#define NVC36F_SEM_PAYLOAD_HI                                      (0x00000068)
#define NVC36F_SEM_PAYLOAD_HI_PAYLOAD                                     31:0
#define NVC36F_SEM_EXECUTE                                         (0x0000006c)
#define NVC36F_SEM_EXECUTE_OPERATION                                       2:0
#define NVC36F_SEM_EXECUTE_OPERATION_ACQUIRE                        0x00000000
#define NVC36F_SEM_EXECUTE_OPERATION_RELEASE                        0x00000001
#define NVC36F_SEM_EXECUTE_OPERATION_ACQ_STRICT_GEQ                 0x00000002
#define NVC36F_SEM_EXECUTE_OPERATION_ACQ_CIRC_GEQ                   0x00000003
#define NVC36F_SEM_EXECUTE_OPERATION_ACQ_AND                        0x00000004
#define NVC36F_SEM_EXECUTE_OPERATION_ACQ_NOR                        0x00000005
#define NVC36F_SEM_EXECUTE_OPERATION_REDUCTION                      0x00000006
#define NVC36F_SEM_EXECUTE_ACQUIRE_SWITCH_TSG                            12:12
#define NVC36F_SEM_EXECUTE_ACQUIRE_SWITCH_TSG_DIS                   0x00000000
#define NVC36F_SEM_EXECUTE_ACQUIRE_SWITCH_TSG_EN                    0x00000001
#define NVC36F_SEM_EXECUTE_RELEASE_WFI                                   20:20
#define NVC36F_SEM_EXECUTE_RELEASE_WFI_DIS                          0x00000000
#define NVC36F_SEM_EXECUTE_RELEASE_WFI_EN                           0x00000001
#define NVC36F_SEM_EXECUTE_PAYLOAD_SIZE                                  24:24
#define NVC36F_SEM_EXECUTE_PAYLOAD_SIZE_32BIT                       0x00000000
#define NVC36F_SEM_EXECUTE_PAYLOAD_SIZE_64BIT                       0x00000001
#define NVC36F_SEM_EXECUTE_RELEASE_TIMESTAMP                             25:25
#define NVC36F_SEM_EXECUTE_RELEASE_TIMESTAMP_DIS                    0x00000000
#define NVC36F_SEM_EXECUTE_RELEASE_TIMESTAMP_EN                     0x00000001
#define NVC36F_SEM_EXECUTE_REDUCTION                                     30:27
#define NVC36F_SEM_EXECUTE_REDUCTION_IMIN                           0x00000000
#define NVC36F_SEM_EXECUTE_REDUCTION_IMAX                           0x00000001
#define NVC36F_SEM_EXECUTE_REDUCTION_IXOR                           0x00000002
#define NVC36F_SEM_EXECUTE_REDUCTION_IAND                           0x00000003
#define NVC36F_SEM_EXECUTE_REDUCTION_IOR                            0x00000004
#define NVC36F_SEM_EXECUTE_REDUCTION_IADD                           0x00000005
#define NVC36F_SEM_EXECUTE_REDUCTION_INC                            0x00000006
#define NVC36F_SEM_EXECUTE_REDUCTION_DEC                            0x00000007
#define NVC36F_SEM_EXECUTE_REDUCTION_FORMAT                              31:31
#define NVC36F_SEM_EXECUTE_REDUCTION_FORMAT_SIGNED                  0x00000000
#define NVC36F_SEM_EXECUTE_REDUCTION_FORMAT_UNSIGNED                0x00000001

#endif
