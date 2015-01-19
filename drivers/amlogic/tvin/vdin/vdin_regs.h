/*
 * VDIN register bit-field definition
 * Sorted by the appearing order of registers in am_regs.h.
 *
 * Author: Lin Xu <lin.xu@amlogic.com>
 *
 * Copyright (C) 2010 Amlogic Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */


#ifndef __VDIN_REGS_H
#define __VDIN_REGS_H
 
#if ((MESON_CPU_TYPE == MESON_CPU_TYPE_MESON6TV)|| \
     (MESON_CPU_TYPE == MESON_CPU_TYPE_MESON6TVD))
    #define VDIN_V1  //for m6tv
#elif ((MESON_CPU_TYPE == MESON_CPU_TYPE_MESON8)|| \
	(MESON_CPU_TYPE == MESON_CPU_TYPE_MESON8B))
    #define VDIN_V1  //for m6tv
    #define VDIN_V2  //for m8
#elif (MESON_CPU_TYPE > MESON_CPU_TYPE_MESON8B)
    #define VDIN_V1
    #define VDIN_V2
#endif
//#define VDIN_SCALE_COEF_IDX                        0x1200
//#define VDIN_SCALE_COEF                            0x1201

//#define VDIN_COM_CTRL0                             0x1202
/* used by other modules,indicates that MPEG input.
0: mpeg source to NR directly,
1: mpeg source pass through here */
#define MPEG_TO_VDIN_SEL_BIT            31
#define MPEG_TO_VDIN_SEL_WID            1
/* indicates MPEG field ID,written by software.
0: EVEN FIELD 1: ODD FIELD */
#define MPEG_FLD_BIT                    30
#define MPEG_FLD_WID                    1




#define MPEG_GO_FLD_EN_BIT              27     // enable external MPEG Go_field (VS)
#define MPEG_GO_FLD_EN_WID              1


/* vdin read enable after hold lines counting from delayed Go-field (VS). */
#define HOLD_LN_BIT                     20
#define HOLD_LN_WID                     7
#define DLY_GO_FLD_EN_BIT               19
#define DLY_GO_FLD_EN_WID               1
#define DLY_GO_FLD_LN_NUM_BIT           12
#define DLY_GO_FLD_LN_NUM_WID           7    // delay go field lines
/* 00: component0_in 01: component1_in 10: component2_in */
#define COMP2_OUT_SWT_BIT               10
#define COMP2_OUT_SWT_WID               2
/* 00: component0_in 01: component1_in 10: component2_in */
#define COMP1_OUT_SWT_BIT               8
#define COMP1_OUT_SWT_WID               2
/* 00: component0_in 01: component1_in 10: component2_in */
#define COMP0_OUT_SWT_BIT               6
#define COMP0_OUT_SWT_WID               2


#define INPUT_WIN_SEL_EN_BIT            5
#define INPUT_WIN_SEL_EN_WID            1


/* 0: no data input 1: common data input */
#define COMMON_DATA_IN_EN_BIT           4
#define COMMON_DATA_IN_EN_WID           1
/* 1: MPEG, 2: 656, 3: TVFE, 4: CVD2, 5: HDMI_Rx,6: DVIN otherwise: NULL
*7: loopback from VIU1, 8: MIPI csi2 in meson6
*/
#define VDIN_SEL_BIT                    0
#define VDIN_SEL_WID                    4

//#define VDIN_ACTIVE_MAX_PIX_CNT_STATUS             0x1203
/* ~field_hold & prehsc input active max pixel every line output of window */
#define ACTIVE_MAX_PIX_CNT_BIT          16
#define ACTIVE_MAX_PIX_CNT_WID          13
#define ACTIVE_MAX_PIX_CNT_SDW_BIT      0    // latch by go_field
#define ACTIVE_MAX_PIX_CNT_SDW_WID      13

//#define VDIN_LCNT_STATUS                           0x1204
/* line count by force_go_line |sel_go_line :output of decimate */
#define GO_LN_CNT_BIT                   16
#define GO_LN_CNT_WID                   13
/* line  count prehsc input active max pixel every active line output of window */
#define ACTIVE_LN_CNT_BIT               0
#define ACTIVE_LN_CNT_WID               13

//#define VDIN_COM_STATUS0                        0x1205                     S
#define LFIFO_BUF_CNT_BIT               3
#define LFIFO_BUF_CNT_WID               10   //wren + read -
#define DIRECT_DONE_STATUS_BIT          2
#define DIRECT_DONE_STATUS_WID          1    // direct_done_clr_bit & reg_wpluse
#define NR_DONE_STATUS_BIT              1
#define NR_DONE_STATUS_WID              1    // nr_done_clr_bit & reg_wpluse
#define VDIN_FLD_EVEN_BIT               0
#define VDIN_FLD_EVEN_WID               1

//#define VDIN_COM_STATUS1                        0x1206
#define FIFO4_OVFL_BIT                  31
#define FIFO4_OVFL_WID                  1
#define ASFIFO4_CNT_BIT                 24
#define ASFIFO4_CNT_WID                 6
#define FIFO3_OVFL_BIT                  23
#define FIFO3_OVFL_WID                  1
#define ASFIFO3_CNT_BIT                 16
#define ASFIFO3_CNT_WID                 6
#define FIFO2_OVFL_BIT                  15
#define FIFO2_OVFL_WID                  1
#define ASFIFO2_CNT_BIT                 8
#define ASFIFO2_CNT_WID                 6
#define FIFO1_OVFL_BIT                  7
#define FIFO1_OVFL_WID                  1
#define ASFIFO1_CNT_BIT                 0
#define ASFIFO1_CNT_WID                 6

//#define VDIN_LCNT_SHADOW_STATUS                 0x1207
#define GO_LN_CNT_SDW_BIT               16
#define GO_LN_CNT_SDW_WID               13   // latch by go_field
#define ACTIVE_LN_CNT_SDW_BIT           0
#define ACTIVE_LN_CNT_SDW_WID           13   // latch by go_field

//#define VDIN_ASFIFO_CTRL0                       0x1208
#define VDI2_ASFIFO_CTRL_BIT			16
#define VDI2_ASFIFO_CTRL_WID			8
#define ASFIFO2_DE_EN_BIT               23
#define ASFIFO2_DE_EN_WID               1
#define ASFIFO2_GO_FLD_EN_BIT           22
#define ASFIFO2_GO_FLD_EN_WID           1
#define ASFIFO2_GO_LN_EN_BIT            21
#define ASFIFO2_GO_LN_EN_WID            1
#define ASFIFO2_NEG_ACTIVE_IN_VS_BIT    20
#define ASFIFO2_NEG_ACTIVE_IN_VS_WID    1
#define ASFIFO2_NEG_ACTIVE_IN_HS_BIT    19
#define ASFIFO2_NEG_ACTIVE_IN_HS_WID    1
#define ASFIFO2_VS_SOFT_RST_FIFO_EN_BIT 18
#define ASFIFO2_VS_SOFT_RST_FIFO_EN_WID 1
#define ASFIFO2_OVFL_STATUS_CLR_BIT     17
#define ASFIFO2_OVFL_STATUS_CLR_WID     1
#define ASFIFO2_SOFT_RST_BIT            16
#define ASFIFO2_SOFT_RST_WID            1    // write 1 & then 0 to reset
#define VDI1_ASFIFO_CTRL_BIT			0
#define VDI1_ASFIFO_CTRL_WID			8
#define ASFIFO1_DE_EN_BIT               7
#define ASFIFO1_DE_EN_WID               1
#define ASFIFO1_GO_FLD_EN_BIT           6
#define ASFIFO1_GO_FLD_EN_WID           1
#define ASFIFO1_GO_LN_EN_BIT            5
#define ASFIFO1_GO_LN_EN_WID            1
#define ASFIFO1_NEG_ACTIVE_IN_VS_BIT    4
#define ASFIFO1_NEG_ACTIVE_IN_VS_WID    1
#define ASFIFO1_NEG_ACTIVE_IN_HS_BIT    3
#define ASFIFO1_NEG_ACTIVE_IN_HS_WID    1
#define ASFIFO1_VS_SOFT_RST_FIFO_EN_BIT 2
#define ASFIFO1_VS_SOFT_RST_FIFO_EN_WID 1
#define ASFIFO1_OVFL_STATUS_CLR_BIT     1
#define ASFIFO1_OVFL_STATUS_CLR_WID     1
#define ASFIFO1_SOFT_RST_BIT            0
#define ASFIFO1_SOFT_RST_WID            1    // write 1 & then 0 to reset

//#define VDIN_ASFIFO_CTRL1                         0x1209
#define VDI4_ASFIFO_CTRL_BIT			16
#define VDI4_ASFIFO_CTRL_WID			8
#define ASFIFO4_DE_EN_BIT               23
#define ASFIFO4_DE_EN_WID               1
#define ASFIFO4_GO_FLD_EN_BIT           22
#define ASFIFO4_GO_FLD_EN_WID           1
#define ASFIFO4_GO_LN_EN_BIT            21
#define ASFIFO4_GO_LN_EN_WID            1
#define ASFIFO4_NEG_ACTIVE_IN_VS_BIT    20
#define ASFIFO4_NEG_ACTIVE_IN_VS_WID    1
#define ASFIFO4_NEG_ACTIVE_IN_HS_BIT    19
#define ASFIFO4_NEG_ACTIVE_IN_HS_WID    1
#define ASFIFO4_VS_SOFT_RST_FIFO_EN_BIT 18
#define ASFIFO4_VS_SOFT_RST_FIFO_EN_WID 1
#define ASFIFO4_OVFL_STATUS_CLR_BIT     17
#define ASFIFO4_OVFL_STATUS_CLR_WID     1
#define ASFIFO4_SOFT_RST_BIT            16
#define ASFIFO4_SOFT_RST_WID            1    // write 1 & then 0 to reset
#define VDI3_ASFIFO_CTRL_BIT			0
#define VDI3_ASFIFO_CTRL_WID			8
#define ASFIFO3_DE_EN_BIT               7
#define ASFIFO3_DE_EN_WID               1
#define ASFIFO3_GO_FLD_EN_BIT           6
#define ASFIFO3_GO_FLD_EN_WID           1
#define ASFIFO3_GO_LN_EN_BIT            5
#define ASFIFO3_GO_LN_EN_WID            1
#define ASFIFO3_NEG_ACTIVE_IN_VS_BIT    4
#define ASFIFO3_NEG_ACTIVE_IN_VS_WID    1
#define ASFIFO3_NEG_ACTIVE_IN_HS_BIT    3
#define ASFIFO3_NEG_ACTIVE_IN_HS_WID    1
#define ASFIFO3_VS_SOFT_RST_FIFO_EN_BIT 2
#define ASFIFO3_VS_SOFT_RST_FIFO_EN_WID 1
#define ASFIFO3_OVFL_STATUS_CLR_BIT     1
#define ASFIFO3_OVFL_STATUS_CLR_WID     1
#define ASFIFO3_SOFT_RST_BIT            0
#define ASFIFO3_SOFT_RST_WID            1    // write 1 & then 0 to reset

//#define VDIN_WIDTHM1I_WIDTHM1O                  0x120a
#define WIDTHM1I_BIT                    16
#define WIDTHM1I_WID                    13
#define WIDTHM1O_BIT                    0
#define WIDTHM1O_WID                    13

//#define VDIN_SC_MISC_CTRL                       0x120b
#define INIT_PIX_IN_PTR_BIT             8
#define INIT_PIX_IN_PTR_WID             7    // signed value for short line output
#define INIT_PIX_IN_PTR_MSK             0x0000007f
#define PRE_HSCL_EN_BIT                 7
#define PRE_HSCL_EN_WID                 1    // pre-hscaler: 1/2 coarse scale down
#define HSCL_EN_BIT                     6
#define HSCL_EN_WID                     1    // hscaler: fine scale down
#define SHORT_LN_OUT_EN_BIT             5
#define SHORT_LN_OUT_EN_WID             1
/*when decimation timing located in between 2 input pixels, decimate the nearest one*/
#define HSCL_NEAREST_EN_BIT             4
#define HSCL_NEAREST_EN_WID             1
#define PHASE0_ALWAYS_EN_BIT            3    // Start decimation from phase 0 for each line
#define PHASE0_ALWAYS_EN_WID            1
/* filter pixel buf len (depth), max is 3 in IP design */
#define HSCL_BANK_LEN_BIT               0
#define HSCL_BANK_LEN_WID               3


//#define VDIN_HSC_PHASE_STEP                     0x120c
#define HSCL_PHASE_STEP_INT_BIT         24
#define HSCL_PHASE_STEP_INT_WID         5
#define HSCL_PHASE_STEP_FRA_BIT         0
#define HSCL_PHASE_STEP_FRA_WID         24

//#define VDIN_HSC_INI_CTRL                          0x120d
/* repeatedly decimation of pixel #0 of each line? */
#define HSCL_RPT_P0_NUM_BIT             29
#define HSCL_RPT_P0_NUM_WID             2
/* if rev>rpt_p0+1, then start decimation upon ini_phase? */
#define HSCL_INI_RCV_NUM_BIT            24
#define HSCL_INI_RCV_NUM_WID            5
/* which one every some pixels is decimated */
#define HSCL_INI_PHASE_BIT              0
#define HSCL_INI_PHASE_WID              24


//#define VDIN_COM_STATUS2                           0x120e
//Read only

#define VDI7_FIFO_OVFL_BIT              23  //vdi7 fifo overflow
#define VDI7_FIFO_OVFL_WID              1
#define VDI7_ASFIFO_CNT_BIT             16  //vdi7_asfifo_cnt
#define VDI7_ASFIFO_CNT_WID             6
#define VDI6_FIFO_OVFL_BIT              15  //vdi6 fifo overflow
#define VDI6_FIFO_OVFL_WID              1
#define VDI6_ASFIFO_CNT_BIT             8  //vdi6_asfifo_cnt
#define VDI6_ASFIFO_CNT_WID             6

#define VDI5_FIFO_OVFL_BIT              7  //vdi5 fifo overflow
#define VDI5_FIFO_OVFL_WID              1
#define VDI5_ASFIFO_CNT_BIT             0  //vdi5_asfifo_cnt
#define VDI5_ASFIFO_CNT_WID             6



//#define VDIN_ASFIFO_CTRL2                          0x120f
#define ASFIFO_DECIMATION_SYNC_WITH_DE_BIT        25
#define ASFIFO_DECIMATION_SYNC_WITH_DE_WID        1
#define ASFIFO_DECIMATION_DE_EN_BIT               24
#define ASFIFO_DECIMATION_DE_EN_WID               1
#define ASFIFO_DECIMATION_PHASE_BIT               20
#define ASFIFO_DECIMATION_PHASE_WID               4 // which counter value used to decimate
#define ASFIFO_DECIMATION_NUM_BIT                 16
#define ASFIFO_DECIMATION_NUM_WID                 4 // 0: not decimation, 1: decimation 2, 2: decimation 3 ...
#define VDI5_ASFIFO_CTRL_BIT					  0
#define VDI5_ASFIFO_CTRL_WID					  8
#define ASFIFO5_DE_EN_BIT                         7
#define ASFIFO5_DE_EN_WID                         1
#define ASFIFO5_GO_FLD_EN_BIT                     6
#define ASFIFO5_GO_FLD_EN_WID                     1
#define ASFIFO5_GO_LN_EN_BIT                      5
#define ASFIFO5_GO_LN_EN_WID                      1
#define ASFIFO5_NEG_ACTIVE_IN_VS_BIT              4
#define ASFIFO5_NEG_ACTIVE_IN_VS_WID              1
#define ASFIFO5_NEG_ACTIVE_IN_HS_BIT              3
#define ASFIFO5_NEG_ACTIVE_IN_HS_WID              1
#define ASFIFO5_VS_SOFT_RST_FIFO_EN_BIT           2
#define ASFIFO5_VS_SOFT_RST_FIFO_EN_WID           1
#define ASFIFO5_OVFL_STATUS_CLR_BIT               1
#define ASFIFO5_OVFL_STATUS_CLR_WID               1
#define ASFIFO5_SOFT_RST_BIT                      0
#define ASFIFO5_SOFT_RST_WID                      1 // write 1 & then 0 to reset


//#define VDIN_MATRIX_CTRL                        0x1210
#define VDIN_MATRIX0_BYPASS_BIT             9//1:bypass 0:pass
#define VDIN_MATRIX0_BYPASS_WID             1
#define VDIN_MATRIX1_BYPASS_BIT             8
#define VDIN_MATRIX1_BYPASS_WID             1
#define VDIN_HIGHLIGHT_EN_BIT               7
#define VDIN_HIGHLIGHT_EN_WID               1
#define VDIN_PROBE_POST_BIT                 6//1: probe pixel data after matrix, 0:probe pixel data before matrix
#define VDIN_PROBE_POST_WID                 1
#define VDIN_PROBE_SEL_BIT                  4//00: select matrix0, 01: select matrix1,otherwise select nothing
#define VDIN_PROBE_SEL_WID                  2
#define VDIN_MATRIX_COEF_INDEX_BIT          2//00: select mat0, 01: select mat1, otherwise slect nothing
#define VDIN_MATRIX_COEF_INDEX_WID          2
#define VDIN_MATRIX1_EN_BIT                 1//Bit 1   mat1 conversion matrix enable
#define VDIN_MATRIX1_EN_WID                 1
#define VDIN_MATRIX_EN_BIT                  0//Bit 0   mat0 conversion matrix enable
#define VDIN_MATRIX_EN_WID                  1

//#define VDIN_MATRIX_COEF00_01                   0x1211
#define MATRIX_C00_BIT                  16
#define MATRIX_C00_WID                  13   // s2.10
#define MATRIX_C01_BIT                  0
#define MATRIX_C01_WID                  13   // s2.10

//#define VDIN_MATRIX_COEF02_10                   0x1212
#define MATRIX_C02_BIT                  16
#define MATRIX_C02_WID                  13   // s2.10
#define MATRIX_C10_BIT                  0
#define MATRIX_C10_WID                  13   // s2.10

//#define VDIN_MATRIX_COEF11_12                   0x1213
#define MATRIX_C11_BIT                  16
#define MATRIX_C11_WID                  13   // s2.10
#define MATRIX_C12_BIT                  0
#define MATRIX_C12_WID                  13   // s2.10

//#define VDIN_MATRIX_COEF20_21                   0x1214
#define MATRIX_C20_BIT                  16
#define MATRIX_C20_WID                  13   // s2.10
#define MATRIX_C21_BIT                  0
#define MATRIX_C21_WID                  13   // s2.10

//#define VDIN_MATRIX_COEF22                      0x1215
#define MATRIX_C22_BIT                  0
#define MATRIX_C22_WID                  13   // s2.10

//#define VDIN_MATRIX_OFFSET0_1                   0x1216
#define MATRIX_OFFSET0_BIT              16
#define MATRIX_OFFSET0_WID              11   // s8.2
#define MATRIX_OFFSET1_BIT              0
#define MATRIX_OFFSET1_WID              11   // s8.2

//#define VDIN_MATRIX_OFFSET2                     0x1217
#define MATRIX_OFFSET2_BIT              0
#define MATRIX_OFFSET2_WID              11   // s8.2

//#define VDIN_MATRIX_PRE_OFFSET0_1               0x1218
#define MATRIX_PRE_OFFSET0_BIT          16
#define MATRIX_PRE_OFFSET0_WID          11   // s8.2
#define MATRIX_PRE_OFFSET1_BIT          0
#define MATRIX_PRE_OFFSET1_WID          11   // s8.2

//#define VDIN_MATRIX_PRE_OFFSET2                 0x1219
#define MATRIX_PRE_OFFSET2_BIT          0
#define MATRIX_PRE_OFFSET2_WID          11   // s8.2

//#define VDIN_LFIFO_CTRL                         0x121a
#define LFIFO_BUF_SIZE_BIT              0
#define LFIFO_BUF_SIZE_WID              12

//#define VDIN_COM_GCLK_CTRL                      0x121b
#define COM_GCLK_BLKBAR_BIT             14
#define COM_GCLK_BLKBAR_WID             2    // 00: auto, 01: off, 1x: on
#define COM_GCLK_HIST_BIT               12
#define COM_GCLK_HIST_WID               2    // 00: auto, 01: off, 1x: on
#define COM_GCLK_LFIFO_BIT              10
#define COM_GCLK_LFIFO_WID              2    // 00: auto, 01: off, 1x: on
#define COM_GCLK_MATRIX_BIT             8
#define COM_GCLK_MATRIX_WID             2    // 00: auto, 01: off, 1x: on
#define COM_GCLK_HSCL_BIT               6
#define COM_GCLK_HSCL_WID               2    // 00: auto, 01: off, 1x: on
#define COM_GCLK_PRE_HSCL_BIT           4
#define COM_GCLK_PRE_HSCL_WID           2    // 00: auto, 01: off, 1x: on
#define COM_GCLK_TOP_BIT                2
#define COM_GCLK_TOP_WID                2    // 00: auto, 01: off, 1x: on
/* Caution !!! never turn it off, otherwise no way to wake up VDIN unless power reset  */
#define COM_GCLK_REG_BIT                0
#define COM_GCLK_REG_WID                1    //  0: auto,  1: off. Caution !!!


//#define VDIN_INTF_WIDTHM1                        0x121c
#define VDIN_INTF_WIDTHM1_BIT           0
#define VDIN_INTF_WIDTHM1_WID           13 // before the cut window function, after the de decimation function


//#define VDIN_WR_CTRL2                           0x121f
#define DISCARD_BEF_LINE_FIFO_BIT                8//1: discard data before line fifo, 0: normal mode
#define DISCARD_BEF_LINE_FIFO_WID               1
#define WRITE_CHROMA_CANVAS_ADDR_BIT    0//Write chroma canvas address
#define WRITE_CHROMA_CANVAS_ADDR_WID   8

//#define VDIN_WR_CTRL                            0x1220

//Applicable only bit[13:12]=0 or 10.
//0: Output every even pixels' CbCr;
//1: Output every odd pixels' CbCr;
//10: Output an average value per even&odd pair of pixels;
//11: Output all CbCr. (This does NOT apply to bit[13:12]=0 -- 4:2:2 mode.)
#define HCONV_MODE_BIT                          30
#define HCONV_MODE_WID                          2
#define NO_CLOCK_GATE_BIT                       29// 1:disable vid_wr_mif clock gating function
#define NO_CLOCK_GATE_WID                      1
#define WR_RESPONSE_CNT_CLR_BIT         28
#define WR_RESPONSE_CNT_CLR_WID         1
#define EOL_SEL_BIT                     27
#define EOL_SEL_WID                     1
#define VCP_NR_EN_BIT                   26//ONLY VDIN0
#define VCP_NR_EN_WID                   1
#define VCP_WR_EN_BIT                   25//ONLY VDIN0
#define VCP_WR_EN_WID                   1
#define VCP_IN_EN_BIT                   24//ONLY VDIN0
#define VCP_IN_EN_WID                   1
//#define WR_OUT_CTRL_BIT                 24 ?
//#define WR_OUT_CTRL_WID                 8    //directly send out
#define FRAME_SOFT_RST_EN_BIT           23
#define FRAME_SOFT_RST_EN_WID           1
#define LFIFO_SOFT_RST_EN_BIT           22   // reset LFIFO on VS (Go_field)
#define LFIFO_SOFT_RST_EN_WID           1
#define DIRECT_DONE_CLR_BIT             21   // used by other modules
#define DIRECT_DONE_CLR_WID             1
#define NR_DONE_CLR_BIT                 20   // used by other modules
#define NR_DONE_CLR_WID                 1
#define SWAP_CBCR_BIT                      18//only [13:12]=10;0 output cbcr(nv12);1 output cbcr(nv21)
#define SWAP_CBCR_WID                      1
#define VCONV_MODE_BIT                     16//0: Output even lines' CbCr; 01: Output odd lines' CbCr;
                                                                    //10: Reserved; 11: Output all CbCr.
#define VCONV_MODE_WID                    2
// 0: 422;1: 444;10:Y to luma canvas cbcr to chroma canvas for NV12/21
#define WR_FMT_BIT                      12
#define WR_FMT_WID                     2
/* vdin_wr_canvas = vdin_wr_canvas_dbuf_en ? wr_canvas_shadow :wr_canvas;  */
#define WR_CANVAS_DOUBLE_BUF_EN_BIT            11   //shadow is latch by go_field
#define WR_CANVAS_DOUBLE_BUF_EN_WID            1
#define WR_REQ_URGENT_BIT               9
#define WR_REQ_URGENT_WID               1    // directly send out
#define WR_REQ_EN_BIT                   8
#define WR_REQ_EN_WID                   1    // directly send out
#define WR_CANVAS_BIT                   0
#define WR_CANVAS_WID                   8



//#define VDIN_WR_H_START_END                        0x1221

#define HORIZONTAL_REVERSE_BIT          29//if true horizontal reverse
#define HORIZONTAL_REVERSE_WID         1
#define WR_HSTART_BIT                   16
#define WR_HSTART_WID                   13   // directly send out
#define WR_HEND_BIT                     0
#define WR_HEND_WID                     13   // directly send out

//#define VDIN_WR_V_START_END                        0x1222

#define VERTICAL_REVERSE_BIT          29//if true vertical reverse
#define VERTICAL_REVERSE_WID         1
#define WR_VSTART_BIT                   16
#define WR_VSTART_WID                   13   // directly send out
#define WR_VEND_BIT                     0
#define WR_VEND_WID                     13  // directly send out

#if defined(VDIN_V1)
//#define VDIN_VSC_PHASE_STEP                       0x1223
#define INTERGER_PORTION_BIT            20
#define INTERGER_PORTION_WID           5
#define FRACTION_PORTION_BIT            0
#define FRACTION_PORTION_WID           20

//#define VDIN_VSC_INI_CTRL                             0x1224
#define VSC_EN_BIT                                  23
#define VSC_EN_WID                                 1
#define VSC_PHASE0_ALWAYS_EN_BIT      21//to be 1 when scale up
#define VSC_PHASE0_ALWAYS_EN_WID     1
#define INI_SKIP_LINE_NUM_BIT                  16
#define INI_SKIP_LINE_NUM_WID                 5
#define VSCALER_INI_PHASE_BIT                0
#define VSCALER_INI_PHASE_WID               16

//#define VDIN_SCIN_HEIGHTM1                          0x1225
//Bit 12:0, scaler input height minus 1
#define SCALER_INPUT_HEIGHT_BIT            0
#define SCALER_INPUT_HEIGHT_WID           12

//#define `define VDIN_DUMMY_DATA                0x1226
#define DUMMY_COMPONENT0_BIT                16
#define DUMMY_COMPONENT0_WID               8
#define DUMMY_COMPONENT1_BIT                8
#define DUMMY_COMPONENT1_WID               8
#define DUMMY_COMPONENT2_BIT                0
#define DUMMY_COMPONENT2_WID               8

//#define VDIN_MATRIX_PROBE_COLOR           0x1228
//Read only
#define COMPONENT0_PROBE_COLOR_BIT                20
#define COMPONENT0_PROBE_COLOR_WID                10
#define COMPONENT1_PROBE_COLOR_BIT                10
#define COMPONENT1_PROBE_COLOR_WID                10
#define COMPONENT2_PROBE_COLOR_BIT                0
#define COMPONENT2_PROBE_COLOR_WID                10

//#define VDIN_MATRIX_HL_COLOR                0x1229
#define COMPONENT0_HL_COLOR_BIT                   16
#define COMPONENT0_HL_COLOR_WID                   8
#define COMPONENT1_HL_COLOR_BIT                   8
#define COMPONENT1_HL_COLOR_WID                   8
#define COMPONENT2_HL_COLOR_BIT                   0
#define COMPONENT2_HL_COLOR_WID                   8

//#define VDIN_MATRIX_PROBE_POS               0x122a
#define PROBE_POS_X_BIT                           16
#define PROBE_POS_X_WID                           13
#define PROBE_POX_Y_BIT                           0
#define PROBE_POX_Y_WID                           13
//#define VDIN_HIST_CTRL                             0x1230
//Bit 10:9  ldim_stts_din_sel, 00: from matrix0 dout,  01: from vsc_dout, 10: from matrix1 dout, 11: form matrix1 din
#define LDIM_STTS_DIN_SEL_BIT                     9
#define LDIM_STTS_DIN_SEL_WID                     2
#define LDIM_STTS_EN_BIT                          8
#define LDIM_STTS_EN_WID                          1
//00: from matrix0 dout,  01: from vsc_dout, 10: from matrix1 dout, 11: form matrix1 din
#define HIST_DIN_SEL_BIT                          2
#define HIST_DIN_SEL_WID                          2

#endif

#if defined(VDIN_V2)

//#define VDIN_CHROMA_ADDR_PORT 	      0x122b

//#define VDIN_CHROMA_DATA_PORT 	      0x122c

//#define VDIN_CM_BRI_CON_CTRL 		      0x122d
#define CM_TOP_EN_BIT				  28
#define CM_TOP_EN_WID				  1
#define BRI_CON_EN_BIT				  27
#define BRI_CON_EN_WID				  1
#define SED_YUVINVEN_BIT			  24
#define SED_YUVINVEN_WID			  3
#define REG_ADJ_BRI_BIT				  12
#define REG_ADJ_BRI_WID				  11
#define REG_ADJ_CON_BIT				  0
#define REG_ADJ_CON_WID				  12

//#define VDIN_GO_LINE_CTRL 		     0x122f
#define CLK_CYC_CNT_CLR_BIT                       17
#define CLK_CYC_CNT_CLR_WID                       1
//Bit 17  clk_cyc_cnt_clr, if true, clear this register
#define LINE_CNT_SRC_SEL_BIT                      16
#define LINE_CNT_SRC_SEL_WID                      1
//Bit 16 if true, use vpu clock to count one line, otherwise use actually hsync to count line_cnt
//Bit 15:0   line width using vpu clk
#define LINE_WID_USING_VPU_CLK_BIT                0
#define LINE_WID_USING_VPU_CLK_WID                16

#endif

//#define VDIN_HIST_CTRL                             0x1230
/* the total pixels = VDIN_HISTXX*(2^(VDIN_HIST_POW+3)) */
#define HIST_POW_BIT                    5
#define HIST_POW_WID                    2
/* Histgram range: 0: full picture, 1: histgram window defined by VDIN_HIST_H_START_END & VDIN_HIST_V_START_END */
#define HIST_WIN_EN_BIT                 1
#define HIST_WIN_EN_WID                 1
/* Histgram readback: 0: disable, 1: enable */
#define HIST_RD_EN_BIT                  0
#define HIST_RD_EN_WID                  1

//#define VDIN_HIST_H_START_END                   0x1231
#define HIST_HSTART_BIT                 16
#define HIST_HSTART_WID                 13
#define HIST_HEND_BIT                   0
#define HIST_HEND_WID                   13

//#define VDIN_HIST_V_START_END                   0x1232
#define HIST_VSTART_BIT                 16
#define HIST_VSTART_WID                 13
#define HIST_VEND_BIT                   0
#define HIST_VEND_WID                   13

//#define VDIN_HIST_MAX_MIN                       0x1233
#define HIST_MAX_BIT                    8
#define HIST_MAX_WID                    8
#define HIST_MIN_BIT                    0
#define HIST_MIN_WID                    8

//#define VDIN_HIST_SPL_VAL                       0x1234
#define HIST_LUMA_SUM_BIT               0
#define HIST_LUMA_SUM_WID               32

//#define VDIN_HIST_SPL_PIX_CNT                   0x1235
#define HIST_PIX_CNT_BIT                0
#define HIST_PIX_CNT_WID                22   // the total calculated pixels

//#define VDIN_HIST_CHROMA_SUM                    0x1236
#define HIST_CHROMA_SUM_BIT             0
#define HIST_CHROMA_SUM_WID             32   // the total chroma value

//#define VDIN_DNLP_HIST00                        0x1237
#define HIST_ON_BIN_01_BIT              16
#define HIST_ON_BIN_01_WID              16
#define HIST_ON_BIN_00_BIT              0
#define HIST_ON_BIN_00_WID              16

//#define VDIN_DNLP_HIST01                        0x1238
#define HIST_ON_BIN_03_BIT              16
#define HIST_ON_BIN_03_WID              16
#define HIST_ON_BIN_02_BIT              0
#define HIST_ON_BIN_02_WID              16

//#define VDIN_DNLP_HIST02                        0x1239
#define HIST_ON_BIN_05_BIT              16
#define HIST_ON_BIN_05_WID              16
#define HIST_ON_BIN_04_BIT              0
#define HIST_ON_BIN_04_WID              16

//#define VDIN_DNLP_HIST03                        0x123a
#define HIST_ON_BIN_07_BIT              16
#define HIST_ON_BIN_07_WID              16
#define HIST_ON_BIN_06_BIT              0
#define HIST_ON_BIN_06_WID              16

//#define VDIN_DNLP_HIST04                        0x123b
#define HIST_ON_BIN_09_BIT              16
#define HIST_ON_BIN_09_WID              16
#define HIST_ON_BIN_08_BIT              0
#define HIST_ON_BIN_08_WID              16

//#define VDIN_DNLP_HIST05                        0x123c
#define HIST_ON_BIN_11_BIT              16
#define HIST_ON_BIN_11_WID              16
#define HIST_ON_BIN_10_BIT              0
#define HIST_ON_BIN_10_WID              16

//#define VDIN_DNLP_HIST06                        0x123d
#define HIST_ON_BIN_13_BIT              16
#define HIST_ON_BIN_13_WID              16
#define HIST_ON_BIN_12_BIT              0
#define HIST_ON_BIN_12_WID              16

//#define VDIN_DNLP_HIST07                        0x123e
#define HIST_ON_BIN_15_BIT              16
#define HIST_ON_BIN_15_WID              16
#define HIST_ON_BIN_14_BIT              0
#define HIST_ON_BIN_14_WID              16

//#define VDIN_DNLP_HIST08                        0x123f
#define HIST_ON_BIN_17_BIT              16
#define HIST_ON_BIN_17_WID              16
#define HIST_ON_BIN_16_BIT              0
#define HIST_ON_BIN_16_WID              16

//#define VDIN_DNLP_HIST09                        0x1240
#define HIST_ON_BIN_19_BIT              16
#define HIST_ON_BIN_19_WID              16
#define HIST_ON_BIN_18_BIT              0
#define HIST_ON_BIN_18_WID              16

//#define VDIN_DNLP_HIST10                        0x1241
#define HIST_ON_BIN_21_BIT              16
#define HIST_ON_BIN_21_WID              16
#define HIST_ON_BIN_20_BIT              0
#define HIST_ON_BIN_20_WID              16

//#define VDIN_DNLP_HIST11                        0x1242
#define HIST_ON_BIN_23_BIT              16
#define HIST_ON_BIN_23_WID              16
#define HIST_ON_BIN_22_BIT              0
#define HIST_ON_BIN_22_WID              16

//#define VDIN_DNLP_HIST12                        0x1243
#define HIST_ON_BIN_25_BIT              16
#define HIST_ON_BIN_25_WID              16
#define HIST_ON_BIN_24_BIT              0
#define HIST_ON_BIN_24_WID              16

//#define VDIN_DNLP_HIST13                        0x1244
#define HIST_ON_BIN_27_BIT              16
#define HIST_ON_BIN_27_WID              16
#define HIST_ON_BIN_26_BIT              0
#define HIST_ON_BIN_26_WID              16

//#define VDIN_DNLP_HIST14                        0x1245
#define HIST_ON_BIN_29_BIT              16
#define HIST_ON_BIN_29_WID              16
#define HIST_ON_BIN_28_BIT              0
#define HIST_ON_BIN_28_WID              16

//#define VDIN_DNLP_HIST15                        0x1246
#define HIST_ON_BIN_31_BIT              16
#define HIST_ON_BIN_31_WID              16
#define HIST_ON_BIN_30_BIT              0
#define HIST_ON_BIN_30_WID              16

//#define VDIN_DNLP_HIST16                        0x1247
#define HIST_ON_BIN_33_BIT              16
#define HIST_ON_BIN_33_WID              16
#define HIST_ON_BIN_32_BIT              0
#define HIST_ON_BIN_32_WID              16

//#define VDIN_DNLP_HIST17                        0x1248
#define HIST_ON_BIN_35_BIT              16
#define HIST_ON_BIN_35_WID              16
#define HIST_ON_BIN_34_BIT              0
#define HIST_ON_BIN_34_WID              16

//#define VDIN_DNLP_HIST18                        0x1249
#define HIST_ON_BIN_37_BIT              16
#define HIST_ON_BIN_37_WID              16
#define HIST_ON_BIN_36_BIT              0
#define HIST_ON_BIN_36_WID              16

//#define VDIN_DNLP_HIST19                        0x124a
#define HIST_ON_BIN_39_BIT              16
#define HIST_ON_BIN_39_WID              16
#define HIST_ON_BIN_38_BIT              0
#define HIST_ON_BIN_38_WID              16

//#define VDIN_DNLP_HIST20                        0x124b
#define HIST_ON_BIN_41_BIT              16
#define HIST_ON_BIN_41_WID              16
#define HIST_ON_BIN_40_BIT              0
#define HIST_ON_BIN_40_WID              16

//#define VDIN_DNLP_HIST21                        0x124c
#define HIST_ON_BIN_43_BIT              16
#define HIST_ON_BIN_43_WID              16
#define HIST_ON_BIN_42_BIT              0
#define HIST_ON_BIN_42_WID              16

//#define VDIN_DNLP_HIST22                        0x124d
#define HIST_ON_BIN_45_BIT              16
#define HIST_ON_BIN_45_WID              16
#define HIST_ON_BIN_44_BIT              0
#define HIST_ON_BIN_44_WID              16

//#define VDIN_DNLP_HIST23                        0x124e
#define HIST_ON_BIN_47_BIT              16
#define HIST_ON_BIN_47_WID              16
#define HIST_ON_BIN_46_BIT              0
#define HIST_ON_BIN_46_WID              16

//#define VDIN_DNLP_HIST24                        0x124f
#define HIST_ON_BIN_49_BIT              16
#define HIST_ON_BIN_49_WID              16
#define HIST_ON_BIN_48_BIT              0
#define HIST_ON_BIN_48_WID              16

//#define VDIN_DNLP_HIST25                        0x1250
#define HIST_ON_BIN_51_BIT              16
#define HIST_ON_BIN_51_WID              16
#define HIST_ON_BIN_50_BIT              0
#define HIST_ON_BIN_50_WID              16

//#define VDIN_DNLP_HIST26                        0x1251
#define HIST_ON_BIN_53_BIT              16
#define HIST_ON_BIN_53_WID              16
#define HIST_ON_BIN_52_BIT              0
#define HIST_ON_BIN_52_WID              16

//#define VDIN_DNLP_HIST27                        0x1252
#define HIST_ON_BIN_55_BIT              16
#define HIST_ON_BIN_55_WID              16
#define HIST_ON_BIN_54_BIT              0
#define HIST_ON_BIN_54_WID              16

//#define VDIN_DNLP_HIST28                        0x1253
#define HIST_ON_BIN_57_BIT              16
#define HIST_ON_BIN_57_WID              16
#define HIST_ON_BIN_56_BIT              0
#define HIST_ON_BIN_56_WID              16

//#define VDIN_DNLP_HIST29                        0x1254
#define HIST_ON_BIN_59_BIT              16
#define HIST_ON_BIN_59_WID              16
#define HIST_ON_BIN_58_BIT              0
#define HIST_ON_BIN_58_WID              16

//#define VDIN_DNLP_HIST30                        0x1255
#define HIST_ON_BIN_61_BIT              16
#define HIST_ON_BIN_61_WID              16
#define HIST_ON_BIN_60_BIT              0
#define HIST_ON_BIN_60_WID              16

//#define VDIN_DNLP_HIST31                        0x1256
#define HIST_ON_BIN_63_BIT              16
#define HIST_ON_BIN_63_WID              16
#define HIST_ON_BIN_62_BIT              0
#define HIST_ON_BIN_62_WID              16

#if defined(VDIN_V1)
//#define VDIN_LDIM_STTS_HIST_REGION_IDX       0x1257
#define LOCAL_DIM_STATISTIC_EN_BIT          31
#define LOCAL_DIM_STATISTIC_EN_WID         1
#define EOL_EN_BIT                                          28
#define EOL_EN_WID                                        1
#define VLINE_OVERLAP_NUMBER_BIT        25
#define VLINE_OVERLAP_NUMBER_WID       3
//0: 17 pix, 1: 9 pix, 2: 5 pix, 3: 3 pix, 4: 0 pix
#define HLINE_OVERLAP_NUMBER_BIT        22
#define HLINE_OVERLAP_NUMBER_WID       3
#define LPF_BEFORE_STATISTIC_EN_BIT    20
#define LPF_BEFORE_STATISTIC_EN_WID    1
// region H/V position index, refer to VDIN_LDIM_STTS_HIST_SET_REGION
#define BLK_HV_POS_IDXS_BIT                     16
#define BLK_HV_POS_IDXS_WID                    4
#define REGION_RD_INDEX_INC_BIT             15
#define REGION_RD_INDEX_INC_WID            1
#define REGION_RD_INDEX_BIT                      0
#define REGION_RD_INDEX_WID                     7

//# VDIN_LDIM_STTS_HIST_SET_REGION                    0x1258
//Bit 28:0, if VDIN_LDIM_STTS_HIST_REGION_IDX[19:16] == 5'h0: read/write hvstart0
//if VDIN_LDIM_STTS_HIST_REGION_IDX[19:16] == 5'h1: read/write hend01
//if VDIN_LDIM_STTS_HIST_REGION_IDX[19:16] == 5'h2: read/write vend01
//if VDIN_LDIM_STTS_HIST_REGION_IDX[19:16] == 5'h3: read/write hend23
//if VDIN_LDIM_STTS_HIST_REGION_IDX[19:16] == 5'h4: read/write vend23
//if VDIN_LDIM_STTS_HIST_REGION_IDX[19:16] == 5'h5: read/write hend45
//if VDIN_LDIM_STTS_HIST_REGION_IDX[19:16] == 5'h6: read/write vend45
//if VDIN_LDIM_STTS_HIST_REGION_IDX[19:16] == 5'd7: read/write hend67
//if VDIN_LDIM_STTS_HIST_REGION_IDX[19:16] == 5'h8: read/write vend67
//if VDIN_LDIM_STTS_HIST_REGION_IDX[19:16] == 5'h9: read/write hend89
//if VDIN_LDIM_STTS_HIST_REGION_IDX[19:16] == 5'ha: read/write vend89
//hvstart0, Bit 28:16 row0 vstart, Bit 12:0 col0 hstart
//hend01, Bit 28:16 col1 hend, Bit 12:0 col0 hend
//vend01, Bit 28:16 row1 vend, Bit 12:0 row0 vend
//hend23, Bit 28:16 col3 hend, Bit 12:0 col2 hend
//vend23, Bit 28:16 row3 vend, Bit 12:0 row2 vend
//hend45, Bit 28:16 col5 hend, Bit 12:0 col4 hend
//vend45, Bit 28:16 row5 vend, Bit 12:0 row4 vend
//hend67, Bit 28:16 col7 hend, Bit 12:0 col6 hend
//vend67, Bit 28:16 row7 vend, Bit 12:0 row6 vend
//hend89, Bit 28:16 col9 hend, Bit 12:0 col8 hend
//vend89, Bit 28:16 row9 vend, Bit 12:0 row8 vend
#define HI_16_28_BIT                        16
#define HI_16_28_WID                       13
#define LOW_0_12_BIT                      0
#define LOW_0_12_WID                    13

//#define VDIN_LDIM_STTS_HIST_READ_REGION           0x1259
//REGION STATISTIC DATA READ OUT PORT,
#define MAX_COMP2_BIT                   20
#define MAX_COMP2_WID                  10
#define MAX_COMP1_BIT                   10
#define MAX_COMP1_WID                  10
#define MAX_COMP0_BIT                   0
#define MAX_COMP0_WID                  10

#endif


//#define VDIN_MEAS_CTRL0                            0x125a
#define MEAS_RST_BIT                    18 // write 1 & then 0 to reset
#define MEAS_RST_WID                    1
#define MEAS_WIDEN_HS_VS_EN_BIT         17 //make hs ,vs at lest 12 pulse wide
#define MEAS_WIDEN_HS_VS_EN_WID         1
#define MEAS_VS_TOTAL_CNT_EN_BIT        16 // vsync total counter always accumulating enable
#define MEAS_VS_TOTAL_CNT_EN_WID        1
#define MEAS_HS_VS_SEL_BIT              12 // 0: null, 1: vdi1, 2: vdi2, 3: vdi3, 4:vdi4, 5:vdi5,for m6 6:vdi6,7:vdi7 8:vdi8-isp
#define MEAS_HS_VS_SEL_WID              4
#define MEAS_VS_SPAN_BIT                4  // define how many VS span need to measure
#define MEAS_VS_SPAN_WID                8
#define MEAS_HS_INDEX_BIT               0  // select which HS counter/range
#define MEAS_HS_INDEX_WID               3



//#define VDIN_MEAS_VS_COUNT_HI                      0x125b // read only
#define MEAS_IND_VS_TOTAL_CNT_N_BIT     16 // after every VDIN_MEAS_VS_SPAN number of VS pulses, VDIN_MEAS_IND_TOTAL_COUNT_N++
#define MEAS_IND_VS_TOTAL_CNT_N_WID     4
#define MEAS_VS_TOTAL_CNT_HI_BIT        0  // vsync_total_counter[47:32]
#define MEAS_VS_TOTAL_CNT_HI_WID        16



//#define VDIN_MEAS_VS_COUNT_LO                      0x125c // read only
#define MEAS_VS_TOTAL_CNT_LO_BIT        0  // vsync_total_counter[31:0]
#define MEAS_VS_TOTAL_CNT_LO_WID        32



//#define VDIN_MEAS_HS_RANGE                         0x125d // 1st/2nd/3rd/4th hs range according to VDIN_MEAS_HS_INDEX
#define MEAS_HS_RANGE_CNT_START_BIT     16
#define MEAS_HS_RANGE_CNT_START_WID     13
#define MEAS_HS_RANGE_CNT_END_BIT       0
#define MEAS_HS_RANGE_CNT_END_WID       13



//#define VDIN_MEAS_HS_COUNT                         0x125e // read only
#define MEAS_HS_CNT_BIT                 0 // hs count as per 1st/2nd/3rd/4th hs range according to VDIN_MEAS_HS_INDEX
#define MEAS_HS_CNT_WID                 24



//#define VDIN_BLKBAR_CTRL1                          0x125f
#define BLKBAR_WHITE_EN_BIT             8
#define BLKBAR_WHITE_EN_WID             1
#define BLKBAR_WHITE_LVL_BIT            0
#define BLKBAR_WHITE_LVL_WID            8


//#define VDIN_BLKBAR_CTRL0                       0x1260



#define BLKBAR_BLK_LVL_BIT              24
#define BLKBAR_BLK_LVL_WID              8   // threshold to judge a black point


#define BLKBAR_H_WIDTH_BIT              8
#define BLKBAR_H_WIDTH_WID              13   // left and right region width
/* select yin or uin or vin to be the valid input */
#define BLKBAR_COMP_SEL_BIT             5
#define BLKBAR_COMP_SEL_WID             3
/* sw statistic of black pixels of each block,
1: search once, 0: search continuously till the exact edge */
#define BLKBAR_SW_STAT_EN_BIT           4
#define BLKBAR_SW_STAT_EN_WID           1
#define BLKBAR_DET_SOFT_RST_N_BIT       3
#define BLKBAR_DET_SOFT_RST_N_WID       1    // write 0 & then 1 to reset
/* 0: matrix_dout, 1: hscaler_dout, 2/3: pre-hscaler_din */
#define BLKBAR_DIN_SEL_BIT              1
#define BLKBAR_DIN_SEL_WID              2
/* blkbar_din_srdy blkbar_din_rrdy  enable */
#define BLKBAR_DET_TOP_EN_BIT           0
#define BLKBAR_DET_TOP_EN_WID           1

//#define VDIN_BLKBAR_H_START_END                    0x1261
#define BLKBAR_HSTART_BIT               16
#define BLKBAR_HSTART_WID               13   // Left region start
#define BLKBAR_HEND_BIT                 0
#define BLKBAR_HEND_WID                 13   // Right region end

//#define VDIN_BLKBAR_V_START_END                    0x1262
#define BLKBAR_VSTART_BIT               16
#define BLKBAR_VSTART_WID               13
#define BLKBAR_VEND_BIT                 0
#define BLKBAR_VEND_WID                 13

//#define VDIN_BLKBAR_CNT_THRESHOLD                  0x1263
/* black pixel number threshold to judge whether a block is totally black */
#define BLKBAR_CNT_TH_BIT               0
#define BLKBAR_CNT_TH_WID               20

//#define VDIN_BLKBAR_ROW_TH1_TH2                    0x1264
/* white pixel number threshold of black line on top */
#define BLKBAR_ROW_TH1_BIT              16
#define BLKBAR_ROW_TH1_WID              13
/* white pixel number threshold of black line on bottom */
#define BLKBAR_ROW_TH2_BIT              0
#define BLKBAR_ROW_TH2_WID              13

//#define VDIN_BLKBAR_IND_LEFT_START_END             0x1265
#define BLKBAR_LEFT_HSTART_BIT          16
#define BLKBAR_LEFT_HSTART_WID          13
#define BLKBAR_LEFT_HEND_BIT            0
#define BLKBAR_LEFT_HEND_WID            13

//#define VDIN_BLKBAR_IND_RIGHT_START_END            0x1266
#define BLKBAR_RIGHT_HSTART_BIT         16
#define BLKBAR_RIGHT_HSTART_WID         13
#define BLKBAR_RIGHT_HEND_BIT           0
#define BLKBAR_RIGHT_HEND_WID           13

//#define VDIN_BLKBAR_IND_LEFT1_CNT                  0x1267
/* Black pixels at left part of the left region */
#define BLKBAR_LEFT1_CNT_BIT            0
#define BLKBAR_LEFT1_CNT_WID            20

//#define VDIN_BLKBAR_IND_LEFT2_CNT                  0x1268
/* Black pixels at right part of the left region */
#define BLKBAR_LEFT2_CNT_BIT            0
#define BLKBAR_LEFT2_CNT_WID            20

//#define VDIN_BLKBAR_IND_RIGHT1_CNT                 0x1269
/* Black pixels at right part of the left region */
#define BLKBAR_RIGHT1_CNT_BIT           0
#define BLKBAR_RIGHT1_CNT_WID           20

//#define VDIN_BLKBAR_IND_RIGHT2_CNT                 0x126a
/* Black pixels at right part of the right region */
#define BLKBAR_RIGHT2_CNT_BIT           0
#define BLKBAR_RIGHT2_CNT_WID           20

//#define VDIN_BLKBAR_STATUS0                        0x126b
/* LEFT/RIGHT Black Bar detection done */
#define BLKBAR_DET_DONE_BIT             29
#define BLKBAR_DET_DONE_WID             1
#define BLKBAR_TOP_POS_BIT              16
#define BLKBAR_TOP_POS_WID              13
#define BLKBAR_BTM_POS_BIT              0
#define BLKBAR_BTM_POS_WID              13

//#define VDIN_BLKBAR_STATUS1                        0x126c
#define BLKBAR_LEFT_POS_BIT             16
#define BLKBAR_LEFT_POS_WID             13
#define BLKBAR_RIGHT_POS_BIT            0
#define BLKBAR_RIGHT_POS_WID            13


//#define VDIN_WIN_H_START_END                       0x126d
#define INPUT_WIN_H_START_BIT            16
#define INPUT_WIN_H_START_WID            13
#define INPUT_WIN_H_END_BIT              0
#define INPUT_WIN_H_END_WID              13



//#define VDIN_WIN_V_START_END                       0x126e
#define INPUT_WIN_V_START_BIT            16
#define INPUT_WIN_V_START_WID            13
#define INPUT_WIN_V_END_BIT              0
#define INPUT_WIN_V_END_WID              13


//Bit 15:8 vdi7 asfifo_ctrl
//Bit 7:0 vdi6 asfifo_ctrl
//#define VDIN_ASFIFO_CTRL3                                 0x126f
#define VDI8_ASFIFO_CTRL_BIT		16
#define VDI8_ASFIFO_CTRL_WID            8
#define VDI7_ASFIFO_CTRL_BIT            8
#define VDI7_ASFIFO_CTRL_WID            8
#define VDI6_ASFIFO_CTRL_BIT            0
#define VDI6_ASFIFO_CTRL_WID            8

#endif // __VDIN_REGS_H
