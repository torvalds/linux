/* linux/arch/arm/mach-s5pv310/setup-fimc0.c
 *
 * Copyright (c) 2010 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * Base FIMC 0 gpio configuration
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/gpio.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <plat/clock.h>
#include <plat/gpio-cfg.h>
#include <mach/regs-gpio.h>
#include <plat/map-s5p.h>
#include <plat/cpu.h>
#include <mach/map.h>

struct platform_device; /* don't need the contents */

void s3c_fimc0_cfg_gpio(struct platform_device *pdev)
{
	if (soc_is_exynos4210()) {
		/* CAM A port(b0010) : PCLK, VSYNC, HREF, DATA[0-4] */
		s3c_gpio_cfgrange_nopull(EXYNOS4210_GPJ0(0), 8, S3C_GPIO_SFN(2));
		/* CAM A port(b0010) : DATA[5-7], CLKOUT(MIPI CAM also), FIELD */
		s3c_gpio_cfgrange_nopull(EXYNOS4210_GPJ1(0), 5, S3C_GPIO_SFN(2));
		/* CAM B port(b0011) : DATA[0-7] */
		s3c_gpio_cfgrange_nopull(EXYNOS4210_GPE1(0), 8, S3C_GPIO_SFN(3));
		/* CAM B port(b0011) : PCLK, VSYNC, HREF, FIELD, CLKOUT */
		s3c_gpio_cfgrange_nopull(EXYNOS4210_GPE0(0), 5, S3C_GPIO_SFN(3));
	} else {
		/* CAM A port(b0010) : PCLK, VSYNC, HREF, DATA[0-4] */
		s3c_gpio_cfgrange_nopull(EXYNOS4212_GPJ0(0), 8, S3C_GPIO_SFN(2));
		/* CAM A port(b0010) : DATA[5-7], CLKOUT(MIPI CAM also), FIELD */
#if defined(CONFIG_BOARD_ODROID_U)||defined(CONFIG_BOARD_ODROID_U2)
		s3c_gpio_cfgrange_nopull(EXYNOS4212_GPJ1(0), 5, S3C_GPIO_SFN(0));
#else
		s3c_gpio_cfgrange_nopull(EXYNOS4212_GPJ1(0), 5, S3C_GPIO_SFN(2));
#endif
		/* CAM B port(b0011) : PCLK, DATA[0-6] */
		s3c_gpio_cfgrange_nopull(EXYNOS4212_GPM0(0), 8, S3C_GPIO_SFN(3));
		/* CAM B port(b0011) : FIELD, DATA[7]*/
		s3c_gpio_cfgrange_nopull(EXYNOS4212_GPM1(0), 2, S3C_GPIO_SFN(3));
		/* CAM B port(b0011) : VSYNC, HREF, CLKOUT*/
		s3c_gpio_cfgrange_nopull(EXYNOS4212_GPM2(0), 3, S3C_GPIO_SFN(3));
	}
	/* note : driver strength to max is unnecessary */
}

int s3c_fimc_clk_on(struct platform_device *pdev, struct clk **clk)
{
	struct clk *sclk_fimc_lclk = NULL;

	sclk_fimc_lclk = clk_get(&pdev->dev, "sclk_fimc");
	if (IS_ERR(sclk_fimc_lclk)) {
		dev_err(&pdev->dev, "failed to get sclk_fimc_lclk\n");
		goto err_clk1;
	}

	/* be able to handle clock on/off only with this clock */
	*clk = clk_get(&pdev->dev, "fimc");
	if (IS_ERR(clk)) {
		dev_err(&pdev->dev, "failed to get interface clock\n");
		goto err_clk2;
	}

	clk_enable(*clk);
	clk_enable(sclk_fimc_lclk);

	return 0;

err_clk2:
	clk_put(sclk_fimc_lclk);
err_clk1:
	return -EINVAL;
}

int s3c_fimc_clk_off(struct platform_device *pdev, struct clk **clk)
{
	if (*clk != NULL) {
		clk_disable(*clk);
		clk_put(*clk);
		*clk = NULL;
	}

	return 0;
}
