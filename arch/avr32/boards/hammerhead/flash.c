/*
 * Hammerhead board-specific flash initialization
 *
 * Copyright (C) 2008 Miromico AG
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
#include <linux/mtd/physmap.h>
#include <linux/usb/isp116x.h>
#include <linux/dma-mapping.h>
#include <linux/delay.h>

#include <mach/portmux.h>
#include <mach/at32ap700x.h>
#include <mach/smc.h>

#include "../../mach-at32ap/clock.h"
#include "flash.h"


#define HAMMERHEAD_USB_PERIPH_GCLK0	0x40000000
#define HAMMERHEAD_USB_PERIPH_CS2	0x02000000
#define HAMMERHEAD_USB_PERIPH_EXTINT0	0x02000000

#define HAMMERHEAD_FPGA_PERIPH_MOSI	0x00000002
#define HAMMERHEAD_FPGA_PERIPH_SCK	0x00000020
#define HAMMERHEAD_FPGA_PERIPH_EXTINT3	0x10000000

static struct smc_timing flash_timing __initdata = {
	.ncs_read_setup		= 0,
	.nrd_setup		= 40,
	.ncs_write_setup	= 0,
	.nwe_setup		= 10,

	.ncs_read_pulse		= 80,
	.nrd_pulse		= 40,
	.ncs_write_pulse	= 65,
	.nwe_pulse		= 55,

	.read_cycle		= 120,
	.write_cycle		= 120,
};

static struct smc_config flash_config __initdata = {
	.bus_width		= 2,
	.nrd_controlled		= 1,
	.nwe_controlled		= 1,
	.byte_write		= 1,
};

static struct mtd_partition flash_parts[] = {
	{
		.name		= "u-boot",
		.offset		= 0x00000000,
		.size		= 0x00020000,           /* 128 KiB */
		.mask_flags	= MTD_WRITEABLE,
	},
	{
		.name		= "root",
		.offset		= 0x00020000,
		.size		= 0x007d0000,
	},
	{
		.name		= "env",
		.offset		= 0x007f0000,
		.size		= 0x00010000,
		.mask_flags	= MTD_WRITEABLE,
	},
};

static struct physmap_flash_data flash_data = {
	.width		= 2,
	.nr_parts	= ARRAY_SIZE(flash_parts),
	.parts		= flash_parts,
};

static struct resource flash_resource = {
	.start		= 0x00000000,
	.end		= 0x007fffff,
	.flags		= IORESOURCE_MEM,
};

static struct platform_device flash_device = {
	.name		= "physmap-flash",
	.id		= 0,
	.resource	= &flash_resource,
	.num_resources	= 1,
	.dev		= { .platform_data = &flash_data, },
};

#ifdef CONFIG_BOARD_HAMMERHEAD_USB

static struct smc_timing isp1160_timing __initdata = {
	.ncs_read_setup		= 75,
	.nrd_setup		= 75,
	.ncs_write_setup	= 75,
	.nwe_setup		= 75,


	/* We use conservative timing settings, as the minimal settings aren't
	   stable. There may be room for tweaking. */
	.ncs_read_pulse		= 75,  /* min. 33ns */
	.nrd_pulse		= 75,  /* min. 33ns */
	.ncs_write_pulse	= 75,  /* min. 26ns */
	.nwe_pulse		= 75,  /* min. 26ns */

	.read_cycle		= 225, /* min. 143ns */
	.write_cycle		= 225, /* min. 136ns */
};

static struct smc_config isp1160_config __initdata = {
	.bus_width		= 2,
	.nrd_controlled		= 1,
	.nwe_controlled		= 1,
	.byte_write		= 0,
};

/*
 * The platform delay function is only used to enforce the strange
 * read to write delay. This can not be configured in the SMC. All other
 * timings are controlled by the SMC (see timings obove)
 * So in isp116x-hcd.c we should comment out USE_PLATFORM_DELAY
 */
void isp116x_delay(struct device *dev, int delay)
{
	if (delay > 150)
		ndelay(delay - 150);
}

static struct  isp116x_platform_data isp1160_data = {
	.sel15Kres		= 1,	/* use internal downstream resistors */
	.oc_enable		= 0,	/* external overcurrent detection */
	.int_edge_triggered	= 0,	/* interrupt is level triggered */
	.int_act_high		= 0,	/* interrupt is active low */
	.delay = isp116x_delay,		/* platform delay function */
};

static struct resource isp1160_resource[] = {
	{
		.start		= 0x08000000,
		.end		= 0x08000001,
		.flags		= IORESOURCE_MEM,
	},
	{
		.start		= 0x08000002,
		.end		= 0x08000003,
		.flags		= IORESOURCE_MEM,
	},
	{
		.start		= 64,
		.flags		= IORESOURCE_IRQ,
	},
};

static struct platform_device isp1160_device = {
	.name		= "isp116x-hcd",
	.id		= 0,
	.resource	= isp1160_resource,
	.num_resources	= 3,
	.dev		= {
		.platform_data = &isp1160_data,
	},
};
#endif

#ifdef CONFIG_BOARD_HAMMERHEAD_USB
static int __init hammerhead_usbh_init(void)
{
	struct clk *gclk;
	struct clk *osc;

	int ret;

	/* setup smc for usbh */
	smc_set_timing(&isp1160_config, &isp1160_timing);
	ret = smc_set_configuration(2, &isp1160_config);

	if (ret < 0) {
		printk(KERN_ERR
		       "hammerhead: failed to set ISP1160 USBH timing\n");
		return ret;
	}

	/* setup gclk0 to run from osc1 */
	gclk = clk_get(NULL, "gclk0");
	if (IS_ERR(gclk)) {
		ret = PTR_ERR(gclk);
		goto err_gclk;
	}

	osc = clk_get(NULL, "osc1");
	if (IS_ERR(osc)) {
		ret = PTR_ERR(osc);
		goto err_osc;
	}

	ret = clk_set_parent(gclk, osc);
	if (ret < 0) {
		pr_debug("hammerhead: failed to set osc1 for USBH clock\n");
		goto err_set_clk;
	}

	/* set clock to 6MHz */
	clk_set_rate(gclk, 6000000);

	/* and enable */
	clk_enable(gclk);

	/* select GCLK0 peripheral function */
	at32_select_periph(GPIO_PIOA_BASE, HAMMERHEAD_USB_PERIPH_GCLK0,
			   GPIO_PERIPH_A, 0);

	/* enable CS2 peripheral function */
	at32_select_periph(GPIO_PIOE_BASE, HAMMERHEAD_USB_PERIPH_CS2,
			   GPIO_PERIPH_A, 0);

	/* H_WAKEUP must be driven low */
	at32_select_gpio(GPIO_PIN_PA(8), AT32_GPIOF_OUTPUT);

	/* Select EXTINT0 for PB25 */
	at32_select_periph(GPIO_PIOB_BASE, HAMMERHEAD_USB_PERIPH_EXTINT0,
			   GPIO_PERIPH_A, 0);

	/* register usbh device driver */
	platform_device_register(&isp1160_device);

 err_set_clk:
	clk_put(osc);
 err_osc:
	clk_put(gclk);
 err_gclk:
	return ret;
}
#endif

#ifdef CONFIG_BOARD_HAMMERHEAD_FPGA
static struct smc_timing fpga_timing __initdata = {
	.ncs_read_setup		= 16,
	.nrd_setup		= 32,
	.ncs_read_pulse		= 48,
	.nrd_pulse		= 32,
	.read_cycle		= 64,

	.ncs_write_setup	= 16,
	.nwe_setup		= 16,
	.ncs_write_pulse	= 32,
	.nwe_pulse		= 32,
	.write_cycle		= 64,
};

static struct smc_config fpga_config __initdata = {
	.bus_width		= 4,
	.nrd_controlled		= 1,
	.nwe_controlled		= 1,
	.byte_write		= 0,
};

static struct resource hh_fpga0_resource[] = {
	{
		.start		= 0xffe00400,
		.end		= 0xffe00400 + 0x3ff,
		.flags		= IORESOURCE_MEM,
	},
	{
		.start		= 4,
		.end		= 4,
		.flags		= IORESOURCE_IRQ,
	},
	{
		.start		= 0x0c000000,
		.end		= 0x0c000100,
		.flags		= IORESOURCE_MEM,
	},
	{
		.start		= 67,
		.end		= 67,
		.flags		= IORESOURCE_IRQ,
	},
};

static u64 hh_fpga0_dma_mask = DMA_BIT_MASK(32);
static struct platform_device hh_fpga0_device = {
	.name		= "hh_fpga",
	.id		= 0,
	.dev		= {
		.dma_mask = &hh_fpga0_dma_mask,
		.coherent_dma_mask = DMA_BIT_MASK(32),
	},
	.resource	= hh_fpga0_resource,
	.num_resources	= ARRAY_SIZE(hh_fpga0_resource),
};

static struct clk hh_fpga0_spi_clk = {
	.name		= "spi_clk",
	.dev		= &hh_fpga0_device.dev,
	.mode		= pba_clk_mode,
	.get_rate	= pba_clk_get_rate,
	.index		= 1,
};

struct platform_device *__init at32_add_device_hh_fpga(void)
{
	/* Select peripheral functionallity for SPI SCK and MOSI */
	at32_select_periph(GPIO_PIOB_BASE, HAMMERHEAD_FPGA_PERIPH_SCK,
			   GPIO_PERIPH_B, 0);
	at32_select_periph(GPIO_PIOB_BASE, HAMMERHEAD_FPGA_PERIPH_MOSI,
			   GPIO_PERIPH_B, 0);

	/* reserve all other needed gpio
	 * We have on board pull ups, so there is no need
	 * to enable gpio pull ups */
	/* INIT_DONE (input) */
	at32_select_gpio(GPIO_PIN_PB(0), 0);

	/* nSTATUS (input) */
	at32_select_gpio(GPIO_PIN_PB(2), 0);

	/* nCONFIG (output, low) */
	at32_select_gpio(GPIO_PIN_PB(3), AT32_GPIOF_OUTPUT);

	/* CONF_DONE (input) */
	at32_select_gpio(GPIO_PIN_PB(4), 0);

	/* Select EXTINT3 for PB28 (Interrupt from FPGA) */
	at32_select_periph(GPIO_PIOB_BASE, HAMMERHEAD_FPGA_PERIPH_EXTINT3,
			   GPIO_PERIPH_A, 0);

	/* Get our parent clock */
	hh_fpga0_spi_clk.parent = clk_get(NULL, "pba");
	clk_put(hh_fpga0_spi_clk.parent);

	/* Register clock in at32 clock tree */
	at32_clk_register(&hh_fpga0_spi_clk);

	platform_device_register(&hh_fpga0_device);
	return &hh_fpga0_device;
}
#endif

/* This needs to be called after the SMC has been initialized */
static int __init hammerhead_flash_init(void)
{
	int ret;

	smc_set_timing(&flash_config, &flash_timing);
	ret = smc_set_configuration(0, &flash_config);

	if (ret < 0) {
		printk(KERN_ERR "hammerhead: failed to set NOR flash timing\n");
		return ret;
	}

	platform_device_register(&flash_device);

#ifdef CONFIG_BOARD_HAMMERHEAD_USB
	hammerhead_usbh_init();
#endif

#ifdef CONFIG_BOARD_HAMMERHEAD_FPGA
	/* Setup SMC for FPGA interface */
	smc_set_timing(&fpga_config, &fpga_timing);
	ret = smc_set_configuration(3, &fpga_config);
#endif


	if (ret < 0) {
		printk(KERN_ERR "hammerhead: failed to set FPGA timing\n");
		return ret;
	}

	return 0;
}

device_initcall(hammerhead_flash_init);
