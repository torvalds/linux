// SPDX-License-Identifier: GPL-2.0
/*
 * Renesas Technology Corp. R0P7785LC0011RL Support.
 *
 * Copyright (C) 2008  Yoshihiro Shimoda
 * Copyright (C) 2009  Paul Mundt
 */
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/sm501.h>
#include <linux/sm501-regs.h>
#include <linux/fb.h>
#include <linux/mtd/physmap.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/i2c.h>
#include <linux/platform_data/i2c-pca-platform.h>
#include <linux/i2c-algo-pca.h>
#include <linux/usb/r8a66597.h>
#include <linux/sh_intc.h>
#include <linux/irq.h>
#include <linux/io.h>
#include <linux/clk.h>
#include <linux/errno.h>
#include <linux/gpio/machine.h>
#include <mach/sh7785lcr.h>
#include <cpu/sh7785.h>
#include <asm/heartbeat.h>
#include <asm/clock.h>
#include <asm/bl_bit.h>

/*
 * NOTE: This board has 2 physical memory maps.
 *	 Please look at include/asm-sh/sh7785lcr.h or hardware manual.
 */
static struct resource heartbeat_resource = {
	.start	= PLD_LEDCR,
	.end	= PLD_LEDCR,
	.flags	= IORESOURCE_MEM | IORESOURCE_MEM_8BIT,
};

static struct platform_device heartbeat_device = {
	.name		= "heartbeat",
	.id		= -1,
	.num_resources	= 1,
	.resource	= &heartbeat_resource,
};

static struct mtd_partition nor_flash_partitions[] = {
	{
		.name		= "loader",
		.offset		= 0x00000000,
		.size		= 512 * 1024,
	},
	{
		.name		= "bootenv",
		.offset		= MTDPART_OFS_APPEND,
		.size		= 512 * 1024,
	},
	{
		.name		= "kernel",
		.offset		= MTDPART_OFS_APPEND,
		.size		= 4 * 1024 * 1024,
	},
	{
		.name		= "data",
		.offset		= MTDPART_OFS_APPEND,
		.size		= MTDPART_SIZ_FULL,
	},
};

static struct physmap_flash_data nor_flash_data = {
	.width		= 4,
	.parts		= nor_flash_partitions,
	.nr_parts	= ARRAY_SIZE(nor_flash_partitions),
};

static struct resource nor_flash_resources[] = {
	[0]	= {
		.start	= NOR_FLASH_ADDR,
		.end	= NOR_FLASH_ADDR + NOR_FLASH_SIZE - 1,
		.flags	= IORESOURCE_MEM,
	}
};

static struct platform_device nor_flash_device = {
	.name		= "physmap-flash",
	.dev		= {
		.platform_data	= &nor_flash_data,
	},
	.num_resources	= ARRAY_SIZE(nor_flash_resources),
	.resource	= nor_flash_resources,
};

static struct r8a66597_platdata r8a66597_data = {
	.xtal = R8A66597_PLATDATA_XTAL_12MHZ,
	.vif = 1,
};

static struct resource r8a66597_usb_host_resources[] = {
	[0] = {
		.start	= R8A66597_ADDR,
		.end	= R8A66597_ADDR + R8A66597_SIZE - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= evt2irq(0x240),
		.end	= evt2irq(0x240),
		.flags	= IORESOURCE_IRQ | IRQF_TRIGGER_LOW,
	},
};

static struct platform_device r8a66597_usb_host_device = {
	.name		= "r8a66597_hcd",
	.id		= -1,
	.dev = {
		.dma_mask		= NULL,
		.coherent_dma_mask	= 0xffffffff,
		.platform_data		= &r8a66597_data,
	},
	.num_resources	= ARRAY_SIZE(r8a66597_usb_host_resources),
	.resource	= r8a66597_usb_host_resources,
};

static struct resource sm501_resources[] = {
	[0]	= {
		.start	= SM107_MEM_ADDR,
		.end	= SM107_MEM_ADDR + SM107_MEM_SIZE - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1]	= {
		.start	= SM107_REG_ADDR,
		.end	= SM107_REG_ADDR + SM107_REG_SIZE - 1,
		.flags	= IORESOURCE_MEM,
	},
	[2]	= {
		.start	= evt2irq(0x340),
		.flags	= IORESOURCE_IRQ,
	},
};

static struct fb_videomode sm501_default_mode_crt = {
	.pixclock	= 35714,	/* 28MHz */
	.xres		= 640,
	.yres		= 480,
	.left_margin	= 105,
	.right_margin	= 16,
	.upper_margin	= 33,
	.lower_margin	= 10,
	.hsync_len	= 39,
	.vsync_len	= 2,
	.sync = FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,
};

static struct fb_videomode sm501_default_mode_pnl = {
	.pixclock	= 40000,	/* 25MHz */
	.xres		= 640,
	.yres		= 480,
	.left_margin	= 2,
	.right_margin	= 16,
	.upper_margin	= 33,
	.lower_margin	= 10,
	.hsync_len	= 39,
	.vsync_len	= 2,
	.sync		= 0,
};

static struct sm501_platdata_fbsub sm501_pdata_fbsub_pnl = {
	.def_bpp	= 16,
	.def_mode	= &sm501_default_mode_pnl,
	.flags		= SM501FB_FLAG_USE_INIT_MODE |
			  SM501FB_FLAG_USE_HWCURSOR |
			  SM501FB_FLAG_USE_HWACCEL |
			  SM501FB_FLAG_DISABLE_AT_EXIT |
			  SM501FB_FLAG_PANEL_NO_VBIASEN,
};

static struct sm501_platdata_fbsub sm501_pdata_fbsub_crt = {
	.def_bpp	= 16,
	.def_mode	= &sm501_default_mode_crt,
	.flags		= SM501FB_FLAG_USE_INIT_MODE |
			  SM501FB_FLAG_USE_HWCURSOR |
			  SM501FB_FLAG_USE_HWACCEL |
			  SM501FB_FLAG_DISABLE_AT_EXIT,
};

static struct sm501_platdata_fb sm501_fb_pdata = {
	.fb_route	= SM501_FB_OWN,
	.fb_crt		= &sm501_pdata_fbsub_crt,
	.fb_pnl		= &sm501_pdata_fbsub_pnl,
};

static struct sm501_initdata sm501_initdata = {
	.gpio_high	= {
		.set	= 0x00001fe0,
		.mask	= 0x0,
	},
	.devices	= 0,
	.mclk		= 84 * 1000000,
	.m1xclk		= 112 * 1000000,
};

static struct sm501_platdata sm501_platform_data = {
	.init		= &sm501_initdata,
	.fb		= &sm501_fb_pdata,
};

static struct platform_device sm501_device = {
	.name		= "sm501",
	.id		= -1,
	.dev		= {
		.platform_data	= &sm501_platform_data,
	},
	.num_resources	= ARRAY_SIZE(sm501_resources),
	.resource	= sm501_resources,
};

static struct resource i2c_proto_resources[] = {
	[0] = {
		.start	= PCA9564_PROTO_32BIT_ADDR,
		.end	= PCA9564_PROTO_32BIT_ADDR + PCA9564_SIZE - 1,
		.flags	= IORESOURCE_MEM | IORESOURCE_MEM_8BIT,
	},
	[1] = {
		.start	= evt2irq(0x380),
		.end	= evt2irq(0x380),
		.flags	= IORESOURCE_IRQ,
	},
};

static struct resource i2c_resources[] = {
	[0] = {
		.start	= PCA9564_ADDR,
		.end	= PCA9564_ADDR + PCA9564_SIZE - 1,
		.flags	= IORESOURCE_MEM | IORESOURCE_MEM_8BIT,
	},
	[1] = {
		.start	= evt2irq(0x380),
		.end	= evt2irq(0x380),
		.flags	= IORESOURCE_IRQ,
	},
};

static struct gpiod_lookup_table i2c_gpio_table = {
	.dev_id = "i2c.0",
	.table = {
		GPIO_LOOKUP("pfc-sh7757", 0, "reset-gpios", GPIO_ACTIVE_LOW),
		{ },
	},
};

static struct i2c_pca9564_pf_platform_data i2c_platform_data = {
	.i2c_clock_speed	= I2C_PCA_CON_330kHz,
	.timeout		= HZ,
};

static struct platform_device i2c_device = {
	.name		= "i2c-pca-platform",
	.id		= -1,
	.dev		= {
		.platform_data	= &i2c_platform_data,
	},
	.num_resources	= ARRAY_SIZE(i2c_resources),
	.resource	= i2c_resources,
};

static struct platform_device *sh7785lcr_devices[] __initdata = {
	&heartbeat_device,
	&nor_flash_device,
	&r8a66597_usb_host_device,
	&sm501_device,
	&i2c_device,
};

static struct i2c_board_info __initdata sh7785lcr_i2c_devices[] = {
	{
		I2C_BOARD_INFO("r2025sd", 0x32),
	},
};

static int __init sh7785lcr_devices_setup(void)
{
	i2c_register_board_info(0, sh7785lcr_i2c_devices,
				ARRAY_SIZE(sh7785lcr_i2c_devices));

	if (mach_is_sh7785lcr_pt()) {
		i2c_device.resource = i2c_proto_resources;
		i2c_device.num_resources = ARRAY_SIZE(i2c_proto_resources);
	}

	gpiod_add_lookup_table(&i2c_gpio_table);
	return platform_add_devices(sh7785lcr_devices,
				    ARRAY_SIZE(sh7785lcr_devices));
}
device_initcall(sh7785lcr_devices_setup);

/* Initialize IRQ setting */
static void __init init_sh7785lcr_IRQ(void)
{
	plat_irq_setup_pins(IRQ_MODE_IRQ7654);
	plat_irq_setup_pins(IRQ_MODE_IRQ3210);
}

static int sh7785lcr_clk_init(void)
{
	struct clk *clk;
	int ret;

	clk = clk_get(NULL, "extal");
	if (IS_ERR(clk))
		return PTR_ERR(clk);
	ret = clk_set_rate(clk, 33333333);
	clk_put(clk);

	return ret;
}

static void sh7785lcr_power_off(void)
{
	unsigned char *p;

	p = ioremap(PLD_POFCR, PLD_POFCR + 1);
	if (!p) {
		printk(KERN_ERR "%s: ioremap error.\n", __func__);
		return;
	}
	*p = 0x01;
	iounmap(p);
	set_bl_bit();
	while (1)
		cpu_relax();
}

/* Initialize the board */
static void __init sh7785lcr_setup(char **cmdline_p)
{
	void __iomem *sm501_reg;

	printk(KERN_INFO "Renesas Technology Corp. R0P7785LC0011RL support.\n");

	pm_power_off = sh7785lcr_power_off;

	/* sm501 DRAM configuration */
	sm501_reg = ioremap(SM107_REG_ADDR, SM501_DRAM_CONTROL);
	if (!sm501_reg) {
		printk(KERN_ERR "%s: ioremap error.\n", __func__);
		return;
	}

	writel(0x000307c2, sm501_reg + SM501_DRAM_CONTROL);
	iounmap(sm501_reg);
}

/* Return the board specific boot mode pin configuration */
static int sh7785lcr_mode_pins(void)
{
	int value = 0;

	/* These are the factory default settings of S1 and S2.
	 * If you change these dip switches then you will need to
	 * adjust the values below as well.
	 */
	value |= MODE_PIN4; /* Clock Mode 16 */
	value |= MODE_PIN5; /* 32-bit Area0 bus width */
	value |= MODE_PIN6; /* 32-bit Area0 bus width */
	value |= MODE_PIN7; /* Area 0 SRAM interface [fixed] */
	value |= MODE_PIN8; /* Little Endian */
	value |= MODE_PIN9; /* Master Mode */
	value |= MODE_PIN14; /* No PLL step-up */

	return value;
}

/*
 * The Machine Vector
 */
static struct sh_machine_vector mv_sh7785lcr __initmv = {
	.mv_name		= "SH7785LCR",
	.mv_setup		= sh7785lcr_setup,
	.mv_clk_init		= sh7785lcr_clk_init,
	.mv_init_irq		= init_sh7785lcr_IRQ,
	.mv_mode_pins		= sh7785lcr_mode_pins,
};

