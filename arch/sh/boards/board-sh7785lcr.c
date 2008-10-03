/*
 * Renesas Technology Corp. R0P7785LC0011RL Support.
 *
 * Copyright (C) 2008  Yoshihiro Shimoda
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/sm501.h>
#include <linux/sm501-regs.h>
#include <linux/fb.h>
#include <linux/mtd/physmap.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/i2c-pca-platform.h>
#include <linux/i2c-algo-pca.h>
#include <asm/heartbeat.h>
#include <asm/sh7785lcr.h>

/*
 * NOTE: This board has 2 physical memory maps.
 *	 Please look at include/asm-sh/sh7785lcr.h or hardware manual.
 */
static struct resource heartbeat_resources[] = {
	[0] = {
		.start	= PLD_LEDCR,
		.end	= PLD_LEDCR,
		.flags	= IORESOURCE_MEM,
	},
};

static struct heartbeat_data heartbeat_data = {
	.regsize = 8,
};

static struct platform_device heartbeat_device = {
	.name		= "heartbeat",
	.id		= -1,
	.dev	= {
		.platform_data	= &heartbeat_data,
	},
	.num_resources	= ARRAY_SIZE(heartbeat_resources),
	.resource	= heartbeat_resources,
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

static struct resource r8a66597_usb_host_resources[] = {
	[0] = {
		.name	= "r8a66597_hcd",
		.start	= R8A66597_ADDR,
		.end	= R8A66597_ADDR + R8A66597_SIZE - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.name	= "r8a66597_hcd",
		.start	= 2,
		.end	= 2,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device r8a66597_usb_host_device = {
	.name		= "r8a66597_hcd",
	.id		= -1,
	.dev = {
		.dma_mask		= NULL,
		.coherent_dma_mask	= 0xffffffff,
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
		.start	= 10,
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

static struct resource i2c_resources[] = {
	[0] = {
		.start	= PCA9564_ADDR,
		.end	= PCA9564_ADDR + PCA9564_SIZE - 1,
		.flags	= IORESOURCE_MEM | IORESOURCE_MEM_8BIT,
	},
	[1] = {
		.start	= 12,
		.end	= 12,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct i2c_pca9564_pf_platform_data i2c_platform_data = {
	.gpio			= 0,
	.i2c_clock_speed	= I2C_PCA_CON_330kHz,
	.timeout		= 100,
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

	return platform_add_devices(sh7785lcr_devices,
				    ARRAY_SIZE(sh7785lcr_devices));
}
__initcall(sh7785lcr_devices_setup);

/* Initialize IRQ setting */
void __init init_sh7785lcr_IRQ(void)
{
	plat_irq_setup_pins(IRQ_MODE_IRQ7654);
	plat_irq_setup_pins(IRQ_MODE_IRQ3210);
}

static void sh7785lcr_power_off(void)
{
	ctrl_outb(0x01, P2SEGADDR(PLD_POFCR));
}

/* Initialize the board */
static void __init sh7785lcr_setup(char **cmdline_p)
{
	void __iomem *sm501_reg;

	printk(KERN_INFO "Renesas Technology Corp. R0P7785LC0011RL support.\n");

	pm_power_off = sh7785lcr_power_off;

	/* sm501 DRAM configuration */
	sm501_reg = (void __iomem *)0xb3e00000 + SM501_DRAM_CONTROL;
	writel(0x000307c2, sm501_reg);
}

/*
 * The Machine Vector
 */
static struct sh_machine_vector mv_sh7785lcr __initmv = {
	.mv_name		= "SH7785LCR",
	.mv_setup		= sh7785lcr_setup,
	.mv_init_irq		= init_sh7785lcr_IRQ,
};

