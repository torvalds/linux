/*
 * drivers/video/rk2818_fb.c 
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
 */
 
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/fb.h>
#include <linux/init.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/backlight.h>
#include <linux/timer.h>
#include <linux/time.h>
#include <linux/wait.h>
#include <linux/earlysuspend.h>
#include <linux/cpufreq.h>


#include <asm/io.h>
#include <asm/div64.h>
#include <asm/uaccess.h>

#include "rk2818_fb.h"

#ifdef CONFIG_PM
#include <linux/pm.h>
#endif

#include <mach/iomux.h>
#include <mach/gpio.h>
#include <mach/board.h>
#include <mach/rk2818_iomap.h>
//#include <asm/uaccess.h>

#include "./display/screen/screen.h"


#define WIN1_USE_DOUBLE_BUF     1       //win1 use double buf to accelerate display
#define CURSOR_BUF_SIZE         256     //RK2818 cursor need 256B buf

#if 0
	#define fbprintk(msg...)	printk(msg);
#else
	#define fbprintk(msg...)
#endif

 
#if 0
	#define fbprintk2(msg...)	printk(msg);
#else
	#define fbprintk2(msg...)
#endif

#define LcdReadBit(inf, addr, msk)      ((inf->regbak.addr=inf->preg->addr)&(msk))
#define LcdWrReg(inf, addr, val)        inf->preg->addr=inf->regbak.addr=(val)
#define LcdRdReg(inf, addr)             (inf->preg->addr)
#define LcdSetBit(inf, addr, msk)       inf->preg->addr=((inf->regbak.addr) |= (msk))
#define LcdClrBit(inf, addr, msk)       inf->preg->addr=((inf->regbak.addr) &= ~(msk))
#define LcdSetRegBit(inf, addr, msk)    inf->preg->addr=((inf->preg->addr) |= (msk))
#define LcdMskReg(inf, addr, msk, val)  (inf->regbak.addr)&=~(msk);   inf->preg->addr=(inf->regbak.addr|=(val))


#define IsMcuLandscape()                ((SCREEN_MCU==inf->cur_screen->type) && (0==inf->mcu_scandir))
#define IsMcuUseFmk()                   ( (2==inf->cur_screen->mcu_usefmk) || (1==inf->cur_screen->mcu_usefmk)) 

#define CalScaleW1(x, y)	            (u32)( ((u32)x*0x1000)/y)
#define CalScaleDownW0(x, y)	            (u32)( (x>=y) ? ( ((u32)x*0x1000)/y) : (0x1000) )
#define CalScaleUpW0(x, y)	            (u32)( (x<=y) ? ( ((u32)x*0x1000)/y) : (0x1000) )

struct rk28fb_rgb {
	struct fb_bitfield	red;
	struct fb_bitfield	green;
	struct fb_bitfield	blue;
	struct fb_bitfield	transp;
};

static struct rk28fb_rgb def_rgb_16 = {
     red:    { offset: 11, length: 5, },
     green:  { offset: 5,  length: 6, },
     blue:   { offset: 0,  length: 5, },
     transp: { offset: 0,  length: 0, },
};

struct win0_par {
	u32 refcount;
	u32	pseudo_pal[16];
	u32 y_offset;
	u32 uv_offset;

    u32 odd_y_offset;
	u32 odd_uv_offset;
    u32 odd_nxt_y_offset;
    u32 odd_nxt_uv_offset;
    u32 even_y_offset;
	u32 even_uv_offset;
    u32 even_nxt_y_offset;
    u32 even_nxt_uv_offset;
   
    u8 par_seted;
    u8 addr_seted;
};

struct win1_par {
	u32 refcount;
	u32	pseudo_pal[16];
	int lstblank;
};

struct rk2818fb_inf {
    struct fb_info *win0fb;
    struct fb_info *win1fb;

    void __iomem *reg_vir_base;  // virtual basic address of lcdc register
	u32 reg_phy_base;       // physical basic address of lcdc register
	u32 len;               // physical map length of lcdc register

    struct clk      *clk;
    struct clk      *dclk;            //lcdc dclk
    struct clk      *dclk_parent;     //lcdc dclk divider frequency source
    struct clk      *dclk_divider;    //lcdc demodulator divider frequency
    struct clk      *clk_share_mem;   //lcdc share memory frequency
    unsigned long	dclk_rate;

    /* lcdc reg base address and backup reg */
    LCDC_REG *preg;
    LCDC_REG regbak;

	int in_suspend;

    /* variable used in mcu panel */
	int mcu_needflush;
	int mcu_isrcnt;
	u16 mcu_scandir;
	struct timer_list mcutimer;
	int mcu_status;
	u8 mcu_fmksync;
	int mcu_usetimer;
	int mcu_stopflush;
	
    /* external memery */
	char __iomem *screen_base2;
    __u32 smem_len2;
    unsigned long  smem_start2;

    char __iomem *cursor_base;   /* cursor Virtual address*/
    __u32 cursor_size;           /* Amount of ioremapped VRAM or 0 */ 
    unsigned long  cursor_start;

    struct rk28fb_screen lcd_info;
    struct rk28fb_screen tv_info[5];
    struct rk28fb_screen hdmi_info[2];
    struct rk28fb_screen *cur_screen;
#ifdef CONFIG_CPU_FREQ
    struct notifier_block freq_transition;
#endif

};

typedef enum _TRSP_MODE
{
    TRSP_CLOSE = 0,
    TRSP_FMREG,
    TRSP_FMREGEX,
    TRSP_FMRAM,
    TRSP_FMRAMEX,
    TRSP_MASK,
    TRSP_INVAL
} TRSP_MODE;


struct platform_device *g_pdev = NULL;
static int win1fb_set_par(struct fb_info *info);

#if 0
#define CHK_SUSPEND(inf)	\
	if(inf->in_suspend)	{	\
		fbprintk(">>>>>> fb is in suspend! return! \n");	\
		return -EPERM;	\
	}
#else
#define CHK_SUSPEND(inf)
#endif

static DECLARE_WAIT_QUEUE_HEAD(wq);
static int wq_condition = 0;

void set_lcd_pin(struct platform_device *pdev, int enable)
{
    int ret =0;
	struct rk2818fb_info *mach_info = pdev->dev.platform_data;

	unsigned display_on = mach_info->disp_on_pin;
	unsigned lcd_standby = mach_info->standby_pin;
    
	int display_on_pol = mach_info->disp_on_value;
	int lcd_standby_pol = mach_info->standby_value;

	fbprintk(">>>>>> %s : %s \n", __FILE__, __FUNCTION__);
	fbprintk(">>>>>> display_on(%d) = %d \n", display_on, enable ? display_on_pol : !display_on_pol);
	fbprintk(">>>>>> lcd_standby(%d) = %d \n", lcd_standby, enable ? lcd_standby_pol : !lcd_standby_pol);

    // set display_on   

    if(display_on != INVALID_GPIO) 
        {
        gpio_direction_output(display_on, 0);
		gpio_set_value(display_on, enable ? display_on_pol : !display_on_pol);
    }
    if(lcd_standby != INVALID_GPIO) 
    {
        gpio_direction_output(lcd_standby, 0);
		gpio_set_value(lcd_standby, enable ? lcd_standby_pol : !lcd_standby_pol);
    }	
    
    return;
pin_err:    
    return;
}

int mcu_do_refresh(struct rk2818fb_inf *inf)
{
    if(inf->mcu_stopflush)  return 0;

    if(SCREEN_MCU!=inf->cur_screen->type)   return 0;

    // use frame mark
    if(IsMcuUseFmk()) 
    {      
        inf->mcu_needflush = 1;
        return 0;
    }

    // not use frame mark
    if(LcdReadBit(inf, MCU_TIMING_CTRL, m_MCU_HOLDMODE_SELECT)) 
    {
        if(!LcdReadBit(inf, MCU_TIMING_CTRL, m_MCU_HOLD_STATUS)) 
        {
            inf->mcu_needflush = 1;
        } 
        else 
        {
            if(inf->cur_screen->refresh)    inf->cur_screen->refresh(REFRESH_PRE);
            inf->mcu_needflush = 0;
            inf->mcu_isrcnt = 0;
            LcdSetRegBit(inf, MCU_TIMING_CTRL, m_MCU_HOLDMODE_FRAME_ST);           
        }
    }
    return 0;
}


void mcutimer_callback(unsigned long arg)
{
    struct rk2818fb_inf *inf = platform_get_drvdata(g_pdev);
    static int waitcnt = 0;

    mod_timer(&inf->mcutimer, jiffies + HZ/10);

    switch(inf->mcu_status)
    {
    case MS_IDLE:
        inf->mcu_status = MS_MCU;        
        break;
    case MS_MCU:
        if(inf->mcu_usetimer)   mcu_do_refresh(inf);
        break; 
    case MS_EWAITSTART:
        inf->mcu_status = MS_EWAITEND;
        waitcnt = 0;        
        break;
    case MS_EWAITEND:
        if(0==waitcnt) {
            mcu_do_refresh(inf);
        }
        if(waitcnt++>14) {
            inf->mcu_status = MS_EEND;
        }
        break;
    case MS_EEND:
        inf->mcu_status = MS_MCU;
        break;
    default:
        inf->mcu_status = MS_MCU;
        break;
    }
}

int mcu_refresh(struct rk2818fb_inf *inf)
{
    static int mcutimer_inited = 0;

    if(SCREEN_MCU!=inf->cur_screen->type)   return 0;

    if(!mcutimer_inited) 
    {
        mcutimer_inited = 1;
        init_timer(&inf->mcutimer);
        inf->mcutimer.function = mcutimer_callback;
        inf->mcutimer.expires = jiffies + HZ/5;
        inf->mcu_status = MS_IDLE;
        add_timer(&inf->mcutimer);
    }

    if(MS_MCU==inf->mcu_status)     mcu_do_refresh(inf);

    return 0;
}

int mcu_ioctl(unsigned int cmd, unsigned long arg)
{
    struct rk2818fb_inf *inf = NULL;
    if(!g_pdev)     return -1;

    inf = dev_get_drvdata(&g_pdev->dev);

    switch(cmd)
    {
    case MCU_WRCMD:
        LcdClrBit(inf, MCU_TIMING_CTRL, m_MCU_RS_SELECT); 
        LcdWrReg(inf, MCU_BYPASS_WPORT, arg);    
        LcdSetBit(inf, MCU_TIMING_CTRL, m_MCU_RS_SELECT);
        break;

    case MCU_WRDATA:
        LcdSetBit(inf, MCU_TIMING_CTRL, m_MCU_RS_SELECT);
        LcdWrReg(inf, MCU_BYPASS_WPORT, arg);
        break;

    case MCU_SETBYPASS:
        LcdMskReg(inf, MCU_TIMING_CTRL, m_MCU_BYPASSMODE_SELECT, v_MCU_BYPASSMODE_SELECT(arg));        
        LcdWrReg(inf, REG_CFG_DONE, 0x01);
        break;

    default:
        break;
    }

    return 0;
}

static irqreturn_t mcu_irqfmk(int irq, void *dev_id)
{
	struct platform_device *pdev = (struct platform_device*)dev_id;
    struct rk2818fb_inf *inf = platform_get_drvdata(pdev);
    struct rk28fb_screen *screen;

    if(!inf)    return IRQ_HANDLED;
    
    screen = inf->cur_screen;

    if(0==screen->mcu_usefmk) {       
        return IRQ_HANDLED;
    }
    
    if(inf->mcu_fmksync == 1)
        return IRQ_HANDLED;
    
    inf->mcu_fmksync = 1;
    if(inf->mcu_needflush)
    {  
        inf->mcu_needflush = 0;        
        inf->mcu_isrcnt = 0;
        if(inf->cur_screen->refresh)
           inf->cur_screen->refresh(REFRESH_PRE);         
        LcdSetBit(inf, MCU_TIMING_CTRL, m_MCU_HOLDMODE_FRAME_ST);        
    }
    inf->mcu_fmksync = 0;

	return IRQ_HANDLED;
}

int init_lcdc(struct fb_info *info)
{
    struct rk2818fb_inf *inf = dev_get_drvdata(info->device);
    u32 reg1=0, reg2=0, msk=0, clr=0;

	fbprintk(">>>>>> %s : %s \n", __FILE__, __FUNCTION__);

	// set AHB access rule and disable all windows
    LcdMskReg(inf, SYS_CONFIG,
        m_W1_ROLLER | m_W0_ROLLER | m_INTERIACE_EN | m_MPEG2_I2P_EN | m_W0_ROTATE |
        m_W1_ENABLE |m_W0_ENABLE | m_HWC_ENABLE | m_HWC_RELOAD_EN |m_W1_INTERLACE_READ |
        m_W0_INTERLACE_READ | m_STANDBY | m_W1_HWC_INCR|
        m_W1_HWC_BURST | m_W0_INCR | m_W0_BURST ,
        v_W1_ROLLER(0) | v_W0_ROLLER(0) | v_INTERIACE_EN(0) |
        v_MPEG2_I2P_EN(0) | v_W0_ROTATE(0) |v_W1_ENABLE(0) |
        v_W0_ENABLE(0) | v_HWC_ENABLE(0) | v_HWC_RELOAD_EN(0) | v_W1_INTERLACE_READ(0) | 
        v_W0_INTERLACE_READ(0) | v_STANDBY(0) | v_W1_HWC_INCR(31) | v_W1_HWC_BURST(1) |
        v_W0_INCR(31) | v_W0_BURST(1)
        ); // use ahb burst32

	// set all swap rule for every window and set water mark
    reg1 = v_W0_565_RB_SWAP(0) | v_W0_YRGB_M8_SWAP(0) |
           v_W0_YRGB_R_SHIFT_SWAP(0) | v_W0_CBR_R_SHIFT_SWAP(0) | v_W0_YRGB_16_SWAP(0) |
           v_W0_YRGB_8_SWAP(0) | v_W0_CBR_16_SWAP(0) | v_W0_CBR_8_SWAP(0) | v_W0_YRGB_HL8_SWAP(0);
           
    reg2 = v_W1_565_RB_SWAP(0) |  v_W1_16_SWAP(0) | v_W1_8_SWAP(0) |
           v_W1_R_SHIFT_SWAP(0) | v_OUTPUT_BG_SWAP(0) | v_OUTPUT_RB_SWAP(0) | v_OUTPUT_RG_SWAP(0) |
           v_DELTA_SWAP(0) | v_DUMMY_SWAP(0);
              
    LcdWrReg(inf, SWAP_CTRL, reg1 | reg2);
	
	// and mcu holdmode; and set win1 top.
    LcdMskReg(inf, MCU_TIMING_CTRL, m_MCU_HOLDMODE_SELECT | m_MCU_HOLDMODE_FRAME_ST | m_MCU_BYPASSMODE_SELECT ,
            v_MCU_HOLDMODE_SELECT(0)| v_MCU_HOLDMODE_FRAME_ST(0) |v_MCU_BYPASSMODE_SELECT(0));

    // disable blank out, black out, tristate out, yuv2rgb bypass
    LcdMskReg(inf, BLEND_CTRL, m_W1_BLEND_EN | m_W0_BLEND_EN | m_HWC_BLEND_EN | m_W1_BLEND_FACTOR_SELECT |
             m_W0_BLEND_FACTOR_SELECT | m_W0W1_OVERLAY | m_HWC_BLEND_FACTOR | m_W1_BLEND_FACTOR | m_W0_BLEND_FACTOR, 
             v_W1_BLEND_EN(0) | v_W0_BLEND_EN(0) | v_HWC_BLEND_EN(0) | v_W1_BLEND_FACTOR_SELECT(0) |
             v_W0_BLEND_FACTOR_SELECT(0) | v_W0W1_OVERLAY(0) | v_HWC_BLEND_FACTOR(0) | v_W1_BLEND_FACTOR(0) | v_W0_BLEND_FACTOR(0) 
             );
    
    LcdMskReg(inf, WIN0_COLOR_KEY_CTRL, m_COLORKEY_EN, v_COLORKEY_EN(0));
    LcdMskReg(inf, WIN1_COLOR_KEY_CTRL, m_COLORKEY_EN, v_COLORKEY_EN(0));
    
    LcdMskReg(inf, DSP_CTRL0, m_HSYNC_POLARITY | m_VSYNC_POLARITY | m_DEN_POLARITY | m_DCLK_POLARITY | 
        m_COLOR_SPACE_CONVERSION | m_DITHERING_EN | m_INTERLACE_FIELD_POLARITY | 
        m_YUV_CLIP_MODE | m_I2P_FILTER_EN , 
        v_HSYNC_POLARITY(0) | v_VSYNC_POLARITY(0) | v_DEN_POLARITY(0) | v_DCLK_POLARITY(0) | v_COLOR_SPACE_CONVERSION(0) | 
        v_DITHERING_EN(0) | v_INTERLACE_FIELD_POLARITY(0) | v_YUV_CLIP_MODE(0) | v_I2P_FILTER_EN(0));
    
    LcdMskReg(inf, DSP_CTRL1, m_BG_COLOR | m_BLANK_MODE | m_BLACK_MODE | m_W1_SD_DEFLICKER_EN | m_W1_SP_DEFLICKER_EN | 
            m_W0CR_SD_DEFLICKER_EN | m_W0CR_SP_DEFLICKER_EN | m_W0YRGB_SD_DEFLICKER_EN | m_W0YRGB_SP_DEFLICKER_EN, 
            v_BG_COLOR(0) | v_BLANK_MODE(0) | v_BLACK_MODE(0) | v_W1_SD_DEFLICKER_EN(0) | v_W1_SP_DEFLICKER_EN(0) | 
            v_W0CR_SD_DEFLICKER_EN(0) | v_W0CR_SP_DEFLICKER_EN(0) | v_W0YRGB_SD_DEFLICKER_EN(0) | v_W0YRGB_SP_DEFLICKER_EN(0));

    // initialize all interrupt
    clr = v_HOR_STARTCLEAR(1) | v_FRM_STARTCLEAR(1) | v_SCANNING_CLEAR(1);
        
    msk = v_HOR_STARTMASK(1) | v_FRM_STARTMASK(0) | v_SCANNING_MASK(1);

    LcdWrReg(inf, INT_STATUS, clr | msk);

	// let above to take effect
    LcdWrReg(inf, REG_CFG_DONE, 0x01);

    return 0;
}

void load_screen(struct fb_info *info, bool initscreen)
{
    int ret = -EINVAL;
    struct rk2818fb_inf *inf = dev_get_drvdata(info->device);
    struct rk28fb_screen *screen = inf->cur_screen;
    u16 face = screen->face;
    u16 mcu_total, mcu_rwstart, mcu_csstart, mcu_rwend, mcu_csend;
    u16 right_margin = screen->right_margin, lower_margin = screen->lower_margin;
    u16 x_res = screen->x_res, y_res = screen->y_res;
    u32 clk_rate = 0;
    u32 dclk_rate = 0;
        
	fbprintk(">>>>>> %s : %s \n", __FILE__, __FUNCTION__);

    // set the rgb or mcu
    LcdMskReg(inf, MCU_TIMING_CTRL, m_MCU_OUTPUT_SELECT, v_MCU_OUTPUT_SELECT((SCREEN_MCU==screen->type)?(1):(0)));

	// set out format and mcu timing
    mcu_total  = (screen->mcu_wrperiod*150*1000)/1000000;
    if(mcu_total>31)    mcu_total = 31;
    if(mcu_total<3)     mcu_total = 3;
    mcu_rwstart = (mcu_total+1)/4 - 1;
    mcu_rwend = ((mcu_total+1)*3)/4 - 1;
    mcu_csstart = (mcu_rwstart>2) ? (mcu_rwstart-3) : (0);
    mcu_csend = (mcu_rwend>15) ? (mcu_rwend-1) : (mcu_rwend);

    fbprintk(">> mcu_total=%d, mcu_rwstart=%d, mcu_csstart=%d, mcu_rwend=%d, mcu_csend=%d \n",
        mcu_total, mcu_rwstart, mcu_csstart, mcu_rwend, mcu_csend);

    LcdMskReg(inf, MCU_TIMING_CTRL,
             m_MCU_CS_ST | m_MCU_CS_END| m_MCU_RW_ST | m_MCU_RW_END |
             m_MCU_WRITE_PERIOD | m_MCU_HOLDMODE_SELECT | m_MCU_HOLDMODE_FRAME_ST,
            v_MCU_CS_ST(mcu_csstart) | v_MCU_CS_END(mcu_csend) | v_MCU_RW_ST(mcu_rwstart) |
            v_MCU_RW_END(mcu_rwend) |  v_MCU_WRITE_PERIOD(mcu_total) |
            v_MCU_HOLDMODE_SELECT((SCREEN_MCU==screen->type)?(1):(0)) | v_MCU_HOLDMODE_FRAME_ST(0)
           );

	// set synchronous pin polarity and data pin swap rule
     LcdMskReg(inf, DSP_CTRL0,
        m_DISPLAY_FORMAT | m_HSYNC_POLARITY | m_VSYNC_POLARITY | m_DEN_POLARITY |
        m_DCLK_POLARITY | m_COLOR_SPACE_CONVERSION,
        v_DISPLAY_FORMAT(face) | v_HSYNC_POLARITY(screen->pin_hsync) | v_VSYNC_POLARITY(screen->pin_vsync) |
        v_DEN_POLARITY(screen->pin_den) | v_DCLK_POLARITY(screen->pin_dclk) | v_COLOR_SPACE_CONVERSION(0)        
        );

     LcdMskReg(inf, DSP_CTRL1, m_BG_COLOR,  v_BG_COLOR(0x000000) );

     LcdMskReg(inf, SWAP_CTRL, m_OUTPUT_RB_SWAP | m_OUTPUT_RG_SWAP | m_DELTA_SWAP | m_DUMMY_SWAP,
            v_OUTPUT_RB_SWAP(screen->swap_rb) | v_OUTPUT_RG_SWAP(screen->swap_rg) | v_DELTA_SWAP(screen->swap_delta) | v_DUMMY_SWAP(screen->swap_dumy));

	// set horizontal & vertical out timing
	if(SCREEN_MCU==inf->cur_screen->type) 
    {
	    right_margin = x_res/6;
	}

    LcdMskReg(inf, DSP_HTOTAL_HS_END, m_BIT11LO | m_BIT11HI, v_BIT11LO(screen->hsync_len) | 
             v_BIT11HI(screen->hsync_len + screen->left_margin + x_res + right_margin));
    LcdMskReg(inf, DSP_HACT_ST_END, m_BIT11LO | m_BIT11HI, v_BIT11LO(screen->hsync_len + screen->left_margin + x_res) | 
             v_BIT11HI(screen->hsync_len + screen->left_margin));        

         
    LcdMskReg(inf, DSP_VTOTAL_VS_END, m_BIT11LO | m_BIT11HI, v_BIT11LO(screen->vsync_len) | 
              v_BIT11HI(screen->vsync_len + screen->upper_margin + y_res + lower_margin));
    LcdMskReg(inf, DSP_VACT_ST_END, m_BIT11LO | m_BIT11HI,  v_BIT11LO(screen->vsync_len + screen->upper_margin+y_res)| 
              v_BIT11HI(screen->vsync_len + screen->upper_margin));
  
    LcdMskReg(inf, DSP_VS_ST_END_F1, m_BIT11LO | m_BIT11HI, v_BIT11LO(0) | v_BIT11HI(0));  
    LcdMskReg(inf, DSP_VACT_ST_END_F1, m_BIT11LO | m_BIT11HI, v_BIT11LO(0) | v_BIT11HI(0));
  
	// let above to take effect
    LcdWrReg(inf, REG_CFG_DONE, 0x01);

    // set lcdc clk
    if(SCREEN_MCU==screen->type)    screen->pixclock = 150; //mcu fix to 150 MHz

    clk_set_parent(inf->dclk_divider, inf->dclk_parent);
    clk_set_parent(inf->dclk, inf->dclk_divider);

    dclk_rate = screen->pixclock * 1000000;
    
    fbprintk(">>>>>> set lcdc dclk need %d HZ, clk_parent = %d hz \n ", screen->pixclock, clk_rate);

    ret = clk_set_rate(inf->dclk_divider, dclk_rate);
    
    if(ret)
    {
        printk(KERN_ERR ">>>>>> set lcdc dclk_divider faild \n ");
    }    
  
    clk_enable(inf->dclk);
    clk_enable(inf->clk);
    clk_enable(inf->clk_share_mem);

    // init screen panel
    if(screen->init && initscreen)
    {
    	screen->init();
    }
}
#ifdef CONFIG_CPU_FREQ
/*
* CPU clock speed change handler. We need to adjust the LCD timing
* parameters when the CPU clock is adjusted by the power management
* subsystem.
*/
#define TO_INF(ptr,member) container_of(ptr,struct rk2818fb_inf,member)

static int
rk2818fb_freq_transition(struct notifier_block *nb, unsigned long val, void *data)
{
    struct rk2818fb_inf *inf = TO_INF(nb, freq_transition);
    struct rk28fb_screen *screen = inf->cur_screen;
    u32 dclk_rate = 0;

    switch (val)
    {
    case CPUFREQ_PRECHANGE:        
          break;
    case CPUFREQ_POSTCHANGE:
        {
         dclk_rate = screen->pixclock * 1000000;
     
         fbprintk(">>>>>> set lcdc dclk need %d HZ, clk_parent = %d hz \n ", screen->pixclock, dclk_rate);
     
         clk_set_rate(inf->dclk_divider, dclk_rate);
         break;
        }
    }
    return 0;
} 
#endif

static inline unsigned int chan_to_field(unsigned int chan,
					 struct fb_bitfield *bf)
{
	chan &= 0xffff;
	chan >>= 16 - bf->length;
	return chan << bf->offset;
}

static int fb_setcolreg(unsigned regno,
			       unsigned red, unsigned green, unsigned blue,
			       unsigned transp, struct fb_info *info)
{
	unsigned int val;
//	fbprintk(">>>>>> %s : %s \n", __FILE__, __FUNCTION__);

	switch (info->fix.visual) {
	case FB_VISUAL_TRUECOLOR:
		/* true-colour, use pseudo-palette */
		if (regno < 16) {
			u32 *pal = info->pseudo_palette;
			val  = chan_to_field(red,   &info->var.red);
			val |= chan_to_field(green, &info->var.green);
			val |= chan_to_field(blue,  &info->var.blue);
			pal[regno] = val;
		}
		break;
	default:
		return -1;	/* unknown type */
	}

	return 0;
}

int rk2818_set_cursor(struct fb_info *info, struct fb_cursor *cursor)
{
    struct rk2818fb_inf *inf = dev_get_drvdata(info->device);

    //fbprintk(">>>>>> %s : %s \n", __FILE__, __FUNCTION__);

    /* check not being asked to exceed capabilities */

    if (cursor->image.width > 32)
        return -EINVAL;

    if (cursor->image.height > 32)
        return -EINVAL;

    if (cursor->image.depth > 1)
        return -EINVAL;

    if (cursor->enable)
        LcdSetBit(inf, SYS_CONFIG, m_HWC_ENABLE);
    else
        LcdClrBit(inf, SYS_CONFIG, m_HWC_ENABLE);    

    /* set data */
    if (cursor->set & FB_CUR_SETPOS) 
    {
        unsigned int x = cursor->image.dx;
        unsigned int y = cursor->image.dy;

        if (x >= 0x800 || y >= 0x800 )
            return -EINVAL;
        LcdWrReg(inf, HWC_DSP_ST, v_BIT11LO(x)|v_BIT11HI(y));        
    }


    if (cursor->set & FB_CUR_SETCMAP) 
    {
        unsigned int bg_col = cursor->image.bg_color;
        unsigned int fg_col = cursor->image.fg_color;

        fbprintk("%s: update cmap (%08x,%08x)\n",
            __func__, bg_col, fg_col);

        LcdMskReg(inf, HWC_COLOR_LUT0, m_HWC_R|m_HWC_G|m_HWC_B, 
                  v_HWC_R(info->cmap.red[bg_col]>>8) | v_HWC_G(info->cmap.green[bg_col]>>8) | v_HWC_B(info->cmap.blue[bg_col]>>8));

        LcdMskReg(inf, HWC_COLOR_LUT2, m_HWC_R|m_HWC_G|m_HWC_B, 
                         v_HWC_R(info->cmap.red[fg_col]>>8) | v_HWC_G(info->cmap.green[fg_col]>>8) | v_HWC_B(info->cmap.blue[fg_col]>>8));
    }

    if ((cursor->set & FB_CUR_SETSIZE ||
        cursor->set & (FB_CUR_SETIMAGE | FB_CUR_SETSHAPE))
        && info->screen_base && info->fix.smem_start && info->fix.smem_len)
    {
        /* rk2818 cursor is a 2 bpp 32x32 bitmap this routine
         * clears it to transparent then combines the cursor
         * shape plane with the colour plane to set the
         * cursor */
        int x, y;
        const unsigned char *pcol = cursor->image.data;
        const unsigned char *pmsk = cursor->mask;
        void __iomem   *dst;
        unsigned long cursor_mem_start;
        unsigned char  dcol = 0;
        unsigned char  dmsk = 0;
        unsigned int   op;
    
        dst = info->screen_base + info->fix.smem_len - CURSOR_BUF_SIZE;
	    cursor_mem_start = info->fix.smem_start + info->fix.smem_len - CURSOR_BUF_SIZE;
        
        fbprintk("%s: setting shape (%d,%d)\n",
            __func__, cursor->image.width, cursor->image.height);

        memset(dst, 0, CURSOR_BUF_SIZE);

        for (y = 0; y < cursor->image.height; y++) 
        {
            for (x = 0; x < cursor->image.width; x++) 
            {
                if ((x % 8) == 0) {
                    dcol = *pcol++;
                    dmsk = *pmsk++;
                } else {
                    dcol >>= 1;
                    dmsk >>= 1;
                }

                if (dmsk & 1) {
                    op = (dcol & 1) ? 1 : 3;
                    op <<= ((x % 4) * 2);   
                    *(u8*)(dst+(x/4)) |= op;
                }               
            }
            dst += (32*2)/8;
        }
        LcdSetBit(inf, SYS_CONFIG,m_HWC_RELOAD_EN);
        LcdWrReg(inf, HWC_MST, cursor_mem_start);   
        // flush end when wq_condition=1 in mcu panel, but not in rgb panel
        if(SCREEN_MCU == inf->cur_screen->type) {
            wait_event_interruptible_timeout(wq, wq_condition, HZ/20);
            wq_condition = 0;
        } else {
            wq_condition = 0;
            wait_event_interruptible_timeout(wq, wq_condition, HZ/20);
        }
        LcdClrBit(inf, SYS_CONFIG, m_HWC_RELOAD_EN);
    }

    return 0;
}


static int win0fb_blank(int blank_mode, struct fb_info *info)
{
    struct rk2818fb_inf *inf = dev_get_drvdata(info->device);

    fbprintk(">>>>>> %s : %s \n", __FILE__, __FUNCTION__);

	CHK_SUSPEND(inf);

    switch(blank_mode)
    {
    case FB_BLANK_UNBLANK:
        LcdMskReg(inf, SYS_CONFIG, m_W0_ENABLE, v_W0_ENABLE(1));
        break;
    default:
        LcdMskReg(inf, SYS_CONFIG, m_W0_ENABLE, v_W0_ENABLE(0));
        break;
    }
    LcdWrReg(inf, REG_CFG_DONE, 0x01);

	mcu_refresh(inf);
    return 0;
}

static int win0fb_check_var(struct fb_var_screeninfo *var, struct fb_info *info)
{
    struct rk2818fb_inf *inf = dev_get_drvdata(info->device);
    struct rk28fb_screen *screen = inf->cur_screen;

    u32 ScaleYUpY=0x1000, ScaleYDnY=0x1000;   
    u16 xpos = (var->nonstd>>8) & 0xfff;   //offset in panel
    u16 ypos = (var->nonstd>>20) & 0xfff;
    u16 xsize = (var->grayscale>>8) & 0xfff;   //visiable size in panel
    u16 ysize = (var->grayscale>>20) & 0xfff;
    u16 xlcd = screen->x_res;        //size of panel
    u16 ylcd = screen->y_res;
    u16 yres = 0;
    
    if(inf->win0fb->var.rotate == 270) {
        xlcd = screen->y_res;
        ylcd = screen->x_res;
    }

    fbprintk(">>>>>> %s : %s\n", __FILE__, __FUNCTION__);

	CHK_SUSPEND(inf);

    if( 0==var->xres_virtual || 0==var->yres_virtual ||
        0==var->xres || 0==var->yres || var->xres<16 ||
        0==xsize || 0==ysize || xsize<16 ||
        ((16!=var->bits_per_pixel)&&(32!=var->bits_per_pixel)) )
    {
        printk(">>>>>> win0fb_check_var fail 1!!! \n");
		printk("0==%d || 0==%d || 0==%d || 0==%d || %d<16 \n ||0==%d || 0==%d || %d<16 ||((16!=%d)&&(32!=%d)) \n", 
				var->xres_virtual, var->yres_virtual, var->xres, var->yres, var->xres, xsize, ysize, xsize,
        		var->bits_per_pixel, var->bits_per_pixel);
        return -EINVAL;
    }

    if( (var->xoffset+var->xres)>var->xres_virtual ||
        (var->yoffset+var->yres)>var->yres_virtual ||
        (xpos+xsize)>xlcd || (ypos+ysize)>ylcd )
    {
        printk(">>>>>> win0fb_check_var fail 2!!! \n");
		printk("(%d+%d)>%d || (%d+%d)>%d || (%d+%d)>%d || (%d+%d)>%d \n ",
				var->xoffset, var->xres, var->xres_virtual, var->yoffset, var->yres, 
				var->yres_virtual, xpos, xsize, xlcd, ypos, ysize, ylcd);
        return -EINVAL;
    }   

    switch(var->nonstd&0x0f)
    {
    case 0: // rgb
        switch(var->bits_per_pixel)
        {
        case 16:    // rgb565
            var->xres_virtual = (var->xres_virtual + 0x1) & (~0x1);
            var->xres = (var->xres + 0x1) & (~0x1);
            var->xoffset = (var->xoffset) & (~0x1);
            break;
        default:    // rgb888
            var->bits_per_pixel = 32;
            break;
        }
        var->nonstd &= ~0xc0;  //not support I2P in this format
        break;
    case 1: // yuv422
        var->xres_virtual = (var->xres_virtual + 0x3) & (~0x3);
        var->xres = (var->xres + 0x3) & (~0x3);
        var->xoffset = (var->xoffset) & (~0x3);
        break;
    case 2: // yuv4200
        var->xres_virtual = (var->xres_virtual + 0x3) & (~0x3);
        var->yres_virtual = (var->yres_virtual + 0x1) & (~0x1);
        var->xres = (var->xres + 0x3) & (~0x3);
        var->yres = (var->yres + 0x1) & (~0x1);
        var->xoffset = (var->xoffset) & (~0x3);
        var->yoffset = (var->yoffset) & (~0x1);
        break;
    case 3: // yuv4201
        var->xres_virtual = (var->xres_virtual + 0x3) & (~0x3);
        var->yres_virtual = (var->yres_virtual + 0x1) & (~0x1);
        var->xres = (var->xres + 0x3) & (~0x3);
        var->yres = (var->yres + 0x1) & (~0x1);
        var->xoffset = (var->xoffset) & (~0x3);
        var->yoffset = (var->yoffset) & (~0x1);
        var->nonstd &= ~0xc0;   //not support I2P in this format
        break;
    case 4: // yuv420m
        var->xres_virtual = (var->xres_virtual + 0x7) & (~0x7);
        var->yres_virtual = (var->yres_virtual + 0x1) & (~0x1);
        var->xres = (var->xres + 0x7) & (~0x7);
        var->yres = (var->yres + 0x1) & (~0x1);
        var->xoffset = (var->xoffset) & (~0x7);
        var->yoffset = (var->yoffset) & (~0x1);
        var->nonstd &= ~0xc0;   //not support I2P in this format
        break;
    case 5: // yuv444
        var->xres_virtual = (var->xres_virtual + 0x3) & (~0x3);
        var->xres = (var->xres + 0x3) & (~0x3);
        var->xoffset = (var->xoffset) & (~0x3);
        var->nonstd &= ~0xc0;   //not support I2P in this format
        break;
    default:
        printk(">>>>>> win0fb var->nonstd=%d is invalid! \n", var->nonstd);
        return -EINVAL;
    }

    if(var->rotate == 270)
    {
        yres = var->xres;
    }
    else
    {
        yres = var->yres;
    }
    ScaleYDnY = CalScaleDownW0(yres, ysize);
    ScaleYUpY = CalScaleUpW0(yres, ysize);

    if((ScaleYDnY>0x8000) || (ScaleYUpY<0x200))
    {
        return -EINVAL;        // multiple of scale down or scale up can't exceed 8
    }    
    
    return 0;
}

static int win0fb_set_par(struct fb_info *info)
{
    struct rk2818fb_inf *inf = dev_get_drvdata(info->device);
    struct rk28fb_screen *screen = inf->cur_screen;
    struct fb_var_screeninfo *var = &info->var;
    struct fb_fix_screeninfo *fix = &info->fix;
    struct win0_par *par = info->par;

    u8 format = 0;
    dma_addr_t map_dma;
    u32 cblen=0, crlen=0, map_size=0, smem_len=0, i2p_len=0;
    u32 pre_y_addr = 0, pre_uv_addr = 0, nxt_y_addr = 0, nxt_uv_addr = 0;

    u32 actWidth = 0; 
    u32 actHeight = 0;     

	u32 xact = var->xres;			    /* visible resolution		*/
	u32 yact = var->yres;
	u32 xvir = var->xres_virtual;		/* virtual resolution		*/
	u32 yvir = var->yres_virtual;
	u32 xact_st = var->xoffset;			/* offset from virtual to visible */
	u32 yact_st = var->yoffset;			/* resolution			*/

    u16 xpos = (var->nonstd>>8) & 0xfff;      //visiable pos in panel
    u16 ypos = (var->nonstd>>20) & 0xfff;
    u16 xsize = (var->grayscale>>8) & 0xfff;  //visiable size in panel
    u16 ysize = (var->grayscale>>20) & 0xfff;

    u32 ScaleYUpX=0x1000, ScaleYDnX=0x1000, ScaleYUpY=0x1000, ScaleYDnY=0x1000;
    u32 ScaleCbrUpX=0x1000, ScaleCbrDnX=0x1000, ScaleCbrUpY=0x1000, ScaleCbrDnY=0x1000;

    u8 i2p_mode = (var->nonstd & 0x80)>>7;
    u8 i2p_polarity = (var->nonstd & 0x40)>>6;
    u8 data_format = var->nonstd&0x0f;
    u32 win0_en = var->reserved[2];
    u32 y_addr = var->reserved[3];       //user alloc buf addr y
    u32 uv_addr = var->reserved[4];    

     if(var->rotate == 270)
     {
          xpos = (var->nonstd>>20) & 0xfff;      //visiable pos in panel
          ypos = (var->nonstd>>8) & 0xfff;
          xsize = (var->grayscale>>20) & 0xfff;  //visiable size in panel
           ysize = (var->grayscale>>8) & 0xfff;
     }
    fbprintk(">>>>>> %s : %s\n", __FILE__, __FUNCTION__);
    
	CHK_SUSPEND(inf);

	/* calculate y_offset,uv_offset,line_length,cblen and crlen  */
    switch (data_format)
    {
    case 0: // rgb
        switch(var->bits_per_pixel)
        {
        case 16:    // rgb565
            format = 1;
            fix->line_length = 2 * xvir;
            par->y_offset = (yact_st*xvir + xact_st)*2;
            break;
        case 32:    // rgb888
            format = 0;
            fix->line_length = 4 * xvir;
            par->y_offset = (yact_st*xvir + xact_st)*4;
            break;
        default:
            return -EINVAL;
        }
        break;
    case 1: // yuv422
        format = 2;
        fix->line_length = xvir;         
        cblen = crlen = (xvir*yvir)/2;
        
        if(i2p_mode)
        {
            i2p_len = (xvir*yvir)*2;                 
            if(var->rotate==0) //even
            {                    
                par->even_y_offset = (yact_st*xvir+xact_st) + xvir;
                par->even_uv_offset = (yact_st*xvir+xact_st) + xvir;                                    
                par->even_nxt_y_offset = (yact_st*xvir+xact_st); 
                par->even_nxt_uv_offset = (yact_st*xvir+xact_st);
                                                    
                par->odd_y_offset = (yact_st*xvir+xact_st);
                par->odd_uv_offset = (yact_st*xvir+xact_st);                
                par->odd_nxt_y_offset = (yact_st*xvir+xact_st) + xvir; 
                par->odd_nxt_uv_offset = (yact_st*xvir+xact_st) + xvir; 
            }
            else if(var->rotate==270)  //even
            {        
                 par->even_y_offset = (yact_st*xvir+xact_st) + xvir*(yact-1);
                 par->even_uv_offset = (yact_st*xvir+xact_st) + xvir*(yact-1);                                     
                 par->even_nxt_y_offset = (yact_st*xvir+xact_st) + xvir*(yact-2); 
                 par->even_nxt_uv_offset = (yact_st*xvir+xact_st) + xvir*(yact-2); 
                   
                 par->odd_y_offset = (yact_st*xvir+xact_st) + xvir*(yact-2);
                 par->odd_uv_offset = (yact_st*xvir+xact_st) + xvir*(yact-2);                                    
                 par->odd_nxt_y_offset = (yact_st*xvir+xact_st) + xvir*(yact-1);
                 par->odd_nxt_uv_offset = (yact_st*xvir+xact_st) + xvir*(yact-1); 
            }               
        }
        else
        {
            par->y_offset = yact_st*xvir + xact_st;
            par->uv_offset = yact_st*xvir + xact_st;
            if(var->rotate == 270)
            {
                par->y_offset += xvir*(yact- 1);
                par->uv_offset += xvir*(yact - 1);
            }
        }
        break;
    case 2: // yuv4200
        format = 3;
        fix->line_length = xvir;        
        cblen = crlen = (xvir*yvir)/4;
        if(i2p_mode)
        {
            i2p_len = (xvir*yvir)*3/2;
                      
            if(var->rotate==0) //even
            {                    
                par->even_y_offset = (yact_st*xvir+xact_st) + xvir;
                par->even_uv_offset = (yact_st/2*xvir+xact_st) + xvir;                                  
                par->even_nxt_y_offset = (yact_st*xvir+xact_st); 
                par->even_nxt_uv_offset = (yact_st/2*xvir+xact_st);
                                                    
                par->odd_y_offset = (yact_st*xvir+xact_st);
                par->odd_uv_offset = (yact_st/2*xvir+xact_st);               
                par->odd_nxt_y_offset = (yact_st*xvir+xact_st) + xvir; 
                par->odd_nxt_uv_offset = (yact_st/2*xvir+xact_st) + xvir; 
            }
            else if(var->rotate==270)  //even
            {        
                 par->even_y_offset = (yact_st*xvir+xact_st) + xvir*(yact-1);
                 par->even_uv_offset = (yact_st/2*xvir+xact_st) + xvir*(yact/2-1);                                    
                 par->even_nxt_y_offset = (yact_st*xvir+xact_st) + xvir*(yact-2); 
                 par->even_nxt_uv_offset = (yact_st/2*xvir+xact_st) + xvir*(yact/2-2); 
                   
                 par->odd_y_offset = (yact_st*xvir+xact_st) + xvir*(yact-2);
                 par->odd_uv_offset = (yact_st/2*xvir+xact_st) + xvir*(yact/2-2);                                   
                 par->odd_nxt_y_offset = (yact_st*xvir+xact_st) + xvir*(yact-1);
                 par->odd_nxt_uv_offset = (yact_st/2*xvir+xact_st) + xvir*(yact/2-1); 
            }               
        }
        else
        {
            par->y_offset = yact_st*xvir + xact_st;
            par->uv_offset = (yact_st/2)*xvir + xact_st;
            if(var->rotate == 270)
            {
                par->y_offset += xvir*(yact - 1);
                par->uv_offset += xvir*(yact/2 - 1);
            }
        }
        break;
    case 3: // yuv4201
        format = 4;
        fix->line_length = xvir;
        par->y_offset = (yact_st/2)*2*xvir + (xact_st)*2;
        par->uv_offset = (yact_st/2)*xvir + xact_st;
        if(var->rotate == 270)
        {
            par->y_offset += xvir*2*(yact/2 - 1);
            par->uv_offset += xvir*(yact/2 - 1);
        }
        cblen = crlen = (xvir*yvir)/4;
        break;
    case 4: // yuv420m
        format = 5;
        fix->line_length = xvir;
        par->y_offset = (yact_st/2)*3*xvir + (xact_st)*3;
        cblen = crlen = (xvir*yvir)/4;
        break;
    case 5: // yuv444
        format = 6;
        fix->line_length = xvir;
        par->y_offset = yact_st*xvir + xact_st;
        par->uv_offset = yact_st*2*xvir + xact_st*2;
        cblen = crlen = (xvir*yvir);
        break;
    default:
        return -EINVAL;
    }

    smem_len = fix->line_length * yvir + cblen + crlen + i2p_len;
    map_size = PAGE_ALIGN(smem_len);

    if(y_addr && uv_addr )  // buffer alloced by user, it's default instance
    {
        if (info->screen_base) {
            printk(">>>>>> win0fb unmap memory(%d)! \n", info->fix.smem_len);
            dma_free_writecombine(NULL, PAGE_ALIGN(info->fix.smem_len),info->screen_base, info->fix.smem_start);
	        info->screen_base = 0;
        }
        fix->smem_start = y_addr;
        fix->smem_len = smem_len;
        fix->mmio_start = uv_addr;

        par->addr_seted = ((-1==(int)y_addr)&&(-1==(int)uv_addr)) ? 0 : 1;
        fbprintk("buffer alloced by user fix->smem_start = %8x, fix->smem_len = %8x, fix->mmio_start = %8x \n", (u32)fix->smem_start, (u32)fix->smem_len, (u32)fix->mmio_start);
    }
    else    // driver alloce buffer
    {
        if ( (smem_len != fix->smem_len) || !info->screen_base )     // buffer need realloc
        {
            fbprintk(">>>>>> win0 buffer size is change! remap memory!\n");
            fbprintk(">>>>>> smem_len %d = %d * %d + %d + %d + %d\n", smem_len, fix->line_length, yvir, cblen, crlen, i2p_len);
            fbprintk(">>>>>> map_size = %d\n", map_size);
            LcdMskReg(inf, SYS_CONFIG, m_W0_ENABLE, v_W0_ENABLE(0));
            LcdWrReg(inf, REG_CFG_DONE, 0x01);
            msleep(50);
            if (info->screen_base) {
                   fbprintk(">>>>>> win0fb unmap memory(%d)! \n", info->fix.smem_len);
	            dma_free_writecombine(NULL, PAGE_ALIGN(info->fix.smem_len),info->screen_base, info->fix.smem_start);
	            info->screen_base = 0;
	            fix->smem_start = 0;
	            fix->smem_len = 0;
                fix->reserved[1] = 0;
                fix->reserved[2] = 0;
    	    }

    	    info->screen_base = dma_alloc_writecombine(NULL, map_size, &map_dma, GFP_KERNEL);
            if(!info->screen_base) {
                printk(">>>>>> win0fb dma_alloc_writecombine fail!\n");
                return -ENOMEM;
            }
            memset(info->screen_base, 0x00, map_size);
            fix->smem_start = map_dma;
            fix->smem_len = smem_len;
            fix->mmio_start = fix->smem_start + fix->line_length * yvir;
            if(i2p_len)
            {
                fix->reserved[1] = fix->mmio_start + cblen + crlen;       //next frame buf Y address in i2p mode               
                fix->reserved[2] = fix->reserved[1] + fix->line_length * yvir;  //next frame buf UV address in i2p mode  
            }   
            else
            {
                fix->reserved[1] = fix->reserved[2] = 0;
            }
            fbprintk(">>>>>> alloc succ, smem_start=%08x, smem_len=%d, mmio_start=%08x!\n",
                (u32)fix->smem_start, fix->smem_len, (u32)fix->mmio_start);
        }
    }

	// calculate the display phy address          
    y_addr = fix->smem_start + par->y_offset;
    uv_addr = fix->mmio_start + par->uv_offset; 
    
    fbprintk("y_addr 0x%08x = 0x%08x + %d\n", y_addr, (u32)fix->smem_start, par->y_offset);
    fbprintk("uv_addr 0x%08x = 0x%08x + %d\n", uv_addr, (u32)fix->mmio_start , par->uv_offset);

    if(i2p_mode && fix->reserved[1] && fix->reserved[2])
    {
        if(i2p_polarity)
        {
            y_addr = fix->smem_start + par->even_y_offset;
            uv_addr = fix->mmio_start + par->even_uv_offset; 
            pre_y_addr = fix->smem_start + par->even_nxt_y_offset;
            pre_uv_addr = fix->mmio_start + par->even_nxt_uv_offset; 
            nxt_y_addr = fix->reserved[1] + par->even_nxt_y_offset;
            nxt_uv_addr = fix->reserved[2] + par->even_nxt_uv_offset; 
        }
        else
        {
            y_addr = fix->smem_start + par->odd_y_offset;
            uv_addr = fix->mmio_start + par->odd_uv_offset; 
            pre_y_addr = fix->smem_start + par->odd_nxt_y_offset;
            pre_uv_addr = fix->mmio_start + par->odd_nxt_uv_offset; 
            nxt_y_addr = fix->reserved[1] + par->odd_nxt_y_offset;
            nxt_uv_addr = fix->reserved[2] + par->odd_nxt_uv_offset; 
        }
        
        fbprintk("pre_y_addr 0x%08x = 0x%08x + %d\n", pre_y_addr, (u32)fix->smem_start, par->even_nxt_y_offset);
        fbprintk("pre_uv_addr 0x%08x = 0x%08x + %d\n", pre_uv_addr, (u32)fix->mmio_start , par->even_nxt_uv_offset);
        fbprintk("nxt_y_addr 0x%08x = 0x%08x + %d\n", nxt_y_addr, (u32)fix->reserved[1], par->even_nxt_y_offset);
        fbprintk("nxt_uv_addr 0x%08x = 0x%08x + %d\n", nxt_uv_addr, (u32)fix->reserved[2] , par->even_nxt_uv_offset);
    }
  
    if(var->rotate == 270)
    {      
        actWidth = yact;
        actHeight = xact;
    }
    else
    {
        actWidth = xact;
        actHeight = yact; 
    }
    if((xact>1280) && (xsize>1280))
    {
        ScaleYDnX = CalScaleDownW0(actWidth, 1280);
        ScaleYUpX = CalScaleUpW0(1280, xsize);
    }
    else
    {
        ScaleYDnX = CalScaleDownW0(actWidth, xsize);
        ScaleYUpX = CalScaleUpW0(actWidth, xsize);
    }

    ScaleYDnY = CalScaleDownW0(actHeight, ysize);
    ScaleYUpY = CalScaleUpW0(actHeight, ysize);

    switch (data_format)
    {       
       case 1:// yuv422
           if((xact>1280) && (xsize>1280))
           {
               ScaleCbrDnX= CalScaleDownW0((actWidth/2), 1280);   
               ScaleCbrUpX = CalScaleUpW0((640), xsize); 
           }
           else             
           {
               if(var->rotate == 270) 
               {
                   ScaleCbrDnX= CalScaleDownW0(actWidth, xsize);   
                   ScaleCbrUpX = CalScaleUpW0(actWidth, xsize); 
               }
               else
               {
                   ScaleCbrDnX= CalScaleDownW0((actWidth/2), xsize);   
                   ScaleCbrUpX = CalScaleUpW0((actWidth/2), xsize);   
               }
           }        
           
           ScaleCbrDnY =  CalScaleDownW0(actHeight, ysize);  
           ScaleCbrUpY =  CalScaleUpW0(actHeight, ysize); 
           break;
       case 2: // yuv4200
       case 3: // yuv4201
       case 4: // yuv420m                   
           if((xact>1280) && (xsize>1280))
           {
               ScaleCbrDnX= CalScaleDownW0(actWidth/2, 1280);   
               ScaleCbrUpX = CalScaleUpW0(640, xsize); 
           }
           else
           {
               ScaleCbrDnX= CalScaleDownW0(actWidth/2, xsize);   
               ScaleCbrUpX = CalScaleUpW0(actWidth/2, xsize); 
           }
           
           ScaleCbrDnY =  CalScaleDownW0(actHeight/2, ysize);  
           ScaleCbrUpY =  CalScaleUpW0(actHeight/2, ysize);  
           break;
       case 5:// yuv444
           if((xact>1280) && (xsize>1280))
           {
               ScaleCbrDnX= CalScaleDownW0(actWidth, 1280);   
               ScaleCbrUpX = CalScaleUpW0(1280, xsize);   
           }
           else
           {
               ScaleCbrDnX= CalScaleDownW0(actWidth, xsize);   
               ScaleCbrUpX = CalScaleUpW0(actWidth, xsize); 
           }
           ScaleCbrDnY =  CalScaleDownW0(actHeight, ysize);  
           ScaleCbrUpY =  CalScaleUpW0(actHeight, ysize);    
           break;
    }
        
    xpos += (screen->left_margin + screen->hsync_len);
    ypos += (screen->upper_margin + screen->vsync_len);

    LcdWrReg(inf, WIN0_YRGB_MST, y_addr);
    LcdWrReg(inf, WIN0_CBR_MST, uv_addr);
    LcdWrReg(inf, WIN0_YRGB_VIR_MST, fix->smem_start);
    LcdWrReg(inf, WIN0_CBR_VIR_MST, fix->mmio_start);

    LcdMskReg(inf, SYS_CONFIG, m_W0_ENABLE | m_W0_FORMAT | m_W0_ROTATE | m_MPEG2_I2P_EN,
        v_W0_ENABLE(win0_en) | v_W0_FORMAT(format) | v_W0_ROTATE(var->rotate==270) | v_MPEG2_I2P_EN(i2p_mode));
    
    LcdMskReg(inf, WIN0_VIR, m_WORDLO | m_WORDHI, v_VIRWIDTH(xvir) | v_VIRHEIGHT((yvir)) );
    LcdMskReg(inf, WIN0_ACT_INFO, m_WORDLO | m_WORDHI, v_WORDLO(actWidth) | v_WORDHI(actHeight));
    LcdMskReg(inf, WIN0_DSP_ST, m_BIT11LO | m_BIT11HI, v_BIT11LO(xpos) | v_BIT11HI(ypos));
    LcdMskReg(inf, WIN0_DSP_INFO, m_BIT11LO | m_BIT11HI,  v_BIT11LO(xsize) | v_BIT11HI(ysize));
    LcdMskReg(inf, WIN0_SD_FACTOR_Y, m_WORDLO | m_WORDHI, v_WORDLO(ScaleYDnX) | v_WORDHI(ScaleYDnY));
    LcdMskReg(inf, WIN0_SP_FACTOR_Y, m_WORDLO | m_WORDHI, v_WORDLO(ScaleYUpX) | v_WORDHI(ScaleYUpY)); 
    LcdMskReg(inf, WIN0_SD_FACTOR_CBR, m_WORDLO | m_WORDHI, v_WORDLO(ScaleCbrDnX) | v_WORDHI(ScaleCbrDnY));
    LcdMskReg(inf, WIN0_SP_FACTOR_CBR, m_WORDLO | m_WORDHI, v_WORDLO(ScaleCbrUpX) | v_WORDHI(ScaleCbrUpY));    
 
    LcdMskReg(inf, DSP_CTRL0, m_I2P_THRESHOLD_Y | m_I2P_THRESHOLD_CBR | m_I2P_CUR_POLARITY | m_DROP_LINE_W0,
                         v_I2P_THRESHOLD_Y(0) | v_I2P_THRESHOLD_CBR(0)| v_I2P_CUR_POLARITY(i2p_polarity) | v_DROP_LINE_W0(0));

    LcdWrReg(inf, I2P_REF0_MST_Y, pre_y_addr);
    LcdWrReg(inf, I2P_REF0_MST_CBR, pre_uv_addr);
    LcdWrReg(inf, I2P_REF1_MST_Y, nxt_y_addr);
    LcdWrReg(inf, I2P_REF1_MST_CBR, nxt_uv_addr);
        
    switch(format)
    {   
    case 1:  //rgb565
        LcdMskReg(inf, SWAP_CTRL, m_W0_YRGB_8_SWAP | m_W0_YRGB_16_SWAP | m_W0_YRGB_R_SHIFT_SWAP | m_W0_565_RB_SWAP | m_W0_YRGB_M8_SWAP | m_W0_CBR_8_SWAP  | m_W0_YRGB_HL8_SWAP,
            v_W0_YRGB_8_SWAP(0) | v_W0_YRGB_16_SWAP(0) | v_W0_YRGB_R_SHIFT_SWAP(0) | v_W0_565_RB_SWAP(1) | v_W0_YRGB_M8_SWAP(0) | v_W0_CBR_8_SWAP(0) | v_W0_YRGB_HL8_SWAP(0) );
        break;   
    case 4:   //yuv4201
        LcdMskReg(inf, SWAP_CTRL, m_W0_YRGB_8_SWAP | m_W0_YRGB_16_SWAP | m_W0_YRGB_R_SHIFT_SWAP | m_W0_565_RB_SWAP | m_W0_YRGB_M8_SWAP | m_W0_CBR_8_SWAP | m_W0_YRGB_HL8_SWAP,
            v_W0_YRGB_8_SWAP(0) | v_W0_YRGB_16_SWAP(0) | v_W0_YRGB_R_SHIFT_SWAP(0) | v_W0_565_RB_SWAP(0) | 
            v_W0_YRGB_M8_SWAP((var->rotate==0)) | v_W0_CBR_8_SWAP(0) | v_W0_YRGB_HL8_SWAP(var->rotate!=0));
        break;
    default:
        LcdMskReg(inf, SWAP_CTRL, m_W0_YRGB_8_SWAP | m_W0_YRGB_16_SWAP | m_W0_YRGB_R_SHIFT_SWAP | m_W0_565_RB_SWAP | m_W0_YRGB_M8_SWAP | m_W0_CBR_8_SWAP | m_W0_YRGB_HL8_SWAP,
            v_W0_YRGB_8_SWAP(0) | v_W0_YRGB_16_SWAP(0) | v_W0_YRGB_R_SHIFT_SWAP(0) | v_W0_565_RB_SWAP(0) | v_W0_YRGB_M8_SWAP(0) | v_W0_CBR_8_SWAP(0)| v_W0_YRGB_HL8_SWAP(0) );
    }

    LcdWrReg(inf, REG_CFG_DONE, 0x01);

    return 0;
}

static int win0fb_pan_display(struct fb_var_screeninfo *var, struct fb_info *info)
{
    struct rk2818fb_inf *inf = dev_get_drvdata(info->device);
    struct fb_var_screeninfo *var0 = &info->var;
    struct fb_fix_screeninfo *fix0 = &info->fix;
    struct win0_par *par = info->par;
    u32 y_offset=0, uv_offset=0, y_addr=0, uv_addr=0;

    fbprintk(">>>>>> %s : %s\n", __FILE__, __FUNCTION__);

	CHK_SUSPEND(inf);
#if 0
    switch(var0->nonstd&0x0f)
    {
    case 0: // rgb
        switch(var0->bits_per_pixel)
        {
        case 16:    // rgb565
            var->xoffset = (var->xoffset) & (~0x1);
            y_offset = (var->yoffset*var0->xres_virtual + var->xoffset)*2;
            break;
        default:    // rgb888
            y_offset = (var->yoffset*var0->xres_virtual + var->xoffset)*4;
            break;
        }
        break;
    case 1: // yuv422
        var->xoffset = (var->xoffset) & (~0x3);
        y_offset = var->yoffset*var0->xres_virtual + var->xoffset;
        uv_offset = var->yoffset*var0->xres_virtual + var->xoffset;
        break;
    case 2: // yuv4200
        var->xoffset = (var->xoffset) & (~0x3);
        var->yoffset = (var->yoffset) & (~0x1);
        y_offset = var->yoffset*var0->xres_virtual + var->xoffset;
        uv_offset = (var->yoffset/2)*var0->xres_virtual + var->xoffset;
        break;
    case 3: // yuv4201
        var->xoffset = (var->xoffset) & (~0x3);
        var->yoffset = (var->yoffset) & (~0x1);
        y_offset = (var->yoffset/2)*2*var0->xres_virtual + (var->xoffset)*2;
        uv_offset = (var->yoffset/2)*var0->xres_virtual + var->xoffset;
        break;
    case 4: // yuv420m
        var->xoffset = (var->xoffset) & (~0x7);
        var->yoffset = (var->yoffset) & (~0x1);
        y_offset = (var->yoffset/2)*3*var0->xres_virtual + (var->xoffset)*3;
        break;
    case 5: // yuv444
        var->xoffset = (var->xoffset) & (~0x3);
        y_offset = var->yoffset*var0->xres_virtual + var->xoffset;
        uv_offset = var->yoffset*2*var0->xres_virtual + var->xoffset*2;
        break;
    default:
        return -EINVAL;
    }

    par->y_offset = y_offset;
    par->uv_offset = uv_offset;
#endif
    y_addr = fix0->smem_start +  par->y_offset;//y_offset;
    uv_addr = fix0->mmio_start + par->uv_offset ;//uv_offset;

    LcdWrReg(inf, WIN0_YRGB_MST, y_addr);
    LcdWrReg(inf, WIN0_CBR_MST, uv_addr);
    LcdWrReg(inf, REG_CFG_DONE, 0x01);

     // enable win0 after the win0 addr is seted
    par->par_seted = 1;
	LcdMskReg(inf, SYS_CONFIG, m_W0_ENABLE, v_W0_ENABLE((1==par->addr_seted)?(1):(0)));
	mcu_refresh(inf);

    return 0;
}

/* Set rotation (0, 90, 180, 270 degree), and switch to the new mode. */
int win0fb_rotate(struct fb_info *fbi, int rotate)
{
    struct fb_var_screeninfo *var = &fbi->var;
  //  u32 SrcFmt = var->nonstd&0x0f;

    fbprintk(">>>>>> %s : %s \n", __FILE__, __FUNCTION__);
    
    if(rotate == 0)
    {
        if(var->rotate)
        {
           var->rotate = 0; 
     //      if(!win0fb_check_var(var, fbi))
     //         win0fb_set_par(fbi);
        }        
    }
    else
    {
      //  if((var->xres >1280) || (var->yres >720)||((SrcFmt!= 1) && (SrcFmt!= 2) && (SrcFmt!= 3)))
      //  {
      //      printk(">>>>>> %s par err SrcFmt = %d\n", __FUNCTION__ ,SrcFmt);
      //      return -EPERM;
     //   }  
        if(var->rotate != 270)
        {
            var->rotate = 270;
      //      if(!win0fb_check_var(var, fbi))
      //         win0fb_set_par(fbi);
        }
    }
    
    return 0;    
}
int win0fb_open(struct fb_info *info, int user)
{
    struct win0_par *par = info->par;

    fbprintk(">>>>>> %s : %s \n", __FILE__, __FUNCTION__);

    par->par_seted = 0;
    par->addr_seted = 0;

    if(par->refcount) {
        printk(">>>>>> win0fb has opened! \n");
        return -EACCES;
    } else {
        par->refcount++;
        return 0;
    }
}

int win0fb_release(struct fb_info *info, int user)
{
    struct win0_par *par = info->par;
	struct fb_var_screeninfo *var0 = &info->var;

    fbprintk(">>>>>> %s : %s \n", __FILE__, __FUNCTION__);

    if(par->refcount) {
        par->refcount--;

        win0fb_blank(FB_BLANK_POWERDOWN, info);
        // wait for lcdc stop access memory
        msleep(50);

        // unmap memory
        if (info->screen_base) {
            printk(">>>>>> win0fb unmap memory(%d)! \n", info->fix.smem_len);
    	    dma_free_writecombine(NULL, PAGE_ALIGN(info->fix.smem_len),info->screen_base, info->fix.smem_start);
    	    info->screen_base = 0;
    	    info->fix.smem_start = 0;
    	    info->fix.smem_len = 0;
        }

		// clean the var param
		memset(var0, 0, sizeof(struct fb_var_screeninfo));
    }

    return 0;
}

static int win0fb_ioctl(struct fb_info *info, unsigned int cmd, unsigned long arg)
{
    struct rk2818fb_inf *inf = dev_get_drvdata(info->device);
    struct win0_par *par = info->par;
    void __user *argp = (void __user *)arg;

	fbprintk(">>>>>> %s : %s \n", __FILE__, __FUNCTION__);
    fbprintk("win0fb_ioctl cmd = %8x, arg = %8x \n", (u32)cmd, (u32)arg);
    
	CHK_SUSPEND(inf);

    switch(cmd)
    {
    case FB1_IOCTL_GET_PANEL_SIZE:    //get panel size
        {
            u32 panel_size[2];
             if(inf->win0fb->var.rotate == 270) {
                panel_size[0] = inf->cur_screen->y_res;
                panel_size[1] = inf->cur_screen->x_res;
            } else {
                panel_size[0] = inf->cur_screen->x_res;
                panel_size[1] = inf->cur_screen->y_res;
            }    
            
            if(copy_to_user(argp, panel_size, 8))  return -EFAULT;
        }
        break;

    case FB1_IOCTL_SET_YUV_ADDR:    //set y&uv address to register direct
        {
            u32 yuv_phy[2];
            if (copy_from_user(yuv_phy, argp, 8))
			    return -EFAULT;
           LcdWrReg(inf, WIN0_YRGB_VIR_MST,yuv_phy[0]);
           LcdWrReg(inf, WIN0_CBR_VIR_MST,yuv_phy[1]);

            yuv_phy[0] += par->y_offset;
            yuv_phy[1] += par->uv_offset;

            LcdWrReg(inf, WIN0_YRGB_MST, yuv_phy[0]);
            LcdWrReg(inf, WIN0_CBR_MST, yuv_phy[1]);
            LcdWrReg(inf, REG_CFG_DONE, 0x01);
            // enable win0 after the win0 par is seted
            par->addr_seted = 1;
            if(par->par_seted) { 
    	        LcdMskReg(inf, SYS_CONFIG, m_W0_ENABLE, v_W0_ENABLE(1));
                mcu_refresh(inf);
            }
        }
        break;

    case FB1_IOCTL_SET_ROTATE:    //change MCU panel scan direction        
        fbprintk(">>>>>> change lcdc direction(%d) \n", (int)arg);
        switch(arg)
        {
        case 0:
            win0fb_rotate(info, 0);
            break;
        case 90:
        case 270:
            win0fb_rotate(info, 270);
            break;
        default:
            return -1;
        }
        break;
    case FB1_IOCTL_SET_I2P_ODD_ADDR:
       {
            u32 yuv_phy[4];
            u32 y_addr=0, uv_addr=0;
            u32 pre_y_addr=0, pre_uv_addr=0;
            u32 nxt_y_addr=0,nxt_uv_addr=0;
            if (copy_from_user(yuv_phy, argp, 16))
			    return -EFAULT;

            y_addr = yuv_phy[0] + par->odd_y_offset;
            uv_addr = yuv_phy[1] + par->odd_uv_offset; 
            pre_y_addr = yuv_phy[0] + par->odd_nxt_y_offset;
            pre_uv_addr = yuv_phy[1] + par->odd_nxt_uv_offset; 
            nxt_y_addr = yuv_phy[2] + par->odd_nxt_y_offset;
            nxt_uv_addr = yuv_phy[3] + par->odd_nxt_uv_offset; 

            //printk("new y_addr=%08x, new uv_addr=%08x \n", yuv_phy[0], yuv_phy[1]);
            LcdWrReg(inf, WIN0_YRGB_MST, y_addr);
            LcdWrReg(inf, WIN0_CBR_MST, uv_addr);
            LcdWrReg(inf, I2P_REF0_MST_Y, pre_y_addr);
            LcdWrReg(inf, I2P_REF0_MST_CBR, pre_uv_addr);
            LcdWrReg(inf, I2P_REF1_MST_Y, nxt_y_addr);
            LcdWrReg(inf, I2P_REF1_MST_CBR, nxt_uv_addr);
            LcdMskReg(inf, DSP_CTRL0, m_I2P_CUR_POLARITY, v_I2P_CUR_POLARITY(0));
            LcdWrReg(inf, REG_CFG_DONE, 0x01);

            // enable win0 after the win0 par is seted
            par->addr_seted = 1;
            if(par->par_seted) { 
    	        LcdMskReg(inf, SYS_CONFIG, m_W0_ENABLE, v_W0_ENABLE(1));
                mcu_refresh(inf);
            }
        } 
        break;
    case FB1_IOCTL_SET_I2P_EVEN_ADDR:
        {
            u32 yuv_phy[4];
            u32 y_addr=0, uv_addr=0;
            u32 pre_y_addr=0, pre_uv_addr=0;
            u32 nxt_y_addr=0,nxt_uv_addr=0;
            if (copy_from_user(yuv_phy, argp, 16))
			    return -EFAULT;

            y_addr = yuv_phy[0] + par->even_y_offset;
            uv_addr = yuv_phy[1] + par->even_uv_offset; 
            pre_y_addr = yuv_phy[0] + par->even_nxt_y_offset;
            pre_uv_addr = yuv_phy[1] + par->even_nxt_uv_offset; 
            nxt_y_addr = yuv_phy[2] + par->even_nxt_y_offset;
            nxt_uv_addr = yuv_phy[3] + par->even_nxt_uv_offset; 

            //printk("new y_addr=%08x, new uv_addr=%08x \n", yuv_phy[0], yuv_phy[1]);
            LcdWrReg(inf, WIN0_YRGB_MST, y_addr);
            LcdWrReg(inf, WIN0_CBR_MST, uv_addr);
            LcdWrReg(inf, I2P_REF0_MST_Y, pre_y_addr);
            LcdWrReg(inf, I2P_REF0_MST_CBR, pre_uv_addr);
            LcdWrReg(inf, I2P_REF1_MST_Y, nxt_y_addr);
            LcdWrReg(inf, I2P_REF1_MST_CBR, nxt_uv_addr);
            LcdMskReg(inf, DSP_CTRL0, m_I2P_CUR_POLARITY, v_I2P_CUR_POLARITY(1));
            LcdWrReg(inf, REG_CFG_DONE, 0x01);
          
            // enable win0 after the win0 par is seted
            par->addr_seted = 1;
            if(par->par_seted) { 
    	        LcdMskReg(inf, SYS_CONFIG, m_W0_ENABLE, v_W0_ENABLE(1));
                mcu_refresh(inf);
            }
        }
        break;
    default:
        break;
    }
    return 0;
}

static struct fb_ops win0fb_ops = {
	.owner		= THIS_MODULE,
	.fb_open    = win0fb_open,
	.fb_release = win0fb_release,
	.fb_check_var	= win0fb_check_var,
	.fb_set_par	= win0fb_set_par,
	.fb_blank	= win0fb_blank,
    .fb_pan_display = win0fb_pan_display,
    .fb_ioctl = win0fb_ioctl,
	.fb_setcolreg	= fb_setcolreg,
	.fb_fillrect	= cfb_fillrect,
	.fb_copyarea	= cfb_copyarea,
	.fb_imageblit	= cfb_imageblit,
};

static int win1fb_blank(int blank_mode, struct fb_info *info)
{
    struct rk2818fb_inf *inf = dev_get_drvdata(info->device);

    fbprintk(">>>>>> %s : %s \n", __FILE__, __FUNCTION__);

	CHK_SUSPEND(inf);

	switch(blank_mode)
    {
    case FB_BLANK_UNBLANK:
        LcdMskReg(inf, SYS_CONFIG, m_W1_ENABLE, v_W1_ENABLE(1));
        break;
    default:
        LcdMskReg(inf, SYS_CONFIG, m_W1_ENABLE, v_W1_ENABLE(0));
        break;
    }
    LcdWrReg(inf, REG_CFG_DONE, 0x01);

	mcu_refresh(inf);
    return 0;
}

static int win1fb_check_var(struct fb_var_screeninfo *var, struct fb_info *info)
{
    struct rk2818fb_inf *inf = dev_get_drvdata(info->device);
    struct rk28fb_screen *screen = inf->cur_screen;
    u32 ScaleY = 0x1000;
    u16 xpos = (var->nonstd>>8) & 0xfff;
    u16 ypos = (var->nonstd>>20) & 0xfff;
    u16 xlcd = screen->x_res;
    u16 ylcd = screen->y_res;
    u8 trspmode = (var->grayscale>>8) & 0xff;
    u8 trspval = (var->grayscale) & 0xff;
    //u16 xsize = screen->x_res;    //visiable size in panel
    u16 ysize = screen->y_res;   

    fbprintk(">>>>>> %s : %s\n", __FILE__, __FUNCTION__);

	CHK_SUSPEND(inf);

#if (0==WIN1_USE_DOUBLE_BUF)
    if(var->yres_virtual>ylcd)
        var->yres_virtual = ylcd;
#endif

    if( 0==var->xres_virtual || 0==var->yres_virtual ||
        0==var->xres || 0==var->yres || var->xres<16 ||
        trspmode>5 || trspval>16 ||
        ((16!=var->bits_per_pixel)&&(32!=var->bits_per_pixel)) )
    {
        printk(">>>>>> win1fb_check_var fail 1!!! \n");
        printk(">>>>>> 0==%d || 0==%d ", var->xres_virtual,var->yres_virtual);
        printk("0==%d || 0==%d || %d<16 || ", var->xres,var->yres,var->xres<16);
        printk("%d>5 || %d>16 \n", trspmode,trspval);
        printk("bits_per_pixel=%d \n", var->bits_per_pixel);
        return -EINVAL;
    }

    if( (var->xoffset+var->xres)>var->xres_virtual ||
        (var->yoffset+var->yres)>var->yres_virtual ||
        (xpos+var->xres)>xlcd || (ypos+var->yres)>ylcd )
    {
        printk(">>>>>> win1fb_check_var fail 2!!! \n");
        printk(">>>>>> (%d+%d)>%d || ", var->xoffset,var->xres,var->xres_virtual);
        printk("(%d+%d)>%d || ", var->yoffset,var->yres,var->yres_virtual);
        printk("(%d+%d)>%d || (%d+%d)>%d \n", xpos,var->xres,xlcd,ypos,var->yres,ylcd);
        return -EINVAL;
    }

    switch(var->bits_per_pixel)
    {
    case 16:    // rgb565
        var->xres_virtual = (var->xres_virtual + 0x1) & (~0x1);
        var->xres = (var->xres + 0x1) & (~0x1);
        var->xoffset = (var->xoffset) & (~0x1);
        break;
    default:    // rgb888
        var->bits_per_pixel = 32;
        break;
    }
    
    ScaleY = CalScaleW1(var->yres, ysize);
 
    if((ScaleY>0x8000) ||(ScaleY<0x200))
    {
        return (-EINVAL);        // multiple of scale down or scale up can't exceed 8
    }   
 
    return 0;
}

static int win1fb_set_par(struct fb_info *info)
{
    struct rk2818fb_inf *inf = dev_get_drvdata(info->device);
    struct fb_var_screeninfo *var = &info->var;
    struct fb_fix_screeninfo *fix = &info->fix;
    struct rk28fb_screen *screen = inf->cur_screen;
    

    u8 format = 0;
    dma_addr_t map_dma;
    u32 offset=0, addr=0, map_size=0, smem_len=0;
    u32 ScaleX = 0x1000;
    u32 ScaleY = 0x1000;

    u16 xres_virtual = var->xres_virtual;      //virtual screen size
    //u16 yres_virtual = var->yres_virtual;
    u16 xsize_virtual = var->xres;             //visiable size in virtual screen
    u16 ysize_virtual = var->yres;
    u16 xpos_virtual = var->xoffset;           //visiable offset in virtual screen
    u16 ypos_virtual = var->yoffset;
    
    u16 xpos = 0;                 //visiable offset in panel
    u16 ypos = 0;
    u16 xsize = screen->x_res;    //visiable size in panel
    u16 ysize = screen->y_res;   
    u8 trspmode = TRSP_CLOSE;
    u8 trspval = 0;

    fbprintk(">>>>>> %s : %s\n", __FILE__, __FUNCTION__);

	CHK_SUSPEND(inf);

    switch(var->bits_per_pixel)
    {
    case 16:    // rgb565
        format = 1;
        fix->line_length = 2 * xres_virtual;
        offset = (ypos_virtual*xres_virtual + xpos_virtual)*2;
        break;
    case 32:    // rgb888
    default:
        format = 0;
        fix->line_length = 4 * xres_virtual;
        offset = (ypos_virtual*xres_virtual + xpos_virtual)*4;
        break;
    }

    smem_len = fix->line_length * var->yres_virtual + CURSOR_BUF_SIZE;   //cursor buf also alloc here
    map_size = PAGE_ALIGN(smem_len);

#if WIN1_USE_DOUBLE_BUF
    if( var->yres_virtual == 2*screen->y_res ) {
        inf->mcu_usetimer = 0;
    }
    if(0==fix->smem_len) {
        smem_len = smem_len*2;
        map_size = PAGE_ALIGN(smem_len);
        fbprintk(">>>>>> first alloc, alloc double!!! \n ");
    }
#endif

#if WIN1_USE_DOUBLE_BUF
    if (smem_len > fix->smem_len)     // buffer need realloc
#else
    if (smem_len != fix->smem_len)     // buffer need realloc
#endif
    {
        fbprintk(">>>>>> win1 buffer size is change(%d->%d)! remap memory!\n",fix->smem_len, smem_len);
        fbprintk(">>>>>> smem_len %d = %d * %d \n", smem_len, fix->line_length, var->yres_virtual);
        fbprintk(">>>>>> map_size = %d\n", map_size);
        LcdMskReg(inf, SYS_CONFIG, m_W1_ENABLE, v_W1_ENABLE(0));
        LcdWrReg(inf, REG_CFG_DONE, 0x01);
        msleep(50);
        if (info->screen_base) {
            printk(">>>>>> win1fb unmap memory(%d)! \n", info->fix.smem_len);
	        dma_free_writecombine(NULL, PAGE_ALIGN(info->fix.smem_len), info->screen_base, info->fix.smem_start);
	        info->screen_base = 0;
	        fix->smem_start = 0;
	        fix->smem_len = 0;
        }

        info->screen_base = dma_alloc_writecombine(NULL, map_size, &map_dma, GFP_KERNEL);
        if(!info->screen_base) {
            printk(">>>>>> win1fb dma_alloc_writecombine fail!\n");
            return -ENOMEM;
        }
        memset(info->screen_base, 0, map_size);
        fix->smem_start = map_dma;
        fix->smem_len = smem_len;
        fbprintk(">>>>>> alloc succ, mem=%08x, len=%d!\n", (u32)fix->smem_start, fix->smem_len);     
    }

    addr = fix->smem_start + offset;

    ScaleX = CalScaleW1(xsize_virtual, xsize);
    ScaleY = CalScaleW1(ysize_virtual, ysize);

    LcdMskReg(inf, SYS_CONFIG, m_W1_ENABLE|m_W1_FORMAT, v_W1_ENABLE(1)|v_W1_FORMAT(format));  

    xpos += (screen->left_margin + screen->hsync_len);
    ypos += (screen->upper_margin + screen->vsync_len);
   
    LcdWrReg(inf, WIN1_YRGB_MST, addr);
    LcdWrReg(inf, WIN1_VIR_MST, fix->smem_start);
    
    LcdMskReg(inf, WIN1_DSP_ST, m_BIT11LO|m_BIT11HI, v_BIT11LO(xpos) | v_BIT11HI(ypos));
    LcdMskReg(inf, WIN1_DSP_INFO, m_BIT11LO|m_BIT11HI, v_BIT11LO(xsize) | v_BIT11HI(ysize)); 

    LcdMskReg(inf, WIN1_VIR, m_WORDLO | m_WORDHI , v_WORDLO(xres_virtual) | v_WORDHI(var->yres_virtual));
    LcdMskReg(inf, WIN1_ACT_INFO, m_WORDLO | m_WORDHI, v_WORDLO(xsize_virtual) | v_WORDHI(ysize_virtual));

    LcdMskReg(inf, WIN1_SCL_FACTOR, m_HSCALE_FACTOR | m_VSCALE_FACTOR, v_HSCALE_FACTOR(ScaleX) | v_VSCALE_FACTOR(ScaleY));

    LcdMskReg(inf, BLEND_CTRL, m_W1_BLEND_EN | m_W1_BLEND_FACTOR_SELECT | m_W1_BLEND_FACTOR,
        v_W1_BLEND_EN((TRSP_FMREG==trspmode) || (TRSP_MASK==trspmode)) | 
        v_W1_BLEND_FACTOR_SELECT(TRSP_FMRAM==trspmode) | v_W1_BLEND_FACTOR(trspval)); 

     // enable win1 color key and set the color to black(rgb=0)
    LcdMskReg(inf, WIN1_COLOR_KEY_CTRL, m_COLORKEY_EN | m_KEYCOLOR, v_COLORKEY_EN(1) | v_KEYCOLOR(0));    
    
    if(1==format) //rgb565
    {
        LcdMskReg(inf, SWAP_CTRL, m_W1_8_SWAP | m_W1_16_SWAP | m_W1_R_SHIFT_SWAP | m_W1_565_RB_SWAP,
            v_W1_8_SWAP(0) | v_W1_16_SWAP(0) | v_W1_R_SHIFT_SWAP(0) | v_W1_565_RB_SWAP(0) );
    } 
    else 
    {
     LcdMskReg(inf, SWAP_CTRL, m_W1_8_SWAP | m_W1_16_SWAP | m_W1_R_SHIFT_SWAP | m_W1_565_RB_SWAP,
            v_W1_8_SWAP(0) | v_W1_16_SWAP(0) | v_W1_R_SHIFT_SWAP(0) | v_W1_565_RB_SWAP(0) );
    }

	LcdWrReg(inf, REG_CFG_DONE, 0x01);

    return 0;    
}

static int win1fb_pan_display(struct fb_var_screeninfo *var, struct fb_info *info)
{
    struct rk2818fb_inf *inf = dev_get_drvdata(info->device);
    struct fb_var_screeninfo *var1 = &info->var;
    struct fb_fix_screeninfo *fix1 = &info->fix;

    u32 offset = 0, addr = 0;
    
	fbprintk(">>>>>> %s : %s \n", __FILE__, __FUNCTION__);

	CHK_SUSPEND(inf);

    switch(var1->bits_per_pixel)
    {
    case 16:    // rgb565
        var->xoffset = (var->xoffset) & (~0x1);
        offset = (var->yoffset*var1->xres_virtual + var->xoffset)*2;
        break;
    case 32:    // rgb888
        offset = (var->yoffset*var1->xres_virtual + var->xoffset)*4;
        break;
    default:
        return -EINVAL;
    }
    
    addr = fix1->smem_start + offset;
 
    fbprintk("info->screen_base = %8x ; fix1->smem_len = %d , addr = %8x\n",(u32)info->screen_base, fix1->smem_len, addr);

    LcdWrReg(inf, WIN1_YRGB_MST, addr);
    LcdWrReg(inf, REG_CFG_DONE, 0x01);
  
	mcu_refresh(inf);

    // flush end when wq_condition=1 in mcu panel, but not in rgb panel
    if(SCREEN_MCU == inf->cur_screen->type) {
        wait_event_interruptible_timeout(wq, wq_condition, HZ/20);
        wq_condition = 0;
    } else {
        wq_condition = 0;
        wait_event_interruptible_timeout(wq, wq_condition, HZ/20);
    }

    return 0;
}


static int win1fb_ioctl(struct fb_info *info, unsigned int cmd, unsigned long arg)
{
    struct rk2818fb_inf *inf = dev_get_drvdata(info->device);
    struct rk2818fb_info *mach_info = info->device->platform_data;
    unsigned display_on;    
    int display_on_pol;

	fbprintk(">>>>>> %s : %s \n", __FILE__, __FUNCTION__);

	CHK_SUSPEND(inf);

    switch(cmd)
    {
    case FB0_IOCTL_STOP_TIMER_FLUSH:    //stop timer flush mcu panel after android is runing
        if(1==arg)
        {
            inf->mcu_usetimer = 0;
        }
        break;

    case FB0_IOCTL_SET_PANEL:
        if(arg>7)   return -1;

        /* Black out, because some display device need clock to standby */
        //LcdMskReg(inf, DSP_CTRL_REG1, m_BLACK_OUT, v_BLACK_OUT(1));
        LcdMskReg(inf, SYS_CONFIG, m_W0_ENABLE, v_W0_ENABLE(0));
        LcdMskReg(inf, SYS_CONFIG, m_W1_ENABLE, v_W1_ENABLE(0));
        LcdMskReg(inf, DSP_CTRL1, m_BLACK_MODE,  v_BLACK_MODE(1));          
        LcdWrReg(inf, REG_CFG_DONE, 0x01);
        if(inf->cur_screen)
        {
            if(inf->cur_screen->standby)    inf->cur_screen->standby(1);
            // operate the display_on pin to power down the lcd
            if(SCREEN_RGB==inf->cur_screen->type || SCREEN_MCU==inf->cur_screen->type) 
            {
                if(mach_info && mach_info->disp_on_pin)
                {
                    display_on = mach_info->disp_on_pin;    
                    display_on_pol = mach_info->disp_on_value;                    
                    gpio_direction_output(display_on, 0);
            		gpio_set_value(display_on, !display_on_pol);
                }
            }
        }

        /* Load the new device's param */
        switch(arg)
        {
        case 0: inf->cur_screen = &inf->lcd_info;   break;  //lcd
        case 1: inf->cur_screen = &inf->tv_info[0]; break;  //tv ntsc cvbs
        case 2: inf->cur_screen = &inf->tv_info[1]; break;  //tv pal cvbs
        case 3: inf->cur_screen = &inf->tv_info[2]; break;  //tv 480 ypbpr
        case 4: inf->cur_screen = &inf->tv_info[3]; break;  //tv 576 ypbpr
        case 5: inf->cur_screen = &inf->tv_info[4]; break;  //tv 720 ypbpr
        case 6: inf->cur_screen = &inf->hdmi_info[0];  break;  //hdmi 576
        case 7: inf->cur_screen = &inf->hdmi_info[1];  break;  //hdmi 720
        default: break;
        }
        load_screen(info, 1);
		mcu_refresh(inf);
        break;
    default:
        break;
    }
    return 0;
}


static struct fb_ops win1fb_ops = {
	.owner		= THIS_MODULE,
	.fb_check_var	= win1fb_check_var,
	.fb_set_par = win1fb_set_par,
	.fb_blank   = win1fb_blank,
	.fb_pan_display = win1fb_pan_display,
    .fb_ioctl = win1fb_ioctl,
	.fb_setcolreg	= fb_setcolreg,
	.fb_fillrect    = cfb_fillrect,
	.fb_copyarea	= cfb_copyarea,
	.fb_imageblit	= cfb_imageblit,
	.fb_cursor      = rk2818_set_cursor,
};


static irqreturn_t rk2818fb_irq(int irq, void *dev_id)
{
	struct platform_device *pdev = (struct platform_device*)dev_id;
    struct rk2818fb_inf *inf = platform_get_drvdata(pdev);
    if(!inf)
        return IRQ_HANDLED;

	//fbprintk(">>>>>> %s : %s \n", __FILE__, __FUNCTION__);

    LcdMskReg(inf, INT_STATUS, m_FRM_STARTCLEAR, v_FRM_STARTCLEAR(1));

	if(SCREEN_MCU == inf->cur_screen->type)
	{
        inf->mcu_isrcnt = !inf->mcu_isrcnt;
        if(inf->mcu_isrcnt)
            return IRQ_HANDLED;

        if(IsMcuUseFmk())
        {            
            if(LcdReadBit(inf, MCU_TIMING_CTRL, m_MCU_HOLD_STATUS) && (inf->mcu_fmksync == 0))
            {      
                inf->mcu_fmksync = 1;
                 if(inf->cur_screen->refresh)
                   inf->cur_screen->refresh(REFRESH_END);  
                inf->mcu_fmksync = 0;
            }
            else
            {         
                return IRQ_HANDLED;                
            }
        }
        else
        {
            if(inf->mcu_needflush) {
                if(inf->cur_screen->refresh)
                    inf->cur_screen->refresh(REFRESH_PRE);
                inf->mcu_needflush = 0;                
                inf->mcu_isrcnt = 0;
                LcdSetRegBit(inf, MCU_TIMING_CTRL, m_MCU_HOLDMODE_FRAME_ST); 
            } else {
                if(inf->cur_screen->refresh)
                    inf->cur_screen->refresh(REFRESH_END);
            }
        }
	}

	wq_condition = 1;
 	wake_up_interruptible(&wq);

	return IRQ_HANDLED;
}

#ifdef CONFIG_HAS_EARLYSUSPEND

struct suspend_info {
	struct early_suspend early_suspend;
	struct rk2818fb_inf *inf;
};

void suspend(struct early_suspend *h)
{
	struct suspend_info *info = container_of(h, struct suspend_info,
						early_suspend);

    struct rk2818fb_inf *inf = info->inf;
        
    fbprintk(">>>>>> %s : %s\n", __FILE__, __FUNCTION__);

    if(!inf) {
        printk("inf==0, rk2818fb_suspend fail! \n");
        return;
    }
    
    set_lcd_pin(g_pdev, 0);

	if(inf->cur_screen->standby)
	{
		fbprintk(">>>>>> power down the screen! \n");
		inf->cur_screen->standby(1);
	}

    LcdMskReg(inf, DSP_CTRL1, m_BLANK_MODE , v_BLANK_MODE(1));    
    LcdMskReg(inf, SYS_CONFIG, m_STANDBY, v_STANDBY(1));
   	LcdWrReg(inf, REG_CFG_DONE, 0x01);

	if(!inf->in_suspend)
	{
		fbprintk(">>>>>> diable the lcdc clk! \n");
		msleep(100);
    	if (inf->dclk){
            clk_disable(inf->dclk);
        }
        if(inf->clk){
            clk_disable(inf->clk);
        }
            
		inf->in_suspend = 1;
	}

}

void resume(struct early_suspend *h)
{
	struct suspend_info *info = container_of(h, struct suspend_info,
					early_suspend);

    struct rk2818fb_inf *inf = info->inf;

    fbprintk(">>>>>> %s : %s\n", __FILE__, __FUNCTION__);
    if(!inf) {
        printk("inf==0, rk2818fb_resume fail! \n");
        return ;
    }

	if(inf->in_suspend)
	{
	    inf->in_suspend = 0;
    	fbprintk(">>>>>> enable the lcdc clk! \n");
        if (inf->dclk){
            clk_enable(inf->dclk);           
        }  
        if(inf->clk){
            clk_enable(inf->clk);
        }        
        msleep(100);
	}
    LcdMskReg(inf, DSP_CTRL1, m_BLANK_MODE , v_BLANK_MODE(0));    
    LcdMskReg(inf, SYS_CONFIG, m_STANDBY, v_STANDBY(0));
    LcdWrReg(inf, REG_CFG_DONE, 0x01);

	if(inf->cur_screen->standby)
	{
		fbprintk(">>>>>> power on the screen! \n");
		inf->cur_screen->standby(0);
	}
    msleep(100);
    set_lcd_pin(g_pdev, 1);
	memcpy(inf->preg, &inf->regbak, 47*4);  //resume reg
}

struct suspend_info suspend_info = {
	.early_suspend.suspend = suspend,
	.early_suspend.resume = resume,
	.early_suspend.level = EARLY_SUSPEND_LEVEL_DISABLE_FB,
};
#endif

static int __init rk2818fb_probe (struct platform_device *pdev)
{
    struct rk2818fb_inf *inf = NULL;
    struct resource *res = NULL;
    struct resource *mem = NULL;
    struct rk2818fb_info *mach_info = NULL;
    struct rk28fb_screen *screen = NULL;
	int irq = 0;
    int ret = 0;

    fbprintk(">>>>>> %s : %s\n", __FILE__, __FUNCTION__);

    /* Malloc rk2818fb_inf and set it to pdev for drvdata */
    fbprintk(">> Malloc rk2818fb_inf and set it to pdev for drvdata \n");
    inf = kmalloc(sizeof(struct rk2818fb_inf), GFP_KERNEL);
    if(!inf) 
    {
        dev_err(&pdev->dev, ">> inf kmalloc fail!");
        ret = -ENOMEM;
		goto release_drvdata;
    }   
    memset(inf, 0, sizeof(struct rk2818fb_inf));
	platform_set_drvdata(pdev, inf);

    mach_info = pdev->dev.platform_data;
    /* Fill screen info and set current screen */
    fbprintk(">> Fill screen info and set current screen \n");
    set_lcd_info(&inf->lcd_info, mach_info->lcd_info);
    set_tv_info(&inf->tv_info[0]);
    set_hdmi_info(&inf->hdmi_info[0]);
    inf->cur_screen = &inf->lcd_info;
    screen = inf->cur_screen;
    if(SCREEN_NULL==screen->type) 
    {
        dev_err(&pdev->dev, ">> Please select a display device! \n");
        ret = -EINVAL;
		goto release_drvdata;
    }

    /* get virtual basic address of lcdc register */
    fbprintk(">> get virtual basic address of lcdc register \n");
    res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
    if (res == NULL) 
    {
        dev_err(&pdev->dev, "failed to get memory registers\n");
        ret = -ENOENT;
		goto release_drvdata;
    }
    inf->reg_phy_base = res->start;
    inf->len = (res->end - res->start) + 1;
    mem = request_mem_region(inf->reg_phy_base, inf->len, pdev->name);
    if (mem == NULL) 
    {
        dev_err(&pdev->dev, "failed to get memory region\n");
        ret = -ENOENT;
		goto release_drvdata;
    }
    fbprintk("inf->reg_phy_base = 0x%08x, inf->len = %d \n", inf->reg_phy_base, inf->len);
    inf->reg_vir_base = ioremap(inf->reg_phy_base, inf->len);
    if (inf->reg_vir_base == NULL) 
    {
        dev_err(&pdev->dev, "ioremap() of registers failed\n");
        ret = -ENXIO;
		goto release_drvdata;
    }  
    inf->preg = (LCDC_REG*)inf->reg_vir_base;

    /* Prepare win1 info */
    fbprintk(">> Prepare win1 info \n");
   	inf->win1fb = framebuffer_alloc(sizeof(struct win1_par), &pdev->dev);
    if(!inf->win1fb) 
    {
        dev_err(&pdev->dev, ">> win1fb framebuffer_alloc fail!");
		inf->win1fb = NULL;
        ret = -ENOMEM;
		goto release_win1fb;
    }

    strcpy(inf->win1fb->fix.id, "win1fb");
    inf->win1fb->fix.type        = FB_TYPE_PACKED_PIXELS;
    inf->win1fb->fix.type_aux    = 0;
    inf->win1fb->fix.xpanstep    = 1;
    inf->win1fb->fix.ypanstep    = 1;
    inf->win1fb->fix.ywrapstep   = 0;
    inf->win1fb->fix.accel       = FB_ACCEL_NONE;
    inf->win1fb->fix.visual      = FB_VISUAL_TRUECOLOR;
    inf->win1fb->fix.smem_len    = 0;
    inf->win1fb->fix.line_length = 0;
    inf->win1fb->fix.smem_start  = 0;

    inf->win1fb->var.xres = screen->x_res;
    inf->win1fb->var.yres = screen->y_res;
    inf->win1fb->var.bits_per_pixel = 16;
    inf->win1fb->var.xres_virtual = screen->x_res;
    inf->win1fb->var.yres_virtual = screen->y_res;
    inf->win1fb->var.width = screen->x_res;
    inf->win1fb->var.height = screen->y_res;
    inf->win1fb->var.pixclock = screen->pixclock;
    inf->win1fb->var.left_margin = screen->left_margin;
    inf->win1fb->var.right_margin = screen->right_margin;
    inf->win1fb->var.upper_margin = screen->upper_margin;
    inf->win1fb->var.lower_margin = screen->lower_margin;
    inf->win1fb->var.vsync_len = screen->vsync_len;
    inf->win1fb->var.hsync_len = screen->hsync_len;
    inf->win1fb->var.red    = def_rgb_16.red;
    inf->win1fb->var.green  = def_rgb_16.green;
    inf->win1fb->var.blue   = def_rgb_16.blue;
    inf->win1fb->var.transp = def_rgb_16.transp;

    inf->win1fb->var.nonstd      = 0;  //win1 format & ypos & xpos (ypos<<20 + xpos<<8 + format)
    inf->win1fb->var.grayscale   = 0;  //win1 transprent mode & value(mode<<8 + value)
    inf->win1fb->var.activate    = FB_ACTIVATE_NOW;
    inf->win1fb->var.accel_flags = 0;
    inf->win1fb->var.vmode       = FB_VMODE_NONINTERLACED;

    inf->win1fb->fbops           = &win1fb_ops;
    inf->win1fb->flags           = FBINFO_FLAG_DEFAULT;
    inf->win1fb->pseudo_palette  = ((struct win1_par*)inf->win1fb->par)->pseudo_pal;
    inf->win1fb->screen_base     = 0;

    memset(inf->win1fb->par, 0, sizeof(struct win1_par));
	ret = fb_alloc_cmap(&inf->win1fb->cmap, 256, 0);
	if (ret < 0)
		goto release_cmap;

    /* Prepare win0 info */
    fbprintk(">> Prepare win0 info \n");
    inf->win0fb = framebuffer_alloc(sizeof(struct win0_par), &pdev->dev);
    if(!inf->win0fb)
    {
        dev_err(&pdev->dev, ">> win0fb framebuffer_alloc fail!");
		inf->win0fb = NULL;
		ret = -ENOMEM;
		goto release_win0fb;
    }

    strcpy(inf->win0fb->fix.id, "win0fb");
	inf->win0fb->fix.type	      = FB_TYPE_PACKED_PIXELS;
	inf->win0fb->fix.type_aux    = 0;
	inf->win0fb->fix.xpanstep    = 1;
	inf->win0fb->fix.ypanstep    = 1;
	inf->win0fb->fix.ywrapstep   = 0;
	inf->win0fb->fix.accel       = FB_ACCEL_NONE;
    inf->win0fb->fix.visual      = FB_VISUAL_TRUECOLOR;
    inf->win0fb->fix.smem_len    = 0;
    inf->win0fb->fix.line_length = 0;
    inf->win0fb->fix.smem_start  = 0;

    inf->win0fb->var.xres = screen->x_res;
    inf->win0fb->var.yres = screen->y_res;
    inf->win0fb->var.bits_per_pixel = 16;
    inf->win0fb->var.xres_virtual = screen->x_res;
    inf->win0fb->var.yres_virtual = screen->y_res;
    inf->win0fb->var.width = screen->x_res;
    inf->win0fb->var.height = screen->y_res;
    inf->win0fb->var.pixclock = screen->pixclock;
    inf->win0fb->var.left_margin = screen->left_margin;
    inf->win0fb->var.right_margin = screen->right_margin;
    inf->win0fb->var.upper_margin = screen->upper_margin;
    inf->win0fb->var.lower_margin = screen->lower_margin;
    inf->win0fb->var.vsync_len = screen->vsync_len;
    inf->win0fb->var.hsync_len = screen->hsync_len;
    inf->win0fb->var.red    = def_rgb_16.red;
    inf->win0fb->var.green  = def_rgb_16.green;
    inf->win0fb->var.blue   = def_rgb_16.blue;
    inf->win0fb->var.transp = def_rgb_16.transp;

    inf->win0fb->var.nonstd      = 0;  //win0 format & ypos & xpos (ypos<<20 + xpos<<8 + format)
    inf->win0fb->var.grayscale   = ((inf->win0fb->var.yres<<20)&0xfff00000) + ((inf->win0fb->var.xres<<8)&0xfff00);//win0 xsize & ysize
    inf->win0fb->var.activate    = FB_ACTIVATE_NOW;
    inf->win0fb->var.accel_flags = 0;
    inf->win0fb->var.vmode       = FB_VMODE_NONINTERLACED;

    inf->win0fb->fbops           = &win0fb_ops;
	inf->win0fb->flags		      = FBINFO_FLAG_DEFAULT;
	inf->win0fb->pseudo_palette  = ((struct win0_par*)inf->win0fb->par)->pseudo_pal;
	inf->win0fb->screen_base     = 0;

    memset(inf->win0fb->par, 0, sizeof(struct win0_par));

 	/* Init all lcdc and lcd before register_framebuffer. */
 	/* because after register_framebuffer, the win1fb_check_par and winfb_set_par execute immediately */
 	fbprintk(">> Init all lcdc and lcd before register_framebuffer \n");
    init_lcdc(inf->win1fb);
    inf->clk = clk_get(&pdev->dev, "lcdc_hclk");
    if (!inf->clk || IS_ERR(inf->clk)) 
    {
        printk(KERN_ERR "failed to get lcdc_hclk source\n");
        ret = -ENOENT;
        goto unregister_win1fb;
    }
    
    inf->dclk = clk_get(&pdev->dev, "lcdc");
	if (!inf->dclk || IS_ERR(inf->dclk)) 
    {
		printk(KERN_ERR "failed to get lcd dclock source\n");
		ret = -ENOENT;
		goto unregister_win1fb;
	}
    inf->dclk_parent = clk_get(&pdev->dev, "arm_pll");
    if (!inf->dclk_parent || IS_ERR(inf->dclk_parent))
    {
		printk(KERN_ERR "failed to get lcd dclock parent source\n");
		ret = -ENOENT;
		goto unregister_win1fb;
	}
    inf->dclk_divider= clk_get(&pdev->dev, "lcdc_divider");
    if (!inf->dclk_divider || IS_ERR(inf->dclk_divider))
    {
		printk(KERN_ERR "failed to get lcd clock lcdc_divider source \n");
		ret = -ENOENT;
		goto unregister_win1fb;
	}
   
    inf->clk_share_mem = clk_get(&pdev->dev, "lcdc_share_memory");
    if (!inf->clk_share_mem || IS_ERR(inf->clk_share_mem))
    {
		dev_err(&pdev->dev,  "failed to get lcd clock clk_share_mem source \n");
		ret = -ENOENT;
		goto unregister_win1fb;
	}
  #ifdef CONFIG_CPU_FREQ
    inf->freq_transition.notifier_call = rk2818fb_freq_transition;
    cpufreq_register_notifier(&inf->freq_transition, CPUFREQ_TRANSITION_NOTIFIER);
  #endif
	fbprintk("got clock\n");  
    
	if(mach_info)
    {
        struct rk2818_fb_setting_info fb_setting;
        if( OUT_P888==inf->lcd_info.face ||
            OUT_P888==inf->tv_info[0].face ||
            OUT_P888==inf->hdmi_info[0].face )     // set lcdc iomux
        {
            fb_setting.data_num = 24;
        } 
        else if(OUT_P666 == inf->lcd_info.face )
        {
            fb_setting.data_num = 18;
        }        
        else 
        {
            fb_setting.data_num = 16;
        }        
        fb_setting.den_en = 1;
        fb_setting.vsync_en = 1;
        fb_setting.disp_on_en = 1;
        fb_setting.standby_en = 1;
        if( inf->lcd_info.mcu_usefmk )
            fb_setting.mcu_fmk_en =1;
        mach_info->io_init(&fb_setting);
    }
    
	set_lcd_pin(pdev, 1);
	mdelay(10);
	g_pdev = pdev;
	inf->mcu_usetimer = 1;
    inf->mcu_fmksync = 0;
	load_screen(inf->win1fb, 1);

    /* Register framebuffer(win1fb & win0fb) */
    fbprintk(">> Register framebuffer(win1fb) \n");
    ret = register_framebuffer(inf->win1fb);
    if(ret<0) 
    {
        printk(">> win1fb register_framebuffer fail!\n");
        ret = -EINVAL;
		goto release_win0fb;
    }
    
    fbprintk(">> Register framebuffer(win0fb) \n");

    ret = register_framebuffer(inf->win0fb);
    if(ret<0) 
    {
        printk(">> win0fb register_framebuffer fail!\n");
        ret = -EINVAL;
		goto unregister_win1fb;
    }

#ifdef CONFIG_HAS_EARLYSUSPEND
	suspend_info.inf = inf;
	register_early_suspend(&suspend_info.early_suspend);
#endif

    /* get and request irq */
    fbprintk(">> get and request irq \n");
    irq = platform_get_irq(pdev, 0);
    if (irq < 0) {
        dev_err(&pdev->dev, "no irq for device\n");
        ret = -ENOENT;
        goto unregister_win1fb;
    }
    ret = request_irq(irq, rk2818fb_irq, IRQF_DISABLED, pdev->name, pdev);
    if (ret) {
        dev_err(&pdev->dev, "cannot get irq %d - err %d\n", irq, ret);
        ret = -EBUSY;
        goto release_irq;
    }
    
    if( inf->lcd_info.mcu_usefmk && (mach_info->mcu_fmk_pin != -1) )
    {
        ret = request_irq(gpio_to_irq(mach_info->mcu_fmk_pin), mcu_irqfmk, GPIOEdgelFalling, pdev->name, pdev);
        if (ret) 
        {
            dev_err(&pdev->dev, "cannot get fmk irq %d - err %d\n", irq, ret);
            ret = -EBUSY;
            goto release_irq;
        }
    }  

    printk(" %s ok\n", __FUNCTION__);
    return ret;

release_irq:
	if(irq>=0)
    	free_irq(irq, pdev);  
unregister_win1fb:
    unregister_framebuffer(inf->win1fb);
release_win0fb:
	if(inf->win0fb)
		framebuffer_release(inf->win0fb);
	inf->win0fb = NULL;
release_cmap:
    if(&inf->win1fb->cmap)
        fb_dealloc_cmap(&inf->win1fb->cmap);
release_win1fb:
	if(inf->win1fb)
		framebuffer_release(inf->win1fb);
	inf->win1fb = NULL;
release_drvdata:
	if(inf && inf->reg_vir_base)
    	iounmap(inf->reg_vir_base);
	if(inf && mem)
    	release_mem_region(inf->reg_phy_base, inf->len);
	if(inf)
    	kfree(inf);
	platform_set_drvdata(pdev, NULL);
	return ret;
}

static int rk2818fb_remove(struct platform_device *pdev)
{
    struct rk2818fb_inf *inf = platform_get_drvdata(pdev);
    struct fb_info *info = NULL;
	//pm_message_t msg;
    struct rk2818fb_info *mach_info = NULL;
    int irq = 0;
    
	fbprintk(">>>>>> %s : %s\n", __FILE__, __FUNCTION__);

    if(!inf) {
        printk("inf==0, rk2818_fb_remove fail! \n");
        return -EINVAL;
    }
    
    irq = platform_get_irq(pdev, 0);
    if (irq >0) 
    {
    free_irq(irq, pdev);   
    }
     
    mach_info = pdev->dev.platform_data;
    if(mach_info->mcu_fmk_pin)
    {
        free_irq(gpio_to_irq(mach_info->mcu_fmk_pin), pdev);
    }

	set_lcd_pin(pdev, 0);

    // blank the lcdc
    if(inf->win0fb)
        win0fb_blank(FB_BLANK_POWERDOWN, inf->win0fb);
    if(inf->win1fb)
        win1fb_blank(FB_BLANK_POWERDOWN, inf->win1fb);
    
	// suspend the lcdc
	//rk2818fb_suspend(pdev, msg);

    // unmap memory and release framebuffer
    if(inf->win0fb) {
        info = inf->win0fb;
        if (info->screen_base) {
	        dma_free_writecombine(NULL, PAGE_ALIGN(info->fix.smem_len),info->screen_base, info->fix.smem_start);
	        info->screen_base = 0;
	        info->fix.smem_start = 0;
	        info->fix.smem_len = 0;
        }
        unregister_framebuffer(inf->win0fb);
        framebuffer_release(inf->win0fb);
        inf->win0fb = NULL;
    }
    if(inf->win1fb) {
        info = inf->win1fb;
        if (info->screen_base) {
	        dma_free_writecombine(NULL, PAGE_ALIGN(info->fix.smem_len),info->screen_base, info->fix.smem_start);
	        info->screen_base = 0;
	        info->fix.smem_start = 0;
	        info->fix.smem_len = 0;
        }
        unregister_framebuffer(inf->win1fb);
        framebuffer_release(inf->win1fb);
        inf->win1fb = NULL;
    }
    
  #ifdef CONFIG_CPU_FREQ
    cpufreq_unregister_notifier(&inf->freq_transition, CPUFREQ_TRANSITION_NOTIFIER);
  #endif
  
	if (inf->clk)
    {
		clk_disable(inf->clk);
		clk_put(inf->clk);
		inf->clk = NULL;
	}
    if (inf->dclk)
    {
		clk_disable(inf->dclk);
		clk_put(inf->dclk);
		inf->dclk = NULL;
	}
    
    kfree(inf);
    platform_set_drvdata(pdev, NULL);

    return 0;
}

static void rk2818fb_shutdown(struct platform_device *pdev)
{
    struct rk2818fb_inf *inf = platform_get_drvdata(pdev);
    mdelay(300);
	//printk("----------------------------rk2818fb_shutdown----------------------------\n");
  	set_lcd_pin(pdev, 0);	
    if (inf->dclk)
    {
        clk_disable(inf->dclk);
    }
    if (inf->clk)
    {
        clk_disable(inf->clk);
    }    	
}

static struct platform_driver rk2818fb_driver = {
	.probe		= rk2818fb_probe,
	.remove		= rk2818fb_remove,
	.driver		= {
		.name	= "rk2818-fb",
		.owner	= THIS_MODULE,
	},
	.shutdown   = rk2818fb_shutdown,
};

static int __init rk2818fb_init(void)
{   
    return platform_driver_register(&rk2818fb_driver);
}

static void __exit rk2818fb_exit(void)
{
    platform_driver_unregister(&rk2818fb_driver);
}

//subsys_initcall(rk2818fb_init);

module_init(rk2818fb_init);
module_exit(rk2818fb_exit);


MODULE_AUTHOR("  zyw@rock-chips.com");
MODULE_DESCRIPTION("Driver for rk2818 fb device");
MODULE_LICENSE("GPL");


