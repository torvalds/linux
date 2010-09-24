/* arch/arm/mach-rockchip/rk28_backlight.c
 *
 * Copyright (C) 2009 Rockchip Corporation.
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

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/debugfs.h>
#include <linux/backlight.h>
#include <linux/fb.h>
#include <linux/cpufreq.h>
#include <linux/clk.h>

#include <linux/earlysuspend.h>
#include <asm/io.h>
//#include <mach/typedef.h>
#include <mach/iomux.h>
#include <mach/gpio.h>
#include <mach/rk2818_iomap.h>
#include <mach/board.h>

#include "rk2818_backlight.h"

//#define RK28_PRINT 
//#include <mach/rk2818_debug.h>

/*
 * Debug
 */
#if 0
#define DBG(x...)	printk(KERN_INFO x)
#else
#define DBG(x...)
#endif


#define write_pwm_reg(id, addr, val)        __raw_writel(val, addr+(RK2818_PWM_BASE+id*0x10)) 
#define read_pwm_reg(id, addr)              __raw_readl(addr+(RK2818_PWM_BASE+id*0x10))    
#define mask_pwm_reg(id, addr, msk, val)    write_dma_reg(id, addr, (val)|((~(msk))&read_dma_reg(id, addr)))

static struct clk *pwm_clk;
static unsigned long pwm_clk_rate;
static struct backlight_device *rk2818_bl;
static int suspend_flag = 0;
#define BACKLIGHT_SEE_MINVALUE	52

static s32 rk2818_bl_update_status(struct backlight_device *bl)
{
    u32 divh,div_total;
    struct rk2818_bl_info *rk2818_bl_info = bl->dev.parent->platform_data;
    u32 id = rk2818_bl_info->pwm_id;
    u32 ref = rk2818_bl_info->bl_ref;

    if (suspend_flag)
        return 0;
    
    div_total = read_pwm_reg(id, PWM_REG_LRC);
    if (ref) {

        divh = div_total*(bl->props.brightness)/BL_STEP;
	 DBG(">>>%s-->%d   bl->props.brightness == %d, div_total == %d , divh == %d\n",__FUNCTION__,__LINE__,bl->props.brightness, div_total, divh);
    } else {
     	 DBG(">>>%s-->%d   bl->props.brightness == %d\n",__FUNCTION__,__LINE__,bl->props.brightness);
	 if(bl->props.brightness < BACKLIGHT_SEE_MINVALUE)	/*avoid can't view screen when close backlight*/
	 	bl->props.brightness = BACKLIGHT_SEE_MINVALUE;
        divh = div_total*(BL_STEP-bl->props.brightness)/BL_STEP;
    }
    write_pwm_reg(id, PWM_REG_HRC, divh);
    DBG("%s::========================================\n",__func__);
    return 0;
}

static s32 rk2818_bl_get_brightness(struct backlight_device *bl)
{
    u32 divh,div_total;
    struct rk2818_bl_info *rk2818_bl_info = bl->dev.parent->platform_data;
    u32 id = rk2818_bl_info->pwm_id;
    u32 ref = rk2818_bl_info->bl_ref;
    
    div_total = read_pwm_reg(id, PWM_REG_LRC);
    divh = read_pwm_reg(id, PWM_REG_HRC);

	DBG("%s::======================================== div_total: %d\n",__func__,div_total);

    if (ref) {
        return BL_STEP*divh/div_total;
    } else {
        return BL_STEP-(BL_STEP*divh/div_total);
    }
}

static struct backlight_ops rk2818_bl_ops = {
	.update_status = rk2818_bl_update_status,
	.get_brightness = rk2818_bl_get_brightness,
};

#ifdef CONFIG_CPU_FREQ
static int rk2818_bl_change_clk(struct notifier_block *nb, unsigned long val, void *data)
{
    struct rk2818_bl_info *rk2818_bl_info;
    u32 id;
    u32 divl, divh, tmp;
    u32 div_total;
	int is_suspended = suspend_flag;

	if (!rk2818_bl) {
		DBG(KERN_CRIT "%s: backlight device does not exist\n", __func__);
		return -ENODEV;		
    }
    
	switch (val) {
    case CPUFREQ_PRECHANGE:        
         break;
    case CPUFREQ_POSTCHANGE:
		if (clk_get_rate(pwm_clk) == pwm_clk_rate)
			break;
		pwm_clk_rate = clk_get_rate(pwm_clk);
         rk2818_bl_info = rk2818_bl->dev.parent->platform_data;
         id = rk2818_bl_info->pwm_id;
     

         
         divl = read_pwm_reg(id, PWM_REG_LRC);
         divh = read_pwm_reg(id, PWM_REG_HRC);
     
		tmp = pwm_clk_rate / PWM_APB_PRE_DIV;
         tmp >>= (1 + (PWM_DIV >> 9));

         
         div_total = (tmp) ? tmp : 1;
         tmp = div_total*divh/divl;
         
		if (!is_suspended)
			clk_disable(pwm_clk);
         write_pwm_reg(id, PWM_REG_LRC, div_total);
         write_pwm_reg(id, PWM_REG_HRC, tmp);    
         write_pwm_reg(id, PWM_REG_CNTR, 0);  
		if (!is_suspended)
			clk_enable(pwm_clk);
         break;
    }
   
    return 0;
}   
#endif
static void rk2818_delaybacklight_timer(unsigned long data)
{
	struct rk2818_bl_info *rk2818_bl_info = (struct rk2818_bl_info *)data;
	u32 id, brightness;
    	u32 div_total, divh;
	clk_enable(pwm_clk);
	id = rk2818_bl_info->pwm_id;
    brightness = rk2818_bl->props.brightness;
    div_total = read_pwm_reg(id, PWM_REG_LRC);
    if (rk2818_bl_info->bl_ref) {
        divh = div_total*(brightness)/BL_STEP;
    } else {
        divh = div_total*(BL_STEP-brightness)/BL_STEP;
    }
    write_pwm_reg(id, PWM_REG_HRC, divh);
    suspend_flag = 0;
    DBG("%s: ======================== \n",__func__); 
}

#ifdef CONFIG_HAS_EARLYSUSPEND
static void rk2818_bl_suspend(struct early_suspend *h)
{
    struct rk2818_bl_info *rk2818_bl_info;
    u32 id;
    u32 div_total, divh;
    
    rk2818_bl_info = rk2818_bl->dev.parent->platform_data;

    id = rk2818_bl_info->pwm_id;

    div_total = read_pwm_reg(id, PWM_REG_LRC);
    
    if(rk2818_bl_info->bl_ref) {
        divh = 0;
    } else {
        divh = div_total;
    }

    DBG("%s: ==========  suspend  =============== \n",__func__); 

    write_pwm_reg(id, PWM_REG_HRC, divh);
	clk_disable(pwm_clk);

    suspend_flag = 1;
    
    DBG("%s: ========================= \n",__func__); 
}


static void rk2818_bl_resume(struct early_suspend *h)
{
    struct rk2818_bl_info *rk2818_bl_info;
   // u32 id, brightness;
    //u32 div_total, divh;
    DBG(">>>>>> %s : %s\n", __FILE__, __FUNCTION__);
    rk2818_bl_info = rk2818_bl->dev.parent->platform_data;
	
	rk2818_bl_info->timer.expires  = jiffies + 30;
	add_timer(&rk2818_bl_info->timer);
	#if 0
    id = rk28_bl_info->pwm_id;
    brightness = rk28_bl->props.brightness;
    
    div_total = read_pwm_reg(id, PWM_REG_LRC);
    if (rk28_bl_info->bl_ref) {
        divh = div_total*(brightness)/BL_STEP;
    } else {
        divh = div_total*(BL_STEP-brightness)/BL_STEP;
    }
    //mdelay(100);
    write_pwm_reg(id, PWM_REG_HRC, divh);

    suspend_flag = 0;

    rk28printk("%s: ======================== \n",__func__); 
	#endif 
}

static struct early_suspend bl_early_suspend;
#endif

static int rk2818_backlight_probe(struct platform_device *pdev)
{		
    int ret = 0;
    struct rk2818_bl_info *rk2818_bl_info = pdev->dev.platform_data;
    u32 id  =  rk2818_bl_info->pwm_id;
    u32 divh, div_total;
 
    DBG("%s::=======================================\n",__func__);

    if (rk2818_bl) {
        DBG(KERN_CRIT "%s: backlight device register has existed \n",
               __func__); 
		return -EEXIST;		
    }
    
	rk2818_bl = backlight_device_register("rk28_bl", &pdev->dev, NULL, &rk2818_bl_ops);
	if (!rk2818_bl) {
        DBG(KERN_CRIT "%s: backlight device register error\n",
               __func__); 
		return -ENODEV;		
	}
	
	if (!pwm_clk)
		pwm_clk = clk_get(NULL, "pwm");
	if (!pwm_clk || IS_ERR(pwm_clk)) {
		printk(KERN_ERR "failed to get pwm clock source\n");
		return -ENODEV;	
	}
	pwm_clk_rate = clk_get_rate(pwm_clk);
	div_total = pwm_clk_rate / PWM_APB_PRE_DIV;

	
    
    div_total >>= (1 + (PWM_DIV >> 9));
    div_total = (div_total) ? div_total : 1;
    
   /// if(rk2818_bl_info->bl_ref) {
    ///    divh = 0;
   /// } else {
        divh = div_total / 2;
   // }

    /*init timer to dispose workqueue */
    setup_timer(&rk2818_bl_info->timer, rk2818_delaybacklight_timer, (unsigned long)rk2818_bl_info);

#ifdef CONFIG_CPU_FREQ
    rk2818_bl_info->freq_transition.notifier_call = rk2818_bl_change_clk;   
    cpufreq_register_notifier(&rk2818_bl_info->freq_transition, CPUFREQ_TRANSITION_NOTIFIER);
#endif
        
//	clk_disable(pwm_clk);
    write_pwm_reg(id, PWM_REG_CTRL, PWM_DIV|PWM_RESET);
    write_pwm_reg(id, PWM_REG_LRC, div_total);
    write_pwm_reg(id, PWM_REG_HRC, divh);
    write_pwm_reg(id, PWM_REG_CNTR, 0x0);
    write_pwm_reg(id, PWM_REG_CTRL, PWM_DIV|PWM_ENABLE|PWM_TIME_EN);
	clk_enable(pwm_clk);
   
	rk2818_bl->props.power = FB_BLANK_UNBLANK;
	rk2818_bl->props.fb_blank = FB_BLANK_UNBLANK;
	rk2818_bl->props.max_brightness = BL_STEP;
	rk2818_bl->props.brightness = rk2818_bl_get_brightness(rk2818_bl);

#ifdef CONFIG_HAS_EARLYSUSPEND
    bl_early_suspend.suspend = rk2818_bl_suspend;
    bl_early_suspend.resume = rk2818_bl_resume;
    bl_early_suspend.level = ~0x0;
    register_early_suspend(&bl_early_suspend);
#endif

    if (rk2818_bl_info && rk2818_bl_info->io_init) {
        rk2818_bl_info->io_init();
    }

    return ret;
}

static int rk2818_backlight_remove(struct platform_device *pdev)
{		
    struct rk2818_bl_info *rk2818_bl_info = pdev->dev.platform_data;
    
	if (rk2818_bl) {
		backlight_device_unregister(rk2818_bl);
#ifdef CONFIG_HAS_EARLYSUSPEND
        unregister_early_suspend(&bl_early_suspend);
#endif       
#ifdef CONFIG_CPU_FREQ
        cpufreq_unregister_notifier(&rk2818_bl_info->freq_transition, CPUFREQ_TRANSITION_NOTIFIER);
#endif
		clk_disable(pwm_clk);
		clk_put(pwm_clk);
        if (rk2818_bl_info && rk2818_bl_info->io_deinit) {
            rk2818_bl_info->io_deinit();
        }
        return 0;
    } else {
        DBG(KERN_CRIT "%s: no backlight device has registered\n",
               __func__); 
        return -ENODEV;      
    }
}
static void rk2818_backlight_shutdown(struct platform_device *pdev)
{

   u32 divh,div_total;
    struct rk2818_bl_info *rk2818_bl_info = pdev->dev.platform_data;
    u32 id = rk2818_bl_info->pwm_id;
    u32  brightness;
	brightness = rk2818_bl->props.brightness; 
	brightness/=2;
	div_total = read_pwm_reg(id, PWM_REG_LRC);   
	
	if (rk2818_bl_info->bl_ref) {
			divh = div_total*(brightness)/BL_STEP;
	} else {
			divh = div_total*(BL_STEP-brightness)/BL_STEP;
	}
	write_pwm_reg(id, PWM_REG_HRC, divh);
	//printk("divh=%d\n",divh);
	 mdelay(100);
	 
	brightness/=2;
	if (rk2818_bl_info->bl_ref) {
	divh = div_total*(brightness)/BL_STEP;
	} else {
	divh = div_total*(BL_STEP-brightness)/BL_STEP;
	}
	//printk("------------rk28_backlight_shutdown  mdelay----------------------------\n");
	write_pwm_reg(id, PWM_REG_HRC, divh); 
	mdelay(100);
	/*set  PF1=1 PF2=1 for close backlight*/	

    if(rk2818_bl_info->bl_ref) {
        divh = 0;
    } else {
        divh = div_total;
    }
    write_pwm_reg(id, PWM_REG_HRC, divh);
  
}

static struct platform_driver rk2818_backlight_driver = {
	.probe	= rk2818_backlight_probe,
	.remove = rk2818_backlight_remove,
	.driver	= {
		.name	= "rk2818_backlight",
		.owner	= THIS_MODULE,
	},
	.shutdown=rk2818_backlight_shutdown,
};


static int __init rk2818_backlight_init(void)
{
	DBG("%s::========================================\n",__func__);
	platform_driver_register(&rk2818_backlight_driver);
	return 0;
}
//rootfs_initcall(rk2818_backlight_init);

late_initcall(rk2818_backlight_init);
//module_init(rk28_backlight_init);
