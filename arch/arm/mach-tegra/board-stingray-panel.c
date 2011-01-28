/*
 * arch/arm/mach-tegra/board-stingray-panel.c
 *
 * Copyright (C) 2010 Google, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/gpio.h>
#include <linux/i2c.h>
#include <linux/leds-auo-panel-backlight.h>
#include <linux/resource.h>
#include <linux/leds-lp8550.h>
#include <linux/platform_device.h>
#include <linux/earlysuspend.h>
#include <linux/delay.h>
#include <linux/keyreset.h>
#include <linux/input.h>
#include <asm/mach-types.h>
#include <mach/irqs.h>
#include <mach/iomap.h>
#include <mach/dc.h>
#include <mach/fb.h>
#include <mach/nvhost.h>
#include <linux/regulator/consumer.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/fixed.h>

#include "board.h"
#include "board-stingray.h"
#include "gpio-names.h"

#define STINGRAY_AUO_DISP_BL	TEGRA_GPIO_PD0
#define STINGRAY_LVDS_SHDN_B	TEGRA_GPIO_PB2
#define STINGRAY_HDMI_5V_EN	TEGRA_GPIO_PC4
#define STINGRAY_HDMI_HPD	TEGRA_GPIO_PN7

/* Display Controller */
static struct resource stingray_disp1_resources[] = {
	{
		.name	= "irq",
		.start	= INT_DISPLAY_GENERAL,
		.end	= INT_DISPLAY_GENERAL,
		.flags	= IORESOURCE_IRQ,
	},
	{
		.name	= "regs",
		.start	= TEGRA_DISPLAY_BASE,
		.end	= TEGRA_DISPLAY_BASE + TEGRA_DISPLAY_SIZE-1,
		.flags	= IORESOURCE_MEM,
	},
	{
		.name	= "fbmem",
		/* .start and .end to be filled in later */
		.flags	= IORESOURCE_MEM,
	},
};

static struct resource stingray_disp2_resources[] = {
	{
		.name	= "irq",
		.start	= INT_DISPLAY_B_GENERAL,
		.end	= INT_DISPLAY_B_GENERAL,
		.flags	= IORESOURCE_IRQ,
	},
	{
		.name	= "regs",
		.start	= TEGRA_DISPLAY2_BASE,
		.end	= TEGRA_DISPLAY2_BASE + TEGRA_DISPLAY2_SIZE-1,
		.flags	= IORESOURCE_MEM,
	},
	{
		.name	= "fbmem",
		/* .start and .end to be filled in later */
		.flags	= IORESOURCE_MEM,
	},
	{
		.name	= "hdmi_regs",
		.start	= TEGRA_HDMI_BASE,
		.end	= TEGRA_HDMI_BASE + TEGRA_HDMI_SIZE-1,
		.flags	= IORESOURCE_MEM,
	},
};

static struct tegra_dc_mode stingray_panel_modes_p0[] = {
	{
		.pclk = 62200000,
		.h_ref_to_sync = 11,
		.v_ref_to_sync = 1,
		.h_sync_width = 42,
		.v_sync_width = 6,
		.h_back_porch = 43,
		.v_back_porch = 5,
		.h_active = 1280,
		.v_active = 720,
		.h_front_porch = 43,
		.v_front_porch = 5,
	},
};

static struct tegra_dc_mode stingray_panel_modes[] = {
	{
		.pclk = 65000000,
		.h_ref_to_sync = 11,
		.v_ref_to_sync = 1,
		.h_sync_width = 26,
		.v_sync_width = 6,
		.h_back_porch = 12,
		.v_back_porch = 3,
		.h_active = 1280,
		.v_active = 800,
		.h_front_porch = 45,
		.v_front_porch = 3,
	},
};

static struct tegra_fb_data stingray_fb_data_p0 = {
	.win		= 0,
	.xres		= 1280,
	.yres		= 720,
	.bits_per_pixel	= -1,
	.flags		= TEGRA_FB_FLIP_ON_PROBE,
};

static struct tegra_fb_data stingray_fb_data = {
	.win		= 0,
	.xres		= 1280,
	.yres		= 800,
	.bits_per_pixel	= -1,
	.flags		= TEGRA_FB_FLIP_ON_PROBE,
};

#define LCD_MANFID_MAX_LEN 3
static char lcd_manfid[LCD_MANFID_MAX_LEN + 1];
int __init board_lcd_manfid_init(char *s)
{
	strncpy(lcd_manfid, s, LCD_MANFID_MAX_LEN);
	lcd_manfid[LCD_MANFID_MAX_LEN] = '\0';
	printk(KERN_INFO "lcd_manfid=%s\n", lcd_manfid);
	return 1;
}
__setup("lcd_manfid=", board_lcd_manfid_init);

/* Disgusting hack to deal with the fact that there are a set of pull down
 * resistors on the panel end of the i2c bus.  These cause a voltage
 * divider on the i2c bus which can cause these devices to fail to recognize
 * their addresses when power to the display is cut.  This creates a dependency
 * between these devices and the power to the panel.
 */
static struct regulator_consumer_supply stingray_panel_reg_consumer_supply[] = {
	{ .supply = "vdd_panel", .dev_name = NULL, },
	{ .supply = "vio", .dev_name = "0-0077" /* barometer */},
	{ .supply = "vio", .dev_name = "0-002c" /* lighting */},
	{ .supply = "vio", .dev_name = "0-005b" /* touch */},
	{ .supply = "vio", .dev_name = "0-004b" /* als */},
};

static struct regulator_init_data stingray_panel_reg_initdata = {
	.consumer_supplies = stingray_panel_reg_consumer_supply,
	.num_consumer_supplies = ARRAY_SIZE(stingray_panel_reg_consumer_supply),
	.constraints = {
		.valid_ops_mask = REGULATOR_CHANGE_STATUS,
	},
};

static struct fixed_voltage_config stingray_panel_reg_config = {
	.supply_name		= "stingray_panel_reg",
	.microvolts		= 5000000,
	.gpio			= STINGRAY_LVDS_SHDN_B,
	.startup_delay		= 200000,
	.enable_high		= 1,
	.enabled_at_boot	= 1,
	.init_data		= &stingray_panel_reg_initdata,
};

static struct platform_device stingray_panel_reg_device = {
	.name	= "reg-fixed-voltage",
	.id	= 0,
	.dev	= {
		.platform_data = &stingray_panel_reg_config,
	},
};

static struct regulator *stingray_panel_regulator = NULL;
static int stingray_panel_enable(void)
{
	if (IS_ERR_OR_NULL(stingray_panel_regulator)) {
		stingray_panel_regulator = regulator_get(NULL, "vdd_panel");
		if (IS_ERR_OR_NULL(stingray_panel_regulator)) {
			pr_err("%s: Could not get panel regulator\n", __func__);
			return PTR_ERR(stingray_panel_regulator);
		}
	}

	return regulator_enable(stingray_panel_regulator);
}

static int stingray_panel_disable(void)
{
	if (!IS_ERR_OR_NULL(stingray_panel_regulator))
		regulator_disable(stingray_panel_regulator);
	return 0;
}

static void stingray_panel_early_reg_disable(struct work_struct *work)
{
	stingray_panel_disable();
}

static DECLARE_DELAYED_WORK(stingray_panel_early_reg_disable_work,
	stingray_panel_early_reg_disable);

static void stingray_panel_early_reg_enable(struct work_struct *work)
{
	/*
	 * If the regulator was previously enabled, the work function to
	 * disable the work will be pending, cancel_delayed_work_sync
	 * will return true, and the regulator will not get enabled again.
	 */
	if (!cancel_delayed_work_sync(&stingray_panel_early_reg_disable_work))
		stingray_panel_enable();

	/*
	 * After the cancel_delay_work_sync, there is no outstanding work
	 * to disable the regulator, so queue the disable in 1 second.
	 * If no other driver calls regulator_enable on stingray_panel_regulator
	 * before 1 second has elapsed, the bridge chip will power down.
	 */
	queue_delayed_work(system_nrt_wq,
		&stingray_panel_early_reg_disable_work,
		msecs_to_jiffies(1000));
}

static DECLARE_WORK(stingray_panel_early_reg_enable_work,
	stingray_panel_early_reg_enable);
static bool stingray_panel_early_reg_in_resume;

static int stingray_panel_early_reg_resume_noirq(struct device *dev)
{
	stingray_panel_early_reg_in_resume = true;
	smp_wmb();
	return 0;
}

static void stingray_panel_early_reg_resume_complete(struct device *dev)
{
	stingray_panel_early_reg_in_resume = false;
	smp_wmb();
}

static int stingray_panel_early_reg_suspend(struct device *dev)
{
	cancel_work_sync(&stingray_panel_early_reg_enable_work);
	flush_delayed_work(&stingray_panel_early_reg_disable_work);

	return 0;
}

/*
 * If the power button key event is detected during the resume process,
 * the screen will get turned on later.  Immediately start the LVDS bridge chip
 * turning on to reduce time spent waiting for it later in resume.
 */
static int stingray_panel_early_reg_power(void)
{
	smp_rmb();
	if (stingray_panel_early_reg_in_resume)
		queue_work(system_nrt_wq, &stingray_panel_early_reg_enable_work);

	return 0;
}

static struct dev_pm_ops stingray_panel_early_reg_pm_ops = {
	.resume_noirq = stingray_panel_early_reg_resume_noirq,
	.complete = stingray_panel_early_reg_resume_complete,
	.suspend = stingray_panel_early_reg_suspend,
};

static struct keyreset_platform_data stingray_panel_early_reg_keyreset = {
	.reset_fn = stingray_panel_early_reg_power,
	.keys_down = {
		KEY_END,
		0
	},
};

struct platform_device stingray_panel_early_reg_keyreset_device = {
	.name	= KEYRESET_NAME,
	.id	= -1,
	.dev	= {
		.platform_data = &stingray_panel_early_reg_keyreset,
	},
};

static struct platform_driver stingray_panel_early_reg_driver = {
	.driver = {
		.name	= "stingray-panel-early-reg",
		.owner	= THIS_MODULE,
		.pm = &stingray_panel_early_reg_pm_ops,
	}
};

static struct platform_device stingray_panel_early_reg_device = {
	.name = "stingray-panel-early-reg",
};

static struct tegra_dc_out stingray_disp1_out = {
	.type = TEGRA_DC_OUT_RGB,

	.align = TEGRA_DC_ALIGN_MSB,
	.order = TEGRA_DC_ORDER_RED_BLUE,
	.depth = 24,

	.modes = stingray_panel_modes,
	.n_modes = ARRAY_SIZE(stingray_panel_modes),

	.enable = stingray_panel_enable,
	.disable = stingray_panel_disable,
};

static struct tegra_dc_platform_data stingray_disp1_pdata = {
	.flags		= TEGRA_DC_FLAG_ENABLED,
	.emc_clk_rate	= 400000000,
	.default_out	= &stingray_disp1_out,
	.fb		= &stingray_fb_data,
};

static struct nvhost_device stingray_disp1_device = {
	.name		= "tegradc",
	.id		= 0,
	.resource	= stingray_disp1_resources,
	.num_resources	= ARRAY_SIZE(stingray_disp1_resources),
	.dev = {
		.platform_data = &stingray_disp1_pdata,
	},
};

static int stingray_hdmi_init(void)
{
	tegra_gpio_enable(STINGRAY_HDMI_5V_EN);
	gpio_request(STINGRAY_HDMI_5V_EN, "hdmi_5v_en");
	gpio_direction_output(STINGRAY_HDMI_5V_EN, 1);

	tegra_gpio_enable(STINGRAY_HDMI_HPD);
	gpio_request(STINGRAY_HDMI_HPD, "hdmi_hpd");
	gpio_direction_input(STINGRAY_HDMI_HPD);


	return 0;
}

static struct tegra_dc_out stingray_disp2_out = {
	.type = TEGRA_DC_OUT_HDMI,
	.flags = TEGRA_DC_OUT_HOTPLUG_HIGH,

	.dcc_bus = 1,
	.hotplug_gpio = STINGRAY_HDMI_HPD,

	.align = TEGRA_DC_ALIGN_MSB,
	.order = TEGRA_DC_ORDER_RED_BLUE,
};

static struct tegra_fb_data stingray_disp2_fb_data = {
	.win		= 0,
	.xres		= 1280,
	.yres		= 720,
	.bits_per_pixel	= 32,
};

static struct tegra_dc_platform_data stingray_disp2_pdata = {
	.flags		= 0,
	.emc_clk_rate	= ULONG_MAX,
	.default_out	= &stingray_disp2_out,
	.fb		= &stingray_disp2_fb_data,
};

static struct nvhost_device stingray_disp2_device = {
	.name		= "tegradc",
	.id		= 1,
	.resource	= stingray_disp2_resources,
	.num_resources	= ARRAY_SIZE(stingray_disp2_resources),
	.dev = {
		.platform_data = &stingray_disp2_pdata,
	},
};


static void stingray_backlight_enable(void)
{
	gpio_set_value(STINGRAY_AUO_DISP_BL, 1);
}

static void stingray_backlight_disable(void)
{
	gpio_set_value(STINGRAY_AUO_DISP_BL, 0);
}

struct auo_panel_bl_platform_data stingray_auo_backlight_data = {
	.bl_enable = stingray_backlight_enable,
	.bl_disable = stingray_backlight_disable,
	.pwm_enable = NULL,
	.pwm_disable = NULL,
};

static struct platform_device stingray_panel_bl_driver = {
	.name = LD_AUO_PANEL_BL_NAME,
	.id = -1,
	.dev = {
		.platform_data = &stingray_auo_backlight_data,
		},
};

struct lp8550_eeprom_data stingray_lp8550_eeprom_data[] = {
	/* Set the backlight current to 19mA each step is .12mA */
	{0xa1},
	/* Boost freq 312khz, PWM controled w/constant current,
	thermal deration disabled, brightness slope 500mS */
	{0x67},
	/* Adaptive mode for light loads, advanced slope enabled, 50% mode selected,
	Adaptive mode enabled, Boost is enabled, Boost Imax is 2.5A */
	{0xbf},
	/* UVLO is disabled, phase shift PWM enabled, PWM Freq 19232 */
	{0x3f},
	/* LED current resistor disabled, LED Fault = 3.3V */
	{0x08},
	/* Vsync is enabled, Dither enabled, Boost voltage 20V */
	{0xea},
	/* PLL 13-bit counter */
	{0x64},
	/* 1-bit hysteresis w/10 bit resolution, PWM output freq is set with
	PWM_FREQ EEPROM bits */
	{0x2a},
};

struct lp8550_platform_data stingray_lp8550_backlight_data = {
	.power_up_brightness = 0x80,
	.dev_ctrl_config = 0x04,
	.brightness_control = 0x80,
	.dev_id = 0xfc,
	.direct_ctrl = 0x01,
	.eeprom_table = stingray_lp8550_eeprom_data,
	.eeprom_tbl_sz = ARRAY_SIZE(stingray_lp8550_eeprom_data),
};

static struct i2c_board_info __initdata stingray_i2c_bus1_led_info[] = {
	 {
		I2C_BOARD_INFO(LD_LP8550_NAME, 0x2c),
		.platform_data = &stingray_lp8550_backlight_data,
	 },
};

#ifdef CONFIG_HAS_EARLYSUSPEND
/* put early_suspend/late_resume handlers here for the display in order
 * to keep the code out of the display driver, keeping it closer to upstream
 */
struct early_suspend stingray_panel_early_suspender;

static void stingray_panel_early_suspend(struct early_suspend *h)
{
	if (num_registered_fb > 0)
		fb_blank(registered_fb[0], FB_BLANK_POWERDOWN);
}

static void stingray_panel_late_resume(struct early_suspend *h)
{
	if (num_registered_fb > 0)
		fb_blank(registered_fb[0], FB_BLANK_UNBLANK);
}
#endif

static struct regulator *stingray_csi_reg;

int __init stingray_panel_init(void)
{
	struct resource *res;

	if (stingray_revision() < STINGRAY_REVISION_P1) {
		tegra_gpio_enable(STINGRAY_AUO_DISP_BL);
		gpio_request(STINGRAY_AUO_DISP_BL, "auo_disp_bl");
		gpio_direction_output(STINGRAY_AUO_DISP_BL, 1);
		platform_device_register(&stingray_panel_bl_driver);
		stingray_disp1_pdata.fb = &stingray_fb_data_p0;
		stingray_disp1_out.modes = stingray_panel_modes_p0;
	} else {
		if (stingray_revision() >= STINGRAY_REVISION_P3)
			stingray_lp8550_backlight_data.dev_ctrl_config = 0x02;
		i2c_register_board_info(0, stingray_i2c_bus1_led_info,
			ARRAY_SIZE(stingray_i2c_bus1_led_info));
	}

	platform_device_register(&stingray_panel_reg_device);

	platform_driver_register(&stingray_panel_early_reg_driver);
	platform_device_register(&stingray_panel_early_reg_device);
	platform_device_register(&stingray_panel_early_reg_keyreset_device);

	stingray_hdmi_init();

	stingray_csi_reg = regulator_get(NULL, "vcsi");
	if (IS_ERR(stingray_csi_reg)) {
		pr_err("hdmi: couldn't get regulator vcsi");
	} else {
		regulator_enable(stingray_csi_reg);
	}

#ifdef CONFIG_HAS_EARLYSUSPEND
	stingray_panel_early_suspender.suspend = stingray_panel_early_suspend;
	stingray_panel_early_suspender.resume = stingray_panel_late_resume;
	stingray_panel_early_suspender.level = EARLY_SUSPEND_LEVEL_DISABLE_FB;
	register_early_suspend(&stingray_panel_early_suspender);
#endif


	res = nvhost_get_resource_byname(&stingray_disp1_device,
		IORESOURCE_MEM, "fbmem");
	res->start = tegra_fb_start;
	res->end = tegra_fb_start + tegra_fb_size - 1;

	res = nvhost_get_resource_byname(&stingray_disp2_device,
		IORESOURCE_MEM, "fbmem");
	res->start = tegra_fb2_start;
	res->end = tegra_fb2_start + tegra_fb2_size - 1;

	tegra_move_framebuffer(tegra_fb_start, tegra_bootloader_fb_start,
		min(tegra_fb_size, tegra_bootloader_fb_size));

	nvhost_device_register(&stingray_disp1_device);
	return  nvhost_device_register(&stingray_disp2_device);
}

