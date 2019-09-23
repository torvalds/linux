/*
 * Copyright (C) 2019  Advanced Micro Devices, Inc.
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
#ifndef _smuio_11_0_0_SH_MASK_HEADER
#define _smuio_11_0_0_SH_MASK_HEADER


// addressBlock: smuio_smuio_SmuSmuioDec
//SMUSVI0_TEL_PLANE0
#define SMUSVI0_TEL_PLANE0__SVI0_PLANE0_IDDCOR__SHIFT                                                         0x0
#define SMUSVI0_TEL_PLANE0__SVI0_PLANE0_VDDCOR__SHIFT                                                         0x10
#define SMUSVI0_TEL_PLANE0__SVI0_PLANE0_IDDCOR_MASK                                                           0x000000FFL
#define SMUSVI0_TEL_PLANE0__SVI0_PLANE0_VDDCOR_MASK                                                           0x01FF0000L
//SMUIO_MCM_CONFIG
#define SMUIO_MCM_CONFIG__DIE_ID__SHIFT                                                                       0x0
#define SMUIO_MCM_CONFIG__PKG_TYPE__SHIFT                                                                     0x2
#define SMUIO_MCM_CONFIG__SOCKET_ID__SHIFT                                                                    0x5
#define SMUIO_MCM_CONFIG__PKG_SUBTYPE__SHIFT                                                                  0x6
#define SMUIO_MCM_CONFIG__DIE_ID_MASK                                                                         0x00000003L
#define SMUIO_MCM_CONFIG__PKG_TYPE_MASK                                                                       0x0000001CL
#define SMUIO_MCM_CONFIG__SOCKET_ID_MASK                                                                      0x00000020L
#define SMUIO_MCM_CONFIG__PKG_SUBTYPE_MASK                                                                    0x000000C0L
//CKSVII2C_IC_CON
#define CKSVII2C_IC_CON__IC_MASTER_MODE__SHIFT                                                                0x0
#define CKSVII2C_IC_CON__IC_MAX_SPEED_MODE__SHIFT                                                             0x1
#define CKSVII2C_IC_CON__IC_10BITADDR_SLAVE__SHIFT                                                            0x3
#define CKSVII2C_IC_CON__IC_10BITADDR_MASTER__SHIFT                                                           0x4
#define CKSVII2C_IC_CON__IC_RESTART_EN__SHIFT                                                                 0x5
#define CKSVII2C_IC_CON__IC_SLAVE_DISABLE__SHIFT                                                              0x6
#define CKSVII2C_IC_CON__STOP_DET_IFADDRESSED__SHIFT                                                          0x7
#define CKSVII2C_IC_CON__TX_EMPTY_CTRL__SHIFT                                                                 0x8
#define CKSVII2C_IC_CON__RX_FIFO_FULL_HLD_CTRL__SHIFT                                                         0x9
#define CKSVII2C_IC_CON__IC_MASTER_MODE_MASK                                                                  0x00000001L
#define CKSVII2C_IC_CON__IC_MAX_SPEED_MODE_MASK                                                               0x00000006L
#define CKSVII2C_IC_CON__IC_10BITADDR_SLAVE_MASK                                                              0x00000008L
#define CKSVII2C_IC_CON__IC_10BITADDR_MASTER_MASK                                                             0x00000010L
#define CKSVII2C_IC_CON__IC_RESTART_EN_MASK                                                                   0x00000020L
#define CKSVII2C_IC_CON__IC_SLAVE_DISABLE_MASK                                                                0x00000040L
#define CKSVII2C_IC_CON__STOP_DET_IFADDRESSED_MASK                                                            0x00000080L
#define CKSVII2C_IC_CON__TX_EMPTY_CTRL_MASK                                                                   0x00000100L
#define CKSVII2C_IC_CON__RX_FIFO_FULL_HLD_CTRL_MASK                                                           0x00000200L
//CKSVII2C_IC_TAR
#define CKSVII2C_IC_TAR__IC_TAR__SHIFT                                                                        0x0
#define CKSVII2C_IC_TAR__GC_OR_START__SHIFT                                                                   0xa
#define CKSVII2C_IC_TAR__SPECIAL__SHIFT                                                                       0xb
#define CKSVII2C_IC_TAR__IC_10BITADDR_MASTER__SHIFT                                                           0xc
#define CKSVII2C_IC_TAR__IC_TAR_MASK                                                                          0x000003FFL
#define CKSVII2C_IC_TAR__GC_OR_START_MASK                                                                     0x00000400L
#define CKSVII2C_IC_TAR__SPECIAL_MASK                                                                         0x00000800L
#define CKSVII2C_IC_TAR__IC_10BITADDR_MASTER_MASK                                                             0x00001000L
//CKSVII2C_IC_SAR
#define CKSVII2C_IC_SAR__IC_SAR__SHIFT                                                                        0x0
#define CKSVII2C_IC_SAR__IC_SAR_MASK                                                                          0x000003FFL
//CKSVII2C_IC_HS_MADDR
#define CKSVII2C_IC_HS_MADDR__IC_HS_MADDR__SHIFT                                                              0x0
#define CKSVII2C_IC_HS_MADDR__IC_HS_MADDR_MASK                                                                0x00000007L
//CKSVII2C_IC_DATA_CMD
#define CKSVII2C_IC_DATA_CMD__DAT__SHIFT                                                                      0x0
#define CKSVII2C_IC_DATA_CMD__CMD__SHIFT                                                                      0x8
#define CKSVII2C_IC_DATA_CMD__STOP__SHIFT                                                                     0x9
#define CKSVII2C_IC_DATA_CMD__RESTART__SHIFT                                                                  0xa
#define CKSVII2C_IC_DATA_CMD__DAT_MASK                                                                        0x000000FFL
#define CKSVII2C_IC_DATA_CMD__CMD_MASK                                                                        0x00000100L
#define CKSVII2C_IC_DATA_CMD__STOP_MASK                                                                       0x00000200L
#define CKSVII2C_IC_DATA_CMD__RESTART_MASK                                                                    0x00000400L
//CKSVII2C_IC_SS_SCL_HCNT
#define CKSVII2C_IC_SS_SCL_HCNT__IC_SS_SCL_HCNT__SHIFT                                                        0x0
#define CKSVII2C_IC_SS_SCL_HCNT__IC_SS_SCL_HCNT_MASK                                                          0x0000FFFFL
//CKSVII2C_IC_SS_SCL_LCNT
#define CKSVII2C_IC_SS_SCL_LCNT__IC_SS_SCL_LCNT__SHIFT                                                        0x0
#define CKSVII2C_IC_SS_SCL_LCNT__IC_SS_SCL_LCNT_MASK                                                          0x0000FFFFL
//CKSVII2C_IC_FS_SCL_HCNT
#define CKSVII2C_IC_FS_SCL_HCNT__IC_FS_SCL_HCNT__SHIFT                                                        0x0
#define CKSVII2C_IC_FS_SCL_HCNT__IC_FS_SCL_HCNT_MASK                                                          0x0000FFFFL
//CKSVII2C_IC_FS_SCL_LCNT
#define CKSVII2C_IC_FS_SCL_LCNT__IC_FS_SCL_LCNT__SHIFT                                                        0x0
#define CKSVII2C_IC_FS_SCL_LCNT__IC_FS_SCL_LCNT_MASK                                                          0x0000FFFFL
//CKSVII2C_IC_HS_SCL_HCNT
#define CKSVII2C_IC_HS_SCL_HCNT__IC_HS_SCL_HCNT__SHIFT                                                        0x0
#define CKSVII2C_IC_HS_SCL_HCNT__IC_HS_SCL_HCNT_MASK                                                          0x0000FFFFL
//CKSVII2C_IC_HS_SCL_LCNT
#define CKSVII2C_IC_HS_SCL_LCNT__IC_HS_SCL_LCNT__SHIFT                                                        0x0
#define CKSVII2C_IC_HS_SCL_LCNT__IC_HS_SCL_LCNT_MASK                                                          0x0000FFFFL
//CKSVII2C_IC_INTR_STAT
#define CKSVII2C_IC_INTR_STAT__R_RX_UNDER__SHIFT                                                              0x0
#define CKSVII2C_IC_INTR_STAT__R_RX_OVER__SHIFT                                                               0x1
#define CKSVII2C_IC_INTR_STAT__R_RX_FULL__SHIFT                                                               0x2
#define CKSVII2C_IC_INTR_STAT__R_TX_OVER__SHIFT                                                               0x3
#define CKSVII2C_IC_INTR_STAT__R_TX_EMPTY__SHIFT                                                              0x4
#define CKSVII2C_IC_INTR_STAT__R_RD_REQ__SHIFT                                                                0x5
#define CKSVII2C_IC_INTR_STAT__R_TX_ABRT__SHIFT                                                               0x6
#define CKSVII2C_IC_INTR_STAT__R_RX_DONE__SHIFT                                                               0x7
#define CKSVII2C_IC_INTR_STAT__R_ACTIVITY__SHIFT                                                              0x8
#define CKSVII2C_IC_INTR_STAT__R_STOP_DET__SHIFT                                                              0x9
#define CKSVII2C_IC_INTR_STAT__R_START_DET__SHIFT                                                             0xa
#define CKSVII2C_IC_INTR_STAT__R_GEN_CALL__SHIFT                                                              0xb
#define CKSVII2C_IC_INTR_STAT__R_RESTART_DET__SHIFT                                                           0xc
#define CKSVII2C_IC_INTR_STAT__R_MST_ON_HOLD__SHIFT                                                           0xd
#define CKSVII2C_IC_INTR_STAT__R_RX_UNDER_MASK                                                                0x00000001L
#define CKSVII2C_IC_INTR_STAT__R_RX_OVER_MASK                                                                 0x00000002L
#define CKSVII2C_IC_INTR_STAT__R_RX_FULL_MASK                                                                 0x00000004L
#define CKSVII2C_IC_INTR_STAT__R_TX_OVER_MASK                                                                 0x00000008L
#define CKSVII2C_IC_INTR_STAT__R_TX_EMPTY_MASK                                                                0x00000010L
#define CKSVII2C_IC_INTR_STAT__R_RD_REQ_MASK                                                                  0x00000020L
#define CKSVII2C_IC_INTR_STAT__R_TX_ABRT_MASK                                                                 0x00000040L
#define CKSVII2C_IC_INTR_STAT__R_RX_DONE_MASK                                                                 0x00000080L
#define CKSVII2C_IC_INTR_STAT__R_ACTIVITY_MASK                                                                0x00000100L
#define CKSVII2C_IC_INTR_STAT__R_STOP_DET_MASK                                                                0x00000200L
#define CKSVII2C_IC_INTR_STAT__R_START_DET_MASK                                                               0x00000400L
#define CKSVII2C_IC_INTR_STAT__R_GEN_CALL_MASK                                                                0x00000800L
#define CKSVII2C_IC_INTR_STAT__R_RESTART_DET_MASK                                                             0x00001000L
#define CKSVII2C_IC_INTR_STAT__R_MST_ON_HOLD_MASK                                                             0x00002000L
//CKSVII2C_IC_INTR_MASK
#define CKSVII2C_IC_INTR_MASK__M_RX_UNDER__SHIFT                                                              0x0
#define CKSVII2C_IC_INTR_MASK__M_RX_OVER__SHIFT                                                               0x1
#define CKSVII2C_IC_INTR_MASK__M_RX_FULL__SHIFT                                                               0x2
#define CKSVII2C_IC_INTR_MASK__M_TX_OVER__SHIFT                                                               0x3
#define CKSVII2C_IC_INTR_MASK__M_TX_EMPTY__SHIFT                                                              0x4
#define CKSVII2C_IC_INTR_MASK__M_RD_REQ__SHIFT                                                                0x5
#define CKSVII2C_IC_INTR_MASK__M_TX_ABRT__SHIFT                                                               0x6
#define CKSVII2C_IC_INTR_MASK__M_RX_DONE__SHIFT                                                               0x7
#define CKSVII2C_IC_INTR_MASK__M_ACTIVITY__SHIFT                                                              0x8
#define CKSVII2C_IC_INTR_MASK__M_STOP_DET__SHIFT                                                              0x9
#define CKSVII2C_IC_INTR_MASK__M_START_DET__SHIFT                                                             0xa
#define CKSVII2C_IC_INTR_MASK__M_GEN_CALL__SHIFT                                                              0xb
#define CKSVII2C_IC_INTR_MASK__M_RESTART_DET__SHIFT                                                           0xc
#define CKSVII2C_IC_INTR_MASK__M_MST_ON_HOLD__SHIFT                                                           0xd
#define CKSVII2C_IC_INTR_MASK__M_RX_UNDER_MASK                                                                0x00000001L
#define CKSVII2C_IC_INTR_MASK__M_RX_OVER_MASK                                                                 0x00000002L
#define CKSVII2C_IC_INTR_MASK__M_RX_FULL_MASK                                                                 0x00000004L
#define CKSVII2C_IC_INTR_MASK__M_TX_OVER_MASK                                                                 0x00000008L
#define CKSVII2C_IC_INTR_MASK__M_TX_EMPTY_MASK                                                                0x00000010L
#define CKSVII2C_IC_INTR_MASK__M_RD_REQ_MASK                                                                  0x00000020L
#define CKSVII2C_IC_INTR_MASK__M_TX_ABRT_MASK                                                                 0x00000040L
#define CKSVII2C_IC_INTR_MASK__M_RX_DONE_MASK                                                                 0x00000080L
#define CKSVII2C_IC_INTR_MASK__M_ACTIVITY_MASK                                                                0x00000100L
#define CKSVII2C_IC_INTR_MASK__M_STOP_DET_MASK                                                                0x00000200L
#define CKSVII2C_IC_INTR_MASK__M_START_DET_MASK                                                               0x00000400L
#define CKSVII2C_IC_INTR_MASK__M_GEN_CALL_MASK                                                                0x00000800L
#define CKSVII2C_IC_INTR_MASK__M_RESTART_DET_MASK                                                             0x00001000L
#define CKSVII2C_IC_INTR_MASK__M_MST_ON_HOLD_MASK                                                             0x00002000L
//CKSVII2C_IC_RAW_INTR_STAT
#define CKSVII2C_IC_RAW_INTR_STAT__R_RX_UNDER__SHIFT                                                              0x0
#define CKSVII2C_IC_RAW_INTR_STAT__R_RX_OVER__SHIFT                                                               0x1
#define CKSVII2C_IC_RAW_INTR_STAT__R_RX_FULL__SHIFT                                                               0x2
#define CKSVII2C_IC_RAW_INTR_STAT__R_TX_OVER__SHIFT                                                               0x3
#define CKSVII2C_IC__RAW_INTR_STAT__R_TX_EMPTY__SHIFT                                                              0x4
#define CKSVII2C_IC_RAW_INTR_STAT__R_RD_REQ__SHIFT                                                                0x5
#define CKSVII2C_IC_RAW_INTR_STAT__R_TX_ABRT__SHIFT                                                               0x6
#define CKSVII2C_IC_RAW_INTR_STAT__R_RX_DONE__SHIFT                                                               0x7
#define CKSVII2C_IC_RAW_INTR_STAT__R_ACTIVITY__SHIFT                                                              0x8
#define CKSVII2C_IC_RAW_INTR_STAT__R_STOP_DET__SHIFT                                                              0x9
#define CKSVII2C_IC_RAW_INTR_STAT__R_START_DET__SHIFT                                                             0xa
#define CKSVII2C_IC_RAW_INTR_STAT__R_GEN_CALL__SHIFT                                                              0xb
#define CKSVII2C_IC_RAW_INTR_STAT__R_RESTART_DET__SHIFT                                                           0xc
#define CKSVII2C_IC_RAW_INTR_STAT__R_MST_ON_HOLD__SHIFT                                                           0xd
#define CKSVII2C_IC_RAW_INTR_STAT__R_RX_UNDER_MASK                                                                0x00000001L
#define CKSVII2C_IC_RAW_INTR_STAT__R_RX_OVER_MASK                                                                 0x00000002L
#define CKSVII2C_IC_RAW_INTR_STAT__R_RX_FULL_MASK                                                                 0x00000004L
#define CKSVII2C_IC_RAW_INTR_STAT__R_TX_OVER_MASK                                                                 0x00000008L
#define CKSVII2C_IC_RAW_INTR_STAT__R_TX_EMPTY_MASK                                                                0x00000010L
#define CKSVII2C_IC_RAW_INTR_STAT__R_RD_REQ_MASK                                                                  0x00000020L
#define CKSVII2C_IC_RAW_INTR_STAT__R_TX_ABRT_MASK                                                                 0x00000040L
#define CKSVII2C_IC_RAW_INTR_STAT__R_RX_DONE_MASK                                                                 0x00000080L
#define CKSVII2C_IC_RAW_INTR_STAT__R_ACTIVITY_MASK                                                                0x00000100L
#define CKSVII2C_IC_RAW_INTR_STAT__R_STOP_DET_MASK                                                                0x00000200L
#define CKSVII2C_IC_RAW_INTR_STAT__R_START_DET_MASK                                                               0x00000400L
#define CKSVII2C_IC_RAW_INTR_STAT__R_GEN_CALL_MASK                                                                0x00000800L
#define CKSVII2C_IC_RAW_INTR_STAT__R_RESTART_DET_MASK                                                             0x00001000L
#define CKSVII2C_IC_RAW_INTR_STAT__R_MST_ON_HOLD_MASK                                                             0x00002000L
//CKSVII2C_IC_RX_TL
//CKSVII2C_IC_TX_TL
//CKSVII2C_IC_CLR_INTR
//CKSVII2C_IC_CLR_RX_UNDER
//CKSVII2C_IC_CLR_RX_OVER
//CKSVII2C_IC_CLR_TX_OVER
//CKSVII2C_IC_CLR_RD_REQ
//CKSVII2C_IC_CLR_TX_ABRT
//CKSVII2C_IC_CLR_RX_DONE
//CKSVII2C_IC_CLR_ACTIVITY
#define CKSVII2C_IC_CLR_ACTIVITY__CLR_ACTIVITY__SHIFT                                                         0x0
#define CKSVII2C_IC_CLR_ACTIVITY__CLR_ACTIVITY_MASK                                                           0x00000001L
//CKSVII2C_IC_CLR_STOP_DET
//CKSVII2C_IC_CLR_START_DET
//CKSVII2C_IC_CLR_GEN_CALL
//CKSVII2C_IC_ENABLE
#define CKSVII2C_IC_ENABLE__ENABLE__SHIFT                                                                     0x0
#define CKSVII2C_IC_ENABLE__ABORT__SHIFT                                                                      0x1
#define CKSVII2C_IC_ENABLE__ENABLE_MASK                                                                       0x00000001L
#define CKSVII2C_IC_ENABLE__ABORT_MASK                                                                        0x00000002L
//CKSVII2C_IC_STATUS
#define CKSVII2C_IC_STATUS__ACTIVITY__SHIFT                                                                   0x0
#define CKSVII2C_IC_STATUS__TFNF__SHIFT                                                                       0x1
#define CKSVII2C_IC_STATUS__TFE__SHIFT                                                                        0x2
#define CKSVII2C_IC_STATUS__RFNE__SHIFT                                                                       0x3
#define CKSVII2C_IC_STATUS__RFF__SHIFT                                                                        0x4
#define CKSVII2C_IC_STATUS__MST_ACTIVITY__SHIFT                                                               0x5
#define CKSVII2C_IC_STATUS__SLV_ACTIVITY__SHIFT                                                               0x6
#define CKSVII2C_IC_STATUS__ACTIVITY_MASK                                                                     0x00000001L
#define CKSVII2C_IC_STATUS__TFNF_MASK                                                                         0x00000002L
#define CKSVII2C_IC_STATUS__TFE_MASK                                                                          0x00000004L
#define CKSVII2C_IC_STATUS__RFNE_MASK                                                                         0x00000008L
#define CKSVII2C_IC_STATUS__RFF_MASK                                                                          0x00000010L
#define CKSVII2C_IC_STATUS__MST_ACTIVITY_MASK                                                                 0x00000020L
#define CKSVII2C_IC_STATUS__SLV_ACTIVITY_MASK                                                                 0x00000040L
//CKSVII2C_IC_TXFLR
//CKSVII2C_IC_RXFLR
//CKSVII2C_IC_SDA_HOLD
#define CKSVII2C_IC_SDA_HOLD__IC_SDA_HOLD__SHIFT                                                              0x0
#define CKSVII2C_IC_SDA_HOLD__IC_SDA_HOLD_MASK                                                                0x00FFFFFFL
//CKSVII2C_IC_TX_ABRT_SOURCE

#define CKSVII2C_IC_TX_ABRT_SOURCE__ABRT_7B_ADDR_NOACK__SHIFT                                                  0x0
#define CKSVII2C_IC_TX_ABRT_SOURCE__ABRT_10ADDR1_NOACK__SHIFT                                                  0x1
#define CKSVII2C_IC_TX_ABRT_SOURCE__ABRT_10ADDR2_NOACK__SHIFT                                                  0x2
#define CKSVII2C_IC_TX_ABRT_SOURCE__ABRT_TXDATA_NOACK__SHIFT                                                   0x3
#define CKSVII2C_IC_TX_ABRT_SOURCE__ABRT_7B_ADDR_NOACK_MASK                                                   0x00000001L
#define CKSVII2C_IC_TX_ABRT_SOURCE__ABRT_10ADDR1_NOACK_MASK                                                   0x00000002L
#define CKSVII2C_IC_TX_ABRT_SOURCE__ABRT_10ADDR2_NOACK_MASK                                                   0x00000004L
#define CKSVII2C_IC_TX_ABRT_SOURCE__ABRT_TXDATA_NOACK_MASK                                                    0x00000008L
//CKSVII2C_IC_SLV_DATA_NACK_ONLY
//CKSVII2C_IC_DMA_CR
//CKSVII2C_IC_DMA_TDLR
//CKSVII2C_IC_DMA_RDLR
//CKSVII2C_IC_SDA_SETUP
#define CKSVII2C_IC_SDA_SETUP__SDA_SETUP__SHIFT                                                               0x0
#define CKSVII2C_IC_SDA_SETUP__SDA_SETUP_MASK                                                                 0x000000FFL
//CKSVII2C_IC_ACK_GENERAL_CALL
#define CKSVII2C_IC_ACK_GENERAL_CALL__ACK_GENERAL_CALL__SHIFT                                                 0x0
#define CKSVII2C_IC_ACK_GENERAL_CALL__ACK_GENERAL_CALL_MASK                                                   0x00000001L
//CKSVII2C_IC_ENABLE_STATUS
#define CKSVII2C_IC_ENABLE_STATUS__IC_EN__SHIFT                                                               0x0
#define CKSVII2C_IC_ENABLE_STATUS__SLV_RX_ABORTED__SHIFT                                                      0x1
#define CKSVII2C_IC_ENABLE_STATUS__SLV_FIFO_FILLED_AND_FLUSHED__SHIFT                                         0x2
#define CKSVII2C_IC_ENABLE_STATUS__IC_EN_MASK                                                                 0x00000001L
#define CKSVII2C_IC_ENABLE_STATUS__SLV_RX_ABORTED_MASK                                                        0x00000002L
#define CKSVII2C_IC_ENABLE_STATUS__SLV_FIFO_FILLED_AND_FLUSHED_MASK                                           0x00000004L
//CKSVII2C_IC_FS_SPKLEN
#define CKSVII2C_IC_FS_SPKLEN__FS_SPKLEN__SHIFT                                                               0x0
#define CKSVII2C_IC_FS_SPKLEN__FS_SPKLEN_MASK                                                                 0x000000FFL
//CKSVII2C_IC_HS_SPKLEN
#define CKSVII2C_IC_HS_SPKLEN__HS_SPKLEN__SHIFT                                                               0x0
#define CKSVII2C_IC_HS_SPKLEN__HS_SPKLEN_MASK                                                                 0x000000FFL
//CKSVII2C_IC_CLR_RESTART_DET
//CKSVII2C_IC_COMP_PARAM_1
#define CKSVII2C_IC_COMP_PARAM_1__COMP_PARAM_1__SHIFT                                                         0x0
#define CKSVII2C_IC_COMP_PARAM_1__COMP_PARAM_1_MASK                                                           0xFFFFFFFFL
//CKSVII2C_IC_COMP_VERSION
#define CKSVII2C_IC_COMP_VERSION__COMP_VERSION__SHIFT                                                         0x0
#define CKSVII2C_IC_COMP_VERSION__COMP_VERSION_MASK                                                           0xFFFFFFFFL
//CKSVII2C_IC_COMP_TYPE
#define CKSVII2C_IC_COMP_TYPE__COMP_TYPE__SHIFT                                                               0x0
#define CKSVII2C_IC_COMP_TYPE__COMP_TYPE_MASK                                                                 0xFFFFFFFFL
//CKSVII2C1_IC_CON
#define CKSVII2C1_IC_CON__IC1_MASTER_MODE__SHIFT                                                              0x0
#define CKSVII2C1_IC_CON__IC1_MAX_SPEED_MODE__SHIFT                                                           0x1
#define CKSVII2C1_IC_CON__IC1_10BITADDR_SLAVE__SHIFT                                                          0x3
#define CKSVII2C1_IC_CON__IC1_10BITADDR_MASTER__SHIFT                                                         0x4
#define CKSVII2C1_IC_CON__IC1_RESTART_EN__SHIFT                                                               0x5
#define CKSVII2C1_IC_CON__IC1_SLAVE_DISABLE__SHIFT                                                            0x6
#define CKSVII2C1_IC_CON__STOP1_DET_IFADDRESSED__SHIFT                                                        0x7
#define CKSVII2C1_IC_CON__TX1_EMPTY_CTRL__SHIFT                                                               0x8
#define CKSVII2C1_IC_CON__RX1_FIFO_FULL_HLD_CTRL__SHIFT                                                       0x9
#define CKSVII2C1_IC_CON__IC1_MASTER_MODE_MASK                                                                0x00000001L
#define CKSVII2C1_IC_CON__IC1_MAX_SPEED_MODE_MASK                                                             0x00000006L
#define CKSVII2C1_IC_CON__IC1_10BITADDR_SLAVE_MASK                                                            0x00000008L
#define CKSVII2C1_IC_CON__IC1_10BITADDR_MASTER_MASK                                                           0x00000010L
#define CKSVII2C1_IC_CON__IC1_RESTART_EN_MASK                                                                 0x00000020L
#define CKSVII2C1_IC_CON__IC1_SLAVE_DISABLE_MASK                                                              0x00000040L
#define CKSVII2C1_IC_CON__STOP1_DET_IFADDRESSED_MASK                                                          0x00000080L
#define CKSVII2C1_IC_CON__TX1_EMPTY_CTRL_MASK                                                                 0x00000100L
#define CKSVII2C1_IC_CON__RX1_FIFO_FULL_HLD_CTRL_MASK                                                         0x00000200L
//CKSVII2C1_IC_TAR
#define CKSVII2C1_IC_TAR__IC1_TAR__SHIFT                                                                      0x0
#define CKSVII2C1_IC_TAR__GC1_OR_START__SHIFT                                                                 0xa
#define CKSVII2C1_IC_TAR__SPECIAL1__SHIFT                                                                     0xb
#define CKSVII2C1_IC_TAR__IC1_10BITADDR_MASTER__SHIFT                                                         0xc
#define CKSVII2C1_IC_TAR__IC1_TAR_MASK                                                                        0x000003FFL
#define CKSVII2C1_IC_TAR__GC1_OR_START_MASK                                                                   0x00000400L
#define CKSVII2C1_IC_TAR__SPECIAL1_MASK                                                                       0x00000800L
#define CKSVII2C1_IC_TAR__IC1_10BITADDR_MASTER_MASK                                                           0x00001000L
//CKSVII2C1_IC_SAR
#define CKSVII2C1_IC_SAR__IC1_SAR__SHIFT                                                                      0x0
#define CKSVII2C1_IC_SAR__IC1_SAR_MASK                                                                        0x000003FFL
//CKSVII2C1_IC_HS_MADDR
#define CKSVII2C1_IC_HS_MADDR__IC1_HS_MADDR__SHIFT                                                            0x0
#define CKSVII2C1_IC_HS_MADDR__IC1_HS_MADDR_MASK                                                              0x00000007L
//CKSVII2C1_IC_DATA_CMD
#define CKSVII2C1_IC_DATA_CMD__DAT1__SHIFT                                                                    0x0
#define CKSVII2C1_IC_DATA_CMD__CMD1__SHIFT                                                                    0x8
#define CKSVII2C1_IC_DATA_CMD__STOP1__SHIFT                                                                   0x9
#define CKSVII2C1_IC_DATA_CMD__RESTART1__SHIFT                                                                0xa
#define CKSVII2C1_IC_DATA_CMD__DAT1_MASK                                                                      0x000000FFL
#define CKSVII2C1_IC_DATA_CMD__CMD1_MASK                                                                      0x00000100L
#define CKSVII2C1_IC_DATA_CMD__STOP1_MASK                                                                     0x00000200L
#define CKSVII2C1_IC_DATA_CMD__RESTART1_MASK                                                                  0x00000400L
//CKSVII2C1_IC_SS_SCL_HCNT
#define CKSVII2C1_IC_SS_SCL_HCNT__IC1_SS_SCL_HCNT__SHIFT                                                      0x0
#define CKSVII2C1_IC_SS_SCL_HCNT__IC1_SS_SCL_HCNT_MASK                                                        0x0000FFFFL
//CKSVII2C1_IC_SS_SCL_LCNT
#define CKSVII2C1_IC_SS_SCL_LCNT__IC1_SS_SCL_LCNT__SHIFT                                                      0x0
#define CKSVII2C1_IC_SS_SCL_LCNT__IC1_SS_SCL_LCNT_MASK                                                        0x0000FFFFL
//CKSVII2C1_IC_FS_SCL_HCNT
#define CKSVII2C1_IC_FS_SCL_HCNT__IC1_FS_SCL_HCNT__SHIFT                                                      0x0
#define CKSVII2C1_IC_FS_SCL_HCNT__IC1_FS_SCL_HCNT_MASK                                                        0x0000FFFFL
//CKSVII2C1_IC_FS_SCL_LCNT
#define CKSVII2C1_IC_FS_SCL_LCNT__IC1_FS_SCL_LCNT__SHIFT                                                      0x0
#define CKSVII2C1_IC_FS_SCL_LCNT__IC1_FS_SCL_LCNT_MASK                                                        0x0000FFFFL
//CKSVII2C1_IC_HS_SCL_HCNT
#define CKSVII2C1_IC_HS_SCL_HCNT__IC1_HS_SCL_HCNT__SHIFT                                                      0x0
#define CKSVII2C1_IC_HS_SCL_HCNT__IC1_HS_SCL_HCNT_MASK                                                        0x0000FFFFL
//CKSVII2C1_IC_HS_SCL_LCNT
#define CKSVII2C1_IC_HS_SCL_LCNT__IC1_HS_SCL_LCNT__SHIFT                                                      0x0
#define CKSVII2C1_IC_HS_SCL_LCNT__IC1_HS_SCL_LCNT_MASK                                                        0x0000FFFFL
//CKSVII2C1_IC_INTR_STAT
#define CKSVII2C1_IC_INTR_STAT__R1_RX_UNDER__SHIFT                                                            0x0
#define CKSVII2C1_IC_INTR_STAT__R1_RX_OVER__SHIFT                                                             0x1
#define CKSVII2C1_IC_INTR_STAT__R1_RX_FULL__SHIFT                                                             0x2
#define CKSVII2C1_IC_INTR_STAT__R1_TX_OVER__SHIFT                                                             0x3
#define CKSVII2C1_IC_INTR_STAT__R1_TX_EMPTY__SHIFT                                                            0x4
#define CKSVII2C1_IC_INTR_STAT__R1_RD_REQ__SHIFT                                                              0x5
#define CKSVII2C1_IC_INTR_STAT__R1_TX_ABRT__SHIFT                                                             0x6
#define CKSVII2C1_IC_INTR_STAT__R1_RX_DONE__SHIFT                                                             0x7
#define CKSVII2C1_IC_INTR_STAT__R1_ACTIVITY__SHIFT                                                            0x8
#define CKSVII2C1_IC_INTR_STAT__R1_STOP_DET__SHIFT                                                            0x9
#define CKSVII2C1_IC_INTR_STAT__R1_START_DET__SHIFT                                                           0xa
#define CKSVII2C1_IC_INTR_STAT__R1_GEN_CALL__SHIFT                                                            0xb
#define CKSVII2C1_IC_INTR_STAT__R1_RESTART_DET__SHIFT                                                         0xc
#define CKSVII2C1_IC_INTR_STAT__R1_MST_ON_HOLD__SHIFT                                                         0xd
#define CKSVII2C1_IC_INTR_STAT__R1_RX_UNDER_MASK                                                              0x00000001L
#define CKSVII2C1_IC_INTR_STAT__R1_RX_OVER_MASK                                                               0x00000002L
#define CKSVII2C1_IC_INTR_STAT__R1_RX_FULL_MASK                                                               0x00000004L
#define CKSVII2C1_IC_INTR_STAT__R1_TX_OVER_MASK                                                               0x00000008L
#define CKSVII2C1_IC_INTR_STAT__R1_TX_EMPTY_MASK                                                              0x00000010L
#define CKSVII2C1_IC_INTR_STAT__R1_RD_REQ_MASK                                                                0x00000020L
#define CKSVII2C1_IC_INTR_STAT__R1_TX_ABRT_MASK                                                               0x00000040L
#define CKSVII2C1_IC_INTR_STAT__R1_RX_DONE_MASK                                                               0x00000080L
#define CKSVII2C1_IC_INTR_STAT__R1_ACTIVITY_MASK                                                              0x00000100L
#define CKSVII2C1_IC_INTR_STAT__R1_STOP_DET_MASK                                                              0x00000200L
#define CKSVII2C1_IC_INTR_STAT__R1_START_DET_MASK                                                             0x00000400L
#define CKSVII2C1_IC_INTR_STAT__R1_GEN_CALL_MASK                                                              0x00000800L
#define CKSVII2C1_IC_INTR_STAT__R1_RESTART_DET_MASK                                                           0x00001000L
#define CKSVII2C1_IC_INTR_STAT__R1_MST_ON_HOLD_MASK                                                           0x00002000L
//CKSVII2C1_IC_INTR_MASK
#define CKSVII2C1_IC_INTR_MASK__M1_RX_UNDER__SHIFT                                                            0x0
#define CKSVII2C1_IC_INTR_MASK__M1_RX_OVER__SHIFT                                                             0x1
#define CKSVII2C1_IC_INTR_MASK__M1_RX_FULL__SHIFT                                                             0x2
#define CKSVII2C1_IC_INTR_MASK__M1_TX_OVER__SHIFT                                                             0x3
#define CKSVII2C1_IC_INTR_MASK__M1_TX_EMPTY__SHIFT                                                            0x4
#define CKSVII2C1_IC_INTR_MASK__M1_RD_REQ__SHIFT                                                              0x5
#define CKSVII2C1_IC_INTR_MASK__M1_TX_ABRT__SHIFT                                                             0x6
#define CKSVII2C1_IC_INTR_MASK__M1_RX_DONE__SHIFT                                                             0x7
#define CKSVII2C1_IC_INTR_MASK__M1_ACTIVITY__SHIFT                                                            0x8
#define CKSVII2C1_IC_INTR_MASK__M1_STOP_DET__SHIFT                                                            0x9
#define CKSVII2C1_IC_INTR_MASK__M1_START_DET__SHIFT                                                           0xa
#define CKSVII2C1_IC_INTR_MASK__M1_GEN_CALL__SHIFT                                                            0xb
#define CKSVII2C1_IC_INTR_MASK__M1_RESTART_DET__SHIFT                                                         0xc
#define CKSVII2C1_IC_INTR_MASK__M1_MST_ON_HOLD__SHIFT                                                         0xd
#define CKSVII2C1_IC_INTR_MASK__M1_RX_UNDER_MASK                                                              0x00000001L
#define CKSVII2C1_IC_INTR_MASK__M1_RX_OVER_MASK                                                               0x00000002L
#define CKSVII2C1_IC_INTR_MASK__M1_RX_FULL_MASK                                                               0x00000004L
#define CKSVII2C1_IC_INTR_MASK__M1_TX_OVER_MASK                                                               0x00000008L
#define CKSVII2C1_IC_INTR_MASK__M1_TX_EMPTY_MASK                                                              0x00000010L
#define CKSVII2C1_IC_INTR_MASK__M1_RD_REQ_MASK                                                                0x00000020L
#define CKSVII2C1_IC_INTR_MASK__M1_TX_ABRT_MASK                                                               0x00000040L
#define CKSVII2C1_IC_INTR_MASK__M1_RX_DONE_MASK                                                               0x00000080L
#define CKSVII2C1_IC_INTR_MASK__M1_ACTIVITY_MASK                                                              0x00000100L
#define CKSVII2C1_IC_INTR_MASK__M1_STOP_DET_MASK                                                              0x00000200L
#define CKSVII2C1_IC_INTR_MASK__M1_START_DET_MASK                                                             0x00000400L
#define CKSVII2C1_IC_INTR_MASK__M1_GEN_CALL_MASK                                                              0x00000800L
#define CKSVII2C1_IC_INTR_MASK__M1_RESTART_DET_MASK                                                           0x00001000L
#define CKSVII2C1_IC_INTR_MASK__M1_MST_ON_HOLD_MASK                                                           0x00002000L
//CKSVII2C1_IC_RAW_INTR_STAT
//CKSVII2C1_IC_RX_TL
//CKSVII2C1_IC_TX_TL
//CKSVII2C1_IC_CLR_INTR
//CKSVII2C1_IC_CLR_RX_UNDER
//CKSVII2C1_IC_CLR_RX_OVER
//CKSVII2C1_IC_CLR_TX_OVER
//CKSVII2C1_IC_CLR_RD_REQ
//CKSVII2C1_IC_CLR_TX_ABRT
//CKSVII2C1_IC_CLR_RX_DONE
//CKSVII2C1_IC_CLR_ACTIVITY
//CKSVII2C1_IC_CLR_STOP_DET
//CKSVII2C1_IC_CLR_START_DET
//CKSVII2C1_IC_CLR_GEN_CALL
//CKSVII2C1_IC_ENABLE
#define CKSVII2C1_IC_ENABLE__ENABLE1__SHIFT                                                                   0x0
#define CKSVII2C1_IC_ENABLE__ABORT1__SHIFT                                                                    0x1
#define CKSVII2C1_IC_ENABLE__ENABLE1_MASK                                                                     0x00000001L
#define CKSVII2C1_IC_ENABLE__ABORT1_MASK                                                                      0x00000002L
//CKSVII2C1_IC_STATUS
#define CKSVII2C1_IC_STATUS__ACTIVITY1__SHIFT                                                                 0x0
#define CKSVII2C1_IC_STATUS__TFNF1__SHIFT                                                                     0x1
#define CKSVII2C1_IC_STATUS__TFE1__SHIFT                                                                      0x2
#define CKSVII2C1_IC_STATUS__RFNE1__SHIFT                                                                     0x3
#define CKSVII2C1_IC_STATUS__RFF1__SHIFT                                                                      0x4
#define CKSVII2C1_IC_STATUS__MST1_ACTIVITY__SHIFT                                                             0x5
#define CKSVII2C1_IC_STATUS__SLV1_ACTIVITY__SHIFT                                                             0x6
#define CKSVII2C1_IC_STATUS__ACTIVITY1_MASK                                                                   0x00000001L
#define CKSVII2C1_IC_STATUS__TFNF1_MASK                                                                       0x00000002L
#define CKSVII2C1_IC_STATUS__TFE1_MASK                                                                        0x00000004L
#define CKSVII2C1_IC_STATUS__RFNE1_MASK                                                                       0x00000008L
#define CKSVII2C1_IC_STATUS__RFF1_MASK                                                                        0x00000010L
#define CKSVII2C1_IC_STATUS__MST1_ACTIVITY_MASK                                                               0x00000020L
#define CKSVII2C1_IC_STATUS__SLV1_ACTIVITY_MASK                                                               0x00000040L
//CKSVII2C1_IC_TXFLR
//CKSVII2C1_IC_RXFLR
//CKSVII2C1_IC_SDA_HOLD
#define CKSVII2C1_IC_SDA_HOLD__IC1_SDA_HOLD__SHIFT                                                            0x0
#define CKSVII2C1_IC_SDA_HOLD__IC1_SDA_HOLD_MASK                                                              0x00FFFFFFL
//CKSVII2C1_IC_TX_ABRT_SOURCE
//CKSVII2C1_IC_SLV_DATA_NACK_ONLY
//CKSVII2C1_IC_DMA_CR
//CKSVII2C1_IC_DMA_TDLR
//CKSVII2C1_IC_DMA_RDLR
//CKSVII2C1_IC_SDA_SETUP
#define CKSVII2C1_IC_SDA_SETUP__SDA1_SETUP__SHIFT                                                             0x0
#define CKSVII2C1_IC_SDA_SETUP__SDA1_SETUP_MASK                                                               0x000000FFL
//CKSVII2C1_IC_ACK_GENERAL_CALL
#define CKSVII2C1_IC_ACK_GENERAL_CALL__ACK1_GENERAL_CALL__SHIFT                                               0x0
#define CKSVII2C1_IC_ACK_GENERAL_CALL__ACK1_GENERAL_CALL_MASK                                                 0x00000001L
//CKSVII2C1_IC_ENABLE_STATUS
#define CKSVII2C1_IC_ENABLE_STATUS__IC1_EN__SHIFT                                                             0x0
#define CKSVII2C1_IC_ENABLE_STATUS__SLV1_RX_ABORTED__SHIFT                                                    0x1
#define CKSVII2C1_IC_ENABLE_STATUS__SLV1_FIFO_FILLED_AND_FLUSHED__SHIFT                                       0x2
#define CKSVII2C1_IC_ENABLE_STATUS__IC1_EN_MASK                                                               0x00000001L
#define CKSVII2C1_IC_ENABLE_STATUS__SLV1_RX_ABORTED_MASK                                                      0x00000002L
#define CKSVII2C1_IC_ENABLE_STATUS__SLV1_FIFO_FILLED_AND_FLUSHED_MASK                                         0x00000004L
//SMUIO_MP_RESET_INTR
#define SMUIO_MP_RESET_INTR__SMUIO_MP_RESET_INTR__SHIFT                                                       0x0
#define SMUIO_MP_RESET_INTR__SMUIO_MP_RESET_INTR_MASK                                                         0x00000001L
//SMUIO_SOC_HALT
#define SMUIO_SOC_HALT__WDT_FORCE_PWROK_EN__SHIFT                                                             0x2
#define SMUIO_SOC_HALT__WDT_FORCE_RESETn_EN__SHIFT                                                            0x3
#define SMUIO_SOC_HALT__WDT_FORCE_PWROK_EN_MASK                                                               0x00000004L
#define SMUIO_SOC_HALT__WDT_FORCE_RESETn_EN_MASK                                                              0x00000008L
//SMUIO_PWRMGT
#define SMUIO_PWRMGT__i2c_clk_gate_en__SHIFT                                                                  0x0
#define SMUIO_PWRMGT__i2c1_clk_gate_en__SHIFT                                                                 0x4
#define SMUIO_PWRMGT__i2c_clk_gate_en_MASK                                                                    0x00000001L
#define SMUIO_PWRMGT__i2c1_clk_gate_en_MASK                                                                   0x00000010L
//ROM_CNTL
#define ROM_CNTL__CLOCK_GATING_EN__SHIFT                                                                      0x0
#define ROM_CNTL__SPI_TIMING_RELAX__SHIFT                                                                     0x14
#define ROM_CNTL__SPI_TIMING_RELAX_OVERRIDE__SHIFT                                                            0x15
#define ROM_CNTL__SPI_FAST_MODE__SHIFT                                                                        0x16
#define ROM_CNTL__SPI_FAST_MODE_OVERRIDE__SHIFT                                                               0x17
#define ROM_CNTL__SCK_PRESCALE_REFCLK__SHIFT                                                                  0x18
#define ROM_CNTL__SCK_PRESCALE_REFCLK_OVERRIDE__SHIFT                                                         0x1c
#define ROM_CNTL__CLOCK_GATING_EN_MASK                                                                        0x00000001L
#define ROM_CNTL__SPI_TIMING_RELAX_MASK                                                                       0x00100000L
#define ROM_CNTL__SPI_TIMING_RELAX_OVERRIDE_MASK                                                              0x00200000L
#define ROM_CNTL__SPI_FAST_MODE_MASK                                                                          0x00400000L
#define ROM_CNTL__SPI_FAST_MODE_OVERRIDE_MASK                                                                 0x00800000L
#define ROM_CNTL__SCK_PRESCALE_REFCLK_MASK                                                                    0x0F000000L
#define ROM_CNTL__SCK_PRESCALE_REFCLK_OVERRIDE_MASK                                                           0x10000000L
//PAGE_MIRROR_CNTL
#define PAGE_MIRROR_CNTL__PAGE_MIRROR_BASE_ADDR__SHIFT                                                        0x0
#define PAGE_MIRROR_CNTL__PAGE_MIRROR_INVALIDATE__SHIFT                                                       0x18
#define PAGE_MIRROR_CNTL__PAGE_MIRROR_ENABLE__SHIFT                                                           0x19
#define PAGE_MIRROR_CNTL__PAGE_MIRROR_USAGE__SHIFT                                                            0x1a
#define PAGE_MIRROR_CNTL__PAGE_MIRROR_BASE_ADDR_MASK                                                          0x00FFFFFFL
#define PAGE_MIRROR_CNTL__PAGE_MIRROR_INVALIDATE_MASK                                                         0x01000000L
#define PAGE_MIRROR_CNTL__PAGE_MIRROR_ENABLE_MASK                                                             0x02000000L
#define PAGE_MIRROR_CNTL__PAGE_MIRROR_USAGE_MASK                                                              0x0C000000L
//ROM_STATUS
#define ROM_STATUS__ROM_BUSY__SHIFT                                                                           0x0
#define ROM_STATUS__ROM_BUSY_MASK                                                                             0x00000001L
//CGTT_ROM_CLK_CTRL0
#define CGTT_ROM_CLK_CTRL0__ON_DELAY__SHIFT                                                                   0x0
#define CGTT_ROM_CLK_CTRL0__OFF_HYSTERESIS__SHIFT                                                             0x4
#define CGTT_ROM_CLK_CTRL0__SOFT_OVERRIDE1__SHIFT                                                             0x1e
#define CGTT_ROM_CLK_CTRL0__SOFT_OVERRIDE0__SHIFT                                                             0x1f
#define CGTT_ROM_CLK_CTRL0__ON_DELAY_MASK                                                                     0x0000000FL
#define CGTT_ROM_CLK_CTRL0__OFF_HYSTERESIS_MASK                                                               0x00000FF0L
#define CGTT_ROM_CLK_CTRL0__SOFT_OVERRIDE1_MASK                                                               0x40000000L
#define CGTT_ROM_CLK_CTRL0__SOFT_OVERRIDE0_MASK                                                               0x80000000L
//ROM_INDEX
#define ROM_INDEX__ROM_INDEX__SHIFT                                                                           0x0
#define ROM_INDEX__ROM_INDEX_MASK                                                                             0x00FFFFFFL
//ROM_DATA
#define ROM_DATA__ROM_DATA__SHIFT                                                                             0x0
#define ROM_DATA__ROM_DATA_MASK                                                                               0xFFFFFFFFL
//ROM_START
#define ROM_START__ROM_START__SHIFT                                                                           0x0
#define ROM_START__ROM_START_MASK                                                                             0x00FFFFFFL
//ROM_SW_CNTL
#define ROM_SW_CNTL__DATA_SIZE__SHIFT                                                                         0x0
#define ROM_SW_CNTL__COMMAND_SIZE__SHIFT                                                                      0x10
#define ROM_SW_CNTL__ROM_SW_RETURN_DATA_ENABLE__SHIFT                                                         0x12
#define ROM_SW_CNTL__DATA_SIZE_MASK                                                                           0x0000FFFFL
#define ROM_SW_CNTL__COMMAND_SIZE_MASK                                                                        0x00030000L
#define ROM_SW_CNTL__ROM_SW_RETURN_DATA_ENABLE_MASK                                                           0x00040000L
//ROM_SW_STATUS
#define ROM_SW_STATUS__ROM_SW_DONE__SHIFT                                                                     0x0
#define ROM_SW_STATUS__ROM_SW_DONE_MASK                                                                       0x00000001L
//ROM_SW_COMMAND
#define ROM_SW_COMMAND__ROM_SW_INSTRUCTION__SHIFT                                                             0x0
#define ROM_SW_COMMAND__ROM_SW_ADDRESS__SHIFT                                                                 0x8
#define ROM_SW_COMMAND__ROM_SW_INSTRUCTION_MASK                                                               0x000000FFL
#define ROM_SW_COMMAND__ROM_SW_ADDRESS_MASK                                                                   0xFFFFFF00L
//ROM_SW_DATA_1
#define ROM_SW_DATA_1__ROM_SW_DATA__SHIFT                                                                     0x0
#define ROM_SW_DATA_1__ROM_SW_DATA_MASK                                                                       0xFFFFFFFFL
//ROM_SW_DATA_2
#define ROM_SW_DATA_2__ROM_SW_DATA__SHIFT                                                                     0x0
#define ROM_SW_DATA_2__ROM_SW_DATA_MASK                                                                       0xFFFFFFFFL
//ROM_SW_DATA_3
#define ROM_SW_DATA_3__ROM_SW_DATA__SHIFT                                                                     0x0
#define ROM_SW_DATA_3__ROM_SW_DATA_MASK                                                                       0xFFFFFFFFL
//ROM_SW_DATA_4
#define ROM_SW_DATA_4__ROM_SW_DATA__SHIFT                                                                     0x0
#define ROM_SW_DATA_4__ROM_SW_DATA_MASK                                                                       0xFFFFFFFFL
//ROM_SW_DATA_5
#define ROM_SW_DATA_5__ROM_SW_DATA__SHIFT                                                                     0x0
#define ROM_SW_DATA_5__ROM_SW_DATA_MASK                                                                       0xFFFFFFFFL
//ROM_SW_DATA_6
#define ROM_SW_DATA_6__ROM_SW_DATA__SHIFT                                                                     0x0
#define ROM_SW_DATA_6__ROM_SW_DATA_MASK                                                                       0xFFFFFFFFL
//ROM_SW_DATA_7
#define ROM_SW_DATA_7__ROM_SW_DATA__SHIFT                                                                     0x0
#define ROM_SW_DATA_7__ROM_SW_DATA_MASK                                                                       0xFFFFFFFFL
//ROM_SW_DATA_8
#define ROM_SW_DATA_8__ROM_SW_DATA__SHIFT                                                                     0x0
#define ROM_SW_DATA_8__ROM_SW_DATA_MASK                                                                       0xFFFFFFFFL
//ROM_SW_DATA_9
#define ROM_SW_DATA_9__ROM_SW_DATA__SHIFT                                                                     0x0
#define ROM_SW_DATA_9__ROM_SW_DATA_MASK                                                                       0xFFFFFFFFL
//ROM_SW_DATA_10
#define ROM_SW_DATA_10__ROM_SW_DATA__SHIFT                                                                    0x0
#define ROM_SW_DATA_10__ROM_SW_DATA_MASK                                                                      0xFFFFFFFFL
//ROM_SW_DATA_11
#define ROM_SW_DATA_11__ROM_SW_DATA__SHIFT                                                                    0x0
#define ROM_SW_DATA_11__ROM_SW_DATA_MASK                                                                      0xFFFFFFFFL
//ROM_SW_DATA_12
#define ROM_SW_DATA_12__ROM_SW_DATA__SHIFT                                                                    0x0
#define ROM_SW_DATA_12__ROM_SW_DATA_MASK                                                                      0xFFFFFFFFL
//ROM_SW_DATA_13
#define ROM_SW_DATA_13__ROM_SW_DATA__SHIFT                                                                    0x0
#define ROM_SW_DATA_13__ROM_SW_DATA_MASK                                                                      0xFFFFFFFFL
//ROM_SW_DATA_14
#define ROM_SW_DATA_14__ROM_SW_DATA__SHIFT                                                                    0x0
#define ROM_SW_DATA_14__ROM_SW_DATA_MASK                                                                      0xFFFFFFFFL
//ROM_SW_DATA_15
#define ROM_SW_DATA_15__ROM_SW_DATA__SHIFT                                                                    0x0
#define ROM_SW_DATA_15__ROM_SW_DATA_MASK                                                                      0xFFFFFFFFL
//ROM_SW_DATA_16
#define ROM_SW_DATA_16__ROM_SW_DATA__SHIFT                                                                    0x0
#define ROM_SW_DATA_16__ROM_SW_DATA_MASK                                                                      0xFFFFFFFFL
//ROM_SW_DATA_17
#define ROM_SW_DATA_17__ROM_SW_DATA__SHIFT                                                                    0x0
#define ROM_SW_DATA_17__ROM_SW_DATA_MASK                                                                      0xFFFFFFFFL
//ROM_SW_DATA_18
#define ROM_SW_DATA_18__ROM_SW_DATA__SHIFT                                                                    0x0
#define ROM_SW_DATA_18__ROM_SW_DATA_MASK                                                                      0xFFFFFFFFL
//ROM_SW_DATA_19
#define ROM_SW_DATA_19__ROM_SW_DATA__SHIFT                                                                    0x0
#define ROM_SW_DATA_19__ROM_SW_DATA_MASK                                                                      0xFFFFFFFFL
//ROM_SW_DATA_20
#define ROM_SW_DATA_20__ROM_SW_DATA__SHIFT                                                                    0x0
#define ROM_SW_DATA_20__ROM_SW_DATA_MASK                                                                      0xFFFFFFFFL
//ROM_SW_DATA_21
#define ROM_SW_DATA_21__ROM_SW_DATA__SHIFT                                                                    0x0
#define ROM_SW_DATA_21__ROM_SW_DATA_MASK                                                                      0xFFFFFFFFL
//ROM_SW_DATA_22
#define ROM_SW_DATA_22__ROM_SW_DATA__SHIFT                                                                    0x0
#define ROM_SW_DATA_22__ROM_SW_DATA_MASK                                                                      0xFFFFFFFFL
//ROM_SW_DATA_23
#define ROM_SW_DATA_23__ROM_SW_DATA__SHIFT                                                                    0x0
#define ROM_SW_DATA_23__ROM_SW_DATA_MASK                                                                      0xFFFFFFFFL
//ROM_SW_DATA_24
#define ROM_SW_DATA_24__ROM_SW_DATA__SHIFT                                                                    0x0
#define ROM_SW_DATA_24__ROM_SW_DATA_MASK                                                                      0xFFFFFFFFL
//ROM_SW_DATA_25
#define ROM_SW_DATA_25__ROM_SW_DATA__SHIFT                                                                    0x0
#define ROM_SW_DATA_25__ROM_SW_DATA_MASK                                                                      0xFFFFFFFFL
//ROM_SW_DATA_26
#define ROM_SW_DATA_26__ROM_SW_DATA__SHIFT                                                                    0x0
#define ROM_SW_DATA_26__ROM_SW_DATA_MASK                                                                      0xFFFFFFFFL
//ROM_SW_DATA_27
#define ROM_SW_DATA_27__ROM_SW_DATA__SHIFT                                                                    0x0
#define ROM_SW_DATA_27__ROM_SW_DATA_MASK                                                                      0xFFFFFFFFL
//ROM_SW_DATA_28
#define ROM_SW_DATA_28__ROM_SW_DATA__SHIFT                                                                    0x0
#define ROM_SW_DATA_28__ROM_SW_DATA_MASK                                                                      0xFFFFFFFFL
//ROM_SW_DATA_29
#define ROM_SW_DATA_29__ROM_SW_DATA__SHIFT                                                                    0x0
#define ROM_SW_DATA_29__ROM_SW_DATA_MASK                                                                      0xFFFFFFFFL
//ROM_SW_DATA_30
#define ROM_SW_DATA_30__ROM_SW_DATA__SHIFT                                                                    0x0
#define ROM_SW_DATA_30__ROM_SW_DATA_MASK                                                                      0xFFFFFFFFL
//ROM_SW_DATA_31
#define ROM_SW_DATA_31__ROM_SW_DATA__SHIFT                                                                    0x0
#define ROM_SW_DATA_31__ROM_SW_DATA_MASK                                                                      0xFFFFFFFFL
//ROM_SW_DATA_32
#define ROM_SW_DATA_32__ROM_SW_DATA__SHIFT                                                                    0x0
#define ROM_SW_DATA_32__ROM_SW_DATA_MASK                                                                      0xFFFFFFFFL
//ROM_SW_DATA_33
#define ROM_SW_DATA_33__ROM_SW_DATA__SHIFT                                                                    0x0
#define ROM_SW_DATA_33__ROM_SW_DATA_MASK                                                                      0xFFFFFFFFL
//ROM_SW_DATA_34
#define ROM_SW_DATA_34__ROM_SW_DATA__SHIFT                                                                    0x0
#define ROM_SW_DATA_34__ROM_SW_DATA_MASK                                                                      0xFFFFFFFFL
//ROM_SW_DATA_35
#define ROM_SW_DATA_35__ROM_SW_DATA__SHIFT                                                                    0x0
#define ROM_SW_DATA_35__ROM_SW_DATA_MASK                                                                      0xFFFFFFFFL
//ROM_SW_DATA_36
#define ROM_SW_DATA_36__ROM_SW_DATA__SHIFT                                                                    0x0
#define ROM_SW_DATA_36__ROM_SW_DATA_MASK                                                                      0xFFFFFFFFL
//ROM_SW_DATA_37
#define ROM_SW_DATA_37__ROM_SW_DATA__SHIFT                                                                    0x0
#define ROM_SW_DATA_37__ROM_SW_DATA_MASK                                                                      0xFFFFFFFFL
//ROM_SW_DATA_38
#define ROM_SW_DATA_38__ROM_SW_DATA__SHIFT                                                                    0x0
#define ROM_SW_DATA_38__ROM_SW_DATA_MASK                                                                      0xFFFFFFFFL
//ROM_SW_DATA_39
#define ROM_SW_DATA_39__ROM_SW_DATA__SHIFT                                                                    0x0
#define ROM_SW_DATA_39__ROM_SW_DATA_MASK                                                                      0xFFFFFFFFL
//ROM_SW_DATA_40
#define ROM_SW_DATA_40__ROM_SW_DATA__SHIFT                                                                    0x0
#define ROM_SW_DATA_40__ROM_SW_DATA_MASK                                                                      0xFFFFFFFFL
//ROM_SW_DATA_41
#define ROM_SW_DATA_41__ROM_SW_DATA__SHIFT                                                                    0x0
#define ROM_SW_DATA_41__ROM_SW_DATA_MASK                                                                      0xFFFFFFFFL
//ROM_SW_DATA_42
#define ROM_SW_DATA_42__ROM_SW_DATA__SHIFT                                                                    0x0
#define ROM_SW_DATA_42__ROM_SW_DATA_MASK                                                                      0xFFFFFFFFL
//ROM_SW_DATA_43
#define ROM_SW_DATA_43__ROM_SW_DATA__SHIFT                                                                    0x0
#define ROM_SW_DATA_43__ROM_SW_DATA_MASK                                                                      0xFFFFFFFFL
//ROM_SW_DATA_44
#define ROM_SW_DATA_44__ROM_SW_DATA__SHIFT                                                                    0x0
#define ROM_SW_DATA_44__ROM_SW_DATA_MASK                                                                      0xFFFFFFFFL
//ROM_SW_DATA_45
#define ROM_SW_DATA_45__ROM_SW_DATA__SHIFT                                                                    0x0
#define ROM_SW_DATA_45__ROM_SW_DATA_MASK                                                                      0xFFFFFFFFL
//ROM_SW_DATA_46
#define ROM_SW_DATA_46__ROM_SW_DATA__SHIFT                                                                    0x0
#define ROM_SW_DATA_46__ROM_SW_DATA_MASK                                                                      0xFFFFFFFFL
//ROM_SW_DATA_47
#define ROM_SW_DATA_47__ROM_SW_DATA__SHIFT                                                                    0x0
#define ROM_SW_DATA_47__ROM_SW_DATA_MASK                                                                      0xFFFFFFFFL
//ROM_SW_DATA_48
#define ROM_SW_DATA_48__ROM_SW_DATA__SHIFT                                                                    0x0
#define ROM_SW_DATA_48__ROM_SW_DATA_MASK                                                                      0xFFFFFFFFL
//ROM_SW_DATA_49
#define ROM_SW_DATA_49__ROM_SW_DATA__SHIFT                                                                    0x0
#define ROM_SW_DATA_49__ROM_SW_DATA_MASK                                                                      0xFFFFFFFFL
//ROM_SW_DATA_50
#define ROM_SW_DATA_50__ROM_SW_DATA__SHIFT                                                                    0x0
#define ROM_SW_DATA_50__ROM_SW_DATA_MASK                                                                      0xFFFFFFFFL
//ROM_SW_DATA_51
#define ROM_SW_DATA_51__ROM_SW_DATA__SHIFT                                                                    0x0
#define ROM_SW_DATA_51__ROM_SW_DATA_MASK                                                                      0xFFFFFFFFL
//ROM_SW_DATA_52
#define ROM_SW_DATA_52__ROM_SW_DATA__SHIFT                                                                    0x0
#define ROM_SW_DATA_52__ROM_SW_DATA_MASK                                                                      0xFFFFFFFFL
//ROM_SW_DATA_53
#define ROM_SW_DATA_53__ROM_SW_DATA__SHIFT                                                                    0x0
#define ROM_SW_DATA_53__ROM_SW_DATA_MASK                                                                      0xFFFFFFFFL
//ROM_SW_DATA_54
#define ROM_SW_DATA_54__ROM_SW_DATA__SHIFT                                                                    0x0
#define ROM_SW_DATA_54__ROM_SW_DATA_MASK                                                                      0xFFFFFFFFL
//ROM_SW_DATA_55
#define ROM_SW_DATA_55__ROM_SW_DATA__SHIFT                                                                    0x0
#define ROM_SW_DATA_55__ROM_SW_DATA_MASK                                                                      0xFFFFFFFFL
//ROM_SW_DATA_56
#define ROM_SW_DATA_56__ROM_SW_DATA__SHIFT                                                                    0x0
#define ROM_SW_DATA_56__ROM_SW_DATA_MASK                                                                      0xFFFFFFFFL
//ROM_SW_DATA_57
#define ROM_SW_DATA_57__ROM_SW_DATA__SHIFT                                                                    0x0
#define ROM_SW_DATA_57__ROM_SW_DATA_MASK                                                                      0xFFFFFFFFL
//ROM_SW_DATA_58
#define ROM_SW_DATA_58__ROM_SW_DATA__SHIFT                                                                    0x0
#define ROM_SW_DATA_58__ROM_SW_DATA_MASK                                                                      0xFFFFFFFFL
//ROM_SW_DATA_59
#define ROM_SW_DATA_59__ROM_SW_DATA__SHIFT                                                                    0x0
#define ROM_SW_DATA_59__ROM_SW_DATA_MASK                                                                      0xFFFFFFFFL
//ROM_SW_DATA_60
#define ROM_SW_DATA_60__ROM_SW_DATA__SHIFT                                                                    0x0
#define ROM_SW_DATA_60__ROM_SW_DATA_MASK                                                                      0xFFFFFFFFL
//ROM_SW_DATA_61
#define ROM_SW_DATA_61__ROM_SW_DATA__SHIFT                                                                    0x0
#define ROM_SW_DATA_61__ROM_SW_DATA_MASK                                                                      0xFFFFFFFFL
//ROM_SW_DATA_62
#define ROM_SW_DATA_62__ROM_SW_DATA__SHIFT                                                                    0x0
#define ROM_SW_DATA_62__ROM_SW_DATA_MASK                                                                      0xFFFFFFFFL
//ROM_SW_DATA_63
#define ROM_SW_DATA_63__ROM_SW_DATA__SHIFT                                                                    0x0
#define ROM_SW_DATA_63__ROM_SW_DATA_MASK                                                                      0xFFFFFFFFL
//ROM_SW_DATA_64
#define ROM_SW_DATA_64__ROM_SW_DATA__SHIFT                                                                    0x0
#define ROM_SW_DATA_64__ROM_SW_DATA_MASK                                                                      0xFFFFFFFFL
//SMU_GPIOPAD_SW_INT_STAT
#define SMU_GPIOPAD_SW_INT_STAT__SW_INT_STAT__SHIFT                                                           0x0
#define SMU_GPIOPAD_SW_INT_STAT__SW_INT_STAT_MASK                                                             0x00000001L
//SMU_GPIOPAD_MASK
#define SMU_GPIOPAD_MASK__GPIO_MASK__SHIFT                                                                    0x0
#define SMU_GPIOPAD_MASK__GPIO_MASK_MASK                                                                      0x7FFFFFFFL
//SMU_GPIOPAD_A
#define SMU_GPIOPAD_A__GPIO_A__SHIFT                                                                          0x0
#define SMU_GPIOPAD_A__GPIO_A_MASK                                                                            0x7FFFFFFFL
//SMU_GPIOPAD_TXIMPSEL
#define SMU_GPIOPAD_TXIMPSEL__GPIO_TXIMPSEL__SHIFT                                                            0x0
#define SMU_GPIOPAD_TXIMPSEL__GPIO_TXIMPSEL_MASK                                                              0x7FFFFFFFL
//SMU_GPIOPAD_EN
#define SMU_GPIOPAD_EN__GPIO_EN__SHIFT                                                                        0x0
#define SMU_GPIOPAD_EN__GPIO_EN_MASK                                                                          0x7FFFFFFFL
//SMU_GPIOPAD_Y
#define SMU_GPIOPAD_Y__GPIO_Y__SHIFT                                                                          0x0
#define SMU_GPIOPAD_Y__GPIO_Y_MASK                                                                            0x7FFFFFFFL
//SMU_GPIOPAD_RXEN
#define SMU_GPIOPAD_RXEN__GPIO_RXEN__SHIFT                                                                    0x0
#define SMU_GPIOPAD_RXEN__GPIO_RXEN_MASK                                                                      0x7FFFFFFFL
//SMU_GPIOPAD_RCVR_SEL0
#define SMU_GPIOPAD_RCVR_SEL0__GPIO_RCVR_SEL0__SHIFT                                                          0x0
#define SMU_GPIOPAD_RCVR_SEL0__GPIO_RCVR_SEL0_MASK                                                            0x7FFFFFFFL
//SMU_GPIOPAD_RCVR_SEL1
#define SMU_GPIOPAD_RCVR_SEL1__GPIO_RCVR_SEL1__SHIFT                                                          0x0
#define SMU_GPIOPAD_RCVR_SEL1__GPIO_RCVR_SEL1_MASK                                                            0x7FFFFFFFL
//SMU_GPIOPAD_PU_EN
#define SMU_GPIOPAD_PU_EN__GPIO_PU_EN__SHIFT                                                                  0x0
#define SMU_GPIOPAD_PU_EN__GPIO_PU_EN_MASK                                                                    0x7FFFFFFFL
//SMU_GPIOPAD_PD_EN
#define SMU_GPIOPAD_PD_EN__GPIO_PD_EN__SHIFT                                                                  0x0
#define SMU_GPIOPAD_PD_EN__GPIO_PD_EN_MASK                                                                    0x7FFFFFFFL
//SMU_GPIOPAD_PINSTRAPS
#define SMU_GPIOPAD_PINSTRAPS__GPIO_PINSTRAP_0__SHIFT                                                         0x0
#define SMU_GPIOPAD_PINSTRAPS__GPIO_PINSTRAP_1__SHIFT                                                         0x1
#define SMU_GPIOPAD_PINSTRAPS__GPIO_PINSTRAP_2__SHIFT                                                         0x2
#define SMU_GPIOPAD_PINSTRAPS__GPIO_PINSTRAP_3__SHIFT                                                         0x3
#define SMU_GPIOPAD_PINSTRAPS__GPIO_PINSTRAP_4__SHIFT                                                         0x4
#define SMU_GPIOPAD_PINSTRAPS__GPIO_PINSTRAP_5__SHIFT                                                         0x5
#define SMU_GPIOPAD_PINSTRAPS__GPIO_PINSTRAP_6__SHIFT                                                         0x6
#define SMU_GPIOPAD_PINSTRAPS__GPIO_PINSTRAP_7__SHIFT                                                         0x7
#define SMU_GPIOPAD_PINSTRAPS__GPIO_PINSTRAP_8__SHIFT                                                         0x8
#define SMU_GPIOPAD_PINSTRAPS__GPIO_PINSTRAP_9__SHIFT                                                         0x9
#define SMU_GPIOPAD_PINSTRAPS__GPIO_PINSTRAP_10__SHIFT                                                        0xa
#define SMU_GPIOPAD_PINSTRAPS__GPIO_PINSTRAP_11__SHIFT                                                        0xb
#define SMU_GPIOPAD_PINSTRAPS__GPIO_PINSTRAP_12__SHIFT                                                        0xc
#define SMU_GPIOPAD_PINSTRAPS__GPIO_PINSTRAP_13__SHIFT                                                        0xd
#define SMU_GPIOPAD_PINSTRAPS__GPIO_PINSTRAP_14__SHIFT                                                        0xe
#define SMU_GPIOPAD_PINSTRAPS__GPIO_PINSTRAP_15__SHIFT                                                        0xf
#define SMU_GPIOPAD_PINSTRAPS__GPIO_PINSTRAP_16__SHIFT                                                        0x10
#define SMU_GPIOPAD_PINSTRAPS__GPIO_PINSTRAP_17__SHIFT                                                        0x11
#define SMU_GPIOPAD_PINSTRAPS__GPIO_PINSTRAP_18__SHIFT                                                        0x12
#define SMU_GPIOPAD_PINSTRAPS__GPIO_PINSTRAP_19__SHIFT                                                        0x13
#define SMU_GPIOPAD_PINSTRAPS__GPIO_PINSTRAP_20__SHIFT                                                        0x14
#define SMU_GPIOPAD_PINSTRAPS__GPIO_PINSTRAP_21__SHIFT                                                        0x15
#define SMU_GPIOPAD_PINSTRAPS__GPIO_PINSTRAP_22__SHIFT                                                        0x16
#define SMU_GPIOPAD_PINSTRAPS__GPIO_PINSTRAP_23__SHIFT                                                        0x17
#define SMU_GPIOPAD_PINSTRAPS__GPIO_PINSTRAP_24__SHIFT                                                        0x18
#define SMU_GPIOPAD_PINSTRAPS__GPIO_PINSTRAP_25__SHIFT                                                        0x19
#define SMU_GPIOPAD_PINSTRAPS__GPIO_PINSTRAP_26__SHIFT                                                        0x1a
#define SMU_GPIOPAD_PINSTRAPS__GPIO_PINSTRAP_27__SHIFT                                                        0x1b
#define SMU_GPIOPAD_PINSTRAPS__GPIO_PINSTRAP_28__SHIFT                                                        0x1c
#define SMU_GPIOPAD_PINSTRAPS__GPIO_PINSTRAP_29__SHIFT                                                        0x1d
#define SMU_GPIOPAD_PINSTRAPS__GPIO_PINSTRAP_30__SHIFT                                                        0x1e
#define SMU_GPIOPAD_PINSTRAPS__GPIO_PINSTRAP_0_MASK                                                           0x00000001L
#define SMU_GPIOPAD_PINSTRAPS__GPIO_PINSTRAP_1_MASK                                                           0x00000002L
#define SMU_GPIOPAD_PINSTRAPS__GPIO_PINSTRAP_2_MASK                                                           0x00000004L
#define SMU_GPIOPAD_PINSTRAPS__GPIO_PINSTRAP_3_MASK                                                           0x00000008L
#define SMU_GPIOPAD_PINSTRAPS__GPIO_PINSTRAP_4_MASK                                                           0x00000010L
#define SMU_GPIOPAD_PINSTRAPS__GPIO_PINSTRAP_5_MASK                                                           0x00000020L
#define SMU_GPIOPAD_PINSTRAPS__GPIO_PINSTRAP_6_MASK                                                           0x00000040L
#define SMU_GPIOPAD_PINSTRAPS__GPIO_PINSTRAP_7_MASK                                                           0x00000080L
#define SMU_GPIOPAD_PINSTRAPS__GPIO_PINSTRAP_8_MASK                                                           0x00000100L
#define SMU_GPIOPAD_PINSTRAPS__GPIO_PINSTRAP_9_MASK                                                           0x00000200L
#define SMU_GPIOPAD_PINSTRAPS__GPIO_PINSTRAP_10_MASK                                                          0x00000400L
#define SMU_GPIOPAD_PINSTRAPS__GPIO_PINSTRAP_11_MASK                                                          0x00000800L
#define SMU_GPIOPAD_PINSTRAPS__GPIO_PINSTRAP_12_MASK                                                          0x00001000L
#define SMU_GPIOPAD_PINSTRAPS__GPIO_PINSTRAP_13_MASK                                                          0x00002000L
#define SMU_GPIOPAD_PINSTRAPS__GPIO_PINSTRAP_14_MASK                                                          0x00004000L
#define SMU_GPIOPAD_PINSTRAPS__GPIO_PINSTRAP_15_MASK                                                          0x00008000L
#define SMU_GPIOPAD_PINSTRAPS__GPIO_PINSTRAP_16_MASK                                                          0x00010000L
#define SMU_GPIOPAD_PINSTRAPS__GPIO_PINSTRAP_17_MASK                                                          0x00020000L
#define SMU_GPIOPAD_PINSTRAPS__GPIO_PINSTRAP_18_MASK                                                          0x00040000L
#define SMU_GPIOPAD_PINSTRAPS__GPIO_PINSTRAP_19_MASK                                                          0x00080000L
#define SMU_GPIOPAD_PINSTRAPS__GPIO_PINSTRAP_20_MASK                                                          0x00100000L
#define SMU_GPIOPAD_PINSTRAPS__GPIO_PINSTRAP_21_MASK                                                          0x00200000L
#define SMU_GPIOPAD_PINSTRAPS__GPIO_PINSTRAP_22_MASK                                                          0x00400000L
#define SMU_GPIOPAD_PINSTRAPS__GPIO_PINSTRAP_23_MASK                                                          0x00800000L
#define SMU_GPIOPAD_PINSTRAPS__GPIO_PINSTRAP_24_MASK                                                          0x01000000L
#define SMU_GPIOPAD_PINSTRAPS__GPIO_PINSTRAP_25_MASK                                                          0x02000000L
#define SMU_GPIOPAD_PINSTRAPS__GPIO_PINSTRAP_26_MASK                                                          0x04000000L
#define SMU_GPIOPAD_PINSTRAPS__GPIO_PINSTRAP_27_MASK                                                          0x08000000L
#define SMU_GPIOPAD_PINSTRAPS__GPIO_PINSTRAP_28_MASK                                                          0x10000000L
#define SMU_GPIOPAD_PINSTRAPS__GPIO_PINSTRAP_29_MASK                                                          0x20000000L
#define SMU_GPIOPAD_PINSTRAPS__GPIO_PINSTRAP_30_MASK                                                          0x40000000L
//DFT_PINSTRAPS
#define DFT_PINSTRAPS__DFT_PINSTRAPS__SHIFT                                                                   0x0
#define DFT_PINSTRAPS__DFT_PINSTRAPS_MASK                                                                     0x000000FFL
//SMU_GPIOPAD_INT_STAT_EN
#define SMU_GPIOPAD_INT_STAT_EN__GPIO_INT_STAT_EN__SHIFT                                                      0x0
#define SMU_GPIOPAD_INT_STAT_EN__SW_INITIATED_INT_STAT_EN__SHIFT                                              0x1f
#define SMU_GPIOPAD_INT_STAT_EN__GPIO_INT_STAT_EN_MASK                                                        0x1FFFFFFFL
#define SMU_GPIOPAD_INT_STAT_EN__SW_INITIATED_INT_STAT_EN_MASK                                                0x80000000L
//SMU_GPIOPAD_INT_STAT
#define SMU_GPIOPAD_INT_STAT__GPIO_INT_STAT__SHIFT                                                            0x0
#define SMU_GPIOPAD_INT_STAT__SW_INITIATED_INT_STAT__SHIFT                                                    0x1f
#define SMU_GPIOPAD_INT_STAT__GPIO_INT_STAT_MASK                                                              0x1FFFFFFFL
#define SMU_GPIOPAD_INT_STAT__SW_INITIATED_INT_STAT_MASK                                                      0x80000000L
//SMU_GPIOPAD_INT_STAT_AK
#define SMU_GPIOPAD_INT_STAT_AK__GPIO_INT_STAT_AK_0__SHIFT                                                    0x0
#define SMU_GPIOPAD_INT_STAT_AK__GPIO_INT_STAT_AK_1__SHIFT                                                    0x1
#define SMU_GPIOPAD_INT_STAT_AK__GPIO_INT_STAT_AK_2__SHIFT                                                    0x2
#define SMU_GPIOPAD_INT_STAT_AK__GPIO_INT_STAT_AK_3__SHIFT                                                    0x3
#define SMU_GPIOPAD_INT_STAT_AK__GPIO_INT_STAT_AK_4__SHIFT                                                    0x4
#define SMU_GPIOPAD_INT_STAT_AK__GPIO_INT_STAT_AK_5__SHIFT                                                    0x5
#define SMU_GPIOPAD_INT_STAT_AK__GPIO_INT_STAT_AK_6__SHIFT                                                    0x6
#define SMU_GPIOPAD_INT_STAT_AK__GPIO_INT_STAT_AK_7__SHIFT                                                    0x7
#define SMU_GPIOPAD_INT_STAT_AK__GPIO_INT_STAT_AK_8__SHIFT                                                    0x8
#define SMU_GPIOPAD_INT_STAT_AK__GPIO_INT_STAT_AK_9__SHIFT                                                    0x9
#define SMU_GPIOPAD_INT_STAT_AK__GPIO_INT_STAT_AK_10__SHIFT                                                   0xa
#define SMU_GPIOPAD_INT_STAT_AK__GPIO_INT_STAT_AK_11__SHIFT                                                   0xb
#define SMU_GPIOPAD_INT_STAT_AK__GPIO_INT_STAT_AK_12__SHIFT                                                   0xc
#define SMU_GPIOPAD_INT_STAT_AK__GPIO_INT_STAT_AK_13__SHIFT                                                   0xd
#define SMU_GPIOPAD_INT_STAT_AK__GPIO_INT_STAT_AK_14__SHIFT                                                   0xe
#define SMU_GPIOPAD_INT_STAT_AK__GPIO_INT_STAT_AK_15__SHIFT                                                   0xf
#define SMU_GPIOPAD_INT_STAT_AK__GPIO_INT_STAT_AK_16__SHIFT                                                   0x10
#define SMU_GPIOPAD_INT_STAT_AK__GPIO_INT_STAT_AK_17__SHIFT                                                   0x11
#define SMU_GPIOPAD_INT_STAT_AK__GPIO_INT_STAT_AK_18__SHIFT                                                   0x12
#define SMU_GPIOPAD_INT_STAT_AK__GPIO_INT_STAT_AK_19__SHIFT                                                   0x13
#define SMU_GPIOPAD_INT_STAT_AK__GPIO_INT_STAT_AK_20__SHIFT                                                   0x14
#define SMU_GPIOPAD_INT_STAT_AK__GPIO_INT_STAT_AK_21__SHIFT                                                   0x15
#define SMU_GPIOPAD_INT_STAT_AK__GPIO_INT_STAT_AK_22__SHIFT                                                   0x16
#define SMU_GPIOPAD_INT_STAT_AK__GPIO_INT_STAT_AK_23__SHIFT                                                   0x17
#define SMU_GPIOPAD_INT_STAT_AK__GPIO_INT_STAT_AK_24__SHIFT                                                   0x18
#define SMU_GPIOPAD_INT_STAT_AK__GPIO_INT_STAT_AK_25__SHIFT                                                   0x19
#define SMU_GPIOPAD_INT_STAT_AK__GPIO_INT_STAT_AK_26__SHIFT                                                   0x1a
#define SMU_GPIOPAD_INT_STAT_AK__GPIO_INT_STAT_AK_27__SHIFT                                                   0x1b
#define SMU_GPIOPAD_INT_STAT_AK__GPIO_INT_STAT_AK_28__SHIFT                                                   0x1c
#define SMU_GPIOPAD_INT_STAT_AK__SW_INITIATED_INT_STAT_AK__SHIFT                                              0x1f
#define SMU_GPIOPAD_INT_STAT_AK__GPIO_INT_STAT_AK_0_MASK                                                      0x00000001L
#define SMU_GPIOPAD_INT_STAT_AK__GPIO_INT_STAT_AK_1_MASK                                                      0x00000002L
#define SMU_GPIOPAD_INT_STAT_AK__GPIO_INT_STAT_AK_2_MASK                                                      0x00000004L
#define SMU_GPIOPAD_INT_STAT_AK__GPIO_INT_STAT_AK_3_MASK                                                      0x00000008L
#define SMU_GPIOPAD_INT_STAT_AK__GPIO_INT_STAT_AK_4_MASK                                                      0x00000010L
#define SMU_GPIOPAD_INT_STAT_AK__GPIO_INT_STAT_AK_5_MASK                                                      0x00000020L
#define SMU_GPIOPAD_INT_STAT_AK__GPIO_INT_STAT_AK_6_MASK                                                      0x00000040L
#define SMU_GPIOPAD_INT_STAT_AK__GPIO_INT_STAT_AK_7_MASK                                                      0x00000080L
#define SMU_GPIOPAD_INT_STAT_AK__GPIO_INT_STAT_AK_8_MASK                                                      0x00000100L
#define SMU_GPIOPAD_INT_STAT_AK__GPIO_INT_STAT_AK_9_MASK                                                      0x00000200L
#define SMU_GPIOPAD_INT_STAT_AK__GPIO_INT_STAT_AK_10_MASK                                                     0x00000400L
#define SMU_GPIOPAD_INT_STAT_AK__GPIO_INT_STAT_AK_11_MASK                                                     0x00000800L
#define SMU_GPIOPAD_INT_STAT_AK__GPIO_INT_STAT_AK_12_MASK                                                     0x00001000L
#define SMU_GPIOPAD_INT_STAT_AK__GPIO_INT_STAT_AK_13_MASK                                                     0x00002000L
#define SMU_GPIOPAD_INT_STAT_AK__GPIO_INT_STAT_AK_14_MASK                                                     0x00004000L
#define SMU_GPIOPAD_INT_STAT_AK__GPIO_INT_STAT_AK_15_MASK                                                     0x00008000L
#define SMU_GPIOPAD_INT_STAT_AK__GPIO_INT_STAT_AK_16_MASK                                                     0x00010000L
#define SMU_GPIOPAD_INT_STAT_AK__GPIO_INT_STAT_AK_17_MASK                                                     0x00020000L
#define SMU_GPIOPAD_INT_STAT_AK__GPIO_INT_STAT_AK_18_MASK                                                     0x00040000L
#define SMU_GPIOPAD_INT_STAT_AK__GPIO_INT_STAT_AK_19_MASK                                                     0x00080000L
#define SMU_GPIOPAD_INT_STAT_AK__GPIO_INT_STAT_AK_20_MASK                                                     0x00100000L
#define SMU_GPIOPAD_INT_STAT_AK__GPIO_INT_STAT_AK_21_MASK                                                     0x00200000L
#define SMU_GPIOPAD_INT_STAT_AK__GPIO_INT_STAT_AK_22_MASK                                                     0x00400000L
#define SMU_GPIOPAD_INT_STAT_AK__GPIO_INT_STAT_AK_23_MASK                                                     0x00800000L
#define SMU_GPIOPAD_INT_STAT_AK__GPIO_INT_STAT_AK_24_MASK                                                     0x01000000L
#define SMU_GPIOPAD_INT_STAT_AK__GPIO_INT_STAT_AK_25_MASK                                                     0x02000000L
#define SMU_GPIOPAD_INT_STAT_AK__GPIO_INT_STAT_AK_26_MASK                                                     0x04000000L
#define SMU_GPIOPAD_INT_STAT_AK__GPIO_INT_STAT_AK_27_MASK                                                     0x08000000L
#define SMU_GPIOPAD_INT_STAT_AK__GPIO_INT_STAT_AK_28_MASK                                                     0x10000000L
#define SMU_GPIOPAD_INT_STAT_AK__SW_INITIATED_INT_STAT_AK_MASK                                                0x80000000L
//SMU_GPIOPAD_INT_EN
#define SMU_GPIOPAD_INT_EN__GPIO_INT_EN__SHIFT                                                                0x0
#define SMU_GPIOPAD_INT_EN__SW_INITIATED_INT_EN__SHIFT                                                        0x1f
#define SMU_GPIOPAD_INT_EN__GPIO_INT_EN_MASK                                                                  0x1FFFFFFFL
#define SMU_GPIOPAD_INT_EN__SW_INITIATED_INT_EN_MASK                                                          0x80000000L
//SMU_GPIOPAD_INT_TYPE
#define SMU_GPIOPAD_INT_TYPE__GPIO_INT_TYPE__SHIFT                                                            0x0
#define SMU_GPIOPAD_INT_TYPE__SW_INITIATED_INT_TYPE__SHIFT                                                    0x1f
#define SMU_GPIOPAD_INT_TYPE__GPIO_INT_TYPE_MASK                                                              0x1FFFFFFFL
#define SMU_GPIOPAD_INT_TYPE__SW_INITIATED_INT_TYPE_MASK                                                      0x80000000L
//SMU_GPIOPAD_INT_POLARITY
#define SMU_GPIOPAD_INT_POLARITY__GPIO_INT_POLARITY__SHIFT                                                    0x0
#define SMU_GPIOPAD_INT_POLARITY__SW_INITIATED_INT_POLARITY__SHIFT                                            0x1f
#define SMU_GPIOPAD_INT_POLARITY__GPIO_INT_POLARITY_MASK                                                      0x1FFFFFFFL
#define SMU_GPIOPAD_INT_POLARITY__SW_INITIATED_INT_POLARITY_MASK                                              0x80000000L
//ROM_CC_BIF_PINSTRAP
#define ROM_CC_BIF_PINSTRAP__BIOS_ROM_EN__SHIFT                                                               0x0
#define ROM_CC_BIF_PINSTRAP__BIF_MEM_AP_SIZE__SHIFT                                                           0x1
#define ROM_CC_BIF_PINSTRAP__ROM_CONFIG__SHIFT                                                                0x4
#define ROM_CC_BIF_PINSTRAP__BIF_GEN3_DIS_A__SHIFT                                                            0x7
#define ROM_CC_BIF_PINSTRAP__BIF_CLK_PM_EN__SHIFT                                                             0x8
#define ROM_CC_BIF_PINSTRAP__BIF_VGA_DIS__SHIFT                                                               0x9
#define ROM_CC_BIF_PINSTRAP__BIF_LC_TX_SWING__SHIFT                                                           0xa
#define ROM_CC_BIF_PINSTRAP__BIOS_ROM_EN_MASK                                                                 0x00000001L
#define ROM_CC_BIF_PINSTRAP__BIF_MEM_AP_SIZE_MASK                                                             0x0000000EL
#define ROM_CC_BIF_PINSTRAP__ROM_CONFIG_MASK                                                                  0x00000070L
#define ROM_CC_BIF_PINSTRAP__BIF_GEN3_DIS_A_MASK                                                              0x00000080L
#define ROM_CC_BIF_PINSTRAP__BIF_CLK_PM_EN_MASK                                                               0x00000100L
#define ROM_CC_BIF_PINSTRAP__BIF_VGA_DIS_MASK                                                                 0x00000200L
#define ROM_CC_BIF_PINSTRAP__BIF_LC_TX_SWING_MASK                                                             0x00000400L
//IO_SMUIO_PINSTRAP
#define IO_SMUIO_PINSTRAP__AUD_PORT_CONN__SHIFT                                                               0x0
#define IO_SMUIO_PINSTRAP__AUD__SHIFT                                                                         0x3
#define IO_SMUIO_PINSTRAP__BOARD_CONFIG__SHIFT                                                                0x5
#define IO_SMUIO_PINSTRAP__SMBUS_ADDR__SHIFT                                                                  0x8
#define IO_SMUIO_PINSTRAP__AUD_PORT_CONN_MASK                                                                 0x00000007L
#define IO_SMUIO_PINSTRAP__AUD_MASK                                                                           0x00000018L
#define IO_SMUIO_PINSTRAP__BOARD_CONFIG_MASK                                                                  0x000000E0L
#define IO_SMUIO_PINSTRAP__SMBUS_ADDR_MASK                                                                    0x00000100L
//SMUIO_PCC_CONTROL
#define SMUIO_PCC_CONTROL__PCC_POLARITY__SHIFT                                                                0x0
#define SMUIO_PCC_CONTROL__PCC_POLARITY_MASK                                                                  0x00000001L
//SMUIO_PCC_GPIO_SELECT
#define SMUIO_PCC_GPIO_SELECT__GPIO__SHIFT                                                                    0x0
#define SMUIO_PCC_GPIO_SELECT__GPIO_MASK                                                                      0xFFFFFFFFL
//SMUIO_GPIO_INT0_SELECT
#define SMUIO_GPIO_INT0_SELECT__GPIO_INT0_SELECT__SHIFT                                                       0x0
#define SMUIO_GPIO_INT0_SELECT__GPIO_INT0_SELECT_MASK                                                         0xFFFFFFFFL
//SMUIO_GPIO_INT1_SELECT
#define SMUIO_GPIO_INT1_SELECT__GPIO_INT1_SELECT__SHIFT                                                       0x0
#define SMUIO_GPIO_INT1_SELECT__GPIO_INT1_SELECT_MASK                                                         0xFFFFFFFFL
//SMUIO_GPIO_INT2_SELECT
#define SMUIO_GPIO_INT2_SELECT__GPIO_INT2_SELECT__SHIFT                                                       0x0
#define SMUIO_GPIO_INT2_SELECT__GPIO_INT2_SELECT_MASK                                                         0xFFFFFFFFL
//SMUIO_GPIO_INT3_SELECT
#define SMUIO_GPIO_INT3_SELECT__GPIO_INT3_SELECT__SHIFT                                                       0x0
#define SMUIO_GPIO_INT3_SELECT__GPIO_INT3_SELECT_MASK                                                         0xFFFFFFFFL
//SMU_GPIOPAD_MP_INT0_STAT
#define SMU_GPIOPAD_MP_INT0_STAT__GPIO_MP_INT0_STAT__SHIFT                                                    0x0
#define SMU_GPIOPAD_MP_INT0_STAT__GPIO_MP_INT0_STAT_MASK                                                      0x1FFFFFFFL
//SMU_GPIOPAD_MP_INT1_STAT
#define SMU_GPIOPAD_MP_INT1_STAT__GPIO_MP_INT1_STAT__SHIFT                                                    0x0
#define SMU_GPIOPAD_MP_INT1_STAT__GPIO_MP_INT1_STAT_MASK                                                      0x1FFFFFFFL
//SMU_GPIOPAD_MP_INT2_STAT
#define SMU_GPIOPAD_MP_INT2_STAT__GPIO_MP_INT2_STAT__SHIFT                                                    0x0
#define SMU_GPIOPAD_MP_INT2_STAT__GPIO_MP_INT2_STAT_MASK                                                      0x1FFFFFFFL
//SMU_GPIOPAD_MP_INT3_STAT
#define SMU_GPIOPAD_MP_INT3_STAT__GPIO_MP_INT3_STAT__SHIFT                                                    0x0
#define SMU_GPIOPAD_MP_INT3_STAT__GPIO_MP_INT3_STAT_MASK                                                      0x1FFFFFFFL
//SMIO_INDEX
#define SMIO_INDEX__SW_SMIO_INDEX__SHIFT                                                                      0x0
#define SMIO_INDEX__SW_SMIO_INDEX_MASK                                                                        0x00000001L
//S0_VID_SMIO_CNTL
#define S0_VID_SMIO_CNTL__S0_SMIO_VALUES__SHIFT                                                               0x0
#define S0_VID_SMIO_CNTL__S0_SMIO_VALUES_MASK                                                                 0xFFFFFFFFL
//S1_VID_SMIO_CNTL
#define S1_VID_SMIO_CNTL__S1_SMIO_VALUES__SHIFT                                                               0x0
#define S1_VID_SMIO_CNTL__S1_SMIO_VALUES_MASK                                                                 0xFFFFFFFFL
//OPEN_DRAIN_SELECT
#define OPEN_DRAIN_SELECT__OPEN_DRAIN_SELECT__SHIFT                                                           0x0
#define OPEN_DRAIN_SELECT__RESERVED__SHIFT                                                                    0x1f
#define OPEN_DRAIN_SELECT__OPEN_DRAIN_SELECT_MASK                                                             0x7FFFFFFFL
#define OPEN_DRAIN_SELECT__RESERVED_MASK                                                                      0x80000000L
//SMIO_ENABLE
#define SMIO_ENABLE__SMIO_ENABLE__SHIFT                                                                       0x0
#define SMIO_ENABLE__SMIO_ENABLE_MASK                                                                         0xFFFFFFFFL
//SMU_GPIOPAD_S0
#define SMU_GPIOPAD_S0__GPIO_S0__SHIFT                                                                        0x0
#define SMU_GPIOPAD_S0__GPIO_S0_MASK                                                                          0x7FFFFFFFL
//SMU_GPIOPAD_S1
#define SMU_GPIOPAD_S1__GPIO_S1__SHIFT                                                                        0x0
#define SMU_GPIOPAD_S1__GPIO_S1_MASK                                                                          0x7FFFFFFFL
//SMU_GPIOPAD_SCL_EN
#define SMU_GPIOPAD_SCL_EN__GPIO_SCL_EN__SHIFT                                                                0x0
#define SMU_GPIOPAD_SCL_EN__GPIO_SCL_EN_MASK                                                                  0x7FFFFFFFL
//SMU_GPIOPAD_SDA_EN
#define SMU_GPIOPAD_SDA_EN__GPIO_SDA_EN__SHIFT                                                                0x0
#define SMU_GPIOPAD_SDA_EN__GPIO_SDA_EN_MASK                                                                  0x7FFFFFFFL
//SMU_GPIOPAD_SCHMEN
#define SMU_GPIOPAD_SCHMEN__GPIO_SCHMEN__SHIFT                                                                0x0
#define SMU_GPIOPAD_SCHMEN__GPIO_SCHMEN_MASK                                                                  0x7FFFFFFFL


// addressBlock: smuio_smuio_pwr_SmuSmuioDec
//IP_DISCOVERY_VERSION
#define IP_DISCOVERY_VERSION__IP_DISCOVERY_VERSION__SHIFT                                                     0x0
#define IP_DISCOVERY_VERSION__IP_DISCOVERY_VERSION_MASK                                                       0xFFFFFFFFL
//SOC_GAP_PWROK
#define SOC_GAP_PWROK__soc_gap_pwrok__SHIFT                                                                   0x0
#define SOC_GAP_PWROK__soc_gap_pwrok_MASK                                                                     0x00000001L
//GFX_GAP_PWROK
#define GFX_GAP_PWROK__gfx_gap_pwrok__SHIFT                                                                   0x0
#define GFX_GAP_PWROK__gfx_gap_pwrok_MASK                                                                     0x00000001L
//PWROK_REFCLK_GAP_CYCLES
#define PWROK_REFCLK_GAP_CYCLES__Pwrok_PreAssertion_clkgap_cycles__SHIFT                                      0x0
#define PWROK_REFCLK_GAP_CYCLES__Pwrok_PostAssertion_clkgap_cycles__SHIFT                                     0x8
#define PWROK_REFCLK_GAP_CYCLES__Pwrok_PreAssertion_clkgap_cycles_MASK                                        0x000000FFL
#define PWROK_REFCLK_GAP_CYCLES__Pwrok_PostAssertion_clkgap_cycles_MASK                                       0x0000FF00L
//GOLDEN_TSC_INCREMENT_UPPER
#define GOLDEN_TSC_INCREMENT_UPPER__GoldenTscIncrementUpper__SHIFT                                            0x0
#define GOLDEN_TSC_INCREMENT_UPPER__GoldenTscIncrementUpper_MASK                                              0x00FFFFFFL
//GOLDEN_TSC_INCREMENT_LOWER
#define GOLDEN_TSC_INCREMENT_LOWER__GoldenTscIncrementLower__SHIFT                                            0x0
#define GOLDEN_TSC_INCREMENT_LOWER__GoldenTscIncrementLower_MASK                                              0xFFFFFFFFL
//GOLDEN_TSC_COUNT_UPPER
#define GOLDEN_TSC_COUNT_UPPER__GoldenTscCountUpper__SHIFT                                                    0x0
#define GOLDEN_TSC_COUNT_UPPER__GoldenTscCountUpper_MASK                                                      0x00FFFFFFL
//GOLDEN_TSC_COUNT_LOWER
#define GOLDEN_TSC_COUNT_LOWER__GoldenTscCountLower__SHIFT                                                    0x0
#define GOLDEN_TSC_COUNT_LOWER__GoldenTscCountLower_MASK                                                      0xFFFFFFFFL
//SOC_GOLDEN_TSC_SHADOW_UPPER
#define SOC_GOLDEN_TSC_SHADOW_UPPER__SOCGoldenTscShadowUpper__SHIFT                                           0x0
#define SOC_GOLDEN_TSC_SHADOW_UPPER__SOCGoldenTscShadowUpper_MASK                                             0x00FFFFFFL
//SOC_GOLDEN_TSC_SHADOW_LOWER
#define SOC_GOLDEN_TSC_SHADOW_LOWER__SOCGoldenTscShadowLower__SHIFT                                           0x0
#define SOC_GOLDEN_TSC_SHADOW_LOWER__SOCGoldenTscShadowLower_MASK                                             0xFFFFFFFFL
//GFX_GOLDEN_TSC_SHADOW_UPPER
#define GFX_GOLDEN_TSC_SHADOW_UPPER__GFXGoldenTscShadowUpper__SHIFT                                           0x0
#define GFX_GOLDEN_TSC_SHADOW_UPPER__GFXGoldenTscShadowUpper_MASK                                             0x00FFFFFFL
//GFX_GOLDEN_TSC_SHADOW_LOWER
#define GFX_GOLDEN_TSC_SHADOW_LOWER__GFXGoldenTscShadowLower__SHIFT                                           0x0
#define GFX_GOLDEN_TSC_SHADOW_LOWER__GFXGoldenTscShadowLower_MASK                                             0xFFFFFFFFL
//PWR_VIRT_RESET_REQ
#define PWR_VIRT_RESET_REQ__VF_FLR__SHIFT                                                                     0x0
#define PWR_VIRT_RESET_REQ__PF_FLR__SHIFT                                                                     0x1f
#define PWR_VIRT_RESET_REQ__VF_FLR_MASK                                                                       0x7FFFFFFFL
#define PWR_VIRT_RESET_REQ__PF_FLR_MASK                                                                       0x80000000L
//SCRATCH_REGISTER0
#define SCRATCH_REGISTER0__ScratchPad0__SHIFT                                                                 0x0
#define SCRATCH_REGISTER0__ScratchPad0_MASK                                                                   0xFFFFFFFFL
//SCRATCH_REGISTER1
#define SCRATCH_REGISTER1__ScratchPad1__SHIFT                                                                 0x0
#define SCRATCH_REGISTER1__ScratchPad1_MASK                                                                   0xFFFFFFFFL
//SCRATCH_REGISTER2
#define SCRATCH_REGISTER2__ScratchPad2__SHIFT                                                                 0x0
#define SCRATCH_REGISTER2__ScratchPad2_MASK                                                                   0xFFFFFFFFL
//SCRATCH_REGISTER3
#define SCRATCH_REGISTER3__ScratchPad3__SHIFT                                                                 0x0
#define SCRATCH_REGISTER3__ScratchPad3_MASK                                                                   0xFFFFFFFFL
//SCRATCH_REGISTER4
#define SCRATCH_REGISTER4__ScratchPad4__SHIFT                                                                 0x0
#define SCRATCH_REGISTER4__ScratchPad4_MASK                                                                   0xFFFFFFFFL
//SCRATCH_REGISTER5
#define SCRATCH_REGISTER5__ScratchPad5__SHIFT                                                                 0x0
#define SCRATCH_REGISTER5__ScratchPad5_MASK                                                                   0xFFFFFFFFL
//SCRATCH_REGISTER6
#define SCRATCH_REGISTER6__ScratchPad6__SHIFT                                                                 0x0
#define SCRATCH_REGISTER6__ScratchPad6_MASK                                                                   0xFFFFFFFFL
//SCRATCH_REGISTER7
#define SCRATCH_REGISTER7__ScratchPad7__SHIFT                                                                 0x0
#define SCRATCH_REGISTER7__ScratchPad7_MASK                                                                   0xFFFFFFFFL
//PWR_DISP_TIMER_CONTROL
#define PWR_DISP_TIMER_CONTROL__DISP_TIMER_INT_COUNT__SHIFT                                                   0x0
#define PWR_DISP_TIMER_CONTROL__DISP_TIMER_INT_ENABLE__SHIFT                                                  0x19
#define PWR_DISP_TIMER_CONTROL__DISP_TIMER_INT_DISABLE__SHIFT                                                 0x1a
#define PWR_DISP_TIMER_CONTROL__DISP_TIMER_INT_MASK__SHIFT                                                    0x1b
#define PWR_DISP_TIMER_CONTROL__DISP_TIMER_INT_STAT_AK__SHIFT                                                 0x1c
#define PWR_DISP_TIMER_CONTROL__DISP_TIMER_INT_TYPE__SHIFT                                                    0x1d
#define PWR_DISP_TIMER_CONTROL__DISP_TIMER_INT_MODE__SHIFT                                                    0x1e
#define PWR_DISP_TIMER_CONTROL__DISP_TIMER_INT_COUNT_MASK                                                     0x01FFFFFFL
#define PWR_DISP_TIMER_CONTROL__DISP_TIMER_INT_ENABLE_MASK                                                    0x02000000L
#define PWR_DISP_TIMER_CONTROL__DISP_TIMER_INT_DISABLE_MASK                                                   0x04000000L
#define PWR_DISP_TIMER_CONTROL__DISP_TIMER_INT_MASK_MASK                                                      0x08000000L
#define PWR_DISP_TIMER_CONTROL__DISP_TIMER_INT_STAT_AK_MASK                                                   0x10000000L
#define PWR_DISP_TIMER_CONTROL__DISP_TIMER_INT_TYPE_MASK                                                      0x20000000L
#define PWR_DISP_TIMER_CONTROL__DISP_TIMER_INT_MODE_MASK                                                      0x40000000L
//PWR_DISP_TIMER2_CONTROL
#define PWR_DISP_TIMER2_CONTROL__DISP_TIMER_INT_COUNT__SHIFT                                                  0x0
#define PWR_DISP_TIMER2_CONTROL__DISP_TIMER_INT_ENABLE__SHIFT                                                 0x19
#define PWR_DISP_TIMER2_CONTROL__DISP_TIMER_INT_DISABLE__SHIFT                                                0x1a
#define PWR_DISP_TIMER2_CONTROL__DISP_TIMER_INT_MASK__SHIFT                                                   0x1b
#define PWR_DISP_TIMER2_CONTROL__DISP_TIMER_INT_STAT_AK__SHIFT                                                0x1c
#define PWR_DISP_TIMER2_CONTROL__DISP_TIMER_INT_TYPE__SHIFT                                                   0x1d
#define PWR_DISP_TIMER2_CONTROL__DISP_TIMER_INT_MODE__SHIFT                                                   0x1e
#define PWR_DISP_TIMER2_CONTROL__DISP_TIMER_INT_COUNT_MASK                                                    0x01FFFFFFL
#define PWR_DISP_TIMER2_CONTROL__DISP_TIMER_INT_ENABLE_MASK                                                   0x02000000L
#define PWR_DISP_TIMER2_CONTROL__DISP_TIMER_INT_DISABLE_MASK                                                  0x04000000L
#define PWR_DISP_TIMER2_CONTROL__DISP_TIMER_INT_MASK_MASK                                                     0x08000000L
#define PWR_DISP_TIMER2_CONTROL__DISP_TIMER_INT_STAT_AK_MASK                                                  0x10000000L
#define PWR_DISP_TIMER2_CONTROL__DISP_TIMER_INT_TYPE_MASK                                                     0x20000000L
#define PWR_DISP_TIMER2_CONTROL__DISP_TIMER_INT_MODE_MASK                                                     0x40000000L
//PWR_DISP_TIMER_GLOBAL_CONTROL
#define PWR_DISP_TIMER_GLOBAL_CONTROL__DISP_TIMER_PULSE_WIDTH__SHIFT                                          0x0
#define PWR_DISP_TIMER_GLOBAL_CONTROL__DISP_TIMER_PULSE_EN__SHIFT                                             0xa
#define PWR_DISP_TIMER_GLOBAL_CONTROL__DISP_TIMER_PULSE_WIDTH_MASK                                            0x000003FFL
#define PWR_DISP_TIMER_GLOBAL_CONTROL__DISP_TIMER_PULSE_EN_MASK                                               0x00000400L
//PWR_IH_CONTROL
#define PWR_IH_CONTROL__MAX_CREDIT__SHIFT                                                                     0x0
#define PWR_IH_CONTROL__DISP_TIMER_TRIGGER_MASK__SHIFT                                                        0x5
#define PWR_IH_CONTROL__DISP_TIMER2_TRIGGER_MASK__SHIFT                                                       0x6
#define PWR_IH_CONTROL__MAX_CREDIT_MASK                                                                       0x0000001FL
#define PWR_IH_CONTROL__DISP_TIMER_TRIGGER_MASK_MASK                                                          0x00000020L
#define PWR_IH_CONTROL__DISP_TIMER2_TRIGGER_MASK_MASK                                                         0x00000040L

#endif
