/*
 * t0-sound.c - Sound Management of T0 Project
 *
 *  Copyright (C) 2012 Samsung Electrnoics
 *  Uk Kim <w0806.kim@samsung.com>
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
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <mach/irqs.h>
#include <mach/pmu.h>
#include <plat/iic.h>

#include <plat/gpio-cfg.h>
#include <mach/gpio-midas.h>

#ifdef CONFIG_SND_SOC_WM8994
#include <linux/mfd/wm8994/pdata.h>
#include <linux/mfd/wm8994/gpio.h>
#endif

#if defined(CONFIG_FM_SI4705)
#include <linux/i2c/si47xx_common.h>
#endif

#include <linux/exynos_audio.h>

static bool midas_snd_mclk_enabled;

#if defined(CONFIG_FM_SI4705)
struct si47xx_info {
	int gpio_int;
	int gpio_rst;
} si47xx_data;

#endif

#define I2C_NUM_CODEC	4
#define SET_PLATDATA_CODEC(i2c_pd)	s3c_i2c4_set_platdata(i2c_pd)

static DEFINE_SPINLOCK(midas_snd_spinlock);

void midas_snd_set_mclk(bool on, bool forced)
{
	static int use_cnt;

	spin_lock(&midas_snd_spinlock);

	midas_snd_mclk_enabled = on;

	if (midas_snd_mclk_enabled) {
		if (use_cnt++ == 0 || forced) {
			pr_info("Sound: enabled mclk\n");
			exynos4_pmu_xclkout_set(midas_snd_mclk_enabled,
							XCLKOUT_XUSBXTI);
			mdelay(10);
		}
	} else {
		if ((--use_cnt <= 0) || forced) {
			pr_info("Sound: disabled mclk\n");
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

	pr_info("Sound: state: %d, use_cnt: %d\n",
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
	.micbias = {0x2f, 0x2b},

	.micd_lvl_sel = 0xFF,

	.ldo_ena_always_driven = true,
	.ldo_ena_delay = 30000,

	.lineout1fb = 1,

	.lineout2fb = 0,
};

static struct i2c_board_info i2c_wm1811[] __initdata = {
	{
		I2C_BOARD_INFO("wm1811", (0x34 >> 1)),	/* Audio CODEC */
		.platform_data = &wm1811_pdata,
		.irq = IRQ_EINT(30),
	},
};

#endif

#if defined(CONFIG_FM_SI4705)
static void fmradio_power(int on)
{
	if (on) {
		gpio_request(GPIO_FM_INT, "FMRAIDO INT");
		gpio_direction_output(GPIO_FM_INT, 1);
		gpio_set_value(si47xx_data.gpio_rst, GPIO_LEVEL_LOW);
		gpio_set_value(GPIO_FM_INT, GPIO_LEVEL_LOW);
		usleep_range(5, 10);
		gpio_set_value(si47xx_data.gpio_rst, GPIO_LEVEL_HIGH);
		usleep_range(10, 15);
		gpio_set_value(GPIO_FM_INT, GPIO_LEVEL_HIGH);

		s3c_gpio_cfgpin(GPIO_FM_INT, S3C_GPIO_SFN(0xF));
		gpio_free(GPIO_FM_INT);
	} else {
		gpio_set_value(si47xx_data.gpio_rst, GPIO_LEVEL_LOW);
	}
}

static struct si47xx_platform_data si47xx_pdata = {
	.rx_vol = {0x0, 0x13, 0x16, 0x19, 0x1C, 0x1F, 0x22, 0x25,
		0x28, 0x2B, 0x2E, 0x31, 0x34, 0x37, 0x3A, 0x3D},
	.power = fmradio_power,

};

static struct i2c_gpio_platform_data gpio_i2c_data19 = {
	.sda_pin = GPIO_FM_SDA,
	.scl_pin = GPIO_FM_SCL,
};

struct platform_device s3c_device_i2c19 = {
	.name = "i2c-gpio",
	.id = 19,
	.dev.platform_data = &gpio_i2c_data19,
};

static struct i2c_board_info i2c_devs19_emul[] __initdata = {
	{
		I2C_BOARD_INFO("Si47xx", (0x22 >> 1)),
		.platform_data = &si47xx_pdata,
		.irq = IRQ_EINT(11),
	},
};
#endif

static void t0_gpio_init(void)
{
	int err;
	unsigned int gpio;

#ifdef CONFIG_SND_SOC_USE_EXTERNAL_MIC_BIAS
	/* Main Microphone BIAS */
	err = gpio_request(GPIO_MIC_BIAS_EN, "MAIN MIC");
	if (err) {
		pr_err(KERN_ERR "MIC_BIAS_EN GPIO set error!\n");
		return;
	}
	gpio_direction_output(GPIO_MIC_BIAS_EN, 0);
	gpio_free(GPIO_MIC_BIAS_EN);
#endif

#ifdef CONFIG_SND_USE_SUB_MIC
	/* Sub Microphone BIAS */
	err = gpio_request(GPIO_SUB_MIC_BIAS_EN, "SUB MIC");
	if (err) {
		pr_err(KERN_ERR "SUB_MIC_BIAS_EN GPIO set error!\n");
		return;
	}
	gpio_direction_output(GPIO_SUB_MIC_BIAS_EN, 0);
	gpio_free(GPIO_SUB_MIC_BIAS_EN);
#endif

#ifdef CONFIG_SND_USE_LINEOUT_SWITCH
	err = gpio_request(GPIO_VPS_SOUND_EN, "LINEOUT_EN");
	if (err) {
		pr_err(KERN_ERR "LINEOUT_EN GPIO set error!\n");
		return;
	}
	gpio_direction_output(GPIO_VPS_SOUND_EN, 0);
	gpio_free(GPIO_VPS_SOUND_EN);
#endif

#if defined(CONFIG_SND_DUOS_MODEM_SWITCH)
	/* Modem selection for DUOS model */
	err = gpio_request(GPIO_AUDIO_PCM_SEL, "PCM_SEL");
	if (err) {
		pr_err(KERN_ERR "PCM switch GPIO set error!\n");
		return;
	}
	gpio_direction_output(GPIO_AUDIO_PCM_SEL, 0);
	gpio_free(GPIO_AUDIO_PCM_SEL);
#endif

#ifdef CONFIG_JACK_GROUND_DET
	if (system_rev >= 3)
		gpio = GPIO_G_DET_N_REV03;
	else
		gpio = GPIO_G_DET_N;

	err = gpio_request(gpio, "GROUND DET");
	if (err) {
		pr_err(KERN_ERR "G_DET_N GPIO set error!\n");
		return;
	}
	s3c_gpio_setpull(gpio, S3C_GPIO_PULL_NONE);
	s5p_register_gpio_interrupt(gpio);
	irq_set_irq_type(gpio_to_irq(gpio), IRQF_TRIGGER_FALLING |
			IRQF_TRIGGER_RISING | IRQF_ONESHOT);
	s3c_gpio_cfgpin(gpio, S3C_GPIO_SFN(0xf));
#endif

#ifdef CONFIG_JACK_FET
	if (system_rev >= 4) {
		err = gpio_request(GPIO_EAR_BIAS_DISCHARGE, "EAR DISCHARGE");
		if (err) {
			pr_err("EAR_BIAS_DISCHARGE GPIO  set error!\n");
			return;
		}
		gpio_direction_output(GPIO_EAR_BIAS_DISCHARGE, 0);
		gpio_free(GPIO_EAR_BIAS_DISCHARGE);
	}

#endif

#ifdef CONFIG_FM_SI4705
	if (system_rev >= 3)
		si47xx_data.gpio_rst = GPIO_FM_RST_REV03;

	if (gpio_is_valid(si47xx_data.gpio_rst)) {
		if (gpio_request(si47xx_data.gpio_rst, "FM_RST"))
			debug(KERN_ERR "Failed to request "
			"FM_RST!\n\n");
		gpio_direction_output(si47xx_data.gpio_rst, GPIO_LEVEL_LOW);
	}
#endif
}

static void t0_set_lineout_switch(int on)
{
#ifdef CONFIG_SND_USE_LINEOUT_SWITCH
	gpio_set_value(GPIO_VPS_SOUND_EN, on);
	pr_info("%s: lineout switch on = %d\n", __func__, on);
#endif
}

static void t0_set_ext_main_mic(int on)
{
#ifdef CONFIG_SND_SOC_USE_EXTERNAL_MIC_BIAS
	/* Main Microphone BIAS */
	gpio_set_value(GPIO_MIC_BIAS_EN, on);

	pr_info("%s: main_mic bias on = %d\n", __func__, on);
#endif
}

static void t0_set_ext_sub_mic(int on)
{
#ifdef CONFIG_SND_USE_SUB_MIC
	/* Sub Microphone BIAS */
	gpio_set_value(GPIO_SUB_MIC_BIAS_EN, on);

	pr_info("%s: sub_mic bias on = %d\n", __func__, on);
#endif
}

#ifdef CONFIG_JACK_GROUND_DET
static int t0_get_ground_det_value(void)
{
	unsigned int g_det_gpio;

	if (system_rev >= 3)
		g_det_gpio = GPIO_G_DET_N_REV03;
	else
		g_det_gpio = GPIO_G_DET_N;
	return gpio_get_value(g_det_gpio);
}

static int t0_get_ground_det_irq_num(void)
{
	unsigned int g_det_gpio;

	if (system_rev >= 3)
		g_det_gpio = GPIO_G_DET_N_REV03;
	else
		g_det_gpio = GPIO_G_DET_N;
	return gpio_to_irq(g_det_gpio);
}
#endif

#if defined(CONFIG_SND_DUOS_MODEM_SWITCH)
static void t0_set_modem_switch(int on)
{
	/* Modem selection for DUOS model */
	gpio_set_value(GPIO_AUDIO_PCM_SEL, on);
	pr_info("%s: t0_set_modem_switch = %d\n", __func__, on);
}
#endif

struct exynos_sound_platform_data t0_sound_pdata __initdata = {
	.set_lineout_switch	= t0_set_lineout_switch,
	.set_ext_main_mic	= t0_set_ext_main_mic,
	.set_ext_sub_mic	= t0_set_ext_sub_mic,
#ifdef CONFIG_JACK_GROUND_DET
	.get_ground_det_value	= t0_get_ground_det_value,
	.get_ground_det_irq_num = t0_get_ground_det_irq_num,
#endif
#if defined(CONFIG_SND_DUOS_MODEM_SWITCH)
	.set_modem_switch = t0_set_modem_switch,
#endif
	.dcs_offset_l = -9,
	.dcs_offset_r = -7,
};

static struct platform_device *t0_sound_devices[] __initdata = {
#if defined(CONFIG_FM_SI4705)
	&s3c_device_i2c19,
#endif
};

void __init midas_sound_init(void)
{
	pr_info("Sound: start %s\n", __func__);

#if defined(CONFIG_MACH_T0_EUR_LTE)
	t0_sound_pdata.dcs_offset_l = -11;
	t0_sound_pdata.dcs_offset_r = -8;
#elif defined(CONFIG_MACH_T0_USA_VZW)
	t0_sound_pdata.dcs_offset_l = -12;
	t0_sound_pdata.dcs_offset_r = -9;
#elif defined(CONFIG_MACH_T0_USA_ATT)
	t0_sound_pdata.dcs_offset_l = -13;
	t0_sound_pdata.dcs_offset_r = -9;
#elif defined(CONFIG_MACH_T0_USA_TMO)
	t0_sound_pdata.dcs_offset_l = -11;
	t0_sound_pdata.dcs_offset_r = -9;
#elif defined(CONFIG_MACH_T0_USA_SPR)
	t0_sound_pdata.dcs_offset_l = -12;
	t0_sound_pdata.dcs_offset_r = -9;
#elif defined(CONFIG_MACH_T0_USA_USCC)
	t0_sound_pdata.dcs_offset_l = -11;
	t0_sound_pdata.dcs_offset_r = -8;
#endif

	t0_gpio_init();

	platform_add_devices(t0_sound_devices,
		ARRAY_SIZE(t0_sound_devices));

	pr_info("%s: set sound platform data for T0 device\n", __func__);
	if (exynos_sound_set_platform_data(&t0_sound_pdata))
		pr_err("%s: failed to register sound pdata\n", __func__);

	SET_PLATDATA_CODEC(NULL);
	i2c_register_board_info(I2C_NUM_CODEC, i2c_wm1811,
					ARRAY_SIZE(i2c_wm1811));

#if defined(CONFIG_FM_SI4705)
	i2c_register_board_info(19, i2c_devs19_emul,
				ARRAY_SIZE(i2c_devs19_emul));
#endif

}
