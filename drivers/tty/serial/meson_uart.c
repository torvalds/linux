// SPDX-License-Identifier: GPL-2.0
/*
 *  Based on meson_uart.c, by AMLOGIC, INC.
 *
 * Copyright (C) 2014 Carlo Caione <carlo@caione.org>
 */

#if defined(CONFIG_SERIAL_MESON_CONSOLE) && defined(CONFIG_MAGIC_SYSRQ)
#define SUPPORT_SYSRQ
#endif

#include <linux/clk.h>
#include <linux/console.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/serial.h>
#include <linux/serial_core.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>

/* Register offsets */
#define AML_UART_WFIFO			0x00
#define AML_UART_RFIFO			0x04
#define AML_UART_CONTROL		0x08
#define AML_UART_STATUS			0x0c
#define AML_UART_MISC			0x10
#define AML_UART_REG5			0x14

/* AML_UART_CONTROL bits */
#define AML_UART_TX_EN			BIT(12)
#define AML_UART_RX_EN			BIT(13)
#define AML_UART_TWO_WIRE_EN		BIT(15)
#define AML_UART_STOP_BIT_LEN_MASK	(0x03 << 16)
#define AML_UART_STOP_BIT_1SB		(0x00 << 16)
#define AML_UART_STOP_BIT_2SB		(0x01 << 16)
#define AML_UART_PARITY_TYPE		BIT(18)
#define AML_UART_PARITY_EN		BIT(19)
#define AML_UART_TX_RST			BIT(22)
#define AML_UART_RX_RST			BIT(23)
#define AML_UART_CLEAR_ERR		BIT(24)
#define AML_UART_RX_INT_EN		BIT(27)
#define AML_UART_TX_INT_EN		BIT(28)
#define AML_UART_DATA_LEN_MASK		(0x03 << 20)
#define AML_UART_DATA_LEN_8BIT		(0x00 << 20)
#define AML_UART_DATA_LEN_7BIT		(0x01 << 20)
#define AML_UART_DATA_LEN_6BIT		(0x02 << 20)
#define AML_UART_DATA_LEN_5BIT		(0x03 << 20)

/* AML_UART_STATUS bits */
#define AML_UART_PARITY_ERR		BIT(16)
#define AML_UART_FRAME_ERR		BIT(17)
#define AML_UART_TX_FIFO_WERR		BIT(18)
#define AML_UART_RX_EMPTY		BIT(20)
#define AML_UART_TX_FULL		BIT(21)
#define AML_UART_TX_EMPTY		BIT(22)
#define AML_UART_XMIT_BUSY		BIT(25)
#define AML_UART_ERR			(AML_UART_PARITY_ERR | \
					 AML_UART_FRAME_ERR  | \
					 AML_UART_TX_FIFO_WERR)

/* AML_UART_MISC bits */
#define AML_UART_XMIT_IRQ(c)		(((c) & 0xff) << 8)
#define AML_UART_RECV_IRQ(c)		((c) & 0xff)

/* AML_UART_REG5 bits */
#define AML_UART_BAUD_MASK		0x7fffff
#define AML_UART_BAUD_USE		BIT(23)
#define AML_UART_BAUD_XTAL		BIT(24)

#define AML_UART_PORT_NUM		12
#define AML_UART_PORT_OFFSET		6
#define AML_UART_DEV_NAME		"ttyAML"


static struct uart_driver meson_uart_driver;

static struct uart_port *meson_ports[AML_UART_PORT_NUM];

static void meson_uart_set_mctrl(struct uart_port *port, unsigned int mctrl)
{
}

static unsigned int meson_uart_get_mctrl(struct uart_port *port)
{
	return TIOCM_CTS;
}

static unsigned int meson_uart_tx_empty(struct uart_port *port)
{
	u32 val;

	val = readl(port->membase + AML_UART_STATUS);
	val &= (AML_UART_TX_EMPTY | AML_UART_XMIT_BUSY);
	return (val == AML_UART_TX_EMPTY) ? TIOCSER_TEMT : 0;
}

static void meson_uart_stop_tx(struct uart_port *port)
{
	u32 val;

	val = readl(port->membase + AML_UART_CONTROL);
	val &= ~AML_UART_TX_INT_EN;
	writel(val, port->membase + AML_UART_CONTROL);
}

static void meson_uart_stop_rx(struct uart_port *port)
{
	u32 val;

	val = readl(port->membase + AML_UART_CONTROL);
	val &= ~AML_UART_RX_EN;
	writel(val, port->membase + AML_UART_CONTROL);
}

static void meson_uart_shutdown(struct uart_port *port)
{
	unsigned long flags;
	u32 val;

	free_irq(port->irq, port);

	spin_lock_irqsave(&port->lock, flags);

	val = readl(port->membase + AML_UART_CONTROL);
	val &= ~AML_UART_RX_EN;
	val &= ~(AML_UART_RX_INT_EN | AML_UART_TX_INT_EN);
	writel(val, port->membase + AML_UART_CONTROL);

	spin_unlock_irqrestore(&port->lock, flags);
}

static void meson_uart_start_tx(struct uart_port *port)
{
	struct circ_buf *xmit = &port->state->xmit;
	unsigned int ch;
	u32 val;

	if (uart_tx_stopped(port)) {
		meson_uart_stop_tx(port);
		return;
	}

	while (!(readl(port->membase + AML_UART_STATUS) & AML_UART_TX_FULL)) {
		if (port->x_char) {
			writel(port->x_char, port->membase + AML_UART_WFIFO);
			port->icount.tx++;
			port->x_char = 0;
			continue;
		}

		if (uart_circ_empty(xmit))
			break;

		ch = xmit->buf[xmit->tail];
		writel(ch, port->membase + AML_UART_WFIFO);
		xmit->tail = (xmit->tail+1) & (SERIAL_XMIT_SIZE - 1);
		port->icount.tx++;
	}

	if (!uart_circ_empty(xmit)) {
		val = readl(port->membase + AML_UART_CONTROL);
		val |= AML_UART_TX_INT_EN;
		writel(val, port->membase + AML_UART_CONTROL);
	}

	if (uart_circ_chars_pending(xmit) < WAKEUP_CHARS)
		uart_write_wakeup(port);
}

static void meson_receive_chars(struct uart_port *port)
{
	struct tty_port *tport = &port->state->port;
	char flag;
	u32 ostatus, status, ch, mode;

	do {
		flag = TTY_NORMAL;
		port->icount.rx++;
		ostatus = status = readl(port->membase + AML_UART_STATUS);

		if (status & AML_UART_ERR) {
			if (status & AML_UART_TX_FIFO_WERR)
				port->icount.overrun++;
			else if (status & AML_UART_FRAME_ERR)
				port->icount.frame++;
			else if (status & AML_UART_PARITY_ERR)
				port->icount.frame++;

			mode = readl(port->membase + AML_UART_CONTROL);
			mode |= AML_UART_CLEAR_ERR;
			writel(mode, port->membase + AML_UART_CONTROL);

			/* It doesn't clear to 0 automatically */
			mode &= ~AML_UART_CLEAR_ERR;
			writel(mode, port->membase + AML_UART_CONTROL);

			status &= port->read_status_mask;
			if (status & AML_UART_FRAME_ERR)
				flag = TTY_FRAME;
			else if (status & AML_UART_PARITY_ERR)
				flag = TTY_PARITY;
		}

		ch = readl(port->membase + AML_UART_RFIFO);
		ch &= 0xff;

		if ((ostatus & AML_UART_FRAME_ERR) && (ch == 0)) {
			port->icount.brk++;
			flag = TTY_BREAK;
			if (uart_handle_break(port))
				continue;
		}

		if (uart_handle_sysrq_char(port, ch))
			continue;

		if ((status & port->ignore_status_mask) == 0)
			tty_insert_flip_char(tport, ch, flag);

		if (status & AML_UART_TX_FIFO_WERR)
			tty_insert_flip_char(tport, 0, TTY_OVERRUN);

	} while (!(readl(port->membase + AML_UART_STATUS) & AML_UART_RX_EMPTY));

	spin_unlock(&port->lock);
	tty_flip_buffer_push(tport);
	spin_lock(&port->lock);
}

static irqreturn_t meson_uart_interrupt(int irq, void *dev_id)
{
	struct uart_port *port = (struct uart_port *)dev_id;

	spin_lock(&port->lock);

	if (!(readl(port->membase + AML_UART_STATUS) & AML_UART_RX_EMPTY))
		meson_receive_chars(port);

	if (!(readl(port->membase + AML_UART_STATUS) & AML_UART_TX_FULL)) {
		if (readl(port->membase + AML_UART_CONTROL) & AML_UART_TX_INT_EN)
			meson_uart_start_tx(port);
	}

	spin_unlock(&port->lock);

	return IRQ_HANDLED;
}

static const char *meson_uart_type(struct uart_port *port)
{
	return (port->type == PORT_MESON) ? "meson_uart" : NULL;
}

static void meson_uart_reset(struct uart_port *port)
{
	u32 val;

	val = readl(port->membase + AML_UART_CONTROL);
	val |= (AML_UART_RX_RST | AML_UART_TX_RST | AML_UART_CLEAR_ERR);
	writel(val, port->membase + AML_UART_CONTROL);

	val &= ~(AML_UART_RX_RST | AML_UART_TX_RST | AML_UART_CLEAR_ERR);
	writel(val, port->membase + AML_UART_CONTROL);
}

static int meson_uart_startup(struct uart_port *port)
{
	u32 val;
	int ret = 0;

	val = readl(port->membase + AML_UART_CONTROL);
	val |= AML_UART_CLEAR_ERR;
	writel(val, port->membase + AML_UART_CONTROL);
	val &= ~AML_UART_CLEAR_ERR;
	writel(val, port->membase + AML_UART_CONTROL);

	val |= (AML_UART_RX_EN | AML_UART_TX_EN);
	writel(val, port->membase + AML_UART_CONTROL);

	val |= (AML_UART_RX_INT_EN | AML_UART_TX_INT_EN);
	writel(val, port->membase + AML_UART_CONTROL);

	val = (AML_UART_RECV_IRQ(1) | AML_UART_XMIT_IRQ(port->fifosize / 2));
	writel(val, port->membase + AML_UART_MISC);

	ret = request_irq(port->irq, meson_uart_interrupt, 0,
			  port->name, port);

	return ret;
}

static void meson_uart_change_speed(struct uart_port *port, unsigned long baud)
{
	u32 val;

	while (!meson_uart_tx_empty(port))
		cpu_relax();

	if (port->uartclk == 24000000) {
		val = ((port->uartclk / 3) / baud) - 1;
		val |= AML_UART_BAUD_XTAL;
	} else {
		val = ((port->uartclk * 10 / (baud * 4) + 5) / 10) - 1;
	}
	val |= AML_UART_BAUD_USE;
	writel(val, port->membase + AML_UART_REG5);
}

static void meson_uart_set_termios(struct uart_port *port,
				   struct ktermios *termios,
				   struct ktermios *old)
{
	unsigned int cflags, iflags, baud;
	unsigned long flags;
	u32 val;

	spin_lock_irqsave(&port->lock, flags);

	cflags = termios->c_cflag;
	iflags = termios->c_iflag;

	val = readl(port->membase + AML_UART_CONTROL);

	val &= ~AML_UART_DATA_LEN_MASK;
	switch (cflags & CSIZE) {
	case CS8:
		val |= AML_UART_DATA_LEN_8BIT;
		break;
	case CS7:
		val |= AML_UART_DATA_LEN_7BIT;
		break;
	case CS6:
		val |= AML_UART_DATA_LEN_6BIT;
		break;
	case CS5:
		val |= AML_UART_DATA_LEN_5BIT;
		break;
	}

	if (cflags & PARENB)
		val |= AML_UART_PARITY_EN;
	else
		val &= ~AML_UART_PARITY_EN;

	if (cflags & PARODD)
		val |= AML_UART_PARITY_TYPE;
	else
		val &= ~AML_UART_PARITY_TYPE;

	val &= ~AML_UART_STOP_BIT_LEN_MASK;
	if (cflags & CSTOPB)
		val |= AML_UART_STOP_BIT_2SB;
	else
		val |= AML_UART_STOP_BIT_1SB;

	if (cflags & CRTSCTS)
		val &= ~AML_UART_TWO_WIRE_EN;
	else
		val |= AML_UART_TWO_WIRE_EN;

	writel(val, port->membase + AML_UART_CONTROL);

	baud = uart_get_baud_rate(port, termios, old, 50, 4000000);
	meson_uart_change_speed(port, baud);

	port->read_status_mask = AML_UART_TX_FIFO_WERR;
	if (iflags & INPCK)
		port->read_status_mask |= AML_UART_PARITY_ERR |
					  AML_UART_FRAME_ERR;

	port->ignore_status_mask = 0;
	if (iflags & IGNPAR)
		port->ignore_status_mask |= AML_UART_PARITY_ERR |
					    AML_UART_FRAME_ERR;

	uart_update_timeout(port, termios->c_cflag, baud);
	spin_unlock_irqrestore(&port->lock, flags);
}

static int meson_uart_verify_port(struct uart_port *port,
				  struct serial_struct *ser)
{
	int ret = 0;

	if (port->type != PORT_MESON)
		ret = -EINVAL;
	if (port->irq != ser->irq)
		ret = -EINVAL;
	if (ser->baud_base < 9600)
		ret = -EINVAL;
	return ret;
}

static void meson_uart_release_port(struct uart_port *port)
{
	devm_iounmap(port->dev, port->membase);
	port->membase = NULL;
	devm_release_mem_region(port->dev, port->mapbase, port->mapsize);
}

static int meson_uart_request_port(struct uart_port *port)
{
	if (!devm_request_mem_region(port->dev, port->mapbase, port->mapsize,
				     dev_name(port->dev))) {
		dev_err(port->dev, "Memory region busy\n");
		return -EBUSY;
	}

	port->membase = devm_ioremap_nocache(port->dev, port->mapbase,
					     port->mapsize);
	if (!port->membase)
		return -ENOMEM;

	return 0;
}

static void meson_uart_config_port(struct uart_port *port, int flags)
{
	if (flags & UART_CONFIG_TYPE) {
		port->type = PORT_MESON;
		meson_uart_request_port(port);
	}
}

static const struct uart_ops meson_uart_ops = {
	.set_mctrl      = meson_uart_set_mctrl,
	.get_mctrl      = meson_uart_get_mctrl,
	.tx_empty	= meson_uart_tx_empty,
	.start_tx	= meson_uart_start_tx,
	.stop_tx	= meson_uart_stop_tx,
	.stop_rx	= meson_uart_stop_rx,
	.startup	= meson_uart_startup,
	.shutdown	= meson_uart_shutdown,
	.set_termios	= meson_uart_set_termios,
	.type		= meson_uart_type,
	.config_port	= meson_uart_config_port,
	.request_port	= meson_uart_request_port,
	.release_port	= meson_uart_release_port,
	.verify_port	= meson_uart_verify_port,
};

#ifdef CONFIG_SERIAL_MESON_CONSOLE
static void meson_uart_enable_tx_engine(struct uart_port *port)
{
	u32 val;

	val = readl(port->membase + AML_UART_CONTROL);
	val |= AML_UART_TX_EN;
	writel(val, port->membase + AML_UART_CONTROL);
}

static void meson_console_putchar(struct uart_port *port, int ch)
{
	if (!port->membase)
		return;

	while (readl(port->membase + AML_UART_STATUS) & AML_UART_TX_FULL)
		cpu_relax();
	writel(ch, port->membase + AML_UART_WFIFO);
}

static void meson_serial_port_write(struct uart_port *port, const char *s,
				    u_int count)
{
	unsigned long flags;
	int locked;
	u32 val, tmp;

	local_irq_save(flags);
	if (port->sysrq) {
		locked = 0;
	} else if (oops_in_progress) {
		locked = spin_trylock(&port->lock);
	} else {
		spin_lock(&port->lock);
		locked = 1;
	}

	val = readl(port->membase + AML_UART_CONTROL);
	tmp = val & ~(AML_UART_TX_INT_EN | AML_UART_RX_INT_EN);
	writel(tmp, port->membase + AML_UART_CONTROL);

	uart_console_write(port, s, count, meson_console_putchar);
	writel(val, port->membase + AML_UART_CONTROL);

	if (locked)
		spin_unlock(&port->lock);
	local_irq_restore(flags);
}

static void meson_serial_console_write(struct console *co, const char *s,
				       u_int count)
{
	struct uart_port *port;

	port = meson_ports[co->index];
	if (!port)
		return;

	meson_serial_port_write(port, s, count);
}

static int meson_serial_console_setup(struct console *co, char *options)
{
	struct uart_port *port;
	int baud = 115200;
	int bits = 8;
	int parity = 'n';
	int flow = 'n';

	if (co->index < 0 || co->index >= AML_UART_PORT_NUM)
		return -EINVAL;

	port = meson_ports[co->index];
	if (!port || !port->membase)
		return -ENODEV;

	meson_uart_enable_tx_engine(port);

	if (options)
		uart_parse_options(options, &baud, &parity, &bits, &flow);

	return uart_set_options(port, co, baud, parity, bits, flow);
}

static struct console meson_serial_console = {
	.name		= AML_UART_DEV_NAME,
	.write		= meson_serial_console_write,
	.device		= uart_console_device,
	.setup		= meson_serial_console_setup,
	.flags		= CON_PRINTBUFFER,
	.index		= -1,
	.data		= &meson_uart_driver,
};

static int __init meson_serial_console_init(void)
{
	register_console(&meson_serial_console);
	return 0;
}
console_initcall(meson_serial_console_init);

static void meson_serial_early_console_write(struct console *co,
					     const char *s,
					     u_int count)
{
	struct earlycon_device *dev = co->data;

	meson_serial_port_write(&dev->port, s, count);
}

static int __init
meson_serial_early_console_setup(struct earlycon_device *device, const char *opt)
{
	if (!device->port.membase)
		return -ENODEV;

	meson_uart_enable_tx_engine(&device->port);
	device->con->write = meson_serial_early_console_write;
	return 0;
}
/* Legacy bindings, should be removed when no more used */
OF_EARLYCON_DECLARE(meson, "amlogic,meson-uart",
		    meson_serial_early_console_setup);
/* Stable bindings */
OF_EARLYCON_DECLARE(meson, "amlogic,meson-ao-uart",
		    meson_serial_early_console_setup);

#define MESON_SERIAL_CONSOLE	(&meson_serial_console)
#else
#define MESON_SERIAL_CONSOLE	NULL
#endif

static struct uart_driver meson_uart_driver = {
	.owner		= THIS_MODULE,
	.driver_name	= "meson_uart",
	.dev_name	= AML_UART_DEV_NAME,
	.nr		= AML_UART_PORT_NUM,
	.cons		= MESON_SERIAL_CONSOLE,
};

static inline struct clk *meson_uart_probe_clock(struct device *dev,
						 const char *id)
{
	struct clk *clk = NULL;
	int ret;

	clk = devm_clk_get(dev, id);
	if (IS_ERR(clk))
		return clk;

	ret = clk_prepare_enable(clk);
	if (ret) {
		dev_err(dev, "couldn't enable clk\n");
		return ERR_PTR(ret);
	}

	devm_add_action_or_reset(dev,
			(void(*)(void *))clk_disable_unprepare,
			clk);

	return clk;
}

/*
 * This function gets clocks in the legacy non-stable DT bindings.
 * This code will be remove once all the platforms switch to the
 * new DT bindings.
 */
static int meson_uart_probe_clocks_legacy(struct platform_device *pdev,
					  struct uart_port *port)
{
	struct clk *clk = NULL;

	clk = meson_uart_probe_clock(&pdev->dev, NULL);
	if (IS_ERR(clk))
		return PTR_ERR(clk);

	port->uartclk = clk_get_rate(clk);

	return 0;
}

static int meson_uart_probe_clocks(struct platform_device *pdev,
				   struct uart_port *port)
{
	struct clk *clk_xtal = NULL;
	struct clk *clk_pclk = NULL;
	struct clk *clk_baud = NULL;

	clk_pclk = meson_uart_probe_clock(&pdev->dev, "pclk");
	if (IS_ERR(clk_pclk))
		return PTR_ERR(clk_pclk);

	clk_xtal = meson_uart_probe_clock(&pdev->dev, "xtal");
	if (IS_ERR(clk_xtal))
		return PTR_ERR(clk_xtal);

	clk_baud = meson_uart_probe_clock(&pdev->dev, "baud");
	if (IS_ERR(clk_baud))
		return PTR_ERR(clk_baud);

	port->uartclk = clk_get_rate(clk_baud);

	return 0;
}

static int meson_uart_probe(struct platform_device *pdev)
{
	struct resource *res_mem, *res_irq;
	struct uart_port *port;
	int ret = 0;
	int id = -1;

	if (pdev->dev.of_node)
		pdev->id = of_alias_get_id(pdev->dev.of_node, "serial");

	if (pdev->id < 0) {
		for (id = AML_UART_PORT_OFFSET; id < AML_UART_PORT_NUM; id++) {
			if (!meson_ports[id]) {
				pdev->id = id;
				break;
			}
		}
	}

	if (pdev->id < 0 || pdev->id >= AML_UART_PORT_NUM)
		return -EINVAL;

	res_mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res_mem)
		return -ENODEV;

	res_irq = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if (!res_irq)
		return -ENODEV;

	if (meson_ports[pdev->id]) {
		dev_err(&pdev->dev, "port %d already allocated\n", pdev->id);
		return -EBUSY;
	}

	port = devm_kzalloc(&pdev->dev, sizeof(struct uart_port), GFP_KERNEL);
	if (!port)
		return -ENOMEM;

	/* Use legacy way until all platforms switch to new bindings */
	if (of_device_is_compatible(pdev->dev.of_node, "amlogic,meson-uart"))
		ret = meson_uart_probe_clocks_legacy(pdev, port);
	else
		ret = meson_uart_probe_clocks(pdev, port);

	if (ret)
		return ret;

	port->iotype = UPIO_MEM;
	port->mapbase = res_mem->start;
	port->mapsize = resource_size(res_mem);
	port->irq = res_irq->start;
	port->flags = UPF_BOOT_AUTOCONF | UPF_LOW_LATENCY;
	port->dev = &pdev->dev;
	port->line = pdev->id;
	port->type = PORT_MESON;
	port->x_char = 0;
	port->ops = &meson_uart_ops;
	port->fifosize = 64;

	meson_ports[pdev->id] = port;
	platform_set_drvdata(pdev, port);

	/* reset port before registering (and possibly registering console) */
	if (meson_uart_request_port(port) >= 0) {
		meson_uart_reset(port);
		meson_uart_release_port(port);
	}

	ret = uart_add_one_port(&meson_uart_driver, port);
	if (ret)
		meson_ports[pdev->id] = NULL;

	return ret;
}

static int meson_uart_remove(struct platform_device *pdev)
{
	struct uart_port *port;

	port = platform_get_drvdata(pdev);
	uart_remove_one_port(&meson_uart_driver, port);
	meson_ports[pdev->id] = NULL;

	return 0;
}

static const struct of_device_id meson_uart_dt_match[] = {
	/* Legacy bindings, should be removed when no more used */
	{ .compatible = "amlogic,meson-uart" },
	/* Stable bindings */
	{ .compatible = "amlogic,meson6-uart" },
	{ .compatible = "amlogic,meson8-uart" },
	{ .compatible = "amlogic,meson8b-uart" },
	{ .compatible = "amlogic,meson-gx-uart" },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, meson_uart_dt_match);

static  struct platform_driver meson_uart_platform_driver = {
	.probe		= meson_uart_probe,
	.remove		= meson_uart_remove,
	.driver		= {
		.name		= "meson_uart",
		.of_match_table	= meson_uart_dt_match,
	},
};

static int __init meson_uart_init(void)
{
	int ret;

	ret = uart_register_driver(&meson_uart_driver);
	if (ret)
		return ret;

	ret = platform_driver_register(&meson_uart_platform_driver);
	if (ret)
		uart_unregister_driver(&meson_uart_driver);

	return ret;
}

static void __exit meson_uart_exit(void)
{
	platform_driver_unregister(&meson_uart_platform_driver);
	uart_unregister_driver(&meson_uart_driver);
}

module_init(meson_uart_init);
module_exit(meson_uart_exit);

MODULE_AUTHOR("Carlo Caione <carlo@caione.org>");
MODULE_DESCRIPTION("Amlogic Meson serial port driver");
MODULE_LICENSE("GPL v2");
