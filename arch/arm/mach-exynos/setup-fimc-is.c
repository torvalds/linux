/* linux/arch/arm/mach-exynos/setup-fimc-is.c
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * FIMC-IS gpio and clock configuration
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
#include <linux/regulator/consumer.h>
#include <linux/delay.h>
#include <mach/regs-gpio.h>
#include <mach/map.h>
#include <mach/regs-clock.h>
#include <plat/clock.h>
#include <plat/gpio-cfg.h>
#include <plat/map-s5p.h>
#include <plat/cpu.h>
#include <media/exynos_fimc_is.h>
#include <mach/exynos-clock.h>

/*#define USE_UART_DEBUG*/

struct platform_device; /* don't need the contents */

#if defined(CONFIG_ARCH_EXYNOS4)
/*
 * Exynos4 series - FIMC-IS
 */
void exynos4_fimc_is_cfg_gpio(struct platform_device *pdev)
{
	int ret;
	/* 1. UART setting for FIMC-IS */
	/* GPM3[5] : TXD_UART_ISP */
	ret = gpio_request(EXYNOS4_GPM3(5), "GPM3");
	if (ret)
		printk(KERN_ERR "#### failed to request GPM3_5 ####\n");
	s3c_gpio_cfgpin(EXYNOS4_GPM3(5), (0x3<<20));
	s3c_gpio_setpull(EXYNOS4_GPM3(5), S3C_GPIO_PULL_NONE);
	gpio_free(EXYNOS4_GPM3(5));

	/* GPM3[7] : RXD_UART_ISP */
	ret = gpio_request(EXYNOS4_GPM3(7), "GPM3");
	if (ret)
		printk(KERN_ERR "#### failed to request GPM3_7 ####\n");
	s3c_gpio_cfgpin(EXYNOS4_GPM3(7), (0x3<<28));
	s3c_gpio_setpull(EXYNOS4_GPM3(7), S3C_GPIO_PULL_NONE);
	gpio_free(EXYNOS4_GPM3(7));

	/* 2. GPIO setting for FIMC-IS */
	ret = gpio_request(EXYNOS4_GPM4(0), "GPM4");
	if (ret)
		printk(KERN_ERR "#### failed to request GPM4_0 ####\n");
	s3c_gpio_cfgpin(EXYNOS4_GPM4(0), (0x2<<0));
	s3c_gpio_setpull(EXYNOS4_GPM4(0), S3C_GPIO_PULL_NONE);
	gpio_free(EXYNOS4_GPM4(0));

	ret = gpio_request(EXYNOS4_GPM4(1), "GPM4");
	if (ret)
		printk(KERN_ERR "#### failed to request GPM4_1 ####\n");
	s3c_gpio_cfgpin(EXYNOS4_GPM4(1), (0x2<<4));
	s3c_gpio_setpull(EXYNOS4_GPM4(1), S3C_GPIO_PULL_NONE);
	gpio_free(EXYNOS4_GPM4(1));

	ret = gpio_request(EXYNOS4_GPM4(2), "GPM4");
	if (ret)
		printk(KERN_ERR "#### failed to request GPM4_2 ####\n");
	s3c_gpio_cfgpin(EXYNOS4_GPM4(2), (0x2<<8));
	s3c_gpio_setpull(EXYNOS4_GPM4(2), S3C_GPIO_PULL_NONE);
	gpio_free(EXYNOS4_GPM4(2));

	ret = gpio_request(EXYNOS4_GPM4(3), "GPM4");
	if (ret)
		printk(KERN_ERR "#### failed to request GPM4_3 ####\n");
	s3c_gpio_cfgpin(EXYNOS4_GPM4(3), (0x2<<12));
	s3c_gpio_setpull(EXYNOS4_GPM4(3), S3C_GPIO_PULL_NONE);
	gpio_free(EXYNOS4_GPM4(3));
}

int exynos4_fimc_is_clk_get(struct platform_device *pdev)
{
	struct fimc_is_platform_data *pdata;
	pdata = to_fimc_is_plat(&pdev->dev);

	/* 1. Get clocks for CMU_ISP clock divider setting */
	/* UART_ISP_SEL - CLK_SRC_ISP (0x1003 C238) , [15:12] */
	pdata->div_clock[0] = clk_get(&pdev->dev, "mout_mpll_user");
	if (IS_ERR(pdata->div_clock[0])) {
		printk(KERN_ERR "failed to get mout_mpll_user\n");
		goto err_clk1;
	}
	/* UART_ISP_RATIO - CLK_DIV_ISP (0x1003 C538) , [31:28] */
	pdata->div_clock[1] = clk_get(&pdev->dev, "sclk_uart_isp");
	if (IS_ERR(pdata->div_clock[1])) {
		printk(KERN_ERR "failed to get sclk_uart_isp\n");
		goto err_clk2;
	}

	/* 2. Get clocks for CMU_ISP clock gate setting */
	/* CLK_UART_ISP - CLK_GATE_IP_ISP0 (0x1004 8800), [31] */
	pdata->control_clock[0] = clk_get(&pdev->dev, "uart_isp");
	if (IS_ERR(pdata->control_clock[0])) {
		printk(KERN_ERR "failed to get uart_isp\n");
		goto err_clk3;
	}
	/* CLK_WDT_ISP - CLK_GATE_IP_ISP0 (0x1004 8800), [30] */
	pdata->control_clock[1] = clk_get(&pdev->dev, "wdt_isp");
	if (IS_ERR(pdata->control_clock[1])) {
		printk(KERN_ERR "failed to get wdt_isp\n");
		goto err_clk4;
	}
	/* CLK_PWM_ISP - CLK_GATE_IP_ISP0 (0x1004 8800), [28] */
	pdata->control_clock[2] = clk_get(&pdev->dev, "pwm_isp");
	if (IS_ERR(pdata->control_clock[2])) {
		printk(KERN_ERR "failed to get pwm_isp\n");
		goto err_clk5;
	}
	/* CLK_MTCADC_ISP - CLK_GATE_IP_ISP0 (0x1004 8800), [27] */
	pdata->control_clock[3] = clk_get(&pdev->dev, "mtcadc");
	if (IS_ERR(pdata->control_clock[3])) {
		printk(KERN_ERR "failed to get mtcadc\n");
		goto err_clk6;
	}
	/* CLK_I2C1_ISP - CLK_GATE_IP_ISP0 (0x1004 8800), [26] */
	pdata->control_clock[4] = clk_get(&pdev->dev, "i2c1_isp");
	if (IS_ERR(pdata->control_clock[4])) {
		printk(KERN_ERR "failed to get i2c1_isp\n");
		goto err_clk7;
	}
	/* CLK_I2C0_ISP - CLK_GATE_IP_ISP0 (0x1004 8800), [25] */
	pdata->control_clock[5] = clk_get(&pdev->dev, "i2c0_isp");
	if (IS_ERR(pdata->control_clock[5])) {
		printk(KERN_ERR "failed to get i2c0_isp\n");
		goto err_clk8;
	}
	/* CLK_MPWM_ISP - CLK_GATE_IP_ISP0 (0x1004 8800), [24] */
	pdata->control_clock[6] = clk_get(&pdev->dev, "mpwm_isp");
	if (IS_ERR(pdata->control_clock[6])) {
		printk(KERN_ERR "failed to get mpwm_isp\n");
		goto err_clk9;
	}
	/* CLK_MCUCTL_ISP - CLK_GATE_IP_ISP0 (0x1004 8800), [23] */
	pdata->control_clock[7] = clk_get(&pdev->dev, "mcuctl_isp");
	if (IS_ERR(pdata->control_clock[7])) {
		printk(KERN_ERR "failed to get mcuctl_isp\n");
		goto err_clk10;
	}
	/* CLK_PPMUISPX  - CLK_GATE_IP_ISP0 (0x1004 8800), [21] */
	/* CLK_PPMUISPMX - CLK_GATE_IP_ISP0 (0x1004 8800), [20] */
	pdata->control_clock[8] = clk_get(&pdev->dev, "ppmuisp");
	if (IS_ERR(pdata->control_clock[8])) {
		printk(KERN_ERR "failed to get ppmuisp\n");
		goto err_clk11;
	}
	/* CLK_QE_LITE1 - CLK_GATE_IP_ISP0 (0x1004 8800), [18] */
	pdata->control_clock[9] = clk_get(&pdev->dev, "qelite1");
	if (IS_ERR(pdata->control_clock[9])) {
		printk(KERN_ERR "failed to get qelite1\n");
		goto err_clk12;
	}
	/* CLK_QE_LITE0 - CLK_GATE_IP_ISP0 (0x1004 8800), [17] */
	pdata->control_clock[10] = clk_get(&pdev->dev, "qelite0");
	if (IS_ERR(pdata->control_clock[10])) {
		printk(KERN_ERR "failed to get qelite0\n");
		goto err_clk13;
	}
	/* CLK_QE_FD - CLK_GATE_IP_ISP0 (0x1004 8800), [16] */
	pdata->control_clock[11] = clk_get(&pdev->dev, "qefd");
	if (IS_ERR(pdata->control_clock[11])) {
		printk(KERN_ERR "failed to get qefd\n");
		goto err_clk14;
	}
	/* CLK_QE_DRC - CLK_GATE_IP_ISP0 (0x1004 8800), [15] */
	pdata->control_clock[12] = clk_get(&pdev->dev, "qedrc");
	if (IS_ERR(pdata->control_clock[12])) {
		printk(KERN_ERR "failed to get qedrc\n");
		goto err_clk15;
	}
	/* CLK_QE_ISP - CLK_GATE_IP_ISP0 (0x1004 8800), [14] */
	pdata->control_clock[13] = clk_get(&pdev->dev, "qeisp");
	if (IS_ERR(pdata->control_clock[13])) {
		printk(KERN_ERR "failed to get qeisp\n");
		goto err_clk16;
	}
	/* CLK_SMMU_LITE1 - CLK_GATE_IP_ISP0 (0x1004 8800), [12] */
	pdata->control_clock[14] = clk_get(&pdev->dev, "sysmmu_lite1");
	if (IS_ERR(pdata->control_clock[14])) {
		printk(KERN_ERR "failed to get sysmmu_lite1\n");
		goto err_clk17;
	}
	/* CLK_SMMU_LITE0 - CLK_GATE_IP_ISP0 (0x1004 8800), [11] */
	pdata->control_clock[15] = clk_get(&pdev->dev, "sysmmu_lite0");
	if (IS_ERR(pdata->control_clock[15])) {
		printk(KERN_ERR "failed to get sysmmu_lite0\n");
		goto err_clk18;
	}
	/* CLK_SPI1_ISP - CLK_GATE_IP_ISP0 (0x1004 8804), [13] */
	pdata->control_clock[16] = clk_get(&pdev->dev, "spi1_isp");
	if (IS_ERR(pdata->control_clock[16])) {
		printk(KERN_ERR "failed to get spi1_isp\n");
		goto err_clk19;
	}
	/* CLK_SPI0_ISP - CLK_GATE_IP_ISP0 (0x1004 8804), [12] */
	pdata->control_clock[17] = clk_get(&pdev->dev, "spi0_isp");
	if (IS_ERR(pdata->control_clock[17])) {
		printk(KERN_ERR "failed to get spi0_isp\n");
		goto err_clk20;
	}
	/* CLK_SMMU_FD - CLK_GATE_IP_ISP0 (0x1004 8800), [10] */
	pdata->control_clock[18] = clk_get(&pdev->dev, "sysmmu_fd");
	if (IS_ERR(pdata->control_clock[18])) {
		printk(KERN_ERR "failed to get sysmmu_fd\n");
		goto err_clk21;
	}
	/* CLK_SMMU_DRC - CLK_GATE_IP_ISP0 (0x1004 8800), [9] */
	pdata->control_clock[19] = clk_get(&pdev->dev, "sysmmu_drc");
	if (IS_ERR(pdata->control_clock[19])) {
		printk(KERN_ERR "failed to get sysmmu_drc\n");
		goto err_clk22;
	}
	/* CLK_SMMU_ISP - CLK_GATE_IP_ISP0 (0x1004 8800), [8] */
	pdata->control_clock[20] = clk_get(&pdev->dev, "sysmmu_isp");
	if (IS_ERR(pdata->control_clock[20])) {
		printk(KERN_ERR "failed to get sysmmu_isp\n");
		goto err_clk23;
	}
	/* CLK_GICISP_ISP - CLK_GATE_IP_ISP0 (0x1004 8800), [7] */
	pdata->control_clock[21] = clk_get(&pdev->dev, "gic_isp");
	if (IS_ERR(pdata->control_clock[21])) {
		printk(KERN_ERR "failed to get gic_isp\n");
		goto err_clk24;
	}
	/* CLK_MCUISP - CLK_GATE_IP_ISP0 (0x1004 8800), [5] */
	pdata->control_clock[22] = clk_get(&pdev->dev, "mcu_isp");
	if (IS_ERR(pdata->control_clock[22])) {
		printk(KERN_ERR "failed to get mcu_isp\n");
		goto err_clk25;
	}
	/* CLK_LITE1 - CLK_GATE_IP_ISP0 (0x1004 8800), [4] */
	pdata->control_clock[23] = clk_get(&pdev->dev, "lite1");
	if (IS_ERR(pdata->control_clock[23])) {
		printk(KERN_ERR "failed to get lite1\n");
		goto err_clk26;
	}
	/* CLK_LITE0 - CLK_GATE_IP_ISP0 (0x1004 8800), [3] */
	pdata->control_clock[24] = clk_get(&pdev->dev, "lite0");
	if (IS_ERR(pdata->control_clock[24])) {
		printk(KERN_ERR "failed to get lite0\n");
		goto err_clk27;
	}
	/* CLK_FD - CLK_GATE_IP_ISP0 (0x1004 8800), [2] */
	pdata->control_clock[25] = clk_get(&pdev->dev, "fd");
	if (IS_ERR(pdata->control_clock[25])) {
		printk(KERN_ERR "failed to get fd\n");
		goto err_clk28;
	}
	/* CLK_DRC - CLK_GATE_IP_ISP0 (0x1004 8800), [1] */
	pdata->control_clock[26] = clk_get(&pdev->dev, "drc");
	if (IS_ERR(pdata->control_clock[26])) {
		printk(KERN_ERR "failed to get drc\n");
		goto err_clk29;
	}
	/* CLK_ISP - CLK_GATE_IP_ISP0 (0x1004 8800), [0] */
	pdata->control_clock[27] = clk_get(&pdev->dev, "isp");
	if (IS_ERR(pdata->control_clock[27])) {
		printk(KERN_ERR "failed to get isp\n");
		goto err_clk30;
	}
	/* CLK_SPI1_ISP - CLK_GATE_IP_ISP0 (0x1004 8804), [13] */
	pdata->control_clock[28] = clk_get(&pdev->dev, "spi1_isp");
	if (IS_ERR(pdata->control_clock[28])) {
		printk(KERN_ERR "failed to get spi1_isp\n");
		goto err_clk31;
	}
	/* CLK_SPI0_ISP - CLK_GATE_IP_ISP0 (0x1004 8804), [12] */
	pdata->control_clock[29] = clk_get(&pdev->dev, "spi0_isp");
	if (IS_ERR(pdata->control_clock[29])) {
		printk(KERN_ERR "failed to get spi0_isp\n");
		goto err_clk32;
	}
	/* CLK_SMMU_ISPCX - CLK_GATE_IP_ISP0 (0x1004 8804), [4] */
	pdata->control_clock[30] = clk_get(&pdev->dev, "sysmmu_ispcx");
	if (IS_ERR(pdata->control_clock[30])) {
		printk(KERN_ERR "failed to get sysmmu_ispcx\n");
		goto err_clk33;
	}
	/* CLK_ASYNCAXIM - CLK_GATE_IP_ISP0 (0x1004 8804), [0] */
	return 0;

err_clk33:
	clk_put(pdata->control_clock[29]);
err_clk32:
	clk_put(pdata->control_clock[28]);
err_clk31:
	clk_put(pdata->control_clock[27]);
err_clk30:
	clk_put(pdata->control_clock[26]);
err_clk29:
	clk_put(pdata->control_clock[25]);
err_clk28:
	clk_put(pdata->control_clock[24]);
err_clk27:
	clk_put(pdata->control_clock[23]);
err_clk26:
	clk_put(pdata->control_clock[22]);
err_clk25:
	clk_put(pdata->control_clock[21]);
err_clk24:
	clk_put(pdata->control_clock[20]);
err_clk23:
	clk_put(pdata->control_clock[19]);
err_clk22:
	clk_put(pdata->control_clock[18]);
err_clk21:
	clk_put(pdata->control_clock[17]);
err_clk20:
	clk_put(pdata->control_clock[16]);
err_clk19:
	clk_put(pdata->control_clock[15]);
err_clk18:
	clk_put(pdata->control_clock[14]);
err_clk17:
	clk_put(pdata->control_clock[13]);
err_clk16:
	clk_put(pdata->control_clock[12]);
err_clk15:
	clk_put(pdata->control_clock[11]);
err_clk14:
	clk_put(pdata->control_clock[10]);
err_clk13:
	clk_put(pdata->control_clock[9]);
err_clk12:
	clk_put(pdata->control_clock[8]);
err_clk11:
	clk_put(pdata->control_clock[7]);
err_clk10:
	clk_put(pdata->control_clock[6]);
err_clk9:
	clk_put(pdata->control_clock[5]);
err_clk8:
	clk_put(pdata->control_clock[4]);
err_clk7:
	clk_put(pdata->control_clock[3]);
err_clk6:
	clk_put(pdata->control_clock[2]);
err_clk5:
	clk_put(pdata->control_clock[1]);
err_clk4:
	clk_put(pdata->control_clock[0]);
err_clk3:
	clk_put(pdata->div_clock[1]);
err_clk2:
	clk_put(pdata->div_clock[0]);
err_clk1:
	return -EINVAL;
}

int exynos4_fimc_is_cfg_clk(struct platform_device *pdev)
{
	struct fimc_is_platform_data *pdata;
	unsigned int tmp;
	pdata = to_fimc_is_plat(&pdev->dev);

	/* 1. MCUISP */
	__raw_writel(0x00000011, EXYNOS4_CLKDIV_ISP0);
	/* 2. ACLK_ISP */
	__raw_writel(0x00000030, EXYNOS4_CLKDIV_ISP1);
	/* 3. Set mux - CLK_SRC_TOP1(0x1003 C214) [24],[20] */
	tmp = __raw_readl(EXYNOS4_CLKSRC_TOP1);
	tmp |= (0x1 << EXYNOS4_CLKDIV_TOP1_ACLK200_SUB_SHIFT |
		0x1 << EXYNOS4_CLKDIV_TOP1_ACLK400_MCUISP_SUB_SHIFT);
	__raw_writel(tmp, EXYNOS4_CLKSRC_TOP1);

	/* 4. UART-ISP */
	clk_set_parent(pdata->div_clock[UART_ISP_RATIO],
					pdata->div_clock[UART_ISP_SEL]);
	clk_set_rate(pdata->div_clock[UART_ISP_RATIO], 50 * 1000000);

	return 0;
}

int exynos4_fimc_is_clk_on(struct platform_device *pdev)
{
	struct fimc_is_platform_data *pdata;
	int i;
	pdata = to_fimc_is_plat(&pdev->dev);

	/* 1. CLK_GATE_IP_ISP (0x1003 C938) */
#if defined(CONFIG_MACH_SMDK4212) || defined(CONFIG_MACH_SMDK4412)
	clk_enable(pdata->div_clock[UART_ISP_RATIO]);
#endif
	/* 2. CLK_GATE_IP_ISP0, CLK_GATE_IP_ISP1 (0x1004 8800) (0x1004 8804) */
	for (i = 0; i < FIMC_IS_MAX_CONTROL_CLOCKS; i++)
		clk_enable(pdata->control_clock[i]);

	return 0;
}

int exynos4_fimc_is_clk_off(struct platform_device *pdev)
{
	struct fimc_is_platform_data *pdata;
	pdata = to_fimc_is_plat(&pdev->dev);
	int i;

	/* 1. CLK_GATE_IP_ISP (0x1003 C938)*/
#if defined(CONFIG_MACH_SMDK4212) || defined(CONFIG_MACH_SMDK4412)
	clk_disable(pdata->div_clock[UART_ISP_RATIO]);
#endif

	/* 2. CLK_GATE_IP_ISP0, CLK_GATE_IP_ISP1 (0x1004 8800) (0x1004 8804) */
	for (i = 0; i < FIMC_IS_MAX_CONTROL_CLOCKS; i++)
		clk_disable(pdata->control_clock[i]);

	return 0;
}

int exynos4_fimc_is_clk_put(struct platform_device *pdev)
{
	struct fimc_is_platform_data *pdata;
	int i;
	pdata = to_fimc_is_plat(&pdev->dev);

	for (i = 0; i < FIMC_IS_MAX_DIV_CLOCKS; i++)
		clk_put(pdata->div_clock[i]);
	for (i = 0; i < FIMC_IS_MAX_CONTROL_CLOCKS; i++)
		clk_put(pdata->control_clock[i]);
	return 0;
}
#elif defined(CONFIG_ARCH_EXYNOS5)
/*------------------------------------------------------*/
/*		Exynos5 series - FIMC-IS		*/
/*------------------------------------------------------*/
int exynos5_fimc_is_gpio(struct platform_device *pdev, struct gpio_set *gpio, int flag_on)
{
	int ret = 0;

	pr_debug("exynos5_fimc_is_gpio\n");

	ret = gpio_request(gpio->pin, gpio->name);
	if (ret) {
		pr_err("Request GPIO error(%s)\n", gpio->name);
		return ret;
	}

	if (flag_on == 1) {
		switch (gpio->act) {
		case GPIO_PULL_NONE:
			s3c_gpio_cfgpin(gpio->pin, gpio->value);
			s3c_gpio_setpull(gpio->pin, S3C_GPIO_PULL_NONE);
			/* set max strength */
			if (strstr(gpio->name, "SDA") || strstr(gpio->name, "SCL"))
				s5p_gpio_set_drvstr(gpio->pin, S5P_GPIO_DRVSTR_LV4);
			break;
		case GPIO_OUTPUT:
			s3c_gpio_cfgpin(gpio->pin, S3C_GPIO_OUTPUT);
			s3c_gpio_setpull(gpio->pin, S3C_GPIO_PULL_NONE);
			if (flag_on == 1)
				gpio_set_value(gpio->pin, gpio->value);
			else
				gpio_set_value(gpio->pin, !gpio->value);
			break;
		case GPIO_INPUT:
			s3c_gpio_cfgpin(gpio->pin, S3C_GPIO_INPUT);
			s3c_gpio_setpull(gpio->pin, S3C_GPIO_PULL_NONE);
			gpio_set_value(gpio->pin, gpio->value);
			break;
		case GPIO_RESET:
			s3c_gpio_setpull(gpio->pin, S3C_GPIO_PULL_NONE);
			gpio_direction_output(gpio->pin, 0);
			gpio_direction_output(gpio->pin, 1);
			break;
		default:
			pr_err("unknown act for gpio\n");
			break;
		}
	} else {
		s3c_gpio_cfgpin(gpio->pin, S3C_GPIO_INPUT);
		s3c_gpio_setpull(gpio->pin, S3C_GPIO_PULL_DOWN);
	}

	gpio_free(gpio->pin);

	return ret;
}

int exynos5_fimc_is_regulator(struct platform_device *pdev, struct gpio_set *gpio, int flag_on)
{
	int ret = 0;

	if (soc_is_exynos5410()) {
		struct regulator *regulator = NULL;

		if (flag_on == 1) {
			regulator = regulator_get(&(pdev->dev), gpio->name);
			if (IS_ERR(regulator)) {
				pr_err("%s : regulator_get(%s) fail\n",
					__func__, gpio->name);
				return PTR_ERR(regulator);
			} else if (!regulator_is_enabled(regulator)) {
				ret = regulator_enable(regulator);
				if (ret) {
					pr_err("%s : regulator_enable(%s) fail\n",
						__func__, gpio->name);
					regulator_put(regulator);
					return ret;
				}
			}
			regulator_put(regulator);
		} else {
			regulator = regulator_get(&(pdev->dev), gpio->name);
			if (IS_ERR(regulator)) {
				pr_err("%s : regulator_get(%s) fail\n",
					__func__, gpio->name);
				return PTR_ERR(regulator);
			} else if (regulator_is_enabled(regulator)) {
				ret = regulator_disable(regulator);
				if (ret) {
					pr_err("%s : regulator_disable(%s) fail\n",
						__func__, gpio->name);
					regulator_put(regulator);
					return ret;
				}
			}
			regulator_put(regulator);
		}
	}

	return ret;
}

static int exynos5_fimc_is_pin_cfg_exception(struct platform_device *pdev,
	int channel, int flag_on, char *gpio_list[], int gpio_list_size)
{
	int ret = 0;
	int i = 0;
	int j = 0;
	int flag_found = 0;

	struct exynos5_platform_fimc_is *dev = pdev->dev.platform_data;
	struct gpio_set *gpio;

	for (i = FIMC_IS_MAX_GPIO_NUM - 1; i >= 0; i--) {
		gpio = &dev->gpio_info->cfg[i];

		if (!gpio->pin_type)
			continue;

		if (gpio->flite_id != FLITE_ID_END &&
			gpio->flite_id != channel)
			continue;

		if (gpio->count == 1) {
			flag_found = 0;

			for (j = 0; j < gpio_list_size; j++) {
				if (strcmp(gpio_list[j], gpio->name) == 0)
					flag_found = 1;
			}

			if (flag_found == 1) {
				switch(gpio->pin_type) {
				case PIN_GPIO:
					ret = exynos5_fimc_is_gpio(pdev, gpio, flag_on);
					if (ret)
						pr_err("%s : exynos5_fimc_is_gpio failed\n", __func__);
					break;
				case PIN_REGULATOR:
					ret = exynos5_fimc_is_regulator(pdev, gpio, flag_on);
					if (ret)
						pr_err("%s : exynos5_fimc_is_regulator failed\n", __func__);
					break;
				default:
					break;
				}

				if (ret == 0 && 0 < gpio->count)
					gpio->count--;
			}
		}
	}
	return ret;
}

int exynos5_fimc_is_pin_cfg(struct platform_device *pdev, u32 channel, bool flag_on)
{
	int ret = 0;
	int i = 0;

	struct exynos5_platform_fimc_is *dev = pdev->dev.platform_data;
	struct gpio_set *gpio;

	pr_debug("exynos5_fimc_is_pin_cfg\n");

	if (dev->flag_power_on[channel] == flag_on) {
		pr_warn("sensor ch%d is already set(%d)", channel, flag_on);
		goto p_err;
	} else {
		dev->flag_power_on[channel] = flag_on;
	}

	if (flag_on == 1) {
		for (i = 0; i < FIMC_IS_MAX_GPIO_NUM; i++) {
			gpio = &dev->gpio_info->cfg[i];

			if (!gpio->pin_type)
				continue;

			if (gpio->flite_id != FLITE_ID_END &&
				gpio->flite_id != channel)
				continue;

			if (gpio->count == 0) {
				/* insert timing */
				/* iT0 */
				if (strcmp("cam_isp_sensor_io_1.8v", gpio->name) == 0)
					usleep_range(2000, 2000);
				/* iT1 */
				if (strcmp("GPIO_MAIN_CAM_RESET", gpio->name) == 0)
					usleep_range(2000, 2000);

				/* vtT0 */
				if (strcmp("GPIO_CAM_VT_nRST", gpio->name) == 0)
					usleep_range(1000, 1000);

				switch(gpio->pin_type) {
				case PIN_GPIO:
					ret = exynos5_fimc_is_gpio(pdev, gpio, flag_on);
					if (ret)
						pr_err("%s : exynos5_fimc_is_gpio failed\n", __func__);
					break;
				case PIN_REGULATOR:
					ret = exynos5_fimc_is_regulator(pdev, gpio, flag_on);
					if (ret)
						pr_err("%s : exynos5_fimc_is_regulator failed\n", __func__);
					break;
				default:
					break;
				}

				/* iT2 + iT3*/
				if (strcmp("GPIO_MAIN_CAM_RESET", gpio->name) == 0)
					usleep_range(1000, 1000);

				/* vtT1 + vtT2 */
				if (strcmp("GPIO_VT_CAM_MCLK", gpio->name) == 0)
					usleep_range(1000, 1000);
			}

			if (ret == 0)
				gpio->count++;
		}
	} else {
		for (i = FIMC_IS_MAX_GPIO_NUM - 1; i >= 0; i--) {
			gpio = &dev->gpio_info->cfg[i];

			if (!gpio->pin_type)
				continue;

			if (gpio->flite_id != FLITE_ID_END &&
				gpio->flite_id != channel)
				continue;

			if (gpio->count == 1) {
				/* insert timing */
				if (strcmp("GPIO_MAIN_CAM_RESET", gpio->name) == 0) {
					/* GPIO_CAM_AF_EN and cam_af_2.8v_pm must turn off before GPIO_MAIN_CAM_RESET */
					char *pic_cfg_exception_af[] = {
						"GPIO_CAM_AF_EN",
						"cam_af_2.8v_pm",
					};

					/* GPIO_CAM_MCLK and must turn off before cGPIO_MAIN_CAM_RESET */
					char *pic_cfg_exception_mclk[] = {
						"GPIO_CAM_MCLK",
					};

					ret = exynos5_fimc_is_pin_cfg_exception(pdev, channel, flag_on, pic_cfg_exception_af, 2);
					if (ret)
						pr_err("%s : exynos5_fimc_is_pin_cfg_exception failed\n", __func__);

					ret = exynos5_fimc_is_pin_cfg_exception(pdev, channel, flag_on, pic_cfg_exception_mclk, 1);
					if (ret)
						pr_err("%s : exynos5_fimc_is_pin_cfg_exception failed\n", __func__);
				}
				/* iT4 */
				if (strcmp("cam_isp_sensor_io_1.8v", gpio->name) == 0)
					usleep_range(9, 9);

				switch(gpio->pin_type) {
				case PIN_GPIO:
					ret = exynos5_fimc_is_gpio(pdev, gpio, flag_on);
					if (ret)
						pr_err("%s : exynos5_fimc_is_gpio failed\n", __func__);
					break;
				case PIN_REGULATOR:
					ret = exynos5_fimc_is_regulator(pdev, gpio, flag_on);
					if (ret)
						pr_err("%s : exynos5_fimc_is_regulator failed\n", __func__);
					break;
				default:
					break;
				}
			}

			if (ret == 0 && 0 < gpio->count)
				gpio->count--;
		}
	}

p_err:
	return ret;

}

int exynos5_fimc_is_cfg_gpio(struct platform_device *pdev,
				u32 channel, bool flag_on)
{
	int ret = 0;

	ret = exynos5_fimc_is_pin_cfg(pdev, channel, flag_on);
	if (ret) {
		pr_err("%s : exynos5_fimc_is_pin_cfg(%d) failed\n",
			__func__, flag_on);
		goto exit;
	}

exit:
	return ret;
}

int exynos5_fimc_is_print_cfg(struct platform_device *pdev, u32 channel)
{
	int ret = 0;
	u32 i;
	struct exynos5_platform_fimc_is *dev;
	struct gpio_set *gpio;
	struct regulator *regulator;

	if (!pdev) {
		pr_err("pdev is NULL\n");
		ret = -EINVAL;
		goto p_err;
	}

	dev = pdev->dev.platform_data;
	if (!dev) {
		pr_err("dev is NULL\n");
		ret = -EINVAL;
		goto p_err;
	}

	for (i = 0; i < FIMC_IS_MAX_GPIO_NUM; i++) {
		gpio = &dev->gpio_info->cfg[i];

		if (gpio->flite_id != channel)
			continue;

		if (gpio->pin_type == PIN_GPIO) {
			ret = gpio_request(gpio->pin, gpio->name);
			if (ret) {
				pr_err("Request GPIO error(%s)\n", gpio->name);
				ret = -EINVAL;
				goto p_err;
			}

			pr_err("%s cfg : %08X\n", gpio->name,
					s3c_gpio_getcfg(gpio->pin));
			pr_err("%s pud : %08X\n", gpio->name,
					s3c_gpio_getpull(gpio->pin));
			pr_err("%s str : %08X\n", gpio->name,
					s5p_gpio_get_drvstr(gpio->pin));
			pr_err("%s val : %08X\n", gpio->name,
					gpio_get_value(gpio->pin));
			pr_err("%s cnt : %08X\n", gpio->name,
					gpio->count);

			gpio_free(gpio->pin);
		} else if (gpio->pin_type == PIN_REGULATOR) {
			regulator = regulator_get(&(pdev->dev), gpio->name);
			if (IS_ERR(regulator)) {
				pr_err("%s : regulator_get(%s) fail\n",
						__func__, gpio->name);
				ret = PTR_ERR(regulator);
				goto p_err;
			}

			pr_err("%s pwr : %08X\n", gpio->name,
					regulator_is_enabled(regulator));

			regulator_put(regulator);
		} else {
			pr_err("pin type is invalid(%d)\n", gpio->pin_type);
		}
	}

p_err:
	return ret;
}

static int cfg_gpio(struct gpio_set *gpio, int value)
{
	int ret;

	pr_debug("gpio.pin:%d gpio.name:%s\n", gpio->pin, gpio->name);
	ret = gpio_request(gpio->pin, gpio->name);
	if (ret) {
		pr_err("Request GPIO error(%s)\n", gpio->name);
		return -1;
	}

	switch (gpio->act) {
	case GPIO_PULL_NONE:
		s3c_gpio_cfgpin(gpio->pin, value);
		s3c_gpio_setpull(gpio->pin, S3C_GPIO_PULL_NONE);
		break;
	case GPIO_OUTPUT:
		s3c_gpio_cfgpin(gpio->pin, S3C_GPIO_OUTPUT);
		s3c_gpio_setpull(gpio->pin, S3C_GPIO_PULL_NONE);
		gpio_set_value(gpio->pin, value);
		break;
	case GPIO_RESET:
		s3c_gpio_setpull(gpio->pin, S3C_GPIO_PULL_NONE);
		gpio_direction_output(gpio->pin, value);
		break;
	default:
		pr_err("unknown act for gpio\n");
		return -1;
	}
	gpio_free(gpio->pin);

	return 0;

}

static int power_control_sensor(char *regulator_name, int on)
{
	struct regulator *regulator = NULL;

	pr_debug("regulator:%s on:%d\n", regulator_name, on);
	regulator = regulator_get(NULL, regulator_name);
	if (IS_ERR(regulator)) {
		pr_err("%s : regulator_get fail\n", __func__);
		return PTR_ERR(regulator);
	}

	if (on)
		regulator_enable(regulator);
	else{
		if (regulator_is_enabled(regulator))
			regulator_disable(regulator);
	}

	regulator_put(regulator);
	return 0;
}

int exynos5_fimc_is_cfg_clk(struct platform_device *pdev)
{
	pr_debug("exynos5_fimc_is_cfg_clk\n");
	if (soc_is_exynos5250()) {
		struct clk *aclk_mcuisp = NULL;
		struct clk *aclk_266 = NULL;
		struct clk *aclk_mcuisp_div0 = NULL;
		struct clk *aclk_mcuisp_div1 = NULL;
		struct clk *aclk_266_div0 = NULL;
		struct clk *aclk_266_div1 = NULL;
		struct clk *aclk_266_mpwm = NULL;
#ifdef USE_UART_DEBUG
		struct clk *sclk_uart_isp = NULL;
		struct clk *sclk_uart_isp_div = NULL;
		unsigned long isp_uart;
#endif
		struct clk *mout_mpll = NULL;
		struct clk *sclk_mipi0 = NULL;
		struct clk *sclk_mipi1 = NULL;
		struct clk *cam_src = NULL;
		struct clk *cam_A_clk = NULL;
		unsigned long mcu_isp_400;
		unsigned long isp_266;
		unsigned long mipi;
		unsigned long xxti;

		/* 1. MCUISP */
		aclk_mcuisp = clk_get(&pdev->dev, "aclk_400_isp");
		if (IS_ERR(aclk_mcuisp))
			return PTR_ERR(aclk_mcuisp);

		aclk_mcuisp_div0 = clk_get(&pdev->dev, "aclk_400_isp_div0");
		if (IS_ERR(aclk_mcuisp_div0)) {
			clk_put(aclk_mcuisp);
			return PTR_ERR(aclk_mcuisp_div0);
		}

		aclk_mcuisp_div1 = clk_get(&pdev->dev, "aclk_400_isp_div1");
		if (IS_ERR(aclk_mcuisp_div1)) {
			clk_put(aclk_mcuisp);
			clk_put(aclk_mcuisp_div0);
			return PTR_ERR(aclk_mcuisp_div1);
		}

		clk_set_rate(aclk_mcuisp_div0, 200 * 1000000);
		clk_set_rate(aclk_mcuisp_div1, 100 * 1000000);

		mcu_isp_400 = clk_get_rate(aclk_mcuisp);
		pr_debug("mcu_isp_400 : %ld\n", mcu_isp_400);

		mcu_isp_400 = clk_get_rate(aclk_mcuisp_div0);
		pr_debug("mcu_isp_400_div0 : %ld\n", mcu_isp_400);

		mcu_isp_400 = clk_get_rate(aclk_mcuisp_div1);
		pr_debug("aclk_mcuisp_div1 : %ld\n", mcu_isp_400);

		clk_put(aclk_mcuisp);
		clk_put(aclk_mcuisp_div0);
		clk_put(aclk_mcuisp_div1);

		/* 2. ACLK_ISP */
		aclk_266 = clk_get(&pdev->dev, "aclk_266_isp");
		if (IS_ERR(aclk_266))
			return PTR_ERR(aclk_266);

		aclk_266_div0 = clk_get(&pdev->dev, "aclk_266_isp_div0");
		if (IS_ERR(aclk_266_div0)) {
			clk_put(aclk_266);
			return PTR_ERR(aclk_266_div0);
		}

		aclk_266_div1 = clk_get(&pdev->dev, "aclk_266_isp_div1");
		if (IS_ERR(aclk_266_div1)) {
			clk_put(aclk_266);
			clk_put(aclk_266_div0);
			return PTR_ERR(aclk_266_div1);
		}

		aclk_266_mpwm = clk_get(&pdev->dev, "aclk_266_isp_divmpwm");
		if (IS_ERR(aclk_266_mpwm)) {
			clk_put(aclk_266);
			clk_put(aclk_266_div0);
			clk_put(aclk_266_div1);
			return PTR_ERR(aclk_266_mpwm);
		}

		clk_set_rate(aclk_266_div0, 134 * 1000000);
		clk_set_rate(aclk_266_div1, 68 * 1000000);
		clk_set_rate(aclk_266_mpwm, 34 * 1000000);

		isp_266 = clk_get_rate(aclk_266);
		pr_debug("isp_266 : %ld\n", isp_266);

		isp_266 = clk_get_rate(aclk_266_div0);
		pr_debug("isp_266_div0 : %ld\n", isp_266);

		isp_266 = clk_get_rate(aclk_266_div1);
		pr_debug("isp_266_div1 : %ld\n", isp_266);

		isp_266 = clk_get_rate(aclk_266_mpwm);
		pr_debug("isp_266_mpwm : %ld\n", isp_266);

		clk_put(aclk_266);
		clk_put(aclk_266_div0);
		clk_put(aclk_266_div1);
		clk_put(aclk_266_mpwm);

#ifdef USE_UART_DEBUG
		/* 3. UART-ISP */
		sclk_uart_isp = clk_get(&pdev->dev, "sclk_uart_src_isp");
		if (IS_ERR(sclk_uart_isp))
			return PTR_ERR(sclk_uart_isp);

		sclk_uart_isp_div = clk_get(&pdev->dev, "sclk_uart_isp");
		if (IS_ERR(sclk_uart_isp_div)) {
			clk_put(sclk_uart_isp);
			return PTR_ERR(sclk_uart_isp_div);
		}

		clk_set_parent(sclk_uart_isp, clk_get(&pdev->dev, "mout_mpll_user"));
		clk_set_parent(sclk_uart_isp_div, sclk_uart_isp);
		clk_set_rate(sclk_uart_isp_div, 50 * 1000000);

		isp_uart = clk_get_rate(sclk_uart_isp);
		pr_debug("isp_uart : %ld\n", isp_uart);
		isp_uart = clk_get_rate(sclk_uart_isp_div);
		pr_debug("isp_uart_div : %ld\n", isp_uart);

		clk_put(sclk_uart_isp);
		clk_put(sclk_uart_isp_div);
#endif

		/* 4. MIPI-CSI */
		mout_mpll = clk_get(&pdev->dev, "mout_mpll_user");
		if (IS_ERR(mout_mpll))
			return PTR_ERR(mout_mpll);

		sclk_mipi0 = clk_get(&pdev->dev, "sclk_gscl_wrap0");
		if (IS_ERR(sclk_mipi0)) {
			clk_put(mout_mpll);
			return PTR_ERR(sclk_mipi0);
		}

		clk_set_parent(sclk_mipi0, mout_mpll);
		clk_set_rate(sclk_mipi0, 267 * 1000000);

		clk_put(mout_mpll);
		clk_put(sclk_mipi0);

		mout_mpll = clk_get(&pdev->dev, "mout_mpll_user");
		if (IS_ERR(mout_mpll))
			return PTR_ERR(mout_mpll);

		sclk_mipi1 = clk_get(&pdev->dev, "sclk_gscl_wrap1");
		if (IS_ERR(sclk_mipi1)) {
			clk_put(mout_mpll);
			return PTR_ERR(sclk_mipi1);
		}

		clk_set_parent(sclk_mipi1, mout_mpll);
		clk_set_rate(sclk_mipi1, 267 * 1000000);

		mipi = clk_get_rate(mout_mpll);
		pr_debug("mipi_src : %ld\n", mipi);
		mipi = clk_get_rate(sclk_mipi1);
		pr_debug("mipi_div : %ld\n", mipi);

		clk_put(mout_mpll);
		clk_put(sclk_mipi1);

		/* 5. Camera A */
		cam_src = clk_get(&pdev->dev, "xxti");
		if (IS_ERR(cam_src))
			return PTR_ERR(cam_src);

		cam_A_clk = clk_get(&pdev->dev, "sclk_cam0");
		if (IS_ERR(cam_A_clk)) {
			clk_put(cam_src);
			return PTR_ERR(cam_A_clk);
		}

		xxti = clk_get_rate(cam_src);
		pr_debug("xxti : %ld\n", xxti);

		clk_set_parent(cam_A_clk, cam_src);
		clk_set_rate(cam_A_clk, 24 * 1000000);

		clk_put(cam_src);
		clk_put(cam_A_clk);

		/* 6. Camera B */
		cam_src = clk_get(&pdev->dev, "xxti");
		if (IS_ERR(cam_src))
			return PTR_ERR(cam_src);

		cam_A_clk = clk_get(&pdev->dev, "sclk_bayer");
		if (IS_ERR(cam_A_clk)) {
			clk_put(cam_src);
			return PTR_ERR(cam_A_clk);
		}

		xxti = clk_get_rate(cam_src);
		pr_debug("xxti : %ld\n", xxti);

		clk_set_parent(cam_A_clk, cam_src);
		clk_set_rate(cam_A_clk, 24 * 1000000);

		clk_put(cam_src);
		clk_put(cam_A_clk);
	} else if (soc_is_exynos5410()) {
		int cfg;
		struct clk *mout_bpll_user = NULL;
		struct clk *aclk_400_isp_pre = NULL;
		struct clk *aclk_mcuisp = NULL;
		struct clk *aclk_266_pre = NULL;
		struct clk *aclk_266 = NULL;
		struct clk *aclk_mcuisp_div0 = NULL;
		struct clk *aclk_mcuisp_div1 = NULL;
		struct clk *dout_aclk_333_432 = NULL;
		struct clk *aclk_333_432 = NULL;
		struct clk *isp_div0 = NULL;
		struct clk *isp_div1 = NULL;
		struct clk *mpwm_div = NULL;
		struct clk *dout_aclk_333_432_gscl = NULL;
		struct clk *aclk_333_432_gscl = NULL;
		struct clk *pclk_166_gscl = NULL;
		struct clk *sclk_pwm_isp = NULL;
		struct clk *sclk_uart_isp = NULL;
		struct clk *sclk_spi1_isp = NULL;
		struct clk *sclk_spi1_isp_pre = NULL;
		struct clk *sclk_spi0_isp = NULL;
		struct clk *sclk_spi0_isp_pre = NULL;
		struct clk *sclk_mout_isp_sensor = NULL;
		struct clk *sclk_isp_sensor0 = NULL;
		struct clk *sclk_isp_sensor1 = NULL;
		struct clk *sclk_isp_sensor2 = NULL;
		unsigned long mcu_isp_400;
		unsigned long isp_266;
		unsigned long isp_uart;
		unsigned long isp_pwm;
		unsigned long isp_spi1;
		unsigned long isp_spi1_pre;
		unsigned long isp_spi0;
		unsigned long isp_spi0_pre;
		unsigned long isp_sensor0;
		unsigned long isp_sensor1;
		unsigned long isp_sensor2;

		/* initialize Clocks */

		/*
		 * HACK: hard clock setting to preventing
		 * ISP init fail problem
		 */
		writel(0x31, EXYNOS5_CLKDIV_ISP0);
		writel(0x31, EXYNOS5_CLKDIV_ISP1);
		writel(0x1, EXYNOS5_CLKDIV_ISP2);
		cfg = readl(EXYNOS5_CLKDIV2_RATIO0);
		cfg |= (0x1 < 6);
		writel(0x1, EXYNOS5_CLKDIV2_RATIO0);

		/* 1. MCUISP */
		mout_bpll_user = clk_get(&pdev->dev, "mout_bpll_user");

		aclk_400_isp_pre = clk_get(&pdev->dev, "aclk_400_isp_pre");
		if (IS_ERR(aclk_400_isp_pre)) {
			pr_err("%s : clk_get(aclk_400_isp_pre) failed\n", __func__);
			return PTR_ERR(aclk_400_isp_pre);
		}

		clk_set_parent(aclk_400_isp_pre, mout_bpll_user);
		clk_set_rate(aclk_400_isp_pre, 400 * 1000000);

		aclk_mcuisp = clk_get(&pdev->dev, "aclk_400_isp");
		if (IS_ERR(aclk_mcuisp)) {
			pr_err("%s : clk_get(aclk_400_isp) failed\n", __func__);
			return PTR_ERR(aclk_mcuisp);
		}

		clk_set_parent(aclk_mcuisp, aclk_400_isp_pre);

		aclk_mcuisp_div0 = clk_get(&pdev->dev, "mcuisp_div0");
		if (IS_ERR(aclk_mcuisp_div0)) {
			pr_err("%s : clk_get(mcuisp_div0) failed\n", __func__);
			return PTR_ERR(aclk_mcuisp_div0);
		}

		aclk_mcuisp_div1 = clk_get(&pdev->dev, "mcuisp_div1");
		if (IS_ERR(aclk_mcuisp_div1)) {
			pr_err("%s : clk_get(mcuisp_div1) failed\n", __func__);
			return PTR_ERR(aclk_mcuisp_div1);
		}

		clk_set_rate(aclk_mcuisp_div0, 200 * 1000000);
		clk_set_rate(aclk_mcuisp_div1, 100 * 1000000);

		mcu_isp_400 = clk_get_rate(aclk_mcuisp);
		pr_debug("mcu_isp_400 : %ld\n", mcu_isp_400);

		mcu_isp_400 = clk_get_rate(aclk_mcuisp_div0);
		pr_debug("mcu_isp_400_div0 : %ld\n", mcu_isp_400);

		mcu_isp_400 = clk_get_rate(aclk_mcuisp_div1);
		pr_debug("aclk_mcuisp_div1 : %ld\n", mcu_isp_400);

		clk_put(aclk_400_isp_pre);
		clk_put(aclk_mcuisp);
		clk_put(aclk_mcuisp_div0);
		clk_put(aclk_mcuisp_div1);

		/* 2. ACLK_266_ISP */
		aclk_266_pre = clk_get(&pdev->dev, "aclk_266");
		if (IS_ERR(aclk_266_pre)) {
			pr_err("%s : clk_get(aclk_266) failed\n", __func__);
			return PTR_ERR(aclk_266_pre);
		}

		aclk_266 = clk_get(&pdev->dev, "aclk_266_isp");
		if (IS_ERR(aclk_266)) {
			pr_err("%s : clk_get(aclk_266_isp) failed\n", __func__);
			return PTR_ERR(aclk_266);
		}

		clk_set_parent(aclk_266, aclk_266_pre);

		isp_266 = clk_get_rate(aclk_266);
		pr_debug("isp_266 : %ld\n", isp_266);

		clk_put(aclk_266_pre);
		clk_put(aclk_266);

		/* 3. ACLK_333_432_ISP */
		dout_aclk_333_432 = clk_get(&pdev->dev, "dout_aclk_333_432_isp");
		if (IS_ERR(dout_aclk_333_432)) {
			pr_err("%s : clk_get(dout_aclk_333_432_isp) failed\n", __func__);
			return PTR_ERR(dout_aclk_333_432);
		}

		clk_set_rate(dout_aclk_333_432, 432 * 1000000);

		aclk_333_432 = clk_get(&pdev->dev, "aclk_333_432_isp");
		if (IS_ERR(aclk_333_432)) {
			pr_err("%s : clk_get(aclk_333_432_isp) failed\n", __func__);
			return PTR_ERR(aclk_333_432);
		}

		clk_set_parent(aclk_333_432, dout_aclk_333_432);

		/* ISP_DIV0 */
		isp_div0 = clk_get(&pdev->dev, "isp_div0");
		if (IS_ERR(isp_div0)) {
			pr_err("%s : clk_get(isp_div0) failed\n", __func__);
			return PTR_ERR(isp_div0);
		}

		clk_set_rate(isp_div0, 216 * 1000000);

		/* ISP_DIV1 */
		isp_div1 = clk_get(&pdev->dev, "isp_div1");
		if (IS_ERR(isp_div1)) {
			pr_err("%s : clk_get(isp_div1) failed\n", __func__);
			return PTR_ERR(isp_div1);
		}

		clk_set_rate(isp_div1, 108 * 1000000);

		/* MPWM_DIV */
		mpwm_div = clk_get(&pdev->dev, "mpwm_div");
		if (IS_ERR(mpwm_div)) {
			pr_err("%s : clk_get(mpwm_div) failed\n", __func__);
			return PTR_ERR(mpwm_div);
		}

		clk_set_rate(mpwm_div, 54 * 1000000);

		clk_put(dout_aclk_333_432);
		clk_put(aclk_333_432);
		clk_put(isp_div0);
		clk_put(isp_div1);
		clk_put(mpwm_div);

		/* 4. ACLK_333_432_GSL */
		dout_aclk_333_432_gscl = clk_get(&pdev->dev, "dout_aclk_333_432_gscl");
		if (IS_ERR(dout_aclk_333_432_gscl)) {
			pr_err("%s : clk_get(dout_aclk_333_432_gscl) failed\n", __func__);
			return PTR_ERR(dout_aclk_333_432_gscl);
		}

		clk_set_rate(dout_aclk_333_432_gscl, 432 * 1000000);

		aclk_333_432_gscl = clk_get(&pdev->dev, "aclk_333_432_gscl");
		if (IS_ERR(aclk_333_432_gscl)) {
			pr_err("%s : clk_get(aclk_333_432_gscl) failed\n", __func__);
			return PTR_ERR(aclk_333_432_gscl);
		}

		clk_set_parent(aclk_333_432_gscl, dout_aclk_333_432_gscl);

		/* PCLK_166_GSCL */
		pclk_166_gscl = clk_get(&pdev->dev, "pclk_166_gscl");
		if (IS_ERR(pclk_166_gscl)) {
			pr_err("%s : clk_get(pclk_166_gscl) failed\n", __func__);
			return PTR_ERR(pclk_166_gscl);
		}

		clk_set_rate(pclk_166_gscl, 216 * 1000000);

		clk_put(dout_aclk_333_432_gscl);
		clk_put(aclk_333_432_gscl);
		clk_put(pclk_166_gscl);

		/* 4. ACLK_333_432_GSL */
		dout_aclk_333_432_gscl = clk_get(&pdev->dev, "dout_aclk_333_432_gscl");
		if (IS_ERR(dout_aclk_333_432_gscl)) {
			pr_err("%s : clk_get(dout_aclk_333_432_gscl) failed\n", __func__);
			return PTR_ERR(dout_aclk_333_432_gscl);
		}

		clk_set_rate(dout_aclk_333_432_gscl, 432 * 1000000);

		aclk_333_432_gscl = clk_get(&pdev->dev, "aclk_333_432_gscl");
		if (IS_ERR(aclk_333_432_gscl)) {
			pr_err("%s : clk_get(aclk_333_432_gscl) failed\n", __func__);
			return PTR_ERR(aclk_333_432_gscl);
		}

		clk_set_parent(aclk_333_432_gscl, dout_aclk_333_432_gscl);

		/* PCLK_166_GSCL */
		pclk_166_gscl = clk_get(&pdev->dev, "pclk_166_gscl");
		if (IS_ERR(pclk_166_gscl)) {
			pr_err("%s : clk_get(pclk_166_gscl) failed\n", __func__);
			return PTR_ERR(pclk_166_gscl);
		}

		clk_set_rate(pclk_166_gscl, 216 * 1000000);

		clk_put(dout_aclk_333_432_gscl);
		clk_put(aclk_333_432_gscl);
		clk_put(pclk_166_gscl);

		/* 5. SCLK_ISP_BLK */
		/* PWM-ISP */
		sclk_pwm_isp = clk_get(&pdev->dev, "sclk_pwm_isp");
		if (IS_ERR(sclk_pwm_isp)) {
			pr_err("%s : clk_get(sclk_pwm_isp) failed\n", __func__);
			return PTR_ERR(sclk_pwm_isp);
		}

		clk_set_parent(sclk_pwm_isp, clk_get(&pdev->dev, "ext_xtal"));
		clk_set_rate(sclk_pwm_isp, 2 * 1000000);

		isp_pwm = clk_get_rate(sclk_pwm_isp);
		pr_debug("isp_pwm : %ld\n", isp_pwm);

		clk_put(sclk_pwm_isp);

		/* UART-ISP */
		sclk_uart_isp = clk_get(&pdev->dev, "sclk_uart_isp");
		if (IS_ERR(sclk_uart_isp)) {
			pr_err("%s : clk_get(sclk_uart_isp) failed\n", __func__);
			return PTR_ERR(sclk_uart_isp);
		}

		clk_set_parent(sclk_uart_isp, clk_get(&pdev->dev, "mout_cpll"));
		clk_set_rate(sclk_uart_isp, 67 * 1000000);

		isp_uart = clk_get_rate(sclk_uart_isp);
		pr_debug("isp_uart : %ld\n", isp_uart);

		clk_put(sclk_uart_isp);

		/* SPI1-ISP */
		sclk_spi1_isp = clk_get(&pdev->dev, "sclk_spi1_isp");
		if (IS_ERR(sclk_spi1_isp)) {
			pr_err("%s : clk_get(sclk_spi1_isp) failed\n", __func__);
			return PTR_ERR(sclk_spi1_isp);
		}

		sclk_spi1_isp_pre = clk_get(&pdev->dev, "sclk_spi1_isp_pre");
		if (IS_ERR(sclk_spi1_isp_pre)) {
			pr_err("%s : clk_get(sclk_spi1_isp_pre) failed\n", __func__);
			return PTR_ERR(sclk_spi1_isp_pre);
		}

		clk_set_parent(sclk_spi1_isp, clk_get(&pdev->dev, "mout_epll"));
		clk_set_rate(sclk_spi1_isp, 100 * 1000000);
		clk_set_rate(sclk_spi1_isp_pre, 100 * 1000000);

		isp_spi1 = clk_get_rate(sclk_spi1_isp);
		pr_debug("isp_spi1 : %ld\n", isp_spi1);
		isp_spi1_pre = clk_get_rate(sclk_spi1_isp_pre);
		pr_debug("isp_spi1_pre : %ld\n", isp_spi1_pre);

		clk_put(sclk_spi1_isp);
		clk_put(sclk_spi1_isp_pre);

		/* SPI0-ISP */
		sclk_spi0_isp = clk_get(&pdev->dev, "sclk_spi0_isp");
		if (IS_ERR(sclk_spi0_isp)) {
			pr_err("%s : clk_get(sclk_spi0_isp) failed\n", __func__);
			return PTR_ERR(sclk_spi0_isp);
		}

		sclk_spi0_isp_pre = clk_get(&pdev->dev, "sclk_spi0_isp_pre");
		if (IS_ERR(sclk_spi0_isp_pre)) {
			pr_err("%s : clk_get(sclk_spi0_isp_pre) failed\n", __func__);
			return PTR_ERR(sclk_spi0_isp_pre);
		}

		clk_set_parent(sclk_spi0_isp, clk_get(&pdev->dev, "mout_epll"));
		clk_set_rate(sclk_spi0_isp, 100 * 1000000);
		clk_set_rate(sclk_spi0_isp_pre, 100 * 1000000);

		isp_spi0 = clk_get_rate(sclk_spi0_isp);
		pr_debug("isp_spi0 : %ld\n", isp_spi0);
		isp_spi0_pre = clk_get_rate(sclk_spi0_isp_pre);
		pr_debug("isp_spi0_pre : %ld\n", isp_spi0_pre);

		clk_put(sclk_spi0_isp);
		clk_put(sclk_spi0_isp_pre);

		/* SENSOR0~2 */
		sclk_mout_isp_sensor = clk_get(&pdev->dev, "sclk_mout_isp_sensor");
		if (IS_ERR(sclk_mout_isp_sensor)) {
			pr_err("%s : clk_get(sclk_mout_isp_sensor) failed\n", __func__);
			return PTR_ERR(sclk_mout_isp_sensor);
		}

		sclk_isp_sensor0 = clk_get(&pdev->dev, "sclk_isp_sensor0");
		if (IS_ERR(sclk_isp_sensor0)) {
			pr_err("%s : clk_get(sclk_isp_sensor0) failed\n", __func__);
			return PTR_ERR(sclk_isp_sensor0);
		}

		sclk_isp_sensor1 = clk_get(&pdev->dev, "sclk_isp_sensor1");
		if (IS_ERR(sclk_isp_sensor1)) {
			pr_err("%s : clk_get(sclk_isp_sensor1) failed\n", __func__);
			return PTR_ERR(sclk_isp_sensor1);
		}

		sclk_isp_sensor2 = clk_get(&pdev->dev, "sclk_isp_sensor2");
		if (IS_ERR(sclk_isp_sensor2)) {
			pr_err("%s : clk_get(sclk_isp_sensor2) failed\n", __func__);
			return PTR_ERR(sclk_isp_sensor2);
		}

		clk_set_parent(sclk_mout_isp_sensor, clk_get(&pdev->dev, "mout_ipll"));
		clk_set_rate(sclk_isp_sensor0, 24 * 1000000);
		clk_set_rate(sclk_isp_sensor1, 24 * 1000000);
		clk_set_rate(sclk_isp_sensor2, 24 * 1000000);

		isp_sensor0 = clk_get_rate(sclk_isp_sensor0);
		pr_debug("isp_sensor0 : %ld\n", isp_sensor0);
		isp_sensor1 = clk_get_rate(sclk_isp_sensor1);
		pr_debug("isp_sensor1 : %ld\n", isp_sensor1);
		isp_sensor2 = clk_get_rate(sclk_isp_sensor2);
		pr_debug("isp_sensor2 : %ld\n", isp_sensor2);

		clk_put(sclk_mout_isp_sensor);
		clk_put(sclk_isp_sensor0);
		clk_put(sclk_isp_sensor1);
		clk_put(sclk_isp_sensor2);

		/* Enable from booting time */
		/* UART-ISP */
		sclk_uart_isp = clk_get(&pdev->dev, "sclk_uart_isp");
		if (IS_ERR(sclk_uart_isp)) {
			pr_err("%s : clk_get(sclk_uart_isp) failed\n", __func__);
			return PTR_ERR(sclk_uart_isp);
		}

		clk_enable(sclk_uart_isp);
		clk_put(sclk_uart_isp);
	}

	return 0;
}

int exynos5_fimc_is_clk_on(struct platform_device *pdev)
{
	pr_debug("exynos5_fimc_is_clk_on\n");

	if (soc_is_exynos5250()) {
		struct clk *gsc_ctrl = NULL;
		struct clk *isp_ctrl = NULL;
		struct clk *cam_if_top = NULL;
		struct clk *isp_400_src = NULL;
		struct clk *isp_266_src = NULL;
		struct clk *isp_400_clk = NULL;
		struct clk *isp_266_clk = NULL;

		gsc_ctrl = clk_get(&pdev->dev, "gscl");
		if (IS_ERR(gsc_ctrl))
			return PTR_ERR(gsc_ctrl);

		clk_enable(gsc_ctrl);
		clk_put(gsc_ctrl);

		isp_ctrl = clk_get(&pdev->dev, "isp0");
		if (IS_ERR(isp_ctrl))
			return PTR_ERR(isp_ctrl);

		clk_enable(isp_ctrl);
		clk_put(isp_ctrl);

		isp_ctrl = clk_get(&pdev->dev, "isp1");
		if (IS_ERR(isp_ctrl))
			return PTR_ERR(isp_ctrl);

		clk_enable(isp_ctrl);
		clk_put(isp_ctrl);

		cam_if_top = clk_get(&pdev->dev, "camif_top");
		if (IS_ERR(cam_if_top))
			return PTR_ERR(cam_if_top);

		clk_enable(cam_if_top);
		clk_put(cam_if_top);

		/*isp sub src selection*/
		isp_400_src = clk_get(&pdev->dev, "aclk_400_isp");
		if (IS_ERR(isp_400_src))
			return PTR_ERR(isp_400_src);

		isp_266_src = clk_get(&pdev->dev, "aclk_266_isp");
		if (IS_ERR(isp_266_src)) {
			clk_put(isp_400_src);
			return PTR_ERR(isp_266_src);
		}

		isp_400_clk = clk_get(&pdev->dev, "dout_aclk_400_isp");
		if (IS_ERR(isp_400_clk)) {
			clk_put(isp_400_src);
			clk_put(isp_266_src);
			return PTR_ERR(isp_400_clk);
		}

		isp_266_clk = clk_get(&pdev->dev, "aclk_266");
		if (IS_ERR(isp_266_clk)) {
			clk_put(isp_400_src);
			clk_put(isp_266_src);
			clk_put(isp_400_clk);
			return PTR_ERR(isp_266_clk);
		}

		clk_set_parent(isp_400_src, isp_400_clk);
		clk_set_parent(isp_266_src, isp_266_clk);

		clk_put(isp_400_src);
		clk_put(isp_266_src);
		clk_put(isp_400_clk);
		clk_put(isp_266_clk);
	} else if (soc_is_exynos5410()) {
		int channel;
		char mipi[20];
		char flite[20];
		struct clk *isp_ctrl = NULL;
		struct clk *fimc_3aa_ctrl = NULL;
		struct clk *mipi_ctrl = NULL;
		struct clk *flite_ctrl = NULL;

		isp_ctrl = clk_get(&pdev->dev, "isp0_333_432");
		if (IS_ERR(isp_ctrl)) {
			pr_err("%s : clk_get(isp0_333_432) failed\n", __func__);
			return PTR_ERR(isp_ctrl);
		}

		clk_enable(isp_ctrl);
		clk_put(isp_ctrl);

		isp_ctrl = clk_get(&pdev->dev, "isp0_400");
		if (IS_ERR(isp_ctrl)) {
			pr_err("%s : clk_get(isp0_400) failed\n", __func__);
			return PTR_ERR(isp_ctrl);
		}

		clk_enable(isp_ctrl);
		clk_put(isp_ctrl);

		isp_ctrl = clk_get(&pdev->dev, "isp0_266");
		if (IS_ERR(isp_ctrl)) {
			pr_err("%s : clk_get(isp0_266) failed\n", __func__);
			return PTR_ERR(isp_ctrl);
		}

		clk_enable(isp_ctrl);
		clk_put(isp_ctrl);

		isp_ctrl = clk_get(&pdev->dev, "isp1_333_432");
		if (IS_ERR(isp_ctrl)) {
			pr_err("%s : clk_get(isp1_333_432) failed\n", __func__);
			return PTR_ERR(isp_ctrl);
		}

		clk_enable(isp_ctrl);
		clk_put(isp_ctrl);

		isp_ctrl = clk_get(&pdev->dev, "isp1_266");
		if (IS_ERR(isp_ctrl)) {
			pr_err("%s : clk_get(isp1_266) failed\n", __func__);
			return PTR_ERR(isp_ctrl);
		}

		clk_enable(isp_ctrl);
		clk_put(isp_ctrl);

		fimc_3aa_ctrl = clk_get(&pdev->dev, "3aa");
		if (IS_ERR(fimc_3aa_ctrl)) {
			pr_err("%s : clk_get(fimc_3aa) failed\n", __func__);
			return PTR_ERR(fimc_3aa_ctrl);
		}

		clk_enable(fimc_3aa_ctrl);
		clk_put(fimc_3aa_ctrl);

		for (channel = FLITE_ID_A; channel < FLITE_ID_END; channel++) {
			snprintf(mipi, sizeof(mipi), "gscl_wrap%d", channel);
			snprintf(flite, sizeof(flite), "gscl_flite%d", channel);

			mipi_ctrl = clk_get(&pdev->dev, mipi);
			if (IS_ERR(mipi_ctrl)) {
				pr_err("%s : clk_get(%s) failed\n",
					__func__, mipi);
				return  PTR_ERR(mipi_ctrl);
			}

			clk_enable(mipi_ctrl);
			clk_put(mipi_ctrl);

			flite_ctrl = clk_get(&pdev->dev, flite);
			if (IS_ERR(flite_ctrl)) {
				pr_err("%s : clk_get(%s) failed\n",
					__func__, flite);
				return PTR_ERR(flite_ctrl);
			}

			clk_enable(flite_ctrl);
			clk_put(flite_ctrl);
		}
	}

	return 0;
}

int exynos5_fimc_is_clk_off(struct platform_device *pdev)
{
	pr_debug("exynos5_fimc_is_clk_off\n");

	if (soc_is_exynos5250()) {
		struct clk *gsc_ctrl = NULL;
		struct clk *isp_ctrl = NULL;
		struct clk *cam_if_top = NULL;
		struct clk *isp_400_src = NULL;
		struct clk *isp_266_src = NULL;
		struct clk *xtal_clk = NULL;

		cam_if_top = clk_get(&pdev->dev, "camif_top");
		if (IS_ERR(cam_if_top))
			return PTR_ERR(cam_if_top);

		clk_disable(cam_if_top);
		clk_put(cam_if_top);

		isp_ctrl = clk_get(&pdev->dev, "isp0");
		if (IS_ERR(isp_ctrl))
			return PTR_ERR(isp_ctrl);

		clk_disable(isp_ctrl);
		clk_put(isp_ctrl);

		isp_ctrl = clk_get(&pdev->dev, "isp1");
		if (IS_ERR(isp_ctrl))
			return PTR_ERR(isp_ctrl);

		clk_disable(isp_ctrl);
		clk_put(isp_ctrl);

		gsc_ctrl = clk_get(&pdev->dev, "gscl");
		if (IS_ERR(gsc_ctrl))
			return PTR_ERR(gsc_ctrl);

		clk_disable(gsc_ctrl);
		clk_put(gsc_ctrl);

		/*isp sub src selection*/
		isp_400_src = clk_get(&pdev->dev, "aclk_400_isp");
		if (IS_ERR(isp_400_src))
			return PTR_ERR(isp_400_src);

		isp_266_src = clk_get(&pdev->dev, "aclk_266_isp");
		if (IS_ERR(isp_266_src)) {
			clk_put(isp_400_src);
			return PTR_ERR(isp_266_src);
		}

		xtal_clk = clk_get(&pdev->dev, "ext_xtal");
		if (IS_ERR(xtal_clk)) {
			clk_put(isp_400_src);
			clk_put(isp_266_src);
			return PTR_ERR(xtal_clk);
		}

		clk_set_parent(isp_400_src, xtal_clk);
		clk_set_parent(isp_266_src, xtal_clk);

		clk_put(isp_400_src);
		clk_put(isp_266_src);
		clk_put(xtal_clk);
	} else if (soc_is_exynos5410()) {
		int channel;
		char mipi[20];
		char flite[20];
		struct clk *isp_ctrl = NULL;
		struct clk *fimc_3aa_ctrl = NULL;
		struct clk *mipi_ctrl = NULL;
		struct clk *flite_ctrl = NULL;

		isp_ctrl = clk_get(&pdev->dev, "isp0_333_432");
		if (IS_ERR(isp_ctrl)) {
			pr_err("%s : clk_get(isp0_333_432) failed\n", __func__);
			return PTR_ERR(isp_ctrl);
		}

		clk_disable(isp_ctrl);
		clk_put(isp_ctrl);

		isp_ctrl = clk_get(&pdev->dev, "isp0_400");
		if (IS_ERR(isp_ctrl)) {
			pr_err("%s : clk_get(isp0_400) failed\n", __func__);
			return PTR_ERR(isp_ctrl);
		}

		clk_disable(isp_ctrl);
		clk_put(isp_ctrl);

		isp_ctrl = clk_get(&pdev->dev, "isp0_266");
		if (IS_ERR(isp_ctrl)) {
			pr_err("%s : clk_get(isp0_266) failed\n", __func__);
			return PTR_ERR(isp_ctrl);
		}

		clk_disable(isp_ctrl);
		clk_put(isp_ctrl);

		isp_ctrl = clk_get(&pdev->dev, "isp1_333_432");
		if (IS_ERR(isp_ctrl)) {
			pr_err("%s : clk_get(isp1_333_432) failed\n", __func__);
			return PTR_ERR(isp_ctrl);
		}

		clk_disable(isp_ctrl);
		clk_put(isp_ctrl);

		isp_ctrl = clk_get(&pdev->dev, "isp1_266");
		if (IS_ERR(isp_ctrl)) {
			pr_err("%s : clk_get(isp1_266) failed\n", __func__);
			return PTR_ERR(isp_ctrl);
		}

		clk_disable(isp_ctrl);
		clk_put(isp_ctrl);

		fimc_3aa_ctrl = clk_get(&pdev->dev, "3aa");
		if (IS_ERR(fimc_3aa_ctrl)) {
			pr_err("%s : clk_get(fimc_3aa) failed\n", __func__);
			return PTR_ERR(fimc_3aa_ctrl);
		}

		clk_disable(fimc_3aa_ctrl);
		clk_put(fimc_3aa_ctrl);

		for (channel = FLITE_ID_A; channel < FLITE_ID_END; channel++) {
			snprintf(mipi, sizeof(mipi), "gscl_wrap%d", channel);
			snprintf(flite, sizeof(flite), "gscl_flite%d", channel);

			flite_ctrl = clk_get(&pdev->dev, flite);
			if (IS_ERR(flite_ctrl)) {
				pr_err("%s : clk_get(%s) failed\n",
					__func__, flite);
				return PTR_ERR(flite_ctrl);
			}

			clk_disable(flite_ctrl);
			clk_put(flite_ctrl);

			mipi_ctrl = clk_get(&pdev->dev, mipi);
			if (IS_ERR(mipi_ctrl)) {
				pr_err("%s : clk_get(%s) failed\n",
					__func__, mipi);
				return PTR_ERR(mipi_ctrl);
			}

			clk_disable(mipi_ctrl);
			clk_put(mipi_ctrl);
		}
	}

	return 0;
}

/* sequence is important, don't change order */
int exynos5_fimc_is_sensor_power_on(struct platform_device *pdev,
						int sensor_id)
{

	struct exynos5_platform_fimc_is *dev = pdev->dev.platform_data;
	struct exynos5_fimc_is_sensor_info *sensor =
						dev->sensor_info[sensor_id];
	int i;

	pr_debug("exynos5_fimc_is_sensor_power_on(%d)\n",
					sensor_id);
	switch (sensor->sensor_id) {
	case SENSOR_NAME_S5K4E5:
		if (sensor->sensor_gpio.reset_peer.pin)
			if (IS_ERR_VALUE(cfg_gpio(
					&sensor->sensor_gpio.reset_peer, 0)))
				goto error_sensor_power_on;

		if (sensor->sensor_gpio.reset_myself.pin)
			if (IS_ERR_VALUE(cfg_gpio(
					&sensor->sensor_gpio.reset_myself, 0)))
				goto error_sensor_power_on;

		if (sensor->sensor_power.cam_core)
			if (IS_ERR_VALUE(power_control_sensor(
					sensor->sensor_power.cam_core, 1)))
				goto error_sensor_power_on;
		usleep_range(500, 1000);

		if (sensor->sensor_gpio.power.pin)
			if (IS_ERR_VALUE(cfg_gpio(
					&sensor->sensor_gpio.power, 1)))
				goto error_sensor_power_on;
		usleep_range(500, 1000);

		if (sensor->sensor_power.cam_io_myself)
			if (IS_ERR_VALUE(power_control_sensor(
					sensor->sensor_power.cam_io_myself, 1)))
				goto error_sensor_power_on;
		usleep_range(500, 1000);

		if (sensor->sensor_power.cam_io_peer)
			if (IS_ERR_VALUE(power_control_sensor(
					sensor->sensor_power.cam_io_peer, 1)))
				goto error_sensor_power_on;
		usleep_range(500, 1000);

		for (i = 0; i < FIMC_IS_MAX_GPIO_NUM; i++) {
			if (!sensor->sensor_gpio.cfg[i].pin)
				continue;
			if (IS_ERR_VALUE(cfg_gpio(&sensor->sensor_gpio.cfg[i],
					sensor->sensor_gpio.cfg[i].value)))
				goto error_sensor_power_on;
		}

		if (sensor->sensor_power.cam_af)
			if (IS_ERR_VALUE(power_control_sensor(
					sensor->sensor_power.cam_af, 1)))
				goto error_sensor_power_on;
		usleep_range(500, 1000);

		if (sensor->sensor_gpio.reset_myself.pin)
			if (IS_ERR_VALUE(cfg_gpio(
					&sensor->sensor_gpio.reset_myself, 1)))
				goto error_sensor_power_on;
		usleep_range(10, 100);

		break;

	case SENSOR_NAME_S5K6A3:
		if (sensor->sensor_gpio.reset_peer.pin)
			if (IS_ERR_VALUE(cfg_gpio(
					&sensor->sensor_gpio.reset_peer, 0)))
				goto error_sensor_power_on;

		if (sensor->sensor_gpio.reset_myself.pin)
			if (IS_ERR_VALUE(cfg_gpio(
					&sensor->sensor_gpio.reset_myself, 0)))
				goto error_sensor_power_on;

		if (sensor->sensor_power.cam_core)
			if (IS_ERR_VALUE(power_control_sensor(
					sensor->sensor_power.cam_core, 1)))
				goto error_sensor_power_on;
		usleep_range(500, 1000);

		for (i = 0; i < FIMC_IS_MAX_GPIO_NUM; i++) {
			if (!sensor->sensor_gpio.cfg[i].pin)
				continue;
			if (IS_ERR_VALUE(cfg_gpio(&sensor->sensor_gpio.cfg[i],
					sensor->sensor_gpio.cfg[i].value)))
				goto error_sensor_power_on;
		}

		if (sensor->sensor_gpio.power.pin)
			if (IS_ERR_VALUE(cfg_gpio(
					&sensor->sensor_gpio.power, 1)))
				goto error_sensor_power_on;
		usleep_range(500, 1000);

		if (sensor->sensor_power.cam_io_myself)
			if (IS_ERR_VALUE(power_control_sensor(
					sensor->sensor_power.cam_io_myself, 1)))
				goto error_sensor_power_on;
		usleep_range(500, 1000);

		if (sensor->sensor_power.cam_io_peer)
			if (IS_ERR_VALUE(power_control_sensor(
					sensor->sensor_power.cam_io_peer, 1)))
				goto error_sensor_power_on;
		usleep_range(500, 1000);

		if (sensor->sensor_gpio.reset_peer.pin)
			if (IS_ERR_VALUE(cfg_gpio(
					&sensor->sensor_gpio.reset_peer, 1)))
				goto error_sensor_power_on;
		usleep_range(1200, 2000); /* must stay here more than 1msec */

		if (sensor->sensor_gpio.reset_peer.pin)
			if (IS_ERR_VALUE(cfg_gpio(
					&sensor->sensor_gpio.reset_peer, 0)))
				goto error_sensor_power_on;
		usleep_range(1000, 1500);

		if (sensor->sensor_gpio.reset_myself.pin)
			if (IS_ERR_VALUE(cfg_gpio(
					&sensor->sensor_gpio.reset_myself, 1)))
				goto error_sensor_power_on;
		usleep_range(500, 1000);

		if (sensor->sensor_gpio.reset_myself.pin)
			if (IS_ERR_VALUE(cfg_gpio(
					&sensor->sensor_gpio.reset_myself, 0)))
				goto error_sensor_power_on;
		usleep_range(500, 1000);

		if (sensor->sensor_gpio.reset_myself.pin)
			if (IS_ERR_VALUE(cfg_gpio(
					&sensor->sensor_gpio.reset_myself, 1)))
				goto error_sensor_power_on;
		usleep_range(10, 100);

		break;
	default:
		pr_err("Bad camera senosr ID(%d)",
				sensor->sensor_id);
		goto error_sensor_power_on;
	}
	return 0;

error_sensor_power_on:
	return -1;

}

/* sequence is important, don't change order */
int exynos5_fimc_is_sensor_power_off(struct platform_device *pdev,
						int sensor_id)
{
	struct exynos5_platform_fimc_is *dev = pdev->dev.platform_data;
	struct exynos5_fimc_is_sensor_info *sensor
					= dev->sensor_info[sensor_id];

	pr_debug("exynos5_fimc_is_sensor_power_off(%d)\n", sensor_id);

	switch (sensor->sensor_id) {
	case SENSOR_NAME_S5K4E5:
		if (sensor->sensor_gpio.reset_peer.pin)
			if (IS_ERR_VALUE(cfg_gpio(
					&sensor->sensor_gpio.reset_peer, 0)))
				goto error_sensor_power_off;

		if (sensor->sensor_gpio.reset_myself.pin)
			if (IS_ERR_VALUE(cfg_gpio(
					&sensor->sensor_gpio.reset_myself, 0)))
				goto error_sensor_power_off;

		if (sensor->sensor_gpio.power.pin)
			if (IS_ERR_VALUE(cfg_gpio(
					&sensor->sensor_gpio.power, 0)))
				goto error_sensor_power_off;
		usleep_range(500, 1000);

		if (sensor->sensor_power.cam_core)
			if (IS_ERR_VALUE(power_control_sensor(
					sensor->sensor_power.cam_core, 0)))
				goto error_sensor_power_off;

		usleep_range(500, 1000);
		if (sensor->sensor_power.cam_io_myself)
			if (IS_ERR_VALUE(power_control_sensor(
					sensor->sensor_power.cam_io_myself, 0)))
				goto error_sensor_power_off;

		usleep_range(500, 1000);
			if (sensor->sensor_power.cam_io_peer)
				if (IS_ERR_VALUE(power_control_sensor(
					sensor->sensor_power.cam_io_peer, 0)))
					goto error_sensor_power_off;

		usleep_range(500, 1000);
		if (sensor->sensor_power.cam_af)
			if (IS_ERR_VALUE(power_control_sensor(
					sensor->sensor_power.cam_af, 0)))
				goto error_sensor_power_off;

		usleep_range(500, 1000);
		break;

	case SENSOR_NAME_S5K6A3:
		if (sensor->sensor_gpio.reset_peer.pin)
			if (IS_ERR_VALUE(cfg_gpio(
					&sensor->sensor_gpio.reset_peer, 0)))
				goto error_sensor_power_off;

		if (sensor->sensor_gpio.reset_myself.pin)
			if (IS_ERR_VALUE(cfg_gpio(
					&sensor->sensor_gpio.reset_myself, 0)))
				goto error_sensor_power_off;

		if (sensor->sensor_gpio.power.pin)
			if (IS_ERR_VALUE(cfg_gpio(
					&sensor->sensor_gpio.power, 0)))
				goto error_sensor_power_off;
		usleep_range(500, 1000);

		if (sensor->sensor_power.cam_core)
			if (IS_ERR_VALUE(power_control_sensor(
					sensor->sensor_power.cam_core, 0)))
				goto error_sensor_power_off;
		usleep_range(500, 1000);

		if (sensor->sensor_power.cam_io_myself)
			if (IS_ERR_VALUE(power_control_sensor(
					sensor->sensor_power.cam_io_myself, 0)))
				goto error_sensor_power_off;
		usleep_range(500, 1000);

		if (sensor->sensor_power.cam_io_peer)
			if (IS_ERR_VALUE(power_control_sensor(
					sensor->sensor_power.cam_io_peer, 0)))
				goto error_sensor_power_off;
		usleep_range(500, 1000);
		break;
	default:
		pr_err("Bad camera senosr ID(%d)",
				sensor->sensor_id);
		goto error_sensor_power_off;
	}
	return 0;

error_sensor_power_off:
	return -1;

}
#endif
