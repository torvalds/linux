// SPDX-License-Identifier: GPL-2.0+
/*
 * Mediatek 8250 driver.
 *
 * Copyright (c) 2014 MundoReader S.L.
 * Author: Matthias Brugger <matthias.bgg@gmail.com>
 */
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/serial_8250.h>
#include <linux/serial_reg.h>
#include <linux/console.h>
#include <linux/dma-mapping.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>

#include "8250.h"

#define MTK_UART_HIGHS		0x09	/* Highspeed register */
#define MTK_UART_SAMPLE_COUNT	0x0a	/* Sample count register */
#define MTK_UART_SAMPLE_POINT	0x0b	/* Sample point register */
#define MTK_UART_RATE_FIX	0x0d	/* UART Rate Fix Register */
#define MTK_UART_ESCAPE_DAT	0x10	/* Escape Character register */
#define MTK_UART_ESCAPE_EN	0x11	/* Escape Enable register */
#define MTK_UART_DMA_EN		0x13	/* DMA Enable register */
#define MTK_UART_RXTRI_AD	0x14	/* RX Trigger address */
#define MTK_UART_FRACDIV_L	0x15	/* Fractional divider LSB address */
#define MTK_UART_FRACDIV_M	0x16	/* Fractional divider MSB address */
#define MTK_UART_IER_XOFFI	0x20	/* Enable XOFF character interrupt */
#define MTK_UART_IER_RTSI	0x40	/* Enable RTS Modem status interrupt */
#define MTK_UART_IER_CTSI	0x80	/* Enable CTS Modem status interrupt */

#define MTK_UART_EFR_EN		0x10	/* Enable enhancement feature */
#define MTK_UART_EFR_RTS	0x40	/* Enable hardware rx flow control */
#define MTK_UART_EFR_CTS	0x80	/* Enable hardware tx flow control */
#define MTK_UART_EFR_NO_SW_FC	0x0	/* no sw flow control */
#define MTK_UART_EFR_XON1_XOFF1	0xa	/* XON1/XOFF1 as sw flow control */
#define MTK_UART_EFR_XON2_XOFF2	0x5	/* XON2/XOFF2 as sw flow control */
#define MTK_UART_EFR_SW_FC_MASK	0xf	/* Enable CTS Modem status interrupt */
#define MTK_UART_EFR_HW_FC	(MTK_UART_EFR_RTS | MTK_UART_EFR_CTS)
#define MTK_UART_DMA_EN_TX	0x2
#define MTK_UART_DMA_EN_RX	0x5

#define MTK_UART_ESCAPE_CHAR	0x77	/* Escape char added under sw fc */
#define MTK_UART_TX_SIZE	UART_XMIT_SIZE
#define MTK_UART_RX_SIZE	0x8000
#define MTK_UART_TX_TRIGGER	1
#define MTK_UART_RX_TRIGGER	MTK_UART_RX_SIZE

#ifdef CONFIG_SERIAL_8250_DMA
enum dma_rx_status {
	DMA_RX_START = 0,
	DMA_RX_RUNNING = 1,
	DMA_RX_SHUTDOWN = 2,
};
#endif

struct mtk8250_data {
	int			line;
	unsigned int		rx_pos;
	unsigned int		clk_count;
	struct clk		*uart_clk;
	struct clk		*bus_clk;
	struct uart_8250_dma	*dma;
#ifdef CONFIG_SERIAL_8250_DMA
	enum dma_rx_status	rx_status;
#endif
};

/* flow control mode */
enum {
	MTK_UART_FC_NONE,
	MTK_UART_FC_SW,
	MTK_UART_FC_HW,
};

#ifdef CONFIG_SERIAL_8250_DMA
static void mtk8250_rx_dma(struct uart_8250_port *up);

static void mtk8250_dma_rx_complete(void *param)
{
	struct uart_8250_port *up = param;
	struct uart_8250_dma *dma = up->dma;
	struct mtk8250_data *data = up->port.private_data;
	struct tty_port *tty_port = &up->port.state->port;
	struct dma_tx_state state;
	unsigned char *ptr;
	int copied;

	dma_sync_single_for_cpu(dma->rxchan->device->dev, dma->rx_addr,
				dma->rx_size, DMA_FROM_DEVICE);

	dmaengine_tx_status(dma->rxchan, dma->rx_cookie, &state);

	if (data->rx_status == DMA_RX_SHUTDOWN)
		return;

	if ((data->rx_pos + state.residue) <= dma->rx_size) {
		ptr = (unsigned char *)(data->rx_pos + dma->rx_buf);
		copied = tty_insert_flip_string(tty_port, ptr, state.residue);
	} else {
		ptr = (unsigned char *)(data->rx_pos + dma->rx_buf);
		copied = tty_insert_flip_string(tty_port, ptr,
						dma->rx_size - data->rx_pos);
		ptr = (unsigned char *)(dma->rx_buf);
		copied += tty_insert_flip_string(tty_port, ptr,
				data->rx_pos + state.residue - dma->rx_size);
	}
	up->port.icount.rx += copied;

	tty_flip_buffer_push(tty_port);

	mtk8250_rx_dma(up);
}

static void mtk8250_rx_dma(struct uart_8250_port *up)
{
	struct uart_8250_dma *dma = up->dma;
	struct mtk8250_data *data = up->port.private_data;
	struct dma_async_tx_descriptor	*desc;
	struct dma_tx_state	 state;

	desc = dmaengine_prep_slave_single(dma->rxchan, dma->rx_addr,
					   dma->rx_size, DMA_DEV_TO_MEM,
					   DMA_PREP_INTERRUPT | DMA_CTRL_ACK);
	if (!desc) {
		pr_err("failed to prepare rx slave single\n");
		return;
	}

	desc->callback = mtk8250_dma_rx_complete;
	desc->callback_param = up;

	dma->rx_cookie = dmaengine_submit(desc);

	dmaengine_tx_status(dma->rxchan, dma->rx_cookie, &state);
	data->rx_pos = state.residue;

	dma_sync_single_for_device(dma->rxchan->device->dev, dma->rx_addr,
				   dma->rx_size, DMA_FROM_DEVICE);

	dma_async_issue_pending(dma->rxchan);
}

static void mtk8250_dma_enable(struct uart_8250_port *up)
{
	struct uart_8250_dma *dma = up->dma;
	struct mtk8250_data *data = up->port.private_data;
	int lcr = serial_in(up, UART_LCR);

	if (data->rx_status != DMA_RX_START)
		return;

	dma->rxconf.direction		= DMA_DEV_TO_MEM;
	dma->rxconf.src_addr_width	= dma->rx_size / 1024;
	dma->rxconf.src_addr		= dma->rx_addr;

	dma->txconf.direction		= DMA_MEM_TO_DEV;
	dma->txconf.dst_addr_width	= MTK_UART_TX_SIZE / 1024;
	dma->txconf.dst_addr		= dma->tx_addr;

	serial_out(up, UART_FCR, UART_FCR_ENABLE_FIFO | UART_FCR_CLEAR_RCVR |
		UART_FCR_CLEAR_XMIT);
	serial_out(up, MTK_UART_DMA_EN,
		   MTK_UART_DMA_EN_RX | MTK_UART_DMA_EN_TX);

	serial_out(up, UART_LCR, UART_LCR_CONF_MODE_B);
	serial_out(up, UART_EFR, UART_EFR_ECB);
	serial_out(up, UART_LCR, lcr);

	if (dmaengine_slave_config(dma->rxchan, &dma->rxconf) != 0)
		pr_err("failed to configure rx dma channel\n");
	if (dmaengine_slave_config(dma->txchan, &dma->txconf) != 0)
		pr_err("failed to configure tx dma channel\n");

	data->rx_status = DMA_RX_RUNNING;
	data->rx_pos = 0;
	mtk8250_rx_dma(up);
}
#endif

static int mtk8250_startup(struct uart_port *port)
{
#ifdef CONFIG_SERIAL_8250_DMA
	struct uart_8250_port *up = up_to_u8250p(port);
	struct mtk8250_data *data = port->private_data;

	/* disable DMA for console */
	if (uart_console(port))
		up->dma = NULL;

	if (up->dma) {
		data->rx_status = DMA_RX_START;
		uart_circ_clear(&port->state->xmit);
	}
#endif
	memset(&port->icount, 0, sizeof(port->icount));

	return serial8250_do_startup(port);
}

static void mtk8250_shutdown(struct uart_port *port)
{
#ifdef CONFIG_SERIAL_8250_DMA
	struct uart_8250_port *up = up_to_u8250p(port);
	struct mtk8250_data *data = port->private_data;

	if (up->dma)
		data->rx_status = DMA_RX_SHUTDOWN;
#endif

	return serial8250_do_shutdown(port);
}

static void mtk8250_disable_intrs(struct uart_8250_port *up, int mask)
{
	serial_out(up, UART_IER, serial_in(up, UART_IER) & (~mask));
}

static void mtk8250_enable_intrs(struct uart_8250_port *up, int mask)
{
	serial_out(up, UART_IER, serial_in(up, UART_IER) | mask);
}

static void mtk8250_set_flow_ctrl(struct uart_8250_port *up, int mode)
{
	struct uart_port *port = &up->port;
	int lcr = serial_in(up, UART_LCR);

	serial_out(up, UART_LCR, UART_LCR_CONF_MODE_B);
	serial_out(up, UART_EFR, UART_EFR_ECB);
	serial_out(up, UART_LCR, lcr);
	lcr = serial_in(up, UART_LCR);

	switch (mode) {
	case MTK_UART_FC_NONE:
		serial_out(up, MTK_UART_ESCAPE_DAT, MTK_UART_ESCAPE_CHAR);
		serial_out(up, MTK_UART_ESCAPE_EN, 0x00);
		serial_out(up, UART_LCR, UART_LCR_CONF_MODE_B);
		serial_out(up, UART_EFR, serial_in(up, UART_EFR) &
			(~(MTK_UART_EFR_HW_FC | MTK_UART_EFR_SW_FC_MASK)));
		serial_out(up, UART_LCR, lcr);
		mtk8250_disable_intrs(up, MTK_UART_IER_XOFFI |
			MTK_UART_IER_RTSI | MTK_UART_IER_CTSI);
		break;

	case MTK_UART_FC_HW:
		serial_out(up, MTK_UART_ESCAPE_DAT, MTK_UART_ESCAPE_CHAR);
		serial_out(up, MTK_UART_ESCAPE_EN, 0x00);
		serial_out(up, UART_MCR, UART_MCR_RTS);
		serial_out(up, UART_LCR, UART_LCR_CONF_MODE_B);

		/*enable hw flow control*/
		serial_out(up, UART_EFR, MTK_UART_EFR_HW_FC |
			(serial_in(up, UART_EFR) &
			(~(MTK_UART_EFR_HW_FC | MTK_UART_EFR_SW_FC_MASK))));

		serial_out(up, UART_LCR, lcr);
		mtk8250_disable_intrs(up, MTK_UART_IER_XOFFI);
		mtk8250_enable_intrs(up, MTK_UART_IER_CTSI | MTK_UART_IER_RTSI);
		break;

	case MTK_UART_FC_SW:	/*MTK software flow control */
		serial_out(up, MTK_UART_ESCAPE_DAT, MTK_UART_ESCAPE_CHAR);
		serial_out(up, MTK_UART_ESCAPE_EN, 0x01);
		serial_out(up, UART_LCR, UART_LCR_CONF_MODE_B);

		/*enable sw flow control */
		serial_out(up, UART_EFR, MTK_UART_EFR_XON1_XOFF1 |
			(serial_in(up, UART_EFR) &
			(~(MTK_UART_EFR_HW_FC | MTK_UART_EFR_SW_FC_MASK))));

		serial_out(up, UART_XON1, START_CHAR(port->state->port.tty));
		serial_out(up, UART_XOFF1, STOP_CHAR(port->state->port.tty));
		serial_out(up, UART_LCR, lcr);
		mtk8250_disable_intrs(up, MTK_UART_IER_CTSI|MTK_UART_IER_RTSI);
		mtk8250_enable_intrs(up, MTK_UART_IER_XOFFI);
		break;
	default:
		break;
	}
}

static void
mtk8250_set_termios(struct uart_port *port, struct ktermios *termios,
			struct ktermios *old)
{
	unsigned short fraction_L_mapping[] = {
		0, 1, 0x5, 0x15, 0x55, 0x57, 0x57, 0x77, 0x7F, 0xFF, 0xFF
	};
	unsigned short fraction_M_mapping[] = {
		0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 3
	};
	struct uart_8250_port *up = up_to_u8250p(port);
	unsigned int baud, quot, fraction;
	unsigned long flags;
	int mode;

#ifdef CONFIG_SERIAL_8250_DMA
	if (up->dma) {
		if (uart_console(port)) {
			devm_kfree(up->port.dev, up->dma);
			up->dma = NULL;
		} else {
			mtk8250_dma_enable(up);
		}
	}
#endif

	serial8250_do_set_termios(port, termios, old);

	/*
	 * Mediatek UARTs use an extra highspeed register (MTK_UART_HIGHS)
	 *
	 * We need to recalcualte the quot register, as the claculation depends
	 * on the vaule in the highspeed register.
	 *
	 * Some baudrates are not supported by the chip, so we use the next
	 * lower rate supported and update termios c_flag.
	 *
	 * If highspeed register is set to 3, we need to specify sample count
	 * and sample point to increase accuracy. If not, we reset the
	 * registers to their default values.
	 */
	baud = uart_get_baud_rate(port, termios, old,
				  port->uartclk / 16 / UART_DIV_MAX,
				  port->uartclk);

	if (baud < 115200) {
		serial_port_out(port, MTK_UART_HIGHS, 0x0);
		quot = uart_get_divisor(port, baud);
	} else {
		serial_port_out(port, MTK_UART_HIGHS, 0x3);
		quot = DIV_ROUND_UP(port->uartclk, 256 * baud);
	}

	/*
	 * Ok, we're now changing the port state.  Do it with
	 * interrupts disabled.
	 */
	spin_lock_irqsave(&port->lock, flags);

	/* set DLAB we have cval saved in up->lcr from the call to the core */
	serial_port_out(port, UART_LCR, up->lcr | UART_LCR_DLAB);
	serial_dl_write(up, quot);

	/* reset DLAB */
	serial_port_out(port, UART_LCR, up->lcr);

	if (baud >= 115200) {
		unsigned int tmp;

		tmp = (port->uartclk / (baud *  quot)) - 1;
		serial_port_out(port, MTK_UART_SAMPLE_COUNT, tmp);
		serial_port_out(port, MTK_UART_SAMPLE_POINT,
					(tmp >> 1) - 1);

		/*count fraction to set fractoin register */
		fraction = ((port->uartclk  * 100) / baud / quot) % 100;
		fraction = DIV_ROUND_CLOSEST(fraction, 10);
		serial_port_out(port, MTK_UART_FRACDIV_L,
						fraction_L_mapping[fraction]);
		serial_port_out(port, MTK_UART_FRACDIV_M,
						fraction_M_mapping[fraction]);
	} else {
		serial_port_out(port, MTK_UART_SAMPLE_COUNT, 0x00);
		serial_port_out(port, MTK_UART_SAMPLE_POINT, 0xff);
		serial_port_out(port, MTK_UART_FRACDIV_L, 0x00);
		serial_port_out(port, MTK_UART_FRACDIV_M, 0x00);
	}

	if ((termios->c_cflag & CRTSCTS) && (!(termios->c_iflag & CRTSCTS)))
		mode = MTK_UART_FC_HW;
	else if (termios->c_iflag & CRTSCTS)
		mode = MTK_UART_FC_SW;
	else
		mode = MTK_UART_FC_NONE;

	mtk8250_set_flow_ctrl(up, mode);

	if (uart_console(port))
		up->port.cons->cflag = termios->c_cflag;

	spin_unlock_irqrestore(&port->lock, flags);
	/* Don't rewrite B0 */
	if (tty_termios_baud_rate(termios))
		tty_termios_encode_baud_rate(termios, baud, baud);
}

static int __maybe_unused mtk8250_runtime_suspend(struct device *dev)
{
	struct mtk8250_data *data = dev_get_drvdata(dev);

	clk_disable_unprepare(data->uart_clk);
	clk_disable_unprepare(data->bus_clk);

	return 0;
}

static int __maybe_unused mtk8250_runtime_resume(struct device *dev)
{
	struct mtk8250_data *data = dev_get_drvdata(dev);
	int err;

	err = clk_prepare_enable(data->uart_clk);
	if (err) {
		dev_warn(dev, "Can't enable clock\n");
		return err;
	}

	err = clk_prepare_enable(data->bus_clk);
	if (err) {
		dev_warn(dev, "Can't enable bus clock\n");
		return err;
	}

	return 0;
}

static void
mtk8250_do_pm(struct uart_port *port, unsigned int state, unsigned int old)
{
	if (!state)
		pm_runtime_get_sync(port->dev);

	serial8250_do_pm(port, state, old);

	if (state)
		pm_runtime_put_sync_suspend(port->dev);
}

#ifdef CONFIG_SERIAL_8250_DMA
static bool mtk8250_dma_filter(struct dma_chan *chan, void *param)
{
	return false;
}
#endif

static int mtk8250_probe_of(struct platform_device *pdev, struct uart_port *p,
			   struct mtk8250_data *data)
{
#ifdef CONFIG_SERIAL_8250_DMA
	int dmacnt;
#endif

	data->uart_clk = devm_clk_get(&pdev->dev, "baud");
	if (IS_ERR(data->uart_clk)) {
		/*
		 * For compatibility with older device trees try unnamed
		 * clk when no baud clk can be found.
		 */
		data->uart_clk = devm_clk_get(&pdev->dev, NULL);
		if (IS_ERR(data->uart_clk)) {
			dev_warn(&pdev->dev, "Can't get uart clock\n");
			return PTR_ERR(data->uart_clk);
		}

		return 0;
	}

	data->bus_clk = devm_clk_get(&pdev->dev, "bus");
	if (IS_ERR(data->bus_clk))
		return PTR_ERR(data->bus_clk);

	data->dma = NULL;
#ifdef CONFIG_SERIAL_8250_DMA
	dmacnt = of_property_count_strings(pdev->dev.of_node, "dma-names");
	if (dmacnt == 2) {
		data->dma = devm_kzalloc(&pdev->dev, sizeof(*data->dma),
					 GFP_KERNEL);
		if (!data->dma)
			return -ENOMEM;

		data->dma->fn = mtk8250_dma_filter;
		data->dma->rx_size = MTK_UART_RX_SIZE;
		data->dma->rxconf.src_maxburst = MTK_UART_RX_TRIGGER;
		data->dma->txconf.dst_maxburst = MTK_UART_TX_TRIGGER;
	}
#endif

	return 0;
}

static int mtk8250_probe(struct platform_device *pdev)
{
	struct uart_8250_port uart = {};
	struct resource *regs = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	struct resource *irq = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	struct mtk8250_data *data;
	int err;

	if (!regs || !irq) {
		dev_err(&pdev->dev, "no registers/irq defined\n");
		return -EINVAL;
	}

	uart.port.membase = devm_ioremap(&pdev->dev, regs->start,
					 resource_size(regs));
	if (!uart.port.membase)
		return -ENOMEM;

	data = devm_kzalloc(&pdev->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	if (pdev->dev.of_node) {
		err = mtk8250_probe_of(pdev, &uart.port, data);
		if (err)
			return err;
	} else
		return -ENODEV;

	spin_lock_init(&uart.port.lock);
	uart.port.mapbase = regs->start;
	uart.port.irq = irq->start;
	uart.port.pm = mtk8250_do_pm;
	uart.port.type = PORT_16550;
	uart.port.flags = UPF_BOOT_AUTOCONF | UPF_FIXED_PORT;
	uart.port.dev = &pdev->dev;
	uart.port.iotype = UPIO_MEM32;
	uart.port.regshift = 2;
	uart.port.private_data = data;
	uart.port.shutdown = mtk8250_shutdown;
	uart.port.startup = mtk8250_startup;
	uart.port.set_termios = mtk8250_set_termios;
	uart.port.uartclk = clk_get_rate(data->uart_clk);
#ifdef CONFIG_SERIAL_8250_DMA
	if (data->dma)
		uart.dma = data->dma;
#endif

	/* Disable Rate Fix function */
	writel(0x0, uart.port.membase +
			(MTK_UART_RATE_FIX << uart.port.regshift));

	platform_set_drvdata(pdev, data);

	err = mtk8250_runtime_resume(&pdev->dev);
	if (err)
		return err;

	data->line = serial8250_register_8250_port(&uart);
	if (data->line < 0)
		return data->line;

	pm_runtime_set_active(&pdev->dev);
	pm_runtime_enable(&pdev->dev);

	return 0;
}

static int mtk8250_remove(struct platform_device *pdev)
{
	struct mtk8250_data *data = platform_get_drvdata(pdev);

	pm_runtime_get_sync(&pdev->dev);

	serial8250_unregister_port(data->line);
	mtk8250_runtime_suspend(&pdev->dev);

	pm_runtime_disable(&pdev->dev);
	pm_runtime_put_noidle(&pdev->dev);

	return 0;
}

static int __maybe_unused mtk8250_suspend(struct device *dev)
{
	struct mtk8250_data *data = dev_get_drvdata(dev);

	serial8250_suspend_port(data->line);

	return 0;
}

static int __maybe_unused mtk8250_resume(struct device *dev)
{
	struct mtk8250_data *data = dev_get_drvdata(dev);

	serial8250_resume_port(data->line);

	return 0;
}

static const struct dev_pm_ops mtk8250_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(mtk8250_suspend, mtk8250_resume)
	SET_RUNTIME_PM_OPS(mtk8250_runtime_suspend, mtk8250_runtime_resume,
				NULL)
};

static const struct of_device_id mtk8250_of_match[] = {
	{ .compatible = "mediatek,mt6577-uart" },
	{ /* Sentinel */ }
};
MODULE_DEVICE_TABLE(of, mtk8250_of_match);

static struct platform_driver mtk8250_platform_driver = {
	.driver = {
		.name		= "mt6577-uart",
		.pm		= &mtk8250_pm_ops,
		.of_match_table	= mtk8250_of_match,
	},
	.probe			= mtk8250_probe,
	.remove			= mtk8250_remove,
};
module_platform_driver(mtk8250_platform_driver);

#ifdef CONFIG_SERIAL_8250_CONSOLE
static int __init early_mtk8250_setup(struct earlycon_device *device,
					const char *options)
{
	if (!device->port.membase)
		return -ENODEV;

	device->port.iotype = UPIO_MEM32;

	return early_serial8250_setup(device, NULL);
}

OF_EARLYCON_DECLARE(mtk8250, "mediatek,mt6577-uart", early_mtk8250_setup);
#endif

MODULE_AUTHOR("Matthias Brugger");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Mediatek 8250 serial port driver");
