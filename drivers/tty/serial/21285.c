// SPDX-License-Identifier: GPL-2.0
/*
 * Driver for the serial port on the 21285 StrongArm-110 core logic chip.
 *
 * Based on drivers/char/serial.c
 */
#include <linux/module.h>
#include <linux/tty.h>
#include <linux/ioport.h>
#include <linux/init.h>
#include <linux/console.h>
#include <linux/device.h>
#include <linux/tty_flip.h>
#include <linux/serial_core.h>
#include <linux/serial.h>
#include <linux/io.h>

#include <asm/irq.h>
#include <asm/mach-types.h>
#include <asm/system_info.h>
#include <asm/hardware/dec21285.h>
#include <mach/hardware.h>

#define BAUD_BASE		(mem_fclk_21285/64)

#define SERIAL_21285_NAME	"ttyFB"
#define SERIAL_21285_MAJOR	204
#define SERIAL_21285_MINOR	4

#define RXSTAT_DUMMY_READ	0x80000000
#define RXSTAT_FRAME		(1 << 0)
#define RXSTAT_PARITY		(1 << 1)
#define RXSTAT_OVERRUN		(1 << 2)
#define RXSTAT_ANYERR		(RXSTAT_FRAME|RXSTAT_PARITY|RXSTAT_OVERRUN)

#define H_UBRLCR_BREAK		(1 << 0)
#define H_UBRLCR_PARENB		(1 << 1)
#define H_UBRLCR_PAREVN		(1 << 2)
#define H_UBRLCR_STOPB		(1 << 3)
#define H_UBRLCR_FIFO		(1 << 4)

static const char serial21285_name[] = "Footbridge UART";

/*
 * We only need 2 bits of data, so instead of creating a whole structure for
 * this, use bits of the private_data pointer of the uart port structure.
 */
#define tx_enabled_bit	0
#define rx_enabled_bit	1

static bool is_enabled(struct uart_port *port, int bit)
{
	unsigned long *private_data = (unsigned long *)&port->private_data;

	if (test_bit(bit, private_data))
		return true;
	return false;
}

static void enable(struct uart_port *port, int bit)
{
	unsigned long *private_data = (unsigned long *)&port->private_data;

	set_bit(bit, private_data);
}

static void disable(struct uart_port *port, int bit)
{
	unsigned long *private_data = (unsigned long *)&port->private_data;

	clear_bit(bit, private_data);
}

#define is_tx_enabled(port)	is_enabled(port, tx_enabled_bit)
#define tx_enable(port)		enable(port, tx_enabled_bit)
#define tx_disable(port)	disable(port, tx_enabled_bit)

#define is_rx_enabled(port)	is_enabled(port, rx_enabled_bit)
#define rx_enable(port)		enable(port, rx_enabled_bit)
#define rx_disable(port)	disable(port, rx_enabled_bit)

/*
 * The documented expression for selecting the divisor is:
 *  BAUD_BASE / baud - 1
 * However, typically BAUD_BASE is not divisible by baud, so
 * we want to select the divisor that gives us the minimum
 * error.  Therefore, we want:
 *  int(BAUD_BASE / baud - 0.5) ->
 *  int(BAUD_BASE / baud - (baud >> 1) / baud) ->
 *  int((BAUD_BASE - (baud >> 1)) / baud)
 */

static void serial21285_stop_tx(struct uart_port *port)
{
	if (is_tx_enabled(port)) {
		disable_irq_nosync(IRQ_CONTX);
		tx_disable(port);
	}
}

static void serial21285_start_tx(struct uart_port *port)
{
	if (!is_tx_enabled(port)) {
		enable_irq(IRQ_CONTX);
		tx_enable(port);
	}
}

static void serial21285_stop_rx(struct uart_port *port)
{
	if (is_rx_enabled(port)) {
		disable_irq_nosync(IRQ_CONRX);
		rx_disable(port);
	}
}

static irqreturn_t serial21285_rx_chars(int irq, void *dev_id)
{
	struct uart_port *port = dev_id;
	unsigned int status, ch, flag, rxs, max_count = 256;

	status = *CSR_UARTFLG;
	while (!(status & 0x10) && max_count--) {
		ch = *CSR_UARTDR;
		flag = TTY_NORMAL;
		port->icount.rx++;

		rxs = *CSR_RXSTAT | RXSTAT_DUMMY_READ;
		if (unlikely(rxs & RXSTAT_ANYERR)) {
			if (rxs & RXSTAT_PARITY)
				port->icount.parity++;
			else if (rxs & RXSTAT_FRAME)
				port->icount.frame++;
			if (rxs & RXSTAT_OVERRUN)
				port->icount.overrun++;

			rxs &= port->read_status_mask;

			if (rxs & RXSTAT_PARITY)
				flag = TTY_PARITY;
			else if (rxs & RXSTAT_FRAME)
				flag = TTY_FRAME;
		}

		uart_insert_char(port, rxs, RXSTAT_OVERRUN, ch, flag);

		status = *CSR_UARTFLG;
	}
	tty_flip_buffer_push(&port->state->port);

	return IRQ_HANDLED;
}

static irqreturn_t serial21285_tx_chars(int irq, void *dev_id)
{
	struct uart_port *port = dev_id;
	struct circ_buf *xmit = &port->state->xmit;
	int count = 256;

	if (port->x_char) {
		*CSR_UARTDR = port->x_char;
		port->icount.tx++;
		port->x_char = 0;
		goto out;
	}
	if (uart_circ_empty(xmit) || uart_tx_stopped(port)) {
		serial21285_stop_tx(port);
		goto out;
	}

	do {
		*CSR_UARTDR = xmit->buf[xmit->tail];
		xmit->tail = (xmit->tail + 1) & (UART_XMIT_SIZE - 1);
		port->icount.tx++;
		if (uart_circ_empty(xmit))
			break;
	} while (--count > 0 && !(*CSR_UARTFLG & 0x20));

	if (uart_circ_chars_pending(xmit) < WAKEUP_CHARS)
		uart_write_wakeup(port);

	if (uart_circ_empty(xmit))
		serial21285_stop_tx(port);

 out:
	return IRQ_HANDLED;
}

static unsigned int serial21285_tx_empty(struct uart_port *port)
{
	return (*CSR_UARTFLG & 8) ? 0 : TIOCSER_TEMT;
}

/* no modem control lines */
static unsigned int serial21285_get_mctrl(struct uart_port *port)
{
	return TIOCM_CAR | TIOCM_DSR | TIOCM_CTS;
}

static void serial21285_set_mctrl(struct uart_port *port, unsigned int mctrl)
{
}

static void serial21285_break_ctl(struct uart_port *port, int break_state)
{
	unsigned long flags;
	unsigned int h_lcr;

	spin_lock_irqsave(&port->lock, flags);
	h_lcr = *CSR_H_UBRLCR;
	if (break_state)
		h_lcr |= H_UBRLCR_BREAK;
	else
		h_lcr &= ~H_UBRLCR_BREAK;
	*CSR_H_UBRLCR = h_lcr;
	spin_unlock_irqrestore(&port->lock, flags);
}

static int serial21285_startup(struct uart_port *port)
{
	int ret;

	tx_enable(port);
	rx_enable(port);

	ret = request_irq(IRQ_CONRX, serial21285_rx_chars, 0,
			  serial21285_name, port);
	if (ret == 0) {
		ret = request_irq(IRQ_CONTX, serial21285_tx_chars, 0,
				  serial21285_name, port);
		if (ret)
			free_irq(IRQ_CONRX, port);
	}

	return ret;
}

static void serial21285_shutdown(struct uart_port *port)
{
	free_irq(IRQ_CONTX, port);
	free_irq(IRQ_CONRX, port);
}

static void
serial21285_set_termios(struct uart_port *port, struct ktermios *termios,
			struct ktermios *old)
{
	unsigned long flags;
	unsigned int baud, quot, h_lcr, b;

	/*
	 * We don't support modem control lines.
	 */
	termios->c_cflag &= ~(HUPCL | CRTSCTS | CMSPAR);
	termios->c_cflag |= CLOCAL;

	/*
	 * We don't support BREAK character recognition.
	 */
	termios->c_iflag &= ~(IGNBRK | BRKINT);

	/*
	 * Ask the core to calculate the divisor for us.
	 */
	baud = uart_get_baud_rate(port, termios, old, 0, port->uartclk/16); 
	quot = uart_get_divisor(port, baud);
	b = port->uartclk / (16 * quot);
	tty_termios_encode_baud_rate(termios, b, b);

	switch (termios->c_cflag & CSIZE) {
	case CS5:
		h_lcr = 0x00;
		break;
	case CS6:
		h_lcr = 0x20;
		break;
	case CS7:
		h_lcr = 0x40;
		break;
	default: /* CS8 */
		h_lcr = 0x60;
		break;
	}

	if (termios->c_cflag & CSTOPB)
		h_lcr |= H_UBRLCR_STOPB;
	if (termios->c_cflag & PARENB) {
		h_lcr |= H_UBRLCR_PARENB;
		if (!(termios->c_cflag & PARODD))
			h_lcr |= H_UBRLCR_PAREVN;
	}

	if (port->fifosize)
		h_lcr |= H_UBRLCR_FIFO;

	spin_lock_irqsave(&port->lock, flags);

	/*
	 * Update the per-port timeout.
	 */
	uart_update_timeout(port, termios->c_cflag, baud);

	/*
	 * Which character status flags are we interested in?
	 */
	port->read_status_mask = RXSTAT_OVERRUN;
	if (termios->c_iflag & INPCK)
		port->read_status_mask |= RXSTAT_FRAME | RXSTAT_PARITY;

	/*
	 * Which character status flags should we ignore?
	 */
	port->ignore_status_mask = 0;
	if (termios->c_iflag & IGNPAR)
		port->ignore_status_mask |= RXSTAT_FRAME | RXSTAT_PARITY;
	if (termios->c_iflag & IGNBRK && termios->c_iflag & IGNPAR)
		port->ignore_status_mask |= RXSTAT_OVERRUN;

	/*
	 * Ignore all characters if CREAD is not set.
	 */
	if ((termios->c_cflag & CREAD) == 0)
		port->ignore_status_mask |= RXSTAT_DUMMY_READ;

	quot -= 1;

	*CSR_UARTCON = 0;
	*CSR_L_UBRLCR = quot & 0xff;
	*CSR_M_UBRLCR = (quot >> 8) & 0x0f;
	*CSR_H_UBRLCR = h_lcr;
	*CSR_UARTCON = 1;

	spin_unlock_irqrestore(&port->lock, flags);
}

static const char *serial21285_type(struct uart_port *port)
{
	return port->type == PORT_21285 ? "DC21285" : NULL;
}

static void serial21285_release_port(struct uart_port *port)
{
	release_mem_region(port->mapbase, 32);
}

static int serial21285_request_port(struct uart_port *port)
{
	return request_mem_region(port->mapbase, 32, serial21285_name)
			 != NULL ? 0 : -EBUSY;
}

static void serial21285_config_port(struct uart_port *port, int flags)
{
	if (flags & UART_CONFIG_TYPE && serial21285_request_port(port) == 0)
		port->type = PORT_21285;
}

/*
 * verify the new serial_struct (for TIOCSSERIAL).
 */
static int serial21285_verify_port(struct uart_port *port, struct serial_struct *ser)
{
	int ret = 0;
	if (ser->type != PORT_UNKNOWN && ser->type != PORT_21285)
		ret = -EINVAL;
	if (ser->irq <= 0)
		ret = -EINVAL;
	if (ser->baud_base != port->uartclk / 16)
		ret = -EINVAL;
	return ret;
}

static const struct uart_ops serial21285_ops = {
	.tx_empty	= serial21285_tx_empty,
	.get_mctrl	= serial21285_get_mctrl,
	.set_mctrl	= serial21285_set_mctrl,
	.stop_tx	= serial21285_stop_tx,
	.start_tx	= serial21285_start_tx,
	.stop_rx	= serial21285_stop_rx,
	.break_ctl	= serial21285_break_ctl,
	.startup	= serial21285_startup,
	.shutdown	= serial21285_shutdown,
	.set_termios	= serial21285_set_termios,
	.type		= serial21285_type,
	.release_port	= serial21285_release_port,
	.request_port	= serial21285_request_port,
	.config_port	= serial21285_config_port,
	.verify_port	= serial21285_verify_port,
};

static struct uart_port serial21285_port = {
	.mapbase	= 0x42000160,
	.iotype		= UPIO_MEM,
	.irq		= 0,
	.fifosize	= 16,
	.ops		= &serial21285_ops,
	.flags		= UPF_BOOT_AUTOCONF,
};

static void serial21285_setup_ports(void)
{
	serial21285_port.uartclk = mem_fclk_21285 / 4;
}

#ifdef CONFIG_SERIAL_21285_CONSOLE
static void serial21285_console_putchar(struct uart_port *port, unsigned char ch)
{
	while (*CSR_UARTFLG & 0x20)
		barrier();
	*CSR_UARTDR = ch;
}

static void
serial21285_console_write(struct console *co, const char *s,
			  unsigned int count)
{
	uart_console_write(&serial21285_port, s, count, serial21285_console_putchar);
}

static void __init
serial21285_get_options(struct uart_port *port, int *baud,
			int *parity, int *bits)
{
	if (*CSR_UARTCON == 1) {
		unsigned int tmp;

		tmp = *CSR_H_UBRLCR;
		switch (tmp & 0x60) {
		case 0x00:
			*bits = 5;
			break;
		case 0x20:
			*bits = 6;
			break;
		case 0x40:
			*bits = 7;
			break;
		default:
		case 0x60:
			*bits = 8;
			break;
		}

		if (tmp & H_UBRLCR_PARENB) {
			*parity = 'o';
			if (tmp & H_UBRLCR_PAREVN)
				*parity = 'e';
		}

		tmp = *CSR_L_UBRLCR | (*CSR_M_UBRLCR << 8);

		*baud = port->uartclk / (16 * (tmp + 1));
	}
}

static int __init serial21285_console_setup(struct console *co, char *options)
{
	struct uart_port *port = &serial21285_port;
	int baud = 9600;
	int bits = 8;
	int parity = 'n';
	int flow = 'n';

	if (machine_is_personal_server())
		baud = 57600;

	/*
	 * Check whether an invalid uart number has been specified, and
	 * if so, search for the first available port that does have
	 * console support.
	 */
	if (options)
		uart_parse_options(options, &baud, &parity, &bits, &flow);
	else
		serial21285_get_options(port, &baud, &parity, &bits);

	return uart_set_options(port, co, baud, parity, bits, flow);
}

static struct uart_driver serial21285_reg;

static struct console serial21285_console =
{
	.name		= SERIAL_21285_NAME,
	.write		= serial21285_console_write,
	.device		= uart_console_device,
	.setup		= serial21285_console_setup,
	.flags		= CON_PRINTBUFFER,
	.index		= -1,
	.data		= &serial21285_reg,
};

static int __init rs285_console_init(void)
{
	serial21285_setup_ports();
	register_console(&serial21285_console);
	return 0;
}
console_initcall(rs285_console_init);

#define SERIAL_21285_CONSOLE	&serial21285_console
#else
#define SERIAL_21285_CONSOLE	NULL
#endif

static struct uart_driver serial21285_reg = {
	.owner			= THIS_MODULE,
	.driver_name		= "ttyFB",
	.dev_name		= "ttyFB",
	.major			= SERIAL_21285_MAJOR,
	.minor			= SERIAL_21285_MINOR,
	.nr			= 1,
	.cons			= SERIAL_21285_CONSOLE,
};

static int __init serial21285_init(void)
{
	int ret;

	printk(KERN_INFO "Serial: 21285 driver\n");

	serial21285_setup_ports();

	ret = uart_register_driver(&serial21285_reg);
	if (ret == 0)
		uart_add_one_port(&serial21285_reg, &serial21285_port);

	return ret;
}

static void __exit serial21285_exit(void)
{
	uart_remove_one_port(&serial21285_reg, &serial21285_port);
	uart_unregister_driver(&serial21285_reg);
}

module_init(serial21285_init);
module_exit(serial21285_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Intel Footbridge (21285) serial driver");
MODULE_ALIAS_CHARDEV(SERIAL_21285_MAJOR, SERIAL_21285_MINOR);
