/*
 * linux/arch/arm/mach-exynos4/mach-hkdk4412.c
 *
 * Copyright (c) 2012 AgreeYa Mobility Co., Ltd.
 *		http://www.agreeyamobility.net
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/gpio.h>
#include <linux/gpio_keys.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/io.h>
#include <linux/mfd/max77686.h>
#include <linux/mmc/host.h>
#include <linux/platform_device.h>
#include <linux/pwm_backlight.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/fixed.h>
#include <linux/serial_core.h>
#include <linux/platform_data/s3c-hsotg.h>
#include <linux/platform_data/i2c-s3c2410.h>
#include <linux/platform_data/usb-ehci-s5p.h>
#include <linux/platform_data/usb-exynos.h>
#include <linux/delay.h>
#include <linux/lcd.h>
#include <linux/clk.h>

#include <asm/mach/arch.h>
#include <asm/hardware/gic.h>
#include <asm/mach-types.h>

#include <plat/backlight.h>
#include <plat/clock.h>
#include <plat/cpu.h>
#include <plat/devs.h>
#include <plat/gpio-cfg.h>
#include <plat/keypad.h>
#include <plat/mfc.h>
#include <plat/regs-serial.h>
#include <plat/sdhci.h>
#include <plat/fb.h>
#include <plat/hdmi.h>

#include <video/platform_lcd.h>
#include <video/samsung_fimd.h>

#include <mach/map.h>
#include <mach/regs-pmu.h>
#include <mach/dwmci.h>

#include "common.h"

extern void exynos4_setup_dwmci_cfg_gpio(struct platform_device *dev, int width);

/* Following are default values for UCON, ULCON and UFCON UART registers */
#define HKDK4412_UCON_DEFAULT	(S3C2410_UCON_TXILEVEL |	\
				 S3C2410_UCON_RXILEVEL |	\
				 S3C2410_UCON_TXIRQMODE |	\
				 S3C2410_UCON_RXIRQMODE |	\
				 S3C2410_UCON_RXFIFO_TOI |	\
				 S3C2443_UCON_RXERR_IRQEN)

#define HKDK4412_ULCON_DEFAULT	S3C2410_LCON_CS8

#define HKDK4412_UFCON_DEFAULT	(S3C2410_UFCON_FIFOMODE |	\
				 S5PV210_UFCON_TXTRIG4 |	\
				 S5PV210_UFCON_RXTRIG4)

static struct s3c2410_uartcfg hkdk4412_uartcfgs[] __initdata = {
	[0] = {
		.hwport		= 0,
		.flags		= 0,
		.ucon		= HKDK4412_UCON_DEFAULT,
		.ulcon		= HKDK4412_ULCON_DEFAULT,
		.ufcon		= HKDK4412_UFCON_DEFAULT,
	},
	[1] = {
		.hwport		= 1,
		.flags		= 0,
		.ucon		= HKDK4412_UCON_DEFAULT,
		.ulcon		= HKDK4412_ULCON_DEFAULT,
		.ufcon		= HKDK4412_UFCON_DEFAULT,
	},
	[2] = {
		.hwport		= 2,
		.flags		= 0,
		.ucon		= HKDK4412_UCON_DEFAULT,
		.ulcon		= HKDK4412_ULCON_DEFAULT,
		.ufcon		= HKDK4412_UFCON_DEFAULT,
	},
	[3] = {
		.hwport		= 3,
		.flags		= 0,
		.ucon		= HKDK4412_UCON_DEFAULT,
		.ulcon		= HKDK4412_ULCON_DEFAULT,
		.ufcon		= HKDK4412_UFCON_DEFAULT,
	},
};

static struct regulator_consumer_supply __initdata max77686_buck1_consumer[] = {
};

static struct regulator_consumer_supply __initdata max77686_buck2_consumer[] = {
	REGULATOR_SUPPLY("vdd_arm", NULL),	/* CPUFREQ */
};

static struct regulator_consumer_supply __initdata max77686_buck3_consumer[] = {
	REGULATOR_SUPPLY("vdd_int", NULL),	/* CPUFREQ */
};

static struct regulator_consumer_supply __initdata max77686_buck4_consumer[] = {
	REGULATOR_SUPPLY("vdd_g3d", "mali_drm"),	/* G3D */
};

static struct regulator_consumer_supply __initdata max77686_ldo1_consumer[] = {
	REGULATOR_SUPPLY("vdd_alive", NULL),	/* ALIVE */
};

static struct regulator_consumer_supply __initdata max77686_ldo3_consumer[] = {
};

static struct regulator_consumer_supply __initdata max77686_ldo4_consumer[] = {
};

static struct regulator_consumer_supply __initdata max77686_ldo5_consumer[] = {
};

static struct regulator_consumer_supply __initdata max77686_ldo8_consumer[] = {
	REGULATOR_SUPPLY("vdd", "exynos4-hdmi"),        /* HDMI */
	REGULATOR_SUPPLY("vdd_pll", "exynos4-hdmi"),    /* HDMI */
};

static struct regulator_consumer_supply __initdata max77686_ldo10_consumer[] = {
	REGULATOR_SUPPLY("vdd_osc", "exynos4-hdmi"),    /* HDMI */
};

static struct regulator_consumer_supply __initdata max77686_ldo11_consumer[] = {
};

static struct regulator_consumer_supply __initdata max77686_ldo13_consumer[] = {
};

static struct regulator_consumer_supply __initdata max77686_ldo14_consumer[] = {
};

static struct regulator_consumer_supply __initdata max77686_ldo17_consumer[] = {
};

static struct regulator_consumer_supply __initdata max77686_ldo18_consumer[] = {
};

static struct regulator_consumer_supply __initdata max77686_ldo19_consumer[] = {
};

static struct regulator_consumer_supply __initdata max77686_ldo21_consumer[] = {
	REGULATOR_SUPPLY("vqmmc", "exynos4-sdhci.2"),
};

static struct regulator_consumer_supply __initdata max77686_ldo23_consumer[] = {
};

static struct regulator_consumer_supply __initdata max77686_ldo24_consumer[] = {
};

static struct regulator_consumer_supply __initdata max77686_ldo25_consumer[] = {
	REGULATOR_SUPPLY("vddq_lcd", "platform-lcd"),
};

static struct regulator_consumer_supply __initdata max77686_ldo26_consumer[] = {
};

static struct regulator_init_data __initdata max77686_buck1_data = {
	.constraints = {
		.name		= "VDD_MIF_1.0V",
		.min_uV		= 800000,
		.max_uV		= 1050000,
		.always_on	= 1,
		.boot_on	= 1,
		.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE |
				REGULATOR_CHANGE_STATUS,
	},
	.num_consumer_supplies = ARRAY_SIZE(max77686_buck1_consumer),
	.consumer_supplies = max77686_buck1_consumer,
};

static struct regulator_init_data __initdata max77686_buck2_data = {
	.constraints = {
		.name		= "VDD_ARM_1.3V",
		.min_uV		= 800000,
		.max_uV		= 1350000,
		.always_on	= 1,
		.boot_on	= 1,
		.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE,
	},
	.num_consumer_supplies = ARRAY_SIZE(max77686_buck2_consumer),
	.consumer_supplies = max77686_buck2_consumer,
};

static struct regulator_init_data __initdata max77686_buck3_data = {
	.constraints = {
		.name		= "VDD_INT_1.0V",
		.min_uV		= 800000,
		.max_uV		= 1150000,
		.always_on	= 1,
		.boot_on	= 1,
		.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE,
	},
	.num_consumer_supplies = ARRAY_SIZE(max77686_buck3_consumer),
	.consumer_supplies = max77686_buck3_consumer,
};

static struct regulator_init_data __initdata max77686_buck4_data = {
	.constraints = {
		.name		= "VDD_G3D_1.0V",
		.min_uV		= 850000,
		.max_uV		= 1200000,
		.boot_on	= 1,
		.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE |
				  REGULATOR_CHANGE_STATUS,
		.state_mem = {
			.disabled = 1,
		},
	},
	.num_consumer_supplies = ARRAY_SIZE(max77686_buck4_consumer),
	.consumer_supplies = max77686_buck4_consumer,
};

static struct regulator_init_data __initdata max77686_buck5_data = {
	.constraints = {
		.name		= "VDDQ_CKEM1M2_1.2V",
		.min_uV		= 1200000,
		.max_uV		= 1200000,
		.always_on	= 1,
		.apply_uV	= 1,
		.valid_ops_mask = REGULATOR_CHANGE_STATUS,
		.state_mem = {
			.uV	= 1200000,
			.mode	= REGULATOR_MODE_NORMAL,
			.enabled = 1,
		},
	},
};

static struct regulator_init_data __initdata max77686_buck6_data = {
	.constraints = {
		.name		= "VDD_INL_1.35V",
		.min_uV		= 1350000,
		.max_uV		= 1350000,
		.always_on	= 1,
		.apply_uV	= 1,
		.valid_ops_mask = REGULATOR_CHANGE_STATUS,
		.state_mem = {
			.uV	= 1350000,
			.mode	= REGULATOR_MODE_NORMAL,
			.enabled = 1,
		},
	},
};

static struct regulator_init_data __initdata max77686_buck7_data = {
	.constraints = {
		.name		= "VDD_INL_2.0V",
		.min_uV		= 2000000,
		.max_uV		= 2000000,
		.always_on	= 1,
		.apply_uV	= 1,
		.valid_ops_mask = REGULATOR_CHANGE_STATUS,
		.state_mem = {
			.uV	= 2000000,
			.mode	= REGULATOR_MODE_NORMAL,
			.enabled = 1,
		},
	},
};

static struct regulator_init_data __initdata max77686_buck8_data = {
	.constraints = {
		.name		= "VDD_BUCK8_2.8V",
		.min_uV		= 2800000,
		.max_uV		= 2800000,
		.always_on	= 1,
		.apply_uV	= 1,
		.valid_ops_mask = REGULATOR_CHANGE_STATUS,
		.state_mem = {
			.uV	= 2800000,
			.mode	= REGULATOR_MODE_NORMAL,
			.enabled = 1,
		},
	},
};

static struct regulator_init_data __initdata max77686_buck9_data = {
	.constraints = {
		.name		= "VDD_BUCK9_1.2V",
		.min_uV		= 1200000,
		.max_uV		= 1200000,
		.always_on	= 1,
		.apply_uV	= 1,
		.valid_ops_mask = REGULATOR_CHANGE_STATUS,
		.state_mem = {
			.uV	= 1200000,
			.mode	= REGULATOR_MODE_NORMAL,
			.enabled = 1,
		},
	},
};

static struct regulator_init_data __initdata max77686_ldo1_data = {
	.constraints = {
		.name		= "VDD_ALIVE_1.0V",
		.min_uV		= 1000000,
		.max_uV		= 1000000,
		.apply_uV	= 1,
		.always_on	= 1,
		.valid_ops_mask = REGULATOR_CHANGE_STATUS,
		.state_mem = {
			.uV	= 1000000,
			.enabled = 1,
		},
	},
	.num_consumer_supplies = ARRAY_SIZE(max77686_ldo1_consumer),
	.consumer_supplies = max77686_ldo1_consumer,
};

static struct regulator_init_data __initdata max77686_ldo2_data = {
	.constraints = {
		.name		= "VDDQ_M1M2_1.8V",
		.min_uV		= 1800000,
		.max_uV		= 1800000,
		.apply_uV	= 1,
		.always_on	= 1,
		.valid_ops_mask = REGULATOR_CHANGE_STATUS,
		.state_mem = {
			.uV	= 1800000,
			.enabled = 1,
		},
	},
};

static struct regulator_init_data __initdata max77686_ldo3_data = {
	.constraints = {
		.name		= "VDDQ_M0_1.8V",
		.min_uV		= 1800000,
		.max_uV		= 1800000,
		.apply_uV	= 1,
		.always_on	= 1,
		.valid_ops_mask = REGULATOR_CHANGE_STATUS,
		.state_mem = {
			.uV	= 1800000,
			.enabled = 1,
		},
	},
	.num_consumer_supplies = ARRAY_SIZE(max77686_ldo3_consumer),
	.consumer_supplies = max77686_ldo3_consumer,
};

static struct regulator_init_data __initdata max77686_ldo4_data = {
	.constraints = {
		.name		= "VDDQ_MMC2_2.8V",
		.min_uV		= 2800000,
		.max_uV		= 2800000,
		.apply_uV	= 1,
		.always_on	= 1,
		.valid_ops_mask = REGULATOR_CHANGE_STATUS,
		.state_mem = {
			.uV	= 2800000,
			.enabled = 1,
		},
	},
	.num_consumer_supplies = ARRAY_SIZE(max77686_ldo4_consumer),
	.consumer_supplies = max77686_ldo4_consumer,
};

static struct regulator_init_data __initdata max77686_ldo5_data = {
	.constraints = {
		.name		= "VDDQ_MMC13_1V8",
		.min_uV		= 1800000,
		.max_uV		= 1800000,
		.apply_uV	= 1,
		.always_on	= 1,
		.valid_ops_mask = REGULATOR_CHANGE_STATUS,
		.state_mem = {
			.uV	= 1800000,
			.enabled = 1,
		},
	},
	.num_consumer_supplies = ARRAY_SIZE(max77686_ldo5_consumer),
	.consumer_supplies = max77686_ldo5_consumer,
};

static struct regulator_init_data __initdata max77686_ldo6_data = {
	.constraints = {
		.name		= "VDD_MPLL_1.0V",
		.min_uV		= 1000000,
		.max_uV		= 1000000,
		.apply_uV	= 1,
		.always_on	= 1,
		.valid_ops_mask = REGULATOR_CHANGE_STATUS,
		.state_mem = {
			.uV	= 1000000,
			.enabled = 1,
		},
	},
};

static struct regulator_init_data __initdata max77686_ldo7_data = {
	.constraints = {
		.name		= "VDD_VPLL_1.0V",
		.min_uV		= 1000000,
		.max_uV		= 1000000,
		.apply_uV	= 1,
		.always_on	= 1,
		.valid_ops_mask = REGULATOR_CHANGE_STATUS,
		.state_mem = {
			.uV	= 1000000,
			.enabled = 1,
		},
	},
};

static struct regulator_init_data __initdata max77686_ldo8_data = {
	.constraints = {
		.name		= "VDD10_HDMI_1.0V",
		.min_uV		= 1000000,
		.max_uV		= 1000000,
		.apply_uV	= 1,
		.always_on	= 1,
		.valid_ops_mask = REGULATOR_CHANGE_STATUS,
		.state_mem = {
			.uV	= 1000000,
			.enabled = 1,
		},
	},
	.num_consumer_supplies = ARRAY_SIZE(max77686_ldo8_consumer),
	.consumer_supplies = max77686_ldo8_consumer,
};

static struct regulator_init_data __initdata max77686_ldo9_data = {
	.constraints = {
		.name		= "VDD_VTCORE_1.0V",
		.min_uV		= 1000000,
		.max_uV		= 1000000,
		.apply_uV	= 1,
		.always_on	= 1,
		.valid_ops_mask = REGULATOR_CHANGE_STATUS,
		.state_mem = {
			.uV	= 1000000,
			.enabled = 1,
		},
	},
};

static struct regulator_init_data __initdata max77686_ldo10_data = {
	.constraints = {
		.name		= "VDDQ_MIPIHSI_1.8V",
		.min_uV		= 1800000,
		.max_uV		= 1800000,
		.apply_uV	= 1,
		.always_on	= 1,
		.valid_ops_mask = REGULATOR_CHANGE_STATUS,
		.state_mem = {
			.uV	= 1800000,
			.enabled = 1,
		},
	},
	.num_consumer_supplies = ARRAY_SIZE(max77686_ldo10_consumer),
	.consumer_supplies = max77686_ldo10_consumer,
};

static struct regulator_init_data __initdata max77686_ldo11_data = {
	.constraints = {
		.name		= "VDD18_ABB1_1.8V",
		.min_uV		= 1800000,
		.max_uV		= 1800000,
		.apply_uV	= 1,
		.always_on	= 1,
		.state_mem = {
			.uV	= 1800000,
			.enabled = 1,
		},
	},
	.num_consumer_supplies	= ARRAY_SIZE(max77686_ldo11_consumer),
	.consumer_supplies	= max77686_ldo11_consumer,
};

static struct regulator_init_data __initdata max77686_ldo12_data = {
	.constraints = {
		.name		= "VDD33_UOTG_3.3V",
		.min_uV		= 3300000,
		.max_uV		= 3300000,
		.apply_uV	= 1,
		.always_on	= 1,
		.valid_ops_mask = REGULATOR_CHANGE_STATUS,
		.state_mem = {
			.uV	= 3300000,
			.enabled = 1,
		},
	},
};

static struct regulator_init_data __initdata max77686_ldo13_data = {
	.constraints = {
		.name		= "VDDQ_C2C_1.8V",
		.min_uV		= 1800000,
		.max_uV		= 1800000,
		.apply_uV	= 1,
		.always_on	= 1,
		.valid_ops_mask = REGULATOR_CHANGE_STATUS,
		.state_mem = {
			.uV	= 1800000,
			.enabled = 1,
		},
	},
	.num_consumer_supplies = ARRAY_SIZE(max77686_ldo13_consumer),
	.consumer_supplies = max77686_ldo13_consumer,
};

static struct regulator_init_data __initdata max77686_ldo14_data = {
	.constraints = {
		.name		= "VDD18_ABB2_1.8V",
		.min_uV		= 1800000,
		.max_uV		= 1800000,
		.apply_uV	= 1,
		.always_on	= 1,
		.state_mem = {
			.uV	= 1800000,
			.enabled = 1,
		},
	},
	.num_consumer_supplies	= ARRAY_SIZE(max77686_ldo14_consumer),
	.consumer_supplies	= max77686_ldo14_consumer,
};

static struct regulator_init_data __initdata max77686_ldo15_data = {
	.constraints = {
		.name		= "VDD10_HSIC_1.0V",
		.min_uV		= 1000000,
		.max_uV		= 1000000,
		.apply_uV	= 1,
		.always_on	= 1,
		.valid_ops_mask = REGULATOR_CHANGE_STATUS,
		.state_mem = {
			.uV	= 1000000,
			.enabled = 1,
		},
	},
};

static struct regulator_init_data __initdata max77686_ldo16_data = {
	.constraints = {
		.name		= "VDD18_HSIC_1.8V",
		.min_uV		= 1800000,
		.max_uV		= 1800000,
		.apply_uV	= 1,
		.always_on	= 1,
		.valid_ops_mask = REGULATOR_CHANGE_STATUS,
		.state_mem = {
			.uV	= 1800000,
			.enabled = 1,
		},
	},
};

static struct regulator_init_data __initdata max77686_ldo17_data = {
	.constraints = {
		.name		= "VDDQ_CAM_1.8V",
		.min_uV		= 1800000,
		.max_uV		= 1800000,
		.apply_uV	= 1,
		.always_on	= 1,
		.valid_ops_mask = REGULATOR_CHANGE_STATUS,
		.state_mem = {
			.uV	= 1800000,
			.enabled = 1,
		},
	},
	.num_consumer_supplies = ARRAY_SIZE(max77686_ldo17_consumer),
	.consumer_supplies = max77686_ldo17_consumer,
};

static struct regulator_init_data __initdata max77686_ldo18_data = {
	.constraints = {
		.name		= "VDD_LDO18_1.8V",
		.min_uV		= 1800000,
		.max_uV		= 1800000,
		.apply_uV	= 1,
		.always_on	= 1,
		.valid_ops_mask = REGULATOR_CHANGE_STATUS,
		.state_mem = {
			.uV	= 1800000,
			.enabled = 1,
		},
	},
	.num_consumer_supplies = ARRAY_SIZE(max77686_ldo18_consumer),
	.consumer_supplies = max77686_ldo18_consumer,
};

static struct regulator_init_data __initdata max77686_ldo19_data = {
	.constraints = {
		.name		= "VDD_VTCAM_1.8V",
		.min_uV		= 1800000,
		.max_uV		= 1800000,
		.apply_uV	= 0,
		.always_on	= 0,
		.boot_on	= 0,
		.valid_ops_mask = REGULATOR_CHANGE_STATUS,
		.state_mem = {
			.uV	= 1800000,
			.enabled = 0,
		},
	},
	.num_consumer_supplies = ARRAY_SIZE(max77686_ldo19_consumer),
	.consumer_supplies = max77686_ldo19_consumer,
};

static struct regulator_init_data __initdata max77686_ldo20_data = {
	.constraints = {
		.name		= "VDD_LDO20_1V8",
		.min_uV		= 1800000,
		.max_uV		= 1800000,
		.always_on	= 1,
		.boot_on	= 1,
		.valid_ops_mask = REGULATOR_CHANGE_STATUS,
		.state_mem = {
			.uV	= 1800000,
			.enabled = 0,
		},
	},
};

static struct regulator_init_data __initdata max77686_ldo21_data = {
	.constraints = {
		.name		= "VDD_SDCARD_3.3V",
		.min_uV		= 3300000,
		.max_uV		= 3300000,
		.apply_uV	= 1,
		.always_on	= 1,
		.valid_ops_mask = REGULATOR_CHANGE_STATUS,
		.state_mem = {
			.uV	= 3300000,
			.enabled = 1,
		},
	},
	.num_consumer_supplies = ARRAY_SIZE(max77686_ldo21_consumer),
	.consumer_supplies = max77686_ldo21_consumer,
};

static struct regulator_init_data __initdata max77686_ldo22_data = {
	.constraints = {
		.name		= "VDD_LDO22_2.8V",
		.min_uV		= 2800000,
		.max_uV		= 2800000,
		.apply_uV	= 1,
		.always_on	= 0,
		.valid_ops_mask = REGULATOR_CHANGE_STATUS,
		.state_mem = {
			.uV	= 2800000,
			.enabled = 0,
		},
	},
};

static struct regulator_init_data __initdata max77686_ldo23_data = {
	.constraints = {
		.name		= "VDD_TOUCH_2.8V",
		.min_uV		= 2800000,
		.max_uV		= 2800000,
		.apply_uV	= 1,
		.always_on	= 1,
		.valid_ops_mask = REGULATOR_CHANGE_STATUS,
		.state_mem = {
			.uV	= 2800000,
			.enabled = 1,
		},
	},
	.num_consumer_supplies = ARRAY_SIZE(max77686_ldo23_consumer),
	.consumer_supplies = max77686_ldo23_consumer,
};

static struct regulator_init_data __initdata max77686_ldo24_data = {
	.constraints = {
		.name		= "VDD_TOUCHLED_3.3V",
		.min_uV		= 3300000,
		.max_uV		= 3300000,
		.apply_uV	= 1,
		.always_on	= 1,
		.valid_ops_mask = REGULATOR_CHANGE_STATUS,
		.state_mem = {
			.uV	= 3300000,
			.enabled = 1,
		},
	},
	.num_consumer_supplies = ARRAY_SIZE(max77686_ldo24_consumer),
	.consumer_supplies = max77686_ldo24_consumer,
};

static struct regulator_init_data __initdata max77686_ldo25_data = {
	.constraints = {
		.name		= "VDDQ_LCD_1.8V",
		.min_uV		= 1800000,
		.max_uV		= 1800000,
		.boot_on	= 1,
		.apply_uV	= 1,
		.valid_ops_mask = REGULATOR_CHANGE_STATUS,
		.state_mem = {
			.disabled = 1,
		},
	},
	.num_consumer_supplies = ARRAY_SIZE(max77686_ldo25_consumer),
	.consumer_supplies = max77686_ldo25_consumer,
};

static struct regulator_init_data __initdata max77686_ldo26_data = {
	.constraints = {
		.name		= "VDD_MOTOR_3.0V",
		.min_uV		= 3000000,
		.max_uV		= 3000000,
		.apply_uV	= 1,
		.always_on	= 1,
		.valid_ops_mask = REGULATOR_CHANGE_STATUS,
		.state_mem	= {
			.uV	= 3000000,
			.enabled = 1,
		},
	},
	.num_consumer_supplies = ARRAY_SIZE(max77686_ldo26_consumer),
	.consumer_supplies = max77686_ldo26_consumer,
};

struct max77686_opmode_data max77686_opmode_data[MAX77686_REG_MAX] = {
	[MAX77686_LDO11] = {MAX77686_LDO11, MAX77686_OPMODE_STANDBY},
	[MAX77686_LDO14] = {MAX77686_LDO14, MAX77686_OPMODE_STANDBY},
	[MAX77686_BUCK1] = {MAX77686_BUCK1, MAX77686_OPMODE_STANDBY},
	[MAX77686_BUCK2] = {MAX77686_BUCK2, MAX77686_OPMODE_STANDBY},
	[MAX77686_BUCK3] = {MAX77686_BUCK3, MAX77686_OPMODE_STANDBY},
	[MAX77686_BUCK4] = {MAX77686_BUCK4, MAX77686_OPMODE_STANDBY},
};

static struct max77686_regulator_data max77686_regulators[] = {
	{ MAX77686_LDO1,	&max77686_ldo1_data },
	{ MAX77686_LDO2,	&max77686_ldo2_data },
	{ MAX77686_LDO3,	&max77686_ldo3_data },
	{ MAX77686_LDO4,	&max77686_ldo4_data },
	{ MAX77686_LDO5,	&max77686_ldo5_data },
	{ MAX77686_LDO6,	&max77686_ldo6_data },
	{ MAX77686_LDO7,	&max77686_ldo7_data },
	{ MAX77686_LDO8,	&max77686_ldo8_data },
	{ MAX77686_LDO9,	&max77686_ldo9_data },
	{ MAX77686_LDO10,	&max77686_ldo10_data },
	{ MAX77686_LDO11,	&max77686_ldo11_data },
	{ MAX77686_LDO12,	&max77686_ldo12_data },
	{ MAX77686_LDO13,	&max77686_ldo13_data },
	{ MAX77686_LDO14,	&max77686_ldo14_data },
	{ MAX77686_LDO15,	&max77686_ldo15_data },
	{ MAX77686_LDO16,	&max77686_ldo16_data },
	{ MAX77686_LDO17,	&max77686_ldo17_data },
	{ MAX77686_LDO18,	&max77686_ldo18_data },
	{ MAX77686_LDO19,	&max77686_ldo19_data },
	{ MAX77686_LDO20,	&max77686_ldo20_data },
	{ MAX77686_LDO21,	&max77686_ldo21_data },
	{ MAX77686_LDO22,	&max77686_ldo22_data },
	{ MAX77686_LDO23,	&max77686_ldo23_data },
	{ MAX77686_LDO24,	&max77686_ldo24_data },
	{ MAX77686_LDO25,	&max77686_ldo25_data },
	{ MAX77686_LDO26,	&max77686_ldo26_data },

	{ MAX77686_BUCK1,	&max77686_buck1_data },
	{ MAX77686_BUCK2,	&max77686_buck2_data },
	{ MAX77686_BUCK3,	&max77686_buck3_data },
	{ MAX77686_BUCK4,	&max77686_buck4_data },
	{ MAX77686_BUCK5,	&max77686_buck5_data },
	{ MAX77686_BUCK6,	&max77686_buck6_data },
	{ MAX77686_BUCK7,	&max77686_buck7_data },
	{ MAX77686_BUCK8,	&max77686_buck8_data },
	{ MAX77686_BUCK9,	&max77686_buck9_data },
};

static struct max77686_platform_data hkdk4412_max77686_info = {
	.num_regulators	= ARRAY_SIZE(max77686_regulators),
	.regulators	= max77686_regulators,
	.irq_gpio	= 0,
	.wakeup		= 0,

	.opmode_data	= max77686_opmode_data,

	.buck234_gpio_dvs[0]	= EXYNOS4_GPX2(3),
	.buck234_gpio_dvs[1]	= EXYNOS4_GPX2(4),
	.buck234_gpio_dvs[2]	= EXYNOS4_GPX2(5),

	.buck2_voltage[0] = 1300000,	/* 1.3V */
	.buck2_voltage[1] = 1000000,	/* 1.0V */
	.buck2_voltage[2] = 950000,	/* 0.95V */
	.buck2_voltage[3] = 900000,	/* 0.9V */
	.buck2_voltage[4] = 1000000,	/* 1.0V */
	.buck2_voltage[5] = 1000000,	/* 1.0V */
	.buck2_voltage[6] = 950000,	/* 0.95V */
	.buck2_voltage[7] = 900000,	/* 0.9V */

	.buck3_voltage[0] = 1037500,	/* 1.0375V */
	.buck3_voltage[1] = 1000000,	/* 1.0V */
	.buck3_voltage[2] = 950000,	/* 0.95V */
	.buck3_voltage[3] = 900000,	/* 0.9V */
	.buck3_voltage[4] = 1000000,	/* 1.0V */
	.buck3_voltage[5] = 1000000,	/* 1.0V */
	.buck3_voltage[6] = 950000,	/* 0.95V */
	.buck3_voltage[7] = 900000,	/* 0.9V */

	.buck4_voltage[0] = 1100000,	/* 1.1V */
	.buck4_voltage[1] = 1000000,	/* 1.0V */
	.buck4_voltage[2] = 950000,	/* 0.95V */
	.buck4_voltage[3] = 900000,	/* 0.9V */
	.buck4_voltage[4] = 1000000,	/* 1.0V */
	.buck4_voltage[5] = 1000000,	/* 1.0V */
	.buck4_voltage[6] = 950000,	/* 0.95V */
	.buck4_voltage[7] = 900000,	/* 0.9V */
};

enum fixed_regulator_id {
	FIXED_REG_ID_HDMI_5V,
};

#if defined(CONFIG_VIDEO_SAMSUNG_S5P_HDMI)
static struct regulator_consumer_supply __initdata hdmi_fixed_consumer[] = {
	REGULATOR_SUPPLY("hdmi-en", "exynos4-hdmi"),
};

static struct regulator_init_data __initdata hdmi_fixed_voltage_init_data = {
	.constraints	= {
		.name		= "hdmi_5v",
		.valid_ops_mask = REGULATOR_CHANGE_STATUS,
	},
	.num_consumer_supplies  = ARRAY_SIZE(hdmi_fixed_consumer),
	.consumer_supplies      = hdmi_fixed_consumer,
};

static struct fixed_voltage_config __initdata hdmi_fixed_voltage_config = {
	.supply_name	= "hdmi_en",
	.microvolts	= 5000000,
	.gpio		= 0,		/* FIXME : No GPIO candidated */
	.enable_high	= true,
	.init_data	= &hdmi_fixed_voltage_init_data,
};

static struct platform_device hdmi_fixed_voltage = {
	.name	= "reg-fixed-voltage",
	.id	= FIXED_REG_ID_HDMI_5V,
	.dev	= {
		.platform_data  = &hdmi_fixed_voltage_config,
	},
};
#endif

#if defined(CONFIG_USB_HSIC_USB3503)
#include <linux/platform_data/usb3503.h>

static struct usb3503_platform_data usb3503_pdata = {
	.initial_mode	= USB3503_MODE_HUB,
	.gpio_intn	= EXYNOS4_GPX3(0),
	.gpio_connect	= EXYNOS4_GPX3(4),
	.gpio_reset	= EXYNOS4_GPX3(5),
};
#endif

static struct i2c_board_info hkdk4412_i2c_devs0[] __initdata = {
	{
		I2C_BOARD_INFO("max77686", (0x12 >> 1)),
		.platform_data	= &hkdk4412_max77686_info,
	},
#if defined(CONFIG_USB_HSIC_USB3503)
	{
		I2C_BOARD_INFO("usb3503", (0x08)),
		.platform_data  = &usb3503_pdata,
	},
#endif
};

static struct i2c_board_info hkdk4412_i2c_devs1[] __initdata = {
#if defined(CONFIG_SND_SOC_MAX98090)
	{
		I2C_BOARD_INFO("max98090", (0x20>>1)),
	},
#endif
};

static struct i2c_board_info hkdk4412_i2c_devs3[] __initdata = {
	/* nothing here yet */
};

static struct i2c_board_info hkdk4412_i2c_devs7[] __initdata = {
	/* nothing here yet */
};

static struct gpio_led hkdk4412_gpio_leds[] = {
	{
		.name		= "led1",	/* D5 on ODROID-X */
		.default_trigger	= "oneshot",
		.gpio		= EXYNOS4_GPC1(0),
		.active_low	= 1,
	},
	{
		.name		= "led2",	/* D6 on ODROID-X */
		.default_trigger	= "heartbeat",
		.gpio		= EXYNOS4_GPC1(2),
		.active_low	= 1,
	},
};

static struct gpio_led_platform_data hkdk4412_gpio_led_info = {
	.leds		= hkdk4412_gpio_leds,
	.num_leds	= ARRAY_SIZE(hkdk4412_gpio_leds),
};

static struct platform_device hkdk4412_leds_gpio = {
	.name	= "leds-gpio",
	.id	= -1,
	.dev	= {
		.platform_data	= &hkdk4412_gpio_led_info,
	},
};

/* LCD Backlight data */
static struct samsung_bl_gpio_info hkdk4412_bl_gpio_info = {
	.no	= EXYNOS4_GPD0(1),
	.func	= S3C_GPIO_SFN(2),
};

static struct platform_pwm_backlight_data hkdk4412_bl_data = {
	.pwm_id		= 1,
	.pwm_period_ns	= 1000,
};

#if defined(CONFIG_LCD_LP101WH1)
static struct s3c_fb_pd_win hkdk4412_fb_win0 = {
	.max_bpp	= 32,
	.default_bpp	= 24,
	.xres		= 1360,
	.yres		= 768,
};

static struct fb_videomode hkdk4412_lcd_timing = {
	.left_margin	= 80,
	.right_margin	= 48,
	.upper_margin	= 14,
	.lower_margin	= 3,
	.hsync_len	= 32,
	.vsync_len	= 5,
	.xres		= 1360,
	.yres		= 768,
};

static struct s3c_fb_platdata hkdk4412_fb_pdata __initdata = {
	.win[0]		= &hkdk4412_fb_win0,
	.vtiming	= &hkdk4412_lcd_timing,
	.vidcon0	= VIDCON0_VIDOUT_RGB | VIDCON0_PNRMODE_RGB,
	.vidcon1	= VIDCON1_INV_HSYNC | VIDCON1_INV_VSYNC,
	.setup_gpio	= exynos4_fimd0_gpio_setup_24bpp,
};

static void lcd_lp101wh1_set_power(struct plat_lcd_data *pd,
				   unsigned int power)
{
	gpio_request(EXYNOS4_GPA1(3), "bl_enable");
	gpio_direction_output(EXYNOS4_GPA1(3), power);
	gpio_free(EXYNOS4_GPA1(3));
}

static struct plat_lcd_data hkdk4412_lcd_lp101wh1_data = {
	.set_power	= lcd_lp101wh1_set_power,
};

static struct platform_device hkdk4412_lcd_lp101wh1 = {
	.name	= "platform-lcd",
	.dev	= {
		.parent		= &s5p_device_fimd0.dev,
		.platform_data	= &hkdk4412_lcd_lp101wh1_data,
	},
};
#endif

/* GPIO KEYS */
static struct gpio_keys_button hkdk4412_gpio_keys_tables[] = {
	{
		.code			= KEY_POWER,
		.gpio			= EXYNOS4_GPX1(3),	/* XEINT11 */
		.desc			= "KEY_POWER",
		.type			= EV_KEY,
		.active_low		= 1,
		.wakeup			= 1,
		.debounce_interval	= 1,
	},
};

static struct gpio_keys_platform_data hkdk4412_gpio_keys_data = {
	.buttons	= hkdk4412_gpio_keys_tables,
	.nbuttons	= ARRAY_SIZE(hkdk4412_gpio_keys_tables),
};

static struct platform_device hkdk4412_gpio_keys = {
	.name	= "gpio-keys",
	.dev	= {
		.platform_data	= &hkdk4412_gpio_keys_data,
	},
};

#if defined(CONFIG_SND_SOC_HKDK_MAX98090)
static struct platform_device hardkernel_audio_device = {
	.name	= "hkdk-snd-max89090",
	.id	= -1,
};
#endif

/* USB EHCI */
static struct s5p_ehci_platdata hkdk4412_ehci_pdata;

static void __init hkdk4412_ehci_init(void)
{
	struct s5p_ehci_platdata *pdata = &hkdk4412_ehci_pdata;

	s5p_ehci_set_platdata(pdata);
}

/* USB OHCI */
static struct exynos4_ohci_platdata hkdk4412_ohci_pdata;

static void __init hkdk4412_ohci_init(void)
{
	struct exynos4_ohci_platdata *pdata = &hkdk4412_ohci_pdata;

	exynos4_ohci_set_platdata(pdata);
}

/* USB OTG */
static struct s3c_hsotg_plat hkdk4412_hsotg_pdata;

/* SDCARD */
static struct s3c_sdhci_platdata hkdk4412_hsmmc2_pdata __initdata = {
	.max_width	= 4,
	.host_caps	= MMC_CAP_4_BIT_DATA |
			MMC_CAP_MMC_HIGHSPEED | MMC_CAP_SD_HIGHSPEED,
	.cd_type	= S3C_SDHCI_CD_INTERNAL,
};

/* DWMMC */
static int hkdk4412_dwmci_get_bus_wd(u32 slot_id)
{
       return 8;
}

static int hkdk4412_dwmci_init(u32 slot_id, irq_handler_t handler, void *data)
{
       return 0;
}

static struct dw_mci_board hkdk4412_dwmci_pdata = {
       .num_slots              = 1,
       .quirks                 = DW_MCI_QUIRK_BROKEN_CARD_DETECTION,
//       .bus_hz                 = 80 * 1000 * 1000,
       .detect_delay_ms        = 200,
       .init                   = hkdk4412_dwmci_init,
       .get_bus_wd             = hkdk4412_dwmci_get_bus_wd,
};

static struct platform_device *hkdk4412_devices[] __initdata = {
	&s3c_device_hsmmc2,
	&s3c_device_i2c0,
	&s3c_device_i2c1,
	&s3c_device_i2c3,
	&s3c_device_i2c7,
	&s3c_device_rtc,
	&s3c_device_usb_hsotg,
	&s3c_device_wdt,
	&s5p_device_ehci,
#ifdef CONFIG_SND_SAMSUNG_I2S
	&exynos4_device_i2s0,
#endif
	&s5p_device_fimc0,
	&s5p_device_fimc1,
	&s5p_device_fimc2,
	&s5p_device_fimc3,
	&s5p_device_fimc_md,
	&s5p_device_fimd0,
	&s5p_device_mfc,
	&s5p_device_mfc_l,
	&s5p_device_mfc_r,
#if defined(CONFIG_S5P_DEV_TV)
	&s5p_device_hdmi,
	&s5p_device_i2c_hdmiphy,
	&s5p_device_mixer,
	&hdmi_fixed_voltage,
#endif
	&exynos4_device_ohci,
	&exynos4_device_dwmci,
	&hkdk4412_leds_gpio,
#if defined(CONFIG_LCD_LP101WH1)
	&hkdk4412_lcd_lp101wh1,
#endif
	&hkdk4412_gpio_keys,
	&samsung_asoc_dma,
	&samsung_asoc_idma,
#if defined(CONFIG_SND_SOC_HKDK_MAX98090)
	&hardkernel_audio_device,
#endif
};

static void __init hkdk4412_map_io(void)
{
	clk_xusbxti.rate = 24000000;

	exynos_init_io(NULL, 0);
	s3c24xx_init_clocks(clk_xusbxti.rate);
	s3c24xx_init_uarts(hkdk4412_uartcfgs, ARRAY_SIZE(hkdk4412_uartcfgs));
}

static void __init hkdk4412_reserve(void)
{
	s5p_mfc_reserve_mem(0x43000000, 8 << 20, 0x51000000, 8 << 20);
}

#if defined(CONFIG_S5P_DEV_TV)
static void s5p_tv_setup(void)
{
	/* Direct HPD to HDMI chip */
	gpio_request_one(EXYNOS4_GPX3(7), GPIOF_IN, "hpd-plug");
	s3c_gpio_cfgpin(EXYNOS4_GPX3(7), S3C_GPIO_SFN(0x3));
	s3c_gpio_setpull(EXYNOS4_GPX3(7), S3C_GPIO_PULL_NONE);
}

/* I2C module and id for HDMIPHY */
static struct i2c_board_info hdmiphy_info = {
	I2C_BOARD_INFO("hdmiphy-exynos4412", 0x38),
};
#endif

static void __init hkdk4412_gpio_init(void)
{
	/* Peripheral power enable (P3V3) */
	gpio_request_one(EXYNOS4_GPA1(1), GPIOF_OUT_INIT_HIGH, "p3v3_en");

	/* Power on/off button */
	s3c_gpio_cfgpin(EXYNOS4_GPX1(3), S3C_GPIO_SFN(0xF));
	s3c_gpio_setpull(EXYNOS4_GPX1(3), S3C_GPIO_PULL_NONE);
}

static void hkdk4412_power_off(void)
{
	pr_emerg("Bye...\n");

	writel(0x5200, S5P_PS_HOLD_CONTROL);
	while (1) {
		pr_emerg("%s : should not reach here!\n", __func__);
		msleep(1000);
	}
}

static void __init hkdk4412_machine_init(void)
{
	hkdk4412_gpio_init();

	/* Register power off function */
	pm_power_off = hkdk4412_power_off;

	s3c_i2c0_set_platdata(NULL);
	i2c_register_board_info(0, hkdk4412_i2c_devs0,
				ARRAY_SIZE(hkdk4412_i2c_devs0));

	s3c_i2c1_set_platdata(NULL);
	i2c_register_board_info(1, hkdk4412_i2c_devs1,
				ARRAY_SIZE(hkdk4412_i2c_devs1));

	s3c_i2c3_set_platdata(NULL);
	i2c_register_board_info(3, hkdk4412_i2c_devs3,
				ARRAY_SIZE(hkdk4412_i2c_devs3));

	s3c_i2c7_set_platdata(NULL);
	i2c_register_board_info(7, hkdk4412_i2c_devs7,
				ARRAY_SIZE(hkdk4412_i2c_devs7));

	s3c_sdhci2_set_platdata(&hkdk4412_hsmmc2_pdata);

	exynos4_setup_dwmci_cfg_gpio(NULL, MMC_BUS_WIDTH_8);
	exynos4_dwmci_set_platdata(&hkdk4412_dwmci_pdata);

	hkdk4412_ehci_init();
	hkdk4412_ohci_init();
	s3c_hsotg_set_platdata(&hkdk4412_hsotg_pdata);

#if defined(CONFIG_S5P_DEV_TV)
	s5p_tv_setup();
	s5p_i2c_hdmiphy_set_platdata(NULL);
	s5p_hdmi_set_platdata(&hdmiphy_info, NULL, 0);
#endif

	s5p_fimd0_set_platdata(&hkdk4412_fb_pdata);

	samsung_bl_set(&hkdk4412_bl_gpio_info, &hkdk4412_bl_data);

	platform_add_devices(hkdk4412_devices, ARRAY_SIZE(hkdk4412_devices));
}

//MACHINE_START(ODROIDX, "ODROID-X")
MACHINE_START(SMDK4212, "ODROID-X")
	/* Maintainer: Dongjin Kim <dongjin.kim@agreeyamobiity.net> */
	.atag_offset	= 0x100,
	.smp		= smp_ops(exynos_smp_ops),
	.init_irq	= exynos4_init_irq,
	.init_early	= exynos_firmware_init,
	.map_io		= hkdk4412_map_io,
	.handle_irq	= gic_handle_irq,
	.init_machine	= hkdk4412_machine_init,
	.init_late	= exynos_init_late,
	.timer		= &exynos4_timer,
	.restart	= exynos4_restart,
	.reserve	= &hkdk4412_reserve,
MACHINE_END
