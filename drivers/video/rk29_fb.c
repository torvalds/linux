/*
 * drivers/video/rk29_fb.c
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

#include "rk29_fb.h"

#ifdef CONFIG_PM
#include <linux/pm.h>
#endif
#ifdef CONFIG_HDMI
#include <linux/completion.h>

#include <linux/hdmi.h>
#endif

#include <mach/iomux.h>
#include <mach/gpio.h>
#include <mach/board.h>
#include <mach/rk29_iomap.h>
#include <mach/pmu.h>
//#include <asm/uaccess.h>

#include "./display/screen/screen.h"

#define ANDROID_USE_THREE_BUFS  0       //android use three buffers to accelerate UI display in rgb plane

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

#define CalScaleW0(x, y)	             (((u32)x*0x1000)/y)

struct rk29fb_rgb {
	struct fb_bitfield	red;
	struct fb_bitfield	green;
	struct fb_bitfield	blue;
	struct fb_bitfield	transp;
};

static struct rk29fb_rgb def_rgb_16 = {
     red:    { offset: 11, length: 5, },
     green:  { offset: 5,  length: 6, },
     blue:   { offset: 0,  length: 5, },
     transp: { offset: 0,  length: 0, },
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
    u32 format;

    wait_queue_head_t wait;
    struct win_set mirror;
    struct win_set displ;
    struct win_set done;

    u8 par_seted;
    u8 addr_seted;
};

/*
struct win1_par {
	u32 refcount;
	u32	pseudo_pal[16];
	int lstblank;
    u32 xpos;
    u32 ypos;
    u32 xsize;
    u32 ysize;
    u32 format;
    u32 addr_offset;
};
*/

struct rk29fb_inf {
    struct fb_info *fb1;
    struct fb_info *fb0;

    void __iomem *reg_vir_base;  // virtual basic address of lcdc register
	u32 reg_phy_base;       // physical basic address of lcdc register
	u32 len;               // physical map length of lcdc register
    u32 video_mode;

    struct clk      *clk;
    struct clk      *dclk;            //lcdc dclk
    struct clk      *dclk_parent;     //lcdc dclk divider frequency source
    struct clk      *dclk_divider;    //lcdc demodulator divider frequency
    struct clk      *aclk;   //lcdc share memory frequency
    struct clk      *aclk_parent;     //lcdc aclk divider frequency source
    struct clk      *aclk_ddr_lcdc;   //DDR LCDC AXI clock disable.
    struct clk      *aclk_disp_matrix;  //DISPLAY matrix AXI clock disable.
    struct clk      *hclk_cpu_display;  //CPU DISPLAY AHB bus clock disable.
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

    struct rk29fb_screen panel1_info;         // 1st panel, it's lcd normally
    struct rk29fb_screen panel2_info;         // 2nd panel
    struct rk29fb_screen *cur_screen;
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
//static int win1fb_set_par(struct fb_info *info);

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
static int wq_condition2 = 0;
#if ANDROID_USE_THREE_BUFS
static int new_frame_seted = 1;
#endif

void set_lcd_pin(struct platform_device *pdev, int enable)
{
	struct rk29fb_info *mach_info = pdev->dev.platform_data;

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
}

int mcu_do_refresh(struct rk29fb_inf *inf)
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
    struct rk29fb_inf *inf = platform_get_drvdata(g_pdev);
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

int mcu_refresh(struct rk29fb_inf *inf)
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
    struct rk29fb_inf *inf = NULL;
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
    struct rk29fb_inf *inf = platform_get_drvdata(pdev);
    struct rk29fb_screen *screen;

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
    struct rk29fb_inf *inf = dev_get_drvdata(info->device);
    u32 msk=0, clr=0;

	fbprintk(">>>>>> %s : %s \n", __FILE__, __FUNCTION__);

    pmu_set_power_domain(PD_DISPLAY, 1);
    inf->clk = clk_get(NULL, "hclk_lcdc");
    inf->aclk_ddr_lcdc = clk_get(NULL, "aclk_ddr_lcdc");
    inf->aclk_disp_matrix = clk_get(NULL, "aclk_disp_matrix");
    inf->hclk_cpu_display = clk_get(NULL, "hclk_cpu_display");
    if ((IS_ERR(inf->clk)) || (IS_ERR(inf->aclk_ddr_lcdc)) || (IS_ERR(inf->aclk_disp_matrix)) ||(IS_ERR(inf->hclk_cpu_display)))
    {
        printk(KERN_ERR "failed to get lcdc_hclk source\n");
        return PTR_ERR(inf->clk);
    }
    clk_enable(inf->aclk_ddr_lcdc);
    clk_enable(inf->aclk_disp_matrix);
    clk_enable(inf->hclk_cpu_display);
    clk_enable(inf->clk);

	// set AHB access rule and disable all windows
    LcdWrReg(inf, SYS_CONFIG, 0x60000000);
    LcdWrReg(inf, SWAP_CTRL, 0);
    LcdWrReg(inf, FIFO_WATER_MARK, 0x00000864);//68
    LcdWrReg(inf, AXI_MS_ID, 0x54321);

	// and mcu holdmode; and set win1 top.
    LcdMskReg(inf, MCU_TIMING_CTRL, m_MCU_HOLDMODE_SELECT | m_MCU_HOLDMODE_FRAME_ST | m_MCU_BYPASSMODE_SELECT ,
            v_MCU_HOLDMODE_SELECT(0)| v_MCU_HOLDMODE_FRAME_ST(0) |v_MCU_BYPASSMODE_SELECT(0));

    // disable blank out, black out, tristate out, yuv2rgb bypass
    LcdMskReg(inf, BLEND_CTRL,m_W2_BLEND_EN | m_W1_BLEND_EN | m_W0_BLEND_EN | m_HWC_BLEND_EN |
             m_HWC_BLEND_FACTOR | m_W1_BLEND_FACTOR | m_W0_BLEND_FACTOR,
             v_W2_BLEND_EN(0) |v_W1_BLEND_EN(0) | v_W0_BLEND_EN(0) | v_HWC_BLEND_EN(0) |
             v_HWC_BLEND_FACTOR(0) | v_W2_BLEND_FACTOR(0) | v_W1_BLEND_FACTOR(0) | v_W0_BLEND_FACTOR(0)
             );

    LcdMskReg(inf, WIN0_COLOR_KEY_CTRL, m_COLORKEY_EN, v_COLORKEY_EN(0));
    LcdMskReg(inf, WIN1_COLOR_KEY_CTRL, m_COLORKEY_EN, v_COLORKEY_EN(0));

    LcdWrReg(inf, DSP_CTRL1, 0);

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
    struct rk29fb_inf *inf = dev_get_drvdata(info->device);
    struct rk29fb_screen *screen = inf->cur_screen;
    u16 face;
    u16 mcu_total, mcu_rwstart, mcu_csstart, mcu_rwend, mcu_csend;
    u16 right_margin = screen->right_margin, lower_margin = screen->lower_margin;
    u16 x_res = screen->x_res, y_res = screen->y_res;
    u32 aclk_rate = 150000000;

    if(!g_pdev){
        printk(">>>>>> %s : %s no g_pdev\n", __FILE__, __FUNCTION__);
        return;
    }

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
	switch (screen->face)
	{
        case OUT_P565:
            face = OUT_P565;
            LcdMskReg(inf, DSP_CTRL0, m_DITHER_DOWN_EN | m_DITHER_DOWN_MODE, v_DITHER_DOWN_EN(1) | v_DITHER_DOWN_MODE(0));
            break;
        case OUT_P666:
            face = OUT_P666;
            LcdMskReg(inf, DSP_CTRL0, m_DITHER_DOWN_EN | m_DITHER_DOWN_MODE, v_DITHER_DOWN_EN(1) | v_DITHER_DOWN_MODE(1));
            break;
        case OUT_D888_P565:
            face = OUT_P888;
            LcdMskReg(inf, DSP_CTRL0, m_DITHER_DOWN_EN | m_DITHER_DOWN_MODE, v_DITHER_DOWN_EN(1) | v_DITHER_DOWN_MODE(0));
            break;
        case OUT_D888_P666:
            face = OUT_P888;
            LcdMskReg(inf, DSP_CTRL0, m_DITHER_DOWN_EN | m_DITHER_DOWN_MODE, v_DITHER_DOWN_EN(1) | v_DITHER_DOWN_MODE(1));
            break;
        case OUT_P888:
            face = OUT_P888;
            LcdMskReg(inf, DSP_CTRL0, m_DITHER_UP_EN, v_DITHER_UP_EN(1));
            LcdMskReg(inf, DSP_CTRL0, m_DITHER_DOWN_EN | m_DITHER_DOWN_MODE, v_DITHER_DOWN_EN(0) | v_DITHER_DOWN_MODE(0));
            break;
        default:
            LcdMskReg(inf, DSP_CTRL0, m_DITHER_UP_EN, v_DITHER_UP_EN(0));
            LcdMskReg(inf, DSP_CTRL0, m_DITHER_DOWN_EN | m_DITHER_DOWN_MODE, v_DITHER_DOWN_EN(0) | v_DITHER_DOWN_MODE(0));
            face = screen->face;
            break;
    }

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

    printk("screen->hsync_len =%d,  screen->left_margin =%d, x_res =%d,  right_margin = %d \n",
        screen->hsync_len , screen->left_margin , x_res , right_margin );
    LcdMskReg(inf, DSP_HTOTAL_HS_END, m_BIT12LO | m_BIT12HI, v_BIT12LO(screen->hsync_len) |
             v_BIT12HI(screen->hsync_len + screen->left_margin + x_res + right_margin));
    LcdMskReg(inf, DSP_HACT_ST_END, m_BIT12LO | m_BIT12HI, v_BIT12LO(screen->hsync_len + screen->left_margin + x_res) |
             v_BIT12HI(screen->hsync_len + screen->left_margin));

    LcdMskReg(inf, DSP_VTOTAL_VS_END, m_BIT11LO | m_BIT11HI, v_BIT11LO(screen->vsync_len) |
              v_BIT11HI(screen->vsync_len + screen->upper_margin + y_res + lower_margin));
    LcdMskReg(inf, DSP_VACT_ST_END, m_BIT11LO | m_BIT11HI,  v_BIT11LO(screen->vsync_len + screen->upper_margin+y_res)|
              v_BIT11HI(screen->vsync_len + screen->upper_margin));

    LcdMskReg(inf, DSP_VS_ST_END_F1, m_BIT11LO | m_BIT11HI, v_BIT11LO(0) | v_BIT11HI(0));
    LcdMskReg(inf, DSP_VACT_ST_END_F1, m_BIT11LO | m_BIT11HI, v_BIT11LO(0) | v_BIT11HI(0));

	// let above to take effect
    LcdWrReg(inf, REG_CFG_DONE, 0x01);

    inf->dclk = clk_get(NULL, "dclk_lcdc");
    if (IS_ERR(inf->dclk))
    {
        printk(KERN_ERR "failed to get lcd dclock source\n");
        return ;
    }
    inf->dclk_divider= clk_get(NULL, "dclk_lcdc_div");
    if (IS_ERR(inf->dclk_divider))
    {
        printk(KERN_ERR "failed to get lcd clock lcdc_divider source \n");
		return ;
    }

    if((inf->cur_screen->type == SCREEN_HDMI) || (inf->cur_screen->type == SCREEN_TVOUT)){
        inf->dclk_parent = clk_get(NULL, "codec_pll");
		clk_set_rate(inf->dclk_parent, 594000000);
    }    else    {
        inf->dclk_parent = clk_get(NULL, "general_pll");
    }

    if (IS_ERR(inf->dclk_parent))
    {
        printk(KERN_ERR "failed to get lcd dclock parent source\n");
        return;
    }

    inf->aclk = clk_get(NULL, "aclk_lcdc");
    if (IS_ERR(inf->aclk))
    {
        printk(KERN_ERR "failed to get lcd clock clk_share_mem source \n");
        return;
    }
    inf->aclk_parent = clk_get(NULL, "ddr_pll");//general_pll //ddr_pll
    if (IS_ERR(inf->dclk_parent))
    {
        printk(KERN_ERR "failed to get lcd dclock parent source\n");
        return ;
    }

    // set lcdc clk
    if(SCREEN_MCU==screen->type)    screen->pixclock = 150000000; //mcu fix to 150 MHz

    if(initscreen == 0)    //not init
    {
        clk_disable(inf->dclk);
        clk_disable(inf->aclk);
    }

    clk_disable(inf->aclk_ddr_lcdc);
    clk_disable(inf->aclk_disp_matrix);
    clk_disable(inf->hclk_cpu_display);

    clk_disable(inf->clk);
    clk_set_parent(inf->dclk_divider, inf->dclk_parent);
    clk_set_parent(inf->dclk, inf->dclk_divider);
    ret = clk_set_parent(inf->aclk, inf->aclk_parent);

    fbprintk(">>>>>> set lcdc dclk need %d HZ, clk_parent = %d hz ret =%d\n ", screen->pixclock, screen->lcdc_aclk, ret);

    ret = clk_set_rate(inf->dclk_divider, screen->pixclock);
    if(ret)
    {
        printk(KERN_ERR ">>>>>> set lcdc dclk_divider faild \n ");
    }

    if(screen->lcdc_aclk){
        aclk_rate = screen->lcdc_aclk;
    }
    ret = clk_set_rate(inf->aclk, aclk_rate);
    if(ret){
        printk(KERN_ERR ">>>>>> set lcdc dclk_divider faild \n ");
    }

    clk_enable(inf->dclk);
    clk_enable(inf->aclk);
    clk_enable(inf->clk);

    clk_enable(inf->aclk_ddr_lcdc);
    clk_enable(inf->aclk_disp_matrix);
    clk_enable(inf->hclk_cpu_display);

    // init screen panel
    if(screen->init)
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
#define TO_INF(ptr,member) container_of(ptr,struct rk29fb_inf,member)

static int
rk29fb_freq_transition(struct notifier_block *nb, unsigned long val, void *data)
{
    struct rk29fb_inf *inf = TO_INF(nb, freq_transition);
    struct rk29fb_screen *screen = inf->cur_screen;
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
#if 0

int rk29_set_cursor(struct fb_info *info, struct fb_cursor *cursor)
{
    struct rk29fb_inf *inf = dev_get_drvdata(info->device);

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
        /* rk29 cursor is a 2 bpp 32x32 bitmap this routine
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
#endif

static int win0_blank(int blank_mode, struct fb_info *info)
{
    struct rk29fb_inf *inf = dev_get_drvdata(info->device);

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

static int win0_set_par(struct fb_info *info)
{
    struct rk29fb_inf *inf = dev_get_drvdata(info->device);
    struct rk29fb_screen *screen = inf->cur_screen;
    struct fb_var_screeninfo *var = &info->var;
    struct fb_fix_screeninfo *fix = &info->fix;
    struct win0_par *par = info->par;

	u32 xact = var->xres;			    /* visible resolution		*/
	u32 yact = var->yres;
	u32 xvir = var->xres_virtual;		/* virtual resolution		*/
	u32 yvir = var->yres_virtual;
	//u32 xact_st = var->xoffset;         /* offset from virtual to visible */
	//u32 yact_st = var->yoffset;         /* resolution			*/
    u32 xpos = par->xpos;
    u32 ypos = par->ypos;

    u32 ScaleYrgbX=0x1000,ScaleYrgbY=0x1000;
    u32 ScaleCbrX=0x1000, ScaleCbrY=0x1000;

    u32 y_addr = 0;       //user alloc buf addr y
    u32 uv_addr = 0;

    fbprintk(">>>>>> %s : %s\n", __FILE__, __FUNCTION__);

	CHK_SUSPEND(inf);

	// calculate the display phy address
    y_addr = fix->smem_start + par->y_offset;
    uv_addr = fix->mmio_start + par->c_offset;

    ScaleYrgbX = CalScaleW0(xact, par->xsize);
    ScaleYrgbY = CalScaleW0(yact, par->ysize);

    switch (par->format)
    {
       case 2:// yuv422
           ScaleCbrX= CalScaleW0((xact/2), par->xsize);
           ScaleCbrY =  CalScaleW0(yact, par->ysize);
           break;
       case 3: // yuv4200
       case 4: // yuv4201
           ScaleCbrX= CalScaleW0(xact/2, par->xsize);
           ScaleCbrY =  CalScaleW0(yact/2, par->ysize);
           break;
       case 5:// yuv444
           ScaleCbrX= CalScaleW0(xact, par->xsize);
           ScaleCbrY =  CalScaleW0(yact, par->ysize);
           break;
       default:
           break;
    }

    xpos += (screen->left_margin + screen->hsync_len);
    ypos += (screen->upper_margin + screen->vsync_len);

    LcdWrReg(inf, WIN0_YRGB_MST, y_addr);
    LcdWrReg(inf, WIN0_CBR_MST, uv_addr);

    LcdMskReg(inf, SYS_CONFIG,  m_W0_FORMAT , v_W0_FORMAT(par->format));//(inf->video_mode==0)

    LcdMskReg(inf, WIN0_VIR, m_WORDLO | m_WORDHI, v_VIRWIDTH(xvir) | v_VIRHEIGHT((yvir)) );
    LcdMskReg(inf, WIN0_ACT_INFO, m_WORDLO | m_WORDHI, v_WORDLO(xact) | v_WORDHI(yact));
    LcdMskReg(inf, WIN0_DSP_ST, m_BIT11LO | m_BIT11HI, v_BIT11LO(xpos) | v_BIT11HI(ypos));
    LcdMskReg(inf, WIN0_DSP_INFO, m_BIT12LO | m_BIT12HI,  v_BIT12LO(par->xsize) | v_BIT12HI(par->ysize));
    LcdMskReg(inf, WIN0_SCL_FACTOR_YRGB, m_WORDLO | m_WORDHI, v_WORDLO(ScaleYrgbX) | v_WORDHI(ScaleYrgbY));
    LcdMskReg(inf, WIN0_SCL_FACTOR_CBR, m_WORDLO | m_WORDHI, v_WORDLO(ScaleCbrX) | v_WORDHI(ScaleCbrY));

    switch(par->format)
    {
    case 0:  //rgb888
        LcdMskReg(inf, SWAP_CTRL, m_W0_YRGB_8_SWAP | m_W0_YRGB_16_SWAP | m_W0_YRGB_R_SHIFT_SWAP | m_W0_565_RB_SWAP | m_W0_YRGB_M8_SWAP | m_W0_CBR_8_SWAP,
            v_W0_YRGB_8_SWAP(1) | v_W0_YRGB_16_SWAP(1) | v_W0_YRGB_R_SHIFT_SWAP(1) | v_W0_565_RB_SWAP(0) | v_W0_YRGB_M8_SWAP(0) | v_W0_CBR_8_SWAP(0));
		break;
    case 1:  //rgb565
        LcdMskReg(inf, SWAP_CTRL, m_W0_YRGB_8_SWAP | m_W0_YRGB_16_SWAP | m_W0_YRGB_R_SHIFT_SWAP | m_W0_565_RB_SWAP | m_W0_YRGB_M8_SWAP | m_W0_CBR_8_SWAP,
            v_W0_YRGB_8_SWAP(0) | v_W0_YRGB_16_SWAP(0) | v_W0_YRGB_R_SHIFT_SWAP(0) | v_W0_565_RB_SWAP(0) | v_W0_YRGB_M8_SWAP(0) | v_W0_CBR_8_SWAP(0));
        break;
    case 4:   //yuv4201
        LcdMskReg(inf, SWAP_CTRL, m_W0_YRGB_8_SWAP | m_W0_YRGB_16_SWAP | m_W0_YRGB_R_SHIFT_SWAP | m_W0_565_RB_SWAP | m_W0_YRGB_M8_SWAP | m_W0_CBR_8_SWAP,
            v_W0_YRGB_8_SWAP(0) | v_W0_YRGB_16_SWAP(0) | v_W0_YRGB_R_SHIFT_SWAP(0) | v_W0_565_RB_SWAP(0) |
            v_W0_YRGB_M8_SWAP((var->rotate==0)) | v_W0_CBR_8_SWAP(0));
        break;
    default:
        LcdMskReg(inf, SWAP_CTRL, m_W0_YRGB_8_SWAP | m_W0_YRGB_16_SWAP | m_W0_YRGB_R_SHIFT_SWAP | m_W0_565_RB_SWAP | m_W0_YRGB_M8_SWAP | m_W0_CBR_8_SWAP,
            v_W0_YRGB_8_SWAP(0) | v_W0_YRGB_16_SWAP(0) | v_W0_YRGB_R_SHIFT_SWAP(0) | v_W0_565_RB_SWAP(0) | v_W0_YRGB_M8_SWAP(0) | v_W0_CBR_8_SWAP(0) );
		break;
    }

    LcdWrReg(inf, REG_CFG_DONE, 0x01);

    return 0;

}

static int win0_pan( struct fb_info *info )
{
    struct rk29fb_inf *inf = dev_get_drvdata(info->device);
   // struct fb_var_screeninfo *var0 = &info->var;
    struct fb_fix_screeninfo *fix0 = &info->fix;
    struct win0_par *par = info->par;
    u32 y_addr=0, uv_addr=0;

    fbprintk(">>>>>> %s : %s\n", __FILE__, __FUNCTION__);

	CHK_SUSPEND(inf);

    y_addr = fix0->smem_start +  par->y_offset;//y_offset;
    uv_addr = fix0->mmio_start + par->c_offset ;//c_offset;

    LcdWrReg(inf, WIN0_YRGB_MST, y_addr);
    LcdWrReg(inf, WIN0_CBR_MST, uv_addr);
    LcdWrReg(inf, REG_CFG_DONE, 0x01);

     // enable win0 after the win0 addr is seted
 	LcdMskReg(inf, SYS_CONFIG, m_W0_ENABLE, v_W0_ENABLE((1==par->addr_seted)?(1):(0)));
	mcu_refresh(inf);

    return 0;
}

static int win1_blank(int blank_mode, struct fb_info *info)
{
    struct rk29fb_inf *inf = dev_get_drvdata(info->device);

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

static int win1_set_par(struct fb_info *info)
{
    struct rk29fb_inf *inf = dev_get_drvdata(info->device);
    struct fb_var_screeninfo *var = &info->var;
    struct fb_fix_screeninfo *fix = &info->fix;
    struct rk29fb_screen *screen = inf->cur_screen;
    struct win0_par *par = info->par;

    //u32 offset=0, addr=0, map_size=0, smem_len=0;
    u32 addr=0;

    u16 xres_virtual = var->xres_virtual;      //virtual screen size

    //u16 xpos_virtual = var->xoffset;           //visiable offset in virtual screen
    //u16 ypos_virtual = var->yoffset;

    u16 xpos = par->xpos;                 //visiable offset in panel
    u16 ypos = par->ypos;

    u8 trspmode = TRSP_CLOSE;
    u8 trspval = 0;

    //fbprintk(">>>>>> %s : %s\n", __FILE__, __FUNCTION__);

    addr = fix->smem_start + par->y_offset;

    LcdMskReg(inf, SYS_CONFIG, m_W1_ENABLE|m_W1_FORMAT, v_W1_ENABLE(1)|v_W1_FORMAT(par->format));

    xpos += (screen->left_margin + screen->hsync_len);
    ypos += (screen->upper_margin + screen->vsync_len);

    LcdWrReg(inf, WIN1_YRGB_MST, addr);

    LcdMskReg(inf, WIN1_DSP_ST, m_BIT11LO|m_BIT11HI, v_BIT11LO(xpos) | v_BIT11HI(ypos));
    LcdMskReg(inf, WIN1_DSP_INFO, m_BIT12LO|m_BIT12HI, v_BIT12LO(par->xsize) | v_BIT12HI(par->ysize));

    LcdMskReg(inf, WIN1_VIR, m_WORDLO , v_WORDLO(xres_virtual));

    LcdMskReg(inf, BLEND_CTRL, m_W1_BLEND_EN |  m_W1_BLEND_FACTOR,
        v_W1_BLEND_EN((TRSP_FMREG==trspmode) || (TRSP_MASK==trspmode)) | v_W1_BLEND_FACTOR(trspval));

     // enable win1 color key and set the color to black(rgb=0)
    LcdMskReg(inf, WIN1_COLOR_KEY_CTRL, m_COLORKEY_EN | m_KEYCOLOR, v_COLORKEY_EN(1) | v_KEYCOLOR(0));

    if(1==par->format) //rgb565
    {
        LcdMskReg(inf, SWAP_CTRL, m_W1_8_SWAP | m_W1_16_SWAP | m_W1_R_SHIFT_SWAP | m_W1_565_RB_SWAP,
            v_W1_8_SWAP(0) | v_W1_16_SWAP(0) | v_W1_R_SHIFT_SWAP(0) | v_W1_565_RB_SWAP(0) );
    }
    else
    {
         LcdMskReg(inf, SWAP_CTRL, m_W1_8_SWAP | m_W1_16_SWAP | m_W1_R_SHIFT_SWAP | m_W1_565_RB_SWAP,
                v_W1_8_SWAP(1) | v_W1_16_SWAP(1) | v_W1_R_SHIFT_SWAP(1) | v_W1_565_RB_SWAP(0) );

         LcdMskReg(inf, DSP_CTRL0, m_W1_TRANSP_FROM, v_W1_TRANSP_FROM(TRSP_FMRAM==trspmode) );
    }

	LcdWrReg(inf, REG_CFG_DONE, 0x01);

    return 0;
}

static int win1_pan( struct fb_info *info )
{
    struct rk29fb_inf *inf = dev_get_drvdata(info->device);
    //struct fb_var_screeninfo *var1 = &info->var;
    struct fb_fix_screeninfo *fix1 = &info->fix;
    struct win0_par *par = info->par;

    u32 addr = 0;

	//fbprintk(">>>>>> %s : %s \n", __FILE__, __FUNCTION__);

	CHK_SUSPEND(inf);

    addr = fix1->smem_start + par->y_offset;

    //fbprintk("info->screen_base = %8x ; fix1->smem_len = %d , addr = %8x\n",(u32)info->screen_base, fix1->smem_len, addr);

    LcdWrReg(inf, WIN1_YRGB_MST, addr);
    LcdWrReg(inf, REG_CFG_DONE, 0x01);
	mcu_refresh(inf);

    return 0;
}

static int fb0_blank(int blank_mode, struct fb_info *info)
{
	struct rk29fb_inf *inf = dev_get_drvdata(info->device);

    if(inf->video_mode == 1)
    {
        win1_blank(blank_mode, info);
    }
    else
    {
        win0_blank(blank_mode, info);
    }
    return 0;
}

static int fb0_check_var(struct fb_var_screeninfo *var, struct fb_info *info)
{
    struct rk29fb_inf *inf = dev_get_drvdata(info->device);
    struct rk29fb_screen *screen = inf->cur_screen;
    u16 xpos = (var->nonstd>>8) & 0xfff;
    u16 ypos = (var->nonstd>>20) & 0xfff;
    u16 xlcd = screen->x_res;
    u16 ylcd = screen->y_res;

    //fbprintk(">>>>>> %s : %s\n", __FILE__, __FUNCTION__);

	CHK_SUSPEND(inf);

    if( 0==var->xres_virtual || 0==var->yres_virtual ||
        0==var->xres || 0==var->yres || var->xres<16 ||
        ((16!=var->bits_per_pixel)&&(32!=var->bits_per_pixel)) )
    {
        printk(">>>>>> win1fb_check_var fail 1!!! \n");
        printk(">>>>>> 0==%d || 0==%d ", var->xres_virtual,var->yres_virtual);
        printk("0==%d || 0==%d || %d<16 || ", var->xres,var->yres,var->xres<16);
        printk("bits_per_pixel=%d \n", var->bits_per_pixel);
        return -EINVAL;
    }

    if( (var->xoffset+var->xres)>var->xres_virtual ||
        (var->yoffset+var->yres)>var->yres_virtual )
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

    return 0;
}


static int fb0_set_par(struct fb_info *info)
{
    struct rk29fb_inf *inf = dev_get_drvdata(info->device);
    struct fb_var_screeninfo *var = &info->var;
    struct fb_fix_screeninfo *fix = &info->fix;
    struct rk29fb_screen *screen = inf->cur_screen;
    struct win0_par *par = info->par;

    //u8 format = 0;
    //u32 offset=0, addr=0, map_size=0, smem_len=0;
    u32 offset=0, smem_len=0;

    u16 xres_virtual = var->xres_virtual;      //virtual screen size

    u16 xpos_virtual = var->xoffset;           //visiable offset in virtual screen
    u16 ypos_virtual = var->yoffset;

    //u16 xpos = (screen->x_res - var->xres)/2;                 //visiable offset in panel
    //u16 ypos = (screen->y_res - var->yres)/2;
    //u16 xsize = screen->x_res;    //visiable size in panel
    //u16 ysize = screen->y_res;
    //u8 trspmode = TRSP_CLOSE;
    //u8 trspval = 0;

    //fbprintk(">>>>>> %s : %s\n", __FILE__, __FUNCTION__);

	CHK_SUSPEND(inf);

    if((inf->video_mode == 1)&&(screen->y_res < var->yres))ypos_virtual += (var->yres-screen->y_res);

    switch(var->bits_per_pixel)
    {
    case 16:    // rgb565
        par->format = 1;
        fix->line_length = 2 * xres_virtual;
        offset = (ypos_virtual*xres_virtual + xpos_virtual)*2;
        break;
    case 32:    // rgb888
    default:
        par->format = 0;
        fix->line_length = 4 * xres_virtual;
        offset = (ypos_virtual*xres_virtual + xpos_virtual)*4;
        break;
    }

    smem_len = fix->line_length * var->yres_virtual;
    //map_size = PAGE_ALIGN(smem_len);

    if (smem_len > fix->smem_len)     // buffer need realloc
    {
        printk("%s sorry!!! win1 buf is not enough\n",__FUNCTION__);
    }

    par->y_offset = offset;
    par->addr_seted = 1;
#if ANDROID_USE_THREE_BUFS
    if(0==new_frame_seted) {
        wq_condition = 0;
        wait_event_interruptible_timeout(wq, wq_condition, HZ/20);
    }
    new_frame_seted = 0;
#endif

    if(inf->video_mode == 1)
    {
        par->xpos = (screen->x_res >= var->xres)?((screen->x_res - var->xres)/2):0;              //visiable offset in panel
        par->ypos = (screen->y_res >= var->yres)?(screen->y_res - var->yres):0;
        par->xsize = var->xres;                                //visiable size in panel
        par->ysize = (screen->y_res >= var->yres) ? var->yres : screen->y_res;
        win1_set_par(info);
    }
    else
    {
        par->xpos = 0;
        par->ypos = 0;
        par->xsize = screen->x_res;
        par->ysize = screen->y_res;
        win0_set_par(info);
    }

    return 0;
}

static int fb0_pan_display(struct fb_var_screeninfo *var, struct fb_info *info)
{
    struct rk29fb_inf *inf = dev_get_drvdata(info->device);
    struct fb_var_screeninfo *var1 = &info->var;
    //struct fb_fix_screeninfo *fix1 = &info->fix;
    struct win0_par *par = info->par;

    u32 offset = 0;

	//fbprintk(">>>>>> %s : %s \n", __FILE__, __FUNCTION__);

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

//    par->y_offset = offset;

    if(inf->video_mode == 1)
    {
        win1_pan(info);
    }
    else
    {
        win0_pan(info);
    }
        // flush end when wq_condition=1 in mcu panel, but not in rgb panel
#if !ANDROID_USE_THREE_BUFS
    // flush end when wq_condition=1 in mcu panel, but not in rgb panel
    if(SCREEN_MCU == inf->cur_screen->type) {
        wait_event_interruptible_timeout(wq, wq_condition, HZ/20);
        wq_condition = 0;
    } else {
        wq_condition = 0;
        wait_event_interruptible_timeout(wq, wq_condition, HZ/20);
    }
#endif
    return 0;
}

static int fb0_ioctl(struct fb_info *info, unsigned int cmd, unsigned long arg)
{
    struct rk29fb_inf *inf = dev_get_drvdata(info->device);
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
   default:
        break;
    }
    return 0;
}

static int fb1_blank(int blank_mode, struct fb_info *info)
{
    win0_blank(blank_mode, info);
    return 0;
}

static int fb1_check_var(struct fb_var_screeninfo *var, struct fb_info *info)
{
    struct rk29fb_inf *inf = dev_get_drvdata(info->device);
    struct rk29fb_screen *screen = inf->cur_screen;

    u32 ScaleYRGBY=0x1000;
    u16 xpos = (var->nonstd>>8) & 0xfff;   //offset in panel
    u16 ypos = (var->nonstd>>20) & 0xfff;
    u16 xsize = (var->grayscale>>8) & 0xfff;   //visiable size in panel
    u16 ysize = (var->grayscale>>20) & 0xfff;
    u16 xlcd = screen->x_res;        //size of panel
    u16 ylcd = screen->y_res;
    u16 yres = 0;
#ifdef CONFIG_HDMI
	struct hdmi *hdmi = get_hdmi_struct(0);
#endif
    xpos = (xpos * screen->x_res) / inf->panel1_info.x_res;
    ypos = (ypos * screen->y_res) / inf->panel1_info.y_res;
    xsize = (xsize * screen->x_res) / inf->panel1_info.x_res;
    ysize = (ysize * screen->y_res) / inf->panel1_info.y_res;

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
        (xpos+xsize)>xlcd || (ypos+ysize)>ylcd  )
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
    case 4: // none
    case 5: // yuv444
        var->xres_virtual = (var->xres_virtual + 0x3) & (~0x3);
        var->xres = (var->xres + 0x3) & (~0x3);
        var->xoffset = (var->xoffset) & (~0x3);
        var->nonstd &= ~0xc0;   //not support I2P in this format
        break;
    default:
        printk(">>>>>> fb1 var->nonstd=%d is invalid! \n", var->nonstd);
        return -EINVAL;
    }

    yres = var->yres;

    ScaleYRGBY = CalScaleW0(yres, ysize);

    if((ScaleYRGBY>0x8000) || (ScaleYRGBY<0x200))
    {
        return -EINVAL;        // multiple of scale down or scale up can't exceed 8
    }
#ifdef CONFIG_HDMI
	if(inf->video_mode == 1) {
		if(hdmi_resolution_changed(hdmi,var->xres,var->yres, 1) == 1)
		{
			LcdMskReg(inf, DSP_CTRL1, m_BLACK_MODE,  v_BLACK_MODE(1));
    		LcdWrReg(inf, REG_CFG_DONE, 0x01);
			init_completion(&hdmi->complete);
			hdmi->wait = 1;
			wait_for_completion_interruptible_timeout(&hdmi->complete,
								msecs_to_jiffies(10000));
		}
	}
#endif
    return 0;
}

static int fb1_set_par(struct fb_info *info)
{
    struct rk29fb_inf *inf = dev_get_drvdata(info->device);
    struct rk29fb_screen *screen = inf->cur_screen;
    struct fb_var_screeninfo *var = &info->var;
    struct fb_fix_screeninfo *fix = &info->fix;
    struct win0_par *par = info->par;

    u8 format = 0;
    u32 cblen=0, crlen=0, map_size=0, smem_len=0;

	//u32 xact = var->xres;			    /* visible resolution		*/
	//u32 yact = var->yres;
	u32 xvir = var->xres_virtual;		/* virtual resolution		*/
	u32 yvir = var->yres_virtual;
	u32 xact_st = var->xoffset;			/* offset from virtual to visible */
	u32 yact_st = var->yoffset;			/* resolution			*/

    u16 xpos = (var->nonstd>>8) & 0xfff;      //visiable pos in panel
    u16 ypos = (var->nonstd>>20) & 0xfff;
    u16 xsize = (var->grayscale>>8) & 0xfff;  //visiable size in panel
    u16 ysize = (var->grayscale>>20) & 0xfff;

    //u32 ScaleYrgbX=0x1000,ScaleYrgbY=0x1000;
    //u32 ScaleCbrX=0x1000, ScaleCbrY=0x1000;

    u8 data_format = var->nonstd&0x0f;
   // u32 win0_en = var->reserved[2];
   // u32 y_addr = var->reserved[3];       //user alloc buf addr y
   // u32 uv_addr = var->reserved[4];

    fbprintk(">>>>>> %s : %s\n", __FILE__, __FUNCTION__);

	CHK_SUSPEND(inf);

    xpos = (xpos * screen->x_res) / inf->panel1_info.x_res;
    ypos = (ypos * screen->y_res) / inf->panel1_info.y_res;
    xsize = (xsize * screen->x_res) / inf->panel1_info.x_res;
    ysize = (ysize * screen->y_res) / inf->panel1_info.y_res;

	/* calculate y_offset,c_offset,line_length,cblen and crlen  */
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
        par->y_offset = yact_st*xvir + xact_st;
        par->c_offset = yact_st*xvir + xact_st;
        break;
    case 2: // yuv4200
        format = 3;
        fix->line_length = xvir;
        cblen = crlen = (xvir*yvir)/4;

        par->y_offset = yact_st*xvir + xact_st;
        par->c_offset = (yact_st/2)*xvir + xact_st;

        break;
    case 3: // yuv4201
        format = 4;
        fix->line_length = xvir;
        par->y_offset = (yact_st/2)*2*xvir + (xact_st)*2;
        par->c_offset = (yact_st/2)*xvir + xact_st;
        cblen = crlen = (xvir*yvir)/4;
        break;
    case 4: // none
    case 5: // yuv444
        format = 5;
        fix->line_length = xvir;
        par->y_offset = yact_st*xvir + xact_st;
        par->c_offset = yact_st*2*xvir + xact_st*2;
        cblen = crlen = (xvir*yvir);
        break;
    default:
        return -EINVAL;
    }

    smem_len = fix->line_length * yvir + cblen + crlen;
    map_size = PAGE_ALIGN(smem_len);

   // fix->smem_start = y_addr;
    fix->smem_len = smem_len;
  //  fix->mmio_start = uv_addr;

 //   par->addr_seted = ((-1==(int)y_addr) || (0==(int)y_addr) || (win0_en==0)) ? 0 : 1;
    fbprintk("buffer alloced by user fix->smem_start = %8x, fix->smem_len = %8x, fix->mmio_start = %8x \n", (u32)fix->smem_start, (u32)fix->smem_len, (u32)fix->mmio_start);

    par->format = format;
    par->xpos = xpos;
    par->ypos = ypos;
    par->xsize = xsize;
    par->ysize = ysize;
    win0_set_par(info);

    if ( wq_condition2 == 0 ) {
        wait_event_interruptible_timeout(wq,  wq_condition2, HZ/20);
    }
    wq_condition2 = 0;

    return 0;
}

static int fb1_pan_display(struct fb_var_screeninfo *var, struct fb_info *info)
{
    struct win0_par *par = info->par;
     // enable win0 after the win0 addr is seted

    win0_pan(info);
    par->par_seted = 1;
    return 0;
}

int fb1_open(struct fb_info *info, int user)
{
    struct rk29fb_inf *inf = dev_get_drvdata(info->device);
    struct win0_par *par = info->par;

    fbprintk(">>>>>> %s : %s \n", __FILE__, __FUNCTION__);

    par->par_seted = 0;
    par->addr_seted = 0;
    inf->video_mode = 1;
    wq_condition2 = 1;

    if(par->refcount) {
        printk(">>>>>> fb1 has opened! \n");
        return -EACCES;
    } else {
        par->refcount++;
        win0_blank(FB_BLANK_POWERDOWN, info);
        return 0;
    }
}

int fb1_release(struct fb_info *info, int user)
{
    struct rk29fb_inf *inf = dev_get_drvdata(info->device);
    struct win0_par *par = info->par;
	struct fb_var_screeninfo *var0 = &info->var;

    fbprintk(">>>>>> %s : %s \n", __FILE__, __FUNCTION__);

    if(par->refcount) {
        par->refcount--;
        inf->video_mode = 0;
        par->par_seted = 0;
        par->addr_seted = 0;
        win0_blank(FB_BLANK_POWERDOWN, info);
        win1_blank(FB_BLANK_POWERDOWN, info);
        // wait for lcdc stop access memory
        msleep(50);

        // unmap memory
        info->screen_base = 0;
        info->fix.smem_start = 0;
        info->fix.smem_len = 0;
		// clean the var param
		memset(var0, 0, sizeof(struct fb_var_screeninfo));
    }

    return 0;
}

static int fb1_ioctl(struct fb_info *info, unsigned int cmd, unsigned long arg)
{
    struct rk29fb_inf *inf = dev_get_drvdata(info->device);
    struct win0_par *par = info->par;
    struct fb_fix_screeninfo *fix0 = &info->fix;
    void __user *argp = (void __user *)arg;

	fbprintk(">>>>>> %s : %s \n", __FILE__, __FUNCTION__);
    fbprintk("win0fb_ioctl cmd = %8x, arg = %8x \n", (u32)cmd, (u32)arg);

	CHK_SUSPEND(inf);

    switch(cmd)
    {
    case FB1_IOCTL_GET_PANEL_SIZE:    //get panel size
        {
            u32 panel_size[2];
             if(inf->fb1->var.rotate == 270) {
                panel_size[0] = inf->panel1_info.y_res; //inf->cur_screen->y_res; change for hdmi video size
                panel_size[1] = inf->panel1_info.x_res;
            } else {
                panel_size[0] = inf->panel1_info.x_res;
                panel_size[1] = inf->panel1_info.y_res;
            }

            if(copy_to_user(argp, panel_size, 8))  return -EFAULT;
        }
        break;

    case FB1_IOCTL_SET_YUV_ADDR:    //set y&uv address to register direct
        {
            u32 yuv_phy[2];
            if (copy_from_user(yuv_phy, argp, 8))
			    return -EFAULT;

            fix0->smem_start = yuv_phy[0];
            fix0->mmio_start = yuv_phy[1];
            yuv_phy[0] += par->y_offset;
            yuv_phy[1] += par->c_offset;

            LcdWrReg(inf, WIN0_YRGB_MST, yuv_phy[0]);
            LcdWrReg(inf, WIN0_CBR_MST, yuv_phy[1]);
            // enable win0 after the win0 par is seted
            LcdMskReg(inf, SYS_CONFIG, m_W0_ENABLE, v_W0_ENABLE(par->par_seted && par->addr_seted));
            par->addr_seted = 1;
            LcdWrReg(inf, REG_CFG_DONE, 0x01);
            if(par->par_seted) {
                unsigned long flags;
#if 0
                if (par->mirror.c_offset) {
                    ret = wait_event_interruptible_timeout(par->wait,
                        (0 == par->mirror.c_offset), HZ/20);
                    if (ret <= 0)
                        break;
                }
#endif

                local_irq_save(flags);
                par->mirror.y_offset = yuv_phy[0];
                par->mirror.c_offset = yuv_phy[1];
                local_irq_restore(flags);

                mcu_refresh(inf);
                //printk("0x%.8x 0x%.8x mirror\n", par->mirror.y_offset, par->mirror.c_offset);
            }
        }
        break;

    case FB1_IOCTL_SET_ROTATE:    //change MCU panel scan direction
        fbprintk(">>>>>> change lcdc direction(%d) \n", (int)arg);
        return -1;
        break;
    case FB1_IOCTL_SET_WIN0_TOP:
        fbprintk(">>>>>> FB1_IOCTL_SET_WIN0_TOP %d\n",arg);
        LcdMskReg(inf, DSP_CTRL0, m_W0_ON_TOP, v_W0_ON_TOP(arg));
        LcdWrReg(inf, REG_CFG_DONE, 0x01);
        break;
    default:
        break;
    }
    return 0;
}

static struct fb_ops fb1_ops = {
	.owner		= THIS_MODULE,
	.fb_open    = fb1_open,
	.fb_release = fb1_release,
	.fb_check_var	= fb1_check_var,
	.fb_set_par	= fb1_set_par,
	.fb_blank	= fb1_blank,
    .fb_pan_display = fb1_pan_display,
    .fb_ioctl = fb1_ioctl,
	.fb_setcolreg	= fb_setcolreg,
	.fb_fillrect	= cfb_fillrect,
	.fb_copyarea	= cfb_copyarea,
	.fb_imageblit	= cfb_imageblit,
};

static struct fb_ops fb0_ops = {
	.owner		= THIS_MODULE,
	.fb_check_var	= fb0_check_var,
	.fb_set_par = fb0_set_par,
	.fb_blank   = fb0_blank,
	.fb_pan_display = fb0_pan_display,
    .fb_ioctl = fb0_ioctl,
	.fb_setcolreg	= fb_setcolreg,
	.fb_fillrect    = cfb_fillrect,
	.fb_copyarea	= cfb_copyarea,
	.fb_imageblit	= cfb_imageblit,
	//.fb_cursor      = rk29_set_cursor,
};

/*
enable: 1, switch to tv or hdmi; 0, switch to lcd
*/
int FB_Switch_Screen( struct rk29fb_screen *screen, u32 enable )
{
    struct rk29fb_inf *inf = platform_get_drvdata(g_pdev);
   // struct rk29fb_info *mach_info = g_pdev->dev.platform_data;

    memcpy(&inf->panel2_info, screen, sizeof( struct rk29fb_screen ));

    if(enable)inf->cur_screen = &inf->panel2_info;
    else inf->cur_screen = &inf->panel1_info;

    /* Black out, because some display device need clock to standby */
    //LcdMskReg(inf, DSP_CTRL_REG1, m_BLACK_OUT, v_BLACK_OUT(1));
   // LcdMskReg(inf, SYS_CONFIG, m_W0_ENABLE, v_W0_ENABLE(0));
   // LcdMskReg(inf, SYS_CONFIG, m_W1_ENABLE, v_W1_ENABLE(0));
    LcdMskReg(inf, DSP_CTRL1, m_BLACK_MODE,  v_BLACK_MODE(1));
    LcdWrReg(inf, REG_CFG_DONE, 0x01);
    msleep(20);

    if(inf->cur_screen->standby)    inf->cur_screen->standby(1);
    // operate the display_on pin to power down the lcd
    set_lcd_pin(g_pdev, (enable==0));

    load_screen(inf->fb0, 0);
	mcu_refresh(inf);

    fb1_set_par(inf->fb1);
    fb0_set_par(inf->fb0);
    LcdMskReg(inf, DSP_CTRL1, m_BLACK_MODE,  v_BLACK_MODE(0));
    LcdWrReg(inf, REG_CFG_DONE, 0x01);

    return 0;
}

static ssize_t dsp_win0_info_read(struct device *device,
			    struct device_attribute *attr, char *buf)
{
    //char * s = _buf;
    struct rk29fb_inf *inf = platform_get_drvdata(g_pdev);
    struct rk29fb_screen *screen = inf->cur_screen;

    u16 xpos=0,ypos=0,xsize=0,ysize=0;

    fbprintk("%s\n",__FUNCTION__);
    xpos = LcdRdReg(inf, WIN0_DSP_ST) & 0xffff;
    ypos = (LcdRdReg(inf, WIN0_DSP_ST)>>16) & 0xffff;

    xpos -= (screen->left_margin + screen->hsync_len);
    ypos -= (screen->upper_margin + screen->vsync_len);

    xsize=LcdRdReg(inf, WIN0_DSP_INFO)& 0xffff;
    ysize=(LcdRdReg(inf, WIN0_DSP_INFO)>>16) & 0xffff;
    fbprintk("%s %d , %d, %d ,%d\n",__FUNCTION__,xpos,ypos,xsize,ysize);

    return snprintf(buf, PAGE_SIZE, "%d,%d,%d,%d\n", xpos,ypos,xsize,ysize);
}
static ssize_t dsp_win0_info_write(struct device *device,
			   struct device_attribute *attr, char *buf)
{
     printk("%s\n",__FUNCTION__);
     printk("%s %x \n",__FUNCTION__,*buf);
     return 0;
}

static DRIVER_ATTR(dsp_win0_info,  S_IRUGO|S_IWUSR, dsp_win0_info_read, dsp_win0_info_write);

static irqreturn_t rk29fb_irq(int irq, void *dev_id)
{
	struct platform_device *pdev = (struct platform_device*)dev_id;
    struct rk29fb_inf *inf = platform_get_drvdata(pdev);
    struct win0_par *par = (struct win0_par *)inf->fb1->par;
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
#if ANDROID_USE_THREE_BUFS
    new_frame_seted = 1;
#endif

    if(waitqueue_active(&par->wait)) {
        if (par->mirror.c_offset == 0)
            printk("error: no new buffer to display\n");

        par->done.y_offset = par->displ.y_offset;
        par->done.c_offset = par->displ.c_offset;
        par->displ.y_offset = par->mirror.y_offset;
        par->displ.c_offset = par->mirror.c_offset;
        par->mirror.y_offset = 0;
        par->mirror.c_offset = 0;

        //printk("0x%.8x 0x%.8x displaying\n", par->displ.y_offset, par->displ.c_offset);
        //printk("0x%.8x 0x%.8x done\n", par->done.y_offset, par->done.c_offset);
        wake_up_interruptible(&par->wait);
    }

    wq_condition2 = 1;
	wq_condition = 1;
 	wake_up_interruptible(&wq);

	return IRQ_HANDLED;
}

#ifdef CONFIG_HAS_EARLYSUSPEND

struct suspend_info {
	struct early_suspend early_suspend;
	struct rk29fb_inf *inf;
};

static void rk29fb_suspend(struct early_suspend *h)
{
	struct suspend_info *info = container_of(h, struct suspend_info,
						early_suspend);

    struct rk29fb_inf *inf = info->inf;

    fbprintk(">>>>>> %s : %s\n", __FILE__, __FUNCTION__);

    if(!inf) {
        printk("inf==0, rk29fb_suspend fail! \n");
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
        clk_disable(inf->aclk_ddr_lcdc);
        clk_disable(inf->aclk_disp_matrix);
        clk_disable(inf->hclk_cpu_display);
        clk_disable(inf->clk);
    	if (inf->dclk){
            clk_disable(inf->dclk);
        }
        if(inf->clk){
            clk_disable(inf->aclk);
        }
       // pmu_set_power_domain(PD_DISPLAY, 0);
		inf->in_suspend = 1;
	}

}

static void rk29fb_resume(struct early_suspend *h)
{
	struct suspend_info *info = container_of(h, struct suspend_info,
					early_suspend);

    struct rk29fb_inf *inf = info->inf;

    fbprintk(">>>>>> %s : %s\n", __FILE__, __FUNCTION__);
    if(!inf) {
        printk("inf==0, rk29fb_resume fail! \n");
        return ;
    }

	if(inf->in_suspend)
	{
	    inf->in_suspend = 0;
       // pmu_set_power_domain(PD_DISPLAY, 1);
    	fbprintk(">>>>>> enable the lcdc clk! \n");
        clk_enable(inf->aclk_ddr_lcdc);
        clk_enable(inf->aclk_disp_matrix);
        clk_enable(inf->hclk_cpu_display);
        clk_enable(inf->clk);
        if (inf->dclk){
            clk_enable(inf->dclk);
        }
        if(inf->clk){
            clk_enable(inf->aclk);
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
	memcpy((u8*)inf->preg, (u8*)&inf->regbak, 0xa4);  //resume reg
}

static struct suspend_info suspend_info = {
	.early_suspend.suspend = rk29fb_suspend,
	.early_suspend.resume = rk29fb_resume,
	.early_suspend.level = EARLY_SUSPEND_LEVEL_DISABLE_FB,
};
#endif

static int __init rk29fb_probe (struct platform_device *pdev)
{
    struct rk29fb_inf *inf = NULL;
    struct resource *res = NULL;
    struct resource *mem = NULL;
    struct rk29fb_info *mach_info = NULL;
    struct rk29fb_screen *screen = NULL;
    struct win0_par* par = NULL;
	int irq = 0;
    int ret = 0;

    fbprintk(">>>>>> %s : %s\n", __FILE__, __FUNCTION__);

    /* Malloc rk29fb_inf and set it to pdev for drvdata */
    fbprintk(">> Malloc rk29fb_inf and set it to pdev for drvdata \n");
    inf = kmalloc(sizeof(struct rk29fb_inf), GFP_KERNEL);
    if(!inf)
    {
        dev_err(&pdev->dev, ">> inf kmalloc fail!");
        ret = -ENOMEM;
		goto release_drvdata;
    }
    memset(inf, 0, sizeof(struct rk29fb_inf));
	platform_set_drvdata(pdev, inf);

    mach_info = pdev->dev.platform_data;
    /* Fill screen info and set current screen */
    fbprintk(">> Fill screen info and set current screen \n");
    set_lcd_info(&inf->panel1_info, mach_info->lcd_info);
    inf->cur_screen = &inf->panel1_info;
    screen = inf->cur_screen;
    if(SCREEN_NULL==screen->type)
    {
        dev_err(&pdev->dev, ">> Please select a display device! \n");
        ret = -EINVAL;
		goto release_drvdata;
    }

    /* get virtual basic address of lcdc register */
    fbprintk(">> get virtual basic address of lcdc register \n");
    res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "lcdc reg");
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
   	inf->fb0 = framebuffer_alloc(sizeof(struct win0_par), &pdev->dev);
    if(!inf->fb0)
    {
        dev_err(&pdev->dev, ">> fb0 framebuffer_alloc fail!");
		inf->fb0 = NULL;
        ret = -ENOMEM;
		goto release_win1fb;
    }

    par = (struct win0_par*)inf->fb0->par;
    strcpy(inf->fb0->fix.id, "fb0");
    inf->fb0->fix.type        = FB_TYPE_PACKED_PIXELS;
    inf->fb0->fix.type_aux    = 0;
    inf->fb0->fix.xpanstep    = 1;
    inf->fb0->fix.ypanstep    = 1;
    inf->fb0->fix.ywrapstep   = 0;
    inf->fb0->fix.accel       = FB_ACCEL_NONE;
    inf->fb0->fix.visual      = FB_VISUAL_TRUECOLOR;
    inf->fb0->fix.smem_len    = 0;
    inf->fb0->fix.line_length = 0;
    inf->fb0->fix.smem_start  = 0;

    inf->fb0->var.xres = screen->x_res;
    inf->fb0->var.yres = screen->y_res;
    inf->fb0->var.bits_per_pixel = 16;
    inf->fb0->var.xres_virtual = screen->x_res;
    inf->fb0->var.yres_virtual = screen->y_res;
    inf->fb0->var.width = screen->width;
    inf->fb0->var.height = screen->height;
    inf->fb0->var.pixclock = screen->pixclock;
    inf->fb0->var.left_margin = screen->left_margin;
    inf->fb0->var.right_margin = screen->right_margin;
    inf->fb0->var.upper_margin = screen->upper_margin;
    inf->fb0->var.lower_margin = screen->lower_margin;
    inf->fb0->var.vsync_len = screen->vsync_len;
    inf->fb0->var.hsync_len = screen->hsync_len;
    inf->fb0->var.red    = def_rgb_16.red;
    inf->fb0->var.green  = def_rgb_16.green;
    inf->fb0->var.blue   = def_rgb_16.blue;
    inf->fb0->var.transp = def_rgb_16.transp;

    inf->fb0->var.nonstd      = 0;  //win1 format & ypos & xpos (ypos<<20 + xpos<<8 + format)
    inf->fb0->var.grayscale   = 0;  //win1 transprent mode & value(mode<<8 + value)
    inf->fb0->var.activate    = FB_ACTIVATE_NOW;
    inf->fb0->var.accel_flags = 0;
    inf->fb0->var.vmode       = FB_VMODE_NONINTERLACED;

    inf->fb0->fbops           = &fb0_ops;
    inf->fb0->flags           = FBINFO_FLAG_DEFAULT;
    inf->fb0->pseudo_palette  = par->pseudo_pal;
    inf->fb0->screen_base     = 0;

    memset(par, 0, sizeof(struct win0_par));

	ret = fb_alloc_cmap(&inf->fb0->cmap, 256, 0);
	if (ret < 0)
		goto release_cmap;

    /* alloc win1 buf */
    res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "win1 buf");
    if (res == NULL)
    {
        dev_err(&pdev->dev, "failed to get win1 memory \n");
        ret = -ENOENT;
        goto release_win1fb;
    }
    inf->fb0->fix.smem_start = res->start;
    inf->fb0->fix.smem_len = res->end - res->start;
    inf->fb0->screen_base = ioremap(res->start, inf->fb0->fix.smem_len);
    memset(inf->fb0->screen_base, 0, inf->fb0->fix.smem_len);

    /* Prepare win0 info */
    fbprintk(">> Prepare win0 info \n");
    inf->fb1 = framebuffer_alloc(sizeof(struct win0_par), &pdev->dev);
    if(!inf->fb1)
    {
        dev_err(&pdev->dev, ">> fb1 framebuffer_alloc fail!");
		inf->fb1 = NULL;
		ret = -ENOMEM;
		goto release_win0fb;
    }

    par = (struct win0_par*)inf->fb1->par;

    strcpy(inf->fb1->fix.id, "fb1");
	inf->fb1->fix.type	      = FB_TYPE_PACKED_PIXELS;
	inf->fb1->fix.type_aux    = 0;
	inf->fb1->fix.xpanstep    = 1;
	inf->fb1->fix.ypanstep    = 1;
	inf->fb1->fix.ywrapstep   = 0;
	inf->fb1->fix.accel       = FB_ACCEL_NONE;
    inf->fb1->fix.visual      = FB_VISUAL_TRUECOLOR;
    inf->fb1->fix.smem_len    = 0;
    inf->fb1->fix.line_length = 0;
    inf->fb1->fix.smem_start  = 0;

    inf->fb1->var.xres = screen->x_res;
    inf->fb1->var.yres = screen->y_res;
    inf->fb1->var.bits_per_pixel = 16;
    inf->fb1->var.xres_virtual = screen->x_res;
    inf->fb1->var.yres_virtual = screen->y_res;
    inf->fb1->var.width = screen->width;
    inf->fb1->var.height = screen->height;
    inf->fb1->var.pixclock = screen->pixclock;
    inf->fb1->var.left_margin = screen->left_margin;
    inf->fb1->var.right_margin = screen->right_margin;
    inf->fb1->var.upper_margin = screen->upper_margin;
    inf->fb1->var.lower_margin = screen->lower_margin;
    inf->fb1->var.vsync_len = screen->vsync_len;
    inf->fb1->var.hsync_len = screen->hsync_len;
    inf->fb1->var.red    = def_rgb_16.red;
    inf->fb1->var.green  = def_rgb_16.green;
    inf->fb1->var.blue   = def_rgb_16.blue;
    inf->fb1->var.transp = def_rgb_16.transp;

    inf->fb1->var.nonstd      = 0;  //win0 format & ypos & xpos (ypos<<20 + xpos<<8 + format)
    inf->fb1->var.grayscale   = ((inf->fb1->var.yres<<20)&0xfff00000) + ((inf->fb1->var.xres<<8)&0xfff00);//win0 xsize & ysize
    inf->fb1->var.activate    = FB_ACTIVATE_NOW;
    inf->fb1->var.accel_flags = 0;
    inf->fb1->var.vmode       = FB_VMODE_NONINTERLACED;

    inf->fb1->fbops           = &fb1_ops;
	inf->fb1->flags		      = FBINFO_FLAG_DEFAULT;
	inf->fb1->pseudo_palette  = par->pseudo_pal;
	inf->fb1->screen_base     = 0;

    memset(par, 0, sizeof(struct win0_par));

	init_waitqueue_head(&par->wait);

 	/* Init all lcdc and lcd before register_framebuffer. */
 	/* because after register_framebuffer, the win1fb_check_par and winfb_set_par execute immediately */
 	fbprintk(">> Init all lcdc and lcd before register_framebuffer \n");
    init_lcdc(inf->fb0);

  #ifdef CONFIG_CPU_FREQ
   // inf->freq_transition.notifier_call = rk29fb_freq_transition;
   // cpufreq_register_notifier(&inf->freq_transition, CPUFREQ_TRANSITION_NOTIFIER);
  #endif
	fbprintk("got clock\n");

	if(mach_info)
    {
        struct rk29_fb_setting_info fb_setting;
        if( OUT_P888==inf->cur_screen->face )     // set lcdc iomux
        {
            fb_setting.data_num = 24;
        }
        else if(OUT_P666 == inf->cur_screen->face )
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
        if( inf->cur_screen->mcu_usefmk )
            fb_setting.mcu_fmk_en =1;
        mach_info->io_init(&fb_setting);
    }

	set_lcd_pin(pdev, 1);
	mdelay(10);
	g_pdev = pdev;
	inf->mcu_usetimer = 1;
    inf->mcu_fmksync = 0;
	load_screen(inf->fb0, 1);

    /* Register framebuffer(fb0 & fb1) */
    fbprintk(">> Register framebuffer(fb0) \n");
    ret = register_framebuffer(inf->fb0);
    if(ret<0)
    {
        printk(">> fb0 register_framebuffer fail!\n");
        ret = -EINVAL;
		goto release_win0fb;
    }
    fbprintk(">> Register framebuffer(fb1) \n");

    ret = register_framebuffer(inf->fb1);
    if(ret<0)
    {
        printk(">> fb1 register_framebuffer fail!\n");
        ret = -EINVAL;
		goto unregister_win1fb;
    }

    ret = device_create_file(inf->fb1->dev, &driver_attr_dsp_win0_info);
    if(ret)
    {
        printk(">> fb1 dsp win0 info device_create_file err\n");
        ret = -EINVAL;
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
    ret = request_irq(irq, rk29fb_irq, IRQF_DISABLED, pdev->name, pdev);
    if (ret) {
        dev_err(&pdev->dev, "cannot get irq %d - err %d\n", irq, ret);
        ret = -EBUSY;
        goto release_irq;
    }

    if( inf->cur_screen->mcu_usefmk && (mach_info->mcu_fmk_pin != -1) )
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
    unregister_framebuffer(inf->fb0);
release_win0fb:
	if(inf->fb1)
		framebuffer_release(inf->fb1);
	inf->fb1 = NULL;
release_cmap:
    if(&inf->fb0->cmap)
        fb_dealloc_cmap(&inf->fb0->cmap);
release_win1fb:
	if(inf->fb0)
		framebuffer_release(inf->fb0);
	inf->fb0 = NULL;
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

static int rk29fb_remove(struct platform_device *pdev)
{
    struct rk29fb_inf *inf = platform_get_drvdata(pdev);
    struct fb_info *info = NULL;
	//pm_message_t msg;
    struct rk29fb_info *mach_info = NULL;
    int irq = 0;

	fbprintk(">>>>>> %s : %s\n", __FILE__, __FUNCTION__);

    if(!inf) {
        printk("inf==0, rk29_fb_remove fail! \n");
        return -EINVAL;
    }
    driver_remove_file(inf->fb1->dev, &driver_attr_dsp_win0_info);

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
    if(inf->fb1)
        fb1_blank(FB_BLANK_POWERDOWN, inf->fb1);
    if(inf->fb0)
        fb0_blank(FB_BLANK_POWERDOWN, inf->fb0);

	// suspend the lcdc
	//rk29fb_suspend(pdev, msg);
    // unmap memory and release framebuffer
    if(inf->fb1) {
        info = inf->fb1;
        if (info->screen_base) {
	        //dma_free_writecombine(NULL, PAGE_ALIGN(info->fix.smem_len),info->screen_base, info->fix.smem_start);
	        info->screen_base = 0;
	        info->fix.smem_start = 0;
	        info->fix.smem_len = 0;
        }
        unregister_framebuffer(inf->fb1);
        framebuffer_release(inf->fb1);
        inf->fb1 = NULL;
    }
    if(inf->fb0) {
        info = inf->fb0;
        if (info->screen_base) {
	    //    dma_free_writecombine(NULL, PAGE_ALIGN(info->fix.smem_len),info->screen_base, info->fix.smem_start);
	        info->screen_base = 0;
	        info->fix.smem_start = 0;
	        info->fix.smem_len = 0;
        }
        unregister_framebuffer(inf->fb0);
        framebuffer_release(inf->fb0);
        inf->fb0 = NULL;
    }

  #ifdef CONFIG_CPU_FREQ
   // cpufreq_unregister_notifier(&inf->freq_transition, CPUFREQ_TRANSITION_NOTIFIER);
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

static void rk29fb_shutdown(struct platform_device *pdev)
{
    struct rk29fb_inf *inf = platform_get_drvdata(pdev);
    mdelay(300);
	//printk("----------------------------rk29fb_shutdown----------------------------\n");
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

static struct platform_driver rk29fb_driver = {
	.probe		= rk29fb_probe,
	.remove		= rk29fb_remove,
	.driver		= {
		.name	= "rk29-fb",
		.owner	= THIS_MODULE,
	},
	.shutdown   = rk29fb_shutdown,
};

static int __init rk29fb_init(void)
{
    return platform_driver_register(&rk29fb_driver);
}

static void __exit rk29fb_exit(void)
{
    platform_driver_unregister(&rk29fb_driver);
}

//subsys_initcall(rk29fb_init);

module_init(rk29fb_init);
module_exit(rk29fb_exit);


MODULE_AUTHOR("  zyw@rock-chips.com");
MODULE_DESCRIPTION("Driver for rk29 fb device");
MODULE_LICENSE("GPL");


