/*
 * Copyright 2003 Digi International (www.digi.com)
 *	Scott H Kilau <Scott_Kilau at digi dot com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY, EXPRESS OR IMPLIED; without even the
 * implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 * PURPOSE.  See the GNU General Public License for more details.
 */

#include <linux/kernel.h>
#include <linux/sched.h>	/* For jiffies, task states */
#include <linux/interrupt.h>	/* For tasklet and interrupt structs/defines */
#include <linux/delay.h>	/* For udelay */
#include <linux/io.h>		/* For read[bwl]/write[bwl] */
#include <linux/serial.h>	/* For struct async_serial */
#include <linux/serial_reg.h>	/* For the various UART offsets */
#include <linux/pci.h>

#include "dgnc_driver.h"	/* Driver main header file */
#include "dgnc_cls.h"
#include "dgnc_tty.h"

static inline void cls_set_cts_flow_control(struct channel_t *ch)
{
	unsigned char lcrb = readb(&ch->ch_cls_uart->lcr);
	unsigned char ier = readb(&ch->ch_cls_uart->ier);
	unsigned char isr_fcr = 0;

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

static inline void cls_set_ixon_flow_control(struct channel_t *ch)
{
	unsigned char lcrb = readb(&ch->ch_cls_uart->lcr);
	unsigned char ier = readb(&ch->ch_cls_uart->ier);
	unsigned char isr_fcr = 0;

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

static inline void cls_set_no_output_flow_control(struct channel_t *ch)
{
	unsigned char lcrb = readb(&ch->ch_cls_uart->lcr);
	unsigned char ier = readb(&ch->ch_cls_uart->ier);
	unsigned char isr_fcr = 0;

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

static inline void cls_set_rts_flow_control(struct channel_t *ch)
{
	unsigned char lcrb = readb(&ch->ch_cls_uart->lcr);
	unsigned char ier = readb(&ch->ch_cls_uart->ier);
	unsigned char isr_fcr = 0;

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

static inline void cls_set_ixoff_flow_control(struct channel_t *ch)
{
	unsigned char lcrb = readb(&ch->ch_cls_uart->lcr);
	unsigned char ier = readb(&ch->ch_cls_uart->ier);
	unsigned char isr_fcr = 0;

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

static inline void cls_set_no_input_flow_control(struct channel_t *ch)
{
	unsigned char lcrb = readb(&ch->ch_cls_uart->lcr);
	unsigned char ier = readb(&ch->ch_cls_uart->ier);
	unsigned char isr_fcr = 0;

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
static inline void cls_clear_break(struct channel_t *ch, int force)
{
	unsigned long flags;

	if (!ch || ch->magic != DGNC_CHANNEL_MAGIC)
		return;

	spin_lock_irqsave(&ch->ch_lock, flags);

	/* Bail if we aren't currently sending a break. */
	if (!ch->ch_stop_sending_break) {
		spin_unlock_irqrestore(&ch->ch_lock, flags);
		return;
	}

	/* Turn break off, and unset some variables */
	if (ch->ch_flags & CH_BREAK_SENDING) {
		if (time_after(jiffies, ch->ch_stop_sending_break) || force) {
			unsigned char temp = readb(&ch->ch_cls_uart->lcr);

			writeb((temp & ~UART_LCR_SBC), &ch->ch_cls_uart->lcr);
			ch->ch_flags &= ~(CH_BREAK_SENDING);
			ch->ch_stop_sending_break = 0;
		}
	}
	spin_unlock_irqrestore(&ch->ch_lock, flags);
}

static void cls_copy_data_from_uart_to_queue(struct channel_t *ch)
{
	int qleft = 0;
	unsigned char linestatus = 0;
	unsigned char error_mask = 0;
	ushort head;
	ushort tail;
	unsigned long flags;

	if (!ch || ch->magic != DGNC_CHANNEL_MAGIC)
		return;

	spin_lock_irqsave(&ch->ch_lock, flags);

	/* cache head and tail of queue */
	head = ch->ch_r_head;
	tail = ch->ch_r_tail;

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
		linestatus = readb(&ch->ch_cls_uart->lsr);

		if (!(linestatus & (UART_LSR_DR)))
			break;

		/*
		 * Discard character if we are ignoring the error mask.
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

	/* Write new final heads to channel structure. */

	ch->ch_r_head = head & RQUEUEMASK;
	ch->ch_e_head = head & EQUEUEMASK;

	spin_unlock_irqrestore(&ch->ch_lock, flags);
}

/* Make the UART raise any of the output signals we want up */
static void cls_assert_modem_signals(struct channel_t *ch)
{
	unsigned char out;

	if (!ch || ch->magic != DGNC_CHANNEL_MAGIC)
		return;

	out = ch->ch_mostat;

	if (ch->ch_flags & CH_LOOPBACK)
		out |= UART_MCR_LOOP;

	writeb(out, &ch->ch_cls_uart->mcr);

	/* Give time for the UART to actually drop the signals */
	udelay(10);
}

static void cls_copy_data_from_queue_to_uart(struct channel_t *ch)
{
	ushort head;
	ushort tail;
	int n;
	int qlen;
	uint len_written = 0;
	unsigned long flags;

	if (!ch || ch->magic != DGNC_CHANNEL_MAGIC)
		return;

	spin_lock_irqsave(&ch->ch_lock, flags);

	/* No data to write to the UART */
	if (ch->ch_w_tail == ch->ch_w_head)
		goto exit_unlock;

	/* If port is "stopped", don't send any data to the UART */
	if ((ch->ch_flags & CH_FORCED_STOP) ||
	    (ch->ch_flags & CH_BREAK_SENDING))
		goto exit_unlock;

	if (!(ch->ch_flags & (CH_TX_FIFO_EMPTY | CH_TX_FIFO_LWM)))
		goto exit_unlock;

	n = 32;

	/* cache head and tail of queue */
	head = ch->ch_w_head & WQUEUEMASK;
	tail = ch->ch_w_tail & WQUEUEMASK;
	qlen = (head - tail) & WQUEUEMASK;

	/* Find minimum of the FIFO space, versus queue length */
	n = min(n, qlen);

	while (n > 0) {
		/*
		 * If RTS Toggle mode is on, turn on RTS now if not already set,
		 * and make sure we get an event when the data transfer has
		 * completed.
		 */
		if (ch->ch_digi.digi_flags & DIGI_RTS_TOGGLE) {
			if (!(ch->ch_mostat & UART_MCR_RTS)) {
				ch->ch_mostat |= (UART_MCR_RTS);
				cls_assert_modem_signals(ch);
			}
			ch->ch_tun.un_flags |= (UN_EMPTY);
		}

		/*
		 * If DTR Toggle mode is on, turn on DTR now if not already set,
		 * and make sure we get an event when the data transfer has
		 * completed.
		 */
		if (ch->ch_digi.digi_flags & DIGI_DTR_TOGGLE) {
			if (!(ch->ch_mostat & UART_MCR_DTR)) {
				ch->ch_mostat |= (UART_MCR_DTR);
				cls_assert_modem_signals(ch);
			}
			ch->ch_tun.un_flags |= (UN_EMPTY);
		}
		writeb(ch->ch_wqueue[ch->ch_w_tail], &ch->ch_cls_uart->txrx);
		ch->ch_w_tail++;
		ch->ch_w_tail &= WQUEUEMASK;
		ch->ch_txcount++;
		len_written++;
		n--;
	}

	if (len_written > 0)
		ch->ch_flags &= ~(CH_TX_FIFO_EMPTY | CH_TX_FIFO_LWM);

exit_unlock:
	spin_unlock_irqrestore(&ch->ch_lock, flags);
}

static void cls_parse_modem(struct channel_t *ch, unsigned char signals)
{
	unsigned char msignals = signals;
	unsigned long flags;

	if (!ch || ch->magic != DGNC_CHANNEL_MAGIC)
		return;

	/*
	 * Do altpin switching. Altpin switches DCD and DSR.
	 * This prolly breaks DSRPACE, so we should be more clever here.
	 */
	spin_lock_irqsave(&ch->ch_lock, flags);
	if (ch->ch_digi.digi_flags & DIGI_ALTPIN) {
		unsigned char mswap = signals;

		if (mswap & UART_MSR_DDCD) {
			msignals &= ~UART_MSR_DDCD;
			msignals |= UART_MSR_DDSR;
		}
		if (mswap & UART_MSR_DDSR) {
			msignals &= ~UART_MSR_DDSR;
			msignals |= UART_MSR_DDCD;
		}
		if (mswap & UART_MSR_DCD) {
			msignals &= ~UART_MSR_DCD;
			msignals |= UART_MSR_DSR;
		}
		if (mswap & UART_MSR_DSR) {
			msignals &= ~UART_MSR_DSR;
			msignals |= UART_MSR_DCD;
		}
	}
	spin_unlock_irqrestore(&ch->ch_lock, flags);

	/*
	 * Scrub off lower bits. They signify delta's, which I don't
	 * care about
	 */
	signals &= 0xf0;

	spin_lock_irqsave(&ch->ch_lock, flags);
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
	spin_unlock_irqrestore(&ch->ch_lock, flags);
}

/* Parse the ISR register for the specific port */
static inline void cls_parse_isr(struct dgnc_board *brd, uint port)
{
	struct channel_t *ch;
	unsigned char isr = 0;
	unsigned long flags;

	/*
	 * No need to verify board pointer, it was already
	 * verified in the interrupt routine.
	 */

	if (port >= brd->nasync)
		return;

	ch = brd->channels[port];
	if (ch->magic != DGNC_CHANNEL_MAGIC)
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
			dgnc_check_queue_flow_control(ch);
		}

		/* Transmit Hold register empty pending */
		if (isr & UART_IIR_THRI) {
			/* Transfer data (if any) from Write Queue -> UART. */
			spin_lock_irqsave(&ch->ch_lock, flags);
			ch->ch_flags |= (CH_TX_FIFO_EMPTY | CH_TX_FIFO_LWM);
			spin_unlock_irqrestore(&ch->ch_lock, flags);
			cls_copy_data_from_queue_to_uart(ch);
		}

		/* Parse any modem signal changes */
		cls_parse_modem(ch, readb(&ch->ch_cls_uart->msr));
	}
}

/* Channel lock MUST be held before calling this function! */
static void cls_flush_uart_write(struct channel_t *ch)
{
	if (!ch || ch->magic != DGNC_CHANNEL_MAGIC)
		return;

	writeb((UART_FCR_ENABLE_FIFO | UART_FCR_CLEAR_XMIT),
	       &ch->ch_cls_uart->isr_fcr);
	usleep_range(10, 20);

	ch->ch_flags |= (CH_TX_FIFO_EMPTY | CH_TX_FIFO_LWM);
}

/* Channel lock MUST be held before calling this function! */
static void cls_flush_uart_read(struct channel_t *ch)
{
	if (!ch || ch->magic != DGNC_CHANNEL_MAGIC)
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

/*
 * cls_param()
 * Send any/all changes to the line to the UART.
 */
static void cls_param(struct tty_struct *tty)
{
	unsigned char lcr = 0;
	unsigned char uart_lcr = 0;
	unsigned char ier = 0;
	unsigned char uart_ier = 0;
	uint baud = 9600;
	int quot = 0;
	struct dgnc_board *bd;
	struct channel_t *ch;
	struct un_t   *un;

	if (!tty || tty->magic != TTY_MAGIC)
		return;

	un = (struct un_t *)tty->driver_data;
	if (!un || un->magic != DGNC_UNIT_MAGIC)
		return;

	ch = un->un_ch;
	if (!ch || ch->magic != DGNC_CHANNEL_MAGIC)
		return;

	bd = ch->ch_bd;
	if (!bd || bd->magic != DGNC_BOARD_MAGIC)
		return;

	/* If baud rate is zero, flush queues, and set mval to drop DTR. */

	if ((ch->ch_c_cflag & (CBAUD)) == 0) {
		ch->ch_r_head = 0;
		ch->ch_r_tail = 0;
		ch->ch_e_head = 0;
		ch->ch_e_tail = 0;
		ch->ch_w_head = 0;
		ch->ch_w_tail = 0;

		cls_flush_uart_write(ch);
		cls_flush_uart_read(ch);

		/* The baudrate is B0 so all modem lines are to be dropped. */
		ch->ch_flags |= (CH_BAUD0);
		ch->ch_mostat &= ~(UART_MCR_RTS | UART_MCR_DTR);
		cls_assert_modem_signals(ch);
		ch->ch_old_baud = 0;
		return;
	} else if (ch->ch_custom_speed) {
		baud = ch->ch_custom_speed;
		/* Handle transition from B0 */
		if (ch->ch_flags & CH_BAUD0) {
			ch->ch_flags &= ~(CH_BAUD0);

			/*
			 * Bring back up RTS and DTR...
			 * Also handle RTS or DTR toggle if set.
			 */
			if (!(ch->ch_digi.digi_flags & DIGI_RTS_TOGGLE))
				ch->ch_mostat |= (UART_MCR_RTS);
			if (!(ch->ch_digi.digi_flags & DIGI_DTR_TOGGLE))
				ch->ch_mostat |= (UART_MCR_DTR);
		}

	} else {
		int iindex = 0;
		int jindex = 0;

		ulong bauds[4][16] = {
			{ /* slowbaud */
				0,      50,     75,     110,
				134,    150,    200,    300,
				600,    1200,   1800,   2400,
				4800,   9600,   19200,  38400 },
			{ /* slowbaud & CBAUDEX */
				0,      57600,  115200, 230400,
				460800, 150,    200,    921600,
				600,    1200,   1800,   2400,
				4800,   9600,   19200,  38400 },
			{ /* fastbaud */
				0,      57600,   76800, 115200,
				131657, 153600, 230400, 460800,
				921600, 1200,   1800,   2400,
				4800,   9600,   19200,  38400 },
			{ /* fastbaud & CBAUDEX */
				0,      57600,  115200, 230400,
				460800, 150,    200,    921600,
				600,    1200,   1800,   2400,
				4800,   9600,   19200,  38400 }
		};

		/*
		 * Only use the TXPrint baud rate if the terminal
		 * unit is NOT open
		 */
		if (!(ch->ch_tun.un_flags & UN_ISOPEN) &&
		    (un->un_type == DGNC_PRINT))
			baud = C_BAUD(ch->ch_pun.un_tty) & 0xff;
		else
			baud = C_BAUD(ch->ch_tun.un_tty) & 0xff;

		if (ch->ch_c_cflag & CBAUDEX)
			iindex = 1;

		if (ch->ch_digi.digi_flags & DIGI_FAST)
			iindex += 2;

		jindex = baud;

		if ((iindex >= 0) && (iindex < 4) && (jindex >= 0) &&
		    (jindex < 16)) {
			baud = bauds[iindex][jindex];
		} else {
			baud = 0;
		}

		if (baud == 0)
			baud = 9600;

		/* Handle transition from B0 */
		if (ch->ch_flags & CH_BAUD0) {
			ch->ch_flags &= ~(CH_BAUD0);

			/*
			 * Bring back up RTS and DTR...
			 * Also handle RTS or DTR toggle if set.
			 */
			if (!(ch->ch_digi.digi_flags & DIGI_RTS_TOGGLE))
				ch->ch_mostat |= (UART_MCR_RTS);
			if (!(ch->ch_digi.digi_flags & DIGI_DTR_TOGGLE))
				ch->ch_mostat |= (UART_MCR_DTR);
		}
	}

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

	switch (ch->ch_c_cflag & CSIZE) {
	case CS5:
		lcr |= UART_LCR_WLEN5;
		break;
	case CS6:
		lcr |= UART_LCR_WLEN6;
		break;
	case CS7:
		lcr |= UART_LCR_WLEN7;
		break;
	case CS8:
	default:
		lcr |= UART_LCR_WLEN8;
		break;
	}

	uart_ier = readb(&ch->ch_cls_uart->ier);
	ier =  uart_ier;
	uart_lcr = readb(&ch->ch_cls_uart->lcr);

	if (baud == 0)
		baud = 9600;

	quot = ch->ch_bd->bd_dividend / baud;

	if (quot != 0 && ch->ch_old_baud != baud) {
		ch->ch_old_baud = baud;
		writeb(UART_LCR_DLAB, &ch->ch_cls_uart->lcr);
		writeb((quot & 0xff), &ch->ch_cls_uart->txrx);
		writeb((quot >> 8), &ch->ch_cls_uart->ier);
		writeb(lcr, &ch->ch_cls_uart->lcr);
	}

	if (uart_lcr != lcr)
		writeb(lcr, &ch->ch_cls_uart->lcr);

	if (ch->ch_c_cflag & CREAD)
		ier |= (UART_IER_RDI | UART_IER_RLSI);
	else
		ier &= ~(UART_IER_RDI | UART_IER_RLSI);

	/*
	 * Have the UART interrupt on modem signal changes ONLY when
	 * we are in hardware flow control mode, or CLOCAL/FORCEDCD is not set.
	 */
	if ((ch->ch_digi.digi_flags & CTSPACE) ||
	    (ch->ch_digi.digi_flags & RTSPACE) ||
	    (ch->ch_c_cflag & CRTSCTS) ||
	    !(ch->ch_digi.digi_flags & DIGI_FORCEDCD) ||
	    !(ch->ch_c_cflag & CLOCAL))
		ier |= UART_IER_MSI;
	else
		ier &= ~UART_IER_MSI;

	ier |= UART_IER_THRI;

	if (ier != uart_ier)
		writeb(ier, &ch->ch_cls_uart->ier);

	if (ch->ch_digi.digi_flags & CTSPACE || ch->ch_c_cflag & CRTSCTS) {
		cls_set_cts_flow_control(ch);
	} else if (ch->ch_c_iflag & IXON) {
		/*
		 * If start/stop is set to disable, then we should
		 * disable flow control
		 */
		if ((ch->ch_startc == _POSIX_VDISABLE) ||
		    (ch->ch_stopc == _POSIX_VDISABLE))
			cls_set_no_output_flow_control(ch);
		else
			cls_set_ixon_flow_control(ch);
	} else {
		cls_set_no_output_flow_control(ch);
	}

	if (ch->ch_digi.digi_flags & RTSPACE || ch->ch_c_cflag & CRTSCTS) {
		cls_set_rts_flow_control(ch);
	} else if (ch->ch_c_iflag & IXOFF) {
		/*
		 * If start/stop is set to disable, then we should disable
		 * flow control
		 */
		if ((ch->ch_startc == _POSIX_VDISABLE) ||
		    (ch->ch_stopc == _POSIX_VDISABLE))
			cls_set_no_input_flow_control(ch);
		else
			cls_set_ixoff_flow_control(ch);
	} else {
		cls_set_no_input_flow_control(ch);
	}

	cls_assert_modem_signals(ch);

	/* Get current status of the modem signals now */
	cls_parse_modem(ch, readb(&ch->ch_cls_uart->msr));
}

/* Our board poller function. */

static void cls_tasklet(unsigned long data)
{
	struct dgnc_board *bd = (struct dgnc_board *)data;
	struct channel_t *ch;
	unsigned long flags;
	int i;
	int state = 0;
	int ports = 0;

	if (!bd || bd->magic != DGNC_BOARD_MAGIC)
		return;

	/* Cache a couple board values */
	spin_lock_irqsave(&bd->bd_lock, flags);
	state = bd->state;
	ports = bd->nasync;
	spin_unlock_irqrestore(&bd->bd_lock, flags);

	/*
	 * Do NOT allow the interrupt routine to read the intr registers
	 * Until we release this lock.
	 */
	spin_lock_irqsave(&bd->bd_intr_lock, flags);

	/* If board is ready, parse deeper to see if there is anything to do. */

	if ((state == BOARD_READY) && (ports > 0)) {
		/* Loop on each port */
		for (i = 0; i < ports; i++) {
			ch = bd->channels[i];

			/*
			 * NOTE: Remember you CANNOT hold any channel
			 * locks when calling input.
			 * During input processing, its possible we
			 * will call ld, which might do callbacks back
			 * into us.
			 */
			dgnc_input(ch);

			/*
			 * Channel lock is grabbed and then released
			 * inside this routine.
			 */
			cls_copy_data_from_queue_to_uart(ch);
			dgnc_wakeup_writes(ch);

			/* Check carrier function. */

			dgnc_carrier(ch);

			/*
			 * The timing check of turning off the break is done
			 * inside clear_break()
			 */
			if (ch->ch_stop_sending_break)
				cls_clear_break(ch, 0);
		}
	}

	spin_unlock_irqrestore(&bd->bd_intr_lock, flags);
}

/*
 * cls_intr()
 *
 * Classic specific interrupt handler.
 */
static irqreturn_t cls_intr(int irq, void *voidbrd)
{
	struct dgnc_board *brd = voidbrd;
	uint i = 0;
	unsigned char poll_reg;
	unsigned long flags;

	/*
	 * Check to make sure it didn't receive interrupt with a null board
	 * associated or a board pointer that wasn't ours.
	 */
	if (!brd || brd->magic != DGNC_BOARD_MAGIC)
		return IRQ_NONE;

	spin_lock_irqsave(&brd->bd_intr_lock, flags);

	/*
	 * Check the board's global interrupt offset to see if we
	 * we actually do have an interrupt pending for us.
	 */
	poll_reg = readb(brd->re_map_membase + UART_CLASSIC_POLL_ADDR_OFFSET);

	/* If 0, no interrupts pending */
	if (!poll_reg) {
		spin_unlock_irqrestore(&brd->bd_intr_lock, flags);
		return IRQ_NONE;
	}

	/* Parse each port to find out what caused the interrupt */
	for (i = 0; i < brd->nasync; i++)
		cls_parse_isr(brd, i);

	/* Schedule tasklet to more in-depth servicing at a better time. */

	tasklet_schedule(&brd->helper_tasklet);

	spin_unlock_irqrestore(&brd->bd_intr_lock, flags);

	return IRQ_HANDLED;
}

static void cls_disable_receiver(struct channel_t *ch)
{
	unsigned char tmp = readb(&ch->ch_cls_uart->ier);

	tmp &= ~(UART_IER_RDI);
	writeb(tmp, &ch->ch_cls_uart->ier);
}

static void cls_enable_receiver(struct channel_t *ch)
{
	unsigned char tmp = readb(&ch->ch_cls_uart->ier);

	tmp |= (UART_IER_RDI);
	writeb(tmp, &ch->ch_cls_uart->ier);
}

/*
 * This function basically goes to sleep for secs, or until
 * it gets signalled that the port has fully drained.
 */
static int cls_drain(struct tty_struct *tty, uint seconds)
{
	unsigned long flags;
	struct channel_t *ch;
	struct un_t *un;

	if (!tty || tty->magic != TTY_MAGIC)
		return -ENXIO;

	un = (struct un_t *)tty->driver_data;
	if (!un || un->magic != DGNC_UNIT_MAGIC)
		return -ENXIO;

	ch = un->un_ch;
	if (!ch || ch->magic != DGNC_CHANNEL_MAGIC)
		return -ENXIO;

	spin_lock_irqsave(&ch->ch_lock, flags);
	un->un_flags |= UN_EMPTY;
	spin_unlock_irqrestore(&ch->ch_lock, flags);

	/* NOTE: Do something with time passed in. */

	/* If ret is non-zero, user ctrl-c'ed us */

	return wait_event_interruptible(un->un_flags_wait,
					 ((un->un_flags & UN_EMPTY) == 0));
}

static void cls_send_start_character(struct channel_t *ch)
{
	if (!ch || ch->magic != DGNC_CHANNEL_MAGIC)
		return;

	if (ch->ch_startc != _POSIX_VDISABLE) {
		ch->ch_xon_sends++;
		writeb(ch->ch_startc, &ch->ch_cls_uart->txrx);
	}
}

static void cls_send_stop_character(struct channel_t *ch)
{
	if (!ch || ch->magic != DGNC_CHANNEL_MAGIC)
		return;

	if (ch->ch_stopc != _POSIX_VDISABLE) {
		ch->ch_xoff_sends++;
		writeb(ch->ch_stopc, &ch->ch_cls_uart->txrx);
	}
}

/* Inits UART */
static void cls_uart_init(struct channel_t *ch)
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

	writeb(UART_FCR_ENABLE_FIFO | UART_FCR_CLEAR_RCVR | UART_FCR_CLEAR_XMIT,
	       &ch->ch_cls_uart->isr_fcr);
	udelay(10);

	ch->ch_flags |= (CH_FIFO_ENABLED | CH_TX_FIFO_EMPTY | CH_TX_FIFO_LWM);

	readb(&ch->ch_cls_uart->lsr);
	readb(&ch->ch_cls_uart->msr);
}

/* Turns off UART.  */

static void cls_uart_off(struct channel_t *ch)
{
	writeb(0, &ch->ch_cls_uart->ier);
}

/*
 * cls_get_uarts_bytes_left.
 * Returns 0 is nothing left in the FIFO, returns 1 otherwise.
 *
 * The channel lock MUST be held by the calling function.
 */
static uint cls_get_uart_bytes_left(struct channel_t *ch)
{
	unsigned char left = 0;
	unsigned char lsr = 0;

	if (!ch || ch->magic != DGNC_CHANNEL_MAGIC)
		return 0;

	lsr = readb(&ch->ch_cls_uart->lsr);

	/* Determine whether the Transmitter is empty or not */
	if (!(lsr & UART_LSR_TEMT)) {
		if (ch->ch_flags & CH_TX_FIFO_EMPTY)
			tasklet_schedule(&ch->ch_bd->helper_tasklet);
		left = 1;
	} else {
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
static void cls_send_break(struct channel_t *ch, int msecs)
{
	if (!ch || ch->magic != DGNC_CHANNEL_MAGIC)
		return;

	/* If we receive a time of 0, this means turn off the break. */

	if (msecs == 0) {
		/* Turn break off, and unset some variables */
		if (ch->ch_flags & CH_BREAK_SENDING) {
			unsigned char temp = readb(&ch->ch_cls_uart->lcr);

			writeb((temp & ~UART_LCR_SBC), &ch->ch_cls_uart->lcr);
			ch->ch_flags &= ~(CH_BREAK_SENDING);
			ch->ch_stop_sending_break = 0;
		}
		return;
	}

	/*
	 * Set the time we should stop sending the break.
	 * If we are already sending a break, toss away the existing
	 * time to stop, and use this new value instead.
	 */
	ch->ch_stop_sending_break = jiffies + dgnc_jiffies_from_ms(msecs);

	/* Tell the UART to start sending the break */
	if (!(ch->ch_flags & CH_BREAK_SENDING)) {
		unsigned char temp = readb(&ch->ch_cls_uart->lcr);

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
static void cls_send_immediate_char(struct channel_t *ch, unsigned char c)
{
	if (!ch || ch->magic != DGNC_CHANNEL_MAGIC)
		return;

	writeb(c, &ch->ch_cls_uart->txrx);
}

static void cls_vpd(struct dgnc_board *brd)
{
	ulong           vpdbase;        /* Start of io base of the card */
	u8 __iomem           *re_map_vpdbase;/* Remapped memory of the card */
	int i = 0;

	vpdbase = pci_resource_start(brd->pdev, 3);

	/* No VPD */
	if (!vpdbase)
		return;

	re_map_vpdbase = ioremap(vpdbase, 0x400);

	if (!re_map_vpdbase)
		return;

	/* Store the VPD into our buffer */
	for (i = 0; i < 0x40; i++) {
		brd->vpd[i] = readb(re_map_vpdbase + i);
		pr_info("%x ", brd->vpd[i]);
	}
	pr_info("\n");

	iounmap(re_map_vpdbase);
}

struct board_ops dgnc_cls_ops = {
	.tasklet =			cls_tasklet,
	.intr =				cls_intr,
	.uart_init =			cls_uart_init,
	.uart_off =			cls_uart_off,
	.drain =			cls_drain,
	.param =			cls_param,
	.vpd =				cls_vpd,
	.assert_modem_signals =		cls_assert_modem_signals,
	.flush_uart_write =		cls_flush_uart_write,
	.flush_uart_read =		cls_flush_uart_read,
	.disable_receiver =		cls_disable_receiver,
	.enable_receiver =		cls_enable_receiver,
	.send_break =			cls_send_break,
	.send_start_character =		cls_send_start_character,
	.send_stop_character =		cls_send_stop_character,
	.copy_data_from_queue_to_uart = cls_copy_data_from_queue_to_uart,
	.get_uart_bytes_left =		cls_get_uart_bytes_left,
	.send_immediate_char =		cls_send_immediate_char
};
