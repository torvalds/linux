/*
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/gpio.h>
#include <linux/smsc911x.h>
#include <linux/mmc/host.h>
#include <linux/delay.h>

#include <plat/gpio-cfg.h>
#include <plat/cpu.h>
#include <plat/clock.h>
#include <plat/devs.h>
#include <plat/sdhci.h>

#include <mach/regs-pmu.h>
#include <mach/dwmci.h>

#include "board-odroidxu.h"

#define GPIO_EMMC_RESET		EXYNOS5410_GPD1(0)

//#define EMMC_DDR_MODE

static struct dw_mci_clk exynos_dwmci_clk_rates[] = {
#if defined(CONFIG_SDMMC_CLOCK_CPLL)
	{20 * 1000 * 1000, 80 * 1000 * 1000},
	{40 * 1000 * 1000, 160 * 1000 * 1000},
	{40 * 1000 * 1000, 160 * 1000 * 1000},
	{80 * 1000 * 1000, 320 * 1000 * 1000},
	{160 * 1000 * 1000, 640 * 1000 * 1000},
	{80 * 1000 * 1000, 320 * 1000 * 1000},
	{160 * 1000 * 1000, 640 * 1000 * 1000},
	{320 * 1000 * 1000, 640 * 1000 * 1000},
#else   // CONFIG_SDMMC_CLOCK_EPLL
	{25 *  1000 * 1000, 100 * 1000 * 1000},
	{50 *  1000 * 1000, 200 * 1000 * 1000},
	{100 * 1000 * 1000, 400 * 1000 * 1000},
	{200 * 1000 * 1000, 800 * 1000 * 1000},
	{25 *  1000 * 1000, 100 * 1000 * 1000},
	{50 *  1000 * 1000, 200 * 1000 * 1000},
	{100 * 1000 * 1000, 400 * 1000 * 1000},
	{200 * 1000 * 1000, 400 * 1000 * 1000},
#endif
};

static void exynos_dwmci_save_drv_st(void *data, u32 slot_id)
{
	struct dw_mci *host = (struct dw_mci *)data;
	struct drv_strength * drv_st = &host->pdata->__drv_st;

	drv_st->val = s5p_gpio_get_drvstr(drv_st->pin);
}

static void exynos_dwmci_restore_drv_st(void *data, u32 slot_id)
{
	struct dw_mci *host = (struct dw_mci *)data;
	struct drv_strength * drv_st = &host->pdata->__drv_st;

	s5p_gpio_set_drvstr(drv_st->pin, drv_st->val);
}

static void exynos_dwmci_tuning_drv_st(void *data, u32 slot_id)
{
	struct dw_mci *host = (struct dw_mci *)data;
	struct drv_strength * drv_st = &host->pdata->__drv_st;
	unsigned int gpio = drv_st->pin;
	s5p_gpio_drvstr_t drv;

	drv = s5p_gpio_get_drvstr(gpio);
	if (drv == S5P_GPIO_DRVSTR_LV1)
		drv = S5P_GPIO_DRVSTR_LV4;
	else
		drv--;

	s5p_gpio_set_drvstr(gpio, drv);
}

static int exynos_dwmci0_get_bus_wd(u32 slot_id)
{
	return 8;
}

static void exynos_dwmci0_cfg_gpio(int width)
{
	unsigned int gpio;

	for (gpio = EXYNOS5410_GPC0(0);
			gpio < EXYNOS5410_GPC0(2); gpio++) {
		s3c_gpio_cfgpin(gpio, S3C_GPIO_SFN(2));
		s3c_gpio_setpull(gpio, S3C_GPIO_PULL_NONE);
		s5p_gpio_set_drvstr(gpio, S5P_GPIO_DRVSTR_LV4);
	}

	switch (width) {
	case 8:
		for (gpio = EXYNOS5410_GPC3(0);
				gpio <= EXYNOS5410_GPC3(3); gpio++) {
			s3c_gpio_cfgpin(gpio, S3C_GPIO_SFN(2));
			s3c_gpio_setpull(gpio, S3C_GPIO_PULL_NONE);
			s5p_gpio_set_drvstr(gpio, S5P_GPIO_DRVSTR_LV4);
		}
	case 4:
		for (gpio = EXYNOS5410_GPC0(3);
				gpio <= EXYNOS5410_GPC0(6); gpio++) {
			s3c_gpio_cfgpin(gpio, S3C_GPIO_SFN(2));
			s3c_gpio_setpull(gpio, S3C_GPIO_PULL_NONE);
			s5p_gpio_set_drvstr(gpio, S5P_GPIO_DRVSTR_LV4);
		}
		break;
	case 1:
		gpio = EXYNOS5410_GPC0(3);
		s3c_gpio_cfgpin(gpio, S3C_GPIO_SFN(2));
		s3c_gpio_setpull(gpio, S3C_GPIO_PULL_NONE);
		s5p_gpio_set_drvstr(gpio, S5P_GPIO_DRVSTR_LV4);
	default:
		break;
	}

	gpio = EXYNOS5410_GPD1(0);
	s3c_gpio_cfgpin(gpio, S3C_GPIO_SFN(2));
	s3c_gpio_setpull(gpio, S3C_GPIO_PULL_DOWN);
}

static struct dw_mci_board smdk5410_dwmci0_pdata __initdata = {
	.num_slots		= 1,
	.quirks			= DW_MCI_QUIRK_BROKEN_CARD_DETECTION |
				  DW_MCI_QUIRK_HIGHSPEED,
#if defined(CONFIG_SDMMC_CLOCK_CPLL)
    #if defined(EMMC_DDR_MODE)
    // DDR
    	.bus_hz			= 40 * 1000 * 1000,
    #else
    // SDR
    	.bus_hz			= 160 * 1000 * 1000,
    #endif
#else
    #if defined(EMMC_DDR_MODE)
    // DDR	
    	.bus_hz			= 50 * 1000 * 1000,
    #else
    // SDR
    	.bus_hz			= 100 * 1000 * 1000,
    #endif
#endif
	.caps			=	MMC_CAP_CMD23 | MMC_CAP_8_BIT_DATA |
				  		MMC_CAP_UHS_DDR50 | MMC_CAP_1_8V_DDR |
				  		MMC_CAP_ERASE,
#if defined(EMMC_DDR_MODE)
// DDR
	.caps2			= 	MMC_CAP2_POWEROFF_NOTIFY | MMC_CAP2_NO_SLEEP_CMD |
						MMC_CAP2_CACHE_CTRL | MMC_CAP2_BROKEN_VOLTAGE,
#else
	.caps2			= 	MMC_CAP2_HS200 | MMC_CAP2_HS200_1_8V_SDR |
	                    MMC_CAP2_POWEROFF_NOTIFY | MMC_CAP2_NO_SLEEP_CMD |
						MMC_CAP2_CACHE_CTRL | MMC_CAP2_BROKEN_VOLTAGE,
#endif
	.fifo_depth		= 0x80,
	.detect_delay_ms	= 200,
	.only_once_tune		= true,
	.hclk_name		= "dwmci",
	.cclk_name		= "sclk_dwmci",
	.cfg_gpio		= exynos_dwmci0_cfg_gpio,
	.cd_type		= DW_MCI_CD_PERMANENT,
	.get_bus_wd		= exynos_dwmci0_get_bus_wd,
	.save_drv_st		= exynos_dwmci_save_drv_st,
	.restore_drv_st		= exynos_dwmci_restore_drv_st,
	.tuning_drv_st		= exynos_dwmci_tuning_drv_st,
	.sdr_timing		= 0x03040000,
	.ddr_timing		= 0x03020000,
	.clk_drv		= 0x3,
	.ddr200_timing		= 0x01020000,
	.clk_tbl		= exynos_dwmci_clk_rates,
	.__drv_st		= {
		.pin			= EXYNOS5410_GPC0(0),
		.val			= S5P_GPIO_DRVSTR_LV4,
	},
};

static void exynos_dwmci2_cfg_gpio(int width)
{
	unsigned int gpio;

	/* set to pull up pin for write protection */
	/* odroidxu write protection pin is open, default pull down	*/
	/*
	gpio = EXYNOS5410_GPM5(0);
	s3c_gpio_cfgpin(gpio, S3C_GPIO_SFN(2));
	s3c_gpio_setpull(gpio, S3C_GPIO_PULL_NONE);
	s5p_gpio_set_drvstr(gpio, S5P_GPIO_DRVSTR_LV4);
	*/	
	// T-Flash CD_TYPE INTERNAL
	for (gpio = EXYNOS5410_GPC2(0); gpio < EXYNOS5410_GPC2(3); gpio++) {
		s3c_gpio_cfgpin(gpio, S3C_GPIO_SFN(2));
		s3c_gpio_setpull(gpio, S3C_GPIO_PULL_NONE);
		s5p_gpio_set_drvstr(gpio, S5P_GPIO_DRVSTR_LV4);
	}

	switch (width) {
	case 4:
		for (gpio = EXYNOS5410_GPC2(3);
				gpio <= EXYNOS5410_GPC2(6); gpio++) {
			s3c_gpio_cfgpin(gpio, S3C_GPIO_SFN(2));
			s3c_gpio_setpull(gpio, S3C_GPIO_PULL_NONE);
			s5p_gpio_set_drvstr(gpio, S5P_GPIO_DRVSTR_LV4);
		}
		break;
	case 1:
		gpio = EXYNOS5410_GPC2(3);
		s3c_gpio_cfgpin(gpio, S3C_GPIO_SFN(2));
		s3c_gpio_setpull(gpio, S3C_GPIO_PULL_NONE);
		s5p_gpio_set_drvstr(gpio, S5P_GPIO_DRVSTR_LV4);
	default:
		break;
	}

	gpio = EXYNOS5410_GPC2(2);
	s3c_gpio_cfgpin(gpio, S3C_GPIO_SFN(2));
	s3c_gpio_setpull(gpio, S3C_GPIO_PULL_NONE);
	s5p_gpio_set_drvstr(gpio, S5P_GPIO_DRVSTR_LV4);
}

static int exynos_dwmci2_get_bus_wd(u32 slot_id)
{
	if (samsung_rev() < EXYNOS5410_REV_1_0)
		return 1;
	else
		return 4;
}

static struct dw_mci_board smdk5410_dwmci2_pdata __initdata = {
	.num_slots		= 1,
	.quirks			= DW_MCI_QUIRK_BROKEN_CARD_DETECTION | DW_MCI_QUIRK_HIGHSPEED,
	.bus_hz			= 80 * 1000 * 1000,
	.caps			= MMC_CAP_CMD23 | MMC_CAP_4_BIT_DATA |
						MMC_CAP_UHS_SDR50 | MMC_CAP_UHS_SDR25 | MMC_CAP_UHS_SDR104 |
						MMC_CAP_1_8V_DDR | MMC_CAP_UHS_DDR50 |
						MMC_CAP_ERASE,
						
	.fifo_depth		= 0x80,
	.detect_delay_ms	= 200,
	.hclk_name		= "dwmci",
	.cclk_name		= "sclk_dwmci",
	.cfg_gpio		= exynos_dwmci2_cfg_gpio,
	.get_bus_wd		= exynos_dwmci2_get_bus_wd,
	.save_drv_st		= exynos_dwmci_save_drv_st,
	.restore_drv_st		= exynos_dwmci_restore_drv_st,
	.tuning_drv_st		= exynos_dwmci_tuning_drv_st,
	.sdr_timing		= 0x03040000,
	.ddr_timing		= 0x03020000,
	.clk_drv		= 0x3,
	.cd_type		= DW_MCI_CD_INTERNAL,
	.clk_tbl        = exynos_dwmci_clk_rates,
	.__drv_st		= {
		.pin			= EXYNOS5410_GPC2(0),
		.val			= S5P_GPIO_DRVSTR_LV4,
	},
};

static struct platform_device *odroidxu_emmc_devices[] __initdata = {
#ifdef CONFIG_MMC_DW
	&exynos5_device_dwmci0,
	&exynos5_device_dwmci2,
#endif
};

static struct platform_device *odroidxu_tflash_devices[] __initdata = {
#ifdef CONFIG_MMC_DW
	&exynos5_device_dwmci2,
	&exynos5_device_dwmci0,
#endif
};

void __init exynos5_odroidxu_mmc_init(void)
{
	int OM_STAT=0;
	if (samsung_rev() < EXYNOS5410_REV_1_0)
		smdk5410_dwmci0_pdata.caps &=
			~(MMC_CAP_UHS_DDR50 | MMC_CAP_1_8V_DDR);
#ifndef CONFIG_EXYNOS_EMMC_HS200
	smdk5410_dwmci0_pdata.caps2 &=
		~MMC_CAP2_HS200_1_8V_SDR;
#endif
	exynos_dwmci_set_platdata(&smdk5410_dwmci0_pdata, 0);
	exynos_dwmci_set_platdata(&smdk5410_dwmci2_pdata, 2);
	
	OM_STAT = readl(EXYNOS_OM_STAT);
	
	if(OM_STAT == 0x4) { // T-Flash_CH2
		exynos_dwmci_set_platdata(&smdk5410_dwmci2_pdata, 2);
		exynos_dwmci_set_platdata(&smdk5410_dwmci0_pdata, 0);
		platform_add_devices(odroidxu_tflash_devices, ARRAY_SIZE(odroidxu_tflash_devices));
	}
	else {	// emmc44_CH0
		exynos_dwmci_set_platdata(&smdk5410_dwmci0_pdata, 0);
		exynos_dwmci_set_platdata(&smdk5410_dwmci2_pdata, 2);
		platform_add_devices(odroidxu_emmc_devices, ARRAY_SIZE(odroidxu_emmc_devices));
	}
}

void exynos5_odroidxu_mmc_reset(void)
{
	gpio_request(GPIO_EMMC_RESET, "emmc_reset");
	s3c_gpio_setpull(GPIO_EMMC_RESET, S3C_GPIO_PULL_NONE);
	gpio_direction_output(GPIO_EMMC_RESET, 0);
	gpio_free(GPIO_EMMC_RESET);
}
