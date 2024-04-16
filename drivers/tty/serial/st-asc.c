// SPDX-License-Identifier: GPL-2.0+
/*
 * st-asc.c: ST Asynchronous serial controller (ASC) driver
 *
 * Copyright (C) 2003-2013 STMicroelectronics (R&D) Limited
 */

#include <linux/module.h>
#include <linux/serial.h>
#include <linux/console.h>
#include <linux/sysrq.h>
#include <linux/pinctrl/consumer.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/delay.h>
#include <linux/spinlock.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/serial_core.h>
#include <linux/clk.h>
#include <linux/gpio/consumer.h>

#define DRIVER_NAME "st-asc"
#define ASC_SERIAL_NAME "ttyAS"
#define ASC_FIFO_SIZE 16
#define ASC_MAX_PORTS 8

/* Pinctrl states */
#define DEFAULT		0
#define NO_HW_FLOWCTRL	1

struct asc_port {
	struct uart_port port;
	struct gpio_desc *rts;
	struct clk *clk;
	struct pinctrl *pinctrl;
	struct pinctrl_state *states[2];
	unsigned int hw_flow_control:1;
	unsigned int force_m1:1;
};

static struct asc_port asc_ports[ASC_MAX_PORTS];
static struct uart_driver asc_uart_driver;

/*---- UART Register definitions ------------------------------*/

/* Register offsets */

#define ASC_BAUDRATE			0x00
#define ASC_TXBUF			0x04
#define ASC_RXBUF			0x08
#define ASC_CTL				0x0C
#define ASC_INTEN			0x10
#define ASC_STA				0x14
#define ASC_GUARDTIME			0x18
#define ASC_TIMEOUT			0x1C
#define ASC_TXRESET			0x20
#define ASC_RXRESET			0x24
#define ASC_RETRIES			0x28

/* ASC_RXBUF */
#define ASC_RXBUF_PE			0x100
#define ASC_RXBUF_FE			0x200
/*
 * Some of status comes from higher bits of the character and some come from
 * the status register. Combining both of them in to single status using dummy
 * bits.
 */
#define ASC_RXBUF_DUMMY_RX		0x10000
#define ASC_RXBUF_DUMMY_BE		0x20000
#define ASC_RXBUF_DUMMY_OE		0x40000

/* ASC_CTL */

#define ASC_CTL_MODE_MSK		0x0007
#define  ASC_CTL_MODE_8BIT		0x0001
#define  ASC_CTL_MODE_7BIT_PAR		0x0003
#define  ASC_CTL_MODE_9BIT		0x0004
#define  ASC_CTL_MODE_8BIT_WKUP		0x0005
#define  ASC_CTL_MODE_8BIT_PAR		0x0007
#define ASC_CTL_STOP_MSK		0x0018
#define  ASC_CTL_STOP_HALFBIT		0x0000
#define  ASC_CTL_STOP_1BIT		0x0008
#define  ASC_CTL_STOP_1_HALFBIT		0x0010
#define  ASC_CTL_STOP_2BIT		0x0018
#define ASC_CTL_PARITYODD		0x0020
#define ASC_CTL_LOOPBACK		0x0040
#define ASC_CTL_RUN			0x0080
#define ASC_CTL_RXENABLE		0x0100
#define ASC_CTL_SCENABLE		0x0200
#define ASC_CTL_FIFOENABLE		0x0400
#define ASC_CTL_CTSENABLE		0x0800
#define ASC_CTL_BAUDMODE		0x1000

/* ASC_GUARDTIME */

#define ASC_GUARDTIME_MSK		0x00FF

/* ASC_INTEN */

#define ASC_INTEN_RBE			0x0001
#define ASC_INTEN_TE			0x0002
#define ASC_INTEN_THE			0x0004
#define ASC_INTEN_PE			0x0008
#define ASC_INTEN_FE			0x0010
#define ASC_INTEN_OE			0x0020
#define ASC_INTEN_TNE			0x0040
#define ASC_INTEN_TOI			0x0080
#define ASC_INTEN_RHF			0x0100

/* ASC_RETRIES */

#define ASC_RETRIES_MSK			0x00FF

/* ASC_RXBUF */

#define ASC_RXBUF_MSK			0x03FF

/* ASC_STA */

#define ASC_STA_RBF			0x0001
#define ASC_STA_TE			0x0002
#define ASC_STA_THE			0x0004
#define ASC_STA_PE			0x0008
#define ASC_STA_FE			0x0010
#define ASC_STA_OE			0x0020
#define ASC_STA_TNE			0x0040
#define ASC_STA_TOI			0x0080
#define ASC_STA_RHF			0x0100
#define ASC_STA_TF			0x0200
#define ASC_STA_NKD			0x0400

/* ASC_TIMEOUT */

#define ASC_TIMEOUT_MSK			0x00FF

/* ASC_TXBUF */

#define ASC_TXBUF_MSK			0x01FF

/*---- Inline function definitions ---------------------------*/

static inline struct asc_port *to_asc_port(struct uart_port *port)
{
	return container_of(port, struct asc_port, port);
}

static inline u32 asc_in(struct uart_port *port, u32 offset)
{
#ifdef readl_relaxed
	return readl_relaxed(port->membase + offset);
#else
	return readl(port->membase + offset);
#endif
}

static inline void asc_out(struct uart_port *port, u32 offset, u32 value)
{
#ifdef writel_relaxed
	writel_relaxed(value, port->membase + offset);
#else
	writel(value, port->membase + offset);
#endif
}

/*
 * Some simple utility functions to enable and disable interrupts.
 * Note that these need to be called with interrupts disabled.
 */
static inline void asc_disable_tx_interrupts(struct uart_port *port)
{
	u32 intenable = asc_in(port, ASC_INTEN) & ~ASC_INTEN_THE;
	asc_out(port, ASC_INTEN, intenable);
	(void)asc_in(port, ASC_INTEN);	/* Defeat bus write posting */
}

static inline void asc_enable_tx_interrupts(struct uart_port *port)
{
	u32 intenable = asc_in(port, ASC_INTEN) | ASC_INTEN_THE;
	asc_out(port, ASC_INTEN, intenable);
}

static inline void asc_disable_rx_interrupts(struct uart_port *port)
{
	u32 intenable = asc_in(port, ASC_INTEN) & ~ASC_INTEN_RBE;
	asc_out(port, ASC_INTEN, intenable);
	(void)asc_in(port, ASC_INTEN);	/* Defeat bus write posting */
}

static inline void asc_enable_rx_interrupts(struct uart_port *port)
{
	u32 intenable = asc_in(port, ASC_INTEN) | ASC_INTEN_RBE;
	asc_out(port, ASC_INTEN, intenable);
}

static inline u32 asc_txfifo_is_empty(struct uart_port *port)
{
	return asc_in(port, ASC_STA) & ASC_STA_TE;
}

static inline u32 asc_txfifo_is_half_empty(struct uart_port *port)
{
	return asc_in(port, ASC_STA) & ASC_STA_THE;
}

static inline const char *asc_port_name(struct uart_port *port)
{
	return to_platform_device(port->dev)->name;
}

/*----------------------------------------------------------------------*/

/*
 * This section contains code to support the use of the ASC as a
 * generic serial port.
 */

static inline unsigned asc_hw_txroom(struct uart_port *port)
{
	u32 status = asc_in(port, ASC_STA);

	if (status & ASC_STA_THE)
		return port->fifosize / 2;
	else if (!(status & ASC_STA_TF))
		return 1;

	return 0;
}

/*
 * Start transmitting chars.
 * This is called from both interrupt and task level.
 * Either way interrupts are disabled.
 */
static void asc_transmit_chars(struct uart_port *port)
{
	struct circ_buf *xmit = &port->state->xmit;
	int txroom;
	unsigned char c;

	txroom = asc_hw_txroom(port);

	if ((txroom != 0) && port->x_char) {
		c = port->x_char;
		port->x_char = 0;
		asc_out(port, ASC_TXBUF, c);
		port->icount.tx++;
		txroom = asc_hw_txroom(port);
	}

	if (uart_tx_stopped(port)) {
		/*
		 * We should try and stop the hardware here, but I
		 * don't think the ASC has any way to do that.
		 */
		asc_disable_tx_interrupts(port);
		return;
	}

	if (uart_circ_empty(xmit)) {
		asc_disable_tx_interrupts(port);
		return;
	}

	if (txroom == 0)
		return;

	do {
		c = xmit->buf[xmit->tail];
		xmit->tail = (xmit->tail + 1) & (UART_XMIT_SIZE - 1);
		asc_out(port, ASC_TXBUF, c);
		port->icount.tx++;
		txroom--;
	} while ((txroom > 0) && (!uart_circ_empty(xmit)));

	if (uart_circ_chars_pending(xmit) < WAKEUP_CHARS)
		uart_write_wakeup(port);

	if (uart_circ_empty(xmit))
		asc_disable_tx_interrupts(port);
}

static void asc_receive_chars(struct uart_port *port)
{
	struct tty_port *tport = &port->state->port;
	unsigned long status, mode;
	unsigned long c = 0;
	char flag;
	bool ignore_pe = false;

	/*
	 * Datasheet states: If the MODE field selects an 8-bit frame then
	 * this [parity error] bit is undefined. Software should ignore this
	 * bit when reading 8-bit frames.
	 */
	mode = asc_in(port, ASC_CTL) & ASC_CTL_MODE_MSK;
	if (mode == ASC_CTL_MODE_8BIT || mode == ASC_CTL_MODE_8BIT_PAR)
		ignore_pe = true;

	if (irqd_is_wakeup_set(irq_get_irq_data(port->irq)))
		pm_wakeup_event(tport->tty->dev, 0);

	while ((status = asc_in(port, ASC_STA)) & ASC_STA_RBF) {
		c = asc_in(port, ASC_RXBUF) | ASC_RXBUF_DUMMY_RX;
		flag = TTY_NORMAL;
		port->icount.rx++;

		if (status & ASC_STA_OE || c & ASC_RXBUF_FE ||
		    (c & ASC_RXBUF_PE && !ignore_pe)) {

			if (c & ASC_RXBUF_FE) {
				if (c == (ASC_RXBUF_FE | ASC_RXBUF_DUMMY_RX)) {
					port->icount.brk++;
					if (uart_handle_break(port))
						continue;
					c |= ASC_RXBUF_DUMMY_BE;
				} else {
					port->icount.frame++;
				}
			} else if (c & ASC_RXBUF_PE) {
				port->icount.parity++;
			}
			/*
			 * Reading any data from the RX FIFO clears the
			 * overflow error condition.
			 */
			if (status & ASC_STA_OE) {
				port->icount.overrun++;
				c |= ASC_RXBUF_DUMMY_OE;
			}

			c &= port->read_status_mask;

			if (c & ASC_RXBUF_DUMMY_BE)
				flag = TTY_BREAK;
			else if (c & ASC_RXBUF_PE)
				flag = TTY_PARITY;
			else if (c & ASC_RXBUF_FE)
				flag = TTY_FRAME;
		}

		if (uart_handle_sysrq_char(port, c & 0xff))
			continue;

		uart_insert_char(port, c, ASC_RXBUF_DUMMY_OE, c & 0xff, flag);
	}

	/* Tell the rest of the system the news. New characters! */
	tty_flip_buffer_push(tport);
}

static irqreturn_t asc_interrupt(int irq, void *ptr)
{
	struct uart_port *port = ptr;
	u32 status;

	spin_lock(&port->lock);

	status = asc_in(port, ASC_STA);

	if (status & ASC_STA_RBF) {
		/* Receive FIFO not empty */
		asc_receive_chars(port);
	}

	if ((status & ASC_STA_THE) &&
	    (asc_in(port, ASC_INTEN) & ASC_INTEN_THE)) {
		/* Transmitter FIFO at least half empty */
		asc_transmit_chars(port);
	}

	spin_unlock(&port->lock);

	return IRQ_HANDLED;
}

/*----------------------------------------------------------------------*/

/*
 * UART Functions
 */

static unsigned int asc_tx_empty(struct uart_port *port)
{
	return asc_txfifo_is_empty(port) ? TIOCSER_TEMT : 0;
}

static void asc_set_mctrl(struct uart_port *port, unsigned int mctrl)
{
	struct asc_port *ascport = to_asc_port(port);

	/*
	 * This routine is used for seting signals of: DTR, DCD, CTS and RTS.
	 * We use ASC's hardware for CTS/RTS when hardware flow-control is
	 * enabled, however if the RTS line is required for another purpose,
	 * commonly controlled using HUP from userspace, then we need to toggle
	 * it manually, using GPIO.
	 *
	 * Some boards also have DTR and DCD implemented using PIO pins, code to
	 * do this should be hooked in here.
	 */

	if (!ascport->rts)
		return;

	/* If HW flow-control is enabled, we can't fiddle with the RTS line */
	if (asc_in(port, ASC_CTL) & ASC_CTL_CTSENABLE)
		return;

	gpiod_set_value(ascport->rts, mctrl & TIOCM_RTS);
}

static unsigned int asc_get_mctrl(struct uart_port *port)
{
	/*
	 * This routine is used for geting signals of: DTR, DCD, DSR, RI,
	 * and CTS/RTS
	 */
	return TIOCM_CAR | TIOCM_DSR | TIOCM_CTS;
}

/* There are probably characters waiting to be transmitted. */
static void asc_start_tx(struct uart_port *port)
{
	struct circ_buf *xmit = &port->state->xmit;

	if (!uart_circ_empty(xmit))
		asc_enable_tx_interrupts(port);
}

/* Transmit stop */
static void asc_stop_tx(struct uart_port *port)
{
	asc_disable_tx_interrupts(port);
}

/* Receive stop */
static void asc_stop_rx(struct uart_port *port)
{
	asc_disable_rx_interrupts(port);
}

/* Handle breaks - ignored by us */
static void asc_break_ctl(struct uart_port *port, int break_state)
{
	/* Nothing here yet .. */
}

/*
 * Enable port for reception.
 */
static int asc_startup(struct uart_port *port)
{
	if (request_irq(port->irq, asc_interrupt, 0,
			asc_port_name(port), port)) {
		dev_err(port->dev, "cannot allocate irq.\n");
		return -ENODEV;
	}

	asc_transmit_chars(port);
	asc_enable_rx_interrupts(port);

	return 0;
}

static void asc_shutdown(struct uart_port *port)
{
	asc_disable_tx_interrupts(port);
	asc_disable_rx_interrupts(port);
	free_irq(port->irq, port);
}

static void asc_pm(struct uart_port *port, unsigned int state,
		unsigned int oldstate)
{
	struct asc_port *ascport = to_asc_port(port);
	unsigned long flags;
	u32 ctl;

	switch (state) {
	case UART_PM_STATE_ON:
		clk_prepare_enable(ascport->clk);
		break;
	case UART_PM_STATE_OFF:
		/*
		 * Disable the ASC baud rate generator, which is as close as
		 * we can come to turning it off. Note this is not called with
		 * the port spinlock held.
		 */
		spin_lock_irqsave(&port->lock, flags);
		ctl = asc_in(port, ASC_CTL) & ~ASC_CTL_RUN;
		asc_out(port, ASC_CTL, ctl);
		spin_unlock_irqrestore(&port->lock, flags);
		clk_disable_unprepare(ascport->clk);
		break;
	}
}

static void asc_set_termios(struct uart_port *port, struct ktermios *termios,
			    const struct ktermios *old)
{
	struct asc_port *ascport = to_asc_port(port);
	struct gpio_desc *gpiod;
	unsigned int baud;
	u32 ctrl_val;
	tcflag_t cflag;
	unsigned long flags;

	/* Update termios to reflect hardware capabilities */
	termios->c_cflag &= ~(CMSPAR |
			 (ascport->hw_flow_control ? 0 : CRTSCTS));

	port->uartclk = clk_get_rate(ascport->clk);

	baud = uart_get_baud_rate(port, termios, old, 0, port->uartclk/16);
	cflag = termios->c_cflag;

	spin_lock_irqsave(&port->lock, flags);

	/* read control register */
	ctrl_val = asc_in(port, ASC_CTL);

	/* stop serial port and reset value */
	asc_out(port, ASC_CTL, (ctrl_val & ~ASC_CTL_RUN));
	ctrl_val = ASC_CTL_RXENABLE | ASC_CTL_FIFOENABLE;

	/* reset fifo rx & tx */
	asc_out(port, ASC_TXRESET, 1);
	asc_out(port, ASC_RXRESET, 1);

	/* set character length */
	if ((cflag & CSIZE) == CS7) {
		ctrl_val |= ASC_CTL_MODE_7BIT_PAR;
		cflag |= PARENB;
	} else {
		ctrl_val |= (cflag & PARENB) ?  ASC_CTL_MODE_8BIT_PAR :
						ASC_CTL_MODE_8BIT;
		cflag &= ~CSIZE;
		cflag |= CS8;
	}
	termios->c_cflag = cflag;

	/* set stop bit */
	ctrl_val |= (cflag & CSTOPB) ? ASC_CTL_STOP_2BIT : ASC_CTL_STOP_1BIT;

	/* odd parity */
	if (cflag & PARODD)
		ctrl_val |= ASC_CTL_PARITYODD;

	/* hardware flow control */
	if ((cflag & CRTSCTS)) {
		ctrl_val |= ASC_CTL_CTSENABLE;

		/* If flow-control selected, stop handling RTS manually */
		if (ascport->rts) {
			devm_gpiod_put(port->dev, ascport->rts);
			ascport->rts = NULL;

			pinctrl_select_state(ascport->pinctrl,
					     ascport->states[DEFAULT]);
		}
	} else {
		/* If flow-control disabled, it's safe to handle RTS manually */
		if (!ascport->rts && ascport->states[NO_HW_FLOWCTRL]) {
			pinctrl_select_state(ascport->pinctrl,
					     ascport->states[NO_HW_FLOWCTRL]);

			gpiod = devm_gpiod_get(port->dev, "rts", GPIOD_OUT_LOW);
			if (!IS_ERR(gpiod)) {
				gpiod_set_consumer_name(gpiod,
						port->dev->of_node->name);
				ascport->rts = gpiod;
			}
		}
	}

	if ((baud < 19200) && !ascport->force_m1) {
		asc_out(port, ASC_BAUDRATE, (port->uartclk / (16 * baud)));
	} else {
		/*
		 * MODE 1: recommended for high bit rates (above 19.2K)
		 *
		 *                   baudrate * 16 * 2^16
		 * ASCBaudRate =   ------------------------
		 *                          inputclock
		 *
		 * To keep maths inside 64bits, we divide inputclock by 16.
		 */
		u64 dividend = (u64)baud * (1 << 16);

		do_div(dividend, port->uartclk / 16);
		asc_out(port, ASC_BAUDRATE, dividend);
		ctrl_val |= ASC_CTL_BAUDMODE;
	}

	uart_update_timeout(port, cflag, baud);

	ascport->port.read_status_mask = ASC_RXBUF_DUMMY_OE;
	if (termios->c_iflag & INPCK)
		ascport->port.read_status_mask |= ASC_RXBUF_FE | ASC_RXBUF_PE;
	if (termios->c_iflag & (IGNBRK | BRKINT | PARMRK))
		ascport->port.read_status_mask |= ASC_RXBUF_DUMMY_BE;

	/*
	 * Characters to ignore
	 */
	ascport->port.ignore_status_mask = 0;
	if (termios->c_iflag & IGNPAR)
		ascport->port.ignore_status_mask |= ASC_RXBUF_FE | ASC_RXBUF_PE;
	if (termios->c_iflag & IGNBRK) {
		ascport->port.ignore_status_mask |= ASC_RXBUF_DUMMY_BE;
		/*
		 * If we're ignoring parity and break indicators,
		 * ignore overruns too (for real raw support).
		 */
		if (termios->c_iflag & IGNPAR)
			ascport->port.ignore_status_mask |= ASC_RXBUF_DUMMY_OE;
	}

	/*
	 * Ignore all characters if CREAD is not set.
	 */
	if (!(termios->c_cflag & CREAD))
		ascport->port.ignore_status_mask |= ASC_RXBUF_DUMMY_RX;

	/* Set the timeout */
	asc_out(port, ASC_TIMEOUT, 20);

	/* write final value and enable port */
	asc_out(port, ASC_CTL, (ctrl_val | ASC_CTL_RUN));

	spin_unlock_irqrestore(&port->lock, flags);
}

static const char *asc_type(struct uart_port *port)
{
	return (port->type == PORT_ASC) ? DRIVER_NAME : NULL;
}

static void asc_release_port(struct uart_port *port)
{
}

static int asc_request_port(struct uart_port *port)
{
	return 0;
}

/*
 * Called when the port is opened, and UPF_BOOT_AUTOCONF flag is set
 * Set type field if successful
 */
static void asc_config_port(struct uart_port *port, int flags)
{
	if ((flags & UART_CONFIG_TYPE))
		port->type = PORT_ASC;
}

static int
asc_verify_port(struct uart_port *port, struct serial_struct *ser)
{
	/* No user changeable parameters */
	return -EINVAL;
}

#ifdef CONFIG_CONSOLE_POLL
/*
 * Console polling routines for writing and reading from the uart while
 * in an interrupt or debug context (i.e. kgdb).
 */

static int asc_get_poll_char(struct uart_port *port)
{
	if (!(asc_in(port, ASC_STA) & ASC_STA_RBF))
		return NO_POLL_CHAR;

	return asc_in(port, ASC_RXBUF);
}

static void asc_put_poll_char(struct uart_port *port, unsigned char c)
{
	while (!asc_txfifo_is_half_empty(port))
		cpu_relax();
	asc_out(port, ASC_TXBUF, c);
}

#endif /* CONFIG_CONSOLE_POLL */

/*---------------------------------------------------------------------*/

static const struct uart_ops asc_uart_ops = {
	.tx_empty	= asc_tx_empty,
	.set_mctrl	= asc_set_mctrl,
	.get_mctrl	= asc_get_mctrl,
	.start_tx	= asc_start_tx,
	.stop_tx	= asc_stop_tx,
	.stop_rx	= asc_stop_rx,
	.break_ctl	= asc_break_ctl,
	.startup	= asc_startup,
	.shutdown	= asc_shutdown,
	.set_termios	= asc_set_termios,
	.type		= asc_type,
	.release_port	= asc_release_port,
	.request_port	= asc_request_port,
	.config_port	= asc_config_port,
	.verify_port	= asc_verify_port,
	.pm		= asc_pm,
#ifdef CONFIG_CONSOLE_POLL
	.poll_get_char = asc_get_poll_char,
	.poll_put_char = asc_put_poll_char,
#endif /* CONFIG_CONSOLE_POLL */
};

static int asc_init_port(struct asc_port *ascport,
			  struct platform_device *pdev)
{
	struct uart_port *port = &ascport->port;
	struct resource *res;
	int ret;

	port->iotype	= UPIO_MEM;
	port->flags	= UPF_BOOT_AUTOCONF;
	port->ops	= &asc_uart_ops;
	port->fifosize	= ASC_FIFO_SIZE;
	port->dev	= &pdev->dev;
	port->irq	= platform_get_irq(pdev, 0);
	port->has_sysrq = IS_ENABLED(CONFIG_SERIAL_ST_ASC_CONSOLE);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	port->membase = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(port->membase))
		return PTR_ERR(port->membase);
	port->mapbase = res->start;

	spin_lock_init(&port->lock);

	ascport->clk = devm_clk_get(&pdev->dev, NULL);

	if (WARN_ON(IS_ERR(ascport->clk)))
		return -EINVAL;
	/* ensure that clk rate is correct by enabling the clk */
	clk_prepare_enable(ascport->clk);
	ascport->port.uartclk = clk_get_rate(ascport->clk);
	WARN_ON(ascport->port.uartclk == 0);
	clk_disable_unprepare(ascport->clk);

	ascport->pinctrl = devm_pinctrl_get(&pdev->dev);
	if (IS_ERR(ascport->pinctrl)) {
		ret = PTR_ERR(ascport->pinctrl);
		dev_err(&pdev->dev, "Failed to get Pinctrl: %d\n", ret);
		return ret;
	}

	ascport->states[DEFAULT] =
		pinctrl_lookup_state(ascport->pinctrl, "default");
	if (IS_ERR(ascport->states[DEFAULT])) {
		ret = PTR_ERR(ascport->states[DEFAULT]);
		dev_err(&pdev->dev,
			"Failed to look up Pinctrl state 'default': %d\n", ret);
		return ret;
	}

	/* "no-hw-flowctrl" state is optional */
	ascport->states[NO_HW_FLOWCTRL] =
		pinctrl_lookup_state(ascport->pinctrl, "no-hw-flowctrl");
	if (IS_ERR(ascport->states[NO_HW_FLOWCTRL]))
		ascport->states[NO_HW_FLOWCTRL] = NULL;

	return 0;
}

static struct asc_port *asc_of_get_asc_port(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	int id;

	if (!np)
		return NULL;

	id = of_alias_get_id(np, "serial");
	if (id < 0)
		id = of_alias_get_id(np, ASC_SERIAL_NAME);

	if (id < 0)
		id = 0;

	if (WARN_ON(id >= ASC_MAX_PORTS))
		return NULL;

	asc_ports[id].hw_flow_control = of_property_read_bool(np,
							"uart-has-rtscts");
	asc_ports[id].force_m1 =  of_property_read_bool(np, "st,force_m1");
	asc_ports[id].port.line = id;
	asc_ports[id].rts = NULL;

	return &asc_ports[id];
}

#ifdef CONFIG_OF
static const struct of_device_id asc_match[] = {
	{ .compatible = "st,asc", },
	{},
};

MODULE_DEVICE_TABLE(of, asc_match);
#endif

static int asc_serial_probe(struct platform_device *pdev)
{
	int ret;
	struct asc_port *ascport;

	ascport = asc_of_get_asc_port(pdev);
	if (!ascport)
		return -ENODEV;

	ret = asc_init_port(ascport, pdev);
	if (ret)
		return ret;

	ret = uart_add_one_port(&asc_uart_driver, &ascport->port);
	if (ret)
		return ret;

	platform_set_drvdata(pdev, &ascport->port);

	return 0;
}

static int asc_serial_remove(struct platform_device *pdev)
{
	struct uart_port *port = platform_get_drvdata(pdev);

	return uart_remove_one_port(&asc_uart_driver, port);
}

#ifdef CONFIG_PM_SLEEP
static int asc_serial_suspend(struct device *dev)
{
	struct uart_port *port = dev_get_drvdata(dev);

	return uart_suspend_port(&asc_uart_driver, port);
}

static int asc_serial_resume(struct device *dev)
{
	struct uart_port *port = dev_get_drvdata(dev);

	return uart_resume_port(&asc_uart_driver, port);
}

#endif /* CONFIG_PM_SLEEP */

/*----------------------------------------------------------------------*/

#ifdef CONFIG_SERIAL_ST_ASC_CONSOLE
static void asc_console_putchar(struct uart_port *port, unsigned char ch)
{
	unsigned int timeout = 1000000;

	/* Wait for upto 1 second in case flow control is stopping us. */
	while (--timeout && !asc_txfifo_is_half_empty(port))
		udelay(1);

	asc_out(port, ASC_TXBUF, ch);
}

/*
 *  Print a string to the serial port trying not to disturb
 *  any possible real use of the port...
 */

static void asc_console_write(struct console *co, const char *s, unsigned count)
{
	struct uart_port *port = &asc_ports[co->index].port;
	unsigned long flags;
	unsigned long timeout = 1000000;
	int locked = 1;
	u32 intenable;

	if (port->sysrq)
		locked = 0; /* asc_interrupt has already claimed the lock */
	else if (oops_in_progress)
		locked = spin_trylock_irqsave(&port->lock, flags);
	else
		spin_lock_irqsave(&port->lock, flags);

	/*
	 * Disable interrupts so we don't get the IRQ line bouncing
	 * up and down while interrupts are disabled.
	 */
	intenable = asc_in(port, ASC_INTEN);
	asc_out(port, ASC_INTEN, 0);
	(void)asc_in(port, ASC_INTEN);	/* Defeat bus write posting */

	uart_console_write(port, s, count, asc_console_putchar);

	while (--timeout && !asc_txfifo_is_empty(port))
		udelay(1);

	asc_out(port, ASC_INTEN, intenable);

	if (locked)
		spin_unlock_irqrestore(&port->lock, flags);
}

static int asc_console_setup(struct console *co, char *options)
{
	struct asc_port *ascport;
	int baud = 115200;
	int bits = 8;
	int parity = 'n';
	int flow = 'n';

	if (co->index >= ASC_MAX_PORTS)
		return -ENODEV;

	ascport = &asc_ports[co->index];

	/*
	 * This driver does not support early console initialization
	 * (use ARM early printk support instead), so we only expect
	 * this to be called during the uart port registration when the
	 * driver gets probed and the port should be mapped at that point.
	 */
	if (ascport->port.mapbase == 0 || ascport->port.membase == NULL)
		return -ENXIO;

	if (options)
		uart_parse_options(options, &baud, &parity, &bits, &flow);

	return uart_set_options(&ascport->port, co, baud, parity, bits, flow);
}

static struct console asc_console = {
	.name		= ASC_SERIAL_NAME,
	.device		= uart_console_device,
	.write		= asc_console_write,
	.setup		= asc_console_setup,
	.flags		= CON_PRINTBUFFER,
	.index		= -1,
	.data		= &asc_uart_driver,
};

#define ASC_SERIAL_CONSOLE (&asc_console)

#else
#define ASC_SERIAL_CONSOLE NULL
#endif /* CONFIG_SERIAL_ST_ASC_CONSOLE */

static struct uart_driver asc_uart_driver = {
	.owner		= THIS_MODULE,
	.driver_name	= DRIVER_NAME,
	.dev_name	= ASC_SERIAL_NAME,
	.major		= 0,
	.minor		= 0,
	.nr		= ASC_MAX_PORTS,
	.cons		= ASC_SERIAL_CONSOLE,
};

static const struct dev_pm_ops asc_serial_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(asc_serial_suspend, asc_serial_resume)
};

static struct platform_driver asc_serial_driver = {
	.probe		= asc_serial_probe,
	.remove		= asc_serial_remove,
	.driver	= {
		.name	= DRIVER_NAME,
		.pm	= &asc_serial_pm_ops,
		.of_match_table = of_match_ptr(asc_match),
	},
};

static int __init asc_init(void)
{
	int ret;
	static const char banner[] __initconst =
		KERN_INFO "STMicroelectronics ASC driver initialized\n";

	printk(banner);

	ret = uart_register_driver(&asc_uart_driver);
	if (ret)
		return ret;

	ret = platform_driver_register(&asc_serial_driver);
	if (ret)
		uart_unregister_driver(&asc_uart_driver);

	return ret;
}

static void __exit asc_exit(void)
{
	platform_driver_unregister(&asc_serial_driver);
	uart_unregister_driver(&asc_uart_driver);
}

module_init(asc_init);
module_exit(asc_exit);

MODULE_ALIAS("platform:" DRIVER_NAME);
MODULE_AUTHOR("STMicroelectronics (R&D) Limited");
MODULE_DESCRIPTION("STMicroelectronics ASC serial port driver");
MODULE_LICENSE("GPL");
