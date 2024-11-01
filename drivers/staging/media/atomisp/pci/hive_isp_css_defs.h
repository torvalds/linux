/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#ifndef _hive_isp_css_defs_h__
#define _hive_isp_css_defs_h__

#define HIVE_ISP_CTRL_DATA_WIDTH     32
#define HIVE_ISP_CTRL_ADDRESS_WIDTH  32
#define HIVE_ISP_CTRL_MAX_BURST_SIZE  1
#define HIVE_ISP_DDR_ADDRESS_WIDTH   36

#define HIVE_ISP_HOST_MAX_BURST_SIZE  8 /* host supports bursts in order to prevent repeating DDRAM accesses */
#define HIVE_ISP_NUM_GPIO_PINS       12

/* This list of vector num_elems/elem_bits pairs is valid both in C as initializer
   and in the DMA parameter list */
#define HIVE_ISP_DDR_DMA_SPECS {{32,  8}, {16, 16}, {18, 14}, {25, 10}, {21, 12}}
#define HIVE_ISP_DDR_WORD_BITS 256
#define HIVE_ISP_DDR_WORD_BYTES  (HIVE_ISP_DDR_WORD_BITS / 8)
#define HIVE_ISP_DDR_BYTES       (512 * 1024 * 1024) /* hss only */
#define HIVE_ISP_DDR_BYTES_RTL   (127 * 1024 * 1024) /* RTL only */
#define HIVE_ISP_DDR_SMALL_BYTES (128 * 256 / 8)
#define HIVE_ISP_PAGE_SHIFT    12
#define HIVE_ISP_PAGE_SIZE     BIT(HIVE_ISP_PAGE_SHIFT)

#define CSS_DDR_WORD_BITS        HIVE_ISP_DDR_WORD_BITS
#define CSS_DDR_WORD_BYTES       HIVE_ISP_DDR_WORD_BYTES

/* If HIVE_ISP_DDR_BASE_OFFSET is set to a non-zero value, the wide bus just before the DDRAM gets an extra dummy port where         */
/* address range 0 .. HIVE_ISP_DDR_BASE_OFFSET-1 maps onto. This effectively creates an offset for the DDRAM from system perspective */
#define HIVE_ISP_DDR_BASE_OFFSET 0x120000000 /* 0x200000 */

#define HIVE_DMA_ISP_BUS_CONN 0
#define HIVE_DMA_ISP_DDR_CONN 1
#define HIVE_DMA_BUS_DDR_CONN 2
#define HIVE_DMA_ISP_MASTER master_port0
#define HIVE_DMA_BUS_MASTER master_port1
#define HIVE_DMA_DDR_MASTER master_port2

#define HIVE_DMA_NUM_CHANNELS       32 /* old value was  8 */
#define HIVE_DMA_CMD_FIFO_DEPTH     24 /* old value was 12 */

#define HIVE_IF_PIXEL_WIDTH 12

#define HIVE_MMU_TLB_SETS           8
#define HIVE_MMU_TLB_SET_BLOCKS     8
#define HIVE_MMU_TLB_BLOCK_ELEMENTS 8
#define HIVE_MMU_PAGE_TABLE_LEVELS  2
#define HIVE_MMU_PAGE_BYTES         HIVE_ISP_PAGE_SIZE

#define HIVE_ISP_CH_ID_BITS    2
#define HIVE_ISP_FMT_TYPE_BITS 5
#define HIVE_ISP_ISEL_SEL_BITS 2

#define HIVE_GP_REGS_SDRAM_WAKEUP_IDX                           0
#define HIVE_GP_REGS_IDLE_IDX                                   1
#define HIVE_GP_REGS_IRQ_0_IDX                                  2
#define HIVE_GP_REGS_IRQ_1_IDX                                  3
#define HIVE_GP_REGS_SP_STREAM_STAT_IDX                         4
#define HIVE_GP_REGS_SP_STREAM_STAT_B_IDX                       5
#define HIVE_GP_REGS_ISP_STREAM_STAT_IDX                        6
#define HIVE_GP_REGS_MOD_STREAM_STAT_IDX                        7
#define HIVE_GP_REGS_SP_STREAM_STAT_IRQ_COND_IDX                8
#define HIVE_GP_REGS_SP_STREAM_STAT_B_IRQ_COND_IDX              9
#define HIVE_GP_REGS_ISP_STREAM_STAT_IRQ_COND_IDX              10
#define HIVE_GP_REGS_MOD_STREAM_STAT_IRQ_COND_IDX              11
#define HIVE_GP_REGS_SP_STREAM_STAT_IRQ_ENABLE_IDX             12
#define HIVE_GP_REGS_SP_STREAM_STAT_B_IRQ_ENABLE_IDX           13
#define HIVE_GP_REGS_ISP_STREAM_STAT_IRQ_ENABLE_IDX            14
#define HIVE_GP_REGS_MOD_STREAM_STAT_IRQ_ENABLE_IDX            15
#define HIVE_GP_REGS_SWITCH_PRIM_IF_IDX                        16
#define HIVE_GP_REGS_SWITCH_GDC1_IDX                           17
#define HIVE_GP_REGS_SWITCH_GDC2_IDX                           18
#define HIVE_GP_REGS_SRST_IDX                                  19
#define HIVE_GP_REGS_SLV_REG_SRST_IDX                          20

/* Bit numbers of the soft reset register */
#define HIVE_GP_REGS_SRST_ISYS_CBUS                             0
#define HIVE_GP_REGS_SRST_ISEL_CBUS                             1
#define HIVE_GP_REGS_SRST_IFMT_CBUS                             2
#define HIVE_GP_REGS_SRST_GPDEV_CBUS                            3
#define HIVE_GP_REGS_SRST_GPIO                                  4
#define HIVE_GP_REGS_SRST_TC                                    5
#define HIVE_GP_REGS_SRST_GPTIMER                               6
#define HIVE_GP_REGS_SRST_FACELLFIFOS                           7
#define HIVE_GP_REGS_SRST_D_OSYS                                8
#define HIVE_GP_REGS_SRST_IFT_SEC_PIPE                          9
#define HIVE_GP_REGS_SRST_GDC1                                 10
#define HIVE_GP_REGS_SRST_GDC2                                 11
#define HIVE_GP_REGS_SRST_VEC_BUS                              12
#define HIVE_GP_REGS_SRST_ISP                                  13
#define HIVE_GP_REGS_SRST_SLV_GRP_BUS                          14
#define HIVE_GP_REGS_SRST_DMA                                  15
#define HIVE_GP_REGS_SRST_SF_ISP_SP                            16
#define HIVE_GP_REGS_SRST_SF_PIF_CELLS                         17
#define HIVE_GP_REGS_SRST_SF_SIF_SP                            18
#define HIVE_GP_REGS_SRST_SF_MC_SP                             19
#define HIVE_GP_REGS_SRST_SF_ISYS_SP                           20
#define HIVE_GP_REGS_SRST_SF_DMA_CELLS                         21
#define HIVE_GP_REGS_SRST_SF_GDC1_CELLS                        22
#define HIVE_GP_REGS_SRST_SF_GDC2_CELLS                        23
#define HIVE_GP_REGS_SRST_SP                                   24
#define HIVE_GP_REGS_SRST_OCP2CIO                              25
#define HIVE_GP_REGS_SRST_NBUS                                 26
#define HIVE_GP_REGS_SRST_HOST12BUS                            27
#define HIVE_GP_REGS_SRST_WBUS                                 28
#define HIVE_GP_REGS_SRST_IC_OSYS                              29
#define HIVE_GP_REGS_SRST_WBUS_IC                              30

/* Bit numbers of the slave register soft reset register */
#define HIVE_GP_REGS_SLV_REG_SRST_DMA                           0
#define HIVE_GP_REGS_SLV_REG_SRST_GDC1                          1
#define HIVE_GP_REGS_SLV_REG_SRST_GDC2                          2

/* order of the input bits for the irq controller */
#define HIVE_GP_DEV_IRQ_GPIO_PIN_0_BIT_ID                       0
#define HIVE_GP_DEV_IRQ_GPIO_PIN_1_BIT_ID                       1
#define HIVE_GP_DEV_IRQ_GPIO_PIN_2_BIT_ID                       2
#define HIVE_GP_DEV_IRQ_GPIO_PIN_3_BIT_ID                       3
#define HIVE_GP_DEV_IRQ_GPIO_PIN_4_BIT_ID                       4
#define HIVE_GP_DEV_IRQ_GPIO_PIN_5_BIT_ID                       5
#define HIVE_GP_DEV_IRQ_GPIO_PIN_6_BIT_ID                       6
#define HIVE_GP_DEV_IRQ_GPIO_PIN_7_BIT_ID                       7
#define HIVE_GP_DEV_IRQ_GPIO_PIN_8_BIT_ID                       8
#define HIVE_GP_DEV_IRQ_GPIO_PIN_9_BIT_ID                       9
#define HIVE_GP_DEV_IRQ_GPIO_PIN_10_BIT_ID                     10
#define HIVE_GP_DEV_IRQ_GPIO_PIN_11_BIT_ID                     11
#define HIVE_GP_DEV_IRQ_SP_BIT_ID                              12
#define HIVE_GP_DEV_IRQ_ISP_BIT_ID                             13
#define HIVE_GP_DEV_IRQ_ISYS_BIT_ID                            14
#define HIVE_GP_DEV_IRQ_ISEL_BIT_ID                            15
#define HIVE_GP_DEV_IRQ_IFMT_BIT_ID                            16
#define HIVE_GP_DEV_IRQ_SP_STREAM_MON_BIT_ID                   17
#define HIVE_GP_DEV_IRQ_ISP_STREAM_MON_BIT_ID                  18
#define HIVE_GP_DEV_IRQ_MOD_STREAM_MON_BIT_ID                  19
#define HIVE_GP_DEV_IRQ_ISP_PMEM_ERROR_BIT_ID                  20
#define HIVE_GP_DEV_IRQ_ISP_BAMEM_ERROR_BIT_ID                 21
#define HIVE_GP_DEV_IRQ_ISP_DMEM_ERROR_BIT_ID                  22
#define HIVE_GP_DEV_IRQ_SP_ICACHE_MEM_ERROR_BIT_ID             23
#define HIVE_GP_DEV_IRQ_SP_DMEM_ERROR_BIT_ID                   24
#define HIVE_GP_DEV_IRQ_MMU_CACHE_MEM_ERROR_BIT_ID             25
#define HIVE_GP_DEV_IRQ_GP_TIMER_0_BIT_ID                      26
#define HIVE_GP_DEV_IRQ_GP_TIMER_1_BIT_ID                      27
#define HIVE_GP_DEV_IRQ_SW_PIN_0_BIT_ID                        28
#define HIVE_GP_DEV_IRQ_SW_PIN_1_BIT_ID                        29
#define HIVE_GP_DEV_IRQ_DMA_BIT_ID                             30
#define HIVE_GP_DEV_IRQ_SP_STREAM_MON_B_BIT_ID                 31

#define HIVE_GP_REGS_NUM_SW_IRQ_REGS                            2

/* order of the input bits for the timed controller */
#define HIVE_GP_DEV_TC_GPIO_PIN_0_BIT_ID                       0
#define HIVE_GP_DEV_TC_GPIO_PIN_1_BIT_ID                       1
#define HIVE_GP_DEV_TC_GPIO_PIN_2_BIT_ID                       2
#define HIVE_GP_DEV_TC_GPIO_PIN_3_BIT_ID                       3
#define HIVE_GP_DEV_TC_GPIO_PIN_4_BIT_ID                       4
#define HIVE_GP_DEV_TC_GPIO_PIN_5_BIT_ID                       5
#define HIVE_GP_DEV_TC_GPIO_PIN_6_BIT_ID                       6
#define HIVE_GP_DEV_TC_GPIO_PIN_7_BIT_ID                       7
#define HIVE_GP_DEV_TC_GPIO_PIN_8_BIT_ID                       8
#define HIVE_GP_DEV_TC_GPIO_PIN_9_BIT_ID                       9
#define HIVE_GP_DEV_TC_GPIO_PIN_10_BIT_ID                     10
#define HIVE_GP_DEV_TC_GPIO_PIN_11_BIT_ID                     11
#define HIVE_GP_DEV_TC_SP_BIT_ID                              12
#define HIVE_GP_DEV_TC_ISP_BIT_ID                             13
#define HIVE_GP_DEV_TC_ISYS_BIT_ID                            14
#define HIVE_GP_DEV_TC_ISEL_BIT_ID                            15
#define HIVE_GP_DEV_TC_IFMT_BIT_ID                            16
#define HIVE_GP_DEV_TC_GP_TIMER_0_BIT_ID                      17
#define HIVE_GP_DEV_TC_GP_TIMER_1_BIT_ID                      18
#define HIVE_GP_DEV_TC_MIPI_SOL_BIT_ID                        19
#define HIVE_GP_DEV_TC_MIPI_EOL_BIT_ID                        20
#define HIVE_GP_DEV_TC_MIPI_SOF_BIT_ID                        21
#define HIVE_GP_DEV_TC_MIPI_EOF_BIT_ID                        22
#define HIVE_GP_DEV_TC_INPSYS_SM                              23

/* definitions for the gp_timer block */
#define HIVE_GP_TIMER_0                                         0
#define HIVE_GP_TIMER_1                                         1
#define HIVE_GP_TIMER_2                                         2
#define HIVE_GP_TIMER_3                                         3
#define HIVE_GP_TIMER_4                                         4
#define HIVE_GP_TIMER_5                                         5
#define HIVE_GP_TIMER_6                                         6
#define HIVE_GP_TIMER_7                                         7
#define HIVE_GP_TIMER_NUM_COUNTERS                              8

#define HIVE_GP_TIMER_IRQ_0                                     0
#define HIVE_GP_TIMER_IRQ_1                                     1
#define HIVE_GP_TIMER_NUM_IRQS                                  2

#define HIVE_GP_TIMER_GPIO_0_BIT_ID                             0
#define HIVE_GP_TIMER_GPIO_1_BIT_ID                             1
#define HIVE_GP_TIMER_GPIO_2_BIT_ID                             2
#define HIVE_GP_TIMER_GPIO_3_BIT_ID                             3
#define HIVE_GP_TIMER_GPIO_4_BIT_ID                             4
#define HIVE_GP_TIMER_GPIO_5_BIT_ID                             5
#define HIVE_GP_TIMER_GPIO_6_BIT_ID                             6
#define HIVE_GP_TIMER_GPIO_7_BIT_ID                             7
#define HIVE_GP_TIMER_GPIO_8_BIT_ID                             8
#define HIVE_GP_TIMER_GPIO_9_BIT_ID                             9
#define HIVE_GP_TIMER_GPIO_10_BIT_ID                           10
#define HIVE_GP_TIMER_GPIO_11_BIT_ID                           11
#define HIVE_GP_TIMER_INP_SYS_IRQ                              12
#define HIVE_GP_TIMER_ISEL_IRQ                                 13
#define HIVE_GP_TIMER_IFMT_IRQ                                 14
#define HIVE_GP_TIMER_SP_STRMON_IRQ                            15
#define HIVE_GP_TIMER_SP_B_STRMON_IRQ                          16
#define HIVE_GP_TIMER_ISP_STRMON_IRQ                           17
#define HIVE_GP_TIMER_MOD_STRMON_IRQ                           18
#define HIVE_GP_TIMER_ISP_BAMEM_ERROR_IRQ                      20
#define HIVE_GP_TIMER_ISP_DMEM_ERROR_IRQ                       21
#define HIVE_GP_TIMER_SP_ICACHE_MEM_ERROR_IRQ                  22
#define HIVE_GP_TIMER_SP_DMEM_ERROR_IRQ                        23
#define HIVE_GP_TIMER_SP_OUT_RUN_DP                            24
#define HIVE_GP_TIMER_SP_WIRE_DEBUG_LM_MSINK_RUN_I0_I0         25
#define HIVE_GP_TIMER_SP_WIRE_DEBUG_LM_MSINK_RUN_I0_I1         26
#define HIVE_GP_TIMER_SP_WIRE_DEBUG_LM_MSINK_RUN_I0_I2         27
#define HIVE_GP_TIMER_SP_WIRE_DEBUG_LM_MSINK_RUN_I0_I3         28
#define HIVE_GP_TIMER_SP_WIRE_DEBUG_LM_MSINK_RUN_I0_I4         29
#define HIVE_GP_TIMER_SP_WIRE_DEBUG_LM_MSINK_RUN_I0_I5         30
#define HIVE_GP_TIMER_SP_WIRE_DEBUG_LM_MSINK_RUN_I0_I6         31
#define HIVE_GP_TIMER_SP_WIRE_DEBUG_LM_MSINK_RUN_I0_I7         32
#define HIVE_GP_TIMER_SP_WIRE_DEBUG_LM_MSINK_RUN_I0_I8         33
#define HIVE_GP_TIMER_SP_WIRE_DEBUG_LM_MSINK_RUN_I0_I9         34
#define HIVE_GP_TIMER_SP_WIRE_DEBUG_LM_MSINK_RUN_I0_I10        35
#define HIVE_GP_TIMER_SP_WIRE_DEBUG_LM_MSINK_RUN_I1_I0         36
#define HIVE_GP_TIMER_SP_WIRE_DEBUG_LM_MSINK_RUN_I2_I0         37
#define HIVE_GP_TIMER_SP_WIRE_DEBUG_LM_MSINK_RUN_I3_I0         38
#define HIVE_GP_TIMER_ISP_OUT_RUN_DP                           39
#define HIVE_GP_TIMER_ISP_WIRE_DEBUG_LM_MSINK_RUN_I0_I0        40
#define HIVE_GP_TIMER_ISP_WIRE_DEBUG_LM_MSINK_RUN_I0_I1        41
#define HIVE_GP_TIMER_ISP_WIRE_DEBUG_LM_MSINK_RUN_I1_I0        42
#define HIVE_GP_TIMER_ISP_WIRE_DEBUG_LM_MSINK_RUN_I2_I0        43
#define HIVE_GP_TIMER_ISP_WIRE_DEBUG_LM_MSINK_RUN_I2_I1        44
#define HIVE_GP_TIMER_ISP_WIRE_DEBUG_LM_MSINK_RUN_I2_I2        45
#define HIVE_GP_TIMER_ISP_WIRE_DEBUG_LM_MSINK_RUN_I2_I3        46
#define HIVE_GP_TIMER_ISP_WIRE_DEBUG_LM_MSINK_RUN_I2_I4        47
#define HIVE_GP_TIMER_ISP_WIRE_DEBUG_LM_MSINK_RUN_I2_I5        48
#define HIVE_GP_TIMER_ISP_WIRE_DEBUG_LM_MSINK_RUN_I2_I6        49
#define HIVE_GP_TIMER_ISP_WIRE_DEBUG_LM_MSINK_RUN_I3_I0        50
#define HIVE_GP_TIMER_ISP_WIRE_DEBUG_LM_MSINK_RUN_I4_I0        51
#define HIVE_GP_TIMER_ISP_WIRE_DEBUG_LM_MSINK_RUN_I5_I0        52
#define HIVE_GP_TIMER_ISP_WIRE_DEBUG_LM_MSINK_RUN_I6_I0        53
#define HIVE_GP_TIMER_ISP_WIRE_DEBUG_LM_MSINK_RUN_I7_I0        54
#define HIVE_GP_TIMER_MIPI_SOL_BIT_ID                          55
#define HIVE_GP_TIMER_MIPI_EOL_BIT_ID                          56
#define HIVE_GP_TIMER_MIPI_SOF_BIT_ID                          57
#define HIVE_GP_TIMER_MIPI_EOF_BIT_ID                          58
#define HIVE_GP_TIMER_INPSYS_SM                                59

/* port definitions for the streaming monitors */
/* port definititions SP streaming monitor, monitors the status of streaming ports at the SP side of the streaming FIFO's */
#define SP_STR_MON_PORT_SP2SIF            0
#define SP_STR_MON_PORT_SIF2SP            1
#define SP_STR_MON_PORT_SP2MC             2
#define SP_STR_MON_PORT_MC2SP             3
#define SP_STR_MON_PORT_SP2DMA            4
#define SP_STR_MON_PORT_DMA2SP            5
#define SP_STR_MON_PORT_SP2ISP            6
#define SP_STR_MON_PORT_ISP2SP            7
#define SP_STR_MON_PORT_SP2GPD            8
#define SP_STR_MON_PORT_FA2SP             9
#define SP_STR_MON_PORT_SP2ISYS          10
#define SP_STR_MON_PORT_ISYS2SP          11
#define SP_STR_MON_PORT_SP2PIFA          12
#define SP_STR_MON_PORT_PIFA2SP          13
#define SP_STR_MON_PORT_SP2PIFB          14
#define SP_STR_MON_PORT_PIFB2SP          15

#define SP_STR_MON_PORT_B_SP2GDC1         0
#define SP_STR_MON_PORT_B_GDC12SP         1
#define SP_STR_MON_PORT_B_SP2GDC2         2
#define SP_STR_MON_PORT_B_GDC22SP         3

/* previously used SP streaming monitor port identifiers, kept for backward compatibility */
#define SP_STR_MON_PORT_SND_SIF           SP_STR_MON_PORT_SP2SIF
#define SP_STR_MON_PORT_RCV_SIF           SP_STR_MON_PORT_SIF2SP
#define SP_STR_MON_PORT_SND_MC            SP_STR_MON_PORT_SP2MC
#define SP_STR_MON_PORT_RCV_MC            SP_STR_MON_PORT_MC2SP
#define SP_STR_MON_PORT_SND_DMA           SP_STR_MON_PORT_SP2DMA
#define SP_STR_MON_PORT_RCV_DMA           SP_STR_MON_PORT_DMA2SP
#define SP_STR_MON_PORT_SND_ISP           SP_STR_MON_PORT_SP2ISP
#define SP_STR_MON_PORT_RCV_ISP           SP_STR_MON_PORT_ISP2SP
#define SP_STR_MON_PORT_SND_GPD           SP_STR_MON_PORT_SP2GPD
#define SP_STR_MON_PORT_RCV_GPD           SP_STR_MON_PORT_FA2SP
/* Deprecated */
#define SP_STR_MON_PORT_SND_PIF           SP_STR_MON_PORT_SP2PIFA
#define SP_STR_MON_PORT_RCV_PIF           SP_STR_MON_PORT_PIFA2SP
#define SP_STR_MON_PORT_SND_PIFB          SP_STR_MON_PORT_SP2PIFB
#define SP_STR_MON_PORT_RCV_PIFB          SP_STR_MON_PORT_PIFB2SP

#define SP_STR_MON_PORT_SND_PIF_A         SP_STR_MON_PORT_SP2PIFA
#define SP_STR_MON_PORT_RCV_PIF_A         SP_STR_MON_PORT_PIFA2SP
#define SP_STR_MON_PORT_SND_PIF_B         SP_STR_MON_PORT_SP2PIFB
#define SP_STR_MON_PORT_RCV_PIF_B         SP_STR_MON_PORT_PIFB2SP

/* port definititions ISP streaming monitor, monitors the status of streaming ports at the ISP side of the streaming FIFO's */
#define ISP_STR_MON_PORT_ISP2PIFA         0
#define ISP_STR_MON_PORT_PIFA2ISP         1
#define ISP_STR_MON_PORT_ISP2PIFB         2
#define ISP_STR_MON_PORT_PIFB2ISP         3
#define ISP_STR_MON_PORT_ISP2DMA          4
#define ISP_STR_MON_PORT_DMA2ISP          5
#define ISP_STR_MON_PORT_ISP2GDC1         6
#define ISP_STR_MON_PORT_GDC12ISP         7
#define ISP_STR_MON_PORT_ISP2GDC2         8
#define ISP_STR_MON_PORT_GDC22ISP         9
#define ISP_STR_MON_PORT_ISP2GPD         10
#define ISP_STR_MON_PORT_FA2ISP          11
#define ISP_STR_MON_PORT_ISP2SP          12
#define ISP_STR_MON_PORT_SP2ISP          13

/* previously used ISP streaming monitor port identifiers, kept for backward compatibility */
#define ISP_STR_MON_PORT_SND_PIF_A       ISP_STR_MON_PORT_ISP2PIFA
#define ISP_STR_MON_PORT_RCV_PIF_A       ISP_STR_MON_PORT_PIFA2ISP
#define ISP_STR_MON_PORT_SND_PIF_B       ISP_STR_MON_PORT_ISP2PIFB
#define ISP_STR_MON_PORT_RCV_PIF_B       ISP_STR_MON_PORT_PIFB2ISP
#define ISP_STR_MON_PORT_SND_DMA         ISP_STR_MON_PORT_ISP2DMA
#define ISP_STR_MON_PORT_RCV_DMA         ISP_STR_MON_PORT_DMA2ISP
#define ISP_STR_MON_PORT_SND_GDC         ISP_STR_MON_PORT_ISP2GDC1
#define ISP_STR_MON_PORT_RCV_GDC         ISP_STR_MON_PORT_GDC12ISP
#define ISP_STR_MON_PORT_SND_GPD         ISP_STR_MON_PORT_ISP2GPD
#define ISP_STR_MON_PORT_RCV_GPD         ISP_STR_MON_PORT_FA2ISP
#define ISP_STR_MON_PORT_SND_SP          ISP_STR_MON_PORT_ISP2SP
#define ISP_STR_MON_PORT_RCV_SP          ISP_STR_MON_PORT_SP2ISP

/* port definititions MOD streaming monitor, monitors the status of streaming ports at the module side of the streaming FIFO's */

#define MOD_STR_MON_PORT_PIFA2CELLS       0
#define MOD_STR_MON_PORT_CELLS2PIFA       1
#define MOD_STR_MON_PORT_PIFB2CELLS       2
#define MOD_STR_MON_PORT_CELLS2PIFB       3
#define MOD_STR_MON_PORT_SIF2SP           4
#define MOD_STR_MON_PORT_SP2SIF           5
#define MOD_STR_MON_PORT_MC2SP            6
#define MOD_STR_MON_PORT_SP2MC            7
#define MOD_STR_MON_PORT_DMA2ISP          8
#define MOD_STR_MON_PORT_ISP2DMA          9
#define MOD_STR_MON_PORT_DMA2SP          10
#define MOD_STR_MON_PORT_SP2DMA          11
#define MOD_STR_MON_PORT_GDC12CELLS      12
#define MOD_STR_MON_PORT_CELLS2GDC1      13
#define MOD_STR_MON_PORT_GDC22CELLS      14
#define MOD_STR_MON_PORT_CELLS2GDC2      15

#define MOD_STR_MON_PORT_SND_PIF_A        0
#define MOD_STR_MON_PORT_RCV_PIF_A        1
#define MOD_STR_MON_PORT_SND_PIF_B        2
#define MOD_STR_MON_PORT_RCV_PIF_B        3
#define MOD_STR_MON_PORT_SND_SIF          4
#define MOD_STR_MON_PORT_RCV_SIF          5
#define MOD_STR_MON_PORT_SND_MC           6
#define MOD_STR_MON_PORT_RCV_MC           7
#define MOD_STR_MON_PORT_SND_DMA2ISP      8
#define MOD_STR_MON_PORT_RCV_DMA_FR_ISP   9
#define MOD_STR_MON_PORT_SND_DMA2SP      10
#define MOD_STR_MON_PORT_RCV_DMA_FR_SP   11
#define MOD_STR_MON_PORT_SND_GDC         12
#define MOD_STR_MON_PORT_RCV_GDC         13

/* testbench signals:       */

/* testbench GP adapter register ids  */
#define HIVE_TESTBENCH_GPIO_DATA_OUT_REG_IDX                    0
#define HIVE_TESTBENCH_GPIO_DIR_OUT_REG_IDX                     1
#define HIVE_TESTBENCH_IRQ_REG_IDX                              2
#define HIVE_TESTBENCH_SDRAM_WAKEUP_REG_IDX                     3
#define HIVE_TESTBENCH_IDLE_REG_IDX                             4
#define HIVE_TESTBENCH_GPIO_DATA_IN_REG_IDX                     5
#define HIVE_TESTBENCH_MIPI_BFM_EN_REG_IDX                      6
#define HIVE_TESTBENCH_CSI_CONFIG_REG_IDX                       7
#define HIVE_TESTBENCH_DDR_STALL_EN_REG_IDX                     8

#define HIVE_TESTBENCH_ISP_PMEM_ERROR_IRQ_REG_IDX               9
#define HIVE_TESTBENCH_ISP_BAMEM_ERROR_IRQ_REG_IDX             10
#define HIVE_TESTBENCH_ISP_DMEM_ERROR_IRQ_REG_IDX              11
#define HIVE_TESTBENCH_SP_ICACHE_MEM_ERROR_IRQ_REG_IDX         12
#define HIVE_TESTBENCH_SP_DMEM_ERROR_IRQ_REG_IDX               13

/* Signal monitor input bit ids */
#define HIVE_TESTBENCH_SIG_MON_GPIO_PIN_O_BIT_ID                0
#define HIVE_TESTBENCH_SIG_MON_GPIO_PIN_1_BIT_ID                1
#define HIVE_TESTBENCH_SIG_MON_GPIO_PIN_2_BIT_ID                2
#define HIVE_TESTBENCH_SIG_MON_GPIO_PIN_3_BIT_ID                3
#define HIVE_TESTBENCH_SIG_MON_GPIO_PIN_4_BIT_ID                4
#define HIVE_TESTBENCH_SIG_MON_GPIO_PIN_5_BIT_ID                5
#define HIVE_TESTBENCH_SIG_MON_GPIO_PIN_6_BIT_ID                6
#define HIVE_TESTBENCH_SIG_MON_GPIO_PIN_7_BIT_ID                7
#define HIVE_TESTBENCH_SIG_MON_GPIO_PIN_8_BIT_ID                8
#define HIVE_TESTBENCH_SIG_MON_GPIO_PIN_9_BIT_ID                9
#define HIVE_TESTBENCH_SIG_MON_GPIO_PIN_10_BIT_ID              10
#define HIVE_TESTBENCH_SIG_MON_GPIO_PIN_11_BIT_ID              11
#define HIVE_TESTBENCH_SIG_MON_IRQ_PIN_BIT_ID                  12
#define HIVE_TESTBENCH_SIG_MON_SDRAM_WAKEUP_PIN_BIT_ID         13
#define HIVE_TESTBENCH_SIG_MON_IDLE_PIN_BIT_ID                 14

#define ISP2400_DEBUG_NETWORK    1

#endif /* _hive_isp_css_defs_h__ */
