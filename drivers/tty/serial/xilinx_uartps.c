/*
 * Cadence UART driver (found in Xilinx Zynq)
 *
 * 2011 - 2014 (C) Xilinx Inc.
 *
 * This program is free software; you can redistribute it
 * and/or modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation;
 * either version 2 of the License, or (at your option) any
 * later version.
 *
 * This driver has originally been pushed by Xilinx using a Zynq-branding. This
 * still shows in the naming of this file, the kconfig symbols and some symbols
 * in the code.
 */

#if defined(CONFIG_SERIAL_XILINX_PS_UART_CONSOLE) && defined(CONFIG_MAGIC_SYSRQ)
#define SUPPORT_SYSRQ
#endif

#include <linux/platform_device.h>
#include <linux/serial.h>
#include <linux/console.h>
#include <linux/serial_core.h>
#include <linux/slab.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/clk.h>
#include <linux/irq.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/module.h>

#define CDNS_UART_TTY_NAME	"ttyPS"
#define CDNS_UART_NAME		"xuartps"
#define CDNS_UART_MAJOR		0	/* use dynamic node allocation */
#define CDNS_UART_MINOR		0	/* works best with devtmpfs */
#define CDNS_UART_NR_PORTS	2
#define CDNS_UART_FIFO_SIZE	64	/* FIFO size */
#define CDNS_UART_REGISTER_SPACE	0x1000

/* Rx Trigger level */
static int rx_trigger_level = 56;
module_param(rx_trigger_level, uint, S_IRUGO);
MODULE_PARM_DESC(rx_trigger_level, "Rx trigger level, 1-63 bytes");

/* Rx Timeout */
static int rx_timeout = 10;
module_param(rx_timeout, uint, S_IRUGO);
MODULE_PARM_DESC(rx_timeout, "Rx timeout, 1-255");

/* Register offsets for the UART. */
#define CDNS_UART_CR		0x00  /* Control Register */
#define CDNS_UART_MR		0x04  /* Mode Register */
#define CDNS_UART_IER		0x08  /* Interrupt Enable */
#define CDNS_UART_IDR		0x0C  /* Interrupt Disable */
#define CDNS_UART_IMR		0x10  /* Interrupt Mask */
#define CDNS_UART_ISR		0x14  /* Interrupt Status */
#define CDNS_UART_BAUDGEN	0x18  /* Baud Rate Generator */
#define CDNS_UART_RXTOUT		0x1C  /* RX Timeout */
#define CDNS_UART_RXWM		0x20  /* RX FIFO Trigger Level */
#define CDNS_UART_MODEMCR	0x24  /* Modem Control */
#define CDNS_UART_MODEMSR	0x28  /* Modem Status */
#define CDNS_UART_SR		0x2C  /* Channel Status */
#define CDNS_UART_FIFO		0x30  /* FIFO */
#define CDNS_UART_BAUDDIV	0x34  /* Baud Rate Divider */
#define CDNS_UART_FLOWDEL	0x38  /* Flow Delay */
#define CDNS_UART_IRRX_PWIDTH	0x3C  /* IR Min Received Pulse Width */
#define CDNS_UART_IRTX_PWIDTH	0x40  /* IR Transmitted pulse Width */
#define CDNS_UART_TXWM		0x44  /* TX FIFO Trigger Level */

/* Control Register Bit Definitions */
#define CDNS_UART_CR_STOPBRK	0x00000100  /* Stop TX break */
#define CDNS_UART_CR_STARTBRK	0x00000080  /* Set TX break */
#define CDNS_UART_CR_TX_DIS	0x00000020  /* TX disabled. */
#define CDNS_UART_CR_TX_EN	0x00000010  /* TX enabled */
#define CDNS_UART_CR_RX_DIS	0x00000008  /* RX disabled. */
#define CDNS_UART_CR_RX_EN	0x00000004  /* RX enabled */
#define CDNS_UART_CR_TXRST	0x00000002  /* TX logic reset */
#define CDNS_UART_CR_RXRST	0x00000001  /* RX logic reset */
#define CDNS_UART_CR_RST_TO	0x00000040  /* Restart Timeout Counter */

/*
 * Mode Register:
 * The mode register (MR) defines the mode of transfer as well as the data
 * format. If this register is modified during transmission or reception,
 * data validity cannot be guaranteed.
 */
#define CDNS_UART_MR_CLKSEL		0x00000001  /* Pre-scalar selection */
#define CDNS_UART_MR_CHMODE_L_LOOP	0x00000200  /* Local loop back mode */
#define CDNS_UART_MR_CHMODE_NORM	0x00000000  /* Normal mode */

#define CDNS_UART_MR_STOPMODE_2_BIT	0x00000080  /* 2 stop bits */
#define CDNS_UART_MR_STOPMODE_1_BIT	0x00000000  /* 1 stop bit */

#define CDNS_UART_MR_PARITY_NONE	0x00000020  /* No parity mode */
#define CDNS_UART_MR_PARITY_MARK	0x00000018  /* Mark parity mode */
#define CDNS_UART_MR_PARITY_SPACE	0x00000010  /* Space parity mode */
#define CDNS_UART_MR_PARITY_ODD		0x00000008  /* Odd parity mode */
#define CDNS_UART_MR_PARITY_EVEN	0x00000000  /* Even parity mode */

#define CDNS_UART_MR_CHARLEN_6_BIT	0x00000006  /* 6 bits data */
#define CDNS_UART_MR_CHARLEN_7_BIT	0x00000004  /* 7 bits data */
#define CDNS_UART_MR_CHARLEN_8_BIT	0x00000000  /* 8 bits data */

/*
 * Interrupt Registers:
 * Interrupt control logic uses the interrupt enable register (IER) and the
 * interrupt disable register (IDR) to set the value of the bits in the
 * interrupt mask register (IMR). The IMR determines whether to pass an
 * interrupt to the interrupt status register (ISR).
 * Writing a 1 to IER Enables an interrupt, writing a 1 to IDR disables an
 * interrupt. IMR and ISR are read only, and IER and IDR are write only.
 * Reading either IER or IDR returns 0x00.
 * All four registers have the same bit definitions.
 */
#define CDNS_UART_IXR_TOUT	0x00000100 /* RX Timeout error interrupt */
#define CDNS_UART_IXR_PARITY	0x00000080 /* Parity error interrupt */
#define CDNS_UART_IXR_FRAMING	0x00000040 /* Framing error interrupt */
#define CDNS_UART_IXR_OVERRUN	0x00000020 /* Overrun error interrupt */
#define CDNS_UART_IXR_TXFULL	0x00000010 /* TX FIFO Full interrupt */
#define CDNS_UART_IXR_TXEMPTY	0x00000008 /* TX FIFO empty interrupt */
#define CDNS_UART_ISR_RXEMPTY	0x00000002 /* RX FIFO empty interrupt */
#define CDNS_UART_IXR_RXTRIG	0x00000001 /* RX FIFO trigger interrupt */
#define CDNS_UART_IXR_RXFULL	0x00000004 /* RX FIFO full interrupt. */
#define CDNS_UART_IXR_RXEMPTY	0x00000002 /* RX FIFO empty interrupt. */
#define CDNS_UART_IXR_MASK	0x00001FFF /* Valid bit mask */

#define CDNS_UART_RX_IRQS	(CDNS_UART_IXR_PARITY | CDNS_UART_IXR_FRAMING | \
				 CDNS_UART_IXR_OVERRUN | CDNS_UART_IXR_RXTRIG | \
				 CDNS_UART_IXR_TOUT)

/* Goes in read_status_mask for break detection as the HW doesn't do it*/
#define CDNS_UART_IXR_BRK	0x80000000

/*
 * Modem Control register:
 * The read/write Modem Control register controls the interface with the modem
 * or data set, or a peripheral device emulating a modem.
 */
#define CDNS_UART_MODEMCR_FCM	0x00000020 /* Automatic flow control mode */
#define CDNS_UART_MODEMCR_RTS	0x00000002 /* Request to send output control */
#define CDNS_UART_MODEMCR_DTR	0x00000001 /* Data Terminal Ready */

/*
 * Channel Status Register:
 * The channel status register (CSR) is provided to enable the control logic
 * to monitor the status of bits in the channel interrupt status register,
 * even if these are masked out by the interrupt mask register.
 */
#define CDNS_UART_SR_RXEMPTY	0x00000002 /* RX FIFO empty */
#define CDNS_UART_SR_TXEMPTY	0x00000008 /* TX FIFO empty */
#define CDNS_UART_SR_TXFULL	0x00000010 /* TX FIFO full */
#define CDNS_UART_SR_RXTRIG	0x00000001 /* Rx Trigger */

/* baud dividers min/max values */
#define CDNS_UART_BDIV_MIN	4
#define CDNS_UART_BDIV_MAX	255
#define CDNS_UART_CD_MAX	65535

/**
 * struct cdns_uart - device data
 * @port:		Pointer to the UART port
 * @uartclk:		Reference clock
 * @pclk:		APB clock
 * @baud:		Current baud rate
 * @clk_rate_change_nb:	Notifier block for clock changes
 */
struct cdns_uart {
	struct uart_port	*port;
	struct clk		*uartclk;
	struct clk		*pclk;
	unsigned int		baud;
	struct notifier_block	clk_rate_change_nb;
};
#define to_cdns_uart(_nb) container_of(_nb, struct cdns_uart, \
		clk_rate_change_nb);

static void cdns_uart_handle_rx(struct uart_port *port, unsigned int isrstatus)
{
	/*
	 * There is no hardware break detection, so we interpret framing
	 * error with all-zeros data as a break sequence. Most of the time,
	 * there's another non-zero byte at the end of the sequence.
	 */
	if (isrstatus & CDNS_UART_IXR_FRAMING) {
		while (!(readl(port->membase + CDNS_UART_SR) &
					CDNS_UART_SR_RXEMPTY)) {
			if (!readl(port->membase + CDNS_UART_FIFO)) {
				port->read_status_mask |= CDNS_UART_IXR_BRK;
				isrstatus &= ~CDNS_UART_IXR_FRAMING;
			}
		}
		writel(CDNS_UART_IXR_FRAMING, port->membase + CDNS_UART_ISR);
	}

	/* drop byte with parity error if IGNPAR specified */
	if (isrstatus & port->ignore_status_mask & CDNS_UART_IXR_PARITY)
		isrstatus &= ~(CDNS_UART_IXR_RXTRIG | CDNS_UART_IXR_TOUT);

	isrstatus &= port->read_status_mask;
	isrstatus &= ~port->ignore_status_mask;

	if (!(isrstatus & (CDNS_UART_IXR_TOUT | CDNS_UART_IXR_RXTRIG)))
		return;

	while (!(readl(port->membase + CDNS_UART_SR) & CDNS_UART_SR_RXEMPTY)) {
		u32 data;
		char status = TTY_NORMAL;

		data = readl(port->membase + CDNS_UART_FIFO);

		/* Non-NULL byte after BREAK is garbage (99%) */
		if (data && (port->read_status_mask & CDNS_UART_IXR_BRK)) {
			port->read_status_mask &= ~CDNS_UART_IXR_BRK;
			port->icount.brk++;
			if (uart_handle_break(port))
				continue;
		}

		if (uart_handle_sysrq_char(port, data))
			continue;

		port->icount.rx++;

		if (isrstatus & CDNS_UART_IXR_PARITY) {
			port->icount.parity++;
			status = TTY_PARITY;
		} else if (isrstatus & CDNS_UART_IXR_FRAMING) {
			port->icount.frame++;
			status = TTY_FRAME;
		} else if (isrstatus & CDNS_UART_IXR_OVERRUN) {
			port->icount.overrun++;
		}

		uart_insert_char(port, isrstatus, CDNS_UART_IXR_OVERRUN,
				 data, status);
	}
	tty_flip_buffer_push(&port->state->port);
}

static void cdns_uart_handle_tx(struct uart_port *port)
{
	unsigned int numbytes;

	if (uart_circ_empty(&port->state->xmit)) {
		writel(CDNS_UART_IXR_TXEMPTY, port->membase + CDNS_UART_IDR);
		return;
	}

	numbytes = port->fifosize;
	while (numbytes && !uart_circ_empty(&port->state->xmit) &&
	       !(readl(port->membase + CDNS_UART_SR) & CDNS_UART_SR_TXFULL)) {
		/*
		 * Get the data from the UART circular buffer
		 * and write it to the cdns_uart's TX_FIFO
		 * register.
		 */
		writel(port->state->xmit.buf[port->state->xmit.tail],
			port->membase + CDNS_UART_FIFO);
		port->icount.tx++;

		/*
		 * Adjust the tail of the UART buffer and wrap
		 * the buffer if it reaches limit.
		 */
		port->state->xmit.tail =
			(port->state->xmit.tail + 1) & (UART_XMIT_SIZE - 1);

		numbytes--;
	}

	if (uart_circ_chars_pending(&port->state->xmit) < WAKEUP_CHARS)
		uart_write_wakeup(port);
}

/**
 * cdns_uart_isr - Interrupt handler
 * @irq: Irq number
 * @dev_id: Id of the port
 *
 * Return: IRQHANDLED
 */
static irqreturn_t cdns_uart_isr(int irq, void *dev_id)
{
	struct uart_port *port = (struct uart_port *)dev_id;
	unsigned long flags;
	unsigned int isrstatus;

	spin_lock_irqsave(&port->lock, flags);

	/* Read the interrupt status register to determine which
	 * interrupt(s) is/are active.
	 */
	isrstatus = readl(port->membase + CDNS_UART_ISR);

	if (isrstatus & CDNS_UART_RX_IRQS)
		cdns_uart_handle_rx(port, isrstatus);

	if ((isrstatus & CDNS_UART_IXR_TXEMPTY) == CDNS_UART_IXR_TXEMPTY)
		cdns_uart_handle_tx(port);

	writel(isrstatus, port->membase + CDNS_UART_ISR);

	/* be sure to release the lock and tty before leaving */
	spin_unlock_irqrestore(&port->lock, flags);

	return IRQ_HANDLED;
}

/**
 * cdns_uart_calc_baud_divs - Calculate baud rate divisors
 * @clk: UART module input clock
 * @baud: Desired baud rate
 * @rbdiv: BDIV value (return value)
 * @rcd: CD value (return value)
 * @div8: Value for clk_sel bit in mod (return value)
 * Return: baud rate, requested baud when possible, or actual baud when there
 *	was too much error, zero if no valid divisors are found.
 *
 * Formula to obtain baud rate is
 *	baud_tx/rx rate = clk/CD * (BDIV + 1)
 *	input_clk = (Uart User Defined Clock or Apb Clock)
 *		depends on UCLKEN in MR Reg
 *	clk = input_clk or input_clk/8;
 *		depends on CLKS in MR reg
 *	CD and BDIV depends on values in
 *			baud rate generate register
 *			baud rate clock divisor register
 */
static unsigned int cdns_uart_calc_baud_divs(unsigned int clk,
		unsigned int baud, u32 *rbdiv, u32 *rcd, int *div8)
{
	u32 cd, bdiv;
	unsigned int calc_baud;
	unsigned int bestbaud = 0;
	unsigned int bauderror;
	unsigned int besterror = ~0;

	if (baud < clk / ((CDNS_UART_BDIV_MAX + 1) * CDNS_UART_CD_MAX)) {
		*div8 = 1;
		clk /= 8;
	} else {
		*div8 = 0;
	}

	for (bdiv = CDNS_UART_BDIV_MIN; bdiv <= CDNS_UART_BDIV_MAX; bdiv++) {
		cd = DIV_ROUND_CLOSEST(clk, baud * (bdiv + 1));
		if (cd < 1 || cd > CDNS_UART_CD_MAX)
			continue;

		calc_baud = clk / (cd * (bdiv + 1));

		if (baud > calc_baud)
			bauderror = baud - calc_baud;
		else
			bauderror = calc_baud - baud;

		if (besterror > bauderror) {
			*rbdiv = bdiv;
			*rcd = cd;
			bestbaud = calc_baud;
			besterror = bauderror;
		}
	}
	/* use the values when percent error is acceptable */
	if (((besterror * 100) / baud) < 3)
		bestbaud = baud;

	return bestbaud;
}

/**
 * cdns_uart_set_baud_rate - Calculate and set the baud rate
 * @port: Handle to the uart port structure
 * @baud: Baud rate to set
 * Return: baud rate, requested baud when possible, or actual baud when there
 *	   was too much error, zero if no valid divisors are found.
 */
static unsigned int cdns_uart_set_baud_rate(struct uart_port *port,
		unsigned int baud)
{
	unsigned int calc_baud;
	u32 cd = 0, bdiv = 0;
	u32 mreg;
	int div8;
	struct cdns_uart *cdns_uart = port->private_data;

	calc_baud = cdns_uart_calc_baud_divs(port->uartclk, baud, &bdiv, &cd,
			&div8);

	/* Write new divisors to hardware */
	mreg = readl(port->membase + CDNS_UART_MR);
	if (div8)
		mreg |= CDNS_UART_MR_CLKSEL;
	else
		mreg &= ~CDNS_UART_MR_CLKSEL;
	writel(mreg, port->membase + CDNS_UART_MR);
	writel(cd, port->membase + CDNS_UART_BAUDGEN);
	writel(bdiv, port->membase + CDNS_UART_BAUDDIV);
	cdns_uart->baud = baud;

	return calc_baud;
}

#ifdef CONFIG_COMMON_CLK
/**
 * cdns_uart_clk_notitifer_cb - Clock notifier callback
 * @nb:		Notifier block
 * @event:	Notify event
 * @data:	Notifier data
 * Return:	NOTIFY_OK or NOTIFY_DONE on success, NOTIFY_BAD on error.
 */
static int cdns_uart_clk_notifier_cb(struct notifier_block *nb,
		unsigned long event, void *data)
{
	u32 ctrl_reg;
	struct uart_port *port;
	int locked = 0;
	struct clk_notifier_data *ndata = data;
	unsigned long flags = 0;
	struct cdns_uart *cdns_uart = to_cdns_uart(nb);

	port = cdns_uart->port;
	if (port->suspended)
		return NOTIFY_OK;

	switch (event) {
	case PRE_RATE_CHANGE:
	{
		u32 bdiv, cd;
		int div8;

		/*
		 * Find out if current baud-rate can be achieved with new clock
		 * frequency.
		 */
		if (!cdns_uart_calc_baud_divs(ndata->new_rate, cdns_uart->baud,
					&bdiv, &cd, &div8)) {
			dev_warn(port->dev, "clock rate change rejected\n");
			return NOTIFY_BAD;
		}

		spin_lock_irqsave(&cdns_uart->port->lock, flags);

		/* Disable the TX and RX to set baud rate */
		ctrl_reg = readl(port->membase + CDNS_UART_CR);
		ctrl_reg |= CDNS_UART_CR_TX_DIS | CDNS_UART_CR_RX_DIS;
		writel(ctrl_reg, port->membase + CDNS_UART_CR);

		spin_unlock_irqrestore(&cdns_uart->port->lock, flags);

		return NOTIFY_OK;
	}
	case POST_RATE_CHANGE:
		/*
		 * Set clk dividers to generate correct baud with new clock
		 * frequency.
		 */

		spin_lock_irqsave(&cdns_uart->port->lock, flags);

		locked = 1;
		port->uartclk = ndata->new_rate;

		cdns_uart->baud = cdns_uart_set_baud_rate(cdns_uart->port,
				cdns_uart->baud);
		/* fall through */
	case ABORT_RATE_CHANGE:
		if (!locked)
			spin_lock_irqsave(&cdns_uart->port->lock, flags);

		/* Set TX/RX Reset */
		ctrl_reg = readl(port->membase + CDNS_UART_CR);
		ctrl_reg |= CDNS_UART_CR_TXRST | CDNS_UART_CR_RXRST;
		writel(ctrl_reg, port->membase + CDNS_UART_CR);

		while (readl(port->membase + CDNS_UART_CR) &
				(CDNS_UART_CR_TXRST | CDNS_UART_CR_RXRST))
			cpu_relax();

		/*
		 * Clear the RX disable and TX disable bits and then set the TX
		 * enable bit and RX enable bit to enable the transmitter and
		 * receiver.
		 */
		writel(rx_timeout, port->membase + CDNS_UART_RXTOUT);
		ctrl_reg = readl(port->membase + CDNS_UART_CR);
		ctrl_reg &= ~(CDNS_UART_CR_TX_DIS | CDNS_UART_CR_RX_DIS);
		ctrl_reg |= CDNS_UART_CR_TX_EN | CDNS_UART_CR_RX_EN;
		writel(ctrl_reg, port->membase + CDNS_UART_CR);

		spin_unlock_irqrestore(&cdns_uart->port->lock, flags);

		return NOTIFY_OK;
	default:
		return NOTIFY_DONE;
	}
}
#endif

/**
 * cdns_uart_start_tx -  Start transmitting bytes
 * @port: Handle to the uart port structure
 */
static void cdns_uart_start_tx(struct uart_port *port)
{
	unsigned int status;

	if (uart_tx_stopped(port))
		return;

	/*
	 * Set the TX enable bit and clear the TX disable bit to enable the
	 * transmitter.
	 */
	status = readl(port->membase + CDNS_UART_CR);
	status &= ~CDNS_UART_CR_TX_DIS;
	status |= CDNS_UART_CR_TX_EN;
	writel(status, port->membase + CDNS_UART_CR);

	if (uart_circ_empty(&port->state->xmit))
		return;

	cdns_uart_handle_tx(port);

	writel(CDNS_UART_IXR_TXEMPTY, port->membase + CDNS_UART_ISR);
	/* Enable the TX Empty interrupt */
	writel(CDNS_UART_IXR_TXEMPTY, port->membase + CDNS_UART_IER);
}

/**
 * cdns_uart_stop_tx - Stop TX
 * @port: Handle to the uart port structure
 */
static void cdns_uart_stop_tx(struct uart_port *port)
{
	unsigned int regval;

	regval = readl(port->membase + CDNS_UART_CR);
	regval |= CDNS_UART_CR_TX_DIS;
	/* Disable the transmitter */
	writel(regval, port->membase + CDNS_UART_CR);
}

/**
 * cdns_uart_stop_rx - Stop RX
 * @port: Handle to the uart port structure
 */
static void cdns_uart_stop_rx(struct uart_port *port)
{
	unsigned int regval;

	/* Disable RX IRQs */
	writel(CDNS_UART_RX_IRQS, port->membase + CDNS_UART_IDR);

	/* Disable the receiver */
	regval = readl(port->membase + CDNS_UART_CR);
	regval |= CDNS_UART_CR_RX_DIS;
	writel(regval, port->membase + CDNS_UART_CR);
}

/**
 * cdns_uart_tx_empty -  Check whether TX is empty
 * @port: Handle to the uart port structure
 *
 * Return: TIOCSER_TEMT on success, 0 otherwise
 */
static unsigned int cdns_uart_tx_empty(struct uart_port *port)
{
	unsigned int status;

	status = readl(port->membase + CDNS_UART_SR) &
				CDNS_UART_SR_TXEMPTY;
	return status ? TIOCSER_TEMT : 0;
}

/**
 * cdns_uart_break_ctl - Based on the input ctl we have to start or stop
 *			transmitting char breaks
 * @port: Handle to the uart port structure
 * @ctl: Value based on which start or stop decision is taken
 */
static void cdns_uart_break_ctl(struct uart_port *port, int ctl)
{
	unsigned int status;
	unsigned long flags;

	spin_lock_irqsave(&port->lock, flags);

	status = readl(port->membase + CDNS_UART_CR);

	if (ctl == -1)
		writel(CDNS_UART_CR_STARTBRK | status,
				port->membase + CDNS_UART_CR);
	else {
		if ((status & CDNS_UART_CR_STOPBRK) == 0)
			writel(CDNS_UART_CR_STOPBRK | status,
					port->membase + CDNS_UART_CR);
	}
	spin_unlock_irqrestore(&port->lock, flags);
}

/**
 * cdns_uart_set_termios - termios operations, handling data length, parity,
 *				stop bits, flow control, baud rate
 * @port: Handle to the uart port structure
 * @termios: Handle to the input termios structure
 * @old: Values of the previously saved termios structure
 */
static void cdns_uart_set_termios(struct uart_port *port,
				struct ktermios *termios, struct ktermios *old)
{
	unsigned int cval = 0;
	unsigned int baud, minbaud, maxbaud;
	unsigned long flags;
	unsigned int ctrl_reg, mode_reg;

	spin_lock_irqsave(&port->lock, flags);

	/* Wait for the transmit FIFO to empty before making changes */
	if (!(readl(port->membase + CDNS_UART_CR) &
				CDNS_UART_CR_TX_DIS)) {
		while (!(readl(port->membase + CDNS_UART_SR) &
				CDNS_UART_SR_TXEMPTY)) {
			cpu_relax();
		}
	}

	/* Disable the TX and RX to set baud rate */
	ctrl_reg = readl(port->membase + CDNS_UART_CR);
	ctrl_reg |= CDNS_UART_CR_TX_DIS | CDNS_UART_CR_RX_DIS;
	writel(ctrl_reg, port->membase + CDNS_UART_CR);

	/*
	 * Min baud rate = 6bps and Max Baud Rate is 10Mbps for 100Mhz clk
	 * min and max baud should be calculated here based on port->uartclk.
	 * this way we get a valid baud and can safely call set_baud()
	 */
	minbaud = port->uartclk /
			((CDNS_UART_BDIV_MAX + 1) * CDNS_UART_CD_MAX * 8);
	maxbaud = port->uartclk / (CDNS_UART_BDIV_MIN + 1);
	baud = uart_get_baud_rate(port, termios, old, minbaud, maxbaud);
	baud = cdns_uart_set_baud_rate(port, baud);
	if (tty_termios_baud_rate(termios))
		tty_termios_encode_baud_rate(termios, baud, baud);

	/* Update the per-port timeout. */
	uart_update_timeout(port, termios->c_cflag, baud);

	/* Set TX/RX Reset */
	ctrl_reg = readl(port->membase + CDNS_UART_CR);
	ctrl_reg |= CDNS_UART_CR_TXRST | CDNS_UART_CR_RXRST;
	writel(ctrl_reg, port->membase + CDNS_UART_CR);

	/*
	 * Clear the RX disable and TX disable bits and then set the TX enable
	 * bit and RX enable bit to enable the transmitter and receiver.
	 */
	ctrl_reg = readl(port->membase + CDNS_UART_CR);
	ctrl_reg &= ~(CDNS_UART_CR_TX_DIS | CDNS_UART_CR_RX_DIS);
	ctrl_reg |= CDNS_UART_CR_TX_EN | CDNS_UART_CR_RX_EN;
	writel(ctrl_reg, port->membase + CDNS_UART_CR);

	writel(rx_timeout, port->membase + CDNS_UART_RXTOUT);

	port->read_status_mask = CDNS_UART_IXR_TXEMPTY | CDNS_UART_IXR_RXTRIG |
			CDNS_UART_IXR_OVERRUN | CDNS_UART_IXR_TOUT;
	port->ignore_status_mask = 0;

	if (termios->c_iflag & INPCK)
		port->read_status_mask |= CDNS_UART_IXR_PARITY |
		CDNS_UART_IXR_FRAMING;

	if (termios->c_iflag & IGNPAR)
		port->ignore_status_mask |= CDNS_UART_IXR_PARITY |
			CDNS_UART_IXR_FRAMING | CDNS_UART_IXR_OVERRUN;

	/* ignore all characters if CREAD is not set */
	if ((termios->c_cflag & CREAD) == 0)
		port->ignore_status_mask |= CDNS_UART_IXR_RXTRIG |
			CDNS_UART_IXR_TOUT | CDNS_UART_IXR_PARITY |
			CDNS_UART_IXR_FRAMING | CDNS_UART_IXR_OVERRUN;

	mode_reg = readl(port->membase + CDNS_UART_MR);

	/* Handling Data Size */
	switch (termios->c_cflag & CSIZE) {
	case CS6:
		cval |= CDNS_UART_MR_CHARLEN_6_BIT;
		break;
	case CS7:
		cval |= CDNS_UART_MR_CHARLEN_7_BIT;
		break;
	default:
	case CS8:
		cval |= CDNS_UART_MR_CHARLEN_8_BIT;
		termios->c_cflag &= ~CSIZE;
		termios->c_cflag |= CS8;
		break;
	}

	/* Handling Parity and Stop Bits length */
	if (termios->c_cflag & CSTOPB)
		cval |= CDNS_UART_MR_STOPMODE_2_BIT; /* 2 STOP bits */
	else
		cval |= CDNS_UART_MR_STOPMODE_1_BIT; /* 1 STOP bit */

	if (termios->c_cflag & PARENB) {
		/* Mark or Space parity */
		if (termios->c_cflag & CMSPAR) {
			if (termios->c_cflag & PARODD)
				cval |= CDNS_UART_MR_PARITY_MARK;
			else
				cval |= CDNS_UART_MR_PARITY_SPACE;
		} else {
			if (termios->c_cflag & PARODD)
				cval |= CDNS_UART_MR_PARITY_ODD;
			else
				cval |= CDNS_UART_MR_PARITY_EVEN;
		}
	} else {
		cval |= CDNS_UART_MR_PARITY_NONE;
	}
	cval |= mode_reg & 1;
	writel(cval, port->membase + CDNS_UART_MR);

	spin_unlock_irqrestore(&port->lock, flags);
}

/**
 * cdns_uart_startup - Called when an application opens a cdns_uart port
 * @port: Handle to the uart port structure
 *
 * Return: 0 on success, negative errno otherwise
 */
static int cdns_uart_startup(struct uart_port *port)
{
	int ret;
	unsigned long flags;
	unsigned int status = 0;

	spin_lock_irqsave(&port->lock, flags);

	/* Disable the TX and RX */
	writel(CDNS_UART_CR_TX_DIS | CDNS_UART_CR_RX_DIS,
			port->membase + CDNS_UART_CR);

	/* Set the Control Register with TX/RX Enable, TX/RX Reset,
	 * no break chars.
	 */
	writel(CDNS_UART_CR_TXRST | CDNS_UART_CR_RXRST,
			port->membase + CDNS_UART_CR);

	/*
	 * Clear the RX disable bit and then set the RX enable bit to enable
	 * the receiver.
	 */
	status = readl(port->membase + CDNS_UART_CR);
	status &= CDNS_UART_CR_RX_DIS;
	status |= CDNS_UART_CR_RX_EN;
	writel(status, port->membase + CDNS_UART_CR);

	/* Set the Mode Register with normal mode,8 data bits,1 stop bit,
	 * no parity.
	 */
	writel(CDNS_UART_MR_CHMODE_NORM | CDNS_UART_MR_STOPMODE_1_BIT
		| CDNS_UART_MR_PARITY_NONE | CDNS_UART_MR_CHARLEN_8_BIT,
		port->membase + CDNS_UART_MR);

	/*
	 * Set the RX FIFO Trigger level to use most of the FIFO, but it
	 * can be tuned with a module parameter
	 */
	writel(rx_trigger_level, port->membase + CDNS_UART_RXWM);

	/*
	 * Receive Timeout register is enabled but it
	 * can be tuned with a module parameter
	 */
	writel(rx_timeout, port->membase + CDNS_UART_RXTOUT);

	/* Clear out any pending interrupts before enabling them */
	writel(readl(port->membase + CDNS_UART_ISR),
			port->membase + CDNS_UART_ISR);

	spin_unlock_irqrestore(&port->lock, flags);

	ret = request_irq(port->irq, cdns_uart_isr, 0, CDNS_UART_NAME, port);
	if (ret) {
		dev_err(port->dev, "request_irq '%d' failed with %d\n",
			port->irq, ret);
		return ret;
	}

	/* Set the Interrupt Registers with desired interrupts */
	writel(CDNS_UART_RX_IRQS, port->membase + CDNS_UART_IER);

	return 0;
}

/**
 * cdns_uart_shutdown - Called when an application closes a cdns_uart port
 * @port: Handle to the uart port structure
 */
static void cdns_uart_shutdown(struct uart_port *port)
{
	int status;
	unsigned long flags;

	spin_lock_irqsave(&port->lock, flags);

	/* Disable interrupts */
	status = readl(port->membase + CDNS_UART_IMR);
	writel(status, port->membase + CDNS_UART_IDR);
	writel(0xffffffff, port->membase + CDNS_UART_ISR);

	/* Disable the TX and RX */
	writel(CDNS_UART_CR_TX_DIS | CDNS_UART_CR_RX_DIS,
			port->membase + CDNS_UART_CR);

	spin_unlock_irqrestore(&port->lock, flags);

	free_irq(port->irq, port);
}

/**
 * cdns_uart_type - Set UART type to cdns_uart port
 * @port: Handle to the uart port structure
 *
 * Return: string on success, NULL otherwise
 */
static const char *cdns_uart_type(struct uart_port *port)
{
	return port->type == PORT_XUARTPS ? CDNS_UART_NAME : NULL;
}

/**
 * cdns_uart_verify_port - Verify the port params
 * @port: Handle to the uart port structure
 * @ser: Handle to the structure whose members are compared
 *
 * Return: 0 on success, negative errno otherwise.
 */
static int cdns_uart_verify_port(struct uart_port *port,
					struct serial_struct *ser)
{
	if (ser->type != PORT_UNKNOWN && ser->type != PORT_XUARTPS)
		return -EINVAL;
	if (port->irq != ser->irq)
		return -EINVAL;
	if (ser->io_type != UPIO_MEM)
		return -EINVAL;
	if (port->iobase != ser->port)
		return -EINVAL;
	if (ser->hub6 != 0)
		return -EINVAL;
	return 0;
}

/**
 * cdns_uart_request_port - Claim the memory region attached to cdns_uart port,
 *				called when the driver adds a cdns_uart port via
 *				uart_add_one_port()
 * @port: Handle to the uart port structure
 *
 * Return: 0 on success, negative errno otherwise.
 */
static int cdns_uart_request_port(struct uart_port *port)
{
	if (!request_mem_region(port->mapbase, CDNS_UART_REGISTER_SPACE,
					 CDNS_UART_NAME)) {
		return -ENOMEM;
	}

	port->membase = ioremap(port->mapbase, CDNS_UART_REGISTER_SPACE);
	if (!port->membase) {
		dev_err(port->dev, "Unable to map registers\n");
		release_mem_region(port->mapbase, CDNS_UART_REGISTER_SPACE);
		return -ENOMEM;
	}
	return 0;
}

/**
 * cdns_uart_release_port - Release UART port
 * @port: Handle to the uart port structure
 *
 * Release the memory region attached to a cdns_uart port. Called when the
 * driver removes a cdns_uart port via uart_remove_one_port().
 */
static void cdns_uart_release_port(struct uart_port *port)
{
	release_mem_region(port->mapbase, CDNS_UART_REGISTER_SPACE);
	iounmap(port->membase);
	port->membase = NULL;
}

/**
 * cdns_uart_config_port - Configure UART port
 * @port: Handle to the uart port structure
 * @flags: If any
 */
static void cdns_uart_config_port(struct uart_port *port, int flags)
{
	if (flags & UART_CONFIG_TYPE && cdns_uart_request_port(port) == 0)
		port->type = PORT_XUARTPS;
}

/**
 * cdns_uart_get_mctrl - Get the modem control state
 * @port: Handle to the uart port structure
 *
 * Return: the modem control state
 */
static unsigned int cdns_uart_get_mctrl(struct uart_port *port)
{
	return TIOCM_CTS | TIOCM_DSR | TIOCM_CAR;
}

static void cdns_uart_set_mctrl(struct uart_port *port, unsigned int mctrl)
{
	u32 val;

	val = readl(port->membase + CDNS_UART_MODEMCR);

	val &= ~(CDNS_UART_MODEMCR_RTS | CDNS_UART_MODEMCR_DTR);

	if (mctrl & TIOCM_RTS)
		val |= CDNS_UART_MODEMCR_RTS;
	if (mctrl & TIOCM_DTR)
		val |= CDNS_UART_MODEMCR_DTR;

	writel(val, port->membase + CDNS_UART_MODEMCR);
}

#ifdef CONFIG_CONSOLE_POLL
static int cdns_uart_poll_get_char(struct uart_port *port)
{
	int c;
	unsigned long flags;

	spin_lock_irqsave(&port->lock, flags);

	/* Check if FIFO is empty */
	if (readl(port->membase + CDNS_UART_SR) & CDNS_UART_SR_RXEMPTY)
		c = NO_POLL_CHAR;
	else /* Read a character */
		c = (unsigned char) readl(port->membase + CDNS_UART_FIFO);

	spin_unlock_irqrestore(&port->lock, flags);

	return c;
}

static void cdns_uart_poll_put_char(struct uart_port *port, unsigned char c)
{
	unsigned long flags;

	spin_lock_irqsave(&port->lock, flags);

	/* Wait until FIFO is empty */
	while (!(readl(port->membase + CDNS_UART_SR) & CDNS_UART_SR_TXEMPTY))
		cpu_relax();

	/* Write a character */
	writel(c, port->membase + CDNS_UART_FIFO);

	/* Wait until FIFO is empty */
	while (!(readl(port->membase + CDNS_UART_SR) & CDNS_UART_SR_TXEMPTY))
		cpu_relax();

	spin_unlock_irqrestore(&port->lock, flags);

	return;
}
#endif

static void cdns_uart_pm(struct uart_port *port, unsigned int state,
		   unsigned int oldstate)
{
	struct cdns_uart *cdns_uart = port->private_data;

	switch (state) {
	case UART_PM_STATE_OFF:
		clk_disable(cdns_uart->uartclk);
		clk_disable(cdns_uart->pclk);
		break;
	default:
		clk_enable(cdns_uart->pclk);
		clk_enable(cdns_uart->uartclk);
		break;
	}
}

static struct uart_ops cdns_uart_ops = {
	.set_mctrl	= cdns_uart_set_mctrl,
	.get_mctrl	= cdns_uart_get_mctrl,
	.start_tx	= cdns_uart_start_tx,
	.stop_tx	= cdns_uart_stop_tx,
	.stop_rx	= cdns_uart_stop_rx,
	.tx_empty	= cdns_uart_tx_empty,
	.break_ctl	= cdns_uart_break_ctl,
	.set_termios	= cdns_uart_set_termios,
	.startup	= cdns_uart_startup,
	.shutdown	= cdns_uart_shutdown,
	.pm		= cdns_uart_pm,
	.type		= cdns_uart_type,
	.verify_port	= cdns_uart_verify_port,
	.request_port	= cdns_uart_request_port,
	.release_port	= cdns_uart_release_port,
	.config_port	= cdns_uart_config_port,
#ifdef CONFIG_CONSOLE_POLL
	.poll_get_char	= cdns_uart_poll_get_char,
	.poll_put_char	= cdns_uart_poll_put_char,
#endif
};

static struct uart_port cdns_uart_port[CDNS_UART_NR_PORTS];

/**
 * cdns_uart_get_port - Configure the port from platform device resource info
 * @id: Port id
 *
 * Return: a pointer to a uart_port or NULL for failure
 */
static struct uart_port *cdns_uart_get_port(int id)
{
	struct uart_port *port;

	/* Try the given port id if failed use default method */
	if (cdns_uart_port[id].mapbase != 0) {
		/* Find the next unused port */
		for (id = 0; id < CDNS_UART_NR_PORTS; id++)
			if (cdns_uart_port[id].mapbase == 0)
				break;
	}

	if (id >= CDNS_UART_NR_PORTS)
		return NULL;

	port = &cdns_uart_port[id];

	/* At this point, we've got an empty uart_port struct, initialize it */
	spin_lock_init(&port->lock);
	port->membase	= NULL;
	port->irq	= 0;
	port->type	= PORT_UNKNOWN;
	port->iotype	= UPIO_MEM32;
	port->flags	= UPF_BOOT_AUTOCONF;
	port->ops	= &cdns_uart_ops;
	port->fifosize	= CDNS_UART_FIFO_SIZE;
	port->line	= id;
	port->dev	= NULL;
	return port;
}

#ifdef CONFIG_SERIAL_XILINX_PS_UART_CONSOLE
/**
 * cdns_uart_console_wait_tx - Wait for the TX to be full
 * @port: Handle to the uart port structure
 */
static void cdns_uart_console_wait_tx(struct uart_port *port)
{
	while (!(readl(port->membase + CDNS_UART_SR) & CDNS_UART_SR_TXEMPTY))
		barrier();
}

/**
 * cdns_uart_console_putchar - write the character to the FIFO buffer
 * @port: Handle to the uart port structure
 * @ch: Character to be written
 */
static void cdns_uart_console_putchar(struct uart_port *port, int ch)
{
	cdns_uart_console_wait_tx(port);
	writel(ch, port->membase + CDNS_UART_FIFO);
}

static void __init cdns_early_write(struct console *con, const char *s,
				    unsigned n)
{
	struct earlycon_device *dev = con->data;

	uart_console_write(&dev->port, s, n, cdns_uart_console_putchar);
}

static int __init cdns_early_console_setup(struct earlycon_device *device,
					   const char *opt)
{
	if (!device->port.membase)
		return -ENODEV;

	device->con->write = cdns_early_write;

	return 0;
}
OF_EARLYCON_DECLARE(cdns, "xlnx,xuartps", cdns_early_console_setup);
OF_EARLYCON_DECLARE(cdns, "cdns,uart-r1p8", cdns_early_console_setup);
OF_EARLYCON_DECLARE(cdns, "cdns,uart-r1p12", cdns_early_console_setup);

/**
 * cdns_uart_console_write - perform write operation
 * @co: Console handle
 * @s: Pointer to character array
 * @count: No of characters
 */
static void cdns_uart_console_write(struct console *co, const char *s,
				unsigned int count)
{
	struct uart_port *port = &cdns_uart_port[co->index];
	unsigned long flags;
	unsigned int imr, ctrl;
	int locked = 1;

	if (port->sysrq)
		locked = 0;
	else if (oops_in_progress)
		locked = spin_trylock_irqsave(&port->lock, flags);
	else
		spin_lock_irqsave(&port->lock, flags);

	/* save and disable interrupt */
	imr = readl(port->membase + CDNS_UART_IMR);
	writel(imr, port->membase + CDNS_UART_IDR);

	/*
	 * Make sure that the tx part is enabled. Set the TX enable bit and
	 * clear the TX disable bit to enable the transmitter.
	 */
	ctrl = readl(port->membase + CDNS_UART_CR);
	ctrl &= ~CDNS_UART_CR_TX_DIS;
	ctrl |= CDNS_UART_CR_TX_EN;
	writel(ctrl, port->membase + CDNS_UART_CR);

	uart_console_write(port, s, count, cdns_uart_console_putchar);
	cdns_uart_console_wait_tx(port);

	writel(ctrl, port->membase + CDNS_UART_CR);

	/* restore interrupt state */
	writel(imr, port->membase + CDNS_UART_IER);

	if (locked)
		spin_unlock_irqrestore(&port->lock, flags);
}

/**
 * cdns_uart_console_setup - Initialize the uart to default config
 * @co: Console handle
 * @options: Initial settings of uart
 *
 * Return: 0 on success, negative errno otherwise.
 */
static int __init cdns_uart_console_setup(struct console *co, char *options)
{
	struct uart_port *port = &cdns_uart_port[co->index];
	int baud = 9600;
	int bits = 8;
	int parity = 'n';
	int flow = 'n';

	if (co->index < 0 || co->index >= CDNS_UART_NR_PORTS)
		return -EINVAL;

	if (!port->membase) {
		pr_debug("console on " CDNS_UART_TTY_NAME "%i not present\n",
			 co->index);
		return -ENODEV;
	}

	if (options)
		uart_parse_options(options, &baud, &parity, &bits, &flow);

	return uart_set_options(port, co, baud, parity, bits, flow);
}

static struct uart_driver cdns_uart_uart_driver;

static struct console cdns_uart_console = {
	.name	= CDNS_UART_TTY_NAME,
	.write	= cdns_uart_console_write,
	.device	= uart_console_device,
	.setup	= cdns_uart_console_setup,
	.flags	= CON_PRINTBUFFER,
	.index	= -1, /* Specified on the cmdline (e.g. console=ttyPS ) */
	.data	= &cdns_uart_uart_driver,
};

/**
 * cdns_uart_console_init - Initialization call
 *
 * Return: 0 on success, negative errno otherwise
 */
static int __init cdns_uart_console_init(void)
{
	register_console(&cdns_uart_console);
	return 0;
}

console_initcall(cdns_uart_console_init);

#endif /* CONFIG_SERIAL_XILINX_PS_UART_CONSOLE */

static struct uart_driver cdns_uart_uart_driver = {
	.owner		= THIS_MODULE,
	.driver_name	= CDNS_UART_NAME,
	.dev_name	= CDNS_UART_TTY_NAME,
	.major		= CDNS_UART_MAJOR,
	.minor		= CDNS_UART_MINOR,
	.nr		= CDNS_UART_NR_PORTS,
#ifdef CONFIG_SERIAL_XILINX_PS_UART_CONSOLE
	.cons		= &cdns_uart_console,
#endif
};

#ifdef CONFIG_PM_SLEEP
/**
 * cdns_uart_suspend - suspend event
 * @device: Pointer to the device structure
 *
 * Return: 0
 */
static int cdns_uart_suspend(struct device *device)
{
	struct uart_port *port = dev_get_drvdata(device);
	struct tty_struct *tty;
	struct device *tty_dev;
	int may_wake = 0;

	/* Get the tty which could be NULL so don't assume it's valid */
	tty = tty_port_tty_get(&port->state->port);
	if (tty) {
		tty_dev = tty->dev;
		may_wake = device_may_wakeup(tty_dev);
		tty_kref_put(tty);
	}

	/*
	 * Call the API provided in serial_core.c file which handles
	 * the suspend.
	 */
	uart_suspend_port(&cdns_uart_uart_driver, port);
	if (console_suspend_enabled && !may_wake) {
		struct cdns_uart *cdns_uart = port->private_data;

		clk_disable(cdns_uart->uartclk);
		clk_disable(cdns_uart->pclk);
	} else {
		unsigned long flags = 0;

		spin_lock_irqsave(&port->lock, flags);
		/* Empty the receive FIFO 1st before making changes */
		while (!(readl(port->membase + CDNS_UART_SR) &
					CDNS_UART_SR_RXEMPTY))
			readl(port->membase + CDNS_UART_FIFO);
		/* set RX trigger level to 1 */
		writel(1, port->membase + CDNS_UART_RXWM);
		/* disable RX timeout interrups */
		writel(CDNS_UART_IXR_TOUT, port->membase + CDNS_UART_IDR);
		spin_unlock_irqrestore(&port->lock, flags);
	}

	return 0;
}

/**
 * cdns_uart_resume - Resume after a previous suspend
 * @device: Pointer to the device structure
 *
 * Return: 0
 */
static int cdns_uart_resume(struct device *device)
{
	struct uart_port *port = dev_get_drvdata(device);
	unsigned long flags = 0;
	u32 ctrl_reg;
	struct tty_struct *tty;
	struct device *tty_dev;
	int may_wake = 0;

	/* Get the tty which could be NULL so don't assume it's valid */
	tty = tty_port_tty_get(&port->state->port);
	if (tty) {
		tty_dev = tty->dev;
		may_wake = device_may_wakeup(tty_dev);
		tty_kref_put(tty);
	}

	if (console_suspend_enabled && !may_wake) {
		struct cdns_uart *cdns_uart = port->private_data;

		clk_enable(cdns_uart->pclk);
		clk_enable(cdns_uart->uartclk);

		spin_lock_irqsave(&port->lock, flags);

		/* Set TX/RX Reset */
		ctrl_reg = readl(port->membase + CDNS_UART_CR);
		ctrl_reg |= CDNS_UART_CR_TXRST | CDNS_UART_CR_RXRST;
		writel(ctrl_reg, port->membase + CDNS_UART_CR);
		while (readl(port->membase + CDNS_UART_CR) &
				(CDNS_UART_CR_TXRST | CDNS_UART_CR_RXRST))
			cpu_relax();

		/* restore rx timeout value */
		writel(rx_timeout, port->membase + CDNS_UART_RXTOUT);
		/* Enable Tx/Rx */
		ctrl_reg = readl(port->membase + CDNS_UART_CR);
		ctrl_reg &= ~(CDNS_UART_CR_TX_DIS | CDNS_UART_CR_RX_DIS);
		ctrl_reg |= CDNS_UART_CR_TX_EN | CDNS_UART_CR_RX_EN;
		writel(ctrl_reg, port->membase + CDNS_UART_CR);

		spin_unlock_irqrestore(&port->lock, flags);
	} else {
		spin_lock_irqsave(&port->lock, flags);
		/* restore original rx trigger level */
		writel(rx_trigger_level, port->membase + CDNS_UART_RXWM);
		/* enable RX timeout interrupt */
		writel(CDNS_UART_IXR_TOUT, port->membase + CDNS_UART_IER);
		spin_unlock_irqrestore(&port->lock, flags);
	}

	return uart_resume_port(&cdns_uart_uart_driver, port);
}
#endif /* ! CONFIG_PM_SLEEP */

static SIMPLE_DEV_PM_OPS(cdns_uart_dev_pm_ops, cdns_uart_suspend,
		cdns_uart_resume);

/**
 * cdns_uart_probe - Platform driver probe
 * @pdev: Pointer to the platform device structure
 *
 * Return: 0 on success, negative errno otherwise
 */
static int cdns_uart_probe(struct platform_device *pdev)
{
	int rc, id, irq;
	struct uart_port *port;
	struct resource *res;
	struct cdns_uart *cdns_uart_data;

	cdns_uart_data = devm_kzalloc(&pdev->dev, sizeof(*cdns_uart_data),
			GFP_KERNEL);
	if (!cdns_uart_data)
		return -ENOMEM;

	cdns_uart_data->pclk = devm_clk_get(&pdev->dev, "pclk");
	if (IS_ERR(cdns_uart_data->pclk)) {
		cdns_uart_data->pclk = devm_clk_get(&pdev->dev, "aper_clk");
		if (!IS_ERR(cdns_uart_data->pclk))
			dev_err(&pdev->dev, "clock name 'aper_clk' is deprecated.\n");
	}
	if (IS_ERR(cdns_uart_data->pclk)) {
		dev_err(&pdev->dev, "pclk clock not found.\n");
		return PTR_ERR(cdns_uart_data->pclk);
	}

	cdns_uart_data->uartclk = devm_clk_get(&pdev->dev, "uart_clk");
	if (IS_ERR(cdns_uart_data->uartclk)) {
		cdns_uart_data->uartclk = devm_clk_get(&pdev->dev, "ref_clk");
		if (!IS_ERR(cdns_uart_data->uartclk))
			dev_err(&pdev->dev, "clock name 'ref_clk' is deprecated.\n");
	}
	if (IS_ERR(cdns_uart_data->uartclk)) {
		dev_err(&pdev->dev, "uart_clk clock not found.\n");
		return PTR_ERR(cdns_uart_data->uartclk);
	}

	rc = clk_prepare(cdns_uart_data->pclk);
	if (rc) {
		dev_err(&pdev->dev, "Unable to enable pclk clock.\n");
		return rc;
	}
	rc = clk_prepare(cdns_uart_data->uartclk);
	if (rc) {
		dev_err(&pdev->dev, "Unable to enable device clock.\n");
		goto err_out_clk_dis_pclk;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		rc = -ENODEV;
		goto err_out_clk_disable;
	}

	irq = platform_get_irq(pdev, 0);
	if (irq <= 0) {
		rc = -ENXIO;
		goto err_out_clk_disable;
	}

#ifdef CONFIG_COMMON_CLK
	cdns_uart_data->clk_rate_change_nb.notifier_call =
			cdns_uart_clk_notifier_cb;
	if (clk_notifier_register(cdns_uart_data->uartclk,
				&cdns_uart_data->clk_rate_change_nb))
		dev_warn(&pdev->dev, "Unable to register clock notifier.\n");
#endif
	/* Look for a serialN alias */
	id = of_alias_get_id(pdev->dev.of_node, "serial");
	if (id < 0)
		id = 0;

	/* Initialize the port structure */
	port = cdns_uart_get_port(id);

	if (!port) {
		dev_err(&pdev->dev, "Cannot get uart_port structure\n");
		rc = -ENODEV;
		goto err_out_notif_unreg;
	}

	/*
	 * Register the port.
	 * This function also registers this device with the tty layer
	 * and triggers invocation of the config_port() entry point.
	 */
	port->mapbase = res->start;
	port->irq = irq;
	port->dev = &pdev->dev;
	port->uartclk = clk_get_rate(cdns_uart_data->uartclk);
	port->private_data = cdns_uart_data;
	cdns_uart_data->port = port;
	platform_set_drvdata(pdev, port);

	rc = uart_add_one_port(&cdns_uart_uart_driver, port);
	if (rc) {
		dev_err(&pdev->dev,
			"uart_add_one_port() failed; err=%i\n", rc);
		goto err_out_notif_unreg;
	}

	return 0;

err_out_notif_unreg:
#ifdef CONFIG_COMMON_CLK
	clk_notifier_unregister(cdns_uart_data->uartclk,
			&cdns_uart_data->clk_rate_change_nb);
#endif
err_out_clk_disable:
	clk_unprepare(cdns_uart_data->uartclk);
err_out_clk_dis_pclk:
	clk_unprepare(cdns_uart_data->pclk);

	return rc;
}

/**
 * cdns_uart_remove - called when the platform driver is unregistered
 * @pdev: Pointer to the platform device structure
 *
 * Return: 0 on success, negative errno otherwise
 */
static int cdns_uart_remove(struct platform_device *pdev)
{
	struct uart_port *port = platform_get_drvdata(pdev);
	struct cdns_uart *cdns_uart_data = port->private_data;
	int rc;

	/* Remove the cdns_uart port from the serial core */
#ifdef CONFIG_COMMON_CLK
	clk_notifier_unregister(cdns_uart_data->uartclk,
			&cdns_uart_data->clk_rate_change_nb);
#endif
	rc = uart_remove_one_port(&cdns_uart_uart_driver, port);
	port->mapbase = 0;
	clk_unprepare(cdns_uart_data->uartclk);
	clk_unprepare(cdns_uart_data->pclk);
	return rc;
}

/* Match table for of_platform binding */
static const struct of_device_id cdns_uart_of_match[] = {
	{ .compatible = "xlnx,xuartps", },
	{ .compatible = "cdns,uart-r1p8", },
	{}
};
MODULE_DEVICE_TABLE(of, cdns_uart_of_match);

static struct platform_driver cdns_uart_platform_driver = {
	.probe   = cdns_uart_probe,
	.remove  = cdns_uart_remove,
	.driver  = {
		.name = CDNS_UART_NAME,
		.of_match_table = cdns_uart_of_match,
		.pm = &cdns_uart_dev_pm_ops,
		},
};

static int __init cdns_uart_init(void)
{
	int retval = 0;

	/* Register the cdns_uart driver with the serial core */
	retval = uart_register_driver(&cdns_uart_uart_driver);
	if (retval)
		return retval;

	/* Register the platform driver */
	retval = platform_driver_register(&cdns_uart_platform_driver);
	if (retval)
		uart_unregister_driver(&cdns_uart_uart_driver);

	return retval;
}

static void __exit cdns_uart_exit(void)
{
	/* Unregister the platform driver */
	platform_driver_unregister(&cdns_uart_platform_driver);

	/* Unregister the cdns_uart driver */
	uart_unregister_driver(&cdns_uart_uart_driver);
}

module_init(cdns_uart_init);
module_exit(cdns_uart_exit);

MODULE_DESCRIPTION("Driver for Cadence UART");
MODULE_AUTHOR("Xilinx Inc.");
MODULE_LICENSE("GPL");
