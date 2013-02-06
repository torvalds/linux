/*
 * midas-sound.c - Sound Management of MIDAS Project
 *
 *  Copyright (C) 2012 Samsung Electrnoics
 *  JS Park <aitdark.park@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/i2c.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/fixed.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/i2c-gpio.h>
#include <mach/irqs.h>
#include <mach/pmu.h>
#include <plat/iic.h>

#include <plat/gpio-cfg.h>
#ifdef CONFIG_ARCH_EXYNOS5
#include <mach/gpio-p10.h>
#else
#include <mach/gpio-midas.h>
#endif

#ifdef CONFIG_SND_SOC_WM8994
#include <linux/mfd/wm8994/pdata.h>
#include <linux/mfd/wm8994/gpio.h>
#endif

#ifdef CONFIG_FM34_WE395
#include <linux/i2c/fm34_we395.h>
#endif

#ifdef CONFIG_AUDIENCE_ES305
#include <linux/i2c/es305.h>
#endif

static bool midas_snd_mclk_enabled;

#ifdef CONFIG_ARCH_EXYNOS5
#define I2C_NUM_2MIC	4
#define I2C_NUM_CODEC	7
#define SET_PLATDATA_2MIC(i2c_pd)	s3c_i2c4_set_platdata(i2c_pd)
#define SET_PLATDATA_CODEC(i2c_pd)	s3c_i2c7_set_platdata(i2c_pd)
#else /* for CONFIG_ARCH_EXYNOS4 */
#define I2C_NUM_2MIC	6
#define I2C_NUM_CODEC	4
#define SET_PLATDATA_2MIC(i2c_pd)	s3c_i2c6_set_platdata(i2c_pd)
#define SET_PLATDATA_CODEC(i2c_pd)	s3c_i2c4_set_platdata(i2c_pd)
#endif

static DEFINE_SPINLOCK(midas_snd_spinlock);

void midas_snd_set_mclk(bool on, bool forced)
{
	static int use_cnt;

	spin_lock(&midas_snd_spinlock);

	midas_snd_mclk_enabled = on;

	if (midas_snd_mclk_enabled) {
		if (use_cnt++ == 0 || forced) {
			printk(KERN_INFO "Sound: enabled mclk\n");
#ifdef CONFIG_ARCH_EXYNOS5
			exynos5_pmu_xclkout_set(midas_snd_mclk_enabled,
							XCLKOUT_XXTI);
#else /* for CONFIG_ARCH_EXYNOS4 */
			exynos4_pmu_xclkout_set(midas_snd_mclk_enabled,
							XCLKOUT_XUSBXTI);
#endif
			mdelay(10);
		}
	} else {
		if ((--use_cnt <= 0) || forced) {
			printk(KERN_INFO "Sound: disabled mclk\n");
#ifdef CONFIG_ARCH_EXYNOS5
			exynos5_pmu_xclkout_set(midas_snd_mclk_enabled,
							XCLKOUT_XXTI);
#else /* for CONFIG_ARCH_EXYNOS4 */
			exynos4_pmu_xclkout_set(midas_snd_mclk_enabled,
							XCLKOUT_XUSBXTI);
#endif
			use_cnt = 0;
		}
	}

	spin_unlock(&midas_snd_spinlock);

	printk(KERN_INFO "Sound: state: %d, use_cnt: %d\n",
					midas_snd_mclk_enabled, use_cnt);
}

bool midas_snd_get_mclk(void)
{
	return midas_snd_mclk_enabled;
}

#ifdef CONFIG_SND_SOC_WM8994
/* vbatt_devices */
static struct regulator_consumer_supply vbatt_supplies[] = {
	REGULATOR_SUPPLY("LDO1VDD", NULL),
	REGULATOR_SUPPLY("SPKVDD1", NULL),
	REGULATOR_SUPPLY("SPKVDD2", NULL),
};

static struct regulator_init_data vbatt_initdata = {
	.constraints = {
		.always_on = 1,
	},
	.num_consumer_supplies = ARRAY_SIZE(vbatt_supplies),
	.consumer_supplies = vbatt_supplies,
};

static struct fixed_voltage_config vbatt_config = {
	.init_data = &vbatt_initdata,
	.microvolts = 5000000,
	.supply_name = "VBATT",
	.gpio = -EINVAL,
};

struct platform_device vbatt_device = {
	.name = "reg-fixed-voltage",
	.id = -1,
	.dev = {
		.platform_data = &vbatt_config,
	},
};

/* wm1811 ldo1 */
static struct regulator_consumer_supply wm1811_ldo1_supplies[] = {
	REGULATOR_SUPPLY("AVDD1", NULL),
};

static struct regulator_init_data wm1811_ldo1_initdata = {
	.constraints = {
		.name = "WM1811 LDO1",
		.valid_ops_mask = REGULATOR_CHANGE_STATUS,
	},
	.num_consumer_supplies = ARRAY_SIZE(wm1811_ldo1_supplies),
	.consumer_supplies = wm1811_ldo1_supplies,
};

/* wm1811 ldo2 */
static struct regulator_consumer_supply wm1811_ldo2_supplies[] = {
	REGULATOR_SUPPLY("DCVDD", NULL),
};

static struct regulator_init_data wm1811_ldo2_initdata = {
	.constraints = {
		.name = "WM1811 LDO2",
		.always_on = true, /* Actually status changed by LDO1 */
	},
	.num_consumer_supplies = ARRAY_SIZE(wm1811_ldo2_supplies),
	.consumer_supplies = wm1811_ldo2_supplies,
};

static struct wm8994_drc_cfg drc_value[] = {
	{
		.name = "voice call DRC",
		.regs[0] = 0x009B,
		.regs[1] = 0x0844,
		.regs[2] = 0x00E8,
		.regs[3] = 0x0210,
		.regs[4] = 0x0000,
	},
#if defined(CONFIG_MACH_C1_KOR_LGT)
	{
		.name = "voice call DRC",
		.regs[0] = 0x008c,
		.regs[1] = 0x0253,
		.regs[2] = 0x0028,
		.regs[3] = 0x028c,
		.regs[4] = 0x0000,
	},
#endif
#if defined(CONFIG_MACH_P4NOTE)
{
		.name = "cam rec DRC",
		.regs[0] = 0x019B,
		.regs[1] = 0x0844,
		.regs[2] = 0x0408,
		.regs[3] = 0x0108,
		.regs[4] = 0x0120,
	},
#endif
};

static struct wm8994_pdata wm1811_pdata = {
	.gpio_defaults = {
		[0] = WM8994_GP_FN_IRQ,	  /* GPIO1 IRQ output, CMOS mode */
		[7] = WM8994_GPN_DIR | WM8994_GP_FN_PIN_SPECIFIC, /* DACDAT3 */
		[8] = WM8994_CONFIGURE_GPIO |
		      WM8994_GP_FN_PIN_SPECIFIC, /* ADCDAT3 */
		[9] = WM8994_CONFIGURE_GPIO |\
		      WM8994_GP_FN_PIN_SPECIFIC, /* LRCLK3 */
		[10] = WM8994_CONFIGURE_GPIO |\
		       WM8994_GP_FN_PIN_SPECIFIC, /* BCLK3 */
	},

	.irq_base = IRQ_BOARD_CODEC_START,

	/* The enable is shared but assign it to LDO1 for software */
	.ldo = {
		{
			.enable = GPIO_WM8994_LDO,
			.init_data = &wm1811_ldo1_initdata,
		},
		{
			.init_data = &wm1811_ldo2_initdata,
		},
	},
	/* Apply DRC Value */
	.drc_cfgs = drc_value,
	.num_drc_cfgs = ARRAY_SIZE(drc_value),

	/* Support external capacitors*/
	.jd_ext_cap = 1,

	/* Regulated mode at highest output voltage */
#ifdef CONFIG_TARGET_LOCALE_KOR
	.micbias = {0x22, 0x22},
#else
	.micbias = {0x2f, 0x2f},
#endif

	.micd_lvl_sel = 0xFF,

	.ldo_ena_always_driven = true,
	.ldo_ena_delay = 30000,
#ifdef CONFIG_TARGET_LOCALE_KOR
	.lineout2_diff = 0,
#endif
#ifdef CONFIG_MACH_C1
	.lineout1fb = 0,
#else
	.lineout1fb = 1,
#endif
#if defined(CONFIG_MACH_M0) || defined(CONFIG_MACH_C1_KOR_SKT) || \
	defined(CONFIG_MACH_C1_KOR_KT) || defined(CONFIG_MACH_C1_KOR_LGT) || \
	defined(CONFIG_MACH_P4NOTE) || defined(CONFIG_MACH_GC1) \
	|| defined(CONFIG_MACH_GRANDE) || defined(CONFIG_MACH_IRON)
	.lineout2fb = 0,
#else
	.lineout2fb = 1,
#endif
};

static struct i2c_board_info i2c_wm1811[] __initdata = {
	{
		I2C_BOARD_INFO("wm1811", (0x34 >> 1)),	/* Audio CODEC */
		.platform_data = &wm1811_pdata,
		.irq = IRQ_EINT(30),
	},
};

#endif

#ifdef CONFIG_FM34_WE395
static struct fm34_platform_data fm34_we395_pdata = {
	.gpio_pwdn = GPIO_FM34_PWDN,
	.gpio_rst = GPIO_FM34_RESET,
	.gpio_bp = GPIO_FM34_BYPASS,
	.set_mclk = midas_snd_set_mclk,
};
#ifdef CONFIG_MACH_C1_KOR_LGT
static struct fm34_platform_data fm34_we395_pdata_rev05 = {
	.gpio_pwdn = GPIO_FM34_PWDN,
	.gpio_rst = GPIO_FM34_RESET_05,
	.gpio_bp = GPIO_FM34_BYPASS_05,
	.set_mclk = midas_snd_set_mclk,
};
#endif
static struct i2c_board_info i2c_2mic[] __initdata = {
	{
		I2C_BOARD_INFO("fm34_we395", (0xC0 >> 1)), /* 2MIC */
		.platform_data = &fm34_we395_pdata,
	},
};

#if defined(CONFIG_MACH_C1_KOR_LGT)
static struct i2c_gpio_platform_data gpio_i2c_fm34 = {
	.sda_pin = GPIO_FM34_SDA,
	.scl_pin = GPIO_FM34_SCL,
};

struct platform_device s3c_device_fm34 = {
	.name = "i2c-gpio",
	.id = I2C_NUM_2MIC,
	.dev.platform_data = &gpio_i2c_fm34,
};
#endif
#endif

#ifdef CONFIG_AUDIENCE_ES305
static struct es305_platform_data es305_pdata = {
	.gpio_wakeup = GPIO_ES305_WAKEUP,
	.gpio_reset = GPIO_ES305_RESET,
	.set_mclk = midas_snd_set_mclk,
};

static struct i2c_board_info i2c_2mic[] __initdata = {
	{
		I2C_BOARD_INFO("audience_es305", 0x3E), /* 2MIC */
		.platform_data = &es305_pdata,
	},
};
#endif

static struct platform_device *midas_sound_devices[] __initdata = {
#if defined(CONFIG_MACH_C1_KOR_LGT)
#ifdef CONFIG_FM34_WE395
	&s3c_device_fm34,
#endif
#endif
};

void __init midas_sound_init(void)
{
	printk(KERN_INFO "Sound: start %s\n", __func__);

	platform_add_devices(midas_sound_devices,
		ARRAY_SIZE(midas_sound_devices));

#ifdef CONFIG_ARCH_EXYNOS5
#ifndef CONFIG_MACH_P10_LTE_00_BD
	i2c_wm1811[0].irq = IRQ_EINT(29);
#endif
	SET_PLATDATA_CODEC(NULL);
	i2c_register_board_info(I2C_NUM_CODEC, i2c_wm1811,
					ARRAY_SIZE(i2c_wm1811));
#else /* for CONFIG_ARCH_EXYNOS4 */
	i2c_wm1811[0].irq = 0;
	SET_PLATDATA_CODEC(NULL);
	i2c_register_board_info(I2C_NUM_CODEC, i2c_wm1811,
					ARRAY_SIZE(i2c_wm1811));
#endif/* CONFIG_ARCH_EXYNOS5 */

#ifdef CONFIG_FM34_WE395
	midas_snd_set_mclk(true, false);
	SET_PLATDATA_2MIC(NULL);

#if defined(CONFIG_MACH_C1_KOR_LGT)
	if (system_rev > 5)
		i2c_2mic[0].platform_data = &fm34_we395_pdata_rev05;
#endif

	i2c_register_board_info(I2C_NUM_2MIC, i2c_2mic, ARRAY_SIZE(i2c_2mic));
#endif


#ifdef CONFIG_AUDIENCE_ES305
	midas_snd_set_mclk(true, false);
	SET_PLATDATA_2MIC(NULL);
	i2c_register_board_info(I2C_NUM_2MIC, i2c_2mic, ARRAY_SIZE(i2c_2mic));
#endif
}
