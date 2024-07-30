// SPDX-License-Identifier: GPL-2.0+
/*
 * Actions Semi Owl family serial console
 *
 * Copyright 2013 Actions Semi Inc.
 * Author: Actions Semi, Inc.
 *
 * Copyright (c) 2016-2017 Andreas Färber
 */

#include <linux/clk.h>
#include <linux/console.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/serial.h>
#include <linux/serial_core.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>

#define OWL_UART_PORT_NUM 7
#define OWL_UART_DEV_NAME "ttyOWL"

#define OWL_UART_CTL	0x000
#define OWL_UART_RXDAT	0x004
#define OWL_UART_TXDAT	0x008
#define OWL_UART_STAT	0x00c

#define OWL_UART_CTL_DWLS_MASK		GENMASK(1, 0)
#define OWL_UART_CTL_DWLS_5BITS		(0x0 << 0)
#define OWL_UART_CTL_DWLS_6BITS		(0x1 << 0)
#define OWL_UART_CTL_DWLS_7BITS		(0x2 << 0)
#define OWL_UART_CTL_DWLS_8BITS		(0x3 << 0)
#define OWL_UART_CTL_STPS_2BITS		BIT(2)
#define OWL_UART_CTL_PRS_MASK		GENMASK(6, 4)
#define OWL_UART_CTL_PRS_NONE		(0x0 << 4)
#define OWL_UART_CTL_PRS_ODD		(0x4 << 4)
#define OWL_UART_CTL_PRS_MARK		(0x5 << 4)
#define OWL_UART_CTL_PRS_EVEN		(0x6 << 4)
#define OWL_UART_CTL_PRS_SPACE		(0x7 << 4)
#define OWL_UART_CTL_AFE		BIT(12)
#define OWL_UART_CTL_TRFS_TX		BIT(14)
#define OWL_UART_CTL_EN			BIT(15)
#define OWL_UART_CTL_RXDE		BIT(16)
#define OWL_UART_CTL_TXDE		BIT(17)
#define OWL_UART_CTL_RXIE		BIT(18)
#define OWL_UART_CTL_TXIE		BIT(19)
#define OWL_UART_CTL_LBEN		BIT(20)

#define OWL_UART_STAT_RIP		BIT(0)
#define OWL_UART_STAT_TIP		BIT(1)
#define OWL_UART_STAT_RXER		BIT(2)
#define OWL_UART_STAT_TFER		BIT(3)
#define OWL_UART_STAT_RXST		BIT(4)
#define OWL_UART_STAT_RFEM		BIT(5)
#define OWL_UART_STAT_TFFU		BIT(6)
#define OWL_UART_STAT_CTSS		BIT(7)
#define OWL_UART_STAT_RTSS		BIT(8)
#define OWL_UART_STAT_TFES		BIT(10)
#define OWL_UART_STAT_TRFL_MASK		GENMASK(16, 11)
#define OWL_UART_STAT_UTBB		BIT(17)

#define OWL_UART_POLL_USEC		5
#define OWL_UART_TIMEOUT_USEC		10000

static struct uart_driver owl_uart_driver;

struct owl_uart_info {
	unsigned int tx_fifosize;
};

struct owl_uart_port {
	struct uart_port port;
	struct clk *clk;
};

#define to_owl_uart_port(prt) container_of(prt, struct owl_uart_port, prt)

static struct owl_uart_port *owl_uart_ports[OWL_UART_PORT_NUM];

static inline void owl_uart_write(struct uart_port *port, u32 val, unsigned int off)
{
	writel(val, port->membase + off);
}

static inline u32 owl_uart_read(struct uart_port *port, unsigned int off)
{
	return readl(port->membase + off);
}

static void owl_uart_set_mctrl(struct uart_port *port, unsigned int mctrl)
{
	u32 ctl;

	ctl = owl_uart_read(port, OWL_UART_CTL);

	if (mctrl & TIOCM_LOOP)
		ctl |= OWL_UART_CTL_LBEN;
	else
		ctl &= ~OWL_UART_CTL_LBEN;

	owl_uart_write(port, ctl, OWL_UART_CTL);
}

static unsigned int owl_uart_get_mctrl(struct uart_port *port)
{
	unsigned int mctrl = TIOCM_CAR | TIOCM_DSR;
	u32 stat, ctl;

	ctl = owl_uart_read(port, OWL_UART_CTL);
	stat = owl_uart_read(port, OWL_UART_STAT);
	if (stat & OWL_UART_STAT_RTSS)
		mctrl |= TIOCM_RTS;
	if ((stat & OWL_UART_STAT_CTSS) || !(ctl & OWL_UART_CTL_AFE))
		mctrl |= TIOCM_CTS;
	return mctrl;
}

static unsigned int owl_uart_tx_empty(struct uart_port *port)
{
	unsigned long flags;
	u32 val;
	unsigned int ret;

	uart_port_lock_irqsave(port, &flags);

	val = owl_uart_read(port, OWL_UART_STAT);
	ret = (val & OWL_UART_STAT_TFES) ? TIOCSER_TEMT : 0;

	uart_port_unlock_irqrestore(port, flags);

	return ret;
}

static void owl_uart_stop_rx(struct uart_port *port)
{
	u32 val;

	val = owl_uart_read(port, OWL_UART_CTL);
	val &= ~(OWL_UART_CTL_RXIE | OWL_UART_CTL_RXDE);
	owl_uart_write(port, val, OWL_UART_CTL);

	val = owl_uart_read(port, OWL_UART_STAT);
	val |= OWL_UART_STAT_RIP;
	owl_uart_write(port, val, OWL_UART_STAT);
}

static void owl_uart_stop_tx(struct uart_port *port)
{
	u32 val;

	val = owl_uart_read(port, OWL_UART_CTL);
	val &= ~(OWL_UART_CTL_TXIE | OWL_UART_CTL_TXDE);
	owl_uart_write(port, val, OWL_UART_CTL);

	val = owl_uart_read(port, OWL_UART_STAT);
	val |= OWL_UART_STAT_TIP;
	owl_uart_write(port, val, OWL_UART_STAT);
}

static void owl_uart_start_tx(struct uart_port *port)
{
	u32 val;

	if (uart_tx_stopped(port)) {
		owl_uart_stop_tx(port);
		return;
	}

	val = owl_uart_read(port, OWL_UART_STAT);
	val |= OWL_UART_STAT_TIP;
	owl_uart_write(port, val, OWL_UART_STAT);

	val = owl_uart_read(port, OWL_UART_CTL);
	val |= OWL_UART_CTL_TXIE;
	owl_uart_write(port, val, OWL_UART_CTL);
}

static void owl_uart_send_chars(struct uart_port *port)
{
	u8 ch;

	uart_port_tx(port, ch,
		!(owl_uart_read(port, OWL_UART_STAT) & OWL_UART_STAT_TFFU),
		owl_uart_write(port, ch, OWL_UART_TXDAT));
}

static void owl_uart_receive_chars(struct uart_port *port)
{
	u32 stat, val;

	val = owl_uart_read(port, OWL_UART_CTL);
	val &= ~OWL_UART_CTL_TRFS_TX;
	owl_uart_write(port, val, OWL_UART_CTL);

	stat = owl_uart_read(port, OWL_UART_STAT);
	while (!(stat & OWL_UART_STAT_RFEM)) {
		char flag = TTY_NORMAL;
		bool sysrq;

		if (stat & OWL_UART_STAT_RXER)
			port->icount.overrun++;

		if (stat & OWL_UART_STAT_RXST) {
			/* We are not able to distinguish the error type. */
			port->icount.brk++;
			port->icount.frame++;

			stat &= port->read_status_mask;
			if (stat & OWL_UART_STAT_RXST)
				flag = TTY_PARITY;
		} else
			port->icount.rx++;

		val = owl_uart_read(port, OWL_UART_RXDAT);
		val &= 0xff;

		sysrq = uart_prepare_sysrq_char(port, val);

		if (!sysrq && (stat & port->ignore_status_mask) == 0)
			tty_insert_flip_char(&port->state->port, val, flag);

		stat = owl_uart_read(port, OWL_UART_STAT);
	}

	tty_flip_buffer_push(&port->state->port);
}

static irqreturn_t owl_uart_irq(int irq, void *dev_id)
{
	struct uart_port *port = dev_id;
	u32 stat;

	uart_port_lock(port);

	stat = owl_uart_read(port, OWL_UART_STAT);

	if (stat & OWL_UART_STAT_RIP)
		owl_uart_receive_chars(port);

	if (stat & OWL_UART_STAT_TIP)
		owl_uart_send_chars(port);

	stat = owl_uart_read(port, OWL_UART_STAT);
	stat |= OWL_UART_STAT_RIP | OWL_UART_STAT_TIP;
	owl_uart_write(port, stat, OWL_UART_STAT);

	uart_unlock_and_check_sysrq(port);

	return IRQ_HANDLED;
}

static void owl_uart_shutdown(struct uart_port *port)
{
	u32 val;
	unsigned long flags;

	uart_port_lock_irqsave(port, &flags);

	val = owl_uart_read(port, OWL_UART_CTL);
	val &= ~(OWL_UART_CTL_TXIE | OWL_UART_CTL_RXIE
		| OWL_UART_CTL_TXDE | OWL_UART_CTL_RXDE | OWL_UART_CTL_EN);
	owl_uart_write(port, val, OWL_UART_CTL);

	uart_port_unlock_irqrestore(port, flags);

	free_irq(port->irq, port);
}

static int owl_uart_startup(struct uart_port *port)
{
	u32 val;
	unsigned long flags;
	int ret;

	ret = request_irq(port->irq, owl_uart_irq, IRQF_TRIGGER_HIGH,
			"owl-uart", port);
	if (ret)
		return ret;

	uart_port_lock_irqsave(port, &flags);

	val = owl_uart_read(port, OWL_UART_STAT);
	val |= OWL_UART_STAT_RIP | OWL_UART_STAT_TIP
		| OWL_UART_STAT_RXER | OWL_UART_STAT_TFER | OWL_UART_STAT_RXST;
	owl_uart_write(port, val, OWL_UART_STAT);

	val = owl_uart_read(port, OWL_UART_CTL);
	val |= OWL_UART_CTL_RXIE | OWL_UART_CTL_TXIE;
	val |= OWL_UART_CTL_EN;
	owl_uart_write(port, val, OWL_UART_CTL);

	uart_port_unlock_irqrestore(port, flags);

	return 0;
}

static void owl_uart_change_baudrate(struct owl_uart_port *owl_port,
				     unsigned long baud)
{
	clk_set_rate(owl_port->clk, baud * 8);
}

static void owl_uart_set_termios(struct uart_port *port,
				 struct ktermios *termios,
				 const struct ktermios *old)
{
	struct owl_uart_port *owl_port = to_owl_uart_port(port);
	unsigned int baud;
	u32 ctl;
	unsigned long flags;

	uart_port_lock_irqsave(port, &flags);

	ctl = owl_uart_read(port, OWL_UART_CTL);

	ctl &= ~OWL_UART_CTL_DWLS_MASK;
	switch (termios->c_cflag & CSIZE) {
	case CS5:
		ctl |= OWL_UART_CTL_DWLS_5BITS;
		break;
	case CS6:
		ctl |= OWL_UART_CTL_DWLS_6BITS;
		break;
	case CS7:
		ctl |= OWL_UART_CTL_DWLS_7BITS;
		break;
	case CS8:
	default:
		ctl |= OWL_UART_CTL_DWLS_8BITS;
		break;
	}

	if (termios->c_cflag & CSTOPB)
		ctl |= OWL_UART_CTL_STPS_2BITS;
	else
		ctl &= ~OWL_UART_CTL_STPS_2BITS;

	ctl &= ~OWL_UART_CTL_PRS_MASK;
	if (termios->c_cflag & PARENB) {
		if (termios->c_cflag & CMSPAR) {
			if (termios->c_cflag & PARODD)
				ctl |= OWL_UART_CTL_PRS_MARK;
			else
				ctl |= OWL_UART_CTL_PRS_SPACE;
		} else if (termios->c_cflag & PARODD)
			ctl |= OWL_UART_CTL_PRS_ODD;
		else
			ctl |= OWL_UART_CTL_PRS_EVEN;
	} else
		ctl |= OWL_UART_CTL_PRS_NONE;

	if (termios->c_cflag & CRTSCTS)
		ctl |= OWL_UART_CTL_AFE;
	else
		ctl &= ~OWL_UART_CTL_AFE;

	owl_uart_write(port, ctl, OWL_UART_CTL);

	baud = uart_get_baud_rate(port, termios, old, 9600, 3200000);
	owl_uart_change_baudrate(owl_port, baud);

	/* Don't rewrite B0 */
	if (tty_termios_baud_rate(termios))
		tty_termios_encode_baud_rate(termios, baud, baud);

	port->read_status_mask |= OWL_UART_STAT_RXER;
	if (termios->c_iflag & INPCK)
		port->read_status_mask |= OWL_UART_STAT_RXST;

	uart_update_timeout(port, termios->c_cflag, baud);

	uart_port_unlock_irqrestore(port, flags);
}

static void owl_uart_release_port(struct uart_port *port)
{
	struct platform_device *pdev = to_platform_device(port->dev);
	struct resource *res;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return;

	if (port->flags & UPF_IOREMAP) {
		devm_release_mem_region(port->dev, port->mapbase,
			resource_size(res));
		devm_iounmap(port->dev, port->membase);
		port->membase = NULL;
	}
}

static int owl_uart_request_port(struct uart_port *port)
{
	struct platform_device *pdev = to_platform_device(port->dev);
	struct resource *res;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -ENXIO;

	if (!devm_request_mem_region(port->dev, port->mapbase,
			resource_size(res), dev_name(port->dev)))
		return -EBUSY;

	if (port->flags & UPF_IOREMAP) {
		port->membase = devm_ioremap(port->dev, port->mapbase,
				resource_size(res));
		if (!port->membase)
			return -EBUSY;
	}

	return 0;
}

static const char *owl_uart_type(struct uart_port *port)
{
	return (port->type == PORT_OWL) ? "owl-uart" : NULL;
}

static int owl_uart_verify_port(struct uart_port *port,
				struct serial_struct *ser)
{
	if (port->type != PORT_OWL)
		return -EINVAL;

	if (port->irq != ser->irq)
		return -EINVAL;

	return 0;
}

static void owl_uart_config_port(struct uart_port *port, int flags)
{
	if (flags & UART_CONFIG_TYPE) {
		port->type = PORT_OWL;
		owl_uart_request_port(port);
	}
}

#ifdef CONFIG_CONSOLE_POLL

static int owl_uart_poll_get_char(struct uart_port *port)
{
	if (owl_uart_read(port, OWL_UART_STAT) & OWL_UART_STAT_RFEM)
		return NO_POLL_CHAR;

	return owl_uart_read(port, OWL_UART_RXDAT);
}

static void owl_uart_poll_put_char(struct uart_port *port, unsigned char ch)
{
	u32 reg;
	int ret;

	/* Wait while FIFO is full or timeout */
	ret = readl_poll_timeout_atomic(port->membase + OWL_UART_STAT, reg,
					!(reg & OWL_UART_STAT_TFFU),
					OWL_UART_POLL_USEC,
					OWL_UART_TIMEOUT_USEC);
	if (ret == -ETIMEDOUT) {
		dev_err(port->dev, "Timeout waiting while UART TX FULL\n");
		return;
	}

	owl_uart_write(port, ch, OWL_UART_TXDAT);
}

#endif /* CONFIG_CONSOLE_POLL */

static const struct uart_ops owl_uart_ops = {
	.set_mctrl = owl_uart_set_mctrl,
	.get_mctrl = owl_uart_get_mctrl,
	.tx_empty = owl_uart_tx_empty,
	.start_tx = owl_uart_start_tx,
	.stop_rx = owl_uart_stop_rx,
	.stop_tx = owl_uart_stop_tx,
	.startup = owl_uart_startup,
	.shutdown = owl_uart_shutdown,
	.set_termios = owl_uart_set_termios,
	.type = owl_uart_type,
	.config_port = owl_uart_config_port,
	.request_port = owl_uart_request_port,
	.release_port = owl_uart_release_port,
	.verify_port = owl_uart_verify_port,
#ifdef CONFIG_CONSOLE_POLL
	.poll_get_char = owl_uart_poll_get_char,
	.poll_put_char = owl_uart_poll_put_char,
#endif
};

#ifdef CONFIG_SERIAL_OWL_CONSOLE

static void owl_console_putchar(struct uart_port *port, unsigned char ch)
{
	if (!port->membase)
		return;

	while (owl_uart_read(port, OWL_UART_STAT) & OWL_UART_STAT_TFFU)
		cpu_relax();

	owl_uart_write(port, ch, OWL_UART_TXDAT);
}

static void owl_uart_port_write(struct uart_port *port, const char *s,
				u_int count)
{
	u32 old_ctl, val;
	unsigned long flags;
	int locked = 1;

	if (oops_in_progress)
		locked = uart_port_trylock_irqsave(port, &flags);
	else
		uart_port_lock_irqsave(port, &flags);

	old_ctl = owl_uart_read(port, OWL_UART_CTL);
	val = old_ctl | OWL_UART_CTL_TRFS_TX;
	/* disable IRQ */
	val &= ~(OWL_UART_CTL_RXIE | OWL_UART_CTL_TXIE);
	owl_uart_write(port, val, OWL_UART_CTL);

	uart_console_write(port, s, count, owl_console_putchar);

	/* wait until all contents have been sent out */
	while (owl_uart_read(port, OWL_UART_STAT) & OWL_UART_STAT_TRFL_MASK)
		cpu_relax();

	/* clear IRQ pending */
	val = owl_uart_read(port, OWL_UART_STAT);
	val |= OWL_UART_STAT_TIP | OWL_UART_STAT_RIP;
	owl_uart_write(port, val, OWL_UART_STAT);

	owl_uart_write(port, old_ctl, OWL_UART_CTL);

	if (locked)
		uart_port_unlock_irqrestore(port, flags);
}

static void owl_uart_console_write(struct console *co, const char *s,
				   u_int count)
{
	struct owl_uart_port *owl_port;

	owl_port = owl_uart_ports[co->index];
	if (!owl_port)
		return;

	owl_uart_port_write(&owl_port->port, s, count);
}

static int owl_uart_console_setup(struct console *co, char *options)
{
	struct owl_uart_port *owl_port;
	int baud = 115200;
	int bits = 8;
	int parity = 'n';
	int flow = 'n';

	if (co->index < 0 || co->index >= OWL_UART_PORT_NUM)
		return -EINVAL;

	owl_port = owl_uart_ports[co->index];
	if (!owl_port || !owl_port->port.membase)
		return -ENODEV;

	if (options)
		uart_parse_options(options, &baud, &parity, &bits, &flow);

	return uart_set_options(&owl_port->port, co, baud, parity, bits, flow);
}

static struct console owl_uart_console = {
	.name = OWL_UART_DEV_NAME,
	.write = owl_uart_console_write,
	.device = uart_console_device,
	.setup = owl_uart_console_setup,
	.flags = CON_PRINTBUFFER,
	.index = -1,
	.data = &owl_uart_driver,
};

static int __init owl_uart_console_init(void)
{
	register_console(&owl_uart_console);

	return 0;
}
console_initcall(owl_uart_console_init);

static void owl_uart_early_console_write(struct console *co,
					 const char *s,
					 u_int count)
{
	struct earlycon_device *dev = co->data;

	owl_uart_port_write(&dev->port, s, count);
}

static int __init
owl_uart_early_console_setup(struct earlycon_device *device, const char *opt)
{
	if (!device->port.membase)
		return -ENODEV;

	device->con->write = owl_uart_early_console_write;

	return 0;
}
OF_EARLYCON_DECLARE(owl, "actions,owl-uart",
		    owl_uart_early_console_setup);

#define OWL_UART_CONSOLE (&owl_uart_console)
#else
#define OWL_UART_CONSOLE NULL
#endif

static struct uart_driver owl_uart_driver = {
	.owner = THIS_MODULE,
	.driver_name = "owl-uart",
	.dev_name = OWL_UART_DEV_NAME,
	.nr = OWL_UART_PORT_NUM,
	.cons = OWL_UART_CONSOLE,
};

static const struct owl_uart_info owl_s500_info = {
	.tx_fifosize = 16,
};

static const struct owl_uart_info owl_s900_info = {
	.tx_fifosize = 32,
};

static const struct of_device_id owl_uart_dt_matches[] = {
	{ .compatible = "actions,s500-uart", .data = &owl_s500_info },
	{ .compatible = "actions,s900-uart", .data = &owl_s900_info },
	{ }
};
MODULE_DEVICE_TABLE(of, owl_uart_dt_matches);

static int owl_uart_probe(struct platform_device *pdev)
{
	const struct of_device_id *match;
	const struct owl_uart_info *info = NULL;
	struct resource *res_mem;
	struct owl_uart_port *owl_port;
	int ret, irq;

	if (pdev->dev.of_node) {
		pdev->id = of_alias_get_id(pdev->dev.of_node, "serial");
		match = of_match_node(owl_uart_dt_matches, pdev->dev.of_node);
		if (match)
			info = match->data;
	}

	if (pdev->id < 0 || pdev->id >= OWL_UART_PORT_NUM) {
		dev_err(&pdev->dev, "id %d out of range\n", pdev->id);
		return -EINVAL;
	}

	res_mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res_mem) {
		dev_err(&pdev->dev, "could not get mem\n");
		return -ENODEV;
	}

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	if (owl_uart_ports[pdev->id]) {
		dev_err(&pdev->dev, "port %d already allocated\n", pdev->id);
		return -EBUSY;
	}

	owl_port = devm_kzalloc(&pdev->dev, sizeof(*owl_port), GFP_KERNEL);
	if (!owl_port)
		return -ENOMEM;

	owl_port->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(owl_port->clk)) {
		dev_err(&pdev->dev, "could not get clk\n");
		return PTR_ERR(owl_port->clk);
	}

	ret = clk_prepare_enable(owl_port->clk);
	if (ret) {
		dev_err(&pdev->dev, "could not enable clk\n");
		return ret;
	}

	owl_port->port.dev = &pdev->dev;
	owl_port->port.line = pdev->id;
	owl_port->port.type = PORT_OWL;
	owl_port->port.iotype = UPIO_MEM;
	owl_port->port.mapbase = res_mem->start;
	owl_port->port.irq = irq;
	owl_port->port.uartclk = clk_get_rate(owl_port->clk);
	if (owl_port->port.uartclk == 0) {
		dev_err(&pdev->dev, "clock rate is zero\n");
		clk_disable_unprepare(owl_port->clk);
		return -EINVAL;
	}
	owl_port->port.flags = UPF_BOOT_AUTOCONF | UPF_IOREMAP | UPF_LOW_LATENCY;
	owl_port->port.x_char = 0;
	owl_port->port.fifosize = (info) ? info->tx_fifosize : 16;
	owl_port->port.ops = &owl_uart_ops;

	owl_uart_ports[pdev->id] = owl_port;
	platform_set_drvdata(pdev, owl_port);

	ret = uart_add_one_port(&owl_uart_driver, &owl_port->port);
	if (ret)
		owl_uart_ports[pdev->id] = NULL;

	return ret;
}

static void owl_uart_remove(struct platform_device *pdev)
{
	struct owl_uart_port *owl_port = platform_get_drvdata(pdev);

	uart_remove_one_port(&owl_uart_driver, &owl_port->port);
	owl_uart_ports[pdev->id] = NULL;
	clk_disable_unprepare(owl_port->clk);
}

static struct platform_driver owl_uart_platform_driver = {
	.probe = owl_uart_probe,
	.remove_new = owl_uart_remove,
	.driver = {
		.name = "owl-uart",
		.of_match_table = owl_uart_dt_matches,
	},
};

static int __init owl_uart_init(void)
{
	int ret;

	ret = uart_register_driver(&owl_uart_driver);
	if (ret)
		return ret;

	ret = platform_driver_register(&owl_uart_platform_driver);
	if (ret)
		uart_unregister_driver(&owl_uart_driver);

	return ret;
}

static void __exit owl_uart_exit(void)
{
	platform_driver_unregister(&owl_uart_platform_driver);
	uart_unregister_driver(&owl_uart_driver);
}

module_init(owl_uart_init);
module_exit(owl_uart_exit);

MODULE_DESCRIPTION("Actions Semi Owl family serial console");
MODULE_LICENSE("GPL");
