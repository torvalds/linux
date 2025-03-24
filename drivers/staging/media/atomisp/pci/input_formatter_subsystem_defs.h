/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 */

#ifndef _if_subsystem_defs_h__
#define _if_subsystem_defs_h__

#define HIVE_IFMT_GP_REGS_INPUT_SWITCH_LUT_REG_0            0
#define HIVE_IFMT_GP_REGS_INPUT_SWITCH_LUT_REG_1            1
#define HIVE_IFMT_GP_REGS_INPUT_SWITCH_LUT_REG_2            2
#define HIVE_IFMT_GP_REGS_INPUT_SWITCH_LUT_REG_3            3
#define HIVE_IFMT_GP_REGS_INPUT_SWITCH_LUT_REG_4            4
#define HIVE_IFMT_GP_REGS_INPUT_SWITCH_LUT_REG_5            5
#define HIVE_IFMT_GP_REGS_INPUT_SWITCH_LUT_REG_6            6
#define HIVE_IFMT_GP_REGS_INPUT_SWITCH_LUT_REG_7            7
#define HIVE_IFMT_GP_REGS_INPUT_SWITCH_FSYNC_LUT_REG        8
#define HIVE_IFMT_GP_REGS_SRST_IDX                          9
#define HIVE_IFMT_GP_REGS_SLV_REG_SRST_IDX                 10

#define HIVE_IFMT_GP_REGS_CH_ID_FMT_TYPE_IDX               11

#define HIVE_IFMT_GP_REGS_INPUT_SWITCH_LUT_REG_BASE         HIVE_IFMT_GP_REGS_INPUT_SWITCH_LUT_REG_0

/* order of the input bits for the ifmt irq controller */
#define HIVE_IFMT_IRQ_IFT_PRIM_BIT_ID                       0
#define HIVE_IFMT_IRQ_IFT_PRIM_B_BIT_ID                     1
#define HIVE_IFMT_IRQ_IFT_SEC_BIT_ID                        2
#define HIVE_IFMT_IRQ_MEM_CPY_BIT_ID                        3
#define HIVE_IFMT_IRQ_SIDEBAND_CHANGED_BIT_ID               4

/* order of the input bits for the ifmt Soft reset register */
#define HIVE_IFMT_GP_REGS_SRST_IFT_PRIM_BIT_IDX             0
#define HIVE_IFMT_GP_REGS_SRST_IFT_PRIM_B_BIT_IDX           1
#define HIVE_IFMT_GP_REGS_SRST_IFT_SEC_BIT_IDX              2
#define HIVE_IFMT_GP_REGS_SRST_MEM_CPY_BIT_IDX              3

/* order of the input bits for the ifmt Soft reset register */
#define HIVE_IFMT_GP_REGS_SLV_REG_SRST_IFT_PRIM_BIT_IDX     0
#define HIVE_IFMT_GP_REGS_SLV_REG_SRST_IFT_PRIM_B_BIT_IDX   1
#define HIVE_IFMT_GP_REGS_SLV_REG_SRST_IFT_SEC_BIT_IDX      2
#define HIVE_IFMT_GP_REGS_SLV_REG_SRST_MEM_CPY_BIT_IDX      3

#endif /* _if_subsystem_defs_h__ */
