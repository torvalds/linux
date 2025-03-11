/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 */

#ifndef _isp_capture_defs_h
#define _isp_capture_defs_h

#define _ISP_CAPTURE_REG_ALIGN                    4  /* assuming 32 bit control bus width */
#define _ISP_CAPTURE_BITS_PER_ELEM                32  /* only for data, not SOP */
#define _ISP_CAPTURE_BYTES_PER_ELEM               (_ISP_CAPTURE_BITS_PER_ELEM / 8)
#define _ISP_CAPTURE_BYTES_PER_WORD               32		/* 256/8 */
#define _ISP_CAPTURE_ELEM_PER_WORD                _ISP_CAPTURE_BYTES_PER_WORD / _ISP_CAPTURE_BYTES_PER_ELEM

/* --------------------------------------------------*/

#define NOF_IRQS                                  2

/* --------------------------------------------------*/
/* REGISTER INFO */
/* --------------------------------------------------*/

// Number of registers
#define CAPT_NOF_REGS                             16

// Register id's of MMIO slave accessible registers
#define CAPT_START_MODE_REG_ID                    0
#define CAPT_START_ADDR_REG_ID                    1
#define CAPT_MEM_REGION_SIZE_REG_ID               2
#define CAPT_NUM_MEM_REGIONS_REG_ID               3
#define CAPT_INIT_REG_ID                          4
#define CAPT_START_REG_ID                         5
#define CAPT_STOP_REG_ID                          6

#define CAPT_PACKET_LENGTH_REG_ID                 7
#define CAPT_RECEIVED_LENGTH_REG_ID               8
#define CAPT_RECEIVED_SHORT_PACKETS_REG_ID        9
#define CAPT_RECEIVED_LONG_PACKETS_REG_ID         10
#define CAPT_LAST_COMMAND_REG_ID                  11
#define CAPT_NEXT_COMMAND_REG_ID                  12
#define CAPT_LAST_ACKNOWLEDGE_REG_ID              13
#define CAPT_NEXT_ACKNOWLEDGE_REG_ID              14
#define CAPT_FSM_STATE_INFO_REG_ID                15

// Register width
#define CAPT_START_MODE_REG_WIDTH                 1

#define CAPT_START_REG_WIDTH                      1
#define CAPT_STOP_REG_WIDTH                       1

/* --------------------------------------------------*/
/* FSM */
/* --------------------------------------------------*/
#define CAPT_WRITE2MEM_FSM_STATE_BITS             2
#define CAPT_SYNCHRONIZER_FSM_STATE_BITS          3

#define CAPT_PACKET_LENGTH_REG_WIDTH              17
#define CAPT_RECEIVED_LENGTH_REG_WIDTH            17
#define CAPT_RECEIVED_SHORT_PACKETS_REG_WIDTH     32
#define CAPT_RECEIVED_LONG_PACKETS_REG_WIDTH      32
#define CAPT_LAST_COMMAND_REG_WIDTH               32
#define CAPT_LAST_ACKNOWLEDGE_REG_WIDTH           32
#define CAPT_NEXT_ACKNOWLEDGE_REG_WIDTH           32
#define CAPT_FSM_STATE_INFO_REG_WIDTH             ((CAPT_WRITE2MEM_FSM_STATE_BITS * 3) + (CAPT_SYNCHRONIZER_FSM_STATE_BITS * 3))

/* register reset value */
#define CAPT_START_MODE_REG_RSTVAL                0
#define CAPT_START_ADDR_REG_RSTVAL                0
#define CAPT_MEM_REGION_SIZE_REG_RSTVAL           128
#define CAPT_NUM_MEM_REGIONS_REG_RSTVAL           3
#define CAPT_INIT_REG_RSTVAL                      0

#define CAPT_START_REG_RSTVAL                     0
#define CAPT_STOP_REG_RSTVAL                      0

#define CAPT_PACKET_LENGTH_REG_RSTVAL             0
#define CAPT_RECEIVED_LENGTH_REG_RSTVAL           0
#define CAPT_RECEIVED_SHORT_PACKETS_REG_RSTVAL    0
#define CAPT_RECEIVED_LONG_PACKETS_REG_RSTVAL     0
#define CAPT_LAST_COMMAND_REG_RSTVAL              0
#define CAPT_NEXT_COMMAND_REG_RSTVAL              0
#define CAPT_LAST_ACKNOWLEDGE_REG_RSTVAL          0
#define CAPT_NEXT_ACKNOWLEDGE_REG_RSTVAL          0
#define CAPT_FSM_STATE_INFO_REG_RSTVAL            0

/* bit definitions */
#define CAPT_INIT_RST_REG_BIT                     0
#define CAPT_INIT_FLUSH_BIT                       1
#define CAPT_INIT_RESYNC_BIT                      2
#define CAPT_INIT_RESTART_BIT                     3
#define CAPT_INIT_RESTART_MEM_ADDR_LSB            4

#define CAPT_INIT_RST_REG_IDX                     CAPT_INIT_RST_REG_BIT
#define CAPT_INIT_RST_REG_BITS                    1
#define CAPT_INIT_FLUSH_IDX                       CAPT_INIT_FLUSH_BIT
#define CAPT_INIT_FLUSH_BITS                      1
#define CAPT_INIT_RESYNC_IDX                      CAPT_INIT_RESYNC_BIT
#define CAPT_INIT_RESYNC_BITS                     1
#define CAPT_INIT_RESTART_IDX                     CAPT_INIT_RESTART_BIT
#define CAPT_INIT_RESTART_BITS									1
#define CAPT_INIT_RESTART_MEM_ADDR_IDX            CAPT_INIT_RESTART_MEM_ADDR_LSB

/* --------------------------------------------------*/
/* TOKEN INFO */
/* --------------------------------------------------*/
#define CAPT_TOKEN_ID_LSB                         0
#define CAPT_TOKEN_ID_MSB                         3
#define CAPT_TOKEN_WIDTH                         (CAPT_TOKEN_ID_MSB - CAPT_TOKEN_ID_LSB  + 1) /* 4 */

/* Command tokens IDs */
#define CAPT_START_TOKEN_ID                       0 /* 0000b */
#define CAPT_STOP_TOKEN_ID                        1 /* 0001b */
#define CAPT_FREEZE_TOKEN_ID                      2 /* 0010b */
#define CAPT_RESUME_TOKEN_ID                      3 /* 0011b */
#define CAPT_INIT_TOKEN_ID                        8 /* 1000b */

#define CAPT_START_TOKEN_BIT                      0
#define CAPT_STOP_TOKEN_BIT                       0
#define CAPT_FREEZE_TOKEN_BIT                     0
#define CAPT_RESUME_TOKEN_BIT                     0
#define CAPT_INIT_TOKEN_BIT                       0

/* Acknowledge token IDs */
#define CAPT_END_OF_PACKET_RECEIVED_TOKEN_ID      0 /* 0000b */
#define CAPT_END_OF_PACKET_WRITTEN_TOKEN_ID       1 /* 0001b */
#define CAPT_END_OF_REGION_WRITTEN_TOKEN_ID       2 /* 0010b */
#define CAPT_FLUSH_DONE_TOKEN_ID                  3 /* 0011b */
#define CAPT_PREMATURE_SOP_TOKEN_ID               4 /* 0100b */
#define CAPT_MISSING_SOP_TOKEN_ID                 5 /* 0101b */
#define CAPT_UNDEF_PH_TOKEN_ID                    6 /* 0110b */
#define CAPT_STOP_ACK_TOKEN_ID                    7 /* 0111b */

#define CAPT_PACKET_LENGTH_TOKEN_MSB             19
#define CAPT_PACKET_LENGTH_TOKEN_LSB              4
#define CAPT_SUPER_PACKET_LENGTH_TOKEN_MSB       20
#define CAPT_SUPER_PACKET_LENGTH_TOKEN_LSB        4
#define CAPT_PACKET_DATA_FORMAT_ID_TOKEN_MSB     25
#define CAPT_PACKET_DATA_FORMAT_ID_TOKEN_LSB     20
#define CAPT_PACKET_CH_ID_TOKEN_MSB              27
#define CAPT_PACKET_CH_ID_TOKEN_LSB              26
#define CAPT_PACKET_MEM_REGION_ID_TOKEN_MSB      29
#define CAPT_PACKET_MEM_REGION_ID_TOKEN_LSB      21

/*  bit definition */
#define CAPT_CMD_IDX                              CAPT_TOKEN_ID_LSB
#define	CAPT_CMD_BITS                             (CAPT_TOKEN_ID_MSB - CAPT_TOKEN_ID_LSB + 1)
#define CAPT_SOP_IDX                              32
#define CAPT_SOP_BITS                             1
#define CAPT_PKT_INFO_IDX                         16
#define CAPT_PKT_INFO_BITS                        8
#define CAPT_PKT_TYPE_IDX                         0
#define CAPT_PKT_TYPE_BITS                        6
#define CAPT_HEADER_DATA_IDX                      0
#define CAPT_HEADER_DATA_BITS                     16
#define CAPT_PKT_DATA_IDX                         0
#define CAPT_PKT_DATA_BITS                        32
#define CAPT_WORD_CNT_IDX                         0
#define CAPT_WORD_CNT_BITS                        16
#define CAPT_ACK_TOKEN_ID_IDX                     0
#define CAPT_ACK_TOKEN_ID_BITS                    4
//#define CAPT_ACK_PKT_LEN_IDX                      CAPT_PACKET_LENGTH_TOKEN_LSB
//#define CAPT_ACK_PKT_LEN_BITS                     (CAPT_PACKET_LENGTH_TOKEN_MSB - CAPT_PACKET_LENGTH_TOKEN_LSB + 1)
//#define CAPT_ACK_PKT_INFO_IDX                     20
//#define CAPT_ACK_PKT_INFO_BITS                    8
//#define CAPT_ACK_MEM_REG_ID1_IDX                  20			/* for capt_end_of_packet_written */
//#define CAPT_ACK_MEM_REG_ID2_IDX                  4       /* for capt_end_of_region_written */
#define CAPT_ACK_PKT_LEN_IDX                      CAPT_PACKET_LENGTH_TOKEN_LSB
#define CAPT_ACK_PKT_LEN_BITS                     (CAPT_PACKET_LENGTH_TOKEN_MSB - CAPT_PACKET_LENGTH_TOKEN_LSB + 1)
#define CAPT_ACK_SUPER_PKT_LEN_IDX                CAPT_SUPER_PACKET_LENGTH_TOKEN_LSB
#define CAPT_ACK_SUPER_PKT_LEN_BITS               (CAPT_SUPER_PACKET_LENGTH_TOKEN_MSB - CAPT_SUPER_PACKET_LENGTH_TOKEN_LSB + 1)
#define CAPT_ACK_PKT_INFO_IDX                     CAPT_PACKET_DATA_FORMAT_ID_TOKEN_LSB
#define CAPT_ACK_PKT_INFO_BITS                    (CAPT_PACKET_CH_ID_TOKEN_MSB - CAPT_PACKET_DATA_FORMAT_ID_TOKEN_LSB + 1)
#define CAPT_ACK_MEM_REGION_ID_IDX                CAPT_PACKET_MEM_REGION_ID_TOKEN_LSB
#define CAPT_ACK_MEM_REGION_ID_BITS               (CAPT_PACKET_MEM_REGION_ID_TOKEN_MSB - CAPT_PACKET_MEM_REGION_ID_TOKEN_LSB + 1)
#define CAPT_ACK_PKT_TYPE_IDX                     CAPT_PACKET_DATA_FORMAT_ID_TOKEN_LSB
#define CAPT_ACK_PKT_TYPE_BITS                    (CAPT_PACKET_DATA_FORMAT_ID_TOKEN_MSB - CAPT_PACKET_DATA_FORMAT_ID_TOKEN_LSB + 1)
#define CAPT_INIT_TOKEN_INIT_IDX                  4
#define CAPT_INIT_TOKEN_INIT_BITS                 22

/* --------------------------------------------------*/
/* MIPI */
/* --------------------------------------------------*/

#define CAPT_WORD_COUNT_WIDTH                     16
#define CAPT_PKT_CODE_WIDTH                       6
#define CAPT_CHN_NO_WIDTH                         2
#define CAPT_ERROR_INFO_WIDTH                     8

#define LONG_PKTCODE_MAX                          63
#define LONG_PKTCODE_MIN                          16
#define SHORT_PKTCODE_MAX                         15

/* --------------------------------------------------*/
/* Packet Info */
/* --------------------------------------------------*/
#define CAPT_START_OF_FRAME                       0
#define CAPT_END_OF_FRAME                         1
#define CAPT_START_OF_LINE                        2
#define CAPT_END_OF_LINE                          3
#define CAPT_LINE_PAYLOAD                         4
#define CAPT_GEN_SH_PKT                           5

/* --------------------------------------------------*/
/* Packet Data Type */
/* --------------------------------------------------*/

#define CAPT_YUV420_8_DATA                       24   /* 01 1000 YUV420 8-bit                                        */
#define CAPT_YUV420_10_DATA                      25   /* 01 1001  YUV420 10-bit                                      */
#define CAPT_YUV420_8L_DATA                      26   /* 01 1010   YUV420 8-bit legacy                               */
#define CAPT_YUV422_8_DATA                       30   /* 01 1110   YUV422 8-bit                                      */
#define CAPT_YUV422_10_DATA                      31   /* 01 1111   YUV422 10-bit                                     */
#define CAPT_RGB444_DATA                         32   /* 10 0000   RGB444                                            */
#define CAPT_RGB555_DATA						 33   /* 10 0001   RGB555                                            */
#define CAPT_RGB565_DATA						 34   /* 10 0010   RGB565                                            */
#define CAPT_RGB666_DATA						 35   /* 10 0011   RGB666                                            */
#define CAPT_RGB888_DATA						 36   /* 10 0100   RGB888                                            */
#define CAPT_RAW6_DATA							 40   /* 10 1000   RAW6                                              */
#define CAPT_RAW7_DATA							 41   /* 10 1001   RAW7                                              */
#define CAPT_RAW8_DATA							 42   /* 10 1010   RAW8                                              */
#define CAPT_RAW10_DATA						 43   /* 10 1011   RAW10                                             */
#define CAPT_RAW12_DATA						 44   /* 10 1100   RAW12                                             */
#define CAPT_RAW14_DATA						 45   /* 10 1101   RAW14                                             */
#define CAPT_USR_DEF_1_DATA						 48   /* 11 0000    JPEG [User Defined 8-bit Data Type 1]            */
#define CAPT_USR_DEF_2_DATA						 49   /* 11 0001    User Defined 8-bit Data Type 2                   */
#define CAPT_USR_DEF_3_DATA						 50   /* 11 0010    User Defined 8-bit Data Type 3                   */
#define CAPT_USR_DEF_4_DATA						 51   /* 11 0011    User Defined 8-bit Data Type 4                   */
#define CAPT_USR_DEF_5_DATA						 52   /* 11 0100    User Defined 8-bit Data Type 5                   */
#define CAPT_USR_DEF_6_DATA						 53   /* 11 0101    User Defined 8-bit Data Type 6                   */
#define CAPT_USR_DEF_7_DATA						 54   /* 11 0110    User Defined 8-bit Data Type 7                   */
#define CAPT_USR_DEF_8_DATA						 55   /* 11 0111    User Defined 8-bit Data Type 8                   */
#define CAPT_Emb_DATA							 18   /* 01 0010    embedded eight bit non image data                */
#define CAPT_SOF_DATA							 0   /* 00 0000    frame start                                      */
#define CAPT_EOF_DATA							 1   /* 00 0001    frame end                                        */
#define CAPT_SOL_DATA							 2   /* 00 0010    line start                                       */
#define CAPT_EOL_DATA							 3   /* 00 0011    line end                                         */
#define CAPT_GEN_SH1_DATA						 8   /* 00 1000  Generic Short Packet Code 1                        */
#define CAPT_GEN_SH2_DATA						 9   /* 00 1001    Generic Short Packet Code 2                      */
#define CAPT_GEN_SH3_DATA						 10   /* 00 1010    Generic Short Packet Code 3                      */
#define CAPT_GEN_SH4_DATA						 11   /* 00 1011    Generic Short Packet Code 4                      */
#define CAPT_GEN_SH5_DATA						 12   /* 00 1100    Generic Short Packet Code 5                      */
#define CAPT_GEN_SH6_DATA						 13   /* 00 1101    Generic Short Packet Code 6                      */
#define CAPT_GEN_SH7_DATA						 14   /* 00 1110    Generic Short Packet Code 7                      */
#define CAPT_GEN_SH8_DATA						 15   /* 00 1111    Generic Short Packet Code 8                      */
#define CAPT_YUV420_8_CSPS_DATA					 28   /* 01 1100   YUV420 8-bit (Chroma Shifted Pixel Sampling)      */
#define CAPT_YUV420_10_CSPS_DATA					 29   /* 01 1101   YUV420 10-bit (Chroma Shifted Pixel Sampling)     */
#define CAPT_RESERVED_DATA_TYPE_MIN              56
#define CAPT_RESERVED_DATA_TYPE_MAX              63
#define CAPT_GEN_LONG_RESERVED_DATA_TYPE_MIN     19
#define CAPT_GEN_LONG_RESERVED_DATA_TYPE_MAX     23
#define CAPT_YUV_RESERVED_DATA_TYPE              27
#define CAPT_RGB_RESERVED_DATA_TYPE_MIN          37
#define CAPT_RGB_RESERVED_DATA_TYPE_MAX          39
#define CAPT_RAW_RESERVED_DATA_TYPE_MIN          46
#define CAPT_RAW_RESERVED_DATA_TYPE_MAX          47

/* --------------------------------------------------*/
/* Capture Unit State */
/* --------------------------------------------------*/
#define CAPT_FREE_RUN                             0
#define CAPT_NO_SYNC                              1
#define CAPT_SYNC_SWP                             2
#define CAPT_SYNC_MWP                             3
#define CAPT_SYNC_WAIT                            4
#define CAPT_FREEZE                               5
#define CAPT_RUN                                  6

/* --------------------------------------------------*/

#endif /* _isp_capture_defs_h */
