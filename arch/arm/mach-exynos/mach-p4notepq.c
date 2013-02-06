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

#ifdef CONFIG_BATTERY_MAX17042_FUELGAUGE_PX
#include <linux/power/max17042_fuelgauge_px.h>
#endif
#ifdef CONFIG_SMB347_CHARGER
#include <linux/power/smb347_charger.h>
#endif
#ifdef CONFIG_BATTERY_SEC_PX
#include <linux/power/sec_battery_px.h>
#endif
#include <linux/power_supply.h>
#ifdef CONFIG_STMPE811_ADC
#include <linux/stmpe811-adc.h>
#endif
#include <linux/v4l2-mediabus.h>
#include <linux/memblock.h>
#include <linux/delay.h>
#include <linux/bootmem.h>

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

#include <mach/p4-input.h>

#include <mach/midas-power.h>
#ifdef CONFIG_SEC_THERMISTOR
#include <mach/sec_thermistor.h>
#endif
#include <mach/midas-thermistor.h>
#include <mach/midas-tsp.h>
#include <mach/regs-clock.h>

#include <mach/midas-lcd.h>
#include <mach/midas-sound.h>
#if defined(CONFIG_SEC_DEV_JACK)
#include <mach/p4note-jack.h>
#endif
#ifdef CONFIG_USB_HOST_NOTIFY
#include <linux/host_notify.h>
#include <linux/pm_runtime.h>
#include <linux/usb.h>
#include <linux/usb/hcd.h>
#include <mach/usb_switch.h>
#endif

#if defined(CONFIG_PHONE_IPC_SPI)
#include <linux/phone_svn/ipc_spi.h>
#include <linux/irq.h>
#endif

#ifdef CONFIG_30PIN_CONN
#include <linux/30pin_con.h>
#endif

#ifdef CONFIG_MOTOR_DRV_ISA1200
#include <linux/isa1200_vibrator.h>
#endif

#include "board-mobile.h"

#ifdef CONFIG_IR_REMOCON_MC96
#include <linux/ir_remote_con_mc96.h>
#endif

extern int s6c1372_panel_gpio_init(void);

/* cable state */
bool is_cable_attached;
bool is_usb_lpm_enter;

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
				S5PV210_UFCON_TXTRIG8 |  \
				S5PV210_UFCON_RXTRIG32)

static struct s3c2410_uartcfg smdk4212_uartcfgs[] __initdata = {
	[0] = {
		.hwport		= 0,
		.flags		= 0,
		.ucon		= SMDK4212_UCON_DEFAULT,
		.ulcon		= SMDK4212_ULCON_DEFAULT,
		.ufcon		= SMDK4212_UFCON_DEFAULT,
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
		.fb_delay = 0x2,
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

#if defined(CONFIG_PHONE_IPC_SPI) \
	|| defined(CONFIG_TDMB) || defined(CONFIG_TDMB_MODULE)
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
#else
	{
		.modalias = "ipc_spi",
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


#if defined(CONFIG_TDMB) || defined(CONFIG_TDMB_MODULE)
static void tdmb_set_config_poweron(void)
{
	s3c_gpio_cfgpin(GPIO_TDMB_EN, S3C_GPIO_OUTPUT);
	s3c_gpio_setpull(GPIO_TDMB_EN, S3C_GPIO_PULL_NONE);
	gpio_set_value(GPIO_TDMB_EN, GPIO_LEVEL_LOW);

	s3c_gpio_cfgpin(GPIO_TDMB_INT, S3C_GPIO_SFN(GPIO_TDMB_INT_AF));
	s3c_gpio_setpull(GPIO_TDMB_INT, S3C_GPIO_PULL_NONE);
}
static void tdmb_set_config_poweroff(void)
{
	s3c_gpio_cfgpin(GPIO_TDMB_EN, S3C_GPIO_OUTPUT);
	s3c_gpio_setpull(GPIO_TDMB_EN, S3C_GPIO_PULL_NONE);
	gpio_set_value(GPIO_TDMB_EN, GPIO_LEVEL_LOW);

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
}

static void tdmb_gpio_off(void)
{
	printk(KERN_DEBUG "tdmb_gpio_off\n");

	tdmb_set_config_poweroff();

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
	gpio_set_value(GPIO_CAM_SW_EN, 1);

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
	gpio_direction_output(GPIO_TORCH_EN, 1);
	err = gpio_request(GPIO_TORCH_SET, "TORCH_SET");
	if (err) {
		printk(KERN_ERR "failed to request TORCH_SET\n");
		gpio_free(GPIO_TORCH_EN);
		return -EPERM;
	}
	gpio_direction_output(GPIO_TORCH_SET, 1);
	gpio_set_value(GPIO_TORCH_EN, 0);
	gpio_set_value(GPIO_TORCH_SET, 0);

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
	.host_caps2		= MMC_CAP2_PACKED_CMD,
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
		unsigned int newluns = 0;
		unsigned int cdfs = 1;

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
	defined(CONFIG_MACH_C1_KOR_LGT)
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

/* I2C0 */
static struct i2c_board_info i2c_devs0[] __initdata = {
};

/* I2C1 */
static struct i2c_board_info i2c_devs1[] __initdata = {
};

#ifdef CONFIG_S3C_DEV_I2C5
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
#endif

static struct i2c_board_info i2c_devs7[] __initdata = {
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
};

/* Bluetooth */
#ifdef CONFIG_BT_BCM4334
static struct platform_device bcm4334_bluetooth_device = {
	.name = "bcm4334_bluetooth",
	.id = -1,
};
#endif

/* I2C9 */
static struct i2c_board_info i2c_devs9_emul[] __initdata = {
};

/* I2C10 */
static struct i2c_board_info i2c_devs10_emul[] __initdata = {
};

/* I2C11 */
static struct i2c_board_info i2c_devs11_emul[] __initdata = {
};

#ifdef CONFIG_SMB347_CHARGER
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
	.ta_nconnected = GPIO_TA_nCONNECTED,
};

/* I2C13 */
static struct i2c_board_info i2c_devs13_emul[] __initdata = {
	{
		I2C_BOARD_INFO("smb347-charger", 0x0C >> 1),
		.platform_data = &smb_charger_pdata,
	},
};

static void __init smb_gpio_init(void)
{
	s3c_gpio_cfgpin(GPIO_TA_nCHG, S3C_GPIO_SFN(0xf));
	/* external pull up */
	s3c_gpio_setpull(GPIO_TA_nCHG, S3C_GPIO_PULL_NONE);
	i2c_devs13_emul[0].irq = gpio_to_irq(GPIO_TA_nCHG);
}

static struct i2c_gpio_platform_data gpio_i2c_data13 = {
	.sda_pin = GPIO_CHG_SDA,
	.scl_pin = GPIO_CHG_SCL,
};

struct platform_device s3c_device_i2c13 = {
	.name = "i2c-gpio",
	.id = 13,
	.dev.platform_data = &gpio_i2c_data13,
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

static int sec_bat_get_charger_is_full(void)
{
	if (smb_callbacks && smb_callbacks->get_charger_is_full)
		return smb_callbacks->get_charger_is_full();
	else
		return 0;
}

static int sec_bat_get_aicl_current(void)
{
	if (smb_callbacks && smb_callbacks->get_aicl_current)
		return smb_callbacks->get_aicl_current();
	else
		return 0;
}

static int sec_bat_get_input_current(void)
{
	if (smb_callbacks && smb_callbacks->get_input_current)
		return smb_callbacks->get_input_current();
	else
		return 0;

}

#endif

static int check_bootmode(void)
{
	int inform2;

	inform2 = __raw_readl(S5P_INFORM2);
	if (inform2 == 0x1)
		return 1;
	else
		return 0;
}

static int check_jig_on(void)
{
	/* check GPIO_IF_CON_SENSE pin */
	if (system_rev >= 1)
		return !gpio_get_value(GPIO_IF_CON_SENSE);
	else
		return 0;
}

/* I2C14 */
#ifdef CONFIG_BATTERY_MAX17042_FUELGAUGE_PX
static struct i2c_gpio_platform_data gpio_i2c_data14 = {
	.sda_pin = GPIO_FUEL_SDA,
	.scl_pin = GPIO_FUEL_SCL,
};

struct platform_device s3c_device_i2c14 = {
	.name = "i2c-gpio",
	.id = 14,
	.dev.platform_data = &gpio_i2c_data14,
};

static struct max17042_platform_data max17042_pdata = {
	.sdi_capacity = 0x3730,
	.sdi_vfcapacity = 0x4996,
	.sdi_low_bat_comp_start_vol = 3600,
	.atl_capacity = 0x3022,
	.atl_vfcapacity = 0x4024,
	.atl_low_bat_comp_start_vol = 3450,
	.byd_capacity = 0x36B0,
	.byd_vfcapacity = 0x48EA,
	.byd_low_bat_comp_start_vol = 3600,
	.fuel_alert_line = GPIO_FUEL_ALERT,
	.check_jig_status = check_jig_on
};

static struct i2c_board_info i2c_devs14_emul[] __initdata = {
	{
		I2C_BOARD_INFO("fuelgauge", 0x36),
		.platform_data = &max17042_pdata,
	},
};
#endif

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
static struct i2c_gpio_platform_data gpio_i2c_data16 = {
	.sda_pin = GPIO_MHL_DSDA_2_8V,
	.scl_pin = GPIO_MHL_DSCL_2_8V,
};

struct platform_device s3c_device_i2c16 = {
	.name = "i2c-gpio",
	.id = 16,
	.dev.platform_data = &gpio_i2c_data16,
};

static struct i2c_board_info i2c_devs16_emul[] __initdata = {
};

#if 0
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

#if defined(CONFIG_STMPE811_ADC) || defined(CONFIG_FM_SI4709_MODULE) \
	|| defined(CONFIG_FM_SI4705_MODULE)
static struct i2c_gpio_platform_data gpio_i2c_data19 = {
	.sda_pin = GPIO_ADC_SDA,
	.scl_pin = GPIO_ADC_SCL,
};

struct platform_device s3c_device_i2c19 = {
	.name = "i2c-gpio",
	.id = 19,
	.dev.platform_data = &gpio_i2c_data19,
};


/* I2C19 */
static struct i2c_board_info i2c_devs19_emul[] __initdata = {
#if defined(CONFIG_STMPE811_ADC)
	{
		I2C_BOARD_INFO("stmpe811-adc", (0x82 >> 1)),
		.platform_data	= &stmpe811_pdata,
	},
#endif
#ifdef CONFIG_FM_SI4705_MODULE
	{
		I2C_BOARD_INFO("Si4709", (0x22 >> 1)),
	},
#endif
#ifdef CONFIG_FM_SI4709_MODULE
	{
		I2C_BOARD_INFO("Si4709", (0x20 >> 1)),
	},
#endif

};
#endif

/* I2C21 */
#ifdef CONFIG_LEDS_AN30259A
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
static struct i2c_board_info i2c_devs21_emul[] __initdata = {
#ifdef CONFIG_LEDS_AN30259A
	{
		I2C_BOARD_INFO("an30259a", 0x30),
	},
#endif
};


/* I2C22 */
#ifdef CONFIG_IR_REMOCON_MC96
static void irda_wake_en(bool onoff)
{
	gpio_direction_output(GPIO_IRDA_WAKE, onoff);
#if 0
	printk(KERN_ERR "%s: irda_wake_en : %d\n", __func__, onoff);
#endif
}

static void irda_device_init(void)
{
	int ret;

	printk(KERN_ERR "%s called!\n", __func__);

	ret = gpio_request(GPIO_IRDA_WAKE, "irda_wake");
	if (ret) {
		printk(KERN_ERR "%s: gpio_request fail[%d], ret = %d\n",
				__func__, GPIO_IRDA_WAKE, ret);
		return;
	}
	gpio_direction_output(GPIO_IRDA_WAKE, 0);

	s3c_gpio_cfgpin(GPIO_IRDA_IRQ, S3C_GPIO_INPUT);
	s3c_gpio_setpull(GPIO_IRDA_IRQ, S3C_GPIO_PULL_UP);
	gpio_direction_input(GPIO_IRDA_IRQ);

	return;
}

static int vled_ic_onoff;

static void irda_vdd_onoff(bool onoff)
{
	static struct regulator *vled_ic;

	if (onoff) {
		vled_ic = regulator_get(NULL, "vled_ic_1.9v");
		if (IS_ERR(vled_ic)) {
			pr_err("could not get regulator vled_ic_1.9v\n");
			return;
		}
		regulator_enable(vled_ic);
		vled_ic_onoff = 1;
	} else if (vled_ic_onoff == 1) {
		regulator_force_disable(vled_ic);
		regulator_put(vled_ic);
		vled_ic_onoff = 0;
	}
}

static struct i2c_gpio_platform_data gpio_i2c_data22 = {
	.sda_pin = GPIO_IRDA_SDA,
	.scl_pin = GPIO_IRDA_SCL,
	.udelay = 2,
	.sda_is_open_drain = 0,
	.scl_is_open_drain = 0,
	.scl_is_output_only = 0,
};

struct platform_device s3c_device_i2c22 = {
	.name = "i2c-gpio",
	.id = 22,
	.dev.platform_data = &gpio_i2c_data22,
};

static struct mc96_platform_data mc96_pdata = {
	.ir_wake_en = irda_wake_en,
	.ir_vdd_onoff = irda_vdd_onoff,
};

static struct i2c_board_info i2c_devs22_emul[] __initdata = {
	{
		I2C_BOARD_INFO("mc96", (0XA0 >> 1)),
		.platform_data = &mc96_pdata,
	},
};
#endif

#ifdef CONFIG_I2C_GPIO
#ifdef CONFIG_MOTOR_DRV_ISA1200
static void isa1200_init(void)
{
	int gpio;
	gpio = GPIO_MOTOR_EN;
	gpio_request(gpio, "MOTOR_EN");
	gpio_direction_output(gpio, 1);
	gpio_export(gpio, 0);
}
static int isa1200_vdd_en(bool en)
{
	return gpio_direction_output(GPIO_MOTOR_EN, en);
}

static struct i2c_gpio_platform_data gpio_i2c_data17 = {
	.sda_pin = GPIO_MOTOR_SDA,
	.scl_pin = GPIO_MOTOR_SCL,
};

struct platform_device s3c_device_i2c17 = {
	.name = "i2c-gpio",
	.id = 17,
	.dev.platform_data = &gpio_i2c_data17,
};

static struct isa1200_vibrator_platform_data isa1200_vibrator_pdata = {
	.gpio_en = isa1200_vdd_en,
	.max_timeout = 10000,
	.ctrl0 = CTL0_DIVIDER128 | CTL0_PWM_INPUT,
	.ctrl1 = CTL1_DEFAULT,
	.ctrl2 = 0,
	.ctrl4 = 0,
	. pll = 0x23,
	.duty = 0x71,
	.period = 0x74,
	.get_clk = NULL,
	.pwm_id = 0,
	.pwm_duty = 37000,
	.pwm_period = 38675,
};
static struct i2c_board_info i2c_devs17_emul[] = {
	{
		I2C_BOARD_INFO("isa1200_vibrator",  0x48),
		.platform_data = &isa1200_vibrator_pdata,
	},
};
#endif	/* CONFIG_MOTOR_DRV_ISA1200 */
#endif

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

	gpio_request(GPIO_TA_nCHG, "TA_nCHG");
	s5p_register_gpio_interrupt(GPIO_TA_nCHG);

	pr_info("BAT : Battery GPIO initialized.\n");
}

static void  sec_charger_cb(int set_cable_type, int cable_sub_type)
{
	struct usb_gadget *gadget = platform_get_drvdata(&s3c_device_usbgadget);
	bool cable_state_to_tsp;
	bool cable_state_to_usb;
	enum usb_path_t usb_path;

	switch (set_cable_type) {
	case CHARGER_USB:
		cable_state_to_tsp = true;
		cable_state_to_usb = true;
		is_cable_attached = true;
		is_usb_lpm_enter = false;
		break;
	case CHARGER_DOCK:
		switch (cable_sub_type) {
		case ONLINE_SUB_TYPE_KBD:
			cable_state_to_tsp = true;
			cable_state_to_usb = false;
			is_cable_attached = true;
			is_usb_lpm_enter = true;
			pr_info("%s: key dock is detected\n", __func__);
			break;
		case ONLINE_SUB_TYPE_UNKNOWN:
		case ONLINE_SUB_TYPE_MHL:
		case ONLINE_SUB_TYPE_AUDIO:
		case ONLINE_SUB_TYPE_DESK:
		case ONLINE_SUB_TYPE_SMART_NOTG:
		case ONLINE_SUB_TYPE_SMART_OTG:
		default:
			cable_state_to_tsp = true;
			cable_state_to_usb = false;
			is_cable_attached = true;
			is_usb_lpm_enter = true;

			usb_path = usb_switch_get_path();
			if (usb_path != USB_PATH_AP) {
				usb_switch_lock();
				usb_switch_set_path(USB_PATH_TA);
				usb_switch_unlock();
			} else {
				pr_info("%s: sub cx is detected and ap path\n",
					__func__);
			}
			break;
		}
		break;
	case CHARGER_AC:
	case CHARGER_MISC:
		cable_state_to_tsp = true;
		cable_state_to_usb = false;
		is_cable_attached = true;
		is_usb_lpm_enter = true;

		usb_path = usb_switch_get_path();
		if (usb_path != USB_PATH_AP) {
			usb_switch_lock();
			usb_switch_set_path(USB_PATH_TA);
			usb_switch_unlock();
		} else {
			pr_info("%s: charger is detected and ap path\n",
								__func__);
		}
		break;
	case CHARGER_BATTERY:
	case CHARGER_DISCHARGE:
	default:
		cable_state_to_tsp = false;
		cable_state_to_usb = false;
		is_cable_attached = false;
		is_usb_lpm_enter = true;
		usb_switch_lock();
		usb_switch_clr_path(USB_PATH_TA);
		usb_switch_unlock();
		break;
	}
	pr_info("%s:cable_type=%d,tsp(%d),usb(%d),attached(%d),usblpm(%d)\n",
		__func__, set_cable_type, cable_state_to_tsp,
		cable_state_to_usb, is_cable_attached, is_usb_lpm_enter);

#if defined(CONFIG_TOUCHSCREEN_SYNAPTICS_S7301)
	synaptics_ts_charger_infom(is_cable_attached);
#endif

#if defined(CONFIG_TOUCHSCREEN_ATMEL_MXT1664S)
	ts_charger_infom(is_cable_attached);
#endif

/* Send charger state to px-switch. px-switch needs cable type what USB or not */
	set_usb_connection_state(!is_usb_lpm_enter);

#ifdef CONFIG_TARGET_LOCALE_KOR
	if (px_switch_get_usb_lock_state()) {
		pr_info("%s: usb locked by mdm\n", __func__);
		return;
	}
#endif

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
		.accessory_line = GPIO_ACCESSORY_INT,
	},
#if defined(CONFIG_SMB347_CHARGER)
	.set_charging_state = sec_bat_set_charging_state,
	.get_charging_state = sec_bat_get_charging_state,
	.set_charging_current = sec_bat_set_charging_current,
	.get_charging_current = sec_bat_get_charging_current,
	.get_charger_is_full = sec_bat_get_charger_is_full,
	.get_aicl_current = sec_bat_get_aicl_current,
	.get_input_current = sec_bat_get_input_current,
#endif
	.init_charger_gpio = sec_bat_gpio_init,
	.inform_charger_connection = sec_charger_cb,

#if defined(CONFIG_TARGET_LOCALE_USA)
#if defined(CONFIG_MACH_P4NOTELTE_USA_SPR)
	.temp_event_threshold = 70000,		/* 62c */
	.temp_high_threshold = 48000,		/* 45c */
	.temp_high_recovery = 43200,		/* 42c */
	.temp_low_recovery = 0,			/* 0c */
	.temp_low_threshold = -5000,		/* -5c */

	.temp_lpm_high_threshold = 48000,	/* 45c */
	.temp_lpm_high_recovery = 43500,	/* 42c */
	.temp_lpm_low_recovery = 0,		/* 0c */
	.temp_lpm_low_threshold = -3700,	/* -5c */
#elif defined(CONFIG_MACH_P4NOTELTE_USA_VZW)
	.temp_event_threshold = 62000,		/* 62c */
	.temp_high_threshold = 45000,		/* 45c */
	.temp_high_recovery = 42000,		/* 42c */
	.temp_low_recovery = 0,			/* 0c */
	.temp_low_threshold = -5000,		/* -5c */

	.temp_lpm_high_threshold = 45000,	/* 45c */
	.temp_lpm_high_recovery = 42000,	/* 42c */
	.temp_lpm_low_recovery = 0,		/* 0c */
	.temp_lpm_low_threshold = -5000,	/* -5c */
#else
	.temp_event_threshold = 62000,          /* 62c */
	.temp_high_threshold = 62000,		/* 62c */
	.temp_high_recovery = 42000,		/* 42c */
	.temp_low_recovery = 0,			/* 0c */
	.temp_low_threshold = -5000,		/* -5c */

	.temp_lpm_high_threshold = 62000,	/* 62c */
	.temp_lpm_high_recovery = 42000,	/* 42c */
	.temp_lpm_low_recovery = 0,		/* 0c */
	.temp_lpm_low_threshold = -5000,	/* -5c */
#endif
#elif defined(CONFIG_TARGET_LOCALE_KOR)
#if defined(CONFIG_MACH_P4NOTELTE_KOR_SKT) || \
	defined(CONFIG_MACH_P4NOTELTE_KOR_KT) || \
	defined(CONFIG_MACH_P4NOTELTE_KOR_LGT)
	.temp_high_threshold = 63000,	/* 62c */
	.temp_high_recovery = 43000,	/* 42c */
	.temp_low_recovery = -1000,		/* 0c */
	.temp_low_threshold = -4000,	/* -5c */
#else
	.temp_high_threshold = 61400,	/* 65c */
	.temp_high_recovery = 43500,	/* 42c */
	.temp_low_recovery = 0,			/* 0c */
	.temp_low_threshold = -5000,	/* -5c */
#endif
#else
	.temp_high_threshold = 50000,	/* 50c */
	.temp_high_recovery = 42000,	/* 42c */
	.temp_low_recovery = 0,			/* 0c */
	.temp_low_threshold = -5000,	/* -5c */
#endif
	.recharge_voltage = 4150,	/*4.15V */

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

#ifdef CONFIG_USB_HOST_NOTIFY
static void px_usb_otg_power(int active);
#define HOST_NOTIFIER_BOOSTER	px_usb_otg_power
#define HOST_NOTIFIER_GPIO		GPIO_ACCESSORY_OUT_5V
#define RETRY_CNT_LIMIT 100

struct host_notifier_platform_data host_notifier_pdata = {
	.ndev.name	= "usb_otg",
	.gpio		= HOST_NOTIFIER_GPIO,
	.booster	= HOST_NOTIFIER_BOOSTER,
	.irq_enable = 1,
};

struct platform_device host_notifier_device = {
	.name = "host_notifier",
	.dev.platform_data = &host_notifier_pdata,
};

static void __init acc_chk_gpio_init(void)
{
	gpio_request(GPIO_ACCESSORY_EN, "GPIO_USB_OTG_EN");
	s3c_gpio_cfgpin(GPIO_ACCESSORY_EN, S3C_GPIO_OUTPUT);
	s3c_gpio_setpull(GPIO_ACCESSORY_EN, S3C_GPIO_PULL_NONE);
	gpio_direction_output(GPIO_ACCESSORY_EN, false);

	gpio_request(GPIO_ACCESSORY_OUT_5V, "gpio_acc_5v");
	s3c_gpio_cfgpin(GPIO_ACCESSORY_OUT_5V, S3C_GPIO_SFN(0xf));
	s3c_gpio_setpull(GPIO_ACCESSORY_OUT_5V, S3C_GPIO_PULL_NONE);
	gpio_direction_input(GPIO_ACCESSORY_OUT_5V);
}
#endif

#ifdef CONFIG_30PIN_CONN
static void smdk_accessory_gpio_init(void)
{
	gpio_request(GPIO_ACCESSORY_INT, "accessory");
	s3c_gpio_cfgpin(GPIO_ACCESSORY_INT, S3C_GPIO_SFN(0xf));
	s3c_gpio_setpull(GPIO_ACCESSORY_INT, S3C_GPIO_PULL_NONE);
	gpio_direction_input(GPIO_ACCESSORY_INT);

	gpio_request(GPIO_DOCK_INT, "dock");
	s3c_gpio_cfgpin(GPIO_DOCK_INT, S3C_GPIO_SFN(0xf));
	s3c_gpio_setpull(GPIO_DOCK_INT, S3C_GPIO_PULL_NONE);
	gpio_direction_input(GPIO_DOCK_INT);
}

void smdk_accessory_power(u8 token, bool active)
{
	int gpio_acc_en = 0;
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
	gpio_acc_5v = GPIO_ACCESSORY_OUT_5V;

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
#if (CONFIG_SAMSUNG_ANALOG_UART_SWITCH == 2)
	int gpio_uart_sel2;
	gpio_uart_sel = GPIO_UART_SEL;
	gpio_uart_sel2 = GPIO_UART_SEL2;
#else
	gpio_uart_sel = GPIO_UART_SEL;
#endif
#endif

#if (CONFIG_SAMSUNG_ANALOG_UART_SWITCH == 2)
	if (en) {
		gpio_direction_output(gpio_uart_sel, 1);
		gpio_direction_output(gpio_uart_sel2, 1);
		printk(KERN_DEBUG "[Keyboard] uart_sel : 1, 1\n");
	} else {
		gpio_direction_output(gpio_uart_sel, 1);
		gpio_direction_output(gpio_uart_sel2, 0);
		printk(KERN_DEBUG "[Keyboard] uart_sel : 0, 0\n");
	}
#else
	if (en)
		gpio_direction_output(gpio_uart_sel, 1);
	else
		gpio_direction_output(gpio_uart_sel, 0);

	printk(KERN_DEBUG "[Keyboard] uart_sel : %d\n",
		gpio_get_value(gpio_uart_sel));
#endif
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
static void px_usb_otg_power(int active)
{
	smdk_accessory_power(2, active);
}

static void px_usb_otg_en(int active)
{
	pr_info("otg %s : %d\n", __func__, active);

	usb_switch_lock();

	if (active) {

#ifdef CONFIG_USB_EHCI_S5P
		pm_runtime_get_sync(&s5p_device_ehci.dev);
#endif
#ifdef CONFIG_USB_OHCI_S5P
		pm_runtime_get_sync(&s5p_device_ohci.dev);
#endif
		usb_switch_set_path(USB_PATH_AP);
		px_usb_otg_power(1);

		msleep(500);

		host_notifier_pdata.ndev.mode = NOTIFY_HOST_MODE;
		if (host_notifier_pdata.usbhostd_start)
			host_notifier_pdata.usbhostd_start();
	} else {

#ifdef CONFIG_USB_OHCI_S5P
		pm_runtime_put_sync(&s5p_device_ohci.dev);
#endif
#ifdef CONFIG_USB_EHCI_S5P
		pm_runtime_put_sync(&s5p_device_ehci.dev);
#endif

		usb_switch_clr_path(USB_PATH_AP);
		host_notifier_pdata.ndev.mode = NOTIFY_NONE_MODE;
		if (host_notifier_pdata.usbhostd_stop)
			host_notifier_pdata.usbhostd_stop();
		px_usb_otg_power(0);
	}

	usb_switch_unlock();
}
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
#if defined(CONFIG_SAMSUNG_MHL_9290)
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

#if defined(CONFIG_SENSORS_BH1721) || defined(CONFIG_SENSORS_AL3201)
static struct i2c_gpio_platform_data i2c9_platdata = {
	.sda_pin	= GPIO_PS_ALS_SDA_28V,
	.scl_pin	= GPIO_PS_ALS_SCL_28V,
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
#endif

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

#ifdef CONFIG_SENSORS_LPS331
static struct i2c_gpio_platform_data i2c11_platdata = {
	.sda_pin	= GPIO_BSENSE_SDA_18V,
	.scl_pin	= GPIO_BSENSE_SCL_18V,
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

/* IR_LED */
#ifdef CONFIG_IR_REMOCON_GPIO

static struct platform_device ir_remote_device = {
	.name = "ir_rc",
	.id = 0,
	.dev = {
	},
};

static void ir_rc_init_hw(void)
{
	s3c_gpio_cfgpin(GPIO_IRDA_CONTROL, S3C_GPIO_OUTPUT);
	s3c_gpio_setpull(GPIO_IRDA_CONTROL, S3C_GPIO_PULL_NONE);
	gpio_set_value(GPIO_IRDA_CONTROL, 0);
}

#endif
/* IR_LED */

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

#ifdef CONFIG_BATTERY_SEC_PX
	&sec_battery_device,
#endif

#ifdef CONFIG_SND_SOC_WM8994
	&vbatt_device,
#endif

	&s3c_device_wdt,
	&s3c_device_rtc,

	&s3c_device_i2c0,
	&s3c_device_i2c1,
#ifdef CONFIG_S3C_DEV_I2C3
	&s3c_device_i2c3,
#endif
#ifdef CONFIG_S3C_DEV_I2C4
	&s3c_device_i2c4,
#endif
	/* &s3c_device_i2c5, */
#ifdef CONFIG_S3C_DEV_I2C6
	&s3c_device_i2c6,
#endif
	&s3c_device_i2c7,
#ifdef CONFIG_S3C_DEV_I2C8
	&s3c_device_i2c8,
#endif
	/* &s3c_device_i2c9, */
#ifdef CONFIG_SENSORS_AK8975C
	&s3c_device_i2c10,
#endif
#ifdef CONFIG_SENSORS_AK8963C
	&s3c_device_i2c10,
#endif
#ifdef CONFIG_SENSORS_LPS331
	&s3c_device_i2c11,
#endif
	/* &s3c_device_i2c12, */
#ifdef CONFIG_SMB347_CHARGER
	&s3c_device_i2c13,
#endif
#ifdef CONFIG_BATTERY_MAX17042_FUELGAUGE_PX
	&s3c_device_i2c14,
#endif

#ifdef CONFIG_SAMSUNG_MHL
	&s3c_device_i2c15,
	&s3c_device_i2c16,
#endif

#ifdef CONFIG_MOTOR_DRV_ISA1200
	&s3c_device_i2c17,
#endif

#ifdef CONFIG_LEDS_AN30259A
	&s3c_device_i2c21,
#endif

#ifdef CONFIG_IR_REMOCON_MC96
	&s3c_device_i2c22,
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
#ifdef CONFIG_FB_S5P_S6C1372
	&lcd_s6c1372,
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
#ifdef CONFIG_EXYNOS_C2C
	&exynos_device_c2c,
#endif
#if defined(CONFIG_S3C64XX_DEV_SPI)
#if defined(CONFIG_VIDEO_S5C73M3_SPI)
	&exynos_device_spi1,
#endif
#if defined(CONFIG_PHONE_IPC_SPI)
	&exynos_device_spi2,
	&ipc_spi_device,
#elif defined(CONFIG_TDMB) || defined(CONFIG_TDMB_MODULE)
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
#ifdef CONFIG_30PIN_CONN
	&sec_device_connector,
#ifdef CONFIG_SEC_KEYBOARD_DOCK
	&sec_keyboard,
#endif
#endif
#if defined(CONFIG_IR_REMOCON_GPIO)
/* IR_LED */
	&ir_remote_device,
/* IR_LED */
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
		.arm_volt = 925000, /* vdd_arm in uV for temp compensation */
		.bus_volt = 900000, /* vdd_bus in uV for temp compensation */
		.g3d_volt = 900000, /* vdd_g3d in uV for temp compensation */
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

#if defined(CONFIG_CMA)
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
#if (CONFIG_VIDEO_SAMSUNG_MEMSIZE_JPEG > 0)
		{
			.name = "jpeg",
			.size = CONFIG_VIDEO_SAMSUNG_MEMSIZE_JPEG * SZ_1K,
			.start = 0
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
		{
			.name = "fimc1",
			.size = CONFIG_VIDEO_SAMSUNG_MEMSIZE_FIMC1 * SZ_1K,
			.start = 0x65c00000,
		},
#endif
#ifdef CONFIG_VIDEO_SAMSUNG_MEMSIZE_MFC1
		{
			.name = "mfc1",
			.size = CONFIG_VIDEO_SAMSUNG_MEMSIZE_MFC1 * SZ_1K,
			{
				.alignment = 1 << 26,
			},
			.start = 0x64000000,
		},
#endif
#ifdef CONFIG_VIDEO_SAMSUNG_MEMSIZE_MFC_NORMAL
		{
			.name = "mfc-normal",
			.size = CONFIG_VIDEO_SAMSUNG_MEMSIZE_MFC_NORMAL * SZ_1K,
			.start = 0x64000000,
		},
#endif
		{
			.size = 0
		},
	};
#ifdef CONFIG_EXYNOS_CONTENT_PATH_PROTECTION
	static struct cma_region regions_secure[] = {
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
#if (CONFIG_VIDEO_SAMSUNG_MEMSIZE_JPEG > 0)
		"s5p-jpeg=jpeg;"
#endif
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
		"s5p-smem/fimc0=fimc0;";

		s5p_cma_region_reserve(regions, regions_secure, 0, map);
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
	return 1; /* orig: system_rev != 3; */
}

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
#if defined(CONFIG_PHONE_IPC_SPI) \
	|| defined(CONFIG_TDMB) || defined(CONFIG_TDMB_MODULE)
	struct device *spi2_dev = &exynos_device_spi2.dev;
#endif
#endif

	/*
	  * prevent 4x12 ISP power off problem
	  * ISP_SYS Register has to be 0 before ISP block power off.
	  */
	__raw_writel(0x0, S5P_CMU_RESET_ISP_SYS);

	/* initialise the gpios */
	midas_config_gpio_table();
	exynos4_sleep_gpio_table_set = midas_config_sleep_gpio_table;

	midas_power_init();

	s3c_i2c0_set_platdata(NULL);
	i2c_register_board_info(0, i2c_devs0, ARRAY_SIZE(i2c_devs0));

	s3c_i2c1_set_platdata(NULL);
	i2c_register_board_info(1, i2c_devs1, ARRAY_SIZE(i2c_devs1));

	p4_tsp_init(system_rev);
	p4_key_init();
#if defined(CONFIG_EPEN_WACOM_G5SP)
	p4_wacom_init();
#endif	/* CONFIG_EPEN_WACOM_G5SP */

#ifdef CONFIG_LEDS_AAT1290A
	platform_device_register(&s3c_device_aat1290a_led);
#endif
	midas_sound_init();

#ifdef CONFIG_S3C_DEV_I2C5
	if (need_i2c5()) {
		s3c_i2c5_set_platdata(NULL);
		i2c_register_board_info(5, i2c_devs5,
			ARRAY_SIZE(i2c_devs5));
	}
#endif

	s3c_i2c7_set_platdata(NULL);
	i2c_register_board_info(7, i2c_devs7, ARRAY_SIZE(i2c_devs7));

	i2c_register_board_info(9, i2c_devs9_emul, ARRAY_SIZE(i2c_devs9_emul));

	i2c_register_board_info(10, i2c_devs10_emul,
				ARRAY_SIZE(i2c_devs10_emul));

	i2c_register_board_info(11, i2c_devs11_emul,
				ARRAY_SIZE(i2c_devs11_emul));

#ifdef CONFIG_SMB347_CHARGER
	/* smb347 charger */
	i2c_register_board_info(13, i2c_devs13_emul,
				ARRAY_SIZE(i2c_devs13_emul));
#endif

#ifdef CONFIG_BATTERY_MAX17042_FUELGAUGE_PX
	/* max17042 fuelgauge */
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

#ifdef CONFIG_MOTOR_DRV_ISA1200
	isa1200_init();
	i2c_register_board_info(17, i2c_devs17_emul,
				ARRAY_SIZE(i2c_devs17_emul));
#endif

#if defined(CONFIG_STMPE811_ADC) || defined(CONFIG_FM_SI4709_MODULE) \
	|| defined(CONFIG_FM_SI4705_MODULE)
	i2c_register_board_info(19, i2c_devs19_emul,
				ARRAY_SIZE(i2c_devs19_emul));
#endif

#ifdef CONFIG_LEDS_AN30259A
	i2c_register_board_info(21, i2c_devs21_emul,
				ARRAY_SIZE(i2c_devs21_emul));
#endif

#ifdef CONFIG_IR_REMOCON_MC96
	i2c_register_board_info(22, i2c_devs22_emul,
				ARRAY_SIZE(i2c_devs22_emul));
#endif

#if defined(GPIO_OLED_DET)
	gpio_request(GPIO_OLED_DET, "OLED_DET");
	s5p_register_gpio_interrupt(GPIO_OLED_DET);
	gpio_free(GPIO_OLED_DET);
#endif
#ifdef CONFIG_FB_S5P
#ifdef CONFIG_FB_S5P_LMS501KF03
	spi_register_board_info(spi_board_info, ARRAY_SIZE(spi_board_info));
	s3cfb_set_platdata(&lms501kf03_data);
#endif
#if defined(CONFIG_BACKLIGHT_PWM)
	samsung_bl_set(&smdk4212_bl_gpio_info, &smdk4212_bl_data);
#elif defined(CONFIG_FB_S5P_S6C1372)
	s6c1372_panel_gpio_init();
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

#if defined(CONFIG_VIDEO_TVOUT)
	s5p_hdmi_hpd_set_platdata(&hdmi_hpd_data);
	s5p_hdmi_cec_set_platdata(&hdmi_cec_data);
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
	platform_device_register(&s3c_device_adc);
#endif
#if defined(CONFIG_STMPE811_ADC) || defined(CONFIG_FM_SI4709_MODULE) \
	|| defined(CONFIG_FM_SI4705_MODULE)
	platform_device_register(&s3c_device_i2c19);
#endif
#ifdef CONFIG_SEC_THERMISTOR
	platform_device_register(&sec_device_thermistor);
#endif

#if defined(CONFIG_S3C_DEV_I2C5)
	if (need_i2c5())
		platform_device_register(&s3c_device_i2c5);
#endif

#if defined(CONFIG_SENSORS_BH1721) || defined(CONFIG_SENSORS_AL3201)
	platform_device_register(&s3c_device_i2c9);
#endif

#ifdef CONFIG_30PIN_CONN
	smdk_accessory_gpio_init();
#endif
#ifdef CONFIG_USB_HOST_NOTIFY
	acc_chk_gpio_init();
#endif

#if defined(CONFIG_SEC_DEV_JACK)
	p4note_jack_init();
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

	clk_set_rate(sclk, 800 * 1000 * 1000);
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

#if defined(CONFIG_PHONE_IPC_SPI) \
	|| defined(CONFIG_TDMB) || defined(CONFIG_TDMB_MODULE)
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
	__raw_writel((__raw_readl(EXYNOS4_CLKDIV_FSYS2) & 0xfff0fff0)
		     | 0x80008, EXYNOS4_CLKDIV_FSYS2);
	__raw_writel((__raw_readl(EXYNOS4_CLKDIV_FSYS1) & 0xfff0fff0)
		     | 0x80008, EXYNOS4_CLKDIV_FSYS1);

/* IR_LED */
#if defined(CONFIG_IR_REMOCON_MC96)
	irda_device_init();
#elif defined(CONFIG_IR_REMOCON_GPIO)
	ir_rc_init_hw();
#endif
/* IR_LED */
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
#ifdef CONFIG_EXYNOS_C2C
	.reserve	= &exynos_c2c_reserve,
#endif
	.init_early	= &exynos_init_reserve,
MACHINE_END

MACHINE_START(SMDK4212, "SMDK4x12")
	.boot_params	= S5P_PA_SDRAM + 0x100,
	.init_irq	= exynos4_init_irq,
	.map_io		= midas_map_io,
	.init_machine	= midas_machine_init,
	.timer		= &exynos4_timer,
#ifdef CONFIG_EXYNOS_C2C
	.reserve	= &exynos_c2c_reserve,
#endif
	.init_early	= &exynos_init_reserve,
MACHINE_END
