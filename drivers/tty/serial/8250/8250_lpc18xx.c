// SPDX-License-Identifier: GPL-2.0
/*
 * Serial port driver for NXP LPC18xx/43xx UART
 *
 * Copyright (C) 2015 Joachim Eastwood <manabian@gmail.com>
 *
 * Based on 8250_mtk.c:
 * Copyright (c) 2014 MundoReader S.L.
 * Matthias Brugger <matthias.bgg@gmail.com>
 */

#include <linux/clk.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>

#include "8250.h"

/* Additional LPC18xx/43xx 8250 registers and bits */
#define LPC18XX_UART_RS485CTRL		(0x04c / sizeof(u32))
#define  LPC18XX_UART_RS485CTRL_NMMEN	BIT(0)
#define  LPC18XX_UART_RS485CTRL_DCTRL	BIT(4)
#define  LPC18XX_UART_RS485CTRL_OINV	BIT(5)
#define LPC18XX_UART_RS485DLY		(0x054 / sizeof(u32))
#define LPC18XX_UART_RS485DLY_MAX	255

struct lpc18xx_uart_data {
	struct uart_8250_dma dma;
	struct clk *clk_uart;
	struct clk *clk_reg;
	int line;
};

static int lpc18xx_rs485_config(struct uart_port *port, struct ktermios *termios,
				struct serial_rs485 *rs485)
{
	struct uart_8250_port *up = up_to_u8250p(port);
	u32 rs485_ctrl_reg = 0;
	u32 rs485_dly_reg = 0;
	unsigned baud_clk;

	if (rs485->flags & SER_RS485_ENABLED) {
		rs485_ctrl_reg |= LPC18XX_UART_RS485CTRL_NMMEN |
				  LPC18XX_UART_RS485CTRL_DCTRL;

		if (rs485->flags & SER_RS485_RTS_ON_SEND)
			rs485_ctrl_reg |= LPC18XX_UART_RS485CTRL_OINV;
	}

	if (rs485->delay_rts_after_send) {
		baud_clk = port->uartclk / up->dl_read(up);
		rs485_dly_reg = DIV_ROUND_UP(rs485->delay_rts_after_send
						* baud_clk, MSEC_PER_SEC);

		if (rs485_dly_reg > LPC18XX_UART_RS485DLY_MAX)
			rs485_dly_reg = LPC18XX_UART_RS485DLY_MAX;

		/* Calculate the resulting delay in ms */
		rs485->delay_rts_after_send = (rs485_dly_reg * MSEC_PER_SEC)
						/ baud_clk;
	}

	serial_out(up, LPC18XX_UART_RS485CTRL, rs485_ctrl_reg);
	serial_out(up, LPC18XX_UART_RS485DLY, rs485_dly_reg);

	return 0;
}

static void lpc18xx_uart_serial_out(struct uart_port *p, int offset, int value)
{
	/*
	 * For DMA mode one must ensure that the UART_FCR_DMA_SELECT
	 * bit is set when FIFO is enabled. Even if DMA is not used
	 * setting this bit doesn't seem to affect anything.
	 */
	if (offset == UART_FCR && (value & UART_FCR_ENABLE_FIFO))
		value |= UART_FCR_DMA_SELECT;

	offset = offset << p->regshift;
	writel(value, p->membase + offset);
}

static const struct serial_rs485 lpc18xx_rs485_supported = {
	.flags = SER_RS485_ENABLED | SER_RS485_RTS_ON_SEND | SER_RS485_RTS_AFTER_SEND,
	.delay_rts_after_send = 1,
	/* Delay RTS before send is not supported */
};

static int lpc18xx_serial_probe(struct platform_device *pdev)
{
	struct lpc18xx_uart_data *data;
	struct uart_8250_port uart;
	struct resource *res;
	int irq, ret;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "memory resource not found");
		return -EINVAL;
	}

	memset(&uart, 0, sizeof(uart));

	uart.port.membase = devm_ioremap(&pdev->dev, res->start,
					 resource_size(res));
	if (!uart.port.membase)
		return -ENOMEM;

	data = devm_kzalloc(&pdev->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->clk_uart = devm_clk_get(&pdev->dev, "uartclk");
	if (IS_ERR(data->clk_uart)) {
		dev_err(&pdev->dev, "uart clock not found\n");
		return PTR_ERR(data->clk_uart);
	}

	data->clk_reg = devm_clk_get(&pdev->dev, "reg");
	if (IS_ERR(data->clk_reg)) {
		dev_err(&pdev->dev, "reg clock not found\n");
		return PTR_ERR(data->clk_reg);
	}

	ret = clk_prepare_enable(data->clk_reg);
	if (ret) {
		dev_err(&pdev->dev, "unable to enable reg clock\n");
		return ret;
	}

	ret = clk_prepare_enable(data->clk_uart);
	if (ret) {
		dev_err(&pdev->dev, "unable to enable uart clock\n");
		goto dis_clk_reg;
	}

	ret = of_alias_get_id(pdev->dev.of_node, "serial");
	if (ret >= 0)
		uart.port.line = ret;

	data->dma.rx_param = data;
	data->dma.tx_param = data;

	spin_lock_init(&uart.port.lock);
	uart.port.dev = &pdev->dev;
	uart.port.irq = irq;
	uart.port.iotype = UPIO_MEM32;
	uart.port.mapbase = res->start;
	uart.port.regshift = 2;
	uart.port.type = PORT_16550A;
	uart.port.flags = UPF_FIXED_PORT | UPF_FIXED_TYPE | UPF_SKIP_TEST;
	uart.port.uartclk = clk_get_rate(data->clk_uart);
	uart.port.private_data = data;
	uart.port.rs485_config = lpc18xx_rs485_config;
	uart.port.rs485_supported = lpc18xx_rs485_supported;
	uart.port.serial_out = lpc18xx_uart_serial_out;

	uart.dma = &data->dma;
	uart.dma->rxconf.src_maxburst = 1;
	uart.dma->txconf.dst_maxburst = 1;

	ret = serial8250_register_8250_port(&uart);
	if (ret < 0) {
		dev_err(&pdev->dev, "unable to register 8250 port\n");
		goto dis_uart_clk;
	}

	data->line = ret;
	platform_set_drvdata(pdev, data);

	return 0;

dis_uart_clk:
	clk_disable_unprepare(data->clk_uart);
dis_clk_reg:
	clk_disable_unprepare(data->clk_reg);
	return ret;
}

static void lpc18xx_serial_remove(struct platform_device *pdev)
{
	struct lpc18xx_uart_data *data = platform_get_drvdata(pdev);

	serial8250_unregister_port(data->line);
	clk_disable_unprepare(data->clk_uart);
	clk_disable_unprepare(data->clk_reg);
}

static const struct of_device_id lpc18xx_serial_match[] = {
	{ .compatible = "nxp,lpc1850-uart" },
	{ },
};
MODULE_DEVICE_TABLE(of, lpc18xx_serial_match);

static struct platform_driver lpc18xx_serial_driver = {
	.probe  = lpc18xx_serial_probe,
	.remove_new = lpc18xx_serial_remove,
	.driver = {
		.name = "lpc18xx-uart",
		.of_match_table = lpc18xx_serial_match,
	},
};
module_platform_driver(lpc18xx_serial_driver);

MODULE_AUTHOR("Joachim Eastwood <manabian@gmail.com>");
MODULE_DESCRIPTION("Serial port driver NXP LPC18xx/43xx devices");
MODULE_LICENSE("GPL v2");
