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

#include <mach/regs-clock.h>
#include <mach/regs-gpio.h>
#include <mach/map.h>
#include <mach/gpio.h>
#include <mach/board_rev.h>

#include <plat/clock.h>
#include <plat/gpio-cfg.h>
#include <plat/cpu.h>

struct platform_device; /* don't need the contents */

#ifdef CONFIG_FB_S5P
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

#if defined(CONFIG_FB_S5P_WA101S) || defined(CONFIG_FB_S5P_LTE480WV)
void s3cfb_cfg_gpio(struct platform_device *pdev)
{
	s3cfb_gpio_setup_24bpp(EXYNOS4_GPF0(0), 8, S3C_GPIO_SFN(2), S5P_GPIO_DRVSTR_LV4);
	s3cfb_gpio_setup_24bpp(EXYNOS4_GPF1(0), 8, S3C_GPIO_SFN(2), S5P_GPIO_DRVSTR_LV4);
	s3cfb_gpio_setup_24bpp(EXYNOS4_GPF2(0), 8, S3C_GPIO_SFN(2), S5P_GPIO_DRVSTR_LV4);
	s3cfb_gpio_setup_24bpp(EXYNOS4_GPF3(0), 4, S3C_GPIO_SFN(2), S5P_GPIO_DRVSTR_LV4);
}
#elif defined(CONFIG_FB_S5P_AMS369FG06)
void s3cfb_cfg_gpio(struct platform_device *pdev)
{
	s3cfb_gpio_setup_24bpp(EXYNOS4_GPF0(0), 8, S3C_GPIO_SFN(2), S5P_GPIO_DRVSTR_LV1);
	s3cfb_gpio_setup_24bpp(EXYNOS4_GPF1(0), 8, S3C_GPIO_SFN(2), S5P_GPIO_DRVSTR_LV1);
	s3cfb_gpio_setup_24bpp(EXYNOS4_GPF2(0), 8, S3C_GPIO_SFN(2), S5P_GPIO_DRVSTR_LV1);
	s3cfb_gpio_setup_24bpp(EXYNOS4_GPF3(0), 4, S3C_GPIO_SFN(2), S5P_GPIO_DRVSTR_LV1);
}
#elif defined(CONFIG_FB_S5P_LMS501KF03)
void s3cfb_cfg_gpio(struct platform_device *pdev)
{
	s3cfb_gpio_setup_24bpp(EXYNOS4_GPF0(0), 8, S3C_GPIO_SFN(2), S5P_GPIO_DRVSTR_LV4);
	s3cfb_gpio_setup_24bpp(EXYNOS4_GPF1(0), 8, S3C_GPIO_SFN(2), S5P_GPIO_DRVSTR_LV1);
	s3cfb_gpio_setup_24bpp(EXYNOS4_GPF2(0), 8, S3C_GPIO_SFN(2), S5P_GPIO_DRVSTR_LV1);
	s3cfb_gpio_setup_24bpp(EXYNOS4_GPF3(0), 4, S3C_GPIO_SFN(2), S5P_GPIO_DRVSTR_LV1);
}
#elif defined(CONFIG_FB_S5P_HT101HD1)
void s3cfb_cfg_gpio(struct platform_device *pdev)
{
	s3cfb_gpio_setup_24bpp(EXYNOS4_GPF0(0), 8, S3C_GPIO_SFN(2), S5P_GPIO_DRVSTR_LV1);
	s3cfb_gpio_setup_24bpp(EXYNOS4_GPF1(0), 8, S3C_GPIO_SFN(2), S5P_GPIO_DRVSTR_LV1);
	s3cfb_gpio_setup_24bpp(EXYNOS4_GPF2(0), 8, S3C_GPIO_SFN(2), S5P_GPIO_DRVSTR_LV1);
	s3cfb_gpio_setup_24bpp(EXYNOS4_GPF3(0), 6, S3C_GPIO_SFN(2), S5P_GPIO_DRVSTR_LV1);
}
#endif
#endif

int s3cfb_clk_on(struct platform_device *pdev, struct clk **s3cfb_clk)
{
	struct clk *sclk = NULL;
	struct clk *mout_mpll = NULL;
	struct clk *lcd_clk = NULL;

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

	return 0;

err_clk0:
	clk_put(lcd_clk);

	return -EINVAL;
}

void s3cfb_get_clk_name(char *clk_name)
{
	strcpy(clk_name, "sclk_fimd");
}

#define EXYNOS4_GPD_0_0_TOUT_0  (0x2)
#define EXYNOS4_GPD_0_1_TOUT_1  (0x2 << 4)
#define EXYNOS4_GPD_0_2_TOUT_2  (0x2 << 8)
#define EXYNOS4_GPD_0_3_TOUT_3  (0x2 << 12)

#if defined(CONFIG_FB_S5P_WA101S)
int s3cfb_backlight_on(struct platform_device *pdev)
{
#if !defined(CONFIG_BACKLIGHT_PWM)
	int err;

	err = gpio_request_one(EXYNOS4_GPD0(1), GPIOF_OUT_INIT_HIGH, "GPD0");
	if (err) {
		printk(KERN_ERR "failed to request GPD0 for "
			"lcd backlight control\n");
		return err;
	}
	gpio_free(EXYNOS4_GPD0(1));
#endif
	return 0;
}

int s3cfb_backlight_off(struct platform_device *pdev)
{
#if !defined(CONFIG_BACKLIGHT_PWM)
	int err;

	err = gpio_request_one(EXYNOS4_GPD0(1), GPIOF_OUT_INIT_LOW, "GPD0");
	if (err) {
		printk(KERN_ERR "failed to request GPD0 for "
			"lcd backlight control\n");
		return err;
	}
	gpio_free(EXYNOS4_GPD0(1));
#endif
	return 0;
}

int s3cfb_lcd_on(struct platform_device *pdev)
{
	return 0;
}

int s3cfb_lcd_off(struct platform_device *pdev)
{
	return 0;
}

#elif defined(CONFIG_FB_S5P_LTE480WV)
int s3cfb_backlight_on(struct platform_device *pdev)
{
#if !defined(CONFIG_BACKLIGHT_PWM)
	int err;

	err = gpio_request_one(EXYNOS4_GPD0(1), GPIOF_OUT_INIT_HIGH, "GPD0");
	if (err) {
		printk(KERN_ERR "failed to request GPD0 for "
			"lcd backlight control\n");
		return err;
	}
	gpio_free(EXYNOS4_GPD0(1));
#endif
	return 0;
}

int s3cfb_backlight_off(struct platform_device *pdev)
{
#if !defined(CONFIG_BACKLIGHT_PWM)
	int err;

	err = gpio_request_one(EXYNOS4_GPD0(1), GPIOF_OUT_INIT_LOW, "GPD0");
	if (err) {
		printk(KERN_ERR "failed to request GPD0 for "
			"lcd backlight control\n");
		return err;
	}
	gpio_free(EXYNOS4_GPD0(1));
#endif
	return 0;
}

int s3cfb_lcd_on(struct platform_device *pdev)
{
	int err;

	err = gpio_request_one(EXYNOS4_GPX0(6), GPIOF_OUT_INIT_HIGH, "GPX0");
	if (err) {
		printk(KERN_ERR "failed to request GPX0 for "
			"lcd reset control\n");
		return err;
	}
	msleep(100);

	gpio_set_value(EXYNOS4_GPX0(6), 0);
	msleep(10);

	gpio_set_value(EXYNOS4_GPX0(6), 1);
	msleep(10);

	gpio_free(EXYNOS4_GPX0(6));

	return 0;
}

int s3cfb_lcd_off(struct platform_device *pdev)
{
	return 0;
}
#elif defined(CONFIG_FB_S5P_HT101HD1)
int s3cfb_backlight_on(struct platform_device *pdev)
{
#if !defined(CONFIG_BACKLIGHT_PWM)
	int err;

	/* Backlight High */
	err = gpio_request_one(EXYNOS4_GPD0(0), GPIOF_OUT_INIT_HIGH, "GPD0");
	if (err) {
		printk(KERN_ERR "failed to request GPD0 for "
			"lcd backlight control\n");
		return err;
	}
	gpio_free(EXYNOS4_GPD0(0));

	/* LED_EN (SPI1_MOSI) High */
	err = gpio_request_one(EXYNOS4_GPB(2), GPIOF_OUT_INIT_HIGH, "GPB");
	if (err) {
		printk(KERN_ERR "failed to request GPB for "
			"lcd LED_EN control\n");
		return err;
	}
	gpio_free(EXYNOS4_GPB(2));
#endif
	return 0;
}

int s3cfb_backlight_off(struct platform_device *pdev)
{
#if !defined(CONFIG_BACKLIGHT_PWM)
	int err;

	/* Backlight Low */
	err = gpio_request_one(EXYNOS4_GPD0(0), GPIOF_OUT_INIT_LOW, "GPD0");
	if (err) {
		printk(KERN_ERR "failed to request GPD0 for "
			"lcd backlight control\n");
		return err;
	}
	gpio_free(EXYNOS4_GPD0(0));

	/* LED_EN (SPI1_MOSI) Low */
	err = gpio_request_one(EXYNOS4_GPB(2), GPIOF_OUT_INIT_LOW, "GPB");
	if (err) {
		printk(KERN_ERR "failed to request GPB for "
			"lcd LED_EN control\n");
		return err;
	}
	gpio_free(EXYNOS4_GPB(2));
#endif
	return 0;
}

int s3cfb_lcd_on(struct platform_device *pdev)
{
	int err;

	err = gpio_request_one(EXYNOS4_GPH0(1), GPIOF_OUT_INIT_HIGH, "GPH0");
	if (err) {
		printk(KERN_ERR "failed to request GPH0 for "
			"lcd reset control\n");
		return err;
	}

	gpio_set_value(EXYNOS4_GPH0(1), 0);
	gpio_set_value(EXYNOS4_GPH0(1), 1);

	gpio_free(EXYNOS4_GPH0(1));

	return 0;
}

int s3cfb_lcd_off(struct platform_device *pdev)
{
	return 0;
}
#elif defined(CONFIG_FB_S5P_AMS369FG06) || defined(CONFIG_FB_S5P_LMS501KF03)
int s3cfb_backlight_on(struct platform_device *pdev)
{
#if !defined(CONFIG_BACKLIGHT_PWM)
	int err;

	err = gpio_request_one(EXYNOS4_GPD0(1), GPIOF_OUT_INIT_HIGH, "GPD0");
	if (err) {
		printk(KERN_ERR "failed to request GPD0 for "
			"lcd backlight control\n");
		return err;
	}
	gpio_free(EXYNOS4_GPD0(1));
#endif
	return 0;
}

int s3cfb_backlight_off(struct platform_device *pdev)
{
#if !defined(CONFIG_BACKLIGHT_PWM)
	int err;

	err = gpio_request_one(EXYNOS4_GPD0(1), GPIOF_OUT_INIT_LOW, "GPD0");
	if (err) {
		printk(KERN_ERR "failed to request GPD0 for "
			"lcd backlight control\n");
		return err;
	}
	gpio_free(EXYNOS4_GPD0(1));
#endif
	return 0;
}

int s3cfb_lcd_on(struct platform_device *pdev)
{
	int err;

#ifdef CONFIG_MACH_SMDKC210
	err = gpio_request_one(EXYNOS4_GPX0(6), GPIOF_OUT_INIT_HIGH, "GPX0");
	if (err) {
		printk(KERN_ERR "failed to request GPX0 for "
			"lcd reset control\n");
		return err;
	}

	gpio_set_value(EXYNOS4_GPX0(6), 0);
	mdelay(1);

	gpio_set_value(EXYNOS4_GPX0(6), 1);

	gpio_free(EXYNOS4_GPX0(6));
#elif defined(CONFIG_MACH_SMDK4X12)
	if (samsung_board_rev_is_0_1()) {
		err = gpio_request_one(EXYNOS4212_GPM3(6),
				GPIOF_OUT_INIT_HIGH, "GPM3");
		if (err) {
			printk(KERN_ERR "failed to request GPM3 for "
				"lcd reset control\n");
			return err;
		}

		gpio_set_value(EXYNOS4212_GPM3(6), 0);
		mdelay(1);

		gpio_set_value(EXYNOS4212_GPM3(6), 1);

		gpio_free(EXYNOS4212_GPM3(6));

	} else {
		err = gpio_request_one(EXYNOS4_GPX1(5),
				GPIOF_OUT_INIT_HIGH, "GPX0");
		if (err) {
			printk(KERN_ERR "failed to request GPX0 for "
				"lcd reset control\n");
			return err;
		}

		gpio_set_value(EXYNOS4_GPX1(5), 0);
		mdelay(1);

		gpio_set_value(EXYNOS4_GPX1(5), 1);

		gpio_free(EXYNOS4_GPX1(5));
	}
#endif

	return 0;
}

int s3cfb_lcd_off(struct platform_device *pdev)
{
	return 0;
}

#else
void s3cfb_cfg_gpio(struct platform_device *pdev)
{
	return;
}
int s3cfb_backlight_on(struct platform_device *pdev)
{
	return 0;
}

int s3cfb_backlight_off(struct platform_device *pdev)
{
	return 0;
}

int s3cfb_lcd_on(struct platform_device *pdev)
{
	return 0;
}

int s3cfb_lcd_off(struct platform_device *pdev)
{
	return 0;
}
#endif
