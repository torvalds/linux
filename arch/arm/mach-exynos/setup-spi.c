/* linux/arch/arm/mach-exynos4/setup-spi.c
 *
 * Copyright (C) 2011 Samsung Electronics Ltd.
 *             http://www.samsung.com/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/clk.h>
#include <linux/err.h>
#include <linux/gpio.h>
#include <linux/platform_device.h>

#include <plat/clock.h>
#include <plat/cpu.h>
#include <plat/gpio-cfg.h>
#include <plat/s3c64xx-spi.h>

#define	EXYNOS_SPI_NAME_SIZE	16

int exynos_spi_cfg_cs(int gpio, int ch_num)
{
	char cs_name[EXYNOS_SPI_NAME_SIZE];

	snprintf(cs_name, EXYNOS_SPI_NAME_SIZE, "SPI_CS%d", ch_num);

	if (gpio_request(gpio, cs_name))
		return -EIO;

	gpio_direction_output(gpio, 1);
	s3c_gpio_cfgpin(gpio, S3C_GPIO_SFN(1));
	s3c_gpio_setpull(gpio, S3C_GPIO_PULL_UP);
	gpio_free(gpio);

	return 0;

}

void exynos_spi_clock_setup(struct device *spi_dev, int ch_num)
{
	struct clk *child_clk = NULL;
	struct clk *parent_clk = NULL;
	char clk_name[EXYNOS_SPI_NAME_SIZE];

	snprintf(clk_name, EXYNOS_SPI_NAME_SIZE, "dout_spi%d", ch_num);
	child_clk = clk_get(spi_dev, clk_name);

	if (IS_ERR(child_clk)) {
		pr_err("%s: Failed to get %s clk\n", __func__, clk_name);
		return;
	}

	if (soc_is_exynos5410())
		parent_clk = clk_get(spi_dev, "mout_mpll_bpll");
	else
		parent_clk = clk_get(spi_dev, "mout_mpll_user");

	if (IS_ERR(parent_clk)) {
		pr_err("%s: Failed to get mout_mpll_user clk\n", __func__);
		goto err1;
	}

	if (clk_set_parent(child_clk, parent_clk)) {
		pr_err("%s: Unable to set parent %s of clock %s\n",
				__func__, parent_clk->name, child_clk->name);
		goto err2;
	}

	if (clk_set_rate(child_clk, 100 * 1000 * 1000))
		pr_err("%s: Unable to set rate of clock %s\n",
				__func__, child_clk->name);

err2:
	clk_put(parent_clk);
err1:
	clk_put(child_clk);
}

#ifdef CONFIG_S3C64XX_DEV_SPI0
struct s3c64xx_spi_info s3c64xx_spi0_pdata __initdata = {
	.fifo_lvl_mask	= 0x1ff,
	.rx_lvl_offset	= 15,
	.high_speed	= 1,
	.clk_from_cmu	= true,
	.tx_st_done	= 25,
	.dma_mode	= HYBRID_MODE,
};

int s3c64xx_spi0_cfg_gpio(struct platform_device *dev)
{
	int gpio;

	if (soc_is_exynos5410()) {
		s3c_gpio_cfgpin(EXYNOS5410_GPA2(0), S3C_GPIO_SFN(2));
		s3c_gpio_setpull(EXYNOS5410_GPA2(0), S3C_GPIO_PULL_UP);
		s3c_gpio_cfgall_range(EXYNOS5410_GPA2(2), 2,
				      S3C_GPIO_SFN(2), S3C_GPIO_PULL_UP);

		for (gpio = EXYNOS5410_GPA2(0);
				gpio < EXYNOS5410_GPA2(4); gpio++)
			s5p_gpio_set_drvstr(gpio, S5P_GPIO_DRVSTR_LV3);
	} else if (soc_is_exynos5250()) {
		s3c_gpio_cfgpin(EXYNOS5_GPA2(0), S3C_GPIO_SFN(2));
		s3c_gpio_setpull(EXYNOS5_GPA2(0), S3C_GPIO_PULL_UP);
		s3c_gpio_cfgall_range(EXYNOS5_GPA2(2), 2,
				      S3C_GPIO_SFN(2), S3C_GPIO_PULL_UP);

		for (gpio = EXYNOS5_GPA2(0); gpio < EXYNOS5_GPA2(4); gpio++)
			s5p_gpio_set_drvstr(gpio, S5P_GPIO_DRVSTR_LV3);
	} else {
		s3c_gpio_cfgpin(EXYNOS4_GPB(0), S3C_GPIO_SFN(2));
		s3c_gpio_setpull(EXYNOS4_GPB(0), S3C_GPIO_PULL_UP);
		s3c_gpio_cfgall_range(EXYNOS4_GPB(2), 2,
				      S3C_GPIO_SFN(2), S3C_GPIO_PULL_UP);

		for (gpio = EXYNOS4_GPB(0); gpio < EXYNOS4_GPB(4); gpio++)
			s5p_gpio_set_drvstr(gpio, S5P_GPIO_DRVSTR_LV3);
	}

	return 0;
}
#endif

#ifdef CONFIG_S3C64XX_DEV_SPI1
struct s3c64xx_spi_info s3c64xx_spi1_pdata __initdata = {
	.fifo_lvl_mask	= 0x7f,
	.rx_lvl_offset	= 15,
	.high_speed	= 1,
	.clk_from_cmu	= true,
	.tx_st_done	= 25,
	.dma_mode	= HYBRID_MODE,
};

int s3c64xx_spi1_cfg_gpio(struct platform_device *dev)
{
	int gpio;

	if (soc_is_exynos5410()) {
		s3c_gpio_cfgpin(EXYNOS5410_GPA2(4), S3C_GPIO_SFN(2));
		s3c_gpio_setpull(EXYNOS5410_GPA2(4), S3C_GPIO_PULL_UP);
		s3c_gpio_cfgall_range(EXYNOS5410_GPA2(6), 2,
				      S3C_GPIO_SFN(2), S3C_GPIO_PULL_UP);

		for (gpio = EXYNOS5410_GPA2(4);
				gpio < EXYNOS5410_GPA2(8); gpio++)
			s5p_gpio_set_drvstr(gpio, S5P_GPIO_DRVSTR_LV3);
	} else if (soc_is_exynos5250()) {
		s3c_gpio_cfgpin(EXYNOS5_GPA2(4), S3C_GPIO_SFN(2));
		s3c_gpio_setpull(EXYNOS5_GPA2(4), S3C_GPIO_PULL_UP);
		s3c_gpio_cfgall_range(EXYNOS5_GPA2(6), 2,
				      S3C_GPIO_SFN(2), S3C_GPIO_PULL_UP);

		for (gpio = EXYNOS5_GPA2(4); gpio < EXYNOS5_GPA2(8); gpio++)
			s5p_gpio_set_drvstr(gpio, S5P_GPIO_DRVSTR_LV3);
	} else {
		s3c_gpio_cfgpin(EXYNOS4_GPB(4), S3C_GPIO_SFN(2));
		s3c_gpio_setpull(EXYNOS4_GPB(4), S3C_GPIO_PULL_UP);
		s3c_gpio_cfgall_range(EXYNOS4_GPB(6), 2,
				      S3C_GPIO_SFN(2), S3C_GPIO_PULL_UP);

		for (gpio = EXYNOS4_GPB(4); gpio < EXYNOS4_GPB(8); gpio++)
			s5p_gpio_set_drvstr(gpio, S5P_GPIO_DRVSTR_LV3);
	}

	return 0;
}
#endif

#ifdef CONFIG_S3C64XX_DEV_SPI2
struct s3c64xx_spi_info s3c64xx_spi2_pdata __initdata = {
	.fifo_lvl_mask	= 0x7f,
	.rx_lvl_offset	= 15,
	.high_speed	= 1,
	.clk_from_cmu	= true,
	.tx_st_done	= 25,
	.dma_mode	= HYBRID_MODE,
};

int s3c64xx_spi2_cfg_gpio(struct platform_device *dev)
{
	int gpio;

	if (soc_is_exynos5410()) {
		s3c_gpio_cfgpin(EXYNOS5410_GPB1(1), S3C_GPIO_SFN(5));
		s3c_gpio_setpull(EXYNOS5410_GPB1(1), S3C_GPIO_PULL_UP);
		s3c_gpio_cfgall_range(EXYNOS5410_GPB1(3), 2,
				      S3C_GPIO_SFN(5), S3C_GPIO_PULL_UP);

		for (gpio = EXYNOS5410_GPB1(1);
				gpio < EXYNOS5410_GPB1(5); gpio++)
			s5p_gpio_set_drvstr(gpio, S5P_GPIO_DRVSTR_LV3);
	} else if (soc_is_exynos5250()) {
		s3c_gpio_cfgpin(EXYNOS5_GPB1(1), S3C_GPIO_SFN(5));
		s3c_gpio_setpull(EXYNOS5_GPB1(1), S3C_GPIO_PULL_UP);
		s3c_gpio_cfgall_range(EXYNOS5_GPB1(3), 2,
				      S3C_GPIO_SFN(5), S3C_GPIO_PULL_UP);

		for (gpio = EXYNOS5_GPB1(1); gpio < EXYNOS5_GPB1(5); gpio++)
			s5p_gpio_set_drvstr(gpio, S5P_GPIO_DRVSTR_LV3);
	} else {
		s3c_gpio_cfgpin(EXYNOS4_GPC1(1), S3C_GPIO_SFN(5));
		s3c_gpio_setpull(EXYNOS4_GPC1(1), S3C_GPIO_PULL_UP);
		s3c_gpio_cfgall_range(EXYNOS4_GPC1(3), 2,
				      S3C_GPIO_SFN(5), S3C_GPIO_PULL_UP);

		for (gpio = EXYNOS4_GPC1(1); gpio < EXYNOS4_GPC1(5); gpio++)
			s5p_gpio_set_drvstr(gpio, S5P_GPIO_DRVSTR_LV3);
	}

	return 0;
}
#endif

#ifdef CONFIG_S3C64XX_DEV_SPI3
struct s3c64xx_spi_info s3c64xx_spi3_pdata __initdata = {
	.fifo_lvl_mask	= 0x1ff,
	.rx_lvl_offset	= 15,
	.high_speed	= 1,
	.clk_from_cmu	= true,
	.tx_st_done	= 25,
	.dma_mode	= PIO_MODE,
};

int s3c64xx_spi3_cfg_gpio(struct platform_device *dev)
{
	return 0;
}
#endif
