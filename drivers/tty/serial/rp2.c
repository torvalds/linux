// SPDX-License-Identifier: GPL-2.0
/*
 * Driver for Comtrol RocketPort EXPRESS/INFINITY cards
 *
 * Copyright (C) 2012 Kevin Cernekee <cernekee@gmail.com>
 *
 * Inspired by, and loosely based on:
 *
 *   ar933x_uart.c
 *     Copyright (C) 2011 Gabor Juhos <juhosg@openwrt.org>
 *
 *   rocketport_infinity_express-linux-1.20.tar.gz
 *     Copyright (C) 2004-2011 Comtrol, Inc.
 */

#include <linux/bitops.h>
#include <linux/compiler.h>
#include <linux/completion.h>
#include <linux/console.h>
#include <linux/delay.h>
#include <linux/firmware.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/log2.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/serial.h>
#include <linux/serial_core.h>
#include <linux/slab.h>
#include <linux/sysrq.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/types.h>

#define DRV_NAME			"rp2"

#define RP2_FW_NAME			"rp2.fw"
#define RP2_UCODE_BYTES			0x3f

#define PORTS_PER_ASIC			16
#define ALL_PORTS_MASK			(BIT(PORTS_PER_ASIC) - 1)

#define UART_CLOCK			44236800
#define DEFAULT_BAUD_DIV		(UART_CLOCK / (9600 * 16))
#define FIFO_SIZE			512

/* BAR0 registers */
#define RP2_FPGA_CTL0			0x110
#define RP2_FPGA_CTL1			0x11c
#define RP2_IRQ_MASK			0x1ec
#define RP2_IRQ_MASK_EN_m		BIT(0)
#define RP2_IRQ_STATUS			0x1f0

/* BAR1 registers */
#define RP2_ASIC_SPACING		0x1000
#define RP2_ASIC_OFFSET(i)		((i) << ilog2(RP2_ASIC_SPACING))

#define RP2_PORT_BASE			0x000
#define RP2_PORT_SPACING		0x040

#define RP2_UCODE_BASE			0x400
#define RP2_UCODE_SPACING		0x80

#define RP2_CLK_PRESCALER		0xc00
#define RP2_CH_IRQ_STAT			0xc04
#define RP2_CH_IRQ_MASK			0xc08
#define RP2_ASIC_IRQ			0xd00
#define RP2_ASIC_IRQ_EN_m		BIT(20)
#define RP2_GLOBAL_CMD			0xd0c
#define RP2_ASIC_CFG			0xd04

/* port registers */
#define RP2_DATA_DWORD			0x000

#define RP2_DATA_BYTE			0x008
#define RP2_DATA_BYTE_ERR_PARITY_m	BIT(8)
#define RP2_DATA_BYTE_ERR_OVERRUN_m	BIT(9)
#define RP2_DATA_BYTE_ERR_FRAMING_m	BIT(10)
#define RP2_DATA_BYTE_BREAK_m		BIT(11)

/* This lets uart_insert_char() drop bytes received on a !CREAD port */
#define RP2_DUMMY_READ			BIT(16)

#define RP2_DATA_BYTE_EXCEPTION_MASK	(RP2_DATA_BYTE_ERR_PARITY_m | \
					 RP2_DATA_BYTE_ERR_OVERRUN_m | \
					 RP2_DATA_BYTE_ERR_FRAMING_m | \
					 RP2_DATA_BYTE_BREAK_m)

#define RP2_RX_FIFO_COUNT		0x00c
#define RP2_TX_FIFO_COUNT		0x00e

#define RP2_CHAN_STAT			0x010
#define RP2_CHAN_STAT_RXDATA_m		BIT(0)
#define RP2_CHAN_STAT_DCD_m		BIT(3)
#define RP2_CHAN_STAT_DSR_m		BIT(4)
#define RP2_CHAN_STAT_CTS_m		BIT(5)
#define RP2_CHAN_STAT_RI_m		BIT(6)
#define RP2_CHAN_STAT_OVERRUN_m		BIT(13)
#define RP2_CHAN_STAT_DSR_CHANGED_m	BIT(16)
#define RP2_CHAN_STAT_CTS_CHANGED_m	BIT(17)
#define RP2_CHAN_STAT_CD_CHANGED_m	BIT(18)
#define RP2_CHAN_STAT_RI_CHANGED_m	BIT(22)
#define RP2_CHAN_STAT_TXEMPTY_m		BIT(25)

#define RP2_CHAN_STAT_MS_CHANGED_MASK	(RP2_CHAN_STAT_DSR_CHANGED_m | \
					 RP2_CHAN_STAT_CTS_CHANGED_m | \
					 RP2_CHAN_STAT_CD_CHANGED_m | \
					 RP2_CHAN_STAT_RI_CHANGED_m)

#define RP2_TXRX_CTL			0x014
#define RP2_TXRX_CTL_MSRIRQ_m		BIT(0)
#define RP2_TXRX_CTL_RXIRQ_m		BIT(2)
#define RP2_TXRX_CTL_RX_TRIG_s		3
#define RP2_TXRX_CTL_RX_TRIG_m		(0x3 << RP2_TXRX_CTL_RX_TRIG_s)
#define RP2_TXRX_CTL_RX_TRIG_1		(0x1 << RP2_TXRX_CTL_RX_TRIG_s)
#define RP2_TXRX_CTL_RX_TRIG_256	(0x2 << RP2_TXRX_CTL_RX_TRIG_s)
#define RP2_TXRX_CTL_RX_TRIG_448	(0x3 << RP2_TXRX_CTL_RX_TRIG_s)
#define RP2_TXRX_CTL_RX_EN_m		BIT(5)
#define RP2_TXRX_CTL_RTSFLOW_m		BIT(6)
#define RP2_TXRX_CTL_DTRFLOW_m		BIT(7)
#define RP2_TXRX_CTL_TX_TRIG_s		16
#define RP2_TXRX_CTL_TX_TRIG_m		(0x3 << RP2_TXRX_CTL_RX_TRIG_s)
#define RP2_TXRX_CTL_DSRFLOW_m		BIT(18)
#define RP2_TXRX_CTL_TXIRQ_m		BIT(19)
#define RP2_TXRX_CTL_CTSFLOW_m		BIT(23)
#define RP2_TXRX_CTL_TX_EN_m		BIT(24)
#define RP2_TXRX_CTL_RTS_m		BIT(25)
#define RP2_TXRX_CTL_DTR_m		BIT(26)
#define RP2_TXRX_CTL_LOOP_m		BIT(27)
#define RP2_TXRX_CTL_BREAK_m		BIT(28)
#define RP2_TXRX_CTL_CMSPAR_m		BIT(29)
#define RP2_TXRX_CTL_nPARODD_m		BIT(30)
#define RP2_TXRX_CTL_PARENB_m		BIT(31)

#define RP2_UART_CTL			0x018
#define RP2_UART_CTL_MODE_s		0
#define RP2_UART_CTL_MODE_m		(0x7 << RP2_UART_CTL_MODE_s)
#define RP2_UART_CTL_MODE_rs232		(0x1 << RP2_UART_CTL_MODE_s)
#define RP2_UART_CTL_FLUSH_RX_m		BIT(3)
#define RP2_UART_CTL_FLUSH_TX_m		BIT(4)
#define RP2_UART_CTL_RESET_CH_m		BIT(5)
#define RP2_UART_CTL_XMIT_EN_m		BIT(6)
#define RP2_UART_CTL_DATABITS_s		8
#define RP2_UART_CTL_DATABITS_m		(0x3 << RP2_UART_CTL_DATABITS_s)
#define RP2_UART_CTL_DATABITS_8		(0x3 << RP2_UART_CTL_DATABITS_s)
#define RP2_UART_CTL_DATABITS_7		(0x2 << RP2_UART_CTL_DATABITS_s)
#define RP2_UART_CTL_DATABITS_6		(0x1 << RP2_UART_CTL_DATABITS_s)
#define RP2_UART_CTL_DATABITS_5		(0x0 << RP2_UART_CTL_DATABITS_s)
#define RP2_UART_CTL_STOPBITS_m		BIT(10)

#define RP2_BAUD			0x01c

/* ucode registers */
#define RP2_TX_SWFLOW			0x02
#define RP2_TX_SWFLOW_ena		0x81
#define RP2_TX_SWFLOW_dis		0x9d

#define RP2_RX_SWFLOW			0x0c
#define RP2_RX_SWFLOW_ena		0x81
#define RP2_RX_SWFLOW_dis		0x8d

#define RP2_RX_FIFO			0x37
#define RP2_RX_FIFO_ena			0x08
#define RP2_RX_FIFO_dis			0x81

static struct uart_driver rp2_uart_driver = {
	.owner				= THIS_MODULE,
	.driver_name			= DRV_NAME,
	.dev_name			= "ttyRP",
	.nr				= CONFIG_SERIAL_RP2_NR_UARTS,
};

struct rp2_card;

struct rp2_uart_port {
	struct uart_port		port;
	int				idx;
	int				ignore_rx;
	struct rp2_card			*card;
	void __iomem			*asic_base;
	void __iomem			*base;
	void __iomem			*ucode;
};

struct rp2_card {
	struct pci_dev			*pdev;
	struct rp2_uart_port		*ports;
	int				n_ports;
	int				initialized_ports;
	int				minor_start;
	int				smpte;
	void __iomem			*bar0;
	void __iomem			*bar1;
	spinlock_t			card_lock;
};

#define RP_ID(prod) PCI_VDEVICE(RP, (prod))
#define RP_CAP(ports, smpte) (((ports) << 8) | ((smpte) << 0))

static inline void rp2_decode_cap(const struct pci_device_id *id,
				  int *ports, int *smpte)
{
	*ports = id->driver_data >> 8;
	*smpte = id->driver_data & 0xff;
}

static DEFINE_SPINLOCK(rp2_minor_lock);
static int rp2_minor_next;

static int rp2_alloc_ports(int n_ports)
{
	int ret = -ENOSPC;

	spin_lock(&rp2_minor_lock);
	if (rp2_minor_next + n_ports <= CONFIG_SERIAL_RP2_NR_UARTS) {
		/* sorry, no support for hot unplugging individual cards */
		ret = rp2_minor_next;
		rp2_minor_next += n_ports;
	}
	spin_unlock(&rp2_minor_lock);

	return ret;
}

static inline struct rp2_uart_port *port_to_up(struct uart_port *port)
{
	return container_of(port, struct rp2_uart_port, port);
}

static void rp2_rmw(struct rp2_uart_port *up, int reg,
		    u32 clr_bits, u32 set_bits)
{
	u32 tmp = readl(up->base + reg);
	tmp &= ~clr_bits;
	tmp |= set_bits;
	writel(tmp, up->base + reg);
}

static void rp2_rmw_clr(struct rp2_uart_port *up, int reg, u32 val)
{
	rp2_rmw(up, reg, val, 0);
}

static void rp2_rmw_set(struct rp2_uart_port *up, int reg, u32 val)
{
	rp2_rmw(up, reg, 0, val);
}

static void rp2_mask_ch_irq(struct rp2_uart_port *up, int ch_num,
			    int is_enabled)
{
	unsigned long flags, irq_mask;

	spin_lock_irqsave(&up->card->card_lock, flags);

	irq_mask = readl(up->asic_base + RP2_CH_IRQ_MASK);
	if (is_enabled)
		irq_mask &= ~BIT(ch_num);
	else
		irq_mask |= BIT(ch_num);
	writel(irq_mask, up->asic_base + RP2_CH_IRQ_MASK);

	spin_unlock_irqrestore(&up->card->card_lock, flags);
}

static unsigned int rp2_uart_tx_empty(struct uart_port *port)
{
	struct rp2_uart_port *up = port_to_up(port);
	unsigned long tx_fifo_bytes, flags;

	/*
	 * This should probably check the transmitter, not the FIFO.
	 * But the TXEMPTY bit doesn't seem to work unless the TX IRQ is
	 * enabled.
	 */
	uart_port_lock_irqsave(&up->port, &flags);
	tx_fifo_bytes = readw(up->base + RP2_TX_FIFO_COUNT);
	uart_port_unlock_irqrestore(&up->port, flags);

	return tx_fifo_bytes ? 0 : TIOCSER_TEMT;
}

static unsigned int rp2_uart_get_mctrl(struct uart_port *port)
{
	struct rp2_uart_port *up = port_to_up(port);
	u32 status;

	status = readl(up->base + RP2_CHAN_STAT);
	return ((status & RP2_CHAN_STAT_DCD_m) ? TIOCM_CAR : 0) |
	       ((status & RP2_CHAN_STAT_DSR_m) ? TIOCM_DSR : 0) |
	       ((status & RP2_CHAN_STAT_CTS_m) ? TIOCM_CTS : 0) |
	       ((status & RP2_CHAN_STAT_RI_m) ? TIOCM_RI : 0);
}

static void rp2_uart_set_mctrl(struct uart_port *port, unsigned int mctrl)
{
	rp2_rmw(port_to_up(port), RP2_TXRX_CTL,
		RP2_TXRX_CTL_DTR_m | RP2_TXRX_CTL_RTS_m | RP2_TXRX_CTL_LOOP_m,
		((mctrl & TIOCM_DTR) ? RP2_TXRX_CTL_DTR_m : 0) |
		((mctrl & TIOCM_RTS) ? RP2_TXRX_CTL_RTS_m : 0) |
		((mctrl & TIOCM_LOOP) ? RP2_TXRX_CTL_LOOP_m : 0));
}

static void rp2_uart_start_tx(struct uart_port *port)
{
	rp2_rmw_set(port_to_up(port), RP2_TXRX_CTL, RP2_TXRX_CTL_TXIRQ_m);
}

static void rp2_uart_stop_tx(struct uart_port *port)
{
	rp2_rmw_clr(port_to_up(port), RP2_TXRX_CTL, RP2_TXRX_CTL_TXIRQ_m);
}

static void rp2_uart_stop_rx(struct uart_port *port)
{
	rp2_rmw_clr(port_to_up(port), RP2_TXRX_CTL, RP2_TXRX_CTL_RXIRQ_m);
}

static void rp2_uart_break_ctl(struct uart_port *port, int break_state)
{
	unsigned long flags;

	uart_port_lock_irqsave(port, &flags);
	rp2_rmw(port_to_up(port), RP2_TXRX_CTL, RP2_TXRX_CTL_BREAK_m,
		break_state ? RP2_TXRX_CTL_BREAK_m : 0);
	uart_port_unlock_irqrestore(port, flags);
}

static void rp2_uart_enable_ms(struct uart_port *port)
{
	rp2_rmw_set(port_to_up(port), RP2_TXRX_CTL, RP2_TXRX_CTL_MSRIRQ_m);
}

static void __rp2_uart_set_termios(struct rp2_uart_port *up,
				   unsigned long cfl,
				   unsigned long ifl,
				   unsigned int baud_div)
{
	/* baud rate divisor (calculated elsewhere).  0 = divide-by-1 */
	writew(baud_div - 1, up->base + RP2_BAUD);

	/* data bits and stop bits */
	rp2_rmw(up, RP2_UART_CTL,
		RP2_UART_CTL_STOPBITS_m | RP2_UART_CTL_DATABITS_m,
		((cfl & CSTOPB) ? RP2_UART_CTL_STOPBITS_m : 0) |
		(((cfl & CSIZE) == CS8) ? RP2_UART_CTL_DATABITS_8 : 0) |
		(((cfl & CSIZE) == CS7) ? RP2_UART_CTL_DATABITS_7 : 0) |
		(((cfl & CSIZE) == CS6) ? RP2_UART_CTL_DATABITS_6 : 0) |
		(((cfl & CSIZE) == CS5) ? RP2_UART_CTL_DATABITS_5 : 0));

	/* parity and hardware flow control */
	rp2_rmw(up, RP2_TXRX_CTL,
		RP2_TXRX_CTL_PARENB_m | RP2_TXRX_CTL_nPARODD_m |
		RP2_TXRX_CTL_CMSPAR_m | RP2_TXRX_CTL_DTRFLOW_m |
		RP2_TXRX_CTL_DSRFLOW_m | RP2_TXRX_CTL_RTSFLOW_m |
		RP2_TXRX_CTL_CTSFLOW_m,
		((cfl & PARENB) ? RP2_TXRX_CTL_PARENB_m : 0) |
		((cfl & PARODD) ? 0 : RP2_TXRX_CTL_nPARODD_m) |
		((cfl & CMSPAR) ? RP2_TXRX_CTL_CMSPAR_m : 0) |
		((cfl & CRTSCTS) ? (RP2_TXRX_CTL_RTSFLOW_m |
				    RP2_TXRX_CTL_CTSFLOW_m) : 0));

	/* XON/XOFF software flow control */
	writeb((ifl & IXON) ? RP2_TX_SWFLOW_ena : RP2_TX_SWFLOW_dis,
	       up->ucode + RP2_TX_SWFLOW);
	writeb((ifl & IXOFF) ? RP2_RX_SWFLOW_ena : RP2_RX_SWFLOW_dis,
	       up->ucode + RP2_RX_SWFLOW);
}

static void rp2_uart_set_termios(struct uart_port *port, struct ktermios *new,
				 const struct ktermios *old)
{
	struct rp2_uart_port *up = port_to_up(port);
	unsigned long flags;
	unsigned int baud, baud_div;

	baud = uart_get_baud_rate(port, new, old, 0, port->uartclk / 16);
	baud_div = uart_get_divisor(port, baud);

	if (tty_termios_baud_rate(new))
		tty_termios_encode_baud_rate(new, baud, baud);

	uart_port_lock_irqsave(port, &flags);

	/* ignore all characters if CREAD is not set */
	port->ignore_status_mask = (new->c_cflag & CREAD) ? 0 : RP2_DUMMY_READ;

	__rp2_uart_set_termios(up, new->c_cflag, new->c_iflag, baud_div);
	uart_update_timeout(port, new->c_cflag, baud);

	uart_port_unlock_irqrestore(port, flags);
}

static void rp2_rx_chars(struct rp2_uart_port *up)
{
	u16 bytes = readw(up->base + RP2_RX_FIFO_COUNT);
	struct tty_port *port = &up->port.state->port;

	for (; bytes != 0; bytes--) {
		u32 byte = readw(up->base + RP2_DATA_BYTE) | RP2_DUMMY_READ;
		u8 ch = byte & 0xff;

		if (likely(!(byte & RP2_DATA_BYTE_EXCEPTION_MASK))) {
			if (!uart_handle_sysrq_char(&up->port, ch))
				uart_insert_char(&up->port, byte, 0, ch,
						 TTY_NORMAL);
		} else {
			u8 flag = TTY_NORMAL;

			if (byte & RP2_DATA_BYTE_BREAK_m)
				flag = TTY_BREAK;
			else if (byte & RP2_DATA_BYTE_ERR_FRAMING_m)
				flag = TTY_FRAME;
			else if (byte & RP2_DATA_BYTE_ERR_PARITY_m)
				flag = TTY_PARITY;
			uart_insert_char(&up->port, byte,
					 RP2_DATA_BYTE_ERR_OVERRUN_m, ch, flag);
		}
		up->port.icount.rx++;
	}

	tty_flip_buffer_push(port);
}

static void rp2_tx_chars(struct rp2_uart_port *up)
{
	u8 ch;

	uart_port_tx_limited(&up->port, ch,
		FIFO_SIZE - readw(up->base + RP2_TX_FIFO_COUNT),
		true,
		writeb(ch, up->base + RP2_DATA_BYTE),
		({}));
}

static void rp2_ch_interrupt(struct rp2_uart_port *up)
{
	u32 status;

	uart_port_lock(&up->port);

	/*
	 * The IRQ status bits are clear-on-write.  Other status bits in
	 * this register aren't, so it's harmless to write to them.
	 */
	status = readl(up->base + RP2_CHAN_STAT);
	writel(status, up->base + RP2_CHAN_STAT);

	if (status & RP2_CHAN_STAT_RXDATA_m)
		rp2_rx_chars(up);
	if (status & RP2_CHAN_STAT_TXEMPTY_m)
		rp2_tx_chars(up);
	if (status & RP2_CHAN_STAT_MS_CHANGED_MASK)
		wake_up_interruptible(&up->port.state->port.delta_msr_wait);

	uart_port_unlock(&up->port);
}

static int rp2_asic_interrupt(struct rp2_card *card, unsigned int asic_id)
{
	void __iomem *base = card->bar1 + RP2_ASIC_OFFSET(asic_id);
	int ch, handled = 0;
	unsigned long status = readl(base + RP2_CH_IRQ_STAT) &
			       ~readl(base + RP2_CH_IRQ_MASK);

	for_each_set_bit(ch, &status, PORTS_PER_ASIC) {
		rp2_ch_interrupt(&card->ports[ch]);
		handled++;
	}
	return handled;
}

static irqreturn_t rp2_uart_interrupt(int irq, void *dev_id)
{
	struct rp2_card *card = dev_id;
	int handled;

	handled = rp2_asic_interrupt(card, 0);
	if (card->n_ports >= PORTS_PER_ASIC)
		handled += rp2_asic_interrupt(card, 1);

	return handled ? IRQ_HANDLED : IRQ_NONE;
}

static inline void rp2_flush_fifos(struct rp2_uart_port *up)
{
	rp2_rmw_set(up, RP2_UART_CTL,
		    RP2_UART_CTL_FLUSH_RX_m | RP2_UART_CTL_FLUSH_TX_m);
	readl(up->base + RP2_UART_CTL);
	udelay(10);
	rp2_rmw_clr(up, RP2_UART_CTL,
		    RP2_UART_CTL_FLUSH_RX_m | RP2_UART_CTL_FLUSH_TX_m);
}

static int rp2_uart_startup(struct uart_port *port)
{
	struct rp2_uart_port *up = port_to_up(port);

	rp2_flush_fifos(up);
	rp2_rmw(up, RP2_TXRX_CTL, RP2_TXRX_CTL_MSRIRQ_m, RP2_TXRX_CTL_RXIRQ_m);
	rp2_rmw(up, RP2_TXRX_CTL, RP2_TXRX_CTL_RX_TRIG_m,
		RP2_TXRX_CTL_RX_TRIG_1);
	rp2_rmw(up, RP2_CHAN_STAT, 0, 0);
	rp2_mask_ch_irq(up, up->idx, 1);

	return 0;
}

static void rp2_uart_shutdown(struct uart_port *port)
{
	struct rp2_uart_port *up = port_to_up(port);
	unsigned long flags;

	rp2_uart_break_ctl(port, 0);

	uart_port_lock_irqsave(port, &flags);
	rp2_mask_ch_irq(up, up->idx, 0);
	rp2_rmw(up, RP2_CHAN_STAT, 0, 0);
	uart_port_unlock_irqrestore(port, flags);
}

static const char *rp2_uart_type(struct uart_port *port)
{
	return (port->type == PORT_RP2) ? "RocketPort 2 UART" : NULL;
}

static void rp2_uart_release_port(struct uart_port *port)
{
	/* Nothing to release ... */
}

static int rp2_uart_request_port(struct uart_port *port)
{
	/* UARTs always present */
	return 0;
}

static void rp2_uart_config_port(struct uart_port *port, int flags)
{
	if (flags & UART_CONFIG_TYPE)
		port->type = PORT_RP2;
}

static int rp2_uart_verify_port(struct uart_port *port,
				   struct serial_struct *ser)
{
	if (ser->type != PORT_UNKNOWN && ser->type != PORT_RP2)
		return -EINVAL;

	return 0;
}

static const struct uart_ops rp2_uart_ops = {
	.tx_empty	= rp2_uart_tx_empty,
	.set_mctrl	= rp2_uart_set_mctrl,
	.get_mctrl	= rp2_uart_get_mctrl,
	.stop_tx	= rp2_uart_stop_tx,
	.start_tx	= rp2_uart_start_tx,
	.stop_rx	= rp2_uart_stop_rx,
	.enable_ms	= rp2_uart_enable_ms,
	.break_ctl	= rp2_uart_break_ctl,
	.startup	= rp2_uart_startup,
	.shutdown	= rp2_uart_shutdown,
	.set_termios	= rp2_uart_set_termios,
	.type		= rp2_uart_type,
	.release_port	= rp2_uart_release_port,
	.request_port	= rp2_uart_request_port,
	.config_port	= rp2_uart_config_port,
	.verify_port	= rp2_uart_verify_port,
};

static void rp2_reset_asic(struct rp2_card *card, unsigned int asic_id)
{
	void __iomem *base = card->bar1 + RP2_ASIC_OFFSET(asic_id);
	u32 clk_cfg;

	writew(1, base + RP2_GLOBAL_CMD);
	readw(base + RP2_GLOBAL_CMD);
	msleep(100);
	writel(0, base + RP2_CLK_PRESCALER);

	/* TDM clock configuration */
	clk_cfg = readw(base + RP2_ASIC_CFG);
	clk_cfg = (clk_cfg & ~BIT(8)) | BIT(9);
	writew(clk_cfg, base + RP2_ASIC_CFG);

	/* IRQ routing */
	writel(ALL_PORTS_MASK, base + RP2_CH_IRQ_MASK);
	writel(RP2_ASIC_IRQ_EN_m, base + RP2_ASIC_IRQ);
}

static void rp2_init_card(struct rp2_card *card)
{
	writel(4, card->bar0 + RP2_FPGA_CTL0);
	writel(0, card->bar0 + RP2_FPGA_CTL1);

	rp2_reset_asic(card, 0);
	if (card->n_ports >= PORTS_PER_ASIC)
		rp2_reset_asic(card, 1);

	writel(RP2_IRQ_MASK_EN_m, card->bar0 + RP2_IRQ_MASK);
}

static void rp2_init_port(struct rp2_uart_port *up, const struct firmware *fw)
{
	int i;

	writel(RP2_UART_CTL_RESET_CH_m, up->base + RP2_UART_CTL);
	readl(up->base + RP2_UART_CTL);
	udelay(1);

	writel(0, up->base + RP2_TXRX_CTL);
	writel(0, up->base + RP2_UART_CTL);
	readl(up->base + RP2_UART_CTL);
	udelay(1);

	rp2_flush_fifos(up);

	for (i = 0; i < min_t(int, fw->size, RP2_UCODE_BYTES); i++)
		writeb(fw->data[i], up->ucode + i);

	__rp2_uart_set_termios(up, CS8 | CREAD | CLOCAL, 0, DEFAULT_BAUD_DIV);
	rp2_uart_set_mctrl(&up->port, 0);

	writeb(RP2_RX_FIFO_ena, up->ucode + RP2_RX_FIFO);
	rp2_rmw(up, RP2_UART_CTL, RP2_UART_CTL_MODE_m,
		RP2_UART_CTL_XMIT_EN_m | RP2_UART_CTL_MODE_rs232);
	rp2_rmw_set(up, RP2_TXRX_CTL,
		    RP2_TXRX_CTL_TX_EN_m | RP2_TXRX_CTL_RX_EN_m);
}

static void rp2_remove_ports(struct rp2_card *card)
{
	int i;

	for (i = 0; i < card->initialized_ports; i++)
		uart_remove_one_port(&rp2_uart_driver, &card->ports[i].port);
	card->initialized_ports = 0;
}

static int rp2_load_firmware(struct rp2_card *card, const struct firmware *fw)
{
	resource_size_t phys_base;
	int i, rc = 0;

	phys_base = pci_resource_start(card->pdev, 1);

	for (i = 0; i < card->n_ports; i++) {
		struct rp2_uart_port *rp = &card->ports[i];
		struct uart_port *p;
		int j = (unsigned)i % PORTS_PER_ASIC;

		rp->asic_base = card->bar1;
		rp->base = card->bar1 + RP2_PORT_BASE + j*RP2_PORT_SPACING;
		rp->ucode = card->bar1 + RP2_UCODE_BASE + j*RP2_UCODE_SPACING;
		rp->card = card;
		rp->idx = j;

		p = &rp->port;
		p->line = card->minor_start + i;
		p->dev = &card->pdev->dev;
		p->type = PORT_RP2;
		p->iotype = UPIO_MEM32;
		p->uartclk = UART_CLOCK;
		p->regshift = 2;
		p->fifosize = FIFO_SIZE;
		p->ops = &rp2_uart_ops;
		p->irq = card->pdev->irq;
		p->membase = rp->base;
		p->mapbase = phys_base + RP2_PORT_BASE + j*RP2_PORT_SPACING;

		if (i >= PORTS_PER_ASIC) {
			rp->asic_base += RP2_ASIC_SPACING;
			rp->base += RP2_ASIC_SPACING;
			rp->ucode += RP2_ASIC_SPACING;
			p->mapbase += RP2_ASIC_SPACING;
		}

		rp2_init_port(rp, fw);
		rc = uart_add_one_port(&rp2_uart_driver, p);
		if (rc) {
			dev_err(&card->pdev->dev,
				"error registering port %d: %d\n", i, rc);
			rp2_remove_ports(card);
			break;
		}
		card->initialized_ports++;
	}

	return rc;
}

static int rp2_probe(struct pci_dev *pdev,
				   const struct pci_device_id *id)
{
	const struct firmware *fw;
	struct rp2_card *card;
	struct rp2_uart_port *ports;
	void __iomem * const *bars;
	int rc;

	card = devm_kzalloc(&pdev->dev, sizeof(*card), GFP_KERNEL);
	if (!card)
		return -ENOMEM;
	pci_set_drvdata(pdev, card);
	spin_lock_init(&card->card_lock);

	rc = pcim_enable_device(pdev);
	if (rc)
		return rc;

	rc = pcim_iomap_regions_request_all(pdev, 0x03, DRV_NAME);
	if (rc)
		return rc;

	bars = pcim_iomap_table(pdev);
	card->bar0 = bars[0];
	card->bar1 = bars[1];
	card->pdev = pdev;

	rp2_decode_cap(id, &card->n_ports, &card->smpte);
	dev_info(&pdev->dev, "found new card with %d ports\n", card->n_ports);

	card->minor_start = rp2_alloc_ports(card->n_ports);
	if (card->minor_start < 0) {
		dev_err(&pdev->dev,
			"too many ports (try increasing CONFIG_SERIAL_RP2_NR_UARTS)\n");
		return -EINVAL;
	}

	rp2_init_card(card);

	ports = devm_kcalloc(&pdev->dev, card->n_ports, sizeof(*ports),
			     GFP_KERNEL);
	if (!ports)
		return -ENOMEM;
	card->ports = ports;

	rc = request_firmware(&fw, RP2_FW_NAME, &pdev->dev);
	if (rc < 0) {
		dev_err(&pdev->dev, "cannot find '%s' firmware image\n",
			RP2_FW_NAME);
		return rc;
	}

	rc = rp2_load_firmware(card, fw);

	release_firmware(fw);
	if (rc < 0)
		return rc;

	rc = devm_request_irq(&pdev->dev, pdev->irq, rp2_uart_interrupt,
			      IRQF_SHARED, DRV_NAME, card);
	if (rc)
		return rc;

	return 0;
}

static void rp2_remove(struct pci_dev *pdev)
{
	struct rp2_card *card = pci_get_drvdata(pdev);

	rp2_remove_ports(card);
}

static const struct pci_device_id rp2_pci_tbl[] = {

	/* RocketPort INFINITY cards */

	{ RP_ID(0x0040), RP_CAP(8,  0) }, /* INF Octa, RJ45, selectable */
	{ RP_ID(0x0041), RP_CAP(32, 0) }, /* INF 32, ext interface */
	{ RP_ID(0x0042), RP_CAP(8,  0) }, /* INF Octa, ext interface */
	{ RP_ID(0x0043), RP_CAP(16, 0) }, /* INF 16, ext interface */
	{ RP_ID(0x0044), RP_CAP(4,  0) }, /* INF Quad, DB, selectable */
	{ RP_ID(0x0045), RP_CAP(8,  0) }, /* INF Octa, DB, selectable */
	{ RP_ID(0x0046), RP_CAP(4,  0) }, /* INF Quad, ext interface */
	{ RP_ID(0x0047), RP_CAP(4,  0) }, /* INF Quad, RJ45 */
	{ RP_ID(0x004a), RP_CAP(4,  0) }, /* INF Plus, Quad */
	{ RP_ID(0x004b), RP_CAP(8,  0) }, /* INF Plus, Octa */
	{ RP_ID(0x004c), RP_CAP(8,  0) }, /* INF III, Octa */
	{ RP_ID(0x004d), RP_CAP(4,  0) }, /* INF III, Quad */
	{ RP_ID(0x004e), RP_CAP(2,  0) }, /* INF Plus, 2, RS232 */
	{ RP_ID(0x004f), RP_CAP(2,  1) }, /* INF Plus, 2, SMPTE */
	{ RP_ID(0x0050), RP_CAP(4,  0) }, /* INF Plus, Quad, RJ45 */
	{ RP_ID(0x0051), RP_CAP(8,  0) }, /* INF Plus, Octa, RJ45 */
	{ RP_ID(0x0052), RP_CAP(8,  1) }, /* INF Octa, SMPTE */

	/* RocketPort EXPRESS cards */

	{ RP_ID(0x0060), RP_CAP(8,  0) }, /* EXP Octa, RJ45, selectable */
	{ RP_ID(0x0061), RP_CAP(32, 0) }, /* EXP 32, ext interface */
	{ RP_ID(0x0062), RP_CAP(8,  0) }, /* EXP Octa, ext interface */
	{ RP_ID(0x0063), RP_CAP(16, 0) }, /* EXP 16, ext interface */
	{ RP_ID(0x0064), RP_CAP(4,  0) }, /* EXP Quad, DB, selectable */
	{ RP_ID(0x0065), RP_CAP(8,  0) }, /* EXP Octa, DB, selectable */
	{ RP_ID(0x0066), RP_CAP(4,  0) }, /* EXP Quad, ext interface */
	{ RP_ID(0x0067), RP_CAP(4,  0) }, /* EXP Quad, RJ45 */
	{ RP_ID(0x0068), RP_CAP(8,  0) }, /* EXP Octa, RJ11 */
	{ RP_ID(0x0072), RP_CAP(8,  1) }, /* EXP Octa, SMPTE */
	{ }
};
MODULE_DEVICE_TABLE(pci, rp2_pci_tbl);

static struct pci_driver rp2_pci_driver = {
	.name		= DRV_NAME,
	.id_table	= rp2_pci_tbl,
	.probe		= rp2_probe,
	.remove		= rp2_remove,
};

static int __init rp2_uart_init(void)
{
	int rc;

	rc = uart_register_driver(&rp2_uart_driver);
	if (rc)
		return rc;

	rc = pci_register_driver(&rp2_pci_driver);
	if (rc) {
		uart_unregister_driver(&rp2_uart_driver);
		return rc;
	}

	return 0;
}

static void __exit rp2_uart_exit(void)
{
	pci_unregister_driver(&rp2_pci_driver);
	uart_unregister_driver(&rp2_uart_driver);
}

module_init(rp2_uart_init);
module_exit(rp2_uart_exit);

MODULE_DESCRIPTION("Comtrol RocketPort EXPRESS/INFINITY driver");
MODULE_AUTHOR("Kevin Cernekee <cernekee@gmail.com>");
MODULE_LICENSE("GPL v2");
MODULE_FIRMWARE(RP2_FW_NAME);
