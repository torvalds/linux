// SPDX-License-Identifier: GPL-2.0+
/*
 * Airoha UART driver
 *
 * Copyright (c) 2025 Genexis Sweden AB
 * Author: Benjamin Larsson <benjamin.larsson@genexis.eu>
 *	   Christian Marangi <ansuelsmth@gmail.com>
 */

#include <linux/array_size.h>
#include <linux/bits.h>
#include <linux/bitfield.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/math64.h>
#include <linux/module.h>
#include <linux/property.h>
#include <linux/platform_device.h>
#include <linux/serial_8250.h>
#include <linux/serial_core.h>
#include <linux/serial_reg.h>
#include <linux/types.h>
#include <linux/units.h>

#include "8250.h"

#define UART_AIROHA_XINCLKDR	10
#define UART_AIROHA_XYD		11
#define   UART_AIROHA_XYD_X	GENMASK(31, 16)
#define   UART_AIROHA_XYD_Y	GENMASK(15, 0)

#define UART_BRD_20M		0x0001

#define XINDIV_CLOCK		(20 * HZ_PER_MHZ)
#define XYD_Y			65000

static const unsigned int airoha_clk_divs[] = { 2, 4, 10 };

static unsigned int airoha_get_divisor(struct uart_port *port,
				       unsigned int baud,
				       unsigned int *frac)
{
	/* Hardware always uses BRDIV = 1. */
	*frac = 0;

	return UART_BRD_20M;
}

/*
 * Airoha UART baud rate calculation logic
 *
 * crystal_clock = 20 MHz (fixed frequency)
 * xindiv_clock = crystal_clock / clock_div
 * (x/y) = XYD, 32 bit register with 16 bits of x and then 16 bits of y
 * clock_div = XINCLK_DIVCNT (default set to 10 (0x4)),
 *           - 3 bit register [ 1, 2, 4, 8, 10, 12, 16, 20 ]
 *
 * baud_rate = ((xindiv_clock) * (x/y)) / ([BRDH,BRDL] * 16)
 *
 * Selecting divider needs to fulfill
 * 1.8432 MHz <= xindiv_clk <= APB clock / 2
 * The clocks are unknown but a divider of value 1 did not result in a valid
 * waveform.
 *
 * XYD_y seems to need to be larger then XYD_x for proper waveform generation.
 * Setting [BRDH,BRDL] to [0,1] and XYD_y to 65000 gives even values
 * for usual baud rates.
 */
static void airoha_set_divisor(struct uart_port *port, unsigned int baud,
			       unsigned int quot, unsigned int quot_frac)
{
	struct uart_8250_port *up = up_to_u8250p(port);
	u32 xindiv_clk;
	u64 xyd_x, nom;
	int div_bit;

	/* Set baud rate calculation defaults (BRDIV [BRDH,BRDL] to 1) */
	serial8250_do_set_divisor(port, baud, UART_BRD_20M);

	/*
	 * Calculate XYD_x and XINCLKDR register by searching
	 * through a table of crystal_clock divisors.
	 */
	nom = (u64)baud * XYD_Y;
	for (div_bit = ARRAY_SIZE(airoha_clk_divs) - 1; div_bit >= 0; div_bit--) {
		unsigned int div = airoha_clk_divs[div_bit];

		xindiv_clk = XINDIV_CLOCK / div;
		xyd_x = div_u64(nom * 16, xindiv_clk);

		/* For the HSUART xyd_x needs to be scaled by a factor of 2 */
		if (port->type == UART_PORT_AIROHA_HS)
			xyd_x /= 2;

		if (xyd_x < XYD_Y)
			break;
	}

	/* Couldn't find a valid xyd_x */
	if (div_bit < 0) {
		dev_err(port->dev, "failed to find suitable clock divisor for baud %u\n",
			baud);
		return;
	}

	serial_port_out(port, UART_AIROHA_XINCLKDR, BIT(div_bit));
	serial_port_out(port, UART_AIROHA_XYD,
			FIELD_PREP(UART_AIROHA_XYD_X, xyd_x) |
			FIELD_PREP(UART_AIROHA_XYD_Y, XYD_Y));

	/* Restore normal register access. */
	serial_port_out(port, UART_LCR, up->lcr);
}

static int airoha_8250_probe(struct platform_device *pdev)
{
	struct uart_8250_port uart = { };
	struct device *dev = &pdev->dev;
	struct resource *res;
	int ret;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return dev_err_probe(dev, -EINVAL, "invalid address\n");

	uart.port.dev = dev;
	if (device_is_compatible(dev, "airoha,an7581-hsuart"))
		uart.port.type = UART_PORT_AIROHA_HS;
	else
		uart.port.type = UART_PORT_AIROHA;
	uart.port.flags = UPF_BOOT_AUTOCONF | UPF_FIXED_PORT |
			  UPF_FIXED_TYPE | UPF_IOREMAP;
	uart.port.set_divisor = airoha_set_divisor;
	uart.port.get_divisor = airoha_get_divisor;
	uart.port.mapbase = res->start;
	uart.port.mapsize = resource_size(res);

	ret = uart_read_and_validate_port_properties(&uart.port);
	if (ret)
		return ret;

	struct uart_8250_port *uport = serial8250_register_8250_port(&uart);
	if (IS_ERR(uport))
		return PTR_ERR(uport);

	platform_set_drvdata(pdev, uport);

	return 0;
}

static void airoha_8250_remove(struct platform_device *ofdev)
{
	struct uart_8250_port *uport = platform_get_drvdata(ofdev);

	serial8250_unregister_port(uport);
}

static const struct of_device_id airoha_8250_dt_ids[] = {
	{ .compatible = "airoha,en7523-uart" },
	{ .compatible = "airoha,an7581-hsuart" },
	{ }
};
MODULE_DEVICE_TABLE(of, airoha_8250_dt_ids);

static struct platform_driver airoha_8250_driver = {
	.driver = {
		.name = "8250_airoha",
		.of_match_table = airoha_8250_dt_ids,
	},
	.probe = airoha_8250_probe,
	.remove = airoha_8250_remove,
};
module_platform_driver(airoha_8250_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Airoha UART driver");
MODULE_AUTHOR("Benjamin Larsson <benjamin.larsson@genexis.eu>");
MODULE_AUTHOR("Christian Marangi <ansuelsmth@gmail.com>");
