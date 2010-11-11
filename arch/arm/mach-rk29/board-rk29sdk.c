/* arch/arm/mach-rk29/board-rk29.c
 *
 * Copyright (C) 2010 ROCKCHIP, Inc.
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
#include <linux/spi/spi.h>
#include <linux/mmc/host.h>

#include <mach/hardware.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/flash.h>
#include <asm/hardware/gic.h>

#include <mach/iomux.h>
#include <mach/gpio.h>
#include <mach/irqs.h>
#include <mach/rk29_iomap.h>
#include <mach/board.h>


#include <linux/mtd/nand.h>
#include <linux/mtd/partitions.h>

#include "devices.h"
#include "../../../drivers/input/touchscreen/xpt2046_cbn_ts.h"


extern struct sys_timer rk29_timer;


static struct rk29_gpio_bank rk29_gpiobankinit[] = {
	{
		.id		= RK29_ID_GPIO0,
		.offset	= RK29_GPIO0_BASE,
	},
	{
		.id		= RK29_ID_GPIO1,
		.offset	= RK29_GPIO1_BASE,
	}, 
	{
		.id		= RK29_ID_GPIO2,
		.offset	= RK29_GPIO2_BASE,
	}, 
	{
		.id		= RK29_ID_GPIO3,
		.offset	= RK29_GPIO3_BASE,
	}, 
	{
		.id		= RK29_ID_GPIO4,
		.offset	= RK29_GPIO4_BASE,
	}, 
	{
		.id		= RK29_ID_GPIO5,
		.offset	= RK29_GPIO5_BASE,
	}, 
	{
		.id		= RK29_ID_GPIO6,
		.offset	= RK29_GPIO6_BASE,
	},  	
};

static struct platform_device *devices[] __initdata = {
#ifdef CONFIG_UART1_RK29	
	&rk29_device_uart1,
#endif	
#ifdef CONFIG_SPIM_RK29XX
    &rk29xx_device_spi0m,
    &rk29xx_device_spi1m,
#endif
};

/*****************************************************************************************
 * spi devices
 * author: cmc@rock-chips.com
 *****************************************************************************************/
#define SPI_CHIPSELECT_NUM 2
struct spi_cs_gpio rk29xx_spi0_cs_gpios[SPI_CHIPSELECT_NUM] = {
    {
		.name = "spi0 cs0",
		.cs_gpio = RK29_PIN2_PC1,
		.cs_iomux_name = NULL,
	},
	{
		.name = "spi0 cs1",
		.cs_gpio = RK29_PIN1_PA4,
		.cs_iomux_name = GPIO1A4_EMMCWRITEPRT_SPI0CS1_NAME,//if no iomux,set it NULL
		.cs_iomux_mode = GPIO1L_SPI0_CSN1,
	}
};

struct spi_cs_gpio rk29xx_spi1_cs_gpios[SPI_CHIPSELECT_NUM] = {
    {
		.name = "spi1 cs0",
		.cs_gpio = RK29_PIN2_PC5,
		.cs_iomux_name = NULL,
	},
	{
		.name = "spi1 cs1",
		.cs_gpio = RK29_PIN1_PA3,
		.cs_iomux_name = GPIO1A3_EMMCDETECTN_SPI1CS1_NAME,//if no iomux,set it NULL
		.cs_iomux_mode = GPIO1L_SPI0_CSN1,
	}
};

static int spi_io_init(struct spi_cs_gpio *cs_gpios, int cs_num)
{	
	int i,j,ret;
	
	//cs
	if (cs_gpios) {
		for (i=0; i<cs_num; i++) {
			rk29_mux_api_set(cs_gpios[i].cs_iomux_name, cs_gpios[i].cs_iomux_mode);
			ret = gpio_request(cs_gpios[i].cs_gpio, cs_gpios[i].name);
			if (ret) {
				for (j=0;j<i;j++) {
					gpio_free(cs_gpios[j].cs_gpio);
					//rk29_mux_api_mode_resume(cs_gpios[j].cs_iomux_name);
				}
				printk("[fun:%s, line:%d], gpio request err\n", __func__, __LINE__);
				return -1;
			}			
			gpio_direction_output(cs_gpios[i].cs_gpio, GPIO_HIGH);
		}
	}
	return 0;
}

static int spi_io_deinit(struct spi_cs_gpio *cs_gpios, int cs_num)
{
	int i;
	
	if (cs_gpios) {
		for (i=0; i<cs_num; i++) {
			gpio_free(cs_gpios[i].cs_gpio);
			//rk29_mux_api_mode_resume(cs_gpios[i].cs_iomux_name);
		}
	}
	
	return 0;
}

static int spi_io_fix_leakage_bug(void)
{
	gpio_direction_output(RK29_PIN2_PC1, GPIO_LOW); 
	return 0;
}

static int spi_io_resume_leakage_bug(void)
{
	gpio_direction_output(RK29_PIN2_PC1, GPIO_HIGH);
	return 0;
}

struct rk29xx_spi_platform_data rk29xx_spi0_platdata = {
	.num_chipselect = SPI_CHIPSELECT_NUM,
	.chipselect_gpios = rk29xx_spi0_cs_gpios,
	.io_init = spi_io_init,
	.io_deinit = spi_io_deinit,
	.io_fix_leakage_bug = spi_io_fix_leakage_bug,
	.io_resume_leakage_bug = spi_io_resume_leakage_bug,
};

struct rk29xx_spi_platform_data rk29xx_spi1_platdata = {
	.num_chipselect = SPI_CHIPSELECT_NUM,
	.chipselect_gpios = rk29xx_spi1_cs_gpios,
	.io_init = spi_io_init,
	.io_deinit = spi_io_deinit,
	.io_fix_leakage_bug = spi_io_fix_leakage_bug,
	.io_resume_leakage_bug = spi_io_resume_leakage_bug,
};

/*****************************************************************************************
 * xpt2046 touch panel
 * author: cmc@rock-chips.com
 *****************************************************************************************/
#define XPT2046_GPIO_INT           RK29_PIN0_PA3
#define DEBOUNCE_REPTIME  3

#if defined(CONFIG_TOUCHSCREEN_XPT2046_320X480_SPI) 
static struct xpt2046_platform_data xpt2046_info = {
	.model			= 2046,
	.keep_vref_on 	= 1,
	.swap_xy		= 0,
	.x_min			= 0,
	.x_max			= 320,
	.y_min			= 0,
	.y_max			= 480,
	.debounce_max		= 7,
	.debounce_rep		= DEBOUNCE_REPTIME,
	.debounce_tol		= 20,
	.gpio_pendown		= XPT2046_GPIO_INT,
	.penirq_recheck_delay_usecs = 1,
};
#elif defined(CONFIG_TOUCHSCREEN_XPT2046_320X480_CBN_SPI)
static struct xpt2046_platform_data xpt2046_info = {
	.model			= 2046,
	.keep_vref_on 	= 1,
	.swap_xy		= 0,
	.x_min			= 0,
	.x_max			= 320,
	.y_min			= 0,
	.y_max			= 480,
	.debounce_max		= 7,
	.debounce_rep		= DEBOUNCE_REPTIME,
	.debounce_tol		= 20,
	.gpio_pendown		= XPT2046_GPIO_INT,
	.penirq_recheck_delay_usecs = 1,
};
#elif defined(CONFIG_TOUCHSCREEN_XPT2046_SPI) 
static struct xpt2046_platform_data xpt2046_info = {
	.model			= 2046,
	.keep_vref_on 	= 1,
	.swap_xy		= 1,
	.x_min			= 0,
	.x_max			= 800,
	.y_min			= 0,
	.y_max			= 480,
	.debounce_max		= 7,
	.debounce_rep		= DEBOUNCE_REPTIME,
	.debounce_tol		= 20,
	.gpio_pendown		= XPT2046_GPIO_INT,
	
	.penirq_recheck_delay_usecs = 1,
};
#elif defined(CONFIG_TOUCHSCREEN_XPT2046_CBN_SPI)
static struct xpt2046_platform_data xpt2046_info = {
	.model			= 2046,
	.keep_vref_on 	= 1,
	.swap_xy		= 1,
	.x_min			= 0,
	.x_max			= 800,
	.y_min			= 0,
	.y_max			= 480,
	.debounce_max		= 7,
	.debounce_rep		= DEBOUNCE_REPTIME,
	.debounce_tol		= 20,
	.gpio_pendown		= XPT2046_GPIO_INT,
	
	.penirq_recheck_delay_usecs = 1,
};
#endif

static struct spi_board_info board_spi_devices[] = {
#if defined(CONFIG_TOUCHSCREEN_XPT2046_320X480_SPI) || defined(CONFIG_TOUCHSCREEN_XPT2046_320X480_CBN_SPI)\
    ||defined(CONFIG_TOUCHSCREEN_XPT2046_SPI) || defined(CONFIG_TOUCHSCREEN_XPT2046_CBN_SPI)
	{
		.modalias	= "xpt2046_ts",
		.chip_select	= 0,
		.max_speed_hz	= 125 * 1000 * 26,/* (max sample rate @ 3V) * (cmd + data + overhead) */
		.bus_num	= 0,
		.irq = XPT2046_GPIO_INT,
		.platform_data = &xpt2046_info,
	},
#endif
}; 


static void __init rk29_gic_init_irq(void)
{
	gic_dist_init(0, (void __iomem *)RK29_GICPERI_BASE, 32);
	gic_cpu_init(0, (void __iomem *)RK29_GICCPU_BASE);
}

static void __init machine_rk29_init_irq(void)
{
	rk29_gic_init_irq();
	rk29_gpio_init(rk29_gpiobankinit, MAX_BANK);
	rk29_gpio_irq_setup();
}
static void __init machine_rk29_board_init(void)
{ 
	platform_add_devices(devices, ARRAY_SIZE(devices));	
	spi_register_board_info(board_spi_devices, ARRAY_SIZE(board_spi_devices));
}

static void __init machine_rk29_mapio(void)
{
	rk29_map_common_io();
	rk29_clock_init();
	rk29_iomux_init();	
}

MACHINE_START(RK29, "RK29board")

/* UART for LL DEBUG */
	.phys_io	= RK29_UART1_PHYS, 
	.io_pg_offst	= ((RK29_UART1_BASE) >> 18) & 0xfffc,
	.boot_params	= RK29_SDRAM_PHYS + 0x88000,
	.map_io		= machine_rk29_mapio,
	.init_irq	= machine_rk29_init_irq,
	.init_machine	= machine_rk29_board_init,
	.timer		= &rk29_timer,
MACHINE_END
