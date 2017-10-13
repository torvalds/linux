/*
* ***************************************************************************
* Marvell Armada-3700 Serial Driver
* Author: Wilson Ding <dingwei@marvell.com>
* Copyright (C) 2015 Marvell International Ltd.
* ***************************************************************************
* This program is free software: you can redistribute it and/or modify it
* under the terms of the GNU General Public License as published by the Free
* Software Foundation, either version 2 of the License, or any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program.  If not, see <http://www.gnu.org/licenses/>.
* ***************************************************************************
*/

#include <linux/clk.h>
#include <linux/console.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/serial.h>
#include <linux/serial_core.h>
#include <linux/slab.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>

/* Register Map */
#define UART_STD_RBR		0x00

#define UART_STD_TSH		0x04

#define UART_STD_CTRL1		0x08
#define  CTRL_SOFT_RST		BIT(31)
#define  CTRL_TXFIFO_RST	BIT(15)
#define  CTRL_RXFIFO_RST	BIT(14)
#define  CTRL_SND_BRK_SEQ	BIT(11)
#define  CTRL_BRK_DET_INT	BIT(3)
#define  CTRL_FRM_ERR_INT	BIT(2)
#define  CTRL_PAR_ERR_INT	BIT(1)
#define  CTRL_OVR_ERR_INT	BIT(0)
#define  CTRL_BRK_INT		(CTRL_BRK_DET_INT | CTRL_FRM_ERR_INT | \
				CTRL_PAR_ERR_INT | CTRL_OVR_ERR_INT)

#define UART_STD_CTRL2		UART_STD_CTRL1
#define  CTRL_STD_TX_RDY_INT	BIT(5)
#define  CTRL_STD_RX_RDY_INT	BIT(4)

#define UART_STAT		0x0C
#define  STAT_TX_FIFO_EMP	BIT(13)
#define  STAT_TX_FIFO_FUL	BIT(11)
#define  STAT_TX_EMP		BIT(6)
#define  STAT_STD_TX_RDY	BIT(5)
#define  STAT_STD_RX_RDY	BIT(4)
#define  STAT_BRK_DET		BIT(3)
#define  STAT_FRM_ERR		BIT(2)
#define  STAT_PAR_ERR		BIT(1)
#define  STAT_OVR_ERR		BIT(0)
#define  STAT_BRK_ERR		(STAT_BRK_DET | STAT_FRM_ERR | STAT_FRM_ERR\
				 | STAT_PAR_ERR | STAT_OVR_ERR)

#define UART_BRDV		0x10
#define  BRDV_BAUD_MASK         0x3FF

#define MVEBU_NR_UARTS		1

#define MVEBU_UART_TYPE		"mvebu-uart"
#define DRIVER_NAME		"mvebu_serial"

/* Register offsets, different depending on the UART */
struct uart_regs_layout {
	unsigned int rbr;
	unsigned int tsh;
	unsigned int ctrl;
	unsigned int intr;
};

/* Diverging flags */
struct uart_flags {
	unsigned int ctrl_tx_rdy_int;
	unsigned int ctrl_rx_rdy_int;
	unsigned int stat_tx_rdy;
	unsigned int stat_rx_rdy;
};

/* Driver data, a structure for each UART port */
struct mvebu_uart_driver_data {
	bool is_ext;
	struct uart_regs_layout regs;
	struct uart_flags flags;
};

/* MVEBU UART driver structure */
struct mvebu_uart {
	struct uart_port *port;
	struct clk *clk;
	struct mvebu_uart_driver_data *data;
};

static struct mvebu_uart *to_mvuart(struct uart_port *port)
{
	return (struct mvebu_uart *)port->private_data;
}

#define IS_EXTENDED(port) (to_mvuart(port)->data->is_ext)

#define UART_RBR(port) (to_mvuart(port)->data->regs.rbr)
#define UART_TSH(port) (to_mvuart(port)->data->regs.tsh)
#define UART_CTRL(port) (to_mvuart(port)->data->regs.ctrl)
#define UART_INTR(port) (to_mvuart(port)->data->regs.intr)

#define CTRL_TX_RDY_INT(port) (to_mvuart(port)->data->flags.ctrl_tx_rdy_int)
#define CTRL_RX_RDY_INT(port) (to_mvuart(port)->data->flags.ctrl_rx_rdy_int)
#define STAT_TX_RDY(port) (to_mvuart(port)->data->flags.stat_tx_rdy)
#define STAT_RX_RDY(port) (to_mvuart(port)->data->flags.stat_rx_rdy)

static struct uart_port mvebu_uart_ports[MVEBU_NR_UARTS];

/* Core UART Driver Operations */
static unsigned int mvebu_uart_tx_empty(struct uart_port *port)
{
	unsigned long flags;
	unsigned int st;

	spin_lock_irqsave(&port->lock, flags);
	st = readl(port->membase + UART_STAT);
	spin_unlock_irqrestore(&port->lock, flags);

	return (st & STAT_TX_FIFO_EMP) ? TIOCSER_TEMT : 0;
}

static unsigned int mvebu_uart_get_mctrl(struct uart_port *port)
{
	return TIOCM_CTS | TIOCM_DSR | TIOCM_CAR;
}

static void mvebu_uart_set_mctrl(struct uart_port *port,
				 unsigned int mctrl)
{
/*
 * Even if we do not support configuring the modem control lines, this
 * function must be proided to the serial core
 */
}

static void mvebu_uart_stop_tx(struct uart_port *port)
{
	unsigned int ctl = readl(port->membase + UART_INTR(port));

	ctl &= ~CTRL_TX_RDY_INT(port);
	writel(ctl, port->membase + UART_INTR(port));
}

static void mvebu_uart_start_tx(struct uart_port *port)
{
	unsigned int ctl;
	struct circ_buf *xmit = &port->state->xmit;

	if (IS_EXTENDED(port) && !uart_circ_empty(xmit)) {
		writel(xmit->buf[xmit->tail], port->membase + UART_TSH(port));
		xmit->tail = (xmit->tail + 1) & (UART_XMIT_SIZE - 1);
		port->icount.tx++;
	}

	ctl = readl(port->membase + UART_INTR(port));
	ctl |= CTRL_TX_RDY_INT(port);
	writel(ctl, port->membase + UART_INTR(port));
}

static void mvebu_uart_stop_rx(struct uart_port *port)
{
	unsigned int ctl;

	ctl = readl(port->membase + UART_CTRL(port));
	ctl &= ~CTRL_BRK_INT;
	writel(ctl, port->membase + UART_CTRL(port));

	ctl = readl(port->membase + UART_INTR(port));
	ctl &= ~CTRL_RX_RDY_INT(port);
	writel(ctl, port->membase + UART_INTR(port));
}

static void mvebu_uart_break_ctl(struct uart_port *port, int brk)
{
	unsigned int ctl;
	unsigned long flags;

	spin_lock_irqsave(&port->lock, flags);
	ctl = readl(port->membase + UART_CTRL(port));
	if (brk == -1)
		ctl |= CTRL_SND_BRK_SEQ;
	else
		ctl &= ~CTRL_SND_BRK_SEQ;
	writel(ctl, port->membase + UART_CTRL(port));
	spin_unlock_irqrestore(&port->lock, flags);
}

static void mvebu_uart_rx_chars(struct uart_port *port, unsigned int status)
{
	struct tty_port *tport = &port->state->port;
	unsigned char ch = 0;
	char flag = 0;

	do {
		if (status & STAT_RX_RDY(port)) {
			ch = readl(port->membase + UART_RBR(port));
			ch &= 0xff;
			flag = TTY_NORMAL;
			port->icount.rx++;

			if (status & STAT_PAR_ERR)
				port->icount.parity++;
		}

		if (status & STAT_BRK_DET) {
			port->icount.brk++;
			status &= ~(STAT_FRM_ERR | STAT_PAR_ERR);
			if (uart_handle_break(port))
				goto ignore_char;
		}

		if (status & STAT_OVR_ERR)
			port->icount.overrun++;

		if (status & STAT_FRM_ERR)
			port->icount.frame++;

		if (uart_handle_sysrq_char(port, ch))
			goto ignore_char;

		if (status & port->ignore_status_mask & STAT_PAR_ERR)
			status &= ~STAT_RX_RDY(port);

		status &= port->read_status_mask;

		if (status & STAT_PAR_ERR)
			flag = TTY_PARITY;

		status &= ~port->ignore_status_mask;

		if (status & STAT_RX_RDY(port))
			tty_insert_flip_char(tport, ch, flag);

		if (status & STAT_BRK_DET)
			tty_insert_flip_char(tport, 0, TTY_BREAK);

		if (status & STAT_FRM_ERR)
			tty_insert_flip_char(tport, 0, TTY_FRAME);

		if (status & STAT_OVR_ERR)
			tty_insert_flip_char(tport, 0, TTY_OVERRUN);

ignore_char:
		status = readl(port->membase + UART_STAT);
	} while (status & (STAT_RX_RDY(port) | STAT_BRK_DET));

	tty_flip_buffer_push(tport);
}

static void mvebu_uart_tx_chars(struct uart_port *port, unsigned int status)
{
	struct circ_buf *xmit = &port->state->xmit;
	unsigned int count;
	unsigned int st;

	if (port->x_char) {
		writel(port->x_char, port->membase + UART_TSH(port));
		port->icount.tx++;
		port->x_char = 0;
		return;
	}

	if (uart_circ_empty(xmit) || uart_tx_stopped(port)) {
		mvebu_uart_stop_tx(port);
		return;
	}

	for (count = 0; count < port->fifosize; count++) {
		writel(xmit->buf[xmit->tail], port->membase + UART_TSH(port));
		xmit->tail = (xmit->tail + 1) & (UART_XMIT_SIZE - 1);
		port->icount.tx++;

		if (uart_circ_empty(xmit))
			break;

		st = readl(port->membase + UART_STAT);
		if (st & STAT_TX_FIFO_FUL)
			break;
	}

	if (uart_circ_chars_pending(xmit) < WAKEUP_CHARS)
		uart_write_wakeup(port);

	if (uart_circ_empty(xmit))
		mvebu_uart_stop_tx(port);
}

static irqreturn_t mvebu_uart_isr(int irq, void *dev_id)
{
	struct uart_port *port = (struct uart_port *)dev_id;
	unsigned int st = readl(port->membase + UART_STAT);

	if (st & (STAT_RX_RDY(port) | STAT_OVR_ERR | STAT_FRM_ERR |
			STAT_BRK_DET))
		mvebu_uart_rx_chars(port, st);

	if (st & STAT_TX_RDY(port))
		mvebu_uart_tx_chars(port, st);

	return IRQ_HANDLED;
}

static int mvebu_uart_startup(struct uart_port *port)
{
	unsigned int ctl;
	int ret;

	writel(CTRL_TXFIFO_RST | CTRL_RXFIFO_RST,
	       port->membase + UART_CTRL(port));
	udelay(1);

	/* Clear the error bits of state register before IRQ request */
	ret = readl(port->membase + UART_STAT);
	ret |= STAT_BRK_ERR;
	writel(ret, port->membase + UART_STAT);

	writel(CTRL_BRK_INT, port->membase + UART_CTRL(port));

	ctl = readl(port->membase + UART_INTR(port));
	ctl |= CTRL_RX_RDY_INT(port);
	writel(ctl, port->membase + UART_INTR(port));

	ret = request_irq(port->irq, mvebu_uart_isr, port->irqflags,
			  DRIVER_NAME, port);
	if (ret) {
		dev_err(port->dev, "failed to request irq\n");
		return ret;
	}

	return 0;
}

static void mvebu_uart_shutdown(struct uart_port *port)
{
	writel(0, port->membase + UART_INTR(port));

	free_irq(port->irq, port);
}

static int mvebu_uart_baud_rate_set(struct uart_port *port, unsigned int baud)
{
	struct mvebu_uart *mvuart = to_mvuart(port);
	unsigned int baud_rate_div;
	u32 brdv;

	if (IS_ERR(mvuart->clk))
		return -PTR_ERR(mvuart->clk);

	/*
	 * The UART clock is divided by the value of the divisor to generate
	 * UCLK_OUT clock, which is 16 times faster than the baudrate.
	 * This prescaler can achieve all standard baudrates until 230400.
	 * Higher baudrates could be achieved for the extended UART by using the
	 * programmable oversampling stack (also called fractional divisor).
	 */
	baud_rate_div = DIV_ROUND_UP(port->uartclk, baud * 16);
	brdv = readl(port->membase + UART_BRDV);
	brdv &= ~BRDV_BAUD_MASK;
	brdv |= baud_rate_div;
	writel(brdv, port->membase + UART_BRDV);

	return 0;
}

static void mvebu_uart_set_termios(struct uart_port *port,
				   struct ktermios *termios,
				   struct ktermios *old)
{
	unsigned long flags;
	unsigned int baud;

	spin_lock_irqsave(&port->lock, flags);

	port->read_status_mask = STAT_RX_RDY(port) | STAT_OVR_ERR |
		STAT_TX_RDY(port) | STAT_TX_FIFO_FUL;

	if (termios->c_iflag & INPCK)
		port->read_status_mask |= STAT_FRM_ERR | STAT_PAR_ERR;

	port->ignore_status_mask = 0;
	if (termios->c_iflag & IGNPAR)
		port->ignore_status_mask |=
			STAT_FRM_ERR | STAT_PAR_ERR | STAT_OVR_ERR;

	if ((termios->c_cflag & CREAD) == 0)
		port->ignore_status_mask |= STAT_RX_RDY(port) | STAT_BRK_ERR;

	/*
	 * Maximum achievable frequency with simple baudrate divisor is 230400.
	 * Since the error per bit frame would be of more than 15%, achieving
	 * higher frequencies would require to implement the fractional divisor
	 * feature.
	 */
	baud = uart_get_baud_rate(port, termios, old, 0, 230400);
	if (mvebu_uart_baud_rate_set(port, baud)) {
		/* No clock available, baudrate cannot be changed */
		if (old)
			baud = uart_get_baud_rate(port, old, NULL, 0, 230400);
	} else {
		tty_termios_encode_baud_rate(termios, baud, baud);
		uart_update_timeout(port, termios->c_cflag, baud);
	}

	/* Only the following flag changes are supported */
	if (old) {
		termios->c_iflag &= INPCK | IGNPAR;
		termios->c_iflag |= old->c_iflag & ~(INPCK | IGNPAR);
		termios->c_cflag &= CREAD | CBAUD;
		termios->c_cflag |= old->c_cflag & ~(CREAD | CBAUD);
		termios->c_lflag = old->c_lflag;
	}

	spin_unlock_irqrestore(&port->lock, flags);
}

static const char *mvebu_uart_type(struct uart_port *port)
{
	return MVEBU_UART_TYPE;
}

static void mvebu_uart_release_port(struct uart_port *port)
{
	/* Nothing to do here */
}

static int mvebu_uart_request_port(struct uart_port *port)
{
	return 0;
}

#ifdef CONFIG_CONSOLE_POLL
static int mvebu_uart_get_poll_char(struct uart_port *port)
{
	unsigned int st = readl(port->membase + UART_STAT);

	if (!(st & STAT_RX_RDY(port)))
		return NO_POLL_CHAR;

	return readl(port->membase + UART_RBR(port));
}

static void mvebu_uart_put_poll_char(struct uart_port *port, unsigned char c)
{
	unsigned int st;

	for (;;) {
		st = readl(port->membase + UART_STAT);

		if (!(st & STAT_TX_FIFO_FUL))
			break;

		udelay(1);
	}

	writel(c, port->membase + UART_TSH(port));
}
#endif

static const struct uart_ops mvebu_uart_ops = {
	.tx_empty	= mvebu_uart_tx_empty,
	.set_mctrl	= mvebu_uart_set_mctrl,
	.get_mctrl	= mvebu_uart_get_mctrl,
	.stop_tx	= mvebu_uart_stop_tx,
	.start_tx	= mvebu_uart_start_tx,
	.stop_rx	= mvebu_uart_stop_rx,
	.break_ctl	= mvebu_uart_break_ctl,
	.startup	= mvebu_uart_startup,
	.shutdown	= mvebu_uart_shutdown,
	.set_termios	= mvebu_uart_set_termios,
	.type		= mvebu_uart_type,
	.release_port	= mvebu_uart_release_port,
	.request_port	= mvebu_uart_request_port,
#ifdef CONFIG_CONSOLE_POLL
	.poll_get_char	= mvebu_uart_get_poll_char,
	.poll_put_char	= mvebu_uart_put_poll_char,
#endif
};

/* Console Driver Operations  */

#ifdef CONFIG_SERIAL_MVEBU_CONSOLE
/* Early Console */
static void mvebu_uart_putc(struct uart_port *port, int c)
{
	unsigned int st;

	for (;;) {
		st = readl(port->membase + UART_STAT);
		if (!(st & STAT_TX_FIFO_FUL))
			break;
	}

	/* At early stage, DT is not parsed yet, only use UART0 */
	writel(c, port->membase + UART_STD_TSH);

	for (;;) {
		st = readl(port->membase + UART_STAT);
		if (st & STAT_TX_FIFO_EMP)
			break;
	}
}

static void mvebu_uart_putc_early_write(struct console *con,
					const char *s,
					unsigned n)
{
	struct earlycon_device *dev = con->data;

	uart_console_write(&dev->port, s, n, mvebu_uart_putc);
}

static int __init
mvebu_uart_early_console_setup(struct earlycon_device *device,
			       const char *opt)
{
	if (!device->port.membase)
		return -ENODEV;

	device->con->write = mvebu_uart_putc_early_write;

	return 0;
}

EARLYCON_DECLARE(ar3700_uart, mvebu_uart_early_console_setup);
OF_EARLYCON_DECLARE(ar3700_uart, "marvell,armada-3700-uart",
		    mvebu_uart_early_console_setup);

static void wait_for_xmitr(struct uart_port *port)
{
	u32 val;

	readl_poll_timeout_atomic(port->membase + UART_STAT, val,
				  (val & STAT_TX_EMP), 1, 10000);
}

static void mvebu_uart_console_putchar(struct uart_port *port, int ch)
{
	wait_for_xmitr(port);
	writel(ch, port->membase + UART_TSH(port));
}

static void mvebu_uart_console_write(struct console *co, const char *s,
				     unsigned int count)
{
	struct uart_port *port = &mvebu_uart_ports[co->index];
	unsigned long flags;
	unsigned int ier, intr, ctl;
	int locked = 1;

	if (oops_in_progress)
		locked = spin_trylock_irqsave(&port->lock, flags);
	else
		spin_lock_irqsave(&port->lock, flags);

	ier = readl(port->membase + UART_CTRL(port)) & CTRL_BRK_INT;
	intr = readl(port->membase + UART_INTR(port)) &
		(CTRL_RX_RDY_INT(port) | CTRL_TX_RDY_INT(port));
	writel(0, port->membase + UART_CTRL(port));
	writel(0, port->membase + UART_INTR(port));

	uart_console_write(port, s, count, mvebu_uart_console_putchar);

	wait_for_xmitr(port);

	if (ier)
		writel(ier, port->membase + UART_CTRL(port));

	if (intr) {
		ctl = intr | readl(port->membase + UART_INTR(port));
		writel(ctl, port->membase + UART_INTR(port));
	}

	if (locked)
		spin_unlock_irqrestore(&port->lock, flags);
}

static int mvebu_uart_console_setup(struct console *co, char *options)
{
	struct uart_port *port;
	int baud = 9600;
	int bits = 8;
	int parity = 'n';
	int flow = 'n';

	if (co->index < 0 || co->index >= MVEBU_NR_UARTS)
		return -EINVAL;

	port = &mvebu_uart_ports[co->index];

	if (!port->mapbase || !port->membase) {
		pr_debug("console on ttyMV%i not present\n", co->index);
		return -ENODEV;
	}

	if (options)
		uart_parse_options(options, &baud, &parity, &bits, &flow);

	return uart_set_options(port, co, baud, parity, bits, flow);
}

static struct uart_driver mvebu_uart_driver;

static struct console mvebu_uart_console = {
	.name	= "ttyMV",
	.write	= mvebu_uart_console_write,
	.device	= uart_console_device,
	.setup	= mvebu_uart_console_setup,
	.flags	= CON_PRINTBUFFER,
	.index	= -1,
	.data	= &mvebu_uart_driver,
};

static int __init mvebu_uart_console_init(void)
{
	register_console(&mvebu_uart_console);
	return 0;
}

console_initcall(mvebu_uart_console_init);


#endif /* CONFIG_SERIAL_MVEBU_CONSOLE */

static struct uart_driver mvebu_uart_driver = {
	.owner			= THIS_MODULE,
	.driver_name		= DRIVER_NAME,
	.dev_name		= "ttyMV",
	.nr			= MVEBU_NR_UARTS,
#ifdef CONFIG_SERIAL_MVEBU_CONSOLE
	.cons			= &mvebu_uart_console,
#endif
};

static const struct of_device_id mvebu_uart_of_match[];

/* Counter to keep track of each UART port id when not using CONFIG_OF */
static int uart_num_counter;

static int mvebu_uart_probe(struct platform_device *pdev)
{
	struct resource *reg = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	struct resource *irq = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	const struct of_device_id *match = of_match_device(mvebu_uart_of_match,
							   &pdev->dev);
	struct uart_port *port;
	struct mvebu_uart *mvuart;
	int ret, id;

	if (!reg || !irq) {
		dev_err(&pdev->dev, "no registers/irq defined\n");
		return -EINVAL;
	}

	/* Assume that all UART ports have a DT alias or none has */
	id = of_alias_get_id(pdev->dev.of_node, "serial");
	if (!pdev->dev.of_node || id < 0)
		pdev->id = uart_num_counter++;
	else
		pdev->id = id;

	if (pdev->id >= MVEBU_NR_UARTS) {
		dev_err(&pdev->dev, "cannot have more than %d UART ports\n",
			MVEBU_NR_UARTS);
		return -EINVAL;
	}

	port = &mvebu_uart_ports[pdev->id];

	spin_lock_init(&port->lock);

	port->dev        = &pdev->dev;
	port->type       = PORT_MVEBU;
	port->ops        = &mvebu_uart_ops;
	port->regshift   = 0;

	port->fifosize   = 32;
	port->iotype     = UPIO_MEM32;
	port->flags      = UPF_FIXED_PORT;
	port->line       = pdev->id;

	port->irq        = irq->start;
	port->irqflags   = 0;
	port->mapbase    = reg->start;

	port->membase = devm_ioremap_resource(&pdev->dev, reg);
	if (IS_ERR(port->membase))
		return -PTR_ERR(port->membase);

	mvuart = devm_kzalloc(&pdev->dev, sizeof(struct mvebu_uart),
			      GFP_KERNEL);
	if (!mvuart)
		return -ENOMEM;

	/* Get controller data depending on the compatible string */
	mvuart->data = (struct mvebu_uart_driver_data *)match->data;
	mvuart->port = port;

	port->private_data = mvuart;
	platform_set_drvdata(pdev, mvuart);

	/* Get fixed clock frequency */
	mvuart->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(mvuart->clk)) {
		if (PTR_ERR(mvuart->clk) == -EPROBE_DEFER)
			return PTR_ERR(mvuart->clk);

		if (IS_EXTENDED(port)) {
			dev_err(&pdev->dev, "unable to get UART clock\n");
			return PTR_ERR(mvuart->clk);
		}
	} else {
		if (!clk_prepare_enable(mvuart->clk))
			port->uartclk = clk_get_rate(mvuart->clk);
	}

	/* UART Soft Reset*/
	writel(CTRL_SOFT_RST, port->membase + UART_CTRL(port));
	udelay(1);
	writel(0, port->membase + UART_CTRL(port));

	ret = uart_add_one_port(&mvebu_uart_driver, port);
	if (ret)
		return ret;
	return 0;
}

static struct mvebu_uart_driver_data uart_std_driver_data = {
	.is_ext = false,
	.regs.rbr = UART_STD_RBR,
	.regs.tsh = UART_STD_TSH,
	.regs.ctrl = UART_STD_CTRL1,
	.regs.intr = UART_STD_CTRL2,
	.flags.ctrl_tx_rdy_int = CTRL_STD_TX_RDY_INT,
	.flags.ctrl_rx_rdy_int = CTRL_STD_RX_RDY_INT,
	.flags.stat_tx_rdy = STAT_STD_TX_RDY,
	.flags.stat_rx_rdy = STAT_STD_RX_RDY,
};

/* Match table for of_platform binding */
static const struct of_device_id mvebu_uart_of_match[] = {
	{
		.compatible = "marvell,armada-3700-uart",
		.data = (void *)&uart_std_driver_data,
	},
	{}
};

static struct platform_driver mvebu_uart_platform_driver = {
	.probe	= mvebu_uart_probe,
	.driver	= {
		.name  = "mvebu-uart",
		.of_match_table = of_match_ptr(mvebu_uart_of_match),
		.suppress_bind_attrs = true,
	},
};

static int __init mvebu_uart_init(void)
{
	int ret;

	ret = uart_register_driver(&mvebu_uart_driver);
	if (ret)
		return ret;

	ret = platform_driver_register(&mvebu_uart_platform_driver);
	if (ret)
		uart_unregister_driver(&mvebu_uart_driver);

	return ret;
}
arch_initcall(mvebu_uart_init);
