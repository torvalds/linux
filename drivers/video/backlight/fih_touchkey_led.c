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
#include <mach/rk29_iomap.h>
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


#define write_pwm_reg(id, addr, val)        __raw_writel(val, addr+(RK29_PWM_BASE+id*0x10))
#define read_pwm_reg(id, addr)              __raw_readl(addr+(RK29_PWM_BASE+id*0x10))    

static struct clk *pwm_clk;
static struct backlight_device *fih_touchkey_led;
static int suspend_flag = 0;

static int fih_touchkey_led_update_status(struct backlight_device *bl)
{
	u32 divh,div_total;
	struct rk29_bl_info *fih_touchkey_led_info = bl_get_data(bl);
	u32 id = fih_touchkey_led_info->pwm_id;
	u32 ref = fih_touchkey_led_info->bl_ref;

	if (suspend_flag)
		return 0;

	if (bl->props.brightness < fih_touchkey_led_info->min_brightness)	/*avoid can't view screen when close backlight*/
		bl->props.brightness = fih_touchkey_led_info->min_brightness;

	div_total = read_pwm_reg(id, PWM_REG_LRC);
	if (ref) {
		divh = div_total*(bl->props.brightness)/BL_STEP;
	} else {
		divh = div_total*(BL_STEP-bl->props.brightness)/BL_STEP;
	}
	write_pwm_reg(id, PWM_REG_HRC, divh);

	DBG(">>>%s-->%d brightness = %d, div_total = %d, divh = %d\n",__FUNCTION__,__LINE__,bl->props.brightness, div_total, divh);
	return 0;
}

static int fih_touchkey_led_get_brightness(struct backlight_device *bl)
{
	u32 divh,div_total;
	struct rk29_bl_info *fih_touchkey_led_info = bl_get_data(bl);
	u32 id = fih_touchkey_led_info->pwm_id;
	u32 ref = fih_touchkey_led_info->bl_ref;

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

static struct backlight_ops fih_touchkey_led_ops = {
	.update_status	= fih_touchkey_led_update_status,
	.get_brightness	= fih_touchkey_led_get_brightness,
};

static void fih_touchkey_led_work_func(struct work_struct *work)
{
	suspend_flag = 0;
	fih_touchkey_led_update_status(fih_touchkey_led);
}
static DECLARE_DELAYED_WORK(fih_touchkey_led_work, fih_touchkey_led_work_func);

#ifdef CONFIG_HAS_EARLYSUSPEND
static void fih_touchkey_led_suspend(struct early_suspend *h)
{
	struct rk29_bl_info *fih_touchkey_led_info = bl_get_data(fih_touchkey_led);
	int brightness = fih_touchkey_led->props.brightness;

	cancel_delayed_work_sync(&fih_touchkey_led_work);

	if (fih_touchkey_led->props.brightness) {
		fih_touchkey_led->props.brightness = 0;
		fih_touchkey_led_update_status(fih_touchkey_led);
		fih_touchkey_led->props.brightness = brightness;
	}

	if (!suspend_flag) {
		clk_disable(pwm_clk);
		if (fih_touchkey_led_info->pwm_suspend)
			fih_touchkey_led_info->pwm_suspend();
	}

	suspend_flag = 1;
}

static void fih_touchkey_led_resume(struct early_suspend *h)
{
	struct rk29_bl_info *fih_touchkey_led_info = bl_get_data(fih_touchkey_led);
	DBG("%s : %s\n", __FILE__, __FUNCTION__);

	if (fih_touchkey_led_info->pwm_resume)
		fih_touchkey_led_info->pwm_resume();

	clk_enable(pwm_clk);

	schedule_delayed_work(&fih_touchkey_led_work, msecs_to_jiffies(fih_touchkey_led_info->delay_ms));
}

static struct early_suspend fih_touchkey_led_early_suspend = {
	.suspend = fih_touchkey_led_suspend,
	.resume = fih_touchkey_led_resume,
	.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN - 1,
};
#endif

static int fih_touchkey_led_probe(struct platform_device *pdev)
{		
	int ret = 0;
	struct rk29_bl_info *fih_touchkey_led_info = pdev->dev.platform_data;
	u32 id  =  fih_touchkey_led_info->pwm_id;
	u32 divh, div_total;
	unsigned long pwm_clk_rate;

	if (fih_touchkey_led) {
		DBG(KERN_CRIT "%s: fih touchkey led device register has existed \n",
				__func__);
		return -EEXIST;		
	}

	if (!fih_touchkey_led_info->delay_ms)
		fih_touchkey_led_info->delay_ms = 30;

	if (fih_touchkey_led_info->min_brightness < 0 || fih_touchkey_led_info->min_brightness > BL_STEP)
		fih_touchkey_led_info->min_brightness = 52;

	if (fih_touchkey_led_info && fih_touchkey_led_info->io_init) {
		fih_touchkey_led_info->io_init();
	}

	fih_touchkey_led = backlight_device_register("fih_touchkey_led", &pdev->dev, fih_touchkey_led_info, &fih_touchkey_led_ops);
	if (!fih_touchkey_led) {
		DBG(KERN_CRIT "%s: fih touchkey led register error\n",
				__func__);
		return -ENODEV;		
	}

	pwm_clk = clk_get(NULL, "pwm");
	if (IS_ERR(pwm_clk)) {
		printk(KERN_ERR "failed to get pwm clock source\n");
		return -ENODEV;	
	}
	pwm_clk_rate = clk_get_rate(pwm_clk);
	div_total = pwm_clk_rate / PWM_APB_PRE_DIV;

	div_total >>= (1 + (PWM_DIV >> 9));
	div_total = (div_total) ? div_total : 1;

	if(fih_touchkey_led_info->bl_ref) {
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

	fih_touchkey_led->props.power = FB_BLANK_UNBLANK;
	fih_touchkey_led->props.fb_blank = FB_BLANK_UNBLANK;
	fih_touchkey_led->props.max_brightness = BL_STEP;
	fih_touchkey_led->props.brightness = BL_STEP / 2;

	schedule_delayed_work(&fih_touchkey_led_work, msecs_to_jiffies(fih_touchkey_led_info->delay_ms));

	register_early_suspend(&fih_touchkey_led_early_suspend);

	printk("Fih touchkey led Driver Initialized.\n");
	return ret;
}

static int fih_touchkey_led_remove(struct platform_device *pdev)
{		
	struct rk29_bl_info *rk29_bl_info = pdev->dev.platform_data;

	if (fih_touchkey_led) {
		backlight_device_unregister(fih_touchkey_led);
		unregister_early_suspend(&fih_touchkey_led_early_suspend);
		clk_disable(pwm_clk);
		clk_put(pwm_clk);
		if (rk29_bl_info && rk29_bl_info->io_deinit) {
			rk29_bl_info->io_deinit();
		}
		return 0;
	} else {
		DBG(KERN_CRIT "%s: no Fih touchkey led device has registered\n",
				__func__);
		return -ENODEV;
	}
}

static void fih_touchkey_led_shutdown(struct platform_device *pdev)
{
	struct rk29_bl_info *fih_touchkey_led_info = pdev->dev.platform_data;

	fih_touchkey_led->props.brightness >>= 1;
	fih_touchkey_led_update_status(fih_touchkey_led);
	mdelay(100);

	fih_touchkey_led->props.brightness >>= 1;
	fih_touchkey_led_update_status(fih_touchkey_led);
	mdelay(100);

	fih_touchkey_led->props.brightness = 0;
	fih_touchkey_led_update_status(fih_touchkey_led);

	if (fih_touchkey_led_info && fih_touchkey_led_info->io_deinit)
		fih_touchkey_led_info->io_deinit();
}

static struct platform_driver fih_touchkey_led_driver = {
	.probe	= fih_touchkey_led_probe,
	.remove = fih_touchkey_led_remove,
	.driver	= {
		.name	= "fih_touchkey_led",
		.owner	= THIS_MODULE,
	},
	.shutdown	= fih_touchkey_led_shutdown,
};

static int __init fih_touchkey_led_init(void)
{
	platform_driver_register(&fih_touchkey_led_driver);
	return 0;
}
fs_initcall_sync(fih_touchkey_led_init);
//late_initcall_sync(fih_touchkey_led_init);
