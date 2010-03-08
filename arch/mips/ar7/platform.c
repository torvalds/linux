/*
 * Copyright (C) 2006,2007 Felix Fietkau <nbd@openwrt.org>
 * Copyright (C) 2006,2007 Eugene Konev <ejka@openwrt.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <linux/init.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/platform_device.h>
#include <linux/mtd/physmap.h>
#include <linux/serial.h>
#include <linux/serial_8250.h>
#include <linux/ioport.h>
#include <linux/io.h>
#include <linux/vlynq.h>
#include <linux/leds.h>
#include <linux/string.h>
#include <linux/etherdevice.h>
#include <linux/phy.h>
#include <linux/phy_fixed.h>
#include <linux/gpio.h>
#include <linux/clk.h>

#include <asm/addrspace.h>
#include <asm/mach-ar7/ar7.h>
#include <asm/mach-ar7/gpio.h>
#include <asm/mach-ar7/prom.h>

/*****************************************************************************
 * VLYNQ Bus
 ****************************************************************************/
struct plat_vlynq_data {
	struct plat_vlynq_ops ops;
	int gpio_bit;
	int reset_bit;
};

static int vlynq_on(struct vlynq_device *dev)
{
	int ret;
	struct plat_vlynq_data *pdata = dev->dev.platform_data;

	ret = gpio_request(pdata->gpio_bit, "vlynq");
	if (ret)
		goto out;

	ar7_device_reset(pdata->reset_bit);

	ret = ar7_gpio_disable(pdata->gpio_bit);
	if (ret)
		goto out_enabled;

	ret = ar7_gpio_enable(pdata->gpio_bit);
	if (ret)
		goto out_enabled;

	ret = gpio_direction_output(pdata->gpio_bit, 0);
	if (ret)
		goto out_gpio_enabled;

	msleep(50);

	gpio_set_value(pdata->gpio_bit, 1);

	msleep(50);

	return 0;

out_gpio_enabled:
	ar7_gpio_disable(pdata->gpio_bit);
out_enabled:
	ar7_device_disable(pdata->reset_bit);
	gpio_free(pdata->gpio_bit);
out:
	return ret;
}

static void vlynq_off(struct vlynq_device *dev)
{
	struct plat_vlynq_data *pdata = dev->dev.platform_data;

	ar7_gpio_disable(pdata->gpio_bit);
	gpio_free(pdata->gpio_bit);
	ar7_device_disable(pdata->reset_bit);
}

static struct resource vlynq_low_res[] = {
	{
		.name	= "regs",
		.flags	= IORESOURCE_MEM,
		.start	= AR7_REGS_VLYNQ0,
		.end	= AR7_REGS_VLYNQ0 + 0xff,
	},
	{
		.name	= "irq",
		.flags	= IORESOURCE_IRQ,
		.start	= 29,
		.end	= 29,
	},
	{
		.name	= "mem",
		.flags	= IORESOURCE_MEM,
		.start	= 0x04000000,
		.end	= 0x04ffffff,
	},
	{
		.name	= "devirq",
		.flags	= IORESOURCE_IRQ,
		.start	= 80,
		.end	= 111,
	},
};

static struct resource vlynq_high_res[] = {
	{
		.name	= "regs",
		.flags	= IORESOURCE_MEM,
		.start	= AR7_REGS_VLYNQ1,
		.end	= AR7_REGS_VLYNQ1 + 0xff,
	},
	{
		.name	= "irq",
		.flags	= IORESOURCE_IRQ,
		.start	= 33,
		.end	= 33,
	},
	{
		.name	= "mem",
		.flags	= IORESOURCE_MEM,
		.start	= 0x0c000000,
		.end	= 0x0cffffff,
	},
	{
		.name	= "devirq",
		.flags	= IORESOURCE_IRQ,
		.start	= 112,
		.end	= 143,
	},
};

static struct plat_vlynq_data vlynq_low_data = {
	.ops = {
		.on	= vlynq_on,
		.off	= vlynq_off,
	},
	.reset_bit	= 20,
	.gpio_bit	= 18,
};

static struct plat_vlynq_data vlynq_high_data = {
	.ops = {
		.on	= vlynq_on,
		.off	= vlynq_off,
	},
	.reset_bit	= 26,
	.gpio_bit	= 19,
};

static struct platform_device vlynq_low = {
	.id		= 0,
	.name		= "vlynq",
	.dev = {
		.platform_data	= &vlynq_low_data,
	},
	.resource	= vlynq_low_res,
	.num_resources	= ARRAY_SIZE(vlynq_low_res),
};

static struct platform_device vlynq_high = {
	.id		= 1,
	.name		= "vlynq",
	.dev = {
		.platform_data	= &vlynq_high_data,
	},
	.resource	= vlynq_high_res,
	.num_resources	= ARRAY_SIZE(vlynq_high_res),
};

/*****************************************************************************
 * Flash
 ****************************************************************************/
static struct resource physmap_flash_resource = {
	.name	= "mem",
	.flags	= IORESOURCE_MEM,
	.start	= 0x10000000,
	.end	= 0x107fffff,
};

static struct physmap_flash_data physmap_flash_data = {
	.width	= 2,
};

static struct platform_device physmap_flash = {
	.name		= "physmap-flash",
	.dev = {
		.platform_data	= &physmap_flash_data,
	},
	.resource	= &physmap_flash_resource,
	.num_resources	= 1,
};

/*****************************************************************************
 * Ethernet
 ****************************************************************************/
static struct resource cpmac_low_res[] = {
	{
		.name	= "regs",
		.flags	= IORESOURCE_MEM,
		.start	= AR7_REGS_MAC0,
		.end	= AR7_REGS_MAC0 + 0x7ff,
	},
	{
		.name	= "irq",
		.flags	= IORESOURCE_IRQ,
		.start	= 27,
		.end 	= 27,
	},
};

static struct resource cpmac_high_res[] = {
	{
		.name	= "regs",
		.flags	= IORESOURCE_MEM,
		.start	= AR7_REGS_MAC1,
		.end	= AR7_REGS_MAC1 + 0x7ff,
	},
	{
		.name	= "irq",
		.flags	= IORESOURCE_IRQ,
		.start	= 41,
		.end	= 41,
	},
};

static struct fixed_phy_status fixed_phy_status __initdata = {
	.link		= 1,
	.speed		= 100,
	.duplex		= 1,
};

static struct plat_cpmac_data cpmac_low_data = {
	.reset_bit	= 17,
	.power_bit	= 20,
	.phy_mask	= 0x80000000,
};

static struct plat_cpmac_data cpmac_high_data = {
	.reset_bit	= 21,
	.power_bit	= 22,
	.phy_mask	= 0x7fffffff,
};

static u64 cpmac_dma_mask = DMA_BIT_MASK(32);

static struct platform_device cpmac_low = {
	.id		= 0,
	.name		= "cpmac",
	.dev = {
		.dma_mask		= &cpmac_dma_mask,
		.coherent_dma_mask	= DMA_BIT_MASK(32),
		.platform_data		= &cpmac_low_data,
	},
	.resource	= cpmac_low_res,
	.num_resources	= ARRAY_SIZE(cpmac_low_res),
};

static struct platform_device cpmac_high = {
	.id		= 1,
	.name		= "cpmac",
	.dev = {
		.dma_mask		= &cpmac_dma_mask,
		.coherent_dma_mask	= DMA_BIT_MASK(32),
		.platform_data		= &cpmac_high_data,
	},
	.resource	= cpmac_high_res,
	.num_resources	= ARRAY_SIZE(cpmac_high_res),
};

static inline unsigned char char2hex(char h)
{
	switch (h) {
	case '0': case '1': case '2': case '3': case '4':
	case '5': case '6': case '7': case '8': case '9':
		return h - '0';
	case 'A': case 'B': case 'C': case 'D': case 'E': case 'F':
		return h - 'A' + 10;
	case 'a': case 'b': case 'c': case 'd': case 'e': case 'f':
		return h - 'a' + 10;
	default:
		return 0;
	}
}

static void cpmac_get_mac(int instance, unsigned char *dev_addr)
{
	int i;
	char name[5], default_mac[ETH_ALEN], *mac;

	mac = NULL;
	sprintf(name, "mac%c", 'a' + instance);
	mac = prom_getenv(name);
	if (!mac) {
		sprintf(name, "mac%c", 'a');
		mac = prom_getenv(name);
	}
	if (!mac) {
		random_ether_addr(default_mac);
		mac = default_mac;
	}
	for (i = 0; i < 6; i++)
		dev_addr[i] = (char2hex(mac[i * 3]) << 4) +
			char2hex(mac[i * 3 + 1]);
}

/*****************************************************************************
 * USB
 ****************************************************************************/
static struct resource usb_res[] = {
	{
		.name	= "regs",
		.flags	= IORESOURCE_MEM,
		.start	= AR7_REGS_USB,
		.end	= AR7_REGS_USB + 0xff,
	},
	{
		.name	= "irq",
		.flags	= IORESOURCE_IRQ,
		.start	= 32,
		.end	= 32,
	},
	{
		.name	= "mem",
		.flags	= IORESOURCE_MEM,
		.start	= 0x03400000,
		.end	= 0x03401fff,
	},
};

static struct platform_device ar7_udc = {
	.name		= "ar7_udc",
	.resource	= usb_res,
	.num_resources	= ARRAY_SIZE(usb_res),
};

/*****************************************************************************
 * LEDs
 ****************************************************************************/
static struct gpio_led default_leds[] = {
	{
		.name			= "status",
		.gpio			= 8,
		.active_low		= 1,
	},
};

static struct gpio_led dsl502t_leds[] = {
	{
		.name			= "status",
		.gpio			= 9,
		.active_low		= 1,
	},
	{
		.name			= "ethernet",
		.gpio			= 7,
		.active_low		= 1,
	},
	{
		.name			= "usb",
		.gpio			= 12,
		.active_low		= 1,
	},
};

static struct gpio_led dg834g_leds[] = {
	{
		.name			= "ppp",
		.gpio			= 6,
		.active_low		= 1,
	},
	{
		.name			= "status",
		.gpio			= 7,
		.active_low		= 1,
	},
	{
		.name			= "adsl",
		.gpio			= 8,
		.active_low		= 1,
	},
	{
		.name			= "wifi",
		.gpio			= 12,
		.active_low		= 1,
	},
	{
		.name			= "power",
		.gpio			= 14,
		.active_low		= 1,
		.default_trigger	= "default-on",
	},
};

static struct gpio_led fb_sl_leds[] = {
	{
		.name			= "1",
		.gpio			= 7,
	},
	{
		.name			= "2",
		.gpio			= 13,
		.active_low		= 1,
	},
	{
		.name			= "3",
		.gpio			= 10,
		.active_low		= 1,
	},
	{
		.name			= "4",
		.gpio			= 12,
		.active_low		= 1,
	},
	{
		.name			= "5",
		.gpio			= 9,
		.active_low		= 1,
	},
};

static struct gpio_led fb_fon_leds[] = {
	{
		.name			= "1",
		.gpio			= 8,
	},
	{
		.name			= "2",
		.gpio			= 3,
		.active_low		= 1,
	},
	{
		.name			= "3",
		.gpio			= 5,
	},
	{
		.name			= "4",
		.gpio			= 4,
		.active_low		= 1,
	},
	{
		.name			= "5",
		.gpio			= 11,
		.active_low		= 1,
	},
};

static struct gpio_led_platform_data ar7_led_data;

static struct platform_device ar7_gpio_leds = {
	.name = "leds-gpio",
	.dev = {
		.platform_data = &ar7_led_data,
	}
};

static void __init detect_leds(void)
{
	char *prid, *usb_prod;

	/* Default LEDs	*/
	ar7_led_data.num_leds = ARRAY_SIZE(default_leds);
	ar7_led_data.leds = default_leds;

	/* FIXME: the whole thing is unreliable */
	prid = prom_getenv("ProductID");
	usb_prod = prom_getenv("usb_prod");

	/* If we can't get the product id from PROM, use the default LEDs */
	if (!prid)
		return;

	if (strstr(prid, "Fritz_Box_FON")) {
		ar7_led_data.num_leds = ARRAY_SIZE(fb_fon_leds);
		ar7_led_data.leds = fb_fon_leds;
	} else if (strstr(prid, "Fritz_Box_")) {
		ar7_led_data.num_leds = ARRAY_SIZE(fb_sl_leds);
		ar7_led_data.leds = fb_sl_leds;
	} else if ((!strcmp(prid, "AR7RD") || !strcmp(prid, "AR7DB"))
		&& usb_prod != NULL && strstr(usb_prod, "DSL-502T")) {
		ar7_led_data.num_leds = ARRAY_SIZE(dsl502t_leds);
		ar7_led_data.leds = dsl502t_leds;
	} else if (strstr(prid, "DG834")) {
		ar7_led_data.num_leds = ARRAY_SIZE(dg834g_leds);
		ar7_led_data.leds = dg834g_leds;
	}
}

/*****************************************************************************
 * Watchdog
 ****************************************************************************/
static struct resource ar7_wdt_res = {
	.name		= "regs",
	.flags		= IORESOURCE_MEM,
	.start		= -1,	/* Filled at runtime */
	.end		= -1,	/* Filled at runtime */
};

static struct platform_device ar7_wdt = {
	.name		= "ar7_wdt",
	.resource	= &ar7_wdt_res,
	.num_resources	= 1,
};

/*****************************************************************************
 * Init
 ****************************************************************************/
static int __init ar7_register_uarts(void)
{
#ifdef CONFIG_SERIAL_8250
	static struct uart_port uart_port __initdata;
	struct clk *bus_clk;
	int res;

	memset(&uart_port, 0, sizeof(struct uart_port));

	bus_clk = clk_get(NULL, "bus");
	if (IS_ERR(bus_clk))
		panic("unable to get bus clk\n");

	uart_port.type		= PORT_16550A;
	uart_port.uartclk	= clk_get_rate(bus_clk) / 2;
	uart_port.iotype	= UPIO_MEM32;
	uart_port.regshift	= 2;

	uart_port.line		= 0;
	uart_port.irq		= AR7_IRQ_UART0;
	uart_port.mapbase	= AR7_REGS_UART0;
	uart_port.membase	= ioremap(uart_port.mapbase, 256);

	res = early_serial_setup(&uart_port);
	if (res)
		return res;

	/* Only TNETD73xx have a second serial port */
	if (ar7_has_second_uart()) {
		uart_port.line		= 1;
		uart_port.irq		= AR7_IRQ_UART1;
		uart_port.mapbase	= UR8_REGS_UART1;
		uart_port.membase	= ioremap(uart_port.mapbase, 256);

		res = early_serial_setup(&uart_port);
		if (res)
			return res;
	}
#endif

	return 0;
}

static int __init ar7_register_devices(void)
{
	void __iomem *bootcr;
	u32 val;
	u16 chip_id;
	int res;

	res = ar7_register_uarts();
	if (res)
		pr_err("unable to setup uart(s): %d\n", res);

	res = platform_device_register(&physmap_flash);
	if (res)
		pr_warning("unable to register physmap-flash: %d\n", res);

	ar7_device_disable(vlynq_low_data.reset_bit);
	res = platform_device_register(&vlynq_low);
	if (res)
		pr_warning("unable to register vlynq-low: %d\n", res);

	if (ar7_has_high_vlynq()) {
		ar7_device_disable(vlynq_high_data.reset_bit);
		res = platform_device_register(&vlynq_high);
		if (res)
			pr_warning("unable to register vlynq-high: %d\n", res);
	}

	if (ar7_has_high_cpmac()) {
		if (!res) {
			cpmac_get_mac(1, cpmac_high_data.dev_addr);

			res = platform_device_register(&cpmac_high);
			if (res)
				pr_warning("unable to register cpmac-high: %d\n", res);
		} else
			pr_warning("unable to add cpmac-high phy: %d\n", res);
	} else
		cpmac_low_data.phy_mask = 0xffffffff;

	res = fixed_phy_add(PHY_POLL, cpmac_low.id, &fixed_phy_status);
	if (!res) {
		cpmac_get_mac(0, cpmac_low_data.dev_addr);
		res = platform_device_register(&cpmac_low);
		if (res)
			pr_warning("unable to register cpmac-low: %d\n", res);
	} else
		pr_warning("unable to add cpmac-low phy: %d\n", res);

	detect_leds();
	res = platform_device_register(&ar7_gpio_leds);
	if (res)
		pr_warning("unable to register leds: %d\n", res);

	res = platform_device_register(&ar7_udc);
	if (res)
		pr_warning("unable to register usb slave: %d\n", res);

	/* Register watchdog only if enabled in hardware */
	bootcr = ioremap_nocache(AR7_REGS_DCL, 4);
	val = readl(bootcr);
	iounmap(bootcr);
	if (val & AR7_WDT_HW_ENA) {
		chip_id = ar7_chip_id();
		switch (chip_id) {
		case AR7_CHIP_7100:
		case AR7_CHIP_7200:
			ar7_wdt_res.start = AR7_REGS_WDT;
			break;
		case AR7_CHIP_7300:
			ar7_wdt_res.start = UR8_REGS_WDT;
			break;
		default:
			break;
		}

		ar7_wdt_res.end = ar7_wdt_res.start + 0x20;
		res = platform_device_register(&ar7_wdt);
		if (res)
			pr_warning("unable to register watchdog: %d\n", res);
	}

	return 0;
}
arch_initcall(ar7_register_devices);
