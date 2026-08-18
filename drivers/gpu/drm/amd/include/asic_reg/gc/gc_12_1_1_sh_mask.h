/*
 * Copyright 2025 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 */
#ifndef _gc_12_1_1_SH_MASK_HEADER
#define _gc_12_1_1_SH_MASK_HEADER


// addressBlock: aigc_grbma_grbma_grbmadec
//GRBMA_GFX_INDEX
#define GRBMA_GFX_INDEX__INSTANCE_INDEX__SHIFT                                                                0x0
#define GRBMA_GFX_INDEX__SA_INDEX__SHIFT                                                                      0x8
#define GRBMA_GFX_INDEX__SE_INDEX__SHIFT                                                                      0x10
#define GRBMA_GFX_INDEX__SA_BROADCAST_WRITES__SHIFT                                                           0x1d
#define GRBMA_GFX_INDEX__INSTANCE_BROADCAST_WRITES__SHIFT                                                     0x1e
#define GRBMA_GFX_INDEX__SE_BROADCAST_WRITES__SHIFT                                                           0x1f
#define GRBMA_GFX_INDEX__INSTANCE_INDEX_MASK                                                                  0x0000007FL
#define GRBMA_GFX_INDEX__SA_INDEX_MASK                                                                        0x00000300L
#define GRBMA_GFX_INDEX__SE_INDEX_MASK                                                                        0x000F0000L
#define GRBMA_GFX_INDEX__SA_BROADCAST_WRITES_MASK                                                             0x20000000L
#define GRBMA_GFX_INDEX__INSTANCE_BROADCAST_WRITES_MASK                                                       0x40000000L
#define GRBMA_GFX_INDEX__SE_BROADCAST_WRITES_MASK                                                             0x80000000L


// addressBlock: aigc_grbma_grbma_perfddec
//GRBMA_PERFCOUNTER0_LO
#define GRBMA_PERFCOUNTER0_LO__PERFCOUNTER_LO__SHIFT                                                          0x0
#define GRBMA_PERFCOUNTER0_LO__PERFCOUNTER_LO_MASK                                                            0xFFFFFFFFL
//GRBMA_PERFCOUNTER0_HI
#define GRBMA_PERFCOUNTER0_HI__PERFCOUNTER_HI__SHIFT                                                          0x0
#define GRBMA_PERFCOUNTER0_HI__PERFCOUNTER_HI_MASK                                                            0xFFFFFFFFL
//GRBMA_PERFCOUNTER1_LO
#define GRBMA_PERFCOUNTER1_LO__PERFCOUNTER_LO__SHIFT                                                          0x0
#define GRBMA_PERFCOUNTER1_LO__PERFCOUNTER_LO_MASK                                                            0xFFFFFFFFL
//GRBMA_PERFCOUNTER1_HI
#define GRBMA_PERFCOUNTER1_HI__PERFCOUNTER_HI__SHIFT                                                          0x0
#define GRBMA_PERFCOUNTER1_HI__PERFCOUNTER_HI_MASK                                                            0xFFFFFFFFL


// addressBlock: aigc_grbma_grbma_perfsdec
//GRBMA_PERFCOUNTER0_SELECT
#define GRBMA_PERFCOUNTER0_SELECT__PERF_SEL__SHIFT                                                            0x0
#define GRBMA_PERFCOUNTER0_SELECT__GL1CC_BUSY_USER_DEFINED_MASK__SHIFT                                        0x6
#define GRBMA_PERFCOUNTER0_SELECT__GL1XCC_BUSY_USER_DEFINED_MASK__SHIFT                                       0x7
#define GRBMA_PERFCOUNTER0_SELECT__PMR_BUSY_USER_DEFINED_MASK__SHIFT                                          0x8
#define GRBMA_PERFCOUNTER0_SELECT__SC_CLEAN_USER_DEFINED_MASK__SHIFT                                          0x9
#define GRBMA_PERFCOUNTER0_SELECT__WGS_BUSY_USER_DEFINED_MASK__SHIFT                                          0xa
#define GRBMA_PERFCOUNTER0_SELECT__DB_CLEAN_USER_DEFINED_MASK__SHIFT                                          0xb
#define GRBMA_PERFCOUNTER0_SELECT__CB_CLEAN_USER_DEFINED_MASK__SHIFT                                          0xc
#define GRBMA_PERFCOUNTER0_SELECT__TA_BUSY_USER_DEFINED_MASK__SHIFT                                           0xd
#define GRBMA_PERFCOUNTER0_SELECT__SX_BUSY_USER_DEFINED_MASK__SHIFT                                           0xe
#define GRBMA_PERFCOUNTER0_SELECT__GL2C_BUSY_USER_DEFINED_MASK__SHIFT                                         0xf
#define GRBMA_PERFCOUNTER0_SELECT__SPI_BUSY_USER_DEFINED_MASK__SHIFT                                          0x10
#define GRBMA_PERFCOUNTER0_SELECT__XCAC_BUSY_USER_DEFINED_MASK__SHIFT                                         0x11
#define GRBMA_PERFCOUNTER0_SELECT__PA_BUSY_USER_DEFINED_MASK__SHIFT                                           0x12
#define GRBMA_PERFCOUNTER0_SELECT__GL2A_BUSY_USER_DEFINED_MASK__SHIFT                                         0x13
#define GRBMA_PERFCOUNTER0_SELECT__DB_BUSY_USER_DEFINED_MASK__SHIFT                                           0x14
#define GRBMA_PERFCOUNTER0_SELECT__CB_BUSY_USER_DEFINED_MASK__SHIFT                                           0x15
#define GRBMA_PERFCOUNTER0_SELECT__EA_LINK_BUSY_USER_DEFINED_MASK__SHIFT                                      0x17
#define GRBMA_PERFCOUNTER0_SELECT__AIGC_CAC_BUSY_USER_DEFINED_MASK__SHIFT                                     0x18
#define GRBMA_PERFCOUNTER0_SELECT__BCI_BUSY_USER_DEFINED_MASK__SHIFT                                          0x19
#define GRBMA_PERFCOUNTER0_SELECT__RLC_BUSY_USER_DEFINED_MASK__SHIFT                                          0x1a
#define GRBMA_PERFCOUNTER0_SELECT__TCP_BUSY_USER_DEFINED_MASK__SHIFT                                          0x1b
#define GRBMA_PERFCOUNTER0_SELECT__GE_BUSY_USER_DEFINED_MASK__SHIFT                                           0x1c
#define GRBMA_PERFCOUNTER0_SELECT__UTCL1_BUSY_USER_DEFINED_MASK__SHIFT                                        0x1d
#define GRBMA_PERFCOUNTER0_SELECT__EA_BUSY_USER_DEFINED_MASK__SHIFT                                           0x1e
#define GRBMA_PERFCOUNTER0_SELECT__PERF_SEL_MASK                                                              0x0000003FL
#define GRBMA_PERFCOUNTER0_SELECT__GL1CC_BUSY_USER_DEFINED_MASK_MASK                                          0x00000040L
#define GRBMA_PERFCOUNTER0_SELECT__GL1XCC_BUSY_USER_DEFINED_MASK_MASK                                         0x00000080L
#define GRBMA_PERFCOUNTER0_SELECT__PMR_BUSY_USER_DEFINED_MASK_MASK                                            0x00000100L
#define GRBMA_PERFCOUNTER0_SELECT__SC_CLEAN_USER_DEFINED_MASK_MASK                                            0x00000200L
#define GRBMA_PERFCOUNTER0_SELECT__WGS_BUSY_USER_DEFINED_MASK_MASK                                            0x00000400L
#define GRBMA_PERFCOUNTER0_SELECT__DB_CLEAN_USER_DEFINED_MASK_MASK                                            0x00000800L
#define GRBMA_PERFCOUNTER0_SELECT__CB_CLEAN_USER_DEFINED_MASK_MASK                                            0x00001000L
#define GRBMA_PERFCOUNTER0_SELECT__TA_BUSY_USER_DEFINED_MASK_MASK                                             0x00002000L
#define GRBMA_PERFCOUNTER0_SELECT__SX_BUSY_USER_DEFINED_MASK_MASK                                             0x00004000L
#define GRBMA_PERFCOUNTER0_SELECT__GL2C_BUSY_USER_DEFINED_MASK_MASK                                           0x00008000L
#define GRBMA_PERFCOUNTER0_SELECT__SPI_BUSY_USER_DEFINED_MASK_MASK                                            0x00010000L
#define GRBMA_PERFCOUNTER0_SELECT__XCAC_BUSY_USER_DEFINED_MASK_MASK                                           0x00020000L
#define GRBMA_PERFCOUNTER0_SELECT__PA_BUSY_USER_DEFINED_MASK_MASK                                             0x00040000L
#define GRBMA_PERFCOUNTER0_SELECT__GL2A_BUSY_USER_DEFINED_MASK_MASK                                           0x00080000L
#define GRBMA_PERFCOUNTER0_SELECT__DB_BUSY_USER_DEFINED_MASK_MASK                                             0x00100000L
#define GRBMA_PERFCOUNTER0_SELECT__CB_BUSY_USER_DEFINED_MASK_MASK                                             0x00200000L
#define GRBMA_PERFCOUNTER0_SELECT__EA_LINK_BUSY_USER_DEFINED_MASK_MASK                                        0x00800000L
#define GRBMA_PERFCOUNTER0_SELECT__AIGC_CAC_BUSY_USER_DEFINED_MASK_MASK                                       0x01000000L
#define GRBMA_PERFCOUNTER0_SELECT__BCI_BUSY_USER_DEFINED_MASK_MASK                                            0x02000000L
#define GRBMA_PERFCOUNTER0_SELECT__RLC_BUSY_USER_DEFINED_MASK_MASK                                            0x04000000L
#define GRBMA_PERFCOUNTER0_SELECT__TCP_BUSY_USER_DEFINED_MASK_MASK                                            0x08000000L
#define GRBMA_PERFCOUNTER0_SELECT__GE_BUSY_USER_DEFINED_MASK_MASK                                             0x10000000L
#define GRBMA_PERFCOUNTER0_SELECT__UTCL1_BUSY_USER_DEFINED_MASK_MASK                                          0x20000000L
#define GRBMA_PERFCOUNTER0_SELECT__EA_BUSY_USER_DEFINED_MASK_MASK                                             0x40000000L
//GRBMA_PERFCOUNTER1_SELECT
#define GRBMA_PERFCOUNTER1_SELECT__PERF_SEL__SHIFT                                                            0x0
#define GRBMA_PERFCOUNTER1_SELECT__GL1CC_BUSY_USER_DEFINED_MASK__SHIFT                                        0x6
#define GRBMA_PERFCOUNTER1_SELECT__GL1XCC_BUSY_USER_DEFINED_MASK__SHIFT                                       0x7
#define GRBMA_PERFCOUNTER1_SELECT__PMR_BUSY_USER_DEFINED_MASK__SHIFT                                          0x8
#define GRBMA_PERFCOUNTER1_SELECT__SC_CLEAN_USER_DEFINED_MASK__SHIFT                                          0x9
#define GRBMA_PERFCOUNTER1_SELECT__WGS_BUSY_USER_DEFINED_MASK__SHIFT                                          0xa
#define GRBMA_PERFCOUNTER1_SELECT__DB_CLEAN_USER_DEFINED_MASK__SHIFT                                          0xb
#define GRBMA_PERFCOUNTER1_SELECT__CB_CLEAN_USER_DEFINED_MASK__SHIFT                                          0xc
#define GRBMA_PERFCOUNTER1_SELECT__TA_BUSY_USER_DEFINED_MASK__SHIFT                                           0xd
#define GRBMA_PERFCOUNTER1_SELECT__SX_BUSY_USER_DEFINED_MASK__SHIFT                                           0xe
#define GRBMA_PERFCOUNTER1_SELECT__GL2C_BUSY_USER_DEFINED_MASK__SHIFT                                         0xf
#define GRBMA_PERFCOUNTER1_SELECT__SPI_BUSY_USER_DEFINED_MASK__SHIFT                                          0x10
#define GRBMA_PERFCOUNTER1_SELECT__XCAC_BUSY_USER_DEFINED_MASK__SHIFT                                         0x11
#define GRBMA_PERFCOUNTER1_SELECT__PA_BUSY_USER_DEFINED_MASK__SHIFT                                           0x12
#define GRBMA_PERFCOUNTER1_SELECT__GL2A_BUSY_USER_DEFINED_MASK__SHIFT                                         0x13
#define GRBMA_PERFCOUNTER1_SELECT__DB_BUSY_USER_DEFINED_MASK__SHIFT                                           0x14
#define GRBMA_PERFCOUNTER1_SELECT__CB_BUSY_USER_DEFINED_MASK__SHIFT                                           0x15
#define GRBMA_PERFCOUNTER1_SELECT__EA_LINK_BUSY_USER_DEFINED_MASK__SHIFT                                      0x17
#define GRBMA_PERFCOUNTER1_SELECT__AIGC_CAC_BUSY_USER_DEFINED_MASK__SHIFT                                     0x18
#define GRBMA_PERFCOUNTER1_SELECT__BCI_BUSY_USER_DEFINED_MASK__SHIFT                                          0x19
#define GRBMA_PERFCOUNTER1_SELECT__RLC_BUSY_USER_DEFINED_MASK__SHIFT                                          0x1a
#define GRBMA_PERFCOUNTER1_SELECT__TCP_BUSY_USER_DEFINED_MASK__SHIFT                                          0x1b
#define GRBMA_PERFCOUNTER1_SELECT__GE_BUSY_USER_DEFINED_MASK__SHIFT                                           0x1c
#define GRBMA_PERFCOUNTER1_SELECT__UTCL1_BUSY_USER_DEFINED_MASK__SHIFT                                        0x1d
#define GRBMA_PERFCOUNTER1_SELECT__EA_BUSY_USER_DEFINED_MASK__SHIFT                                           0x1e
#define GRBMA_PERFCOUNTER1_SELECT__PERF_SEL_MASK                                                              0x0000003FL
#define GRBMA_PERFCOUNTER1_SELECT__GL1CC_BUSY_USER_DEFINED_MASK_MASK                                          0x00000040L
#define GRBMA_PERFCOUNTER1_SELECT__GL1XCC_BUSY_USER_DEFINED_MASK_MASK                                         0x00000080L
#define GRBMA_PERFCOUNTER1_SELECT__PMR_BUSY_USER_DEFINED_MASK_MASK                                            0x00000100L
#define GRBMA_PERFCOUNTER1_SELECT__SC_CLEAN_USER_DEFINED_MASK_MASK                                            0x00000200L
#define GRBMA_PERFCOUNTER1_SELECT__WGS_BUSY_USER_DEFINED_MASK_MASK                                            0x00000400L
#define GRBMA_PERFCOUNTER1_SELECT__DB_CLEAN_USER_DEFINED_MASK_MASK                                            0x00000800L
#define GRBMA_PERFCOUNTER1_SELECT__CB_CLEAN_USER_DEFINED_MASK_MASK                                            0x00001000L
#define GRBMA_PERFCOUNTER1_SELECT__TA_BUSY_USER_DEFINED_MASK_MASK                                             0x00002000L
#define GRBMA_PERFCOUNTER1_SELECT__SX_BUSY_USER_DEFINED_MASK_MASK                                             0x00004000L
#define GRBMA_PERFCOUNTER1_SELECT__GL2C_BUSY_USER_DEFINED_MASK_MASK                                           0x00008000L
#define GRBMA_PERFCOUNTER1_SELECT__SPI_BUSY_USER_DEFINED_MASK_MASK                                            0x00010000L
#define GRBMA_PERFCOUNTER1_SELECT__XCAC_BUSY_USER_DEFINED_MASK_MASK                                           0x00020000L
#define GRBMA_PERFCOUNTER1_SELECT__PA_BUSY_USER_DEFINED_MASK_MASK                                             0x00040000L
#define GRBMA_PERFCOUNTER1_SELECT__GL2A_BUSY_USER_DEFINED_MASK_MASK                                           0x00080000L
#define GRBMA_PERFCOUNTER1_SELECT__DB_BUSY_USER_DEFINED_MASK_MASK                                             0x00100000L
#define GRBMA_PERFCOUNTER1_SELECT__CB_BUSY_USER_DEFINED_MASK_MASK                                             0x00200000L
#define GRBMA_PERFCOUNTER1_SELECT__EA_LINK_BUSY_USER_DEFINED_MASK_MASK                                        0x00800000L
#define GRBMA_PERFCOUNTER1_SELECT__AIGC_CAC_BUSY_USER_DEFINED_MASK_MASK                                       0x01000000L
#define GRBMA_PERFCOUNTER1_SELECT__BCI_BUSY_USER_DEFINED_MASK_MASK                                            0x02000000L
#define GRBMA_PERFCOUNTER1_SELECT__RLC_BUSY_USER_DEFINED_MASK_MASK                                            0x04000000L
#define GRBMA_PERFCOUNTER1_SELECT__TCP_BUSY_USER_DEFINED_MASK_MASK                                            0x08000000L
#define GRBMA_PERFCOUNTER1_SELECT__GE_BUSY_USER_DEFINED_MASK_MASK                                             0x10000000L
#define GRBMA_PERFCOUNTER1_SELECT__UTCL1_BUSY_USER_DEFINED_MASK_MASK                                          0x20000000L
#define GRBMA_PERFCOUNTER1_SELECT__EA_BUSY_USER_DEFINED_MASK_MASK                                             0x40000000L
//AID_PERFMON_CNTL
#define AID_PERFMON_CNTL__PERFMON_STATE__SHIFT                                                                0x0
#define AID_PERFMON_CNTL__SPM_PERFMON_STATE__SHIFT                                                            0x4
#define AID_PERFMON_CNTL__PERFMON_ENABLE_MODE__SHIFT                                                          0x8
#define AID_PERFMON_CNTL__PERFMON_SAMPLE_ENABLE__SHIFT                                                        0xa
#define AID_PERFMON_CNTL__PERFMON_STATE_MASK                                                                  0x0000000FL
#define AID_PERFMON_CNTL__SPM_PERFMON_STATE_MASK                                                              0x000000F0L
#define AID_PERFMON_CNTL__PERFMON_ENABLE_MODE_MASK                                                            0x00000300L
#define AID_PERFMON_CNTL__PERFMON_SAMPLE_ENABLE_MASK                                                          0x00000400L


// addressBlock: aigc_gl2x_gfx_se_perfsdec
//GL2C_PERFCOUNTER0_SELECT
#define GL2C_PERFCOUNTER0_SELECT__PERF_SEL__SHIFT                                                             0x0
#define GL2C_PERFCOUNTER0_SELECT__PERF_SEL1__SHIFT                                                            0xa
#define GL2C_PERFCOUNTER0_SELECT__CNTR_MODE__SHIFT                                                            0x14
#define GL2C_PERFCOUNTER0_SELECT__PERF_MODE1__SHIFT                                                           0x18
#define GL2C_PERFCOUNTER0_SELECT__PERF_MODE__SHIFT                                                            0x1c
#define GL2C_PERFCOUNTER0_SELECT__PERF_SEL_MASK                                                               0x000003FFL
#define GL2C_PERFCOUNTER0_SELECT__PERF_SEL1_MASK                                                              0x000FFC00L
#define GL2C_PERFCOUNTER0_SELECT__CNTR_MODE_MASK                                                              0x00F00000L
#define GL2C_PERFCOUNTER0_SELECT__PERF_MODE1_MASK                                                             0x0F000000L
#define GL2C_PERFCOUNTER0_SELECT__PERF_MODE_MASK                                                              0xF0000000L
//GL2C_PERFCOUNTER0_SELECT1
#define GL2C_PERFCOUNTER0_SELECT1__PERF_SEL2__SHIFT                                                           0x0
#define GL2C_PERFCOUNTER0_SELECT1__PERF_SEL3__SHIFT                                                           0xa
#define GL2C_PERFCOUNTER0_SELECT1__PERF_MODE3__SHIFT                                                          0x18
#define GL2C_PERFCOUNTER0_SELECT1__PERF_MODE2__SHIFT                                                          0x1c
#define GL2C_PERFCOUNTER0_SELECT1__PERF_SEL2_MASK                                                             0x000003FFL
#define GL2C_PERFCOUNTER0_SELECT1__PERF_SEL3_MASK                                                             0x000FFC00L
#define GL2C_PERFCOUNTER0_SELECT1__PERF_MODE3_MASK                                                            0x0F000000L
#define GL2C_PERFCOUNTER0_SELECT1__PERF_MODE2_MASK                                                            0xF0000000L
//GL2C_PERFCOUNTER1_SELECT
#define GL2C_PERFCOUNTER1_SELECT__PERF_SEL__SHIFT                                                             0x0
#define GL2C_PERFCOUNTER1_SELECT__PERF_SEL1__SHIFT                                                            0xa
#define GL2C_PERFCOUNTER1_SELECT__CNTR_MODE__SHIFT                                                            0x14
#define GL2C_PERFCOUNTER1_SELECT__PERF_MODE1__SHIFT                                                           0x18
#define GL2C_PERFCOUNTER1_SELECT__PERF_MODE__SHIFT                                                            0x1c
#define GL2C_PERFCOUNTER1_SELECT__PERF_SEL_MASK                                                               0x000003FFL
#define GL2C_PERFCOUNTER1_SELECT__PERF_SEL1_MASK                                                              0x000FFC00L
#define GL2C_PERFCOUNTER1_SELECT__CNTR_MODE_MASK                                                              0x00F00000L
#define GL2C_PERFCOUNTER1_SELECT__PERF_MODE1_MASK                                                             0x0F000000L
#define GL2C_PERFCOUNTER1_SELECT__PERF_MODE_MASK                                                              0xF0000000L
//GL2C_PERFCOUNTER1_SELECT1
#define GL2C_PERFCOUNTER1_SELECT1__PERF_SEL2__SHIFT                                                           0x0
#define GL2C_PERFCOUNTER1_SELECT1__PERF_SEL3__SHIFT                                                           0xa
#define GL2C_PERFCOUNTER1_SELECT1__PERF_MODE3__SHIFT                                                          0x18
#define GL2C_PERFCOUNTER1_SELECT1__PERF_MODE2__SHIFT                                                          0x1c
#define GL2C_PERFCOUNTER1_SELECT1__PERF_SEL2_MASK                                                             0x000003FFL
#define GL2C_PERFCOUNTER1_SELECT1__PERF_SEL3_MASK                                                             0x000FFC00L
#define GL2C_PERFCOUNTER1_SELECT1__PERF_MODE3_MASK                                                            0x0F000000L
#define GL2C_PERFCOUNTER1_SELECT1__PERF_MODE2_MASK                                                            0xF0000000L
//GL2C_PERFCOUNTER2_SELECT
#define GL2C_PERFCOUNTER2_SELECT__PERF_SEL__SHIFT                                                             0x0
#define GL2C_PERFCOUNTER2_SELECT__PERF_SEL1__SHIFT                                                            0xa
#define GL2C_PERFCOUNTER2_SELECT__CNTR_MODE__SHIFT                                                            0x14
#define GL2C_PERFCOUNTER2_SELECT__PERF_MODE1__SHIFT                                                           0x18
#define GL2C_PERFCOUNTER2_SELECT__PERF_MODE__SHIFT                                                            0x1c
#define GL2C_PERFCOUNTER2_SELECT__PERF_SEL_MASK                                                               0x000003FFL
#define GL2C_PERFCOUNTER2_SELECT__PERF_SEL1_MASK                                                              0x000FFC00L
#define GL2C_PERFCOUNTER2_SELECT__CNTR_MODE_MASK                                                              0x00F00000L
#define GL2C_PERFCOUNTER2_SELECT__PERF_MODE1_MASK                                                             0x0F000000L
#define GL2C_PERFCOUNTER2_SELECT__PERF_MODE_MASK                                                              0xF0000000L
//GL2C_PERFCOUNTER2_SELECT1
#define GL2C_PERFCOUNTER2_SELECT1__PERF_SEL2__SHIFT                                                           0x0
#define GL2C_PERFCOUNTER2_SELECT1__PERF_SEL3__SHIFT                                                           0xa
#define GL2C_PERFCOUNTER2_SELECT1__PERF_MODE3__SHIFT                                                          0x18
#define GL2C_PERFCOUNTER2_SELECT1__PERF_MODE2__SHIFT                                                          0x1c
#define GL2C_PERFCOUNTER2_SELECT1__PERF_SEL2_MASK                                                             0x000003FFL
#define GL2C_PERFCOUNTER2_SELECT1__PERF_SEL3_MASK                                                             0x000FFC00L
#define GL2C_PERFCOUNTER2_SELECT1__PERF_MODE3_MASK                                                            0x0F000000L
#define GL2C_PERFCOUNTER2_SELECT1__PERF_MODE2_MASK                                                            0xF0000000L
//GL2C_PERFCOUNTER3_SELECT
#define GL2C_PERFCOUNTER3_SELECT__PERF_SEL__SHIFT                                                             0x0
#define GL2C_PERFCOUNTER3_SELECT__PERF_SEL1__SHIFT                                                            0xa
#define GL2C_PERFCOUNTER3_SELECT__CNTR_MODE__SHIFT                                                            0x14
#define GL2C_PERFCOUNTER3_SELECT__PERF_MODE1__SHIFT                                                           0x18
#define GL2C_PERFCOUNTER3_SELECT__PERF_MODE__SHIFT                                                            0x1c
#define GL2C_PERFCOUNTER3_SELECT__PERF_SEL_MASK                                                               0x000003FFL
#define GL2C_PERFCOUNTER3_SELECT__PERF_SEL1_MASK                                                              0x000FFC00L
#define GL2C_PERFCOUNTER3_SELECT__CNTR_MODE_MASK                                                              0x00F00000L
#define GL2C_PERFCOUNTER3_SELECT__PERF_MODE1_MASK                                                             0x0F000000L
#define GL2C_PERFCOUNTER3_SELECT__PERF_MODE_MASK                                                              0xF0000000L
//GL2C_PERFCOUNTER3_SELECT1
#define GL2C_PERFCOUNTER3_SELECT1__PERF_SEL2__SHIFT                                                           0x0
#define GL2C_PERFCOUNTER3_SELECT1__PERF_SEL3__SHIFT                                                           0xa
#define GL2C_PERFCOUNTER3_SELECT1__PERF_MODE3__SHIFT                                                          0x18
#define GL2C_PERFCOUNTER3_SELECT1__PERF_MODE2__SHIFT                                                          0x1c
#define GL2C_PERFCOUNTER3_SELECT1__PERF_SEL2_MASK                                                             0x000003FFL
#define GL2C_PERFCOUNTER3_SELECT1__PERF_SEL3_MASK                                                             0x000FFC00L
#define GL2C_PERFCOUNTER3_SELECT1__PERF_MODE3_MASK                                                            0x0F000000L
#define GL2C_PERFCOUNTER3_SELECT1__PERF_MODE2_MASK                                                            0xF0000000L
//GL2A_PERFCOUNTER0_SELECT
#define GL2A_PERFCOUNTER0_SELECT__PERF_SEL__SHIFT                                                             0x0
#define GL2A_PERFCOUNTER0_SELECT__PERF_SEL1__SHIFT                                                            0xa
#define GL2A_PERFCOUNTER0_SELECT__CNTR_MODE__SHIFT                                                            0x14
#define GL2A_PERFCOUNTER0_SELECT__PERF_MODE1__SHIFT                                                           0x18
#define GL2A_PERFCOUNTER0_SELECT__PERF_MODE__SHIFT                                                            0x1c
#define GL2A_PERFCOUNTER0_SELECT__PERF_SEL_MASK                                                               0x000003FFL
#define GL2A_PERFCOUNTER0_SELECT__PERF_SEL1_MASK                                                              0x000FFC00L
#define GL2A_PERFCOUNTER0_SELECT__CNTR_MODE_MASK                                                              0x00F00000L
#define GL2A_PERFCOUNTER0_SELECT__PERF_MODE1_MASK                                                             0x0F000000L
#define GL2A_PERFCOUNTER0_SELECT__PERF_MODE_MASK                                                              0xF0000000L
//GL2A_PERFCOUNTER0_SELECT1
#define GL2A_PERFCOUNTER0_SELECT1__PERF_SEL2__SHIFT                                                           0x0
#define GL2A_PERFCOUNTER0_SELECT1__PERF_SEL3__SHIFT                                                           0xa
#define GL2A_PERFCOUNTER0_SELECT1__PERF_MODE3__SHIFT                                                          0x18
#define GL2A_PERFCOUNTER0_SELECT1__PERF_MODE2__SHIFT                                                          0x1c
#define GL2A_PERFCOUNTER0_SELECT1__PERF_SEL2_MASK                                                             0x000003FFL
#define GL2A_PERFCOUNTER0_SELECT1__PERF_SEL3_MASK                                                             0x000FFC00L
#define GL2A_PERFCOUNTER0_SELECT1__PERF_MODE3_MASK                                                            0x0F000000L
#define GL2A_PERFCOUNTER0_SELECT1__PERF_MODE2_MASK                                                            0xF0000000L
//GL2A_PERFCOUNTER1_SELECT
#define GL2A_PERFCOUNTER1_SELECT__PERF_SEL__SHIFT                                                             0x0
#define GL2A_PERFCOUNTER1_SELECT__PERF_SEL1__SHIFT                                                            0xa
#define GL2A_PERFCOUNTER1_SELECT__CNTR_MODE__SHIFT                                                            0x14
#define GL2A_PERFCOUNTER1_SELECT__PERF_MODE1__SHIFT                                                           0x18
#define GL2A_PERFCOUNTER1_SELECT__PERF_MODE__SHIFT                                                            0x1c
#define GL2A_PERFCOUNTER1_SELECT__PERF_SEL_MASK                                                               0x000003FFL
#define GL2A_PERFCOUNTER1_SELECT__PERF_SEL1_MASK                                                              0x000FFC00L
#define GL2A_PERFCOUNTER1_SELECT__CNTR_MODE_MASK                                                              0x00F00000L
#define GL2A_PERFCOUNTER1_SELECT__PERF_MODE1_MASK                                                             0x0F000000L
#define GL2A_PERFCOUNTER1_SELECT__PERF_MODE_MASK                                                              0xF0000000L
//GL2A_PERFCOUNTER1_SELECT1
#define GL2A_PERFCOUNTER1_SELECT1__PERF_SEL2__SHIFT                                                           0x0
#define GL2A_PERFCOUNTER1_SELECT1__PERF_SEL3__SHIFT                                                           0xa
#define GL2A_PERFCOUNTER1_SELECT1__PERF_MODE3__SHIFT                                                          0x18
#define GL2A_PERFCOUNTER1_SELECT1__PERF_MODE2__SHIFT                                                          0x1c
#define GL2A_PERFCOUNTER1_SELECT1__PERF_SEL2_MASK                                                             0x000003FFL
#define GL2A_PERFCOUNTER1_SELECT1__PERF_SEL3_MASK                                                             0x000FFC00L
#define GL2A_PERFCOUNTER1_SELECT1__PERF_MODE3_MASK                                                            0x0F000000L
#define GL2A_PERFCOUNTER1_SELECT1__PERF_MODE2_MASK                                                            0xF0000000L
//GL2A_PERFCOUNTER2_SELECT
#define GL2A_PERFCOUNTER2_SELECT__PERF_SEL__SHIFT                                                             0x0
#define GL2A_PERFCOUNTER2_SELECT__PERF_SEL1__SHIFT                                                            0xa
#define GL2A_PERFCOUNTER2_SELECT__CNTR_MODE__SHIFT                                                            0x14
#define GL2A_PERFCOUNTER2_SELECT__PERF_MODE1__SHIFT                                                           0x18
#define GL2A_PERFCOUNTER2_SELECT__PERF_MODE__SHIFT                                                            0x1c
#define GL2A_PERFCOUNTER2_SELECT__PERF_SEL_MASK                                                               0x000003FFL
#define GL2A_PERFCOUNTER2_SELECT__PERF_SEL1_MASK                                                              0x000FFC00L
#define GL2A_PERFCOUNTER2_SELECT__CNTR_MODE_MASK                                                              0x00F00000L
#define GL2A_PERFCOUNTER2_SELECT__PERF_MODE1_MASK                                                             0x0F000000L
#define GL2A_PERFCOUNTER2_SELECT__PERF_MODE_MASK                                                              0xF0000000L
//GL2A_PERFCOUNTER2_SELECT1
#define GL2A_PERFCOUNTER2_SELECT1__PERF_SEL2__SHIFT                                                           0x0
#define GL2A_PERFCOUNTER2_SELECT1__PERF_SEL3__SHIFT                                                           0xa
#define GL2A_PERFCOUNTER2_SELECT1__PERF_MODE3__SHIFT                                                          0x18
#define GL2A_PERFCOUNTER2_SELECT1__PERF_MODE2__SHIFT                                                          0x1c
#define GL2A_PERFCOUNTER2_SELECT1__PERF_SEL2_MASK                                                             0x000003FFL
#define GL2A_PERFCOUNTER2_SELECT1__PERF_SEL3_MASK                                                             0x000FFC00L
#define GL2A_PERFCOUNTER2_SELECT1__PERF_MODE3_MASK                                                            0x0F000000L
#define GL2A_PERFCOUNTER2_SELECT1__PERF_MODE2_MASK                                                            0xF0000000L
//GL2A_PERFCOUNTER3_SELECT
#define GL2A_PERFCOUNTER3_SELECT__PERF_SEL__SHIFT                                                             0x0
#define GL2A_PERFCOUNTER3_SELECT__PERF_SEL1__SHIFT                                                            0xa
#define GL2A_PERFCOUNTER3_SELECT__CNTR_MODE__SHIFT                                                            0x14
#define GL2A_PERFCOUNTER3_SELECT__PERF_MODE1__SHIFT                                                           0x18
#define GL2A_PERFCOUNTER3_SELECT__PERF_MODE__SHIFT                                                            0x1c
#define GL2A_PERFCOUNTER3_SELECT__PERF_SEL_MASK                                                               0x000003FFL
#define GL2A_PERFCOUNTER3_SELECT__PERF_SEL1_MASK                                                              0x000FFC00L
#define GL2A_PERFCOUNTER3_SELECT__CNTR_MODE_MASK                                                              0x00F00000L
#define GL2A_PERFCOUNTER3_SELECT__PERF_MODE1_MASK                                                             0x0F000000L
#define GL2A_PERFCOUNTER3_SELECT__PERF_MODE_MASK                                                              0xF0000000L
//GL2A_PERFCOUNTER3_SELECT1
#define GL2A_PERFCOUNTER3_SELECT1__PERF_SEL2__SHIFT                                                           0x0
#define GL2A_PERFCOUNTER3_SELECT1__PERF_SEL3__SHIFT                                                           0xa
#define GL2A_PERFCOUNTER3_SELECT1__PERF_MODE3__SHIFT                                                          0x18
#define GL2A_PERFCOUNTER3_SELECT1__PERF_MODE2__SHIFT                                                          0x1c
#define GL2A_PERFCOUNTER3_SELECT1__PERF_SEL2_MASK                                                             0x000003FFL
#define GL2A_PERFCOUNTER3_SELECT1__PERF_SEL3_MASK                                                             0x000FFC00L
#define GL2A_PERFCOUNTER3_SELECT1__PERF_MODE3_MASK                                                            0x0F000000L
#define GL2A_PERFCOUNTER3_SELECT1__PERF_MODE2_MASK                                                            0xF0000000L


// addressBlock: aigc_gfx_gcea_se_gfx_se_perfsdec
//GC_EA_SE_PERFCOUNTER0_SELECT
#define GC_EA_SE_PERFCOUNTER0_SELECT__PERF_SEL__SHIFT                                                         0x0
#define GC_EA_SE_PERFCOUNTER0_SELECT__PERF_SEL1__SHIFT                                                        0xa
#define GC_EA_SE_PERFCOUNTER0_SELECT__CNTR_MODE__SHIFT                                                        0x14
#define GC_EA_SE_PERFCOUNTER0_SELECT__PERF_MODE1__SHIFT                                                       0x18
#define GC_EA_SE_PERFCOUNTER0_SELECT__PERF_MODE__SHIFT                                                        0x1c
#define GC_EA_SE_PERFCOUNTER0_SELECT__PERF_SEL_MASK                                                           0x000003FFL
#define GC_EA_SE_PERFCOUNTER0_SELECT__PERF_SEL1_MASK                                                          0x000FFC00L
#define GC_EA_SE_PERFCOUNTER0_SELECT__CNTR_MODE_MASK                                                          0x00F00000L
#define GC_EA_SE_PERFCOUNTER0_SELECT__PERF_MODE1_MASK                                                         0x0F000000L
#define GC_EA_SE_PERFCOUNTER0_SELECT__PERF_MODE_MASK                                                          0xF0000000L
//GC_EA_SE_PERFCOUNTER0_SELECT1
#define GC_EA_SE_PERFCOUNTER0_SELECT1__PERF_SEL2__SHIFT                                                       0x0
#define GC_EA_SE_PERFCOUNTER0_SELECT1__PERF_SEL3__SHIFT                                                       0xa
#define GC_EA_SE_PERFCOUNTER0_SELECT1__PERF_MODE3__SHIFT                                                      0x18
#define GC_EA_SE_PERFCOUNTER0_SELECT1__PERF_MODE2__SHIFT                                                      0x1c
#define GC_EA_SE_PERFCOUNTER0_SELECT1__PERF_SEL2_MASK                                                         0x000003FFL
#define GC_EA_SE_PERFCOUNTER0_SELECT1__PERF_SEL3_MASK                                                         0x000FFC00L
#define GC_EA_SE_PERFCOUNTER0_SELECT1__PERF_MODE3_MASK                                                        0x0F000000L
#define GC_EA_SE_PERFCOUNTER0_SELECT1__PERF_MODE2_MASK                                                        0xF0000000L
//GC_EA_SE_PERFCOUNTER1_SELECT
#define GC_EA_SE_PERFCOUNTER1_SELECT__PERF_SEL__SHIFT                                                         0x0
#define GC_EA_SE_PERFCOUNTER1_SELECT__COUNTER_MODE__SHIFT                                                     0x1c
#define GC_EA_SE_PERFCOUNTER1_SELECT__PERF_SEL_MASK                                                           0x000003FFL
#define GC_EA_SE_PERFCOUNTER1_SELECT__COUNTER_MODE_MASK                                                       0xF0000000L


// addressBlock: aigc_gfx_gcea_se_gfx_se_perfddec
//GC_EA_SE_PERFCOUNTER0_LO
#define GC_EA_SE_PERFCOUNTER0_LO__PERFCOUNTER_LO__SHIFT                                                       0x0
#define GC_EA_SE_PERFCOUNTER0_LO__PERFCOUNTER_LO_MASK                                                         0xFFFFFFFFL
//GC_EA_SE_PERFCOUNTER0_HI
#define GC_EA_SE_PERFCOUNTER0_HI__PERFCOUNTER_HI__SHIFT                                                       0x0
#define GC_EA_SE_PERFCOUNTER0_HI__PERFCOUNTER_HI_MASK                                                         0xFFFFFFFFL
//GC_EA_SE_PERFCOUNTER1_LO
#define GC_EA_SE_PERFCOUNTER1_LO__PERFCOUNTER_LO__SHIFT                                                       0x0
#define GC_EA_SE_PERFCOUNTER1_LO__PERFCOUNTER_LO_MASK                                                         0xFFFFFFFFL
//GC_EA_SE_PERFCOUNTER1_HI
#define GC_EA_SE_PERFCOUNTER1_HI__PERFCOUNTER_HI__SHIFT                                                       0x0
#define GC_EA_SE_PERFCOUNTER1_HI__PERFCOUNTER_HI_MASK                                                         0xFFFFFFFFL

#endif
