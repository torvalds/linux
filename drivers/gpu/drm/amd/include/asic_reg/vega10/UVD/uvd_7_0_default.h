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
#ifndef _uvd_7_0_DEFAULT_HEADER
#define _uvd_7_0_DEFAULT_HEADER


// addressBlock: uvd0_uvd_pg_dec
#define mmUVD_POWER_STATUS_DEFAULT                                               0x00000000
#define mmUVD_DPG_RBC_RB_CNTL_DEFAULT                                            0x01000101
#define mmUVD_DPG_RBC_RB_BASE_LOW_DEFAULT                                        0x00000000
#define mmUVD_DPG_RBC_RB_BASE_HIGH_DEFAULT                                       0x00000000
#define mmUVD_DPG_RBC_RB_WPTR_CNTL_DEFAULT                                       0x00000000
#define mmUVD_DPG_RBC_RB_RPTR_DEFAULT                                            0x00000000
#define mmUVD_DPG_RBC_RB_WPTR_DEFAULT                                            0x00000000
#define mmUVD_DPG_LMI_VCPU_CACHE_64BIT_BAR_LOW_DEFAULT                           0x00000000
#define mmUVD_DPG_LMI_VCPU_CACHE_64BIT_BAR_HIGH_DEFAULT                          0x00000000
#define mmUVD_DPG_VCPU_CACHE_OFFSET0_DEFAULT                                     0x00000000


// addressBlock: uvd0_uvdnpdec
#define mmUVD_JPEG_ADDR_CONFIG_DEFAULT                                           0x22010010
#define mmUVD_GPCOM_VCPU_CMD_DEFAULT                                             0x00000000
#define mmUVD_GPCOM_VCPU_DATA0_DEFAULT                                           0x00000000
#define mmUVD_GPCOM_VCPU_DATA1_DEFAULT                                           0x00000000
#define mmUVD_UDEC_ADDR_CONFIG_DEFAULT                                           0x22010010
#define mmUVD_UDEC_DB_ADDR_CONFIG_DEFAULT                                        0x22010010
#define mmUVD_UDEC_DBW_ADDR_CONFIG_DEFAULT                                       0x22010010
#define mmUVD_SUVD_CGC_GATE_DEFAULT                                              0x00000000
#define mmUVD_SUVD_CGC_CTRL_DEFAULT                                              0x00000000
#define mmUVD_LMI_VCPU_CACHE1_64BIT_BAR_LOW_DEFAULT                              0x00000000
#define mmUVD_LMI_VCPU_CACHE1_64BIT_BAR_HIGH_DEFAULT                             0x00000000
#define mmUVD_LMI_VCPU_CACHE2_64BIT_BAR_LOW_DEFAULT                              0x00000000
#define mmUVD_LMI_VCPU_CACHE2_64BIT_BAR_HIGH_DEFAULT                             0x00000000
#define mmUVD_POWER_STATUS_U_DEFAULT                                             0x00000000
#define mmUVD_NO_OP_DEFAULT                                                      0x00000000
#define mmUVD_GP_SCRATCH8_DEFAULT                                                0x00000000
#define mmUVD_RB_BASE_LO2_DEFAULT                                                0x00000000
#define mmUVD_RB_BASE_HI2_DEFAULT                                                0x00000000
#define mmUVD_RB_SIZE2_DEFAULT                                                   0x00000000
#define mmUVD_RB_RPTR2_DEFAULT                                                   0x00000000
#define mmUVD_RB_WPTR2_DEFAULT                                                   0x00000000
#define mmUVD_RB_BASE_LO_DEFAULT                                                 0x00000000
#define mmUVD_RB_BASE_HI_DEFAULT                                                 0x00000000
#define mmUVD_RB_SIZE_DEFAULT                                                    0x00000000
#define mmUVD_RB_RPTR_DEFAULT                                                    0x00000000
#define mmUVD_RB_WPTR_DEFAULT                                                    0x00000000
#define mmUVD_JRBC_RB_RPTR_DEFAULT                                               0x00000000
#define mmUVD_LMI_VCPU_CACHE_64BIT_BAR_HIGH_DEFAULT                              0x00000000
#define mmUVD_LMI_VCPU_CACHE_64BIT_BAR_LOW_DEFAULT                               0x00000000
#define mmUVD_LMI_RBC_IB_64BIT_BAR_HIGH_DEFAULT                                  0x00000000
#define mmUVD_LMI_RBC_IB_64BIT_BAR_LOW_DEFAULT                                   0x00000000
#define mmUVD_LMI_RBC_RB_64BIT_BAR_HIGH_DEFAULT                                  0x00000000
#define mmUVD_LMI_RBC_RB_64BIT_BAR_LOW_DEFAULT                                   0x00000000


// addressBlock: uvd0_uvddec
#define mmUVD_SEMA_CNTL_DEFAULT                                                  0x00000003
#define mmUVD_LMI_JRBC_RB_64BIT_BAR_LOW_DEFAULT                                  0x00000000
#define mmUVD_JRBC_RB_WPTR_DEFAULT                                               0x00000000
#define mmUVD_RB_RPTR3_DEFAULT                                                   0x00000000
#define mmUVD_RB_WPTR3_DEFAULT                                                   0x00000000
#define mmUVD_RB_BASE_LO3_DEFAULT                                                0x00000000
#define mmUVD_RB_BASE_HI3_DEFAULT                                                0x00000000
#define mmUVD_RB_SIZE3_DEFAULT                                                   0x00000000
#define mmJPEG_CGC_GATE_DEFAULT                                                  0x00300000
#define mmUVD_CTX_INDEX_DEFAULT                                                  0x00000000
#define mmUVD_CTX_DATA_DEFAULT                                                   0x00000000
#define mmUVD_CGC_GATE_DEFAULT                                                   0x000fffff
#define mmUVD_CGC_CTRL_DEFAULT                                                   0x1fff018d
#define mmUVD_GP_SCRATCH4_DEFAULT                                                0x00000000
#define mmUVD_LMI_CTRL2_DEFAULT                                                  0x003e0000
#define mmUVD_MASTINT_EN_DEFAULT                                                 0x00000000
#define mmJPEG_CGC_CTRL_DEFAULT                                                  0x0000018d
#define mmUVD_LMI_CTRL_DEFAULT                                                   0x00104340
#define mmUVD_LMI_VM_CTRL_DEFAULT                                                0x00000000
#define mmUVD_LMI_SWAP_CNTL_DEFAULT                                              0x00000000
#define mmUVD_MP_SWAP_CNTL_DEFAULT                                               0x00000000
#define mmUVD_MPC_SET_MUXA0_DEFAULT                                              0x00002040
#define mmUVD_MPC_SET_MUXA1_DEFAULT                                              0x00000000
#define mmUVD_MPC_SET_MUXB0_DEFAULT                                              0x00002040
#define mmUVD_MPC_SET_MUXB1_DEFAULT                                              0x00000000
#define mmUVD_MPC_SET_MUX_DEFAULT                                                0x00000088
#define mmUVD_MPC_SET_ALU_DEFAULT                                                0x00000000
#define mmUVD_VCPU_CACHE_OFFSET0_DEFAULT                                         0x00000000
#define mmUVD_VCPU_CACHE_SIZE0_DEFAULT                                           0x00000000
#define mmUVD_VCPU_CACHE_OFFSET1_DEFAULT                                         0x00000000
#define mmUVD_VCPU_CACHE_SIZE1_DEFAULT                                           0x00000000
#define mmUVD_VCPU_CACHE_OFFSET2_DEFAULT                                         0x00000000
#define mmUVD_VCPU_CACHE_SIZE2_DEFAULT                                           0x00000000
#define mmUVD_VCPU_CNTL_DEFAULT                                                  0x0ff20000
#define mmUVD_SOFT_RESET_DEFAULT                                                 0x00000008
#define mmUVD_LMI_RBC_IB_VMID_DEFAULT                                            0x00000000
#define mmUVD_RBC_IB_SIZE_DEFAULT                                                0x00000000
#define mmUVD_LMI_RBC_RB_VMID_DEFAULT                                            0x00000000
#define mmUVD_RBC_RB_RPTR_DEFAULT                                                0x00000000
#define mmUVD_RBC_RB_WPTR_DEFAULT                                                0x00000000
#define mmUVD_RBC_RB_WPTR_CNTL_DEFAULT                                           0x00000000
#define mmUVD_RBC_RB_CNTL_DEFAULT                                                0x01000101
#define mmUVD_RBC_RB_RPTR_ADDR_DEFAULT                                           0x00000000
#define mmUVD_STATUS_DEFAULT                                                     0x00000000
#define mmUVD_SEMA_TIMEOUT_STATUS_DEFAULT                                        0x00000000
#define mmUVD_SEMA_WAIT_INCOMPLETE_TIMEOUT_CNTL_DEFAULT                          0x02000000
#define mmUVD_SEMA_WAIT_FAULT_TIMEOUT_CNTL_DEFAULT                               0x02000000
#define mmUVD_SEMA_SIGNAL_INCOMPLETE_TIMEOUT_CNTL_DEFAULT                        0x02000000
#define mmUVD_CONTEXT_ID_DEFAULT                                                 0x00000000
#define mmUVD_CONTEXT_ID2_DEFAULT                                                0x00000000


#endif
