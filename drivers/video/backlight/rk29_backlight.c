/* drivers/video/backlight/rk29_backlight.c
 *
 * Copyright (C) 2009-2011 Rockchip Corporation.
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
#include <linux/backlight.h>
#include <linux/fb.h>
#include <linux/clk.h>

#include <linux/earlysuspend.h>
#include <asm/io.h>
#include <mach/board.h>

#include "rk2818_backlight.h"

/*
 * Debug
 */
#if 0
#define DBG(x...)	printk(KERN_INFO x)
#else
#define DBG(x...)
#endif

#if defined(CONFIG_ARCH_RK30)
#define write_pwm_reg(id, addr, val)        __raw_writel(val, addr+(RK30_PWM01_BASE+(id>>1)*0x20000)+id*0x10)
#define read_pwm_reg(id, addr)              __raw_readl(addr+(RK30_PWM01_BASE+(id>>1)*0x20000+id*0x10))

#elif defined(CONFIG_ARCH_RK2928)
#define write_pwm_reg(id, addr, val)        __raw_writel(val, addr+(RK2928_PWM_BASE+id*0x10))
#define read_pwm_reg(id, addr)              __raw_readl(addr+(RK2928_PWM_BASE+id*0x10))

#elif defined(CONFIG_ARCH_RK29)
#define write_pwm_reg(id, addr, val)        __raw_writel(val, addr+(RK29_PWM_BASE+id*0x10))
#define read_pwm_reg(id, addr)              __raw_readl(addr+(RK29_PWM_BASE+id*0x10))    
#endif

static struct clk *pwm_clk;
static struct backlight_device *rk29_bl;
static int suspend_flag = 0;


int convertint(char s[])  
{  
    int i;  
    int n = 0;  
    for (i = 0; s[i] >= '0' && s[i] <= '9'; ++i)  
    {  
        n = 10 * n + (s[i] - '0');  
    }  
    return n;  
} 

static ssize_t backlight_write(struct device *dev,
		struct device_attribute *attr, char *buf)
{
   
	struct rk29_bl_info *rk29_bl_info = bl_get_data(rk29_bl);
	int number;

	number = convertint(buf);
	
	rk29_bl_info->min_brightness=number;
	return 0;
}


static ssize_t backlight_read(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct rk29_bl_info *rk29_bl_info = bl_get_data(rk29_bl);

	DBG("rk29_bl_info->min_brightness=%d\n",rk29_bl_info->min_brightness);
}
static DEVICE_ATTR(rk29backlight, 0660, backlight_read, backlight_write);

static int rk29_bl_update_status(struct backlight_device *bl)
{
	u32 divh,div_total;
	struct rk29_bl_info *rk29_bl_info = bl_get_data(bl);
	u32 id = rk29_bl_info->pwm_id;
	u32 ref = rk29_bl_info->bl_ref;
	int brightness = bl->props.brightness;
	
	if (suspend_flag)
		return 0;	

	if (bl->props.power != FB_BLANK_UNBLANK)
		brightness = 0;

	if (bl->props.fb_blank != FB_BLANK_UNBLANK)
		brightness = 0;	

	if (bl->props.state & BL_CORE_SUSPENDED)
		brightness = 0;

	if (bl->props.brightness < rk29_bl_info->min_brightness)	/*avoid can't view screen when close backlight*/
		brightness = rk29_bl_info->min_brightness;

	div_total = read_pwm_reg(id, PWM_REG_LRC);
	if (ref) {
		divh = div_total*brightness/BL_STEP;
	} else {
		divh = div_total*(BL_STEP-brightness)/BL_STEP;
	}
	write_pwm_reg(id, PWM_REG_HRC, divh);

	DBG("%s:line=%d,brightness = %d, div_total = %d, divh = %d\n",__FUNCTION__,__LINE__,brightness, div_total, divh);
	return 0;
}

static int rk29_bl_get_brightness(struct backlight_device *bl)
{
	u32 divh,div_total;
	struct rk29_bl_info *rk29_bl_info = bl_get_data(bl);
	u32 id = rk29_bl_info->pwm_id;
	u32 ref = rk29_bl_info->bl_ref;

	div_total = read_pwm_reg(id, PWM_REG_LRC);
	divh = read_pwm_reg(id, PWM_REG_HRC);

	if (!div_total)
		return 0;

	if (ref) {
		return BL_STEP*divh/div_total;
	} else {
		return BL_STEP-(BL_STEP*divh/div_total);
	}
}

static struct backlight_ops rk29_bl_ops = {
	.update_status	= rk29_bl_update_status,
	.get_brightness	= rk29_bl_get_brightness,
};

static void rk29_backlight_work_func(struct work_struct *work)
{
	suspend_flag = 0;
	rk29_bl_update_status(rk29_bl);
}
static DECLARE_DELAYED_WORK(rk29_backlight_work, rk29_backlight_work_func);

#ifdef CONFIG_HAS_EARLYSUSPEND
static void rk29_bl_suspend(struct early_suspend *h)
{
	struct rk29_bl_info *rk29_bl_info = bl_get_data(rk29_bl);
	int brightness = rk29_bl->props.brightness;

	cancel_delayed_work_sync(&rk29_backlight_work);

	if (rk29_bl->props.brightness) {
		rk29_bl->props.brightness = 0;
		rk29_bl_update_status(rk29_bl);
		rk29_bl->props.brightness = brightness;
	}

	if (!suspend_flag) {
		clk_disable(pwm_clk);
		if (rk29_bl_info->pwm_suspend)
			rk29_bl_info->pwm_suspend();
	}

	suspend_flag = 1;
}

static void rk29_bl_resume(struct early_suspend *h)
{
	struct rk29_bl_info *rk29_bl_info = bl_get_data(rk29_bl);
	DBG("%s : %s\n", __FILE__, __FUNCTION__);

	if (rk29_bl_info->pwm_resume)
		rk29_bl_info->pwm_resume();

	clk_enable(pwm_clk);

	schedule_delayed_work(&rk29_backlight_work, msecs_to_jiffies(rk29_bl_info->delay_ms));
}

static struct early_suspend bl_early_suspend = {
	.suspend = rk29_bl_suspend,
	.resume = rk29_bl_resume,
	.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN - 1,
};

void rk29_backlight_set(bool on)
{
	printk("%s: set %d\n", __func__, on);
	if(on)
		rk29_bl_resume(NULL);
	else
		rk29_bl_suspend(NULL);
	return;
}
EXPORT_SYMBOL(rk29_backlight_set);
#endif

static int rk29_backlight_probe(struct platform_device *pdev)
{		
	int ret = 0;
	struct rk29_bl_info *rk29_bl_info = pdev->dev.platform_data;
	u32 id  =  rk29_bl_info->pwm_id;
	u32 divh, div_total;
	unsigned long pwm_clk_rate;
	struct backlight_properties props;

	if (rk29_bl) {
		printk(KERN_CRIT "%s: backlight device register has existed \n",
				__func__);
		return -EEXIST;		
	}

	if (!rk29_bl_info->delay_ms)
		rk29_bl_info->delay_ms = 100;

	if (rk29_bl_info->min_brightness < 0 || rk29_bl_info->min_brightness > BL_STEP)
		rk29_bl_info->min_brightness = 52;

	if (rk29_bl_info && rk29_bl_info->io_init) {
		rk29_bl_info->io_init();
	}

	memset(&props, 0, sizeof(struct backlight_properties));
	props.type = BACKLIGHT_RAW;
	props.max_brightness = BL_STEP;
	rk29_bl = backlight_device_register("rk28_bl", &pdev->dev, rk29_bl_info, &rk29_bl_ops, &props);
	if (!rk29_bl) {
		printk(KERN_CRIT "%s: backlight device register error\n",
				__func__);
		return -ENODEV;		
	}

#if defined(CONFIG_ARCH_RK29)
	pwm_clk = clk_get(NULL, "pwm");
#elif defined(CONFIG_ARCH_RK30) || defined(CONFIG_ARCH_RK2928)
	if (id == 0 || id == 1)
		pwm_clk = clk_get(NULL, "pwm01");
	else if (id == 2 || id == 3)
		pwm_clk = clk_get(NULL, "pwm23");
#endif
	if (IS_ERR(pwm_clk) || !pwm_clk) {
		printk(KERN_ERR "failed to get pwm clock source\n");
		return -ENODEV;
	}
	pwm_clk_rate = clk_get_rate(pwm_clk);
	div_total = pwm_clk_rate / PWM_APB_PRE_DIV;

	div_total >>= (1 + (PWM_DIV >> 9));
	div_total = (div_total) ? div_total : 1;

	if(rk29_bl_info->bl_ref) {
		divh = 0;
	} else {
		divh = div_total;
	}

	clk_enable(pwm_clk);
	write_pwm_reg(id, PWM_REG_CTRL, PWM_DIV|PWM_RESET);
	write_pwm_reg(id, PWM_REG_LRC, div_total);
	write_pwm_reg(id, PWM_REG_HRC, divh);
	write_pwm_reg(id, PWM_REG_CNTR, 0x0);
	write_pwm_reg(id, PWM_REG_CTRL, PWM_DIV|PWM_ENABLE|PWM_TIME_EN);

	rk29_bl->props.power = FB_BLANK_UNBLANK;
	rk29_bl->props.fb_blank = FB_BLANK_UNBLANK;
	rk29_bl->props.brightness = BL_STEP / 2;

	schedule_delayed_work(&rk29_backlight_work, msecs_to_jiffies(rk29_bl_info->delay_ms));
	ret = device_create_file(&pdev->dev,&dev_attr_rk29backlight);
	if(ret)
	{
		dev_err(&pdev->dev, "failed to create sysfs file\n");
	}

	register_early_suspend(&bl_early_suspend);

	printk("RK29 Backlight Driver Initialized.\n");
	return ret;
}

static int rk29_backlight_remove(struct platform_device *pdev)
{		
	struct rk29_bl_info *rk29_bl_info = pdev->dev.platform_data;

	if (rk29_bl) {
		backlight_device_unregister(rk29_bl);
		unregister_early_suspend(&bl_early_suspend);
		clk_disable(pwm_clk);
		clk_put(pwm_clk);
		if (rk29_bl_info && rk29_bl_info->io_deinit) {
			rk29_bl_info->io_deinit();
		}
		return 0;
	} else {
		DBG(KERN_CRIT "%s: no backlight device has registered\n",
				__func__);
		return -ENODEV;
	}
}

static void rk29_backlight_shutdown(struct platform_device *pdev)
{
	struct rk29_bl_info *rk29_bl_info = pdev->dev.platform_data;

	unregister_early_suspend(&bl_early_suspend);

	rk29_bl->props.brightness >>= 1;
	rk29_bl_update_status(rk29_bl);
	mdelay(100);

	rk29_bl->props.brightness >>= 1;
	rk29_bl_update_status(rk29_bl);
	mdelay(100);

	rk29_bl->props.brightness = 0;
	rk29_bl_update_status(rk29_bl);

	if (rk29_bl_info && rk29_bl_info->io_deinit)
		rk29_bl_info->io_deinit();
}

static struct platform_driver rk29_backlight_driver = {
	.probe	= rk29_backlight_probe,
	.remove = rk29_backlight_remove,
	.driver	= {
		.name	= "rk29_backlight",
		.owner	= THIS_MODULE,
	},
	.shutdown	= rk29_backlight_shutdown,
};

static int __init rk29_backlight_init(void)
{
	platform_driver_register(&rk29_backlight_driver);
	return 0;
}
fs_initcall_sync(rk29_backlight_init);
