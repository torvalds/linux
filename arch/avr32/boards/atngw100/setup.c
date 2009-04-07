/*
 * Board-specific setup code for the ATNGW100 Network Gateway
 *
 * Copyright (C) 2005-2006 Atmel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/clk.h>
#include <linux/etherdevice.h>
#include <linux/gpio.h>
#include <linux/irq.h>
#include <linux/i2c.h>
#include <linux/i2c-gpio.h>
#include <linux/init.h>
#include <linux/linkage.h>
#include <linux/platform_device.h>
#include <linux/types.h>
#include <linux/leds.h>
#include <linux/spi/spi.h>
#include <linux/atmel-mci.h>

#include <asm/io.h>
#include <asm/setup.h>

#include <mach/at32ap700x.h>
#include <mach/board.h>
#include <mach/init.h>
#include <mach/portmux.h>

/* Oscillator frequencies. These are board-specific */
unsigned long at32_board_osc_rates[3] = {
	[0] = 32768,	/* 32.768 kHz on RTC osc */
	[1] = 20000000,	/* 20 MHz on osc0 */
	[2] = 12000000,	/* 12 MHz on osc1 */
};

/* Initialized by bootloader-specific startup code. */
struct tag *bootloader_tags __initdata;

struct eth_addr {
	u8 addr[6];
};
static struct eth_addr __initdata hw_addr[2];
static struct eth_platform_data __initdata eth_data[2];

static struct spi_board_info spi0_board_info[] __initdata = {
	{
		.modalias	= "mtd_dataflash",
		.max_speed_hz	= 8000000,
		.chip_select	= 0,
	},
};

static struct mci_platform_data __initdata mci0_data = {
	.slot[0] = {
		.bus_width	= 4,
		.detect_pin	= GPIO_PIN_PC(25),
		.wp_pin		= GPIO_PIN_PE(0),
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
	if (!pclk)
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
	at32_map_usart(1, 0, 0);	/* USART 1: /dev/ttyS0, DB9 */
	at32_setup_serial_console(0);
}

static const struct gpio_led ngw_leds[] = {
	{ .name = "sys", .gpio = GPIO_PIN_PA(16), .active_low = 1,
		.default_trigger = "heartbeat",
	},
	{ .name = "a", .gpio = GPIO_PIN_PA(19), .active_low = 1, },
	{ .name = "b", .gpio = GPIO_PIN_PE(19), .active_low = 1, },
};

static const struct gpio_led_platform_data ngw_led_data = {
	.num_leds =	ARRAY_SIZE(ngw_leds),
	.leds =		(void *) ngw_leds,
};

static struct platform_device ngw_gpio_leds = {
	.name =		"leds-gpio",
	.id =		-1,
	.dev = {
		.platform_data = (void *) &ngw_led_data,
	}
};

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
	/* NOTE:  original ATtiny24 firmware is at address 0x0b */
};

static int __init atngw100_init(void)
{
	unsigned	i;

	/*
	 * ATNGW100 uses 16-bit SDRAM interface, so we don't need to
	 * reserve any pins for it.
	 */

	at32_add_device_usart(0);

	set_hw_addr(at32_add_device_eth(0, &eth_data[0]));
	set_hw_addr(at32_add_device_eth(1, &eth_data[1]));

	at32_add_device_spi(0, spi0_board_info, ARRAY_SIZE(spi0_board_info));
	at32_add_device_mci(0, &mci0_data);
	at32_add_device_usba(0, NULL);

	for (i = 0; i < ARRAY_SIZE(ngw_leds); i++) {
		at32_select_gpio(ngw_leds[i].gpio,
				AT32_GPIOF_OUTPUT | AT32_GPIOF_HIGH);
	}
	platform_device_register(&ngw_gpio_leds);

	/* all these i2c/smbus pins should have external pullups for
	 * open-drain sharing among all I2C devices.  SDA and SCL do;
	 * PB28/EXTINT3 doesn't; it should be SMBALERT# (for PMBus),
	 * but it's not available off-board.
	 */
	at32_select_periph(GPIO_PIOB_BASE, 1 << 28, 0, AT32_GPIOF_PULLUP);
	at32_select_gpio(i2c_gpio_data.sda_pin,
		AT32_GPIOF_MULTIDRV | AT32_GPIOF_OUTPUT | AT32_GPIOF_HIGH);
	at32_select_gpio(i2c_gpio_data.scl_pin,
		AT32_GPIOF_MULTIDRV | AT32_GPIOF_OUTPUT | AT32_GPIOF_HIGH);
	platform_device_register(&i2c_gpio_device);
	i2c_register_board_info(0, i2c_info, ARRAY_SIZE(i2c_info));

	return 0;
}
postcore_initcall(atngw100_init);

static int __init atngw100_arch_init(void)
{
	/* PB30 is the otherwise unused jumper on the mainboard, with an
	 * external pullup; the jumper grounds it.  Use it however you
	 * like, including letting U-Boot or Linux tweak boot sequences.
	 */
	at32_select_gpio(GPIO_PIN_PB(30), 0);
	gpio_request(GPIO_PIN_PB(30), "j15");
	gpio_direction_input(GPIO_PIN_PB(30));
	gpio_export(GPIO_PIN_PB(30), false);

	/* set_irq_type() after the arch_initcall for EIC has run, and
	 * before the I2C subsystem could try using this IRQ.
	 */
	return set_irq_type(AT32_EXTINT(3), IRQ_TYPE_EDGE_FALLING);
}
arch_initcall(atngw100_arch_init);
