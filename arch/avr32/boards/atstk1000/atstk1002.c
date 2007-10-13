/*
 * ATSTK1002 daughterboard-specific init code
 *
 * Copyright (C) 2005-2006 Atmel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/clk.h>
#include <linux/etherdevice.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/leds.h>
#include <linux/platform_device.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/spi/spi.h>

#include <video/atmel_lcdc.h>

#include <asm/io.h>
#include <asm/setup.h>
#include <asm/arch/at32ap7000.h>
#include <asm/arch/board.h>
#include <asm/arch/init.h>
#include <asm/arch/portmux.h>

#include "atstk1000.h"


struct eth_addr {
	u8 addr[6];
};

static struct eth_addr __initdata hw_addr[2];
static struct eth_platform_data __initdata eth_data[2] = {
	{
		/*
		 * The MDIO pullups on STK1000 are a bit too weak for
		 * the autodetection to work properly, so we have to
		 * mask out everything but the correct address.
		 */
		.phy_mask	= ~(1U << 16),
	},
	{
		.phy_mask	= ~(1U << 17),
	},
};

#ifndef CONFIG_BOARD_ATSTK1002_SW1_CUSTOM
static struct spi_board_info spi0_board_info[] __initdata = {
	{
		/* QVGA display */
		.modalias	= "ltv350qv",
		.max_speed_hz	= 16000000,
		.chip_select	= 1,
		.mode		= SPI_MODE_3,
	},
};
#endif

#ifdef CONFIG_BOARD_ATSTK1002_SPI1
static struct spi_board_info spi1_board_info[] __initdata = { {
	/* patch in custom entries here */
} };
#endif

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

#ifdef CONFIG_BOARD_ATSTK1002_J2_LED

static struct gpio_led stk_j2_led[] = {
#ifdef CONFIG_BOARD_ATSTK1002_J2_LED8
#define LEDSTRING "J2 jumpered to LED8"
	{ .name = "led0:amber", .gpio = GPIO_PIN_PB( 8), },
	{ .name = "led1:amber", .gpio = GPIO_PIN_PB( 9), },
	{ .name = "led2:amber", .gpio = GPIO_PIN_PB(10), },
	{ .name = "led3:amber", .gpio = GPIO_PIN_PB(13), },
	{ .name = "led4:amber", .gpio = GPIO_PIN_PB(14), },
	{ .name = "led5:amber", .gpio = GPIO_PIN_PB(15), },
	{ .name = "led6:amber", .gpio = GPIO_PIN_PB(16), },
	{ .name = "led7:amber", .gpio = GPIO_PIN_PB(30),
			.default_trigger = "heartbeat", },
#else	/* RGB */
#define LEDSTRING "J2 jumpered to RGB LEDs"
	{ .name = "r1:red",     .gpio = GPIO_PIN_PB( 8), },
	{ .name = "g1:green",   .gpio = GPIO_PIN_PB(10), },
	{ .name = "b1:blue",    .gpio = GPIO_PIN_PB(14), },

	{ .name = "r2:red",     .gpio = GPIO_PIN_PB( 9),
			.default_trigger = "heartbeat", },
	{ .name = "g2:green",   .gpio = GPIO_PIN_PB(13), },
	{ .name = "b2:blue",    .gpio = GPIO_PIN_PB(15),
			.default_trigger = "heartbeat", },
	/* PB16, PB30 unused */
#endif
};

static struct gpio_led_platform_data stk_j2_led_data = {
	.num_leds	= ARRAY_SIZE(stk_j2_led),
	.leds		= stk_j2_led,
};

static struct platform_device stk_j2_led_dev = {
	.name		= "leds-gpio",
	.id		= 2,	/* gpio block J2 */
	.dev		= {
		.platform_data	= &stk_j2_led_data,
	},
};

static void setup_j2_leds(void)
{
	unsigned	i;

	for (i = 0; i < ARRAY_SIZE(stk_j2_led); i++)
		at32_select_gpio(stk_j2_led[i].gpio, AT32_GPIOF_OUTPUT);

	printk("STK1002: " LEDSTRING "\n");
	platform_device_register(&stk_j2_led_dev);
}

#else
static void setup_j2_leds(void)
{
}
#endif

void __init setup_board(void)
{
#ifdef	CONFIG_BOARD_ATSTK1002_SW2_CUSTOM
	at32_map_usart(0, 1);	/* USART 0/B: /dev/ttyS1, IRDA */
#else
	at32_map_usart(1, 0);	/* USART 1/A: /dev/ttyS0, DB9 */
#endif
	/* USART 2/unused: expansion connector */
	at32_map_usart(3, 2);	/* USART 3/C: /dev/ttyS2, DB9 */

	at32_setup_serial_console(0);
}

static int __init atstk1002_init(void)
{
	/*
	 * ATSTK1000 uses 32-bit SDRAM interface. Reserve the
	 * SDRAM-specific pins so that nobody messes with them.
	 */
	at32_reserve_pin(GPIO_PIN_PE(0));	/* DATA[16]	*/
	at32_reserve_pin(GPIO_PIN_PE(1));	/* DATA[17]	*/
	at32_reserve_pin(GPIO_PIN_PE(2));	/* DATA[18]	*/
	at32_reserve_pin(GPIO_PIN_PE(3));	/* DATA[19]	*/
	at32_reserve_pin(GPIO_PIN_PE(4));	/* DATA[20]	*/
	at32_reserve_pin(GPIO_PIN_PE(5));	/* DATA[21]	*/
	at32_reserve_pin(GPIO_PIN_PE(6));	/* DATA[22]	*/
	at32_reserve_pin(GPIO_PIN_PE(7));	/* DATA[23]	*/
	at32_reserve_pin(GPIO_PIN_PE(8));	/* DATA[24]	*/
	at32_reserve_pin(GPIO_PIN_PE(9));	/* DATA[25]	*/
	at32_reserve_pin(GPIO_PIN_PE(10));	/* DATA[26]	*/
	at32_reserve_pin(GPIO_PIN_PE(11));	/* DATA[27]	*/
	at32_reserve_pin(GPIO_PIN_PE(12));	/* DATA[28]	*/
	at32_reserve_pin(GPIO_PIN_PE(13));	/* DATA[29]	*/
	at32_reserve_pin(GPIO_PIN_PE(14));	/* DATA[30]	*/
	at32_reserve_pin(GPIO_PIN_PE(15));	/* DATA[31]	*/
	at32_reserve_pin(GPIO_PIN_PE(26));	/* SDCS		*/

	at32_add_system_devices();

#ifdef	CONFIG_BOARD_ATSTK1002_SW2_CUSTOM
	at32_add_device_usart(1);
#else
	at32_add_device_usart(0);
#endif
	at32_add_device_usart(2);

#ifndef CONFIG_BOARD_ATSTK1002_SW6_CUSTOM
	set_hw_addr(at32_add_device_eth(0, &eth_data[0]));
#endif
#ifndef CONFIG_BOARD_ATSTK1002_SW1_CUSTOM
	at32_add_device_spi(0, spi0_board_info, ARRAY_SIZE(spi0_board_info));
#endif
#ifdef CONFIG_BOARD_ATSTK1002_SPI1
	at32_add_device_spi(1, spi1_board_info, ARRAY_SIZE(spi1_board_info));
#endif
#ifdef CONFIG_BOARD_ATSTK1002_SW5_CUSTOM
	set_hw_addr(at32_add_device_eth(1, &eth_data[1]));
#else
	at32_add_device_lcdc(0, &atstk1000_lcdc_data,
			     fbmem_start, fbmem_size);
#endif
	at32_add_device_usba(0, NULL);
#ifndef CONFIG_BOARD_ATSTK1002_SW3_CUSTOM
	at32_add_device_ssc(0, ATMEL_SSC_TX);
#endif

	setup_j2_leds();

	return 0;
}
postcore_initcall(atstk1002_init);
