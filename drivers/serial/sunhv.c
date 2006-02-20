/* sunhv.c: Serial driver for SUN4V hypervisor console.
 *
 * Copyright (C) 2006 David S. Miller (davem@davemloft.net)
 */

#include <linux/module.h>
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

#include <asm/hypervisor.h>
#include <asm/spitfire.h>
#include <asm/vdev.h>
#include <asm/oplib.h>
#include <asm/irq.h>

#if defined(CONFIG_MAGIC_SYSRQ)
#define SUPPORT_SYSRQ
#endif

#include <linux/serial_core.h>

#include "suncore.h"

#define CON_BREAK	((long)-1)
#define CON_HUP		((long)-2)

static inline long hypervisor_con_getchar(long *status)
{
	register unsigned long func asm("%o5");
	register unsigned long arg0 asm("%o0");
	register unsigned long arg1 asm("%o1");

	func = HV_FAST_CONS_GETCHAR;
	arg0 = 0;
	arg1 = 0;
	__asm__ __volatile__("ta	%6"
			     : "=&r" (func), "=&r" (arg0), "=&r" (arg1)
			     : "0" (func), "1" (arg0), "2" (arg1),
			       "i" (HV_FAST_TRAP));

	*status = arg0;

	return (long) arg1;
}

static inline long hypervisor_con_putchar(long ch)
{
	register unsigned long func asm("%o5");
	register unsigned long arg0 asm("%o0");

	func = HV_FAST_CONS_PUTCHAR;
	arg0 = ch;
	__asm__ __volatile__("ta	%4"
			     : "=&r" (func), "=&r" (arg0)
			     : "0" (func), "1" (arg0), "i" (HV_FAST_TRAP));

	return (long) arg0;
}

#define IGNORE_BREAK	0x1
#define IGNORE_ALL	0x2

static int hung_up = 0;

static struct tty_struct *receive_chars(struct uart_port *port, struct pt_regs *regs)
{
	struct tty_struct *tty = NULL;
	int saw_console_brk = 0;
	int limit = 10000;

	if (port->info != NULL)		/* Unopened serial console */
		tty = port->info->tty;

	while (limit-- > 0) {
		long status;
		long c = hypervisor_con_getchar(&status);
		unsigned char flag;

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

		if (tty == NULL) {
			uart_handle_sysrq_char(port, c, regs);
			continue;
		}

		flag = TTY_NORMAL;
		port->icount.rx++;
		if (c == CON_BREAK) {
			port->icount.brk++;
			if (uart_handle_break(port))
				continue;
			flag = TTY_BREAK;
		}

		if (uart_handle_sysrq_char(port, c, regs))
			continue;

		if ((port->ignore_status_mask & IGNORE_ALL) ||
		    ((port->ignore_status_mask & IGNORE_BREAK) &&
		     (c == CON_BREAK)))
			continue;

		tty_insert_flip_char(tty, c, flag);
	}

	if (saw_console_brk)
		sun_do_break();

	return tty;
}

static void transmit_chars(struct uart_port *port)
{
	struct circ_buf *xmit;

	if (!port->info)
		return;

	xmit = &port->info->xmit;
	if (uart_circ_empty(xmit) || uart_tx_stopped(port))
		return;

	while (!uart_circ_empty(xmit)) {
		long status = hypervisor_con_putchar(xmit->buf[xmit->tail]);

		if (status != HV_EOK)
			break;

		xmit->tail = (xmit->tail + 1) & (UART_XMIT_SIZE - 1);
		port->icount.tx++;
	}

	if (uart_circ_chars_pending(xmit) < WAKEUP_CHARS)
		uart_write_wakeup(port);
}

static irqreturn_t sunhv_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	struct uart_port *port = dev_id;
	struct tty_struct *tty;
	unsigned long flags;

	spin_lock_irqsave(&port->lock, flags);
	tty = receive_chars(port, regs);
	transmit_chars(port);
	spin_unlock_irqrestore(&port->lock, flags);

	if (tty)
		tty_flip_buffer_push(tty);

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
	struct circ_buf *xmit = &port->info->xmit;

	while (!uart_circ_empty(xmit)) {
		long status = hypervisor_con_putchar(xmit->buf[xmit->tail]);

		if (status != HV_EOK)
			break;

		xmit->tail = (xmit->tail + 1) & (UART_XMIT_SIZE - 1);
		port->icount.tx++;
	}
}

/* port->lock is not held.  */
static void sunhv_send_xchar(struct uart_port *port, char ch)
{
	unsigned long flags;
	int limit = 10000;

	spin_lock_irqsave(&port->lock, flags);

	while (limit-- > 0) {
		long status = hypervisor_con_putchar(ch);
		if (status == HV_EOK)
			break;
	}

	spin_unlock_irqrestore(&port->lock, flags);
}

/* port->lock held by caller.  */
static void sunhv_stop_rx(struct uart_port *port)
{
}

/* port->lock held by caller.  */
static void sunhv_enable_ms(struct uart_port *port)
{
}

/* port->lock is not held.  */
static void sunhv_break_ctl(struct uart_port *port, int break_state)
{
	if (break_state) {
		unsigned long flags;
		int limit = 1000000;

		spin_lock_irqsave(&port->lock, flags);

		while (limit-- > 0) {
			long status = hypervisor_con_putchar(CON_BREAK);
			if (status == HV_EOK)
				break;
			udelay(2);
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
static void sunhv_set_termios(struct uart_port *port, struct termios *termios,
			      struct termios *old)
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
	.enable_ms	= sunhv_enable_ms,
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
	.driver_name		= "serial",
	.devfs_name		= "tts/",
	.dev_name		= "ttyS",
	.major			= TTY_MAJOR,
};

static struct uart_port *sunhv_port;

static inline void sunhv_console_putchar(struct uart_port *port, char c)
{
	unsigned long flags;
	int limit = 1000000;

	spin_lock_irqsave(&port->lock, flags);

	while (limit-- > 0) {
		long status = hypervisor_con_putchar(c);
		if (status == HV_EOK)
			break;
		udelay(2);
	}

	spin_unlock_irqrestore(&port->lock, flags);
}

static void sunhv_console_write(struct console *con, const char *s, unsigned n)
{
	struct uart_port *port = sunhv_port;
	int i;

	for (i = 0; i < n; i++) {
		if (*s == '\n')
			sunhv_console_putchar(port, '\r');
		sunhv_console_putchar(port, *s++);
	}
}

static struct console sunhv_console = {
	.name	=	"ttyHV",
	.write	=	sunhv_console_write,
	.device	=	uart_console_device,
	.flags	=	CON_PRINTBUFFER,
	.index	=	-1,
	.data	=	&sunhv_reg,
};

static inline struct console *SUNHV_CONSOLE(void)
{
	if (con_is_present())
		return NULL;

	sunhv_console.index = 0;

	return &sunhv_console;
}

static int __init hv_console_compatible(char *buf, int len)
{
	while (len) {
		int this_len;

		if (!strcmp(buf, "qcn"))
			return 1;

		this_len = strlen(buf) + 1;

		buf += this_len;
		len -= this_len;
	}

	return 0;
}

static unsigned int __init get_interrupt(void)
{
	const char *cons_str = "console";
	const char *compat_str = "compatible";
	int node = prom_getchild(sun4v_vdev_root);
	char buf[64];
	int err, len;

	node = prom_searchsiblings(node, cons_str);
	if (!node)
		return 0;

	len = prom_getproplen(node, compat_str);
	if (len == 0 || len == -1)
		return 0;

	err = prom_getproperty(node, compat_str, buf, 64);
	if (err == -1)
		return 0;

	if (!hv_console_compatible(buf, len))
		return 0;

	/* Ok, the this is the OBP node for the sun4v hypervisor
	 * console device.  Decode the interrupt.
	 */
	return sun4v_vdev_device_interrupt(node);
}

static int __init sunhv_init(void)
{
	struct uart_port *port;
	int ret;

	if (tlb_type != hypervisor)
		return -ENODEV;

	port = kmalloc(sizeof(struct uart_port), GFP_KERNEL);
	if (unlikely(!port))
		return -ENOMEM;

	memset(port, 0, sizeof(struct uart_port));

	port->line = 0;
	port->ops = &sunhv_pops;
	port->type = PORT_SUNHV;
	port->uartclk = ( 29491200 / 16 ); /* arbitrary */

	/* Set this just to make uart_configure_port() happy.  */
	port->membase = (unsigned char __iomem *) __pa(port);

	port->irq = get_interrupt();
	if (!port->irq) {
		kfree(port);
		return -ENODEV;
	}

	sunhv_reg.minor = sunserial_current_minor;
	sunhv_reg.nr = 1;

	ret = uart_register_driver(&sunhv_reg);
	if (ret < 0) {
		printk(KERN_ERR "SUNHV: uart_register_driver() failed %d\n",
		       ret);
		kfree(port);

		return ret;
	}

	sunhv_reg.tty_driver->name_base = sunhv_reg.minor - 64;
	sunserial_current_minor += 1;

	sunhv_reg.cons = SUNHV_CONSOLE();

	sunhv_port = port;

	ret = uart_add_one_port(&sunhv_reg, port);
	if (ret < 0) {
		printk(KERN_ERR "SUNHV: uart_add_one_port() failed %d\n", ret);
		sunserial_current_minor -= 1;
		uart_unregister_driver(&sunhv_reg);
		kfree(port);
		sunhv_port = NULL;
		return -ENODEV;
	}

	if (request_irq(port->irq, sunhv_interrupt,
			SA_SHIRQ, "serial(sunhv)", port)) {
		printk(KERN_ERR "sunhv: Cannot register IRQ\n");
		uart_remove_one_port(&sunhv_reg, port);
		sunserial_current_minor -= 1;
		uart_unregister_driver(&sunhv_reg);
		kfree(port);
		sunhv_port = NULL;
		return -ENODEV;
	}

	return 0;
}

static void __exit sunhv_exit(void)
{
	struct uart_port *port = sunhv_port;

	BUG_ON(!port);

	free_irq(port->irq, port);

	uart_remove_one_port(&sunhv_reg, port);
	sunserial_current_minor -= 1;

	uart_unregister_driver(&sunhv_reg);

	kfree(sunhv_port);
	sunhv_port = NULL;
}

module_init(sunhv_init);
module_exit(sunhv_exit);

MODULE_AUTHOR("David S. Miller");
MODULE_DESCRIPTION("SUN4V Hypervisor console driver")
MODULE_LICENSE("GPL");
