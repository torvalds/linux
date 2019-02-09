/*
 * Copyright (C) 2017  Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN
 * AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */
#ifndef _uvd_7_0_OFFSET_HEADER
#define _uvd_7_0_OFFSET_HEADER



// addressBlock: uvd0_uvd_pg_dec
// base address: 0x1fb00
#define mmUVD_POWER_STATUS                                                                             0x00c4
#define mmUVD_POWER_STATUS_BASE_IDX                                                                    1
#define mmUVD_DPG_RBC_RB_CNTL                                                                          0x00cb
#define mmUVD_DPG_RBC_RB_CNTL_BASE_IDX                                                                 1
#define mmUVD_DPG_RBC_RB_BASE_LOW                                                                      0x00cc
#define mmUVD_DPG_RBC_RB_BASE_LOW_BASE_IDX                                                             1
#define mmUVD_DPG_RBC_RB_BASE_HIGH                                                                     0x00cd
#define mmUVD_DPG_RBC_RB_BASE_HIGH_BASE_IDX                                                            1
#define mmUVD_DPG_RBC_RB_WPTR_CNTL                                                                     0x00ce
#define mmUVD_DPG_RBC_RB_WPTR_CNTL_BASE_IDX                                                            1
#define mmUVD_DPG_RBC_RB_RPTR                                                                          0x00cf
#define mmUVD_DPG_RBC_RB_RPTR_BASE_IDX                                                                 1
#define mmUVD_DPG_RBC_RB_WPTR                                                                          0x00d0
#define mmUVD_DPG_RBC_RB_WPTR_BASE_IDX                                                                 1
#define mmUVD_DPG_LMI_VCPU_CACHE_64BIT_BAR_LOW                                                         0x00e5
#define mmUVD_DPG_LMI_VCPU_CACHE_64BIT_BAR_LOW_BASE_IDX                                                1
#define mmUVD_DPG_LMI_VCPU_CACHE_64BIT_BAR_HIGH                                                        0x00e6
#define mmUVD_DPG_LMI_VCPU_CACHE_64BIT_BAR_HIGH_BASE_IDX                                               1
#define mmUVD_DPG_VCPU_CACHE_OFFSET0                                                                   0x00e7
#define mmUVD_DPG_VCPU_CACHE_OFFSET0_BASE_IDX                                                          1


// addressBlock: uvd0_uvdnpdec
// base address: 0x20000
#define mmUVD_JPEG_ADDR_CONFIG                                                                         0x021f
#define mmUVD_JPEG_ADDR_CONFIG_BASE_IDX                                                                1
#define mmUVD_GPCOM_VCPU_CMD                                                                           0x03c3
#define mmUVD_GPCOM_VCPU_CMD_BASE_IDX                                                                  1
#define mmUVD_GPCOM_VCPU_DATA0                                                                         0x03c4
#define mmUVD_GPCOM_VCPU_DATA0_BASE_IDX                                                                1
#define mmUVD_GPCOM_VCPU_DATA1                                                                         0x03c5
#define mmUVD_GPCOM_VCPU_DATA1_BASE_IDX                                                                1
#define mmUVD_UDEC_ADDR_CONFIG                                                                         0x03d3
#define mmUVD_UDEC_ADDR_CONFIG_BASE_IDX                                                                1
#define mmUVD_UDEC_DB_ADDR_CONFIG                                                                      0x03d4
#define mmUVD_UDEC_DB_ADDR_CONFIG_BASE_IDX                                                             1
#define mmUVD_UDEC_DBW_ADDR_CONFIG                                                                     0x03d5
#define mmUVD_UDEC_DBW_ADDR_CONFIG_BASE_IDX                                                            1
#define mmUVD_SUVD_CGC_GATE                                                                            0x03e4
#define mmUVD_SUVD_CGC_GATE_BASE_IDX                                                                   1
#define mmUVD_SUVD_CGC_CTRL                                                                            0x03e6
#define mmUVD_SUVD_CGC_CTRL_BASE_IDX                                                                   1
#define mmUVD_LMI_VCPU_CACHE1_64BIT_BAR_LOW                                                            0x03ec
#define mmUVD_LMI_VCPU_CACHE1_64BIT_BAR_LOW_BASE_IDX                                                   1
#define mmUVD_LMI_VCPU_CACHE1_64BIT_BAR_HIGH                                                           0x03ed
#define mmUVD_LMI_VCPU_CACHE1_64BIT_BAR_HIGH_BASE_IDX                                                  1
#define mmUVD_LMI_VCPU_CACHE2_64BIT_BAR_LOW                                                            0x03f0
#define mmUVD_LMI_VCPU_CACHE2_64BIT_BAR_LOW_BASE_IDX                                                   1
#define mmUVD_LMI_VCPU_CACHE2_64BIT_BAR_HIGH                                                           0x03f1
#define mmUVD_LMI_VCPU_CACHE2_64BIT_BAR_HIGH_BASE_IDX                                                  1
#define mmUVD_POWER_STATUS_U                                                                           0x03fd
#define mmUVD_POWER_STATUS_U_BASE_IDX                                                                  1
#define mmUVD_NO_OP                                                                                    0x03ff
#define mmUVD_NO_OP_BASE_IDX                                                                           1
#define mmUVD_GP_SCRATCH8                                                                              0x040a
#define mmUVD_GP_SCRATCH8_BASE_IDX                                                                     1
#define mmUVD_RB_BASE_LO2                                                                              0x0421
#define mmUVD_RB_BASE_LO2_BASE_IDX                                                                     1
#define mmUVD_RB_BASE_HI2                                                                              0x0422
#define mmUVD_RB_BASE_HI2_BASE_IDX                                                                     1
#define mmUVD_RB_SIZE2                                                                                 0x0423
#define mmUVD_RB_SIZE2_BASE_IDX                                                                        1
#define mmUVD_RB_RPTR2                                                                                 0x0424
#define mmUVD_RB_RPTR2_BASE_IDX                                                                        1
#define mmUVD_RB_WPTR2                                                                                 0x0425
#define mmUVD_RB_WPTR2_BASE_IDX                                                                        1
#define mmUVD_RB_BASE_LO                                                                               0x0426
#define mmUVD_RB_BASE_LO_BASE_IDX                                                                      1
#define mmUVD_RB_BASE_HI                                                                               0x0427
#define mmUVD_RB_BASE_HI_BASE_IDX                                                                      1
#define mmUVD_RB_SIZE                                                                                  0x0428
#define mmUVD_RB_SIZE_BASE_IDX                                                                         1
#define mmUVD_RB_RPTR                                                                                  0x0429
#define mmUVD_RB_RPTR_BASE_IDX                                                                         1
#define mmUVD_RB_WPTR                                                                                  0x042a
#define mmUVD_RB_WPTR_BASE_IDX                                                                         1
#define mmUVD_JRBC_RB_RPTR                                                                             0x0457
#define mmUVD_JRBC_RB_RPTR_BASE_IDX                                                                    1
#define mmUVD_LMI_VCPU_CACHE_64BIT_BAR_HIGH                                                            0x045e
#define mmUVD_LMI_VCPU_CACHE_64BIT_BAR_HIGH_BASE_IDX                                                   1
#define mmUVD_LMI_VCPU_CACHE_64BIT_BAR_LOW                                                             0x045f
#define mmUVD_LMI_VCPU_CACHE_64BIT_BAR_LOW_BASE_IDX                                                    1
#define mmUVD_LMI_RBC_IB_64BIT_BAR_HIGH                                                                0x0466
#define mmUVD_LMI_RBC_IB_64BIT_BAR_HIGH_BASE_IDX                                                       1
#define mmUVD_LMI_RBC_IB_64BIT_BAR_LOW                                                                 0x0467
#define mmUVD_LMI_RBC_IB_64BIT_BAR_LOW_BASE_IDX                                                        1
#define mmUVD_LMI_RBC_RB_64BIT_BAR_HIGH                                                                0x0468
#define mmUVD_LMI_RBC_RB_64BIT_BAR_HIGH_BASE_IDX                                                       1
#define mmUVD_LMI_RBC_RB_64BIT_BAR_LOW                                                                 0x0469
#define mmUVD_LMI_RBC_RB_64BIT_BAR_LOW_BASE_IDX                                                        1


// addressBlock: uvd0_uvddec
// base address: 0x20c00
#define mmUVD_SEMA_CNTL                                                                                0x0500
#define mmUVD_SEMA_CNTL_BASE_IDX                                                                       1
#define mmUVD_LMI_JRBC_RB_64BIT_BAR_LOW                                                                0x0503
#define mmUVD_LMI_JRBC_RB_64BIT_BAR_LOW_BASE_IDX                                                       1
#define mmUVD_JRBC_RB_WPTR                                                                             0x0509
#define mmUVD_JRBC_RB_WPTR_BASE_IDX                                                                    1
#define mmUVD_RB_RPTR3                                                                                 0x051b
#define mmUVD_RB_RPTR3_BASE_IDX                                                                        1
#define mmUVD_RB_WPTR3                                                                                 0x051c
#define mmUVD_RB_WPTR3_BASE_IDX                                                                        1
#define mmUVD_RB_BASE_LO3                                                                              0x051d
#define mmUVD_RB_BASE_LO3_BASE_IDX                                                                     1
#define mmUVD_RB_BASE_HI3                                                                              0x051e
#define mmUVD_RB_BASE_HI3_BASE_IDX                                                                     1
#define mmUVD_RB_SIZE3                                                                                 0x051f
#define mmUVD_RB_SIZE3_BASE_IDX                                                                        1
#define mmJPEG_CGC_GATE                                                                                0x0526
#define mmJPEG_CGC_GATE_BASE_IDX                                                                       1
#define mmUVD_CTX_INDEX                                                                                0x0528
#define mmUVD_CTX_INDEX_BASE_IDX                                                                       1
#define mmUVD_CTX_DATA                                                                                 0x0529
#define mmUVD_CTX_DATA_BASE_IDX                                                                        1
#define mmUVD_CGC_GATE                                                                                 0x052a
#define mmUVD_CGC_GATE_BASE_IDX                                                                        1
#define mmUVD_CGC_CTRL                                                                                 0x052c
#define mmUVD_CGC_CTRL_BASE_IDX                                                                        1
#define mmUVD_GP_SCRATCH4                                                                              0x0538
#define mmUVD_GP_SCRATCH4_BASE_IDX                                                                     1
#define mmUVD_LMI_CTRL2                                                                                0x053d
#define mmUVD_LMI_CTRL2_BASE_IDX                                                                       1
#define mmUVD_MASTINT_EN                                                                               0x0540
#define mmUVD_MASTINT_EN_BASE_IDX                                                                      1
#define mmJPEG_CGC_CTRL                                                                                0x0565
#define mmJPEG_CGC_CTRL_BASE_IDX                                                                       1
#define mmUVD_LMI_CTRL                                                                                 0x0566
#define mmUVD_LMI_CTRL_BASE_IDX                                                                        1
#define mmUVD_LMI_VM_CTRL                                                                              0x0568
#define mmUVD_LMI_VM_CTRL_BASE_IDX                                                                     1
#define mmUVD_LMI_SWAP_CNTL                                                                            0x056d
#define mmUVD_LMI_SWAP_CNTL_BASE_IDX                                                                   1
#define mmUVD_MP_SWAP_CNTL                                                                             0x056f
#define mmUVD_MP_SWAP_CNTL_BASE_IDX                                                                    1
#define mmUVD_MPC_SET_MUXA0                                                                            0x0579
#define mmUVD_MPC_SET_MUXA0_BASE_IDX                                                                   1
#define mmUVD_MPC_SET_MUXA1                                                                            0x057a
#define mmUVD_MPC_SET_MUXA1_BASE_IDX                                                                   1
#define mmUVD_MPC_SET_MUXB0                                                                            0x057b
#define mmUVD_MPC_SET_MUXB0_BASE_IDX                                                                   1
#define mmUVD_MPC_SET_MUXB1                                                                            0x057c
#define mmUVD_MPC_SET_MUXB1_BASE_IDX                                                                   1
#define mmUVD_MPC_SET_MUX                                                                              0x057d
#define mmUVD_MPC_SET_MUX_BASE_IDX                                                                     1
#define mmUVD_MPC_SET_ALU                                                                              0x057e
#define mmUVD_MPC_SET_ALU_BASE_IDX                                                                     1
#define mmUVD_VCPU_CACHE_OFFSET0                                                                       0x0582
#define mmUVD_VCPU_CACHE_OFFSET0_BASE_IDX                                                              1
#define mmUVD_VCPU_CACHE_SIZE0                                                                         0x0583
#define mmUVD_VCPU_CACHE_SIZE0_BASE_IDX                                                                1
#define mmUVD_VCPU_CACHE_OFFSET1                                                                       0x0584
#define mmUVD_VCPU_CACHE_OFFSET1_BASE_IDX                                                              1
#define mmUVD_VCPU_CACHE_SIZE1                                                                         0x0585
#define mmUVD_VCPU_CACHE_SIZE1_BASE_IDX                                                                1
#define mmUVD_VCPU_CACHE_OFFSET2                                                                       0x0586
#define mmUVD_VCPU_CACHE_OFFSET2_BASE_IDX                                                              1
#define mmUVD_VCPU_CACHE_SIZE2                                                                         0x0587
#define mmUVD_VCPU_CACHE_SIZE2_BASE_IDX                                                                1
#define mmUVD_VCPU_CNTL                                                                                0x0598
#define mmUVD_VCPU_CNTL_BASE_IDX                                                                       1
#define mmUVD_SOFT_RESET                                                                               0x05a0
#define mmUVD_SOFT_RESET_BASE_IDX                                                                      1
#define mmUVD_LMI_RBC_IB_VMID                                                                          0x05a1
#define mmUVD_LMI_RBC_IB_VMID_BASE_IDX                                                                 1
#define mmUVD_RBC_IB_SIZE                                                                              0x05a2
#define mmUVD_RBC_IB_SIZE_BASE_IDX                                                                     1
#define mmUVD_RBC_RB_RPTR                                                                              0x05a4
#define mmUVD_RBC_RB_RPTR_BASE_IDX                                                                     1
#define mmUVD_RBC_RB_WPTR                                                                              0x05a5
#define mmUVD_RBC_RB_WPTR_BASE_IDX                                                                     1
#define mmUVD_RBC_RB_WPTR_CNTL                                                                         0x05a6
#define mmUVD_RBC_RB_WPTR_CNTL_BASE_IDX                                                                1
#define mmUVD_RBC_RB_CNTL                                                                              0x05a9
#define mmUVD_RBC_RB_CNTL_BASE_IDX                                                                     1
#define mmUVD_RBC_RB_RPTR_ADDR                                                                         0x05aa
#define mmUVD_RBC_RB_RPTR_ADDR_BASE_IDX                                                                1
#define mmUVD_STATUS                                                                                   0x05af
#define mmUVD_STATUS_BASE_IDX                                                                          1
#define mmUVD_SEMA_TIMEOUT_STATUS                                                                      0x05b0
#define mmUVD_SEMA_TIMEOUT_STATUS_BASE_IDX                                                             1
#define mmUVD_SEMA_WAIT_INCOMPLETE_TIMEOUT_CNTL                                                        0x05b1
#define mmUVD_SEMA_WAIT_INCOMPLETE_TIMEOUT_CNTL_BASE_IDX                                               1
#define mmUVD_SEMA_WAIT_FAULT_TIMEOUT_CNTL                                                             0x05b2
#define mmUVD_SEMA_WAIT_FAULT_TIMEOUT_CNTL_BASE_IDX                                                    1
#define mmUVD_SEMA_SIGNAL_INCOMPLETE_TIMEOUT_CNTL                                                      0x05b3
#define mmUVD_SEMA_SIGNAL_INCOMPLETE_TIMEOUT_CNTL_BASE_IDX                                             1
#define mmUVD_CONTEXT_ID                                                                               0x05bd
#define mmUVD_CONTEXT_ID_BASE_IDX                                                                      1
#define mmUVD_CONTEXT_ID2                                                                              0x05bf
#define mmUVD_CONTEXT_ID2_BASE_IDX                                                                     1


#endif
