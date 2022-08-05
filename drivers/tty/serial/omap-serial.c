// SPDX-License-Identifier: GPL-2.0+
/*
 * Driver for OMAP-UART controller.
 * Based on drivers/serial/8250.c
 *
 * Copyright (C) 2010 Texas Instruments.
 *
 * Authors:
 *	Govindraj R	<govindraj.raja@ti.com>
 *	Thara Gopinath	<thara@ti.com>
 *
 * Note: This driver is made separate from 8250 driver as we cannot
 * over load 8250 driver with omap platform specific configuration for
 * features like DMA, it makes easier to implement features like DMA and
 * hardware flow control and software flow control configuration with
 * this driver as required for the omap-platform.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/console.h>
#include <linux/serial_reg.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/clk.h>
#include <linux/serial_core.h>
#include <linux/irq.h>
#include <linux/pm_runtime.h>
#include <linux/pm_wakeirq.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/gpio/consumer.h>
#include <linux/platform_data/serial-omap.h>

#define OMAP_MAX_HSUART_PORTS	10

#define UART_BUILD_REVISION(x, y)	(((x) << 8) | (y))

#define OMAP_UART_REV_42 0x0402
#define OMAP_UART_REV_46 0x0406
#define OMAP_UART_REV_52 0x0502
#define OMAP_UART_REV_63 0x0603

#define OMAP_UART_TX_WAKEUP_EN		BIT(7)

/* Feature flags */
#define OMAP_UART_WER_HAS_TX_WAKEUP	BIT(0)

#define UART_ERRATA_i202_MDR1_ACCESS	BIT(0)
#define UART_ERRATA_i291_DMA_FORCEIDLE	BIT(1)

#define DEFAULT_CLK_SPEED 48000000 /* 48Mhz */

/* SCR register bitmasks */
#define OMAP_UART_SCR_RX_TRIG_GRANU1_MASK		(1 << 7)
#define OMAP_UART_SCR_TX_TRIG_GRANU1_MASK		(1 << 6)
#define OMAP_UART_SCR_TX_EMPTY			(1 << 3)

/* FCR register bitmasks */
#define OMAP_UART_FCR_RX_FIFO_TRIG_MASK			(0x3 << 6)
#define OMAP_UART_FCR_TX_FIFO_TRIG_MASK			(0x3 << 4)

/* MVR register bitmasks */
#define OMAP_UART_MVR_SCHEME_SHIFT	30

#define OMAP_UART_LEGACY_MVR_MAJ_MASK	0xf0
#define OMAP_UART_LEGACY_MVR_MAJ_SHIFT	4
#define OMAP_UART_LEGACY_MVR_MIN_MASK	0x0f

#define OMAP_UART_MVR_MAJ_MASK		0x700
#define OMAP_UART_MVR_MAJ_SHIFT		8
#define OMAP_UART_MVR_MIN_MASK		0x3f

#define OMAP_UART_DMA_CH_FREE	-1

#define MSR_SAVE_FLAGS		UART_MSR_ANY_DELTA
#define OMAP_MODE13X_SPEED	230400

/* WER = 0x7F
 * Enable module level wakeup in WER reg
 */
#define OMAP_UART_WER_MOD_WKUP	0x7F

/* Enable XON/XOFF flow control on output */
#define OMAP_UART_SW_TX		0x08

/* Enable XON/XOFF flow control on input */
#define OMAP_UART_SW_RX		0x02

#define OMAP_UART_SW_CLR	0xF0

#define OMAP_UART_TCR_TRIG	0x0F

struct uart_omap_dma {
	u8			uart_dma_tx;
	u8			uart_dma_rx;
	int			rx_dma_channel;
	int			tx_dma_channel;
	dma_addr_t		rx_buf_dma_phys;
	dma_addr_t		tx_buf_dma_phys;
	unsigned int		uart_base;
	/*
	 * Buffer for rx dma. It is not required for tx because the buffer
	 * comes from port structure.
	 */
	unsigned char		*rx_buf;
	unsigned int		prev_rx_dma_pos;
	int			tx_buf_size;
	int			tx_dma_used;
	int			rx_dma_used;
	spinlock_t		tx_lock;
	spinlock_t		rx_lock;
	/* timer to poll activity on rx dma */
	struct timer_list	rx_timer;
	unsigned int		rx_buf_size;
	unsigned int		rx_poll_rate;
	unsigned int		rx_timeout;
};

struct uart_omap_port {
	struct uart_port	port;
	struct uart_omap_dma	uart_dma;
	struct device		*dev;
	int			wakeirq;

	unsigned char		ier;
	unsigned char		lcr;
	unsigned char		mcr;
	unsigned char		fcr;
	unsigned char		efr;
	unsigned char		dll;
	unsigned char		dlh;
	unsigned char		mdr1;
	unsigned char		scr;
	unsigned char		wer;

	int			use_dma;
	/*
	 * Some bits in registers are cleared on a read, so they must
	 * be saved whenever the register is read, but the bits will not
	 * be immediately processed.
	 */
	unsigned int		lsr_break_flag;
	unsigned char		msr_saved_flags;
	char			name[20];
	unsigned long		port_activity;
	int			context_loss_cnt;
	u32			errata;
	u32			features;

	struct gpio_desc	*rts_gpiod;

	struct pm_qos_request	pm_qos_request;
	u32			latency;
	u32			calc_latency;
	struct work_struct	qos_work;
	bool			is_suspending;

	unsigned int		rs485_tx_filter_count;
};

#define to_uart_omap_port(p) ((container_of((p), struct uart_omap_port, port)))

static struct uart_omap_port *ui[OMAP_MAX_HSUART_PORTS];

/* Forward declaration of functions */
static void serial_omap_mdr1_errataset(struct uart_omap_port *up, u8 mdr1);

static inline unsigned int serial_in(struct uart_omap_port *up, int offset)
{
	offset <<= up->port.regshift;
	return readw(up->port.membase + offset);
}

static inline void serial_out(struct uart_omap_port *up, int offset, int value)
{
	offset <<= up->port.regshift;
	writew(value, up->port.membase + offset);
}

static inline void serial_omap_clear_fifos(struct uart_omap_port *up)
{
	serial_out(up, UART_FCR, UART_FCR_ENABLE_FIFO);
	serial_out(up, UART_FCR, UART_FCR_ENABLE_FIFO |
		       UART_FCR_CLEAR_RCVR | UART_FCR_CLEAR_XMIT);
	serial_out(up, UART_FCR, 0);
}

#ifdef CONFIG_PM
static int serial_omap_get_context_loss_count(struct uart_omap_port *up)
{
	struct omap_uart_port_info *pdata = dev_get_platdata(up->dev);

	if (!pdata || !pdata->get_context_loss_count)
		return -EINVAL;

	return pdata->get_context_loss_count(up->dev);
}

/* REVISIT: Remove this when omap3 boots in device tree only mode */
static void serial_omap_enable_wakeup(struct uart_omap_port *up, bool enable)
{
	struct omap_uart_port_info *pdata = dev_get_platdata(up->dev);

	if (!pdata || !pdata->enable_wakeup)
		return;

	pdata->enable_wakeup(up->dev, enable);
}
#endif /* CONFIG_PM */

/*
 * Calculate the absolute difference between the desired and actual baud
 * rate for the given mode.
 */
static inline int calculate_baud_abs_diff(struct uart_port *port,
				unsigned int baud, unsigned int mode)
{
	unsigned int n = port->uartclk / (mode * baud);
	int abs_diff;

	if (n == 0)
		n = 1;

	abs_diff = baud - (port->uartclk / (mode * n));
	if (abs_diff < 0)
		abs_diff = -abs_diff;

	return abs_diff;
}

/*
 * serial_omap_baud_is_mode16 - check if baud rate is MODE16X
 * @port: uart port info
 * @baud: baudrate for which mode needs to be determined
 *
 * Returns true if baud rate is MODE16X and false if MODE13X
 * Original table in OMAP TRM named "UART Mode Baud Rates, Divisor Values,
 * and Error Rates" determines modes not for all common baud rates.
 * E.g. for 1000000 baud rate mode must be 16x, but according to that
 * table it's determined as 13x.
 */
static bool
serial_omap_baud_is_mode16(struct uart_port *port, unsigned int baud)
{
	int abs_diff_13 = calculate_baud_abs_diff(port, baud, 13);
	int abs_diff_16 = calculate_baud_abs_diff(port, baud, 16);

	return (abs_diff_13 >= abs_diff_16);
}

/*
 * serial_omap_get_divisor - calculate divisor value
 * @port: uart port info
 * @baud: baudrate for which divisor needs to be calculated.
 */
static unsigned int
serial_omap_get_divisor(struct uart_port *port, unsigned int baud)
{
	unsigned int mode;

	if (!serial_omap_baud_is_mode16(port, baud))
		mode = 13;
	else
		mode = 16;
	return port->uartclk/(mode * baud);
}

static void serial_omap_enable_ms(struct uart_port *port)
{
	struct uart_omap_port *up = to_uart_omap_port(port);

	dev_dbg(up->port.dev, "serial_omap_enable_ms+%d\n", up->port.line);

	up->ier |= UART_IER_MSI;
	serial_out(up, UART_IER, up->ier);
}

static void serial_omap_stop_tx(struct uart_port *port)
{
	struct uart_omap_port *up = to_uart_omap_port(port);
	int res;

	/* Handle RS-485 */
	if (port->rs485.flags & SER_RS485_ENABLED) {
		if (up->scr & OMAP_UART_SCR_TX_EMPTY) {
			/* THR interrupt is fired when both TX FIFO and TX
			 * shift register are empty. This means there's nothing
			 * left to transmit now, so make sure the THR interrupt
			 * is fired when TX FIFO is below the trigger level,
			 * disable THR interrupts and toggle the RS-485 GPIO
			 * data direction pin if needed.
			 */
			up->scr &= ~OMAP_UART_SCR_TX_EMPTY;
			serial_out(up, UART_OMAP_SCR, up->scr);
			res = (port->rs485.flags & SER_RS485_RTS_AFTER_SEND) ?
				1 : 0;
			if (up->rts_gpiod &&
			    gpiod_get_value(up->rts_gpiod) != res) {
				if (port->rs485.delay_rts_after_send > 0)
					mdelay(
					port->rs485.delay_rts_after_send);
				gpiod_set_value(up->rts_gpiod, res);
			}
		} else {
			/* We're asked to stop, but there's still stuff in the
			 * UART FIFO, so make sure the THR interrupt is fired
			 * when both TX FIFO and TX shift register are empty.
			 * The next THR interrupt (if no transmission is started
			 * in the meantime) will indicate the end of a
			 * transmission. Therefore we _don't_ disable THR
			 * interrupts in this situation.
			 */
			up->scr |= OMAP_UART_SCR_TX_EMPTY;
			serial_out(up, UART_OMAP_SCR, up->scr);
			return;
		}
	}

	if (up->ier & UART_IER_THRI) {
		up->ier &= ~UART_IER_THRI;
		serial_out(up, UART_IER, up->ier);
	}
}

static void serial_omap_stop_rx(struct uart_port *port)
{
	struct uart_omap_port *up = to_uart_omap_port(port);

	up->ier &= ~(UART_IER_RLSI | UART_IER_RDI);
	up->port.read_status_mask &= ~UART_LSR_DR;
	serial_out(up, UART_IER, up->ier);
}

static void transmit_chars(struct uart_omap_port *up, unsigned int lsr)
{
	struct circ_buf *xmit = &up->port.state->xmit;
	int count;

	if (up->port.x_char) {
		serial_out(up, UART_TX, up->port.x_char);
		up->port.icount.tx++;
		up->port.x_char = 0;
		if ((up->port.rs485.flags & SER_RS485_ENABLED) &&
		    !(up->port.rs485.flags & SER_RS485_RX_DURING_TX))
			up->rs485_tx_filter_count++;

		return;
	}
	if (uart_circ_empty(xmit) || uart_tx_stopped(&up->port)) {
		serial_omap_stop_tx(&up->port);
		return;
	}
	count = up->port.fifosize / 4;
	do {
		serial_out(up, UART_TX, xmit->buf[xmit->tail]);
		xmit->tail = (xmit->tail + 1) & (UART_XMIT_SIZE - 1);
		up->port.icount.tx++;
		if ((up->port.rs485.flags & SER_RS485_ENABLED) &&
		    !(up->port.rs485.flags & SER_RS485_RX_DURING_TX))
			up->rs485_tx_filter_count++;

		if (uart_circ_empty(xmit))
			break;
	} while (--count > 0);

	if (uart_circ_chars_pending(xmit) < WAKEUP_CHARS)
		uart_write_wakeup(&up->port);

	if (uart_circ_empty(xmit))
		serial_omap_stop_tx(&up->port);
}

static inline void serial_omap_enable_ier_thri(struct uart_omap_port *up)
{
	if (!(up->ier & UART_IER_THRI)) {
		up->ier |= UART_IER_THRI;
		serial_out(up, UART_IER, up->ier);
	}
}

static void serial_omap_start_tx(struct uart_port *port)
{
	struct uart_omap_port *up = to_uart_omap_port(port);
	int res;

	/* Handle RS-485 */
	if (port->rs485.flags & SER_RS485_ENABLED) {
		/* Fire THR interrupts when FIFO is below trigger level */
		up->scr &= ~OMAP_UART_SCR_TX_EMPTY;
		serial_out(up, UART_OMAP_SCR, up->scr);

		/* if rts not already enabled */
		res = (port->rs485.flags & SER_RS485_RTS_ON_SEND) ? 1 : 0;
		if (up->rts_gpiod && gpiod_get_value(up->rts_gpiod) != res) {
			gpiod_set_value(up->rts_gpiod, res);
			if (port->rs485.delay_rts_before_send > 0)
				mdelay(port->rs485.delay_rts_before_send);
		}
	}

	if ((port->rs485.flags & SER_RS485_ENABLED) &&
	    !(port->rs485.flags & SER_RS485_RX_DURING_TX))
		up->rs485_tx_filter_count = 0;

	serial_omap_enable_ier_thri(up);
}

static void serial_omap_throttle(struct uart_port *port)
{
	struct uart_omap_port *up = to_uart_omap_port(port);
	unsigned long flags;

	spin_lock_irqsave(&up->port.lock, flags);
	up->ier &= ~(UART_IER_RLSI | UART_IER_RDI);
	serial_out(up, UART_IER, up->ier);
	spin_unlock_irqrestore(&up->port.lock, flags);
}

static void serial_omap_unthrottle(struct uart_port *port)
{
	struct uart_omap_port *up = to_uart_omap_port(port);
	unsigned long flags;

	spin_lock_irqsave(&up->port.lock, flags);
	up->ier |= UART_IER_RLSI | UART_IER_RDI;
	serial_out(up, UART_IER, up->ier);
	spin_unlock_irqrestore(&up->port.lock, flags);
}

static unsigned int check_modem_status(struct uart_omap_port *up)
{
	unsigned int status;

	status = serial_in(up, UART_MSR);
	status |= up->msr_saved_flags;
	up->msr_saved_flags = 0;
	if ((status & UART_MSR_ANY_DELTA) == 0)
		return status;

	if (status & UART_MSR_ANY_DELTA && up->ier & UART_IER_MSI &&
	    up->port.state != NULL) {
		if (status & UART_MSR_TERI)
			up->port.icount.rng++;
		if (status & UART_MSR_DDSR)
			up->port.icount.dsr++;
		if (status & UART_MSR_DDCD)
			uart_handle_dcd_change
				(&up->port, status & UART_MSR_DCD);
		if (status & UART_MSR_DCTS)
			uart_handle_cts_change
				(&up->port, status & UART_MSR_CTS);
		wake_up_interruptible(&up->port.state->port.delta_msr_wait);
	}

	return status;
}

static void serial_omap_rlsi(struct uart_omap_port *up, unsigned int lsr)
{
	unsigned int flag;

	/*
	 * Read one data character out to avoid stalling the receiver according
	 * to the table 23-246 of the omap4 TRM.
	 */
	if (likely(lsr & UART_LSR_DR)) {
		serial_in(up, UART_RX);
		if ((up->port.rs485.flags & SER_RS485_ENABLED) &&
		    !(up->port.rs485.flags & SER_RS485_RX_DURING_TX) &&
		    up->rs485_tx_filter_count)
			up->rs485_tx_filter_count--;
	}

	up->port.icount.rx++;
	flag = TTY_NORMAL;

	if (lsr & UART_LSR_BI) {
		flag = TTY_BREAK;
		lsr &= ~(UART_LSR_FE | UART_LSR_PE);
		up->port.icount.brk++;
		/*
		 * We do the SysRQ and SAK checking
		 * here because otherwise the break
		 * may get masked by ignore_status_mask
		 * or read_status_mask.
		 */
		if (uart_handle_break(&up->port))
			return;

	}

	if (lsr & UART_LSR_PE) {
		flag = TTY_PARITY;
		up->port.icount.parity++;
	}

	if (lsr & UART_LSR_FE) {
		flag = TTY_FRAME;
		up->port.icount.frame++;
	}

	if (lsr & UART_LSR_OE)
		up->port.icount.overrun++;

#ifdef CONFIG_SERIAL_OMAP_CONSOLE
	if (up->port.line == up->port.cons->index) {
		/* Recover the break flag from console xmit */
		lsr |= up->lsr_break_flag;
	}
#endif
	uart_insert_char(&up->port, lsr, UART_LSR_OE, 0, flag);
}

static void serial_omap_rdi(struct uart_omap_port *up, unsigned int lsr)
{
	unsigned char ch = 0;
	unsigned int flag;

	if (!(lsr & UART_LSR_DR))
		return;

	ch = serial_in(up, UART_RX);
	if ((up->port.rs485.flags & SER_RS485_ENABLED) &&
	    !(up->port.rs485.flags & SER_RS485_RX_DURING_TX) &&
	    up->rs485_tx_filter_count) {
		up->rs485_tx_filter_count--;
		return;
	}

	flag = TTY_NORMAL;
	up->port.icount.rx++;

	if (uart_handle_sysrq_char(&up->port, ch))
		return;

	uart_insert_char(&up->port, lsr, UART_LSR_OE, ch, flag);
}

/**
 * serial_omap_irq() - This handles the interrupt from one port
 * @irq: uart port irq number
 * @dev_id: uart port info
 */
static irqreturn_t serial_omap_irq(int irq, void *dev_id)
{
	struct uart_omap_port *up = dev_id;
	unsigned int iir, lsr;
	unsigned int type;
	irqreturn_t ret = IRQ_NONE;
	int max_count = 256;

	spin_lock(&up->port.lock);

	do {
		iir = serial_in(up, UART_IIR);
		if (iir & UART_IIR_NO_INT)
			break;

		ret = IRQ_HANDLED;
		lsr = serial_in(up, UART_LSR);

		/* extract IRQ type from IIR register */
		type = iir & 0x3e;

		switch (type) {
		case UART_IIR_MSI:
			check_modem_status(up);
			break;
		case UART_IIR_THRI:
			transmit_chars(up, lsr);
			break;
		case UART_IIR_RX_TIMEOUT:
		case UART_IIR_RDI:
			serial_omap_rdi(up, lsr);
			break;
		case UART_IIR_RLSI:
			serial_omap_rlsi(up, lsr);
			break;
		case UART_IIR_CTS_RTS_DSR:
			/* simply try again */
			break;
		case UART_IIR_XOFF:
		default:
			break;
		}
	} while (max_count--);

	spin_unlock(&up->port.lock);

	tty_flip_buffer_push(&up->port.state->port);

	up->port_activity = jiffies;

	return ret;
}

static unsigned int serial_omap_tx_empty(struct uart_port *port)
{
	struct uart_omap_port *up = to_uart_omap_port(port);
	unsigned long flags;
	unsigned int ret = 0;

	dev_dbg(up->port.dev, "serial_omap_tx_empty+%d\n", up->port.line);
	spin_lock_irqsave(&up->port.lock, flags);
	ret = serial_in(up, UART_LSR) & UART_LSR_TEMT ? TIOCSER_TEMT : 0;
	spin_unlock_irqrestore(&up->port.lock, flags);

	return ret;
}

static unsigned int serial_omap_get_mctrl(struct uart_port *port)
{
	struct uart_omap_port *up = to_uart_omap_port(port);
	unsigned int status;
	unsigned int ret = 0;

	status = check_modem_status(up);

	dev_dbg(up->port.dev, "serial_omap_get_mctrl+%d\n", up->port.line);

	if (status & UART_MSR_DCD)
		ret |= TIOCM_CAR;
	if (status & UART_MSR_RI)
		ret |= TIOCM_RNG;
	if (status & UART_MSR_DSR)
		ret |= TIOCM_DSR;
	if (status & UART_MSR_CTS)
		ret |= TIOCM_CTS;
	return ret;
}

static void serial_omap_set_mctrl(struct uart_port *port, unsigned int mctrl)
{
	struct uart_omap_port *up = to_uart_omap_port(port);
	unsigned char mcr = 0, old_mcr, lcr;

	dev_dbg(up->port.dev, "serial_omap_set_mctrl+%d\n", up->port.line);
	if (mctrl & TIOCM_RTS)
		mcr |= UART_MCR_RTS;
	if (mctrl & TIOCM_DTR)
		mcr |= UART_MCR_DTR;
	if (mctrl & TIOCM_OUT1)
		mcr |= UART_MCR_OUT1;
	if (mctrl & TIOCM_OUT2)
		mcr |= UART_MCR_OUT2;
	if (mctrl & TIOCM_LOOP)
		mcr |= UART_MCR_LOOP;

	old_mcr = serial_in(up, UART_MCR);
	old_mcr &= ~(UART_MCR_LOOP | UART_MCR_OUT2 | UART_MCR_OUT1 |
		     UART_MCR_DTR | UART_MCR_RTS);
	up->mcr = old_mcr | mcr;
	serial_out(up, UART_MCR, up->mcr);

	/* Turn off autoRTS if RTS is lowered; restore autoRTS if RTS raised */
	lcr = serial_in(up, UART_LCR);
	serial_out(up, UART_LCR, UART_LCR_CONF_MODE_B);
	if ((mctrl & TIOCM_RTS) && (port->status & UPSTAT_AUTORTS))
		up->efr |= UART_EFR_RTS;
	else
		up->efr &= ~UART_EFR_RTS;
	serial_out(up, UART_EFR, up->efr);
	serial_out(up, UART_LCR, lcr);
}

static void serial_omap_break_ctl(struct uart_port *port, int break_state)
{
	struct uart_omap_port *up = to_uart_omap_port(port);
	unsigned long flags;

	dev_dbg(up->port.dev, "serial_omap_break_ctl+%d\n", up->port.line);
	spin_lock_irqsave(&up->port.lock, flags);
	if (break_state == -1)
		up->lcr |= UART_LCR_SBC;
	else
		up->lcr &= ~UART_LCR_SBC;
	serial_out(up, UART_LCR, up->lcr);
	spin_unlock_irqrestore(&up->port.lock, flags);
}

static int serial_omap_startup(struct uart_port *port)
{
	struct uart_omap_port *up = to_uart_omap_port(port);
	unsigned long flags;
	int retval;

	/*
	 * Allocate the IRQ
	 */
	retval = request_irq(up->port.irq, serial_omap_irq, up->port.irqflags,
				up->name, up);
	if (retval)
		return retval;

	/* Optional wake-up IRQ */
	if (up->wakeirq) {
		retval = dev_pm_set_dedicated_wake_irq(up->dev, up->wakeirq);
		if (retval) {
			free_irq(up->port.irq, up);
			return retval;
		}
	}

	dev_dbg(up->port.dev, "serial_omap_startup+%d\n", up->port.line);

	pm_runtime_get_sync(up->dev);
	/*
	 * Clear the FIFO buffers and disable them.
	 * (they will be reenabled in set_termios())
	 */
	serial_omap_clear_fifos(up);

	/*
	 * Clear the interrupt registers.
	 */
	(void) serial_in(up, UART_LSR);
	if (serial_in(up, UART_LSR) & UART_LSR_DR)
		(void) serial_in(up, UART_RX);
	(void) serial_in(up, UART_IIR);
	(void) serial_in(up, UART_MSR);

	/*
	 * Now, initialize the UART
	 */
	serial_out(up, UART_LCR, UART_LCR_WLEN8);
	spin_lock_irqsave(&up->port.lock, flags);
	/*
	 * Most PC uarts need OUT2 raised to enable interrupts.
	 */
	up->port.mctrl |= TIOCM_OUT2;
	serial_omap_set_mctrl(&up->port, up->port.mctrl);
	spin_unlock_irqrestore(&up->port.lock, flags);

	up->msr_saved_flags = 0;
	/*
	 * Finally, enable interrupts. Note: Modem status interrupts
	 * are set via set_termios(), which will be occurring imminently
	 * anyway, so we don't enable them here.
	 */
	up->ier = UART_IER_RLSI | UART_IER_RDI;
	serial_out(up, UART_IER, up->ier);

	/* Enable module level wake up */
	up->wer = OMAP_UART_WER_MOD_WKUP;
	if (up->features & OMAP_UART_WER_HAS_TX_WAKEUP)
		up->wer |= OMAP_UART_TX_WAKEUP_EN;

	serial_out(up, UART_OMAP_WER, up->wer);

	up->port_activity = jiffies;
	return 0;
}

static void serial_omap_shutdown(struct uart_port *port)
{
	struct uart_omap_port *up = to_uart_omap_port(port);
	unsigned long flags;

	dev_dbg(up->port.dev, "serial_omap_shutdown+%d\n", up->port.line);

	/*
	 * Disable interrupts from this port
	 */
	up->ier = 0;
	serial_out(up, UART_IER, 0);

	spin_lock_irqsave(&up->port.lock, flags);
	up->port.mctrl &= ~TIOCM_OUT2;
	serial_omap_set_mctrl(&up->port, up->port.mctrl);
	spin_unlock_irqrestore(&up->port.lock, flags);

	/*
	 * Disable break condition and FIFOs
	 */
	serial_out(up, UART_LCR, serial_in(up, UART_LCR) & ~UART_LCR_SBC);
	serial_omap_clear_fifos(up);

	/*
	 * Read data port to reset things, and then free the irq
	 */
	if (serial_in(up, UART_LSR) & UART_LSR_DR)
		(void) serial_in(up, UART_RX);

	pm_runtime_put_sync(up->dev);
	free_irq(up->port.irq, up);
	dev_pm_clear_wake_irq(up->dev);
}

static void serial_omap_uart_qos_work(struct work_struct *work)
{
	struct uart_omap_port *up = container_of(work, struct uart_omap_port,
						qos_work);

	cpu_latency_qos_update_request(&up->pm_qos_request, up->latency);
}

static void
serial_omap_set_termios(struct uart_port *port, struct ktermios *termios,
			struct ktermios *old)
{
	struct uart_omap_port *up = to_uart_omap_port(port);
	unsigned char cval = 0;
	unsigned long flags;
	unsigned int baud, quot;

	cval = UART_LCR_WLEN(tty_get_char_size(termios->c_cflag));

	if (termios->c_cflag & CSTOPB)
		cval |= UART_LCR_STOP;
	if (termios->c_cflag & PARENB)
		cval |= UART_LCR_PARITY;
	if (!(termios->c_cflag & PARODD))
		cval |= UART_LCR_EPAR;
	if (termios->c_cflag & CMSPAR)
		cval |= UART_LCR_SPAR;

	/*
	 * Ask the core to calculate the divisor for us.
	 */

	baud = uart_get_baud_rate(port, termios, old, 0, port->uartclk/13);
	quot = serial_omap_get_divisor(port, baud);

	/* calculate wakeup latency constraint */
	up->calc_latency = (USEC_PER_SEC * up->port.fifosize) / (baud / 8);
	up->latency = up->calc_latency;
	schedule_work(&up->qos_work);

	up->dll = quot & 0xff;
	up->dlh = quot >> 8;
	up->mdr1 = UART_OMAP_MDR1_DISABLE;

	up->fcr = UART_FCR_R_TRIG_01 | UART_FCR_T_TRIG_01 |
			UART_FCR_ENABLE_FIFO;

	/*
	 * Ok, we're now changing the port state. Do it with
	 * interrupts disabled.
	 */
	spin_lock_irqsave(&up->port.lock, flags);

	/*
	 * Update the per-port timeout.
	 */
	uart_update_timeout(port, termios->c_cflag, baud);

	up->port.read_status_mask = UART_LSR_OE | UART_LSR_THRE | UART_LSR_DR;
	if (termios->c_iflag & INPCK)
		up->port.read_status_mask |= UART_LSR_FE | UART_LSR_PE;
	if (termios->c_iflag & (BRKINT | PARMRK))
		up->port.read_status_mask |= UART_LSR_BI;

	/*
	 * Characters to ignore
	 */
	up->port.ignore_status_mask = 0;
	if (termios->c_iflag & IGNPAR)
		up->port.ignore_status_mask |= UART_LSR_PE | UART_LSR_FE;
	if (termios->c_iflag & IGNBRK) {
		up->port.ignore_status_mask |= UART_LSR_BI;
		/*
		 * If we're ignoring parity and break indicators,
		 * ignore overruns too (for real raw support).
		 */
		if (termios->c_iflag & IGNPAR)
			up->port.ignore_status_mask |= UART_LSR_OE;
	}

	/*
	 * ignore all characters if CREAD is not set
	 */
	if ((termios->c_cflag & CREAD) == 0)
		up->port.ignore_status_mask |= UART_LSR_DR;

	/*
	 * Modem status interrupts
	 */
	up->ier &= ~UART_IER_MSI;
	if (UART_ENABLE_MS(&up->port, termios->c_cflag))
		up->ier |= UART_IER_MSI;
	serial_out(up, UART_IER, up->ier);
	serial_out(up, UART_LCR, cval);		/* reset DLAB */
	up->lcr = cval;
	up->scr = 0;

	/* FIFOs and DMA Settings */

	/* FCR can be changed only when the
	 * baud clock is not running
	 * DLL_REG and DLH_REG set to 0.
	 */
	serial_out(up, UART_LCR, UART_LCR_CONF_MODE_A);
	serial_out(up, UART_DLL, 0);
	serial_out(up, UART_DLM, 0);
	serial_out(up, UART_LCR, 0);

	serial_out(up, UART_LCR, UART_LCR_CONF_MODE_B);

	up->efr = serial_in(up, UART_EFR) & ~UART_EFR_ECB;
	up->efr &= ~UART_EFR_SCD;
	serial_out(up, UART_EFR, up->efr | UART_EFR_ECB);

	serial_out(up, UART_LCR, UART_LCR_CONF_MODE_A);
	up->mcr = serial_in(up, UART_MCR) & ~UART_MCR_TCRTLR;
	serial_out(up, UART_MCR, up->mcr | UART_MCR_TCRTLR);
	/* FIFO ENABLE, DMA MODE */

	up->scr |= OMAP_UART_SCR_RX_TRIG_GRANU1_MASK;
	/*
	 * NOTE: Setting OMAP_UART_SCR_RX_TRIG_GRANU1_MASK
	 * sets Enables the granularity of 1 for TRIGGER RX
	 * level. Along with setting RX FIFO trigger level
	 * to 1 (as noted below, 16 characters) and TLR[3:0]
	 * to zero this will result RX FIFO threshold level
	 * to 1 character, instead of 16 as noted in comment
	 * below.
	 */

	/* Set receive FIFO threshold to 16 characters and
	 * transmit FIFO threshold to 32 spaces
	 */
	up->fcr &= ~OMAP_UART_FCR_RX_FIFO_TRIG_MASK;
	up->fcr &= ~OMAP_UART_FCR_TX_FIFO_TRIG_MASK;
	up->fcr |= UART_FCR6_R_TRIGGER_16 | UART_FCR6_T_TRIGGER_24 |
		UART_FCR_ENABLE_FIFO;

	serial_out(up, UART_FCR, up->fcr);
	serial_out(up, UART_LCR, UART_LCR_CONF_MODE_B);

	serial_out(up, UART_OMAP_SCR, up->scr);

	/* Reset UART_MCR_TCRTLR: this must be done with the EFR_ECB bit set */
	serial_out(up, UART_LCR, UART_LCR_CONF_MODE_A);
	serial_out(up, UART_MCR, up->mcr);
	serial_out(up, UART_LCR, UART_LCR_CONF_MODE_B);
	serial_out(up, UART_EFR, up->efr);
	serial_out(up, UART_LCR, UART_LCR_CONF_MODE_A);

	/* Protocol, Baud Rate, and Interrupt Settings */

	if (up->errata & UART_ERRATA_i202_MDR1_ACCESS)
		serial_omap_mdr1_errataset(up, up->mdr1);
	else
		serial_out(up, UART_OMAP_MDR1, up->mdr1);

	serial_out(up, UART_LCR, UART_LCR_CONF_MODE_B);
	serial_out(up, UART_EFR, up->efr | UART_EFR_ECB);

	serial_out(up, UART_LCR, 0);
	serial_out(up, UART_IER, 0);
	serial_out(up, UART_LCR, UART_LCR_CONF_MODE_B);

	serial_out(up, UART_DLL, up->dll);	/* LS of divisor */
	serial_out(up, UART_DLM, up->dlh);	/* MS of divisor */

	serial_out(up, UART_LCR, 0);
	serial_out(up, UART_IER, up->ier);
	serial_out(up, UART_LCR, UART_LCR_CONF_MODE_B);

	serial_out(up, UART_EFR, up->efr);
	serial_out(up, UART_LCR, cval);

	if (!serial_omap_baud_is_mode16(port, baud))
		up->mdr1 = UART_OMAP_MDR1_13X_MODE;
	else
		up->mdr1 = UART_OMAP_MDR1_16X_MODE;

	if (up->errata & UART_ERRATA_i202_MDR1_ACCESS)
		serial_omap_mdr1_errataset(up, up->mdr1);
	else
		serial_out(up, UART_OMAP_MDR1, up->mdr1);

	/* Configure flow control */
	serial_out(up, UART_LCR, UART_LCR_CONF_MODE_B);

	/* XON1/XOFF1 accessible mode B, TCRTLR=0, ECB=0 */
	serial_out(up, UART_XON1, termios->c_cc[VSTART]);
	serial_out(up, UART_XOFF1, termios->c_cc[VSTOP]);

	/* Enable access to TCR/TLR */
	serial_out(up, UART_EFR, up->efr | UART_EFR_ECB);
	serial_out(up, UART_LCR, UART_LCR_CONF_MODE_A);
	serial_out(up, UART_MCR, up->mcr | UART_MCR_TCRTLR);

	serial_out(up, UART_TI752_TCR, OMAP_UART_TCR_TRIG);

	up->port.status &= ~(UPSTAT_AUTOCTS | UPSTAT_AUTORTS | UPSTAT_AUTOXOFF);

	if (termios->c_cflag & CRTSCTS && up->port.flags & UPF_HARD_FLOW) {
		/* Enable AUTOCTS (autoRTS is enabled when RTS is raised) */
		up->port.status |= UPSTAT_AUTOCTS | UPSTAT_AUTORTS;
		up->efr |= UART_EFR_CTS;
	} else {
		/* Disable AUTORTS and AUTOCTS */
		up->efr &= ~(UART_EFR_CTS | UART_EFR_RTS);
	}

	if (up->port.flags & UPF_SOFT_FLOW) {
		/* clear SW control mode bits */
		up->efr &= OMAP_UART_SW_CLR;

		/*
		 * IXON Flag:
		 * Enable XON/XOFF flow control on input.
		 * Receiver compares XON1, XOFF1.
		 */
		if (termios->c_iflag & IXON)
			up->efr |= OMAP_UART_SW_RX;

		/*
		 * IXOFF Flag:
		 * Enable XON/XOFF flow control on output.
		 * Transmit XON1, XOFF1
		 */
		if (termios->c_iflag & IXOFF) {
			up->port.status |= UPSTAT_AUTOXOFF;
			up->efr |= OMAP_UART_SW_TX;
		}

		/*
		 * IXANY Flag:
		 * Enable any character to restart output.
		 * Operation resumes after receiving any
		 * character after recognition of the XOFF character
		 */
		if (termios->c_iflag & IXANY)
			up->mcr |= UART_MCR_XONANY;
		else
			up->mcr &= ~UART_MCR_XONANY;
	}
	serial_out(up, UART_MCR, up->mcr);
	serial_out(up, UART_LCR, UART_LCR_CONF_MODE_B);
	serial_out(up, UART_EFR, up->efr);
	serial_out(up, UART_LCR, up->lcr);

	serial_omap_set_mctrl(&up->port, up->port.mctrl);

	spin_unlock_irqrestore(&up->port.lock, flags);
	dev_dbg(up->port.dev, "serial_omap_set_termios+%d\n", up->port.line);
}

static void
serial_omap_pm(struct uart_port *port, unsigned int state,
	       unsigned int oldstate)
{
	struct uart_omap_port *up = to_uart_omap_port(port);
	unsigned char efr;

	dev_dbg(up->port.dev, "serial_omap_pm+%d\n", up->port.line);

	serial_out(up, UART_LCR, UART_LCR_CONF_MODE_B);
	efr = serial_in(up, UART_EFR);
	serial_out(up, UART_EFR, efr | UART_EFR_ECB);
	serial_out(up, UART_LCR, 0);

	serial_out(up, UART_IER, (state != 0) ? UART_IERX_SLEEP : 0);
	serial_out(up, UART_LCR, UART_LCR_CONF_MODE_B);
	serial_out(up, UART_EFR, efr);
	serial_out(up, UART_LCR, 0);
}

static void serial_omap_release_port(struct uart_port *port)
{
	dev_dbg(port->dev, "serial_omap_release_port+\n");
}

static int serial_omap_request_port(struct uart_port *port)
{
	dev_dbg(port->dev, "serial_omap_request_port+\n");
	return 0;
}

static void serial_omap_config_port(struct uart_port *port, int flags)
{
	struct uart_omap_port *up = to_uart_omap_port(port);

	dev_dbg(up->port.dev, "serial_omap_config_port+%d\n",
							up->port.line);
	up->port.type = PORT_OMAP;
	up->port.flags |= UPF_SOFT_FLOW | UPF_HARD_FLOW;
}

static int
serial_omap_verify_port(struct uart_port *port, struct serial_struct *ser)
{
	/* we don't want the core code to modify any port params */
	dev_dbg(port->dev, "serial_omap_verify_port+\n");
	return -EINVAL;
}

static const char *
serial_omap_type(struct uart_port *port)
{
	struct uart_omap_port *up = to_uart_omap_port(port);

	dev_dbg(up->port.dev, "serial_omap_type+%d\n", up->port.line);
	return up->name;
}

#define BOTH_EMPTY (UART_LSR_TEMT | UART_LSR_THRE)

static void __maybe_unused wait_for_xmitr(struct uart_omap_port *up)
{
	unsigned int status, tmout = 10000;

	/* Wait up to 10ms for the character(s) to be sent. */
	do {
		status = serial_in(up, UART_LSR);

		if (status & UART_LSR_BI)
			up->lsr_break_flag = UART_LSR_BI;

		if (--tmout == 0)
			break;
		udelay(1);
	} while ((status & BOTH_EMPTY) != BOTH_EMPTY);

	/* Wait up to 1s for flow control if necessary */
	if (up->port.flags & UPF_CONS_FLOW) {
		tmout = 1000000;
		for (tmout = 1000000; tmout; tmout--) {
			unsigned int msr = serial_in(up, UART_MSR);

			up->msr_saved_flags |= msr & MSR_SAVE_FLAGS;
			if (msr & UART_MSR_CTS)
				break;

			udelay(1);
		}
	}
}

#ifdef CONFIG_CONSOLE_POLL

static void serial_omap_poll_put_char(struct uart_port *port, unsigned char ch)
{
	struct uart_omap_port *up = to_uart_omap_port(port);

	wait_for_xmitr(up);
	serial_out(up, UART_TX, ch);
}

static int serial_omap_poll_get_char(struct uart_port *port)
{
	struct uart_omap_port *up = to_uart_omap_port(port);
	unsigned int status;

	status = serial_in(up, UART_LSR);
	if (!(status & UART_LSR_DR)) {
		status = NO_POLL_CHAR;
		goto out;
	}

	status = serial_in(up, UART_RX);

out:
	return status;
}

#endif /* CONFIG_CONSOLE_POLL */

#ifdef CONFIG_SERIAL_OMAP_CONSOLE

#ifdef CONFIG_SERIAL_EARLYCON
static unsigned int omap_serial_early_in(struct uart_port *port, int offset)
{
	offset <<= port->regshift;
	return readw(port->membase + offset);
}

static void omap_serial_early_out(struct uart_port *port, int offset,
				  int value)
{
	offset <<= port->regshift;
	writew(value, port->membase + offset);
}

static void omap_serial_early_putc(struct uart_port *port, unsigned char c)
{
	unsigned int status;

	for (;;) {
		status = omap_serial_early_in(port, UART_LSR);
		if ((status & BOTH_EMPTY) == BOTH_EMPTY)
			break;
		cpu_relax();
	}
	omap_serial_early_out(port, UART_TX, c);
}

static void early_omap_serial_write(struct console *console, const char *s,
				    unsigned int count)
{
	struct earlycon_device *device = console->data;
	struct uart_port *port = &device->port;

	uart_console_write(port, s, count, omap_serial_early_putc);
}

static int __init early_omap_serial_setup(struct earlycon_device *device,
					  const char *options)
{
	struct uart_port *port = &device->port;

	if (!(device->port.membase || device->port.iobase))
		return -ENODEV;

	port->regshift = 2;
	device->con->write = early_omap_serial_write;
	return 0;
}

OF_EARLYCON_DECLARE(omapserial, "ti,omap2-uart", early_omap_serial_setup);
OF_EARLYCON_DECLARE(omapserial, "ti,omap3-uart", early_omap_serial_setup);
OF_EARLYCON_DECLARE(omapserial, "ti,omap4-uart", early_omap_serial_setup);
#endif /* CONFIG_SERIAL_EARLYCON */

static struct uart_omap_port *serial_omap_console_ports[OMAP_MAX_HSUART_PORTS];

static struct uart_driver serial_omap_reg;

static void serial_omap_console_putchar(struct uart_port *port, unsigned char ch)
{
	struct uart_omap_port *up = to_uart_omap_port(port);

	wait_for_xmitr(up);
	serial_out(up, UART_TX, ch);
}

static void
serial_omap_console_write(struct console *co, const char *s,
		unsigned int count)
{
	struct uart_omap_port *up = serial_omap_console_ports[co->index];
	unsigned long flags;
	unsigned int ier;
	int locked = 1;

	local_irq_save(flags);
	if (up->port.sysrq)
		locked = 0;
	else if (oops_in_progress)
		locked = spin_trylock(&up->port.lock);
	else
		spin_lock(&up->port.lock);

	/*
	 * First save the IER then disable the interrupts
	 */
	ier = serial_in(up, UART_IER);
	serial_out(up, UART_IER, 0);

	uart_console_write(&up->port, s, count, serial_omap_console_putchar);

	/*
	 * Finally, wait for transmitter to become empty
	 * and restore the IER
	 */
	wait_for_xmitr(up);
	serial_out(up, UART_IER, ier);
	/*
	 * The receive handling will happen properly because the
	 * receive ready bit will still be set; it is not cleared
	 * on read.  However, modem control will not, we must
	 * call it if we have saved something in the saved flags
	 * while processing with interrupts off.
	 */
	if (up->msr_saved_flags)
		check_modem_status(up);

	if (locked)
		spin_unlock(&up->port.lock);
	local_irq_restore(flags);
}

static int __init
serial_omap_console_setup(struct console *co, char *options)
{
	struct uart_omap_port *up;
	int baud = 115200;
	int bits = 8;
	int parity = 'n';
	int flow = 'n';

	if (serial_omap_console_ports[co->index] == NULL)
		return -ENODEV;
	up = serial_omap_console_ports[co->index];

	if (options)
		uart_parse_options(options, &baud, &parity, &bits, &flow);

	return uart_set_options(&up->port, co, baud, parity, bits, flow);
}

static struct console serial_omap_console = {
	.name		= OMAP_SERIAL_NAME,
	.write		= serial_omap_console_write,
	.device		= uart_console_device,
	.setup		= serial_omap_console_setup,
	.flags		= CON_PRINTBUFFER,
	.index		= -1,
	.data		= &serial_omap_reg,
};

static void serial_omap_add_console_port(struct uart_omap_port *up)
{
	serial_omap_console_ports[up->port.line] = up;
}

#define OMAP_CONSOLE	(&serial_omap_console)

#else

#define OMAP_CONSOLE	NULL

static inline void serial_omap_add_console_port(struct uart_omap_port *up)
{}

#endif

/* Enable or disable the rs485 support */
static int
serial_omap_config_rs485(struct uart_port *port, struct serial_rs485 *rs485)
{
	struct uart_omap_port *up = to_uart_omap_port(port);
	unsigned int mode;
	int val;

	/* Disable interrupts from this port */
	mode = up->ier;
	up->ier = 0;
	serial_out(up, UART_IER, 0);

	if (up->rts_gpiod) {
		/* enable / disable rts */
		val = (rs485->flags & SER_RS485_ENABLED) ?
			SER_RS485_RTS_AFTER_SEND : SER_RS485_RTS_ON_SEND;
		val = (rs485->flags & val) ? 1 : 0;
		gpiod_set_value(up->rts_gpiod, val);
	}

	/* Enable interrupts */
	up->ier = mode;
	serial_out(up, UART_IER, up->ier);

	/* If RS-485 is disabled, make sure the THR interrupt is fired when
	 * TX FIFO is below the trigger level.
	 */
	if (!(rs485->flags & SER_RS485_ENABLED) &&
	    (up->scr & OMAP_UART_SCR_TX_EMPTY)) {
		up->scr &= ~OMAP_UART_SCR_TX_EMPTY;
		serial_out(up, UART_OMAP_SCR, up->scr);
	}

	return 0;
}

static const struct uart_ops serial_omap_pops = {
	.tx_empty	= serial_omap_tx_empty,
	.set_mctrl	= serial_omap_set_mctrl,
	.get_mctrl	= serial_omap_get_mctrl,
	.stop_tx	= serial_omap_stop_tx,
	.start_tx	= serial_omap_start_tx,
	.throttle	= serial_omap_throttle,
	.unthrottle	= serial_omap_unthrottle,
	.stop_rx	= serial_omap_stop_rx,
	.enable_ms	= serial_omap_enable_ms,
	.break_ctl	= serial_omap_break_ctl,
	.startup	= serial_omap_startup,
	.shutdown	= serial_omap_shutdown,
	.set_termios	= serial_omap_set_termios,
	.pm		= serial_omap_pm,
	.type		= serial_omap_type,
	.release_port	= serial_omap_release_port,
	.request_port	= serial_omap_request_port,
	.config_port	= serial_omap_config_port,
	.verify_port	= serial_omap_verify_port,
#ifdef CONFIG_CONSOLE_POLL
	.poll_put_char  = serial_omap_poll_put_char,
	.poll_get_char  = serial_omap_poll_get_char,
#endif
};

static struct uart_driver serial_omap_reg = {
	.owner		= THIS_MODULE,
	.driver_name	= "OMAP-SERIAL",
	.dev_name	= OMAP_SERIAL_NAME,
	.nr		= OMAP_MAX_HSUART_PORTS,
	.cons		= OMAP_CONSOLE,
};

#ifdef CONFIG_PM_SLEEP
static int serial_omap_prepare(struct device *dev)
{
	struct uart_omap_port *up = dev_get_drvdata(dev);

	up->is_suspending = true;

	return 0;
}

static void serial_omap_complete(struct device *dev)
{
	struct uart_omap_port *up = dev_get_drvdata(dev);

	up->is_suspending = false;
}

static int serial_omap_suspend(struct device *dev)
{
	struct uart_omap_port *up = dev_get_drvdata(dev);

	uart_suspend_port(&serial_omap_reg, &up->port);
	flush_work(&up->qos_work);

	if (device_may_wakeup(dev))
		serial_omap_enable_wakeup(up, true);
	else
		serial_omap_enable_wakeup(up, false);

	return 0;
}

static int serial_omap_resume(struct device *dev)
{
	struct uart_omap_port *up = dev_get_drvdata(dev);

	if (device_may_wakeup(dev))
		serial_omap_enable_wakeup(up, false);

	uart_resume_port(&serial_omap_reg, &up->port);

	return 0;
}
#else
#define serial_omap_prepare NULL
#define serial_omap_complete NULL
#endif /* CONFIG_PM_SLEEP */

static void omap_serial_fill_features_erratas(struct uart_omap_port *up)
{
	u32 mvr, scheme;
	u16 revision, major, minor;

	mvr = readl(up->port.membase + (UART_OMAP_MVER << up->port.regshift));

	/* Check revision register scheme */
	scheme = mvr >> OMAP_UART_MVR_SCHEME_SHIFT;

	switch (scheme) {
	case 0: /* Legacy Scheme: OMAP2/3 */
		/* MINOR_REV[0:4], MAJOR_REV[4:7] */
		major = (mvr & OMAP_UART_LEGACY_MVR_MAJ_MASK) >>
					OMAP_UART_LEGACY_MVR_MAJ_SHIFT;
		minor = (mvr & OMAP_UART_LEGACY_MVR_MIN_MASK);
		break;
	case 1:
		/* New Scheme: OMAP4+ */
		/* MINOR_REV[0:5], MAJOR_REV[8:10] */
		major = (mvr & OMAP_UART_MVR_MAJ_MASK) >>
					OMAP_UART_MVR_MAJ_SHIFT;
		minor = (mvr & OMAP_UART_MVR_MIN_MASK);
		break;
	default:
		dev_warn(up->dev,
			"Unknown %s revision, defaulting to highest\n",
			up->name);
		/* highest possible revision */
		major = 0xff;
		minor = 0xff;
	}

	/* normalize revision for the driver */
	revision = UART_BUILD_REVISION(major, minor);

	switch (revision) {
	case OMAP_UART_REV_46:
		up->errata |= (UART_ERRATA_i202_MDR1_ACCESS |
				UART_ERRATA_i291_DMA_FORCEIDLE);
		break;
	case OMAP_UART_REV_52:
		up->errata |= (UART_ERRATA_i202_MDR1_ACCESS |
				UART_ERRATA_i291_DMA_FORCEIDLE);
		up->features |= OMAP_UART_WER_HAS_TX_WAKEUP;
		break;
	case OMAP_UART_REV_63:
		up->errata |= UART_ERRATA_i202_MDR1_ACCESS;
		up->features |= OMAP_UART_WER_HAS_TX_WAKEUP;
		break;
	default:
		break;
	}
}

static struct omap_uart_port_info *of_get_uart_port_info(struct device *dev)
{
	struct omap_uart_port_info *omap_up_info;

	omap_up_info = devm_kzalloc(dev, sizeof(*omap_up_info), GFP_KERNEL);
	if (!omap_up_info)
		return NULL; /* out of memory */

	of_property_read_u32(dev->of_node, "clock-frequency",
					 &omap_up_info->uartclk);

	omap_up_info->flags = UPF_BOOT_AUTOCONF;

	return omap_up_info;
}

static int serial_omap_probe_rs485(struct uart_omap_port *up,
				   struct device *dev)
{
	struct serial_rs485 *rs485conf = &up->port.rs485;
	struct device_node *np = dev->of_node;
	enum gpiod_flags gflags;
	int ret;

	rs485conf->flags = 0;
	up->rts_gpiod = NULL;

	if (!np)
		return 0;

	ret = uart_get_rs485_mode(&up->port);
	if (ret)
		return ret;

	if (of_property_read_bool(np, "rs485-rts-active-high")) {
		rs485conf->flags |= SER_RS485_RTS_ON_SEND;
		rs485conf->flags &= ~SER_RS485_RTS_AFTER_SEND;
	} else {
		rs485conf->flags &= ~SER_RS485_RTS_ON_SEND;
		rs485conf->flags |= SER_RS485_RTS_AFTER_SEND;
	}

	/* check for tx enable gpio */
	gflags = rs485conf->flags & SER_RS485_RTS_AFTER_SEND ?
		GPIOD_OUT_HIGH : GPIOD_OUT_LOW;
	up->rts_gpiod = devm_gpiod_get_optional(dev, "rts", gflags);
	if (IS_ERR(up->rts_gpiod)) {
		ret = PTR_ERR(up->rts_gpiod);
	        if (ret == -EPROBE_DEFER)
			return ret;
		/*
		 * FIXME: the code historically ignored any other error than
		 * -EPROBE_DEFER and just went on without GPIO.
		 */
		up->rts_gpiod = NULL;
	} else {
		gpiod_set_consumer_name(up->rts_gpiod, "omap-serial");
	}

	return 0;
}

static int serial_omap_probe(struct platform_device *pdev)
{
	struct omap_uart_port_info *omap_up_info = dev_get_platdata(&pdev->dev);
	struct uart_omap_port *up;
	struct resource *mem;
	void __iomem *base;
	int uartirq = 0;
	int wakeirq = 0;
	int ret;

	/* The optional wakeirq may be specified in the board dts file */
	if (pdev->dev.of_node) {
		uartirq = irq_of_parse_and_map(pdev->dev.of_node, 0);
		if (!uartirq)
			return -EPROBE_DEFER;
		wakeirq = irq_of_parse_and_map(pdev->dev.of_node, 1);
		omap_up_info = of_get_uart_port_info(&pdev->dev);
		pdev->dev.platform_data = omap_up_info;
	} else {
		uartirq = platform_get_irq(pdev, 0);
		if (uartirq < 0)
			return -EPROBE_DEFER;
	}

	up = devm_kzalloc(&pdev->dev, sizeof(*up), GFP_KERNEL);
	if (!up)
		return -ENOMEM;

	mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	base = devm_ioremap_resource(&pdev->dev, mem);
	if (IS_ERR(base))
		return PTR_ERR(base);

	up->dev = &pdev->dev;
	up->port.dev = &pdev->dev;
	up->port.type = PORT_OMAP;
	up->port.iotype = UPIO_MEM;
	up->port.irq = uartirq;
	up->port.regshift = 2;
	up->port.fifosize = 64;
	up->port.ops = &serial_omap_pops;
	up->port.has_sysrq = IS_ENABLED(CONFIG_SERIAL_OMAP_CONSOLE);

	if (pdev->dev.of_node)
		ret = of_alias_get_id(pdev->dev.of_node, "serial");
	else
		ret = pdev->id;

	if (ret < 0) {
		dev_err(&pdev->dev, "failed to get alias/pdev id, errno %d\n",
			ret);
		goto err_port_line;
	}
	up->port.line = ret;

	if (up->port.line >= OMAP_MAX_HSUART_PORTS) {
		dev_err(&pdev->dev, "uart ID %d >  MAX %d.\n", up->port.line,
			OMAP_MAX_HSUART_PORTS);
		ret = -ENXIO;
		goto err_port_line;
	}

	up->wakeirq = wakeirq;
	if (!up->wakeirq)
		dev_info(up->port.dev, "no wakeirq for uart%d\n",
			 up->port.line);

	ret = serial_omap_probe_rs485(up, &pdev->dev);
	if (ret < 0)
		goto err_rs485;

	sprintf(up->name, "OMAP UART%d", up->port.line);
	up->port.mapbase = mem->start;
	up->port.membase = base;
	up->port.flags = omap_up_info->flags;
	up->port.uartclk = omap_up_info->uartclk;
	up->port.rs485_config = serial_omap_config_rs485;
	if (!up->port.uartclk) {
		up->port.uartclk = DEFAULT_CLK_SPEED;
		dev_warn(&pdev->dev,
			 "No clock speed specified: using default: %d\n",
			 DEFAULT_CLK_SPEED);
	}

	up->latency = PM_QOS_CPU_LATENCY_DEFAULT_VALUE;
	up->calc_latency = PM_QOS_CPU_LATENCY_DEFAULT_VALUE;
	cpu_latency_qos_add_request(&up->pm_qos_request, up->latency);
	INIT_WORK(&up->qos_work, serial_omap_uart_qos_work);

	platform_set_drvdata(pdev, up);
	if (omap_up_info->autosuspend_timeout == 0)
		omap_up_info->autosuspend_timeout = -1;

	device_init_wakeup(up->dev, true);

	pm_runtime_enable(&pdev->dev);

	pm_runtime_get_sync(&pdev->dev);

	omap_serial_fill_features_erratas(up);

	ui[up->port.line] = up;
	serial_omap_add_console_port(up);

	ret = uart_add_one_port(&serial_omap_reg, &up->port);
	if (ret != 0)
		goto err_add_port;

	return 0;

err_add_port:
	pm_runtime_put_sync(&pdev->dev);
	pm_runtime_disable(&pdev->dev);
	cpu_latency_qos_remove_request(&up->pm_qos_request);
	device_init_wakeup(up->dev, false);
err_rs485:
err_port_line:
	return ret;
}

static int serial_omap_remove(struct platform_device *dev)
{
	struct uart_omap_port *up = platform_get_drvdata(dev);

	pm_runtime_get_sync(up->dev);

	uart_remove_one_port(&serial_omap_reg, &up->port);

	pm_runtime_put_sync(up->dev);
	pm_runtime_disable(up->dev);
	cpu_latency_qos_remove_request(&up->pm_qos_request);
	device_init_wakeup(&dev->dev, false);

	return 0;
}

/*
 * Work Around for Errata i202 (2430, 3430, 3630, 4430 and 4460)
 * The access to uart register after MDR1 Access
 * causes UART to corrupt data.
 *
 * Need a delay =
 * 5 L4 clock cycles + 5 UART functional clock cycle (@48MHz = ~0.2uS)
 * give 10 times as much
 */
static void serial_omap_mdr1_errataset(struct uart_omap_port *up, u8 mdr1)
{
	u8 timeout = 255;

	serial_out(up, UART_OMAP_MDR1, mdr1);
	udelay(2);
	serial_out(up, UART_FCR, up->fcr | UART_FCR_CLEAR_XMIT |
			UART_FCR_CLEAR_RCVR);
	/*
	 * Wait for FIFO to empty: when empty, RX_FIFO_E bit is 0 and
	 * TX_FIFO_E bit is 1.
	 */
	while (UART_LSR_THRE != (serial_in(up, UART_LSR) &
				(UART_LSR_THRE | UART_LSR_DR))) {
		timeout--;
		if (!timeout) {
			/* Should *never* happen. we warn and carry on */
			dev_crit(up->dev, "Errata i202: timedout %x\n",
						serial_in(up, UART_LSR));
			break;
		}
		udelay(1);
	}
}

#ifdef CONFIG_PM
static void serial_omap_restore_context(struct uart_omap_port *up)
{
	if (up->errata & UART_ERRATA_i202_MDR1_ACCESS)
		serial_omap_mdr1_errataset(up, UART_OMAP_MDR1_DISABLE);
	else
		serial_out(up, UART_OMAP_MDR1, UART_OMAP_MDR1_DISABLE);

	serial_out(up, UART_LCR, UART_LCR_CONF_MODE_B); /* Config B mode */
	serial_out(up, UART_EFR, UART_EFR_ECB);
	serial_out(up, UART_LCR, 0x0); /* Operational mode */
	serial_out(up, UART_IER, 0x0);
	serial_out(up, UART_LCR, UART_LCR_CONF_MODE_B); /* Config B mode */
	serial_out(up, UART_DLL, up->dll);
	serial_out(up, UART_DLM, up->dlh);
	serial_out(up, UART_LCR, 0x0); /* Operational mode */
	serial_out(up, UART_IER, up->ier);
	serial_out(up, UART_FCR, up->fcr);
	serial_out(up, UART_LCR, UART_LCR_CONF_MODE_A);
	serial_out(up, UART_MCR, up->mcr);
	serial_out(up, UART_LCR, UART_LCR_CONF_MODE_B); /* Config B mode */
	serial_out(up, UART_OMAP_SCR, up->scr);
	serial_out(up, UART_EFR, up->efr);
	serial_out(up, UART_LCR, up->lcr);
	if (up->errata & UART_ERRATA_i202_MDR1_ACCESS)
		serial_omap_mdr1_errataset(up, up->mdr1);
	else
		serial_out(up, UART_OMAP_MDR1, up->mdr1);
	serial_out(up, UART_OMAP_WER, up->wer);
}

static int serial_omap_runtime_suspend(struct device *dev)
{
	struct uart_omap_port *up = dev_get_drvdata(dev);

	if (!up)
		return -EINVAL;

	/*
	* When using 'no_console_suspend', the console UART must not be
	* suspended. Since driver suspend is managed by runtime suspend,
	* preventing runtime suspend (by returning error) will keep device
	* active during suspend.
	*/
	if (up->is_suspending && !console_suspend_enabled &&
	    uart_console(&up->port))
		return -EBUSY;

	up->context_loss_cnt = serial_omap_get_context_loss_count(up);

	serial_omap_enable_wakeup(up, true);

	up->latency = PM_QOS_CPU_LATENCY_DEFAULT_VALUE;
	schedule_work(&up->qos_work);

	return 0;
}

static int serial_omap_runtime_resume(struct device *dev)
{
	struct uart_omap_port *up = dev_get_drvdata(dev);

	int loss_cnt = serial_omap_get_context_loss_count(up);

	serial_omap_enable_wakeup(up, false);

	if (loss_cnt < 0) {
		dev_dbg(dev, "serial_omap_get_context_loss_count failed : %d\n",
			loss_cnt);
		serial_omap_restore_context(up);
	} else if (up->context_loss_cnt != loss_cnt) {
		serial_omap_restore_context(up);
	}
	up->latency = up->calc_latency;
	schedule_work(&up->qos_work);

	return 0;
}
#endif

static const struct dev_pm_ops serial_omap_dev_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(serial_omap_suspend, serial_omap_resume)
	SET_RUNTIME_PM_OPS(serial_omap_runtime_suspend,
				serial_omap_runtime_resume, NULL)
	.prepare        = serial_omap_prepare,
	.complete       = serial_omap_complete,
};

#if defined(CONFIG_OF)
static const struct of_device_id omap_serial_of_match[] = {
	{ .compatible = "ti,omap2-uart" },
	{ .compatible = "ti,omap3-uart" },
	{ .compatible = "ti,omap4-uart" },
	{},
};
MODULE_DEVICE_TABLE(of, omap_serial_of_match);
#endif

static struct platform_driver serial_omap_driver = {
	.probe          = serial_omap_probe,
	.remove         = serial_omap_remove,
	.driver		= {
		.name	= OMAP_SERIAL_DRIVER_NAME,
		.pm	= &serial_omap_dev_pm_ops,
		.of_match_table = of_match_ptr(omap_serial_of_match),
	},
};

static int __init serial_omap_init(void)
{
	int ret;

	ret = uart_register_driver(&serial_omap_reg);
	if (ret != 0)
		return ret;
	ret = platform_driver_register(&serial_omap_driver);
	if (ret != 0)
		uart_unregister_driver(&serial_omap_reg);
	return ret;
}

static void __exit serial_omap_exit(void)
{
	platform_driver_unregister(&serial_omap_driver);
	uart_unregister_driver(&serial_omap_reg);
}

module_init(serial_omap_init);
module_exit(serial_omap_exit);

MODULE_DESCRIPTION("OMAP High Speed UART driver");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Texas Instruments Inc");
