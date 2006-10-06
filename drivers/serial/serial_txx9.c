/*
 *  drivers/serial/serial_txx9.c
 *
 * Derived from many drivers using generic_serial interface,
 * especially serial_tx3912.c by Steven J. Hill and r39xx_serial.c
 * (was in Linux/VR tree) by Jim Pick.
 *
 *  Copyright (C) 1999 Harald Koerfgen
 *  Copyright (C) 2000 Jim Pick <jim@jimpick.com>
 *  Copyright (C) 2001 Steven J. Hill (sjhill@realitydiluted.com)
 *  Copyright (C) 2000-2002 Toshiba Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  Serial driver for TX3927/TX4927/TX4925/TX4938 internal SIO controller
 *
 *  Revision History:
 *	0.30	Initial revision. (Renamed from serial_txx927.c)
 *	0.31	Use save_flags instead of local_irq_save.
 *	0.32	Support SCLK.
 *	0.33	Switch TXX9_TTY_NAME by CONFIG_SERIAL_TXX9_STDSERIAL.
 *		Support TIOCSERGETLSR.
 *	0.34	Support slow baudrate.
 *	0.40	Merge codes from mainstream kernel (2.4.22).
 *	0.41	Fix console checking in rs_shutdown_port().
 *		Disable flow-control in serial_console_write().
 *	0.42	Fix minor compiler warning.
 *	1.00	Kernel 2.6.  Converted to new serial core (based on 8250.c).
 *	1.01	Set fifosize to make tx_empry called properly.
 *		Use standard uart_get_divisor.
 *	1.02	Cleanup. (import 8250.c changes)
 *	1.03	Fix low-latency mode. (import 8250.c changes)
 *	1.04	Remove usage of deprecated functions, cleanup.
 *	1.05	More strict check in verify_port.  Cleanup.
 *	1.06	Do not insert a char caused previous overrun.
 *		Fix some spin_locks.
 *		Do not call uart_add_one_port for absent ports.
 */

#if defined(CONFIG_SERIAL_TXX9_CONSOLE) && defined(CONFIG_MAGIC_SYSRQ)
#define SUPPORT_SYSRQ
#endif

#include <linux/module.h>
#include <linux/ioport.h>
#include <linux/init.h>
#include <linux/console.h>
#include <linux/sysrq.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/pci.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/serial_core.h>
#include <linux/serial.h>
#include <linux/mutex.h>

#include <asm/io.h>
#include <asm/irq.h>

static char *serial_version = "1.06";
static char *serial_name = "TX39/49 Serial driver";

#define PASS_LIMIT	256

#if !defined(CONFIG_SERIAL_TXX9_STDSERIAL)
/* "ttyS" is used for standard serial driver */
#define TXX9_TTY_NAME "ttyTX"
#define TXX9_TTY_MINOR_START	(64 + 64)	/* ttyTX0(128), ttyTX1(129) */
#else
/* acts like standard serial driver */
#define TXX9_TTY_NAME "ttyS"
#define TXX9_TTY_MINOR_START	64
#endif
#define TXX9_TTY_MAJOR	TTY_MAJOR

/* flag aliases */
#define UPF_TXX9_HAVE_CTS_LINE	UPF_BUGGY_UART
#define UPF_TXX9_USE_SCLK	UPF_MAGIC_MULTIPLIER

#ifdef CONFIG_PCI
/* support for Toshiba TC86C001 SIO */
#define ENABLE_SERIAL_TXX9_PCI
#endif

/*
 * Number of serial ports
 */
#ifdef ENABLE_SERIAL_TXX9_PCI
#define NR_PCI_BOARDS	4
#define UART_NR  (4 + NR_PCI_BOARDS)
#else
#define UART_NR  4
#endif

#define HIGH_BITS_OFFSET	((sizeof(long)-sizeof(int))*8)

struct uart_txx9_port {
	struct uart_port	port;

	/*
	 * We provide a per-port pm hook.
	 */
	void			(*pm)(struct uart_port *port,
				      unsigned int state, unsigned int old);
};

#define TXX9_REGION_SIZE	0x24

/* TXX9 Serial Registers */
#define TXX9_SILCR	0x00
#define TXX9_SIDICR	0x04
#define TXX9_SIDISR	0x08
#define TXX9_SICISR	0x0c
#define TXX9_SIFCR	0x10
#define TXX9_SIFLCR	0x14
#define TXX9_SIBGR	0x18
#define TXX9_SITFIFO	0x1c
#define TXX9_SIRFIFO	0x20

/* SILCR : Line Control */
#define TXX9_SILCR_SCS_MASK	0x00000060
#define TXX9_SILCR_SCS_IMCLK	0x00000000
#define TXX9_SILCR_SCS_IMCLK_BG	0x00000020
#define TXX9_SILCR_SCS_SCLK	0x00000040
#define TXX9_SILCR_SCS_SCLK_BG	0x00000060
#define TXX9_SILCR_UEPS	0x00000010
#define TXX9_SILCR_UPEN	0x00000008
#define TXX9_SILCR_USBL_MASK	0x00000004
#define TXX9_SILCR_USBL_1BIT	0x00000000
#define TXX9_SILCR_USBL_2BIT	0x00000004
#define TXX9_SILCR_UMODE_MASK	0x00000003
#define TXX9_SILCR_UMODE_8BIT	0x00000000
#define TXX9_SILCR_UMODE_7BIT	0x00000001

/* SIDICR : DMA/Int. Control */
#define TXX9_SIDICR_TDE	0x00008000
#define TXX9_SIDICR_RDE	0x00004000
#define TXX9_SIDICR_TIE	0x00002000
#define TXX9_SIDICR_RIE	0x00001000
#define TXX9_SIDICR_SPIE	0x00000800
#define TXX9_SIDICR_CTSAC	0x00000600
#define TXX9_SIDICR_STIE_MASK	0x0000003f
#define TXX9_SIDICR_STIE_OERS		0x00000020
#define TXX9_SIDICR_STIE_CTSS		0x00000010
#define TXX9_SIDICR_STIE_RBRKD	0x00000008
#define TXX9_SIDICR_STIE_TRDY		0x00000004
#define TXX9_SIDICR_STIE_TXALS	0x00000002
#define TXX9_SIDICR_STIE_UBRKD	0x00000001

/* SIDISR : DMA/Int. Status */
#define TXX9_SIDISR_UBRK	0x00008000
#define TXX9_SIDISR_UVALID	0x00004000
#define TXX9_SIDISR_UFER	0x00002000
#define TXX9_SIDISR_UPER	0x00001000
#define TXX9_SIDISR_UOER	0x00000800
#define TXX9_SIDISR_ERI	0x00000400
#define TXX9_SIDISR_TOUT	0x00000200
#define TXX9_SIDISR_TDIS	0x00000100
#define TXX9_SIDISR_RDIS	0x00000080
#define TXX9_SIDISR_STIS	0x00000040
#define TXX9_SIDISR_RFDN_MASK	0x0000001f

/* SICISR : Change Int. Status */
#define TXX9_SICISR_OERS	0x00000020
#define TXX9_SICISR_CTSS	0x00000010
#define TXX9_SICISR_RBRKD	0x00000008
#define TXX9_SICISR_TRDY	0x00000004
#define TXX9_SICISR_TXALS	0x00000002
#define TXX9_SICISR_UBRKD	0x00000001

/* SIFCR : FIFO Control */
#define TXX9_SIFCR_SWRST	0x00008000
#define TXX9_SIFCR_RDIL_MASK	0x00000180
#define TXX9_SIFCR_RDIL_1	0x00000000
#define TXX9_SIFCR_RDIL_4	0x00000080
#define TXX9_SIFCR_RDIL_8	0x00000100
#define TXX9_SIFCR_RDIL_12	0x00000180
#define TXX9_SIFCR_RDIL_MAX	0x00000180
#define TXX9_SIFCR_TDIL_MASK	0x00000018
#define TXX9_SIFCR_TDIL_MASK	0x00000018
#define TXX9_SIFCR_TDIL_1	0x00000000
#define TXX9_SIFCR_TDIL_4	0x00000001
#define TXX9_SIFCR_TDIL_8	0x00000010
#define TXX9_SIFCR_TDIL_MAX	0x00000010
#define TXX9_SIFCR_TFRST	0x00000004
#define TXX9_SIFCR_RFRST	0x00000002
#define TXX9_SIFCR_FRSTE	0x00000001
#define TXX9_SIO_TX_FIFO	8
#define TXX9_SIO_RX_FIFO	16

/* SIFLCR : Flow Control */
#define TXX9_SIFLCR_RCS	0x00001000
#define TXX9_SIFLCR_TES	0x00000800
#define TXX9_SIFLCR_RTSSC	0x00000200
#define TXX9_SIFLCR_RSDE	0x00000100
#define TXX9_SIFLCR_TSDE	0x00000080
#define TXX9_SIFLCR_RTSTL_MASK	0x0000001e
#define TXX9_SIFLCR_RTSTL_MAX	0x0000001e
#define TXX9_SIFLCR_TBRK	0x00000001

/* SIBGR : Baudrate Control */
#define TXX9_SIBGR_BCLK_MASK	0x00000300
#define TXX9_SIBGR_BCLK_T0	0x00000000
#define TXX9_SIBGR_BCLK_T2	0x00000100
#define TXX9_SIBGR_BCLK_T4	0x00000200
#define TXX9_SIBGR_BCLK_T6	0x00000300
#define TXX9_SIBGR_BRD_MASK	0x000000ff

static inline unsigned int sio_in(struct uart_txx9_port *up, int offset)
{
	switch (up->port.iotype) {
	default:
		return __raw_readl(up->port.membase + offset);
	case UPIO_PORT:
		return inl(up->port.iobase + offset);
	}
}

static inline void
sio_out(struct uart_txx9_port *up, int offset, int value)
{
	switch (up->port.iotype) {
	default:
		__raw_writel(value, up->port.membase + offset);
		break;
	case UPIO_PORT:
		outl(value, up->port.iobase + offset);
		break;
	}
}

static inline void
sio_mask(struct uart_txx9_port *up, int offset, unsigned int value)
{
	sio_out(up, offset, sio_in(up, offset) & ~value);
}
static inline void
sio_set(struct uart_txx9_port *up, int offset, unsigned int value)
{
	sio_out(up, offset, sio_in(up, offset) | value);
}

static inline void
sio_quot_set(struct uart_txx9_port *up, int quot)
{
	quot >>= 1;
	if (quot < 256)
		sio_out(up, TXX9_SIBGR, quot | TXX9_SIBGR_BCLK_T0);
	else if (quot < (256 << 2))
		sio_out(up, TXX9_SIBGR, (quot >> 2) | TXX9_SIBGR_BCLK_T2);
	else if (quot < (256 << 4))
		sio_out(up, TXX9_SIBGR, (quot >> 4) | TXX9_SIBGR_BCLK_T4);
	else if (quot < (256 << 6))
		sio_out(up, TXX9_SIBGR, (quot >> 6) | TXX9_SIBGR_BCLK_T6);
	else
		sio_out(up, TXX9_SIBGR, 0xff | TXX9_SIBGR_BCLK_T6);
}

static void serial_txx9_stop_tx(struct uart_port *port)
{
	struct uart_txx9_port *up = (struct uart_txx9_port *)port;
	sio_mask(up, TXX9_SIDICR, TXX9_SIDICR_TIE);
}

static void serial_txx9_start_tx(struct uart_port *port)
{
	struct uart_txx9_port *up = (struct uart_txx9_port *)port;
	sio_set(up, TXX9_SIDICR, TXX9_SIDICR_TIE);
}

static void serial_txx9_stop_rx(struct uart_port *port)
{
	struct uart_txx9_port *up = (struct uart_txx9_port *)port;
	up->port.read_status_mask &= ~TXX9_SIDISR_RDIS;
}

static void serial_txx9_enable_ms(struct uart_port *port)
{
	/* TXX9-SIO can not control DTR... */
}

static inline void
receive_chars(struct uart_txx9_port *up, unsigned int *status)
{
	struct tty_struct *tty = up->port.info->tty;
	unsigned char ch;
	unsigned int disr = *status;
	int max_count = 256;
	char flag;
	unsigned int next_ignore_status_mask;

	do {
		ch = sio_in(up, TXX9_SIRFIFO);
		flag = TTY_NORMAL;
		up->port.icount.rx++;

		/* mask out RFDN_MASK bit added by previous overrun */
		next_ignore_status_mask =
			up->port.ignore_status_mask & ~TXX9_SIDISR_RFDN_MASK;
		if (unlikely(disr & (TXX9_SIDISR_UBRK | TXX9_SIDISR_UPER |
				     TXX9_SIDISR_UFER | TXX9_SIDISR_UOER))) {
			/*
			 * For statistics only
			 */
			if (disr & TXX9_SIDISR_UBRK) {
				disr &= ~(TXX9_SIDISR_UFER | TXX9_SIDISR_UPER);
				up->port.icount.brk++;
				/*
				 * We do the SysRQ and SAK checking
				 * here because otherwise the break
				 * may get masked by ignore_status_mask
				 * or read_status_mask.
				 */
				if (uart_handle_break(&up->port))
					goto ignore_char;
			} else if (disr & TXX9_SIDISR_UPER)
				up->port.icount.parity++;
			else if (disr & TXX9_SIDISR_UFER)
				up->port.icount.frame++;
			if (disr & TXX9_SIDISR_UOER) {
				up->port.icount.overrun++;
				/*
				 * The receiver read buffer still hold
				 * a char which caused overrun.
				 * Ignore next char by adding RFDN_MASK
				 * to ignore_status_mask temporarily.
				 */
				next_ignore_status_mask |=
					TXX9_SIDISR_RFDN_MASK;
			}

			/*
			 * Mask off conditions which should be ingored.
			 */
			disr &= up->port.read_status_mask;

			if (disr & TXX9_SIDISR_UBRK) {
				flag = TTY_BREAK;
			} else if (disr & TXX9_SIDISR_UPER)
				flag = TTY_PARITY;
			else if (disr & TXX9_SIDISR_UFER)
				flag = TTY_FRAME;
		}
		if (uart_handle_sysrq_char(&up->port, ch))
			goto ignore_char;

		uart_insert_char(&up->port, disr, TXX9_SIDISR_UOER, ch, flag);

	ignore_char:
		up->port.ignore_status_mask = next_ignore_status_mask;
		disr = sio_in(up, TXX9_SIDISR);
	} while (!(disr & TXX9_SIDISR_UVALID) && (max_count-- > 0));
	spin_unlock(&up->port.lock);
	tty_flip_buffer_push(tty);
	spin_lock(&up->port.lock);
	*status = disr;
}

static inline void transmit_chars(struct uart_txx9_port *up)
{
	struct circ_buf *xmit = &up->port.info->xmit;
	int count;

	if (up->port.x_char) {
		sio_out(up, TXX9_SITFIFO, up->port.x_char);
		up->port.icount.tx++;
		up->port.x_char = 0;
		return;
	}
	if (uart_circ_empty(xmit) || uart_tx_stopped(&up->port)) {
		serial_txx9_stop_tx(&up->port);
		return;
	}

	count = TXX9_SIO_TX_FIFO;
	do {
		sio_out(up, TXX9_SITFIFO, xmit->buf[xmit->tail]);
		xmit->tail = (xmit->tail + 1) & (UART_XMIT_SIZE - 1);
		up->port.icount.tx++;
		if (uart_circ_empty(xmit))
			break;
	} while (--count > 0);

	if (uart_circ_chars_pending(xmit) < WAKEUP_CHARS)
		uart_write_wakeup(&up->port);

	if (uart_circ_empty(xmit))
		serial_txx9_stop_tx(&up->port);
}

static irqreturn_t serial_txx9_interrupt(int irq, void *dev_id)
{
	int pass_counter = 0;
	struct uart_txx9_port *up = dev_id;
	unsigned int status;

	while (1) {
		spin_lock(&up->port.lock);
		status = sio_in(up, TXX9_SIDISR);
		if (!(sio_in(up, TXX9_SIDICR) & TXX9_SIDICR_TIE))
			status &= ~TXX9_SIDISR_TDIS;
		if (!(status & (TXX9_SIDISR_TDIS | TXX9_SIDISR_RDIS |
				TXX9_SIDISR_TOUT))) {
			spin_unlock(&up->port.lock);
			break;
		}

		if (status & TXX9_SIDISR_RDIS)
			receive_chars(up, &status);
		if (status & TXX9_SIDISR_TDIS)
			transmit_chars(up);
		/* Clear TX/RX Int. Status */
		sio_mask(up, TXX9_SIDISR,
			 TXX9_SIDISR_TDIS | TXX9_SIDISR_RDIS |
			 TXX9_SIDISR_TOUT);
		spin_unlock(&up->port.lock);

		if (pass_counter++ > PASS_LIMIT)
			break;
	}

	return pass_counter ? IRQ_HANDLED : IRQ_NONE;
}

static unsigned int serial_txx9_tx_empty(struct uart_port *port)
{
	struct uart_txx9_port *up = (struct uart_txx9_port *)port;
	unsigned long flags;
	unsigned int ret;

	spin_lock_irqsave(&up->port.lock, flags);
	ret = (sio_in(up, TXX9_SICISR) & TXX9_SICISR_TXALS) ? TIOCSER_TEMT : 0;
	spin_unlock_irqrestore(&up->port.lock, flags);

	return ret;
}

static unsigned int serial_txx9_get_mctrl(struct uart_port *port)
{
	struct uart_txx9_port *up = (struct uart_txx9_port *)port;
	unsigned int ret;

	ret =  ((sio_in(up, TXX9_SIFLCR) & TXX9_SIFLCR_RTSSC) ? 0 : TIOCM_RTS)
		| ((sio_in(up, TXX9_SICISR) & TXX9_SICISR_CTSS) ? 0 : TIOCM_CTS);

	return ret;
}

static void serial_txx9_set_mctrl(struct uart_port *port, unsigned int mctrl)
{
	struct uart_txx9_port *up = (struct uart_txx9_port *)port;

	if (mctrl & TIOCM_RTS)
		sio_mask(up, TXX9_SIFLCR, TXX9_SIFLCR_RTSSC);
	else
		sio_set(up, TXX9_SIFLCR, TXX9_SIFLCR_RTSSC);
}

static void serial_txx9_break_ctl(struct uart_port *port, int break_state)
{
	struct uart_txx9_port *up = (struct uart_txx9_port *)port;
	unsigned long flags;

	spin_lock_irqsave(&up->port.lock, flags);
	if (break_state == -1)
		sio_set(up, TXX9_SIFLCR, TXX9_SIFLCR_TBRK);
	else
		sio_mask(up, TXX9_SIFLCR, TXX9_SIFLCR_TBRK);
	spin_unlock_irqrestore(&up->port.lock, flags);
}

static int serial_txx9_startup(struct uart_port *port)
{
	struct uart_txx9_port *up = (struct uart_txx9_port *)port;
	unsigned long flags;
	int retval;

	/*
	 * Clear the FIFO buffers and disable them.
	 * (they will be reenabled in set_termios())
	 */
	sio_set(up, TXX9_SIFCR,
		TXX9_SIFCR_TFRST | TXX9_SIFCR_RFRST | TXX9_SIFCR_FRSTE);
	/* clear reset */
	sio_mask(up, TXX9_SIFCR,
		 TXX9_SIFCR_TFRST | TXX9_SIFCR_RFRST | TXX9_SIFCR_FRSTE);
	sio_out(up, TXX9_SIDICR, 0);

	/*
	 * Clear the interrupt registers.
	 */
	sio_out(up, TXX9_SIDISR, 0);

	retval = request_irq(up->port.irq, serial_txx9_interrupt,
			     IRQF_SHARED, "serial_txx9", up);
	if (retval)
		return retval;

	/*
	 * Now, initialize the UART
	 */
	spin_lock_irqsave(&up->port.lock, flags);
	serial_txx9_set_mctrl(&up->port, up->port.mctrl);
	spin_unlock_irqrestore(&up->port.lock, flags);

	/* Enable RX/TX */
	sio_mask(up, TXX9_SIFLCR, TXX9_SIFLCR_RSDE | TXX9_SIFLCR_TSDE);

	/*
	 * Finally, enable interrupts.
	 */
	sio_set(up, TXX9_SIDICR, TXX9_SIDICR_RIE);

	return 0;
}

static void serial_txx9_shutdown(struct uart_port *port)
{
	struct uart_txx9_port *up = (struct uart_txx9_port *)port;
	unsigned long flags;

	/*
	 * Disable interrupts from this port
	 */
	sio_out(up, TXX9_SIDICR, 0);	/* disable all intrs */

	spin_lock_irqsave(&up->port.lock, flags);
	serial_txx9_set_mctrl(&up->port, up->port.mctrl);
	spin_unlock_irqrestore(&up->port.lock, flags);

	/*
	 * Disable break condition
	 */
	sio_mask(up, TXX9_SIFLCR, TXX9_SIFLCR_TBRK);

#ifdef CONFIG_SERIAL_TXX9_CONSOLE
	if (up->port.cons && up->port.line == up->port.cons->index) {
		free_irq(up->port.irq, up);
		return;
	}
#endif
	/* reset FIFOs */
	sio_set(up, TXX9_SIFCR,
		TXX9_SIFCR_TFRST | TXX9_SIFCR_RFRST | TXX9_SIFCR_FRSTE);
	/* clear reset */
	sio_mask(up, TXX9_SIFCR,
		 TXX9_SIFCR_TFRST | TXX9_SIFCR_RFRST | TXX9_SIFCR_FRSTE);

	/* Disable RX/TX */
	sio_set(up, TXX9_SIFLCR, TXX9_SIFLCR_RSDE | TXX9_SIFLCR_TSDE);

	free_irq(up->port.irq, up);
}

static void
serial_txx9_set_termios(struct uart_port *port, struct termios *termios,
		       struct termios *old)
{
	struct uart_txx9_port *up = (struct uart_txx9_port *)port;
	unsigned int cval, fcr = 0;
	unsigned long flags;
	unsigned int baud, quot;

	cval = sio_in(up, TXX9_SILCR);
	/* byte size and parity */
	cval &= ~TXX9_SILCR_UMODE_MASK;
	switch (termios->c_cflag & CSIZE) {
	case CS7:
		cval |= TXX9_SILCR_UMODE_7BIT;
		break;
	default:
	case CS5:	/* not supported */
	case CS6:	/* not supported */
	case CS8:
		cval |= TXX9_SILCR_UMODE_8BIT;
		break;
	}

	cval &= ~TXX9_SILCR_USBL_MASK;
	if (termios->c_cflag & CSTOPB)
		cval |= TXX9_SILCR_USBL_2BIT;
	else
		cval |= TXX9_SILCR_USBL_1BIT;
	cval &= ~(TXX9_SILCR_UPEN | TXX9_SILCR_UEPS);
	if (termios->c_cflag & PARENB)
		cval |= TXX9_SILCR_UPEN;
	if (!(termios->c_cflag & PARODD))
		cval |= TXX9_SILCR_UEPS;

	/*
	 * Ask the core to calculate the divisor for us.
	 */
	baud = uart_get_baud_rate(port, termios, old, 0, port->uartclk/16/2);
	quot = uart_get_divisor(port, baud);

	/* Set up FIFOs */
	/* TX Int by FIFO Empty, RX Int by Receiving 1 char. */
	fcr = TXX9_SIFCR_TDIL_MAX | TXX9_SIFCR_RDIL_1;

	/*
	 * Ok, we're now changing the port state.  Do it with
	 * interrupts disabled.
	 */
	spin_lock_irqsave(&up->port.lock, flags);

	/*
	 * Update the per-port timeout.
	 */
	uart_update_timeout(port, termios->c_cflag, baud);

	up->port.read_status_mask = TXX9_SIDISR_UOER |
		TXX9_SIDISR_TDIS | TXX9_SIDISR_RDIS;
	if (termios->c_iflag & INPCK)
		up->port.read_status_mask |= TXX9_SIDISR_UFER | TXX9_SIDISR_UPER;
	if (termios->c_iflag & (BRKINT | PARMRK))
		up->port.read_status_mask |= TXX9_SIDISR_UBRK;

	/*
	 * Characteres to ignore
	 */
	up->port.ignore_status_mask = 0;
	if (termios->c_iflag & IGNPAR)
		up->port.ignore_status_mask |= TXX9_SIDISR_UPER | TXX9_SIDISR_UFER;
	if (termios->c_iflag & IGNBRK) {
		up->port.ignore_status_mask |= TXX9_SIDISR_UBRK;
		/*
		 * If we're ignoring parity and break indicators,
		 * ignore overruns too (for real raw support).
		 */
		if (termios->c_iflag & IGNPAR)
			up->port.ignore_status_mask |= TXX9_SIDISR_UOER;
	}

	/*
	 * ignore all characters if CREAD is not set
	 */
	if ((termios->c_cflag & CREAD) == 0)
		up->port.ignore_status_mask |= TXX9_SIDISR_RDIS;

	/* CTS flow control flag */
	if ((termios->c_cflag & CRTSCTS) &&
	    (up->port.flags & UPF_TXX9_HAVE_CTS_LINE)) {
		sio_set(up, TXX9_SIFLCR,
			TXX9_SIFLCR_RCS | TXX9_SIFLCR_TES);
	} else {
		sio_mask(up, TXX9_SIFLCR,
			 TXX9_SIFLCR_RCS | TXX9_SIFLCR_TES);
	}

	sio_out(up, TXX9_SILCR, cval);
	sio_quot_set(up, quot);
	sio_out(up, TXX9_SIFCR, fcr);

	serial_txx9_set_mctrl(&up->port, up->port.mctrl);
	spin_unlock_irqrestore(&up->port.lock, flags);
}

static void
serial_txx9_pm(struct uart_port *port, unsigned int state,
	      unsigned int oldstate)
{
	struct uart_txx9_port *up = (struct uart_txx9_port *)port;
	if (up->pm)
		up->pm(port, state, oldstate);
}

static int serial_txx9_request_resource(struct uart_txx9_port *up)
{
	unsigned int size = TXX9_REGION_SIZE;
	int ret = 0;

	switch (up->port.iotype) {
	default:
		if (!up->port.mapbase)
			break;

		if (!request_mem_region(up->port.mapbase, size, "serial_txx9")) {
			ret = -EBUSY;
			break;
		}

		if (up->port.flags & UPF_IOREMAP) {
			up->port.membase = ioremap(up->port.mapbase, size);
			if (!up->port.membase) {
				release_mem_region(up->port.mapbase, size);
				ret = -ENOMEM;
			}
		}
		break;

	case UPIO_PORT:
		if (!request_region(up->port.iobase, size, "serial_txx9"))
			ret = -EBUSY;
		break;
	}
	return ret;
}

static void serial_txx9_release_resource(struct uart_txx9_port *up)
{
	unsigned int size = TXX9_REGION_SIZE;

	switch (up->port.iotype) {
	default:
		if (!up->port.mapbase)
			break;

		if (up->port.flags & UPF_IOREMAP) {
			iounmap(up->port.membase);
			up->port.membase = NULL;
		}

		release_mem_region(up->port.mapbase, size);
		break;

	case UPIO_PORT:
		release_region(up->port.iobase, size);
		break;
	}
}

static void serial_txx9_release_port(struct uart_port *port)
{
	struct uart_txx9_port *up = (struct uart_txx9_port *)port;
	serial_txx9_release_resource(up);
}

static int serial_txx9_request_port(struct uart_port *port)
{
	struct uart_txx9_port *up = (struct uart_txx9_port *)port;
	return serial_txx9_request_resource(up);
}

static void serial_txx9_config_port(struct uart_port *port, int uflags)
{
	struct uart_txx9_port *up = (struct uart_txx9_port *)port;
	unsigned long flags;
	int ret;

	/*
	 * Find the region that we can probe for.  This in turn
	 * tells us whether we can probe for the type of port.
	 */
	ret = serial_txx9_request_resource(up);
	if (ret < 0)
		return;
	port->type = PORT_TXX9;
	up->port.fifosize = TXX9_SIO_TX_FIFO;

#ifdef CONFIG_SERIAL_TXX9_CONSOLE
	if (up->port.line == up->port.cons->index)
		return;
#endif
	spin_lock_irqsave(&up->port.lock, flags);
	/*
	 * Reset the UART.
	 */
	sio_out(up, TXX9_SIFCR, TXX9_SIFCR_SWRST);
#ifdef CONFIG_CPU_TX49XX
	/* TX4925 BUG WORKAROUND.  Accessing SIOC register
	 * immediately after soft reset causes bus error. */
	iob();
	udelay(1);
#endif
	while (sio_in(up, TXX9_SIFCR) & TXX9_SIFCR_SWRST)
		;
	/* TX Int by FIFO Empty, RX Int by Receiving 1 char. */
	sio_set(up, TXX9_SIFCR,
		TXX9_SIFCR_TDIL_MAX | TXX9_SIFCR_RDIL_1);
	/* initial settings */
	sio_out(up, TXX9_SILCR,
		TXX9_SILCR_UMODE_8BIT | TXX9_SILCR_USBL_1BIT |
		((up->port.flags & UPF_TXX9_USE_SCLK) ?
		 TXX9_SILCR_SCS_SCLK_BG : TXX9_SILCR_SCS_IMCLK_BG));
	sio_quot_set(up, uart_get_divisor(port, 9600));
	sio_out(up, TXX9_SIFLCR, TXX9_SIFLCR_RTSTL_MAX /* 15 */);
	spin_unlock_irqrestore(&up->port.lock, flags);
}

static int
serial_txx9_verify_port(struct uart_port *port, struct serial_struct *ser)
{
	unsigned long new_port = ser->port;
	if (HIGH_BITS_OFFSET)
		new_port += (unsigned long)ser->port_high << HIGH_BITS_OFFSET;
	if (ser->type != port->type ||
	    ser->irq != port->irq ||
	    ser->io_type != port->iotype ||
	    new_port != port->iobase ||
	    (unsigned long)ser->iomem_base != port->mapbase)
		return -EINVAL;
	return 0;
}

static const char *
serial_txx9_type(struct uart_port *port)
{
	return "txx9";
}

static struct uart_ops serial_txx9_pops = {
	.tx_empty	= serial_txx9_tx_empty,
	.set_mctrl	= serial_txx9_set_mctrl,
	.get_mctrl	= serial_txx9_get_mctrl,
	.stop_tx	= serial_txx9_stop_tx,
	.start_tx	= serial_txx9_start_tx,
	.stop_rx	= serial_txx9_stop_rx,
	.enable_ms	= serial_txx9_enable_ms,
	.break_ctl	= serial_txx9_break_ctl,
	.startup	= serial_txx9_startup,
	.shutdown	= serial_txx9_shutdown,
	.set_termios	= serial_txx9_set_termios,
	.pm		= serial_txx9_pm,
	.type		= serial_txx9_type,
	.release_port	= serial_txx9_release_port,
	.request_port	= serial_txx9_request_port,
	.config_port	= serial_txx9_config_port,
	.verify_port	= serial_txx9_verify_port,
};

static struct uart_txx9_port serial_txx9_ports[UART_NR];

static void __init serial_txx9_register_ports(struct uart_driver *drv)
{
	int i;

	for (i = 0; i < UART_NR; i++) {
		struct uart_txx9_port *up = &serial_txx9_ports[i];

		up->port.line = i;
		up->port.ops = &serial_txx9_pops;
		if (up->port.iobase || up->port.mapbase)
			uart_add_one_port(drv, &up->port);
	}
}

#ifdef CONFIG_SERIAL_TXX9_CONSOLE

/*
 *	Wait for transmitter & holding register to empty
 */
static inline void wait_for_xmitr(struct uart_txx9_port *up)
{
	unsigned int tmout = 10000;

	/* Wait up to 10ms for the character(s) to be sent. */
	while (--tmout &&
	       !(sio_in(up, TXX9_SICISR) & TXX9_SICISR_TXALS))
		udelay(1);

	/* Wait up to 1s for flow control if necessary */
	if (up->port.flags & UPF_CONS_FLOW) {
		tmout = 1000000;
		while (--tmout &&
		       (sio_in(up, TXX9_SICISR) & TXX9_SICISR_CTSS))
			udelay(1);
	}
}

static void serial_txx9_console_putchar(struct uart_port *port, int ch)
{
	struct uart_txx9_port *up = (struct uart_txx9_port *)port;

	wait_for_xmitr(up);
	sio_out(up, TXX9_SITFIFO, ch);
}

/*
 *	Print a string to the serial port trying not to disturb
 *	any possible real use of the port...
 *
 *	The console_lock must be held when we get here.
 */
static void
serial_txx9_console_write(struct console *co, const char *s, unsigned int count)
{
	struct uart_txx9_port *up = &serial_txx9_ports[co->index];
	unsigned int ier, flcr;

	/*
	 *	First save the UER then disable the interrupts
	 */
	ier = sio_in(up, TXX9_SIDICR);
	sio_out(up, TXX9_SIDICR, 0);
	/*
	 *	Disable flow-control if enabled (and unnecessary)
	 */
	flcr = sio_in(up, TXX9_SIFLCR);
	if (!(up->port.flags & UPF_CONS_FLOW) && (flcr & TXX9_SIFLCR_TES))
		sio_out(up, TXX9_SIFLCR, flcr & ~TXX9_SIFLCR_TES);

	uart_console_write(&up->port, s, count, serial_txx9_console_putchar);

	/*
	 *	Finally, wait for transmitter to become empty
	 *	and restore the IER
	 */
	wait_for_xmitr(up);
	sio_out(up, TXX9_SIFLCR, flcr);
	sio_out(up, TXX9_SIDICR, ier);
}

static int serial_txx9_console_setup(struct console *co, char *options)
{
	struct uart_port *port;
	struct uart_txx9_port *up;
	int baud = 9600;
	int bits = 8;
	int parity = 'n';
	int flow = 'n';

	/*
	 * Check whether an invalid uart number has been specified, and
	 * if so, search for the first available port that does have
	 * console support.
	 */
	if (co->index >= UART_NR)
		co->index = 0;
	up = &serial_txx9_ports[co->index];
	port = &up->port;
	if (!port->ops)
		return -ENODEV;

	/*
	 *	Disable UART interrupts, set DTR and RTS high
	 *	and set speed.
	 */
	sio_out(up, TXX9_SIDICR, 0);
	/* initial settings */
	sio_out(up, TXX9_SILCR,
		TXX9_SILCR_UMODE_8BIT | TXX9_SILCR_USBL_1BIT |
		((port->flags & UPF_TXX9_USE_SCLK) ?
		 TXX9_SILCR_SCS_SCLK_BG : TXX9_SILCR_SCS_IMCLK_BG));
	sio_out(up, TXX9_SIFLCR, TXX9_SIFLCR_RTSTL_MAX /* 15 */);

	if (options)
		uart_parse_options(options, &baud, &parity, &bits, &flow);

	return uart_set_options(port, co, baud, parity, bits, flow);
}

static struct uart_driver serial_txx9_reg;
static struct console serial_txx9_console = {
	.name		= TXX9_TTY_NAME,
	.write		= serial_txx9_console_write,
	.device		= uart_console_device,
	.setup		= serial_txx9_console_setup,
	.flags		= CON_PRINTBUFFER,
	.index		= -1,
	.data		= &serial_txx9_reg,
};

static int __init serial_txx9_console_init(void)
{
	register_console(&serial_txx9_console);
	return 0;
}
console_initcall(serial_txx9_console_init);

#define SERIAL_TXX9_CONSOLE	&serial_txx9_console
#else
#define SERIAL_TXX9_CONSOLE	NULL
#endif

static struct uart_driver serial_txx9_reg = {
	.owner			= THIS_MODULE,
	.driver_name		= "serial_txx9",
	.dev_name		= TXX9_TTY_NAME,
	.major			= TXX9_TTY_MAJOR,
	.minor			= TXX9_TTY_MINOR_START,
	.nr			= UART_NR,
	.cons			= SERIAL_TXX9_CONSOLE,
};

int __init early_serial_txx9_setup(struct uart_port *port)
{
	if (port->line >= ARRAY_SIZE(serial_txx9_ports))
		return -ENODEV;

	serial_txx9_ports[port->line].port = *port;
	serial_txx9_ports[port->line].port.ops = &serial_txx9_pops;
	serial_txx9_ports[port->line].port.flags |= UPF_BOOT_AUTOCONF;
	return 0;
}

#ifdef ENABLE_SERIAL_TXX9_PCI
/**
 *	serial_txx9_suspend_port - suspend one serial port
 *	@line:  serial line number
 *
 *	Suspend one serial port.
 */
static void serial_txx9_suspend_port(int line)
{
	uart_suspend_port(&serial_txx9_reg, &serial_txx9_ports[line].port);
}

/**
 *	serial_txx9_resume_port - resume one serial port
 *	@line:  serial line number
 *
 *	Resume one serial port.
 */
static void serial_txx9_resume_port(int line)
{
	uart_resume_port(&serial_txx9_reg, &serial_txx9_ports[line].port);
}

static DEFINE_MUTEX(serial_txx9_mutex);

/**
 *	serial_txx9_register_port - register a serial port
 *	@port: serial port template
 *
 *	Configure the serial port specified by the request.
 *
 *	The port is then probed and if necessary the IRQ is autodetected
 *	If this fails an error is returned.
 *
 *	On success the port is ready to use and the line number is returned.
 */
static int __devinit serial_txx9_register_port(struct uart_port *port)
{
	int i;
	struct uart_txx9_port *uart;
	int ret = -ENOSPC;

	mutex_lock(&serial_txx9_mutex);
	for (i = 0; i < UART_NR; i++) {
		uart = &serial_txx9_ports[i];
		if (!(uart->port.iobase || uart->port.mapbase))
			break;
	}
	if (i < UART_NR) {
		uart->port.iobase = port->iobase;
		uart->port.membase = port->membase;
		uart->port.irq      = port->irq;
		uart->port.uartclk  = port->uartclk;
		uart->port.iotype   = port->iotype;
		uart->port.flags    = port->flags | UPF_BOOT_AUTOCONF;
		uart->port.mapbase  = port->mapbase;
		if (port->dev)
			uart->port.dev = port->dev;
		ret = uart_add_one_port(&serial_txx9_reg, &uart->port);
		if (ret == 0)
			ret = uart->port.line;
	}
	mutex_unlock(&serial_txx9_mutex);
	return ret;
}

/**
 *	serial_txx9_unregister_port - remove a txx9 serial port at runtime
 *	@line: serial line number
 *
 *	Remove one serial port.  This may not be called from interrupt
 *	context.  We hand the port back to the our control.
 */
static void __devexit serial_txx9_unregister_port(int line)
{
	struct uart_txx9_port *uart = &serial_txx9_ports[line];

	mutex_lock(&serial_txx9_mutex);
	uart_remove_one_port(&serial_txx9_reg, &uart->port);
	uart->port.flags = 0;
	uart->port.type = PORT_UNKNOWN;
	uart->port.iobase = 0;
	uart->port.mapbase = 0;
	uart->port.membase = NULL;
	uart->port.dev = NULL;
	mutex_unlock(&serial_txx9_mutex);
}

/*
 * Probe one serial board.  Unfortunately, there is no rhyme nor reason
 * to the arrangement of serial ports on a PCI card.
 */
static int __devinit
pciserial_txx9_init_one(struct pci_dev *dev, const struct pci_device_id *ent)
{
	struct uart_port port;
	int line;
	int rc;

	rc = pci_enable_device(dev);
	if (rc)
		return rc;

	memset(&port, 0, sizeof(port));
	port.ops = &serial_txx9_pops;
	port.flags |= UPF_TXX9_HAVE_CTS_LINE;
	port.uartclk = 66670000;
	port.irq = dev->irq;
	port.iotype = UPIO_PORT;
	port.iobase = pci_resource_start(dev, 1);
	port.dev = &dev->dev;
	line = serial_txx9_register_port(&port);
	if (line < 0) {
		printk(KERN_WARNING "Couldn't register serial port %s: %d\n", pci_name(dev), line);
	}
	pci_set_drvdata(dev, (void *)(long)line);

	return 0;
}

static void __devexit pciserial_txx9_remove_one(struct pci_dev *dev)
{
	int line = (int)(long)pci_get_drvdata(dev);

	pci_set_drvdata(dev, NULL);

	if (line) {
		serial_txx9_unregister_port(line);
		pci_disable_device(dev);
	}
}

static int pciserial_txx9_suspend_one(struct pci_dev *dev, pm_message_t state)
{
	int line = (int)(long)pci_get_drvdata(dev);

	if (line)
		serial_txx9_suspend_port(line);
	pci_save_state(dev);
	pci_set_power_state(dev, pci_choose_state(dev, state));
	return 0;
}

static int pciserial_txx9_resume_one(struct pci_dev *dev)
{
	int line = (int)(long)pci_get_drvdata(dev);

	pci_set_power_state(dev, PCI_D0);
	pci_restore_state(dev);

	if (line) {
		pci_enable_device(dev);
		serial_txx9_resume_port(line);
	}
	return 0;
}

static struct pci_device_id serial_txx9_pci_tbl[] = {
	{	PCI_VENDOR_ID_TOSHIBA_2, PCI_DEVICE_ID_TOSHIBA_TC86C001_MISC,
		PCI_ANY_ID, PCI_ANY_ID,
		0, 0, 0 },
	{ 0, }
};

static struct pci_driver serial_txx9_pci_driver = {
	.name		= "serial_txx9",
	.probe		= pciserial_txx9_init_one,
	.remove		= __devexit_p(pciserial_txx9_remove_one),
	.suspend	= pciserial_txx9_suspend_one,
	.resume		= pciserial_txx9_resume_one,
	.id_table	= serial_txx9_pci_tbl,
};

MODULE_DEVICE_TABLE(pci, serial_txx9_pci_tbl);
#endif /* ENABLE_SERIAL_TXX9_PCI */

static int __init serial_txx9_init(void)
{
	int ret;

 	printk(KERN_INFO "%s version %s\n", serial_name, serial_version);

	ret = uart_register_driver(&serial_txx9_reg);
	if (ret >= 0) {
		serial_txx9_register_ports(&serial_txx9_reg);

#ifdef ENABLE_SERIAL_TXX9_PCI
		ret = pci_register_driver(&serial_txx9_pci_driver);
#endif
	}
	return ret;
}

static void __exit serial_txx9_exit(void)
{
	int i;

#ifdef ENABLE_SERIAL_TXX9_PCI
	pci_unregister_driver(&serial_txx9_pci_driver);
#endif
	for (i = 0; i < UART_NR; i++) {
		struct uart_txx9_port *up = &serial_txx9_ports[i];
		if (up->port.iobase || up->port.mapbase)
			uart_remove_one_port(&serial_txx9_reg, &up->port);
	}

	uart_unregister_driver(&serial_txx9_reg);
}

module_init(serial_txx9_init);
module_exit(serial_txx9_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("TX39/49 serial driver");

MODULE_ALIAS_CHARDEV_MAJOR(TXX9_TTY_MAJOR);
