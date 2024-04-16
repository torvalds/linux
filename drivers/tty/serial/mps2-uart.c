// SPDX-License-Identifier: GPL-2.0
/*
 * MPS2 UART driver
 *
 * Copyright (C) 2015 ARM Limited
 *
 * Author: Vladimir Murzin <vladimir.murzin@arm.com>
 *
 * TODO: support for SysRq
 */

#define pr_fmt(fmt)	KBUILD_MODNAME ": " fmt

#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/console.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/of_device.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/serial_core.h>
#include <linux/tty_flip.h>
#include <linux/types.h>
#include <linux/idr.h>

#define SERIAL_NAME	"ttyMPS"
#define DRIVER_NAME	"mps2-uart"
#define MAKE_NAME(x)	(DRIVER_NAME # x)

#define UARTn_DATA				0x00

#define UARTn_STATE				0x04
#define UARTn_STATE_TX_FULL			BIT(0)
#define UARTn_STATE_RX_FULL			BIT(1)
#define UARTn_STATE_TX_OVERRUN			BIT(2)
#define UARTn_STATE_RX_OVERRUN			BIT(3)

#define UARTn_CTRL				0x08
#define UARTn_CTRL_TX_ENABLE			BIT(0)
#define UARTn_CTRL_RX_ENABLE			BIT(1)
#define UARTn_CTRL_TX_INT_ENABLE		BIT(2)
#define UARTn_CTRL_RX_INT_ENABLE		BIT(3)
#define UARTn_CTRL_TX_OVERRUN_INT_ENABLE	BIT(4)
#define UARTn_CTRL_RX_OVERRUN_INT_ENABLE	BIT(5)

#define UARTn_INT				0x0c
#define UARTn_INT_TX				BIT(0)
#define UARTn_INT_RX				BIT(1)
#define UARTn_INT_TX_OVERRUN			BIT(2)
#define UARTn_INT_RX_OVERRUN			BIT(3)

#define UARTn_BAUDDIV				0x10
#define UARTn_BAUDDIV_MASK			GENMASK(20, 0)

/*
 * Helpers to make typical enable/disable operations more readable.
 */
#define UARTn_CTRL_TX_GRP	(UARTn_CTRL_TX_ENABLE		 |\
				 UARTn_CTRL_TX_INT_ENABLE	 |\
				 UARTn_CTRL_TX_OVERRUN_INT_ENABLE)

#define UARTn_CTRL_RX_GRP	(UARTn_CTRL_RX_ENABLE		 |\
				 UARTn_CTRL_RX_INT_ENABLE	 |\
				 UARTn_CTRL_RX_OVERRUN_INT_ENABLE)

#define MPS2_MAX_PORTS		3

#define UART_PORT_COMBINED_IRQ	BIT(0)

struct mps2_uart_port {
	struct uart_port port;
	struct clk *clk;
	unsigned int tx_irq;
	unsigned int rx_irq;
	unsigned int flags;
};

static inline struct mps2_uart_port *to_mps2_port(struct uart_port *port)
{
	return container_of(port, struct mps2_uart_port, port);
}

static void mps2_uart_write8(struct uart_port *port, u8 val, unsigned int off)
{
	struct mps2_uart_port *mps_port = to_mps2_port(port);

	writeb(val, mps_port->port.membase + off);
}

static u8 mps2_uart_read8(struct uart_port *port, unsigned int off)
{
	struct mps2_uart_port *mps_port = to_mps2_port(port);

	return readb(mps_port->port.membase + off);
}

static void mps2_uart_write32(struct uart_port *port, u32 val, unsigned int off)
{
	struct mps2_uart_port *mps_port = to_mps2_port(port);

	writel_relaxed(val, mps_port->port.membase + off);
}

static unsigned int mps2_uart_tx_empty(struct uart_port *port)
{
	u8 status = mps2_uart_read8(port, UARTn_STATE);

	return (status & UARTn_STATE_TX_FULL) ? 0 : TIOCSER_TEMT;
}

static void mps2_uart_set_mctrl(struct uart_port *port, unsigned int mctrl)
{
}

static unsigned int mps2_uart_get_mctrl(struct uart_port *port)
{
	return TIOCM_CAR | TIOCM_CTS | TIOCM_DSR;
}

static void mps2_uart_stop_tx(struct uart_port *port)
{
	u8 control = mps2_uart_read8(port, UARTn_CTRL);

	control &= ~UARTn_CTRL_TX_INT_ENABLE;

	mps2_uart_write8(port, control, UARTn_CTRL);
}

static void mps2_uart_tx_chars(struct uart_port *port)
{
	struct circ_buf *xmit = &port->state->xmit;

	while (!(mps2_uart_read8(port, UARTn_STATE) & UARTn_STATE_TX_FULL)) {
		if (port->x_char) {
			mps2_uart_write8(port, port->x_char, UARTn_DATA);
			port->x_char = 0;
			port->icount.tx++;
			continue;
		}

		if (uart_circ_empty(xmit) || uart_tx_stopped(port))
			break;

		mps2_uart_write8(port, xmit->buf[xmit->tail], UARTn_DATA);
		xmit->tail = (xmit->tail + 1) % UART_XMIT_SIZE;
		port->icount.tx++;
	}

	if (uart_circ_chars_pending(xmit) < WAKEUP_CHARS)
		uart_write_wakeup(port);

	if (uart_circ_empty(xmit))
		mps2_uart_stop_tx(port);
}

static void mps2_uart_start_tx(struct uart_port *port)
{
	u8 control = mps2_uart_read8(port, UARTn_CTRL);

	control |= UARTn_CTRL_TX_INT_ENABLE;

	mps2_uart_write8(port, control, UARTn_CTRL);

	/*
	 * We've just unmasked the TX IRQ and now slow-starting via
	 * polling; if there is enough data to fill up the internal
	 * write buffer in one go, the TX IRQ should assert, at which
	 * point we switch to fully interrupt-driven TX.
	 */

	mps2_uart_tx_chars(port);
}

static void mps2_uart_stop_rx(struct uart_port *port)
{
	u8 control = mps2_uart_read8(port, UARTn_CTRL);

	control &= ~UARTn_CTRL_RX_GRP;

	mps2_uart_write8(port, control, UARTn_CTRL);
}

static void mps2_uart_break_ctl(struct uart_port *port, int ctl)
{
}

static void mps2_uart_rx_chars(struct uart_port *port)
{
	struct tty_port *tport = &port->state->port;

	while (mps2_uart_read8(port, UARTn_STATE) & UARTn_STATE_RX_FULL) {
		u8 rxdata = mps2_uart_read8(port, UARTn_DATA);

		port->icount.rx++;
		tty_insert_flip_char(&port->state->port, rxdata, TTY_NORMAL);
	}

	tty_flip_buffer_push(tport);
}

static irqreturn_t mps2_uart_rxirq(int irq, void *data)
{
	struct uart_port *port = data;
	u8 irqflag = mps2_uart_read8(port, UARTn_INT);

	if (unlikely(!(irqflag & UARTn_INT_RX)))
		return IRQ_NONE;

	spin_lock(&port->lock);

	mps2_uart_write8(port, UARTn_INT_RX, UARTn_INT);
	mps2_uart_rx_chars(port);

	spin_unlock(&port->lock);

	return IRQ_HANDLED;
}

static irqreturn_t mps2_uart_txirq(int irq, void *data)
{
	struct uart_port *port = data;
	u8 irqflag = mps2_uart_read8(port, UARTn_INT);

	if (unlikely(!(irqflag & UARTn_INT_TX)))
		return IRQ_NONE;

	spin_lock(&port->lock);

	mps2_uart_write8(port, UARTn_INT_TX, UARTn_INT);
	mps2_uart_tx_chars(port);

	spin_unlock(&port->lock);

	return IRQ_HANDLED;
}

static irqreturn_t mps2_uart_oerrirq(int irq, void *data)
{
	irqreturn_t handled = IRQ_NONE;
	struct uart_port *port = data;
	u8 irqflag = mps2_uart_read8(port, UARTn_INT);

	spin_lock(&port->lock);

	if (irqflag & UARTn_INT_RX_OVERRUN) {
		struct tty_port *tport = &port->state->port;

		mps2_uart_write8(port, UARTn_INT_RX_OVERRUN, UARTn_INT);
		port->icount.overrun++;
		tty_insert_flip_char(tport, 0, TTY_OVERRUN);
		tty_flip_buffer_push(tport);
		handled = IRQ_HANDLED;
	}

	/*
	 * It's never been seen in practice and it never *should* happen since
	 * we check if there is enough room in TX buffer before sending data.
	 * So we keep this check in case something suspicious has happened.
	 */
	if (irqflag & UARTn_INT_TX_OVERRUN) {
		mps2_uart_write8(port, UARTn_INT_TX_OVERRUN, UARTn_INT);
		handled = IRQ_HANDLED;
	}

	spin_unlock(&port->lock);

	return handled;
}

static irqreturn_t mps2_uart_combinedirq(int irq, void *data)
{
	if (mps2_uart_rxirq(irq, data) == IRQ_HANDLED)
		return IRQ_HANDLED;

	if (mps2_uart_txirq(irq, data) == IRQ_HANDLED)
		return IRQ_HANDLED;

	if (mps2_uart_oerrirq(irq, data) == IRQ_HANDLED)
		return IRQ_HANDLED;

	return IRQ_NONE;
}

static int mps2_uart_startup(struct uart_port *port)
{
	struct mps2_uart_port *mps_port = to_mps2_port(port);
	u8 control = mps2_uart_read8(port, UARTn_CTRL);
	int ret;

	control &= ~(UARTn_CTRL_RX_GRP | UARTn_CTRL_TX_GRP);

	mps2_uart_write8(port, control, UARTn_CTRL);

	if (mps_port->flags & UART_PORT_COMBINED_IRQ) {
		ret = request_irq(port->irq, mps2_uart_combinedirq, 0,
				  MAKE_NAME(-combined), mps_port);

		if (ret) {
			dev_err(port->dev, "failed to register combinedirq (%d)\n", ret);
			return ret;
		}
	} else {
		ret = request_irq(port->irq, mps2_uart_oerrirq, IRQF_SHARED,
				  MAKE_NAME(-overrun), mps_port);

		if (ret) {
			dev_err(port->dev, "failed to register oerrirq (%d)\n", ret);
			return ret;
		}

		ret = request_irq(mps_port->rx_irq, mps2_uart_rxirq, 0,
				  MAKE_NAME(-rx), mps_port);
		if (ret) {
			dev_err(port->dev, "failed to register rxirq (%d)\n", ret);
			goto err_free_oerrirq;
		}

		ret = request_irq(mps_port->tx_irq, mps2_uart_txirq, 0,
				  MAKE_NAME(-tx), mps_port);
		if (ret) {
			dev_err(port->dev, "failed to register txirq (%d)\n", ret);
			goto err_free_rxirq;
		}

	}

	control |= UARTn_CTRL_RX_GRP | UARTn_CTRL_TX_GRP;

	mps2_uart_write8(port, control, UARTn_CTRL);

	return 0;

err_free_rxirq:
	free_irq(mps_port->rx_irq, mps_port);
err_free_oerrirq:
	free_irq(port->irq, mps_port);

	return ret;
}

static void mps2_uart_shutdown(struct uart_port *port)
{
	struct mps2_uart_port *mps_port = to_mps2_port(port);
	u8 control = mps2_uart_read8(port, UARTn_CTRL);

	control &= ~(UARTn_CTRL_RX_GRP | UARTn_CTRL_TX_GRP);

	mps2_uart_write8(port, control, UARTn_CTRL);

	if (!(mps_port->flags & UART_PORT_COMBINED_IRQ)) {
		free_irq(mps_port->rx_irq, mps_port);
		free_irq(mps_port->tx_irq, mps_port);
	}

	free_irq(port->irq, mps_port);
}

static void
mps2_uart_set_termios(struct uart_port *port, struct ktermios *termios,
		      const struct ktermios *old)
{
	unsigned long flags;
	unsigned int baud, bauddiv;

	termios->c_cflag &= ~(CRTSCTS | CMSPAR);
	termios->c_cflag &= ~CSIZE;
	termios->c_cflag |= CS8;
	termios->c_cflag &= ~PARENB;
	termios->c_cflag &= ~CSTOPB;

	baud = uart_get_baud_rate(port, termios, old,
			DIV_ROUND_CLOSEST(port->uartclk, UARTn_BAUDDIV_MASK),
			DIV_ROUND_CLOSEST(port->uartclk, 16));

	bauddiv = DIV_ROUND_CLOSEST(port->uartclk, baud);

	spin_lock_irqsave(&port->lock, flags);

	uart_update_timeout(port, termios->c_cflag, baud);
	mps2_uart_write32(port, bauddiv, UARTn_BAUDDIV);

	spin_unlock_irqrestore(&port->lock, flags);

	if (tty_termios_baud_rate(termios))
		tty_termios_encode_baud_rate(termios, baud, baud);
}

static const char *mps2_uart_type(struct uart_port *port)
{
	return (port->type == PORT_MPS2UART) ? DRIVER_NAME : NULL;
}

static void mps2_uart_release_port(struct uart_port *port)
{
}

static int mps2_uart_request_port(struct uart_port *port)
{
	return 0;
}

static void mps2_uart_config_port(struct uart_port *port, int type)
{
	if (type & UART_CONFIG_TYPE && !mps2_uart_request_port(port))
		port->type = PORT_MPS2UART;
}

static int mps2_uart_verify_port(struct uart_port *port, struct serial_struct *serinfo)
{
	return -EINVAL;
}

static const struct uart_ops mps2_uart_pops = {
	.tx_empty = mps2_uart_tx_empty,
	.set_mctrl = mps2_uart_set_mctrl,
	.get_mctrl = mps2_uart_get_mctrl,
	.stop_tx = mps2_uart_stop_tx,
	.start_tx = mps2_uart_start_tx,
	.stop_rx = mps2_uart_stop_rx,
	.break_ctl = mps2_uart_break_ctl,
	.startup = mps2_uart_startup,
	.shutdown = mps2_uart_shutdown,
	.set_termios = mps2_uart_set_termios,
	.type = mps2_uart_type,
	.release_port = mps2_uart_release_port,
	.request_port = mps2_uart_request_port,
	.config_port = mps2_uart_config_port,
	.verify_port = mps2_uart_verify_port,
};

static DEFINE_IDR(ports_idr);

#ifdef CONFIG_SERIAL_MPS2_UART_CONSOLE
static void mps2_uart_console_putchar(struct uart_port *port, unsigned char ch)
{
	while (mps2_uart_read8(port, UARTn_STATE) & UARTn_STATE_TX_FULL)
		cpu_relax();

	mps2_uart_write8(port, ch, UARTn_DATA);
}

static void mps2_uart_console_write(struct console *co, const char *s, unsigned int cnt)
{
	struct mps2_uart_port *mps_port = idr_find(&ports_idr, co->index);
	struct uart_port *port = &mps_port->port;

	uart_console_write(port, s, cnt, mps2_uart_console_putchar);
}

static int mps2_uart_console_setup(struct console *co, char *options)
{
	struct mps2_uart_port *mps_port;
	int baud = 9600;
	int bits = 8;
	int parity = 'n';
	int flow = 'n';

	if (co->index < 0 || co->index >= MPS2_MAX_PORTS)
		return -ENODEV;

	mps_port = idr_find(&ports_idr, co->index);

	if (!mps_port)
		return -ENODEV;

	if (options)
		uart_parse_options(options, &baud, &parity, &bits, &flow);

	return uart_set_options(&mps_port->port, co, baud, parity, bits, flow);
}

static struct uart_driver mps2_uart_driver;

static struct console mps2_uart_console = {
	.name = SERIAL_NAME,
	.device = uart_console_device,
	.write = mps2_uart_console_write,
	.setup = mps2_uart_console_setup,
	.flags = CON_PRINTBUFFER,
	.index = -1,
	.data = &mps2_uart_driver,
};

#define MPS2_SERIAL_CONSOLE (&mps2_uart_console)

static void mps2_early_putchar(struct uart_port *port, unsigned char ch)
{
	while (readb(port->membase + UARTn_STATE) & UARTn_STATE_TX_FULL)
		cpu_relax();

	writeb((unsigned char)ch, port->membase + UARTn_DATA);
}

static void mps2_early_write(struct console *con, const char *s, unsigned int n)
{
	struct earlycon_device *dev = con->data;

	uart_console_write(&dev->port, s, n, mps2_early_putchar);
}

static int __init mps2_early_console_setup(struct earlycon_device *device,
					   const char *opt)
{
	if (!device->port.membase)
		return -ENODEV;

	device->con->write = mps2_early_write;

	return 0;
}

OF_EARLYCON_DECLARE(mps2, "arm,mps2-uart", mps2_early_console_setup);

#else
#define MPS2_SERIAL_CONSOLE NULL
#endif

static struct uart_driver mps2_uart_driver = {
	.driver_name = DRIVER_NAME,
	.dev_name = SERIAL_NAME,
	.nr = MPS2_MAX_PORTS,
	.cons = MPS2_SERIAL_CONSOLE,
};

static int mps2_of_get_port(struct platform_device *pdev,
			    struct mps2_uart_port *mps_port)
{
	struct device_node *np = pdev->dev.of_node;
	int id;

	if (!np)
		return -ENODEV;

	id = of_alias_get_id(np, "serial");

	if (id < 0)
		id = idr_alloc_cyclic(&ports_idr, (void *)mps_port, 0, MPS2_MAX_PORTS, GFP_KERNEL);
	else
		id = idr_alloc(&ports_idr, (void *)mps_port, id, MPS2_MAX_PORTS, GFP_KERNEL);

	if (id < 0)
		return id;

	/* Only combined irq is presesnt */
	if (platform_irq_count(pdev) == 1)
		mps_port->flags |= UART_PORT_COMBINED_IRQ;

	mps_port->port.line = id;

	return 0;
}

static int mps2_init_port(struct platform_device *pdev,
			  struct mps2_uart_port *mps_port)
{
	struct resource *res;
	int ret;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	mps_port->port.membase = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(mps_port->port.membase))
		return PTR_ERR(mps_port->port.membase);

	mps_port->port.mapbase = res->start;
	mps_port->port.mapsize = resource_size(res);
	mps_port->port.iotype = UPIO_MEM;
	mps_port->port.flags = UPF_BOOT_AUTOCONF;
	mps_port->port.fifosize = 1;
	mps_port->port.ops = &mps2_uart_pops;
	mps_port->port.dev = &pdev->dev;

	mps_port->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(mps_port->clk))
		return PTR_ERR(mps_port->clk);

	ret = clk_prepare_enable(mps_port->clk);
	if (ret)
		return ret;

	mps_port->port.uartclk = clk_get_rate(mps_port->clk);

	clk_disable_unprepare(mps_port->clk);


	if (mps_port->flags & UART_PORT_COMBINED_IRQ) {
		mps_port->port.irq = platform_get_irq(pdev, 0);
	} else {
		mps_port->rx_irq = platform_get_irq(pdev, 0);
		mps_port->tx_irq = platform_get_irq(pdev, 1);
		mps_port->port.irq = platform_get_irq(pdev, 2);
	}

	return ret;
}

static int mps2_serial_probe(struct platform_device *pdev)
{
	struct mps2_uart_port *mps_port;
	int ret;

	mps_port = devm_kzalloc(&pdev->dev, sizeof(struct mps2_uart_port), GFP_KERNEL);

        if (!mps_port)
                return -ENOMEM;

	ret = mps2_of_get_port(pdev, mps_port);
	if (ret)
		return ret;

	ret = mps2_init_port(pdev, mps_port);
	if (ret)
		return ret;

	ret = uart_add_one_port(&mps2_uart_driver, &mps_port->port);
	if (ret)
		return ret;

	platform_set_drvdata(pdev, mps_port);

	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id mps2_match[] = {
	{ .compatible = "arm,mps2-uart", },
	{},
};
#endif

static struct platform_driver mps2_serial_driver = {
	.probe = mps2_serial_probe,

	.driver = {
		.name = DRIVER_NAME,
		.of_match_table = of_match_ptr(mps2_match),
		.suppress_bind_attrs = true,
	},
};

static int __init mps2_uart_init(void)
{
	int ret;

	ret = uart_register_driver(&mps2_uart_driver);
	if (ret)
		return ret;

	ret = platform_driver_register(&mps2_serial_driver);
	if (ret)
		uart_unregister_driver(&mps2_uart_driver);

	return ret;
}
arch_initcall(mps2_uart_init);
