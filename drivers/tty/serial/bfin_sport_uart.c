// SPDX-License-Identifier: GPL-2.0+
/*
 * Blackfin On-Chip Sport Emulated UART Driver
 *
 * Copyright 2006-2009 Analog Devices Inc.
 *
 * Enter bugs at http://blackfin.uclinux.org/
 */

/*
 * This driver and the hardware supported are in term of EE-191 of ADI.
 * http://www.analog.com/static/imported-files/application_notes/EE191.pdf 
 * This application note describe how to implement a UART on a Sharc DSP,
 * but this driver is implemented on Blackfin Processor.
 * Transmit Frame Sync is not used by this driver to transfer data out.
 */

/* #define DEBUG */

#define DRV_NAME "bfin-sport-uart"
#define DEVICE_NAME	"ttySS"
#define pr_fmt(fmt) DRV_NAME ": " fmt

#include <linux/module.h>
#include <linux/ioport.h>
#include <linux/io.h>
#include <linux/init.h>
#include <linux/console.h>
#include <linux/sysrq.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/serial_core.h>
#include <linux/gpio.h>

#include <asm/bfin_sport.h>
#include <asm/delay.h>
#include <asm/portmux.h>

#include "bfin_sport_uart.h"

struct sport_uart_port {
	struct uart_port	port;
	int			err_irq;
	unsigned short		csize;
	unsigned short		rxmask;
	unsigned short		txmask1;
	unsigned short		txmask2;
	unsigned char		stopb;
/*	unsigned char		parib; */
#ifdef CONFIG_SERIAL_BFIN_SPORT_CTSRTS
	int cts_pin;
	int rts_pin;
#endif
};

static int sport_uart_tx_chars(struct sport_uart_port *up);
static void sport_stop_tx(struct uart_port *port);

static inline void tx_one_byte(struct sport_uart_port *up, unsigned int value)
{
	pr_debug("%s value:%x, mask1=0x%x, mask2=0x%x\n", __func__, value,
		up->txmask1, up->txmask2);

	/* Place Start and Stop bits */
	__asm__ __volatile__ (
		"%[val] <<= 1;"
		"%[val] = %[val] & %[mask1];"
		"%[val] = %[val] | %[mask2];"
		: [val]"+d"(value)
		: [mask1]"d"(up->txmask1), [mask2]"d"(up->txmask2)
		: "ASTAT"
	);
	pr_debug("%s value:%x\n", __func__, value);

	SPORT_PUT_TX(up, value);
}

static inline unsigned char rx_one_byte(struct sport_uart_port *up)
{
	unsigned int value;
	unsigned char extract;
	u32 tmp_mask1, tmp_mask2, tmp_shift, tmp;

	if ((up->csize + up->stopb) > 7)
		value = SPORT_GET_RX32(up);
	else
		value = SPORT_GET_RX(up);

	pr_debug("%s value:%x, cs=%d, mask=0x%x\n", __func__, value,
		up->csize, up->rxmask);

	/* Extract data */
	__asm__ __volatile__ (
		"%[extr] = 0;"
		"%[mask1] = %[rxmask];"
		"%[mask2] = 0x0200(Z);"
		"%[shift] = 0;"
		"LSETUP(.Lloop_s, .Lloop_e) LC0 = %[lc];"
		".Lloop_s:"
		"%[tmp] = extract(%[val], %[mask1].L)(Z);"
		"%[tmp] <<= %[shift];"
		"%[extr] = %[extr] | %[tmp];"
		"%[mask1] = %[mask1] - %[mask2];"
		".Lloop_e:"
		"%[shift] += 1;"
		: [extr]"=&d"(extract), [shift]"=&d"(tmp_shift), [tmp]"=&d"(tmp),
		  [mask1]"=&d"(tmp_mask1), [mask2]"=&d"(tmp_mask2)
		: [val]"d"(value), [rxmask]"d"(up->rxmask), [lc]"a"(up->csize)
		: "ASTAT", "LB0", "LC0", "LT0"
	);

	pr_debug("	extract:%x\n", extract);
	return extract;
}

static int sport_uart_setup(struct sport_uart_port *up, int size, int baud_rate)
{
	int tclkdiv, rclkdiv;
	unsigned int sclk = get_sclk();

	/* Set TCR1 and TCR2, TFSR is not enabled for uart */
	SPORT_PUT_TCR1(up, (LATFS | ITFS | TFSR | TLSBIT | ITCLK));
	SPORT_PUT_TCR2(up, size + 1);
	pr_debug("%s TCR1:%x, TCR2:%x\n", __func__, SPORT_GET_TCR1(up), SPORT_GET_TCR2(up));

	/* Set RCR1 and RCR2 */
	SPORT_PUT_RCR1(up, (RCKFE | LARFS | LRFS | RFSR | IRCLK));
	SPORT_PUT_RCR2(up, (size + 1) * 2 - 1);
	pr_debug("%s RCR1:%x, RCR2:%x\n", __func__, SPORT_GET_RCR1(up), SPORT_GET_RCR2(up));

	tclkdiv = sclk / (2 * baud_rate) - 1;
	/* The actual uart baud rate of devices vary between +/-2%. The sport
	 * RX sample rate should be faster than the double of the worst case,
	 * otherwise, wrong data are received. So, set sport RX clock to be
	 * 3% faster.
	 */
	rclkdiv = sclk / (2 * baud_rate * 2 * 97 / 100) - 1;
	SPORT_PUT_TCLKDIV(up, tclkdiv);
	SPORT_PUT_RCLKDIV(up, rclkdiv);
	SSYNC();
	pr_debug("%s sclk:%d, baud_rate:%d, tclkdiv:%d, rclkdiv:%d\n",
			__func__, sclk, baud_rate, tclkdiv, rclkdiv);

	return 0;
}

static irqreturn_t sport_uart_rx_irq(int irq, void *dev_id)
{
	struct sport_uart_port *up = dev_id;
	struct tty_port *port = &up->port.state->port;
	unsigned int ch;

	spin_lock(&up->port.lock);

	while (SPORT_GET_STAT(up) & RXNE) {
		ch = rx_one_byte(up);
		up->port.icount.rx++;

		if (!uart_handle_sysrq_char(&up->port, ch))
			tty_insert_flip_char(port, ch, TTY_NORMAL);
	}

	spin_unlock(&up->port.lock);

	/* XXX this won't deadlock with lowlat? */
	tty_flip_buffer_push(port);

	return IRQ_HANDLED;
}

static irqreturn_t sport_uart_tx_irq(int irq, void *dev_id)
{
	struct sport_uart_port *up = dev_id;

	spin_lock(&up->port.lock);
	sport_uart_tx_chars(up);
	spin_unlock(&up->port.lock);

	return IRQ_HANDLED;
}

static irqreturn_t sport_uart_err_irq(int irq, void *dev_id)
{
	struct sport_uart_port *up = dev_id;
	unsigned int stat = SPORT_GET_STAT(up);

	spin_lock(&up->port.lock);

	/* Overflow in RX FIFO */
	if (stat & ROVF) {
		up->port.icount.overrun++;
		tty_insert_flip_char(&up->port.state->port, 0, TTY_OVERRUN);
		SPORT_PUT_STAT(up, ROVF); /* Clear ROVF bit */
	}
	/* These should not happen */
	if (stat & (TOVF | TUVF | RUVF)) {
		pr_err("SPORT Error:%s %s %s\n",
		       (stat & TOVF) ? "TX overflow" : "",
		       (stat & TUVF) ? "TX underflow" : "",
		       (stat & RUVF) ? "RX underflow" : "");
		SPORT_PUT_TCR1(up, SPORT_GET_TCR1(up) & ~TSPEN);
		SPORT_PUT_RCR1(up, SPORT_GET_RCR1(up) & ~RSPEN);
	}
	SSYNC();

	spin_unlock(&up->port.lock);
	/* XXX we don't push the overrun bit to TTY? */

	return IRQ_HANDLED;
}

#ifdef CONFIG_SERIAL_BFIN_SPORT_CTSRTS
static unsigned int sport_get_mctrl(struct uart_port *port)
{
	struct sport_uart_port *up = (struct sport_uart_port *)port;
	if (up->cts_pin < 0)
		return TIOCM_CTS | TIOCM_DSR | TIOCM_CAR;

	/* CTS PIN is negative assertive. */
	if (SPORT_UART_GET_CTS(up))
		return TIOCM_CTS | TIOCM_DSR | TIOCM_CAR;
	else
		return TIOCM_DSR | TIOCM_CAR;
}

static void sport_set_mctrl(struct uart_port *port, unsigned int mctrl)
{
	struct sport_uart_port *up = (struct sport_uart_port *)port;
	if (up->rts_pin < 0)
		return;

	/* RTS PIN is negative assertive. */
	if (mctrl & TIOCM_RTS)
		SPORT_UART_ENABLE_RTS(up);
	else
		SPORT_UART_DISABLE_RTS(up);
}

/*
 * Handle any change of modem status signal.
 */
static irqreturn_t sport_mctrl_cts_int(int irq, void *dev_id)
{
	struct sport_uart_port *up = (struct sport_uart_port *)dev_id;
	unsigned int status;

	status = sport_get_mctrl(&up->port);
	uart_handle_cts_change(&up->port, status & TIOCM_CTS);

	return IRQ_HANDLED;
}
#else
static unsigned int sport_get_mctrl(struct uart_port *port)
{
	pr_debug("%s enter\n", __func__);
	return TIOCM_CTS | TIOCM_CD | TIOCM_DSR;
}

static void sport_set_mctrl(struct uart_port *port, unsigned int mctrl)
{
	pr_debug("%s enter\n", __func__);
}
#endif

/* Reqeust IRQ, Setup clock */
static int sport_startup(struct uart_port *port)
{
	struct sport_uart_port *up = (struct sport_uart_port *)port;
	int ret;

	pr_debug("%s enter\n", __func__);
	ret = request_irq(up->port.irq, sport_uart_rx_irq, 0,
		"SPORT_UART_RX", up);
	if (ret) {
		dev_err(port->dev, "unable to request SPORT RX interrupt\n");
		return ret;
	}

	ret = request_irq(up->port.irq+1, sport_uart_tx_irq, 0,
		"SPORT_UART_TX", up);
	if (ret) {
		dev_err(port->dev, "unable to request SPORT TX interrupt\n");
		goto fail1;
	}

	ret = request_irq(up->err_irq, sport_uart_err_irq, 0,
		"SPORT_UART_STATUS", up);
	if (ret) {
		dev_err(port->dev, "unable to request SPORT status interrupt\n");
		goto fail2;
	}

#ifdef CONFIG_SERIAL_BFIN_SPORT_CTSRTS
	if (up->cts_pin >= 0) {
		if (request_irq(gpio_to_irq(up->cts_pin),
			sport_mctrl_cts_int,
			IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING |
			0, "BFIN_SPORT_UART_CTS", up)) {
			up->cts_pin = -1;
			dev_info(port->dev, "Unable to attach BlackFin UART over SPORT CTS interrupt. So, disable it.\n");
		}
	}
	if (up->rts_pin >= 0) {
		if (gpio_request(up->rts_pin, DRV_NAME)) {
			dev_info(port->dev, "fail to request RTS PIN at GPIO_%d\n", up->rts_pin);
			up->rts_pin = -1;
		} else
			gpio_direction_output(up->rts_pin, 0);
	}
#endif

	return 0;
 fail2:
	free_irq(up->port.irq+1, up);
 fail1:
	free_irq(up->port.irq, up);

	return ret;
}

/*
 * sport_uart_tx_chars
 *
 * ret 1 means need to enable sport.
 * ret 0 means do nothing.
 */
static int sport_uart_tx_chars(struct sport_uart_port *up)
{
	struct circ_buf *xmit = &up->port.state->xmit;

	if (SPORT_GET_STAT(up) & TXF)
		return 0;

	if (up->port.x_char) {
		tx_one_byte(up, up->port.x_char);
		up->port.icount.tx++;
		up->port.x_char = 0;
		return 1;
	}

	if (uart_circ_empty(xmit) || uart_tx_stopped(&up->port)) {
		/* The waiting loop to stop SPORT TX from TX interrupt is
		 * too long. This may block SPORT RX interrupts and cause
		 * RX FIFO overflow. So, do stop sport TX only after the last
		 * char in TX FIFO is moved into the shift register.
		 */
		if (SPORT_GET_STAT(up) & TXHRE)
			sport_stop_tx(&up->port);
		return 0;
	}

	while(!(SPORT_GET_STAT(up) & TXF) && !uart_circ_empty(xmit)) {
		tx_one_byte(up, xmit->buf[xmit->tail]);
		xmit->tail = (xmit->tail + 1) & (UART_XMIT_SIZE -1);
		up->port.icount.tx++;
	}

	if (uart_circ_chars_pending(xmit) < WAKEUP_CHARS)
		uart_write_wakeup(&up->port);

	return 1;
}

static unsigned int sport_tx_empty(struct uart_port *port)
{
	struct sport_uart_port *up = (struct sport_uart_port *)port;
	unsigned int stat;

	stat = SPORT_GET_STAT(up);
	pr_debug("%s stat:%04x\n", __func__, stat);
	if (stat & TXHRE) {
		return TIOCSER_TEMT;
	} else
		return 0;
}

static void sport_stop_tx(struct uart_port *port)
{
	struct sport_uart_port *up = (struct sport_uart_port *)port;

	pr_debug("%s enter\n", __func__);

	if (!(SPORT_GET_TCR1(up) & TSPEN))
		return;

	/* Although the hold register is empty, last byte is still in shift
	 * register and not sent out yet. So, put a dummy data into TX FIFO.
	 * Then, sport tx stops when last byte is shift out and the dummy
	 * data is moved into the shift register.
	 */
	SPORT_PUT_TX(up, 0xffff);
	while (!(SPORT_GET_STAT(up) & TXHRE))
		cpu_relax();

	SPORT_PUT_TCR1(up, (SPORT_GET_TCR1(up) & ~TSPEN));
	SSYNC();

	return;
}

static void sport_start_tx(struct uart_port *port)
{
	struct sport_uart_port *up = (struct sport_uart_port *)port;

	pr_debug("%s enter\n", __func__);

	/* Write data into SPORT FIFO before enable SPROT to transmit */
	if (sport_uart_tx_chars(up)) {
		/* Enable transmit, then an interrupt will generated */
		SPORT_PUT_TCR1(up, (SPORT_GET_TCR1(up) | TSPEN));
		SSYNC();
	}

	pr_debug("%s exit\n", __func__);
}

static void sport_stop_rx(struct uart_port *port)
{
	struct sport_uart_port *up = (struct sport_uart_port *)port;

	pr_debug("%s enter\n", __func__);
	/* Disable sport to stop rx */
	SPORT_PUT_RCR1(up, (SPORT_GET_RCR1(up) & ~RSPEN));
	SSYNC();
}

static void sport_break_ctl(struct uart_port *port, int break_state)
{
	pr_debug("%s enter\n", __func__);
}

static void sport_shutdown(struct uart_port *port)
{
	struct sport_uart_port *up = (struct sport_uart_port *)port;

	dev_dbg(port->dev, "%s enter\n", __func__);

	/* Disable sport */
	SPORT_PUT_TCR1(up, (SPORT_GET_TCR1(up) & ~TSPEN));
	SPORT_PUT_RCR1(up, (SPORT_GET_RCR1(up) & ~RSPEN));
	SSYNC();

	free_irq(up->port.irq, up);
	free_irq(up->port.irq+1, up);
	free_irq(up->err_irq, up);
#ifdef CONFIG_SERIAL_BFIN_SPORT_CTSRTS
	if (up->cts_pin >= 0)
		free_irq(gpio_to_irq(up->cts_pin), up);
	if (up->rts_pin >= 0)
		gpio_free(up->rts_pin);
#endif
}

static const char *sport_type(struct uart_port *port)
{
	struct sport_uart_port *up = (struct sport_uart_port *)port;

	pr_debug("%s enter\n", __func__);
	return up->port.type == PORT_BFIN_SPORT ? "BFIN-SPORT-UART" : NULL;
}

static void sport_release_port(struct uart_port *port)
{
	pr_debug("%s enter\n", __func__);
}

static int sport_request_port(struct uart_port *port)
{
	pr_debug("%s enter\n", __func__);
	return 0;
}

static void sport_config_port(struct uart_port *port, int flags)
{
	struct sport_uart_port *up = (struct sport_uart_port *)port;

	pr_debug("%s enter\n", __func__);
	up->port.type = PORT_BFIN_SPORT;
}

static int sport_verify_port(struct uart_port *port, struct serial_struct *ser)
{
	pr_debug("%s enter\n", __func__);
	return 0;
}

static void sport_set_termios(struct uart_port *port,
		struct ktermios *termios, struct ktermios *old)
{
	struct sport_uart_port *up = (struct sport_uart_port *)port;
	unsigned long flags;
	int i;

	pr_debug("%s enter, c_cflag:%08x\n", __func__, termios->c_cflag);

#ifdef CONFIG_SERIAL_BFIN_SPORT_CTSRTS
	if (old == NULL && up->cts_pin != -1)
		termios->c_cflag |= CRTSCTS;
	else if (up->cts_pin == -1)
		termios->c_cflag &= ~CRTSCTS;
#endif

	switch (termios->c_cflag & CSIZE) {
	case CS8:
		up->csize = 8;
		break;
	case CS7:
		up->csize = 7;
		break;
	case CS6:
		up->csize = 6;
		break;
	case CS5:
		up->csize = 5;
		break;
	default:
		pr_warn("requested word length not supported\n");
		break;
	}

	if (termios->c_cflag & CSTOPB) {
		up->stopb = 1;
	}
	if (termios->c_cflag & PARENB) {
		pr_warn("PAREN bit is not supported yet\n");
		/* up->parib = 1; */
	}

	spin_lock_irqsave(&up->port.lock, flags);

	port->read_status_mask = 0;

	/*
	 * Characters to ignore
	 */
	port->ignore_status_mask = 0;

	/* RX extract mask */
	up->rxmask = 0x01 | (((up->csize + up->stopb) * 2 - 1) << 0x8);
	/* TX masks, 8 bit data and 1 bit stop for example:
	 * mask1 = b#0111111110
	 * mask2 = b#1000000000
	 */
	for (i = 0, up->txmask1 = 0; i < up->csize; i++)
		up->txmask1 |= (1<<i);
	up->txmask2 = (1<<i);
	if (up->stopb) {
		++i;
		up->txmask2 |= (1<<i);
	}
	up->txmask1 <<= 1;
	up->txmask2 <<= 1;
	/* uart baud rate */
	port->uartclk = uart_get_baud_rate(port, termios, old, 0, get_sclk()/16);

	/* Disable UART */
	SPORT_PUT_TCR1(up, SPORT_GET_TCR1(up) & ~TSPEN);
	SPORT_PUT_RCR1(up, SPORT_GET_RCR1(up) & ~RSPEN);

	sport_uart_setup(up, up->csize + up->stopb, port->uartclk);

	/* driver TX line high after config, one dummy data is
	 * necessary to stop sport after shift one byte
	 */
	SPORT_PUT_TX(up, 0xffff);
	SPORT_PUT_TX(up, 0xffff);
	SPORT_PUT_TCR1(up, (SPORT_GET_TCR1(up) | TSPEN));
	SSYNC();
	while (!(SPORT_GET_STAT(up) & TXHRE))
		cpu_relax();
	SPORT_PUT_TCR1(up, SPORT_GET_TCR1(up) & ~TSPEN);
	SSYNC();

	/* Port speed changed, update the per-port timeout. */
	uart_update_timeout(port, termios->c_cflag, port->uartclk);

	/* Enable sport rx */
	SPORT_PUT_RCR1(up, SPORT_GET_RCR1(up) | RSPEN);
	SSYNC();

	spin_unlock_irqrestore(&up->port.lock, flags);
}

static const struct uart_ops sport_uart_ops = {
	.tx_empty	= sport_tx_empty,
	.set_mctrl	= sport_set_mctrl,
	.get_mctrl	= sport_get_mctrl,
	.stop_tx	= sport_stop_tx,
	.start_tx	= sport_start_tx,
	.stop_rx	= sport_stop_rx,
	.break_ctl	= sport_break_ctl,
	.startup	= sport_startup,
	.shutdown	= sport_shutdown,
	.set_termios	= sport_set_termios,
	.type		= sport_type,
	.release_port	= sport_release_port,
	.request_port	= sport_request_port,
	.config_port	= sport_config_port,
	.verify_port	= sport_verify_port,
};

#define BFIN_SPORT_UART_MAX_PORTS 4

static struct sport_uart_port *bfin_sport_uart_ports[BFIN_SPORT_UART_MAX_PORTS];

#ifdef CONFIG_SERIAL_BFIN_SPORT_CONSOLE
#define CLASS_BFIN_SPORT_CONSOLE	"bfin-sport-console"

static int __init
sport_uart_console_setup(struct console *co, char *options)
{
	struct sport_uart_port *up;
	int baud = 57600;
	int bits = 8;
	int parity = 'n';
# ifdef CONFIG_SERIAL_BFIN_SPORT_CTSRTS
	int flow = 'r';
# else
	int flow = 'n';
# endif

	/* Check whether an invalid uart number has been specified */
	if (co->index < 0 || co->index >= BFIN_SPORT_UART_MAX_PORTS)
		return -ENODEV;

	up = bfin_sport_uart_ports[co->index];
	if (!up)
		return -ENODEV;

	if (options)
		uart_parse_options(options, &baud, &parity, &bits, &flow);

	return uart_set_options(&up->port, co, baud, parity, bits, flow);
}

static void sport_uart_console_putchar(struct uart_port *port, int ch)
{
	struct sport_uart_port *up = (struct sport_uart_port *)port;

	while (SPORT_GET_STAT(up) & TXF)
		barrier();

	tx_one_byte(up, ch);
}

/*
 * Interrupts are disabled on entering
 */
static void
sport_uart_console_write(struct console *co, const char *s, unsigned int count)
{
	struct sport_uart_port *up = bfin_sport_uart_ports[co->index];
	unsigned long flags;

	spin_lock_irqsave(&up->port.lock, flags);

	if (SPORT_GET_TCR1(up) & TSPEN)
		uart_console_write(&up->port, s, count, sport_uart_console_putchar);
	else {
		/* dummy data to start sport */
		while (SPORT_GET_STAT(up) & TXF)
			barrier();
		SPORT_PUT_TX(up, 0xffff);
		/* Enable transmit, then an interrupt will generated */
		SPORT_PUT_TCR1(up, (SPORT_GET_TCR1(up) | TSPEN));
		SSYNC();

		uart_console_write(&up->port, s, count, sport_uart_console_putchar);

		/* Although the hold register is empty, last byte is still in shift
		 * register and not sent out yet. So, put a dummy data into TX FIFO.
		 * Then, sport tx stops when last byte is shift out and the dummy
		 * data is moved into the shift register.
		 */
		while (SPORT_GET_STAT(up) & TXF)
			barrier();
		SPORT_PUT_TX(up, 0xffff);
		while (!(SPORT_GET_STAT(up) & TXHRE))
			barrier();

		/* Stop sport tx transfer */
		SPORT_PUT_TCR1(up, (SPORT_GET_TCR1(up) & ~TSPEN));
		SSYNC();
	}

	spin_unlock_irqrestore(&up->port.lock, flags);
}

static struct uart_driver sport_uart_reg;

static struct console sport_uart_console = {
	.name		= DEVICE_NAME,
	.write		= sport_uart_console_write,
	.device		= uart_console_device,
	.setup		= sport_uart_console_setup,
	.flags		= CON_PRINTBUFFER,
	.index		= -1,
	.data		= &sport_uart_reg,
};

#define SPORT_UART_CONSOLE	(&sport_uart_console)
#else
#define SPORT_UART_CONSOLE	NULL
#endif /* CONFIG_SERIAL_BFIN_SPORT_CONSOLE */


static struct uart_driver sport_uart_reg = {
	.owner		= THIS_MODULE,
	.driver_name	= DRV_NAME,
	.dev_name	= DEVICE_NAME,
	.major		= 204,
	.minor		= 84,
	.nr		= BFIN_SPORT_UART_MAX_PORTS,
	.cons		= SPORT_UART_CONSOLE,
};

#ifdef CONFIG_PM
static int sport_uart_suspend(struct device *dev)
{
	struct sport_uart_port *sport = dev_get_drvdata(dev);

	dev_dbg(dev, "%s enter\n", __func__);
	if (sport)
		uart_suspend_port(&sport_uart_reg, &sport->port);

	return 0;
}

static int sport_uart_resume(struct device *dev)
{
	struct sport_uart_port *sport = dev_get_drvdata(dev);

	dev_dbg(dev, "%s enter\n", __func__);
	if (sport)
		uart_resume_port(&sport_uart_reg, &sport->port);

	return 0;
}

static const struct dev_pm_ops bfin_sport_uart_dev_pm_ops = {
	.suspend	= sport_uart_suspend,
	.resume		= sport_uart_resume,
};
#endif

static int sport_uart_probe(struct platform_device *pdev)
{
	struct resource *res;
	struct sport_uart_port *sport;
	int ret = 0;

	dev_dbg(&pdev->dev, "%s enter\n", __func__);

	if (pdev->id < 0 || pdev->id >= BFIN_SPORT_UART_MAX_PORTS) {
		dev_err(&pdev->dev, "Wrong sport uart platform device id.\n");
		return -ENOENT;
	}

	if (bfin_sport_uart_ports[pdev->id] == NULL) {
		bfin_sport_uart_ports[pdev->id] =
			kzalloc(sizeof(struct sport_uart_port), GFP_KERNEL);
		sport = bfin_sport_uart_ports[pdev->id];
		if (!sport) {
			dev_err(&pdev->dev,
				"Fail to malloc sport_uart_port\n");
			return -ENOMEM;
		}

		ret = peripheral_request_list(dev_get_platdata(&pdev->dev),
						DRV_NAME);
		if (ret) {
			dev_err(&pdev->dev,
				"Fail to request SPORT peripherals\n");
			goto out_error_free_mem;
		}

		spin_lock_init(&sport->port.lock);
		sport->port.fifosize  = SPORT_TX_FIFO_SIZE,
		sport->port.ops       = &sport_uart_ops;
		sport->port.line      = pdev->id;
		sport->port.iotype    = UPIO_MEM;
		sport->port.flags     = UPF_BOOT_AUTOCONF;

		res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
		if (res == NULL) {
			dev_err(&pdev->dev, "Cannot get IORESOURCE_MEM\n");
			ret = -ENOENT;
			goto out_error_free_peripherals;
		}

		sport->port.membase = ioremap(res->start, resource_size(res));
		if (!sport->port.membase) {
			dev_err(&pdev->dev, "Cannot map sport IO\n");
			ret = -ENXIO;
			goto out_error_free_peripherals;
		}
		sport->port.mapbase = res->start;

		sport->port.irq = platform_get_irq(pdev, 0);
		if ((int)sport->port.irq < 0) {
			dev_err(&pdev->dev, "No sport RX/TX IRQ specified\n");
			ret = -ENOENT;
			goto out_error_unmap;
		}

		sport->err_irq = platform_get_irq(pdev, 1);
		if (sport->err_irq < 0) {
			dev_err(&pdev->dev, "No sport status IRQ specified\n");
			ret = -ENOENT;
			goto out_error_unmap;
		}
#ifdef CONFIG_SERIAL_BFIN_SPORT_CTSRTS
		res = platform_get_resource(pdev, IORESOURCE_IO, 0);
		if (res == NULL)
			sport->cts_pin = -1;
		else
			sport->cts_pin = res->start;

		res = platform_get_resource(pdev, IORESOURCE_IO, 1);
		if (res == NULL)
			sport->rts_pin = -1;
		else
			sport->rts_pin = res->start;
#endif
	}

#ifdef CONFIG_SERIAL_BFIN_SPORT_CONSOLE
	if (!is_early_platform_device(pdev)) {
#endif
		sport = bfin_sport_uart_ports[pdev->id];
		sport->port.dev = &pdev->dev;
		dev_set_drvdata(&pdev->dev, sport);
		ret = uart_add_one_port(&sport_uart_reg, &sport->port);
#ifdef CONFIG_SERIAL_BFIN_SPORT_CONSOLE
	}
#endif
	if (!ret)
		return 0;

	if (sport) {
out_error_unmap:
		iounmap(sport->port.membase);
out_error_free_peripherals:
		peripheral_free_list(dev_get_platdata(&pdev->dev));
out_error_free_mem:
		kfree(sport);
		bfin_sport_uart_ports[pdev->id] = NULL;
	}

	return ret;
}

static int sport_uart_remove(struct platform_device *pdev)
{
	struct sport_uart_port *sport = platform_get_drvdata(pdev);

	dev_dbg(&pdev->dev, "%s enter\n", __func__);
	dev_set_drvdata(&pdev->dev, NULL);

	if (sport) {
		uart_remove_one_port(&sport_uart_reg, &sport->port);
		iounmap(sport->port.membase);
		peripheral_free_list(dev_get_platdata(&pdev->dev));
		kfree(sport);
		bfin_sport_uart_ports[pdev->id] = NULL;
	}

	return 0;
}

static struct platform_driver sport_uart_driver = {
	.probe		= sport_uart_probe,
	.remove		= sport_uart_remove,
	.driver		= {
		.name	= DRV_NAME,
#ifdef CONFIG_PM
		.pm	= &bfin_sport_uart_dev_pm_ops,
#endif
	},
};

#ifdef CONFIG_SERIAL_BFIN_SPORT_CONSOLE
static struct early_platform_driver early_sport_uart_driver __initdata = {
	.class_str = CLASS_BFIN_SPORT_CONSOLE,
	.pdrv = &sport_uart_driver,
	.requested_id = EARLY_PLATFORM_ID_UNSET,
};

static int __init sport_uart_rs_console_init(void)
{
	early_platform_driver_register(&early_sport_uart_driver, DRV_NAME);

	early_platform_driver_probe(CLASS_BFIN_SPORT_CONSOLE,
		BFIN_SPORT_UART_MAX_PORTS, 0);

	register_console(&sport_uart_console);

	return 0;
}
console_initcall(sport_uart_rs_console_init);
#endif

static int __init sport_uart_init(void)
{
	int ret;

	pr_info("Blackfin uart over sport driver\n");

	ret = uart_register_driver(&sport_uart_reg);
	if (ret) {
		pr_err("failed to register %s:%d\n",
				sport_uart_reg.driver_name, ret);
		return ret;
	}

	ret = platform_driver_register(&sport_uart_driver);
	if (ret) {
		pr_err("failed to register sport uart driver:%d\n", ret);
		uart_unregister_driver(&sport_uart_reg);
	}

	return ret;
}
module_init(sport_uart_init);

static void __exit sport_uart_exit(void)
{
	platform_driver_unregister(&sport_uart_driver);
	uart_unregister_driver(&sport_uart_reg);
}
module_exit(sport_uart_exit);

MODULE_AUTHOR("Sonic Zhang, Roy Huang");
MODULE_DESCRIPTION("Blackfin serial over SPORT driver");
MODULE_LICENSE("GPL");
