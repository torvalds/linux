/*
 *  linux/drivers/serial/imx.c
 *
 *  Driver for Motorola IMX serial ports
 *
 *  Based on drivers/char/serial.c, by Linus Torvalds, Theodore Ts'o.
 *
 *  Author: Sascha Hauer <sascha@saschahauer.de>
 *  Copyright (C) 2004 Pengutronix
 *
 *  Copyright (C) 2009 emlix GmbH
 *  Author: Fabian Godehardt (added IrDA support for iMX)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * [29-Mar-2005] Mike Lee
 * Added hardware handshake
 */

#if defined(CONFIG_SERIAL_IMX_CONSOLE) && defined(CONFIG_MAGIC_SYSRQ)
#define SUPPORT_SYSRQ
#endif

#include <linux/module.h>
#include <linux/ioport.h>
#include <linux/init.h>
#include <linux/console.h>
#include <linux/sysrq.h>
#include <linux/platform_device.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/serial_core.h>
#include <linux/serial.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/rational.h>

#include <asm/io.h>
#include <asm/irq.h>
#include <mach/hardware.h>
#include <mach/imx-uart.h>

/* Register definitions */
#define URXD0 0x0  /* Receiver Register */
#define URTX0 0x40 /* Transmitter Register */
#define UCR1  0x80 /* Control Register 1 */
#define UCR2  0x84 /* Control Register 2 */
#define UCR3  0x88 /* Control Register 3 */
#define UCR4  0x8c /* Control Register 4 */
#define UFCR  0x90 /* FIFO Control Register */
#define USR1  0x94 /* Status Register 1 */
#define USR2  0x98 /* Status Register 2 */
#define UESC  0x9c /* Escape Character Register */
#define UTIM  0xa0 /* Escape Timer Register */
#define UBIR  0xa4 /* BRM Incremental Register */
#define UBMR  0xa8 /* BRM Modulator Register */
#define UBRC  0xac /* Baud Rate Count Register */
#define MX2_ONEMS 0xb0 /* One Millisecond register */
#define UTS (cpu_is_mx1() ? 0xd0 : 0xb4) /* UART Test Register */

/* UART Control Register Bit Fields.*/
#define  URXD_CHARRDY    (1<<15)
#define  URXD_ERR        (1<<14)
#define  URXD_OVRRUN     (1<<13)
#define  URXD_FRMERR     (1<<12)
#define  URXD_BRK        (1<<11)
#define  URXD_PRERR      (1<<10)
#define  UCR1_ADEN       (1<<15) /* Auto dectect interrupt */
#define  UCR1_ADBR       (1<<14) /* Auto detect baud rate */
#define  UCR1_TRDYEN     (1<<13) /* Transmitter ready interrupt enable */
#define  UCR1_IDEN       (1<<12) /* Idle condition interrupt */
#define  UCR1_RRDYEN     (1<<9)	 /* Recv ready interrupt enable */
#define  UCR1_RDMAEN     (1<<8)	 /* Recv ready DMA enable */
#define  UCR1_IREN       (1<<7)	 /* Infrared interface enable */
#define  UCR1_TXMPTYEN   (1<<6)	 /* Transimitter empty interrupt enable */
#define  UCR1_RTSDEN     (1<<5)	 /* RTS delta interrupt enable */
#define  UCR1_SNDBRK     (1<<4)	 /* Send break */
#define  UCR1_TDMAEN     (1<<3)	 /* Transmitter ready DMA enable */
#define  MX1_UCR1_UARTCLKEN  (1<<2)	 /* UART clock enabled, mx1 only */
#define  UCR1_DOZE       (1<<1)	 /* Doze */
#define  UCR1_UARTEN     (1<<0)	 /* UART enabled */
#define  UCR2_ESCI     	 (1<<15) /* Escape seq interrupt enable */
#define  UCR2_IRTS  	 (1<<14) /* Ignore RTS pin */
#define  UCR2_CTSC  	 (1<<13) /* CTS pin control */
#define  UCR2_CTS        (1<<12) /* Clear to send */
#define  UCR2_ESCEN      (1<<11) /* Escape enable */
#define  UCR2_PREN       (1<<8)  /* Parity enable */
#define  UCR2_PROE       (1<<7)  /* Parity odd/even */
#define  UCR2_STPB       (1<<6)	 /* Stop */
#define  UCR2_WS         (1<<5)	 /* Word size */
#define  UCR2_RTSEN      (1<<4)	 /* Request to send interrupt enable */
#define  UCR2_TXEN       (1<<2)	 /* Transmitter enabled */
#define  UCR2_RXEN       (1<<1)	 /* Receiver enabled */
#define  UCR2_SRST 	 (1<<0)	 /* SW reset */
#define  UCR3_DTREN 	 (1<<13) /* DTR interrupt enable */
#define  UCR3_PARERREN   (1<<12) /* Parity enable */
#define  UCR3_FRAERREN   (1<<11) /* Frame error interrupt enable */
#define  UCR3_DSR        (1<<10) /* Data set ready */
#define  UCR3_DCD        (1<<9)  /* Data carrier detect */
#define  UCR3_RI         (1<<8)  /* Ring indicator */
#define  UCR3_TIMEOUTEN  (1<<7)  /* Timeout interrupt enable */
#define  UCR3_RXDSEN	 (1<<6)  /* Receive status interrupt enable */
#define  UCR3_AIRINTEN   (1<<5)  /* Async IR wake interrupt enable */
#define  UCR3_AWAKEN	 (1<<4)  /* Async wake interrupt enable */
#define  MX1_UCR3_REF25 	 (1<<3)  /* Ref freq 25 MHz, only on mx1 */
#define  MX1_UCR3_REF30 	 (1<<2)  /* Ref Freq 30 MHz, only on mx1 */
#define  MX2_UCR3_RXDMUXSEL	 (1<<2)  /* RXD Muxed Input Select, on mx2/mx3 */
#define  UCR3_INVT  	 (1<<1)  /* Inverted Infrared transmission */
#define  UCR3_BPEN  	 (1<<0)  /* Preset registers enable */
#define  UCR4_CTSTL_32   (32<<10) /* CTS trigger level (32 chars) */
#define  UCR4_INVR  	 (1<<9)  /* Inverted infrared reception */
#define  UCR4_ENIRI 	 (1<<8)  /* Serial infrared interrupt enable */
#define  UCR4_WKEN  	 (1<<7)  /* Wake interrupt enable */
#define  UCR4_REF16 	 (1<<6)  /* Ref freq 16 MHz */
#define  UCR4_IRSC  	 (1<<5)  /* IR special case */
#define  UCR4_TCEN  	 (1<<3)  /* Transmit complete interrupt enable */
#define  UCR4_BKEN  	 (1<<2)  /* Break condition interrupt enable */
#define  UCR4_OREN  	 (1<<1)  /* Receiver overrun interrupt enable */
#define  UCR4_DREN  	 (1<<0)  /* Recv data ready interrupt enable */
#define  UFCR_RXTL_SHF   0       /* Receiver trigger level shift */
#define  UFCR_RFDIV      (7<<7)  /* Reference freq divider mask */
#define  UFCR_RFDIV_REG(x)	(((x) < 7 ? 6 - (x) : 6) << 7)
#define  UFCR_TXTL_SHF   10      /* Transmitter trigger level shift */
#define  USR1_PARITYERR  (1<<15) /* Parity error interrupt flag */
#define  USR1_RTSS  	 (1<<14) /* RTS pin status */
#define  USR1_TRDY  	 (1<<13) /* Transmitter ready interrupt/dma flag */
#define  USR1_RTSD  	 (1<<12) /* RTS delta */
#define  USR1_ESCF  	 (1<<11) /* Escape seq interrupt flag */
#define  USR1_FRAMERR    (1<<10) /* Frame error interrupt flag */
#define  USR1_RRDY       (1<<9)	 /* Receiver ready interrupt/dma flag */
#define  USR1_TIMEOUT    (1<<7)	 /* Receive timeout interrupt status */
#define  USR1_RXDS  	 (1<<6)	 /* Receiver idle interrupt flag */
#define  USR1_AIRINT	 (1<<5)	 /* Async IR wake interrupt flag */
#define  USR1_AWAKE 	 (1<<4)	 /* Aysnc wake interrupt flag */
#define  USR2_ADET  	 (1<<15) /* Auto baud rate detect complete */
#define  USR2_TXFE  	 (1<<14) /* Transmit buffer FIFO empty */
#define  USR2_DTRF  	 (1<<13) /* DTR edge interrupt flag */
#define  USR2_IDLE  	 (1<<12) /* Idle condition */
#define  USR2_IRINT 	 (1<<8)	 /* Serial infrared interrupt flag */
#define  USR2_WAKE  	 (1<<7)	 /* Wake */
#define  USR2_RTSF  	 (1<<4)	 /* RTS edge interrupt flag */
#define  USR2_TXDC  	 (1<<3)	 /* Transmitter complete */
#define  USR2_BRCD  	 (1<<2)	 /* Break condition */
#define  USR2_ORE        (1<<1)	 /* Overrun error */
#define  USR2_RDR        (1<<0)	 /* Recv data ready */
#define  UTS_FRCPERR	 (1<<13) /* Force parity error */
#define  UTS_LOOP        (1<<12) /* Loop tx and rx */
#define  UTS_TXEMPTY	 (1<<6)	 /* TxFIFO empty */
#define  UTS_RXEMPTY	 (1<<5)	 /* RxFIFO empty */
#define  UTS_TXFULL 	 (1<<4)	 /* TxFIFO full */
#define  UTS_RXFULL 	 (1<<3)	 /* RxFIFO full */
#define  UTS_SOFTRST	 (1<<0)	 /* Software reset */

/* We've been assigned a range on the "Low-density serial ports" major */
#define SERIAL_IMX_MAJOR        207
#define MINOR_START	        16
#define DEV_NAME		"ttymxc"
#define MAX_INTERNAL_IRQ	MXC_INTERNAL_IRQS

/*
 * This determines how often we check the modem status signals
 * for any change.  They generally aren't connected to an IRQ
 * so we have to poll them.  We also check immediately before
 * filling the TX fifo incase CTS has been dropped.
 */
#define MCTRL_TIMEOUT	(250*HZ/1000)

#define DRIVER_NAME "IMX-uart"

#define UART_NR 8

struct imx_port {
	struct uart_port	port;
	struct timer_list	timer;
	unsigned int		old_status;
	int			txirq,rxirq,rtsirq;
	unsigned int		have_rtscts:1;
	unsigned int		use_irda:1;
	unsigned int		irda_inv_rx:1;
	unsigned int		irda_inv_tx:1;
	unsigned short		trcv_delay; /* transceiver delay */
	struct clk		*clk;
};

#ifdef CONFIG_IRDA
#define USE_IRDA(sport)	((sport)->use_irda)
#else
#define USE_IRDA(sport)	(0)
#endif

/*
 * Handle any change of modem status signal since we were last called.
 */
static void imx_mctrl_check(struct imx_port *sport)
{
	unsigned int status, changed;

	status = sport->port.ops->get_mctrl(&sport->port);
	changed = status ^ sport->old_status;

	if (changed == 0)
		return;

	sport->old_status = status;

	if (changed & TIOCM_RI)
		sport->port.icount.rng++;
	if (changed & TIOCM_DSR)
		sport->port.icount.dsr++;
	if (changed & TIOCM_CAR)
		uart_handle_dcd_change(&sport->port, status & TIOCM_CAR);
	if (changed & TIOCM_CTS)
		uart_handle_cts_change(&sport->port, status & TIOCM_CTS);

	wake_up_interruptible(&sport->port.state->port.delta_msr_wait);
}

/*
 * This is our per-port timeout handler, for checking the
 * modem status signals.
 */
static void imx_timeout(unsigned long data)
{
	struct imx_port *sport = (struct imx_port *)data;
	unsigned long flags;

	if (sport->port.state) {
		spin_lock_irqsave(&sport->port.lock, flags);
		imx_mctrl_check(sport);
		spin_unlock_irqrestore(&sport->port.lock, flags);

		mod_timer(&sport->timer, jiffies + MCTRL_TIMEOUT);
	}
}

/*
 * interrupts disabled on entry
 */
static void imx_stop_tx(struct uart_port *port)
{
	struct imx_port *sport = (struct imx_port *)port;
	unsigned long temp;

	if (USE_IRDA(sport)) {
		/* half duplex - wait for end of transmission */
		int n = 256;
		while ((--n > 0) &&
		      !(readl(sport->port.membase + USR2) & USR2_TXDC)) {
			udelay(5);
			barrier();
		}
		/*
		 * irda transceiver - wait a bit more to avoid
		 * cutoff, hardware dependent
		 */
		udelay(sport->trcv_delay);

		/*
		 * half duplex - reactivate receive mode,
		 * flush receive pipe echo crap
		 */
		if (readl(sport->port.membase + USR2) & USR2_TXDC) {
			temp = readl(sport->port.membase + UCR1);
			temp &= ~(UCR1_TXMPTYEN | UCR1_TRDYEN);
			writel(temp, sport->port.membase + UCR1);

			temp = readl(sport->port.membase + UCR4);
			temp &= ~(UCR4_TCEN);
			writel(temp, sport->port.membase + UCR4);

			while (readl(sport->port.membase + URXD0) &
			       URXD_CHARRDY)
				barrier();

			temp = readl(sport->port.membase + UCR1);
			temp |= UCR1_RRDYEN;
			writel(temp, sport->port.membase + UCR1);

			temp = readl(sport->port.membase + UCR4);
			temp |= UCR4_DREN;
			writel(temp, sport->port.membase + UCR4);
		}
		return;
	}

	temp = readl(sport->port.membase + UCR1);
	writel(temp & ~UCR1_TXMPTYEN, sport->port.membase + UCR1);
}

/*
 * interrupts disabled on entry
 */
static void imx_stop_rx(struct uart_port *port)
{
	struct imx_port *sport = (struct imx_port *)port;
	unsigned long temp;

	temp = readl(sport->port.membase + UCR2);
	writel(temp &~ UCR2_RXEN, sport->port.membase + UCR2);
}

/*
 * Set the modem control timer to fire immediately.
 */
static void imx_enable_ms(struct uart_port *port)
{
	struct imx_port *sport = (struct imx_port *)port;

	mod_timer(&sport->timer, jiffies);
}

static inline void imx_transmit_buffer(struct imx_port *sport)
{
	struct circ_buf *xmit = &sport->port.state->xmit;

	while (!(readl(sport->port.membase + UTS) & UTS_TXFULL)) {
		/* send xmit->buf[xmit->tail]
		 * out the port here */
		writel(xmit->buf[xmit->tail], sport->port.membase + URTX0);
		xmit->tail = (xmit->tail + 1) & (UART_XMIT_SIZE - 1);
		sport->port.icount.tx++;
		if (uart_circ_empty(xmit))
			break;
	}

	if (uart_circ_chars_pending(xmit) < WAKEUP_CHARS)
		uart_write_wakeup(&sport->port);

	if (uart_circ_empty(xmit))
		imx_stop_tx(&sport->port);
}

/*
 * interrupts disabled on entry
 */
static void imx_start_tx(struct uart_port *port)
{
	struct imx_port *sport = (struct imx_port *)port;
	unsigned long temp;

	if (USE_IRDA(sport)) {
		/* half duplex in IrDA mode; have to disable receive mode */
		temp = readl(sport->port.membase + UCR4);
		temp &= ~(UCR4_DREN);
		writel(temp, sport->port.membase + UCR4);

		temp = readl(sport->port.membase + UCR1);
		temp &= ~(UCR1_RRDYEN);
		writel(temp, sport->port.membase + UCR1);
	}

	temp = readl(sport->port.membase + UCR1);
	writel(temp | UCR1_TXMPTYEN, sport->port.membase + UCR1);

	if (USE_IRDA(sport)) {
		temp = readl(sport->port.membase + UCR1);
		temp |= UCR1_TRDYEN;
		writel(temp, sport->port.membase + UCR1);

		temp = readl(sport->port.membase + UCR4);
		temp |= UCR4_TCEN;
		writel(temp, sport->port.membase + UCR4);
	}

	if (readl(sport->port.membase + UTS) & UTS_TXEMPTY)
		imx_transmit_buffer(sport);
}

static irqreturn_t imx_rtsint(int irq, void *dev_id)
{
	struct imx_port *sport = dev_id;
	unsigned int val = readl(sport->port.membase + USR1) & USR1_RTSS;
	unsigned long flags;

	spin_lock_irqsave(&sport->port.lock, flags);

	writel(USR1_RTSD, sport->port.membase + USR1);
	uart_handle_cts_change(&sport->port, !!val);
	wake_up_interruptible(&sport->port.state->port.delta_msr_wait);

	spin_unlock_irqrestore(&sport->port.lock, flags);
	return IRQ_HANDLED;
}

static irqreturn_t imx_txint(int irq, void *dev_id)
{
	struct imx_port *sport = dev_id;
	struct circ_buf *xmit = &sport->port.state->xmit;
	unsigned long flags;

	spin_lock_irqsave(&sport->port.lock,flags);
	if (sport->port.x_char)
	{
		/* Send next char */
		writel(sport->port.x_char, sport->port.membase + URTX0);
		goto out;
	}

	if (uart_circ_empty(xmit) || uart_tx_stopped(&sport->port)) {
		imx_stop_tx(&sport->port);
		goto out;
	}

	imx_transmit_buffer(sport);

	if (uart_circ_chars_pending(xmit) < WAKEUP_CHARS)
		uart_write_wakeup(&sport->port);

out:
	spin_unlock_irqrestore(&sport->port.lock,flags);
	return IRQ_HANDLED;
}

static irqreturn_t imx_rxint(int irq, void *dev_id)
{
	struct imx_port *sport = dev_id;
	unsigned int rx,flg,ignored = 0;
	struct tty_struct *tty = sport->port.state->port.tty;
	unsigned long flags, temp;

	spin_lock_irqsave(&sport->port.lock,flags);

	while (readl(sport->port.membase + USR2) & USR2_RDR) {
		flg = TTY_NORMAL;
		sport->port.icount.rx++;

		rx = readl(sport->port.membase + URXD0);

		temp = readl(sport->port.membase + USR2);
		if (temp & USR2_BRCD) {
			writel(USR2_BRCD, sport->port.membase + USR2);
			if (uart_handle_break(&sport->port))
				continue;
		}

		if (uart_handle_sysrq_char(&sport->port, (unsigned char)rx))
			continue;

		if (rx & (URXD_PRERR | URXD_OVRRUN | URXD_FRMERR) ) {
			if (rx & URXD_PRERR)
				sport->port.icount.parity++;
			else if (rx & URXD_FRMERR)
				sport->port.icount.frame++;
			if (rx & URXD_OVRRUN)
				sport->port.icount.overrun++;

			if (rx & sport->port.ignore_status_mask) {
				if (++ignored > 100)
					goto out;
				continue;
			}

			rx &= sport->port.read_status_mask;

			if (rx & URXD_PRERR)
				flg = TTY_PARITY;
			else if (rx & URXD_FRMERR)
				flg = TTY_FRAME;
			if (rx & URXD_OVRRUN)
				flg = TTY_OVERRUN;

#ifdef SUPPORT_SYSRQ
			sport->port.sysrq = 0;
#endif
		}

		tty_insert_flip_char(tty, rx, flg);
	}

out:
	spin_unlock_irqrestore(&sport->port.lock,flags);
	tty_flip_buffer_push(tty);
	return IRQ_HANDLED;
}

static irqreturn_t imx_int(int irq, void *dev_id)
{
	struct imx_port *sport = dev_id;
	unsigned int sts;

	sts = readl(sport->port.membase + USR1);

	if (sts & USR1_RRDY)
		imx_rxint(irq, dev_id);

	if (sts & USR1_TRDY &&
			readl(sport->port.membase + UCR1) & UCR1_TXMPTYEN)
		imx_txint(irq, dev_id);

	if (sts & USR1_RTSD)
		imx_rtsint(irq, dev_id);

	return IRQ_HANDLED;
}

/*
 * Return TIOCSER_TEMT when transmitter is not busy.
 */
static unsigned int imx_tx_empty(struct uart_port *port)
{
	struct imx_port *sport = (struct imx_port *)port;

	return (readl(sport->port.membase + USR2) & USR2_TXDC) ?  TIOCSER_TEMT : 0;
}

/*
 * We have a modem side uart, so the meanings of RTS and CTS are inverted.
 */
static unsigned int imx_get_mctrl(struct uart_port *port)
{
	struct imx_port *sport = (struct imx_port *)port;
	unsigned int tmp = TIOCM_DSR | TIOCM_CAR;

	if (readl(sport->port.membase + USR1) & USR1_RTSS)
		tmp |= TIOCM_CTS;

	if (readl(sport->port.membase + UCR2) & UCR2_CTS)
		tmp |= TIOCM_RTS;

	return tmp;
}

static void imx_set_mctrl(struct uart_port *port, unsigned int mctrl)
{
	struct imx_port *sport = (struct imx_port *)port;
	unsigned long temp;

	temp = readl(sport->port.membase + UCR2) & ~UCR2_CTS;

	if (mctrl & TIOCM_RTS)
		temp |= UCR2_CTS;

	writel(temp, sport->port.membase + UCR2);
}

/*
 * Interrupts always disabled.
 */
static void imx_break_ctl(struct uart_port *port, int break_state)
{
	struct imx_port *sport = (struct imx_port *)port;
	unsigned long flags, temp;

	spin_lock_irqsave(&sport->port.lock, flags);

	temp = readl(sport->port.membase + UCR1) & ~UCR1_SNDBRK;

	if ( break_state != 0 )
		temp |= UCR1_SNDBRK;

	writel(temp, sport->port.membase + UCR1);

	spin_unlock_irqrestore(&sport->port.lock, flags);
}

#define TXTL 2 /* reset default */
#define RXTL 1 /* reset default */

static int imx_setup_ufcr(struct imx_port *sport, unsigned int mode)
{
	unsigned int val;
	unsigned int ufcr_rfdiv;

	/* set receiver / transmitter trigger level.
	 * RFDIV is set such way to satisfy requested uartclk value
	 */
	val = TXTL << 10 | RXTL;
	ufcr_rfdiv = (clk_get_rate(sport->clk) + sport->port.uartclk / 2)
			/ sport->port.uartclk;

	if(!ufcr_rfdiv)
		ufcr_rfdiv = 1;

	val |= UFCR_RFDIV_REG(ufcr_rfdiv);

	writel(val, sport->port.membase + UFCR);

	return 0;
}

static int imx_startup(struct uart_port *port)
{
	struct imx_port *sport = (struct imx_port *)port;
	int retval;
	unsigned long flags, temp;

	imx_setup_ufcr(sport, 0);

	/* disable the DREN bit (Data Ready interrupt enable) before
	 * requesting IRQs
	 */
	temp = readl(sport->port.membase + UCR4);

	if (USE_IRDA(sport))
		temp |= UCR4_IRSC;

	writel(temp & ~UCR4_DREN, sport->port.membase + UCR4);

	if (USE_IRDA(sport)) {
		/* reset fifo's and state machines */
		int i = 100;
		temp = readl(sport->port.membase + UCR2);
		temp &= ~UCR2_SRST;
		writel(temp, sport->port.membase + UCR2);
		while (!(readl(sport->port.membase + UCR2) & UCR2_SRST) &&
		    (--i > 0)) {
			udelay(1);
		}
	}

	/*
	 * Allocate the IRQ(s) i.MX1 has three interrupts whereas later
	 * chips only have one interrupt.
	 */
	if (sport->txirq > 0) {
		retval = request_irq(sport->rxirq, imx_rxint, 0,
				DRIVER_NAME, sport);
		if (retval)
			goto error_out1;

		retval = request_irq(sport->txirq, imx_txint, 0,
				DRIVER_NAME, sport);
		if (retval)
			goto error_out2;

		/* do not use RTS IRQ on IrDA */
		if (!USE_IRDA(sport)) {
			retval = request_irq(sport->rtsirq, imx_rtsint,
				     (sport->rtsirq < MAX_INTERNAL_IRQ) ? 0 :
				       IRQF_TRIGGER_FALLING |
				       IRQF_TRIGGER_RISING,
					DRIVER_NAME, sport);
			if (retval)
				goto error_out3;
		}
	} else {
		retval = request_irq(sport->port.irq, imx_int, 0,
				DRIVER_NAME, sport);
		if (retval) {
			free_irq(sport->port.irq, sport);
			goto error_out1;
		}
	}

	/*
	 * Finally, clear and enable interrupts
	 */
	writel(USR1_RTSD, sport->port.membase + USR1);

	temp = readl(sport->port.membase + UCR1);
	temp |= UCR1_RRDYEN | UCR1_RTSDEN | UCR1_UARTEN;

	if (USE_IRDA(sport)) {
		temp |= UCR1_IREN;
		temp &= ~(UCR1_RTSDEN);
	}

	writel(temp, sport->port.membase + UCR1);

	temp = readl(sport->port.membase + UCR2);
	temp |= (UCR2_RXEN | UCR2_TXEN);
	writel(temp, sport->port.membase + UCR2);

	if (USE_IRDA(sport)) {
		/* clear RX-FIFO */
		int i = 64;
		while ((--i > 0) &&
			(readl(sport->port.membase + URXD0) & URXD_CHARRDY)) {
			barrier();
		}
	}

	if (!cpu_is_mx1()) {
		temp = readl(sport->port.membase + UCR3);
		temp |= MX2_UCR3_RXDMUXSEL;
		writel(temp, sport->port.membase + UCR3);
	}

	if (USE_IRDA(sport)) {
		temp = readl(sport->port.membase + UCR4);
		if (sport->irda_inv_rx)
			temp |= UCR4_INVR;
		else
			temp &= ~(UCR4_INVR);
		writel(temp | UCR4_DREN, sport->port.membase + UCR4);

		temp = readl(sport->port.membase + UCR3);
		if (sport->irda_inv_tx)
			temp |= UCR3_INVT;
		else
			temp &= ~(UCR3_INVT);
		writel(temp, sport->port.membase + UCR3);
	}

	/*
	 * Enable modem status interrupts
	 */
	spin_lock_irqsave(&sport->port.lock,flags);
	imx_enable_ms(&sport->port);
	spin_unlock_irqrestore(&sport->port.lock,flags);

	if (USE_IRDA(sport)) {
		struct imxuart_platform_data *pdata;
		pdata = sport->port.dev->platform_data;
		sport->irda_inv_rx = pdata->irda_inv_rx;
		sport->irda_inv_tx = pdata->irda_inv_tx;
		sport->trcv_delay = pdata->transceiver_delay;
		if (pdata->irda_enable)
			pdata->irda_enable(1);
	}

	return 0;

error_out3:
	if (sport->txirq)
		free_irq(sport->txirq, sport);
error_out2:
	if (sport->rxirq)
		free_irq(sport->rxirq, sport);
error_out1:
	return retval;
}

static void imx_shutdown(struct uart_port *port)
{
	struct imx_port *sport = (struct imx_port *)port;
	unsigned long temp;

	temp = readl(sport->port.membase + UCR2);
	temp &= ~(UCR2_TXEN);
	writel(temp, sport->port.membase + UCR2);

	if (USE_IRDA(sport)) {
		struct imxuart_platform_data *pdata;
		pdata = sport->port.dev->platform_data;
		if (pdata->irda_enable)
			pdata->irda_enable(0);
	}

	/*
	 * Stop our timer.
	 */
	del_timer_sync(&sport->timer);

	/*
	 * Free the interrupts
	 */
	if (sport->txirq > 0) {
		if (!USE_IRDA(sport))
			free_irq(sport->rtsirq, sport);
		free_irq(sport->txirq, sport);
		free_irq(sport->rxirq, sport);
	} else
		free_irq(sport->port.irq, sport);

	/*
	 * Disable all interrupts, port and break condition.
	 */

	temp = readl(sport->port.membase + UCR1);
	temp &= ~(UCR1_TXMPTYEN | UCR1_RRDYEN | UCR1_RTSDEN | UCR1_UARTEN);
	if (USE_IRDA(sport))
		temp &= ~(UCR1_IREN);

	writel(temp, sport->port.membase + UCR1);
}

static void
imx_set_termios(struct uart_port *port, struct ktermios *termios,
		   struct ktermios *old)
{
	struct imx_port *sport = (struct imx_port *)port;
	unsigned long flags;
	unsigned int ucr2, old_ucr1, old_txrxen, baud, quot;
	unsigned int old_csize = old ? old->c_cflag & CSIZE : CS8;
	unsigned int div, ufcr;
	unsigned long num, denom;
	uint64_t tdiv64;

	/*
	 * If we don't support modem control lines, don't allow
	 * these to be set.
	 */
	if (0) {
		termios->c_cflag &= ~(HUPCL | CRTSCTS | CMSPAR);
		termios->c_cflag |= CLOCAL;
	}

	/*
	 * We only support CS7 and CS8.
	 */
	while ((termios->c_cflag & CSIZE) != CS7 &&
	       (termios->c_cflag & CSIZE) != CS8) {
		termios->c_cflag &= ~CSIZE;
		termios->c_cflag |= old_csize;
		old_csize = CS8;
	}

	if ((termios->c_cflag & CSIZE) == CS8)
		ucr2 = UCR2_WS | UCR2_SRST | UCR2_IRTS;
	else
		ucr2 = UCR2_SRST | UCR2_IRTS;

	if (termios->c_cflag & CRTSCTS) {
		if( sport->have_rtscts ) {
			ucr2 &= ~UCR2_IRTS;
			ucr2 |= UCR2_CTSC;
		} else {
			termios->c_cflag &= ~CRTSCTS;
		}
	}

	if (termios->c_cflag & CSTOPB)
		ucr2 |= UCR2_STPB;
	if (termios->c_cflag & PARENB) {
		ucr2 |= UCR2_PREN;
		if (termios->c_cflag & PARODD)
			ucr2 |= UCR2_PROE;
	}

	/*
	 * Ask the core to calculate the divisor for us.
	 */
	baud = uart_get_baud_rate(port, termios, old, 50, port->uartclk / 16);
	quot = uart_get_divisor(port, baud);

	spin_lock_irqsave(&sport->port.lock, flags);

	sport->port.read_status_mask = 0;
	if (termios->c_iflag & INPCK)
		sport->port.read_status_mask |= (URXD_FRMERR | URXD_PRERR);
	if (termios->c_iflag & (BRKINT | PARMRK))
		sport->port.read_status_mask |= URXD_BRK;

	/*
	 * Characters to ignore
	 */
	sport->port.ignore_status_mask = 0;
	if (termios->c_iflag & IGNPAR)
		sport->port.ignore_status_mask |= URXD_PRERR;
	if (termios->c_iflag & IGNBRK) {
		sport->port.ignore_status_mask |= URXD_BRK;
		/*
		 * If we're ignoring parity and break indicators,
		 * ignore overruns too (for real raw support).
		 */
		if (termios->c_iflag & IGNPAR)
			sport->port.ignore_status_mask |= URXD_OVRRUN;
	}

	del_timer_sync(&sport->timer);

	/*
	 * Update the per-port timeout.
	 */
	uart_update_timeout(port, termios->c_cflag, baud);

	/*
	 * disable interrupts and drain transmitter
	 */
	old_ucr1 = readl(sport->port.membase + UCR1);
	writel(old_ucr1 & ~(UCR1_TXMPTYEN | UCR1_RRDYEN | UCR1_RTSDEN),
			sport->port.membase + UCR1);

	while ( !(readl(sport->port.membase + USR2) & USR2_TXDC))
		barrier();

	/* then, disable everything */
	old_txrxen = readl(sport->port.membase + UCR2);
	writel(old_txrxen & ~( UCR2_TXEN | UCR2_RXEN),
			sport->port.membase + UCR2);
	old_txrxen &= (UCR2_TXEN | UCR2_RXEN);

	if (USE_IRDA(sport)) {
		/*
		 * use maximum available submodule frequency to
		 * avoid missing short pulses due to low sampling rate
		 */
		div = 1;
	} else {
		div = sport->port.uartclk / (baud * 16);
		if (div > 7)
			div = 7;
		if (!div)
			div = 1;
	}

	rational_best_approximation(16 * div * baud, sport->port.uartclk,
		1 << 16, 1 << 16, &num, &denom);

	if (port->state && port->state->port.tty) {
		tdiv64 = sport->port.uartclk;
		tdiv64 *= num;
		do_div(tdiv64, denom * 16 * div);
		tty_encode_baud_rate(sport->port.state->port.tty,
				(speed_t)tdiv64, (speed_t)tdiv64);
	}

	num -= 1;
	denom -= 1;

	ufcr = readl(sport->port.membase + UFCR);
	ufcr = (ufcr & (~UFCR_RFDIV)) | UFCR_RFDIV_REG(div);
	writel(ufcr, sport->port.membase + UFCR);

	writel(num, sport->port.membase + UBIR);
	writel(denom, sport->port.membase + UBMR);

	if (!cpu_is_mx1())
		writel(sport->port.uartclk / div / 1000,
				sport->port.membase + MX2_ONEMS);

	writel(old_ucr1, sport->port.membase + UCR1);

	/* set the parity, stop bits and data size */
	writel(ucr2 | old_txrxen, sport->port.membase + UCR2);

	if (UART_ENABLE_MS(&sport->port, termios->c_cflag))
		imx_enable_ms(&sport->port);

	spin_unlock_irqrestore(&sport->port.lock, flags);
}

static const char *imx_type(struct uart_port *port)
{
	struct imx_port *sport = (struct imx_port *)port;

	return sport->port.type == PORT_IMX ? "IMX" : NULL;
}

/*
 * Release the memory region(s) being used by 'port'.
 */
static void imx_release_port(struct uart_port *port)
{
	struct platform_device *pdev = to_platform_device(port->dev);
	struct resource *mmres;

	mmres = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	release_mem_region(mmres->start, mmres->end - mmres->start + 1);
}

/*
 * Request the memory region(s) being used by 'port'.
 */
static int imx_request_port(struct uart_port *port)
{
	struct platform_device *pdev = to_platform_device(port->dev);
	struct resource *mmres;
	void *ret;

	mmres = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!mmres)
		return -ENODEV;

	ret = request_mem_region(mmres->start, mmres->end - mmres->start + 1,
			"imx-uart");

	return  ret ? 0 : -EBUSY;
}

/*
 * Configure/autoconfigure the port.
 */
static void imx_config_port(struct uart_port *port, int flags)
{
	struct imx_port *sport = (struct imx_port *)port;

	if (flags & UART_CONFIG_TYPE &&
	    imx_request_port(&sport->port) == 0)
		sport->port.type = PORT_IMX;
}

/*
 * Verify the new serial_struct (for TIOCSSERIAL).
 * The only change we allow are to the flags and type, and
 * even then only between PORT_IMX and PORT_UNKNOWN
 */
static int
imx_verify_port(struct uart_port *port, struct serial_struct *ser)
{
	struct imx_port *sport = (struct imx_port *)port;
	int ret = 0;

	if (ser->type != PORT_UNKNOWN && ser->type != PORT_IMX)
		ret = -EINVAL;
	if (sport->port.irq != ser->irq)
		ret = -EINVAL;
	if (ser->io_type != UPIO_MEM)
		ret = -EINVAL;
	if (sport->port.uartclk / 16 != ser->baud_base)
		ret = -EINVAL;
	if ((void *)sport->port.mapbase != ser->iomem_base)
		ret = -EINVAL;
	if (sport->port.iobase != ser->port)
		ret = -EINVAL;
	if (ser->hub6 != 0)
		ret = -EINVAL;
	return ret;
}

static struct uart_ops imx_pops = {
	.tx_empty	= imx_tx_empty,
	.set_mctrl	= imx_set_mctrl,
	.get_mctrl	= imx_get_mctrl,
	.stop_tx	= imx_stop_tx,
	.start_tx	= imx_start_tx,
	.stop_rx	= imx_stop_rx,
	.enable_ms	= imx_enable_ms,
	.break_ctl	= imx_break_ctl,
	.startup	= imx_startup,
	.shutdown	= imx_shutdown,
	.set_termios	= imx_set_termios,
	.type		= imx_type,
	.release_port	= imx_release_port,
	.request_port	= imx_request_port,
	.config_port	= imx_config_port,
	.verify_port	= imx_verify_port,
};

static struct imx_port *imx_ports[UART_NR];

#ifdef CONFIG_SERIAL_IMX_CONSOLE
static void imx_console_putchar(struct uart_port *port, int ch)
{
	struct imx_port *sport = (struct imx_port *)port;

	while (readl(sport->port.membase + UTS) & UTS_TXFULL)
		barrier();

	writel(ch, sport->port.membase + URTX0);
}

/*
 * Interrupts are disabled on entering
 */
static void
imx_console_write(struct console *co, const char *s, unsigned int count)
{
	struct imx_port *sport = imx_ports[co->index];
	unsigned int old_ucr1, old_ucr2, ucr1;

	/*
	 *	First, save UCR1/2 and then disable interrupts
	 */
	ucr1 = old_ucr1 = readl(sport->port.membase + UCR1);
	old_ucr2 = readl(sport->port.membase + UCR2);

	if (cpu_is_mx1())
		ucr1 |= MX1_UCR1_UARTCLKEN;
	ucr1 |= UCR1_UARTEN;
	ucr1 &= ~(UCR1_TXMPTYEN | UCR1_RRDYEN | UCR1_RTSDEN);

	writel(ucr1, sport->port.membase + UCR1);

	writel(old_ucr2 | UCR2_TXEN, sport->port.membase + UCR2);

	uart_console_write(&sport->port, s, count, imx_console_putchar);

	/*
	 *	Finally, wait for transmitter to become empty
	 *	and restore UCR1/2
	 */
	while (!(readl(sport->port.membase + USR2) & USR2_TXDC));

	writel(old_ucr1, sport->port.membase + UCR1);
	writel(old_ucr2, sport->port.membase + UCR2);
}

/*
 * If the port was already initialised (eg, by a boot loader),
 * try to determine the current setup.
 */
static void __init
imx_console_get_options(struct imx_port *sport, int *baud,
			   int *parity, int *bits)
{

	if (readl(sport->port.membase + UCR1) & UCR1_UARTEN) {
		/* ok, the port was enabled */
		unsigned int ucr2, ubir,ubmr, uartclk;
		unsigned int baud_raw;
		unsigned int ucfr_rfdiv;

		ucr2 = readl(sport->port.membase + UCR2);

		*parity = 'n';
		if (ucr2 & UCR2_PREN) {
			if (ucr2 & UCR2_PROE)
				*parity = 'o';
			else
				*parity = 'e';
		}

		if (ucr2 & UCR2_WS)
			*bits = 8;
		else
			*bits = 7;

		ubir = readl(sport->port.membase + UBIR) & 0xffff;
		ubmr = readl(sport->port.membase + UBMR) & 0xffff;

		ucfr_rfdiv = (readl(sport->port.membase + UFCR) & UFCR_RFDIV) >> 7;
		if (ucfr_rfdiv == 6)
			ucfr_rfdiv = 7;
		else
			ucfr_rfdiv = 6 - ucfr_rfdiv;

		uartclk = clk_get_rate(sport->clk);
		uartclk /= ucfr_rfdiv;

		{	/*
			 * The next code provides exact computation of
			 *   baud_raw = round(((uartclk/16) * (ubir + 1)) / (ubmr + 1))
			 * without need of float support or long long division,
			 * which would be required to prevent 32bit arithmetic overflow
			 */
			unsigned int mul = ubir + 1;
			unsigned int div = 16 * (ubmr + 1);
			unsigned int rem = uartclk % div;

			baud_raw = (uartclk / div) * mul;
			baud_raw += (rem * mul + div / 2) / div;
			*baud = (baud_raw + 50) / 100 * 100;
		}

		if(*baud != baud_raw)
			printk(KERN_INFO "Serial: Console IMX rounded baud rate from %d to %d\n",
				baud_raw, *baud);
	}
}

static int __init
imx_console_setup(struct console *co, char *options)
{
	struct imx_port *sport;
	int baud = 9600;
	int bits = 8;
	int parity = 'n';
	int flow = 'n';

	/*
	 * Check whether an invalid uart number has been specified, and
	 * if so, search for the first available port that does have
	 * console support.
	 */
	if (co->index == -1 || co->index >= ARRAY_SIZE(imx_ports))
		co->index = 0;
	sport = imx_ports[co->index];
	if(sport == NULL)
		return -ENODEV;

	if (options)
		uart_parse_options(options, &baud, &parity, &bits, &flow);
	else
		imx_console_get_options(sport, &baud, &parity, &bits);

	imx_setup_ufcr(sport, 0);

	return uart_set_options(&sport->port, co, baud, parity, bits, flow);
}

static struct uart_driver imx_reg;
static struct console imx_console = {
	.name		= DEV_NAME,
	.write		= imx_console_write,
	.device		= uart_console_device,
	.setup		= imx_console_setup,
	.flags		= CON_PRINTBUFFER,
	.index		= -1,
	.data		= &imx_reg,
};

#define IMX_CONSOLE	&imx_console
#else
#define IMX_CONSOLE	NULL
#endif

static struct uart_driver imx_reg = {
	.owner          = THIS_MODULE,
	.driver_name    = DRIVER_NAME,
	.dev_name       = DEV_NAME,
	.major          = SERIAL_IMX_MAJOR,
	.minor          = MINOR_START,
	.nr             = ARRAY_SIZE(imx_ports),
	.cons           = IMX_CONSOLE,
};

static int serial_imx_suspend(struct platform_device *dev, pm_message_t state)
{
	struct imx_port *sport = platform_get_drvdata(dev);

	if (sport)
		uart_suspend_port(&imx_reg, &sport->port);

	return 0;
}

static int serial_imx_resume(struct platform_device *dev)
{
	struct imx_port *sport = platform_get_drvdata(dev);

	if (sport)
		uart_resume_port(&imx_reg, &sport->port);

	return 0;
}

static int serial_imx_probe(struct platform_device *pdev)
{
	struct imx_port *sport;
	struct imxuart_platform_data *pdata;
	void __iomem *base;
	int ret = 0;
	struct resource *res;

	sport = kzalloc(sizeof(*sport), GFP_KERNEL);
	if (!sport)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		ret = -ENODEV;
		goto free;
	}

	base = ioremap(res->start, PAGE_SIZE);
	if (!base) {
		ret = -ENOMEM;
		goto free;
	}

	sport->port.dev = &pdev->dev;
	sport->port.mapbase = res->start;
	sport->port.membase = base;
	sport->port.type = PORT_IMX,
	sport->port.iotype = UPIO_MEM;
	sport->port.irq = platform_get_irq(pdev, 0);
	sport->rxirq = platform_get_irq(pdev, 0);
	sport->txirq = platform_get_irq(pdev, 1);
	sport->rtsirq = platform_get_irq(pdev, 2);
	sport->port.fifosize = 32;
	sport->port.ops = &imx_pops;
	sport->port.flags = UPF_BOOT_AUTOCONF;
	sport->port.line = pdev->id;
	init_timer(&sport->timer);
	sport->timer.function = imx_timeout;
	sport->timer.data     = (unsigned long)sport;

	sport->clk = clk_get(&pdev->dev, "uart");
	if (IS_ERR(sport->clk)) {
		ret = PTR_ERR(sport->clk);
		goto unmap;
	}
	clk_enable(sport->clk);

	sport->port.uartclk = clk_get_rate(sport->clk);

	imx_ports[pdev->id] = sport;

	pdata = pdev->dev.platform_data;
	if (pdata && (pdata->flags & IMXUART_HAVE_RTSCTS))
		sport->have_rtscts = 1;

#ifdef CONFIG_IRDA
	if (pdata && (pdata->flags & IMXUART_IRDA))
		sport->use_irda = 1;
#endif

	if (pdata && pdata->init) {
		ret = pdata->init(pdev);
		if (ret)
			goto clkput;
	}

	ret = uart_add_one_port(&imx_reg, &sport->port);
	if (ret)
		goto deinit;
	platform_set_drvdata(pdev, &sport->port);

	return 0;
deinit:
	if (pdata && pdata->exit)
		pdata->exit(pdev);
clkput:
	clk_put(sport->clk);
	clk_disable(sport->clk);
unmap:
	iounmap(sport->port.membase);
free:
	kfree(sport);

	return ret;
}

static int serial_imx_remove(struct platform_device *pdev)
{
	struct imxuart_platform_data *pdata;
	struct imx_port *sport = platform_get_drvdata(pdev);

	pdata = pdev->dev.platform_data;

	platform_set_drvdata(pdev, NULL);

	if (sport) {
		uart_remove_one_port(&imx_reg, &sport->port);
		clk_put(sport->clk);
	}

	clk_disable(sport->clk);

	if (pdata && pdata->exit)
		pdata->exit(pdev);

	iounmap(sport->port.membase);
	kfree(sport);

	return 0;
}

static struct platform_driver serial_imx_driver = {
	.probe		= serial_imx_probe,
	.remove		= serial_imx_remove,

	.suspend	= serial_imx_suspend,
	.resume		= serial_imx_resume,
	.driver		= {
		.name	= "imx-uart",
		.owner	= THIS_MODULE,
	},
};

static int __init imx_serial_init(void)
{
	int ret;

	printk(KERN_INFO "Serial: IMX driver\n");

	ret = uart_register_driver(&imx_reg);
	if (ret)
		return ret;

	ret = platform_driver_register(&serial_imx_driver);
	if (ret != 0)
		uart_unregister_driver(&imx_reg);

	return 0;
}

static void __exit imx_serial_exit(void)
{
	platform_driver_unregister(&serial_imx_driver);
	uart_unregister_driver(&imx_reg);
}

module_init(imx_serial_init);
module_exit(imx_serial_exit);

MODULE_AUTHOR("Sascha Hauer");
MODULE_DESCRIPTION("IMX generic serial port driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:imx-uart");
