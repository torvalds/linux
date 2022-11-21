// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2003 Digi International (www.digi.com)
 *	Scott H Kilau <Scott_Kilau at digi dot com>
 *
 *	NOTE TO LINUX KERNEL HACKERS:  DO NOT REFORMAT THIS CODE!
 *
 *	This is shared code between Digi's CVS archive and the
 *	Linux Kernel sources.
 *	Changing the source just for reformatting needlessly breaks
 *	our CVS diff history.
 *
 *	Send any bug fixes/changes to:  Eng.Linux at digi dot com.
 *	Thank you.
 *
 */

#include <linux/delay.h>	/* For udelay */
#include <linux/io.h>		/* For read[bwl]/write[bwl] */
#include <linux/serial.h>	/* For struct async_serial */
#include <linux/serial_reg.h>	/* For the various UART offsets */
#include <linux/pci.h>
#include <linux/tty.h>

#include "jsm.h"	/* Driver main header file */

static struct {
	unsigned int rate;
	unsigned int cflag;
} baud_rates[] = {
	{ 921600, B921600 },
	{ 460800, B460800 },
	{ 230400, B230400 },
	{ 115200, B115200 },
	{  57600, B57600  },
	{  38400, B38400  },
	{  19200, B19200  },
	{   9600, B9600   },
	{   4800, B4800   },
	{   2400, B2400   },
	{   1200, B1200   },
	{    600, B600    },
	{    300, B300    },
	{    200, B200    },
	{    150, B150    },
	{    134, B134    },
	{    110, B110    },
	{     75, B75     },
	{     50, B50     },
};

static void cls_set_cts_flow_control(struct jsm_channel *ch)
{
	u8 lcrb = readb(&ch->ch_cls_uart->lcr);
	u8 ier = readb(&ch->ch_cls_uart->ier);
	u8 isr_fcr = 0;

	/*
	 * The Enhanced Register Set may only be accessed when
	 * the Line Control Register is set to 0xBFh.
	 */
	writeb(UART_EXAR654_ENHANCED_REGISTER_SET, &ch->ch_cls_uart->lcr);

	isr_fcr = readb(&ch->ch_cls_uart->isr_fcr);

	/* Turn on CTS flow control, turn off IXON flow control */
	isr_fcr |= (UART_EXAR654_EFR_ECB | UART_EXAR654_EFR_CTSDSR);
	isr_fcr &= ~(UART_EXAR654_EFR_IXON);

	writeb(isr_fcr, &ch->ch_cls_uart->isr_fcr);

	/* Write old LCR value back out, which turns enhanced access off */
	writeb(lcrb, &ch->ch_cls_uart->lcr);

	/*
	 * Enable interrupts for CTS flow, turn off interrupts for
	 * received XOFF chars
	 */
	ier |= (UART_EXAR654_IER_CTSDSR);
	ier &= ~(UART_EXAR654_IER_XOFF);
	writeb(ier, &ch->ch_cls_uart->ier);

	/* Set the usual FIFO values */
	writeb((UART_FCR_ENABLE_FIFO), &ch->ch_cls_uart->isr_fcr);

	writeb((UART_FCR_ENABLE_FIFO | UART_16654_FCR_RXTRIGGER_56 |
		UART_16654_FCR_TXTRIGGER_16 | UART_FCR_CLEAR_RCVR),
		&ch->ch_cls_uart->isr_fcr);

	ch->ch_t_tlevel = 16;
}

static void cls_set_ixon_flow_control(struct jsm_channel *ch)
{
	u8 lcrb = readb(&ch->ch_cls_uart->lcr);
	u8 ier = readb(&ch->ch_cls_uart->ier);
	u8 isr_fcr = 0;

	/*
	 * The Enhanced Register Set may only be accessed when
	 * the Line Control Register is set to 0xBFh.
	 */
	writeb(UART_EXAR654_ENHANCED_REGISTER_SET, &ch->ch_cls_uart->lcr);

	isr_fcr = readb(&ch->ch_cls_uart->isr_fcr);

	/* Turn on IXON flow control, turn off CTS flow control */
	isr_fcr |= (UART_EXAR654_EFR_ECB | UART_EXAR654_EFR_IXON);
	isr_fcr &= ~(UART_EXAR654_EFR_CTSDSR);

	writeb(isr_fcr, &ch->ch_cls_uart->isr_fcr);

	/* Now set our current start/stop chars while in enhanced mode */
	writeb(ch->ch_startc, &ch->ch_cls_uart->mcr);
	writeb(0, &ch->ch_cls_uart->lsr);
	writeb(ch->ch_stopc, &ch->ch_cls_uart->msr);
	writeb(0, &ch->ch_cls_uart->spr);

	/* Write old LCR value back out, which turns enhanced access off */
	writeb(lcrb, &ch->ch_cls_uart->lcr);

	/*
	 * Disable interrupts for CTS flow, turn on interrupts for
	 * received XOFF chars
	 */
	ier &= ~(UART_EXAR654_IER_CTSDSR);
	ier |= (UART_EXAR654_IER_XOFF);
	writeb(ier, &ch->ch_cls_uart->ier);

	/* Set the usual FIFO values */
	writeb((UART_FCR_ENABLE_FIFO), &ch->ch_cls_uart->isr_fcr);

	writeb((UART_FCR_ENABLE_FIFO | UART_16654_FCR_RXTRIGGER_16 |
		UART_16654_FCR_TXTRIGGER_16 | UART_FCR_CLEAR_RCVR),
		&ch->ch_cls_uart->isr_fcr);
}

static void cls_set_no_output_flow_control(struct jsm_channel *ch)
{
	u8 lcrb = readb(&ch->ch_cls_uart->lcr);
	u8 ier = readb(&ch->ch_cls_uart->ier);
	u8 isr_fcr = 0;

	/*
	 * The Enhanced Register Set may only be accessed when
	 * the Line Control Register is set to 0xBFh.
	 */
	writeb(UART_EXAR654_ENHANCED_REGISTER_SET, &ch->ch_cls_uart->lcr);

	isr_fcr = readb(&ch->ch_cls_uart->isr_fcr);

	/* Turn off IXON flow control, turn off CTS flow control */
	isr_fcr |= (UART_EXAR654_EFR_ECB);
	isr_fcr &= ~(UART_EXAR654_EFR_CTSDSR | UART_EXAR654_EFR_IXON);

	writeb(isr_fcr, &ch->ch_cls_uart->isr_fcr);

	/* Write old LCR value back out, which turns enhanced access off */
	writeb(lcrb, &ch->ch_cls_uart->lcr);

	/*
	 * Disable interrupts for CTS flow, turn off interrupts for
	 * received XOFF chars
	 */
	ier &= ~(UART_EXAR654_IER_CTSDSR);
	ier &= ~(UART_EXAR654_IER_XOFF);
	writeb(ier, &ch->ch_cls_uart->ier);

	/* Set the usual FIFO values */
	writeb((UART_FCR_ENABLE_FIFO), &ch->ch_cls_uart->isr_fcr);

	writeb((UART_FCR_ENABLE_FIFO | UART_16654_FCR_RXTRIGGER_16 |
		UART_16654_FCR_TXTRIGGER_16 | UART_FCR_CLEAR_RCVR),
		&ch->ch_cls_uart->isr_fcr);

	ch->ch_r_watermark = 0;
	ch->ch_t_tlevel = 16;
	ch->ch_r_tlevel = 16;
}

static void cls_set_rts_flow_control(struct jsm_channel *ch)
{
	u8 lcrb = readb(&ch->ch_cls_uart->lcr);
	u8 ier = readb(&ch->ch_cls_uart->ier);
	u8 isr_fcr = 0;

	/*
	 * The Enhanced Register Set may only be accessed when
	 * the Line Control Register is set to 0xBFh.
	 */
	writeb(UART_EXAR654_ENHANCED_REGISTER_SET, &ch->ch_cls_uart->lcr);

	isr_fcr = readb(&ch->ch_cls_uart->isr_fcr);

	/* Turn on RTS flow control, turn off IXOFF flow control */
	isr_fcr |= (UART_EXAR654_EFR_ECB | UART_EXAR654_EFR_RTSDTR);
	isr_fcr &= ~(UART_EXAR654_EFR_IXOFF);

	writeb(isr_fcr, &ch->ch_cls_uart->isr_fcr);

	/* Write old LCR value back out, which turns enhanced access off */
	writeb(lcrb, &ch->ch_cls_uart->lcr);

	/* Enable interrupts for RTS flow */
	ier |= (UART_EXAR654_IER_RTSDTR);
	writeb(ier, &ch->ch_cls_uart->ier);

	/* Set the usual FIFO values */
	writeb((UART_FCR_ENABLE_FIFO), &ch->ch_cls_uart->isr_fcr);

	writeb((UART_FCR_ENABLE_FIFO | UART_16654_FCR_RXTRIGGER_56 |
		UART_16654_FCR_TXTRIGGER_16 | UART_FCR_CLEAR_RCVR),
		&ch->ch_cls_uart->isr_fcr);

	ch->ch_r_watermark = 4;
	ch->ch_r_tlevel = 8;
}

static void cls_set_ixoff_flow_control(struct jsm_channel *ch)
{
	u8 lcrb = readb(&ch->ch_cls_uart->lcr);
	u8 ier = readb(&ch->ch_cls_uart->ier);
	u8 isr_fcr = 0;

	/*
	 * The Enhanced Register Set may only be accessed when
	 * the Line Control Register is set to 0xBFh.
	 */
	writeb(UART_EXAR654_ENHANCED_REGISTER_SET, &ch->ch_cls_uart->lcr);

	isr_fcr = readb(&ch->ch_cls_uart->isr_fcr);

	/* Turn on IXOFF flow control, turn off RTS flow control */
	isr_fcr |= (UART_EXAR654_EFR_ECB | UART_EXAR654_EFR_IXOFF);
	isr_fcr &= ~(UART_EXAR654_EFR_RTSDTR);

	writeb(isr_fcr, &ch->ch_cls_uart->isr_fcr);

	/* Now set our current start/stop chars while in enhanced mode */
	writeb(ch->ch_startc, &ch->ch_cls_uart->mcr);
	writeb(0, &ch->ch_cls_uart->lsr);
	writeb(ch->ch_stopc, &ch->ch_cls_uart->msr);
	writeb(0, &ch->ch_cls_uart->spr);

	/* Write old LCR value back out, which turns enhanced access off */
	writeb(lcrb, &ch->ch_cls_uart->lcr);

	/* Disable interrupts for RTS flow */
	ier &= ~(UART_EXAR654_IER_RTSDTR);
	writeb(ier, &ch->ch_cls_uart->ier);

	/* Set the usual FIFO values */
	writeb((UART_FCR_ENABLE_FIFO), &ch->ch_cls_uart->isr_fcr);

	writeb((UART_FCR_ENABLE_FIFO | UART_16654_FCR_RXTRIGGER_16 |
		UART_16654_FCR_TXTRIGGER_16 | UART_FCR_CLEAR_RCVR),
		&ch->ch_cls_uart->isr_fcr);
}

static void cls_set_no_input_flow_control(struct jsm_channel *ch)
{
	u8 lcrb = readb(&ch->ch_cls_uart->lcr);
	u8 ier = readb(&ch->ch_cls_uart->ier);
	u8 isr_fcr = 0;

	/*
	 * The Enhanced Register Set may only be accessed when
	 * the Line Control Register is set to 0xBFh.
	 */
	writeb(UART_EXAR654_ENHANCED_REGISTER_SET, &ch->ch_cls_uart->lcr);

	isr_fcr = readb(&ch->ch_cls_uart->isr_fcr);

	/* Turn off IXOFF flow control, turn off RTS flow control */
	isr_fcr |= (UART_EXAR654_EFR_ECB);
	isr_fcr &= ~(UART_EXAR654_EFR_RTSDTR | UART_EXAR654_EFR_IXOFF);

	writeb(isr_fcr, &ch->ch_cls_uart->isr_fcr);

	/* Write old LCR value back out, which turns enhanced access off */
	writeb(lcrb, &ch->ch_cls_uart->lcr);

	/* Disable interrupts for RTS flow */
	ier &= ~(UART_EXAR654_IER_RTSDTR);
	writeb(ier, &ch->ch_cls_uart->ier);

	/* Set the usual FIFO values */
	writeb((UART_FCR_ENABLE_FIFO), &ch->ch_cls_uart->isr_fcr);

	writeb((UART_FCR_ENABLE_FIFO | UART_16654_FCR_RXTRIGGER_16 |
		UART_16654_FCR_TXTRIGGER_16 | UART_FCR_CLEAR_RCVR),
		&ch->ch_cls_uart->isr_fcr);

	ch->ch_t_tlevel = 16;
	ch->ch_r_tlevel = 16;
}

/*
 * cls_clear_break.
 * Determines whether its time to shut off break condition.
 *
 * No locks are assumed to be held when calling this function.
 * channel lock is held and released in this function.
 */
static void cls_clear_break(struct jsm_channel *ch)
{
	unsigned long lock_flags;

	spin_lock_irqsave(&ch->ch_lock, lock_flags);

	/* Turn break off, and unset some variables */
	if (ch->ch_flags & CH_BREAK_SENDING) {
		u8 temp = readb(&ch->ch_cls_uart->lcr);

		writeb((temp & ~UART_LCR_SBC), &ch->ch_cls_uart->lcr);

		ch->ch_flags &= ~(CH_BREAK_SENDING);
		jsm_dbg(IOCTL, &ch->ch_bd->pci_dev,
			"clear break Finishing UART_LCR_SBC! finished: %lx\n",
			jiffies);
	}
	spin_unlock_irqrestore(&ch->ch_lock, lock_flags);
}

static void cls_disable_receiver(struct jsm_channel *ch)
{
	u8 tmp = readb(&ch->ch_cls_uart->ier);

	tmp &= ~(UART_IER_RDI);
	writeb(tmp, &ch->ch_cls_uart->ier);
}

static void cls_enable_receiver(struct jsm_channel *ch)
{
	u8 tmp = readb(&ch->ch_cls_uart->ier);

	tmp |= (UART_IER_RDI);
	writeb(tmp, &ch->ch_cls_uart->ier);
}

/* Make the UART raise any of the output signals we want up */
static void cls_assert_modem_signals(struct jsm_channel *ch)
{
	if (!ch)
		return;

	writeb(ch->ch_mostat, &ch->ch_cls_uart->mcr);
}

static void cls_copy_data_from_uart_to_queue(struct jsm_channel *ch)
{
	int qleft = 0;
	u8 linestatus;
	u8 error_mask = 0;
	u16 head;
	u16 tail;
	unsigned long flags;

	if (!ch)
		return;

	spin_lock_irqsave(&ch->ch_lock, flags);

	/* cache head and tail of queue */
	head = ch->ch_r_head & RQUEUEMASK;
	tail = ch->ch_r_tail & RQUEUEMASK;

	ch->ch_cached_lsr = 0;

	/* Store how much space we have left in the queue */
	qleft = tail - head - 1;
	if (qleft < 0)
		qleft += RQUEUEMASK + 1;

	/*
	 * Create a mask to determine whether we should
	 * insert the character (if any) into our queue.
	 */
	if (ch->ch_c_iflag & IGNBRK)
		error_mask |= UART_LSR_BI;

	while (1) {
		/*
		 * Grab the linestatus register, we need to
		 * check to see if there is any data to read
		 */
		linestatus = readb(&ch->ch_cls_uart->lsr);

		/* Break out if there is no data to fetch */
		if (!(linestatus & UART_LSR_DR))
			break;

		/*
		 * Discard character if we are ignoring the error mask
		 * which in this case is the break signal.
		 */
		if (linestatus & error_mask)  {
			linestatus = 0;
			readb(&ch->ch_cls_uart->txrx);
			continue;
		}

		/*
		 * If our queue is full, we have no choice but to drop some
		 * data. The assumption is that HWFLOW or SWFLOW should have
		 * stopped things way way before we got to this point.
		 *
		 * I decided that I wanted to ditch the oldest data first,
		 * I hope thats okay with everyone? Yes? Good.
		 */
		while (qleft < 1) {
			tail = (tail + 1) & RQUEUEMASK;
			ch->ch_r_tail = tail;
			ch->ch_err_overrun++;
			qleft++;
		}

		ch->ch_equeue[head] = linestatus & (UART_LSR_BI | UART_LSR_PE
								 | UART_LSR_FE);
		ch->ch_rqueue[head] = readb(&ch->ch_cls_uart->txrx);

		qleft--;

		if (ch->ch_equeue[head] & UART_LSR_PE)
			ch->ch_err_parity++;
		if (ch->ch_equeue[head] & UART_LSR_BI)
			ch->ch_err_break++;
		if (ch->ch_equeue[head] & UART_LSR_FE)
			ch->ch_err_frame++;

		/* Add to, and flip head if needed */
		head = (head + 1) & RQUEUEMASK;
		ch->ch_rxcount++;
	}

	/*
	 * Write new final heads to channel structure.
	 */
	ch->ch_r_head = head & RQUEUEMASK;
	ch->ch_e_head = head & EQUEUEMASK;

	spin_unlock_irqrestore(&ch->ch_lock, flags);
}

static void cls_copy_data_from_queue_to_uart(struct jsm_channel *ch)
{
	u16 tail;
	int n;
	int qlen;
	u32 len_written = 0;
	struct circ_buf *circ;

	if (!ch)
		return;

	circ = &ch->uart_port.state->xmit;

	/* No data to write to the UART */
	if (uart_circ_empty(circ))
		return;

	/* If port is "stopped", don't send any data to the UART */
	if ((ch->ch_flags & CH_STOP) || (ch->ch_flags & CH_BREAK_SENDING))
		return;

	/* We have to do it this way, because of the EXAR TXFIFO count bug. */
	if (!(ch->ch_flags & (CH_TX_FIFO_EMPTY | CH_TX_FIFO_LWM)))
		return;

	n = 32;

	/* cache tail of queue */
	tail = circ->tail & (UART_XMIT_SIZE - 1);
	qlen = uart_circ_chars_pending(circ);

	/* Find minimum of the FIFO space, versus queue length */
	n = min(n, qlen);

	while (n > 0) {
		writeb(circ->buf[tail], &ch->ch_cls_uart->txrx);
		tail = (tail + 1) & (UART_XMIT_SIZE - 1);
		n--;
		ch->ch_txcount++;
		len_written++;
	}

	/* Update the final tail */
	circ->tail = tail & (UART_XMIT_SIZE - 1);

	if (len_written > ch->ch_t_tlevel)
		ch->ch_flags &= ~(CH_TX_FIFO_EMPTY | CH_TX_FIFO_LWM);

	if (uart_circ_empty(circ))
		uart_write_wakeup(&ch->uart_port);
}

static void cls_parse_modem(struct jsm_channel *ch, u8 signals)
{
	u8 msignals = signals;

	jsm_dbg(MSIGS, &ch->ch_bd->pci_dev,
		"neo_parse_modem: port: %d msignals: %x\n",
		ch->ch_portnum, msignals);

	/*
	 * Scrub off lower bits.
	 * They signify delta's, which I don't care about
	 * Keep DDCD and DDSR though
	 */
	msignals &= 0xf8;

	if (msignals & UART_MSR_DDCD)
		uart_handle_dcd_change(&ch->uart_port, msignals & UART_MSR_DCD);
	if (msignals & UART_MSR_DDSR)
		uart_handle_dcd_change(&ch->uart_port, msignals & UART_MSR_CTS);

	if (msignals & UART_MSR_DCD)
		ch->ch_mistat |= UART_MSR_DCD;
	else
		ch->ch_mistat &= ~UART_MSR_DCD;

	if (msignals & UART_MSR_DSR)
		ch->ch_mistat |= UART_MSR_DSR;
	else
		ch->ch_mistat &= ~UART_MSR_DSR;

	if (msignals & UART_MSR_RI)
		ch->ch_mistat |= UART_MSR_RI;
	else
		ch->ch_mistat &= ~UART_MSR_RI;

	if (msignals & UART_MSR_CTS)
		ch->ch_mistat |= UART_MSR_CTS;
	else
		ch->ch_mistat &= ~UART_MSR_CTS;

	jsm_dbg(MSIGS, &ch->ch_bd->pci_dev,
		"Port: %d DTR: %d RTS: %d CTS: %d DSR: %d " "RI: %d CD: %d\n",
		ch->ch_portnum,
		!!((ch->ch_mistat | ch->ch_mostat) & UART_MCR_DTR),
		!!((ch->ch_mistat | ch->ch_mostat) & UART_MCR_RTS),
		!!((ch->ch_mistat | ch->ch_mostat) & UART_MSR_CTS),
		!!((ch->ch_mistat | ch->ch_mostat) & UART_MSR_DSR),
		!!((ch->ch_mistat | ch->ch_mostat) & UART_MSR_RI),
		!!((ch->ch_mistat | ch->ch_mostat) & UART_MSR_DCD));
}

/* Parse the ISR register for the specific port */
static inline void cls_parse_isr(struct jsm_board *brd, uint port)
{
	struct jsm_channel *ch;
	u8 isr = 0;
	unsigned long flags;

	/*
	 * No need to verify board pointer, it was already
	 * verified in the interrupt routine.
	 */

	if (port >= brd->nasync)
		return;

	ch = brd->channels[port];
	if (!ch)
		return;

	/* Here we try to figure out what caused the interrupt to happen */
	while (1) {
		isr = readb(&ch->ch_cls_uart->isr_fcr);

		/* Bail if no pending interrupt on port */
		if (isr & UART_IIR_NO_INT)
			break;

		/* Receive Interrupt pending */
		if (isr & (UART_IIR_RDI | UART_IIR_RDI_TIMEOUT)) {
			/* Read data from uart -> queue */
			cls_copy_data_from_uart_to_queue(ch);
			jsm_check_queue_flow_control(ch);
		}

		/* Transmit Hold register empty pending */
		if (isr & UART_IIR_THRI) {
			/* Transfer data (if any) from Write Queue -> UART. */
			spin_lock_irqsave(&ch->ch_lock, flags);
			ch->ch_flags |= (CH_TX_FIFO_EMPTY | CH_TX_FIFO_LWM);
			spin_unlock_irqrestore(&ch->ch_lock, flags);
			cls_copy_data_from_queue_to_uart(ch);
		}

		/*
		 * CTS/RTS change of state:
		 * Don't need to do anything, the cls_parse_modem
		 * below will grab the updated modem signals.
		 */

		/* Parse any modem signal changes */
		cls_parse_modem(ch, readb(&ch->ch_cls_uart->msr));
	}
}

/* Channel lock MUST be held before calling this function! */
static void cls_flush_uart_write(struct jsm_channel *ch)
{
	u8 tmp = 0;
	u8 i = 0;

	if (!ch)
		return;

	writeb((UART_FCR_ENABLE_FIFO | UART_FCR_CLEAR_XMIT),
						&ch->ch_cls_uart->isr_fcr);

	for (i = 0; i < 10; i++) {
		/* Check to see if the UART feels it completely flushed FIFO */
		tmp = readb(&ch->ch_cls_uart->isr_fcr);
		if (tmp & UART_FCR_CLEAR_XMIT) {
			jsm_dbg(IOCTL, &ch->ch_bd->pci_dev,
				"Still flushing TX UART... i: %d\n", i);
			udelay(10);
		} else
			break;
	}

	ch->ch_flags |= (CH_TX_FIFO_EMPTY | CH_TX_FIFO_LWM);
}

/* Channel lock MUST be held before calling this function! */
static void cls_flush_uart_read(struct jsm_channel *ch)
{
	if (!ch)
		return;

	/*
	 * For complete POSIX compatibility, we should be purging the
	 * read FIFO in the UART here.
	 *
	 * However, clearing the read FIFO (UART_FCR_CLEAR_RCVR) also
	 * incorrectly flushes write data as well as just basically trashing the
	 * FIFO.
	 *
	 * Presumably, this is a bug in this UART.
	 */

	udelay(10);
}

static void cls_send_start_character(struct jsm_channel *ch)
{
	if (!ch)
		return;

	if (ch->ch_startc != __DISABLED_CHAR) {
		ch->ch_xon_sends++;
		writeb(ch->ch_startc, &ch->ch_cls_uart->txrx);
	}
}

static void cls_send_stop_character(struct jsm_channel *ch)
{
	if (!ch)
		return;

	if (ch->ch_stopc != __DISABLED_CHAR) {
		ch->ch_xoff_sends++;
		writeb(ch->ch_stopc, &ch->ch_cls_uart->txrx);
	}
}

/*
 * cls_param()
 * Send any/all changes to the line to the UART.
 */
static void cls_param(struct jsm_channel *ch)
{
	u8 lcr = 0;
	u8 uart_lcr = 0;
	u8 ier = 0;
	u32 baud = 9600;
	int quot = 0;
	struct jsm_board *bd;
	int i;
	unsigned int cflag;

	bd = ch->ch_bd;
	if (!bd)
		return;

	/*
	 * If baud rate is zero, flush queues, and set mval to drop DTR.
	 */
	if ((ch->ch_c_cflag & (CBAUD)) == 0) {
		ch->ch_r_head = 0;
		ch->ch_r_tail = 0;
		ch->ch_e_head = 0;
		ch->ch_e_tail = 0;

		cls_flush_uart_write(ch);
		cls_flush_uart_read(ch);

		/* The baudrate is B0 so all modem lines are to be dropped. */
		ch->ch_flags |= (CH_BAUD0);
		ch->ch_mostat &= ~(UART_MCR_RTS | UART_MCR_DTR);
		cls_assert_modem_signals(ch);
		return;
	}

	cflag = C_BAUD(ch->uart_port.state->port.tty);
	baud = 9600;
	for (i = 0; i < ARRAY_SIZE(baud_rates); i++) {
		if (baud_rates[i].cflag == cflag) {
			baud = baud_rates[i].rate;
			break;
		}
	}

	if (ch->ch_flags & CH_BAUD0)
		ch->ch_flags &= ~(CH_BAUD0);

	if (ch->ch_c_cflag & PARENB)
		lcr |= UART_LCR_PARITY;

	if (!(ch->ch_c_cflag & PARODD))
		lcr |= UART_LCR_EPAR;

	/*
	 * Not all platforms support mark/space parity,
	 * so this will hide behind an ifdef.
	 */
#ifdef CMSPAR
	if (ch->ch_c_cflag & CMSPAR)
		lcr |= UART_LCR_SPAR;
#endif

	if (ch->ch_c_cflag & CSTOPB)
		lcr |= UART_LCR_STOP;

	lcr |= UART_LCR_WLEN(tty_get_char_size(ch->ch_c_cflag));

	ier = readb(&ch->ch_cls_uart->ier);
	uart_lcr = readb(&ch->ch_cls_uart->lcr);

	quot = ch->ch_bd->bd_dividend / baud;

	if (quot != 0) {
		writeb(UART_LCR_DLAB, &ch->ch_cls_uart->lcr);
		writeb((quot & 0xff), &ch->ch_cls_uart->txrx);
		writeb((quot >> 8), &ch->ch_cls_uart->ier);
		writeb(lcr, &ch->ch_cls_uart->lcr);
	}

	if (uart_lcr != lcr)
		writeb(lcr, &ch->ch_cls_uart->lcr);

	if (ch->ch_c_cflag & CREAD)
		ier |= (UART_IER_RDI | UART_IER_RLSI);

	ier |= (UART_IER_THRI | UART_IER_MSI);

	writeb(ier, &ch->ch_cls_uart->ier);

	if (ch->ch_c_cflag & CRTSCTS)
		cls_set_cts_flow_control(ch);
	else if (ch->ch_c_iflag & IXON) {
		/*
		 * If start/stop is set to disable,
		 * then we should disable flow control.
		 */
		if ((ch->ch_startc == __DISABLED_CHAR) ||
			(ch->ch_stopc == __DISABLED_CHAR))
			cls_set_no_output_flow_control(ch);
		else
			cls_set_ixon_flow_control(ch);
	} else
		cls_set_no_output_flow_control(ch);

	if (ch->ch_c_cflag & CRTSCTS)
		cls_set_rts_flow_control(ch);
	else if (ch->ch_c_iflag & IXOFF) {
		/*
		 * If start/stop is set to disable,
		 * then we should disable flow control.
		 */
		if ((ch->ch_startc == __DISABLED_CHAR) ||
			(ch->ch_stopc == __DISABLED_CHAR))
			cls_set_no_input_flow_control(ch);
		else
			cls_set_ixoff_flow_control(ch);
	} else
		cls_set_no_input_flow_control(ch);

	cls_assert_modem_signals(ch);

	/* get current status of the modem signals now */
	cls_parse_modem(ch, readb(&ch->ch_cls_uart->msr));
}

/*
 * cls_intr()
 *
 * Classic specific interrupt handler.
 */
static irqreturn_t cls_intr(int irq, void *voidbrd)
{
	struct jsm_board *brd = voidbrd;
	unsigned long lock_flags;
	unsigned char uart_poll;
	uint i = 0;

	/* Lock out the slow poller from running on this board. */
	spin_lock_irqsave(&brd->bd_intr_lock, lock_flags);

	/*
	 * Check the board's global interrupt offset to see if we
	 * acctually do have an interrupt pending on us.
	 */
	uart_poll = readb(brd->re_map_membase + UART_CLASSIC_POLL_ADDR_OFFSET);

	jsm_dbg(INTR, &brd->pci_dev, "%s:%d uart_poll: %x\n",
		__FILE__, __LINE__, uart_poll);

	if (!uart_poll) {
		jsm_dbg(INTR, &brd->pci_dev,
			"Kernel interrupted to me, but no pending interrupts...\n");
		spin_unlock_irqrestore(&brd->bd_intr_lock, lock_flags);
		return IRQ_NONE;
	}

	/* At this point, we have at least SOMETHING to service, dig further. */

	/* Parse each port to find out what caused the interrupt */
	for (i = 0; i < brd->nasync; i++)
		cls_parse_isr(brd, i);

	spin_unlock_irqrestore(&brd->bd_intr_lock, lock_flags);

	return IRQ_HANDLED;
}

/* Inits UART */
static void cls_uart_init(struct jsm_channel *ch)
{
	unsigned char lcrb = readb(&ch->ch_cls_uart->lcr);
	unsigned char isr_fcr = 0;

	writeb(0, &ch->ch_cls_uart->ier);

	/*
	 * The Enhanced Register Set may only be accessed when
	 * the Line Control Register is set to 0xBFh.
	 */
	writeb(UART_EXAR654_ENHANCED_REGISTER_SET, &ch->ch_cls_uart->lcr);

	isr_fcr = readb(&ch->ch_cls_uart->isr_fcr);

	/* Turn on Enhanced/Extended controls */
	isr_fcr |= (UART_EXAR654_EFR_ECB);

	writeb(isr_fcr, &ch->ch_cls_uart->isr_fcr);

	/* Write old LCR value back out, which turns enhanced access off */
	writeb(lcrb, &ch->ch_cls_uart->lcr);

	/* Clear out UART and FIFO */
	readb(&ch->ch_cls_uart->txrx);

	writeb((UART_FCR_ENABLE_FIFO|UART_FCR_CLEAR_RCVR|UART_FCR_CLEAR_XMIT),
						 &ch->ch_cls_uart->isr_fcr);
	udelay(10);

	ch->ch_flags |= (CH_FIFO_ENABLED | CH_TX_FIFO_EMPTY | CH_TX_FIFO_LWM);

	readb(&ch->ch_cls_uart->lsr);
	readb(&ch->ch_cls_uart->msr);
}

/*
 * Turns off UART.
 */
static void cls_uart_off(struct jsm_channel *ch)
{
	/* Stop all interrupts from accurring. */
	writeb(0, &ch->ch_cls_uart->ier);
}

/*
 * cls_get_uarts_bytes_left.
 * Returns 0 is nothing left in the FIFO, returns 1 otherwise.
 *
 * The channel lock MUST be held by the calling function.
 */
static u32 cls_get_uart_bytes_left(struct jsm_channel *ch)
{
	u8 left = 0;
	u8 lsr = readb(&ch->ch_cls_uart->lsr);

	/* Determine whether the Transmitter is empty or not */
	if (!(lsr & UART_LSR_TEMT))
		left = 1;
	else {
		ch->ch_flags |= (CH_TX_FIFO_EMPTY | CH_TX_FIFO_LWM);
		left = 0;
	}

	return left;
}

/*
 * cls_send_break.
 * Starts sending a break thru the UART.
 *
 * The channel lock MUST be held by the calling function.
 */
static void cls_send_break(struct jsm_channel *ch)
{
	/* Tell the UART to start sending the break */
	if (!(ch->ch_flags & CH_BREAK_SENDING)) {
		u8 temp = readb(&ch->ch_cls_uart->lcr);

		writeb((temp | UART_LCR_SBC), &ch->ch_cls_uart->lcr);
		ch->ch_flags |= (CH_BREAK_SENDING);
	}
}

/*
 * cls_send_immediate_char.
 * Sends a specific character as soon as possible to the UART,
 * jumping over any bytes that might be in the write queue.
 *
 * The channel lock MUST be held by the calling function.
 */
static void cls_send_immediate_char(struct jsm_channel *ch, unsigned char c)
{
	writeb(c, &ch->ch_cls_uart->txrx);
}

struct board_ops jsm_cls_ops = {
	.intr =				cls_intr,
	.uart_init =			cls_uart_init,
	.uart_off =			cls_uart_off,
	.param =			cls_param,
	.assert_modem_signals =		cls_assert_modem_signals,
	.flush_uart_write =		cls_flush_uart_write,
	.flush_uart_read =		cls_flush_uart_read,
	.disable_receiver =		cls_disable_receiver,
	.enable_receiver =		cls_enable_receiver,
	.send_break =			cls_send_break,
	.clear_break =			cls_clear_break,
	.send_start_character =		cls_send_start_character,
	.send_stop_character =		cls_send_stop_character,
	.copy_data_from_queue_to_uart = cls_copy_data_from_queue_to_uart,
	.get_uart_bytes_left =		cls_get_uart_bytes_left,
	.send_immediate_char =		cls_send_immediate_char
};

