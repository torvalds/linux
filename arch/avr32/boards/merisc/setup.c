/*
 * Board-specific setup code for the Merisc
 *
 * Copyright (C) 2008 Martinsson Elektronik AB
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/clk.h>
#include <linux/etherdevice.h>
#include <linux/i2c.h>
#include <linux/i2c-gpio.h>
#include <linux/gpio.h>
#include <linux/init.h>
#include <linux/linkage.h>
#include <linux/platform_device.h>
#include <linux/types.h>
#include <linux/leds.h>
#include <linux/spi/spi.h>
#include <linux/spi/ads7846.h>
#include <linux/irq.h>
#include <linux/fb.h>
#include <linux/atmel-mci.h>

#include <asm/io.h>
#include <asm/setup.h>
#include <asm/gpio.h>

#include <mach/at32ap700x.h>
#include <mach/board.h>
#include <mach/init.h>
#include <mach/portmux.h>

#include "merisc.h"

/* Holds the autodetected board model and revision */
static int merisc_board_id;

/* Initialized by bootloader-specific startup code. */
struct tag *bootloader_tags __initdata;

/* Oscillator frequencies. These are board specific */
unsigned long at32_board_osc_rates[3] = {
	[0]	= 32768,	/* 32.768 kHz on RTC osc */
	[1]	= 20000000,	/* 20 MHz on osc0 */
	[2]	= 12000000,	/* 12 MHz on osc1 */
};

struct eth_addr {
	u8 addr[6];
};

static struct eth_addr __initdata hw_addr[2];
static struct macb_platform_data __initdata eth_data[2];

static int ads7846_get_pendown_state_PB26(void)
{
	return !gpio_get_value(GPIO_PIN_PB(26));
}

static int ads7846_get_pendown_state_PB28(void)
{
	return !gpio_get_value(GPIO_PIN_PB(28));
}

static struct ads7846_platform_data __initdata ads7846_data = {
	.model				= 7846,
	.vref_delay_usecs		= 100,
	.vref_mv			= 0,
	.keep_vref_on			= 0,
	.settle_delay_usecs		= 150,
	.penirq_recheck_delay_usecs	= 1,
	.x_plate_ohms			= 800,
	.debounce_rep			= 4,
	.debounce_max			= 10,
	.debounce_tol			= 50,
	.get_pendown_state		= ads7846_get_pendown_state_PB26,
	.filter_init			= NULL,
	.filter				= NULL,
	.filter_cleanup			= NULL,
};

static struct spi_board_info __initdata spi0_board_info[] = {
	{
		.modalias	= "ads7846",
		.max_speed_hz	= 3250000,
		.chip_select	= 0,
		.bus_num	= 0,
		.platform_data	= &ads7846_data,
		.mode		= SPI_MODE_0,
	},
};

static struct mci_platform_data __initdata mci0_data = {
	.slot[0] = {
		.bus_width		= 4,
		.detect_pin		= GPIO_PIN_PE(19),
		.wp_pin			= GPIO_PIN_PE(20),
		.detect_is_active_high	= true,
	},
};

static int __init parse_tag_ethernet(struct tag *tag)
{
	int i;

	i = tag->u.ethernet.mac_index;
	if (i < ARRAY_SIZE(hw_addr)) {
		memcpy(hw_addr[i].addr, tag->u.ethernet.hw_address,
		       sizeof(hw_addr[i].addr));
	}

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

static struct i2c_gpio_platform_data i2c_gpio_data = {
	.sda_pin		= GPIO_PIN_PA(6),
	.scl_pin		= GPIO_PIN_PA(7),
	.sda_is_open_drain	= 1,
	.scl_is_open_drain	= 1,
	.udelay			= 2,
};

static struct platform_device i2c_gpio_device = {
	.name	= "i2c-gpio",
	.id	= 0,
	.dev	= {
		.platform_data	= &i2c_gpio_data,
	},
};

static struct i2c_board_info __initdata i2c_info[] = {
	{
		I2C_BOARD_INFO("pcf8563", 0x51)
	},
};

#ifdef CONFIG_LEDS_ATMEL_PWM
static struct gpio_led stk_pwm_led[] = {
	{
		.name	= "backlight",
		.gpio	= 0,		/* PWM channel 0 (LCD backlight) */
	},
};

static struct gpio_led_platform_data stk_pwm_led_data = {
	.num_leds	= ARRAY_SIZE(stk_pwm_led),
	.leds		= stk_pwm_led,
};

static struct platform_device stk_pwm_led_dev = {
	.name	= "leds-atmel-pwm",
	.id	= -1,
	.dev	= {
		.platform_data	= &stk_pwm_led_data,
	},
};
#endif

const char *merisc_model(void)
{
	switch (merisc_board_id) {
	case 0:
	case 1:
		return "500-01";
	case 2:
		return "BT";
	default:
		return "Unknown";
	}
}

const char *merisc_revision(void)
{
	switch (merisc_board_id) {
	case 0:
		return "B";
	case 1:
		return "D";
	case 2:
		return "A";
	default:
		return "Unknown";
	}
}

static void detect_merisc_board_id(void)
{
	/* Board ID pins MUST be set as input or the board may be damaged */
	at32_select_gpio(GPIO_PIN_PA(24), AT32_GPIOF_PULLUP);
	at32_select_gpio(GPIO_PIN_PA(25), AT32_GPIOF_PULLUP);
	at32_select_gpio(GPIO_PIN_PA(26), AT32_GPIOF_PULLUP);
	at32_select_gpio(GPIO_PIN_PA(27), AT32_GPIOF_PULLUP);

	merisc_board_id = !gpio_get_value(GPIO_PIN_PA(24)) +
		!gpio_get_value(GPIO_PIN_PA(25)) * 2 +
		!gpio_get_value(GPIO_PIN_PA(26)) * 4 +
		!gpio_get_value(GPIO_PIN_PA(27)) * 8;
}

void __init setup_board(void)
{
	at32_map_usart(0, 0, 0);
	at32_map_usart(1, 1, 0);
	at32_map_usart(3, 3, 0);
	at32_setup_serial_console(1);
}

static int __init merisc_init(void)
{
	detect_merisc_board_id();

	printk(KERN_NOTICE "BOARD: Merisc %s revision %s\n", merisc_model(),
	       merisc_revision());

	/* Reserve pins for SDRAM */
	at32_reserve_pin(GPIO_PIOE_BASE, ATMEL_EBI_PE_DATA_ALL | (1 << 26));

	if (merisc_board_id >= 1)
		at32_map_usart(2, 2, 0);

	at32_add_device_usart(0);
	at32_add_device_usart(1);
	if (merisc_board_id >= 1)
		at32_add_device_usart(2);
	at32_add_device_usart(3);
	set_hw_addr(at32_add_device_eth(0, &eth_data[0]));

	/* ADS7846 PENIRQ */
	if (merisc_board_id == 0) {
		ads7846_data.get_pendown_state = ads7846_get_pendown_state_PB26;
		at32_select_periph(GPIO_PIOB_BASE, 1 << 26,
				   GPIO_PERIPH_A, AT32_GPIOF_PULLUP);
		spi0_board_info[0].irq = AT32_EXTINT(1);
	} else {
		ads7846_data.get_pendown_state = ads7846_get_pendown_state_PB28;
		at32_select_periph(GPIO_PIOB_BASE, 1 << 28, GPIO_PERIPH_A,
				   AT32_GPIOF_PULLUP);
		spi0_board_info[0].irq = AT32_EXTINT(3);
	}

	/* ADS7846 busy pin */
	at32_select_gpio(GPIO_PIN_PA(4), AT32_GPIOF_PULLUP);

	at32_add_device_spi(0, spi0_board_info, ARRAY_SIZE(spi0_board_info));

	at32_add_device_mci(0, &mci0_data);

#ifdef CONFIG_LEDS_ATMEL_PWM
	at32_add_device_pwm((1 << 0) | (1 << 2));
	platform_device_register(&stk_pwm_led_dev);
#else
	at32_add_device_pwm((1 << 2));
#endif

	at32_select_gpio(i2c_gpio_data.sda_pin,
		AT32_GPIOF_MULTIDRV | AT32_GPIOF_OUTPUT | AT32_GPIOF_HIGH);
	at32_select_gpio(i2c_gpio_data.scl_pin,
		AT32_GPIOF_MULTIDRV | AT32_GPIOF_OUTPUT | AT32_GPIOF_HIGH);
	platform_device_register(&i2c_gpio_device);

	i2c_register_board_info(0, i2c_info, ARRAY_SIZE(i2c_info));

	return 0;
}
postcore_initcall(merisc_init);
