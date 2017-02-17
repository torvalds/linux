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

#ifndef _mipi_backend_defs_h
#define _mipi_backend_defs_h

#include "mipi_backend_common_defs.h"

#define MIPI_BACKEND_REG_ALIGN                    4 // assuming 32 bit control bus width 

#define _HRT_MIPI_BACKEND_NOF_IRQS                         3 // sid_lut     

// SH Backend Register IDs
#define _HRT_MIPI_BACKEND_ENABLE_REG_IDX                   0  
#define _HRT_MIPI_BACKEND_STATUS_REG_IDX                   1  
//#define _HRT_MIPI_BACKEND_HIGH_PREC_REG_IDX                2
#define _HRT_MIPI_BACKEND_COMP_FORMAT_REG0_IDX             2
#define _HRT_MIPI_BACKEND_COMP_FORMAT_REG1_IDX             3
#define _HRT_MIPI_BACKEND_COMP_FORMAT_REG2_IDX             4
#define _HRT_MIPI_BACKEND_COMP_FORMAT_REG3_IDX             5
#define _HRT_MIPI_BACKEND_RAW16_CONFIG_REG_IDX             6
#define _HRT_MIPI_BACKEND_RAW18_CONFIG_REG_IDX             7
#define _HRT_MIPI_BACKEND_FORCE_RAW8_REG_IDX               8
#define _HRT_MIPI_BACKEND_IRQ_STATUS_REG_IDX               9
#define _HRT_MIPI_BACKEND_IRQ_CLEAR_REG_IDX               10
////
#define _HRT_MIPI_BACKEND_CUST_EN_REG_IDX                 11        
#define _HRT_MIPI_BACKEND_CUST_DATA_STATE_REG_IDX         12
#define _HRT_MIPI_BACKEND_CUST_PIX_EXT_S0P0_REG_IDX       13
#define _HRT_MIPI_BACKEND_CUST_PIX_EXT_S0P1_REG_IDX       14
#define _HRT_MIPI_BACKEND_CUST_PIX_EXT_S0P2_REG_IDX       15
#define _HRT_MIPI_BACKEND_CUST_PIX_EXT_S0P3_REG_IDX       16
#define _HRT_MIPI_BACKEND_CUST_PIX_EXT_S1P0_REG_IDX       17
#define _HRT_MIPI_BACKEND_CUST_PIX_EXT_S1P1_REG_IDX       18
#define _HRT_MIPI_BACKEND_CUST_PIX_EXT_S1P2_REG_IDX       19
#define _HRT_MIPI_BACKEND_CUST_PIX_EXT_S1P3_REG_IDX       20
#define _HRT_MIPI_BACKEND_CUST_PIX_EXT_S2P0_REG_IDX       21
#define _HRT_MIPI_BACKEND_CUST_PIX_EXT_S2P1_REG_IDX       22
#define _HRT_MIPI_BACKEND_CUST_PIX_EXT_S2P2_REG_IDX       23
#define _HRT_MIPI_BACKEND_CUST_PIX_EXT_S2P3_REG_IDX       24
#define _HRT_MIPI_BACKEND_CUST_PIX_VALID_EOP_REG_IDX      25
////
#define _HRT_MIPI_BACKEND_GLOBAL_LUT_DISREGARD_REG_IDX    26
#define _HRT_MIPI_BACKEND_PKT_STALL_STATUS_REG_IDX        27
//#define _HRT_MIPI_BACKEND_SP_LUT_ENABLE_REG_IDX           28
#define _HRT_MIPI_BACKEND_SP_LUT_ENTRY_0_REG_IDX          28 
#define _HRT_MIPI_BACKEND_SP_LUT_ENTRY_1_REG_IDX          29 
#define _HRT_MIPI_BACKEND_SP_LUT_ENTRY_2_REG_IDX          30  
#define _HRT_MIPI_BACKEND_SP_LUT_ENTRY_3_REG_IDX          31 

#define _HRT_MIPI_BACKEND_NOF_REGISTERS                   32 // excluding the LP LUT entries

#define _HRT_MIPI_BACKEND_LP_LUT_ENTRY_0_REG_IDX          32


/////////////////////////////////////////////////////////////////////////////////////////////////////
#define _HRT_MIPI_BACKEND_ENABLE_REG_WIDTH                 1  
#define _HRT_MIPI_BACKEND_STATUS_REG_WIDTH                 1  
//#define _HRT_MIPI_BACKEND_HIGH_PREC_REG_WIDTH              1
#define _HRT_MIPI_BACKEND_COMP_FORMAT_REG_WIDTH           32
#define _HRT_MIPI_BACKEND_RAW16_CONFIG_REG_WIDTH           7 
#define _HRT_MIPI_BACKEND_RAW18_CONFIG_REG_WIDTH           9
#define _HRT_MIPI_BACKEND_FORCE_RAW8_REG_WIDTH             8
#define _HRT_MIPI_BACKEND_IRQ_STATUS_REG_WIDTH            _HRT_MIPI_BACKEND_NOF_IRQS
#define _HRT_MIPI_BACKEND_IRQ_CLEAR_REG_WIDTH              0 
#define _HRT_MIPI_BACKEND_GLOBAL_LUT_DISREGARD_REG_WIDTH   1
#define _HRT_MIPI_BACKEND_PKT_STALL_STATUS_REG_WIDTH       1+2+6
//#define _HRT_MIPI_BACKEND_SP_LUT_ENABLE_REG_WIDTH          1
//#define _HRT_MIPI_BACKEND_SP_LUT_ENTRY_0_REG_WIDTH         7 
//#define _HRT_MIPI_BACKEND_SP_LUT_ENTRY_1_REG_WIDTH         7 
//#define _HRT_MIPI_BACKEND_SP_LUT_ENTRY_2_REG_WIDTH         7 
//#define _HRT_MIPI_BACKEND_SP_LUT_ENTRY_3_REG_WIDTH         7 

/////////////////////////////////////////////////////////////////////////////////////////////////////

#define _HRT_MIPI_BACKEND_NOF_SP_LUT_ENTRIES               4

//#define _HRT_MIPI_BACKEND_MAX_NOF_LP_LUT_ENTRIES           16  // to satisfy hss model static array declaration
 

#define _HRT_MIPI_BACKEND_CHANNEL_ID_WIDTH                 2
#define _HRT_MIPI_BACKEND_FORMAT_TYPE_WIDTH                6
#define _HRT_MIPI_BACKEND_PACKET_ID_WIDTH                  _HRT_MIPI_BACKEND_CHANNEL_ID_WIDTH + _HRT_MIPI_BACKEND_FORMAT_TYPE_WIDTH

#define _HRT_MIPI_BACKEND_STREAMING_PIX_A_LSB                 0
#define _HRT_MIPI_BACKEND_STREAMING_PIX_A_MSB(pix_width)     (_HRT_MIPI_BACKEND_STREAMING_PIX_A_LSB + (pix_width) - 1)
#define _HRT_MIPI_BACKEND_STREAMING_PIX_A_VAL_BIT(pix_width) (_HRT_MIPI_BACKEND_STREAMING_PIX_A_MSB(pix_width) + 1)
#define _HRT_MIPI_BACKEND_STREAMING_PIX_B_LSB(pix_width)     (_HRT_MIPI_BACKEND_STREAMING_PIX_A_VAL_BIT(pix_width) + 1)
#define _HRT_MIPI_BACKEND_STREAMING_PIX_B_MSB(pix_width)     (_HRT_MIPI_BACKEND_STREAMING_PIX_B_LSB(pix_width) + (pix_width) - 1)
#define _HRT_MIPI_BACKEND_STREAMING_PIX_B_VAL_BIT(pix_width) (_HRT_MIPI_BACKEND_STREAMING_PIX_B_MSB(pix_width) + 1)
#define _HRT_MIPI_BACKEND_STREAMING_SOP_BIT(pix_width)       (_HRT_MIPI_BACKEND_STREAMING_PIX_B_VAL_BIT(pix_width) + 1)
#define _HRT_MIPI_BACKEND_STREAMING_EOP_BIT(pix_width)       (_HRT_MIPI_BACKEND_STREAMING_SOP_BIT(pix_width) + 1)
#define _HRT_MIPI_BACKEND_STREAMING_WIDTH(pix_width)         (_HRT_MIPI_BACKEND_STREAMING_EOP_BIT(pix_width) + 1)

/*************************************************************************************************/
/* Custom Decoding                                                                               */
/* These Custom Defs are defined based on design-time config in "mipi_backend_pixel_formatter.chdl" !! */
/*************************************************************************************************/
#define _HRT_MIPI_BACKEND_CUST_EN_IDX                     0     /* 2bits */
#define _HRT_MIPI_BACKEND_CUST_EN_DATAID_IDX              2     /* 6bits MIPI DATA ID */ 
#define _HRT_MIPI_BACKEND_CUST_EN_HIGH_PREC_IDX           8     // 1 bit
#define _HRT_MIPI_BACKEND_CUST_EN_WIDTH                   9     
#define _HRT_MIPI_BACKEND_CUST_MODE_ALL                   1     /* Enable Custom Decoding for all DATA IDs */
#define _HRT_MIPI_BACKEND_CUST_MODE_ONE                   3     /* Enable Custom Decoding for ONE DATA ID, programmed in CUST_EN_DATA_ID */

#define _HRT_MIPI_BACKEND_CUST_EN_OPTION_IDX              1    

/* Data State config = {get_bits(6bits), valid(1bit)}  */
#define _HRT_MIPI_BACKEND_CUST_DATA_STATE_S0_IDX          0     /* 7bits */ 
#define _HRT_MIPI_BACKEND_CUST_DATA_STATE_S1_IDX          8     /* 7bits */ 
#define _HRT_MIPI_BACKEND_CUST_DATA_STATE_S2_IDX          16    /* was 14 7bits */
#define _HRT_MIPI_BACKEND_CUST_DATA_STATE_WIDTH           24    /* was 21*/
#define _HRT_MIPI_BACKEND_CUST_DATA_STATE_VALID_IDX       0     /* 1bits */
#define _HRT_MIPI_BACKEND_CUST_DATA_STATE_GETBITS_IDX     1     /* 6bits */

/* Pixel Extractor config */
#define _HRT_MIPI_BACKEND_CUST_PIX_EXT_DATA_ALIGN_IDX     0     /* 6bits */
#define _HRT_MIPI_BACKEND_CUST_PIX_EXT_PIX_ALIGN_IDX      6     /* 5bits */
#define _HRT_MIPI_BACKEND_CUST_PIX_EXT_PIX_MASK_IDX       11    /* was 10 18bits */
#define _HRT_MIPI_BACKEND_CUST_PIX_EXT_PIX_EN_IDX         29    /* was 28 1bits */

#define _HRT_MIPI_BACKEND_CUST_PIX_EXT_WIDTH              30    /* was 29 */

/* Pixel Valid & EoP config = {[eop,valid](especial), [eop,valid](normal)} */
#define _HRT_MIPI_BACKEND_CUST_PIX_VALID_EOP_P0_IDX        0    /* 4bits */
#define _HRT_MIPI_BACKEND_CUST_PIX_VALID_EOP_P1_IDX        4    /* 4bits */
#define _HRT_MIPI_BACKEND_CUST_PIX_VALID_EOP_P2_IDX        8    /* 4bits */
#define _HRT_MIPI_BACKEND_CUST_PIX_VALID_EOP_P3_IDX        12   /* 4bits */
#define _HRT_MIPI_BACKEND_CUST_PIX_VALID_EOP_WIDTH         16 
#define _HRT_MIPI_BACKEND_CUST_PIX_VALID_EOP_NOR_VALID_IDX 0    /* Normal (NO less get_bits case) Valid - 1bits */
#define _HRT_MIPI_BACKEND_CUST_PIX_VALID_EOP_NOR_EOP_IDX   1    /* Normal (NO less get_bits case) EoP - 1bits */
#define _HRT_MIPI_BACKEND_CUST_PIX_VALID_EOP_ESP_VALID_IDX 2    /* Especial (less get_bits case) Valid - 1bits */
#define _HRT_MIPI_BACKEND_CUST_PIX_VALID_EOP_ESP_EOP_IDX   3    /* Especial (less get_bits case) EoP - 1bits */

/*************************************************************************************************/
/* MIPI backend output streaming interface definition                                            */
/* These parameters define the fields within the streaming bus. These should also be used by the */
/* subsequent block, ie stream2mmio.                                                             */
/*************************************************************************************************/
/* The pipe backend - stream2mmio should be design time configurable in                          */
/*   PixWidth - Number of bits per pixel                                                         */
/*   PPC      - Pixel per Clocks                                                                 */
/*   NumSids  - Max number of source Ids (ifc's)  and derived from that:                         */
/*   SidWidth - Number of bits required for the sid parameter                                    */
/* In order to keep this configurability, below Macro's have these as a parameter                */
/*************************************************************************************************/

#define HRT_MIPI_BACKEND_STREAM_EOP_BIT                      0
#define HRT_MIPI_BACKEND_STREAM_SOP_BIT                      1
#define HRT_MIPI_BACKEND_STREAM_EOF_BIT                      2
#define HRT_MIPI_BACKEND_STREAM_SOF_BIT                      3
#define HRT_MIPI_BACKEND_STREAM_CHID_LS_BIT                  4
#define HRT_MIPI_BACKEND_STREAM_CHID_MS_BIT(sid_width)      (HRT_MIPI_BACKEND_STREAM_CHID_LS_BIT+(sid_width)-1)
#define HRT_MIPI_BACKEND_STREAM_PIX_VAL_BIT(sid_width,p)    (HRT_MIPI_BACKEND_STREAM_CHID_MS_BIT(sid_width)+1+p)

#define HRT_MIPI_BACKEND_STREAM_PIX_LS_BIT(sid_width,ppc,pix_width,p) (HRT_MIPI_BACKEND_STREAM_PIX_VAL_BIT(sid_width,ppc)+ ((pix_width)*p))
#define HRT_MIPI_BACKEND_STREAM_PIX_MS_BIT(sid_width,ppc,pix_width,p) (HRT_MIPI_BACKEND_STREAM_PIX_LS_BIT(sid_width,ppc,pix_width,p) + (pix_width) - 1)

#if 0
//#define HRT_MIPI_BACKEND_STREAM_PIX_BITS                    14
//#define HRT_MIPI_BACKEND_STREAM_CHID_BITS                    4
//#define HRT_MIPI_BACKEND_STREAM_PPC                          4
#endif

#define HRT_MIPI_BACKEND_STREAM_BITS(sid_width,ppc,pix_width)         (HRT_MIPI_BACKEND_STREAM_PIX_MS_BIT(sid_width,ppc,pix_width,(ppc-1))+1)


/* SP and LP LUT BIT POSITIONS */
#define HRT_MIPI_BACKEND_LUT_PKT_DISREGARD_BIT              0                                                                                           // 0    
#define HRT_MIPI_BACKEND_LUT_SID_LS_BIT                     HRT_MIPI_BACKEND_LUT_PKT_DISREGARD_BIT + 1                                                  // 1    
#define HRT_MIPI_BACKEND_LUT_SID_MS_BIT(sid_width)          (HRT_MIPI_BACKEND_LUT_SID_LS_BIT+(sid_width)-1)                                             // 1 + (4) - 1 = 4  
#define HRT_MIPI_BACKEND_LUT_MIPI_CH_ID_LS_BIT(sid_width)   HRT_MIPI_BACKEND_LUT_SID_MS_BIT(sid_width) + 1                                              // 5
#define HRT_MIPI_BACKEND_LUT_MIPI_CH_ID_MS_BIT(sid_width)   HRT_MIPI_BACKEND_LUT_MIPI_CH_ID_LS_BIT(sid_width) + _HRT_MIPI_BACKEND_CHANNEL_ID_WIDTH - 1  // 6
#define HRT_MIPI_BACKEND_LUT_MIPI_FMT_LS_BIT(sid_width)     HRT_MIPI_BACKEND_LUT_MIPI_CH_ID_MS_BIT(sid_width) + 1                                       // 7
#define HRT_MIPI_BACKEND_LUT_MIPI_FMT_MS_BIT(sid_width)     HRT_MIPI_BACKEND_LUT_MIPI_FMT_LS_BIT(sid_width) + _HRT_MIPI_BACKEND_FORMAT_TYPE_WIDTH - 1   // 12    

/* #define HRT_MIPI_BACKEND_SP_LUT_BITS(sid_width)             HRT_MIPI_BACKEND_LUT_MIPI_CH_ID_MS_BIT(sid_width) + 1                                       // 7          */

#define HRT_MIPI_BACKEND_SP_LUT_BITS(sid_width)             HRT_MIPI_BACKEND_LUT_SID_MS_BIT(sid_width) + 1  
#define HRT_MIPI_BACKEND_LP_LUT_BITS(sid_width)             HRT_MIPI_BACKEND_LUT_MIPI_FMT_MS_BIT(sid_width) + 1                                         // 13


// temp solution
//#define HRT_MIPI_BACKEND_STREAM_PIXA_VAL_BIT                HRT_MIPI_BACKEND_STREAM_CHID_MS_BIT  + 1                                    // 8                     
//#define HRT_MIPI_BACKEND_STREAM_PIXB_VAL_BIT                HRT_MIPI_BACKEND_STREAM_PIXA_VAL_BIT + 1                                    // 9
//#define HRT_MIPI_BACKEND_STREAM_PIXC_VAL_BIT                HRT_MIPI_BACKEND_STREAM_PIXB_VAL_BIT + 1                                    // 10
//#define HRT_MIPI_BACKEND_STREAM_PIXD_VAL_BIT                HRT_MIPI_BACKEND_STREAM_PIXC_VAL_BIT + 1                                    // 11
//#define HRT_MIPI_BACKEND_STREAM_PIXA_LS_BIT                 HRT_MIPI_BACKEND_STREAM_PIXD_VAL_BIT + 1                                    // 12
//#define HRT_MIPI_BACKEND_STREAM_PIXA_MS_BIT                 HRT_MIPI_BACKEND_STREAM_PIXA_LS_BIT  + HRT_MIPI_BACKEND_STREAM_PIX_BITS - 1 // 25
//#define HRT_MIPI_BACKEND_STREAM_PIXB_LS_BIT                 HRT_MIPI_BACKEND_STREAM_PIXA_MS_BIT + 1                                     // 26
//#define HRT_MIPI_BACKEND_STREAM_PIXB_MS_BIT                 HRT_MIPI_BACKEND_STREAM_PIXB_LS_BIT  + HRT_MIPI_BACKEND_STREAM_PIX_BITS - 1 // 39
//#define HRT_MIPI_BACKEND_STREAM_PIXC_LS_BIT                 HRT_MIPI_BACKEND_STREAM_PIXB_MS_BIT + 1                                     // 40
//#define HRT_MIPI_BACKEND_STREAM_PIXC_MS_BIT                 HRT_MIPI_BACKEND_STREAM_PIXC_LS_BIT  + HRT_MIPI_BACKEND_STREAM_PIX_BITS - 1 // 53
//#define HRT_MIPI_BACKEND_STREAM_PIXD_LS_BIT                 HRT_MIPI_BACKEND_STREAM_PIXC_MS_BIT + 1                                     // 54
//#define HRT_MIPI_BACKEND_STREAM_PIXD_MS_BIT                 HRT_MIPI_BACKEND_STREAM_PIXD_LS_BIT  + HRT_MIPI_BACKEND_STREAM_PIX_BITS - 1 // 67
 
// vc hidden in pixb data (passed as raw12 the pipe)
#define HRT_MIPI_BACKEND_STREAM_VC_LS_BIT(sid_width,ppc,pix_width)  HRT_MIPI_BACKEND_STREAM_PIX_LS_BIT(sid_width,ppc,pix_width,1) + 10  //HRT_MIPI_BACKEND_STREAM_PIXB_LS_BIT + 10 // 36 
#define HRT_MIPI_BACKEND_STREAM_VC_MS_BIT(sid_width,ppc,pix_width)  HRT_MIPI_BACKEND_STREAM_VC_LS_BIT(sid_width,ppc,pix_width) + 1    // 37




#endif /* _mipi_backend_defs_h */
