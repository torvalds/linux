/*
 * Board-specific setup code for the MIMC200
 *
 * Copyright (C) 2008 Mercury IMC Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

extern struct atmel_lcdfb_info mimc200_lcdc_data;

#include <linux/clk.h>
#include <linux/etherdevice.h>
#include <linux/i2c-gpio.h>
#include <linux/init.h>
#include <linux/linkage.h>
#include <linux/platform_device.h>
#include <linux/types.h>
#include <linux/leds.h>
#include <linux/spi/spi.h>
#include <linux/spi/eeprom.h>

#include <video/atmel_lcdc.h>
#include <linux/fb.h>

#include <linux/atmel-mci.h>
#include <linux/io.h>
#include <asm/setup.h>

#include <mach/at32ap700x.h>
#include <mach/board.h>
#include <mach/init.h>
#include <mach/portmux.h>

/* Oscillator frequencies. These are board-specific */
unsigned long at32_board_osc_rates[3] = {
	[0] = 32768,	/* 32.768 kHz on RTC osc */
	[1] = 10000000,	/* 10 MHz on osc0 */
	[2] = 12000000,	/* 12 MHz on osc1 */
};

/* Initialized by bootloader-specific startup code. */
struct tag *bootloader_tags __initdata;

static struct fb_videomode __initdata pt0434827_modes[] = {
	{
		.name		= "480x272 @ 72",
		.refresh	= 72,
		.xres		= 480,		.yres		= 272,
		.pixclock	= KHZ2PICOS(10000),

		.left_margin	= 1,		.right_margin	= 1,
		.upper_margin	= 12,		.lower_margin	= 1,
		.hsync_len	= 42,		.vsync_len	= 1,

		.sync		= 0,
		.vmode		= FB_VMODE_NONINTERLACED,
	},
};

static struct fb_monspecs __initdata mimc200_default_monspecs = {
	.manufacturer		= "PT",
	.monitor		= "PT0434827-A401",
	.modedb			= pt0434827_modes,
	.modedb_len		= ARRAY_SIZE(pt0434827_modes),
	.hfmin			= 14820,
	.hfmax			= 22230,
	.vfmin			= 60,
	.vfmax			= 85,
	.dclkmax		= 25200000,
};

struct atmel_lcdfb_info __initdata mimc200_lcdc_data = {
	.default_bpp		= 16,
	.default_dmacon		= ATMEL_LCDC_DMAEN | ATMEL_LCDC_DMA2DEN,
	.default_lcdcon2	= (ATMEL_LCDC_DISTYPE_TFT
				   | ATMEL_LCDC_INVCLK
				   | ATMEL_LCDC_CLKMOD_ALWAYSACTIVE
				   | ATMEL_LCDC_MEMOR_BIG),
	.default_monspecs	= &mimc200_default_monspecs,
	.guard_time		= 2,
};

struct eth_addr {
	u8 addr[6];
};
static struct eth_addr __initdata hw_addr[2];
static struct eth_platform_data __initdata eth_data[2];

static struct spi_eeprom eeprom_25lc010 = {
		.name = "25lc010",
		.byte_len = 128,
		.page_size = 16,
		.flags = EE_ADDR1,
};

static struct spi_board_info spi0_board_info[] __initdata = {
	{
		.modalias	= "rtc-ds1390",
		.max_speed_hz	= 4000000,
		.chip_select	= 2,
	},
	{
		.modalias	= "at25",
		.max_speed_hz	= 1000000,
		.chip_select	= 1,
		.mode		= SPI_MODE_3,
		.platform_data	= &eeprom_25lc010,
	},
};

static struct mci_platform_data __initdata mci0_data = {
	.slot[0] = {
		.bus_width	= 4,
		.detect_pin	= GPIO_PIN_PA(26),
		.wp_pin		= GPIO_PIN_PA(27),
	},
};

/*
 * The next two functions should go away as the boot loader is
 * supposed to initialize the macb address registers with a valid
 * ethernet address. But we need to keep it around for a while until
 * we can be reasonably sure the boot loader does this.
 *
 * The phy_id is ignored as the driver will probe for it.
 */
static int __init parse_tag_ethernet(struct tag *tag)
{
	int i;

	i = tag->u.ethernet.mac_index;
	if (i < ARRAY_SIZE(hw_addr))
		memcpy(hw_addr[i].addr, tag->u.ethernet.hw_address,
		       sizeof(hw_addr[i].addr));

	return 0;
}
__tagtable(ATAG_ETHERNET, parse_tag_ethernet);

static void __init set_hw_addr(struct platform_device *pdev)
{
	struct resource *res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	const u8 *addr;
	void __iomem *regs;
	struct clk *pclk;

	if (!res)
		return;
	if (pdev->id >= ARRAY_SIZE(hw_addr))
		return;

	addr = hw_addr[pdev->id].addr;
	if (!is_valid_ether_addr(addr))
		return;

	/*
	 * Since this is board-specific code, we'll cheat and use the
	 * physical address directly as we happen to know that it's
	 * the same as the virtual address.
	 */
	regs = (void __iomem __force *)res->start;
	pclk = clk_get(&pdev->dev, "pclk");
	if (IS_ERR(pclk))
		return;

	clk_enable(pclk);
	__raw_writel((addr[3] << 24) | (addr[2] << 16)
		     | (addr[1] << 8) | addr[0], regs + 0x98);
	__raw_writel((addr[5] << 8) | addr[4], regs + 0x9c);
	clk_disable(pclk);
	clk_put(pclk);
}

void __init setup_board(void)
{
	at32_map_usart(0, 0, 0);	/* USART 0: /dev/ttyS0 (TTL --> Altera) */
	at32_map_usart(1, 1, 0);	/* USART 1: /dev/ttyS1 (RS232) */
	at32_map_usart(2, 2, 0);	/* USART 2: /dev/ttyS2 (RS485) */
	at32_map_usart(3, 3, 0);	/* USART 3: /dev/ttyS3 (RS422 Multidrop) */
}

static struct i2c_gpio_platform_data i2c_gpio_data = {
	.sda_pin		= GPIO_PIN_PA(6),
	.scl_pin		= GPIO_PIN_PA(7),
	.sda_is_open_drain	= 1,
	.scl_is_open_drain	= 1,
	.udelay			= 2,	/* close to 100 kHz */
};

static struct platform_device i2c_gpio_device = {
	.name		= "i2c-gpio",
	.id		= 0,
	.dev		= {
	.platform_data	= &i2c_gpio_data,
	},
};

static struct i2c_board_info __initdata i2c_info[] = {
};

static int __init mimc200_init(void)
{
	/*
	 * MIMC200 uses 16-bit SDRAM interface, so we don't need to
	 * reserve any pins for it.
	 */

	at32_add_device_usart(0);
	at32_add_device_usart(1);
	at32_add_device_usart(2);
	at32_add_device_usart(3);

	set_hw_addr(at32_add_device_eth(0, &eth_data[0]));
	set_hw_addr(at32_add_device_eth(1, &eth_data[1]));

	at32_add_device_spi(0, spi0_board_info, ARRAY_SIZE(spi0_board_info));
	at32_add_device_mci(0, &mci0_data);
	at32_add_device_usba(0, NULL);

	at32_select_periph(GPIO_PIOB_BASE, 1 << 28, 0, AT32_GPIOF_PULLUP);
	at32_select_gpio(i2c_gpio_data.sda_pin,
		AT32_GPIOF_MULTIDRV | AT32_GPIOF_OUTPUT | AT32_GPIOF_HIGH);
	at32_select_gpio(i2c_gpio_data.scl_pin,
		AT32_GPIOF_MULTIDRV | AT32_GPIOF_OUTPUT | AT32_GPIOF_HIGH);
	platform_device_register(&i2c_gpio_device);
	i2c_register_board_info(0, i2c_info, ARRAY_SIZE(i2c_info));

	at32_add_device_lcdc(0, &mimc200_lcdc_data,
			     fbmem_start, fbmem_size,
			     ATMEL_LCDC_CONTROL | ATMEL_LCDC_ALT_CONTROL | ATMEL_LCDC_ALT_24B_DATA);

	return 0;
}
postcore_initcall(mimc200_init);
