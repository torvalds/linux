/*
 * Xilinx PS UART driver
 *
 * 2011 (c) Xilinx Inc.
 *
 * This program is free software; you can redistribute it
 * and/or modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation;
 * either version 2 of the License, or (at your option) any
 * later version.
 *
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

#define XUARTPS_TTY_NAME	"ttyPS"
#define XUARTPS_NAME		"xuartps"
#define XUARTPS_MAJOR		0	/* use dynamic node allocation */
#define XUARTPS_MINOR		0	/* works best with devtmpfs */
#define XUARTPS_NR_PORTS	2
#define XUARTPS_FIFO_SIZE	64	/* FIFO size */
#define XUARTPS_REGISTER_SPACE	0xFFF

#define xuartps_readl(offset)		ioread32(port->membase + offset)
#define xuartps_writel(val, offset)	iowrite32(val, port->membase + offset)

/* Rx Trigger level */
static int rx_trigger_level = 56;
module_param(rx_trigger_level, uint, S_IRUGO);
MODULE_PARM_DESC(rx_trigger_level, "Rx trigger level, 1-63 bytes");

/* Rx Timeout */
static int rx_timeout = 10;
module_param(rx_timeout, uint, S_IRUGO);
MODULE_PARM_DESC(rx_timeout, "Rx timeout, 1-255");

/********************************Register Map********************************/
/** UART
 *
 * Register offsets for the UART.
 *
 */
#define XUARTPS_CR_OFFSET	0x00  /* Control Register [8:0] */
#define XUARTPS_MR_OFFSET	0x04  /* Mode Register [10:0] */
#define XUARTPS_IER_OFFSET	0x08  /* Interrupt Enable [10:0] */
#define XUARTPS_IDR_OFFSET	0x0C  /* Interrupt Disable [10:0] */
#define XUARTPS_IMR_OFFSET	0x10  /* Interrupt Mask [10:0] */
#define XUARTPS_ISR_OFFSET	0x14  /* Interrupt Status [10:0]*/
#define XUARTPS_BAUDGEN_OFFSET	0x18  /* Baud Rate Generator [15:0] */
#define XUARTPS_RXTOUT_OFFSET	0x1C  /* RX Timeout [7:0] */
#define XUARTPS_RXWM_OFFSET	0x20  /* RX FIFO Trigger Level [5:0] */
#define XUARTPS_MODEMCR_OFFSET	0x24  /* Modem Control [5:0] */
#define XUARTPS_MODEMSR_OFFSET	0x28  /* Modem Status [8:0] */
#define XUARTPS_SR_OFFSET	0x2C  /* Channel Status [11:0] */
#define XUARTPS_FIFO_OFFSET	0x30  /* FIFO [15:0] or [7:0] */
#define XUARTPS_BAUDDIV_OFFSET	0x34  /* Baud Rate Divider [7:0] */
#define XUARTPS_FLOWDEL_OFFSET	0x38  /* Flow Delay [15:0] */
#define XUARTPS_IRRX_PWIDTH_OFFSET 0x3C /* IR Minimum Received Pulse
						Width [15:0] */
#define XUARTPS_IRTX_PWIDTH_OFFSET 0x40 /* IR Transmitted pulse
						Width [7:0] */
#define XUARTPS_TXWM_OFFSET	0x44  /* TX FIFO Trigger Level [5:0] */

/** Control Register
 *
 * The Control register (CR) controls the major functions of the device.
 *
 * Control Register Bit Definitions
 */
#define XUARTPS_CR_STOPBRK	0x00000100  /* Stop TX break */
#define XUARTPS_CR_STARTBRK	0x00000080  /* Set TX break */
#define XUARTPS_CR_TX_DIS	0x00000020  /* TX disabled. */
#define XUARTPS_CR_TX_EN	0x00000010  /* TX enabled */
#define XUARTPS_CR_RX_DIS	0x00000008  /* RX disabled. */
#define XUARTPS_CR_RX_EN	0x00000004  /* RX enabled */
#define XUARTPS_CR_TXRST	0x00000002  /* TX logic reset */
#define XUARTPS_CR_RXRST	0x00000001  /* RX logic reset */
#define XUARTPS_CR_RST_TO	0x00000040  /* Restart Timeout Counter */

/** Mode Register
 *
 * The mode register (MR) defines the mode of transfer as well as the data
 * format. If this register is modified during transmission or reception,
 * data validity cannot be guaranteed.
 *
 * Mode Register Bit Definitions
 *
 */
#define XUARTPS_MR_CLKSEL		0x00000001  /* Pre-scalar selection */
#define XUARTPS_MR_CHMODE_L_LOOP	0x00000200  /* Local loop back mode */
#define XUARTPS_MR_CHMODE_NORM		0x00000000  /* Normal mode */

#define XUARTPS_MR_STOPMODE_2_BIT	0x00000080  /* 2 stop bits */
#define XUARTPS_MR_STOPMODE_1_BIT	0x00000000  /* 1 stop bit */

#define XUARTPS_MR_PARITY_NONE		0x00000020  /* No parity mode */
#define XUARTPS_MR_PARITY_MARK		0x00000018  /* Mark parity mode */
#define XUARTPS_MR_PARITY_SPACE		0x00000010  /* Space parity mode */
#define XUARTPS_MR_PARITY_ODD		0x00000008  /* Odd parity mode */
#define XUARTPS_MR_PARITY_EVEN		0x00000000  /* Even parity mode */

#define XUARTPS_MR_CHARLEN_6_BIT	0x00000006  /* 6 bits data */
#define XUARTPS_MR_CHARLEN_7_BIT	0x00000004  /* 7 bits data */
#define XUARTPS_MR_CHARLEN_8_BIT	0x00000000  /* 8 bits data */

/** Interrupt Registers
 *
 * Interrupt control logic uses the interrupt enable register (IER) and the
 * interrupt disable register (IDR) to set the value of the bits in the
 * interrupt mask register (IMR). The IMR determines whether to pass an
 * interrupt to the interrupt status register (ISR).
 * Writing a 1 to IER Enables an interrupt, writing a 1 to IDR disables an
 * interrupt. IMR and ISR are read only, and IER and IDR are write only.
 * Reading either IER or IDR returns 0x00.
 *
 * All four registers have the same bit definitions.
 */
#define XUARTPS_IXR_TOUT	0x00000100 /* RX Timeout error interrupt */
#define XUARTPS_IXR_PARITY	0x00000080 /* Parity error interrupt */
#define XUARTPS_IXR_FRAMING	0x00000040 /* Framing error interrupt */
#define XUARTPS_IXR_OVERRUN	0x00000020 /* Overrun error interrupt */
#define XUARTPS_IXR_TXFULL	0x00000010 /* TX FIFO Full interrupt */
#define XUARTPS_IXR_TXEMPTY	0x00000008 /* TX FIFO empty interrupt */
#define XUARTPS_ISR_RXEMPTY	0x00000002 /* RX FIFO empty interrupt */
#define XUARTPS_IXR_RXTRIG	0x00000001 /* RX FIFO trigger interrupt */
#define XUARTPS_IXR_RXFULL	0x00000004 /* RX FIFO full interrupt. */
#define XUARTPS_IXR_RXEMPTY	0x00000002 /* RX FIFO empty interrupt. */
#define XUARTPS_IXR_MASK	0x00001FFF /* Valid bit mask */

/* Goes in read_status_mask for break detection as the HW doesn't do it*/
#define XUARTPS_IXR_BRK		0x80000000

/** Channel Status Register
 *
 * The channel status register (CSR) is provided to enable the control logic
 * to monitor the status of bits in the channel interrupt status register,
 * even if these are masked out by the interrupt mask register.
 */
#define XUARTPS_SR_RXEMPTY	0x00000002 /* RX FIFO empty */
#define XUARTPS_SR_TXEMPTY	0x00000008 /* TX FIFO empty */
#define XUARTPS_SR_TXFULL	0x00000010 /* TX FIFO full */
#define XUARTPS_SR_RXTRIG	0x00000001 /* Rx Trigger */

/* baud dividers min/max values */
#define XUARTPS_BDIV_MIN	4
#define XUARTPS_BDIV_MAX	255
#define XUARTPS_CD_MAX		65535

/**
 * struct xuartps - device data
 * @port		Pointer to the UART port
 * @refclk		Reference clock
 * @aperclk		APB clock
 * @baud		Current baud rate
 * @clk_rate_change_nb	Notifier block for clock changes
 */
struct xuartps {
	struct uart_port	*port;
	struct clk		*refclk;
	struct clk		*aperclk;
	unsigned int		baud;
	struct notifier_block	clk_rate_change_nb;
};
#define to_xuartps(_nb) container_of(_nb, struct xuartps, clk_rate_change_nb);

/**
 * xuartps_isr - Interrupt handler
 * @irq: Irq number
 * @dev_id: Id of the port
 *
 * Returns IRQHANDLED
 **/
static irqreturn_t xuartps_isr(int irq, void *dev_id)
{
	struct uart_port *port = (struct uart_port *)dev_id;
	unsigned long flags;
	unsigned int isrstatus, numbytes;
	unsigned int data;
	char status = TTY_NORMAL;

	spin_lock_irqsave(&port->lock, flags);

	/* Read the interrupt status register to determine which
	 * interrupt(s) is/are active.
	 */
	isrstatus = xuartps_readl(XUARTPS_ISR_OFFSET);

	/*
	 * There is no hardware break detection, so we interpret framing
	 * error with all-zeros data as a break sequence. Most of the time,
	 * there's another non-zero byte at the end of the sequence.
	 */

	if (isrstatus & XUARTPS_IXR_FRAMING) {
		while (!(xuartps_readl(XUARTPS_SR_OFFSET) &
					XUARTPS_SR_RXEMPTY)) {
			if (!xuartps_readl(XUARTPS_FIFO_OFFSET)) {
				port->read_status_mask |= XUARTPS_IXR_BRK;
				isrstatus &= ~XUARTPS_IXR_FRAMING;
			}
		}
		xuartps_writel(XUARTPS_IXR_FRAMING, XUARTPS_ISR_OFFSET);
	}

	/* drop byte with parity error if IGNPAR specified */
	if (isrstatus & port->ignore_status_mask & XUARTPS_IXR_PARITY)
		isrstatus &= ~(XUARTPS_IXR_RXTRIG | XUARTPS_IXR_TOUT);

	isrstatus &= port->read_status_mask;
	isrstatus &= ~port->ignore_status_mask;

	if ((isrstatus & XUARTPS_IXR_TOUT) ||
		(isrstatus & XUARTPS_IXR_RXTRIG)) {
		/* Receive Timeout Interrupt */
		while ((xuartps_readl(XUARTPS_SR_OFFSET) &
			XUARTPS_SR_RXEMPTY) != XUARTPS_SR_RXEMPTY) {
			data = xuartps_readl(XUARTPS_FIFO_OFFSET);

			/* Non-NULL byte after BREAK is garbage (99%) */
			if (data && (port->read_status_mask &
						XUARTPS_IXR_BRK)) {
				port->read_status_mask &= ~XUARTPS_IXR_BRK;
				port->icount.brk++;
				if (uart_handle_break(port))
					continue;
			}

			/*
			 * uart_handle_sysrq_char() doesn't work if
			 * spinlocked, for some reason
			 */
			 if (port->sysrq) {
				spin_unlock(&port->lock);
				if (uart_handle_sysrq_char(port,
							(unsigned char)data)) {
					spin_lock(&port->lock);
					continue;
				}
				spin_lock(&port->lock);
			}

			port->icount.rx++;

			if (isrstatus & XUARTPS_IXR_PARITY) {
				port->icount.parity++;
				status = TTY_PARITY;
			} else if (isrstatus & XUARTPS_IXR_FRAMING) {
				port->icount.frame++;
				status = TTY_FRAME;
			} else if (isrstatus & XUARTPS_IXR_OVERRUN)
				port->icount.overrun++;

			uart_insert_char(port, isrstatus, XUARTPS_IXR_OVERRUN,
					data, status);
		}
		spin_unlock(&port->lock);
		tty_flip_buffer_push(&port->state->port);
		spin_lock(&port->lock);
	}

	/* Dispatch an appropriate handler */
	if ((isrstatus & XUARTPS_IXR_TXEMPTY) == XUARTPS_IXR_TXEMPTY) {
		if (uart_circ_empty(&port->state->xmit)) {
			xuartps_writel(XUARTPS_IXR_TXEMPTY,
						XUARTPS_IDR_OFFSET);
		} else {
			numbytes = port->fifosize;
			/* Break if no more data available in the UART buffer */
			while (numbytes--) {
				if (uart_circ_empty(&port->state->xmit))
					break;
				/* Get the data from the UART circular buffer
				 * and write it to the xuartps's TX_FIFO
				 * register.
				 */
				xuartps_writel(
					port->state->xmit.buf[port->state->xmit.
					tail], XUARTPS_FIFO_OFFSET);

				port->icount.tx++;

				/* Adjust the tail of the UART buffer and wrap
				 * the buffer if it reaches limit.
				 */
				port->state->xmit.tail =
					(port->state->xmit.tail + 1) & \
						(UART_XMIT_SIZE - 1);
			}

			if (uart_circ_chars_pending(
					&port->state->xmit) < WAKEUP_CHARS)
				uart_write_wakeup(port);
		}
	}

	xuartps_writel(isrstatus, XUARTPS_ISR_OFFSET);

	/* be sure to release the lock and tty before leaving */
	spin_unlock_irqrestore(&port->lock, flags);

	return IRQ_HANDLED;
}

/**
 * xuartps_calc_baud_divs - Calculate baud rate divisors
 * @clk: UART module input clock
 * @baud: Desired baud rate
 * @rbdiv: BDIV value (return value)
 * @rcd: CD value (return value)
 * @div8: Value for clk_sel bit in mod (return value)
 * Returns baud rate, requested baud when possible, or actual baud when there
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
static unsigned int xuartps_calc_baud_divs(unsigned int clk, unsigned int baud,
		u32 *rbdiv, u32 *rcd, int *div8)
{
	u32 cd, bdiv;
	unsigned int calc_baud;
	unsigned int bestbaud = 0;
	unsigned int bauderror;
	unsigned int besterror = ~0;

	if (baud < clk / ((XUARTPS_BDIV_MAX + 1) * XUARTPS_CD_MAX)) {
		*div8 = 1;
		clk /= 8;
	} else {
		*div8 = 0;
	}

	for (bdiv = XUARTPS_BDIV_MIN; bdiv <= XUARTPS_BDIV_MAX; bdiv++) {
		cd = DIV_ROUND_CLOSEST(clk, baud * (bdiv + 1));
		if (cd < 1 || cd > XUARTPS_CD_MAX)
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
 * xuartps_set_baud_rate - Calculate and set the baud rate
 * @port: Handle to the uart port structure
 * @baud: Baud rate to set
 * Returns baud rate, requested baud when possible, or actual baud when there
 *	   was too much error, zero if no valid divisors are found.
 */
static unsigned int xuartps_set_baud_rate(struct uart_port *port,
		unsigned int baud)
{
	unsigned int calc_baud;
	u32 cd, bdiv;
	u32 mreg;
	int div8;
	struct xuartps *xuartps = port->private_data;

	calc_baud = xuartps_calc_baud_divs(port->uartclk, baud, &bdiv, &cd,
			&div8);

	/* Write new divisors to hardware */
	mreg = xuartps_readl(XUARTPS_MR_OFFSET);
	if (div8)
		mreg |= XUARTPS_MR_CLKSEL;
	else
		mreg &= ~XUARTPS_MR_CLKSEL;
	xuartps_writel(mreg, XUARTPS_MR_OFFSET);
	xuartps_writel(cd, XUARTPS_BAUDGEN_OFFSET);
	xuartps_writel(bdiv, XUARTPS_BAUDDIV_OFFSET);
	xuartps->baud = baud;

	return calc_baud;
}

/**
 * xuartps_clk_notitifer_cb - Clock notifier callback
 * @nb:		Notifier block
 * @event:	Notify event
 * @data:	Notifier data
 * Returns NOTIFY_OK on success, NOTIFY_BAD on error.
 */
static int xuartps_clk_notifier_cb(struct notifier_block *nb,
		unsigned long event, void *data)
{
	u32 ctrl_reg;
	struct uart_port *port;
	int locked = 0;
	struct clk_notifier_data *ndata = data;
	unsigned long flags = 0;
	struct xuartps *xuartps = to_xuartps(nb);

	port = xuartps->port;
	if (port->suspended)
		return NOTIFY_OK;

	switch (event) {
	case PRE_RATE_CHANGE:
	{
		u32 bdiv;
		u32 cd;
		int div8;

		/*
		 * Find out if current baud-rate can be achieved with new clock
		 * frequency.
		 */
		if (!xuartps_calc_baud_divs(ndata->new_rate, xuartps->baud,
					&bdiv, &cd, &div8))
			return NOTIFY_BAD;

		spin_lock_irqsave(&xuartps->port->lock, flags);

		/* Disable the TX and RX to set baud rate */
		xuartps_writel(xuartps_readl(XUARTPS_CR_OFFSET) |
				(XUARTPS_CR_TX_DIS | XUARTPS_CR_RX_DIS),
				XUARTPS_CR_OFFSET);

		spin_unlock_irqrestore(&xuartps->port->lock, flags);

		return NOTIFY_OK;
	}
	case POST_RATE_CHANGE:
		/*
		 * Set clk dividers to generate correct baud with new clock
		 * frequency.
		 */

		spin_lock_irqsave(&xuartps->port->lock, flags);

		locked = 1;
		port->uartclk = ndata->new_rate;

		xuartps->baud = xuartps_set_baud_rate(xuartps->port,
				xuartps->baud);
		/* fall through */
	case ABORT_RATE_CHANGE:
		if (!locked)
			spin_lock_irqsave(&xuartps->port->lock, flags);

		/* Set TX/RX Reset */
		xuartps_writel(xuartps_readl(XUARTPS_CR_OFFSET) |
				(XUARTPS_CR_TXRST | XUARTPS_CR_RXRST),
				XUARTPS_CR_OFFSET);

		while (xuartps_readl(XUARTPS_CR_OFFSET) &
				(XUARTPS_CR_TXRST | XUARTPS_CR_RXRST))
			cpu_relax();

		/*
		 * Clear the RX disable and TX disable bits and then set the TX
		 * enable bit and RX enable bit to enable the transmitter and
		 * receiver.
		 */
		xuartps_writel(rx_timeout, XUARTPS_RXTOUT_OFFSET);
		ctrl_reg = xuartps_readl(XUARTPS_CR_OFFSET);
		xuartps_writel(
			(ctrl_reg & ~(XUARTPS_CR_TX_DIS | XUARTPS_CR_RX_DIS)) |
			(XUARTPS_CR_TX_EN | XUARTPS_CR_RX_EN),
			XUARTPS_CR_OFFSET);

		spin_unlock_irqrestore(&xuartps->port->lock, flags);

		return NOTIFY_OK;
	default:
		return NOTIFY_DONE;
	}
}

/*----------------------Uart Operations---------------------------*/

/**
 * xuartps_start_tx -  Start transmitting bytes
 * @port: Handle to the uart port structure
 *
 **/
static void xuartps_start_tx(struct uart_port *port)
{
	unsigned int status, numbytes = port->fifosize;

	if (uart_circ_empty(&port->state->xmit) || uart_tx_stopped(port))
		return;

	status = xuartps_readl(XUARTPS_CR_OFFSET);
	/* Set the TX enable bit and clear the TX disable bit to enable the
	 * transmitter.
	 */
	xuartps_writel((status & ~XUARTPS_CR_TX_DIS) | XUARTPS_CR_TX_EN,
		XUARTPS_CR_OFFSET);

	while (numbytes-- && ((xuartps_readl(XUARTPS_SR_OFFSET)
		& XUARTPS_SR_TXFULL)) != XUARTPS_SR_TXFULL) {

		/* Break if no more data available in the UART buffer */
		if (uart_circ_empty(&port->state->xmit))
			break;

		/* Get the data from the UART circular buffer and
		 * write it to the xuartps's TX_FIFO register.
		 */
		xuartps_writel(
			port->state->xmit.buf[port->state->xmit.tail],
			XUARTPS_FIFO_OFFSET);
		port->icount.tx++;

		/* Adjust the tail of the UART buffer and wrap
		 * the buffer if it reaches limit.
		 */
		port->state->xmit.tail = (port->state->xmit.tail + 1) &
					(UART_XMIT_SIZE - 1);
	}
	xuartps_writel(XUARTPS_IXR_TXEMPTY, XUARTPS_ISR_OFFSET);
	/* Enable the TX Empty interrupt */
	xuartps_writel(XUARTPS_IXR_TXEMPTY, XUARTPS_IER_OFFSET);

	if (uart_circ_chars_pending(&port->state->xmit) < WAKEUP_CHARS)
		uart_write_wakeup(port);
}

/**
 * xuartps_stop_tx - Stop TX
 * @port: Handle to the uart port structure
 *
 **/
static void xuartps_stop_tx(struct uart_port *port)
{
	unsigned int regval;

	regval = xuartps_readl(XUARTPS_CR_OFFSET);
	regval |= XUARTPS_CR_TX_DIS;
	/* Disable the transmitter */
	xuartps_writel(regval, XUARTPS_CR_OFFSET);
}

/**
 * xuartps_stop_rx - Stop RX
 * @port: Handle to the uart port structure
 *
 **/
static void xuartps_stop_rx(struct uart_port *port)
{
	unsigned int regval;

	regval = xuartps_readl(XUARTPS_CR_OFFSET);
	regval |= XUARTPS_CR_RX_DIS;
	/* Disable the receiver */
	xuartps_writel(regval, XUARTPS_CR_OFFSET);
}

/**
 * xuartps_tx_empty -  Check whether TX is empty
 * @port: Handle to the uart port structure
 *
 * Returns TIOCSER_TEMT on success, 0 otherwise
 **/
static unsigned int xuartps_tx_empty(struct uart_port *port)
{
	unsigned int status;

	status = xuartps_readl(XUARTPS_ISR_OFFSET) & XUARTPS_IXR_TXEMPTY;
	return status ? TIOCSER_TEMT : 0;
}

/**
 * xuartps_break_ctl - Based on the input ctl we have to start or stop
 *			transmitting char breaks
 * @port: Handle to the uart port structure
 * @ctl: Value based on which start or stop decision is taken
 *
 **/
static void xuartps_break_ctl(struct uart_port *port, int ctl)
{
	unsigned int status;
	unsigned long flags;

	spin_lock_irqsave(&port->lock, flags);

	status = xuartps_readl(XUARTPS_CR_OFFSET);

	if (ctl == -1)
		xuartps_writel(XUARTPS_CR_STARTBRK | status,
					XUARTPS_CR_OFFSET);
	else {
		if ((status & XUARTPS_CR_STOPBRK) == 0)
			xuartps_writel(XUARTPS_CR_STOPBRK | status,
					 XUARTPS_CR_OFFSET);
	}
	spin_unlock_irqrestore(&port->lock, flags);
}

/**
 * xuartps_set_termios - termios operations, handling data length, parity,
 *				stop bits, flow control, baud rate
 * @port: Handle to the uart port structure
 * @termios: Handle to the input termios structure
 * @old: Values of the previously saved termios structure
 *
 **/
static void xuartps_set_termios(struct uart_port *port,
				struct ktermios *termios, struct ktermios *old)
{
	unsigned int cval = 0;
	unsigned int baud, minbaud, maxbaud;
	unsigned long flags;
	unsigned int ctrl_reg, mode_reg;

	spin_lock_irqsave(&port->lock, flags);

	/* Empty the receive FIFO 1st before making changes */
	while ((xuartps_readl(XUARTPS_SR_OFFSET) &
		 XUARTPS_SR_RXEMPTY) != XUARTPS_SR_RXEMPTY) {
		xuartps_readl(XUARTPS_FIFO_OFFSET);
	}

	/* Disable the TX and RX to set baud rate */
	xuartps_writel(xuartps_readl(XUARTPS_CR_OFFSET) |
			(XUARTPS_CR_TX_DIS | XUARTPS_CR_RX_DIS),
			XUARTPS_CR_OFFSET);

	/*
	 * Min baud rate = 6bps and Max Baud Rate is 10Mbps for 100Mhz clk
	 * min and max baud should be calculated here based on port->uartclk.
	 * this way we get a valid baud and can safely call set_baud()
	 */
	minbaud = port->uartclk / ((XUARTPS_BDIV_MAX + 1) * XUARTPS_CD_MAX * 8);
	maxbaud = port->uartclk / (XUARTPS_BDIV_MIN + 1);
	baud = uart_get_baud_rate(port, termios, old, minbaud, maxbaud);
	baud = xuartps_set_baud_rate(port, baud);
	if (tty_termios_baud_rate(termios))
		tty_termios_encode_baud_rate(termios, baud, baud);

	/*
	 * Update the per-port timeout.
	 */
	uart_update_timeout(port, termios->c_cflag, baud);

	/* Set TX/RX Reset */
	xuartps_writel(xuartps_readl(XUARTPS_CR_OFFSET) |
			(XUARTPS_CR_TXRST | XUARTPS_CR_RXRST),
			XUARTPS_CR_OFFSET);

	ctrl_reg = xuartps_readl(XUARTPS_CR_OFFSET);

	/* Clear the RX disable and TX disable bits and then set the TX enable
	 * bit and RX enable bit to enable the transmitter and receiver.
	 */
	xuartps_writel(
		(ctrl_reg & ~(XUARTPS_CR_TX_DIS | XUARTPS_CR_RX_DIS))
			| (XUARTPS_CR_TX_EN | XUARTPS_CR_RX_EN),
			XUARTPS_CR_OFFSET);

	xuartps_writel(rx_timeout, XUARTPS_RXTOUT_OFFSET);

	port->read_status_mask = XUARTPS_IXR_TXEMPTY | XUARTPS_IXR_RXTRIG |
			XUARTPS_IXR_OVERRUN | XUARTPS_IXR_TOUT;
	port->ignore_status_mask = 0;

	if (termios->c_iflag & INPCK)
		port->read_status_mask |= XUARTPS_IXR_PARITY |
		XUARTPS_IXR_FRAMING;

	if (termios->c_iflag & IGNPAR)
		port->ignore_status_mask |= XUARTPS_IXR_PARITY |
			XUARTPS_IXR_FRAMING | XUARTPS_IXR_OVERRUN;

	/* ignore all characters if CREAD is not set */
	if ((termios->c_cflag & CREAD) == 0)
		port->ignore_status_mask |= XUARTPS_IXR_RXTRIG |
			XUARTPS_IXR_TOUT | XUARTPS_IXR_PARITY |
			XUARTPS_IXR_FRAMING | XUARTPS_IXR_OVERRUN;

	mode_reg = xuartps_readl(XUARTPS_MR_OFFSET);

	/* Handling Data Size */
	switch (termios->c_cflag & CSIZE) {
	case CS6:
		cval |= XUARTPS_MR_CHARLEN_6_BIT;
		break;
	case CS7:
		cval |= XUARTPS_MR_CHARLEN_7_BIT;
		break;
	default:
	case CS8:
		cval |= XUARTPS_MR_CHARLEN_8_BIT;
		termios->c_cflag &= ~CSIZE;
		termios->c_cflag |= CS8;
		break;
	}

	/* Handling Parity and Stop Bits length */
	if (termios->c_cflag & CSTOPB)
		cval |= XUARTPS_MR_STOPMODE_2_BIT; /* 2 STOP bits */
	else
		cval |= XUARTPS_MR_STOPMODE_1_BIT; /* 1 STOP bit */

	if (termios->c_cflag & PARENB) {
		/* Mark or Space parity */
		if (termios->c_cflag & CMSPAR) {
			if (termios->c_cflag & PARODD)
				cval |= XUARTPS_MR_PARITY_MARK;
			else
				cval |= XUARTPS_MR_PARITY_SPACE;
		} else {
			if (termios->c_cflag & PARODD)
				cval |= XUARTPS_MR_PARITY_ODD;
			else
				cval |= XUARTPS_MR_PARITY_EVEN;
		}
	} else {
		cval |= XUARTPS_MR_PARITY_NONE;
	}
	cval |= mode_reg & 1;
	xuartps_writel(cval, XUARTPS_MR_OFFSET);

	spin_unlock_irqrestore(&port->lock, flags);
}

/**
 * xuartps_startup - Called when an application opens a xuartps port
 * @port: Handle to the uart port structure
 *
 * Returns 0 on success, negative error otherwise
 **/
static int xuartps_startup(struct uart_port *port)
{
	unsigned int retval = 0, status = 0;

	retval = request_irq(port->irq, xuartps_isr, 0, XUARTPS_NAME,
								(void *)port);
	if (retval)
		return retval;

	/* Disable the TX and RX */
	xuartps_writel(XUARTPS_CR_TX_DIS | XUARTPS_CR_RX_DIS,
						XUARTPS_CR_OFFSET);

	/* Set the Control Register with TX/RX Enable, TX/RX Reset,
	 * no break chars.
	 */
	xuartps_writel(XUARTPS_CR_TXRST | XUARTPS_CR_RXRST,
				XUARTPS_CR_OFFSET);

	status = xuartps_readl(XUARTPS_CR_OFFSET);

	/* Clear the RX disable and TX disable bits and then set the TX enable
	 * bit and RX enable bit to enable the transmitter and receiver.
	 */
	xuartps_writel((status & ~(XUARTPS_CR_TX_DIS | XUARTPS_CR_RX_DIS))
			| (XUARTPS_CR_TX_EN | XUARTPS_CR_RX_EN |
			XUARTPS_CR_STOPBRK), XUARTPS_CR_OFFSET);

	/* Set the Mode Register with normal mode,8 data bits,1 stop bit,
	 * no parity.
	 */
	xuartps_writel(XUARTPS_MR_CHMODE_NORM | XUARTPS_MR_STOPMODE_1_BIT
		| XUARTPS_MR_PARITY_NONE | XUARTPS_MR_CHARLEN_8_BIT,
		 XUARTPS_MR_OFFSET);

	/*
	 * Set the RX FIFO Trigger level to use most of the FIFO, but it
	 * can be tuned with a module parameter
	 */
	xuartps_writel(rx_trigger_level, XUARTPS_RXWM_OFFSET);

	/*
	 * Receive Timeout register is enabled but it
	 * can be tuned with a module parameter
	 */
	xuartps_writel(rx_timeout, XUARTPS_RXTOUT_OFFSET);

	/* Clear out any pending interrupts before enabling them */
	xuartps_writel(xuartps_readl(XUARTPS_ISR_OFFSET), XUARTPS_ISR_OFFSET);

	/* Set the Interrupt Registers with desired interrupts */
	xuartps_writel(XUARTPS_IXR_TXEMPTY | XUARTPS_IXR_PARITY |
		XUARTPS_IXR_FRAMING | XUARTPS_IXR_OVERRUN |
		XUARTPS_IXR_RXTRIG | XUARTPS_IXR_TOUT, XUARTPS_IER_OFFSET);

	return retval;
}

/**
 * xuartps_shutdown - Called when an application closes a xuartps port
 * @port: Handle to the uart port structure
 *
 **/
static void xuartps_shutdown(struct uart_port *port)
{
	int status;

	/* Disable interrupts */
	status = xuartps_readl(XUARTPS_IMR_OFFSET);
	xuartps_writel(status, XUARTPS_IDR_OFFSET);

	/* Disable the TX and RX */
	xuartps_writel(XUARTPS_CR_TX_DIS | XUARTPS_CR_RX_DIS,
				 XUARTPS_CR_OFFSET);
	free_irq(port->irq, port);
}

/**
 * xuartps_type - Set UART type to xuartps port
 * @port: Handle to the uart port structure
 *
 * Returns string on success, NULL otherwise
 **/
static const char *xuartps_type(struct uart_port *port)
{
	return port->type == PORT_XUARTPS ? XUARTPS_NAME : NULL;
}

/**
 * xuartps_verify_port - Verify the port params
 * @port: Handle to the uart port structure
 * @ser: Handle to the structure whose members are compared
 *
 * Returns 0 if success otherwise -EINVAL
 **/
static int xuartps_verify_port(struct uart_port *port,
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
 * xuartps_request_port - Claim the memory region attached to xuartps port,
 *				called when the driver adds a xuartps port via
 *				uart_add_one_port()
 * @port: Handle to the uart port structure
 *
 * Returns 0, -ENOMEM if request fails
 **/
static int xuartps_request_port(struct uart_port *port)
{
	if (!request_mem_region(port->mapbase, XUARTPS_REGISTER_SPACE,
					 XUARTPS_NAME)) {
		return -ENOMEM;
	}

	port->membase = ioremap(port->mapbase, XUARTPS_REGISTER_SPACE);
	if (!port->membase) {
		dev_err(port->dev, "Unable to map registers\n");
		release_mem_region(port->mapbase, XUARTPS_REGISTER_SPACE);
		return -ENOMEM;
	}
	return 0;
}

/**
 * xuartps_release_port - Release the memory region attached to a xuartps
 *				port, called when the driver removes a xuartps
 *				port via uart_remove_one_port().
 * @port: Handle to the uart port structure
 *
 **/
static void xuartps_release_port(struct uart_port *port)
{
	release_mem_region(port->mapbase, XUARTPS_REGISTER_SPACE);
	iounmap(port->membase);
	port->membase = NULL;
}

/**
 * xuartps_config_port - Configure xuartps, called when the driver adds a
 *				xuartps port
 * @port: Handle to the uart port structure
 * @flags: If any
 *
 **/
static void xuartps_config_port(struct uart_port *port, int flags)
{
	if (flags & UART_CONFIG_TYPE && xuartps_request_port(port) == 0)
		port->type = PORT_XUARTPS;
}

/**
 * xuartps_get_mctrl - Get the modem control state
 *
 * @port: Handle to the uart port structure
 *
 * Returns the modem control state
 *
 **/
static unsigned int xuartps_get_mctrl(struct uart_port *port)
{
	return TIOCM_CTS | TIOCM_DSR | TIOCM_CAR;
}

static void xuartps_set_mctrl(struct uart_port *port, unsigned int mctrl)
{
	/* N/A */
}

static void xuartps_enable_ms(struct uart_port *port)
{
	/* N/A */
}

#ifdef CONFIG_CONSOLE_POLL
static int xuartps_poll_get_char(struct uart_port *port)
{
	u32 imr;
	int c;

	/* Disable all interrupts */
	imr = xuartps_readl(XUARTPS_IMR_OFFSET);
	xuartps_writel(imr, XUARTPS_IDR_OFFSET);

	/* Check if FIFO is empty */
	if (xuartps_readl(XUARTPS_SR_OFFSET) & XUARTPS_SR_RXEMPTY)
		c = NO_POLL_CHAR;
	else /* Read a character */
		c = (unsigned char) xuartps_readl(XUARTPS_FIFO_OFFSET);

	/* Enable interrupts */
	xuartps_writel(imr, XUARTPS_IER_OFFSET);

	return c;
}

static void xuartps_poll_put_char(struct uart_port *port, unsigned char c)
{
	u32 imr;

	/* Disable all interrupts */
	imr = xuartps_readl(XUARTPS_IMR_OFFSET);
	xuartps_writel(imr, XUARTPS_IDR_OFFSET);

	/* Wait until FIFO is empty */
	while (!(xuartps_readl(XUARTPS_SR_OFFSET) & XUARTPS_SR_TXEMPTY))
		cpu_relax();

	/* Write a character */
	xuartps_writel(c, XUARTPS_FIFO_OFFSET);

	/* Wait until FIFO is empty */
	while (!(xuartps_readl(XUARTPS_SR_OFFSET) & XUARTPS_SR_TXEMPTY))
		cpu_relax();

	/* Enable interrupts */
	xuartps_writel(imr, XUARTPS_IER_OFFSET);

	return;
}
#endif

/** The UART operations structure
 */
static struct uart_ops xuartps_ops = {
	.set_mctrl	= xuartps_set_mctrl,
	.get_mctrl	= xuartps_get_mctrl,
	.enable_ms	= xuartps_enable_ms,

	.start_tx	= xuartps_start_tx,	/* Start transmitting */
	.stop_tx	= xuartps_stop_tx,	/* Stop transmission */
	.stop_rx	= xuartps_stop_rx,	/* Stop reception */
	.tx_empty	= xuartps_tx_empty,	/* Transmitter busy? */
	.break_ctl	= xuartps_break_ctl,	/* Start/stop
						 * transmitting break
						 */
	.set_termios	= xuartps_set_termios,	/* Set termios */
	.startup	= xuartps_startup,	/* App opens xuartps */
	.shutdown	= xuartps_shutdown,	/* App closes xuartps */
	.type		= xuartps_type,		/* Set UART type */
	.verify_port	= xuartps_verify_port,	/* Verification of port
						 * params
						 */
	.request_port	= xuartps_request_port,	/* Claim resources
						 * associated with a
						 * xuartps port
						 */
	.release_port	= xuartps_release_port,	/* Release resources
						 * associated with a
						 * xuartps port
						 */
	.config_port	= xuartps_config_port,	/* Configure when driver
						 * adds a xuartps port
						 */
#ifdef CONFIG_CONSOLE_POLL
	.poll_get_char	= xuartps_poll_get_char,
	.poll_put_char	= xuartps_poll_put_char,
#endif
};

static struct uart_port xuartps_port[2];

/**
 * xuartps_get_port - Configure the port from the platform device resource
 *			info
 *
 * Returns a pointer to a uart_port or NULL for failure
 **/
static struct uart_port *xuartps_get_port(void)
{
	struct uart_port *port;
	int id;

	/* Find the next unused port */
	for (id = 0; id < XUARTPS_NR_PORTS; id++)
		if (xuartps_port[id].mapbase == 0)
			break;

	if (id >= XUARTPS_NR_PORTS)
		return NULL;

	port = &xuartps_port[id];

	/* At this point, we've got an empty uart_port struct, initialize it */
	spin_lock_init(&port->lock);
	port->membase	= NULL;
	port->iobase	= 1; /* mark port in use */
	port->irq	= 0;
	port->type	= PORT_UNKNOWN;
	port->iotype	= UPIO_MEM32;
	port->flags	= UPF_BOOT_AUTOCONF;
	port->ops	= &xuartps_ops;
	port->fifosize	= XUARTPS_FIFO_SIZE;
	port->line	= id;
	port->dev	= NULL;
	return port;
}

/*-----------------------Console driver operations--------------------------*/

#ifdef CONFIG_SERIAL_XILINX_PS_UART_CONSOLE
/**
 * xuartps_console_wait_tx - Wait for the TX to be full
 * @port: Handle to the uart port structure
 *
 **/
static void xuartps_console_wait_tx(struct uart_port *port)
{
	while ((xuartps_readl(XUARTPS_SR_OFFSET) & XUARTPS_SR_TXEMPTY)
				!= XUARTPS_SR_TXEMPTY)
		barrier();
}

/**
 * xuartps_console_putchar - write the character to the FIFO buffer
 * @port: Handle to the uart port structure
 * @ch: Character to be written
 *
 **/
static void xuartps_console_putchar(struct uart_port *port, int ch)
{
	xuartps_console_wait_tx(port);
	xuartps_writel(ch, XUARTPS_FIFO_OFFSET);
}

/**
 * xuartps_console_write - perform write operation
 * @port: Handle to the uart port structure
 * @s: Pointer to character array
 * @count: No of characters
 **/
static void xuartps_console_write(struct console *co, const char *s,
				unsigned int count)
{
	struct uart_port *port = &xuartps_port[co->index];
	unsigned long flags;
	unsigned int imr, ctrl;
	int locked = 1;

	if (oops_in_progress)
		locked = spin_trylock_irqsave(&port->lock, flags);
	else
		spin_lock_irqsave(&port->lock, flags);

	/* save and disable interrupt */
	imr = xuartps_readl(XUARTPS_IMR_OFFSET);
	xuartps_writel(imr, XUARTPS_IDR_OFFSET);

	/*
	 * Make sure that the tx part is enabled. Set the TX enable bit and
	 * clear the TX disable bit to enable the transmitter.
	 */
	ctrl = xuartps_readl(XUARTPS_CR_OFFSET);
	xuartps_writel((ctrl & ~XUARTPS_CR_TX_DIS) | XUARTPS_CR_TX_EN,
		XUARTPS_CR_OFFSET);

	uart_console_write(port, s, count, xuartps_console_putchar);
	xuartps_console_wait_tx(port);

	xuartps_writel(ctrl, XUARTPS_CR_OFFSET);

	/* restore interrupt state, it seems like there may be a h/w bug
	 * in that the interrupt enable register should not need to be
	 * written based on the data sheet
	 */
	xuartps_writel(~imr, XUARTPS_IDR_OFFSET);
	xuartps_writel(imr, XUARTPS_IER_OFFSET);

	if (locked)
		spin_unlock_irqrestore(&port->lock, flags);
}

/**
 * xuartps_console_setup - Initialize the uart to default config
 * @co: Console handle
 * @options: Initial settings of uart
 *
 * Returns 0, -ENODEV if no device
 **/
static int __init xuartps_console_setup(struct console *co, char *options)
{
	struct uart_port *port = &xuartps_port[co->index];
	int baud = 9600;
	int bits = 8;
	int parity = 'n';
	int flow = 'n';

	if (co->index < 0 || co->index >= XUARTPS_NR_PORTS)
		return -EINVAL;

	if (!port->mapbase) {
		pr_debug("console on ttyPS%i not present\n", co->index);
		return -ENODEV;
	}

	if (options)
		uart_parse_options(options, &baud, &parity, &bits, &flow);

	return uart_set_options(port, co, baud, parity, bits, flow);
}

static struct uart_driver xuartps_uart_driver;

static struct console xuartps_console = {
	.name	= XUARTPS_TTY_NAME,
	.write	= xuartps_console_write,
	.device	= uart_console_device,
	.setup	= xuartps_console_setup,
	.flags	= CON_PRINTBUFFER,
	.index	= -1, /* Specified on the cmdline (e.g. console=ttyPS ) */
	.data	= &xuartps_uart_driver,
};

/**
 * xuartps_console_init - Initialization call
 *
 * Returns 0 on success, negative error otherwise
 **/
static int __init xuartps_console_init(void)
{
	register_console(&xuartps_console);
	return 0;
}

console_initcall(xuartps_console_init);

#endif /* CONFIG_SERIAL_XILINX_PS_UART_CONSOLE */

#ifdef CONFIG_PM_SLEEP
/**
 * xuartps_suspend - suspend event
 * @device: Pointer to the device structure
 *
 * Returns 0
 */
static int xuartps_suspend(struct device *device)
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
	uart_suspend_port(&xuartps_uart_driver, port);
	if (console_suspend_enabled && !may_wake) {
		struct xuartps *xuartps = port->private_data;

		clk_disable(xuartps->refclk);
		clk_disable(xuartps->aperclk);
	} else {
		unsigned long flags = 0;

		spin_lock_irqsave(&port->lock, flags);
		/* Empty the receive FIFO 1st before making changes */
		while (!(xuartps_readl(XUARTPS_SR_OFFSET) & XUARTPS_SR_RXEMPTY))
			xuartps_readl(XUARTPS_FIFO_OFFSET);
		/* set RX trigger level to 1 */
		xuartps_writel(1, XUARTPS_RXWM_OFFSET);
		/* disable RX timeout interrups */
		xuartps_writel(XUARTPS_IXR_TOUT, XUARTPS_IDR_OFFSET);
		spin_unlock_irqrestore(&port->lock, flags);
	}

	return 0;
}

/**
 * xuartps_resume - Resume after a previous suspend
 * @device: Pointer to the device structure
 *
 * Returns 0
 */
static int xuartps_resume(struct device *device)
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
		struct xuartps *xuartps = port->private_data;

		clk_enable(xuartps->aperclk);
		clk_enable(xuartps->refclk);

		spin_lock_irqsave(&port->lock, flags);

		/* Set TX/RX Reset */
		xuartps_writel(xuartps_readl(XUARTPS_CR_OFFSET) |
				(XUARTPS_CR_TXRST | XUARTPS_CR_RXRST),
				XUARTPS_CR_OFFSET);
		while (xuartps_readl(XUARTPS_CR_OFFSET) &
				(XUARTPS_CR_TXRST | XUARTPS_CR_RXRST))
			cpu_relax();

		/* restore rx timeout value */
		xuartps_writel(rx_timeout, XUARTPS_RXTOUT_OFFSET);
		/* Enable Tx/Rx */
		ctrl_reg = xuartps_readl(XUARTPS_CR_OFFSET);
		xuartps_writel(
			(ctrl_reg & ~(XUARTPS_CR_TX_DIS | XUARTPS_CR_RX_DIS)) |
			(XUARTPS_CR_TX_EN | XUARTPS_CR_RX_EN),
			XUARTPS_CR_OFFSET);

		spin_unlock_irqrestore(&port->lock, flags);
	} else {
		spin_lock_irqsave(&port->lock, flags);
		/* restore original rx trigger level */
		xuartps_writel(rx_trigger_level, XUARTPS_RXWM_OFFSET);
		/* enable RX timeout interrupt */
		xuartps_writel(XUARTPS_IXR_TOUT, XUARTPS_IER_OFFSET);
		spin_unlock_irqrestore(&port->lock, flags);
	}

	return uart_resume_port(&xuartps_uart_driver, port);
}
#endif /* ! CONFIG_PM_SLEEP */

static SIMPLE_DEV_PM_OPS(xuartps_dev_pm_ops, xuartps_suspend, xuartps_resume);

/** Structure Definitions
 */
static struct uart_driver xuartps_uart_driver = {
	.owner		= THIS_MODULE,		/* Owner */
	.driver_name	= XUARTPS_NAME,		/* Driver name */
	.dev_name	= XUARTPS_TTY_NAME,	/* Node name */
	.major		= XUARTPS_MAJOR,	/* Major number */
	.minor		= XUARTPS_MINOR,	/* Minor number */
	.nr		= XUARTPS_NR_PORTS,	/* Number of UART ports */
#ifdef CONFIG_SERIAL_XILINX_PS_UART_CONSOLE
	.cons		= &xuartps_console,	/* Console */
#endif
};

/* ---------------------------------------------------------------------
 * Platform bus binding
 */
/**
 * xuartps_probe - Platform driver probe
 * @pdev: Pointer to the platform device structure
 *
 * Returns 0 on success, negative error otherwise
 **/
static int xuartps_probe(struct platform_device *pdev)
{
	int rc;
	struct uart_port *port;
	struct resource *res, *res2;
	struct xuartps *xuartps_data;

	xuartps_data = devm_kzalloc(&pdev->dev, sizeof(*xuartps_data),
			GFP_KERNEL);
	if (!xuartps_data)
		return -ENOMEM;

	xuartps_data->aperclk = devm_clk_get(&pdev->dev, "aper_clk");
	if (IS_ERR(xuartps_data->aperclk)) {
		dev_err(&pdev->dev, "aper_clk clock not found.\n");
		return PTR_ERR(xuartps_data->aperclk);
	}
	xuartps_data->refclk = devm_clk_get(&pdev->dev, "ref_clk");
	if (IS_ERR(xuartps_data->refclk)) {
		dev_err(&pdev->dev, "ref_clk clock not found.\n");
		return PTR_ERR(xuartps_data->refclk);
	}

	rc = clk_prepare_enable(xuartps_data->aperclk);
	if (rc) {
		dev_err(&pdev->dev, "Unable to enable APER clock.\n");
		return rc;
	}
	rc = clk_prepare_enable(xuartps_data->refclk);
	if (rc) {
		dev_err(&pdev->dev, "Unable to enable device clock.\n");
		goto err_out_clk_dis_aper;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		rc = -ENODEV;
		goto err_out_clk_disable;
	}

	res2 = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if (!res2) {
		rc = -ENODEV;
		goto err_out_clk_disable;
	}

	xuartps_data->clk_rate_change_nb.notifier_call =
			xuartps_clk_notifier_cb;
	if (clk_notifier_register(xuartps_data->refclk,
				&xuartps_data->clk_rate_change_nb))
		dev_warn(&pdev->dev, "Unable to register clock notifier.\n");

	/* Initialize the port structure */
	port = xuartps_get_port();

	if (!port) {
		dev_err(&pdev->dev, "Cannot get uart_port structure\n");
		rc = -ENODEV;
		goto err_out_notif_unreg;
	} else {
		/* Register the port.
		 * This function also registers this device with the tty layer
		 * and triggers invocation of the config_port() entry point.
		 */
		port->mapbase = res->start;
		port->irq = res2->start;
		port->dev = &pdev->dev;
		port->uartclk = clk_get_rate(xuartps_data->refclk);
		port->private_data = xuartps_data;
		xuartps_data->port = port;
		platform_set_drvdata(pdev, port);
		rc = uart_add_one_port(&xuartps_uart_driver, port);
		if (rc) {
			dev_err(&pdev->dev,
				"uart_add_one_port() failed; err=%i\n", rc);
			goto err_out_notif_unreg;
		}
		return 0;
	}

err_out_notif_unreg:
	clk_notifier_unregister(xuartps_data->refclk,
			&xuartps_data->clk_rate_change_nb);
err_out_clk_disable:
	clk_disable_unprepare(xuartps_data->refclk);
err_out_clk_dis_aper:
	clk_disable_unprepare(xuartps_data->aperclk);

	return rc;
}

/**
 * xuartps_remove - called when the platform driver is unregistered
 * @pdev: Pointer to the platform device structure
 *
 * Returns 0 on success, negative error otherwise
 **/
static int xuartps_remove(struct platform_device *pdev)
{
	struct uart_port *port = platform_get_drvdata(pdev);
	struct xuartps *xuartps_data = port->private_data;
	int rc;

	/* Remove the xuartps port from the serial core */
	clk_notifier_unregister(xuartps_data->refclk,
			&xuartps_data->clk_rate_change_nb);
	rc = uart_remove_one_port(&xuartps_uart_driver, port);
	port->mapbase = 0;
	clk_disable_unprepare(xuartps_data->refclk);
	clk_disable_unprepare(xuartps_data->aperclk);
	return rc;
}

/* Match table for of_platform binding */
static struct of_device_id xuartps_of_match[] = {
	{ .compatible = "xlnx,xuartps", },
	{}
};
MODULE_DEVICE_TABLE(of, xuartps_of_match);

static struct platform_driver xuartps_platform_driver = {
	.probe   = xuartps_probe,		/* Probe method */
	.remove  = xuartps_remove,		/* Detach method */
	.driver  = {
		.owner = THIS_MODULE,
		.name = XUARTPS_NAME,		/* Driver name */
		.of_match_table = xuartps_of_match,
		.pm = &xuartps_dev_pm_ops,
		},
};

/* ---------------------------------------------------------------------
 * Module Init and Exit
 */
/**
 * xuartps_init - Initial driver registration call
 *
 * Returns whether the registration was successful or not
 **/
static int __init xuartps_init(void)
{
	int retval = 0;

	/* Register the xuartps driver with the serial core */
	retval = uart_register_driver(&xuartps_uart_driver);
	if (retval)
		return retval;

	/* Register the platform driver */
	retval = platform_driver_register(&xuartps_platform_driver);
	if (retval)
		uart_unregister_driver(&xuartps_uart_driver);

	return retval;
}

/**
 * xuartps_exit - Driver unregistration call
 **/
static void __exit xuartps_exit(void)
{
	/* The order of unregistration is important. Unregister the
	 * UART driver before the platform driver crashes the system.
	 */

	/* Unregister the platform driver */
	platform_driver_unregister(&xuartps_platform_driver);

	/* Unregister the xuartps driver */
	uart_unregister_driver(&xuartps_uart_driver);
}

module_init(xuartps_init);
module_exit(xuartps_exit);

MODULE_DESCRIPTION("Driver for PS UART");
MODULE_AUTHOR("Xilinx Inc.");
MODULE_LICENSE("GPL");
