// SPDX-License-Identifier: GPL-2.0
/*
 * Serial port driver for BCM2835AUX UART
 *
 * Copyright (C) 2016 Martin Sperl <kernel@martin.sperl.org>
 *
 * Based on 8250_lpc18xx.c:
 * Copyright (C) 2015 Joachim Eastwood <manabian@gmail.com>
 *
 * The bcm2835aux is capable of RTS auto flow-control, but this driver doesn't
 * take advantage of it yet.  When adding support, be sure not to enable it
 * simultaneously to rs485.
 */

#include <linux/clk.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>

#include "8250.h"

#define BCM2835_AUX_UART_CNTL		8
#define BCM2835_AUX_UART_CNTL_RXEN	0x01 /* Receiver enable */
#define BCM2835_AUX_UART_CNTL_TXEN	0x02 /* Transmitter enable */
#define BCM2835_AUX_UART_CNTL_AUTORTS	0x04 /* RTS set by RX fill level */
#define BCM2835_AUX_UART_CNTL_AUTOCTS	0x08 /* CTS stops transmitter */
#define BCM2835_AUX_UART_CNTL_RTS3	0x00 /* RTS set until 3 chars left */
#define BCM2835_AUX_UART_CNTL_RTS2	0x10 /* RTS set until 2 chars left */
#define BCM2835_AUX_UART_CNTL_RTS1	0x20 /* RTS set until 1 chars left */
#define BCM2835_AUX_UART_CNTL_RTS4	0x30 /* RTS set until 4 chars left */
#define BCM2835_AUX_UART_CNTL_RTSINV	0x40 /* Invert auto RTS polarity */
#define BCM2835_AUX_UART_CNTL_CTSINV	0x80 /* Invert auto CTS polarity */

/**
 * struct bcm2835aux_data - driver private data of BCM2835 auxiliary UART
 * @clk: clock producer of the port's uartclk
 * @line: index of the port's serial8250_ports[] entry
 * @cntl: cached copy of CNTL register
 */
struct bcm2835aux_data {
	struct clk *clk;
	int line;
	u32 cntl;
};

static void bcm2835aux_rs485_start_tx(struct uart_8250_port *up)
{
	if (!(up->port.rs485.flags & SER_RS485_RX_DURING_TX)) {
		struct bcm2835aux_data *data = dev_get_drvdata(up->port.dev);

		data->cntl &= ~BCM2835_AUX_UART_CNTL_RXEN;
		serial_out(up, BCM2835_AUX_UART_CNTL, data->cntl);
	}

	/*
	 * On the bcm2835aux, the MCR register contains no other
	 * flags besides RTS.  So no need for a read-modify-write.
	 */
	if (up->port.rs485.flags & SER_RS485_RTS_ON_SEND)
		serial8250_out_MCR(up, 0);
	else
		serial8250_out_MCR(up, UART_MCR_RTS);
}

static void bcm2835aux_rs485_stop_tx(struct uart_8250_port *up)
{
	if (up->port.rs485.flags & SER_RS485_RTS_AFTER_SEND)
		serial8250_out_MCR(up, 0);
	else
		serial8250_out_MCR(up, UART_MCR_RTS);

	if (!(up->port.rs485.flags & SER_RS485_RX_DURING_TX)) {
		struct bcm2835aux_data *data = dev_get_drvdata(up->port.dev);

		data->cntl |= BCM2835_AUX_UART_CNTL_RXEN;
		serial_out(up, BCM2835_AUX_UART_CNTL, data->cntl);
	}
}

static int bcm2835aux_serial_probe(struct platform_device *pdev)
{
	struct uart_8250_port up = { };
	struct bcm2835aux_data *data;
	struct resource *res;
	int ret;

	/* allocate the custom structure */
	data = devm_kzalloc(&pdev->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	/* initialize data */
	up.capabilities = UART_CAP_FIFO | UART_CAP_MINI;
	up.port.dev = &pdev->dev;
	up.port.regshift = 2;
	up.port.type = PORT_16550;
	up.port.iotype = UPIO_MEM;
	up.port.fifosize = 8;
	up.port.flags = UPF_SHARE_IRQ | UPF_FIXED_PORT | UPF_FIXED_TYPE |
			UPF_SKIP_TEST | UPF_IOREMAP;
	up.port.rs485_config = serial8250_em485_config;
	up.rs485_start_tx = bcm2835aux_rs485_start_tx;
	up.rs485_stop_tx = bcm2835aux_rs485_stop_tx;

	/* initialize cached copy with power-on reset value */
	data->cntl = BCM2835_AUX_UART_CNTL_RXEN | BCM2835_AUX_UART_CNTL_TXEN;

	platform_set_drvdata(pdev, data);

	/* get the clock - this also enables the HW */
	data->clk = devm_clk_get(&pdev->dev, NULL);
	ret = PTR_ERR_OR_ZERO(data->clk);
	if (ret) {
		if (ret != -EPROBE_DEFER)
			dev_err(&pdev->dev, "could not get clk: %d\n", ret);
		return ret;
	}

	/* get the interrupt */
	ret = platform_get_irq(pdev, 0);
	if (ret < 0)
		return ret;
	up.port.irq = ret;

	/* map the main registers */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "memory resource not found");
		return -EINVAL;
	}
	up.port.mapbase = res->start;
	up.port.mapsize = resource_size(res);

	/* Check for a fixed line number */
	ret = of_alias_get_id(pdev->dev.of_node, "serial");
	if (ret >= 0)
		up.port.line = ret;

	/* enable the clock as a last step */
	ret = clk_prepare_enable(data->clk);
	if (ret) {
		dev_err(&pdev->dev, "unable to enable uart clock - %d\n",
			ret);
		return ret;
	}

	/* the HW-clock divider for bcm2835aux is 8,
	 * but 8250 expects a divider of 16,
	 * so we have to multiply the actual clock by 2
	 * to get identical baudrates.
	 */
	up.port.uartclk = clk_get_rate(data->clk) * 2;

	/* register the port */
	ret = serial8250_register_8250_port(&up);
	if (ret < 0) {
		if (ret != -EPROBE_DEFER)
			dev_err(&pdev->dev,
				"unable to register 8250 port - %d\n", ret);
		goto dis_clk;
	}
	data->line = ret;

	return 0;

dis_clk:
	clk_disable_unprepare(data->clk);
	return ret;
}

static int bcm2835aux_serial_remove(struct platform_device *pdev)
{
	struct bcm2835aux_data *data = platform_get_drvdata(pdev);

	serial8250_unregister_port(data->line);
	clk_disable_unprepare(data->clk);

	return 0;
}

static const struct of_device_id bcm2835aux_serial_match[] = {
	{ .compatible = "brcm,bcm2835-aux-uart" },
	{ },
};
MODULE_DEVICE_TABLE(of, bcm2835aux_serial_match);

static struct platform_driver bcm2835aux_serial_driver = {
	.driver = {
		.name = "bcm2835-aux-uart",
		.of_match_table = bcm2835aux_serial_match,
	},
	.probe  = bcm2835aux_serial_probe,
	.remove = bcm2835aux_serial_remove,
};
module_platform_driver(bcm2835aux_serial_driver);

#ifdef CONFIG_SERIAL_8250_CONSOLE

static int __init early_bcm2835aux_setup(struct earlycon_device *device,
					const char *options)
{
	if (!device->port.membase)
		return -ENODEV;

	device->port.iotype = UPIO_MEM32;
	device->port.regshift = 2;

	return early_serial8250_setup(device, NULL);
}

OF_EARLYCON_DECLARE(bcm2835aux, "brcm,bcm2835-aux-uart",
		    early_bcm2835aux_setup);
#endif

MODULE_DESCRIPTION("BCM2835 auxiliar UART driver");
MODULE_AUTHOR("Martin Sperl <kernel@martin.sperl.org>");
MODULE_LICENSE("GPL v2");
