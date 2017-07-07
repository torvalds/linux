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

#ifndef _input_system_defs_h
#define _input_system_defs_h

/* csi controller modes */
#define HIVE_CSI_CONFIG_MAIN                   0
#define HIVE_CSI_CONFIG_STEREO1                4
#define HIVE_CSI_CONFIG_STEREO2                8

/* general purpose register IDs */

/* Stream Multicast select modes */
#define HIVE_ISYS_GPREG_MULTICAST_A_IDX           0
#define HIVE_ISYS_GPREG_MULTICAST_B_IDX           1
#define HIVE_ISYS_GPREG_MULTICAST_C_IDX           2

/* Stream Mux select modes */
#define HIVE_ISYS_GPREG_MUX_IDX                   3

/* streaming monitor status and control */
#define HIVE_ISYS_GPREG_STRMON_STAT_IDX           4
#define HIVE_ISYS_GPREG_STRMON_COND_IDX           5
#define HIVE_ISYS_GPREG_STRMON_IRQ_EN_IDX         6
#define HIVE_ISYS_GPREG_SRST_IDX                  7
#define HIVE_ISYS_GPREG_SLV_REG_SRST_IDX          8
#define HIVE_ISYS_GPREG_REG_PORT_A_IDX            9
#define HIVE_ISYS_GPREG_REG_PORT_B_IDX            10

/* Bit numbers of the soft reset register */
#define HIVE_ISYS_GPREG_SRST_CAPT_FIFO_A_BIT      0
#define HIVE_ISYS_GPREG_SRST_CAPT_FIFO_B_BIT      1
#define HIVE_ISYS_GPREG_SRST_CAPT_FIFO_C_BIT      2
#define HIVE_ISYS_GPREG_SRST_MULTICAST_A_BIT      3
#define HIVE_ISYS_GPREG_SRST_MULTICAST_B_BIT      4
#define HIVE_ISYS_GPREG_SRST_MULTICAST_C_BIT      5
#define HIVE_ISYS_GPREG_SRST_CAPT_A_BIT           6
#define HIVE_ISYS_GPREG_SRST_CAPT_B_BIT           7
#define HIVE_ISYS_GPREG_SRST_CAPT_C_BIT           8
#define HIVE_ISYS_GPREG_SRST_ACQ_BIT              9
/* For ISYS_CTRL 5bits are defined to allow soft-reset per sub-controller and top-ctrl */
#define HIVE_ISYS_GPREG_SRST_ISYS_CTRL_BIT        10  /*LSB for 5bit vector */
#define HIVE_ISYS_GPREG_SRST_ISYS_CTRL_CAPT_A_BIT 10
#define HIVE_ISYS_GPREG_SRST_ISYS_CTRL_CAPT_B_BIT 11
#define HIVE_ISYS_GPREG_SRST_ISYS_CTRL_CAPT_C_BIT 12
#define HIVE_ISYS_GPREG_SRST_ISYS_CTRL_ACQ_BIT    13
#define HIVE_ISYS_GPREG_SRST_ISYS_CTRL_TOP_BIT    14
/* -- */
#define HIVE_ISYS_GPREG_SRST_STR_MUX_BIT          15
#define HIVE_ISYS_GPREG_SRST_CIO2AHB_BIT          16
#define HIVE_ISYS_GPREG_SRST_GEN_SHORT_FIFO_BIT   17
#define HIVE_ISYS_GPREG_SRST_WIDE_BUS_BIT         18 // includes CIO conv
#define HIVE_ISYS_GPREG_SRST_DMA_BIT              19
#define HIVE_ISYS_GPREG_SRST_SF_CTRL_CAPT_A_BIT   20
#define HIVE_ISYS_GPREG_SRST_SF_CTRL_CAPT_B_BIT   21
#define HIVE_ISYS_GPREG_SRST_SF_CTRL_CAPT_C_BIT   22
#define HIVE_ISYS_GPREG_SRST_SF_CTRL_ACQ_BIT      23
#define HIVE_ISYS_GPREG_SRST_CSI_BE_OUT_BIT       24

#define HIVE_ISYS_GPREG_SLV_REG_SRST_CAPT_A_BIT    0
#define HIVE_ISYS_GPREG_SLV_REG_SRST_CAPT_B_BIT    1
#define HIVE_ISYS_GPREG_SLV_REG_SRST_CAPT_C_BIT    2
#define HIVE_ISYS_GPREG_SLV_REG_SRST_ACQ_BIT       3
#define HIVE_ISYS_GPREG_SLV_REG_SRST_DMA_BIT        4
#define HIVE_ISYS_GPREG_SLV_REG_SRST_ISYS_CTRL_BIT  5

/* streaming monitor port id's */
#define HIVE_ISYS_STR_MON_PORT_CAPA            0
#define HIVE_ISYS_STR_MON_PORT_CAPB            1
#define HIVE_ISYS_STR_MON_PORT_CAPC            2
#define HIVE_ISYS_STR_MON_PORT_ACQ             3
#define HIVE_ISYS_STR_MON_PORT_CSS_GENSH       4
#define HIVE_ISYS_STR_MON_PORT_SF_GENSH        5
#define HIVE_ISYS_STR_MON_PORT_SP2ISYS         6
#define HIVE_ISYS_STR_MON_PORT_ISYS2SP         7
#define HIVE_ISYS_STR_MON_PORT_PIXA            8
#define HIVE_ISYS_STR_MON_PORT_PIXB            9

/* interrupt bit ID's        */
#define HIVE_ISYS_IRQ_CSI_SOF_BIT_ID           0
#define HIVE_ISYS_IRQ_CSI_EOF_BIT_ID           1
#define HIVE_ISYS_IRQ_CSI_SOL_BIT_ID           2
#define HIVE_ISYS_IRQ_CSI_EOL_BIT_ID           3
#define HIVE_ISYS_IRQ_CSI_RECEIVER_BIT_ID      4
#define HIVE_ISYS_IRQ_CSI_RECEIVER_BE_BIT_ID   5
#define HIVE_ISYS_IRQ_CAP_UNIT_A_NO_SOP        6
#define HIVE_ISYS_IRQ_CAP_UNIT_A_LATE_SOP      7
/*#define HIVE_ISYS_IRQ_CAP_UNIT_A_UNDEF_PH      7*/
#define HIVE_ISYS_IRQ_CAP_UNIT_B_NO_SOP        8
#define HIVE_ISYS_IRQ_CAP_UNIT_B_LATE_SOP      9
/*#define HIVE_ISYS_IRQ_CAP_UNIT_B_UNDEF_PH     10*/
#define HIVE_ISYS_IRQ_CAP_UNIT_C_NO_SOP       10
#define HIVE_ISYS_IRQ_CAP_UNIT_C_LATE_SOP     11
/*#define HIVE_ISYS_IRQ_CAP_UNIT_C_UNDEF_PH     13*/
#define HIVE_ISYS_IRQ_ACQ_UNIT_SOP_MISMATCH   12
/*#define HIVE_ISYS_IRQ_ACQ_UNIT_UNDEF_PH       15*/
#define HIVE_ISYS_IRQ_INP_CTRL_CAPA           13
#define HIVE_ISYS_IRQ_INP_CTRL_CAPB           14
#define HIVE_ISYS_IRQ_INP_CTRL_CAPC           15
#define HIVE_ISYS_IRQ_CIO2AHB                 16
#define HIVE_ISYS_IRQ_DMA_BIT_ID              17
#define HIVE_ISYS_IRQ_STREAM_MON_BIT_ID       18
#define HIVE_ISYS_IRQ_NUM_BITS                19

/* DMA */
#define HIVE_ISYS_DMA_CHANNEL                  0
#define HIVE_ISYS_DMA_IBUF_DDR_CONN            0
#define HIVE_ISYS_DMA_HEIGHT                   1
#define HIVE_ISYS_DMA_ELEMS                    1 /* both master buses of same width */
#define HIVE_ISYS_DMA_STRIDE                   0 /* no stride required as height is fixed to 1 */
#define HIVE_ISYS_DMA_CROP                     0 /* no cropping */
#define HIVE_ISYS_DMA_EXTENSION                0 /* no extension as elem width is same on both side */

#endif /* _input_system_defs_h */
