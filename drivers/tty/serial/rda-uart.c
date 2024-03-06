// SPDX-License-Identifier: GPL-2.0+
/*
 * RDA8810PL serial device driver
 *
 * Copyright RDA Microelectronics Company Limited
 * Copyright (c) 2017 Andreas Färber
 * Copyright (c) 2018 Manivannan Sadhasivam
 */

#include <linux/clk.h>
#include <linux/console.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/serial.h>
#include <linux/serial_core.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>

#define RDA_UART_PORT_NUM 3
#define RDA_UART_DEV_NAME "ttyRDA"

#define RDA_UART_CTRL		0x00
#define RDA_UART_STATUS		0x04
#define RDA_UART_RXTX_BUFFER	0x08
#define RDA_UART_IRQ_MASK	0x0c
#define RDA_UART_IRQ_CAUSE	0x10
#define RDA_UART_IRQ_TRIGGERS	0x14
#define RDA_UART_CMD_SET	0x18
#define RDA_UART_CMD_CLR	0x1c

/* UART_CTRL Bits */
#define RDA_UART_ENABLE			BIT(0)
#define RDA_UART_DBITS_8		BIT(1)
#define RDA_UART_TX_SBITS_2		BIT(2)
#define RDA_UART_PARITY_EN		BIT(3)
#define RDA_UART_PARITY(x)		(((x) & 0x3) << 4)
#define RDA_UART_PARITY_ODD		RDA_UART_PARITY(0)
#define RDA_UART_PARITY_EVEN		RDA_UART_PARITY(1)
#define RDA_UART_PARITY_SPACE		RDA_UART_PARITY(2)
#define RDA_UART_PARITY_MARK		RDA_UART_PARITY(3)
#define RDA_UART_DIV_MODE		BIT(20)
#define RDA_UART_IRDA_EN		BIT(21)
#define RDA_UART_DMA_EN			BIT(22)
#define RDA_UART_FLOW_CNT_EN		BIT(23)
#define RDA_UART_LOOP_BACK_EN		BIT(24)
#define RDA_UART_RX_LOCK_ERR		BIT(25)
#define RDA_UART_RX_BREAK_LEN(x)	(((x) & 0xf) << 28)

/* UART_STATUS Bits */
#define RDA_UART_RX_FIFO(x)		(((x) & 0x7f) << 0)
#define RDA_UART_RX_FIFO_MASK		(0x7f << 0)
#define RDA_UART_TX_FIFO(x)		(((x) & 0x1f) << 8)
#define RDA_UART_TX_FIFO_MASK		(0x1f << 8)
#define RDA_UART_TX_ACTIVE		BIT(14)
#define RDA_UART_RX_ACTIVE		BIT(15)
#define RDA_UART_RX_OVERFLOW_ERR	BIT(16)
#define RDA_UART_TX_OVERFLOW_ERR	BIT(17)
#define RDA_UART_RX_PARITY_ERR		BIT(18)
#define RDA_UART_RX_FRAMING_ERR		BIT(19)
#define RDA_UART_RX_BREAK_INT		BIT(20)
#define RDA_UART_DCTS			BIT(24)
#define RDA_UART_CTS			BIT(25)
#define RDA_UART_DTR			BIT(28)
#define RDA_UART_CLK_ENABLED		BIT(31)

/* UART_RXTX_BUFFER Bits */
#define RDA_UART_RX_DATA(x)		(((x) & 0xff) << 0)
#define RDA_UART_TX_DATA(x)		(((x) & 0xff) << 0)

/* UART_IRQ_MASK Bits */
#define RDA_UART_TX_MODEM_STATUS	BIT(0)
#define RDA_UART_RX_DATA_AVAILABLE	BIT(1)
#define RDA_UART_TX_DATA_NEEDED		BIT(2)
#define RDA_UART_RX_TIMEOUT		BIT(3)
#define RDA_UART_RX_LINE_ERR		BIT(4)
#define RDA_UART_TX_DMA_DONE		BIT(5)
#define RDA_UART_RX_DMA_DONE		BIT(6)
#define RDA_UART_RX_DMA_TIMEOUT		BIT(7)
#define RDA_UART_DTR_RISE		BIT(8)
#define RDA_UART_DTR_FALL		BIT(9)

/* UART_IRQ_CAUSE Bits */
#define RDA_UART_TX_MODEM_STATUS_U	BIT(16)
#define RDA_UART_RX_DATA_AVAILABLE_U	BIT(17)
#define RDA_UART_TX_DATA_NEEDED_U	BIT(18)
#define RDA_UART_RX_TIMEOUT_U		BIT(19)
#define RDA_UART_RX_LINE_ERR_U		BIT(20)
#define RDA_UART_TX_DMA_DONE_U		BIT(21)
#define RDA_UART_RX_DMA_DONE_U		BIT(22)
#define RDA_UART_RX_DMA_TIMEOUT_U	BIT(23)
#define RDA_UART_DTR_RISE_U		BIT(24)
#define RDA_UART_DTR_FALL_U		BIT(25)

/* UART_TRIGGERS Bits */
#define RDA_UART_RX_TRIGGER(x)		(((x) & 0x1f) << 0)
#define RDA_UART_TX_TRIGGER(x)		(((x) & 0xf) << 8)
#define RDA_UART_AFC_LEVEL(x)		(((x) & 0x1f) << 16)

/* UART_CMD_SET Bits */
#define RDA_UART_RI			BIT(0)
#define RDA_UART_DCD			BIT(1)
#define RDA_UART_DSR			BIT(2)
#define RDA_UART_TX_BREAK_CONTROL	BIT(3)
#define RDA_UART_TX_FINISH_N_WAIT	BIT(4)
#define RDA_UART_RTS			BIT(5)
#define RDA_UART_RX_FIFO_RESET		BIT(6)
#define RDA_UART_TX_FIFO_RESET		BIT(7)

#define RDA_UART_TX_FIFO_SIZE	16

static struct uart_driver rda_uart_driver;

struct rda_uart_port {
	struct uart_port port;
	struct clk *clk;
};

#define to_rda_uart_port(port) container_of(port, struct rda_uart_port, port)

static struct rda_uart_port *rda_uart_ports[RDA_UART_PORT_NUM];

static inline void rda_uart_write(struct uart_port *port, u32 val,
				  unsigned int off)
{
	writel(val, port->membase + off);
}

static inline u32 rda_uart_read(struct uart_port *port, unsigned int off)
{
	return readl(port->membase + off);
}

static unsigned int rda_uart_tx_empty(struct uart_port *port)
{
	unsigned long flags;
	unsigned int ret;
	u32 val;

	uart_port_lock_irqsave(port, &flags);

	val = rda_uart_read(port, RDA_UART_STATUS);
	ret = (val & RDA_UART_TX_FIFO_MASK) ? TIOCSER_TEMT : 0;

	uart_port_unlock_irqrestore(port, flags);

	return ret;
}

static unsigned int rda_uart_get_mctrl(struct uart_port *port)
{
	unsigned int mctrl = 0;
	u32 cmd_set, status;

	cmd_set = rda_uart_read(port, RDA_UART_CMD_SET);
	status = rda_uart_read(port, RDA_UART_STATUS);
	if (cmd_set & RDA_UART_RTS)
		mctrl |= TIOCM_RTS;
	if (!(status & RDA_UART_CTS))
		mctrl |= TIOCM_CTS;

	return mctrl;
}

static void rda_uart_set_mctrl(struct uart_port *port, unsigned int mctrl)
{
	u32 val;

	if (mctrl & TIOCM_RTS) {
		val = rda_uart_read(port, RDA_UART_CMD_SET);
		rda_uart_write(port, (val | RDA_UART_RTS), RDA_UART_CMD_SET);
	} else {
		/* Clear RTS to stop to receive. */
		val = rda_uart_read(port, RDA_UART_CMD_CLR);
		rda_uart_write(port, (val | RDA_UART_RTS), RDA_UART_CMD_CLR);
	}

	val = rda_uart_read(port, RDA_UART_CTRL);

	if (mctrl & TIOCM_LOOP)
		val |= RDA_UART_LOOP_BACK_EN;
	else
		val &= ~RDA_UART_LOOP_BACK_EN;

	rda_uart_write(port, val, RDA_UART_CTRL);
}

static void rda_uart_stop_tx(struct uart_port *port)
{
	u32 val;

	val = rda_uart_read(port, RDA_UART_IRQ_MASK);
	val &= ~RDA_UART_TX_DATA_NEEDED;
	rda_uart_write(port, val, RDA_UART_IRQ_MASK);

	val = rda_uart_read(port, RDA_UART_CMD_SET);
	val |= RDA_UART_TX_FIFO_RESET;
	rda_uart_write(port, val, RDA_UART_CMD_SET);
}

static void rda_uart_stop_rx(struct uart_port *port)
{
	u32 val;

	val = rda_uart_read(port, RDA_UART_IRQ_MASK);
	val &= ~(RDA_UART_RX_DATA_AVAILABLE | RDA_UART_RX_TIMEOUT);
	rda_uart_write(port, val, RDA_UART_IRQ_MASK);

	/* Read Rx buffer before reset to avoid Rx timeout interrupt */
	val = rda_uart_read(port, RDA_UART_RXTX_BUFFER);

	val = rda_uart_read(port, RDA_UART_CMD_SET);
	val |= RDA_UART_RX_FIFO_RESET;
	rda_uart_write(port, val, RDA_UART_CMD_SET);
}

static void rda_uart_start_tx(struct uart_port *port)
{
	u32 val;

	if (uart_tx_stopped(port)) {
		rda_uart_stop_tx(port);
		return;
	}

	val = rda_uart_read(port, RDA_UART_IRQ_MASK);
	val |= RDA_UART_TX_DATA_NEEDED;
	rda_uart_write(port, val, RDA_UART_IRQ_MASK);
}

static void rda_uart_change_baudrate(struct rda_uart_port *rda_port,
				     unsigned long baud)
{
	clk_set_rate(rda_port->clk, baud * 8);
}

static void rda_uart_set_termios(struct uart_port *port,
				 struct ktermios *termios,
				 const struct ktermios *old)
{
	struct rda_uart_port *rda_port = to_rda_uart_port(port);
	unsigned long flags;
	unsigned int ctrl, cmd_set, cmd_clr, triggers;
	unsigned int baud;
	u32 irq_mask;

	uart_port_lock_irqsave(port, &flags);

	baud = uart_get_baud_rate(port, termios, old, 9600, port->uartclk / 4);
	rda_uart_change_baudrate(rda_port, baud);

	ctrl = rda_uart_read(port, RDA_UART_CTRL);
	cmd_set = rda_uart_read(port, RDA_UART_CMD_SET);
	cmd_clr = rda_uart_read(port, RDA_UART_CMD_CLR);

	switch (termios->c_cflag & CSIZE) {
	case CS5:
	case CS6:
		dev_warn(port->dev, "bit size not supported, using 7 bits\n");
		fallthrough;
	case CS7:
		ctrl &= ~RDA_UART_DBITS_8;
		termios->c_cflag &= ~CSIZE;
		termios->c_cflag |= CS7;
		break;
	default:
		ctrl |= RDA_UART_DBITS_8;
		break;
	}

	/* stop bits */
	if (termios->c_cflag & CSTOPB)
		ctrl |= RDA_UART_TX_SBITS_2;
	else
		ctrl &= ~RDA_UART_TX_SBITS_2;

	/* parity check */
	if (termios->c_cflag & PARENB) {
		ctrl |= RDA_UART_PARITY_EN;

		/* Mark or Space parity */
		if (termios->c_cflag & CMSPAR) {
			if (termios->c_cflag & PARODD)
				ctrl |= RDA_UART_PARITY_MARK;
			else
				ctrl |= RDA_UART_PARITY_SPACE;
		} else if (termios->c_cflag & PARODD) {
			ctrl |= RDA_UART_PARITY_ODD;
		} else {
			ctrl |= RDA_UART_PARITY_EVEN;
		}
	} else {
		ctrl &= ~RDA_UART_PARITY_EN;
	}

	/* Hardware handshake (RTS/CTS) */
	if (termios->c_cflag & CRTSCTS) {
		ctrl   |= RDA_UART_FLOW_CNT_EN;
		cmd_set |= RDA_UART_RTS;
	} else {
		ctrl   &= ~RDA_UART_FLOW_CNT_EN;
		cmd_clr |= RDA_UART_RTS;
	}

	ctrl |= RDA_UART_ENABLE;
	ctrl &= ~RDA_UART_DMA_EN;

	triggers  = (RDA_UART_AFC_LEVEL(20) | RDA_UART_RX_TRIGGER(16));
	irq_mask = rda_uart_read(port, RDA_UART_IRQ_MASK);
	rda_uart_write(port, 0, RDA_UART_IRQ_MASK);

	rda_uart_write(port, triggers, RDA_UART_IRQ_TRIGGERS);
	rda_uart_write(port, ctrl, RDA_UART_CTRL);
	rda_uart_write(port, cmd_set, RDA_UART_CMD_SET);
	rda_uart_write(port, cmd_clr, RDA_UART_CMD_CLR);

	rda_uart_write(port, irq_mask, RDA_UART_IRQ_MASK);

	/* Don't rewrite B0 */
	if (tty_termios_baud_rate(termios))
		tty_termios_encode_baud_rate(termios, baud, baud);

	/* update the per-port timeout */
	uart_update_timeout(port, termios->c_cflag, baud);

	uart_port_unlock_irqrestore(port, flags);
}

static void rda_uart_send_chars(struct uart_port *port)
{
	struct circ_buf *xmit = &port->state->xmit;
	unsigned int ch;
	u32 val;

	if (uart_tx_stopped(port))
		return;

	if (port->x_char) {
		while (!(rda_uart_read(port, RDA_UART_STATUS) &
			 RDA_UART_TX_FIFO_MASK))
			cpu_relax();

		rda_uart_write(port, port->x_char, RDA_UART_RXTX_BUFFER);
		port->icount.tx++;
		port->x_char = 0;
	}

	while (rda_uart_read(port, RDA_UART_STATUS) & RDA_UART_TX_FIFO_MASK) {
		if (uart_circ_empty(xmit))
			break;

		ch = xmit->buf[xmit->tail];
		rda_uart_write(port, ch, RDA_UART_RXTX_BUFFER);
		uart_xmit_advance(port, 1);
	}

	if (uart_circ_chars_pending(xmit) < WAKEUP_CHARS)
		uart_write_wakeup(port);

	if (!uart_circ_empty(xmit)) {
		/* Re-enable Tx FIFO interrupt */
		val = rda_uart_read(port, RDA_UART_IRQ_MASK);
		val |= RDA_UART_TX_DATA_NEEDED;
		rda_uart_write(port, val, RDA_UART_IRQ_MASK);
	}
}

static void rda_uart_receive_chars(struct uart_port *port)
{
	u32 status, val;

	status = rda_uart_read(port, RDA_UART_STATUS);
	while ((status & RDA_UART_RX_FIFO_MASK)) {
		char flag = TTY_NORMAL;

		if (status & RDA_UART_RX_PARITY_ERR) {
			port->icount.parity++;
			flag = TTY_PARITY;
		}

		if (status & RDA_UART_RX_FRAMING_ERR) {
			port->icount.frame++;
			flag = TTY_FRAME;
		}

		if (status & RDA_UART_RX_OVERFLOW_ERR) {
			port->icount.overrun++;
			flag = TTY_OVERRUN;
		}

		val = rda_uart_read(port, RDA_UART_RXTX_BUFFER);
		val &= 0xff;

		port->icount.rx++;
		tty_insert_flip_char(&port->state->port, val, flag);

		status = rda_uart_read(port, RDA_UART_STATUS);
	}

	tty_flip_buffer_push(&port->state->port);
}

static irqreturn_t rda_interrupt(int irq, void *dev_id)
{
	struct uart_port *port = dev_id;
	unsigned long flags;
	u32 val, irq_mask;

	uart_port_lock_irqsave(port, &flags);

	/* Clear IRQ cause */
	val = rda_uart_read(port, RDA_UART_IRQ_CAUSE);
	rda_uart_write(port, val, RDA_UART_IRQ_CAUSE);

	if (val & (RDA_UART_RX_DATA_AVAILABLE | RDA_UART_RX_TIMEOUT))
		rda_uart_receive_chars(port);

	if (val & (RDA_UART_TX_DATA_NEEDED)) {
		irq_mask = rda_uart_read(port, RDA_UART_IRQ_MASK);
		irq_mask &= ~RDA_UART_TX_DATA_NEEDED;
		rda_uart_write(port, irq_mask, RDA_UART_IRQ_MASK);

		rda_uart_send_chars(port);
	}

	uart_port_unlock_irqrestore(port, flags);

	return IRQ_HANDLED;
}

static int rda_uart_startup(struct uart_port *port)
{
	unsigned long flags;
	int ret;
	u32 val;

	uart_port_lock_irqsave(port, &flags);
	rda_uart_write(port, 0, RDA_UART_IRQ_MASK);
	uart_port_unlock_irqrestore(port, flags);

	ret = request_irq(port->irq, rda_interrupt, IRQF_NO_SUSPEND,
			  "rda-uart", port);
	if (ret)
		return ret;

	uart_port_lock_irqsave(port, &flags);

	val = rda_uart_read(port, RDA_UART_CTRL);
	val |= RDA_UART_ENABLE;
	rda_uart_write(port, val, RDA_UART_CTRL);

	/* enable rx interrupt */
	val = rda_uart_read(port, RDA_UART_IRQ_MASK);
	val |= (RDA_UART_RX_DATA_AVAILABLE | RDA_UART_RX_TIMEOUT);
	rda_uart_write(port, val, RDA_UART_IRQ_MASK);

	uart_port_unlock_irqrestore(port, flags);

	return 0;
}

static void rda_uart_shutdown(struct uart_port *port)
{
	unsigned long flags;
	u32 val;

	uart_port_lock_irqsave(port, &flags);

	rda_uart_stop_tx(port);
	rda_uart_stop_rx(port);

	val = rda_uart_read(port, RDA_UART_CTRL);
	val &= ~RDA_UART_ENABLE;
	rda_uart_write(port, val, RDA_UART_CTRL);

	uart_port_unlock_irqrestore(port, flags);
}

static const char *rda_uart_type(struct uart_port *port)
{
	return (port->type == PORT_RDA) ? "rda-uart" : NULL;
}

static int rda_uart_request_port(struct uart_port *port)
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

static void rda_uart_config_port(struct uart_port *port, int flags)
{
	unsigned long irq_flags;

	if (flags & UART_CONFIG_TYPE) {
		port->type = PORT_RDA;
		rda_uart_request_port(port);
	}

	uart_port_lock_irqsave(port, &irq_flags);

	/* Clear mask, so no surprise interrupts. */
	rda_uart_write(port, 0, RDA_UART_IRQ_MASK);

	/* Clear status register */
	rda_uart_write(port, 0, RDA_UART_STATUS);

	uart_port_unlock_irqrestore(port, irq_flags);
}

static void rda_uart_release_port(struct uart_port *port)
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

static int rda_uart_verify_port(struct uart_port *port,
				struct serial_struct *ser)
{
	if (port->type != PORT_RDA)
		return -EINVAL;

	if (port->irq != ser->irq)
		return -EINVAL;

	return 0;
}

static const struct uart_ops rda_uart_ops = {
	.tx_empty       = rda_uart_tx_empty,
	.get_mctrl      = rda_uart_get_mctrl,
	.set_mctrl      = rda_uart_set_mctrl,
	.start_tx       = rda_uart_start_tx,
	.stop_tx        = rda_uart_stop_tx,
	.stop_rx        = rda_uart_stop_rx,
	.startup        = rda_uart_startup,
	.shutdown       = rda_uart_shutdown,
	.set_termios    = rda_uart_set_termios,
	.type           = rda_uart_type,
	.request_port	= rda_uart_request_port,
	.release_port	= rda_uart_release_port,
	.config_port	= rda_uart_config_port,
	.verify_port	= rda_uart_verify_port,
};

#ifdef CONFIG_SERIAL_RDA_CONSOLE

static void rda_console_putchar(struct uart_port *port, unsigned char ch)
{
	if (!port->membase)
		return;

	while (!(rda_uart_read(port, RDA_UART_STATUS) & RDA_UART_TX_FIFO_MASK))
		cpu_relax();

	rda_uart_write(port, ch, RDA_UART_RXTX_BUFFER);
}

static void rda_uart_port_write(struct uart_port *port, const char *s,
				u_int count)
{
	u32 old_irq_mask;
	unsigned long flags;
	int locked;

	local_irq_save(flags);

	if (port->sysrq) {
		locked = 0;
	} else if (oops_in_progress) {
		locked = uart_port_trylock(port);
	} else {
		uart_port_lock(port);
		locked = 1;
	}

	old_irq_mask = rda_uart_read(port, RDA_UART_IRQ_MASK);
	rda_uart_write(port, 0, RDA_UART_IRQ_MASK);

	uart_console_write(port, s, count, rda_console_putchar);

	/* wait until all contents have been sent out */
	while (!(rda_uart_read(port, RDA_UART_STATUS) & RDA_UART_TX_FIFO_MASK))
		cpu_relax();

	rda_uart_write(port, old_irq_mask, RDA_UART_IRQ_MASK);

	if (locked)
		uart_port_unlock(port);

	local_irq_restore(flags);
}

static void rda_uart_console_write(struct console *co, const char *s,
				   u_int count)
{
	struct rda_uart_port *rda_port;

	rda_port = rda_uart_ports[co->index];
	if (!rda_port)
		return;

	rda_uart_port_write(&rda_port->port, s, count);
}

static int rda_uart_console_setup(struct console *co, char *options)
{
	struct rda_uart_port *rda_port;
	int baud = 921600;
	int bits = 8;
	int parity = 'n';
	int flow = 'n';

	if (co->index < 0 || co->index >= RDA_UART_PORT_NUM)
		return -EINVAL;

	rda_port = rda_uart_ports[co->index];
	if (!rda_port || !rda_port->port.membase)
		return -ENODEV;

	if (options)
		uart_parse_options(options, &baud, &parity, &bits, &flow);

	return uart_set_options(&rda_port->port, co, baud, parity, bits, flow);
}

static struct console rda_uart_console = {
	.name = RDA_UART_DEV_NAME,
	.write = rda_uart_console_write,
	.device = uart_console_device,
	.setup = rda_uart_console_setup,
	.flags = CON_PRINTBUFFER,
	.index = -1,
	.data = &rda_uart_driver,
};

static int __init rda_uart_console_init(void)
{
	register_console(&rda_uart_console);

	return 0;
}
console_initcall(rda_uart_console_init);

static void rda_uart_early_console_write(struct console *co,
					 const char *s,
					 u_int count)
{
	struct earlycon_device *dev = co->data;

	rda_uart_port_write(&dev->port, s, count);
}

static int __init
rda_uart_early_console_setup(struct earlycon_device *device, const char *opt)
{
	if (!device->port.membase)
		return -ENODEV;

	device->con->write = rda_uart_early_console_write;

	return 0;
}

OF_EARLYCON_DECLARE(rda, "rda,8810pl-uart",
		    rda_uart_early_console_setup);

#define RDA_UART_CONSOLE (&rda_uart_console)
#else
#define RDA_UART_CONSOLE NULL
#endif /* CONFIG_SERIAL_RDA_CONSOLE */

static struct uart_driver rda_uart_driver = {
	.owner = THIS_MODULE,
	.driver_name = "rda-uart",
	.dev_name = RDA_UART_DEV_NAME,
	.nr = RDA_UART_PORT_NUM,
	.cons = RDA_UART_CONSOLE,
};

static const struct of_device_id rda_uart_dt_matches[] = {
	{ .compatible = "rda,8810pl-uart" },
	{ }
};
MODULE_DEVICE_TABLE(of, rda_uart_dt_matches);

static int rda_uart_probe(struct platform_device *pdev)
{
	struct resource *res_mem;
	struct rda_uart_port *rda_port;
	int ret, irq;

	if (pdev->dev.of_node)
		pdev->id = of_alias_get_id(pdev->dev.of_node, "serial");

	if (pdev->id < 0 || pdev->id >= RDA_UART_PORT_NUM) {
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

	if (rda_uart_ports[pdev->id]) {
		dev_err(&pdev->dev, "port %d already allocated\n", pdev->id);
		return -EBUSY;
	}

	rda_port = devm_kzalloc(&pdev->dev, sizeof(*rda_port), GFP_KERNEL);
	if (!rda_port)
		return -ENOMEM;

	rda_port->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(rda_port->clk)) {
		dev_err(&pdev->dev, "could not get clk\n");
		return PTR_ERR(rda_port->clk);
	}

	rda_port->port.dev = &pdev->dev;
	rda_port->port.regshift = 0;
	rda_port->port.line = pdev->id;
	rda_port->port.type = PORT_RDA;
	rda_port->port.iotype = UPIO_MEM;
	rda_port->port.mapbase = res_mem->start;
	rda_port->port.irq = irq;
	rda_port->port.uartclk = clk_get_rate(rda_port->clk);
	if (rda_port->port.uartclk == 0) {
		dev_err(&pdev->dev, "clock rate is zero\n");
		return -EINVAL;
	}
	rda_port->port.flags = UPF_BOOT_AUTOCONF | UPF_IOREMAP |
			       UPF_LOW_LATENCY;
	rda_port->port.x_char = 0;
	rda_port->port.fifosize = RDA_UART_TX_FIFO_SIZE;
	rda_port->port.ops = &rda_uart_ops;

	rda_uart_ports[pdev->id] = rda_port;
	platform_set_drvdata(pdev, rda_port);

	ret = uart_add_one_port(&rda_uart_driver, &rda_port->port);
	if (ret)
		rda_uart_ports[pdev->id] = NULL;

	return ret;
}

static void rda_uart_remove(struct platform_device *pdev)
{
	struct rda_uart_port *rda_port = platform_get_drvdata(pdev);

	uart_remove_one_port(&rda_uart_driver, &rda_port->port);
	rda_uart_ports[pdev->id] = NULL;
}

static struct platform_driver rda_uart_platform_driver = {
	.probe = rda_uart_probe,
	.remove_new = rda_uart_remove,
	.driver = {
		.name = "rda-uart",
		.of_match_table = rda_uart_dt_matches,
	},
};

static int __init rda_uart_init(void)
{
	int ret;

	ret = uart_register_driver(&rda_uart_driver);
	if (ret)
		return ret;

	ret = platform_driver_register(&rda_uart_platform_driver);
	if (ret)
		uart_unregister_driver(&rda_uart_driver);

	return ret;
}

static void __exit rda_uart_exit(void)
{
	platform_driver_unregister(&rda_uart_platform_driver);
	uart_unregister_driver(&rda_uart_driver);
}

module_init(rda_uart_init);
module_exit(rda_uart_exit);

MODULE_AUTHOR("Manivannan Sadhasivam <manivannan.sadhasivam@linaro.org>");
MODULE_DESCRIPTION("RDA8810PL serial device driver");
MODULE_LICENSE("GPL");
