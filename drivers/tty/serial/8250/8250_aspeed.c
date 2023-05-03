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

/* offsets for the aspeed virtual uart registers */
#define VUART_GCRA	0x20
#define   VUART_GCRA_VUART_EN			BIT(0)
#define   VUART_GCRA_SIRQ_POLARITY		BIT(1)
#define   VUART_GCRA_DISABLE_HOST_TX_DISCARD	BIT(5)
#define VUART_GCRB	0x24
#define   VUART_GCRB_HOST_SIRQ_MASK		GENMASK(7, 4)
#define   VUART_GCRB_HOST_SIRQ_SHIFT		4
#define VUART_ADDRL	0x28
#define VUART_ADDRH	0x2c

#define DMA_TX_BUFSZ	PAGE_SIZE
#define DMA_RX_BUFSZ	(64 * 1024)

struct uart_ops ast8250_pops;

struct ast8250_vuart {
	u32 port;
	u32 sirq;
	u32 sirq_pol;
};

struct ast8250_data {
	int line;
	int irq;
	u8 __iomem *regs;
	struct reset_control *rst;
	struct clk *clk;
	bool is_vuart;
	struct ast8250_vuart vuart;
#ifdef CONFIG_SERIAL_8250_DMA
	struct uart_8250_dma dma;
#endif
};

static void ast8250_vuart_init(struct ast8250_data *data)
{
	u8 reg;
	struct ast8250_vuart *vuart = &data->vuart;

	/* IO port address */
	writeb((u8)(vuart->port >> 0), data->regs + VUART_ADDRL);
	writeb((u8)(vuart->port >> 8), data->regs + VUART_ADDRH);

	/* SIRQ number */
	reg = readb(data->regs + VUART_GCRB);
	reg &= ~VUART_GCRB_HOST_SIRQ_MASK;
	reg |= ((vuart->sirq << VUART_GCRB_HOST_SIRQ_SHIFT) & VUART_GCRB_HOST_SIRQ_MASK);
	writeb(reg, data->regs + VUART_GCRB);

	/* SIRQ polarity */
	reg = readb(data->regs + VUART_GCRA);
	if (vuart->sirq_pol)
		reg |= VUART_GCRA_SIRQ_POLARITY;
	else
		reg &= ~VUART_GCRA_SIRQ_POLARITY;
	writeb(reg, data->regs + VUART_GCRA);
}

static void ast8250_vuart_set_host_tx_discard(struct ast8250_data *data, bool discard)
{
	u8 reg;

	reg = readb(data->regs + VUART_GCRA);
	if (discard)
		reg &= ~VUART_GCRA_DISABLE_HOST_TX_DISCARD;
	else
		reg |= VUART_GCRA_DISABLE_HOST_TX_DISCARD;
	writeb(reg, data->regs + VUART_GCRA);
}

static void ast8250_vuart_set_enable(struct ast8250_data *data, bool enable)
{
	u8 reg;

	reg = readb(data->regs + VUART_GCRA);
	if (enable)
		reg |= VUART_GCRA_VUART_EN;
	else
		reg &= ~VUART_GCRA_VUART_EN;
	writeb(reg, data->regs + VUART_GCRA);
}

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
	struct ast8250_data *data = port->private_data;

	if (data->is_vuart)
		ast8250_vuart_set_host_tx_discard(data, false);

	return serial8250_do_startup(port);
#endif
}

static void ast8250_shutdown(struct uart_port *port)
{
	struct ast8250_data *data = port->private_data;

	if (data->is_vuart)
		ast8250_vuart_set_host_tx_discard(data, true);

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

	data->is_vuart = of_property_read_bool(dev->of_node, "virtual");
	if (data->is_vuart) {
		rc = of_property_read_u32(dev->of_node, "port", &data->vuart.port);
		if (rc) {
			dev_err(dev, "failed to get VUART port address\n");
			return -ENODEV;
		}

		rc = of_property_read_u32(dev->of_node, "sirq", &data->vuart.sirq);
		if (rc) {
			dev_err(dev, "failed to get VUART SIRQ number\n");
			return -ENODEV;
		}

		rc = of_property_read_u32(dev->of_node, "sirq-polarity", &data->vuart.sirq_pol);
		if (rc) {
			dev_err(dev, "failed to get VUART SIRQ polarity\n");
			return -ENODEV;
		}

		ast8250_vuart_init(data);
		ast8250_vuart_set_host_tx_discard(data, true);
		ast8250_vuart_set_enable(data, true);
	}

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

	if (data->is_vuart)
		ast8250_vuart_set_enable(data, false);

	serial8250_unregister_port(data->line);

	return 0;
}

static const struct of_device_id ast8250_of_match[] = {
	{ .compatible = "aspeed,ast2500-uart" },
	{ .compatible = "aspeed,ast2600-uart" },
	{ .compatible = "aspeed,ast2700-uart" },
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
