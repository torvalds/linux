/* linux/arch/arm/mach-rk2818/board-midsdk.c
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
#include <linux/spi/spi.h>

#include <mach/hardware.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/flash.h>

#include <mach/irqs.h>
#include <mach/board.h>
#include <mach/rk2818_iomap.h>
#include <mach/iomux.h>
#include <mach/gpio.h>

#include <linux/mtd/nand.h>
#include <linux/mtd/partitions.h>

#include "devices.h"


/* --------------------------------------------------------------------
 *  声明了rk2818_gpioBank数组，并定义了GPIO寄存器组ID和寄存器基地址。
 * -------------------------------------------------------------------- */

static struct rk2818_gpio_bank rk2818_gpioBank[] = {
		{
		.id		= RK2818_ID_PIOA,
		.offset		= RK2818_GPIO0_BASE,
		.clock		= NULL,
	}, 
		{
		.id		= RK2818_ID_PIOB,
		.offset		= RK2818_GPIO0_BASE,
		.clock		= NULL,
	}, 
		{
		.id		= RK2818_ID_PIOC,
		.offset		= RK2818_GPIO0_BASE,
		.clock		= NULL,
	}, 
		{
		.id		= RK2818_ID_PIOD,
		.offset		= RK2818_GPIO0_BASE,
		.clock		= NULL,
	},
		{
		.id		= RK2818_ID_PIOE,
		.offset		= RK2818_GPIO1_BASE,
		.clock		= NULL,
	},
		{
		.id		= RK2818_ID_PIOF,
		.offset		= RK2818_GPIO1_BASE,
		.clock		= NULL,
	},
		{
		.id		= RK2818_ID_PIOG,
		.offset		= RK2818_GPIO1_BASE,
		.clock		= NULL,
	},
		{
		.id		= RK2818_ID_PIOH,
		.offset		= RK2818_GPIO1_BASE,
		.clock		= NULL,
	}
};

//IO映射方式描述 ，每个为一段线性连续映射
static struct map_desc rk2818_io_desc[] __initdata = {

	{
		.virtual	= RK2818_MCDMA_BASE,					//虚拟地址
		.pfn		= __phys_to_pfn(RK2818_MCDMA_PHYS),    //物理地址，须与页表对齐
		.length 	= RK2818_MCDMA_SIZE,							//长度
		.type		= MT_DEVICE							//映射方式
	},
	
	{
		.virtual	= RK2818_DWDMA_BASE,					
		.pfn		= __phys_to_pfn(RK2818_DWDMA_PHYS),    
		.length 	= RK2818_DWDMA_SIZE,						
		.type		= MT_DEVICE							
	},
	
	{
		.virtual	= RK2818_INTC_BASE,					
		.pfn		= __phys_to_pfn(RK2818_INTC_PHYS),   
		.length 	= RK2818_INTC_SIZE,					
		.type		= MT_DEVICE						
	},
	
	{
		.virtual	= RK2818_SDRAMC_BASE,
		.pfn		= __phys_to_pfn(RK2818_SDRAMC_PHYS),
		.length 	= RK2818_SDRAMC_SIZE,
		.type		= MT_DEVICE
	},

	{
		.virtual	= RK2818_ARMDARBITER_BASE,					
		.pfn		= __phys_to_pfn(RK2818_ARMDARBITER_PHYS),    
		.length 	= RK2818_ARMDARBITER_SIZE,						
		.type		= MT_DEVICE							
	},
	
	{
		.virtual	= RK2818_APB_BASE,
		.pfn		= __phys_to_pfn(RK2818_APB_PHYS),
		.length 	= 0xa0000,                     
		.type		= MT_DEVICE
	},
	
	{
		.virtual	= RK2818_WDT_BASE,
		.pfn		= __phys_to_pfn(RK2818_WDT_PHYS),
		.length 	= 0xa0000,                      ///apb bus i2s i2c spi no map in this
		.type		= MT_DEVICE
	},
};

/*****************************************************************************************
 * I2C devices
 *author: kfx
 *****************************************************************************************/
static struct rk2818_i2c_platform_data default_i2c0_data __initdata = { 
	.bus_num    = 0,
	.flags      = 0,
	.slave_addr = 0xff,
	.scl_rate  = 400*1000,
	.clk_id  = "i2c0",
};
static struct rk2818_i2c_platform_data default_i2c1_data __initdata = { 
	.bus_num    = 1,
	.flags      = 0,
	.slave_addr = 0xff,
	.scl_rate  = 400*1000,
	.clk_id  = "i2c1",
};

static struct i2c_board_info __initdata board_i2c0_devices[] = {
#if defined (CONFIG_RK1000_CONTROL)
	{
		.type    		= "rk1000_control",
		.addr           = 0x40,
		.flags			= 0,
	},
#endif

#if defined (CONFIG_RK1000_TVOUT)
	{
		.type    		= "rk1000_tvout",
		.addr           = 0x42,
		.flags			= 0,
	},
#endif
#if defined (CONFIG_SND_SOC_RK1000)
	{
		.type    		= "rk1000_i2c_codec",
		.addr           = 0x60,
		.flags			= 0,
	},
#endif
	{}
};
static struct i2c_board_info __initdata board_i2c1_devices[] = {
#if defined (CONFIG_RTC_HYM8563)
	{
		.type    		= "rtc_hym8563",
		.addr           = 0x51,
		.flags			= 0,
	},
#endif
#if defined (CONFIG_FM_QN8006)
	{
		.type    		= "fm_qn8006",
		.addr           = 0x2b, 
		.flags			= 0,
	},
#endif
	{}
};

static void rk2818_i2c0_cfg_gpio(struct platform_device *dev)
{
	rk2818_mux_api_set(GPIOE_I2C0_SEL_NAME, IOMUXA_I2C0);
}

static void rk2818_i2c1_cfg_gpio(struct platform_device *dev)
{
	rk2818_mux_api_set(GPIOE_U1IR_I2C1_NAME, IOMUXA_I2C1);
}


static void __init rk2818_i2c0_set_platdata(struct rk2818_i2c_platform_data *pd)
{
	struct rk2818_i2c_platform_data *npd;

	if (!pd)
		pd = &default_i2c0_data;

	npd = kmemdup(pd, sizeof(struct rk2818_i2c_platform_data), GFP_KERNEL);
	if (!npd)
		printk(KERN_ERR "%s: no memory for platform data\n", __func__);
	else if (!npd->cfg_gpio)
		npd->cfg_gpio = rk2818_i2c0_cfg_gpio;

	rk2818_device_i2c0.dev.platform_data = npd;
}
static void __init rk2818_i2c1_set_platdata(struct rk2818_i2c_platform_data *pd)
{
	struct rk2818_i2c_platform_data *npd;

	if (!pd)
		pd = &default_i2c1_data;

	npd = kmemdup(pd, sizeof(struct rk2818_i2c_platform_data), GFP_KERNEL);
	if (!npd)
		printk(KERN_ERR "%s: no memory for platform data\n", __func__);
	else if (!npd->cfg_gpio)
		npd->cfg_gpio = rk2818_i2c1_cfg_gpio;

	rk2818_device_i2c1.dev.platform_data = npd;
}

static void __init rk2818_i2c_board_init(void)
{
	rk2818_i2c0_set_platdata(NULL);
	rk2818_i2c1_set_platdata(NULL);
	i2c_register_board_info(0, board_i2c0_devices,
			ARRAY_SIZE(board_i2c0_devices));
	i2c_register_board_info(1, board_i2c1_devices,
			ARRAY_SIZE(board_i2c1_devices));
}
/*****************************************************************************************
 * SPI devices
 *author: lhh
 *****************************************************************************************/
static struct spi_board_info board_spi_devices[] = {
	{	/* net chip */
		.modalias	= "enc28j60",
		.chip_select	= 1,
		.max_speed_hz	= 12 * 1000 * 1000,
		.bus_num	= 0,
		.mode	= SPI_MODE_0,
	},

  	{	
		.modalias	= "xpt2046_ts",
		.chip_select	= 0,
		.max_speed_hz	= 1000000,
		.bus_num	= 0,
		.mode	= SPI_MODE_0,
	},

};

static struct platform_device *devices[] __initdata = {
	&rk2818_device_uart1,
	&rk2818_device_spim,
};

extern struct sys_timer rk2818_timer;

static void __init machine_rk2818_init_irq(void)
{
	rk2818_init_irq();
	rk2818_gpio_init(rk2818_gpioBank, 8);
	rk2818_gpio_irq_setup();
}

static void __init machine_rk2818_board_init(void)
{
	rk2818_i2c_board_init();
	platform_add_devices(devices, ARRAY_SIZE(devices));
	spi_register_board_info(board_spi_devices, ARRAY_SIZE(board_spi_devices));
	rk2818_mux_api_set(GPIOB4_SPI0CS0_MMC0D4_NAME,IOMUXA_GPIO0_B4); //IOMUXA_SPI0_CSN0);//use for gpio SPI CS0
	rk2818_mux_api_set(GPIOB0_SPI0CSN1_MMC1PCA_NAME,IOMUXA_GPIO0_B0); //IOMUXA_SPI0_CSN1);//use for gpio SPI CS1
	rk2818_mux_api_set(GPIOB_SPI0_MMC0_NAME,IOMUXA_SPI0);//use for SPI CLK SDI SDO
}

static void __init machine_rk2818_mapio(void)
{
	iotable_init(rk2818_io_desc, ARRAY_SIZE(rk2818_io_desc));
	rk2818_clock_init();
	rk2818_iomux_init();	
}

MACHINE_START(RK2818, "rk2818midsdk")

/* UART for LL DEBUG */
	.phys_io	= 0x18002000,
	.io_pg_offst	= ((0xFF100000) >> 18) & 0xfffc,
	.boot_params	= RK2818_SDRAM_PHYS + 0x100,
	.map_io		= machine_rk2818_mapio,
	.init_irq	= machine_rk2818_init_irq,
	.init_machine	= machine_rk2818_board_init,
	.timer		= &rk2818_timer,
MACHINE_END
