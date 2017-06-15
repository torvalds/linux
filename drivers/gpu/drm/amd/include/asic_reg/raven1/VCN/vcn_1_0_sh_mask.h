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
#ifndef _vcn_1_0_SH_MASK_HEADER
#define _vcn_1_0_SH_MASK_HEADER


// addressBlock: uvd_uvd_pg_dec
//UVD_PGFSM_CONFIG
#define UVD_PGFSM_CONFIG__UVDM_PWR_CONFIG__SHIFT                                                              0x0
#define UVD_PGFSM_CONFIG__UVDU_PWR_CONFIG__SHIFT                                                              0x2
#define UVD_PGFSM_CONFIG__UVDF_PWR_CONFIG__SHIFT                                                              0x4
#define UVD_PGFSM_CONFIG__UVDC_PWR_CONFIG__SHIFT                                                              0x6
#define UVD_PGFSM_CONFIG__UVDB_PWR_CONFIG__SHIFT                                                              0x8
#define UVD_PGFSM_CONFIG__UVDIL_PWR_CONFIG__SHIFT                                                             0xa
#define UVD_PGFSM_CONFIG__UVDIR_PWR_CONFIG__SHIFT                                                             0xc
#define UVD_PGFSM_CONFIG__UVDTD_PWR_CONFIG__SHIFT                                                             0xe
#define UVD_PGFSM_CONFIG__UVDTE_PWR_CONFIG__SHIFT                                                             0x10
#define UVD_PGFSM_CONFIG__UVDE_PWR_CONFIG__SHIFT                                                              0x12
#define UVD_PGFSM_CONFIG__UVDW_PWR_CONFIG__SHIFT                                                              0x14
#define UVD_PGFSM_CONFIG__UVDM_PWR_CONFIG_MASK                                                                0x00000003L
#define UVD_PGFSM_CONFIG__UVDU_PWR_CONFIG_MASK                                                                0x0000000CL
#define UVD_PGFSM_CONFIG__UVDF_PWR_CONFIG_MASK                                                                0x00000030L
#define UVD_PGFSM_CONFIG__UVDC_PWR_CONFIG_MASK                                                                0x000000C0L
#define UVD_PGFSM_CONFIG__UVDB_PWR_CONFIG_MASK                                                                0x00000300L
#define UVD_PGFSM_CONFIG__UVDIL_PWR_CONFIG_MASK                                                               0x00000C00L
#define UVD_PGFSM_CONFIG__UVDIR_PWR_CONFIG_MASK                                                               0x00003000L
#define UVD_PGFSM_CONFIG__UVDTD_PWR_CONFIG_MASK                                                               0x0000C000L
#define UVD_PGFSM_CONFIG__UVDTE_PWR_CONFIG_MASK                                                               0x00030000L
#define UVD_PGFSM_CONFIG__UVDE_PWR_CONFIG_MASK                                                                0x000C0000L
#define UVD_PGFSM_CONFIG__UVDW_PWR_CONFIG_MASK                                                                0x00300000L
//UVD_PGFSM_STATUS
#define UVD_PGFSM_STATUS__UVDM_PWR_STATUS__SHIFT                                                              0x0
#define UVD_PGFSM_STATUS__UVDU_PWR_STATUS__SHIFT                                                              0x2
#define UVD_PGFSM_STATUS__UVDF_PWR_STATUS__SHIFT                                                              0x4
#define UVD_PGFSM_STATUS__UVDC_PWR_STATUS__SHIFT                                                              0x6
#define UVD_PGFSM_STATUS__UVDB_PWR_STATUS__SHIFT                                                              0x8
#define UVD_PGFSM_STATUS__UVDIL_PWR_STATUS__SHIFT                                                             0xa
#define UVD_PGFSM_STATUS__UVDIR_PWR_STATUS__SHIFT                                                             0xc
#define UVD_PGFSM_STATUS__UVDTD_PWR_STATUS__SHIFT                                                             0xe
#define UVD_PGFSM_STATUS__UVDTE_PWR_STATUS__SHIFT                                                             0x10
#define UVD_PGFSM_STATUS__UVDE_PWR_STATUS__SHIFT                                                              0x12
#define UVD_PGFSM_STATUS__UVDW_PWR_STATUS__SHIFT                                                              0x14
#define UVD_PGFSM_STATUS__UVDM_PWR_STATUS_MASK                                                                0x00000003L
#define UVD_PGFSM_STATUS__UVDU_PWR_STATUS_MASK                                                                0x0000000CL
#define UVD_PGFSM_STATUS__UVDF_PWR_STATUS_MASK                                                                0x00000030L
#define UVD_PGFSM_STATUS__UVDC_PWR_STATUS_MASK                                                                0x000000C0L
#define UVD_PGFSM_STATUS__UVDB_PWR_STATUS_MASK                                                                0x00000300L
#define UVD_PGFSM_STATUS__UVDIL_PWR_STATUS_MASK                                                               0x00000C00L
#define UVD_PGFSM_STATUS__UVDIR_PWR_STATUS_MASK                                                               0x00003000L
#define UVD_PGFSM_STATUS__UVDTD_PWR_STATUS_MASK                                                               0x0000C000L
#define UVD_PGFSM_STATUS__UVDTE_PWR_STATUS_MASK                                                               0x00030000L
#define UVD_PGFSM_STATUS__UVDE_PWR_STATUS_MASK                                                                0x000C0000L
#define UVD_PGFSM_STATUS__UVDW_PWR_STATUS_MASK                                                                0x00300000L
//UVD_POWER_STATUS
#define UVD_POWER_STATUS__UVD_POWER_STATUS__SHIFT                                                             0x0
#define UVD_POWER_STATUS__UVD_PG_MODE__SHIFT                                                                  0x2
#define UVD_POWER_STATUS__UVD_CG_MODE__SHIFT                                                                  0x4
#define UVD_POWER_STATUS__UVD_PG_EN__SHIFT                                                                    0x8
#define UVD_POWER_STATUS__RBC_SNOOP_DIS__SHIFT                                                                0x9
#define UVD_POWER_STATUS__JRBC_SNOOP_DIS__SHIFT                                                               0xa
#define UVD_POWER_STATUS__SW_RB_SNOOP_DIS__SHIFT                                                              0xb
#define UVD_POWER_STATUS__UVD_POWER_STATUS_MASK                                                               0x00000003L
#define UVD_POWER_STATUS__UVD_PG_MODE_MASK                                                                    0x00000004L
#define UVD_POWER_STATUS__UVD_CG_MODE_MASK                                                                    0x00000030L
#define UVD_POWER_STATUS__UVD_PG_EN_MASK                                                                      0x00000100L
#define UVD_POWER_STATUS__RBC_SNOOP_DIS_MASK                                                                  0x00000200L
#define UVD_POWER_STATUS__JRBC_SNOOP_DIS_MASK                                                                 0x00000400L
#define UVD_POWER_STATUS__SW_RB_SNOOP_DIS_MASK                                                                0x00000800L
//CC_UVD_HARVESTING
#define CC_UVD_HARVESTING__UVD_DISABLE__SHIFT                                                                 0x1
#define CC_UVD_HARVESTING__UVD_DISABLE_MASK                                                                   0x00000002L
//UVD_SCRATCH1
#define UVD_SCRATCH1__SCRATCH1_DATA__SHIFT                                                                    0x0
#define UVD_SCRATCH1__SCRATCH1_DATA_MASK                                                                      0xFFFFFFFFL
//UVD_SCRATCH2
#define UVD_SCRATCH2__SCRATCH2_DATA__SHIFT                                                                    0x0
#define UVD_SCRATCH2__SCRATCH2_DATA_MASK                                                                      0xFFFFFFFFL
//UVD_SCRATCH3
#define UVD_SCRATCH3__SCRATCH3_DATA__SHIFT                                                                    0x0
#define UVD_SCRATCH3__SCRATCH3_DATA_MASK                                                                      0xFFFFFFFFL
//UVD_SCRATCH4
#define UVD_SCRATCH4__SCRATCH4_DATA__SHIFT                                                                    0x0
#define UVD_SCRATCH4__SCRATCH4_DATA_MASK                                                                      0xFFFFFFFFL
//UVD_SCRATCH5
#define UVD_SCRATCH5__SCRATCH5_DATA__SHIFT                                                                    0x0
#define UVD_SCRATCH5__SCRATCH5_DATA_MASK                                                                      0xFFFFFFFFL
//UVD_SCRATCH6
#define UVD_SCRATCH6__SCRATCH6_DATA__SHIFT                                                                    0x0
#define UVD_SCRATCH6__SCRATCH6_DATA_MASK                                                                      0xFFFFFFFFL
//UVD_SCRATCH7
#define UVD_SCRATCH7__SCRATCH7_DATA__SHIFT                                                                    0x0
#define UVD_SCRATCH7__SCRATCH7_DATA_MASK                                                                      0xFFFFFFFFL
//UVD_SCRATCH8
#define UVD_SCRATCH8__SCRATCH8_DATA__SHIFT                                                                    0x0
#define UVD_SCRATCH8__SCRATCH8_DATA_MASK                                                                      0xFFFFFFFFL
//UVD_SCRATCH9
#define UVD_SCRATCH9__SCRATCH9_DATA__SHIFT                                                                    0x0
#define UVD_SCRATCH9__SCRATCH9_DATA_MASK                                                                      0xFFFFFFFFL
//UVD_SCRATCH10
#define UVD_SCRATCH10__SCRATCH10_DATA__SHIFT                                                                  0x0
#define UVD_SCRATCH10__SCRATCH10_DATA_MASK                                                                    0xFFFFFFFFL
//UVD_SCRATCH11
#define UVD_SCRATCH11__SCRATCH11_DATA__SHIFT                                                                  0x0
#define UVD_SCRATCH11__SCRATCH11_DATA_MASK                                                                    0xFFFFFFFFL
//UVD_SCRATCH12
#define UVD_SCRATCH12__SCRATCH12_DATA__SHIFT                                                                  0x0
#define UVD_SCRATCH12__SCRATCH12_DATA_MASK                                                                    0xFFFFFFFFL
//UVD_SCRATCH13
#define UVD_SCRATCH13__SCRATCH13_DATA__SHIFT                                                                  0x0
#define UVD_SCRATCH13__SCRATCH13_DATA_MASK                                                                    0xFFFFFFFFL
//UVD_SCRATCH14
#define UVD_SCRATCH14__SCRATCH14_DATA__SHIFT                                                                  0x0
#define UVD_SCRATCH14__SCRATCH14_DATA_MASK                                                                    0xFFFFFFFFL
//UVD_DPG_LMI_VCPU_CACHE_64BIT_BAR_LOW
#define UVD_DPG_LMI_VCPU_CACHE_64BIT_BAR_LOW__BITS_31_0__SHIFT                                                0x0
#define UVD_DPG_LMI_VCPU_CACHE_64BIT_BAR_LOW__BITS_31_0_MASK                                                  0xFFFFFFFFL
//UVD_DPG_LMI_VCPU_CACHE_64BIT_BAR_HIGH
#define UVD_DPG_LMI_VCPU_CACHE_64BIT_BAR_HIGH__BITS_63_32__SHIFT                                              0x0
#define UVD_DPG_LMI_VCPU_CACHE_64BIT_BAR_HIGH__BITS_63_32_MASK                                                0xFFFFFFFFL
//UVD_DPG_VCPU_CACHE_OFFSET0
#define UVD_DPG_VCPU_CACHE_OFFSET0__CACHE_OFFSET0__SHIFT                                                      0x0
#define UVD_DPG_VCPU_CACHE_OFFSET0__CACHE_OFFSET0_MASK                                                        0x01FFFFFFL


// addressBlock: uvd_uvdgendec
//UVD_LCM_CGC_CNTRL
#define UVD_LCM_CGC_CNTRL__FORCE_OFF__SHIFT                                                                   0x12
#define UVD_LCM_CGC_CNTRL__FORCE_ON__SHIFT                                                                    0x13
#define UVD_LCM_CGC_CNTRL__OFF_DELAY__SHIFT                                                                   0x14
#define UVD_LCM_CGC_CNTRL__ON_DELAY__SHIFT                                                                    0x1c
#define UVD_LCM_CGC_CNTRL__FORCE_OFF_MASK                                                                     0x00040000L
#define UVD_LCM_CGC_CNTRL__FORCE_ON_MASK                                                                      0x00080000L
#define UVD_LCM_CGC_CNTRL__OFF_DELAY_MASK                                                                     0x0FF00000L
#define UVD_LCM_CGC_CNTRL__ON_DELAY_MASK                                                                      0xF0000000L


// addressBlock: uvd_uvdnpdec
//UVD_JPEG_CNTL
#define UVD_JPEG_CNTL__SOFT_RESET__SHIFT                                                                      0x0
#define UVD_JPEG_CNTL__REQUEST_EN__SHIFT                                                                      0x1
#define UVD_JPEG_CNTL__ERR_RST_EN__SHIFT                                                                      0x2
#define UVD_JPEG_CNTL__HUFF_SPEED_EN__SHIFT                                                                   0x3
#define UVD_JPEG_CNTL__HUFF_SPEED_STATUS__SHIFT                                                               0x4
#define UVD_JPEG_CNTL__DBG_MUX_SEL__SHIFT                                                                     0x8
#define UVD_JPEG_CNTL__SOFT_RESET_MASK                                                                        0x00000001L
#define UVD_JPEG_CNTL__REQUEST_EN_MASK                                                                        0x00000002L
#define UVD_JPEG_CNTL__ERR_RST_EN_MASK                                                                        0x00000004L
#define UVD_JPEG_CNTL__HUFF_SPEED_EN_MASK                                                                     0x00000008L
#define UVD_JPEG_CNTL__HUFF_SPEED_STATUS_MASK                                                                 0x00000010L
#define UVD_JPEG_CNTL__DBG_MUX_SEL_MASK                                                                       0x00007F00L
//UVD_JPEG_RB_BASE
#define UVD_JPEG_RB_BASE__RB_BYTE_OFF__SHIFT                                                                  0x0
#define UVD_JPEG_RB_BASE__RB_BASE__SHIFT                                                                      0x6
#define UVD_JPEG_RB_BASE__RB_BYTE_OFF_MASK                                                                    0x0000003FL
#define UVD_JPEG_RB_BASE__RB_BASE_MASK                                                                        0xFFFFFFC0L
//UVD_JPEG_RB_WPTR
#define UVD_JPEG_RB_WPTR__RB_WPTR__SHIFT                                                                      0x4
#define UVD_JPEG_RB_WPTR__RB_WPTR_MASK                                                                        0x3FFFFFF0L
//UVD_JPEG_RB_RPTR
#define UVD_JPEG_RB_RPTR__RB_RPTR__SHIFT                                                                      0x4
#define UVD_JPEG_RB_RPTR__RB_RPTR_MASK                                                                        0x3FFFFFF0L
//UVD_JPEG_RB_SIZE
#define UVD_JPEG_RB_SIZE__RB_SIZE__SHIFT                                                                      0x4
#define UVD_JPEG_RB_SIZE__RB_SIZE_MASK                                                                        0x3FFFFFF0L
//UVD_JPEG_ADDR_CONFIG
#define UVD_JPEG_ADDR_CONFIG__NUM_PIPES__SHIFT                                                                0x0
#define UVD_JPEG_ADDR_CONFIG__PIPE_INTERLEAVE_SIZE__SHIFT                                                     0x3
#define UVD_JPEG_ADDR_CONFIG__MAX_COMPRESSED_FRAGS__SHIFT                                                     0x6
#define UVD_JPEG_ADDR_CONFIG__BANK_INTERLEAVE_SIZE__SHIFT                                                     0x8
#define UVD_JPEG_ADDR_CONFIG__NUM_BANKS__SHIFT                                                                0xc
#define UVD_JPEG_ADDR_CONFIG__SHADER_ENGINE_TILE_SIZE__SHIFT                                                  0x10
#define UVD_JPEG_ADDR_CONFIG__NUM_SHADER_ENGINES__SHIFT                                                       0x13
#define UVD_JPEG_ADDR_CONFIG__NUM_GPUS__SHIFT                                                                 0x15
#define UVD_JPEG_ADDR_CONFIG__MULTI_GPU_TILE_SIZE__SHIFT                                                      0x18
#define UVD_JPEG_ADDR_CONFIG__NUM_RB_PER_SE__SHIFT                                                            0x1a
#define UVD_JPEG_ADDR_CONFIG__ROW_SIZE__SHIFT                                                                 0x1c
#define UVD_JPEG_ADDR_CONFIG__NUM_LOWER_PIPES__SHIFT                                                          0x1e
#define UVD_JPEG_ADDR_CONFIG__SE_ENABLE__SHIFT                                                                0x1f
#define UVD_JPEG_ADDR_CONFIG__NUM_PIPES_MASK                                                                  0x00000007L
#define UVD_JPEG_ADDR_CONFIG__PIPE_INTERLEAVE_SIZE_MASK                                                       0x00000038L
#define UVD_JPEG_ADDR_CONFIG__MAX_COMPRESSED_FRAGS_MASK                                                       0x000000C0L
#define UVD_JPEG_ADDR_CONFIG__BANK_INTERLEAVE_SIZE_MASK                                                       0x00000700L
#define UVD_JPEG_ADDR_CONFIG__NUM_BANKS_MASK                                                                  0x00007000L
#define UVD_JPEG_ADDR_CONFIG__SHADER_ENGINE_TILE_SIZE_MASK                                                    0x00070000L
#define UVD_JPEG_ADDR_CONFIG__NUM_SHADER_ENGINES_MASK                                                         0x00180000L
#define UVD_JPEG_ADDR_CONFIG__NUM_GPUS_MASK                                                                   0x00E00000L
#define UVD_JPEG_ADDR_CONFIG__MULTI_GPU_TILE_SIZE_MASK                                                        0x03000000L
#define UVD_JPEG_ADDR_CONFIG__NUM_RB_PER_SE_MASK                                                              0x0C000000L
#define UVD_JPEG_ADDR_CONFIG__ROW_SIZE_MASK                                                                   0x30000000L
#define UVD_JPEG_ADDR_CONFIG__NUM_LOWER_PIPES_MASK                                                            0x40000000L
#define UVD_JPEG_ADDR_CONFIG__SE_ENABLE_MASK                                                                  0x80000000L
//UVD_JPEG_GPCOM_CMD
#define UVD_JPEG_GPCOM_CMD__CMD_SEND__SHIFT                                                                   0x0
#define UVD_JPEG_GPCOM_CMD__CMD__SHIFT                                                                        0x1
#define UVD_JPEG_GPCOM_CMD__CMD_SOURCE__SHIFT                                                                 0x1f
#define UVD_JPEG_GPCOM_CMD__CMD_SEND_MASK                                                                     0x00000001L
#define UVD_JPEG_GPCOM_CMD__CMD_MASK                                                                          0x7FFFFFFEL
#define UVD_JPEG_GPCOM_CMD__CMD_SOURCE_MASK                                                                   0x80000000L
//UVD_JPEG_GPCOM_DATA0
#define UVD_JPEG_GPCOM_DATA0__DATA0__SHIFT                                                                    0x0
#define UVD_JPEG_GPCOM_DATA0__DATA0_MASK                                                                      0xFFFFFFFFL
//UVD_JPEG_GPCOM_DATA1
#define UVD_JPEG_GPCOM_DATA1__DATA1__SHIFT                                                                    0x0
#define UVD_JPEG_GPCOM_DATA1__DATA1_MASK                                                                      0xFFFFFFFFL
//UVD_JPEG_JRB_BASE_LO
#define UVD_JPEG_JRB_BASE_LO__JRB_BASE_LO__SHIFT                                                              0x6
#define UVD_JPEG_JRB_BASE_LO__JRB_BASE_LO_MASK                                                                0xFFFFFFC0L
//UVD_JPEG_JRB_BASE_HI
#define UVD_JPEG_JRB_BASE_HI__JRB_BASE_HI__SHIFT                                                              0x0
#define UVD_JPEG_JRB_BASE_HI__JRB_BASE_HI_MASK                                                                0xFFFFFFFFL
//UVD_JPEG_JRB_SIZE
#define UVD_JPEG_JRB_SIZE__JRB_SIZE__SHIFT                                                                    0x4
#define UVD_JPEG_JRB_SIZE__JRB_SIZE_MASK                                                                      0x007FFFF0L
//UVD_JPEG_JRB_RPTR
#define UVD_JPEG_JRB_RPTR__JRB_RPTR__SHIFT                                                                    0x4
#define UVD_JPEG_JRB_RPTR__JRB_RPTR_MASK                                                                      0x007FFFF0L
//UVD_JPEG_JRB_WPTR
#define UVD_JPEG_JRB_WPTR__JRB_WPTR__SHIFT                                                                    0x4
#define UVD_JPEG_JRB_WPTR__JRB_WPTR_MASK                                                                      0x007FFFF0L
//UVD_JPEG_UV_ADDR_CONFIG
#define UVD_JPEG_UV_ADDR_CONFIG__NUM_PIPES__SHIFT                                                             0x0
#define UVD_JPEG_UV_ADDR_CONFIG__PIPE_INTERLEAVE_SIZE__SHIFT                                                  0x3
#define UVD_JPEG_UV_ADDR_CONFIG__MAX_COMPRESSED_FRAGS__SHIFT                                                  0x6
#define UVD_JPEG_UV_ADDR_CONFIG__BANK_INTERLEAVE_SIZE__SHIFT                                                  0x8
#define UVD_JPEG_UV_ADDR_CONFIG__NUM_BANKS__SHIFT                                                             0xc
#define UVD_JPEG_UV_ADDR_CONFIG__SHADER_ENGINE_TILE_SIZE__SHIFT                                               0x10
#define UVD_JPEG_UV_ADDR_CONFIG__NUM_SHADER_ENGINES__SHIFT                                                    0x13
#define UVD_JPEG_UV_ADDR_CONFIG__NUM_GPUS__SHIFT                                                              0x15
#define UVD_JPEG_UV_ADDR_CONFIG__MULTI_GPU_TILE_SIZE__SHIFT                                                   0x18
#define UVD_JPEG_UV_ADDR_CONFIG__NUM_RB_PER_SE__SHIFT                                                         0x1a
#define UVD_JPEG_UV_ADDR_CONFIG__ROW_SIZE__SHIFT                                                              0x1c
#define UVD_JPEG_UV_ADDR_CONFIG__NUM_LOWER_PIPES__SHIFT                                                       0x1e
#define UVD_JPEG_UV_ADDR_CONFIG__SE_ENABLE__SHIFT                                                             0x1f
#define UVD_JPEG_UV_ADDR_CONFIG__NUM_PIPES_MASK                                                               0x00000007L
#define UVD_JPEG_UV_ADDR_CONFIG__PIPE_INTERLEAVE_SIZE_MASK                                                    0x00000038L
#define UVD_JPEG_UV_ADDR_CONFIG__MAX_COMPRESSED_FRAGS_MASK                                                    0x000000C0L
#define UVD_JPEG_UV_ADDR_CONFIG__BANK_INTERLEAVE_SIZE_MASK                                                    0x00000700L
#define UVD_JPEG_UV_ADDR_CONFIG__NUM_BANKS_MASK                                                               0x00007000L
#define UVD_JPEG_UV_ADDR_CONFIG__SHADER_ENGINE_TILE_SIZE_MASK                                                 0x00070000L
#define UVD_JPEG_UV_ADDR_CONFIG__NUM_SHADER_ENGINES_MASK                                                      0x00180000L
#define UVD_JPEG_UV_ADDR_CONFIG__NUM_GPUS_MASK                                                                0x00E00000L
#define UVD_JPEG_UV_ADDR_CONFIG__MULTI_GPU_TILE_SIZE_MASK                                                     0x03000000L
#define UVD_JPEG_UV_ADDR_CONFIG__NUM_RB_PER_SE_MASK                                                           0x0C000000L
#define UVD_JPEG_UV_ADDR_CONFIG__ROW_SIZE_MASK                                                                0x30000000L
#define UVD_JPEG_UV_ADDR_CONFIG__NUM_LOWER_PIPES_MASK                                                         0x40000000L
#define UVD_JPEG_UV_ADDR_CONFIG__SE_ENABLE_MASK                                                               0x80000000L
//UVD_SEMA_ADDR_LOW
#define UVD_SEMA_ADDR_LOW__ADDR_26_3__SHIFT                                                                   0x0
#define UVD_SEMA_ADDR_LOW__ADDR_26_3_MASK                                                                     0x00FFFFFFL
//UVD_SEMA_ADDR_HIGH
#define UVD_SEMA_ADDR_HIGH__ADDR_47_27__SHIFT                                                                 0x0
#define UVD_SEMA_ADDR_HIGH__ADDR_47_27_MASK                                                                   0x001FFFFFL
//UVD_SEMA_CMD
#define UVD_SEMA_CMD__REQ_CMD__SHIFT                                                                          0x0
#define UVD_SEMA_CMD__WR_PHASE__SHIFT                                                                         0x4
#define UVD_SEMA_CMD__MODE__SHIFT                                                                             0x6
#define UVD_SEMA_CMD__VMID_EN__SHIFT                                                                          0x7
#define UVD_SEMA_CMD__VMID__SHIFT                                                                             0x8
#define UVD_SEMA_CMD__REQ_CMD_MASK                                                                            0x0000000FL
#define UVD_SEMA_CMD__WR_PHASE_MASK                                                                           0x00000030L
#define UVD_SEMA_CMD__MODE_MASK                                                                               0x00000040L
#define UVD_SEMA_CMD__VMID_EN_MASK                                                                            0x00000080L
#define UVD_SEMA_CMD__VMID_MASK                                                                               0x00000F00L
//UVD_GPCOM_VCPU_CMD
#define UVD_GPCOM_VCPU_CMD__CMD_SEND__SHIFT                                                                   0x0
#define UVD_GPCOM_VCPU_CMD__CMD__SHIFT                                                                        0x1
#define UVD_GPCOM_VCPU_CMD__CMD_SOURCE__SHIFT                                                                 0x1f
#define UVD_GPCOM_VCPU_CMD__CMD_SEND_MASK                                                                     0x00000001L
#define UVD_GPCOM_VCPU_CMD__CMD_MASK                                                                          0x7FFFFFFEL
#define UVD_GPCOM_VCPU_CMD__CMD_SOURCE_MASK                                                                   0x80000000L
//UVD_GPCOM_VCPU_DATA0
#define UVD_GPCOM_VCPU_DATA0__DATA0__SHIFT                                                                    0x0
#define UVD_GPCOM_VCPU_DATA0__DATA0_MASK                                                                      0xFFFFFFFFL
//UVD_GPCOM_VCPU_DATA1
#define UVD_GPCOM_VCPU_DATA1__DATA1__SHIFT                                                                    0x0
#define UVD_GPCOM_VCPU_DATA1__DATA1_MASK                                                                      0xFFFFFFFFL
//UVD_UDEC_DBW_UV_ADDR_CONFIG
#define UVD_UDEC_DBW_UV_ADDR_CONFIG__NUM_PIPES__SHIFT                                                         0x0
#define UVD_UDEC_DBW_UV_ADDR_CONFIG__PIPE_INTERLEAVE_SIZE__SHIFT                                              0x3
#define UVD_UDEC_DBW_UV_ADDR_CONFIG__MAX_COMPRESSED_FRAGS__SHIFT                                              0x6
#define UVD_UDEC_DBW_UV_ADDR_CONFIG__BANK_INTERLEAVE_SIZE__SHIFT                                              0x8
#define UVD_UDEC_DBW_UV_ADDR_CONFIG__NUM_BANKS__SHIFT                                                         0xc
#define UVD_UDEC_DBW_UV_ADDR_CONFIG__SHADER_ENGINE_TILE_SIZE__SHIFT                                           0x10
#define UVD_UDEC_DBW_UV_ADDR_CONFIG__NUM_SHADER_ENGINES__SHIFT                                                0x13
#define UVD_UDEC_DBW_UV_ADDR_CONFIG__NUM_GPUS__SHIFT                                                          0x15
#define UVD_UDEC_DBW_UV_ADDR_CONFIG__MULTI_GPU_TILE_SIZE__SHIFT                                               0x18
#define UVD_UDEC_DBW_UV_ADDR_CONFIG__NUM_RB_PER_SE__SHIFT                                                     0x1a
#define UVD_UDEC_DBW_UV_ADDR_CONFIG__ROW_SIZE__SHIFT                                                          0x1c
#define UVD_UDEC_DBW_UV_ADDR_CONFIG__NUM_LOWER_PIPES__SHIFT                                                   0x1e
#define UVD_UDEC_DBW_UV_ADDR_CONFIG__SE_ENABLE__SHIFT                                                         0x1f
#define UVD_UDEC_DBW_UV_ADDR_CONFIG__NUM_PIPES_MASK                                                           0x00000007L
#define UVD_UDEC_DBW_UV_ADDR_CONFIG__PIPE_INTERLEAVE_SIZE_MASK                                                0x00000038L
#define UVD_UDEC_DBW_UV_ADDR_CONFIG__MAX_COMPRESSED_FRAGS_MASK                                                0x000000C0L
#define UVD_UDEC_DBW_UV_ADDR_CONFIG__BANK_INTERLEAVE_SIZE_MASK                                                0x00000700L
#define UVD_UDEC_DBW_UV_ADDR_CONFIG__NUM_BANKS_MASK                                                           0x00007000L
#define UVD_UDEC_DBW_UV_ADDR_CONFIG__SHADER_ENGINE_TILE_SIZE_MASK                                             0x00070000L
#define UVD_UDEC_DBW_UV_ADDR_CONFIG__NUM_SHADER_ENGINES_MASK                                                  0x00180000L
#define UVD_UDEC_DBW_UV_ADDR_CONFIG__NUM_GPUS_MASK                                                            0x00E00000L
#define UVD_UDEC_DBW_UV_ADDR_CONFIG__MULTI_GPU_TILE_SIZE_MASK                                                 0x03000000L
#define UVD_UDEC_DBW_UV_ADDR_CONFIG__NUM_RB_PER_SE_MASK                                                       0x0C000000L
#define UVD_UDEC_DBW_UV_ADDR_CONFIG__ROW_SIZE_MASK                                                            0x30000000L
#define UVD_UDEC_DBW_UV_ADDR_CONFIG__NUM_LOWER_PIPES_MASK                                                     0x40000000L
#define UVD_UDEC_DBW_UV_ADDR_CONFIG__SE_ENABLE_MASK                                                           0x80000000L
//UVD_UDEC_ADDR_CONFIG
#define UVD_UDEC_ADDR_CONFIG__NUM_PIPES__SHIFT                                                                0x0
#define UVD_UDEC_ADDR_CONFIG__PIPE_INTERLEAVE_SIZE__SHIFT                                                     0x3
#define UVD_UDEC_ADDR_CONFIG__MAX_COMPRESSED_FRAGS__SHIFT                                                     0x6
#define UVD_UDEC_ADDR_CONFIG__BANK_INTERLEAVE_SIZE__SHIFT                                                     0x8
#define UVD_UDEC_ADDR_CONFIG__NUM_BANKS__SHIFT                                                                0xc
#define UVD_UDEC_ADDR_CONFIG__SHADER_ENGINE_TILE_SIZE__SHIFT                                                  0x10
#define UVD_UDEC_ADDR_CONFIG__NUM_SHADER_ENGINES__SHIFT                                                       0x13
#define UVD_UDEC_ADDR_CONFIG__NUM_GPUS__SHIFT                                                                 0x15
#define UVD_UDEC_ADDR_CONFIG__MULTI_GPU_TILE_SIZE__SHIFT                                                      0x18
#define UVD_UDEC_ADDR_CONFIG__NUM_RB_PER_SE__SHIFT                                                            0x1a
#define UVD_UDEC_ADDR_CONFIG__ROW_SIZE__SHIFT                                                                 0x1c
#define UVD_UDEC_ADDR_CONFIG__NUM_LOWER_PIPES__SHIFT                                                          0x1e
#define UVD_UDEC_ADDR_CONFIG__SE_ENABLE__SHIFT                                                                0x1f
#define UVD_UDEC_ADDR_CONFIG__NUM_PIPES_MASK                                                                  0x00000007L
#define UVD_UDEC_ADDR_CONFIG__PIPE_INTERLEAVE_SIZE_MASK                                                       0x00000038L
#define UVD_UDEC_ADDR_CONFIG__MAX_COMPRESSED_FRAGS_MASK                                                       0x000000C0L
#define UVD_UDEC_ADDR_CONFIG__BANK_INTERLEAVE_SIZE_MASK                                                       0x00000700L
#define UVD_UDEC_ADDR_CONFIG__NUM_BANKS_MASK                                                                  0x00007000L
#define UVD_UDEC_ADDR_CONFIG__SHADER_ENGINE_TILE_SIZE_MASK                                                    0x00070000L
#define UVD_UDEC_ADDR_CONFIG__NUM_SHADER_ENGINES_MASK                                                         0x00180000L
#define UVD_UDEC_ADDR_CONFIG__NUM_GPUS_MASK                                                                   0x00E00000L
#define UVD_UDEC_ADDR_CONFIG__MULTI_GPU_TILE_SIZE_MASK                                                        0x03000000L
#define UVD_UDEC_ADDR_CONFIG__NUM_RB_PER_SE_MASK                                                              0x0C000000L
#define UVD_UDEC_ADDR_CONFIG__ROW_SIZE_MASK                                                                   0x30000000L
#define UVD_UDEC_ADDR_CONFIG__NUM_LOWER_PIPES_MASK                                                            0x40000000L
#define UVD_UDEC_ADDR_CONFIG__SE_ENABLE_MASK                                                                  0x80000000L
//UVD_UDEC_DB_ADDR_CONFIG
#define UVD_UDEC_DB_ADDR_CONFIG__NUM_PIPES__SHIFT                                                             0x0
#define UVD_UDEC_DB_ADDR_CONFIG__PIPE_INTERLEAVE_SIZE__SHIFT                                                  0x3
#define UVD_UDEC_DB_ADDR_CONFIG__MAX_COMPRESSED_FRAGS__SHIFT                                                  0x6
#define UVD_UDEC_DB_ADDR_CONFIG__BANK_INTERLEAVE_SIZE__SHIFT                                                  0x8
#define UVD_UDEC_DB_ADDR_CONFIG__NUM_BANKS__SHIFT                                                             0xc
#define UVD_UDEC_DB_ADDR_CONFIG__SHADER_ENGINE_TILE_SIZE__SHIFT                                               0x10
#define UVD_UDEC_DB_ADDR_CONFIG__NUM_SHADER_ENGINES__SHIFT                                                    0x13
#define UVD_UDEC_DB_ADDR_CONFIG__NUM_GPUS__SHIFT                                                              0x15
#define UVD_UDEC_DB_ADDR_CONFIG__MULTI_GPU_TILE_SIZE__SHIFT                                                   0x18
#define UVD_UDEC_DB_ADDR_CONFIG__NUM_RB_PER_SE__SHIFT                                                         0x1a
#define UVD_UDEC_DB_ADDR_CONFIG__ROW_SIZE__SHIFT                                                              0x1c
#define UVD_UDEC_DB_ADDR_CONFIG__NUM_LOWER_PIPES__SHIFT                                                       0x1e
#define UVD_UDEC_DB_ADDR_CONFIG__SE_ENABLE__SHIFT                                                             0x1f
#define UVD_UDEC_DB_ADDR_CONFIG__NUM_PIPES_MASK                                                               0x00000007L
#define UVD_UDEC_DB_ADDR_CONFIG__PIPE_INTERLEAVE_SIZE_MASK                                                    0x00000038L
#define UVD_UDEC_DB_ADDR_CONFIG__MAX_COMPRESSED_FRAGS_MASK                                                    0x000000C0L
#define UVD_UDEC_DB_ADDR_CONFIG__BANK_INTERLEAVE_SIZE_MASK                                                    0x00000700L
#define UVD_UDEC_DB_ADDR_CONFIG__NUM_BANKS_MASK                                                               0x00007000L
#define UVD_UDEC_DB_ADDR_CONFIG__SHADER_ENGINE_TILE_SIZE_MASK                                                 0x00070000L
#define UVD_UDEC_DB_ADDR_CONFIG__NUM_SHADER_ENGINES_MASK                                                      0x00180000L
#define UVD_UDEC_DB_ADDR_CONFIG__NUM_GPUS_MASK                                                                0x00E00000L
#define UVD_UDEC_DB_ADDR_CONFIG__MULTI_GPU_TILE_SIZE_MASK                                                     0x03000000L
#define UVD_UDEC_DB_ADDR_CONFIG__NUM_RB_PER_SE_MASK                                                           0x0C000000L
#define UVD_UDEC_DB_ADDR_CONFIG__ROW_SIZE_MASK                                                                0x30000000L
#define UVD_UDEC_DB_ADDR_CONFIG__NUM_LOWER_PIPES_MASK                                                         0x40000000L
#define UVD_UDEC_DB_ADDR_CONFIG__SE_ENABLE_MASK                                                               0x80000000L
//UVD_UDEC_DBW_ADDR_CONFIG
#define UVD_UDEC_DBW_ADDR_CONFIG__NUM_PIPES__SHIFT                                                            0x0
#define UVD_UDEC_DBW_ADDR_CONFIG__PIPE_INTERLEAVE_SIZE__SHIFT                                                 0x3
#define UVD_UDEC_DBW_ADDR_CONFIG__MAX_COMPRESSED_FRAGS__SHIFT                                                 0x6
#define UVD_UDEC_DBW_ADDR_CONFIG__BANK_INTERLEAVE_SIZE__SHIFT                                                 0x8
#define UVD_UDEC_DBW_ADDR_CONFIG__NUM_BANKS__SHIFT                                                            0xc
#define UVD_UDEC_DBW_ADDR_CONFIG__SHADER_ENGINE_TILE_SIZE__SHIFT                                              0x10
#define UVD_UDEC_DBW_ADDR_CONFIG__NUM_SHADER_ENGINES__SHIFT                                                   0x13
#define UVD_UDEC_DBW_ADDR_CONFIG__NUM_GPUS__SHIFT                                                             0x15
#define UVD_UDEC_DBW_ADDR_CONFIG__MULTI_GPU_TILE_SIZE__SHIFT                                                  0x18
#define UVD_UDEC_DBW_ADDR_CONFIG__NUM_RB_PER_SE__SHIFT                                                        0x1a
#define UVD_UDEC_DBW_ADDR_CONFIG__ROW_SIZE__SHIFT                                                             0x1c
#define UVD_UDEC_DBW_ADDR_CONFIG__NUM_LOWER_PIPES__SHIFT                                                      0x1e
#define UVD_UDEC_DBW_ADDR_CONFIG__SE_ENABLE__SHIFT                                                            0x1f
#define UVD_UDEC_DBW_ADDR_CONFIG__NUM_PIPES_MASK                                                              0x00000007L
#define UVD_UDEC_DBW_ADDR_CONFIG__PIPE_INTERLEAVE_SIZE_MASK                                                   0x00000038L
#define UVD_UDEC_DBW_ADDR_CONFIG__MAX_COMPRESSED_FRAGS_MASK                                                   0x000000C0L
#define UVD_UDEC_DBW_ADDR_CONFIG__BANK_INTERLEAVE_SIZE_MASK                                                   0x00000700L
#define UVD_UDEC_DBW_ADDR_CONFIG__NUM_BANKS_MASK                                                              0x00007000L
#define UVD_UDEC_DBW_ADDR_CONFIG__SHADER_ENGINE_TILE_SIZE_MASK                                                0x00070000L
#define UVD_UDEC_DBW_ADDR_CONFIG__NUM_SHADER_ENGINES_MASK                                                     0x00180000L
#define UVD_UDEC_DBW_ADDR_CONFIG__NUM_GPUS_MASK                                                               0x00E00000L
#define UVD_UDEC_DBW_ADDR_CONFIG__MULTI_GPU_TILE_SIZE_MASK                                                    0x03000000L
#define UVD_UDEC_DBW_ADDR_CONFIG__NUM_RB_PER_SE_MASK                                                          0x0C000000L
#define UVD_UDEC_DBW_ADDR_CONFIG__ROW_SIZE_MASK                                                               0x30000000L
#define UVD_UDEC_DBW_ADDR_CONFIG__NUM_LOWER_PIPES_MASK                                                        0x40000000L
#define UVD_UDEC_DBW_ADDR_CONFIG__SE_ENABLE_MASK                                                              0x80000000L
//UVD_SUVD_CGC_GATE
#define UVD_SUVD_CGC_GATE__SRE__SHIFT                                                                         0x0
#define UVD_SUVD_CGC_GATE__SIT__SHIFT                                                                         0x1
#define UVD_SUVD_CGC_GATE__SMP__SHIFT                                                                         0x2
#define UVD_SUVD_CGC_GATE__SCM__SHIFT                                                                         0x3
#define UVD_SUVD_CGC_GATE__SDB__SHIFT                                                                         0x4
#define UVD_SUVD_CGC_GATE__SRE_H264__SHIFT                                                                    0x5
#define UVD_SUVD_CGC_GATE__SRE_HEVC__SHIFT                                                                    0x6
#define UVD_SUVD_CGC_GATE__SIT_H264__SHIFT                                                                    0x7
#define UVD_SUVD_CGC_GATE__SIT_HEVC__SHIFT                                                                    0x8
#define UVD_SUVD_CGC_GATE__SCM_H264__SHIFT                                                                    0x9
#define UVD_SUVD_CGC_GATE__SCM_HEVC__SHIFT                                                                    0xa
#define UVD_SUVD_CGC_GATE__SDB_H264__SHIFT                                                                    0xb
#define UVD_SUVD_CGC_GATE__SDB_HEVC__SHIFT                                                                    0xc
#define UVD_SUVD_CGC_GATE__SCLR__SHIFT                                                                        0xd
#define UVD_SUVD_CGC_GATE__UVD_SC__SHIFT                                                                      0xe
#define UVD_SUVD_CGC_GATE__ENT__SHIFT                                                                         0xf
#define UVD_SUVD_CGC_GATE__IME__SHIFT                                                                         0x10
#define UVD_SUVD_CGC_GATE__SIT_HEVC_DEC__SHIFT                                                                0x11
#define UVD_SUVD_CGC_GATE__SIT_HEVC_ENC__SHIFT                                                                0x12
#define UVD_SUVD_CGC_GATE__SITE__SHIFT                                                                        0x13
#define UVD_SUVD_CGC_GATE__SRE_VP9__SHIFT                                                                     0x14
#define UVD_SUVD_CGC_GATE__SCM_VP9__SHIFT                                                                     0x15
#define UVD_SUVD_CGC_GATE__SIT_VP9_DEC__SHIFT                                                                 0x16
#define UVD_SUVD_CGC_GATE__SDB_VP9__SHIFT                                                                     0x17
#define UVD_SUVD_CGC_GATE__IME_HEVC__SHIFT                                                                    0x18
#define UVD_SUVD_CGC_GATE__SRE_MASK                                                                           0x00000001L
#define UVD_SUVD_CGC_GATE__SIT_MASK                                                                           0x00000002L
#define UVD_SUVD_CGC_GATE__SMP_MASK                                                                           0x00000004L
#define UVD_SUVD_CGC_GATE__SCM_MASK                                                                           0x00000008L
#define UVD_SUVD_CGC_GATE__SDB_MASK                                                                           0x00000010L
#define UVD_SUVD_CGC_GATE__SRE_H264_MASK                                                                      0x00000020L
#define UVD_SUVD_CGC_GATE__SRE_HEVC_MASK                                                                      0x00000040L
#define UVD_SUVD_CGC_GATE__SIT_H264_MASK                                                                      0x00000080L
#define UVD_SUVD_CGC_GATE__SIT_HEVC_MASK                                                                      0x00000100L
#define UVD_SUVD_CGC_GATE__SCM_H264_MASK                                                                      0x00000200L
#define UVD_SUVD_CGC_GATE__SCM_HEVC_MASK                                                                      0x00000400L
#define UVD_SUVD_CGC_GATE__SDB_H264_MASK                                                                      0x00000800L
#define UVD_SUVD_CGC_GATE__SDB_HEVC_MASK                                                                      0x00001000L
#define UVD_SUVD_CGC_GATE__SCLR_MASK                                                                          0x00002000L
#define UVD_SUVD_CGC_GATE__UVD_SC_MASK                                                                        0x00004000L
#define UVD_SUVD_CGC_GATE__ENT_MASK                                                                           0x00008000L
#define UVD_SUVD_CGC_GATE__IME_MASK                                                                           0x00010000L
#define UVD_SUVD_CGC_GATE__SIT_HEVC_DEC_MASK                                                                  0x00020000L
#define UVD_SUVD_CGC_GATE__SIT_HEVC_ENC_MASK                                                                  0x00040000L
#define UVD_SUVD_CGC_GATE__SITE_MASK                                                                          0x00080000L
#define UVD_SUVD_CGC_GATE__SRE_VP9_MASK                                                                       0x00100000L
#define UVD_SUVD_CGC_GATE__SCM_VP9_MASK                                                                       0x00200000L
#define UVD_SUVD_CGC_GATE__SIT_VP9_DEC_MASK                                                                   0x00400000L
#define UVD_SUVD_CGC_GATE__SDB_VP9_MASK                                                                       0x00800000L
#define UVD_SUVD_CGC_GATE__IME_HEVC_MASK                                                                      0x01000000L
//UVD_SUVD_CGC_STATUS
#define UVD_SUVD_CGC_STATUS__SRE_VCLK__SHIFT                                                                  0x0
#define UVD_SUVD_CGC_STATUS__SRE_DCLK__SHIFT                                                                  0x1
#define UVD_SUVD_CGC_STATUS__SIT_DCLK__SHIFT                                                                  0x2
#define UVD_SUVD_CGC_STATUS__SMP_DCLK__SHIFT                                                                  0x3
#define UVD_SUVD_CGC_STATUS__SCM_DCLK__SHIFT                                                                  0x4
#define UVD_SUVD_CGC_STATUS__SDB_DCLK__SHIFT                                                                  0x5
#define UVD_SUVD_CGC_STATUS__SRE_H264_VCLK__SHIFT                                                             0x6
#define UVD_SUVD_CGC_STATUS__SRE_HEVC_VCLK__SHIFT                                                             0x7
#define UVD_SUVD_CGC_STATUS__SIT_H264_DCLK__SHIFT                                                             0x8
#define UVD_SUVD_CGC_STATUS__SIT_HEVC_DCLK__SHIFT                                                             0x9
#define UVD_SUVD_CGC_STATUS__SCM_H264_DCLK__SHIFT                                                             0xa
#define UVD_SUVD_CGC_STATUS__SCM_HEVC_DCLK__SHIFT                                                             0xb
#define UVD_SUVD_CGC_STATUS__SDB_H264_DCLK__SHIFT                                                             0xc
#define UVD_SUVD_CGC_STATUS__SDB_HEVC_DCLK__SHIFT                                                             0xd
#define UVD_SUVD_CGC_STATUS__SCLR_DCLK__SHIFT                                                                 0xe
#define UVD_SUVD_CGC_STATUS__UVD_SC__SHIFT                                                                    0xf
#define UVD_SUVD_CGC_STATUS__ENT_DCLK__SHIFT                                                                  0x10
#define UVD_SUVD_CGC_STATUS__IME_DCLK__SHIFT                                                                  0x11
#define UVD_SUVD_CGC_STATUS__SIT_HEVC_DEC_DCLK__SHIFT                                                         0x12
#define UVD_SUVD_CGC_STATUS__SIT_HEVC_ENC_DCLK__SHIFT                                                         0x13
#define UVD_SUVD_CGC_STATUS__SITE_DCLK__SHIFT                                                                 0x14
#define UVD_SUVD_CGC_STATUS__SITE_HEVC_DCLK__SHIFT                                                            0x15
#define UVD_SUVD_CGC_STATUS__SITE_HEVC_ENC_DCLK__SHIFT                                                        0x16
#define UVD_SUVD_CGC_STATUS__SRE_VP9_VCLK__SHIFT                                                              0x17
#define UVD_SUVD_CGC_STATUS__SCM_VP9_VCLK__SHIFT                                                              0x18
#define UVD_SUVD_CGC_STATUS__SIT_VP9_DEC_DCLK__SHIFT                                                          0x19
#define UVD_SUVD_CGC_STATUS__SDB_VP9_DCLK__SHIFT                                                              0x1a
#define UVD_SUVD_CGC_STATUS__IME_HEVC_DCLK__SHIFT                                                             0x1b
#define UVD_SUVD_CGC_STATUS__SRE_VCLK_MASK                                                                    0x00000001L
#define UVD_SUVD_CGC_STATUS__SRE_DCLK_MASK                                                                    0x00000002L
#define UVD_SUVD_CGC_STATUS__SIT_DCLK_MASK                                                                    0x00000004L
#define UVD_SUVD_CGC_STATUS__SMP_DCLK_MASK                                                                    0x00000008L
#define UVD_SUVD_CGC_STATUS__SCM_DCLK_MASK                                                                    0x00000010L
#define UVD_SUVD_CGC_STATUS__SDB_DCLK_MASK                                                                    0x00000020L
#define UVD_SUVD_CGC_STATUS__SRE_H264_VCLK_MASK                                                               0x00000040L
#define UVD_SUVD_CGC_STATUS__SRE_HEVC_VCLK_MASK                                                               0x00000080L
#define UVD_SUVD_CGC_STATUS__SIT_H264_DCLK_MASK                                                               0x00000100L
#define UVD_SUVD_CGC_STATUS__SIT_HEVC_DCLK_MASK                                                               0x00000200L
#define UVD_SUVD_CGC_STATUS__SCM_H264_DCLK_MASK                                                               0x00000400L
#define UVD_SUVD_CGC_STATUS__SCM_HEVC_DCLK_MASK                                                               0x00000800L
#define UVD_SUVD_CGC_STATUS__SDB_H264_DCLK_MASK                                                               0x00001000L
#define UVD_SUVD_CGC_STATUS__SDB_HEVC_DCLK_MASK                                                               0x00002000L
#define UVD_SUVD_CGC_STATUS__SCLR_DCLK_MASK                                                                   0x00004000L
#define UVD_SUVD_CGC_STATUS__UVD_SC_MASK                                                                      0x00008000L
#define UVD_SUVD_CGC_STATUS__ENT_DCLK_MASK                                                                    0x00010000L
#define UVD_SUVD_CGC_STATUS__IME_DCLK_MASK                                                                    0x00020000L
#define UVD_SUVD_CGC_STATUS__SIT_HEVC_DEC_DCLK_MASK                                                           0x00040000L
#define UVD_SUVD_CGC_STATUS__SIT_HEVC_ENC_DCLK_MASK                                                           0x00080000L
#define UVD_SUVD_CGC_STATUS__SITE_DCLK_MASK                                                                   0x00100000L
#define UVD_SUVD_CGC_STATUS__SITE_HEVC_DCLK_MASK                                                              0x00200000L
#define UVD_SUVD_CGC_STATUS__SITE_HEVC_ENC_DCLK_MASK                                                          0x00400000L
#define UVD_SUVD_CGC_STATUS__SRE_VP9_VCLK_MASK                                                                0x00800000L
#define UVD_SUVD_CGC_STATUS__SCM_VP9_VCLK_MASK                                                                0x01000000L
#define UVD_SUVD_CGC_STATUS__SIT_VP9_DEC_DCLK_MASK                                                            0x02000000L
#define UVD_SUVD_CGC_STATUS__SDB_VP9_DCLK_MASK                                                                0x04000000L
#define UVD_SUVD_CGC_STATUS__IME_HEVC_DCLK_MASK                                                               0x08000000L
//UVD_SUVD_CGC_CTRL
#define UVD_SUVD_CGC_CTRL__SRE_MODE__SHIFT                                                                    0x0
#define UVD_SUVD_CGC_CTRL__SIT_MODE__SHIFT                                                                    0x1
#define UVD_SUVD_CGC_CTRL__SMP_MODE__SHIFT                                                                    0x2
#define UVD_SUVD_CGC_CTRL__SCM_MODE__SHIFT                                                                    0x3
#define UVD_SUVD_CGC_CTRL__SDB_MODE__SHIFT                                                                    0x4
#define UVD_SUVD_CGC_CTRL__SCLR_MODE__SHIFT                                                                   0x5
#define UVD_SUVD_CGC_CTRL__UVD_SC_MODE__SHIFT                                                                 0x6
#define UVD_SUVD_CGC_CTRL__ENT_MODE__SHIFT                                                                    0x7
#define UVD_SUVD_CGC_CTRL__IME_MODE__SHIFT                                                                    0x8
#define UVD_SUVD_CGC_CTRL__SITE_MODE__SHIFT                                                                   0x9
#define UVD_SUVD_CGC_CTRL__SRE_MODE_MASK                                                                      0x00000001L
#define UVD_SUVD_CGC_CTRL__SIT_MODE_MASK                                                                      0x00000002L
#define UVD_SUVD_CGC_CTRL__SMP_MODE_MASK                                                                      0x00000004L
#define UVD_SUVD_CGC_CTRL__SCM_MODE_MASK                                                                      0x00000008L
#define UVD_SUVD_CGC_CTRL__SDB_MODE_MASK                                                                      0x00000010L
#define UVD_SUVD_CGC_CTRL__SCLR_MODE_MASK                                                                     0x00000020L
#define UVD_SUVD_CGC_CTRL__UVD_SC_MODE_MASK                                                                   0x00000040L
#define UVD_SUVD_CGC_CTRL__ENT_MODE_MASK                                                                      0x00000080L
#define UVD_SUVD_CGC_CTRL__IME_MODE_MASK                                                                      0x00000100L
#define UVD_SUVD_CGC_CTRL__SITE_MODE_MASK                                                                     0x00000200L
//UVD_LMI_VCPU_CACHE1_64BIT_BAR_LOW
#define UVD_LMI_VCPU_CACHE1_64BIT_BAR_LOW__BITS_31_0__SHIFT                                                   0x0
#define UVD_LMI_VCPU_CACHE1_64BIT_BAR_LOW__BITS_31_0_MASK                                                     0xFFFFFFFFL
//UVD_LMI_VCPU_CACHE1_64BIT_BAR_HIGH
#define UVD_LMI_VCPU_CACHE1_64BIT_BAR_HIGH__BITS_63_32__SHIFT                                                 0x0
#define UVD_LMI_VCPU_CACHE1_64BIT_BAR_HIGH__BITS_63_32_MASK                                                   0xFFFFFFFFL
//UVD_LMI_VCPU_CACHE2_64BIT_BAR_LOW
#define UVD_LMI_VCPU_CACHE2_64BIT_BAR_LOW__BITS_31_0__SHIFT                                                   0x0
#define UVD_LMI_VCPU_CACHE2_64BIT_BAR_LOW__BITS_31_0_MASK                                                     0xFFFFFFFFL
//UVD_LMI_VCPU_CACHE2_64BIT_BAR_HIGH
#define UVD_LMI_VCPU_CACHE2_64BIT_BAR_HIGH__BITS_63_32__SHIFT                                                 0x0
#define UVD_LMI_VCPU_CACHE2_64BIT_BAR_HIGH__BITS_63_32_MASK                                                   0xFFFFFFFFL
//UVD_NO_OP
#define UVD_NO_OP__NO_OP__SHIFT                                                                               0x0
#define UVD_NO_OP__NO_OP_MASK                                                                                 0xFFFFFFFFL
//UVD_VERSION
#define UVD_VERSION__MINOR_VERSION__SHIFT                                                                     0x0
#define UVD_VERSION__MAJOR_VERSION__SHIFT                                                                     0x10
#define UVD_VERSION__MINOR_VERSION_MASK                                                                       0x0000FFFFL
#define UVD_VERSION__MAJOR_VERSION_MASK                                                                       0xFFFF0000L
//UVD_GP_SCRATCH8
#define UVD_GP_SCRATCH8__DATA__SHIFT                                                                          0x0
#define UVD_GP_SCRATCH8__DATA_MASK                                                                            0xFFFFFFFFL
//UVD_GP_SCRATCH9
#define UVD_GP_SCRATCH9__DATA__SHIFT                                                                          0x0
#define UVD_GP_SCRATCH9__DATA_MASK                                                                            0xFFFFFFFFL
//UVD_GP_SCRATCH10
#define UVD_GP_SCRATCH10__DATA__SHIFT                                                                         0x0
#define UVD_GP_SCRATCH10__DATA_MASK                                                                           0xFFFFFFFFL
//UVD_GP_SCRATCH11
#define UVD_GP_SCRATCH11__DATA__SHIFT                                                                         0x0
#define UVD_GP_SCRATCH11__DATA_MASK                                                                           0xFFFFFFFFL
//UVD_GP_SCRATCH12
#define UVD_GP_SCRATCH12__DATA__SHIFT                                                                         0x0
#define UVD_GP_SCRATCH12__DATA_MASK                                                                           0xFFFFFFFFL
//UVD_GP_SCRATCH13
#define UVD_GP_SCRATCH13__DATA__SHIFT                                                                         0x0
#define UVD_GP_SCRATCH13__DATA_MASK                                                                           0xFFFFFFFFL
//UVD_GP_SCRATCH14
#define UVD_GP_SCRATCH14__DATA__SHIFT                                                                         0x0
#define UVD_GP_SCRATCH14__DATA_MASK                                                                           0xFFFFFFFFL
//UVD_GP_SCRATCH15
#define UVD_GP_SCRATCH15__DATA__SHIFT                                                                         0x0
#define UVD_GP_SCRATCH15__DATA_MASK                                                                           0xFFFFFFFFL
//UVD_GP_SCRATCH16
#define UVD_GP_SCRATCH16__DATA__SHIFT                                                                         0x0
#define UVD_GP_SCRATCH16__DATA_MASK                                                                           0xFFFFFFFFL
//UVD_GP_SCRATCH17
#define UVD_GP_SCRATCH17__DATA__SHIFT                                                                         0x0
#define UVD_GP_SCRATCH17__DATA_MASK                                                                           0xFFFFFFFFL
//UVD_GP_SCRATCH18
#define UVD_GP_SCRATCH18__DATA__SHIFT                                                                         0x0
#define UVD_GP_SCRATCH18__DATA_MASK                                                                           0xFFFFFFFFL
//UVD_GP_SCRATCH19
#define UVD_GP_SCRATCH19__DATA__SHIFT                                                                         0x0
#define UVD_GP_SCRATCH19__DATA_MASK                                                                           0xFFFFFFFFL
//UVD_GP_SCRATCH20
#define UVD_GP_SCRATCH20__DATA__SHIFT                                                                         0x0
#define UVD_GP_SCRATCH20__DATA_MASK                                                                           0xFFFFFFFFL
//UVD_GP_SCRATCH21
#define UVD_GP_SCRATCH21__DATA__SHIFT                                                                         0x0
#define UVD_GP_SCRATCH21__DATA_MASK                                                                           0xFFFFFFFFL
//UVD_GP_SCRATCH22
#define UVD_GP_SCRATCH22__DATA__SHIFT                                                                         0x0
#define UVD_GP_SCRATCH22__DATA_MASK                                                                           0xFFFFFFFFL
//UVD_GP_SCRATCH23
#define UVD_GP_SCRATCH23__DATA__SHIFT                                                                         0x0
#define UVD_GP_SCRATCH23__DATA_MASK                                                                           0xFFFFFFFFL
//UVD_RB_BASE_LO2
#define UVD_RB_BASE_LO2__RB_BASE_LO__SHIFT                                                                    0x6
#define UVD_RB_BASE_LO2__RB_BASE_LO_MASK                                                                      0xFFFFFFC0L
//UVD_RB_BASE_HI2
#define UVD_RB_BASE_HI2__RB_BASE_HI__SHIFT                                                                    0x0
#define UVD_RB_BASE_HI2__RB_BASE_HI_MASK                                                                      0xFFFFFFFFL
//UVD_RB_SIZE2
#define UVD_RB_SIZE2__RB_SIZE__SHIFT                                                                          0x4
#define UVD_RB_SIZE2__RB_SIZE_MASK                                                                            0x007FFFF0L
//UVD_RB_RPTR2
#define UVD_RB_RPTR2__RB_RPTR__SHIFT                                                                          0x4
#define UVD_RB_RPTR2__RB_RPTR_MASK                                                                            0x007FFFF0L
//UVD_RB_WPTR2
#define UVD_RB_WPTR2__RB_WPTR__SHIFT                                                                          0x4
#define UVD_RB_WPTR2__RB_WPTR_MASK                                                                            0x007FFFF0L
//UVD_RB_BASE_LO
#define UVD_RB_BASE_LO__RB_BASE_LO__SHIFT                                                                     0x6
#define UVD_RB_BASE_LO__RB_BASE_LO_MASK                                                                       0xFFFFFFC0L
//UVD_RB_BASE_HI
#define UVD_RB_BASE_HI__RB_BASE_HI__SHIFT                                                                     0x0
#define UVD_RB_BASE_HI__RB_BASE_HI_MASK                                                                       0xFFFFFFFFL
//UVD_RB_SIZE
#define UVD_RB_SIZE__RB_SIZE__SHIFT                                                                           0x4
#define UVD_RB_SIZE__RB_SIZE_MASK                                                                             0x007FFFF0L
//UVD_RB_RPTR
#define UVD_RB_RPTR__RB_RPTR__SHIFT                                                                           0x4
#define UVD_RB_RPTR__RB_RPTR_MASK                                                                             0x007FFFF0L
//UVD_RB_WPTR
#define UVD_RB_WPTR__RB_WPTR__SHIFT                                                                           0x4
#define UVD_RB_WPTR__RB_WPTR_MASK                                                                             0x007FFFF0L
//UVD_RB_WPTR4
#define UVD_RB_WPTR4__RB_WPTR__SHIFT                                                                          0x4
#define UVD_RB_WPTR4__RB_WPTR_MASK                                                                            0x007FFFF0L
//UVD_JRBC_RB_RPTR
#define UVD_JRBC_RB_RPTR__RB_RPTR__SHIFT                                                                      0x4
#define UVD_JRBC_RB_RPTR__RB_RPTR_MASK                                                                        0x007FFFF0L
//UVD_LMI_VCPU_CACHE_64BIT_BAR_HIGH
#define UVD_LMI_VCPU_CACHE_64BIT_BAR_HIGH__BITS_63_32__SHIFT                                                  0x0
#define UVD_LMI_VCPU_CACHE_64BIT_BAR_HIGH__BITS_63_32_MASK                                                    0xFFFFFFFFL
//UVD_LMI_VCPU_CACHE_64BIT_BAR_LOW
#define UVD_LMI_VCPU_CACHE_64BIT_BAR_LOW__BITS_31_0__SHIFT                                                    0x0
#define UVD_LMI_VCPU_CACHE_64BIT_BAR_LOW__BITS_31_0_MASK                                                      0xFFFFFFFFL
//UVD_LMI_VCPU_NC1_64BIT_BAR_HIGH
#define UVD_LMI_VCPU_NC1_64BIT_BAR_HIGH__BITS_63_32__SHIFT                                                    0x0
#define UVD_LMI_VCPU_NC1_64BIT_BAR_HIGH__BITS_63_32_MASK                                                      0xFFFFFFFFL
//UVD_LMI_VCPU_NC1_64BIT_BAR_LOW
#define UVD_LMI_VCPU_NC1_64BIT_BAR_LOW__BITS_31_0__SHIFT                                                      0x0
#define UVD_LMI_VCPU_NC1_64BIT_BAR_LOW__BITS_31_0_MASK                                                        0xFFFFFFFFL
//UVD_LMI_VCPU_NC0_64BIT_BAR_HIGH
#define UVD_LMI_VCPU_NC0_64BIT_BAR_HIGH__BITS_63_32__SHIFT                                                    0x0
#define UVD_LMI_VCPU_NC0_64BIT_BAR_HIGH__BITS_63_32_MASK                                                      0xFFFFFFFFL
//UVD_LMI_VCPU_NC0_64BIT_BAR_LOW
#define UVD_LMI_VCPU_NC0_64BIT_BAR_LOW__BITS_31_0__SHIFT                                                      0x0
#define UVD_LMI_VCPU_NC0_64BIT_BAR_LOW__BITS_31_0_MASK                                                        0xFFFFFFFFL
//UVD_LMI_LBSI_64BIT_BAR_HIGH
#define UVD_LMI_LBSI_64BIT_BAR_HIGH__BITS_63_32__SHIFT                                                        0x0
#define UVD_LMI_LBSI_64BIT_BAR_HIGH__BITS_63_32_MASK                                                          0xFFFFFFFFL
//UVD_LMI_LBSI_64BIT_BAR_LOW
#define UVD_LMI_LBSI_64BIT_BAR_LOW__BITS_31_0__SHIFT                                                          0x0
#define UVD_LMI_LBSI_64BIT_BAR_LOW__BITS_31_0_MASK                                                            0xFFFFFFFFL
//UVD_LMI_RBC_IB_64BIT_BAR_HIGH
#define UVD_LMI_RBC_IB_64BIT_BAR_HIGH__BITS_63_32__SHIFT                                                      0x0
#define UVD_LMI_RBC_IB_64BIT_BAR_HIGH__BITS_63_32_MASK                                                        0xFFFFFFFFL
//UVD_LMI_RBC_IB_64BIT_BAR_LOW
#define UVD_LMI_RBC_IB_64BIT_BAR_LOW__BITS_31_0__SHIFT                                                        0x0
#define UVD_LMI_RBC_IB_64BIT_BAR_LOW__BITS_31_0_MASK                                                          0xFFFFFFFFL
//UVD_LMI_RBC_RB_64BIT_BAR_HIGH
#define UVD_LMI_RBC_RB_64BIT_BAR_HIGH__BITS_63_32__SHIFT                                                      0x0
#define UVD_LMI_RBC_RB_64BIT_BAR_HIGH__BITS_63_32_MASK                                                        0xFFFFFFFFL
//UVD_LMI_RBC_RB_64BIT_BAR_LOW
#define UVD_LMI_RBC_RB_64BIT_BAR_LOW__BITS_31_0__SHIFT                                                        0x0
#define UVD_LMI_RBC_RB_64BIT_BAR_LOW__BITS_31_0_MASK                                                          0xFFFFFFFFL


// addressBlock: uvd_uvddec
//UVD_SEMA_CNTL
#define UVD_SEMA_CNTL__SEMAPHORE_EN__SHIFT                                                                    0x0
#define UVD_SEMA_CNTL__ADVANCED_MODE_DIS__SHIFT                                                               0x1
#define UVD_SEMA_CNTL__SEMAPHORE_EN_MASK                                                                      0x00000001L
#define UVD_SEMA_CNTL__ADVANCED_MODE_DIS_MASK                                                                 0x00000002L
//UVD_LMI_JRBC_RB_64BIT_BAR_LOW
#define UVD_LMI_JRBC_RB_64BIT_BAR_LOW__BITS_31_0__SHIFT                                                       0x0
#define UVD_LMI_JRBC_RB_64BIT_BAR_LOW__BITS_31_0_MASK                                                         0xFFFFFFFFL
//UVD_LMI_JRBC_RB_64BIT_BAR_HIGH
#define UVD_LMI_JRBC_RB_64BIT_BAR_HIGH__BITS_63_32__SHIFT                                                     0x0
#define UVD_LMI_JRBC_RB_64BIT_BAR_HIGH__BITS_63_32_MASK                                                       0xFFFFFFFFL
//UVD_LMI_JRBC_IB_64BIT_BAR_LOW
#define UVD_LMI_JRBC_IB_64BIT_BAR_LOW__BITS_31_0__SHIFT                                                       0x0
#define UVD_LMI_JRBC_IB_64BIT_BAR_LOW__BITS_31_0_MASK                                                         0xFFFFFFFFL
//UVD_LMI_JRBC_IB_64BIT_BAR_HIGH
#define UVD_LMI_JRBC_IB_64BIT_BAR_HIGH__BITS_63_32__SHIFT                                                     0x0
#define UVD_LMI_JRBC_IB_64BIT_BAR_HIGH__BITS_63_32_MASK                                                       0xFFFFFFFFL
//UVD_LMI_JRBC_IB_VMID
#define UVD_LMI_JRBC_IB_VMID__IB_WR_VMID__SHIFT                                                               0x0
#define UVD_LMI_JRBC_IB_VMID__IB_RD_VMID__SHIFT                                                               0x4
#define UVD_LMI_JRBC_IB_VMID__IB_WR_VMID_MASK                                                                 0x0000000FL
#define UVD_LMI_JRBC_IB_VMID__IB_RD_VMID_MASK                                                                 0x000000F0L
//UVD_JRBC_RB_WPTR
#define UVD_JRBC_RB_WPTR__RB_WPTR__SHIFT                                                                      0x4
#define UVD_JRBC_RB_WPTR__RB_WPTR_MASK                                                                        0x007FFFF0L
//UVD_JRBC_RB_CNTL
#define UVD_JRBC_RB_CNTL__RB_NO_FETCH__SHIFT                                                                  0x0
#define UVD_JRBC_RB_CNTL__RB_RPTR_WR_EN__SHIFT                                                                0x1
#define UVD_JRBC_RB_CNTL__RB_PRE_WRITE_TIMER__SHIFT                                                           0x4
#define UVD_JRBC_RB_CNTL__RB_NO_FETCH_MASK                                                                    0x00000001L
#define UVD_JRBC_RB_CNTL__RB_RPTR_WR_EN_MASK                                                                  0x00000002L
#define UVD_JRBC_RB_CNTL__RB_PRE_WRITE_TIMER_MASK                                                             0x0007FFF0L
//UVD_JRBC_IB_SIZE
#define UVD_JRBC_IB_SIZE__IB_SIZE__SHIFT                                                                      0x4
#define UVD_JRBC_IB_SIZE__IB_SIZE_MASK                                                                        0x007FFFF0L
//UVD_JRBC_LMI_SWAP_CNTL
#define UVD_JRBC_LMI_SWAP_CNTL__RB_MC_SWAP__SHIFT                                                             0x0
#define UVD_JRBC_LMI_SWAP_CNTL__IB_MC_SWAP__SHIFT                                                             0x2
#define UVD_JRBC_LMI_SWAP_CNTL__RB_MC_SWAP_MASK                                                               0x00000003L
#define UVD_JRBC_LMI_SWAP_CNTL__IB_MC_SWAP_MASK                                                               0x0000000CL
//UVD_JRBC_SOFT_RESET
#define UVD_JRBC_SOFT_RESET__RESET__SHIFT                                                                     0x0
#define UVD_JRBC_SOFT_RESET__VCLK_RESET_STATUS__SHIFT                                                         0x10
#define UVD_JRBC_SOFT_RESET__SCLK_RESET_STATUS__SHIFT                                                         0x11
#define UVD_JRBC_SOFT_RESET__RESET_MASK                                                                       0x00000001L
#define UVD_JRBC_SOFT_RESET__VCLK_RESET_STATUS_MASK                                                           0x00010000L
#define UVD_JRBC_SOFT_RESET__SCLK_RESET_STATUS_MASK                                                           0x00020000L
//UVD_JRBC_STATUS
#define UVD_JRBC_STATUS__RB_JOB_DONE__SHIFT                                                                   0x0
#define UVD_JRBC_STATUS__IB_JOB_DONE__SHIFT                                                                   0x1
#define UVD_JRBC_STATUS__RB_ILLEGAL_CMD__SHIFT                                                                0x2
#define UVD_JRBC_STATUS__RB_COND_REG_RD_TIMEOUT__SHIFT                                                        0x3
#define UVD_JRBC_STATUS__RB_MEM_WR_TIMEOUT__SHIFT                                                             0x4
#define UVD_JRBC_STATUS__RB_MEM_RD_TIMEOUT__SHIFT                                                             0x5
#define UVD_JRBC_STATUS__IB_ILLEGAL_CMD__SHIFT                                                                0x6
#define UVD_JRBC_STATUS__IB_COND_REG_RD_TIMEOUT__SHIFT                                                        0x7
#define UVD_JRBC_STATUS__IB_MEM_WR_TIMEOUT__SHIFT                                                             0x8
#define UVD_JRBC_STATUS__IB_MEM_RD_TIMEOUT__SHIFT                                                             0x9
#define UVD_JRBC_STATUS__RB_TRAP_STATUS__SHIFT                                                                0xa
#define UVD_JRBC_STATUS__PREEMPT_STATUS__SHIFT                                                                0xb
#define UVD_JRBC_STATUS__IB_TRAP_STATUS__SHIFT                                                                0xc
#define UVD_JRBC_STATUS__INT_EN__SHIFT                                                                        0x10
#define UVD_JRBC_STATUS__INT_ACK__SHIFT                                                                       0x11
#define UVD_JRBC_STATUS__RB_JOB_DONE_MASK                                                                     0x00000001L
#define UVD_JRBC_STATUS__IB_JOB_DONE_MASK                                                                     0x00000002L
#define UVD_JRBC_STATUS__RB_ILLEGAL_CMD_MASK                                                                  0x00000004L
#define UVD_JRBC_STATUS__RB_COND_REG_RD_TIMEOUT_MASK                                                          0x00000008L
#define UVD_JRBC_STATUS__RB_MEM_WR_TIMEOUT_MASK                                                               0x00000010L
#define UVD_JRBC_STATUS__RB_MEM_RD_TIMEOUT_MASK                                                               0x00000020L
#define UVD_JRBC_STATUS__IB_ILLEGAL_CMD_MASK                                                                  0x00000040L
#define UVD_JRBC_STATUS__IB_COND_REG_RD_TIMEOUT_MASK                                                          0x00000080L
#define UVD_JRBC_STATUS__IB_MEM_WR_TIMEOUT_MASK                                                               0x00000100L
#define UVD_JRBC_STATUS__IB_MEM_RD_TIMEOUT_MASK                                                               0x00000200L
#define UVD_JRBC_STATUS__RB_TRAP_STATUS_MASK                                                                  0x00000400L
#define UVD_JRBC_STATUS__PREEMPT_STATUS_MASK                                                                  0x00000800L
#define UVD_JRBC_STATUS__IB_TRAP_STATUS_MASK                                                                  0x00001000L
#define UVD_JRBC_STATUS__INT_EN_MASK                                                                          0x00010000L
#define UVD_JRBC_STATUS__INT_ACK_MASK                                                                         0x00020000L
//UVD_RB_RPTR3
#define UVD_RB_RPTR3__RB_RPTR__SHIFT                                                                          0x4
#define UVD_RB_RPTR3__RB_RPTR_MASK                                                                            0x007FFFF0L
//UVD_RB_WPTR3
#define UVD_RB_WPTR3__RB_WPTR__SHIFT                                                                          0x4
#define UVD_RB_WPTR3__RB_WPTR_MASK                                                                            0x007FFFF0L
//UVD_RB_BASE_LO3
#define UVD_RB_BASE_LO3__RB_BASE_LO__SHIFT                                                                    0x6
#define UVD_RB_BASE_LO3__RB_BASE_LO_MASK                                                                      0xFFFFFFC0L
//UVD_RB_BASE_HI3
#define UVD_RB_BASE_HI3__RB_BASE_HI__SHIFT                                                                    0x0
#define UVD_RB_BASE_HI3__RB_BASE_HI_MASK                                                                      0xFFFFFFFFL
//UVD_RB_SIZE3
#define UVD_RB_SIZE3__RB_SIZE__SHIFT                                                                          0x4
#define UVD_RB_SIZE3__RB_SIZE_MASK                                                                            0x007FFFF0L
//JPEG_CGC_GATE
#define JPEG_CGC_GATE__JPEG__SHIFT                                                                            0x14
#define JPEG_CGC_GATE__JPEG2__SHIFT                                                                           0x15
#define JPEG_CGC_GATE__JPEG_MASK                                                                              0x00100000L
#define JPEG_CGC_GATE__JPEG2_MASK                                                                             0x00200000L
//UVD_CTX_INDEX
#define UVD_CTX_INDEX__INDEX__SHIFT                                                                           0x0
#define UVD_CTX_INDEX__INDEX_MASK                                                                             0x000001FFL
//UVD_CTX_DATA
#define UVD_CTX_DATA__DATA__SHIFT                                                                             0x0
#define UVD_CTX_DATA__DATA_MASK                                                                               0xFFFFFFFFL
//UVD_CGC_GATE
#define UVD_CGC_GATE__SYS__SHIFT                                                                              0x0
#define UVD_CGC_GATE__UDEC__SHIFT                                                                             0x1
#define UVD_CGC_GATE__MPEG2__SHIFT                                                                            0x2
#define UVD_CGC_GATE__REGS__SHIFT                                                                             0x3
#define UVD_CGC_GATE__RBC__SHIFT                                                                              0x4
#define UVD_CGC_GATE__LMI_MC__SHIFT                                                                           0x5
#define UVD_CGC_GATE__LMI_UMC__SHIFT                                                                          0x6
#define UVD_CGC_GATE__IDCT__SHIFT                                                                             0x7
#define UVD_CGC_GATE__MPRD__SHIFT                                                                             0x8
#define UVD_CGC_GATE__MPC__SHIFT                                                                              0x9
#define UVD_CGC_GATE__LBSI__SHIFT                                                                             0xa
#define UVD_CGC_GATE__LRBBM__SHIFT                                                                            0xb
#define UVD_CGC_GATE__UDEC_RE__SHIFT                                                                          0xc
#define UVD_CGC_GATE__UDEC_CM__SHIFT                                                                          0xd
#define UVD_CGC_GATE__UDEC_IT__SHIFT                                                                          0xe
#define UVD_CGC_GATE__UDEC_DB__SHIFT                                                                          0xf
#define UVD_CGC_GATE__UDEC_MP__SHIFT                                                                          0x10
#define UVD_CGC_GATE__WCB__SHIFT                                                                              0x11
#define UVD_CGC_GATE__VCPU__SHIFT                                                                             0x12
#define UVD_CGC_GATE__SCPU__SHIFT                                                                             0x13
#define UVD_CGC_GATE__SYS_MASK                                                                                0x00000001L
#define UVD_CGC_GATE__UDEC_MASK                                                                               0x00000002L
#define UVD_CGC_GATE__MPEG2_MASK                                                                              0x00000004L
#define UVD_CGC_GATE__REGS_MASK                                                                               0x00000008L
#define UVD_CGC_GATE__RBC_MASK                                                                                0x00000010L
#define UVD_CGC_GATE__LMI_MC_MASK                                                                             0x00000020L
#define UVD_CGC_GATE__LMI_UMC_MASK                                                                            0x00000040L
#define UVD_CGC_GATE__IDCT_MASK                                                                               0x00000080L
#define UVD_CGC_GATE__MPRD_MASK                                                                               0x00000100L
#define UVD_CGC_GATE__MPC_MASK                                                                                0x00000200L
#define UVD_CGC_GATE__LBSI_MASK                                                                               0x00000400L
#define UVD_CGC_GATE__LRBBM_MASK                                                                              0x00000800L
#define UVD_CGC_GATE__UDEC_RE_MASK                                                                            0x00001000L
#define UVD_CGC_GATE__UDEC_CM_MASK                                                                            0x00002000L
#define UVD_CGC_GATE__UDEC_IT_MASK                                                                            0x00004000L
#define UVD_CGC_GATE__UDEC_DB_MASK                                                                            0x00008000L
#define UVD_CGC_GATE__UDEC_MP_MASK                                                                            0x00010000L
#define UVD_CGC_GATE__WCB_MASK                                                                                0x00020000L
#define UVD_CGC_GATE__VCPU_MASK                                                                               0x00040000L
#define UVD_CGC_GATE__SCPU_MASK                                                                               0x00080000L
//UVD_CGC_STATUS
#define UVD_CGC_STATUS__SYS_SCLK__SHIFT                                                                       0x0
#define UVD_CGC_STATUS__SYS_DCLK__SHIFT                                                                       0x1
#define UVD_CGC_STATUS__SYS_VCLK__SHIFT                                                                       0x2
#define UVD_CGC_STATUS__UDEC_SCLK__SHIFT                                                                      0x3
#define UVD_CGC_STATUS__UDEC_DCLK__SHIFT                                                                      0x4
#define UVD_CGC_STATUS__UDEC_VCLK__SHIFT                                                                      0x5
#define UVD_CGC_STATUS__MPEG2_SCLK__SHIFT                                                                     0x6
#define UVD_CGC_STATUS__MPEG2_DCLK__SHIFT                                                                     0x7
#define UVD_CGC_STATUS__MPEG2_VCLK__SHIFT                                                                     0x8
#define UVD_CGC_STATUS__REGS_SCLK__SHIFT                                                                      0x9
#define UVD_CGC_STATUS__REGS_VCLK__SHIFT                                                                      0xa
#define UVD_CGC_STATUS__RBC_SCLK__SHIFT                                                                       0xb
#define UVD_CGC_STATUS__LMI_MC_SCLK__SHIFT                                                                    0xc
#define UVD_CGC_STATUS__LMI_UMC_SCLK__SHIFT                                                                   0xd
#define UVD_CGC_STATUS__IDCT_SCLK__SHIFT                                                                      0xe
#define UVD_CGC_STATUS__IDCT_VCLK__SHIFT                                                                      0xf
#define UVD_CGC_STATUS__MPRD_SCLK__SHIFT                                                                      0x10
#define UVD_CGC_STATUS__MPRD_DCLK__SHIFT                                                                      0x11
#define UVD_CGC_STATUS__MPRD_VCLK__SHIFT                                                                      0x12
#define UVD_CGC_STATUS__MPC_SCLK__SHIFT                                                                       0x13
#define UVD_CGC_STATUS__MPC_DCLK__SHIFT                                                                       0x14
#define UVD_CGC_STATUS__LBSI_SCLK__SHIFT                                                                      0x15
#define UVD_CGC_STATUS__LBSI_VCLK__SHIFT                                                                      0x16
#define UVD_CGC_STATUS__LRBBM_SCLK__SHIFT                                                                     0x17
#define UVD_CGC_STATUS__WCB_SCLK__SHIFT                                                                       0x18
#define UVD_CGC_STATUS__VCPU_SCLK__SHIFT                                                                      0x19
#define UVD_CGC_STATUS__VCPU_VCLK__SHIFT                                                                      0x1a
#define UVD_CGC_STATUS__SCPU_SCLK__SHIFT                                                                      0x1b
#define UVD_CGC_STATUS__SCPU_VCLK__SHIFT                                                                      0x1c
#define UVD_CGC_STATUS__ALL_ENC_ACTIVE__SHIFT                                                                 0x1d
#define UVD_CGC_STATUS__JPEG_ACTIVE__SHIFT                                                                    0x1e
#define UVD_CGC_STATUS__ALL_DEC_ACTIVE__SHIFT                                                                 0x1f
#define UVD_CGC_STATUS__SYS_SCLK_MASK                                                                         0x00000001L
#define UVD_CGC_STATUS__SYS_DCLK_MASK                                                                         0x00000002L
#define UVD_CGC_STATUS__SYS_VCLK_MASK                                                                         0x00000004L
#define UVD_CGC_STATUS__UDEC_SCLK_MASK                                                                        0x00000008L
#define UVD_CGC_STATUS__UDEC_DCLK_MASK                                                                        0x00000010L
#define UVD_CGC_STATUS__UDEC_VCLK_MASK                                                                        0x00000020L
#define UVD_CGC_STATUS__MPEG2_SCLK_MASK                                                                       0x00000040L
#define UVD_CGC_STATUS__MPEG2_DCLK_MASK                                                                       0x00000080L
#define UVD_CGC_STATUS__MPEG2_VCLK_MASK                                                                       0x00000100L
#define UVD_CGC_STATUS__REGS_SCLK_MASK                                                                        0x00000200L
#define UVD_CGC_STATUS__REGS_VCLK_MASK                                                                        0x00000400L
#define UVD_CGC_STATUS__RBC_SCLK_MASK                                                                         0x00000800L
#define UVD_CGC_STATUS__LMI_MC_SCLK_MASK                                                                      0x00001000L
#define UVD_CGC_STATUS__LMI_UMC_SCLK_MASK                                                                     0x00002000L
#define UVD_CGC_STATUS__IDCT_SCLK_MASK                                                                        0x00004000L
#define UVD_CGC_STATUS__IDCT_VCLK_MASK                                                                        0x00008000L
#define UVD_CGC_STATUS__MPRD_SCLK_MASK                                                                        0x00010000L
#define UVD_CGC_STATUS__MPRD_DCLK_MASK                                                                        0x00020000L
#define UVD_CGC_STATUS__MPRD_VCLK_MASK                                                                        0x00040000L
#define UVD_CGC_STATUS__MPC_SCLK_MASK                                                                         0x00080000L
#define UVD_CGC_STATUS__MPC_DCLK_MASK                                                                         0x00100000L
#define UVD_CGC_STATUS__LBSI_SCLK_MASK                                                                        0x00200000L
#define UVD_CGC_STATUS__LBSI_VCLK_MASK                                                                        0x00400000L
#define UVD_CGC_STATUS__LRBBM_SCLK_MASK                                                                       0x00800000L
#define UVD_CGC_STATUS__WCB_SCLK_MASK                                                                         0x01000000L
#define UVD_CGC_STATUS__VCPU_SCLK_MASK                                                                        0x02000000L
#define UVD_CGC_STATUS__VCPU_VCLK_MASK                                                                        0x04000000L
#define UVD_CGC_STATUS__SCPU_SCLK_MASK                                                                        0x08000000L
#define UVD_CGC_STATUS__SCPU_VCLK_MASK                                                                        0x10000000L
#define UVD_CGC_STATUS__ALL_ENC_ACTIVE_MASK                                                                   0x20000000L
#define UVD_CGC_STATUS__JPEG_ACTIVE_MASK                                                                      0x40000000L
#define UVD_CGC_STATUS__ALL_DEC_ACTIVE_MASK                                                                   0x80000000L
//UVD_CGC_CTRL
#define UVD_CGC_CTRL__DYN_CLOCK_MODE__SHIFT                                                                   0x0
#define UVD_CGC_CTRL__CLK_GATE_DLY_TIMER__SHIFT                                                               0x2
#define UVD_CGC_CTRL__CLK_OFF_DELAY__SHIFT                                                                    0x6
#define UVD_CGC_CTRL__UDEC_RE_MODE__SHIFT                                                                     0xb
#define UVD_CGC_CTRL__UDEC_CM_MODE__SHIFT                                                                     0xc
#define UVD_CGC_CTRL__UDEC_IT_MODE__SHIFT                                                                     0xd
#define UVD_CGC_CTRL__UDEC_DB_MODE__SHIFT                                                                     0xe
#define UVD_CGC_CTRL__UDEC_MP_MODE__SHIFT                                                                     0xf
#define UVD_CGC_CTRL__SYS_MODE__SHIFT                                                                         0x10
#define UVD_CGC_CTRL__UDEC_MODE__SHIFT                                                                        0x11
#define UVD_CGC_CTRL__MPEG2_MODE__SHIFT                                                                       0x12
#define UVD_CGC_CTRL__REGS_MODE__SHIFT                                                                        0x13
#define UVD_CGC_CTRL__RBC_MODE__SHIFT                                                                         0x14
#define UVD_CGC_CTRL__LMI_MC_MODE__SHIFT                                                                      0x15
#define UVD_CGC_CTRL__LMI_UMC_MODE__SHIFT                                                                     0x16
#define UVD_CGC_CTRL__IDCT_MODE__SHIFT                                                                        0x17
#define UVD_CGC_CTRL__MPRD_MODE__SHIFT                                                                        0x18
#define UVD_CGC_CTRL__MPC_MODE__SHIFT                                                                         0x19
#define UVD_CGC_CTRL__LBSI_MODE__SHIFT                                                                        0x1a
#define UVD_CGC_CTRL__LRBBM_MODE__SHIFT                                                                       0x1b
#define UVD_CGC_CTRL__WCB_MODE__SHIFT                                                                         0x1c
#define UVD_CGC_CTRL__VCPU_MODE__SHIFT                                                                        0x1d
#define UVD_CGC_CTRL__SCPU_MODE__SHIFT                                                                        0x1e
#define UVD_CGC_CTRL__DYN_CLOCK_MODE_MASK                                                                     0x00000001L
#define UVD_CGC_CTRL__CLK_GATE_DLY_TIMER_MASK                                                                 0x0000003CL
#define UVD_CGC_CTRL__CLK_OFF_DELAY_MASK                                                                      0x000007C0L
#define UVD_CGC_CTRL__UDEC_RE_MODE_MASK                                                                       0x00000800L
#define UVD_CGC_CTRL__UDEC_CM_MODE_MASK                                                                       0x00001000L
#define UVD_CGC_CTRL__UDEC_IT_MODE_MASK                                                                       0x00002000L
#define UVD_CGC_CTRL__UDEC_DB_MODE_MASK                                                                       0x00004000L
#define UVD_CGC_CTRL__UDEC_MP_MODE_MASK                                                                       0x00008000L
#define UVD_CGC_CTRL__SYS_MODE_MASK                                                                           0x00010000L
#define UVD_CGC_CTRL__UDEC_MODE_MASK                                                                          0x00020000L
#define UVD_CGC_CTRL__MPEG2_MODE_MASK                                                                         0x00040000L
#define UVD_CGC_CTRL__REGS_MODE_MASK                                                                          0x00080000L
#define UVD_CGC_CTRL__RBC_MODE_MASK                                                                           0x00100000L
#define UVD_CGC_CTRL__LMI_MC_MODE_MASK                                                                        0x00200000L
#define UVD_CGC_CTRL__LMI_UMC_MODE_MASK                                                                       0x00400000L
#define UVD_CGC_CTRL__IDCT_MODE_MASK                                                                          0x00800000L
#define UVD_CGC_CTRL__MPRD_MODE_MASK                                                                          0x01000000L
#define UVD_CGC_CTRL__MPC_MODE_MASK                                                                           0x02000000L
#define UVD_CGC_CTRL__LBSI_MODE_MASK                                                                          0x04000000L
#define UVD_CGC_CTRL__LRBBM_MODE_MASK                                                                         0x08000000L
#define UVD_CGC_CTRL__WCB_MODE_MASK                                                                           0x10000000L
#define UVD_CGC_CTRL__VCPU_MODE_MASK                                                                          0x20000000L
#define UVD_CGC_CTRL__SCPU_MODE_MASK                                                                          0x40000000L
//UVD_GP_SCRATCH0
#define UVD_GP_SCRATCH0__DATA__SHIFT                                                                          0x0
#define UVD_GP_SCRATCH0__DATA_MASK                                                                            0xFFFFFFFFL
//UVD_GP_SCRATCH1
#define UVD_GP_SCRATCH1__DATA__SHIFT                                                                          0x0
#define UVD_GP_SCRATCH1__DATA_MASK                                                                            0xFFFFFFFFL
//UVD_GP_SCRATCH2
#define UVD_GP_SCRATCH2__DATA__SHIFT                                                                          0x0
#define UVD_GP_SCRATCH2__DATA_MASK                                                                            0xFFFFFFFFL
//UVD_GP_SCRATCH3
#define UVD_GP_SCRATCH3__DATA__SHIFT                                                                          0x0
#define UVD_GP_SCRATCH3__DATA_MASK                                                                            0xFFFFFFFFL
//UVD_GP_SCRATCH4
#define UVD_GP_SCRATCH4__DATA__SHIFT                                                                          0x0
#define UVD_GP_SCRATCH4__DATA_MASK                                                                            0xFFFFFFFFL
//UVD_GP_SCRATCH5
#define UVD_GP_SCRATCH5__DATA__SHIFT                                                                          0x0
#define UVD_GP_SCRATCH5__DATA_MASK                                                                            0xFFFFFFFFL
//UVD_GP_SCRATCH6
#define UVD_GP_SCRATCH6__DATA__SHIFT                                                                          0x0
#define UVD_GP_SCRATCH6__DATA_MASK                                                                            0xFFFFFFFFL
//UVD_GP_SCRATCH7
#define UVD_GP_SCRATCH7__DATA__SHIFT                                                                          0x0
#define UVD_GP_SCRATCH7__DATA_MASK                                                                            0xFFFFFFFFL
//UVD_LMI_VCPU_CACHE_VMID
#define UVD_LMI_VCPU_CACHE_VMID__VCPU_CACHE_VMID__SHIFT                                                       0x0
#define UVD_LMI_VCPU_CACHE_VMID__VCPU_CACHE_VMID_MASK                                                         0x0000000FL
//UVD_LMI_CTRL2
#define UVD_LMI_CTRL2__SPH_DIS__SHIFT                                                                         0x0
#define UVD_LMI_CTRL2__STALL_ARB__SHIFT                                                                       0x1
#define UVD_LMI_CTRL2__ASSERT_UMC_URGENT__SHIFT                                                               0x2
#define UVD_LMI_CTRL2__MASK_UMC_URGENT__SHIFT                                                                 0x3
#define UVD_LMI_CTRL2__DRCITF_BUBBLE_FIX_DIS__SHIFT                                                           0x7
#define UVD_LMI_CTRL2__STALL_ARB_UMC__SHIFT                                                                   0x8
#define UVD_LMI_CTRL2__MC_READ_ID_SEL__SHIFT                                                                  0x9
#define UVD_LMI_CTRL2__MC_WRITE_ID_SEL__SHIFT                                                                 0xb
#define UVD_LMI_CTRL2__SPH_DIS_MASK                                                                           0x00000001L
#define UVD_LMI_CTRL2__STALL_ARB_MASK                                                                         0x00000002L
#define UVD_LMI_CTRL2__ASSERT_UMC_URGENT_MASK                                                                 0x00000004L
#define UVD_LMI_CTRL2__MASK_UMC_URGENT_MASK                                                                   0x00000008L
#define UVD_LMI_CTRL2__DRCITF_BUBBLE_FIX_DIS_MASK                                                             0x00000080L
#define UVD_LMI_CTRL2__STALL_ARB_UMC_MASK                                                                     0x00000100L
#define UVD_LMI_CTRL2__MC_READ_ID_SEL_MASK                                                                    0x00000600L
#define UVD_LMI_CTRL2__MC_WRITE_ID_SEL_MASK                                                                   0x00001800L
//UVD_MASTINT_EN
#define UVD_MASTINT_EN__OVERRUN_RST__SHIFT                                                                    0x0
#define UVD_MASTINT_EN__VCPU_EN__SHIFT                                                                        0x1
#define UVD_MASTINT_EN__SYS_EN__SHIFT                                                                         0x2
#define UVD_MASTINT_EN__INT_OVERRUN__SHIFT                                                                    0x4
#define UVD_MASTINT_EN__OVERRUN_RST_MASK                                                                      0x00000001L
#define UVD_MASTINT_EN__VCPU_EN_MASK                                                                          0x00000002L
#define UVD_MASTINT_EN__SYS_EN_MASK                                                                           0x00000004L
#define UVD_MASTINT_EN__INT_OVERRUN_MASK                                                                      0x007FFFF0L
//JPEG_CGC_CTRL
#define JPEG_CGC_CTRL__DYN_CLOCK_MODE__SHIFT                                                                  0x0
#define JPEG_CGC_CTRL__JPEG2_MODE__SHIFT                                                                      0x1
#define JPEG_CGC_CTRL__CLK_GATE_DLY_TIMER__SHIFT                                                              0x2
#define JPEG_CGC_CTRL__CLK_OFF_DELAY__SHIFT                                                                   0x6
#define JPEG_CGC_CTRL__JPEG_MODE__SHIFT                                                                       0x1f
#define JPEG_CGC_CTRL__DYN_CLOCK_MODE_MASK                                                                    0x00000001L
#define JPEG_CGC_CTRL__JPEG2_MODE_MASK                                                                        0x00000002L
#define JPEG_CGC_CTRL__CLK_GATE_DLY_TIMER_MASK                                                                0x0000003CL
#define JPEG_CGC_CTRL__CLK_OFF_DELAY_MASK                                                                     0x000007C0L
#define JPEG_CGC_CTRL__JPEG_MODE_MASK                                                                         0x80000000L
//UVD_LMI_CTRL
#define UVD_LMI_CTRL__WRITE_CLEAN_TIMER__SHIFT                                                                0x0
#define UVD_LMI_CTRL__WRITE_CLEAN_TIMER_EN__SHIFT                                                             0x8
#define UVD_LMI_CTRL__REQ_MODE__SHIFT                                                                         0x9
#define UVD_LMI_CTRL__ASSERT_MC_URGENT__SHIFT                                                                 0xb
#define UVD_LMI_CTRL__MASK_MC_URGENT__SHIFT                                                                   0xc
#define UVD_LMI_CTRL__DATA_COHERENCY_EN__SHIFT                                                                0xd
#define UVD_LMI_CTRL__CRC_RESET__SHIFT                                                                        0xe
#define UVD_LMI_CTRL__CRC_SEL__SHIFT                                                                          0xf
#define UVD_LMI_CTRL__VCPU_DATA_COHERENCY_EN__SHIFT                                                           0x15
#define UVD_LMI_CTRL__CM_DATA_COHERENCY_EN__SHIFT                                                             0x16
#define UVD_LMI_CTRL__DB_DB_DATA_COHERENCY_EN__SHIFT                                                          0x17
#define UVD_LMI_CTRL__DB_IT_DATA_COHERENCY_EN__SHIFT                                                          0x18
#define UVD_LMI_CTRL__IT_IT_DATA_COHERENCY_EN__SHIFT                                                          0x19
#define UVD_LMI_CTRL__RFU__SHIFT                                                                              0x1b
#define UVD_LMI_CTRL__WRITE_CLEAN_TIMER_MASK                                                                  0x000000FFL
#define UVD_LMI_CTRL__WRITE_CLEAN_TIMER_EN_MASK                                                               0x00000100L
#define UVD_LMI_CTRL__REQ_MODE_MASK                                                                           0x00000200L
#define UVD_LMI_CTRL__ASSERT_MC_URGENT_MASK                                                                   0x00000800L
#define UVD_LMI_CTRL__MASK_MC_URGENT_MASK                                                                     0x00001000L
#define UVD_LMI_CTRL__DATA_COHERENCY_EN_MASK                                                                  0x00002000L
#define UVD_LMI_CTRL__CRC_RESET_MASK                                                                          0x00004000L
#define UVD_LMI_CTRL__CRC_SEL_MASK                                                                            0x000F8000L
#define UVD_LMI_CTRL__VCPU_DATA_COHERENCY_EN_MASK                                                             0x00200000L
#define UVD_LMI_CTRL__CM_DATA_COHERENCY_EN_MASK                                                               0x00400000L
#define UVD_LMI_CTRL__DB_DB_DATA_COHERENCY_EN_MASK                                                            0x00800000L
#define UVD_LMI_CTRL__DB_IT_DATA_COHERENCY_EN_MASK                                                            0x01000000L
#define UVD_LMI_CTRL__IT_IT_DATA_COHERENCY_EN_MASK                                                            0x02000000L
#define UVD_LMI_CTRL__RFU_MASK                                                                                0xF8000000L
//UVD_LMI_SWAP_CNTL
#define UVD_LMI_SWAP_CNTL__RB_MC_SWAP__SHIFT                                                                  0x0
#define UVD_LMI_SWAP_CNTL__IB_MC_SWAP__SHIFT                                                                  0x2
#define UVD_LMI_SWAP_CNTL__RB_RPTR_MC_SWAP__SHIFT                                                             0x4
#define UVD_LMI_SWAP_CNTL__VCPU_R_MC_SWAP__SHIFT                                                              0x6
#define UVD_LMI_SWAP_CNTL__VCPU_W_MC_SWAP__SHIFT                                                              0x8
#define UVD_LMI_SWAP_CNTL__CM_MC_SWAP__SHIFT                                                                  0xa
#define UVD_LMI_SWAP_CNTL__IT_MC_SWAP__SHIFT                                                                  0xc
#define UVD_LMI_SWAP_CNTL__DB_R_MC_SWAP__SHIFT                                                                0xe
#define UVD_LMI_SWAP_CNTL__DB_W_MC_SWAP__SHIFT                                                                0x10
#define UVD_LMI_SWAP_CNTL__CSM_MC_SWAP__SHIFT                                                                 0x12
#define UVD_LMI_SWAP_CNTL__ACAP_MC_SWAP__SHIFT                                                                0x14
#define UVD_LMI_SWAP_CNTL__MP_REF16_MC_SWAP__SHIFT                                                            0x16
#define UVD_LMI_SWAP_CNTL__DBW_MC_SWAP__SHIFT                                                                 0x18
#define UVD_LMI_SWAP_CNTL__RB_WR_MC_SWAP__SHIFT                                                               0x1a
#define UVD_LMI_SWAP_CNTL__RE_MC_SWAP__SHIFT                                                                  0x1c
#define UVD_LMI_SWAP_CNTL__MP_MC_SWAP__SHIFT                                                                  0x1e
#define UVD_LMI_SWAP_CNTL__RB_MC_SWAP_MASK                                                                    0x00000003L
#define UVD_LMI_SWAP_CNTL__IB_MC_SWAP_MASK                                                                    0x0000000CL
#define UVD_LMI_SWAP_CNTL__RB_RPTR_MC_SWAP_MASK                                                               0x00000030L
#define UVD_LMI_SWAP_CNTL__VCPU_R_MC_SWAP_MASK                                                                0x000000C0L
#define UVD_LMI_SWAP_CNTL__VCPU_W_MC_SWAP_MASK                                                                0x00000300L
#define UVD_LMI_SWAP_CNTL__CM_MC_SWAP_MASK                                                                    0x00000C00L
#define UVD_LMI_SWAP_CNTL__IT_MC_SWAP_MASK                                                                    0x00003000L
#define UVD_LMI_SWAP_CNTL__DB_R_MC_SWAP_MASK                                                                  0x0000C000L
#define UVD_LMI_SWAP_CNTL__DB_W_MC_SWAP_MASK                                                                  0x00030000L
#define UVD_LMI_SWAP_CNTL__CSM_MC_SWAP_MASK                                                                   0x000C0000L
#define UVD_LMI_SWAP_CNTL__ACAP_MC_SWAP_MASK                                                                  0x00300000L
#define UVD_LMI_SWAP_CNTL__MP_REF16_MC_SWAP_MASK                                                              0x00C00000L
#define UVD_LMI_SWAP_CNTL__DBW_MC_SWAP_MASK                                                                   0x03000000L
#define UVD_LMI_SWAP_CNTL__RB_WR_MC_SWAP_MASK                                                                 0x0C000000L
#define UVD_LMI_SWAP_CNTL__RE_MC_SWAP_MASK                                                                    0x30000000L
#define UVD_LMI_SWAP_CNTL__MP_MC_SWAP_MASK                                                                    0xC0000000L
//UVD_MPC_SET_MUXA0
#define UVD_MPC_SET_MUXA0__VARA_0__SHIFT                                                                      0x0
#define UVD_MPC_SET_MUXA0__VARA_1__SHIFT                                                                      0x6
#define UVD_MPC_SET_MUXA0__VARA_2__SHIFT                                                                      0xc
#define UVD_MPC_SET_MUXA0__VARA_3__SHIFT                                                                      0x12
#define UVD_MPC_SET_MUXA0__VARA_4__SHIFT                                                                      0x18
#define UVD_MPC_SET_MUXA0__VARA_0_MASK                                                                        0x0000003FL
#define UVD_MPC_SET_MUXA0__VARA_1_MASK                                                                        0x00000FC0L
#define UVD_MPC_SET_MUXA0__VARA_2_MASK                                                                        0x0003F000L
#define UVD_MPC_SET_MUXA0__VARA_3_MASK                                                                        0x00FC0000L
#define UVD_MPC_SET_MUXA0__VARA_4_MASK                                                                        0x3F000000L
//UVD_MPC_SET_MUXA1
#define UVD_MPC_SET_MUXA1__VARA_5__SHIFT                                                                      0x0
#define UVD_MPC_SET_MUXA1__VARA_6__SHIFT                                                                      0x6
#define UVD_MPC_SET_MUXA1__VARA_7__SHIFT                                                                      0xc
#define UVD_MPC_SET_MUXA1__VARA_5_MASK                                                                        0x0000003FL
#define UVD_MPC_SET_MUXA1__VARA_6_MASK                                                                        0x00000FC0L
#define UVD_MPC_SET_MUXA1__VARA_7_MASK                                                                        0x0003F000L
//UVD_MPC_SET_MUXB0
#define UVD_MPC_SET_MUXB0__VARB_0__SHIFT                                                                      0x0
#define UVD_MPC_SET_MUXB0__VARB_1__SHIFT                                                                      0x6
#define UVD_MPC_SET_MUXB0__VARB_2__SHIFT                                                                      0xc
#define UVD_MPC_SET_MUXB0__VARB_3__SHIFT                                                                      0x12
#define UVD_MPC_SET_MUXB0__VARB_4__SHIFT                                                                      0x18
#define UVD_MPC_SET_MUXB0__VARB_0_MASK                                                                        0x0000003FL
#define UVD_MPC_SET_MUXB0__VARB_1_MASK                                                                        0x00000FC0L
#define UVD_MPC_SET_MUXB0__VARB_2_MASK                                                                        0x0003F000L
#define UVD_MPC_SET_MUXB0__VARB_3_MASK                                                                        0x00FC0000L
#define UVD_MPC_SET_MUXB0__VARB_4_MASK                                                                        0x3F000000L
//UVD_MPC_SET_MUXB1
#define UVD_MPC_SET_MUXB1__VARB_5__SHIFT                                                                      0x0
#define UVD_MPC_SET_MUXB1__VARB_6__SHIFT                                                                      0x6
#define UVD_MPC_SET_MUXB1__VARB_7__SHIFT                                                                      0xc
#define UVD_MPC_SET_MUXB1__VARB_5_MASK                                                                        0x0000003FL
#define UVD_MPC_SET_MUXB1__VARB_6_MASK                                                                        0x00000FC0L
#define UVD_MPC_SET_MUXB1__VARB_7_MASK                                                                        0x0003F000L
//UVD_MPC_SET_MUX
#define UVD_MPC_SET_MUX__SET_0__SHIFT                                                                         0x0
#define UVD_MPC_SET_MUX__SET_1__SHIFT                                                                         0x3
#define UVD_MPC_SET_MUX__SET_2__SHIFT                                                                         0x6
#define UVD_MPC_SET_MUX__SET_0_MASK                                                                           0x00000007L
#define UVD_MPC_SET_MUX__SET_1_MASK                                                                           0x00000038L
#define UVD_MPC_SET_MUX__SET_2_MASK                                                                           0x000001C0L
//UVD_MPC_SET_ALU
#define UVD_MPC_SET_ALU__FUNCT__SHIFT                                                                         0x0
#define UVD_MPC_SET_ALU__OPERAND__SHIFT                                                                       0x4
#define UVD_MPC_SET_ALU__FUNCT_MASK                                                                           0x00000007L
#define UVD_MPC_SET_ALU__OPERAND_MASK                                                                         0x00000FF0L
//UVD_GPCOM_SYS_CMD
#define UVD_GPCOM_SYS_CMD__CMD_SEND__SHIFT                                                                    0x0
#define UVD_GPCOM_SYS_CMD__CMD__SHIFT                                                                         0x1
#define UVD_GPCOM_SYS_CMD__CMD_SOURCE__SHIFT                                                                  0x1f
#define UVD_GPCOM_SYS_CMD__CMD_SEND_MASK                                                                      0x00000001L
#define UVD_GPCOM_SYS_CMD__CMD_MASK                                                                           0x7FFFFFFEL
#define UVD_GPCOM_SYS_CMD__CMD_SOURCE_MASK                                                                    0x80000000L
//UVD_GPCOM_SYS_DATA0
#define UVD_GPCOM_SYS_DATA0__DATA0__SHIFT                                                                     0x0
#define UVD_GPCOM_SYS_DATA0__DATA0_MASK                                                                       0xFFFFFFFFL
//UVD_GPCOM_SYS_DATA1
#define UVD_GPCOM_SYS_DATA1__DATA1__SHIFT                                                                     0x0
#define UVD_GPCOM_SYS_DATA1__DATA1_MASK                                                                       0xFFFFFFFFL
//UVD_VCPU_CACHE_OFFSET0
#define UVD_VCPU_CACHE_OFFSET0__CACHE_OFFSET0__SHIFT                                                          0x0
#define UVD_VCPU_CACHE_OFFSET0__CACHE_OFFSET0_MASK                                                            0x001FFFFFL
//UVD_VCPU_CACHE_SIZE0
#define UVD_VCPU_CACHE_SIZE0__CACHE_SIZE0__SHIFT                                                              0x0
#define UVD_VCPU_CACHE_SIZE0__CACHE_SIZE0_MASK                                                                0x001FFFFFL
//UVD_VCPU_CACHE_OFFSET1
#define UVD_VCPU_CACHE_OFFSET1__CACHE_OFFSET1__SHIFT                                                          0x0
#define UVD_VCPU_CACHE_OFFSET1__CACHE_OFFSET1_MASK                                                            0x001FFFFFL
//UVD_VCPU_CACHE_SIZE1
#define UVD_VCPU_CACHE_SIZE1__CACHE_SIZE1__SHIFT                                                              0x0
#define UVD_VCPU_CACHE_SIZE1__CACHE_SIZE1_MASK                                                                0x001FFFFFL
//UVD_VCPU_CACHE_OFFSET2
#define UVD_VCPU_CACHE_OFFSET2__CACHE_OFFSET2__SHIFT                                                          0x0
#define UVD_VCPU_CACHE_OFFSET2__CACHE_OFFSET2_MASK                                                            0x001FFFFFL
//UVD_VCPU_CACHE_SIZE2
#define UVD_VCPU_CACHE_SIZE2__CACHE_SIZE2__SHIFT                                                              0x0
#define UVD_VCPU_CACHE_SIZE2__CACHE_SIZE2_MASK                                                                0x001FFFFFL
//UVD_VCPU_CNTL
#define UVD_VCPU_CNTL__CLK_EN__SHIFT                                                                          0x9
#define UVD_VCPU_CNTL__CLK_EN_MASK                                                                            0x00000200L
//UVD_SOFT_RESET
#define UVD_SOFT_RESET__RBC_SOFT_RESET__SHIFT                                                                 0x0
#define UVD_SOFT_RESET__LBSI_SOFT_RESET__SHIFT                                                                0x1
#define UVD_SOFT_RESET__LMI_SOFT_RESET__SHIFT                                                                 0x2
#define UVD_SOFT_RESET__VCPU_SOFT_RESET__SHIFT                                                                0x3
#define UVD_SOFT_RESET__UDEC_SOFT_RESET__SHIFT                                                                0x4
#define UVD_SOFT_RESET__CSM_SOFT_RESET__SHIFT                                                                 0x5
#define UVD_SOFT_RESET__CXW_SOFT_RESET__SHIFT                                                                 0x6
#define UVD_SOFT_RESET__TAP_SOFT_RESET__SHIFT                                                                 0x7
#define UVD_SOFT_RESET__MPC_SOFT_RESET__SHIFT                                                                 0x8
#define UVD_SOFT_RESET__IH_SOFT_RESET__SHIFT                                                                  0xa
#define UVD_SOFT_RESET__LMI_UMC_SOFT_RESET__SHIFT                                                             0xd
#define UVD_SOFT_RESET__SPH_SOFT_RESET__SHIFT                                                                 0xe
#define UVD_SOFT_RESET__MIF_SOFT_RESET__SHIFT                                                                 0xf
#define UVD_SOFT_RESET__LCM_SOFT_RESET__SHIFT                                                                 0x10
#define UVD_SOFT_RESET__SUVD_SOFT_RESET__SHIFT                                                                0x11
#define UVD_SOFT_RESET__LBSI_VCLK_RESET_STATUS__SHIFT                                                         0x12
#define UVD_SOFT_RESET__VCPU_VCLK_RESET_STATUS__SHIFT                                                         0x13
#define UVD_SOFT_RESET__UDEC_VCLK_RESET_STATUS__SHIFT                                                         0x14
#define UVD_SOFT_RESET__UDEC_DCLK_RESET_STATUS__SHIFT                                                         0x15
#define UVD_SOFT_RESET__MPC_DCLK_RESET_STATUS__SHIFT                                                          0x16
#define UVD_SOFT_RESET__MIF_DCLK_RESET_STATUS__SHIFT                                                          0x1a
#define UVD_SOFT_RESET__LCM_DCLK_RESET_STATUS__SHIFT                                                          0x1b
#define UVD_SOFT_RESET__SUVD_VCLK_RESET_STATUS__SHIFT                                                         0x1c
#define UVD_SOFT_RESET__SUVD_DCLK_RESET_STATUS__SHIFT                                                         0x1d
#define UVD_SOFT_RESET__RE_DCLK_RESET_STATUS__SHIFT                                                           0x1e
#define UVD_SOFT_RESET__SRE_DCLK_RESET_STATUS__SHIFT                                                          0x1f
#define UVD_SOFT_RESET__RBC_SOFT_RESET_MASK                                                                   0x00000001L
#define UVD_SOFT_RESET__LBSI_SOFT_RESET_MASK                                                                  0x00000002L
#define UVD_SOFT_RESET__LMI_SOFT_RESET_MASK                                                                   0x00000004L
#define UVD_SOFT_RESET__VCPU_SOFT_RESET_MASK                                                                  0x00000008L
#define UVD_SOFT_RESET__UDEC_SOFT_RESET_MASK                                                                  0x00000010L
#define UVD_SOFT_RESET__CSM_SOFT_RESET_MASK                                                                   0x00000020L
#define UVD_SOFT_RESET__CXW_SOFT_RESET_MASK                                                                   0x00000040L
#define UVD_SOFT_RESET__TAP_SOFT_RESET_MASK                                                                   0x00000080L
#define UVD_SOFT_RESET__MPC_SOFT_RESET_MASK                                                                   0x00000100L
#define UVD_SOFT_RESET__IH_SOFT_RESET_MASK                                                                    0x00000400L
#define UVD_SOFT_RESET__LMI_UMC_SOFT_RESET_MASK                                                               0x00002000L
#define UVD_SOFT_RESET__SPH_SOFT_RESET_MASK                                                                   0x00004000L
#define UVD_SOFT_RESET__MIF_SOFT_RESET_MASK                                                                   0x00008000L
#define UVD_SOFT_RESET__LCM_SOFT_RESET_MASK                                                                   0x00010000L
#define UVD_SOFT_RESET__SUVD_SOFT_RESET_MASK                                                                  0x00020000L
#define UVD_SOFT_RESET__LBSI_VCLK_RESET_STATUS_MASK                                                           0x00040000L
#define UVD_SOFT_RESET__VCPU_VCLK_RESET_STATUS_MASK                                                           0x00080000L
#define UVD_SOFT_RESET__UDEC_VCLK_RESET_STATUS_MASK                                                           0x00100000L
#define UVD_SOFT_RESET__UDEC_DCLK_RESET_STATUS_MASK                                                           0x00200000L
#define UVD_SOFT_RESET__MPC_DCLK_RESET_STATUS_MASK                                                            0x00400000L
#define UVD_SOFT_RESET__MIF_DCLK_RESET_STATUS_MASK                                                            0x04000000L
#define UVD_SOFT_RESET__LCM_DCLK_RESET_STATUS_MASK                                                            0x08000000L
#define UVD_SOFT_RESET__SUVD_VCLK_RESET_STATUS_MASK                                                           0x10000000L
#define UVD_SOFT_RESET__SUVD_DCLK_RESET_STATUS_MASK                                                           0x20000000L
#define UVD_SOFT_RESET__RE_DCLK_RESET_STATUS_MASK                                                             0x40000000L
#define UVD_SOFT_RESET__SRE_DCLK_RESET_STATUS_MASK                                                            0x80000000L
//UVD_LMI_RBC_IB_VMID
#define UVD_LMI_RBC_IB_VMID__IB_VMID__SHIFT                                                                   0x0
#define UVD_LMI_RBC_IB_VMID__IB_VMID_MASK                                                                     0x0000000FL
//UVD_RBC_IB_SIZE
#define UVD_RBC_IB_SIZE__IB_SIZE__SHIFT                                                                       0x4
#define UVD_RBC_IB_SIZE__IB_SIZE_MASK                                                                         0x007FFFF0L
//UVD_RBC_RB_RPTR
#define UVD_RBC_RB_RPTR__RB_RPTR__SHIFT                                                                       0x4
#define UVD_RBC_RB_RPTR__RB_RPTR_MASK                                                                         0x007FFFF0L
//UVD_RBC_RB_WPTR
#define UVD_RBC_RB_WPTR__RB_WPTR__SHIFT                                                                       0x4
#define UVD_RBC_RB_WPTR__RB_WPTR_MASK                                                                         0x007FFFF0L
//UVD_RBC_RB_WPTR_CNTL
#define UVD_RBC_RB_WPTR_CNTL__RB_PRE_WRITE_TIMER__SHIFT                                                       0x0
#define UVD_RBC_RB_WPTR_CNTL__RB_PRE_WRITE_TIMER_MASK                                                         0x00007FFFL
//UVD_RBC_WPTR_STATUS
#define UVD_RBC_WPTR_STATUS__RB_WPTR_IN_USE__SHIFT                                                            0x4
#define UVD_RBC_WPTR_STATUS__RB_WPTR_IN_USE_MASK                                                              0x007FFFF0L
//UVD_RBC_RB_CNTL
#define UVD_RBC_RB_CNTL__RB_BUFSZ__SHIFT                                                                      0x0
#define UVD_RBC_RB_CNTL__RB_BLKSZ__SHIFT                                                                      0x8
#define UVD_RBC_RB_CNTL__RB_NO_FETCH__SHIFT                                                                   0x10
#define UVD_RBC_RB_CNTL__RB_WPTR_POLL_EN__SHIFT                                                               0x14
#define UVD_RBC_RB_CNTL__RB_NO_UPDATE__SHIFT                                                                  0x18
#define UVD_RBC_RB_CNTL__RB_RPTR_WR_EN__SHIFT                                                                 0x1c
#define UVD_RBC_RB_CNTL__RB_BUFSZ_MASK                                                                        0x0000001FL
#define UVD_RBC_RB_CNTL__RB_BLKSZ_MASK                                                                        0x00001F00L
#define UVD_RBC_RB_CNTL__RB_NO_FETCH_MASK                                                                     0x00010000L
#define UVD_RBC_RB_CNTL__RB_WPTR_POLL_EN_MASK                                                                 0x00100000L
#define UVD_RBC_RB_CNTL__RB_NO_UPDATE_MASK                                                                    0x01000000L
#define UVD_RBC_RB_CNTL__RB_RPTR_WR_EN_MASK                                                                   0x10000000L
//UVD_RBC_RB_RPTR_ADDR
#define UVD_RBC_RB_RPTR_ADDR__RB_RPTR_ADDR__SHIFT                                                             0x0
#define UVD_RBC_RB_RPTR_ADDR__RB_RPTR_ADDR_MASK                                                               0xFFFFFFFFL
//UVD_STATUS
#define UVD_STATUS__RBC_BUSY__SHIFT                                                                           0x0
#define UVD_STATUS__VCPU_REPORT__SHIFT                                                                        0x1
#define UVD_STATUS__AVP_BUSY__SHIFT                                                                           0x8
#define UVD_STATUS__IDCT_BUSY__SHIFT                                                                          0x9
#define UVD_STATUS__IDCT_CTL_ACK__SHIFT                                                                       0xb
#define UVD_STATUS__UVD_CTL_ACK__SHIFT                                                                        0xc
#define UVD_STATUS__AVP_BLOCK_ACK__SHIFT                                                                      0xd
#define UVD_STATUS__IDCT_BLOCK_ACK__SHIFT                                                                     0xe
#define UVD_STATUS__UVD_BLOCK_ACK__SHIFT                                                                      0xf
#define UVD_STATUS__RBC_ACCESS_GPCOM__SHIFT                                                                   0x10
#define UVD_STATUS__SYS_GPCOM_REQ__SHIFT                                                                      0x1f
#define UVD_STATUS__RBC_BUSY_MASK                                                                             0x00000001L
#define UVD_STATUS__VCPU_REPORT_MASK                                                                          0x000000FEL
#define UVD_STATUS__AVP_BUSY_MASK                                                                             0x00000100L
#define UVD_STATUS__IDCT_BUSY_MASK                                                                            0x00000200L
#define UVD_STATUS__IDCT_CTL_ACK_MASK                                                                         0x00000800L
#define UVD_STATUS__UVD_CTL_ACK_MASK                                                                          0x00001000L
#define UVD_STATUS__AVP_BLOCK_ACK_MASK                                                                        0x00002000L
#define UVD_STATUS__IDCT_BLOCK_ACK_MASK                                                                       0x00004000L
#define UVD_STATUS__UVD_BLOCK_ACK_MASK                                                                        0x00008000L
#define UVD_STATUS__RBC_ACCESS_GPCOM_MASK                                                                     0x00010000L
#define UVD_STATUS__SYS_GPCOM_REQ_MASK                                                                        0x80000000L
//UVD_SEMA_TIMEOUT_STATUS
#define UVD_SEMA_TIMEOUT_STATUS__SEMAPHORE_WAIT_INCOMPLETE_TIMEOUT_STAT__SHIFT                                0x0
#define UVD_SEMA_TIMEOUT_STATUS__SEMAPHORE_WAIT_FAULT_TIMEOUT_STAT__SHIFT                                     0x1
#define UVD_SEMA_TIMEOUT_STATUS__SEMAPHORE_SIGNAL_INCOMPLETE_TIMEOUT_STAT__SHIFT                              0x2
#define UVD_SEMA_TIMEOUT_STATUS__SEMAPHORE_TIMEOUT_CLEAR__SHIFT                                               0x3
#define UVD_SEMA_TIMEOUT_STATUS__SEMAPHORE_WAIT_INCOMPLETE_TIMEOUT_STAT_MASK                                  0x00000001L
#define UVD_SEMA_TIMEOUT_STATUS__SEMAPHORE_WAIT_FAULT_TIMEOUT_STAT_MASK                                       0x00000002L
#define UVD_SEMA_TIMEOUT_STATUS__SEMAPHORE_SIGNAL_INCOMPLETE_TIMEOUT_STAT_MASK                                0x00000004L
#define UVD_SEMA_TIMEOUT_STATUS__SEMAPHORE_TIMEOUT_CLEAR_MASK                                                 0x00000008L
//UVD_SEMA_WAIT_INCOMPLETE_TIMEOUT_CNTL
#define UVD_SEMA_WAIT_INCOMPLETE_TIMEOUT_CNTL__WAIT_INCOMPLETE_EN__SHIFT                                      0x0
#define UVD_SEMA_WAIT_INCOMPLETE_TIMEOUT_CNTL__WAIT_INCOMPLETE_COUNT__SHIFT                                   0x1
#define UVD_SEMA_WAIT_INCOMPLETE_TIMEOUT_CNTL__RESEND_TIMER__SHIFT                                            0x18
#define UVD_SEMA_WAIT_INCOMPLETE_TIMEOUT_CNTL__WAIT_INCOMPLETE_EN_MASK                                        0x00000001L
#define UVD_SEMA_WAIT_INCOMPLETE_TIMEOUT_CNTL__WAIT_INCOMPLETE_COUNT_MASK                                     0x001FFFFEL
#define UVD_SEMA_WAIT_INCOMPLETE_TIMEOUT_CNTL__RESEND_TIMER_MASK                                              0x07000000L
//UVD_SEMA_WAIT_FAULT_TIMEOUT_CNTL
#define UVD_SEMA_WAIT_FAULT_TIMEOUT_CNTL__WAIT_FAULT_EN__SHIFT                                                0x0
#define UVD_SEMA_WAIT_FAULT_TIMEOUT_CNTL__WAIT_FAULT_COUNT__SHIFT                                             0x1
#define UVD_SEMA_WAIT_FAULT_TIMEOUT_CNTL__RESEND_TIMER__SHIFT                                                 0x18
#define UVD_SEMA_WAIT_FAULT_TIMEOUT_CNTL__WAIT_FAULT_EN_MASK                                                  0x00000001L
#define UVD_SEMA_WAIT_FAULT_TIMEOUT_CNTL__WAIT_FAULT_COUNT_MASK                                               0x001FFFFEL
#define UVD_SEMA_WAIT_FAULT_TIMEOUT_CNTL__RESEND_TIMER_MASK                                                   0x07000000L
//UVD_SEMA_SIGNAL_INCOMPLETE_TIMEOUT_CNTL
#define UVD_SEMA_SIGNAL_INCOMPLETE_TIMEOUT_CNTL__SIGNAL_INCOMPLETE_EN__SHIFT                                  0x0
#define UVD_SEMA_SIGNAL_INCOMPLETE_TIMEOUT_CNTL__SIGNAL_INCOMPLETE_COUNT__SHIFT                               0x1
#define UVD_SEMA_SIGNAL_INCOMPLETE_TIMEOUT_CNTL__RESEND_TIMER__SHIFT                                          0x18
#define UVD_SEMA_SIGNAL_INCOMPLETE_TIMEOUT_CNTL__SIGNAL_INCOMPLETE_EN_MASK                                    0x00000001L
#define UVD_SEMA_SIGNAL_INCOMPLETE_TIMEOUT_CNTL__SIGNAL_INCOMPLETE_COUNT_MASK                                 0x001FFFFEL
#define UVD_SEMA_SIGNAL_INCOMPLETE_TIMEOUT_CNTL__RESEND_TIMER_MASK                                            0x07000000L
//UVD_CONTEXT_ID
#define UVD_CONTEXT_ID__CONTEXT_ID__SHIFT                                                                     0x0
#define UVD_CONTEXT_ID__CONTEXT_ID_MASK                                                                       0xFFFFFFFFL
//UVD_CONTEXT_ID2
#define UVD_CONTEXT_ID2__CONTEXT_ID2__SHIFT                                                                   0x0
#define UVD_CONTEXT_ID2__CONTEXT_ID2_MASK                                                                     0xFFFFFFFFL
//UVD_RBC_WPTR_POLL_CNTL
#define UVD_RBC_WPTR_POLL_CNTL__POLL_FREQ__SHIFT                                                              0x0
#define UVD_RBC_WPTR_POLL_CNTL__IDLE_POLL_COUNT__SHIFT                                                        0x10
#define UVD_RBC_WPTR_POLL_CNTL__POLL_FREQ_MASK                                                                0x0000FFFFL
#define UVD_RBC_WPTR_POLL_CNTL__IDLE_POLL_COUNT_MASK                                                          0xFFFF0000L
//UVD_RBC_WPTR_POLL_ADDR
#define UVD_RBC_WPTR_POLL_ADDR__POLL_ADDR__SHIFT                                                              0x2
#define UVD_RBC_WPTR_POLL_ADDR__POLL_ADDR_MASK                                                                0xFFFFFFFCL
//UVD_RB_BASE_LO4
#define UVD_RB_BASE_LO4__RB_BASE_LO__SHIFT                                                                    0x6
#define UVD_RB_BASE_LO4__RB_BASE_LO_MASK                                                                      0xFFFFFFC0L
//UVD_RB_BASE_HI4
#define UVD_RB_BASE_HI4__RB_BASE_HI__SHIFT                                                                    0x0
#define UVD_RB_BASE_HI4__RB_BASE_HI_MASK                                                                      0xFFFFFFFFL
//UVD_RB_SIZE4
#define UVD_RB_SIZE4__RB_SIZE__SHIFT                                                                          0x4
#define UVD_RB_SIZE4__RB_SIZE_MASK                                                                            0x007FFFF0L
//UVD_RB_RPTR4
#define UVD_RB_RPTR4__RB_RPTR__SHIFT                                                                          0x4
#define UVD_RB_RPTR4__RB_RPTR_MASK                                                                            0x007FFFF0L


#endif
