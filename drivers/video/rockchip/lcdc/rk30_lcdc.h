/* SPDX-License-Identifier: GPL-2.0 */
#ifndef RK30_LCDC_H_
#define RK30_LCDC_H_

#include<linux/rk_fb.h>


#if 0
#define LcdReadBit(inf, addr, msk)      ((inf->regbak.addr=inf->preg->addr)&(msk))
#define LcdWrReg(inf, addr, val)        inf->preg->addr=inf->regbak.addr=(val)
#define LcdRdReg(inf, addr)             (inf->preg->addr)
#define LcdSetBit(inf, addr, msk)       inf->preg->addr=((inf->regbak.addr) |= (msk))
#define LcdClrBit(inf, addr, msk)       inf->preg->addr=((inf->regbak.addr) &= ~(msk))
#define LcdSetRegBit(inf, addr, msk)    inf->preg->addr=((inf->preg->addr) |= (msk))
#define LcdMskReg(inf, addr, msk, val)  (inf->regbak.addr)&=~(msk);   inf->preg->addr=(inf->regbak.addr|=(val))
#define LCDC_REG_CFG_DONE()		LcdWrReg(lcdc_dev, REG_CFG_DONE, 0x01); dsb()

/********************************************************************
**                          结构定义                                *
********************************************************************/
/* LCDC的寄存器结构 */

typedef volatile struct tagLCDC_REG
{
    /* offset 0x00~0xc0 */
	unsigned int SYS_CTRL0;               //0x00 system control register 0
	unsigned int SYS_CTRL1;				  //0x04 system control register 1
	unsigned int DSP_CTRL0;				 //0x08 display control register 0
	unsigned int DSP_CTRL1;				 //0x0c display control register 1
	unsigned int INT_STATUS;             //0x10 Interrupt status register
	unsigned int MCU_CTRL ;				 //0x14 MCU mode contol register
	unsigned int BLEND_CTRL;             //0x18 Blending control register
	unsigned int WIN0_COLOR_KEY_CTRL;     //0x1c Win0 blending control register
	unsigned int WIN1_COLOR_KEY_CTRL;     //0x20 Win1 blending control register
	unsigned int WIN2_COLOR_KEY_CTRL;     //0x24 Win2 blending control register
	unsigned int WIN0_YRGB_MST0;           //0x28 Win0 active YRGB memory start address0
	unsigned int WIN0_CBR_MST0;            //0x2c Win0 active Cbr memory start address0
	unsigned int WIN0_YRGB_MST1;           //0x30 Win0 active YRGB memory start address1
	unsigned int WIN0_CBR_MST1;            //0x34 Win0 active Cbr memory start address1
	unsigned int WIN0_VIR;                //0x38 WIN0 virtual display width/height
	unsigned int WIN0_ACT_INFO;           //0x3C Win0 active window width/height
	unsigned int WIN0_DSP_INFO;           //0x40 Win0 display width/height on panel
	unsigned int WIN0_DSP_ST;             //0x44 Win0 display start point on panel
	unsigned int WIN0_SCL_FACTOR_YRGB;    //0x48Win0 YRGB scaling  factor setting
	unsigned int WIN0_SCL_FACTOR_CBR;     //0x4c Win0 YRGB scaling factor setting
	unsigned int WIN0_SCL_OFFSET;         //0x50 Win0 Cbr scaling start point offset
	unsigned int WIN1_YRGB_MST;           //0x54 Win1 active YRGB memory start address
	unsigned int WIN1_CBR_MST;            //0x58 Win1 active Cbr memory start address
	unsigned int WIN1_VIR;                //0x5c WIN1 virtual display width/height
	unsigned int WIN1_ACT_INFO;           //0x60 Win1 active window width/height
	unsigned int WIN1_DSP_INFO;           //0x64 Win1 display width/height on panel
	unsigned int WIN1_DSP_ST;             //0x68 Win1 display start point on panel
	unsigned int WIN1_SCL_FACTOR_YRGB;    //0x6c Win1 YRGB scaling  factor setting
	unsigned int WIN1_SCL_FACTOR_CBR;     //0x70 Win1 YRGB scaling factor setting
	unsigned int WIN1_SCL_OFFSET;         //0x74 Win1 Cbr scaling start point offset
	unsigned int WIN2_MST;		 		  //0x78 win2 memort start address
	unsigned int WIN2_VIR;				  //0x7c win2 virtual stride
	unsigned int WIN2_DSP_INFO;           //0x80 Win2 display width/height on panel
	unsigned int WIN2_DSP_ST;             //0x84 Win2 display start point on panel
	unsigned int HWC_MST;                 //0x88 HWC memory start address
	unsigned int HWC_DSP_ST;              //0x8C HWC display start point on panel
	unsigned int HWC_COLOR_LUT0;          //0x90 Hardware cursor color 2’b01 look up table 0
	unsigned int HWC_COLOR_LUT1;          //0x94 Hardware cursor color 2’b10 look up table 1
	unsigned int HWC_COLOR_LUT2;          //0x98 Hardware cursor color 2’b11 look up table 2
	unsigned int DSP_HTOTAL_HS_END;       //0x9c Panel scanning horizontal width and hsync pulse end point
	unsigned int DSP_HACT_ST_END;         //0xa0 Panel active horizontal scanning start/end point
	unsigned int DSP_VTOTAL_VS_END;       //0xa4 Panel scanning vertical height and vsync pulse end point
	unsigned int DSP_VACT_ST_END;         //0xa8 Panel active vertical scanning start/end point
	unsigned int DSP_VS_ST_END_F1;        //0xac Vertical scanning start point and vsync pulse end point of even filed in interlace mode
	unsigned int DSP_VACT_ST_END_F1;      //0xb0 Vertical scanning active start/end point of even filed in interlace mode
	unsigned int reserved0[(0xc0-0xb4)/4];
	unsigned int REG_CFG_DONE;            //0xc0 REGISTER CONFIG FINISH
	unsigned int reserved1[(0x100-0xc4)/4];
	unsigned int MCU_BYPASS_WPORT;         //0x100 MCU BYPASS MODE, DATA Write Only Port
	unsigned int reserved2[(0x200-0x104)/4];
	unsigned int MCU_BYPASS_RPORT;         //0x200 MCU BYPASS MODE, DATA Read Only Port   
	unsigned int reserved3[(0x400-0x204)/4];
	unsigned int WIN2_LUT_ADDR;
	unsigned int reserved4[(0x800-0x404)/4];
	unsigned int DSP_LUT_ADDR;
  
} LCDC_REG, *pLCDC_REG;

#else

#define SYS_CTRL0  	0x00			//0x00 system control register 0
#define SYS_CTRL1	0x04			//0x04 system control register 1
#define DSP_CTRL0	0x08			//0x08 display control register 0
#define DSP_CTRL1	0x0c			//0x0c display control register 1
#define INT_STATUS 	0x10            	//0x10 Interrupt status register
#define MCU_CTRL	0x14			//0x14 MCU mode contol register
#define BLEND_CTRL 	0x18           		//0x18 Blending control register
#define WIN0_COLOR_KEY_CTRL	0x1c    	//0x1c Win0 blending control register
#define WIN1_COLOR_KEY_CTRL 	0x20    	//0x20 Win1 blending control register
#define WIN2_COLOR_KEY_CTRL 	0x24    	//0x24 Win2 blending control register
#define WIN0_YRGB_MST0 		0x28          //0x28 Win0 active YRGB memory start address0
#define WIN0_CBR_MST0 		0x2c           //0x2c Win0 active Cbr memory start address0
#define WIN0_YRGB_MST1		0x30           //0x30 Win0 active YRGB memory start address1
#define WIN0_CBR_MST1		0x34            //0x34 Win0 active Cbr memory start address1
#define WIN0_VIR 		0x38              //0x38 WIN0 virtual display width/height
#define WIN0_ACT_INFO		0x3c           //0x3C Win0 active window width/height
#define WIN0_DSP_INFO 		0x40          //0x40 Win0 display width/height on panel
#define WIN0_DSP_ST  		0x44          //0x44 Win0 display start point on panel
#define WIN0_SCL_FACTOR_YRGB	0x48    //0x48Win0 YRGB scaling  factor setting
#define WIN0_SCL_FACTOR_CBR 	0x4c    //0x4c Win0 YRGB scaling factor setting
#define WIN0_SCL_OFFSET 	0x50        //0x50 Win0 Cbr scaling start point offset
#define WIN1_YRGB_MST  		0x54       //0x54 Win1 active YRGB memory start address
#define WIN1_CBR_MST		0x58       //0x58 Win1 active Cbr memory start address
#define WIN1_VIR 		0x5c       //0x5c WIN1 virtual display width/height
#define WIN1_ACT_INFO 		0x60          //0x60 Win1 active window width/height
#define WIN1_DSP_INFO		0x64           //0x64 Win1 display width/height on panel
#define WIN1_DSP_ST		0x68             //0x68 Win1 display start point on panel
#define WIN1_SCL_FACTOR_YRGB    0x6c   //0x6c Win1 YRGB scaling  factor setting
#define WIN1_SCL_FACTOR_CBR	0x70     //0x70 Win1 YRGB scaling factor setting
#define WIN1_SCL_OFFSET 	0x74        //0x74 Win1 Cbr scaling start point offset
#define WIN2_MST		0x78	 //0x78 win2 memort start address
#define WIN2_VIR		0x7c	//0x7c win2 virtual stride
#define WIN2_DSP_INFO 		0x80    //0x80 Win2 display width/height on panel
#define WIN2_DSP_ST		0x84    //0x84 Win2 display start point on panel
#define HWC_MST                 0x88	//0x88 HWC memory start address
#define HWC_DSP_ST		0x8c    //0x8C HWC display start point on panel
#define HWC_COLOR_LUT0 		0x90         //0x90 Hardware cursor color 2’b01 look up table 0
#define HWC_COLOR_LUT1		0x94          //0x94 Hardware cursor color 2’b10 look up table 1
#define HWC_COLOR_LUT2          0x98		//0x98 Hardware cursor color 2’b11 look up table 2
#define DSP_HTOTAL_HS_END       0x9c		//0x9c Panel scanning horizontal width and hsync pulse end point
#define DSP_HACT_ST_END		0xa0         //0xa0 Panel active horizontal scanning start/end point
#define DSP_VTOTAL_VS_END	0xa4       //0xa4 Panel scanning vertical height and vsync pulse end point
#define DSP_VACT_ST_END		0xa8         //0xa8 Panel active vertical scanning start/end point
#define DSP_VS_ST_END_F1 	0xac       //0xac Vertical scanning start point and vsync pulse end point of even filed in interlace mode
#define DSP_VACT_ST_END_F1     	0xb0		//0xb0 Vertical scanning active start/end point of even filed in interlace mode
#define REG_CFG_DONE		0xc0            //0xc0 REGISTER CONFIG FINISH
#define MCU_BYPASS_WPORT 	0x100        //0x100 MCU BYPASS MODE, DATA Write Only Port
#define MCU_BYPASS_RPORT        0x200		//0x200 MCU BYPASS MODE, DATA Read Only Port   
#define WIN2_LUT_ADDR		0x400
#define DSP_LUT_ADDR		0x800

#if 0
#define lcdc_writel(lcdc_dev,offset,v)  do { \
	u32 *_pv = (u32*)lcdc_dev->regsbak;	\
	_pv += (offset >> 2);			\
	writel_relaxed(v,lcdc_dev->regs+offset);\
	*_pv = v;	\
} while(0)

#define lcdc_readl(lcdc_dev,offset)  		\
	readl_relaxed(lcdc_dev->regs+offset)

#define lcdc_read_bit(lcdc_dev,offset,msk) ( { \
	u32 _v = readl_relaxed(lcdc_dev->regs+offset); \
	_v &= msk;_v;	} )

#define lcdc_set_bit(lcdc_dev,offset,msk) do {  \
	u32* _pv = (u32*)lcdc_dev->regsbak;	\
	_pv += (offset >> 2);				\
	(*_pv) |= msk;				\
	writel_relaxed(*_pv,lcdc_dev->regs + offset); \
} while(0)

#define lcdc_clr_bit(lcdc_dev,offset,msk) do{	\
	u32* _pv = (u32*)lcdc_dev->regsbak;	\
	_pv += (offset >> 2);				\
	(*_pv) &= ~msk;				\
	writel_relaxed(*_pv,lcdc_dev->regs + offset); \
} while (0)

#define lcdc_msk_reg(lcdc_dev,offset,msk,v) do {	\
	u32 *_pv = (u32*)lcdc_dev->regsbak;	\
	_pv += (offset >> 2);			\
	(*_pv) &= (~msk);				\
	(*_pv) |= v;				\
	writel_relaxed(*_pv,lcdc_dev->regs+offset);	\
} while(0)

#define lcdc_cfg_done(lcdc_dev) do{ \
	writel_relaxed(0x01,lcdc_dev->regs+REG_CFG_DONE); \
	dsb();						\
} while(0)
#endif
#endif

/* SYS_CONFIG */

#define m_LCDC_DMA_STOP              (1<<0)
#define m_LCDC_STANDBY               (1<<1)
#define m_HWC_RELOAD_EN               (1<<2)
#define m_W0_AXI_OUTSTANDING_DISABLE (1<<3) 
#define m_W1_AXI_OUTSTANDING_DISABLE (1<<4)
#define m_W2_AXI_OUTSTANDING_DISABLE (1<<5)
#define m_DMA_BURST_LENGTH	      (3<<6)
#define m_WIN0_YRGB_CHANNEL0_ID	      ((0x07)<<8)
#define m_WIN0_CBR_CHANNEL0_ID	      ((0x07)<<11)
#define m_WIN0_YRGB_CHANNEL1_ID	      ((0x07)<<14)
#define m_WIN0_CBR_CHANNEL1_ID	      ((0x07)<<17)
#define m_WIN1_YRGB_CHANNEL_ID	      ((0x07)<<20)
#define m_WIN1_CBR_CHANNEL_ID	      ((0x07)<<23)
#define m_WIN2_CHANNEL_ID	      ((0x07)<<26)
#define m_HWC_CHANNEL_ID	      ((0x07)<<29)





#define v_LCDC_DMA_STOP(x)              (((x)&1)<<0)
#define v_LCDC_STANDBY(x)              (((x)&1)<<1)
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



//LCDC_SYS_CTRL1
#define m_W0_EN          (1<<0)
#define m_W1_EN          (1<<1)
#define m_W2_EN         (1<<2)
#define m_HWC_EN         (1<<3)
#define m_W0_FORMAT          (7<<4)
#define m_W1_FORMAT          (7<<7)
#define m_W2_FORMAT          (7<<10)
#define m_HWC_COLOR_MODE     (1<<13)
#define m_HWC_SIZE_SELET     (1<<14)
#define m_W0_3D_MODE_EN      (1<<15)
#define m_W0_3D_MODE_SELET    (7<<16)
#define m_W0_RGB_RB_SWAP      (1<<19)
#define m_W0_RGB_ALPHA_SWAP   (1<<20)
#define m_W0_YRGB_M8_SWAP     (1<<21)
#define m_W0_CBCR_SWAP        (1<<22)
#define m_W1_RGB_RB_SWAP      (1<<23)
#define m_W1_RGB_ALPHA_SWAP   (1<<24)
#define m_W1_YRGB_M8_SWAP     (1<<25)
#define m_W1_CBCR_SWAP        (1<<26)
#define m_W2_RGB_RB_SWAP      (1<<27)
#define m_W2_RGB_ALPHA_SWAP   (1<<28)
#define m_W2_8pp_PALETTE_ENDIAN_SELECT (1<<29)
#define m_W2_LUT_RAM_EN       (1<<30)
#define m_DSP_LUT_RAM_EN      (1<<31)

#define v_W0_EN(x)          (((x)&1)<<0)
#define v_W1_EN(x)          (((x)&1)<<1)
#define v_W2_EN(x)          (((x)&1)<<2)
#define v_HWC_EN(x)         (((x)&1)<<3)
#define v_W0_FORMAT(x)      (((x)&7)<<4)
#define v_W1_FORMAT(x)      (((x)&7)<<7)
#define v_W2_FORMAT(x)      (((x)&7)<<10)
#define v_HWC_COLOR_MODE(x)     (((x)&1)<<13)
#define v_HWC_SIZE_SELET(x)     (((x)&1)<<14)
#define v_W0_3D_MODE_EN(x)     (((x)&1)<<15)
#define v_W0_3D_MODE_SELET(x)    (((x)&3)<<16)
#define v_W0_RGB_RB_SWAP(x)      (((x)&1)<<19)
#define v_W0_RGB_ALPHA_SWAP(x)   (((x)&1)<<20)
#define v_W0_YRGB_M8_SWAP(x)     (((x)&1)<<21)
#define v_W0_CBCR_SWAP(x)       (((x)&1)<<22)
#define v_W1_RGB_RB_SWAP(x)      (((x)&1)<<23)
#define v_W1_RGB_ALPHA_SWAP(x)   (((x)&1)<<24)
#define v_W1_YRGB_M8_SWAP(x)     (((x)&1)<<25)
#define v_W1_CBCR_SWAP(x)       (((x)&1)<<26)
#define v_W2_RGB_RB_SWAP(x)      (((x)&1)<<27)
#define v_W2_RGB_ALPHA_SWAP(x)   (((x)&1)<<28)
#define v_W2_8pp_PALETTE_ENDIAN_SELECT (((x)&1)<<29)
#define v_W2_LUT_RAM_EN(x)      (((x)&1)<<30)
#define v_DSP_LUT_RAM_EN(x)      (((x)&1)<<31)

//LCDC_DSP_CTRL_REG0
#define m_DISPLAY_FORMAT             (0x0f<<0)
#define m_HSYNC_POLARITY             (1<<4)
#define m_VSYNC_POLARITY             (1<<5)
#define m_DEN_POLARITY               (1<<6)
#define m_DCLK_POLARITY              (1<<7)
#define m_W0W1_POSITION_SWAP         (1<<8)
#define m_DITHER_UP_EN               (1<<9)
#define m_DITHER_DOWN_MODE           (1<<10)
#define m_DITHER_DOWN_EN             (1<<11)
#define m_INTERLACE_DSP_EN           (1<<12)
#define m_INTERLACE_FIELD_POLARITY   (1<<13)
#define m_W0_INTERLACE_READ_MODE     (1<<14)
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
#define m_LCDC_AXICLK_AUTO_ENABLE 	(1<<31) //eanble for low power

#define v_DISPLAY_FORMAT(x)            (((x)&0xf)<<0)
#define v_HSYNC_POLARITY(x)             (((x)&1)<<4)
#define v_VSYNC_POLARITY(x)             (((x)&1)<<5)
#define v_DEN_POLARITY(x)               (((x)&1)<<6)
#define v_DCLK_POLARITY(x)              (((x)&1)<<7)
#define v_W0W1_POSITION_SWAP(x)			(((x)&1)<<8)		
#define v_DITHER_UP_EN(x)               (((x)&1)<<9)
#define v_DITHER_DOWN_MODE(x)           (((x)&1)<<10)
#define v_DITHER_DOWN_EN(x)             (((x)&1)<<11)
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
#define v_LCDC_AXICLK_AUTO_ENABLE(x) 	(((x)&1)<<31) //eanble for low power

//LCDC_DSP_CTRL_REG1
#define m_BG_COLOR                    (0xffffff<<0)
#define m_BG_B                        (0xff<<0)
#define m_BG_G                        (0xff<<8)
#define m_BG_R                        (0xff<<16)
#define m_BLANK_MODE                  (1<<24)
#define m_BLACK_MODE                  (1<<25)
#define m_OUTPUT_BG_SWAP        	  (1<<26)
#define m_OUTPUT_RB_SWAP        	  (1<<27)
#define m_OUTPUT_RG_SWAP        	  (1<<28)
#define m_DELTA_SWAP            	  (1<<29)
#define m_DUMMY_SWAP            	  (1<<30)

#define v_BG_COLOR(x)                 (((x)&0xffffff)<<0)
#define v_BG_B(x)                     (((x)&0xff)<<0)
#define v_BG_G(x)                     (((x)&0xff)<<8)
#define v_BG_R(x)                     (((x)&0xff)<<16)
#define v_BLANK_MODE(x)               (((x)&1)<<24)
#define v_BLACK_MODE(x)               (((x)&1)<<25)
#define v_OUTPUT_BG_SWAP(x)        	  (((x)&1)<<26)
#define v_OUTPUT_RB_SWAP(x)        	  (((x)&1)<<27)
#define v_OUTPUT_RG_SWAP(x)        	  (((x)&1)<<28)
#define v_DELTA_SWAP(x)            	  (((x)&1)<<29)
#define v_DUMMY_SWAP(x)            	  (((x)&1)<<30)


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



//LCDC_MCU_TIMING_CTRL
#define m_MCU_WRITE_PERIOD      (0x3f<<0)
#define m_MCU_CS_ST             (0xf<<6)
#define m_MCU_CS_END            (0x3f<<10)
#define m_MCU_RW_ST             (0xf<<16)
#define m_MCU_RW_END            (0x3f<<20)
#define m_MCU_BPS_CLK_SEL		(1<<26)
#define m_MCU_HOLDMODE_SELECT     (1<<27)
#define m_MCU_HOLDMODE_FRAME_ST   (1<<28)
#define m_MCU_RS_SELECT            (1<<29)
#define m_MCU_BYPASSMODE_SELECT   (1<<30)
#define m_MCU_OUTPUT_SELECT        (1<<31)
#define v_MCU_WRITE_PERIOD(x)      (((x)&0x3f)<<0)
#define v_MCU_CS_ST(x)          (((x)&0xf)<<6)
#define v_MCU_CS_END(x)         (((x)&0x3f)<<10)
#define v_MCU_RW_ST(x)          (((x)&0xf)<<16)
#define v_MCU_RW_END(x)         (((x)&0x3f)<<20)
#define v_MCU_BPS_CLK_SEL	(((x)&1)<<26)
#define v_MCU_HOLDMODE_SELECT(x)     (((x)&1)<<27)
#define v_MCU_HOLDMODE_FRAME_ST(x)   (((x)&1)<<28)
#define v_MCU_RS_SELECT(x)            (((x)&1)<<29)
#define v_MCU_BYPASSMODE_SELECT(x)   (((x)&1)<<30)
#define v_MCU_OUTPUT_SELECT(x)        (((x)&1)<<31)

//LCDC_ BLEND_CTRL
#define m_W0_BLEND_EN         (1<<0)
#define m_W1_BLEND_EN          (1<<1)
#define m_W2_BLEND_EN          (1<<2)
#define m_HWC_BLEND_EN          (1<<3)
#define m_W0_BLEND_FACTOR     (15<<4)
#define m_W1_BLEND_FACTOR     (0xff<<8)
#define m_W2_BLEND_FACTOR     (0xff<<16)
#define m_HWC_BLEND_FACTOR     (0xff<<24)

#define v_W0_BLEND_EN(x)         (((x)&1)<<0)
#define v_W1_BLEND_EN(x)          (((x)&1)<<1)
#define v_W2_BLEND_EN(x)          (((x)&1)<<2)
#define v_HWC_BLEND_EN(x)          (((x)&1)<<3)
#define v_W0_BLEND_FACTOR(x)    (((x)&15)<<4)
#define v_W1_BLEND_FACTOR(x)     (((x)&0xff)<<8)
#define v_W2_BLEND_FACTOR(x)     (((x)&0xff)<<16)
#define v_HWC_BLEND_FACTOR(x)     (((x)&0xff)<<24)


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


//LCDC_WINx_VIR ,x is number of words of win0 virtual width
#define v_ARGB888_VIRWIDTH(x) (x)
#define v_RGB888_VIRWIDTH(x) (((x*3)>>2)+((x)%3))
#define v_RGB565_VIRWIDTH(x) (((x)>>1) + ((x%2)?1:0))
#define v_YUV_VIRWIDTH(x)    (((x)>>2) +((x%4)?1:0))

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



#define CalScale(x, y)	             (((u32)x*0x1000)/y)
struct rk30_lcdc_device{
	int id;
	struct rk_lcdc_device_driver driver;
	rk_screen *screen;
	
	//LCDC_REG *preg;         // LCDC reg base address and backup reg 
    	//LCDC_REG regbak;
    	void __iomem *regs;
	void *regsbak;		//back up reg
	int __iomem *dsp_lut_addr_base;

	void __iomem *reg_vir_base;  	// virtual basic address of lcdc register
	u32 reg_phy_base;       	// physical basic address of lcdc register
	u32 len;               		// physical map length of lcdc register
	spinlock_t  reg_lock;		//one time only one process allowed to config the register
	bool clk_on;			//if aclk or hclk is closed ,acess to register is not allowed
	u8 atv_layer_cnt;		//active layer counter,when  atv_layer_cnt = 0,disable lcdc
	
	unsigned int		irq;

	struct clk		*pd;				//lcdc power domain
	struct clk		*hclk;				//lcdc AHP clk
	struct clk		*dclk;				//lcdc dclk
	struct clk		*aclk;				//lcdc share memory frequency
	struct clk		*aclk_parent;		//lcdc aclk divider frequency source
	struct clk		*aclk_ddr_lcdc; 	//DDR LCDC AXI clock disable.
	struct clk		*aclk_disp_matrix;	//DISPLAY matrix AXI clock disable.
	struct clk		*hclk_cpu_display;	//CPU DISPLAY AHB bus clock disable.
	struct clk		*pd_display;		// display power domain
	u32	pixclock;
};

struct lcdc_info{
/*LCD CLK*/
	struct rk30_lcdc_device lcdc0;
	struct rk30_lcdc_device lcdc1;

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


static inline void lcdc_writel(struct rk30_lcdc_device *lcdc_dev,u32 offset,u32 v)
{
	u32 *_pv = (u32*)lcdc_dev->regsbak;	
	_pv += (offset >> 2);	
	*_pv = v;
	writel_relaxed(v,lcdc_dev->regs+offset);	
}

static inline u32 lcdc_readl(struct rk30_lcdc_device *lcdc_dev,u32 offset)
{
	u32 v;
	u32 *_pv = (u32*)lcdc_dev->regsbak;	
	_pv += (offset >> 2);
	v = readl_relaxed(lcdc_dev->regs+offset);
	*_pv = v;
	return v;
}

static inline u32 lcdc_read_bit(struct rk30_lcdc_device *lcdc_dev,u32 offset,u32 msk) 
{
       u32 _v = readl_relaxed(lcdc_dev->regs+offset); 
       _v &= msk;
       return (_v >> msk);   
}

static inline void  lcdc_set_bit(struct rk30_lcdc_device *lcdc_dev,u32 offset,u32 msk) 
{
	u32* _pv = (u32*)lcdc_dev->regsbak;	
	_pv += (offset >> 2);				
	(*_pv) |= msk;				
	writel_relaxed(*_pv,lcdc_dev->regs + offset); 
} 

static inline void lcdc_clr_bit(struct rk30_lcdc_device *lcdc_dev,u32 offset,u32 msk)
{
	u32* _pv = (u32*)lcdc_dev->regsbak;	
	_pv += (offset >> 2);				
	(*_pv) &= (~msk);				
	writel_relaxed(*_pv,lcdc_dev->regs + offset); 
} 

static inline void  lcdc_msk_reg(struct rk30_lcdc_device *lcdc_dev,u32 offset,u32 msk,u32 v)
{
	u32 *_pv = (u32*)lcdc_dev->regsbak;	
	_pv += (offset >> 2);			
	(*_pv) &= (~msk);				
	(*_pv) |= v;				
	writel_relaxed(*_pv,lcdc_dev->regs+offset);	
}

static inline void lcdc_cfg_done(struct rk30_lcdc_device *lcdc_dev) 
{
	writel_relaxed(0x01,lcdc_dev->regs+REG_CFG_DONE); 
	dsb();						
} 

#endif


