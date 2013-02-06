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
#include <linux/smsc911x.h>
#include <linux/io.h>
#include <linux/i2c.h>
#include <linux/i2c-gpio.h>
#include <linux/input.h>
#include <linux/switch.h>
#include <linux/spi/spi.h>
#include <linux/pwm_backlight.h>
#include <linux/spi/spi_gpio.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/fixed.h>
#include <linux/mfd/max8997.h>
#include <linux/mfd/max8997-private.h>
#include <linux/sensor/k3g.h>
#include <linux/sensor/k3dh.h>
#include <linux/sensor/ak8975.h>
#if defined(CONFIG_MACH_P8LTE)
#include <linux/platform_data/lte_modem_bootloader.h>
#endif
#include <linux/sensor/cm3663.h>
#include <linux/pn544.h>
#ifdef CONFIG_SND_SOC_U1_MC1N2
#include <linux/mfd/mc1n2_pdata.h>
#endif
#if defined(CONFIG_TOUCHSCREEN_MXT540E)
#include <linux/i2c/mxt540e.h>
#elif defined(CONFIG_TOUCHSCREEN_MXT768E)
#include <linux/i2c/mxt768e.h>
#elif defined(CONFIG_TOUCHSCREEN_MMS152)
#include <linux/mms152.h>
#else
#include <linux/i2c/mxt224_u1.h>
#endif
#ifdef CONFIG_TOUCHSCREEN_MXT1386
#include <linux/atmel_mxt1386.h>
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
#include <plat/regs-otg.h>

#ifdef CONFIG_S3C64XX_DEV_SPI0
#include <plat/s3c64xx-spi.h>
#include <mach/spi-clocks.h>
#endif

#include <mach/map.h>
#include <mach/exynos-clock.h>
#include <mach/media.h>
#include <plat/regs-fb.h>

#include <mach/dev-sysmmu.h>
#include <mach/dev.h>
#include <mach/regs-clock.h>
#include <mach/exynos-ion.h>
#if defined(CONFIG_MACH_P8LTE)
#include <mach/gpio.h>
#endif

#ifdef CONFIG_FB_S5P_MIPI_DSIM
#include <mach/mipi_ddi.h>
#include <mach/dsim.h>
#include <plat/fb-s5p.h>
#endif

#if defined(CONFIG_VIDEO_SAMSUNG_S5P_MFC) || defined(CONFIG_VIDEO_MFC5X)
#include <plat/s5p-mfc.h>
#endif

#ifdef CONFIG_VIDEO_M5MO
#include <media/m5mo_platform.h>
#endif
#ifdef CONFIG_VIDEO_S5K5CCGX_COMMON
#include <media/s5k5ccgx_platform.h>
#endif
#ifdef CONFIG_VIDEO_S5K5BAFX
#include <media/s5k5bafx_platform.h>
#endif
#ifdef CONFIG_VIDEO_SR200PC20
#include <media/sr200pc20_platform.h>
#endif

#if defined(CONFIG_EXYNOS4_SETUP_THERMAL)
#include <plat/s5p-tmu.h>
#include <mach/regs-tmu.h>
#endif

#ifdef CONFIG_SEC_DEV_JACK
#include <linux/sec_jack.h>
#endif

#ifdef CONFIG_SENSORS_AK8975
#include <linux/i2c/ak8975.h>
#endif

#ifdef CONFIG_MPU_SENSORS_MPU3050
#include <linux/mpu.h>
#endif

#ifdef CONFIG_OPTICAL_GP2A
#include <linux/gp2a.h>
#endif

#ifdef CONFIG_SENSORS_BH1721FVC
#include <linux/bh1721fvc.h>
#endif

#ifdef CONFIG_BT_BCM4330
#include <mach/board-bluetooth-bcm.h>
#endif

#ifdef CONFIG_BT_CSR8811
#include <mach/board-bluetooth-csr.h>
#endif

#ifdef CONFIG_FB_S5P_LD9040
#include <linux/ld9040.h>
#endif

#ifdef CONFIG_FB_S5P_MDNIE
#include <linux/mdnie.h>
#endif

#include <../../../drivers/video/samsung/s3cfb.h>
#include "px.h"

#include <mach/sec_debug.h>

#if defined(CONFIG_MHL_SII9234)
#include <linux/mhd9234.h>
#endif

#ifdef CONFIG_BATTERY_SEC_PX
#include <linux/power/sec_battery_px.h>
#endif

#ifdef CONFIG_SEC_THERMISTOR
#include <mach/sec_thermistor.h>
#include "px_thermistor.h"
#endif

#ifdef CONFIG_BATTERY_MAX17042_FUELGAUGE_PX
#include <linux/power/max17042_fuelgauge_px.h>
#endif

#ifdef CONFIG_USB_HOST_NOTIFY
#include <linux/host_notify.h>
#endif
#include <linux/pm_runtime.h>

#ifdef CONFIG_SMB136_CHARGER
#include <linux/power/smb136_charger.h>
#endif

#ifdef CONFIG_SMB347_CHARGER
#include <linux/power/smb347_charger.h>
#endif

#ifdef CONFIG_30PIN_CONN
#include <linux/30pin_con.h>
#include <mach/usb_switch.h>
#endif

#ifdef CONFIG_EPEN_WACOM_G5SP
#include <linux/wacom_i2c.h>
static struct wacom_g5_callbacks *wacom_callbacks;
#endif /* CONFIG_EPEN_WACOM_G5SP */

#ifdef CONFIG_USBHUB_USB3503_OTG_CONN
#include <linux/usb.h>
#include <linux/usb/hcd.h>
#include <linux/platform_data/usb3503_otg_conn.h>
#endif


static struct charging_status_callbacks {
	void (*tsp_set_charging_cable) (int type);
} charging_cbs;

bool is_cable_attached;
bool is_usb_lpm_enter;

unsigned int lcdtype;
static int __init lcdtype_setup(char *str)
{
	get_option(&str, &lcdtype);
	return 1;
}
__setup("lcdtype=", lcdtype_setup);

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
#if defined(CONFIG_BT_BCM4330)
		.wake_peer = bcm_bt_lpm_exit_lpm_locked,
#elif defined(CONFIG_BT_CSR8811)
		.wake_peer = csr_bt_lpm_exit_lpm_locked,
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

#ifdef CONFIG_MACH_PX

#ifdef CONFIG_SEC_MODEM_P8LTE
#define LTE_MODEM_SPI_BUS_NUM   0
#define LTE_MODEM_SPI_CS        0
#define LTE_MODEM_SPI_MAX_CLK   (500*1000)
struct lte_modem_bootloader_platform_data lte_modem_bootloader_pdata = {
		.name = "lte_modem_int",
		.gpio_lte2ap_status = GPIO_LTE2AP_STATUS,
};
/*struct lte_modem_bootloader_platform_data lte_modem_bootloader_pdata = {
	.name = "lte_modem_bootloader",
	.gpio_lte2ap_status = GPIO_LTE2AP_STATUS,
	.gpio_lte_active = GPIO_LTE_ACTIVE,
};*/

static struct s3c64xx_spi_csinfo spi0_csi_lte[] = {
	[0] = {
		.line = EXYNOS4_GPB(1), /*S5PV310_GPB(1),*/
		.set_level = gpio_set_value,
	},
};

static struct spi_board_info spi0_board_info_lte[] __initdata = {
	{
		.modalias = "lte_modem_spi",
		.platform_data = &lte_modem_bootloader_pdata,
		.max_speed_hz = LTE_MODEM_SPI_MAX_CLK,
		.bus_num = LTE_MODEM_SPI_BUS_NUM,
		.chip_select = LTE_MODEM_SPI_CS,
		.mode = SPI_MODE_0,
		.controller_data = &spi0_csi_lte[0],
	}
};
#endif
static struct platform_device p4w_wlan_ar6000_pm_device = {
	.name				= "wlan_ar6000_pm_dev",
	.id					= 1,
	.num_resources		= 0,
	.resource			= NULL,
};

static void
(*wlan_status_notify_cb)(struct platform_device *dev_id, int card_present);
struct platform_device *wlan_devid;

static int register_wlan_status_notify
(void (*callback)(struct platform_device *dev_id, int card_present))
{
	wlan_status_notify_cb = callback;
	return 0;
}

static int register_wlan_pdev(struct platform_device *pdev)
{
	wlan_devid = pdev;
	printk(KERN_ERR "ATHR register_wlan_pdev pdev->id = %d\n", pdev->id);
	return 0;
}

#define WLAN_HOST_WAKE
#ifdef WLAN_HOST_WAKE
struct wlansleep_info {
	unsigned host_wake;
	unsigned host_wake_irq;
	struct wake_lock wake_lock;
};

static struct wlansleep_info *wsi;
static struct tasklet_struct hostwake_task;

static void wlan_hostwake_task(unsigned long data)
{
#if defined(CONFIG_MACH_P8LTE)
	printk(KERN_INFO "WLAN: wake lock timeout 1 sec...\n");
	wake_lock_timeout(&wsi->wake_lock, HZ);
#else
	printk(KERN_INFO "WLAN: wake lock timeout 0.5 sec...\n");
	wake_lock_timeout(&wsi->wake_lock, HZ / 2);
#endif
}

static irqreturn_t wlan_hostwake_isr(int irq, void *dev_id)
{
	tasklet_schedule(&hostwake_task);
	return IRQ_HANDLED;
}

static int wlan_host_wake_init(void)
{
	int ret;

	wsi = kzalloc(sizeof(struct wlansleep_info), GFP_KERNEL);
	if (!wsi)
		return -ENOMEM;

	wake_lock_init(&wsi->wake_lock, WAKE_LOCK_SUSPEND, "bluesleep");
	tasklet_init(&hostwake_task, wlan_hostwake_task, 0);

	wsi->host_wake = GPIO_WLAN_HOST_WAKE;
	wsi->host_wake_irq = gpio_to_irq(GPIO_WLAN_HOST_WAKE);

	ret = request_irq(wsi->host_wake_irq, wlan_hostwake_isr,
				IRQF_DISABLED | IRQF_TRIGGER_RISING,
				"wlan hostwake", NULL);
	if (ret	< 0) {
		printk(KERN_ERR "WLAN: Couldn't acquire WLAN_HOST_WAKE IRQ");
		return -1;
	}

	ret = enable_irq_wake(wsi->host_wake_irq);
	if (ret < 0) {
		printk(KERN_ERR "WLAN: Couldn't enable WLAN_HOST_WAKE as wakeup interrupt");
		free_irq(wsi->host_wake_irq, NULL);
		return -1;
	}

	return 0;
}

static void wlan_host_wake_exit(void)
{
	if (disable_irq_wake(wsi->host_wake_irq))
		printk(KERN_ERR "WLAN: Couldn't disable hostwake IRQ wakeup mode\n");
	free_irq(wsi->host_wake_irq, NULL);
	tasklet_kill(&hostwake_task);
	wake_lock_destroy(&wsi->wake_lock);
	kfree(wsi);
}
#endif /* WLAN_HOST_WAKE */

static void config_wlan_gpio(void)
{
	int ret = 0;
	unsigned int gpio;

	printk(KERN_ERR "ATHR - %s\n", __func__);
	ret = gpio_request(GPIO_WLAN_HOST_WAKE, "wifi_irq");
	if (ret < 0) {
		printk(KERN_ERR "cannot reserve GPIO_WLAN_HOST_WAKE: %s - %d\n"\
				, __func__, GPIO_WLAN_HOST_WAKE);
		gpio_free(GPIO_WLAN_HOST_WAKE);
		return;
	}

	ret = gpio_request(GPIO_WLAN_EN, "wifi_pwr_33");

	if (ret < 0) {
		printk(KERN_ERR "cannot reserve GPIO_WLAN_EN: %s - %d\n"\
				, __func__, GPIO_WLAN_EN);
		gpio_free(GPIO_WLAN_EN);
		return;
	}

	if (system_rev >= 4) {
		ret = gpio_request(GPIO_WLAN_EN2, "wifi_pwr_18");

		if (ret < 0) {
			printk(KERN_ERR "cannot reserve GPIO_WLAN_EN2: "\
					"%s - %d\n", __func__, GPIO_WLAN_EN2);
			gpio_free(GPIO_WLAN_EN2);
			return;
		}
	}

	ret = gpio_request(GPIO_WLAN_nRST, "wifi_rst");

	if (ret < 0) {
		printk(KERN_ERR "cannot reserve GPIO_WLAN_nRST: %s - %d\n"\
				, __func__, GPIO_WLAN_nRST);
		gpio_free(GPIO_WLAN_nRST);
		return;
	}

	gpio_direction_output(GPIO_WLAN_nRST, 0);
	gpio_direction_output(GPIO_WLAN_EN, 1);

	if (system_rev >= 4)
		gpio_direction_output(GPIO_WLAN_EN2, 0);
}

void
wlan_setup_power(int on, int detect)
{
	printk(KERN_ERR "ATHR - %s %s --enter\n", __func__, on ? "on" : "off");

	if (on) {
		/* WAR for nRST is high */


		if (system_rev >= 4) {
			gpio_direction_output(GPIO_WLAN_EN2, 1);
			udelay(10);
		}
		gpio_direction_output(GPIO_WLAN_nRST, 0);
		mdelay(30);
		gpio_direction_output(GPIO_WLAN_nRST, 1);

#ifdef WLAN_HOST_WAKE
		wlan_host_wake_init();
#endif /* WLAN_HOST_WAKE */

	} else {
#ifdef WLAN_HOST_WAKE
		wlan_host_wake_exit();
#endif /* WLAN_HOST_WAKE */

		gpio_direction_output(GPIO_WLAN_nRST, 0);
		if (system_rev >= 4)
			gpio_direction_output(GPIO_WLAN_EN2, 0);
	}

	mdelay(100);

	printk(KERN_ERR "ATHR - rev : %02d\n", system_rev);

	if (system_rev >= 4) {
		printk(KERN_ERR "ATHR - GPIO_WLAN_EN1(%d: %d), "\
			"GPIO_WLAN_EN2(%d: %d), GPIO_WALN_nRST(%d: %d)\n"\
			, GPIO_WLAN_EN, gpio_get_value(GPIO_WLAN_EN)
			, GPIO_WLAN_EN2, gpio_get_value(GPIO_WLAN_EN2)
			, GPIO_WLAN_nRST, gpio_get_value(GPIO_WLAN_nRST));
	} else {
		printk(KERN_ERR "ATHR - GPIO_WLAN_EN(%d: %d), "\
			" GPIO_WALN_nRST(%d: %d)\n"\
			, GPIO_WLAN_EN, gpio_get_value(GPIO_WLAN_EN)
			, GPIO_WLAN_nRST, gpio_get_value(GPIO_WLAN_nRST));
	}

	if (detect) {
		if (wlan_status_notify_cb)
			wlan_status_notify_cb(wlan_devid, on);
		else
			printk(KERN_ERR "ATHR - WLAN: No notify available\n");
	}
}
EXPORT_SYMBOL(wlan_setup_power);

#endif

#ifdef CONFIG_VIDEO_FIMC
/*
 * External camera reset
 * Because the most of cameras take i2c bus signal, so that
 * you have to reset at the boot time for other i2c slave devices.
 * This function also called at fimc_init_camera()
 * Do optimization for cameras on your platform.
 */

#define CAM_CHECK_ERR_RET(x, msg)	\
	if (unlikely((x) < 0)) { \
		printk(KERN_ERR "\nfail to %s: err = %d\n", msg, x); \
		return x; \
	}
#define CAM_CHECK_ERR(x, msg)	\
	if (unlikely((x) < 0)) { \
		printk(KERN_ERR "\nfail to %s: err = %d\n", msg, x); \
	}

#define CAM_CHECK_ERR_GOTO(x, out, fmt, ...) \
	if (unlikely((x) < 0)) { \
		printk(KERN_ERR fmt, ##__VA_ARGS__); \
		goto out; \
	}

#ifdef CONFIG_VIDEO_FIMC_MIPI
int s3c_csis_power(int enable)
{
	struct regulator *regulator;
	int ret = -ENODEV;

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
			if (unlikely(ret < 0)) {
				pr_err("%s: error, vmipi_1.1v\n", __func__);
				return ret;
			}
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
			if (unlikely(ret < 0)) {
				pr_err("%s: error, vmipi_1.8v\n", __func__);
				return ret;
			}
		}
		regulator_put(regulator);
	}

	return 0;

error_out:
	printk(KERN_ERR "%s: ERROR: failed to check mipi-power\n", __func__);
	return ret;
}

#endif


#ifdef CONFIG_VIDEO_S5K5CCGX_COMMON
static int s5k5ccgx_get_i2c_busnum(void)
{
	return 0;
}

#ifdef CONFIG_VIDEO_S5K5CCGX_P8
static int s5k5ccgx_power_on(void)
{
	struct regulator *regulator;
	int ret = 0;

	printk(KERN_DEBUG "%s in P8\n", __func__);

#ifndef USE_CAM_GPIO_CFG
#if !defined(CONFIG_MACH_P8LTE)
	ret = gpio_request(GPIO_CAM_AVDD_EN, "GPJ1");
	if (ret) {
		printk(KERN_ERR "Error: fail to request gpio(CAM_AVDD)\n");
		return ret;
	}
#endif
	ret = gpio_request(GPIO_2M_nSTBY, "GPL2");
	if (ret) {
		printk(KERN_ERR "Error: fail to request gpio(2M_nSTBY)\n");
		return ret;
	}
	ret = gpio_request(GPIO_2M_nRST, "GPL2");
	if (ret) {
		printk(KERN_ERR "Error: fail to request gpio(2M_nRST)\n");
		return ret;
	}
	ret = gpio_request(GPIO_3M_nSTBY, "GPL2");
	if (ret) {
		printk(KERN_ERR "Error: fail to request gpio(3M_nSTBY)\n");
		return ret;
	}
	ret = gpio_request(GPIO_3M_nRST, "GPL2");
	if (ret) {
		printk(KERN_ERR "Error: fail to request gpio(3M_nRST)\n");
		return ret;
	}
#endif

	/* 2M_nSTBY low */
	ret = gpio_direction_output(GPIO_2M_nSTBY, 0);
	CAM_CHECK_ERR_RET(ret, "2M_nSTBY");

	/* 2M_nRST low */
	ret = gpio_direction_output(GPIO_2M_nRST, 0);
	CAM_CHECK_ERR_RET(ret, "2M_nRST");

	/* CAM_A2.8V */
#if defined(CONFIG_MACH_P8LTE)
	regulator = regulator_get(NULL, "cam_analog_2.8v");
	if (IS_ERR(regulator))
		return -ENODEV;
	ret = regulator_enable(regulator);
	regulator_put(regulator);
	CAM_CHECK_ERR_RET(ret, "CAM_A2.8V");
#else
	ret = gpio_direction_output(GPIO_CAM_AVDD_EN, 1);
	CAM_CHECK_ERR_RET(ret, "CAM_AVDD");
	udelay(1);
#endif

	/* 3MP_CORE_1.2V */
	regulator = regulator_get(NULL, "3mp_core");
	if (IS_ERR(regulator))
		return -ENODEV;
	ret = regulator_enable(regulator);
	regulator_put(regulator);
	CAM_CHECK_ERR_RET(ret, "3mp_core");
	udelay(1);

	/* VT_CORE_1.8V */
	regulator = regulator_get(NULL, "vt_core_1.8v");
	if (IS_ERR(regulator))
		return -ENODEV;
	ret = regulator_enable(regulator);
	regulator_put(regulator);
	CAM_CHECK_ERR_RET(ret, "vt_core_1.8v");
	udelay(1);

	/* CAM_IO_1.8V */
	regulator = regulator_get(NULL, "cam_io_1.8v");
	if (IS_ERR(regulator))
		return -ENODEV;
	ret = regulator_enable(regulator);
	regulator_put(regulator);
	CAM_CHECK_ERR_RET(ret, "cam_io_1.8v");
	udelay(70);

	/* CAM_MCLK */
	ret = s3c_gpio_cfgpin(GPIO_CAM_MCLK, S3C_GPIO_SFN(2));
	CAM_CHECK_ERR_RET(ret, "cfg mclk");
	s3c_gpio_setpull(GPIO_CAM_MCLK, S3C_GPIO_PULL_NONE);
	udelay(10);

	/* 3M_nSTBY */
	ret = gpio_direction_output(GPIO_3M_nSTBY, 1);
	CAM_CHECK_ERR_RET(ret, "3M_nSTBY");
	udelay(16);

	/* 3M_nRST */
	ret = gpio_direction_output(GPIO_3M_nRST, 1);
	CAM_CHECK_ERR_RET(ret, "3M_nRST");

	/* 3MP_AF_2.8V */
	regulator = regulator_get(NULL, "3m_af_2.8v");
	if (IS_ERR(regulator))
		return -ENODEV;
	ret = regulator_enable(regulator);
	regulator_put(regulator);
	CAM_CHECK_ERR_RET(ret, "3m_af_2.8v");
	msleep(10);

#ifndef USE_CAM_GPIO_CFG
#if !defined(CONFIG_MACH_P8LTE)
	gpio_free(GPIO_CAM_AVDD_EN);
#endif
	gpio_free(GPIO_2M_nSTBY);
	gpio_free(GPIO_2M_nRST);
	gpio_free(GPIO_3M_nSTBY);
	gpio_free(GPIO_3M_nRST);
#endif
	return ret;
}

static int s5k5ccgx_power_down(void)
{
	struct regulator *regulator;
	int ret = 0;

	printk(KERN_DEBUG "%s in P8\n", __func__);

#ifndef USE_CAM_GPIO_CFG
#if !defined(CONFIG_MACH_P8LTE)
	ret = gpio_request(GPIO_CAM_AVDD_EN, "GPJ1");
	if (ret) {
		printk(KERN_ERR "Error: fail to request gpio(CAM_AVDD)\n");
		return ret;
	}
#endif
	ret = gpio_request(GPIO_3M_nRST, "GPL2");
	if (ret) {
		printk(KERN_ERR "Error: fail to request gpio(3M_nRST)\n");
		return ret;
	}
	ret = gpio_request(GPIO_3M_nSTBY, "GPL2");
	if (ret) {
		printk(KERN_ERR "Error: fail to request gpio(3M_nSTBY)\n");
		return ret;
	}
#endif
	/* 3MP_AF_2.8V */
	regulator = regulator_get(NULL, "3m_af_2.8v");
	if (IS_ERR(regulator))
		return -ENODEV;
	if (regulator_is_enabled(regulator))
		ret = regulator_force_disable(regulator);
	regulator_put(regulator);
	CAM_CHECK_ERR(ret, "3m_af_2.8v");

	/* 3M_nRST Low*/
	ret = gpio_direction_output(GPIO_3M_nRST, 0);
	CAM_CHECK_ERR(ret, "3M_nSTBY");
	udelay(50);

	/* CAM_MCLK */
	ret = s3c_gpio_cfgpin(GPIO_CAM_MCLK, S3C_GPIO_INPUT);
	s3c_gpio_setpull(GPIO_CAM_MCLK, S3C_GPIO_PULL_DOWN);
	CAM_CHECK_ERR(ret, "cfg mclk");
	udelay(5);

	/* 3M_nSTBY */
	ret = gpio_direction_output(GPIO_3M_nSTBY, 0);
	CAM_CHECK_ERR(ret, "3M_nSTBY");
	udelay(1);

	/* CAM_IO_1.8V */
	regulator = regulator_get(NULL, "cam_io_1.8v");
	if (IS_ERR(regulator))
		return -ENODEV;
	if (regulator_is_enabled(regulator))
		ret = regulator_force_disable(regulator);
	regulator_put(regulator);
	CAM_CHECK_ERR(ret, "cam_io_1.8v");
	udelay(1);

	/* VT_CORE_1.8V */
	regulator = regulator_get(NULL, "vt_core_1.8v");
	if (IS_ERR(regulator))
		return -ENODEV;
	if (regulator_is_enabled(regulator))
		ret = regulator_force_disable(regulator);
	regulator_put(regulator);
	CAM_CHECK_ERR(ret, "vt_core_1.8v");
	udelay(1);

	/* 3MP_CORE_1.2V */
	regulator = regulator_get(NULL, "3mp_core");
	if (IS_ERR(regulator))
		return -ENODEV;
	if (regulator_is_enabled(regulator))
		ret = regulator_force_disable(regulator);
	regulator_put(regulator);
	CAM_CHECK_ERR(ret, "3mp_core");
	udelay(1);

	/* CAM_A2.8V */
#if defined(CONFIG_MACH_P8LTE)
	regulator = regulator_get(NULL, "cam_analog_2.8v");
	if (IS_ERR(regulator))
		return -ENODEV;
	if (regulator_is_enabled(regulator))
		ret = regulator_force_disable(regulator);
	regulator_put(regulator);
	CAM_CHECK_ERR(ret, "CAM_A2.8V");
#else
	ret = gpio_direction_output(GPIO_CAM_AVDD_EN, 0);
	CAM_CHECK_ERR_RET(ret, "CAM_AVDD");
#endif

#ifndef USE_CAM_GPIO_CFG
#if !defined(CONFIG_MACH_P8LTE)
	gpio_free(GPIO_CAM_AVDD_EN);
#endif
	gpio_free(GPIO_3M_nSTBY);
	gpio_free(GPIO_3M_nRST);
#endif
	return ret;
}

#else /* CONFIG_VIDEO_S5K5CCGX_P8 */

/* Power up/down func for P4C, P2. */
static int s5k5ccgx_power_on(void)
{
	struct regulator *regulator;
	int ret = 0;

	printk(KERN_DEBUG "%s in P4C,P2\n", __func__);

#ifndef USE_CAM_GPIO_CFG
	ret = gpio_request(GPIO_2M_nSTBY, "GPL2");
	if (ret) {
		printk(KERN_ERR "Error: fail to request gpio(2M_nSTBY)\n");
		return ret;
	}
	ret = gpio_request(GPIO_2M_nRST, "GPL2");
	if (ret) {
		printk(KERN_ERR "Error: fail to request gpio(2M_nRST)\n");
		return ret;
	}
	ret = gpio_request(GPIO_3M_nSTBY, "GPL2");
	if (ret) {
		printk(KERN_ERR "Error: fail to request gpio(3M_nSTBY)\n");
		return ret;
	}
	ret = gpio_request(GPIO_3M_nRST, "GPL2");
	if (ret) {
		printk(KERN_ERR "Error: fail to request gpio(3M_nRST)\n");
		return ret;
	}
#endif

	/* 2M_nSTBY low */
	ret = gpio_direction_output(GPIO_2M_nSTBY, 0);
	CAM_CHECK_ERR_RET(ret, "2M_nSTBY");

	/* 2M_nRST low */
	ret = gpio_direction_output(GPIO_2M_nRST, 0);
	CAM_CHECK_ERR_RET(ret, "2M_nRST");

	/* 3MP_CORE_1.2V */
	regulator = regulator_get(NULL, "3mp_core");
	if (IS_ERR(regulator))
		return -ENODEV;
	ret = regulator_enable(regulator);
	regulator_put(regulator);
	CAM_CHECK_ERR_RET(ret, "3mp_core");

	/* CAM_IO_1.8V */
	regulator = regulator_get(NULL, "cam_io_1.8v");
	if (IS_ERR(regulator))
		return -ENODEV;
	ret = regulator_enable(regulator);
	regulator_put(regulator);
	CAM_CHECK_ERR_RET(ret, "cam_io_1.8v");

	/* CAM_A2.8V, LDO13 */
	regulator = regulator_get(NULL, "cam_analog_2.8v");
	if (IS_ERR(regulator))
		return -ENODEV;
	ret = regulator_enable(regulator);
	regulator_put(regulator);
	CAM_CHECK_ERR_RET(ret, "cam_analog_2.8v");

	/* VT_CORE_1.8V */
	regulator = regulator_get(NULL, "vt_core_1.8v");
	if (IS_ERR(regulator))
		return -ENODEV;
	ret = regulator_enable(regulator);
	regulator_put(regulator);
	CAM_CHECK_ERR_RET(ret, "vt_core_1.8v");
	udelay(20);

	/* 2M_nSTBY High */
	ret = gpio_direction_output(GPIO_2M_nSTBY, 1);
	CAM_CHECK_ERR_RET(ret, "2M_nSTBY");
	udelay(3);

	/* CAM_MCLK */
	ret = s3c_gpio_cfgpin(GPIO_CAM_MCLK, S3C_GPIO_SFN(2));
	CAM_CHECK_ERR_RET(ret, "cfg mclk");
	s3c_gpio_setpull(GPIO_CAM_MCLK, S3C_GPIO_PULL_NONE);
	msleep(5); /* >=5ms */

	/* 2M_nSTBY Low */
	ret = gpio_direction_output(GPIO_2M_nSTBY, 0);
	CAM_CHECK_ERR_RET(ret, "2M_nSTBY");
	msleep(10); /* >=10ms */

	/* 2M_nRST High */
	ret = gpio_direction_output(GPIO_2M_nRST, 1);
	CAM_CHECK_ERR_RET(ret, "2M_nRST");
	msleep(5);

	/* 2M_nSTBY High */
	ret = gpio_direction_output(GPIO_2M_nSTBY, 1);
	CAM_CHECK_ERR_RET(ret, "2M_nSTBY");
	udelay(2);

	/* 3M_nSTBY */
	ret = gpio_direction_output(GPIO_3M_nSTBY, 1);
	CAM_CHECK_ERR_RET(ret, "3M_nSTBY");
	udelay(16);

	/* 3M_nRST */
	ret = gpio_direction_output(GPIO_3M_nRST, 1);
	CAM_CHECK_ERR_RET(ret, "3M_nRST");
	/* udelay(10); */

	/* 3MP_AF_2.8V */
	regulator = regulator_get(NULL, "3m_af_2.8v");
	if (IS_ERR(regulator))
		return -ENODEV;
	ret = regulator_enable(regulator);
	regulator_put(regulator);
	CAM_CHECK_ERR_RET(ret, "3m_af_2.8v");
	msleep(10);

#ifndef USE_CAM_GPIO_CFG
	gpio_free(GPIO_2M_nSTBY);
	gpio_free(GPIO_2M_nRST);
	gpio_free(GPIO_3M_nSTBY);
	gpio_free(GPIO_3M_nRST);
#endif

	return ret;
}

static int s5k5ccgx_power_down(void)
{
	struct regulator *regulator;
	int ret = 0;

	printk(KERN_DEBUG "%s in P4C,P2\n", __func__);

#ifndef USE_CAM_GPIO_CFG
	ret = gpio_request(GPIO_2M_nSTBY, "GPL2");
	if (ret) {
		printk(KERN_ERR "Error: fail to request gpio(2M_nSTBY)\n");
		return ret;
	}
	ret = gpio_request(GPIO_2M_nRST, "GPL2");
	if (ret) {
		printk(KERN_ERR "Error: fail to request gpio(2M_nRST)\n");
		return ret;
	}
	ret = gpio_request(GPIO_3M_nRST, "GPL2");
	if (ret) {
		printk(KERN_ERR "Error: fail to request gpio(3M_nRST)\n");
		return ret;
	}
	ret = gpio_request(GPIO_3M_nSTBY, "GPL2");
	if (ret) {
		printk(KERN_ERR "Error: fail to request gpio(3M_nSTBY)\n");
		return ret;
	}
#endif
	/* 3MP_AF_2.8V */
	regulator = regulator_get(NULL, "3m_af_2.8v");
	if (IS_ERR(regulator))
		return -ENODEV;
	if (regulator_is_enabled(regulator))
		ret = regulator_force_disable(regulator);
	regulator_put(regulator);
	CAM_CHECK_ERR(ret, "3m_af_2.8v");

	/* 3M_nRST Low*/
	ret = gpio_direction_output(GPIO_3M_nRST, 0);
	CAM_CHECK_ERR(ret, "3M_nSTBY");
	udelay(50);

	/* CAM_MCLK */
	ret = s3c_gpio_cfgpin(GPIO_CAM_MCLK, S3C_GPIO_INPUT);
	s3c_gpio_setpull(GPIO_CAM_MCLK, S3C_GPIO_PULL_DOWN);
	CAM_CHECK_ERR(ret, "cfg mclk");
	udelay(5);

	/* 3M_nSTBY */
	ret = gpio_direction_output(GPIO_3M_nSTBY, 0);
	CAM_CHECK_ERR(ret, "3M_nSTBY");
	udelay(1);

	/* 2M_nRST Low */
	ret = gpio_direction_output(GPIO_2M_nRST, 0);
	CAM_CHECK_ERR_RET(ret, "2M_nRST");

	/* 2M_nSTBY Low */
	ret = gpio_direction_output(GPIO_2M_nSTBY, 0);
	CAM_CHECK_ERR_RET(ret, "2M_nSTBY");

	/* VT_CORE_1.8V */
	regulator = regulator_get(NULL, "vt_core_1.8v");
	if (IS_ERR(regulator))
		return -ENODEV;
	if (regulator_is_enabled(regulator))
		ret = regulator_force_disable(regulator);
	regulator_put(regulator);
	CAM_CHECK_ERR(ret, "vt_core_1.8v");

	/* CAM_A2.8V */
	regulator = regulator_get(NULL, "cam_analog_2.8v");
	if (IS_ERR(regulator))
		return -ENODEV;
	if (regulator_is_enabled(regulator))
		ret = regulator_force_disable(regulator);
	regulator_put(regulator);
	CAM_CHECK_ERR(ret, "cam_analog_2.8v");
	/* udelay(50); */

	/* CAM_IO_1.8V */
	regulator = regulator_get(NULL, "cam_io_1.8v");
	if (IS_ERR(regulator))
		return -ENODEV;
	if (regulator_is_enabled(regulator))
		ret = regulator_force_disable(regulator);
	regulator_put(regulator);
	CAM_CHECK_ERR(ret, "cam_io_1.8v");
	/*udelay(50); */

	/* 3MP_CORE_1.2V */
	regulator = regulator_get(NULL, "3mp_core");
	if (IS_ERR(regulator))
		return -ENODEV;
	if (regulator_is_enabled(regulator))
		ret = regulator_force_disable(regulator);
	regulator_put(regulator);
	CAM_CHECK_ERR(ret, "3mp_core");

#ifndef USE_CAM_GPIO_CFG
	gpio_free(GPIO_2M_nSTBY);
	gpio_free(GPIO_2M_nRST);
	gpio_free(GPIO_3M_nSTBY);
	gpio_free(GPIO_3M_nRST);
#endif
	return ret;
}
#endif /* CONFIG_VIDEO_S5K5CCGX_P8 */

static int s5k5ccgx_power(int enable)
{
	int ret = 0;

	printk(KERN_DEBUG "%s %s\n", __func__, enable ? "on" : "down");
	if (enable) {
#ifdef USE_CAM_GPIO_CFG
		if (cfg_gpio_err) {
			printk(KERN_ERR "%s: ERROR: gpio configuration",
				__func__);
			return cfg_gpio_err;
		}
#endif
		ret = s5k5ccgx_power_on();
	} else
		ret = s5k5ccgx_power_down();

	s3c_csis_power(enable);

	return ret;
}

static void s5k5ccgx_flashtimer_handler(unsigned long data)
{
	int ret = -ENODEV;
	atomic_t *flash_status = (atomic_t *)data;

	pr_info("********** flashtimer_handler **********\n");

	ret = gpio_direction_output(GPIO_CAM_FLASH_EN, 0);
	atomic_set(flash_status, S5K5CCGX_FLASH_OFF);
	if (unlikely(ret))
		pr_err("flash_timer: ERROR, failed to oneshot flash off\n");
}

static atomic_t flash_status = ATOMIC_INIT(S5K5CCGX_FLASH_OFF);
static int s5k5ccgx_flash_en(u32 mode, u32 onoff)
{
	static int flash_mode = S5K5CCGX_FLASH_MODE_NORMAL;
	static DEFINE_MUTEX(flash_lock);
	static DEFINE_TIMER(flash_timer, s5k5ccgx_flashtimer_handler,
			0, (unsigned long)&flash_status);
	int ret = 0;

	printk(KERN_DEBUG "flash_en: mode=%d, on=%d\n", mode, onoff);

	if (unlikely((u32)mode >= S5K5CCGX_FLASH_MODE_MAX)) {
		pr_err("flash_en: ERROR, invalid flash mode(%d)\n", mode);
		return -EINVAL;
	}

	/* We could not use spin lock because of gpio kernel API.*/
	mutex_lock(&flash_lock);
	if (atomic_read(&flash_status) == onoff) {
		mutex_unlock(&flash_lock);
		pr_warn("flash_en: WARNING, already flash %s\n",
			onoff ? "On" : "Off");
		return 0;
	}

	switch (onoff) {
	case S5K5CCGX_FLASH_ON:
		if (mode == S5K5CCGX_FLASH_MODE_MOVIE)
			ret = gpio_direction_output(GPIO_CAM_MOVIE_EN, 1);
		else {
			ret = gpio_direction_output(GPIO_CAM_FLASH_EN, 1);
			flash_timer.expires = get_jiffies_64() + HZ / 2;
			add_timer(&flash_timer);
		}
		CAM_CHECK_ERR_GOTO(ret, out,
			"flash_en: ERROR, fail to turn flash on (mode:%d)\n",
			mode);
		flash_mode = mode;
		break;

	case S5K5CCGX_FLASH_OFF:
		if (unlikely(flash_mode != mode)) {
			pr_err("flash_en: ERROR, unmatched flash mode(%d, %d)\n",
				flash_mode, mode);
			WARN_ON(1);
			goto out;
		}

		if (mode == S5K5CCGX_FLASH_MODE_MOVIE)
			ret = gpio_direction_output(GPIO_CAM_MOVIE_EN, 0);
		else {
			if (del_timer_sync(&flash_timer)) {
				pr_info("flash_en: terminate flash timer...\n");
				ret = gpio_direction_output(GPIO_CAM_FLASH_EN,
							0);
			}
		}
		CAM_CHECK_ERR_GOTO(ret, out,
			"flash_en: ERROR, flash off (mode:%d)\n", mode);
		break;

	default:
		pr_err("flash_en: ERROR, invalid flash cmd(%d)\n", onoff);
		goto out;
		break;
	}

	atomic_set(&flash_status, onoff);

out:
	mutex_unlock(&flash_lock);
	return 0;
}

static int s5k5ccgx_is_flash_on(void)
{
	return atomic_read(&flash_status);
}

static int px_cam_cfg_init(void)
{
	int ret = -ENODEV;

	/* pr_info("%s\n", __func__); */

	ret = gpio_request(GPIO_CAM_MOVIE_EN, "GPL0");
	if (unlikely(ret)) {
		pr_err("cam_cfg_init: fail to get gpio(MOVIE_EN), "
			"err=%d\n", ret);
		goto out;
	}

	ret = gpio_request(GPIO_CAM_FLASH_EN, "GPL0");
	if (unlikely(ret)) {
		pr_err("cam_cfg_init: fail to get gpio(FLASH_EN), "
			"err=%d\n", ret);
		goto out_free;
	}

	return 0;

out_free:
	gpio_free(GPIO_CAM_MOVIE_EN);
out:
	return ret;
}

static struct s5k5ccgx_platform_data s5k5ccgx_plat = {
	.default_width = 1024,
	.default_height = 768,
	.pixelformat = V4L2_PIX_FMT_UYVY,
	.freq = 24000000,
	.is_mipi = 1,
	.streamoff_delay = S5K5CCGX_STREAMOFF_DELAY,
	.flash_en = s5k5ccgx_flash_en,
	.is_flash_on = s5k5ccgx_is_flash_on,
	.dbg_level = CAMDBG_LEVEL_DEFAULT,
};
#define REAR_CAM_PLAT		(s5k5ccgx_plat)

static struct i2c_board_info  s5k5ccgx_i2c_info = {
	I2C_BOARD_INFO("S5K5CCGX", 0x78>>1),
	.platform_data = &s5k5ccgx_plat,
};

static struct s3c_platform_camera s5k5ccgx = {
	.id		= CAMERA_CSI_C,
	.clk_name	= "sclk_cam0",
	.get_i2c_busnum	= s5k5ccgx_get_i2c_busnum,
	.cam_power	= s5k5ccgx_power, /*smdkv310_mipi_cam0_reset,*/
	.type		= CAM_TYPE_MIPI,
	.fmt		= ITU_601_YCBCR422_8BIT, /*MIPI_CSI_YCBCR422_8BIT*/
	.order422	= CAM_ORDER422_8BIT_CBYCRY,
	.info		= &s5k5ccgx_i2c_info,
	.pixelformat	= V4L2_PIX_FMT_UYVY,
	.srclk_name	= "xusbxti", /* "mout_mpll" */
	.clk_rate	= 24000000, /* 48000000 */
	.line_length	= 640,
	.width		= 640,
	.height		= 480,
	.window		= {
		.left	= 0,
		.top	= 0,
		.width	= 640,
		.height	= 480,
	},

	.mipi_lanes	= 1,
	.mipi_settle	= 6,
	.mipi_align	= 32,

	/* Polarity */
	.inv_pclk	= 0,
	.inv_vsync	= 1,
	.inv_href	= 0,
	.inv_hsync	= 0,
	.reset_camera	= 0,
	.initialized	= 0,
};
#endif /* #ifdef CONFIG_VIDEO_S5K5CCGX_COMMON */


#ifdef CONFIG_VIDEO_S5K5BAFX
static int s5k5bafx_get_i2c_busnum(void)
{
	return 0;
}

static int s5k5bafx_power_on(void)
{
	struct regulator *regulator;
	int ret = 0;

	printk(KERN_DEBUG "%s: in\n", __func__);
#if !defined(CONFIG_MACH_P8LTE)
	ret = gpio_request(GPIO_CAM_AVDD_EN, "GPJ1");
	if (ret) {
		printk(KERN_ERR "Error: fail to request gpio(CAM_AVDD)\n");
		return ret;
	}
#endif
	ret = gpio_request(GPIO_2M_nSTBY, "GPL2");
	if (ret) {
		printk(KERN_ERR "Error: fail to request gpio(2M_nSTBY)\n");
		return ret;
	}
	ret = gpio_request(GPIO_2M_nRST, "GPL2");
	if (ret) {
		printk(KERN_ERR "Error: fail to request gpio(2M_nRST)\n");
		return ret;
	}
	ret = gpio_request(GPIO_3M_nSTBY, "GPL2");
	if (ret) {
		printk(KERN_ERR "Error: fail to request gpio(3M_nSTBY)\n");
		return ret;
	}
	ret = gpio_request(GPIO_3M_nRST, "GPL2");
	if (ret) {
		printk(KERN_ERR "Error: fail to request gpio(3M_nRST)\n");
		return ret;
	}

	/* 3M_nSTBY low */
	ret = gpio_direction_output(GPIO_3M_nSTBY, 0);
	CAM_CHECK_ERR_RET(ret, "3M_nSTBY");

	/* 3M_nRST low */
	ret = gpio_direction_output(GPIO_3M_nRST, 0);
	CAM_CHECK_ERR_RET(ret, "3M_nRST");

	/* CAM_A2.8V */
#if defined(CONFIG_MACH_P8LTE)
	regulator = regulator_get(NULL, "cam_analog_2.8v");
	if (IS_ERR(regulator))
		return -ENODEV;
	ret = regulator_enable(regulator);
	regulator_put(regulator);
	CAM_CHECK_ERR_RET(ret, "CAM_A2.8V");
#else

	ret = gpio_direction_output(GPIO_CAM_AVDD_EN, 1);
	CAM_CHECK_ERR_RET(ret, "CAM_AVDD");
	/* udelay(1); */
#endif

	/* 3MP_CORE_1.2V */
	regulator = regulator_get(NULL, "3mp_core");
	if (IS_ERR(regulator))
		return -ENODEV;
	ret = regulator_enable(regulator);
	regulator_put(regulator);
	CAM_CHECK_ERR_RET(ret, "3mp_core");
	/* udelay(1); */

	/* VT_CORE_1.8V */
	regulator = regulator_get(NULL, "vt_core_1.8v");
	if (IS_ERR(regulator))
		return -ENODEV;
	ret = regulator_enable(regulator);
	regulator_put(regulator);
	CAM_CHECK_ERR_RET(ret, "vt_core_1.8v");
	/* udelay(1); */

	/* CAM_IO_1.8V */
	regulator = regulator_get(NULL, "cam_io_1.8v");
	if (IS_ERR(regulator))
		return -ENODEV;
	ret = regulator_enable(regulator);
	regulator_put(regulator);
	CAM_CHECK_ERR_RET(ret, "cam_io_1.8v");
	udelay(70);

	/* 2M_nSTBY High */
	ret = gpio_direction_output(GPIO_2M_nSTBY, 1);
	CAM_CHECK_ERR_RET(ret, "2M_nSTBY");
	udelay(10);

	/* Mclk */
	ret = s3c_gpio_cfgpin(GPIO_CAM_MCLK, S3C_GPIO_SFN(2));
	s3c_gpio_setpull(GPIO_CAM_MCLK, S3C_GPIO_PULL_NONE);
	CAM_CHECK_ERR_RET(ret, "cfg mclk");
	udelay(50);

	/* 2M_nRST High */
	ret = gpio_direction_output(GPIO_2M_nRST, 1);
	CAM_CHECK_ERR_RET(ret, "2M_nRST");
	udelay(50);

#if !defined(CONFIG_MACH_P8LTE)
	gpio_free(GPIO_CAM_AVDD_EN);
#endif
	gpio_free(GPIO_2M_nSTBY);
	gpio_free(GPIO_2M_nRST);
	gpio_free(GPIO_3M_nSTBY);
	gpio_free(GPIO_3M_nRST);

	return 0;
}

static int s5k5bafx_power_off(void)
{
	struct regulator *regulator;
	int ret = 0;

	printk(KERN_DEBUG "%s: in\n", __func__);

#if !defined(CONFIG_MACH_P8LTE)
	ret = gpio_request(GPIO_CAM_AVDD_EN, "GPJ1");
	if (ret) {
		printk(KERN_ERR "Error: fail to request gpio(CAM_AVDD)\n");
		return ret;
	}
#endif
	ret = gpio_request(GPIO_2M_nRST, "GPL2");
	if (ret) {
		printk(KERN_ERR "Error: fail to request gpio(3M_nRST)\n");
		return ret;
	}
	ret = gpio_request(GPIO_2M_nSTBY, "GPL2");
	if (ret) {
		printk(KERN_ERR "Error: fail to request gpio(3M_nSTBY)\n");
		return ret;
	}

	/* 2M_nRST Low*/
	ret = gpio_direction_output(GPIO_2M_nRST, 0);
	CAM_CHECK_ERR(ret, "3M_nSTBY");
	udelay(55);

	/* CAM_MCLK */
	ret = s3c_gpio_cfgpin(GPIO_CAM_MCLK, S3C_GPIO_INPUT);
	s3c_gpio_setpull(GPIO_CAM_MCLK, S3C_GPIO_PULL_DOWN);
	CAM_CHECK_ERR(ret, "cfg mclk");
	udelay(10);

	/* 2M_nSTBY */
	ret = gpio_direction_output(GPIO_2M_nSTBY, 0);
	CAM_CHECK_ERR(ret, "3M_nSTBY");
	udelay(1);

	/* CAM_IO_1.8V */
	regulator = regulator_get(NULL, "cam_io_1.8v");
	if (IS_ERR(regulator))
		return -ENODEV;
	if (regulator_is_enabled(regulator))
		ret = regulator_force_disable(regulator);
	regulator_put(regulator);
	CAM_CHECK_ERR(ret, "cam_io_1.8v");
	/* udelay(1); */

	/* VT_CORE_1.8V */
	regulator = regulator_get(NULL, "vt_core_1.8v");
	if (IS_ERR(regulator))
		return -ENODEV;
	if (regulator_is_enabled(regulator))
		ret = regulator_force_disable(regulator);
	regulator_put(regulator);
	CAM_CHECK_ERR(ret, "vt_core_1.8v");
	/* udelay(1); */

	/* 3MP_CORE_1.2V */
	regulator = regulator_get(NULL, "3mp_core");
	if (IS_ERR(regulator))
		return -ENODEV;
	if (regulator_is_enabled(regulator))
		ret = regulator_force_disable(regulator);
	regulator_put(regulator);
	CAM_CHECK_ERR(ret, "3mp_core");

	/* CAM_A2.8V */
#if defined(CONFIG_MACH_P8LTE)
	regulator = regulator_get(NULL, "cam_analog_2.8v");
	if (IS_ERR(regulator))
		return -ENODEV;
	if (regulator_is_enabled(regulator))
		ret = regulator_force_disable(regulator);
	regulator_put(regulator);
	CAM_CHECK_ERR(ret, "CAM_A2.8V");
#else
	ret = gpio_direction_output(GPIO_CAM_AVDD_EN, 0);
	CAM_CHECK_ERR_RET(ret, "CAM_AVDD");
#endif

#if !defined(CONFIG_MACH_P8LTE)
	gpio_free(GPIO_CAM_AVDD_EN);
#endif
	gpio_free(GPIO_2M_nRST);
	gpio_free(GPIO_2M_nSTBY);

	return 0;
}

static int s5k5bafx_power(int onoff)
{
	int ret = 0;

	printk(KERN_INFO "%s(): %s\n", __func__, onoff ? "on" : "down");
	if (onoff) {
		ret = s5k5bafx_power_on();
		if (unlikely(ret))
			goto error_out;
	} else {
		ret = s5k5bafx_power_off();
		/* s3c_i2c0_force_stop();*//* DSLIM. Should be implemented */
	}

	ret = s3c_csis_power(onoff);

error_out:
	return ret;
}

static struct s5k5bafx_platform_data s5k5bafx_plat = {
	.default_width = 800,
	.default_height = 600,
	.pixelformat = V4L2_PIX_FMT_UYVY,
	.freq = 24000000,
	.is_mipi = 1,
	.streamoff_delay = S5K5BAFX_STREAMOFF_DELAY,
	.init_streamoff = false,
	.dbg_level = CAMDBG_LEVEL_DEFAULT,
};
#define FRONT_CAM_PLAT		(s5k5bafx_plat)

static struct i2c_board_info  s5k5bafx_i2c_info = {
	I2C_BOARD_INFO("S5K5BAFX", 0x5A >> 1),
	.platform_data = &s5k5bafx_plat,
};

static struct s3c_platform_camera s5k5bafx = {
	.id		= CAMERA_CSI_D,
	.type		= CAM_TYPE_MIPI,
	.fmt		= ITU_601_YCBCR422_8BIT,
	.order422	= CAM_ORDER422_8BIT_CBYCRY,
	.mipi_lanes	= 1,
	.mipi_settle	= 6,
	.mipi_align	= 32,

	.get_i2c_busnum	= s5k5bafx_get_i2c_busnum,
	.info		= &s5k5bafx_i2c_info,
	.pixelformat	= V4L2_PIX_FMT_UYVY,
	.srclk_name	= "xusbxti",
	.clk_name	= "sclk_cam0",
	.clk_rate	= 24000000,
	.line_length	= 800,
	.width		= 800,
	.height		= 600,
	.window		= {
		.left	= 0,
		.top	= 0,
		.width	= 800,
		.height	= 600,
	},

	/* Polarity */
	.inv_pclk	= 0,
	.inv_vsync	= 1,
	.inv_href	= 0,
	.inv_hsync	= 0,
	.reset_camera	= 0,
	.initialized	= 0,
	.cam_power	= s5k5bafx_power,
};
#endif


#ifdef CONFIG_VIDEO_SR200PC20
static int sr200pc20_get_i2c_busnum(void)
{
#ifdef CONFIG_MACH_P4
	if (system_rev >= 2)
		return 0;
	else
#endif
		return 13;
}

static int sr200pc20_power_on(void)
{
	struct regulator *regulator;
	int ret = 0;

#ifndef USE_CAM_GPIO_CFG
	ret = gpio_request(GPIO_2M_nSTBY, "GPL2");
	if (ret) {
		printk(KERN_ERR "Error: fail to request gpio(2M_nSTBY)\n");
		return ret;
	}
	ret = gpio_request(GPIO_2M_nRST, "GPL2");
	if (ret) {
		printk(KERN_ERR "Error: fail to request gpio(2M_nRST)\n");
		return ret;
	}
	ret = gpio_request(GPIO_3M_nSTBY, "GPL2");
	if (ret) {
		printk(KERN_ERR "Error: fail to request gpio(3M_nSTBY)\n");
		return ret;
	}
	ret = gpio_request(GPIO_3M_nRST, "GPL2");
	if (ret) {
		printk(KERN_ERR "Error: fail to request gpio(3M_nRST)\n");
		return ret;
	}
#endif

	/* 3M_nSTBY low */
	ret = gpio_direction_output(GPIO_3M_nSTBY, 0);
	CAM_CHECK_ERR_RET(ret, "3M_nSTBY");

	/* 3M_nRST low */
	ret = gpio_direction_output(GPIO_3M_nRST, 0);
	CAM_CHECK_ERR_RET(ret, "3M_nRST");

	/* 2M_nSTBY low */
	ret = gpio_direction_output(GPIO_2M_nSTBY, 0);
	CAM_CHECK_ERR_RET(ret, "2M_nSTBY");

	/* 2M_nRST low */
	ret = gpio_direction_output(GPIO_2M_nRST, 0);
	CAM_CHECK_ERR_RET(ret, "2M_nRST");

	/* 3MP_CORE_1.2V */
	regulator = regulator_get(NULL, "3mp_core");
	if (IS_ERR(regulator))
		return -ENODEV;
	ret = regulator_enable(regulator);
	regulator_put(regulator);
	CAM_CHECK_ERR_RET(ret, "3mp_core");
	/* udelay(5); */

	/* CAM_IO_1.8V */
	regulator = regulator_get(NULL, "cam_io_1.8v");
	if (IS_ERR(regulator))
		return -ENODEV;
	ret = regulator_enable(regulator);
	regulator_put(regulator);
	CAM_CHECK_ERR_RET(ret, "cam_io_1.8v");
	/*udelay(5); */

	/* CAM_A2.8V */
	regulator = regulator_get(NULL, "cam_analog_2.8v");
	if (IS_ERR(regulator))
		return -ENODEV;
	ret = regulator_enable(regulator);
	regulator_put(regulator);
	CAM_CHECK_ERR_RET(ret, "cam_analog_2.8v");
	/* udelay(5); */

	/* VT_CORE_1.8V */
	regulator = regulator_get(NULL, "vt_core_1.8v");
	if (IS_ERR(regulator))
		return -ENODEV;
	ret = regulator_enable(regulator);
	regulator_put(regulator);
	CAM_CHECK_ERR_RET(ret, "vt_core_1.8v");
	udelay(20);

	/* ENB High */
	ret = gpio_direction_output(GPIO_2M_nSTBY, 1);
	CAM_CHECK_ERR_RET(ret, "2M_nSTBY");
	udelay(3); /* 30 -> 3 */

	/* CAM_MCLK */
	ret = s3c_gpio_cfgpin(GPIO_CAM_MCLK, S3C_GPIO_SFN(2));
	CAM_CHECK_ERR_RET(ret, "cfg mclk");
	s3c_gpio_setpull(GPIO_CAM_MCLK, S3C_GPIO_PULL_NONE);
	msleep(5); /* >= 5ms */

	/* ENB Low */
	ret = gpio_direction_output(GPIO_2M_nSTBY, 0);
	CAM_CHECK_ERR_RET(ret, "2M_nSTBY");
	msleep(10); /* >= 10ms */

	/* 2M_nRST High*/
	ret = gpio_direction_output(GPIO_2M_nRST, 1);
	CAM_CHECK_ERR_RET(ret, "2M_nRST");
	/*msleep(7);*/ /* >= 7ms */

#if 0
	/* ENB High */
	ret = gpio_direction_output(GPIO_2M_nSTBY, 1);
	CAM_CHECK_ERR_RET(ret, "2M_nSTBY");
	msleep(12); /* >= 10ms */

	/* ENB Low */
	ret = gpio_direction_output(GPIO_2M_nSTBY, 0);
	CAM_CHECK_ERR_RET(ret, "2M_nSTBY");
	msleep(12); /* >= 10ms */

	/* 2M_nRST Low*/
	ret = gpio_direction_output(GPIO_2M_nRST, 0);
	CAM_CHECK_ERR_RET(ret, "2M_nRST");
	udelay(10); /* >= 16 cycle */

	/* 2M_nRST High */
	ret = gpio_direction_output(GPIO_2M_nRST, 1);
	CAM_CHECK_ERR_RET(ret, "2M_nRST");
#endif
	udelay(10); /* >= 16 cycle */

#ifndef USE_CAM_GPIO_CFG
	gpio_free(GPIO_2M_nSTBY);
	gpio_free(GPIO_2M_nRST);
	gpio_free(GPIO_3M_nSTBY);
	gpio_free(GPIO_3M_nRST);
#endif
	return 0;
}

static int sr200pc20_power_off(void)
{
	struct regulator *regulator;
	int ret = 0;

	printk(KERN_DEBUG "%s in\n", __func__);

#ifndef USE_CAM_GPIO_CFG
	ret = gpio_request(GPIO_2M_nSTBY, "GPL2");
	if (ret) {
		printk(KERN_ERR "Error: fail to request gpio(2M_nSTBY)\n");
		return ret;
	}
	ret = gpio_request(GPIO_2M_nRST, "GPL2");
	if (ret) {
		printk(KERN_ERR "Error: fail to request gpio(2M_nRST)\n");
		return ret;
	}
#endif

#if 0
	/* 2M_nRST */
	ret = gpio_direction_output(GPIO_2M_nRST, 0);
	CAM_CHECK_ERR_RET(ret, "2M_nRST");
	udelay(100);

	/* 2M_nSTBY */
	ret = gpio_direction_output(GPIO_2M_nSTBY, 0);
	CAM_CHECK_ERR_RET(ret, "2M_nSTBY");
	udelay(100);
#endif
	/* Sleep command */
	mdelay(1);

	/* 2M_nRST Low*/
	ret = gpio_direction_output(GPIO_2M_nRST, 0);
	CAM_CHECK_ERR_RET(ret, "2M_nRST");
	udelay(3);

	/* CAM_MCLK */
	ret = s3c_gpio_cfgpin(GPIO_CAM_MCLK, S3C_GPIO_INPUT);
	s3c_gpio_setpull(GPIO_CAM_MCLK, S3C_GPIO_PULL_DOWN);
	CAM_CHECK_ERR(ret, "cfg mclk");
	udelay(10);

	/* ENB High*/
	ret = gpio_direction_output(GPIO_2M_nSTBY, 1);
	CAM_CHECK_ERR_RET(ret, "2M_nSTBY");
	mdelay(5);

	/* ENB Low */
	ret = gpio_direction_output(GPIO_2M_nSTBY, 0);
	CAM_CHECK_ERR_RET(ret, "2M_spnSTBY");
	/* udelay(1); */

	/* VT_CORE_1.8V */
	regulator = regulator_get(NULL, "vt_core_1.8v");
	if (IS_ERR(regulator))
		return -ENODEV;
	if (regulator_is_enabled(regulator))
		ret = regulator_force_disable(regulator);
	regulator_put(regulator);
	CAM_CHECK_ERR(ret, "vt_core_1.8v");
	/* udelay(10); */

	/* CAM_A2.8V */
	regulator = regulator_get(NULL, "cam_analog_2.8v");
	if (IS_ERR(regulator))
		return -ENODEV;
	if (regulator_is_enabled(regulator))
		ret = regulator_force_disable(regulator);
	regulator_put(regulator);
	CAM_CHECK_ERR(ret, "cam_analog_2.8v");
	/* udelay(10); */

	/* CAM_IO_1.8V */
	regulator = regulator_get(NULL, "cam_io_1.8v");
	if (IS_ERR(regulator))
		return -ENODEV;
	if (regulator_is_enabled(regulator))
		ret = regulator_force_disable(regulator);
	regulator_put(regulator);
	CAM_CHECK_ERR(ret, "cam_io_1.8v");
	/*udelay(10); */

	/* 3MP_CORE_1.2V */
	regulator = regulator_get(NULL, "3mp_core");
	if (IS_ERR(regulator))
		return -ENODEV;
	if (regulator_is_enabled(regulator))
		ret = regulator_force_disable(regulator);
	regulator_put(regulator);

#ifndef USE_CAM_GPIO_CFG
	gpio_free(GPIO_2M_nSTBY);
	gpio_free(GPIO_2M_nRST);
#endif
	return 0;
}

static int sr200pc20_power(int onoff)
{
	int ret = 0;

	printk(KERN_DEBUG "%s(): %s\n", __func__, onoff ? "on" : "down");

	if (onoff) {
#ifdef USE_CAM_GPIO_CFG
		if (cfg_gpio_err) {
			printk(KERN_ERR "%s: ERROR: gpio configuration",
				__func__);
			return cfg_gpio_err;
		}
#endif
		ret = sr200pc20_power_on();
	} else {
		ret = sr200pc20_power_off();
		/* s3c_i2c0_force_stop();*/ /* DSLIM. Should be implemented */
	}

	return ret;
}

static struct sr200pc20_platform_data sr200pc20_plat = {
	.default_width = 800,
	.default_height = 600,
	.pixelformat = V4L2_PIX_FMT_UYVY,
	.is_mipi = 0,
	.streamoff_delay = 0,
	.dbg_level = CAMDBG_LEVEL_DEFAULT,
};
#define FRONT_CAM_PLAT		(sr200pc20_plat)

static struct i2c_board_info  sr200pc20_i2c_info = {
	I2C_BOARD_INFO("SR200PC20", 0x40 >> 1),
	.platform_data = &sr200pc20_plat,
};

static struct s3c_platform_camera sr200pc20 = {
	.id		= CAMERA_PAR_A,
	.type		= CAM_TYPE_ITU,
	.fmt		= ITU_601_YCBCR422_8BIT,
	.order422	= CAM_ORDER422_8BIT_YCBYCR,
	.get_i2c_busnum	= sr200pc20_get_i2c_busnum,
	.info		= &sr200pc20_i2c_info,
	.pixelformat	= V4L2_PIX_FMT_UYVY,
	.srclk_name	= "xusbxti",
	.clk_name	= "sclk_cam0",
	.clk_rate	= 24000000,
	.line_length	= 800,
	.width		= 800,
	.height		= 600,
	.window		= {
		.left	= 0,
		.top	= 0,
		.width	= 800,
		.height	= 600,
	},

	/* Polarity */
#if 0 /*def CONFIG_VIDEO_SR200PC20_P4W */
	.inv_pclk	= 0,
	.inv_vsync	= 1,
#else
	.inv_pclk	= 1,
	.inv_vsync	= 0,
#endif
	.inv_href	= 0,
	.inv_hsync	= 0,
	.reset_camera	= 0,
	.initialized	= 0,
	.cam_power	= sr200pc20_power,
};
#endif /* CONFIG_VIDEO_SR200PC20 */



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
#ifdef CONFIG_VIDEO_S5K5CCGX_COMMON
		&s5k5ccgx,
#endif
#ifdef CONFIG_VIDEO_S5K5BAFX
		&s5k5bafx,
#endif
#ifdef CONFIG_VIDEO_SR200PC20
		&sr200pc20,
#endif
#ifdef WRITEBACK_ENABLED
		&writeback,
#endif
	},
	.hw_ver = 0x51,
};

#if defined(CONFIG_VIDEO_S5K5CCGX_COMMON) || defined(CONFIG_VIDEO_S5K5BAFX) \
	|| defined(CONFIG_VIDEO_SR200PC20)
ssize_t cam_loglevel_show(struct device *dev, struct device_attribute *attr,
			char *buf)
{
	char temp_buf[60] = {0,};

	sprintf(buf, "Log Level(Rear): ");
	if (REAR_CAM_PLAT.dbg_level & CAMDBG_LEVEL_TRACE) {
		sprintf(temp_buf, "trace ");
		strcat(buf, temp_buf);
	}

	if (REAR_CAM_PLAT.dbg_level & CAMDBG_LEVEL_DEBUG) {
		sprintf(temp_buf, "debug ");
		strcat(buf, temp_buf);
	}

	if (REAR_CAM_PLAT.dbg_level & CAMDBG_LEVEL_INFO) {
		sprintf(temp_buf, "info ");
		strcat(buf, temp_buf);
	}

	sprintf(temp_buf, "\nLog Level(Front): ");
	strcat(buf, temp_buf);
	if (FRONT_CAM_PLAT.dbg_level & CAMDBG_LEVEL_TRACE) {
		sprintf(temp_buf, "trace ");
		strcat(buf, temp_buf);
	}

	if (FRONT_CAM_PLAT.dbg_level & CAMDBG_LEVEL_DEBUG) {
		sprintf(temp_buf, "debug ");
		strcat(buf, temp_buf);
	}

	if (FRONT_CAM_PLAT.dbg_level & CAMDBG_LEVEL_INFO) {
		sprintf(temp_buf, "info ");
		strcat(buf, temp_buf);
	}

	sprintf(temp_buf, "\n - Warn and Error level is always on\n\n");
	strcat(buf, temp_buf);

	return strlen(buf);
}

ssize_t cam_loglevel_store(struct device *dev, struct device_attribute *attr,
			 const char *buf, size_t count)
{
	printk(KERN_DEBUG "CAM buf=%s, count=%d\n", buf, count);

	if (strstr(buf, "trace")) {
		REAR_CAM_PLAT.dbg_level |= CAMDBG_LEVEL_TRACE;
		FRONT_CAM_PLAT.dbg_level |= CAMDBG_LEVEL_TRACE;
	}

	if (strstr(buf, "debug")) {
		REAR_CAM_PLAT.dbg_level |= CAMDBG_LEVEL_DEBUG;
		FRONT_CAM_PLAT.dbg_level |= CAMDBG_LEVEL_DEBUG;
	}

	if (strstr(buf, "info")) {
		REAR_CAM_PLAT.dbg_level |= CAMDBG_LEVEL_INFO;
		FRONT_CAM_PLAT.dbg_level |= CAMDBG_LEVEL_INFO;
	}

	return count;
}
static DEVICE_ATTR(loglevel, 0664, cam_loglevel_show, cam_loglevel_store);
#endif

ssize_t rear_camera_type_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	/* Change camera type properly */
	char cam_type[] = "SLSI_S5K5CCGX";

	pr_info("%s\n", __func__);
	return sprintf(buf, "%s\n", cam_type);
}

ssize_t front_camera_type_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	/* Change camera type properly */
#if defined(CONFIG_MACH_P8) || defined(CONFIG_MACH_P8LTE)
	char cam_type[] = "SLSI_S5K5BAFX";
#else
	char cam_type[] = "SILICONFILE_SR200PC20";
#endif

	pr_info("%s\n", __func__);
	return sprintf(buf, "%s\n", cam_type);
}
static DEVICE_ATTR(rear_camtype, 0664, rear_camera_type_show, NULL);
static DEVICE_ATTR(front_camtype, 0664, front_camera_type_show, NULL);

#ifdef CONFIG_VIDEO_S5K5CCGX_COMMON
ssize_t flash_show(struct device *dev, struct device_attribute *attr,
			char *buf)
{
	return sprintf(buf, "%s\n", s5k5ccgx_is_flash_on() ? "on" : "off");
}

ssize_t flash_store(struct device *dev, struct device_attribute *attr,
			 const char *buf, size_t count)
{
	switch (*buf) {
	case '0':
		s5k5ccgx_flash_en(S5K5CCGX_FLASH_MODE_MOVIE,
				S5K5CCGX_FLASH_OFF);
		break;
	case '1':
		s5k5ccgx_flash_en(S5K5CCGX_FLASH_MODE_MOVIE,
				S5K5CCGX_FLASH_ON);
		break;
	default:
		pr_err("flash: invalid data=%c(0x%X)\n", *buf, *buf);
		break;
	}

	return count;
}
static DEVICE_ATTR(rear_flash, 0664, flash_show, flash_store);
#endif


static inline int cam_cfg_init(void)
{
#ifdef CONFIG_VIDEO_S5K5CCGX_COMMON
	return px_cam_cfg_init();
#else
	return 0;
#endif
}

static int cam_create_file(struct class *cls)
{
	struct device *dev_rear = NULL;
	struct device *dev_front = NULL;
	int ret = -ENODEV;

#if defined(CONFIG_VIDEO_S5K5CCGX_COMMON) || defined(CONFIG_VIDEO_S5K5BAFX) \
	|| defined(CONFIG_VIDEO_SR200PC20)

	dev_rear = device_create(cls, NULL, 0, NULL, "rear");
	if (IS_ERR(dev_rear)) {
		pr_err("cam_init: failed to create device(rearcam_dev)\n");
		dev_rear = NULL;
		goto front;
	}

	ret = device_create_file(dev_rear, &dev_attr_rear_camtype);
	if (unlikely(ret < 0)) {
		pr_err("cam_init: failed to create device file, %s\n",
			dev_attr_rear_camtype.attr.name);
	}

	ret = device_create_file(dev_rear, &dev_attr_rear_flash);
	if (unlikely(ret < 0)) {
		pr_err("cam_init: failed to create device file, %s\n",
			dev_attr_rear_flash.attr.name);
	}

	ret = device_create_file(dev_rear, &dev_attr_loglevel);
	if (unlikely(ret < 0)) {
		pr_err("cam_init: failed to create device file, %s\n",
			dev_attr_loglevel.attr.name);
	}

front:
	dev_front = device_create(cls, NULL, 0, NULL, "front");
	if (IS_ERR(dev_front)) {
		pr_err("cam_init: failed to create device(frontcam_dev)\n");
		goto out_unreg_class;
	}

	ret = device_create_file(dev_front, &dev_attr_front_camtype);
	if (unlikely(ret < 0)) {
		pr_err("cam_init: failed to create device file, %s\n",
			dev_attr_front_camtype.attr.name);
		goto out_unreg_dev_front;
	}
#endif
	return 0;

out_unreg_dev_front:
	device_destroy(cls, 0);
out_unreg_class:
	if (!dev_rear)
		class_destroy(cls);

	return -ENODEV;
}

static struct class *camera_class;

/**
 * cam_init - Intialize something concerning camera device if needed.
 *
 * And excute codes about camera needed on boot-up time
 */
static void cam_init(void)
{
	/* pr_info("%s: E\n", __func__); */

	cam_cfg_init();

	camera_class = class_create(THIS_MODULE, "camera");
	if (IS_ERR(camera_class)) {
		pr_err("cam_init: failed to create class\n");
		return;
	}

	/* create device and device file for supporting camera sysfs.*/
	cam_create_file(camera_class);

	pr_info("%s: X\n", __func__);
}

#endif				/* CONFIG_VIDEO_FIMC */

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
	.cd_type = S3C_SDHCI_CD_EXTERNAL,
	.clk_type = S3C_SDHCI_CLK_DIV_EXTERNAL,
	.host_caps = MMC_CAP_4_BIT_DATA,
#if defined(CONFIG_MACH_P8LTE)
	.pm_flags = S3C_SDHCI_PM_IGNORE_SUSPEND_RESUME,
#endif
#ifdef CONFIG_MACH_PX
	.ext_cd_init = register_wlan_status_notify,
	.ext_pdev = register_wlan_pdev
#endif
};
#endif

#ifdef CONFIG_EXYNOS4_DEV_MSHC
static struct s3c_mshci_platdata exynos4_mshc_pdata __initdata = {
	.cd_type = S3C_MSHCI_CD_PERMANENT,
#if defined(CONFIG_EXYNOS4_MSHC_8BIT) &&	\
	defined(CONFIG_EXYNOS4_MSHC_DDR)
	.max_width = 8,
	.host_caps = MMC_CAP_8_BIT_DATA | MMC_CAP_1_8V_DDR | MMC_CAP_UHS_DDR50,
#elif defined(CONFIG_EXYNOS4_MSHC_8BIT)
	.max_width = 8,
	.host_caps = MMC_CAP_8_BIT_DATA,
#elif defined(CONFIG_EXYNOS4_MSHC_DDR)
	.host_caps = MMC_CAP_1_8V_DDR | MMC_CAP_UHS_DDR50,
#endif
	.int_power_gpio = GPIO_XMMC0_CDn,
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
	msleep(100);

	gpio_set_value(EXYNOS4_GPX0(6), 1);
	msleep(100);

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
		msleep(100);

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
#ifdef CONFIG_S3C64XX_DEV_SPI0
static struct s3c64xx_spi_csinfo spi0_csi[] = {
	[0] = {
		.line = EXYNOS4_GPB(1),
		.set_level = gpio_set_value,
		.fb_delay = 0x0,
	},
};

static struct spi_board_info spi0_board_info[] __initdata = {
	{
		.modalias = "spidev",
		.platform_data = NULL,
		.max_speed_hz = 10 * 1000 * 1000,
		.bus_num = 0,
		.chip_select = 0,
		.mode = SPI_MODE_0,
		.controller_data = &spi0_csi[0],
	}
};
#endif
#ifdef CONFIG_FB_S5P

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
#endif

static struct resource smdkc210_smsc911x_resources[] = {
	[0] = {
		.start = EXYNOS4_PA_SROM_BANK(1),
		.end = EXYNOS4_PA_SROM_BANK(1) + SZ_64K - 1,
		.flags = IORESOURCE_MEM,
	},
	[1] = {
		.start = IRQ_EINT(5),
		.end = IRQ_EINT(5),
		.flags = IORESOURCE_IRQ | IRQF_TRIGGER_LOW,
	},
};

static struct smsc911x_platform_config smsc9215_config = {
	.irq_polarity = SMSC911X_IRQ_POLARITY_ACTIVE_LOW,
	.irq_type = SMSC911X_IRQ_TYPE_PUSH_PULL,
	.flags = SMSC911X_USE_16BIT | SMSC911X_FORCE_INTERNAL_PHY,
	.phy_interface = PHY_INTERFACE_MODE_MII,
	.mac = {0x00, 0x80, 0x00, 0x23, 0x45, 0x67},
};

static struct platform_device smdkc210_smsc911x = {
	.name = "smsc911x",
	.id = -1,
	.num_resources = ARRAY_SIZE(smdkc210_smsc911x_resources),
	.resource = smdkc210_smsc911x_resources,
	.dev = {
		.platform_data = &smsc9215_config,
	},
};

static struct platform_device u1_regulator_consumer = {
	.name = "u1-regulator-consumer",
	.id = -1,
};

#ifdef CONFIG_REGULATOR_MAX8997
static struct regulator_consumer_supply ldo1_supply[] = {
	REGULATOR_SUPPLY("vadc_3.3v", NULL),
};

static struct regulator_consumer_supply ldo3_supply[] = {
	REGULATOR_SUPPLY("vusb_1.1v", "s5p-ehci"),
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
	REGULATOR_SUPPLY("vt_core_1.5v", NULL),
	REGULATOR_SUPPLY("vt_core_1.8v", NULL),
};

static struct regulator_consumer_supply ldo8_supply[] = {
	REGULATOR_SUPPLY("vusb_3.3v", NULL),
};

static struct regulator_consumer_supply ldo10_supply[] = {
	REGULATOR_SUPPLY("vpll_1.1v", NULL),
};

static struct regulator_consumer_supply ldo11_supply[] = {
	REGULATOR_SUPPLY("hdp_2.8v", NULL),
};

static struct regulator_consumer_supply ldo12_supply[] = {
	REGULATOR_SUPPLY("cam_io_1.8v", NULL),
};

static struct regulator_consumer_supply ldo13_supply[] = {
	REGULATOR_SUPPLY("cam_analog_2.8v", NULL),
};

static struct regulator_consumer_supply ldo14_supply[] = {
	REGULATOR_SUPPLY("vmotor", NULL),
};

static struct regulator_consumer_supply ldo15_supply[] = {
	REGULATOR_SUPPLY("vled_3.3v", NULL),
};

static struct regulator_consumer_supply ldo16_supply[] = {
	REGULATOR_SUPPLY("irda_2.8v", NULL),
};

static struct regulator_consumer_supply ldo17_supply[] = {
	REGULATOR_SUPPLY("3m_af_2.8v", NULL),
};

static struct regulator_consumer_supply ldo18_supply[] = {
	REGULATOR_SUPPLY("vtf_2.8v", NULL),
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
	REGULATOR_SUPPLY("3mp_core", NULL),
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
REGULATOR_INIT(ldo5, "VHSIC_1.2V", 1200000, 1200000, 1,
		REGULATOR_CHANGE_STATUS, 1);

#if defined(CONFIG_MACH_P8) || defined(CONFIG_MACH_P8LTE)
REGULATOR_INIT(ldo7, "VT_CORE_1.5V", 1500000, 1500000, 0,
		REGULATOR_CHANGE_STATUS, 1);
#else
REGULATOR_INIT(ldo7, "VT_CORE_1.8V", 1800000, 1800000, 0,
		REGULATOR_CHANGE_STATUS, 1);
#endif

REGULATOR_INIT(ldo8, "VUSB_3.3V", 3300000, 3300000, 1,
		REGULATOR_CHANGE_STATUS, 1);
REGULATOR_INIT(ldo10, "VPLL_1.2V", 1200000, 1200000, 1,
		REGULATOR_CHANGE_STATUS, 1);
REGULATOR_INIT(ldo11, "VCC_2.8V_HPD", 2800000, 2800000, 0,
		REGULATOR_CHANGE_STATUS, 1);
REGULATOR_INIT(ldo12, "CAM_IO_1.8V", 1800000, 1800000, 0,
		REGULATOR_CHANGE_STATUS, 1);
REGULATOR_INIT(ldo13, "CAM_ANALOG_2.8V", 2800000, 2800000, 0,
		REGULATOR_CHANGE_STATUS, 1);

#if defined(CONFIG_MACH_P2)
REGULATOR_INIT(ldo14, "VCC_3.0V_MOTOR", 2400000, 2400000, 0,
		REGULATOR_CHANGE_STATUS, 1);
#elif defined(CONFIG_MACH_P8) || defined(CONFIG_MACH_P8LTE)
REGULATOR_INIT(ldo14, "VCC_3.0V_MOTOR", 3100000, 3100000, 0,
		REGULATOR_CHANGE_STATUS, 1);
#else
REGULATOR_INIT(ldo14, "VCC_3.0V_MOTOR", 3000000, 3000000, 0,
		REGULATOR_CHANGE_STATUS, 1);
#endif

#if defined(CONFIG_MACH_P8)
REGULATOR_INIT(ldo15, "VLED_3.3V", 3200000, 3200000, 1,
		REGULATOR_CHANGE_STATUS, -1);
#else
REGULATOR_INIT(ldo15, "VLED_3.3V", 3300000, 3300000, 0,
		REGULATOR_CHANGE_STATUS, 1);
#endif

REGULATOR_INIT(ldo16, "IRDA_2.8V", 2800000, 2800000, 0,
		REGULATOR_CHANGE_STATUS, 1);
REGULATOR_INIT(ldo17, "3M_AF_2.8V", 2800000, 2800000, 0,
		REGULATOR_CHANGE_STATUS, 1);
REGULATOR_INIT(ldo18, "VTF_2.8V", 2800000, 2800000, 0,
		REGULATOR_CHANGE_STATUS, 1);
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
			.uV		= 1250000,
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
			.uV		= 1100000,
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
		.boot_on	= 1,
		.apply_uV	= 1,
		.valid_ops_mask	= REGULATOR_CHANGE_VOLTAGE |
				  REGULATOR_CHANGE_STATUS,
		.state_mem	= {
			.uV		= 1100000,
			.mode		= REGULATOR_MODE_NORMAL,
			.disabled	= 1,
		},
	},
	.num_consumer_supplies	= 1,
	.consumer_supplies	= &buck3_supply[0],
};

static struct regulator_init_data buck4_init_data = {
	.constraints	= {
		.name		= "3MP_CORE_1.2V",
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
			.uV		= 1200000,
			.mode		= REGULATOR_MODE_NORMAL,
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
		.valid_ops_mask	= REGULATOR_CHANGE_STATUS,
		.always_on	= 1,
		.boot_on	= 1,
		.state_mem      = {
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
		.always_on	= 0,
		.boot_on	= 0,
		.state_mem	= {
			.enabled = 1,
		},
	},
	.num_consumer_supplies	= ARRAY_SIZE(safeout2_supply),
	.consumer_supplies	= safeout2_supply,
};

static struct max8997_regulator_data max8997_regulators[] = {
	{ MAX8997_LDO1,	 &ldo1_init_data, NULL, },
	{ MAX8997_LDO3,  &ldo3_init_data, NULL, },
	{ MAX8997_LDO4,  &ldo4_init_data, NULL, },
	{ MAX8997_LDO5,  &ldo5_init_data, NULL, },
	{ MAX8997_LDO7,  &ldo7_init_data, NULL, },
	{ MAX8997_LDO8,  &ldo8_init_data, NULL, },
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
};

static struct max8997_power_data max8997_power = {
	.batt_detect = 1,
};

#ifdef CONFIG_VIBETONZ
static struct max8997_motor_data max8997_motor = {
#if defined(CONFIG_MACH_P8) || defined(CONFIG_MACH_P8LTE)
	.reg2 = MOTOR_LRA | EXT_PWM | DIVIDER_256,
#else
	.reg2 = MOTOR_LRA | EXT_PWM | DIVIDER_128,
#endif
	.max_timeout = 10000,
#if defined(CONFIG_MACH_P4)
	.duty = 37000,
	.period = 38675,
#elif defined(CONFIG_MACH_P2)
	.duty = 44707,
	.period = 45159,
#elif defined(CONFIG_MACH_P8) || defined(CONFIG_MACH_P8LTE)
	.duty = 38288,
	.period = 38676,
#else
	.duty = 37641,
	.period = 38022,
#endif
	.init_hw = NULL,
	.motor_en = NULL,
	.pwm_id = 1,
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
	.mr_debounce_time = 8,	/* 8sec */
	.power = &max8997_power,
#ifdef CONFIG_VIBETONZ
	.motor = &max8997_motor,
#endif
	.register_buck1_dvs_funcs = max8997_register_buck1dvs_funcs,
};
#endif /* CONFIG_REGULATOR_MAX8997 */


#ifdef CONFIG_MPU_SENSORS_MPU3050

extern struct class *sec_class;

/* we use a skeleton to provide some information needed by MPL
 * but we don't use the suspend/resume/read functions so we
 * don't initialize them so that mldl_cfg.c doesn't try to
 * control it directly.  we have a separate mag driver instead.
 */
static struct mpu3050_platform_data mpu3050_pdata = {
	.int_config  = 0x12,
	/* Orientation for MPU.  Part is mounted rotated
	 * 90 degrees counter-clockwise from natural orientation.
	 * So X & Y are swapped and Y is negated.
	 */
#if defined(CONFIG_MACH_P8)
	.orientation = {0, 1, 0,
			1, 0, 0,
			0, 0, -1},
#elif defined(CONFIG_MACH_P8LTE)
	.orientation = {0, -1, 0,
			1, 0, 0,
			0, 0, 1},
#elif defined(CONFIG_MACH_P2)
	.orientation = {1, 0, 0,
			0, -1, 0,
			0, 0, -1},
#elif defined(CONFIG_MACH_P4)
	.orientation = {1 , 0, 0,
			0, -1, 0,
			0, 0, -1},
#else
	.orientation = {0, -1, 0,
			-1, 0, 0,
			0, 0, -1},
#endif
	.level_shifter = 0,
	.accel = {
		.get_slave_descr = kxtf9_get_slave_descr,
		.irq		= 0,	/* not used */
		.adapt_num	= 1,
		.bus		= EXT_SLAVE_BUS_SECONDARY,
		.address	= 0x0F,
		/* Orientation for the Acc.  Part is mounted rotated
		 * 180 degrees from natural orientation.
		 * So X & Y are both negated.
		 */
#if defined(CONFIG_MACH_P8)
		.orientation = {0, 1, 0,
				1, 0, 0,
				0, 0, -1},
#elif defined(CONFIG_MACH_P8LTE)
		.orientation = {0, 1, 0,
				-1, 0, 0,
				0, 0, 1},
#elif defined(CONFIG_MACH_P2)
		.orientation = {1, 0, 0,
				0, -1, 0,
				0, 0, -1},
#elif defined(CONFIG_MACH_P4)
		.orientation = {0, -1, 0,
				-1, 0, 0,
				0, 0, -1},
#else
		/* Rotate Accel Orientation for CL339008 */
		.orientation = {0, -1, 0,
				-1, 0, 0,
				0, 0, -1},
#endif
	},

	.compass = {
		.get_slave_descr = NULL,
		.adapt_num	= 7,	/*bus number 7*/
		.bus		= EXT_SLAVE_BUS_PRIMARY,
		.address	= 0x0C,
		/* Orientation for the Mag.  Part is mounted rotated
		 * 90 degrees clockwise from natural orientation.
		 * So X & Y are swapped and Y & Z are negated.
		 */
		.orientation = {1, 0, 0,
				0, 1, 0,
				0, 0, 1},
	},

};


static void ak8975_init(void)
{
	gpio_request(GPIO_MSENSE_INT, "ak8975_int");
	gpio_direction_input(GPIO_MSENSE_INT);
}

static void mpu3050_init(void)
{
	gpio_request(GPIO_GYRO_INT, "mpu3050_int");
	gpio_direction_input(GPIO_GYRO_INT);
	/* mpu3050_pdata.sec_class = sec_class; */
}

static const struct i2c_board_info i2c_mpu_sensor_board_info[] = {
	{
		I2C_BOARD_INFO("mpu3050", 0x68),
		.irq = IRQ_EINT(0),
		.platform_data = &mpu3050_pdata,
	},
#if 0
	{
		I2C_BOARD_INFO("kxtf9", 0x0F),
	},
#endif
};
#endif   /* CONFIG_MPU_SENSORS_MPU3050 */

static int check_bootmode(void)
{
	return __raw_readl(S5P_INFORM2);
}

static int check_jig_on(void)
{
	return !gpio_get_value(GPIO_IF_CON_SENSE);
}

/* Bluetooth */
#ifdef CONFIG_BT_BCM4330
static struct platform_device bcm4330_bluetooth_device = {
	.name = "bcm4330_bluetooth",
	.id = -1,
};
#endif				/* CONFIG_BT_BCM4330 */

#ifdef CONFIG_BT_CSR8811
static struct platform_device csr8811_bluetooth_device = {
	.name = "csr8811_bluetooth",
	.id = -1,
};
#endif				/* CONFIG_BT_CSR8811 */

#define SYSTEM_REV_SND 0x09

#ifdef CONFIG_SND_SOC_U1_MC1N2
static DEFINE_SPINLOCK(mic_bias_lock);
#ifndef CONFIG_MACH_P8
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
#endif
void sec_set_sub_mic_bias(bool on)
{
#ifdef CONFIG_MACH_P4
	return;
#else
#ifdef CONFIG_SND_SOC_USE_EXTERNAL_MIC_BIAS
#ifdef CONFIG_MACH_P8
		gpio_set_value(GPIO_SUB_MIC_BIAS_EN, on);
#else
		unsigned long flags;
		spin_lock_irqsave(&mic_bias_lock, flags);
		mc1n2_submic_bias = on;
		set_shared_mic_bias();
		spin_unlock_irqrestore(&mic_bias_lock, flags);
#endif
#endif
#endif
}

void sec_set_main_mic_bias(bool on)
{
#ifdef CONFIG_SND_SOC_USE_EXTERNAL_MIC_BIAS

#ifdef CONFIG_MACH_P8
		gpio_set_value(GPIO_MAIN_MIC_BIAS_EN, on);
#else
		unsigned long flags;
		spin_lock_irqsave(&mic_bias_lock, flags);
		mc1n2_mainmic_bias = on;
		set_shared_mic_bias();
		spin_unlock_irqrestore(&mic_bias_lock, flags);
#endif
#endif
}

int sec_set_ldo1_constraints(int disabled)
{
#if 0 /* VADC_3.3V_C210 is always on */
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
#endif
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

#ifdef CONFIG_MACH_P8
	err = gpio_request(GPIO_MAIN_MIC_BIAS_EN, "GPC0");
	if (err) {
		pr_err(KERN_ERR "MAIN_MIC_BIAS_EN GPIO set error!\n");
		return;
	}

	gpio_direction_output(GPIO_MAIN_MIC_BIAS_EN, 1);
	gpio_set_value(GPIO_MAIN_MIC_BIAS_EN, 0);
	gpio_free(GPIO_MAIN_MIC_BIAS_EN);

	err = gpio_request(GPIO_SUB_MIC_BIAS_EN, "GPE1");
	if (err) {
		pr_err(KERN_ERR "GPIO_SUB_MIC_BIAS_EN GPIO set error!\n");
		return;
	}
	gpio_direction_output(GPIO_SUB_MIC_BIAS_EN, 1);
	gpio_set_value(GPIO_SUB_MIC_BIAS_EN, 0);
	gpio_free(GPIO_SUB_MIC_BIAS_EN);

#else
	err = gpio_request(GPIO_MIC_BIAS_EN, "GPE1");
	if (err) {
		pr_err(KERN_ERR "MIC_BIAS_EN GPIO set error!\n");
		return;
	}
	gpio_direction_output(GPIO_MIC_BIAS_EN, 1);
	gpio_set_value(GPIO_MIC_BIAS_EN, 0);
	gpio_free(GPIO_MIC_BIAS_EN);

#endif

	err = gpio_request(GPIO_EAR_MIC_BIAS_EN, "GPE2");
	if (err) {
		pr_err(KERN_ERR "EAR_MIC_BIAS_EN GPIO set error!\n");
		return;
	}
	gpio_direction_output(GPIO_EAR_MIC_BIAS_EN, 1);
	gpio_set_value(GPIO_EAR_MIC_BIAS_EN, 0);
	gpio_free(GPIO_EAR_MIC_BIAS_EN);
#ifndef CONFIG_MACH_PX
	if (system_rev >= SYSTEM_REV_SND) {
		err = gpio_request(GPIO_SUB_MIC_BIAS_EN, "submic_bias");
		if (err) {
			pr_err(KERN_ERR "SUB_MIC_BIAS_EN GPIO set error!\n");
			return;
		}
		gpio_direction_output(GPIO_SUB_MIC_BIAS_EN, 0);
		gpio_free(GPIO_SUB_MIC_BIAS_EN);
	}
#endif
#endif
}
#endif

/* IR_LED */
#ifdef CONFIG_IR_REMOCON_GPIO

static struct platform_device ir_remote_device = {
	.name = "ir_rc",
	.id = 0,
	.dev = {
	},
};

#if defined(CONFIG_MACH_P2) || defined(CONFIG_MACH_P4)
static void ir_rc_init_hw(void)
{
	s3c_gpio_cfgpin(GPIO_IRDA_CONTROL, S3C_GPIO_OUTPUT);
	s3c_gpio_setpull(GPIO_IRDA_CONTROL, S3C_GPIO_PULL_NONE);
	gpio_set_value(GPIO_IRDA_CONTROL, 0);
}
#endif

#if defined(CONFIG_MACH_P8LTE) || defined(CONFIG_MACH_P8)
static void ir_rc_init_hw(void)
{
	s3c_gpio_cfgpin(GPIO_IRDA_nINT, S3C_GPIO_OUTPUT);
	s3c_gpio_setpull(GPIO_IRDA_nINT, S3C_GPIO_PULL_NONE);
	gpio_set_value(GPIO_IRDA_nINT, 0);

	s3c_gpio_cfgpin(GPIO_IRDA_EN, S3C_GPIO_OUTPUT);
	s3c_gpio_setpull(GPIO_IRDA_EN, S3C_GPIO_PULL_NONE);
	gpio_set_value(GPIO_IRDA_EN, 0);
}
#endif

#endif
/* IR_LED */

#ifdef CONFIG_BATTERY_SAMSUNG
static struct platform_device samsung_device_battery = {
	.name = "samsung-fake-battery",
	.id = -1,
};
#endif



#if defined(CONFIG_SEC_THERMISTOR)
static struct sec_therm_platform_data sec_therm_pdata = {
	.adc_channel	= 7,
	.adc_arr_size	= ARRAY_SIZE(adc_temp_table),
	.adc_table	= adc_temp_table,
	.polling_interval = 60 * 1000, /* msecs */
};

static struct platform_device sec_device_thermistor = {
	.name = "sec-thermistor",
	.id = -1,
	.dev.platform_data = &sec_therm_pdata,
};
#endif /* CONFIG_SEC_THERMISTOR */

#ifdef CONFIG_KEYBOARD_GPIO
#define GPIO_KEYS(_code, _gpio, _active_low, _iswake, _hook)		\
{					\
	.code = _code,			\
	.gpio = _gpio,	\
	.active_low = _active_low,		\
	.type = EV_KEY,			\
	.wakeup = _iswake,		\
	.debounce_interval = 10,	\
	.isr_hook = _hook			\
}

struct gpio_keys_button px_buttons[] = {
	GPIO_KEYS(KEY_VOLUMEUP, GPIO_VOL_UP,
		1, 0, sec_debug_check_crash_key),
	GPIO_KEYS(KEY_VOLUMEDOWN, GPIO_VOL_DOWN,
		1, 0, sec_debug_check_crash_key),
	GPIO_KEYS(KEY_POWER, GPIO_nPOWER,
		1, 1, sec_debug_check_crash_key),
};

struct gpio_keys_platform_data px_keys_platform_data = {
	.buttons	= px_buttons,
	.nbuttons	 = ARRAY_SIZE(px_buttons),
};

struct platform_device px_gpio_keys = {
	.name	= "gpio-keys",
	.dev.platform_data = &px_keys_platform_data,
};
#endif

#ifdef CONFIG_SEC_DEV_JACK
static void sec_set_jack_micbias(bool on)
{
#ifdef CONFIG_MACH_P8
		gpio_set_value(GPIO_EAR_MIC_BIAS_EN, on);
#else
	if (system_rev >= 3)
		gpio_set_value(GPIO_EAR_MIC_BIAS_EN, on);
	else
		gpio_set_value(GPIO_MIC_BIAS_EN, on);
#endif
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
		.delay_ms = 10,
		.check_count = 5,
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
		.adc_high = 170,
	},
	{
		/* 171 <= adc <= 370, stable zone */
		.code = KEY_VOLUMEUP,
		.adc_low = 171,
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

#ifdef CONFIG_TOUCHSCREEN_MMS152
static struct tsp_callbacks *charger_cbs;

static void sec_charger_melfas_cb(bool en)
{
	if (charger_cbs && charger_cbs->inform_charger)
		charger_cbs->inform_charger(charger_cbs, en);

	printk(KERN_DEBUG "[TSP] %s - %s\n", __func__,
		en ? "on" : "off");
}
static void register_tsp_callbacks(struct tsp_callbacks *cb)
{
	charger_cbs = cb;
}

static void ts_power_on(void)
{
/*	s3c_gpio_cfgpin(GPIO_TSP_RST, S3C_GPIO_OUTPUT);
	s3c_gpio_setpull(GPIO_TSP_RST, S3C_GPIO_PULL_NONE);
*/
	gpio_set_value(GPIO_TSP_RST, GPIO_LEVEL_HIGH);
	msleep(70);
	s3c_gpio_setpull(GPIO_TSP_INT, S3C_GPIO_PULL_NONE);
	s3c_gpio_cfgpin(GPIO_TSP_INT, S3C_GPIO_SFN(0xf));
	pr_info("[TSP] TSP POWER ON\n");
}

static void ts_power_off(void)
{
	s3c_gpio_cfgpin(GPIO_TSP_INT, S3C_GPIO_INPUT);
	s3c_gpio_setpull(GPIO_TSP_INT, S3C_GPIO_PULL_NONE);

/*	s3c_gpio_cfgpin(GPIO_TSP_RST, S3C_GPIO_OUTPUT);
	s3c_gpio_setpull(GPIO_TSP_RST, S3C_GPIO_PULL_NONE);
*/
	gpio_set_value(GPIO_TSP_RST, GPIO_LEVEL_LOW);
	pr_info("[TSP] TSP POWER OFF");
}

static void ts_read_ta_status(bool *ta_status)
{
	*ta_status = is_cable_attached;
}

static void ts_set_touch_i2c(void)
{
	s3c_gpio_cfgpin(GPIO_TSP_SDA, S3C_GPIO_SFN(3));
	s3c_gpio_setpull(GPIO_TSP_SDA, S3C_GPIO_PULL_UP);
	s3c_gpio_cfgpin(GPIO_TSP_SCL, S3C_GPIO_SFN(3));
	s3c_gpio_setpull(GPIO_TSP_SCL, S3C_GPIO_PULL_UP);
	gpio_free(GPIO_TSP_SDA);
	gpio_free(GPIO_TSP_SCL);
}

static void ts_set_touch_i2c_to_gpio(void)
{

	s3c_gpio_cfgpin(GPIO_TSP_SDA, S3C_GPIO_OUTPUT);
	s3c_gpio_setpull(GPIO_TSP_SDA, S3C_GPIO_PULL_UP);
	s3c_gpio_cfgpin(GPIO_TSP_SCL, S3C_GPIO_OUTPUT);
	s3c_gpio_setpull(GPIO_TSP_SCL, S3C_GPIO_PULL_UP);
	gpio_request(GPIO_TSP_SDA, "GPIO_TSP_SDA");
	gpio_request(GPIO_TSP_SCL, "GPIO_TSP_SCL");

}

static struct ts_platform_data ts_data = {
	.gpio_read_done = GPIO_TSP_INT,
	.gpio_int = GPIO_TSP_INT,
	.power_on = ts_power_on,
	.power_off = ts_power_off,
	.register_cb = register_tsp_callbacks,
	.read_ta_status = ts_read_ta_status,
	.set_touch_i2c = ts_set_touch_i2c,
	.set_touch_i2c_to_gpio = ts_set_touch_i2c_to_gpio,
};

#endif /* ifdef CONFIG_TOUCHSCREEN_MMS152 */

#ifdef CONFIG_TOUCHSCREEN_ATMEL_MXT224_U1
static void mxt224_power_on(void)
{
	s3c_gpio_cfgpin(GPIO_TSP_LDO_ON, S3C_GPIO_OUTPUT);
	s3c_gpio_setpull(GPIO_TSP_LDO_ON, S3C_GPIO_PULL_NONE);
	gpio_set_value(GPIO_TSP_LDO_ON, 1);
	mdelay(70);
	s3c_gpio_setpull(GPIO_TSP_INT, S3C_GPIO_PULL_NONE);
	s3c_gpio_cfgpin(GPIO_TSP_INT, S3C_GPIO_SFN(0xf));
	mdelay(40);
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

#if defined(CONFIG_MACH_U1Q1_REV02)
|| defined(CONFIG_MACH_Q1_REV00) || defined(CONFIG_MACH_Q1_REV02)
static u8 t7_config[] = { GEN_POWERCONFIG_T7,
	64, 255, 20
};

static u8 t8_config[] = { GEN_ACQUISITIONCONFIG_T8,
	36, 0, 20, 20, 0, 0, 10, 10, 50, 25
};

static u8 t9_config[] = { TOUCH_MULTITOUCHSCREEN_T9,
	139, 0, 0, 18, 11, 0, 16, MXT224_THRESHOLD, 2, 1, 0, 3, 1,
	0, MXT224_MAX_MT_FINGERS, 10, 10, 10, 31, 3,
	223, 1, 0, 0, 0, 0, 0, 0, 0, 0, 10, 5, 5, 5
};

static u8 t15_config[] = { TOUCH_KEYARRAY_T15,
	131, 16, 11, 2, 1, 0, 0, 40, 3, 0, 0
};

static u8 t18_config[] = { SPT_COMCONFIG_T18,
	0, 0
};

static u8 t48_config[] = { PROCG_NOISESUPPRESSION_T48,
	3, 0, 2, 10, 6, 12, 18, 24, 20, 30, 0, 0, 0, 0,
	0, 0, 0
};

static u8 t46_config[] = { SPT_CTECONFIG_T46,
	0, 2, 0, 0, 0, 0, 0
};
static u8 end_config[] = { RESERVED_T255 };

static const u8 *mxt224_config[] = {
	t7_config,
	t8_config,
	t9_config,
	t15_config,
	t18_config,
	t46_config,
	t48_config,
	end_config,
};
#else
/*
  Configuration for MXT224
*/
static u8 t7_config[] = { GEN_POWERCONFIG_T7,
	48,			/* IDLEACQINT */
	255,			/* ACTVACQINT */
	25			/* ACTV2IDLETO: 25 * 200ms = 5s */
};

static u8 t8_config[] = { GEN_ACQUISITIONCONFIG_T8,
	10, 0, 5, 1, 0, 0, 9, 30
};				/*byte 3: 0 */

static u8 t9_config[] = { TOUCH_MULTITOUCHSCREEN_T9,
	131, 0, 0, 19, 11, 0, 32, MXT224_THRESHOLD, 2, 1,
	0,
	15,			/* MOVHYSTI */
	1, 11, MXT224_MAX_MT_FINGERS, 5, 40, 10, 31, 3,
	223, 1, 0, 0, 0, 0, 143, 55, 143, 90, 18
};

static u8 t18_config[] = { SPT_COMCONFIG_T18,
	0, 1
};

static u8 t20_config[] = { PROCI_GRIPFACESUPPRESSION_T20,
	7, 0, 0, 0, 0, 0, 0, 30, 20, 4, 15, 10
};

static u8 t22_config[] = { PROCG_NOISESUPPRESSION_T22,
	143, 0, 0, 0, 0, 0, 0, 3, 30, 0, 0, 29, 34, 39,
	49, 58, 3
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
#if defined(CONFIG_TARGET_LOCALE_NAATT)
static u8 t7_config_e[] = { GEN_POWERCONFIG_T7,
	48, 255, 25
};

static u8 t8_config_e[] = { GEN_ACQUISITIONCONFIG_T8,
	27, 0, 5, 1, 0, 0, 8, 8, 8, 180
};

/* MXT224E_0V5_CONFIG */
/* NEXTTCHDI added */
static u8 t9_config_e[] = { TOUCH_MULTITOUCHSCREEN_T9,
	139, 0, 0, 19, 11, 0, 32, 50, 2, 1,
	10, 3, 1, 11, MXT224_MAX_MT_FINGERS, 5, 40, 10, 31, 3,
	223, 1, 10, 10, 10, 10, 143, 40, 143, 80,
	18, 15, 50, 50, 2
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
	0, 0, 188, 52, 124, 21, 188, 52, 124, 21, 0, 0, 0, 0
};

static u8 t40_config_e[] = { PROCI_GRIPSUPPRESSION_T40,
	0, 0, 0, 0, 0
};

static u8 t42_config_e[] = { PROCI_TOUCHSUPPRESSION_T42,
	0, 32, 120, 100, 0, 0, 0, 0
};

static u8 t46_config_e[] = { SPT_CTECONFIG_T46,
	0, 3, 16, 35, 0, 0, 1, 0
};

static u8 t47_config_e[] = { PROCI_STYLUS_T47,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

/*MXT224E_0V5_CONFIG */
static u8 t48_config_e[] = { PROCG_NOISESUPPRESSION_T48,
	3, 4, 72, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	6, 6, 0, 0, 100, 4, 64, 10, 0, 20, 5, 0, 38, 0, 5,
	0, 0, 0, 0, 0, 0, 32, 50, 2, 3, 1, 11, 10, 5, 40, 10, 10,
	10, 10, 143, 40, 143, 80, 18, 15, 2
};

static u8 t48_config_e_ta[] = { PROCG_NOISESUPPRESSION_T48,
	1, 4, 88, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	6, 6, 0, 0, 100, 4, 64, 10, 0, 20, 5, 0, 38, 0, 20,
	0, 0, 0, 0, 0, 0, 16, 70, 2, 5, 2, 46, 10, 5, 40, 10, 0,
	10, 10, 143, 40, 143, 80, 18, 15, 2
};

#elif defined(CONFIG_MACH_U1_NA_SPR_EPIC2_REV00)
static u8 t7_config_e[] = { GEN_POWERCONFIG_T7,
	48, 255, 15
};

static u8 t8_config_e[] = { GEN_ACQUISITIONCONFIG_T8,
	27, 0, 5, 1, 0, 0, 4, 35, 40, 55
};

static u8 t9_config_e[] = { TOUCH_MULTITOUCHSCREEN_T9,
	131, 0, 0, 19, 11, 0, 32, 50, 2, 7,
	10, 3, 1, 46, MXT224_MAX_MT_FINGERS, 5, 40, 10, 31, 3,
	223, 1, 10, 10, 10, 10, 143, 40, 143, 80,
	18, 15, 50, 50, 2
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

static u8 t40_config_e[] = { PROCI_GRIPSUPPRESSION_T40,
	0, 0, 0, 0, 0
};

static u8 t42_config_e[] = { PROCI_TOUCHSUPPRESSION_T42,
	0, 32, 120, 100, 0, 0, 0, 0
};

static u8 t46_config_e[] = { SPT_CTECONFIG_T46,
	0, 3, 16, 48, 0, 0, 1, 0, 0
};

static u8 t47_config_e[] = { PROCI_STYLUS_T47,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

static u8 t48_config_e[] = { PROCG_NOISESUPPRESSION_T48,
	3, 4, 64, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 6, 6, 0, 0, 100, 4, 64,
	10, 0, 20, 5, 0, 38, 0, 5, 0, 0,
	0, 0, 0, 0, 32, 50, 2, 3, 1, 46,
	10, 5, 40, 10, 10, 10, 10, 143, 40, 143,
	80, 18, 15, 2
};

static u8 t48_config_e_ta[] = { PROCG_NOISESUPPRESSION_T48,
	1, 4, 80, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 6, 6, 0, 0, 100, 4, 64,
	10, 0, 20, 5, 0, 38, 0, 20, 0, 0,
	0, 0, 0, 0, 16, 70, 2, 5, 2, 46,
	10, 5, 40, 10, 0, 10, 10, 143, 40, 143,
	80, 18, 15, 2
};
#else

static u8 t7_config_e[] = { GEN_POWERCONFIG_T7,
	48,			/* IDLEACQINT */
	255,			/* ACTVACQINT */
	25			/* ACTV2IDLETO: 25 * 200ms = 5s */
};

static u8 t8_config_e[] = { GEN_ACQUISITIONCONFIG_T8,
	27, 0, 5, 1, 0, 0, 4, 35, 40, 55
};

#if 1				/* MXT224E_0V5_CONFIG */
/* NEXTTCHDI added */
#ifdef CONFIG_TARGET_LOCALE_NA
#ifdef CONFIG_MACH_U1_NA_USCC_REV05
static u8 t9_config_e[] = { TOUCH_MULTITOUCHSCREEN_T9,
	139, 0, 0, 19, 11, 0, 32, 50, 2, 1,
	10,
	10,			/* MOVHYSTI */
	1, 46, MXT224_MAX_MT_FINGERS, 5, 40, 10, 31, 3,
	223, 1, 10, 10, 10, 10, 143, 40, 143, 80,
	18, 15, 50, 50, 0
};

#else
static u8 t9_config_e[] = { TOUCH_MULTITOUCHSCREEN_T9,
	139, 0, 0, 19, 11, 0, 32, 50, 2, 1,
	10,
	10,			/* MOVHYSTI */
	1, 46, MXT224_MAX_MT_FINGERS, 5, 40, 10, 31, 3,
	223, 1, 10, 10, 10, 10, 143, 40, 143, 80,
	18, 15, 50, 50, 2
};
#endif
#else
static u8 t9_config_e[] = { TOUCH_MULTITOUCHSCREEN_T9,
	139, 0, 0, 19, 11, 0, 32, 50, 2, 1,
	10,
	15,			/* MOVHYSTI */
	1, 46, MXT224_MAX_MT_FINGERS, 5, 40, 10, 31, 3,
	223, 1, 10, 10, 10, 10, 143, 40, 143, 80,
	18, 15, 50, 50, 0
};
#endif
#else
static u8 t9_config_e[] = { TOUCH_MULTITOUCHSCREEN_T9,
	139, 0, 0, 19, 11, 0, 16, MXT224_THRESHOLD, 2, 1, 10, 3, 1,
	0, MXT224_MAX_MT_FINGERS, 10, 40, 10, 31, 3,
	223, 1, 10, 10, 10, 10, 143, 40, 143, 80, 18, 15, 50, 50
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

#ifdef CONFIG_MACH_U1_NA_USCC_REV05
static u8 t38_config_e[] = { SPT_USERDATA_T38,
	0, 1, 13, 19, 44, 0, 0, 0
};
#else
static u8 t38_config_e[] = { SPT_USERDATA_T38,
	0, 1, 14, 23, 44, 0, 0, 0
};
#endif

static u8 t40_config_e[] = { PROCI_GRIPSUPPRESSION_T40,
	0, 0, 0, 0, 0
};

static u8 t42_config_e[] = { PROCI_TOUCHSUPPRESSION_T42,
	0, 0, 0, 0, 0, 0, 0, 0
};

static u8 t46_config_e[] = { SPT_CTECONFIG_T46,
	0, 3, 16, 40, 0, 0, 1, 0, 0
};

static u8 t47_config_e[] = { PROCI_STYLUS_T47,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

/*MXT224E_0V5_CONFIG */
#ifdef CONFIG_TARGET_LOCALE_NA
#ifdef CONFIG_MACH_U1_NA_USCC_REV05
static u8 t48_config_e_ta[] = { PROCG_NOISESUPPRESSION_T48,
	3, 132, 0x52, 0, 0, 0, 0, 0, 10, 15,
	0, 0, 0, 6, 6, 0, 0, 64, 4, 64,
	10, 0, 10, 5, 0, 19, 0, 20, 0, 0,
	0, 0, 0, 0, 0, 40, 2,	/*blen=0,threshold=50 */
	10,			/* MOVHYSTI */
	1, 47,
	10, 5, 40, 240, 245, 10, 10, 148, 50, 143,
	80, 18, 10, 0
};

static u8 t48_config_e[] = { PROCG_NOISESUPPRESSION_T48,
	3, 132, 0x40, 0, 0, 0, 0, 0, 10, 15,
	0, 0, 0, 6, 6, 0, 0, 64, 4, 64,
	10, 0, 20, 5, 0, 38, 0, 5, 0, 0,	/*byte 27 original value 20 */
	0, 0, 0, 0, 32, MXT224E_THRESHOLD, 2,
	10,
	1, 46,
	MXT224_MAX_MT_FINGERS, 5, 40, 10, 0, 10, 10, 143, 40, 143,
	80, 18, 15, 0
};
#else
static u8 t48_config_e_ta[] = { PROCG_NOISESUPPRESSION_T48,
	1, 4, 0x50, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 6, 6, 0, 0, 100, 4, 64,
	10, 0, 20, 5, 0, 38, 0, 20, 0, 0,
	0, 0, 0, 0, 0, 40, 2,	/*blen=0,threshold=50 */
	10,			/* MOVHYSTI */
	1, 15,
	10, 5, 40, 240, 245, 10, 10, 148, 50, 143,
	80, 18, 10, 2
};

static u8 t48_config_e[] = { PROCG_NOISESUPPRESSION_T48,
	1, 4, 0x40, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 6, 6, 0, 0, 100, 4, 64,
	10, 0, 20, 5, 0, 38, 0, 5, 0, 0,	/*byte 27 original value 20 */
	0, 0, 0, 0, 32, 50, 2,
	10,
	1, 46,
	MXT224_MAX_MT_FINGERS, 5, 40, 10, 0, 10, 10, 143, 40, 143,
	80, 18, 15, 2
};
#endif				/*CONFIG_MACH_U1_NA_USCC_REV05 */
#else
static u8 t48_config_e_ta[] = { PROCG_NOISESUPPRESSION_T48,
	3, 132, 0x52, 0, 0, 0, 0, 0, 10, 15,
	0, 0, 0, 6, 6, 0, 0, 64, 4, 64,
	10, 0, 9, 5, 0, 15, 0, 20, 0, 0,
	0, 0, 0, 0, 0, 40, 2,	/*blen=0,threshold=50 */
	15,			/* MOVHYSTI */
	1, 47,
	10, 5, 40, 235, 235, 10, 10, 160, 50, 143,
	80, 18, 10, 0
};

static u8 t48_config_e[] = { PROCG_NOISESUPPRESSION_T48,
	3, 132, 0x40, 0, 0, 0, 0, 0, 10, 15,
	0, 0, 0, 6, 6, 0, 0, 64, 4, 64,
	10, 0, 20, 5, 0, 38, 0, 5, 0, 0,	/*byte 27 original value 20 */
	0, 0, 0, 0, 32, MXT224E_THRESHOLD, 2,
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
#endif

static struct mxt224_platform_data mxt224_data = {
	.max_finger_touches = MXT224_MAX_MT_FINGERS,
	.gpio_read_done = GPIO_TSP_INT,
#if defined(CONFIG_MACH_U1Q1_REV02)
|| defined(CONFIG_MACH_Q1_REV00) || defined(CONFIG_MACH_Q1_REV02)
	.config = mxt224_config,
#else
	.config = mxt224_config,
	.config_e = mxt224e_config,
	.t48_ta_cfg = t48_config_e_ta,
#endif
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

#endif				/*CONFIG_TOUCHSCREEN_ATMEL_MXT224_U1 */

#if defined(CONFIG_TOUCHSCREEN_MXT540E)

void tsp_register_callback(void *function)
{
	charging_cbs.tsp_set_charging_cable = function;
}

void tsp_read_ta_status(bool *ta_status)
{
	*ta_status = is_cable_attached;
}

static void mxt540e_power_on(void)
{
	s3c_gpio_cfgpin(GPIO_TSP_LDO_ON, S3C_GPIO_OUTPUT);
	s3c_gpio_setpull(GPIO_TSP_LDO_ON, S3C_GPIO_PULL_NONE);
	gpio_set_value(GPIO_TSP_LDO_ON, GPIO_LEVEL_HIGH);
	msleep(MXT540E_HW_RESET_TIME);
	s3c_gpio_setpull(GPIO_TSP_INT, S3C_GPIO_PULL_NONE);
	s3c_gpio_cfgpin(GPIO_TSP_INT, S3C_GPIO_SFN(0xf));
}

static void mxt540e_power_off(void)
{
	s3c_gpio_cfgpin(GPIO_TSP_INT, S3C_GPIO_INPUT);
	s3c_gpio_setpull(GPIO_TSP_INT, S3C_GPIO_PULL_DOWN);

	s3c_gpio_cfgpin(GPIO_TSP_LDO_ON, S3C_GPIO_OUTPUT);
	s3c_gpio_setpull(GPIO_TSP_LDO_ON, S3C_GPIO_PULL_NONE);
	gpio_set_value(GPIO_TSP_LDO_ON, GPIO_LEVEL_LOW);
}

/*
  Configuration for MXT540E
*/
#define MXT540E_MAX_MT_FINGERS		10
#define MXT540E_CHRGTIME_BATT		68
#define MXT540E_CHRGTIME_CHRG		60
#define MXT540E_THRESHOLD_BATT		50
#define MXT540E_THRESHOLD_CHRG		60
#define MXT540E_ACTVSYNCSPERX_BATT		36
#define MXT540E_ACTVSYNCSPERX_CHRG		24
#define MXT540E_CALCFG_BATT		64
#define MXT540E_CALCFG_CHRG		80
#define MXT540E_ATCHFRCCALTHR_WAKEUP		8
#define MXT540E_ATCHFRCCALRATIO_WAKEUP		180
#define MXT540E_ATCHFRCCALTHR_NORMAL		40
#define MXT540E_ATCHFRCCALRATIO_NORMAL		55

static u8 t7_config_e[] = { GEN_POWERCONFIG_T7,
	48, 255, 50
};

static u8 t8_config_e[] = { GEN_ACQUISITIONCONFIG_T8,
	MXT540E_CHRGTIME_BATT, 0, 5, 1, 0, 0, 4, 30,
	MXT540E_ATCHFRCCALTHR_WAKEUP, MXT540E_ATCHFRCCALRATIO_WAKEUP
};

static u8 t9_config_e[] = { TOUCH_MULTITOUCHSCREEN_T9,
	131, 0, 0, 16, 26, 0, 176, MXT540E_THRESHOLD_BATT, 2, 6, 10, 10, 1,
	47, MXT540E_MAX_MT_FINGERS, 5, 20, 20, 31, 3,
	255, 4, 253, 3, 254, 2, 136, 60, 136, 40, 18, 12, 0, 0, 2
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
	0, 0, 24, MXT540E_ACTVSYNCSPERX_BATT, 0, 0, 1, 0
};

static u8 t47_config_e[] = { PROCI_STYLUS_T47,
	0, 30, 60, 15, 2, 20, 20, 150, 0, 32
};

static u8 t48_config_e[] = { PROCG_NOISESUPPRESSION_T48,
	1, 132, MXT540E_CALCFG_BATT, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	6, 6, 0, 0, 100, 4, 64, 10, 0, 20, 6, 0, 46, 0, 1,
	0, 0, 0, 0, 0, 0, 176, MXT540E_THRESHOLD_BATT, 2, 10, 1, 47,
	MXT540E_MAX_MT_FINGERS, 5, 20, 253, 3,
	254, 2, 136, 60, 136, 40, 18, 12, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0
};

static u8 t48_config_chrg_e[] = { PROCG_NOISESUPPRESSION_T48,
	1, 132, MXT540E_CALCFG_CHRG, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	6, 6, 0, 0, 100, 4, 64, 10, 0, 20, 10, 0, 46, 0, 10,
	0, 0, 0, 0, 0, 0, 128, MXT540E_THRESHOLD_CHRG, 2, 10, 1, 0,
	MXT540E_MAX_MT_FINGERS, 5, 20, 240, 240,
	10, 10, 138, 70, 132, 0, 18, 15, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0
};

static u8 t52_config_e[] = { TOUCH_PROXKEY_T52,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
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
	.register_cb = tsp_register_callback,
	.read_ta_status = tsp_read_ta_status,
};
#endif

#if defined(CONFIG_TOUCHSCREEN_MXT768E)

static void ts_power_on(void)
{
	s3c_gpio_cfgpin(GPIO_TSP_LDO_ON, S3C_GPIO_OUTPUT);
	s3c_gpio_setpull(GPIO_TSP_LDO_ON, S3C_GPIO_PULL_NONE);
	gpio_set_value(GPIO_TSP_LDO_ON, GPIO_LEVEL_HIGH);
	msleep(70);
	s3c_gpio_setpull(GPIO_TSP_INT_18V, S3C_GPIO_PULL_NONE);
	s3c_gpio_cfgpin(GPIO_TSP_INT_18V, S3C_GPIO_SFN(0xf));
	msleep(40);
	printk(KERN_DEBUG"mxt_power_on is finished\n");

}

static void ts_power_off(void)
{
	s3c_gpio_cfgpin(GPIO_TSP_INT_18V, S3C_GPIO_INPUT);
	s3c_gpio_setpull(GPIO_TSP_INT_18V, S3C_GPIO_PULL_DOWN);

	s3c_gpio_cfgpin(GPIO_TSP_LDO_ON, S3C_GPIO_OUTPUT);
	s3c_gpio_setpull(GPIO_TSP_LDO_ON, S3C_GPIO_PULL_NONE);
	gpio_set_value(GPIO_TSP_LDO_ON, GPIO_LEVEL_LOW);
	printk(KERN_DEBUG"mxt_power_off is finished\n");
}

static void ts_register_callback(void *function)
{
	printk(KERN_DEBUG"mxt_register_callback\n");
	charging_cbs.tsp_set_charging_cable = function;
}

static void ts_read_ta_status(bool *ta_status)
{
	*ta_status = is_cable_attached;
}
/*
	Configuration for MXT768-E
*/
#define MXT768E_MAX_MT_FINGERS		10
#define MXT768E_CHRGTIME_BATT		64
#define MXT768E_CHRGTIME_CHRG		64
#define MXT768E_THRESHOLD_BATT		30
#define MXT768E_THRESHOLD_CHRG		40
#define MXT768E_CALCFG_BATT		242
#define MXT768E_CALCFG_CHRG		114

#define MXT768E_ATCHCALSTHR_NORMAL			50
#define MXT768E_ATCHFRCCALTHR_NORMAL		50
#define MXT768E_ATCHFRCCALRATIO_NORMAL		0
#define MXT768E_ATCHFRCCALTHR_WAKEUP		8
#define MXT768E_ATCHFRCCALRATIO_WAKEUP		136

#define MXT768E_IDLESYNCSPERX_BATT		21
#define MXT768E_IDLESYNCSPERX_CHRG		38
#define MXT768E_ACTVSYNCSPERX_BATT		21
#define MXT768E_ACTVSYNCSPERX_CHRG		38

#define MXT768E_IDLEACQINT_BATT			255
#define MXT768E_IDLEACQINT_CHRG			24
#define MXT768E_ACTACQINT_BATT			255
#define MXT768E_ACTACQINT_CHRG			255

#define MXT768E_XLOCLIP_BATT		0
#define MXT768E_XLOCLIP_CHRG		12
#define MXT768E_XHICLIP_BATT		0
#define MXT768E_XHICLIP_CHRG		12
#define MXT768E_YLOCLIP_BATT		0
#define MXT768E_YLOCLIP_CHRG		5
#define MXT768E_YHICLIP_BATT		0
#define MXT768E_YHICLIP_CHRG		5
#define MXT768E_XEDGECTRL_BATT		136
#define MXT768E_XEDGECTRL_CHRG		128
#define MXT768E_XEDGEDIST_BATT		50
#define MXT768E_XEDGEDIST_CHRG		0
#define MXT768E_YEDGECTRL_BATT		136
#define MXT768E_YEDGECTRL_CHRG		136
#define MXT768E_YEDGEDIST_BATT		40
#define MXT768E_YEDGEDIST_CHRG		30
#define MXT768E_TCHHYST_BATT			15
#define MXT768E_TCHHYST_CHRG			15


static u8 t7_config_e[] = { GEN_POWERCONFIG_T7,
	MXT768E_IDLEACQINT_BATT, MXT768E_ACTACQINT_BATT, 7
};

static u8 t8_config_e[] = { GEN_ACQUISITIONCONFIG_T8,
	MXT768E_CHRGTIME_BATT, 0, 5, 1, 0, 0, 4,
	MXT768E_ATCHCALSTHR_NORMAL,
	MXT768E_ATCHFRCCALTHR_WAKEUP,
	MXT768E_ATCHFRCCALRATIO_WAKEUP
};

static u8 t9_config_e[] = { TOUCH_MULTITOUCHSCREEN_T9,
	139, 0, 0, 24, 32, 0, 176, MXT768E_THRESHOLD_BATT, 2, 1,
	10, 10, 1, 13, MXT768E_MAX_MT_FINGERS, 20, 40, 20, 31, 3,
	255, 4, MXT768E_XLOCLIP_BATT, MXT768E_XHICLIP_BATT,
	MXT768E_YLOCLIP_BATT, MXT768E_YHICLIP_BATT,
	MXT768E_XEDGECTRL_BATT, MXT768E_XEDGEDIST_BATT,
	MXT768E_YEDGECTRL_BATT, MXT768E_YEDGEDIST_BATT,
	12, MXT768E_TCHHYST_BATT, 43, 51, 0
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

static u8 t25_config_e[] = { SPT_SELFTEST_T25,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

static u8 t40_config_e[] = { PROCI_GRIPSUPPRESSION_T40,
	0, 0, 0, 0, 0
};

static u8 t42_config_e[] = { PROCI_TOUCHSUPPRESSION_T42,
	3/*51*/, 15, 100, 64, 224, 2, 0, 0, 200, 200
};

static u8 t43_config_e[] = { SPT_DIGITIZER_T43,
	0, 0, 0, 0, 0, 0, 0
};

static u8 t46_config_e[] = { SPT_CTECONFIG_T46,
	0, 0, MXT768E_IDLESYNCSPERX_BATT,
	MXT768E_ACTVSYNCSPERX_BATT, 0, 0, 2, 0, 0
};

static u8 t47_config_e[] = { PROCI_STYLUS_T47,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

static u8 t48_config_e[] = {PROCG_NOISESUPPRESSION_T48,
	3, 0, MXT768E_CALCFG_BATT, 60, 0, 0, 0, 0, 0, 0,
	112, 15, 0, 6, 6, 0, 0, 48, 4, 64,
	0, 0, 9, 0, 0, 0, 0, 5, 0, 0,
	0, 0, 0, 0, 112, MXT768E_THRESHOLD_BATT, 2, 16, 2, 81,
	MXT768E_MAX_MT_FINGERS, 20, 40, 250, 250, 5, 5, 143, 50, 136,
	30, 12, MXT768E_TCHHYST_CHRG, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0
};

static u8 t48_config_chrg_e[] = {PROCG_NOISESUPPRESSION_T48,
	3, 0, MXT768E_CALCFG_CHRG, 15, 0, 0, 0, 0, 3, 5,
	96, 20, 0, 6, 6, 0, 0, 48, 4, 64,
	0, 0, 20, 0, 0, 0, 0, 15, 0, 0,
	0, 0, 0, 0, 96, MXT768E_THRESHOLD_CHRG, 2, 10, 5, 81,
	MXT768E_MAX_MT_FINGERS, 20, 40, 251, 251, 6, 6, 144, 50, 136,
	30, 12, MXT768E_TCHHYST_CHRG, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0
};

static u8 t52_config_e[] = { TOUCH_PROXIMITY_KEY_T52,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

static u8 t55_config_e[] = {ADAPTIVE_T55,
	0, 0, 0, 0, 0
};

/* T56 used from 2.0 firmware */
static u8 t56_config_e[] = {PROCI_SHIELDLESS_T56,
	1,  0,  1,  47,  14,  15,  15,  16,  15,  17,
	16,  16,  16,  16,  17,  16,  16,  16,  16,  16,
	16,  16,  15,  15,  14,  13,  12,  14,  0,  48,
	1,  1, 27, 4
};

static u8 t57_config_e[] = {SPT_GENERICDATA_T57,
	131/*0*/, 15, 0
};


static u8 end_config_e[] = { RESERVED_T255 };

static const u8 *mxt768e_config[] = {
	t7_config_e,
	t8_config_e,
	t9_config_e,
	t15_config_e,
	t18_config_e,
	t19_config_e,
	t25_config_e,
	t40_config_e,
	t42_config_e,
	t43_config_e,
	t46_config_e,
	t47_config_e,
	t48_config_e,
	t52_config_e,
	t55_config_e,
	t56_config_e,
	t57_config_e,
	end_config_e,
};

static struct mxt_platform_data mxt_data = {
	.max_finger_touches = MXT768E_MAX_MT_FINGERS,
	.gpio_read_done = GPIO_TSP_INT_18V,
	.config = mxt768e_config,
	.min_x = 0,
	.max_x = 1279,
	.min_y = 0,
	.max_y = 799,
	.min_z = 0,
	.max_z = 255,
	.min_w = 0,
	.max_w = 30,
	.tchthr_batt = MXT768E_THRESHOLD_BATT,
	.tchthr_charging = MXT768E_THRESHOLD_CHRG,
	.calcfg_batt = MXT768E_CALCFG_BATT,
	.calcfg_charging = MXT768E_CALCFG_CHRG,
	.idlesyncsperx_batt = MXT768E_IDLESYNCSPERX_BATT,
	.idlesyncsperx_charging = MXT768E_IDLESYNCSPERX_CHRG,
	.actvsyncsperx_batt = MXT768E_ACTVSYNCSPERX_BATT,
	.actvsyncsperx_charging = MXT768E_ACTVSYNCSPERX_CHRG,
	.xloclip_batt = MXT768E_XLOCLIP_BATT,
	.xloclip_charging = MXT768E_XLOCLIP_CHRG,
	.xhiclip_batt = MXT768E_XHICLIP_BATT,
	.xhiclip_charging = MXT768E_XHICLIP_CHRG,
	.yloclip_batt = MXT768E_YLOCLIP_BATT,
	.yloclip_charging = MXT768E_YLOCLIP_CHRG,
	.yhiclip_batt = MXT768E_YHICLIP_BATT,
	.yhiclip_charging = MXT768E_YHICLIP_CHRG,
	.xedgectrl_batt = MXT768E_XEDGECTRL_BATT,
	.xedgectrl_charging = MXT768E_XEDGECTRL_CHRG,
	.xedgedist_batt = MXT768E_XEDGEDIST_BATT,
	.xedgedist_charging = MXT768E_XEDGEDIST_CHRG,
	.yedgectrl_batt = MXT768E_YEDGECTRL_BATT,
	.yedgectrl_charging = MXT768E_YEDGECTRL_CHRG,
	.yedgedist_batt = MXT768E_YEDGEDIST_BATT,
	.yedgedist_charging = MXT768E_YEDGEDIST_CHRG,
	.t48_config_batt = t48_config_e,
	.t48_config_chrg = t48_config_chrg_e,
	.power_on = ts_power_on,
	.power_off = ts_power_off,
	.register_cb = ts_register_callback,
	.read_ta_status = ts_read_ta_status,
};
#endif

#if defined(CONFIG_TOUCHSCREEN_MXT1386)
static struct mxt_callbacks *charger_callbacks;
static void  sec_mxt1386_charger_infom(bool en)
{
	if (charger_callbacks && charger_callbacks->inform_charger)
		charger_callbacks->inform_charger(charger_callbacks, en);

	printk(KERN_DEBUG "[TSP] %s - %s\n", __func__,
		en ? "on" : "off");
}
static void p3_register_touch_callbacks(struct mxt_callbacks *cb)
{
	charger_callbacks = cb;
}

static void mxt1386_power_on(void)
{
	s3c_gpio_cfgpin(GPIO_TSP_LDO_ON, S3C_GPIO_OUTPUT);
	s3c_gpio_setpull(GPIO_TSP_LDO_ON, S3C_GPIO_PULL_NONE);
	gpio_set_value(GPIO_TSP_LDO_ON, 1);
	s3c_gpio_cfgpin(GPIO_TSP_RST, S3C_GPIO_OUTPUT);
	s3c_gpio_setpull(GPIO_TSP_RST, S3C_GPIO_PULL_NONE);
	gpio_set_value(GPIO_TSP_RST, 1);
	mdelay(70);
	s3c_gpio_setpull(GPIO_TSP_INT, S3C_GPIO_PULL_NONE);
	s3c_gpio_cfgpin(GPIO_TSP_INT, S3C_GPIO_SFN(0xf));
	mdelay(40);
	printk(KERN_ERR "[TSP]mxt1386_power_on is finished\n");
}

static void mxt1386_power_off(void)
{
	s3c_gpio_cfgpin(GPIO_TSP_INT, S3C_GPIO_INPUT);
	s3c_gpio_setpull(GPIO_TSP_INT, S3C_GPIO_PULL_NONE);

	s3c_gpio_cfgpin(GPIO_TSP_RST, S3C_GPIO_OUTPUT);
	s3c_gpio_setpull(GPIO_TSP_RST, S3C_GPIO_PULL_NONE);
	gpio_set_value(GPIO_TSP_RST, 0);

	s3c_gpio_cfgpin(GPIO_TSP_LDO_ON, S3C_GPIO_OUTPUT);
	s3c_gpio_setpull(GPIO_TSP_LDO_ON, S3C_GPIO_PULL_NONE);
	gpio_set_value(GPIO_TSP_LDO_ON, 0);
	/* printk("mxt224_power_off is finished\n"); */
}

static struct mxt_platform_data p4w_touch_platform_data = {
	.numtouch = 10,
	.max_x  = 1280,
	.max_y  = 800,
	.init_platform_hw  = mxt1386_power_on,
	.exit_platform_hw  = mxt1386_power_off,
	.suspend_platform_hw = mxt1386_power_off,
	.resume_platform_hw = mxt1386_power_on,
	.register_cb = p3_register_touch_callbacks,
	/*mxt_power_config*/
	/* Set Idle Acquisition Interval to 32 ms. */
	.power_config.idleacqint = 32,
	.power_config.actvacqint = 255,
	/* Set Active to Idle Timeout to 4 s (one unit = 200ms). */
	.power_config.actv2idleto = 50,
	/*acquisition_config*/
	/* Atmel: 8 -> 10*/
	.acquisition_config.chrgtime = 10,
	.acquisition_config.reserved = 0,
	.acquisition_config.tchdrift = 5,
	/* Atmel: 0 -> 10*/
	.acquisition_config.driftst = 10,
	/* infinite*/
	.acquisition_config.tchautocal = 0,
	/* disabled*/
	.acquisition_config.sync = 0,
#ifdef MXT_CALIBRATE_WORKAROUND
	/*autocal config at wakeup status*/
	.acquisition_config.atchcalst = 9,
	.acquisition_config.atchcalsthr = 48,
	/* Atmel: 50 => 10 : avoid wakeup lockup : 2 or 3 finger*/
	.acquisition_config.atchcalfrcthr = 10,
	.acquisition_config.atchcalfrcratio = 215,
#else
	/* Atmel: 5 -> 0 -> 9  (to avoid ghost touch problem)*/
	.acquisition_config.atchcalst = 9,
	/* Atmel: 50 -> 55 -> 48 ->10 (to avoid ghost touch problem)*/
	.acquisition_config.atchcalsthr = 10,
	/* 50-> 20 (To avoid  wakeup touch lockup)  */
	.acquisition_config.atchcalfrcthr = 20,
	/* 25-> 0  (To avoid  wakeup touch lockup */
	.acquisition_config.atchcalfrcratio = 0,
#endif
	/*multitouch_config*/
	/* enable + message-enable*/
	.touchscreen_config.ctrl = 0x8b,
	.touchscreen_config.xorigin = 0,
	.touchscreen_config.yorigin = 0,
	.touchscreen_config.xsize = 27,
	.touchscreen_config.ysize = 42,
	.touchscreen_config.akscfg = 0,
	/* Atmel: 0x11 -> 0x21 -> 0x11*/
	.touchscreen_config.blen = 0x11,
	/* Atmel: 50 -> 55 -> 48,*/
	.touchscreen_config.tchthr = 48,
	.touchscreen_config.tchdi = 2,
	/* orient : Horizontal flip */
	.touchscreen_config.orient = 1,
	.touchscreen_config.mrgtimeout = 0,
	.touchscreen_config.movhysti = 10,
	.touchscreen_config.movhystn = 1,
	 /* Atmel  0x20 ->0x21 -> 0x2e(-2)*/
	.touchscreen_config.movfilter = 0x50,
	.touchscreen_config.numtouch = MXT_MAX_NUM_TOUCHES,
	.touchscreen_config.mrghyst = 5, /*Atmel 10 -> 5*/
	 /* Atmel 20 -> 5 -> 50 (To avoid One finger Pinch Zoom) */
	.touchscreen_config.mrgthr = 50,
	.touchscreen_config.amphyst = 10,
	.touchscreen_config.xrange = 799,
	.touchscreen_config.yrange = 1279,
	.touchscreen_config.xloclip = 0,
	.touchscreen_config.xhiclip = 0,
	.touchscreen_config.yloclip = 0,
	.touchscreen_config.yhiclip = 0,
	.touchscreen_config.xedgectrl = 0,
	.touchscreen_config.xedgedist = 0,
	.touchscreen_config.yedgectrl = 0,
	.touchscreen_config.yedgedist = 0,
	.touchscreen_config.jumplimit = 18,
	.touchscreen_config.tchhyst = 10,
	.touchscreen_config.xpitch = 1,
	.touchscreen_config.ypitch = 3,
	/*noise_suppression_config*/
	.noise_suppression_config.ctrl = 0x87,
	.noise_suppression_config.reserved = 0,
	.noise_suppression_config.reserved1 = 0,
	.noise_suppression_config.reserved2 = 0,
	.noise_suppression_config.reserved3 = 0,
	.noise_suppression_config.reserved4 = 0,
	.noise_suppression_config.reserved5 = 0,
	.noise_suppression_config.reserved6 = 0,
	.noise_suppression_config.noisethr = 30,
	.noise_suppression_config.reserved7 = 0,/*1;*/
	.noise_suppression_config.freqhopscale = 0,
	.noise_suppression_config.freq[0] = 10,
	.noise_suppression_config.freq[1] = 18,
	.noise_suppression_config.freq[2] = 23,
	.noise_suppression_config.freq[3] = 30,
	.noise_suppression_config.freq[4] = 36,
	.noise_suppression_config.reserved8 = 0, /* 3 -> 0*/
	/*cte_config*/
	.cte_config.ctrl = 0,
	.cte_config.cmd = 0,
	.cte_config.mode = 0,
	/*16 -> 4 -> 8*/
	.cte_config.idlegcafdepth = 8,
	/*63 -> 16 -> 54(16ms sampling)*/
	.cte_config.actvgcafdepth = 54,
	.cte_config.voltage = 0x3c,
	/* (enable + non-locking mode)*/
	.gripsupression_config.ctrl = 0,
	.gripsupression_config.xlogrip = 0, /*10 -> 0*/
	.gripsupression_config.xhigrip = 0, /*10 -> 0*/
	.gripsupression_config.ylogrip = 0, /*10 -> 15*/
	.gripsupression_config.yhigrip = 0,/*10 -> 15*/
	.palmsupression_config.ctrl = 1,
	.palmsupression_config.reserved1 = 0,
	.palmsupression_config.reserved2 = 0,
	/* 40 -> 20(For PalmSuppression detect) */
	.palmsupression_config.largeobjthr = 10,
	/* 5 -> 50(For PalmSuppression detect) */
	.palmsupression_config.distancethr = 50,
	.palmsupression_config.supextto = 5,
	/*config change for ta connected*/
	.idleacqint_for_ta_connect = 255,
	.tchthr_for_ta_connect = 80,
	.noisethr_for_ta_connect = 50,
	.idlegcafdepth_ta_connect = 32,
	.fherr_cnt = 0,
	.fherr_chg_cnt = 10,
	.tch_blen_for_fherr = 0x11,
	.tchthr_for_fherr = 85,
	.noisethr_for_fherr = 50,
	.movefilter_for_fherr = 0x57,
	.jumplimit_for_fherr = 30,
	.freqhopscale_for_fherr = 1,
	.freq_for_fherr1[0] = 10,
	.freq_for_fherr1[1] = 12,
	.freq_for_fherr1[2] = 18,
	.freq_for_fherr1[3] = 40,
	.freq_for_fherr1[4] = 72,
	.freq_for_fherr2[0] = 45,
	.freq_for_fherr2[1] = 49,
	.freq_for_fherr2[2] = 55,
	.freq_for_fherr2[3] = 59,
	.freq_for_fherr2[4] = 63,
	.freq_for_fherr3[0] = 7,
	.freq_for_fherr3[1] = 33,
	.freq_for_fherr3[2] = 39,
	.freq_for_fherr3[3] = 52,
	.freq_for_fherr3[4] = 64,
	.fherr_cnt_no_ta = 0,
	.fherr_chg_cnt_no_ta = 1,
	.tch_blen_for_fherr_no_ta = 0,
	.tchthr_for_fherr_no_ta = 45,
	.movfilter_fherr_no_ta = 0,
	.noisethr_for_fherr_no_ta = 40,
#ifdef MXT_CALIBRATE_WORKAROUND
	/*autocal config at idle status*/
	.atchcalst_idle = 9,
	.atchcalsthr_idle = 10,
	.atchcalfrcthr_idle = 50,
	/* Atmel: 25 => 55 : avoid idle palm on lockup*/
	.atchcalfrcratio_idle = 55,
#endif
};
#endif

#if defined(CONFIG_RMI4_I2C)
static int synaptics_tsp_pre_suspend(const void *pm_data)
{
	printk(KERN_DEBUG "[TSP] %s\n", __func__);
	s3c_gpio_cfgpin(GPIO_TSP_INT, S3C_GPIO_OUTPUT);
	s3c_gpio_setpull(GPIO_TSP_INT, S3C_GPIO_PULL_NONE);
	gpio_set_value(GPIO_TSP_INT, 0);
	s3c_gpio_cfgpin(GPIO_TSP_RST, S3C_GPIO_OUTPUT);
	s3c_gpio_setpull(GPIO_TSP_RST, S3C_GPIO_PULL_NONE);
	gpio_set_value(GPIO_TSP_RST, 0);
	s3c_gpio_cfgpin(GPIO_TSP_LDO_ON, S3C_GPIO_OUTPUT);
	s3c_gpio_setpull(GPIO_TSP_LDO_ON, S3C_GPIO_PULL_NONE);
	gpio_set_value(GPIO_TSP_LDO_ON, 0);

	return 0;
}

static int synaptics_tsp_post_resume(const void *pm_data)
{
	printk(KERN_DEBUG "[TSP] %s\n", __func__);
	s3c_gpio_cfgpin(GPIO_TSP_LDO_ON, S3C_GPIO_OUTPUT);
	s3c_gpio_setpull(GPIO_TSP_LDO_ON, S3C_GPIO_PULL_NONE);
	gpio_set_value(GPIO_TSP_LDO_ON, 1);
	s3c_gpio_cfgpin(GPIO_TSP_RST, S3C_GPIO_OUTPUT);
	s3c_gpio_setpull(GPIO_TSP_RST, S3C_GPIO_PULL_NONE);
	gpio_set_value(GPIO_TSP_RST, 1);
	s3c_gpio_setpull(GPIO_TSP_INT, S3C_GPIO_PULL_NONE);
	s3c_gpio_cfgpin(GPIO_TSP_INT, S3C_GPIO_SFN(0xf));

	return 0;
}
#endif

#ifdef CONFIG_I2C_S3C2410
/* I2C0 */
static struct i2c_board_info i2c_devs0[] __initdata = {
	{I2C_BOARD_INFO("24c128", 0x50),},	/* Samsung S524AD0XD1 */
	{I2C_BOARD_INFO("24c128", 0x52),},	/* Samsung S524AD0XD1 */
};

#ifdef CONFIG_S3C_DEV_I2C1

#ifndef CONFIG_MPU_SENSORS_MPU3050

/* I2C1 */
static struct i2c_board_info i2c_devs1[] __initdata = {
	{
		I2C_BOARD_INFO("k3g", 0x69),
		.irq = IRQ_EINT(1),
	},
	{
		I2C_BOARD_INFO("k3dh", 0x19),
	},
};

#endif /* !CONFIG_MPU_SENSORS_MPU3050 */

#endif /* CONFIG_S3C_DEV_I2C1 */

#ifdef CONFIG_S3C_DEV_I2C2
/* I2C2 */
static struct i2c_board_info i2c_devs2[] __initdata = {
};
#endif
#ifdef CONFIG_S3C_DEV_I2C3
/* I2C3 */
#if defined(CONFIG_TOUCHSCREEN_MXT1386)	\
	|| defined(CONFIG_RMI4_I2C)
#include <plat/regs-iic.h>
static struct s3c2410_platform_i2c i2c3_data __initdata = {
	.flags		= 0,
	.bus_num	= 3,
	.slave_addr	= 0x10,
	.frequency	= 400 * 1000,
	.sda_delay	= S3C2410_IICLC_SDA_DELAY5 | S3C2410_IICLC_FILTER_ON,
};
#endif

#if defined(CONFIG_RMI4_I2C)
#include <linux/rmi.h>
#define SYNAPTICS_RMI_NAME "rmi-i2c"
#define SYNAPTICS_RMI_ADDR 0x20
static struct rmi_device_platform_data synaptics_pdata = {
	.driver_name = "rmi-generic",
	.sensor_name = "s7301",
	.attn_gpio = GPIO_TSP_INT,
	.attn_polarity = RMI_ATTN_ACTIVE_LOW,
	.axis_align = { },
	.pm_data = NULL,
	.pre_suspend = synaptics_tsp_pre_suspend,
	.post_resume = synaptics_tsp_post_resume,
};
#endif	/* CONFIG_RMI4_I2C */

#if defined(CONFIG_TOUCHSCREEN_MXT1386)	\
	&& defined(CONFIG_RMI4_I2C)
static struct i2c_board_info i2c_devs3_mxt[] __initdata = {
	{
		I2C_BOARD_INFO("sec_touchscreen", 0x4c),
		.platform_data = &p4w_touch_platform_data,
	},
};
static struct i2c_board_info i2c_devs3_syn[] __initdata = {
	{
		I2C_BOARD_INFO(SYNAPTICS_RMI_NAME,
			SYNAPTICS_RMI_ADDR),
		.platform_data = &synaptics_pdata,
	},
};

#else	/* defined(CONFIG_TOUCHSCREEN_MXT1386)	\
			&& defined(CONFIG_RMI4_I2C)*/

static struct i2c_board_info i2c_devs3[] __initdata = {
#ifdef CONFIG_TOUCHSCREEN_ATMEL_MXT224_U1
	{
		I2C_BOARD_INFO(MXT224_DEV_NAME, 0x4a),
		.platform_data = &mxt224_data,
	},
#endif
#if defined(CONFIG_TOUCHSCREEN_MXT540E)
	{
		I2C_BOARD_INFO(MXT540E_DEV_NAME, 0x4c),
		.platform_data = &mxt540e_data,
	},
#endif
#if defined(CONFIG_TOUCHSCREEN_MXT768E)
	{
		I2C_BOARD_INFO(MXT_DEV_NAME, 0x4c),
		.platform_data = &mxt_data
	},
#endif
#if defined(CONFIG_TOUCHSCREEN_MMS152)
	{
	    I2C_BOARD_INFO(TS_DEV_NAME, TS_DEV_ADDR),
	    .platform_data	= &ts_data,
	},
#endif
};

#endif	/* defined(CONFIG_TOUCHSCREEN_MXT1386)	\
			&& defined(CONFIG_RMI4_I2C)*/
#endif	/* CONFIG_S3C_DEV_I2C3 */

#ifdef CONFIG_S3C_DEV_I2C4
#ifdef CONFIG_EPEN_WACOM_G5SP
static int p4w_wacom_init_hw(void);
static int p4w_wacom_suspend_hw(void);
static int p4w_wacom_resume_hw(void);
static int p4w_wacom_early_suspend_hw(void);
static int p4w_wacom_late_resume_hw(void);
static int p4w_wacom_reset_hw(void);
static void p4w_wacom_register_callbacks(struct wacom_g5_callbacks *cb);

static struct wacom_g5_platform_data p4w_wacom_platform_data = {
	.x_invert = 0,
	.y_invert = 0,
	.xy_switch = 0,
	.gpio_pendct = GPIO_PEN_PDCT_18V,
	.init_platform_hw = p4w_wacom_init_hw,
	.suspend_platform_hw = p4w_wacom_suspend_hw,
	.resume_platform_hw = p4w_wacom_resume_hw,
	.early_suspend_platform_hw = p4w_wacom_early_suspend_hw,
	.late_resume_platform_hw = p4w_wacom_late_resume_hw,
	.reset_platform_hw = p4w_wacom_reset_hw,
	.register_cb = p4w_wacom_register_callbacks,
};
#endif /* CONFIG_EPEN_WACOM_G5SP */

/* I2C4 */
static struct i2c_board_info i2c_devs4[] __initdata = {
#ifdef CONFIG_EPEN_WACOM_G5SP
	{
		I2C_BOARD_INFO("wacom_g5sp_i2c", 0x56),
		.platform_data = &p4w_wacom_platform_data,
	},
#endif /* CONFIG_EPEN_WACOM_G5SP */
};
#endif

#ifdef CONFIG_EPEN_WACOM_G5SP
static void p4w_wacom_register_callbacks(struct wacom_g5_callbacks *cb)
{
	wacom_callbacks = cb;
};

static int __init p4w_wacom_init(void)
{
	p4w_wacom_init_hw();
	gpio_set_value(GPIO_PEN_LDO_EN, 1);
	printk(KERN_INFO "[E-PEN]: %s.\n", __func__);
	return 0;
}

static int p4w_wacom_init_hw(void)
{
	int ret;
	ret = gpio_request(GPIO_PEN_LDO_EN, "PEN_LDO_EN");
	if (ret) {
		printk(KERN_ERR "[E-PEN]: faile to request gpio(GPIO_PEN_LDO_EN)\n");
		return ret;
	}
	s3c_gpio_cfgpin(GPIO_PEN_LDO_EN, S3C_GPIO_SFN(0x1));
	s3c_gpio_setpull(GPIO_PEN_LDO_EN, S3C_GPIO_PULL_NONE);
	gpio_direction_output(GPIO_PEN_LDO_EN, 0);

	ret = gpio_request(GPIO_PEN_PDCT_18V, "PEN_PDCT");
	if (ret) {
		printk(KERN_ERR "[E-PEN]: faile to request gpio(GPIO_PEN_PDCT_18V)\n");
		return ret;
	}
	s3c_gpio_cfgpin(GPIO_PEN_PDCT_18V, S3C_GPIO_SFN(0x0));
	s3c_gpio_setpull(GPIO_PEN_PDCT_18V, S3C_GPIO_PULL_NONE);
	gpio_direction_input(GPIO_PEN_PDCT_18V);

	ret = gpio_request(GPIO_PEN_SLP_18V, "PEN_SLP");
	if (ret) {
		printk(KERN_ERR "[E-PEN]: faile to request gpio(GPIO_PEN_SLP_18V)\n");
		return ret;
	}
	s3c_gpio_cfgpin(GPIO_PEN_SLP_18V, S3C_GPIO_SFN(0x1));
	s3c_gpio_setpull(GPIO_PEN_SLP_18V, S3C_GPIO_PULL_NONE);
	gpio_direction_output(GPIO_PEN_SLP_18V, 0);

	ret = gpio_request(GPIO_PEN_IRQ_18V, "PEN_IRQ");
	if (ret) {
		printk(KERN_ERR "[E-PEN]: faile to request gpio(GPIO_PEN_IRQ_18V)\n");
		return ret;
	}
	s3c_gpio_cfgpin(GPIO_PEN_IRQ_18V, S3C_GPIO_SFN(0xf));
	s3c_gpio_setpull(GPIO_PEN_IRQ_18V, S3C_GPIO_PULL_DOWN);
	s5p_register_gpio_interrupt(GPIO_PEN_IRQ_18V);
	gpio_direction_input(GPIO_PEN_IRQ_18V);
	i2c_devs4[0].irq = gpio_to_irq(GPIO_PEN_IRQ_18V);
	return 0;
}

static int p4w_wacom_suspend_hw(void)
{
	return p4w_wacom_early_suspend_hw();
}

static int p4w_wacom_resume_hw(void)
{
	return p4w_wacom_late_resume_hw();
}

static int p4w_wacom_early_suspend_hw(void)
{
#if defined(WACOM_SLEEP_WITH_PEN_SLP)
	gpio_set_value(GPIO_PEN_SLP_18V, 1);
#elif defined(WACOM_SLEEP_WITH_PEN_LDO_EN)
	gpio_set_value(GPIO_PEN_LDO_EN, 0);
#endif
	return 0;
}

static int p4w_wacom_late_resume_hw(void)
{
#if defined(WACOM_SLEEP_WITH_PEN_SLP)
	gpio_set_value(GPIO_PEN_SLP_18V, 0);
#elif defined(WACOM_SLEEP_WITH_PEN_LDO_EN)
	gpio_set_value(GPIO_PEN_LDO_EN, 1);
#endif

#if (WACOM_HAVE_RESET_CONTROL == 1)
	msleep(WACOM_DELAY_FOR_RST_RISING);
	gpio_set_value(GPIO_PEN_SLP_18V, 1);
#endif
	return 0;
}
static int p4w_wacom_reset_hw(void)
{

#if (WACOM_HAVE_RESET_CONTROL == 1)
	gpio_set_value(OMAP_GPIO_PEN_RST, 0);
	msleep(200);
	gpio_set_value(OMAP_GPIO_PEN_RST, 1);
#endif
	printk(KERN_INFO "[E-PEN] : wacom warm reset(%d).\n",
		WACOM_HAVE_RESET_CONTROL);
	return 0;
}
#endif /* CONFIG_EPEN_WACOM_G5SP */


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
#endif
static void s3c_i2c7_cfg_gpio_px(struct platform_device *dev)
{
	s3c_gpio_cfgall_range(EXYNOS4_GPD0(2), 2,
		S3C_GPIO_SFN(3), S3C_GPIO_PULL_NONE);
}

struct s3c2410_platform_i2c default_i2c7_data __initdata = {
	.bus_num	= 7,
	.flags		= 0,
	.slave_addr	= 0x10,
	.frequency	= 100*1000,
	.sda_delay	= 100,
	.cfg_gpio	= s3c_i2c7_cfg_gpio_px,
};
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
	{
		I2C_BOARD_INFO("sec_touchkey", 0x20),
	},
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

/* I2C9 */
#ifdef CONFIG_BATTERY_MAX17042_FUELGAUGE_PX
static struct max17042_platform_data max17042_pdata = {
#if defined(CONFIG_MACH_P2)
	.sdi_capacity = 0x1EC8,
	.sdi_vfcapacity = 0x290A,
	.atl_capacity = 0x1FBE,
	.atl_vfcapacity = 0x2A54,
	.sdi_low_bat_comp_start_vol = 3550,
	.atl_low_bat_comp_start_vol = 3450,
	.fuel_alert_line = GPIO_FUEL_ALERT,
#elif defined(CONFIG_MACH_P4)
	.sdi_capacity = 0x3730,
	.sdi_vfcapacity = 0x4996,
	.atl_capacity = 0x3022,
	.atl_vfcapacity = 0x4024,
	.sdi_low_bat_comp_start_vol = 3600,
	.atl_low_bat_comp_start_vol = 3450,
	.fuel_alert_line = GPIO_FUEL_ALERT,
#elif defined(CONFIG_MACH_P8) || defined(CONFIG_MACH_P8LTE)
	.sdi_capacity = 0x2B06,
	.sdi_vfcapacity = 0x395E,
	.atl_capacity = 0x2B06,
	.atl_vfcapacity = 0x395E,
	.sdi_low_bat_comp_start_vol = 3600,
	.atl_low_bat_comp_start_vol = 3450,
	.fuel_alert_line = GPIO_FUEL_ALERT,
#else	/* default value */
	.sdi_capacity = 0x1F40,
	.sdi_vfcapacity = 0x29AC,
	.atl_capacity = 0x1FBE,
	.atl_vfcapacity = 0x2A54,
	.sdi_low_bat_comp_start_vol = 3600,
	.atl_low_bat_comp_start_vol = 3450,
	.fuel_alert_line = GPIO_FUEL_ALERT,
#endif
	.check_jig_status = check_jig_on
};

static struct i2c_board_info i2c_devs9_emul[] __initdata = {
	{
		I2C_BOARD_INFO("fuelgauge", 0x36),
		.platform_data = &max17042_pdata,
	},
};
#else
static struct i2c_board_info i2c_devs9_emul[] __initdata = {
	{
		I2C_BOARD_INFO("max17040", 0x36),
	},
};
#endif
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

#ifdef CONFIG_SENSORS_BH1721FVC
static int light_sensor_init(void)
{
	int err;
	int gpio_vout = GPIO_PS_VOUT;

	#if defined(CONFIG_OPTICAL_WAKE_ENABLE)
	if (system_rev >= 0x03) {
		printk(KERN_INFO" BH1721 Reset GPIO = GPX0(1) (rev%02d)\n", system_rev);
		gpio_vout = GPIO_PS_VOUT_WAKE;
	} else
		printk(KERN_INFO" BH1721 Reset GPIO = GPL0(6) (rev%02d)\n", system_rev);
	#endif

	printk(KERN_INFO"============================\n");
	printk(KERN_INFO"==    BH1721 Light Sensor Init         ==\n");
	printk(KERN_INFO"============================\n");
	printk("%d %d\n", GPIO_PS_ALS_SDA, GPIO_PS_ALS_SCL);
	err = gpio_request(gpio_vout, "LIGHT_SENSOR_RESET");
	if (err) {
		printk(KERN_INFO" bh1721fvc Failed to request the light "
			" sensor gpio (%d)\n", err);
		return err;
	}

	s3c_gpio_cfgpin(gpio_vout, S3C_GPIO_OUTPUT);
	s3c_gpio_setpull(gpio_vout, S3C_GPIO_PULL_NONE);

	err = gpio_direction_output(gpio_vout, 0);
	udelay(2);
	err = gpio_direction_output(gpio_vout, 1);
	if (err) {
		printk(KERN_INFO" bh1721fvc Failed to make the light sensor gpio(reset)"
			" high (%d)\n", err);
		return err;
	}

	return 0;
}

static int  bh1721fvc_light_sensor_reset(void)
{
	int err;
	int gpio_vout = GPIO_PS_VOUT;

	#if defined(CONFIG_OPTICAL_WAKE_ENABLE)
	if (system_rev >= 0x03)
		gpio_vout = GPIO_PS_VOUT_WAKE;
	#endif

	printk(KERN_INFO" bh1721fvc_light_sensor_reset\n");
	err = gpio_direction_output(gpio_vout, 0);
	if (err) {
		printk(KERN_INFO" bh1721fvc Failed to make the light sensor gpio(reset)"
			" low (%d)\n", err);
		return err;
	}

	udelay(2);

	err = gpio_direction_output(gpio_vout, 1);
	if (err) {
		printk(KERN_INFO" bh1721fvc Failed to make the light sensor gpio(reset)"
			" high (%d)\n", err);
		return err;
	}
	return 0;
}

static int  bh1721fvc_light_sensor_output(int value)
{
	int err;
	int gpio_vout = GPIO_PS_VOUT;

	#if defined(CONFIG_OPTICAL_WAKE_ENABLE)
	if (system_rev >= 0x03)
		gpio_vout = GPIO_PS_VOUT_WAKE;
	#endif

	err = gpio_direction_output(gpio_vout, value);
	if (err) {
		printk(KERN_INFO" bh1721fvc Failed to make the light sensor gpio(dvi)"
			" low (%d)\n", err);
		return err;
	}
	return 0;
}

static struct bh1721fvc_platform_data bh1721fvc_pdata = {
	.reset = bh1721fvc_light_sensor_reset,
	/* .output = bh1721fvc_light_sensor_output, */
};

static struct i2c_board_info i2c_bh1721_emul[] __initdata = {
	{
		I2C_BOARD_INFO("bh1721fvc", 0x23),
		.platform_data = &bh1721fvc_pdata,
	},
#if defined(CONFIG_SENSORS_AL3201)
	{
		I2C_BOARD_INFO("AL3201", 0x1c),
	},
#endif
};
#endif

#ifdef CONFIG_OPTICAL_GP2A
static int gp2a_power(bool on)
{
	printk("%s : %d\n", __func__, on);
	return 0;
}


#if defined(CONFIG_OPTICAL_WAKE_ENABLE)

static struct gp2a_platform_data gp2a_wake_pdata = {
	.power = gp2a_power,
	.p_out = GPIO_PS_VOUT_WAKE,
};

static struct i2c_board_info i2c_wake_devs11[] __initdata = {
	{
		I2C_BOARD_INFO("gp2a", (0x88 >> 1)),
		.platform_data = &gp2a_wake_pdata,
	},
};
#endif

static struct gp2a_platform_data gp2a_pdata = {
	.power = gp2a_power,
	.p_out = GPIO_PS_VOUT,
};

static struct i2c_board_info i2c_devs11[] __initdata = {
	{
		I2C_BOARD_INFO("gp2a", (0x88 >> 1)),
		.platform_data = &gp2a_pdata,
	},
};

#endif

#endif /* CONFIG_S3C_DEV_I2C11_EMUL */

/* I2C13 EMUL*/
#ifdef CONFIG_VIDEO_SR200PC20_P2
static struct i2c_gpio_platform_data  i2c13_platdata = {
	.sda_pin		= VT_CAM_SDA_18V,
	.scl_pin		= VT_CAM_SCL_18V,
	.udelay			= 2,    /* 250KHz */
	.sda_is_open_drain	= 0,
	.scl_is_open_drain	= 0,
	.scl_is_output_only	= 0,
};

static struct platform_device s3c_device_i2c13 = {
	.name			= "i2c-gpio",
	.id			= 13,
	.dev.platform_data	= &i2c13_platdata,
};
#endif /* CONFIG_VIDEO_SR200PC20_P2 */

#if defined(CONFIG_MHL_SII9234)
static void sii9234_init(void)
{
	int ret = gpio_request(GPIO_HDMI_EN1, "hdmi_en1");
	if (ret) {
		pr_err("%s: gpio_request() for HDMI_EN1 failed\n", __func__);
		return;
	}
	gpio_direction_output(GPIO_HDMI_EN1, 0);
	if (ret) {
		pr_err("%s: gpio_direction_output() for HDMI_EN1 failed\n",
			__func__);
		return;
	}

	ret = gpio_request(GPIO_MHL_RST, "mhl_rst");
	if (ret) {
		pr_err("%s: gpio_request() for MHL_RST failed\n", __func__);
		return;
	}
	ret = gpio_direction_output(GPIO_MHL_RST, 0);
	if (ret) {
		pr_err("%s: gpio_direction_output() for MHL_RST failed\n",
			__func__);
		return;
	}
}

static void sii9234_hw_reset(void)
{
#if defined(CONFIG_HPD_PULL)
	struct regulator *reg;
	reg = regulator_get(NULL, "hdp_2.8v");
	if (IS_ERR_OR_NULL(reg)) {
		pr_err("%s: failed to get LDO11 regulator\n", __func__);
		return;
	}
#endif
	gpio_set_value(GPIO_MHL_RST, 0);
	gpio_set_value(GPIO_HDMI_EN1, 1);

	usleep_range(5000, 10000);
	gpio_set_value(GPIO_MHL_RST, 1);
#if defined(CONFIG_HPD_PULL)
	regulator_enable(reg);
	regulator_put(reg);
#endif
	printk(KERN_ERR "[MHL]sii9234_hw_reset.\n");
	msleep(30);
}

static void sii9234_hw_off(void)
{
#if defined(CONFIG_HPD_PULL)
	struct regulator *reg;
	reg = regulator_get(NULL, "hdp_2.8v");
	if (IS_ERR_OR_NULL(reg)) {
		pr_err("%s: failed to get LDO11 regulator\n", __func__);
		return;
	}
	regulator_disable(reg);
	regulator_put(reg);
#endif
	gpio_set_value(GPIO_HDMI_EN1, 0);
	gpio_set_value(GPIO_MHL_RST, 0);
	printk(KERN_ERR "[MHL]sii9234_hw_off.\n");
}

struct sii9234_platform_data sii9234_pdata = {
	.hw_reset = sii9234_hw_reset,
	.hw_off = sii9234_hw_off
};
static struct i2c_board_info i2c_devs15[] __initdata = {
	{
		I2C_BOARD_INFO("SII9234", 0x72>>1),
		.platform_data = &sii9234_pdata,
	},
	{
		I2C_BOARD_INFO("SII9234A", 0x7A>>1),
	},
	{
		I2C_BOARD_INFO("SII9234B", 0x92>>1),
	},
	{
		I2C_BOARD_INFO("SII9234C", 0xC8>>1),
	},
};
/* i2c-gpio emulation platform_data */
static struct i2c_gpio_platform_data i2c15_platdata = {
	.sda_pin		= GPIO_AP_SDA_18V,
	.scl_pin		= GPIO_AP_SCL_18V,
	.udelay			= 2,	/* 250 kHz*/
	.sda_is_open_drain	= 0,
	.scl_is_open_drain	= 0,
	.scl_is_output_only	= 0,
};

static struct platform_device s3c_device_i2c15 = {
	.name			= "i2c-gpio",
	.id			= 15,
	.dev.platform_data	= &i2c15_platdata,
};

#endif


#ifdef CONFIG_S3C_DEV_I2C16_EMUL
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
#ifdef CONFIG_FM_SI4709_MODULE
	{
		I2C_BOARD_INFO("Si4709", (0x20 >> 1)),
	},
#endif
};
#endif

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
#if defined(CONFIG_FB_S5P_S6F1202A)
static struct s3cfb_lcd s6f1202a = {
	.width = 1024,
	.height = 600,
	.p_width = 161,
	.p_height = 98,
	.bpp = 24,
	.freq = 59,
	.timing = {
		.h_fp = 142,
		.h_bp = 210,
		.h_sw = 50,
		.v_fp = 10,
		.v_fpe = 1,
		.v_bp = 11,
		.v_bpe = 1,
		.v_sw = 10,
	},
	.polarity = {
		.rise_vclk = 1,
		.inv_hsync = 1,
		.inv_vsync = 1,
		.inv_vden = 0,
	},
};
static int lcd_power_on(struct lcd_device *ld, int enable)
{
	if (enable) {
		/* LVDS_N_SHDN to high*/
		gpio_set_value(GPIO_LVDS_NSHDN, GPIO_LEVEL_HIGH);
		if (lcdtype == 2) /* BOE_NT51008 */
			msleep(200);
		else              /* HYDIS_NT51008 & SMD_S6F1202A02 */
			msleep(300);

	} else {
		/* For backlight hw spec timming(T4) */
		msleep(220);

		/* LVDS_nSHDN low*/
		gpio_set_value(GPIO_LVDS_NSHDN, GPIO_LEVEL_LOW);
		msleep(20);
	}
	return 0;
}
static struct lcd_platform_data p2_lcd_platform_data = {
	.power_on		= lcd_power_on,
};
#endif

#if defined(CONFIG_FB_S5P_S6C1372)
static struct s3cfb_lcd s6c1372 = {
	.width = 1280,
	.height = 800,
	.p_width = 217,
	.p_height = 135,
	.bpp = 24,

	.freq = 60,
	.timing = {
		.h_fp = 18,
		.h_bp = 36,
		.h_sw = 16,
		.v_fp = 4,
		.v_fpe = 1,
		.v_bp = 16,
		.v_bpe = 1,
		.v_sw = 3,
	},

	.polarity = {
		.rise_vclk = 1,
		.inv_hsync = 1,
		.inv_vsync = 1,
		.inv_vden = 0,
	},
};
static int lcd_power_on(struct lcd_device *ld, int enable)
{
	if (enable) {
		gpio_set_value(GPIO_LCD_EN, GPIO_LEVEL_HIGH);
		msleep(40);

		/* LVDS_N_SHDN to high*/
		gpio_set_value(GPIO_LVDS_NSHDN, GPIO_LEVEL_HIGH);
		msleep(300);

		gpio_set_value(GPIO_LED_BACKLIGHT_RESET, GPIO_LEVEL_HIGH);
		mdelay(2);

	} else {
		gpio_set_value(GPIO_LED_BACKLIGHT_RESET, GPIO_LEVEL_LOW);
		msleep(200);

		/* LVDS_nSHDN low*/
		gpio_set_value(GPIO_LVDS_NSHDN, GPIO_LEVEL_LOW);
		msleep(40);

		/* Disable LVDS Panel Power, 1.2, 1.8, display 3.3V */
		gpio_set_value(GPIO_LCD_EN, GPIO_LEVEL_LOW);
		msleep(400);
	}

	return 0;
}

static struct lcd_platform_data p4_lcd_platform_data = {
	.power_on		= lcd_power_on,
};
#endif

#if defined(CONFIG_FB_S5P_S6C1372) || defined(CONFIG_FB_S5P_S6F1202A)
static struct platform_device lcd_s6c1372 = {
	.name   = "s6c1372",
	.id	= -1,
#if defined(CONFIG_FB_S5P_S6F1202A)
	.dev.platform_data = &p2_lcd_platform_data,
#else
	.dev.platform_data = &p4_lcd_platform_data,
#endif
};
static struct s3c_platform_fb fb_platform_data __initdata = {
	.hw_ver		= 0x70,
	.clk_name	= "fimd",
	.nr_wins	= 5,
#ifdef CONFIG_FB_S5P_DEFAULT_WINDOW
	.default_win	= CONFIG_FB_S5P_DEFAULT_WINDOW,
#else
	.default_win	= 0,
#endif
	.swap		= FB_SWAP_HWORD | FB_SWAP_WORD,
#if defined(CONFIG_FB_S5P_S6F1202A)
	.lcd		= &s6f1202a
#endif
#if defined(CONFIG_FB_S5P_S6C1372)
	.lcd		= &s6c1372
#endif
};
#endif
#if defined(CONFIG_BACKLIGHT_PWM)
static struct platform_pwm_backlight_data smdk_backlight_data = {
	.pwm_id  = 1,
	.max_brightness = 255,
	.dft_brightness = 30,
	.pwm_period_ns  = 25000,
};
static struct platform_device smdk_backlight_device = {
	.name      = "backlight",
	.id        = -1,
	.dev        = {
		.parent = &s3c_device_timer[0].dev,
		.platform_data = &smdk_backlight_data,
	},
};
static void __init smdk_backlight_register(void)
{
	int ret;
	if (system_rev < 3)
		smdk_backlight_data.pwm_id = 0;
	ret = platform_device_register(&smdk_backlight_device);
	if (ret)
		printk(KERN_ERR "failed to register backlight device: %d\n",
				ret);
}
#endif
#ifdef CONFIG_FB_S5P_MDNIE
static struct platform_mdnie_data mdnie_data = {
	.display_type	= -1,
#if defined(CONFIG_FB_S5P_S6F1202A)
	.lcd_pd		= &p2_lcd_platform_data,
#elif defined(CONFIG_FB_S5P_S6C1372)
	.lcd_pd		= &p4_lcd_platform_data,
#endif
};
static struct platform_device mdnie_device = {
	.name = "mdnie",
	.id = -1,
	.dev = {
		.parent = &exynos4_device_pd[PD_LCD0].dev,
		.platform_data = &mdnie_data,
	},
};
static void __init mdnie_device_register(void)
{
	int ret;

	mdnie_data.display_type = lcdtype;

	ret = platform_device_register(&mdnie_device);
	if (ret)
		printk(KERN_ERR "failed to register mdnie device: %d\n",
				ret);
}
#endif

#if defined(CONFIG_FB_S5P_S6C1372) || defined(CONFIG_FB_S5P_S6F1202A)
static int lcd_cfg_gpio(void)
{
	return 0;
}

int s6c1372_panel_gpio_init(void)
{
	int ret;

	lcd_cfg_gpio();

	/* GPIO Initialize  for S6C1372 LVDS panel */
	ret = gpio_request(GPIO_LCD_EN, "GPIO_LCD_EN");
	if (ret) {
		pr_err("failed to request LCD_EN GPIO%d\n",
				GPIO_LCD_EN);
		return ret;
	}
	ret = gpio_request(GPIO_LVDS_NSHDN, "GPIO_LVDS_NSHDN");
	if (ret) {
		pr_err("failed to request LVDS GPIO%d\n",
				GPIO_LVDS_NSHDN);
		return ret;
	}

	gpio_direction_output(GPIO_LCD_EN, 1);
	gpio_direction_output(GPIO_LVDS_NSHDN, 1);

	gpio_free(GPIO_LCD_EN);
	gpio_free(GPIO_LVDS_NSHDN);

#ifdef GPIO_LED_BACKLIGHT_RESET
	ret = gpio_request(GPIO_LED_BACKLIGHT_RESET,
				"GPIO_LED_BACKLIGHT_RESET");
	if (ret) {
		pr_err("failed to request LVDS GPIO%d\n",
				GPIO_LED_BACKLIGHT_RESET);
		return ret;
	}
	gpio_direction_output(GPIO_LED_BACKLIGHT_RESET, 1);
	gpio_free(GPIO_LED_BACKLIGHT_RESET);
#endif
	s3cfb_set_platdata(&fb_platform_data);
	return 0;
}
#endif

#ifdef CONFIG_FB_S5P_MIPI_DSIM
#ifdef CONFIG_FB_S5P_S6E8AB0
/* for Geminus based on MIPI-DSI interface */
static struct s3cfb_lcd s6e8ab0 = {
	.name = "s6e8ab0",
	.width = 1280,
	.height = 800,
	.p_width = 165,
	.p_height = 103,
	.bpp = 24,

	.freq = 60,

	/* minumun value is 0 except for wr_act time. */
	.cpu_timing = {
		.cs_setup = 0,
		.wr_setup = 0,
		.wr_act = 1,
		.wr_hold = 0,
	},

	.timing = {
		.h_fp = 128,
		.h_bp = 128,
		.h_sw = 94,
		.v_fp = 13,
		.v_fpe = 1,
		.v_bp = 3,
		.v_bpe = 1,
		.v_sw = 2,
		.cmd_allow_len = 11,    /*v_fp=stable_vfp + cmd_allow_len */
		.stable_vfp = 2,
	},

	.polarity = {
		.rise_vclk = 1,
		.inv_hsync = 0,
		.inv_vsync = 0,
		.inv_vden = 0,
	},
};

static int reset_lcd(void)
{
	int err;

	printk(KERN_INFO "%s\n", __func__);

	err = gpio_request(GPIO_LCD_RST, "MLCD_RST");
	if (err) {
		printk(KERN_ERR "failed to request GPF0[1] for "
				"MLCD_RST control\n");
		return -EPERM;
	}
	gpio_direction_output(GPIO_LCD_RST, 0);

	/* Power Reset */
	gpio_set_value(GPIO_LCD_RST, GPIO_LEVEL_HIGH);
	msleep(5);
	gpio_set_value(GPIO_LCD_RST, GPIO_LEVEL_LOW);
	msleep(5);
	gpio_set_value(GPIO_LCD_RST, GPIO_LEVEL_HIGH);


	/* Release GPIO */
	gpio_free(GPIO_LCD_RST);

	return 0;
}

static void lcd_cfg_gpio(void)
{
	/* MLCD_RST */
	s3c_gpio_cfgpin(GPIO_LCD_RST, S3C_GPIO_OUTPUT);
	s3c_gpio_setpull(GPIO_LCD_RST, S3C_GPIO_PULL_NONE);

	/* MLCD_ON */
	s3c_gpio_cfgpin(GPIO_LCD_LDO_EN, S3C_GPIO_OUTPUT);
	s3c_gpio_setpull(GPIO_LCD_LDO_EN, S3C_GPIO_PULL_NONE);

	/* LCD_EN */
	s3c_gpio_cfgpin(GPIO_LCD_EN, S3C_GPIO_OUTPUT);
	s3c_gpio_setpull(GPIO_LCD_EN, S3C_GPIO_PULL_NONE);

	return;
}

static int lcd_power_on(void *pdev, int enable)
{
	int err;

	printk(KERN_INFO "%s : enable=%d\n", __func__, enable);

	/* Request GPIO */
	err = gpio_request(GPIO_LCD_LDO_EN, "MLCD_ON");
	if (err) {
		printk(KERN_ERR "failed to request GPK1[1] for "
				"MLCD_ON control\n");
		return -EPERM;
	}

	err = gpio_request(GPIO_LCD_EN, "LCD_EN");
	if (err) {
		printk(KERN_ERR "failed to request GPL0[7] for "
				"LCD_EN control\n");
		return -EPERM;
	}

	err = gpio_request(GPIO_LCD_RST, "LCD_RST");
	if (err) {
		printk(KERN_ERR "failed to request GPL0[7] for "
				"LCD_EN control\n");
		return -EPERM;
	}

	if (enable) {
		gpio_set_value(GPIO_LCD_LDO_EN, GPIO_LEVEL_HIGH);
		gpio_set_value(GPIO_LCD_EN, GPIO_LEVEL_HIGH);
	} else {
		gpio_set_value(GPIO_LCD_RST, GPIO_LEVEL_LOW);
		gpio_set_value(GPIO_LCD_LDO_EN, GPIO_LEVEL_LOW);
		gpio_set_value(GPIO_LCD_EN, GPIO_LEVEL_LOW);
		mdelay(10);
	}

	gpio_free(GPIO_LCD_LDO_EN);
	gpio_free(GPIO_LCD_EN);
	gpio_free(GPIO_LCD_RST);

	return 0;
}
#endif

static struct s3c_platform_fb fb_platform_data __initdata = {
	.hw_ver = 0x70,
	.clk_name = "fimd",
	.nr_wins = 5,
#ifdef CONFIG_FB_S5P_DEFAULT_WINDOW
	.default_win	= CONFIG_FB_S5P_DEFAULT_WINDOW,
#else
	.default_win = 0,
#endif
	.swap = FB_SWAP_HWORD | FB_SWAP_WORD,
#ifdef CONFIG_FB_S5P_S6E8AB0
	.lcd = &s6e8ab0
#endif
};

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

#ifdef CONFIG_FB_S5P_S6E8AB0
	dsim_lcd_info->lcd_panel_info = (void *)&s6e8ab0;

	/* 500Mbps */
	dsim_pd->dsim_info->p = 3;
	dsim_pd->dsim_info->m = 125;
	dsim_pd->dsim_info->s = 1;

	dsim_pd->dsim_info->hs_toggle = msecs_to_jiffies(500);
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

		/* HS DC Voltage Level Adjustment [3:0] (1011 : +16%) */
		pdata->phy_tune_mask |= 0xf;
		pdata->phy_tune |= 0xb;

#if defined(CONFIG_MACH_P8LTE)
		/* squelch threshold tune [13:11] (001 : +10%) */
		pdata->phy_tune_mask |= 0x7 << 11;
		pdata->phy_tune |= 0x1 << 11;
#elif defined(CONFIG_TARGET_LOCALE_P2EUR_TEMP)
		/* P2 EUR OPEN */
		/*squelch threshold tune [13:11] (100 : -5%) */
		pdata->phy_tune_mask |= 0x7 << 11;
		pdata->phy_tune |= 0x4 << 11;
#endif
		printk(KERN_DEBUG "usb: %s tune_mask=0x%x, tune=0x%x\n",
			__func__, pdata->phy_tune_mask, pdata->phy_tune);
	}

}
#endif

#if defined(CONFIG_SMB136_CHARGER) || defined(CONFIG_SMB347_CHARGER)
struct smb_charger_callbacks *smb_callbacks;

static void smb_charger_register_callbacks(struct smb_charger_callbacks *ptr)
{
	smb_callbacks = ptr;
}

static void smb_charger_unregister_callbacks(void)
{
	smb_callbacks = NULL;
}

static struct smb_charger_data smb_charger_pdata = {
	.register_callbacks = smb_charger_register_callbacks,
	.unregister_callbacks = smb_charger_unregister_callbacks,
	.enable = GPIO_TA_EN,
	.stat = GPIO_TA_nCHG,
#if defined(CONFIG_MACH_P4)
	.ta_nconnected = GPIO_TA_nCONNECTED,
#else
	.ta_nconnected = 0,
#endif
};

static struct i2c_board_info i2c_devs12_emul[] __initdata = {
	{
#if defined(CONFIG_SMB347_CHARGER)
	 I2C_BOARD_INFO("smb347-charger", 0x0C >> 1),
#else
	 I2C_BOARD_INFO("smb136-charger", 0x9A >> 1),
#endif
	 .platform_data = &smb_charger_pdata,
	 },
};

static void __init smb_gpio_init(void)
{
	s3c_gpio_cfgpin(GPIO_TA_nCHG, S3C_GPIO_SFN(0xf));
	/* external pull up */
	s3c_gpio_setpull(GPIO_TA_nCHG, S3C_GPIO_PULL_NONE);
	i2c_devs12_emul[0].irq = gpio_to_irq(GPIO_TA_nCHG);
}

static struct i2c_gpio_platform_data gpio_i2c_data12 = {
	.sda_pin = GPIO_CHG_SDA,
	.scl_pin = GPIO_CHG_SCL,
};

static struct platform_device s3c_device_i2c12 = {
	.name = "i2c-gpio",
	.id = 12,
	.dev.platform_data = &gpio_i2c_data12,
};

static void sec_bat_set_charging_state(int enable, int cable_status)
{
	if (smb_callbacks && smb_callbacks->set_charging_state)
		smb_callbacks->set_charging_state(enable, cable_status);
}

static int sec_bat_get_charging_state(void)
{
	if (smb_callbacks && smb_callbacks->get_charging_state)
		return smb_callbacks->get_charging_state();
	else
		return 0;
}

static void sec_bat_set_charging_current(int set_current)
{
	if (smb_callbacks && smb_callbacks->set_charging_current)
		smb_callbacks->set_charging_current(set_current);
}

static int sec_bat_get_charging_current(void)
{
	if (smb_callbacks && smb_callbacks->get_charging_current)
		return smb_callbacks->get_charging_current();
	else
		return 0;
}
#endif

#if defined(CONFIG_SMB347_CHARGER)
static int sec_bat_get_charger_is_full(void)
{
	if (smb_callbacks && smb_callbacks->get_charger_is_full)
		return smb_callbacks->get_charger_is_full();
	else
		return 0;
}
#endif

#ifdef CONFIG_BATTERY_SEC_PX
void sec_bat_gpio_init(void)
{

	s3c_gpio_cfgpin(GPIO_TA_nCONNECTED, S3C_GPIO_INPUT);
	s3c_gpio_setpull(GPIO_TA_nCONNECTED, S3C_GPIO_PULL_NONE);

	s3c_gpio_cfgpin(GPIO_TA_nCHG, S3C_GPIO_INPUT);
	s3c_gpio_setpull(GPIO_TA_nCHG, S3C_GPIO_PULL_UP);

	s3c_gpio_cfgpin(GPIO_TA_EN, S3C_GPIO_OUTPUT);
	s3c_gpio_setpull(GPIO_TA_EN, S3C_GPIO_PULL_NONE);
	gpio_set_value(GPIO_TA_EN, 0);

#ifndef CONFIG_MACH_P2
	s3c_gpio_cfgpin(GPIO_CURR_ADJ, S3C_GPIO_OUTPUT);
	s3c_gpio_setpull(GPIO_CURR_ADJ, S3C_GPIO_PULL_NONE);
#else
	gpio_request(GPIO_TA_nCHG, "TA_nCHG");
	s5p_register_gpio_interrupt(GPIO_TA_nCHG);
#endif
	pr_info("BAT : Battery GPIO initialized.\n");
}

static void  sec_charger_cb(int set_cable_type)
{
	struct usb_gadget *gadget = platform_get_drvdata(&s3c_device_usbgadget);
	bool cable_state_to_tsp;
	bool cable_state_to_usb;

	switch (set_cable_type) {
	case CHARGER_USB:
		cable_state_to_tsp = true;
		cable_state_to_usb = true;
		is_cable_attached = true;
		is_usb_lpm_enter = false;
		break;
	case CHARGER_AC:
	case CHARGER_MISC:
		cable_state_to_tsp = true;
		cable_state_to_usb = false;
		is_cable_attached = true;
		is_usb_lpm_enter = true;
		break;
	case CHARGER_BATTERY:
	case CHARGER_DISCHARGE:
	default:
		cable_state_to_tsp = false;
		cable_state_to_usb = false;
		is_cable_attached = false;
		is_usb_lpm_enter = true;
		break;
	}
	pr_info("%s:cable_type=%d,tsp(%d),usb(%d),attached(%d),usblpm(%d)\n",
		__func__, set_cable_type, cable_state_to_tsp,
		cable_state_to_usb, is_cable_attached, is_usb_lpm_enter);

/* Send charger state to TSP. TSP needs cable type what charging or not */
#if defined(CONFIG_TOUCHSCREEN_MMS152)
	sec_charger_melfas_cb(is_cable_attached);
#elif defined(CONFIG_TOUCHSCREEN_MXT1386)
	if (system_rev < 13)
		sec_mxt1386_charger_infom(is_cable_attached);
#else
	if (charging_cbs.tsp_set_charging_cable)
		charging_cbs.tsp_set_charging_cable(is_cable_attached);

#endif

/* Send charger state to px-switch. px-switch needs cable type what USB or not */
	set_usb_connection_state(!is_usb_lpm_enter);

/* Send charger state to USB. USB needs cable type what USB data or not */
	if (gadget) {
		if (cable_state_to_usb)
			usb_gadget_vbus_connect(gadget);
		else
			usb_gadget_vbus_disconnect(gadget);
	}

	pr_info("%s\n", __func__);
}

static struct sec_battery_platform_data sec_battery_platform = {
	.charger = {
		.enable_line = GPIO_TA_EN,
		.connect_line = GPIO_TA_nCONNECTED,
		.fullcharge_line = GPIO_TA_nCHG,
#ifndef CONFIG_MACH_P2
		.currentset_line = GPIO_CURR_ADJ,
#endif
#if defined(CONFIG_MACH_P4)
		.accessory_line = GPIO_ACCESSORY_INT,
#else
		.accessory_line = 0,
#endif
	},
#if defined(CONFIG_SMB136_CHARGER) || defined(CONFIG_SMB347_CHARGER)
	.set_charging_state = sec_bat_set_charging_state,
	.get_charging_state = sec_bat_get_charging_state,
	.set_charging_current = sec_bat_set_charging_current,
	.get_charging_current = sec_bat_get_charging_current,
#endif
#if defined(CONFIG_SMB347_CHARGER)
	.get_charger_is_full = sec_bat_get_charger_is_full,
#endif
	.init_charger_gpio = sec_bat_gpio_init,
	.inform_charger_connection = sec_charger_cb,

#if defined(CONFIG_MACH_P8LTE)
	.temp_high_threshold = 55800,	/* 55.8c */
	.temp_high_recovery = 45700,	/* 45.7c */
	.temp_low_recovery = 2200,		/* 2.2c  */
	.temp_low_threshold = -2000,	/* -2c   */
	.recharge_voltage = 4130,	    /*4.13V  */
#else
	.temp_high_threshold = 50000,	/* 50c */
	.temp_high_recovery = 42000,	/* 42c */
	.temp_low_recovery = 2000,		/* 2c */
	.temp_low_threshold = 0,		/* 0c */
	.recharge_voltage = 4150,	/*4.15V */
#endif

	.charge_duration = 10*60*60,	/* 10 hour */
	.recharge_duration = 1.5*60*60,	/* 1.5 hour */
	.check_lp_charging_boot = check_bootmode,
	.check_jig_status = check_jig_on
};

static struct platform_device sec_battery_device = {
	.name = "sec-battery",
	.id = -1,
	.dev = {
		.platform_data = &sec_battery_platform,
	},
};
#endif /* CONFIG_BATTERY_SEC_PX */

#ifdef CONFIG_USBHUB_USB3503_OTG_CONN
int usb3503_hw_config(void)
{
	s3c_gpio_cfgpin(GPIO_USB_HUB_RST, S3C_GPIO_OUTPUT);
	s3c_gpio_setpull(GPIO_USB_HUB_RST, S3C_GPIO_PULL_NONE);
	gpio_set_value(GPIO_USB_HUB_RST, S3C_GPIO_SETPIN_ZERO);
	s5p_gpio_set_drvstr(GPIO_USB_HUB_RST,
		S5P_GPIO_DRVSTR_LV1); /* need to check drvstr 1 or 2 */

	return 0;
}

int usb3503_reset_n(int val)
{
	gpio_set_value(GPIO_USB_HUB_RST, !!val);

	pr_info("Board : %s = %d\n", __func__,
		gpio_get_value(GPIO_USB_HUB_RST));

	return 0;
}

static int host_port_enable(int port, int enable)
{
	int err;

	pr_info("port(%d) control(%d)\n", port, enable);

	if (enable) {
		err = s5p_ehci_port_control(&s5p_device_ehci, port, 1);
		if (err < 0) {
			pr_err("ERR: port(%d) enable fail\n", port);
			goto exit;
		}
	} else {
		err = s5p_ehci_port_control(&s5p_device_ehci, port, 0);
		if (err < 0) {
			pr_err("ERR: port(%d) enable fail\n", port);
			goto exit;
		}
	}

exit:
	return err;
}

/* I2C17_EMUL */
static struct i2c_gpio_platform_data i2c17_platdata = {
	.sda_pin = GPIO_USB_HUB_I2C_SDA,
	.scl_pin = GPIO_USB_HUB_I2C_SCL,
};

struct platform_device s3c_device_i2c17 = {
	.name = "i2c-gpio",
	.id = 17,
	.dev.platform_data = &i2c17_platdata,
};

struct usb3503_platform_data usb3503_pdata = {
	.init_needed    =  1,
	.es_ver         = 1,
	.inital_mode    = USB_3503_MODE_STANDBY,
	.hw_config      = usb3503_hw_config,
	.reset_n        = usb3503_reset_n,
	.port_enable = host_port_enable,
};

static struct i2c_board_info i2c_devs17_emul[] __initdata = {
	{
		I2C_BOARD_INFO(USB3503_I2C_NAME, 0x08),
		.platform_data  = &usb3503_pdata,
	},
};
#endif


#ifdef CONFIG_30PIN_CONN
static void smdk_accessory_gpio_init(void)
{
	gpio_request(GPIO_ACCESSORY_INT, "accessory");
	s3c_gpio_cfgpin(GPIO_ACCESSORY_INT, S3C_GPIO_SFN(0xf));
	s3c_gpio_setpull(GPIO_ACCESSORY_INT, S3C_GPIO_PULL_UP);
	gpio_direction_input(GPIO_ACCESSORY_INT);

	gpio_request(GPIO_DOCK_INT, "dock");
	s3c_gpio_cfgpin(GPIO_DOCK_INT, S3C_GPIO_SFN(0xf));
	s3c_gpio_setpull(GPIO_DOCK_INT, S3C_GPIO_PULL_NONE);
	gpio_direction_input(GPIO_DOCK_INT);

	gpio_request(GPIO_USB_OTG_EN, "GPIO_USB_OTG_EN");
	s3c_gpio_cfgpin(GPIO_USB_OTG_EN, S3C_GPIO_OUTPUT);
	s3c_gpio_setpull(GPIO_USB_OTG_EN, S3C_GPIO_PULL_NONE);
	gpio_direction_output(GPIO_USB_OTG_EN, false);
	gpio_free(GPIO_USB_OTG_EN);

	gpio_request(GPIO_ACCESSORY_EN, "GPIO_ACCESSORY_EN");
	s3c_gpio_cfgpin(GPIO_ACCESSORY_EN, S3C_GPIO_OUTPUT);
	s3c_gpio_setpull(GPIO_ACCESSORY_EN, S3C_GPIO_PULL_NONE);
	gpio_direction_output(GPIO_ACCESSORY_EN, false);
	gpio_free(GPIO_ACCESSORY_EN);
}

void smdk_accessory_power(u8 token, bool active)
{
	int gpio_acc_en;
	int try_cnt = 0;
	int gpio_acc_5v = 0;
	static bool enable;
	static u8 acc_en_token;

	/*
		token info
		0 : power off,
		1 : Keyboard dock
		2 : USB
	*/
	gpio_acc_en = GPIO_ACCESSORY_EN;
#ifdef CONFIG_MACH_P4
	if (system_rev >= 2)
		gpio_acc_5v = GPIO_ACCESSORY_OUT_5V;
#elif defined(CONFIG_MACH_P2)	/* for checking p2 3g and wifi */
	gpio_acc_5v = GPIO_ACCESSORY_OUT_5V;
#elif defined(CONFIG_MACH_P8LTE)
	if (system_rev >= 2)
		gpio_acc_5v = GPIO_ACCESSORY_OUT_5V;
	/*for checking p8 3g and wifi*/
#elif defined(CONFIG_MACH_P8) || defined(CONFIG_MACH_P8LTE)
if (system_rev >= 4)
		gpio_acc_5v = GPIO_ACCESSORY_OUT_5V;
#endif

	gpio_request(gpio_acc_en, "GPIO_ACCESSORY_EN");
	s3c_gpio_cfgpin(gpio_acc_en, S3C_GPIO_OUTPUT);
	s3c_gpio_setpull(gpio_acc_en, S3C_GPIO_PULL_NONE);

	if (active) {
		if (acc_en_token) {
			pr_info("Board : Keyboard dock is connected.\n");
			gpio_direction_output(gpio_acc_en, 0);
			msleep(100);
		}

		acc_en_token |= (1 << token);
		enable = true;
		gpio_direction_output(gpio_acc_en, 1);
		usleep_range(2000, 2000);

		if (0 != gpio_acc_5v) {
			gpio_request(gpio_acc_5v, "gpio_acc_5v");
			s3c_gpio_cfgpin(gpio_acc_5v, S3C_GPIO_INPUT);
			s3c_gpio_setpull(gpio_acc_5v, S3C_GPIO_PULL_NONE);
			msleep(20);

			/* prevent the overcurrent */
			while (!gpio_get_value(gpio_acc_5v)) {
				gpio_direction_output(gpio_acc_en, 0);
				msleep(20);
				gpio_direction_output(gpio_acc_en, 1);
				if (try_cnt > 10) {
					pr_err("[acc] failed to enable the accessory_en");
					break;
				} else
					try_cnt++;
			}
			gpio_free(gpio_acc_5v);

		} else
			pr_info("[ACC] gpio_acc_5v is not set\n");

	} else {
		if (0 == token) {
			gpio_direction_output(gpio_acc_en, 0);
			enable = false;
		} else {
			acc_en_token &= ~(1 << token);
			if (0 == acc_en_token) {
				gpio_direction_output(gpio_acc_en, 0);
				enable = false;
			}
		}
	}
	gpio_free(gpio_acc_en);
	pr_info("Board : %s (%d,%d) %s\n", __func__,
		token, active, enable ? "on" : "off");
}

static int smdk_get_acc_state(void)
{
	return gpio_get_value(GPIO_DOCK_INT);
}

static int smdk_get_dock_state(void)
{
	return gpio_get_value(GPIO_ACCESSORY_INT);
}

#ifdef CONFIG_SEC_KEYBOARD_DOCK
static struct sec_keyboard_callbacks *keyboard_callbacks;
static int check_sec_keyboard_dock(bool attached)
{
	if (keyboard_callbacks && keyboard_callbacks->check_keyboard_dock)
		return keyboard_callbacks->
			check_keyboard_dock(keyboard_callbacks, attached);
	return 0;
}

/* call 30pin func. from sec_keyboard */
static struct sec_30pin_callbacks *s30pin_callbacks;
static int noti_sec_univ_kbd_dock(unsigned int code)
{
	if (s30pin_callbacks && s30pin_callbacks->noti_univ_kdb_dock)
		return s30pin_callbacks->
			noti_univ_kdb_dock(s30pin_callbacks, code);
	return 0;
}

static void check_uart_path(bool en)
{
	int gpio_uart_sel;
#ifdef CONFIG_MACH_P8LTE
	int gpio_uart_sel2;

	gpio_uart_sel = GPIO_UART_SEL1;
	gpio_uart_sel2 = GPIO_UART_SEL2;
	if (en)
		gpio_direction_output(gpio_uart_sel2, 1);
	else
		gpio_direction_output(gpio_uart_sel2, 0);
	printk(KERN_DEBUG "[Keyboard] uart_sel2 : %d\n",
		gpio_get_value(gpio_uart_sel2));
#else
	gpio_uart_sel = GPIO_UART_SEL;
#endif

	if (en)
		gpio_direction_output(gpio_uart_sel, 1);
	else
		gpio_direction_output(gpio_uart_sel, 0);

	printk(KERN_DEBUG "[Keyboard] uart_sel : %d\n",
		gpio_get_value(gpio_uart_sel));
}

static void sec_30pin_register_cb(struct sec_30pin_callbacks *cb)
{
	 s30pin_callbacks = cb;
}

static void sec_keyboard_register_cb(struct sec_keyboard_callbacks *cb)
{
	keyboard_callbacks = cb;
}

static struct sec_keyboard_platform_data kbd_pdata = {
	.accessory_irq_gpio = GPIO_ACCESSORY_INT,
	.acc_power = smdk_accessory_power,
	.check_uart_path = check_uart_path,
	.register_cb = sec_keyboard_register_cb,
	.noti_univ_kbd_dock = noti_sec_univ_kbd_dock,
	.wakeup_key = NULL,
};

static struct platform_device sec_keyboard = {
	.name	= "sec_keyboard",
	.id	= -1,
	.dev = {
		.platform_data = &kbd_pdata,
	}
};
#endif

#ifdef CONFIG_USB_HOST_NOTIFY
#include <linux/host_notify.h>

static void px_usb_otg_power(int active)
{
	smdk_accessory_power(2, active);
}

struct host_notifier_platform_data host_notifier_pdata = {
	.ndev.name	= "usb_otg",
	.gpio		= GPIO_ACCESSORY_OUT_5V,
	.booster	= px_usb_otg_power,
	.thread_enable = 1,
};

struct platform_device host_notifier_device = {
	.name = "host_notifier",
	.dev.platform_data = &host_notifier_pdata,
};

#ifdef CONFIG_USBHUB_USB3503_OTG_CONN
#define RETRY_CNT_LIMIT 100
#endif

static void px_usb_otg_en(int active)
{
#ifdef CONFIG_USBHUB_USB3503_OTG_CONN
	struct usb_hcd *ehci_hcd = platform_get_drvdata(&s5p_device_ehci);
	int retry_cnt = 1;
#endif

	pr_info("otg %s : %d\n", __func__, active);

	usb_switch_lock();

	if (active) {

#ifdef CONFIG_USB_EHCI_S5P
		pm_runtime_get_sync(&s5p_device_ehci.dev);
#endif
#ifdef CONFIG_USB_OHCI_S5P
		pm_runtime_get_sync(&s5p_device_ohci.dev);
#endif

		usb_switch_set_path(USB_PATH_HOST);
		smdk_accessory_power(2, 1);

		host_notifier_pdata.ndev.mode = NOTIFY_HOST_MODE;
		if (host_notifier_pdata.usbhostd_start)
			host_notifier_pdata.usbhostd_start();
	} else {

#ifdef CONFIG_USB_OHCI_S5P
		pm_runtime_put_sync(&s5p_device_ohci.dev);
#endif
#ifdef CONFIG_USB_EHCI_S5P
		pm_runtime_put_sync(&s5p_device_ehci.dev);
#ifdef CONFIG_USBHUB_USB3503_OTG_CONN
		/* waiting for ehci root hub suspend is done */
		while (ehci_hcd->state != HC_STATE_SUSPENDED) {
			msleep(50);
			if (retry_cnt++ > RETRY_CNT_LIMIT) {
				printk(KERN_ERR "ehci suspend not completed\n");
				break;
			}
		}
#endif
#endif

		usb_switch_clr_path(USB_PATH_HOST);
		if (host_notifier_pdata.usbhostd_stop)
			host_notifier_pdata.usbhostd_stop();
		smdk_accessory_power(2, 0);

	}

	usb_switch_unlock();

#ifdef CONFIG_USBHUB_USB3503_OTG_CONN
	if (!active) {
		host_port_enable(2, 0);
		usb3503_set_mode(USB_3503_MODE_STANDBY);
	}

	gpio_request(GPIO_USB_OTG_EN, "GPIO_USB_OTG_EN");
	gpio_direction_output(GPIO_USB_OTG_EN, active);
	gpio_free(GPIO_USB_OTG_EN);

	if (active) {
		usb3503_set_mode(USB_3503_MODE_HUB);
		host_port_enable(2, 1);
	}
#endif

}
#endif

#ifdef CONFIG_INTERNAL_MODEM_IF
struct platform_device sec_idpram_pm_device = {
	.name	= "idparam_pm",
	.id	= -1,
};
#endif

struct acc_con_platform_data acc_con_pdata = {
	.otg_en = px_usb_otg_en,
	.acc_power = smdk_accessory_power,
	.usb_ldo_en = NULL,
	.get_acc_state = smdk_get_acc_state,
	.get_dock_state = smdk_get_dock_state,
#ifdef CONFIG_SEC_KEYBOARD_DOCK
	.check_keyboard = check_sec_keyboard_dock,
#endif
	.register_cb = sec_30pin_register_cb,
	.accessory_irq_gpio = GPIO_ACCESSORY_INT,
	.dock_irq_gpio = GPIO_DOCK_INT,
#ifdef CONFIG_MHL_SII9234
	.mhl_irq_gpio = GPIO_MHL_INT,
	.hdmi_hpd_gpio = GPIO_HDMI_HPD,
#endif
};
struct platform_device sec_device_connector = {
	.name = "acc_con",
	.id = -1,
	.dev.platform_data = &acc_con_pdata,
};
#endif

/* BUSFREQ to control memory/bus*/
static struct device_domain busfreq;

static struct platform_device exynos4_busfreq = {
	.id = -1,
	.name = "exynos-busfreq",
};
#ifdef CONFIG_SEC_WATCHDOG_RESET
static struct platform_device watchdog_reset_device = {
	.name = "watchdog-reset",
	.id = -1,
};
#endif

#ifdef CONFIG_CORESIGHT_ETM

#define CORESIGHT_PHYS_BASE             0x10880000
#define CORESIGHT_ETB_PHYS_BASE         (CORESIGHT_PHYS_BASE + 0x1000)
#define CORESIGHT_TPIU_PHYS_BASE        (CORESIGHT_PHYS_BASE + 0x3000)
#define CORESIGHT_FUNNEL_PHYS_BASE      (CORESIGHT_PHYS_BASE + 0x4000)
#define CORESIGHT_ETM_PHYS_BASE         (CORESIGHT_PHYS_BASE + 0x1C000)

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
		.end   = CORESIGHT_ETM_PHYS_BASE + (SZ_4K * 2) - 1,
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

	&smdkc210_smsc911x,
#ifdef CONFIG_BATTERY_SAMSUNG
	&samsung_device_battery,
#endif
#ifdef CONFIG_BATTERY_SEC_PX
	&sec_battery_device,
#endif
#ifdef CONFIG_ANDROID_PMEM
	&pmem_device,
	&pmem_gpu1_device,
#endif

#ifdef CONFIG_INTERNAL_MODEM_IF
	&sec_idpram_pm_device,
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
#if defined(CONFIG_VIDEO_SR200PC20_P2)
	&s3c_device_i2c13,
#endif
#if defined(CONFIG_S3C_DEV_I2C14_EMUL)
	&s3c_device_i2c14,
#endif
#if defined(CONFIG_SMB136_CHARGER)
	&s3c_device_i2c12,
#endif
#if defined(CONFIG_MHL_SII9234)
		&s3c_device_i2c15,
#endif
#ifdef CONFIG_S3C_DEV_I2C16_EMUL
	&s3c_device_i2c16,
#endif
#ifdef CONFIG_USBHUB_USB3503_OTG_CONN
	&s3c_device_i2c17,	/* USB HUB */
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

#ifdef CONFIG_MACH_PX
	&p4w_wlan_ar6000_pm_device,
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
#ifdef CONFIG_KEYBOARD_GPIO
	&px_gpio_keys,
#endif
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

#ifdef CONFIG_S3C64XX_DEV_SPI0
	&exynos_device_spi0,
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
#if defined(CONFIG_FB_S5P_S6C1372) || defined(CONFIG_FB_S5P_S6F1202A)
	&lcd_s6c1372,
#endif
#ifdef CONFIG_FB_S5P_MDNIE
/* &mdnie_device,*/
#endif
#ifdef CONFIG_VIDEO_TVOUT
	&s5p_device_tvout,
	&s5p_device_cec,
	&s5p_device_hpd,
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
#if defined CONFIG_USB_OHCI_S5P && !defined CONFIG_LINK_DEVICE_HSIC
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
#ifdef CONFIG_EXYNOS4_SETUP_THERMAL
	&s5p_device_tmu,
#endif
#ifdef CONFIG_BT_BCM4330
	&bcm4330_bluetooth_device,
#endif
#ifdef CONFIG_BT_CSR8811
	&csr8811_bluetooth_device,
#endif
#ifdef CONFIG_S5P_DEV_ACE
	&s5p_device_ace,
#endif
	&exynos4_busfreq,
#ifdef CONFIG_SEC_DEV_JACK
	&sec_device_jack,
#endif
#if (defined(CONFIG_30PIN_CONN) && defined(CONFIG_USB_HOST_NOTIFY))
	&host_notifier_device,
#endif
#if defined(CONFIG_IR_REMOCON_GPIO)
/* IR_LED */
	&ir_remote_device,
/* IR_LED */
#endif
#ifdef CONFIG_30PIN_CONN
	&sec_device_connector,
#ifdef CONFIG_SEC_KEYBOARD_DOCK
	&sec_keyboard,
#endif
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
struct s5p_platform_tmu px_tmu_data __initdata = {
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

#if defined CONFIG_USB_OHCI_S5P && defined CONFIG_LINK_DEVICE_HSIC
static int __init s5p_ohci_device_initcall(void)
{
	return platform_device_register(&s5p_device_ohci);
}
late_initcall(s5p_ohci_device_initcall);
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

static void __init smdkc210_smsc911x_init(void)
{
	u32 cs1;

	/* configure nCS1 width to 16 bits */
	cs1 = __raw_readl(S5P_SROM_BW) &
		~(S5P_SROM_BW__CS_MASK << S5P_SROM_BW__NCS1__SHIFT);
	cs1 |= ((1 << S5P_SROM_BW__DATAWIDTH__SHIFT) |
		(1 << S5P_SROM_BW__WAITENABLE__SHIFT) |
		(1 << S5P_SROM_BW__BYTEENABLE__SHIFT)) <<
		S5P_SROM_BW__NCS1__SHIFT;
	__raw_writel(cs1, S5P_SROM_BW);

	/* set timing for nCS1 suitable for ethernet chip */
	__raw_writel((0x1 << S5P_SROM_BCX__PMC__SHIFT) |
		     (0x9 << S5P_SROM_BCX__TACP__SHIFT) |
		     (0xc << S5P_SROM_BCX__TCAH__SHIFT) |
		     (0x1 << S5P_SROM_BCX__TCOH__SHIFT) |
		     (0x6 << S5P_SROM_BCX__TACC__SHIFT) |
		     (0x1 << S5P_SROM_BCX__TCOS__SHIFT) |
		     (0x1 << S5P_SROM_BCX__TACS__SHIFT), S5P_SROM_BC1);
}

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
#ifdef CONFIG_VIDEO_SAMSUNG_MEMSIZE_FIMC1
		{
			.name = "fimc1",
			.size = CONFIG_VIDEO_SAMSUNG_MEMSIZE_FIMC1 * SZ_1K,
			.start = 0,
		},
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
			.start = 0,
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
		"exynos4210-fimc.0=fimc0;exynos4210-fimc.1=fimc1;exynos4210-fimc.2=fimc2;exynos4210-fimc3=fimc3;"
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
	int gpio_touch_id = 0;

#if !defined(CONFIG_TOUCHSCREEN_MMS152)
	/* TSP_LDO_ON: XMDMADDR_11 */
	gpio = GPIO_TSP_LDO_ON;
	gpio_request(gpio, "TSP_LDO_ON");
	gpio_direction_output(gpio, 1);
	gpio_export(gpio, 0);
#endif

#if defined(CONFIG_TOUCHSCREEN_MXT1386) || defined(CONFIG_TOUCHSCREEN_MMS152) \
	|| defined(CONFIG_RMI4_I2C)
	gpio = GPIO_TSP_RST;
	gpio_request(gpio, "TSP_RST");
	gpio_direction_output(gpio, 1);
	gpio_export(gpio, 0);
#endif

#if defined(CONFIG_MACH_P8) || defined(CONFIG_MACH_P8LTE)
	/* TSP_INT: XMDMADDR_7 */
	gpio = GPIO_TSP_INT_18V;
	gpio_request(gpio, "TSP_INT_18V");
#else
	gpio = GPIO_TSP_INT;
	gpio_request(gpio, "TSP_INT");
#endif
	s3c_gpio_cfgpin(gpio, S3C_GPIO_SFN(0xf));
	s3c_gpio_setpull(gpio, S3C_GPIO_PULL_UP);

#if defined(CONFIG_MACH_P8LTE)
s5p_register_gpio_interrupt(gpio);
#endif

#if defined(CONFIG_TOUCHSCREEN_MXT1386)	\
	&& defined(CONFIG_RMI4_I2C)
	i2c_devs3_mxt[0].irq = gpio_to_irq(gpio);
	i2c_devs3_syn[0].irq = gpio_to_irq(gpio);
#else
	i2c_devs3[0].irq = gpio_to_irq(gpio);
#endif

	printk(KERN_INFO "%s touch irq : %d, system_rev : %d\n",
		__func__, gpio_to_irq(gpio), system_rev);

#if defined(CONFIG_TOUCHSCREEN_MMS152)

	gpio = GPIO_TSP_VENDOR1;
	gpio_request(gpio, "GPIO_TSP_VENDOR1");
	s3c_gpio_cfgpin(gpio, S3C_GPIO_INPUT);
	s3c_gpio_setpull(gpio, S3C_GPIO_PULL_NONE);

	gpio = GPIO_TSP_VENDOR2;
	gpio_request(gpio, "GPIO_TSP_VENDOR2");
	s3c_gpio_cfgpin(gpio, S3C_GPIO_INPUT);
	s3c_gpio_setpull(gpio, S3C_GPIO_PULL_NONE);


	s3c_gpio_cfgpin(GPIO_TSP_RST, S3C_GPIO_OUTPUT);
	s3c_gpio_setpull(GPIO_TSP_RST, S3C_GPIO_PULL_NONE);

	if (system_rev < 3) {
		gpio_touch_id = gpio_get_value(GPIO_TSP_VENDOR1);
	} else {
		gpio_touch_id = gpio_get_value(GPIO_TSP_VENDOR1)
					+ gpio_get_value(GPIO_TSP_VENDOR2)*2;
	}
	printk(KERN_ERR "[TSP] %s : gpio_touch_id = %d, system_rev = %d\n",
			__func__, gpio_touch_id, system_rev);
	ts_data.gpio_touch_id = gpio_touch_id;

#endif

}

static void __init smdkc210_machine_init(void)
{
#ifdef CONFIG_S3C64XX_DEV_SPI0
	struct clk *sclk = NULL;
	struct clk *prnt = NULL;
	struct device *spi0_dev = &exynos_device_spi0.dev;
#endif

	/* initialise the gpios */
#if defined(CONFIG_MACH_P2)
	p2_config_gpio_table();
	exynos4_sleep_gpio_table_set = p2_config_sleep_gpio_table;
#elif defined(CONFIG_MACH_P8)
	p8_config_gpio_table();
	exynos4_sleep_gpio_table_set = p8_config_sleep_gpio_table;
#elif defined(CONFIG_MACH_P8LTE)
	p8lte_config_gpio_table();
	exynos4_sleep_gpio_table_set = p8lte_config_sleep_gpio_table;
#else /* CONFIG_MACH_P4 */
	p4_config_gpio_table();
	exynos4_sleep_gpio_table_set = p4_config_sleep_gpio_table;
#endif

#ifdef CONFIG_MACH_PX
	config_wlan_gpio();
#endif

#if defined(CONFIG_EXYNOS4_DEV_PD) && defined(CONFIG_PM_RUNTIME)
	exynos_pd_disable(&exynos4_device_pd[PD_MFC].dev);
	exynos_pd_disable(&exynos4_device_pd[PD_G3D].dev);
	exynos_pd_disable(&exynos4_device_pd[PD_LCD0].dev);
	exynos_pd_disable(&exynos4_device_pd[PD_LCD1].dev);
	exynos_pd_disable(&exynos4_device_pd[PD_CAM].dev);
	exynos_pd_disable(&exynos4_device_pd[PD_TV].dev);
	exynos_pd_disable(&exynos4_device_pd[PD_GPS].dev);

#elif defined(CONFIG_EXYNOS_DEV_PD)
	/*
	 * These power domains should be always on
	 * without runtime pm support.
	 */
	exynos_pd_enable(&exynos4_device_pd[PD_MFC].dev);
	exynos_pd_enable(&exynos4_device_pd[PD_G3D].dev);
	exynos_pd_enable(&exynos4_device_pd[PD_LCD0].dev);
	exynos_pd_enable(&exynos4_device_pd[PD_LCD1].dev);
	exynos_pd_enable(&exynos4_device_pd[PD_CAM].dev);
	exynos_pd_enable(&exynos4_device_pd[PD_TV].dev);
	exynos_pd_enable(&exynos4_device_pd[PD_GPS].dev);
#endif
#ifdef CONFIG_I2C_S3C2410
	s3c_i2c0_set_platdata(NULL);
	i2c_register_board_info(0, i2c_devs0, ARRAY_SIZE(i2c_devs0));

#ifdef CONFIG_S3C_DEV_I2C1

#ifdef CONFIG_MPU_SENSORS_MPU3050
	ak8975_init();
	mpu3050_init();
	s3c_i2c1_set_platdata(NULL);
	i2c_register_board_info(1, i2c_mpu_sensor_board_info
			, ARRAY_SIZE(i2c_mpu_sensor_board_info));
#else
	s3c_i2c1_set_platdata(NULL);
	i2c_register_board_info(1, i2c_devs1, ARRAY_SIZE(i2c_devs1));
#endif
#endif

#ifdef CONFIG_S3C_DEV_I2C2
	s3c_i2c2_set_platdata(NULL);
	i2c_register_board_info(2, i2c_devs2, ARRAY_SIZE(i2c_devs2));
#endif
#ifdef CONFIG_S3C_DEV_I2C3
	universal_tsp_init();
#if defined(CONFIG_TOUCHSCREEN_MXT1386) \
	&& defined(CONFIG_RMI4_I2C)
	if (system_rev >= 13)
		i2c_register_board_info(3, i2c_devs3_syn,
			ARRAY_SIZE(i2c_devs3_syn));
	else {
		i2c_register_board_info(3, i2c_devs3_mxt,
			ARRAY_SIZE(i2c_devs3_mxt));
		i2c3_data.frequency = 100 * 1000;
	}
	s3c_i2c3_set_platdata(&i2c3_data);
#else
	s3c_i2c3_set_platdata(NULL);
	i2c_register_board_info(3, i2c_devs3, ARRAY_SIZE(i2c_devs3));
#endif
#endif
#ifdef CONFIG_S3C_DEV_I2C4
#ifdef CONFIG_EPEN_WACOM_G5SP
	p4w_wacom_init();
#endif /* CONFIG_EPEN_WACOM_G5SP */
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

#if defined(CONFIG_MACH_P4) || defined(CONFIG_MACH_P2)
#ifdef CONFIG_VIBETONZ
	if (system_rev >= 3)
		max8997_motor.pwm_id = 0;
#endif
#endif

#ifdef CONFIG_S3C_DEV_I2C6
	s3c_i2c6_set_platdata(NULL);
	i2c_register_board_info(6, i2c_devs6, ARRAY_SIZE(i2c_devs6));
#endif
#ifdef CONFIG_S3C_DEV_I2C7
#ifdef CONFIG_VIDEO_TVOUT
	s3c_i2c7_set_platdata(&default_i2c7_data);
#else
	s3c_i2c7_set_platdata(NULL);
#endif
	i2c_register_board_info(7, i2c_devs7, ARRAY_SIZE(i2c_devs7));
#endif
#ifdef CONFIG_SAMSUNG_MHL
	printk(KERN_INFO "%s() register sii9234 driver\n", __func__);

	i2c_register_board_info(15, tuna_i2c15_boardinfo,
			ARRAY_SIZE(tuna_i2c15_boardinfo));
#endif
#ifdef CONFIG_S3C_DEV_I2C8_EMUL
	i2c_register_board_info(8, i2c_devs8_emul, ARRAY_SIZE(i2c_devs8_emul));
	gpio_request(GPIO_3_TOUCH_INT, "sec_touchkey");
	s5p_register_gpio_interrupt(GPIO_3_TOUCH_INT);

#endif
#ifdef CONFIG_S3C_DEV_I2C9_EMUL
	i2c_register_board_info(9, i2c_devs9_emul, ARRAY_SIZE(i2c_devs9_emul));
#endif
#ifdef CONFIG_S3C_DEV_I2C10_EMUL
	i2c_register_board_info(10, i2c_devs10_emul,
				ARRAY_SIZE(i2c_devs10_emul));
#endif
#ifdef CONFIG_S3C_DEV_I2C11_EMUL

#ifdef CONFIG_OPTICAL_GP2A
	#if defined(CONFIG_OPTICAL_WAKE_ENABLE)
	if (system_rev >= 0x03)
		i2c_register_board_info(11, i2c_wake_devs11, ARRAY_SIZE(i2c_wake_devs11));
	else
		i2c_register_board_info(11, i2c_devs11, ARRAY_SIZE(i2c_devs11));
	#else
	/* optical sensor */
	i2c_register_board_info(11, i2c_devs11, ARRAY_SIZE(i2c_devs11));
	#endif
#else
	light_sensor_init();
	i2c_register_board_info(11, i2c_bh1721_emul, ARRAY_SIZE(i2c_bh1721_emul));
#endif
#endif

#ifdef CONFIG_S3C_DEV_I2C14_EMUL
	nfc_setup_gpio();
	i2c_register_board_info(14, i2c_devs14, ARRAY_SIZE(i2c_devs14));
#endif

#if defined(CONFIG_SMB136_CHARGER)
	/* smb charger */
	smb_gpio_init();
	i2c_register_board_info(12, i2c_devs12_emul,
				ARRAY_SIZE(i2c_devs12_emul));
#endif
#if defined(CONFIG_SMB347_CHARGER)
	if (system_rev >= 02) {
		printk(KERN_INFO "%s : Add smb347 charger.\n", __func__);
		/* smb charger */
		smb_gpio_init();
		i2c_register_board_info(12, i2c_devs12_emul,
					ARRAY_SIZE(i2c_devs12_emul));
		platform_device_register(&s3c_device_i2c12);
	}
#endif

	/* I2C13 EMUL */
#if 0 /*defined(CONFIG_VIDEO_SR200PC20) && defined(CONFIG_MACH_P4W_REV01)*/
	if (system_rev < 2)
		platform_device_register(&s3c_device_i2c13);
#endif

#if defined(CONFIG_MHL_SII9234)
	sii9234_init();
	i2c_register_board_info(15, i2c_devs15, ARRAY_SIZE(i2c_devs15));
#endif
#ifdef CONFIG_S3C_DEV_I2C16_EMUL
	i2c_register_board_info(16, i2c_devs16, ARRAY_SIZE(i2c_devs16));
#endif
#ifdef CONFIG_USBHUB_USB3503_OTG_CONN
	i2c_register_board_info(17, i2c_devs17_emul,
		ARRAY_SIZE(i2c_devs17_emul));
#endif
#endif
	smdkc210_smsc911x_init();

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
	s5p_tmu_set_platdata(&px_tmu_data);
#endif
#if defined(CONFIG_VIDEO_MFC5X) || defined(CONFIG_VIDEO_SAMSUNG_S5P_MFC)
#ifdef CONFIG_EXYNOS_DEV_PD
	s5p_device_mfc.dev.parent = &exynos4_device_pd[PD_MFC].dev;
#endif
#endif

#if defined(CONFIG_VIDEO_MFC5X)
	exynos4_mfc_setup_clock(&s5p_device_fimd0.dev, 200 * MHZ);
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
#ifdef CONFIG_SEC_MODEM_P8LTE
	spi_register_board_info(spi0_board_info_lte, ARRAY_SIZE(spi0_board_info_lte));
	modem_p8ltevzw_init();
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
	platform_add_devices(smdkc210_devices, ARRAY_SIZE(smdkc210_devices));

#ifdef CONFIG_BACKLIGHT_PWM
	smdk_backlight_register();
#endif
#if defined(CONFIG_FB_S5P_MDNIE) && defined(CONFIG_MACH_PX)
	mdnie_device_register();
#endif
#ifdef CONFIG_FB_S5P_LD9040
	ld9040_fb_init();
#endif
#if defined(CONFIG_FB_S5P_S6C1372) || defined(CONFIG_FB_S5P_S6F1202A)
	s6c1372_panel_gpio_init();
#endif
#if defined(CONFIG_FB_S5P_MIPI_DSIM)
	mipi_fb_init();
#endif

#ifdef CONFIG_SND_SOC_U1_MC1N2
	u1_sound_init();
#endif

#ifdef CONFIG_30PIN_CONN
	smdk_accessory_gpio_init();
#endif

	exynos_sysmmu_init();


#ifdef CONFIG_SEC_THERMISTOR
	platform_device_register(&sec_device_thermistor);
#endif

#ifdef CONFIG_FB_S3C
	exynos4_fimd0_setup_clock(&s5p_device_fimd0.dev, "mout_mpll",
				  800 * MHZ);
#endif

#ifdef CONFIG_S3C64XX_DEV_SPI0
	sclk = clk_get(spi0_dev, "sclk_spi");
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

	cam_init();
#if defined(CONFIG_IR_REMOCON_GPIO)
/* IR_LED */
	ir_rc_init_hw();
/* IR_LED */
#endif
}

static void __init exynos_init_reserve(void)
{
	sec_debug_magic_init();
}

MACHINE_START(SMDKC210, "SMDK4210")
	/* Maintainer: Kukjin Kim <kgene.kim@samsung.com> */
	.boot_params	= S5P_PA_SDRAM + 0x100,
	.init_irq	= exynos4_init_irq,
	.map_io		= smdkc210_map_io,
	.init_machine	= smdkc210_machine_init,
	.timer		= &exynos4_timer,
	.init_early	= &exynos_init_reserve,
MACHINE_END
