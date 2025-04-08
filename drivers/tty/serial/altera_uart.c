// SPDX-License-Identifier: GPL-2.0+
/*
 * altera_uart.c -- Altera UART driver
 *
 * Based on mcf.c -- Freescale ColdFire UART driver
 *
 * (C) Copyright 2003-2007, Greg Ungerer <gerg@snapgear.com>
 * (C) Copyright 2008, Thomas Chou <thomas@wytron.com.tw>
 * (C) Copyright 2010, Tobias Klauser <tklauser@distanz.ch>
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/timer.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/console.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/serial.h>
#include <linux/serial_core.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/io.h>
#include <linux/altera_uart.h>

#define SERIAL_ALTERA_MAJOR 204
#define SERIAL_ALTERA_MINOR 213

/*
 * Altera UART register definitions according to the Nios UART datasheet:
 * http://www.altera.com/literature/ds/ds_nios_uart.pdf
 */

#define ALTERA_UART_SIZE		32

#define ALTERA_UART_RXDATA_REG		0
#define ALTERA_UART_TXDATA_REG		4
#define ALTERA_UART_STATUS_REG		8
#define ALTERA_UART_CONTROL_REG		12
#define ALTERA_UART_DIVISOR_REG		16
#define ALTERA_UART_EOP_REG		20

#define ALTERA_UART_STATUS_PE_MSK	0x0001	/* parity error */
#define ALTERA_UART_STATUS_FE_MSK	0x0002	/* framing error */
#define ALTERA_UART_STATUS_BRK_MSK	0x0004	/* break */
#define ALTERA_UART_STATUS_ROE_MSK	0x0008	/* RX overrun error */
#define ALTERA_UART_STATUS_TOE_MSK	0x0010	/* TX overrun error */
#define ALTERA_UART_STATUS_TMT_MSK	0x0020	/* TX shift register state */
#define ALTERA_UART_STATUS_TRDY_MSK	0x0040	/* TX ready */
#define ALTERA_UART_STATUS_RRDY_MSK	0x0080	/* RX ready */
#define ALTERA_UART_STATUS_E_MSK	0x0100	/* exception condition */
#define ALTERA_UART_STATUS_DCTS_MSK	0x0400	/* CTS logic-level change */
#define ALTERA_UART_STATUS_CTS_MSK	0x0800	/* CTS logic state */
#define ALTERA_UART_STATUS_EOP_MSK	0x1000	/* EOP written/read */

						/* Enable interrupt on... */
#define ALTERA_UART_CONTROL_PE_MSK	0x0001	/* ...parity error */
#define ALTERA_UART_CONTROL_FE_MSK	0x0002	/* ...framing error */
#define ALTERA_UART_CONTROL_BRK_MSK	0x0004	/* ...break */
#define ALTERA_UART_CONTROL_ROE_MSK	0x0008	/* ...RX overrun */
#define ALTERA_UART_CONTROL_TOE_MSK	0x0010	/* ...TX overrun */
#define ALTERA_UART_CONTROL_TMT_MSK	0x0020	/* ...TX shift register empty */
#define ALTERA_UART_CONTROL_TRDY_MSK	0x0040	/* ...TX ready */
#define ALTERA_UART_CONTROL_RRDY_MSK	0x0080	/* ...RX ready */
#define ALTERA_UART_CONTROL_E_MSK	0x0100	/* ...exception*/

#define ALTERA_UART_CONTROL_TRBK_MSK	0x0200	/* TX break */
#define ALTERA_UART_CONTROL_DCTS_MSK	0x0400	/* Interrupt on CTS change */
#define ALTERA_UART_CONTROL_RTS_MSK	0x0800	/* RTS signal */
#define ALTERA_UART_CONTROL_EOP_MSK	0x1000	/* Interrupt on EOP */

/*
 * Local per-uart structure.
 */
struct altera_uart {
	struct uart_port port;
	struct timer_list tmr;
	unsigned int sigs;	/* Local copy of line sigs */
	unsigned short imr;	/* Local IMR mirror */
};

static u32 altera_uart_readl(struct uart_port *port, int reg)
{
	return readl(port->membase + (reg << port->regshift));
}

static void altera_uart_writel(struct uart_port *port, u32 dat, int reg)
{
	writel(dat, port->membase + (reg << port->regshift));
}

static unsigned int altera_uart_tx_empty(struct uart_port *port)
{
	return (altera_uart_readl(port, ALTERA_UART_STATUS_REG) &
		ALTERA_UART_STATUS_TMT_MSK) ? TIOCSER_TEMT : 0;
}

static unsigned int altera_uart_get_mctrl(struct uart_port *port)
{
	struct altera_uart *pp = container_of(port, struct altera_uart, port);
	unsigned int sigs;

	sigs = (altera_uart_readl(port, ALTERA_UART_STATUS_REG) &
	     ALTERA_UART_STATUS_CTS_MSK) ? TIOCM_CTS : 0;
	sigs |= (pp->sigs & TIOCM_RTS);

	return sigs;
}

static void altera_uart_update_ctrl_reg(struct altera_uart *pp)
{
	unsigned short imr = pp->imr;

	/*
	 * If the device doesn't have an irq, ensure that the irq bits are
	 * masked out to keep the irq line inactive.
	 */
	if (!pp->port.irq)
		imr &= ALTERA_UART_CONTROL_TRBK_MSK | ALTERA_UART_CONTROL_RTS_MSK;

	altera_uart_writel(&pp->port, imr, ALTERA_UART_CONTROL_REG);
}

static void altera_uart_set_mctrl(struct uart_port *port, unsigned int sigs)
{
	struct altera_uart *pp = container_of(port, struct altera_uart, port);

	pp->sigs = sigs;
	if (sigs & TIOCM_RTS)
		pp->imr |= ALTERA_UART_CONTROL_RTS_MSK;
	else
		pp->imr &= ~ALTERA_UART_CONTROL_RTS_MSK;
	altera_uart_update_ctrl_reg(pp);
}

static void altera_uart_start_tx(struct uart_port *port)
{
	struct altera_uart *pp = container_of(port, struct altera_uart, port);

	pp->imr |= ALTERA_UART_CONTROL_TRDY_MSK;
	altera_uart_update_ctrl_reg(pp);
}

static void altera_uart_stop_tx(struct uart_port *port)
{
	struct altera_uart *pp = container_of(port, struct altera_uart, port);

	pp->imr &= ~ALTERA_UART_CONTROL_TRDY_MSK;
	altera_uart_update_ctrl_reg(pp);
}

static void altera_uart_stop_rx(struct uart_port *port)
{
	struct altera_uart *pp = container_of(port, struct altera_uart, port);

	pp->imr &= ~ALTERA_UART_CONTROL_RRDY_MSK;
	altera_uart_update_ctrl_reg(pp);
}

static void altera_uart_break_ctl(struct uart_port *port, int break_state)
{
	struct altera_uart *pp = container_of(port, struct altera_uart, port);
	unsigned long flags;

	uart_port_lock_irqsave(port, &flags);
	if (break_state == -1)
		pp->imr |= ALTERA_UART_CONTROL_TRBK_MSK;
	else
		pp->imr &= ~ALTERA_UART_CONTROL_TRBK_MSK;
	altera_uart_update_ctrl_reg(pp);
	uart_port_unlock_irqrestore(port, flags);
}

static void altera_uart_set_termios(struct uart_port *port,
				    struct ktermios *termios,
				    const struct ktermios *old)
{
	unsigned long flags;
	unsigned int baud, baudclk;

	baud = uart_get_baud_rate(port, termios, old, 0, 4000000);
	baudclk = port->uartclk / baud;

	if (old)
		tty_termios_copy_hw(termios, old);
	tty_termios_encode_baud_rate(termios, baud, baud);

	uart_port_lock_irqsave(port, &flags);
	uart_update_timeout(port, termios->c_cflag, baud);
	altera_uart_writel(port, baudclk, ALTERA_UART_DIVISOR_REG);
	uart_port_unlock_irqrestore(port, flags);

	/*
	 * FIXME: port->read_status_mask and port->ignore_status_mask
	 * need to be initialized based on termios settings for
	 * INPCK, IGNBRK, IGNPAR, PARMRK, BRKINT
	 */
}

static void altera_uart_rx_chars(struct uart_port *port)
{
	unsigned short status;
	u8 ch, flag;

	while ((status = altera_uart_readl(port, ALTERA_UART_STATUS_REG)) &
	       ALTERA_UART_STATUS_RRDY_MSK) {
		ch = altera_uart_readl(port, ALTERA_UART_RXDATA_REG);
		flag = TTY_NORMAL;
		port->icount.rx++;

		if (status & ALTERA_UART_STATUS_E_MSK) {
			altera_uart_writel(port, status,
					   ALTERA_UART_STATUS_REG);

			if (status & ALTERA_UART_STATUS_BRK_MSK) {
				port->icount.brk++;
				if (uart_handle_break(port))
					continue;
			} else if (status & ALTERA_UART_STATUS_PE_MSK) {
				port->icount.parity++;
			} else if (status & ALTERA_UART_STATUS_ROE_MSK) {
				port->icount.overrun++;
			} else if (status & ALTERA_UART_STATUS_FE_MSK) {
				port->icount.frame++;
			}

			status &= port->read_status_mask;

			if (status & ALTERA_UART_STATUS_BRK_MSK)
				flag = TTY_BREAK;
			else if (status & ALTERA_UART_STATUS_PE_MSK)
				flag = TTY_PARITY;
			else if (status & ALTERA_UART_STATUS_FE_MSK)
				flag = TTY_FRAME;
		}

		if (uart_handle_sysrq_char(port, ch))
			continue;
		uart_insert_char(port, status, ALTERA_UART_STATUS_ROE_MSK, ch,
				 flag);
	}

	tty_flip_buffer_push(&port->state->port);
}

static void altera_uart_tx_chars(struct uart_port *port)
{
	u8 ch;

	uart_port_tx(port, ch,
		altera_uart_readl(port, ALTERA_UART_STATUS_REG) &
		                ALTERA_UART_STATUS_TRDY_MSK,
		altera_uart_writel(port, ch, ALTERA_UART_TXDATA_REG));
}

static irqreturn_t altera_uart_interrupt(int irq, void *data)
{
	struct uart_port *port = data;
	struct altera_uart *pp = container_of(port, struct altera_uart, port);
	unsigned long flags;
	unsigned int isr;

	isr = altera_uart_readl(port, ALTERA_UART_STATUS_REG) & pp->imr;

	uart_port_lock_irqsave(port, &flags);
	if (isr & ALTERA_UART_STATUS_RRDY_MSK)
		altera_uart_rx_chars(port);
	if (isr & ALTERA_UART_STATUS_TRDY_MSK)
		altera_uart_tx_chars(port);
	uart_port_unlock_irqrestore(port, flags);

	return IRQ_RETVAL(isr);
}

static void altera_uart_timer(struct timer_list *t)
{
	struct altera_uart *pp = from_timer(pp, t, tmr);
	struct uart_port *port = &pp->port;

	altera_uart_interrupt(0, port);
	mod_timer(&pp->tmr, jiffies + uart_poll_timeout(port));
}

static void altera_uart_config_port(struct uart_port *port, int flags)
{
	port->type = PORT_ALTERA_UART;

	/* Clear mask, so no surprise interrupts. */
	altera_uart_writel(port, 0, ALTERA_UART_CONTROL_REG);
	/* Clear status register */
	altera_uart_writel(port, 0, ALTERA_UART_STATUS_REG);
}

static int altera_uart_startup(struct uart_port *port)
{
	struct altera_uart *pp = container_of(port, struct altera_uart, port);
	unsigned long flags;

	if (!port->irq) {
		timer_setup(&pp->tmr, altera_uart_timer, 0);
		mod_timer(&pp->tmr, jiffies + uart_poll_timeout(port));
	} else {
		int ret;

		ret = request_irq(port->irq, altera_uart_interrupt, 0,
				dev_name(port->dev), port);
		if (ret) {
			dev_err(port->dev, "unable to attach Altera UART %d interrupt vector=%d\n",
				port->line, port->irq);
			return ret;
		}
	}

	uart_port_lock_irqsave(port, &flags);

	/* Enable RX interrupts now */
	pp->imr = ALTERA_UART_CONTROL_RRDY_MSK;
	altera_uart_update_ctrl_reg(pp);

	uart_port_unlock_irqrestore(port, flags);

	return 0;
}

static void altera_uart_shutdown(struct uart_port *port)
{
	struct altera_uart *pp = container_of(port, struct altera_uart, port);
	unsigned long flags;

	uart_port_lock_irqsave(port, &flags);

	/* Disable all interrupts now */
	pp->imr = 0;
	altera_uart_update_ctrl_reg(pp);

	uart_port_unlock_irqrestore(port, flags);

	if (port->irq)
		free_irq(port->irq, port);
	else
		timer_delete_sync(&pp->tmr);
}

static const char *altera_uart_type(struct uart_port *port)
{
	return (port->type == PORT_ALTERA_UART) ? "Altera UART" : NULL;
}

static int altera_uart_request_port(struct uart_port *port)
{
	/* UARTs always present */
	return 0;
}

static void altera_uart_release_port(struct uart_port *port)
{
	/* Nothing to release... */
}

static int altera_uart_verify_port(struct uart_port *port,
				   struct serial_struct *ser)
{
	if ((ser->type != PORT_UNKNOWN) && (ser->type != PORT_ALTERA_UART))
		return -EINVAL;
	return 0;
}

#ifdef CONFIG_CONSOLE_POLL
static int altera_uart_poll_get_char(struct uart_port *port)
{
	while (!(altera_uart_readl(port, ALTERA_UART_STATUS_REG) &
		 ALTERA_UART_STATUS_RRDY_MSK))
		cpu_relax();

	return altera_uart_readl(port, ALTERA_UART_RXDATA_REG);
}

static void altera_uart_poll_put_char(struct uart_port *port, unsigned char c)
{
	while (!(altera_uart_readl(port, ALTERA_UART_STATUS_REG) &
		 ALTERA_UART_STATUS_TRDY_MSK))
		cpu_relax();

	altera_uart_writel(port, c, ALTERA_UART_TXDATA_REG);
}
#endif

/*
 *	Define the basic serial functions we support.
 */
static const struct uart_ops altera_uart_ops = {
	.tx_empty	= altera_uart_tx_empty,
	.get_mctrl	= altera_uart_get_mctrl,
	.set_mctrl	= altera_uart_set_mctrl,
	.start_tx	= altera_uart_start_tx,
	.stop_tx	= altera_uart_stop_tx,
	.stop_rx	= altera_uart_stop_rx,
	.break_ctl	= altera_uart_break_ctl,
	.startup	= altera_uart_startup,
	.shutdown	= altera_uart_shutdown,
	.set_termios	= altera_uart_set_termios,
	.type		= altera_uart_type,
	.request_port	= altera_uart_request_port,
	.release_port	= altera_uart_release_port,
	.config_port	= altera_uart_config_port,
	.verify_port	= altera_uart_verify_port,
#ifdef CONFIG_CONSOLE_POLL
	.poll_get_char	= altera_uart_poll_get_char,
	.poll_put_char	= altera_uart_poll_put_char,
#endif
};

static struct altera_uart altera_uart_ports[CONFIG_SERIAL_ALTERA_UART_MAXPORTS];

#if defined(CONFIG_SERIAL_ALTERA_UART_CONSOLE)

static void altera_uart_console_putc(struct uart_port *port, unsigned char c)
{
	while (!(altera_uart_readl(port, ALTERA_UART_STATUS_REG) &
		 ALTERA_UART_STATUS_TRDY_MSK))
		cpu_relax();

	altera_uart_writel(port, c, ALTERA_UART_TXDATA_REG);
}

static void altera_uart_console_write(struct console *co, const char *s,
				      unsigned int count)
{
	struct uart_port *port = &(altera_uart_ports + co->index)->port;

	uart_console_write(port, s, count, altera_uart_console_putc);
}

static int __init altera_uart_console_setup(struct console *co, char *options)
{
	struct uart_port *port;
	int baud = CONFIG_SERIAL_ALTERA_UART_BAUDRATE;
	int bits = 8;
	int parity = 'n';
	int flow = 'n';

	if (co->index < 0 || co->index >= CONFIG_SERIAL_ALTERA_UART_MAXPORTS)
		return -EINVAL;
	port = &altera_uart_ports[co->index].port;
	if (!port->membase)
		return -ENODEV;

	if (options)
		uart_parse_options(options, &baud, &parity, &bits, &flow);

	return uart_set_options(port, co, baud, parity, bits, flow);
}

static struct uart_driver altera_uart_driver;

static struct console altera_uart_console = {
	.name	= "ttyAL",
	.write	= altera_uart_console_write,
	.device	= uart_console_device,
	.setup	= altera_uart_console_setup,
	.flags	= CON_PRINTBUFFER,
	.index	= -1,
	.data	= &altera_uart_driver,
};

static int __init altera_uart_console_init(void)
{
	register_console(&altera_uart_console);
	return 0;
}

console_initcall(altera_uart_console_init);

#define	ALTERA_UART_CONSOLE	(&altera_uart_console)

static void altera_uart_earlycon_write(struct console *co, const char *s,
				       unsigned int count)
{
	struct earlycon_device *dev = co->data;

	uart_console_write(&dev->port, s, count, altera_uart_console_putc);
}

static int __init altera_uart_earlycon_setup(struct earlycon_device *dev,
					     const char *options)
{
	struct uart_port *port = &dev->port;

	if (!port->membase)
		return -ENODEV;

	/* Enable RX interrupts now */
	altera_uart_writel(port, ALTERA_UART_CONTROL_RRDY_MSK,
			   ALTERA_UART_CONTROL_REG);

	if (dev->baud) {
		unsigned int baudclk = port->uartclk / dev->baud;

		altera_uart_writel(port, baudclk, ALTERA_UART_DIVISOR_REG);
	}

	dev->con->write = altera_uart_earlycon_write;
	return 0;
}

OF_EARLYCON_DECLARE(uart, "altr,uart-1.0", altera_uart_earlycon_setup);

#else

#define	ALTERA_UART_CONSOLE	NULL

#endif /* CONFIG_SERIAL_ALTERA_UART_CONSOLE */

/*
 *	Define the altera_uart UART driver structure.
 */
static struct uart_driver altera_uart_driver = {
	.owner		= THIS_MODULE,
	.driver_name	= KBUILD_MODNAME,
	.dev_name	= "ttyAL",
	.major		= SERIAL_ALTERA_MAJOR,
	.minor		= SERIAL_ALTERA_MINOR,
	.nr		= CONFIG_SERIAL_ALTERA_UART_MAXPORTS,
	.cons		= ALTERA_UART_CONSOLE,
};

static int altera_uart_probe(struct platform_device *pdev)
{
	struct altera_uart_platform_uart *platp = dev_get_platdata(&pdev->dev);
	struct uart_port *port;
	struct resource *res_mem;
	int i = pdev->id;
	int ret;

	/* if id is -1 scan for a free id and use that one */
	if (i == -1) {
		for (i = 0; i < CONFIG_SERIAL_ALTERA_UART_MAXPORTS; i++)
			if (altera_uart_ports[i].port.mapbase == 0)
				break;
	}

	if (i < 0 || i >= CONFIG_SERIAL_ALTERA_UART_MAXPORTS)
		return -EINVAL;

	port = &altera_uart_ports[i].port;

	res_mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (res_mem)
		port->mapbase = res_mem->start;
	else if (platp)
		port->mapbase = platp->mapbase;
	else
		return -EINVAL;

	ret = platform_get_irq_optional(pdev, 0);
	if (ret < 0 && ret != -ENXIO)
		return ret;
	if (ret > 0)
		port->irq = ret;
	else if (platp)
		port->irq = platp->irq;

	/* Check platform data first so we can override device node data */
	if (platp)
		port->uartclk = platp->uartclk;
	else {
		ret = of_property_read_u32(pdev->dev.of_node, "clock-frequency",
					   &port->uartclk);
		if (ret)
			return ret;
	}

	port->membase = ioremap(port->mapbase, ALTERA_UART_SIZE);
	if (!port->membase)
		return -ENOMEM;

	if (platp)
		port->regshift = platp->bus_shift;
	else
		port->regshift = 0;

	port->line = i;
	port->type = PORT_ALTERA_UART;
	port->iotype = SERIAL_IO_MEM;
	port->ops = &altera_uart_ops;
	port->flags = UPF_BOOT_AUTOCONF;
	port->dev = &pdev->dev;

	platform_set_drvdata(pdev, port);

	uart_add_one_port(&altera_uart_driver, port);

	return 0;
}

static void altera_uart_remove(struct platform_device *pdev)
{
	struct uart_port *port = platform_get_drvdata(pdev);

	if (port) {
		uart_remove_one_port(&altera_uart_driver, port);
		port->mapbase = 0;
		iounmap(port->membase);
	}
}

#ifdef CONFIG_OF
static const struct of_device_id altera_uart_match[] = {
	{ .compatible = "ALTR,uart-1.0", },
	{ .compatible = "altr,uart-1.0", },
	{},
};
MODULE_DEVICE_TABLE(of, altera_uart_match);
#endif /* CONFIG_OF */

static struct platform_driver altera_uart_platform_driver = {
	.probe	= altera_uart_probe,
	.remove = altera_uart_remove,
	.driver	= {
		.name		= KBUILD_MODNAME,
		.of_match_table	= of_match_ptr(altera_uart_match),
	},
};

static int __init altera_uart_init(void)
{
	int rc;

	rc = uart_register_driver(&altera_uart_driver);
	if (rc)
		return rc;
	rc = platform_driver_register(&altera_uart_platform_driver);
	if (rc)
		uart_unregister_driver(&altera_uart_driver);
	return rc;
}

static void __exit altera_uart_exit(void)
{
	platform_driver_unregister(&altera_uart_platform_driver);
	uart_unregister_driver(&altera_uart_driver);
}

module_init(altera_uart_init);
module_exit(altera_uart_exit);

MODULE_DESCRIPTION("Altera UART driver");
MODULE_AUTHOR("Thomas Chou <thomas@wytron.com.tw>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:" KBUILD_MODNAME);
MODULE_ALIAS_CHARDEV_MAJOR(SERIAL_ALTERA_MAJOR);
