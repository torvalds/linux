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
#ifndef _vcn_1_0_DEFAULT_HEADER
#define _vcn_1_0_DEFAULT_HEADER


// addressBlock: uvd_uvd_pg_dec
#define mmUVD_PGFSM_CONFIG_DEFAULT                                               0x00000000
#define mmUVD_PGFSM_STATUS_DEFAULT                                               0x002aaaaa
#define mmUVD_POWER_STATUS_DEFAULT                                               0x00000801
#define mmCC_UVD_HARVESTING_DEFAULT                                              0x00000000
#define mmUVD_SCRATCH1_DEFAULT                                                   0x00000000
#define mmUVD_SCRATCH2_DEFAULT                                                   0x00000000
#define mmUVD_SCRATCH3_DEFAULT                                                   0x00000000
#define mmUVD_SCRATCH4_DEFAULT                                                   0x00000000
#define mmUVD_SCRATCH5_DEFAULT                                                   0x00000000
#define mmUVD_SCRATCH6_DEFAULT                                                   0x00000000
#define mmUVD_SCRATCH7_DEFAULT                                                   0x00000000
#define mmUVD_SCRATCH8_DEFAULT                                                   0x00000000
#define mmUVD_SCRATCH9_DEFAULT                                                   0x00000000
#define mmUVD_SCRATCH10_DEFAULT                                                  0x00000000
#define mmUVD_SCRATCH11_DEFAULT                                                  0x00000000
#define mmUVD_SCRATCH12_DEFAULT                                                  0x00000000
#define mmUVD_SCRATCH13_DEFAULT                                                  0x00000000
#define mmUVD_SCRATCH14_DEFAULT                                                  0x00000000
#define mmUVD_DPG_LMI_VCPU_CACHE_64BIT_BAR_LOW_DEFAULT                           0x00000000
#define mmUVD_DPG_LMI_VCPU_CACHE_64BIT_BAR_HIGH_DEFAULT                          0x00000000
#define mmUVD_DPG_VCPU_CACHE_OFFSET0_DEFAULT                                     0x00000000


// addressBlock: uvd_uvdgendec
#define mmUVD_LCM_CGC_CNTRL_DEFAULT                                              0xa0f00000


// addressBlock: uvd_uvdnpdec
#define mmUVD_JPEG_CNTL_DEFAULT                                                  0x00000004
#define mmUVD_JPEG_RB_BASE_DEFAULT                                               0x00000000
#define mmUVD_JPEG_RB_WPTR_DEFAULT                                               0x00000000
#define mmUVD_JPEG_RB_RPTR_DEFAULT                                               0x00000000
#define mmUVD_JPEG_RB_SIZE_DEFAULT                                               0x00000000
#define mmUVD_JPEG_UV_TILING_CTRL_DEFAULT                                        0x02104800
#define mmUVD_JPEG_TILING_CTRL_DEFAULT                                           0x02104800
#define mmUVD_JPEG_ADDR_CONFIG_DEFAULT                                           0x22010010
#define mmUVD_JPEG_GPCOM_CMD_DEFAULT                                             0x00000000
#define mmUVD_JPEG_GPCOM_DATA0_DEFAULT                                           0x00000000
#define mmUVD_JPEG_GPCOM_DATA1_DEFAULT                                           0x00000000
#define mmUVD_JPEG_JRB_BASE_LO_DEFAULT                                           0x00000000
#define mmUVD_JPEG_JRB_BASE_HI_DEFAULT                                           0x00000000
#define mmUVD_JPEG_JRB_SIZE_DEFAULT                                              0x00000000
#define mmUVD_JPEG_JRB_RPTR_DEFAULT                                              0x00000000
#define mmUVD_JPEG_JRB_WPTR_DEFAULT                                              0x00000000
#define mmUVD_JPEG_UV_ADDR_CONFIG_DEFAULT                                        0x22010010
#define mmUVD_SEMA_ADDR_LOW_DEFAULT                                              0x00000000
#define mmUVD_SEMA_ADDR_HIGH_DEFAULT                                             0x00000000
#define mmUVD_SEMA_CMD_DEFAULT                                                   0x00000080
#define mmUVD_GPCOM_VCPU_CMD_DEFAULT                                             0x00000000
#define mmUVD_GPCOM_VCPU_DATA0_DEFAULT                                           0x00000000
#define mmUVD_GPCOM_VCPU_DATA1_DEFAULT                                           0x00000000
#define mmUVD_UDEC_DBW_UV_ADDR_CONFIG_DEFAULT                                    0x22010010
#define mmUVD_UDEC_ADDR_CONFIG_DEFAULT                                           0x22010010
#define mmUVD_UDEC_DB_ADDR_CONFIG_DEFAULT                                        0x22010010
#define mmUVD_UDEC_DBW_ADDR_CONFIG_DEFAULT                                       0x22010010
#define mmUVD_SUVD_CGC_GATE_DEFAULT                                              0x00000000
#define mmUVD_SUVD_CGC_STATUS_DEFAULT                                            0x00000000
#define mmUVD_SUVD_CGC_CTRL_DEFAULT                                              0x00000000
#define mmUVD_LMI_VCPU_CACHE1_64BIT_BAR_LOW_DEFAULT                              0x00000000
#define mmUVD_LMI_VCPU_CACHE1_64BIT_BAR_HIGH_DEFAULT                             0x00000000
#define mmUVD_LMI_VCPU_CACHE2_64BIT_BAR_LOW_DEFAULT                              0x00000000
#define mmUVD_LMI_VCPU_CACHE2_64BIT_BAR_HIGH_DEFAULT                             0x00000000
#define mmUVD_NO_OP_DEFAULT                                                      0x00000000
#define mmUVD_JPEG_CNTL2_DEFAULT                                                 0x00000000
#define mmUVD_VERSION_DEFAULT                                                    0x00010000
#define mmUVD_GP_SCRATCH8_DEFAULT                                                0x00000000
#define mmUVD_GP_SCRATCH9_DEFAULT                                                0x00000000
#define mmUVD_GP_SCRATCH10_DEFAULT                                               0x00000000
#define mmUVD_GP_SCRATCH11_DEFAULT                                               0x00000000
#define mmUVD_GP_SCRATCH12_DEFAULT                                               0x00000000
#define mmUVD_GP_SCRATCH13_DEFAULT                                               0x00000000
#define mmUVD_GP_SCRATCH14_DEFAULT                                               0x00000000
#define mmUVD_GP_SCRATCH15_DEFAULT                                               0x00000000
#define mmUVD_GP_SCRATCH16_DEFAULT                                               0x00000000
#define mmUVD_GP_SCRATCH17_DEFAULT                                               0x00000000
#define mmUVD_GP_SCRATCH18_DEFAULT                                               0x00000000
#define mmUVD_GP_SCRATCH19_DEFAULT                                               0x00000000
#define mmUVD_GP_SCRATCH20_DEFAULT                                               0x00000000
#define mmUVD_GP_SCRATCH21_DEFAULT                                               0x00000000
#define mmUVD_GP_SCRATCH22_DEFAULT                                               0x00000000
#define mmUVD_GP_SCRATCH23_DEFAULT                                               0x00000000
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
#define mmUVD_RB_WPTR4_DEFAULT                                                   0x00000000
#define mmUVD_JRBC_RB_RPTR_DEFAULT                                               0x00000000
#define mmUVD_LMI_VCPU_CACHE_64BIT_BAR_HIGH_DEFAULT                              0x00000000
#define mmUVD_LMI_VCPU_CACHE_64BIT_BAR_LOW_DEFAULT                               0x00000000


// addressBlock: uvd_uvddec
#define mmUVD_SEMA_CNTL_DEFAULT                                                  0x00000003
#define mmUVD_LMI_JRBC_RB_64BIT_BAR_LOW_DEFAULT                                  0x00000000
#define mmUVD_LMI_JRBC_RB_64BIT_BAR_HIGH_DEFAULT                                 0x00000000
#define mmUVD_LMI_JRBC_IB_64BIT_BAR_LOW_DEFAULT                                  0x00000000
#define mmUVD_LMI_JRBC_IB_64BIT_BAR_HIGH_DEFAULT                                 0x00000000
#define mmUVD_LMI_JRBC_IB_VMID_DEFAULT                                           0x00000000
#define mmUVD_JRBC_RB_WPTR_DEFAULT                                               0x00000000
#define mmUVD_JRBC_RB_CNTL_DEFAULT                                               0x00000100
#define mmUVD_JRBC_IB_SIZE_DEFAULT                                               0x00000000
#define mmUVD_JRBC_LMI_SWAP_CNTL_DEFAULT                                         0x00000000
#define mmUVD_JRBC_SOFT_RESET_DEFAULT                                            0x00000000
#define mmUVD_JRBC_STATUS_DEFAULT                                                0x00000003
#define mmUVD_RB_RPTR3_DEFAULT                                                   0x00000000
#define mmUVD_RB_WPTR3_DEFAULT                                                   0x00000000
#define mmUVD_RB_BASE_LO3_DEFAULT                                                0x00000000
#define mmUVD_RB_BASE_HI3_DEFAULT                                                0x00000000
#define mmUVD_RB_SIZE3_DEFAULT                                                   0x00000000
#define mmJPEG_CGC_GATE_DEFAULT                                                  0x00300000
#define mmUVD_CTX_INDEX_DEFAULT                                                  0x00000000
#define mmUVD_CTX_DATA_DEFAULT                                                   0x00000000
#define mmUVD_CGC_GATE_DEFAULT                                                   0x000fffff
#define mmUVD_CGC_STATUS_DEFAULT                                                 0x00000000
#define mmUVD_CGC_CTRL_DEFAULT                                                   0x1fff018d
#define mmUVD_GP_SCRATCH0_DEFAULT                                                0x00000000
#define mmUVD_GP_SCRATCH1_DEFAULT                                                0x00000000
#define mmUVD_GP_SCRATCH2_DEFAULT                                                0x00000000
#define mmUVD_GP_SCRATCH3_DEFAULT                                                0x00000000
#define mmUVD_GP_SCRATCH4_DEFAULT                                                0x00000000
#define mmUVD_GP_SCRATCH5_DEFAULT                                                0x00000000
#define mmUVD_GP_SCRATCH6_DEFAULT                                                0x00000000
#define mmUVD_GP_SCRATCH7_DEFAULT                                                0x00000000
#define mmUVD_LMI_VCPU_CACHE_VMID_DEFAULT                                        0x00000000
#define mmUVD_LMI_CTRL2_DEFAULT                                                  0x003e0000
#define mmUVD_MASTINT_EN_DEFAULT                                                 0x00000000
#define mmJPEG_CGC_CTRL_DEFAULT                                                  0x0000018d
#define mmUVD_LMI_CTRL_DEFAULT                                                   0x00104340
#define mmUVD_LMI_STATUS_DEFAULT                                                 0x003fff7f
#define mmUVD_LMI_VM_CTRL_DEFAULT                                                0x00000000
#define mmUVD_LMI_SWAP_CNTL_DEFAULT                                              0x00000000
#define mmUVD_MPC_SET_MUXA0_DEFAULT                                              0x00002040
#define mmUVD_MPC_SET_MUXA1_DEFAULT                                              0x00000000
#define mmUVD_MPC_SET_MUXB0_DEFAULT                                              0x00002040
#define mmUVD_MPC_SET_MUXB1_DEFAULT                                              0x00000000
#define mmUVD_MPC_SET_MUX_DEFAULT                                                0x00000088
#define mmUVD_MPC_SET_ALU_DEFAULT                                                0x00000000
#define mmUVD_GPCOM_SYS_CMD_DEFAULT                                              0x00000000
#define mmUVD_GPCOM_SYS_DATA0_DEFAULT                                            0x00000000
#define mmUVD_GPCOM_SYS_DATA1_DEFAULT                                            0x00000000
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
#define mmUVD_RBC_WPTR_POLL_CNTL_DEFAULT                                         0x00400100
#define mmUVD_RBC_WPTR_POLL_ADDR_DEFAULT                                         0x00000000
#define mmUVD_RB_BASE_LO4_DEFAULT                                                0x00000000
#define mmUVD_RB_BASE_HI4_DEFAULT                                                0x00000000
#define mmUVD_RB_SIZE4_DEFAULT                                                   0x00000000
#define mmUVD_RB_RPTR4_DEFAULT                                                   0x00000000


#endif
