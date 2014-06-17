/*
 * Favr-32 board-specific setup code.
 *
 * Copyright (C) 2008 Atmel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/clk.h>
#include <linux/etherdevice.h>
#include <linux/bootmem.h>
#include <linux/fb.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/types.h>
#include <linux/linkage.h>
#include <linux/gpio.h>
#include <linux/leds.h>
#include <linux/atmel-mci.h>
#include <linux/atmel-pwm-bl.h>
#include <linux/spi/spi.h>
#include <linux/spi/ads7846.h>

#include <sound/atmel-abdac.h>

#include <video/atmel_lcdc.h>

#include <asm/setup.h>

#include <mach/at32ap700x.h>
#include <mach/init.h>
#include <mach/board.h>
#include <mach/portmux.h>

/* Oscillator frequencies. These are board-specific */
unsigned long at32_board_osc_rates[3] = {
	[0] = 32768,	/* 32.768 kHz on RTC osc */
	[1] = 20000000,	/* 20 MHz on osc0 */
	[2] = 12000000,	/* 12 MHz on osc1 */
};

/* Initialized by bootloader-specific startup code. */
struct tag *bootloader_tags __initdata;

static struct atmel_abdac_pdata __initdata abdac0_data = {
};

struct eth_addr {
	u8 addr[6];
};
static struct eth_addr __initdata hw_addr[1];
static struct macb_platform_data __initdata eth_data[1] = {
	{
		.phy_mask	= ~(1U << 1),
	},
};

static int ads7843_get_pendown_state(void)
{
	return !gpio_get_value(GPIO_PIN_PB(3));
}

static struct ads7846_platform_data ads7843_data = {
	.model			= 7843,
	.get_pendown_state	= ads7843_get_pendown_state,
	.pressure_max		= 255,
	/*
	 * Values below are for debounce filtering, these can be experimented
	 * with further.
	 */
	.debounce_max		= 20,
	.debounce_rep		= 4,
	.debounce_tol		= 5,

	.keep_vref_on		= true,
	.settle_delay_usecs	= 500,
	.penirq_recheck_delay_usecs = 100,
};

static struct spi_board_info __initdata spi1_board_info[] = {
	{
		/* ADS7843 touch controller */
		.modalias	= "ads7846",
		.max_speed_hz	= 2000000,
		.chip_select	= 0,
		.bus_num	= 1,
		.platform_data	= &ads7843_data,
	},
};

static struct mci_platform_data __initdata mci0_data = {
	.slot[0] = {
		.bus_width	= 4,
		.detect_pin	= -ENODEV,
		.wp_pin		= -ENODEV,
	},
};

static struct fb_videomode __initdata lb104v03_modes[] = {
	{
		.name		= "640x480 @ 50",
		.refresh	= 50,
		.xres		= 640,		.yres		= 480,
		.pixclock	= KHZ2PICOS(25100),

		.left_margin	= 90,		.right_margin	= 70,
		.upper_margin	= 30,		.lower_margin	= 15,
		.hsync_len	= 12,		.vsync_len	= 2,

		.sync		= 0,
		.vmode		= FB_VMODE_NONINTERLACED,
	},
};

static struct fb_monspecs __initdata favr32_default_monspecs = {
	.manufacturer		= "LG",
	.monitor		= "LB104V03",
	.modedb			= lb104v03_modes,
	.modedb_len		= ARRAY_SIZE(lb104v03_modes),
	.hfmin			= 27273,
	.hfmax			= 31111,
	.vfmin			= 45,
	.vfmax			= 60,
	.dclkmax		= 28000000,
};

struct atmel_lcdfb_pdata __initdata favr32_lcdc_data = {
	.default_bpp		= 16,
	.default_dmacon		= ATMEL_LCDC_DMAEN | ATMEL_LCDC_DMA2DEN,
	.default_lcdcon2	= (ATMEL_LCDC_DISTYPE_TFT
				   | ATMEL_LCDC_CLKMOD_ALWAYSACTIVE
				   | ATMEL_LCDC_MEMOR_BIG),
	.default_monspecs	= &favr32_default_monspecs,
	.guard_time		= 2,
};

static struct gpio_led favr32_leds[] = {
	{
		.name		 = "green",
		.gpio		 = GPIO_PIN_PE(19),
		.default_trigger = "heartbeat",
		.active_low	 = 1,
	},
	{
		.name		 = "red",
		.gpio		 = GPIO_PIN_PE(20),
		.active_low	 = 1,
	},
};

static struct gpio_led_platform_data favr32_led_data = {
	.num_leds	= ARRAY_SIZE(favr32_leds),
	.leds		= favr32_leds,
};

static struct platform_device favr32_led_dev = {
	.name		= "leds-gpio",
	.id		= 0,
	.dev		= {
		.platform_data	= &favr32_led_data,
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

void __init favr32_setup_leds(void)
{
	unsigned i;

	for (i = 0; i < ARRAY_SIZE(favr32_leds); i++)
		at32_select_gpio(favr32_leds[i].gpio, AT32_GPIOF_OUTPUT);

	platform_device_register(&favr32_led_dev);
}

static struct atmel_pwm_bl_platform_data atmel_pwm_bl_pdata = {
	.pwm_channel		= 2,
	.pwm_frequency		= 200000,
	.pwm_compare_max	= 345,
	.pwm_duty_max		= 345,
	.pwm_duty_min		= 90,
	.pwm_active_low		= 1,
	.gpio_on		= GPIO_PIN_PA(28),
	.on_active_low		= 0,
};

static struct platform_device atmel_pwm_bl_dev = {
	.name		= "atmel-pwm-bl",
	.id		= 0,
	.dev		= {
		.platform_data = &atmel_pwm_bl_pdata,
	},
};

static void __init favr32_setup_atmel_pwm_bl(void)
{
	platform_device_register(&atmel_pwm_bl_dev);
	at32_select_gpio(atmel_pwm_bl_pdata.gpio_on, 0);
}

void __init setup_board(void)
{
	at32_map_usart(3, 0, 0);	/* USART 3 => /dev/ttyS0 */
	at32_setup_serial_console(0);
}

static int __init set_abdac_rate(struct platform_device *pdev)
{
	int retval;
	struct clk *osc1;
	struct clk *pll1;
	struct clk *abdac;

	if (pdev == NULL)
		return -ENXIO;

	osc1 = clk_get(NULL, "osc1");
	if (IS_ERR(osc1)) {
		retval = PTR_ERR(osc1);
		goto out;
	}

	pll1 = clk_get(NULL, "pll1");
	if (IS_ERR(pll1)) {
		retval = PTR_ERR(pll1);
		goto out_osc1;
	}

	abdac = clk_get(&pdev->dev, "sample_clk");
	if (IS_ERR(abdac)) {
		retval = PTR_ERR(abdac);
		goto out_pll1;
	}

	retval = clk_set_parent(pll1, osc1);
	if (retval != 0)
		goto out_abdac;

	/*
	 * Rate is 32000 to 50000 and ABDAC oversamples 256x. Multiply, in
	 * power of 2, to a value above 80 MHz. Power of 2 so it is possible
	 * for the generic clock to divide it down again and 80 MHz is the
	 * lowest frequency for the PLL.
	 */
	retval = clk_round_rate(pll1,
			CONFIG_BOARD_FAVR32_ABDAC_RATE * 256 * 16);
	if (retval <= 0) {
		retval = -EINVAL;
		goto out_abdac;
	}

	retval = clk_set_rate(pll1, retval);
	if (retval != 0)
		goto out_abdac;

	retval = clk_set_parent(abdac, pll1);
	if (retval != 0)
		goto out_abdac;

out_abdac:
	clk_put(abdac);
out_pll1:
	clk_put(pll1);
out_osc1:
	clk_put(osc1);
out:
	return retval;
}

static int __init favr32_init(void)
{
	/*
	 * Favr-32 uses 32-bit SDRAM interface. Reserve the SDRAM-specific
	 * pins so that nobody messes with them.
	 */
	at32_reserve_pin(GPIO_PIOE_BASE, ATMEL_EBI_PE_DATA_ALL);

	at32_select_gpio(GPIO_PIN_PB(3), 0);	/* IRQ from ADS7843 */

	at32_add_device_usart(0);

	set_hw_addr(at32_add_device_eth(0, &eth_data[0]));

	spi1_board_info[0].irq = gpio_to_irq(GPIO_PIN_PB(3));

	set_abdac_rate(at32_add_device_abdac(0, &abdac0_data));

	at32_add_device_pwm(1 << atmel_pwm_bl_pdata.pwm_channel);
	at32_add_device_spi(1, spi1_board_info, ARRAY_SIZE(spi1_board_info));
	at32_add_device_mci(0, &mci0_data);
	at32_add_device_usba(0, NULL);
	at32_add_device_lcdc(0, &favr32_lcdc_data, fbmem_start, fbmem_size, 0);

	favr32_setup_leds();

	favr32_setup_atmel_pwm_bl();

	return 0;
}
postcore_initcall(favr32_init);
