/* linux/arch/arm/mach-exynos/setup-fb-s5p.c
 *
 * Copyright (c) 2010 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * Base FIMD controller configuration
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/gpio.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/types.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/gcd.h>

#include <mach/regs-clock.h>
#include <mach/regs-gpio.h>
#include <mach/map.h>
#include <mach/gpio.h>
#include <mach/board_rev.h>

#include <plat/clock.h>
#include <plat/gpio-cfg.h>
#include <plat/cpu.h>
#include <plat/clock-clksrc.h>
#if defined(CONFIG_S5P_DSIM_SWITCHABLE_DUAL_LCD)
#include <../../../drivers/video/samsung_duallcd/s3cfb.h>
#else
#include <../../../drivers/video/samsung/s3cfb.h>	/* should be fixed */
#endif 
#include <linux/gcd.h>


struct platform_device; /* don't need the contents */

extern	void 	SYSTEM_POWER_CONTROL	(int power, int val);

#ifdef CONFIG_FB_S5P

void s3cfb_set_display_path(void)
{
	u32 reg;
#ifdef CONFIG_FB_S5P_MDNIE
	reg = __raw_readl(S3C_VA_SYS + 0x0210);
	reg &= ~(1<<13);
	reg &= ~(1<<12);
	reg &= ~(3<<10);
	reg |= (1<<0);
	reg &= ~(1<<1);
	__raw_writel(reg, S3C_VA_SYS + 0x0210);
#else
	reg = __raw_readl(S3C_VA_SYS + 0x0210);
	reg |= (1<<1);
	__raw_writel(reg, S3C_VA_SYS + 0x0210);
#endif
}

static void s3cfb_gpio_setup_24bpp(unsigned int start, unsigned int size,
		unsigned int cfg, s5p_gpio_drvstr_t drvstr)
{
	u32 reg;

	s3c_gpio_cfgrange_nopull(start, size, cfg);

	for (; size > 0; size--, start++)
		s5p_gpio_set_drvstr(start, drvstr);

	/* Set FIMD0 bypass */
	reg = __raw_readl(S3C_VA_SYS + 0x0210);
	reg |= (1<<1);
	__raw_writel(reg, S3C_VA_SYS + 0x0210);
}

void s3cfb_cfg_gpio(struct platform_device *pdev)
{
#if defined(CONFIG_BOARD_ODROID_X)||defined(CONFIG_BOARD_ODROID_X2)||defined(CONFIG_BOARD_ODROID_U)||defined(CONFIG_BOARD_ODROID_U2)
	s3cfb_gpio_setup_24bpp(EXYNOS4_GPF0(0), 8, S3C_GPIO_SFN(2), S5P_GPIO_DRVSTR_LV4);
	s3cfb_gpio_setup_24bpp(EXYNOS4_GPF1(0), 8, S3C_GPIO_SFN(2), S5P_GPIO_DRVSTR_LV4);
	s3cfb_gpio_setup_24bpp(EXYNOS4_GPF2(0), 8, S3C_GPIO_SFN(2), S5P_GPIO_DRVSTR_LV4);
	s3cfb_gpio_setup_24bpp(EXYNOS4_GPF3(0), 4, S3C_GPIO_SFN(2), S5P_GPIO_DRVSTR_LV4);
#else	
	s3cfb_gpio_setup_24bpp(EXYNOS4_GPF0(0), 8, S3C_GPIO_SFN(2), S5P_GPIO_DRVSTR_LV4);
	s3cfb_gpio_setup_24bpp(EXYNOS4_GPF1(0), 8, S3C_GPIO_SFN(2), S5P_GPIO_DRVSTR_LV1);
	s3cfb_gpio_setup_24bpp(EXYNOS4_GPF2(0), 8, S3C_GPIO_SFN(2), S5P_GPIO_DRVSTR_LV1);
	s3cfb_gpio_setup_24bpp(EXYNOS4_GPF3(0), 4, S3C_GPIO_SFN(2), S5P_GPIO_DRVSTR_LV1);
#endif	
}

#if defined(CONFIG_FB_S5P_MIPI_DSIM)
int s3cfb_mipi_clk_enable(int enable)
{
	struct clk *dsim_clk = NULL;

	dsim_clk = clk_get(NULL, "dsim0");
	if (IS_ERR(dsim_clk)) {
		printk(KERN_ERR "failed to get ip clk for dsim0\n");
		goto err_clk0;
	}

	if (enable)
		clk_enable(dsim_clk);
	else
		clk_disable(dsim_clk);

	clk_put(dsim_clk);
	return 0;

err_clk0:
	clk_put(dsim_clk);

	return -EINVAL;
}
#endif

#if 0
unsigned int get_clk_rate(struct platform_device *pdev, struct clk *sclk)
{
	struct s3c_platform_fb *pdata = pdev->dev.platform_data;
	struct s3cfb_lcd *lcd = (struct s3cfb_lcd *)pdata->lcd;
	struct s3cfb_lcd_timing *timing = &lcd->timing;
	u32 src_clk, vclk, div, rate;
	u32 vclk_limit, div_limit, fimd_div;

	src_clk = clk_get_rate(sclk);

	vclk = (lcd->freq *
		(timing->h_bp + timing->h_fp + timing->h_sw + lcd->width) *
		(timing->v_bp + timing->v_fp + timing->v_sw + lcd->height));

	if (!vclk)
		vclk = src_clk;

	div = DIV_ROUND_CLOSEST(src_clk, vclk);

	if (lcd->freq_limit) {
		vclk_limit = (lcd->freq_limit *
			(timing->h_bp + timing->h_fp + timing->h_sw + lcd->width) *
			(timing->v_bp + timing->v_fp + timing->v_sw + lcd->height));

		div_limit = DIV_ROUND_CLOSEST(src_clk, vclk_limit);

		fimd_div = gcd(div, div_limit);

		div /= fimd_div;
	}

	if (!div) {
		dev_err(&pdev->dev, "div(%d) should be non-zero\n", div);
		div = 1;
	} else if (div > 16) {
		dev_err(&pdev->dev, "div(%d) max should be 16\n", div);
		for (fimd_div = 2; fimd_div < div; div++) {
			if (div%fimd_div == 0)
				break;
		}
		div /= fimd_div;
		div = (div > 16) ? 16 : div;
	}

	rate = src_clk / div;

	if ((src_clk % rate) && (div != 1)) {
		div--;
		rate = src_clk / div;
		if (!(src_clk % rate))
			rate--;
	}

	dev_info(&pdev->dev, "vclk=%d, div=%d(%d), rate=%d\n",
		vclk, DIV_ROUND_CLOSEST(src_clk, vclk), div, rate);

	return rate;
}

#endif

int s3cfb_clk_on(struct platform_device *pdev, struct clk **s3cfb_clk)
{
	struct clk *sclk = NULL;
	struct clk *mout_mpll = NULL;
	struct clk *lcd_clk = NULL;
	struct clksrc_clk *src_clk = NULL;
	u32 clkdiv = 0;
	struct s3c_platform_fb *pdata = pdev->dev.platform_data;
	struct s3cfb_lcd *lcd = (struct s3cfb_lcd *)pdata->lcd;

	u32 rate = 0;
	int ret = 0;

	lcd_clk = clk_get(&pdev->dev, "lcd");
	if (IS_ERR(lcd_clk)) {
		dev_err(&pdev->dev, "failed to get operation clk for fimd\n");
		goto err_clk0;
	}

	ret = clk_enable(lcd_clk);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to clk_enable of lcd clk for fimd\n");
		goto err_clk0;
	}
	clk_put(lcd_clk);

	sclk = clk_get(&pdev->dev, "sclk_fimd");
	if (IS_ERR(sclk)) {
		dev_err(&pdev->dev, "failed to get sclk for fimd\n");
		goto err_clk1;
	}

	if (soc_is_exynos4210())
		mout_mpll = clk_get(&pdev->dev, "mout_mpll");
	else
		mout_mpll = clk_get(&pdev->dev, "mout_mpll_user");

	if (IS_ERR(mout_mpll)) {
		dev_err(&pdev->dev, "failed to get mout_mpll for fimd\n");
		goto err_clk2;
	}

	ret = clk_set_parent(sclk, mout_mpll);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to clk_set_parent for fimd\n");
		goto err_clk2;
	}

#if 0
	if (!lcd->vclk) {
		rate = get_clk_rate(pdev, mout_mpll);
		if (!rate)
			rate = 800 * MHZ;	/* MOUT PLL */
		lcd->vclk = rate;
	} else
		rate = lcd->vclk;

	ret = clk_set_rate(sclk, rate);
#endif

    ret = clk_set_rate(sclk, 800000000);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to clk_set_rate of sclk for fimd\n");
		goto err_clk2;
	}
	dev_dbg(&pdev->dev, "set fimd sclk rate to %d\n", rate);

	clk_put(mout_mpll);

	ret = clk_enable(sclk);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to clk_enable of sclk for fimd\n");
		goto err_clk2;
	}

	*s3cfb_clk = sclk;

#ifdef CONFIG_FB_S5P_MIPI_DSIM
	s3cfb_mipi_clk_enable(1);
#endif
#ifdef CONFIG_FB_S5P_MDNIE
	s3cfb_mdnie_clk_on(rate);
#ifdef CONFIG_FB_MDNIE_PWM
	s3cfb_mdnie_pwm_clk_on();
#endif
#endif

	src_clk = container_of(sclk, struct clksrc_clk, clk);
	clkdiv = __raw_readl(src_clk->reg_div.reg);

	dev_info(&pdev->dev, "fimd sclk rate %ld, clkdiv 0x%x\n",
		clk_get_rate(sclk), clkdiv);

	return 0;

err_clk2:
	clk_put(mout_mpll);
err_clk1:
	clk_put(sclk);
err_clk0:
	clk_put(lcd_clk);

	return -EINVAL;
}

int s3cfb_clk_off(struct platform_device *pdev, struct clk **clk)
{
	struct clk *lcd_clk = NULL;

	lcd_clk = clk_get(&pdev->dev, "lcd");
	if (IS_ERR(lcd_clk)) {
		printk(KERN_ERR "failed to get ip clk for fimd0\n");
		goto err_clk0;
	}

	clk_disable(lcd_clk);
	clk_put(lcd_clk);

	clk_disable(*clk);
	clk_put(*clk);

	*clk = NULL;

#ifdef CONFIG_FB_S5P_MIPI_DSIM
	s3cfb_mipi_clk_enable(0);
#endif

	return 0;

err_clk0:
	clk_put(lcd_clk);

	return -EINVAL;
}


#define EXYNOS4_GPD_0_0_TOUT_0  (0x2)
#define EXYNOS4_GPD_0_1_TOUT_1  (0x2 << 4)
#define EXYNOS4_GPD_0_2_TOUT_2  (0x2 << 8)
#define EXYNOS4_GPD_0_3_TOUT_3  (0x2 << 12)

int s3cfb_backlight_on(struct platform_device *pdev)
{
#if defined(CONFIG_MACH_ODROID_4X12)
	int err;

#if defined(CONFIG_BOARD_ODROID_X)||defined(CONFIG_BOARD_ODROID_X2)||defined(CONFIG_BOARD_ODROID_U)||defined(CONFIG_BOARD_ODROID_U2)
    	err = gpio_request_one(EXYNOS4_GPA1(3), GPIOF_OUT_INIT_HIGH, "GPA1.3");
#else
	#if defined(CONFIG_FB_S5P_LP101WH1) || defined(CONFIG_FB_S5P_U133WA01)
    	err = gpio_request_one(EXYNOS4_GPA1(3), GPIOF_OUT_INIT_LOW, "GPA1.3");
    #else
    	err = gpio_request_one(EXYNOS4_GPA1(3), GPIOF_OUT_INIT_HIGH, "GPA1.3");
    #endif    	
#endif    
	if (err) {
		printk(KERN_ERR "failed to request GPA1.3 for "
			"lcd backlight control\n");
		return err;
	}
	s3c_gpio_setpull(EXYNOS4_GPA1(3), S3C_GPIO_PULL_NONE);
	gpio_free(EXYNOS4_GPA1(3));

	return 0;

#endif
}

#if defined(CONFIG_FB_S5P_MIPI_DSIM)
#define GPIO_MLCD_RST   EXYNOS4_GPX2(1)

static int reset_lcd(void)
{
	int err;

	err = gpio_request(GPIO_MLCD_RST, "MLCD_RST");
	if (err) {
		printk(KERN_ERR "failed to request GPY4(5) for "
			"lcd reset control\n");
		return -EINVAL;
	}

#if defined(CONFIG_FB_S5P_S6E8AA1)
	gpio_direction_output(GPIO_MLCD_RST, 1);
#endif	
#if defined(CONFIG_FB_S5P_LG4591)
	gpio_direction_output(GPIO_MLCD_RST, 0);
#endif	
	gpio_free(GPIO_MLCD_RST);
	return  0;
}
#endif

int s3cfb_backlight_off(struct platform_device *pdev)
{
#if defined(CONFIG_MACH_ODROID_4X12)
	int err;

#if defined(CONFIG_BOARD_ODROID_X)||defined(CONFIG_BOARD_ODROID_X2)||defined(CONFIG_BOARD_ODROID_U)||defined(CONFIG_BOARD_ODROID_U2)
    	err = gpio_request_one(EXYNOS4_GPA1(3), GPIOF_OUT_INIT_LOW, "GPA1.3");
#else    	
	#if defined(CONFIG_FB_S5P_LP101WH1) || defined(CONFIG_FB_S5P_U133WA01)
    	err = gpio_request_one(EXYNOS4_GPA1(3), GPIOF_OUT_INIT_HIGH, "GPA1.3");
    #else
    	err = gpio_request_one(EXYNOS4_GPA1(3), GPIOF_OUT_INIT_LOW, "GPA1.3");
    #endif    	
#endif    
	if (err) {
		printk(KERN_ERR "failed to request GPA1.3 for "
			"lcd backlight control\n");
		return err;
	}
	gpio_free(EXYNOS4_GPA1(3));

#if defined(CONFIG_FB_S5P_MIPI_DSIM)
    reset_lcd();
#endif
    
	return 0;
#endif
}

#if defined(CONFIG_FB_S5P_LG4591)
    extern  void    lg4591_lcd_init   (void);
#endif

#if defined(CONFIG_FB_S5P_S6E8AA1)
    extern  void    s6e8aa1_lcd_init  (void);
#endif

int s3cfb_lcd_on(struct platform_device *pdev)
{
    printk("%s\n", __func__);
#if defined(CONFIG_FB_S5P_LG4591)
    lg4591_lcd_init   ();
#endif

#if defined(CONFIG_FB_S5P_S6E8AA1)
    s6e8aa1_lcd_init  ();
#endif
	return 0;
}

int s3cfb_lcd_off(struct platform_device *pdev)
{
    printk("%s\n", __func__);
	return 0;
}

void s3cfb_get_clk_name(char *clk_name)
{
	strcpy(clk_name, "sclk_fimd");
}

#endif
