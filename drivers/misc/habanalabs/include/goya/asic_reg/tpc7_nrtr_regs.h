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

#ifndef ASIC_REG_TPC7_NRTR_REGS_H_
#define ASIC_REG_TPC7_NRTR_REGS_H_

/*
 *****************************************
 *   TPC7_NRTR (Prototype: IF_NRTR)
 *****************************************
 */

#define mmTPC7_NRTR_HBW_MAX_CRED                                     0xFC0100

#define mmTPC7_NRTR_LBW_MAX_CRED                                     0xFC0120

#define mmTPC7_NRTR_DBG_E_ARB                                        0xFC0300

#define mmTPC7_NRTR_DBG_W_ARB                                        0xFC0304

#define mmTPC7_NRTR_DBG_N_ARB                                        0xFC0308

#define mmTPC7_NRTR_DBG_S_ARB                                        0xFC030C

#define mmTPC7_NRTR_DBG_L_ARB                                        0xFC0310

#define mmTPC7_NRTR_DBG_E_ARB_MAX                                    0xFC0320

#define mmTPC7_NRTR_DBG_W_ARB_MAX                                    0xFC0324

#define mmTPC7_NRTR_DBG_N_ARB_MAX                                    0xFC0328

#define mmTPC7_NRTR_DBG_S_ARB_MAX                                    0xFC032C

#define mmTPC7_NRTR_DBG_L_ARB_MAX                                    0xFC0330

#define mmTPC7_NRTR_SPLIT_COEF_0                                     0xFC0400

#define mmTPC7_NRTR_SPLIT_COEF_1                                     0xFC0404

#define mmTPC7_NRTR_SPLIT_COEF_2                                     0xFC0408

#define mmTPC7_NRTR_SPLIT_COEF_3                                     0xFC040C

#define mmTPC7_NRTR_SPLIT_COEF_4                                     0xFC0410

#define mmTPC7_NRTR_SPLIT_COEF_5                                     0xFC0414

#define mmTPC7_NRTR_SPLIT_COEF_6                                     0xFC0418

#define mmTPC7_NRTR_SPLIT_COEF_7                                     0xFC041C

#define mmTPC7_NRTR_SPLIT_COEF_8                                     0xFC0420

#define mmTPC7_NRTR_SPLIT_COEF_9                                     0xFC0424

#define mmTPC7_NRTR_SPLIT_CFG                                        0xFC0440

#define mmTPC7_NRTR_SPLIT_RD_SAT                                     0xFC0444

#define mmTPC7_NRTR_SPLIT_RD_RST_TOKEN                               0xFC0448

#define mmTPC7_NRTR_SPLIT_RD_TIMEOUT_0                               0xFC044C

#define mmTPC7_NRTR_SPLIT_RD_TIMEOUT_1                               0xFC0450

#define mmTPC7_NRTR_SPLIT_WR_SAT                                     0xFC0454

#define mmTPC7_NRTR_WPLIT_WR_TST_TOLEN                               0xFC0458

#define mmTPC7_NRTR_SPLIT_WR_TIMEOUT_0                               0xFC045C

#define mmTPC7_NRTR_SPLIT_WR_TIMEOUT_1                               0xFC0460

#define mmTPC7_NRTR_HBW_RANGE_HIT                                    0xFC0470

#define mmTPC7_NRTR_HBW_RANGE_MASK_L_0                               0xFC0480

#define mmTPC7_NRTR_HBW_RANGE_MASK_L_1                               0xFC0484

#define mmTPC7_NRTR_HBW_RANGE_MASK_L_2                               0xFC0488

#define mmTPC7_NRTR_HBW_RANGE_MASK_L_3                               0xFC048C

#define mmTPC7_NRTR_HBW_RANGE_MASK_L_4                               0xFC0490

#define mmTPC7_NRTR_HBW_RANGE_MASK_L_5                               0xFC0494

#define mmTPC7_NRTR_HBW_RANGE_MASK_L_6                               0xFC0498

#define mmTPC7_NRTR_HBW_RANGE_MASK_L_7                               0xFC049C

#define mmTPC7_NRTR_HBW_RANGE_MASK_H_0                               0xFC04A0

#define mmTPC7_NRTR_HBW_RANGE_MASK_H_1                               0xFC04A4

#define mmTPC7_NRTR_HBW_RANGE_MASK_H_2                               0xFC04A8

#define mmTPC7_NRTR_HBW_RANGE_MASK_H_3                               0xFC04AC

#define mmTPC7_NRTR_HBW_RANGE_MASK_H_4                               0xFC04B0

#define mmTPC7_NRTR_HBW_RANGE_MASK_H_5                               0xFC04B4

#define mmTPC7_NRTR_HBW_RANGE_MASK_H_6                               0xFC04B8

#define mmTPC7_NRTR_HBW_RANGE_MASK_H_7                               0xFC04BC

#define mmTPC7_NRTR_HBW_RANGE_BASE_L_0                               0xFC04C0

#define mmTPC7_NRTR_HBW_RANGE_BASE_L_1                               0xFC04C4

#define mmTPC7_NRTR_HBW_RANGE_BASE_L_2                               0xFC04C8

#define mmTPC7_NRTR_HBW_RANGE_BASE_L_3                               0xFC04CC

#define mmTPC7_NRTR_HBW_RANGE_BASE_L_4                               0xFC04D0

#define mmTPC7_NRTR_HBW_RANGE_BASE_L_5                               0xFC04D4

#define mmTPC7_NRTR_HBW_RANGE_BASE_L_6                               0xFC04D8

#define mmTPC7_NRTR_HBW_RANGE_BASE_L_7                               0xFC04DC

#define mmTPC7_NRTR_HBW_RANGE_BASE_H_0                               0xFC04E0

#define mmTPC7_NRTR_HBW_RANGE_BASE_H_1                               0xFC04E4

#define mmTPC7_NRTR_HBW_RANGE_BASE_H_2                               0xFC04E8

#define mmTPC7_NRTR_HBW_RANGE_BASE_H_3                               0xFC04EC

#define mmTPC7_NRTR_HBW_RANGE_BASE_H_4                               0xFC04F0

#define mmTPC7_NRTR_HBW_RANGE_BASE_H_5                               0xFC04F4

#define mmTPC7_NRTR_HBW_RANGE_BASE_H_6                               0xFC04F8

#define mmTPC7_NRTR_HBW_RANGE_BASE_H_7                               0xFC04FC

#define mmTPC7_NRTR_LBW_RANGE_HIT                                    0xFC0500

#define mmTPC7_NRTR_LBW_RANGE_MASK_0                                 0xFC0510

#define mmTPC7_NRTR_LBW_RANGE_MASK_1                                 0xFC0514

#define mmTPC7_NRTR_LBW_RANGE_MASK_2                                 0xFC0518

#define mmTPC7_NRTR_LBW_RANGE_MASK_3                                 0xFC051C

#define mmTPC7_NRTR_LBW_RANGE_MASK_4                                 0xFC0520

#define mmTPC7_NRTR_LBW_RANGE_MASK_5                                 0xFC0524

#define mmTPC7_NRTR_LBW_RANGE_MASK_6                                 0xFC0528

#define mmTPC7_NRTR_LBW_RANGE_MASK_7                                 0xFC052C

#define mmTPC7_NRTR_LBW_RANGE_MASK_8                                 0xFC0530

#define mmTPC7_NRTR_LBW_RANGE_MASK_9                                 0xFC0534

#define mmTPC7_NRTR_LBW_RANGE_MASK_10                                0xFC0538

#define mmTPC7_NRTR_LBW_RANGE_MASK_11                                0xFC053C

#define mmTPC7_NRTR_LBW_RANGE_MASK_12                                0xFC0540

#define mmTPC7_NRTR_LBW_RANGE_MASK_13                                0xFC0544

#define mmTPC7_NRTR_LBW_RANGE_MASK_14                                0xFC0548

#define mmTPC7_NRTR_LBW_RANGE_MASK_15                                0xFC054C

#define mmTPC7_NRTR_LBW_RANGE_BASE_0                                 0xFC0550

#define mmTPC7_NRTR_LBW_RANGE_BASE_1                                 0xFC0554

#define mmTPC7_NRTR_LBW_RANGE_BASE_2                                 0xFC0558

#define mmTPC7_NRTR_LBW_RANGE_BASE_3                                 0xFC055C

#define mmTPC7_NRTR_LBW_RANGE_BASE_4                                 0xFC0560

#define mmTPC7_NRTR_LBW_RANGE_BASE_5                                 0xFC0564

#define mmTPC7_NRTR_LBW_RANGE_BASE_6                                 0xFC0568

#define mmTPC7_NRTR_LBW_RANGE_BASE_7                                 0xFC056C

#define mmTPC7_NRTR_LBW_RANGE_BASE_8                                 0xFC0570

#define mmTPC7_NRTR_LBW_RANGE_BASE_9                                 0xFC0574

#define mmTPC7_NRTR_LBW_RANGE_BASE_10                                0xFC0578

#define mmTPC7_NRTR_LBW_RANGE_BASE_11                                0xFC057C

#define mmTPC7_NRTR_LBW_RANGE_BASE_12                                0xFC0580

#define mmTPC7_NRTR_LBW_RANGE_BASE_13                                0xFC0584

#define mmTPC7_NRTR_LBW_RANGE_BASE_14                                0xFC0588

#define mmTPC7_NRTR_LBW_RANGE_BASE_15                                0xFC058C

#define mmTPC7_NRTR_RGLTR                                            0xFC0590

#define mmTPC7_NRTR_RGLTR_WR_RESULT                                  0xFC0594

#define mmTPC7_NRTR_RGLTR_RD_RESULT                                  0xFC0598

#define mmTPC7_NRTR_SCRAMB_EN                                        0xFC0600

#define mmTPC7_NRTR_NON_LIN_SCRAMB                                   0xFC0604

#endif /* ASIC_REG_TPC7_NRTR_REGS_H_ */
