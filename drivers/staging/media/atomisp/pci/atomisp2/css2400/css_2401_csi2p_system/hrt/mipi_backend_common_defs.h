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

#ifndef _css_receiver_2400_common_defs_h_
#define _css_receiver_2400_common_defs_h_
#ifndef _mipi_backend_common_defs_h_
#define _mipi_backend_common_defs_h_

#define _HRT_CSS_RECEIVER_2400_GEN_SHORT_DATA_WIDTH     16
#define _HRT_CSS_RECEIVER_2400_GEN_SHORT_CH_ID_WIDTH     2
#define _HRT_CSS_RECEIVER_2400_GEN_SHORT_FMT_TYPE_WIDTH  3
#define _HRT_CSS_RECEIVER_2400_GEN_SHORT_STR_REAL_WIDTH (_HRT_CSS_RECEIVER_2400_GEN_SHORT_DATA_WIDTH + _HRT_CSS_RECEIVER_2400_GEN_SHORT_CH_ID_WIDTH + _HRT_CSS_RECEIVER_2400_GEN_SHORT_FMT_TYPE_WIDTH)
#define _HRT_CSS_RECEIVER_2400_GEN_SHORT_STR_WIDTH      32 /* use 32 to be compatibel with streaming monitor !, MSB's of interface are tied to '0' */ 

/* Definition of data format ID at the interface CSS_receiver capture/acquisition units */
#define _HRT_CSS_RECEIVER_2400_DATA_FORMAT_ID_YUV420_8          24   /* 01 1000 YUV420 8-bit                                        */
#define _HRT_CSS_RECEIVER_2400_DATA_FORMAT_ID_YUV420_10         25   /* 01 1001  YUV420 10-bit                                      */
#define _HRT_CSS_RECEIVER_2400_DATA_FORMAT_ID_YUV420_8L         26   /* 01 1010   YUV420 8-bit legacy                               */
#define _HRT_CSS_RECEIVER_2400_DATA_FORMAT_ID_YUV422_8          30   /* 01 1110   YUV422 8-bit                                      */
#define _HRT_CSS_RECEIVER_2400_DATA_FORMAT_ID_YUV422_10         31   /* 01 1111   YUV422 10-bit                                     */
#define _HRT_CSS_RECEIVER_2400_DATA_FORMAT_ID_RGB444            32   /* 10 0000   RGB444                                            */
#define _HRT_CSS_RECEIVER_2400_DATA_FORMAT_ID_RGB555            33   /* 10 0001   RGB555                                            */
#define _HRT_CSS_RECEIVER_2400_DATA_FORMAT_ID_RGB565            34   /* 10 0010   RGB565                                            */
#define _HRT_CSS_RECEIVER_2400_DATA_FORMAT_ID_RGB666            35   /* 10 0011   RGB666                                            */
#define _HRT_CSS_RECEIVER_2400_DATA_FORMAT_ID_RGB888            36   /* 10 0100   RGB888                                            */
#define _HRT_CSS_RECEIVER_2400_DATA_FORMAT_ID_RAW6              40   /* 10 1000   RAW6                                              */
#define _HRT_CSS_RECEIVER_2400_DATA_FORMAT_ID_RAW7              41   /* 10 1001   RAW7                                              */
#define _HRT_CSS_RECEIVER_2400_DATA_FORMAT_ID_RAW8              42   /* 10 1010   RAW8                                              */
#define _HRT_CSS_RECEIVER_2400_DATA_FORMAT_ID_RAW10             43   /* 10 1011   RAW10                                             */
#define _HRT_CSS_RECEIVER_2400_DATA_FORMAT_ID_RAW12             44   /* 10 1100   RAW12                                             */
#define _HRT_CSS_RECEIVER_2400_DATA_FORMAT_ID_RAW14             45   /* 10 1101   RAW14                                             */
#define _HRT_CSS_RECEIVER_2400_DATA_FORMAT_ID_USR_DEF_1         48   /* 11 0000    JPEG [User Defined 8-bit Data Type 1]            */
#define _HRT_CSS_RECEIVER_2400_DATA_FORMAT_ID_USR_DEF_2         49   /* 11 0001    User Defined 8-bit Data Type 2                   */
#define _HRT_CSS_RECEIVER_2400_DATA_FORMAT_ID_USR_DEF_3         50   /* 11 0010    User Defined 8-bit Data Type 3                   */
#define _HRT_CSS_RECEIVER_2400_DATA_FORMAT_ID_USR_DEF_4         51   /* 11 0011    User Defined 8-bit Data Type 4                   */
#define _HRT_CSS_RECEIVER_2400_DATA_FORMAT_ID_USR_DEF_5         52   /* 11 0100    User Defined 8-bit Data Type 5                   */
#define _HRT_CSS_RECEIVER_2400_DATA_FORMAT_ID_USR_DEF_6         53   /* 11 0101    User Defined 8-bit Data Type 6                   */
#define _HRT_CSS_RECEIVER_2400_DATA_FORMAT_ID_USR_DEF_7         54   /* 11 0110    User Defined 8-bit Data Type 7                   */
#define _HRT_CSS_RECEIVER_2400_DATA_FORMAT_ID_USR_DEF_8         55   /* 11 0111    User Defined 8-bit Data Type 8                   */
#define _HRT_CSS_RECEIVER_2400_DATA_FORMAT_ID_Emb               18   /* 01 0010    embedded eight bit non image data                */
#define _HRT_CSS_RECEIVER_2400_DATA_FORMAT_ID_SOF                0   /* 00 0000    frame start                                      */
#define _HRT_CSS_RECEIVER_2400_DATA_FORMAT_ID_EOF                1   /* 00 0001    frame end                                        */
#define _HRT_CSS_RECEIVER_2400_DATA_FORMAT_ID_SOL                2   /* 00 0010    line start                                       */
#define _HRT_CSS_RECEIVER_2400_DATA_FORMAT_ID_EOL                3   /* 00 0011    line end                                         */
#define _HRT_CSS_RECEIVER_2400_DATA_FORMAT_ID_GEN_SH1            8   /* 00 1000  Generic Short Packet Code 1                        */
#define _HRT_CSS_RECEIVER_2400_DATA_FORMAT_ID_GEN_SH2            9   /* 00 1001    Generic Short Packet Code 2                      */
#define _HRT_CSS_RECEIVER_2400_DATA_FORMAT_ID_GEN_SH3           10   /* 00 1010    Generic Short Packet Code 3                      */
#define _HRT_CSS_RECEIVER_2400_DATA_FORMAT_ID_GEN_SH4           11   /* 00 1011    Generic Short Packet Code 4                      */
#define _HRT_CSS_RECEIVER_2400_DATA_FORMAT_ID_GEN_SH5           12   /* 00 1100    Generic Short Packet Code 5                      */
#define _HRT_CSS_RECEIVER_2400_DATA_FORMAT_ID_GEN_SH6           13   /* 00 1101    Generic Short Packet Code 6                      */
#define _HRT_CSS_RECEIVER_2400_DATA_FORMAT_ID_GEN_SH7           14   /* 00 1110    Generic Short Packet Code 7                      */
#define _HRT_CSS_RECEIVER_2400_DATA_FORMAT_ID_GEN_SH8           15   /* 00 1111    Generic Short Packet Code 8                      */
#define _HRT_CSS_RECEIVER_2400_DATA_FORMAT_ID_YUV420_8_CSPS     28   /* 01 1100   YUV420 8-bit (Chroma Shifted Pixel Sampling)      */
#define _HRT_CSS_RECEIVER_2400_DATA_FORMAT_ID_YUV420_10_CSPS    29   /* 01 1101   YUV420 10-bit (Chroma Shifted Pixel Sampling)     */
/* used reseved mipi positions for these */
#define _HRT_CSS_RECEIVER_2400_DATA_FORMAT_ID_RAW16             46 
#define _HRT_CSS_RECEIVER_2400_DATA_FORMAT_ID_RAW18             47 
#define _HRT_CSS_RECEIVER_2400_DATA_FORMAT_ID_RAW18_2           37 
#define _HRT_CSS_RECEIVER_2400_DATA_FORMAT_ID_RAW18_3           38 

//_HRT_CSS_RECEIVER_2400_FMT_TYPE_CUSTOM 63
#define _HRT_MIPI_BACKEND_FMT_TYPE_CUSTOM                       63

#define _HRT_CSS_RECEIVER_2400_DATA_FORMAT_ID_WIDTH              6

/* Definition of format_types at the interface CSS --> input_selector*/
/* !! Changes here should be copied to systems/isp/isp_css/bin/conv_transmitter_cmd.tcl !! */
#define _HRT_CSS_RECEIVER_2400_FMT_TYPE_RGB888           0  // 36 'h24
#define _HRT_CSS_RECEIVER_2400_FMT_TYPE_RGB555           1  // 33 'h
#define _HRT_CSS_RECEIVER_2400_FMT_TYPE_RGB444           2  // 32
#define _HRT_CSS_RECEIVER_2400_FMT_TYPE_RGB565           3  // 34
#define _HRT_CSS_RECEIVER_2400_FMT_TYPE_RGB666           4  // 35
#define _HRT_CSS_RECEIVER_2400_FMT_TYPE_RAW8             5  // 42 
#define _HRT_CSS_RECEIVER_2400_FMT_TYPE_RAW10            6  // 43
#define _HRT_CSS_RECEIVER_2400_FMT_TYPE_RAW6             7  // 40
#define _HRT_CSS_RECEIVER_2400_FMT_TYPE_RAW7             8  // 41
#define _HRT_CSS_RECEIVER_2400_FMT_TYPE_RAW12            9  // 43
#define _HRT_CSS_RECEIVER_2400_FMT_TYPE_RAW14           10  // 45
#define _HRT_CSS_RECEIVER_2400_FMT_TYPE_YUV420_8        11  // 30
#define _HRT_CSS_RECEIVER_2400_FMT_TYPE_YUV420_10       12  // 25
#define _HRT_CSS_RECEIVER_2400_FMT_TYPE_YUV422_8        13  // 30
#define _HRT_CSS_RECEIVER_2400_FMT_TYPE_YUV422_10       14  // 31
#define _HRT_CSS_RECEIVER_2400_FMT_TYPE_USR_DEF_1       15  // 48
#define _HRT_CSS_RECEIVER_2400_FMT_TYPE_YUV420_8L       16  // 26
#define _HRT_CSS_RECEIVER_2400_FMT_TYPE_Emb             17  // 18
#define _HRT_CSS_RECEIVER_2400_FMT_TYPE_USR_DEF_2       18  // 49
#define _HRT_CSS_RECEIVER_2400_FMT_TYPE_USR_DEF_3       19  // 50
#define _HRT_CSS_RECEIVER_2400_FMT_TYPE_USR_DEF_4       20  // 51
#define _HRT_CSS_RECEIVER_2400_FMT_TYPE_USR_DEF_5       21  // 52
#define _HRT_CSS_RECEIVER_2400_FMT_TYPE_USR_DEF_6       22  // 53
#define _HRT_CSS_RECEIVER_2400_FMT_TYPE_USR_DEF_7       23  // 54
#define _HRT_CSS_RECEIVER_2400_FMT_TYPE_USR_DEF_8       24  // 55
#define _HRT_CSS_RECEIVER_2400_FMT_TYPE_YUV420_8_CSPS   25  // 28
#define _HRT_CSS_RECEIVER_2400_FMT_TYPE_YUV420_10_CSPS  26  // 29
#define _HRT_CSS_RECEIVER_2400_FMT_TYPE_RAW16           27  // ?
#define _HRT_CSS_RECEIVER_2400_FMT_TYPE_RAW18           28  // ?
#define _HRT_CSS_RECEIVER_2400_FMT_TYPE_RAW18_2         29  // ? Option 2 for depacketiser
#define _HRT_CSS_RECEIVER_2400_FMT_TYPE_RAW18_3         30  // ? Option 3 for depacketiser
#define _HRT_CSS_RECEIVER_2400_FMT_TYPE_CUSTOM          31  // to signal custom decoding 

/* definition for state machine of data FIFO for decode different type of data */
#define _HRT_CSS_RECEIVER_2400_YUV420_8_REPEAT_PTN                 1  
#define _HRT_CSS_RECEIVER_2400_YUV420_10_REPEAT_PTN                5
#define _HRT_CSS_RECEIVER_2400_YUV420_8L_REPEAT_PTN                1
#define _HRT_CSS_RECEIVER_2400_YUV422_8_REPEAT_PTN                 1
#define _HRT_CSS_RECEIVER_2400_YUV422_10_REPEAT_PTN                5
#define _HRT_CSS_RECEIVER_2400_RGB444_REPEAT_PTN                   2 
#define _HRT_CSS_RECEIVER_2400_RGB555_REPEAT_PTN                   2
#define _HRT_CSS_RECEIVER_2400_RGB565_REPEAT_PTN                   2
#define _HRT_CSS_RECEIVER_2400_RGB666_REPEAT_PTN                   9                       
#define _HRT_CSS_RECEIVER_2400_RGB888_REPEAT_PTN                   3
#define _HRT_CSS_RECEIVER_2400_RAW6_REPEAT_PTN                     3
#define _HRT_CSS_RECEIVER_2400_RAW7_REPEAT_PTN                     7
#define _HRT_CSS_RECEIVER_2400_RAW8_REPEAT_PTN                     1
#define _HRT_CSS_RECEIVER_2400_RAW10_REPEAT_PTN                    5
#define _HRT_CSS_RECEIVER_2400_RAW12_REPEAT_PTN                    3        
#define _HRT_CSS_RECEIVER_2400_RAW14_REPEAT_PTN                    7

#define _HRT_CSS_RECEIVER_2400_MAX_REPEAT_PTN                      _HRT_CSS_RECEIVER_2400_RGB666_REPEAT_PTN

#define _HRT_CSS_RECEIVER_2400_BE_COMP_FMT_IDX                     0
#define _HRT_CSS_RECEIVER_2400_BE_COMP_FMT_WIDTH                   3
#define _HRT_CSS_RECEIVER_2400_BE_COMP_PRED_IDX                    3
#define _HRT_CSS_RECEIVER_2400_BE_COMP_PRED_WIDTH                  1
#define _HRT_CSS_RECEIVER_2400_BE_COMP_USD_BITS                    4  /* bits per USD type */

#define _HRT_CSS_RECEIVER_2400_BE_RAW16_DATAID_IDX                 0
#define _HRT_CSS_RECEIVER_2400_BE_RAW16_EN_IDX                     6
#define _HRT_CSS_RECEIVER_2400_BE_RAW18_DATAID_IDX                 0
#define _HRT_CSS_RECEIVER_2400_BE_RAW18_OPTION_IDX                 6
#define _HRT_CSS_RECEIVER_2400_BE_RAW18_EN_IDX                     8

#define _HRT_CSS_RECEIVER_2400_BE_COMP_NO_COMP                     0
#define _HRT_CSS_RECEIVER_2400_BE_COMP_10_6_10                     1
#define _HRT_CSS_RECEIVER_2400_BE_COMP_10_7_10                     2
#define _HRT_CSS_RECEIVER_2400_BE_COMP_10_8_10                     3
#define _HRT_CSS_RECEIVER_2400_BE_COMP_12_6_12                     4
#define _HRT_CSS_RECEIVER_2400_BE_COMP_12_7_12                     5
#define _HRT_CSS_RECEIVER_2400_BE_COMP_12_8_12                     6


/* packet bit definition */
#define _HRT_CSS_RECEIVER_2400_PKT_SOP_IDX                        32
#define _HRT_CSS_RECEIVER_2400_PKT_SOP_BITS                        1
#define _HRT_CSS_RECEIVER_2400_PKT_CH_ID_IDX                      22
#define _HRT_CSS_RECEIVER_2400_PKT_CH_ID_BITS                      2
#define _HRT_CSS_RECEIVER_2400_PKT_FMT_ID_IDX                     16
#define _HRT_CSS_RECEIVER_2400_PKT_FMT_ID_BITS                     6
#define _HRT_CSS_RECEIVER_2400_PH_DATA_FIELD_IDX                   0
#define _HRT_CSS_RECEIVER_2400_PH_DATA_FIELD_BITS                 16
#define _HRT_CSS_RECEIVER_2400_PKT_PAYLOAD_IDX                     0
#define _HRT_CSS_RECEIVER_2400_PKT_PAYLOAD_BITS                   32


/*************************************************************************************************/
/* Custom Decoding                                                                               */
/* These Custom Defs are defined based on design-time config in "mipi_backend_pixel_formatter.chdl" !! */
/*************************************************************************************************/
/*
#define BE_CUST_EN_IDX                     0     // 2bits 
#define BE_CUST_EN_DATAID_IDX              2     // 6bits MIPI DATA ID 
#define BE_CUST_EN_WIDTH                   8     
#define BE_CUST_MODE_ALL                   1     // Enable Custom Decoding for all DATA IDs 
#define BE_CUST_MODE_ONE                   3     // Enable Custom Decoding for ONE DATA ID, programmed in CUST_EN_DATA_ID 

// Data State config = {get_bits(6bits), valid(1bit)}  //
#define BE_CUST_DATA_STATE_S0_IDX          0     // 7bits
#define BE_CUST_DATA_STATE_S1_IDX          8 //7      // 7bits 
#define BE_CUST_DATA_STATE_S2_IDX          16//14    // 7bits /
#define BE_CUST_DATA_STATE_WIDTH           24//21    
#define BE_CUST_DATA_STATE_VALID_IDX       0     // 1bits 
#define BE_CUST_DATA_STATE_GETBITS_IDX     1     // 6bits 




// Pixel Extractor config 
#define BE_CUST_PIX_EXT_DATA_ALIGN_IDX     0     // 6bits 
#define BE_CUST_PIX_EXT_PIX_ALIGN_IDX      6//5     // 5bits 
#define BE_CUST_PIX_EXT_PIX_MASK_IDX       11//10    // 18bits
#define BE_CUST_PIX_EXT_PIX_EN_IDX         29 //28    // 1bits

#define BE_CUST_PIX_EXT_WIDTH              30//29    

// Pixel Valid & EoP config = {[eop,valid](especial), [eop,valid](normal)} 
#define BE_CUST_PIX_VALID_EOP_P0_IDX        0    // 4bits 
#define BE_CUST_PIX_VALID_EOP_P1_IDX        4    // 4bits 
#define BE_CUST_PIX_VALID_EOP_P2_IDX        8    // 4bits 
#define BE_CUST_PIX_VALID_EOP_P3_IDX        12   // 4bits 
#define BE_CUST_PIX_VALID_EOP_WIDTH         16 
#define BE_CUST_PIX_VALID_EOP_NOR_VALID_IDX 0    // Normal (NO less get_bits case) Valid - 1bits
#define BE_CUST_PIX_VALID_EOP_NOR_EOP_IDX   1    // Normal (NO less get_bits case) EoP - 1bits 
#define BE_CUST_PIX_VALID_EOP_ESP_VALID_IDX 2    // Especial (less get_bits case) Valid - 1bits 
#define BE_CUST_PIX_VALID_EOP_ESP_EOP_IDX   3    // Especial (less get_bits case) EoP - 1bits

*/

#endif /* _mipi_backend_common_defs_h_ */
#endif /* _css_receiver_2400_common_defs_h_ */ 
