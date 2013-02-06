/* linux/arch/arm/mach-exynos/mach-smdk4212.c
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/platform_device.h>
#include <linux/serial_core.h>
#include <linux/spi/spi.h>
#include <linux/spi/spi_gpio.h>
#include <linux/clk.h>
#include <linux/gpio.h>
#include <linux/gpio_keys.h>
#include <linux/gpio_event.h>
#include <linux/i2c.h>
#include <linux/i2c-gpio.h>
#include <linux/pwm_backlight.h>
#include <linux/input.h>
#include <linux/mmc/host.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/max8649.h>
#include <linux/regulator/fixed.h>
#ifdef CONFIG_LEDS_AAT1290A
#include <linux/leds-aat1290a.h>
#endif
#ifdef CONFIG_MFD_MAX77693
#include <linux/mfd/max77693.h>
#include <linux/mfd/max77693-private.h>
#include <linux/leds-max77693.h>
#endif

#ifdef CONFIG_BATTERY_MAX17047_FUELGAUGE
#include <linux/battery/max17047_fuelgauge.h>
#endif
#if defined(CONFIG_BATTERY_SAMSUNG)
#include <linux/power_supply.h>
#include <linux/battery/samsung_battery.h>
#endif
#if defined(CONFIG_CHARGER_MAX8922_U1)
#include <linux/power/max8922_charger_u1.h>
#endif

#ifdef CONFIG_BT_BCM4334
#include <mach/board-bluetooth-bcm.h>
#endif

#ifdef CONFIG_STMPE811_ADC
#include <linux/stmpe811-adc.h>
#endif
#include <linux/v4l2-mediabus.h>
#include <linux/memblock.h>
#include <linux/delay.h>
#include <linux/bootmem.h>

#ifdef CONFIG_DMA_CMA
#include <linux/dma-contiguous.h>
#endif

#include <asm/mach/arch.h>
#include <asm/mach-types.h>

#include <plat/regs-serial.h>
#include <plat/exynos4.h>
#include <plat/cpu.h>
#include <plat/clock.h>
#include <plat/keypad.h>
#include <plat/devs.h>
#include <plat/fb-s5p.h>
#include <plat/fb-core.h>
#include <plat/regs-fb-v4.h>
#include <plat/backlight.h>
#include <plat/gpio-cfg.h>
#include <plat/iic.h>
#include <plat/pd.h>
#include <plat/sdhci.h>
#include <plat/mshci.h>
#include <plat/ehci.h>
#include <plat/usbgadget.h>
#include <plat/s3c64xx-spi.h>
#include <plat/tvout.h>
#include <plat/csis.h>
#include <plat/media.h>
#include <plat/adc.h>
#include <media/exynos_fimc_is.h>
#include <mach/exynos-ion.h>

#if defined(CONFIG_TDMB) || defined(CONFIG_TDMB_MODULE)
#include <mach/tdmb_pdata.h>
#elif defined(CONFIG_ISDBT)
#include <media/isdbt_pdata.h>
#endif

#include <mach/map.h>
#include <mach/spi-clocks.h>

#include <mach/dev.h>
#include <mach/ppmu.h>

#ifdef CONFIG_EXYNOS4_SETUP_THERMAL
#include <plat/s5p-tmu.h>
#include <mach/regs-tmu.h>
#endif

#ifdef CONFIG_EXYNOS_C2C
#include <mach/c2c.h>
#endif
#if defined(CONFIG_VIDEO_SAMSUNG_S5P_MFC) || defined(CONFIG_VIDEO_MFC5X)
#include <plat/s5p-mfc.h>
#endif

#include <plat/fb-s5p.h>

#ifdef CONFIG_FB_S5P_EXTDSP
struct s3cfb_extdsp_lcd {
	int	width;
	int	height;
	int	bpp;
};
#endif
#include <mach/dev-sysmmu.h>

#ifdef CONFIG_VIDEO_JPEG_V2X
#include <plat/jpeg.h>
#endif

#include <plat/fimg2d.h>
#include <plat/s5p-sysmmu.h>

#include <mach/sec_debug.h>

#include <mach/gpio-midas.h>
#if defined(CONFIG_MACH_GC1)
#include <mach/gc1-power.h>
#elif defined(CONFIG_MACH_T0)
#include <mach/t0-power.h>
#else
#include <mach/midas-power.h>
#endif
#ifdef CONFIG_SEC_THERMISTOR
#include <mach/sec_thermistor.h>
#endif
#include <mach/midas-thermistor.h>
#include <mach/midas-tsp.h>
#include <mach/regs-clock.h>

#include <mach/midas-lcd.h>
#include <mach/midas-sound.h>

#ifdef CONFIG_INPUT_WACOM
#include <mach/midas-wacom.h>
#endif

#ifdef CONFIG_USB_HOST_NOTIFY
#include <linux/host_notify.h>
#endif

#ifdef CONFIG_KEYBOARD_CYPRESS_TOUCH
#include <linux/i2c/touchkey_i2c.h>
#endif

#if defined(CONFIG_MACH_GC1)
#include <mach/gc1-jack.h>
#endif

#include "board-mobile.h"

/* Following are default values for UCON, ULCON and UFCON UART registers */
#define SMDK4212_UCON_DEFAULT	(S3C2410_UCON_TXILEVEL |	\
				 S3C2410_UCON_RXILEVEL |	\
				 S3C2410_UCON_TXIRQMODE |	\
				 S3C2410_UCON_RXIRQMODE |	\
				 S3C2410_UCON_RXFIFO_TOI |	\
				 S3C2443_UCON_RXERR_IRQEN)

#define SMDK4212_ULCON_DEFAULT	S3C2410_LCON_CS8

#define SMDK4212_UFCON_DEFAULT	(S3C2410_UFCON_FIFOMODE |	\
				 S5PV210_UFCON_TXTRIG4 |	\
				 S5PV210_UFCON_RXTRIG4)

#define SMDK4212_UFCON_GPS	(S3C2410_UFCON_FIFOMODE |	\
				S5PV210_UFCON_TXTRIG8 |	\
				S5PV210_UFCON_RXTRIG32)

static struct s3c2410_uartcfg smdk4212_uartcfgs[] __initdata = {
	[0] = {
		.hwport		= 0,
		.flags		= 0,
		.ucon		= SMDK4212_UCON_DEFAULT,
		.ulcon		= SMDK4212_ULCON_DEFAULT,
		.ufcon		= SMDK4212_UFCON_DEFAULT,
#ifdef CONFIG_BT_BCM4334
		.wake_peer = bcm_bt_lpm_exit_lpm_locked,
#endif
	},
	[1] = {
		.hwport		= 1,
		.flags		= 0,
		.ucon		= SMDK4212_UCON_DEFAULT,
		.ulcon		= SMDK4212_ULCON_DEFAULT,
		.ufcon		= SMDK4212_UFCON_GPS,
		.set_runstate	= set_gps_uart_op,
	},
	[2] = {
		.hwport		= 2,
		.flags		= 0,
		.ucon		= SMDK4212_UCON_DEFAULT,
		.ulcon		= SMDK4212_ULCON_DEFAULT,
		.ufcon		= SMDK4212_UFCON_DEFAULT,
	},
	[3] = {
		.hwport		= 3,
		.flags		= 0,
		.ucon		= SMDK4212_UCON_DEFAULT,
		.ulcon		= SMDK4212_ULCON_DEFAULT,
		.ufcon		= SMDK4212_UFCON_DEFAULT,
	},
};

#if defined(CONFIG_S3C64XX_DEV_SPI)
#if defined(CONFIG_VIDEO_S5C73M3_SPI)

static struct s3c64xx_spi_csinfo spi1_csi[] = {
	[0] = {
		.line = EXYNOS4_GPB(5),
		.set_level = gpio_set_value,
		.fb_delay = 0x00,
	},
};

static struct spi_board_info spi1_board_info[] __initdata = {
	{
		.modalias = "s5c73m3_spi",
		.platform_data = NULL,
		.max_speed_hz = 50000000,
		.bus_num = 1,
		.chip_select = 0,
		.mode = SPI_MODE_0,
		.controller_data = &spi1_csi[0],
	}
};
#endif

#if defined(CONFIG_LINK_DEVICE_SPI)  \
	|| defined(CONFIG_TDMB) || defined(CONFIG_TDMB_MODULE) \
	|| defined(CONFIG_ISDBT) || defined(CONFIG_LINK_DEVICE_PLD)
static struct s3c64xx_spi_csinfo spi2_csi[] = {
	[0] = {
		.line = EXYNOS4_GPC1(2),
		.set_level = gpio_set_value,
	},
};

static struct spi_board_info spi2_board_info[] __initdata = {
#if defined(CONFIG_TDMB) || defined(CONFIG_TDMB_MODULE)
	{
		.modalias = "tdmbspi",
		.platform_data = NULL,
		.max_speed_hz = 5000000,
		.bus_num = 2,
		.chip_select = 0,
		.mode = SPI_MODE_0,
		.controller_data = &spi2_csi[0],
	},
#elif defined(CONFIG_ISDBT)
	{
		.modalias = "fc8150_spi",
		.platform_data = NULL,
		.max_speed_hz = 5000000,
		.bus_num = 2,
		.chip_select = 0,
		.mode = SPI_MODE_0,
		.controller_data = &spi2_csi[0],
	},
#else
	{
		.modalias = "modem_if_spi",
		.platform_data = NULL,
		.bus_num = 2,
		.chip_select = 0,
		.max_speed_hz = 12*1000*1000,
		.mode = SPI_MODE_1,
		.controller_data = &spi2_csi[0],
	}
#endif
};
#endif
#endif

static struct i2c_board_info i2c_devs8_emul[];

#ifdef CONFIG_KEYBOARD_CYPRESS_TOUCH
static void touchkey_init_hw(void)
{
#if defined(CONFIG_MACH_M0) || defined(CONFIG_MACH_C1)
#if defined(CONFIG_MACH_M0_CHNOPEN) || defined(CONFIG_MACH_M0_HKTW) || \
	defined(CONFIG_TARGET_LOCALE_KOR)
	/* do nothing */
#elif defined(CONFIG_MACH_C1)
#if defined(CONFIG_MACH_C1_KOR_SKT) || defined(CONFIG_MACH_C1_KOR_KT)
	if (system_rev < 8)
		return;
#elif defined(CONFIG_MACH_C1_KOR_LGT)
	if (system_rev < 5)
		return;
#else
	if (system_rev < 7)
		return;
#endif
#else
	if (system_rev < 11)
		return; /* rev 1.0 */
#endif
#endif

#if defined(CONFIG_TARGET_LOCALE_KOR) || \
	defined(CONFIG_MACH_M0) || \
	defined(CONFIG_MACH_C1) || \
	defined(CONFIG_MACH_M3) || \
	defined(CONFIG_MACH_T0) || \
	defined(CONFIG_MACH_SUPERIOR_KOR_SKT)

#if defined(CONFIG_MACH_M3_JPN_DCM)
	if (system_rev < 3)
		gpio_request(GPIO_3_TOUCH_EN_R1, "gpio_3_touch_en");
	else
		gpio_request(GPIO_3_TOUCH_EN, "gpio_3_touch_en");
#else
	gpio_request(GPIO_3_TOUCH_EN, "gpio_3_touch_en");
#endif
#if defined(CONFIG_MACH_C1_KOR_LGT)
	gpio_request(GPIO_3_TOUCH_LDO_EN, "gpio_3_touch_ldo_en");
#endif
#endif

	gpio_request(GPIO_3_TOUCH_INT, "3_TOUCH_INT");
	s3c_gpio_setpull(GPIO_3_TOUCH_INT, S3C_GPIO_PULL_NONE);
	s5p_register_gpio_interrupt(GPIO_3_TOUCH_INT);
	gpio_direction_input(GPIO_3_TOUCH_INT);

	i2c_devs8_emul[0].irq = gpio_to_irq(GPIO_3_TOUCH_INT);
	irq_set_irq_type(gpio_to_irq(GPIO_3_TOUCH_INT), IRQF_TRIGGER_FALLING);
	s3c_gpio_cfgpin(GPIO_3_TOUCH_INT, S3C_GPIO_SFN(0xf));

	s3c_gpio_setpull(GPIO_3_TOUCH_SCL, S3C_GPIO_PULL_DOWN);
	s3c_gpio_setpull(GPIO_3_TOUCH_SDA, S3C_GPIO_PULL_DOWN);
}

static int touchkey_suspend(void)
{
	struct regulator *regulator;
#if defined(CONFIG_MACH_SUPERIOR_KOR_SKT)
	struct regulator *regulator_ldo17;
#endif

	regulator = regulator_get(NULL, TK_REGULATOR_NAME);
	if (IS_ERR(regulator))
		return 0;
	if (regulator_is_enabled(regulator))
		regulator_force_disable(regulator);
#if defined(CONFIG_MACH_SUPERIOR_KOR_SKT)
	regulator_ldo17 = regulator_get(NULL, TK_VDD_REGULATOR);
	if (IS_ERR(regulator_ldo17))
		return 0;
	if (regulator_is_enabled(regulator_ldo17))
		regulator_force_disable(regulator_ldo17);
#endif

#if defined(CONFIG_MACH_C1_KOR_LGT)
	gpio_request(GPIO_3_TOUCH_LDO_EN, "gpio_3_touch_ldo_en");
	gpio_direction_output(GPIO_3_TOUCH_LDO_EN, 0);
#endif

	s3c_gpio_setpull(GPIO_3_TOUCH_SCL, S3C_GPIO_PULL_DOWN);
	s3c_gpio_setpull(GPIO_3_TOUCH_SDA, S3C_GPIO_PULL_DOWN);

	regulator_put(regulator);
#if defined(CONFIG_MACH_SUPERIOR_KOR_SKT)
	regulator_put(regulator_ldo17);
#endif

	return 1;
}

static int touchkey_resume(void)
{
	struct regulator *regulator;
#if defined(CONFIG_MACH_SUPERIOR_KOR_SKT)
	struct regulator *regulator_ldo17;
#endif

#if defined(CONFIG_MACH_SUPERIOR_KOR_SKT)
	regulator_ldo17 = regulator_get(NULL, TK_VDD_REGULATOR);
	if (IS_ERR(regulator_ldo17))
		return 0;
	regulator_enable(regulator_ldo17);
#endif
	regulator = regulator_get(NULL, TK_REGULATOR_NAME);
	if (IS_ERR(regulator))
		return 0;
	regulator_enable(regulator);
	#if defined(CONFIG_MACH_C1_KOR_LGT)
	gpio_request(GPIO_3_TOUCH_LDO_EN, "gpio_3_touch_ldo_en");
	gpio_direction_output(GPIO_3_TOUCH_LDO_EN, 1);
	#endif
#if defined(CONFIG_MACH_SUPERIOR_KOR_SKT)
	regulator_put(regulator_ldo17);
#endif
	regulator_put(regulator);

	s3c_gpio_setpull(GPIO_3_TOUCH_SCL, S3C_GPIO_PULL_NONE);
	s3c_gpio_setpull(GPIO_3_TOUCH_SDA, S3C_GPIO_PULL_NONE);

	return 1;
}

static int touchkey_power_on(bool on)
{
	int ret;

	if (on) {
		gpio_direction_output(GPIO_3_TOUCH_INT, 1);

		ret = touchkey_resume();

		irq_set_irq_type(gpio_to_irq(GPIO_3_TOUCH_INT),
			IRQF_TRIGGER_FALLING);
		s3c_gpio_cfgpin(GPIO_3_TOUCH_INT, S3C_GPIO_SFN(0xf));
		s3c_gpio_setpull(GPIO_3_TOUCH_INT, S3C_GPIO_PULL_NONE);
	} else {
		gpio_direction_input(GPIO_3_TOUCH_INT);
		ret = touchkey_suspend();
	}

	return ret;
}

static int touchkey_led_power_on(bool on)
{
#if defined(LED_LDO_WITH_EN_PIN)
#if defined(CONFIG_MACH_M3_JPN_DCM)
	if (system_rev < 3) {
		if (on)
			gpio_direction_output(GPIO_3_TOUCH_EN_R1, 1);
		else
			gpio_direction_output(GPIO_3_TOUCH_EN_R1, 0);
	} else {
		if (on)
			gpio_direction_output(GPIO_3_TOUCH_EN, 1);
		else
			gpio_direction_output(GPIO_3_TOUCH_EN, 0);
	}
#else
	if (on)
		gpio_direction_output(GPIO_3_TOUCH_EN, 1);
	else
		gpio_direction_output(GPIO_3_TOUCH_EN, 0);
#endif
#else
	struct regulator *regulator;

	if (on) {
		regulator = regulator_get(NULL, "touch_led");
		if (IS_ERR(regulator))
			return 0;
		regulator_enable(regulator);
		regulator_put(regulator);
	} else {
		regulator = regulator_get(NULL, "touch_led");
		if (IS_ERR(regulator))
			return 0;
		if (regulator_is_enabled(regulator))
			regulator_force_disable(regulator);
		regulator_put(regulator);
	}
#endif
	return 1;
}

static struct touchkey_platform_data touchkey_pdata = {
	.gpio_sda = GPIO_3_TOUCH_SDA,
	.gpio_scl = GPIO_3_TOUCH_SCL,
	.gpio_int = GPIO_3_TOUCH_INT,
	.init_platform_hw = touchkey_init_hw,
	.suspend = touchkey_suspend,
	.resume = touchkey_resume,
	.power_on = touchkey_power_on,
	.led_power_on = touchkey_led_power_on,
};
#endif /*CONFIG_KEYBOARD_CYPRESS_TOUCH*/


#if defined(CONFIG_TDMB) || defined(CONFIG_TDMB_MODULE)
static void tdmb_set_config_poweron(void)
{
	s3c_gpio_cfgpin(GPIO_TDMB_EN, S3C_GPIO_OUTPUT);
	s3c_gpio_setpull(GPIO_TDMB_EN, S3C_GPIO_PULL_NONE);
	gpio_set_value(GPIO_TDMB_EN, GPIO_LEVEL_LOW);
#if defined(CONFIG_MACH_T0) || defined(CONFIG_MACH_BAFFIN)
	s3c_gpio_cfgpin(GPIO_TDMB_RST_N, S3C_GPIO_OUTPUT);
	s3c_gpio_setpull(GPIO_TDMB_RST_N, S3C_GPIO_PULL_NONE);
	gpio_set_value(GPIO_TDMB_RST_N, GPIO_LEVEL_LOW);
#endif
	s3c_gpio_cfgpin(GPIO_TDMB_INT, S3C_GPIO_SFN(0xf));
	s3c_gpio_setpull(GPIO_TDMB_INT, S3C_GPIO_PULL_NONE);

	s3c_gpio_cfgpin(GPIO_TDMB_SPI_CLK, S3C_GPIO_SFN(5));
	s3c_gpio_cfgpin(GPIO_TDMB_SPI_MISO, S3C_GPIO_SFN(5));
	s3c_gpio_cfgpin(GPIO_TDMB_SPI_MOSI, S3C_GPIO_SFN(5));
	s3c_gpio_setpull(GPIO_TDMB_SPI_CLK, S3C_GPIO_PULL_DOWN);
	s3c_gpio_setpull(GPIO_TDMB_SPI_MISO, S3C_GPIO_PULL_DOWN);
	s3c_gpio_setpull(GPIO_TDMB_SPI_MOSI, S3C_GPIO_PULL_DOWN);
}
static void tdmb_set_config_poweroff(void)
{
	s3c_gpio_cfgpin(GPIO_TDMB_EN, S3C_GPIO_OUTPUT);
	s3c_gpio_setpull(GPIO_TDMB_EN, S3C_GPIO_PULL_NONE);
	gpio_set_value(GPIO_TDMB_EN, GPIO_LEVEL_LOW);

#if defined(CONFIG_MACH_T0) || defined(CONFIG_MACH_BAFFIN)
	s3c_gpio_cfgpin(GPIO_TDMB_RST_N, S3C_GPIO_OUTPUT);
	s3c_gpio_setpull(GPIO_TDMB_RST_N, S3C_GPIO_PULL_NONE);
	gpio_set_value(GPIO_TDMB_RST_N, GPIO_LEVEL_LOW);
#endif

	s3c_gpio_cfgpin(GPIO_TDMB_INT, S3C_GPIO_OUTPUT);
	s3c_gpio_setpull(GPIO_TDMB_INT, S3C_GPIO_PULL_NONE);
	gpio_set_value(GPIO_TDMB_INT, GPIO_LEVEL_LOW);
}

static void tdmb_gpio_on(void)
{
	printk(KERN_DEBUG "tdmb_gpio_on\n");

	tdmb_set_config_poweron();

	gpio_set_value(GPIO_TDMB_EN, GPIO_LEVEL_LOW);
	usleep_range(1000, 1000);
	gpio_set_value(GPIO_TDMB_EN, GPIO_LEVEL_HIGH);

#if defined(CONFIG_MACH_T0) || defined(CONFIG_MACH_BAFFIN)
	usleep_range(1000, 1000);
	gpio_set_value(GPIO_TDMB_RST_N, GPIO_LEVEL_LOW);
	usleep_range(2000, 2000);
	gpio_set_value(GPIO_TDMB_RST_N, GPIO_LEVEL_HIGH);
	usleep_range(1000, 1000);
#endif

}

static void tdmb_gpio_off(void)
{
	printk(KERN_DEBUG "tdmb_gpio_off\n");

	tdmb_set_config_poweroff();
#if defined(CONFIG_MACH_T0) || defined(CONFIG_MACH_BAFFIN)
	gpio_set_value(GPIO_TDMB_RST_N, GPIO_LEVEL_LOW);
	usleep_range(1000, 1000);
#endif

	gpio_set_value(GPIO_TDMB_EN, GPIO_LEVEL_LOW);
}

static struct tdmb_platform_data tdmb_pdata = {
	.gpio_on = tdmb_gpio_on,
	.gpio_off = tdmb_gpio_off,
};

static struct platform_device tdmb_device = {
	.name			= "tdmb",
	.id				= -1,
	.dev			= {
		.platform_data = &tdmb_pdata,
	},
};

static int __init tdmb_dev_init(void)
{
#if defined(CONFIG_MACH_T0) && defined(CONFIG_TDMB_ANT_DET)
	unsigned int tdmb_ant_det_gpio;
	unsigned int tdmb_ant_det_irq;
	if (system_rev >= 6) {
		tdmb_ant_det_gpio = GPIO_TDMB_ANT_DET_REV08;
		tdmb_ant_det_irq = GPIO_TDMB_IRQ_ANT_DET_REV08;
	} else {
		s5p_register_gpio_interrupt(GPIO_TDMB_ANT_DET);
		tdmb_ant_det_gpio = GPIO_TDMB_ANT_DET;
		tdmb_ant_det_irq = GPIO_TDMB_IRQ_ANT_DET;
	}
	s3c_gpio_cfgpin(tdmb_ant_det_gpio, S3C_GPIO_SFN(0xf));
	s3c_gpio_setpull(tdmb_ant_det_gpio, S3C_GPIO_PULL_NONE);
	tdmb_pdata.gpio_ant_det = tdmb_ant_det_gpio;
	tdmb_pdata.irq_ant_det = tdmb_ant_det_irq;
#endif
	tdmb_set_config_poweroff();

	s5p_register_gpio_interrupt(GPIO_TDMB_INT);
	tdmb_pdata.irq = GPIO_TDMB_IRQ;
	platform_device_register(&tdmb_device);

	return 0;
}
#elif defined(CONFIG_ISDBT)
static void isdbt_set_config_poweron(void)
{
	s3c_gpio_cfgpin(GPIO_ISDBT_EN, S3C_GPIO_OUTPUT);
	s3c_gpio_setpull(GPIO_ISDBT_EN, S3C_GPIO_PULL_NONE);
	gpio_set_value(GPIO_ISDBT_EN, GPIO_LEVEL_LOW);
	s3c_gpio_cfgpin(GPIO_ISDBT_RST_N, S3C_GPIO_OUTPUT);
	s3c_gpio_setpull(GPIO_ISDBT_RST_N, S3C_GPIO_PULL_NONE);
	gpio_set_value(GPIO_ISDBT_RST_N, GPIO_LEVEL_LOW);
	s3c_gpio_cfgpin(GPIO_ISDBT_INT, S3C_GPIO_SFN(GPIO_ISDBT_INT_AF));
	s3c_gpio_setpull(GPIO_ISDBT_INT, S3C_GPIO_PULL_NONE);

	s3c_gpio_cfgpin(GPIO_ISDBT_SPI_CLK, S3C_GPIO_SFN(5));
	s3c_gpio_cfgpin(GPIO_ISDBT_SPI_MISO, S3C_GPIO_SFN(5));
	s3c_gpio_cfgpin(GPIO_ISDBT_SPI_MOSI, S3C_GPIO_SFN(5));
	s3c_gpio_setpull(GPIO_ISDBT_SPI_CLK, S3C_GPIO_PULL_DOWN);
	s3c_gpio_setpull(GPIO_ISDBT_SPI_MISO, S3C_GPIO_PULL_DOWN);
	s3c_gpio_setpull(GPIO_ISDBT_SPI_MOSI, S3C_GPIO_PULL_DOWN);

	printk(KERN_DEBUG "isdbt_set_config_poweron\n");

}
static void isdbt_set_config_poweroff(void)
{
	s3c_gpio_cfgpin(GPIO_ISDBT_EN, S3C_GPIO_OUTPUT);
	s3c_gpio_setpull(GPIO_ISDBT_EN, S3C_GPIO_PULL_NONE);
	gpio_set_value(GPIO_ISDBT_EN, GPIO_LEVEL_LOW);

	s3c_gpio_cfgpin(GPIO_ISDBT_RST_N, S3C_GPIO_OUTPUT);
	s3c_gpio_setpull(GPIO_ISDBT_RST_N, S3C_GPIO_PULL_NONE);
	gpio_set_value(GPIO_ISDBT_RST_N, GPIO_LEVEL_LOW);

	s3c_gpio_cfgpin(GPIO_ISDBT_INT, S3C_GPIO_OUTPUT);
	s3c_gpio_setpull(GPIO_ISDBT_INT, S3C_GPIO_PULL_NONE);
	gpio_set_value(GPIO_ISDBT_INT, GPIO_LEVEL_LOW);


	printk(KERN_DEBUG "isdbt_set_config_poweroff\n");
}

static void isdbt_gpio_on(void)
{
	printk(KERN_DEBUG "isdbt_gpio_on\n");

	isdbt_set_config_poweron();

	gpio_set_value(GPIO_ISDBT_EN, GPIO_LEVEL_LOW);
	usleep_range(1000, 1000);
	gpio_set_value(GPIO_ISDBT_EN, GPIO_LEVEL_HIGH);

	usleep_range(1000, 1000);
	gpio_set_value(GPIO_ISDBT_RST_N, GPIO_LEVEL_LOW);
	usleep_range(2000, 2000);
	gpio_set_value(GPIO_ISDBT_RST_N, GPIO_LEVEL_HIGH);
	usleep_range(1000, 1000);

}

static void isdbt_gpio_off(void)
{
	printk(KERN_DEBUG "isdbt_gpio_off\n");

	isdbt_set_config_poweroff();

	gpio_set_value(GPIO_ISDBT_RST_N, GPIO_LEVEL_LOW);
	usleep_range(1000, 1000);

	gpio_set_value(GPIO_ISDBT_EN, GPIO_LEVEL_LOW);
}

static struct isdbt_platform_data isdbt_pdata = {
	.gpio_on = isdbt_gpio_on,
	.gpio_off = isdbt_gpio_off,
};

static struct platform_device isdbt_device = {
	.name			= "isdbt",
	.id				= -1,
	.dev			= {
		.platform_data = &isdbt_pdata,
	},
};

static int __init isdbt_dev_init(void)
{
#if defined(CONFIG_MACH_T0_JPN_LTE_DCM) && defined(CONFIG_ISDBT_ANT_DET)
	s5p_register_gpio_interrupt(GPIO_ISDBT_ANT_DET);
	s3c_gpio_cfgpin(GPIO_ISDBT_ANT_DET, S3C_GPIO_SFN(0xf));
	s3c_gpio_setpull(GPIO_ISDBT_ANT_DET, S3C_GPIO_PULL_NONE);
	isdbt_pdata.gpio_ant_det = GPIO_ISDBT_ANT_DET;
	isdbt_pdata.irq_ant_det = GPIO_ISDBT_IRQ_ANT_DET;
#endif
	isdbt_set_config_poweroff();
	s5p_register_gpio_interrupt(GPIO_ISDBT_INT);
	isdbt_pdata.irq = GPIO_ISDBT_IRQ;
	platform_device_register(&isdbt_device);

	printk(KERN_DEBUG "isdbt_dev_init\n");

	return 0;
}
#endif

#ifdef CONFIG_LEDS_AAT1290A
static int aat1290a_initGpio(void)
{
	int err;

	err = gpio_request(GPIO_CAM_SW_EN, "CAM_SW_EN");
	if (err) {
		printk(KERN_ERR "failed to request CAM_SW_EN\n");
		return -EPERM;
	}
	gpio_direction_output(GPIO_CAM_SW_EN, 1);

	return 0;
}

static void aat1290a_switch(int enable)
{
	gpio_set_value(GPIO_CAM_SW_EN, enable);
}

static int aat1290a_setGpio(void)
{
	int err;

	err = gpio_request(GPIO_TORCH_EN, "TORCH_EN");
	if (err) {
		printk(KERN_ERR "failed to request TORCH_EN\n");
		return -EPERM;
	}
	gpio_direction_output(GPIO_TORCH_EN, 0);
	err = gpio_request(GPIO_TORCH_SET, "TORCH_SET");
	if (err) {
		printk(KERN_ERR "failed to request TORCH_SET\n");
		gpio_free(GPIO_TORCH_EN);
		return -EPERM;
	}
	gpio_direction_output(GPIO_TORCH_SET, 0);

	return 0;
}

static int aat1290a_freeGpio(void)
{
	gpio_free(GPIO_TORCH_EN);
	gpio_free(GPIO_TORCH_SET);

	return 0;
}

static void aat1290a_torch_en(int onoff)
{
	gpio_set_value(GPIO_TORCH_EN, onoff);
}

static void aat1290a_torch_set(int onoff)
{
	gpio_set_value(GPIO_TORCH_SET, onoff);
}

static struct aat1290a_led_platform_data aat1290a_led_data = {
	.brightness = TORCH_BRIGHTNESS_50,
	.status	= STATUS_UNAVAILABLE,
	.switch_sel = aat1290a_switch,
	.initGpio = aat1290a_initGpio,
	.setGpio = aat1290a_setGpio,
	.freeGpio = aat1290a_freeGpio,
	.torch_en = aat1290a_torch_en,
	.torch_set = aat1290a_torch_set,
};

static struct platform_device s3c_device_aat1290a_led = {
	.name	= "aat1290a-led",
	.id	= -1,
	.dev	= {
		.platform_data	= &aat1290a_led_data,
	},
};
#endif

static DEFINE_MUTEX(notify_lock);

#define DEFINE_MMC_CARD_NOTIFIER(num) \
static void (*hsmmc##num##_notify_func)(struct platform_device *, int state); \
static int ext_cd_init_hsmmc##num(void (*notify_func)( \
			struct platform_device *, int state)) \
{ \
	mutex_lock(&notify_lock); \
	WARN_ON(hsmmc##num##_notify_func); \
	hsmmc##num##_notify_func = notify_func; \
	mutex_unlock(&notify_lock); \
	return 0; \
} \
static int ext_cd_cleanup_hsmmc##num(void (*notify_func)( \
			struct platform_device *, int state)) \
{ \
	mutex_lock(&notify_lock); \
	WARN_ON(hsmmc##num##_notify_func != notify_func); \
	hsmmc##num##_notify_func = NULL; \
	mutex_unlock(&notify_lock); \
	return 0; \
}

#ifdef CONFIG_S3C_DEV_HSMMC3
	DEFINE_MMC_CARD_NOTIFIER(3)
#endif

/*
 * call this when you need sd stack to recognize insertion or removal of card
 * that can't be told by SDHCI regs
 */
void mmc_force_presence_change(struct platform_device *pdev)
{
	void (*notify_func)(struct platform_device *, int state) = NULL;
	mutex_lock(&notify_lock);
#ifdef CONFIG_S3C_DEV_HSMMC3
	if (pdev == &s3c_device_hsmmc3)
		notify_func = hsmmc3_notify_func;
#endif

	if (notify_func)
		notify_func(pdev, 1);
	else
		pr_warn("%s: called for device with no notifier\n", __func__);
	mutex_unlock(&notify_lock);
}
EXPORT_SYMBOL_GPL(mmc_force_presence_change);

#ifdef CONFIG_S3C_DEV_HSMMC
static struct s3c_sdhci_platdata smdk4212_hsmmc0_pdata __initdata = {
	.cd_type		= S3C_MSHCI_CD_PERMANENT,
	.clk_type		= S3C_SDHCI_CLK_DIV_EXTERNAL,
#ifdef CONFIG_EXYNOS4_SDHCI_CH0_8BIT
	.max_width		= 8,
	.host_caps		= MMC_CAP_8_BIT_DATA,
#endif
};
#endif

#ifdef CONFIG_S3C_DEV_HSMMC1
static struct s3c_sdhci_platdata smdk4212_hsmmc1_pdata __initdata = {
	.cd_type		= S3C_SDHCI_CD_INTERNAL,
	.clk_type		= S3C_SDHCI_CLK_DIV_EXTERNAL,
};
#endif

#ifdef CONFIG_S3C_DEV_HSMMC2
static struct s3c_sdhci_platdata smdk4212_hsmmc2_pdata __initdata = {
	.cd_type		= S3C_SDHCI_CD_GPIO,
	.ext_cd_gpio		= EXYNOS4_GPX3(4),
	.ext_cd_gpio_invert	= true,
	.clk_type		= S3C_SDHCI_CLK_DIV_EXTERNAL,
	.vmmc_name		= "vtf_2.8v"
#ifdef CONFIG_EXYNOS4_SDHCI_CH2_8BIT
	.max_width		= 8,
	.host_caps		= MMC_CAP_8_BIT_DATA,
#endif
};
#endif

#ifdef CONFIG_S3C_DEV_HSMMC3
static struct s3c_sdhci_platdata smdk4212_hsmmc3_pdata __initdata = {
/* new code for brm4334 */
	.cd_type		= S3C_SDHCI_CD_EXTERNAL,

	.clk_type		= S3C_SDHCI_CLK_DIV_EXTERNAL,
	.pm_flags = S3C_SDHCI_PM_IGNORE_SUSPEND_RESUME,
	.ext_cd_init = ext_cd_init_hsmmc3,
	.ext_cd_cleanup = ext_cd_cleanup_hsmmc3,
};
#endif

#ifdef CONFIG_EXYNOS4_DEV_MSHC
static struct s3c_mshci_platdata exynos4_mshc_pdata __initdata = {
	.cd_type		= S3C_MSHCI_CD_PERMANENT,
	.fifo_depth		= 0x80,
#if defined(CONFIG_EXYNOS4_MSHC_8BIT) && \
	defined(CONFIG_EXYNOS4_MSHC_DDR)
	.max_width		= 8,
	.host_caps		= MMC_CAP_8_BIT_DATA | MMC_CAP_1_8V_DDR |
				  MMC_CAP_UHS_DDR50 | MMC_CAP_CMD23,
#ifdef CONFIG_MMC_MSHCI_ENABLE_CACHE
	.host_caps2		= MMC_CAP2_ADAPT_PACKED | MMC_CAP2_PACKED_CMD |
				  MMC_CAP2_CACHE_CTRL,
#else
	.host_caps2		= MMC_CAP2_ADAPT_PACKED | MMC_CAP2_PACKED_CMD,
#endif
#elif defined(CONFIG_EXYNOS4_MSHC_8BIT)
	.max_width		= 8,
	.host_caps		= MMC_CAP_8_BIT_DATA | MMC_CAP_CMD23,
#elif defined(CONFIG_EXYNOS4_MSHC_DDR)
	.host_caps		= MMC_CAP_1_8V_DDR | MMC_CAP_UHS_DDR50 |
				  MMC_CAP_CMD23,
#endif
	.int_power_gpio		= GPIO_eMMC_EN,
};
#endif

#ifdef CONFIG_USB_EHCI_S5P
static struct s5p_ehci_platdata smdk4212_ehci_pdata;

static void __init smdk4212_ehci_init(void)
{
	struct s5p_ehci_platdata *pdata = &smdk4212_ehci_pdata;

	s5p_ehci_set_platdata(pdata);
}
#endif

#ifdef CONFIG_USB_OHCI_S5P
static struct s5p_ohci_platdata smdk4212_ohci_pdata;

static void __init smdk4212_ohci_init(void)
{
	struct s5p_ohci_platdata *pdata = &smdk4212_ohci_pdata;

	s5p_ohci_set_platdata(pdata);
}
#endif

/* USB GADGET */
#ifdef CONFIG_USB_GADGET
static struct s5p_usbgadget_platdata smdk4212_usbgadget_pdata;

#include <linux/usb/android_composite.h>
static void __init smdk4212_usbgadget_init(void)
{
	struct s5p_usbgadget_platdata *pdata = &smdk4212_usbgadget_pdata;
	struct android_usb_platform_data *android_pdata =
		s3c_device_android_usb.dev.platform_data;
	if (android_pdata) {
		unsigned int cdfs = 0;
#if defined(CONFIG_MACH_M0_CTC) || defined(CONFIG_MACH_T0_CHN_CTC) || \
	defined(CONFIG_MACH_M0_DUOSCTC)
		unsigned int newluns = 1;
		cdfs = 1;   /* China CTC required CDFS */
#elif defined(CONFIG_MACH_T0_USA_VZW)
		unsigned int newluns = 0;
		cdfs = 1;   /* VZW required CDFS */
#else
		unsigned int newluns = 2;
#endif
		printk(KERN_DEBUG "usb: %s: default luns=%d, new luns=%d\n",
				__func__, android_pdata->nluns, newluns);
		android_pdata->nluns = newluns;
		android_pdata->cdfs_support = cdfs;
	} else {
		printk(KERN_DEBUG "usb: %s android_pdata is not available\n",
				__func__);
	}

	s5p_usbgadget_set_platdata(pdata);

#if defined(CONFIG_MACH_C1_KOR_SKT) || defined(CONFIG_MACH_C1_KOR_KT) || \
	defined(CONFIG_MACH_C1_KOR_LGT) || defined(CONFIG_MACH_BAFFIN)
	pdata = s3c_device_usbgadget.dev.platform_data;
	if (pdata) {
		/* Squelch Threshold Tune [13:11] (111 : -20%) */
		pdata->phy_tune_mask |= (0x7 << 11);
		pdata->phy_tune |= (0x7 << 11);
		printk(KERN_DEBUG "usb: %s tune_mask=0x%x, tune=0x%x\n",
			__func__, pdata->phy_tune_mask, pdata->phy_tune);
	}
#endif

}
#endif

#ifdef CONFIG_MACH_GC1
static void motor_init_hw(void)
{
	if (gpio_request(EXYNOS4_GPD0(0), "VIBTONE_PWM") < 0)
		printk(KERN_ERR "[VIB] gpio requst is failed\n");
	else {
		gpio_direction_output(EXYNOS4_GPD0(0), 0);
		printk(KERN_DEBUG "[VIB] gpio request is succeed\n");
	}
}

static void motor_en(bool enable)
{
	gpio_direction_output(EXYNOS4_GPD0(0), enable);
	printk(KERN_DEBUG "[VIB] motor_enabled GPIO GPD0(0) : %d\n",
	       gpio_get_value(EXYNOS4_GPD0(0)));
}
#endif
#ifdef CONFIG_MACH_BAFFIN
static void motor_en(bool enable)
{
	gpio_direction_output(EXYNOS4_GPY2(2), enable);
	printk(KERN_DEBUG "[VIB] motor_enabled GPIO GPY2(2) : %d\n",
	       gpio_get_value(EXYNOS4_GPY2(2)));
}
#endif
#if defined(CONFIG_MACH_T0) && defined(CONFIG_TARGET_LOCALE_KOR) || \
	defined(CONFIG_MACH_T0_JPN_LTE_DCM)
static void motor_en(bool enable)
{
	gpio_direction_output(EXYNOS4_GPC0(3), enable);
	printk(KERN_DEBUG "[VIB] motor_enabled GPIO GPC0(3) : %d\n",
	       gpio_get_value(EXYNOS4_GPC0(3)));
}
#endif

#ifdef CONFIG_MFD_MAX77693
#ifdef CONFIG_VIBETONZ
static struct max77693_haptic_platform_data max77693_haptic_pdata = {
#ifdef CONFIG_MACH_GC1
	.reg2 = MOTOR_ERM,
	.pwm_id = 1,
	.init_hw = motor_init_hw,
	.motor_en = motor_en,
#else
	.reg2 = MOTOR_LRA | EXT_PWM | DIVIDER_128,
	.pwm_id = 0,
	.init_hw = NULL,
	.motor_en = NULL,
#endif
	.max_timeout = 10000,
	.duty = 35500,
#if defined(CONFIG_MACH_SUPERIOR_KOR_SKT)
	.period = 38295,
#else
	.period = 37904,
#endif
	.regulator_name = "vmotor",
};
#endif

#ifdef CONFIG_LEDS_MAX77693
static struct max77693_led_platform_data max77693_led_pdata = {
	.num_leds = 4,

	.leds[0].name = "leds-sec1",
	.leds[0].id = MAX77693_FLASH_LED_1,
	.leds[0].timer = MAX77693_FLASH_TIME_500MS,
	.leds[0].timer_mode = MAX77693_TIMER_MODE_MAX_TIMER,
	.leds[0].cntrl_mode = MAX77693_LED_CTRL_BY_FLASHSTB,
	.leds[0].brightness = 0x1F,

	.leds[1].name = "leds-sec2",
	.leds[1].id = MAX77693_FLASH_LED_2,
	.leds[1].timer = MAX77693_FLASH_TIME_500MS,
	.leds[1].timer_mode = MAX77693_TIMER_MODE_MAX_TIMER,
	.leds[1].cntrl_mode = MAX77693_LED_CTRL_BY_FLASHSTB,
	.leds[1].brightness = 0x1F,

	.leds[2].name = "torch-sec1",
	.leds[2].id = MAX77693_TORCH_LED_1,
	.leds[2].cntrl_mode = MAX77693_LED_CTRL_BY_FLASHSTB,
	.leds[2].brightness = 0x03,

	.leds[3].name = "torch-sec2",
	.leds[3].id = MAX77693_TORCH_LED_2,
	.leds[3].cntrl_mode = MAX77693_LED_CTRL_BY_FLASHSTB,
	.leds[3].brightness = 0x04,
};

#endif

#ifdef CONFIG_BATTERY_MAX77693_CHARGER
static struct max77693_charger_platform_data max77693_charger_pdata = {
#ifdef CONFIG_BATTERY_WPC_CHARGER
	.wpc_irq_gpio = GPIO_WPC_INT,
#if defined(CONFIG_MACH_M0) || defined(CONFIG_MACH_C1) || \
	defined(CONFIG_MACH_M3) || defined(CONFIG_MACH_T0)
	.vbus_irq_gpio = GPIO_V_BUS_INT,
#endif
#if defined(CONFIG_MACH_T0)
	.wc_pwr_det = true,
#else
	.wc_pwr_det = false,
#endif
#endif
};
#endif

extern struct max77693_muic_data max77693_muic;
extern struct max77693_regulator_data max77693_regulators;

static bool is_muic_default_uart_path_cp(void)
{
#if defined(CONFIG_MACH_M0_CTC)
	return false;
#endif
#ifdef CONFIG_MACH_M0
	if (system_rev == 5)
		return true;
#endif
#ifdef CONFIG_MACH_C1
	if (system_rev == 4)
		return true;
#endif
	return false;
}

struct max77693_platform_data exynos4_max77693_info = {
	.irq_base	= IRQ_BOARD_IFIC_START,
	.irq_gpio	= GPIO_IF_PMIC_IRQ,
	.wakeup		= 1,
	.muic = &max77693_muic,
	.is_default_uart_path_cp =  is_muic_default_uart_path_cp,
	.regulators = &max77693_regulators,
	.num_regulators = MAX77693_REG_MAX,
#ifdef CONFIG_VIBETONZ
	.haptic_data = &max77693_haptic_pdata,
#endif
#ifdef CONFIG_LEDS_MAX77693
	.led_data = &max77693_led_pdata,
#endif
#ifdef CONFIG_BATTERY_MAX77693_CHARGER
	.charger_data = &max77693_charger_pdata,
#endif
};
#endif

#if defined(CONFIG_CHARGER_MAX8922_U1)
static int max8922_cfg_gpio(void)
{
	printk(KERN_INFO "[Battery] %s called.\n", __func__);

	s3c_gpio_cfgpin(GPIO_CHG_EN, S3C_GPIO_OUTPUT);
	s3c_gpio_setpull(GPIO_CHG_EN, S3C_GPIO_PULL_NONE);
	gpio_set_value(GPIO_CHG_EN, GPIO_LEVEL_LOW);

	s3c_gpio_cfgpin(GPIO_CHG_ING_N, S3C_GPIO_INPUT);
	s3c_gpio_setpull(GPIO_CHG_ING_N, S3C_GPIO_PULL_NONE);

	s3c_gpio_cfgpin(GPIO_TA_nCONNECTED, S3C_GPIO_INPUT);
	s3c_gpio_setpull(GPIO_TA_nCONNECTED, S3C_GPIO_PULL_NONE);

	return 0;
}

static struct max8922_platform_data max8922_pdata = {
	.cfg_gpio = max8922_cfg_gpio,
	.gpio_chg_en = GPIO_CHG_EN,
	.gpio_chg_ing = GPIO_CHG_ING_N,
	.gpio_ta_nconnected = GPIO_TA_nCONNECTED,
};

static struct platform_device max8922_device_charger = {
	.name = "max8922-charger",
	.id = -1,
	.dev.platform_data = &max8922_pdata,
};
#endif /* CONFIG_CHARGER_MAX8922_U1 */

/* I2C0 */
static struct i2c_board_info i2c_devs0[] __initdata = {
};

/* I2C1 */
static struct i2c_board_info i2c_devs1[] __initdata = {
};

#ifdef CONFIG_S3C_DEV_I2C4
#ifdef CONFIG_MFD_MAX77693
static struct i2c_board_info i2c_devs4_max77693[] __initdata = {
	{
		I2C_BOARD_INFO("max77693", (0xCC >> 1)),
		.platform_data	= &exynos4_max77693_info,
	}
};
#endif
#endif

#ifdef CONFIG_S3C_DEV_I2C5
#ifdef CONFIG_MACH_GC1
static struct i2c_board_info i2c_devs5[] __initdata = {
	/* HDMI */
	{
		I2C_BOARD_INFO("s5p_ddc", (0x74 >> 1)),
	},
};
#elif !defined(CONFIG_MACH_T0_EUR_OPEN) && !defined(CONFIG_MACH_T0_CHN_OPEN)
static struct i2c_board_info i2c_devs5[] __initdata = {
#ifdef CONFIG_REGULATOR_MAX8997
	{
		I2C_BOARD_INFO("max8997", (0xcc >> 1)),
		.platform_data = &exynos4_max8997_info,
	},
#endif

#if defined(CONFIG_REGULATOR_MAX77686)
	/* max77686 on i2c5 other than M1 board */
	{
		I2C_BOARD_INFO("max77686", (0x12 >> 1)),
		.platform_data	= &exynos4_max77686_info,
	},
#endif
};
#endif /* CONFIG_MACH_GC1 */
#ifdef CONFIG_MACH_GC1
static void hdmi_ext_ic_control_gc1(bool ic_on)
{
	if (ic_on)
		gpio_set_value(GPIO_HDMI_EN, GPIO_LEVEL_HIGH);
	else
		gpio_set_value(GPIO_HDMI_EN, GPIO_LEVEL_LOW);
}

static void s3c_i2c5_cfg_gpio_gc1(struct platform_device *dev)
{
	s3c_gpio_cfgall_range(EXYNOS4_GPB(2), 2,
		S3C_GPIO_SFN(3), S3C_GPIO_PULL_NONE);
	s5p_gpio_set_drvstr(EXYNOS4_GPB(2), S5P_GPIO_DRVSTR_LV4);
	s5p_gpio_set_drvstr(EXYNOS4_GPB(3), S5P_GPIO_DRVSTR_LV4);
	s3c_gpio_cfgpin(GPIO_HDMI_EN, S3C_GPIO_OUTPUT);	/* HDMI_EN */
	s3c_gpio_setpull(GPIO_HDMI_EN, S3C_GPIO_PULL_NONE);
}
#endif
struct s3c2410_platform_i2c default_i2c5_data __initdata = {
	.bus_num        = 5,
	.flags          = 0,
	.slave_addr     = 0x10,
#ifdef CONFIG_MACH_GC1
	.frequency      = 50*1000,
	.sda_delay      = 200,
	.cfg_gpio	= s3c_i2c5_cfg_gpio_gc1,
#else
	.frequency      = 100*1000,
	.sda_delay      = 100,
#endif
};

#endif

#ifdef CONFIG_S3C_DEV_I2C6
static struct i2c_board_info i2c_devs6[] __initdata = {
};
#if defined(CONFIG_MACH_T0_EUR_OPEN) || defined(CONFIG_MACH_T0_CHN_OPEN)
static void i2c6_mhl_ddc_cfg_gpio(struct platform_device *dev)
{
	s3c_gpio_cfgall_range(EXYNOS4_GPC1(3), 2,
		S3C_GPIO_SFN(4), S3C_GPIO_PULL_NONE);
}
struct s3c2410_platform_i2c default_i2c6_data __initdata = {
	.bus_num        = 6,
	.flags          = 0,
	.slave_addr     = 0x10,
	.frequency      = 100*1000,
	.sda_delay      = 100,
	.cfg_gpio	= i2c6_mhl_ddc_cfg_gpio,
};
#endif /* CONFIG_MACH_T0_EUR_OPEN */
#endif /* CONFIG_S3C_DEV_I2C6 */

#ifdef CONFIG_MACH_GC1
static struct i2c_board_info i2c_devs7[] __initdata = {
	{
		I2C_BOARD_INFO("max77686", (0x12 >> 1)),
		.platform_data	= &exynos4_max77686_info,
	},
};
static struct i2c_board_info i2c_devs7_s5m[] __initdata = {
	{
		I2C_BOARD_INFO("s5m87xx", 0xCC >> 1),
		.platform_data = &exynos4_s5m8767_info,
	},
};
#else
static struct i2c_board_info i2c_devs7[] __initdata = {
#if defined(CONFIG_MACH_C1) || \
	defined(CONFIG_MACH_M3) || \
	defined(CONFIG_MACH_M0) || defined(CONFIG_MACH_T0) || \
	defined(CONFIG_MACH_BAFFIN)
#if defined(CONFIG_REGULATOR_MAX77686) /* max77686 on i2c7 with M1 board */
	{
		I2C_BOARD_INFO("max77686", (0x12 >> 1)),
		.platform_data	= &exynos4_max77686_info,
	},
#endif

#if defined(CONFIG_REGULATOR_S5M8767)
	{
		I2C_BOARD_INFO("s5m87xx", 0xCC >> 1),
		.platform_data = &exynos4_s5m8767_info,
		.irq	= IRQ_EINT(7),
	},
#endif
#endif
};
#endif /* CONFIG_MACH_GC1 */

/* Bluetooth */
#ifdef CONFIG_BT_BCM4334
static struct platform_device bcm4334_bluetooth_device = {
	.name = "bcm4334_bluetooth",
	.id = -1,
};
#endif

static struct i2c_gpio_platform_data gpio_i2c_data8 = {
	.sda_pin = GPIO_3_TOUCH_SDA,
	.scl_pin = GPIO_3_TOUCH_SCL,
};

struct platform_device s3c_device_i2c8 = {
	.name = "i2c-gpio",
	.id = 8,
	.dev.platform_data = &gpio_i2c_data8,
};

/* I2C8 */
static struct i2c_board_info i2c_devs8_emul[] = {
#ifdef CONFIG_KEYBOARD_CYPRESS_TOUCH
	{
		I2C_BOARD_INFO("sec_touchkey", 0x20),
		.platform_data = &touchkey_pdata,
	},
#endif
};

/* I2C9 */
static struct i2c_board_info i2c_devs9_emul[] __initdata = {
};

/* I2C10 */
static struct i2c_board_info i2c_devs10_emul[] __initdata = {
};

/* I2C11 */
static struct i2c_board_info i2c_devs11_emul[] __initdata = {
};

/* I2C12 */
#if defined(CONFIG_PN65N_NFC) && \
	!defined(CONFIG_MACH_C1) && !defined(CONFIG_MACH_BAFFIN)
static struct i2c_board_info i2c_devs12_emul[] __initdata = {
};
#endif

#ifdef CONFIG_BATTERY_MAX17047_FUELGAUGE
static struct i2c_gpio_platform_data gpio_i2c_data14 = {
	.sda_pin = GPIO_FUEL_SDA,
	.scl_pin = GPIO_FUEL_SCL,
};

struct platform_device s3c_device_i2c14 = {
	.name = "i2c-gpio",
	.id = 14,
	.dev.platform_data = &gpio_i2c_data14,
};

static struct max17047_platform_data max17047_pdata = {
	.irq_gpio = GPIO_FUEL_ALERT,
};

/* I2C14 */
static struct i2c_board_info i2c_devs14_emul[] __initdata = {
	{
		I2C_BOARD_INFO("max17047-fuelgauge", 0x36),
		.platform_data = &max17047_pdata,
	},
};
#endif

#ifdef CONFIG_SAMSUNG_MHL
/* I2C15 */
static struct i2c_gpio_platform_data gpio_i2c_data15 = {
	.sda_pin = GPIO_MHL_SDA_1_8V,
	.scl_pin = GPIO_MHL_SCL_1_8V,
	.udelay = 3,
	.timeout = 0,
};

struct platform_device s3c_device_i2c15 = {
	.name = "i2c-gpio",
	.id = 15,
	.dev = {
		.platform_data = &gpio_i2c_data15,
	}
};

static struct i2c_board_info i2c_devs15_emul[] __initdata = {
};

/* I2C16 */
#if !defined(CONFIG_MACH_T0) && !defined(CONFIG_MACH_M3)
static struct i2c_gpio_platform_data gpio_i2c_data16 = {
	.sda_pin = GPIO_MHL_DSDA_2_8V,
	.scl_pin = GPIO_MHL_DSCL_2_8V,
};

struct platform_device s3c_device_i2c16 = {
	.name = "i2c-gpio",
	.id = 16,
	.dev.platform_data = &gpio_i2c_data16,
};
#endif /* !defined(CONFIG_MACH_T0) */

static struct i2c_board_info i2c_devs16_emul[] __initdata = {
};
#endif

#if defined(CONFIG_MACH_C1) || \
	defined(CONFIG_MACH_M3) || \
	defined(CONFIG_MACH_M0) || \
	defined(CONFIG_MACH_GC1) || defined(CONFIG_MACH_T0) || \
	defined(CONFIG_MACH_BAFFIN)
static struct i2c_gpio_platform_data gpio_i2c_data17 = {
	.sda_pin = GPIO_IF_PMIC_SDA,
	.scl_pin = GPIO_IF_PMIC_SCL,
};

struct platform_device s3c_device_i2c17 = {
	.name = "i2c-gpio",
	.id = 17,
	.dev.platform_data = &gpio_i2c_data17,
};

/* I2C17 */
static struct i2c_board_info i2c_devs17_emul[] __initdata = {
#ifdef CONFIG_MFD_MAX77693
	{
		I2C_BOARD_INFO("max77693", (0xCC >> 1)),
		.platform_data	= &exynos4_max77693_info,
	}
#endif
};
#endif

#if 0
#if defined(CONFIG_MACH_C1) || \
	defined(CONFIG_MACH_M3) || \
	defined(CONFIG_MACH_M0)
static struct i2c_gpio_platform_data i2c18_platdata = {
	.sda_pin		= GPIO_8M_CAM_SDA_18V,
	.scl_pin		= GPIO_8M_CAM_SCL_18V,
	.udelay			= 2, /* 250 kHz */
	.sda_is_open_drain	= 0,
	.scl_is_open_drain	= 0,
	.scl_is_output_only = 0,
};

static struct platform_device s3c_device_i2c18 = {
	.name	= "i2c-gpio",
	.id	= 18,
	.dev.platform_data	= &i2c18_platdata,
};

/* I2C18 */
/* No explicit i2c client array here. The channel number 18 is passed
   to camera driver from midas-camera.c instead. */
#endif
#endif

/* I2C21 */
#if defined(CONFIG_LEDS_AN30259A) || defined(CONFIG_LEDS_LP5521)
static struct i2c_gpio_platform_data gpio_i2c_data21 = {
	.scl_pin = GPIO_S_LED_I2C_SCL,
	.sda_pin = GPIO_S_LED_I2C_SDA,
};

struct platform_device s3c_device_i2c21 = {
	.name = "i2c-gpio",
	.id = 21,
	.dev.platform_data = &gpio_i2c_data21,
};
#endif

/* I2C21 */
#ifdef CONFIG_LEDS_AN30259A
static struct i2c_board_info i2c_devs21_emul[] __initdata = {
	{
		I2C_BOARD_INFO("an30259a", 0x30),
	},
};
#endif

/* I2C22 */
#if defined (CONFIG_BARCODE_EMUL_ICE4)
static struct i2c_gpio_platform_data gpio_i2c_data22 = {
	.scl_pin = GPIO_BARCODE_SCL_1_8V,
	.sda_pin = GPIO_BARCODE_SDA_1_8V,
};

struct platform_device s3c_device_i2c22 = {
	.name = "i2c-gpio",
	.id = 22,
	.dev.platform_data = &gpio_i2c_data22,
};

static struct i2c_board_info i2c_devs22_emul[] __initdata = {
	{
		I2C_BOARD_INFO("ice4", (0x6c)),
	},
};
#endif

#if defined(CONFIG_FELICA)

#define  FELICA_GPIO_RFS_NAME     "FeliCa-RFS"
#define  FELICA_GPIO_PON_NAME     "FeliCa-PON"
#define  FELICA_GPIO_INT_NAME     "FeliCa-INT"
#define  FELICA_GPIO_I2C_SDA_NAME "FeliCa-SDA"
#define  FELICA_GPIO_I2C_SCL_NAME "FeliCa-SCL"

static struct  i2c_gpio_platform_data  i2c30_gpio_platdata = {
	.sda_pin = FELICA_GPIO_I2C_SDA,
	.scl_pin = FELICA_GPIO_I2C_SCL,
	.udelay  = 0,
	.sda_is_open_drain = 0,
	.scl_is_open_drain = 0,
	.scl_is_output_only = 0
};

static struct  platform_device  s3c_device_i2c30 = {
	.name  = "i2c-gpio",
	.id   = 30,                               /* adepter number */
	.dev.platform_data = &i2c30_gpio_platdata,
};

static struct i2c_board_info i2c_devs30[] __initdata = {
	{
		I2C_BOARD_INFO("felica_i2c", (0x56 >> 1)),
	},
};

#endif /* CONFIG_FELICA */

#ifdef CONFIG_ANDROID_RAM_CONSOLE
static struct resource ram_console_resource[] = {
	{
		.flags = IORESOURCE_MEM,
	}
};

static struct platform_device ram_console_device = {
	.name = "ram_console",
	.id = -1,
	.num_resources = ARRAY_SIZE(ram_console_resource),
	.resource = ram_console_resource,
};

static int __init setup_ram_console_mem(char *str)
{
	unsigned size = memparse(str, &str);

	if (size && (*str == '@')) {
		unsigned long long base = 0;

		base = simple_strtoul(++str, &str, 0);
		if (reserve_bootmem(base, size, BOOTMEM_EXCLUSIVE)) {
			pr_err("%s: failed reserving size %d "
			       "at base 0x%llx\n", __func__, size, base);
			return -1;
		}

		ram_console_resource[0].start = base;
		ram_console_resource[0].end = base + size - 1;
		pr_err("%s: %x at %llx\n", __func__, size, base);
	}
	return 0;
}

__setup("ram_console=", setup_ram_console_mem);
#endif

#if defined(CONFIG_BATTERY_SAMSUNG)
static struct samsung_battery_platform_data samsung_battery_pdata = {
	.charger_name	= "max77693-charger",
	.fuelgauge_name	= "max17047-fuelgauge",
#if defined(CONFIG_CHARGER_MAX8922_U1)
	.sub_charger_name   = "max8922-charger",
#endif
#if defined(CONFIG_MACH_GC1)
	.voltage_max = 4200000,
#else
	.voltage_max = 4350000,
#endif
	.voltage_min = 3400000,

#if defined(CONFIG_MACH_GC1)
#if defined(CONFIG_MACH_GC1_USA_ATT)
	.in_curr_limit = 700,
	.chg_curr_ta = 700,
	.chg_curr_dock = 700,
#else
	.in_curr_limit = 1000,
	.chg_curr_ta = 1000,
	.chg_curr_dock = 1000,
#endif
	.chg_curr_siop_lv1 = 475,
	.chg_curr_siop_lv2 = 475,
	.chg_curr_siop_lv3 = 475,
#elif defined(CONFIG_MACH_T0)
	.in_curr_limit = 1800,
	.chg_curr_ta = 1700,
	.chg_curr_dock = 1700,
	.chg_curr_siop_lv1 = 1000,
	.chg_curr_siop_lv2 = 475,
	.chg_curr_siop_lv3 = 1,	/* zero make charger off */
#elif defined(CONFIG_MACH_BAFFIN_KOR_SKT) || \
	defined(CONFIG_MACH_BAFFIN_KOR_KT) || \
	defined(CONFIG_MACH_BAFFIN_KOR_LGT)
	.in_curr_limit = 1000,
	.chg_curr_ta = 1500,
	.chg_curr_dock = 1000,
	.chg_curr_siop_lv1 = 475,
	.chg_curr_siop_lv2 = 475,
	.chg_curr_siop_lv3 = 475,
#else
	.in_curr_limit = 1000,
	.chg_curr_ta = 1000,
	.chg_curr_dock = 1000,
	.chg_curr_siop_lv1 = 475,
	.chg_curr_siop_lv2 = 475,
	.chg_curr_siop_lv3 = 475,
#endif

	.chg_curr_usb = 1700,
	.chg_curr_cdp = 1000,
#if defined(CONFIG_MACH_T0_USA_VZW)
	.chg_curr_wpc = 1700,
#else
	.chg_curr_wpc = 1700,
#endif
	.chg_curr_etc = 1700,

	.chng_interval = 30,
	.chng_susp_interval = 60,
	.norm_interval = 120,
	.norm_susp_interval = 7200,
	.emer_lv1_interval = 30,
	.emer_lv2_interval = 10,

#if defined(CONFIG_MACH_GC1)
	.recharge_voltage = 4150000,
#else
	/* it will be cacaluated in probe */
	.recharge_voltage = 4300000,
#endif

#if defined(CONFIG_TARGET_LOCALE_KOR) || defined(CONFIG_MACH_M0_CTC) || \
	defined(CONFIG_MACH_T0_USA_VZW) || defined(CONFIG_MACH_T0_USA_SPR) || \
	defined(CONFIG_MACH_T0_USA_USCC) || defined(CONFIG_MACH_T0_CHN_CTC)
#if defined(CONFIG_MACH_GC1)
	/* GC1-KOR - 1650mAh Battery : ABS Timer Spec(6hr / 2hr) */
	.abstimer_charge_duration = 6 * 60 * 60,
	.abstimer_charge_duration_wpc = 8 * 60 * 60,
	.abstimer_recharge_duration = 2 * 60 * 60,
#else
	.abstimer_charge_duration = 8 * 60 * 60,
	.abstimer_charge_duration_wpc = 8 * 60 * 60,
	.abstimer_recharge_duration = 2 * 60 * 60,
#endif
#else
	.abstimer_charge_duration = 6 * 60 * 60,
	.abstimer_charge_duration_wpc = 8 * 60 * 60,
	.abstimer_recharge_duration = 1.5 * 60 * 60,
#endif

	.cb_det_src = CABLE_DET_CHARGER,
#if defined(CONFIG_TARGET_LOCALE_KOR)
#if defined(CONFIG_MACH_C1_KOR_SKT) || defined(CONFIG_MACH_C1_KOR_KT)
	.overheat_stop_temp = 640,
	.overheat_recovery_temp = 429,
	.freeze_stop_temp = -70,
	.freeze_recovery_temp = 8,
#elif defined(CONFIG_MACH_C1_KOR_LGT)
	.overheat_stop_temp = 630,
	.overheat_recovery_temp = 430,
	.freeze_stop_temp = -70,
	.freeze_recovery_temp = 0,
#elif defined(CONFIG_MACH_M0_KOR_SKT) || defined(CONFIG_MACH_M0_KOR_KT)
	.overheat_stop_temp = 710,
	.overheat_recovery_temp = 430,
	.freeze_stop_temp = -40,
	.freeze_recovery_temp = 30,
#elif defined(CONFIG_MACH_T0_KOR_SKT) || defined(CONFIG_MACH_T0_KOR_KT) || \
	defined(CONFIG_MACH_T0_KOR_LGT)
	.overheat_stop_temp = 660,
	.overheat_recovery_temp = 425,
	.freeze_stop_temp = -45,
	.freeze_recovery_temp = 3,
#elif defined(CONFIG_MACH_BAFFIN_KOR_SKT) || \
	defined(CONFIG_MACH_BAFFIN_KOR_KT)
	.overheat_stop_temp = 620,
	.overheat_recovery_temp = 445,
	.freeze_stop_temp = -40,
	.freeze_recovery_temp = 8,
#elif defined(CONFIG_MACH_BAFFIN_KOR_LGT)
	.overheat_stop_temp = 620,
	.overheat_recovery_temp = 445,
	.freeze_stop_temp = -50,
	.freeze_recovery_temp = -10,
#elif defined(CONFIG_MACH_SUPERIOR_KOR_SKT)
	.overheat_stop_temp = 620,
	.overheat_recovery_temp = 420,
	.freeze_stop_temp = -60,
	.freeze_recovery_temp = -10,
#elif defined(CONFIG_MACH_GC1)
	.overheat_stop_temp = 620,
	.overheat_recovery_temp = 440,
	.freeze_stop_temp = -50,
	.freeze_recovery_temp = 0,
#else
	.overheat_stop_temp = 600,
	.overheat_recovery_temp = 430,
	.freeze_stop_temp = -50,
	.freeze_recovery_temp = 0,
#endif /* KOR model */
#elif defined(CONFIG_TARGET_LOCALE_USA)
#if defined(CONFIG_MACH_C1_USA_ATT)
	.overheat_stop_temp = 450,
	.overheat_recovery_temp = 400,
	.freeze_stop_temp = -50,
	.freeze_recovery_temp = 0,
#elif defined(CONFIG_MACH_T0_USA_ATT)
	.overheat_stop_temp = 475,
	.overheat_recovery_temp = 400,
	.freeze_stop_temp = -50,
	.freeze_recovery_temp = 0,
#elif defined(CONFIG_MACH_T0_USA_VZW)
	.overheat_stop_temp = 515,
	.overheat_recovery_temp = 440,
	.freeze_stop_temp = -50,
	.freeze_recovery_temp = 0,
#elif defined(CONFIG_MACH_T0_USA_TMO)
	.overheat_stop_temp = 475,
	.overheat_recovery_temp = 400,
	.freeze_stop_temp = -50,
	.freeze_recovery_temp = 0,
#elif defined(CONFIG_MACH_T0_USA_SPR)
	.overheat_stop_temp = 515,
	.overheat_recovery_temp = 420,
	.freeze_stop_temp = -80,
	.freeze_recovery_temp = -10,
#elif defined(CONFIG_MACH_T0_USA_USCC)
	.overheat_stop_temp = 630,
	.overheat_recovery_temp = 420,
	.freeze_stop_temp = -50,
	.freeze_recovery_temp = 30,
#else
	/* USA default */
	.overheat_stop_temp = 450,
	.overheat_recovery_temp = 400,
	.freeze_stop_temp = -50,
	.freeze_recovery_temp = 0,
#endif
#elif defined(CONFIG_MACH_T0_CHN_CTC)
	.overheat_stop_temp = 642,
	.overheat_recovery_temp = 450,
	.freeze_stop_temp = -40,
	.freeze_recovery_temp = 20,
#elif defined(CONFIG_MACH_M0_CTC)
#if defined(CONFIG_MACH_M0_DUOSCTC)
	.overheat_stop_temp = 660,
	.overheat_recovery_temp = 440,
	.freeze_stop_temp = -40,
	.freeze_recovery_temp = 20,
#else
	.overheat_stop_temp = 640,
	.overheat_recovery_temp = 430,
	.freeze_stop_temp = -50,
	.freeze_recovery_temp = 30,
#endif
#elif defined(CONFIG_MACH_GC1)
	.overheat_stop_temp = 600,
	.overheat_recovery_temp = 400,
	.freeze_stop_temp = -50,
	.freeze_recovery_temp = 0,
#else
	/* M0 EUR */
	.overheat_stop_temp = 600,
	.overheat_recovery_temp = 400,
	.freeze_stop_temp = -50,
	.freeze_recovery_temp = 0,
#endif

	/* CTIA spec */
#if defined(CONFIG_TARGET_LOCALE_USA) && !defined(CONFIG_MACH_T0_USA_USCC)
	.ctia_spec  = true,
#else
	.ctia_spec  = false,
#endif

	/* CTIA temperature spec */
	.event_time = 10 * 60,
#if defined(CONFIG_MACH_C1_USA_ATT)
	.event_overheat_stop_temp = 600,
	.event_overheat_recovery_temp = 400,
	.event_freeze_stop_temp = -50,
	.event_freeze_recovery_temp = 0,
	.lpm_overheat_stop_temp = 480,
	.lpm_overheat_recovery_temp = 450,
	.lpm_freeze_stop_temp = -50,
	.lpm_freeze_recovery_temp = 0,
#elif defined(CONFIG_MACH_T0_USA_VZW)
	.event_overheat_stop_temp = 600,
	.event_overheat_recovery_temp = 409,
	.event_freeze_stop_temp = -50,
	.event_freeze_recovery_temp = 0,
	.lpm_overheat_stop_temp = 480,
	.lpm_overheat_recovery_temp = 450,
	.lpm_freeze_stop_temp = -50,
	.lpm_freeze_recovery_temp = 0,
#else
	/* USA default */
	.event_overheat_stop_temp = 600,
	.event_overheat_recovery_temp = 400,
	.event_freeze_stop_temp = -50,
	.event_freeze_recovery_temp = 0,
	.lpm_overheat_stop_temp = 480,
	.lpm_overheat_recovery_temp = 450,
	.lpm_freeze_stop_temp = -50,
	.lpm_freeze_recovery_temp = 0,
#endif

	.temper_src = TEMPER_AP_ADC,
	.temper_ch = 2, /* if src == TEMPER_AP_ADC */
#ifdef CONFIG_S3C_ADC
	/*
	 * s3c adc driver does not convert raw adc data.
	 * so, register convert function.
	 */
	.covert_adc = convert_adc,
#endif

#if (defined(CONFIG_MACH_T0) || defined(CONFIG_MACH_M3_USA_TMO)) && \
	defined(CONFIG_TARGET_LOCALE_USA)
	.vf_det_src = VF_DET_GPIO,	/* check H/W rev in battery probe */
#elif defined(CONFIG_MACH_GC1) && defined(CONFIG_TARGET_LOCALE_USA)
	.vf_det_src = VF_DET_ADC_GPIO,	/* check H/W rev in battery probe */
#else
	.vf_det_src = VF_DET_CHARGER,
#endif
	.vf_det_ch = 0,	/* if src == VF_DET_ADC */
#if defined(CONFIG_MACH_GC1)
	.vf_det_th_l = 310,
	.vf_det_th_h = 490,
#else
	.vf_det_th_l = 100,
	.vf_det_th_h = 1500,
#endif
#if (defined(CONFIG_MACH_T0) || defined(CONFIG_MACH_GC1) || \
	defined(CONFIG_MACH_M3_USA_TMO)) && \
	defined(CONFIG_TARGET_LOCALE_USA)
	.batt_present_gpio = GPIO_BATT_PRESENT_N_INT,
#endif

	.suspend_chging = true,

	.led_indicator = false,

	.battery_standever = false,
};

static struct platform_device samsung_device_battery = {
	.name	= "samsung-battery",
	.id	= -1,
	.dev.platform_data = &samsung_battery_pdata,
};
#endif

#define GPIO_KEYS(_code, _gpio, _active_low, _iswake, _hook)	\
	{							\
		.code = _code,					\
		.gpio = _gpio,					\
		.active_low = _active_low,			\
		.type = EV_KEY,					\
		.wakeup = _iswake,				\
		.debounce_interval = 10,			\
		.isr_hook = _hook,				\
		.value = 1					\
	}

struct gpio_keys_button midas_buttons[] = {
#if defined(CONFIG_MACH_GC1)
	GPIO_KEYS(KEY_POWER, GPIO_nPOWER,
			1, 1, sec_debug_check_crash_key),
	GPIO_KEYS(KEY_CAMERA_FOCUS, GPIO_S1_KEY,
			1, 1, sec_debug_check_crash_key),
	/*KEY_CAMERA_SHUTTER*/
	GPIO_KEYS(0x220, GPIO_S2_KEY,
			1, 1, sec_debug_check_crash_key),
	GPIO_KEYS(KEY_CAMERA_ZOOMIN, GPIO_TELE_KEY,
			1, 1, sec_debug_check_crash_key),
	GPIO_KEYS(KEY_CAMERA_ZOOMOUT, GPIO_WIDE_KEY,
			1, 1, sec_debug_check_crash_key),
	GPIO_KEYS(0x221, GPIO_FAST_TELE_KEY,
			1, 1, sec_debug_check_crash_key),
	GPIO_KEYS(0x222, GPIO_FAST_WIDE_KEY,
			1, 1, sec_debug_check_crash_key),
#if 0
	GPIO_KEYS(KEY_HOMEPAGE, GPIO_OK_KEY_ANDROID,
			1, 1, sec_debug_check_crash_key),
	GPIO_KEYS(KEY_MENU, GPIO_MENU_KEY,
			1, 1, sec_debug_check_crash_key),
	GPIO_KEYS(KEY_BACK, GPIO_BACK_KEY,
			1, 1, sec_debug_check_crash_key),
#endif
#else
	GPIO_KEYS(KEY_VOLUMEUP, GPIO_VOL_UP,
		  1, 0, sec_debug_check_crash_key),
	GPIO_KEYS(KEY_VOLUMEDOWN, GPIO_VOL_DOWN,
		  1, 0, sec_debug_check_crash_key),
	GPIO_KEYS(KEY_POWER, GPIO_nPOWER,
		  1, 1, sec_debug_check_crash_key),
#endif
};

#if !defined(CONFIG_MACH_T0) && !defined(CONFIG_MACH_M3)
struct gpio_keys_button m0_buttons[] = {
	GPIO_KEYS(KEY_VOLUMEUP, GPIO_VOL_UP_00,
		  1, 0, sec_debug_check_crash_key),
	GPIO_KEYS(KEY_VOLUMEDOWN, GPIO_VOL_DOWN_00,
		  1, 0, sec_debug_check_crash_key),
	GPIO_KEYS(KEY_POWER, GPIO_nPOWER,
		  1, 1, sec_debug_check_crash_key),
};
#endif

#if defined(CONFIG_MACH_M0) || \
	defined(CONFIG_MACH_C1_USA_ATT)
struct gpio_keys_button m0_rev11_buttons[] = {
	GPIO_KEYS(KEY_VOLUMEUP, GPIO_VOL_UP_00,
		  1, 0, sec_debug_check_crash_key),
	GPIO_KEYS(KEY_VOLUMEDOWN, GPIO_VOL_DOWN_00,
		  1, 0, sec_debug_check_crash_key),
	GPIO_KEYS(KEY_POWER, GPIO_nPOWER,
		  1, 1, sec_debug_check_crash_key),
	GPIO_KEYS(KEY_HOMEPAGE, GPIO_OK_KEY_ANDROID,
		  1, 1, sec_debug_check_crash_key),
};
#endif

#if defined(CONFIG_TARGET_LOCALE_KOR) && \
	(defined(CONFIG_MACH_M0) || defined(CONFIG_MACH_C1) ||\
	defined(CONFIG_MACH_BAFFIN))
struct gpio_keys_button c1_rev04_buttons[] = {
	GPIO_KEYS(KEY_VOLUMEUP, GPIO_VOL_UP_00,
		  1, 0, sec_debug_check_crash_key),
	GPIO_KEYS(KEY_VOLUMEDOWN, GPIO_VOL_DOWN_00,
		  1, 0, sec_debug_check_crash_key),
	GPIO_KEYS(KEY_POWER, GPIO_nPOWER,
		  1, 1, sec_debug_check_crash_key),
	GPIO_KEYS(KEY_HOMEPAGE, GPIO_OK_KEY_ANDROID,
		  1, 1, sec_debug_check_crash_key),
};
#endif

#if defined(CONFIG_MACH_T0) || defined(CONFIG_MACH_M3)
struct gpio_keys_button t0_buttons[] = {
	GPIO_KEYS(KEY_VOLUMEUP, GPIO_VOL_UP,
		  1, 0, sec_debug_check_crash_key),
	GPIO_KEYS(KEY_VOLUMEDOWN, GPIO_VOL_DOWN,
		  1, 0, sec_debug_check_crash_key),
	GPIO_KEYS(KEY_POWER, GPIO_nPOWER,
		  1, 1, sec_debug_check_crash_key),
	GPIO_KEYS(KEY_HOMEPAGE, GPIO_OK_KEY,
		  1, 1, sec_debug_check_crash_key),
};
#endif

#if defined(CONFIG_MACH_M3_USA_TMO)
struct gpio_keys_button m3_uas_tmo_00_buttons[] = {
	GPIO_KEYS(KEY_VOLUMEUP, GPIO_VOL_UP_00,
		  1, 0, sec_debug_check_crash_key),
	GPIO_KEYS(KEY_VOLUMEDOWN, GPIO_VOL_DOWN_00,
		  1, 0, sec_debug_check_crash_key),
	GPIO_KEYS(KEY_POWER, GPIO_nPOWER,
		  1, 1, sec_debug_check_crash_key),
	GPIO_KEYS(KEY_HOMEPAGE, GPIO_OK_KEY,
		  1, 1, sec_debug_check_crash_key),
};
#endif

struct gpio_keys_platform_data midas_gpiokeys_platform_data = {
	midas_buttons,
	ARRAY_SIZE(midas_buttons),
#ifdef CONFIG_MACH_GC1
	.gpio_strobe_insert = STR_PU_DET_18V,
#endif
};

static struct platform_device midas_keypad = {
	.name	= "gpio-keys",
	.dev	= {
		.platform_data = &midas_gpiokeys_platform_data,
	},
};



#ifdef CONFIG_VIDEO_FIMG2D
static struct fimg2d_platdata fimg2d_data __initdata = {
	.hw_ver = 0x41,
	.parent_clkname = "mout_g2d0",
	.clkname = "sclk_fimg2d",
	.gate_clkname = "fimg2d",
	.clkrate = 199 * 1000000,	/* 160 Mhz */
};
#endif

#ifdef CONFIG_EXYNOS_C2C
struct exynos_c2c_platdata smdk4212_c2c_pdata = {
	.setup_gpio	= NULL,
	.shdmem_addr	= C2C_SHAREDMEM_BASE,
	.shdmem_size	= C2C_MEMSIZE_64,
	.ap_sscm_addr	= NULL,
	.cp_sscm_addr	= NULL,
	.rx_width	= C2C_BUSWIDTH_16,
	.tx_width	= C2C_BUSWIDTH_16,
	.clk_opp100	= 400,
	.clk_opp50	= 266,
	.clk_opp25	= 0,
	.default_opp_mode	= C2C_OPP50,
	.get_c2c_state	= NULL,
};
#endif
/* BUSFREQ to control memory/bus */
static struct device_domain busfreq;

static struct platform_device exynos4_busfreq = {
	.id = -1,
	.name = "exynos-busfreq",
};

static struct i2c_gpio_platform_data i2c9_platdata = {
#if defined(CONFIG_SENSORS_CM3663)
	.sda_pin	= GPIO_PS_ALS_SDA_18V,
	.scl_pin	= GPIO_PS_ALS_SCL_18V,
#elif defined(CONFIG_SENSORS_BH1721)
	.sda_pin	= GPIO_PS_ALS_SDA_28V,
	.scl_pin	= GPIO_PS_ALS_SCL_28V,
#elif defined(CONFIG_SENSORS_CM36651)
	.sda_pin	= GPIO_RGB_SDA_1_8V,
	.scl_pin	= GPIO_RGB_SCL_1_8V,
#elif defined(CONFIG_SENSORS_GP2A)
	.sda_pin	= GPIO_PS_ALS_SDA_28V,
	.scl_pin	= GPIO_PS_ALS_SCL_28V,
#endif
	.udelay	= 2, /* 250KHz */
	.sda_is_open_drain	= 0,
	.scl_is_open_drain	= 0,
	.scl_is_output_only = 0,
};

static struct platform_device s3c_device_i2c9 = {
	.name	= "i2c-gpio",
	.id	= 9,
	.dev.platform_data	= &i2c9_platdata,
};

#ifdef CONFIG_SENSORS_AK8975C
static struct i2c_gpio_platform_data i2c10_platdata = {
	.sda_pin	= GPIO_MSENSOR_SDA_18V,
	.scl_pin	= GPIO_MSENSOR_SCL_18V,
	.udelay	= 2, /* 250KHz */
	.sda_is_open_drain	= 0,
	.scl_is_open_drain	= 0,
	.scl_is_output_only = 0,
};

static struct platform_device s3c_device_i2c10 = {
	.name	= "i2c-gpio",
	.id	= 10,
	.dev.platform_data	= &i2c10_platdata,
};
#endif

#ifdef CONFIG_SENSORS_AK8963C
static struct i2c_gpio_platform_data i2c10_platdata = {
	.sda_pin	= GPIO_MSENSOR_SDA_18V,
	.scl_pin	= GPIO_MSENSOR_SCL_18V,
	.udelay	= 2, /* 250KHz */
	.sda_is_open_drain	= 0,
	.scl_is_open_drain	= 0,
	.scl_is_output_only = 0,
};

static struct platform_device s3c_device_i2c10 = {
	.name	= "i2c-gpio",
	.id	= 10,
	.dev.platform_data	= &i2c10_platdata,
};
#endif

#ifdef CONFIG_SENSORS_AK8963
static struct i2c_gpio_platform_data i2c10_platdata = {
	.sda_pin	= GPIO_MSENSOR_SDA_18V,
	.scl_pin	= GPIO_MSENSOR_SCL_18V,
	.udelay	= 2, /* 250KHz */
	.sda_is_open_drain	= 0,
	.scl_is_open_drain	= 0,
	.scl_is_output_only = 0,
};

static struct platform_device s3c_device_i2c10 = {
	.name	= "i2c-gpio",
	.id	= 10,
	.dev.platform_data	= &i2c10_platdata,
};
#endif

#ifdef CONFIG_SENSORS_LPS331
static struct i2c_gpio_platform_data i2c11_platdata = {
	.sda_pin	= GPIO_BSENSE_SDA_18V,
	.scl_pin	= GPIO_BENSE_SCL_18V,
	.udelay	= 2, /* 250KHz */
	.sda_is_open_drain	= 0,
	.scl_is_open_drain	= 0,
	.scl_is_output_only = 0,
};

static struct platform_device s3c_device_i2c11 = {
	.name			= "i2c-gpio",
	.id	= 11,
	.dev.platform_data	= &i2c11_platdata,
};
#endif

#if defined(CONFIG_PN65N_NFC) && \
	!defined(CONFIG_MACH_C1) && !defined(CONFIG_MACH_BAFFIN)
static struct i2c_gpio_platform_data i2c12_platdata = {
	.sda_pin		= GPIO_NFC_SDA_18V,
	.scl_pin		= GPIO_NFC_SCL_18V,
	.udelay			= 2, /* 250 kHz */
	.sda_is_open_drain	= 0,
	.scl_is_open_drain	= 0,
	.scl_is_output_only = 0,
};

static struct platform_device s3c_device_i2c12 = {
	.name	= "i2c-gpio",
	.id		= 12,
	.dev.platform_data	= &i2c12_platdata,
};
#endif

#ifdef CONFIG_USB_HOST_NOTIFY
static void otg_accessory_power(int enable)
{
	u8 on = (u8)!!enable;
	int err;

	/* max77693 otg power control */
	otg_control(enable);
#if !defined(CONFIG_MACH_M3_USA_TMO)
	err = gpio_request(GPIO_OTG_EN, "USB_OTG_EN");
	if (err)
		printk(KERN_ERR "failed to request USB_OTG_EN\n");
	gpio_direction_output(GPIO_OTG_EN, on);
	gpio_free(GPIO_OTG_EN);
#endif
	pr_info("%s: otg accessory power = %d\n", __func__, on);
}

static void otg_accessory_powered_booster(int enable)
{
	u8 on = (u8)!!enable;

	/* max77693 powered otg power control */
	powered_otg_control(enable);
	pr_info("%s: otg accessory power = %d\n", __func__, on);
}

static struct host_notifier_platform_data host_notifier_pdata = {
	.ndev.name	= "usb_otg",
	.booster	= otg_accessory_power,
	.powered_booster = otg_accessory_powered_booster,
	.thread_enable	= 0,
};

struct platform_device host_notifier_device = {
	.name = "host_notifier",
	.dev.platform_data = &host_notifier_pdata,
};
#endif

#ifdef CONFIG_SEC_WATCHDOG_RESET
static struct platform_device watchdog_reset_device = {
	.name = "watchdog-reset",
	.id = -1,
};
#endif

#ifdef CONFIG_CORESIGHT_ETM

#define CORESIGHT_PHYS_BASE		0x10880000
#define CORESIGHT_ETB_PHYS_BASE		(CORESIGHT_PHYS_BASE + 0x1000)
#define CORESIGHT_TPIU_PHYS_BASE	(CORESIGHT_PHYS_BASE + 0x3000)
#define CORESIGHT_FUNNEL_PHYS_BASE	(CORESIGHT_PHYS_BASE + 0x4000)
#define CORESIGHT_ETM_PHYS_BASE		(CORESIGHT_PHYS_BASE + 0x1C000)

static struct resource coresight_etb_resources[] = {
	{
		.start = CORESIGHT_ETB_PHYS_BASE,
		.end   = CORESIGHT_ETB_PHYS_BASE + SZ_4K - 1,
		.flags = IORESOURCE_MEM,
	},
};

struct platform_device coresight_etb_device = {
	.name          = "coresight_etb",
	.id            = -1,
	.num_resources = ARRAY_SIZE(coresight_etb_resources),
	.resource      = coresight_etb_resources,
};

static struct resource coresight_tpiu_resources[] = {
	{
		.start = CORESIGHT_TPIU_PHYS_BASE,
		.end   = CORESIGHT_TPIU_PHYS_BASE + SZ_4K - 1,
		.flags = IORESOURCE_MEM,
	},
};

struct platform_device coresight_tpiu_device = {
	.name          = "coresight_tpiu",
	.id            = -1,
	.num_resources = ARRAY_SIZE(coresight_tpiu_resources),
	.resource      = coresight_tpiu_resources,
};

static struct resource coresight_funnel_resources[] = {
	{
		.start = CORESIGHT_FUNNEL_PHYS_BASE,
		.end   = CORESIGHT_FUNNEL_PHYS_BASE + SZ_4K - 1,
		.flags = IORESOURCE_MEM,
	},
};

struct platform_device coresight_funnel_device = {
	.name          = "coresight_funnel",
	.id            = -1,
	.num_resources = ARRAY_SIZE(coresight_funnel_resources),
	.resource      = coresight_funnel_resources,
};

static struct resource coresight_etm_resources[] = {
	{
		.start = CORESIGHT_ETM_PHYS_BASE,
		.end   = CORESIGHT_ETM_PHYS_BASE + (SZ_4K * 4) - 1,
		.flags = IORESOURCE_MEM,
	},
};

struct platform_device coresight_etm_device = {
	.name          = "coresight_etm",
	.id            = -1,
	.num_resources = ARRAY_SIZE(coresight_etm_resources),
	.resource      = coresight_etm_resources,
};

#endif

static struct platform_device *midas_devices[] __initdata = {
#ifdef CONFIG_SEC_WATCHDOG_RESET
	&watchdog_reset_device,
#endif
#ifdef CONFIG_ANDROID_RAM_CONSOLE
	&ram_console_device,
#endif
	/* Samsung Power Domain */
	&exynos4_device_pd[PD_MFC],
	&exynos4_device_pd[PD_G3D],
	&exynos4_device_pd[PD_LCD0],
	&exynos4_device_pd[PD_CAM],
	&exynos4_device_pd[PD_TV],
	&exynos4_device_pd[PD_GPS],
#ifdef CONFIG_VIDEO_EXYNOS_FIMC_IS
	&exynos4_device_pd[PD_ISP],
#endif
	&exynos4_device_pd[PD_GPS_ALIVE],
	/* legacy fimd */
#ifdef CONFIG_FB_S5P
	&s3c_device_fb,
#ifdef CONFIG_FB_S5P_LMS501KF03
	&s3c_device_spi_gpio,
#endif
#endif

#ifdef CONFIG_FB_S5P_MDNIE
	&mdnie_device,
#endif

#ifdef CONFIG_HAVE_PWM
	&s3c_device_timer[0],
	&s3c_device_timer[1],
	&s3c_device_timer[2],
	&s3c_device_timer[3],
#endif

#ifdef CONFIG_SND_SOC_WM8994
	&vbatt_device,
#endif

	&s3c_device_wdt,
	&s3c_device_rtc,

	&s3c_device_i2c0,
	&s3c_device_i2c1,
#ifdef CONFIG_S3C_DEV_I2C2
	&s3c_device_i2c2,
#endif
	&s3c_device_i2c3,
#ifdef CONFIG_S3C_DEV_I2C4
	&s3c_device_i2c4,
#endif
	/* &s3c_device_i2c5, */

#if defined(CONFIG_AUDIENCE_ES305) || defined(CONFIG_MACH_T0_EUR_OPEN) || defined(CONFIG_MACH_T0_CHN_OPEN)
	&s3c_device_i2c6,
#endif
	&s3c_device_i2c7,
	&s3c_device_i2c8,
	&s3c_device_i2c9,
#ifdef CONFIG_SENSORS_AK8975C
	&s3c_device_i2c10,
#endif
#ifdef CONFIG_SENSORS_AK8963C
	&s3c_device_i2c10,
#endif
#ifdef CONFIG_SENSORS_AK8963
	&s3c_device_i2c10,
#endif

#ifdef CONFIG_SENSORS_LPS331
	&s3c_device_i2c11,
#endif
	/* &s3c_device_i2c12, */
#ifdef CONFIG_BATTERY_MAX17047_FUELGAUGE
	&s3c_device_i2c14,	/* max17047-fuelgauge */
#endif

#ifdef CONFIG_SAMSUNG_MHL
	&s3c_device_i2c15,
#if !defined(CONFIG_MACH_T0) && !defined(CONFIG_MACH_M3)
	&s3c_device_i2c16,
#endif
#endif /* CONFIG_SAMSUNG_MHL */
#if defined(CONFIG_MACH_C1) || \
	defined(CONFIG_MACH_M3) || \
	defined(CONFIG_MACH_M0) || \
	defined(CONFIG_MACH_GC1) || defined(CONFIG_MACH_T0) || \
	defined(CONFIG_MACH_BAFFIN)
	&s3c_device_i2c17,
#if 0
	&s3c_device_i2c18,
#endif
#endif
#if defined(CONFIG_LEDS_AN30259A) || defined(CONFIG_LEDS_LP5521)
	&s3c_device_i2c21,
#endif
#if defined (CONFIG_BARCODE_EMUL_ICE4)
	&s3c_device_i2c22,
#endif
#if defined(CONFIG_FELICA)
	&s3c_device_i2c30,
#endif

#if defined CONFIG_USB_EHCI_S5P
#if defined(CONFIG_LINK_DEVICE_HSIC) || defined(CONFIG_LINK_DEVICE_USB)
#else
	&s5p_device_ehci,
#endif
#endif

#if defined CONFIG_USB_OHCI_S5P
#if defined(CONFIG_LINK_DEVICE_HSIC) || defined(CONFIG_LINK_DEVICE_USB)
#else
	&s5p_device_ohci,
#endif
#endif

#ifdef CONFIG_USB_GADGET
	&s3c_device_usbgadget,
#endif
#ifdef CONFIG_USB_ANDROID_RNDIS
	&s3c_device_rndis,
#endif
#if defined(CONFIG_USB_ANDROID) || defined(CONFIG_USB_G_ANDROID)
	&s3c_device_android_usb,
	&s3c_device_usb_mass_storage,
#endif
#ifdef CONFIG_EXYNOS4_DEV_MSHC
	&s3c_device_mshci,
#endif
#ifdef CONFIG_S3C_DEV_HSMMC
	&s3c_device_hsmmc0,
#endif
#ifdef CONFIG_S3C_DEV_HSMMC1
	&s3c_device_hsmmc1,
#endif
#ifdef CONFIG_S3C_DEV_HSMMC2
	&s3c_device_hsmmc2,
#endif
#ifdef CONFIG_S3C_DEV_HSMMC3
	&s3c_device_hsmmc3,
#endif

#ifdef CONFIG_SND_SAMSUNG_AC97
	&exynos_device_ac97,
#endif
#ifdef CONFIG_SND_SAMSUNG_I2S
	&exynos_device_i2s0,
#endif
#ifdef CONFIG_SND_SAMSUNG_PCM
	&exynos_device_pcm0,
#endif
#ifdef CONFIG_SND_SAMSUNG_SPDIF
	&exynos_device_spdif,
#endif
#if defined(CONFIG_SND_SAMSUNG_RP) || defined(CONFIG_SND_SAMSUNG_ALP)
	&exynos_device_srp,
#endif
#ifdef CONFIG_VIDEO_EXYNOS_FIMC_IS
	&exynos4_device_fimc_is,
#endif
#ifdef CONFIG_FB_S5P_LD9040
	&ld9040_spi_gpio,
#endif
#ifdef CONFIG_VIDEO_TVOUT
	&s5p_device_tvout,
	&s5p_device_cec,
	&s5p_device_hpd,
#endif
#ifdef CONFIG_FB_S5P_EXTDSP
	&s3c_device_extdsp,
#endif
#ifdef CONFIG_VIDEO_FIMC
	&s3c_device_fimc0,
	&s3c_device_fimc1,
	&s3c_device_fimc2,
	&s3c_device_fimc3,
/* CONFIG_VIDEO_SAMSUNG_S5P_FIMC is the feature for mainline */
#elif defined(CONFIG_VIDEO_SAMSUNG_S5P_FIMC)
	&s5p_device_fimc0,
	&s5p_device_fimc1,
	&s5p_device_fimc2,
	&s5p_device_fimc3,
#endif
#if defined(CONFIG_VIDEO_FIMC_MIPI)
	&s3c_device_csis0,
	&s3c_device_csis1,
#endif
#if defined(CONFIG_VIDEO_MFC5X) || defined(CONFIG_VIDEO_SAMSUNG_S5P_MFC)
	&s5p_device_mfc,
#endif
#ifdef CONFIG_S5P_SYSTEM_MMU
	&SYSMMU_PLATDEV(g2d_acp),
	&SYSMMU_PLATDEV(fimc0),
	&SYSMMU_PLATDEV(fimc1),
	&SYSMMU_PLATDEV(fimc2),
	&SYSMMU_PLATDEV(fimc3),
	&SYSMMU_PLATDEV(jpeg),
#ifdef CONFIG_FB_S5P_SYSMMU
	&SYSMMU_PLATDEV(fimd0),
#endif
	&SYSMMU_PLATDEV(mfc_l),
	&SYSMMU_PLATDEV(mfc_r),
	&SYSMMU_PLATDEV(tv),
#ifdef CONFIG_VIDEO_EXYNOS_FIMC_IS
	&SYSMMU_PLATDEV(is_isp),
	&SYSMMU_PLATDEV(is_drc),
	&SYSMMU_PLATDEV(is_fd),
	&SYSMMU_PLATDEV(is_cpu),
#endif
#endif
#ifdef CONFIG_ION_EXYNOS
	&exynos_device_ion,
#endif
#ifdef CONFIG_VIDEO_EXYNOS_FIMC_LITE
	&exynos_device_flite0,
	&exynos_device_flite1,
#endif
#ifdef CONFIG_VIDEO_FIMG2D
	&s5p_device_fimg2d,
#endif

#ifdef CONFIG_VIDEO_JPEG_V2X
	&s5p_device_jpeg,
#endif
	&samsung_asoc_dma,
#ifndef CONFIG_SND_SOC_SAMSUNG_USE_DMA_WRAPPER
	&samsung_asoc_idma,
#endif
#if defined(CONFIG_CHARGER_MAX8922_U1)
	&max8922_device_charger,
#endif
#ifdef CONFIG_EXYNOS_C2C
	&exynos_device_c2c,
#endif
#if defined(CONFIG_S3C64XX_DEV_SPI)
#if defined(CONFIG_VIDEO_S5C73M3_SPI)
	&exynos_device_spi1,
#endif
#if defined(CONFIG_LINK_DEVICE_SPI)
	&exynos_device_spi2,
#elif defined(CONFIG_TDMB) || defined(CONFIG_TDMB_MODULE)
	&exynos_device_spi2,
#elif defined(CONFIG_ISDBT)
	&exynos_device_spi2,
#elif defined(CONFIG_LINK_DEVICE_PLD)
	&exynos_device_spi2,
#endif
#endif
#ifdef CONFIG_BT_BCM4334
	&bcm4334_bluetooth_device,
#endif
#ifdef CONFIG_S5P_DEV_ACE
	&s5p_device_ace,
#endif
	&exynos4_busfreq,
#ifdef CONFIG_USB_HOST_NOTIFY
	&host_notifier_device,
#endif
#ifdef CONFIG_EXYNOS4_SETUP_THERMAL
	&s5p_device_tmu,
#endif

#ifdef CONFIG_CORESIGHT_ETM
	&coresight_etb_device,
	&coresight_tpiu_device,
	&coresight_funnel_device,
	&coresight_etm_device,
#endif
};

#ifdef CONFIG_EXYNOS4_SETUP_THERMAL
/* below temperature base on the celcius degree */
struct s5p_platform_tmu midas_tmu_data __initdata = {
	.ts = {
		.stop_1st_throttle  = 78,
		.start_1st_throttle = 80,
		.stop_2nd_throttle  = 87,
		.start_2nd_throttle = 103,
		.start_tripping	    = 110, /* temp to do tripping */
		.start_emergency    = 120, /* To protect chip,forcely kernel panic */
		.stop_mem_throttle  = 80,
		.start_mem_throttle = 85,
		.stop_tc  = 13,
		.start_tc = 10,
	},
	.cpufreq = {
		.limit_1st_throttle  = 800000, /* 800MHz in KHz order */
		.limit_2nd_throttle  = 200000, /* 200MHz in KHz order */
	},
	.temp_compensate = {
		.arm_volt = 925000, /* vdd_arm in uV for temperature compensation */
		.bus_volt = 900000, /* vdd_bus in uV for temperature compensation */
		.g3d_volt = 900000, /* vdd_g3d in uV for temperature compensation */
	},
};
#endif

#if defined CONFIG_USB_OHCI_S5P
#if defined(CONFIG_LINK_DEVICE_HSIC) || defined(CONFIG_LINK_DEVICE_USB)
static int __init s5p_ohci_device_initcall(void)
{
	return platform_device_register(&s5p_device_ohci);
}
late_initcall(s5p_ohci_device_initcall);
#endif
#endif

#if defined CONFIG_USB_EHCI_S5P
#if defined(CONFIG_LINK_DEVICE_HSIC) || defined(CONFIG_LINK_DEVICE_USB)
static int __init s5p_ehci_device_initcall(void)
{
	return platform_device_register(&s5p_device_ehci);
}
late_initcall(s5p_ehci_device_initcall);
#endif
#endif

#if defined(CONFIG_VIDEO_TVOUT)
static struct s5p_platform_hpd hdmi_hpd_data __initdata = {
#if defined(CONFIG_MACH_GC1) && defined(CONFIG_HDMI_CONTROLLED_BY_EXT_IC)
	.ext_ic_control = hdmi_ext_ic_control_gc1,
#endif

};
static struct s5p_platform_cec hdmi_cec_data __initdata = {

};

#if defined(CONFIG_MACH_GC1) && defined(CONFIG_HDMI_TX_STRENGTH)
static u8 gc1_hdmi_tx_val[5] = {0x00, 0x1f, 0x00, 0x00, 0x00};
static struct s5p_tx_tuning gc1_tx_tuning = {
	/* tx_ch: bit4 - Pre-emp
	 *	  bit3 - Amp all
	 *	  bit2 - fine amp ch0
	 *	  bit1 - fine amp ch1
	 *	  bit0 - fine amp ch2
	 */
	.tx_ch = 0x08,
	.tx_val = &gc1_hdmi_tx_val[0],
};
static struct s5p_platform_tvout hdmi_tvout_data __initdata = {
	.tx_tune = &gc1_tx_tuning,
};
#endif
#endif

#if defined(CONFIG_CMA)
static unsigned long fbmem_start;
static unsigned long fbmem_size;
static int __init early_fbmem(char *p)
{
	char *endp;

	if (!p)
		return -EINVAL;

	fbmem_size = memparse(p, &endp);
	if (*endp == '@')
		fbmem_start = memparse(endp + 1, &endp);

	return endp > p ? 0 : -EINVAL;
}
early_param("fbmem", early_fbmem);

static void __init exynos4_reserve_mem(void)
{
	static struct cma_region regions[] = {
#ifdef CONFIG_VIDEO_EXYNOS_FIMC_IS
		{
			.name = "fimc_is",
			.size = CONFIG_VIDEO_EXYNOS_MEMSIZE_FIMC_IS * SZ_1K,
			{
				.alignment = 1 << 26,
			},
			.start = 0
		},
#endif
#ifdef CONFIG_VIDEO_SAMSUNG_MEMSIZE_FIMD
		{
			.name = "fimd",
			.size = CONFIG_VIDEO_SAMSUNG_MEMSIZE_FIMD * SZ_1K,
			{
				.alignment = 1 << 20,
			},
			.start = 0
		},
#endif
#ifdef CONFIG_VIDEO_SAMSUNG_MEMSIZE_FIMC0
		{
			.name = "fimc0",
			.size = CONFIG_VIDEO_SAMSUNG_MEMSIZE_FIMC0 * SZ_1K,
			{
				.alignment = 1 << 20,
			},
			.start = 0
		},
#endif
#if !defined(CONFIG_EXYNOS_CONTENT_PATH_PROTECTION) && \
	defined(CONFIG_VIDEO_SAMSUNG_MEMSIZE_MFC0)
		{
			.name = "mfc0",
			.size = CONFIG_VIDEO_SAMSUNG_MEMSIZE_MFC0 * SZ_1K,
			{
				.alignment = 1 << 17,
			},
			.start = 0,
		},
#endif
#if !defined(CONFIG_EXYNOS_CONTENT_PATH_PROTECTION) && \
	defined(CONFIG_ION_EXYNOS_CONTIGHEAP_SIZE)
		{
			.name	= "ion",
			.size	= CONFIG_ION_EXYNOS_CONTIGHEAP_SIZE * SZ_1K,
		},
#endif
#ifdef CONFIG_VIDEO_SAMSUNG_MEMSIZE_MFC
		{
			.name = "mfc",
			.size = CONFIG_VIDEO_SAMSUNG_MEMSIZE_MFC * SZ_1K,
			{
				.alignment = 1 << 17,
			},
			.start = 0
		},
#endif
#if !defined(CONFIG_EXYNOS_CONTENT_PATH_PROTECTION) && \
	defined(CONFIG_VIDEO_SAMSUNG_S5P_MFC)
		{
			.name		= "b2",
			.size		= 32 << 20,
			{ .alignment	= 128 << 10 },
		},
		{
			.name		= "b1",
			.size		= 32 << 20,
			{ .alignment	= 128 << 10 },
		},
		{
			.name		= "fw",
			.size		= 1 << 20,
			{ .alignment	= 128 << 10 },
		},
#endif
#ifdef CONFIG_AUDIO_SAMSUNG_MEMSIZE_SRP
		{
			.name = "srp",
			.size = CONFIG_AUDIO_SAMSUNG_MEMSIZE_SRP * SZ_1K,
			.start = 0,
		},
#endif
#ifdef CONFIG_VIDEO_SAMSUNG_MEMSIZE_FIMG2D
		{
			.name = "fimg2d",
			.size = CONFIG_VIDEO_SAMSUNG_MEMSIZE_FIMG2D * SZ_1K,
			.start = 0
		},
#endif
#ifdef CONFIG_VIDEO_SAMSUNG_MEMSIZE_FIMC1
#ifndef CONFIG_USE_FIMC_CMA
		{
			.name = "fimc1",
			.size = CONFIG_VIDEO_SAMSUNG_MEMSIZE_FIMC1 * SZ_1K,
#if defined(CONFIG_MACH_GC1)
			.start = 0x5ec00000,
#else
			.start = 0x65c00000,
#endif
		},
#endif
#endif
#ifdef CONFIG_VIDEO_SAMSUNG_MEMSIZE_MFC1
		{
			.name = "mfc1",
			.size = CONFIG_VIDEO_SAMSUNG_MEMSIZE_MFC1 * SZ_1K,
			{
				.alignment = 1 << 26,
			},
#if defined(CONFIG_MACH_GC1)
			.start = 0x5e000000,
#else
			.start = 0x64000000,
#endif
		},
#endif
#ifdef CONFIG_VIDEO_SAMSUNG_MEMSIZE_MFC_NORMAL
		{
			.name = "mfc-normal",
			.size = CONFIG_VIDEO_SAMSUNG_MEMSIZE_MFC_NORMAL * SZ_1K,
#if defined(CONFIG_MACH_GC1)
			.start = 0x5e000000,
#else
			.start = 0x64000000,
#endif
		},
#endif
		{
			.size = 0
		},
	};
#ifdef CONFIG_EXYNOS_CONTENT_PATH_PROTECTION
	static struct cma_region regions_secure[] = {
#ifndef CONFIG_DMA_CMA
#ifdef CONFIG_ION_EXYNOS_CONTIGHEAP_SIZE
		{
			.name	= "ion",
			.size	= CONFIG_ION_EXYNOS_CONTIGHEAP_SIZE * SZ_1K,
		},
#endif
#ifdef CONFIG_VIDEO_SAMSUNG_MEMSIZE_MFC_SECURE
		{
			.name = "mfc-secure",
			.size = CONFIG_VIDEO_SAMSUNG_MEMSIZE_MFC_SECURE * SZ_1K,
		},
#endif
		{
			.name = "sectbl",
			.size = SZ_1M,
		},
#else
#if defined(CONFIG_USE_MFC_CMA) && defined(CONFIG_MACH_M0)
#ifdef CONFIG_ION_EXYNOS_CONTIGHEAP_SIZE
		{
			.name = "ion",
			.size = CONFIG_ION_EXYNOS_CONTIGHEAP_SIZE * SZ_1K,
			.start = 0x5F200000,
		},
#endif
#ifdef CONFIG_VIDEO_SAMSUNG_MEMSIZE_MFC_SECURE
		{
			.name = "mfc-secure",
			.size = CONFIG_VIDEO_SAMSUNG_MEMSIZE_MFC_SECURE * SZ_1K,
			.start = 0x5C100000,
		},
#endif
		{
			.name = "sectbl",
			.size = SZ_1M,
			.start = 0x5C000000,
		},
#elif defined(CONFIG_USE_MFC_CMA) && defined(CONFIG_MACH_GC1)
#ifdef CONFIG_ION_EXYNOS_CONTIGHEAP_SIZE
		{
			.name = "ion",
			.size = CONFIG_ION_EXYNOS_CONTIGHEAP_SIZE * SZ_1K,
			.start = 0x53500000,
		},
#endif
#ifdef CONFIG_VIDEO_SAMSUNG_MEMSIZE_MFC_SECURE
		{
			.name = "mfc-secure",
			.size = CONFIG_VIDEO_SAMSUNG_MEMSIZE_MFC_SECURE * SZ_1K,
			.start = 0x50400000,
		},
#endif
		{
			.name = "sectbl",
			.size = SZ_1M,
			.start = 0x50000000,
		},
#else
#ifdef CONFIG_ION_EXYNOS_CONTIGHEAP_SIZE
		{
			.name   = "ion",
			.size   = CONFIG_ION_EXYNOS_CONTIGHEAP_SIZE * SZ_1K,
		},
#endif
#ifdef CONFIG_VIDEO_SAMSUNG_MEMSIZE_MFC_SECURE
		{
			.name = "mfc-secure",
			.size = CONFIG_VIDEO_SAMSUNG_MEMSIZE_MFC_SECURE * SZ_1K,
		},
#endif
		{
			.name = "sectbl",
			.size = SZ_1M,
		},
#endif
#endif
		{
			.size = 0
		},
	};
#else /* !CONFIG_EXYNOS_CONTENT_PATH_PROTECTION */
	struct cma_region *regions_secure = NULL;
#endif

	static const char map[] __initconst =
#ifdef CONFIG_EXYNOS_C2C
		"samsung-c2c=c2c_shdmem;"
#endif
		"s3cfb.0=fimd;exynos4-fb.0=fimd;"
		"s3c-fimc.0=fimc0;s3c-fimc.1=fimc1;s3c-fimc.2=fimc2;s3c-fimc.3=fimc3;"
		"exynos4210-fimc.0=fimc0;exynos4210-fimc.1=fimc1;exynos4210-fimc.2=fimc2;exynos4210-fimc.3=fimc3;"
#ifdef CONFIG_ION_EXYNOS
		"ion-exynos=ion;"
#endif
#ifdef CONFIG_VIDEO_MFC5X
		"s3c-mfc/A=mfc0,mfc-secure;"
		"s3c-mfc/B=mfc1,mfc-normal;"
		"s3c-mfc/AB=mfc;"
#endif
#ifdef CONFIG_VIDEO_SAMSUNG_S5P_MFC
		"s5p-mfc/f=fw;"
		"s5p-mfc/a=b1;"
		"s5p-mfc/b=b2;"
#endif
		"samsung-rp=srp;"
#ifdef CONFIG_VIDEO_EXYNOS_FIMC_IS
		"exynos4-fimc-is=fimc_is;"
#endif
		"s5p-fimg2d=fimg2d;"
#ifdef CONFIG_EXYNOS_CONTENT_PATH_PROTECTION
		"s5p-smem/sectbl=sectbl;"
#endif
		"s5p-smem/mfc=mfc-secure;"
		"s5p-smem/fimc=ion;"
		"s5p-smem/mfc-shm=mfc-normal;"
		"s5p-smem/fimd=fimd;"
		"s5p-smem/fimc0=fimc0";

	int i;

	s5p_cma_region_reserve(regions, regions_secure, 0, map);

	if (!(fbmem_start && fbmem_size))
		return;

	for (i = 0; i < ARRAY_SIZE(regions); i++) {
		if (regions[i].name && !strcmp(regions[i].name, "fimd")) {
			memcpy(phys_to_virt(regions[i].start), phys_to_virt(fbmem_start), fbmem_size * SZ_1K);
			printk(KERN_INFO "Bootloader sent 'fbmem' : %08X\n", (u32)fbmem_start);
			break;
		}
	}
}
#else
static inline void exynos4_reserve_mem(void)
{
}
#endif

#ifdef CONFIG_BACKLIGHT_PWM
/* LCD Backlight data */
static struct samsung_bl_gpio_info smdk4212_bl_gpio_info = {
	.no = EXYNOS4_GPD0(1),
	.func = S3C_GPIO_SFN(2),
};

static struct platform_pwm_backlight_data smdk4212_bl_data = {
	.pwm_id = 1,
#ifdef CONFIG_FB_S5P_LMS501KF03
	.pwm_period_ns = 1000,
#endif
};
#endif

static void __init midas_map_io(void)
{
	clk_xusbxti.rate = 24000000;
	s5p_init_io(NULL, 0, S5P_VA_CHIPID);
	s3c24xx_init_clocks(24000000);
	s3c24xx_init_uarts(smdk4212_uartcfgs, ARRAY_SIZE(smdk4212_uartcfgs));

#if defined(CONFIG_S5P_MEM_CMA)
	exynos4_reserve_mem();
#endif

	/* as soon as INFORM6 is visible, sec_debug is ready to run */
	sec_debug_init();
}

static void __init exynos_sysmmu_init(void)
{
	ASSIGN_SYSMMU_POWERDOMAIN(fimc0, &exynos4_device_pd[PD_CAM].dev);
	ASSIGN_SYSMMU_POWERDOMAIN(fimc1, &exynos4_device_pd[PD_CAM].dev);
	ASSIGN_SYSMMU_POWERDOMAIN(fimc2, &exynos4_device_pd[PD_CAM].dev);
	ASSIGN_SYSMMU_POWERDOMAIN(fimc3, &exynos4_device_pd[PD_CAM].dev);
	ASSIGN_SYSMMU_POWERDOMAIN(jpeg, &exynos4_device_pd[PD_CAM].dev);

#if defined(CONFIG_VIDEO_SAMSUNG_S5P_MFC) || defined(CONFIG_VIDEO_MFC5X)
	ASSIGN_SYSMMU_POWERDOMAIN(mfc_l, &exynos4_device_pd[PD_MFC].dev);
	ASSIGN_SYSMMU_POWERDOMAIN(mfc_r, &exynos4_device_pd[PD_MFC].dev);
#endif
	ASSIGN_SYSMMU_POWERDOMAIN(tv, &exynos4_device_pd[PD_TV].dev);
#ifdef CONFIG_VIDEO_FIMG2D
	sysmmu_set_owner(&SYSMMU_PLATDEV(g2d_acp).dev, &s5p_device_fimg2d.dev);
#endif
#ifdef CONFIG_VIDEO_MFC5X
	sysmmu_set_owner(&SYSMMU_PLATDEV(mfc_l).dev, &s5p_device_mfc.dev);
	sysmmu_set_owner(&SYSMMU_PLATDEV(mfc_r).dev, &s5p_device_mfc.dev);
#endif
#ifdef CONFIG_VIDEO_FIMC
	sysmmu_set_owner(&SYSMMU_PLATDEV(fimc0).dev, &s3c_device_fimc0.dev);
	sysmmu_set_owner(&SYSMMU_PLATDEV(fimc1).dev, &s3c_device_fimc1.dev);
	sysmmu_set_owner(&SYSMMU_PLATDEV(fimc2).dev, &s3c_device_fimc2.dev);
	sysmmu_set_owner(&SYSMMU_PLATDEV(fimc3).dev, &s3c_device_fimc3.dev);
#endif
#ifdef CONFIG_VIDEO_TVOUT
	sysmmu_set_owner(&SYSMMU_PLATDEV(tv).dev, &s5p_device_tvout.dev);
#endif
#ifdef CONFIG_VIDEO_JPEG_V2X
	sysmmu_set_owner(&SYSMMU_PLATDEV(jpeg).dev, &s5p_device_jpeg.dev);
#endif
#ifdef CONFIG_FB_S5P_SYSMMU
	sysmmu_set_owner(&SYSMMU_PLATDEV(fimd0).dev, &s3c_device_fb.dev);
#endif
#ifdef CONFIG_VIDEO_EXYNOS_FIMC_IS
	ASSIGN_SYSMMU_POWERDOMAIN(is_isp, &exynos4_device_pd[PD_ISP].dev);
	ASSIGN_SYSMMU_POWERDOMAIN(is_drc, &exynos4_device_pd[PD_ISP].dev);
	ASSIGN_SYSMMU_POWERDOMAIN(is_fd, &exynos4_device_pd[PD_ISP].dev);
	ASSIGN_SYSMMU_POWERDOMAIN(is_cpu, &exynos4_device_pd[PD_ISP].dev);

	sysmmu_set_owner(&SYSMMU_PLATDEV(is_isp).dev,
		&exynos4_device_fimc_is.dev);
	sysmmu_set_owner(&SYSMMU_PLATDEV(is_drc).dev,
		&exynos4_device_fimc_is.dev);
	sysmmu_set_owner(&SYSMMU_PLATDEV(is_fd).dev,
		&exynos4_device_fimc_is.dev);
	sysmmu_set_owner(&SYSMMU_PLATDEV(is_cpu).dev,
		&exynos4_device_fimc_is.dev);
#endif
}

#ifdef CONFIG_FB_S5P_EXTDSP
struct platform_device s3c_device_extdsp = {
	.name		= "s3cfb_extdsp",
	.id		= 0,
};

static struct s3cfb_extdsp_lcd dummy_buffer = {
	.width = 1920,
	.height = 1080,
	.bpp = 16,
};

static struct s3c_platform_fb default_extdsp_data __initdata = {
	.hw_ver		= 0x70,
	.nr_wins	= 1,
	.default_win	= 0,
	.swap		= FB_SWAP_WORD | FB_SWAP_HWORD,
	.lcd		= &dummy_buffer
};

void __init s3cfb_extdsp_set_platdata(struct s3c_platform_fb *pd)
{
	struct s3c_platform_fb *npd;
	int i;

	if (!pd)
		pd = &default_extdsp_data;

	npd = kmemdup(pd, sizeof(struct s3c_platform_fb), GFP_KERNEL);
	if (!npd)
		printk(KERN_ERR "%s: no memory for platform data\n", __func__);
	else {
		for (i = 0; i < npd->nr_wins; i++)
			npd->nr_buffers[i] = 1;
		s3c_device_extdsp.dev.platform_data = npd;
	}
}
#endif

static inline int need_i2c5(void)
{
#if defined(CONFIG_MACH_C1) || \
	defined(CONFIG_MACH_M0)
	return system_rev != 3;
#else
	return 1;
#endif
}

#if defined(CONFIG_FELICA)
static void felica_setup(void)
{
#if defined(CONFIG_MACH_M3_JPN_DCM)
	if (system_rev < 3) {
		/* I2C SDA GPY2[4] */
		gpio_request(FELICA_GPIO_I2C_SDA_R1, FELICA_GPIO_I2C_SDA_NAME);
		s3c_gpio_setpull(FELICA_GPIO_I2C_SDA_R1, S3C_GPIO_PULL_DOWN);
		gpio_free(FELICA_GPIO_I2C_SDA_R1);

		/* I2C SCL GPY2[5] */
		gpio_request(FELICA_GPIO_I2C_SCL_R1, FELICA_GPIO_I2C_SCL_NAME);
		s3c_gpio_setpull(FELICA_GPIO_I2C_SCL_R1, S3C_GPIO_PULL_DOWN);
		gpio_free(FELICA_GPIO_I2C_SCL_R1);
	} else {
		/* I2C SDA GPY2[4] */
		gpio_request(FELICA_GPIO_I2C_SDA, FELICA_GPIO_I2C_SDA_NAME);
		s3c_gpio_setpull(FELICA_GPIO_I2C_SDA, S3C_GPIO_PULL_DOWN);
		gpio_free(FELICA_GPIO_I2C_SDA);

		/* I2C SCL GPY2[5] */
		gpio_request(FELICA_GPIO_I2C_SCL, FELICA_GPIO_I2C_SCL_NAME);
		s3c_gpio_setpull(FELICA_GPIO_I2C_SCL, S3C_GPIO_PULL_DOWN);
		gpio_free(FELICA_GPIO_I2C_SCL);
	}
#elif defined(CONFIG_MACH_T0_JPN_LTE_DCM)
	/* I2C SDA GPY2[4] */
	gpio_request(FELICA_GPIO_I2C_SDA, FELICA_GPIO_I2C_SDA_NAME);
	s3c_gpio_setpull(FELICA_GPIO_I2C_SDA, S3C_GPIO_PULL_DOWN);
	gpio_free(FELICA_GPIO_I2C_SDA);

	/* I2C SCL GPY2[5] */
	gpio_request(FELICA_GPIO_I2C_SCL, FELICA_GPIO_I2C_SCL_NAME);
	s3c_gpio_setpull(FELICA_GPIO_I2C_SCL, S3C_GPIO_PULL_DOWN);
	gpio_free(FELICA_GPIO_I2C_SCL);
#endif
	/* PON GPL2[7] */
	gpio_request(FELICA_GPIO_PON, FELICA_GPIO_PON_NAME);
	s3c_gpio_setpull(FELICA_GPIO_PON, S3C_GPIO_PULL_DOWN);
	s3c_gpio_cfgpin(FELICA_GPIO_PON, S3C_GPIO_SFN(1)); /* OUTPUT */
	gpio_free(FELICA_GPIO_PON);

	/* RFS GPL2[6] */
	gpio_request(FELICA_GPIO_RFS, FELICA_GPIO_RFS_NAME);
	s3c_gpio_setpull(FELICA_GPIO_RFS, S3C_GPIO_PULL_DOWN);
	gpio_direction_input(FELICA_GPIO_RFS);
	gpio_free(FELICA_GPIO_RFS);

	/* INT GPX1[7] = WAKEUP_INT1[7] */
	gpio_request(FELICA_GPIO_INT, FELICA_GPIO_INT_NAME);
	s3c_gpio_setpull(FELICA_GPIO_INT, S3C_GPIO_PULL_DOWN);
	s5p_register_gpio_interrupt(FELICA_GPIO_INT);
	gpio_direction_input(FELICA_GPIO_INT);
	irq_set_irq_type(gpio_to_irq(FELICA_GPIO_INT), IRQF_TRIGGER_FALLING);
	s3c_gpio_cfgpin(FELICA_GPIO_INT, S3C_GPIO_SFN(0xF)); /* EINT */
	gpio_free(FELICA_GPIO_INT);
}
#endif

static void __init midas_machine_init(void)
{
	struct clk *ppmu_clk = NULL;

#if defined(CONFIG_S3C64XX_DEV_SPI)
#if defined(CONFIG_VIDEO_S5C73M3_SPI)
	unsigned int gpio;
	struct clk *sclk = NULL;
	struct clk *prnt = NULL;
	struct device *spi1_dev = &exynos_device_spi1.dev;
#endif
#if defined(CONFIG_LINK_DEVICE_SPI) \
	|| defined(CONFIG_TDMB) || defined(CONFIG_TDMB_MODULE) \
	|| defined(CONFIG_ISDBT) || defined(CONFIG_LINK_DEVICE_PLD)
	struct device *spi2_dev = &exynos_device_spi2.dev;
#endif
#endif

	/*
	  * prevent 4x12 ISP power off problem
	  * ISP_SYS Register has to be 0 before ISP block power off.
	  */
	__raw_writel(0x0, S5P_CMU_RESET_ISP_SYS);

#if defined(CONFIG_MACH_M3_JPN_DCM)
	if (system_rev < 3) {
		i2c10_platdata.sda_pin = GPIO_MSENSOR_SDA_18V_R1;
		i2c10_platdata.scl_pin = GPIO_MSENSOR_SCL_18V_R1;
#ifdef CONFIG_BATTERY_WPC_CHARGER
		max77693_charger_pdata.vbus_irq_gpio = GPIO_V_BUS_INT_R1;
#endif
		i2c30_gpio_platdata.sda_pin = FELICA_GPIO_I2C_SDA_R1;
		i2c30_gpio_platdata.scl_pin = FELICA_GPIO_I2C_SCL_R1;
	}
#endif

	/* initialise the gpios */
	midas_config_gpio_table();
	exynos4_sleep_gpio_table_set = midas_config_sleep_gpio_table;

	midas_power_init();

	s3c_i2c0_set_platdata(NULL);
	i2c_register_board_info(0, i2c_devs0, ARRAY_SIZE(i2c_devs0));

	s3c_i2c1_set_platdata(NULL);
	i2c_register_board_info(1, i2c_devs1, ARRAY_SIZE(i2c_devs1));

#if defined(CONFIG_S3C_DEV_I2C2) \
	&& !defined(CONFIG_MACH_T0_EUR_OPEN) \
	&& !defined(CONFIG_MACH_T0_CHN_CU_DUOS) \
	&& !defined(CONFIG_MACH_T0_CHN_OPEN_DUOS) \
	&& !defined(CONFIG_MACH_T0_CHN_CMCC) \
	&& !defined(CONFIG_MACH_T0_CHN_OPEN)
	s3c_i2c2_set_platdata(NULL);
#endif

	s3c_i2c3_set_platdata(NULL);
	midas_tsp_init();
#ifndef CONFIG_TOUCHSCREEN_MELFAS_GC
#ifndef CONFIG_TOUCHSCREEN_CYPRESS_TMA46X
	midas_tsp_set_lcdtype(lcdtype);
#endif
#endif

#ifdef CONFIG_LEDS_AAT1290A
	platform_device_register(&s3c_device_aat1290a_led);
#endif

#ifdef CONFIG_S3C_DEV_I2C4
#if defined(CONFIG_MACH_T0) || defined(CONFIG_MACH_M3) \
	|| defined(CONFIG_MACH_BAFFIN)
	s3c_i2c4_set_platdata(NULL);
#else
	s3c_i2c4_set_platdata(NULL);
	if (!(system_rev != 3 && system_rev >= 0)) {
		i2c_register_board_info(4, i2c_devs4_max77693,
			ARRAY_SIZE(i2c_devs4_max77693));
	}
#endif
#endif /* CONFIG_S3C_DEV_I2C4 */
	midas_sound_init();

#ifdef CONFIG_S3C_DEV_I2C5
#if defined(CONFIG_MACH_T0_EUR_OPEN) || defined(CONFIG_MACH_T0_CHN_OPEN)
	s3c_i2c5_set_platdata(NULL);
#else
	if (need_i2c5()) {
		s3c_i2c5_set_platdata(&default_i2c5_data);
		i2c_register_board_info(5, i2c_devs5,
			ARRAY_SIZE(i2c_devs5));
	}
#endif
#endif

#if defined(CONFIG_INPUT_WACOM)
	midas_wacom_init();
#endif

#ifdef CONFIG_S3C_DEV_I2C6
#if defined(CONFIG_MACH_T0_EUR_OPEN) || defined(CONFIG_MACH_T0_CHN_OPEN)
	s3c_i2c6_set_platdata(&default_i2c6_data);
	i2c_register_board_info(6, i2c_devs6, ARRAY_SIZE(i2c_devs6));
#endif
#endif

#ifdef CONFIG_MACH_GC1
	s3c_i2c7_set_platdata(NULL);
	if (system_rev < 1) {
		i2c_register_board_info(7, i2c_devs7, ARRAY_SIZE(i2c_devs7));
	} else {
		i2c_register_board_info(7, i2c_devs7_s5m,
						ARRAY_SIZE(i2c_devs7_s5m));
	}
#else
	s3c_i2c7_set_platdata(NULL);
	i2c_register_board_info(7, i2c_devs7, ARRAY_SIZE(i2c_devs7));
#endif
#ifdef CONFIG_KEYBOARD_CYPRESS_TOUCH
	touchkey_init_hw();
#endif
	i2c_register_board_info(8, i2c_devs8_emul, ARRAY_SIZE(i2c_devs8_emul));

#if !defined(CONFIG_MACH_GC1) && !defined(CONFIG_LEDS_AAT1290A)
	gpio_request(GPIO_3_TOUCH_INT, "3_TOUCH_INT");
	s5p_register_gpio_interrupt(GPIO_3_TOUCH_INT);
#endif

	i2c_register_board_info(9, i2c_devs9_emul, ARRAY_SIZE(i2c_devs9_emul));

	i2c_register_board_info(10, i2c_devs10_emul,
				ARRAY_SIZE(i2c_devs10_emul));

	i2c_register_board_info(11, i2c_devs11_emul,
				ARRAY_SIZE(i2c_devs11_emul));

#if defined(CONFIG_PN65N_NFC) && \
	!defined(CONFIG_MACH_C1) && !defined(CONFIG_MACH_BAFFIN)
	i2c_register_board_info(12, i2c_devs12_emul,
				ARRAY_SIZE(i2c_devs12_emul));
#endif

#ifdef CONFIG_BATTERY_MAX17047_FUELGAUGE
	/* max17047 fuel gauge */
	i2c_register_board_info(14, i2c_devs14_emul,
				ARRAY_SIZE(i2c_devs14_emul));
#endif
#ifdef CONFIG_SAMSUNG_MHL
	printk(KERN_INFO "%s() register sii9234 driver\n", __func__);

	i2c_register_board_info(15, i2c_devs15_emul,
				ARRAY_SIZE(i2c_devs15_emul));
	i2c_register_board_info(16, i2c_devs16_emul,
				ARRAY_SIZE(i2c_devs16_emul));
#endif
#if defined(CONFIG_MACH_C1) || \
	defined(CONFIG_MACH_M3) || \
	defined(CONFIG_MACH_M0) || \
	defined(CONFIG_MACH_GC1) || defined(CONFIG_MACH_T0) ||\
	defined(CONFIG_MACH_BAFFIN)
#if defined(CONFIG_MACH_T0) && defined(CONFIG_TARGET_LOCALE_KOR)
	if (system_rev >= 9)
		max77693_haptic_pdata.motor_en = motor_en;
#endif
#if defined(CONFIG_MACH_T0_JPN_LTE_DCM)
	if (system_rev >= 12)
		max77693_haptic_pdata.motor_en = motor_en;
#endif
#if defined(CONFIG_MACH_BAFFIN_KOR_SKT) || \
	defined(CONFIG_MACH_BAFFIN_KOR_KT) || \
	defined(CONFIG_MACH_BAFFIN_KOR_LGT)
	if (system_rev >= 2)
		max77693_haptic_pdata.motor_en = motor_en;
#endif
	i2c_register_board_info(17, i2c_devs17_emul,
				ARRAY_SIZE(i2c_devs17_emul));
#endif

#if defined(CONFIG_LEDS_AN30259A) || defined(CONFIG_LEDS_LP5521)
	i2c_register_board_info(21, i2c_devs21_emul,
				ARRAY_SIZE(i2c_devs21_emul));
#endif
#if defined (CONFIG_BARCODE_EMUL_ICE4)
	i2c_register_board_info(22, i2c_devs22_emul,
				ARRAY_SIZE(i2c_devs22_emul));
#endif

#if defined(CONFIG_FELICA)
	i2c_register_board_info(30, i2c_devs30, ARRAY_SIZE(i2c_devs30));
#endif

#if defined(GPIO_OLED_DET)
	if (unlikely(gpio_request(GPIO_OLED_DET, "OLED_DET")))
		pr_err("Request GPIO_OLED_DET is failed\n");
	else {
		s5p_register_gpio_interrupt(GPIO_OLED_DET);
		gpio_free(GPIO_OLED_DET);
	}
#endif

#if defined(GPIO_ERR_FG)
	if (unlikely(gpio_request(GPIO_ERR_FG, "OLED_DET")))
		pr_err("Request GPIO_ERR_FG is failed\n");
	else {
		s5p_register_gpio_interrupt(GPIO_ERR_FG);
		gpio_free(GPIO_ERR_FG);
	}
#endif

#ifdef CONFIG_FB_S5P
#ifdef CONFIG_FB_S5P_LMS501KF03
	spi_register_board_info(spi_board_info, ARRAY_SIZE(spi_board_info));
	s3cfb_set_platdata(&lms501kf03_data);
#endif
#if defined(CONFIG_FB_S5P_MIPI_DSIM)
	mipi_fb_init();
#elif defined(CONFIG_FB_S5P_LD9040)
	ld9040_fb_init();
#elif defined(CONFIG_BACKLIGHT_PWM)
	samsung_bl_set(&smdk4212_bl_gpio_info, &smdk4212_bl_data);
#endif
	s3cfb_set_platdata(&fb_platform_data);

#ifdef CONFIG_EXYNOS_DEV_PD
	s3c_device_fb.dev.parent = &exynos4_device_pd[PD_LCD0].dev;
#endif
#endif
#ifdef CONFIG_USB_EHCI_S5P
	smdk4212_ehci_init();
#endif
#ifdef CONFIG_USB_OHCI_S5P
	smdk4212_ohci_init();
#endif
#ifdef CONFIG_USB_GADGET
	smdk4212_usbgadget_init();
#endif

#ifdef CONFIG_VIDEO_EXYNOS_FIMC_IS
	exynos4_fimc_is_set_platdata(NULL);
#ifdef CONFIG_EXYNOS_DEV_PD
	exynos4_device_fimc_is.dev.parent = &exynos4_device_pd[PD_ISP].dev;
#endif
#endif
#ifdef CONFIG_EXYNOS4_DEV_MSHC
	s3c_mshci_set_platdata(&exynos4_mshc_pdata);
#endif
#ifdef CONFIG_S3C_DEV_HSMMC
	s3c_sdhci0_set_platdata(&smdk4212_hsmmc0_pdata);
#endif
#ifdef CONFIG_S3C_DEV_HSMMC1
	s3c_sdhci1_set_platdata(&smdk4212_hsmmc1_pdata);
#endif
#ifdef CONFIG_S3C_DEV_HSMMC2
	s3c_sdhci2_set_platdata(&smdk4212_hsmmc2_pdata);
#endif
#ifdef CONFIG_S3C_DEV_HSMMC3
	s3c_sdhci3_set_platdata(&smdk4212_hsmmc3_pdata);
#endif

	midas_camera_init();

#ifdef CONFIG_FB_S5P_EXTDSP
	s3cfb_extdsp_set_platdata(&default_extdsp_data);
#endif

#if defined(CONFIG_FELICA)
	felica_setup();
#endif /* CONFIG_FELICA */

#if defined(CONFIG_VIDEO_TVOUT)
	s5p_hdmi_hpd_set_platdata(&hdmi_hpd_data);
	s5p_hdmi_cec_set_platdata(&hdmi_cec_data);
#if defined(CONFIG_MACH_GC1) && defined(CONFIG_HDMI_TX_STRENGTH)
	s5p_hdmi_tvout_set_platdata(&hdmi_tvout_data);
#endif
#ifdef CONFIG_EXYNOS_DEV_PD
	s5p_device_tvout.dev.parent = &exynos4_device_pd[PD_TV].dev;
	exynos4_device_pd[PD_TV].dev.parent = &exynos4_device_pd[PD_LCD0].dev;
#endif
#endif

#ifdef CONFIG_VIDEO_JPEG_V2X
#ifdef CONFIG_EXYNOS_DEV_PD
	s5p_device_jpeg.dev.parent = &exynos4_device_pd[PD_CAM].dev;
	exynos4_jpeg_setup_clock(&s5p_device_jpeg.dev, 160000000);
#endif
#endif

#ifdef CONFIG_ION_EXYNOS
	exynos_ion_set_platdata();
#endif

#if defined(CONFIG_VIDEO_MFC5X) || defined(CONFIG_VIDEO_SAMSUNG_S5P_MFC)
#ifdef CONFIG_EXYNOS_DEV_PD
	s5p_device_mfc.dev.parent = &exynos4_device_pd[PD_MFC].dev;
#endif
	exynos4_mfc_setup_clock(&s5p_device_mfc.dev, 200 * MHZ);
#endif

#if defined(CONFIG_VIDEO_SAMSUNG_S5P_MFC)
	dev_set_name(&s5p_device_mfc.dev, "s3c-mfc");
	clk_add_alias("mfc", "s5p-mfc", "mfc", &s5p_device_mfc.dev);
	s5p_mfc_setname(&s5p_device_mfc, "s5p-mfc");
#endif
#ifdef CONFIG_VIDEO_FIMG2D
	s5p_fimg2d_set_platdata(&fimg2d_data);
#endif
#ifdef CONFIG_EXYNOS_C2C
	exynos_c2c_set_platdata(&smdk4212_c2c_pdata);
#endif

	brcm_wlan_init();

#ifdef CONFIG_EXYNOS4_SETUP_THERMAL
	s5p_tmu_set_platdata(&midas_tmu_data);
#endif

	exynos_sysmmu_init();

	platform_add_devices(midas_devices, ARRAY_SIZE(midas_devices));

#ifdef CONFIG_S3C_ADC
#if defined(CONFIG_MACH_GC1) || \
	defined(CONFIG_MACH_T0) || \
	defined(CONFIG_MACH_M3) || \
	defined(CONFIG_MACH_BAFFIN)
	platform_device_register(&s3c_device_adc);
#else
	if (system_rev != 3)
		platform_device_register(&s3c_device_adc);
#endif

#if defined(CONFIG_MACH_GC1)
	gc1_jack_init();
#endif

#endif
#if defined(CONFIG_BATTERY_SAMSUNG)
	platform_device_register(&samsung_device_battery);
#endif
#ifdef CONFIG_SEC_THERMISTOR
	platform_device_register(&sec_device_thermistor);
#endif
#ifdef CONFIG_SEC_SUBTHERMISTOR
	platform_device_register(&sec_device_subthermistor);
#endif
#if defined(CONFIG_MACH_M0_CTC)
	midas_gpiokeys_platform_data.buttons = m0_buttons;
	midas_gpiokeys_platform_data.nbuttons = ARRAY_SIZE(m0_buttons);
#elif defined(CONFIG_MACH_C1) || \
	defined(CONFIG_MACH_M0)
	if (system_rev != 3 && system_rev >= 1) {
		midas_gpiokeys_platform_data.buttons = m0_buttons;
		midas_gpiokeys_platform_data.nbuttons = ARRAY_SIZE(m0_buttons);
	}
#endif
	/* Above logic is too complex. Let's override whatever the
	   result is... */

#if defined(CONFIG_MACH_M0)
#if defined(CONFIG_MACH_M0_CHNOPEN) || defined(CONFIG_MACH_M0_HKTW)
	{
#else
	if (system_rev >= 11) {
#endif
		s3c_gpio_setpull(GPIO_OK_KEY_ANDROID, S3C_GPIO_PULL_NONE);
		midas_gpiokeys_platform_data.buttons = m0_rev11_buttons;
		midas_gpiokeys_platform_data.nbuttons =
			ARRAY_SIZE(m0_rev11_buttons);
	}
#elif defined(CONFIG_TARGET_LOCALE_KOR) && \
	(defined(CONFIG_MACH_M0) || defined(CONFIG_MACH_C1))
		s3c_gpio_setpull(GPIO_OK_KEY_ANDROID, S3C_GPIO_PULL_NONE);
		midas_gpiokeys_platform_data.buttons = c1_rev04_buttons;
		midas_gpiokeys_platform_data.nbuttons =
			ARRAY_SIZE(c1_rev04_buttons);

#elif defined(CONFIG_MACH_C1_USA_ATT)
	if (system_rev >= 7) {
		s3c_gpio_setpull(GPIO_OK_KEY_ANDROID, S3C_GPIO_PULL_UP);
		midas_gpiokeys_platform_data.buttons = m0_rev11_buttons;
		midas_gpiokeys_platform_data.nbuttons =
			ARRAY_SIZE(m0_rev11_buttons);
	}
#elif defined(CONFIG_MACH_T0) || defined(CONFIG_MACH_M3)
	midas_gpiokeys_platform_data.buttons = t0_buttons;
	midas_gpiokeys_platform_data.nbuttons = ARRAY_SIZE(t0_buttons);
#if defined(CONFIG_MACH_M3_USA_TMO)
	if (system_rev == 0) {
		midas_gpiokeys_platform_data.buttons = m3_uas_tmo_00_buttons;
		midas_gpiokeys_platform_data.nbuttons =
				ARRAY_SIZE(m3_uas_tmo_00_buttons);
	} else {
		midas_gpiokeys_platform_data.buttons = t0_buttons;
		midas_gpiokeys_platform_data.nbuttons = ARRAY_SIZE(t0_buttons);
	}
#endif

#elif defined(CONFIG_MACH_BAFFIN)
	s3c_gpio_setpull(GPIO_OK_KEY_ANDROID, S3C_GPIO_PULL_UP);
	midas_gpiokeys_platform_data.buttons = c1_rev04_buttons;
	midas_gpiokeys_platform_data.nbuttons =
		ARRAY_SIZE(c1_rev04_buttons);
#endif
#ifdef CONFIG_MACH_GC1
	/*for emul type*/
	if (system_rev < 2) {
		printk(KERN_DEBUG"[KEYS] rev %x. switch wide/tele gpio\n",
			system_rev);
		gpio_direction_output(GPIO_TOP_PCB_PWREN, 1);
		midas_buttons[3].gpio = GPIO_WIDE_KEY;
		midas_buttons[4].gpio = GPIO_TELE_KEY;
		midas_buttons[5].code = KEY_RECORD;
		midas_buttons[5].gpio = GPIO_RECORD_KEY;
		midas_buttons[6].code = KEY_PLAY;
		midas_buttons[6].gpio = GPIO_PLAY_KEY;
	}
	/*strobe open/close*/
	gpio_request(STR_PU_DET_18V, "STR_PU_DET_18V");
	s3c_gpio_cfgpin(STR_PU_DET_18V, S3C_GPIO_SFN(0xf));
	s5p_register_gpio_interrupt(STR_PU_DET_18V);
	gpio_direction_input(STR_PU_DET_18V);
#endif
	platform_device_register(&midas_keypad);

#if defined(CONFIG_S3C_DEV_I2C5)
	if (need_i2c5())
		platform_device_register(&s3c_device_i2c5);
#endif

#if defined(CONFIG_PN65N_NFC) && \
	!defined(CONFIG_MACH_C1) && !defined(CONFIG_MACH_BAFFIN)
	platform_device_register(&s3c_device_i2c12);
#endif

#if defined(CONFIG_S3C64XX_DEV_SPI)
#if defined(CONFIG_VIDEO_S5C73M3_SPI)
	sclk = clk_get(spi1_dev, "dout_spi1");
	if (IS_ERR(sclk))
		dev_err(spi1_dev, "failed to get sclk for SPI-1\n");
	prnt = clk_get(spi1_dev, "mout_mpll_user");
	if (IS_ERR(prnt))
		dev_err(spi1_dev, "failed to get prnt\n");
	if (clk_set_parent(sclk, prnt))
		printk(KERN_ERR "Unable to set parent %s of clock %s.\n",
		       prnt->name, sclk->name);

	clk_set_rate(sclk, 100 * 1000 * 1000); /*50MHz*/

	clk_put(sclk);
	clk_put(prnt);

	if (!gpio_request(EXYNOS4_GPB(5), "SPI_CS1")) {
		gpio_direction_output(EXYNOS4_GPB(5), 1);
		s3c_gpio_cfgpin(EXYNOS4_GPB(5), S3C_GPIO_SFN(1));
		s3c_gpio_setpull(EXYNOS4_GPB(5), S3C_GPIO_PULL_UP);
		exynos_spi_set_info(1, EXYNOS_SPI_SRCCLK_SCLK,
				    ARRAY_SIZE(spi1_csi));
	}

	for (gpio = EXYNOS4_GPB(4); gpio < EXYNOS4_GPB(8); gpio++)
		s5p_gpio_set_drvstr(gpio, S5P_GPIO_DRVSTR_LV3);

	spi_register_board_info(spi1_board_info, ARRAY_SIZE(spi1_board_info));
#endif

#if defined(CONFIG_LINK_DEVICE_SPI) \
	|| defined(CONFIG_TDMB) || defined(CONFIG_TDMB_MODULE) \
	|| defined(CONFIG_ISDBT) || defined(CONFIG_LINK_DEVICE_PLD)
	sclk = NULL;
	prnt = NULL;

	sclk = clk_get(spi2_dev, "dout_spi2");
	if (IS_ERR(sclk))
		dev_err(spi2_dev, "failed to get sclk for SPI-2\n");
	prnt = clk_get(spi2_dev, "mout_mpll_user");
	if (IS_ERR(prnt))
		dev_err(spi2_dev, "failed to get prnt\n");
	if (clk_set_parent(sclk, prnt))
		printk(KERN_ERR "Unable to set parent %s of clock %s.\n",
		       prnt->name, sclk->name);

	clk_put(sclk);
	clk_put(prnt);

	if (!gpio_request(EXYNOS4_GPC1(2), "SPI_CS2")) {
		gpio_direction_output(EXYNOS4_GPC1(2), 1);
		s3c_gpio_cfgpin(EXYNOS4_GPC1(2), S3C_GPIO_SFN(1));
		s3c_gpio_setpull(EXYNOS4_GPC1(2), S3C_GPIO_PULL_UP);
		exynos_spi_set_info(2, EXYNOS_SPI_SRCCLK_SCLK,
			ARRAY_SIZE(spi2_csi));
	}
	for (gpio = EXYNOS4_GPC1(1); gpio < EXYNOS4_GPC1(5); gpio++)
		s5p_gpio_set_drvstr(gpio, S5P_GPIO_DRVSTR_LV3);

	spi_register_board_info(spi2_board_info, ARRAY_SIZE(spi2_board_info));
#endif

#if defined(CONFIG_TDMB) || defined(CONFIG_TDMB_MODULE)
	tdmb_dev_init();
#elif defined(CONFIG_ISDBT)
	isdbt_dev_init();
#endif
#endif

#ifdef CONFIG_BUSFREQ_OPP
	dev_add(&busfreq, &exynos4_busfreq.dev);

	/* PPMUs using for cpufreq get clk from clk_list */
	ppmu_clk = clk_get(NULL, "ppmudmc0");
	if (IS_ERR(ppmu_clk))
		printk(KERN_ERR "failed to get ppmu_dmc0\n");
	clk_enable(ppmu_clk);
	clk_put(ppmu_clk);

	ppmu_clk = clk_get(NULL, "ppmudmc1");
	if (IS_ERR(ppmu_clk))
		printk(KERN_ERR "failed to get ppmu_dmc1\n");
	clk_enable(ppmu_clk);
	clk_put(ppmu_clk);

	ppmu_clk = clk_get(NULL, "ppmucpu");
	if (IS_ERR(ppmu_clk))
		printk(KERN_ERR "failed to get ppmu_cpu\n");
	clk_enable(ppmu_clk);
	clk_put(ppmu_clk);

	ppmu_init(&exynos_ppmu[PPMU_DMC0], &exynos4_busfreq.dev);
	ppmu_init(&exynos_ppmu[PPMU_DMC1], &exynos4_busfreq.dev);
	ppmu_init(&exynos_ppmu[PPMU_CPU], &exynos4_busfreq.dev);
#endif


	/* 400 kHz for initialization of MMC Card  */
	__raw_writel((__raw_readl(EXYNOS4_CLKDIV_FSYS3) & 0xfffffff0)
		     | 0x9, EXYNOS4_CLKDIV_FSYS3);
#if defined(CONFIG_MACH_T0) || defined(CONFIG_MACH_M3_JPN_DCM)
	__raw_writel((__raw_readl(EXYNOS4_CLKDIV_FSYS2) & 0xfff0fff0)
		     | 0x90009, EXYNOS4_CLKDIV_FSYS2);
#else
	__raw_writel((__raw_readl(EXYNOS4_CLKDIV_FSYS2) & 0xfff0fff0)
		     | 0x80008, EXYNOS4_CLKDIV_FSYS2);
#endif
	__raw_writel((__raw_readl(EXYNOS4_CLKDIV_FSYS1) & 0xfff0fff0)
		     | 0x80008, EXYNOS4_CLKDIV_FSYS1);
}

#ifdef CONFIG_EXYNOS_C2C
static void __init exynos_c2c_reserve(void)
{
	static struct cma_region region = {
			.name = "c2c_shdmem",
			.size = 64 * SZ_1M,
			{ .alignment	= 64 * SZ_1M },
			.start = C2C_SHAREDMEM_BASE
	};

	BUG_ON(cma_early_region_register(&region));
	BUG_ON(cma_early_region_reserve(&region));

	pr_info("%s %10s %8x %8x\n", __func__,
		region.name, region.start, region.size);
}
#endif

#ifdef CONFIG_DMA_CMA
static void __init exynos4_reserve(void)
{
	int ret = 0;

#ifdef CONFIG_USE_FIMC_CMA
	ret = dma_declare_contiguous(&s3c_device_fimc1.dev,
		CONFIG_VIDEO_SAMSUNG_MEMSIZE_FIMC1 * SZ_1K, 0x65800000, 0);
	if (ret != 0)
		panic("alloc failed for FIMC1\n");
	else {
		static struct cma_region fimc_reg = {
			.name = "fimc1",
			.size = CONFIG_VIDEO_SAMSUNG_MEMSIZE_FIMC1 * SZ_1K,
			.start = 0x65800000,
			.reserved = 1,
		};

		if (cma_early_region_register(&fimc_reg))
			pr_err("S5P/CMA: Failed to register '%s'\n",
						fimc_reg.name);
	}
#endif

#if defined(CONFIG_USE_MFC_CMA) && defined(CONFIG_MACH_M0)
	ret = dma_declare_contiguous(&s5p_device_mfc.dev,
			0x02800000, 0x5C800000, 0);
	if (ret == 0) {
		static struct cma_region mfc_reg = {
			.name = "mfc",
			.size = 0x02800000,
			.start = 0x5C800000,
			.reserved = 1,
		};

		if (cma_early_region_register(&mfc_reg))
			pr_err("S5P/CMA: Failed to register '%s'\n",
						mfc_reg.name);
	}
#endif
	if (ret != 0)
		printk(KERN_ERR "%s Fail\n", __func__);
}
#endif

static void __init exynos_init_reserve(void)
{
	sec_debug_magic_init();
}

MACHINE_START(SMDK4412, "SMDK4x12")
	.boot_params	= S5P_PA_SDRAM + 0x100,
	.init_irq	= exynos4_init_irq,
	.map_io		= midas_map_io,
	.init_machine	= midas_machine_init,
	.timer		= &exynos4_timer,
#if defined(CONFIG_EXYNOS_C2C)
	.reserve	= &exynos_c2c_reserve,
#elif defined(CONFIG_DMA_CMA)
	.reserve	= &exynos4_reserve,
#endif
	.init_early	= &exynos_init_reserve,
MACHINE_END

MACHINE_START(SMDK4212, "SMDK4x12")
	.boot_params	= S5P_PA_SDRAM + 0x100,
	.init_irq	= exynos4_init_irq,
	.map_io		= midas_map_io,
	.init_machine	= midas_machine_init,
	.timer		= &exynos4_timer,
#if defined(CONFIG_EXYNOS_C2C)
	.reserve	= &exynos_c2c_reserve,
#elif defined(CONFIG_DMA_CMA)
	.reserve	= &exynos4_reserve,
#endif
	.init_early	= &exynos_init_reserve,
MACHINE_END
