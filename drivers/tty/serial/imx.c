// SPDX-License-Identifier: GPL-2.0+
/*
 * Driver for Motorola/Freescale IMX serial ports
 *
 * Based on drivers/char/serial.c, by Linus Torvalds, Theodore Ts'o.
 *
 * Author: Sascha Hauer <sascha@saschahauer.de>
 * Copyright (C) 2004 Pengutronix
 */

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
#include <linux/ktime.h>
#include <linux/pinctrl/consumer.h>
#include <linux/rational.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/io.h>
#include <linux/dma-mapping.h>

#include <asm/irq.h>
#include <linux/dma/imx-dma.h>

#include "serial_mctrl_gpio.h"

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
#define IMX21_ONEMS 0xb0 /* One Millisecond register */
#define IMX1_UTS 0xd0 /* UART Test Register on i.mx1 */
#define IMX21_UTS 0xb4 /* UART Test Register on all other i.mx*/

/* UART Control Register Bit Fields.*/
#define URXD_DUMMY_READ (1<<16)
#define URXD_CHARRDY	(1<<15)
#define URXD_ERR	(1<<14)
#define URXD_OVRRUN	(1<<13)
#define URXD_FRMERR	(1<<12)
#define URXD_BRK	(1<<11)
#define URXD_PRERR	(1<<10)
#define URXD_RX_DATA	(0xFF<<0)
#define UCR1_ADEN	(1<<15) /* Auto detect interrupt */
#define UCR1_ADBR	(1<<14) /* Auto detect baud rate */
#define UCR1_TRDYEN	(1<<13) /* Transmitter ready interrupt enable */
#define UCR1_IDEN	(1<<12) /* Idle condition interrupt */
#define UCR1_ICD_REG(x) (((x) & 3) << 10) /* idle condition detect */
#define UCR1_RRDYEN	(1<<9)	/* Recv ready interrupt enable */
#define UCR1_RXDMAEN	(1<<8)	/* Recv ready DMA enable */
#define UCR1_IREN	(1<<7)	/* Infrared interface enable */
#define UCR1_TXMPTYEN	(1<<6)	/* Transimitter empty interrupt enable */
#define UCR1_RTSDEN	(1<<5)	/* RTS delta interrupt enable */
#define UCR1_SNDBRK	(1<<4)	/* Send break */
#define UCR1_TXDMAEN	(1<<3)	/* Transmitter ready DMA enable */
#define IMX1_UCR1_UARTCLKEN (1<<2) /* UART clock enabled, i.mx1 only */
#define UCR1_ATDMAEN    (1<<2)  /* Aging DMA Timer Enable */
#define UCR1_DOZE	(1<<1)	/* Doze */
#define UCR1_UARTEN	(1<<0)	/* UART enabled */
#define UCR2_ESCI	(1<<15)	/* Escape seq interrupt enable */
#define UCR2_IRTS	(1<<14)	/* Ignore RTS pin */
#define UCR2_CTSC	(1<<13)	/* CTS pin control */
#define UCR2_CTS	(1<<12)	/* Clear to send */
#define UCR2_ESCEN	(1<<11)	/* Escape enable */
#define UCR2_PREN	(1<<8)	/* Parity enable */
#define UCR2_PROE	(1<<7)	/* Parity odd/even */
#define UCR2_STPB	(1<<6)	/* Stop */
#define UCR2_WS		(1<<5)	/* Word size */
#define UCR2_RTSEN	(1<<4)	/* Request to send interrupt enable */
#define UCR2_ATEN	(1<<3)	/* Aging Timer Enable */
#define UCR2_TXEN	(1<<2)	/* Transmitter enabled */
#define UCR2_RXEN	(1<<1)	/* Receiver enabled */
#define UCR2_SRST	(1<<0)	/* SW reset */
#define UCR3_DTREN	(1<<13) /* DTR interrupt enable */
#define UCR3_PARERREN	(1<<12) /* Parity enable */
#define UCR3_FRAERREN	(1<<11) /* Frame error interrupt enable */
#define UCR3_DSR	(1<<10) /* Data set ready */
#define UCR3_DCD	(1<<9)	/* Data carrier detect */
#define UCR3_RI		(1<<8)	/* Ring indicator */
#define UCR3_ADNIMP	(1<<7)	/* Autobaud Detection Not Improved */
#define UCR3_RXDSEN	(1<<6)	/* Receive status interrupt enable */
#define UCR3_AIRINTEN	(1<<5)	/* Async IR wake interrupt enable */
#define UCR3_AWAKEN	(1<<4)	/* Async wake interrupt enable */
#define UCR3_DTRDEN	(1<<3)	/* Data Terminal Ready Delta Enable. */
#define IMX21_UCR3_RXDMUXSEL	(1<<2)	/* RXD Muxed Input Select */
#define UCR3_INVT	(1<<1)	/* Inverted Infrared transmission */
#define UCR3_BPEN	(1<<0)	/* Preset registers enable */
#define UCR4_CTSTL_SHF	10	/* CTS trigger level shift */
#define UCR4_CTSTL_MASK	0x3F	/* CTS trigger is 6 bits wide */
#define UCR4_INVR	(1<<9)	/* Inverted infrared reception */
#define UCR4_ENIRI	(1<<8)	/* Serial infrared interrupt enable */
#define UCR4_WKEN	(1<<7)	/* Wake interrupt enable */
#define UCR4_REF16	(1<<6)	/* Ref freq 16 MHz */
#define UCR4_IDDMAEN    (1<<6)  /* DMA IDLE Condition Detected */
#define UCR4_IRSC	(1<<5)	/* IR special case */
#define UCR4_TCEN	(1<<3)	/* Transmit complete interrupt enable */
#define UCR4_BKEN	(1<<2)	/* Break condition interrupt enable */
#define UCR4_OREN	(1<<1)	/* Receiver overrun interrupt enable */
#define UCR4_DREN	(1<<0)	/* Recv data ready interrupt enable */
#define UFCR_RXTL_SHF	0	/* Receiver trigger level shift */
#define UFCR_DCEDTE	(1<<6)	/* DCE/DTE mode select */
#define UFCR_RFDIV	(7<<7)	/* Reference freq divider mask */
#define UFCR_RFDIV_REG(x)	(((x) < 7 ? 6 - (x) : 6) << 7)
#define UFCR_TXTL_SHF	10	/* Transmitter trigger level shift */
#define USR1_PARITYERR	(1<<15) /* Parity error interrupt flag */
#define USR1_RTSS	(1<<14) /* RTS pin status */
#define USR1_TRDY	(1<<13) /* Transmitter ready interrupt/dma flag */
#define USR1_RTSD	(1<<12) /* RTS delta */
#define USR1_ESCF	(1<<11) /* Escape seq interrupt flag */
#define USR1_FRAMERR	(1<<10) /* Frame error interrupt flag */
#define USR1_RRDY	(1<<9)	 /* Receiver ready interrupt/dma flag */
#define USR1_AGTIM	(1<<8)	 /* Ageing timer interrupt flag */
#define USR1_DTRD	(1<<7)	 /* DTR Delta */
#define USR1_RXDS	 (1<<6)	 /* Receiver idle interrupt flag */
#define USR1_AIRINT	 (1<<5)	 /* Async IR wake interrupt flag */
#define USR1_AWAKE	 (1<<4)	 /* Aysnc wake interrupt flag */
#define USR2_ADET	 (1<<15) /* Auto baud rate detect complete */
#define USR2_TXFE	 (1<<14) /* Transmit buffer FIFO empty */
#define USR2_DTRF	 (1<<13) /* DTR edge interrupt flag */
#define USR2_IDLE	 (1<<12) /* Idle condition */
#define USR2_RIDELT	 (1<<10) /* Ring Interrupt Delta */
#define USR2_RIIN	 (1<<9)	 /* Ring Indicator Input */
#define USR2_IRINT	 (1<<8)	 /* Serial infrared interrupt flag */
#define USR2_WAKE	 (1<<7)	 /* Wake */
#define USR2_DCDIN	 (1<<5)	 /* Data Carrier Detect Input */
#define USR2_RTSF	 (1<<4)	 /* RTS edge interrupt flag */
#define USR2_TXDC	 (1<<3)	 /* Transmitter complete */
#define USR2_BRCD	 (1<<2)	 /* Break condition */
#define USR2_ORE	(1<<1)	 /* Overrun error */
#define USR2_RDR	(1<<0)	 /* Recv data ready */
#define UTS_FRCPERR	(1<<13) /* Force parity error */
#define UTS_LOOP	(1<<12)	 /* Loop tx and rx */
#define UTS_TXEMPTY	 (1<<6)	 /* TxFIFO empty */
#define UTS_RXEMPTY	 (1<<5)	 /* RxFIFO empty */
#define UTS_TXFULL	 (1<<4)	 /* TxFIFO full */
#define UTS_RXFULL	 (1<<3)	 /* RxFIFO full */
#define UTS_SOFTRST	 (1<<0)	 /* Software reset */

/* We've been assigned a range on the "Low-density serial ports" major */
#define SERIAL_IMX_MAJOR	207
#define MINOR_START		16
#define DEV_NAME		"ttymxc"

/*
 * This determines how often we check the modem status signals
 * for any change.  They generally aren't connected to an IRQ
 * so we have to poll them.  We also check immediately before
 * filling the TX fifo incase CTS has been dropped.
 */
#define MCTRL_TIMEOUT	(250*HZ/1000)

#define DRIVER_NAME "IMX-uart"

#define UART_NR 8

/* i.MX21 type uart runs on all i.mx except i.MX1 and i.MX6q */
enum imx_uart_type {
	IMX1_UART,
	IMX21_UART,
	IMX53_UART,
	IMX6Q_UART,
};

/* device type dependent stuff */
struct imx_uart_data {
	unsigned uts_reg;
	enum imx_uart_type devtype;
};

enum imx_tx_state {
	OFF,
	WAIT_AFTER_RTS,
	SEND,
	WAIT_AFTER_SEND,
};

struct imx_port {
	struct uart_port	port;
	struct timer_list	timer;
	unsigned int		old_status;
	unsigned int		have_rtscts:1;
	unsigned int		have_rtsgpio:1;
	unsigned int		dte_mode:1;
	unsigned int		inverted_tx:1;
	unsigned int		inverted_rx:1;
	struct clk		*clk_ipg;
	struct clk		*clk_per;
	const struct imx_uart_data *devdata;

	struct mctrl_gpios *gpios;

	/* counter to stop 0xff flood */
	int idle_counter;

	/* DMA fields */
	unsigned int		dma_is_enabled:1;
	unsigned int		dma_is_rxing:1;
	unsigned int		dma_is_txing:1;
	struct dma_chan		*dma_chan_rx, *dma_chan_tx;
	struct scatterlist	rx_sgl, tx_sgl[2];
	void			*rx_buf;
	struct circ_buf		rx_ring;
	unsigned int		rx_buf_size;
	unsigned int		rx_period_length;
	unsigned int		rx_periods;
	dma_cookie_t		rx_cookie;
	unsigned int		tx_bytes;
	unsigned int		dma_tx_nents;
	unsigned int            saved_reg[10];
	bool			context_saved;

	enum imx_tx_state	tx_state;
	struct hrtimer		trigger_start_tx;
	struct hrtimer		trigger_stop_tx;
};

struct imx_port_ucrs {
	unsigned int	ucr1;
	unsigned int	ucr2;
	unsigned int	ucr3;
};

static struct imx_uart_data imx_uart_devdata[] = {
	[IMX1_UART] = {
		.uts_reg = IMX1_UTS,
		.devtype = IMX1_UART,
	},
	[IMX21_UART] = {
		.uts_reg = IMX21_UTS,
		.devtype = IMX21_UART,
	},
	[IMX53_UART] = {
		.uts_reg = IMX21_UTS,
		.devtype = IMX53_UART,
	},
	[IMX6Q_UART] = {
		.uts_reg = IMX21_UTS,
		.devtype = IMX6Q_UART,
	},
};

static const struct of_device_id imx_uart_dt_ids[] = {
	{ .compatible = "fsl,imx6q-uart", .data = &imx_uart_devdata[IMX6Q_UART], },
	{ .compatible = "fsl,imx53-uart", .data = &imx_uart_devdata[IMX53_UART], },
	{ .compatible = "fsl,imx1-uart", .data = &imx_uart_devdata[IMX1_UART], },
	{ .compatible = "fsl,imx21-uart", .data = &imx_uart_devdata[IMX21_UART], },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, imx_uart_dt_ids);

static inline void imx_uart_writel(struct imx_port *sport, u32 val, u32 offset)
{
	writel(val, sport->port.membase + offset);
}

static inline u32 imx_uart_readl(struct imx_port *sport, u32 offset)
{
	return readl(sport->port.membase + offset);
}

static inline unsigned imx_uart_uts_reg(struct imx_port *sport)
{
	return sport->devdata->uts_reg;
}

static inline int imx_uart_is_imx1(struct imx_port *sport)
{
	return sport->devdata->devtype == IMX1_UART;
}

/*
 * Save and restore functions for UCR1, UCR2 and UCR3 registers
 */
#if IS_ENABLED(CONFIG_SERIAL_IMX_CONSOLE)
static void imx_uart_ucrs_save(struct imx_port *sport,
			       struct imx_port_ucrs *ucr)
{
	/* save control registers */
	ucr->ucr1 = imx_uart_readl(sport, UCR1);
	ucr->ucr2 = imx_uart_readl(sport, UCR2);
	ucr->ucr3 = imx_uart_readl(sport, UCR3);
}

static void imx_uart_ucrs_restore(struct imx_port *sport,
				  struct imx_port_ucrs *ucr)
{
	/* restore control registers */
	imx_uart_writel(sport, ucr->ucr1, UCR1);
	imx_uart_writel(sport, ucr->ucr2, UCR2);
	imx_uart_writel(sport, ucr->ucr3, UCR3);
}
#endif

/* called with port.lock taken and irqs caller dependent */
static void imx_uart_rts_active(struct imx_port *sport, u32 *ucr2)
{
	*ucr2 &= ~(UCR2_CTSC | UCR2_CTS);

	mctrl_gpio_set(sport->gpios, sport->port.mctrl | TIOCM_RTS);
}

/* called with port.lock taken and irqs caller dependent */
static void imx_uart_rts_inactive(struct imx_port *sport, u32 *ucr2)
{
	*ucr2 &= ~UCR2_CTSC;
	*ucr2 |= UCR2_CTS;

	mctrl_gpio_set(sport->gpios, sport->port.mctrl & ~TIOCM_RTS);
}

static void start_hrtimer_ms(struct hrtimer *hrt, unsigned long msec)
{
       hrtimer_start(hrt, ms_to_ktime(msec), HRTIMER_MODE_REL);
}

/* called with port.lock taken and irqs off */
static void imx_uart_soft_reset(struct imx_port *sport)
{
	int i = 10;
	u32 ucr2, ubir, ubmr, uts;

	/*
	 * According to the Reference Manual description of the UART SRST bit:
	 *
	 * "Reset the transmit and receive state machines,
	 * all FIFOs and register USR1, USR2, UBIR, UBMR, UBRC, URXD, UTXD
	 * and UTS[6-3]".
	 *
	 * We don't need to restore the old values from USR1, USR2, URXD and
	 * UTXD. UBRC is read only, so only save/restore the other three
	 * registers.
	 */
	ubir = imx_uart_readl(sport, UBIR);
	ubmr = imx_uart_readl(sport, UBMR);
	uts = imx_uart_readl(sport, IMX21_UTS);

	ucr2 = imx_uart_readl(sport, UCR2);
	imx_uart_writel(sport, ucr2 & ~UCR2_SRST, UCR2);

	while (!(imx_uart_readl(sport, UCR2) & UCR2_SRST) && (--i > 0))
		udelay(1);

	/* Restore the registers */
	imx_uart_writel(sport, ubir, UBIR);
	imx_uart_writel(sport, ubmr, UBMR);
	imx_uart_writel(sport, uts, IMX21_UTS);

	sport->idle_counter = 0;
}

static void imx_uart_disable_loopback_rs485(struct imx_port *sport)
{
	unsigned int uts;

	/* See SER_RS485_ENABLED/UTS_LOOP comment in imx_uart_probe() */
	uts = imx_uart_readl(sport, imx_uart_uts_reg(sport));
	uts &= ~UTS_LOOP;
	imx_uart_writel(sport, uts, imx_uart_uts_reg(sport));
}

/* called with port.lock taken and irqs off */
static void imx_uart_start_rx(struct uart_port *port)
{
	struct imx_port *sport = (struct imx_port *)port;
	unsigned int ucr1, ucr2;

	ucr1 = imx_uart_readl(sport, UCR1);
	ucr2 = imx_uart_readl(sport, UCR2);

	ucr2 |= UCR2_RXEN;

	if (sport->dma_is_enabled) {
		ucr1 |= UCR1_RXDMAEN | UCR1_ATDMAEN;
	} else {
		ucr1 |= UCR1_RRDYEN;
		ucr2 |= UCR2_ATEN;
	}

	/* Write UCR2 first as it includes RXEN */
	imx_uart_writel(sport, ucr2, UCR2);
	imx_uart_writel(sport, ucr1, UCR1);
	imx_uart_disable_loopback_rs485(sport);
}

/* called with port.lock taken and irqs off */
static void imx_uart_stop_tx(struct uart_port *port)
{
	struct imx_port *sport = (struct imx_port *)port;
	u32 ucr1, ucr4, usr2;

	if (sport->tx_state == OFF)
		return;

	/*
	 * We are maybe in the SMP context, so if the DMA TX thread is running
	 * on other cpu, we have to wait for it to finish.
	 */
	if (sport->dma_is_txing)
		return;

	ucr1 = imx_uart_readl(sport, UCR1);
	imx_uart_writel(sport, ucr1 & ~UCR1_TRDYEN, UCR1);

	ucr4 = imx_uart_readl(sport, UCR4);
	usr2 = imx_uart_readl(sport, USR2);
	if ((!(usr2 & USR2_TXDC)) && (ucr4 & UCR4_TCEN)) {
		/* The shifter is still busy, so retry once TC triggers */
		return;
	}

	ucr4 &= ~UCR4_TCEN;
	imx_uart_writel(sport, ucr4, UCR4);

	/* in rs485 mode disable transmitter */
	if (port->rs485.flags & SER_RS485_ENABLED) {
		if (sport->tx_state == SEND) {
			sport->tx_state = WAIT_AFTER_SEND;

			if (port->rs485.delay_rts_after_send > 0) {
				start_hrtimer_ms(&sport->trigger_stop_tx,
					 port->rs485.delay_rts_after_send);
				return;
			}

			/* continue without any delay */
		}

		if (sport->tx_state == WAIT_AFTER_RTS ||
		    sport->tx_state == WAIT_AFTER_SEND) {
			u32 ucr2;

			hrtimer_try_to_cancel(&sport->trigger_start_tx);

			ucr2 = imx_uart_readl(sport, UCR2);
			if (port->rs485.flags & SER_RS485_RTS_AFTER_SEND)
				imx_uart_rts_active(sport, &ucr2);
			else
				imx_uart_rts_inactive(sport, &ucr2);
			imx_uart_writel(sport, ucr2, UCR2);

			if (!port->rs485_rx_during_tx_gpio)
				imx_uart_start_rx(port);

			sport->tx_state = OFF;
		}
	} else {
		sport->tx_state = OFF;
	}
}

/* called with port.lock taken and irqs off */
static void imx_uart_stop_rx(struct uart_port *port)
{
	struct imx_port *sport = (struct imx_port *)port;
	u32 ucr1, ucr2, ucr4, uts;

	ucr1 = imx_uart_readl(sport, UCR1);
	ucr2 = imx_uart_readl(sport, UCR2);
	ucr4 = imx_uart_readl(sport, UCR4);

	if (sport->dma_is_enabled) {
		ucr1 &= ~(UCR1_RXDMAEN | UCR1_ATDMAEN);
	} else {
		ucr1 &= ~UCR1_RRDYEN;
		ucr2 &= ~UCR2_ATEN;
		ucr4 &= ~UCR4_OREN;
	}
	imx_uart_writel(sport, ucr1, UCR1);
	imx_uart_writel(sport, ucr4, UCR4);

	/* See SER_RS485_ENABLED/UTS_LOOP comment in imx_uart_probe() */
	if (port->rs485.flags & SER_RS485_ENABLED &&
	    port->rs485.flags & SER_RS485_RTS_ON_SEND &&
	    sport->have_rtscts && !sport->have_rtsgpio) {
		uts = imx_uart_readl(sport, imx_uart_uts_reg(sport));
		uts |= UTS_LOOP;
		imx_uart_writel(sport, uts, imx_uart_uts_reg(sport));
		ucr2 |= UCR2_RXEN;
	} else {
		ucr2 &= ~UCR2_RXEN;
	}

	imx_uart_writel(sport, ucr2, UCR2);
}

/* called with port.lock taken and irqs off */
static void imx_uart_enable_ms(struct uart_port *port)
{
	struct imx_port *sport = (struct imx_port *)port;

	mod_timer(&sport->timer, jiffies);

	mctrl_gpio_enable_ms(sport->gpios);
}

static void imx_uart_dma_tx(struct imx_port *sport);

/* called with port.lock taken and irqs off */
static inline void imx_uart_transmit_buffer(struct imx_port *sport)
{
	struct circ_buf *xmit = &sport->port.state->xmit;

	if (sport->port.x_char) {
		/* Send next char */
		imx_uart_writel(sport, sport->port.x_char, URTX0);
		sport->port.icount.tx++;
		sport->port.x_char = 0;
		return;
	}

	if (uart_circ_empty(xmit) || uart_tx_stopped(&sport->port)) {
		imx_uart_stop_tx(&sport->port);
		return;
	}

	if (sport->dma_is_enabled) {
		u32 ucr1;
		/*
		 * We've just sent a X-char Ensure the TX DMA is enabled
		 * and the TX IRQ is disabled.
		 **/
		ucr1 = imx_uart_readl(sport, UCR1);
		ucr1 &= ~UCR1_TRDYEN;
		if (sport->dma_is_txing) {
			ucr1 |= UCR1_TXDMAEN;
			imx_uart_writel(sport, ucr1, UCR1);
		} else {
			imx_uart_writel(sport, ucr1, UCR1);
			imx_uart_dma_tx(sport);
		}

		return;
	}

	while (!uart_circ_empty(xmit) &&
	       !(imx_uart_readl(sport, imx_uart_uts_reg(sport)) & UTS_TXFULL)) {
		/* send xmit->buf[xmit->tail]
		 * out the port here */
		imx_uart_writel(sport, xmit->buf[xmit->tail], URTX0);
		uart_xmit_advance(&sport->port, 1);
	}

	if (uart_circ_chars_pending(xmit) < WAKEUP_CHARS)
		uart_write_wakeup(&sport->port);

	if (uart_circ_empty(xmit))
		imx_uart_stop_tx(&sport->port);
}

static void imx_uart_dma_tx_callback(void *data)
{
	struct imx_port *sport = data;
	struct scatterlist *sgl = &sport->tx_sgl[0];
	struct circ_buf *xmit = &sport->port.state->xmit;
	unsigned long flags;
	u32 ucr1;

	spin_lock_irqsave(&sport->port.lock, flags);

	dma_unmap_sg(sport->port.dev, sgl, sport->dma_tx_nents, DMA_TO_DEVICE);

	ucr1 = imx_uart_readl(sport, UCR1);
	ucr1 &= ~UCR1_TXDMAEN;
	imx_uart_writel(sport, ucr1, UCR1);

	uart_xmit_advance(&sport->port, sport->tx_bytes);

	dev_dbg(sport->port.dev, "we finish the TX DMA.\n");

	sport->dma_is_txing = 0;

	if (uart_circ_chars_pending(xmit) < WAKEUP_CHARS)
		uart_write_wakeup(&sport->port);

	if (!uart_circ_empty(xmit) && !uart_tx_stopped(&sport->port))
		imx_uart_dma_tx(sport);
	else if (sport->port.rs485.flags & SER_RS485_ENABLED) {
		u32 ucr4 = imx_uart_readl(sport, UCR4);
		ucr4 |= UCR4_TCEN;
		imx_uart_writel(sport, ucr4, UCR4);
	}

	spin_unlock_irqrestore(&sport->port.lock, flags);
}

/* called with port.lock taken and irqs off */
static void imx_uart_dma_tx(struct imx_port *sport)
{
	struct circ_buf *xmit = &sport->port.state->xmit;
	struct scatterlist *sgl = sport->tx_sgl;
	struct dma_async_tx_descriptor *desc;
	struct dma_chan	*chan = sport->dma_chan_tx;
	struct device *dev = sport->port.dev;
	u32 ucr1, ucr4;
	int ret;

	if (sport->dma_is_txing)
		return;

	ucr4 = imx_uart_readl(sport, UCR4);
	ucr4 &= ~UCR4_TCEN;
	imx_uart_writel(sport, ucr4, UCR4);

	sport->tx_bytes = uart_circ_chars_pending(xmit);

	if (xmit->tail < xmit->head || xmit->head == 0) {
		sport->dma_tx_nents = 1;
		sg_init_one(sgl, xmit->buf + xmit->tail, sport->tx_bytes);
	} else {
		sport->dma_tx_nents = 2;
		sg_init_table(sgl, 2);
		sg_set_buf(sgl, xmit->buf + xmit->tail,
				UART_XMIT_SIZE - xmit->tail);
		sg_set_buf(sgl + 1, xmit->buf, xmit->head);
	}

	ret = dma_map_sg(dev, sgl, sport->dma_tx_nents, DMA_TO_DEVICE);
	if (ret == 0) {
		dev_err(dev, "DMA mapping error for TX.\n");
		return;
	}
	desc = dmaengine_prep_slave_sg(chan, sgl, ret,
					DMA_MEM_TO_DEV, DMA_PREP_INTERRUPT);
	if (!desc) {
		dma_unmap_sg(dev, sgl, sport->dma_tx_nents,
			     DMA_TO_DEVICE);
		dev_err(dev, "We cannot prepare for the TX slave dma!\n");
		return;
	}
	desc->callback = imx_uart_dma_tx_callback;
	desc->callback_param = sport;

	dev_dbg(dev, "TX: prepare to send %lu bytes by DMA.\n",
			uart_circ_chars_pending(xmit));

	ucr1 = imx_uart_readl(sport, UCR1);
	ucr1 |= UCR1_TXDMAEN;
	imx_uart_writel(sport, ucr1, UCR1);

	/* fire it */
	sport->dma_is_txing = 1;
	dmaengine_submit(desc);
	dma_async_issue_pending(chan);
	return;
}

/* called with port.lock taken and irqs off */
static void imx_uart_start_tx(struct uart_port *port)
{
	struct imx_port *sport = (struct imx_port *)port;
	u32 ucr1;

	if (!sport->port.x_char && uart_circ_empty(&port->state->xmit))
		return;

	/*
	 * We cannot simply do nothing here if sport->tx_state == SEND already
	 * because UCR1_TXMPTYEN might already have been cleared in
	 * imx_uart_stop_tx(), but tx_state is still SEND.
	 */

	if (port->rs485.flags & SER_RS485_ENABLED) {
		if (sport->tx_state == OFF) {
			u32 ucr2 = imx_uart_readl(sport, UCR2);
			if (port->rs485.flags & SER_RS485_RTS_ON_SEND)
				imx_uart_rts_active(sport, &ucr2);
			else
				imx_uart_rts_inactive(sport, &ucr2);
			imx_uart_writel(sport, ucr2, UCR2);

			if (!(port->rs485.flags & SER_RS485_RX_DURING_TX) &&
			    !port->rs485_rx_during_tx_gpio)
				imx_uart_stop_rx(port);

			sport->tx_state = WAIT_AFTER_RTS;

			if (port->rs485.delay_rts_before_send > 0) {
				start_hrtimer_ms(&sport->trigger_start_tx,
					 port->rs485.delay_rts_before_send);
				return;
			}

			/* continue without any delay */
		}

		if (sport->tx_state == WAIT_AFTER_SEND
		    || sport->tx_state == WAIT_AFTER_RTS) {

			hrtimer_try_to_cancel(&sport->trigger_stop_tx);

			/*
			 * Enable transmitter and shifter empty irq only if DMA
			 * is off.  In the DMA case this is done in the
			 * tx-callback.
			 */
			if (!sport->dma_is_enabled) {
				u32 ucr4 = imx_uart_readl(sport, UCR4);
				ucr4 |= UCR4_TCEN;
				imx_uart_writel(sport, ucr4, UCR4);
			}

			sport->tx_state = SEND;
		}
	} else {
		sport->tx_state = SEND;
	}

	if (!sport->dma_is_enabled) {
		ucr1 = imx_uart_readl(sport, UCR1);
		imx_uart_writel(sport, ucr1 | UCR1_TRDYEN, UCR1);
	}

	if (sport->dma_is_enabled) {
		if (sport->port.x_char) {
			/* We have X-char to send, so enable TX IRQ and
			 * disable TX DMA to let TX interrupt to send X-char */
			ucr1 = imx_uart_readl(sport, UCR1);
			ucr1 &= ~UCR1_TXDMAEN;
			ucr1 |= UCR1_TRDYEN;
			imx_uart_writel(sport, ucr1, UCR1);
			return;
		}

		if (!uart_circ_empty(&port->state->xmit) &&
		    !uart_tx_stopped(port))
			imx_uart_dma_tx(sport);
		return;
	}
}

static irqreturn_t __imx_uart_rtsint(int irq, void *dev_id)
{
	struct imx_port *sport = dev_id;
	u32 usr1;

	imx_uart_writel(sport, USR1_RTSD, USR1);
	usr1 = imx_uart_readl(sport, USR1) & USR1_RTSS;
	uart_handle_cts_change(&sport->port, usr1);
	wake_up_interruptible(&sport->port.state->port.delta_msr_wait);

	return IRQ_HANDLED;
}

static irqreturn_t imx_uart_rtsint(int irq, void *dev_id)
{
	struct imx_port *sport = dev_id;
	irqreturn_t ret;

	spin_lock(&sport->port.lock);

	ret = __imx_uart_rtsint(irq, dev_id);

	spin_unlock(&sport->port.lock);

	return ret;
}

static irqreturn_t imx_uart_txint(int irq, void *dev_id)
{
	struct imx_port *sport = dev_id;

	spin_lock(&sport->port.lock);
	imx_uart_transmit_buffer(sport);
	spin_unlock(&sport->port.lock);
	return IRQ_HANDLED;
}

/* Check if hardware Rx flood is in progress, and issue soft reset to stop it.
 * This is to be called from Rx ISRs only when some bytes were actually
 * received.
 *
 * A way to reproduce the flood (checked on iMX6SX) is: open iMX UART at 9600
 * 8N1, and from external source send 0xf0 char at 115200 8N1. In about 90% of
 * cases this starts a flood of "receiving" of 0xff characters by the iMX6 UART
 * that is terminated by any activity on RxD line, or could be stopped by
 * issuing soft reset to the UART (just stop/start of RX does not help). Note
 * that what we do here is sending isolated start bit about 2.4 times shorter
 * than it is to be on UART configured baud rate.
 */
static void imx_uart_check_flood(struct imx_port *sport, u32 usr2)
{
	/* To detect hardware 0xff flood we monitor RxD line between RX
	 * interrupts to isolate "receiving" of char(s) with no activity
	 * on RxD line, that'd never happen on actual data transfers.
	 *
	 * We use USR2_WAKE bit to check for activity on RxD line, but we have a
	 * race here if we clear USR2_WAKE when receiving of a char is in
	 * progress, so we might get RX interrupt later with USR2_WAKE bit
	 * cleared. Note though that as we don't try to clear USR2_WAKE when we
	 * detected no activity, this race may hide actual activity only once.
	 *
	 * Yet another case where receive interrupt may occur without RxD
	 * activity is expiration of aging timer, so we consider this as well.
	 *
	 * We use 'idle_counter' to ensure that we got at least so many RX
	 * interrupts without any detected activity on RxD line. 2 cases
	 * described plus 1 to be on the safe side gives us a margin of 3,
	 * below. In practice I was not able to produce a false positive to
	 * induce soft reset at regular data transfers even using 1 as the
	 * margin, so 3 is actually very strong.
	 *
	 * We count interrupts, not chars in 'idle-counter' for simplicity.
	 */

	if (usr2 & USR2_WAKE) {
		imx_uart_writel(sport, USR2_WAKE, USR2);
		sport->idle_counter = 0;
	} else if (++sport->idle_counter > 3) {
		dev_warn(sport->port.dev, "RX flood detected: soft reset.");
		imx_uart_soft_reset(sport); /* also clears 'sport->idle_counter' */
	}
}

static irqreturn_t __imx_uart_rxint(int irq, void *dev_id)
{
	struct imx_port *sport = dev_id;
	struct tty_port *port = &sport->port.state->port;
	u32 usr2, rx;

	/* If we received something, check for 0xff flood */
	usr2 = imx_uart_readl(sport, USR2);
	if (usr2 & USR2_RDR)
		imx_uart_check_flood(sport, usr2);

	while ((rx = imx_uart_readl(sport, URXD0)) & URXD_CHARRDY) {
		unsigned int flg = TTY_NORMAL;
		sport->port.icount.rx++;

		if (unlikely(rx & URXD_ERR)) {
			if (rx & URXD_BRK) {
				sport->port.icount.brk++;
				if (uart_handle_break(&sport->port))
					continue;
			}
			else if (rx & URXD_PRERR)
				sport->port.icount.parity++;
			else if (rx & URXD_FRMERR)
				sport->port.icount.frame++;
			if (rx & URXD_OVRRUN)
				sport->port.icount.overrun++;

			if (rx & sport->port.ignore_status_mask)
				continue;

			rx &= (sport->port.read_status_mask | 0xFF);

			if (rx & URXD_BRK)
				flg = TTY_BREAK;
			else if (rx & URXD_PRERR)
				flg = TTY_PARITY;
			else if (rx & URXD_FRMERR)
				flg = TTY_FRAME;
			if (rx & URXD_OVRRUN)
				flg = TTY_OVERRUN;

			sport->port.sysrq = 0;
		} else if (uart_handle_sysrq_char(&sport->port, (unsigned char)rx)) {
			continue;
		}

		if (sport->port.ignore_status_mask & URXD_DUMMY_READ)
			continue;

		if (tty_insert_flip_char(port, rx, flg) == 0)
			sport->port.icount.buf_overrun++;
	}

	tty_flip_buffer_push(port);

	return IRQ_HANDLED;
}

static irqreturn_t imx_uart_rxint(int irq, void *dev_id)
{
	struct imx_port *sport = dev_id;
	irqreturn_t ret;

	spin_lock(&sport->port.lock);

	ret = __imx_uart_rxint(irq, dev_id);

	spin_unlock(&sport->port.lock);

	return ret;
}

static void imx_uart_clear_rx_errors(struct imx_port *sport);

/*
 * We have a modem side uart, so the meanings of RTS and CTS are inverted.
 */
static unsigned int imx_uart_get_hwmctrl(struct imx_port *sport)
{
	unsigned int tmp = TIOCM_DSR;
	unsigned usr1 = imx_uart_readl(sport, USR1);
	unsigned usr2 = imx_uart_readl(sport, USR2);

	if (usr1 & USR1_RTSS)
		tmp |= TIOCM_CTS;

	/* in DCE mode DCDIN is always 0 */
	if (!(usr2 & USR2_DCDIN))
		tmp |= TIOCM_CAR;

	if (sport->dte_mode)
		if (!(imx_uart_readl(sport, USR2) & USR2_RIIN))
			tmp |= TIOCM_RI;

	return tmp;
}

/*
 * Handle any change of modem status signal since we were last called.
 */
static void imx_uart_mctrl_check(struct imx_port *sport)
{
	unsigned int status, changed;

	status = imx_uart_get_hwmctrl(sport);
	changed = status ^ sport->old_status;

	if (changed == 0)
		return;

	sport->old_status = status;

	if (changed & TIOCM_RI && status & TIOCM_RI)
		sport->port.icount.rng++;
	if (changed & TIOCM_DSR)
		sport->port.icount.dsr++;
	if (changed & TIOCM_CAR)
		uart_handle_dcd_change(&sport->port, status & TIOCM_CAR);
	if (changed & TIOCM_CTS)
		uart_handle_cts_change(&sport->port, status & TIOCM_CTS);

	wake_up_interruptible(&sport->port.state->port.delta_msr_wait);
}

static irqreturn_t imx_uart_int(int irq, void *dev_id)
{
	struct imx_port *sport = dev_id;
	unsigned int usr1, usr2, ucr1, ucr2, ucr3, ucr4;
	irqreturn_t ret = IRQ_NONE;

	spin_lock(&sport->port.lock);

	usr1 = imx_uart_readl(sport, USR1);
	usr2 = imx_uart_readl(sport, USR2);
	ucr1 = imx_uart_readl(sport, UCR1);
	ucr2 = imx_uart_readl(sport, UCR2);
	ucr3 = imx_uart_readl(sport, UCR3);
	ucr4 = imx_uart_readl(sport, UCR4);

	/*
	 * Even if a condition is true that can trigger an irq only handle it if
	 * the respective irq source is enabled. This prevents some undesired
	 * actions, for example if a character that sits in the RX FIFO and that
	 * should be fetched via DMA is tried to be fetched using PIO. Or the
	 * receiver is currently off and so reading from URXD0 results in an
	 * exception. So just mask the (raw) status bits for disabled irqs.
	 */
	if ((ucr1 & UCR1_RRDYEN) == 0)
		usr1 &= ~USR1_RRDY;
	if ((ucr2 & UCR2_ATEN) == 0)
		usr1 &= ~USR1_AGTIM;
	if ((ucr1 & UCR1_TRDYEN) == 0)
		usr1 &= ~USR1_TRDY;
	if ((ucr4 & UCR4_TCEN) == 0)
		usr2 &= ~USR2_TXDC;
	if ((ucr3 & UCR3_DTRDEN) == 0)
		usr1 &= ~USR1_DTRD;
	if ((ucr1 & UCR1_RTSDEN) == 0)
		usr1 &= ~USR1_RTSD;
	if ((ucr3 & UCR3_AWAKEN) == 0)
		usr1 &= ~USR1_AWAKE;
	if ((ucr4 & UCR4_OREN) == 0)
		usr2 &= ~USR2_ORE;

	if (usr1 & (USR1_RRDY | USR1_AGTIM)) {
		imx_uart_writel(sport, USR1_AGTIM, USR1);

		__imx_uart_rxint(irq, dev_id);
		ret = IRQ_HANDLED;
	}

	if ((usr1 & USR1_TRDY) || (usr2 & USR2_TXDC)) {
		imx_uart_transmit_buffer(sport);
		ret = IRQ_HANDLED;
	}

	if (usr1 & USR1_DTRD) {
		imx_uart_writel(sport, USR1_DTRD, USR1);

		imx_uart_mctrl_check(sport);

		ret = IRQ_HANDLED;
	}

	if (usr1 & USR1_RTSD) {
		__imx_uart_rtsint(irq, dev_id);
		ret = IRQ_HANDLED;
	}

	if (usr1 & USR1_AWAKE) {
		imx_uart_writel(sport, USR1_AWAKE, USR1);
		ret = IRQ_HANDLED;
	}

	if (usr2 & USR2_ORE) {
		sport->port.icount.overrun++;
		imx_uart_writel(sport, USR2_ORE, USR2);
		ret = IRQ_HANDLED;
	}

	spin_unlock(&sport->port.lock);

	return ret;
}

/*
 * Return TIOCSER_TEMT when transmitter is not busy.
 */
static unsigned int imx_uart_tx_empty(struct uart_port *port)
{
	struct imx_port *sport = (struct imx_port *)port;
	unsigned int ret;

	ret = (imx_uart_readl(sport, USR2) & USR2_TXDC) ?  TIOCSER_TEMT : 0;

	/* If the TX DMA is working, return 0. */
	if (sport->dma_is_txing)
		ret = 0;

	return ret;
}

/* called with port.lock taken and irqs off */
static unsigned int imx_uart_get_mctrl(struct uart_port *port)
{
	struct imx_port *sport = (struct imx_port *)port;
	unsigned int ret = imx_uart_get_hwmctrl(sport);

	mctrl_gpio_get(sport->gpios, &ret);

	return ret;
}

/* called with port.lock taken and irqs off */
static void imx_uart_set_mctrl(struct uart_port *port, unsigned int mctrl)
{
	struct imx_port *sport = (struct imx_port *)port;
	u32 ucr3, uts;

	if (!(port->rs485.flags & SER_RS485_ENABLED)) {
		u32 ucr2;

		/*
		 * Turn off autoRTS if RTS is lowered and restore autoRTS
		 * setting if RTS is raised.
		 */
		ucr2 = imx_uart_readl(sport, UCR2);
		ucr2 &= ~(UCR2_CTS | UCR2_CTSC);
		if (mctrl & TIOCM_RTS) {
			ucr2 |= UCR2_CTS;
			/*
			 * UCR2_IRTS is unset if and only if the port is
			 * configured for CRTSCTS, so we use inverted UCR2_IRTS
			 * to get the state to restore to.
			 */
			if (!(ucr2 & UCR2_IRTS))
				ucr2 |= UCR2_CTSC;
		}
		imx_uart_writel(sport, ucr2, UCR2);
	}

	ucr3 = imx_uart_readl(sport, UCR3) & ~UCR3_DSR;
	if (!(mctrl & TIOCM_DTR))
		ucr3 |= UCR3_DSR;
	imx_uart_writel(sport, ucr3, UCR3);

	uts = imx_uart_readl(sport, imx_uart_uts_reg(sport)) & ~UTS_LOOP;
	if (mctrl & TIOCM_LOOP)
		uts |= UTS_LOOP;
	imx_uart_writel(sport, uts, imx_uart_uts_reg(sport));

	mctrl_gpio_set(sport->gpios, mctrl);
}

/*
 * Interrupts always disabled.
 */
static void imx_uart_break_ctl(struct uart_port *port, int break_state)
{
	struct imx_port *sport = (struct imx_port *)port;
	unsigned long flags;
	u32 ucr1;

	spin_lock_irqsave(&sport->port.lock, flags);

	ucr1 = imx_uart_readl(sport, UCR1) & ~UCR1_SNDBRK;

	if (break_state != 0)
		ucr1 |= UCR1_SNDBRK;

	imx_uart_writel(sport, ucr1, UCR1);

	spin_unlock_irqrestore(&sport->port.lock, flags);
}

/*
 * This is our per-port timeout handler, for checking the
 * modem status signals.
 */
static void imx_uart_timeout(struct timer_list *t)
{
	struct imx_port *sport = from_timer(sport, t, timer);
	unsigned long flags;

	if (sport->port.state) {
		spin_lock_irqsave(&sport->port.lock, flags);
		imx_uart_mctrl_check(sport);
		spin_unlock_irqrestore(&sport->port.lock, flags);

		mod_timer(&sport->timer, jiffies + MCTRL_TIMEOUT);
	}
}

/*
 * There are two kinds of RX DMA interrupts(such as in the MX6Q):
 *   [1] the RX DMA buffer is full.
 *   [2] the aging timer expires
 *
 * Condition [2] is triggered when a character has been sitting in the FIFO
 * for at least 8 byte durations.
 */
static void imx_uart_dma_rx_callback(void *data)
{
	struct imx_port *sport = data;
	struct dma_chan	*chan = sport->dma_chan_rx;
	struct scatterlist *sgl = &sport->rx_sgl;
	struct tty_port *port = &sport->port.state->port;
	struct dma_tx_state state;
	struct circ_buf *rx_ring = &sport->rx_ring;
	enum dma_status status;
	unsigned int w_bytes = 0;
	unsigned int r_bytes;
	unsigned int bd_size;

	status = dmaengine_tx_status(chan, sport->rx_cookie, &state);

	if (status == DMA_ERROR) {
		spin_lock(&sport->port.lock);
		imx_uart_clear_rx_errors(sport);
		spin_unlock(&sport->port.lock);
		return;
	}

	/*
	 * The state-residue variable represents the empty space
	 * relative to the entire buffer. Taking this in consideration
	 * the head is always calculated base on the buffer total
	 * length - DMA transaction residue. The UART script from the
	 * SDMA firmware will jump to the next buffer descriptor,
	 * once a DMA transaction if finalized (IMX53 RM - A.4.1.2.4).
	 * Taking this in consideration the tail is always at the
	 * beginning of the buffer descriptor that contains the head.
	 */

	/* Calculate the head */
	rx_ring->head = sg_dma_len(sgl) - state.residue;

	/* Calculate the tail. */
	bd_size = sg_dma_len(sgl) / sport->rx_periods;
	rx_ring->tail = ((rx_ring->head-1) / bd_size) * bd_size;

	if (rx_ring->head <= sg_dma_len(sgl) &&
	    rx_ring->head > rx_ring->tail) {

		/* Move data from tail to head */
		r_bytes = rx_ring->head - rx_ring->tail;

		/* If we received something, check for 0xff flood */
		spin_lock(&sport->port.lock);
		imx_uart_check_flood(sport, imx_uart_readl(sport, USR2));
		spin_unlock(&sport->port.lock);

		if (!(sport->port.ignore_status_mask & URXD_DUMMY_READ)) {

			/* CPU claims ownership of RX DMA buffer */
			dma_sync_sg_for_cpu(sport->port.dev, sgl, 1,
					    DMA_FROM_DEVICE);

			w_bytes = tty_insert_flip_string(port,
							 sport->rx_buf + rx_ring->tail, r_bytes);

			/* UART retrieves ownership of RX DMA buffer */
			dma_sync_sg_for_device(sport->port.dev, sgl, 1,
					       DMA_FROM_DEVICE);

			if (w_bytes != r_bytes)
				sport->port.icount.buf_overrun++;

			sport->port.icount.rx += w_bytes;
		}
	} else	{
		WARN_ON(rx_ring->head > sg_dma_len(sgl));
		WARN_ON(rx_ring->head <= rx_ring->tail);
	}

	if (w_bytes) {
		tty_flip_buffer_push(port);
		dev_dbg(sport->port.dev, "We get %d bytes.\n", w_bytes);
	}
}

static int imx_uart_start_rx_dma(struct imx_port *sport)
{
	struct scatterlist *sgl = &sport->rx_sgl;
	struct dma_chan	*chan = sport->dma_chan_rx;
	struct device *dev = sport->port.dev;
	struct dma_async_tx_descriptor *desc;
	int ret;

	sport->rx_ring.head = 0;
	sport->rx_ring.tail = 0;

	sg_init_one(sgl, sport->rx_buf, sport->rx_buf_size);
	ret = dma_map_sg(dev, sgl, 1, DMA_FROM_DEVICE);
	if (ret == 0) {
		dev_err(dev, "DMA mapping error for RX.\n");
		return -EINVAL;
	}

	desc = dmaengine_prep_dma_cyclic(chan, sg_dma_address(sgl),
		sg_dma_len(sgl), sg_dma_len(sgl) / sport->rx_periods,
		DMA_DEV_TO_MEM, DMA_PREP_INTERRUPT);

	if (!desc) {
		dma_unmap_sg(dev, sgl, 1, DMA_FROM_DEVICE);
		dev_err(dev, "We cannot prepare for the RX slave dma!\n");
		return -EINVAL;
	}
	desc->callback = imx_uart_dma_rx_callback;
	desc->callback_param = sport;

	dev_dbg(dev, "RX: prepare for the DMA.\n");
	sport->dma_is_rxing = 1;
	sport->rx_cookie = dmaengine_submit(desc);
	dma_async_issue_pending(chan);
	return 0;
}

static void imx_uart_clear_rx_errors(struct imx_port *sport)
{
	struct tty_port *port = &sport->port.state->port;
	u32 usr1, usr2;

	usr1 = imx_uart_readl(sport, USR1);
	usr2 = imx_uart_readl(sport, USR2);

	if (usr2 & USR2_BRCD) {
		sport->port.icount.brk++;
		imx_uart_writel(sport, USR2_BRCD, USR2);
		uart_handle_break(&sport->port);
		if (tty_insert_flip_char(port, 0, TTY_BREAK) == 0)
			sport->port.icount.buf_overrun++;
		tty_flip_buffer_push(port);
	} else {
		if (usr1 & USR1_FRAMERR) {
			sport->port.icount.frame++;
			imx_uart_writel(sport, USR1_FRAMERR, USR1);
		} else if (usr1 & USR1_PARITYERR) {
			sport->port.icount.parity++;
			imx_uart_writel(sport, USR1_PARITYERR, USR1);
		}
	}

	if (usr2 & USR2_ORE) {
		sport->port.icount.overrun++;
		imx_uart_writel(sport, USR2_ORE, USR2);
	}

	sport->idle_counter = 0;

}

#define TXTL_DEFAULT 2 /* reset default */
#define RXTL_DEFAULT 8 /* 8 characters or aging timer */
#define TXTL_DMA 8 /* DMA burst setting */
#define RXTL_DMA 9 /* DMA burst setting */

static void imx_uart_setup_ufcr(struct imx_port *sport,
				unsigned char txwl, unsigned char rxwl)
{
	unsigned int val;

	/* set receiver / transmitter trigger level */
	val = imx_uart_readl(sport, UFCR) & (UFCR_RFDIV | UFCR_DCEDTE);
	val |= txwl << UFCR_TXTL_SHF | rxwl;
	imx_uart_writel(sport, val, UFCR);
}

static void imx_uart_dma_exit(struct imx_port *sport)
{
	if (sport->dma_chan_rx) {
		dmaengine_terminate_sync(sport->dma_chan_rx);
		dma_release_channel(sport->dma_chan_rx);
		sport->dma_chan_rx = NULL;
		sport->rx_cookie = -EINVAL;
		kfree(sport->rx_buf);
		sport->rx_buf = NULL;
	}

	if (sport->dma_chan_tx) {
		dmaengine_terminate_sync(sport->dma_chan_tx);
		dma_release_channel(sport->dma_chan_tx);
		sport->dma_chan_tx = NULL;
	}
}

static int imx_uart_dma_init(struct imx_port *sport)
{
	struct dma_slave_config slave_config = {};
	struct device *dev = sport->port.dev;
	int ret;

	/* Prepare for RX : */
	sport->dma_chan_rx = dma_request_slave_channel(dev, "rx");
	if (!sport->dma_chan_rx) {
		dev_dbg(dev, "cannot get the DMA channel.\n");
		ret = -EINVAL;
		goto err;
	}

	slave_config.direction = DMA_DEV_TO_MEM;
	slave_config.src_addr = sport->port.mapbase + URXD0;
	slave_config.src_addr_width = DMA_SLAVE_BUSWIDTH_1_BYTE;
	/* one byte less than the watermark level to enable the aging timer */
	slave_config.src_maxburst = RXTL_DMA - 1;
	ret = dmaengine_slave_config(sport->dma_chan_rx, &slave_config);
	if (ret) {
		dev_err(dev, "error in RX dma configuration.\n");
		goto err;
	}

	sport->rx_buf_size = sport->rx_period_length * sport->rx_periods;
	sport->rx_buf = kzalloc(sport->rx_buf_size, GFP_KERNEL);
	if (!sport->rx_buf) {
		ret = -ENOMEM;
		goto err;
	}
	sport->rx_ring.buf = sport->rx_buf;

	/* Prepare for TX : */
	sport->dma_chan_tx = dma_request_slave_channel(dev, "tx");
	if (!sport->dma_chan_tx) {
		dev_err(dev, "cannot get the TX DMA channel!\n");
		ret = -EINVAL;
		goto err;
	}

	slave_config.direction = DMA_MEM_TO_DEV;
	slave_config.dst_addr = sport->port.mapbase + URTX0;
	slave_config.dst_addr_width = DMA_SLAVE_BUSWIDTH_1_BYTE;
	slave_config.dst_maxburst = TXTL_DMA;
	ret = dmaengine_slave_config(sport->dma_chan_tx, &slave_config);
	if (ret) {
		dev_err(dev, "error in TX dma configuration.");
		goto err;
	}

	return 0;
err:
	imx_uart_dma_exit(sport);
	return ret;
}

static void imx_uart_enable_dma(struct imx_port *sport)
{
	u32 ucr1;

	imx_uart_setup_ufcr(sport, TXTL_DMA, RXTL_DMA);

	/* set UCR1 */
	ucr1 = imx_uart_readl(sport, UCR1);
	ucr1 |= UCR1_RXDMAEN | UCR1_TXDMAEN | UCR1_ATDMAEN;
	imx_uart_writel(sport, ucr1, UCR1);

	sport->dma_is_enabled = 1;
}

static void imx_uart_disable_dma(struct imx_port *sport)
{
	u32 ucr1;

	/* clear UCR1 */
	ucr1 = imx_uart_readl(sport, UCR1);
	ucr1 &= ~(UCR1_RXDMAEN | UCR1_TXDMAEN | UCR1_ATDMAEN);
	imx_uart_writel(sport, ucr1, UCR1);

	imx_uart_setup_ufcr(sport, TXTL_DEFAULT, RXTL_DEFAULT);

	sport->dma_is_enabled = 0;
}

/* half the RX buffer size */
#define CTSTL 16

static int imx_uart_startup(struct uart_port *port)
{
	struct imx_port *sport = (struct imx_port *)port;
	int retval;
	unsigned long flags;
	int dma_is_inited = 0;
	u32 ucr1, ucr2, ucr3, ucr4;

	retval = clk_prepare_enable(sport->clk_per);
	if (retval)
		return retval;
	retval = clk_prepare_enable(sport->clk_ipg);
	if (retval) {
		clk_disable_unprepare(sport->clk_per);
		return retval;
	}

	imx_uart_setup_ufcr(sport, TXTL_DEFAULT, RXTL_DEFAULT);

	/* disable the DREN bit (Data Ready interrupt enable) before
	 * requesting IRQs
	 */
	ucr4 = imx_uart_readl(sport, UCR4);

	/* set the trigger level for CTS */
	ucr4 &= ~(UCR4_CTSTL_MASK << UCR4_CTSTL_SHF);
	ucr4 |= CTSTL << UCR4_CTSTL_SHF;

	imx_uart_writel(sport, ucr4 & ~UCR4_DREN, UCR4);

	/* Can we enable the DMA support? */
	if (!uart_console(port) && imx_uart_dma_init(sport) == 0)
		dma_is_inited = 1;

	spin_lock_irqsave(&sport->port.lock, flags);

	/* Reset fifo's and state machines */
	imx_uart_soft_reset(sport);

	/*
	 * Finally, clear and enable interrupts
	 */
	imx_uart_writel(sport, USR1_RTSD | USR1_DTRD, USR1);
	imx_uart_writel(sport, USR2_ORE, USR2);

	ucr1 = imx_uart_readl(sport, UCR1) & ~UCR1_RRDYEN;
	ucr1 |= UCR1_UARTEN;
	if (sport->have_rtscts)
		ucr1 |= UCR1_RTSDEN;

	imx_uart_writel(sport, ucr1, UCR1);

	ucr4 = imx_uart_readl(sport, UCR4) & ~(UCR4_OREN | UCR4_INVR);
	if (!dma_is_inited)
		ucr4 |= UCR4_OREN;
	if (sport->inverted_rx)
		ucr4 |= UCR4_INVR;
	imx_uart_writel(sport, ucr4, UCR4);

	ucr3 = imx_uart_readl(sport, UCR3) & ~UCR3_INVT;
	/*
	 * configure tx polarity before enabling tx
	 */
	if (sport->inverted_tx)
		ucr3 |= UCR3_INVT;

	if (!imx_uart_is_imx1(sport)) {
		ucr3 |= UCR3_DTRDEN | UCR3_RI | UCR3_DCD;

		if (sport->dte_mode)
			/* disable broken interrupts */
			ucr3 &= ~(UCR3_RI | UCR3_DCD);
	}
	imx_uart_writel(sport, ucr3, UCR3);

	ucr2 = imx_uart_readl(sport, UCR2) & ~UCR2_ATEN;
	ucr2 |= (UCR2_RXEN | UCR2_TXEN);
	if (!sport->have_rtscts)
		ucr2 |= UCR2_IRTS;
	/*
	 * make sure the edge sensitive RTS-irq is disabled,
	 * we're using RTSD instead.
	 */
	if (!imx_uart_is_imx1(sport))
		ucr2 &= ~UCR2_RTSEN;
	imx_uart_writel(sport, ucr2, UCR2);

	/*
	 * Enable modem status interrupts
	 */
	imx_uart_enable_ms(&sport->port);

	if (dma_is_inited) {
		imx_uart_enable_dma(sport);
		imx_uart_start_rx_dma(sport);
	} else {
		ucr1 = imx_uart_readl(sport, UCR1);
		ucr1 |= UCR1_RRDYEN;
		imx_uart_writel(sport, ucr1, UCR1);

		ucr2 = imx_uart_readl(sport, UCR2);
		ucr2 |= UCR2_ATEN;
		imx_uart_writel(sport, ucr2, UCR2);
	}

	imx_uart_disable_loopback_rs485(sport);

	spin_unlock_irqrestore(&sport->port.lock, flags);

	return 0;
}

static void imx_uart_shutdown(struct uart_port *port)
{
	struct imx_port *sport = (struct imx_port *)port;
	unsigned long flags;
	u32 ucr1, ucr2, ucr4, uts;

	if (sport->dma_is_enabled) {
		dmaengine_terminate_sync(sport->dma_chan_tx);
		if (sport->dma_is_txing) {
			dma_unmap_sg(sport->port.dev, &sport->tx_sgl[0],
				     sport->dma_tx_nents, DMA_TO_DEVICE);
			sport->dma_is_txing = 0;
		}
		dmaengine_terminate_sync(sport->dma_chan_rx);
		if (sport->dma_is_rxing) {
			dma_unmap_sg(sport->port.dev, &sport->rx_sgl,
				     1, DMA_FROM_DEVICE);
			sport->dma_is_rxing = 0;
		}

		spin_lock_irqsave(&sport->port.lock, flags);
		imx_uart_stop_tx(port);
		imx_uart_stop_rx(port);
		imx_uart_disable_dma(sport);
		spin_unlock_irqrestore(&sport->port.lock, flags);
		imx_uart_dma_exit(sport);
	}

	mctrl_gpio_disable_ms(sport->gpios);

	spin_lock_irqsave(&sport->port.lock, flags);
	ucr2 = imx_uart_readl(sport, UCR2);
	ucr2 &= ~(UCR2_TXEN | UCR2_ATEN);
	imx_uart_writel(sport, ucr2, UCR2);
	spin_unlock_irqrestore(&sport->port.lock, flags);

	/*
	 * Stop our timer.
	 */
	del_timer_sync(&sport->timer);

	/*
	 * Disable all interrupts, port and break condition.
	 */

	spin_lock_irqsave(&sport->port.lock, flags);

	ucr1 = imx_uart_readl(sport, UCR1);
	ucr1 &= ~(UCR1_TRDYEN | UCR1_RRDYEN | UCR1_RTSDEN | UCR1_RXDMAEN |
		  UCR1_ATDMAEN | UCR1_SNDBRK);
	/* See SER_RS485_ENABLED/UTS_LOOP comment in imx_uart_probe() */
	if (port->rs485.flags & SER_RS485_ENABLED &&
	    port->rs485.flags & SER_RS485_RTS_ON_SEND &&
	    sport->have_rtscts && !sport->have_rtsgpio) {
		uts = imx_uart_readl(sport, imx_uart_uts_reg(sport));
		uts |= UTS_LOOP;
		imx_uart_writel(sport, uts, imx_uart_uts_reg(sport));
		ucr1 |= UCR1_UARTEN;
	} else {
		ucr1 &= ~UCR1_UARTEN;
	}
	imx_uart_writel(sport, ucr1, UCR1);

	ucr4 = imx_uart_readl(sport, UCR4);
	ucr4 &= ~UCR4_TCEN;
	imx_uart_writel(sport, ucr4, UCR4);

	spin_unlock_irqrestore(&sport->port.lock, flags);

	clk_disable_unprepare(sport->clk_per);
	clk_disable_unprepare(sport->clk_ipg);
}

/* called with port.lock taken and irqs off */
static void imx_uart_flush_buffer(struct uart_port *port)
{
	struct imx_port *sport = (struct imx_port *)port;
	struct scatterlist *sgl = &sport->tx_sgl[0];

	if (!sport->dma_chan_tx)
		return;

	sport->tx_bytes = 0;
	dmaengine_terminate_all(sport->dma_chan_tx);
	if (sport->dma_is_txing) {
		u32 ucr1;

		dma_unmap_sg(sport->port.dev, sgl, sport->dma_tx_nents,
			     DMA_TO_DEVICE);
		ucr1 = imx_uart_readl(sport, UCR1);
		ucr1 &= ~UCR1_TXDMAEN;
		imx_uart_writel(sport, ucr1, UCR1);
		sport->dma_is_txing = 0;
	}

	imx_uart_soft_reset(sport);

}

static void
imx_uart_set_termios(struct uart_port *port, struct ktermios *termios,
		     const struct ktermios *old)
{
	struct imx_port *sport = (struct imx_port *)port;
	unsigned long flags;
	u32 ucr2, old_ucr2, ufcr;
	unsigned int baud, quot;
	unsigned int old_csize = old ? old->c_cflag & CSIZE : CS8;
	unsigned long div;
	unsigned long num, denom, old_ubir, old_ubmr;
	uint64_t tdiv64;

	/*
	 * We only support CS7 and CS8.
	 */
	while ((termios->c_cflag & CSIZE) != CS7 &&
	       (termios->c_cflag & CSIZE) != CS8) {
		termios->c_cflag &= ~CSIZE;
		termios->c_cflag |= old_csize;
		old_csize = CS8;
	}

	del_timer_sync(&sport->timer);

	/*
	 * Ask the core to calculate the divisor for us.
	 */
	baud = uart_get_baud_rate(port, termios, old, 50, port->uartclk / 16);
	quot = uart_get_divisor(port, baud);

	spin_lock_irqsave(&sport->port.lock, flags);

	/*
	 * Read current UCR2 and save it for future use, then clear all the bits
	 * except those we will or may need to preserve.
	 */
	old_ucr2 = imx_uart_readl(sport, UCR2);
	ucr2 = old_ucr2 & (UCR2_TXEN | UCR2_RXEN | UCR2_ATEN | UCR2_CTS);

	ucr2 |= UCR2_SRST | UCR2_IRTS;
	if ((termios->c_cflag & CSIZE) == CS8)
		ucr2 |= UCR2_WS;

	if (!sport->have_rtscts)
		termios->c_cflag &= ~CRTSCTS;

	if (port->rs485.flags & SER_RS485_ENABLED) {
		/*
		 * RTS is mandatory for rs485 operation, so keep
		 * it under manual control and keep transmitter
		 * disabled.
		 */
		if (port->rs485.flags & SER_RS485_RTS_AFTER_SEND)
			imx_uart_rts_active(sport, &ucr2);
		else
			imx_uart_rts_inactive(sport, &ucr2);

	} else if (termios->c_cflag & CRTSCTS) {
		/*
		 * Only let receiver control RTS output if we were not requested
		 * to have RTS inactive (which then should take precedence).
		 */
		if (ucr2 & UCR2_CTS)
			ucr2 |= UCR2_CTSC;
	}

	if (termios->c_cflag & CRTSCTS)
		ucr2 &= ~UCR2_IRTS;
	if (termios->c_cflag & CSTOPB)
		ucr2 |= UCR2_STPB;
	if (termios->c_cflag & PARENB) {
		ucr2 |= UCR2_PREN;
		if (termios->c_cflag & PARODD)
			ucr2 |= UCR2_PROE;
	}

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
		sport->port.ignore_status_mask |= URXD_PRERR | URXD_FRMERR;
	if (termios->c_iflag & IGNBRK) {
		sport->port.ignore_status_mask |= URXD_BRK;
		/*
		 * If we're ignoring parity and break indicators,
		 * ignore overruns too (for real raw support).
		 */
		if (termios->c_iflag & IGNPAR)
			sport->port.ignore_status_mask |= URXD_OVRRUN;
	}

	if ((termios->c_cflag & CREAD) == 0)
		sport->port.ignore_status_mask |= URXD_DUMMY_READ;

	/*
	 * Update the per-port timeout.
	 */
	uart_update_timeout(port, termios->c_cflag, baud);

	/* custom-baudrate handling */
	div = sport->port.uartclk / (baud * 16);
	if (baud == 38400 && quot != div)
		baud = sport->port.uartclk / (quot * 16);

	div = sport->port.uartclk / (baud * 16);
	if (div > 7)
		div = 7;
	if (!div)
		div = 1;

	rational_best_approximation(16 * div * baud, sport->port.uartclk,
		1 << 16, 1 << 16, &num, &denom);

	tdiv64 = sport->port.uartclk;
	tdiv64 *= num;
	do_div(tdiv64, denom * 16 * div);
	tty_termios_encode_baud_rate(termios,
				(speed_t)tdiv64, (speed_t)tdiv64);

	num -= 1;
	denom -= 1;

	ufcr = imx_uart_readl(sport, UFCR);
	ufcr = (ufcr & (~UFCR_RFDIV)) | UFCR_RFDIV_REG(div);
	imx_uart_writel(sport, ufcr, UFCR);

	/*
	 *  Two registers below should always be written both and in this
	 *  particular order. One consequence is that we need to check if any of
	 *  them changes and then update both. We do need the check for change
	 *  as even writing the same values seem to "restart"
	 *  transmission/receiving logic in the hardware, that leads to data
	 *  breakage even when rate doesn't in fact change. E.g., user switches
	 *  RTS/CTS handshake and suddenly gets broken bytes.
	 */
	old_ubir = imx_uart_readl(sport, UBIR);
	old_ubmr = imx_uart_readl(sport, UBMR);
	if (old_ubir != num || old_ubmr != denom) {
		imx_uart_writel(sport, num, UBIR);
		imx_uart_writel(sport, denom, UBMR);
	}

	if (!imx_uart_is_imx1(sport))
		imx_uart_writel(sport, sport->port.uartclk / div / 1000,
				IMX21_ONEMS);

	imx_uart_writel(sport, ucr2, UCR2);

	if (UART_ENABLE_MS(&sport->port, termios->c_cflag))
		imx_uart_enable_ms(&sport->port);

	spin_unlock_irqrestore(&sport->port.lock, flags);
}

static const char *imx_uart_type(struct uart_port *port)
{
	return port->type == PORT_IMX ? "IMX" : NULL;
}

/*
 * Configure/autoconfigure the port.
 */
static void imx_uart_config_port(struct uart_port *port, int flags)
{
	if (flags & UART_CONFIG_TYPE)
		port->type = PORT_IMX;
}

/*
 * Verify the new serial_struct (for TIOCSSERIAL).
 * The only change we allow are to the flags and type, and
 * even then only between PORT_IMX and PORT_UNKNOWN
 */
static int
imx_uart_verify_port(struct uart_port *port, struct serial_struct *ser)
{
	int ret = 0;

	if (ser->type != PORT_UNKNOWN && ser->type != PORT_IMX)
		ret = -EINVAL;
	if (port->irq != ser->irq)
		ret = -EINVAL;
	if (ser->io_type != UPIO_MEM)
		ret = -EINVAL;
	if (port->uartclk / 16 != ser->baud_base)
		ret = -EINVAL;
	if (port->mapbase != (unsigned long)ser->iomem_base)
		ret = -EINVAL;
	if (port->iobase != ser->port)
		ret = -EINVAL;
	if (ser->hub6 != 0)
		ret = -EINVAL;
	return ret;
}

#if defined(CONFIG_CONSOLE_POLL)

static int imx_uart_poll_init(struct uart_port *port)
{
	struct imx_port *sport = (struct imx_port *)port;
	unsigned long flags;
	u32 ucr1, ucr2;
	int retval;

	retval = clk_prepare_enable(sport->clk_ipg);
	if (retval)
		return retval;
	retval = clk_prepare_enable(sport->clk_per);
	if (retval)
		clk_disable_unprepare(sport->clk_ipg);

	imx_uart_setup_ufcr(sport, TXTL_DEFAULT, RXTL_DEFAULT);

	spin_lock_irqsave(&sport->port.lock, flags);

	/*
	 * Be careful about the order of enabling bits here. First enable the
	 * receiver (UARTEN + RXEN) and only then the corresponding irqs.
	 * This prevents that a character that already sits in the RX fifo is
	 * triggering an irq but the try to fetch it from there results in an
	 * exception because UARTEN or RXEN is still off.
	 */
	ucr1 = imx_uart_readl(sport, UCR1);
	ucr2 = imx_uart_readl(sport, UCR2);

	if (imx_uart_is_imx1(sport))
		ucr1 |= IMX1_UCR1_UARTCLKEN;

	ucr1 |= UCR1_UARTEN;
	ucr1 &= ~(UCR1_TRDYEN | UCR1_RTSDEN | UCR1_RRDYEN);

	ucr2 |= UCR2_RXEN | UCR2_TXEN;
	ucr2 &= ~UCR2_ATEN;

	imx_uart_writel(sport, ucr1, UCR1);
	imx_uart_writel(sport, ucr2, UCR2);

	/* now enable irqs */
	imx_uart_writel(sport, ucr1 | UCR1_RRDYEN, UCR1);
	imx_uart_writel(sport, ucr2 | UCR2_ATEN, UCR2);

	spin_unlock_irqrestore(&sport->port.lock, flags);

	return 0;
}

static int imx_uart_poll_get_char(struct uart_port *port)
{
	struct imx_port *sport = (struct imx_port *)port;
	if (!(imx_uart_readl(sport, USR2) & USR2_RDR))
		return NO_POLL_CHAR;

	return imx_uart_readl(sport, URXD0) & URXD_RX_DATA;
}

static void imx_uart_poll_put_char(struct uart_port *port, unsigned char c)
{
	struct imx_port *sport = (struct imx_port *)port;
	unsigned int status;

	/* drain */
	do {
		status = imx_uart_readl(sport, USR1);
	} while (~status & USR1_TRDY);

	/* write */
	imx_uart_writel(sport, c, URTX0);

	/* flush */
	do {
		status = imx_uart_readl(sport, USR2);
	} while (~status & USR2_TXDC);
}
#endif

/* called with port.lock taken and irqs off or from .probe without locking */
static int imx_uart_rs485_config(struct uart_port *port, struct ktermios *termios,
				 struct serial_rs485 *rs485conf)
{
	struct imx_port *sport = (struct imx_port *)port;
	u32 ucr2;

	if (rs485conf->flags & SER_RS485_ENABLED) {
		/* Enable receiver if low-active RTS signal is requested */
		if (sport->have_rtscts &&  !sport->have_rtsgpio &&
		    !(rs485conf->flags & SER_RS485_RTS_ON_SEND))
			rs485conf->flags |= SER_RS485_RX_DURING_TX;

		/* disable transmitter */
		ucr2 = imx_uart_readl(sport, UCR2);
		if (rs485conf->flags & SER_RS485_RTS_AFTER_SEND)
			imx_uart_rts_active(sport, &ucr2);
		else
			imx_uart_rts_inactive(sport, &ucr2);
		imx_uart_writel(sport, ucr2, UCR2);
	}

	/* Make sure Rx is enabled in case Tx is active with Rx disabled */
	if (!(rs485conf->flags & SER_RS485_ENABLED) ||
	    rs485conf->flags & SER_RS485_RX_DURING_TX)
		imx_uart_start_rx(port);

	return 0;
}

static const struct uart_ops imx_uart_pops = {
	.tx_empty	= imx_uart_tx_empty,
	.set_mctrl	= imx_uart_set_mctrl,
	.get_mctrl	= imx_uart_get_mctrl,
	.stop_tx	= imx_uart_stop_tx,
	.start_tx	= imx_uart_start_tx,
	.stop_rx	= imx_uart_stop_rx,
	.enable_ms	= imx_uart_enable_ms,
	.break_ctl	= imx_uart_break_ctl,
	.startup	= imx_uart_startup,
	.shutdown	= imx_uart_shutdown,
	.flush_buffer	= imx_uart_flush_buffer,
	.set_termios	= imx_uart_set_termios,
	.type		= imx_uart_type,
	.config_port	= imx_uart_config_port,
	.verify_port	= imx_uart_verify_port,
#if defined(CONFIG_CONSOLE_POLL)
	.poll_init      = imx_uart_poll_init,
	.poll_get_char  = imx_uart_poll_get_char,
	.poll_put_char  = imx_uart_poll_put_char,
#endif
};

static struct imx_port *imx_uart_ports[UART_NR];

#if IS_ENABLED(CONFIG_SERIAL_IMX_CONSOLE)
static void imx_uart_console_putchar(struct uart_port *port, unsigned char ch)
{
	struct imx_port *sport = (struct imx_port *)port;

	while (imx_uart_readl(sport, imx_uart_uts_reg(sport)) & UTS_TXFULL)
		barrier();

	imx_uart_writel(sport, ch, URTX0);
}

/*
 * Interrupts are disabled on entering
 */
static void
imx_uart_console_write(struct console *co, const char *s, unsigned int count)
{
	struct imx_port *sport = imx_uart_ports[co->index];
	struct imx_port_ucrs old_ucr;
	unsigned long flags;
	unsigned int ucr1;
	int locked = 1;

	if (sport->port.sysrq)
		locked = 0;
	else if (oops_in_progress)
		locked = spin_trylock_irqsave(&sport->port.lock, flags);
	else
		spin_lock_irqsave(&sport->port.lock, flags);

	/*
	 *	First, save UCR1/2/3 and then disable interrupts
	 */
	imx_uart_ucrs_save(sport, &old_ucr);
	ucr1 = old_ucr.ucr1;

	if (imx_uart_is_imx1(sport))
		ucr1 |= IMX1_UCR1_UARTCLKEN;
	ucr1 |= UCR1_UARTEN;
	ucr1 &= ~(UCR1_TRDYEN | UCR1_RRDYEN | UCR1_RTSDEN);

	imx_uart_writel(sport, ucr1, UCR1);

	imx_uart_writel(sport, old_ucr.ucr2 | UCR2_TXEN, UCR2);

	uart_console_write(&sport->port, s, count, imx_uart_console_putchar);

	/*
	 *	Finally, wait for transmitter to become empty
	 *	and restore UCR1/2/3
	 */
	while (!(imx_uart_readl(sport, USR2) & USR2_TXDC));

	imx_uart_ucrs_restore(sport, &old_ucr);

	if (locked)
		spin_unlock_irqrestore(&sport->port.lock, flags);
}

/*
 * If the port was already initialised (eg, by a boot loader),
 * try to determine the current setup.
 */
static void
imx_uart_console_get_options(struct imx_port *sport, int *baud,
			     int *parity, int *bits)
{

	if (imx_uart_readl(sport, UCR1) & UCR1_UARTEN) {
		/* ok, the port was enabled */
		unsigned int ucr2, ubir, ubmr, uartclk;
		unsigned int baud_raw;
		unsigned int ucfr_rfdiv;

		ucr2 = imx_uart_readl(sport, UCR2);

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

		ubir = imx_uart_readl(sport, UBIR) & 0xffff;
		ubmr = imx_uart_readl(sport, UBMR) & 0xffff;

		ucfr_rfdiv = (imx_uart_readl(sport, UFCR) & UFCR_RFDIV) >> 7;
		if (ucfr_rfdiv == 6)
			ucfr_rfdiv = 7;
		else
			ucfr_rfdiv = 6 - ucfr_rfdiv;

		uartclk = clk_get_rate(sport->clk_per);
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

		if (*baud != baud_raw)
			dev_info(sport->port.dev, "Console IMX rounded baud rate from %d to %d\n",
				baud_raw, *baud);
	}
}

static int
imx_uart_console_setup(struct console *co, char *options)
{
	struct imx_port *sport;
	int baud = 9600;
	int bits = 8;
	int parity = 'n';
	int flow = 'n';
	int retval;

	/*
	 * Check whether an invalid uart number has been specified, and
	 * if so, search for the first available port that does have
	 * console support.
	 */
	if (co->index == -1 || co->index >= ARRAY_SIZE(imx_uart_ports))
		co->index = 0;
	sport = imx_uart_ports[co->index];
	if (sport == NULL)
		return -ENODEV;

	/* For setting the registers, we only need to enable the ipg clock. */
	retval = clk_prepare_enable(sport->clk_ipg);
	if (retval)
		goto error_console;

	if (options)
		uart_parse_options(options, &baud, &parity, &bits, &flow);
	else
		imx_uart_console_get_options(sport, &baud, &parity, &bits);

	imx_uart_setup_ufcr(sport, TXTL_DEFAULT, RXTL_DEFAULT);

	retval = uart_set_options(&sport->port, co, baud, parity, bits, flow);

	if (retval) {
		clk_disable_unprepare(sport->clk_ipg);
		goto error_console;
	}

	retval = clk_prepare_enable(sport->clk_per);
	if (retval)
		clk_disable_unprepare(sport->clk_ipg);

error_console:
	return retval;
}

static int
imx_uart_console_exit(struct console *co)
{
	struct imx_port *sport = imx_uart_ports[co->index];

	clk_disable_unprepare(sport->clk_per);
	clk_disable_unprepare(sport->clk_ipg);

	return 0;
}

static struct uart_driver imx_uart_uart_driver;
static struct console imx_uart_console = {
	.name		= DEV_NAME,
	.write		= imx_uart_console_write,
	.device		= uart_console_device,
	.setup		= imx_uart_console_setup,
	.exit		= imx_uart_console_exit,
	.flags		= CON_PRINTBUFFER,
	.index		= -1,
	.data		= &imx_uart_uart_driver,
};

#define IMX_CONSOLE	&imx_uart_console

#else
#define IMX_CONSOLE	NULL
#endif

static struct uart_driver imx_uart_uart_driver = {
	.owner          = THIS_MODULE,
	.driver_name    = DRIVER_NAME,
	.dev_name       = DEV_NAME,
	.major          = SERIAL_IMX_MAJOR,
	.minor          = MINOR_START,
	.nr             = ARRAY_SIZE(imx_uart_ports),
	.cons           = IMX_CONSOLE,
};

static enum hrtimer_restart imx_trigger_start_tx(struct hrtimer *t)
{
	struct imx_port *sport = container_of(t, struct imx_port, trigger_start_tx);
	unsigned long flags;

	spin_lock_irqsave(&sport->port.lock, flags);
	if (sport->tx_state == WAIT_AFTER_RTS)
		imx_uart_start_tx(&sport->port);
	spin_unlock_irqrestore(&sport->port.lock, flags);

	return HRTIMER_NORESTART;
}

static enum hrtimer_restart imx_trigger_stop_tx(struct hrtimer *t)
{
	struct imx_port *sport = container_of(t, struct imx_port, trigger_stop_tx);
	unsigned long flags;

	spin_lock_irqsave(&sport->port.lock, flags);
	if (sport->tx_state == WAIT_AFTER_SEND)
		imx_uart_stop_tx(&sport->port);
	spin_unlock_irqrestore(&sport->port.lock, flags);

	return HRTIMER_NORESTART;
}

static const struct serial_rs485 imx_rs485_supported = {
	.flags = SER_RS485_ENABLED | SER_RS485_RTS_ON_SEND | SER_RS485_RTS_AFTER_SEND |
		 SER_RS485_RX_DURING_TX,
	.delay_rts_before_send = 1,
	.delay_rts_after_send = 1,
};

/* Default RX DMA buffer configuration */
#define RX_DMA_PERIODS		16
#define RX_DMA_PERIOD_LEN	(PAGE_SIZE / 4)

static int imx_uart_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct imx_port *sport;
	void __iomem *base;
	u32 dma_buf_conf[2];
	int ret = 0;
	u32 ucr1, ucr2, uts;
	struct resource *res;
	int txirq, rxirq, rtsirq;

	sport = devm_kzalloc(&pdev->dev, sizeof(*sport), GFP_KERNEL);
	if (!sport)
		return -ENOMEM;

	sport->devdata = of_device_get_match_data(&pdev->dev);

	ret = of_alias_get_id(np, "serial");
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to get alias id, errno %d\n", ret);
		return ret;
	}
	sport->port.line = ret;

	sport->have_rtscts = of_property_read_bool(np, "uart-has-rtscts") ||
		of_property_read_bool(np, "fsl,uart-has-rtscts"); /* deprecated */

	sport->dte_mode = of_property_read_bool(np, "fsl,dte-mode");

	sport->have_rtsgpio = of_property_present(np, "rts-gpios");

	sport->inverted_tx = of_property_read_bool(np, "fsl,inverted-tx");

	sport->inverted_rx = of_property_read_bool(np, "fsl,inverted-rx");

	if (!of_property_read_u32_array(np, "fsl,dma-info", dma_buf_conf, 2)) {
		sport->rx_period_length = dma_buf_conf[0];
		sport->rx_periods = dma_buf_conf[1];
	} else {
		sport->rx_period_length = RX_DMA_PERIOD_LEN;
		sport->rx_periods = RX_DMA_PERIODS;
	}

	if (sport->port.line >= ARRAY_SIZE(imx_uart_ports)) {
		dev_err(&pdev->dev, "serial%d out of range\n",
			sport->port.line);
		return -EINVAL;
	}

	base = devm_platform_get_and_ioremap_resource(pdev, 0, &res);
	if (IS_ERR(base))
		return PTR_ERR(base);

	rxirq = platform_get_irq(pdev, 0);
	if (rxirq < 0)
		return rxirq;
	txirq = platform_get_irq_optional(pdev, 1);
	rtsirq = platform_get_irq_optional(pdev, 2);

	sport->port.dev = &pdev->dev;
	sport->port.mapbase = res->start;
	sport->port.membase = base;
	sport->port.type = PORT_IMX;
	sport->port.iotype = UPIO_MEM;
	sport->port.irq = rxirq;
	sport->port.fifosize = 32;
	sport->port.has_sysrq = IS_ENABLED(CONFIG_SERIAL_IMX_CONSOLE);
	sport->port.ops = &imx_uart_pops;
	sport->port.rs485_config = imx_uart_rs485_config;
	/* RTS is required to control the RS485 transmitter */
	if (sport->have_rtscts || sport->have_rtsgpio)
		sport->port.rs485_supported = imx_rs485_supported;
	sport->port.flags = UPF_BOOT_AUTOCONF;
	timer_setup(&sport->timer, imx_uart_timeout, 0);

	sport->gpios = mctrl_gpio_init(&sport->port, 0);
	if (IS_ERR(sport->gpios))
		return PTR_ERR(sport->gpios);

	sport->clk_ipg = devm_clk_get(&pdev->dev, "ipg");
	if (IS_ERR(sport->clk_ipg)) {
		ret = PTR_ERR(sport->clk_ipg);
		dev_err(&pdev->dev, "failed to get ipg clk: %d\n", ret);
		return ret;
	}

	sport->clk_per = devm_clk_get(&pdev->dev, "per");
	if (IS_ERR(sport->clk_per)) {
		ret = PTR_ERR(sport->clk_per);
		dev_err(&pdev->dev, "failed to get per clk: %d\n", ret);
		return ret;
	}

	sport->port.uartclk = clk_get_rate(sport->clk_per);

	/* For register access, we only need to enable the ipg clock. */
	ret = clk_prepare_enable(sport->clk_ipg);
	if (ret) {
		dev_err(&pdev->dev, "failed to enable ipg clk: %d\n", ret);
		return ret;
	}

	ret = uart_get_rs485_mode(&sport->port);
	if (ret)
		goto err_clk;

	/*
	 * If using the i.MX UART RTS/CTS control then the RTS (CTS_B)
	 * signal cannot be set low during transmission in case the
	 * receiver is off (limitation of the i.MX UART IP).
	 */
	if (sport->port.rs485.flags & SER_RS485_ENABLED &&
	    sport->have_rtscts && !sport->have_rtsgpio &&
	    (!(sport->port.rs485.flags & SER_RS485_RTS_ON_SEND) &&
	     !(sport->port.rs485.flags & SER_RS485_RX_DURING_TX)))
		dev_err(&pdev->dev,
			"low-active RTS not possible when receiver is off, enabling receiver\n");

	/* Disable interrupts before requesting them */
	ucr1 = imx_uart_readl(sport, UCR1);
	ucr1 &= ~(UCR1_ADEN | UCR1_TRDYEN | UCR1_IDEN | UCR1_RRDYEN | UCR1_RTSDEN);
	imx_uart_writel(sport, ucr1, UCR1);

	/* Disable Ageing Timer interrupt */
	ucr2 = imx_uart_readl(sport, UCR2);
	ucr2 &= ~UCR2_ATEN;
	imx_uart_writel(sport, ucr2, UCR2);

	/*
	 * In case RS485 is enabled without GPIO RTS control, the UART IP
	 * is used to control CTS signal. Keep both the UART and Receiver
	 * enabled, otherwise the UART IP pulls CTS signal always HIGH no
	 * matter how the UCR2 CTSC and CTS bits are set. To prevent any
	 * data from being fed into the RX FIFO, enable loopback mode in
	 * UTS register, which disconnects the RX path from external RXD
	 * pin and connects it to the Transceiver, which is disabled, so
	 * no data can be fed to the RX FIFO that way.
	 */
	if (sport->port.rs485.flags & SER_RS485_ENABLED &&
	    sport->have_rtscts && !sport->have_rtsgpio) {
		uts = imx_uart_readl(sport, imx_uart_uts_reg(sport));
		uts |= UTS_LOOP;
		imx_uart_writel(sport, uts, imx_uart_uts_reg(sport));

		ucr1 = imx_uart_readl(sport, UCR1);
		ucr1 |= UCR1_UARTEN;
		imx_uart_writel(sport, ucr1, UCR1);

		ucr2 = imx_uart_readl(sport, UCR2);
		ucr2 |= UCR2_RXEN;
		imx_uart_writel(sport, ucr2, UCR2);
	}

	if (!imx_uart_is_imx1(sport) && sport->dte_mode) {
		/*
		 * The DCEDTE bit changes the direction of DSR, DCD, DTR and RI
		 * and influences if UCR3_RI and UCR3_DCD changes the level of RI
		 * and DCD (when they are outputs) or enables the respective
		 * irqs. So set this bit early, i.e. before requesting irqs.
		 */
		u32 ufcr = imx_uart_readl(sport, UFCR);
		if (!(ufcr & UFCR_DCEDTE))
			imx_uart_writel(sport, ufcr | UFCR_DCEDTE, UFCR);

		/*
		 * Disable UCR3_RI and UCR3_DCD irqs. They are also not
		 * enabled later because they cannot be cleared
		 * (confirmed on i.MX25) which makes them unusable.
		 */
		imx_uart_writel(sport,
				IMX21_UCR3_RXDMUXSEL | UCR3_ADNIMP | UCR3_DSR,
				UCR3);

	} else {
		u32 ucr3 = UCR3_DSR;
		u32 ufcr = imx_uart_readl(sport, UFCR);
		if (ufcr & UFCR_DCEDTE)
			imx_uart_writel(sport, ufcr & ~UFCR_DCEDTE, UFCR);

		if (!imx_uart_is_imx1(sport))
			ucr3 |= IMX21_UCR3_RXDMUXSEL | UCR3_ADNIMP;
		imx_uart_writel(sport, ucr3, UCR3);
	}

	hrtimer_init(&sport->trigger_start_tx, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	hrtimer_init(&sport->trigger_stop_tx, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	sport->trigger_start_tx.function = imx_trigger_start_tx;
	sport->trigger_stop_tx.function = imx_trigger_stop_tx;

	/*
	 * Allocate the IRQ(s) i.MX1 has three interrupts whereas later
	 * chips only have one interrupt.
	 */
	if (txirq > 0) {
		ret = devm_request_irq(&pdev->dev, rxirq, imx_uart_rxint, 0,
				       dev_name(&pdev->dev), sport);
		if (ret) {
			dev_err(&pdev->dev, "failed to request rx irq: %d\n",
				ret);
			goto err_clk;
		}

		ret = devm_request_irq(&pdev->dev, txirq, imx_uart_txint, 0,
				       dev_name(&pdev->dev), sport);
		if (ret) {
			dev_err(&pdev->dev, "failed to request tx irq: %d\n",
				ret);
			goto err_clk;
		}

		ret = devm_request_irq(&pdev->dev, rtsirq, imx_uart_rtsint, 0,
				       dev_name(&pdev->dev), sport);
		if (ret) {
			dev_err(&pdev->dev, "failed to request rts irq: %d\n",
				ret);
			goto err_clk;
		}
	} else {
		ret = devm_request_irq(&pdev->dev, rxirq, imx_uart_int, 0,
				       dev_name(&pdev->dev), sport);
		if (ret) {
			dev_err(&pdev->dev, "failed to request irq: %d\n", ret);
			goto err_clk;
		}
	}

	imx_uart_ports[sport->port.line] = sport;

	platform_set_drvdata(pdev, sport);

	ret = uart_add_one_port(&imx_uart_uart_driver, &sport->port);

err_clk:
	clk_disable_unprepare(sport->clk_ipg);

	return ret;
}

static int imx_uart_remove(struct platform_device *pdev)
{
	struct imx_port *sport = platform_get_drvdata(pdev);

	uart_remove_one_port(&imx_uart_uart_driver, &sport->port);

	return 0;
}

static void imx_uart_restore_context(struct imx_port *sport)
{
	unsigned long flags;

	spin_lock_irqsave(&sport->port.lock, flags);
	if (!sport->context_saved) {
		spin_unlock_irqrestore(&sport->port.lock, flags);
		return;
	}

	imx_uart_writel(sport, sport->saved_reg[4], UFCR);
	imx_uart_writel(sport, sport->saved_reg[5], UESC);
	imx_uart_writel(sport, sport->saved_reg[6], UTIM);
	imx_uart_writel(sport, sport->saved_reg[7], UBIR);
	imx_uart_writel(sport, sport->saved_reg[8], UBMR);
	imx_uart_writel(sport, sport->saved_reg[9], IMX21_UTS);
	imx_uart_writel(sport, sport->saved_reg[0], UCR1);
	imx_uart_writel(sport, sport->saved_reg[1] | UCR2_SRST, UCR2);
	imx_uart_writel(sport, sport->saved_reg[2], UCR3);
	imx_uart_writel(sport, sport->saved_reg[3], UCR4);
	sport->context_saved = false;
	spin_unlock_irqrestore(&sport->port.lock, flags);
}

static void imx_uart_save_context(struct imx_port *sport)
{
	unsigned long flags;

	/* Save necessary regs */
	spin_lock_irqsave(&sport->port.lock, flags);
	sport->saved_reg[0] = imx_uart_readl(sport, UCR1);
	sport->saved_reg[1] = imx_uart_readl(sport, UCR2);
	sport->saved_reg[2] = imx_uart_readl(sport, UCR3);
	sport->saved_reg[3] = imx_uart_readl(sport, UCR4);
	sport->saved_reg[4] = imx_uart_readl(sport, UFCR);
	sport->saved_reg[5] = imx_uart_readl(sport, UESC);
	sport->saved_reg[6] = imx_uart_readl(sport, UTIM);
	sport->saved_reg[7] = imx_uart_readl(sport, UBIR);
	sport->saved_reg[8] = imx_uart_readl(sport, UBMR);
	sport->saved_reg[9] = imx_uart_readl(sport, IMX21_UTS);
	sport->context_saved = true;
	spin_unlock_irqrestore(&sport->port.lock, flags);
}

static void imx_uart_enable_wakeup(struct imx_port *sport, bool on)
{
	u32 ucr3;

	ucr3 = imx_uart_readl(sport, UCR3);
	if (on) {
		imx_uart_writel(sport, USR1_AWAKE, USR1);
		ucr3 |= UCR3_AWAKEN;
	} else {
		ucr3 &= ~UCR3_AWAKEN;
	}
	imx_uart_writel(sport, ucr3, UCR3);

	if (sport->have_rtscts) {
		u32 ucr1 = imx_uart_readl(sport, UCR1);
		if (on) {
			imx_uart_writel(sport, USR1_RTSD, USR1);
			ucr1 |= UCR1_RTSDEN;
		} else {
			ucr1 &= ~UCR1_RTSDEN;
		}
		imx_uart_writel(sport, ucr1, UCR1);
	}
}

static int imx_uart_suspend_noirq(struct device *dev)
{
	struct imx_port *sport = dev_get_drvdata(dev);

	imx_uart_save_context(sport);

	clk_disable(sport->clk_ipg);

	pinctrl_pm_select_sleep_state(dev);

	return 0;
}

static int imx_uart_resume_noirq(struct device *dev)
{
	struct imx_port *sport = dev_get_drvdata(dev);
	int ret;

	pinctrl_pm_select_default_state(dev);

	ret = clk_enable(sport->clk_ipg);
	if (ret)
		return ret;

	imx_uart_restore_context(sport);

	return 0;
}

static int imx_uart_suspend(struct device *dev)
{
	struct imx_port *sport = dev_get_drvdata(dev);
	int ret;

	uart_suspend_port(&imx_uart_uart_driver, &sport->port);
	disable_irq(sport->port.irq);

	ret = clk_prepare_enable(sport->clk_ipg);
	if (ret)
		return ret;

	/* enable wakeup from i.MX UART */
	imx_uart_enable_wakeup(sport, true);

	return 0;
}

static int imx_uart_resume(struct device *dev)
{
	struct imx_port *sport = dev_get_drvdata(dev);

	/* disable wakeup from i.MX UART */
	imx_uart_enable_wakeup(sport, false);

	uart_resume_port(&imx_uart_uart_driver, &sport->port);
	enable_irq(sport->port.irq);

	clk_disable_unprepare(sport->clk_ipg);

	return 0;
}

static int imx_uart_freeze(struct device *dev)
{
	struct imx_port *sport = dev_get_drvdata(dev);

	uart_suspend_port(&imx_uart_uart_driver, &sport->port);

	return clk_prepare_enable(sport->clk_ipg);
}

static int imx_uart_thaw(struct device *dev)
{
	struct imx_port *sport = dev_get_drvdata(dev);

	uart_resume_port(&imx_uart_uart_driver, &sport->port);

	clk_disable_unprepare(sport->clk_ipg);

	return 0;
}

static const struct dev_pm_ops imx_uart_pm_ops = {
	.suspend_noirq = imx_uart_suspend_noirq,
	.resume_noirq = imx_uart_resume_noirq,
	.freeze_noirq = imx_uart_suspend_noirq,
	.thaw_noirq = imx_uart_resume_noirq,
	.restore_noirq = imx_uart_resume_noirq,
	.suspend = imx_uart_suspend,
	.resume = imx_uart_resume,
	.freeze = imx_uart_freeze,
	.thaw = imx_uart_thaw,
	.restore = imx_uart_thaw,
};

static struct platform_driver imx_uart_platform_driver = {
	.probe = imx_uart_probe,
	.remove = imx_uart_remove,

	.driver = {
		.name = "imx-uart",
		.of_match_table = imx_uart_dt_ids,
		.pm = &imx_uart_pm_ops,
	},
};

static int __init imx_uart_init(void)
{
	int ret = uart_register_driver(&imx_uart_uart_driver);

	if (ret)
		return ret;

	ret = platform_driver_register(&imx_uart_platform_driver);
	if (ret != 0)
		uart_unregister_driver(&imx_uart_uart_driver);

	return ret;
}

static void __exit imx_uart_exit(void)
{
	platform_driver_unregister(&imx_uart_platform_driver);
	uart_unregister_driver(&imx_uart_uart_driver);
}

module_init(imx_uart_init);
module_exit(imx_uart_exit);

MODULE_AUTHOR("Sascha Hauer");
MODULE_DESCRIPTION("IMX generic serial port driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:imx-uart");
