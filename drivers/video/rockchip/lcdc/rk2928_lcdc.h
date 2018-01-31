/* SPDX-License-Identifier: GPL-2.0 */
#ifndef RK2928_LCDC_H_
#define RK2928_LCDC_H_

#include<linux/rk_fb.h>

#define LcdReadBit(inf, addr, msk)      ((inf->regbak.addr=inf->preg->addr)&(msk))
#define LcdWrReg(inf, addr, val)        inf->preg->addr=inf->regbak.addr=(val)
#define LcdRdReg(inf, addr)             (inf->preg->addr)
#define LcdSetBit(inf, addr, msk)       inf->preg->addr=((inf->regbak.addr) |= (msk))
#define LcdClrBit(inf, addr, msk)       inf->preg->addr=((inf->regbak.addr) &= ~(msk))
#define LcdSetRegBit(inf, addr, msk)    inf->preg->addr=((inf->preg->addr) |= (msk))
#define LcdMskReg(inf, addr, msk, val)  (inf->regbak.addr)&=~(msk);   inf->preg->addr=(inf->regbak.addr|=(val))
#define LCDC_REG_CFG_DONE()             LcdWrReg(lcdc_dev, REG_CFG_DONE, 0x01); dsb()

/********************************************************************
**                          结构定义                                *
********************************************************************/
/* LCDC的寄存器结构 */

typedef volatile struct tagLCDC_REG
{
    /* offset 0x00~0xc0 */
	unsigned int SYS_CFG;               		//0x00 system config register
	unsigned int DSP_CTRL;				//0x0c display control register 
	unsigned int BG_COLOR;				//back ground color register
	unsigned int ALPHA_CTRL;				//alpha control register
	unsigned int INT_STATUS;             		//0x10 Interrupt status register
	unsigned int WIN0_COLOR_KEY_CTRL;     //0x1c Win0 blending control register
	unsigned int WIN1_COLOR_KEY_CTRL;     //0x20 Win1 blending control register
	unsigned int WIN0_YRGB_MST;           //0x28 Win0 active YRGB memory start address0
	unsigned int WIN0_CBR_MST;            //0x2c Win0 active Cbr memory start address0
	unsigned int WIN_VIR;                //0x38 WIN0 virtual display width/height
	unsigned int WIN0_ACT_INFO;           //0x3C Win0 active window width/height
	unsigned int WIN0_DSP_INFO;           //0x40 Win0 display width/height on panel
	unsigned int WIN0_DSP_ST;             //0x44 Win0 display start point on panel
	unsigned int WIN0_SCL_FACTOR_YRGB;    //0x48Win0 YRGB scaling  factor setting
	unsigned int WIN0_SCL_FACTOR_CBR;     //0x4c Win0 YRGB scaling factor setting
	unsigned int WIN0_SCL_OFFSET;         //0x50 Win0 Cbr scaling start point offset
	unsigned int WIN1_RGB_MST;           //0x54 Win1 active YRGB memory start address
	unsigned int WIN1_DSP_INFO;           //0x64 Win1 display width/height on panel
	unsigned int WIN1_DSP_ST;             //0x68 Win1 display start point on panel
	unsigned int HWC_MST;                 //0x88 HWC memory start address
	unsigned int HWC_DSP_ST;              //0x8C HWC display start point on panel
	unsigned int HWC_COLOR_LUT0;          //0x90 Hardware cursor color 2’b01 look up table 0
	unsigned int HWC_COLOR_LUT1;          //0x94 Hardware cursor color 2’b10 look up table 1
	unsigned int HWC_COLOR_LUT2;          //0x98 Hardware cursor color 2’b11 look up table 2
	unsigned int DSP_HTOTAL_HS_END;       //0x9c Panel scanning horizontal width and hsync pulse end point
	unsigned int DSP_HACT_ST_END;         //0xa0 Panel active horizontal scanning start/end point
	unsigned int DSP_VTOTAL_VS_END;       //0xa4 Panel scanning vertical height and vsync pulse end point
	unsigned int DSP_VACT_ST_END;         //0xa8 Panel active vertical scanning start/end point
	unsigned int SCL_REG0;		      //scaler register
	unsigned int SCL_REG1;
	unsigned int SCL_REG2;
	unsigned int SCL_REG3;
	unsigned int SCL_REG4;
	unsigned int SCL_REG5;
	unsigned int SCL_REG6;
	unsigned int SCL_REG7;
	unsigned int SCL_REG8;
	unsigned int reserve[3];
	unsigned int REG_CFG_DONE;            //0xc0 REGISTER CONFIG FINISH
  
} LCDC_REG, *pLCDC_REG;


/* SYS_CONFIG */

#define  m_W0_EN              (1<<0)
#define  m_W1_EN              (1<<1)
#define  m_HWC_EN             (1<<2)
#define  m_W0_FORMAT          (7<<3)
#define  m_W1_FORMAT          (7<<6)
#define  m_W0_RGB_RB_SWAP     (1<<10)
#define  m_W1_RGB_RB_SWAP     (1<<14)

#define m_W0_AXI_OUTSTANDING_DISABLE (1<<16) 
#define m_W1_AXI_OUTSTANDING_DISABLE (1<<17)
#define m_DMA_BURST_LENGTH	     (3<<18)
#define m_LCDC_STANDBY		     (1<<22)

#define m_LCDC_AXICLK_AUTO_ENABLE    (1<<24) //eanble for low power
#define m_DSP_OUT_ZERO		     (1<<25)

#define v_W0_EN(x)          		(((x)&1)<<0)
#define v_W1_EN(x)          		(((x)&1)<<1)
#define v_HWC_EN(x)         		(((x)&1)<<2)
#define v_W0_FORMAT(x)      		(((x)&7)<<3)
#define v_W1_FORMAT(x)      		(((x)&7)<<6)
#define v_W0_RGB_RB_SWAP(x)		(((x)&1)<<10)	
#define v_W1_RGB_RB_SWAP(x)		(((x)&1)<<14)

#define v_LCDC_STANDBY(x)		(((x)&1)<<22)
#define v_LCDC_AXICLK_AUTO_ENABLE(x)    (((x)&1)<<24)
#define v_DSP_OUT_ZERO(x)    		(((x)&1)<<25)


#define v_LCDC_DMA_STOP(x)              (((x)&1)<<0)
#define v_HWC_RELOAD_EN(x)             (((x)&1)<<2)
#define v_W0_AXI_OUTSTANDING_DISABLE(x) (((x)&1)<<3)
#define v_W1_AXI_OUTSTANDING_DISABLE(x) (((x)&1)<<4)
#define v_W2_AXI_OUTSTANDING_DISABLE(x) (((x)&1)<<5)
#define v_DMA_BURST_LENGTH(x)		(((x)&3)<<6)
#define v_WIN0_YRGB_CHANNEL0_ID(x)	(((x)&7)<<8)
#define v_WIN0_CBR_CHANNEL0_ID(x)	(((x)&7)<<11)
#define v_WIN0_YRGB_CHANNEL1_ID(x)      (((x)&7)<<14)
#define v_WIN0_CBR_CHANNEL1_ID(x)	(((x)&7)<<17)
#define v_WIN1_YRGB_CHANNEL_ID(x)	(((x)&7)<<20)
#define v_WIN1_CBR_CHANNEL_ID(x)	(((x)&7)<<23)
#define v_WIN2_CHANNEL_ID(x)	        (((x)&7)<<26)
#define v_HWC_CHANNEL_ID(x)	        (((x)&7)<<29)



//LCDC_DSP_CTRL_REG
#define m_DISPLAY_FORMAT             (3<<0)
#define m_BLANK_MODE                 (1<<2)
#define m_BLACK_MODE                 (1<<3)
#define m_HSYNC_POLARITY             (1<<4)
#define m_VSYNC_POLARITY             (1<<5)
#define m_DEN_POLARITY               (1<<6)
#define m_DCLK_POLARITY              (1<<7)
#define m_W0W1_POSITION_SWAP         (1<<8)
#define m_OUTPUT_BG_SWAP             (1<<9)
#define m_OUTPUT_RB_SWAP             (1<<10)
#define m_OUTPUT_RG_SWAP             (1<<11)
#define m_DITHER_UP_EN               (1<<12)
#define m_DITHER_DOWN_MODE           (1<<13)
#define m_DITHER_DOWN_EN             (1<<14)


#define m_W1_INTERLACE_READ_MODE     (1<<15)
#define m_W2_INTERLACE_READ_MODE     (1<<16)
#define m_W0_YRGB_DEFLICK_MODE       (1<<17)
#define m_W0_CBR_DEFLICK_MODE        (1<<18)
#define m_W1_YRGB_DEFLICK_MODE       (1<<19)
#define m_W1_CBR_DEFLICK_MODE        (1<<20)
#define m_W0_ALPHA_MODE              (1<<21)
#define m_W1_ALPHA_MODE              (1<<22)
#define m_W2_ALPHA_MODE              (1<<23)
#define m_W0_COLOR_SPACE_CONVERSION  (3<<24)
#define m_W1_COLOR_SPACE_CONVERSION  (3<<26)
#define m_W2_COLOR_SPACE_CONVERSION  (1<<28)
#define m_YCRCB_CLIP_EN              (1<<29)
#define m_CBR_FILTER_656             (1<<30)

#define v_DISPLAY_FORMAT(x)           (((x)&0x3)<<0)
#define v_BLANK_MODE(x)               (((x)&1)<<2)
#define v_BLACK_MODE(x)               (((x)&1)<<3)
#define v_HSYNC_POLARITY(x)           (((x)&1)<<4)
#define v_VSYNC_POLARITY(x)           (((x)&1)<<5)
#define v_DEN_POLARITY(x)             (((x)&1)<<6)
#define v_DCLK_POLARITY(x)            (((x)&1)<<7)
#define v_W0W1_POSITION_SWAP(x)	      (((x)&1)<<8)
#define v_OUTPUT_BG_SWAP(x)           (((x)&1)<<9)
#define v_OUTPUT_RB_SWAP(x)           (((x)&1)<<10)
#define v_OUTPUT_RG_SWAP(x)           (((x)&1)<<11)
#define v_DITHER_UP_EN(x)               (((x)&1)<<12)
#define v_DITHER_DOWN_MODE(x)           (((x)&1)<<13)
#define v_DITHER_DOWN_EN(x)             (((x)&1)<<14)

#define v_INTERLACE_DSP_EN(x)             (((x)&1)<<12)
#define v_INTERLACE_FIELD_POLARITY(x)   (((x)&1)<<13)
#define v_W0_INTERLACE_READ_MODE(x)     (((x)&1)<<14)
#define v_W1_INTERLACE_READ_MODE(x)     (((x)&1)<<15)
#define v_W2_INTERLACE_READ_MODE(x)     (((x)&1)<<16)
#define v_W0_YRGB_DEFLICK_MODE(x)       (((x)&1)<<17)
#define v_W0_CBR_DEFLICK_MODE(x)        (((x)&1)<<18)
#define v_W1_YRGB_DEFLICK_MODE(x)       (((x)&1)<<19)
#define v_W1_CBR_DEFLICK_MODE(x)        (((x)&1)<<20)
#define v_W0_ALPHA_MODE(x)             (((x)&1)<<21)
#define v_W1_ALPHA_MODE(x)              (((x)&1)<<22)
#define v_W2_ALPHA_MODE(x)             (((x)&1)<<23)
#define v_W0_COLOR_SPACE_CONVERSION(x)  (((x)&3)<<24)
#define v_W1_COLOR_SPACE_CONVERSION(x)  (((x)&3)<<26)
#define v_W2_COLOR_SPACE_CONVERSION(x)  (((x)&1)<<28)
#define v_YCRCB_CLIP_EN(x)            (((x)&1)<<29)
#define v_CBR_FILTER_656(x)             (((x)&1)<<30)

//LCDC_BG_COLOR
#define m_BG_COLOR                    (0xffffff<<0)
#define m_BG_B                        (0xff<<0)
#define m_BG_G                        (0xff<<8)
#define m_BG_R                        (0xff<<16)
#define v_BG_COLOR(x)                 (((x)&0xffffff)<<0)
#define v_BG_B(x)                     (((x)&0xff)<<0)
#define v_BG_G(x)                     (((x)&0xff)<<8)
#define v_BG_R(x)                     (((x)&0xff)<<16)




//LCDC_ BLEND_CTRL
#define m_HWC_BLEND_EN         (1<<0)
#define m_W2_BLEND_EN          (1<<1)
#define m_W1_BLEND_EN          (1<<2)
#define m_W0_BLEND_EN          (1<<3)
#define m_HWC_BLEND_FACTOR     (15<<4)
#define m_W2_BLEND_FACTOR     (0xff<<8)
#define m_W1_BLEND_FACTOR     (0xff<<16)
#define m_W0_BLEND_FACTOR     (0xff<<24)

#define v_HWC_BLEND_EN(x)         (((x)&1)<<0)
#define v_W2_BLEND_EN(x)          (((x)&1)<<1)
#define v_W1_BLEND_EN(x)          (((x)&1)<<2)
#define v_W0_BLEND_EN(x)          (((x)&1)<<3)
#define v_HWC_BLEND_FACTOR(x)    (((x)&15)<<4)
#define v_W2_BLEND_FACTOR(x)     (((x)&0xff)<<8)
#define v_W1_BLEND_FACTOR(x)     (((x)&0xff)<<16)
#define v_W0_BLEND_FACTOR(x)     (((x)&0xff)<<24)

//LCDC_INT_STATUS
#define v_HOR_START_INT_STA        (1<<0)  //status
#define v_FRM_START_INT_STA        (1<<1)
#define v_LINE_FLAG_INT_STA        (1<<2)
#define v_BUS_ERR_INT_STA	   (1<<3)
#define m_HOR_START_INT_EN     	   (1<<4)  //enable
#define m_FRM_START_INT_EN          (1<<5)
#define m_LINE_FLAG_INT_EN         (1<<6)
#define m_BUS_ERR_INT_EN	   (1<<7)
#define m_HOR_START_INT_CLEAR      (1<<8) //auto clear
#define m_FRM_START_INT_CLEAR      (1<<9)
#define m_LINE_FLAG_INT_CLEAR      (1<<10)
#define m_BUS_ERR_INT_CLEAR        (1<<11)
#define m_LINE_FLAG_NUM		   (0xfff<<12)
#define v_HOR_START_INT_EN(x)      (((x)&1)<<4)
#define v_FRM_START_INT_EN(x)      (((x)&1)<<5)
#define v_LINE_FLAG_INT_EN(x)      (((x)&1)<<6)
#define v_BUS_ERR_INT_EN(x)	   (((x)&1)<<7)
#define v_HOR_START_INT_CLEAR(x)      (((x)&1)<<8)
#define v_FRM_START_INT_CLEAR(x)     (((x)&1)<<9)
#define v_LINE_FLAG_INT_CLEAR(x)     (((x)&1)<<10)
#define v_BUS_ERR_INT_CLEAR(x)        (((x)&1)<<11)
#define v_LINE_FLAG_NUM(x)	   (((x)&0xfff)<<12)


//LCDC_WIN_VIR
#define m_WIN0_VIR   (0xfff << 0)
#define m_WIN1_VIR   (0xfff << 16)
//LCDC_WINx_VIR ,x is number of words of win0 virtual width
#define v_WIN0_ARGB888_VIRWIDTH(x) (x)
#define v_WIN0_RGB888_VIRWIDTH(x) (((x*3)>>2)+((x)%3))
#define v_WIN0_RGB565_VIRWIDTH(x) (((x)>>1) + ((x%2)?1:0))
#define v_WIN0_YUV_VIRWIDTH(x)    (((x)>>2) +((x%4)?1:0))

#define v_WIN1_ARGB888_VIRWIDTH(x) (x << 16)
#define v_WIN1_RGB888_VIRWIDTH(x)  ((((x*3)>>2)+((x)%3)) << 16)
#define v_WIN1_RGB565_VIRWIDTH(x)  ((((x)>>1) + ((x%2)?1:0)) << 16)
#define v_WIN1_YUV_VIRWIDTH(x)     ((((x)>>2) +((x%4)?1:0)) << 16 )


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





//AXI MS ID
#define m_W0_YRGB_CH_ID        (0xF<<0)
#define m_W0_CBR_CH_ID         (0xF<<4)
#define m_W1_YRGB_CH_ID        (0xF<<8)
#define m_W2_CH_ID             (0xF<<12)
#define m_HWC_CH_ID            (0xF<<16)
#define v_W0_YRGB_CH_ID(x)        (((x)&0xF)<<0)
#define v_W0_CBR_CH_ID(x)         (((x)&0xF)<<4)
#define v_W1_YRGB_CH_ID(x)        (((x)&0xF)<<8)
#define v_W2_CH_ID(x)             (((x)&0xF)<<12)
#define v_HWC_CH_ID(x)            (((x)&0xF)<<16)


/* Low Bits Mask */
#define m_WORDLO            (0xffff<<0)
#define m_WORDHI            (0xffff<<16)
#define v_WORDLO(x)         (((x)&0xffff)<<0)
#define v_WORDHI(x)         (((x)&0xffff)<<16)


//LCDC_WINx_SCL_FACTOR_Y/CBCR
#define v_X_SCL_FACTOR(x)  ((x)<<0)
#define v_Y_SCL_FACTOR(x)  ((x)<<16)

//LCDC_DSP_HTOTAL_HS_END
#define v_HSYNC(x)  ((x)<<0)   //hsync pulse width
#define v_HORPRD(x) ((x)<<16)   //horizontal period


//LCDC_DSP_HACT_ST_END
#define v_HAEP(x) ((x)<<0)  //horizontal active end point
#define v_HASP(x) ((x)<<16) //horizontal active start point

//LCDC_DSP_VTOTAL_VS_END
#define v_VSYNC(x) ((x)<<0)
#define v_VERPRD(x) ((x)<<16)

//LCDC_DSP_VACT_ST_END
#define v_VAEP(x) ((x)<<0)
#define v_VASP(x) ((x)<<16)



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

//LCDC_WIN0_ACT_INFO
#define v_ACT_WIDTH(x)     ((x-1)<<0)
#define v_ACT_HEIGHT(x)    ((x-1)<<16)

//LCDC_WIN0_DSP_INFO
#define v_DSP_WIDTH(x)     ((x-1)<<0)
#define v_DSP_HEIGHT(x)    ((x-1)<<16)

//LCDC_WIN0_DSP_ST    //x,y start point of the panel scanning
#define v_DSP_STX(x)      (x<<0)
#define v_DSP_STY(x)      (x<<16)

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


//LCDC_SCL_REG0
#define m_SCL_DSP_ZERO 		 (1<<4)
#define m_SCL_DEN_INVERT	 (1<<3)
#define m_SCL_SYNC_INVERT	 (1<<2)
#define m_SCL_DCLK_INVERT	 (1<<1)
#define m_SCL_EN	 	 (1<<0)
#define v_SCL_DSP_ZERO(x) 	 (((x)&1)<<4)
#define v_SCL_DEN_INVERT(x)	 (((x)&1)<<3)
#define v_SCL_SYNC_INVERT(x)	 (((x)&1)<<2)
#define v_SCL_DCLK_INVERT(x)	 (((x)&1)<<1)
#define v_SCL_EN(x)	 	 (((x)&1)<<0)

//LCDC_SCL_REG1
#define m_SCL_V_FACTOR 		 (0x3fff<<16)
#define m_SCL_H_FACTOR		 (0x3fff<<0)
#define v_SCL_V_FACTOR(x) 		 (((x)&0x3fff)<<16)
#define v_SCL_H_FACTOR(x)		 (((x)&0x3fff)<<0)


//LCDC_SCL_REG2
#define m_SCL_DSP_FRAME_VST	(0xfff<<16)
#define m_SCL_DSP_FRAME_HST	(0xfff<<0)
#define v_SCL_DSP_FRAME_VST(x)	(((x)&0xfff)<<16)
#define v_SCL_DSP_FRAME_HST(x)	(((x)&0xfff)<<0)

//LCDC_SCL_REG3
#define m_SCL_DSP_HS_END	(0xff<<16)
#define m_SCL_DSP_HTOTAL	(0xfff<<0)
#define v_SCL_DSP_HS_END(x)	(((x)&0xff)<<16)
#define v_SCL_DSP_HTOTAL(x)	(((x)&0xfff)<<0)

//LCDC_SCL_REG4
#define m_SCL_DSP_HACT_ST	(0x3ff<<16)
#define m_SCL_DSP_HACT_END	(0xfff<<0)
#define v_SCL_DSP_HACT_ST(x)	(((x)&0x3ff)<<16)
#define v_SCL_DSP_HACT_END(x)	(((x)&0xfff)<<0)

//LCDC_SCL_REG5
#define m_SCL_DSP_VS_END	(0xff<<16)
#define m_SCL_DSP_VTOTAL	(0xfff<<0)
#define v_SCL_DSP_VS_END(x)	(((x)&0xff)<<16)
#define v_SCL_DSP_VTOTAL(x)	(((x)&0xfff)<<0)

//LCDC_SCL_REG6
#define m_SCL_DSP_VACT_ST	(0xff<<16)
#define m_SCL_DSP_VACT_END	(0xfff<<0)
#define v_SCL_DSP_VACT_ST(x)	(((x)&0xff)<<16)
#define v_SCL_DSP_VACT_END(x)	(((x)&0xfff)<<0)


//LCDC_SCL_REG7
#define m_SCL_DSP_HBOR_ST	(0x3ff<<16)
#define m_SCL_DSP_HBOR_END	(0xfff<<0)
#define v_SCL_DSP_HBOR_ST(x)	(((x)&0x3ff)<<16)
#define v_SCL_DSP_HBOR_END(x)	(((x)&0xfff)<<0)

//LCDC_SCL_REG8

#define m_SCL_DSP_VBOR_ST	(0xff<<16)
#define m_SCL_DSP_VBOR_END	(0xfff<<0)
#define v_SCL_DSP_VBOR_ST(x)	(((x)&0xff)<<16)
#define v_SCL_DSP_VBOR_END(x)	(((x)&0xfff)<<0)





#define CalScale(x, y)	             (((u32)(x)*0x1000)/(y))
struct rk2928_lcdc_device{
	int id;
	struct rk_lcdc_device_driver driver;
	rk_screen *screen;
	
	LCDC_REG *preg;         // LCDC reg base address and backup reg 
    	LCDC_REG regbak;

	void __iomem *reg_vir_base;  	// virtual basic address of lcdc register
	u32 reg_phy_base;       	// physical basic address of lcdc register
	u32 len;               		// physical map length of lcdc register
	spinlock_t  reg_lock;		//one time only one process allowed to config the register
	bool clk_on;			//if aclk or hclk is closed ,acess to register is not allowed
	u8 atv_layer_cnt;               //active layer counter,when  atv_layer_cnt = 0,disable lcdc
	unsigned int		irq;
	
	struct clk              *pd;                            //lcdc power domain	
	struct clk		*hclk;				//lcdc AHP clk
	struct clk		*dclk;				//lcdc dclk
	struct clk		*aclk;				//lcdc share memory frequency
	struct clk		*sclk;				//scale clk
	struct clk		*aclk_parent;		//lcdc aclk divider frequency source
	struct clk		*aclk_ddr_lcdc; 	//DDR LCDC AXI clock disable.
	struct clk		*aclk_disp_matrix;	//DISPLAY matrix AXI clock disable.
	struct clk		*hclk_cpu_display;	//CPU DISPLAY AHB bus clock disable.
	struct clk		*pd_display;		// display power domain
	u32	pixclock;
};

struct lcdc_info{
/*LCD CLK*/
	struct rk2928_lcdc_device lcdc0;

};


struct win_set {
	volatile u32 y_offset;
	volatile u32 c_offset;
};

struct win0_par {
    u32 refcount;
    u32	pseudo_pal[16];
    u32 y_offset;
    u32 c_offset;
    u32 xpos;         //size in panel
    u32 ypos;
    u32 xsize;        //start point in panel
    u32 ysize;
    enum data_format format;

    wait_queue_head_t wait;
    struct win_set mirror;
    struct win_set displ;
    struct win_set done;

    u8 par_seted;
    u8 addr_seted;
};

#endif


