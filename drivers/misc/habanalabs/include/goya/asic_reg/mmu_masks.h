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

#ifndef ASIC_REG_MMU_MASKS_H_
#define ASIC_REG_MMU_MASKS_H_

/*
 *****************************************
 *   MMU (Prototype: MMU)
 *****************************************
 */

/* MMU_INPUT_FIFO_THRESHOLD */
#define MMU_INPUT_FIFO_THRESHOLD_PCI_SHIFT                           0
#define MMU_INPUT_FIFO_THRESHOLD_PCI_MASK                            0x7
#define MMU_INPUT_FIFO_THRESHOLD_PSOC_SHIFT                          4
#define MMU_INPUT_FIFO_THRESHOLD_PSOC_MASK                           0x70
#define MMU_INPUT_FIFO_THRESHOLD_DMA_SHIFT                           8
#define MMU_INPUT_FIFO_THRESHOLD_DMA_MASK                            0x700
#define MMU_INPUT_FIFO_THRESHOLD_CPU_SHIFT                           12
#define MMU_INPUT_FIFO_THRESHOLD_CPU_MASK                            0x7000
#define MMU_INPUT_FIFO_THRESHOLD_MME_SHIFT                           16
#define MMU_INPUT_FIFO_THRESHOLD_MME_MASK                            0x70000
#define MMU_INPUT_FIFO_THRESHOLD_TPC_SHIFT                           20
#define MMU_INPUT_FIFO_THRESHOLD_TPC_MASK                            0x700000
#define MMU_INPUT_FIFO_THRESHOLD_OTHER_SHIFT                         24
#define MMU_INPUT_FIFO_THRESHOLD_OTHER_MASK                          0x7000000

/* MMU_MMU_ENABLE */
#define MMU_MMU_ENABLE_R_SHIFT                                       0
#define MMU_MMU_ENABLE_R_MASK                                        0x1

/* MMU_FORCE_ORDERING */
#define MMU_FORCE_ORDERING_DMA_WEAK_ORDERING_SHIFT                   0
#define MMU_FORCE_ORDERING_DMA_WEAK_ORDERING_MASK                    0x1
#define MMU_FORCE_ORDERING_PSOC_WEAK_ORDERING_SHIFT                  1
#define MMU_FORCE_ORDERING_PSOC_WEAK_ORDERING_MASK                   0x2
#define MMU_FORCE_ORDERING_PCI_WEAK_ORDERING_SHIFT                   2
#define MMU_FORCE_ORDERING_PCI_WEAK_ORDERING_MASK                    0x4
#define MMU_FORCE_ORDERING_CPU_WEAK_ORDERING_SHIFT                   3
#define MMU_FORCE_ORDERING_CPU_WEAK_ORDERING_MASK                    0x8
#define MMU_FORCE_ORDERING_MME_WEAK_ORDERING_SHIFT                   4
#define MMU_FORCE_ORDERING_MME_WEAK_ORDERING_MASK                    0x10
#define MMU_FORCE_ORDERING_TPC_WEAK_ORDERING_SHIFT                   5
#define MMU_FORCE_ORDERING_TPC_WEAK_ORDERING_MASK                    0x20
#define MMU_FORCE_ORDERING_DEFAULT_WEAK_ORDERING_SHIFT               6
#define MMU_FORCE_ORDERING_DEFAULT_WEAK_ORDERING_MASK                0x40
#define MMU_FORCE_ORDERING_DMA_STRONG_ORDERING_SHIFT                 8
#define MMU_FORCE_ORDERING_DMA_STRONG_ORDERING_MASK                  0x100
#define MMU_FORCE_ORDERING_PSOC_STRONG_ORDERING_SHIFT                9
#define MMU_FORCE_ORDERING_PSOC_STRONG_ORDERING_MASK                 0x200
#define MMU_FORCE_ORDERING_PCI_STRONG_ORDERING_SHIFT                 10
#define MMU_FORCE_ORDERING_PCI_STRONG_ORDERING_MASK                  0x400
#define MMU_FORCE_ORDERING_CPU_STRONG_ORDERING_SHIFT                 11
#define MMU_FORCE_ORDERING_CPU_STRONG_ORDERING_MASK                  0x800
#define MMU_FORCE_ORDERING_MME_STRONG_ORDERING_SHIFT                 12
#define MMU_FORCE_ORDERING_MME_STRONG_ORDERING_MASK                  0x1000
#define MMU_FORCE_ORDERING_TPC_STRONG_ORDERING_SHIFT                 13
#define MMU_FORCE_ORDERING_TPC_STRONG_ORDERING_MASK                  0x2000
#define MMU_FORCE_ORDERING_DEFAULT_STRONG_ORDERING_SHIFT             14
#define MMU_FORCE_ORDERING_DEFAULT_STRONG_ORDERING_MASK              0x4000

/* MMU_FEATURE_ENABLE */
#define MMU_FEATURE_ENABLE_VA_ORDERING_EN_SHIFT                      0
#define MMU_FEATURE_ENABLE_VA_ORDERING_EN_MASK                       0x1
#define MMU_FEATURE_ENABLE_CLEAN_LINK_LIST_SHIFT                     1
#define MMU_FEATURE_ENABLE_CLEAN_LINK_LIST_MASK                      0x2
#define MMU_FEATURE_ENABLE_HOP_OFFSET_EN_SHIFT                       2
#define MMU_FEATURE_ENABLE_HOP_OFFSET_EN_MASK                        0x4
#define MMU_FEATURE_ENABLE_OBI_ORDERING_EN_SHIFT                     3
#define MMU_FEATURE_ENABLE_OBI_ORDERING_EN_MASK                      0x8
#define MMU_FEATURE_ENABLE_STRONG_ORDERING_READ_EN_SHIFT             4
#define MMU_FEATURE_ENABLE_STRONG_ORDERING_READ_EN_MASK              0x10
#define MMU_FEATURE_ENABLE_TRACE_ENABLE_SHIFT                        5
#define MMU_FEATURE_ENABLE_TRACE_ENABLE_MASK                         0x20

/* MMU_VA_ORDERING_MASK_31_7 */
#define MMU_VA_ORDERING_MASK_31_7_R_SHIFT                            0
#define MMU_VA_ORDERING_MASK_31_7_R_MASK                             0x1FFFFFF

/* MMU_VA_ORDERING_MASK_49_32 */
#define MMU_VA_ORDERING_MASK_49_32_R_SHIFT                           0
#define MMU_VA_ORDERING_MASK_49_32_R_MASK                            0x3FFFF

/* MMU_LOG2_DDR_SIZE */
#define MMU_LOG2_DDR_SIZE_R_SHIFT                                    0
#define MMU_LOG2_DDR_SIZE_R_MASK                                     0xFF

/* MMU_SCRAMBLER */
#define MMU_SCRAMBLER_ADDR_BIT_SHIFT                                 0
#define MMU_SCRAMBLER_ADDR_BIT_MASK                                  0x3F
#define MMU_SCRAMBLER_SINGLE_DDR_EN_SHIFT                            6
#define MMU_SCRAMBLER_SINGLE_DDR_EN_MASK                             0x40
#define MMU_SCRAMBLER_SINGLE_DDR_ID_SHIFT                            7
#define MMU_SCRAMBLER_SINGLE_DDR_ID_MASK                             0x80

/* MMU_MEM_INIT_BUSY */
#define MMU_MEM_INIT_BUSY_DATA_SHIFT                                 0
#define MMU_MEM_INIT_BUSY_DATA_MASK                                  0x3
#define MMU_MEM_INIT_BUSY_OBI0_SHIFT                                 2
#define MMU_MEM_INIT_BUSY_OBI0_MASK                                  0x4
#define MMU_MEM_INIT_BUSY_OBI1_SHIFT                                 3
#define MMU_MEM_INIT_BUSY_OBI1_MASK                                  0x8

/* MMU_SPI_MASK */
#define MMU_SPI_MASK_R_SHIFT                                         0
#define MMU_SPI_MASK_R_MASK                                          0xFF

/* MMU_SPI_CAUSE */
#define MMU_SPI_CAUSE_R_SHIFT                                        0
#define MMU_SPI_CAUSE_R_MASK                                         0xFF

/* MMU_PAGE_ERROR_CAPTURE */
#define MMU_PAGE_ERROR_CAPTURE_VA_49_32_SHIFT                        0
#define MMU_PAGE_ERROR_CAPTURE_VA_49_32_MASK                         0x3FFFF
#define MMU_PAGE_ERROR_CAPTURE_ENTRY_VALID_SHIFT                     18
#define MMU_PAGE_ERROR_CAPTURE_ENTRY_VALID_MASK                      0x40000

/* MMU_PAGE_ERROR_CAPTURE_VA */
#define MMU_PAGE_ERROR_CAPTURE_VA_VA_31_0_SHIFT                      0
#define MMU_PAGE_ERROR_CAPTURE_VA_VA_31_0_MASK                       0xFFFFFFFF

/* MMU_ACCESS_ERROR_CAPTURE */
#define MMU_ACCESS_ERROR_CAPTURE_VA_49_32_SHIFT                      0
#define MMU_ACCESS_ERROR_CAPTURE_VA_49_32_MASK                       0x3FFFF
#define MMU_ACCESS_ERROR_CAPTURE_ENTRY_VALID_SHIFT                   18
#define MMU_ACCESS_ERROR_CAPTURE_ENTRY_VALID_MASK                    0x40000

/* MMU_ACCESS_ERROR_CAPTURE_VA */
#define MMU_ACCESS_ERROR_CAPTURE_VA_VA_31_0_SHIFT                    0
#define MMU_ACCESS_ERROR_CAPTURE_VA_VA_31_0_MASK                     0xFFFFFFFF

#endif /* ASIC_REG_MMU_MASKS_H_ */

