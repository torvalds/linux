/*
 * linux/arch/arm/mach-exynos4/mach-smdk4x12.c
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/gpio.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/io.h>
#include <linux/mmc/host.h>
#include <linux/platform_device.h>
#include <linux/pwm_backlight.h>
#include <linux/regulator/machine.h>
#include <linux/serial_core.h>
#include <linux/persistent_ram.h>
#include <linux/spi/spi.h>
#include <linux/spi/spi_gpio.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/gpio_keys.h>
#include <linux/lcd.h>
#include <linux/clk.h>
#include <linux/input/pixcir_ts.h>
#include <linux/sys_soc.h>

#include <asm/mach/arch.h>
#include <asm/hardware/gic.h>
#include <asm/mach-types.h>
#include <asm/system_info.h>

#include <plat/backlight.h>
#include <plat/clock.h>
#include <plat/cpu.h>
#include <plat/devs.h>
#include <plat/gpio-cfg.h>
#include <plat/iic.h>
#include <plat/keypad.h>
#include <plat/regs-serial.h>
#include <plat/regs-adc.h>
#include <plat/sdhci.h>
#include <plat/watchdog.h>

#include <mach/map.h>
#include <mach/exynos_fiq_debugger.h>
#include <plat/hwmon.h>
#include <plat/iic.h>
#include <mach/map.h>
#include <mach/pmu.h>

#include "common.h"
#include "board-smdk4x12.h"

static struct platform_device ramconsole_device = {
	.name	= "ram_console",
	.id	= -1,
};

static struct platform_device persistent_trace_device = {
	.name	= "persistent_trace",
	.id	= -1,
};

/* Following are default values for UCON, ULCON and UFCON UART registers */
#define SMDK4X12_UCON_DEFAULT	(S3C2410_UCON_TXILEVEL |	\
				 S3C2410_UCON_RXILEVEL |	\
				 S3C2410_UCON_TXIRQMODE |	\
				 S3C2410_UCON_RXIRQMODE |	\
				 S3C2410_UCON_RXFIFO_TOI |	\
				 S3C2443_UCON_RXERR_IRQEN)

#define SMDK4X12_ULCON_DEFAULT	S3C2410_LCON_CS8

#define SMDK4X12_UFCON_DEFAULT	(S3C2410_UFCON_FIFOMODE |	\
				 S5PV210_UFCON_TXTRIG4 |	\
				 S5PV210_UFCON_RXTRIG4)

static struct s3c2410_uartcfg smdk4x12_uartcfgs[] __initdata = {
	[0] = {
		.hwport		= 0,
		.flags		= 0,
		.ucon		= SMDK4X12_UCON_DEFAULT,
		.ulcon		= SMDK4X12_ULCON_DEFAULT,
		.ufcon		= SMDK4X12_UFCON_DEFAULT,
	},
#ifdef CONFIG_EXYNOS_FIQ_DEBUGGER
	/*
	 * Don't need to initialize hwport 1, when FIQ debugger is
	 * enabled. Because it will be handled by fiq_debugger.
	 */
	[1] = {
		.hwport		= 2,
		.flags		= 0,
		.ucon		= SMDK4X12_UCON_DEFAULT,
		.ulcon		= SMDK4X12_ULCON_DEFAULT,
		.ufcon		= SMDK4X12_UFCON_DEFAULT,
	},
	[2] = {
		.hwport		= 3,
		.flags		= 0,
		.ucon		= SMDK4X12_UCON_DEFAULT,
		.ulcon		= SMDK4X12_ULCON_DEFAULT,
		.ufcon		= SMDK4X12_UFCON_DEFAULT,
	},
#else
	[1] = {
		.hwport		= 1,
		.flags		= 0,
		.ucon		= SMDK4X12_UCON_DEFAULT,
		.ulcon		= SMDK4X12_ULCON_DEFAULT,
		.ufcon		= SMDK4X12_UFCON_DEFAULT,
	},
	[2] = {
		.hwport		= 2,
		.flags		= 0,
		.ucon		= SMDK4X12_UCON_DEFAULT,
		.ulcon		= SMDK4X12_ULCON_DEFAULT,
		.ufcon		= SMDK4X12_UFCON_DEFAULT,
	},
	[3] = {
		.hwport		= 3,
		.flags		= 0,
		.ucon		= SMDK4X12_UCON_DEFAULT,
		.ulcon		= SMDK4X12_ULCON_DEFAULT,
		.ufcon		= SMDK4X12_UFCON_DEFAULT,
	},
#endif
};

#ifdef CONFIG_S3C_DEV_HWMON
static struct s3c_hwmon_pdata smdk4x12_hwmon_pdata __initdata = {
	/* Reference voltage (1.2V) */
	.in[0] = &(struct s3c_hwmon_chcfg) {
		.name		= "smdk:reference-voltage",
		.mult		= 3300,
		.div		= 4096,
	},
};
#endif

static void smdk4x12_ts_gpio_setup(void)
{
	int gpio_reset = EXYNOS4_GPM3(4);

	gpio_request_one(gpio_reset, GPIOF_OUT_INIT_LOW, "TOUCH RESET");
	mdelay(20);
	gpio_direction_output(gpio_reset, 1);
	mdelay(30);
	s3c_gpio_setpull(EXYNOS4_GPX2(6), S3C_GPIO_PULL_NONE);
}

static int smdk4x12_ts_get_value(void)
{
	return gpio_get_value(EXYNOS4_GPX2(6));
}

static struct pixcir_ts_platform_data smdk4x12_ts_data = {
	.attb_read_val = smdk4x12_ts_get_value,
	.x_max = 480,
	.y_max = 800,
};

static struct i2c_board_info smdk4x12_i2c_devs7[] __initdata = {
	{
		I2C_BOARD_INFO("pixcir_ts", 0x5C),
		.irq		= IRQ_EINT(22),
		.platform_data	= &smdk4x12_ts_data,
	}
};

struct gpio_keys_button smdk4x12_button[] = {
	{
		.code = KEY_POWER,
		.gpio = EXYNOS4_GPX0(0),
		.active_low = 1,
		.wakeup = 1,
	}
};

struct gpio_keys_platform_data smdk4x12_gpiokeys_platform_data = {
	smdk4x12_button,
	ARRAY_SIZE(smdk4x12_button),
};

static struct platform_device smdk4x12_gpio_keys = {
	.name	= "gpio-keys",
	.dev	= {
		.platform_data = &smdk4x12_gpiokeys_platform_data,
	},
};

static uint32_t smdk4x12_keymap[] __initdata = {
	/* KEY(row, col, keycode) */
	KEY(1, 0, KEY_D), KEY(1, 1, KEY_A), KEY(1, 2, KEY_B),
	KEY(1, 3, KEY_E), KEY(1, 4, KEY_C)
};

static struct matrix_keymap_data smdk4x12_keymap_data __initdata = {
	.keymap		= smdk4x12_keymap,
	.keymap_size	= ARRAY_SIZE(smdk4x12_keymap),
};

static struct samsung_keypad_platdata smdk4x12_keypad_data __initdata = {
	.keymap_data	= &smdk4x12_keymap_data,
	.rows		= 2,
	.cols		= 5,
};

static struct platform_device *smdk4x12_devices[] __initdata = {
	&ramconsole_device,
	&persistent_trace_device,
	&s3c_device_i2c4,
	&s3c_device_i2c5,
	&s3c_device_i2c7,
	&s3c_device_rtc,
	&s3c_device_wdt,
	&smdk4x12_gpio_keys,
	&samsung_device_keypad,
	&exynos4_device_g3d,
};

#if defined(CONFIG_CMA)
#include "reserve-mem.h"
static void __init exynos_reserve_mem(void)
{
	static struct cma_region regions[] = {
		{
			.name = "ion",
#ifdef CONFIG_ION_EXYNOS_CONTIGHEAP_SIZE
			.size = CONFIG_ION_EXYNOS_CONTIGHEAP_SIZE * SZ_1K,
#endif
			.start = 0
		},
		{
			.size = 0
		},
	};
	static const char map[] __initconst = "ion-exynos=ion;";

	exynos_cma_region_reserve(regions, NULL, NULL, map);
}
#else /* !CONFIG_CMA*/
static inline void exynos_reserve_mem(void)
{
}
#endif

static void __init smdk4x12_map_io(void)
{
	clk_xusbxti.rate = 24000000;

	exynos_init_io(NULL, 0);
	s3c24xx_init_clocks(clk_xusbxti.rate);
	s3c24xx_init_uarts(smdk4x12_uartcfgs, ARRAY_SIZE(smdk4x12_uartcfgs));

	exynos_reserve_mem();
}

static struct persistent_ram_descriptor smdk4x12_prd[] __initdata = {
	{
		.name = "ram_console",
		.size = SZ_2M,
	},
#ifdef CONFIG_PERSISTENT_TRACER
	{
		.name = "persistent_trace",
		.size = SZ_1M,
	},
#endif
};

static struct persistent_ram smdk4x12_pr __initdata = {
	.descs = smdk4x12_prd,
	.num_descs = ARRAY_SIZE(smdk4x12_prd),
	.start = PLAT_PHYS_OFFSET + SZ_512M,
#ifdef CONFIG_PERSISTENT_TRACER
	.size = 3 * SZ_1M,
#else
	.size = SZ_2M,
#endif
};

static void __init smdk4x12_init_early(void)
{
	persistent_ram_early_init(&smdk4x12_pr);
}

/* WDT */
static struct s3c_watchdog_platdata smdk4x12_watchdog_platform_data = {
	exynos_pmu_wdt_control,
	PMU_WDT_RESET_TYPE0,
};

#define SMDK4412_REV_0_0_ADC_VALUE 0
#define SMDK4412_REV_0_1_ADC_VALUE 443

static int smdk4x12_hw_rev;

int exynos4_smdk4x12_get_revision(void)
{
	return smdk4x12_hw_rev;
}

static void smdk4x12_init_hw_rev(void)
{
	int		adc_val = 0;
	struct clk	*adc_clk;
	struct resource	*res;
	void __iomem	*adc_regs;
	unsigned int	con;

	if ((soc_is_exynos4412() && samsung_rev() < EXYNOS4412_REV_1_0) ||
		(soc_is_exynos4212() && samsung_rev() < EXYNOS4212_REV_1_0)) {
		smdk4x12_hw_rev = SMDK4X12_REV_0_0;
		goto done;
	}

	adc_clk = clk_get(NULL, "adc");
	if (unlikely(IS_ERR(adc_clk))) {
		smdk4x12_hw_rev = SMDK4X12_REV_0_0;
		goto done;
	}

	clk_enable(adc_clk);

	res = platform_get_resource(&s3c_device_adc, IORESOURCE_MEM, 0);
	if (unlikely(!res))
		goto err_clk;

	adc_regs = ioremap(res->start, resource_size(res));
	if (unlikely(!adc_regs))
		goto err_clk;

	writel(3, adc_regs + S5P_ADCMUX);

	con = readl(adc_regs + S3C2410_ADCCON);
	con &= ~S3C2410_ADCCON_STDBM;
	con &= ~S3C2410_ADCCON_STARTMASK;
	con |=  S3C2410_ADCCON_PRSCEN;

	con |= S3C2410_ADCCON_ENABLE_START;
	writel(con, adc_regs + S3C2410_ADCCON);

	udelay(50);

	adc_val = readl(adc_regs + S3C2410_ADCDAT0) & 0xFFF;
	writel(0, adc_regs + S3C64XX_ADCCLRINT);

	iounmap(adc_regs);
err_clk:
	clk_disable(adc_clk);
	clk_put(adc_clk);

	smdk4x12_hw_rev = (adc_val < SMDK4412_REV_0_1_ADC_VALUE/2) ?
			SMDK4X12_REV_0_0 : SMDK4X12_REV_0_1;

done:
	pr_info("SMDK HW revision: %d, CPU EXYNOS4X12 Rev%d.%d\n",
			smdk4x12_hw_rev,
			samsung_rev() >> 4,
			samsung_rev() & 0xf);
}

static void __init soc_info_populate(struct soc_device_attribute *soc_dev_attr)
{
	soc_dev_attr->soc_id = kasprintf(GFP_KERNEL, "%08x%08x\n",
					 system_serial_high, system_serial_low);

	if (soc_is_exynos4212())
		soc_dev_attr->machine = kasprintf(GFP_KERNEL, "Exynos 4212\n");
	else
		soc_dev_attr->machine = kasprintf(GFP_KERNEL, "Exynos 4412\n");

	soc_dev_attr->family = kasprintf(GFP_KERNEL, "Exynos 4\n");
	soc_dev_attr->revision = kasprintf(GFP_KERNEL, "%d.%d\n",
					   samsung_rev() >> 4,
					   samsung_rev() & 0xf);
}

static ssize_t smdk4x12_get_board_revision(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	return sprintf(buf, "%d\n", smdk4x12_hw_rev);
}

struct device_attribute smdk4x12_soc_attr =
	__ATTR(board_rev, S_IRUGO, smdk4x12_get_board_revision, NULL);

static void __init exynos4_smdk4x12_sysfs_soc_init(void)
{
	struct device *parent;
	struct soc_device *soc_dev;
	struct soc_device_attribute *soc_dev_attr;

	soc_dev_attr = kzalloc(sizeof(*soc_dev_attr), GFP_KERNEL);
	if (!soc_dev_attr) {
		printk(KERN_ERR "Failed to allocate memory for soc_dev_attr\n");
		return;
	}

	soc_info_populate(soc_dev_attr);

	soc_dev = soc_device_register(soc_dev_attr);
	if (IS_ERR_OR_NULL(soc_dev)) {
		kfree(soc_dev_attr);
		printk(KERN_ERR "Failed to register a soc device under /sys\n");
		return;
	}

	parent = soc_device_to_device(soc_dev);
	if (!IS_ERR_OR_NULL(parent))
		device_create_file(parent, &smdk4x12_soc_attr);

	return;	/* Or return parent should you need to use one later */
}

static void __init smdk4x12_machine_init(void)
{
	smdk4x12_init_hw_rev();
#ifdef CONFIG_EXYNOS_FIQ_DEBUGGER
	exynos_serial_debug_init(1, 0);
#endif
	s3c_watchdog_set_platdata(&smdk4x12_watchdog_platform_data);

	exynos4_smdk4x12_power_init();

	s3c_i2c4_set_platdata(NULL);
	s3c_i2c5_set_platdata(NULL);

	s3c_i2c7_set_platdata(NULL);
	i2c_register_board_info(7, smdk4x12_i2c_devs7,
				ARRAY_SIZE(smdk4x12_i2c_devs7));

	samsung_keypad_set_platdata(&smdk4x12_keypad_data);

	exynos4_smdk4x12_mmc_init();
	exynos4_smdk4x12_audio_init();
	exynos4_smdk4x12_display_init();
	exynos4_smdk4x12_media_init();

	smdk4x12_ts_gpio_setup();
	exynos4_smdk4x12_usb_init();

	platform_add_devices(smdk4x12_devices, ARRAY_SIZE(smdk4x12_devices));
#ifdef CONFIG_S3C_ADC
	if ((soc_is_exynos4412() && samsung_rev() >= EXYNOS4412_REV_1_0) ||
		(soc_is_exynos4212() && samsung_rev() >= EXYNOS4212_REV_1_0)) {
		if (platform_device_register(&s3c_device_adc) == 0) {
#ifdef CONFIG_S3C_DEV_HWMON
			s3c_hwmon_set_platdata(&smdk4x12_hwmon_pdata);
			platform_device_register(&s3c_device_hwmon);
#endif
		}
	}
#endif

	exynos4_smdk4x12_sysfs_soc_init();
}

MACHINE_START(SMDK4212, "SMDK4X12")
	/* Maintainer: Kukjin Kim <kgene.kim@samsung.com> */
	.atag_offset	= 0x100,
	.init_early	= smdk4x12_init_early,
	.init_irq	= exynos4_init_irq,
	.map_io		= smdk4x12_map_io,
	.handle_irq	= gic_handle_irq,
	.init_machine	= smdk4x12_machine_init,
	.timer		= &exynos4_timer,
	.restart	= exynos4_restart,
MACHINE_END

MACHINE_START(SMDK4412, "SMDK4X12")
	/* Maintainer: Kukjin Kim <kgene.kim@samsung.com> */
	/* Maintainer: Changhwan Youn <chaos.youn@samsung.com> */
	.atag_offset	= 0x100,
	.init_early	= smdk4x12_init_early,
	.init_irq	= exynos4_init_irq,
	.map_io		= smdk4x12_map_io,
	.handle_irq	= gic_handle_irq,
	.init_machine	= smdk4x12_machine_init,
	.timer		= &exynos4_timer,
	.restart	= exynos4_restart,
MACHINE_END
