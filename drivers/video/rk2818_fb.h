/* drivers/video/rk2818_fb.h
 *
 * Copyright (C) 2010 ROCKCHIP, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef __ARCH_ARM_MACH_RK2818_FB_H
#define __ARCH_ARM_MACH_RK2818_FB_H

/********************************************************************
**                            宏定义                                *
********************************************************************/
/* 输往屏的数据格式 */
#define OUT_P888            0
#define OUT_P666            1
#define OUT_P565            2
#define OUT_S888x           4
#define OUT_CCIR656         6
#define OUT_S888            8
#define OUT_S888DUMY        12
#define OUT_P16BPP4         24  //模拟方式,控制器并不支持

/* Low Bits Mask */
#define m_WORDLO            (0xffff<<0)
#define m_WORDHI            (0xffff<<16)
#define v_WORDLO(x)         (((x)&0xffff)<<0)
#define v_WORDHI(x)         (((x)&0xffff)<<16)

#define m_BIT11LO           (0x7ff<<0)
#define m_BIT11HI           (0x7ff<<16)
#define v_BIT11LO(x)        (((x)&0x7ff)<<0)
#define v_BIT11HI(x)        (((x)&0x7ff)<<16)


/* SYS_CONFIG */
#define m_W1_FORMAT          (1<<0)
#define m_W0_FORMAT          (7<<1)
#define m_W1_ROLLER          (1<<4)
#define m_W0_ROLLER          (1<<5)
#define m_INTERIACE_EN       (1<<6)      
#define m_MPEG2_I2P_EN       (1<<7)
#define m_W0_ROTATE          (1<<8)
#define m_W1_ENABLE          (1<<9)
#define m_W0_ENABLE          (1<<10)
#define m_HWC_ENABLE         (1<<11)
#define m_HWC_RELOAD_EN         (1<<12)
#define m_W1_INTERLACE_READ    (1<<13)
#define m_W0_INTERLACE_READ    (1<<14)
#define m_STANDBY            (1<<15)
#define m_W1_HWC_INCR        (31<<16)
#define m_W1_HWC_BURST       (7<<21)
#define m_W0_INCR            (31<<24)
#define m_W0_BURST           (7<<29)
#define v_W1_FORMAT(x)          (((x)&1)<<0)
#define v_W0_FORMAT(x)          (((x)&7)<<1)
#define v_W1_ROLLER(x)          (((x)&1)<<4)
#define v_W0_ROLLER(x)          (((x)&1)<<5)
#define v_INTERIACE_EN(x)          (((x)&1)<<6)      
#define v_MPEG2_I2P_EN(x)          (((x)&1)<<7)
#define v_W0_ROTATE(x)          (((x)&1)<<8)
#define v_W1_ENABLE(x)          (((x)&1)<<9)
#define v_W0_ENABLE(x)          (((x)&1)<<10)
#define v_HWC_ENABLE(x)         (((x)&1)<<11)
#define v_HWC_RELOAD_EN(x)         (((x)&1)<<12)
#define v_W1_INTERLACE_READ(x)    (((x)&1)<<13)
#define v_W0_INTERLACE_READ(x)    (((x)&1)<<14)
#define v_STANDBY(x)            (((x)&1)<<15)
#define v_W1_HWC_INCR(x)        (((x)&31)<<16)
#define v_W1_HWC_BURST(x)       (((x)&7)<<21)
#define v_W0_INCR(x)            (((x)&31)<<24)
#define v_W0_BURST(x)           (((x)&7)<<29)

//LCDC_SWAP_CTRL
#define m_W1_565_RB_SWAP        (1<<0)
#define m_W0_565_RB_SWAP        (1<<1)
#define m_W0_YRGB_M8_SWAP       (1<<2)
#define m_W0_YRGB_R_SHIFT_SWAP  (1<<3)
#define m_W0_CBR_R_SHIFT_SWAP   (1<<4)
#define m_W0_YRGB_16_SWAP       (1<<5)
#define m_W0_YRGB_8_SWAP        (1<<6)
#define m_W0_CBR_16_SWAP        (1<<7)
#define m_W0_CBR_8_SWAP         (1<<8)
#define m_W1_16_SWAP            (1<<9)
#define m_W1_8_SWAP             (1<<10)
#define m_W1_R_SHIFT_SWAP       (1<<11)
#define m_OUTPUT_BG_SWAP        (1<<12)
#define m_OUTPUT_RB_SWAP        (1<<13)
#define m_OUTPUT_RG_SWAP        (1<<14)
#define m_DELTA_SWAP            (1<<15)
#define m_DUMMY_SWAP            (1<<16)
#define m_W0_YRGB_HL8_SWAP      (1<<17)
#define v_W1_565_RB_SWAP(x)        (((x)&1)<<0)
#define v_W0_565_RB_SWAP(x)        (((x)&1)<<1)
#define v_W0_YRGB_M8_SWAP(x)       (((x)&1)<<2)
#define v_W0_YRGB_R_SHIFT_SWAP(x)  (((x)&1)<<3)
#define v_W0_CBR_R_SHIFT_SWAP(x)   (((x)&1)<<4)
#define v_W0_YRGB_16_SWAP(x)       (((x)&1)<<5)
#define v_W0_YRGB_8_SWAP(x)        (((x)&1)<<6)
#define v_W0_CBR_16_SWAP(x)        (((x)&1)<<7)
#define v_W0_CBR_8_SWAP(x)         (((x)&1)<<8)
#define v_W1_16_SWAP(x)            (((x)&1)<<9)
#define v_W1_8_SWAP(x)             (((x)&1)<<10)
#define v_W1_R_SHIFT_SWAP(x)       (((x)&1)<<11)
#define v_OUTPUT_BG_SWAP(x)        (((x)&1)<<12)
#define v_OUTPUT_RB_SWAP(x)        (((x)&1)<<13)
#define v_OUTPUT_RG_SWAP(x)        (((x)&1)<<14)
#define v_DELTA_SWAP(x)            (((x)&1)<<15)
#define v_DUMMY_SWAP(x)            (((x)&1)<<16)
#define v_W0_YRGB_HL8_SWAP(x)      (((x)&1)<<17)

//LCDC_MCU_TIMING_CTRL
#define m_MCU_WRITE_PERIOD      (31<<0)
#define m_MCU_CS_ST             (31<<5)
#define m_MCU_CS_END            (31<<10)
#define m_MCU_RW_ST             (31<<15)
#define m_MCU_RW_END            (31<<20)
#define m_MCU_HOLD_STATUS          (1<<26)
#define m_MCU_HOLDMODE_SELECT     (1<<27)
#define m_MCU_HOLDMODE_FRAME_ST   (1<<28)
#define m_MCU_RS_SELECT            (1<<29)
#define m_MCU_BYPASSMODE_SELECT   (1<<30)
#define m_MCU_OUTPUT_SELECT        (1<<31)
#define v_MCU_WRITE_PERIOD(x)      (((x)&31)<<0)
#define v_MCU_CS_ST(x)          (((x)&31)<<5)
#define v_MCU_CS_END(x)         (((x)&31)<<10)
#define v_MCU_RW_ST(x)          (((x)&31)<<15)
#define v_MCU_RW_END(x)         (((x)&31)<<20)
#define v_MCU_HOLD_STATUS(x)          (((x)&1)<<26)
#define v_MCU_HOLDMODE_SELECT(x)     (((x)&1)<<27)
#define v_MCU_HOLDMODE_FRAME_ST(x)   (((x)&1)<<28)
#define v_MCU_RS_SELECT(x)            (((x)&1)<<29)
#define v_MCU_BYPASSMODE_SELECT(x)   (((x)&1)<<30)
#define v_MCU_OUTPUT_SELECT(x)        (((x)&1)<<31)

//LCDC_ BLEND_CTRL
#define m_W1_BLEND_EN          (1<<0) 
#define m_W0_BLEND_EN          (1<<1)
#define m_HWC_BLEND_EN          (1<<2)
#define m_W1_BLEND_FACTOR_SELECT     (1<<3)
#define m_W0_BLEND_FACTOR_SELECT     (1<<4)
#define m_W0W1_OVERLAY                 (1<<5)
#define m_HWC_BLEND_FACTOR    (15<<12)
#define m_W1_BLEND_FACTOR     (0xff<<16)
#define m_W0_BLEND_FACTOR     (0xff<<24)
#define v_W1_BLEND_EN(x)          (((x)&1)<<0) 
#define v_W0_BLEND_EN(x)          (((x)&1)<<1)
#define v_HWC_BLEND_EN(x)          (((x)&1)<<2)
#define v_W1_BLEND_FACTOR_SELECT(x)     (((x)&1)<<3)
#define v_W0_BLEND_FACTOR_SELECT(x)     (((x)&1)<<4)
#define v_W0W1_OVERLAY(x)                 (((x)&1)<<5)
#define v_HWC_BLEND_FACTOR(x)    (((x)&15)<<12)
#define v_W1_BLEND_FACTOR(x)     (((x)&0xff)<<16)
#define v_W0_BLEND_FACTOR(x)     (((x)&0xff)<<24)

//LCDC_WIN0_COLOR_KEY_CTRL / LCDC_WIN1_COLOR_KEY_CTRL
#define m_KEYCOLOR          (0xffffff<<0)
#define m_KEYCOLOR_B          (0xff<<0)    
#define m_KEYCOLOR_G          (0xff<<8)
#define m_KEYCOLOR_R          (0xff<<16)
#define m_COLORKEY_EN         (1<<24) 
#define v_KEYCOLOR(x)          (((x)&0xffffff)<<0)
#define v_KEYCOLOR_B(x)          (((x)&0xff)<<0)    
#define v_KEYCOLOR_G(x)         (((x)&0xff)<<8)
#define v_KEYCOLOR_R(x)          (((x)&0xff)<<16)
#define v_COLORKEY_EN(x)         (((x)&1)<<24)

//LCDC_DEFLICKER_SCL_OFFSET
#define m_W0_YRGB_VSD_OFFSET      (0xff<<0)
#define m_W0_YRGB_VSP_OFFSET      (0xff<<8)
#define m_W1_VSD_OFFSET           (0xff<<16)
#define m_W1_VSP_OFFSET           (0xff<<24)
#define v_W0_YRGB_VSD_OFFSET(x)      (((x)&0xff)<<0)
#define v_W0_YRGB_VSP_OFFSET(x)      (((x)&0xff)<<8)
#define v_W1_VSD_OFFSET(x)           (((x)&0xff)<<16)
#define v_W1_VSP_OFFSET(x)           (((x)&0xff)<<24)

//LCDC_DSP_CTRL_REG0
#define m_DISPLAY_FORMAT             (0xf<<0)
#define m_HSYNC_POLARITY             (1<<4)
#define m_VSYNC_POLARITY             (1<<5)
#define m_DEN_POLARITY               (1<<6)
#define m_DCLK_POLARITY              (1<<7)
#define m_COLOR_SPACE_CONVERSION     (3<<8)
#define m_I2P_THRESHOLD_Y            (0x3f<<10)        
#define m_I2P_THRESHOLD_CBR          (0x3f<<16) 
#define m_565_TO_888_REPLICATION_EN  (1<<22)
#define m_DITHERING_MODE             (1<<23)
#define m_DITHERING_EN               (1<<24)
#define m_DROP_LINE_W1               (1<<25)
#define m_DROP_LINE_W0               (1<<26)
#define m_I2P_CUR_POLARITY           (1<<27)
#define m_INTERLACE_FIELD_POLARITY   (1<<28)
#define m_YUV_CLIP_MODE              (1<<29)
#define m_I2P_FILTER_EN              (1<<30) 
#define m_I2P_FILTER_PARAM           (1<<31)  
#define v_DISPLAY_FORMAT(x)            (((x)&0xf)<<0)
#define v_HSYNC_POLARITY(x)             (((x)&1)<<4)
#define v_VSYNC_POLARITY(x)             (((x)&1)<<5)
#define v_DEN_POLARITY(x)               (((x)&1)<<6)
#define v_DCLK_POLARITY(x)              (((x)&1)<<7)
#define v_COLOR_SPACE_CONVERSION(x)     (((x)&3)<<8)
#define v_I2P_THRESHOLD_Y(x)            (((x)&0x3f)<<10)        
#define v_I2P_THRESHOLD_CBR(x)          (((x)&0x3f)<<16) 
#define v_565_TO_888_REPLICATION_EN(x)  (((x)&1)<<22)
#define v_DITHERING_MODE(x)             (((x)&1)<<23)
#define v_DITHERING_EN(x)               (((x)&1)<<24)
#define v_DROP_LINE_W1(x)               (((x)&1)<<25)
#define v_DROP_LINE_W0(x)               (((x)&1)<<26)
#define v_I2P_CUR_POLARITY(x)           (((x)&1)<<27)
#define v_INTERLACE_FIELD_POLARITY(x)   (((x)&1)<<28)
#define v_YUV_CLIP_MODE(x)              (((x)&1)<<29)
#define v_I2P_FILTER_EN(x)              (((x)&1)<<30) 
#define v_I2P_FILTER_PARAM(x)           (((x)&1)<<31)  

//LCDC_DSP_CTRL_REG1
#define m_BG_COLOR                    (0xffffff<<0)
#define m_BG_B                        (0xff<<0)          
#define m_BG_G                        (0xff<<8)   
#define m_BG_R                        (0xff<<16)   
#define m_BLANK_MODE                  (1<<24) 
#define m_BLACK_MODE                  (1<<25) 
#define m_W1_SD_DEFLICKER_EN            (1<<26)
#define m_W1_SP_DEFLICKER_EN            (1<<27)
#define m_W0CR_SD_DEFLICKER_EN          (1<<28)
#define m_W0CR_SP_DEFLICKER_EN          (1<<29)
#define m_W0YRGB_SD_DEFLICKER_EN        (1<<30)
#define m_W0YRGB_SP_DEFLICKER_EN        (1<<31)
#define v_BG_COLOR(x)                    (((x)&0xffffff)<<0)  
#define v_BG_B(x)                        (((x)&0xff)<<0)          
#define v_BG_G(x)                        (((x)&0xff)<<8)   
#define v_BG_R(x)                        (((x)&0xff)<<16)   
#define v_BLANK_MODE(x)                  (((x)&1)<<24) 
#define v_BLACK_MODE(x)                  (((x)&1)<<25) 
#define v_W1_SD_DEFLICKER_EN(x)            (((x)&1)<<26)
#define v_W1_SP_DEFLICKER_EN(x)            (((x)&1)<<27)
#define v_W0CR_SD_DEFLICKER_EN(x)          (((x)&1)<<28)
#define v_W0CR_SP_DEFLICKER_EN(x)          (((x)&1)<<29)
#define v_W0YRGB_SD_DEFLICKER_EN(x)        (((x)&1)<<30)
#define v_W0YRGB_SP_DEFLICKER_EN(x)        (((x)&1)<<31)

//LCDC_INT_STATUS
#define m_HOR_START         (1<<0)
#define m_FRM_START         (1<<1)
#define m_SCANNING_FLAG     (1<<2)
#define m_HOR_STARTMASK     (1<<3)
#define m_FRM_STARTMASK     (1<<4)
#define m_SCANNING_MASK     (1<<5)
#define m_HOR_STARTCLEAR    (1<<6)
#define m_FRM_STARTCLEAR    (1<<7)
#define m_SCANNING_CLEAR    (1<<8)
#define m_SCAN_LINE_NUM     (0x7ff<<9)
#define v_HOR_START(x)         (((x)&1)<<0)
#define v_FRM_START(x)         (((x)&1)<<1)
#define v_SCANNING_FLAG(x)     (((x)&1)<<2)
#define v_HOR_STARTMASK(x)     (((x)&1)<<3)
#define v_FRM_STARTMASK(x)     (((x)&1)<<4)
#define v_SCANNING_MASK(x)     (((x)&1)<<5)
#define v_HOR_STARTCLEAR(x)    (((x)&1)<<6)
#define v_FRM_STARTCLEAR(x)    (((x)&1)<<7)
#define v_SCANNING_CLEAR(x)    (((x)&1)<<8)
#define v_SCAN_LINE_NUM(x)     (((x)&0x7ff)<<9)

#define m_VIRWIDTH       (0xffff<<0)
#define m_VIRHEIGHT      (0xffff<<16)
#define v_VIRWIDTH(x)       (((x)&0xffff)<<0)
#define v_VIRHEIGHT(x)      (((x)&0xffff)<<16)

#define m_ACTWIDTH       (0xffff<<0)
#define m_ACTHEIGHT      (0xffff<<16)
#define v_ACTWIDTH(x)       (((x)&0xffff)<<0)
#define v_ACTHEIGHT(x)      (((x)&0xffff)<<16)

#define m_VIRST_X      (0xffff<<0)
#define m_VIRST_Y      (0xffff<<16)
#define v_VIRST_X(x)      (((x)&0xffff)<<0)
#define v_VIRST_Y(x)      (((x)&0xffff)<<16)

#define m_PANELST_X      (0x3ff<<0)
#define m_PANELST_Y      (0x3ff<<16)
#define v_PANELST_X(x)      (((x)&0x3ff)<<0)
#define v_PANELST_Y(x)      (((x)&0x3ff)<<16)

#define m_PANELWIDTH       (0x3ff<<0)
#define m_PANELHEIGHT      (0x3ff<<16)
#define v_PANELWIDTH(x)       (((x)&0x3ff)<<0)
#define v_PANELHEIGHT(x)      (((x)&0x3ff)<<16)

#define m_HWC_B                 (0xff<<0)
#define m_HWC_G                 (0xff<<8)
#define m_HWC_R                 (0xff<<16)
#define m_W0_YRGB_HSP_OFFSET    (0xff<<24)
#define m_W0_YRGB_HSD_OFFSET    (0xff<<24)
#define v_HWC_B(x)                 (((x)&0xff)<<0)
#define v_HWC_G(x)                 (((x)&0xff)<<8)
#define v_HWC_R(x)                 (((x)&0xff)<<16)
#define v_W0_YRGB_HSP_OFFSET(x)    (((x)&0xff)<<24)
#define v_W0_YRGB_HSD_OFFSET(x)    (((x)&0xff)<<24)


//Panel display scanning
#define m_PANEL_HSYNC_WIDTH             (0x3ff<<0)
#define m_PANEL_HORIZONTAL_PERIOD       (0x3ff<<16)
#define v_PANEL_HSYNC_WIDTH(x)             (((x)&0x3ff)<<0)
#define v_PANEL_HORIZONTAL_PERIOD(x)       (((x)&0x3ff)<<16)

#define m_PANEL_END              (0x3ff<<0)  
#define m_PANEL_START            (0x3ff<<16)
#define v_PANEL_END(x)              (((x)&0x3ff)<<0)  
#define v_PANEL_START(x)            (((x)&0x3ff)<<16)

#define m_PANEL_VSYNC_WIDTH             (0x3ff<<0)
#define m_PANEL_VERTICAL_PERIOD       (0x3ff<<16)
#define v_PANEL_VSYNC_WIDTH(x)             (((x)&0x3ff)<<0)
#define v_PANEL_VERTICAL_PERIOD(x)       (((x)&0x3ff)<<16)
//-----------

#define m_HSCALE_FACTOR        (0xffff<<0)  
#define m_VSCALE_FACTOR        (0xffff<<16)
#define v_HSCALE_FACTOR(x)        (((x)&0xffff)<<0)  
#define v_VSCALE_FACTOR(x)        (((x)&0xffff)<<16)

#define m_W0_CBR_HSD_OFFSET   (0xff<<0)
#define m_W0_CBR_HSP_OFFSET   (0xff<<8)
#define m_W0_CBR_VSD_OFFSET   (0xff<<16)
#define m_W0_CBR_VSP_OFFSET   (0xff<<24)
#define v_W0_CBR_HSD_OFFSET(x)   (((x)&0xff)<<0)
#define v_W0_CBR_HSP_OFFSET(x)   (((x)&0xff)<<8)
#define v_W0_CBR_VSD_OFFSET(x)   (((x)&0xff)<<16)
#define v_W0_CBR_VSP_OFFSET(x)   (((x)&0xff)<<24)


#define FB0_IOCTL_STOP_TIMER_FLUSH		0x6001
#define FB0_IOCTL_SET_PANEL				0x6002

#define FB1_IOCTL_GET_PANEL_SIZE		0x5001
#define FB1_IOCTL_SET_YUV_ADDR			0x5002
//#define FB1_TOCTL_SET_MCU_DIR			0x5003
#define FB1_IOCTL_SET_ROTATE            0x5003
#define FB1_IOCTL_SET_I2P_ODD_ADDR      0x5005
#define FB1_IOCTL_SET_I2P_EVEN_ADDR     0x5006


/********************************************************************
**                          结构定义                                *
********************************************************************/
/* LCDC的寄存器结构 */
typedef volatile struct tagLCDC_REG
{
    /* offset 0x00~0xc0 */
    unsigned int SYS_CONFIG;              //SYSTEM configure register
    unsigned int SWAP_CTRL;               //Data SWAP control
    unsigned int MCU_TIMING_CTRL;         //MCU TIMING control register
    unsigned int BLEND_CTRL;              //Blending control register
    unsigned int WIN0_COLOR_KEY_CTRL;     //Win0 blending control register
    unsigned int WIN1_COLOR_KEY_CTRL;     //Win1 blending control register
    unsigned int DEFLICKER_SCL_OFFSET;    //Deflick scaling start point offset
    unsigned int DSP_CTRL0;               //Display control register0
    unsigned int DSP_CTRL1;               //Display control register1
    unsigned int INT_STATUS;              //Interrupt status register
    unsigned int WIN0_VIR;                //WIN0 virtual display width/height
    unsigned int WIN0_YRGB_MST;           //Win0 active YRGB memory start address
    unsigned int WIN0_CBR_MST;            //Win0 active Cbr memory start address
    unsigned int WIN0_ACT_INFO;           //Win0 active window width/height
    unsigned int WIN0_ROLLER_INFO;        //Win0 x and y value of start point in roller mode
    unsigned int WIN0_DSP_ST;             //Win0 display start point on panel
    unsigned int WIN0_DSP_INFO;           //Win0 display width/height on panel
    unsigned int WIN1_VIR;                //Win1 virtual display width/height
    unsigned int WIN1_YRGB_MST;            //Win1 active  memory start address
    unsigned int WIN1_ACT_INFO;           //Win1 active width /height
    unsigned int WIN1_ROLLER_INFO;        //Win1 x and y value of start point in roller mode
    unsigned int WIN1_DSP_ST;             //Win1 display start point on panel
    unsigned int WIN1_DSP_INFO;           //Win1 display width/height on panel
    unsigned int HWC_MST;                 //HWC memory start address
    unsigned int HWC_DSP_ST;              //HWC display start point on panel
    unsigned int HWC_COLOR_LUT0;          //Hardware cursor color 2’b01 look up table 0
    unsigned int HWC_COLOR_LUT1;          //Hardware cursor color 2’b10 look up table 1
    unsigned int HWC_COLOR_LUT2;          //Hardware cursor color 2’b11 look up table 2
    unsigned int DSP_HTOTAL_HS_END;       //Panel scanning horizontal width and hsync pulse end point
    unsigned int DSP_HACT_ST_END;         //Panel active horizontal scanning start/end point
    unsigned int DSP_VTOTAL_VS_END;       //Panel scanning vertical height and vsync pulse end point
    unsigned int DSP_VACT_ST_END;         //Panel active vertical scanning start/end point
    unsigned int DSP_VS_ST_END_F1;        //Vertical scanning start point and vsync pulse end point of even filed in interlace mode
    unsigned int DSP_VACT_ST_END_F1;      //Vertical scanning active start/end point of even filed in interlace mode
    unsigned int WIN0_SD_FACTOR_Y;        //Win0 YRGB scaling down factor setting
    unsigned int WIN0_SP_FACTOR_Y;        //Win0 YRGB scaling up factor setting
    unsigned int WIN0_CBR_SCL_OFFSET;     //Win0 Cbr scaling start point offset
    unsigned int WIN1_SCL_FACTOR;         //Win1 scaling factor setting
    unsigned int I2P_REF0_MST_Y;          //I2P field 0 memory start address
    unsigned int I2P_REF0_MST_CBR;        //I2P field 0 memory start address
    unsigned int I2P_REF1_MST_Y;          //I2P field 2 memory start address
    unsigned int I2P_REF1_MST_CBR;        //I2P field 2 memory start address
    unsigned int WIN0_YRGB_VIR_MST;       //Win0 virtual memory start address
    unsigned int WIN0_CBR_VIR_MST;        //Win0 virtual memory start address
    unsigned int WIN1_VIR_MST;            //Win1 virtual memory start address
    unsigned int WIN0_SD_FACTOR_CBR;      //Win0 CBR scaling down factor setting
    unsigned int WIN0_SP_FACTOR_CBR;      //Win0 CBR scaling up factor setting
    unsigned int reserved0;
    unsigned int REG_CFG_DONE;            //REGISTER CONFIG FINISH
    unsigned int reserved1[(0x500-0xc4)/4];
    unsigned int MCU_BYPASS_WPORT;         //MCU BYPASS MODE, DATA Write Port
} LCDC_REG, *pLCDC_REG;


extern void __init rk2818_add_device_lcdc(void);
extern int mcu_ioctl(unsigned int cmd, unsigned long arg);

#endif
