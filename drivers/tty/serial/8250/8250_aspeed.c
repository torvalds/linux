// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) ASPEED Technology Inc.
 */
#include <linux/device.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/serial_8250.h>
#include <linux/serial_reg.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/reset.h>
#include <linux/dma-mapping.h>
#include <linux/circ_buf.h>
#include <linux/tty_flip.h>
#include <linux/pm_runtime.h>

#include "8250.h"

#define DEVICE_NAME "aspeed-uart"

struct ast8250_data {
	int line;
	int irq;
	u8 __iomem *regs;
	struct reset_control *rst;
	struct clk *clk;
#ifdef CONFIG_SERIAL_8250_DMA
	struct uart_8250_dma dma;
#endif
};

#ifdef CONFIG_SERIAL_8250_DMA
static int ast8250_rx_dma(struct uart_8250_port *p);

static void ast8250_rx_dma_complete(void *param)
{
	struct uart_8250_port *p = param;
	struct uart_8250_dma *dma = p->dma;
	struct tty_port *tty_port = &p->port.state->port;
	struct dma_tx_state	state;
	int	count;

	dmaengine_tx_status(dma->rxchan, dma->rx_cookie, &state);

	count = dma->rx_size - state.residue;

	tty_insert_flip_string(tty_port, dma->rx_buf, count);
	p->port.icount.rx += count;

	tty_flip_buffer_push(tty_port);

	ast8250_rx_dma(p);
}

static int ast8250_rx_dma(struct uart_8250_port *p)
{
	struct uart_8250_dma *dma = p->dma;
	struct dma_async_tx_descriptor *tx;

	tx = dmaengine_prep_slave_single(dma->rxchan, dma->rx_addr,
					 dma->rx_size, DMA_DEV_TO_MEM,
					 DMA_PREP_INTERRUPT | DMA_CTRL_ACK);
	if (!tx)
		return -EBUSY;

	tx->callback = ast8250_rx_dma_complete;
	tx->callback_param = p;

	dma->rx_cookie = dmaengine_submit(tx);

	dma_async_issue_pending(dma->rxchan);

	return 0;
}
#endif

static int ast8250_handle_irq(struct uart_port *port)
{
	return serial8250_handle_irq(port, serial_port_in(port, UART_IIR));
}

static int ast8250_startup(struct uart_port *port)
{
#ifdef CONFIG_SERIAL_8250_DMA
	int rc;
	struct uart_8250_port *up = up_to_u8250p(port);

	rc = serial8250_do_startup(port);
	if (rc)
		return rc;

	/*
	 * The default RX DMA is launched upon rising DR bit.
	 *
	 * However, this can result in byte lost if UART FIFO has
	 * been overruned before the DMA engine gets prepared and
	 * read the data out. This is especially common when UART
	 * DMA is used for file transfer. Thus we initiate RX DMA
	 * as early as possible.
	 */
	if (up->dma)
		return ast8250_rx_dma(up);

	return 0;
#else
	return serial8250_do_startup(port);
#endif
}

static void ast8250_shutdown(struct uart_port *port)
{
	return serial8250_do_shutdown(port);
}

static int ast8250_probe(struct platform_device *pdev)
{
	int rc;
	struct uart_8250_port uart = {};
	struct uart_port *port = &uart.port;
	struct device *dev = &pdev->dev;
	struct ast8250_data *data;
	struct resource *res;

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->irq = platform_get_irq(pdev, 0);
	if (data->irq < 0)
		return data->irq;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (res == NULL) {
		dev_err(dev, "failed to get register base\n");
		return -ENODEV;
	}

	data->regs = devm_ioremap(dev, res->start, resource_size(res));
	if (IS_ERR(data->regs)) {
		dev_err(dev, "failed to map registers\n");
		return PTR_ERR(data->regs);
	}

	data->clk = devm_clk_get(dev, NULL);
	if (IS_ERR(data->clk)) {
		dev_err(dev, "failed to get clocks\n");
		return -ENODEV;
	}

	rc = clk_prepare_enable(data->clk);
	if (rc) {
		dev_err(dev, "failed to enable clock\n");
		return rc;
	}

	data->rst = devm_reset_control_get_optional_exclusive(dev, NULL);
	if (!IS_ERR(data->rst))
		reset_control_deassert(data->rst);

	spin_lock_init(&port->lock);
	port->dev = dev;
	port->type = PORT_16550A;
	port->irq = data->irq;
	port->line = of_alias_get_id(dev->of_node, "serial");
	port->handle_irq = ast8250_handle_irq;
	port->mapbase = res->start;
	port->mapsize = resource_size(res);
	port->membase = data->regs;
	port->uartclk = clk_get_rate(data->clk);
	port->regshift = 2;
	port->iotype = UPIO_MEM32;
	port->flags = UPF_FIXED_TYPE | UPF_FIXED_PORT | UPF_SHARE_IRQ;
	port->startup = ast8250_startup;
	port->shutdown = ast8250_shutdown;
	port->private_data = data;
#ifdef CONFIG_SERIAL_8250_DMA
	data->dma.rxconf.src_maxburst = UART_XMIT_SIZE;
	data->dma.txconf.dst_maxburst = UART_XMIT_SIZE;
	uart.dma = &data->dma;
#endif

	data->line = serial8250_register_8250_port(&uart);
	if (data->line < 0) {
		dev_err(dev, "failed to register 8250 port\n");
		return data->line;
	}

	platform_set_drvdata(pdev, data);

	return 0;
}

static int ast8250_remove(struct platform_device *pdev)
{
	struct ast8250_data *data = platform_get_drvdata(pdev);

	serial8250_unregister_port(data->line);

	return 0;
}

static const struct of_device_id ast8250_of_match[] = {
	{ .compatible = "aspeed,ast2600-uart" },
	{ },
};

static struct platform_driver ast8250_platform_driver = {
	.driver = {
		.name = DEVICE_NAME,
		.of_match_table = ast8250_of_match,
	},
	.probe = ast8250_probe,
	.remove = ast8250_remove,
};

module_platform_driver(ast8250_platform_driver);

MODULE_AUTHOR("Chia-Wei Wang <chiawei_wang@aspeedtech.com>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Aspeed 8250 UART Driver with DMA support");
