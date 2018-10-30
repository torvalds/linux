// SPDX-License-Identifier: GPL-2.0+
/*
 * Cadence UART driver (found in Xilinx Zynq)
 *
 * 2011 - 2014 (C) Xilinx Inc.
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
#include <linux/pm_runtime.h>

#define CDNS_UART_TTY_NAME	"ttyPS"
#define CDNS_UART_NAME		"xuartps"
#define CDNS_UART_MAJOR		0	/* use dynamic node allocation */
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
#define CDNS_UART_RXTOUT	0x1C  /* RX Timeout */
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
#define CDNS_UART_RXBS		0x48  /* RX FIFO byte status register */

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
#define CDNS_UART_RXBS_PARITY    0x00000001 /* Parity error status */
#define CDNS_UART_RXBS_FRAMING   0x00000002 /* Framing error status */
#define CDNS_UART_RXBS_BRK       0x00000004 /* Overrun error status */

/*
 * Mode Register:
 * The mode register (MR) defines the mode of transfer as well as the data
 * format. If this register is modified during transmission or reception,
 * data validity cannot be guaranteed.
 */
#define CDNS_UART_MR_CLKSEL		0x00000001  /* Pre-scalar selection */
#define CDNS_UART_MR_CHMODE_L_LOOP	0x00000200  /* Local loop back mode */
#define CDNS_UART_MR_CHMODE_NORM	0x00000000  /* Normal mode */
#define CDNS_UART_MR_CHMODE_MASK	0x00000300  /* Mask for mode bits */

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

	/*
	 * Do not enable parity error interrupt for the following
	 * reason: When parity error interrupt is enabled, each Rx
	 * parity error always results in 2 events. The first one
	 * being parity error interrupt and the second one with a
	 * proper Rx interrupt with the incoming data.  Disabling
	 * parity error interrupt ensures better handling of parity
	 * error events. With this change, for a parity error case, we
	 * get a Rx interrupt with parity error set in ISR register
	 * and we still handle parity errors in the desired way.
	 */

#define CDNS_UART_RX_IRQS	(CDNS_UART_IXR_FRAMING | \
				 CDNS_UART_IXR_OVERRUN | \
				 CDNS_UART_IXR_RXTRIG |	 \
				 CDNS_UART_IXR_TOUT)

/* Goes in read_status_mask for break detection as the HW doesn't do it*/
#define CDNS_UART_IXR_BRK	0x00002000

#define CDNS_UART_RXBS_SUPPORT BIT(1)
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
#define CDNS_UART_SR_TACTIVE	0x00000800 /* TX state machine active */

/* baud dividers min/max values */
#define CDNS_UART_BDIV_MIN	4
#define CDNS_UART_BDIV_MAX	255
#define CDNS_UART_CD_MAX	65535
#define UART_AUTOSUSPEND_TIMEOUT	3000

/**
 * struct cdns_uart - device data
 * @port:		Pointer to the UART port
 * @uartclk:		Reference clock
 * @pclk:		APB clock
 * @cdns_uart_driver:	Pointer to UART driver
 * @baud:		Current baud rate
 * @id:			Port ID
 * @clk_rate_change_nb:	Notifier block for clock changes
 * @quirks:		Flags for RXBS support.
 */
struct cdns_uart {
	struct uart_port	*port;
	struct clk		*uartclk;
	struct clk		*pclk;
	struct uart_driver	*cdns_uart_driver;
	unsigned int		baud;
	int			id;
	struct notifier_block	clk_rate_change_nb;
	u32			quirks;
};
struct cdns_platform_data {
	u32 quirks;
};
#define to_cdns_uart(_nb) container_of(_nb, struct cdns_uart, \
		clk_rate_change_nb);

/**
 * cdns_uart_handle_rx - Handle the received bytes along with Rx errors.
 * @dev_id: Id of the UART port
 * @isrstatus: The interrupt status register value as read
 * Return: None
 */
static void cdns_uart_handle_rx(void *dev_id, unsigned int isrstatus)
{
	struct uart_port *port = (struct uart_port *)dev_id;
	struct cdns_uart *cdns_uart = port->private_data;
	unsigned int data;
	unsigned int rxbs_status = 0;
	unsigned int status_mask;
	unsigned int framerrprocessed = 0;
	char status = TTY_NORMAL;
	bool is_rxbs_support;

	is_rxbs_support = cdns_uart->quirks & CDNS_UART_RXBS_SUPPORT;

	while ((readl(port->membase + CDNS_UART_SR) &
		CDNS_UART_SR_RXEMPTY) != CDNS_UART_SR_RXEMPTY) {
		if (is_rxbs_support)
			rxbs_status = readl(port->membase + CDNS_UART_RXBS);
		data = readl(port->membase + CDNS_UART_FIFO);
		port->icount.rx++;
		/*
		 * There is no hardware break detection in Zynq, so we interpret
		 * framing error with all-zeros data as a break sequence.
		 * Most of the time, there's another non-zero byte at the
		 * end of the sequence.
		 */
		if (!is_rxbs_support && (isrstatus & CDNS_UART_IXR_FRAMING)) {
			if (!data) {
				port->read_status_mask |= CDNS_UART_IXR_BRK;
				framerrprocessed = 1;
				continue;
			}
		}
		if (is_rxbs_support && (rxbs_status & CDNS_UART_RXBS_BRK)) {
			port->icount.brk++;
			status = TTY_BREAK;
			if (uart_handle_break(port))
				continue;
		}

		isrstatus &= port->read_status_mask;
		isrstatus &= ~port->ignore_status_mask;
		status_mask = port->read_status_mask;
		status_mask &= ~port->ignore_status_mask;

		if (data &&
		    (port->read_status_mask & CDNS_UART_IXR_BRK)) {
			port->read_status_mask &= ~CDNS_UART_IXR_BRK;
			port->icount.brk++;
			if (uart_handle_break(port))
				continue;
		}

		if (uart_handle_sysrq_char(port, data))
			continue;

		if (is_rxbs_support) {
			if ((rxbs_status & CDNS_UART_RXBS_PARITY)
			    && (status_mask & CDNS_UART_IXR_PARITY)) {
				port->icount.parity++;
				status = TTY_PARITY;
			}
			if ((rxbs_status & CDNS_UART_RXBS_FRAMING)
			    && (status_mask & CDNS_UART_IXR_PARITY)) {
				port->icount.frame++;
				status = TTY_FRAME;
			}
		} else {
			if (isrstatus & CDNS_UART_IXR_PARITY) {
				port->icount.parity++;
				status = TTY_PARITY;
			}
			if ((isrstatus & CDNS_UART_IXR_FRAMING) &&
			    !framerrprocessed) {
				port->icount.frame++;
				status = TTY_FRAME;
			}
		}
		if (isrstatus & CDNS_UART_IXR_OVERRUN) {
			port->icount.overrun++;
			tty_insert_flip_char(&port->state->port, 0,
					     TTY_OVERRUN);
		}
		tty_insert_flip_char(&port->state->port, data, status);
		isrstatus = 0;
	}
	spin_unlock(&port->lock);
	tty_flip_buffer_push(&port->state->port);
	spin_lock(&port->lock);
}

/**
 * cdns_uart_handle_tx - Handle the bytes to be Txed.
 * @dev_id: Id of the UART port
 * Return: None
 */
static void cdns_uart_handle_tx(void *dev_id)
{
	struct uart_port *port = (struct uart_port *)dev_id;
	unsigned int numbytes;

	if (uart_circ_empty(&port->state->xmit)) {
		writel(CDNS_UART_IXR_TXEMPTY, port->membase + CDNS_UART_IDR);
	} else {
		numbytes = port->fifosize;
		while (numbytes && !uart_circ_empty(&port->state->xmit) &&
		       !(readl(port->membase + CDNS_UART_SR) & CDNS_UART_SR_TXFULL)) {
			/*
			 * Get the data from the UART circular buffer
			 * and write it to the cdns_uart's TX_FIFO
			 * register.
			 */
			writel(
				port->state->xmit.buf[port->state->xmit.
				tail], port->membase + CDNS_UART_FIFO);

			port->icount.tx++;

			/*
			 * Adjust the tail of the UART buffer and wrap
			 * the buffer if it reaches limit.
			 */
			port->state->xmit.tail =
				(port->state->xmit.tail + 1) &
					(UART_XMIT_SIZE - 1);

			numbytes--;
		}

		if (uart_circ_chars_pending(
				&port->state->xmit) < WAKEUP_CHARS)
			uart_write_wakeup(port);
	}
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
	unsigned int isrstatus;

	spin_lock(&port->lock);

	/* Read the interrupt status register to determine which
	 * interrupt(s) is/are active and clear them.
	 */
	isrstatus = readl(port->membase + CDNS_UART_ISR);
	writel(isrstatus, port->membase + CDNS_UART_ISR);

	if (isrstatus & CDNS_UART_IXR_TXEMPTY) {
		cdns_uart_handle_tx(dev_id);
		isrstatus &= ~CDNS_UART_IXR_TXEMPTY;
	}
	if (isrstatus & CDNS_UART_IXR_MASK)
		cdns_uart_handle_rx(dev_id, isrstatus);

	spin_unlock(&port->lock);
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

	while (readl(port->membase + CDNS_UART_CR) &
		(CDNS_UART_CR_TXRST | CDNS_UART_CR_RXRST))
		cpu_relax();

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
	struct cdns_uart *cdns_uart = port->private_data;
	bool is_brk_support;
	int ret;
	unsigned long flags;
	unsigned int status = 0;

	is_brk_support = cdns_uart->quirks & CDNS_UART_RXBS_SUPPORT;

	spin_lock_irqsave(&port->lock, flags);

	/* Disable the TX and RX */
	writel(CDNS_UART_CR_TX_DIS | CDNS_UART_CR_RX_DIS,
			port->membase + CDNS_UART_CR);

	/* Set the Control Register with TX/RX Enable, TX/RX Reset,
	 * no break chars.
	 */
	writel(CDNS_UART_CR_TXRST | CDNS_UART_CR_RXRST,
			port->membase + CDNS_UART_CR);

	while (readl(port->membase + CDNS_UART_CR) &
		(CDNS_UART_CR_TXRST | CDNS_UART_CR_RXRST))
		cpu_relax();

	/*
	 * Clear the RX disable bit and then set the RX enable bit to enable
	 * the receiver.
	 */
	status = readl(port->membase + CDNS_UART_CR);
	status &= ~CDNS_UART_CR_RX_DIS;
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
	if (is_brk_support)
		writel(CDNS_UART_RX_IRQS | CDNS_UART_IXR_BRK,
					port->membase + CDNS_UART_IER);
	else
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
	u32 mode_reg;

	val = readl(port->membase + CDNS_UART_MODEMCR);
	mode_reg = readl(port->membase + CDNS_UART_MR);

	val &= ~(CDNS_UART_MODEMCR_RTS | CDNS_UART_MODEMCR_DTR |
		 CDNS_UART_MODEMCR_FCM);
	mode_reg &= ~CDNS_UART_MR_CHMODE_MASK;

	if (mctrl & TIOCM_RTS || mctrl & TIOCM_DTR)
		val |= CDNS_UART_MODEMCR_FCM;
	if (mctrl & TIOCM_LOOP)
		mode_reg |= CDNS_UART_MR_CHMODE_L_LOOP;
	else
		mode_reg |= CDNS_UART_MR_CHMODE_NORM;

	writel(val, port->membase + CDNS_UART_MODEMCR);
	writel(mode_reg, port->membase + CDNS_UART_MR);
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
	switch (state) {
	case UART_PM_STATE_OFF:
		pm_runtime_mark_last_busy(port->dev);
		pm_runtime_put_autosuspend(port->dev);
		break;
	default:
		pm_runtime_get_sync(port->dev);
		break;
	}
}

static const struct uart_ops cdns_uart_ops = {
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

#ifdef CONFIG_SERIAL_XILINX_PS_UART_CONSOLE
/**
 * cdns_uart_console_putchar - write the character to the FIFO buffer
 * @port: Handle to the uart port structure
 * @ch: Character to be written
 */
static void cdns_uart_console_putchar(struct uart_port *port, int ch)
{
	while (readl(port->membase + CDNS_UART_SR) & CDNS_UART_SR_TXFULL)
		cpu_relax();
	writel(ch, port->membase + CDNS_UART_FIFO);
}

static void cdns_early_write(struct console *con, const char *s,
				    unsigned n)
{
	struct earlycon_device *dev = con->data;

	uart_console_write(&dev->port, s, n, cdns_uart_console_putchar);
}

static int __init cdns_early_console_setup(struct earlycon_device *device,
					   const char *opt)
{
	struct uart_port *port = &device->port;

	if (!port->membase)
		return -ENODEV;

	/* initialise control register */
	writel(CDNS_UART_CR_TX_EN|CDNS_UART_CR_TXRST|CDNS_UART_CR_RXRST,
	       port->membase + CDNS_UART_CR);

	/* only set baud if specified on command line - otherwise
	 * assume it has been initialized by a boot loader.
	 */
	if (port->uartclk && device->baud) {
		u32 cd = 0, bdiv = 0;
		u32 mr;
		int div8;

		cdns_uart_calc_baud_divs(port->uartclk, device->baud,
					 &bdiv, &cd, &div8);
		mr = CDNS_UART_MR_PARITY_NONE;
		if (div8)
			mr |= CDNS_UART_MR_CLKSEL;

		writel(mr,   port->membase + CDNS_UART_MR);
		writel(cd,   port->membase + CDNS_UART_BAUDGEN);
		writel(bdiv, port->membase + CDNS_UART_BAUDDIV);
	}

	device->con->write = cdns_early_write;

	return 0;
}
OF_EARLYCON_DECLARE(cdns, "xlnx,xuartps", cdns_early_console_setup);
OF_EARLYCON_DECLARE(cdns, "cdns,uart-r1p8", cdns_early_console_setup);
OF_EARLYCON_DECLARE(cdns, "cdns,uart-r1p12", cdns_early_console_setup);
OF_EARLYCON_DECLARE(cdns, "xlnx,zynqmp-uart", cdns_early_console_setup);


/* Static pointer to console port */
static struct uart_port *console_port;

/**
 * cdns_uart_console_write - perform write operation
 * @co: Console handle
 * @s: Pointer to character array
 * @count: No of characters
 */
static void cdns_uart_console_write(struct console *co, const char *s,
				unsigned int count)
{
	struct uart_port *port = console_port;
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
	while ((readl(port->membase + CDNS_UART_SR) &
			(CDNS_UART_SR_TXEMPTY | CDNS_UART_SR_TACTIVE)) !=
			CDNS_UART_SR_TXEMPTY)
		cpu_relax();

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
static int cdns_uart_console_setup(struct console *co, char *options)
{
	struct uart_port *port = console_port;

	int baud = 9600;
	int bits = 8;
	int parity = 'n';
	int flow = 'n';

	if (!port->membase) {
		pr_debug("console on " CDNS_UART_TTY_NAME "%i not present\n",
			 co->index);
		return -ENODEV;
	}

	if (options)
		uart_parse_options(options, &baud, &parity, &bits, &flow);

	return uart_set_options(port, co, baud, parity, bits, flow);
}
#endif /* CONFIG_SERIAL_XILINX_PS_UART_CONSOLE */

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
	struct cdns_uart *cdns_uart = port->private_data;
	int may_wake;

	may_wake = device_may_wakeup(device);

	if (console_suspend_enabled && may_wake) {
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

	/*
	 * Call the API provided in serial_core.c file which handles
	 * the suspend.
	 */
	return uart_suspend_port(cdns_uart->cdns_uart_driver, port);
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
	struct cdns_uart *cdns_uart = port->private_data;
	unsigned long flags = 0;
	u32 ctrl_reg;
	int may_wake;

	may_wake = device_may_wakeup(device);

	if (console_suspend_enabled && !may_wake) {
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

		clk_disable(cdns_uart->uartclk);
		clk_disable(cdns_uart->pclk);
		spin_unlock_irqrestore(&port->lock, flags);
	} else {
		spin_lock_irqsave(&port->lock, flags);
		/* restore original rx trigger level */
		writel(rx_trigger_level, port->membase + CDNS_UART_RXWM);
		/* enable RX timeout interrupt */
		writel(CDNS_UART_IXR_TOUT, port->membase + CDNS_UART_IER);
		spin_unlock_irqrestore(&port->lock, flags);
	}

	return uart_resume_port(cdns_uart->cdns_uart_driver, port);
}
#endif /* ! CONFIG_PM_SLEEP */
static int __maybe_unused cdns_runtime_suspend(struct device *dev)
{
	struct uart_port *port = dev_get_drvdata(dev);
	struct cdns_uart *cdns_uart = port->private_data;

	clk_disable(cdns_uart->uartclk);
	clk_disable(cdns_uart->pclk);
	return 0;
};

static int __maybe_unused cdns_runtime_resume(struct device *dev)
{
	struct uart_port *port = dev_get_drvdata(dev);
	struct cdns_uart *cdns_uart = port->private_data;

	clk_enable(cdns_uart->pclk);
	clk_enable(cdns_uart->uartclk);
	return 0;
};

static const struct dev_pm_ops cdns_uart_dev_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(cdns_uart_suspend, cdns_uart_resume)
	SET_RUNTIME_PM_OPS(cdns_runtime_suspend,
			   cdns_runtime_resume, NULL)
};

static const struct cdns_platform_data zynqmp_uart_def = {
				.quirks = CDNS_UART_RXBS_SUPPORT, };

/* Match table for of_platform binding */
static const struct of_device_id cdns_uart_of_match[] = {
	{ .compatible = "xlnx,xuartps", },
	{ .compatible = "cdns,uart-r1p8", },
	{ .compatible = "cdns,uart-r1p12", .data = &zynqmp_uart_def },
	{ .compatible = "xlnx,zynqmp-uart", .data = &zynqmp_uart_def },
	{}
};
MODULE_DEVICE_TABLE(of, cdns_uart_of_match);

/*
 * Maximum number of instances without alias IDs but if there is alias
 * which target "< MAX_UART_INSTANCES" range this ID can't be used.
 */
#define MAX_UART_INSTANCES	32

/* Stores static aliases list */
static DECLARE_BITMAP(alias_bitmap, MAX_UART_INSTANCES);
static int alias_bitmap_initialized;

/* Stores actual bitmap of allocated IDs with alias IDs together */
static DECLARE_BITMAP(bitmap, MAX_UART_INSTANCES);
/* Protect bitmap operations to have unique IDs */
static DEFINE_MUTEX(bitmap_lock);

static int cdns_get_id(struct platform_device *pdev)
{
	int id, ret;

	mutex_lock(&bitmap_lock);

	/* Alias list is stable that's why get alias bitmap only once */
	if (!alias_bitmap_initialized) {
		ret = of_alias_get_alias_list(cdns_uart_of_match, "serial",
					      alias_bitmap, MAX_UART_INSTANCES);
		if (ret && ret != -EOVERFLOW) {
			mutex_unlock(&bitmap_lock);
			return ret;
		}

		alias_bitmap_initialized++;
	}

	/* Make sure that alias ID is not taken by instance without alias */
	bitmap_or(bitmap, bitmap, alias_bitmap, MAX_UART_INSTANCES);

	dev_dbg(&pdev->dev, "Alias bitmap: %*pb\n",
		MAX_UART_INSTANCES, bitmap);

	/* Look for a serialN alias */
	id = of_alias_get_id(pdev->dev.of_node, "serial");
	if (id < 0) {
		dev_warn(&pdev->dev,
			 "No serial alias passed. Using the first free id\n");

		/*
		 * Start with id 0 and check if there is no serial0 alias
		 * which points to device which is compatible with this driver.
		 * If alias exists then try next free position.
		 */
		id = 0;

		for (;;) {
			dev_info(&pdev->dev, "Checking id %d\n", id);
			id = find_next_zero_bit(bitmap, MAX_UART_INSTANCES, id);

			/* No free empty instance */
			if (id == MAX_UART_INSTANCES) {
				dev_err(&pdev->dev, "No free ID\n");
				mutex_unlock(&bitmap_lock);
				return -EINVAL;
			}

			dev_dbg(&pdev->dev, "The empty id is %d\n", id);
			/* Check if ID is empty */
			if (!test_and_set_bit(id, bitmap)) {
				/* Break the loop if bit is taken */
				dev_dbg(&pdev->dev,
					"Selected ID %d allocation passed\n",
					id);
				break;
			}
			dev_dbg(&pdev->dev,
				"Selected ID %d allocation failed\n", id);
			/* if taking bit fails then try next one */
			id++;
		}
	}

	mutex_unlock(&bitmap_lock);

	return id;
}

/**
 * cdns_uart_probe - Platform driver probe
 * @pdev: Pointer to the platform device structure
 *
 * Return: 0 on success, negative errno otherwise
 */
static int cdns_uart_probe(struct platform_device *pdev)
{
	int rc, irq;
	struct uart_port *port;
	struct resource *res;
	struct cdns_uart *cdns_uart_data;
	const struct of_device_id *match;
	struct uart_driver *cdns_uart_uart_driver;
	char *driver_name;
#ifdef CONFIG_SERIAL_XILINX_PS_UART_CONSOLE
	struct console *cdns_uart_console;
#endif

	cdns_uart_data = devm_kzalloc(&pdev->dev, sizeof(*cdns_uart_data),
			GFP_KERNEL);
	if (!cdns_uart_data)
		return -ENOMEM;
	port = devm_kzalloc(&pdev->dev, sizeof(*port), GFP_KERNEL);
	if (!port)
		return -ENOMEM;

	cdns_uart_uart_driver = devm_kzalloc(&pdev->dev,
					     sizeof(*cdns_uart_uart_driver),
					     GFP_KERNEL);
	if (!cdns_uart_uart_driver)
		return -ENOMEM;

	cdns_uart_data->id = cdns_get_id(pdev);
	if (cdns_uart_data->id < 0)
		return cdns_uart_data->id;

	/* There is a need to use unique driver name */
	driver_name = devm_kasprintf(&pdev->dev, GFP_KERNEL, "%s%d",
				     CDNS_UART_NAME, cdns_uart_data->id);
	if (!driver_name) {
		rc = -ENOMEM;
		goto err_out_id;
	}

	cdns_uart_uart_driver->owner = THIS_MODULE;
	cdns_uart_uart_driver->driver_name = driver_name;
	cdns_uart_uart_driver->dev_name	= CDNS_UART_TTY_NAME;
	cdns_uart_uart_driver->major = CDNS_UART_MAJOR;
	cdns_uart_uart_driver->minor = cdns_uart_data->id;
	cdns_uart_uart_driver->nr = 1;

#ifdef CONFIG_SERIAL_XILINX_PS_UART_CONSOLE
	cdns_uart_console = devm_kzalloc(&pdev->dev, sizeof(*cdns_uart_console),
					 GFP_KERNEL);
	if (!cdns_uart_console)
		return -ENOMEM;

	strncpy(cdns_uart_console->name, CDNS_UART_TTY_NAME,
		sizeof(cdns_uart_console->name));
	cdns_uart_console->index = cdns_uart_data->id;
	cdns_uart_console->write = cdns_uart_console_write;
	cdns_uart_console->device = uart_console_device;
	cdns_uart_console->setup = cdns_uart_console_setup;
	cdns_uart_console->flags = CON_PRINTBUFFER;
	cdns_uart_console->data = cdns_uart_uart_driver;
	cdns_uart_uart_driver->cons = cdns_uart_console;
#endif

	rc = uart_register_driver(cdns_uart_uart_driver);
	if (rc < 0) {
		dev_err(&pdev->dev, "Failed to register driver\n");
		goto err_out_id;
	}

	cdns_uart_data->cdns_uart_driver = cdns_uart_uart_driver;

	/*
	 * Setting up proper name_base needs to be done after uart
	 * registration because tty_driver structure is not filled.
	 * name_base is 0 by default.
	 */
	cdns_uart_uart_driver->tty_driver->name_base = cdns_uart_data->id;

	match = of_match_node(cdns_uart_of_match, pdev->dev.of_node);
	if (match && match->data) {
		const struct cdns_platform_data *data = match->data;

		cdns_uart_data->quirks = data->quirks;
	}

	cdns_uart_data->pclk = devm_clk_get(&pdev->dev, "pclk");
	if (IS_ERR(cdns_uart_data->pclk)) {
		cdns_uart_data->pclk = devm_clk_get(&pdev->dev, "aper_clk");
		if (!IS_ERR(cdns_uart_data->pclk))
			dev_err(&pdev->dev, "clock name 'aper_clk' is deprecated.\n");
	}
	if (IS_ERR(cdns_uart_data->pclk)) {
		dev_err(&pdev->dev, "pclk clock not found.\n");
		rc = PTR_ERR(cdns_uart_data->pclk);
		goto err_out_unregister_driver;
	}

	cdns_uart_data->uartclk = devm_clk_get(&pdev->dev, "uart_clk");
	if (IS_ERR(cdns_uart_data->uartclk)) {
		cdns_uart_data->uartclk = devm_clk_get(&pdev->dev, "ref_clk");
		if (!IS_ERR(cdns_uart_data->uartclk))
			dev_err(&pdev->dev, "clock name 'ref_clk' is deprecated.\n");
	}
	if (IS_ERR(cdns_uart_data->uartclk)) {
		dev_err(&pdev->dev, "uart_clk clock not found.\n");
		rc = PTR_ERR(cdns_uart_data->uartclk);
		goto err_out_unregister_driver;
	}

	rc = clk_prepare_enable(cdns_uart_data->pclk);
	if (rc) {
		dev_err(&pdev->dev, "Unable to enable pclk clock.\n");
		goto err_out_unregister_driver;
	}
	rc = clk_prepare_enable(cdns_uart_data->uartclk);
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

	/* At this point, we've got an empty uart_port struct, initialize it */
	spin_lock_init(&port->lock);
	port->type	= PORT_UNKNOWN;
	port->iotype	= UPIO_MEM32;
	port->flags	= UPF_BOOT_AUTOCONF;
	port->ops	= &cdns_uart_ops;
	port->fifosize	= CDNS_UART_FIFO_SIZE;

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

	pm_runtime_use_autosuspend(&pdev->dev);
	pm_runtime_set_autosuspend_delay(&pdev->dev, UART_AUTOSUSPEND_TIMEOUT);
	pm_runtime_set_active(&pdev->dev);
	pm_runtime_enable(&pdev->dev);

#ifdef CONFIG_SERIAL_XILINX_PS_UART_CONSOLE
	/*
	 * If console hasn't been found yet try to assign this port
	 * because it is required to be assigned for console setup function.
	 * If register_console() don't assign value, then console_port pointer
	 * is cleanup.
	 */
	if (!console_port)
		console_port = port;
#endif

	rc = uart_add_one_port(cdns_uart_uart_driver, port);
	if (rc) {
		dev_err(&pdev->dev,
			"uart_add_one_port() failed; err=%i\n", rc);
		goto err_out_pm_disable;
	}

#ifdef CONFIG_SERIAL_XILINX_PS_UART_CONSOLE
	/* This is not port which is used for console that's why clean it up */
	if (console_port == port &&
	    !(cdns_uart_uart_driver->cons->flags & CON_ENABLED))
		console_port = NULL;
#endif

	return 0;

err_out_pm_disable:
	pm_runtime_disable(&pdev->dev);
	pm_runtime_set_suspended(&pdev->dev);
	pm_runtime_dont_use_autosuspend(&pdev->dev);
#ifdef CONFIG_COMMON_CLK
	clk_notifier_unregister(cdns_uart_data->uartclk,
			&cdns_uart_data->clk_rate_change_nb);
#endif
err_out_clk_disable:
	clk_disable_unprepare(cdns_uart_data->uartclk);
err_out_clk_dis_pclk:
	clk_disable_unprepare(cdns_uart_data->pclk);
err_out_unregister_driver:
	uart_unregister_driver(cdns_uart_data->cdns_uart_driver);
err_out_id:
	mutex_lock(&bitmap_lock);
	if (cdns_uart_data->id < MAX_UART_INSTANCES)
		clear_bit(cdns_uart_data->id, bitmap);
	mutex_unlock(&bitmap_lock);
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
	rc = uart_remove_one_port(cdns_uart_data->cdns_uart_driver, port);
	port->mapbase = 0;
	mutex_lock(&bitmap_lock);
	if (cdns_uart_data->id < MAX_UART_INSTANCES)
		clear_bit(cdns_uart_data->id, bitmap);
	mutex_unlock(&bitmap_lock);
	clk_disable_unprepare(cdns_uart_data->uartclk);
	clk_disable_unprepare(cdns_uart_data->pclk);
	pm_runtime_disable(&pdev->dev);
	pm_runtime_set_suspended(&pdev->dev);
	pm_runtime_dont_use_autosuspend(&pdev->dev);

#ifdef CONFIG_SERIAL_XILINX_PS_UART_CONSOLE
	if (console_port == port)
		console_port = NULL;
#endif

	uart_unregister_driver(cdns_uart_data->cdns_uart_driver);
	return rc;
}

static struct platform_driver cdns_uart_platform_driver = {
	.probe   = cdns_uart_probe,
	.remove  = cdns_uart_remove,
	.driver  = {
		.name = CDNS_UART_NAME,
		.of_match_table = cdns_uart_of_match,
		.pm = &cdns_uart_dev_pm_ops,
		.suppress_bind_attrs = IS_BUILTIN(CONFIG_SERIAL_XILINX_PS_UART),
		},
};

static int __init cdns_uart_init(void)
{
	/* Register the platform driver */
	return platform_driver_register(&cdns_uart_platform_driver);
}

static void __exit cdns_uart_exit(void)
{
	/* Unregister the platform driver */
	platform_driver_unregister(&cdns_uart_platform_driver);
}

arch_initcall(cdns_uart_init);
module_exit(cdns_uart_exit);

MODULE_DESCRIPTION("Driver for Cadence UART");
MODULE_AUTHOR("Xilinx Inc.");
MODULE_LICENSE("GPL");
