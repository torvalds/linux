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
#ifndef _athub_1_0_DEFAULT_HEADER
#define _athub_1_0_DEFAULT_HEADER


// addressBlock: athub_atsdec
#define mmATC_ATS_CNTL_DEFAULT                                                   0x009a0800
#define mmATC_ATS_STATUS_DEFAULT                                                 0x00000000
#define mmATC_ATS_FAULT_CNTL_DEFAULT                                             0x000001ff
#define mmATC_ATS_FAULT_STATUS_INFO_DEFAULT                                      0x00000000
#define mmATC_ATS_FAULT_STATUS_ADDR_DEFAULT                                      0x00000000
#define mmATC_ATS_DEFAULT_PAGE_LOW_DEFAULT                                       0x00000000
#define mmATC_TRANS_FAULT_RSPCNTRL_DEFAULT                                       0xffffffff
#define mmATC_ATS_FAULT_STATUS_INFO2_DEFAULT                                     0x00000000
#define mmATHUB_MISC_CNTL_DEFAULT                                                0x00040200
#define mmATC_VMID_PASID_MAPPING_UPDATE_STATUS_DEFAULT                           0x00000000
#define mmATC_VMID0_PASID_MAPPING_DEFAULT                                        0x00000000
#define mmATC_VMID1_PASID_MAPPING_DEFAULT                                        0x00000000
#define mmATC_VMID2_PASID_MAPPING_DEFAULT                                        0x00000000
#define mmATC_VMID3_PASID_MAPPING_DEFAULT                                        0x00000000
#define mmATC_VMID4_PASID_MAPPING_DEFAULT                                        0x00000000
#define mmATC_VMID5_PASID_MAPPING_DEFAULT                                        0x00000000
#define mmATC_VMID6_PASID_MAPPING_DEFAULT                                        0x00000000
#define mmATC_VMID7_PASID_MAPPING_DEFAULT                                        0x00000000
#define mmATC_VMID8_PASID_MAPPING_DEFAULT                                        0x00000000
#define mmATC_VMID9_PASID_MAPPING_DEFAULT                                        0x00000000
#define mmATC_VMID10_PASID_MAPPING_DEFAULT                                       0x00000000
#define mmATC_VMID11_PASID_MAPPING_DEFAULT                                       0x00000000
#define mmATC_VMID12_PASID_MAPPING_DEFAULT                                       0x00000000
#define mmATC_VMID13_PASID_MAPPING_DEFAULT                                       0x00000000
#define mmATC_VMID14_PASID_MAPPING_DEFAULT                                       0x00000000
#define mmATC_VMID15_PASID_MAPPING_DEFAULT                                       0x00000000
#define mmATC_ATS_VMID_STATUS_DEFAULT                                            0x00000000
#define mmATC_ATS_GFX_ATCL2_STATUS_DEFAULT                                       0x00000000
#define mmATC_PERFCOUNTER0_CFG_DEFAULT                                           0x00000000
#define mmATC_PERFCOUNTER1_CFG_DEFAULT                                           0x00000000
#define mmATC_PERFCOUNTER2_CFG_DEFAULT                                           0x00000000
#define mmATC_PERFCOUNTER3_CFG_DEFAULT                                           0x00000000
#define mmATC_PERFCOUNTER_RSLT_CNTL_DEFAULT                                      0x04000000
#define mmATC_PERFCOUNTER_LO_DEFAULT                                             0x00000000
#define mmATC_PERFCOUNTER_HI_DEFAULT                                             0x00000000
#define mmATHUB_PCIE_ATS_CNTL_DEFAULT                                            0x00000000
#define mmATHUB_PCIE_PASID_CNTL_DEFAULT                                          0x00000000
#define mmATHUB_PCIE_PAGE_REQ_CNTL_DEFAULT                                       0x00000000
#define mmATHUB_PCIE_OUTSTAND_PAGE_REQ_ALLOC_DEFAULT                             0x00000000
#define mmATHUB_COMMAND_DEFAULT                                                  0x00000000
#define mmATHUB_PCIE_ATS_CNTL_VF_0_DEFAULT                                       0x00000000
#define mmATHUB_PCIE_ATS_CNTL_VF_1_DEFAULT                                       0x00000000
#define mmATHUB_PCIE_ATS_CNTL_VF_2_DEFAULT                                       0x00000000
#define mmATHUB_PCIE_ATS_CNTL_VF_3_DEFAULT                                       0x00000000
#define mmATHUB_PCIE_ATS_CNTL_VF_4_DEFAULT                                       0x00000000
#define mmATHUB_PCIE_ATS_CNTL_VF_5_DEFAULT                                       0x00000000
#define mmATHUB_PCIE_ATS_CNTL_VF_6_DEFAULT                                       0x00000000
#define mmATHUB_PCIE_ATS_CNTL_VF_7_DEFAULT                                       0x00000000
#define mmATHUB_PCIE_ATS_CNTL_VF_8_DEFAULT                                       0x00000000
#define mmATHUB_PCIE_ATS_CNTL_VF_9_DEFAULT                                       0x00000000
#define mmATHUB_PCIE_ATS_CNTL_VF_10_DEFAULT                                      0x00000000
#define mmATHUB_PCIE_ATS_CNTL_VF_11_DEFAULT                                      0x00000000
#define mmATHUB_PCIE_ATS_CNTL_VF_12_DEFAULT                                      0x00000000
#define mmATHUB_PCIE_ATS_CNTL_VF_13_DEFAULT                                      0x00000000
#define mmATHUB_PCIE_ATS_CNTL_VF_14_DEFAULT                                      0x00000000
#define mmATHUB_PCIE_ATS_CNTL_VF_15_DEFAULT                                      0x00000000
#define mmATHUB_MEM_POWER_LS_DEFAULT                                             0x00000208
#define mmATS_IH_CREDIT_DEFAULT                                                  0x00150002
#define mmATHUB_IH_CREDIT_DEFAULT                                                0x00020002
#define mmATC_VMID16_PASID_MAPPING_DEFAULT                                       0x00000000
#define mmATC_VMID17_PASID_MAPPING_DEFAULT                                       0x00000000
#define mmATC_VMID18_PASID_MAPPING_DEFAULT                                       0x00000000
#define mmATC_VMID19_PASID_MAPPING_DEFAULT                                       0x00000000
#define mmATC_VMID20_PASID_MAPPING_DEFAULT                                       0x00000000
#define mmATC_VMID21_PASID_MAPPING_DEFAULT                                       0x00000000
#define mmATC_VMID22_PASID_MAPPING_DEFAULT                                       0x00000000
#define mmATC_VMID23_PASID_MAPPING_DEFAULT                                       0x00000000
#define mmATC_VMID24_PASID_MAPPING_DEFAULT                                       0x00000000
#define mmATC_VMID25_PASID_MAPPING_DEFAULT                                       0x00000000
#define mmATC_VMID26_PASID_MAPPING_DEFAULT                                       0x00000000
#define mmATC_VMID27_PASID_MAPPING_DEFAULT                                       0x00000000
#define mmATC_VMID28_PASID_MAPPING_DEFAULT                                       0x00000000
#define mmATC_VMID29_PASID_MAPPING_DEFAULT                                       0x00000000
#define mmATC_VMID30_PASID_MAPPING_DEFAULT                                       0x00000000
#define mmATC_VMID31_PASID_MAPPING_DEFAULT                                       0x00000000
#define mmATC_ATS_MMHUB_ATCL2_STATUS_DEFAULT                                     0x00000000
#define mmATHUB_SHARED_VIRT_RESET_REQ_DEFAULT                                    0x00000000
#define mmATHUB_SHARED_ACTIVE_FCN_ID_DEFAULT                                     0x00000000
#define mmATC_ATS_SDPPORT_CNTL_DEFAULT                                           0x03ffa210
#define mmATC_ATS_VMID_SNAPSHOT_GFX_STAT_DEFAULT                                 0x00000000
#define mmATC_ATS_VMID_SNAPSHOT_MMHUB_STAT_DEFAULT                               0x00000000


// addressBlock: athub_xpbdec
#define mmXPB_RTR_SRC_APRTR0_DEFAULT                                             0x00000000
#define mmXPB_RTR_SRC_APRTR1_DEFAULT                                             0x00000000
#define mmXPB_RTR_SRC_APRTR2_DEFAULT                                             0x00000000
#define mmXPB_RTR_SRC_APRTR3_DEFAULT                                             0x00000000
#define mmXPB_RTR_SRC_APRTR4_DEFAULT                                             0x00000000
#define mmXPB_RTR_SRC_APRTR5_DEFAULT                                             0x00000000
#define mmXPB_RTR_SRC_APRTR6_DEFAULT                                             0x00000000
#define mmXPB_RTR_SRC_APRTR7_DEFAULT                                             0x00000000
#define mmXPB_RTR_SRC_APRTR8_DEFAULT                                             0x00000000
#define mmXPB_RTR_SRC_APRTR9_DEFAULT                                             0x00000000
#define mmXPB_XDMA_RTR_SRC_APRTR0_DEFAULT                                        0x00000000
#define mmXPB_XDMA_RTR_SRC_APRTR1_DEFAULT                                        0x00000000
#define mmXPB_XDMA_RTR_SRC_APRTR2_DEFAULT                                        0x00000000
#define mmXPB_XDMA_RTR_SRC_APRTR3_DEFAULT                                        0x00000000
#define mmXPB_RTR_DEST_MAP0_DEFAULT                                              0x00000000
#define mmXPB_RTR_DEST_MAP1_DEFAULT                                              0x00000000
#define mmXPB_RTR_DEST_MAP2_DEFAULT                                              0x00000000
#define mmXPB_RTR_DEST_MAP3_DEFAULT                                              0x00000000
#define mmXPB_RTR_DEST_MAP4_DEFAULT                                              0x00000000
#define mmXPB_RTR_DEST_MAP5_DEFAULT                                              0x00000000
#define mmXPB_RTR_DEST_MAP6_DEFAULT                                              0x00000000
#define mmXPB_RTR_DEST_MAP7_DEFAULT                                              0x00000000
#define mmXPB_RTR_DEST_MAP8_DEFAULT                                              0x00000000
#define mmXPB_RTR_DEST_MAP9_DEFAULT                                              0x00000000
#define mmXPB_XDMA_RTR_DEST_MAP0_DEFAULT                                         0x00000000
#define mmXPB_XDMA_RTR_DEST_MAP1_DEFAULT                                         0x00000000
#define mmXPB_XDMA_RTR_DEST_MAP2_DEFAULT                                         0x00000000
#define mmXPB_XDMA_RTR_DEST_MAP3_DEFAULT                                         0x00000000
#define mmXPB_CLG_CFG0_DEFAULT                                                   0x00000000
#define mmXPB_CLG_CFG1_DEFAULT                                                   0x00000000
#define mmXPB_CLG_CFG2_DEFAULT                                                   0x00000000
#define mmXPB_CLG_CFG3_DEFAULT                                                   0x00000000
#define mmXPB_CLG_CFG4_DEFAULT                                                   0x00000000
#define mmXPB_CLG_CFG5_DEFAULT                                                   0x00000000
#define mmXPB_CLG_CFG6_DEFAULT                                                   0x00000000
#define mmXPB_CLG_CFG7_DEFAULT                                                   0x00000000
#define mmXPB_CLG_EXTRA_DEFAULT                                                  0x00000000
#define mmXPB_CLG_EXTRA_MSK_DEFAULT                                              0x00000000
#define mmXPB_LB_ADDR_DEFAULT                                                    0x00000000
#define mmXPB_WCB_STS_DEFAULT                                                    0x00000000
#define mmXPB_HST_CFG_DEFAULT                                                    0x00000000
#define mmXPB_P2P_BAR_CFG_DEFAULT                                                0x0000000f
#define mmXPB_P2P_BAR0_DEFAULT                                                   0x00000000
#define mmXPB_P2P_BAR1_DEFAULT                                                   0x00000000
#define mmXPB_P2P_BAR2_DEFAULT                                                   0x00000000
#define mmXPB_P2P_BAR3_DEFAULT                                                   0x00000000
#define mmXPB_P2P_BAR4_DEFAULT                                                   0x00000000
#define mmXPB_P2P_BAR5_DEFAULT                                                   0x00000000
#define mmXPB_P2P_BAR6_DEFAULT                                                   0x00000000
#define mmXPB_P2P_BAR7_DEFAULT                                                   0x00000000
#define mmXPB_P2P_BAR_SETUP_DEFAULT                                              0x00000000
#define mmXPB_P2P_BAR_DELTA_ABOVE_DEFAULT                                        0x00000000
#define mmXPB_P2P_BAR_DELTA_BELOW_DEFAULT                                        0x00000000
#define mmXPB_PEER_SYS_BAR0_DEFAULT                                              0x00000000
#define mmXPB_PEER_SYS_BAR1_DEFAULT                                              0x00000000
#define mmXPB_PEER_SYS_BAR2_DEFAULT                                              0x00000000
#define mmXPB_PEER_SYS_BAR3_DEFAULT                                              0x00000000
#define mmXPB_PEER_SYS_BAR4_DEFAULT                                              0x00000000
#define mmXPB_PEER_SYS_BAR5_DEFAULT                                              0x00000000
#define mmXPB_PEER_SYS_BAR6_DEFAULT                                              0x00000000
#define mmXPB_PEER_SYS_BAR7_DEFAULT                                              0x00000000
#define mmXPB_PEER_SYS_BAR8_DEFAULT                                              0x00000000
#define mmXPB_PEER_SYS_BAR9_DEFAULT                                              0x00000000
#define mmXPB_XDMA_PEER_SYS_BAR0_DEFAULT                                         0x00000000
#define mmXPB_XDMA_PEER_SYS_BAR1_DEFAULT                                         0x00000000
#define mmXPB_XDMA_PEER_SYS_BAR2_DEFAULT                                         0x00000000
#define mmXPB_XDMA_PEER_SYS_BAR3_DEFAULT                                         0x00000000
#define mmXPB_CLK_GAT_DEFAULT                                                    0x00040400
#define mmXPB_INTF_CFG_DEFAULT                                                   0x000f1040
#define mmXPB_INTF_STS_DEFAULT                                                   0x00000000
#define mmXPB_PIPE_STS_DEFAULT                                                   0x00000000
#define mmXPB_SUB_CTRL_DEFAULT                                                   0x00000000
#define mmXPB_MAP_INVERT_FLUSH_NUM_LSB_DEFAULT                                   0x00000000
#define mmXPB_PERF_KNOBS_DEFAULT                                                 0x00000000
#define mmXPB_STICKY_DEFAULT                                                     0x00000000
#define mmXPB_STICKY_W1C_DEFAULT                                                 0x00000000
#define mmXPB_MISC_CFG_DEFAULT                                                   0x4d585042
#define mmXPB_INTF_CFG2_DEFAULT                                                  0x00000040
#define mmXPB_CLG_EXTRA_RD_DEFAULT                                               0x00000000
#define mmXPB_CLG_EXTRA_MSK_RD_DEFAULT                                           0x00000000
#define mmXPB_CLG_GFX_MATCH_DEFAULT                                              0x03000000
#define mmXPB_CLG_GFX_MATCH_MSK_DEFAULT                                          0x00000000
#define mmXPB_CLG_MM_MATCH_DEFAULT                                               0x03000000
#define mmXPB_CLG_MM_MATCH_MSK_DEFAULT                                           0x00000000
#define mmXPB_CLG_GFX_UNITID_MAPPING0_DEFAULT                                    0x00000000
#define mmXPB_CLG_GFX_UNITID_MAPPING1_DEFAULT                                    0x00000040
#define mmXPB_CLG_GFX_UNITID_MAPPING2_DEFAULT                                    0x00000080
#define mmXPB_CLG_GFX_UNITID_MAPPING3_DEFAULT                                    0x000000c0
#define mmXPB_CLG_GFX_UNITID_MAPPING4_DEFAULT                                    0x00000100
#define mmXPB_CLG_GFX_UNITID_MAPPING5_DEFAULT                                    0x00000140
#define mmXPB_CLG_GFX_UNITID_MAPPING6_DEFAULT                                    0x00000000
#define mmXPB_CLG_GFX_UNITID_MAPPING7_DEFAULT                                    0x000001c0
#define mmXPB_CLG_MM_UNITID_MAPPING0_DEFAULT                                     0x00000000
#define mmXPB_CLG_MM_UNITID_MAPPING1_DEFAULT                                     0x00000040
#define mmXPB_CLG_MM_UNITID_MAPPING2_DEFAULT                                     0x00000080
#define mmXPB_CLG_MM_UNITID_MAPPING3_DEFAULT                                     0x000000c0


// addressBlock: athub_rpbdec
#define mmRPB_PASSPW_CONF_DEFAULT                                                0x00000230
#define mmRPB_BLOCKLEVEL_CONF_DEFAULT                                            0x000000f0
#define mmRPB_TAG_CONF_DEFAULT                                                   0x00204020
#define mmRPB_EFF_CNTL_DEFAULT                                                   0x00001010
#define mmRPB_ARB_CNTL_DEFAULT                                                   0x00040404
#define mmRPB_ARB_CNTL2_DEFAULT                                                  0x00040104
#define mmRPB_BIF_CNTL_DEFAULT                                                   0x01000404
#define mmRPB_WR_SWITCH_CNTL_DEFAULT                                             0x02040810
#define mmRPB_RD_SWITCH_CNTL_DEFAULT                                             0x02040810
#define mmRPB_CID_QUEUE_WR_DEFAULT                                               0x00000000
#define mmRPB_CID_QUEUE_RD_DEFAULT                                               0x00000000
#define mmRPB_CID_QUEUE_EX_DEFAULT                                               0x00000000
#define mmRPB_CID_QUEUE_EX_DATA_DEFAULT                                          0x00000000
#define mmRPB_SWITCH_CNTL2_DEFAULT                                               0x02040810
#define mmRPB_DEINTRLV_COMBINE_CNTL_DEFAULT                                      0x00000004
#define mmRPB_VC_SWITCH_RDWR_DEFAULT                                             0x00004040
#define mmRPB_PERFCOUNTER_LO_DEFAULT                                             0x00000000
#define mmRPB_PERFCOUNTER_HI_DEFAULT                                             0x00000000
#define mmRPB_PERFCOUNTER0_CFG_DEFAULT                                           0x00000000
#define mmRPB_PERFCOUNTER1_CFG_DEFAULT                                           0x00000000
#define mmRPB_PERFCOUNTER2_CFG_DEFAULT                                           0x00000000
#define mmRPB_PERFCOUNTER3_CFG_DEFAULT                                           0x00000000
#define mmRPB_PERFCOUNTER_RSLT_CNTL_DEFAULT                                      0x04000000
#define mmRPB_RD_QUEUE_CNTL_DEFAULT                                              0x00000000
#define mmRPB_RD_QUEUE_CNTL2_DEFAULT                                             0x00000000
#define mmRPB_WR_QUEUE_CNTL_DEFAULT                                              0x00000000
#define mmRPB_WR_QUEUE_CNTL2_DEFAULT                                             0x00000000
#define mmRPB_EA_QUEUE_WR_DEFAULT                                                0x00000000
#define mmRPB_ATS_CNTL_DEFAULT                                                   0x58088422
#define mmRPB_ATS_CNTL2_DEFAULT                                                  0x00050b13
#define mmRPB_SDPPORT_CNTL_DEFAULT                                               0x0fd14814

#endif
