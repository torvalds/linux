/*
 * Copyright (C) 2012 ROCKCHIP, Inc.
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

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/input.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/skbuff.h>
#include <linux/spi/spi.h>
#include <linux/mmc/host.h>
#include <linux/ion.h>
#include <linux/cpufreq.h>
#include <linux/clk.h>

#include <asm/setup.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/flash.h>
#include <asm/hardware/gic.h>

#include <mach/board.h>
#include <mach/hardware.h>
#include <mach/io.h>
#include <mach/gpio.h>
#include <mach/iomux.h>
#include <linux/fb.h>
#include <linux/regulator/machine.h>
#include <linux/rfkill-rk.h>
#include <linux/sensor-dev.h>


#define RK_FB_MEM_SIZE 3*SZ_1M

#if defined(CONFIG_FB_ROCKCHIP)
#define LCD_CS_MUX_NAME    GPIO4C7_SMCDATA7_TRACEDATA7_NAME
#define LCD_CS_PIN         RK30_PIN4_PC7
#define LCD_CS_VALUE       GPIO_HIGH

#define LCD_EN_MUX_NAME    GPIO4C7_SMCDATA7_TRACEDATA7_NAME
#define LCD_EN_PIN         RK30_PIN6_PB4
#define LCD_EN_VALUE       GPIO_LOW

static int rk_fb_io_init(struct rk29_fb_setting_info *fb_setting)
{
	int ret = 0;
	rk30_mux_api_set(LCD_CS_MUX_NAME, GPIO4C_GPIO4C7);
	ret = gpio_request(LCD_CS_PIN, NULL);
	if (ret != 0)
	{
		gpio_free(LCD_CS_PIN);
		printk(KERN_ERR "request lcd cs pin fail!\n");
		return -1;
	}
	else
	{
		gpio_direction_output(LCD_CS_PIN, LCD_CS_VALUE);
	}
	ret = gpio_request(LCD_EN_PIN, NULL);
	if (ret != 0)
	{
		gpio_free(LCD_EN_PIN);
		printk(KERN_ERR "request lcd en pin fail!\n");
		return -1;
	}
	else
	{
		gpio_direction_output(LCD_EN_PIN, LCD_EN_VALUE);
	}
	return 0;
}
static int rk_fb_io_disable(void)
{
	gpio_set_value(LCD_CS_PIN, LCD_CS_VALUE? 0:1);
	gpio_set_value(LCD_EN_PIN, LCD_EN_VALUE? 0:1);
	return 0;
}
static int rk_fb_io_enable(void)
{
	gpio_set_value(LCD_CS_PIN, LCD_CS_VALUE);
	gpio_set_value(LCD_EN_PIN, LCD_EN_VALUE);
	return 0;
}

#if defined(CONFIG_LCDC0_RK31)
struct rk29fb_info lcdc0_screen_info = {
	.prop	   = PRMRY,		//primary display device
	.io_init   = rk_fb_io_init,
	.io_disable = rk_fb_io_disable,
	.io_enable = rk_fb_io_enable,
	.set_screen_info = set_lcd_info,
};
#endif

#if defined(CONFIG_LCDC1_RK31)
struct rk29fb_info lcdc1_screen_info = {
	#if defined(CONFIG_HDMI_RK30)
	.prop		= EXTEND,	//extend display device
	.lcd_info  = NULL,
	.set_screen_info = hdmi_init_lcdc,
	#endif
};
#endif

static struct resource resource_fb[] = {
	[0] = {
		.name  = "fb0 buf",
		.start = 0,
		.end   = 0,//RK30_FB0_MEM_SIZE - 1,
		.flags = IORESOURCE_MEM,
	},
	[1] = {
		.name  = "ipp buf",  //for rotate
		.start = 0,
		.end   = 0,//RK30_FB0_MEM_SIZE - 1,
		.flags = IORESOURCE_MEM,
	},
	[2] = {
		.name  = "fb2 buf",
		.start = 0,
		.end   = 0,//RK30_FB0_MEM_SIZE - 1,
		.flags = IORESOURCE_MEM,
	},
};

static struct platform_device device_fb = {
	.name		= "rk-fb",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(resource_fb),
	.resource	= resource_fb,
};
#endif

//i2c
#ifdef CONFIG_I2C0_RK30
static struct i2c_board_info __initdata i2c0_info[] = {
};
#endif
#ifdef CONFIG_I2C1_RK30
static struct i2c_board_info __initdata i2c1_info[] = {
};
#endif
#ifdef CONFIG_I2C2_RK30
static struct i2c_board_info __initdata i2c2_info[] = {
};
#endif
#ifdef CONFIG_I2C3_RK30
static struct i2c_board_info __initdata i2c3_info[] = {
};
#endif

#ifdef CONFIG_I2C_GPIO_RK30
static struct i2c_board_info __initdata i2c_gpio_info[] = {
};
#endif

static void __init rk30_i2c_register_board_info(void)
{
#ifdef CONFIG_I2C0_RK30
	i2c_register_board_info(0, i2c0_info, ARRAY_SIZE(i2c0_info));
#endif
#ifdef CONFIG_I2C1_RK30
	i2c_register_board_info(1, i2c1_info, ARRAY_SIZE(i2c1_info));
#endif
#ifdef CONFIG_I2C2_RK30
	i2c_register_board_info(2, i2c2_info, ARRAY_SIZE(i2c2_info));
#endif
#ifdef CONFIG_I2C3_RK30
	i2c_register_board_info(3, i2c3_info, ARRAY_SIZE(i2c3_info));
#endif
#ifdef CONFIG_I2C_GPIO_RK30
	i2c_register_board_info(4, i2c_gpio_info, ARRAY_SIZE(i2c_gpio_info));
#endif
}
//end of i2c

static struct spi_board_info board_spi_devices[] = {
};

static struct platform_device *devices[] __initdata = {
#if defined(CONFIG_FB_ROCKCHIP)
	&device_fb,
#endif
};

static void __init rk31_board_init(void)
{
        rk30_i2c_register_board_info();
        spi_register_board_info(board_spi_devices, ARRAY_SIZE(board_spi_devices));
        platform_add_devices(devices, ARRAY_SIZE(devices));
}

static void __init rk31_reserve(void)
{
#if defined(CONFIG_FB_ROCKCHIP)
	resource_fb[0].start = board_mem_reserve_add("fb0", RK_FB_MEM_SIZE);
	resource_fb[0].end = resource_fb[0].start + RK_FB_MEM_SIZE - 1;
#endif
	board_mem_reserved();
}

#include <linux/clkdev.h>

struct clk {
	const char		*name;
	unsigned long		rate;
};

static struct clk xin24m = {
	.name		= "xin24m",
	.rate		= 24000000,
};

#define CLK(dev, con, ck) \
	{ \
		.dev_id = dev, \
		.con_id = con, \
		.clk = ck, \
	}

static struct clk_lookup clks[] = {
	CLK("rk30_i2c.0", "i2c", &xin24m),
	CLK("rk30_i2c.1", "i2c", &xin24m),
	CLK("rk30_i2c.2", "i2c", &xin24m),
	CLK("rk30_i2c.3", "i2c", &xin24m),
	CLK("rk30_i2c.4", "i2c", &xin24m),
	CLK("rk29xx_spim.0", "spi", &xin24m),
	CLK("rk29xx_spim.1", "spi", &xin24m),

        CLK("rk_serial.0", "uart_div", &xin24m),
	CLK("rk_serial.0", "uart_frac_div", &xin24m),
	CLK("rk_serial.0", "uart", &xin24m),
	CLK("rk_serial.0", "pclk_uart", &xin24m),
	CLK("rk_serial.1", "uart_div", &xin24m),
	CLK("rk_serial.1", "uart_frac_div", &xin24m),
	CLK("rk_serial.1", "uart", &xin24m),
	CLK("rk_serial.1", "pclk_uart", &xin24m),
	CLK("rk_serial.2", "uart_div", &xin24m),
	CLK("rk_serial.2", "uart_frac_div", &xin24m),
	CLK("rk_serial.2", "uart", &xin24m),
	CLK("rk_serial.2", "pclk_uart", &xin24m),

        CLK("rk29_i2s.0", "i2s_div", &xin24m),
	CLK("rk29_i2s.0", "i2s_frac_div", &xin24m),
	CLK("rk29_i2s.0", "i2s", &xin24m),
	CLK("rk29_i2s.0", "hclk_i2s", &xin24m),
};

static void __init rk30_clock_init(void)
{
	struct clk_lookup *lk;

	for (lk = clks; lk < clks + ARRAY_SIZE(clks); lk++) {
		clkdev_add(lk);
	}
}

void __init board_clock_init(void)
{
 	rk30_clock_init();   
}

int __init clk_disable_unused(void)
{
	return 0;
}

int clk_enable(struct clk *clk)
{
	return 0;
}
EXPORT_SYMBOL(clk_enable);

void clk_disable(struct clk *clk)
{
}
EXPORT_SYMBOL(clk_disable);

unsigned long clk_get_rate(struct clk *clk)
{
	return 24000000;
}
EXPORT_SYMBOL(clk_get_rate);

int clk_set_rate(struct clk *clk, unsigned long rate)
{
	return 0;
}
EXPORT_SYMBOL(clk_set_rate);

int clk_set_parent(struct clk *clk, struct clk *parent)
{
	return 0;
}
EXPORT_SYMBOL(clk_set_parent);

#include <mach/gpio.h>
#include <plat/key.h>

#define EV_ENCALL				KEY_F4
#define EV_MENU					KEY_F1

#define PRESS_LEV_LOW			1
#define PRESS_LEV_HIGH			0

static struct rk29_keys_button key_button[] = {
	{
		.desc	= "menu",
		.code	= EV_MENU,
		.gpio	= RK30_PIN3_PB2,
		.active_low = PRESS_LEV_LOW,
	},
	{
		.desc	= "vol+",
		.code	= KEY_VOLUMEUP,
		.gpio	= RK30_PIN3_PB1,
		.active_low = PRESS_LEV_LOW,
	},
	{
		.desc	= "vol-",
		.code	= KEY_VOLUMEDOWN,
		.gpio	= RK30_PIN3_PB0,
		.active_low = PRESS_LEV_LOW,
	},
	{
		.desc	= "home",
		.code	= KEY_HOME,
		.gpio	= RK30_PIN3_PB3,
		.active_low = PRESS_LEV_LOW,
	},
	{
		.desc	= "esc",
		.code	= KEY_BACK,
		.gpio	= RK30_PIN3_PB4,
		.active_low = PRESS_LEV_LOW,
	},
	{
		.desc	= "key6",
		.code	= KEY_CAMERA,
		.gpio	= RK30_PIN3_PB5,
		.active_low = PRESS_LEV_LOW,
	},
};

struct rk29_keys_platform_data rk29_keys_pdata = {
	.buttons	= key_button,
	.nbuttons	= ARRAY_SIZE(key_button),
	.chn	= -1,  //chn: 0-7, if do not use ADC,set 'chn' -1
};

static void __init fpga_fixup(struct machine_desc *desc, struct tag *tags,
			char **cmdline, struct meminfo *mi)
{
	mi->nr_banks = 1;
	mi->bank[0].start = PLAT_PHYS_OFFSET;
	mi->bank[0].size = SZ_128M;
}

#include <mach/system.h>
static void fpga_reset(char mode, const char *cmd)
{
	while (1);
}

static void __init fpga_map_io(void)
{
	arch_reset = fpga_reset;
	rk30_map_common_io();
	rk29_setup_early_printk();
	rk29_sram_init();
	board_clock_init();
	rk30_iomux_init();
}

MACHINE_START(RK31, "RK31board")
	.boot_params	= PLAT_PHYS_OFFSET + 0x800,
	.fixup		= fpga_fixup,
	.reserve	= &rk31_reserve,
	.map_io		= fpga_map_io,
	.init_irq	= rk30_init_irq,
	.timer		= &rk30_timer,
	.init_machine	= rk31_board_init,
MACHINE_END
