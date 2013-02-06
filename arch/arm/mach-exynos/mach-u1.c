/* linux/arch/arm/mach-exynos4/mach-smdkc210.c
 *
 * Copyright (c) 2010-2011 Samsung Electronics Co., Ltd.
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
#include <linux/mmc/host.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/i2c.h>
#include <linux/i2c-gpio.h>
#include <linux/input.h>
#include <linux/switch.h>
#include <linux/spi/spi.h>
#include <linux/spi/spi_gpio.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/fixed.h>
#include <linux/mfd/max8997.h>
#include <linux/mfd/max8997-private.h>
#include <linux/sensor/k3g.h>
#include <linux/sensor/k3dh.h>
#include <linux/sensor/ak8975.h>
#ifdef CONFIG_MACH_U1_BD
#include <linux/sensor/cm3663.h>
#include <linux/sensor/pas2m110.h>
#endif
#ifdef CONFIG_MACH_Q1_BD
#include <linux/sensor/gp2a_analog.h>
#endif
#include <linux/pn544.h>
#ifdef CONFIG_SND_SOC_U1_MC1N2
#include <linux/mfd/mc1n2_pdata.h>
#endif
#if defined(CONFIG_TOUCHSCREEN_ATMEL_MXT540E)
#include <linux/i2c/mxt540e.h>
#else
#ifdef CONFIG_TOUCHSCREEN_ATMEL_MXT224_GC
#include <linux/i2c/mxt224_gc.h>
#else
#include <linux/i2c/mxt224_u1.h>
#endif
#endif
#include <linux/memblock.h>
#include <linux/power_supply.h>
#if defined(CONFIG_S5P_MEM_CMA)
#include <linux/cma.h>
#endif
#ifdef CONFIG_ANDROID_PMEM
#include <linux/android_pmem.h>
#endif

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
#include <plat/fb-s5p.h>
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
#include <plat/regs-fb-v4.h>
#include <plat/media.h>
#include <plat/udc-hs.h>
#include <plat/s5p-clock.h>
#include <plat/tvout.h>
#include <plat/fimg2d.h>
#include <plat/ehci.h>
#include <plat/usbgadget.h>

#ifdef CONFIG_S3C64XX_DEV_SPI
#include <plat/s3c64xx-spi.h>
#endif

#include <mach/map.h>
#include <mach/exynos-clock.h>
#include <mach/media.h>
#include <plat/regs-fb.h>

#include <mach/dev-sysmmu.h>
#include <mach/dev.h>
#include <mach/regs-clock.h>
#include <mach/exynos-ion.h>

#ifdef CONFIG_FB_S5P_MIPI_DSIM
#include <mach/mipi_ddi.h>
#include <mach/dsim.h>
#include <plat/fb-s5p.h>
#endif

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

#ifdef CONFIG_VIDEO_S5K5BBGX
#include <media/s5k5bbgx_platform.h>
#endif
#if defined(CONFIG_EXYNOS4_SETUP_THERMAL)
#include <plat/s5p-tmu.h>
#include <mach/regs-tmu.h>
#endif

#ifdef CONFIG_SEC_DEV_JACK
#include <linux/sec_jack.h>
#endif

#ifdef CONFIG_USBHUB_USB3803
#include <linux/usb3803.h>
#endif

#ifdef CONFIG_BT_BCM4330
#include <mach/board-bluetooth-bcm.h>
#endif

#ifdef CONFIG_FB_S5P_LD9040
#include <linux/ld9040.h>
#endif

#ifdef CONFIG_FB_S5P_MDNIE
#include <linux/mdnie.h>
#endif

#include <../../../drivers/video/samsung/s3cfb.h>
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

#ifdef CONFIG_EPEN_WACOM_G5SP
#include <linux/wacom_i2c.h>
static struct wacom_g5_callbacks *wacom_callbacks;
#endif /* CONFIG_EPEN_WACOM_G5SP */

#ifdef CONFIG_KEYBOARD_CYPRESS_TOUCH
#include <linux/i2c/touchkey_i2c.h>
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
#if defined(CONFIG_MACH_U1_NA_SPR) || defined(CONFIG_MACH_U1_NA_USCC)
#include "../../../drivers/usb/gadget/s3c_udc.h"
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

#define WRITEBACK_ENABLED

#ifdef CONFIG_VIDEO_FIMC
/*
 * External camera reset
 * Because the most of cameras take i2c bus signal, so that
 * you have to reset at the boot time for other i2c slave devices.
 * This function also called at fimc_init_camera()
 * Do optimization for cameras on your platform.
 */

#ifdef CONFIG_VIDEO_M5MO

struct class *camera_class;

static int __init camera_class_init(void)
{
	camera_class = class_create(THIS_MODULE, "camera");
	if (IS_ERR(camera_class)) {
		pr_err("Failed to create class(camera)!\n");
		return PTR_ERR(camera_class);
	}

	return 0;
}

subsys_initcall(camera_class_init);

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

	printk(KERN_DEBUG "%s: in. hw=0x%X\n", __func__, system_rev);

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
#ifdef	CONFIG_SAMSUNG_MHL


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

#ifdef CONFIG_TARGET_LOCALE_KOR
	s3c_gpio_cfgpin(GPIO_HDMI_EN, S3C_GPIO_OUTPUT);
	gpio_set_value(GPIO_HDMI_EN, GPIO_LEVEL_LOW);
	s3c_gpio_setpull(GPIO_HDMI_EN, S3C_GPIO_PULL_NONE);
#else
	if (system_rev < 7) {
		s3c_gpio_cfgpin(GPIO_HDMI_EN, S3C_GPIO_OUTPUT);
		gpio_set_value(GPIO_HDMI_EN, GPIO_LEVEL_LOW);
		s3c_gpio_setpull(GPIO_HDMI_EN, S3C_GPIO_PULL_NONE);
	} else {
		s3c_gpio_cfgpin(GPIO_HDMI_EN_REV07, S3C_GPIO_OUTPUT);
		gpio_set_value(GPIO_HDMI_EN_REV07, GPIO_LEVEL_LOW);
		s3c_gpio_setpull(GPIO_HDMI_EN_REV07, S3C_GPIO_PULL_NONE);
	}
#endif

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
		/*s3c_gpio_cfgpin(GPIO_HDMI_EN,S3C_GPIO_OUTPUT);*/
#ifdef CONFIG_TARGET_LOCALE_KOR
		gpio_set_value(GPIO_HDMI_EN, GPIO_LEVEL_HIGH);
#else
		if (system_rev < 7)
			gpio_set_value(GPIO_HDMI_EN, GPIO_LEVEL_HIGH);
		else
			gpio_set_value(GPIO_HDMI_EN_REV07, GPIO_LEVEL_HIGH);
#endif

		s3c_gpio_setpull(GPIO_AP_SCL_18V, S3C_GPIO_PULL_DOWN);
		s3c_gpio_setpull(GPIO_AP_SCL_18V, S3C_GPIO_PULL_NONE);

	} else {
		gpio_set_value(GPIO_MHL_RST, GPIO_LEVEL_LOW);
		usleep_range(10000, 20000);
		gpio_set_value(GPIO_MHL_RST, GPIO_LEVEL_HIGH);
#ifdef CONFIG_TARGET_LOCALE_KOR
		gpio_set_value(GPIO_HDMI_EN, GPIO_LEVEL_HIGH);
#else
		if (system_rev < 7)
			gpio_set_value(GPIO_HDMI_EN, GPIO_LEVEL_LOW);
		else
			gpio_set_value(GPIO_HDMI_EN_REV07, GPIO_LEVEL_LOW);
#endif
		gpio_set_value(GPIO_MHL_RST, GPIO_LEVEL_LOW);
	}
	pr_info("[MHL]%s : %d\n", __func__, on);
}

void sii9234_reset(void)
{
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
	.streamoff_delay = S5K5BAFX_STREAMOFF_DELAY,
	.init_streamoff = true,
	.dbg_level = CAMDBG_LEVEL_DEFAULT,
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

#ifdef CONFIG_VIDEO_S5K5BBGX
static int s5k5bbgx_get_i2c_busnum(void)
{
	return 12;
}

static int s5k5bbgx_power_on(void)
{
	struct regulator *regulator;
	int ret = 0;

	/* printk("%s: in\n", __func__); */

	ret = gpio_request(GPIO_ISP_RESET, "GPY3");
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

	s3c_gpio_setpull(VT_CAM_SDA_18V, S3C_GPIO_PULL_NONE);
	s3c_gpio_setpull(VT_CAM_SCL_18V, S3C_GPIO_PULL_NONE);

	/* ISP_RESET low*/
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
	udelay(300); /* don't change me */

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

	/* Mclk */
	ret = s3c_gpio_cfgpin(GPIO_CAM_MCLK, S3C_GPIO_SFN(2));
	s3c_gpio_setpull(GPIO_CAM_MCLK, S3C_GPIO_PULL_NONE);
	CAM_CHECK_ERR_RET(ret, "cfg mclk");
	udelay(10);

	/* CAM_VGA_nSTBY */
	ret = gpio_direction_output(GPIO_CAM_VGA_nSTBY, 1);
	CAM_CHECK_ERR_RET(ret, "output VGA_nSTBY");
	udelay(50);

	/* CAM_VGA_nRST	 */
	ret = gpio_direction_output(GPIO_CAM_VGA_nRST, 1);
	CAM_CHECK_ERR_RET(ret, "output VGA_nRST");
	udelay(100);

	gpio_free(GPIO_ISP_RESET);
	gpio_free(GPIO_CAM_IO_EN);
	gpio_free(GPIO_VT_CAM_15V);
	gpio_free(GPIO_CAM_VGA_nSTBY);
	gpio_free(GPIO_CAM_VGA_nRST);

	return 0;
}

static int s5k5bbgx_power_off(void)
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

	/* CAM_VGA_nSTBY */
	ret = gpio_direction_output(GPIO_CAM_VGA_nSTBY, 0);
	CAM_CHECK_ERR(ret, "output VGA_nSTBY");
	udelay(20);

	/* Mclk */
	ret = s3c_gpio_cfgpin(GPIO_CAM_MCLK, S3C_GPIO_INPUT);
	s3c_gpio_setpull(GPIO_CAM_MCLK, S3C_GPIO_PULL_DOWN);
	CAM_CHECK_ERR(ret, "cfg mclk");
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

	gpio_direction_input(VT_CAM_SDA_18V);
	s3c_gpio_setpull(VT_CAM_SDA_18V, S3C_GPIO_PULL_DOWN);
	gpio_direction_input(VT_CAM_SCL_18V);
	s3c_gpio_setpull(VT_CAM_SCL_18V, S3C_GPIO_PULL_DOWN);

	gpio_free(GPIO_CAM_VGA_nRST);
	gpio_free(GPIO_CAM_VGA_nSTBY);
	gpio_free(GPIO_VT_CAM_15V);
	gpio_free(GPIO_CAM_IO_EN);

	return 0;
}

static int s5k5bbgx_power(int onoff)
{
	int ret = 0;

	printk(KERN_INFO "%s(): %s\n", __func__, onoff ? "on" : "down");
	if (onoff) {
#if defined(CONFIG_TARGET_LOCALE_NA)
		exynos_cpufreq_lock(DVFS_LOCK_ID_CAM, 1);
		ret = s5k5bbgx_power_on();
		exynos_cpufreq_lock_free(DVFS_LOCK_ID_CAM);
#else
		ret = s5k5bbgx_power_on();
#endif
		if (unlikely(ret))
			goto error_out;
	} else {
#if defined(CONFIG_TARGET_LOCALE_NA)
		exynos_cpufreq_lock(DVFS_LOCK_ID_CAM, 1);
		ret = s5k5bbgx_power_off();
		exynos_cpufreq_lock_free(DVFS_LOCK_ID_CAM);
#else
		ret = s5k5bbgx_power_off();
#endif
		/* s3c_i2c0_force_stop();*/ /* DSLIM. Should be implemented */
	}

	/* ret = s3c_csis_power(onoff); */

error_out:
	return ret;
}

static struct s5k5bbgx_platform_data s5k5bbgx_plat = {
	.default_width = 640,
	.default_height = 480,
	.pixelformat = V4L2_PIX_FMT_UYVY,
	.freq = 24000000,
	.is_mipi = 0,
};

static struct i2c_board_info  s5k5bbgx_i2c_info = {
	I2C_BOARD_INFO("S5K5BBGX", 0x5A >> 1),
	.platform_data = &s5k5bbgx_plat,
};

static struct s3c_platform_camera s5k5bbgx = {
#if defined(CONFIG_VIDEO_S5K5BBGX_MIPI)
	.id		= CAMERA_CSI_D,
	.type		= CAM_TYPE_MIPI,
	.fmt		= ITU_601_YCBCR422_8BIT,
	.order422	= CAM_ORDER422_8BIT_CBYCRY,

	.mipi_lanes	= 1,
	.mipi_settle	= 6,
	.mipi_align	= 32,
#else
	.id		= CAMERA_PAR_A,
	.type		= CAM_TYPE_ITU,
	.fmt		= ITU_601_YCBCR422_8BIT,
	.order422	= CAM_ORDER422_8BIT_YCBYCR,
#endif
	.get_i2c_busnum	= s5k5bbgx_get_i2c_busnum,
	.info		= &s5k5bbgx_i2c_info,
	.pixelformat	= V4L2_PIX_FMT_UYVY,
	.srclk_name	= "xusbxti",
	.clk_name	= "sclk_cam0",
	.clk_rate	= 24000000,
	.line_length	= 640,
	.width		= 640,
	.height		= 480,
	.window		= {
		.left	= 0,
		.top	= 0,
		.width	= 640,
		.height	= 480,
	},

	/* Polarity */
	.inv_pclk	= 0,
	.inv_vsync	= 1,
	.inv_href	= 0,
	.inv_hsync	= 0,
	.reset_camera	= 0,
	.initialized	= 0,
	.cam_power	= s5k5bbgx_power,
};
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
#ifdef CONFIG_VIDEO_S5K5BBGX
		&s5k5bbgx,
#endif

#ifdef CONFIG_VIDEO_S5K5BAFX
		&s5k5bafx,
#endif
#ifdef WRITEBACK_ENABLED
		&writeback,
#endif
	},
	.hw_ver = 0x51,
#if defined(CONFIG_VIDEO_S5K5BBGX)
	.cfg_gpio = s3c_fimc0_cfg_gpio,
#else
	.cfg_gpio = cam_cfg_gpio,
#endif
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

#if defined(CONFIG_MACH_U1_NA_SPR) || defined(CONFIG_MACH_U1_NA_USCC)
#ifdef CONFIG_S3C_DEV_HSMMC2
	DEFINE_MMC_CARD_NOTIFIER(2)
#endif
#endif

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
#if defined(CONFIG_MACH_U1_NA_SPR) || defined(CONFIG_MACH_U1_NA_USCC)
#ifdef CONFIG_S3C_DEV_HSMMC2
	if (pdev == &s3c_device_hsmmc2) {
		printk(KERN_INFO "Test logs pdev : %p s3c_device_hsmmc2 %p\n",
				pdev, &s3c_device_hsmmc2);
		notify_func = hsmmc2_notify_func;
		printk(KERN_INFO "Test logs notify_func = hsmmc2_notify_func : %p\n",
				notify_func);
	}
#endif
#endif
#ifdef CONFIG_S3C_DEV_HSMMC3
	if (pdev == &s3c_device_hsmmc3) {
		printk(KERN_INFO "Test logs pdev : %p s3c_device_hsmmc3 %p\n",
				pdev, &s3c_device_hsmmc3);
		notify_func = hsmmc3_notify_func;
		printk(KERN_INFO"Test logs notify_func = hsmmc3_notify_func: %p\n",
				notify_func);
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
#if defined(CONFIG_MACH_U1_NA_SPR) || defined(CONFIG_MACH_U1_NA_USCC)
	.cd_type = S3C_SDHCI_CD_EXTERNAL,
#else
	.cd_type = S3C_SDHCI_CD_GPIO,
	.ext_cd_gpio = EXYNOS4_GPX3(4),
	.vmmc_name = "vtf_2.8v",
	.ext_cd_gpio_invert = 1,
#endif
	.clk_type = S3C_SDHCI_CLK_DIV_EXTERNAL,
#if defined(CONFIG_MACH_U1_NA_SPR) || defined(CONFIG_MACH_U1_NA_USCC)
/* For Wi-Fi */
	.ext_cd_init = ext_cd_init_hsmmc2,
	.ext_cd_cleanup = ext_cd_cleanup_hsmmc2,
	.pm_flags = S3C_SDHCI_PM_IGNORE_SUSPEND_RESUME,
#endif
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

#ifdef CONFIG_VIDEO_FIMG2D
static struct fimg2d_platdata fimg2d_data __initdata = {
	.hw_ver = 30,
	.parent_clkname = "mout_g2d0",
	.clkname = "sclk_fimg2d",
	.gate_clkname = "fimg2d",
	.clkrate = 267 * 1000000,	/* 266 Mhz */
};
#endif

#ifdef CONFIG_FB_S3C
#if defined(CONFIG_LCD_AMS369FG06)
static int lcd_power_on(struct lcd_device *ld, int enable)
{
	return 1;
}

static int reset_lcd(struct lcd_device *ld)
{
	int err = 0;

	err = gpio_request(EXYNOS4_GPX0(6), "GPX0");
	if (err) {
		printk(KERN_ERR "failed to request GPX0 for "
		       "lcd reset control\n");
		return err;
	}

	gpio_direction_output(EXYNOS4_GPX0(6), 1);
	mdelay(100);

	gpio_set_value(EXYNOS4_GPX0(6), 1);
	mdelay(100);

	gpio_free(EXYNOS4_GPX0(6));

	return 1;
}

static struct lcd_platform_data ams369fg06_platform_data = {
	.reset = reset_lcd,
	.power_on = lcd_power_on,
	.lcd_enabled = 0,
	.reset_delay = 100,	/* 100ms */
};

#define		LCD_BUS_NUM	3
#define		DISPLAY_CS	EXYNOS4_GPB(5)
#define		DISPLAY_CLK	EXYNOS4_GPB(4)
#define		DISPLAY_SI	EXYNOS4_GPB(7)

static struct spi_board_info spi_board_info[] __initdata = {
	{
		.modalias = "ams369fg06",
		.platform_data = (void *)&ams369fg06_platform_data,
		.max_speed_hz = 1200000,
		.bus_num = LCD_BUS_NUM,
		.chip_select = 0,
		.mode = SPI_MODE_3,
		.controller_data = (void *)DISPLAY_CS,
	}
};

static struct spi_gpio_platform_data ams369fg06_spi_gpio_data = {
	.sck = DISPLAY_CLK,
	.mosi = DISPLAY_SI,
	.miso = -1,
	.num_chipselect = 1,
};

static struct platform_device s3c_device_spi_gpio = {
	.name = "spi_gpio",
	.id = LCD_BUS_NUM,
	.dev = {
		.parent = &s5p_device_fimd0.dev,
		.platform_data = &ams369fg06_spi_gpio_data,
	},
};

static struct s3c_fb_pd_win smdkc210_fb_win0 = {
	.win_mode = {
		.left_margin = 9,
		.right_margin = 9,
		.upper_margin = 5,
		.lower_margin = 5,
		.hsync_len = 2,
		.vsync_len = 2,
		.xres = 480,
		.yres = 800,
	},
	.virtual_x = 480,
	.virtual_y = 1600,
	.width = 48,
	.height = 80,
	.max_bpp = 32,
	.default_bpp = 24,
};

static struct s3c_fb_pd_win smdkc210_fb_win1 = {
	.win_mode = {
		.left_margin = 9,
		.right_margin = 9,
		.upper_margin = 5,
		.lower_margin = 5,
		.hsync_len = 2,
		.vsync_len = 2,
		.xres = 480,
		.yres = 800,
	},
	.virtual_x = 480,
	.virtual_y = 1600,
	.width = 48,
	.height = 80,
	.max_bpp = 32,
	.default_bpp = 24,
};

static struct s3c_fb_pd_win smdkc210_fb_win2 = {
	.win_mode = {
		.left_margin = 9,
		.right_margin = 9,
		.upper_margin = 5,
		.lower_margin = 5,
		.hsync_len = 2,
		.vsync_len = 2,
		.xres = 480,
		.yres = 800,
	},
	.virtual_x = 480,
	.virtual_y = 1600,
	.width = 48,
	.height = 80,
	.max_bpp = 32,
	.default_bpp = 24,
};

#elif defined(CONFIG_LCD_WA101S)
static void lcd_wa101s_set_power(struct plat_lcd_data *pd, unsigned int power)
{
	if (power) {
#if !defined(CONFIG_BACKLIGHT_PWM)
		gpio_request(EXYNOS4_GPD0(1), "GPD0");
		gpio_direction_output(EXYNOS4_GPD0(1), 1);
		gpio_free(EXYNOS4_GPD0(1));
#endif
	} else {
#if !defined(CONFIG_BACKLIGHT_PWM)
		gpio_request(EXYNOS4_GPD0(1), "GPD0");
		gpio_direction_output(EXYNOS4_GPD0(1), 0);
		gpio_free(EXYNOS4_GPD0(1));
#endif
	}
}

static struct plat_lcd_data smdkc210_lcd_wa101s_data = {
	.set_power = lcd_wa101s_set_power,
};

static struct platform_device smdkc210_lcd_wa101s = {
	.name = "platform-lcd",
	.dev.parent = &s5p_device_fimd0.dev,
	.dev.platform_data = &smdkc210_lcd_wa101s_data,
};

static struct s3c_fb_pd_win smdkc210_fb_win0 = {
	.win_mode = {
		.left_margin = 80,
		.right_margin = 48,
		.upper_margin = 14,
		.lower_margin = 3,
		.hsync_len = 32,
		.vsync_len = 5,
		.xres = 1366,
		.yres = 768,
	},
	.virtual_x = 1366,
	.virtual_y = 768 * 2,
	.width = 223,
	.height = 125,
	.max_bpp = 32,
	.default_bpp = 24,
};

#ifndef CONFIG_LCD_WA101S	/* temporarily disables window1 */
static struct s3c_fb_pd_win smdkc210_fb_win1 = {
	.win_mode = {
		.left_margin = 80,
		.right_margin = 48,
		.upper_margin = 14,
		.lower_margin = 3,
		.hsync_len = 32,
		.vsync_len = 5,
		.xres = 1366,
		.yres = 768,
	},
	.virtual_x = 1366,
	.virtual_y = 768 * 2,
	.max_bpp = 32,
	.default_bpp = 24,
};
#endif

#elif defined(CONFIG_LCD_LTE480WV)
static void lcd_lte480wv_set_power(struct plat_lcd_data *pd, unsigned int power)
{
	if (power) {
#if !defined(CONFIG_BACKLIGHT_PWM)
		gpio_request(EXYNOS4_GPD0(1), "GPD0");
		gpio_direction_output(EXYNOS4_GPD0(1), 1);
		gpio_free(EXYNOS4_GPD0(1));
#endif
		/* fire nRESET on power up */
		gpio_request(EXYNOS4_GPX0(6), "GPX0");

		gpio_direction_output(EXYNOS4_GPX0(6), 1);
		mdelay(100);

		gpio_set_value(EXYNOS4_GPX0(6), 0);
		mdelay(10);

		gpio_set_value(EXYNOS4_GPX0(6), 1);
		mdelay(10);

		gpio_free(EXYNOS4_GPX0(6));
	} else {
#if !defined(CONFIG_BACKLIGHT_PWM)
		gpio_request(EXYNOS4_GPD0(1), "GPD0");
		gpio_direction_output(EXYNOS4_GPD0(1), 0);
		gpio_free(EXYNOS4_GPD0(1));
#endif
	}
}

static struct plat_lcd_data smdkc210_lcd_lte480wv_data = {
	.set_power = lcd_lte480wv_set_power,
};

static struct platform_device smdkc210_lcd_lte480wv = {
	.name = "platform-lcd",
	.dev.parent = &s5p_device_fimd0.dev,
	.dev.platform_data = &smdkc210_lcd_lte480wv_data,
};

static struct s3c_fb_pd_win smdkc210_fb_win0 = {
	.win_mode = {
		.left_margin = 13,
		.right_margin = 8,
		.upper_margin = 7,
		.lower_margin = 5,
		.hsync_len = 3,
		.vsync_len = 1,
		.xres = 800,
		.yres = 480,
	},
	.virtual_x = 800,
	.virtual_y = 960,
	.max_bpp = 32,
	.default_bpp = 24,
};

static struct s3c_fb_pd_win smdkc210_fb_win1 = {
	.win_mode = {
		.left_margin = 13,
		.right_margin = 8,
		.upper_margin = 7,
		.lower_margin = 5,
		.hsync_len = 3,
		.vsync_len = 1,
		.xres = 800,
		.yres = 480,
	},
	.virtual_x = 800,
	.virtual_y = 960,
	.max_bpp = 32,
	.default_bpp = 24,
};
#endif

static struct s3c_fb_platdata smdkc210_lcd0_pdata __initdata = {
#if defined(CONFIG_LCD_AMS369FG06) || defined(CONFIG_LCD_WA101S) ||	\
	defined(CONFIG_LCD_LTE480WV)
	.win[0] = &smdkc210_fb_win0,
#ifndef CONFIG_LCD_WA101S	/* temporarily disables window1 */
	.win[1] = &smdkc210_fb_win1,
#endif
#endif
	.vidcon0 = VIDCON0_VIDOUT_RGB | VIDCON0_PNRMODE_RGB,
#if defined(CONFIG_LCD_AMS369FG06)
	.vidcon1 = VIDCON1_INV_VCLK | VIDCON1_INV_VDEN |
	VIDCON1_INV_HSYNC | VIDCON1_INV_VSYNC,
#elif defined(CONFIG_LCD_WA101S)
	.vidcon1 = VIDCON1_INV_VCLK | VIDCON1_INV_HSYNC | VIDCON1_INV_VSYNC,
#elif defined(CONFIG_LCD_LTE480WV)
	.vidcon1 = VIDCON1_INV_HSYNC | VIDCON1_INV_VSYNC,
#endif
	.setup_gpio = exynos4_fimd0_gpio_setup_24bpp,
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

#ifdef CONFIG_FB_S5P
unsigned int lcdtype;
static int __init lcdtype_setup(char *str)
{
	get_option(&str, &lcdtype);
	return 1;
}
__setup("lcdtype=", lcdtype_setup);

#ifdef CONFIG_FB_S5P_LD9040
unsigned int ld9040_lcdtype;
static int __init ld9040_lcdtype_setup(char *str)
{
	get_option(&str, &ld9040_lcdtype);
	return 1;
}

__setup("ld9040.get_lcdtype=0x", ld9040_lcdtype_setup);

static int lcd_cfg_gpio(void)
{
	int i, f3_end = 4;

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

#ifdef MAX_DRVSTR
	/* drive strength to max */
	writel(0xffffffff, S5P_VA_GPIO + 0x18c);
	writel(0xffffffff, S5P_VA_GPIO + 0x1ac);
	writel(0xffffffff, S5P_VA_GPIO + 0x1cc);
	writel(readl(S5P_VA_GPIO + 0x1ec) | 0xffffff, S5P_VA_GPIO + 0x1ec);
#else
	/* drive strength to 2X */
	writel(0xaaaaaaaa, S5P_VA_GPIO + 0x18c);
	writel(0xaaaaaaaa, S5P_VA_GPIO + 0x1ac);
	writel(0xaaaaaaaa, S5P_VA_GPIO + 0x1cc);
	writel(readl(S5P_VA_GPIO + 0x1ec) | 0xaaaaaa, S5P_VA_GPIO + 0x1ec);
#endif

#if !defined(CONFIG_MACH_U1_KOR_LGT)
	/* MLCD_RST */
	s3c_gpio_cfgpin(EXYNOS4_GPY4(5), S3C_GPIO_OUTPUT);
	s3c_gpio_setpull(EXYNOS4_GPY4(5), S3C_GPIO_PULL_NONE);

	/* LCD_nCS */
	s3c_gpio_cfgpin(EXYNOS4_GPY4(3), S3C_GPIO_OUTPUT);
	s3c_gpio_setpull(EXYNOS4_GPY4(3), S3C_GPIO_PULL_NONE);
	/* LCD_SCLK */
	s3c_gpio_cfgpin(EXYNOS4_GPY3(1), S3C_GPIO_OUTPUT);
	s3c_gpio_setpull(EXYNOS4_GPY3(1), S3C_GPIO_PULL_NONE);
	/* LCD_SDI */
	s3c_gpio_cfgpin(EXYNOS4_GPY3(3), S3C_GPIO_OUTPUT);
	s3c_gpio_setpull(EXYNOS4_GPY3(3), S3C_GPIO_PULL_NONE);
#else
	/* MLCD_RST */
	s3c_gpio_cfgpin(EXYNOS4_GPX1(3), S3C_GPIO_OUTPUT);
	s3c_gpio_setpull(EXYNOS4_GPX1(3), S3C_GPIO_PULL_NONE);
	/* LCD_nCS */
	s3c_gpio_cfgpin(EXYNOS4_GPY0(3), S3C_GPIO_OUTPUT);
	s3c_gpio_setpull(EXYNOS4_GPY0(3), S3C_GPIO_PULL_NONE);
	/* LCD_SCLK */
	s3c_gpio_cfgpin(EXYNOS4210_GPE2(3), S3C_GPIO_OUTPUT);
	s3c_gpio_setpull(EXYNOS4210_GPE2(3), S3C_GPIO_PULL_NONE);
	/* LCD_SDI */
	s3c_gpio_cfgpin(EXYNOS4_GPX1(1), S3C_GPIO_OUTPUT);
	s3c_gpio_setpull(EXYNOS4_GPX1(1), S3C_GPIO_PULL_NONE);
#endif

	return 0;
}

static int lcd_power_on(struct lcd_device *ld, int enable)
{
	struct regulator *regulator;

	if (ld == NULL) {
		printk(KERN_ERR "lcd device object is NULL.\n");
		return 0;
	}

	if (enable) {
		regulator = regulator_get(NULL, "vlcd_3.0v");
		if (IS_ERR(regulator))
			return 0;

		regulator_enable(regulator);
		regulator_put(regulator);
	} else {
		regulator = regulator_get(NULL, "vlcd_3.0v");

		if (IS_ERR(regulator))
			return 0;

		if (regulator_is_enabled(regulator))
			regulator_force_disable(regulator);

		regulator_put(regulator);
	}

	return 1;
}

static int reset_lcd(struct lcd_device *ld)
{
	int reset_gpio = -1;
	int err;

#if !defined(CONFIG_MACH_U1_KOR_LGT)
	reset_gpio = EXYNOS4_GPY4(5);
#else
	reset_gpio = EXYNOS4_GPX1(3);
#endif

	err = gpio_request(reset_gpio, "MLCD_RST");
	if (err) {
		printk(KERN_ERR "failed to request MLCD_RST for "
		       "lcd reset control\n");
		return err;
	}

	gpio_request(reset_gpio, "MLCD_RST");

	gpio_direction_output(reset_gpio, 1);
	mdelay(5);
	gpio_direction_output(reset_gpio, 0);
	mdelay(5);
	gpio_direction_output(reset_gpio, 1);

	gpio_free(reset_gpio);

	return 1;
}

static int lcd_gpio_cfg_earlysuspend(struct lcd_device *ld)
{
	int reset_gpio = -1;
	int err;

#if !defined(CONFIG_MACH_U1_KOR_LGT)
	reset_gpio = EXYNOS4_GPY4(5);
#else
	reset_gpio = EXYNOS4_GPX1(3);
#endif

	err = gpio_request(reset_gpio, "MLCD_RST");
	if (err) {
		printk(KERN_ERR "failed to request MLCD_RST for "
		       "lcd reset control\n");
		return err;
	}

	mdelay(5);
	gpio_direction_output(reset_gpio, 0);

	gpio_free(reset_gpio);

	return 0;
}

static int lcd_gpio_cfg_lateresume(struct lcd_device *ld)
{
#if !defined(CONFIG_MACH_U1_KOR_LGT)
	/* MLCD_RST */
	s3c_gpio_cfgpin(EXYNOS4_GPY4(5), S3C_GPIO_OUTPUT);
	s3c_gpio_setpull(EXYNOS4_GPY4(5), S3C_GPIO_PULL_NONE);

	/* LCD_nCS */
	s3c_gpio_cfgpin(EXYNOS4_GPY4(3), S3C_GPIO_OUTPUT);
	s3c_gpio_setpull(EXYNOS4_GPY4(3), S3C_GPIO_PULL_NONE);
	/* LCD_SCLK */
	s3c_gpio_cfgpin(EXYNOS4_GPY3(1), S3C_GPIO_OUTPUT);
	s3c_gpio_setpull(EXYNOS4_GPY3(1), S3C_GPIO_PULL_NONE);
	/* LCD_SDI */
	s3c_gpio_cfgpin(EXYNOS4_GPY3(3), S3C_GPIO_OUTPUT);
	s3c_gpio_setpull(EXYNOS4_GPY3(3), S3C_GPIO_PULL_NONE);
#else
	/* MLCD_RST */
	s3c_gpio_cfgpin(EXYNOS4_GPX1(3), S3C_GPIO_OUTPUT);
	s3c_gpio_setpull(EXYNOS4_GPX1(3), S3C_GPIO_PULL_NONE);
	/* LCD_nCS */
	s3c_gpio_cfgpin(EXYNOS4_GPY0(3), S3C_GPIO_OUTPUT);
	s3c_gpio_setpull(EXYNOS4_GPY0(3), S3C_GPIO_PULL_NONE);
	/* LCD_SCLK */
	s3c_gpio_cfgpin(EXYNOS4210_GPE2(3), S3C_GPIO_OUTPUT);
	s3c_gpio_setpull(EXYNOS4210_GPE2(3), S3C_GPIO_PULL_NONE);
	/* LCD_SDI */
	s3c_gpio_cfgpin(EXYNOS4_GPX1(1), S3C_GPIO_OUTPUT);
	s3c_gpio_setpull(EXYNOS4_GPX1(1), S3C_GPIO_PULL_NONE);
#endif

	return 0;
}

static struct s3cfb_lcd ld9040_info = {
	.width = 480,
	.height = 800,
	.p_width = 56,
	.p_height = 93,
	.bpp = 24,

	.freq = 60,
	.timing = {
		.h_fp = 16,
		.h_bp = 14,
		.h_sw = 2,
		.v_fp = 10,
		.v_fpe = 1,
		.v_bp = 4,
		.v_bpe = 1,
		.v_sw = 2,
	},
	.polarity = {
		.rise_vclk = 1,
		.inv_hsync = 1,
		.inv_vsync = 1,
		.inv_vden = 1,
	},
};

static struct lcd_platform_data ld9040_platform_data = {
	.reset = reset_lcd,
	.power_on = lcd_power_on,
	.gpio_cfg_earlysuspend = lcd_gpio_cfg_earlysuspend,
	.gpio_cfg_lateresume = lcd_gpio_cfg_lateresume,
	/* it indicates whether lcd panel is enabled from u-boot. */
	.lcd_enabled = 1,
	.reset_delay = 20,	/* 10ms */
	.power_on_delay = 20,	/* 20ms */
	.power_off_delay = 200,	/* 120ms */
	.pdata = &u1_panel_data,
};

#define LCD_BUS_NUM	3
#if !defined(CONFIG_MACH_U1_KOR_LGT)
#define DISPLAY_CS	EXYNOS4_GPY4(3)
#else
#define DISPLAY_CS	EXYNOS4_GPY0(3)
#endif
static struct spi_board_info spi_board_info[] __initdata = {
	{
		.max_speed_hz = 1200000,
		.bus_num = LCD_BUS_NUM,
		.chip_select = 0,
		.mode = SPI_MODE_3,
		.controller_data = (void *)DISPLAY_CS,
	},
};

#if !defined(CONFIG_MACH_U1_KOR_LGT)
#define DISPLAY_CLK	EXYNOS4_GPY3(1)
#define DISPLAY_SI	EXYNOS4_GPY3(3)
#else
#define DISPLAY_CLK	EXYNOS4210_GPE2(3)
#define DISPLAY_SI	EXYNOS4_GPX1(1)
#endif
static struct spi_gpio_platform_data lcd_spi_gpio_data = {
	.sck = DISPLAY_CLK,
	.mosi = DISPLAY_SI,
	.miso = SPI_GPIO_NO_MISO,
	.num_chipselect = 1,
};

static struct platform_device ld9040_spi_gpio = {
	.name = "spi_gpio",
	.id = LCD_BUS_NUM,
	.dev = {
		.parent = &s3c_device_fb.dev,
		.platform_data = &lcd_spi_gpio_data,
	},
};

static struct s3c_platform_fb fb_platform_data __initdata = {
	.hw_ver = 0x70,
	.clk_name = "fimd",
	.nr_wins = 5,
#ifdef CONFIG_FB_S5P_DEFAULT_WINDOW
	.default_win = CONFIG_FB_S5P_DEFAULT_WINDOW,
#else
	.default_win = 0,
#endif
	.swap = FB_SWAP_HWORD | FB_SWAP_WORD,
	.lcd = &ld9040_info,
};

/* reading with 3-WIRE SPI with GPIO */
static inline void setcs(u8 is_on)
{
	gpio_set_value(DISPLAY_CS, is_on);
}

static inline void setsck(u8 is_on)
{
	gpio_set_value(DISPLAY_CLK, is_on);
}

static inline void setmosi(u8 is_on)
{
	gpio_set_value(DISPLAY_SI, is_on);
}

static inline unsigned int getmiso(void)
{
	return !!gpio_get_value(DISPLAY_SI);
}

static inline void setmosi2miso(u8 is_on)
{
	if (is_on)
		s3c_gpio_cfgpin(DISPLAY_SI, S3C_GPIO_INPUT);
	else
		s3c_gpio_cfgpin(DISPLAY_SI, S3C_GPIO_OUTPUT);
}

struct spi_ops ops = {
	.setcs = setcs,
	.setsck = setsck,
	.setmosi = setmosi,
	.setmosi2miso = setmosi2miso,
	.getmiso = getmiso,
};

static void __init ld9040_fb_init(void)
{
	struct ld9040_panel_data *pdata;

	strcpy(spi_board_info[0].modalias, "ld9040");
	spi_board_info[0].platform_data = (void *)&ld9040_platform_data;

	lcdtype = max(ld9040_lcdtype, lcdtype);

#if !defined(CONFIG_PANEL_U1_NA_SPR) && !defined(CONFIG_MACH_U1_NA_USCC)
	if (lcdtype == LCDTYPE_SM2_A2)
		ld9040_platform_data.pdata = &u1_panel_data_a2;
	else if (lcdtype == LCDTYPE_M2)
		ld9040_platform_data.pdata = &u1_panel_data_m2;
#endif

	pdata = ld9040_platform_data.pdata;
	pdata->ops = &ops;

	printk(KERN_INFO "%s :: lcdtype=%d\n", __func__, lcdtype);

	spi_register_board_info(spi_board_info, ARRAY_SIZE(spi_board_info));

	if (!ld9040_platform_data.lcd_enabled)
		lcd_cfg_gpio();
	s3cfb_set_platdata(&fb_platform_data);
}
#endif

#ifdef CONFIG_FB_S5P_NT35560
static int lcd_cfg_gpio(void)
{
	int i, f3_end = 4;

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

#ifdef MAX_DRVSTR
	/* drive strength to max */
	writel(0xffffffff, S5P_VA_GPIO + 0x18c);
	writel(0xffffffff, S5P_VA_GPIO + 0x1ac);
	writel(0xffffffff, S5P_VA_GPIO + 0x1cc);
	writel(readl(S5P_VA_GPIO + 0x1ec) | 0xffffff, S5P_VA_GPIO + 0x1ec);
#else
	/* drive strength to 2X */
	writel(0xaaaaaaaa, S5P_VA_GPIO + 0x18c);
	writel(0xaaaaaaaa, S5P_VA_GPIO + 0x1ac);
	writel(0xaaaaaaaa, S5P_VA_GPIO + 0x1cc);
	writel(readl(S5P_VA_GPIO + 0x1ec) | 0xaaaaaa, S5P_VA_GPIO + 0x1ec);
#endif

	/* MLCD_RST */
	s3c_gpio_cfgpin(EXYNOS4_GPY4(5), S3C_GPIO_OUTPUT);
	s3c_gpio_setpull(EXYNOS4_GPY4(5), S3C_GPIO_PULL_NONE);

	/* LCD_nCS */
	s3c_gpio_cfgpin(EXYNOS4_GPY4(3), S3C_GPIO_OUTPUT);
	s3c_gpio_setpull(EXYNOS4_GPY4(3), S3C_GPIO_PULL_NONE);

	/* LCD_SCLK */
	s3c_gpio_cfgpin(EXYNOS4_GPY3(1), S3C_GPIO_OUTPUT);
	s3c_gpio_setpull(EXYNOS4_GPY3(1), S3C_GPIO_PULL_NONE);

	/* LCD_SDI */
	s3c_gpio_cfgpin(EXYNOS4_GPY3(3), S3C_GPIO_OUTPUT);
	s3c_gpio_setpull(EXYNOS4_GPY3(3), S3C_GPIO_PULL_NONE);

	return 0;
}

static int lcd_power_on(struct lcd_device *ld, int enable)
{
	struct regulator *regulator;

	if (ld == NULL) {
		printk(KERN_ERR "lcd device object is NULL.\n");
		return 0;
	}

	if (enable) {
		regulator = regulator_get(NULL, "vlcd_3.0v");
		if (IS_ERR(regulator))
			return 0;

		regulator_enable(regulator);
		regulator_put(regulator);

		regulator = regulator_get(NULL, "vlcd_1.8v");
		if (IS_ERR(regulator))
			return 0;

		regulator_enable(regulator);
		regulator_put(regulator);
	} else {
		regulator = regulator_get(NULL, "vlcd_1.8v");

		if (IS_ERR(regulator))
			return 0;

		if (regulator_is_enabled(regulator))
			regulator_force_disable(regulator);

		regulator_put(regulator);

		regulator = regulator_get(NULL, "vlcd_3.0v");

		if (IS_ERR(regulator))
			return 0;

		if (regulator_is_enabled(regulator))
			regulator_force_disable(regulator);

		regulator_put(regulator);
	}

	return 1;
}

static int reset_lcd(struct lcd_device *ld)
{
	int reset_gpio = -1;
	int err;

	reset_gpio = EXYNOS4_GPY4(5);

	err = gpio_request(reset_gpio, "MLCD_RST");
	if (err) {
		printk(KERN_ERR "failed to request MLCD_RST for "
		       "lcd reset control\n");
		return err;
	}

	gpio_request(reset_gpio, "MLCD_RST");

	gpio_direction_output(reset_gpio, 1);
	mdelay(5);
	gpio_direction_output(reset_gpio, 0);
	mdelay(5);
	gpio_direction_output(reset_gpio, 1);

	gpio_free(reset_gpio);

	return 1;
}

static int lcd_gpio_cfg_earlysuspend(struct lcd_device *ld)
{
	int reset_gpio = -1;
	int err;

	reset_gpio = EXYNOS4_GPY4(5);

	err = gpio_request(reset_gpio, "MLCD_RST");
	if (err) {
		printk(KERN_ERR "failed to request MLCD_RST for "
		       "lcd reset control\n");
		return err;
	}

	mdelay(5);
	gpio_direction_output(reset_gpio, 0);

	gpio_free(reset_gpio);

	return 0;
}

static int lcd_gpio_cfg_lateresume(struct lcd_device *ld)
{
	/* MLCD_RST */
	s3c_gpio_cfgpin(EXYNOS4_GPY4(5), S3C_GPIO_OUTPUT);
	s3c_gpio_setpull(EXYNOS4_GPY4(5), S3C_GPIO_PULL_NONE);

	/* LCD_nCS */
	s3c_gpio_cfgpin(EXYNOS4_GPY4(3), S3C_GPIO_OUTPUT);
	s3c_gpio_setpull(EXYNOS4_GPY4(3), S3C_GPIO_PULL_NONE);

	/* LCD_SCLK */
	s3c_gpio_cfgpin(EXYNOS4_GPY3(1), S3C_GPIO_OUTPUT);
	s3c_gpio_setpull(EXYNOS4_GPY3(1), S3C_GPIO_PULL_NONE);

	/* LCD_SDI */
	s3c_gpio_cfgpin(EXYNOS4_GPY3(3), S3C_GPIO_OUTPUT);
	s3c_gpio_setpull(EXYNOS4_GPY3(3), S3C_GPIO_PULL_NONE);

	return 0;
}

static struct s3cfb_lcd nt35560_info = {
	.width = 480,
	.height = 800,
	.p_width = 52,
	.p_height = 86,
	.bpp = 24,

	.freq = 60,
	.timing = {
		.h_fp = 10,
		.h_bp = 10,
		.h_sw = 10,
		.v_fp = 9,
		.v_fpe = 1,
		.v_bp = 4,
		.v_bpe = 1,
		.v_sw = 2,
	},
	.polarity = {
		.rise_vclk = 1,
		.inv_hsync = 1,
		.inv_vsync = 1,
		.inv_vden = 1,
	},
};

static struct lcd_platform_data nt35560_platform_data = {
	.reset = reset_lcd,
	.power_on = lcd_power_on,
	.gpio_cfg_earlysuspend = lcd_gpio_cfg_earlysuspend,
	.gpio_cfg_lateresume = lcd_gpio_cfg_lateresume,
	/* it indicates whether lcd panel is enabled from u-boot. */
	.lcd_enabled = 1,
	.reset_delay = 10,	/* 10ms */
	.power_on_delay = 10,	/* 10ms */
	.power_off_delay = 150,	/* 150ms */
};

#define LCD_BUS_NUM	3
#define DISPLAY_CS	EXYNOS4_GPY4(3)
static struct spi_board_info spi_board_info[] __initdata = {
	{
		.max_speed_hz = 1200000,
		.bus_num = LCD_BUS_NUM,
		.chip_select = 0,
		.mode = SPI_MODE_3,
		.controller_data = (void *)DISPLAY_CS,
	},
};

#define DISPLAY_CLK	EXYNOS4_GPY3(1)
#define DISPLAY_SI	EXYNOS4_GPY3(3)
static struct spi_gpio_platform_data lcd_spi_gpio_data = {
	.sck = DISPLAY_CLK,
	.mosi = DISPLAY_SI,
	.miso = SPI_GPIO_NO_MISO,
	.num_chipselect = 1,
};

static struct platform_device nt35560_spi_gpio = {
	.name = "spi_gpio",
	.id = LCD_BUS_NUM,
	.dev = {
		.parent = &s3c_device_fb.dev,
		.platform_data = &lcd_spi_gpio_data,
	},
};

static struct s3c_platform_fb fb_platform_data __initdata = {
	.hw_ver = 0x70,
	.clk_name = "fimd",
	.nr_wins = 5,
#ifdef CONFIG_FB_S5P_DEFAULT_WINDOW
	.default_win = CONFIG_FB_S5P_DEFAULT_WINDOW,
#else
	.default_win = 0,
#endif
	.swap = FB_SWAP_HWORD | FB_SWAP_WORD,
	.lcd = &nt35560_info,
};

static void __init nt35560_fb_init(void)
{
	struct ld9040_panel_data *pdata;

	strcpy(spi_board_info[0].modalias, "nt35560");
	spi_board_info[0].platform_data = (void *)&nt35560_platform_data;

	pdata = nt35560_platform_data.pdata;

	spi_register_board_info(spi_board_info, ARRAY_SIZE(spi_board_info));

	if (!nt35560_platform_data.lcd_enabled)
		lcd_cfg_gpio();
	s3cfb_set_platdata(&fb_platform_data);
}
#endif

#ifdef CONFIG_FB_S5P_AMS369FG06
static struct s3c_platform_fb ams369fg06_data __initdata = {
	.hw_ver = 0x70,
	.clk_name = "sclk_lcd",
	.nr_wins = 5,
	.default_win = CONFIG_FB_S5P_DEFAULT_WINDOW,
	.swap = FB_SWAP_HWORD | FB_SWAP_WORD,
};

#define		LCD_BUS_NUM	3
#define		DISPLAY_CS	EXYNOS4_GPB(5)
#define		DISPLAY_CLK	EXYNOS4_GPB(4)
#define		DISPLAY_SI	EXYNOS4_GPB(7)

static struct spi_board_info spi_board_info[] __initdata = {
	{
		.modalias = "ams369fg06",
		.platform_data = NULL,
		.max_speed_hz = 1200000,
		.bus_num = LCD_BUS_NUM,
		.chip_select = 0,
		.mode = SPI_MODE_3,
		.controller_data = (void *)DISPLAY_CS,
	}
};

static struct spi_gpio_platform_data ams369fg06_spi_gpio_data = {
	.sck = DISPLAY_CLK,
	.mosi = DISPLAY_SI,
	.miso = -1,
	.num_chipselect = 1,
};

static struct platform_device s3c_device_spi_gpio = {
	.name = "spi_gpio",
	.id = LCD_BUS_NUM,
	.dev = {
		.parent = &s3c_device_fb.dev,
		.platform_data = &ams369fg06_spi_gpio_data,
	},
};
#endif

#ifdef CONFIG_FB_S5P_MDNIE
static struct platform_device mdnie_device = {
	.name = "mdnie",
	.id = -1,
	.dev = {
		.parent = &exynos4_device_pd[PD_LCD0].dev,
	},
};
#endif

#endif

static struct platform_device u1_regulator_consumer = {
	.name = "u1-regulator-consumer",
	.id = -1,
};

#ifdef CONFIG_REGULATOR_MAX8997
static struct regulator_consumer_supply ldo1_supply[] = {
	REGULATOR_SUPPLY("vadc_3.3v", NULL),
};

static struct regulator_consumer_supply ldo3_supply[] = {
	REGULATOR_SUPPLY("vusb_1.1v", "usb_otg"),
	REGULATOR_SUPPLY("vmipi_1.1v", "m5mo"),
	REGULATOR_SUPPLY("vmipi_1.1v", NULL),
};

static struct regulator_consumer_supply ldo4_supply[] = {
	REGULATOR_SUPPLY("vmipi_1.8v", NULL),
};

static struct regulator_consumer_supply ldo5_supply[] = {
	REGULATOR_SUPPLY("vhsic", NULL),
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
	REGULATOR_SUPPLY("vled", NULL),
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

#if defined(CONFIG_MACH_Q1_BD) || defined(CONFIG_LEDS_MAX8997)
static struct regulator_consumer_supply led_torch_supply[] = {
	REGULATOR_SUPPLY("led_torch", NULL),
};
#endif /* CONFIG_MACH_Q1_BD */

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
REGULATOR_INIT(ldo15, "LED_A_2.8V", 2800000, 2800000, 0,
		REGULATOR_CHANGE_STATUS, -1);
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
#if !defined(CONFIG_MACH_Q1_BD) && !defined(CONFIG_LEDS_MAX8997)
		.state_mem	= {
			.disabled	= 1,
		},
#endif
	},
	.num_consumer_supplies = 1,
	.consumer_supplies = &led_movie_supply[0],
};

#if defined(CONFIG_MACH_Q1_BD) || defined(CONFIG_LEDS_MAX8997)
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
#if defined(CONFIG_MACH_Q1_BD) || defined(CONFIG_LEDS_MAX8997)
	{ MAX8997_FLASH_TORCH, &led_torch_init_data, NULL, },
#endif /* CONFIG_MACH_Q1_BD */
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

#if defined(CONFIG_TARGET_LOCALE_NA)
#define USB_PATH_AP	0
#define USB_PATH_CP	       1
#define USB_PATH_ALL	2
extern int u1_get_usb_hub_path(void);
static int max8997_muic_set_safeout(int path)
{
	struct regulator *regulator;
	int hub_usb_path = u1_get_usb_hub_path();

	if (hub_usb_path == USB_PATH_CP) {
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
	} else if (hub_usb_path == USB_PATH_AP) {
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
	} else if (hub_usb_path == USB_PATH_ALL) {
		regulator = regulator_get(NULL, "safeout1");
		if (IS_ERR(regulator))
			return -ENODEV;
		if (!regulator_is_enabled(regulator))
			regulator_enable(regulator);
		regulator_put(regulator);

		regulator = regulator_get(NULL, "safeout2");
		if (IS_ERR(regulator))
			return -ENODEV;
		if (!regulator_is_enabled(regulator))
			regulator_enable(regulator);
		regulator_put(regulator);
	}

	return 0;
}
#elif defined(CONFIG_MACH_U1_KOR_LGT)
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
	} else
		pr_info("otg error s3c_udc is null.\n");
}
#elif defined(CONFIG_MACH_U1_NA_SPR) || defined(CONFIG_MACH_U1_NA_USCC)
static void max8997_muic_usb_cb(u8 usb_mode)
{
	struct s3c_udc *udc_dev = platform_get_drvdata(&s3c_device_usbgadget);
	int ret = 0;

	pr_info("%s: usb mode=%d\n", __func__, usb_mode);
	if (udc_dev) {
		switch (usb_mode) {
		case USB_CABLE_DETACHED:
			if (udc_dev->udc_enabled)
				usb_gadget_vbus_disconnect(&udc_dev->gadget);
			break;
		case USB_CABLE_ATTACHED:
			if (!udc_dev->udc_enabled)
				usb_gadget_vbus_connect(&udc_dev->gadget);
			break;
		}
	}
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

static struct switch_dev switch_dock = {
	.name = "dock",
};

static void max8997_muic_deskdock_cb(bool attached)
{
	if (attached)
		switch_set_state(&switch_dock, 1);
	else
		switch_set_state(&switch_dock, 0);
}

static void max8997_muic_cardock_cb(bool attached)
{
	if (attached)
		switch_set_state(&switch_dock, 2);
	else
		switch_set_state(&switch_dock, 0);
}

static void max8997_muic_init_cb(void)
{
	int ret;

	/* for CarDock, DeskDock */
	ret = switch_dev_register(&switch_dock);
	if (ret < 0)
		pr_err("Failed to register dock switch. %d\n", ret);
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
#ifdef CONFIG_USB_HOST_NOTIFY
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
#endif

static struct max8997_muic_data max8997_muic = {
	.usb_cb = max8997_muic_usb_cb,
	.charger_cb = max8997_muic_charger_cb,
	.mhl_cb = max8997_muic_mhl_cb,
	.is_mhl_attached = max8997_muic_is_mhl_attached,
	.set_safeout = max8997_muic_set_safeout,
	.init_cb = max8997_muic_init_cb,
	.deskdock_cb = max8997_muic_deskdock_cb,
	.cardock_cb = max8997_muic_cardock_cb,
#if !defined(CONFIG_MACH_U1_NA_USCC)
	.cfg_uart_gpio = max8997_muic_cfg_uart_gpio,
#endif
	.jig_uart_cb = max8997_muic_jig_uart_cb,
#ifdef CONFIG_USB_HOST_NOTIFY
	.host_notify_cb = max8997_muic_host_notify_cb,
#else
	.host_notify_cb = NULL,
#endif
#if !defined(CONFIG_MACH_U1_NA_USCC)
	.gpio_uart_sel =  GPIO_UART_SEL,
#endif
	.gpio_usb_sel = GPIO_USB_SEL,
};

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

#ifdef CONFIG_TARGET_LOCALE_KOR
#define SYSTEM_REV_SND 0x05
#else
#define SYSTEM_REV_SND 0x09
#endif

#ifdef CONFIG_SND_SOC_U1_MC1N2
static DEFINE_SPINLOCK(mic_bias_lock);
static bool mc1n2_mainmic_bias;
static bool mc1n2_submic_bias;

static void set_shared_mic_bias(void)
{
	if (system_rev >= 0x03)
		gpio_set_value(GPIO_MIC_BIAS_EN, mc1n2_mainmic_bias
			       || mc1n2_submic_bias);
	else
		gpio_set_value(GPIO_EAR_MIC_BIAS_EN, mc1n2_mainmic_bias
			       || mc1n2_submic_bias);
}

void sec_set_sub_mic_bias(bool on)
{
#ifdef CONFIG_SND_SOC_USE_EXTERNAL_MIC_BIAS
#if defined(CONFIG_MACH_Q1_BD)
	gpio_set_value(GPIO_SUB_MIC_BIAS_EN, on);
#else
	if (system_rev < SYSTEM_REV_SND) {
		unsigned long flags;
		spin_lock_irqsave(&mic_bias_lock, flags);
		mc1n2_submic_bias = on;
		set_shared_mic_bias();
		spin_unlock_irqrestore(&mic_bias_lock, flags);
	} else
		gpio_set_value(GPIO_SUB_MIC_BIAS_EN, on);
#endif /* #if defined(CONFIG_MACH_Q1_BD) */
#endif
}

void sec_set_main_mic_bias(bool on)
{
#ifdef CONFIG_SND_SOC_USE_EXTERNAL_MIC_BIAS
#if defined(CONFIG_MACH_Q1_BD)
	gpio_set_value(GPIO_MIC_BIAS_EN, on);
#else
	if (system_rev < SYSTEM_REV_SND) {
		unsigned long flags;
		spin_lock_irqsave(&mic_bias_lock, flags);
		mc1n2_mainmic_bias = on;
		set_shared_mic_bias();
		spin_unlock_irqrestore(&mic_bias_lock, flags);
	} else
		gpio_set_value(GPIO_MIC_BIAS_EN, on);
#endif /* #if defined(CONFIG_MACH_Q1_BD) */
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

static void u1_sound_init(void)
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
#if defined(CONFIG_MACH_U1_NA_SPR) || defined(CONFIG_MACH_U1_NA_USCC)
static struct sec_bat_adc_table_data temper_table[] = {
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
#ifdef CONFIG_LEDS_GPIO
struct gpio_led leds_gpio[] = {
	{
		.name = "red",
		.default_trigger = NULL,
			/* "default-on", // Turn ON RED LED at boot time ! */
		.gpio = GPIO_SVC_LED_RED,
		.active_low = 0,
	},
	{
		.name = "blue",
		.default_trigger = NULL,
			/* "default-on", // Turn ON BLUE LED at boot time ! */
		.gpio = GPIO_SVC_LED_BLUE,
		.active_low = 0,
	}
};

struct gpio_led_platform_data leds_gpio_platform_data = {
		.num_leds = ARRAY_SIZE(leds_gpio),
		.leds = leds_gpio,
};

struct platform_device sec_device_leds_gpio = {
		.name   = "leds-gpio",
		.id     = -1,
		.dev = { .platform_data = &leds_gpio_platform_data },
};
#endif /* CONFIG_LEDS_GPIO */

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
#if !defined(CONFIG_MACH_U1_NA_SPR) && !defined(CONFIG_MACH_U1_NA_USCC)
	{
		.code = KEY_HOMEPAGE,
		.gpio = GPIO_OK_KEY,
		.active_low = 1,
		.type = EV_KEY,
		.wakeup = 1,
		.debounce_interval = 10,
	},			/* ok key */
#endif
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
		.delay_ms = 15,
		.check_count = 20,
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

void tsp_register_callback(void *function)
{
	charging_cbs.tsp_set_charging_cable = function;
}

void tsp_read_ta_status(void *ta_status)
{
	*(bool *)ta_status = is_cable_attached;
}
#ifdef CONFIG_TOUCHSCREEN_ATMEL_MXT224_GC
static void mxt224_power_on(void)
{
	s3c_gpio_cfgpin(GPIO_TSP_LDO_ON, S3C_GPIO_OUTPUT);
	s3c_gpio_setpull(GPIO_TSP_LDO_ON, S3C_GPIO_PULL_NONE);
	gpio_set_value(GPIO_TSP_LDO_ON, 1);
	msleep(70);
	s3c_gpio_setpull(GPIO_TSP_INT, S3C_GPIO_PULL_NONE);
	s3c_gpio_cfgpin(GPIO_TSP_INT, S3C_GPIO_SFN(0xf));
	msleep(40);
}

static void mxt224_power_off(void)
{
	s3c_gpio_cfgpin(GPIO_TSP_INT, S3C_GPIO_INPUT);
	s3c_gpio_setpull(GPIO_TSP_INT, S3C_GPIO_PULL_DOWN);

	s3c_gpio_cfgpin(GPIO_TSP_LDO_ON, S3C_GPIO_OUTPUT);
	s3c_gpio_setpull(GPIO_TSP_LDO_ON, S3C_GPIO_PULL_NONE);
	gpio_set_value(GPIO_TSP_LDO_ON, 0);
}

static u8 t7_config[] = {GEN_POWERCONFIG_T7,
	64, 255, 50
};
static u8 t8_config[] = {GEN_ACQUISITIONCONFIG_T8,
	10, 0, 5, 1, 0, 0, 9, 27
};
static u8 t9_config[] = {TOUCH_MULTITOUCHSCREEN_T9,
	143, 0, 0, 18, 11, 0, 16, 32, 2, 0,
	0, 3, 1, 46, 10, 5, 40, 10, 31, 3,
	223, 1, 0, 0, 0, 0, 0, 0, 0, 0,
	18
};
static u8 t15_config[] = {TOUCH_KEYARRAY_T15,
	131, 16, 11, 2, 1, 0, 0, 45, 4, 0,
	0
};
static u8 t18_config[] = {SPT_COMCONFIG_T18,
	0, 0
};
static u8 t19_config[] = {SPT_GPIOPWM_T19,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0
};
static u8 t20_config[] = {PROCI_GRIPFACESUPPRESSION_T20,
	19, 0, 0, 5, 5, 0, 0, 30, 20, 4, 15,
	10
};
static u8 t22_config[] = {PROCG_NOISESUPPRESSION_T22,
	5, 0, 0, 0, 0, 0, 0, 3, 27, 0,
	0, 29, 34, 39, 49, 58, 3
};
static u8 t23_config[] = {TOUCH_PROXIMITY_T23,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0
};
static u8 t24_config[] = {PROCI_ONETOUCHGESTUREPROCESSOR_T24,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0
};
static u8 t25_config[] = {SPT_SELFTEST_T25,
	0, 0
};
static u8 t27_config[] = {PROCI_TWOTOUCHGESTUREPROCESSOR_T27,
	0, 0, 0, 0, 0, 0, 0
};
static u8 t28_config[] = {SPT_CTECONFIG_T28,
	1, 0, 2, 16, 63, 60
};
static u8 end_config[] = {RESERVED_T255};

static const u8 *mxt224_config[] = {
	t7_config,
	t8_config,
	t9_config,
	t15_config,
	t18_config,
	t19_config,
	t20_config,
	t22_config,
	t23_config,
	t24_config,
	t25_config,
	t27_config,
	t28_config,
	end_config,
};


static struct mxt224_platform_data mxt224_data = {
	.max_finger_touches = 10,
	.gpio_read_done = GPIO_TSP_INT,
	.config = mxt224_config,
	.min_x = 0,
	.max_x = 479,
	.min_y = 0,
	.max_y = 799,
	.min_z = 0,
	.max_z = 255,
	.min_w = 0,
	.max_w = 30,
	.power_on = mxt224_power_on,
	.power_off = mxt224_power_off,
};


#endif
#ifdef CONFIG_TOUCHSCREEN_ATMEL_MXT224_U1
static void mxt224_power_on(void)
{
	s3c_gpio_cfgpin(GPIO_TSP_LDO_ON, S3C_GPIO_OUTPUT);
	s3c_gpio_setpull(GPIO_TSP_LDO_ON, S3C_GPIO_PULL_NONE);
	gpio_set_value(GPIO_TSP_LDO_ON, 1);
	msleep(70);
	s3c_gpio_setpull(GPIO_TSP_INT, S3C_GPIO_PULL_NONE);
	s3c_gpio_cfgpin(GPIO_TSP_INT, S3C_GPIO_SFN(0xf));
	msleep(40);
	/* printk("mxt224_power_on is finished\n"); */
}

static void mxt224_power_off(void)
{
	s3c_gpio_cfgpin(GPIO_TSP_INT, S3C_GPIO_INPUT);
	s3c_gpio_setpull(GPIO_TSP_INT, S3C_GPIO_PULL_DOWN);

	s3c_gpio_cfgpin(GPIO_TSP_LDO_ON, S3C_GPIO_OUTPUT);
	s3c_gpio_setpull(GPIO_TSP_LDO_ON, S3C_GPIO_PULL_NONE);
	gpio_set_value(GPIO_TSP_LDO_ON, 0);
	/* printk("mxt224_power_off is finished\n"); */
}

/*
  Configuration for MXT224
*/
#define MXT224_THRESHOLD_BATT		40
#define MXT224_THRESHOLD_BATT_INIT		55
#define MXT224_THRESHOLD_CHRG		70
#define MXT224_NOISE_THRESHOLD_BATT		30
#define MXT224_NOISE_THRESHOLD_CHRG		40
#if defined(CONFIG_MACH_U1_NA_SPR) || defined(CONFIG_MACH_U1_NA_USCC)
#define MXT224_MOVFILTER_BATT           47
#else
#define MXT224_MOVFILTER_BATT		11
#endif
#define MXT224_MOVFILTER_CHRG		47
#define MXT224_ATCHCALST		4
#define MXT224_ATCHCALTHR		35

static u8 t7_config[] = { GEN_POWERCONFIG_T7,
	48,			/* IDLEACQINT */
	255,			/* ACTVACQINT */
	25			/* ACTV2IDLETO: 25 * 200ms = 5s */
};

static u8 t8_config[] = { GEN_ACQUISITIONCONFIG_T8,
	10, 0, 5, 1, 0, 0, MXT224_ATCHCALST, MXT224_ATCHCALTHR
};				/*byte 3: 0 */

static u8 t9_config[] = { TOUCH_MULTITOUCHSCREEN_T9,
	131, 0, 0, 19, 11, 0, 32, MXT224_THRESHOLD_BATT, 2, 1,
	0,
	15,			/* MOVHYSTI */
	1, MXT224_MOVFILTER_BATT, MXT224_MAX_MT_FINGERS, 5, 40, 10, 31, 3,
	223, 1, 0, 0, 0, 0, 143, 55, 143, 90, 18
};

static u8 t18_config[] = { SPT_COMCONFIG_T18,
	0, 1
};

static u8 t20_config[] = { PROCI_GRIPFACESUPPRESSION_T20,
	7, 0, 0, 0, 0, 0, 0, 30, 20, 4, 15, 10
};

static u8 t22_config[] = { PROCG_NOISESUPPRESSION_T22,
	143, 0, 0, 0, 0, 0, 0, 3, MXT224_NOISE_THRESHOLD_BATT, 0,
	0, 29, 34, 39, 49, 58, 3
};

static u8 t28_config[] = { SPT_CTECONFIG_T28,
			   0, 0, 3, 16, 19, 60
};
static u8 end_config[] = { RESERVED_T255 };

static const u8 *mxt224_config[] = {
	t7_config,
	t8_config,
	t9_config,
	t18_config,
	t20_config,
	t22_config,
	t28_config,
	end_config,
};

/*
  Configuration for MXT224-E
*/
#ifdef CONFIG_TARGET_LOCALE_NAATT_TEMP
#define MXT224E_THRESHOLD_BATT		50
#define MXT224E_THRESHOLD_CHRG		40
#define MXT224E_T48_THRESHOLD_BATT		33
#define MXT224E_CALCFG_BATT		0x72
#define MXT224E_CALCFG_CHRG		0x72
#define MXT224E_ATCHFRCCALTHR_NORMAL		40
#define MXT224E_ATCHFRCCALRATIO_NORMAL		55
#define MXT224E_GHRGTIME_BATT		22
#define MXT224E_GHRGTIME_CHRG		22
#define MXT224E_ATCHCALST		4
#define MXT224E_ATCHCALTHR		35
#define MXT224E_BLEN_BATT		32
#define MXT224E_T48_BLEN_BATT		0
#define MXT224E_BLEN_CHRG		0
#define MXT224E_T48_BLEN_CHRG		0
#define MXT224E_MOVFILTER_BATT		14
#define MXT224E_MOVFILTER_CHRG		46
#define MXT224E_ACTVSYNCSPERX_NORMAL		29
#define MXT224E_NEXTTCHDI_NORMAL		0
#define MXT224E_NEXTTCHDI_CHRG		1
#else
#define MXT224E_THRESHOLD_BATT		50
#define MXT224E_T48_THRESHOLD_BATT		28
#define MXT224E_THRESHOLD_CHRG		40
#define MXT224E_CALCFG_BATT		0x42
#define MXT224E_CALCFG_CHRG		0x52
#if defined(CONFIG_TARGET_LOCALE_NA)
#define MXT224E_ATCHFRCCALTHR_NORMAL		45
#define MXT224E_ATCHFRCCALRATIO_NORMAL		60
#else
#define MXT224E_ATCHFRCCALTHR_NORMAL		40
#define MXT224E_ATCHFRCCALRATIO_NORMAL		55
#endif
#define MXT224E_GHRGTIME_BATT		22
#define MXT224E_GHRGTIME_CHRG		22
#define MXT224E_ATCHCALST		4
#define MXT224E_ATCHCALTHR		35
#define MXT224E_BLEN_BATT		32
#define MXT224E_BLEN_CHRG		16
#define MXT224E_T48_BLEN_BATT		0
#define MXT224E_T48_BLEN_CHRG		0
#define MXT224E_MOVFILTER_BATT		13
#define MXT224E_MOVFILTER_CHRG		46
#define MXT224E_ACTVSYNCSPERX_NORMAL		32
#define MXT224E_NEXTTCHDI_NORMAL		0
#endif

#if defined(CONFIG_TARGET_LOCALE_NAATT_TEMP)
static u8 t7_config_e[] = { GEN_POWERCONFIG_T7,
	48, 255, 25
};

static u8 t8_config_e[] = { GEN_ACQUISITIONCONFIG_T8,
	MXT224E_GHRGTIME_BATT, 0, 5, 1, 0, 0,
	MXT224E_ATCHCALST, MXT224E_ATCHCALTHR,
	MXT224E_ATCHFRCCALTHR_NORMAL,
	MXT224E_ATCHFRCCALRATIO_NORMAL
};

static u8 t9_config_e[] = { TOUCH_MULTITOUCHSCREEN_T9,
	139, 0, 0, 19, 11, 0, MXT224E_BLEN_BATT, MXT224E_THRESHOLD_BATT, 2, 1,
	10,
	15,			/* MOVHYSTI */
	1, MXT224E_MOVFILTER_BATT, MXT224_MAX_MT_FINGERS, 5, 40, 10, 31, 3,
	223, 1, 10, 10, 10, 10, 143, 40, 143, 80,
	18, 15, 50, 50, MXT224E_NEXTTCHDI_NORMAL
};

static u8 t15_config_e[] = { TOUCH_KEYARRAY_T15,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

static u8 t18_config_e[] = { SPT_COMCONFIG_T18,
	0, 0
};

static u8 t23_config_e[] = { TOUCH_PROXIMITY_T23,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

static u8 t25_config_e[] = { SPT_SELFTEST_T25,
	0, 0, 0, 0, 0, 0, 0, 0
};

static u8 t38_config_e[] = { SPT_USERDATA_T38,
	0, 0, 0, 0, 0, 0, 0, 0
};

static u8 t40_config_e[] = { PROCI_GRIPSUPPRESSION_T40,
	0, 0, 0, 0, 0
};

static u8 t42_config_e[] = { PROCI_TOUCHSUPPRESSION_T42,
	0, 0, 0, 0, 0, 0, 0, 0
};

static u8 t46_config_e[] = { SPT_CTECONFIG_T46,
	0, 3, 16, MXT224E_ACTVSYNCSPERX_NORMAL, 0, 0, 1, 0, 0
};

static u8 t47_config_e[] = { PROCI_STYLUS_T47,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

static u8 t48_config_e[] = { PROCG_NOISESUPPRESSION_T48,
	3, 132, MXT224E_CALCFG_BATT, 20, 0, 0, 0, 0, 1, 2,
	0, 0, 0, 6, 6, 0, 0, 64, 4, 64,
	10, 0, 10, 5, 0, 20, 0, 5, 0, 0,
	0, 0, 0, 0, MXT224E_T48_BLEN_BATT, MXT224E_T48_THRESHOLD_BATT, 2,
	15,
	1, MXT224E_MOVFILTER_BATT,
	MXT224_MAX_MT_FINGERS, 5, 40, 235, 235, 10, 10, 160, 50, 143,
	80, 18, 15, MXT224E_NEXTTCHDI_NORMAL
};

static u8 t48_config_chrg_e[] = { PROCG_NOISESUPPRESSION_T48,
	3, 132, MXT224E_CALCFG_CHRG, 0, 0, 0, 0, 0, 10, 15,
	0, 0, 0, 6, 6, 0, 0, 64, 4, 64,
	10, 0, 9, 5, 0, 15, 0, 20, 0, 0,
	0, 0, 0, 0, MXT224E_BLEN_CHRG, MXT224E_THRESHOLD_CHRG, 2,
	15,			/* MOVHYSTI */
	1, 47,
	MXT224_MAX_MT_FINGERS, 5, 40, 235, 235, 10, 10, 160, 50, 143,
	80, 18, 10, MXT224E_NEXTTCHDI_CHRG
};

#else
static u8 t7_config_e[] = { GEN_POWERCONFIG_T7,
	48,			/* IDLEACQINT */
	255,			/* ACTVACQINT */
	25			/* ACTV2IDLETO: 25 * 200ms = 5s */
};

static u8 t8_config_e[] = { GEN_ACQUISITIONCONFIG_T8,
	MXT224E_GHRGTIME_BATT, 0, 5, 1, 0, 0,
	MXT224E_ATCHCALST, MXT224E_ATCHCALTHR,
	MXT224E_ATCHFRCCALTHR_NORMAL,
	MXT224E_ATCHFRCCALRATIO_NORMAL
};

/* MXT224E_0V5_CONFIG */
/* NEXTTCHDI added */
#ifdef CONFIG_TARGET_LOCALE_NA
static u8 t9_config_e[] = { TOUCH_MULTITOUCHSCREEN_T9,
	139, 0, 0, 19, 11, 0, MXT224E_BLEN_BATT, MXT224E_THRESHOLD_BATT, 2, 1,
	10,
	10,			/* MOVHYSTI */
	1, MXT224E_MOVFILTER_BATT, MXT224_MAX_MT_FINGERS, 5, 40, 10, 31, 3,
	223, 1, 10, 10, 10, 10, 143, 40, 143, 80,
	18, 15, 50, 50, 0
};
#else
static u8 t9_config_e[] = { TOUCH_MULTITOUCHSCREEN_T9,
	139, 0, 0, 19, 11, 0, MXT224E_BLEN_BATT, MXT224E_THRESHOLD_BATT, 2, 1,
	10,
	15,			/* MOVHYSTI */
	1, MXT224E_MOVFILTER_BATT, MXT224_MAX_MT_FINGERS, 5, 40, 10, 31, 3,
	223, 1, 10, 10, 10, 10, 143, 40, 143, 80,
	18, 15, 50, 50, MXT224E_NEXTTCHDI_NORMAL
};
#endif

static u8 t15_config_e[] = { TOUCH_KEYARRAY_T15,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

static u8 t18_config_e[] = { SPT_COMCONFIG_T18,
	0, 0
};

static u8 t23_config_e[] = { TOUCH_PROXIMITY_T23,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

static u8 t25_config_e[] = { SPT_SELFTEST_T25,
	0, 0, 0, 0, 0, 0, 0, 0
};

static u8 t38_config_e[] = { SPT_USERDATA_T38,
	0, 1, 14, 23, 44, 0, 0, 0
};

static u8 t40_config_e[] = { PROCI_GRIPSUPPRESSION_T40,
	0, 0, 0, 0, 0
};

static u8 t42_config_e[] = { PROCI_TOUCHSUPPRESSION_T42,
	0, 0, 0, 0, 0, 0, 0, 0
};

static u8 t46_config_e[] = { SPT_CTECONFIG_T46,
	0, 3, 16, MXT224E_ACTVSYNCSPERX_NORMAL, 0, 0, 1, 0, 0
};

static u8 t47_config_e[] = { PROCI_STYLUS_T47,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

/*MXT224E_0V5_CONFIG */
#ifdef CONFIG_TARGET_LOCALE_NA
static u8 t48_config_chrg_e[] = { PROCG_NOISESUPPRESSION_T48,
	3, 132, MXT224E_CALCFG_CHRG, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 6, 6, 0, 0, 64, 4, 64,
	10, 0, 9, 5, 0, 15, 0, 20, 0, 0,
	0, 0, 0, 0, MXT224E_T48_BLEN_CHRG, MXT224E_THRESHOLD_CHRG, 2,
	10,			/* MOVHYSTI */
	1, 47,
	MXT224_MAX_MT_FINGERS, 5, 40, 240, 245, 10, 10, 148, 50, 143,
	80, 18, 10, 0
};

static u8 t48_config_e[] = { PROCG_NOISESUPPRESSION_T48,
	3, 132, MXT224E_CALCFG_BATT, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 6, 6, 0, 0, 48, 4, 48,
	10, 0, 10, 5, 0, 20, 0, 5, 0, 0,	/*byte 27 original value 20 */
	0, 0, 0, 0, MXT224E_T48_BLEN_BATT, MXT224E_T48_THRESHOLD_BATT, 2,
	10,
	1, 46,
	MXT224_MAX_MT_FINGERS, 5, 40, 240, 245, 10, 10, 148, 50, 143,
	80, 18, 15, MXT224E_NEXTTCHDI_NORMAL
};
#else
static u8 t48_config_chrg_e[] = { PROCG_NOISESUPPRESSION_T48,
	3, 132, MXT224E_CALCFG_CHRG, 0, 0, 0, 0, 0, 10, 15,
	0, 0, 0, 6, 6, 0, 0, 64, 4, 64,
	10, 0, 9, 5, 0, 15, 0, 20, 0, 0,
	0, 0, 0, 0, 0, MXT224E_THRESHOLD_CHRG, 2,
	15,			/* MOVHYSTI */
	1, 47,
	MXT224_MAX_MT_FINGERS, 5, 40, 235, 235, 10, 10, 160, 50, 143,
	80, 18, 10, 0
};

static u8 t48_config_e[] = { PROCG_NOISESUPPRESSION_T48,
	3, 132, MXT224E_CALCFG_BATT, 0, 0, 0, 0, 0, 10, 15,
	0, 0, 0, 6, 6, 0, 0, 48, 4, 48,
	10, 0, 10, 5, 0, 20, 0, 5, 0, 0,	/*byte 27 original value 20 */
	0, 0, 0, 0, 32, MXT224E_THRESHOLD_BATT, 2,
	15,
	1, 46,
	MXT224_MAX_MT_FINGERS, 5, 40, 10, 10, 10, 10, 143, 40, 143,
	80, 18, 15, 0
};
#endif				/*CONFIG_TARGET_LOCALE_NA */
#endif				/*CONFIG_TARGET_LOCALE_NAATT */

static u8 end_config_e[] = { RESERVED_T255 };

static const u8 *mxt224e_config[] = {
	t7_config_e,
	t8_config_e,
	t9_config_e,
	t15_config_e,
	t18_config_e,
	t23_config_e,
	t25_config_e,
	t38_config_e,
	t40_config_e,
	t42_config_e,
	t46_config_e,
	t47_config_e,
	t48_config_e,
	end_config_e,
};

static struct mxt224_platform_data mxt224_data = {
	.max_finger_touches = MXT224_MAX_MT_FINGERS,
	.gpio_read_done = GPIO_TSP_INT,
	.config = mxt224_config,
	.config_e = mxt224e_config,
	.t48_config_batt_e = t48_config_e,
	.t48_config_chrg_e = t48_config_chrg_e,
	.min_x = 0,
	.max_x = 479,
	.min_y = 0,
	.max_y = 799,
	.min_z = 0,
	.max_z = 255,
	.min_w = 0,
	.max_w = 30,
	.atchcalst = MXT224_ATCHCALST,
	.atchcalsthr = MXT224_ATCHCALTHR,
	.tchthr_batt = MXT224_THRESHOLD_BATT,
	.tchthr_batt_init = MXT224_THRESHOLD_BATT_INIT,
	.tchthr_charging = MXT224_THRESHOLD_CHRG,
	.noisethr_batt = MXT224_NOISE_THRESHOLD_BATT,
	.noisethr_charging = MXT224_NOISE_THRESHOLD_CHRG,
	.movfilter_batt = MXT224_MOVFILTER_BATT,
	.movfilter_charging = MXT224_MOVFILTER_CHRG,
	.atchcalst_e = MXT224E_ATCHCALST,
	.atchcalsthr_e = MXT224E_ATCHCALTHR,
	.tchthr_batt_e = MXT224E_THRESHOLD_BATT,
	.tchthr_charging_e = MXT224E_THRESHOLD_CHRG,
	.calcfg_batt_e = MXT224E_CALCFG_BATT,
	.calcfg_charging_e = MXT224E_CALCFG_CHRG,
	.atchfrccalthr_e = MXT224E_ATCHFRCCALTHR_NORMAL,
	.atchfrccalratio_e = MXT224E_ATCHFRCCALRATIO_NORMAL,
	.chrgtime_batt_e = MXT224E_GHRGTIME_BATT,
	.chrgtime_charging_e = MXT224E_GHRGTIME_CHRG,
	.blen_batt_e = MXT224E_BLEN_BATT,
	.blen_charging_e = MXT224E_T48_BLEN_CHRG,
	.movfilter_batt_e = MXT224E_MOVFILTER_BATT,
	.movfilter_charging_e = MXT224E_MOVFILTER_CHRG,
	.actvsyncsperx_e = MXT224E_ACTVSYNCSPERX_NORMAL,
	.nexttchdi_e = MXT224E_NEXTTCHDI_NORMAL,
	.power_on = mxt224_power_on,
	.power_off = mxt224_power_off,
	.register_cb = tsp_register_callback,
	.read_ta_status = tsp_read_ta_status,
};

#endif				/*CONFIG_TOUCHSCREEN_ATMEL_MXT224_U1 */

#if defined(CONFIG_TOUCHSCREEN_ATMEL_MXT540E)
static void mxt540e_power_on(void)
{
	gpio_request(GPIO_TSP_SDA, "TSP_SDA");
	gpio_request(GPIO_TSP_SCL, "TSP_SCL");

	s3c_gpio_cfgpin(GPIO_TSP_SDA, S3C_GPIO_SFN(3));
	s3c_gpio_cfgpin(GPIO_TSP_SCL, S3C_GPIO_SFN(3));
	s3c_gpio_setpull(GPIO_TSP_SDA, S3C_GPIO_PULL_UP);
	s3c_gpio_setpull(GPIO_TSP_SCL, S3C_GPIO_PULL_UP);

	s3c_gpio_cfgpin(GPIO_TSP_LDO_ON, S3C_GPIO_OUTPUT);
	s3c_gpio_setpull(GPIO_TSP_LDO_ON, S3C_GPIO_PULL_NONE);
	gpio_direction_output(GPIO_TSP_LDO_ON, GPIO_LEVEL_HIGH);
	msleep(MXT540E_HW_RESET_TIME);
	s3c_gpio_setpull(GPIO_TSP_INT, S3C_GPIO_PULL_NONE);
	s3c_gpio_cfgpin(GPIO_TSP_INT, S3C_GPIO_SFN(0xf));

	gpio_free(GPIO_TSP_SDA);
	gpio_free(GPIO_TSP_SCL);
}

static void mxt540e_power_off(void)
{
	gpio_request(GPIO_TSP_SDA, "TSP_SDA");
	gpio_request(GPIO_TSP_SCL, "TSP_SCL");

	s3c_gpio_cfgpin(GPIO_TSP_SDA, S3C_GPIO_OUTPUT);
	s3c_gpio_cfgpin(GPIO_TSP_SCL, S3C_GPIO_OUTPUT);
	s3c_gpio_setpull(GPIO_TSP_SDA, S3C_GPIO_PULL_NONE);
	s3c_gpio_setpull(GPIO_TSP_SCL, S3C_GPIO_PULL_NONE);
	gpio_direction_output(GPIO_TSP_SDA, GPIO_LEVEL_LOW);
	gpio_direction_output(GPIO_TSP_SCL, GPIO_LEVEL_LOW);

	s3c_gpio_cfgpin(GPIO_TSP_INT, S3C_GPIO_INPUT);
	s3c_gpio_setpull(GPIO_TSP_INT, S3C_GPIO_PULL_DOWN);

	s3c_gpio_cfgpin(GPIO_TSP_LDO_ON, S3C_GPIO_OUTPUT);
	s3c_gpio_setpull(GPIO_TSP_LDO_ON, S3C_GPIO_PULL_NONE);
	gpio_direction_output(GPIO_TSP_LDO_ON, GPIO_LEVEL_LOW);

	gpio_free(GPIO_TSP_SDA);
	gpio_free(GPIO_TSP_SCL);
}

static void mxt540e_power_on_oled(void)
{
	gpio_request(GPIO_OLED_DET, "OLED_DET");

	mxt540e_power_on();

	s3c_gpio_setpull(GPIO_OLED_DET, S3C_GPIO_PULL_NONE);
	s3c_gpio_cfgpin(GPIO_OLED_DET, S3C_GPIO_SFN(0xf));

	gpio_free(GPIO_OLED_DET);

	printk(KERN_INFO "[TSP] %s\n", __func__);
}

static void mxt540e_power_off_oled(void)
{
	gpio_request(GPIO_OLED_DET, "OLED_DET");

	s3c_gpio_cfgpin(GPIO_OLED_DET, S3C_GPIO_OUTPUT);
	s3c_gpio_setpull(GPIO_OLED_DET, S3C_GPIO_PULL_NONE);
	gpio_direction_output(GPIO_OLED_DET, GPIO_LEVEL_LOW);

	mxt540e_power_off();

	gpio_free(GPIO_OLED_DET);

	printk(KERN_INFO "[TSP] %s\n", __func__);
}

/*
  Configuration for MXT540E
*/
#define MXT540E_MAX_MT_FINGERS		10
#define MXT540E_CHRGTIME_BATT		48
#define MXT540E_CHRGTIME_CHRG		48
#define MXT540E_THRESHOLD_BATT		50
#define MXT540E_THRESHOLD_CHRG		40
#define MXT540E_ACTVSYNCSPERX_BATT		34
#define MXT540E_ACTVSYNCSPERX_CHRG		34
#define MXT540E_CALCFG_BATT		98
#define MXT540E_CALCFG_CHRG		114
#define MXT540E_ATCHFRCCALTHR_WAKEUP		8
#define MXT540E_ATCHFRCCALRATIO_WAKEUP		180
#define MXT540E_ATCHFRCCALTHR_NORMAL		40
#define MXT540E_ATCHFRCCALRATIO_NORMAL		55

static u8 t7_config_e[] = { GEN_POWERCONFIG_T7,
	48, 255, 50
};

static u8 t8_config_e[] = { GEN_ACQUISITIONCONFIG_T8,
	MXT540E_CHRGTIME_BATT, 0, 5, 1, 0, 0, 4, 20,
	MXT540E_ATCHFRCCALTHR_WAKEUP, MXT540E_ATCHFRCCALRATIO_WAKEUP
};

static u8 t9_config_e[] = { TOUCH_MULTITOUCHSCREEN_T9,
	131, 0, 0, 16, 26, 0, 192, MXT540E_THRESHOLD_BATT, 2, 6,
	10, 10, 10, 80, MXT540E_MAX_MT_FINGERS, 20, 40, 20, 31, 3,
	255, 4, 3, 3, 2, 2, 136, 60, 136, 40,
	18, 15, 0, 0, 0
};

static u8 t15_config_e[] = { TOUCH_KEYARRAY_T15,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

static u8 t18_config_e[] = { SPT_COMCONFIG_T18,
	0, 0
};

static u8 t19_config_e[] = { SPT_GPIOPWM_T19,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

static u8 t24_config_e[] = { PROCI_ONETOUCHGESTUREPROCESSOR_T24,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

static u8 t25_config_e[] = { SPT_SELFTEST_T25,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

static u8 t27_config_e[] = { PROCI_TWOTOUCHGESTUREPROCESSOR_T27,
	0, 0, 0, 0, 0, 0, 0
};

static u8 t40_config_e[] = { PROCI_GRIPSUPPRESSION_T40,
	0, 0, 0, 0, 0
};

static u8 t42_config_e[] = { PROCI_TOUCHSUPPRESSION_T42,
	0, 0, 0, 0, 0, 0, 0, 0
};

static u8 t43_config_e[] = { SPT_DIGITIZER_T43,
	0, 0, 0, 0, 0, 0, 0
};

static u8 t46_config_e[] = { SPT_CTECONFIG_T46,
	0, 0, 16, MXT540E_ACTVSYNCSPERX_BATT, 0, 0, 1, 0
};

static u8 t47_config_e[] = { PROCI_STYLUS_T47,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

static u8 t48_config_e[] = { PROCG_NOISESUPPRESSION_T48,
	3, 132, MXT540E_CALCFG_BATT, 0, 0, 0, 0, 0, 1, 2,
	0, 0, 0, 6, 6, 0, 0, 28, 4, 64,
	10, 0, 20, 6, 0, 30, 0, 0, 0, 0,
	0, 0, 0, 0, 192, MXT540E_THRESHOLD_BATT, 2, 10, 10, 47,
	MXT540E_MAX_MT_FINGERS, 5, 20, 253, 0, 7, 7, 160, 55, 136,
	0, 18, 5, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0
};

static u8 t48_config_chrg_e[] = { PROCG_NOISESUPPRESSION_T48,
	3, 132, MXT540E_CALCFG_CHRG, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 6, 6, 0, 0, 36, 4, 64,
	10, 0, 10, 6, 0, 20, 0, 0, 0, 0,
	0, 0, 0, 0, 112, MXT540E_THRESHOLD_CHRG, 2, 10, 5, 47,
	MXT540E_MAX_MT_FINGERS, 5, 20, 253, 0, 7, 7, 160, 55, 136,
	0, 18, 10, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0
};

static u8 t52_config_e[] = { TOUCH_PROXKEY_T52,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

static u8 t55_config_e[] = {ADAPTIVE_T55,
	0, 0, 0, 0, 0, 0
};

static u8 t57_config_e[] = {SPT_GENERICDATA_T57,
	243, 25, 1
};

static u8 t61_config_e[] = {SPT_TIMER_T61,
	0, 0, 0, 0, 0
};

static u8 end_config_e[] = { RESERVED_T255 };

static const u8 *mxt540e_config[] = {
	t7_config_e,
	t8_config_e,
	t9_config_e,
	t15_config_e,
	t18_config_e,
	t19_config_e,
	t24_config_e,
	t25_config_e,
	t27_config_e,
	t40_config_e,
	t42_config_e,
	t43_config_e,
	t46_config_e,
	t47_config_e,
	t48_config_e,
	t52_config_e,
	t55_config_e,
	t57_config_e,
	t61_config_e,
	end_config_e,
};

struct mxt540e_platform_data mxt540e_data = {
	.max_finger_touches = MXT540E_MAX_MT_FINGERS,
	.gpio_read_done = GPIO_TSP_INT,
	.config_e = mxt540e_config,
	.min_x = 0,
	.max_x = 799,
	.min_y = 0,
	.max_y = 1279,
	.min_z = 0,
	.max_z = 255,
	.min_w = 0,
	.max_w = 30,
	.chrgtime_batt = MXT540E_CHRGTIME_BATT,
	.chrgtime_charging = MXT540E_CHRGTIME_CHRG,
	.tchthr_batt = MXT540E_THRESHOLD_BATT,
	.tchthr_charging = MXT540E_THRESHOLD_CHRG,
	.actvsyncsperx_batt = MXT540E_ACTVSYNCSPERX_BATT,
	.actvsyncsperx_charging = MXT540E_ACTVSYNCSPERX_CHRG,
	.calcfg_batt_e = MXT540E_CALCFG_BATT,
	.calcfg_charging_e = MXT540E_CALCFG_CHRG,
	.atchfrccalthr_e = MXT540E_ATCHFRCCALTHR_NORMAL,
	.atchfrccalratio_e = MXT540E_ATCHFRCCALRATIO_NORMAL,
	.t48_config_batt_e = t48_config_e,
	.t48_config_chrg_e = t48_config_chrg_e,
	.power_on = mxt540e_power_on,
	.power_off = mxt540e_power_off,
	.power_on_with_oleddet = mxt540e_power_on_oled,
	.power_off_with_oleddet = mxt540e_power_off_oled,
	.register_cb = tsp_register_callback,
	.read_ta_status = tsp_read_ta_status,
};
#endif

#ifdef CONFIG_EPEN_WACOM_G5SP
static int p6_wacom_init_hw(void);
static int p6_wacom_exit_hw(void);
static int p6_wacom_suspend_hw(void);
static int p6_wacom_resume_hw(void);
static int p6_wacom_early_suspend_hw(void);
static int p6_wacom_late_resume_hw(void);
static int p6_wacom_reset_hw(void);
static void p6_wacom_register_callbacks(struct wacom_g5_callbacks *cb);

static struct wacom_g5_platform_data p6_wacom_platform_data = {
	.x_invert = 1,
	.y_invert = 0,
	.xy_switch = 1,
	.min_x = 0,
	.max_x = WACOM_POSX_MAX,
	.min_y = 0,
	.max_y = WACOM_POSY_MAX,
	.min_pressure = 0,
	.max_pressure = WACOM_PRESSURE_MAX,
	.gpio_pendct = GPIO_PEN_PDCT,
	.init_platform_hw = p6_wacom_init_hw,
/*	.exit_platform_hw =,	*/
	.suspend_platform_hw = p6_wacom_suspend_hw,
	.resume_platform_hw = p6_wacom_resume_hw,
	.early_suspend_platform_hw = p6_wacom_early_suspend_hw,
	.late_resume_platform_hw = p6_wacom_late_resume_hw,
	.reset_platform_hw = p6_wacom_reset_hw,
	.register_cb = p6_wacom_register_callbacks,
};

#endif

#ifdef CONFIG_EPEN_WACOM_G5SP
static int p6_wacom_suspend_hw(void)
{
	return p6_wacom_early_suspend_hw();
}

static int p6_wacom_resume_hw(void)
{
	return p6_wacom_late_resume_hw();
}

static int p6_wacom_early_suspend_hw(void)
{
	gpio_direction_input(GPIO_PEN_PDCT);
	gpio_set_value(GPIO_PEN_RESET, 0);
	return 0;
}

static int p6_wacom_late_resume_hw(void)
{
	gpio_direction_output(GPIO_PEN_PDCT, 1);
	gpio_set_value(GPIO_PEN_RESET, 1);
	return 0;
}

static int p6_wacom_reset_hw(void)
{
	p6_wacom_early_suspend_hw();
	msleep(200);
	p6_wacom_late_resume_hw();

	return 0;
}

static void p6_wacom_register_callbacks(struct wacom_g5_callbacks *cb)
{
	wacom_callbacks = cb;
};
#endif /* CONFIG_EPEN_WACOM_G5SP */


#ifdef CONFIG_S3C_DEV_I2C8_EMUL
static struct i2c_board_info i2c_devs8_emul[];
#endif
#ifdef CONFIG_KEYBOARD_CYPRESS_TOUCH
static void touchkey_init_hw(void)
{
	gpio_request(GPIO_3_TOUCH_INT, "3_TOUCH_INT");
	s3c_gpio_setpull(GPIO_3_TOUCH_INT, S3C_GPIO_PULL_NONE);
	s5p_register_gpio_interrupt(GPIO_3_TOUCH_INT);
	gpio_direction_input(GPIO_3_TOUCH_INT);

	i2c_devs8_emul[0].irq = gpio_to_irq(GPIO_3_TOUCH_INT);
	irq_set_irq_type(gpio_to_irq(GPIO_3_TOUCH_INT), IRQF_TRIGGER_FALLING);
	s3c_gpio_cfgpin(GPIO_3_TOUCH_INT, S3C_GPIO_SFN(0xf));
}

static int touchkey_suspend(void)
{
	struct regulator *regulator;

	regulator = regulator_get(NULL, TK_REGULATOR_NAME);
	if (IS_ERR(regulator))
		return 0;
	if (regulator_is_enabled(regulator))
		regulator_force_disable(regulator);

	regulator_put(regulator);

	return 1;
}

static int touchkey_resume(void)
{
	struct regulator *regulator;

	regulator = regulator_get(NULL, TK_REGULATOR_NAME);
	if (IS_ERR(regulator))
		return 0;
	regulator_enable(regulator);
	regulator_put(regulator);

	return 1;
}

static int touchkey_power_on(bool on)
{
	int ret;

	if (on) {
		gpio_direction_output(GPIO_3_TOUCH_INT, 1);
		irq_set_irq_type(gpio_to_irq(GPIO_3_TOUCH_INT),
					IRQF_TRIGGER_FALLING);
		s3c_gpio_cfgpin(GPIO_3_TOUCH_INT, S3C_GPIO_SFN(0xf));
		s3c_gpio_setpull(GPIO_3_TOUCH_INT, S3C_GPIO_PULL_NONE);
	} else {
		gpio_direction_input(GPIO_3_TOUCH_INT);
	}

	if (on)
		ret = touchkey_resume();
	else
		ret = touchkey_suspend();

	return ret;
}

static int touchkey_led_power_on(bool on)
{
#if defined(LED_LDO_WITH_EN_PIN)
	if (on)
		gpio_direction_output(GPIO_3_TOUCH_EN, 1);
	else
		gpio_direction_output(GPIO_3_TOUCH_EN, 0);
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



#ifdef CONFIG_I2C_S3C2410
/* I2C0 */
static struct i2c_board_info i2c_devs0[] __initdata = {
	{I2C_BOARD_INFO("24c128", 0x50),},	/* Samsung S524AD0XD1 */
	{I2C_BOARD_INFO("24c128", 0x52),},	/* Samsung S524AD0XD1 */
};

#ifdef CONFIG_S3C_DEV_I2C1
/* I2C1 */
static struct i2c_board_info i2c_devs1[] __initdata = {
	{
		I2C_BOARD_INFO("k3g", 0x69),
		.irq = IRQ_EINT(1),
	},
	{
		I2C_BOARD_INFO("k3dh", 0x19),
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
#ifdef CONFIG_TOUCHSCREEN_ATMEL_MXT224_U1
	{
		I2C_BOARD_INFO(MXT224_DEV_NAME, 0x4a),
		.platform_data = &mxt224_data,
	},
#endif
#ifdef CONFIG_TOUCHSCREEN_ATMEL_MXT540E
	{
		I2C_BOARD_INFO(MXT540E_DEV_NAME, 0x4c),
		.platform_data = &mxt540e_data,
	},
#endif
#ifdef CONFIG_TOUCHSCREEN_ATMEL_MXT224_GC
	{
		I2C_BOARD_INFO(MXT224_DEV_NAME, 0x4a),
		.platform_data = &mxt224_data,
	},
#endif
};
#endif
#ifdef CONFIG_S3C_DEV_I2C4
/* I2C4 */
static struct i2c_board_info i2c_devs4[] __initdata = {
#if defined(CONFIG_WIMAX_CMC)
	{
		I2C_BOARD_INFO("max8893_wmx", 0x3E),
		.platform_data = NULL,
	},
#endif /* CONFIG_WIMAX_CMC */
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
#ifdef CONFIG_SND_SOC_U1_MC1N2
	{
		I2C_BOARD_INFO("mc1n2", 0x3a),	/* MC1N2 */
		.platform_data = &mc1n2_pdata,
	},
#endif
#ifdef CONFIG_EPEN_WACOM_G5SP
	{
		I2C_BOARD_INFO("wacom_g5sp_i2c", 0x56),
		.platform_data = &p6_wacom_platform_data,
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
#ifdef CONFIG_VIDEO_TVOUT
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

#ifdef CONFIG_S3C_DEV_I2C8_EMUL
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
#ifdef CONFIG_SENSORS_CM3663
static int cm3663_ldo(bool on)
{
	struct regulator *regulator;

	if (on) {
		regulator = regulator_get(NULL, "vled");
		if (IS_ERR(regulator))
			return 0;
		regulator_enable(regulator);
		regulator_put(regulator);
	} else {
		regulator = regulator_get(NULL, "vled");
		if (IS_ERR(regulator))
			return 0;
		if (regulator_is_enabled(regulator))
			regulator_force_disable(regulator);
		regulator_put(regulator);
	}

	return 0;
}

#ifdef CONFIG_USBHUB_USB3803
int usb3803_hw_config(void)
{
	int i;
	int usb_gpio[] = {GPIO_USB_RESET_N,
					GPIO_USB_BYPASS_N,
					GPIO_USB_CLOCK_EN};

	for (i = 0; i < 3; i++) {
		s3c_gpio_cfgpin(usb_gpio[i], S3C_GPIO_OUTPUT);
		s3c_gpio_setpull(usb_gpio[i], S3C_GPIO_PULL_NONE);
		gpio_set_value(usb_gpio[i], S3C_GPIO_SETPIN_ZERO);
		s5p_gpio_set_drvstr(usb_gpio[i], S5P_GPIO_DRVSTR_LV1);
		/* need to check drvstr 1 or 2 */
	}
	return 0;
}

int usb3803_reset_n(int val)
{
	gpio_set_value(GPIO_USB_RESET_N, !!val);
	return 0;
}

int usb3803_bypass_n(int val)
{
	gpio_set_value(GPIO_USB_BYPASS_N, !!val);
	return 0;
}

int usb3803_clock_en(int val)
{
	gpio_set_value(GPIO_USB_CLOCK_EN, !!val);
	return 0;
}
#endif /* CONFIG_USBHUB_USB3803 */

static struct cm3663_platform_data cm3663_pdata = {
	.proximity_power = cm3663_ldo,
};
#ifdef CONFIG_SENSORS_PAS2M110
static struct pas2m110_platform_data pas2m110_pdata = {
	.proximity_power = cm3663_ldo,
};
#endif
#endif
#ifdef CONFIG_SENSORS_GP2A_ANALOG
static int gp2a_power(bool on)
{
	struct regulator *regulator;

	if (on) {
		regulator = regulator_get(NULL, "vled");
		if (IS_ERR(regulator))
			return 0;
		regulator_enable(regulator);
		regulator_put(regulator);
	} else {
		regulator = regulator_get(NULL, "vled");
		if (IS_ERR(regulator))
			return 0;
		if (regulator_is_enabled(regulator))
			regulator_force_disable(regulator);
		regulator_put(regulator);
	}

	return 0;
}

static struct gp2a_platform_data gp2a_pdata = {
	.p_out = GPIO_PS_ALS_INT,
	.power = gp2a_power,
};
#endif

static struct i2c_board_info i2c_devs11_emul[] __initdata = {
#ifdef CONFIG_MACH_U1_BD
	{
		I2C_BOARD_INFO("cm3663", 0x20),
		.irq = GPIO_PS_ALS_INT,
		.platform_data = &cm3663_pdata,
	},
#ifdef CONFIG_SENSORS_PAS2M110
	{
		I2C_BOARD_INFO("pas2m110", (0x88>>1)),
		.irq = GPIO_PS_ALS_INT,
		.platform_data = &pas2m110_pdata,
	},
#endif
#endif
#ifdef CONFIG_MACH_Q1_BD
	{
		I2C_BOARD_INFO("gp2a", (0x88 >> 1)),
		.platform_data = &gp2a_pdata,
	},
#endif
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

#if defined(CONFIG_VIDEO_S5K5BAFX) || defined(CONFIG_VIDEO_S5K5BBGX)
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


#ifdef CONFIG_S3C_DEV_I2C17_EMUL
#ifdef CONFIG_USBHUB_USB3803
/* I2C17_EMUL */
static struct i2c_gpio_platform_data i2c17_platdata = {
	.sda_pin = GPIO_USB_I2C_SDA,
	.scl_pin = GPIO_USB_I2C_SCL,
};

struct platform_device s3c_device_i2c17 = {
	.name = "i2c-gpio",
	.id = 17,
	.dev.platform_data = &i2c17_platdata,
};

#endif
#endif /* CONFIG_S3C_DEV_I2C17_EMUL */

#ifdef CONFIG_USBHUB_USB3803
struct usb3803_platform_data usb3803_pdata = {
	.init_needed    =  1,
	.es_ver         = 1,
	.inital_mode    = USB_3803_MODE_STANDBY,
	.hw_config      = usb3803_hw_config,
	.reset_n        = usb3803_reset_n,
	.bypass_n       = usb3803_bypass_n,
	.clock_en       = usb3803_clock_en,
};

static struct i2c_board_info i2c_devs17_emul[] __initdata = {
	{
		I2C_BOARD_INFO(USB3803_I2C_NAME, 0x08),
		.platform_data  = &usb3803_pdata,
	},
};
#endif /* CONFIG_USBHUB_USB3803 */





#endif

#ifdef CONFIG_TOUCHSCREEN_S3C2410
static struct s3c2410_ts_mach_info s3c_ts_platform __initdata = {
	.delay = 10000,
	.presc = 49,
	.oversampling_shift = 2,
	.cal_x_max = 480,
	.cal_y_max = 800,
	.cal_param = {
		33, -9156, 34720100, 14819, 57, -4234968, 65536,
	},
};
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


#ifdef CONFIG_FB_S5P_MIPI_DSIM
#ifdef CONFIG_FB_S5P_S6E8AA0
/* for Geminus based on MIPI-DSI interface */
static struct s3cfb_lcd s6e8aa0 = {
	.name = "s6e8aa0",
	.width = 800,
	.height = 1280,
	.p_width = 64,
	.p_height = 106,
	.bpp = 24,

	.freq = 57,

	/* minumun value is 0 except for wr_act time. */
	.cpu_timing = {
		.cs_setup = 0,
		.wr_setup = 0,
		.wr_act = 1,
		.wr_hold = 0,
	},

	.timing = {
		.h_fp = 10,
		.h_bp = 10,
		.h_sw = 10,
		.v_fp = 13,
		.v_fpe = 1,
		.v_bp = 1,
		.v_bpe = 1,
		.v_sw = 2,
		.cmd_allow_len = 11,	/*v_fp=stable_vfp + cmd_allow_len */
		.stable_vfp = 2,
	},

	.polarity = {
		.rise_vclk = 1,
		.inv_hsync = 0,
		.inv_vsync = 0,
		.inv_vden = 0,
	},
};
#endif
static struct s3c_platform_fb fb_platform_data __initdata = {
	.hw_ver		= 0x70,
	.clk_name	= "fimd",
	.nr_wins	= 5,
#ifdef CONFIG_FB_S5P_DEFAULT_WINDOW
	.default_win	= CONFIG_FB_S5P_DEFAULT_WINDOW,
#else
	.default_win = 0,
#endif
	.swap = FB_SWAP_HWORD | FB_SWAP_WORD,
#ifdef CONFIG_FB_S5P_S6E8AA0
	.lcd = &s6e8aa0
#endif
};

#ifdef CONFIG_FB_S5P_S6E8AA0
static int reset_lcd(void)
{
	int err;

	/* Set GPY4[5] OUTPUT HIGH */
	err = gpio_request(EXYNOS4_GPY4(5), "MLCD_RST");
	if (err) {
		printk(KERN_ERR "failed to request GPY4(5) for "
		       "lcd reset control\n");
		return -EPERM;
	}

	gpio_direction_output(EXYNOS4_GPY4(5), 1);
	usleep_range(5000, 5000);
	gpio_set_value(EXYNOS4_GPY4(5), 0);
	usleep_range(5000, 5000);
	gpio_set_value(EXYNOS4_GPY4(5), 1);
	usleep_range(5000, 5000);

	gpio_free(EXYNOS4_GPY4(5));

	return 0;
}
#endif
static void lcd_cfg_gpio(void)
{
	/* MLCD_RST */
	s3c_gpio_cfgpin(EXYNOS4_GPY4(5), S3C_GPIO_OUTPUT);
	s3c_gpio_setpull(EXYNOS4_GPY4(5), S3C_GPIO_PULL_NONE);

	/* LCD_EN */
	s3c_gpio_cfgpin(GPIO_LCD_EN, S3C_GPIO_OUTPUT);
	s3c_gpio_setpull(GPIO_LCD_EN, S3C_GPIO_PULL_NONE);

	return;
}

static int lcd_power_on(void *ld, int enable)
{
	struct regulator *regulator;
	int err;

	printk(KERN_INFO "%s : enable=%d\n", __func__, enable);

	if (ld == NULL) {
		printk(KERN_ERR "lcd device object is NULL.\n");
		return -EPERM;
	}

	err = gpio_request(EXYNOS4_GPY4(5), "MLCD_RST");
	if (err) {
		printk(KERN_ERR "failed to request GPY4[5] for "
		       "MLCD_RST control\n");
		return -EPERM;
	}

	err = gpio_request(GPIO_LCD_EN, "LCD_EN");
	if (err) {
		printk(KERN_ERR "failed to request GPY3[1] for "
				"LCD_EN control\n");
		return -EPERM;
	}

	if (enable) {
#ifdef CONFIG_MACH_Q1_BD
		if (system_rev < 8) {
			regulator = regulator_get(NULL, "vlcd_2.2v");
			if (IS_ERR(regulator))
				return 0;
			regulator_enable(regulator);
			regulator_put(regulator);
		} else
			gpio_set_value(GPIO_LCD_EN, GPIO_LEVEL_HIGH);
#endif
		regulator = regulator_get(NULL, "vlcd_3.0v");
		if (IS_ERR(regulator))
			return 0;
		regulator_enable(regulator);
		regulator_put(regulator);
	} else {
		regulator = regulator_get(NULL, "vlcd_3.0v");
		if (IS_ERR(regulator))
			return 0;
		if (regulator_is_enabled(regulator))
			regulator_force_disable(regulator);
		regulator_put(regulator);
#ifdef CONFIG_MACH_Q1_BD
		if (system_rev < 8) {
			regulator = regulator_get(NULL, "vlcd_2.2v");
			if (IS_ERR(regulator))
				return 0;
			if (regulator_is_enabled(regulator))
				regulator_force_disable(regulator);
			regulator_put(regulator);
		} else
			gpio_set_value(GPIO_LCD_EN, GPIO_LEVEL_LOW);
#endif

		gpio_set_value(EXYNOS4_GPY4(5), 0);
	}

	/* Release GPIO */
	gpio_free(EXYNOS4_GPY4(5));
	gpio_free(GPIO_LCD_EN);

	return 0;
}

static void __init mipi_fb_init(void)
{
	struct s5p_platform_dsim *dsim_pd = NULL;
	struct mipi_ddi_platform_data *mipi_ddi_pd = NULL;
	struct dsim_lcd_config *dsim_lcd_info = NULL;

	/* set platform data */

	/* gpio pad configuration for rgb and spi interface. */
	lcd_cfg_gpio();

	/*
	 * register lcd panel data.
	 */
	printk(KERN_INFO "%s :: fb_platform_data.hw_ver = 0x%x\n",
	       __func__, fb_platform_data.hw_ver);

	dsim_pd = (struct s5p_platform_dsim *)
	    s5p_device_dsim.dev.platform_data;

	dsim_pd->platform_rev = 1;

	dsim_lcd_info = dsim_pd->dsim_lcd_info;

#ifdef CONFIG_FB_S5P_S6E8AA0
	dsim_lcd_info->lcd_panel_info = (void *)&s6e8aa0;

	/* 483Mbps for Q1 */
	dsim_pd->dsim_info->p = 4;
	dsim_pd->dsim_info->m = 161;
	dsim_pd->dsim_info->s = 1;
#endif

	mipi_ddi_pd = (struct mipi_ddi_platform_data *)
	    dsim_lcd_info->mipi_ddi_pd;
	mipi_ddi_pd->lcd_reset = reset_lcd;
	mipi_ddi_pd->lcd_power_on = lcd_power_on;

	platform_device_register(&s5p_device_dsim);

	s3cfb_set_platdata(&fb_platform_data);

	printk(KERN_INFO
	       "platform data of %s lcd panel has been registered.\n",
	       dsim_pd->lcd_panel_name);
}
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

#include <linux/usb/android_composite.h>
static void __init smdkc210_usbgadget_init(void)
{
	struct s5p_usbgadget_platdata *pdata = &smdkc210_usbgadget_pdata;

#if defined(CONFIG_USB_ANDROID) || defined(CONFIG_USB_G_ANDROID)
	struct android_usb_platform_data *android_pdata =
		s3c_device_android_usb.dev.platform_data;
	if (android_pdata) {
		unsigned int newluns = 2;
		printk(KERN_DEBUG "usb: %s: default luns=%d, new luns=%d\n",
				__func__, android_pdata->nluns, newluns);
		android_pdata->nluns = newluns;
	} else {
		printk(KERN_DEBUG "usb: %s android_pdata is not available\n",
				__func__);
	}
#endif

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

#if defined(CONFIG_WIMAX_CMC)
	&s3c_device_cmc732,
#endif

#ifdef CONFIG_BATTERY_SAMSUNG
	&samsung_device_battery,
#endif

#ifdef CONFIG_FB_S5P
	&s3c_device_fb,
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
#if defined(CONFIG_S3C_DEV_I2C8_EMUL)
	&s3c_device_i2c8,
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
#if defined(CONFIG_VIDEO_S5K5BAFX) || defined(CONFIG_VIDEO_S5K5BBGX)
	&s3c_device_i2c12,
#endif
#ifdef CONFIG_SAMSUNG_MHL
		&s3c_device_i2c15,
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
#if defined(CONFIG_USBHUB_USB3803)
#if defined(CONFIG_S3C_DEV_I2C17_EMUL)
	&s3c_device_i2c17,	/* USB HUB */
#endif
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
#ifdef CONFIG_TOUCHSCREEN_S3C2410
#ifdef CONFIG_S3C_DEV_ADC
	&s3c_device_ts,
#elif CONFIG_S3C_DEV_ADC1
	&s3c_device_ts1,
#endif
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
#ifdef CONFIG_LEDS_GPIO
	&sec_device_leds_gpio,
#endif
#ifdef	CONFIG_LEDS_MAX8997
	&sec_device_leds_max8997,
#endif
#ifdef CONFIG_CHARGER_MAX8922_U1
	&max8922_device_charger,
#endif
#ifdef CONFIG_S5P_SYSTEM_MMU
	&SYSMMU_PLATDEV(fimc0),
	&SYSMMU_PLATDEV(fimc1),
	&SYSMMU_PLATDEV(fimc2),
	&SYSMMU_PLATDEV(fimc3),
	&SYSMMU_PLATDEV(2d),
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

/* mainline fimd */
#ifdef CONFIG_FB_S3C
	&s5p_device_fimd0,
#if defined(CONFIG_LCD_AMS369FG06)
	&s3c_device_spi_gpio,
#elif defined(CONFIG_LCD_WA101S)
	&smdkc210_lcd_wa101s,
#elif defined(CONFIG_LCD_LTE480WV)
	&smdkc210_lcd_lte480wv,
#endif
#endif
/* legacy fimd */
#ifdef CONFIG_FB_S5P_AMS369FG06
	&s3c_device_spi_gpio,
#endif
#ifdef CONFIG_FB_S5P_LD9040
	&ld9040_spi_gpio,
#endif
#ifdef CONFIG_FB_S5P_NT35560
	&nt35560_spi_gpio,
#endif
#ifdef CONFIG_FB_S5P_MDNIE
	&mdnie_device,
#endif
#ifdef CONFIG_VIDEO_TVOUT
	&s5p_device_tvout,
	&s5p_device_cec,
	&s5p_device_hpd,
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
#ifdef CONFIG_VIDEO_FIMG2D
	&s5p_device_fimg2d,
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
#ifdef CONFIG_USB_ANDROID_RNDIS
	&s3c_device_rndis,
#endif
#if defined(CONFIG_USB_ANDROID) || defined(CONFIG_USB_G_ANDROID)
	&s3c_device_android_usb,
	&s3c_device_usb_mass_storage,
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
#ifdef CONFIG_USB_HOST_NOTIFY
	&s3c_device_usb_otghcd,
#endif
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

#if defined(CONFIG_VIDEO_TVOUT)
static struct s5p_platform_hpd hdmi_hpd_data __initdata = {

};

static struct s5p_platform_cec hdmi_cec_data __initdata = {

};
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
#if defined(CONFIG_USE_MFC_CMA) && defined(CONFIG_MACH_Q1_BD)
			if (reg->start == 0x67200000) {
				if (!memblock_is_region_reserved
					(reg->start, 0x600000) &&
					memblock_reserve(reg->start,
						reg->size) >= 0)
					reg->reserved = 1;
			} else if (reg->start == 0x68400000)
				reg->reserved = 1;
			else
#endif
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

		if (reg->reserved)
			pr_info("S5P/CMA: Reserved 0x%08x/0x%08x for '%s'\n",
				reg->start, reg->size, reg->name);
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
#ifdef CONFIG_VIDEO_SAMSUNG_MEMSIZE_FIMD
		{
			.name = "fimd",
			.size = CONFIG_VIDEO_SAMSUNG_MEMSIZE_FIMD * SZ_1K,
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
#if defined(CONFIG_USE_MFC_CMA) && defined(CONFIG_MACH_Q1_BD)
			.start = 0x68400000,
#else
			.start = 0,
#endif
		},
#endif
#ifdef CONFIG_VIDEO_SAMSUNG_MEMSIZE_MFC0
		{
			.name = "mfc0",
			.size = CONFIG_VIDEO_SAMSUNG_MEMSIZE_MFC0 * SZ_1K,
			{
				.alignment = 1 << 17,
			},
#if defined(CONFIG_USE_MFC_CMA) && defined(CONFIG_MACH_Q1_BD)
			.start = 0x67200000,
#else
			.start = 0,
#endif
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
#ifdef CONFIG_VIDEO_SAMSUNG_MEMSIZE_FIMG2D
		{
			.name = "fimg2d",
			.size = CONFIG_VIDEO_SAMSUNG_MEMSIZE_FIMG2D * SZ_1K,
			.start = 0,
		},
#endif
#ifdef CONFIG_VIDEO_SAMSUNG_MEMSIZE_TVOUT
		{
			.name = "tvout",
			.size = CONFIG_VIDEO_SAMSUNG_MEMSIZE_TVOUT * SZ_1K,
#ifdef CONFIG_USE_TVOUT_CMA
			.start = 0x65800000,
			.reserved = 1,
#else
			.start = 0,
#endif
		},
#endif
		{
			.size = 0,
		},
	};

	static const char map[] __initconst =
		"android_pmem.0=pmem;android_pmem.1=pmem_gpu1;"
		"s3cfb.0=fimd;exynos4-fb.0=fimd;"
		"s3c-fimc.0=fimc0;s3c-fimc.1=fimc1;s3c-fimc.2=fimc2;"
		"exynos4210-fimc.0=fimc0;exynos4210-fimc.1=fimc1;"
		"exynos4210-fimc.2=fimc2;exynos4210-fimc3=fimc3;"
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
		"s5p-fimg2d=fimg2d;"
		"s5p-tvout=tvout";

	cma_set_defaults(regions, map);
	exynos4_cma_region_reserve(regions, NULL);

}
#endif

static void __init exynos_reserve(void)
{
#ifdef CONFIG_USE_TVOUT_CMA
	if (dma_declare_contiguous(&s5p_device_tvout.dev,
			CONFIG_VIDEO_SAMSUNG_MEMSIZE_TVOUT * SZ_1K,
			0x65800000, 0))
		printk(KERN_ERR "%s: failed to reserve contiguous "
			"memory region for TVOUT\n", __func__);
#endif

#ifdef CONFIG_USE_MFC_CMA
	if (dma_declare_contiguous(&s5p_device_mfc.dev,
			SZ_1M * 40, 0x67800000, 0))
		printk(KERN_ERR "%s: failed to reserve contiguous "
			"memory region for MFC0/1\n", __func__);
#endif
}


static void __init exynos_sysmmu_init(void)
{
	ASSIGN_SYSMMU_POWERDOMAIN(fimc0, &exynos4_device_pd[PD_CAM].dev);
	ASSIGN_SYSMMU_POWERDOMAIN(fimc1, &exynos4_device_pd[PD_CAM].dev);
	ASSIGN_SYSMMU_POWERDOMAIN(fimc2, &exynos4_device_pd[PD_CAM].dev);
	ASSIGN_SYSMMU_POWERDOMAIN(fimc3, &exynos4_device_pd[PD_CAM].dev);
	ASSIGN_SYSMMU_POWERDOMAIN(jpeg, &exynos4_device_pd[PD_CAM].dev);
	ASSIGN_SYSMMU_POWERDOMAIN(fimd0, &exynos4_device_pd[PD_LCD0].dev);
	ASSIGN_SYSMMU_POWERDOMAIN(2d, &exynos4_device_pd[PD_LCD0].dev);
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
#ifdef CONFIG_FB_S3C
	sysmmu_set_owner(&SYSMMU_PLATDEV(fimd0).dev, &s5p_device_fimd0.dev);
#endif
#ifdef CONFIG_VIDEO_FIMG2D
	sysmmu_set_owner(&SYSMMU_PLATDEV(2d).dev, &s5p_device_fimg2d.dev);
#endif
#ifdef CONFIG_VIDEO_TVOUT
	sysmmu_set_owner(&SYSMMU_PLATDEV(tv).dev, &s5p_device_tvout.dev);
#endif
#if defined(CONFIG_VIDEO_MFC5X) || defined(CONFIG_VIDEO_SAMSUNG_S5P_MFC)
	sysmmu_set_owner(&SYSMMU_PLATDEV(mfc_l).dev, &s5p_device_mfc.dev);
	sysmmu_set_owner(&SYSMMU_PLATDEV(mfc_r).dev, &s5p_device_mfc.dev);
#endif
}

static void __init smdkc210_map_io(void)
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

#ifdef CONFIG_EPEN_WACOM_G5SP
static int p6_wacom_init_hw(void)
{
	int gpio;
	int ret;

	gpio = GPIO_PEN_RESET;
	ret = gpio_request(gpio, "PEN_RESET");
	s3c_gpio_cfgpin(gpio, S3C_GPIO_OUTPUT);
	gpio_direction_output(gpio, 1);

	gpio = GPIO_PEN_SLP;
	ret = gpio_request(gpio, "PEN_SLP");
	s3c_gpio_cfgpin(gpio, S3C_GPIO_SFN(0x1));
	s3c_gpio_setpull(gpio, S3C_GPIO_PULL_UP);
	gpio_direction_output(gpio, 0);

	gpio = GPIO_PEN_PDCT;
	ret = gpio_request(gpio, "PEN_PDCT");
	s3c_gpio_cfgpin(gpio, S3C_GPIO_SFN(0xf));
	s3c_gpio_setpull(gpio, S3C_GPIO_PULL_UP);
	s5p_register_gpio_interrupt(gpio);
	gpio_direction_input(gpio);

	irq_set_irq_type(gpio_to_irq(gpio), IRQ_TYPE_EDGE_BOTH);

	gpio = GPIO_PEN_IRQ;
	ret = gpio_request(gpio, "PEN_IRQ");
	s3c_gpio_setpull(gpio, S3C_GPIO_PULL_NONE);
	s5p_register_gpio_interrupt(gpio);
	gpio_direction_input(gpio);

	i2c_devs6[1].irq = gpio_to_irq(gpio);
	irq_set_irq_type(i2c_devs6[1].irq, IRQ_TYPE_EDGE_RISING);

	s3c_gpio_cfgpin(gpio, S3C_GPIO_SFN(0xf));

	return 0;
}

static int __init p6_wacom_init(void)
{
	p6_wacom_init_hw();
	printk(KERN_INFO "[E-PEN] : wacom IC initialized.\n");
	return 0;
}
#endif

static void __init smdkc210_machine_init(void)
{
#ifdef CONFIG_S3C64XX_DEV_SPI
	struct clk *sclk = NULL;
	struct clk *prnt = NULL;
	struct device *spi0_dev = &exynos_device_spi0.dev;
#endif
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
#ifdef CONFIG_S3C_DEV_I2C3
	universal_tsp_init();
	s3c_i2c3_set_platdata(NULL);
	i2c_register_board_info(3, i2c_devs3, ARRAY_SIZE(i2c_devs3));
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
#ifdef CONFIG_S3C_DEV_I2C8_EMUL
#ifdef CONFIG_KEYBOARD_CYPRESS_TOUCH
	touchkey_init_hw();
#endif
	i2c_register_board_info(8, i2c_devs8_emul, ARRAY_SIZE(i2c_devs8_emul));
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
#if defined(CONFIG_VIDEO_S5K5BAFX) || defined(CONFIG_VIDEO_S5K5BBGX)
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
#ifdef CONFIG_S3C_DEV_I2C17_EMUL
#ifdef CONFIG_USBHUB_USB3803
	i2c_register_board_info(17, i2c_devs17_emul,
						ARRAY_SIZE(i2c_devs17_emul));
#endif
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

#ifdef CONFIG_FB_S3C
#ifdef CONFIG_LCD_AMS369FG06
	spi_register_board_info(spi_board_info, ARRAY_SIZE(spi_board_info));
#endif
	s5p_fimd0_set_platdata(&smdkc210_lcd0_pdata);
#ifdef CONFIG_EXYNOS_DEV_PD
	s5p_device_fimd0.dev.parent = &exynos4_device_pd[PD_LCD0].dev;
#endif
#endif

#ifdef CONFIG_FB_S5P
#ifdef CONFIG_FB_S5P_AMS369FG06
	spi_register_board_info(spi_board_info, ARRAY_SIZE(spi_board_info));
	s3cfb_set_platdata(&ams369fg06_data);
#else
	s3cfb_set_platdata(NULL);
#endif
#ifdef CONFIG_EXYNOS_DEV_PD
	s3c_device_fb.dev.parent = &exynos4_device_pd[PD_LCD0].dev;
#endif
#endif

#ifdef CONFIG_EXYNOS_DEV_PD
#ifdef CONFIG_VIDEO_JPEG
	s5p_device_jpeg.dev.parent = &exynos4_device_pd[PD_CAM].dev;
#endif
#endif
#if defined(CONFIG_VIDEO_TVOUT)
	s5p_hdmi_hpd_set_platdata(&hdmi_hpd_data);
	s5p_hdmi_cec_set_platdata(&hdmi_cec_data);
#ifdef CONFIG_EXYNOS_DEV_PD
	s5p_device_tvout.dev.parent = &exynos4_device_pd[PD_TV].dev;
#endif
#endif
#ifdef CONFIG_TOUCHSCREEN_S3C2410
#ifdef CONFIG_S3C_DEV_ADC
	s3c24xx_ts_set_platdata(&s3c_ts_platform);
#endif
#ifdef CONFIG_S3C_DEV_ADC1
	s3c24xx_ts1_set_platdata(&s3c_ts_platform);
#endif
#endif
#ifdef CONFIG_ANDROID_PMEM
	android_pmem_set_platdata();
#endif
#ifdef CONFIG_VIDEO_FIMC
	/* fimc */
	s3c_fimc0_set_platdata(&fimc_plat);
	s3c_fimc1_set_platdata(NULL);
	s3c_fimc2_set_platdata(&fimc_plat);
	s3c_fimc3_set_platdata(NULL);
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

#ifdef CONFIG_VIDEO_FIMG2D
	s5p_fimg2d_set_platdata(&fimg2d_data);
#ifdef CONFIG_EXYNOS_DEV_PD
	s5p_device_fimg2d.dev.parent = &exynos4_device_pd[PD_LCD0].dev;
#endif
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
#ifdef CONFIG_FB_S5P_LD9040
	ld9040_fb_init();
#endif
#ifdef CONFIG_FB_S5P_NT35560
	nt35560_fb_init();
#endif
#if defined(CONFIG_FB_S5P_MIPI_DSIM)
	mipi_fb_init();
#endif

#ifdef CONFIG_SND_SOC_U1_MC1N2
	u1_sound_init();
#endif

	brcm_wlan_init();

	exynos_sysmmu_init();

	platform_add_devices(smdkc210_devices, ARRAY_SIZE(smdkc210_devices));

#ifdef CONFIG_SEC_THERMISTOR
	platform_device_register(&sec_device_thermistor);
#endif

#ifdef CONFIG_FB_S3C
	exynos4_fimd0_setup_clock(&s5p_device_fimd0.dev, "mout_mpll",
				  800 * MHZ);
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

MACHINE_START(SMDKC210, MODEL_NAME)
	/* Maintainer: Kukjin Kim <kgene.kim@samsung.com> */
	.boot_params	= S5P_PA_SDRAM + 0x100,
	.init_irq	= exynos4_init_irq,
	.map_io		= smdkc210_map_io,
	.init_machine	= smdkc210_machine_init,
	.timer		= &exynos4_timer,
	.init_early	= &exynos_init_reserve,
	.reserve	= &exynos_reserve,
MACHINE_END
