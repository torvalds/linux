// SPDX-License-Identifier: GPL-2.0
/*
 * Sunplus SoC UART driver
 *
 * Author: Hammer Hsieh <hammerh0314@gmail.com>
 *
 * Note1: This driver is 8250-like uart, but are not register compatible.
 *
 * Note2: On some buses, for preventing data incoherence, must do a read
 * for ensure write made it to hardware. In this driver, function startup
 * and shutdown did not do a read but only do a write directly. For what?
 * In Sunplus bus communication between memory bus and peripheral bus with
 * posted write, it will send a specific command after last write command
 * to make sure write done. Then memory bus identify the specific command
 * and send done signal back to master device. After master device received
 * done signal, then proceed next write command. It is no need to do a read
 * before write.
 */
#include <linux/clk.h>
#include <linux/console.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/reset.h>
#include <linux/serial_core.h>
#include <linux/serial_reg.h>
#include <linux/sysrq.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <asm/irq.h>

/* Register offsets */
#define SUP_UART_DATA			0x00
#define SUP_UART_LSR			0x04
#define SUP_UART_MSR			0x08
#define SUP_UART_LCR			0x0C
#define SUP_UART_MCR			0x10
#define SUP_UART_DIV_L			0x14
#define SUP_UART_DIV_H			0x18
#define SUP_UART_ISC			0x1C
#define SUP_UART_TX_RESIDUE		0x20
#define SUP_UART_RX_RESIDUE		0x24

/* Line Status Register bits */
#define SUP_UART_LSR_BC			BIT(5) /* break condition status */
#define SUP_UART_LSR_FE			BIT(4) /* frame error status */
#define SUP_UART_LSR_OE			BIT(3) /* overrun error status */
#define SUP_UART_LSR_PE			BIT(2) /* parity error status */
#define SUP_UART_LSR_RX			BIT(1) /* 1: receive fifo not empty */
#define SUP_UART_LSR_TX			BIT(0) /* 1: transmit fifo is not full */
#define SUP_UART_LSR_TX_NOT_FULL	1
#define SUP_UART_LSR_BRK_ERROR_BITS	GENMASK(5, 2)

/* Line Control Register bits */
#define SUP_UART_LCR_SBC		BIT(5) /* select break condition */

/* Modem Control Register bits */
#define SUP_UART_MCR_RI			BIT(3) /* ring indicator */
#define SUP_UART_MCR_DCD		BIT(2) /* data carrier detect */

/* Interrupt Status/Control Register bits */
#define SUP_UART_ISC_RXM		BIT(5) /* RX interrupt enable */
#define SUP_UART_ISC_TXM		BIT(4) /* TX interrupt enable */
#define SUP_UART_ISC_RX			BIT(1) /* RX interrupt status */
#define SUP_UART_ISC_TX			BIT(0) /* TX interrupt status */

#define SUP_DUMMY_READ			BIT(16) /* drop bytes received on a !CREAD port */
#define SUP_UART_NR			5

struct sunplus_uart_port {
	struct uart_port port;
	struct clk *clk;
	struct reset_control *rstc;
};

static void sp_uart_put_char(struct uart_port *port, unsigned int ch)
{
	writel(ch, port->membase + SUP_UART_DATA);
}

static u32 sunplus_tx_buf_not_full(struct uart_port *port)
{
	unsigned int lsr = readl(port->membase + SUP_UART_LSR);

	return (lsr & SUP_UART_LSR_TX) ? SUP_UART_LSR_TX_NOT_FULL : 0;
}

static unsigned int sunplus_tx_empty(struct uart_port *port)
{
	unsigned int lsr = readl(port->membase + SUP_UART_LSR);

	return (lsr & UART_LSR_TEMT) ? TIOCSER_TEMT : 0;
}

static void sunplus_set_mctrl(struct uart_port *port, unsigned int mctrl)
{
	unsigned int mcr = readl(port->membase + SUP_UART_MCR);

	if (mctrl & TIOCM_DTR)
		mcr |= UART_MCR_DTR;
	else
		mcr &= ~UART_MCR_DTR;

	if (mctrl & TIOCM_RTS)
		mcr |= UART_MCR_RTS;
	else
		mcr &= ~UART_MCR_RTS;

	if (mctrl & TIOCM_CAR)
		mcr |= SUP_UART_MCR_DCD;
	else
		mcr &= ~SUP_UART_MCR_DCD;

	if (mctrl & TIOCM_RI)
		mcr |= SUP_UART_MCR_RI;
	else
		mcr &= ~SUP_UART_MCR_RI;

	if (mctrl & TIOCM_LOOP)
		mcr |= UART_MCR_LOOP;
	else
		mcr &= ~UART_MCR_LOOP;

	writel(mcr, port->membase + SUP_UART_MCR);
}

static unsigned int sunplus_get_mctrl(struct uart_port *port)
{
	unsigned int mcr, ret = 0;

	mcr = readl(port->membase + SUP_UART_MCR);

	if (mcr & UART_MCR_DTR)
		ret |= TIOCM_DTR;

	if (mcr & UART_MCR_RTS)
		ret |= TIOCM_RTS;

	if (mcr & SUP_UART_MCR_DCD)
		ret |= TIOCM_CAR;

	if (mcr & SUP_UART_MCR_RI)
		ret |= TIOCM_RI;

	if (mcr & UART_MCR_LOOP)
		ret |= TIOCM_LOOP;

	return ret;
}

static void sunplus_stop_tx(struct uart_port *port)
{
	unsigned int isc;

	isc = readl(port->membase + SUP_UART_ISC);
	isc &= ~SUP_UART_ISC_TXM;
	writel(isc, port->membase + SUP_UART_ISC);
}

static void sunplus_start_tx(struct uart_port *port)
{
	unsigned int isc;

	isc = readl(port->membase + SUP_UART_ISC);
	isc |= SUP_UART_ISC_TXM;
	writel(isc, port->membase + SUP_UART_ISC);
}

static void sunplus_stop_rx(struct uart_port *port)
{
	unsigned int isc;

	isc = readl(port->membase + SUP_UART_ISC);
	isc &= ~SUP_UART_ISC_RXM;
	writel(isc, port->membase + SUP_UART_ISC);
}

static void sunplus_break_ctl(struct uart_port *port, int ctl)
{
	unsigned long flags;
	unsigned int lcr;

	spin_lock_irqsave(&port->lock, flags);

	lcr = readl(port->membase + SUP_UART_LCR);

	if (ctl)
		lcr |= SUP_UART_LCR_SBC; /* start break */
	else
		lcr &= ~SUP_UART_LCR_SBC; /* stop break */

	writel(lcr, port->membase + SUP_UART_LCR);

	spin_unlock_irqrestore(&port->lock, flags);
}

static void transmit_chars(struct uart_port *port)
{
	struct circ_buf *xmit = &port->state->xmit;

	if (port->x_char) {
		sp_uart_put_char(port, port->x_char);
		port->icount.tx++;
		port->x_char = 0;
		return;
	}

	if (uart_circ_empty(xmit) || uart_tx_stopped(port)) {
		sunplus_stop_tx(port);
		return;
	}

	do {
		sp_uart_put_char(port, xmit->buf[xmit->tail]);
		xmit->tail = (xmit->tail + 1) % UART_XMIT_SIZE;
		port->icount.tx++;

		if (uart_circ_empty(xmit))
			break;
	} while (sunplus_tx_buf_not_full(port));

	if (uart_circ_chars_pending(xmit) < WAKEUP_CHARS)
		uart_write_wakeup(port);

	if (uart_circ_empty(xmit))
		sunplus_stop_tx(port);
}

static void receive_chars(struct uart_port *port)
{
	unsigned int lsr = readl(port->membase + SUP_UART_LSR);
	unsigned int ch, flag;

	do {
		ch = readl(port->membase + SUP_UART_DATA);
		flag = TTY_NORMAL;
		port->icount.rx++;

		if (unlikely(lsr & SUP_UART_LSR_BRK_ERROR_BITS)) {
			if (lsr & SUP_UART_LSR_BC) {
				lsr &= ~(SUP_UART_LSR_FE | SUP_UART_LSR_PE);
				port->icount.brk++;
				flag = TTY_BREAK;
				if (uart_handle_break(port))
					goto ignore_char;
			} else if (lsr & SUP_UART_LSR_PE) {
				port->icount.parity++;
				flag = TTY_PARITY;
			} else if (lsr & SUP_UART_LSR_FE) {
				port->icount.frame++;
				flag = TTY_FRAME;
			}

			if (lsr & SUP_UART_LSR_OE)
				port->icount.overrun++;
		}

		if (port->ignore_status_mask & SUP_DUMMY_READ)
			goto ignore_char;

		if (uart_handle_sysrq_char(port, ch))
			goto ignore_char;

		uart_insert_char(port, lsr, SUP_UART_LSR_OE, ch, flag);

ignore_char:
		lsr = readl(port->membase + SUP_UART_LSR);
	} while (lsr & SUP_UART_LSR_RX);

	tty_flip_buffer_push(&port->state->port);
}

static irqreturn_t sunplus_uart_irq(int irq, void *args)
{
	struct uart_port *port = args;
	unsigned int isc;

	spin_lock(&port->lock);

	isc = readl(port->membase + SUP_UART_ISC);

	if (isc & SUP_UART_ISC_RX)
		receive_chars(port);

	if (isc & SUP_UART_ISC_TX)
		transmit_chars(port);

	spin_unlock(&port->lock);

	return IRQ_HANDLED;
}

static int sunplus_startup(struct uart_port *port)
{
	unsigned long flags;
	unsigned int isc = 0;
	int ret;

	ret = request_irq(port->irq, sunplus_uart_irq, 0, "sunplus_uart", port);
	if (ret)
		return ret;

	spin_lock_irqsave(&port->lock, flags);
	/* isc define Bit[7:4] int setting, Bit[3:0] int status
	 * isc register will clean Bit[3:0] int status after read
	 * only do a write to Bit[7:4] int setting
	 */
	isc |= SUP_UART_ISC_RXM;
	writel(isc, port->membase + SUP_UART_ISC);
	spin_unlock_irqrestore(&port->lock, flags);

	return 0;
}

static void sunplus_shutdown(struct uart_port *port)
{
	unsigned long flags;

	spin_lock_irqsave(&port->lock, flags);
	/* isc define Bit[7:4] int setting, Bit[3:0] int status
	 * isc register will clean Bit[3:0] int status after read
	 * only do a write to Bit[7:4] int setting
	 */
	writel(0, port->membase + SUP_UART_ISC); /* disable all interrupt */
	spin_unlock_irqrestore(&port->lock, flags);

	free_irq(port->irq, port);
}

static void sunplus_set_termios(struct uart_port *port,
				struct ktermios *termios,
				struct ktermios *oldtermios)
{
	u32 ext, div, div_l, div_h, baud, lcr;
	u32 clk = port->uartclk;
	unsigned long flags;

	baud = uart_get_baud_rate(port, termios, oldtermios, 0, port->uartclk / 16);

	/* baud rate = uartclk / ((16 * divisor + 1) + divisor_ext) */
	clk += baud >> 1;
	div = clk / baud;
	ext = div & 0x0F;
	div = (div >> 4) - 1;
	div_l = (div & 0xFF) | (ext << 12);
	div_h = div >> 8;

	switch (termios->c_cflag & CSIZE) {
	case CS5:
		lcr = UART_LCR_WLEN5;
		break;
	case CS6:
		lcr = UART_LCR_WLEN6;
		break;
	case CS7:
		lcr = UART_LCR_WLEN7;
		break;
	default:
		lcr = UART_LCR_WLEN8;
		break;
	}

	if (termios->c_cflag & CSTOPB)
		lcr |= UART_LCR_STOP;

	if (termios->c_cflag & PARENB) {
		lcr |= UART_LCR_PARITY;

		if (!(termios->c_cflag & PARODD))
			lcr |= UART_LCR_EPAR;
	}

	spin_lock_irqsave(&port->lock, flags);

	uart_update_timeout(port, termios->c_cflag, baud);

	port->read_status_mask = 0;
	if (termios->c_iflag & INPCK)
		port->read_status_mask |= SUP_UART_LSR_PE | SUP_UART_LSR_FE;

	if (termios->c_iflag & (BRKINT | PARMRK))
		port->read_status_mask |= SUP_UART_LSR_BC;

	/* Characters to ignore */
	port->ignore_status_mask = 0;
	if (termios->c_iflag & IGNPAR)
		port->ignore_status_mask |= SUP_UART_LSR_FE | SUP_UART_LSR_PE;

	if (termios->c_iflag & IGNBRK) {
		port->ignore_status_mask |= SUP_UART_LSR_BC;

		if (termios->c_iflag & IGNPAR)
			port->ignore_status_mask |= SUP_UART_LSR_OE;
	}

	/* Ignore all characters if CREAD is not set */
	if ((termios->c_cflag & CREAD) == 0) {
		port->ignore_status_mask |= SUP_DUMMY_READ;
		/* flush rx data FIFO */
		writel(0, port->membase + SUP_UART_RX_RESIDUE);
	}

	/* Settings for baud rate divisor and lcr */
	writel(div_h, port->membase + SUP_UART_DIV_H);
	writel(div_l, port->membase + SUP_UART_DIV_L);
	writel(lcr, port->membase + SUP_UART_LCR);

	spin_unlock_irqrestore(&port->lock, flags);
}

static void sunplus_set_ldisc(struct uart_port *port, struct ktermios *termios)
{
	int new = termios->c_line;

	if (new == N_PPS)
		port->flags |= UPF_HARDPPS_CD;
	else
		port->flags &= ~UPF_HARDPPS_CD;
}

static const char *sunplus_type(struct uart_port *port)
{
	return port->type == PORT_SUNPLUS ? "sunplus_uart" : NULL;
}

static void sunplus_config_port(struct uart_port *port, int type)
{
	if (type & UART_CONFIG_TYPE)
		port->type = PORT_SUNPLUS;
}

static int sunplus_verify_port(struct uart_port *port, struct serial_struct *ser)
{
	if (ser->type != PORT_UNKNOWN && ser->type != PORT_SUNPLUS)
		return -EINVAL;

	return 0;
}

#if defined(CONFIG_SERIAL_SUNPLUS_CONSOLE) || defined(CONFIG_CONSOLE_POLL)
static void wait_for_xmitr(struct uart_port *port)
{
	unsigned int val;
	int ret;

	/* Wait while FIFO is full or timeout */
	ret = readl_poll_timeout_atomic(port->membase + SUP_UART_LSR, val,
					(val & SUP_UART_LSR_TX), 1, 10000);

	if (ret == -ETIMEDOUT) {
		dev_err(port->dev, "Timeout waiting while UART TX FULL\n");
		return;
	}
}
#endif

#ifdef CONFIG_CONSOLE_POLL
static void sunplus_poll_put_char(struct uart_port *port, unsigned char data)
{
	wait_for_xmitr(port);
	sp_uart_put_char(port, data);
}

static int sunplus_poll_get_char(struct uart_port *port)
{
	unsigned int lsr = readl(port->membase + SUP_UART_LSR);

	if (!(lsr & SUP_UART_LSR_RX))
		return NO_POLL_CHAR;

	return readl(port->membase + SUP_UART_DATA);
}
#endif

static const struct uart_ops sunplus_uart_ops = {
	.tx_empty	= sunplus_tx_empty,
	.set_mctrl	= sunplus_set_mctrl,
	.get_mctrl	= sunplus_get_mctrl,
	.stop_tx	= sunplus_stop_tx,
	.start_tx	= sunplus_start_tx,
	.stop_rx	= sunplus_stop_rx,
	.break_ctl	= sunplus_break_ctl,
	.startup	= sunplus_startup,
	.shutdown	= sunplus_shutdown,
	.set_termios	= sunplus_set_termios,
	.set_ldisc	= sunplus_set_ldisc,
	.type		= sunplus_type,
	.config_port	= sunplus_config_port,
	.verify_port	= sunplus_verify_port,
#ifdef CONFIG_CONSOLE_POLL
	.poll_put_char	= sunplus_poll_put_char,
	.poll_get_char	= sunplus_poll_get_char,
#endif
};

#ifdef CONFIG_SERIAL_SUNPLUS_CONSOLE
struct sunplus_uart_port *sunplus_console_ports[SUP_UART_NR];

static void sunplus_uart_console_putchar(struct uart_port *port,
					 unsigned char ch)
{
	wait_for_xmitr(port);
	sp_uart_put_char(port, ch);
}

static void sunplus_console_write(struct console *co,
				  const char *s,
				  unsigned int count)
{
	unsigned long flags;
	int locked = 1;

	local_irq_save(flags);

	if (sunplus_console_ports[co->index]->port.sysrq)
		locked = 0;
	else if (oops_in_progress)
		locked = spin_trylock(&sunplus_console_ports[co->index]->port.lock);
	else
		spin_lock(&sunplus_console_ports[co->index]->port.lock);

	uart_console_write(&sunplus_console_ports[co->index]->port, s, count,
			   sunplus_uart_console_putchar);

	if (locked)
		spin_unlock(&sunplus_console_ports[co->index]->port.lock);

	local_irq_restore(flags);
}

static int __init sunplus_console_setup(struct console *co, char *options)
{
	struct sunplus_uart_port *sup;
	int baud = 115200;
	int bits = 8;
	int parity = 'n';
	int flow = 'n';

	if (co->index < 0 || co->index >= SUP_UART_NR)
		return -EINVAL;

	sup = sunplus_console_ports[co->index];
	if (!sup)
		return -ENODEV;

	if (options)
		uart_parse_options(options, &baud, &parity, &bits, &flow);

	return uart_set_options(&sup->port, co, baud, parity, bits, flow);
}

static struct uart_driver sunplus_uart_driver;
static struct console sunplus_uart_console = {
	.name		= "ttySUP",
	.write		= sunplus_console_write,
	.device		= uart_console_device,
	.setup		= sunplus_console_setup,
	.flags		= CON_PRINTBUFFER,
	.index		= -1,
	.data		= &sunplus_uart_driver
};

#define	SERIAL_SUNPLUS_CONSOLE	(&sunplus_uart_console)
#else
#define	SERIAL_SUNPLUS_CONSOLE	NULL
#endif

static struct uart_driver sunplus_uart_driver = {
	.owner		= THIS_MODULE,
	.driver_name	= "sunplus_uart",
	.dev_name	= "ttySUP",
	.major		= TTY_MAJOR,
	.minor		= 64,
	.nr		= SUP_UART_NR,
	.cons		= SERIAL_SUNPLUS_CONSOLE,
};

static void sunplus_uart_disable_unprepare(void *data)
{
	clk_disable_unprepare(data);
}

static void sunplus_uart_reset_control_assert(void *data)
{
	reset_control_assert(data);
}

static int sunplus_uart_probe(struct platform_device *pdev)
{
	struct sunplus_uart_port *sup;
	struct uart_port *port;
	struct resource *res;
	int ret, irq;

	pdev->id = of_alias_get_id(pdev->dev.of_node, "serial");

	if (pdev->id < 0 || pdev->id >= SUP_UART_NR)
		return -EINVAL;

	sup = devm_kzalloc(&pdev->dev, sizeof(*sup), GFP_KERNEL);
	if (!sup)
		return -ENOMEM;

	sup->clk = devm_clk_get_optional(&pdev->dev, NULL);
	if (IS_ERR(sup->clk))
		return dev_err_probe(&pdev->dev, PTR_ERR(sup->clk), "clk not found\n");

	ret = clk_prepare_enable(sup->clk);
	if (ret)
		return ret;

	ret = devm_add_action_or_reset(&pdev->dev, sunplus_uart_disable_unprepare, sup->clk);
	if (ret)
		return ret;

	sup->rstc = devm_reset_control_get_exclusive(&pdev->dev, NULL);
	if (IS_ERR(sup->rstc))
		return dev_err_probe(&pdev->dev, PTR_ERR(sup->rstc), "rstc not found\n");

	port = &sup->port;

	port->membase = devm_platform_get_and_ioremap_resource(pdev, 0, &res);
	if (IS_ERR(port->membase))
		return dev_err_probe(&pdev->dev, PTR_ERR(port->membase), "membase not found\n");

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	port->mapbase = res->start;
	port->uartclk = clk_get_rate(sup->clk);
	port->line = pdev->id;
	port->irq = irq;
	port->dev = &pdev->dev;
	port->iotype = UPIO_MEM;
	port->ops = &sunplus_uart_ops;
	port->flags = UPF_BOOT_AUTOCONF;
	port->fifosize = 128;

	ret = reset_control_deassert(sup->rstc);
	if (ret)
		return ret;

	ret = devm_add_action_or_reset(&pdev->dev, sunplus_uart_reset_control_assert, sup->rstc);
	if (ret)
		return ret;

#ifdef CONFIG_SERIAL_SUNPLUS_CONSOLE
	sunplus_console_ports[sup->port.line] = sup;
#endif

	platform_set_drvdata(pdev, &sup->port);

	ret = uart_add_one_port(&sunplus_uart_driver, &sup->port);
#ifdef CONFIG_SERIAL_SUNPLUS_CONSOLE
	if (ret)
		sunplus_console_ports[sup->port.line] = NULL;
#endif

	return ret;
}

static int sunplus_uart_remove(struct platform_device *pdev)
{
	struct sunplus_uart_port *sup = platform_get_drvdata(pdev);

	uart_remove_one_port(&sunplus_uart_driver, &sup->port);

	return 0;
}

static int __maybe_unused sunplus_uart_suspend(struct device *dev)
{
	struct sunplus_uart_port *sup = dev_get_drvdata(dev);

	if (!uart_console(&sup->port))
		uart_suspend_port(&sunplus_uart_driver, &sup->port);

	return 0;
}

static int __maybe_unused sunplus_uart_resume(struct device *dev)
{
	struct sunplus_uart_port *sup = dev_get_drvdata(dev);

	if (!uart_console(&sup->port))
		uart_resume_port(&sunplus_uart_driver, &sup->port);

	return 0;
}

static const struct dev_pm_ops sunplus_uart_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(sunplus_uart_suspend, sunplus_uart_resume)
};

static const struct of_device_id sp_uart_of_match[] = {
	{ .compatible = "sunplus,sp7021-uart" },
	{}
};
MODULE_DEVICE_TABLE(of, sp_uart_of_match);

static struct platform_driver sunplus_uart_platform_driver = {
	.probe		= sunplus_uart_probe,
	.remove		= sunplus_uart_remove,
	.driver = {
		.name	= "sunplus_uart",
		.of_match_table = sp_uart_of_match,
		.pm     = &sunplus_uart_pm_ops,
	}
};

static int __init sunplus_uart_init(void)
{
	int ret;

	ret = uart_register_driver(&sunplus_uart_driver);
	if (ret)
		return ret;

	ret = platform_driver_register(&sunplus_uart_platform_driver);
	if (ret)
		uart_unregister_driver(&sunplus_uart_driver);

	return ret;
}
module_init(sunplus_uart_init);

static void __exit sunplus_uart_exit(void)
{
	platform_driver_unregister(&sunplus_uart_platform_driver);
	uart_unregister_driver(&sunplus_uart_driver);
}
module_exit(sunplus_uart_exit);

#ifdef CONFIG_SERIAL_EARLYCON
static void sunplus_uart_putc(struct uart_port *port, unsigned char c)
{
	unsigned int val;
	int ret;

	ret = readl_poll_timeout_atomic(port->membase + SUP_UART_LSR, val,
					(val & UART_LSR_TEMT), 1, 10000);
	if (ret)
		return;

	writel(c, port->membase + SUP_UART_DATA);
}

static void sunplus_uart_early_write(struct console *con, const char *s, unsigned int n)
{
	struct earlycon_device *dev = con->data;

	uart_console_write(&dev->port, s, n, sunplus_uart_putc);
}

static int __init
sunplus_uart_early_setup(struct earlycon_device *dev, const char *opt)
{
	if (!(dev->port.membase || dev->port.iobase))
		return -ENODEV;

	dev->con->write = sunplus_uart_early_write;

	return 0;
}
OF_EARLYCON_DECLARE(sunplus_uart, "sunplus,sp7021-uart", sunplus_uart_early_setup);
#endif

MODULE_DESCRIPTION("Sunplus UART driver");
MODULE_AUTHOR("Hammer Hsieh <hammerh0314@gmail.com>");
MODULE_LICENSE("GPL v2");
