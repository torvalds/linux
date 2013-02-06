/* linux/arch/arm/mach-exynos/mach-trats.c
 *
 * Copyright (c) 2011-2012 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/serial_core.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/gpio_keys.h>
#include <linux/gpio_event.h>
#include <linux/lcd.h>
#include <linux/lcd-property.h>
#include <linux/mmc/host.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/i2c.h>
#include <linux/i2c-gpio.h>
#include <linux/input.h>
#include <linux/spi/spi.h>
#include <linux/spi/spi_gpio.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/fixed.h>
#include <linux/mfd/max8997.h>
#include <linux/mfd/max8997-private.h>
#include <linux/sensor/ak8975.h>
#include <linux/kr3dh.h>
#include <linux/utsname.h>
#include <linux/pn544.h>
#include <linux/sensor/gp2a.h>
#include <linux/printk.h>
#ifdef CONFIG_UART_SELECT
#include <linux/uart_select.h>
#endif
#if defined(CONFIG_SND_SOC_SLP_TRATS_MC1N2)
#include <linux/mfd/mc1n2_pdata.h>
#endif
#include <linux/memblock.h>
#include <linux/power_supply.h>
#if defined(CONFIG_S5P_MEM_CMA)
#include <linux/cma.h>
#endif
#ifdef CONFIG_JACK_MON
#include <linux/jack.h>
#endif
#ifdef CONFIG_ANDROID_PMEM
#include <linux/android_pmem.h>
#endif
#include <linux/k3g.h>

#include <asm/mach/arch.h>
#include <asm/mach-types.h>

#include <video/platform_lcd.h>

#include <plat/regs-serial.h>
#include <plat/regs-srom.h>
#include <plat/exynos4.h>
#include <plat/clock.h>
#include <plat/hwmon.h>
#include <plat/cpu.h>
#include <plat/devs.h>
#include <plat/fimc.h>
#include <plat/csis.h>
#include <plat/gpio-cfg.h>
#include <plat/adc.h>
#include <plat/ts.h>
#include <plat/keypad.h>
#include <plat/sdhci.h>
#include <plat/mshci.h>
#include <plat/iic.h>
#include <plat/sysmmu.h>
#include <plat/pd.h>
#include <plat/media.h>
#include <plat/udc-hs.h>
#include <plat/s5p-clock.h>
#include <plat/fimg2d.h>
#include <plat/ehci.h>
#include <plat/usbgadget.h>

#ifdef CONFIG_S3C64XX_DEV_SPI
#include <plat/s3c64xx-spi.h>
#endif

#include <mach/map.h>
#include <mach/exynos-clock.h>
#include <mach/media.h>

#include <mach/dev-sysmmu.h>
#include <mach/dev.h>
#include <mach/regs-clock.h>
#include <mach/exynos-ion.h>

#include <drm/exynos_drm.h>
#include <plat/regs-fb.h>
#include <plat/fb-core.h>
#include <plat/mipi_dsim2.h>
#include <plat/fimd_lite_ext.h>

#include <plat/hdmi.h>
#if defined(CONFIG_VIDEO_SAMSUNG_S5P_MFC) || defined(CONFIG_VIDEO_MFC5X)
#include <plat/s5p-mfc.h>
#endif

#ifdef CONFIG_S3C64XX_DEV_SPI
#include <mach/spi-clocks.h>
#endif

#ifdef CONFIG_VIDEO_M5MO
#include <media/m5mo_platform.h>
#endif
#ifdef CONFIG_VIDEO_S5K5BAFX
#include <media/s5k5bafx_platform.h>
#endif

#if defined(CONFIG_EXYNOS4_SETUP_THERMAL)
#include <plat/s5p-tmu.h>
#include <mach/regs-tmu.h>
#endif

#ifdef CONFIG_SEC_DEV_JACK
#include <linux/sec_jack.h>
#endif

#ifdef CONFIG_BT_BCM4330
#include <mach/board-bluetooth-bcm.h>
#endif

#include "u1.h"

#include <mach/sec_debug.h>

#ifdef	CONFIG_SAMSUNG_MHL
#include <linux/irq.h>
#include <linux/sii9234.h>
#endif

#ifdef CONFIG_BATTERY_SEC_U1
#include <linux/power/sec_battery_u1.h>
#endif

#ifdef CONFIG_SEC_THERMISTOR
#include <mach/sec_thermistor.h>
#endif

#ifdef CONFIG_BATTERY_MAX17042_FUELGAUGE_U1
#include <linux/power/max17042_fuelgauge_u1.h>
#endif

#ifdef CONFIG_CHARGER_MAX8922_U1
#include <linux/power/max8922_charger_u1.h>
#endif

#ifdef CONFIG_SMB136_CHARGER_Q1
#include <linux/power/smb136_charger_q1.h>
#endif

#ifdef CONFIG_SMB328_CHARGER
#include <linux/power/smb328_charger.h>
#endif

#ifdef CONFIG_USB_HOST_NOTIFY
#include <linux/host_notify.h>
#endif


#if defined(CONFIG_TDMB) || defined(CONFIG_TDMB_MODULE)
#include <mach/tdmb_pdata.h>
#endif

#ifdef CONFIG_LEDS_MAX8997
#include <linux/leds-max8997.h>
#endif

#if defined(CONFIG_PHONE_IPC_SPI)
#include <linux/phone_svn/ipc_spi.h>
#include <linux/irq.h>
#endif

#ifdef CONFIG_TOUCHSCREEN_MELFAS_MMS
#include <linux/melfas_mms_ts.h>
#endif

#ifdef CONFIG_GPS_GSD4T
#include <mach/gsd4t.h>
#endif

/* Following are default values for UCON, ULCON and UFCON UART registers */
#define SMDKC210_UCON_DEFAULT	(S3C2410_UCON_TXILEVEL |	\
				 S3C2410_UCON_RXILEVEL |	\
				 S3C2410_UCON_TXIRQMODE |	\
				 S3C2410_UCON_RXIRQMODE |	\
				 S3C2410_UCON_RXFIFO_TOI |	\
				 S3C2443_UCON_RXERR_IRQEN)

#define SMDKC210_ULCON_DEFAULT	S3C2410_LCON_CS8

#define SMDKC210_UFCON_DEFAULT	(S3C2410_UFCON_FIFOMODE |	\
				 S5PV210_UFCON_TXTRIG4 |	\
				 S5PV210_UFCON_RXTRIG4)

enum fixed_regulator_id {
	FIXED_REG_ID_LCD = 0,
	FIXED_REG_ID_HDMI = 1,
	FIXED_REG_ID_LED_A,
};

enum board_rev {
	U1HD_5INCH_REV0_0 = 0x2,
	U1HD_REV0_1 = 0x1,
};

static struct s3c2410_uartcfg smdkc210_uartcfgs[] __initdata = {
	[0] = {
		.hwport = 0,
		.flags = 0,
		.ucon = SMDKC210_UCON_DEFAULT,
		.ulcon = SMDKC210_ULCON_DEFAULT,
		.ufcon = SMDKC210_UFCON_DEFAULT,
#ifdef CONFIG_BT_BCM4330
		.wake_peer = bcm_bt_lpm_exit_lpm_locked,
#endif
	},
	[1] = {
		.hwport = 1,
		.flags = 0,
		.ucon = SMDKC210_UCON_DEFAULT,
		.ulcon = SMDKC210_ULCON_DEFAULT,
		.ufcon = SMDKC210_UFCON_DEFAULT,
		.set_runstate = set_gps_uart_op,
	},
	[2] = {
		.hwport = 2,
		.flags = 0,
		.ucon = SMDKC210_UCON_DEFAULT,
		.ulcon = SMDKC210_ULCON_DEFAULT,
		.ufcon = SMDKC210_UFCON_DEFAULT,
	},
	[3] = {
		.hwport = 3,
		.flags = 0,
		.ucon = SMDKC210_UCON_DEFAULT,
		.ulcon = SMDKC210_ULCON_DEFAULT,
		.ufcon = SMDKC210_UFCON_DEFAULT,
	},
};

#ifdef CONFIG_VIDEO_FIMC
/*
 * External camera reset
 * Because the most of cameras take i2c bus signal, so that
 * you have to reset at the boot time for other i2c slave devices.
 * This function also called at fimc_init_camera()
 * Do optimization for cameras on your platform.
 */

#ifdef CONFIG_VIDEO_M5MO
#define CAM_CHECK_ERR_RET(x, msg)					\
	if (unlikely((x) < 0)) {					\
		printk(KERN_ERR "\nfail to %s: err = %d\n", msg, x);	\
		return x;						\
	}
#define CAM_CHECK_ERR(x, msg)						\
	if (unlikely((x) < 0)) {					\
		printk(KERN_ERR "\nfail to %s: err = %d\n", msg, x);	\
	}

static int m5mo_get_i2c_busnum(void)
{
#ifdef CONFIG_VIDEO_M5MO_USE_SWI2C
	return 25;
#else
	return 0;
#endif
}

static int m5mo_power_on(void)
{
	struct regulator *regulator;
	int ret = 0;

	printk(KERN_DEBUG "%s: in\n", __func__);

	ret = gpio_request(GPIO_CAM_VGA_nSTBY, "GPL2");
	if (ret) {
		printk(KERN_ERR "faile to request gpio(GPIO_CAM_VGA_nSTBY)\n");
		return ret;
	}
	ret = gpio_request(GPIO_CAM_VGA_nRST, "GPL2");
	if (ret) {
		printk(KERN_ERR "faile to request gpio(GPIO_CAM_VGA_nRST)\n");
		return ret;
	}
	ret = gpio_request(GPIO_CAM_SENSOR_CORE, "GPE2");
	if (ret) {
		printk(KERN_ERR "fail to request gpio(CAM_SENSOR_CORE)\n");
		return ret;
	}
	ret = gpio_request(GPIO_CAM_IO_EN, "GPE2");
	if (ret) {
		printk(KERN_ERR "fail to request gpio(CAM_IO_EN)\n");
		return ret;
	}
	ret = gpio_request(GPIO_VT_CAM_15V, "GPE2");
	if (ret) {
		printk(KERN_ERR "faile to request gpio(GPIO_VT_CAM_15V)\n");
		return ret;
	}
	ret = gpio_request(GPIO_ISP_RESET, "ISP_RESET");
	if (ret) {
		printk(KERN_ERR "faile to request gpio(ISP_RESET)\n");
		return ret;
	}
	ret = gpio_request(GPIO_8M_AF_EN, "GPK1");
	if (ret) {
		printk(KERN_ERR "fail to request gpio(8M_AF_EN)\n");
		return ret;
	}

	/* CAM_VT_nSTBY low */
	ret = gpio_direction_output(GPIO_CAM_VGA_nSTBY, 0);
	CAM_CHECK_ERR_RET(ret, "output VGA_nSTBY");

	/* CAM_VT_nRST	low */
	gpio_direction_output(GPIO_CAM_VGA_nRST, 0);
	CAM_CHECK_ERR_RET(ret, "output VGA_nRST");
	udelay(10);

	/* CAM_ISP_CORE_1.2V */
	regulator = regulator_get(NULL, "cam_isp_core");
	if (IS_ERR(regulator))
		return -ENODEV;
	ret = regulator_enable(regulator);
	regulator_put(regulator);
	CAM_CHECK_ERR_RET(ret, "enable cam_isp_core");
	/* No delay */

	/* CAM_SENSOR_CORE_1.2V */
	ret = gpio_direction_output(GPIO_CAM_SENSOR_CORE, 1);
	CAM_CHECK_ERR_RET(ret, "output senser_core");

#if defined(CONFIG_MACH_Q1_BD)
	udelay(120);
#else
	udelay(10);
#endif

	/* CAM_SENSOR_A2.8V */
	ret = gpio_direction_output(GPIO_CAM_IO_EN, 1);
	CAM_CHECK_ERR_RET(ret, "output IO_EN");
	/* it takes about 100us at least during level transition. */
	udelay(160);		/* 130us -> 160us */

	/* VT_CORE_1.5V */
	ret = gpio_direction_output(GPIO_VT_CAM_15V, 1);
	CAM_CHECK_ERR_RET(ret, "output VT_CAM_1.5V");
	udelay(20);

#if defined(CONFIG_MACH_Q1_BD)
	udelay(120);
#endif

	/* CAM_AF_2.8V */
	ret = gpio_direction_output(GPIO_8M_AF_EN, 1);
	CAM_CHECK_ERR(ret, "output AF");
	mdelay(7);

	/* VT_CAM_1.8V */
	regulator = regulator_get(NULL, "vt_cam_1.8v");
	if (IS_ERR(regulator))
		return -ENODEV;
	ret = regulator_enable(regulator);
	regulator_put(regulator);
	CAM_CHECK_ERR_RET(ret, "enable vt_1.8v");
	udelay(10);

	/* CAM_ISP_1.8V */
	regulator = regulator_get(NULL, "cam_isp");
	if (IS_ERR(regulator))
		return -ENODEV;
	ret = regulator_enable(regulator);
	regulator_put(regulator);
	CAM_CHECK_ERR_RET(ret, "enable cam_isp");
	udelay(120);		/* at least */

	/* CAM_SENSOR_IO_1.8V */
	regulator = regulator_get(NULL, "cam_sensor_io");
	if (IS_ERR(regulator))
		return -ENODEV;
	ret = regulator_enable(regulator);
	regulator_put(regulator);
	CAM_CHECK_ERR_RET(ret, "enable sensor_io");
	udelay(30);

	/* MCLK */
	ret = s3c_gpio_cfgpin(GPIO_CAM_MCLK, S3C_GPIO_SFN(2));
	CAM_CHECK_ERR_RET(ret, "cfg mclk");
	s3c_gpio_setpull(GPIO_CAM_MCLK, S3C_GPIO_PULL_NONE);
	udelay(70);

	/* ISP_RESET */
	ret = gpio_direction_output(GPIO_ISP_RESET, 1);
	CAM_CHECK_ERR_RET(ret, "output reset");
	mdelay(4);

	gpio_free(GPIO_CAM_VGA_nSTBY);
	gpio_free(GPIO_CAM_VGA_nRST);
	gpio_free(GPIO_CAM_SENSOR_CORE);
	gpio_free(GPIO_CAM_IO_EN);
	gpio_free(GPIO_VT_CAM_15V);
	gpio_free(GPIO_ISP_RESET);
	gpio_free(GPIO_8M_AF_EN);
	printk(KERN_DEBUG "%s: out\n", __func__);

	return ret;
}

#ifdef CONFIG_SAMSUNG_MHL
static void sii9234_cfg_gpio(void)
{
	printk(KERN_INFO "%s()\n", __func__);

	s3c_gpio_cfgpin(GPIO_AP_SDA_18V, S3C_GPIO_SFN(0x0));
	s3c_gpio_setpull(GPIO_AP_SDA_18V, S3C_GPIO_PULL_NONE);

	s3c_gpio_cfgpin(GPIO_AP_SCL_18V, S3C_GPIO_SFN(0x1));
	s3c_gpio_setpull(GPIO_AP_SCL_18V, S3C_GPIO_PULL_NONE);

	s3c_gpio_cfgpin(GPIO_MHL_WAKE_UP, S3C_GPIO_INPUT);
	irq_set_irq_type(MHL_WAKEUP_IRQ, IRQ_TYPE_EDGE_RISING);
	s3c_gpio_setpull(GPIO_MHL_WAKE_UP, S3C_GPIO_PULL_DOWN);

	gpio_request(GPIO_MHL_INT, "MHL_INT");
	s5p_register_gpio_interrupt(GPIO_MHL_INT);
	s3c_gpio_setpull(GPIO_MHL_INT, S3C_GPIO_PULL_DOWN);
	irq_set_irq_type(MHL_INT_IRQ, IRQ_TYPE_EDGE_RISING);
	s3c_gpio_cfgpin(GPIO_MHL_INT, GPIO_MHL_INT_AF);

	s3c_gpio_cfgpin(GPIO_HDMI_EN_REV07, S3C_GPIO_OUTPUT);
	gpio_set_value(GPIO_HDMI_EN_REV07, GPIO_LEVEL_LOW);
	s3c_gpio_setpull(GPIO_HDMI_EN_REV07, S3C_GPIO_PULL_NONE);

	s3c_gpio_cfgpin(GPIO_MHL_RST, S3C_GPIO_OUTPUT);
	s3c_gpio_setpull(GPIO_MHL_RST, S3C_GPIO_PULL_NONE);
	gpio_set_value(GPIO_MHL_RST, GPIO_LEVEL_LOW);

	s3c_gpio_cfgpin(GPIO_MHL_SEL, S3C_GPIO_OUTPUT);
	s3c_gpio_setpull(GPIO_MHL_SEL, S3C_GPIO_PULL_NONE);
	gpio_set_value(GPIO_MHL_SEL, GPIO_LEVEL_LOW);

}

void sii9234_power_onoff(bool on)
{
	pr_info("%s(%d)\n", __func__, on);

	if (on) {
		/* To avoid floating state of the HPD pin *
		 * in the absence of external pull-up     */
		s3c_gpio_setpull(GPIO_HDMI_HPD, S3C_GPIO_PULL_NONE);
		gpio_set_value(GPIO_HDMI_EN_REV07, GPIO_LEVEL_HIGH);

		s3c_gpio_setpull(GPIO_AP_SCL_18V, S3C_GPIO_PULL_DOWN);
		s3c_gpio_setpull(GPIO_AP_SCL_18V, S3C_GPIO_PULL_NONE);
	} else {
		gpio_set_value(GPIO_MHL_RST, GPIO_LEVEL_LOW);
		usleep_range(10000, 20000);
		gpio_set_value(GPIO_MHL_RST, GPIO_LEVEL_HIGH);

		/* To avoid floating state of the HPD pin *
		 * in the absence of external pull-up     */
		s3c_gpio_setpull(GPIO_HDMI_HPD, S3C_GPIO_PULL_DOWN);
		gpio_set_value(GPIO_HDMI_EN_REV07, GPIO_LEVEL_LOW);
		gpio_set_value(GPIO_MHL_RST, GPIO_LEVEL_LOW);
	}
	pr_info("[MHL]%s : %d\n", __func__, on);
}

void sii9234_reset(void)
{
	printk(KERN_INFO "%s()\n", __func__);

	s3c_gpio_cfgpin(GPIO_MHL_RST, S3C_GPIO_OUTPUT);
	s3c_gpio_setpull(GPIO_MHL_RST, S3C_GPIO_PULL_NONE);

	gpio_set_value(GPIO_MHL_RST, GPIO_LEVEL_LOW);
	usleep_range(10000, 20000);
	gpio_set_value(GPIO_MHL_RST, GPIO_LEVEL_HIGH);
}

void mhl_usb_switch_control(bool on)
{
	pr_info("%s() [MHL] USB path change : %s\n",
			__func__, on ? "MHL" : "USB");
	if (on == 1) {
		if (gpio_get_value(GPIO_MHL_SEL))
			pr_info("[MHL] GPIO_MHL_SEL :already 1\n");
		else {
			gpio_set_value(GPIO_MHL_SEL, GPIO_LEVEL_HIGH);
			/* sii9234_cfg_power(1);	// onegun */
			/* sii9234_init();		// onegun */
		}
	} else {
		if (!gpio_get_value(GPIO_MHL_SEL))
			pr_info("[MHL]	GPIO_MHL_SEL :already0\n");
		else {
			/* sii9234_cfg_power(0);	// onegun */
			gpio_set_value(GPIO_MHL_SEL, GPIO_LEVEL_LOW);
		}
	}
}

static struct sii9234_platform_data sii9234_pdata = {
	.init = sii9234_cfg_gpio,
	.mhl_sel = mhl_usb_switch_control,
	.hw_onoff = sii9234_power_onoff,
	.hw_reset = sii9234_reset,
	.enable_vbus = NULL,
	.vbus_present = NULL,
#ifdef CONFIG_EXTCON
	.extcon_name = "max8997-muic",
#endif
};

static struct i2c_board_info __initdata tuna_i2c15_boardinfo[] = {
	{
		I2C_BOARD_INFO("sii9234_mhl_tx", 0x72>>1),
		.platform_data = &sii9234_pdata,
	},
	{
		I2C_BOARD_INFO("sii9234_tpi", 0x7A>>1),
		.platform_data = &sii9234_pdata,
	},
	{
		I2C_BOARD_INFO("sii9234_hdmi_rx", 0x92>>1),
		.platform_data = &sii9234_pdata,
	},
	{
		I2C_BOARD_INFO("sii9234_cbus", 0xC8>>1),
		.platform_data = &sii9234_pdata,
	},
};

#define I2C_BUS_ID_MHL	15
static struct i2c_gpio_platform_data gpio_i2c_data15 = {
	.sda_pin = GPIO_MHL_SDA_18V,
	.scl_pin = GPIO_MHL_SCL_18V,
	.udelay = 2,
	.sda_is_open_drain	= 0,
	.scl_is_open_drain	= 0,
	.scl_is_output_only	= 0,
};

struct platform_device s3c_device_i2c15 = {
	.name = "i2c-gpio",
	.id = I2C_BUS_ID_MHL,
	.dev = {
		.platform_data = &gpio_i2c_data15,
	}
};
#endif

#ifdef CONFIG_JACK_MON
static struct jack_platform_data trats_jack_data = {
	.usb_online		= 0,
	.charger_online	= 0,
	.hdmi_online	= 0,
	.earjack_online	= 0,
	.earkey_online	= 0,
	.ums_online		= -1,
	.cdrom_online	= -1,
	.jig_online		= -1,
	.host_online	= 0,
};

static struct platform_device trats_jack = {
	.name		= "jack",
	.id			= -1,
	.dev		= {
		.platform_data = &trats_jack_data,
	},
};
#endif

static int m5mo_power_down(void)
{
	struct regulator *regulator;
	int ret = 0;

	printk(KERN_DEBUG "%s: in\n", __func__);

	ret = gpio_request(GPIO_8M_AF_EN, "GPK1");
	if (ret) {
		printk(KERN_ERR "fail to request gpio(8M_AF_EN)\n");
		return ret;
	}
	ret = gpio_request(GPIO_ISP_RESET, "ISP_RESET");
	if (ret) {
		printk(KERN_ERR "faile to request gpio(ISP_RESET)\n");
		return ret;
	}
	ret = gpio_request(GPIO_CAM_IO_EN, "GPE2");
	if (ret) {
		printk(KERN_ERR "fail to request gpio(GPIO_CAM_IO_EN)\n");
		return ret;
	}
	ret = gpio_request(GPIO_CAM_SENSOR_CORE, "GPE2");
	if (ret) {
		printk(KERN_ERR "fail to request gpio(CAM_SENSOR_COR)\n");
		return ret;
	}
	ret = gpio_request(GPIO_VT_CAM_15V, "GPE2");
	if (ret) {
		printk(KERN_ERR "faile to request gpio(GPIO_VT_CAM_15V)\n");
		return ret;
	}

	/* s3c_i2c0_force_stop(); */

	mdelay(3);

	/* ISP_RESET */
	ret = gpio_direction_output(GPIO_ISP_RESET, 0);
	CAM_CHECK_ERR(ret, "output reset");
#ifdef CONFIG_TARGET_LOCALE_KOR
	mdelay(3);		/* fix without seeing signal form for kor. */
#else
	mdelay(2);
#endif

	/* MCLK */
	ret = s3c_gpio_cfgpin(GPIO_CAM_MCLK, S3C_GPIO_INPUT);
	s3c_gpio_setpull(GPIO_CAM_MCLK, S3C_GPIO_PULL_DOWN);
	CAM_CHECK_ERR(ret, "cfg mclk");
	udelay(20);

	/* CAM_AF_2.8V */
	/* 8M_AF_2.8V_EN */
	ret = gpio_direction_output(GPIO_8M_AF_EN, 0);
	CAM_CHECK_ERR(ret, "output AF");

	/* CAM_SENSOR_IO_1.8V */
	regulator = regulator_get(NULL, "cam_sensor_io");
	if (IS_ERR(regulator))
		return -ENODEV;
	if (regulator_is_enabled(regulator))
		ret = regulator_force_disable(regulator);
	regulator_put(regulator);
	CAM_CHECK_ERR(ret, "disable, sensor_io");
	udelay(10);

	/* CAM_ISP_1.8V */
	regulator = regulator_get(NULL, "cam_isp");
	if (IS_ERR(regulator))
		return -ENODEV;
	if (regulator_is_enabled(regulator))
		ret = regulator_force_disable(regulator);
	regulator_put(regulator);
	CAM_CHECK_ERR(ret, "disable cam_isp");
	udelay(500);		/* 100us -> 500us */

	/* VT_CAM_1.8V */
	regulator = regulator_get(NULL, "vt_cam_1.8v");
	if (IS_ERR(regulator))
		return -ENODEV;
	if (regulator_is_enabled(regulator))
		ret = regulator_disable(regulator);
	regulator_put(regulator);
	CAM_CHECK_ERR(ret, "disable vt_1.8v");
	udelay(250);		/* 10us -> 250us */

	/* VT_CORE_1.5V */
	ret = gpio_direction_output(GPIO_VT_CAM_15V, 0);
	CAM_CHECK_ERR(ret, "output VT_CAM_1.5V");
	udelay(300);		/*10 -> 300 us */

	/* CAM_SENSOR_A2.8V */
	ret = gpio_direction_output(GPIO_CAM_IO_EN, 0);
	CAM_CHECK_ERR(ret, "output IO_EN");
	udelay(800);

	/* CAM_SENSOR_CORE_1.2V */
	ret = gpio_direction_output(GPIO_CAM_SENSOR_CORE, 0);
	CAM_CHECK_ERR(ret, "output SENSOR_CORE");
	udelay(5);

	/* CAM_ISP_CORE_1.2V */
	regulator = regulator_get(NULL, "cam_isp_core");
	if (IS_ERR(regulator))
		return -ENODEV;
	if (regulator_is_enabled(regulator))
		ret = regulator_force_disable(regulator);
	regulator_put(regulator);
	CAM_CHECK_ERR(ret, "disable isp_core");

#if defined(CONFIG_MACH_Q1_BD)
	mdelay(250);
#endif

	gpio_free(GPIO_8M_AF_EN);
	gpio_free(GPIO_ISP_RESET);
	gpio_free(GPIO_CAM_IO_EN);
	gpio_free(GPIO_CAM_SENSOR_CORE);
	gpio_free(GPIO_VT_CAM_15V);

	return ret;
}

int s3c_csis_power(int enable)
{
	struct regulator *regulator;
	int ret = 0;
	printk(KERN_DEBUG "%s: in\n", __func__);

	/* mipi_1.1v ,mipi_1.8v are always powered-on.
	 * If they are off, we then power them on.
	 */
	if (enable) {
		/* VMIPI_1.1V */
		regulator = regulator_get(NULL, "vmipi_1.1v");
		if (IS_ERR(regulator))
			goto error_out;
		if (!regulator_is_enabled(regulator)) {
			printk(KERN_WARNING "%s: vmipi_1.1v is off. so ON\n",
			       __func__);
			ret = regulator_enable(regulator);
			CAM_CHECK_ERR(ret, "enable vmipi_1.1v");
		}
		regulator_put(regulator);

		/* VMIPI_1.8V */
		regulator = regulator_get(NULL, "vmipi_1.8v");
		if (IS_ERR(regulator))
			goto error_out;
		if (!regulator_is_enabled(regulator)) {
			printk(KERN_WARNING "%s: vmipi_1.8v is off. so ON\n",
			       __func__);
			ret = regulator_enable(regulator);
			CAM_CHECK_ERR(ret, "enable vmipi_1.8v");
		}
		regulator_put(regulator);
	}
	printk(KERN_DEBUG "%s: out\n", __func__);

	return 0;

error_out:
	printk(KERN_ERR "%s: ERROR: failed to check mipi-power\n", __func__);
	return 0;
}

#if defined(CONFIG_MACH_Q1_BD)
static bool is_torch;
#endif

static int m5mo_flash_power(int enable)
{
	struct regulator *flash = regulator_get(NULL, "led_flash");
	struct regulator *movie = regulator_get(NULL, "led_movie");

	if (enable) {

#if defined(CONFIG_MACH_Q1_BD)
		if (regulator_is_enabled(movie)) {
			printk(KERN_DEBUG "%s: m5mo_torch set~~~~", __func__);
			is_torch = true;
			goto torch_exit;
		}
		is_torch = false;
#endif
		regulator_set_current_limit(flash, 490000, 530000);
		regulator_enable(flash);
		regulator_set_current_limit(movie, 90000, 110000);
		regulator_enable(movie);
	} else {

#if defined(CONFIG_MACH_Q1_BD)
		if (is_torch)
			goto torch_exit;
#endif

		if (regulator_is_enabled(flash))
			regulator_disable(flash);
		if (regulator_is_enabled(movie))
			regulator_disable(movie);
	}
torch_exit:
	regulator_put(flash);
	regulator_put(movie);

	return 0;
}

static int m5mo_power(int enable)
{
	int ret = 0;

	printk(KERN_DEBUG "%s %s\n", __func__, enable ? "on" : "down");
	if (enable) {
		ret = m5mo_power_on();
		if (unlikely(ret))
			goto error_out;
	} else
		ret = m5mo_power_down();

	ret = s3c_csis_power(enable);
	m5mo_flash_power(enable);

error_out:
	return ret;
}

static int m5mo_config_isp_irq(void)
{
	s3c_gpio_cfgpin(GPIO_ISP_INT, S3C_GPIO_SFN(0xF));
	s3c_gpio_setpull(GPIO_ISP_INT, S3C_GPIO_PULL_NONE);
	return 0;
}

static struct m5mo_platform_data m5mo_plat = {
	.default_width = 640,	/* 1920 */
	.default_height = 480,	/* 1080 */
	.pixelformat = V4L2_PIX_FMT_UYVY,
	.freq = 24000000,
	.is_mipi = 1,
	.config_isp_irq = m5mo_config_isp_irq,
	.irq = IRQ_EINT(13),
};

static struct i2c_board_info m5mo_i2c_info = {
	I2C_BOARD_INFO("M5MO", 0x1F),
	.platform_data = &m5mo_plat,
};

static struct s3c_platform_camera m5mo = {
	.id = CAMERA_CSI_C,
	.clk_name = "sclk_cam0",
	.get_i2c_busnum = m5mo_get_i2c_busnum,
	.cam_power = m5mo_power,	/*smdkv310_mipi_cam0_reset, */
	.type = CAM_TYPE_MIPI,
	.fmt = ITU_601_YCBCR422_8BIT,	/*MIPI_CSI_YCBCR422_8BIT */
	.order422 = CAM_ORDER422_8BIT_CBYCRY,
	.info = &m5mo_i2c_info,
	.pixelformat = V4L2_PIX_FMT_UYVY,
	.srclk_name = "xusbxti",	/* "mout_mpll" */
	.clk_rate = 24000000,	/* 48000000 */
	.line_length = 1920,
	.width = 640,
	.height = 480,
	.window = {
		.left = 0,
		.top = 0,
		.width = 640,
		.height = 480,
	},

	.mipi_lanes = 2,
	.mipi_settle = 12,
	.mipi_align = 32,

	/* Polarity */
	.inv_pclk = 0,
	.inv_vsync = 1,
	.inv_href = 0,
	.inv_hsync = 0,
	.reset_camera = 0,
	.initialized = 0,
};
#endif				/* #ifdef CONFIG_VIDEO_M5MO */

#ifdef CONFIG_VIDEO_S5K5BAFX
static int s5k5bafx_get_i2c_busnum(void)
{
	return 12;
}

static int s5k5bafx_power_on(void)
{
	struct regulator *regulator;
	int ret = 0;

	/* printk("%s: in\n", __func__); */

	ret = gpio_request(GPIO_ISP_RESET, "ISP_RESET");
	if (ret) {
		printk(KERN_ERR "faile to request gpio(ISP_RESET)\n");
		return ret;
	}
	ret = gpio_request(GPIO_CAM_IO_EN, "GPE2");
	if (ret) {
		printk(KERN_ERR "faile to request gpio(GPIO_CAM_IO_EN)\n");
		return ret;
	}
	ret = gpio_request(GPIO_VT_CAM_15V, "GPE2");
	if (ret) {
		printk(KERN_ERR "faile to request gpio(GPIO_VT_CAM_15V)\n");
		return ret;
	}
	ret = gpio_request(GPIO_CAM_VGA_nSTBY, "GPL2");
	if (ret) {
		printk(KERN_ERR "faile to request gpio(GPIO_CAM_VGA_nSTBY)\n");
		return ret;
	}
	ret = gpio_request(GPIO_CAM_VGA_nRST, "GPL2");
	if (ret) {
		printk(KERN_ERR "faile to request gpio(GPIO_CAM_VGA_nRST)\n");
		return ret;
	}

#ifndef CONFIG_MACH_U1_KOR_LGT
	if (system_rev >= 9) {
#endif
		s3c_gpio_setpull(VT_CAM_SDA_18V, S3C_GPIO_PULL_NONE);
		s3c_gpio_setpull(VT_CAM_SCL_18V, S3C_GPIO_PULL_NONE);
#ifndef CONFIG_MACH_U1_KOR_LGT
	}
#endif

	/* ISP_RESET low */
	ret = gpio_direction_output(GPIO_ISP_RESET, 0);
	CAM_CHECK_ERR_RET(ret, "output reset");
	udelay(100);

	/* CAM_ISP_CORE_1.2V */
	regulator = regulator_get(NULL, "cam_isp_core");
	if (IS_ERR(regulator))
		return -ENODEV;
	ret = regulator_enable(regulator);
	regulator_put(regulator);
	CAM_CHECK_ERR_RET(ret, "enable isp_core");
	udelay(10);

	/* CAM_SENSOR_A2.8V */
	ret = gpio_direction_output(GPIO_CAM_IO_EN, 1);
	CAM_CHECK_ERR_RET(ret, "output io_en");
	udelay(300);		/* don't change me */

	/* VT_CORE_1.5V */
	ret = gpio_direction_output(GPIO_VT_CAM_15V, 1);
	CAM_CHECK_ERR_RET(ret, "output vt_15v");
	udelay(100);

	/* CAM_ISP_1.8V */
	regulator = regulator_get(NULL, "cam_isp");
	if (IS_ERR(regulator))
		return -ENODEV;
	ret = regulator_enable(regulator);
	regulator_put(regulator);
	CAM_CHECK_ERR_RET(ret, "enable cam_isp");
	udelay(10);

	/* VT_CAM_1.8V */
	regulator = regulator_get(NULL, "vt_cam_1.8v");
	if (IS_ERR(regulator))
		return -ENODEV;
	ret = regulator_enable(regulator);
	regulator_put(regulator);
	CAM_CHECK_ERR_RET(ret, "enable vt_1.8v");
	udelay(10);

	/* CAM_VGA_nSTBY */
	ret = gpio_direction_output(GPIO_CAM_VGA_nSTBY, 1);
	CAM_CHECK_ERR_RET(ret, "output VGA_nSTBY");
	udelay(50);

	/* Mclk */
	ret = s3c_gpio_cfgpin(GPIO_CAM_MCLK, S3C_GPIO_SFN(2));
	s3c_gpio_setpull(GPIO_CAM_MCLK, S3C_GPIO_PULL_NONE);
	CAM_CHECK_ERR_RET(ret, "cfg mclk");
	udelay(100);

	/* CAM_VGA_nRST	 */
	ret = gpio_direction_output(GPIO_CAM_VGA_nRST, 1);
	CAM_CHECK_ERR_RET(ret, "output VGA_nRST");
	mdelay(2);

	gpio_free(GPIO_ISP_RESET);
	gpio_free(GPIO_CAM_IO_EN);
	gpio_free(GPIO_VT_CAM_15V);
	gpio_free(GPIO_CAM_VGA_nSTBY);
	gpio_free(GPIO_CAM_VGA_nRST);

	return 0;
}

static int s5k5bafx_power_off(void)
{
	struct regulator *regulator;
	int ret = 0;

	/* printk("n%s: in\n", __func__); */

	ret = gpio_request(GPIO_CAM_VGA_nRST, "GPL2");
	if (ret) {
		printk(KERN_ERR "faile to request gpio(GPIO_CAM_VGA_nRST)\n");
		return ret;
	}
	ret = gpio_request(GPIO_CAM_VGA_nSTBY, "GPL2");
	if (ret) {
		printk(KERN_ERR "faile to request gpio(GPIO_CAM_VGA_nSTBY)\n");
		return ret;
	}
	ret = gpio_request(GPIO_VT_CAM_15V, "GPE2");
	if (ret) {
		printk(KERN_ERR "faile to request gpio(GPIO_VT_CAM_15V)\n");
		return ret;
	}
	ret = gpio_request(GPIO_CAM_IO_EN, "GPE2");
	if (ret) {
		printk(KERN_ERR "faile to request gpio(GPIO_CAM_IO_EN)\n");
		return ret;
	}

	/* CAM_VGA_nRST	 */
	ret = gpio_direction_output(GPIO_CAM_VGA_nRST, 0);
	CAM_CHECK_ERR(ret, "output VGA_nRST");
	udelay(100);

	/* Mclk */
	ret = s3c_gpio_cfgpin(GPIO_CAM_MCLK, S3C_GPIO_INPUT);
	s3c_gpio_setpull(GPIO_CAM_MCLK, S3C_GPIO_PULL_DOWN);
	CAM_CHECK_ERR(ret, "cfg mclk");
	udelay(20);

	/* CAM_VGA_nSTBY */
	ret = gpio_direction_output(GPIO_CAM_VGA_nSTBY, 0);
	CAM_CHECK_ERR(ret, "output VGA_nSTBY");
	udelay(20);

	/* VT_CAM_1.8V */
	regulator = regulator_get(NULL, "vt_cam_1.8v");
	if (IS_ERR(regulator))
		return -ENODEV;
	if (regulator_is_enabled(regulator))
		ret = regulator_disable(regulator);
	regulator_put(regulator);
	CAM_CHECK_ERR(ret, "disable vt_1.8v");
	udelay(10);

	/* CAM_ISP_1.8V */
	regulator = regulator_get(NULL, "cam_isp");
	if (IS_ERR(regulator))
		return -ENODEV;
	if (regulator_is_enabled(regulator))
		ret = regulator_force_disable(regulator);
	regulator_put(regulator);
	CAM_CHECK_ERR(ret, "disable cam_isp");
	udelay(10);

	/* VT_CORE_1.5V */
	ret = gpio_direction_output(GPIO_VT_CAM_15V, 0);
	CAM_CHECK_ERR(ret, "output vt_1.5v");
	udelay(10);

	/* CAM_SENSOR_A2.8V */
	ret = gpio_direction_output(GPIO_CAM_IO_EN, 0);
	CAM_CHECK_ERR(ret, "output io_en");
	udelay(10);

	/* CAM_ISP_CORE_1.2V */
	regulator = regulator_get(NULL, "cam_isp_core");
	if (IS_ERR(regulator))
		return -ENODEV;
	if (regulator_is_enabled(regulator))
		ret = regulator_force_disable(regulator);
	regulator_put(regulator);
	CAM_CHECK_ERR(ret, "disable isp_core");

#ifndef CONFIG_MACH_U1_KOR_LGT
	if (system_rev >= 9) {
#endif
		gpio_direction_input(VT_CAM_SDA_18V);
		s3c_gpio_setpull(VT_CAM_SDA_18V, S3C_GPIO_PULL_DOWN);
		gpio_direction_input(VT_CAM_SCL_18V);
		s3c_gpio_setpull(VT_CAM_SCL_18V, S3C_GPIO_PULL_DOWN);
#ifndef CONFIG_MACH_U1_KOR_LGT
	}
#endif

#if defined(CONFIG_MACH_Q1_BD)
	mdelay(350);
#endif

	gpio_free(GPIO_CAM_VGA_nRST);
	gpio_free(GPIO_CAM_VGA_nSTBY);
	gpio_free(GPIO_VT_CAM_15V);
	gpio_free(GPIO_CAM_IO_EN);

	return 0;
}

static int s5k5bafx_power(int onoff)
{
	int ret = 0;
#if defined(CONFIG_MACH_U1_KOR_LGT)
	u32 cfg = 0;
#endif
	printk(KERN_INFO "%s(): %s\n", __func__, onoff ? "on" : "down");

#if defined(CONFIG_MACH_U1_KOR_LGT)
	cfg = readl(S5P_VA_GPIO2 + 0x002c);
#endif

	if (onoff) {

#if defined(CONFIG_MACH_U1_KOR_LGT)
		writel(cfg | 0x0080, S5P_VA_GPIO2 + 0x002c);
#endif

		ret = s5k5bafx_power_on();
		if (unlikely(ret))
			goto error_out;
	} else {

#if defined(CONFIG_MACH_U1_KOR_LGT)
		writel(cfg & 0xff3f, S5P_VA_GPIO2 + 0x002c);
#endif
		ret = s5k5bafx_power_off();
		/* s3c_i2c0_force_stop(); *//* DSLIM. Should be implemented */
	}

	ret = s3c_csis_power(onoff);

error_out:
	return ret;
}

static struct s5k5bafx_platform_data s5k5bafx_plat = {
	.default_width = 640,
	.default_height = 480,
	.pixelformat = V4L2_PIX_FMT_UYVY,
	.freq = 24000000,
	.is_mipi = 1,
};

static struct i2c_board_info s5k5bafx_i2c_info = {
	I2C_BOARD_INFO("S5K5BAFX", 0x5A >> 1),
	.platform_data = &s5k5bafx_plat,
};

static struct s3c_platform_camera s5k5bafx = {
	.id = CAMERA_CSI_D,
	.type = CAM_TYPE_MIPI,
	.fmt = ITU_601_YCBCR422_8BIT,
	.order422 = CAM_ORDER422_8BIT_CBYCRY,
	.mipi_lanes = 1,
	.mipi_settle = 6,
	.mipi_align = 32,

	.get_i2c_busnum = s5k5bafx_get_i2c_busnum,
	.info = &s5k5bafx_i2c_info,
	.pixelformat = V4L2_PIX_FMT_UYVY,
	.srclk_name = "xusbxti",
	.clk_name = "sclk_cam0",
	.clk_rate = 24000000,
	.line_length = 640,
	.width = 640,
	.height = 480,
	.window = {
		.left = 0,
		.top = 0,
		.width = 640,
		.height = 480,
	},

	/* Polarity */
	.inv_pclk = 0,
	.inv_vsync = 1,
	.inv_href = 0,
	.inv_hsync = 0,
	.reset_camera = 0,
	.initialized = 0,
	.cam_power = s5k5bafx_power,
};
#endif

#ifdef CONFIG_WRITEBACK_ENABLED
#define WRITEBACK_ENABLED
#endif
#ifdef WRITEBACK_ENABLED
static int get_i2c_busnum_writeback(void)
{
	return 0;
}

static struct i2c_board_info writeback_i2c_info = {
	I2C_BOARD_INFO("WriteBack", 0x0),
};

static struct s3c_platform_camera writeback = {
	.id = CAMERA_WB,
	.fmt = ITU_601_YCBCR422_8BIT,
	.order422 = CAM_ORDER422_8BIT_CBYCRY,
	.get_i2c_busnum = get_i2c_busnum_writeback,
	.info = &writeback_i2c_info,
	.pixelformat = V4L2_PIX_FMT_YUV444,
	.line_length = 800,
	.width = 480,
	.height = 800,
	.window = {
		.left = 0,
		.top = 0,
		.width = 480,
		.height = 800,
	},

	.initialized = 0,
};
#endif

void cam_cfg_gpio(struct platform_device *pdev)
{
	int ret = 0;
	printk(KERN_INFO "\n\n\n%s: pdev->id=%d\n", __func__, pdev->id);

	if (pdev->id != 0)
		return;

#ifdef CONFIG_VIDEO_S5K5BAFX
#ifndef CONFIG_MACH_U1_KOR_LGT
	if (system_rev >= 9) {
#endif
		/* Rev0.9 */
		ret = gpio_direction_input(VT_CAM_SDA_18V);
		CAM_CHECK_ERR(ret, "VT_CAM_SDA_18V");
		s3c_gpio_setpull(VT_CAM_SDA_18V, S3C_GPIO_PULL_DOWN);

		ret = gpio_direction_input(VT_CAM_SCL_18V);
		CAM_CHECK_ERR(ret, "VT_CAM_SCL_18V");
		s3c_gpio_setpull(VT_CAM_SCL_18V, S3C_GPIO_PULL_DOWN);
#ifndef CONFIG_MACH_U1_KOR_LGT
	}
#endif
#endif
}

/* Interface setting */
static struct s3c_platform_fimc fimc_plat = {
#ifdef CONFIG_ITU_A
	.default_cam = CAMERA_PAR_A,
#endif
#ifdef CONFIG_ITU_B
	.default_cam = CAMERA_PAR_B,
#endif
#ifdef CONFIG_CSI_C
	.default_cam = CAMERA_CSI_C,
#endif
#ifdef CONFIG_CSI_D
	.default_cam = CAMERA_CSI_D,
#endif
	.camera = {
#ifdef CONFIG_VIDEO_M5MO
		&m5mo,
#endif
#ifdef CONFIG_VIDEO_S5K5BAFX
		&s5k5bafx,
#endif
#ifdef WRITEBACK_ENABLED
		&writeback,
#endif
	},
	.hw_ver = 0x51,
	.cfg_gpio = cam_cfg_gpio,
};
#endif				/* CONFIG_VIDEO_FIMC */

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
	pr_info("---------test logs pdev : %p s3c_device_hsmmc3 %p\n",
		pdev, &s3c_device_hsmmc3);
	if (pdev == &s3c_device_hsmmc3) {
		notify_func = hsmmc3_notify_func;
		pr_info("---------test logs notify_func : %p\n", notify_func);
	}
#endif

	if (notify_func)
		notify_func(pdev, 1);
	else
		pr_warn("%s: called for device with no notifier\n", __func__);
	mutex_unlock(&notify_lock);
}
EXPORT_SYMBOL_GPL(mmc_force_presence_change);

#ifdef CONFIG_S3C_DEV_HSMMC
static struct s3c_sdhci_platdata exynos4_hsmmc0_pdata __initdata = {
	.cd_type = S3C_SDHCI_CD_PERMANENT,
	.clk_type = S3C_SDHCI_CLK_DIV_EXTERNAL,
#ifdef CONFIG_EXYNOS4_SDHCI_CH0_8BIT
	.max_width = 8,
	.host_caps = MMC_CAP_8_BIT_DATA,
#endif
};
#endif

#ifdef CONFIG_S3C_DEV_HSMMC2
static struct s3c_sdhci_platdata exynos4_hsmmc2_pdata __initdata = {
	.cd_type = S3C_SDHCI_CD_GPIO,
	.ext_cd_gpio = EXYNOS4_GPX3(4),
	.ext_cd_gpio_invert = 1,
	.clk_type = S3C_SDHCI_CLK_DIV_EXTERNAL,
	.vmmc_name = "vtf_2.8v",
};
#endif

#ifdef CONFIG_S3C_DEV_HSMMC3
static struct s3c_sdhci_platdata exynos4_hsmmc3_pdata __initdata = {
/* For Wi-Fi */
#if 0
	.cd_type = S3C_SDHCI_CD_PERMANENT,
	.clk_type = S3C_SDHCI_CLK_DIV_EXTERNAL,
	.pm_flags = S3C_SDHCI_PM_IGNORE_SUSPEND_RESUME,
#else
	.cd_type = S3C_SDHCI_CD_EXTERNAL,
	.clk_type = S3C_SDHCI_CLK_DIV_EXTERNAL,
	.pm_flags = S3C_SDHCI_PM_IGNORE_SUSPEND_RESUME,
	.ext_cd_init = ext_cd_init_hsmmc3,
	.ext_cd_cleanup = ext_cd_cleanup_hsmmc3,
#endif
};
#endif

#ifdef CONFIG_EXYNOS4_DEV_MSHC
static struct s3c_mshci_platdata exynos4_mshc_pdata __initdata = {
	.cd_type = S3C_MSHCI_CD_PERMANENT,
#if defined(CONFIG_EXYNOS4_MSHC_8BIT) &&	\
	defined(CONFIG_EXYNOS4_MSHC_DDR)
	.max_width = 8,
	.host_caps = MMC_CAP_8_BIT_DATA | MMC_CAP_1_8V_DDR |
			MMC_CAP_UHS_DDR50 | MMC_CAP_CMD23,
#elif defined(CONFIG_EXYNOS4_MSHC_8BIT)
	.max_width = 8,
	.host_caps = MMC_CAP_8_BIT_DATA | MMC_CAP_CMD23,
#elif defined(CONFIG_EXYNOS4_MSHC_DDR)
	.host_caps = MMC_CAP_1_8V_DDR | MMC_CAP_UHS_DDR50 |
			MMC_CAP_CMD23,
#endif
	.int_power_gpio		= GPIO_XMMC0_CDn,
};
#endif

#ifdef CONFIG_S3C64XX_DEV_SPI
static struct s3c64xx_spi_csinfo spi0_csi[] = {
	[0] = {
		.line = EXYNOS4_GPB(1),
		.set_level = gpio_set_value,
		.fb_delay = 0x0,
	},
};

static struct spi_board_info spi0_board_info[] __initdata = {
#if defined(CONFIG_TDMB) || defined(CONFIG_TDMB_MODULE)
	{
		.modalias = "tdmbspi",
		.platform_data = NULL,
		.max_speed_hz = 5000000,
		.bus_num = 0,
		.chip_select = 0,
		.mode = SPI_MODE_0,
		.controller_data = &spi0_csi[0],
	},
#elif defined(CONFIG_ISDBT_FC8100)
	{
		.modalias	=	"isdbtspi",
		.platform_data	=	NULL,
		.max_speed_hz	=	400000,
		.bus_num	=	0,
		.chip_select	=	0,
		.mode		=	(SPI_MODE_0|SPI_CS_HIGH),
		.controller_data	=	&spi0_csi[0],
	},

#elif defined(CONFIG_PHONE_IPC_SPI)
	{
		.modalias = "ipc_spi",
		.bus_num = 0,
		.chip_select = 0,
		.max_speed_hz = 12*1000*1000,
		.mode = SPI_MODE_1,
		.controller_data = &spi0_csi[0],
	},
#else
	{
		.modalias = "spidev",
		.platform_data = NULL,
		.max_speed_hz = 10 * 1000 * 1000,
		.bus_num = 0,
		.chip_select = 0,
		.mode = SPI_MODE_0,
		.controller_data = &spi0_csi[0],
	}
#endif
};
#endif

#if defined(CONFIG_PHONE_IPC_SPI)
static void ipc_spi_cfg_gpio(void);

static struct ipc_spi_platform_data ipc_spi_data = {
	.gpio_ipc_mrdy = GPIO_IPC_MRDY,
	.gpio_ipc_srdy = GPIO_IPC_SRDY,
	.gpio_ipc_sub_mrdy = GPIO_IPC_SUB_MRDY,
	.gpio_ipc_sub_srdy = GPIO_IPC_SUB_SRDY,

	.cfg_gpio = ipc_spi_cfg_gpio,
};

static struct resource ipc_spi_res[] = {
	[0] = {
		.start = IRQ_IPC_SRDY,
		.end = IRQ_IPC_SRDY,
		.flags = IORESOURCE_IRQ,
	},
};

static struct platform_device ipc_spi_device = {
	.name = "onedram",
	.id = -1,
	.num_resources = ARRAY_SIZE(ipc_spi_res),
	.resource = ipc_spi_res,
	.dev = {
		.platform_data = &ipc_spi_data,
	},
};

static void ipc_spi_cfg_gpio(void)
{
	int err = 0;

	unsigned gpio_ipc_mrdy = ipc_spi_data.gpio_ipc_mrdy;
	unsigned gpio_ipc_srdy = ipc_spi_data.gpio_ipc_srdy;
	unsigned gpio_ipc_sub_mrdy = ipc_spi_data.gpio_ipc_sub_mrdy;
	unsigned gpio_ipc_sub_srdy = ipc_spi_data.gpio_ipc_sub_srdy;

	err = gpio_request(gpio_ipc_mrdy, "IPC_MRDY");
	if (err) {
		printk(KERN_ERR "ipc_spi_cfg_gpio - fail to request gpio %s : %d\n",
			"IPC_MRDY", err);
	} else {
		gpio_direction_output(gpio_ipc_mrdy, 0);
		s3c_gpio_setpull(gpio_ipc_mrdy, S3C_GPIO_PULL_DOWN);
	}

	err = gpio_request(gpio_ipc_srdy, "IPC_SRDY");
	if (err) {
		printk(KERN_ERR "ipc_spi_cfg_gpio - fail to request gpio %s : %d\n",
			"IPC_SRDY", err);
	} else {
		gpio_direction_input(gpio_ipc_srdy);
		s3c_gpio_cfgpin(gpio_ipc_srdy, S3C_GPIO_SFN(0xF));
		s3c_gpio_setpull(gpio_ipc_srdy, S3C_GPIO_PULL_NONE);
	}

	err = gpio_request(gpio_ipc_sub_mrdy, "IPC_SUB_MRDY");
	if (err) {
		printk(KERN_ERR "ipc_spi_cfg_gpio - fail to request gpio %s : %d\n",
			"IPC_SUB_MRDY", err);
	} else {
		gpio_direction_output(gpio_ipc_sub_mrdy, 0);
		s3c_gpio_setpull(gpio_ipc_sub_mrdy, S3C_GPIO_PULL_DOWN);
	}

	err = gpio_request(gpio_ipc_sub_srdy, "IPC_SUB_SRDY");
	if (err) {
		printk(KERN_ERR "ipc_spi_cfg_gpio - fail to request gpio %s : %d\n",
			"IPC_SUB_SRDY", err);
	} else {
		gpio_direction_input(gpio_ipc_sub_srdy);
		s3c_gpio_cfgpin(gpio_ipc_sub_srdy, S3C_GPIO_SFN(0xF));
		s3c_gpio_setpull(gpio_ipc_sub_srdy, S3C_GPIO_PULL_NONE);
	}

	irq_set_irq_type(gpio_to_irq(GPIO_IPC_SRDY), IRQ_TYPE_EDGE_RISING);
	irq_set_irq_type(gpio_to_irq(GPIO_IPC_SUB_SRDY), IRQ_TYPE_EDGE_RISING);
}
#endif

#ifdef CONFIG_CPU_FREQ_GOV_ONDEMAND_FLEXRATE
static void flexrate_work(struct work_struct *work)
{
	cpufreq_ondemand_flexrate_request(10000, 10);
}
static DECLARE_WORK(flex_work, flexrate_work);
#endif

#include <linux/pm_qos_params.h>
static struct pm_qos_request_list busfreq_qos;
static void flexrate_qos_cancel(struct work_struct *work)
{
	pm_qos_update_request(&busfreq_qos, 0);
}

static DECLARE_DELAYED_WORK(busqos_work, flexrate_qos_cancel);

void tsp_request_qos(void *data)
{
#ifdef CONFIG_CPU_FREQ_GOV_ONDEMAND_FLEXRATE
	if (!work_pending(&flex_work))
		schedule_work_on(0, &flex_work);
#endif
	/* Guarantee that the bus runs at >= 266MHz */
	if (!pm_qos_request_active(&busfreq_qos))
		pm_qos_add_request(&busfreq_qos, PM_QOS_BUS_DMA_THROUGHPUT,
				   266000);
	else {
		cancel_delayed_work_sync(&busqos_work);
		pm_qos_update_request(&busfreq_qos, 266000);
	}

	/* Cancel the QoS request after 1/10 sec */
	schedule_delayed_work_on(0, &busqos_work, HZ / 5);
}

#ifdef CONFIG_TOUCHSCREEN_MELFAS_MMS
static int melfas_mms_power(int on)
{
	if (on) {
		gpio_request(GPIO_TSP_LDO_ON, "TSP_LDO_ON");
		s3c_gpio_cfgpin(GPIO_TSP_LDO_ON, S3C_GPIO_OUTPUT);
		s3c_gpio_setpull(GPIO_TSP_LDO_ON, S3C_GPIO_PULL_NONE);
		gpio_set_value(GPIO_TSP_LDO_ON, GPIO_LEVEL_HIGH);

		mdelay(70);
		gpio_request(GPIO_TSP_INT, "TSP_INT");
		s3c_gpio_setpull(GPIO_TSP_INT, S3C_GPIO_PULL_NONE);
		s3c_gpio_cfgpin(GPIO_TSP_INT, S3C_GPIO_SFN(0xf));

		printk(KERN_INFO "[TSP]melfas power on\n");
		return 0;
	} else {
		gpio_request(GPIO_TSP_INT, "TSP_INT");
		s3c_gpio_cfgpin(GPIO_TSP_INT, S3C_GPIO_INPUT);
		s3c_gpio_setpull(GPIO_TSP_INT, S3C_GPIO_PULL_DOWN);

		gpio_request(GPIO_TSP_LDO_ON, "TSP_LDO_ON");
		s3c_gpio_cfgpin(GPIO_TSP_LDO_ON, S3C_GPIO_OUTPUT);
		s3c_gpio_setpull(GPIO_TSP_LDO_ON, S3C_GPIO_PULL_NONE);
		gpio_set_value(GPIO_TSP_LDO_ON, GPIO_LEVEL_LOW);

		printk(KERN_INFO "[TSP]melfas power on\n");
		return 0;
	}
}

static int melfas_mms_mux_fw_flash(bool to_gpios)
{
	pr_info("%s:to_gpios=%d\n", __func__, to_gpios);

	/* TOUCH_EN is always an output */
	if (to_gpios) {
		if (gpio_request(GPIO_TSP_SCL, "GPIO_TSP_SCL"))
			pr_err("failed to request gpio(GPIO_TSP_SCL)\n");
		if (gpio_request(GPIO_TSP_SDA, "GPIO_TSP_SDA"))
			pr_err("failed to request gpio(GPIO_TSP_SDA)\n");

		gpio_direction_output(GPIO_TSP_INT, 0);
		s3c_gpio_cfgpin(GPIO_TSP_INT, S3C_GPIO_OUTPUT);
		s3c_gpio_setpull(GPIO_TSP_INT, S3C_GPIO_PULL_NONE);

		gpio_direction_output(GPIO_TSP_SCL, 0);
		s3c_gpio_cfgpin(GPIO_TSP_SCL, S3C_GPIO_OUTPUT);
		s3c_gpio_setpull(GPIO_TSP_SCL, S3C_GPIO_PULL_NONE);

		gpio_direction_output(GPIO_TSP_SDA, 0);
		s3c_gpio_cfgpin(GPIO_TSP_SDA, S3C_GPIO_OUTPUT);
		s3c_gpio_setpull(GPIO_TSP_SDA, S3C_GPIO_PULL_NONE);

	} else {
		gpio_direction_output(GPIO_TSP_INT, 1);
		gpio_direction_input(GPIO_TSP_INT);
		s3c_gpio_cfgpin(GPIO_TSP_INT, S3C_GPIO_SFN(0xf));
		/*s3c_gpio_cfgpin(GPIO_TSP_INT, S3C_GPIO_INPUT); */
		s3c_gpio_setpull(GPIO_TSP_INT, S3C_GPIO_PULL_NONE);
		/*S3C_GPIO_PULL_UP */

		gpio_direction_output(GPIO_TSP_SCL, 1);
		gpio_direction_input(GPIO_TSP_SCL);
		s3c_gpio_cfgpin(GPIO_TSP_SCL, S3C_GPIO_SFN(3));
		s3c_gpio_setpull(GPIO_TSP_SCL, S3C_GPIO_PULL_NONE);

		gpio_direction_output(GPIO_TSP_SDA, 1);
		gpio_direction_input(GPIO_TSP_SDA);
		s3c_gpio_cfgpin(GPIO_TSP_SDA, S3C_GPIO_SFN(3));
		s3c_gpio_setpull(GPIO_TSP_SDA, S3C_GPIO_PULL_NONE);

		gpio_free(GPIO_TSP_SCL);
		gpio_free(GPIO_TSP_SDA);
	}
	return 0;
}

static int is_melfas_mms_vdd_on(void)
{
	int ret;
	/* 3.3V */
	static struct regulator *regulator;

	if (!regulator) {
		regulator = regulator_get(NULL, "touch");
		if (IS_ERR(regulator)) {
			ret = PTR_ERR(regulator);
			pr_err("could not get touch, rc = %d\n", ret);
			return ret;
		}
	}

	if (regulator_is_enabled(regulator))
		return 1;
	else
		return 0;
}

struct tsp_callbacks *charger_callbacks;
struct tsp_callbacks {
	void (*inform_charger)(struct tsp_callbacks *, bool);
};

static void tsp_charger_infom(bool en)
{
	if (charger_callbacks && charger_callbacks->inform_charger)
		charger_callbacks->inform_charger(charger_callbacks, en);
}

static void melfas_register_callback(void *cb)
{
	charger_callbacks = cb;
	pr_debug("[TSP] melfas_register_callback\n");
}

static struct melfas_mms_platform_data melfas_mms_ts_pdata = {
	.max_x = 720,
	.max_y = 1280,
	.invert_x = 0,
	.invert_y = 0,
	.gpio_int = GPIO_TSP_INT,
	.gpio_scl = GPIO_TSP_SCL,
	.gpio_sda = GPIO_TSP_SDA,
	.power = melfas_mms_power,
	.mux_fw_flash = melfas_mms_mux_fw_flash,
	.is_vdd_on = is_melfas_mms_vdd_on,
	.input_event = tsp_request_qos,
	.register_cb = melfas_register_callback,
};

static struct melfas_mms_platform_data melfas_mms_ts_pdata_rotate = {
	.max_x = 720,
	.max_y = 1280,
	.invert_x = 720,
	.invert_y = 1280,
	.gpio_int = GPIO_TSP_INT,
	.gpio_scl = GPIO_TSP_SCL,
	.gpio_sda = GPIO_TSP_SDA,
	.power = melfas_mms_power,
	.mux_fw_flash = melfas_mms_mux_fw_flash,
	.is_vdd_on = is_melfas_mms_vdd_on,
	.input_event = tsp_request_qos,
	.register_cb = melfas_register_callback,
};

#endif

#ifdef CONFIG_DRM_EXYNOS
static struct resource exynos_drm_resource[] = {
	[0] = {
		.start = IRQ_FIMD0_VSYNC,
		.end   = IRQ_FIMD0_VSYNC,
		.flags = IORESOURCE_IRQ,
	},
};

static struct platform_device exynos_drm_device = {
	.name	= "exynos-drm",
	.id	= -1,
	.num_resources	  = ARRAY_SIZE(exynos_drm_resource),
	.resource	  = exynos_drm_resource,
	.dev	= {
		.dma_mask = &exynos_drm_device.dev.coherent_dma_mask,
		.coherent_dma_mask = 0xffffffffUL,
	}
};
#endif

#ifdef CONFIG_DRM_EXYNOS_FIMD
static struct exynos_drm_fimd_pdata drm_fimd_pdata = {
	.panel = {
		.timing	= {
			.xres		= 720,
			.yres		= 1280,
			.hsync_len	= 5,
			.left_margin	= 5,
			.right_margin	= 5,
			.vsync_len	= 2,
			.upper_margin	= 1,
			.lower_margin	= 13,
			.refresh	= 60,
		},
		.width_mm	= 58,
		.height_mm	= 103,
	},
	.vidcon0		= VIDCON0_VIDOUT_RGB | VIDCON0_PNRMODE_RGB,
	.vidcon1		= VIDCON1_INV_VCLK,
	.default_win		= 3,
	.bpp			= 32,
	.dynamic_refresh	= 0,
	.high_freq		= 1,
};

#ifdef CONFIG_MDNIE_SUPPORT
static struct resource exynos4_fimd_lite_resource[] = {
	[0] = {
		.start	= EXYNOS4_PA_LCD_LITE0,
		.end	= EXYNOS4_PA_LCD_LITE0 + S5P_SZ_LCD_LITE0 - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= IRQ_LCD_LITE0,
		.end	= IRQ_LCD_LITE0,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct resource exynos4_mdnie_resource[] = {
	[0] = {
		.start	= EXYNOS4_PA_MDNIE0,
		.end	= EXYNOS4_PA_MDNIE0 + S5P_SZ_MDNIE0 - 1,
		.flags	= IORESOURCE_MEM,
	},
};

static struct mdnie_platform_data exynos4_mdnie_pdata = {
	.width			= 720,
	.height			= 1280,
};

static struct s5p_fimd_ext_device exynos4_fimd_lite_device = {
	.name			= "fimd_lite",
	.id			= -1,
	.num_resources		= ARRAY_SIZE(exynos4_fimd_lite_resource),
	.resource		= exynos4_fimd_lite_resource,
	.dev			= {
		.platform_data	= &drm_fimd_pdata,
	},
};

static struct s5p_fimd_ext_device exynos4_mdnie_device = {
	.name			= "mdnie",
	.id			= -1,
	.num_resources		= ARRAY_SIZE(exynos4_mdnie_resource),
	.resource		= exynos4_mdnie_resource,
	.dev			= {
		.platform_data	= &exynos4_mdnie_pdata,
	},
};

/* FIXME:!! why init at this point ? */
int exynos4_common_setup_clock(const char *sclk_name, const char *pclk_name,
		unsigned long rate, unsigned int rate_set)
{
	struct clk *sclk = NULL;
	struct clk *pclk = NULL;

	sclk = clk_get(NULL, sclk_name);
	if (IS_ERR(sclk)) {
		printk(KERN_ERR "failed to get %s clock.\n", sclk_name);
		goto err_clk;
	}

	pclk = clk_get(NULL, pclk_name);
	if (IS_ERR(pclk)) {
		printk(KERN_ERR "failed to get %s clock.\n", pclk_name);
		goto err_clk;
	}

	clk_set_parent(sclk, pclk);

	printk(KERN_INFO "set parent clock of %s to %s\n", sclk_name,
			pclk_name);
	if (!rate_set)
		goto set_end;

	if (!rate)
		rate = 200 * MHZ;

	clk_set_rate(sclk, rate);

set_end:
	clk_put(sclk);
	clk_put(pclk);

	return 0;

err_clk:
	clk_put(sclk);
	clk_put(pclk);

	return -EINVAL;

}
#endif

static int reset_lcd(struct lcd_device *ld)
{
	static unsigned int first = 1;
	int reset_gpio = -1;

	reset_gpio = EXYNOS4_GPY4(5);

	if (first) {
		gpio_request(reset_gpio, "MLCD_RST");
		first = 0;
	}

	gpio_direction_output(reset_gpio, 1);
	usleep_range(1000, 2000);
	gpio_direction_output(reset_gpio, 0);
	usleep_range(1000, 2000);
	gpio_direction_output(reset_gpio, 1);

	dev_info(&ld->dev, "reset completed.\n");

	return 0;
}

static struct lcd_property s6e8aa0_property = {
	.flip = LCD_PROPERTY_FLIP_VERTICAL |
		LCD_PROPERTY_FLIP_HORIZONTAL,
	.dynamic_refresh = false,
};

static struct lcd_platform_data s6e8aa0_pdata = {
	.reset			= reset_lcd,
	.reset_delay		= 25,
	.power_off_delay	= 120,
	.power_on_delay	= 120,
	.lcd_enabled		= 1,
	.pdata	= &s6e8aa0_property,
};

static void lcd_cfg_gpio(void)
{
	int i, f3_end = 4;
	int reg;

	for (i = 0; i < 8; i++) {
		/* set GPF0,1,2[0:7] for RGB Interface and Data line (32bit) */
		s3c_gpio_cfgpin(EXYNOS4_GPF0(i), S3C_GPIO_SFN(2));
		s3c_gpio_setpull(EXYNOS4_GPF0(i), S3C_GPIO_PULL_NONE);

	}
	for (i = 0; i < 8; i++) {
		s3c_gpio_cfgpin(EXYNOS4_GPF1(i), S3C_GPIO_SFN(2));
		s3c_gpio_setpull(EXYNOS4_GPF1(i), S3C_GPIO_PULL_NONE);
	}

	for (i = 0; i < 8; i++) {
		s3c_gpio_cfgpin(EXYNOS4_GPF2(i), S3C_GPIO_SFN(2));
		s3c_gpio_setpull(EXYNOS4_GPF2(i), S3C_GPIO_PULL_NONE);
	}

	for (i = 0; i < f3_end; i++) {
		s3c_gpio_cfgpin(EXYNOS4_GPF3(i), S3C_GPIO_SFN(2));
		s3c_gpio_setpull(EXYNOS4_GPF3(i), S3C_GPIO_PULL_NONE);
	}

	reg = __raw_readl(S3C_VA_SYS + 0x210);
	reg |= 1 << 1;
	__raw_writel(reg, S3C_VA_SYS + 0x210);

	return;
}

#ifdef CONFIG_S5P_MIPI_DSI2
static struct mipi_dsim_config dsim_config = {
	.e_interface		= DSIM_VIDEO,
	.e_virtual_ch		= DSIM_VIRTUAL_CH_0,
	.e_pixel_format		= DSIM_24BPP_888,
	.e_burst_mode		= DSIM_BURST_SYNC_EVENT,
	.e_no_data_lane		= DSIM_DATA_LANE_4,
	.e_byte_clk		= DSIM_PLL_OUT_DIV8,
	.cmd_allow		= 0xf,

	/*
	 * ===========================================
	 * |    P    |    M    |    S    |    MHz    |
	 * -------------------------------------------
	 * |    3    |   100   |    3    |    100    |
	 * |    3    |   100   |    2    |    200    |
	 * |    3    |    63   |    1    |    252    |
	 * |    4    |   100   |    1    |    300    |
	 * |    4    |   110   |    1    |    330    |
	 * |   12    |   350   |    1    |    350    |
	 * |    3    |   100   |    1    |    400    |
	 * |    4    |   150   |    1    |    450    |
	 * |    3    |   120   |    1    |    480    |
	 * |   12    |   250   |    0    |    500    |
	 * |    4    |   100   |    0    |    600    |
	 * |    3    |    81   |    0    |    648    |
	 * |    3    |    88   |    0    |    704    |
	 * |    3    |    90   |    0    |    720    |
	 * |    3    |   100   |    0    |    800    |
	 * |   12    |   425   |    0    |    850    |
	 * |    4    |   150   |    0    |    900    |
	 * |   12    |   475   |    0    |    950    |
	 * |    6    |   250   |    0    |   1000    |
	 * -------------------------------------------
	 */

	.p			= 12,
	.m			= 250,
	.s			= 0,

	/* D-PHY PLL stable time spec :min = 200usec ~ max 400usec */
	.pll_stable_time	= 500,

	/* escape clk : 10MHz */
	.esc_clk		= 10 * 1000000,

	/* stop state holding counter after bta change count 0 ~ 0xfff */
	.stop_holding_cnt	= 0x7ff,
	/* bta timeout 0 ~ 0xff */
	.bta_timeout		= 0xff,
	/* lp rx timeout 0 ~ 0xffff */
	.rx_timeout		= 0xffff,
};

static struct s5p_platform_mipi_dsim dsim_platform_data = {
	/* already enabled at boot loader. FIXME!!! */
	.enabled		= true,
	.phy_enable		= s5p_dsim_phy_enable,
	.dsim_config		= &dsim_config,
};

static struct mipi_dsim_lcd_device mipi_lcd_device = {
	.name			= "s6e8aa0",
	.id			= -1,
	.bus_id			= 0,

	.platform_data		= (void *)&s6e8aa0_pdata,
};
#endif

static void __init trats_fb_init(void)
{
#ifdef CONFIG_S5P_MIPI_DSI2
	struct s5p_platform_mipi_dsim *dsim_pdata;

	dsim_pdata = (struct s5p_platform_mipi_dsim *)&dsim_platform_data;
	strcpy(dsim_pdata->lcd_panel_name, "s6e8aa0");
	dsim_pdata->lcd_panel_info = (void *)&drm_fimd_pdata.panel.timing;

	s5p_mipi_dsi_register_lcd_device(&mipi_lcd_device);
#ifdef CONFIG_MDNIE_SUPPORT
	s5p_fimd_ext_device_register(&exynos4_mdnie_device);
	s5p_fimd_ext_device_register(&exynos4_fimd_lite_device);
	exynos4_common_setup_clock("sclk_mdnie", "mout_mpll_user",
				400 * MHZ, 1);
#endif
	s5p_device_mipi_dsim0.dev.platform_data = (void *)&dsim_platform_data;
	platform_device_register(&s5p_device_mipi_dsim0);
#endif

	s5p_device_fimd0.dev.platform_data = &drm_fimd_pdata;
	lcd_cfg_gpio();
}

static unsigned long fbmem_start;
static int __init early_fbmem(char *p)
{
	char *endp;
	unsigned long size;

	if (!p)
		return -EINVAL;

	size = memparse(p, &endp);
	if (*endp == '@')
		fbmem_start = memparse(endp + 1, &endp);

	return endp > p ? 0 : -EINVAL;
}
early_param("fbmem", early_fbmem);
#endif

#ifdef CONFIG_DRM_EXYNOS_HDMI
/* I2C HDMIPHY */
static struct s3c2410_platform_i2c hdmiphy_i2c_data __initdata = {
	.bus_num	= 8,
	.flags		= 0,
	.slave_addr	= 0x10,
	.frequency	= 100*1000,
	.sda_delay	= 100,
};

static struct i2c_board_info i2c_hdmiphy_devs[] __initdata = {
	{
		/* hdmiphy */
		I2C_BOARD_INFO("s5p_hdmiphy", (0x70 >> 1)),
	},
};

static struct exynos_drm_hdmi_pdata drm_hdmi_pdata = {
	.is_v13 = true,
	.cfg_hpd	= s5p_hdmi_cfg_hpd,
	.get_hpd	= s5p_hdmi_get_hpd,
};

static struct exynos_drm_common_hdmi_pd drm_common_hdmi_pdata = {
	.hdmi_dev	= &s5p_device_hdmi.dev,
	.mixer_dev	= &s5p_device_mixer.dev,
};

static struct platform_device exynos_drm_hdmi_device = {
	.name	= "exynos-drm-hdmi",
	.dev	= {
		.platform_data = &drm_common_hdmi_pdata,
	},
};

static void trats_tv_init(void)
{
	/* HDMI PHY */
	s5p_i2c_hdmiphy_set_platdata(&hdmiphy_i2c_data);
	i2c_register_board_info(8, i2c_hdmiphy_devs,
				ARRAY_SIZE(i2c_hdmiphy_devs));

	gpio_request(GPIO_HDMI_HPD, "HDMI_HPD");
	gpio_direction_input(GPIO_HDMI_HPD);
	s3c_gpio_cfgpin(GPIO_HDMI_HPD, S3C_GPIO_SFN(0x3));
	s3c_gpio_setpull(GPIO_HDMI_HPD, S3C_GPIO_PULL_DOWN);

#ifdef CONFIG_EXYNOS_DEV_PD
	s5p_device_hdmi.dev.parent = &exynos4_device_pd[PD_TV].dev;
	s5p_device_mixer.dev.parent = &exynos4_device_pd[PD_TV].dev;
#endif
	s5p_device_hdmi.dev.platform_data = &drm_hdmi_pdata;
}

void mhl_hpd_handler(bool onoff)
{
	printk(KERN_INFO "hpd(%d)\n", onoff);
}
EXPORT_SYMBOL(mhl_hpd_handler);
#endif

static struct platform_device exynos_drm_vidi_device = {
	.name	= "exynos-drm-vidi",
};

static struct platform_device u1_regulator_consumer = {
	.name = "u1-regulator-consumer",
	.id = -1,
};

#ifdef CONFIG_REGULATOR_MAX8997
static struct regulator_consumer_supply ldo1_supply[] = {
	REGULATOR_SUPPLY("vadc_3.3v", NULL),
	REGULATOR_SUPPLY("vdd_osc", "exynos4-hdmi"),
};

static struct regulator_consumer_supply ldo3_supply[] = {
	REGULATOR_SUPPLY("vusb_1.1v", "usb_otg"),
	REGULATOR_SUPPLY("vmipi_1.1v", "m5mo"),
	REGULATOR_SUPPLY("vmipi_1.1v", NULL),
	REGULATOR_SUPPLY("VDD10", "s5p-mipi-dsim.0"),
	REGULATOR_SUPPLY("vdd", "exynos4-hdmi"),
	REGULATOR_SUPPLY("vdd_pll", "exynos4-hdmi"),
};

static struct regulator_consumer_supply ldo4_supply[] = {
	REGULATOR_SUPPLY("vmipi_1.8v", NULL),
	REGULATOR_SUPPLY("VDD18", "s5p-mipi-dsim.0"),
};

static struct regulator_consumer_supply ldo5_supply[] = {
	REGULATOR_SUPPLY("vhsic", NULL),
};

static struct regulator_consumer_supply ldo6_supply[] = {
	REGULATOR_SUPPLY("v_gps_1.8v", "gsd4t"),
};

static struct regulator_consumer_supply ldo7_supply[] = {
	REGULATOR_SUPPLY("cam_isp", NULL),
};

static struct regulator_consumer_supply ldo8_supply[] = {
	REGULATOR_SUPPLY("vusb_3.3v", NULL),
};

#if defined(CONFIG_S5PV310_HI_ARMCLK_THAN_1_2GHZ)
static struct regulator_consumer_supply ldo10_supply[] = {
	REGULATOR_SUPPLY("vpll_1.2v", NULL),
};
#else
static struct regulator_consumer_supply ldo10_supply[] = {
	REGULATOR_SUPPLY("vpll_1.1v", NULL),
};
#endif

static struct regulator_consumer_supply ldo11_supply[] = {
	REGULATOR_SUPPLY("touch", NULL),
};

static struct regulator_consumer_supply ldo12_supply[] = {
	REGULATOR_SUPPLY("vt_cam_1.8v", NULL),
};

static struct regulator_consumer_supply ldo13_supply[] = {
	REGULATOR_SUPPLY("vlcd_3.0v", NULL),
	REGULATOR_SUPPLY("VCI", "s6e8aa0"),
};

#ifdef CONFIG_MACH_Q1_BD
static struct regulator_consumer_supply ldo14_supply[] = {
	REGULATOR_SUPPLY("vlcd_2.2v", NULL),
};
#else
static struct regulator_consumer_supply ldo14_supply[] = {
	REGULATOR_SUPPLY("vmotor", NULL),
};
#endif

static struct regulator_consumer_supply ldo15_supply[] = {
	REGULATOR_SUPPLY("vlcd_2.2v", NULL),
	REGULATOR_SUPPLY("VDD3", "s6e8aa0"),
};

static struct regulator_consumer_supply ldo16_supply[] = {
	REGULATOR_SUPPLY("cam_sensor_io", NULL),
};

static struct regulator_consumer_supply ldo17_supply[] = {
	REGULATOR_SUPPLY("vtf_2.8v", NULL),
};

static struct regulator_consumer_supply ldo18_supply[] = {
	REGULATOR_SUPPLY("touch_led", NULL),
};


static struct regulator_consumer_supply ldo21_supply[] = {
	REGULATOR_SUPPLY("vddq_m1m2", NULL),
};

static struct regulator_consumer_supply buck1_supply[] = {
	REGULATOR_SUPPLY("vdd_arm", NULL),
};

static struct regulator_consumer_supply buck2_supply[] = {
	REGULATOR_SUPPLY("vdd_int", NULL),
};

static struct regulator_consumer_supply buck3_supply[] = {
	REGULATOR_SUPPLY("vdd_g3d", NULL),
};

static struct regulator_consumer_supply buck4_supply[] = {
	REGULATOR_SUPPLY("cam_isp_core", NULL),
};

static struct regulator_consumer_supply buck7_supply[] = {
	REGULATOR_SUPPLY("vcc_sub", NULL),
};

static struct regulator_consumer_supply safeout1_supply[] = {
	REGULATOR_SUPPLY("safeout1", NULL),
};

static struct regulator_consumer_supply safeout2_supply[] = {
	REGULATOR_SUPPLY("safeout2", NULL),
};

static struct regulator_consumer_supply led_flash_supply[] = {
	REGULATOR_SUPPLY("led_flash", NULL),
};

static struct regulator_consumer_supply led_movie_supply[] = {
	REGULATOR_SUPPLY("led_movie", NULL),
};

#if defined(CONFIG_MACH_Q1_BD)
static struct regulator_consumer_supply led_torch_supply[] = {
	REGULATOR_SUPPLY("led_torch", NULL),
};
#endif /* CONFIG_MACH_Q1_BD */

static struct regulator_consumer_supply enp_32khz_ap_consumer[] = {
	REGULATOR_SUPPLY("gps_clk", "gsd4t"),
	REGULATOR_SUPPLY("bt_clk", NULL),
	REGULATOR_SUPPLY("wifi_clk", NULL),
};

#define REGULATOR_INIT(_ldo, _name, _min_uV, _max_uV, _always_on, _ops_mask, \
		       _disabled)					\
	static struct regulator_init_data _ldo##_init_data = {		\
		.constraints = {					\
			.name	= _name,				\
			.min_uV = _min_uV,				\
			.max_uV = _max_uV,				\
			.always_on	= _always_on,			\
			.boot_on	= _always_on,			\
			.apply_uV	= 1,				\
			.valid_ops_mask = _ops_mask,			\
			.state_mem	= {				\
				.disabled =				\
					(_disabled == -1 ? 0 : _disabled),\
				.enabled =				\
					(_disabled == -1 ? 0 : !(_disabled)),\
			}						\
		},							\
		.num_consumer_supplies = ARRAY_SIZE(_ldo##_supply),	\
		.consumer_supplies = &_ldo##_supply[0],			\
	};

REGULATOR_INIT(ldo1, "VADC_3.3V_C210", 3300000, 3300000, 1,
		REGULATOR_CHANGE_STATUS, 0);
REGULATOR_INIT(ldo3, "VUSB_1.1V", 1100000, 1100000, 1,
		REGULATOR_CHANGE_STATUS, 1);
REGULATOR_INIT(ldo4, "VMIPI_1.8V", 1800000, 1800000, 1,
		REGULATOR_CHANGE_STATUS, 1);
#if defined(CONFIG_MACH_U1_KOR_LGT)
REGULATOR_INIT(ldo5, "VHSIC_1.2V", 1200000, 1200000, 0,
		REGULATOR_CHANGE_STATUS, 1);
#else
REGULATOR_INIT(ldo5, "VHSIC_1.2V", 1200000, 1200000, 1,
		REGULATOR_CHANGE_STATUS, 1);
#endif
REGULATOR_INIT(ldo6, "VCC_1.8V_PDA", 1800000, 1800000, 1,
		REGULATOR_CHANGE_STATUS, 1);
REGULATOR_INIT(ldo7, "CAM_ISP_1.8V", 1800000, 1800000, 0,
		REGULATOR_CHANGE_STATUS, 1);
REGULATOR_INIT(ldo8, "VUSB_3.3V", 3300000, 3300000, 1,
		REGULATOR_CHANGE_STATUS, 1);
#if defined(CONFIG_S5PV310_HI_ARMCLK_THAN_1_2GHZ)
REGULATOR_INIT(ldo10, "VPLL_1.2V", 1200000, 1200000, 1,
		REGULATOR_CHANGE_STATUS, 1);
#else
REGULATOR_INIT(ldo10, "VPLL_1.1V", 1100000, 1100000, 1,
		REGULATOR_CHANGE_STATUS, 1);
#endif
REGULATOR_INIT(ldo11, "TOUCH_2.8V", 2800000, 2800000, 0,
		REGULATOR_CHANGE_STATUS, 1);
REGULATOR_INIT(ldo12, "VT_CAM_1.8V", 1800000, 1800000, 0,
		REGULATOR_CHANGE_STATUS, 1);
#if defined(CONFIG_MACH_Q1_BD)
REGULATOR_INIT(ldo13, "VCC_3.0V_LCD", 3100000, 3100000, 1,
		REGULATOR_CHANGE_STATUS, 1);
#else
REGULATOR_INIT(ldo13, "VCC_3.0V_LCD", 3000000, 3000000, 1,
		REGULATOR_CHANGE_STATUS, 1);
#endif
#if defined(CONFIG_MACH_Q1_BD)
REGULATOR_INIT(ldo14, "VCC_2.2V_LCD", 2200000, 2200000, 1,
		REGULATOR_CHANGE_STATUS, 1);
#else
REGULATOR_INIT(ldo14, "VCC_2.8V_MOTOR", 2800000, 2800000, 0,
		REGULATOR_CHANGE_STATUS, 1);
#endif
REGULATOR_INIT(ldo15, "VDD_2.2V_LCD", 2200000, 2200000, 1,
	       REGULATOR_CHANGE_STATUS, 1);
REGULATOR_INIT(ldo16, "CAM_SENSOR_IO_1.8V", 1800000, 1800000, 0,
		REGULATOR_CHANGE_STATUS, 1);
REGULATOR_INIT(ldo17, "VTF_2.8V", 2800000, 2800000, 0,
		REGULATOR_CHANGE_STATUS, 1);
#if defined(CONFIG_MACH_Q1_BD)
REGULATOR_INIT(ldo18, "TOUCH_LED_3.3V", 3300000, 3300000, 0,
		REGULATOR_CHANGE_STATUS, 1);
#else
REGULATOR_INIT(ldo18, "TOUCH_LED_3.3V", 3000000, 3300000, 0,
	REGULATOR_CHANGE_STATUS | REGULATOR_CHANGE_VOLTAGE, 1);
#endif
REGULATOR_INIT(ldo21, "VDDQ_M1M2_1.2V", 1200000, 1200000, 1,
		REGULATOR_CHANGE_STATUS, 1);


static struct regulator_init_data buck1_init_data = {
	.constraints	= {
		.name		= "vdd_arm range",
		.min_uV		= 650000,
		.max_uV		= 2225000,
		.always_on	= 1,
		.boot_on	= 1,
		.apply_uV	= 1,
		.valid_ops_mask	= REGULATOR_CHANGE_VOLTAGE |
				  REGULATOR_CHANGE_STATUS,
		.state_mem	= {
			.mode		= REGULATOR_MODE_NORMAL,
			.disabled	= 1,
		},
	},
	.num_consumer_supplies	= 1,
	.consumer_supplies	= &buck1_supply[0],
};

static struct regulator_init_data buck2_init_data = {
	.constraints	= {
		.name		= "vdd_int range",
		.min_uV		= 650000,
		.max_uV		= 2225000,
		.always_on	= 1,
		.boot_on	= 1,
		.apply_uV	= 1,
		.valid_ops_mask	= REGULATOR_CHANGE_VOLTAGE |
				  REGULATOR_CHANGE_STATUS,
		.state_mem	= {
			.mode		= REGULATOR_MODE_NORMAL,
			.disabled	= 1,
		},
	},
	.num_consumer_supplies	= 1,
	.consumer_supplies	= &buck2_supply[0],
};

static struct regulator_init_data buck3_init_data = {
	.constraints	= {
		.name		= "G3D_1.1V",
		.min_uV		= 900000,
		.max_uV		= 1200000,
		.always_on	= 0,
		.boot_on	= 0,
		.apply_uV	= 1,
		.valid_ops_mask	= REGULATOR_CHANGE_VOLTAGE |
				  REGULATOR_CHANGE_STATUS,
		.state_mem	= {
			.mode		= REGULATOR_MODE_NORMAL,
			.disabled	= 1,
		},
	},
	.num_consumer_supplies	= 1,
	.consumer_supplies	= &buck3_supply[0],
};

static struct regulator_init_data buck4_init_data = {
	.constraints	= {
		.name		= "CAM_ISP_CORE_1.2V",
		.min_uV		= 1200000,
		.max_uV		= 1200000,
		.apply_uV	= 1,
		.valid_ops_mask	= REGULATOR_CHANGE_STATUS,
		.state_mem	= {
			.disabled	= 1,
		},
	},
	.num_consumer_supplies	= 1,
	.consumer_supplies	= &buck4_supply[0],
};

static struct regulator_init_data buck5_init_data = {
	.constraints	= {
		.name		= "VMEM_1.2V",
		.min_uV		= 1200000,
		.max_uV		= 1200000,
		.apply_uV	= 1,
		.always_on	= 1,
		.state_mem	= {
			.uV	= 1200000,
			.mode	= REGULATOR_MODE_NORMAL,
			.enabled = 1,
		},
	},
};

static struct regulator_init_data buck7_init_data = {
	.constraints	= {
		.name		= "VCC_SUB_2.0V",
		.min_uV		= 2000000,
		.max_uV		= 2000000,
		.apply_uV	= 1,
		.always_on	= 1,
		.valid_ops_mask	= REGULATOR_CHANGE_STATUS,
		.state_mem	= {
			.enabled = 1,
		},
	},
	.num_consumer_supplies	= 1,
	.consumer_supplies	= &buck7_supply[0],
};

static struct regulator_init_data enp_32khz_ap_data = {
	.constraints	= {
		.name		= "32KHz AP",
		.always_on	= 1,
		.valid_ops_mask	= REGULATOR_CHANGE_STATUS,
		.state_mem	= {
			.enabled	= 1,
			.disabled	= 0,
		},
	},
	.num_consumer_supplies = ARRAY_SIZE(enp_32khz_ap_consumer),
	.consumer_supplies = enp_32khz_ap_consumer,
};

static struct regulator_init_data safeout1_init_data = {
	.constraints	= {
		.name		= "safeout1 range",
		.valid_ops_mask = REGULATOR_CHANGE_STATUS,
		.boot_on	= 1,
		.state_mem	= {
			.enabled = 1,
		},
	},
	.num_consumer_supplies	= ARRAY_SIZE(safeout1_supply),
	.consumer_supplies	= safeout1_supply,
};

static struct regulator_init_data safeout2_init_data = {
	.constraints	= {
		.name		= "safeout2 range",
		.valid_ops_mask = REGULATOR_CHANGE_STATUS,
		.state_mem	= {
			.enabled = 1,
		},
	},
	.num_consumer_supplies	= ARRAY_SIZE(safeout2_supply),
	.consumer_supplies	= safeout2_supply,
};

static struct regulator_init_data led_flash_init_data = {
	.constraints = {
		.name	= "FLASH_CUR",
		.min_uA = 23440,
		.max_uA = 750080,
		.valid_ops_mask	= REGULATOR_CHANGE_CURRENT |
				  REGULATOR_CHANGE_STATUS,
#if !defined(CONFIG_MACH_Q1_BD)
		.state_mem	= {
			.disabled	= 1,
		},
#endif
	},
	.num_consumer_supplies = 1,
	.consumer_supplies = &led_flash_supply[0],
};

static struct regulator_init_data led_movie_init_data = {
	.constraints = {
		.name	= "MOVIE_CUR",
		.min_uA = 15625,
		.max_uA = 250000,
		.valid_ops_mask	= REGULATOR_CHANGE_CURRENT |
				  REGULATOR_CHANGE_STATUS,
#if !defined(CONFIG_MACH_Q1_BD)
		.state_mem	= {
			.disabled	= 1,
		},
#endif
	},
	.num_consumer_supplies = 1,
	.consumer_supplies = &led_movie_supply[0],
};

#if defined(CONFIG_MACH_Q1_BD)
static struct regulator_init_data led_torch_init_data = {
	.constraints = {
		.name	= "FLASH_TORCH",
		.min_uA = 15625,
		.max_uA = 250000,
		.valid_ops_mask	= REGULATOR_CHANGE_CURRENT |
				  REGULATOR_CHANGE_STATUS,
	},
	.num_consumer_supplies = 1,
	.consumer_supplies = &led_torch_supply[0],
};
#endif /* CONFIG_MACH_Q1_BD */

static struct max8997_regulator_data max8997_regulators[] = {
	{ MAX8997_LDO1,	 &ldo1_init_data, NULL, },
	{ MAX8997_LDO3,	 &ldo3_init_data, NULL, },
	{ MAX8997_LDO4,	 &ldo4_init_data, NULL, },
	{ MAX8997_LDO5,	 &ldo5_init_data, NULL, },
	{ MAX8997_LDO6,	 &ldo6_init_data, NULL, },
	{ MAX8997_LDO7,	 &ldo7_init_data, NULL, },
	{ MAX8997_LDO8,	 &ldo8_init_data, NULL, },
	{ MAX8997_LDO10, &ldo10_init_data, NULL, },
	{ MAX8997_LDO11, &ldo11_init_data, NULL, },
	{ MAX8997_LDO12, &ldo12_init_data, NULL, },
	{ MAX8997_LDO13, &ldo13_init_data, NULL, },
	{ MAX8997_LDO14, &ldo14_init_data, NULL, },
	{ MAX8997_LDO15, &ldo15_init_data, NULL, },
	{ MAX8997_LDO16, &ldo16_init_data, NULL, },
	{ MAX8997_LDO17, &ldo17_init_data, NULL, },
	{ MAX8997_LDO18, &ldo18_init_data, NULL, },
	{ MAX8997_LDO21, &ldo21_init_data, NULL, },
	{ MAX8997_BUCK1, &buck1_init_data, NULL, },
	{ MAX8997_BUCK2, &buck2_init_data, NULL, },
	{ MAX8997_BUCK3, &buck3_init_data, NULL, },
	{ MAX8997_BUCK4, &buck4_init_data, NULL, },
	{ MAX8997_BUCK5, &buck5_init_data, NULL, },
	{ MAX8997_BUCK7, &buck7_init_data, NULL, },
	{ MAX8997_ESAFEOUT1, &safeout1_init_data, NULL, },
	{ MAX8997_ESAFEOUT2, &safeout2_init_data, NULL, },
	{ MAX8997_FLASH_CUR, &led_flash_init_data, NULL, },
	{ MAX8997_MOVIE_CUR, &led_movie_init_data, NULL, },
#if defined CONFIG_MACH_Q1_BD
	{ MAX8997_FLASH_TORCH, &led_torch_init_data, NULL, },
#endif /* CONFIG_MACH_Q1_BD */
	{MAX8997_EN32KHZ_AP, &enp_32khz_ap_data, NULL},
};

static struct max8997_power_data max8997_power = {
	.batt_detect = 1,
};

#if defined(CONFIG_MACH_Q1_BD)
static void motor_init_hw(void)
{
	if (gpio_request(GPIO_MOTOR_EN, "MOTOR_EN") < 0)
		pr_err("[VIB] Failed to request GPIO_MOTOR_EN\n");
}

static void motor_en(bool enable)
{
	gpio_direction_output(GPIO_MOTOR_EN, enable);
}
#endif

#ifdef CONFIG_VIBETONZ
#ifdef CONFIG_TARGET_LOCALE_NTT
static struct max8997_motor_data max8997_motor = {
	.max_timeout = 10000,
	.duty = 43696,
	.period = 44138,
	.init_hw = NULL,
	.motor_en = NULL,
	.pwm_id = 1,
};
#elif defined(CONFIG_TARGET_LOCALE_KOR) || defined(CONFIG_TARGET_LOCALE_NA)
static struct max8997_motor_data max8997_motor = {
	.max_timeout = 10000,
	.duty = 44196,
	.period = 44643,
	.init_hw = NULL,
	.motor_en = NULL,
	.pwm_id = 1,
};
#elif defined(CONFIG_MACH_Q1_BD)
static struct max8997_motor_data max8997_motor = {
	.max_timeout = 10000,
	.duty = 37641,
	.period = 38022,
	.init_hw = motor_init_hw,
	.motor_en = motor_en,
	.pwm_id = 1,
};
#else
static struct max8997_motor_data max8997_motor = {
	.max_timeout = 10000,
	.duty = 37641,
	.period = 38022,
	.init_hw = NULL,
	.motor_en = NULL,
	.pwm_id = 1,
};
#endif
#endif

#ifdef CONFIG_MACH_U1_KOR_LGT
static int max8997_muic_set_safeout(int path)
{
	static int safeout2_enabled;
	struct regulator *regulator;

	pr_info("%s: path = %d\n", __func__, path);

	if (path == CP_USB_MODE) {
		regulator = regulator_get(NULL, "safeout1");
		if (IS_ERR(regulator))
			return -ENODEV;
		if (regulator_is_enabled(regulator))
			regulator_force_disable(regulator);
		regulator_put(regulator);

		regulator = regulator_get(NULL, "safeout2");
		if (IS_ERR(regulator))
			return -ENODEV;
		if (!safeout2_enabled) {
			pr_info("%s: enable safeout2\n", __func__);
			regulator_enable(regulator);
			safeout2_enabled = 1;
		} else
			pr_info("%s: safeout2 is already enabled\n",
							__func__);
		regulator_put(regulator);
	} else {
		/* AP_USB_MODE || AUDIO_MODE */
		regulator = regulator_get(NULL, "safeout1");
		if (IS_ERR(regulator))
			return -ENODEV;
		if (!regulator_is_enabled(regulator))
			regulator_enable(regulator);
		regulator_put(regulator);

		regulator = regulator_get(NULL, "safeout2");
		if (IS_ERR(regulator))
			return -ENODEV;
		if (safeout2_enabled) {
			pr_info("%s: disable safeout2\n", __func__);
			regulator_disable(regulator);
			safeout2_enabled = 0;
		} else
			pr_info("%s: safeout2 is already disabled\n",
							__func__);
		regulator_put(regulator);
	}

	return 0;
}
#else
static int max8997_muic_set_safeout(int path)
{
	struct regulator *regulator;

	if (path == CP_USB_MODE) {
		regulator = regulator_get(NULL, "safeout1");
		if (IS_ERR(regulator))
			return -ENODEV;
		if (regulator_is_enabled(regulator))
			regulator_force_disable(regulator);
		regulator_put(regulator);

		regulator = regulator_get(NULL, "safeout2");
		if (IS_ERR(regulator))
			return -ENODEV;
		if (!regulator_is_enabled(regulator))
			regulator_enable(regulator);
		regulator_put(regulator);
	} else {
		/* AP_USB_MODE || AUDIO_MODE */
		regulator = regulator_get(NULL, "safeout1");
		if (IS_ERR(regulator))
			return -ENODEV;
		if (!regulator_is_enabled(regulator))
			regulator_enable(regulator);
		regulator_put(regulator);

		regulator = regulator_get(NULL, "safeout2");
		if (IS_ERR(regulator))
			return -ENODEV;
		if (regulator_is_enabled(regulator))
			regulator_force_disable(regulator);
		regulator_put(regulator);
	}

	return 0;
}
#endif

static struct charging_status_callbacks {
	void (*tsp_set_charging_cable) (int type);
} charging_cbs;

bool is_cable_attached;
static int connected_cable_type = CABLE_TYPE_NONE;

static int max8997_muic_charger_cb(int cable_type)
{
	struct power_supply *psy = power_supply_get_by_name("battery");
	union power_supply_propval value;

	connected_cable_type = cable_type;

	if (!psy) {
		pr_err("%s: fail to get battery ps\n", __func__);
#if defined(CONFIG_MACH_Q1_BD)
		return 0;
#else
		return -ENODEV;
#endif
	}

	switch (cable_type) {
	case CABLE_TYPE_NONE:
	case CABLE_TYPE_OTG:
	case CABLE_TYPE_JIG_UART_OFF:
	case CABLE_TYPE_MHL:
		value.intval = POWER_SUPPLY_TYPE_BATTERY;
		is_cable_attached = false;
		break;
	case CABLE_TYPE_USB:
	case CABLE_TYPE_JIG_USB_OFF:
	case CABLE_TYPE_JIG_USB_ON:
		value.intval = POWER_SUPPLY_TYPE_USB;
		is_cable_attached = true;
		break;
	case CABLE_TYPE_MHL_VB:
		value.intval = POWER_SUPPLY_TYPE_MISC;
		is_cable_attached = true;
		break;
	case CABLE_TYPE_TA:
	case CABLE_TYPE_CARDOCK:
	case CABLE_TYPE_DESKDOCK:
	case CABLE_TYPE_JIG_UART_OFF_VB:
		value.intval = POWER_SUPPLY_TYPE_MAINS;
		is_cable_attached = true;
		break;
	default:
		pr_err("%s: invalid type:%d\n", __func__, cable_type);
		return -EINVAL;
	}

	if (charging_cbs.tsp_set_charging_cable)
		charging_cbs.tsp_set_charging_cable(value.intval);
#ifdef CONFIG_JACK_MON
	jack_event_handler("charger", is_cable_attached);
#endif

	return psy->set_property(psy, POWER_SUPPLY_PROP_ONLINE, &value);
}

#ifdef CONFIG_USB_HOST_NOTIFY
static void usb_otg_accessory_power(int enable)
{
#ifdef CONFIG_SMB328_CHARGER	/* Q1_EUR_OPEN */
	u8 on = (u8)!!enable;
	struct power_supply *psy_sub =
		power_supply_get_by_name("smb328-charger");
	union power_supply_propval value;
	int ret;

	if (!psy_sub) {
		pr_info("%s: fail to get charger ps\n", __func__);
		return;
	}

	value.intval = on;
	ret = psy_sub->set_property(psy_sub,
		POWER_SUPPLY_PROP_CHARGE_TYPE, /* only for OTG */
		&value);
	if (ret) {
		pr_info("%s: fail to set OTG (%d)\n",
			__func__, ret);
		return;
	}
	pr_info("%s: otg power = %d\n", __func__, on);
#else
	u8 on = (u8)!!enable;

	gpio_request(GPIO_USB_OTG_EN, "USB_OTG_EN");
	gpio_direction_output(GPIO_USB_OTG_EN, on);
	gpio_free(GPIO_USB_OTG_EN);
	pr_info("%s: otg accessory power = %d\n", __func__, on);
#endif
}

static struct host_notifier_platform_data host_notifier_pdata = {
	.ndev.name	= "usb_otg",
	.booster	= usb_otg_accessory_power,
};

struct platform_device host_notifier_device = {
	.name = "host_notifier",
	.dev.platform_data = &host_notifier_pdata,
};

#include "u1-otg.c"
static void max8997_muic_usb_cb(u8 usb_mode)
{
	struct s3c_udc *udc = platform_get_drvdata(&s3c_device_usbgadget);
	int ret = 0;

	pr_info("otg %s: usb mode=%d\n", __func__, usb_mode);

#if 0
	u32 lpcharging = __raw_readl(S5P_INFORM2);
	if (lpcharging == 1) {
		struct regulator *regulator;
		pr_info("%s: lpcharging: disable USB\n", __func__);

		ret = c210_change_usb_mode(udc, USB_CABLE_DETACHED);
		if (ret < 0)
			pr_warn("%s: fail to change mode!!!\n", __func__);

		regulator = regulator_get(NULL, "safeout1");
		if (IS_ERR(regulator)) {
			pr_err("%s: fail to get regulator\n", __func__);
			return;
		}

		if (regulator_is_enabled(regulator))
			regulator_force_disable(regulator);
		regulator_put(regulator);

		return;
	}
#endif

#ifdef CONFIG_TARGET_LOCALE_KOR
	if (u1_switch_get_usb_lock_state()) {
		pr_info("%s: usb locked by mdm\n", __func__);
		return;
	}
#endif

	if (udc) {
		if (usb_mode == USB_OTGHOST_ATTACHED) {
			usb_otg_accessory_power(1);
			max8997_muic_charger_cb(CABLE_TYPE_OTG);
		}

		ret = c210_change_usb_mode(udc, usb_mode);
		if (ret < 0)
			pr_err("%s: fail to change mode!!!\n", __func__);

		if (usb_mode == USB_OTGHOST_DETACHED)
			usb_otg_accessory_power(0);
#ifdef CONFIG_JACK_MON
		if (usb_mode == USB_OTGHOST_ATTACHED)
			jack_event_handler("host", USB_CABLE_ATTACHED);
		else if (usb_mode == USB_OTGHOST_DETACHED)
			jack_event_handler("host", USB_CABLE_DETACHED);
		else if ((usb_mode == USB_CABLE_ATTACHED)
			|| (usb_mode == USB_CABLE_DETACHED))
			jack_event_handler("usb", usb_mode);
#endif
	} else
		pr_info("otg error s3c_udc is null.\n");
}
#endif

static void max8997_muic_mhl_cb(int attached)
{
	pr_info("%s(%d)\n", __func__, attached);

	if (attached == MAX8997_MUIC_ATTACHED) {
#ifdef	CONFIG_SAMSUNG_MHL
		mhl_onoff_ex(true);
	} else if (attached == MAX8997_MUIC_DETACHED) {
		mhl_onoff_ex(false);
#endif
	}
}

static bool max8997_muic_is_mhl_attached(void)
{
	int val;

	gpio_request(GPIO_MHL_SEL, "MHL_SEL");
	val = gpio_get_value(GPIO_MHL_SEL);
	gpio_free(GPIO_MHL_SEL);

	return !!val;
}

static void max8997_muic_deskdock_cb(bool attached)
{
#ifdef CONFIG_JACK_MON
	if (attached)
		jack_event_handler("cradle", 1);
	else
		jack_event_handler("cradle", 0);
#endif
}

static void max8997_muic_cardock_cb(bool attached)
{
#ifdef CONFIG_JACK_MON
	if (attached)
		jack_event_handler("cradle", 2);
	else
		jack_event_handler("cradle", 0);
#endif
}

static int max8997_muic_cfg_uart_gpio(void)
{
	int val, path;

	val = gpio_get_value(GPIO_UART_SEL);
	path = val ? UART_PATH_AP : UART_PATH_CP;
#if 0
	/* Workaround
	 * Sometimes sleep current is 15 ~ 20mA if UART path was CP.
	 */
	if (path == UART_PATH_CP)
		gpio_set_value(GPIO_UART_SEL, GPIO_LEVEL_HIGH);
#endif
	pr_info("%s: path=%d\n", __func__, path);
	return path;
}

static void max8997_muic_jig_uart_cb(int path)
{
	int val;

	val = path == UART_PATH_AP ? GPIO_LEVEL_HIGH : GPIO_LEVEL_LOW;
	gpio_set_value(GPIO_UART_SEL, val);
	pr_info("%s: val:%d\n", __func__, val);
}

static int max8997_muic_host_notify_cb(int enable)
{
	struct host_notify_dev *ndev = &host_notifier_pdata.ndev;

	if (ndev) {
		ndev->booster = enable ? NOTIFY_POWER_ON : NOTIFY_POWER_OFF;
		pr_info("%s: mode %d, enable %d\n", __func__,
				ndev->mode, enable);
		return ndev->mode;
	} else
		pr_info("%s: host_notify_dev is null, enable %d\n",
				__func__, enable);

	return -1;
}

static struct max8997_muic_data max8997_muic = {
	.usb_cb = max8997_muic_usb_cb,
	.charger_cb = max8997_muic_charger_cb,
	.mhl_cb = max8997_muic_mhl_cb,
	.is_mhl_attached = max8997_muic_is_mhl_attached,
	.set_safeout = max8997_muic_set_safeout,
	.deskdock_cb = max8997_muic_deskdock_cb,
	.cardock_cb = max8997_muic_cardock_cb,
	.cfg_uart_gpio = max8997_muic_cfg_uart_gpio,
	.jig_uart_cb = max8997_muic_jig_uart_cb,
	.host_notify_cb = max8997_muic_host_notify_cb,
	.gpio_usb_sel = GPIO_USB_SEL,
};

#ifdef CONFIG_UART_SELECT
/* Uart Select */
static void trats_set_uart_switch(int path)
{
	gpio_request(GPIO_UART_SEL, "UART_SEL");

	/* trats target is gpio_high == AP */
	if (path == UART_SW_PATH_AP)
		gpio_set_value(GPIO_UART_SEL, GPIO_LEVEL_HIGH);
	else if (path == UART_SW_PATH_CP)
		gpio_set_value(GPIO_UART_SEL, GPIO_LEVEL_LOW);

	gpio_free(GPIO_UART_SEL);
	return;
}

static int trats_get_uart_switch(void)
{
	int val;

	gpio_request(GPIO_UART_SEL, "UART_SEL");
	val = gpio_get_value(GPIO_UART_SEL);
	gpio_free(GPIO_UART_SEL);

	/* trats target is gpio_high == AP */
	if (val == GPIO_LEVEL_HIGH)
		return UART_SW_PATH_AP;
	else if (val == GPIO_LEVEL_LOW)
		return UART_SW_PATH_CP;
	else
		return UART_SW_PATH_NA;
}

static struct uart_select_platform_data trats_uart_select_data = {
	.set_uart_switch	= trats_set_uart_switch,
	.get_uart_switch	= trats_get_uart_switch,
};

static struct platform_device trats_uart_select = {
	.name			= "uart-select",
	.id			= -1,
	.dev			= {
		.platform_data	= &trats_uart_select_data,
	},
};
#endif


static struct max8997_buck1_dvs_funcs *buck1_dvs_funcs;

void max8997_set_arm_voltage_table(int *voltage_table, int arr_size)
{
	pr_info("%s\n", __func__);
	if (buck1_dvs_funcs && buck1_dvs_funcs->set_buck1_dvs_table)
		buck1_dvs_funcs->set_buck1_dvs_table(buck1_dvs_funcs,
			 voltage_table, arr_size);
}

static void max8997_register_buck1dvs_funcs(struct max8997_buck1_dvs_funcs *ptr)
{
	buck1_dvs_funcs = ptr;
}


static struct max8997_platform_data exynos4_max8997_info = {
	.num_regulators = ARRAY_SIZE(max8997_regulators),
	.regulators	= &max8997_regulators[0],
	.irq_base	= IRQ_BOARD_START,
	.wakeup		= 1,
	.buck1_gpiodvs	= false,
	.buck1_max_vol	= 1350000,
	.buck2_max_vol	= 1150000,
	.buck5_max_vol	= 1200000,
	.buck_set1 = GPIO_BUCK1_EN_A,
	.buck_set2 = GPIO_BUCK1_EN_B,
	.buck_set3 = GPIO_BUCK2_EN,
	.buck_ramp_en = true,
	.buck_ramp_delay = 10,		/* 10.00mV /us (default) */
	.flash_cntl_val = 0x5F,	/* Flash safety timer duration: 800msec,
					   Maximum timer mode */
	.power = &max8997_power,
#ifdef CONFIG_VIBETONZ
	.motor = &max8997_motor,
#endif
	.muic = &max8997_muic,
	.register_buck1_dvs_funcs = max8997_register_buck1dvs_funcs,
};
#endif /* CONFIG_REGULATOR_MAX8997 */

/* Bluetooth */
#ifdef CONFIG_BT_BCM4330
static struct platform_device bcm4330_bluetooth_device = {
	.name = "bcm4330_bluetooth",
	.id = -1,
};
#endif				/* CONFIG_BT_BCM4330 */

#ifdef CONFIG_GPS_GSD4T
/* GSD4T GPS */
static struct gsd4t_platform_data u1_gsd4t_data = {
	.onoff		= GPIO_GPS_PWR_EN,
	.nrst		= GPIO_GPS_nRST,
	.uart_rxd	= GPIO_GPS_RXD,
	.uart_txd	= GPIO_GPS_TXD,
	.uart_cts	= GPIO_GPS_CTS,
	.uart_rts	= GPIO_GPS_RTS,
};

static struct platform_device u1_gsd4t = {
	.name			= "gsd4t",
	.id			= -1,
	.dev			= {
		.platform_data	= &u1_gsd4t_data,
	},
};
#endif

#ifdef CONFIG_TARGET_LOCALE_KOR
#define SYSTEM_REV_SND 0x05
#else
#define SYSTEM_REV_SND 0x09
#endif

#if defined(CONFIG_SND_SOC_SLP_TRATS_MC1N2)
void sec_set_sub_mic_bias(bool on)
{
#ifdef CONFIG_SND_SOC_USE_EXTERNAL_MIC_BIAS
	gpio_set_value(GPIO_SUB_MIC_BIAS_EN, on);
#endif
}

void sec_set_main_mic_bias(bool on)
{
#ifdef CONFIG_SND_SOC_USE_EXTERNAL_MIC_BIAS
	gpio_set_value(GPIO_MIC_BIAS_EN, on);
#endif
}

int sec_set_ldo1_constraints(int disabled)
{
	struct regulator *regulator;

	if (!disabled) {
		regulator = regulator_get(NULL, "vadc_3.3v");
		if (IS_ERR(regulator))
			return -1;
		regulator_enable(regulator);
		regulator_put(regulator);
	} else {
		regulator = regulator_get(NULL, "vadc_3.3v");
		if (IS_ERR(regulator))
			return -1;
		if (regulator_is_enabled(regulator))
			regulator_force_disable(regulator);
		regulator_put(regulator);
	}

	return 0;
}

static struct mc1n2_platform_data mc1n2_pdata = {
	.set_main_mic_bias = sec_set_main_mic_bias,
	.set_sub_mic_bias = sec_set_sub_mic_bias,
	.set_adc_power_constraints = sec_set_ldo1_constraints,
};

static void trats_sound_init(void)
{
#ifdef CONFIG_SND_SOC_USE_EXTERNAL_MIC_BIAS
	int err;

	err = gpio_request(GPIO_MIC_BIAS_EN, "GPE1");
	if (err) {
		pr_err(KERN_ERR "MIC_BIAS_EN GPIO set error!\n");
		return;
	}
	gpio_direction_output(GPIO_MIC_BIAS_EN, 1);
	gpio_set_value(GPIO_MIC_BIAS_EN, 0);
	gpio_free(GPIO_MIC_BIAS_EN);

	err = gpio_request(GPIO_EAR_MIC_BIAS_EN, "GPE2");
	if (err) {
		pr_err(KERN_ERR "EAR_MIC_BIAS_EN GPIO set error!\n");
		return;
	}
	gpio_direction_output(GPIO_EAR_MIC_BIAS_EN, 1);
	gpio_set_value(GPIO_EAR_MIC_BIAS_EN, 0);
	gpio_free(GPIO_EAR_MIC_BIAS_EN);

#if defined(CONFIG_MACH_Q1_BD)
	err = gpio_request(GPIO_SUB_MIC_BIAS_EN, "submic_bias");
	if (err) {
		pr_err(KERN_ERR "SUB_MIC_BIAS_EN GPIO set error!\n");
		return;
	}
	gpio_direction_output(GPIO_SUB_MIC_BIAS_EN, 1);
	gpio_set_value(GPIO_SUB_MIC_BIAS_EN, 0);
	gpio_free(GPIO_SUB_MIC_BIAS_EN);

#else
	if (system_rev >= SYSTEM_REV_SND) {
		err = gpio_request(GPIO_SUB_MIC_BIAS_EN, "submic_bias");
		if (err) {
			pr_err(KERN_ERR "SUB_MIC_BIAS_EN GPIO set error!\n");
			return;
		}
		gpio_direction_output(GPIO_SUB_MIC_BIAS_EN, 0);
		gpio_free(GPIO_SUB_MIC_BIAS_EN);
	}
#endif /* #if defined(CONFIG_MACH_Q1_BD) */
#endif
}
#endif

#if defined(CONFIG_TDMB) || defined(CONFIG_TDMB_MODULE)
static void tdmb_set_config_poweron(void)
{
	s3c_gpio_cfgpin(GPIO_TDMB_EN, S3C_GPIO_OUTPUT);
	s3c_gpio_setpull(GPIO_TDMB_EN, S3C_GPIO_PULL_NONE);
	gpio_set_value(GPIO_TDMB_EN, GPIO_LEVEL_LOW);

	s3c_gpio_cfgpin(GPIO_TDMB_RST_N, S3C_GPIO_OUTPUT);
	s3c_gpio_setpull(GPIO_TDMB_RST_N, S3C_GPIO_PULL_NONE);
	gpio_set_value(GPIO_TDMB_RST_N, GPIO_LEVEL_LOW);

	s3c_gpio_cfgpin(GPIO_TDMB_INT, S3C_GPIO_SFN(GPIO_TDMB_INT_AF));
	s3c_gpio_setpull(GPIO_TDMB_INT, S3C_GPIO_PULL_NONE);
}
static void tdmb_set_config_poweroff(void)
{
	s3c_gpio_cfgpin(GPIO_TDMB_EN, S3C_GPIO_OUTPUT);
	s3c_gpio_setpull(GPIO_TDMB_EN, S3C_GPIO_PULL_NONE);
	gpio_set_value(GPIO_TDMB_EN, GPIO_LEVEL_LOW);

	s3c_gpio_cfgpin(GPIO_TDMB_RST_N, S3C_GPIO_OUTPUT);
	s3c_gpio_setpull(GPIO_TDMB_RST_N, S3C_GPIO_PULL_NONE);
	gpio_set_value(GPIO_TDMB_RST_N, GPIO_LEVEL_LOW);

	s3c_gpio_cfgpin(GPIO_TDMB_INT, S3C_GPIO_OUTPUT);
	s3c_gpio_setpull(GPIO_TDMB_INT, S3C_GPIO_PULL_NONE);
	gpio_set_value(GPIO_TDMB_INT, GPIO_LEVEL_LOW);
}

static void tdmb_gpio_on(void)
{
	printk(KERN_DEBUG "tdmb_gpio_on\n");

	tdmb_set_config_poweron();

	gpio_set_value(GPIO_TDMB_EN, GPIO_LEVEL_HIGH);
	usleep_range(10000, 10000);
	gpio_set_value(GPIO_TDMB_RST_N, GPIO_LEVEL_LOW);
	usleep_range(2000, 2000);
	gpio_set_value(GPIO_TDMB_RST_N, GPIO_LEVEL_HIGH);
	usleep_range(10000, 10000);
}

static void tdmb_gpio_off(void)
{
	printk(KERN_DEBUG "tdmb_gpio_off\n");

	tdmb_set_config_poweroff();

	gpio_set_value(GPIO_TDMB_RST_N, GPIO_LEVEL_LOW);
	usleep_range(1000, 1000);
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
	tdmb_set_config_poweroff();
	s5p_register_gpio_interrupt(GPIO_TDMB_INT);
	tdmb_pdata.irq = GPIO_TDMB_IRQ;
	platform_device_register(&tdmb_device);

	return 0;
}
#endif

#ifdef CONFIG_BATTERY_SEC_U1
static int c1_charger_topoff_cb(void)
{
	struct power_supply *psy = power_supply_get_by_name("battery");
	union power_supply_propval value;

	if (!psy) {
		pr_err("%s: fail to get battery ps\n", __func__);
		return -ENODEV;
	}

	value.intval = POWER_SUPPLY_STATUS_FULL;
	return psy->set_property(psy, POWER_SUPPLY_PROP_STATUS, &value);
}
#endif

#if defined(CONFIG_MACH_Q1_CHN) && \
	(defined(CONFIG_SMB136_CHARGER_Q1) || defined(CONFIG_SMB328_CHARGER))
static int c1_charger_ovp_cb(bool is_ovp)
{
	struct power_supply *psy = power_supply_get_by_name("battery");
	union power_supply_propval value;

	if (!psy) {
		pr_err("%s: fail to get battery ps\n", __func__);
		return -ENODEV;
	}

	if (is_ovp)
		value.intval = POWER_SUPPLY_HEALTH_OVERVOLTAGE;
	else
		value.intval = POWER_SUPPLY_HEALTH_GOOD;

	return psy->set_property(psy, POWER_SUPPLY_PROP_VOLTAGE_MAX, &value);
}
#endif

#ifdef CONFIG_LEDS_MAX8997
struct led_max8997_platform_data led_max8997_platform_data = {
	.name = "leds-sec",
	.brightness = 0,
};

struct platform_device sec_device_leds_max8997 = {
	.name   = "leds-max8997",
	.id     = -1,
	.dev = { .platform_data = &led_max8997_platform_data},
};
#endif /* CONFIG_LEDS_MAX8997 */

#ifdef CONFIG_CHARGER_MAX8922_U1
static int max8922_cfg_gpio(void)
{
	if (system_rev < HWREV_FOR_BATTERY)
		return -ENODEV;

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
#ifdef CONFIG_BATTERY_SEC_U1
	.topoff_cb = c1_charger_topoff_cb,
#endif
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

#ifdef CONFIG_BATTERY_SAMSUNG
static struct platform_device samsung_device_battery = {
	.name = "samsung-fake-battery",
	.id = -1,
};
#endif

#ifdef CONFIG_BATTERY_SEC_U1

#ifdef CONFIG_TARGET_LOCALE_KOR
/* temperature table for ADC 6 */
static struct sec_bat_adc_table_data temper_table[] =  {
	{  264,	 500 },
	{  275,	 490 },
	{  286,	 480 },
	{  293,	 480 },
	{  299,	 470 },
	{  306,	 460 },
	{  324,	 450 },
	{  341,	 450 },
	{  354,	 440 },
	{  368,	 430 },
	{  381,	 420 },
	{  396,	 420 },
	{  411,	 410 },
	{  427,	 400 },
	{  442,	 390 },
	{  457,	 390 },
	{  472,	 380 },
	{  487,	 370 },
	{  503,	 370 },
	{  518,	 360 },
	{  533,	 350 },
	{  554,	 340 },
	{  574,	 330 },
	{  595,	 330 },
	{  615,	 320 },
	{  636,	 310 },
	{  656,	 310 },
	{  677,	 300 },
	{  697,	 290 },
	{  718,	 280 },
	{  738,	 270 },
	{  761,	 270 },
	{  784,	 260 },
	{  806,	 250 },
	{  829,	 240 },
	{  852,	 230 },
	{  875,	 220 },
	{  898,	 210 },
	{  920,	 200 },
	{  943,	 190 },
	{  966,	 180 },
	{  990,	 170 },
	{ 1013,	 160 },
	{ 1037,	 150 },
	{ 1060,	 140 },
	{ 1084,	 130 },
	{ 1108,	 120 },
	{ 1131,	 110 },
	{ 1155,	 100 },
	{ 1178,	  90 },
	{ 1202,	  80 },
	{ 1226,	  70 },
	{ 1251,	  60 },
	{ 1275,	  50 },
	{ 1299,	  40 },
	{ 1324,	  30 },
	{ 1348,	  20 },
	{ 1372,	  10 },
	{ 1396,	   0 },
	{ 1421,	 -10 },
	{ 1445,	 -20 },
	{ 1468,	 -30 },
	{ 1491,	 -40 },
	{ 1513,	 -50 },
	{ 1536,	 -60 },
	{ 1559,	 -70 },
	{ 1577,	 -80 },
	{ 1596,	 -90 },
	{ 1614,	 -100 },
	{ 1619,	 -110 },
	{ 1632,	 -120 },
	{ 1658,	 -130 },
	{ 1667,	 -140 },
};
#elif defined(CONFIG_TARGET_LOCALE_NTT)
/* temperature table for ADC 6 */
static struct sec_bat_adc_table_data temper_table[] =  {
	{  273,	 670 },
	{  289,	 660 },
	{  304,	 650 },
	{  314,	 640 },
	{  325,	 630 },
	{  337,	 620 },
	{  347,	 610 },
	{  361,	 600 },
	{  376,	 590 },
	{  391,	 580 },
	{  406,	 570 },
	{  417,	 560 },
	{  431,	 550 },
	{  447,	 540 },
	{  474,	 530 },
	{  491,	 520 },
	{  499,	 510 },
	{  511,	 500 },
	{  519,	 490 },
	{  547,	 480 },
	{  568,	 470 },
	{  585,	 460 },
	{  597,	 450 },
	{  614,	 440 },
	{  629,	 430 },
	{  647,	 420 },
	{  672,	 410 },
	{  690,	 400 },
	{  720,	 390 },
	{  735,	 380 },
	{  755,	 370 },
	{  775,	 360 },
	{  795,	 350 },
	{  818,	 340 },
	{  841,	 330 },
	{  864,	 320 },
	{  887,	 310 },
	{  909,	 300 },
	{  932,	 290 },
	{  954,	 280 },
	{  976,	 270 },
	{  999,	 260 },
	{ 1021,	 250 },
	{ 1051,	 240 },
	{ 1077,	 230 },
	{ 1103,	 220 },
	{ 1129,	 210 },
	{ 1155,	 200 },
	{ 1177,	 190 },
	{ 1199,	 180 },
	{ 1220,	 170 },
	{ 1242,	 160 },
	{ 1263,	 150 },
	{ 1284,	 140 },
	{ 1306,	 130 },
	{ 1326,	 120 },
	{ 1349,	 110 },
	{ 1369,	 100 },
	{ 1390,	  90 },
	{ 1411,	  80 },
	{ 1433,	  70 },
	{ 1454,	  60 },
	{ 1474,	  50 },
	{ 1486,	  40 },
	{ 1499,	  30 },
	{ 1512,	  20 },
	{ 1531,	  10 },
	{ 1548,	   0 },
	{ 1570,	 -10 },
	{ 1597,	 -20 },
	{ 1624,	 -30 },
	{ 1633,	 -40 },
	{ 1643,	 -50 },
	{ 1652,	 -60 },
	{ 1663,	 -70 },
};
#else
/* temperature table for ADC 6 */
static struct sec_bat_adc_table_data temper_table[] = {
	{  165,	 800 },
	{  171,	 790 },
	{  177,	 780 },
	{  183,	 770 },
	{  189,	 760 },
	{  196,	 750 },
	{  202,	 740 },
	{  208,	 730 },
	{  214,	 720 },
	{  220,	 710 },
	{  227,	 700 },
	{  237,	 690 },
	{  247,	 680 },
	{  258,	 670 },
	{  269,	 660 },
	{  281,	 650 },
	{  296,	 640 },
	{  311,	 630 },
	{  326,	 620 },
	{  341,	 610 },
	{  356,	 600 },
	{  370,	 590 },
	{  384,	 580 },
	{  398,	 570 },
	{  412,	 560 },
	{  427,	 550 },
	{  443,	 540 },
	{  457,	 530 },
	{  471,	 520 },
	{  485,	 510 },
	{  498,	 500 },
	{  507,	 490 },
	{  516,	 480 },
	{  525,	 470 },
	{  535,	 460 },
	{  544,	 450 },
	{  553,	 440 },
	{  562,	 430 },
	{  579,	 420 },
	{  596,	 410 },
	{  613,	 400 },
	{  630,	 390 },
	{  648,	 380 },
	{  665,	 370 },
	{  684,	 360 },
	{  702,	 350 },
	{  726,	 340 },
	{  750,	 330 },
	{  774,	 320 },
	{  798,	 310 },
	{  821,	 300 },
	{  844,	 290 },
	{  867,	 280 },
	{  891,	 270 },
	{  914,	 260 },
	{  937,	 250 },
	{  960,	 240 },
	{  983,	 230 },
	{ 1007,	 220 },
	{ 1030,	 210 },
	{ 1054,	 200 },
	{ 1083,	 190 },
	{ 1113,	 180 },
	{ 1143,	 170 },
	{ 1173,	 160 },
	{ 1202,	 150 },
	{ 1232,	 140 },
	{ 1262,	 130 },
	{ 1291,	 120 },
	{ 1321,	 110 },
	{ 1351,	 100 },
	{ 1357,	  90 },
	{ 1363,	  80 },
	{ 1369,	  70 },
	{ 1375,	  60 },
	{ 1382,	  50 },
	{ 1402,	  40 },
	{ 1422,	  30 },
	{ 1442,	  20 },
	{ 1462,	  10 },
	{ 1482,	   0 },
	{ 1519,	 -10 },
	{ 1528,	 -20 },
	{ 1546,	 -30 },
	{ 1563,	 -40 },
	{ 1587,	 -50 },
	{ 1601,	 -60 },
	{ 1614,	 -70 },
	{ 1625,  -80 },
	{ 1641,  -90 },
	{ 1663, -100 },
	{ 1678, -110 },
	{ 1693, -120 },
	{ 1705, -130 },
	{ 1720, -140 },
	{ 1736, -150 },
	{ 1751, -160 },
	{ 1767, -170 },
	{ 1782, -180 },
	{ 1798, -190 },
	{ 1815, -200 },
};
#endif
#ifdef CONFIG_TARGET_LOCALE_NTT
/* temperature table for ADC 7 */
static struct sec_bat_adc_table_data temper_table_ADC7[] =  {
	{  300,	 670 },
	{  310,	 660 },
	{  324,	 650 },
	{  330,	 640 },
	{  340,	 630 },
	{  353,	 620 },
	{  368,	 610 },
	{  394,	 600 },
	{  394,	 590 },
	{  401,	 580 },
	{  418,	 570 },
	{  431,	 560 },
	{  445,	 550 },
	{  460,	 540 },
	{  478,	 530 },
	{  496,	 520 },
	{  507,	 510 },
	{  513,	 500 },
	{  531,	 490 },
	{  553,	 480 },
	{  571,	 470 },
	{  586,	 460 },
	{  604,	 450 },
	{  614,	 440 },
	{  640,	 430 },
	{  659,	 420 },
	{  669,	 410 },
	{  707,	 400 },
	{  722,	 390 },
	{  740,	 380 },
	{  769,	 370 },
	{  783,	 360 },
	{  816,	 350 },
	{  818,	 340 },
	{  845,	 330 },
	{  859,	 320 },
	{  889,	 310 },
	{  929,	 300 },
	{  942,	 290 },
	{  955,	 280 },
	{  972,	 270 },
	{  996,	 260 },
	{ 1040,	 250 },
	{ 1049,	 240 },
	{ 1073,	 230 },
	{ 1096,	 220 },
	{ 1114,	 210 },
	{ 1159,	 200 },
	{ 1165,	 190 },
	{ 1206,	 180 },
	{ 1214,	 170 },
	{ 1227,	 160 },
	{ 1256,	 150 },
	{ 1275,	 140 },
	{ 1301,	 130 },
	{ 1308,	 120 },
	{ 1357,	 110 },
	{ 1388,	 100 },
	{ 1396,	  90 },
	{ 1430,	  80 },
	{ 1448,	  70 },
	{ 1468,	  60 },
	{ 1499,	  50 },
	{ 1506,	  40 },
	{ 1522,	  30 },
	{ 1535,	  20 },
	{ 1561,	  10 },
	{ 1567,	   0 },
	{ 1595,	 -10 },
	{ 1620,	 -20 },
	{ 1637,	 -30 },
	{ 1640,	 -40 },
	{ 1668,	 -50 },
	{ 1669,	 -60 },
	{ 1688,	 -70 },
};
#else
/* temperature table for ADC 7 */
static struct sec_bat_adc_table_data temper_table_ADC7[] = {
	{  193,	 800 },
	{  200,	 790 },
	{  207,	 780 },
	{  215,	 770 },
	{  223,	 760 },
	{  230,	 750 },
	{  238,	 740 },
	{  245,	 730 },
	{  252,	 720 },
	{  259,	 710 },
	{  266,	 700 },
	{  277,	 690 },
	{  288,	 680 },
	{  300,	 670 },
	{  311,	 660 },
	{  326,	 650 },
	{  340,	 640 },
	{  354,	 630 },
	{  368,	 620 },
	{  382,	 610 },
	{  397,	 600 },
	{  410,	 590 },
	{  423,	 580 },
	{  436,	 570 },
	{  449,	 560 },
	{  462,	 550 },
	{  475,	 540 },
	{  488,	 530 },
	{  491,	 520 },
	{  503,	 510 },
	{  535,	 500 },
	{  548,	 490 },
	{  562,	 480 },
	{  576,	 470 },
	{  590,	 460 },
	{  603,	 450 },
	{  616,	 440 },
	{  630,	 430 },
	{  646,	 420 },
	{  663,	 410 },
	{  679,	 400 },
	{  696,	 390 },
	{  712,	 380 },
	{  728,	 370 },
	{  745,	 360 },
	{  762,	 350 },
	{  784,	 340 },
	{  806,	 330 },
	{  828,	 320 },
	{  850,	 310 },
	{  872,	 300 },
	{  895,	 290 },
	{  919,	 280 },
	{  942,	 270 },
	{  966,	 260 },
	{  989,	 250 },
	{ 1013,	 240 },
	{ 1036,	 230 },
	{ 1060,	 220 },
	{ 1083,	 210 },
	{ 1107,	 200 },
	{ 1133,	 190 },
	{ 1159,	 180 },
	{ 1186,	 170 },
	{ 1212,	 160 },
	{ 1238,	 150 },
	{ 1265,	 140 },
	{ 1291,	 130 },
	{ 1316,	 120 },
	{ 1343,	 110 },
	{ 1370,	 100 },
	{ 1381,	  90 },
	{ 1393,	  80 },
	{ 1404,	  70 },
	{ 1416,	  60 },
	{ 1427,	  50 },
	{ 1453,	  40 },
	{ 1479,	  30 },
	{ 1505,	  20 },
	{ 1531,	  10 },
	{ 1557,	   0 },
	{ 1565,	 -10 },
	{ 1577,	 -20 },
	{ 1601,	 -30 },
	{ 1620,	 -40 },
	{ 1633,	 -50 },
	{ 1642,	 -60 },
	{ 1656,	 -70 },
	{ 1667,  -80 },
	{ 1674,  -90 },
	{ 1689, -100 },
	{ 1704, -110 },
	{ 1719, -120 },
	{ 1734, -130 },
	{ 1749, -140 },
	{ 1763, -150 },
	{ 1778, -160 },
	{ 1793, -170 },
	{ 1818, -180 },
	{ 1823, -190 },
	{ 1838, -200 },
};
#endif

#define ADC_CH_TEMPERATURE_PMIC	6
#define ADC_CH_TEMPERATURE_LCD	7

static unsigned int sec_bat_get_lpcharging_state(void)
{
	u32 val = __raw_readl(S5P_INFORM2);
	struct power_supply *psy = power_supply_get_by_name("max8997-charger");
	union power_supply_propval value;

	BUG_ON(!psy);

	if (val == 1) {
		psy->get_property(psy, POWER_SUPPLY_PROP_STATUS, &value);
		pr_info("%s: charging status: %d\n", __func__, value.intval);
		if (value.intval == POWER_SUPPLY_STATUS_DISCHARGING)
			pr_warn("%s: DISCHARGING\n", __func__);
	}

	pr_info("%s: LP charging:%d\n", __func__, val);
	return val;
}

static void sec_bat_initial_check(void)
{
	pr_info("%s: connected_cable_type:%d\n",
		__func__, connected_cable_type);
	if (connected_cable_type != CABLE_TYPE_NONE)
		max8997_muic_charger_cb(connected_cable_type);
}

static struct sec_bat_platform_data sec_bat_pdata = {
	.fuel_gauge_name	= "fuelgauge",
	.charger_name		= "max8997-charger",
#ifdef CONFIG_CHARGER_MAX8922_U1
	.sub_charger_name	= "max8922-charger",
#elif defined(CONFIG_MAX8903_CHARGER)
	.sub_charger_name	= "max8903-charger",
#endif
	/* TODO: should provide temperature table */
	.adc_arr_size		= ARRAY_SIZE(temper_table),
	.adc_table			= temper_table,
	.adc_channel		= ADC_CH_TEMPERATURE_PMIC,
	.adc_sub_arr_size	= ARRAY_SIZE(temper_table_ADC7),
	.adc_sub_table		= temper_table_ADC7,
	.adc_sub_channel	= ADC_CH_TEMPERATURE_LCD,
	.get_lpcharging_state	= sec_bat_get_lpcharging_state,
#if defined(CONFIG_MACH_Q1_BD)
	.initial_check		= sec_bat_initial_check,
#else
	.initial_check		= NULL,
#endif
};

static struct platform_device sec_device_battery = {
	.name = "sec-battery",
	.id = -1,
	.dev.platform_data = &sec_bat_pdata,
};
#endif /* CONFIG_BATTERY_SEC_U1 */

#ifdef CONFIG_SMB136_CHARGER_Q1
static void smb136_set_charger_name(void)
{
	sec_bat_pdata.sub_charger_name = "smb136-charger";
}

static struct smb136_platform_data smb136_pdata = {
	.topoff_cb = c1_charger_topoff_cb,
#if defined(CONFIG_MACH_Q1_CHN) && defined(CONFIG_SMB136_CHARGER_Q1)
	.ovp_cb = c1_charger_ovp_cb,
#endif
	.set_charger_name = smb136_set_charger_name,
	.gpio_chg_en = GPIO_CHG_EN,
	.gpio_otg_en = GPIO_OTG_EN,
#if defined(CONFIG_MACH_Q1_CHN) && defined(CONFIG_SMB136_CHARGER_Q1)
	.gpio_chg_ing = GPIO_CHG_ING_N,
#endif
	.gpio_ta_nconnected = 0,	/*GPIO_TA_nCONNECTED,*/
};
#endif /* CONFIG_SMB136_CHARGER_Q1 */

#ifdef CONFIG_SMB328_CHARGER
static void smb328_set_charger_name(void)
{
	sec_bat_pdata.sub_charger_name = "smb328-charger";
}

static struct smb328_platform_data smb328_pdata = {
	.topoff_cb = c1_charger_topoff_cb,
#if defined(CONFIG_MACH_Q1_CHN) && defined(CONFIG_SMB328_CHARGER)
	.ovp_cb = c1_charger_ovp_cb,
#endif
	.set_charger_name = smb328_set_charger_name,
	.gpio_chg_ing = GPIO_CHG_ING_N,
	.gpio_ta_nconnected = 0,	/*GPIO_TA_nCONNECTED,*/
};
#endif /* CONFIG_SMB328_CHARGER */

#if defined(CONFIG_SMB136_CHARGER_Q1) || defined(CONFIG_SMB328_CHARGER)
static struct i2c_gpio_platform_data gpio_i2c_data19 = {
	.sda_pin = GPIO_CHG_SDA,
	.scl_pin = GPIO_CHG_SCL,
};

static struct platform_device s3c_device_i2c19 = {
	.name = "i2c-gpio",
	.id = 19,
	.dev.platform_data = &gpio_i2c_data19,
};

static struct i2c_board_info i2c_devs19_emul[] = {
#ifdef CONFIG_SMB136_CHARGER_Q1
	{
		I2C_BOARD_INFO("smb136-charger", SMB136_SLAVE_ADDR>>1),
		.platform_data	= &smb136_pdata,
	},
#endif
#ifdef CONFIG_SMB328_CHARGER
	{
		I2C_BOARD_INFO("smb328-charger", SMB328_SLAVE_ADDR>>1),
		.platform_data	= &smb328_pdata,
	},
#endif
};
#endif

#if defined(CONFIG_SEC_THERMISTOR)
#if defined(CONFIG_MACH_Q1_BD)
/* temperature table for ADC CH 6 */
static struct sec_therm_adc_table adc_ch6_table[] = {
	/* ADC, Temperature */
	{  165,  800 },
	{  173,  790 },
	{  179,  780 },
	{  185,  770 },
	{  191,  760 },
	{  197,  750 },
	{  203,  740 },
	{  209,  730 },
	{  215,  720 },
	{  221,  710 },
	{  227,  700 },
	{  236,  690 },
	{  247,  680 },
	{  258,  670 },
	{  269,  660 },
	{  281,  650 },
	{  296,  640 },
	{  311,  630 },
	{  326,  620 },
	{  341,  610 },
	{  356,  600 },
	{  372,  590 },
	{  386,  580 },
	{  400,  570 },
	{  414,  560 },
	{  428,  550 },
	{  442,  540 },
	{  456,  530 },
	{  470,  520 },
	{  484,  510 },
	{  498,  500 },
	{  508,  490 },
	{  517,  480 },
	{  526,  470 },
	{  535,  460 },
	{  544,  450 },
	{  553,  440 },
	{  562,  430 },
	{  576,  420 },
	{  594,  410 },
	{  612,  400 },
	{  630,  390 },
	{  648,  380 },
	{  666,  370 },
	{  684,  360 },
	{  702,  350 },
	{  725,  340 },
	{  749,  330 },
	{  773,  320 },
	{  797,  310 },
	{  821,  300 },
	{  847,  290 },
	{  870,  280 },
	{  893,  270 },
	{  916,  260 },
	{  939,  250 },
	{  962,  240 },
	{  985,  230 },
	{ 1008,  220 },
	{ 1031,  210 },
	{ 1054,  200 },
	{ 1081,  190 },
	{ 1111,  180 },
	{ 1141,  170 },
	{ 1171,  160 },
	{ 1201,  150 },
	{ 1231,  140 },
	{ 1261,  130 },
	{ 1291,  120 },
	{ 1321,  110 },
	{ 1351,  100 },
	{ 1358,   90 },
	{ 1364,   80 },
	{ 1370,   70 },
	{ 1376,   60 },
	{ 1382,   50 },
	{ 1402,   40 },
	{ 1422,   30 },
	{ 1442,   20 },
	{ 1462,   10 },
	{ 1482,    0 },
	{ 1519,  -10 },
	{ 1528,  -20 },
	{ 1546,  -30 },
	{ 1563,  -40 },
	{ 1587,  -50 },
	{ 1601,  -60 },
	{ 1614,  -70 },
	{ 1625,  -80 },
	{ 1641,  -90 },
	{ 1663,  -100 },
	{ 1680,  -110 },
	{ 1695,  -120 },
	{ 1710,  -130 },
	{ 1725,  -140 },
	{ 1740,  -150 },
	{ 1755,  -160 },
	{ 1770,  -170 },
	{ 1785,  -180 },
	{ 1800,  -190 },
	{ 1815,  -200 },
};
#else
/* temperature table for ADC CH 6 */
static struct sec_therm_adc_table adc_ch6_table[] = {
	/* ADC, Temperature */
	{  173,  800 },
	{  180,  790 },
	{  188,  780 },
	{  196,  770 },
	{  204,  760 },
	{  212,  750 },
	{  220,  740 },
	{  228,  730 },
	{  236,  720 },
	{  244,  710 },
	{  252,  700 },
	{  259,  690 },
	{  266,  680 },
	{  273,  670 },
	{  289,  660 },
	{  304,  650 },
	{  314,  640 },
	{  325,  630 },
	{  337,  620 },
	{  347,  610 },
	{  361,  600 },
	{  376,  590 },
	{  391,  580 },
	{  406,  570 },
	{  417,  560 },
	{  431,  550 },
	{  447,  540 },
	{  474,  530 },
	{  491,  520 },
	{  499,  510 },
	{  511,  500 },
	{  519,  490 },
	{  547,  480 },
	{  568,  470 },
	{  585,  460 },
	{  597,  450 },
	{  614,  440 },
	{  629,  430 },
	{  647,  420 },
	{  672,  410 },
	{  690,  400 },
	{  720,  390 },
	{  735,  380 },
	{  755,  370 },
	{  775,  360 },
	{  795,  350 },
	{  818,  340 },
	{  841,  330 },
	{  864,  320 },
	{  887,  310 },
	{  909,  300 },
	{  932,  290 },
	{  954,  280 },
	{  976,  270 },
	{  999,  260 },
	{ 1021,  250 },
	{ 1051,  240 },
	{ 1077,  230 },
	{ 1103,  220 },
	{ 1129,  210 },
	{ 1155,  200 },
	{ 1177,  190 },
	{ 1199,  180 },
	{ 1220,  170 },
	{ 1242,  160 },
	{ 1263,  150 },
	{ 1284,  140 },
	{ 1306,  130 },
	{ 1326,  120 },
	{ 1349,  110 },
	{ 1369,  100 },
	{ 1390,   90 },
	{ 1411,   80 },
	{ 1433,   70 },
	{ 1454,   60 },
	{ 1474,   50 },
	{ 1486,   40 },
	{ 1499,   30 },
	{ 1512,   20 },
	{ 1531,   10 },
	{ 1548,    0 },
	{ 1570,  -10 },
	{ 1597,  -20 },
	{ 1624,  -30 },
	{ 1633,  -40 },
	{ 1643,  -50 },
	{ 1652,  -60 },
	{ 1663,  -70 },
	{ 1687,  -80 },
	{ 1711,  -90 },
	{ 1735,  -100 },
	{ 1746,  -110 },
	{ 1757,  -120 },
	{ 1768,  -130 },
	{ 1779,  -140 },
	{ 1790,  -150 },
	{ 1801,  -160 },
	{ 1812,  -170 },
	{ 1823,  -180 },
	{ 1834,  -190 },
	{ 1845,  -200 },
};
#endif

static struct sec_therm_platform_data sec_therm_pdata = {
	.adc_channel	= 6,
	.adc_arr_size	= ARRAY_SIZE(adc_ch6_table),
	.adc_table	= adc_ch6_table,
	.polling_interval = 30 * 1000, /* msecs */
};

static struct platform_device sec_device_thermistor = {
	.name = "sec-thermistor",
	.id = -1,
	.dev.platform_data = &sec_therm_pdata,
};
#endif /* CONFIG_SEC_THERMISTOR */


struct gpio_keys_button u1_buttons[] = {
	{
		.code = KEY_VOLUMEUP,
		.gpio = GPIO_VOL_UP,
		.active_low = 1,
		.type = EV_KEY,
		.wakeup = 1,
		.isr_hook = sec_debug_check_crash_key,
		.debounce_interval = 10,
	},			/* vol up */
	{
		.code = KEY_VOLUMEDOWN,
		.gpio = GPIO_VOL_DOWN,
		.active_low = 1,
		.type = EV_KEY,
		.wakeup = 1,
		.isr_hook = sec_debug_check_crash_key,
		.debounce_interval = 10,
	},			/* vol down */
	{
		.code = KEY_POWER,
		.gpio = GPIO_nPOWER,
		.active_low = 1,
		.type = EV_KEY,
		.wakeup = 1,
		.isr_hook = sec_debug_check_crash_key,
		.debounce_interval = 10,
	},			/* power key */
	{
		.code = KEY_HOME,
		.gpio = GPIO_OK_KEY,
		.active_low = 1,
		.type = EV_KEY,
		.wakeup = 1,
		.debounce_interval = 10,
	},			/* ok key */
};

struct gpio_keys_platform_data u1_keypad_platform_data = {
	u1_buttons,
	ARRAY_SIZE(u1_buttons),
};

struct platform_device u1_keypad = {
	.name = "gpio-keys",
	.dev.platform_data = &u1_keypad_platform_data,
};

#ifdef CONFIG_SEC_DEV_JACK
static void sec_set_jack_micbias(bool on)
{
#ifdef CONFIG_SND_SOC_USE_EXTERNAL_MIC_BIAS
#if defined(CONFIG_MACH_Q1_BD)
	gpio_set_value(GPIO_EAR_MIC_BIAS_EN, on);
#else
	if (system_rev >= 3)
		gpio_set_value(GPIO_EAR_MIC_BIAS_EN, on);
	else
		gpio_set_value(GPIO_MIC_BIAS_EN, on);
#endif /* #if defined(CONFIG_MACH_Q1_BD) */
#endif /* #ifdef CONFIG_SND_SOC_USE_EXTERNAL_MIC_BIAS */
}

static struct sec_jack_zone sec_jack_zones[] = {
	{
		/* adc == 0, unstable zone, default to 3pole if it stays
		 * in this range for 300ms (15ms delays, 20 samples)
		 */
		.adc_high = 0,
		.delay_ms = 15,
		.check_count = 20,
		.jack_type = SEC_HEADSET_3POLE,
	},
	{
		/* 0 < adc <= 1200, unstable zone, default to 3pole if it stays
		 * in this range for 300ms (15ms delays, 20 samples)
		 */
		.adc_high = 1200,
		.delay_ms = 10,
		.check_count = 80,
		.jack_type = SEC_HEADSET_3POLE,
	},
	{
		/* 950 < adc <= 2600, unstable zone, default to 4pole if it
		 * stays in this range for 800ms (10ms delays, 80 samples)
		 */
		.adc_high = 2600,
		.delay_ms = 10,
		.check_count = 10,
		.jack_type = SEC_HEADSET_4POLE,
	},
	{
		/* 2600 < adc <= 3400, 3 pole zone, default to 3pole if it
		 * stays in this range for 100ms (10ms delays, 10 samples)
		 */
		.adc_high = 3800,
#if defined(CONFIG_MACH_Q1_BD)
		.delay_ms = 15,
		.check_count = 20,
#else
		.delay_ms = 10,
		.check_count = 5,
#endif
		.jack_type = SEC_HEADSET_4POLE,
	},
	{
		/* adc > 3400, unstable zone, default to 3pole if it stays
		 * in this range for two seconds (10ms delays, 200 samples)
		 */
		.adc_high = 0x7fffffff,
		.delay_ms = 10,
		.check_count = 200,
		.jack_type = SEC_HEADSET_3POLE,
	},
};

/* To support 3-buttons earjack */
static struct sec_jack_buttons_zone sec_jack_buttons_zones[] = {
	{
		/* 0 <= adc <=170, stable zone */
		.code = KEY_MEDIA,
		.adc_low = 0,
#if defined(CONFIG_TARGET_LOCALE_NTT)
		.adc_high = 150,
#else
		.adc_high = 170,
#endif
	},
	{
		/* 171 <= adc <= 370, stable zone */
		.code = KEY_VOLUMEUP,
#if defined(CONFIG_TARGET_LOCALE_NTT)
		.adc_low = 151,
#else
		.adc_low = 171,
#endif
		.adc_high = 370,
	},
	{
		/* 371 <= adc <= 850, stable zone */
		.code = KEY_VOLUMEDOWN,
		.adc_low = 371,
		.adc_high = 850,
	},
};

static struct sec_jack_platform_data sec_jack_data = {
	.set_micbias_state = sec_set_jack_micbias,
	.zones = sec_jack_zones,
	.num_zones = ARRAY_SIZE(sec_jack_zones),
	.buttons_zones = sec_jack_buttons_zones,
	.num_buttons_zones = ARRAY_SIZE(sec_jack_buttons_zones),
	.det_gpio = GPIO_DET_35,
	.send_end_gpio = GPIO_EAR_SEND_END,
};

static struct platform_device sec_device_jack = {
	.name = "sec_jack",
	.id = 1,		/* will be used also for gpio_event id */
	.dev.platform_data = &sec_jack_data,
};
#endif

#ifdef CONFIG_I2C_S3C2410
/* I2C0 */
static struct i2c_board_info i2c_devs0[] __initdata = {
	{I2C_BOARD_INFO("24c128", 0x50),},	/* Samsung S524AD0XD1 */
	{I2C_BOARD_INFO("24c128", 0x52),},	/* Samsung S524AD0XD1 */
};

static struct k3g_platform_data trats_k3g_data = {
	.irq2			= IRQ_EINT(1),
	.powerdown		= K3G_POWERDOWN_NORMAL,
	.zen			= K3G_Z_EN,
	.yen			= K3G_Y_EN,
	.xen			= K3G_X_EN,
	.block_data_update	= K3G_BLOCK_DATA_UPDATE,
	.fullscale		= K3G_FULL_SCALE_500DPS,
	.fifo_mode		= K3G_FIFO_FIFO_MODE,
	.int2_src		= K3G_INT2_OVERRUN,
	.fifo_threshold		= 16,
	.int1_z_high_enable	= K3G_Z_HIGH_INT_EN,
	.int1_y_high_enable	= K3G_Y_HIGH_INT_EN,
	.int1_x_high_enable	= K3G_X_HIGH_INT_EN,
	.int1_latch		= K3G_INTERRUPT_LATCHED,
	.int1_z_threshold	= 0x12,
	.int1_y_threshold	= 0x25,
	.int1_x_threshold	= 0x25,
	.int1_wait_enable	= K3G_INT1_WAIT_EN,
	.int1_wait_duration	= 0x10,
};

static struct kr3dh_platform_data trats_kr3dh_data = {
	.power_mode		= KR3DH_LOW_POWER_ONE_HALF_HZ,
	.data_rate		= KR3DH_ODR_50HZ,
	.zen			= 1,
	.yen			= 1,
	.xen			= 1,
	.int1_latch		= 1,
	.int1_cfg		= KR3DH_INT_SOURCE,
	.block_data_update	= 1,
	.fullscale		= KR3DH_RANGE_2G,
	.int1_combination	= KR3DH_OR_COMBINATION,
	.int1_6d_enable		= 1,
	.int1_z_high_enable	= 1,
	.int1_z_low_enable	= 1,
	.int1_y_high_enable	= 1,
	.int1_y_low_enable	= 1,
	.int1_x_high_enable	= 1,
	.int1_x_low_enable	= 1,
	.int1_threshold		= 0x25,
	.int1_duration		= 0x01,
	.negate_x		= 0,
	.negate_y		= 0,
	.negate_z		= 1,
	.change_xy		= 1,
};

#ifdef CONFIG_S3C_DEV_I2C1
/* I2C1 */
static struct i2c_board_info i2c_devs1[] __initdata = {
	{
		I2C_BOARD_INFO("K3G_1", 0x69),
		.platform_data = &trats_k3g_data,
		.irq = IRQ_EINT(0),
	}, {
		/* Accelerometer */
		I2C_BOARD_INFO("KR3DH", 0x19),
		.platform_data	= &trats_kr3dh_data,
		.irq		= IRQ_EINT(24),
	},
#ifdef CONFIG_MACH_Q1_BD
	{
		I2C_BOARD_INFO("bmp180", 0x77),
	},
#endif
};
#endif
#ifdef CONFIG_S3C_DEV_I2C2
/* I2C2 */
static struct i2c_board_info i2c_devs2[] __initdata = {
};
#endif
#ifdef CONFIG_S3C_DEV_I2C3
/* I2C3 */
static struct i2c_board_info i2c_devs3[] __initdata = {
#ifdef CONFIG_TOUCHSCREEN_MELFAS_MMS
	{
		I2C_BOARD_INFO(MELFAS_TS_NAME, MELFAS_DEV_ADDR),
		.platform_data = &melfas_mms_ts_pdata_rotate,
	},
#endif
};
#endif
#ifdef CONFIG_S3C_DEV_I2C4
/* I2C4 */
static struct i2c_board_info i2c_devs4[] __initdata = {
};
#endif

#ifdef CONFIG_S3C_DEV_I2C5
/* I2C5 */
static struct i2c_board_info i2c_devs5[] __initdata = {
#ifdef CONFIG_MFD_MAX8998
	{
		I2C_BOARD_INFO("lp3974", 0x66),
		.platform_data = &s5pv310_max8998_info,
	},
#endif
#ifdef CONFIG_MFD_MAX8997
	{
		I2C_BOARD_INFO("max8997", (0xcc >> 1)),
		.platform_data = &exynos4_max8997_info,
	},
#endif
};
#endif
#ifdef CONFIG_S3C_DEV_I2C6
/* I2C6 */
static struct i2c_board_info i2c_devs6[] __initdata = {
#if defined(CONFIG_SND_SOC_SLP_TRATS_MC1N2)
	{
		I2C_BOARD_INFO("mc1n2", 0x3a),	/* MC1N2 */
		.platform_data = &mc1n2_pdata,
	},
#endif
};
#endif
#ifdef CONFIG_S3C_DEV_I2C7
static struct akm8975_platform_data akm8975_pdata = {
	.gpio_data_ready_int = GPIO_MSENSE_INT,
};

/* I2C7 */
static struct i2c_board_info i2c_devs7[] __initdata = {
	{
		I2C_BOARD_INFO("ak8975", 0x0C),
		.platform_data = &akm8975_pdata,
	},
#ifdef CONFIG_DRM_EXYNOS_HDMI
	{
		I2C_BOARD_INFO("s5p_ddc", (0x74 >> 1)),
	},
#endif
};

static void s3c_i2c7_cfg_gpio_u1(struct platform_device *dev)
{
	/* u1 magnetic sensor & MHL are using i2c7
	 * and the i2c line has external pull-resistors.
	 */
	s3c_gpio_cfgall_range(EXYNOS4_GPD0(2), 2,
		S3C_GPIO_SFN(3), S3C_GPIO_PULL_NONE);
}

static struct s3c2410_platform_i2c default_i2c7_data __initdata = {
	.bus_num	= 7,
	.flags		= 0,
	.slave_addr	= 0x10,
	.frequency	= 100*1000,
	.sda_delay	= 100,
	.cfg_gpio	= s3c_i2c7_cfg_gpio_u1,
};
#endif

#ifdef CONFIG_S3C_DEV_I2C9_EMUL
static struct i2c_gpio_platform_data gpio_i2c_data9 = {
	.sda_pin = GPIO_FUEL_SDA,
	.scl_pin = GPIO_FUEL_SCL,
};

struct platform_device s3c_device_i2c9 = {
	.name = "i2c-gpio",
	.id = 9,
	.dev.platform_data = &gpio_i2c_data9,
};

#ifdef CONFIG_BATTERY_MAX17042_FUELGAUGE_U1

struct max17042_reg_data max17042_init_data[] = {
	{ MAX17042_REG_CGAIN,		0x00,	0x00 },
	{ MAX17042_REG_MISCCFG,		0x03,	0x00 },
	{ MAX17042_REG_LEARNCFG,	0x07,	0x00 },
	/* RCOMP: 0x0050 2011.02.29 from MAXIM */
	{ MAX17042_REG_RCOMP,		0x50,	0x00 },
};

struct max17042_reg_data max17042_alert_init_data[] = {
#ifdef CONFIG_MACH_Q1_BD
	/* SALRT Threshold setting to 1% wake lock */
	{ MAX17042_REG_SALRT_TH,	0x01,	0xFF },
#elif defined(CONFIG_TARGET_LOCALE_KOR)
	/* SALRT Threshold setting to 1% wake lock */
	{ MAX17042_REG_SALRT_TH,	0x01,	0xFF },
#else
	/* SALRT Threshold setting to 2% => 1% wake lock */
	{ MAX17042_REG_SALRT_TH,	0x02,	0xFF },
#endif
	/* VALRT Threshold setting (disable) */
	{ MAX17042_REG_VALRT_TH,	0x00,	0xFF },
	/* TALRT Threshold setting (disable) */
	{ MAX17042_REG_TALRT_TH,	0x80,	0x7F },
};

bool max17042_is_low_batt(void)
{
	struct power_supply *psy = power_supply_get_by_name("battery");
	union power_supply_propval value;

	if (!psy) {
		pr_err("%s: fail to get battery ps\n", __func__);
		return -ENODEV;
	}

	if (!(psy->get_property(psy, POWER_SUPPLY_PROP_CAPACITY, &value)))
		if (value.intval > SEC_BATTERY_SOC_3_6)
			return false;

	return true;
}
EXPORT_SYMBOL(max17042_is_low_batt);

static int max17042_low_batt_cb(void)
{
	struct power_supply *psy = power_supply_get_by_name("battery");
	union power_supply_propval value;

	if (!psy) {
		pr_err("%s: fail to get battery ps\n", __func__);
		return -ENODEV;
	}

	value.intval = POWER_SUPPLY_CAPACITY_LEVEL_CRITICAL;
	return psy->set_property(psy, POWER_SUPPLY_PROP_CAPACITY_LEVEL, &value);
}

#ifdef RECAL_SOC_FOR_MAXIM
static bool max17042_need_soc_recal(void)
{
	pr_info("%s: HW(0x%x)\n", __func__, system_rev);

	if (system_rev >= NO_NEED_RECAL_SOC_HW_REV)
		return false;
	else
		return true;
}
#endif

static struct max17042_platform_data s5pv310_max17042_info = {
	.low_batt_cb = max17042_low_batt_cb,
	.init = max17042_init_data,
	.init_size = sizeof(max17042_init_data),
	.alert_init = max17042_alert_init_data,
	.alert_init_size = sizeof(max17042_alert_init_data),
	.alert_gpio = GPIO_FUEL_ALERT,
	.alert_irq = 0,
	.enable_current_sense = false,
	.enable_gauging_temperature = true,
#ifdef RECAL_SOC_FOR_MAXIM
	.need_soc_recal = max17042_need_soc_recal,
#endif
};
#endif	/* CONFIG_BATTERY_MAX17042_U1 */

/* I2C9 */
static struct i2c_board_info i2c_devs9_emul[] __initdata = {
#ifdef CONFIG_BATTERY_MAX17042_FUELGAUGE_U1
	{
		I2C_BOARD_INFO("max17042", 0x36),
		.platform_data	= &s5pv310_max17042_info,
		.irq = IRQ_EINT(19),
	},
#endif
#ifdef CONFIG_BATTERY_MAX17040
	{
		I2C_BOARD_INFO("max17040", 0x36),
	},
#endif
};
#endif

#ifdef CONFIG_S3C_DEV_I2C10_EMUL
static struct i2c_gpio_platform_data gpio_i2c_data10 __initdata = {
	.sda_pin = GPIO_USB_SDA,
	.scl_pin = GPIO_USB_SCL,
};

struct platform_device s3c_device_i2c10 = {
	.name = "i2c-gpio",
	.id = 10,
	.dev.platform_data = &gpio_i2c_data10,
};

/* I2C10 */
static struct fsa9480_platform_data fsa9480_info = {
};

static struct i2c_board_info i2c_devs10_emul[] __initdata = {
	{
		I2C_BOARD_INFO("fsa9480", 0x25),
		.platform_data = &fsa9480_info,
	},
};
#endif

static struct regulator_consumer_supply supplies_ps_on_led_a[] = {
	REGULATOR_SUPPLY("led_a_2.8v", NULL),
};
static struct regulator_init_data ps_on_led_a_data = {
	.constraints = {
		.name = "LED_A_2.8V",
		.valid_ops_mask	= REGULATOR_CHANGE_STATUS,
		.boot_on = 0,
		.state_mem = {
			.enabled = 1,
			.disabled = 0,
		},
	},
	.num_consumer_supplies = ARRAY_SIZE(supplies_ps_on_led_a),
	.consumer_supplies = supplies_ps_on_led_a,
};
static struct fixed_voltage_config ps_on_led_a_pdata = {
	.supply_name = "LED_A_2.8V",
	.microvolts = 2800000,
	.gpio = EXYNOS4210_GPE2(3), /* PS_ON */
	.enable_high = 1,
	.enabled_at_boot = 0,
	.init_data = &ps_on_led_a_data,
};
static struct platform_device ps_on_led_a_fixed_reg_dev = {
	.name = "reg-fixed-voltage",
	.id = FIXED_REG_ID_LED_A,
	.dev = { .platform_data = &ps_on_led_a_pdata },
};

#ifdef CONFIG_S3C_DEV_I2C11_EMUL
static struct i2c_gpio_platform_data gpio_i2c_data11 = {
	.sda_pin = GPIO_PS_ALS_SDA,
	.scl_pin = GPIO_PS_ALS_SCL,
};

struct platform_device s3c_device_i2c11 = {
	.name = "i2c-gpio",
	.id = 11,
	.dev.platform_data = &gpio_i2c_data11,
};

/* I2C11 */
static int gp2a_leda_on(bool enable)
{
	struct regulator *regulator;
	DEFINE_MUTEX(lock);
	int ret = 0;

	pr_info("%s, enable = %d\n", __func__, enable);
	mutex_lock(&lock);

	regulator = regulator_get(NULL, "led_a_2.8v");
	WARN(IS_ERR_OR_NULL(regulator), "%s cannot get regulator\n", __func__);
	if (IS_ERR_OR_NULL(regulator)) {
		regulator = NULL;
		ret = -ENODEV;
		goto leda_out;
	}

	if (regulator_is_enabled(regulator))
		regulator_force_disable(regulator);

	if (enable)
		regulator_enable(regulator);

leda_out:
	if (regulator)
		regulator_put(regulator);
	mutex_unlock(&lock);
	return ret;
}

static int gp2a_get_threshold(void)
{
	int new_threshold = 7; /* LTH value */

	if (system_rev == 2)	/* U1HD Linchbox board is not calibrated */
		new_threshold = 100;

	return new_threshold;
}

static struct gp2a_platform_data trats_gp2a_pdata = {
	.gp2a_led_on = gp2a_leda_on,
	.p_out = GPIO_PS_ALS_INT,
	.gp2a_get_threshold = gp2a_get_threshold,
};

static struct platform_device opt_gp2a = {
	.name = "gp2a-opt",
	.id = -1,
	.dev = {
		.platform_data = &trats_gp2a_pdata,
	},
};

static struct i2c_board_info i2c_devs11_emul[] __initdata = {
	{
		I2C_BOARD_INFO("gp2a", (0x72 >> 1)),
	},
};
#endif

#ifdef CONFIG_S3C_DEV_I2C14_EMUL
static struct i2c_gpio_platform_data i2c14_platdata = {
	.sda_pin = GPIO_NFC_SDA,
	.scl_pin = GPIO_NFC_SCL,
	.udelay = 2,
	.sda_is_open_drain = 0,
	.scl_is_open_drain = 0,
	.scl_is_output_only = 0,
};

static struct platform_device s3c_device_i2c14 = {
	.name = "i2c-gpio",
	.id = 14,
	.dev.platform_data = &i2c14_platdata,
};

static struct pn544_i2c_platform_data pn544_pdata = {
	.irq_gpio = GPIO_NFC_IRQ,
	.ven_gpio = GPIO_NFC_EN,
	.firm_gpio = GPIO_NFC_FIRM,
};

static struct i2c_board_info i2c_devs14[] __initdata = {
	{
		I2C_BOARD_INFO("pn544", 0x2b),
		.irq = IRQ_EINT(15),
		.platform_data = &pn544_pdata,
	},
};

static unsigned int nfc_gpio_table[][4] = {
	{GPIO_NFC_IRQ, S3C_GPIO_INPUT, GPIO_LEVEL_NONE, S3C_GPIO_PULL_DOWN},
	{GPIO_NFC_EN, S3C_GPIO_OUTPUT, GPIO_LEVEL_LOW, S3C_GPIO_PULL_NONE},
	{GPIO_NFC_FIRM, S3C_GPIO_OUTPUT, GPIO_LEVEL_LOW, S3C_GPIO_PULL_NONE},
/*	{GPIO_NFC_SCL, S3C_GPIO_INPUT, GPIO_LEVEL_NONE, S3C_GPIO_PULL_NONE}, */
/*	{GPIO_NFC_SDA, S3C_GPIO_INPUT, GPIO_LEVEL_NONE, S3C_GPIO_PULL_NONE}, */
};

void nfc_setup_gpio(void)
{
	/* s3c_config_gpio_alive_table(ARRAY_SIZE(nfc_gpio_table),
	   nfc_gpio_table); */
	int array_size = ARRAY_SIZE(nfc_gpio_table);
	u32 i, gpio;
	for (i = 0; i < array_size; i++) {
		gpio = nfc_gpio_table[i][0];
		s3c_gpio_cfgpin(gpio, S3C_GPIO_SFN(nfc_gpio_table[i][1]));
		s3c_gpio_setpull(gpio, nfc_gpio_table[i][3]);
		if (nfc_gpio_table[i][2] != GPIO_LEVEL_NONE)
			gpio_set_value(gpio, nfc_gpio_table[i][2]);
	}

	/* s3c_gpio_cfgpin(GPIO_NFC_IRQ, EINT_MODE); */
	/* s3c_gpio_setpull(GPIO_NFC_IRQ, S3C_GPIO_PULL_DOWN); */
}
#endif

#if defined(CONFIG_VIDEO_S5K5BAFX)
static struct i2c_gpio_platform_data i2c12_platdata = {
	.sda_pin = VT_CAM_SDA_18V,
	.scl_pin = VT_CAM_SCL_18V,
	.udelay = 2,		/* 250KHz */
	.sda_is_open_drain = 0,
	.scl_is_open_drain = 0,
	.scl_is_output_only = 0,
};

static struct platform_device s3c_device_i2c12 = {
	.name = "i2c-gpio",
	.id = 12,
	.dev.platform_data = &i2c12_platdata,
};

/* I2C12 */
static struct i2c_board_info i2c_devs12_emul[] __initdata = {
	/* need to work here */
};
#endif

#ifdef CONFIG_FM_SI4709_MODULE
static struct i2c_gpio_platform_data i2c16_platdata = {
	.sda_pin		= GPIO_FM_SDA_28V,
	.scl_pin		= GPIO_FM_SCL_28V,
	.udelay			= 2,	/* 250KHz */
	.sda_is_open_drain	= 0,
	.scl_is_open_drain	= 0,
	.scl_is_output_only	= 0,
};

static struct platform_device s3c_device_i2c16 = {
	.name					= "i2c-gpio",
	.id						= 16,
	.dev.platform_data	= &i2c16_platdata,
};

static struct i2c_board_info i2c_devs16[] __initdata = {
	{
		I2C_BOARD_INFO("Si4709", (0x20 >> 1)),
	},
};
#endif

#endif

#ifdef	CONFIG_ISDBT_FC8100
static struct	i2c_board_info i2c_devs17[]	__initdata = {
	{
		I2C_BOARD_INFO("isdbti2c", 0x77),
	},
};

static struct i2c_gpio_platform_data i2c17_platdata = {
	.sda_pin		= GPIO_ISDBT_SDA_28V,
	.scl_pin		= GPIO_ISDBT_SCL_28V,
	.udelay			= 3,	/*  kHz	*/
	.sda_is_open_drain	= 0,
	.scl_is_open_drain	= 0,
	.scl_is_output_only = 0,
};

static struct platform_device s3c_device_i2c17 = {
	.name			= "i2c-gpio",
	.id				= 17,
	.dev.platform_data	= &i2c17_platdata,
};
#endif

#ifdef CONFIG_ANDROID_PMEM
static struct android_pmem_platform_data pmem_pdata = {
	.name = "pmem",
	.no_allocator = 1,
	.cached = 0,
	.start = 0,
	.size = 0
};

static struct android_pmem_platform_data pmem_gpu1_pdata = {
	.name = "pmem_gpu1",
	.no_allocator = 1,
	.cached = 0,
	.start = 0,
	.size = 0,
};

static struct platform_device pmem_device = {
	.name = "android_pmem",
	.id = 0,
	.dev = {
		.platform_data = &pmem_pdata},
};

static struct platform_device pmem_gpu1_device = {
	.name = "android_pmem",
	.id = 1,
	.dev = {
		.platform_data = &pmem_gpu1_pdata},
};

static void __init android_pmem_set_platdata(void)
{
#if defined(CONFIG_S5P_MEM_CMA)
	pmem_pdata.size = CONFIG_ANDROID_PMEM_MEMSIZE_PMEM * SZ_1K;
	pmem_gpu1_pdata.size = CONFIG_ANDROID_PMEM_MEMSIZE_PMEM_GPU1 * SZ_1K;
#else
	pmem_pdata.start = (u32) s5p_get_media_memory_bank(S5P_MDEV_PMEM, 0);
	pmem_pdata.size = (u32) s5p_get_media_memsize_bank(S5P_MDEV_PMEM, 0);
	pmem_gpu1_pdata.start =
		(u32) s5p_get_media_memory_bank(S5P_MDEV_PMEM_GPU1, 0);
	pmem_gpu1_pdata.size =
		(u32) s5p_get_media_memsize_bank(S5P_MDEV_PMEM_GPU1, 0);
#endif
}
#endif

/* USB EHCI */
#ifdef CONFIG_USB_EHCI_S5P
static struct s5p_ehci_platdata smdkc210_ehci_pdata;

static void __init smdkc210_ehci_init(void)
{
	struct s5p_ehci_platdata *pdata = &smdkc210_ehci_pdata;

	s5p_ehci_set_platdata(pdata);
}
#endif

#ifdef CONFIG_USB_OHCI_S5P
static struct s5p_ohci_platdata smdkc210_ohci_pdata;

static void __init smdkc210_ohci_init(void)
{
	struct s5p_ohci_platdata *pdata = &smdkc210_ohci_pdata;

	s5p_ohci_set_platdata(pdata);
}
#endif

/* USB GADGET */
#ifdef CONFIG_USB_GADGET
static struct s5p_usbgadget_platdata smdkc210_usbgadget_pdata;

static void __init smdkc210_usbgadget_init(void)
{
	struct s5p_usbgadget_platdata *pdata = &smdkc210_usbgadget_pdata;

	s5p_usbgadget_set_platdata(pdata);

	pdata = s3c_device_usbgadget.dev.platform_data;
	if (pdata) {
		/* Enables HS Transmitter pre-emphasis [20] */
		pdata->phy_tune_mask = 0;
		pdata->phy_tune_mask |= (0x1 << 20);
		pdata->phy_tune |= (0x1 << 20);

#if defined(CONFIG_MACH_U1_KOR_SKT) || defined(CONFIG_MACH_U1_KOR_KT)
		/* Squelch Threshold Tune [13:11] (101 : -10%) */
		pdata->phy_tune_mask |= (0x7 << 11);
		pdata->phy_tune |= (0x5 << 11);

		/* HS DC Voltage Level Adjustment [3:0] (1011 : +16%) */
		pdata->phy_tune_mask |= 0xf;
		pdata->phy_tune |= 0xb;
#elif defined(CONFIG_MACH_U1_KOR_LGT)
		/* Squelch Threshold Tune [13:11] (100 : -5%) */
		pdata->phy_tune_mask |= (0x7 << 11);
		pdata->phy_tune |= (0x4 << 11);

		/* HS DC Voltage Level Adjustment [3:0] (1100 : +18%) */
		pdata->phy_tune_mask |= 0xf;
		pdata->phy_tune |= 0xc;
#else
		/* Squelch Threshold Tune [13:11] (101 : -10%) */
		pdata->phy_tune_mask |= (0x7 << 11);
		pdata->phy_tune |= (0x5 << 11);
		/* HS DC Voltage Level Adjustment [3:0] (1011 : +16%) */
		pdata->phy_tune_mask |= 0xf;
		pdata->phy_tune |= 0xb;
#endif

		printk(KERN_DEBUG "usb: %s tune_mask=0x%x, tune=0x%x\n",
			__func__, pdata->phy_tune_mask, pdata->phy_tune);
	}
}
#endif

#ifdef CONFIG_USB_G_SLP
#include <linux/usb/slp_multi.h>
static struct slp_multi_func_data midas_slp_multi_funcs[] = {
	{
		.name = "mtp",
		.usb_config_id = USB_CONFIGURATION_DUAL,
	}, {
		.name = "acm",
		.usb_config_id = USB_CONFIGURATION_2,
	}, {
		.name = "sdb",
		.usb_config_id = USB_CONFIGURATION_2,
	}, {
		.name = "mass_storage",
		.usb_config_id = USB_CONFIGURATION_1,
	}, {
		.name = "rndis",
		.usb_config_id = USB_CONFIGURATION_1,
	},
};

static struct slp_multi_platform_data midas_slp_multi_pdata = {
	.nluns	= 2,
	.funcs = midas_slp_multi_funcs,
	.nfuncs = ARRAY_SIZE(midas_slp_multi_funcs),
};

static struct platform_device midas_slp_usb_multi = {
	.name		= "slp_multi",
	.id			= -1,
	.dev		= {
		.platform_data = &midas_slp_multi_pdata,
	},
};
#endif

#ifdef CONFIG_BUSFREQ_OPP
/* BUSFREQ to control memory/bus*/
static struct device_domain busfreq;

static struct platform_device exynos4_busfreq = {
	.id = -1,
	.name = "exynos-busfreq",
};
#endif

#ifdef CONFIG_SEC_WATCHDOG_RESET
static struct platform_device watchdog_reset_device = {
	.name = "watchdog-reset",
	.id = -1,
};
#endif

static struct platform_device *smdkc210_devices[] __initdata = {
#ifdef CONFIG_SEC_WATCHDOG_RESET
	&watchdog_reset_device,
#endif
	&exynos4_device_pd[PD_MFC],
	&exynos4_device_pd[PD_G3D],
	&exynos4_device_pd[PD_LCD0],
	&exynos4_device_pd[PD_LCD1],
	&exynos4_device_pd[PD_CAM],
	&exynos4_device_pd[PD_TV],
	&exynos4_device_pd[PD_GPS],

#ifdef CONFIG_BATTERY_SAMSUNG
	&samsung_device_battery,
#endif

#ifdef CONFIG_DRM_EXYNOS_FIMD
	&s5p_device_fimd0,
#endif
#ifdef CONFIG_DRM_EXYNOS_HDMI
	&s5p_device_i2c_hdmiphy,
	&s5p_device_hdmi,
	&s5p_device_mixer,
	&exynos_drm_hdmi_device,
#endif
	&exynos_drm_vidi_device,
#ifdef CONFIG_DRM_EXYNOS_G2D
	&s5p_device_fimg2d,
#endif
#ifdef CONFIG_DRM_EXYNOS
	&exynos_drm_device,
#endif
#ifdef CONFIG_I2C_S3C2410
	&s3c_device_i2c0,
#if defined(CONFIG_S3C_DEV_I2C1)
	&s3c_device_i2c1,
#endif
#if defined(CONFIG_S3C_DEV_I2C2)
	&s3c_device_i2c2,
#endif
#if defined(CONFIG_S3C_DEV_I2C3)
	&s3c_device_i2c3,
#endif
#if defined(CONFIG_S3C_DEV_I2C4)
	&s3c_device_i2c4,
#endif
#if defined(CONFIG_S3C_DEV_I2C5)
	&s3c_device_i2c5,
#endif
#if defined(CONFIG_S3C_DEV_I2C6)
	&s3c_device_i2c6,
#endif
#if defined(CONFIG_S3C_DEV_I2C7)
	&s3c_device_i2c7,
#endif
#if defined(CONFIG_S3C_DEV_I2C9_EMUL)
	&s3c_device_i2c9,
#endif
#if defined(CONFIG_S3C_DEV_I2C10_EMUL)
	&s3c_device_i2c10,
#endif
#if defined(CONFIG_S3C_DEV_I2C11_EMUL)
	&s3c_device_i2c11,
#endif
#if defined(CONFIG_S3C_DEV_I2C14_EMUL)
	&s3c_device_i2c14,
#endif
#if defined(CONFIG_VIDEO_S5K5BAFX)
	&s3c_device_i2c12,
#endif
#ifdef CONFIG_SAMSUNG_MHL
	&s3c_device_i2c15,
#endif
#ifdef CONFIG_JACK_MON
	&trats_jack,
#endif
#ifdef CONFIG_FM_SI4709_MODULE
	&s3c_device_i2c16,
#endif
#ifdef	CONFIG_ISDBT_FC8100
	&s3c_device_i2c17,	/* ISDBT */
#endif
#if defined(CONFIG_SMB136_CHARGER_Q1) || defined(CONFIG_SMB328_CHARGER)
	&s3c_device_i2c19,	/* SMB136, SMB328 */
#endif
#endif

	/* consumer driver should resume after resuming i2c drivers */
	&u1_regulator_consumer,

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
#ifdef CONFIG_S3C_ADC
	&s3c_device_adc,
#endif
#ifdef CONFIG_UART_SELECT
	&trats_uart_select,
#endif
	&u1_keypad,
	&s3c_device_rtc,
	&s3c_device_wdt,
#ifdef CONFIG_SND_SAMSUNG_AC97
	&exynos_device_ac97,
#endif
#ifdef CONFIG_SND_SAMSUNG_I2S
	&exynos_device_i2s0,
#endif
#ifdef CONFIG_SND_SAMSUNG_PCM
	&exynos_device_pcm0,
#endif
#if defined(CONFIG_SND_SAMSUNG_RP) || defined(CONFIG_SND_SAMSUNG_ALP)
	&exynos_device_srp,
#endif
#ifdef CONFIG_SND_SAMSUNG_SPDIF
	&exynos_device_spdif,
#endif
#ifdef CONFIG_BATTERY_SEC_U1
	&sec_device_battery,
#endif
#ifdef	CONFIG_LEDS_MAX8997
	&sec_device_leds_max8997,
#endif
#ifdef CONFIG_CHARGER_MAX8922_U1
	&max8922_device_charger,
#endif
#ifdef CONFIG_S5P_SYSTEM_MMU
#ifdef CONFIG_DRM_EXYNOS_FIMD
	&SYSMMU_PLATDEV(fimd0),
#endif
#ifdef CONFIG_DRM_EXYNOS_G2D
	&SYSMMU_PLATDEV(g2d_acp),
#endif
	&SYSMMU_PLATDEV(fimc0),
	&SYSMMU_PLATDEV(fimc1),
	&SYSMMU_PLATDEV(fimc2),
	&SYSMMU_PLATDEV(fimc3),
	&SYSMMU_PLATDEV(tv),
	&SYSMMU_PLATDEV(mfc_l),
	&SYSMMU_PLATDEV(mfc_r),
#endif
#ifdef CONFIG_ION_EXYNOS
	&exynos_device_ion,
#endif

	&samsung_asoc_dma,
#ifndef CONFIG_SND_SOC_SAMSUNG_USE_DMA_WRAPPER
	&samsung_asoc_idma,
#endif

#ifdef CONFIG_S3C64XX_DEV_SPI
	&exynos_device_spi0,
#endif

#ifdef CONFIG_PHONE_IPC_SPI
	&ipc_spi_device,
#endif

#ifdef CONFIG_ANDROID_PMEM
	&pmem_device,
	&pmem_gpu1_device,
#endif
#ifdef CONFIG_VIDEO_FIMC
	&s3c_device_fimc0,
	&s3c_device_fimc1,
	&s3c_device_fimc2,
	&s3c_device_fimc3,
#ifdef CONFIG_VIDEO_FIMC_MIPI
	&s3c_device_csis0,
	&s3c_device_csis1,
#endif
#endif
#if defined(CONFIG_VIDEO_MFC5X) || defined(CONFIG_VIDEO_SAMSUNG_S5P_MFC)
	&s5p_device_mfc,
#endif
#ifdef CONFIG_VIDEO_JPEG
	&s5p_device_jpeg,
#endif
#if defined CONFIG_USB_EHCI_S5P && !defined CONFIG_LINK_DEVICE_HSIC
	&s5p_device_ehci,
#endif
#ifdef CONFIG_USB_OHCI_S5P
	&s5p_device_ohci,
#endif
#ifdef CONFIG_USB_GADGET
	&s3c_device_usbgadget,
#endif
#ifdef CONFIG_USB_G_SLP
	&midas_slp_usb_multi,
#endif
#ifdef CONFIG_HAVE_PWM
	&s3c_device_timer[0],
	&s3c_device_timer[1],
	&s3c_device_timer[2],
	&s3c_device_timer[3],
#endif
#ifdef CONFIG_VIDEO_TSI
	&s3c_device_tsi,
#endif
#ifdef CONFIG_EXYNOS4_SETUP_THERMAL
	&s5p_device_tmu,
#endif
#ifdef CONFIG_BT_BCM4330
	&bcm4330_bluetooth_device,
#endif
#ifdef CONFIG_S5P_DEV_ACE
	&s5p_device_ace,
#endif
#ifdef CONFIG_BUSFREQ_OPP
	&exynos4_busfreq,
#endif
#ifdef CONFIG_SEC_DEV_JACK
	&sec_device_jack,
#endif
#ifdef CONFIG_USB_HOST_NOTIFY
	&host_notifier_device,
#endif
	&s3c_device_usb_otghcd,
#ifdef CONFIG_GPS_GSD4T
	&u1_gsd4t,
#endif
	&ps_on_led_a_fixed_reg_dev,
	&opt_gp2a,
};

#ifdef CONFIG_EXYNOS4_SETUP_THERMAL
/* below temperature base on the celcius degree */
struct s5p_platform_tmu u1_tmu_data __initdata = {
	.ts = {
		.stop_1st_throttle  = 61,
		.start_1st_throttle = 64,
		.stop_2nd_throttle  = 87,
		.start_2nd_throttle = 103,
		.start_tripping     = 110,
		.start_emergency    = 120,
		.stop_mem_throttle  = 80,
		.start_mem_throttle = 85,
	},
	.cpufreq = {
		.limit_1st_throttle  = 800000, /* 800MHz in KHz order */
		.limit_2nd_throttle  = 200000, /* 200MHz in KHz order */
	},
};
#endif

#if defined CONFIG_USB_EHCI_S5P && defined CONFIG_LINK_DEVICE_HSIC
static int __init s5p_ehci_device_initcall(void)
{
	return platform_device_register(&s5p_device_ehci);
}
late_initcall(s5p_ehci_device_initcall);
#endif

#if defined(CONFIG_S5P_MEM_CMA)
static void __init exynos4_cma_region_reserve(struct cma_region *regions_normal,
					      struct cma_region *regions_secure)
{
	struct cma_region *reg;
	size_t size_secure = 0, align_secure = 0;
	phys_addr_t paddr = 0;

	for (reg = regions_normal; reg->size != 0; reg++) {
		if (WARN_ON(cma_early_region_register(reg)))
			continue;

		if ((reg->alignment & (reg->alignment - 1)) || reg->reserved)
			continue;

		if (reg->start) {
			if (!memblock_is_region_reserved(reg->start, reg->size)
			    && memblock_reserve(reg->start, reg->size) >= 0)
				reg->reserved = 1;
		} else {
			paddr = __memblock_alloc_base(reg->size, reg->alignment,
					MEMBLOCK_ALLOC_ACCESSIBLE);
			if (paddr) {
				reg->start = paddr;
				reg->reserved = 1;
			}
		}
	}

	if (regions_secure && regions_secure->size) {
		for (reg = regions_secure; reg->size != 0; reg++)
			size_secure += reg->size;

		reg--;

		align_secure = reg->alignment;
		BUG_ON(align_secure & (align_secure - 1));

		paddr -= size_secure;
		paddr &= ~(align_secure - 1);

		if (!memblock_reserve(paddr, size_secure)) {
			do {
				reg->start = paddr;
				reg->reserved = 1;
				paddr += reg->size;

				if (WARN_ON(cma_early_region_register(reg)))
					memblock_free(reg->start, reg->size);
			} while (reg-- != regions_secure);
		}
	}
}

static void __init exynos4_reserve_mem(void)
{
	static struct cma_region regions[] = {
	/*
	 * caution : do not allowed other region definitions above of drm.
	 * drm only using region 0 for startup screen display.
	 */
#ifdef CONFIG_DRM_EXYNOS
		{
			.name = "drm",
			.size = CONFIG_DRM_EXYNOS_MEMSIZE * SZ_1K,
			.start = 0
		},
#endif
#ifdef CONFIG_ANDROID_PMEM_MEMSIZE_PMEM
		{
			.name = "pmem",
			.size = CONFIG_ANDROID_PMEM_MEMSIZE_PMEM * SZ_1K,
			.start = 0,
		},
#endif
#ifdef CONFIG_ANDROID_PMEM_MEMSIZE_PMEM_GPU1
		{
			.name = "pmem_gpu1",
			.size = CONFIG_ANDROID_PMEM_MEMSIZE_PMEM_GPU1 * SZ_1K,
			.start = 0,
		},
#endif
#ifdef CONFIG_VIDEO_SAMSUNG_MEMSIZE_FIMC0
		{
			.name = "fimc0",
			.size = CONFIG_VIDEO_SAMSUNG_MEMSIZE_FIMC0 * SZ_1K,
			.start = 0,
		},
#endif
#ifndef CONFIG_ION_EXYNOS_CONTIGHEAP_SIZE
#ifdef CONFIG_VIDEO_SAMSUNG_MEMSIZE_FIMC1
		{
			.name = "fimc1",
			.size = CONFIG_VIDEO_SAMSUNG_MEMSIZE_FIMC1 * SZ_1K,
			.start = 0,
		},
#endif
#endif
#ifdef CONFIG_VIDEO_SAMSUNG_MEMSIZE_FIMC2
		{
			.name = "fimc2",
			.size = CONFIG_VIDEO_SAMSUNG_MEMSIZE_FIMC2 * SZ_1K,
			.start = 0,
		},
#endif
#ifdef CONFIG_VIDEO_SAMSUNG_MEMSIZE_FIMC3
		{
			.name = "fimc3",
			.size = CONFIG_VIDEO_SAMSUNG_MEMSIZE_FIMC3 * SZ_1K,
			.start = 0,
		},
#endif
#ifdef CONFIG_ION_EXYNOS_CONTIGHEAP_SIZE
		{
			.name   = "ion",
			.size   = CONFIG_ION_EXYNOS_CONTIGHEAP_SIZE * SZ_1K,
		},
#endif
#ifdef CONFIG_VIDEO_SAMSUNG_MEMSIZE_MFC1
		{
			.name = "mfc1",
			.size = CONFIG_VIDEO_SAMSUNG_MEMSIZE_MFC1 * SZ_1K,
			{
				.alignment = 1 << 17,
			},
			.start = 0,
		},
#endif
#ifdef CONFIG_VIDEO_SAMSUNG_MEMSIZE_MFC0
		{
			.name = "mfc0",
			.size = CONFIG_VIDEO_SAMSUNG_MEMSIZE_MFC0 * SZ_1K,
			{
				.alignment = 1 << 17,
			},
			.start = 0,
		},
#endif
#ifdef CONFIG_VIDEO_SAMSUNG_MEMSIZE_MFC
		{
			.name = "mfc",
			.size = CONFIG_VIDEO_SAMSUNG_MEMSIZE_MFC * SZ_1K,
			{
				.alignment = 1 << 17,
			},
			.start = 0,
		},
#endif
#ifdef CONFIG_VIDEO_SAMSUNG_MEMSIZE_JPEG
		{
			.name = "jpeg",
			.size = CONFIG_VIDEO_SAMSUNG_MEMSIZE_JPEG * SZ_1K,
			.start = 0,
		},
#endif
#ifdef CONFIG_AUDIO_SAMSUNG_MEMSIZE_SRP
		{
			.name = "srp",
			.size = CONFIG_AUDIO_SAMSUNG_MEMSIZE_SRP * SZ_1K,
			.start = 0,
		},
#endif
		{
			.size = 0,
		},
	};

	static const char map[] __initconst =
#ifdef CONFIG_DRM_EXYNOS
		"exynos-drm=drm;"
#endif
		"android_pmem.0=pmem;android_pmem.1=pmem_gpu1;"
		"s3c-fimc.0=fimc0;s3c-fimc.1=fimc1;s3c-fimc.2=fimc2;s3c-fimc.3=fimc3;"
		"exynos4210-fimc.0=fimc0;exynos4210-fimc.1=fimc1;exynos4210-fimc.2=fimc2;exynos4210-fimc3=fimc3;"
#ifdef CONFIG_ION_EXYNOS
		"ion-exynos=ion;"
#endif
#ifdef CONFIG_VIDEO_MFC5X
		"s3c-mfc/A=mfc0,mfc-secure;"
		"s3c-mfc/B=mfc1,mfc-normal;"
		"s3c-mfc/AB=mfc;"
#endif
		"samsung-rp=srp;"
		"s5p-jpeg=jpeg;"
#ifdef CONFIG_VIDEO_EXYNOS_FIMC_IS
		"exynos4-fimc-is=fimc_is;"
#endif
		;

	cma_set_defaults(regions, map);
	exynos4_cma_region_reserve(regions, NULL);

}
#endif

static void __init exynos_sysmmu_init(void)
{
	ASSIGN_SYSMMU_POWERDOMAIN(fimc0, &exynos4_device_pd[PD_CAM].dev);
	ASSIGN_SYSMMU_POWERDOMAIN(fimc1, &exynos4_device_pd[PD_CAM].dev);
	ASSIGN_SYSMMU_POWERDOMAIN(fimc2, &exynos4_device_pd[PD_CAM].dev);
	ASSIGN_SYSMMU_POWERDOMAIN(fimc3, &exynos4_device_pd[PD_CAM].dev);
	ASSIGN_SYSMMU_POWERDOMAIN(jpeg, &exynos4_device_pd[PD_CAM].dev);
	ASSIGN_SYSMMU_POWERDOMAIN(fimd0, &exynos4_device_pd[PD_LCD0].dev);
	ASSIGN_SYSMMU_POWERDOMAIN(rot, &exynos4_device_pd[PD_LCD0].dev);
	ASSIGN_SYSMMU_POWERDOMAIN(tv, &exynos4_device_pd[PD_TV].dev);
	ASSIGN_SYSMMU_POWERDOMAIN(mfc_l, &exynos4_device_pd[PD_MFC].dev);
	ASSIGN_SYSMMU_POWERDOMAIN(mfc_r, &exynos4_device_pd[PD_MFC].dev);
#if defined CONFIG_VIDEO_FIMC
	sysmmu_set_owner(&SYSMMU_PLATDEV(fimc0).dev, &s3c_device_fimc0.dev);
	sysmmu_set_owner(&SYSMMU_PLATDEV(fimc1).dev, &s3c_device_fimc1.dev);
	sysmmu_set_owner(&SYSMMU_PLATDEV(fimc2).dev, &s3c_device_fimc2.dev);
	sysmmu_set_owner(&SYSMMU_PLATDEV(fimc3).dev, &s3c_device_fimc3.dev);
#elif defined CONFIG_VIDEO_SAMSUNG_S5P_FIMC
	sysmmu_set_owner(&SYSMMU_PLATDEV(fimc0).dev, &s5p_device_fimc0.dev);
	sysmmu_set_owner(&SYSMMU_PLATDEV(fimc1).dev, &s5p_device_fimc1.dev);
	sysmmu_set_owner(&SYSMMU_PLATDEV(fimc2).dev, &s5p_device_fimc2.dev);
	sysmmu_set_owner(&SYSMMU_PLATDEV(fimc3).dev, &s5p_device_fimc3.dev);
#endif
#ifdef CONFIG_VIDEO_JPEG
	sysmmu_set_owner(&SYSMMU_PLATDEV(jpeg).dev, &s5p_device_jpeg.dev);
#endif
#ifdef CONFIG_DRM_EXYNOS_FIMD
	sysmmu_set_owner(&SYSMMU_PLATDEV(fimd0).dev, &s5p_device_fimd0.dev);
#endif
#ifdef CONFIG_DRM_EXYNOS_HDMI
	sysmmu_set_owner(&SYSMMU_PLATDEV(tv).dev, &s5p_device_hdmi.dev);
#endif
#ifdef CONFIG_DRM_EXYNOS_G2D
	sysmmu_set_owner(&SYSMMU_PLATDEV(g2d_acp).dev, &s5p_device_fimg2d.dev);
#endif
#if defined(CONFIG_VIDEO_MFC5X) || defined(CONFIG_VIDEO_SAMSUNG_S5P_MFC)
	sysmmu_set_owner(&SYSMMU_PLATDEV(mfc_l).dev, &s5p_device_mfc.dev);
	sysmmu_set_owner(&SYSMMU_PLATDEV(mfc_r).dev, &s5p_device_mfc.dev);
#endif
}

static void __init trats_map_io(void)
{
	clk_xusbxti.rate = 24000000;

	s5p_init_io(NULL, 0, S5P_VA_CHIPID);
	s3c24xx_init_clocks(24000000);
	s3c24xx_init_uarts(smdkc210_uartcfgs, ARRAY_SIZE(smdkc210_uartcfgs));

#if defined(CONFIG_S5P_MEM_CMA)
	exynos4_reserve_mem();
#else
	s5p_reserve_mem(S5P_RANGE_MFC);
#endif

	/* as soon as INFORM3 is visible, sec_debug is ready to run */
	sec_debug_init();
}

#ifdef CONFIG_TOUCHSCREEN_MELFAS_MMS
static void __init universal_tsp_set_platdata(struct melfas_mms_platform_data
	*pdata)
{
	if (!pdata)
		pdata = &melfas_mms_ts_pdata;

	i2c_devs3[0].platform_data = pdata;
}
#endif

static void __init universal_tsp_init(void)
{
	int gpio;

	/* TSP_LDO_ON: XMDMADDR_11 */
	gpio = GPIO_TSP_LDO_ON;
	gpio_request(gpio, "TSP_LDO_ON");
	gpio_direction_output(gpio, 1);
	gpio_export(gpio, 0);

	/* TSP_INT: XMDMADDR_7 */
	gpio = GPIO_TSP_INT;
	gpio_request(gpio, "TSP_INT");
	s3c_gpio_cfgpin(gpio, S3C_GPIO_SFN(0xf));
	s3c_gpio_setpull(gpio, S3C_GPIO_PULL_UP);
	i2c_devs3[0].irq = gpio_to_irq(gpio);

	printk(KERN_INFO "%s touch : %d\n", __func__, i2c_devs3[0].irq);
#ifdef CONFIG_MACH_Q1_BD
	gpio_request(GPIO_TSP_SDA, "TSP_SDA");
	gpio_request(GPIO_TSP_SCL, "TSP_SCL");
	gpio_request(GPIO_OLED_DET, "OLED_DET");
#endif
}

static void check_hw_revision(void)
{
	unsigned int hwrev = system_rev & 0xff;

	switch (hwrev) {
	case U1HD_5INCH_REV0_0:		/* U1HD_5INCH_REV0.0_111228 */
		universal_tsp_set_platdata(&melfas_mms_ts_pdata_rotate);
		universal_tsp_init();
		s3c_i2c3_set_platdata(NULL);
		i2c_register_board_info(3, i2c_devs3, ARRAY_SIZE(i2c_devs3));
		break;
	case U1HD_REV0_1:			/* U1HD_REV0_1 */
		universal_tsp_set_platdata(&melfas_mms_ts_pdata);
		universal_tsp_init();
		s3c_i2c3_set_platdata(NULL);
		i2c_register_board_info(3, i2c_devs3, ARRAY_SIZE(i2c_devs3));
		break;

	default:
		break;
	}
}

static void __init trats_machine_init(void)
{
#ifdef CONFIG_S3C64XX_DEV_SPI
	struct clk *sclk = NULL;
	struct clk *prnt = NULL;
	struct device *spi0_dev = &exynos_device_spi0.dev;
#endif

	strcpy(utsname()->nodename, machine_desc->name);
	/* initialise the gpios */
	u1_config_gpio_table();
	exynos4_sleep_gpio_table_set = u1_config_sleep_gpio_table;

#ifdef CONFIG_I2C_S3C2410
	s3c_i2c0_set_platdata(NULL);
	i2c_register_board_info(0, i2c_devs0, ARRAY_SIZE(i2c_devs0));

#ifdef CONFIG_S3C_DEV_I2C1
	s3c_i2c1_set_platdata(NULL);
	i2c_register_board_info(1, i2c_devs1, ARRAY_SIZE(i2c_devs1));
#endif
#ifdef CONFIG_S3C_DEV_I2C2
	s3c_i2c2_set_platdata(NULL);
	i2c_register_board_info(2, i2c_devs2, ARRAY_SIZE(i2c_devs2));
#endif

#ifdef CONFIG_S3C_DEV_I2C4
	s3c_i2c4_set_platdata(NULL);
	i2c_register_board_info(4, i2c_devs4, ARRAY_SIZE(i2c_devs4));
#endif
#ifdef CONFIG_S3C_DEV_I2C5
	s3c_i2c5_set_platdata(NULL);
	s3c_gpio_cfgpin(GPIO_PMIC_IRQ, S3C_GPIO_SFN(0xF));
	s3c_gpio_setpull(GPIO_PMIC_IRQ, S3C_GPIO_PULL_NONE);
	i2c_devs5[0].irq = gpio_to_irq(GPIO_PMIC_IRQ);
	i2c_register_board_info(5, i2c_devs5, ARRAY_SIZE(i2c_devs5));
#endif
#ifdef CONFIG_S3C_DEV_I2C6
#ifdef CONFIG_EPEN_WACOM_G5SP
	p6_wacom_init();
#endif
	s3c_i2c6_set_platdata(NULL);
	i2c_register_board_info(6, i2c_devs6, ARRAY_SIZE(i2c_devs6));
#endif
#ifdef CONFIG_S3C_DEV_I2C7
	s3c_i2c7_set_platdata(&default_i2c7_data);
	i2c_register_board_info(7, i2c_devs7, ARRAY_SIZE(i2c_devs7));
#endif
#ifdef CONFIG_SAMSUNG_MHL
	printk(KERN_INFO "%s() register sii9234 driver\n", __func__);

	i2c_register_board_info(15, tuna_i2c15_boardinfo,
			ARRAY_SIZE(tuna_i2c15_boardinfo));
#endif
#ifdef CONFIG_S3C_DEV_I2C9_EMUL
	i2c_register_board_info(9, i2c_devs9_emul, ARRAY_SIZE(i2c_devs9_emul));
#endif
#ifdef CONFIG_S3C_DEV_I2C10_EMUL
	i2c_register_board_info(10, i2c_devs10_emul,
				ARRAY_SIZE(i2c_devs10_emul));
#endif
#ifdef CONFIG_S3C_DEV_I2C11_EMUL
	s3c_gpio_setpull(GPIO_PS_ALS_INT, S3C_GPIO_PULL_NONE);
	i2c_register_board_info(11, i2c_devs11_emul,
				ARRAY_SIZE(i2c_devs11_emul));
#endif
#ifdef CONFIG_S3C_DEV_I2C14_EMUL
	nfc_setup_gpio();
	i2c_register_board_info(14, i2c_devs14, ARRAY_SIZE(i2c_devs14));
#endif
#if defined(CONFIG_VIDEO_S5K5BAFX)
	i2c_register_board_info(12, i2c_devs12_emul,
				ARRAY_SIZE(i2c_devs12_emul));
#endif
#ifdef CONFIG_FM_SI4709_MODULE
	i2c_register_board_info(16, i2c_devs16, ARRAY_SIZE(i2c_devs16));
#endif
#ifdef	CONFIG_ISDBT_FC8100
	i2c_register_board_info(17, i2c_devs17, ARRAY_SIZE(i2c_devs17));
#endif

#if defined(CONFIG_SMB136_CHARGER_Q1) || defined(CONFIG_SMB328_CHARGER)
	i2c_register_board_info(19, i2c_devs19_emul,
				ARRAY_SIZE(i2c_devs19_emul));
#endif
#endif

	/* 400 kHz for initialization of MMC Card  */
	__raw_writel((__raw_readl(EXYNOS4_CLKDIV_FSYS3) & 0xfffffff0)
		     | 0x9, EXYNOS4_CLKDIV_FSYS3);
	__raw_writel((__raw_readl(EXYNOS4_CLKDIV_FSYS2) & 0xfff0fff0)
		     | 0x80008, EXYNOS4_CLKDIV_FSYS2);
	__raw_writel((__raw_readl(EXYNOS4_CLKDIV_FSYS1) & 0xfff0fff0)
		     | 0x90009, EXYNOS4_CLKDIV_FSYS1);

#ifdef CONFIG_S3C_DEV_HSMMC
	s3c_sdhci0_set_platdata(&exynos4_hsmmc0_pdata);
#endif
#ifdef CONFIG_S3C_DEV_HSMMC1
	s3c_sdhci1_set_platdata(&exynos4_hsmmc1_pdata);
#endif
#ifdef CONFIG_S3C_DEV_HSMMC2
	s3c_sdhci2_set_platdata(&exynos4_hsmmc2_pdata);
#endif
#ifdef CONFIG_S3C_DEV_HSMMC3
	s3c_sdhci3_set_platdata(&exynos4_hsmmc3_pdata);
#endif

#ifdef CONFIG_EXYNOS4_DEV_MSHC
	s3c_mshci_set_platdata(&exynos4_mshc_pdata);
#endif

#ifdef CONFIG_DRM_EXYNOS_FIMD
	/*
	 * platform device name for fimd driver should be changed
	 * because we can get source clock with this name.
	 *
	 * P.S. refer to sclk_fimd definition of clock-exynos4.c
	 */
	s5p_fb_setname(0, "s3cfb");
	s5p_device_fimd0.dev.parent = &exynos4_device_pd[PD_LCD0].dev;
#ifdef CONFIG_S5P_MIPI_DSI2
	s5p_device_mipi_dsim0.dev.parent = &exynos4_device_pd[PD_LCD0].dev;
#endif
#endif

#ifdef CONFIG_EXYNOS_DEV_PD
#ifdef CONFIG_VIDEO_JPEG
	s5p_device_jpeg.dev.parent = &exynos4_device_pd[PD_CAM].dev;
#endif
#endif
#ifdef CONFIG_ANDROID_PMEM
	android_pmem_set_platdata();
#endif
#ifdef CONFIG_VIDEO_FIMC
	/* fimc */
	s3c_fimc0_set_platdata(&fimc_plat);
	s3c_fimc1_set_platdata(&fimc_plat);
	s3c_fimc2_set_platdata(NULL);
#ifdef CONFIG_DRM_EXYNOS_FIMD_WB
	s3c_fimc3_set_platdata(&fimc_plat);
#else
	s3c_fimc3_set_platdata(NULL);
#endif
#ifdef CONFIG_VIDEO_FIMC_MIPI
	s3c_csis0_set_platdata(NULL);
	s3c_csis1_set_platdata(NULL);
#endif
#endif

#ifdef CONFIG_EXYNOS_DEV_PD
#ifdef CONFIG_VIDEO_FIMC
	s3c_device_fimc0.dev.parent = &exynos4_device_pd[PD_CAM].dev;
	s3c_device_fimc1.dev.parent = &exynos4_device_pd[PD_CAM].dev;
	s3c_device_fimc2.dev.parent = &exynos4_device_pd[PD_CAM].dev;
	s3c_device_fimc3.dev.parent = &exynos4_device_pd[PD_CAM].dev;
#endif
#endif
#ifdef CONFIG_ION_EXYNOS
	exynos_ion_set_platdata();
#endif

#ifdef CONFIG_EXYNOS4_SETUP_THERMAL
	s5p_tmu_set_platdata(&u1_tmu_data);
#endif

#if defined(CONFIG_VIDEO_MFC5X) || defined(CONFIG_VIDEO_SAMSUNG_S5P_MFC)
#ifdef CONFIG_EXYNOS_DEV_PD
	s5p_device_mfc.dev.parent = &exynos4_device_pd[PD_MFC].dev;
#endif
#endif

#if defined(CONFIG_VIDEO_MFC5X)
	exynos4_mfc_setup_clock(&s5p_device_mfc.dev, 200 * MHZ);
#endif
#if defined(CONFIG_VIDEO_SAMSUNG_S5P_MFC)
	dev_set_name(&s5p_device_mfc.dev, "s3c-mfc");
	clk_add_alias("mfc", "s5p-mfc", "mfc", &s5p_device_mfc.dev);
	s5p_mfc_setname(&s5p_device_mfc, "s5p-mfc");
#endif

#ifdef CONFIG_USB_EHCI_S5P
	smdkc210_ehci_init();
#endif
#ifdef CONFIG_USB_OHCI_S5P
	smdkc210_ohci_init();
#endif
#ifdef CONFIG_USB_GADGET
	smdkc210_usbgadget_init();
#endif

#if defined(CONFIG_SND_SOC_SLP_TRATS_MC1N2)
	trats_sound_init();
#endif

	brcm_wlan_init();

	exynos_sysmmu_init();

	platform_add_devices(smdkc210_devices, ARRAY_SIZE(smdkc210_devices));

#ifdef CONFIG_DRM_EXYNOS_FIMD
	trats_fb_init();
#endif
#ifdef CONFIG_DRM_EXYNOS_HDMI
	trats_tv_init();
#endif

#ifdef CONFIG_SEC_THERMISTOR
	platform_device_register(&sec_device_thermistor);
#endif

#ifdef CONFIG_S3C64XX_DEV_SPI
	sclk = clk_get(spi0_dev, "dout_spi0");
	if (IS_ERR(sclk))
		dev_err(spi0_dev, "failed to get sclk for SPI-0\n");
	prnt = clk_get(spi0_dev, "mout_mpll");
	if (IS_ERR(prnt))
		dev_err(spi0_dev, "failed to get prnt\n");
	clk_set_parent(sclk, prnt);

	clk_put(sclk);
	clk_put(prnt);

	if (!gpio_request(EXYNOS4_GPB(1), "SPI_CS0")) {
		gpio_direction_output(EXYNOS4_GPB(1), 1);
		s3c_gpio_cfgpin(EXYNOS4_GPB(1), S3C_GPIO_SFN(1));
		s3c_gpio_setpull(EXYNOS4_GPB(1), S3C_GPIO_PULL_UP);
		exynos_spi_set_info(0, EXYNOS_SPI_SRCCLK_SCLK,
				    ARRAY_SIZE(spi0_csi));
	}
	spi_register_board_info(spi0_board_info, ARRAY_SIZE(spi0_board_info));
#endif

#ifdef CONFIG_BUSFREQ_OPP
	dev_add(&busfreq, &exynos4_busfreq.dev);
#endif

#if defined(CONFIG_TDMB) || defined(CONFIG_TDMB_MODULE)
	tdmb_dev_init();
#endif
	check_hw_revision();

}

static void __init exynos_init_reserve(void)
{
	sec_debug_magic_init();
}

#ifdef CONFIG_MACH_U1_KOR_SKT
#define MODEL_NAME "SHW-M250S"
#elif defined(CONFIG_MACH_U1_KOR_KT)
#define MODEL_NAME "SHW-M250K"
#elif defined(CONFIG_MACH_U1_KOR_LGT)
#define MODEL_NAME "SHW-M250L"
#else
#define MODEL_NAME "SMDK4210"
#endif

MACHINE_START(TRATS, "TRATS")
	/* Maintainer: Kyungmin Park <kyungmin.park@samsung.com> */
	.boot_params	= S5P_PA_SDRAM + 0x100,
	.init_irq	= exynos4_init_irq,
	.map_io		= trats_map_io,
	.init_machine	= trats_machine_init,
	.timer		= &exynos4_timer,
	.init_early	= &exynos_init_reserve,
MACHINE_END

/*
 * This is just for backward compatability because the old TRATS have been
 * shipped with this id. MACH_DDNAS also has same but we don't care.
 */
MACHINE_START(U1HD, "U1HD")
	/* Maintainer: Kyungmin Park <kyungmin.park@samsung.com> */
	.boot_params	= S5P_PA_SDRAM + 0x100,
	.init_irq	= exynos4_init_irq,
	.map_io		= trats_map_io,
	.init_machine	= trats_machine_init,
	.timer		= &exynos4_timer,
	.init_early	= &exynos_init_reserve,
MACHINE_END

