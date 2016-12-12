/* sunhv.c: Serial driver for SUN4V hypervisor console.
 *
 * Copyright (C) 2006, 2007 David S. Miller (davem@davemloft.net)
 */

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/major.h>
#include <linux/circ_buf.h>
#include <linux/serial.h>
#include <linux/sysrq.h>
#include <linux/console.h>
#include <linux/spinlock.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/of_device.h>

#include <asm/hypervisor.h>
#include <asm/spitfire.h>
#include <asm/prom.h>
#include <asm/irq.h>
#include <asm/setup.h>

#if defined(CONFIG_MAGIC_SYSRQ)
#define SUPPORT_SYSRQ
#endif

#include <linux/serial_core.h>
#include <linux/sunserialcore.h>

#define CON_BREAK	((long)-1)
#define CON_HUP		((long)-2)

#define IGNORE_BREAK	0x1
#define IGNORE_ALL	0x2

static char *con_write_page;
static char *con_read_page;

static int hung_up = 0;

static void transmit_chars_putchar(struct uart_port *port, struct circ_buf *xmit)
{
	while (!uart_circ_empty(xmit)) {
		long status = sun4v_con_putchar(xmit->buf[xmit->tail]);

		if (status != HV_EOK)
			break;

		xmit->tail = (xmit->tail + 1) & (UART_XMIT_SIZE - 1);
		port->icount.tx++;
	}
}

static void transmit_chars_write(struct uart_port *port, struct circ_buf *xmit)
{
	while (!uart_circ_empty(xmit)) {
		unsigned long ra = __pa(xmit->buf + xmit->tail);
		unsigned long len, status, sent;

		len = CIRC_CNT_TO_END(xmit->head, xmit->tail,
				      UART_XMIT_SIZE);
		status = sun4v_con_write(ra, len, &sent);
		if (status != HV_EOK)
			break;
		xmit->tail = (xmit->tail + sent) & (UART_XMIT_SIZE - 1);
		port->icount.tx += sent;
	}
}

static int receive_chars_getchar(struct uart_port *port)
{
	int saw_console_brk = 0;
	int limit = 10000;

	while (limit-- > 0) {
		long status;
		long c = sun4v_con_getchar(&status);

		if (status == HV_EWOULDBLOCK)
			break;

		if (c == CON_BREAK) {
			if (uart_handle_break(port))
				continue;
			saw_console_brk = 1;
			c = 0;
		}

		if (c == CON_HUP) {
			hung_up = 1;
			uart_handle_dcd_change(port, 0);
		} else if (hung_up) {
			hung_up = 0;
			uart_handle_dcd_change(port, 1);
		}

		if (port->state == NULL) {
			uart_handle_sysrq_char(port, c);
			continue;
		}

		port->icount.rx++;

		if (uart_handle_sysrq_char(port, c))
			continue;

		tty_insert_flip_char(&port->state->port, c, TTY_NORMAL);
	}

	return saw_console_brk;
}

static int receive_chars_read(struct uart_port *port)
{
	int saw_console_brk = 0;
	int limit = 10000;

	while (limit-- > 0) {
		unsigned long ra = __pa(con_read_page);
		unsigned long bytes_read, i;
		long stat = sun4v_con_read(ra, PAGE_SIZE, &bytes_read);

		if (stat != HV_EOK) {
			bytes_read = 0;

			if (stat == CON_BREAK) {
				if (uart_handle_break(port))
					continue;
				saw_console_brk = 1;
				*con_read_page = 0;
				bytes_read = 1;
			} else if (stat == CON_HUP) {
				hung_up = 1;
				uart_handle_dcd_change(port, 0);
				continue;
			} else {
				/* HV_EWOULDBLOCK, etc.  */
				break;
			}
		}

		if (hung_up) {
			hung_up = 0;
			uart_handle_dcd_change(port, 1);
		}

		if (port->sysrq != 0 &&  *con_read_page) {
			for (i = 0; i < bytes_read; i++)
				uart_handle_sysrq_char(port, con_read_page[i]);
		}

		if (port->state == NULL)
			continue;

		port->icount.rx += bytes_read;

		tty_insert_flip_string(&port->state->port, con_read_page,
				bytes_read);
	}

	return saw_console_brk;
}

struct sunhv_ops {
	void (*transmit_chars)(struct uart_port *port, struct circ_buf *xmit);
	int (*receive_chars)(struct uart_port *port);
};

static const struct sunhv_ops bychar_ops = {
	.transmit_chars = transmit_chars_putchar,
	.receive_chars = receive_chars_getchar,
};

static const struct sunhv_ops bywrite_ops = {
	.transmit_chars = transmit_chars_write,
	.receive_chars = receive_chars_read,
};

static const struct sunhv_ops *sunhv_ops = &bychar_ops;

static struct tty_port *receive_chars(struct uart_port *port)
{
	struct tty_port *tport = NULL;

	if (port->state != NULL)		/* Unopened serial console */
		tport = &port->state->port;

	if (sunhv_ops->receive_chars(port))
		sun_do_break();

	return tport;
}

static void transmit_chars(struct uart_port *port)
{
	struct circ_buf *xmit;

	if (!port->state)
		return;

	xmit = &port->state->xmit;
	if (uart_circ_empty(xmit) || uart_tx_stopped(port))
		return;

	sunhv_ops->transmit_chars(port, xmit);

	if (uart_circ_chars_pending(xmit) < WAKEUP_CHARS)
		uart_write_wakeup(port);
}

static irqreturn_t sunhv_interrupt(int irq, void *dev_id)
{
	struct uart_port *port = dev_id;
	struct tty_port *tport;
	unsigned long flags;

	spin_lock_irqsave(&port->lock, flags);
	tport = receive_chars(port);
	transmit_chars(port);
	spin_unlock_irqrestore(&port->lock, flags);

	if (tport)
		tty_flip_buffer_push(tport);

	return IRQ_HANDLED;
}

/* port->lock is not held.  */
static unsigned int sunhv_tx_empty(struct uart_port *port)
{
	/* Transmitter is always empty for us.  If the circ buffer
	 * is non-empty or there is an x_char pending, our caller
	 * will do the right thing and ignore what we return here.
	 */
	return TIOCSER_TEMT;
}

/* port->lock held by caller.  */
static void sunhv_set_mctrl(struct uart_port *port, unsigned int mctrl)
{
	return;
}

/* port->lock is held by caller and interrupts are disabled.  */
static unsigned int sunhv_get_mctrl(struct uart_port *port)
{
	return TIOCM_DSR | TIOCM_CAR | TIOCM_CTS;
}

/* port->lock held by caller.  */
static void sunhv_stop_tx(struct uart_port *port)
{
	return;
}

/* port->lock held by caller.  */
static void sunhv_start_tx(struct uart_port *port)
{
	transmit_chars(port);
}

/* port->lock is not held.  */
static void sunhv_send_xchar(struct uart_port *port, char ch)
{
	unsigned long flags;
	int limit = 10000;

	if (ch == __DISABLED_CHAR)
		return;

	spin_lock_irqsave(&port->lock, flags);

	while (limit-- > 0) {
		long status = sun4v_con_putchar(ch);
		if (status == HV_EOK)
			break;
		udelay(1);
	}

	spin_unlock_irqrestore(&port->lock, flags);
}

/* port->lock held by caller.  */
static void sunhv_stop_rx(struct uart_port *port)
{
}

/* port->lock is not held.  */
static void sunhv_break_ctl(struct uart_port *port, int break_state)
{
	if (break_state) {
		unsigned long flags;
		int limit = 10000;

		spin_lock_irqsave(&port->lock, flags);

		while (limit-- > 0) {
			long status = sun4v_con_putchar(CON_BREAK);
			if (status == HV_EOK)
				break;
			udelay(1);
		}

		spin_unlock_irqrestore(&port->lock, flags);
	}
}

/* port->lock is not held.  */
static int sunhv_startup(struct uart_port *port)
{
	return 0;
}

/* port->lock is not held.  */
static void sunhv_shutdown(struct uart_port *port)
{
}

/* port->lock is not held.  */
static void sunhv_set_termios(struct uart_port *port, struct ktermios *termios,
			      struct ktermios *old)
{
	unsigned int baud = uart_get_baud_rate(port, termios, old, 0, 4000000);
	unsigned int quot = uart_get_divisor(port, baud);
	unsigned int iflag, cflag;
	unsigned long flags;

	spin_lock_irqsave(&port->lock, flags);

	iflag = termios->c_iflag;
	cflag = termios->c_cflag;

	port->ignore_status_mask = 0;
	if (iflag & IGNBRK)
		port->ignore_status_mask |= IGNORE_BREAK;
	if ((cflag & CREAD) == 0)
		port->ignore_status_mask |= IGNORE_ALL;

	/* XXX */
	uart_update_timeout(port, cflag,
			    (port->uartclk / (16 * quot)));

	spin_unlock_irqrestore(&port->lock, flags);
}

static const char *sunhv_type(struct uart_port *port)
{
	return "SUN4V HCONS";
}

static void sunhv_release_port(struct uart_port *port)
{
}

static int sunhv_request_port(struct uart_port *port)
{
	return 0;
}

static void sunhv_config_port(struct uart_port *port, int flags)
{
}

static int sunhv_verify_port(struct uart_port *port, struct serial_struct *ser)
{
	return -EINVAL;
}

static struct uart_ops sunhv_pops = {
	.tx_empty	= sunhv_tx_empty,
	.set_mctrl	= sunhv_set_mctrl,
	.get_mctrl	= sunhv_get_mctrl,
	.stop_tx	= sunhv_stop_tx,
	.start_tx	= sunhv_start_tx,
	.send_xchar	= sunhv_send_xchar,
	.stop_rx	= sunhv_stop_rx,
	.break_ctl	= sunhv_break_ctl,
	.startup	= sunhv_startup,
	.shutdown	= sunhv_shutdown,
	.set_termios	= sunhv_set_termios,
	.type		= sunhv_type,
	.release_port	= sunhv_release_port,
	.request_port	= sunhv_request_port,
	.config_port	= sunhv_config_port,
	.verify_port	= sunhv_verify_port,
};

static struct uart_driver sunhv_reg = {
	.owner			= THIS_MODULE,
	.driver_name		= "sunhv",
	.dev_name		= "ttyS",
	.major			= TTY_MAJOR,
};

static struct uart_port *sunhv_port;

/* Copy 's' into the con_write_page, decoding "\n" into
 * "\r\n" along the way.  We have to return two lengths
 * because the caller needs to know how much to advance
 * 's' and also how many bytes to output via con_write_page.
 */
static int fill_con_write_page(const char *s, unsigned int n,
			       unsigned long *page_bytes)
{
	const char *orig_s = s;
	char *p = con_write_page;
	int left = PAGE_SIZE;

	while (n--) {
		if (*s == '\n') {
			if (left < 2)
				break;
			*p++ = '\r';
			left--;
		} else if (left < 1)
			break;
		*p++ = *s++;
		left--;
	}
	*page_bytes = p - con_write_page;
	return s - orig_s;
}

static void sunhv_console_write_paged(struct console *con, const char *s, unsigned n)
{
	struct uart_port *port = sunhv_port;
	unsigned long flags;
	int locked = 1;

	if (port->sysrq || oops_in_progress)
		locked = spin_trylock_irqsave(&port->lock, flags);
	else
		spin_lock_irqsave(&port->lock, flags);

	while (n > 0) {
		unsigned long ra = __pa(con_write_page);
		unsigned long page_bytes;
		unsigned int cpy = fill_con_write_page(s, n,
						       &page_bytes);

		n -= cpy;
		s += cpy;
		while (page_bytes > 0) {
			unsigned long written;
			int limit = 1000000;

			while (limit--) {
				unsigned long stat;

				stat = sun4v_con_write(ra, page_bytes,
						       &written);
				if (stat == HV_EOK)
					break;
				udelay(1);
			}
			if (limit < 0)
				break;
			page_bytes -= written;
			ra += written;
		}
	}

	if (locked)
		spin_unlock_irqrestore(&port->lock, flags);
}

static inline void sunhv_console_putchar(struct uart_port *port, char c)
{
	int limit = 1000000;

	while (limit-- > 0) {
		long status = sun4v_con_putchar(c);
		if (status == HV_EOK)
			break;
		udelay(1);
	}
}

static void sunhv_console_write_bychar(struct console *con, const char *s, unsigned n)
{
	struct uart_port *port = sunhv_port;
	unsigned long flags;
	int i, locked = 1;

	if (port->sysrq || oops_in_progress)
		locked = spin_trylock_irqsave(&port->lock, flags);
	else
		spin_lock_irqsave(&port->lock, flags);

	for (i = 0; i < n; i++) {
		if (*s == '\n')
			sunhv_console_putchar(port, '\r');
		sunhv_console_putchar(port, *s++);
	}

	if (locked)
		spin_unlock_irqrestore(&port->lock, flags);
}

static struct console sunhv_console = {
	.name	=	"ttyHV",
	.write	=	sunhv_console_write_bychar,
	.device	=	uart_console_device,
	.flags	=	CON_PRINTBUFFER,
	.index	=	-1,
	.data	=	&sunhv_reg,
};

static int hv_probe(struct platform_device *op)
{
	struct uart_port *port;
	unsigned long minor;
	int err;

	if (op->archdata.irqs[0] == 0xffffffff)
		return -ENODEV;

	port = kzalloc(sizeof(struct uart_port), GFP_KERNEL);
	if (unlikely(!port))
		return -ENOMEM;

	minor = 1;
	if (sun4v_hvapi_register(HV_GRP_CORE, 1, &minor) == 0 &&
	    minor >= 1) {
		err = -ENOMEM;
		con_write_page = kzalloc(PAGE_SIZE, GFP_KERNEL);
		if (!con_write_page)
			goto out_free_port;

		con_read_page = kzalloc(PAGE_SIZE, GFP_KERNEL);
		if (!con_read_page)
			goto out_free_con_write_page;

		sunhv_console.write = sunhv_console_write_paged;
		sunhv_ops = &bywrite_ops;
	}

	sunhv_port = port;

	port->line = 0;
	port->ops = &sunhv_pops;
	port->type = PORT_SUNHV;
	port->uartclk = ( 29491200 / 16 ); /* arbitrary */

	port->membase = (unsigned char __iomem *) __pa(port);

	port->irq = op->archdata.irqs[0];

	port->dev = &op->dev;

	err = sunserial_register_minors(&sunhv_reg, 1);
	if (err)
		goto out_free_con_read_page;

	sunserial_console_match(&sunhv_console, op->dev.of_node,
				&sunhv_reg, port->line, false);

	err = uart_add_one_port(&sunhv_reg, port);
	if (err)
		goto out_unregister_driver;

	err = request_irq(port->irq, sunhv_interrupt, 0, "hvcons", port);
	if (err)
		goto out_remove_port;

	platform_set_drvdata(op, port);

	return 0;

out_remove_port:
	uart_remove_one_port(&sunhv_reg, port);

out_unregister_driver:
	sunserial_unregister_minors(&sunhv_reg, 1);

out_free_con_read_page:
	kfree(con_read_page);

out_free_con_write_page:
	kfree(con_write_page);

out_free_port:
	kfree(port);
	sunhv_port = NULL;
	return err;
}

static int hv_remove(struct platform_device *dev)
{
	struct uart_port *port = platform_get_drvdata(dev);

	free_irq(port->irq, port);

	uart_remove_one_port(&sunhv_reg, port);

	sunserial_unregister_minors(&sunhv_reg, 1);

	kfree(port);
	sunhv_port = NULL;

	return 0;
}

static const struct of_device_id hv_match[] = {
	{
		.name = "console",
		.compatible = "qcn",
	},
	{
		.name = "console",
		.compatible = "SUNW,sun4v-console",
	},
	{},
};

static struct platform_driver hv_driver = {
	.driver = {
		.name = "hv",
		.of_match_table = hv_match,
	},
	.probe		= hv_probe,
	.remove		= hv_remove,
};

static int __init sunhv_init(void)
{
	if (tlb_type != hypervisor)
		return -ENODEV;

	return platform_driver_register(&hv_driver);
}
device_initcall(sunhv_init);

#if 0 /* ...def MODULE ; never supported as such */
MODULE_AUTHOR("David S. Miller");
MODULE_DESCRIPTION("SUN4V Hypervisor console driver");
MODULE_VERSION("2.0");
MODULE_LICENSE("GPL");
#endif
