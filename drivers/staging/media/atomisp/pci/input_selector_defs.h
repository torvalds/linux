/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 */

#ifndef _input_selector_defs_h
#define _input_selector_defs_h

#ifndef HIVE_ISP_ISEL_SEL_BITS
#define HIVE_ISP_ISEL_SEL_BITS                                  2
#endif

#ifndef HIVE_ISP_CH_ID_BITS
#define HIVE_ISP_CH_ID_BITS                                     2
#endif

#ifndef HIVE_ISP_FMT_TYPE_BITS
#define HIVE_ISP_FMT_TYPE_BITS                                  5
#endif

/* gp_register register id's -- Outputs */
#define HIVE_ISEL_GP_REGS_SYNCGEN_ENABLE_IDX                    0
#define HIVE_ISEL_GP_REGS_SYNCGEN_FREE_RUNNING_IDX              1
#define HIVE_ISEL_GP_REGS_SYNCGEN_PAUSE_IDX                     2
#define HIVE_ISEL_GP_REGS_SYNCGEN_NR_FRAMES_IDX                 3
#define HIVE_ISEL_GP_REGS_SYNCGEN_NR_PIX_IDX                    4
#define HIVE_ISEL_GP_REGS_SYNCGEN_NR_LINES_IDX                  5
#define HIVE_ISEL_GP_REGS_SYNCGEN_HBLANK_CYCLES_IDX             6
#define HIVE_ISEL_GP_REGS_SYNCGEN_VBLANK_CYCLES_IDX             7

#define HIVE_ISEL_GP_REGS_SOF_IDX                               8
#define HIVE_ISEL_GP_REGS_EOF_IDX                               9
#define HIVE_ISEL_GP_REGS_SOL_IDX                              10
#define HIVE_ISEL_GP_REGS_EOL_IDX                              11

#define HIVE_ISEL_GP_REGS_PRBS_ENABLE                          12
#define HIVE_ISEL_GP_REGS_PRBS_ENABLE_PORT_B                   13
#define HIVE_ISEL_GP_REGS_PRBS_LFSR_RESET_VALUE                14

#define HIVE_ISEL_GP_REGS_TPG_ENABLE                           15
#define HIVE_ISEL_GP_REGS_TPG_ENABLE_PORT_B                    16
#define HIVE_ISEL_GP_REGS_TPG_HOR_CNT_MASK_IDX                 17
#define HIVE_ISEL_GP_REGS_TPG_VER_CNT_MASK_IDX                 18
#define HIVE_ISEL_GP_REGS_TPG_XY_CNT_MASK_IDX                  19
#define HIVE_ISEL_GP_REGS_TPG_HOR_CNT_DELTA_IDX                20
#define HIVE_ISEL_GP_REGS_TPG_VER_CNT_DELTA_IDX                21
#define HIVE_ISEL_GP_REGS_TPG_MODE_IDX                         22
#define HIVE_ISEL_GP_REGS_TPG_R1_IDX                           23
#define HIVE_ISEL_GP_REGS_TPG_G1_IDX                           24
#define HIVE_ISEL_GP_REGS_TPG_B1_IDX                           25
#define HIVE_ISEL_GP_REGS_TPG_R2_IDX                           26
#define HIVE_ISEL_GP_REGS_TPG_G2_IDX                           27
#define HIVE_ISEL_GP_REGS_TPG_B2_IDX                           28

#define HIVE_ISEL_GP_REGS_CH_ID_IDX                            29
#define HIVE_ISEL_GP_REGS_FMT_TYPE_IDX                         30
#define HIVE_ISEL_GP_REGS_DATA_SEL_IDX                         31
#define HIVE_ISEL_GP_REGS_SBAND_SEL_IDX                        32
#define HIVE_ISEL_GP_REGS_SYNC_SEL_IDX                         33
#define HIVE_ISEL_GP_REGS_SRST_IDX                             37

#define HIVE_ISEL_GP_REGS_SRST_SYNCGEN_BIT                      0
#define HIVE_ISEL_GP_REGS_SRST_PRBS_BIT                         1
#define HIVE_ISEL_GP_REGS_SRST_TPG_BIT                          2
#define HIVE_ISEL_GP_REGS_SRST_FIFO_BIT                         3

/* gp_register register id's -- Inputs   */
#define HIVE_ISEL_GP_REGS_SYNCGEN_HOR_CNT_IDX                  34
#define HIVE_ISEL_GP_REGS_SYNCGEN_VER_CNT_IDX                  35
#define HIVE_ISEL_GP_REGS_SYNCGEN_FRAMES_CNT_IDX               36

/* irq sources isel irq controller */
#define HIVE_ISEL_IRQ_SYNC_GEN_SOF_BIT_ID                       0
#define HIVE_ISEL_IRQ_SYNC_GEN_EOF_BIT_ID                       1
#define HIVE_ISEL_IRQ_SYNC_GEN_SOL_BIT_ID                       2
#define HIVE_ISEL_IRQ_SYNC_GEN_EOL_BIT_ID                       3
#define HIVE_ISEL_IRQ_NUM_IRQS                                  4

#endif /* _input_selector_defs_h */
