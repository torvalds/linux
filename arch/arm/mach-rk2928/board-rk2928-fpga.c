/* arch/arm/mach-rk2928/board-rk2928-fpga.c
 *
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
#include <linux/fb.h>
#include <linux/regulator/machine.h>
#include <linux/rfkill-rk.h>
#include <linux/sensor-dev.h>


#define RK2928_FB_MEM_SIZE 3*SZ_1M

#ifdef CONFIG_FB_ROCKCHIP

static int rk_fb_io_init(struct rk29_fb_setting_info *fb_setting)
{
	return 0;
}
static int rk_fb_io_disable(void)
{
	return 0;
}
static int rk_fb_io_enable(void)
{
	return 0;
}

#if defined(CONFIG_LCDC_RK2928)
struct rk29fb_info lcdc_screen_info = {
	.prop	   = PRMRY,		//primary display device
	.io_init   = rk_fb_io_init,
	.io_disable = rk_fb_io_disable,
	.io_enable = rk_fb_io_enable,
	.set_screen_info = set_lcd_info,
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
#ifdef CONFIG_FB_ROCKCHIP
	&device_fb,
#endif
};

static void __init rk2928_board_init(void)
{
        rk30_i2c_register_board_info();
        spi_register_board_info(board_spi_devices, ARRAY_SIZE(board_spi_devices));
        platform_add_devices(devices, ARRAY_SIZE(devices));
}

static void __init rk2928_reserve(void)
{
#ifdef CONFIG_FB_ROCKCHIP
	resource_fb[0].start = board_mem_reserve_add("fb0", RK2928_FB_MEM_SIZE);
	resource_fb[0].end = resource_fb[0].start + RK2928_FB_MEM_SIZE - 1;
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
        CLK(NULL, "pd_lcdc0", &xin24m),
        CLK(NULL, "hclk_lcdc0", &xin24m),
        CLK(NULL, "aclk_lcdc0", &xin24m),
        CLK(NULL, "dclk_lcdc0", &xin24m),
};

void __init rk30_clock_init(void)
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

MACHINE_START(RK2928, "RK2928board")
	.boot_params	= PLAT_PHYS_OFFSET + 0x800,
	.fixup		= rk2928_fixup,
	.reserve	= &rk2928_reserve,
	.map_io		= rk2928_map_io,
	.init_irq	= rk2928_init_irq,
	.timer		= &rk2928_timer,
	.init_machine	= rk2928_board_init,
MACHINE_END
