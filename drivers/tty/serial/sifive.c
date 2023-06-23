// SPDX-License-Identifier: GPL-2.0+
/*
 * SiFive UART driver
 * Copyright (C) 2018 Paul Walmsley <paul@pwsan.com>
 * Copyright (C) 2018-2019 SiFive
 *
 * Based partially on:
 * - drivers/tty/serial/pxa.c
 * - drivers/tty/serial/amba-pl011.c
 * - drivers/tty/serial/uartlite.c
 * - drivers/tty/serial/omap-serial.c
 * - drivers/pwm/pwm-sifive.c
 *
 * See the following sources for further documentation:
 * - Chapter 19 "Universal Asynchronous Receiver/Transmitter (UART)" of
 *   SiFive FE310-G000 v2p3
 * - The tree/master/src/main/scala/devices/uart directory of
 *   https://github.com/sifive/sifive-blocks/
 *
 * The SiFive UART design is not 8250-compatible.  The following common
 * features are not supported:
 * - Word lengths other than 8 bits
 * - Break handling
 * - Parity
 * - Flow control
 * - Modem signals (DSR, RI, etc.)
 * On the other hand, the design is free from the baggage of the 8250
 * programming model.
 */

#include <linux/clk.h>
#include <linux/console.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <linux/serial_core.h>
#include <linux/serial_reg.h>
#include <linux/slab.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>

/*
 * Register offsets
 */

/* TXDATA */
#define SIFIVE_SERIAL_TXDATA_OFFS		0x0
#define SIFIVE_SERIAL_TXDATA_FULL_SHIFT		31
#define SIFIVE_SERIAL_TXDATA_FULL_MASK		(1 << SIFIVE_SERIAL_TXDATA_FULL_SHIFT)
#define SIFIVE_SERIAL_TXDATA_DATA_SHIFT		0
#define SIFIVE_SERIAL_TXDATA_DATA_MASK		(0xff << SIFIVE_SERIAL_TXDATA_DATA_SHIFT)

/* RXDATA */
#define SIFIVE_SERIAL_RXDATA_OFFS		0x4
#define SIFIVE_SERIAL_RXDATA_EMPTY_SHIFT	31
#define SIFIVE_SERIAL_RXDATA_EMPTY_MASK		(1 << SIFIVE_SERIAL_RXDATA_EMPTY_SHIFT)
#define SIFIVE_SERIAL_RXDATA_DATA_SHIFT		0
#define SIFIVE_SERIAL_RXDATA_DATA_MASK		(0xff << SIFIVE_SERIAL_RXDATA_DATA_SHIFT)

/* TXCTRL */
#define SIFIVE_SERIAL_TXCTRL_OFFS		0x8
#define SIFIVE_SERIAL_TXCTRL_TXCNT_SHIFT	16
#define SIFIVE_SERIAL_TXCTRL_TXCNT_MASK		(0x7 << SIFIVE_SERIAL_TXCTRL_TXCNT_SHIFT)
#define SIFIVE_SERIAL_TXCTRL_NSTOP_SHIFT	1
#define SIFIVE_SERIAL_TXCTRL_NSTOP_MASK		(1 << SIFIVE_SERIAL_TXCTRL_NSTOP_SHIFT)
#define SIFIVE_SERIAL_TXCTRL_TXEN_SHIFT		0
#define SIFIVE_SERIAL_TXCTRL_TXEN_MASK		(1 << SIFIVE_SERIAL_TXCTRL_TXEN_SHIFT)

/* RXCTRL */
#define SIFIVE_SERIAL_RXCTRL_OFFS		0xC
#define SIFIVE_SERIAL_RXCTRL_RXCNT_SHIFT	16
#define SIFIVE_SERIAL_RXCTRL_RXCNT_MASK		(0x7 << SIFIVE_SERIAL_TXCTRL_TXCNT_SHIFT)
#define SIFIVE_SERIAL_RXCTRL_RXEN_SHIFT		0
#define SIFIVE_SERIAL_RXCTRL_RXEN_MASK		(1 << SIFIVE_SERIAL_RXCTRL_RXEN_SHIFT)

/* IE */
#define SIFIVE_SERIAL_IE_OFFS			0x10
#define SIFIVE_SERIAL_IE_RXWM_SHIFT		1
#define SIFIVE_SERIAL_IE_RXWM_MASK		(1 << SIFIVE_SERIAL_IE_RXWM_SHIFT)
#define SIFIVE_SERIAL_IE_TXWM_SHIFT		0
#define SIFIVE_SERIAL_IE_TXWM_MASK		(1 << SIFIVE_SERIAL_IE_TXWM_SHIFT)

/* IP */
#define SIFIVE_SERIAL_IP_OFFS			0x14
#define SIFIVE_SERIAL_IP_RXWM_SHIFT		1
#define SIFIVE_SERIAL_IP_RXWM_MASK		(1 << SIFIVE_SERIAL_IP_RXWM_SHIFT)
#define SIFIVE_SERIAL_IP_TXWM_SHIFT		0
#define SIFIVE_SERIAL_IP_TXWM_MASK		(1 << SIFIVE_SERIAL_IP_TXWM_SHIFT)

/* DIV */
#define SIFIVE_SERIAL_DIV_OFFS			0x18
#define SIFIVE_SERIAL_DIV_DIV_SHIFT		0
#define SIFIVE_SERIAL_DIV_DIV_MASK		(0xffff << SIFIVE_SERIAL_IP_DIV_SHIFT)

/*
 * Config macros
 */

/*
 * SIFIVE_SERIAL_MAX_PORTS: maximum number of UARTs on a device that can
 *                          host a serial console
 */
#define SIFIVE_SERIAL_MAX_PORTS			8

/*
 * SIFIVE_DEFAULT_BAUD_RATE: default baud rate that the driver should
 *                           configure itself to use
 */
#define SIFIVE_DEFAULT_BAUD_RATE		115200

/* SIFIVE_SERIAL_NAME: our driver's name that we pass to the operating system */
#define SIFIVE_SERIAL_NAME			"sifive-serial"

/* SIFIVE_TTY_PREFIX: tty name prefix for SiFive serial ports */
#define SIFIVE_TTY_PREFIX			"ttySIF"

/* SIFIVE_TX_FIFO_DEPTH: depth of the TX FIFO (in bytes) */
#define SIFIVE_TX_FIFO_DEPTH			8

/* SIFIVE_RX_FIFO_DEPTH: depth of the TX FIFO (in bytes) */
#define SIFIVE_RX_FIFO_DEPTH			8

#if (SIFIVE_TX_FIFO_DEPTH != SIFIVE_RX_FIFO_DEPTH)
#error Driver does not support configurations with different TX, RX FIFO sizes
#endif

/*
 *
 */

/**
 * struct sifive_serial_port - driver-specific data extension to struct uart_port
 * @port: struct uart_port embedded in this struct
 * @dev: struct device *
 * @ier: shadowed copy of the interrupt enable register
 * @baud_rate: UART serial line rate (e.g., 115200 baud)
 * @clk: reference to this device's clock
 * @clk_notifier: clock rate change notifier for upstream clock changes
 *
 * Configuration data specific to this SiFive UART.
 */
struct sifive_serial_port {
	struct uart_port	port;
	struct device		*dev;
	unsigned char		ier;
	unsigned long		baud_rate;
	struct clk		*clk;
	struct notifier_block	clk_notifier;
};

/*
 * Structure container-of macros
 */

#define port_to_sifive_serial_port(p) (container_of((p), \
						    struct sifive_serial_port, \
						    port))

#define notifier_to_sifive_serial_port(nb) (container_of((nb), \
							 struct sifive_serial_port, \
							 clk_notifier))

/*
 * Forward declarations
 */
static void sifive_serial_stop_tx(struct uart_port *port);

/*
 * Internal functions
 */

/**
 * __ssp_early_writel() - write to a SiFive serial port register (early)
 * @port: pointer to a struct uart_port record
 * @offs: register address offset from the IP block base address
 * @v: value to write to the register
 *
 * Given a pointer @port to a struct uart_port record, write the value
 * @v to the IP block register address offset @offs.  This function is
 * intended for early console use.
 *
 * Context: Intended to be used only by the earlyconsole code.
 */
static void __ssp_early_writel(u32 v, u16 offs, struct uart_port *port)
{
	writel_relaxed(v, port->membase + offs);
}

/**
 * __ssp_early_readl() - read from a SiFive serial port register (early)
 * @port: pointer to a struct uart_port record
 * @offs: register address offset from the IP block base address
 *
 * Given a pointer @port to a struct uart_port record, read the
 * contents of the IP block register located at offset @offs from the
 * IP block base and return it.  This function is intended for early
 * console use.
 *
 * Context: Intended to be called only by the earlyconsole code or by
 *          __ssp_readl() or __ssp_writel() (in this driver)
 *
 * Returns: the register value read from the UART.
 */
static u32 __ssp_early_readl(struct uart_port *port, u16 offs)
{
	return readl_relaxed(port->membase + offs);
}

/**
 * __ssp_writel() - write to a SiFive serial port register
 * @v: value to write to the register
 * @offs: register address offset from the IP block base address
 * @ssp: pointer to a struct sifive_serial_port record
 *
 * Write the value @v to the IP block register located at offset @offs from the
 * IP block base, given a pointer @ssp to a struct sifive_serial_port record.
 *
 * Context: Any context.
 */
static void __ssp_writel(u32 v, u16 offs, struct sifive_serial_port *ssp)
{
	__ssp_early_writel(v, offs, &ssp->port);
}

/**
 * __ssp_readl() - read from a SiFive serial port register
 * @ssp: pointer to a struct sifive_serial_port record
 * @offs: register address offset from the IP block base address
 *
 * Read the contents of the IP block register located at offset @offs from the
 * IP block base, given a pointer @ssp to a struct sifive_serial_port record.
 *
 * Context: Any context.
 *
 * Returns: the value of the UART register
 */
static u32 __ssp_readl(struct sifive_serial_port *ssp, u16 offs)
{
	return __ssp_early_readl(&ssp->port, offs);
}

/**
 * sifive_serial_is_txfifo_full() - is the TXFIFO full?
 * @ssp: pointer to a struct sifive_serial_port
 *
 * Read the transmit FIFO "full" bit, returning a non-zero value if the
 * TX FIFO is full, or zero if space remains.  Intended to be used to prevent
 * writes to the TX FIFO when it's full.
 *
 * Returns: SIFIVE_SERIAL_TXDATA_FULL_MASK (non-zero) if the transmit FIFO
 * is full, or 0 if space remains.
 */
static int sifive_serial_is_txfifo_full(struct sifive_serial_port *ssp)
{
	return __ssp_readl(ssp, SIFIVE_SERIAL_TXDATA_OFFS) &
		SIFIVE_SERIAL_TXDATA_FULL_MASK;
}

/**
 * __ssp_transmit_char() - enqueue a byte to transmit onto the TX FIFO
 * @ssp: pointer to a struct sifive_serial_port
 * @ch: character to transmit
 *
 * Enqueue a byte @ch onto the transmit FIFO, given a pointer @ssp to the
 * struct sifive_serial_port * to transmit on.  Caller should first check to
 * ensure that the TXFIFO has space; see sifive_serial_is_txfifo_full().
 *
 * Context: Any context.
 */
static void __ssp_transmit_char(struct sifive_serial_port *ssp, int ch)
{
	__ssp_writel(ch, SIFIVE_SERIAL_TXDATA_OFFS, ssp);
}

/**
 * __ssp_transmit_chars() - enqueue multiple bytes onto the TX FIFO
 * @ssp: pointer to a struct sifive_serial_port
 *
 * Transfer up to a TX FIFO size's worth of characters from the Linux serial
 * transmit buffer to the SiFive UART TX FIFO.
 *
 * Context: Any context.  Expects @ssp->port.lock to be held by caller.
 */
static void __ssp_transmit_chars(struct sifive_serial_port *ssp)
{
	struct circ_buf *xmit = &ssp->port.state->xmit;
	int count;

	if (ssp->port.x_char) {
		__ssp_transmit_char(ssp, ssp->port.x_char);
		ssp->port.icount.tx++;
		ssp->port.x_char = 0;
		return;
	}
	if (uart_circ_empty(xmit) || uart_tx_stopped(&ssp->port)) {
		sifive_serial_stop_tx(&ssp->port);
		return;
	}
	count = SIFIVE_TX_FIFO_DEPTH;
	do {
		__ssp_transmit_char(ssp, xmit->buf[xmit->tail]);
		xmit->tail = (xmit->tail + 1) & (UART_XMIT_SIZE - 1);
		ssp->port.icount.tx++;
		if (uart_circ_empty(xmit))
			break;
	} while (--count > 0);

	if (uart_circ_chars_pending(xmit) < WAKEUP_CHARS)
		uart_write_wakeup(&ssp->port);

	if (uart_circ_empty(xmit))
		sifive_serial_stop_tx(&ssp->port);
}

/**
 * __ssp_enable_txwm() - enable transmit watermark interrupts
 * @ssp: pointer to a struct sifive_serial_port
 *
 * Enable interrupt generation when the transmit FIFO watermark is reached
 * on the SiFive UART referred to by @ssp.
 */
static void __ssp_enable_txwm(struct sifive_serial_port *ssp)
{
	if (ssp->ier & SIFIVE_SERIAL_IE_TXWM_MASK)
		return;

	ssp->ier |= SIFIVE_SERIAL_IE_TXWM_MASK;
	__ssp_writel(ssp->ier, SIFIVE_SERIAL_IE_OFFS, ssp);
}

/**
 * __ssp_enable_rxwm() - enable receive watermark interrupts
 * @ssp: pointer to a struct sifive_serial_port
 *
 * Enable interrupt generation when the receive FIFO watermark is reached
 * on the SiFive UART referred to by @ssp.
 */
static void __ssp_enable_rxwm(struct sifive_serial_port *ssp)
{
	if (ssp->ier & SIFIVE_SERIAL_IE_RXWM_MASK)
		return;

	ssp->ier |= SIFIVE_SERIAL_IE_RXWM_MASK;
	__ssp_writel(ssp->ier, SIFIVE_SERIAL_IE_OFFS, ssp);
}

/**
 * __ssp_disable_txwm() - disable transmit watermark interrupts
 * @ssp: pointer to a struct sifive_serial_port
 *
 * Disable interrupt generation when the transmit FIFO watermark is reached
 * on the UART referred to by @ssp.
 */
static void __ssp_disable_txwm(struct sifive_serial_port *ssp)
{
	if (!(ssp->ier & SIFIVE_SERIAL_IE_TXWM_MASK))
		return;

	ssp->ier &= ~SIFIVE_SERIAL_IE_TXWM_MASK;
	__ssp_writel(ssp->ier, SIFIVE_SERIAL_IE_OFFS, ssp);
}

/**
 * __ssp_disable_rxwm() - disable receive watermark interrupts
 * @ssp: pointer to a struct sifive_serial_port
 *
 * Disable interrupt generation when the receive FIFO watermark is reached
 * on the UART referred to by @ssp.
 */
static void __ssp_disable_rxwm(struct sifive_serial_port *ssp)
{
	if (!(ssp->ier & SIFIVE_SERIAL_IE_RXWM_MASK))
		return;

	ssp->ier &= ~SIFIVE_SERIAL_IE_RXWM_MASK;
	__ssp_writel(ssp->ier, SIFIVE_SERIAL_IE_OFFS, ssp);
}

/**
 * __ssp_receive_char() - receive a byte from the UART
 * @ssp: pointer to a struct sifive_serial_port
 * @is_empty: char pointer to return whether the RX FIFO is empty
 *
 * Try to read a byte from the SiFive UART RX FIFO, referenced by
 * @ssp, and to return it.  Also returns the RX FIFO empty bit in
 * the char pointed to by @ch.  The caller must pass the byte back to the
 * Linux serial layer if needed.
 *
 * Returns: the byte read from the UART RX FIFO.
 */
static char __ssp_receive_char(struct sifive_serial_port *ssp, char *is_empty)
{
	u32 v;
	u8 ch;

	v = __ssp_readl(ssp, SIFIVE_SERIAL_RXDATA_OFFS);

	if (!is_empty)
		WARN_ON(1);
	else
		*is_empty = (v & SIFIVE_SERIAL_RXDATA_EMPTY_MASK) >>
			SIFIVE_SERIAL_RXDATA_EMPTY_SHIFT;

	ch = (v & SIFIVE_SERIAL_RXDATA_DATA_MASK) >>
		SIFIVE_SERIAL_RXDATA_DATA_SHIFT;

	return ch;
}

/**
 * __ssp_receive_chars() - receive multiple bytes from the UART
 * @ssp: pointer to a struct sifive_serial_port
 *
 * Receive up to an RX FIFO's worth of bytes from the SiFive UART referred
 * to by @ssp and pass them up to the Linux serial layer.
 *
 * Context: Expects ssp->port.lock to be held by caller.
 */
static void __ssp_receive_chars(struct sifive_serial_port *ssp)
{
	unsigned char ch;
	char is_empty;
	int c;

	for (c = SIFIVE_RX_FIFO_DEPTH; c > 0; --c) {
		ch = __ssp_receive_char(ssp, &is_empty);
		if (is_empty)
			break;

		ssp->port.icount.rx++;
		uart_insert_char(&ssp->port, 0, 0, ch, TTY_NORMAL);
	}

	tty_flip_buffer_push(&ssp->port.state->port);
}

/**
 * __ssp_update_div() - calculate the divisor setting by the line rate
 * @ssp: pointer to a struct sifive_serial_port
 *
 * Calculate the appropriate value of the clock divisor for the UART
 * and target line rate referred to by @ssp and write it into the
 * hardware.
 */
static void __ssp_update_div(struct sifive_serial_port *ssp)
{
	u16 div;

	div = DIV_ROUND_UP(ssp->port.uartclk, ssp->baud_rate) - 1;

	__ssp_writel(div, SIFIVE_SERIAL_DIV_OFFS, ssp);
}

/**
 * __ssp_update_baud_rate() - set the UART "baud rate"
 * @ssp: pointer to a struct sifive_serial_port
 * @rate: new target bit rate
 *
 * Calculate the UART divisor value for the target bit rate @rate for the
 * SiFive UART described by @ssp and program it into the UART.  There may
 * be some error between the target bit rate and the actual bit rate implemented
 * by the UART due to clock ratio granularity.
 */
static void __ssp_update_baud_rate(struct sifive_serial_port *ssp,
				   unsigned int rate)
{
	if (ssp->baud_rate == rate)
		return;

	ssp->baud_rate = rate;
	__ssp_update_div(ssp);
}

/**
 * __ssp_set_stop_bits() - set the number of stop bits
 * @ssp: pointer to a struct sifive_serial_port
 * @nstop: 1 or 2 (stop bits)
 *
 * Program the SiFive UART referred to by @ssp to use @nstop stop bits.
 */
static void __ssp_set_stop_bits(struct sifive_serial_port *ssp, char nstop)
{
	u32 v;

	if (nstop < 1 || nstop > 2) {
		WARN_ON(1);
		return;
	}

	v = __ssp_readl(ssp, SIFIVE_SERIAL_TXCTRL_OFFS);
	v &= ~SIFIVE_SERIAL_TXCTRL_NSTOP_MASK;
	v |= (nstop - 1) << SIFIVE_SERIAL_TXCTRL_NSTOP_SHIFT;
	__ssp_writel(v, SIFIVE_SERIAL_TXCTRL_OFFS, ssp);
}

/**
 * __ssp_wait_for_xmitr() - wait for an empty slot on the TX FIFO
 * @ssp: pointer to a struct sifive_serial_port
 *
 * Delay while the UART TX FIFO referred to by @ssp is marked as full.
 *
 * Context: Any context.
 */
static void __maybe_unused __ssp_wait_for_xmitr(struct sifive_serial_port *ssp)
{
	while (sifive_serial_is_txfifo_full(ssp))
		udelay(1); /* XXX Could probably be more intelligent here */
}

/*
 * Linux serial API functions
 */

static void sifive_serial_stop_tx(struct uart_port *port)
{
	struct sifive_serial_port *ssp = port_to_sifive_serial_port(port);

	__ssp_disable_txwm(ssp);
}

static void sifive_serial_stop_rx(struct uart_port *port)
{
	struct sifive_serial_port *ssp = port_to_sifive_serial_port(port);

	__ssp_disable_rxwm(ssp);
}

static void sifive_serial_start_tx(struct uart_port *port)
{
	struct sifive_serial_port *ssp = port_to_sifive_serial_port(port);

	__ssp_enable_txwm(ssp);
}

static irqreturn_t sifive_serial_irq(int irq, void *dev_id)
{
	struct sifive_serial_port *ssp = dev_id;
	u32 ip;

	spin_lock(&ssp->port.lock);

	ip = __ssp_readl(ssp, SIFIVE_SERIAL_IP_OFFS);
	if (!ip) {
		spin_unlock(&ssp->port.lock);
		return IRQ_NONE;
	}

	if (ip & SIFIVE_SERIAL_IP_RXWM_MASK)
		__ssp_receive_chars(ssp);
	if (ip & SIFIVE_SERIAL_IP_TXWM_MASK)
		__ssp_transmit_chars(ssp);

	spin_unlock(&ssp->port.lock);

	return IRQ_HANDLED;
}

static unsigned int sifive_serial_tx_empty(struct uart_port *port)
{
	return TIOCSER_TEMT;
}

static unsigned int sifive_serial_get_mctrl(struct uart_port *port)
{
	return TIOCM_CAR | TIOCM_CTS | TIOCM_DSR;
}

static void sifive_serial_set_mctrl(struct uart_port *port, unsigned int mctrl)
{
	/* IP block does not support these signals */
}

static void sifive_serial_break_ctl(struct uart_port *port, int break_state)
{
	/* IP block does not support sending a break */
}

static int sifive_serial_startup(struct uart_port *port)
{
	struct sifive_serial_port *ssp = port_to_sifive_serial_port(port);

	__ssp_enable_rxwm(ssp);

	return 0;
}

static void sifive_serial_shutdown(struct uart_port *port)
{
	struct sifive_serial_port *ssp = port_to_sifive_serial_port(port);

	__ssp_disable_rxwm(ssp);
	__ssp_disable_txwm(ssp);
}

/**
 * sifive_serial_clk_notifier() - clock post-rate-change notifier
 * @nb: pointer to the struct notifier_block, from the notifier code
 * @event: event mask from the notifier code
 * @data: pointer to the struct clk_notifier_data from the notifier code
 *
 * On the V0 SoC, the UART IP block is derived from the CPU clock source
 * after a synchronous divide-by-two divider, so any CPU clock rate change
 * requires the UART baud rate to be updated.  This presumably corrupts any
 * serial word currently being transmitted or received.  In order to avoid
 * corrupting the output data stream, we drain the transmit queue before
 * allowing the clock's rate to be changed.
 */
static int sifive_serial_clk_notifier(struct notifier_block *nb,
				      unsigned long event, void *data)
{
	struct clk_notifier_data *cnd = data;
	struct sifive_serial_port *ssp = notifier_to_sifive_serial_port(nb);

	if (event == PRE_RATE_CHANGE) {
		/*
		 * The TX watermark is always set to 1 by this driver, which
		 * means that the TX busy bit will lower when there are 0 bytes
		 * left in the TX queue -- in other words, when the TX FIFO is
		 * empty.
		 */
		__ssp_wait_for_xmitr(ssp);
		/*
		 * On the cycle the TX FIFO goes empty there is still a full
		 * UART frame left to be transmitted in the shift register.
		 * The UART provides no way for software to directly determine
		 * when that last frame has been transmitted, so we just sleep
		 * here instead.  As we're not tracking the number of stop bits
		 * they're just worst cased here.  The rest of the serial
		 * framing parameters aren't configurable by software.
		 */
		udelay(DIV_ROUND_UP(12 * 1000 * 1000, ssp->baud_rate));
	}

	if (event == POST_RATE_CHANGE && ssp->port.uartclk != cnd->new_rate) {
		ssp->port.uartclk = cnd->new_rate;
		__ssp_update_div(ssp);
	}

	return NOTIFY_OK;
}

static void sifive_serial_set_termios(struct uart_port *port,
				      struct ktermios *termios,
				      struct ktermios *old)
{
	struct sifive_serial_port *ssp = port_to_sifive_serial_port(port);
	unsigned long flags;
	u32 v, old_v;
	int rate;
	char nstop;

	if ((termios->c_cflag & CSIZE) != CS8) {
		dev_err_once(ssp->port.dev, "only 8-bit words supported\n");
		termios->c_cflag &= ~CSIZE;
		termios->c_cflag |= CS8;
	}
	if (termios->c_iflag & (INPCK | PARMRK))
		dev_err_once(ssp->port.dev, "parity checking not supported\n");
	if (termios->c_iflag & BRKINT)
		dev_err_once(ssp->port.dev, "BREAK detection not supported\n");
	termios->c_iflag &= ~(INPCK|PARMRK|BRKINT);

	/* Set number of stop bits */
	nstop = (termios->c_cflag & CSTOPB) ? 2 : 1;
	__ssp_set_stop_bits(ssp, nstop);

	/* Set line rate */
	rate = uart_get_baud_rate(port, termios, old, 0,
				  ssp->port.uartclk / 16);
	__ssp_update_baud_rate(ssp, rate);

	spin_lock_irqsave(&ssp->port.lock, flags);

	/* Update the per-port timeout */
	uart_update_timeout(port, termios->c_cflag, rate);

	ssp->port.read_status_mask = 0;

	/* Ignore all characters if CREAD is not set */
	v = __ssp_readl(ssp, SIFIVE_SERIAL_RXCTRL_OFFS);
	old_v = v;
	if ((termios->c_cflag & CREAD) == 0)
		v &= SIFIVE_SERIAL_RXCTRL_RXEN_MASK;
	else
		v |= SIFIVE_SERIAL_RXCTRL_RXEN_MASK;
	if (v != old_v)
		__ssp_writel(v, SIFIVE_SERIAL_RXCTRL_OFFS, ssp);

	spin_unlock_irqrestore(&ssp->port.lock, flags);
}

static void sifive_serial_release_port(struct uart_port *port)
{
}

static int sifive_serial_request_port(struct uart_port *port)
{
	return 0;
}

static void sifive_serial_config_port(struct uart_port *port, int flags)
{
	struct sifive_serial_port *ssp = port_to_sifive_serial_port(port);

	ssp->port.type = PORT_SIFIVE_V0;
}

static int sifive_serial_verify_port(struct uart_port *port,
				     struct serial_struct *ser)
{
	return -EINVAL;
}

static const char *sifive_serial_type(struct uart_port *port)
{
	return port->type == PORT_SIFIVE_V0 ? "SiFive UART v0" : NULL;
}

#ifdef CONFIG_CONSOLE_POLL
static int sifive_serial_poll_get_char(struct uart_port *port)
{
	struct sifive_serial_port *ssp = port_to_sifive_serial_port(port);
	char is_empty, ch;

	ch = __ssp_receive_char(ssp, &is_empty);
	if (is_empty)
		return NO_POLL_CHAR;

	return ch;
}

static void sifive_serial_poll_put_char(struct uart_port *port,
					unsigned char c)
{
	struct sifive_serial_port *ssp = port_to_sifive_serial_port(port);

	__ssp_wait_for_xmitr(ssp);
	__ssp_transmit_char(ssp, c);
}
#endif /* CONFIG_CONSOLE_POLL */

/*
 * Early console support
 */

#ifdef CONFIG_SERIAL_EARLYCON
static void early_sifive_serial_putc(struct uart_port *port, unsigned char c)
{
	while (__ssp_early_readl(port, SIFIVE_SERIAL_TXDATA_OFFS) &
	       SIFIVE_SERIAL_TXDATA_FULL_MASK)
		cpu_relax();

	__ssp_early_writel(c, SIFIVE_SERIAL_TXDATA_OFFS, port);
}

static void early_sifive_serial_write(struct console *con, const char *s,
				      unsigned int n)
{
	struct earlycon_device *dev = con->data;
	struct uart_port *port = &dev->port;

	uart_console_write(port, s, n, early_sifive_serial_putc);
}

static int __init early_sifive_serial_setup(struct earlycon_device *dev,
					    const char *options)
{
	struct uart_port *port = &dev->port;

	if (!port->membase)
		return -ENODEV;

	dev->con->write = early_sifive_serial_write;

	return 0;
}

OF_EARLYCON_DECLARE(sifive, "sifive,uart0", early_sifive_serial_setup);
OF_EARLYCON_DECLARE(sifive, "sifive,fu540-c000-uart0",
		    early_sifive_serial_setup);
#endif /* CONFIG_SERIAL_EARLYCON */

/*
 * Linux console interface
 */

#ifdef CONFIG_SERIAL_SIFIVE_CONSOLE

static struct sifive_serial_port *sifive_serial_console_ports[SIFIVE_SERIAL_MAX_PORTS];

static void sifive_serial_console_putchar(struct uart_port *port, unsigned char ch)
{
	struct sifive_serial_port *ssp = port_to_sifive_serial_port(port);

	__ssp_wait_for_xmitr(ssp);
	__ssp_transmit_char(ssp, ch);
}

static void sifive_serial_console_write(struct console *co, const char *s,
					unsigned int count)
{
	struct sifive_serial_port *ssp = sifive_serial_console_ports[co->index];
	unsigned long flags;
	unsigned int ier;
	int locked = 1;

	if (!ssp)
		return;

	local_irq_save(flags);
	if (ssp->port.sysrq)
		locked = 0;
	else if (oops_in_progress)
		locked = spin_trylock(&ssp->port.lock);
	else
		spin_lock(&ssp->port.lock);

	ier = __ssp_readl(ssp, SIFIVE_SERIAL_IE_OFFS);
	__ssp_writel(0, SIFIVE_SERIAL_IE_OFFS, ssp);

	uart_console_write(&ssp->port, s, count, sifive_serial_console_putchar);

	__ssp_writel(ier, SIFIVE_SERIAL_IE_OFFS, ssp);

	if (locked)
		spin_unlock(&ssp->port.lock);
	local_irq_restore(flags);
}

static int __init sifive_serial_console_setup(struct console *co, char *options)
{
	struct sifive_serial_port *ssp;
	int baud = SIFIVE_DEFAULT_BAUD_RATE;
	int bits = 8;
	int parity = 'n';
	int flow = 'n';

	if (co->index < 0 || co->index >= SIFIVE_SERIAL_MAX_PORTS)
		return -ENODEV;

	ssp = sifive_serial_console_ports[co->index];
	if (!ssp)
		return -ENODEV;

	if (options)
		uart_parse_options(options, &baud, &parity, &bits, &flow);

	return uart_set_options(&ssp->port, co, baud, parity, bits, flow);
}

static struct uart_driver sifive_serial_uart_driver;

static struct console sifive_serial_console = {
	.name		= SIFIVE_TTY_PREFIX,
	.write		= sifive_serial_console_write,
	.device		= uart_console_device,
	.setup		= sifive_serial_console_setup,
	.flags		= CON_PRINTBUFFER,
	.index		= -1,
	.data		= &sifive_serial_uart_driver,
};

static int __init sifive_console_init(void)
{
	register_console(&sifive_serial_console);
	return 0;
}

console_initcall(sifive_console_init);

static void __ssp_add_console_port(struct sifive_serial_port *ssp)
{
	sifive_serial_console_ports[ssp->port.line] = ssp;
}

static void __ssp_remove_console_port(struct sifive_serial_port *ssp)
{
	sifive_serial_console_ports[ssp->port.line] = NULL;
}

#define SIFIVE_SERIAL_CONSOLE	(&sifive_serial_console)

#else

#define SIFIVE_SERIAL_CONSOLE	NULL

static void __ssp_add_console_port(struct sifive_serial_port *ssp)
{}
static void __ssp_remove_console_port(struct sifive_serial_port *ssp)
{}

#endif

static const struct uart_ops sifive_serial_uops = {
	.tx_empty	= sifive_serial_tx_empty,
	.set_mctrl	= sifive_serial_set_mctrl,
	.get_mctrl	= sifive_serial_get_mctrl,
	.stop_tx	= sifive_serial_stop_tx,
	.start_tx	= sifive_serial_start_tx,
	.stop_rx	= sifive_serial_stop_rx,
	.break_ctl	= sifive_serial_break_ctl,
	.startup	= sifive_serial_startup,
	.shutdown	= sifive_serial_shutdown,
	.set_termios	= sifive_serial_set_termios,
	.type		= sifive_serial_type,
	.release_port	= sifive_serial_release_port,
	.request_port	= sifive_serial_request_port,
	.config_port	= sifive_serial_config_port,
	.verify_port	= sifive_serial_verify_port,
#ifdef CONFIG_CONSOLE_POLL
	.poll_get_char	= sifive_serial_poll_get_char,
	.poll_put_char	= sifive_serial_poll_put_char,
#endif
};

static struct uart_driver sifive_serial_uart_driver = {
	.owner		= THIS_MODULE,
	.driver_name	= SIFIVE_SERIAL_NAME,
	.dev_name	= SIFIVE_TTY_PREFIX,
	.nr		= SIFIVE_SERIAL_MAX_PORTS,
	.cons		= SIFIVE_SERIAL_CONSOLE,
};

static int sifive_serial_probe(struct platform_device *pdev)
{
	struct sifive_serial_port *ssp;
	struct resource *mem;
	struct clk *clk;
	void __iomem *base;
	int irq, id, r;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return -EPROBE_DEFER;

	mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	base = devm_ioremap_resource(&pdev->dev, mem);
	if (IS_ERR(base)) {
		dev_err(&pdev->dev, "could not acquire device memory\n");
		return PTR_ERR(base);
	}

	clk = devm_clk_get_enabled(&pdev->dev, NULL);
	if (IS_ERR(clk)) {
		dev_err(&pdev->dev, "unable to find controller clock\n");
		return PTR_ERR(clk);
	}

	id = of_alias_get_id(pdev->dev.of_node, "serial");
	if (id < 0) {
		dev_err(&pdev->dev, "missing aliases entry\n");
		return id;
	}

#ifdef CONFIG_SERIAL_SIFIVE_CONSOLE
	if (id > SIFIVE_SERIAL_MAX_PORTS) {
		dev_err(&pdev->dev, "too many UARTs (%d)\n", id);
		return -EINVAL;
	}
#endif

	ssp = devm_kzalloc(&pdev->dev, sizeof(*ssp), GFP_KERNEL);
	if (!ssp)
		return -ENOMEM;

	ssp->port.dev = &pdev->dev;
	ssp->port.type = PORT_SIFIVE_V0;
	ssp->port.iotype = UPIO_MEM;
	ssp->port.irq = irq;
	ssp->port.fifosize = SIFIVE_TX_FIFO_DEPTH;
	ssp->port.ops = &sifive_serial_uops;
	ssp->port.line = id;
	ssp->port.mapbase = mem->start;
	ssp->port.membase = base;
	ssp->dev = &pdev->dev;
	ssp->clk = clk;
	ssp->clk_notifier.notifier_call = sifive_serial_clk_notifier;

	r = clk_notifier_register(ssp->clk, &ssp->clk_notifier);
	if (r) {
		dev_err(&pdev->dev, "could not register clock notifier: %d\n",
			r);
		goto probe_out1;
	}

	/* Set up clock divider */
	ssp->port.uartclk = clk_get_rate(ssp->clk);
	ssp->baud_rate = SIFIVE_DEFAULT_BAUD_RATE;
	__ssp_update_div(ssp);

	platform_set_drvdata(pdev, ssp);

	/* Enable transmits and set the watermark level to 1 */
	__ssp_writel((1 << SIFIVE_SERIAL_TXCTRL_TXCNT_SHIFT) |
		     SIFIVE_SERIAL_TXCTRL_TXEN_MASK,
		     SIFIVE_SERIAL_TXCTRL_OFFS, ssp);

	/* Enable receives and set the watermark level to 0 */
	__ssp_writel((0 << SIFIVE_SERIAL_RXCTRL_RXCNT_SHIFT) |
		     SIFIVE_SERIAL_RXCTRL_RXEN_MASK,
		     SIFIVE_SERIAL_RXCTRL_OFFS, ssp);

	r = request_irq(ssp->port.irq, sifive_serial_irq, ssp->port.irqflags,
			dev_name(&pdev->dev), ssp);
	if (r) {
		dev_err(&pdev->dev, "could not attach interrupt: %d\n", r);
		goto probe_out2;
	}

	__ssp_add_console_port(ssp);

	r = uart_add_one_port(&sifive_serial_uart_driver, &ssp->port);
	if (r != 0) {
		dev_err(&pdev->dev, "could not add uart: %d\n", r);
		goto probe_out3;
	}

	return 0;

probe_out3:
	__ssp_remove_console_port(ssp);
	free_irq(ssp->port.irq, ssp);
probe_out2:
	clk_notifier_unregister(ssp->clk, &ssp->clk_notifier);
probe_out1:
	return r;
}

static int sifive_serial_remove(struct platform_device *dev)
{
	struct sifive_serial_port *ssp = platform_get_drvdata(dev);

	__ssp_remove_console_port(ssp);
	uart_remove_one_port(&sifive_serial_uart_driver, &ssp->port);
	free_irq(ssp->port.irq, ssp);
	clk_notifier_unregister(ssp->clk, &ssp->clk_notifier);

	return 0;
}

static const struct of_device_id sifive_serial_of_match[] = {
	{ .compatible = "sifive,fu540-c000-uart0" },
	{ .compatible = "sifive,uart0" },
	{},
};
MODULE_DEVICE_TABLE(of, sifive_serial_of_match);

static struct platform_driver sifive_serial_platform_driver = {
	.probe		= sifive_serial_probe,
	.remove		= sifive_serial_remove,
	.driver		= {
		.name	= SIFIVE_SERIAL_NAME,
		.of_match_table = of_match_ptr(sifive_serial_of_match),
	},
};

static int __init sifive_serial_init(void)
{
	int r;

	r = uart_register_driver(&sifive_serial_uart_driver);
	if (r)
		goto init_out1;

	r = platform_driver_register(&sifive_serial_platform_driver);
	if (r)
		goto init_out2;

	return 0;

init_out2:
	uart_unregister_driver(&sifive_serial_uart_driver);
init_out1:
	return r;
}

static void __exit sifive_serial_exit(void)
{
	platform_driver_unregister(&sifive_serial_platform_driver);
	uart_unregister_driver(&sifive_serial_uart_driver);
}

module_init(sifive_serial_init);
module_exit(sifive_serial_exit);

MODULE_DESCRIPTION("SiFive UART serial driver");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Paul Walmsley <paul@pwsan.com>");
