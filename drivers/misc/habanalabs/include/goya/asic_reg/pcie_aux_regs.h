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

#ifndef ASIC_REG_PCIE_AUX_REGS_H_
#define ASIC_REG_PCIE_AUX_REGS_H_

/*
 *****************************************
 *   PCIE_AUX (Prototype: PCIE_AUX)
 *****************************************
 */

#define mmPCIE_AUX_APB_TIMEOUT                                       0xC07004

#define mmPCIE_AUX_PHY_INIT                                          0xC07100

#define mmPCIE_AUX_LTR_MAX_LATENCY                                   0xC07138

#define mmPCIE_AUX_BAR0_START_L                                      0xC07160

#define mmPCIE_AUX_BAR0_START_H                                      0xC07164

#define mmPCIE_AUX_BAR1_START                                        0xC07168

#define mmPCIE_AUX_BAR2_START_L                                      0xC0716C

#define mmPCIE_AUX_BAR2_START_H                                      0xC07170

#define mmPCIE_AUX_BAR3_START                                        0xC07174

#define mmPCIE_AUX_BAR4_START_L                                      0xC07178

#define mmPCIE_AUX_BAR4_START_H                                      0xC0717C

#define mmPCIE_AUX_BAR5_START                                        0xC07180

#define mmPCIE_AUX_BAR0_LIMIT_L                                      0xC07184

#define mmPCIE_AUX_BAR0_LIMIT_H                                      0xC07188

#define mmPCIE_AUX_BAR1_LIMIT                                        0xC0718C

#define mmPCIE_AUX_BAR2_LIMIT_L                                      0xC07190

#define mmPCIE_AUX_BAR2_LIMIT_H                                      0xC07194

#define mmPCIE_AUX_BAR3_LIMIT                                        0xC07198

#define mmPCIE_AUX_BAR4_LIMIT_L                                      0xC0719C

#define mmPCIE_AUX_BAR4_LIMIT_H                                      0xC07200

#define mmPCIE_AUX_BAR5_LIMIT                                        0xC07204

#define mmPCIE_AUX_BUS_MASTER_EN                                     0xC07208

#define mmPCIE_AUX_MEM_SPACE_EN                                      0xC0720C

#define mmPCIE_AUX_MAX_RD_REQ_SIZE                                   0xC07210

#define mmPCIE_AUX_MAX_PAYLOAD_SIZE                                  0xC07214

#define mmPCIE_AUX_EXT_TAG_EN                                        0xC07218

#define mmPCIE_AUX_RCB                                               0xC0721C

#define mmPCIE_AUX_PM_NO_SOFT_RST                                    0xC07220

#define mmPCIE_AUX_PBUS_NUM                                          0xC07224

#define mmPCIE_AUX_PBUS_DEV_NUM                                      0xC07228

#define mmPCIE_AUX_NO_SNOOP_EN                                       0xC0722C

#define mmPCIE_AUX_RELAX_ORDER_EN                                    0xC07230

#define mmPCIE_AUX_HP_SLOT_CTRL_ACCESS                               0xC07234

#define mmPCIE_AUX_DLL_STATE_CHGED_EN                                0xC07238

#define mmPCIE_AUX_CMP_CPLED_INT_EN                                  0xC0723C

#define mmPCIE_AUX_HP_INT_EN                                         0xC07340

#define mmPCIE_AUX_PRE_DET_CHGEN_EN                                  0xC07344

#define mmPCIE_AUX_MRL_SENSOR_CHGED_EN                               0xC07348

#define mmPCIE_AUX_PWR_FAULT_DET_EN                                  0xC0734C

#define mmPCIE_AUX_ATTEN_BUTTON_PRESSED_EN                           0xC07350

#define mmPCIE_AUX_PF_FLR_ACTIVE                                     0xC07360

#define mmPCIE_AUX_PF_FLR_DONE                                       0xC07364

#define mmPCIE_AUX_FLR_INT                                           0xC07390

#define mmPCIE_AUX_LTR_M_EN                                          0xC073B0

#define mmPCIE_AUX_LTSSM_EN                                          0xC07428

#define mmPCIE_AUX_SYS_INTR                                          0xC07440

#define mmPCIE_AUX_INT_DISABLE                                       0xC07444

#define mmPCIE_AUX_SMLH_LINK_UP                                      0xC07448

#define mmPCIE_AUX_PM_CURR_STATE                                     0xC07450

#define mmPCIE_AUX_RDLH_LINK_UP                                      0xC07458

#define mmPCIE_AUX_BRDG_SLV_XFER_PENDING                             0xC0745C

#define mmPCIE_AUX_BRDG_DBI_XFER_PENDING                             0xC07460

#define mmPCIE_AUX_AUTO_SP_DIS                                       0xC07478

#define mmPCIE_AUX_DBI                                               0xC07490

#define mmPCIE_AUX_DBI_32                                            0xC07494

#define mmPCIE_AUX_DIAG_STATUS_BUS_0                                 0xC074A4

#define mmPCIE_AUX_DIAG_STATUS_BUS_1                                 0xC074A8

#define mmPCIE_AUX_DIAG_STATUS_BUS_2                                 0xC074AC

#define mmPCIE_AUX_DIAG_STATUS_BUS_3                                 0xC074B0

#define mmPCIE_AUX_DIAG_STATUS_BUS_4                                 0xC074B4

#define mmPCIE_AUX_DIAG_STATUS_BUS_5                                 0xC074B8

#define mmPCIE_AUX_DIAG_STATUS_BUS_6                                 0xC074BC

#define mmPCIE_AUX_DIAG_STATUS_BUS_7                                 0xC074C0

#define mmPCIE_AUX_DIAG_STATUS_BUS_8                                 0xC074C4

#define mmPCIE_AUX_DIAG_STATUS_BUS_9                                 0xC074C8

#define mmPCIE_AUX_DIAG_STATUS_BUS_10                                0xC074CC

#define mmPCIE_AUX_DIAG_STATUS_BUS_11                                0xC074D0

#define mmPCIE_AUX_DIAG_STATUS_BUS_12                                0xC074D4

#define mmPCIE_AUX_DIAG_STATUS_BUS_13                                0xC074D8

#define mmPCIE_AUX_DIAG_STATUS_BUS_14                                0xC074DC

#define mmPCIE_AUX_DIAG_STATUS_BUS_15                                0xC074E0

#define mmPCIE_AUX_DIAG_STATUS_BUS_16                                0xC074E4

#define mmPCIE_AUX_DIAG_STATUS_BUS_17                                0xC074E8

#define mmPCIE_AUX_DIAG_STATUS_BUS_18                                0xC074EC

#define mmPCIE_AUX_DIAG_STATUS_BUS_19                                0xC074F0

#define mmPCIE_AUX_DIAG_STATUS_BUS_20                                0xC074F4

#define mmPCIE_AUX_DIAG_STATUS_BUS_21                                0xC074F8

#define mmPCIE_AUX_DIAG_STATUS_BUS_22                                0xC074FC

#define mmPCIE_AUX_DIAG_STATUS_BUS_23                                0xC07500

#define mmPCIE_AUX_DIAG_STATUS_BUS_24                                0xC07504

#define mmPCIE_AUX_DIAG_STATUS_BUS_25                                0xC07508

#define mmPCIE_AUX_DIAG_STATUS_BUS_26                                0xC0750C

#define mmPCIE_AUX_DIAG_STATUS_BUS_27                                0xC07510

#define mmPCIE_AUX_DIAG_STATUS_BUS_28                                0xC07514

#define mmPCIE_AUX_CDM_RAS_DES_EC_INFO_0                             0xC07640

#define mmPCIE_AUX_CDM_RAS_DES_EC_INFO_1                             0xC07644

#define mmPCIE_AUX_CDM_RAS_DES_EC_INFO_2                             0xC07648

#define mmPCIE_AUX_CDM_RAS_DES_EC_INFO_3                             0xC0764C

#define mmPCIE_AUX_CDM_RAS_DES_EC_INFO_4                             0xC07650

#define mmPCIE_AUX_CDM_RAS_DES_EC_INFO_5                             0xC07654

#define mmPCIE_AUX_CDM_RAS_DES_EC_INFO_6                             0xC07658

#define mmPCIE_AUX_CDM_RAS_DES_EC_INFO_7                             0xC0765C

#define mmPCIE_AUX_CDM_RAS_DES_SD_COMMON_0                           0xC07744

#define mmPCIE_AUX_CDM_RAS_DES_SD_COMMON_1                           0xC07748

#define mmPCIE_AUX_CDM_RAS_DES_SD_COMMON_2                           0xC0774C

#define mmPCIE_AUX_APP_RAS_DES_TBA_CTRL                              0xC07774

#define mmPCIE_AUX_PM_DSTATE                                         0xC07840

#define mmPCIE_AUX_PM_PME_EN                                         0xC07844

#define mmPCIE_AUX_PM_LINKST_IN_L0S                                  0xC07848

#define mmPCIE_AUX_PM_LINKST_IN_L1                                   0xC0784C

#define mmPCIE_AUX_PM_LINKST_IN_L2                                   0xC07850

#define mmPCIE_AUX_PM_LINKST_L2_EXIT                                 0xC07854

#define mmPCIE_AUX_PM_STATUS                                         0xC07858

#define mmPCIE_AUX_APP_READY_ENTER_L23                               0xC0785C

#define mmPCIE_AUX_APP_XFER_PENDING                                  0xC07860

#define mmPCIE_AUX_APP_REQ_L1                                        0xC07930

#define mmPCIE_AUX_AUX_PM_EN                                         0xC07934

#define mmPCIE_AUX_APPS_PM_XMT_PME                                   0xC07938

#define mmPCIE_AUX_OUTBAND_PWRUP_CMD                                 0xC07940

#define mmPCIE_AUX_PERST                                             0xC079B8

#endif /* ASIC_REG_PCIE_AUX_REGS_H_ */
