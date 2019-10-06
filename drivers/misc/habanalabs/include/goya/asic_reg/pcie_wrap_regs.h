/* SPDX-License-Identifier: GPL-2.0
 *
 * Copyright 2016-2018 HabanaLabs, Ltd.
 * All Rights Reserved.
 *
 */

/************************************
 ** This is an auto-generated file **
 **       DO NOT EDIT BELOW        **
 ************************************/

#ifndef ASIC_REG_PCIE_WRAP_REGS_H_
#define ASIC_REG_PCIE_WRAP_REGS_H_

/*
 *****************************************
 *   PCIE_WRAP (Prototype: PCIE_WRAP)
 *****************************************
 */

#define mmPCIE_WRAP_PHY_RST_N                                        0xC01300

#define mmPCIE_WRAP_OUTSTAND_TRANS                                   0xC01400

#define mmPCIE_WRAP_MASK_REQ                                         0xC01404

#define mmPCIE_WRAP_IND_AWADDR_L                                     0xC01500

#define mmPCIE_WRAP_IND_AWADDR_H                                     0xC01504

#define mmPCIE_WRAP_IND_AWLEN                                        0xC01508

#define mmPCIE_WRAP_IND_AWSIZE                                       0xC0150C

#define mmPCIE_WRAP_IND_AWBURST                                      0xC01510

#define mmPCIE_WRAP_IND_AWLOCK                                       0xC01514

#define mmPCIE_WRAP_IND_AWCACHE                                      0xC01518

#define mmPCIE_WRAP_IND_AWPROT                                       0xC0151C

#define mmPCIE_WRAP_IND_AWVALID                                      0xC01520

#define mmPCIE_WRAP_IND_WDATA_0                                      0xC01524

#define mmPCIE_WRAP_IND_WDATA_1                                      0xC01528

#define mmPCIE_WRAP_IND_WDATA_2                                      0xC0152C

#define mmPCIE_WRAP_IND_WDATA_3                                      0xC01530

#define mmPCIE_WRAP_IND_WSTRB                                        0xC01544

#define mmPCIE_WRAP_IND_WLAST                                        0xC01548

#define mmPCIE_WRAP_IND_WVALID                                       0xC0154C

#define mmPCIE_WRAP_IND_BRESP                                        0xC01550

#define mmPCIE_WRAP_IND_BVALID                                       0xC01554

#define mmPCIE_WRAP_IND_ARADDR_0                                     0xC01558

#define mmPCIE_WRAP_IND_ARADDR_1                                     0xC0155C

#define mmPCIE_WRAP_IND_ARLEN                                        0xC01560

#define mmPCIE_WRAP_IND_ARSIZE                                       0xC01564

#define mmPCIE_WRAP_IND_ARBURST                                      0xC01568

#define mmPCIE_WRAP_IND_ARLOCK                                       0xC0156C

#define mmPCIE_WRAP_IND_ARCACHE                                      0xC01570

#define mmPCIE_WRAP_IND_ARPROT                                       0xC01574

#define mmPCIE_WRAP_IND_ARVALID                                      0xC01578

#define mmPCIE_WRAP_IND_RDATA_0                                      0xC0157C

#define mmPCIE_WRAP_IND_RDATA_1                                      0xC01580

#define mmPCIE_WRAP_IND_RDATA_2                                      0xC01584

#define mmPCIE_WRAP_IND_RDATA_3                                      0xC01588

#define mmPCIE_WRAP_IND_RLAST                                        0xC0159C

#define mmPCIE_WRAP_IND_RRESP                                        0xC015A0

#define mmPCIE_WRAP_IND_RVALID                                       0xC015A4

#define mmPCIE_WRAP_IND_AWMISC_INFO                                  0xC015A8

#define mmPCIE_WRAP_IND_AWMISC_INFO_HDR_34DW_0                       0xC015AC

#define mmPCIE_WRAP_IND_AWMISC_INFO_HDR_34DW_1                       0xC015B0

#define mmPCIE_WRAP_IND_AWMISC_INFO_P_TAG                            0xC015B4

#define mmPCIE_WRAP_IND_AWMISC_INFO_ATU_BYPAS                        0xC015B8

#define mmPCIE_WRAP_IND_AWMISC_INFO_FUNC_NUM                         0xC015BC

#define mmPCIE_WRAP_IND_AWMISC_INFO_VFUNC_ACT                        0xC015C0

#define mmPCIE_WRAP_IND_AWMISC_INFO_VFUNC_NUM                        0xC015C4

#define mmPCIE_WRAP_IND_AWMISC_INFO_TLPPRFX                          0xC015C8

#define mmPCIE_WRAP_IND_ARMISC_INFO                                  0xC015CC

#define mmPCIE_WRAP_IND_ARMISC_INFO_TLPPRFX                          0xC015D0

#define mmPCIE_WRAP_IND_ARMISC_INFO_ATU_BYP                          0xC015D4

#define mmPCIE_WRAP_IND_ARMISC_INFO_FUNC_NUM                         0xC015D8

#define mmPCIE_WRAP_IND_ARMISC_INFO_VFUNC_ACT                        0xC015DC

#define mmPCIE_WRAP_IND_ARMISC_INFO_VFUNC_NUM                        0xC015E0

#define mmPCIE_WRAP_SLV_AWMISC_INFO                                  0xC01800

#define mmPCIE_WRAP_SLV_AWMISC_INFO_HDR_34DW_0                       0xC01804

#define mmPCIE_WRAP_SLV_AWMISC_INFO_HDR_34DW_1                       0xC01808

#define mmPCIE_WRAP_SLV_AWMISC_INFO_P_TAG                            0xC0180C

#define mmPCIE_WRAP_SLV_AWMISC_INFO_ATU_BYPAS                        0xC01810

#define mmPCIE_WRAP_SLV_AWMISC_INFO_FUNC_NUM                         0xC01814

#define mmPCIE_WRAP_SLV_AWMISC_INFO_VFUNC_ACT                        0xC01818

#define mmPCIE_WRAP_SLV_AWMISC_INFO_VFUNC_NUM                        0xC0181C

#define mmPCIE_WRAP_SLV_AWMISC_INFO_TLPPRFX                          0xC01820

#define mmPCIE_WRAP_SLV_ARMISC_INFO                                  0xC01824

#define mmPCIE_WRAP_SLV_ARMISC_INFO_TLPPRFX                          0xC01828

#define mmPCIE_WRAP_SLV_ARMISC_INFO_ATU_BYP                          0xC0182C

#define mmPCIE_WRAP_SLV_ARMISC_INFO_FUNC_NUM                         0xC01830

#define mmPCIE_WRAP_SLV_ARMISC_INFO_VFUNC_ACT                        0xC01834

#define mmPCIE_WRAP_SLV_ARMISC_INFO_VFUNC_NUM                        0xC01838

#define mmPCIE_WRAP_MAX_QID                                          0xC01900

#define mmPCIE_WRAP_DB_BASE_ADDR_L_0                                 0xC01910

#define mmPCIE_WRAP_DB_BASE_ADDR_L_1                                 0xC01914

#define mmPCIE_WRAP_DB_BASE_ADDR_L_2                                 0xC01918

#define mmPCIE_WRAP_DB_BASE_ADDR_L_3                                 0xC0191C

#define mmPCIE_WRAP_DB_BASE_ADDR_H_0                                 0xC01920

#define mmPCIE_WRAP_DB_BASE_ADDR_H_1                                 0xC01924

#define mmPCIE_WRAP_DB_BASE_ADDR_H_2                                 0xC01928

#define mmPCIE_WRAP_DB_BASE_ADDR_H_3                                 0xC0192C

#define mmPCIE_WRAP_DB_MASK                                          0xC01940

#define mmPCIE_WRAP_SQ_BASE_ADDR_H                                   0xC01A00

#define mmPCIE_WRAP_SQ_BASE_ADDR_L                                   0xC01A04

#define mmPCIE_WRAP_SQ_STRIDE_ACCRESS                                0xC01A08

#define mmPCIE_WRAP_SQ_POP_CMD                                       0xC01A10

#define mmPCIE_WRAP_SQ_POP_DATA                                      0xC01A14

#define mmPCIE_WRAP_DB_INTR_0                                        0xC01A20

#define mmPCIE_WRAP_DB_INTR_1                                        0xC01A24

#define mmPCIE_WRAP_DB_INTR_2                                        0xC01A28

#define mmPCIE_WRAP_DB_INTR_3                                        0xC01A2C

#define mmPCIE_WRAP_DB_INTR_4                                        0xC01A30

#define mmPCIE_WRAP_DB_INTR_5                                        0xC01A34

#define mmPCIE_WRAP_DB_INTR_6                                        0xC01A38

#define mmPCIE_WRAP_DB_INTR_7                                        0xC01A3C

#define mmPCIE_WRAP_MMU_BYPASS_DMA                                   0xC01A80

#define mmPCIE_WRAP_MMU_BYPASS_NON_DMA                               0xC01A84

#define mmPCIE_WRAP_ASID_NON_DMA                                     0xC01A90

#define mmPCIE_WRAP_ASID_DMA_0                                       0xC01AA0

#define mmPCIE_WRAP_ASID_DMA_1                                       0xC01AA4

#define mmPCIE_WRAP_ASID_DMA_2                                       0xC01AA8

#define mmPCIE_WRAP_ASID_DMA_3                                       0xC01AAC

#define mmPCIE_WRAP_ASID_DMA_4                                       0xC01AB0

#define mmPCIE_WRAP_ASID_DMA_5                                       0xC01AB4

#define mmPCIE_WRAP_ASID_DMA_6                                       0xC01AB8

#define mmPCIE_WRAP_ASID_DMA_7                                       0xC01ABC

#define mmPCIE_WRAP_CPU_HOT_RST                                      0xC01AE0

#define mmPCIE_WRAP_AXI_PROT_OVR                                     0xC01AE4

#define mmPCIE_WRAP_CACHE_OVR                                        0xC01B00

#define mmPCIE_WRAP_LOCK_OVR                                         0xC01B04

#define mmPCIE_WRAP_PROT_OVR                                         0xC01B08

#define mmPCIE_WRAP_ARUSER_OVR                                       0xC01B0C

#define mmPCIE_WRAP_AWUSER_OVR                                       0xC01B10

#define mmPCIE_WRAP_ARUSER_OVR_EN                                    0xC01B14

#define mmPCIE_WRAP_AWUSER_OVR_EN                                    0xC01B18

#define mmPCIE_WRAP_MAX_OUTSTAND                                     0xC01B20

#define mmPCIE_WRAP_MST_IN                                           0xC01B24

#define mmPCIE_WRAP_RSP_OK                                           0xC01B28

#define mmPCIE_WRAP_LBW_CACHE_OVR                                    0xC01B40

#define mmPCIE_WRAP_LBW_LOCK_OVR                                     0xC01B44

#define mmPCIE_WRAP_LBW_PROT_OVR                                     0xC01B48

#define mmPCIE_WRAP_LBW_ARUSER_OVR                                   0xC01B4C

#define mmPCIE_WRAP_LBW_AWUSER_OVR                                   0xC01B50

#define mmPCIE_WRAP_LBW_ARUSER_OVR_EN                                0xC01B58

#define mmPCIE_WRAP_LBW_AWUSER_OVR_EN                                0xC01B5C

#define mmPCIE_WRAP_LBW_MAX_OUTSTAND                                 0xC01B60

#define mmPCIE_WRAP_LBW_MST_IN                                       0xC01B64

#define mmPCIE_WRAP_LBW_RSP_OK                                       0xC01B68

#define mmPCIE_WRAP_QUEUE_INIT                                       0xC01C00

#define mmPCIE_WRAP_AXI_SPLIT_INTR_0                                 0xC01C10

#define mmPCIE_WRAP_AXI_SPLIT_INTR_1                                 0xC01C14

#define mmPCIE_WRAP_DB_AWUSER                                        0xC01D00

#define mmPCIE_WRAP_DB_ARUSER                                        0xC01D04

#define mmPCIE_WRAP_PCIE_AWUSER                                      0xC01D08

#define mmPCIE_WRAP_PCIE_ARUSER                                      0xC01D0C

#define mmPCIE_WRAP_PSOC_AWUSER                                      0xC01D10

#define mmPCIE_WRAP_PSOC_ARUSER                                      0xC01D14

#define mmPCIE_WRAP_SCH_Q_AWUSER                                     0xC01D18

#define mmPCIE_WRAP_SCH_Q_ARUSER                                     0xC01D1C

#define mmPCIE_WRAP_PSOC2PCI_AWUSER                                  0xC01D40

#define mmPCIE_WRAP_PSOC2PCI_ARUSER                                  0xC01D44

#define mmPCIE_WRAP_DRAIN_TIMEOUT                                    0xC01D50

#define mmPCIE_WRAP_DRAIN_CFG                                        0xC01D54

#define mmPCIE_WRAP_DB_AXI_ERR                                       0xC01DE0

#define mmPCIE_WRAP_SPMU_INTR                                        0xC01DE4

#define mmPCIE_WRAP_AXI_INTR                                         0xC01DE8

#define mmPCIE_WRAP_E2E_CTRL                                         0xC01DF0

#endif /* ASIC_REG_PCIE_WRAP_REGS_H_ */
