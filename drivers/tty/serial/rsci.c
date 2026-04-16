// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2025 Renesas Electronics Corp.
 */

#include <linux/bitfield.h>
#include <linux/bitops.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/serial_core.h>
#include <linux/serial_sci.h>
#include <linux/tty_flip.h>

#include "serial_mctrl_gpio.h"
#include "rsci.h"

MODULE_IMPORT_NS("SH_SCI");

/* RSCI registers */
#define RDR	0x00
#define TDR	0x04
#define CCR0	0x08
#define CCR1	0x0C
#define CCR2	0x10
#define CCR3	0x14
#define CCR4	0x18
#define FCR	0x24
#define CSR	0x48
#define FRSR	0x50
#define FTSR	0x54
#define CFCLR	0x68
#define FFCLR	0x70

/* RDR (Receive Data Register) */
#define RDR_FFER		BIT(12) /* FIFO Framing Error */
#define RDR_FPER		BIT(11) /* FIFO Parity Error */
#define RDR_RDAT_MSK		GENMASK(8, 0)

/* CCR0 (Common Control Register 0) */
#define CCR0_SSE		BIT(24)	/* SSn# Pin Function Enable */
#define CCR0_TEIE		BIT(21)	/* Transmit End Interrupt Enable */
#define CCR0_TIE		BIT(20)	/* Transmit Interrupt Enable */
#define CCR0_RIE		BIT(16)	/* Receive Interrupt Enable */
#define CCR0_IDSEL		BIT(10)	/* ID Frame Select */
#define CCR0_DCME		BIT(9)	/* Data Compare Match Enable */
#define CCR0_MPIE		BIT(8)	/* Multiprocessor Interrupt Enable */
#define CCR0_TE			BIT(4)	/* Transmit Enable */
#define CCR0_RE			BIT(0)	/* Receive Enable */

/* CCR1 (Common Control Register 1) */
#define CCR1_NFEN		BIT(28)	/* Digital Noise Filter Function */
#define CCR1_SHARPS		BIT(20)	/* Half -duplex Communication Select */
#define CCR1_SPLP		BIT(16)	/* Loopback Control */
#define CCR1_RINV		BIT(13)	/* RxD invert */
#define CCR1_TINV		BIT(12)	/* TxD invert */
#define CCR1_PM			BIT(9)	/* Parity Mode */
#define CCR1_PE			BIT(8)	/* Parity Enable */
#define CCR1_SPB2IO		BIT(5)	/* Serial Port Break I/O */
#define CCR1_SPB2DT		BIT(4)	/* Serial Port Break Data Select */
#define CCR1_CTSPEN		BIT(1)	/* CTS External Pin Enable */
#define CCR1_CTSE		BIT(0)	/* CTS Enable */

/* CCR2 (Common Control Register 2) */
#define CCR2_INIT			0xFF000004
#define CCR2_CKS_TCLK			(0)	/* TCLK clock */
#define CCR2_CKS_TCLK_DIV4		BIT(20)	/* TCLK/4 clock */
#define CCR2_CKS_TCLK_DIV16		BIT(21)	/* TCLK16 clock */
#define CCR2_CKS_TCLK_DIV64		(BIT(21) | BIT(20)) /* TCLK/64 clock */
#define CCR2_BRME			BIT(16)	/* Bitrate Modulation Enable */
#define CCR2_ABCSE			BIT(6)	/* Asynchronous Mode Extended Base Clock Select */
#define CCR2_ABCS			BIT(5)	/* Asynchronous Mode Base Clock Select */
#define CCR2_BGDM			BIT(4)	/* Baud Rate Generator Double-Speed Mode Select */

/* CCR3 (Common Control Register 3) */
#define CCR3_INIT			0x1203
#define CCR3_BLK			BIT(29)	/* Block Transfer Mode */
#define CCR3_GM				BIT(28)	/* GSM Mode */
#define CCR3_CKE1			BIT(25)	/* Clock Enable 1 */
#define CCR3_CKE0			BIT(24)	/* Clock Enable 0 */
#define CCR3_DEN			BIT(21)	/* Driver Enabled */
#define CCR3_FM				BIT(20)	/* FIFO Mode Select */
#define CCR3_MP				BIT(19)	/* Multi-Processor Mode */
#define CCR3_MOD_ASYNC			0	/* Asynchronous mode (Multi-processor mode) */
#define CCR3_MOD_IRDA			BIT(16)	/* Smart card interface mode */
#define CCR3_MOD_CLK_SYNC		BIT(17)	/* Clock synchronous mode */
#define CCR3_MOD_SPI			(BIT(17) | BIT(16)) /* Simple SPI mode */
#define CCR3_MOD_I2C			BIT(18)	/* Simple I2C mode */
#define CCR3_RXDESEL			BIT(15)	/* Asynchronous Start Bit Edge Detection Select */
#define CCR3_STP			BIT(14)	/* Stop bit Length */
#define CCR3_SINV			BIT(13)	/* Transmitted/Received Data Invert */
#define CCR3_LSBF			BIT(12)	/* LSB First select */
#define CCR3_CHR1			BIT(9)	/* Character Length */
#define CCR3_CHR0			BIT(8)	/* Character Length */
#define CCR3_BPEN			BIT(7)	/* Synchronizer Bypass Enable */
#define CCR3_CPOL			BIT(1)	/* Clock Polarity Select */
#define CCR3_CPHA			BIT(0)	/* Clock Phase Select */

/* FCR (FIFO Control Register) */
#define FCR_RFRST		BIT(23)	/* Receive FIFO Data Register Reset */
#define FCR_TFRST		BIT(15)	/* Transmit FIFO Data Register Reset */
#define FCR_DRES		BIT(0)	/* Incoming Data Ready Error Select */
#define FCR_RTRG4_0		GENMASK(20, 16)
#define FCR_TTRG		GENMASK(12, 8)

/* CSR (Common Status Register) */
#define CSR_RDRF		BIT(31)	/* Receive Data Full */
#define CSR_TEND		BIT(30)	/* Transmit End Flag */
#define CSR_TDRE		BIT(29)	/* Transmit Data Empty */
#define CSR_FER			BIT(28)	/* Framing Error */
#define CSR_PER			BIT(27)	/* Parity Error */
#define CSR_MFF			BIT(26)	/* Mode Fault Error */
#define CSR_ORER		BIT(24)	/* Overrun Error */
#define CSR_DFER		BIT(18)	/* Data Compare Match Framing Error */
#define CSR_DPER		BIT(17)	/* Data Compare Match Parity Error */
#define CSR_DCMF		BIT(16)	/* Data Compare Match */
#define CSR_RXDMON		BIT(15)	/* Serial Input Data Monitor */
#define CSR_ERS			BIT(4)	/* Error Signal Status */

#define SCxSR_ERRORS(port)	(to_sci_port(port)->params->error_mask)
#define SCxSR_ERROR_CLEAR(port)	(to_sci_port(port)->params->error_clear)

#define RSCI_DEFAULT_ERROR_MASK	(CSR_PER | CSR_FER)

#define RSCI_RDxF_CLEAR		(CFCLR_RDRFC)
#define RSCI_ERROR_CLEAR	(CFCLR_PERC | CFCLR_FERC)
#define RSCI_TDxE_CLEAR		(CFCLR_TDREC)
#define RSCI_BREAK_CLEAR	(CFCLR_PERC | CFCLR_FERC | CFCLR_ORERC)

/* FRSR (FIFO Receive Status Register) */
#define FRSR_R5_0		GENMASK(13, 8)	/* Receive FIFO Data Count */
#define FRSR_DR			BIT(0)	/* Receive Data Ready */

/* CFCLR (Common Flag CLear Register) */
#define CFCLR_RDRFC		BIT(31)	/* RDRF Clear */
#define CFCLR_TDREC		BIT(29)	/* TDRE Clear */
#define CFCLR_FERC		BIT(28)	/* FER Clear */
#define CFCLR_PERC		BIT(27)	/* PER Clear */
#define CFCLR_MFFC		BIT(26)	/* MFF Clear */
#define CFCLR_ORERC		BIT(24)	/* ORER Clear */
#define CFCLR_DFERC		BIT(18)	/* DFER Clear */
#define CFCLR_DPERC		BIT(17)	/* DPER Clear */
#define CFCLR_DCMFC		BIT(16)	/* DCMF Clear */
#define CFCLR_ERSC		BIT(4)	/* ERS Clear */
#define CFCLR_CLRFLAG		(CFCLR_RDRFC | CFCLR_FERC | CFCLR_PERC | \
				 CFCLR_MFFC | CFCLR_ORERC | CFCLR_DFERC | \
				 CFCLR_DPERC | CFCLR_DCMFC | CFCLR_ERSC)

/* FFCLR (FIFO Flag CLear Register) */
#define FFCLR_DRC		BIT(0)	/* DR Clear */

static u32 rsci_serial_in(struct uart_port *p, int offset)
{
	return readl(p->membase + offset);
}

static void rsci_serial_out(struct uart_port *p, int offset, int value)
{
	writel(value, p->membase + offset);
}

static void rsci_clear_DRxC(struct uart_port *port)
{
	rsci_serial_out(port, CFCLR, CFCLR_RDRFC);
	rsci_serial_out(port, FFCLR, FFCLR_DRC);
}


static void rsci_start_rx(struct uart_port *port)
{
	unsigned int ctrl;

	ctrl = rsci_serial_in(port, CCR0);
	ctrl |= CCR0_RIE;
	rsci_serial_out(port, CCR0, ctrl);
}

static void rsci_enable_ms(struct uart_port *port)
{
	mctrl_gpio_enable_ms(to_sci_port(port)->gpios);
}

static void rsci_init_pins(struct uart_port *port, unsigned int cflag)
{
	struct sci_port *s = to_sci_port(port);

	/* Use port-specific handler if provided */
	if (s->cfg->ops && s->cfg->ops->init_pins) {
		s->cfg->ops->init_pins(port, cflag);
		return;
	}

	if (!s->has_rtscts)
		return;

	if (s->autorts)
		rsci_serial_out(port, CCR1, rsci_serial_in(port, CCR1) |
				CCR1_CTSE | CCR1_CTSPEN);
}

static int rsci_scif_set_rtrg(struct uart_port *port, int rx_trig)
{
	u32 fcr = rsci_serial_in(port, FCR);

	if (rx_trig >= port->fifosize)
		rx_trig = port->fifosize - 1;
	else if (rx_trig < 1)
		rx_trig = 0;

	FIELD_MODIFY(FCR_RTRG4_0, &fcr, rx_trig);
	rsci_serial_out(port, FCR, fcr);

	return rx_trig;
}

static void rsci_set_termios(struct uart_port *port, struct ktermios *termios,
			     const struct ktermios *old)
{
	unsigned int ccr2_val = CCR2_INIT, ccr3_val = CCR3_INIT;
	unsigned int ccr0_val = 0, ccr1_val = 0, ccr4_val = 0;
	unsigned int brr1 = 255, cks1 = 0, srr1 = 15;
	struct sci_port *s = to_sci_port(port);
	unsigned int brr = 255, cks = 0;
	int min_err = INT_MAX, err;
	unsigned long max_freq = 0;
	unsigned int baud, i;
	unsigned long flags;
	unsigned int ctrl;
	int best_clk = -1;

	if ((termios->c_cflag & CSIZE) == CS7) {
		ccr3_val |= CCR3_CHR0;
	} else {
		termios->c_cflag &= ~CSIZE;
		termios->c_cflag |= CS8;
	}

	if (termios->c_cflag & PARENB)
		ccr1_val |= CCR1_PE;

	if (termios->c_cflag & PARODD)
		ccr1_val |= (CCR1_PE | CCR1_PM);

	if (termios->c_cflag & CSTOPB)
		ccr3_val |= CCR3_STP;

	/* Enable noise filter function */
	ccr1_val |= CCR1_NFEN;

	/*
	 * earlyprintk comes here early on with port->uartclk set to zero.
	 * the clock framework is not up and running at this point so here
	 * we assume that 115200 is the maximum baud rate. please note that
	 * the baud rate is not programmed during earlyprintk - it is assumed
	 * that the previous boot loader has enabled required clocks and
	 * setup the baud rate generator hardware for us already.
	 */
	if (!port->uartclk) {
		max_freq = 115200;
	} else {
		for (i = 0; i < SCI_NUM_CLKS; i++)
			max_freq = max(max_freq, s->clk_rates[i]);

		max_freq /= min_sr(s);
	}

	baud = uart_get_baud_rate(port, termios, old, 0, max_freq);
	if (!baud)
		goto done;

	/* Divided Functional Clock using standard Bit Rate Register */
	err = sci_scbrr_calc(s, baud, &brr1, &srr1, &cks1);
	if (abs(err) < abs(min_err)) {
		best_clk = SCI_FCK;
		ccr0_val = 0;
		min_err = err;
		brr = brr1;
		cks = cks1;
	}

done:
	if (best_clk >= 0)
		dev_dbg(port->dev, "Using clk %pC for %u%+d bps\n",
			s->clks[best_clk], baud, min_err);

	sci_port_enable(s);
	uart_port_lock_irqsave(port, &flags);

	uart_update_timeout(port, termios->c_cflag, baud);

	rsci_serial_out(port, CCR0, ccr0_val);

	ccr3_val |= CCR3_FM;
	rsci_serial_out(port, CCR3, ccr3_val);

	ccr2_val |= (cks << 20) | (brr << 8);
	rsci_serial_out(port, CCR2, ccr2_val);

	rsci_serial_out(port, CCR1, ccr1_val);
	rsci_serial_out(port, CCR4, ccr4_val);

	ctrl = rsci_serial_in(port, FCR);
	ctrl |= (FCR_RFRST | FCR_TFRST);
	rsci_serial_out(port, FCR, ctrl);

	if (s->rx_trigger > 1)
		rsci_scif_set_rtrg(port, s->rx_trigger);

	port->status &= ~UPSTAT_AUTOCTS;
	s->autorts = false;

	if ((port->flags & UPF_HARD_FLOW) && (termios->c_cflag & CRTSCTS)) {
		port->status |= UPSTAT_AUTOCTS;
		s->autorts = true;
	}

	rsci_init_pins(port, termios->c_cflag);
	rsci_serial_out(port, CFCLR, CFCLR_CLRFLAG);
	rsci_serial_out(port, FFCLR, FFCLR_DRC);

	ccr0_val |= CCR0_RE;
	rsci_serial_out(port, CCR0, ccr0_val);

	if ((termios->c_cflag & CREAD) != 0)
		rsci_start_rx(port);

	uart_port_unlock_irqrestore(port, flags);
	sci_port_disable(s);

	if (UART_ENABLE_MS(port, termios->c_cflag))
		rsci_enable_ms(port);
}

static int rsci_txfill(struct uart_port *port)
{
	return rsci_serial_in(port, FTSR);
}

static int rsci_rxfill(struct uart_port *port)
{
	u32 val = rsci_serial_in(port, FRSR);

	return FIELD_GET(FRSR_R5_0, val);
}

static unsigned int rsci_tx_empty(struct uart_port *port)
{
	unsigned int status = rsci_serial_in(port, CSR);
	unsigned int in_tx_fifo = rsci_txfill(port);

	return (status & CSR_TEND) && !in_tx_fifo ? TIOCSER_TEMT : 0;
}

static void rsci_set_mctrl(struct uart_port *port, unsigned int mctrl)
{
	if (mctrl & TIOCM_LOOP) {
		/* Standard loopback mode */
		rsci_serial_out(port, CCR1, rsci_serial_in(port, CCR1) | CCR1_SPLP);
	}
}

static unsigned int rsci_get_mctrl(struct uart_port *port)
{
	struct sci_port *s = to_sci_port(port);
	struct mctrl_gpios *gpios = s->gpios;
	unsigned int mctrl = 0;

	mctrl_gpio_get(gpios, &mctrl);

	/*
	 * CTS/RTS is handled in hardware when supported, while nothing
	 * else is wired up.
	 */
	if (!mctrl_gpio_to_gpiod(gpios, UART_GPIO_CTS))
		mctrl |= TIOCM_CTS;

	if (!mctrl_gpio_to_gpiod(gpios, UART_GPIO_DSR))
		mctrl |= TIOCM_DSR;

	if (!mctrl_gpio_to_gpiod(gpios, UART_GPIO_DCD))
		mctrl |= TIOCM_CAR;

	return mctrl;
}

static void rsci_clear_CFC(struct uart_port *port, unsigned int mask)
{
	rsci_serial_out(port, CFCLR, mask);
}

static void rsci_start_tx(struct uart_port *port)
{
	struct sci_port *sp = to_sci_port(port);
	u32 ctrl;

	if (sp->chan_tx)
		return;

	/*
	 * TE (Transmit Enable) must be set after setting TIE
	 * (Transmit Interrupt Enable) or in the same instruction
	 * to start the transmit process.
	 */
	ctrl = rsci_serial_in(port, CCR0);
	ctrl |= CCR0_TIE | CCR0_TE;
	rsci_serial_out(port, CCR0, ctrl);
}

static void rsci_stop_tx(struct uart_port *port)
{
	u32 ctrl;

	ctrl = rsci_serial_in(port, CCR0);
	ctrl &= ~CCR0_TIE;
	rsci_serial_out(port, CCR0, ctrl);
}

static void rsci_stop_rx(struct uart_port *port)
{
	u32 ctrl;

	ctrl = rsci_serial_in(port, CCR0);
	ctrl &= ~CCR0_RIE;
	rsci_serial_out(port, CCR0, ctrl);
}

static int rsci_txroom(struct uart_port *port)
{
	return port->fifosize - rsci_txfill(port);
}

static void rsci_transmit_chars(struct uart_port *port)
{
	unsigned int stopped = uart_tx_stopped(port);
	struct tty_port *tport = &port->state->port;
	u32 status, ctrl;
	int count;

	status = rsci_serial_in(port, CSR);
	if (!(status & CSR_TDRE)) {
		ctrl = rsci_serial_in(port, CCR0);
		if (kfifo_is_empty(&tport->xmit_fifo))
			ctrl &= ~CCR0_TIE;
		else
			ctrl |= CCR0_TIE;
		rsci_serial_out(port, CCR0, ctrl);
		return;
	}

	count = rsci_txroom(port);

	do {
		unsigned char c;

		if (port->x_char) {
			c = port->x_char;
			port->x_char = 0;
		} else if (stopped || !kfifo_get(&tport->xmit_fifo, &c)) {
			break;
		}

		rsci_clear_CFC(port, CFCLR_TDREC);
		rsci_serial_out(port, TDR, c);

		port->icount.tx++;
	} while (--count > 0);

	if (kfifo_len(&tport->xmit_fifo) < WAKEUP_CHARS)
		uart_write_wakeup(port);

	if (kfifo_is_empty(&tport->xmit_fifo)) {
		ctrl = rsci_serial_in(port, CCR0);
		ctrl &= ~CCR0_TIE;
		ctrl |= CCR0_TEIE;
		rsci_serial_out(port, CCR0, ctrl);
	}
}

static void rsci_receive_chars(struct uart_port *port)
{
	struct tty_port *tport = &port->state->port;
	u32 rdat, status, frsr_status = 0;
	int i, count, copied = 0;
	unsigned char flag;

	status = rsci_serial_in(port, CSR);
	frsr_status = rsci_serial_in(port, FRSR);

	if (!(status & CSR_RDRF) && !(frsr_status & FRSR_DR))
		return;

	while (1) {
		/* Don't copy more bytes than there is room for in the buffer */
		count = tty_buffer_request_room(tport, rsci_rxfill(port));

		/* If for any reason we can't copy more data, we're done! */
		if (count == 0)
			break;

		for (i = 0; i < count; i++) {
			char c;

			rdat = rsci_serial_in(port, RDR);
			/* 9-bits data is not supported yet */
			c = rdat & RDR_RDAT_MSK;

			if (uart_handle_sysrq_char(port, c)) {
				count--;
				i--;
				continue;
			}

			/*
			 * Store data and status.
			 * Non FIFO mode is not supported
			 */
			if (rdat & RDR_FFER) {
				flag = TTY_FRAME;
				port->icount.frame++;
			} else if (rdat & RDR_FPER) {
				flag = TTY_PARITY;
				port->icount.parity++;
			} else {
				flag = TTY_NORMAL;
			}

			tty_insert_flip_char(tport, c, flag);
		}

		rsci_serial_in(port, CSR); /* dummy read */
		rsci_clear_DRxC(port);

		copied += count;
		port->icount.rx += count;
	}

	if (copied) {
		/* Tell the rest of the system the news. New characters! */
		tty_flip_buffer_push(tport);
	} else {
		/* TTY buffers full; read from RX reg to prevent lockup */
		rsci_serial_in(port, RDR);
		rsci_serial_in(port, CSR); /* dummy read */
		rsci_clear_DRxC(port);
	}
}

static void rsci_break_ctl(struct uart_port *port, int break_state)
{
	unsigned short ccr0_val, ccr1_val;
	unsigned long flags;

	uart_port_lock_irqsave(port, &flags);
	ccr1_val = rsci_serial_in(port, CCR1);
	ccr0_val = rsci_serial_in(port, CCR0);

	if (break_state == -1) {
		ccr1_val = (ccr1_val | CCR1_SPB2IO) & ~CCR1_SPB2DT;
		ccr0_val &= ~CCR0_TE;
	} else {
		ccr1_val = (ccr1_val | CCR1_SPB2DT) & ~CCR1_SPB2IO;
		ccr0_val |= CCR0_TE;
	}

	rsci_serial_out(port, CCR1, ccr1_val);
	rsci_serial_out(port, CCR0, ccr0_val);
	uart_port_unlock_irqrestore(port, flags);
}

static void rsci_poll_put_char(struct uart_port *port, unsigned char c)
{
	u32 status;
	int ret;

	ret = readl_relaxed_poll_timeout_atomic(port->membase + CSR, status,
						(status & CSR_TDRE), 100,
						USEC_PER_SEC);
	if (ret != 0) {
		dev_err(port->dev,
			"Error while sending data in UART TX : %d\n", ret);
		goto done;
	}
	rsci_serial_out(port, TDR, c);
done:
	rsci_clear_CFC(port, CFCLR_TDREC);
}

static void rsci_prepare_console_write(struct uart_port *port, u32 ctrl)
{
	struct sci_port *s = to_sci_port(port);
	u32 ctrl_temp = s->params->param_bits->rxtx_enable;

	if (s->type == RSCI_PORT_SCIF16)
		ctrl_temp |= CCR0_TIE | s->hscif_tot;

	rsci_serial_out(port, CCR0, ctrl_temp);
}

static void rsci_finish_console_write(struct uart_port *port, u32 ctrl)
{
	/* First set TE = 0 and then restore the CCR0 value */
	rsci_serial_out(port, CCR0, ctrl & ~CCR0_TE);
	rsci_serial_out(port, CCR0, ctrl);
}

static const char *rsci_type(struct uart_port *port)
{
	return "rsci";
}

static size_t rsci_suspend_regs_size(void)
{
	return 0;
}

static void rsci_shutdown_complete(struct uart_port *port)
{
	/*
	 * Stop RX and TX, disable related interrupts, keep clock source
	 */
	rsci_serial_out(port, CCR0, 0);
}

static const struct sci_common_regs rsci_common_regs = {
	.status = CSR,
	.control = CCR0,
};

static const struct sci_port_params_bits rsci_port_param_bits = {
	.rxtx_enable = CCR0_RE | CCR0_TE,
	.te_clear = CCR0_TE | CCR0_TEIE,
	.poll_sent_bits = CSR_TDRE | CSR_TEND,
};

static const struct sci_port_params rsci_rzg3e_port_params = {
	.fifosize = 32,
	.overrun_reg = CSR,
	.overrun_mask = CSR_ORER,
	.sampling_rate_mask = SCI_SR(32),
	.error_mask = RSCI_DEFAULT_ERROR_MASK,
	.error_clear = RSCI_ERROR_CLEAR,
	.param_bits = &rsci_port_param_bits,
	.common_regs = &rsci_common_regs,
};

static const struct sci_port_params rsci_rzt2h_port_params = {
	.fifosize = 16,
	.overrun_reg = CSR,
	.overrun_mask = CSR_ORER,
	.sampling_rate_mask = SCI_SR(32),
	.error_mask = RSCI_DEFAULT_ERROR_MASK,
	.error_clear = RSCI_ERROR_CLEAR,
	.param_bits = &rsci_port_param_bits,
	.common_regs = &rsci_common_regs,
};

static const struct uart_ops rsci_uart_ops = {
	.tx_empty	= rsci_tx_empty,
	.set_mctrl	= rsci_set_mctrl,
	.get_mctrl	= rsci_get_mctrl,
	.start_tx	= rsci_start_tx,
	.stop_tx	= rsci_stop_tx,
	.stop_rx	= rsci_stop_rx,
	.enable_ms	= rsci_enable_ms,
	.break_ctl	= rsci_break_ctl,
	.startup	= sci_startup,
	.shutdown	= sci_shutdown,
	.set_termios	= rsci_set_termios,
	.pm		= sci_pm,
	.type		= rsci_type,
	.release_port	= sci_release_port,
	.request_port	= sci_request_port,
	.config_port	= sci_config_port,
	.verify_port	= sci_verify_port,
};

static const struct sci_port_ops rsci_port_ops = {
	.read_reg		= rsci_serial_in,
	.write_reg		= rsci_serial_out,
	.clear_SCxSR		= rsci_clear_CFC,
	.transmit_chars		= rsci_transmit_chars,
	.receive_chars		= rsci_receive_chars,
	.poll_put_char		= rsci_poll_put_char,
	.prepare_console_write	= rsci_prepare_console_write,
	.finish_console_write	= rsci_finish_console_write,
	.suspend_regs_size	= rsci_suspend_regs_size,
	.set_rtrg		= rsci_scif_set_rtrg,
	.shutdown_complete	= rsci_shutdown_complete,
};

struct sci_of_data of_rsci_rzg3e_data = {
	.type = RSCI_PORT_SCIF32,
	.ops = &rsci_port_ops,
	.uart_ops = &rsci_uart_ops,
	.params = &rsci_rzg3e_port_params,
};

struct sci_of_data of_rsci_rzt2h_data = {
	.type = RSCI_PORT_SCIF16,
	.ops = &rsci_port_ops,
	.uart_ops = &rsci_uart_ops,
	.params = &rsci_rzt2h_port_params,
};

#ifdef CONFIG_SERIAL_SH_SCI_EARLYCON

static int __init rsci_rzg3e_early_console_setup(struct earlycon_device *device,
						 const char *opt)
{
	return scix_early_console_setup(device, &of_rsci_rzg3e_data);
}

static int __init rsci_rzt2h_early_console_setup(struct earlycon_device *device,
						 const char *opt)
{
	return scix_early_console_setup(device, &of_rsci_rzt2h_data);
}

OF_EARLYCON_DECLARE(rsci, "renesas,r9a09g047-rsci", rsci_rzg3e_early_console_setup);
OF_EARLYCON_DECLARE(rsci, "renesas,r9a09g077-rsci", rsci_rzt2h_early_console_setup);

#endif /* CONFIG_SERIAL_SH_SCI_EARLYCON */

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("RSCI serial driver");
