// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2020 NXP
 */

#include <linux/module.h>
#include <linux/ioport.h>
#include <linux/init.h>
#include <linux/serial_core.h>
#include <linux/serial.h>
#include <linux/delay.h>
#include <linux/of.h>
#include <linux/io.h>

#define URTX0 0x40 /* Transmitter Register */
#define UTS_TXFULL (1<<4) /* TxFIFO full */
#define IMX21_UTS 0xb4 /* UART Test Register on all other i.mx*/

static void imx_uart_console_early_putchar(struct uart_port *port, unsigned char ch)
{
	while (readl_relaxed(port->membase + IMX21_UTS) & UTS_TXFULL)
		cpu_relax();

	writel_relaxed(ch, port->membase + URTX0);
}

static void imx_uart_console_early_write(struct console *con, const char *s,
					 unsigned count)
{
	struct earlycon_device *dev = con->data;

	uart_console_write(&dev->port, s, count, imx_uart_console_early_putchar);
}

static int __init
imx_console_early_setup(struct earlycon_device *dev, const char *opt)
{
	if (!dev->port.membase)
		return -ENODEV;

	dev->con->write = imx_uart_console_early_write;

	return 0;
}
OF_EARLYCON_DECLARE(ec_imx6q, "fsl,imx6q-uart", imx_console_early_setup);
OF_EARLYCON_DECLARE(ec_imx21, "fsl,imx21-uart", imx_console_early_setup);

MODULE_AUTHOR("NXP");
MODULE_DESCRIPTION("IMX earlycon driver");
MODULE_LICENSE("GPL");
