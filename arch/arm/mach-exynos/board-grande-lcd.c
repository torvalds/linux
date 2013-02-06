/*
 * midas-lcd.c - lcd driver of MIDAS Project
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

#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/regulator/consumer.h>
#include <linux/lcd.h>

#include <plat/devs.h>
#include <plat/fb-s5p.h>
#include <plat/gpio-cfg.h>
#include <plat/pd.h>
#include <plat/map-base.h>
#include <plat/map-s5p.h>

#ifdef CONFIG_FB_S5P_LD9040
#include <linux/ld9040.h>
#endif

#ifdef CONFIG_FB_S5P_MIPI_DSIM
#include <mach/mipi_ddi.h>
#include <mach/dsim.h>
#endif
#if defined(CONFIG_S5P_DSIM_SWITCHABLE_DUAL_LCD)
#include <../../../drivers/video/samsung_duallcd/s3cfb.h>
#else
#include <../../../drivers/video/samsung/s3cfb.h>
#endif

#ifdef CONFIG_FB_S5P_MDNIE
#include <linux/mdnie.h>
#endif

struct s3c_platform_fb fb_platform_data;
unsigned int lcdtype;
static int __init lcdtype_setup(char *str)
{
	get_option(&str, &lcdtype);
	return 1;
}
__setup("lcdtype=", lcdtype_setup);


#ifdef CONFIG_FB_S5P
#ifdef CONFIG_FB_S5P_LD9040
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

	reset_gpio = EXYNOS4_GPY4(5);

	err = gpio_request(reset_gpio, "MLCD_RST");
	if (err) {
		printk(KERN_ERR "failed to request MLCD_RST for "
			"lcd reset control\n");
		return err;
	}

	gpio_request(reset_gpio, "MLCD_RST");

	mdelay(10);
	gpio_direction_output(reset_gpio, 0);
	mdelay(10);
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

	mdelay(10);
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

struct ld9040_panel_data s2plus_panel_data;
static struct lcd_platform_data ld9040_platform_data = {
	.reset = reset_lcd,
	.power_on = lcd_power_on,
	.gpio_cfg_earlysuspend = lcd_gpio_cfg_earlysuspend,
	.gpio_cfg_lateresume = lcd_gpio_cfg_lateresume,
		 /* it indicates whether lcd panel is enabled from u-boot. */
	.lcd_enabled = 0,
	.reset_delay = 20,  /* 20ms */
	.power_on_delay = 20,	 /* 20ms */
	.power_off_delay = 200, /* 200ms */
	.sleep_in_delay = 160,
	.pdata = &s2plus_panel_data,
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

static struct platform_device ld9040_spi_gpio = {
	.name = "spi_gpio",
	.id = LCD_BUS_NUM,
	.dev = {
		.parent = &s3c_device_fb.dev,
		.platform_data = &lcd_spi_gpio_data,
	},
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

void __init ld9040_fb_init(void)
{
	struct ld9040_panel_data *pdata;

	strcpy(spi_board_info[0].modalias, "ld9040");
	spi_board_info[0].platform_data = (void *)&ld9040_platform_data;

	pdata = ld9040_platform_data.pdata;
	pdata->ops = &ops;

	spi_register_board_info(spi_board_info, ARRAY_SIZE(spi_board_info));

	if (!ld9040_platform_data.lcd_enabled)
		lcd_cfg_gpio();
	/*s3cfb_set_platdata(&fb_platform_data);*/
}
#endif

#if defined(CONFIG_FB_S5P_S6C1372)
int s6c1372_panel_gpio_init(void)
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

	return 0;
}

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
		/* LVDS_N_SHDN to high*/
		mdelay(1);
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
}

	return 0;
}

static struct lcd_platform_data s6c1372_platform_data = {
	.power_on	= lcd_power_on,
};

struct platform_device lcd_s6c1372 = {
	.name   = "s6c1372",
	.id	= -1,
	.dev.platform_data = &s6c1372_platform_data,
};

#endif

#ifdef CONFIG_FB_S5P_LMS501KF03
static struct s3c_platform_fb lms501kf03_data __initdata = {
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
		.modalias	 = "lms501kf03",
		.platform_data  = NULL,
		.max_speed_hz	 = 1200000,
		.bus_num	 = LCD_BUS_NUM,
		.chip_select	 = 0,
		.mode		 = SPI_MODE_3,
		.controller_data = (void *)DISPLAY_CS,
	}
};

static struct spi_gpio_platform_data lms501kf03_spi_gpio_data = {
	.sck	 = DISPLAY_CLK,
	.mosi	 = DISPLAY_SI,
	.miso	 = -1,
	.num_chipselect = 1,
};

static struct platform_device s3c_device_spi_gpio = {
	.name	 = "spi_gpio",
	.id = LCD_BUS_NUM,
	.dev	 = {
		.parent	 = &s3c_device_fb.dev,
		.platform_data  = &lms501kf03_spi_gpio_data,
	},
};
#endif

#ifdef CONFIG_FB_S5P_MIPI_DSIM
#ifdef CONFIG_FB_S5P_S6E8AA0
/* for Geminus based on MIPI-DSI interface */
static struct s3cfb_lcd s6e8aa0 = {
	.name = "s6e8aa0",
	.width = 720,
	.height = 1280,
	.p_width = 60,		/* 59.76 mm */
	.p_height = 106,	 /* 106.24 mm */
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
		.h_fp = 5,
		.h_bp = 5,
		.h_sw = 5,
		.v_fp = 13,
		.v_fpe = 1,
		.v_bp = 1,
		.v_bpe = 1,
		.v_sw = 2,
		.cmd_allow_len = 11,	 /* v_fp=stable_vfp + cmd_allow_len */
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

#ifdef CONFIG_FB_S5P_S6E63M0
/* for Geminus based on MIPI-DSI interface */
static struct s3cfb_lcd s6e63m0 = {
	.name = "s6e63m0",
	.width = 480,
#if 1 /* Only for S6E63M0X03 DDI */
	.height = 802,		/* Originally 800 (due to 2 Line in LCD below issue) */
#else
	.height = 800,
#endif
	.p_width = 60,		/* 59.76 mm */
	.p_height = 106,	 /* 106.24 mm */
	.bpp = 24,

	.freq = 56,

	/* minumun value is 0 except for wr_act time. */
	.cpu_timing = {
		.cs_setup = 0,
		.wr_setup = 0,
		.wr_act = 1,
		.wr_hold = 0,
	},

	.timing = {
		.h_fp = 16,
		.h_bp = 14,
		.h_sw = 2,
		.v_fp = 28,
		.v_fpe = 1,
		.v_bp = 1,
		.v_bpe = 1,
		.v_sw = 2,
		.cmd_allow_len = 11,	 /* v_fp=stable_vfp + cmd_allow_len */
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

#ifdef CONFIG_FB_S5P_S6E39A0
static struct s3cfb_lcd s6e39a0 = {
	.name = "s6e8aa0",
	.width = 540,
	.height = 960,
	.p_width = 58,
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
		.h_fp = 0x48,
		.h_bp = 12,
		.h_sw = 4,
		.v_fp = 13,
		.v_fpe = 1,
		.v_bp = 1,
		.v_bpe = 1,
		.v_sw = 2,
		.cmd_allow_len = 0x4,
	},

	.polarity = {
		.rise_vclk = 1,
		.inv_hsync = 0,
		.inv_vsync = 0,
		.inv_vden = 0,
	},
};
#endif

#ifdef CONFIG_FB_S5P_S6D6AA1
/* for Geminus based on MIPI-DSI interface */
static struct s3cfb_lcd s6d6aa1 = {
	.name = "s6d6aa1",
	.width = 720,
	.height = 1280,
	.p_width = 63,		/* 63.2 mm */
	.p_height = 114,	/* 114.19 mm */
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
		.h_fp = 50,
		.h_bp = 15,
		.h_sw = 3,
		.v_fp = 3,
		.v_fpe = 1,
		.v_bp = 2,
		.v_bpe = 1,
		.v_sw = 2,
		.cmd_allow_len = 11,	 /* v_fp=stable_vfp + cmd_allow_len */
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

static int reset_lcd(void)
{
	int err;

	err = gpio_request(GPIO_MLCD_RST, "MLCD_RST");
	if (err) {
		printk(KERN_ERR "failed to request GPY4(5) for "
			"lcd reset control\n");
		return -EINVAL;
	}

	gpio_direction_output(GPIO_MLCD_RST, 1);
	usleep_range(5000, 5000);
	gpio_set_value(GPIO_MLCD_RST, 0);
	usleep_range(5000, 5000);
	gpio_set_value(GPIO_MLCD_RST, 1);
	usleep_range(5000, 5000);
	gpio_free(GPIO_MLCD_RST);
	return 0;
}

static void lcd_cfg_gpio(void)
{
	/* MLCD_RST */
	s3c_gpio_cfgpin(GPIO_MLCD_RST, S3C_GPIO_OUTPUT);
	s3c_gpio_setpull(GPIO_MLCD_RST, S3C_GPIO_PULL_NONE);

	/* LCD_EN */
	s3c_gpio_cfgpin(GPIO_LCD_22V_EN_00, S3C_GPIO_OUTPUT);
	s3c_gpio_setpull(GPIO_LCD_22V_EN_00, S3C_GPIO_PULL_NONE);

	return;
}

static int lcd_power_on(void *ld, int enable)
{
	struct regulator *regulator;
	int err;

	printk(KERN_INFO "%s : enable=%d\n", __func__, enable);

	err = gpio_request(GPIO_MLCD_RST, "MLCD_RST");
	if (err) {
		printk(KERN_ERR "failed to request GPY4[5] for "
			"MLCD_RST control\n");
		return -EPERM;
	}

	err = gpio_request(GPIO_LCD_22V_EN_00, "LCD_EN");
	if (err) {
		printk(KERN_ERR "failed to request GPM4[4] for "
			"LCD_2.2V_EN control\n");
		return -EPERM;
	}

	if (enable) {
		gpio_set_value(GPIO_LCD_22V_EN_00, GPIO_LEVEL_HIGH);

		regulator = regulator_get(NULL, "vlcd_2.8v");
		if (IS_ERR(regulator))
			goto out;
		regulator_enable(regulator);
		regulator_put(regulator);
	} else {
		regulator = regulator_get(NULL, "vlcd_2.8v");
		if (IS_ERR(regulator))
			goto out;
		if (regulator_is_enabled(regulator))
			regulator_force_disable(regulator);
		regulator_put(regulator);

		gpio_set_value(GPIO_LCD_22V_EN_00, GPIO_LEVEL_LOW);
		gpio_set_value(GPIO_MLCD_RST, 0);
	}

out:
/* Release GPIO */
	gpio_free(GPIO_MLCD_RST);
	gpio_free(GPIO_LCD_22V_EN_00);
return 0;
}

static void s5p_dsim_mipi_power_control(int enable)
{
	struct regulator *regulator;
	int power_en = 0;

	if (power_en == 1) {
		printk(KERN_INFO "%s : enable=%d\n", __func__, enable);

		if (enable) {
				regulator = regulator_get(NULL, "vmipi_1.0v");
			if (IS_ERR(regulator))
				goto out;
			regulator_enable(regulator);
			regulator_put(regulator);

			regulator = regulator_get(NULL, "vmipi_1.8v");
			if (IS_ERR(regulator))
				goto out;
			regulator_enable(regulator);
			regulator_put(regulator);
		} else {
			regulator = regulator_get(NULL, "vmipi_1.8v");
			if (IS_ERR(regulator))
				goto out;
			if (regulator_is_enabled(regulator))
				regulator_disable(regulator);
			regulator_put(regulator);

			regulator = regulator_get(NULL, "vmipi_1.0v");
			if (IS_ERR(regulator))
				goto out;
			if (regulator_is_enabled(regulator))
				regulator_disable(regulator);
			regulator_put(regulator);
		}
out:
	return ;
	} else {
		return ;
	}
}

void __init mipi_fb_init(void)
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
	dsim_pd->mipi_power = s5p_dsim_mipi_power_control;

	dsim_lcd_info = dsim_pd->dsim_lcd_info;

#if defined(CONFIG_FB_S5P_S6E8AA0)
	dsim_lcd_info->lcd_panel_info = (void *)&s6e8aa0;
#endif
#if defined(CONFIG_FB_S5P_S6D6AA1)
	dsim_lcd_info->lcd_panel_info = (void *)&s6d6aa1;
#endif

#ifdef CONFIG_FB_S5P_S6E63M0
	dsim_lcd_info->lcd_panel_info = (void *)&s6e63m0;
	dsim_pd->dsim_info->e_no_data_lane = DSIM_DATA_LANE_2;
	/* 320Mbps */
	dsim_pd->dsim_info->p = 3;
	dsim_pd->dsim_info->m = 80;
	dsim_pd->dsim_info->s = 1;
#else
	/* 500Mbps */
	dsim_pd->dsim_info->p = 3;
	dsim_pd->dsim_info->m = 125;
	dsim_pd->dsim_info->s = 1;
#endif

	mipi_ddi_pd = (struct mipi_ddi_platform_data *)
	dsim_lcd_info->mipi_ddi_pd;
	mipi_ddi_pd->lcd_reset = reset_lcd;
	mipi_ddi_pd->lcd_power_on = lcd_power_on;
#if defined(CONFIG_S5P_DSIM_SWITCHABLE_DUAL_LCD)
	mipi_ddi_pd->lcd_sel_pin = GPIO_LCD_SEL;
#endif	/* CONFIG_S5P_DSIM_SWITCHABLE_DUAL_LCD */
	platform_device_register(&s5p_device_dsim);

	/*s3cfb_set_platdata(&fb_platform_data);*/
}
#endif
#endif

struct s3c_platform_fb fb_platform_data __initdata = {
	.hw_ver		= 0x70,
	.clk_name	= "fimd",
	.nr_wins	= 5,
#ifdef CONFIG_FB_S5P_DEFAULT_WINDOW
	.default_win	= CONFIG_FB_S5P_DEFAULT_WINDOW,
#else
	.default_win	= 0,
#endif
	.swap		= FB_SWAP_HWORD | FB_SWAP_WORD,
#if defined(CONFIG_FB_S5P_S6E8AA0)
	.lcd		= &s6e8aa0
#endif
#if defined(CONFIG_FB_S5P_S6E63M0)
	.lcd		= &s6e63m0
#endif
#if defined(CONFIG_FB_S5P_S6E39A0)
	.lcd		= &s6e39a0
#endif
#if defined(CONFIG_FB_S5P_LD9040)
	.lcd		= &ld9040_info
#endif
#if defined(CONFIG_FB_S5P_S6C1372)
	.lcd		= &s6c1372
#endif
#if defined(CONFIG_FB_S5P_S6D6AA1)
	.lcd		= &s6d6aa1
#endif
};

#ifdef CONFIG_FB_S5P_MDNIE
static struct platform_mdnie_data mdnie_data = {
	.display_type	= -1,
#if defined(CONFIG_FB_S5P_S6C1372)
	.lcd_pd		= &s6c1372_platform_data,
#endif
};
#endif

struct platform_device mdnie_device = {
		.name		 = "mdnie",
		.id	 = -1,
		.dev		 = {
			.parent = &exynos4_device_pd[PD_LCD0].dev,
#ifdef CONFIG_FB_S5P_MDNIE
			.platform_data = &mdnie_data,
#endif
	},
};
