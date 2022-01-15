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

#ifndef _isp_acquisition_defs_h
#define _isp_acquisition_defs_h

#define _ISP_ACQUISITION_REG_ALIGN                4  /* assuming 32 bit control bus width */
#define _ISP_ACQUISITION_BYTES_PER_ELEM           4

/* --------------------------------------------------*/

#define NOF_ACQ_IRQS                              1

/* --------------------------------------------------*/
/* FSM */
/* --------------------------------------------------*/
#define MEM2STREAM_FSM_STATE_BITS                 2
#define ACQ_SYNCHRONIZER_FSM_STATE_BITS           2

/* --------------------------------------------------*/
/* REGISTER INFO */
/* --------------------------------------------------*/

#define NOF_ACQ_REGS                              12

// Register id's of MMIO slave accessible registers
#define ACQ_START_ADDR_REG_ID                     0
#define ACQ_MEM_REGION_SIZE_REG_ID                1
#define ACQ_NUM_MEM_REGIONS_REG_ID                2
#define ACQ_INIT_REG_ID                           3
#define ACQ_RECEIVED_SHORT_PACKETS_REG_ID         4
#define ACQ_RECEIVED_LONG_PACKETS_REG_ID          5
#define ACQ_LAST_COMMAND_REG_ID                   6
#define ACQ_NEXT_COMMAND_REG_ID                   7
#define ACQ_LAST_ACKNOWLEDGE_REG_ID               8
#define ACQ_NEXT_ACKNOWLEDGE_REG_ID               9
#define ACQ_FSM_STATE_INFO_REG_ID                 10
#define ACQ_INT_CNTR_INFO_REG_ID                  11

// Register width
#define ACQ_START_ADDR_REG_WIDTH                  9
#define ACQ_MEM_REGION_SIZE_REG_WIDTH             9
#define ACQ_NUM_MEM_REGIONS_REG_WIDTH             9
#define ACQ_INIT_REG_WIDTH                        3
#define ACQ_RECEIVED_SHORT_PACKETS_REG_WIDTH      32
#define ACQ_RECEIVED_LONG_PACKETS_REG_WIDTH       32
#define ACQ_LAST_COMMAND_REG_WIDTH                32
#define ACQ_NEXT_COMMAND_REG_WIDTH                32
#define ACQ_LAST_ACKNOWLEDGE_REG_WIDTH            32
#define ACQ_NEXT_ACKNOWLEDGE_REG_WIDTH            32
#define ACQ_FSM_STATE_INFO_REG_WIDTH              ((MEM2STREAM_FSM_STATE_BITS * 3) + (ACQ_SYNCHRONIZER_FSM_STATE_BITS * 3))
#define ACQ_INT_CNTR_INFO_REG_WIDTH               32

/* register reset value */
#define ACQ_START_ADDR_REG_RSTVAL                 0
#define ACQ_MEM_REGION_SIZE_REG_RSTVAL            128
#define ACQ_NUM_MEM_REGIONS_REG_RSTVAL            3
#define ACQ_INIT_REG_RSTVAL                       0
#define ACQ_RECEIVED_SHORT_PACKETS_REG_RSTVAL     0
#define ACQ_RECEIVED_LONG_PACKETS_REG_RSTVAL      0
#define ACQ_LAST_COMMAND_REG_RSTVAL               0
#define ACQ_NEXT_COMMAND_REG_RSTVAL               0
#define ACQ_LAST_ACKNOWLEDGE_REG_RSTVAL           0
#define ACQ_NEXT_ACKNOWLEDGE_REG_RSTVAL           0
#define ACQ_FSM_STATE_INFO_REG_RSTVAL             0
#define ACQ_INT_CNTR_INFO_REG_RSTVAL              0

/* bit definitions */
#define ACQ_INIT_RST_REG_BIT                      0
#define ACQ_INIT_RESYNC_BIT                       2
#define ACQ_INIT_RST_IDX                          ACQ_INIT_RST_REG_BIT
#define ACQ_INIT_RST_BITS                         1
#define ACQ_INIT_RESYNC_IDX                       ACQ_INIT_RESYNC_BIT
#define ACQ_INIT_RESYNC_BITS                      1

/* --------------------------------------------------*/
/* TOKEN INFO */
/* --------------------------------------------------*/
#define ACQ_TOKEN_ID_LSB                          0
#define ACQ_TOKEN_ID_MSB                          3
#define ACQ_TOKEN_WIDTH                           (ACQ_TOKEN_ID_MSB - ACQ_TOKEN_ID_LSB  + 1) // 4
#define ACQ_TOKEN_ID_IDX                          0
#define ACQ_TOKEN_ID_BITS                         ACQ_TOKEN_WIDTH
#define ACQ_INIT_CMD_INIT_IDX                     4
#define ACQ_INIT_CMD_INIT_BITS                    3
#define ACQ_CMD_START_ADDR_IDX                    4
#define ACQ_CMD_START_ADDR_BITS                   9
#define ACQ_CMD_NOFWORDS_IDX                      13
#define ACQ_CMD_NOFWORDS_BITS                     9
#define ACQ_MEM_REGION_ID_IDX                     22
#define ACQ_MEM_REGION_ID_BITS                    9
#define ACQ_PACKET_LENGTH_TOKEN_MSB               21
#define ACQ_PACKET_LENGTH_TOKEN_LSB               13
#define ACQ_PACKET_DATA_FORMAT_ID_TOKEN_MSB       9
#define ACQ_PACKET_DATA_FORMAT_ID_TOKEN_LSB       4
#define ACQ_PACKET_CH_ID_TOKEN_MSB                11
#define ACQ_PACKET_CH_ID_TOKEN_LSB                10
#define ACQ_PACKET_MEM_REGION_ID_TOKEN_MSB        12		/* only for capt_end_of_packet_written */
#define ACQ_PACKET_MEM_REGION_ID_TOKEN_LSB        4		/* only for capt_end_of_packet_written */

/* Command tokens IDs */
#define ACQ_READ_REGION_AUTO_INCR_TOKEN_ID        0 //0000b
#define ACQ_READ_REGION_TOKEN_ID                  1 //0001b
#define ACQ_READ_REGION_SOP_TOKEN_ID              2 //0010b
#define ACQ_INIT_TOKEN_ID                         8 //1000b

/* Acknowledge token IDs */
#define ACQ_READ_REGION_ACK_TOKEN_ID              0 //0000b
#define ACQ_END_OF_PACKET_TOKEN_ID                4 //0100b
#define ACQ_END_OF_REGION_TOKEN_ID                5 //0101b
#define ACQ_SOP_MISMATCH_TOKEN_ID                 6 //0110b
#define ACQ_UNDEF_PH_TOKEN_ID                     7 //0111b

#define ACQ_TOKEN_MEMREGIONID_MSB                 30
#define ACQ_TOKEN_MEMREGIONID_LSB                 22
#define ACQ_TOKEN_NOFWORDS_MSB                    21
#define ACQ_TOKEN_NOFWORDS_LSB                    13
#define ACQ_TOKEN_STARTADDR_MSB                   12
#define ACQ_TOKEN_STARTADDR_LSB                   4

/* --------------------------------------------------*/
/* MIPI */
/* --------------------------------------------------*/

#define WORD_COUNT_WIDTH                          16
#define PKT_CODE_WIDTH                            6
#define CHN_NO_WIDTH                              2
#define ERROR_INFO_WIDTH                          8

#define LONG_PKTCODE_MAX                          63
#define LONG_PKTCODE_MIN                          16
#define SHORT_PKTCODE_MAX                         15

#define EOF_CODE                                  1

/* --------------------------------------------------*/
/* Packet Info */
/* --------------------------------------------------*/
#define ACQ_START_OF_FRAME                        0
#define ACQ_END_OF_FRAME                          1
#define ACQ_START_OF_LINE                         2
#define ACQ_END_OF_LINE                           3
#define ACQ_LINE_PAYLOAD                          4
#define ACQ_GEN_SH_PKT                            5

/* bit definition */
#define ACQ_PKT_TYPE_IDX                          16
#define ACQ_PKT_TYPE_BITS                         6
#define ACQ_PKT_SOP_IDX                           32
#define ACQ_WORD_CNT_IDX                          0
#define ACQ_WORD_CNT_BITS                         16
#define ACQ_PKT_INFO_IDX                          16
#define ACQ_PKT_INFO_BITS                         8
#define ACQ_HEADER_DATA_IDX                       0
#define ACQ_HEADER_DATA_BITS                      16
#define ACQ_ACK_TOKEN_ID_IDX                      ACQ_TOKEN_ID_IDX
#define ACQ_ACK_TOKEN_ID_BITS                     ACQ_TOKEN_ID_BITS
#define ACQ_ACK_NOFWORDS_IDX                      13
#define ACQ_ACK_NOFWORDS_BITS                     9
#define ACQ_ACK_PKT_LEN_IDX                       4
#define ACQ_ACK_PKT_LEN_BITS                      16

/* --------------------------------------------------*/
/* Packet Data Type */
/* --------------------------------------------------*/

#define ACQ_YUV420_8_DATA                       24   /* 01 1000 YUV420 8-bit                                        */
#define ACQ_YUV420_10_DATA                      25   /* 01 1001  YUV420 10-bit                                      */
#define ACQ_YUV420_8L_DATA                      26   /* 01 1010   YUV420 8-bit legacy                               */
#define ACQ_YUV422_8_DATA                       30   /* 01 1110   YUV422 8-bit                                      */
#define ACQ_YUV422_10_DATA                      31   /* 01 1111   YUV422 10-bit                                     */
#define ACQ_RGB444_DATA                         32   /* 10 0000   RGB444                                            */
#define ACQ_RGB555_DATA						 33   /* 10 0001   RGB555                                            */
#define ACQ_RGB565_DATA						 34   /* 10 0010   RGB565                                            */
#define ACQ_RGB666_DATA						 35   /* 10 0011   RGB666                                            */
#define ACQ_RGB888_DATA						 36   /* 10 0100   RGB888                                            */
#define ACQ_RAW6_DATA							 40   /* 10 1000   RAW6                                              */
#define ACQ_RAW7_DATA							 41   /* 10 1001   RAW7                                              */
#define ACQ_RAW8_DATA							 42   /* 10 1010   RAW8                                              */
#define ACQ_RAW10_DATA						 43   /* 10 1011   RAW10                                             */
#define ACQ_RAW12_DATA						 44   /* 10 1100   RAW12                                             */
#define ACQ_RAW14_DATA						 45   /* 10 1101   RAW14                                             */
#define ACQ_USR_DEF_1_DATA						 48   /* 11 0000    JPEG [User Defined 8-bit Data Type 1]            */
#define ACQ_USR_DEF_2_DATA						 49   /* 11 0001    User Defined 8-bit Data Type 2                   */
#define ACQ_USR_DEF_3_DATA						 50   /* 11 0010    User Defined 8-bit Data Type 3                   */
#define ACQ_USR_DEF_4_DATA						 51   /* 11 0011    User Defined 8-bit Data Type 4                   */
#define ACQ_USR_DEF_5_DATA						 52   /* 11 0100    User Defined 8-bit Data Type 5                   */
#define ACQ_USR_DEF_6_DATA						 53   /* 11 0101    User Defined 8-bit Data Type 6                   */
#define ACQ_USR_DEF_7_DATA						 54   /* 11 0110    User Defined 8-bit Data Type 7                   */
#define ACQ_USR_DEF_8_DATA						 55   /* 11 0111    User Defined 8-bit Data Type 8                   */
#define ACQ_Emb_DATA							 18   /* 01 0010    embedded eight bit non image data                */
#define ACQ_SOF_DATA							 0   /* 00 0000    frame start                                      */
#define ACQ_EOF_DATA							 1   /* 00 0001    frame end                                        */
#define ACQ_SOL_DATA							 2   /* 00 0010    line start                                       */
#define ACQ_EOL_DATA							 3   /* 00 0011    line end                                         */
#define ACQ_GEN_SH1_DATA						 8   /* 00 1000  Generic Short Packet Code 1                        */
#define ACQ_GEN_SH2_DATA						 9   /* 00 1001    Generic Short Packet Code 2                      */
#define ACQ_GEN_SH3_DATA						 10   /* 00 1010    Generic Short Packet Code 3                      */
#define ACQ_GEN_SH4_DATA						 11   /* 00 1011    Generic Short Packet Code 4                      */
#define ACQ_GEN_SH5_DATA						 12   /* 00 1100    Generic Short Packet Code 5                      */
#define ACQ_GEN_SH6_DATA						 13   /* 00 1101    Generic Short Packet Code 6                      */
#define ACQ_GEN_SH7_DATA						 14   /* 00 1110    Generic Short Packet Code 7                      */
#define ACQ_GEN_SH8_DATA						 15   /* 00 1111    Generic Short Packet Code 8                      */
#define ACQ_YUV420_8_CSPS_DATA					 28   /* 01 1100   YUV420 8-bit (Chroma Shifted Pixel Sampling)      */
#define ACQ_YUV420_10_CSPS_DATA					 29   /* 01 1101   YUV420 10-bit (Chroma Shifted Pixel Sampling)     */
#define ACQ_RESERVED_DATA_TYPE_MIN              56
#define ACQ_RESERVED_DATA_TYPE_MAX              63
#define ACQ_GEN_LONG_RESERVED_DATA_TYPE_MIN     19
#define ACQ_GEN_LONG_RESERVED_DATA_TYPE_MAX     23
#define ACQ_YUV_RESERVED_DATA_TYPE              27
#define ACQ_RGB_RESERVED_DATA_TYPE_MIN          37
#define ACQ_RGB_RESERVED_DATA_TYPE_MAX          39
#define ACQ_RAW_RESERVED_DATA_TYPE_MIN          46
#define ACQ_RAW_RESERVED_DATA_TYPE_MAX          47

/* --------------------------------------------------*/

#endif /* _isp_acquisition_defs_h */
