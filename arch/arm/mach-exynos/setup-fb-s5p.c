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

struct platform_device; /* don't need the contents */

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

#if !defined(CONFIG_FB_S5P_MIPI_DSIM)
static void s3cfb_gpio_setup_24bpp(unsigned int start, unsigned int size,
		unsigned int cfg, s5p_gpio_drvstr_t drvstr)
{
	s3c_gpio_cfgrange_nopull(start, size, cfg);

	for (; size > 0; size--, start++)
		s5p_gpio_set_drvstr(start, drvstr);
}
#endif

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
	s3cfb_gpio_setup_24bpp(EXYNOS4_GPF3(0), 4, S3C_GPIO_SFN(2), S5P_GPIO_DRVSTR_LV1);
}
#elif defined(CONFIG_FB_S5P_LD9040) || defined(CONFIG_FB_S5P_S6F1202A)
void s3cfb_cfg_gpio(struct platform_device *pdev)
{
	s3cfb_gpio_setup_24bpp(EXYNOS4_GPF0(0), 8, S3C_GPIO_SFN(2), S5P_GPIO_DRVSTR_LV4);
	s3cfb_gpio_setup_24bpp(EXYNOS4_GPF1(0), 8, S3C_GPIO_SFN(2), S5P_GPIO_DRVSTR_LV4);
	s3cfb_gpio_setup_24bpp(EXYNOS4_GPF2(0), 8, S3C_GPIO_SFN(2), S5P_GPIO_DRVSTR_LV4);
	s3cfb_gpio_setup_24bpp(EXYNOS4_GPF3(0), 4, S3C_GPIO_SFN(2), S5P_GPIO_DRVSTR_LV4);
}
#elif defined(CONFIG_FB_S5P_S6C1372)
void s3cfb_cfg_gpio(struct platform_device *pdev)
{
	s3cfb_gpio_setup_24bpp(EXYNOS4_GPF0(0), 8, S3C_GPIO_SFN(2), S5P_GPIO_DRVSTR_LV2);
	s3cfb_gpio_setup_24bpp(EXYNOS4_GPF1(0), 8, S3C_GPIO_SFN(2), S5P_GPIO_DRVSTR_LV2);
	s3cfb_gpio_setup_24bpp(EXYNOS4_GPF2(0), 8, S3C_GPIO_SFN(2), S5P_GPIO_DRVSTR_LV2);
	s3cfb_gpio_setup_24bpp(EXYNOS4_GPF3(0), 4, S3C_GPIO_SFN(2), S5P_GPIO_DRVSTR_LV2);
}
#else
void s3cfb_cfg_gpio(struct platform_device *pdev)
{
	/* do not modify this #else function,
	if you want another rgb gpio configuration plz add another one */
}
#endif
#endif

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

#elif defined(CONFIG_FB_S5P_S6C1372) && !defined(CONFIG_FB_MDNIE_PWM)
int s3cfb_backlight_on(struct platform_device *pdev)
{
	gpio_request_one(EXYNOS4_GPD0(1), GPIOF_OUT_INIT_HIGH, "GPD0");
	gpio_free(EXYNOS4_GPD0(1));
	return 0;
}

int s3cfb_backlight_off(struct platform_device *pdev)
{
	gpio_request_one(EXYNOS4_GPD0(1), GPIOF_OUT_INIT_LOW, "GPD0");
	gpio_free(EXYNOS4_GPD0(1));
	return 0;
}

int s3cfb_lcd_on(struct platform_device *pdev)
{
	int err;

	err = gpio_request_one(EXYNOS4_GPC0(1), GPIOF_OUT_INIT_LOW, "GPC0");
	if (err) {
		printk(KERN_ERR "failed to request GPC0 for "
			"lcd backlight control\n");
		return err;
	}

	gpio_set_value(EXYNOS4_GPC0(1), GPIO_LEVEL_HIGH);
	msleep(40);

	/*  LVDS_N_SHDN to low */
	err = gpio_request_one(EXYNOS4212_GPM0(5), GPIOF_OUT_INIT_LOW, "GPM0");
	if (err) {
		printk(KERN_ERR "failed to request GPM0 for "
			"lcd backlight control\n");
		return err;
	}

	gpio_set_value(EXYNOS4212_GPM0(5), GPIO_LEVEL_HIGH);
	msleep(300);

	err = gpio_request_one(EXYNOS4212_GPM0(1), GPIOF_OUT_INIT_LOW, "GPM0");
	if (err) {
		printk(KERN_ERR "failed to request GPM0 for "
			"lcd backlight control\n");
		return err;
	}

	gpio_set_value(EXYNOS4212_GPM0(1), GPIO_LEVEL_HIGH);
	mdelay(2);
	return 0;
}

int s3cfb_lcd_off(struct platform_device *pdev)
{
	gpio_set_value(EXYNOS4212_GPM0(1), GPIO_LEVEL_LOW);
	mdelay(200);

	/*  LVDS_N_SHDN to low */
	gpio_set_value(EXYNOS4212_GPM0(5), GPIO_LEVEL_LOW);
	msleep(40);

	gpio_set_value(EXYNOS4_GPC0(1), GPIO_LEVEL_LOW);
	msleep(400);

	return 0;
}

#elif defined(CONFIG_FB_S5P_S6C1372) || defined(CONFIG_FB_S5P_S6F1202A)
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
#if !defined(CONFIG_FB_MDNIE_PWM)
	int err;

	err = gpio_request_one(EXYNOS4_GPD0(1), GPIOF_OUT_INIT_HIGH, "GPD0");
	if (err) {
		printk(KERN_ERR "failed to request GPD0 for lcd reset control\n");
		return err;
	}
	gpio_set_value(GPIO_LCD_EN, GPIO_LEVEL_HIGH);
	msleep(40);
	/* LVDS_N_SHDN to high*/
	gpio_set_value(GPIO_LVDS_NSHDN, GPIO_LEVEL_HIGH);
	msleep(300);
#if defined(CONFIG_FB_S5P_S6C1372)
	gpio_set_value(GPIO_LED_BACKLIGHT_RESET, GPIO_LEVEL_HIGH);
	mdelay(2);
#else
	gpio_set_value(GPIO_LCD_LDO_EN, GPIO_LEVEL_HIGH);
	msleep(200);
#endif
	gpio_set_value(EXYNOS4_GPD0(1), GPIO_LEVEL_HIGH);
	gpio_free(EXYNOS4_GPD0(1));
#endif
	return 0;
}

int s3cfb_lcd_off(struct platform_device *pdev)
{
#if !defined(CONFIG_FB_MDNIE_PWM)
	int err;

	err = gpio_request_one(EXYNOS4_GPD0(1), GPIOF_OUT_INIT_LOW, "GPD0");
	if (err) {
		printk(KERN_ERR "failed to request GPD0 for "
			"lcd reset control\n");
		return err;
	}

	/* LVDS_nSHDN low*/
	gpio_set_value(EXYNOS4_GPD0(1), GPIO_LEVEL_LOW);
	gpio_free(EXYNOS4_GPD0(1));
#if defined(CONFIG_FB_S5P_S6C1372)
	gpio_set_value(GPIO_LED_BACKLIGHT_RESET, GPIO_LEVEL_LOW);
	msleep(200);
#else
	gpio_set_value(GPIO_LCD_LDO_EN, GPIO_LEVEL_LOW);
	msleep(200);
#endif
	/* LVDS_nSHDN low*/
	gpio_set_value(GPIO_LVDS_NSHDN, GPIO_LEVEL_LOW);
	msleep(40);
	/* Disable LVDS Panel Power, 1.2, 1.8, display 3.3V */
	gpio_set_value(GPIO_LCD_EN, GPIO_LEVEL_LOW);
	msleep(400);
#endif
	return 0;
}

#else
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

#ifdef CONFIG_FB_S5P_MIPI_DSIM
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

int s3cfb_mdnie_clk_on(u32 rate)
{
	struct clk *sclk = NULL;
	struct clk *mout_mpll = NULL;
	struct clk *mdnie_clk = NULL;
	int ret = 0;

	mdnie_clk = clk_get(NULL, "mdnie0"); /*   CLOCK GATE IP ENABLE */
	if (IS_ERR(mdnie_clk)) {
		printk(KERN_ERR "failed to get ip clk for mdnie0\n");
		goto err_clk0;
	}
	clk_enable(mdnie_clk);
	clk_put(mdnie_clk);

	sclk = clk_get(NULL, "sclk_mdnie");
	if (IS_ERR(sclk)) {
		printk(KERN_ERR "failed to get sclk for mdnie\n");
		goto err_clk1;
	}

	if (soc_is_exynos4210())
		mout_mpll = clk_get(NULL, "mout_mpll");
	else
		mout_mpll = clk_get(NULL, "mout_mpll_user");

	if (IS_ERR(mout_mpll)) {
		printk(KERN_ERR "failed to get mout_mpll\n");
		goto err_clk2;
	}

	clk_set_parent(sclk, mout_mpll);

	if (!rate)
		rate = 800 * MHZ;

	ret = clk_set_rate(sclk, rate);

	clk_put(mout_mpll);

	clk_enable(sclk);

	return 0;

err_clk1:
	clk_put(mout_mpll);
err_clk2:
	clk_put(sclk);
err_clk0:
	clk_put(mdnie_clk);

	return -EINVAL;
}

int s3cfb_mdnie_pwm_clk_on(void)
{
	struct clk *sclk = NULL;
	struct clk *sclk_pre = NULL;
	struct clk *mout_mpll = NULL;
	u32 rate = 0;

	sclk = clk_get(NULL, "sclk_mdnie_pwm");
	if (IS_ERR(sclk)) {
		printk(KERN_ERR "failed to get sclk for mdnie_pwm\n");
		goto err_clk1;
	}

	sclk_pre = clk_get(NULL, "sclk_mdnie_pwm_pre");
	if (IS_ERR(sclk_pre)) {
		printk(KERN_ERR "failed to get sclk for mdnie_pwm_pre\n");
		goto err_clk2;
	}
#if defined(CONFIG_FB_S5P_S6C1372)
	mout_mpll = clk_get(NULL, "xusbxti");
	if (IS_ERR(mout_mpll)) {
		printk(KERN_ERR "failed to get mout_mpll\n");
		goto err_clk3;
	}
	clk_set_parent(sclk, mout_mpll);
	rate = clk_round_rate(sclk, 2200000);
	if (!rate)
		rate = 2200000;
	clk_set_rate(sclk, rate);
	printk(KERN_INFO "set mdnie_pwm sclk rate to %d\n", rate);
	clk_set_parent(sclk_pre, mout_mpll);
	rate = clk_round_rate(sclk_pre, 22000000);
	if (!rate)
		rate = 22000000;
	clk_set_rate(sclk_pre, rate);
#elif defined(CONFIG_FB_S5P_S6F1202A)
	if (soc_is_exynos4210())
		mout_mpll = clk_get(NULL, "mout_mpll");
	else
		mout_mpll = clk_get(NULL, "mout_mpll_user");
	if (IS_ERR(mout_mpll)) {
		printk(KERN_ERR "failed to get mout_mpll\n");
		goto err_clk3;
	}
	clk_set_parent(sclk, mout_mpll);
	rate = clk_round_rate(sclk, 50000000);
	if (!rate)
		rate = 50000000;
	clk_set_rate(sclk, rate);
	printk(KERN_INFO "set mdnie_pwm sclk rate to %d\n", rate);
	clk_set_parent(sclk_pre, mout_mpll);
	rate = clk_round_rate(sclk_pre, 160000000);
	if (!rate)
		rate = 160000000;
	clk_set_rate(sclk_pre, rate);
#else
	if (soc_is_exynos4210())
		mout_mpll = clk_get(NULL, "mout_mpll");
	else
		mout_mpll = clk_get(NULL, "mout_mpll_user");

	if (IS_ERR(mout_mpll)) {
		printk(KERN_ERR "failed to get mout_mpll\n");
		goto err_clk3;
	}

	clk_set_parent(sclk, mout_mpll);

	rate = 57500000;
	clk_set_rate(sclk, rate);
#endif
	printk(KERN_INFO "set mdnie_pwm sclk rate to %d\n", rate);

	clk_put(mout_mpll);

	clk_enable(sclk);

	return 0;

err_clk3:
	clk_put(mout_mpll);
err_clk2:
	clk_put(sclk_pre);
err_clk1:
	clk_put(sclk);

	return -EINVAL;
}

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

	if (!lcd->vclk) {
		rate = get_clk_rate(pdev, mout_mpll);
		if (!rate)
			rate = 800 * MHZ;	/* MOUT PLL */
		lcd->vclk = rate;
	} else
		rate = lcd->vclk;

	ret = clk_set_rate(sclk, rate);

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

int s3cfb_mdnie_clk_off(void)
{
	struct clk *sclk = NULL;
	struct clk *mdnie_clk = NULL;

	mdnie_clk = clk_get(NULL, "mdnie0"); /*   CLOCK GATE IP ENABLE */
	if (IS_ERR(mdnie_clk)) {
		printk(KERN_ERR "failed to get ip clk for fimd0\n");
		goto err_clk0;
	}
	clk_disable(mdnie_clk);
	clk_put(mdnie_clk);

	sclk = clk_get(NULL, "sclk_mdnie");
	if (IS_ERR(sclk))
		printk(KERN_ERR "failed to get sclk for mdnie\n");

	clk_disable(sclk);
	clk_put(sclk);

	return 0;

err_clk0:
	clk_put(mdnie_clk);

	return -EINVAL;
}

int s3cfb_mdnie_pwm_clk_off(void)
{
	struct clk *sclk = NULL;

	sclk = clk_get(NULL, "sclk_mdnie_pwm");
	if (IS_ERR(sclk))
		printk(KERN_ERR "failed to get sclk for mdnie_pwm\n");

	clk_disable(sclk);
	clk_put(sclk);

	return 0;
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
#ifdef CONFIG_FB_S5P_MDNIE
	s3cfb_mdnie_clk_off();
	s3cfb_mdnie_pwm_clk_off();
#endif

	return 0;

err_clk0:
	clk_put(lcd_clk);

	return -EINVAL;
}

void s3cfb_get_clk_name(char *clk_name)
{
	strcpy(clk_name, "sclk_fimd");
}

