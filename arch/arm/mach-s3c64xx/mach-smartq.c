// SPDX-License-Identifier: GPL-2.0
//
// Copyright (C) 2010 Maurus Cuelenaere

#include <linux/delay.h>
#include <linux/fb.h>
#include <linux/gpio.h>
#include <linux/gpio/machine.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/pwm.h>
#include <linux/pwm_backlight.h>
#include <linux/serial_core.h>
#include <linux/serial_s3c.h>
#include <linux/spi/spi_gpio.h>
#include <linux/platform_data/s3c-hsotg.h>

#include <asm/mach-types.h>
#include <asm/mach/map.h>

#include <mach/map.h>
#include <mach/regs-gpio.h>
#include <mach/gpio-samsung.h>

#include <plat/cpu.h>
#include <plat/devs.h>
#include <linux/platform_data/i2c-s3c2410.h>
#include <plat/gpio-cfg.h>
#include <linux/platform_data/hwmon-s3c.h>
#include <linux/platform_data/usb-ohci-s3c2410.h>
#include <plat/sdhci.h>
#include <linux/platform_data/touchscreen-s3c2410.h>

#include <video/platform_lcd.h>
#include <plat/samsung-time.h>

#include "common.h"
#include "mach-smartq.h"
#include "regs-modem.h"

#define UCON S3C2410_UCON_DEFAULT
#define ULCON (S3C2410_LCON_CS8 | S3C2410_LCON_PNONE)
#define UFCON (S3C2410_UFCON_RXTRIG8 | S3C2410_UFCON_FIFOMODE)

static struct s3c2410_uartcfg smartq_uartcfgs[] __initdata = {
	[0] = {
		.hwport	     = 0,
		.flags	     = 0,
		.ucon	     = UCON,
		.ulcon	     = ULCON,
		.ufcon	     = UFCON,
	},
	[1] = {
		.hwport	     = 1,
		.flags	     = 0,
		.ucon	     = UCON,
		.ulcon	     = ULCON,
		.ufcon	     = UFCON,
	},
	[2] = {
		.hwport	     = 2,
		.flags	     = 0,
		.ucon	     = UCON,
		.ulcon	     = ULCON,
		.ufcon	     = UFCON,
	},
};

static void smartq_usb_host_powercontrol(int port, int to)
{
	pr_debug("%s(%d, %d)\n", __func__, port, to);

	if (port == 0) {
		gpio_set_value(S3C64XX_GPL(0), to);
		gpio_set_value(S3C64XX_GPL(1), to);
	}
}

static irqreturn_t smartq_usb_host_ocirq(int irq, void *pw)
{
	struct s3c2410_hcd_info *info = pw;

	if (gpio_get_value(S3C64XX_GPL(10)) == 0) {
		pr_debug("%s: over-current irq (oc detected)\n", __func__);
		s3c2410_usb_report_oc(info, 3);
	} else {
		pr_debug("%s: over-current irq (oc cleared)\n", __func__);
		s3c2410_usb_report_oc(info, 0);
	}

	return IRQ_HANDLED;
}

static void smartq_usb_host_enableoc(struct s3c2410_hcd_info *info, int on)
{
	int ret;

	/* This isn't present on a SmartQ 5 board */
	if (machine_is_smartq5())
		return;

	if (on) {
		ret = request_irq(gpio_to_irq(S3C64XX_GPL(10)),
				  smartq_usb_host_ocirq,
				  IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
				  "USB host overcurrent", info);
		if (ret != 0)
			pr_err("failed to request usb oc irq: %d\n", ret);
	} else {
		free_irq(gpio_to_irq(S3C64XX_GPL(10)), info);
	}
}

static struct s3c2410_hcd_info smartq_usb_host_info = {
	.port[0]	= {
		.flags	= S3C_HCDFLG_USED
	},
	.port[1]	= {
		.flags	= 0
	},

	.power_control	= smartq_usb_host_powercontrol,
	.enable_oc	= smartq_usb_host_enableoc,
};

static struct gpiod_lookup_table smartq_usb_otg_vbus_gpiod_table = {
	.dev_id = "gpio-vbus",
	.table = {
		GPIO_LOOKUP("GPL", 9, "vbus", GPIO_ACTIVE_LOW),
		{ },
	},
};

static struct platform_device smartq_usb_otg_vbus_dev = {
	.name			= "gpio-vbus",
};

static struct pwm_lookup smartq_pwm_lookup[] = {
	PWM_LOOKUP("samsung-pwm", 1, "pwm-backlight.0", NULL,
		   1000000000 / (1000 * 20), PWM_POLARITY_NORMAL),
};

static int smartq_bl_init(struct device *dev)
{
    s3c_gpio_cfgpin(S3C64XX_GPF(15), S3C_GPIO_SFN(2));

    return 0;
}

static struct platform_pwm_backlight_data smartq_backlight_data = {
	.max_brightness	= 1000,
	.dft_brightness	= 600,
	.enable_gpio	= -1,
	.init		= smartq_bl_init,
};

static struct platform_device smartq_backlight_device = {
	.name		= "pwm-backlight",
	.dev		= {
		.parent	= &samsung_device_pwm.dev,
		.platform_data = &smartq_backlight_data,
	},
};

static struct s3c2410_ts_mach_info smartq_touchscreen_pdata __initdata = {
	.delay			= 65535,
	.presc			= 99,
	.oversampling_shift	= 4,
};

static struct s3c_sdhci_platdata smartq_internal_hsmmc_pdata = {
	.max_width		= 4,
	.cd_type		= S3C_SDHCI_CD_PERMANENT,
};

static struct s3c_hwmon_pdata smartq_hwmon_pdata __initdata = {
	/* Battery voltage (?-4.2V) */
	.in[0] = &(struct s3c_hwmon_chcfg) {
		.name		= "smartq:battery-voltage",
		.mult		= 3300,
		.div		= 2048,
	},
	/* Reference voltage (1.2V) */
	.in[1] = &(struct s3c_hwmon_chcfg) {
		.name		= "smartq:reference-voltage",
		.mult		= 3300,
		.div		= 4096,
	},
};

static struct dwc2_hsotg_plat smartq_hsotg_pdata;

static int __init smartq_lcd_setup_gpio(void)
{
	int ret;

	ret = gpio_request(S3C64XX_GPM(3), "LCD power");
	if (ret < 0)
		return ret;

	/* turn power off */
	gpio_direction_output(S3C64XX_GPM(3), 0);

	return 0;
}

/* GPM0 -> CS */
static struct spi_gpio_platform_data smartq_lcd_control = {
	.num_chipselect	= 1,
};

static struct platform_device smartq_lcd_control_device = {
	.name			= "spi_gpio",
	.id			= 1,
	.dev.platform_data	= &smartq_lcd_control,
};

static struct gpiod_lookup_table smartq_lcd_control_gpiod_table = {
	.dev_id         = "spi_gpio",
	.table          = {
		GPIO_LOOKUP("GPIOM", 1,
			    "sck", GPIO_ACTIVE_HIGH),
		GPIO_LOOKUP("GPIOM", 2,
			    "mosi", GPIO_ACTIVE_HIGH),
		GPIO_LOOKUP("GPIOM", 3,
			    "miso", GPIO_ACTIVE_HIGH),
		GPIO_LOOKUP("GPIOM", 0,
			    "cs", GPIO_ACTIVE_HIGH),
		{ },
	},
};

static void smartq_lcd_power_set(struct plat_lcd_data *pd, unsigned int power)
{
	gpio_direction_output(S3C64XX_GPM(3), power);
}

static struct plat_lcd_data smartq_lcd_power_data = {
	.set_power	= smartq_lcd_power_set,
};

static struct platform_device smartq_lcd_power_device = {
	.name			= "platform-lcd",
	.dev.parent		= &s3c_device_fb.dev,
	.dev.platform_data	= &smartq_lcd_power_data,
};

static struct i2c_board_info smartq_i2c_devs[] __initdata = {
	{ I2C_BOARD_INFO("wm8987", 0x1a), },
};

static struct platform_device *smartq_devices[] __initdata = {
	&s3c_device_hsmmc1,	/* Init iNAND first, ... */
	&s3c_device_hsmmc0,	/* ... then the external SD card */
	&s3c_device_hsmmc2,
	&s3c_device_adc,
	&s3c_device_fb,
	&s3c_device_hwmon,
	&s3c_device_i2c0,
	&s3c_device_ohci,
	&s3c_device_rtc,
	&samsung_device_pwm,
	&s3c_device_usb_hsotg,
	&s3c64xx_device_iis0,
	&smartq_backlight_device,
	&smartq_lcd_control_device,
	&smartq_lcd_power_device,
	&smartq_usb_otg_vbus_dev,
};

static void __init smartq_lcd_mode_set(void)
{
	u32 tmp;

	/* set the LCD type */
	tmp = __raw_readl(S3C64XX_SPCON);
	tmp &= ~S3C64XX_SPCON_LCD_SEL_MASK;
	tmp |= S3C64XX_SPCON_LCD_SEL_RGB;
	__raw_writel(tmp, S3C64XX_SPCON);

	/* remove the LCD bypass */
	tmp = __raw_readl(S3C64XX_MODEM_MIFPCON);
	tmp &= ~MIFPCON_LCD_BYPASS;
	__raw_writel(tmp, S3C64XX_MODEM_MIFPCON);
}

static void smartq_power_off(void)
{
	gpio_direction_output(S3C64XX_GPK(15), 1);
}

static int __init smartq_power_off_init(void)
{
	int ret;

	ret = gpio_request(S3C64XX_GPK(15), "Power control");
	if (ret < 0) {
		pr_err("%s: failed to get GPK15\n", __func__);
		return ret;
	}

	/* leave power on */
	gpio_direction_output(S3C64XX_GPK(15), 0);

	pm_power_off = smartq_power_off;

	return ret;
}

static int __init smartq_usb_host_init(void)
{
	int ret;

	ret = gpio_request(S3C64XX_GPL(0), "USB power control");
	if (ret < 0) {
		pr_err("%s: failed to get GPL0\n", __func__);
		return ret;
	}

	ret = gpio_request(S3C64XX_GPL(1), "USB host power control");
	if (ret < 0) {
		pr_err("%s: failed to get GPL1\n", __func__);
		goto err;
	}

	if (!machine_is_smartq5()) {
		/* This isn't present on a SmartQ 5 board */
		ret = gpio_request(S3C64XX_GPL(10), "USB host overcurrent");
		if (ret < 0) {
			pr_err("%s: failed to get GPL10\n", __func__);
			goto err2;
		}
	}

	/* turn power off */
	gpio_direction_output(S3C64XX_GPL(0), 0);
	gpio_direction_output(S3C64XX_GPL(1), 0);
	if (!machine_is_smartq5())
		gpio_direction_input(S3C64XX_GPL(10));

	s3c_device_ohci.dev.platform_data = &smartq_usb_host_info;

	return 0;

err2:
	gpio_free(S3C64XX_GPL(1));
err:
	gpio_free(S3C64XX_GPL(0));
	return ret;
}

static int __init smartq_wifi_init(void)
{
	int ret;

	ret = gpio_request(S3C64XX_GPK(1), "wifi control");
	if (ret < 0) {
		pr_err("%s: failed to get GPK1\n", __func__);
		return ret;
	}

	ret = gpio_request(S3C64XX_GPK(2), "wifi reset");
	if (ret < 0) {
		pr_err("%s: failed to get GPK2\n", __func__);
		gpio_free(S3C64XX_GPK(1));
		return ret;
	}

	/* turn power on */
	gpio_direction_output(S3C64XX_GPK(1), 1);

	/* reset device */
	gpio_direction_output(S3C64XX_GPK(2), 0);
	mdelay(100);
	gpio_set_value(S3C64XX_GPK(2), 1);
	gpio_direction_input(S3C64XX_GPK(2));

	return 0;
}

static struct map_desc smartq_iodesc[] __initdata = {};
void __init smartq_map_io(void)
{
	s3c64xx_init_io(smartq_iodesc, ARRAY_SIZE(smartq_iodesc));
	s3c64xx_set_xtal_freq(12000000);
	s3c64xx_set_xusbxti_freq(12000000);
	s3c24xx_init_uarts(smartq_uartcfgs, ARRAY_SIZE(smartq_uartcfgs));
	samsung_set_timer_source(SAMSUNG_PWM3, SAMSUNG_PWM4);

	smartq_lcd_mode_set();
}

static struct gpiod_lookup_table smartq_audio_gpios = {
	.dev_id = "smartq-audio",
	.table = {
		GPIO_LOOKUP("GPL", 12, "headphone detect", 0),
		GPIO_LOOKUP("GPK", 12, "amplifiers shutdown", 0),
		{ },
	},
};

void __init smartq_machine_init(void)
{
	s3c_i2c0_set_platdata(NULL);
	dwc2_hsotg_set_platdata(&smartq_hsotg_pdata);
	s3c_hwmon_set_platdata(&smartq_hwmon_pdata);
	s3c_sdhci1_set_platdata(&smartq_internal_hsmmc_pdata);
	s3c_sdhci2_set_platdata(&smartq_internal_hsmmc_pdata);
	s3c64xx_ts_set_platdata(&smartq_touchscreen_pdata);

	i2c_register_board_info(0, smartq_i2c_devs,
				ARRAY_SIZE(smartq_i2c_devs));

	WARN_ON(smartq_lcd_setup_gpio());
	WARN_ON(smartq_power_off_init());
	WARN_ON(smartq_usb_host_init());
	WARN_ON(smartq_wifi_init());

	pwm_add_table(smartq_pwm_lookup, ARRAY_SIZE(smartq_pwm_lookup));
	gpiod_add_lookup_table(&smartq_lcd_control_gpiod_table);
	gpiod_add_lookup_table(&smartq_usb_otg_vbus_gpiod_table);
	platform_add_devices(smartq_devices, ARRAY_SIZE(smartq_devices));

	gpiod_add_lookup_table(&smartq_audio_gpios);
	platform_device_register_simple("smartq-audio", -1, NULL, 0);
}
