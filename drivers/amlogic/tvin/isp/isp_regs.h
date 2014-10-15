/*
 * ISP register bit-field definition
 * Sorted by the appearing order of registers in register.h.
 *
 * Author: bai kele <kele.bai@amlogic.com>
 *
 * Copyright (C) 2010 Amlogic Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __ISP_REGS_H
#define __ISP_REGS_H
// ----------------------------
// ISP_FLASH_LED_CTRL
// ----------------------------
#define HHI_ISP_LED_CLK_CNTL                       0x1098
//bit 31, reg_led_en //rising pulse start, falling pulse stop for torch mode
//bit 30, reg_inc_i_st_lat //for as3685, dynamic increase current during st_latch
//bit 29:28, reg_inv_en_pol //bit[29], invert en1, bit [28], invert en2
//bit 27, reg_switch_en1en2  //switch output en1 and en2. For IS3231: en1 is EN, en2 is mode. 
//bit 26, reg_force_off_mode //1: reset state machine at the falling edge no matter which state it is in
//bit 25, reg_hold_nonstd_off_mode //hold the non-std led_off signal input before state "OFF"
//bit 24, reg_flash_mode_timeout_en  //force to exit the Tlat state when time out, for flash mode protection
//bit 23, reg_en1_st_ini_level //en1 level during state ST_INI
//bit 22, reg_en2_st_set_mp_level //en2 level during state ST_SET_MP_HI, ST_SET_MP_LO
//bit 21, reg_en1_st_off_level //en1 level during state ST_OFF
//bit 20, reg_en2_st_off_level //en2 level during state ST_OFF
//bit 19:12, reg_en1_mp_num  //en1 multi pulse number, up to 256 step current control
//bit 10:0, reg_t_st_ini //Max: 85.2 us under 24M clock input
#define ISP_LED_CTRL                        0x2198
//bit 31:21, reg_t_en1_inc_i_st_lat_cnt //Max: 85.2us under 24M. for as3685, dynamic increase current during st_latch
//bit 20:10, reg_t_en2_lo_st_ini //Max: 85.2 us under 24M clock input
//bit 9:0, reg_t_en2_hi1_st_lat_cnt //42.62us @24M, EN2 can output hi-lo-hi during ST_LATCH, this is the first lo -duration
#define ISP_LED_TIMING1                     0x2199 
//bit 31:21, reg_t_en1_mp_hi_cnt //Max: 85.2 us under 24M clock input, mp means multi pulse
//bit 20:10, reg_t_en1_mp_lo_cnt //Max: 85.2 us under 24M clock input, mp means multi pulse
//bit 9:0, reg_t_en2_lo1_st_lat_cnt //42.62us @24M, EN2 can output hi-lo-hi during ST_LATCH, this is the first lo -duration
#define ISP_LED_TIMING2                     0x219a 
//bit 30:28, RO state
//bit 25:0, reg_flash_mode_timeout_cnt //up to 2.79s at 24Mhz clk input
#define ISP_LED_TIMING3                     0x219b  
//bit 25:0, reg_t_st_lat_cnt //up to 2.79s, keep the led on if it's 26'h3ffffff under torch mode
#define ISP_LED_TIMING4                     0x219c  
//bit 31:26, reg_t_st_off_cnt[5:0] //up to 85.2us OFF state.
//bit 25:0, reg_t_en2_hi2_st_lat_cnt //EN2 can output hi-lo-hi during ST_LATCH, this is the second hi -duration
#define ISP_LED_TIMING5                     0x219d 
//bit 30:26, reg_t_st_off_cnt[10:6] //up to 85.2us OFF state.
//bit 25:0, reg_t_en1_st_lat_hold_cnt //to make sure Tlat to meat it's minimum request 500us (since non-std led_off signal may comes in anytime)
#define ISP_LED_TIMING6                     0x219e  

#define VPU_MISC_CTRL			    0x2740
//1:bt656 2:mipi
#define ISP_IN_SEL_BIT			    1
#define ISP_IN_SEL_WID			    2
#define ISP_VCBUS_BASE                      0x2d00

#define ISP_HV_SIZE                         0x2d00  
//Bit 31:29, reserved  
#define REG_HSIZE_BIT			    16
#define REG_HSIZE_WID			    13
//Bit 28:16, reg_hsize                      image horizontal size (number of cols)   
//Bit 15:13, reserved                       
#define REG_VSIZE_BIT			    0
#define REG_VSIZE_WID			    13
//Bit 12: 0, reg_vsize                      image vertical size   (number of rows)  

#define ISP_HBLANK                          0x2d01  
//Bit 31:29, reserved
#define REG_TOTAL_W_BIT				16
#define REG_TOTAL_W_WID				13
//Bit 15: 8, reserved
#define REG_HBLANK_BIT			    0
#define REG_HBLANK_WID			    8
//Bit  7: 0, reg_hblank                     image horizontal blank length   
#define ISP_TIMING_MODE                    0x2d02  
//Bit 31:17, reserved                      
//Bit    16, reg_din_timing_sw_ph           for safeing, enable this bit to disable de/vsyn/hsyn pulse sample when the input de/vsyn/hsyn polarity switch default=0   
//Bit 15: 8, reg_out_hs_ofst                the offset of output hsync (between two de) generate after last de of each line,  default=0
//Bit     7, reserved                       
//Bit  6: 5, reg_frm_syn_mode               bit[0]: isp global reset generate mode   1--reg soft reset input; 0--vsyn generate reset    //     bit[1]: input data process mode based on input hsyn  1--there is hsyn input; 0--no hsyn input   default=0
//Bit     4, reg_vs_samp_mode               vsyn posedge or negedge is sampled as trigger edge  //                                          0--posedge sample;   1--negedge sample     default=0
//Bit     3, reserved                       
//Bit  2: 0, reg_syn_level_invs             high-level or low-level of vsyn/hsyn/de is valid   default=0//                                          bit[2] for vsyn, 0--high-level valid    1--low is valid//                                          bit[1] for hsyn, 0--high-level valid    1--low is valid//                                          bit[0] for de  , 0--high-level valid    1--low is valid
#define ISP_RST_DLY_NUM                    0x2d03 
//Bit 31: 0, reg_frm_rst_dlynum             represent how many clock number delay after generated-global reset(from input vsyn or soft-reset).  default=0
#define ISP_OUTVS_DLY_NUM                  0x2d04 
//Bit 31: 0, reg_out_vs_dlynum              represent how many clock number delay generate the output vsync after generated-global reset(from input vsyn or soft-reset).default=0
#define ISP_DIN_WIND_OFST                  0x2d05
//Bit 31:29, reserved                       
//Bit 28:16, reg_din_wind_vofst             data input window vertical top offset default=0
//Bit 15:13, reserved                       
//Bit 12: 0, reg_din_wind_hofst             data input window horizontal left offset default=0
#define ISP_FRM_SOFT_RST                   0x2d06
//Bit 31: 1, reserved                       
//Bit     0, reg_frm_soft_rst               frm soft reset default=0
#define ISP_RST_SYN_SEL                   0x2d07 
//Bit 31: 0, reg_rst_syn_sel                some important reg-setting signal, such as sub-function enable, need to switch sync to 
// the isp reset time, this signal is cfg those signal whether sync-reset enable or real-time
// enable, when 1 is sync to reset default=32'hffffffff,


/******** pattern generator registers***********/

#define ISP_PAT_GEN_CTRL                    0x2d08  
//Bit 31:29, reserved  
#define ISP_PAT_ENABLE_BIT		    28
#define ISP_PAT_ENABLE_WID		    1
//Bit    28, reg_isp_pat_enable             enable of pattern generator data path, 0- disable; 1- enable; default=0    
#define ISP_PAT_XINVT_BIT		    27
#define ISP_PAT_XINVT_WID		    1
//Bit    27, reg_isp_pat_xinvt             invert the pattern in horizontal direction, 0- no invert; 1- invert; default=0 
#define ISP_PAT_YINVT_BIT		    26
#define ISP_PAT_YINVT_WID		    1
//Bit    26, reg_isp_pat_yinvt              invert the pattern in vertical direction,   0- no invert; 1- invert; default=0 
#define PAT_BAYER_FMT_BIT		    24//bayer fmt 0:BGGR 1:RGGB 2:GBRG 3:GRBG
#define PAT_BAYER_FMT_WID		    2
//Bit    24, reg_isp_pat_yphase_ofst        bayer pattern yphase offset in pattern generator, 0- vertically start from G(B)/R(G); 1- vertially start from B(G)/G(R); default=0
//Bit    23, reserved
#define ISP_PAT_XMODE_BIT	            20
#define ISP_PAT_XMODE_WID		    3
//Bit 22:20, reg_isp_pat_xmode              pattern horizontal mode, 0: raster/bar16/burst; 1; ramp-up; 2/up: normalized gain of horizontal dirrection (no change); default = 0
//Bit    19, reserved                       
#define ISP_PAT_YMODE_BIT		    16
#define ISP_PAT_YMODE_WID		    3
//Bit 18:16, reg_isp_pat_ymode              pattern vertical mode, 0: raster/bar16/burst; 1; ramp-up; 2/up: normalized gain of vertical dirrection (no change); default = 0
//Bit 15: 2, reserved 
#define ISP_PAT_DFT_MODE_BIT		    0
#define ISP_PAT_DFT_MODE_WID	            2
//Bit  1: 0, reg_isp_pat_dft_mode           defect pixel emulatiion mode in pattern generator; 0: no dft; 1: dead pixel (black); 2: hot pixel; 3:not dft default =0;

#define ISP_PAT_XRAMP_SCAL                  0x2d09  
//Bit 31:16, reserved
#define PAT_XRMP_SCALE_R_BIT		    16
#define PAT_XRMP_SCALE_R_WID		    8
//Bit 23:16, reg_isp_pat_xrmp_scale_r       ramp pattern horizontal scale for red channel,  default= 255
#define PAT_XRMP_SCALE_G_BIT		    8
#define PAT_XRMP_SCALE_G_WID		    8
//Bit 15: 8, reg_isp_pat_xrmp_scale_g       ramp pattern horizontal scale for green channel,default= 255
#define PAT_XRMP_SCALE_B_BIT		    0
#define PAT_XRMP_SCALE_B_WID		    8
//Bit  7: 0, reg_isp_pat_xrmp_scale_b       ramp pattern horizontal scale for blue channel, default= 255

#define ISP_PAT_YRAMP_SCAL                  0x2d0a  
//Bit 31:16, reserved
#define PAT_YRMP_SCALE_R_BIT		    16
#define PAT_YRMP_SCALE_R_WID		    8
//Bit 23:16, reg_isp_pat_yrmp_scale_r       ramp pattern horizontal scale for red channel,   default= 255
#define PAT_YRMP_SCALE_G_BIT		    8
#define PAT_YRMP_SCALE_G_WID	            8
//Bit 15: 8, reg_isp_pat_yrmp_scale_g       ramp pattern horizontal scale for green channel, default= 255
#define PAT_YRMP_SCALE_B_BIT		    0
#define PAT_YRMP_SCALE_B_WID		    8
//Bit  7: 0, reg_isp_pat_yrmp_scale_b       ramp pattern horizontal scale for blue channel,  default= 255

#define ISP_PAT_XYIDX_OFST                  0x2d0b  
//Bit 31:29, reserved  
#define PAT_XIEX_OFSET_BIT		    16
#define PAT_XIEX_OFSET_WID		    13
//Bit 28:16, reg_isp_pat_xidx_ofset         horizontal index ofset for pattern generation. default=0    
//Bit 15:13, reserved  
#define PAT_YIEX_OFSET_BIT		    0
#define PAT_YIEX_OFSET_WID		    13
//Bit 12: 0, reg_isp_pat_yidx_ofset         vertical index ofset for pattern generation. default=0 

#define ISP_PAT_XYIDX_SCAL                  0x2d0c  
//Bit 31:30, reserved  
#define PAT_XIEX_RSHFT_BIT		    28
#define PAT_XIEX_RSHFT_WID		    2
//Bit 29:28, reg_isp_pat_xidx_rshft         pattern generator horizontal index scale right shift, 0~3: scale normalized to divx=2^(6-rshft); default = 0 
#define PAT_XIDX_SCALE_BIT		    16
#define PAT_XIDX_SCALE_WID		    12
//Bit 27:16, reg_isp_pat_xidx_scale         pattern generator horizontal index scale. default=ceil((1024/reg_hsize)*divx) will cover one pattern cycle horizontally
//Bit 15:14, reserved                       
#define PAT_YIEX_RSHFT_BIT		    12
#define PAT_YIEX_RSHFT_WID		    2
//Bit 13:12, reg_isp_pat_yidx_rshft         pattern generator horizontal index scale right shift, 0~3: scale normalized to divy=2^(6-rshft); default = 0 
#define PAT_YIDX_SCALE_BIT		    0
#define PAT_YIDX_SCALE_WID		    12
//Bit 11: 0, reg_isp_pat_yidx_scale         pattern generator horizontal index scale. ceil((1024/reg_vsize)*divy) will cover one pattern cycle vertically

#define ISP_PAT_BAR16_RED0                  0x2d0d  
#define PAT_BAR16RGB_R3_BIT		    24
#define PAT_BAR16RGB_R3_WID		    8
//Bit 31:24, reg_isp_pat_bar16rgb_r3     pattern generator 4th bar red value. 0~255.   default= 0
#define PAT_BAR16RGB_R2_BIT		    16
#define PAT_BAR16RGB_R2_WID		    8
//Bit 23:16, reg_isp_pat_bar16rgb_r2     pattern generator 3rd bar red value. 0~255.   default= 0
#define PAT_BAR16RGB_R1_BIT		    8
#define PAT_BAR16RGB_R1_WID		    8
//Bit 15: 8, reg_isp_pat_bar16rgb_r1     pattern generator 2nd bar red value. 0~255.   default= 255
#define PAT_BAR16RGB_R0_BIT		    0
#define PAT_BAR16RGB_R0_WID		    8
//Bit  7: 0, reg_isp_pat_bar16rgb_r0     pattern generator 1st bar red value. 0~255.   default= 255

#define ISP_PAT_BAR16_RED1                  0x2d0e  
#define PAT_BAR16RGB_R7_BIT		    24
#define PAT_BAR16RGB_R7_WID		    8
//Bit 31:24, reg_isp_pat_bar16rgb_r7     pattern generator 7th bar red value. 0~255.   default= 0
#define PAT_BAR16RGB_R6_BIT		    16
#define PAT_BAR16RGB_R6_WID		    8
//Bit 23:16, reg_isp_pat_bar16rgb_r6     pattern generator 6th bar red value. 0~255.   default= 0
#define PAT_BAR16RGB_R5_BIT		    8
#define PAT_BAR16RGB_R5_WID		    8
//Bit 15: 8, reg_isp_pat_bar16rgb_r5     pattern generator 5th bar red value. 0~255.   default= 255
#define PAT_BAR16RGB_R4_BIT		    0
#define PAT_BAR16RGB_R4_WID		    8
//Bit  7: 0, reg_isp_pat_bar16rgb_r4     pattern generator 4th bar red value. 0~255.   default= 255

#define ISP_PAT_BAR16_RED2                  0x2d0f  
#define PAT_BAR16RGB_R11_BIT		    24
#define PAT_BAR16RGB_R11_WID		    8
//Bit 31:24, reg_isp_pat_bar16rgb_r11    pattern generator 11th bar red value. 0~255.   default= 128
#define PAT_BAR16RGB_R10_BIT	            16
#define PAT_BAR16RGB_R10_WID		    8
//Bit 23:16, reg_isp_pat_bar16rgb_r10    pattern generator 10th bar red value. 0~255.   default= 96
#define PAT_BAR16RGB_R9_BIT		    8
#define PAT_BAR16RGB_R9_WID		    8
//Bit 15: 8, reg_isp_pat_bar16rgb_r9     pattern generator  9th bar red value. 0~255.   default= 64
#define PAT_BAR16RGB_R8_BIT		    0
#define PAT_BAR16RGB_R8_WID		    8
//Bit  7: 0, reg_isp_pat_bar16rgb_r8     pattern generator  8th bar red value. 0~255.   default= 32

#define ISP_PAT_BAR16_RED3                  0x2d10  
#define PAT_BAR16RGB_R15_BIT		    24
#define PAT_BAR16RGB_R15_WID		    8
//Bit 31:24, reg_isp_pat_bar16rgb_r15    pattern generator 15th bar red value. 0~255.   default= 255
#define PAT_BAR16RGB_R14_BIT		    16
#define PAT_BAR16RGB_R14_WID		    8
//Bit 23:16, reg_isp_pat_bar16rgb_r14    pattern generator 14th bar red value. 0~255.   default= 224
#define PAT_BAR16RGB_R13_BIT		    8
#define PAT_BAR16RGB_R13_WID		    8
//Bit 15: 8, reg_isp_pat_bar16rgb_r13    pattern generator 13th bar red value. 0~255.   default= 192
#define PAT_BAR16RGB_R12_BIT		    0
#define PAT_BAR16RGB_R12_WID		    8
//Bit  7: 0, reg_isp_pat_bar16rgb_r12    pattern generator 12th bar red value. 0~255.   default= 160

#define ISP_PAT_BAR16_GRN0                  0x2d11  
#define PAT_BAR16RGB_G3_BIT		    24
#define PAT_BAR16RGB_G3_WID		    8
//Bit 31:24, reg_isp_pat_bar16rgb_g3     pattern generator 4th bar green value. 0~255.   default= 255
#define PAT_BAR16RGB_G2_BIT		    16
#define PAT_BAR16RGB_G2_WID		    8
//Bit 23:16, reg_isp_pat_bar16rgb_g2     pattern generator 3rd bar green value. 0~255.   default= 255
#define PAT_BAR16RGB_G1_BIT		    8
#define PAT_BAR16RGB_G1_WID		    8
//Bit 15: 8, reg_isp_pat_bar16rgb_g1     pattern generator 2nd bar green value. 0~255.   default= 255
#define PAT_BAR16RGB_G0_BIT		    0
#define PAT_BAR16RGB_G0_WID		    8
//Bit  7: 0, reg_isp_pat_bar16rgb_g0     pattern generator 1st bar green value. 0~255.   default= 255

#define ISP_PAT_BAR16_GRN1                  0x2d12 
#define PAT_BAR16RGB_G7_BIT		    24
#define PAT_BAR16RGB_G7_WID		    8
//Bit 31:24, reg_isp_pat_bar16rgb_g7     pattern generator 7th bar green value. 0~255.   default= 0
#define PAT_BAR16RGB_G6_BIT		    16
#define PAT_BAR16RGB_G6_WID		    8
//Bit 23:16, reg_isp_pat_bar16rgb_g6     pattern generator 6th bar green value. 0~255.   default= 0
#define PAT_BAR16RGB_G5_BIT		    8
#define PAT_BAR16RGB_G5_WID		    8
//Bit 15: 8, reg_isp_pat_bar16rgb_g5     pattern generator 5th bar green value. 0~255.   default= 0
#define PAT_BAR16RGB_G4_BIT		    0
#define PAT_BAR16RGB_G4_WID		    8
//Bit  7: 0, reg_isp_pat_bar16rgb_g4     pattern generator 4th bar green value. 0~255.   default= 0

#define ISP_PAT_BAR16_GRN2                  0x2d13  
#define PAT_BAR16RGB_G11_BIT		    24
#define PAT_BAR16RGB_G11_WID		    8
//Bit 31:24, reg_isp_pat_bar16rgb_g11    pattern generator 11th bar green value. 0~255.   default= 128
#define PAT_BAR16RGB_G10_BIT		    16
#define PAT_BAR16RGB_G10_WID		    8
//Bit 23:16, reg_isp_pat_bar16rgb_g10    pattern generator 10th bar green value. 0~255.   default= 96
#define PAT_BAR16RGB_G9_BIT		    8
#define PAT_BAR16RGB_G9_WID		    8
//Bit 15: 8, reg_isp_pat_bar16rgb_g9    pattern generator  9th bar green value. 0~255.   default= 64
#define PAT_BAR16RGB_G8_BIT		    0
#define PAT_BAR16RGB_G8_WID		    8
//Bit  7: 0, reg_isp_pat_bar16rgb_g8    pattern generator  8th bar green value. 0~255.   default= 32

#define ISP_PAT_BAR16_GRN3                  0x2d14
#define PAT_BAR16RGB_G15_BIT		    24
#define PAT_BAR16RGB_G15_WID		    8
//Bit 31:24, reg_isp_pat_bar16rgb_g15    pattern generator 15th bar green value. 0~255.   default= 255
#define PAT_BAR16RGB_G14_BIT		    16
#define PAT_BAR16RGB_G14_WID		    8
//Bit 23:16, reg_isp_pat_bar16rgb_g14    pattern generator 14th bar green value. 0~255.   default= 224
#define PAT_BAR16RGB_G13_BIT		    8
#define PAT_BAR16RGB_G13_WID		    8
//Bit 15: 8, reg_isp_pat_bar16rgb_g13    pattern generator 13th bar green value. 0~255.   default= 192
#define PAT_BAR16RGB_G12_BIT		    0
#define PAT_BAR16RGB_G12_WID		    8
//Bit  7: 0, reg_isp_pat_bar16rgb_g12    pattern generator 12th bar green value. 0~255.   default= 160 

#define ISP_PAT_BAR16_BLU0                  0x2d15       
#define PAT_BAR16RGB_B3_BIT		    24
#define PAT_BAR16RGB_B3_WID		    8
//Bit 31:24, reg_isp_pat_bar16rgb_b3     pattern generator 4th bar green value. 0~255.   default= 0  
#define PAT_BAR16RGB_B2_BIT		    16
#define PAT_BAR16RGB_B2_WID		    8
//Bit 23:16, reg_isp_pat_bar16rgb_b2     pattern generator 3rd bar green value. 0~255.   default= 255  
#define PAT_BAR16RGB_B1_BIT		    8
#define PAT_BAR16RGB_B1_WID		    8
//Bit 15: 8, reg_isp_pat_bar16rgb_b1     pattern generator 2nd bar green value. 0~255.   default= 0  
#define PAT_BAR16RGB_B0_BIT		    0
#define PAT_BAR16RGB_B0_WID		    8
//Bit  7: 0, reg_isp_pat_bar16rgb_b0     pattern generator 1st bar green value. 0~255.   default= 255   

#define ISP_PAT_BAR16_BLU1                  0x2d16
#define PAT_BAR16RGB_B7_BIT		    24
#define PAT_BAR16RGB_B7_WID		    8
//Bit 31:24, reg_isp_pat_bar16rgb_b7     pattern generator 7th bar green value. 0~255.   default= 0    
#define PAT_BAR16RGB_B6_BIT		    16
#define PAT_BAR16RGB_B6_WID		    8
//Bit 23:16, reg_isp_pat_bar16rgb_b6     pattern generator 6th bar green value. 0~255.   default= 255    
#define PAT_BAR16RGB_B5_BIT		    8
#define PAT_BAR16RGB_B5_WID		    8
//Bit 15: 8, reg_isp_pat_bar16rgb_b5     pattern generator 5th bar green value. 0~255.   default= 0    
#define PAT_BAR16RGB_B4_BIT		    0
#define PAT_BAR16RGB_B4_WID		    8
//Bit  7: 0, reg_isp_pat_bar16rgb_b4     pattern generator 4th bar green value. 0~255.   default= 255     

#define ISP_PAT_BAR16_BLU2                  0x2d17    
#define PAT_BAR16RGB_B11_BIT		    24
#define PAT_BAR16RGB_B11_WID		    8
//Bit 31:24, reg_isp_pat_bar16rgb_b11    pattern generator 11th bar green value. 0~255.   default= 128
#define PAT_BAR16RGB_B10_BIT		    16
#define PAT_BAR16RGB_B10_WID		    8
//Bit 23:16, reg_isp_pat_bar16rgb_b10    pattern generator 10th bar green value. 0~255.   default= 96 
#define PAT_BAR16RGB_B9_BIT		    8
#define PAT_BAR16RGB_B9_WID		    8
//Bit 15: 8, reg_isp_pat_bar16rgb_b9     pattern generator  9th bar green value. 0~255.   default= 64 
#define PAT_BAR16RGB_B8_BIT		    0
#define PAT_BAR16RGB_B8_WID		    8
//Bit  7: 0, reg_isp_pat_bar16rgb_b8     pattern generator  8th bar green value. 0~255.   default= 32                                                                                                       

#define ISP_PAT_BAR16_BLU3                  0x2d18
#define PAT_BAR16RGB_B15_BIT		    24
#define PAT_BAR16RGB_B15_WID		    8
//Bit 31:24, reg_isp_pat_bar16rgb_b15    pattern generator 15th bar green value. 0~255.   default= 255
#define PAT_BAR16RGB_B14_BIT		    16
#define PAT_BAR16RGB_B14_WID		    8
//Bit 23:16, reg_isp_pat_bar16rgb_b14    pattern generator 14th bar green value. 0~255.   default= 224
#define PAT_BAR16RGB_B13_BIT		    8
#define PAT_BAR16RGB_B13_WID		    8
//Bit 15: 8, reg_isp_pat_bar16rgb_b13    pattern generator 13th bar green value. 0~255.   default= 192
#define PAT_BAR16RGB_B12_BIT		    0
#define PAT_BAR16RGB_B12_WID		    8
//Bit  7: 0, reg_isp_pat_bar16rgb_b12    pattern generator 12th bar green value. 0~255.   default= 160                                                                                                      

#define ISP_PAT_DFT_XYIDX                   0x2d19  
//Bit 31:29, reserved  
#define PAT_DFT_XIDEX_BIT		    16
#define PAT_DFT_XIDEX_WID		    13
//Bit 28:16, reg_isp_pat_dft_xidx           horizontal index of defect region center for pattern generation. default=100    
//Bit 15:13, reserved   
#define PAT_DFT_YIDEX_BIT		    0
#define PAT_DFT_YIDEX_WID		    13
//Bit 12: 0, reg_isp_pat_dft_yidx           ver index of defect region center for pattern generation. default=200 

#define ISP_PAT_DFT_XYWID                   0x2d1a  
//Bit 31:29, reserved  
#define PAT_DFT_XWID_BIT		    16
#define PAT_DFT_XWID_WID		    13
//Bit 28:16, reg_isp_pat_dft_xwid           horizontal half width of defect region for pattern generation. width = 2x+1, default=1    
//Bit 15:13, reserved                    
#define PAT_DFT_YWID_BIT		    0
#define PAT_DFT_YWID_WID		    13
//Bit 12: 0, reg_isp_pat_dft_ywid           vertical half width of defect region for pattern generation. width = 2y+1, default=64

#define ISP_PAT_DFT_GAIN                    0x2d1b  
//Bit 31:30, reserved  
#define PAT_DFT_GAINGRBG0_BIT		    24
#define PAT_DFT_GAINGRBG0_WID		    6
//Bit 29:24, reg_isp_pat_dft_gaingrbg0   lazy or active pixel gain of green channel (phase0). <32 means lazy pixel, >32 means active, 32: means normal; default=0    
//Bit 23:22, reserved    
#define PAT_DFT_GAINGRBG1_BIT		    21
#define PAT_DFT_GAINGRBG1_WID		    6
//Bit 21:16, reg_isp_pat_dft_gaingrbg1   lazy or active pixel gain of red channel (phase1). <32 means lazy pixel, >32 means active, 32: means normal; default=0   
//Bit 15:14, reserved   
#define PAT_DFT_GAINGRBG2_BIT		    8
#define PAT_DFT_GAINGRBG2_WID		    6
//Bit 13: 8, reg_isp_pat_dft_gaingrbg2   lazy or active pixel gain of blue channel (phase2). <32 means lazy pixel, >32 means active, 32: means normal; default=0   
//Bit  7: 6, reserved
#define PAT_DFT_GAINGRBG3_BIT		    0
#define PAT_DFT_GAINGRBG3_WID		    6
//Bit  5: 0, reg_isp_pat_dft_gaingrbg3   lazy or active pixel gain of green channel (phase3). <32 means lazy pixel, >32 means active, 32: means normal; default=0  

#define ISP_PAT_HVTOTAL                     0x2d1c 
//Bit 31:16, reg_isp_pat_vtotal             default=490    
//Bit 15: 0, reg_isp_pat_htotal             default=760
#define ISP_PAT_VDE_SLINE                   0x2d1d
//Bit 31:29, reserved  
//Bit 15: 0, reg_isp_pat_vde_sline          default=64
#define ISP_OUTHS_PARA                      0x2d1e 
//Bit 31:16, reg_ouths_pre_dist             represent the output hsyn inter-distance when need some output hsyn in the head of each frame  default=0        
//Bit 15: 8, reserved                     
//Bit  7: 0, reg_ouths_pre_num              how many output hsyn is needed in the head of each frame   default=0        
#define ISP_FRM_DONE_PARA                   0x2d1f 
//Bit 31:17, reserved                      
//Bit    16, reg_isp_intr_sel               isp interrupt source select  0--generated global reset 1--isp real frame process done  default=0       
//Bit 15: 0, reg_frm_done_dlynum            how many time frame done delay as the finally done  default=128        



/*********** Clamp and Gain module registers************/

#define ISP_CLAMPGAIN_CTRL                  0x2d20  
//Bit 31:26, reserved
#define CLP_BAYER_FMT_BIT		    24//bayer fmt 0:BGGR 1:RGGB 2:GBRG 3:GRBG
#define CLP_BAYER_FMT_WID		    2
//Bit    24, reg_isp_clp_yphase_ofst        bayer pattern yphase offset in clamp gain, 0- vertically start from G(B)/R(G); 1- vertially start from B(G)/G(R); default=0 
//Bit 23:22, reserved
#define AECRAW_LPF_SEL_BIT		    20
#define AECRAW_LPF_SEL_WID		    2
//Bit 21:20, reg_isp_aecraw_lpf_sel         low-pass filter mode for raw data components too bright statistics for auto exposure. 0: no lpf; 1:[1 2 1]/4; 2: [1 2 2 2 1]/8; 3:[1 2 3 4 3 2 1]/16; default = 0
//Bit 19:18, reserved            
#define BANDSPLIT_MODE_BIT		    16
#define BANDSPLIT_MODE_WID		    2
//Bit 17:16, reg_isp_bandsplit_mode         bandsplit filter mode for clamp and gain,  0- no lpf; 1: [1 2 1]/4; 2:[1 2 2 2 1]/8; 3: [1 2 3 4 3 2 1]/4;   default= 0
//Bit 15: 8, reserved
#define BANDSPLIT_USK_BIT		    0
#define BANDSPLIT_USK_WID		    8
//Bit  7: 0, reg_isp_bandsplit_usk          digital gain to unsharped highpass portion of each channel before adding back, 0~255, 32 normalized to '1'.  default= 32

#define ISP_GAIN_BSCORE_GRBG                0x2d21
#define BANDSPLIT_CORE0_BIT		    24
#define BANDSPLIT_CORE0_WID		    8
//Bit 31:24, reg_isp_bandsplit_core0     coring to unsharp part of green (phase0) channel.   default= 0
#define BANDSPLIT_CORE1_BIT		    16
#define BANDSPLIT_CORE1_WID		    8
//Bit 23:16, reg_isp_bandsplit_core1     coring to unsharp part of red   (phase1) channel.   default= 0
#define BANDSPLIT_CORE2_BIT		    8
#define BANDSPLIT_CORE2_WID		    8
//Bit 15: 8, reg_isp_bandsplit_core2     coring to unsharp part of blue  (phase2) channel.   default= 0
#define BANDSPLIT_CORE3_BIT		    0
#define BANDSPLIT_CORE3_WID		    8
//Bit  7: 0, reg_isp_bandsplit_core3     coring to unsharp part of green (phase3) channel.   default= 0

#define ISP_CLAMP_GRBG01                    0x2d22  
//Bit 31:26, reserved
#define CLAMP_GRBG0_BIT			    16
#define CLAMP_GRBG0_WID			    10
//Bit 25:16, reg_isp_clamp_grbg0         clamping offset to raw data green (phase0) channel, -512~511.   default= 0
//Bit 15:10, reserved
#define CLAMP_GRBG1_BIT			    0
#define CLAMP_GRBG1_WID			    10
//Bit  9: 0, reg_isp_clamp_grbg1         clamping offset to raw data red (phase1) channel, -512~511.   default= 0

#define ISP_CLAMP_GRBG23                    0x2d23  
//Bit 31:26, reserved
#define CLAMP_GRBG2_BIT			    16
#define CLAMP_GRBG2_WID			    10
//Bit 25:16, reg_isp_clamp_grbg2         clamping offset to raw data blue (phase2) channel, -512~511.   default= 0
//Bit 15:10, reserved
#define CLAMP_GRBG3_BIT			    0
#define CLAMP_GRBG3_WID			    10
//Bit  9: 0, reg_isp_clamp_grbg3         clamping offset to raw data green (phase3) channel, -512~511.   default= 0

#define ISP_GAIN_GRBG01                     0x2d24  
//Bit 31:28, reserved
#define GAIN_GRBG0_BIT			    16
#define GAIN_GRBG0_WID			    12
//Bit 27:16, reg_isp_gain_grbg0          digital gain to raw data green (phase0) channel, 0~4095, 256 as normalized '1'.   default= 256
//Bit 15:12, reserved 
#define GAIN_GRBG1_BIT			    0
#define GAIN_GRBG1_WID			    12
//Bit 11: 0, reg_isp_gain_grbg1          digital gain to raw data red   (phase1) channel, 0~4095, 256 as normalized '1'.   default= 256

#define ISP_GAIN_GRBG23                     0x2d25  
//Bit 31:28, reserved
#define GAIN_GRBG2_BIT			    16
#define GAIN_GRBG2_WID			    12
//Bit 27:16, reg_isp_gain_grbg2          digital gain to raw data blue  (phase2) channel, 0~4095, 256 as normalized '1'.   default= 256
//Bit 15:12, reserved
#define GAIN_GRBG3_BIT			    0
#define GAIN_GRBG3_WID			    12
//Bit 11: 0, reg_isp_gain_grbg3          digital gain to raw data green (phase3) channel, 0~4095, 256 as normalized '1'.   default= 256

// address 0x2d26~ 0x2d27 null
/*********************** lens shading correction registers*****************/
#define ISP_LNS_CTRL                        0x2d28    
//Bit 31:29, reserved
#define LNS_CMOP_ENABLE_BIT		    28
#define LNS_CMOP_ENABLE_WID		    1
//Bit    28, reg_isp_lns_cmop_enable    lens shading compensation enable. 0: no compensation; 1: compensation enable;  default= 0
//Bit 27:26, reserved
#define LNS_BAYER_FMT_BIT		    24//bayer fmt 0:BGGR 1:RGGB 2:GBRG 3:GRBG
#define LNS_BAYER_FMT_WID		    2
//Bit    24, reg_isp_lns_yphase_ofst    bayer pattern yphase offset in lens shading correction, 0- vertically start from G(B)/R(G); 1- vertially start from B(G)/G(R); default=0 
#define LNS_GAINNORM_GRBG0_BIT		    22
#define LNS_GAINNORM_GRBG0_WID		    2
//Bit 23:22, reg_isp_lns_gainnorm_grbg0 normalization mode for green channel (phase0) of compensation gain. 0: norm to 128; 1: norm to 64; 2: norm to 32; 3: norm to 16; defautl =1
#define LNS_GAINNORM_GRBG1_BIT		    20
#define LNS_GAINNORM_GRBG1_WID		    2
//Bit 21:20, reg_isp_lns_gainnorm_grbg1 normalization mode for red   channel (phase1) of compensation gain. 0: norm to 128; 1: norm to 64; 2: norm to 32; 3: norm to 16; defautl =1
#define LNS_GAINNORM_GRBG2_BIT		    18
#define LNS_GAINNORM_GRBG2_WID		    2
//Bit 19:18, reg_isp_lns_gainnorm_grbg2 normalization mode for blue  channel (phase2) of compensation gain. 0: norm to 128; 1: norm to 64; 2: norm to 32; 3: norm to 16; defautl =1
#define LNS_GAINNORM_GRBG3_BIT		    16
#define LNS_GAINNORM_GRBG3_WID		    2
//Bit 17:16, reg_isp_lns_gainnorm_grbg3 normalization mode for green channel (phase3) of compensation gain. 0: norm to 128; 1: norm to 64; 2: norm to 32; 3: norm to 16; defautl =1
#define LNS_GAIN_MODE_BIT		    15
#define LNS_GAIN_MODE_WID		    1
//Bit    15, reg_isp_lns_gain_mode      mode for gains from lut32x32x4. 0: final gain= x; 1: final gain = 1+x; default = 1
#define LNS_MESH_MODE_BIT		    12
#define LNS_MESH_MODE_WID		    3
//Bit 14:12, reg_isp_lns_mesh_mode      mode for getting gain value from or write pixel raw components to lut32x32x4. if write pixel to lut (x>0), it was used for lut gain calculation during calibration. 0: read gain from lut 32x32x4; 1: gain='1', write pixel data without lpf to lut32x32x4 of the mesh position; 2; gain='1', write [1 2 1]/4 lpf filtered pixel data to lut32x32x4 of mesh position; 3: gain='1', write [1 2 2 2 1]/8 lpf filtered pixel data to lut32x32x4 of mesh position; default = 0;
#define LNS_HOLD_BIT			    0
#define LNS_HOLD_WID			    12
//Bit 11: 0, reg_isp_lns_hold           TBD

#define ISP_LNS_XYSCAL                      0x2d29    
//Bit 31:28, reserved
#define LNS_XSCALE_BIT			    16
#define LNS_XSCALE_WID			    12
//Bit 27:16, reg_isp_lns_xscale             lens shading compensation horizontal index scale. extend the sensor region to 32x32 grid, xscale=floor(7936*32/reg_hsize)
//Bit 15:12, reserved
#define LNS_YSCALE_BIT			    0
#define LNS_YSCALE_WID			    12
//Bit 11: 0, reg_isp_lns_yscale             lens shading compensation vertical index scale. extend the sensor region to 32x32 grid, yscale=floor(7936*32/reg_vsize)

#define ISP_LNS_XYIDX_SHFT                  0x2d2a    
//Bit 31:30, reserved
#define LNS_XIDX_SHIFT_BIT		    16
#define LNS_XIDX_SHIFT_WID		    14
//Bit 29:16, reg_isp_lns_xidx_shift         lens shading compensation horizontal index offset. -8192~8191; default= 0;
//Bit 15:14, reserved
#define LNS_YIDX_SHIFT_BIT		    0
#define LNS_YIDX_SHIFT_WID		    14
//Bit 13: 0, reg_isp_lns_yidx_shift         lens shading compensation vertical index offset.   -8192~8191; default= 0;


#define ISP_LNS_SENSOR_GAINGRBG             0x2d2b
#define LNS_SENSOR_GAINGRBG0_BIT	    24
#define LNS_SENSOR_GAINGRBG0_WID	    8
//Bit 31:24, reg_isp_lns_sensor_gaingrbg0    gain to the lut 32x32 phase 0 green gain to compensate different Lenshading characters under different sensor gain. 0~255. normalized 128 as '1'; default=128                        
#define LNS_SENSOR_GAINGRBG1_BIT	    16
#define LNS_SENSOR_GAINGRBG1_WID	    8
//Bit 23:16, reg_isp_lns_sensor_gaingrbg1    gain to the lut 32x32 phase 1   red gain to compensate different Lenshading characters under different sensor gain. 0~255. normalized 128 as '1'; default=128
#define LNS_SENSOR_GAINGRBG2_BIT	    8
#define LNS_SENSOR_GAINGRBG2_WID	    8
//Bit 15: 8, reg_isp_lns_sensor_gaingrbg2    gain to the lut 32x32 phase 2  blue gain to compensate different Lenshading characters under different sensor gain. 0~255. normalized 128 as '1'; default=128                      
#define LNS_SENSOR_GAINGRBG3_BIT	    0
#define LNS_SENSOR_GAINGRBG3_WID	    8
//Bit  7: 0, reg_isp_lns_sensor_gaingrbg3    gain to the lut 32x32 phase 3 green gain to compensate different Lenshading characters under different sensor gain. 0~255. normalized 128 as '1'; default=128

#define ISP_LNS_POST_OFSTGRBG               0x2d2c
#define LNS_POST_OFSET_GRBG0_BIT	    24
#define LNS_POST_OFSET_GRBG0_WID	    8
//Bit 31:24, reg_isp_lns_post_ofset_grbg0    ofset to phase 0 green pixel after lens compensation (gain). -128~127. default= 0                        
#define LNS_POST_OFSET_GRBG1_BIT	    16
#define LNS_POST_OFSET_GRBG1_WID	    8
//Bit 23:16, reg_isp_lns_post_ofset_grbg1    ofset to phase 1 red   pixel after lens compensation (gain). -128~127. default= 0
#define LNS_POST_OFSET_GRBG2_BIT	    8
#define LNS_POST_OFSET_GRBG2_WID	    8
//Bit 15: 8, reg_isp_lns_post_ofset_grbg2    ofset to phase 2 blue  pixel after lens compensation (gain). -128~127. default= 0                      
#define LNS_POST_OFSET_GRBG3_BIT	    0
#define LNS_POST_OFSET_GRBG3_WID	    8
//Bit  7: 0, reg_isp_lns_post_ofset_grbg3    ofset to phase 3 green pixel after lens compensation (gain). -128~127. default= 0

// address 0x2d2d~ 0x2d2f null

/*****************gamma correction registers*******************/

#define ISP_GMR0_CTRL                       0x2d30  
//Bit 31:29, reserved
#define GMR_CORRECT_ENABLE_BIT		    28
#define GMR_CORRECT_ENABLE_WID		    1
//Bit    28, reg_isp_gmr_correct_enable     raw data gammar correction enable;    default= 0;
//Bit 27:26, reserved
#define GMR_BAYER_FMT_BIT		    24//bayer fmt 0:BGGR 1:RGGB 2:GBRG 3:GRBG
#define GMR_BAYER_FMT_WID		    2
//Bit    24, reg_isp_gmr_yphase_ofst        bayer pattern yphase offset in gammar correction, 0- vertically start from G(B)/R(G); 1- vertially start from B(G)/G(R); default=0 
//Bit 23: 1, reserved 
#define GCLUT_ACCMODE_BIT		    0
#define GCLUT_ACCMODE_WID		    1
//Bit     0, reg_isp_gclut_accmode          TBD

// TBD: gammar LUT memory operation

/*********************defect pixel correction registers**********************/

#define ISP_DFT_CTRL                        0x2d31   
//Bit 31:29, reserved
#define ISP_DFT_ENABLE_BIT		    28
#define ISP_DFT_ENABLE_WID		    1
//Bit    28, reg_isp_dft_enable             defect pixel detection block operation enable, reg_isp_dft_detect_mode decides the detection mode; 0: no defect_detection; 1: detection logic on;    default= 1;
//Bit 27:26, reserved
#define DFT_BAYER_FMT_BIT		    24//bayer fmt 0:BGGR 1:RGGB 2:GBRG 3:GRBG
#define DFT_BAYER_FMT_WID		    2
//Bit    24, reg_isp_dft_yphase_ofst        bayer pattern yphase offset in defect pixel correction, 0- vertically start from G(B)/R(G); 1- vertially start from B(G)/G(R); default=0 
//Bit 23:20, reserved
#define DFTMAP_WRITETO_LUT_STLINE_BIT	    16
#define DFTMAP_WRITETO_LUT_STLINE_WID	    4
//Bit 19:16, reg_isp_dftmap_writeto_lut_stline starting line number to write dftmap to LUT1024. 0~15; default= 14;
//Bit 15:13, reserved
#define DFTMAP_WRITETO_LUT_ENABLE_BIT	    12
#define DFTMAP_WRITETO_LUT_ENABLE_WID	    1
//Bit    12, reg_isp_dftmap_writeto_lut     enable to HW write dftmap info to LUT1024 in raster-scan mode. 0: no write to LUT1024; 1: write to LUT1024 (HW)
//Bit 11:10, reserved
#define DFTMAP_CORRECT_DRTATE_BIT	    8
#define DFTMAP_CORRECT_DRTATE_WID	    2
//Bit  9: 8, reg_isp_dftmap_correct_drtrate  defect pixel correction directional rate for error. 0; x2; 1:1  2: 1/2; 3:1/4; default=1
//Bit  7: 6, reserved
#define DFT_DETECT_MODE_BIT		    4
#define DFT_DETECT_MODE_WID		    2
//Bit  5: 4, reg_isp_dft_detect_mode        defect pixel detection mode. 0: manual mode; 1/2/3: adaptive mode, default = 2;
//Bit  3: 2, reserved
#define DFTMAP_CORRECT_MODE_BIT	            0
#define DFTMAP_CORRECT_MODE_WID		    2
//Bit  1: 0, reg_isp_dftmap_correct_mode    defect map generation mode. 0: all dftmap=0; 1: dftmap based on LUT1024 only; 2: based on on dft_map9 (only sky mode only; 3 based on both LUT1024 and dft_map9) defaul=3


#define ISP_DFT_VAR_MINMAX                  0x2d33
#define ISP_DFT_VARMIN_BIT		    24
#define ISP_DFT_VARMIN_WID		    8
//Bit 31:24, reg_isp_dft_varmin             min limit for 4x8 block variance.  default = 10;
#define ISP_DFT_VARMAX_BIT		    16
#define ISP_DFT_VARMAX_WID		    8
//Bit 23:16, reg_isp_dft_varmax             max limit for 4x8 block variance.  default = 32;
#define ISP_DFT_THDLOW_BIT		    8
#define ISP_DFT_THDLOW_WID		    8
//Bit 15: 8, reg_isp_dft_thdlow_minfloor    min limit for 4x8 block mininum value for thd_low calculation. default= 75;
#define ISP_DFT_THDHIG_BIT		    0
#define ISP_DFT_THDHIG_WID		    8
//Bit  7: 0, reg_isp_dft_thdhig_maxfloor    max limit for 4x8 block maximum value for thd_low calculation. default= 175;

#define ISP_DFT_THDLOW                      0x2d34
#define DFT_THDLOW_VARRATE_BIT		    24
#define DFT_THDLOW_VARRATE_WID		    8
//Bit 31:24, reg_isp_dft_thdlow_varrate     rate to variance in thdlow calculation. normalized to 16 as '1', default = 16; 
#define DFT_THDLOW_MINRATE_BIT		    16
#define DFT_THDLOW_MINRATE_WID		    8
//Bit 23:16, reg_isp_dft_thdlow_minrate     rate to distance between min_BLK and avg_BLK in thdlow calculation. normalized to 16 as '1', default = 8; 
#define DFT_THDLOW_MINRANGE_BIT		    8
#define DFT_THDLOW_MINRANGE_WID		    8
//Bit 15: 8, reg_isp_dft_thdlow_minrange    min limit of adaptive range for thd_low calculation. default= 2 (calibrat mode =0/1);  25 (calibrat mode =2/3);
#define DFT_THDLOW_MAXRANGE_BIT		    0
#define DFT_THDLOW_MAXRANGE_WID		    8
//Bit  7: 0, reg_isp_dft_thdlow_maxrange    max limit of adaptive range for thd_low calculation. default= 2 (calibrat mode =0/1); 210 (calibrat mode =2/3);


#define ISP_DFT_THDHIG                      0x2d35
#define DFT_THDHIG_VARRATE_BIT		    24
#define DFT_THDHIG_VARRATE_WID		    8
//Bit 31:24, reg_isp_dft_thdhig_varrate     rate to variance in thdhig calculation. normalized to 16 as '1', default = 16; 
#define DFT_THDHIG_MINRATE_BIT		    16
#define DFT_THDHIG_MINRATE_WID		    8
//Bit 23:16, reg_isp_dft_thdhig_minrate     rate to distance between min_BLK and avg_BLK in thdhig calculation. normalized to 16 as '1', default = 8; 
#define DFT_THDHIG_MINRANGE_BIT		    8
#define DFT_THDHIG_MINRANGE_WID		    8
//Bit 15: 8, reg_isp_dft_thdhig_minrange    min limit of adaptive range for thdhig calculation. default= 2 (calibrat mode =0/1);  25 (calibrat mode =2/3);
#define DFT_THDHIG_MAXRANGE_BIT		    0
#define DFT_THDHIG_MAXRANGE_WID		    8
//Bit  7: 0, reg_isp_dft_thdhig_maxrange    max limit of adaptive range for thdhig calculation. default= 2 (calibrat mode =0/1); 210 (calibrat mode =2/3);

#define ISP_DFT_CALIBRAT_REF                0x2d36  
//Bit 31:24, reserved
#define DFT_CALIBRAT_REF_R_BIT		    16
#define DFT_CALIBRAT_REF_R_WID		    8
//Bit 23:16  reg_isp_dft_calibrat_ref_r     calibration reference   red color value under reg_ISP_dft_calibrat_mode=0; normally get from average of red component of the image (only work for raster pattern); default= 100;
#define DFT_CALIBRAT_REF_G_BIT		    8
#define DFT_CALIBRAT_REF_G_WID		    8
//Bit 15: 8, reg_isp_dft_calibrat_ref_g     calibration reference green color value under reg_ISP_dft_calibrat_mode=0; normally get from average of green component of the image (only work for raster pattern); default= 100;
#define DFT_CALIBRAT_REF_B_BIT		    0
#define DFT_CALIBRAT_REF_B_WID		    8
//Bit  7: 0, reg_isp_dft_calibrat_ref_b     calibration reference  blue color value under reg_ISP_dft_calibrat_mode=0; normally get from average of blue component of the image (only work for raster pattern); default= 100;

#define ISP_DFT_CALIBRAT_CTRL               0x2d37   
//Bit 31:30, reserved
#define DFT_CALIBRAT_MIDNUM_BIT		    24
#define DFT_CALIBRAT_MIDNUM_WID		    6
//Bit 29:24, reg_isp_dft_calibrat_midnum    threshold of number of pixels (5x9 block) located in the [thdlow, thdhig] range, if num of pixels is larger than this midnum, the calibration reference will be updated. 0~45. default=44
//Bit 23:22, reserved      
#define DFT_CALIBRAT_TMIDNUM_BIT	    21
#define DFT_CALIBRAT_TMIDNUM_WID	    6
//Bit 21:16, reg_isp_dft_calibrat_tmidnum   threshold of number of pixels (4x9 block) located in the [thdlow, thdhig] range, if num of pixels is larger than this midnum, the calibration reference will be updated. 0~36. default=35 
//Bit 15:10, reserved
#define DFT_LASTVALID_MODE_BIT		    8
#define DFT_LASTVALID_MODE_WID		    2
//Bit  9: 8, reg_isp_dft_lastvalid_mode     current phase color valid (not defect) status mode. it will be used in calibration update decision. 0: only current line; 1: n-2/n/n+2 lines; 2/3:n-4/n-2/n/n+2/n+4 lines; default = 1
//Bit  7: 6, reserved
#define DFT_LASTVALID_TMODE_BIT		    4
#define DFT_LASTVALID_TMODE_WID		    2
//Bit  5: 4, reg_isp_dft_lastvalid_tmode    top phase color valid (not defect) status mode. it will be used in calibration update decision.0: not update; 1: n-1/n+1 lines; 2/3:n-3/n-1/n+1/n+3 lines; default = 1
//Bit  3: 2, reserved
#define DFT_CALIBRAT_MODE_BIT		    0
#define DFT_CALIBRAT_MODE_WID		    2
//Bit  1: 0, reg_isp_dft_calibrat_mode      calibration reference selection mode. 0: from reg_isp_dft_calibrat_ref_r/g/b; 1: from local average of current phase if it is not dft; 
// 2: from local average of both current and other phases if it was not dft; 3:from local average of both current and other phases whether it was not dft; default=2

#define ISP_DFT_DET0_MANUALTH              0x2d38   
//Bit 31:24, reserved
#define DFT_MANUAL_THRD_R_BIT		   16
#define DFT_MANUAL_THRD_R_WID		   8
//Bit 23:16  reg_isp_dft_manual_thrd_r      threshold of difference between pixel to calibration reference in red channel under reg_isp_dft_detect_mode=0 (manual mode). default= 80;
#define DFT_MANUAL_THRD_G_BIT		   8
#define DFT_MANUAL_THRD_G_WID		   8
//Bit 15: 8, reg_isp_dft_manual_thrd_g      threshold of difference between pixel to calibration reference in green channel under reg_isp_dft_detect_mode=0 (manual mode). default= 80;
#define DFT_MANUAL_THRD_B_BIT		   0
#define DFT_MANUAL_THRD_B_WID		   8
//Bit  7: 0, reg_isp_dft_manual_thrd_b      threshold of difference between pixel to calibration reference in blue channel under reg_isp_dft_detect_mode=0 (manual mode). default= 80;

#define ISP_DFT_DET1_ADPTLOWTH             0x2d39  
//Bit 31:24, reserved
#define DFT_LOW_THRD_R_BIT		   16
#define DFT_LOW_THRD_R_WID		   8
//Bit 23:16  reg_isp_dft_low_thrd_r         low threshold to   red channel channel to decide dead pixel under reg_isp_dft_detect_mode=1 (adptive mode). default= 50;
#define DFT_LOW_THRD_G_BIT		   8
#define DFT_LOW_THRD_G_WID		   8
//Bit 15: 8, reg_isp_dft_low_thrd_g         low threshold to green channel channel to decide dead pixel under reg_isp_dft_detect_mode=1 (adptive mode). default= 50;
#define DFT_LOW_THRD_B_BIT		   0
#define DFT_LOW_THRD_B_WID		   8
//Bit  7: 0, reg_isp_dft_low_thrd_b         low threshold to  blue channel channel to decide dead pixel under reg_isp_dft_detect_mode=1 (adptive mode). default= 50;

#define ISP_DFT_DET1_ADPTHIGTH             0x2d3a   
//Bit 31:24, reserve
#define DFT_HIG_THRD_R_BIT		   16
#define DFT_HIG_THRD_R_WID		   8
//Bit 23:16  reg_isp_dft_hig_thrd_r         high threshold to   red channel channel to decide dead pixel under reg_isp_dft_detect_mode=1 (adptive mode). default= 200;
#define DFT_HIG_THRD_G_BIT		   8
#define DFT_HIG_THRD_G_WID		   8
//Bit 15: 8, reg_isp_dft_hig_thrd_g         high threshold to green channel channel to decide dead pixel under reg_isp_dft_detect_mode=1 (adptive mode). default= 200;
#define DFT_HIG_THRD_B_BIT		   0
#define DFT_HIG_THRD_B_WID		   8
//Bit  7: 0, reg_isp_dft_hig_thrd_b         high threshold to  blue channel channel to decide dead pixel under reg_isp_dft_detect_mode=1 (adptive mode). default= 200;

#define ISP_DFT_DET1_ADPTNUM0              0x2d3b  
//Bit 31:22, reserved
#define DFT_ADAPT_NUM_LOW_BIT		   16
#define DFT_ADAPT_NUM_LOW_WID		   6
//Bit 21:16  reg_isp_dft_adapt_num_low      threshold of number of pixels smaller than thdlow in 5x9 window to decide defect pixel under reg_isp_dft_detect_mode=1 (adptive mode). 0~46; default= 4;
//Bit 15:14, reserved
#define DFT_ADAPT_NUM_MID_BIT		   8
#define DFT_ADAPT_NUM_MID_WID		   6
//Bit 13: 8, reg_isp_dft_adapt_num_mid      threshold of number of pixels within [thdlow, thdhig] in 5x9 window to decide defect pixel under reg_isp_dft_detect_mode=1 (adptive mode). 0~46; default= 4;
//Bit  7: 6, reserved
#define DFT_ADAPT_NUM_HIG_BIT		   0
#define DFT_ADAPT_NUM_HIG_WID		   6
//Bit  5: 0, reg_isp_dft_adapt_num_hig      threshold of number of pixels larger than thdhig in 5x9 window to decide defect pixel under reg_isp_dft_detect_mode=1 (adptive mode). 0~46; default= 4;

#define ISP_DFT_DET1_ADPTNUM1              0x2d3c   
//Bit 31:22, reserved
#define DFT_ADAPT_NUM_TLOW_BIT		   16
#define DFT_ADAPT_NUM_TLOW_WID		   6
//Bit 21:16  reg_isp_dft_adapt_num_tlow     threshold of number of pixels smaller than tthdlow in 4x9 window to decide defect pixel under reg_isp_dft_detect_mode=1 (adptive mode). 0~37; default= 3;
//Bit 15:14, reserved
#define DFT_ADAPT_NUM_TMID_BIT		   8
#define DFT_ADAPT_NUM_TMID_WID		   6
//Bit 13: 8, reg_isp_dft_adapt_num_tmid     threshold of number of pixels within [tthdlow, tthdhig] in 5x9 window to decide defect pixel under reg_isp_dft_detect_mode=1 (adptive mode). 0~37; default= 3;
//Bit  7: 6, reserved
#define DFT_ADAPT_NUM_THIG_BIT		   0
#define DFT_ADAPT_NUM_THIG_WID		   6
//Bit  5: 0, reg_isp_dft_adapt_num_thig     threshold of number of pixels larger than tthdhig in 4x9 window to decide defect pixel under reg_isp_dft_detect_mode=1 (adptive mode). 0~37; default= 3;

// address 0x2d3d~ 0x2d3e null
/************************ demosaicing registers*************************/
#define ISP_DMS_CTRL0                      0x2d40  
//Bit    31, reserved
#define ISP_DMS_BYPASS_BIT		   28
#define ISP_DMS_BYPASS_WID		   3
//Bit 30:28, reg_isp_dms_bypass             bypass of demosaicing module. 0: no bypass; 1: replace (n-2) line dms result to current line; 2: (n-1) line to current; 3: (n+1) line to current; 4: (n+2) lines to current; 5~6: (n)line tocurrent; 7: bypass dms and put bayer data to RGB channels as repeat; default =0;
//Bit 27:26, reserved
#define DMS_BAYER_FMT_BIT		   24//bayer fmt 0:BGGR 1:RGGB 2:GBRG 3:GRBG
#define DMS_BAYER_FMT_WID		   2
//Bit    24, reg_isp_dms_yphase_ofst        bayer pattern yphase offset in demosaicing, 0- vertically start from G(B)/R(G); 1- vertially start from B(G)/G(R); default=0
//Bit 23:22, reserved                       
#define DMS_L28_SIMPLE_BIT		   20
#define DMS_L28_SIMPLE_WID		   2
//Bit 21:20, reg_isp_dms_l28_simple         line 2 and line 8 green interpolation mode. 0/2: full dlmmse; 1:  using 4/6 line's alpha to replace; 2:simple alpha calculation. default= 0
//Bit 19:18, reserved                       
#define DMS_L19_SIMPLE_BIT		   16
#define DMS_L19_SIMPLE_WID		   2
//Bit 17:16, reg_isp_dms_l19_simple         line 1 and line 9 green interpolation mode. 0: using 2/8 line's green to replace; 1: using left/right green pixel to replace; 2: average of left/right and 2/8 line's green; 3:smaller transition directional nearby green average; default=3 
//Bit 15: 1, reserved
#define DMS_DVDH_FILTER_MODE_BIT           0
#define DMS_DVDH_FILTER_MODE_WID	   1
//Bit     0, reg_isp_dms_dvdh_filter_mode   filter mode for dh/dv calculation. 0: [1 0 1]/2; 1: [-1 2 2 2 -1]/4; default = 0.

#define ISP_DMS_CTRL1                      0x2d41  
//Bit 31:30, reserved
#define DMS_GRN_USE_CDM_BIT		   28
#define DMS_GRN_USE_CDM_WID		   2
//Bit 29:28, reg_isp_dms_grn_use_cdm        dms 1st step green recovery fallback mode. 0: ussing dlmmse; 1: simple cdm; 2; simple min gradient; 3: simple average; default = 0;
//Bit 27:26, reserved
#define DMS_DNR_MODE_BIT		   24
#define DMS_DNR_MODE_WID		   2
//Bit 25:24, reg_isp_dms_dnr_mode           denoise mode in greem recovery est_h/est_v calculation for high frequency component; 0: no denoise; 1: high-freq degrade by half; 2: high-freq degrade to 1/4; 3: remove high-freq; default=0
//Bit 23:21, reserved                       
#define DMS_RB_MIN_ENABLE_BIT		   20
#define DMS_RB_MIN_ENABLE_WID		   1
//Bit    20, reg_isp_dms_rb_min_enable      enable signal to use min transition directional dms. 0: always average; 1: adaptive directional; default=1;
//Bit 19:18, reserved                       
#define DMS_RB_MIN_RATE_BIT		   16
#define DMS_RB_MIN_RATE_WID		   2
//Bit 17:16, reg_isp_dms_rb_min_rate        minimum transition (after offset) to maximum transition ratio (./4) threshold to decide using min direction dms. 0~3. default = 2
#define DMS_RB_MIN_OFST_BIT		   8
#define DMS_RB_MIN_OFST_WID		   8
//Bit 15: 8, reg_isp_dms_rb_min_ofst        minimum transition offset (s8) to decide whether using min direction dms or average. if reg_ISP_dms_RB_min_rate=127, will use average. -128~127; default= 5
#define DMS_G_ALP_OFSET_BIT		   0
#define DMS_G_ALP_OFSET_WID		   8
//Bit  7: 0, reg_isp_dms_g_alp_ofset        offset to alpha in lmmse algorithm green component recovery. the larger of this value, the more blender; default = 16

/********************* isp color matrix 0 registers*********************/
#define ISP_MATRIX_PRE_OFST0_1             0x2d42   
//Bit 31:27, reserved
#define MATRIX_PRE_OFST0_BIT	           16
#define MATRIX_PRE_OFST0_WID		   11
//Bit 26:16, reg_isp_matrix_pre_ofst0       input offset of component 0 (most likely red) before going to 3x3 matrix; -1024:1023. default=0
//Bit 15:11, reserved
#define MATRIX_PRE_OFST1_BIT		   0
#define MATRIX_PRE_OFST1_WID		   11
//Bit 10: 0, reg_isp_matrix_pre_ofst1       input offset of component 1 (most likely green) before going to 3x3 matrix; -1024:1023. default=0

#define ISP_MATRIX_PRE_OFST2               0x2d43  
//Bit 31:18, reserved
#define MATRIX_RS_BIT			   16
#define MATRIX_RS_WID			   2
//Bit 17:16, reg_isp_matrix_rs              default=0                        
//Bit 15:11, reserved
#define MATRIX_PRE_OFST2_BIT		   0
#define MATRIX_PRE_OFST2_WID		   11
//Bit 10: 0, reg_isp_matrix_pre_ofst2       input offset of component 2 (most likely blue) before going to 3x3 matrix; -1024:1023. default=0

#define ISP_MATRIX_COEF00_01               0x2d44  
//Bit 31:30, reserved
#define MATRIX_COEF00_BIT		   16
#define MATRIX_COEF00_WID		   10
//Bit 25:16, reg_isp_matrix_coef00          3x3 matrix (0,0) coef; -512:511. default= 77.
//Bit 15:10, reserved
#define MATRIX_COEF01_BIT		   0
#define MATRIX_COEF01_WID		   10
//Bit  9: 0, reg_isp_matrix_coef01          3x3 matrix (0,1) coef; -512:511. default=150.

#define ISP_MATRIX_COEF02_10               0x2d45  
//Bit 31:30, reserved
#define MATRIX_COEF02_BIT		   16
#define MATRIX_COEF02_WID		   10
//Bit 25:16, reg_isp_matrix_coef02          3x3 matrix (0,2) coef; -512:511. default= 29.
//Bit 15:10, reserved 
#define MATRIX_COEF10_BIT		   0
#define MATRIX_COEF10_WID		   10
//Bit  9: 0, reg_isp_matrix_coef10          3x3 matrix (1,0) coef; -512:511. default=-43.

#define ISP_MATRIX_COEF11_12               0x2d46  
//Bit 31:30, reserved
#define MATRIX_COEF11_BIT		   25
#define MATRIX_COEF11_WID		   10
//Bit 25:16, reg_isp_matrix_coef11          3x3 matrix (1,1) coef; -512:511. default=-85.
//Bit 15:10, reserved
#define MATRIX_COEF12_BIT		   0
#define MATRIX_COEF12_WID		   10
//Bit  9: 0, reg_isp_matrix_coef12          3x3 matrix (1,2) coef; -512:511. default=128.

#define ISP_MATRIX_COEF20_21               0x2d47  
//Bit 31:30, reserved
#define MATRIX_COEF20_BIT		   25
#define MATRIX_COEF20_WID		   10
//Bit 25:16, reg_isp_matrix_coef20          3x3 matrix (2,0) coef; -512:511. default=128.
//Bit 15:10, reserved
#define MATRIX_COEF21_BIT		   0
#define MATRIX_COEF21_WID		   10
//Bit  9: 0, reg_isp_matrix_coef21          3x3 matrix (2,1) coef; -512:511. default=-107.

#define ISP_MATRIX_COEF22                  0x2d48  
//Bit 31:10, reserved
#define MATRIX_COEF22_BIT		   0
#define MATRIX_COEF22_WID		   10
//Bit  9: 0, reg_isp_matrix_coef22          3x3 matrix (2,2) coef; -512:511. default=-21.

#define ISP_MATRIX_POS_OFST0_1             0x2d49   
//Bit 31:27, reserved
#define MATRIX_POS_OFST0_BIT		   16
#define MATRIX_POS_OFST0_WID		   11
//Bit 26:16, reg_isp_matrix_pos_ofst0       output offset of component 0 (most likely Y) before going to 3x3 matrix; -1024:1023. default=0
//Bit 15:11, reserved
#define MATRIX_POS_OFST1_BIT		   0
#define MATRIX_POS_OFST1_WID		   11
//Bit 10: 0, reg_isp_matrix_pos_ofst1       output offset of component 1 (most likely U) before going to 3x3 matrix; -1024:1023. default=512

#define ISP_MATRIX_POS_OFST2               0x2d4a  
//Bit 31:11, reserved
#define MATRIX_POS_OFST2_BIT		   0
#define MATRIX_POS_OFST2_WID		   11
//Bit 10: 0, reg_isp_matrix_pos_ofst2       output offset of component 2 (most likely V) before going to 3x3 matrix; -1024:1023. default=512

// address 0x2d4b~ 0x2d4f null

/******************** noise reduction and peaking registers***********************/
#define ISP_PKNR_HVBLANK_NUM                0x2d50
#define DMS_HBLANK_NUM_BIT		    24
#define DMS_HBLANK_NUM_WID		    8
//Bit 31:24, reg_isp_dms_hblank_num         dms horizontal blank pixel number.  default = 8
#define DMS_VBLANK_NUM_BIT		    16
#define DMS_VBLANK_NUM_WID		    8
//Bit 23:16, reg_isp_dms_vblank_num         dms vertical blank pixel number.   default = 30
#define PKNR_HBLANK_NUM_BIT		    8
#define PKNR_HBLANK_NUM_WID		    8
//Bit 15: 8, reg_pknr_hblank_num            peaking and noise reduction horizontal blank pixel number.  default = 8
#define PKNR_VBLANK_NUM_BIT		    8
#define PKNR_VBLANK_NUM_WID		    8
//Bit  7: 0, reg_pknr_vblank_num            peaking and noise reduction vertical blank pixel number.   default = 30

#define ISP_NR_GAUSSIAN_MODE                0x2d51  
//Bit 31: 5, reserved
#define NR_GAU_YMODE_BIT		    4
#define NR_GAU_YMODE_WID		    1
//Bit     4, reg_nr_gau_ymode               noise reduction luma gaussian filter mode. 0: 3x3 gaussian filter; 1:5x5 gaussian filter. default=1;
//Bit  3: 1, reserved
#define NR_GAU_CMODE_BIT		    0
#define NR_GAU_CMODE_WID		    1
//Bit     0, reg_nr_gau_cmode               noise reduction chroma gaussian filter mode. 0: 3x3 gaussian filter; 1:5x5 gaussian filter. default=1;

#define ISP_PK_HVCON_LPF_MODE               0x2d52  
//Bit 31:30, reserved
#define HCON_HPF_MODE_BIT		    28
#define HCON_HPF_MODE_WID		    2
//Bit 29:28, reg_hcon_hpf_mode              horizontal highpass transition ([-1 2 -1]) detection vertical lowpass filter mode. 0: no vertical filter;1:[1 2 1]'/4 filter; 2/3: [1 2 2 2 1]'/8 filter. default= 2;
//Bit 27:26, reserved
#define HCON_BPF_MODE_BIT		    24
#define HCON_BPF_MODE_WID		    2
//Bit 25:24, reg_hcon_bpf_mode              horizontal bandpass transition ([-1 1 0]+[0 1 -1])detection vertical lowpass filter mode. 0: no vertical filter;1:[1 2 1]'/4 filter; 2/3: [1 2 2 2 1]'/8 filter. default= 2;
//Bit 23:22, reserved
#define HCON_LBPF_MODE_BIT		    21
#define HCON_LBPF_MODE_WID		    2
//Bit 21:20, reg_hcon_lbpf_mode             horizontal low-bandpass transition ([-2 0 2])detection vertical lowpass filter mode. 0: no vertical filter;1:[1 2 1]'/4 filter; 2/3: [1 2 2 2 1]'/8 filter. default= 2;
//Bit 19:18, reserved
#define HCON_LLBPF_MODE_BIT		    16
#define HCON_LLBPF_MODE_WID		    2
//Bit 17:16, reg_hcon_llbpf_mode            horizontal very low-bandpass transition ([1 1 0  1 1])detection vertical lowpass filter mode. 0: no vertical filter;1:[1 2 1]'/4 filter; 2/3: [1 2 2 2 1]'/8 filter. default= 2;
//Bit 15:14, reserved
#define VCON_HPF_MODE_BIT		    12
#define VCON_HPF_MODE_WID		    2
//Bit 13:12, reg_vcon_hpf_mode              vertical highpass transition ([-1 2 -1]) detection horizontal lowpass filter mode. 0: no horizontal filter;1:[1 2 1]/4 filter; 2: [1 2 2 2 1]/8 filter; 3: [1 2 3 4 3 2 1]/16 filter. default= 2;
//Bit 11:10, reserved                       
#define VCON_BPF_MODE_BIT		    8
#define VCON_BPF_MODE_WID		    2
//Bit  9: 8, reg_vcon_bpf_mode              vertical bandpass transition ([-1 1 0]+[0 1 -1])detection horizontal lowpass filter mode. 0: no horizontal filter;1:[1 2 1]/4 filter; 2: [1 2 2 2 1]/8 filter; 3: [1 2 3 4 3 2 1]/16 filter. default= 2;
//Bit  7: 6, reserved                       
#define VCON_LBPF_MODE_BIT		    4
#define VCON_LBPF_MODE_WID		    2
//Bit  5: 4, reg_vcon_lbpf_mode             vertical low-bandpass transition ([-2 0 2])detection vertical horizontal filter mode. 0: no horizontal filter;1:[1 2 1]/4 filter; 2: [1 2 2 2 1]/8 filter; 3: [1 2 3 4 3 2 1]/16 filter. default= 2;
//Bit  3: 2, reserved                       
#define VCON_LLBPF_MODE_BIT		    0
#define VCON_LLBPF_MODE_WID		    2
//Bit  1: 0, reg_vcon_llbpf_mode            vertical very low-bandpass transition ([1 1 0  1 1])detection horizontal lowpass filter mode. 0: no horizontal filter;1:[1 2 1]/4 filter; 2: [1 2 2 2 1]/8 filter; 3: [1 2 3 4 3 2 1]/16 filter. default= 2;

#define ISP_PK_CON_BLEND_GAIN               0x2d53 
#define PK_HPCON_HPGAIN_BIT		    28
#define PK_HPCON_HPGAIN_WID		    4
//Bit 31:28, reg_pk_hpcon_hpgain            gain to con_hp (blended from hcon_hp, vcon_hp) to get hpcon. normalized 8 to '1'; default =4
#define PK_HPCON_BPGAIN_BIT		    24
#define PK_HPCON_BPGAIN_WID		    4
//Bit 27:24, reg_pk_hpcon_bpgain            gain to con_bp (blended from hcon_bp, vcon_bp) to get hpcon. normalized 8 to '1'; default =4
#define PK_HPCON_LBPGAIN_BIT		    20
#define PK_HPCON_LBPGAIN_WID		    4
//Bit 23:20, reg_pk_hpcon_lbpgain           gain to con_lbp (blended from hcon_lbp, vcon_lbp) to get hpcon. normalized 8 to '1'; default =0
#define PK_HPCON_LLBPGAIN_BIT	            16
#define PK_HPCON_LLBPGAIN_WID		    4
//Bit 19:16, reg_pk_hpcon_llbpgain          gain to con_llbp (blended from hcon_llbp, vcon_llbp) to get hpcon. normalized 8 to '1'; default =0
#define PK_BPCON_HPGAIN_BIT		    12
#define PK_BPCON_HPGAIN_WID		    4
//Bit 15:12, reg_pk_bpcon_hpgain            gain to con_hp (blended from hcon_hp, vcon_hp) to get bpcon. normalized 8 to '1'; default =0
#define PK_BPCON_BPGAIN_BIT		    8
#define PK_BPCON_BPGAIN_WID		    4
//Bit 11: 8, reg_pk_bpcon_bpgain            gain to con_bp (blended from hcon_bp, vcon_bp) to get bpcon. normalized 8 to '1'; default =2
#define PK_BPCON_LBPGAIN_BIT	            4
#define PK_BPCON_LBPGAIN_WID		    4
//Bit  7: 4, reg_pk_bpcon_lbpgain           gain to con_lbp (blended from hcon_lbp, vcon_lbp) to get bpcon. normalized 8 to '1'; default =6
#define PK_BPCON_LLBPGAIN_BIT	       	    0
#define PK_BPCON_LLBPGAIN_WID		    4
//Bit  3: 0, reg_pk_bpcon_llbpgain          gain to con_llbp (blended from hcon_llbp, vcon_llbp) to get bpcon. normalized 8 to '1'; default =0


#define ISP_PK_CON_2CIRHPGAIN_TH_RATE       0x2d54  
//reg_pk_cirhp_con2gain (0 1 5 6)
#define PK_CIRHP_CON2GAIN_TH0_BIT	    24
#define PK_CIRHP_CON2GAIN_TH0_WID	    8
//Bit 31:24, reg_pk_cirhp_con2gain_th0      threshold0 of curve to map hpcon to hpgain for circle hp filter (all 8 direction same). 0~255. default = 25;
#define PK_CIRHP_CON2GAIN_TH1_BIT	    16
#define PK_CIRHP_CON2GAIN_TH1_WID           8
//Bit 23:16, reg_pk_cirhp_con2gain_th1      threshold1 of curve to map hpcon to hpgain for circle hp filter (all 8 direction same). 0~255. default = 60;
#define PK_CIRHP_CON2GAIN_RATE0_BIT	    8
#define PK_CIRHP_CON2GAIN_RATE0_WID	    8
//Bit 15: 8, reg_pk_cirhp_con2gain_rate0    rate0 (for hpcon<th0) of curve to map hpcon to hpgain for circle hp filter (all 8 direction same). 0~255. default = 80;
#define PK_CIRHP_CON2GAIN_RATE1_BIT	    0
#define PK_CIRHP_CON2GAIN_RATE1_WID	    8
//Bit  7: 0, reg_pk_cirhp_con2gain_rate1    rate1 (for hpcon>th1) of curve to map hpcon to hpgain for circle hp filter (all 8 direction same). 0~255. default = 20;

#define ISP_PK_CON_2CIRHPGAIN_LIMIT         0x2d55   
//reg_pk_cirhp_con2gain (2 3 4)
#define PK_CIRHP_CON2GAIN_LV0_BIT	    24
#define PK_CIRHP_CON2GAIN_LV0_WID	    8
//Bit 31:24, reg_pk_cirhp_con2gain_lv0      level limit(for hpcon<th0) of curve to map hpcon to hpgain for circle hp filter (all 8 direction same). 0~255. default = 0;
#define PK_CIRHP_CON2GAIN_LV1_BIT	    16
#define PK_CIRHP_CON2GAIN_LV1_WID	    8
//Bit 23:16, reg_pk_cirhp_con2gain_lv1      level limit(for th0<hpcon<th1) of curve to map hpcon to hpgain for circle hp filter (all 8 direction same). 0~255. default = 96;
#define PK_CIRHP_CON2GAIN_LV2_BIT	    8
#define PK_CIRHP_CON2GAIN_LV2_WID	    8
//Bit 15: 8, reg_pk_cirhp_con2gain_lv2      level limit(for hpcon>th1) of curve to map hpcon to hpgain for circle hp filter (all 8 direction same). 0~255. default = 5;
//Bit  7: 0, reserved                       

#define ISP_PK_CON_2CIRBPGAIN_TH_RATE       0x2d56   
//reg_pk_cirbp_con2gain (0 1 5 6)
#define PK_CIRBP_CON2GAIN_TH0_BIT	    24
#define PK_CIRBP_CON2GAIN_TH0_WID	    8
//Bit 31:24, reg_pk_cirbp_con2gain_th0      threshold0 of curve to map bpcon to bpgain for circle bp filter (all 8 direction same). 0~255. default = 20;
#define PK_CIRBP_CON2GAIN_TH1_BIT	    16
#define PK_CIRBP_CON2GAIN_TH1_WID	    8
//Bit 23:16, reg_pk_cirbp_con2gain_th1      threshold1 of curve to map bpcon to bpgain for circle bp filter (all 8 direction same). 0~255. default = 50;
#define PK_CIRBP_CON2GAIN_RATE0_BIT	    8
#define PK_CIRBP_CON2GAIN_RATE0_WID	    8
//Bit 15: 8, reg_pk_cirbp_con2gain_rate0    rate0 (for bpcon<th0) of curve to map bpcon to bpgain for circle bp filter (all 8 direction same). 0~255. default = 50;
#define PK_CIRBP_CON2GAIN_RATE1_BIT	    0
#define PK_CIRBP_CON2GAIN_RATE1_WID	    8
//Bit  7: 0, reg_pk_cirbp_con2gain_rate1    rate1 (for bpcon>th1) of curve to map bpcon to bpgain for circle bp filter (all 8 direction same). 0~255. default = 25;

#define ISP_PK_CON_2CIRBPGAIN_LIMIT         0x2d57  
//reg_pk_cirbp_con2gain (2 3 4) 
#define PK_CIRBP_CON2GAIN_LV0_BIT	    24
#define PK_CIRBP_CON2GAIN_LV0_WID	    8
//Bit 31:24, reg_pk_cirbp_con2gain_lv0      level limit(for bpcon<th0) of curve to map bpcon to bpgain for circle bp filter (all 8 direction same). 0~255. default = 0;
#define PK_CIRBP_CON2GAIN_LV1_BIT	    16
#define PK_CIRBP_CON2GAIN_LV1_WID	    8
//Bit 23:16, reg_pk_cirbp_con2gain_lv1      level limit(for th0<bpcon<th1) of curve to map bpcon to bpgain for circle bp filter (all 8 direction same). 0~255. default = 40;
#define PK_CIRBP_CON2GAIN_LV2_BIT	    8
#define PK_CIRBP_CON2GAIN_LV2_WID      	    8
//Bit 15: 8, reg_pk_cirbp_con2gain_lv2      level limit(for bpcon>th1) of curve to map bpcon to bpgain for circle bp filter (all 8 direction same). 0~255. default = 5;
//Bit  7: 0, reserved                       

#define ISP_PK_CON_2DRTHPGAIN_TH_RATE       0x2d58  
//reg_pk_drthp_con2gain (0 1 5 6)
#define PK_DRTHP_CON2GAIN_TH0_BIT	    24
#define PK_DRTHP_CON2GAIN_TH0_WID	    8
//Bit 31:24, reg_pk_drthp_con2gain_th0      threshold0 of curve to map hpcon to hpgain for directional hp filter (best direction). 0~255. default = 25;
#define PK_DRTHP_CON2GAIN_TH1_BIT	    16
#define PK_DRTHP_CON2GAIN_TH1_WID	    8
//Bit 23:16, reg_pk_drthp_con2gain_th1      threshold1 of curve to map hpcon to hpgain for directional hp filter (best direction). 0~255. default = 60;
#define PK_DRTHP_CON2GAIN_RATE0_BIT	    8
#define PK_DRTHP_CON2GAIN_RATE0_WID	    8
//Bit 15: 8, reg_pk_drthp_con2gain_rate0    rate0 (for hpcon<th0) of curve to map hpcon to hpgain for directional hp filter (best direction). 0~255. default = 80;
#define PK_DRTHP_CON2GAIN_RATE1_BIT	    0
#define PK_DRTHP_CON2GAIN_RATE1_WID	    8
//Bit  7: 0, reg_pk_drthp_con2gain_rate1    rate1 (for hpcon>th1) of curve to map hpcon to hpgain for directional hp filter (best direction). 0~255. default = 20;

#define ISP_PK_CON_2DRTHPGAIN_LIMIT         0x2d59   
//reg_pk_drthp_con2gain (2 3 4) 
#define PK_DRTHP_CON2GAIN_LV0_BIT	    24
#define PK_DRTHP_CON2GAIN_LV0_WID	    8
//Bit 31:24, reg_pk_drthp_con2gain_lv0      level limit(for hpcon<th0) of curve to map hpcon to hpgain for directional hp filter (best direction). 0~255. default = 0;
#define PK_DRTHP_CON2GAIN_LV1_BIT	    16
#define PK_DRTHP_CON2GAIN_LV1_WID	    8
//Bit 23:16, reg_pk_drthp_con2gain_lv1      level limit(for th0<hpcon<th1) of curve to map hpcon to hpgain for directional hp filter (best direction). 0~255. default = 96;
#define PK_DRTHP_CON2GAIN_LV2_BIT	    8
#define PK_DRTHP_CON2GAIN_LV2_WID	    8
//Bit 15: 8, reg_pk_drthp_con2gain_lv2      level limit(for hpcon>th1) of curve to map hpcon to hpgain for directional hp filter (best direction). 0~255. default = 5;
//Bit  7: 0, reserved                       

#define ISP_PK_CON_2DRTBPGAIN_TH_RATE       0x2d5a   
//reg_pk_drtbp_con2gain (0 1 5 6) 
#define PK_DRTBP_CON2GAIN_TH0_BIT	    24
#define PK_DRTBP_CON2GAIN_TH0_WID	    8
//Bit 31:24, reg_pk_drtbp_con2gain_th0      threshold0 of curve to map bpcon to bpgain for directional bp filter (best direction). 0~255. default = 20;
#define PK_DRTBP_CON2GAIN_TH1_BIT	    16
#define PK_DRTBP_CON2GAIN_TH1_WID	    8
//Bit 23:16, reg_pk_drtbp_con2gain_th1      threshold1 of curve to map bpcon to bpgain for directional bp filter (best direction). 0~255. default = 50;
#define PK_DRTBP_CON2GAIN_RATE0_BIT	    8
#define PK_DRTBP_CON2GAIN_RATE0_WID	    8
//Bit 15: 8, reg_pk_drtbp_con2gain_rate0    rate0 (for bpcon<th0) of curve to map bpcon to bpgain for directional bp filter (best direction). 0~255. default = 50;
#define PK_DRTBP_CON2GAIN_RATE1_BIT	    0
#define PK_DRTBP_CON2GAIN_RATE1_WID	    8
//Bit  7: 0, reg_pk_drtbp_con2gain_rate1    rate1 (for bpcon>th1) of curve to map bpcon to bpgain for directional bp filter (best direction). 0~255. default = 25;

#define ISP_PK_CON_2DRTBPGAIN_LIMIT         0x2d5b   
//reg_pk_drtbp_con2gain (2 3 4) 
#define PK_DRTBP_CON2GAIN_LV0_BIT	    24
#define PK_DRTBP_CON2GAIN_LV0_WID	    8
//Bit 31:24, reg_pk_drtbp_con2gain_lv0      level limit(for bpcon<th0) of curve to map bpcon to bpgain for directional bp filter (best direction). 0~255. default = 0;
#define PK_DRTBP_CON2GAIN_LV1_BIT 	    16
#define PK_DRTBP_CON2GAIN_LV1_WID	    8
//Bit 23:16, reg_pk_drtbp_con2gain_lv1      level limit(for th0<bpcon<th1) of curve to map bpcon to bpgain for directional bp filter (best direction). 0~255. default = 40;
#define PK_DRTBP_CON2GAIN_LV2_BIT	    8
#define PK_DRTBP_CON2GAIN_LV2_WID	    8
//Bit 15: 8, reg_pk_drtbp_con2gain_lv2      level limit(for bpcon>th1) of curve to map bpcon to bpgain for directional bp filter (best direction). 0~255. default = 5;
//Bit  7: 0, reserved                       

// address 0x2d5c~ 0x2d5f null

#define ISP_PK_CIRFB_LPF_MODE               0x2d60  
//Bit 31:30, reserved
#define CIRHP_HORZ_FLT_MODE_BIE		    28
#define CIRHP_HORZ_FLT_MODE_WID		    2
//Bit 29:28, reg_cirhp_horz_flt_mode        vertical highpass filter ([-1 2 -1]') horizontal lpf mode for circle hpf calculation. 0: no lpf filter;1:[1 2 1]/4 filter; 2/3: [1 2 2 2 1]/8 filter. default= 1;
//Bit 27:26, reserved
#define CIRHP_VERT_FLT_MODE_BIE		    24
#define CIRHP_VERT_FLT_MODE_WID		    2
//Bit 25:24, reg_cirhp_vert_flt_mode        horizontal highpass filter ([-1 2 -1]) vertical lpf mode  for circle hpf calculation. 0: no lpf filter;1:[1 2 1]'/4 filter; 2/3: [1 2 2 2 1]'/8 filter. default= 1;
//Bit 23:21, reserved                       
#define CIRHP_DIAG_FLT_MODE_BIE		    20
#define CIRHP_DIAG_FLT_MODE_WID		    1
//Bit    20, reg_cirhp_diag_flt_mode        diagonal highpass filter crossing lpf mode for circle hpf calculaton. 0: no lpf filter; 1:[1 2 1]/4 filter; default =1;
//Bit 19:14, reserved
#define CIRBP_HORZ_FLT_MODE_BIT		    12
#define CIRBP_HORZ_FLT_MODE_WID		    2
//Bit 13:12, reg_cirbp_horz_flt_mode        vertical bandpass filte ([-3 1 4 1 -3]') horizontal lpf mode for circle bpf calculation. 0: no lpf filter;1:[1 2 1]/4 filter; 2/3: [1 2 2 2 1]/8 filter. default= 1;
//Bit 11:10, reserved                       
#define CIRBP_VERT_FLT_MODE_BIT		    8
#define CIRBP_VERT_FLT_MODE_WID		    2
//Bit  9: 8, reg_cirbp_vert_flt_mode        horizontal bandpass filte ([-3 1 4 1 -3]) vertical lpf mode  for circle bpf calculation. 0: no lpf filter;1:[1 2 1]'/4 filter; 2/3: [1 2 2 2 1]'/8 filter. default= 1;
//Bit  7: 5, reserved                     
#define CIRBP_DIAG_FLT_MODE_BIT		    4
#define CIRBP_DIAG_FLT_MODE_WID		    1
//Bit     4, reg_cirbp_diag_flt_mode        diagonal bandpass filter crossing lpf mode for circle bpf calculaton. 0: no lpf filter; 1:[1 2 1]/4 filter; default =1;
//Bit  3: 0, reserved                       

#define ISP_PK_DRTFB_LPF_MODE               0x2d61  
//Bit 31:30, reserved
#define DRTHP_HORZ_FLT_MODE_BIT		    28
#define DRTHP_HORZ_FLT_MODE_WID		    2
//Bit 29:28, reg_drthp_horz_flt_mode        vertical highpass filter ([-1 2 -1]') horizontal lpf mode for directional hpf calculation. 0: no lpf filter;1:[1 2 1]/4 filter; 2/3: [1 2 2 2 1]/8 filter. default= 2;
//Bit 27:26, reserved
#define DRTHP_VERT_FLT_MODE_BIT		    24
#define DRTHP_VERT_FLT_MODE_WID		    2
//Bit 25:24, reg_drthp_vert_flt_mode        horizontal highpass filter ([-1 2 -1]) vertical lpf mode  for directional hpf calculation. 0: no lpf filter;1:[1 2 1]'/4 filter; 2/3: [1 2 2 2 1]'/8 filter. default= 2;
//Bit 23:21, reserved                       
#define DRTHP_DIAG_FLT_MODE_BIT		    20
#define DRTHP_DIAG_FLT_MODE_WID		    1
//Bit    20, reg_drthp_diag_flt_mode        diagonal highpass filter crossing lpf mode for directional hpf calculaton. 0: no lpf filter; 1:[1 2 1]/4 filter; default =1;
//Bit 19:14, reserved
#define DRTBP_HORZ_FLT_MODE_BIT		    12
#define DRTBP_HORZ_FLT_MODE_MODE	    2
//Bit 13:12, reg_drtbp_horz_flt_mode        vertical bandpass filte ([-3 1 4 1 -3]') horizontal lpf mode for directional bpf calculation. 0: no lpf filter;1:[1 2 1]/4 filter; 2/3: [1 2 2 2 1]/8 filter. default= 2;
//Bit 11:10, reserved
#define DRTBP_VERT_FLT_MODE_BIT		    8
#define DRTBP_VERT_FLT_MODE_MODE	    2
//Bit  9: 8, reg_drtbp_vert_flt_mode        horizontal bandpass filte ([-3 1 4 1 -3]) vertical lpf mode  for directional bpf calculation. 0: no lpf filter;1:[1 2 1]'/4 filter; 2/3: [1 2 2 2 1]'/8 filter. default= 2;
//Bit  7: 5, reserved
#define DRTBP_DIAG_FLT_MODE_BIE		    4
#define DRTBP_DIAG_FLT_MODE_WID		    1
//Bit     4, reg_drtbp_diag_flt_mode        diagonal bandpass filter crossing lpf mode for directional bpf calculaton. 0: no lpf filter; 1:[1 2 1]/4 filter; default =1;
//Bit  3: 0, reserved                       

#define ISP_PK_CIRFB_HP_CORING              0x2d62  
//Bit 31:22, reserved
#define CIRHP_HORZ_CORE_BIT		    16
#define CIRHP_HORZ_CORE_MODE		    6
//Bit 21:16, reg_cirhp_horz_core            coring to vertical highpass filter ([-1 2 -1]') in circle hpf calculation. 0~63. default= 20;
//Bit 15:14, reserved
#define CIRHP_VERT_CORE_BIT		    13
#define CIRHP_VERT_CORE_MODE		    6
//Bit 13: 8, reg_cirhp_vert_core            coring to horizontal highpass filter ([-1 2 -1])  in circle hpf calculation. 0~63. default= 20;
//Bit  7: 6, reserved
#define CIRHP_DIAG_CORE_BIT		    0
#define CIRHP_DIAG_CORE_WID		    6
//Bit  5: 0, reg_cirhp_diag_core            coring to diagonal highpass filter ([-1 2 -1])  in circle hpf calculation. 0~63. default= 20;

#define ISP_PK_CIRFB_BP_CORING              0x2d63  
//Bit 31:22, reserved
#define CIRBP_HORZ_CORE_BIT		    16
#define CIRBP_HORZ_CORE_WID		    6
//Bit 21:16, reg_cirbp_horz_core            coring to vertical bandpass filter ([-1 2 -1]') in circle bpf calculation. 0~63. default= 15;
//Bit 15:14, reserved                       
#define CIRBP_VERT_CORE_BIT		    8
#define CIRBP_VERT_CORE_WID		    6
//Bit 13: 8, reg_cirbp_vert_core            coring to horizontal bandpass filter ([-1 2 -1])  in circle bpf calculation. 0~63. default= 15;
//Bit  7: 6, reserved                       
#define CIRBP_DIAG_CORE_BIT		    0
#define CIRBP_DIAG_CORE_WID		    6
//Bit  5: 0, reg_cirbp_diag_core            coring to diagonal bandpass filter ([-1 2 -1])  in circle bpf calculation. 0~63. default= 15;

#define ISP_PK_DRTFB_HP_CORING              0x2d64  
//Bit 31:22, reserved
#define DRTHP_HORZ_CORE_BIT		    16
#define DRTHP_HORZ_CORE_WID		    6
//Bit 21:16, reg_drthp_horz_core            coring to vertical highpass filter ([-1 2 -1]') in directional hpf calculation. 0~63. default= 20;
//Bit 15:14, reserved
#define DRTHP_VERT_CORE_BIT		    13
#define DRTHP_VERT_CORE_WID		    6
//Bit 13: 8, reg_drthp_vert_core            coring to horizontal highpass filter ([-1 2 -1])  in directional hpf calculation. 0~63. default= 20;
//Bit  7: 6, reserved
#define DRTHP_DIAG_CORE_BIT		    0
#define DRTHP_DIAG_CORE_WID		    6
//Bit  5: 0, reg_drthp_diag_core            coring to diagonal highpass filter ([-1 2 -1])  in directional hpf calculation. 0~63. default= 20;

#define ISP_PK_DRTFB_BP_CORING              0x2d65  
//Bit 31:22, reserved
#define DRTBP_HORZ_CORE_BIT		    16
#define DRTBP_HORZ_CORE_WID		    6
//Bit 21:16, reg_drtbp_horz_core            coring to vertical bandpass filter ([-1 2 -1]') in directional bpf calculation. 0~63. default= 15;
//Bit 15:14, reserved
#define DRTBP_VERT_CORE_BIT                 8
#define DRTBP_VERT_CORE_WID		    6
//Bit 13: 8, reg_drtbp_vert_core            coring to horizontal bandpass filter ([-1 2 -1])  in directional bpf calculation. 0~63. default= 15;
//Bit  7: 6, reserved
#define DRTBP_DIAG_CORE_BIT		    0
#define DRTBP_DIAG_CORE_WID		    6
//Bit  5: 0, reg_drtbp_diag_core            coring to diagonal bandpass filter ([-1 2 -1])  in directional bpf calculation. 0~63. default= 15;

#define ISP_PK_CIRFB_BLEND_GAIN             0x2d66  
#define HP_CIR_HGAIN_BIT		    28
#define HP_CIR_HGAIN_WID		    4
//Bit 31:28, reg_hp_cir_hgain               vertical hpf gain for hp_cir calculation. 0~15. normalize 8 as '1';  default = 8;
#define HP_CIR_VGAIN_BIT		    24
#define HP_CIR_VGAIN_WID		    4
//Bit 27:24, reg_hp_cir_vgain               horizontal hpf gain for hp_cir calculation. 0~15. normalize 8 as '1'; default = 8;
#define HP_CIR_DGAIN_BIT		    20
#define HP_CIR_DGAIN_WID	            4
//Bit 23:20, reg_hp_cir_dgain               diagonal hpf gain for hp_cir calculation. 0~15. normalize 8 as '1'; default = 8;
//Bit 19:16, reserved                       
#define BP_CIR_HGAIN_BIT		    12
#define BP_CIR_HGAIN_WID		    4
//Bit 15:12, reg_bp_cir_hgain               vertical bpf gain for bp_cir calculation. 0~15. normalize 8 as '1';  default = 8;
#define BP_CIR_VGAIN_BIT	            11
#define BP_CIR_VGAIN_WID		    4
//Bit 11: 8, reg_bp_cir_vgain               horizontal bpf gain for bp_cir calculation. 0~15. normalize 8 as '1'; default = 8;
#define BP_CIR_DGAIN_BIT		    4
#define BP_CIR_DGAIN_WID		    4
//Bit  7: 4, reg_bp_cir_dgain               diagonal bpf gain for bp_cir calculation. 0~15. normalize 8 as '1'; default = 8;
//Bit  3: 0, reserved                       

// address 0x2d67~ 0x2d67 null

//nr_alp0 is for blending of gaussian and original results
#define ISP_NR_ALPY_SSD_GAIN_OFST           0x2d68  
//Bit 31:15, reserved
#define NR_ALP0_SSD_GAIN_BIT	            8
#define NR_ALP0_SSD_GAIN_WID	            8
//Bit 15: 8, reg_nr_alp0_ssd_gain           gain to signed sum difference (SSD for transition detection) for alpha0 calculation. the smaller of this value, the more blur of image (more Gaussian results). 0~255, normalized 16 as '1'; default= 16     
//Bit  7: 6, reserved
#define NR_ALP0_SSD_OFST_BIT	            0
#define NR_ALP0_SSD_OFST_WID		    6
//Bit  5: 0, reg_nr_alp0_ssd_ofst           ofsset to SSD before dividing to min_err. the smaller of this value, the more blur of image (more Gaussian results); -32:31. default= -2;

#define ISP_NR_ALP0Y_ERR2CURV_TH_RATE       0x2d69  
//reg_nr_alp0_minerr_ypar (0 1 5 6)
#define NR_ALP0_MINERR_YPAR_TH0_BIT         24
#define NR_ALP0_MINERR_YPAR_TH0_WID	    8
//Bit 31:24, reg_nr_alp0_minerr_ypar_th0    threshold0 of curve to map mierr to alp0 for luma channel, this will be set value of flat region mierr that no need blur. 0~255. default = 10;
#define NR_ALP0_MINERR_YPAR_TH1_BIT         16
#define NR_ALP0_MINERR_YPAR_TH1_WID	    8
//Bit 23:16, reg_nr_alp0_minerr_ypar_th1    threshold1 of curve to map mierr to alp0 for luma channel,this will be set value of texture region mierr that can not blur. 0~255. default = 25;
#define NR_ALP0_MINERR_YPAR_RATE0_BIT	    8
#define NR_ALP0_MINERR_YPAR_RATE0_WID	    8
//Bit 15: 8, reg_nr_alp0_minerr_ypar_rate0  rate0 (for mierr<th0) of curve to map mierr to alp0 for luma channel. the larger of the value, the deep of the slope. 0~255. default = 80;
#define NR_ALP0_MINERR_YPAR_RATE1_BIT	    0
#define NR_ALP0_MINERR_YPAR_RATE1_WID	    8
//Bit  7: 0, reg_nr_alp0_minerr_ypar_rate1  rate1 (for mierr>th1) of curve to map mierr to alp0 for luma channel. the larger of the value, the deep of the slope. 0~255. default = 64;

//reg_nr_alp0_minerr_ypar (2 3 4)
#define ISP_NR_ALP0Y_ERR2CURV_LIMIT         0x2d6a
#define ALP0_MINERR_YPAR_LV0_BIT	    24
#define ALP0_MINERR_YPAR_LV0_WID	    8
//Bit 31:24, reg_nr_alp0_minerr_ypar_lv0    level limit(for mierr<th0) of curve to map mierr to alp0 for luma channel, this will be set to alp0 that we can do for flat region. 0~255. default = 63;
#define ALP0_MINERR_YPAR_LV1_BIT	    16
#define ALP0_MINERR_YPAR_LV1_WID	    8
//Bit 23:16, reg_nr_alp0_minerr_ypar_lv1    level limit(for th0<mierr<th1) of curve to map mierr to alp0 for luma channel, this will be set to alp0 that we can do for misc region. 0~255. default = 0;
#define ALP0_MINERR_YPAR_LV2_BIT	    8
#define ALP0_MINERR_YPAR_LV2_WID	    8
//Bit 15: 8, reg_nr_alp0_minerr_ypar_lv2    level limit(for mierr>th1) of curve to map mierr to alp0 for luma channel, this will be set to alp0 that we can do for texture region. 0~255. default = 63;
//Bit  7: 0, reserved                                                                   
//reg_nr_alp0_minerr_cpar (0 1 5 6)         
#define ISP_NR_ALP0C_ERR2CURV_TH_RATE       0x2d6b  
#define ALP0_MINERR_CPAR_TH0_BIT	    24
#define ALP0_MINERR_CPAR_TH0_WID	    8
//Bit 31:24, reg_nr_alp0_minerr_cpar_th0    threshold0 of curve to map mierr to alp0 for chroma channel, this will be set value of flat region mierr that no need blur. 0~255. default = 10;
#define ALP0_MINERR_CPAR_TH1_BIT	    16
#define ALP0_MINERR_CPAR_TH1_WID	    8
//Bit 23:16, reg_nr_alp0_minerr_cpar_th1    threshold1 of curve to map mierr to alp0 for chroma channel,this will be set value of texture region mierr that can not blur. 0~255. default = 25;
#define ALP0_MINERR_CPAR_RATE0_BIT	    8
#define ALP0_MINERR_CPAR_RATE0_WID	    8
//Bit 15: 8, reg_nr_alp0_minerr_cpar_rate0  rate0 (for mierr<th0) of curve to map mierr to alp0 for chroma channel. the larger of the value, the deep of the slope. 0~255. default = 80;
#define ALP0_MINERR_CPAR_RATE1_BIT	    0
#define ALP0_MINERR_CPAR_RATE1_WID	    8
//Bit  7: 0, reg_nr_alp0_minerr_cpar_rate1  rate1 (for mierr>th1) of curve to map mierr to alp0 for chroma channel. the larger of the value, the deep of the slope. 0~255. default = 64;

//reg_nr_alp0_minerr_cpar (2 3 4)
#define ISP_NR_ALP0C_ERR2CURV_LIMIT         0x2d6c  
#define ALP0_MINERR_CPAR_LV0_BIT	    24
#define ALP0_MINERR_CPAR_LV0_WID	    8
//Bit 31:24, reg_nr_alp0_minerr_cpar_lv0    level limit(for mierr<th0) of curve to map mierr to alp0 for chroma channel, this will be set to alp0 that we can do for flat region. 0~255. default = 63;
#define ALP0_MINERR_CPAR_LV1_BIT	    16
#define ALP0_MINERR_CPAR_LV1_WID	    8
//Bit 23:16, reg_nr_alp0_minerr_cpar_lv1    level limit(for th0<mierr<th1) of curve to map mierr to alp0 for chroma channel, this will be set to alp0 that we can do for misc region. 0~255. default = 0;
#define ALP0_MINERR_CPAR_LV2_BIT	    8
#define ALP0_MINERR_CPAR_LV2_WID	    8
//Bit 15: 8, reg_nr_alp0_minerr_cpar_lv2    level limit(for mierr>th1) of curve to map mierr to alp0 for chroma channel, this will be set to alp0 that we can do for texture region. 0~255. default = 63;
//Bit  7: 0, reserved                       

#define ISP_NR_ALP0_MIN_MAX                 0x2d6d  
//Bit 31:30, reserved
#define NR_ALP0_YMIN_BIT		    24
#define NR_ALP0_YMIN_WID		    6
//Bit 29:24, reg_nr_alp0_ymin               minumum limit of alp0 for luma channel, if it is 63, means all gaussian lpf result, 0~63, . default = 2;
//Bit 23:22, reserved                       
#define NR_ALP0_YMAX_BIT		    24
#define NR_ALP0_YMAX_WID		    6
//Bit 21:16, reg_nr_alp0_ymax               maximum limit of alp0 for luma channel, if it is  0, means all orginal result, 0~63, . default = 63;
//Bit 15:14, reserved
#define NR_ALP0_CMIN_BIT		    8
#define NR_ALP0_CMIN_WID		    6
//Bit 13: 8, reg_nr_alp0_cmin               minumum limit of alp0 for chroma channel, if it is 63, means all gaussian lpf result, 0~63, . default = 2;
//Bit  7: 6, reserved
#define NR_ALP0_CMAX_BIT		    0
#define NR_ALP0_CMAX_WID		    6
//Bit  5: 0, reg_nr_alp0_cmax               maximum limit of alp0 for chroma channel, if it is  0, means all orginal result, 0~63, . default = 63;


#define ISP_NR_ALP1_MIERR_CORING            0x2d6e      
//Bit 31:17, reserved
#define NR_ALP1_MAXERR_MODE_BIT	            16	
#define NR_ALP1_MAXERR_MODE_WID		    1
//Bit    16, reg_nr_alp1_maxerr_mode        mxerr select mode. 0: max_err of eight directions; 1: crossing direction of min_err direction. default=0;
//Bit 15:14, reserved
#define NR_ALP1_CORE_RATE_BIT	            8
#define NR_ALP1_CORE_RATE_WID		    6
//Bit 13: 8, reg_nr_alp1_core_rate          rate to (mxerr-min_err) to get coring to min_err. normalized to 64 as '1'. 0~63. default =0;   
//Bit  7: 6, reserved
#define NR_ALP1_CORE_OFST_BIT		    0
#define NR_ALP1_CORE_OFST_WID		    6
//Bit  5: 0, reg_nr_alp1_core_ofst          offset of coring to min_err. normalized to 64 as '1'. 0~63. default =3;

#define ISP_NR_ALP1_ERR2CURV_TH_RATE        0x2d6f  
//reg_nr_alp1_minerr_par (0 1 5 6) 
#define ALP1_MINERR_PAR_TH0_BIT		    24
#define ALP1_MINERR_PAR_TH0_WID		    8
//Bit 31:24, reg_nr_alp1_minerr_par_th0     threshold0 of curve to map mierr to alp1 for luma/chroma channel, this will be set value of flat region mierr that no need directional NR. 0~255. default = 0;
#define ALP1_MINERR_PAR_TH1_BIT		    16
#define ALP1_MINERR_PAR_TH1_WID		    8
//Bit 23:16, reg_nr_alp1_minerr_par_th1     threshold1 of curve to map mierr to alp1 for luma/chroma  channel,this will be set value of texture region mierr that can not do directional NR. 0~255. default = 24;
#define ALP1_MINERR_PAR_RATE0_BIT	    8
#define ALP1_MINERR_PAR_RATE0_WID	    8
//Bit 15: 8, reg_nr_alp1_minerr_par_rate0   rate0 (for mierr<th0) of curve to map mierr to alp1 for luma/chroma  channel. the larger of the value, the deep of the slope. 0~255. default = 0;
#define ALP1_MINERR_PAR_RATE1_BIT	    0
#define ALP1_MINERR_PAR_RATE1_WID	    8
//Bit  7: 0, reg_nr_alp1_minerr_par_rate1   rate1 (for mierr>th1) of curve to map mierr to alp1 for luma/chroma  channel. the larger of the value, the deep of the slope. 0~255. default = 20;

#define ISP_NR_ALP1_ERR2CURV_LIMIT          0x2d70    
//reg_nr_alp1_minerr_par (2 3 4)
#define ALP1_MINERR_PAR_LV0_BIT		    24
#define ALP1_MINERR_PAR_LV0_WID		    8
//Bit 31:24, reg_nr_alp1_minerr_par_lv0     level limit(for mierr<th0) of curve to map mierr to alp1 for luma/chroma  channel, this will be set to alp1 that we can do for flat region. 0~255. default = 0;
#define ALP1_MINERR_PAR_LV1_BIT		    16
#define ALP1_MINERR_PAR_LV1_WID		    8
//Bit 23:16, reg_nr_alp1_minerr_par_lv1     level limit(for th0<mierr<th1) of curve to map mierr to alp1 for luma/chroma  channel, this will be set to alp1 that we can do for misc region. 0~255. default = 16;
#define ALP1_MINERR_PAR_LV2_BIT		    8
#define ALP1_MINERR_PAR_LV2_WID		    8
//Bit 15: 8, reg_nr_alp1_minerr_par_lv2     level limit(for mierr>th1) of curve to map mierr to alp1 for luma/chroma  channel, this will be set to alp1 that we can do for texture region. 0~255. default = 63;
//Bit  7: 0, reserved                       

#define ISP_NR_ALP1_MIN_MAX                 0x2d71  
//Bit 31:30, reserved
#define NR_ALP1_YMIN_BIT		    24
#define NR_ALP1_YMIN_WID		    6
//Bit 29:24, reg_nr_alp1_ymin               minumum limit of alp1 for luma channel, if it is 63, means all directional lpf result, 0~63, . default = 0;
//Bit 23:22, reserved
#define NR_ALP1_YMAX_BIT		    16
#define NR_ALP1_YMAX_WID		    6
//Bit 21:16, reg_nr_alp1_ymax               maximum limit of alp1 for luma channel, if it is  0, means all gaussian blend  result, 0~63, . default = 63;
//Bit 15:14, reserved
#define NR_ALP1_CMIN_BIT		    8
#define NR_ALP1_CMIN_WID		    6
//Bit 13: 8, reg_nr_alp1_cmin               minumum limit of alp1 for chroma channel, if it is 63, means all directonal lpf result, 0~63, . default = 0;
//Bit  7: 6, reserved
#define NR_ALP1_CMAX_BIT		    0
#define NR_ALP1_CMAX_WID		    6
//Bit  5: 0, reg_nr_alp1_cmax               maximum limit of alp1 for chroma channel, if it is  0, means all gaussian blend result, 0~63, . default = 63;

#define ISP_PK_ALP2_MIERR_CORING            0x2d72       
//Bit 31:17, reserved
#define ALP2_MAXERR_MODE_BIT	            16
#define ALP2_MAXERR_MODE_WID		    1
//Bit    16, reg_nr_alp2_maxerr_mode        mxerr select mode for alp2 calculation. 0: max_err of eight directions; 1: crossing direction of min_err direction. default=1;
//Bit 15:14, reserved
#define ALP2_CORE_RATE_BIT		    8
#define ALP2_CORE_RATE_WID		    6
//Bit 13: 8, reg_nr_alp2_core_rate          rate to (mxerr-min_err) to get coring to min_err for alp2 calculation. normalized to 64 as '1'. 0~63. default =13;   
//Bit  7: 6, reserved          
#define ALP2_CORE_OFST_BIT		    0
#define ALP2_CORE_OFST_WID		    6
//Bit  5: 0, reg_nr_alp2_core_ofst          offset of coring to min_err for alp2 calculation. normalized to 64 as '1'. 0~63. default =1;

#define ISP_PK_ALP2_ERR2CURV_TH_RATE        0x2d73  
//reg_nr_alp2_minerr_par (0 1 5 6)
#define ALP2_MINERR_PAR_TH0_BIT		    24
#define ALP2_MINERR_PAR_TH0_WID		    8
//Bit 31:24, reg_nr_alp2_minerr_par_th0     threshold0 of curve to map mierr to alp2 for luma channel, this will be set value of flat region mierr that no need peaking. 0~255. default = 0;
#define ALP2_MINERR_PAR_TH1_BIT		    16
#define ALP2_MINERR_PAR_TH1_WID	            8
//Bit 23:16, reg_nr_alp2_minerr_par_th1     threshold1 of curve to map mierr to alp2 for luma  channel,this will be set value of texture region mierr that can not do peaking. 0~255. default = 24;
#define ALP2_MINERR_PAR_RATE0_BIT	    8
#define ALP2_MINERR_PAR_RATE0_WID	    8
//Bit 15: 8, reg_nr_alp2_minerr_par_rate0   rate0 (for mierr<th0) of curve to map mierr to alp2 for luma  channel. the larger of the value, the deep of the slope. 0~255. default = 0;
#define ALP2_MINERR_PAR_RATE1_BIT	    0
#define ALP2_MINERR_PAR_RATE1_WID	    8
//Bit  7: 0, reg_nr_alp2_minerr_par_rate1   rate1 (for mierr>th1) of curve to map mierr to alp2 for luma  channel. the larger of the value, the deep of the slope. 0~255. default = 20;

#define ISP_PK_ALP2_ERR2CURV_LIMIT          0x2d74   
//reg_nr_alp1_minerr_par (2 3 4)
#define ALP2_MINERR_PAR_LV0_BIT		    24
#define ALP2_MINERR_PAR_LV0_WID		    8
//Bit 31:24, reg_nr_alp2_minerr_par_lv0     level limit(for mierr<th0) of curve to map mierr to alp2 for luma  channel, this will be set to alp2 that we can do for flat region. 0~255. default = 0;
#define ALP2_MINERR_PAR_LV1_BIT		    16
#define ALP2_MINERR_PAR_LV1_WID		    8
//Bit 23:16, reg_nr_alp2_minerr_par_lv1     level limit(for th0<mierr<th1) of curve to map mierr to alp2 for luma  channel, this will be set to alp2 that we can do for misc region. 0~255. default = 16;
#define ALP2_MINERR_PAR_LV2_BIT		    8
#define ALP2_MINERR_PAR_LV2_WID		    8
//Bit 15: 8, reg_nr_alp2_minerr_par_lv2     level limit(for mierr>th1) of curve to map mierr to alp2 for luma  channel, this will be set to alp2 that we can do for texture region. 0~255. default = 63;
//Bit  7: 0, reserved                       

#define ISP_PK_ALP2_MIN_MAX                 0x2d75  
//Bit 31:14, reserved
#define NR_ALP2_MIN_BIT			    8
#define NR_ALP2_MIN_WID			    6
//Bit 13: 8, reg_nr_alp2_min                minumum limit of alp2 for luma channel, if it is 63, means directional peaking result, 0~63, . default = 0;
//Bit  7: 6, reserved
#define NR_ALP2_MIN_BIT			    8
#define NR_ALP2_MIN_WID			    6

//Bit  5: 0, reg_nr_alp2_max                maximum limit of alp2 for luma channel, if it is  0, means all circle peaking result, 0~63, . default = 63;

#define ISP_PK_FINALGAIN_HP_BP              0x2d76  
//Bit 31:16, reserved
#define HP_FINAL_GAIN_BIT	            8
#define HP_FINAL_GAIN_WID		    8
//Bit 15: 8, reg_hp_final_gain              gain to highpass boost result (including directional/circle blending), normalized 32 as '1', 0~255. default = 40;
#define BP_FINAL_GAIN_BIT		    0
#define BP_FINAL_GAIN_WID		    8
//Bit  7: 0, reg_bp_final_gain              gain to bandpass boost result (including directional/circle blending), normalized 32 as '1', 0~255. default = 40;

// address 0x2d77~ 0x2d77 null 

#define ISP_PK_OS_HORZ_CORE_GAIN            0x2d78
#define OS_HSIDE_CORE_BIT		    24
#define OS_HSIDE_CORE_WID		    8
//Bit 31:24, reg_os_hside_core              side coring (not to current pixel) to adaptive overshoot margin in horizontal direction. the larger of this value, the less overshoot admitted 0~255; default= 8;             
#define OS_HSIDE_GAIN_BIT		    16
#define OS_HSIDE_GAIN_WID		    8
//Bit 23:16, reg_os_hside_gain              side gain (not to current pixel) to adaptive overshoot margin in horizontal direction. normalized to 32 as '1'. 0~255; default= 20;     
#define OS_HMIDD_CORE_BIT		    8
#define OS_HMIDD_CORE_WID		    8
//Bit 15: 8, reg_os_hmidd_core              midd coring (to current pixel) to adaptive overshoot margin in horizontal direction. the larger of this value, the less overshoot admitted 0~255; default= 2;             
#define OS_HMIDD_GAIN_BIT		    0
#define OS_HMIDD_GAIN_WID		    8
//Bit  7: 0, reg_os_hmidd_gain              midd gain (to current pixel) to adaptive overshoot margin in horizontal direction. normalized to 32 as '1'. 0~255; default= 20;   

#define ISP_PK_OS_VERT_CORE_GAIN            0x2d79  
#define OS_VSIDE_CORE_BIT		    24
#define OS_VSIDE_CORE_WID		    8
//Bit 31:24, reg_os_vside_core              side coring (not to current pixel) to adaptive overshoot margin in vertical direction. the larger of this value, the less overshoot admitted 0~255; default= 2;             
#define OS_VSIDE_GAIN_BIT		    16
#define OS_VSIDE_GAIN_WID		    8
//Bit 23:16, reg_os_vside_gain              side gain (not to current pixel) to adaptive overshoot margin in vertical direction. normalized to 32 as '1'. 0~255; default= 20;     
#define OS_VMIDD_CORE_BIT		    8
#define OS_VMIDD_CORE_WID		    8
//Bit 15: 8, reg_os_vmidd_core              midd coring (to current pixel) to adaptive overshoot margin in vertical direction. the larger of this value, the less overshoot admitted 0~255; default= 8;             
#define OS_VMIDD_GAIN_BIT	            0
#define OS_VMIDD_GAIN_WID		    8
//Bit  7: 0, reg_os_vmidd_gain              midd gain (to current pixel) to adaptive overshoot margin in vertical direction. normalized to 32 as '1'. 0~255; default= 20;   


#define ISP_PK_OS_ADPT_MISC                 0x2d7a
#define PK_OS_MINERR_CORE_BIT	            24
#define PK_OS_MINERR_CORE_WID	            8
//Bit 31:24, reg_pk_os_minerr_core          coring to minerr for adaptive overshoot margin. the larger of this value, the less overshoot admitted 0~255; default= 40;             
#define PK_OS_MINERR_GAIN_BIT	            16
#define PK_OS_MINERR_GAIN_WID		    8
//Bit 23:16, reg_pk_os_minerr_gain          gain to minerr based adaptive overshoot margin. normalized to 64 as '1'. 0~255; default= 6;     
#define PK_OS_ADPT_MAX_BIT		    8
#define PK_OS_ADPT_MAX_WID		    8
//Bit 15: 8, reg_pk_os_adpt_max             maximum limit adaptive overshoot margin (4x). 0~255; default= 200;             
#define PK_OS_ADPT_MIN_BIT		    0
#define PK_OS_ADPT_MIN_WID		    8
//Bit  7: 0, reg_pk_os_adpt_min             minimun limit adaptive overshoot margin (1x). 0~255; default= 20; 

#define ISP_PK_OS_STATIC                    0x2d7b  
//Bit 31:30, reserved
#define PK_OSH_MODE_BIT			    28
#define PK_OSH_MODE_WID			    2
//Bit 29:28, reg_pk_osh_mode                horizontal min_max window size for overshoot. window size =(2x+1); 0~3. default=2;   
//Bit 27:26, reserved                       
#define PK_OSV_MODE_BIT			    24
#define PK_OSV_MODE_WID			    2
//Bit 25:24, reg_pk_osv_mode                vertical min_max window size for overshoot. window size =(2x+1); 0~2. default=2;
//Bit 23:22, reserved                       
#define PK_OS_DOWN_BIT			    12
#define PK_OS_DOWN_WID			    10
//Bit 21:12, reg_pk_os_down                 static negative overshoot margin. 0~1023; default= 0;     
//Bit 11:10, reserved                     
#define PK_OS_UP_BIT			    0
#define PK_OS_UP_WID			    10
//Bit  9: 0, reg_pk_os_up                   static positive overshoot margin. 0~1023; default= 0; 

// address 0x2d7c~ 0x2d7f null 
/************************special digital effect register************************/
#define ISP_PKSDE_MODE_PKGAIN               0x2d80  
//Bit    31, reserved
#define PKSDE_YMODE_BIT			    28
#define PKSDE_YMODE_WID			    3
//Bit 30:28, reg_pksde_ymode                luma channel sde mode. 0~7. 0, no SDE; 1: rplc+ peaking; 2: rplc - peaking; 3: rplc+ peaking>0; 4: rplc + peaking<0; 5: rplc+ abs(peaking) 6: rplc-abs(peaking); 7: binary; default = 0;
//Bit    27, reserved                       
#define PKSDE_UMODE_BIT			    24
#define PKSDE_UMODE_WID			    3
//Bit 26:24, reg_pksde_umode                U channel sde mode. 0~7. 0, no SDE; 1: rplc+ peaking; 2: rplc - peaking; 3: rplc+ peaking>0; 4: rplc + peaking<0; 5: rplc+ abs(peaking) 6: rplc-abs(peaking); 7: binary ; default = 0;
//Bit    23, reserved                       
#define PKSDE_VMODE_BIT			    20
#define PKSDE_VMODE_WID			    3
//Bit 22:20, reg_pksde_vmode                V channel sde mode. 0~7. 0, no SDE; 1: rplc+ peaking; 2: rplc - peaking; 3: rplc+ peaking>0; 4: rplc + peaking<0; 5: rplc+ abs(peaking) 6: rplc-abs(peaking); 7: binary ; default = 0;
//Bit 19:12, reserved
#define PKSDE_PK4Y_GAIN_BIT		    8
#define PKSDE_PK4Y_GAIN_WID		    4
//Bit 11: 8, reg_pksde_pk4y_gain            gain to peaking boost component and add delta to luma channel. normalized 8 as '1'. 0~15; default=8 
#define PKSDE_PK4U_GAIN_BIT		    4
#define PKSDE_PK4U_GAIN_WID		    4
//Bit  7: 4, reg_pksde_pk4u_gain            gain to peaking boost component and add delta to u channel. normalized 8 as '1'. 0~15; default=8 
#define PKSDE_PK4V_GAIN_BIT		    0
#define PKSDE_PK4V_GAIN_WID		    4
//Bit  3: 0, reg_pksde_pk4v_gain            gain to peaking boost component and add delta to v channel. normalized 8 as '1'. 0~15; default=8 

#define ISP_PKSDE_REPLACE_Y_U               0x2d81  
//Bit 31:29, reserved
#define PKSDE_RPLC_YEN_BIT		    28
#define PKSDE_RPLC_YEN_WID		    1
//Bit    28, reg_pksde_rplc_yen             enable to replace Y channel with reg_pksde_rplc_y. default = 0
//Bit 27:26, reserved
#define PKSDE_RPLC_Y_BIT		    16
#define PKSDE_RPLC_Y_WID		    10
//Bit 25:16, reg_pksde_rplc_y               luma value to be replaced to Y channel when reg_pksde_rplc_yen=1. 0~1023. default = 512
//Bit 15:13, reserved
#define PKSDE_RPLC_RPLC_UEN_BIT		    12
#define PKSDE_RPLC_RPLC_UEN_WID		    1
//Bit    12, reg_pksde_rplc_uen             enable to replace U channel with reg_pksde_rplc_u. default = 0
//Bit 11:10, reserved                       
#define PKSDE_RPLC_RPLC_U_BIT		    0
#define PKSDE_RPLC_RPLC_U_WID		    10
//Bit  9: 0, reg_pksde_rplc_u               U value to be replaced to U channel when reg_pksde_rplc_uen=1. 0~1023. default = 512

#define ISP_PKSDE_REPLACE_V                 0x2d82  
//Bit 31:29, reserved
#define PKSDE_RPLC_RPLC_VEN_BIT		    28
#define PKSDE_RPLC_RPLC_VEN_WID		    1
//Bit    28, reg_pksde_rplc_ven             enable to replace V channel with reg_pksde_rplc_v. default = 0
//Bit 27:26, reserved
#define PKSDE_RPLC_RPLC_V_BIT		    16
#define PKSDE_RPLC_RPLC_V_WID		    10
//Bit 25:16, reg_pksde_rplc_v               V value to be replaced to V channel when reg_pksde_rplc_ven=1. 0~1023. default = 512
//Bit 15: 0, reserved                       

#define ISP_PKSDE_BINARY_HIG                0x2d83  
//Bit 31:24, reserved
#define PKSDE_YPOSI_BIT			    16
#define PKSDE_YPOSI_WID			    8
//Bit 23:16, reg_pksde_yposi                binary effect high luma level for Y. 0~255. default = 192
#define PKSDE_UPOSI_BIT			    8
#define PKSDE_UPOSI_WID			    8
//Bit 15: 8, reg_pksde_uposi                binary effect high U level for U. 0~255. default = 192
#define PKSDE_VPOSI_BIT			    0
#define PKSDE_VPOSI_WID			    8
//Bit  7: 0, reg_pksde_vposi                binary effect high V level for V. 0~255. default = 192

#define ISP_PKSDE_BINARY_LOW                0x2d84  
//Bit 31:24, reserved
#define PKSDE_YNEGI_BIT			    16
#define PKSDE_YNEGI_WID			    8
//Bit 23:16, reg_pksde_ynegi                binary effect low luma level for Y. 0~255. default = 64
#define PKSDE_UNEGI_BIT			    8
#define PKSDE_UNEGI_WID			    8
//Bit 15: 8, reg_pksde_unegi                binary effect low U level for U. 0~255. default = 128
#define PKSDE_VNEGI_BIT			    0
#define PKSDE_VNEGI_WID			    8
//Bit  7: 0, reg_pksde_vnegi                binary effect low V level for V. 0~255. default = 128

#define ISP_PKNR_ENABLE                     0x2d85  
//Bit 31: 2, reserved
#define ISP_PK_EN_BIT			    1
#define ISP_PK_EN_WID			    1
//Bit     1, reg_isp_pk_en                  isp peaking enable default = 1
#define ISP_NR_EN_BIT			    0
#define ISP_NR_EN_WID			    1
//Bit     0, reg_isp_nr_en                  isp noise reduction enable default = 1


// address 8'h86~ 8'h87 null 
/***************auto white balance statistics registers*****************/
#define ISP_AWB_WIND_LR                     0x2d88  
//Bit 31:29, reserved
#define AWB_WIND_LEFT_BIT		    16
#define AWB_WIND_LEFT_WID		    13
//Bit 28:16, reg_isp_awb_wind_left          window left x index for rgb raw statistics. 0~8191. default = 0.
//Bit 15:13, reserved
#define AWB_WIND_RIGHT_BIT		    0
#define AWB_WIND_RIGHT_WID		    13
//Bit 12: 0, reg_isp_awb_wind_right         window right x index for rgb raw statistics. 0~8191. default = 100.

#define ISP_AWB_WIND_TB                     0x2d89  
//Bit 31:29, reserved
#define AWB_WIND_TOP_BIT		    16
#define AWB_WIND_TOP_WID		    13
//Bit 28:16, reg_isp_awb_wind_top           window top y index for awb rgb statistics. 0~8191. default = 0.
//Bit 15:13, reserved
#define AWB_WIND_BOT_BIT		    0
#define AWB_WIND_BOT_WID		    13
//Bit 12: 0, reg_isp_awb_wind_bot           window bot y index for awb rgb statistics. 0~8191. default = 100.

#define ISP_AWB_GBGRBR_THRD                 0x2d8a  
//Bit 31:24, reserved
#define AWB_GB_THRD_BIT			    16
#define AWB_GB_THRD_WID			    8
//Bit 23:16, reg_isp_awb_gb_thrd            threshold to abs(g-b) to decide adding to awb statistics. 0~255. default = 200.
#define AWB_GR_THRD_BIT			    8
#define AWB_GR_THRD_WID			    8
//Bit 15: 8, reg_isp_awb_gr_thrd            threshold to abs(g-r) to decide adding to awb statistics. 0~255. default = 200.
#define AWB_BR_THRD_BIT			    0
#define AWB_BR_THRD_WID			    8
//Bit  7: 0, reg_isp_awb_br_thrd            threshold to abs(b-r) to decide adding to awb statistics. 0~255. default = 200.

#define ISP_AWB_UVTH_YPIECE                 0x2d8b
#define AWB_U_THRD_BIT			    24
#define AWB_U_THRD_WID			    8
//Bit 31:24, reg_isp_awb_u_thrd             threshold to abs(u-128) to decide adding to awb statistics. 0~255. default = 200.
#define AWB_V_THRD_BIT			    16
#define AWB_V_THRD_WID			    8
//Bit 23:16, reg_isp_awb_v_thrd             threshold to abs(v-128) to decide adding to awb statistics. 0~255. default = 200.
#define AWB_YPIECE_LOW_BIT		    8
#define AWB_YPIECE_LOW_WID		    8
//Bit 15: 8, reg_isp_awb_ypiece_low         low threshold of Y to decide adding to awb statistics to yluv_sum/num and ymuv_sum/num. 0~255. default = 50. 
#define AWB_YPIECE_HIG_BIT		    0
#define AWB_YPIECE_HIG_WID		    8
//Bit  7: 0, reg_isp_awb_ypiece_hig         hig threshold of Y to decide adding to awb statistics to yhuv_sum/num and ymuv_sum/num. 0~255. default = 200. 

#define ISP_AWB_AEC_ENABLE                  0x2d8c
//Bit 31: 3, reserved
//Bit     2, reg_isp_aec_raw_en             default = 1.
#define AEC_RAW_EN_BIT			    2
#define AEC_RAW_EN_WID			    1
//Bit     1, reg_isp_aec_stat_en            default = 1.
#define AEC_STAT_EN_BIT			    1
#define AEC_STAT_EN_WID			    1
//Bit     0, reg_isp_awb_stat_en            default = 1. 
#define AWB_STAT_EN_BIT			    0
#define AWB_STAT_EN_WID			    1

// address 0x2d8c~ 0x2d8f null 
/*************** auto exposure statistics registers******************/

#define ISP_AEC_THRESHOLDS                  0x2d90 
#define AEC_LUMA_LOWLMT_BIT		    24
#define AEC_LUMA_LOWLMT_WID		    8
//Bit 31:24, reg_isp_aec_luma_lowlmt        luma low limit when added to aec statistics in 4x4 regions. 0~255, default = 128
#define AEC_RAWBRIGHT_R_BIT		    16
#define AEC_RAWBRIGHT_R_WID		    8
//Bit 23:16, reg_isp_aec_rawbright_r        red pixels considered as too bright threshold. 0~255, default = 200
#define AEC_RAWBRIGHT_G_BIT		    8
#define AEC_RAWBRIGHT_G_WID		    8
//Bit 15: 8, reg_isp_aec_rawbright_g        green pixels considered as too bright threshold. 0~255, default = 200
#define AEC_RAWBRIGHT_B_BIT		    0
#define AEC_RAWBRIGHT_B_WID		    8
//Bit  7: 0, reg_isp_aec_rawbright_b        blue pixels considered as too bright threshold. 0~255, default = 200

#define ISP_AEC_WIND_XYSTART                0x2d91  
//Bit 31:29, reserved
#define AEC_WIND_XSTART_BIT		    16
#define AEC_WIND_XSTART_WID		    13
//Bit 28:16, reg_isp_aec_wind_xstart        window 0 left x index for AEC statistics. 0~8191. default = 0.
//Bit 15:13, reserved 
#define AEC_WIND_YSTART_BIT		    0
#define AEC_WIND_YSTART_WID		    13
//Bit 12: 0, reg_isp_aec_wind_ystart        window 0 top y index for AEC statistics. 0~8191. default = 0.

#define ISP_AEC_WIND_XYSTEP                 0x2d92  
//Bit 31:29, reserved
#define AEC_WIND_XSTEP_BIT		    16
#define AEC_WIND_XSTEP_WID		    13
//Bit 28:16, reg_isp_aec_wind_xstep         window 0 sub-window x lenght for AEC statistics. 0~8191. default = 64.
//Bit 15:13, reserved
#define AEC_WIND_YSTEP_BIT		    0
#define AEC_WIND_YSTEP_WID		    13
//Bit 12: 0, reg_isp_aec_wind_ystep         window 0 sub-window y lenght for AEC statistics. 0~8191. default = 64.

#define ISP_AECRAW_WIND_LR                  0x2d93  
//Bit 31:29, reserved
#define AECRAW_WIND_LEFT_BIT		    16
#define AECRAW_WIND_LEFT_WID		    13
//Bit 28:16, reg_isp_aecraw_wind_left       window left x index for AEC raw statistics. 0~8191. default = 0.
//Bit 15:13, reserved
#define AECRAW_WIND_RIGHT_BIT	            0
#define AECRAW_WIND_RIGHT_WID		    13
//Bit 12: 0, reg_isp_aecraw_wind_right      window right x index for AEC raw statistics. 0~8191. default = 100.

#define ISP_AECRAW_WIND_TB                  0x2d94                                     
//Bit 31:29, reserved
#define AECRAW_WIND_TOP_BIT		    16
#define AECRAW_WIND_TOP_WID		    13
//Bit 28:16, reg_isp_aecraw_wind_top        window top y index for AEC raw statistics. 0~8191. default = 0.
//Bit 15:13, reserved                       
#define AECRAW_WIND_BOT_BIT		    0
#define AECRAW_WIND_BOT_WID		    13
//Bit 12: 0, reg_isp_aecraw_wind_bot        window bot y index for AEC raw statistics. 0~8191. default = 100.

// address 0x2d95~ 0x2d97 null 

/*******************  auto focus statistics registers *********************/
#define ISP_AFC_FILTER_SEL                  0x2d98  
//Bit 31:17, reserved
#define AFC_RO_UPDATE_BIT		    16
#define AFC_RO_UPDATE_WID		    1
//Bit    16, reg_isp_afc_ro_update          auto focus statistics read-only register update enable. 0: no update; 1: update in v blank time
//Bit    15, reserved
#define AFC_F0_SELECT_BIT		    12
#define AFC_F0_SELECT_WID		    3
//Bit 14:12, reg_isp_afc_f0_select          filter 0 selection mode. 0: [-2 2]; 1: [-1 0  1;-1 0 1; -1 0 1]; 2: [-1 0  1;-2 0 2; -1 0 1]; 3: [-1 0 0 0  1;-2 0 0 0 2; -1 0 0 0 1]; 4:[-1 2 -1;-1 2 -1;-1 2 -1]; 5/up:[-1 0 2 0 -1;-1 0 2 0 -1;-1 0 2 0 -1]; default= 2
//Bit    11, reserved
#define AFC_F1_SELECT_BIT		    8
#define AFC_F1_SELECT_WID		    3
//Bit 10: 8, reg_isp_afc_f1_select          filter 1 selection mode. 0: [-2 2]; 1: [-1 0  1;-1 0 1; -1 0 1]; 2: [-1 0  1;-2 0 2; -1 0 1]; 3: [-1 0 0 0  1;-2 0 0 0 2; -1 0 0 0 1]; 4:[-1 2 -1;-1 2 -1;-1 2 -1]; 5/up:[-1 0 2 0 -1;-1 0 2 0 -1;-1 0 2 0 -1]; default= 4
#define AFC_F0_CORING_BIT		    4
#define AFC_F0_CORING_WID		    4
//Bit  7: 4, reg_isp_afc_f0_coring          coring to filter 0 filtering result before doing accum, to reduce noise interference. 0~15. default=2
#define AFC_F1_CORING_BIT		    0
#define AFC_F1_CORING_WID		    4
//Bit  3: 0, reg_isp_afc_f1_coring          coring to filter 1 filtering result before doing accum, to reduce noise interference. 0~15. default=2

#define ISP_AFC_WIND0_LR                    0x2d99  
//Bit 31:29, reserved
#define AFC_WIND0_LEFT_BIT		    16
#define AFC_WIND0_LEFT_WID		    13
//Bit 28:16, reg_isp_afc_wind0_left         window 0 left x index for AFC statistics. 0~8191. default = 0.
//Bit 15:13, reserved
#define AFC_WIND0_RIGHT_BIT		    0
#define AFC_WIND0_RIGHT_WID		    13
//Bit 12: 0, reg_isp_afc_wind0_right        window 0 right x index for AFC statistics. 0~8191. default = 100.

#define ISP_AFC_WIND0_TB                    0x2d9a  
//Bit 31:29, reserved
#define AFC_WIND0_TOP_BIT		    16
#define AFC_WIND0_TOP_WID		    13
//Bit 28:16, reg_isp_afc_wind0_top          window 0 top y index for AFC statistics. 0~8191. default = 0.
//Bit 15:13, reserved    
#define AFC_WIND0_BOT_BIT		    0
#define AFC_WIND0_BOT_WID		    13
//Bit 12: 0, reg_isp_afc_wind0_bot          window 0 bot y index for AFC statistics. 0~8191. default = 100.

#define ISP_AFC_WIND1_LR                    0x2d9b  
//Bit 31:29, reserved 
#define AFC_WIND1_LEFT_BIT		    16
#define AFC_WIND1_LEFT_WID		    13
//Bit 28:16, reg_isp_afc_wind1_left         window 1 left x index for AFC statistics. 0~8191. default = 0.
//Bit 15:13, reserved
#define AFC_WIND1_RIGHT_BIT		    0
#define AFC_WIND1_RIGHT_WID		    13
//Bit 12: 0, reg_isp_afc_wind1_righ         window 1 right x index for AFC statistics. 0~8191. default = 100.

#define ISP_AFC_WIND1_TB                    0x2d9c  
//Bit 31:29, reserved
#define AFC_WIND1_TOP_BIT		    16
#define AFC_WIND1_TOP_WID		    13
//Bit 28:16, reg_isp_afc_wind1_top          window 1 top y index for AFC statistics. 0~8191. default = 0.
//Bit 15:13, reserved
#define AFC_WIND1_BOT_BIT		    0
#define AFC_WIND1_BOT_WID		    13
//Bit 12: 0, reg_isp_afc_wind1_bot          window 1 bot y index for AFC statistics. 0~8191. default = 100.

#define ISP_AFC_WIND2_LR                    0x2d9d  
//Bit 31:29, reserved
#define AFC_WIND2_LEFT_BIT		    16
#define AFC_WIND2_LEFT_WID		    13
//Bit 28:16, reg_isp_afc_wind2_left         window 2 left x index for AFC statistics. 0~8191. default = 0.
//Bit 15:13, reserved
#define AFC_WIND2_RIGHT_BIT		    0
#define AFC_WIND2_RIGHT_WID		    13
//Bit 12: 0, reg_isp_afc_wind2_right        window 2 right x index for AFC statistics. 0~8191. default = 100.

#define ISP_AFC_WIND2_TB                    0x2d9e  
//Bit 31:29, reserved
#define AFC_WIND2_TOP_BIT		    16
#define AFC_WIND2_TOP_WID		    13
//Bit 28:16, reg_isp_afc_wind2_top          window 2 top y index for AFC statistics. 0~8191. default = 0.
//Bit 15:13, reserved
#define AFC_WIND2_BOT_BIT		    0
#define AFC_WIND2_BOT_WID		    13
//Bit 12: 0, reg_isp_afc_wind2_bot          window 2 bot y index for AFC statistics. 0~8191. default = 100.

#define ISP_AFC_WIND3_LR                    0x2d9f  
//Bit 31:29, reserved
#define AFC_WIND3_LEFT_BIT		    16
#define AFC_WIND3_LEFT_WID		    13
//Bit 28:16, reg_isp_afc_wind3_left         window 3 left x index for AFC statistics. 0~8191. default = 0.
//Bit 15:13, reserved
#define AFC_WIND3_RIGHT_BIT		    0
#define AFC_WIND3_RIGHT_WID		    13
//Bit 12: 0, reg_isp_afc_wind3_right        window 3 right x index for AFC statistics. 0~8191. default = 100.

#define ISP_AFC_WIND3_TB                    0x2da0  
//Bit 31:29, reserved
#define AFC_WIND3_TOP_BIT		    16
#define AFC_WIND3_TOP_WID		    13
//Bit 28:16, reg_isp_afc_wind3_top          window 3 top y index for AFC statistics. 0~8191. default = 0.
//Bit 15:13, reserved
#define AFC_WIND3_BOT_BIT		    0
#define AFC_WIND3_BOT_WID		    13
//Bit 12: 0, reg_isp_afc_wind3_bot          window 3 bot y index for AFC statistics. 0~8191. default = 100.

#define ISP_AFC_WIND4_LR                    0x2da1  
//Bit 31:29, reserved
#define AFC_WIND4_LEFT_BIT		    16
#define AFC_WIND4_LEFT_WID		    13
//Bit 28:16, reg_isp_afc_wind4_left         window 4 left x index for AFC statistics. 0~8191. default = 0.
//Bit 15:13, reserved                       
#define AFC_WIND4_RIGHT_BIT		    0
#define AFC_WIND4_RIGHT_WID		    13
//Bit 12: 0, reg_isp_afc_wind4_righ         window 4 right x index for AFC statistics. 0~8191. default = 100.

#define ISP_AFC_WIND4_TB                    0x2da2  
//Bit 31:29, reserved
#define AFC_WIND4_TOP_BIT		    16
#define AFC_WIND4_TOP_WID		    13
//Bit 28:16, reg_isp_afc_wind4_top          window 4 top y index for AFC statistics. 0~8191. default = 0.
//Bit 15:13, reserved
#define AFC_WIND4_BOT_BIT		    0
#define AFC_WIND4_BOT_WID		    13
//Bit 12: 0, reg_isp_afc_wind4_bot          window 4 bot y index for AFC statistics. 0~8191. default = 100.

#define ISP_AFC_WIND5_LR                    0x2da3  
//Bit 31:29, reserved                       
#define AFC_WIND5_LEFT_BIT		    16
#define AFC_WIND5_LEFT_WID		    13
//Bit 28:16, reg_isp_afc_wind5_left         window 5 left x index for AFC statistics. 0~8191. default = 0.
//Bit 15:13, reserved                       
#define AFC_WIND5_RIGHT_BIT		    0
#define AFC_WIND5_RIGHT_WID		    13
//Bit 12: 0, reg_isp_afc_wind5_righ         window 5 right x index for AFC statistics. 0~8191. default = 100.

#define ISP_AFC_WIND5_TB                    0x2da4  
//Bit 31:29, reserved                       
#define AFC_WIND5_TOP_BIT		    16
#define AFC_WIND5_TOP_WID		    13
//Bit 28:16, reg_isp_afc_wind5_top          window 5 top y index for AFC statistics. 0~8191. default = 0.
//Bit 15:13, reserved
#define AFC_WIND5_BOT_BIT		    0
#define AFC_WIND5_BOT_WID		    13
//Bit 12: 0, reg_isp_afc_wind5_bot          window 5 bot y index for AFC statistics. 0~8191. default = 100.

#define ISP_AFC_WIND6_LR                    0x2da5  
//Bit 31:29, reserved
#define AFC_WIND6_LEFT_BIT		    16
#define AFC_WIND6_LEFT_WID		    13
//Bit 28:16, reg_isp_afc_wind6_left         window 6 left x index for AFC statistics. 0~8191. default = 0.
//Bit 15:13, reserved                       
#define AFC_WIND6_RIGHT_BIT		    0
#define AFC_WIND6_RIGHT_WID		    13
//Bit 12: 0, reg_isp_afc_wind6_righ         window 6 right x index for AFC statistics. 0~8191. default = 100.

#define ISP_AFC_WIND6_TB                    0x2da6  
//Bit 31:29, reserved
#define AFC_WIND6_TOP_BIT		    16
#define AFC_WIND6_TOP_WID		    13
//Bit 28:16, reg_isp_afc_wind6_top          window 6 top y index for AFC statistics. 0~8191. default = 0.
//Bit 15:13, reserved
#define AFC_WIND6_BOT_BIT		    0
#define AFC_WIND6_BOT_WID		    13
//Bit 12: 0, reg_isp_afc_wind6_bot          window 6 bot y index for AFC statistics. 0~8191. default = 100.

#define ISP_AFC_WIND7_LR                    0x2da7  
//Bit 31:29, reserved
#define AFC_WIND7_LEFT_BIT		    16
#define AFC_WIND7_LEFT_WID		    13
//Bit 28:16, reg_isp_afc_wind7_left         window 7 left x index for AFC statistics. 0~8191. default = 0.
//Bit 15:13, reserved                       
#define AFC_WIND7_RIGHT_BIT		    0
#define AFC_WIND7_RIGHT_WID		    13
//Bit 12: 0, reg_isp_afc_wind7_righ         window 7 right x index for AFC statistics. 0~8191. default = 100.

#define ISP_AFC_WIND7_TB                    0x2da8  
//Bit 31:29, reserved
#define AFC_WIND7_TOP_BIT		    16
#define AFC_WIND7_TOP_WID		    13
//Bit 28:16, reg_isp_afc_wind7_top          window 7 top y index for AFC statistics. 0~8191. default = 0.
//Bit 15:13, reserved
#define AFC_WIND7_BOT_BIT		    0
#define AFC_WIND7_BOT_WID		    13
//Bit 12: 0, reg_isp_afc_wind7_bot          window 7 bot y index for AFC statistics. 0~8191. default = 100.

// address 0x2da9~ 0x2dab null 
//  black level and noise level statistics registers
#define ISP_BLNR_CTRL                       0x2dac  
//Bit 31: 9, reserved
#define BLNR_STATISTICS_EN_BIT		    8
#define BLNR_STATISTICS_EN_WID		    1
//Bit     8, reg_isp_blnr_statistics_en     enable for black level and noise level statistics on raw data. 0: no statistics; 1: enable. default =1
//Bit  7: 6, reserved
#define BLNR_LPF_MODE_BIT		    4
#define BLNR_LPF_MODE_WID		    2
//Bit  5: 4, reg_isp_blnr_lpf_mode          mode for lpf in black level and noise level statistics on raw data.0~3. 0: no lpf; 1: [1 2 1]/4 2/3: [1 2 2 2 1]/8; . default =3
//Bit  3: 2, reserved 
#define BLNR_AC_ADAPTIVE_BIT		    0
#define BLNR_AC_ADAPTIVE_WID		    2
//Bit  1: 0, reg_isp_blnr_ac_adaptive       mode for noise statistics in horizontal and vertical ac blending. 0~3. u2: 0: (H+V)/2; 1: sqrt(H^2 + V^2); 2: min(H,V); 3: max(H,V). default = 2

#define ISP_BLNR_WIND_LR                    0x2dad  
//Bit 31:29, reserved
#define BLNR_WIND_LEFT_BIT		    16
#define BLNR_WIND_LEFT_WID		    13
//Bit 28:16, reg_isp_blnr_wind_left         window left x index for black level and noise level statistics on raw data. 0~8191. default = 0.
//Bit 15:13, reserved
#define BLNR_WIND_RIGHT_BIT		    0
#define BLNR_WIND_RIGHT_WID		    13
//Bit 12: 0, reg_isp_blnr_wind_right         window right x index for black level and noise level statistics on raw data. 0~8191. default = 100.

#define ISP_BLNR_WIND_TB                    0x2dae  
//Bit 31:29, reserved
#define BLNR_WIND_TOP_BIT		    16
#define BLNR_WIND_TOP_WID		    13
//Bit 28:16, reg_isp_blnr_wind_top          window top y index for black level and noise level statistics on raw data. 0~8191. default = 0.
//Bit 15:13, reserved
#define BLNR_WIND_BOT_BIT		    0
#define BLNR_WIND_BOT_WID		    13
//Bit 12: 0, reg_isp_blnr_wind_bot          window bot y index for black level and noise level statistics on raw data. 0~8191. default = 100.

// address 0x2daf~ 0x2daf null                                    
/*************** raw component statistics registers****************/
#define ISP_DBG_PIXEL_CTRL                  0x2db0 
//Bit 31: 2, reserved
#define DBG_PIXEL_LPF_BIT		    0
#define DBG_PIXEL_LPF_WID		    2
//Bit  1: 0, reg_isp_dbg_pixel_lpf          low-pass filter mode for debug pixel position. 0~3. 0: no lpf; 1: [1 2 1]/4; 2: [1 2 1]'/4; 3: [0 1 0; 1 1 1;0 1 0]; default = 0

#define ISP_DBG_PIXEL_POSITION              0x2db1  
//Bit 31:29, reserved
#define DBG_PIXEL_XPOS_BIT		    16
#define DBG_PIXEL_XPOS_WID		    13
//Bit 28:16, reg_isp_dbg_pixel_xpos         x index of pixel for debug on raw data. 0~8191. default = 100.
//Bit 15:13, reserved
#define DBG_PIXEL_YPOS_BIT		    0
#define DBG_PIXEL_YPOS_WID		    13
//Bit 12: 0, reg_isp_dbg_pixel_ypos         y index of pixel for debug on raw data. 0~8191. default = 1000.


#define ISP_RO_DBG_PIXEL_GRBG0_1            0x2db2  
//Bit 31:26, reserved
#define DBG_PIXEL_GRBG_0_BIT		    16
#define DBG_PIXEL_GRBG_0_WID		    10
//Bit 25:16, ro_isp_dbg_pixel_grbg_0        phasDATA_PORTe 0 green component value of the debuged postion (x,y).
//Bit 15:10, reserved
#define DBG_PIXEL_GRBG_1_BIT		    0
#define DBG_PIXEL_GRBG_1_WID		    10
//Bit  9: 0, ro_isp_dbg_pixel_grbg_1        phase 1 red component value of the debuged postion (x,y).

#define ISP_RO_DBG_PIXEL_GRBG2_3            0x2db3   
//Bit 31:26, reserved
#define DBG_PIXEL_GRBG_2_BIT		    16
#define DBG_PIXEL_GRBG_2_WID		    10
//Bit 25:16, ro_isp_dbg_pixel_grbg_2        phase 2 blue component value of the debuged postion (x,y).
//Bit 15:10, reserved
#define DBG_PIXEL_GRBG_3_BIT		    0
#define DBG_PIXEL_GRBG_3_WID		    10
//Bit  9: 0, ro_isp_dbg_pixel_grbg_3        phase 3 green component value of the debuged postion (x,y).

// LUT1024/dft_LUT9x32

#define ISP_RO_DFT_LUTMEM_CTRL              0x2d32  
//Bit 31:11, reserved
//Bit 10: 0, ro_isp_dft_lutpointer          pointer of the latest defect pixel position in LUT1024. will be accumulated in HW, but will be readout

#define ISP_RO_DFT_DET_NUM                  0x2d3f  
//Bit 31:28, reserved
//Bit 27: 0, ro_isp_dftpixel_num            read-only register for detected defect pixels numbers of each frame. shadow locked version during v-blank.



/******************* index + data **********************/

// note there are some space in the header
#define ISP_RO_ADDR_PORT                    0x2dc0
#define ISP_RO_DATA_PORT                    0x2dc1

//=================================awb readonly=======================================================

#define ISP_RO_AWB_RED_SUM                  0x9b  //read-only register, update each v-blank
//Bit 31: 0, ro_isp_awb_red_sum             red accum statistics for awb.
#define ISP_RO_AWB_GRN_SUM                  0x9c  //read-only register, update each v-blank
//Bit 31: 0, ro_isp_awb_grn_sum             green accum statistics for awb.
#define ISP_RO_AWB_BLU_SUM                  0x9d  //read-only register, update each v-blank
//Bit 31: 0, ro_isp_awb_blu_sum             blue accum statistics for awb.
#define ISP_RO_AWB_RGB_NUM                  0x9e  //read-only register, update each v-blank
//Bit 31:24, reserved                       
#define AWB_RGB_NUM_BIT                     0  //number of rgb pixels added to statistics for awb.
#define AWB_RGB_NUM_WID                     24

#define ISP_RO_AWB_LOW_UNEG_SUM             0x9f  //read-only register, update each v-blank
//Bit 31: 0, ro_isp_awb_low_uneg_sum        u (u<0) accum statistics for y<reg_isp_awb_ypiece_low awb.
#define ISP_RO_AWB_LOW_VNEG_SUM             0xa0  //read-only register, update each v-blank
//Bit 31: 0, ro_isp_awb_low_vneg_sum        v (v<0) accum statistics for y<reg_isp_awb_ypiece_low awb.
#define ISP_RO_AWB_LOW_UPOS_SUM             0xa1  //read-only register, update each v-blank
//Bit 31: 0, ro_isp_awb_low_upos_sum        u (u>=0) accum statistics for y>=reg_isp_awb_ypiece_high awb.
#define ISP_RO_AWB_LOW_VPOS_SUM             0xa2  //read-only register, update each v-blank
//Bit 31: 0, ro_isp_awb_low_vpos_sum        v (v>=0) accum statistics for y>=reg_isp_awb_ypiece_high awb.

#define ISP_RO_AWB_LOW_UNEG_NUM             0xa3  //read-only register, update each v-blank
//Bit 31:24, reserved                       
//Bit 23: 0, ro_isp_awb_low_uneg_num        number of u (u<0) put to accum statistics for y<reg_isp_awb_ypiece_low awb.
#define AWB_LOW_UNEG_NUM_BIT		    0
#define AWB_LOW_UNEG_NUM_WID		    24

#define ISP_RO_AWB_LOW_VNEG_NUM             0xa4  //read-only register, update each v-blank
//Bit 31:24, reserved                       
//Bit 23: 0, ro_isp_awb_low_vneg_num        number of v (v<0) put to accum statistics for y<reg_isp_awb_ypiece_low awb.
#define AWB_LOW_VNEG_NUM_BIT		    0
#define AWB_LOW_VNEG_NUM_WID		    24

#define ISP_RO_AWB_LOW_UPOS_NUM             0xa5  //read-only register, update each v-blank
//Bit 31:24, reserved                       
//Bit 23: 0, ro_isp_awb_low_upos_num        number of u (u>=0) put to accum statistics for y<reg_isp_awb_ypiece_low awb.
#define AWB_LOW_UPOS_NUM_BIT		    0
#define AWB_LOW_UPOS_NUM_WID		    24

#define ISP_RO_AWB_LOW_VPOS_NUM             0xa6  //read-only register, update each v-blank
//Bit 31:24, reserved                       
//Bit 23: 0, ro_isp_awb_low_vpos_num        number of v (v>=0) put to accum statistics for y<reg_isp_awb_ypiece_low awb.
#define AWB_LOW_VPOS_NUM_BIT		    0
#define AWB_LOW_VPOS_NUM_WID		    24

#define ISP_RO_AWB_MID_UNEG_SUM             0xa7  //read-only register, update each v-blank
//Bit 31: 0, ro_isp_awb_mid_uneg_sum        u (u<0) accum statistics for reg_isp_awb_ypiece_low<=y<=reg_isp_awb_ypiece_hig awb.
#define ISP_RO_AWB_MID_VNEG_SUM             0xa8  //read-only register, update each v-blank
//Bit 31: 0, ro_isp_awb_mid_vneg_sum        v (v<0) accum statistics for reg_isp_awb_ypiece_low<=y<=reg_isp_awb_ypiece_hig awb.
#define ISP_RO_AWB_MID_UPOS_SUM             0xa9  //read-only register, update each v-blank
//Bit 31: 0, ro_isp_awb_mid_upos_sum        u (u>=0) accum statistics for reg_isp_awb_ypiece_low<=y<=reg_isp_awb_ypiece_hig awb.
#define ISP_RO_AWB_MID_VPOS_SUM             0xaa  //read-only register, update each v-blank
//Bit 31: 0, ro_isp_awb_mid_vpos_sum        v (v>=0) accum statistics for reg_isp_awb_ypiece_low<=y<=reg_isp_awb_ypiece_hig awb.

#define ISP_RO_AWB_MID_UNEG_NUM             0xab  //read-only register, update each v-blank
//Bit 31:24, reserved
//Bit 23: 0, ro_isp_awb_mid_uneg_num        number of u (u<0) put to accum statistics for reg_isp_awb_ypiece_low<=y<=reg_isp_awb_ypiece_hig awb.
#define AWB_MID_UNEG_NUM_BIT		    0
#define AWB_MID_UNEG_NUM_WID		    24

#define ISP_RO_AWB_MID_VNEG_NUM             0xac  //read-only register, update each v-blank
//Bit 31:24, reserved                       
//Bit 23: 0, ro_isp_awb_mid_vneg_num        number of v (v<0) put to accum statistics for reg_isp_awb_ypiece_low<=y<=reg_isp_awb_ypiece_hig awb.
#define AWB_MID_VNEG_NUM_BIT		    0
#define AWB_MID_VNEG_NUM_WID		    24

#define ISP_RO_AWB_MID_UPOS_NUM             0xad  //read-only register, update each v-blank
//Bit 31:24, reserved                       
//Bit 23: 0, ro_isp_awb_mid_upos_num        number of u (u>=0) put to accum statistics for reg_isp_awb_ypiece_low<=y<=reg_isp_awb_ypiece_hig awb.
#define AWB_MID_UPOS_NUM_BIT		    0
#define AWB_MID_UPOS_NUM_WID		    24

#define ISP_RO_AWB_MID_VPOS_NUM             0xae  //read-only register, update each v-blank
//Bit 31:24, reserved                       
//Bit 23: 0, ro_isp_awb_mid_vpos_num        number of v (v>=0) put to accum statistics for reg_isp_awb_ypiece_low<=y<=reg_isp_awb_ypiece_hig awb.
#define AWB_MID_VPOS_NUM_BIT		    0
#define AWB_MID_VPOS_NUM_WID		    24

#define ISP_RO_AWB_HIG_UNEG_SUM             0xaf  //read-only register, update each v-blank
//Bit 31: 0, ro_isp_awb_hig_uneg_sum        u (u<0) accum statistics for y>reg_isp_awb_ypiece_hig awb.
#define ISP_RO_AWB_HIG_VNEG_SUM             0xb0  //read-only register, update each v-blank
//Bit 31: 0, ro_isp_awb_hig_vneg_sum        v (v<0) accum statistics for y>reg_isp_awb_ypiece_hig awb.
#define ISP_RO_AWB_HIG_UPOS_SUM             0xb1  //read-only register, update each v-blank
//Bit 31: 0, ro_isp_awb_hig_upos_sum        u (u>=0) accum statistics for y>reg_isp_awb_ypiece_hig awb.
#define ISP_RO_AWB_HIG_VPOS_SUM             0xb2  //read-only register, update each v-blank
//Bit 31: 0, ro_isp_awb_hig_vpos_sum        v (v>=0) accum statistics for y>reg_isp_awb_ypiece_hig awb.

#define ISP_RO_AWB_HIG_UNEG_NUM             0xb3  //read-only register, update each v-blank
//Bit 31:24, reserved                       
//Bit 23: 0, ro_isp_awb_hig_uneg_num        number of u (u<0) put to accum statistics for y>reg_isp_awb_ypiece_hig awb.
#define AWB_HIG_UNEG_NUM_BIT		    0
#define AWB_HIG_UNEG_NUM_WID		    24

#define ISP_RO_AWB_HIG_VNEG_NUM             0xb4  //read-only register, update each v-blank
//Bit 31:24, reserved                       
//Bit 23: 0, ro_isp_awb_hig_vneg_num        number of v (v<0) put to accum statistics for y>reg_isp_awb_ypiece_hig awb.
#define AWB_HIG_VNEG_NUM_BIT		    0
#define AWB_HIG_VNEG_NUM_WID		    24

#define ISP_RO_AWB_HIG_UPOS_NUM             0xb5  //read-only register, update each v-blank
//Bit 31:24, reserved                       
//Bit 23: 0, ro_isp_awb_hig_upos_num        number of u (u>=0) put to accum statistics for y>reg_isp_awb_ypiece_hig awb.
#define AWB_HIG_UPOS_NUM_BIT		    0
#define AWB_HIG_UPOS_NUM_WID		    24

#define ISP_RO_AWB_HIG_VPOS_NUM             0xb6  //read-only register, update each v-blank
//Bit 31:24, reserved                       
//Bit 23: 0, ro_isp_awb_hig_vpos_num        number of v (v>=0) put to accum statistics for y>reg_isp_awb_ypiece_hig awb.
#define AWB_HIG_VPOS_NUM_BIT		    0
#define AWB_HIG_VPOS_NUM_WID		    24

//======================================aec readonly ==========================================================
#define ISP_RO_AEC_LUMA_WIND0_0             0x8b  //read-only register, update each v-blank
//Bit 31: 0, ro_isp_aec_luma0_0             window 00 luma accum statistics 
#define ISP_RO_AEC_LUMA_WIND0_1             0x8c  //read-only register, update each v-blank
//Bit 31: 0, ro_isp_aec_luma0_1             window 01 luma accum statistics 
#define ISP_RO_AEC_LUMA_WIND0_2             0x8d  //read-only register, update each v-blank
//Bit 31: 0, ro_isp_aec_luma0_2             window 02 luma accum statistics 
#define ISP_RO_AEC_LUMA_WIND0_3             0x8e  //read-only register, update each v-blank
//Bit 31: 0, ro_isp_aec_luma0_3             window 03 luma accum statistics

#define ISP_RO_AEC_LUMA_WIND1_0             0x8f  //read-only register, update each v-blank
//Bit 31: 0, ro_isp_aec_luma1_0             window 10 luma accum statistics 
#define ISP_RO_AEC_LUMA_WIND1_1             0x90  //read-only register, update each v-blank
//Bit 31: 0, ro_isp_aec_luma1_1             window 11 luma accum statistics 
#define ISP_RO_AEC_LUMA_WIND1_2             0x91  //read-only register, update each v-blank
//Bit 31: 0, ro_isp_aec_luma1_2             window 12 luma accum statistics 
#define ISP_RO_AEC_LUMA_WIND1_3             0x92  //read-only register, update each v-blank
//Bit 31: 0, ro_isp_aec_luma1_3             window 13 luma accum statistics 

#define ISP_RO_AEC_LUMA_WIND2_0             0x93  //read-only register, update each v-blank
//Bit 31: 0, ro_isp_aec_luma2_0             window 20 luma accum statistics 
#define ISP_RO_AEC_LUMA_WIND2_1             0x94  //read-only register, update each v-blank
//Bit 31: 0, ro_isp_aec_luma2_1             window 21 luma accum statistics 
#define ISP_RO_AEC_LUMA_WIND2_2             0x95  //read-only register, update each v-blank
//Bit 31: 0, ro_isp_aec_luma2_2             window 22 luma accum statistics 
#define ISP_RO_AEC_LUMA_WIND2_3             0x96  //read-only register, update each v-blank
//Bit 31: 0, ro_isp_aec_luma2_3             window 23 luma accum statistics 

#define ISP_RO_AEC_LUMA_WIND3_0             0x97  //read-only register, update each v-blank
//Bit 31: 0, ro_isp_aec_luma3_0             window 30 luma accum statistics 
#define ISP_RO_AEC_LUMA_WIND3_1             0x98  //read-only register, update each v-blank
//Bit 31: 0, ro_isp_aec_luma3_1             window 31 luma accum statistics 
#define ISP_RO_AEC_LUMA_WIND3_2             0x99  //read-only register, update each v-blank
//Bit 31: 0, ro_isp_aec_luma3_2             window 32 luma accum statistics 
#define ISP_RO_AEC_LUMA_WIND3_3             0x9a  //read-only register, update each v-blank
//Bit 31: 0, ro_isp_aec_luma3_3             window 33 luma accum statistics 

#define ISP_RO_AECRAW_NUM_RED               0x80  //read-only register, update each v-blank
//Bit 31:26, reserved                       
//Bit 25: 0, ro_isp_aecraw_num_r            red bright pixels numbers in raw window    
#define AECRAW_NUM_RED_BIT                  0
#define AECRAW_NUM_RED_WID                  26

#define ISP_RO_AECRAW_NUM_GREEN             0x81  //read-only register, update each v-blank
//Bit 31:26, reserved                       
//Bit 25: 0, ro_isp_aecraw_num_g            green bright pixels numbers in raw window
#define AECRAW_NUM_GREEN_BIT                0
#define AECRAW_NUM_GREEN_WID                26

#define ISP_RO_AECRAW_NUM_BLUE              0x82  //read-only register, update each v-blank
//Bit 31:26, reserved                       
//Bit 25: 0, ro_isp_aecraw_num_b            blue bright pixels numbers in raw window   
#define AECRAW_NUM_BLUE_BIT                 0
#define AECRAW_NUM_BLUE_WID                 26
//===================================afc readonly====================================================

#define ISP_RO_AFC_WIND0_F0                 0xb7  // read-only register, update each v-blank 
//Bit 31: 0, ro_isp_afc_wind0_f0            f0 accum within window 0. the larger the better focus lock. 
#define ISP_RO_AFC_WIND0_F1                 0xb8  // read-only register, update each v-blank
//Bit 31: 0, ro_isp_afc_wind0_f1            f1 accum within window 0. the larger the better focus lock. 

#define ISP_RO_AFC_WIND1_F0                 0xb9  // read-only register, update each v-blank 
//Bit 31: 0, ro_isp_afc_wind1_f0            f0 accum within window 1. the larger the better focus lock. 
#define ISP_RO_AFC_WIND1_F1                 0xba  // read-only register, update each v-blank
//Bit 31: 0, ro_isp_afc_wind1_f1            f1 accum within window 1. the larger the better focus lock.

#define ISP_RO_AFC_WIND2_F0                 0xbb  // read-only register, update each v-blank 
//Bit 31: 0, ro_isp_afc_wind2_f0            f0 accum within window 2. the larger the better focus lock. 
#define ISP_RO_AFC_WIND2_F1                 0xbc  // read-only register, update each v-blank
//Bit 31: 0, ro_isp_afc_wind2_f1            f1 accum within window 2. the larger the better focus lock. 

#define ISP_RO_AFC_WIND3_F0                 0xbd  // read-only register, update each v-blank 
//Bit 31: 0, ro_isp_afc_wind3_f0            f0 accum within window 3. the larger the better focus lock. 
#define ISP_RO_AFC_WIND3_F1                 0xbe  // read-only register, update each v-blank
//Bit 31: 0, ro_isp_afc_wind3_f1            f1 accum within window 3. the larger the better focus lock. 

#define ISP_RO_AFC_WIND4_F0                 0xbf  // read-only register, update each v-blank 
//Bit 31: 0, ro_isp_afc_wind4_f0            f0 accum within window 4. the larger the better focus lock. 
#define ISP_RO_AFC_WIND4_F1                 0xc0  // read-only register, update each v-blank
//Bit 31: 0, ro_isp_afc_wind4_f1            f1 accum within window 4. the larger the better focus lock. 

#define ISP_RO_AFC_WIND5_F0                 0xc1  // read-only register, update each v-blank 
//Bit 31: 0, ro_isp_afc_wind5_f0            f0 accum within window 5. the larger the better focus lock. 
#define ISP_RO_AFC_WIND5_F1                 0xc2  // read-only register, update each v-blank
//Bit 31: 0, ro_isp_afc_wind5_f1            f1 accum within window 5. the larger the better focus lock. 

#define ISP_RO_AFC_WIND6_F0                 0xc3  // read-only register, update each v-blank 
//Bit 31: 0, ro_isp_afc_wind6_f0            f0 accum within window 6. the larger the better focus lock. 
#define ISP_RO_AFC_WIND6_F1                 0xc4  // read-only register, update each v-blank
//Bit 31: 0, ro_isp_afc_wind6_f1            f1 accum within window 6. the larger the better focus lock. 

#define ISP_RO_AFC_WIND7_F0                 0xc5  // read-only register, update each v-blank 
//Bit 31: 0, ro_isp_afc_wind7_f0            f0 accum within window 7. the larger the better focus lock. 
#define ISP_RO_AFC_WIND7_F1                 0xc6  // read-only register, update each v-blank
//Bit 31: 0, ro_isp_afc_wind7_f1            f1 accum within window 7. the larger the better focus lock. 

//=================================blnr readonly=====================================================

#define ISP_RO_BLNR_GRBG_DCSUM0             0x83  // read-only register, update each v-blank 
//Bit 31: 0, ro_isp_blnr_grbg_dcsum0        phase 0 green chanel DC sum within stattistic window for black level statistics. 
#define ISP_RO_BLNR_GRBG_DCSUM1             0x84  // read-only register, update each v-blank 
//Bit 31: 0, ro_isp_blnr_grbg_dcsum1        phase 1 red chanel DC sum within stattistic window for black level statistics. 
#define ISP_RO_BLNR_GRBG_DCSUM2             0x85  // read-only register, update each v-blank 
//Bit 31: 0, ro_isp_blnr_grbg_dcsum2        phase 2 blue chanel DC sum within stattistic window for black level statistics. 
#define ISP_RO_BLNR_GRBG_DCSUM3             0x86  // read-only register, update each v-blank 
//Bit 31: 0, ro_isp_blnr_grbg_dcsum3        phase 2 green chanel DC sum within stattistic window for black level statistics.

#define ISP_RO_BLNR_GRBG_ACSUM0             0x87  // read-only register, update each v-blank 
//Bit 31: 0, ro_isp_blnr_grbg_acsum0        phase 0 green chanel AC sum within stattistic window for noise level statistics. 
#define ISP_RO_BLNR_GRBG_ACSUM1             0x88  // read-only register, update each v-blank 
//Bit 31: 0, ro_isp_blnr_grbg_acsum1        phase 1 red chanel AC sum within stattistic window for noise level statistics. 
#define ISP_RO_BLNR_GRBG_ACSUM2             0x89  // read-only register, update each v-blank 
//Bit 31: 0, ro_isp_blnr_grbg_acsum2        phase 2 blue chanel AC sum within stattistic window for noise level statistics. 
#define ISP_RO_BLNR_GRBG_ACSUM3             0x8a  // read-only register, update each v-blank 
//Bit 31: 0, ro_isp_blnr_grbg_acsum3        phase 2 green chanel AC sum within stattistic window for noise level statistics. 

#define ISP_GAMMA_LUT_ADDR                  0x2dc2
#define ISP_GAMMA_LUT_DATA                  0x2dc3  
#define ISP_DF1024_LUT_ADDR                 0x2dc4 
#define ISP_DF1024_LUT_DATA                 0x2dc5  
#define ISP_LNSD_LUT_ADDR                   0x2dc6
#define ISP_LNSD_LUT_DATA                   0x2dc7 

#endif  // __ISP_REGS_H

