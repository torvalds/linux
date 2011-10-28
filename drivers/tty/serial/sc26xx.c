/*
 * SC268xx.c: Serial driver for Philiphs SC2681/SC2692 devices.
 *
 * Copyright (C) 2006,2007 Thomas Bogendörfer (tsbogend@alpha.franken.de)
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
#include <linux/platform_device.h>
#include <linux/irq.h>

#if defined(CONFIG_MAGIC_SYSRQ)
#define SUPPORT_SYSRQ
#endif

#include <linux/serial_core.h>

#define SC26XX_MAJOR         204
#define SC26XX_MINOR_START   205
#define SC26XX_NR            2

struct uart_sc26xx_port {
	struct uart_port      port[2];
	u8     dsr_mask[2];
	u8     cts_mask[2];
	u8     dcd_mask[2];
	u8     ri_mask[2];
	u8     dtr_mask[2];
	u8     rts_mask[2];
	u8     imr;
};

/* register common to both ports */
#define RD_ISR      0x14
#define RD_IPR      0x34

#define WR_ACR      0x10
#define WR_IMR      0x14
#define WR_OPCR     0x34
#define WR_OPR_SET  0x38
#define WR_OPR_CLR  0x3C

/* access common register */
#define READ_SC(p, r)        readb((p)->membase + RD_##r)
#define WRITE_SC(p, r, v)    writeb((v), (p)->membase + WR_##r)

/* register per port */
#define RD_PORT_MRx 0x00
#define RD_PORT_SR  0x04
#define RD_PORT_RHR 0x0c

#define WR_PORT_MRx 0x00
#define WR_PORT_CSR 0x04
#define WR_PORT_CR  0x08
#define WR_PORT_THR 0x0c

/* SR bits */
#define SR_BREAK    (1 << 7)
#define SR_FRAME    (1 << 6)
#define SR_PARITY   (1 << 5)
#define SR_OVERRUN  (1 << 4)
#define SR_TXRDY    (1 << 2)
#define SR_RXRDY    (1 << 0)

#define CR_RES_MR   (1 << 4)
#define CR_RES_RX   (2 << 4)
#define CR_RES_TX   (3 << 4)
#define CR_STRT_BRK (6 << 4)
#define CR_STOP_BRK (7 << 4)
#define CR_DIS_TX   (1 << 3)
#define CR_ENA_TX   (1 << 2)
#define CR_DIS_RX   (1 << 1)
#define CR_ENA_RX   (1 << 0)

/* ISR bits */
#define ISR_RXRDYB  (1 << 5)
#define ISR_TXRDYB  (1 << 4)
#define ISR_RXRDYA  (1 << 1)
#define ISR_TXRDYA  (1 << 0)

/* IMR bits */
#define IMR_RXRDY   (1 << 1)
#define IMR_TXRDY   (1 << 0)

/* access port register */
static inline u8 read_sc_port(struct uart_port *p, u8 reg)
{
	return readb(p->membase + p->line * 0x20 + reg);
}

static inline void write_sc_port(struct uart_port *p, u8 reg, u8 val)
{
	writeb(val, p->membase + p->line * 0x20 + reg);
}

#define READ_SC_PORT(p, r)     read_sc_port(p, RD_PORT_##r)
#define WRITE_SC_PORT(p, r, v) write_sc_port(p, WR_PORT_##r, v)

static void sc26xx_enable_irq(struct uart_port *port, int mask)
{
	struct uart_sc26xx_port *up;
	int line = port->line;

	port -= line;
	up = container_of(port, struct uart_sc26xx_port, port[0]);

	up->imr |= mask << (line * 4);
	WRITE_SC(port, IMR, up->imr);
}

static void sc26xx_disable_irq(struct uart_port *port, int mask)
{
	struct uart_sc26xx_port *up;
	int line = port->line;

	port -= line;
	up = container_of(port, struct uart_sc26xx_port, port[0]);

	up->imr &= ~(mask << (line * 4));
	WRITE_SC(port, IMR, up->imr);
}

static struct tty_struct *receive_chars(struct uart_port *port)
{
	struct tty_struct *tty = NULL;
	int limit = 10000;
	unsigned char ch;
	char flag;
	u8 status;

	if (port->state != NULL)		/* Unopened serial console */
		tty = port->state->port.tty;

	while (limit-- > 0) {
		status = READ_SC_PORT(port, SR);
		if (!(status & SR_RXRDY))
			break;
		ch = READ_SC_PORT(port, RHR);

		flag = TTY_NORMAL;
		port->icount.rx++;

		if (unlikely(status & (SR_BREAK | SR_FRAME |
				       SR_PARITY | SR_OVERRUN))) {
			if (status & SR_BREAK) {
				status &= ~(SR_PARITY | SR_FRAME);
				port->icount.brk++;
				if (uart_handle_break(port))
					continue;
			} else if (status & SR_PARITY)
				port->icount.parity++;
			else if (status & SR_FRAME)
				port->icount.frame++;
			if (status & SR_OVERRUN)
				port->icount.overrun++;

			status &= port->read_status_mask;
			if (status & SR_BREAK)
				flag = TTY_BREAK;
			else if (status & SR_PARITY)
				flag = TTY_PARITY;
			else if (status & SR_FRAME)
				flag = TTY_FRAME;
		}

		if (uart_handle_sysrq_char(port, ch))
			continue;

		if (status & port->ignore_status_mask)
			continue;

		tty_insert_flip_char(tty, ch, flag);
	}
	return tty;
}

static void transmit_chars(struct uart_port *port)
{
	struct circ_buf *xmit;

	if (!port->state)
		return;

	xmit = &port->state->xmit;
	if (uart_circ_empty(xmit) || uart_tx_stopped(port)) {
		sc26xx_disable_irq(port, IMR_TXRDY);
		return;
	}
	while (!uart_circ_empty(xmit)) {
		if (!(READ_SC_PORT(port, SR) & SR_TXRDY))
			break;

		WRITE_SC_PORT(port, THR, xmit->buf[xmit->tail]);
		xmit->tail = (xmit->tail + 1) & (UART_XMIT_SIZE - 1);
		port->icount.tx++;
	}
	if (uart_circ_chars_pending(xmit) < WAKEUP_CHARS)
		uart_write_wakeup(port);
}

static irqreturn_t sc26xx_interrupt(int irq, void *dev_id)
{
	struct uart_sc26xx_port *up = dev_id;
	struct tty_struct *tty;
	unsigned long flags;
	u8 isr;

	spin_lock_irqsave(&up->port[0].lock, flags);

	tty = NULL;
	isr = READ_SC(&up->port[0], ISR);
	if (isr & ISR_TXRDYA)
	    transmit_chars(&up->port[0]);
	if (isr & ISR_RXRDYA)
	    tty = receive_chars(&up->port[0]);

	spin_unlock(&up->port[0].lock);

	if (tty)
		tty_flip_buffer_push(tty);

	spin_lock(&up->port[1].lock);

	tty = NULL;
	if (isr & ISR_TXRDYB)
	    transmit_chars(&up->port[1]);
	if (isr & ISR_RXRDYB)
	    tty = receive_chars(&up->port[1]);

	spin_unlock_irqrestore(&up->port[1].lock, flags);

	if (tty)
		tty_flip_buffer_push(tty);

	return IRQ_HANDLED;
}

/* port->lock is not held.  */
static unsigned int sc26xx_tx_empty(struct uart_port *port)
{
	return (READ_SC_PORT(port, SR) & SR_TXRDY) ? TIOCSER_TEMT : 0;
}

/* port->lock held by caller.  */
static void sc26xx_set_mctrl(struct uart_port *port, unsigned int mctrl)
{
	struct uart_sc26xx_port *up;
	int line = port->line;

	port -= line;
	up = container_of(port, struct uart_sc26xx_port, port[0]);

	if (up->dtr_mask[line]) {
		if (mctrl & TIOCM_DTR)
			WRITE_SC(port, OPR_SET, up->dtr_mask[line]);
		else
			WRITE_SC(port, OPR_CLR, up->dtr_mask[line]);
	}
	if (up->rts_mask[line]) {
		if (mctrl & TIOCM_RTS)
			WRITE_SC(port, OPR_SET, up->rts_mask[line]);
		else
			WRITE_SC(port, OPR_CLR, up->rts_mask[line]);
	}
}

/* port->lock is held by caller and interrupts are disabled.  */
static unsigned int sc26xx_get_mctrl(struct uart_port *port)
{
	struct uart_sc26xx_port *up;
	int line = port->line;
	unsigned int mctrl = TIOCM_DSR | TIOCM_CTS | TIOCM_CAR;
	u8 ipr;

	port -= line;
	up = container_of(port, struct uart_sc26xx_port, port[0]);
	ipr = READ_SC(port, IPR) ^ 0xff;

	if (up->dsr_mask[line]) {
		mctrl &= ~TIOCM_DSR;
		mctrl |= ipr & up->dsr_mask[line] ? TIOCM_DSR : 0;
	}
	if (up->cts_mask[line]) {
		mctrl &= ~TIOCM_CTS;
		mctrl |= ipr & up->cts_mask[line] ? TIOCM_CTS : 0;
	}
	if (up->dcd_mask[line]) {
		mctrl &= ~TIOCM_CAR;
		mctrl |= ipr & up->dcd_mask[line] ? TIOCM_CAR : 0;
	}
	if (up->ri_mask[line]) {
		mctrl &= ~TIOCM_RNG;
		mctrl |= ipr & up->ri_mask[line] ? TIOCM_RNG : 0;
	}
	return mctrl;
}

/* port->lock held by caller.  */
static void sc26xx_stop_tx(struct uart_port *port)
{
	return;
}

/* port->lock held by caller.  */
static void sc26xx_start_tx(struct uart_port *port)
{
	struct circ_buf *xmit = &port->state->xmit;

	while (!uart_circ_empty(xmit)) {
		if (!(READ_SC_PORT(port, SR) & SR_TXRDY)) {
			sc26xx_enable_irq(port, IMR_TXRDY);
			break;
		}
		WRITE_SC_PORT(port, THR, xmit->buf[xmit->tail]);
		xmit->tail = (xmit->tail + 1) & (UART_XMIT_SIZE - 1);
		port->icount.tx++;
	}
}

/* port->lock held by caller.  */
static void sc26xx_stop_rx(struct uart_port *port)
{
}

/* port->lock held by caller.  */
static void sc26xx_enable_ms(struct uart_port *port)
{
}

/* port->lock is not held.  */
static void sc26xx_break_ctl(struct uart_port *port, int break_state)
{
	if (break_state == -1)
		WRITE_SC_PORT(port, CR, CR_STRT_BRK);
	else
		WRITE_SC_PORT(port, CR, CR_STOP_BRK);
}

/* port->lock is not held.  */
static int sc26xx_startup(struct uart_port *port)
{
	sc26xx_disable_irq(port, IMR_TXRDY | IMR_RXRDY);
	WRITE_SC(port, OPCR, 0);

	/* reset tx and rx */
	WRITE_SC_PORT(port, CR, CR_RES_RX);
	WRITE_SC_PORT(port, CR, CR_RES_TX);

	/* start rx/tx */
	WRITE_SC_PORT(port, CR, CR_ENA_TX | CR_ENA_RX);

	/* enable irqs */
	sc26xx_enable_irq(port, IMR_RXRDY);
	return 0;
}

/* port->lock is not held.  */
static void sc26xx_shutdown(struct uart_port *port)
{
	/* disable interrupst */
	sc26xx_disable_irq(port, IMR_TXRDY | IMR_RXRDY);

	/* stop tx/rx */
	WRITE_SC_PORT(port, CR, CR_DIS_TX | CR_DIS_RX);
}

/* port->lock is not held.  */
static void sc26xx_set_termios(struct uart_port *port, struct ktermios *termios,
			      struct ktermios *old)
{
	unsigned int baud = uart_get_baud_rate(port, termios, old, 0, 4000000);
	unsigned int quot = uart_get_divisor(port, baud);
	unsigned int iflag, cflag;
	unsigned long flags;
	u8 mr1, mr2, csr;

	spin_lock_irqsave(&port->lock, flags);

	while ((READ_SC_PORT(port, SR) & ((1 << 3) | (1 << 2))) != 0xc)
		udelay(2);

	WRITE_SC_PORT(port, CR, CR_DIS_TX | CR_DIS_RX);

	iflag = termios->c_iflag;
	cflag = termios->c_cflag;

	port->read_status_mask = SR_OVERRUN;
	if (iflag & INPCK)
		port->read_status_mask |= SR_PARITY | SR_FRAME;
	if (iflag & (BRKINT | PARMRK))
		port->read_status_mask |= SR_BREAK;

	port->ignore_status_mask = 0;
	if (iflag & IGNBRK)
		port->ignore_status_mask |= SR_BREAK;
	if ((cflag & CREAD) == 0)
		port->ignore_status_mask |= SR_BREAK | SR_FRAME |
					    SR_PARITY | SR_OVERRUN;

	switch (cflag & CSIZE) {
	case CS5:
		mr1 = 0x00;
		break;
	case CS6:
		mr1 = 0x01;
		break;
	case CS7:
		mr1 = 0x02;
		break;
	default:
	case CS8:
		mr1 = 0x03;
		break;
	}
	mr2 = 0x07;
	if (cflag & CSTOPB)
		mr2 = 0x0f;
	if (cflag & PARENB) {
		if (cflag & PARODD)
			mr1 |= (1 << 2);
	} else
		mr1 |= (2 << 3);

	switch (baud) {
	case 50:
		csr = 0x00;
		break;
	case 110:
		csr = 0x11;
		break;
	case 134:
		csr = 0x22;
		break;
	case 200:
		csr = 0x33;
		break;
	case 300:
		csr = 0x44;
		break;
	case 600:
		csr = 0x55;
		break;
	case 1200:
		csr = 0x66;
		break;
	case 2400:
		csr = 0x88;
		break;
	case 4800:
		csr = 0x99;
		break;
	default:
	case 9600:
		csr = 0xbb;
		break;
	case 19200:
		csr = 0xcc;
		break;
	}

	WRITE_SC_PORT(port, CR, CR_RES_MR);
	WRITE_SC_PORT(port, MRx, mr1);
	WRITE_SC_PORT(port, MRx, mr2);

	WRITE_SC(port, ACR, 0x80);
	WRITE_SC_PORT(port, CSR, csr);

	/* reset tx and rx */
	WRITE_SC_PORT(port, CR, CR_RES_RX);
	WRITE_SC_PORT(port, CR, CR_RES_TX);

	WRITE_SC_PORT(port, CR, CR_ENA_TX | CR_ENA_RX);
	while ((READ_SC_PORT(port, SR) & ((1 << 3) | (1 << 2))) != 0xc)
		udelay(2);

	/* XXX */
	uart_update_timeout(port, cflag,
			    (port->uartclk / (16 * quot)));

	spin_unlock_irqrestore(&port->lock, flags);
}

static const char *sc26xx_type(struct uart_port *port)
{
	return "SC26XX";
}

static void sc26xx_release_port(struct uart_port *port)
{
}

static int sc26xx_request_port(struct uart_port *port)
{
	return 0;
}

static void sc26xx_config_port(struct uart_port *port, int flags)
{
}

static int sc26xx_verify_port(struct uart_port *port, struct serial_struct *ser)
{
	return -EINVAL;
}

static struct uart_ops sc26xx_ops = {
	.tx_empty	= sc26xx_tx_empty,
	.set_mctrl	= sc26xx_set_mctrl,
	.get_mctrl	= sc26xx_get_mctrl,
	.stop_tx	= sc26xx_stop_tx,
	.start_tx	= sc26xx_start_tx,
	.stop_rx	= sc26xx_stop_rx,
	.enable_ms	= sc26xx_enable_ms,
	.break_ctl	= sc26xx_break_ctl,
	.startup	= sc26xx_startup,
	.shutdown	= sc26xx_shutdown,
	.set_termios	= sc26xx_set_termios,
	.type		= sc26xx_type,
	.release_port	= sc26xx_release_port,
	.request_port	= sc26xx_request_port,
	.config_port	= sc26xx_config_port,
	.verify_port	= sc26xx_verify_port,
};

static struct uart_port *sc26xx_port;

#ifdef CONFIG_SERIAL_SC26XX_CONSOLE
static void sc26xx_console_putchar(struct uart_port *port, char c)
{
	unsigned long flags;
	int limit = 1000000;

	spin_lock_irqsave(&port->lock, flags);

	while (limit-- > 0) {
		if (READ_SC_PORT(port, SR) & SR_TXRDY) {
			WRITE_SC_PORT(port, THR, c);
			break;
		}
		udelay(2);
	}

	spin_unlock_irqrestore(&port->lock, flags);
}

static void sc26xx_console_write(struct console *con, const char *s, unsigned n)
{
	struct uart_port *port = sc26xx_port;
	int i;

	for (i = 0; i < n; i++) {
		if (*s == '\n')
			sc26xx_console_putchar(port, '\r');
		sc26xx_console_putchar(port, *s++);
	}
}

static int __init sc26xx_console_setup(struct console *con, char *options)
{
	struct uart_port *port = sc26xx_port;
	int baud = 9600;
	int bits = 8;
	int parity = 'n';
	int flow = 'n';

	if (port->type != PORT_SC26XX)
		return -1;

	printk(KERN_INFO "Console: ttySC%d (SC26XX)\n", con->index);
	if (options)
		uart_parse_options(options, &baud, &parity, &bits, &flow);

	return uart_set_options(port, con, baud, parity, bits, flow);
}

static struct uart_driver sc26xx_reg;
static struct console sc26xx_console = {
	.name	=	"ttySC",
	.write	=	sc26xx_console_write,
	.device	=	uart_console_device,
	.setup  =       sc26xx_console_setup,
	.flags	=	CON_PRINTBUFFER,
	.index	=	-1,
	.data	=	&sc26xx_reg,
};
#define SC26XX_CONSOLE   &sc26xx_console
#else
#define SC26XX_CONSOLE   NULL
#endif

static struct uart_driver sc26xx_reg = {
	.owner			= THIS_MODULE,
	.driver_name		= "SC26xx",
	.dev_name		= "ttySC",
	.major			= SC26XX_MAJOR,
	.minor			= SC26XX_MINOR_START,
	.nr			= SC26XX_NR,
	.cons                   = SC26XX_CONSOLE,
};

static u8 sc26xx_flags2mask(unsigned int flags, unsigned int bitpos)
{
	unsigned int bit = (flags >> bitpos) & 15;

	return bit ? (1 << (bit - 1)) : 0;
}

static void __devinit sc26xx_init_masks(struct uart_sc26xx_port *up,
					int line, unsigned int data)
{
	up->dtr_mask[line] = sc26xx_flags2mask(data,  0);
	up->rts_mask[line] = sc26xx_flags2mask(data,  4);
	up->dsr_mask[line] = sc26xx_flags2mask(data,  8);
	up->cts_mask[line] = sc26xx_flags2mask(data, 12);
	up->dcd_mask[line] = sc26xx_flags2mask(data, 16);
	up->ri_mask[line]  = sc26xx_flags2mask(data, 20);
}

static int __devinit sc26xx_probe(struct platform_device *dev)
{
	struct resource *res;
	struct uart_sc26xx_port *up;
	unsigned int *sc26xx_data = dev->dev.platform_data;
	int err;

	res = platform_get_resource(dev, IORESOURCE_MEM, 0);
	if (!res)
		return -ENODEV;

	up = kzalloc(sizeof *up, GFP_KERNEL);
	if (unlikely(!up))
		return -ENOMEM;

	up->port[0].line = 0;
	up->port[0].ops = &sc26xx_ops;
	up->port[0].type = PORT_SC26XX;
	up->port[0].uartclk = (29491200 / 16); /* arbitrary */

	up->port[0].mapbase = res->start;
	up->port[0].membase = ioremap_nocache(up->port[0].mapbase, 0x40);
	up->port[0].iotype = UPIO_MEM;
	up->port[0].irq = platform_get_irq(dev, 0);

	up->port[0].dev = &dev->dev;

	sc26xx_init_masks(up, 0, sc26xx_data[0]);

	sc26xx_port = &up->port[0];

	up->port[1].line = 1;
	up->port[1].ops = &sc26xx_ops;
	up->port[1].type = PORT_SC26XX;
	up->port[1].uartclk = (29491200 / 16); /* arbitrary */

	up->port[1].mapbase = up->port[0].mapbase;
	up->port[1].membase = up->port[0].membase;
	up->port[1].iotype = UPIO_MEM;
	up->port[1].irq = up->port[0].irq;

	up->port[1].dev = &dev->dev;

	sc26xx_init_masks(up, 1, sc26xx_data[1]);

	err = uart_register_driver(&sc26xx_reg);
	if (err)
		goto out_free_port;

	sc26xx_reg.tty_driver->name_base = sc26xx_reg.minor;

	err = uart_add_one_port(&sc26xx_reg, &up->port[0]);
	if (err)
		goto out_unregister_driver;

	err = uart_add_one_port(&sc26xx_reg, &up->port[1]);
	if (err)
		goto out_remove_port0;

	err = request_irq(up->port[0].irq, sc26xx_interrupt, 0, "sc26xx", up);
	if (err)
		goto out_remove_ports;

	dev_set_drvdata(&dev->dev, up);
	return 0;

out_remove_ports:
	uart_remove_one_port(&sc26xx_reg, &up->port[1]);
out_remove_port0:
	uart_remove_one_port(&sc26xx_reg, &up->port[0]);

out_unregister_driver:
	uart_unregister_driver(&sc26xx_reg);

out_free_port:
	kfree(up);
	sc26xx_port = NULL;
	return err;
}


static int __exit sc26xx_driver_remove(struct platform_device *dev)
{
	struct uart_sc26xx_port *up = dev_get_drvdata(&dev->dev);

	free_irq(up->port[0].irq, up);

	uart_remove_one_port(&sc26xx_reg, &up->port[0]);
	uart_remove_one_port(&sc26xx_reg, &up->port[1]);

	uart_unregister_driver(&sc26xx_reg);

	kfree(up);
	sc26xx_port = NULL;

	dev_set_drvdata(&dev->dev, NULL);
	return 0;
}

static struct platform_driver sc26xx_driver = {
	.probe	= sc26xx_probe,
	.remove	= __devexit_p(sc26xx_driver_remove),
	.driver	= {
		.name	= "SC26xx",
		.owner	= THIS_MODULE,
	},
};

static int __init sc26xx_init(void)
{
	return platform_driver_register(&sc26xx_driver);
}

static void __exit sc26xx_exit(void)
{
	platform_driver_unregister(&sc26xx_driver);
}

module_init(sc26xx_init);
module_exit(sc26xx_exit);


MODULE_AUTHOR("Thomas Bogendörfer");
MODULE_DESCRIPTION("SC681/SC2692 serial driver");
MODULE_VERSION("1.0");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:SC26xx");
