#ifndef RK3188_LCDC_H_
#define RK3188_LCDC_H_

#include<linux/rk_fb.h>
#include<linux/io.h>
#include<linux/clk.h>


/*******************register definition**********************/

#define SYS_CTRL 		(0x00)
#define m_WIN0_EN		(1<<0)
#define m_WIN1_EN		(1<<1)
#define m_HWC_EN		(1<<2)
#define m_WIN0_FORMAT		(7<<3)
#define m_WIN1_FORMAT		(7<<6)
#define m_HWC_COLOR_MODE	(1<<9)
#define m_HWC_SIZE		(1<<10)
#define m_WIN0_3D_EN		(1<<11)
#define m_WIN0_3D_MODE		(7<<12)
#define m_WIN0_RB_SWAP		(1<<15)
#define m_WIN0_ALPHA_SWAP	(1<<16)
#define m_WIN0_Y8_SWAP		(1<<17)
#define m_WIN0_UV_SWAP		(1<<18)
#define m_WIN1_RB_SWAP		(1<<19)
#define m_WIN1_ALPHA_SWAP	(1<<20)
#define m_WIN1_BL_SWAP		(1<<21)
#define m_WIN0_OTSD_DISABLE	(1<<22)
#define m_WIN1_OTSD_DISABLE	(1<<23)
#define m_DMA_BURST_LENGTH	(3<<24)
#define m_HWC_LODAD_EN		(1<<26)
#define m_WIN1_LUT_EN		(1<<27)
#define m_DSP_LUT_EN		(1<<28)
#define m_DMA_STOP		(1<<29)
#define m_LCDC_STANDBY		(1<<30)
#define m_AUTO_GATING_EN	(1<<31)
#define v_WIN0_EN(x)		(((x)&1)<<0)
#define v_WIN1_EN(x)		(((x)&1)<<1)
#define v_HWC_EN(x)		(((x)&1)<<2)
#define v_WIN0_FORMAT(x)	(((x)&7)<<3)
#define v_WIN1_FORMAT(x)	(((x)&7)<<6)
#define v_HWC_COLOR_MODE(x)	(((x)&1)<<9)
#define v_HWC_SIZE(x)		(((x)&1)<<10)
#define v_WIN0_3D_EN(x)		(((x)&1)<<11)
#define v_WIN0_3D_MODE(x)	(((x)&7)<<12)
#define v_WIN0_RB_SWAP(x)	(((x)&1)<<15)
#define v_WIN0_ALPHA_SWAP(x)	(((x)&1)<<16)
#define v_WIN0_Y8_SWAP(x)	(((x)&1)<<17)
#define v_WIN0_UV_SWAP(x)	(((x)&1)<<18)
#define v_WIN1_RB_SWAP(x)	(((x)&1)<<19)
#define v_WIN1_ALPHA_SWAP(x)	(((x)&1)<<20)
#define v_WIN1_BL_SWAP(x)	(((x)&1)<<21)
#define v_WIN0_OTSD_DISABLE(x)	(((x)&1)<<22)
#define v_WIN1_OTSD_DISABLE(x)	(((x)&1)<<23)
#define v_DMA_BURST_LENGTH(x)	(((x)&3)<<24)
#define v_HWC_LODAD_EN(x)	(((x)&1)<<26)
#define v_WIN1_LUT_EN(x)	(((x)&1)<<27)
#define v_DSP_LUT_EN(x)		(((x)&1)<<28)
#define v_DMA_STOP(x)		(((x)&1)<<29)
#define v_LCDC_STANDBY(x)	(((x)&1)<<30)
#define v_AUTO_GATING_EN(x)	(((x)&1)<<31)


#define DSP_CTRL0		(0x04)
#define m_DSP_OUT_FORMAT	(0x0f<<0)
#define m_HSYNC_POL		(1<<4)
#define m_VSYNC_POL		(1<<5)
#define m_DEN_POL		(1<<6)
#define m_DCLK_POL		(1<<7)
#define m_WIN0_TOP		(1<<8)
#define m_DITHER_UP_EN		(1<<9)
#define m_DITHER_DOWN_MODE	(1<<10)
#define m_DITHER_DOWN_EN	(1<<11)
#define m_INTERLACE_DSP_EN	(1<<12)
#define m_INTERLACE_POL		(1<<13)
#define m_WIN0_INTERLACE_EN	(1<<14)
#define m_WIN1_INTERLACE_EN	(1<<15)
#define m_WIN0_YRGB_DEFLICK_EN	(1<<16)
#define m_WIN0_CBR_DEFLICK_EN	(1<<17)
#define m_WIN0_ALPHA_MODE	(1<<18)
#define m_WIN1_ALPHA_MODE	(1<<19)
#define m_WIN0_CSC_MODE		(3<<20)
#define m_WIN1_CSC_MODE		(1<<22)
#define m_WIN0_YUV_CLIP		(1<<23)
#define m_DSP_CCIR656_AVG	(1<<24)
#define m_DCLK_OUTPUT_MODE	(1<<25)
#define m_DCLK_PHASE_LOCK	(1<<26)
#define m_DITHER_DOWN_SEL	(3<<27)
#define m_ALPHA_MODE_SEL0	(1<<29)
#define m_ALPHA_MODE_SEL1	(1<<30)
#define m_DIFF_DCLK_EN		(1<<31)
#define v_DSP_OUT_FORMAT(x)	(((x)&0x0f)<<0)
#define v_HSYNC_POL(x)		(((x)&1)<<4)
#define v_VSYNC_POL(x)		(((x)&1)<<5)
#define v_DEN_POL(x)		(((x)&1)<<6)
#define v_DCLK_POL(x)		(((x)&1)<<7)
#define v_WIN0_TOP(x)		(((x)&1)<<8)
#define v_DITHER_UP_EN(x)	(((x)&1)<<9)
#define v_DITHER_DOWN_MODE(x)	(((x)&1)<<10)
#define v_DITHER_DOWN_EN(x)	(((x)&1)<<11)
#define v_INTERLACE_DSP_EN(x)	(((x)&1)<<12)
#define v_INTERLACE_POL(x)	(((x)&1)<<13)
#define v_WIN0_INTERLACE_EN(x)	(((x)&1)<<14)
#define v_WIN1_INTERLACE_EN(x)	(((x)&1)<<15)
#define v_WIN0_YRGB_DEFLICK_EN(x)	(((x)&1)<<16)
#define v_WIN0_CBR_DEFLICK_EN(x)	(((x)&1)<<17)
#define v_WIN0_ALPHA_MODE(x)		(((x)&1)<<18)
#define v_WIN1_ALPHA_MODE(x)		(((x)&1)<<19)
#define v_WIN0_CSC_MODE(x)		(((x)&3)<<20)
#define v_WIN1_CSC_MODE(x)		(((x)&1)<<22)
#define v_WIN0_YUV_CLIP(x)		(((x)&1)<<23)
#define v_DSP_CCIR656_AVG(x)		(((x)&1)<<24)
#define v_DCLK_OUTPUT_MODE(x)		(((x)&1)<<25)
#define v_DCLK_PHASE_LOCK(x)		(((x)&1)<<26)
#define v_DITHER_DOWN_SEL(x)		(((x)&1)<<27)
#define v_ALPHA_MODE_SEL0(x)		(((x)&1)<<29)
#define v_ALPHA_MODE_SEL1(x)		(((x)&1)<<30)
#define v_DIFF_DCLK_EN(x)		(((x)&1)<<31)


#define DSP_CTRL1		(0x08)
#define m_BG_COLOR		(0xffffff<<0)
#define m_BG_B			(0xff<<0)
#define m_BG_G			(0xff<<8)
#define m_BG_R			(0xff<<16)
#define m_BLANK_EN		(1<<24)
#define m_BLACK_EN		(1<<25)
#define m_DSP_BG_SWAP		(1<<26)
#define m_DSP_RB_SWAP		(1<<27)
#define m_DSP_RG_SWAP		(1<<28)
#define m_DSP_DELTA_SWAP	(1<<29)
#define m_DSP_DUMMY_SWAP	(1<<30)
#define m_DSP_OUT_ZERO		(1<<31)
#define v_BG_COLOR(x)		(((x)&0xffffff)<<0)
#define v_BG_B(x)		(((x)&0xff)<<0)
#define v_BG_G(x)		(((x)&0xff)<<8)
#define v_BG_R(x)		(((x)&0xff)<<16)
#define v_BLANK_EN(x)		(((x)&1)<<24)
#define v_BLACK_EN(x)		(((x)&1)<<25)
#define v_DSP_BG_SWAP(x)	(((x)&1)<<26)
#define v_DSP_RB_SWAP(x)	(((x)&1)<<27)
#define v_DSP_RG_SWAP(x)	(((x)&1)<<28)
#define v_DSP_DELTA_SWAP(x)	(((x)&1)<<29)
#define v_DSP_DUMMY_SWAP(x)	(((x)&1)<<30)
#define v_DSP_OUT_ZERO(x)	(((x)&1)<<31)


#define MCU_CTRL		(0x0c)
#define m_MCU_PIX_TOTAL		(0x3f<<0)
#define m_MCU_CS_ST		(0x0f<<6)
#define m_MCU_CS_END		(0x3f<<10)
#define m_MCU_RW_ST		(0x0f<<16)
#define m_MCU_RW_END		(0x3f<<20)
#define m_MCU_CLK_SEL		(1<<26)
#define m_MCU_HOLD_MODE		(1<<27)
#define m_MCU_FS_HOLD_STA	(1<<28)
#define m_MCU_RS_SELECT		(1<<29)
#define m_MCU_BYPASS 		(1<<30)
#define m_MCU_TYPE		(1<<31)

#define v_MCU_PIX_TOTAL(x)		(((x)&0x3f)<<0)
#define v_MCU_CS_ST(x)			(((x)&0x0f)<<6)
#define v_MCU_CS_END(x)			(((x)&0x3f)<<10)
#define v_MCU_RW_ST(x)			(((x)&0x0f)<<16)
#define v_MCU_RW_END(x)			(((x)&0x3f)<<20)
#define v_MCU_CLK_SEL(x)		(((x)&1)<<26)
#define v_MCU_HOLD_MODE(x)		(((x)&1)<<27)
#define v_MCU_FS_HOLD_STA(x)		(((x)&1)<<28)
#define v_MCU_RS_SELECT(x)		(((x)&1)<<29)
#define v_MCU_BYPASS(x) 		(((x)&1)<<30)
#define v_MCU_TYPE(x)			(((x)&1)<<31)

#define INT_STATUS		(0x10)
#define m_HS_INT_STA		(1<<0)  //status
#define m_FS_INT_STA		(1<<1)
#define m_LF_INT_STA		(1<<2)
#define m_BUS_ERR_INT_STA	(1<<3)
#define m_HS_INT_EN		(1<<4)  //enable
#define m_FS_INT_EN          	(1<<5)
#define m_LF_INT_EN         	(1<<6)
#define m_BUS_ERR_INT_EN	(1<<7)
#define m_HS_INT_CLEAR		(1<<8) //auto clear
#define m_FS_INT_CLEAR		(1<<9)
#define m_LF_INT_CLEAR		(1<<10)
#define m_BUS_ERR_INT_CLEAR	(1<<11)
#define m_LF_INT_NUM		(0xfff<<12)
#define v_HS_INT_EN(x)		(((x)&1)<<4)
#define v_FS_INT_EN(x)		(((x)&1)<<5)
#define v_LF_INT_EN(x)		(((x)&1)<<6)
#define v_BUS_ERR_INT_EN(x)	(((x)&1)<<7)
#define v_HS_INT_CLEAR(x)	(((x)&1)<<8)
#define v_FS_INT_CLEAR(x)	(((x)&1)<<9)
#define v_LF_INT_CLEAR(x)	(((x)&1)<<10)
#define v_BUS_ERR_INT_CLEAR(x)	(((x)&1)<<11)
#define v_LF_INT_NUM(x)		(((x)&0xfff)<<12)


#define ALPHA_CTRL		(0x14)
#define m_WIN0_ALPHA_EN		(1<<0)
#define m_WIN1_ALPHA_EN		(1<<1)
#define m_HWC_ALPAH_EN		(1<<2)
#define m_WIN0_ALPHA_VAL	(0xff<<4)
#define m_WIN1_ALPHA_VAL	(0xff<<12)
#define m_HWC_ALPAH_VAL		(0x0f<<20)
#define v_WIN0_ALPHA_EN(x)	(((x)&1)<<0)
#define v_WIN1_ALPHA_EN(x)	(((x)&1)<<1)
#define v_HWC_ALPAH_EN(x)	(((x)&1)<<2)
#define v_WIN0_ALPHA_VAL(x)	(((x)&0xff)<<4)
#define v_WIN1_ALPHA_VAL(x)	(((x)&0xff)<<12)
#define v_HWC_ALPAH_VAL(x)	(((x)&0x0f)<<20)

#define WIN0_COLOR_KEY		(0x18)
#define m_COLOR_KEY_VAL		(0xffffff<<0)
#define m_COLOR_KEY_EN		(1<<24)
#define v_COLOR_KEY_VAL(x)	(((x)&0xffffff)<<0)
#define v_COLOR_KEY_EN(x)	(((x)&1)<<24)

#define WIN1_COLOR_KEY		(0x1C)


#define WIN0_YRGB_MST0		(0x20)
#define WIN0_CBR_MST0		(0x24)
#define WIN0_YRGB_MST1		(0x28)
#define WIN0_CBR_MST1		(0x2C)
#define WIN_VIR			(0x30)
#define m_WIN0_VIR   		(0x1fff << 0)
#define m_WIN1_VIR   		(0x1fff << 16)
#define v_WIN0_VIR_VAL(x)       ((x)<<0)
#define v_WIN1_VIR_VAL(x)       ((x)<<16)
#define v_ARGB888_VIRWIDTH(x) 	(((x)&0x1fff)<<0)
#define v_RGB888_VIRWIDTH(x) 	(((((x*3)>>2)+((x)%3))&0x1fff)<<0)
#define v_RGB565_VIRWIDTH(x) 	 ((DIV_ROUND_UP(x,2)&0x1fff)<<0)
#define v_YUV_VIRWIDTH(x)    	 ((DIV_ROUND_UP(x,4)&0x1fff)<<0)
#define v_WIN1_ARGB888_VIRWIDTH(x) 	(((x)&0x1fff)<<16)
#define v_WIN1_RGB888_VIRWIDTH(x) 	(((((x*3)>>2)+((x)%3))&0x1fff)<<16)
#define v_WIN1_RGB565_VIRWIDTH(x) 	 ((DIV_ROUND_UP(x,2)&0x1fff)<<16)



#define WIN0_ACT_INFO		(0x34)
#define m_ACT_WIDTH       	(0x1fff<<0)
#define m_ACT_HEIGHT      	(0x1fff<<16)
#define v_ACT_WIDTH(x)       	(((x-1)&0x1fff)<<0)
#define v_ACT_HEIGHT(x)      	(((x-1)&0x1fff)<<16)

#define WIN0_DSP_INFO		(0x38)
#define v_DSP_WIDTH(x)     	(((x-1)&0x7ff)<<0)
#define v_DSP_HEIGHT(x)    	(((x-1)&0x7ff)<<16)

#define WIN0_DSP_ST		(0x3C)
#define v_DSP_STX(x)      	(((x)&0xfff)<<0)
#define v_DSP_STY(x)      	(((x)&0xfff)<<16)

#define WIN0_SCL_FACTOR_YRGB	(0x40)
#define v_X_SCL_FACTOR(x)  (((x)&0xffff)<<0)
#define v_Y_SCL_FACTOR(x)  (((x)&0xffff)<<16)

#define WIN0_SCL_FACTOR_CBR	(0x44)
#define WIN0_SCL_OFFSET		(0x48)
#define WIN1_MST		(0x4C)
#define WIN1_DSP_INFO		(0x50)
#define WIN1_DSP_ST		(0x54)
#define HWC_MST			(0x58)
#define HWC_DSP_ST		(0x5C)
#define HWC_COLOR_LUT0		(0x60)
#define HWC_COLOR_LUT1		(0x64)
#define HWC_COLOR_LUT2		(0x68)
#define DSP_HTOTAL_HS_END	(0x6C)
#define v_HSYNC(x)  		(((x)&0xfff)<<0)   //hsync pulse width
#define v_HORPRD(x) 		(((x)&0xfff)<<16)   //horizontal period

#define DSP_HACT_ST_END		(0x70)
#define v_HAEP(x) 		(((x)&0xfff)<<0)  //horizontal active end point
#define v_HASP(x) 		(((x)&0xfff)<<16) //horizontal active start point

#define DSP_VTOTAL_VS_END	(0x74)
#define v_VSYNC(x) 		(((x)&0xfff)<<0)
#define v_VERPRD(x) 		(((x)&0xfff)<<16)
#define DSP_VACT_ST_END		(0x78)
#define v_VAEP(x) 		(((x)&0xfff)<<0)
#define v_VASP(x) 		(((x)&0xfff)<<16)

#define DSP_VS_ST_END_F1	(0x7C)
#define DSP_VACT_ST_END_F1	(0x80)
#define REG_CFG_DONE		(0x90)
#define MCU_BYPASS_WPORT	(0x100)
#define MCU_BYPASS_RPORT	(0x200)
#define WIN1_LUT_ADDR		(0x400)
#define DSP_LUT_ADDR		(0x800)

/*
	RK3026/RK3028A max output  resolution 1920x1080
	support IEP instead of  3d
*/
//#ifdef CONFIG_ARCH_RK3026
//SYS_CTRL 0x00
#define m_DIRECT_PATCH_EN         (1<<11)
#define m_DIRECT_PATH_LAY_SEL     (1<<12)

#define v_DIRECT_PATCH_EN(x)      (((x)&1)<<11)
#define v_DIRECT_PATH_LAY_SEL(x)  (((x)&1)<<12)

//INT_STATUS 0x10
#define m_WIN0_EMPTY_INTR_EN      (1<<24)
#define m_WIN1_EMPTY_INTR_EN      (1<<25)
#define m_WIN0_EMPTY_INTR_CLR     (1<<26)
#define m_WIN1_EMPTY_INTR_CLR     (1<<27)
#define m_WIN0_EMPTY_INTR_STA     (1<<28)
#define m_WIN1_EMPTY_INTR_STA     (1<<29)

#define v_WIN0_EMPTY_INTR_EN(x)   (((x)&1)<<24)
#define v_WIN1_EMPTY_INTR_EN(x)   (((x)&1)<<25)
#define v_WIN0_EMPTY_INTR_CLR(x)  (((x)&1)<<26)
#define v_WIN1_EMPTY_INTR_CLR(x)  (((x)&1)<<27)
#define v_WIN0_EMPTY_INTR_STA(x)  (((x)&1)<<28)
#define v_WIN1_EMPTY_INTR_STA(x)  (((x)&1)<<29)
//#endif


#define CalScale(x, y)	             ((((u32)(x-1))*0x1000)/(y-1))

struct lcdc_device{
	int id;
	struct rk_lcdc_driver driver;
	struct device *dev;
	struct rk_screen *screen;

	void __iomem *regs;
	void *regsbak;			//back up reg
	u32 reg_phy_base;       	// physical basic address of lcdc register
	u32 len;               		// physical map length of lcdc register
	spinlock_t  reg_lock;		//one time only one process allowed to config the register
	
	int __iomem *dsp_lut_addr_base;

	int prop;			/*used for primary or extended display device*/
	bool pre_init;
	bool pwr18;			/*if lcdc use 1.8v power supply*/
	bool clk_on;			//if aclk or hclk is closed ,acess to register is not allowed
	u8 atv_layer_cnt;		//active layer counter,when  atv_layer_cnt = 0,disable lcdc
	

	unsigned int		irq;

	struct clk		*pd;				//lcdc power domain
	struct clk		*hclk;				//lcdc AHP clk
	struct clk		*dclk;				//lcdc dclk
	struct clk		*aclk;				//lcdc share memory frequency
	u32 pixclock;	

	u32 standby;						//1:standby,0:wrok
};



static inline void lcdc_writel(struct lcdc_device *lcdc_dev,u32 offset,u32 v)
{
	u32 *_pv = (u32*)lcdc_dev->regsbak;	
	_pv += (offset >> 2);	
	*_pv = v;
	writel_relaxed(v,lcdc_dev->regs+offset);	
}

static inline u32 lcdc_readl(struct lcdc_device *lcdc_dev,u32 offset)
{
	u32 v;
	u32 *_pv = (u32*)lcdc_dev->regsbak;
	_pv += (offset >> 2);
	v = readl_relaxed(lcdc_dev->regs+offset);
	*_pv = v;
	return v;
}

static inline u32 lcdc_read_bit(struct lcdc_device *lcdc_dev,u32 offset,u32 msk) 
{
       u32 _v = readl_relaxed(lcdc_dev->regs+offset); 
       _v &= msk;
       return (_v?1:0);   
}

static inline void  lcdc_set_bit(struct lcdc_device *lcdc_dev,u32 offset,u32 msk) 
{
	u32* _pv = (u32*)lcdc_dev->regsbak;	
	_pv += (offset >> 2);				
	(*_pv) |= msk;				
	writel_relaxed(*_pv,lcdc_dev->regs + offset); 
} 

static inline void lcdc_clr_bit(struct lcdc_device *lcdc_dev,u32 offset,u32 msk)
{
	u32* _pv = (u32*)lcdc_dev->regsbak;	
	_pv += (offset >> 2);				
	(*_pv) &= (~msk);				
	writel_relaxed(*_pv,lcdc_dev->regs + offset); 
} 

static inline void  lcdc_msk_reg(struct lcdc_device *lcdc_dev,u32 offset,u32 msk,u32 v)
{
	u32 *_pv = (u32*)lcdc_dev->regsbak;	
	_pv += (offset >> 2);			
	(*_pv) &= (~msk);				
	(*_pv) |= v;				
	writel_relaxed(*_pv,lcdc_dev->regs+offset);	
}

static inline void lcdc_cfg_done(struct lcdc_device *lcdc_dev) 
{
	writel_relaxed(0x01,lcdc_dev->regs+REG_CFG_DONE); 
	dsb();	
} 

#endif
