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

#ifndef ASIC_REG_PCI_NRTR_REGS_H_
#define ASIC_REG_PCI_NRTR_REGS_H_

/*
 *****************************************
 *   PCI_NRTR (Prototype: IF_NRTR)
 *****************************************
 */

#define mmPCI_NRTR_HBW_MAX_CRED                                      0x100

#define mmPCI_NRTR_LBW_MAX_CRED                                      0x120

#define mmPCI_NRTR_DBG_E_ARB                                         0x300

#define mmPCI_NRTR_DBG_W_ARB                                         0x304

#define mmPCI_NRTR_DBG_N_ARB                                         0x308

#define mmPCI_NRTR_DBG_S_ARB                                         0x30C

#define mmPCI_NRTR_DBG_L_ARB                                         0x310

#define mmPCI_NRTR_DBG_E_ARB_MAX                                     0x320

#define mmPCI_NRTR_DBG_W_ARB_MAX                                     0x324

#define mmPCI_NRTR_DBG_N_ARB_MAX                                     0x328

#define mmPCI_NRTR_DBG_S_ARB_MAX                                     0x32C

#define mmPCI_NRTR_DBG_L_ARB_MAX                                     0x330

#define mmPCI_NRTR_SPLIT_COEF_0                                      0x400

#define mmPCI_NRTR_SPLIT_COEF_1                                      0x404

#define mmPCI_NRTR_SPLIT_COEF_2                                      0x408

#define mmPCI_NRTR_SPLIT_COEF_3                                      0x40C

#define mmPCI_NRTR_SPLIT_COEF_4                                      0x410

#define mmPCI_NRTR_SPLIT_COEF_5                                      0x414

#define mmPCI_NRTR_SPLIT_COEF_6                                      0x418

#define mmPCI_NRTR_SPLIT_COEF_7                                      0x41C

#define mmPCI_NRTR_SPLIT_COEF_8                                      0x420

#define mmPCI_NRTR_SPLIT_COEF_9                                      0x424

#define mmPCI_NRTR_SPLIT_CFG                                         0x440

#define mmPCI_NRTR_SPLIT_RD_SAT                                      0x444

#define mmPCI_NRTR_SPLIT_RD_RST_TOKEN                                0x448

#define mmPCI_NRTR_SPLIT_RD_TIMEOUT_0                                0x44C

#define mmPCI_NRTR_SPLIT_RD_TIMEOUT_1                                0x450

#define mmPCI_NRTR_SPLIT_WR_SAT                                      0x454

#define mmPCI_NRTR_WPLIT_WR_TST_TOLEN                                0x458

#define mmPCI_NRTR_SPLIT_WR_TIMEOUT_0                                0x45C

#define mmPCI_NRTR_SPLIT_WR_TIMEOUT_1                                0x460

#define mmPCI_NRTR_HBW_RANGE_HIT                                     0x470

#define mmPCI_NRTR_HBW_RANGE_MASK_L_0                                0x480

#define mmPCI_NRTR_HBW_RANGE_MASK_L_1                                0x484

#define mmPCI_NRTR_HBW_RANGE_MASK_L_2                                0x488

#define mmPCI_NRTR_HBW_RANGE_MASK_L_3                                0x48C

#define mmPCI_NRTR_HBW_RANGE_MASK_L_4                                0x490

#define mmPCI_NRTR_HBW_RANGE_MASK_L_5                                0x494

#define mmPCI_NRTR_HBW_RANGE_MASK_L_6                                0x498

#define mmPCI_NRTR_HBW_RANGE_MASK_L_7                                0x49C

#define mmPCI_NRTR_HBW_RANGE_MASK_H_0                                0x4A0

#define mmPCI_NRTR_HBW_RANGE_MASK_H_1                                0x4A4

#define mmPCI_NRTR_HBW_RANGE_MASK_H_2                                0x4A8

#define mmPCI_NRTR_HBW_RANGE_MASK_H_3                                0x4AC

#define mmPCI_NRTR_HBW_RANGE_MASK_H_4                                0x4B0

#define mmPCI_NRTR_HBW_RANGE_MASK_H_5                                0x4B4

#define mmPCI_NRTR_HBW_RANGE_MASK_H_6                                0x4B8

#define mmPCI_NRTR_HBW_RANGE_MASK_H_7                                0x4BC

#define mmPCI_NRTR_HBW_RANGE_BASE_L_0                                0x4C0

#define mmPCI_NRTR_HBW_RANGE_BASE_L_1                                0x4C4

#define mmPCI_NRTR_HBW_RANGE_BASE_L_2                                0x4C8

#define mmPCI_NRTR_HBW_RANGE_BASE_L_3                                0x4CC

#define mmPCI_NRTR_HBW_RANGE_BASE_L_4                                0x4D0

#define mmPCI_NRTR_HBW_RANGE_BASE_L_5                                0x4D4

#define mmPCI_NRTR_HBW_RANGE_BASE_L_6                                0x4D8

#define mmPCI_NRTR_HBW_RANGE_BASE_L_7                                0x4DC

#define mmPCI_NRTR_HBW_RANGE_BASE_H_0                                0x4E0

#define mmPCI_NRTR_HBW_RANGE_BASE_H_1                                0x4E4

#define mmPCI_NRTR_HBW_RANGE_BASE_H_2                                0x4E8

#define mmPCI_NRTR_HBW_RANGE_BASE_H_3                                0x4EC

#define mmPCI_NRTR_HBW_RANGE_BASE_H_4                                0x4F0

#define mmPCI_NRTR_HBW_RANGE_BASE_H_5                                0x4F4

#define mmPCI_NRTR_HBW_RANGE_BASE_H_6                                0x4F8

#define mmPCI_NRTR_HBW_RANGE_BASE_H_7                                0x4FC

#define mmPCI_NRTR_LBW_RANGE_HIT                                     0x500

#define mmPCI_NRTR_LBW_RANGE_MASK_0                                  0x510

#define mmPCI_NRTR_LBW_RANGE_MASK_1                                  0x514

#define mmPCI_NRTR_LBW_RANGE_MASK_2                                  0x518

#define mmPCI_NRTR_LBW_RANGE_MASK_3                                  0x51C

#define mmPCI_NRTR_LBW_RANGE_MASK_4                                  0x520

#define mmPCI_NRTR_LBW_RANGE_MASK_5                                  0x524

#define mmPCI_NRTR_LBW_RANGE_MASK_6                                  0x528

#define mmPCI_NRTR_LBW_RANGE_MASK_7                                  0x52C

#define mmPCI_NRTR_LBW_RANGE_MASK_8                                  0x530

#define mmPCI_NRTR_LBW_RANGE_MASK_9                                  0x534

#define mmPCI_NRTR_LBW_RANGE_MASK_10                                 0x538

#define mmPCI_NRTR_LBW_RANGE_MASK_11                                 0x53C

#define mmPCI_NRTR_LBW_RANGE_MASK_12                                 0x540

#define mmPCI_NRTR_LBW_RANGE_MASK_13                                 0x544

#define mmPCI_NRTR_LBW_RANGE_MASK_14                                 0x548

#define mmPCI_NRTR_LBW_RANGE_MASK_15                                 0x54C

#define mmPCI_NRTR_LBW_RANGE_BASE_0                                  0x550

#define mmPCI_NRTR_LBW_RANGE_BASE_1                                  0x554

#define mmPCI_NRTR_LBW_RANGE_BASE_2                                  0x558

#define mmPCI_NRTR_LBW_RANGE_BASE_3                                  0x55C

#define mmPCI_NRTR_LBW_RANGE_BASE_4                                  0x560

#define mmPCI_NRTR_LBW_RANGE_BASE_5                                  0x564

#define mmPCI_NRTR_LBW_RANGE_BASE_6                                  0x568

#define mmPCI_NRTR_LBW_RANGE_BASE_7                                  0x56C

#define mmPCI_NRTR_LBW_RANGE_BASE_8                                  0x570

#define mmPCI_NRTR_LBW_RANGE_BASE_9                                  0x574

#define mmPCI_NRTR_LBW_RANGE_BASE_10                                 0x578

#define mmPCI_NRTR_LBW_RANGE_BASE_11                                 0x57C

#define mmPCI_NRTR_LBW_RANGE_BASE_12                                 0x580

#define mmPCI_NRTR_LBW_RANGE_BASE_13                                 0x584

#define mmPCI_NRTR_LBW_RANGE_BASE_14                                 0x588

#define mmPCI_NRTR_LBW_RANGE_BASE_15                                 0x58C

#define mmPCI_NRTR_RGLTR                                             0x590

#define mmPCI_NRTR_RGLTR_WR_RESULT                                   0x594

#define mmPCI_NRTR_RGLTR_RD_RESULT                                   0x598

#define mmPCI_NRTR_SCRAMB_EN                                         0x600

#define mmPCI_NRTR_NON_LIN_SCRAMB                                    0x604

#endif /* ASIC_REG_PCI_NRTR_REGS_H_ */

