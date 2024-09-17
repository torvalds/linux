/*
 * Copyright (C) 2020  Advanced Micro Devices, Inc.
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
#ifndef _gc_9_4_1_SH_MASK_HEADER
#define _gc_9_4_1_SH_MASK_HEADER

// addressBlock: gc_cppdec2
//CPF_EDC_TAG_CNT
#define CPF_EDC_TAG_CNT__DED_COUNT__SHIFT                                                                     0x0
#define CPF_EDC_TAG_CNT__SEC_COUNT__SHIFT                                                                     0x2
#define CPF_EDC_TAG_CNT__DED_COUNT_MASK                                                                       0x00000003L
#define CPF_EDC_TAG_CNT__SEC_COUNT_MASK                                                                       0x0000000CL
//CPF_EDC_ROQ_CNT
#define CPF_EDC_ROQ_CNT__DED_COUNT_ME1__SHIFT                                                                 0x0
#define CPF_EDC_ROQ_CNT__SEC_COUNT_ME1__SHIFT                                                                 0x2
#define CPF_EDC_ROQ_CNT__DED_COUNT_ME2__SHIFT                                                                 0x4
#define CPF_EDC_ROQ_CNT__SEC_COUNT_ME2__SHIFT                                                                 0x6
#define CPF_EDC_ROQ_CNT__DED_COUNT_ME1_MASK                                                                   0x00000003L
#define CPF_EDC_ROQ_CNT__SEC_COUNT_ME1_MASK                                                                   0x0000000CL
#define CPF_EDC_ROQ_CNT__DED_COUNT_ME2_MASK                                                                   0x00000030L
#define CPF_EDC_ROQ_CNT__SEC_COUNT_ME2_MASK                                                                   0x000000C0L
//CPG_EDC_TAG_CNT
#define CPG_EDC_TAG_CNT__DED_COUNT__SHIFT                                                                     0x0
#define CPG_EDC_TAG_CNT__SEC_COUNT__SHIFT                                                                     0x2
#define CPG_EDC_TAG_CNT__DED_COUNT_MASK                                                                       0x00000003L
#define CPG_EDC_TAG_CNT__SEC_COUNT_MASK                                                                       0x0000000CL
//CPG_EDC_DMA_CNT
#define CPG_EDC_DMA_CNT__ROQ_DED_COUNT__SHIFT                                                                 0x0
#define CPG_EDC_DMA_CNT__ROQ_SEC_COUNT__SHIFT                                                                 0x2
#define CPG_EDC_DMA_CNT__TAG_DED_COUNT__SHIFT                                                                 0x4
#define CPG_EDC_DMA_CNT__TAG_SEC_COUNT__SHIFT                                                                 0x6
#define CPG_EDC_DMA_CNT__ROQ_DED_COUNT_MASK                                                                   0x00000003L
#define CPG_EDC_DMA_CNT__ROQ_SEC_COUNT_MASK                                                                   0x0000000CL
#define CPG_EDC_DMA_CNT__TAG_DED_COUNT_MASK                                                                   0x00000030L
#define CPG_EDC_DMA_CNT__TAG_SEC_COUNT_MASK                                                                   0x000000C0L
//CPC_EDC_SCRATCH_CNT
#define CPC_EDC_SCRATCH_CNT__DED_COUNT__SHIFT                                                                 0x0
#define CPC_EDC_SCRATCH_CNT__SEC_COUNT__SHIFT                                                                 0x2
#define CPC_EDC_SCRATCH_CNT__DED_COUNT_MASK                                                                   0x00000003L
#define CPC_EDC_SCRATCH_CNT__SEC_COUNT_MASK                                                                   0x0000000CL
//CPC_EDC_UCODE_CNT
#define CPC_EDC_UCODE_CNT__DED_COUNT__SHIFT                                                                   0x0
#define CPC_EDC_UCODE_CNT__SEC_COUNT__SHIFT                                                                   0x2
#define CPC_EDC_UCODE_CNT__DED_COUNT_MASK                                                                     0x00000003L
#define CPC_EDC_UCODE_CNT__SEC_COUNT_MASK                                                                     0x0000000CL
//DC_EDC_STATE_CNT
#define DC_EDC_STATE_CNT__DED_COUNT_ME1__SHIFT                                                                0x0
#define DC_EDC_STATE_CNT__SEC_COUNT_ME1__SHIFT                                                                0x2
#define DC_EDC_STATE_CNT__DED_COUNT_ME1_MASK                                                                  0x00000003L
#define DC_EDC_STATE_CNT__SEC_COUNT_ME1_MASK                                                                  0x0000000CL
//DC_EDC_CSINVOC_CNT
#define DC_EDC_CSINVOC_CNT__DED_COUNT_ME1__SHIFT                                                              0x0
#define DC_EDC_CSINVOC_CNT__SEC_COUNT_ME1__SHIFT                                                              0x2
#define DC_EDC_CSINVOC_CNT__DED_COUNT1_ME1__SHIFT                                                             0x4
#define DC_EDC_CSINVOC_CNT__SEC_COUNT1_ME1__SHIFT                                                             0x6
#define DC_EDC_CSINVOC_CNT__DED_COUNT_ME1_MASK                                                                0x00000003L
#define DC_EDC_CSINVOC_CNT__SEC_COUNT_ME1_MASK                                                                0x0000000CL
#define DC_EDC_CSINVOC_CNT__DED_COUNT1_ME1_MASK                                                               0x00000030L
#define DC_EDC_CSINVOC_CNT__SEC_COUNT1_ME1_MASK                                                               0x000000C0L
//DC_EDC_RESTORE_CNT
#define DC_EDC_RESTORE_CNT__DED_COUNT_ME1__SHIFT                                                              0x0
#define DC_EDC_RESTORE_CNT__SEC_COUNT_ME1__SHIFT                                                              0x2
#define DC_EDC_RESTORE_CNT__DED_COUNT1_ME1__SHIFT                                                             0x4
#define DC_EDC_RESTORE_CNT__SEC_COUNT1_ME1__SHIFT                                                             0x6
#define DC_EDC_RESTORE_CNT__DED_COUNT_ME1_MASK                                                                0x00000003L
#define DC_EDC_RESTORE_CNT__SEC_COUNT_ME1_MASK                                                                0x0000000CL
#define DC_EDC_RESTORE_CNT__DED_COUNT1_ME1_MASK                                                               0x00000030L
#define DC_EDC_RESTORE_CNT__SEC_COUNT1_ME1_MASK                                                               0x000000C0L

// addressBlock: gc_gdsdec
//GDS_EDC_CNT
#define GDS_EDC_CNT__GDS_MEM_DED__SHIFT                                                                       0x0
#define GDS_EDC_CNT__GDS_MEM_SEC__SHIFT                                                                       0x4
#define GDS_EDC_CNT__UNUSED__SHIFT                                                                            0x6
#define GDS_EDC_CNT__GDS_MEM_DED_MASK                                                                         0x00000003L
#define GDS_EDC_CNT__GDS_MEM_SEC_MASK                                                                         0x00000030L
#define GDS_EDC_CNT__UNUSED_MASK                                                                              0xFFFFFFC0L
//GDS_EDC_GRBM_CNT
#define GDS_EDC_GRBM_CNT__DED__SHIFT                                                                          0x0
#define GDS_EDC_GRBM_CNT__SEC__SHIFT                                                                          0x2
#define GDS_EDC_GRBM_CNT__UNUSED__SHIFT                                                                       0x4
#define GDS_EDC_GRBM_CNT__DED_MASK                                                                            0x00000003L
#define GDS_EDC_GRBM_CNT__SEC_MASK                                                                            0x0000000CL
#define GDS_EDC_GRBM_CNT__UNUSED_MASK                                                                         0xFFFFFFF0L
//GDS_EDC_OA_DED
#define GDS_EDC_OA_DED__ME0_GFXHP3D_PIX_DED__SHIFT                                                            0x0
#define GDS_EDC_OA_DED__ME0_GFXHP3D_VTX_DED__SHIFT                                                            0x1
#define GDS_EDC_OA_DED__ME0_CS_DED__SHIFT                                                                     0x2
#define GDS_EDC_OA_DED__ME0_GFXHP3D_GS_DED__SHIFT                                                             0x3
#define GDS_EDC_OA_DED__ME1_PIPE0_DED__SHIFT                                                                  0x4
#define GDS_EDC_OA_DED__ME1_PIPE1_DED__SHIFT                                                                  0x5
#define GDS_EDC_OA_DED__ME1_PIPE2_DED__SHIFT                                                                  0x6
#define GDS_EDC_OA_DED__ME1_PIPE3_DED__SHIFT                                                                  0x7
#define GDS_EDC_OA_DED__ME2_PIPE0_DED__SHIFT                                                                  0x8
#define GDS_EDC_OA_DED__ME2_PIPE1_DED__SHIFT                                                                  0x9
#define GDS_EDC_OA_DED__ME2_PIPE2_DED__SHIFT                                                                  0xa
#define GDS_EDC_OA_DED__ME2_PIPE3_DED__SHIFT                                                                  0xb
#define GDS_EDC_OA_DED__UNUSED1__SHIFT                                                                        0xc
#define GDS_EDC_OA_DED__ME0_GFXHP3D_PIX_DED_MASK                                                              0x00000001L
#define GDS_EDC_OA_DED__ME0_GFXHP3D_VTX_DED_MASK                                                              0x00000002L
#define GDS_EDC_OA_DED__ME0_CS_DED_MASK                                                                       0x00000004L
#define GDS_EDC_OA_DED__ME0_GFXHP3D_GS_DED_MASK                                                               0x00000008L
#define GDS_EDC_OA_DED__ME1_PIPE0_DED_MASK                                                                    0x00000010L
#define GDS_EDC_OA_DED__ME1_PIPE1_DED_MASK                                                                    0x00000020L
#define GDS_EDC_OA_DED__ME1_PIPE2_DED_MASK                                                                    0x00000040L
#define GDS_EDC_OA_DED__ME1_PIPE3_DED_MASK                                                                    0x00000080L
#define GDS_EDC_OA_DED__ME2_PIPE0_DED_MASK                                                                    0x00000100L
#define GDS_EDC_OA_DED__ME2_PIPE1_DED_MASK                                                                    0x00000200L
#define GDS_EDC_OA_DED__ME2_PIPE2_DED_MASK                                                                    0x00000400L
#define GDS_EDC_OA_DED__ME2_PIPE3_DED_MASK                                                                    0x00000800L
#define GDS_EDC_OA_DED__UNUSED1_MASK                                                                          0xFFFFF000L
//GDS_EDC_OA_PHY_CNT
#define GDS_EDC_OA_PHY_CNT__ME0_CS_PIPE_MEM_SEC__SHIFT                                                        0x0
#define GDS_EDC_OA_PHY_CNT__ME0_CS_PIPE_MEM_DED__SHIFT                                                        0x2
#define GDS_EDC_OA_PHY_CNT__PHY_CMD_RAM_MEM_SEC__SHIFT                                                        0x4
#define GDS_EDC_OA_PHY_CNT__PHY_CMD_RAM_MEM_DED__SHIFT                                                        0x6
#define GDS_EDC_OA_PHY_CNT__PHY_DATA_RAM_MEM_SEC__SHIFT                                                       0x8
#define GDS_EDC_OA_PHY_CNT__PHY_DATA_RAM_MEM_DED__SHIFT                                                       0xa
#define GDS_EDC_OA_PHY_CNT__UNUSED1__SHIFT                                                                    0xc
#define GDS_EDC_OA_PHY_CNT__ME0_CS_PIPE_MEM_SEC_MASK                                                          0x00000003L
#define GDS_EDC_OA_PHY_CNT__ME0_CS_PIPE_MEM_DED_MASK                                                          0x0000000CL
#define GDS_EDC_OA_PHY_CNT__PHY_CMD_RAM_MEM_SEC_MASK                                                          0x00000030L
#define GDS_EDC_OA_PHY_CNT__PHY_CMD_RAM_MEM_DED_MASK                                                          0x000000C0L
#define GDS_EDC_OA_PHY_CNT__PHY_DATA_RAM_MEM_SEC_MASK                                                         0x00000300L
#define GDS_EDC_OA_PHY_CNT__PHY_DATA_RAM_MEM_DED_MASK                                                         0x00000C00L
#define GDS_EDC_OA_PHY_CNT__UNUSED1_MASK                                                                      0xFFFFF000L
//GDS_EDC_OA_PIPE_CNT
#define GDS_EDC_OA_PIPE_CNT__ME1_PIPE0_PIPE_MEM_SEC__SHIFT                                                    0x0
#define GDS_EDC_OA_PIPE_CNT__ME1_PIPE0_PIPE_MEM_DED__SHIFT                                                    0x2
#define GDS_EDC_OA_PIPE_CNT__ME1_PIPE1_PIPE_MEM_SEC__SHIFT                                                    0x4
#define GDS_EDC_OA_PIPE_CNT__ME1_PIPE1_PIPE_MEM_DED__SHIFT                                                    0x6
#define GDS_EDC_OA_PIPE_CNT__ME1_PIPE2_PIPE_MEM_SEC__SHIFT                                                    0x8
#define GDS_EDC_OA_PIPE_CNT__ME1_PIPE2_PIPE_MEM_DED__SHIFT                                                    0xa
#define GDS_EDC_OA_PIPE_CNT__ME1_PIPE3_PIPE_MEM_SEC__SHIFT                                                    0xc
#define GDS_EDC_OA_PIPE_CNT__ME1_PIPE3_PIPE_MEM_DED__SHIFT                                                    0xe
#define GDS_EDC_OA_PIPE_CNT__UNUSED__SHIFT                                                                    0x10
#define GDS_EDC_OA_PIPE_CNT__ME1_PIPE0_PIPE_MEM_SEC_MASK                                                      0x00000003L
#define GDS_EDC_OA_PIPE_CNT__ME1_PIPE0_PIPE_MEM_DED_MASK                                                      0x0000000CL
#define GDS_EDC_OA_PIPE_CNT__ME1_PIPE1_PIPE_MEM_SEC_MASK                                                      0x00000030L
#define GDS_EDC_OA_PIPE_CNT__ME1_PIPE1_PIPE_MEM_DED_MASK                                                      0x000000C0L
#define GDS_EDC_OA_PIPE_CNT__ME1_PIPE2_PIPE_MEM_SEC_MASK                                                      0x00000300L
#define GDS_EDC_OA_PIPE_CNT__ME1_PIPE2_PIPE_MEM_DED_MASK                                                      0x00000C00L
#define GDS_EDC_OA_PIPE_CNT__ME1_PIPE3_PIPE_MEM_SEC_MASK                                                      0x00003000L
#define GDS_EDC_OA_PIPE_CNT__ME1_PIPE3_PIPE_MEM_DED_MASK                                                      0x0000C000L
#define GDS_EDC_OA_PIPE_CNT__UNUSED_MASK                                                                      0xFFFF0000L

// addressBlock: gc_shsdec
//SPI_EDC_CNT
#define SPI_EDC_CNT__SPI_SR_MEM_SEC_COUNT__SHIFT                                                              0x0
#define SPI_EDC_CNT__SPI_SR_MEM_DED_COUNT__SHIFT                                                              0x2
#define SPI_EDC_CNT__SPI_GDS_EXPREQ_SEC_COUNT__SHIFT                                                          0x4
#define SPI_EDC_CNT__SPI_GDS_EXPREQ_DED_COUNT__SHIFT                                                          0x6
#define SPI_EDC_CNT__SPI_WB_GRANT_30_SEC_COUNT__SHIFT                                                         0x8
#define SPI_EDC_CNT__SPI_WB_GRANT_30_DED_COUNT__SHIFT                                                         0xa
#define SPI_EDC_CNT__SPI_WB_GRANT_61_SEC_COUNT__SHIFT                                                         0xc
#define SPI_EDC_CNT__SPI_WB_GRANT_61_DED_COUNT__SHIFT                                                         0xe
#define SPI_EDC_CNT__SPI_LIFE_CNT_SEC_COUNT__SHIFT                                                            0x10
#define SPI_EDC_CNT__SPI_LIFE_CNT_DED_COUNT__SHIFT                                                            0x12
#define SPI_EDC_CNT__SPI_SR_MEM_SEC_COUNT_MASK                                                                0x00000003L
#define SPI_EDC_CNT__SPI_SR_MEM_DED_COUNT_MASK                                                                0x0000000CL
#define SPI_EDC_CNT__SPI_GDS_EXPREQ_SEC_COUNT_MASK                                                            0x00000030L
#define SPI_EDC_CNT__SPI_GDS_EXPREQ_DED_COUNT_MASK                                                            0x000000C0L
#define SPI_EDC_CNT__SPI_WB_GRANT_30_SEC_COUNT_MASK                                                           0x00000300L
#define SPI_EDC_CNT__SPI_WB_GRANT_30_DED_COUNT_MASK                                                           0x00000C00L
#define SPI_EDC_CNT__SPI_WB_GRANT_61_SEC_COUNT_MASK                                                           0x00003000L
#define SPI_EDC_CNT__SPI_WB_GRANT_61_DED_COUNT_MASK                                                           0x0000C000L
#define SPI_EDC_CNT__SPI_LIFE_CNT_SEC_COUNT_MASK                                                              0x00030000L
#define SPI_EDC_CNT__SPI_LIFE_CNT_DED_COUNT_MASK                                                              0x000C0000L

// addressBlock: gc_sqdec
//SQC_EDC_CNT2
#define SQC_EDC_CNT2__INST_BANKA_TAG_RAM_SEC_COUNT__SHIFT                                                     0x0
#define SQC_EDC_CNT2__INST_BANKA_TAG_RAM_DED_COUNT__SHIFT                                                     0x2
#define SQC_EDC_CNT2__INST_BANKA_BANK_RAM_SEC_COUNT__SHIFT                                                    0x4
#define SQC_EDC_CNT2__INST_BANKA_BANK_RAM_DED_COUNT__SHIFT                                                    0x6
#define SQC_EDC_CNT2__DATA_BANKA_TAG_RAM_SEC_COUNT__SHIFT                                                     0x8
#define SQC_EDC_CNT2__DATA_BANKA_TAG_RAM_DED_COUNT__SHIFT                                                     0xa
#define SQC_EDC_CNT2__DATA_BANKA_BANK_RAM_SEC_COUNT__SHIFT                                                    0xc
#define SQC_EDC_CNT2__DATA_BANKA_BANK_RAM_DED_COUNT__SHIFT                                                    0xe
#define SQC_EDC_CNT2__INST_UTCL1_LFIFO_SEC_COUNT__SHIFT                                                       0x10
#define SQC_EDC_CNT2__INST_UTCL1_LFIFO_DED_COUNT__SHIFT                                                       0x12
#define SQC_EDC_CNT2__INST_BANKA_TAG_RAM_SEC_COUNT_MASK                                                       0x00000003L
#define SQC_EDC_CNT2__INST_BANKA_TAG_RAM_DED_COUNT_MASK                                                       0x0000000CL
#define SQC_EDC_CNT2__INST_BANKA_BANK_RAM_SEC_COUNT_MASK                                                      0x00000030L
#define SQC_EDC_CNT2__INST_BANKA_BANK_RAM_DED_COUNT_MASK                                                      0x000000C0L
#define SQC_EDC_CNT2__DATA_BANKA_TAG_RAM_SEC_COUNT_MASK                                                       0x00000300L
#define SQC_EDC_CNT2__DATA_BANKA_TAG_RAM_DED_COUNT_MASK                                                       0x00000C00L
#define SQC_EDC_CNT2__DATA_BANKA_BANK_RAM_SEC_COUNT_MASK                                                      0x00003000L
#define SQC_EDC_CNT2__DATA_BANKA_BANK_RAM_DED_COUNT_MASK                                                      0x0000C000L
#define SQC_EDC_CNT2__INST_UTCL1_LFIFO_SEC_COUNT_MASK                                                         0x00030000L
#define SQC_EDC_CNT2__INST_UTCL1_LFIFO_DED_COUNT_MASK                                                         0x000C0000L
//SQC_EDC_CNT3
#define SQC_EDC_CNT3__INST_BANKB_TAG_RAM_SEC_COUNT__SHIFT                                                     0x0
#define SQC_EDC_CNT3__INST_BANKB_TAG_RAM_DED_COUNT__SHIFT                                                     0x2
#define SQC_EDC_CNT3__INST_BANKB_BANK_RAM_SEC_COUNT__SHIFT                                                    0x4
#define SQC_EDC_CNT3__INST_BANKB_BANK_RAM_DED_COUNT__SHIFT                                                    0x6
#define SQC_EDC_CNT3__DATA_BANKB_TAG_RAM_SEC_COUNT__SHIFT                                                     0x8
#define SQC_EDC_CNT3__DATA_BANKB_TAG_RAM_DED_COUNT__SHIFT                                                     0xa
#define SQC_EDC_CNT3__DATA_BANKB_BANK_RAM_SEC_COUNT__SHIFT                                                    0xc
#define SQC_EDC_CNT3__DATA_BANKB_BANK_RAM_DED_COUNT__SHIFT                                                    0xe
#define SQC_EDC_CNT3__INST_BANKB_TAG_RAM_SEC_COUNT_MASK                                                       0x00000003L
#define SQC_EDC_CNT3__INST_BANKB_TAG_RAM_DED_COUNT_MASK                                                       0x0000000CL
#define SQC_EDC_CNT3__INST_BANKB_BANK_RAM_SEC_COUNT_MASK                                                      0x00000030L
#define SQC_EDC_CNT3__INST_BANKB_BANK_RAM_DED_COUNT_MASK                                                      0x000000C0L
#define SQC_EDC_CNT3__DATA_BANKB_TAG_RAM_SEC_COUNT_MASK                                                       0x00000300L
#define SQC_EDC_CNT3__DATA_BANKB_TAG_RAM_DED_COUNT_MASK                                                       0x00000C00L
#define SQC_EDC_CNT3__DATA_BANKB_BANK_RAM_SEC_COUNT_MASK                                                      0x00003000L
#define SQC_EDC_CNT3__DATA_BANKB_BANK_RAM_DED_COUNT_MASK                                                      0x0000C000L
//SQC_EDC_PARITY_CNT3
#define SQC_EDC_PARITY_CNT3__INST_BANKA_UTCL1_MISS_FIFO_SEC_COUNT__SHIFT                                      0x0
#define SQC_EDC_PARITY_CNT3__INST_BANKA_UTCL1_MISS_FIFO_DED_COUNT__SHIFT                                      0x2
#define SQC_EDC_PARITY_CNT3__INST_BANKA_MISS_FIFO_SEC_COUNT__SHIFT                                            0x4
#define SQC_EDC_PARITY_CNT3__INST_BANKA_MISS_FIFO_DED_COUNT__SHIFT                                            0x6
#define SQC_EDC_PARITY_CNT3__DATA_BANKA_HIT_FIFO_SEC_COUNT__SHIFT                                             0x8
#define SQC_EDC_PARITY_CNT3__DATA_BANKA_HIT_FIFO_DED_COUNT__SHIFT                                             0xa
#define SQC_EDC_PARITY_CNT3__DATA_BANKA_MISS_FIFO_SEC_COUNT__SHIFT                                            0xc
#define SQC_EDC_PARITY_CNT3__DATA_BANKA_MISS_FIFO_DED_COUNT__SHIFT                                            0xe
#define SQC_EDC_PARITY_CNT3__INST_BANKB_UTCL1_MISS_FIFO_SEC_COUNT__SHIFT                                      0x10
#define SQC_EDC_PARITY_CNT3__INST_BANKB_UTCL1_MISS_FIFO_DED_COUNT__SHIFT                                      0x12
#define SQC_EDC_PARITY_CNT3__INST_BANKB_MISS_FIFO_SEC_COUNT__SHIFT                                            0x14
#define SQC_EDC_PARITY_CNT3__INST_BANKB_MISS_FIFO_DED_COUNT__SHIFT                                            0x16
#define SQC_EDC_PARITY_CNT3__DATA_BANKB_HIT_FIFO_SEC_COUNT__SHIFT                                             0x18
#define SQC_EDC_PARITY_CNT3__DATA_BANKB_HIT_FIFO_DED_COUNT__SHIFT                                             0x1a
#define SQC_EDC_PARITY_CNT3__DATA_BANKB_MISS_FIFO_SEC_COUNT__SHIFT                                            0x1c
#define SQC_EDC_PARITY_CNT3__DATA_BANKB_MISS_FIFO_DED_COUNT__SHIFT                                            0x1e
#define SQC_EDC_PARITY_CNT3__INST_BANKA_UTCL1_MISS_FIFO_SEC_COUNT_MASK                                        0x00000003L
#define SQC_EDC_PARITY_CNT3__INST_BANKA_UTCL1_MISS_FIFO_DED_COUNT_MASK                                        0x0000000CL
#define SQC_EDC_PARITY_CNT3__INST_BANKA_MISS_FIFO_SEC_COUNT_MASK                                              0x00000030L
#define SQC_EDC_PARITY_CNT3__INST_BANKA_MISS_FIFO_DED_COUNT_MASK                                              0x000000C0L
#define SQC_EDC_PARITY_CNT3__DATA_BANKA_HIT_FIFO_SEC_COUNT_MASK                                               0x00000300L
#define SQC_EDC_PARITY_CNT3__DATA_BANKA_HIT_FIFO_DED_COUNT_MASK                                               0x00000C00L
#define SQC_EDC_PARITY_CNT3__DATA_BANKA_MISS_FIFO_SEC_COUNT_MASK                                              0x00003000L
#define SQC_EDC_PARITY_CNT3__DATA_BANKA_MISS_FIFO_DED_COUNT_MASK                                              0x0000C000L
#define SQC_EDC_PARITY_CNT3__INST_BANKB_UTCL1_MISS_FIFO_SEC_COUNT_MASK                                        0x00030000L
#define SQC_EDC_PARITY_CNT3__INST_BANKB_UTCL1_MISS_FIFO_DED_COUNT_MASK                                        0x000C0000L
#define SQC_EDC_PARITY_CNT3__INST_BANKB_MISS_FIFO_SEC_COUNT_MASK                                              0x00300000L
#define SQC_EDC_PARITY_CNT3__INST_BANKB_MISS_FIFO_DED_COUNT_MASK                                              0x00C00000L
#define SQC_EDC_PARITY_CNT3__DATA_BANKB_HIT_FIFO_SEC_COUNT_MASK                                               0x03000000L
#define SQC_EDC_PARITY_CNT3__DATA_BANKB_HIT_FIFO_DED_COUNT_MASK                                               0x0C000000L
#define SQC_EDC_PARITY_CNT3__DATA_BANKB_MISS_FIFO_SEC_COUNT_MASK                                              0x30000000L
#define SQC_EDC_PARITY_CNT3__DATA_BANKB_MISS_FIFO_DED_COUNT_MASK                                              0xC0000000L
//SQC_EDC_CNT
#define SQC_EDC_CNT__DATA_CU0_WRITE_DATA_BUF_SEC_COUNT__SHIFT                                                 0x0
#define SQC_EDC_CNT__DATA_CU0_WRITE_DATA_BUF_DED_COUNT__SHIFT                                                 0x2
#define SQC_EDC_CNT__DATA_CU0_UTCL1_LFIFO_SEC_COUNT__SHIFT                                                    0x4
#define SQC_EDC_CNT__DATA_CU0_UTCL1_LFIFO_DED_COUNT__SHIFT                                                    0x6
#define SQC_EDC_CNT__DATA_CU1_WRITE_DATA_BUF_SEC_COUNT__SHIFT                                                 0x8
#define SQC_EDC_CNT__DATA_CU1_WRITE_DATA_BUF_DED_COUNT__SHIFT                                                 0xa
#define SQC_EDC_CNT__DATA_CU1_UTCL1_LFIFO_SEC_COUNT__SHIFT                                                    0xc
#define SQC_EDC_CNT__DATA_CU1_UTCL1_LFIFO_DED_COUNT__SHIFT                                                    0xe
#define SQC_EDC_CNT__DATA_CU2_WRITE_DATA_BUF_SEC_COUNT__SHIFT                                                 0x10
#define SQC_EDC_CNT__DATA_CU2_WRITE_DATA_BUF_DED_COUNT__SHIFT                                                 0x12
#define SQC_EDC_CNT__DATA_CU2_UTCL1_LFIFO_SEC_COUNT__SHIFT                                                    0x14
#define SQC_EDC_CNT__DATA_CU2_UTCL1_LFIFO_DED_COUNT__SHIFT                                                    0x16
#define SQC_EDC_CNT__DATA_CU3_WRITE_DATA_BUF_SEC_COUNT__SHIFT                                                 0x18
#define SQC_EDC_CNT__DATA_CU3_WRITE_DATA_BUF_DED_COUNT__SHIFT                                                 0x1a
#define SQC_EDC_CNT__DATA_CU3_UTCL1_LFIFO_SEC_COUNT__SHIFT                                                    0x1c
#define SQC_EDC_CNT__DATA_CU3_UTCL1_LFIFO_DED_COUNT__SHIFT                                                    0x1e
#define SQC_EDC_CNT__DATA_CU0_WRITE_DATA_BUF_SEC_COUNT_MASK                                                   0x00000003L
#define SQC_EDC_CNT__DATA_CU0_WRITE_DATA_BUF_DED_COUNT_MASK                                                   0x0000000CL
#define SQC_EDC_CNT__DATA_CU0_UTCL1_LFIFO_SEC_COUNT_MASK                                                      0x00000030L
#define SQC_EDC_CNT__DATA_CU0_UTCL1_LFIFO_DED_COUNT_MASK                                                      0x000000C0L
#define SQC_EDC_CNT__DATA_CU1_WRITE_DATA_BUF_SEC_COUNT_MASK                                                   0x00000300L
#define SQC_EDC_CNT__DATA_CU1_WRITE_DATA_BUF_DED_COUNT_MASK                                                   0x00000C00L
#define SQC_EDC_CNT__DATA_CU1_UTCL1_LFIFO_SEC_COUNT_MASK                                                      0x00003000L
#define SQC_EDC_CNT__DATA_CU1_UTCL1_LFIFO_DED_COUNT_MASK                                                      0x0000C000L
#define SQC_EDC_CNT__DATA_CU2_WRITE_DATA_BUF_SEC_COUNT_MASK                                                   0x00030000L
#define SQC_EDC_CNT__DATA_CU2_WRITE_DATA_BUF_DED_COUNT_MASK                                                   0x000C0000L
#define SQC_EDC_CNT__DATA_CU2_UTCL1_LFIFO_SEC_COUNT_MASK                                                      0x00300000L
#define SQC_EDC_CNT__DATA_CU2_UTCL1_LFIFO_DED_COUNT_MASK                                                      0x00C00000L
#define SQC_EDC_CNT__DATA_CU3_WRITE_DATA_BUF_SEC_COUNT_MASK                                                   0x03000000L
#define SQC_EDC_CNT__DATA_CU3_WRITE_DATA_BUF_DED_COUNT_MASK                                                   0x0C000000L
#define SQC_EDC_CNT__DATA_CU3_UTCL1_LFIFO_SEC_COUNT_MASK                                                      0x30000000L
#define SQC_EDC_CNT__DATA_CU3_UTCL1_LFIFO_DED_COUNT_MASK                                                      0xC0000000L
//SQ_EDC_SEC_CNT
#define SQ_EDC_SEC_CNT__LDS_SEC__SHIFT                                                                        0x0
#define SQ_EDC_SEC_CNT__SGPR_SEC__SHIFT                                                                       0x8
#define SQ_EDC_SEC_CNT__VGPR_SEC__SHIFT                                                                       0x10
#define SQ_EDC_SEC_CNT__LDS_SEC_MASK                                                                          0x000000FFL
#define SQ_EDC_SEC_CNT__SGPR_SEC_MASK                                                                         0x0000FF00L
#define SQ_EDC_SEC_CNT__VGPR_SEC_MASK                                                                         0x00FF0000L
//SQ_EDC_DED_CNT
#define SQ_EDC_DED_CNT__LDS_DED__SHIFT                                                                        0x0
#define SQ_EDC_DED_CNT__SGPR_DED__SHIFT                                                                       0x8
#define SQ_EDC_DED_CNT__VGPR_DED__SHIFT                                                                       0x10
#define SQ_EDC_DED_CNT__LDS_DED_MASK                                                                          0x000000FFL
#define SQ_EDC_DED_CNT__SGPR_DED_MASK                                                                         0x0000FF00L
#define SQ_EDC_DED_CNT__VGPR_DED_MASK                                                                         0x00FF0000L
//SQ_EDC_INFO
#define SQ_EDC_INFO__WAVE_ID__SHIFT                                                                           0x0
#define SQ_EDC_INFO__SIMD_ID__SHIFT                                                                           0x4
#define SQ_EDC_INFO__SOURCE__SHIFT                                                                            0x6
#define SQ_EDC_INFO__VM_ID__SHIFT                                                                             0x9
#define SQ_EDC_INFO__WAVE_ID_MASK                                                                             0x0000000FL
#define SQ_EDC_INFO__SIMD_ID_MASK                                                                             0x00000030L
#define SQ_EDC_INFO__SOURCE_MASK                                                                              0x000001C0L
#define SQ_EDC_INFO__VM_ID_MASK                                                                               0x00001E00L
//SQ_EDC_CNT
#define SQ_EDC_CNT__LDS_D_SEC_COUNT__SHIFT                                                                    0x0
#define SQ_EDC_CNT__LDS_D_DED_COUNT__SHIFT                                                                    0x2
#define SQ_EDC_CNT__LDS_I_SEC_COUNT__SHIFT                                                                    0x4
#define SQ_EDC_CNT__LDS_I_DED_COUNT__SHIFT                                                                    0x6
#define SQ_EDC_CNT__SGPR_SEC_COUNT__SHIFT                                                                     0x8
#define SQ_EDC_CNT__SGPR_DED_COUNT__SHIFT                                                                     0xa
#define SQ_EDC_CNT__VGPR0_SEC_COUNT__SHIFT                                                                    0xc
#define SQ_EDC_CNT__VGPR0_DED_COUNT__SHIFT                                                                    0xe
#define SQ_EDC_CNT__VGPR1_SEC_COUNT__SHIFT                                                                    0x10
#define SQ_EDC_CNT__VGPR1_DED_COUNT__SHIFT                                                                    0x12
#define SQ_EDC_CNT__VGPR2_SEC_COUNT__SHIFT                                                                    0x14
#define SQ_EDC_CNT__VGPR2_DED_COUNT__SHIFT                                                                    0x16
#define SQ_EDC_CNT__VGPR3_SEC_COUNT__SHIFT                                                                    0x18
#define SQ_EDC_CNT__VGPR3_DED_COUNT__SHIFT                                                                    0x1a
#define SQ_EDC_CNT__LDS_D_SEC_COUNT_MASK                                                                      0x00000003L
#define SQ_EDC_CNT__LDS_D_DED_COUNT_MASK                                                                      0x0000000CL
#define SQ_EDC_CNT__LDS_I_SEC_COUNT_MASK                                                                      0x00000030L
#define SQ_EDC_CNT__LDS_I_DED_COUNT_MASK                                                                      0x000000C0L
#define SQ_EDC_CNT__SGPR_SEC_COUNT_MASK                                                                       0x00000300L
#define SQ_EDC_CNT__SGPR_DED_COUNT_MASK                                                                       0x00000C00L
#define SQ_EDC_CNT__VGPR0_SEC_COUNT_MASK                                                                      0x00003000L
#define SQ_EDC_CNT__VGPR0_DED_COUNT_MASK                                                                      0x0000C000L
#define SQ_EDC_CNT__VGPR1_SEC_COUNT_MASK                                                                      0x00030000L
#define SQ_EDC_CNT__VGPR1_DED_COUNT_MASK                                                                      0x000C0000L
#define SQ_EDC_CNT__VGPR2_SEC_COUNT_MASK                                                                      0x00300000L
#define SQ_EDC_CNT__VGPR2_DED_COUNT_MASK                                                                      0x00C00000L
#define SQ_EDC_CNT__VGPR3_SEC_COUNT_MASK                                                                      0x03000000L
#define SQ_EDC_CNT__VGPR3_DED_COUNT_MASK                                                                      0x0C000000L

// addressBlock: gc_tpdec
//TA_EDC_CNT
#define TA_EDC_CNT__TA_FS_DFIFO_SEC_COUNT__SHIFT                                                              0x0
#define TA_EDC_CNT__TA_FS_DFIFO_DED_COUNT__SHIFT                                                              0x2
#define TA_EDC_CNT__TA_FS_AFIFO_SEC_COUNT__SHIFT                                                              0x4
#define TA_EDC_CNT__TA_FS_AFIFO_DED_COUNT__SHIFT                                                              0x6
#define TA_EDC_CNT__TA_FL_LFIFO_SEC_COUNT__SHIFT                                                              0x8
#define TA_EDC_CNT__TA_FL_LFIFO_DED_COUNT__SHIFT                                                              0xa
#define TA_EDC_CNT__TA_FX_LFIFO_SEC_COUNT__SHIFT                                                              0xc
#define TA_EDC_CNT__TA_FX_LFIFO_DED_COUNT__SHIFT                                                              0xe
#define TA_EDC_CNT__TA_FS_CFIFO_SEC_COUNT__SHIFT                                                              0x10
#define TA_EDC_CNT__TA_FS_CFIFO_DED_COUNT__SHIFT                                                              0x12
#define TA_EDC_CNT__TA_FS_DFIFO_SEC_COUNT_MASK                                                                0x00000003L
#define TA_EDC_CNT__TA_FS_DFIFO_DED_COUNT_MASK                                                                0x0000000CL
#define TA_EDC_CNT__TA_FS_AFIFO_SEC_COUNT_MASK                                                                0x00000030L
#define TA_EDC_CNT__TA_FS_AFIFO_DED_COUNT_MASK                                                                0x000000C0L
#define TA_EDC_CNT__TA_FL_LFIFO_SEC_COUNT_MASK                                                                0x00000300L
#define TA_EDC_CNT__TA_FL_LFIFO_DED_COUNT_MASK                                                                0x00000C00L
#define TA_EDC_CNT__TA_FX_LFIFO_SEC_COUNT_MASK                                                                0x00003000L
#define TA_EDC_CNT__TA_FX_LFIFO_DED_COUNT_MASK                                                                0x0000C000L
#define TA_EDC_CNT__TA_FS_CFIFO_SEC_COUNT_MASK                                                                0x00030000L
#define TA_EDC_CNT__TA_FS_CFIFO_DED_COUNT_MASK                                                                0x000C0000L

// addressBlock: gc_tcdec
//TCP_EDC_CNT
#define TCP_EDC_CNT__SEC_COUNT__SHIFT                                                                         0x0
#define TCP_EDC_CNT__LFIFO_SED_COUNT__SHIFT                                                                   0x8
#define TCP_EDC_CNT__DED_COUNT__SHIFT                                                                         0x10
#define TCP_EDC_CNT__SEC_COUNT_MASK                                                                           0x000000FFL
#define TCP_EDC_CNT__LFIFO_SED_COUNT_MASK                                                                     0x0000FF00L
#define TCP_EDC_CNT__DED_COUNT_MASK                                                                           0x00FF0000L
//TCP_EDC_CNT_NEW
#define TCP_EDC_CNT_NEW__CACHE_RAM_SEC_COUNT__SHIFT                                                           0x0
#define TCP_EDC_CNT_NEW__CACHE_RAM_DED_COUNT__SHIFT                                                           0x2
#define TCP_EDC_CNT_NEW__LFIFO_RAM_SEC_COUNT__SHIFT                                                           0x4
#define TCP_EDC_CNT_NEW__LFIFO_RAM_DED_COUNT__SHIFT                                                           0x6
#define TCP_EDC_CNT_NEW__CMD_FIFO_SEC_COUNT__SHIFT                                                            0x8
#define TCP_EDC_CNT_NEW__CMD_FIFO_DED_COUNT__SHIFT                                                            0xa
#define TCP_EDC_CNT_NEW__VM_FIFO_SEC_COUNT__SHIFT                                                             0xc
#define TCP_EDC_CNT_NEW__VM_FIFO_DED_COUNT__SHIFT                                                             0xe
#define TCP_EDC_CNT_NEW__DB_RAM_SED_COUNT__SHIFT                                                              0x10
#define TCP_EDC_CNT_NEW__UTCL1_LFIFO0_SEC_COUNT__SHIFT                                                        0x12
#define TCP_EDC_CNT_NEW__UTCL1_LFIFO0_DED_COUNT__SHIFT                                                        0x14
#define TCP_EDC_CNT_NEW__UTCL1_LFIFO1_SEC_COUNT__SHIFT                                                        0x16
#define TCP_EDC_CNT_NEW__UTCL1_LFIFO1_DED_COUNT__SHIFT                                                        0x18
#define TCP_EDC_CNT_NEW__CACHE_RAM_SEC_COUNT_MASK                                                             0x00000003L
#define TCP_EDC_CNT_NEW__CACHE_RAM_DED_COUNT_MASK                                                             0x0000000CL
#define TCP_EDC_CNT_NEW__LFIFO_RAM_SEC_COUNT_MASK                                                             0x00000030L
#define TCP_EDC_CNT_NEW__LFIFO_RAM_DED_COUNT_MASK                                                             0x000000C0L
#define TCP_EDC_CNT_NEW__CMD_FIFO_SEC_COUNT_MASK                                                              0x00000300L
#define TCP_EDC_CNT_NEW__CMD_FIFO_DED_COUNT_MASK                                                              0x00000C00L
#define TCP_EDC_CNT_NEW__VM_FIFO_SEC_COUNT_MASK                                                               0x00003000L
#define TCP_EDC_CNT_NEW__VM_FIFO_DED_COUNT_MASK                                                               0x0000C000L
#define TCP_EDC_CNT_NEW__DB_RAM_SED_COUNT_MASK                                                                0x00030000L
#define TCP_EDC_CNT_NEW__UTCL1_LFIFO0_SEC_COUNT_MASK                                                          0x000C0000L
#define TCP_EDC_CNT_NEW__UTCL1_LFIFO0_DED_COUNT_MASK                                                          0x00300000L
#define TCP_EDC_CNT_NEW__UTCL1_LFIFO1_SEC_COUNT_MASK                                                          0x00C00000L
#define TCP_EDC_CNT_NEW__UTCL1_LFIFO1_DED_COUNT_MASK                                                          0x03000000L
//TCP_ATC_EDC_GATCL1_CNT
#define TCP_ATC_EDC_GATCL1_CNT__DATA_SEC__SHIFT                                                               0x0
#define TCP_ATC_EDC_GATCL1_CNT__DATA_SEC_MASK                                                                 0x000000FFL
//TCI_EDC_CNT
#define TCI_EDC_CNT__WRITE_RAM_SEC_COUNT__SHIFT                                                               0x0
#define TCI_EDC_CNT__WRITE_RAM_DED_COUNT__SHIFT                                                               0x2
#define TCI_EDC_CNT__WRITE_RAM_SEC_COUNT_MASK                                                                 0x00000003L
#define TCI_EDC_CNT__WRITE_RAM_DED_COUNT_MASK                                                                 0x0000000CL
//TCA_EDC_CNT
#define TCA_EDC_CNT__HOLE_FIFO_SEC_COUNT__SHIFT                                                               0x0
#define TCA_EDC_CNT__HOLE_FIFO_DED_COUNT__SHIFT                                                               0x2
#define TCA_EDC_CNT__REQ_FIFO_SEC_COUNT__SHIFT                                                                0x4
#define TCA_EDC_CNT__REQ_FIFO_DED_COUNT__SHIFT                                                                0x6
#define TCA_EDC_CNT__HOLE_FIFO_SEC_COUNT_MASK                                                                 0x00000003L
#define TCA_EDC_CNT__HOLE_FIFO_DED_COUNT_MASK                                                                 0x0000000CL
#define TCA_EDC_CNT__REQ_FIFO_SEC_COUNT_MASK                                                                  0x00000030L
#define TCA_EDC_CNT__REQ_FIFO_DED_COUNT_MASK                                                                  0x000000C0L
//TCC_EDC_CNT
#define TCC_EDC_CNT__CACHE_DATA_SEC_COUNT__SHIFT                                                              0x0
#define TCC_EDC_CNT__CACHE_DATA_DED_COUNT__SHIFT                                                              0x2
#define TCC_EDC_CNT__CACHE_DIRTY_SEC_COUNT__SHIFT                                                             0x4
#define TCC_EDC_CNT__CACHE_DIRTY_DED_COUNT__SHIFT                                                             0x6
#define TCC_EDC_CNT__HIGH_RATE_TAG_SEC_COUNT__SHIFT                                                           0x8
#define TCC_EDC_CNT__HIGH_RATE_TAG_DED_COUNT__SHIFT                                                           0xa
#define TCC_EDC_CNT__LOW_RATE_TAG_SEC_COUNT__SHIFT                                                            0xc
#define TCC_EDC_CNT__LOW_RATE_TAG_DED_COUNT__SHIFT                                                            0xe
#define TCC_EDC_CNT__SRC_FIFO_SEC_COUNT__SHIFT                                                                0x10
#define TCC_EDC_CNT__SRC_FIFO_DED_COUNT__SHIFT                                                                0x12
#define TCC_EDC_CNT__LATENCY_FIFO_SEC_COUNT__SHIFT                                                            0x14
#define TCC_EDC_CNT__LATENCY_FIFO_DED_COUNT__SHIFT                                                            0x16
#define TCC_EDC_CNT__LATENCY_FIFO_NEXT_RAM_SEC_COUNT__SHIFT                                                   0x18
#define TCC_EDC_CNT__LATENCY_FIFO_NEXT_RAM_DED_COUNT__SHIFT                                                   0x1a
#define TCC_EDC_CNT__CACHE_DATA_SEC_COUNT_MASK                                                                0x00000003L
#define TCC_EDC_CNT__CACHE_DATA_DED_COUNT_MASK                                                                0x0000000CL
#define TCC_EDC_CNT__CACHE_DIRTY_SEC_COUNT_MASK                                                               0x00000030L
#define TCC_EDC_CNT__CACHE_DIRTY_DED_COUNT_MASK                                                               0x000000C0L
#define TCC_EDC_CNT__HIGH_RATE_TAG_SEC_COUNT_MASK                                                             0x00000300L
#define TCC_EDC_CNT__HIGH_RATE_TAG_DED_COUNT_MASK                                                             0x00000C00L
#define TCC_EDC_CNT__LOW_RATE_TAG_SEC_COUNT_MASK                                                              0x00003000L
#define TCC_EDC_CNT__LOW_RATE_TAG_DED_COUNT_MASK                                                              0x0000C000L
#define TCC_EDC_CNT__SRC_FIFO_SEC_COUNT_MASK                                                                  0x00030000L
#define TCC_EDC_CNT__SRC_FIFO_DED_COUNT_MASK                                                                  0x000C0000L
#define TCC_EDC_CNT__LATENCY_FIFO_SEC_COUNT_MASK                                                              0x00300000L
#define TCC_EDC_CNT__LATENCY_FIFO_DED_COUNT_MASK                                                              0x00C00000L
#define TCC_EDC_CNT__LATENCY_FIFO_NEXT_RAM_SEC_COUNT_MASK                                                     0x03000000L
#define TCC_EDC_CNT__LATENCY_FIFO_NEXT_RAM_DED_COUNT_MASK                                                     0x0C000000L
//TCC_EDC_CNT2
#define TCC_EDC_CNT2__CACHE_TAG_PROBE_FIFO_SEC_COUNT__SHIFT                                                   0x0
#define TCC_EDC_CNT2__CACHE_TAG_PROBE_FIFO_DED_COUNT__SHIFT                                                   0x2
#define TCC_EDC_CNT2__UC_ATOMIC_FIFO_SEC_COUNT__SHIFT                                                         0x4
#define TCC_EDC_CNT2__UC_ATOMIC_FIFO_DED_COUNT__SHIFT                                                         0x6
#define TCC_EDC_CNT2__WRITE_CACHE_READ_SEC_COUNT__SHIFT                                                       0x8
#define TCC_EDC_CNT2__WRITE_CACHE_READ_DED_COUNT__SHIFT                                                       0xa
#define TCC_EDC_CNT2__RETURN_CONTROL_SEC_COUNT__SHIFT                                                         0xc
#define TCC_EDC_CNT2__RETURN_CONTROL_DED_COUNT__SHIFT                                                         0xe
#define TCC_EDC_CNT2__IN_USE_TRANSFER_SEC_COUNT__SHIFT                                                        0x10
#define TCC_EDC_CNT2__IN_USE_TRANSFER_DED_COUNT__SHIFT                                                        0x12
#define TCC_EDC_CNT2__IN_USE_DEC_SEC_COUNT__SHIFT                                                             0x14
#define TCC_EDC_CNT2__IN_USE_DEC_DED_COUNT__SHIFT                                                             0x16
#define TCC_EDC_CNT2__WRITE_RETURN_SEC_COUNT__SHIFT                                                           0x18
#define TCC_EDC_CNT2__WRITE_RETURN_DED_COUNT__SHIFT                                                           0x1a
#define TCC_EDC_CNT2__RETURN_DATA_SEC_COUNT__SHIFT                                                            0x1c
#define TCC_EDC_CNT2__RETURN_DATA_DED_COUNT__SHIFT                                                            0x1e
#define TCC_EDC_CNT2__CACHE_TAG_PROBE_FIFO_SEC_COUNT_MASK                                                     0x00000003L
#define TCC_EDC_CNT2__CACHE_TAG_PROBE_FIFO_DED_COUNT_MASK                                                     0x0000000CL
#define TCC_EDC_CNT2__UC_ATOMIC_FIFO_SEC_COUNT_MASK                                                           0x00000030L
#define TCC_EDC_CNT2__UC_ATOMIC_FIFO_DED_COUNT_MASK                                                           0x000000C0L
#define TCC_EDC_CNT2__WRITE_CACHE_READ_SEC_COUNT_MASK                                                         0x00000300L
#define TCC_EDC_CNT2__WRITE_CACHE_READ_DED_COUNT_MASK                                                         0x00000C00L
#define TCC_EDC_CNT2__RETURN_CONTROL_SEC_COUNT_MASK                                                           0x00003000L
#define TCC_EDC_CNT2__RETURN_CONTROL_DED_COUNT_MASK                                                           0x0000C000L
#define TCC_EDC_CNT2__IN_USE_TRANSFER_SEC_COUNT_MASK                                                          0x00030000L
#define TCC_EDC_CNT2__IN_USE_TRANSFER_DED_COUNT_MASK                                                          0x000C0000L
#define TCC_EDC_CNT2__IN_USE_DEC_SEC_COUNT_MASK                                                               0x00300000L
#define TCC_EDC_CNT2__IN_USE_DEC_DED_COUNT_MASK                                                               0x00C00000L
#define TCC_EDC_CNT2__WRITE_RETURN_SEC_COUNT_MASK                                                             0x03000000L
#define TCC_EDC_CNT2__WRITE_RETURN_DED_COUNT_MASK                                                             0x0C000000L
#define TCC_EDC_CNT2__RETURN_DATA_SEC_COUNT_MASK                                                              0x30000000L
#define TCC_EDC_CNT2__RETURN_DATA_DED_COUNT_MASK                                                              0xC0000000L

// addressBlock: gc_tpdec
//TD_EDC_CNT
#define TD_EDC_CNT__SS_FIFO_LO_SEC_COUNT__SHIFT                                                               0x0
#define TD_EDC_CNT__SS_FIFO_LO_DED_COUNT__SHIFT                                                               0x2
#define TD_EDC_CNT__SS_FIFO_HI_SEC_COUNT__SHIFT                                                               0x4
#define TD_EDC_CNT__SS_FIFO_HI_DED_COUNT__SHIFT                                                               0x6
#define TD_EDC_CNT__CS_FIFO_SEC_COUNT__SHIFT                                                                  0x8
#define TD_EDC_CNT__CS_FIFO_DED_COUNT__SHIFT                                                                  0xa
#define TD_EDC_CNT__SS_FIFO_LO_SEC_COUNT_MASK                                                                 0x00000003L
#define TD_EDC_CNT__SS_FIFO_LO_DED_COUNT_MASK                                                                 0x0000000CL
#define TD_EDC_CNT__SS_FIFO_HI_SEC_COUNT_MASK                                                                 0x00000030L
#define TD_EDC_CNT__SS_FIFO_HI_DED_COUNT_MASK                                                                 0x000000C0L
#define TD_EDC_CNT__CS_FIFO_SEC_COUNT_MASK                                                                    0x00000300L
#define TD_EDC_CNT__CS_FIFO_DED_COUNT_MASK                                                                    0x00000C00L
//TA_EDC_CNT
#define TA_EDC_CNT__TA_FS_DFIFO_SEC_COUNT__SHIFT                                                              0x0
#define TA_EDC_CNT__TA_FS_DFIFO_DED_COUNT__SHIFT                                                              0x2
#define TA_EDC_CNT__TA_FS_AFIFO_SEC_COUNT__SHIFT                                                              0x4
#define TA_EDC_CNT__TA_FS_AFIFO_DED_COUNT__SHIFT                                                              0x6
#define TA_EDC_CNT__TA_FL_LFIFO_SEC_COUNT__SHIFT                                                              0x8
#define TA_EDC_CNT__TA_FL_LFIFO_DED_COUNT__SHIFT                                                              0xa
#define TA_EDC_CNT__TA_FX_LFIFO_SEC_COUNT__SHIFT                                                              0xc
#define TA_EDC_CNT__TA_FX_LFIFO_DED_COUNT__SHIFT                                                              0xe
#define TA_EDC_CNT__TA_FS_CFIFO_SEC_COUNT__SHIFT                                                              0x10
#define TA_EDC_CNT__TA_FS_CFIFO_DED_COUNT__SHIFT                                                              0x12
#define TA_EDC_CNT__TA_FS_DFIFO_SEC_COUNT_MASK                                                                0x00000003L
#define TA_EDC_CNT__TA_FS_DFIFO_DED_COUNT_MASK                                                                0x0000000CL
#define TA_EDC_CNT__TA_FS_AFIFO_SEC_COUNT_MASK                                                                0x00000030L
#define TA_EDC_CNT__TA_FS_AFIFO_DED_COUNT_MASK                                                                0x000000C0L
#define TA_EDC_CNT__TA_FL_LFIFO_SEC_COUNT_MASK                                                                0x00000300L
#define TA_EDC_CNT__TA_FL_LFIFO_DED_COUNT_MASK                                                                0x00000C00L
#define TA_EDC_CNT__TA_FX_LFIFO_SEC_COUNT_MASK                                                                0x00003000L
#define TA_EDC_CNT__TA_FX_LFIFO_DED_COUNT_MASK                                                                0x0000C000L
#define TA_EDC_CNT__TA_FS_CFIFO_SEC_COUNT_MASK                                                                0x00030000L
#define TA_EDC_CNT__TA_FS_CFIFO_DED_COUNT_MASK                                                                0x000C0000L

// addressBlock: gc_ea_gceadec2
//GCEA_EDC_CNT
#define GCEA_EDC_CNT__DRAMRD_CMDMEM_SEC_COUNT__SHIFT                                                          0x0
#define GCEA_EDC_CNT__DRAMRD_CMDMEM_DED_COUNT__SHIFT                                                          0x2
#define GCEA_EDC_CNT__DRAMWR_CMDMEM_SEC_COUNT__SHIFT                                                          0x4
#define GCEA_EDC_CNT__DRAMWR_CMDMEM_DED_COUNT__SHIFT                                                          0x6
#define GCEA_EDC_CNT__DRAMWR_DATAMEM_SEC_COUNT__SHIFT                                                         0x8
#define GCEA_EDC_CNT__DRAMWR_DATAMEM_DED_COUNT__SHIFT                                                         0xa
#define GCEA_EDC_CNT__RRET_TAGMEM_SEC_COUNT__SHIFT                                                            0xc
#define GCEA_EDC_CNT__RRET_TAGMEM_DED_COUNT__SHIFT                                                            0xe
#define GCEA_EDC_CNT__WRET_TAGMEM_SEC_COUNT__SHIFT                                                            0x10
#define GCEA_EDC_CNT__WRET_TAGMEM_DED_COUNT__SHIFT                                                            0x12
#define GCEA_EDC_CNT__DRAMRD_PAGEMEM_SED_COUNT__SHIFT                                                         0x14
#define GCEA_EDC_CNT__DRAMWR_PAGEMEM_SED_COUNT__SHIFT                                                         0x16
#define GCEA_EDC_CNT__IORD_CMDMEM_SED_COUNT__SHIFT                                                            0x18
#define GCEA_EDC_CNT__IOWR_CMDMEM_SED_COUNT__SHIFT                                                            0x1a
#define GCEA_EDC_CNT__IOWR_DATAMEM_SED_COUNT__SHIFT                                                           0x1c
#define GCEA_EDC_CNT__MAM_AFMEM_SEC_COUNT__SHIFT                                                              0x1e
#define GCEA_EDC_CNT__DRAMRD_CMDMEM_SEC_COUNT_MASK                                                            0x00000003L
#define GCEA_EDC_CNT__DRAMRD_CMDMEM_DED_COUNT_MASK                                                            0x0000000CL
#define GCEA_EDC_CNT__DRAMWR_CMDMEM_SEC_COUNT_MASK                                                            0x00000030L
#define GCEA_EDC_CNT__DRAMWR_CMDMEM_DED_COUNT_MASK                                                            0x000000C0L
#define GCEA_EDC_CNT__DRAMWR_DATAMEM_SEC_COUNT_MASK                                                           0x00000300L
#define GCEA_EDC_CNT__DRAMWR_DATAMEM_DED_COUNT_MASK                                                           0x00000C00L
#define GCEA_EDC_CNT__RRET_TAGMEM_SEC_COUNT_MASK                                                              0x00003000L
#define GCEA_EDC_CNT__RRET_TAGMEM_DED_COUNT_MASK                                                              0x0000C000L
#define GCEA_EDC_CNT__WRET_TAGMEM_SEC_COUNT_MASK                                                              0x00030000L
#define GCEA_EDC_CNT__WRET_TAGMEM_DED_COUNT_MASK                                                              0x000C0000L
#define GCEA_EDC_CNT__DRAMRD_PAGEMEM_SED_COUNT_MASK                                                           0x00300000L
#define GCEA_EDC_CNT__DRAMWR_PAGEMEM_SED_COUNT_MASK                                                           0x00C00000L
#define GCEA_EDC_CNT__IORD_CMDMEM_SED_COUNT_MASK                                                              0x03000000L
#define GCEA_EDC_CNT__IOWR_CMDMEM_SED_COUNT_MASK                                                              0x0C000000L
#define GCEA_EDC_CNT__IOWR_DATAMEM_SED_COUNT_MASK                                                             0x30000000L
#define GCEA_EDC_CNT__MAM_AFMEM_SEC_COUNT_MASK                                                                0xC0000000L
//GCEA_EDC_CNT2
#define GCEA_EDC_CNT2__GMIRD_CMDMEM_SEC_COUNT__SHIFT                                                          0x0
#define GCEA_EDC_CNT2__GMIRD_CMDMEM_DED_COUNT__SHIFT                                                          0x2
#define GCEA_EDC_CNT2__GMIWR_CMDMEM_SEC_COUNT__SHIFT                                                          0x4
#define GCEA_EDC_CNT2__GMIWR_CMDMEM_DED_COUNT__SHIFT                                                          0x6
#define GCEA_EDC_CNT2__GMIWR_DATAMEM_SEC_COUNT__SHIFT                                                         0x8
#define GCEA_EDC_CNT2__GMIWR_DATAMEM_DED_COUNT__SHIFT                                                         0xa
#define GCEA_EDC_CNT2__GMIRD_PAGEMEM_SED_COUNT__SHIFT                                                         0xc
#define GCEA_EDC_CNT2__GMIWR_PAGEMEM_SED_COUNT__SHIFT                                                         0xe
#define GCEA_EDC_CNT2__MAM_D0MEM_SED_COUNT__SHIFT                                                             0x10
#define GCEA_EDC_CNT2__MAM_D1MEM_SED_COUNT__SHIFT                                                             0x12
#define GCEA_EDC_CNT2__MAM_D2MEM_SED_COUNT__SHIFT                                                             0x14
#define GCEA_EDC_CNT2__MAM_D3MEM_SED_COUNT__SHIFT                                                             0x16
#define GCEA_EDC_CNT2__MAM_D0MEM_DED_COUNT__SHIFT                                                             0x18
#define GCEA_EDC_CNT2__MAM_D1MEM_DED_COUNT__SHIFT                                                             0x1a
#define GCEA_EDC_CNT2__MAM_D2MEM_DED_COUNT__SHIFT                                                             0x1c
#define GCEA_EDC_CNT2__MAM_D3MEM_DED_COUNT__SHIFT                                                             0x1e
#define GCEA_EDC_CNT2__GMIRD_CMDMEM_SEC_COUNT_MASK                                                            0x00000003L
#define GCEA_EDC_CNT2__GMIRD_CMDMEM_DED_COUNT_MASK                                                            0x0000000CL
#define GCEA_EDC_CNT2__GMIWR_CMDMEM_SEC_COUNT_MASK                                                            0x00000030L
#define GCEA_EDC_CNT2__GMIWR_CMDMEM_DED_COUNT_MASK                                                            0x000000C0L
#define GCEA_EDC_CNT2__GMIWR_DATAMEM_SEC_COUNT_MASK                                                           0x00000300L
#define GCEA_EDC_CNT2__GMIWR_DATAMEM_DED_COUNT_MASK                                                           0x00000C00L
#define GCEA_EDC_CNT2__GMIRD_PAGEMEM_SED_COUNT_MASK                                                           0x00003000L
#define GCEA_EDC_CNT2__GMIWR_PAGEMEM_SED_COUNT_MASK                                                           0x0000C000L
#define GCEA_EDC_CNT2__MAM_D0MEM_SED_COUNT_MASK                                                               0x00030000L
#define GCEA_EDC_CNT2__MAM_D1MEM_SED_COUNT_MASK                                                               0x000C0000L
#define GCEA_EDC_CNT2__MAM_D2MEM_SED_COUNT_MASK                                                               0x00300000L
#define GCEA_EDC_CNT2__MAM_D3MEM_SED_COUNT_MASK                                                               0x00C00000L
#define GCEA_EDC_CNT2__MAM_D0MEM_DED_COUNT_MASK                                                               0x03000000L
#define GCEA_EDC_CNT2__MAM_D1MEM_DED_COUNT_MASK                                                               0x0C000000L
#define GCEA_EDC_CNT2__MAM_D2MEM_DED_COUNT_MASK                                                               0x30000000L
#define GCEA_EDC_CNT2__MAM_D3MEM_DED_COUNT_MASK                                                               0xC0000000L
//GCEA_EDC_CNT3
#define GCEA_EDC_CNT3__DRAMRD_PAGEMEM_DED_COUNT__SHIFT                                                        0x0
#define GCEA_EDC_CNT3__DRAMWR_PAGEMEM_DED_COUNT__SHIFT                                                        0x2
#define GCEA_EDC_CNT3__IORD_CMDMEM_DED_COUNT__SHIFT                                                           0x4
#define GCEA_EDC_CNT3__IOWR_CMDMEM_DED_COUNT__SHIFT                                                           0x6
#define GCEA_EDC_CNT3__IOWR_DATAMEM_DED_COUNT__SHIFT                                                          0x8
#define GCEA_EDC_CNT3__GMIRD_PAGEMEM_DED_COUNT__SHIFT                                                         0xa
#define GCEA_EDC_CNT3__GMIWR_PAGEMEM_DED_COUNT__SHIFT                                                         0xc
#define GCEA_EDC_CNT3__MAM_AFMEM_DED_COUNT__SHIFT                                                             0xe
#define GCEA_EDC_CNT3__MAM_A0MEM_SEC_COUNT__SHIFT                                                             0x10
#define GCEA_EDC_CNT3__MAM_A0MEM_DED_COUNT__SHIFT                                                             0x12
#define GCEA_EDC_CNT3__MAM_A1MEM_SEC_COUNT__SHIFT                                                             0x14
#define GCEA_EDC_CNT3__MAM_A1MEM_DED_COUNT__SHIFT                                                             0x16
#define GCEA_EDC_CNT3__MAM_A2MEM_SEC_COUNT__SHIFT                                                             0x18
#define GCEA_EDC_CNT3__MAM_A2MEM_DED_COUNT__SHIFT                                                             0x1a
#define GCEA_EDC_CNT3__MAM_A3MEM_SEC_COUNT__SHIFT                                                             0x1c
#define GCEA_EDC_CNT3__MAM_A3MEM_DED_COUNT__SHIFT                                                             0x1e
#define GCEA_EDC_CNT3__DRAMRD_PAGEMEM_DED_COUNT_MASK                                                          0x00000003L
#define GCEA_EDC_CNT3__DRAMWR_PAGEMEM_DED_COUNT_MASK                                                          0x0000000CL
#define GCEA_EDC_CNT3__IORD_CMDMEM_DED_COUNT_MASK                                                             0x00000030L
#define GCEA_EDC_CNT3__IOWR_CMDMEM_DED_COUNT_MASK                                                             0x000000C0L
#define GCEA_EDC_CNT3__IOWR_DATAMEM_DED_COUNT_MASK                                                            0x00000300L
#define GCEA_EDC_CNT3__GMIRD_PAGEMEM_DED_COUNT_MASK                                                           0x00000C00L
#define GCEA_EDC_CNT3__GMIWR_PAGEMEM_DED_COUNT_MASK                                                           0x00003000L
#define GCEA_EDC_CNT3__MAM_AFMEM_DED_COUNT_MASK                                                               0x0000C000L
#define GCEA_EDC_CNT3__MAM_A0MEM_SEC_COUNT_MASK                                                               0x00030000L
#define GCEA_EDC_CNT3__MAM_A0MEM_DED_COUNT_MASK                                                               0x000C0000L
#define GCEA_EDC_CNT3__MAM_A1MEM_SEC_COUNT_MASK                                                               0x00300000L
#define GCEA_EDC_CNT3__MAM_A1MEM_DED_COUNT_MASK                                                               0x00C00000L
#define GCEA_EDC_CNT3__MAM_A2MEM_SEC_COUNT_MASK                                                               0x03000000L
#define GCEA_EDC_CNT3__MAM_A2MEM_DED_COUNT_MASK                                                               0x0C000000L
#define GCEA_EDC_CNT3__MAM_A3MEM_SEC_COUNT_MASK                                                               0x30000000L
#define GCEA_EDC_CNT3__MAM_A3MEM_DED_COUNT_MASK                                                               0xC0000000L

//GCEA_ERR_STATUS
#define GCEA_ERR_STATUS__SDP_RDRSP_STATUS__SHIFT                                                              0x0
#define GCEA_ERR_STATUS__SDP_WRRSP_STATUS__SHIFT                                                              0x4
#define GCEA_ERR_STATUS__SDP_RDRSP_DATASTATUS__SHIFT                                                          0x8
#define GCEA_ERR_STATUS__SDP_RDRSP_DATAPARITY_ERROR__SHIFT                                                    0xa
#define GCEA_ERR_STATUS__CLEAR_ERROR_STATUS__SHIFT                                                            0xb
#define GCEA_ERR_STATUS__BUSY_ON_ERROR__SHIFT                                                                 0xc
#define GCEA_ERR_STATUS__FUE_FLAG__SHIFT                                                                      0xd
#define GCEA_ERR_STATUS__SDP_RDRSP_STATUS_MASK                                                                0x0000000FL
#define GCEA_ERR_STATUS__SDP_WRRSP_STATUS_MASK                                                                0x000000F0L
#define GCEA_ERR_STATUS__SDP_RDRSP_DATASTATUS_MASK                                                            0x00000300L
#define GCEA_ERR_STATUS__SDP_RDRSP_DATAPARITY_ERROR_MASK                                                      0x00000400L
#define GCEA_ERR_STATUS__CLEAR_ERROR_STATUS_MASK                                                              0x00000800L
#define GCEA_ERR_STATUS__BUSY_ON_ERROR_MASK                                                                   0x00001000L
#define GCEA_ERR_STATUS__FUE_FLAG_MASK                                                                        0x00002000L

// addressBlock: gc_gfxudec
//GRBM_GFX_INDEX
#define GRBM_GFX_INDEX__INSTANCE_INDEX__SHIFT                                                                 0x0
#define GRBM_GFX_INDEX__SH_INDEX__SHIFT                                                                       0x8
#define GRBM_GFX_INDEX__SE_INDEX__SHIFT                                                                       0x10
#define GRBM_GFX_INDEX__SH_BROADCAST_WRITES__SHIFT                                                            0x1d
#define GRBM_GFX_INDEX__INSTANCE_BROADCAST_WRITES__SHIFT                                                      0x1e
#define GRBM_GFX_INDEX__SE_BROADCAST_WRITES__SHIFT                                                            0x1f
#define GRBM_GFX_INDEX__INSTANCE_INDEX_MASK                                                                   0x000000FFL
#define GRBM_GFX_INDEX__SH_INDEX_MASK                                                                         0x0000FF00L
#define GRBM_GFX_INDEX__SE_INDEX_MASK                                                                         0x00FF0000L
#define GRBM_GFX_INDEX__SH_BROADCAST_WRITES_MASK                                                              0x20000000L
#define GRBM_GFX_INDEX__INSTANCE_BROADCAST_WRITES_MASK                                                        0x40000000L
#define GRBM_GFX_INDEX__SE_BROADCAST_WRITES_MASK                                                              0x80000000L

// addressBlock: gc_utcl2_atcl2dec
//ATC_L2_CNTL
//ATC_L2_CACHE_4K_DSM_INDEX
#define ATC_L2_CACHE_4K_DSM_INDEX__INDEX__SHIFT                                                               0x0
#define ATC_L2_CACHE_4K_DSM_INDEX__INDEX_MASK                                                                 0x000000FFL
//ATC_L2_CACHE_2M_DSM_INDEX
#define ATC_L2_CACHE_2M_DSM_INDEX__INDEX__SHIFT                                                               0x0
#define ATC_L2_CACHE_2M_DSM_INDEX__INDEX_MASK                                                                 0x000000FFL
//ATC_L2_CACHE_4K_DSM_CNTL
#define ATC_L2_CACHE_4K_DSM_CNTL__SEC_COUNT__SHIFT                                                            0xd
#define ATC_L2_CACHE_4K_DSM_CNTL__DED_COUNT__SHIFT                                                            0xf
#define ATC_L2_CACHE_4K_DSM_CNTL__SEC_COUNT_MASK                                                              0x00006000L
#define ATC_L2_CACHE_4K_DSM_CNTL__DED_COUNT_MASK                                                              0x00018000L
//ATC_L2_CACHE_2M_DSM_CNTL
#define ATC_L2_CACHE_2M_DSM_CNTL__SEC_COUNT__SHIFT                                                            0xd
#define ATC_L2_CACHE_2M_DSM_CNTL__DED_COUNT__SHIFT                                                            0xf
#define ATC_L2_CACHE_2M_DSM_CNTL__SEC_COUNT_MASK                                                              0x00006000L
#define ATC_L2_CACHE_2M_DSM_CNTL__DED_COUNT_MASK                                                              0x00018000L

// addressBlock: gc_utcl2_vml2pfdec
//VML2_MEM_ECC_INDEX
#define VML2_MEM_ECC_INDEX__INDEX__SHIFT                                                                      0x0
#define VML2_MEM_ECC_INDEX__INDEX_MASK                                                                        0x000000FFL
//VML2_WALKER_MEM_ECC_INDEX
#define VML2_WALKER_MEM_ECC_INDEX__INDEX__SHIFT                                                               0x0
#define VML2_WALKER_MEM_ECC_INDEX__INDEX_MASK                                                                 0x000000FFL
//UTCL2_MEM_ECC_INDEX
#define UTCL2_MEM_ECC_INDEX__INDEX__SHIFT                                                                     0x0
#define UTCL2_MEM_ECC_INDEX__INDEX_MASK                                                                       0x000000FFL
//VML2_MEM_ECC_CNTL
#define VML2_MEM_ECC_CNTL__SEC_COUNT__SHIFT                                                                   0xc
#define VML2_MEM_ECC_CNTL__DED_COUNT__SHIFT                                                                   0xe
#define VML2_MEM_ECC_CNTL__SEC_COUNT_MASK                                                                     0x00003000L
#define VML2_MEM_ECC_CNTL__DED_COUNT_MASK                                                                     0x0000C000L
//VML2_WALKER_MEM_ECC_CNTL
#define VML2_WALKER_MEM_ECC_CNTL__SEC_COUNT__SHIFT                                                            0xc
#define VML2_WALKER_MEM_ECC_CNTL__DED_COUNT__SHIFT                                                            0xe
#define VML2_WALKER_MEM_ECC_CNTL__SEC_COUNT_MASK                                                              0x00003000L
#define VML2_WALKER_MEM_ECC_CNTL__DED_COUNT_MASK                                                              0x0000C000L
//UTCL2_MEM_ECC_CNTL
#define UTCL2_MEM_ECC_CNTL__SEC_COUNT__SHIFT                                                                  0xc
#define UTCL2_MEM_ECC_CNTL__DED_COUNT__SHIFT                                                                  0xe
#define UTCL2_MEM_ECC_CNTL__SEC_COUNT_MASK                                                                    0x00003000L
#define UTCL2_MEM_ECC_CNTL__DED_COUNT_MASK                                                                    0x0000C000L

// addressBlock: gc_rlcpdec
//RLC_EDC_CNT
#define RLC_EDC_CNT__RLCG_INSTR_RAM_SEC_COUNT__SHIFT                                                          0x0
#define RLC_EDC_CNT__RLCG_INSTR_RAM_DED_COUNT__SHIFT                                                          0x2
#define RLC_EDC_CNT__RLCG_SCRATCH_RAM_SEC_COUNT__SHIFT                                                        0x4
#define RLC_EDC_CNT__RLCG_SCRATCH_RAM_DED_COUNT__SHIFT                                                        0x6
#define RLC_EDC_CNT__RLCV_INSTR_RAM_SEC_COUNT__SHIFT                                                          0x8
#define RLC_EDC_CNT__RLCV_INSTR_RAM_DED_COUNT__SHIFT                                                          0xa
#define RLC_EDC_CNT__RLCV_SCRATCH_RAM_SEC_COUNT__SHIFT                                                        0xc
#define RLC_EDC_CNT__RLCV_SCRATCH_RAM_DED_COUNT__SHIFT                                                        0xe
#define RLC_EDC_CNT__RLC_TCTAG_RAM_SEC_COUNT__SHIFT                                                           0x10
#define RLC_EDC_CNT__RLC_TCTAG_RAM_DED_COUNT__SHIFT                                                           0x12
#define RLC_EDC_CNT__RLC_SPM_SCRATCH_RAM_SEC_COUNT__SHIFT                                                     0x14
#define RLC_EDC_CNT__RLC_SPM_SCRATCH_RAM_DED_COUNT__SHIFT                                                      0x16
#define RLC_EDC_CNT__RLC_SRM_DATA_RAM_SEC_COUNT__SHIFT                                                        0x18
#define RLC_EDC_CNT__RLC_SRM_DATA_RAM_DED_COUNT__SHIFT                                                        0x1a
#define RLC_EDC_CNT__RLC_SRM_ADDR_RAM_SEC_COUNT__SHIFT                                                        0x1c
#define RLC_EDC_CNT__RLC_SRM_ADDR_RAM_DED_COUNT__SHIFT                                                        0x1e
#define RLC_EDC_CNT__RLCG_INSTR_RAM_SEC_COUNT_MASK                                                            0x00000003L
#define RLC_EDC_CNT__RLCG_INSTR_RAM_DED_COUNT_MASK                                                            0x0000000CL
#define RLC_EDC_CNT__RLCG_SCRATCH_RAM_SEC_COUNT_MASK                                                          0x00000030L
#define RLC_EDC_CNT__RLCG_SCRATCH_RAM_DED_COUNT_MASK                                                          0x000000C0L
#define RLC_EDC_CNT__RLCV_INSTR_RAM_SEC_COUNT_MASK                                                            0x00000300L
#define RLC_EDC_CNT__RLCV_INSTR_RAM_DED_COUNT_MASK                                                            0x00000C00L
#define RLC_EDC_CNT__RLCV_SCRATCH_RAM_SEC_COUNT_MASK                                                          0x00003000L
#define RLC_EDC_CNT__RLCV_SCRATCH_RAM_DED_COUNT_MASK                                                          0x0000C000L
#define RLC_EDC_CNT__RLC_TCTAG_RAM_SEC_COUNT_MASK                                                             0x00030000L
#define RLC_EDC_CNT__RLC_TCTAG_RAM_DED_COUNT_MASK                                                             0x000C0000L
#define RLC_EDC_CNT__RLC_SPM_SCRATCH_RAM_SEC_COUNT_MASK                                                       0x00300000L
#define RLC_EDC_CNT__RLC_SPM_SCRATCH_RAM_DED_COUNT_MASK                                                        0x00C00000L
#define RLC_EDC_CNT__RLC_SRM_DATA_RAM_SEC_COUNT_MASK                                                          0x03000000L
#define RLC_EDC_CNT__RLC_SRM_DATA_RAM_DED_COUNT_MASK                                                          0x0C000000L
#define RLC_EDC_CNT__RLC_SRM_ADDR_RAM_SEC_COUNT_MASK                                                          0x30000000L
#define RLC_EDC_CNT__RLC_SRM_ADDR_RAM_DED_COUNT_MASK                                                          0xC0000000L
//RLC_EDC_CNT2
#define RLC_EDC_CNT2__RLC_SPM_SE0_SCRATCH_RAM_SEC_COUNT__SHIFT                                                0x0
#define RLC_EDC_CNT2__RLC_SPM_SE0_SCRATCH_RAM_DED_COUNT__SHIFT                                                0x2
#define RLC_EDC_CNT2__RLC_SPM_SE1_SCRATCH_RAM_SEC_COUNT__SHIFT                                                0x4
#define RLC_EDC_CNT2__RLC_SPM_SE1_SCRATCH_RAM_DED_COUNT__SHIFT                                                0x6
#define RLC_EDC_CNT2__RLC_SPM_SE2_SCRATCH_RAM_SEC_COUNT__SHIFT                                                0x8
#define RLC_EDC_CNT2__RLC_SPM_SE2_SCRATCH_RAM_DED_COUNT__SHIFT                                                0xa
#define RLC_EDC_CNT2__RLC_SPM_SE3_SCRATCH_RAM_SEC_COUNT__SHIFT                                                0xc
#define RLC_EDC_CNT2__RLC_SPM_SE3_SCRATCH_RAM_DED_COUNT__SHIFT                                                0xe
#define RLC_EDC_CNT2__RLC_SPM_SE4_SCRATCH_RAM_SEC_COUNT__SHIFT                                                0x10
#define RLC_EDC_CNT2__RLC_SPM_SE4_SCRATCH_RAM_DED_COUNT__SHIFT                                                0x12
#define RLC_EDC_CNT2__RLC_SPM_SE5_SCRATCH_RAM_SEC_COUNT__SHIFT                                                0x14
#define RLC_EDC_CNT2__RLC_SPM_SE5_SCRATCH_RAM_DED_COUNT__SHIFT                                                0x16
#define RLC_EDC_CNT2__RLC_SPM_SE6_SCRATCH_RAM_SEC_COUNT__SHIFT                                                0x18
#define RLC_EDC_CNT2__RLC_SPM_SE6_SCRATCH_RAM_DED_COUNT__SHIFT                                                0x1a
#define RLC_EDC_CNT2__RLC_SPM_SE7_SCRATCH_RAM_SEC_COUNT__SHIFT                                                0x1c
#define RLC_EDC_CNT2__RLC_SPM_SE7_SCRATCH_RAM_DED_COUNT__SHIFT                                                0x1e
#define RLC_EDC_CNT2__RLC_SPM_SE0_SCRATCH_RAM_SEC_COUNT_MASK                                                  0x00000003L
#define RLC_EDC_CNT2__RLC_SPM_SE0_SCRATCH_RAM_DED_COUNT_MASK                                                  0x0000000CL
#define RLC_EDC_CNT2__RLC_SPM_SE1_SCRATCH_RAM_SEC_COUNT_MASK                                                  0x00000030L
#define RLC_EDC_CNT2__RLC_SPM_SE1_SCRATCH_RAM_DED_COUNT_MASK                                                  0x000000C0L
#define RLC_EDC_CNT2__RLC_SPM_SE2_SCRATCH_RAM_SEC_COUNT_MASK                                                  0x00000300L
#define RLC_EDC_CNT2__RLC_SPM_SE2_SCRATCH_RAM_DED_COUNT_MASK                                                  0x00000C00L
#define RLC_EDC_CNT2__RLC_SPM_SE3_SCRATCH_RAM_SEC_COUNT_MASK                                                  0x00003000L
#define RLC_EDC_CNT2__RLC_SPM_SE3_SCRATCH_RAM_DED_COUNT_MASK                                                  0x0000C000L
#define RLC_EDC_CNT2__RLC_SPM_SE4_SCRATCH_RAM_SEC_COUNT_MASK                                                  0x00030000L
#define RLC_EDC_CNT2__RLC_SPM_SE4_SCRATCH_RAM_DED_COUNT_MASK                                                  0x000C0000L
#define RLC_EDC_CNT2__RLC_SPM_SE5_SCRATCH_RAM_SEC_COUNT_MASK                                                  0x00300000L
#define RLC_EDC_CNT2__RLC_SPM_SE5_SCRATCH_RAM_DED_COUNT_MASK                                                  0x00C00000L
#define RLC_EDC_CNT2__RLC_SPM_SE6_SCRATCH_RAM_SEC_COUNT_MASK                                                  0x03000000L
#define RLC_EDC_CNT2__RLC_SPM_SE6_SCRATCH_RAM_DED_COUNT_MASK                                                  0x0C000000L
#define RLC_EDC_CNT2__RLC_SPM_SE7_SCRATCH_RAM_SEC_COUNT_MASK                                                  0x30000000L
#define RLC_EDC_CNT2__RLC_SPM_SE7_SCRATCH_RAM_DED_COUNT_MASK                                                  0xC0000000L

#endif
