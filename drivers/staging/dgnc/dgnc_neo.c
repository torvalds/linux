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
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
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


#include <linux/kernel.h>
#include <linux/sched.h>	/* For jiffies, task states */
#include <linux/interrupt.h>    /* For tasklet and interrupt structs/defines */
#include <linux/delay.h>	/* For udelay */
#include <linux/io.h>		/* For read[bwl]/write[bwl] */
#include <linux/serial.h>	/* For struct async_serial */
#include <linux/serial_reg.h>	/* For the various UART offsets */

#include "dgnc_driver.h"	/* Driver main header file */
#include "dgnc_neo.h"		/* Our header file */
#include "dgnc_tty.h"

static inline void neo_parse_lsr(struct dgnc_board *brd, uint port);
static inline void neo_parse_isr(struct dgnc_board *brd, uint port);
static void neo_copy_data_from_uart_to_queue(struct channel_t *ch);
static inline void neo_clear_break(struct channel_t *ch, int force);
static inline void neo_set_cts_flow_control(struct channel_t *ch);
static inline void neo_set_rts_flow_control(struct channel_t *ch);
static inline void neo_set_ixon_flow_control(struct channel_t *ch);
static inline void neo_set_ixoff_flow_control(struct channel_t *ch);
static inline void neo_set_no_output_flow_control(struct channel_t *ch);
static inline void neo_set_no_input_flow_control(struct channel_t *ch);
static inline void neo_set_new_start_stop_chars(struct channel_t *ch);
static void neo_parse_modem(struct channel_t *ch, uchar signals);
static void neo_tasklet(unsigned long data);
static void neo_vpd(struct dgnc_board *brd);
static void neo_uart_init(struct channel_t *ch);
static void neo_uart_off(struct channel_t *ch);
static int neo_drain(struct tty_struct *tty, uint seconds);
static void neo_param(struct tty_struct *tty);
static void neo_assert_modem_signals(struct channel_t *ch);
static void neo_flush_uart_write(struct channel_t *ch);
static void neo_flush_uart_read(struct channel_t *ch);
static void neo_disable_receiver(struct channel_t *ch);
static void neo_enable_receiver(struct channel_t *ch);
static void neo_send_break(struct channel_t *ch, int msecs);
static void neo_send_start_character(struct channel_t *ch);
static void neo_send_stop_character(struct channel_t *ch);
static void neo_copy_data_from_queue_to_uart(struct channel_t *ch);
static uint neo_get_uart_bytes_left(struct channel_t *ch);
static void neo_send_immediate_char(struct channel_t *ch, unsigned char c);
static irqreturn_t neo_intr(int irq, void *voidbrd);


struct board_ops dgnc_neo_ops = {
	.tasklet =			neo_tasklet,
	.intr =				neo_intr,
	.uart_init =			neo_uart_init,
	.uart_off =			neo_uart_off,
	.drain =			neo_drain,
	.param =			neo_param,
	.vpd =				neo_vpd,
	.assert_modem_signals =		neo_assert_modem_signals,
	.flush_uart_write =		neo_flush_uart_write,
	.flush_uart_read =		neo_flush_uart_read,
	.disable_receiver =		neo_disable_receiver,
	.enable_receiver =		neo_enable_receiver,
	.send_break =			neo_send_break,
	.send_start_character =		neo_send_start_character,
	.send_stop_character =		neo_send_stop_character,
	.copy_data_from_queue_to_uart =	neo_copy_data_from_queue_to_uart,
	.get_uart_bytes_left =		neo_get_uart_bytes_left,
	.send_immediate_char =		neo_send_immediate_char
};

static uint dgnc_offset_table[8] = { 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80 };


/*
 * This function allows calls to ensure that all outstanding
 * PCI writes have been completed, by doing a PCI read against
 * a non-destructive, read-only location on the Neo card.
 *
 * In this case, we are reading the DVID (Read-only Device Identification)
 * value of the Neo card.
 */
static inline void neo_pci_posting_flush(struct dgnc_board *bd)
{
	readb(bd->re_map_membase + 0x8D);
}

static inline void neo_set_cts_flow_control(struct channel_t *ch)
{
	uchar ier = readb(&ch->ch_neo_uart->ier);
	uchar efr = readb(&ch->ch_neo_uart->efr);


	/* Turn on auto CTS flow control */
#if 1
	ier |= (UART_17158_IER_CTSDSR);
#else
	ier &= ~(UART_17158_IER_CTSDSR);
#endif

	efr |= (UART_17158_EFR_ECB | UART_17158_EFR_CTSDSR);

	/* Turn off auto Xon flow control */
	efr &= ~(UART_17158_EFR_IXON);

	/* Why? Becuz Exar's spec says we have to zero it out before setting it */
	writeb(0, &ch->ch_neo_uart->efr);

	/* Turn on UART enhanced bits */
	writeb(efr, &ch->ch_neo_uart->efr);

	/* Turn on table D, with 8 char hi/low watermarks */
	writeb((UART_17158_FCTR_TRGD | UART_17158_FCTR_RTS_4DELAY), &ch->ch_neo_uart->fctr);

	/* Feed the UART our trigger levels */
	writeb(8, &ch->ch_neo_uart->tfifo);
	ch->ch_t_tlevel = 8;

	writeb(ier, &ch->ch_neo_uart->ier);

	neo_pci_posting_flush(ch->ch_bd);
}


static inline void neo_set_rts_flow_control(struct channel_t *ch)
{
	uchar ier = readb(&ch->ch_neo_uart->ier);
	uchar efr = readb(&ch->ch_neo_uart->efr);

	/* Turn on auto RTS flow control */
#if 1
	ier |= (UART_17158_IER_RTSDTR);
#else
	ier &= ~(UART_17158_IER_RTSDTR);
#endif
	efr |= (UART_17158_EFR_ECB | UART_17158_EFR_RTSDTR);

	/* Turn off auto Xoff flow control */
	ier &= ~(UART_17158_IER_XOFF);
	efr &= ~(UART_17158_EFR_IXOFF);

	/* Why? Becuz Exar's spec says we have to zero it out before setting it */
	writeb(0, &ch->ch_neo_uart->efr);

	/* Turn on UART enhanced bits */
	writeb(efr, &ch->ch_neo_uart->efr);

	writeb((UART_17158_FCTR_TRGD | UART_17158_FCTR_RTS_4DELAY), &ch->ch_neo_uart->fctr);
	ch->ch_r_watermark = 4;

	writeb(32, &ch->ch_neo_uart->rfifo);
	ch->ch_r_tlevel = 32;

	writeb(ier, &ch->ch_neo_uart->ier);

	/*
	 * From the Neo UART spec sheet:
	 * The auto RTS/DTR function must be started by asserting
	 * RTS/DTR# output pin (MCR bit-0 or 1 to logic 1 after
	 * it is enabled.
	 */
	ch->ch_mostat |= (UART_MCR_RTS);

	neo_pci_posting_flush(ch->ch_bd);
}


static inline void neo_set_ixon_flow_control(struct channel_t *ch)
{
	uchar ier = readb(&ch->ch_neo_uart->ier);
	uchar efr = readb(&ch->ch_neo_uart->efr);

	/* Turn off auto CTS flow control */
	ier &= ~(UART_17158_IER_CTSDSR);
	efr &= ~(UART_17158_EFR_CTSDSR);

	/* Turn on auto Xon flow control */
	efr |= (UART_17158_EFR_ECB | UART_17158_EFR_IXON);

	/* Why? Becuz Exar's spec says we have to zero it out before setting it */
	writeb(0, &ch->ch_neo_uart->efr);

	/* Turn on UART enhanced bits */
	writeb(efr, &ch->ch_neo_uart->efr);

	writeb((UART_17158_FCTR_TRGD | UART_17158_FCTR_RTS_8DELAY), &ch->ch_neo_uart->fctr);
	ch->ch_r_watermark = 4;

	writeb(32, &ch->ch_neo_uart->rfifo);
	ch->ch_r_tlevel = 32;

	/* Tell UART what start/stop chars it should be looking for */
	writeb(ch->ch_startc, &ch->ch_neo_uart->xonchar1);
	writeb(0, &ch->ch_neo_uart->xonchar2);

	writeb(ch->ch_stopc, &ch->ch_neo_uart->xoffchar1);
	writeb(0, &ch->ch_neo_uart->xoffchar2);

	writeb(ier, &ch->ch_neo_uart->ier);

	neo_pci_posting_flush(ch->ch_bd);
}


static inline void neo_set_ixoff_flow_control(struct channel_t *ch)
{
	uchar ier = readb(&ch->ch_neo_uart->ier);
	uchar efr = readb(&ch->ch_neo_uart->efr);

	/* Turn off auto RTS flow control */
	ier &= ~(UART_17158_IER_RTSDTR);
	efr &= ~(UART_17158_EFR_RTSDTR);

	/* Turn on auto Xoff flow control */
	ier |= (UART_17158_IER_XOFF);
	efr |= (UART_17158_EFR_ECB | UART_17158_EFR_IXOFF);

	/* Why? Becuz Exar's spec says we have to zero it out before setting it */
	writeb(0, &ch->ch_neo_uart->efr);

	/* Turn on UART enhanced bits */
	writeb(efr, &ch->ch_neo_uart->efr);

	/* Turn on table D, with 8 char hi/low watermarks */
	writeb((UART_17158_FCTR_TRGD | UART_17158_FCTR_RTS_8DELAY), &ch->ch_neo_uart->fctr);

	writeb(8, &ch->ch_neo_uart->tfifo);
	ch->ch_t_tlevel = 8;

	/* Tell UART what start/stop chars it should be looking for */
	writeb(ch->ch_startc, &ch->ch_neo_uart->xonchar1);
	writeb(0, &ch->ch_neo_uart->xonchar2);

	writeb(ch->ch_stopc, &ch->ch_neo_uart->xoffchar1);
	writeb(0, &ch->ch_neo_uart->xoffchar2);

	writeb(ier, &ch->ch_neo_uart->ier);

	neo_pci_posting_flush(ch->ch_bd);
}


static inline void neo_set_no_input_flow_control(struct channel_t *ch)
{
	uchar ier = readb(&ch->ch_neo_uart->ier);
	uchar efr = readb(&ch->ch_neo_uart->efr);

	/* Turn off auto RTS flow control */
	ier &= ~(UART_17158_IER_RTSDTR);
	efr &= ~(UART_17158_EFR_RTSDTR);

	/* Turn off auto Xoff flow control */
	ier &= ~(UART_17158_IER_XOFF);
	if (ch->ch_c_iflag & IXON)
		efr &= ~(UART_17158_EFR_IXOFF);
	else
		efr &= ~(UART_17158_EFR_ECB | UART_17158_EFR_IXOFF);


	/* Why? Becuz Exar's spec says we have to zero it out before setting it */
	writeb(0, &ch->ch_neo_uart->efr);

	/* Turn on UART enhanced bits */
	writeb(efr, &ch->ch_neo_uart->efr);

	/* Turn on table D, with 8 char hi/low watermarks */
	writeb((UART_17158_FCTR_TRGD | UART_17158_FCTR_RTS_8DELAY), &ch->ch_neo_uart->fctr);

	ch->ch_r_watermark = 0;

	writeb(16, &ch->ch_neo_uart->tfifo);
	ch->ch_t_tlevel = 16;

	writeb(16, &ch->ch_neo_uart->rfifo);
	ch->ch_r_tlevel = 16;

	writeb(ier, &ch->ch_neo_uart->ier);

	neo_pci_posting_flush(ch->ch_bd);
}


static inline void neo_set_no_output_flow_control(struct channel_t *ch)
{
	uchar ier = readb(&ch->ch_neo_uart->ier);
	uchar efr = readb(&ch->ch_neo_uart->efr);

	/* Turn off auto CTS flow control */
	ier &= ~(UART_17158_IER_CTSDSR);
	efr &= ~(UART_17158_EFR_CTSDSR);

	/* Turn off auto Xon flow control */
	if (ch->ch_c_iflag & IXOFF)
		efr &= ~(UART_17158_EFR_IXON);
	else
		efr &= ~(UART_17158_EFR_ECB | UART_17158_EFR_IXON);

	/* Why? Becuz Exar's spec says we have to zero it out before setting it */
	writeb(0, &ch->ch_neo_uart->efr);

	/* Turn on UART enhanced bits */
	writeb(efr, &ch->ch_neo_uart->efr);

	/* Turn on table D, with 8 char hi/low watermarks */
	writeb((UART_17158_FCTR_TRGD | UART_17158_FCTR_RTS_8DELAY), &ch->ch_neo_uart->fctr);

	ch->ch_r_watermark = 0;

	writeb(16, &ch->ch_neo_uart->tfifo);
	ch->ch_t_tlevel = 16;

	writeb(16, &ch->ch_neo_uart->rfifo);
	ch->ch_r_tlevel = 16;

	writeb(ier, &ch->ch_neo_uart->ier);

	neo_pci_posting_flush(ch->ch_bd);
}


/* change UARTs start/stop chars */
static inline void neo_set_new_start_stop_chars(struct channel_t *ch)
{

	/* if hardware flow control is set, then skip this whole thing */
	if (ch->ch_digi.digi_flags & (CTSPACE | RTSPACE) || ch->ch_c_cflag & CRTSCTS)
		return;

	/* Tell UART what start/stop chars it should be looking for */
	writeb(ch->ch_startc, &ch->ch_neo_uart->xonchar1);
	writeb(0, &ch->ch_neo_uart->xonchar2);

	writeb(ch->ch_stopc, &ch->ch_neo_uart->xoffchar1);
	writeb(0, &ch->ch_neo_uart->xoffchar2);

	neo_pci_posting_flush(ch->ch_bd);
}


/*
 * No locks are assumed to be held when calling this function.
 */
static inline void neo_clear_break(struct channel_t *ch, int force)
{
	unsigned long flags;

	spin_lock_irqsave(&ch->ch_lock, flags);

	/* Bail if we aren't currently sending a break. */
	if (!ch->ch_stop_sending_break) {
		spin_unlock_irqrestore(&ch->ch_lock, flags);
		return;
	}

	/* Turn break off, and unset some variables */
	if (ch->ch_flags & CH_BREAK_SENDING) {
		if (time_after_eq(jiffies, ch->ch_stop_sending_break)
		    || force) {
			uchar temp = readb(&ch->ch_neo_uart->lcr);

			writeb((temp & ~UART_LCR_SBC), &ch->ch_neo_uart->lcr);
			neo_pci_posting_flush(ch->ch_bd);
			ch->ch_flags &= ~(CH_BREAK_SENDING);
			ch->ch_stop_sending_break = 0;
		}
	}
	spin_unlock_irqrestore(&ch->ch_lock, flags);
}


/*
 * Parse the ISR register.
 */
static inline void neo_parse_isr(struct dgnc_board *brd, uint port)
{
	struct channel_t *ch;
	uchar isr;
	uchar cause;
	unsigned long flags;

	if (!brd || brd->magic != DGNC_BOARD_MAGIC)
		return;

	if (port > brd->maxports)
		return;

	ch = brd->channels[port];
	if (!ch || ch->magic != DGNC_CHANNEL_MAGIC)
		return;

	/* Here we try to figure out what caused the interrupt to happen */
	while (1) {

		isr = readb(&ch->ch_neo_uart->isr_fcr);

		/* Bail if no pending interrupt */
		if (isr & UART_IIR_NO_INT)
			break;

		/*
		 * Yank off the upper 2 bits, which just show that the FIFO's are enabled.
		 */
		isr &= ~(UART_17158_IIR_FIFO_ENABLED);

		if (isr & (UART_17158_IIR_RDI_TIMEOUT | UART_IIR_RDI)) {
			/* Read data from uart -> queue */
			brd->intr_rx++;
			ch->ch_intr_rx++;
			neo_copy_data_from_uart_to_queue(ch);

			/* Call our tty layer to enforce queue flow control if needed. */
			spin_lock_irqsave(&ch->ch_lock, flags);
			dgnc_check_queue_flow_control(ch);
			spin_unlock_irqrestore(&ch->ch_lock, flags);
		}

		if (isr & UART_IIR_THRI) {
			brd->intr_tx++;
			ch->ch_intr_tx++;
			/* Transfer data (if any) from Write Queue -> UART. */
			spin_lock_irqsave(&ch->ch_lock, flags);
			ch->ch_flags |= (CH_TX_FIFO_EMPTY | CH_TX_FIFO_LWM);
			spin_unlock_irqrestore(&ch->ch_lock, flags);
			neo_copy_data_from_queue_to_uart(ch);
		}

		if (isr & UART_17158_IIR_XONXOFF) {
			cause = readb(&ch->ch_neo_uart->xoffchar1);

			/*
			 * Since the UART detected either an XON or
			 * XOFF match, we need to figure out which
			 * one it was, so we can suspend or resume data flow.
			 */
			if (cause == UART_17158_XON_DETECT) {
				/* Is output stopped right now, if so, resume it */
				if (brd->channels[port]->ch_flags & CH_STOP) {
					spin_lock_irqsave(&ch->ch_lock,
							  flags);
					ch->ch_flags &= ~(CH_STOP);
					spin_unlock_irqrestore(&ch->ch_lock,
							       flags);
				}
			} else if (cause == UART_17158_XOFF_DETECT) {
				if (!(brd->channels[port]->ch_flags & CH_STOP)) {
					spin_lock_irqsave(&ch->ch_lock,
							  flags);
					ch->ch_flags |= CH_STOP;
					spin_unlock_irqrestore(&ch->ch_lock,
							       flags);
				}
			}
		}

		if (isr & UART_17158_IIR_HWFLOW_STATE_CHANGE) {
			/*
			 * If we get here, this means the hardware is doing auto flow control.
			 * Check to see whether RTS/DTR or CTS/DSR caused this interrupt.
			 */
			brd->intr_modem++;
			ch->ch_intr_modem++;
			cause = readb(&ch->ch_neo_uart->mcr);
			/* Which pin is doing auto flow? RTS or DTR? */
			if ((cause & 0x4) == 0) {
				if (cause & UART_MCR_RTS) {
					spin_lock_irqsave(&ch->ch_lock,
							  flags);
					ch->ch_mostat |= UART_MCR_RTS;
					spin_unlock_irqrestore(&ch->ch_lock,
							       flags);
				} else {
					spin_lock_irqsave(&ch->ch_lock,
							  flags);
					ch->ch_mostat &= ~(UART_MCR_RTS);
					spin_unlock_irqrestore(&ch->ch_lock,
							       flags);
				}
			} else {
				if (cause & UART_MCR_DTR) {
					spin_lock_irqsave(&ch->ch_lock,
							  flags);
					ch->ch_mostat |= UART_MCR_DTR;
					spin_unlock_irqrestore(&ch->ch_lock,
							       flags);
				} else {
					spin_lock_irqsave(&ch->ch_lock,
							  flags);
					ch->ch_mostat &= ~(UART_MCR_DTR);
					spin_unlock_irqrestore(&ch->ch_lock,
							       flags);
				}
			}
		}

		/* Parse any modem signal changes */
		neo_parse_modem(ch, readb(&ch->ch_neo_uart->msr));
	}
}


static inline void neo_parse_lsr(struct dgnc_board *brd, uint port)
{
	struct channel_t *ch;
	int linestatus;
	unsigned long flags;

	if (!brd)
		return;

	if (brd->magic != DGNC_BOARD_MAGIC)
		return;

	if (port > brd->maxports)
		return;

	ch = brd->channels[port];
	if (!ch || ch->magic != DGNC_CHANNEL_MAGIC)
		return;

	linestatus = readb(&ch->ch_neo_uart->lsr);

	ch->ch_cached_lsr |= linestatus;

	if (ch->ch_cached_lsr & UART_LSR_DR) {
		brd->intr_rx++;
		ch->ch_intr_rx++;
		/* Read data from uart -> queue */
		neo_copy_data_from_uart_to_queue(ch);
		spin_lock_irqsave(&ch->ch_lock, flags);
		dgnc_check_queue_flow_control(ch);
		spin_unlock_irqrestore(&ch->ch_lock, flags);
	}

	/*
	 * The next 3 tests should *NOT* happen, as the above test
	 * should encapsulate all 3... At least, thats what Exar says.
	 */

	if (linestatus & UART_LSR_PE)
		ch->ch_err_parity++;

	if (linestatus & UART_LSR_FE)
		ch->ch_err_frame++;

	if (linestatus & UART_LSR_BI)
		ch->ch_err_break++;

	if (linestatus & UART_LSR_OE) {
		/*
		 * Rx Oruns. Exar says that an orun will NOT corrupt
		 * the FIFO. It will just replace the holding register
		 * with this new data byte. So basically just ignore this.
		 * Probably we should eventually have an orun stat in our driver...
		 */
		ch->ch_err_overrun++;
	}

	if (linestatus & UART_LSR_THRE) {
		brd->intr_tx++;
		ch->ch_intr_tx++;
		spin_lock_irqsave(&ch->ch_lock, flags);
		ch->ch_flags |= (CH_TX_FIFO_EMPTY | CH_TX_FIFO_LWM);
		spin_unlock_irqrestore(&ch->ch_lock, flags);

		/* Transfer data (if any) from Write Queue -> UART. */
		neo_copy_data_from_queue_to_uart(ch);
	} else if (linestatus & UART_17158_TX_AND_FIFO_CLR) {
		brd->intr_tx++;
		ch->ch_intr_tx++;
		spin_lock_irqsave(&ch->ch_lock, flags);
		ch->ch_flags |= (CH_TX_FIFO_EMPTY | CH_TX_FIFO_LWM);
		spin_unlock_irqrestore(&ch->ch_lock, flags);

		/* Transfer data (if any) from Write Queue -> UART. */
		neo_copy_data_from_queue_to_uart(ch);
	}
}


/*
 * neo_param()
 * Send any/all changes to the line to the UART.
 */
static void neo_param(struct tty_struct *tty)
{
	uchar lcr = 0;
	uchar uart_lcr = 0;
	uchar ier = 0;
	uchar uart_ier = 0;
	uint baud = 9600;
	int quot = 0;
	struct dgnc_board *bd;
	struct channel_t *ch;
	struct un_t   *un;

	if (!tty || tty->magic != TTY_MAGIC)
		return;

	un = (struct un_t *) tty->driver_data;
	if (!un || un->magic != DGNC_UNIT_MAGIC)
		return;

	ch = un->un_ch;
	if (!ch || ch->magic != DGNC_CHANNEL_MAGIC)
		return;

	bd = ch->ch_bd;
	if (!bd || bd->magic != DGNC_BOARD_MAGIC)
		return;

	/*
	 * If baud rate is zero, flush queues, and set mval to drop DTR.
	 */
	if ((ch->ch_c_cflag & (CBAUD)) == 0) {
		ch->ch_r_head = 0;
		ch->ch_r_tail = 0;
		ch->ch_e_head = 0;
		ch->ch_e_tail = 0;
		ch->ch_w_head = 0;
		ch->ch_w_tail = 0;

		neo_flush_uart_write(ch);
		neo_flush_uart_read(ch);

		/* The baudrate is B0 so all modem lines are to be dropped. */
		ch->ch_flags |= (CH_BAUD0);
		ch->ch_mostat &= ~(UART_MCR_RTS | UART_MCR_DTR);
		neo_assert_modem_signals(ch);
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

		/* Only use the TXPrint baud rate if the terminal unit is NOT open */
		if (!(ch->ch_tun.un_flags & UN_ISOPEN) && (un->un_type == DGNC_PRINT))
			baud = C_BAUD(ch->ch_pun.un_tty) & 0xff;
		else
			baud = C_BAUD(ch->ch_tun.un_tty) & 0xff;

		if (ch->ch_c_cflag & CBAUDEX)
			iindex = 1;

		if (ch->ch_digi.digi_flags & DIGI_FAST)
			iindex += 2;

		jindex = baud;

		if ((iindex >= 0) && (iindex < 4) && (jindex >= 0) && (jindex < 16))
			baud = bauds[iindex][jindex];
		else
			baud = 0;

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

	uart_ier = readb(&ch->ch_neo_uart->ier);
	ier = uart_ier;

	uart_lcr = readb(&ch->ch_neo_uart->lcr);

	if (baud == 0)
		baud = 9600;

	quot = ch->ch_bd->bd_dividend / baud;

	if (quot != 0 && ch->ch_old_baud != baud) {
		ch->ch_old_baud = baud;
		writeb(UART_LCR_DLAB, &ch->ch_neo_uart->lcr);
		writeb((quot & 0xff), &ch->ch_neo_uart->txrx);
		writeb((quot >> 8), &ch->ch_neo_uart->ier);
		writeb(lcr, &ch->ch_neo_uart->lcr);
	}

	if (uart_lcr != lcr)
		writeb(lcr, &ch->ch_neo_uart->lcr);

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
		writeb(ier, &ch->ch_neo_uart->ier);

	/* Set new start/stop chars */
	neo_set_new_start_stop_chars(ch);

	if (ch->ch_digi.digi_flags & CTSPACE || ch->ch_c_cflag & CRTSCTS) {
		neo_set_cts_flow_control(ch);
	} else if (ch->ch_c_iflag & IXON) {
		/* If start/stop is set to disable, then we should disable flow control */
		if ((ch->ch_startc == _POSIX_VDISABLE) || (ch->ch_stopc == _POSIX_VDISABLE))
			neo_set_no_output_flow_control(ch);
		else
			neo_set_ixon_flow_control(ch);
	} else {
		neo_set_no_output_flow_control(ch);
	}

	if (ch->ch_digi.digi_flags & RTSPACE || ch->ch_c_cflag & CRTSCTS) {
		neo_set_rts_flow_control(ch);
	} else if (ch->ch_c_iflag & IXOFF) {
		/* If start/stop is set to disable, then we should disable flow control */
		if ((ch->ch_startc == _POSIX_VDISABLE) || (ch->ch_stopc == _POSIX_VDISABLE))
			neo_set_no_input_flow_control(ch);
		else
			neo_set_ixoff_flow_control(ch);
	} else {
		neo_set_no_input_flow_control(ch);
	}

	/*
	 * Adjust the RX FIFO Trigger level if baud is less than 9600.
	 * Not exactly elegant, but this is needed because of the Exar chip's
	 * delay on firing off the RX FIFO interrupt on slower baud rates.
	 */
	if (baud < 9600) {
		writeb(1, &ch->ch_neo_uart->rfifo);
		ch->ch_r_tlevel = 1;
	}

	neo_assert_modem_signals(ch);

	/* Get current status of the modem signals now */
	neo_parse_modem(ch, readb(&ch->ch_neo_uart->msr));
}


/*
 * Our board poller function.
 */
static void neo_tasklet(unsigned long data)
{
	struct dgnc_board *bd = (struct dgnc_board *) data;
	struct channel_t *ch;
	unsigned long flags;
	int i;
	int state = 0;
	int ports = 0;

	if (!bd || bd->magic != DGNC_BOARD_MAGIC) {
		APR(("poll_tasklet() - NULL or bad bd.\n"));
		return;
	}

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

	/*
	 * If board is ready, parse deeper to see if there is anything to do.
	 */
	if ((state == BOARD_READY) && (ports > 0)) {
		/* Loop on each port */
		for (i = 0; i < ports; i++) {
			ch = bd->channels[i];

			/* Just being careful... */
			if (!ch || ch->magic != DGNC_CHANNEL_MAGIC)
				continue;

			/*
			 * NOTE: Remember you CANNOT hold any channel
			 * locks when calling the input routine.
			 *
			 * During input processing, its possible we
			 * will call the Linux ld, which might in turn,
			 * do a callback right back into us, resulting
			 * in us trying to grab the channel lock twice!
			 */
			dgnc_input(ch);

			/*
			 * Channel lock is grabbed and then released
			 * inside both of these routines, but neither
			 * call anything else that could call back into us.
			 */
			neo_copy_data_from_queue_to_uart(ch);
			dgnc_wakeup_writes(ch);

			/*
			 * Call carrier carrier function, in case something
			 * has changed.
			 */
			dgnc_carrier(ch);

			/*
			 * Check to see if we need to turn off a sending break.
			 * The timing check is done inside clear_break()
			 */
			if (ch->ch_stop_sending_break)
				neo_clear_break(ch, 0);
		}
	}

	/* Allow interrupt routine to access the interrupt register again */
	spin_unlock_irqrestore(&bd->bd_intr_lock, flags);

}


/*
 * dgnc_neo_intr()
 *
 * Neo specific interrupt handler.
 */
static irqreturn_t neo_intr(int irq, void *voidbrd)
{
	struct dgnc_board *brd = (struct dgnc_board *) voidbrd;
	struct channel_t *ch;
	int port = 0;
	int type = 0;
	int current_port;
	u32 tmp;
	u32 uart_poll;
	unsigned long flags;
	unsigned long flags2;

	if (!brd) {
		APR(("Received interrupt (%d) with null board associated\n", irq));
		return IRQ_NONE;
	}

	/*
	 * Check to make sure its for us.
	 */
	if (brd->magic != DGNC_BOARD_MAGIC) {
		APR(("Received interrupt (%d) with a board pointer that wasn't ours!\n", irq));
		return IRQ_NONE;
	}

	brd->intr_count++;

	/* Lock out the slow poller from running on this board. */
	spin_lock_irqsave(&brd->bd_intr_lock, flags);

	/*
	 * Read in "extended" IRQ information from the 32bit Neo register.
	 * Bits 0-7: What port triggered the interrupt.
	 * Bits 8-31: Each 3bits indicate what type of interrupt occurred.
	 */
	uart_poll = readl(brd->re_map_membase + UART_17158_POLL_ADDR_OFFSET);

	/*
	 * If 0, no interrupts pending.
	 * This can happen if the IRQ is shared among a couple Neo/Classic boards.
	 */
	if (!uart_poll) {
		spin_unlock_irqrestore(&brd->bd_intr_lock, flags);
		return IRQ_NONE;
	}

	/* At this point, we have at least SOMETHING to service, dig further... */

	current_port = 0;

	/* Loop on each port */
	while ((uart_poll & 0xff) != 0) {

		tmp = uart_poll;

		/* Check current port to see if it has interrupt pending */
		if ((tmp & dgnc_offset_table[current_port]) != 0) {
			port = current_port;
			type = tmp >> (8 + (port * 3));
			type &= 0x7;
		} else {
			current_port++;
			continue;
		}

		/* Remove this port + type from uart_poll */
		uart_poll &= ~(dgnc_offset_table[port]);

		if (!type) {
			/* If no type, just ignore it, and move onto next port */
			continue;
		}

		/* Switch on type of interrupt we have */
		switch (type) {

		case UART_17158_RXRDY_TIMEOUT:
			/*
			 * RXRDY Time-out is cleared by reading data in the
			 * RX FIFO until it falls below the trigger level.
			 */

			/* Verify the port is in range. */
			if (port > brd->nasync)
				continue;

			ch = brd->channels[port];
			neo_copy_data_from_uart_to_queue(ch);

			/* Call our tty layer to enforce queue flow control if needed. */
			spin_lock_irqsave(&ch->ch_lock, flags2);
			dgnc_check_queue_flow_control(ch);
			spin_unlock_irqrestore(&ch->ch_lock, flags2);

			continue;

		case UART_17158_RX_LINE_STATUS:
			/*
			 * RXRDY and RX LINE Status (logic OR of LSR[4:1])
			 */
			neo_parse_lsr(brd, port);
			continue;

		case UART_17158_TXRDY:
			/*
			 * TXRDY interrupt clears after reading ISR register for the UART channel.
			 */

			/*
			 * Yes, this is odd...
			 * Why would I check EVERY possibility of type of
			 * interrupt, when we know its TXRDY???
			 * Becuz for some reason, even tho we got triggered for TXRDY,
			 * it seems to be occasionally wrong. Instead of TX, which
			 * it should be, I was getting things like RXDY too. Weird.
			 */
			neo_parse_isr(brd, port);
			continue;

		case UART_17158_MSR:
			/*
			 * MSR or flow control was seen.
			 */
			neo_parse_isr(brd, port);
			continue;

		default:
			/*
			 * The UART triggered us with a bogus interrupt type.
			 * It appears the Exar chip, when REALLY bogged down, will throw
			 * these once and awhile.
			 * Its harmless, just ignore it and move on.
			 */
			continue;
		}
	}

	/*
	 * Schedule tasklet to more in-depth servicing at a better time.
	 */
	tasklet_schedule(&brd->helper_tasklet);

	spin_unlock_irqrestore(&brd->bd_intr_lock, flags);

	return IRQ_HANDLED;
}


/*
 * Neo specific way of turning off the receiver.
 * Used as a way to enforce queue flow control when in
 * hardware flow control mode.
 */
static void neo_disable_receiver(struct channel_t *ch)
{
	uchar tmp = readb(&ch->ch_neo_uart->ier);

	tmp &= ~(UART_IER_RDI);
	writeb(tmp, &ch->ch_neo_uart->ier);
	neo_pci_posting_flush(ch->ch_bd);
}


/*
 * Neo specific way of turning on the receiver.
 * Used as a way to un-enforce queue flow control when in
 * hardware flow control mode.
 */
static void neo_enable_receiver(struct channel_t *ch)
{
	uchar tmp = readb(&ch->ch_neo_uart->ier);

	tmp |= (UART_IER_RDI);
	writeb(tmp, &ch->ch_neo_uart->ier);
	neo_pci_posting_flush(ch->ch_bd);
}


static void neo_copy_data_from_uart_to_queue(struct channel_t *ch)
{
	int qleft = 0;
	uchar linestatus = 0;
	uchar error_mask = 0;
	int n = 0;
	int total = 0;
	ushort head;
	ushort tail;
	unsigned long flags;

	if (!ch || ch->magic != DGNC_CHANNEL_MAGIC)
		return;

	spin_lock_irqsave(&ch->ch_lock, flags);

	/* cache head and tail of queue */
	head = ch->ch_r_head & RQUEUEMASK;
	tail = ch->ch_r_tail & RQUEUEMASK;

	/* Get our cached LSR */
	linestatus = ch->ch_cached_lsr;
	ch->ch_cached_lsr = 0;

	/* Store how much space we have left in the queue */
	qleft = tail - head - 1;
	if (qleft < 0)
		qleft += RQUEUEMASK + 1;

	/*
	 * If the UART is not in FIFO mode, force the FIFO copy to
	 * NOT be run, by setting total to 0.
	 *
	 * On the other hand, if the UART IS in FIFO mode, then ask
	 * the UART to give us an approximation of data it has RX'ed.
	 */
	if (!(ch->ch_flags & CH_FIFO_ENABLED))
		total = 0;
	else {
		total = readb(&ch->ch_neo_uart->rfifo);

		/*
		 * EXAR chip bug - RX FIFO COUNT - Fudge factor.
		 *
		 * This resolves a problem/bug with the Exar chip that sometimes
		 * returns a bogus value in the rfifo register.
		 * The count can be any where from 0-3 bytes "off".
		 * Bizarre, but true.
		 */
		if ((ch->ch_bd->dvid & 0xf0) >= UART_XR17E158_DVID)
			total -= 1;
		else
			total -= 3;
	}


	/*
	 * Finally, bound the copy to make sure we don't overflow
	 * our own queue...
	 * The byte by byte copy loop below this loop this will
	 * deal with the queue overflow possibility.
	 */
	total = min(total, qleft);

	while (total > 0) {

		/*
		 * Grab the linestatus register, we need to check
		 * to see if there are any errors in the FIFO.
		 */
		linestatus = readb(&ch->ch_neo_uart->lsr);

		/*
		 * Break out if there is a FIFO error somewhere.
		 * This will allow us to go byte by byte down below,
		 * finding the exact location of the error.
		 */
		if (linestatus & UART_17158_RX_FIFO_DATA_ERROR)
			break;

		/* Make sure we don't go over the end of our queue */
		n = min(((uint) total), (RQUEUESIZE - (uint) head));

		/*
		 * Cut down n even further if needed, this is to fix
		 * a problem with memcpy_fromio() with the Neo on the
		 * IBM pSeries platform.
		 * 15 bytes max appears to be the magic number.
		 */
		n = min((uint) n, (uint) 12);

		/*
		 * Since we are grabbing the linestatus register, which
		 * will reset some bits after our read, we need to ensure
		 * we don't miss our TX FIFO emptys.
		 */
		if (linestatus & (UART_LSR_THRE | UART_17158_TX_AND_FIFO_CLR))
			ch->ch_flags |= (CH_TX_FIFO_EMPTY | CH_TX_FIFO_LWM);

		linestatus = 0;

		/* Copy data from uart to the queue */
		memcpy_fromio(ch->ch_rqueue + head, &ch->ch_neo_uart->txrxburst, n);
		dgnc_sniff_nowait_nolock(ch, "UART READ", ch->ch_rqueue + head, n);

		/*
		 * Since RX_FIFO_DATA_ERROR was 0, we are guarenteed
		 * that all the data currently in the FIFO is free of
		 * breaks and parity/frame/orun errors.
		 */
		memset(ch->ch_equeue + head, 0, n);

		/* Add to and flip head if needed */
		head = (head + n) & RQUEUEMASK;
		total -= n;
		qleft -= n;
		ch->ch_rxcount += n;
	}

	/*
	 * Create a mask to determine whether we should
	 * insert the character (if any) into our queue.
	 */
	if (ch->ch_c_iflag & IGNBRK)
		error_mask |= UART_LSR_BI;

	/*
	 * Now cleanup any leftover bytes still in the UART.
	 * Also deal with any possible queue overflow here as well.
	 */
	while (1) {

		/*
		 * Its possible we have a linestatus from the loop above
		 * this, so we "OR" on any extra bits.
		 */
		linestatus |= readb(&ch->ch_neo_uart->lsr);

		/*
		 * If the chip tells us there is no more data pending to
		 * be read, we can then leave.
		 * But before we do, cache the linestatus, just in case.
		 */
		if (!(linestatus & UART_LSR_DR)) {
			ch->ch_cached_lsr = linestatus;
			break;
		}

		/* No need to store this bit */
		linestatus &= ~UART_LSR_DR;

		/*
		 * Since we are grabbing the linestatus register, which
		 * will reset some bits after our read, we need to ensure
		 * we don't miss our TX FIFO emptys.
		 */
		if (linestatus & (UART_LSR_THRE | UART_17158_TX_AND_FIFO_CLR)) {
			linestatus &= ~(UART_LSR_THRE | UART_17158_TX_AND_FIFO_CLR);
			ch->ch_flags |= (CH_TX_FIFO_EMPTY | CH_TX_FIFO_LWM);
		}

		/*
		 * Discard character if we are ignoring the error mask.
		 */
		if (linestatus & error_mask)  {
			uchar discard;

			linestatus = 0;
			memcpy_fromio(&discard, &ch->ch_neo_uart->txrxburst, 1);
			continue;
		}

		/*
		 * If our queue is full, we have no choice but to drop some data.
		 * The assumption is that HWFLOW or SWFLOW should have stopped
		 * things way way before we got to this point.
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

		memcpy_fromio(ch->ch_rqueue + head, &ch->ch_neo_uart->txrxburst, 1);
		ch->ch_equeue[head] = (uchar) linestatus;
		dgnc_sniff_nowait_nolock(ch, "UART READ", ch->ch_rqueue + head, 1);

		/* Ditch any remaining linestatus value. */
		linestatus = 0;

		/* Add to and flip head if needed */
		head = (head + 1) & RQUEUEMASK;

		qleft--;
		ch->ch_rxcount++;
	}

	/*
	 * Write new final heads to channel structure.
	 */
	ch->ch_r_head = head & RQUEUEMASK;
	ch->ch_e_head = head & EQUEUEMASK;

	spin_unlock_irqrestore(&ch->ch_lock, flags);
}


/*
 * This function basically goes to sleep for secs, or until
 * it gets signalled that the port has fully drained.
 */
static int neo_drain(struct tty_struct *tty, uint seconds)
{
	unsigned long flags;
	struct channel_t *ch;
	struct un_t *un;
	int rc = 0;

	if (!tty || tty->magic != TTY_MAGIC)
		return -ENXIO;

	un = (struct un_t *) tty->driver_data;
	if (!un || un->magic != DGNC_UNIT_MAGIC)
		return -ENXIO;

	ch = un->un_ch;
	if (!ch || ch->magic != DGNC_CHANNEL_MAGIC)
		return -ENXIO;

	spin_lock_irqsave(&ch->ch_lock, flags);
	un->un_flags |= UN_EMPTY;
	spin_unlock_irqrestore(&ch->ch_lock, flags);

	/*
	 * Go to sleep waiting for the tty layer to wake me back up when
	 * the empty flag goes away.
	 *
	 * NOTE: TODO: Do something with time passed in.
	 */
	rc = wait_event_interruptible(un->un_flags_wait, ((un->un_flags & UN_EMPTY) == 0));

	/* If ret is non-zero, user ctrl-c'ed us */
	return rc;
}


/*
 * Flush the WRITE FIFO on the Neo.
 *
 * NOTE: Channel lock MUST be held before calling this function!
 */
static void neo_flush_uart_write(struct channel_t *ch)
{
	uchar tmp = 0;
	int i = 0;

	if (!ch || ch->magic != DGNC_CHANNEL_MAGIC)
		return;

	writeb((UART_FCR_ENABLE_FIFO | UART_FCR_CLEAR_XMIT), &ch->ch_neo_uart->isr_fcr);
	neo_pci_posting_flush(ch->ch_bd);

	for (i = 0; i < 10; i++) {

		/* Check to see if the UART feels it completely flushed the FIFO. */
		tmp = readb(&ch->ch_neo_uart->isr_fcr);
		if (tmp & 4)
			udelay(10);
		else
			break;
	}

	ch->ch_flags |= (CH_TX_FIFO_EMPTY | CH_TX_FIFO_LWM);
}


/*
 * Flush the READ FIFO on the Neo.
 *
 * NOTE: Channel lock MUST be held before calling this function!
 */
static void neo_flush_uart_read(struct channel_t *ch)
{
	uchar tmp = 0;
	int i = 0;

	if (!ch || ch->magic != DGNC_CHANNEL_MAGIC)
		return;

	writeb((UART_FCR_ENABLE_FIFO | UART_FCR_CLEAR_RCVR), &ch->ch_neo_uart->isr_fcr);
	neo_pci_posting_flush(ch->ch_bd);

	for (i = 0; i < 10; i++) {

		/* Check to see if the UART feels it completely flushed the FIFO. */
		tmp = readb(&ch->ch_neo_uart->isr_fcr);
		if (tmp & 2)
			udelay(10);
		else
			break;
	}
}


static void neo_copy_data_from_queue_to_uart(struct channel_t *ch)
{
	ushort head;
	ushort tail;
	int n;
	int s;
	int qlen;
	uint len_written = 0;
	unsigned long flags;

	if (!ch || ch->magic != DGNC_CHANNEL_MAGIC)
		return;

	spin_lock_irqsave(&ch->ch_lock, flags);

	/* No data to write to the UART */
	if (ch->ch_w_tail == ch->ch_w_head) {
		spin_unlock_irqrestore(&ch->ch_lock, flags);
		return;
	}

	/* If port is "stopped", don't send any data to the UART */
	if ((ch->ch_flags & CH_FORCED_STOP) || (ch->ch_flags & CH_BREAK_SENDING)) {
		spin_unlock_irqrestore(&ch->ch_lock, flags);
		return;
	}

	/*
	 * If FIFOs are disabled. Send data directly to txrx register
	 */
	if (!(ch->ch_flags & CH_FIFO_ENABLED)) {
		uchar lsrbits = readb(&ch->ch_neo_uart->lsr);

		/* Cache the LSR bits for later parsing */
		ch->ch_cached_lsr |= lsrbits;
		if (ch->ch_cached_lsr & UART_LSR_THRE) {
			ch->ch_cached_lsr &= ~(UART_LSR_THRE);

			/*
			 * If RTS Toggle mode is on, turn on RTS now if not already set,
			 * and make sure we get an event when the data transfer has completed.
			 */
			if (ch->ch_digi.digi_flags & DIGI_RTS_TOGGLE) {
				if (!(ch->ch_mostat & UART_MCR_RTS)) {
					ch->ch_mostat |= (UART_MCR_RTS);
					neo_assert_modem_signals(ch);
				}
				ch->ch_tun.un_flags |= (UN_EMPTY);
			}
			/*
			 * If DTR Toggle mode is on, turn on DTR now if not already set,
			 * and make sure we get an event when the data transfer has completed.
			 */
			if (ch->ch_digi.digi_flags & DIGI_DTR_TOGGLE) {
				if (!(ch->ch_mostat & UART_MCR_DTR)) {
					ch->ch_mostat |= (UART_MCR_DTR);
					neo_assert_modem_signals(ch);
				}
				ch->ch_tun.un_flags |= (UN_EMPTY);
			}

			writeb(ch->ch_wqueue[ch->ch_w_tail], &ch->ch_neo_uart->txrx);
			ch->ch_w_tail++;
			ch->ch_w_tail &= WQUEUEMASK;
			ch->ch_txcount++;
		}
		spin_unlock_irqrestore(&ch->ch_lock, flags);
		return;
	}

	/*
	 * We have to do it this way, because of the EXAR TXFIFO count bug.
	 */
	if ((ch->ch_bd->dvid & 0xf0) < UART_XR17E158_DVID) {
		if (!(ch->ch_flags & (CH_TX_FIFO_EMPTY | CH_TX_FIFO_LWM))) {
			spin_unlock_irqrestore(&ch->ch_lock, flags);
			return;
		}

		len_written = 0;

		n = readb(&ch->ch_neo_uart->tfifo);

		if ((unsigned int) n > ch->ch_t_tlevel) {
			spin_unlock_irqrestore(&ch->ch_lock, flags);
			return;
		}

		n = UART_17158_TX_FIFOSIZE - ch->ch_t_tlevel;
	} else {
		n = UART_17158_TX_FIFOSIZE - readb(&ch->ch_neo_uart->tfifo);
	}

	/* cache head and tail of queue */
	head = ch->ch_w_head & WQUEUEMASK;
	tail = ch->ch_w_tail & WQUEUEMASK;
	qlen = (head - tail) & WQUEUEMASK;

	/* Find minimum of the FIFO space, versus queue length */
	n = min(n, qlen);

	while (n > 0) {

		s = ((head >= tail) ? head : WQUEUESIZE) - tail;
		s = min(s, n);

		if (s <= 0)
			break;

		/*
		 * If RTS Toggle mode is on, turn on RTS now if not already set,
		 * and make sure we get an event when the data transfer has completed.
		 */
		if (ch->ch_digi.digi_flags & DIGI_RTS_TOGGLE) {
			if (!(ch->ch_mostat & UART_MCR_RTS)) {
				ch->ch_mostat |= (UART_MCR_RTS);
				neo_assert_modem_signals(ch);
			}
			ch->ch_tun.un_flags |= (UN_EMPTY);
		}

		/*
		 * If DTR Toggle mode is on, turn on DTR now if not already set,
		 * and make sure we get an event when the data transfer has completed.
		 */
		if (ch->ch_digi.digi_flags & DIGI_DTR_TOGGLE) {
			if (!(ch->ch_mostat & UART_MCR_DTR)) {
				ch->ch_mostat |= (UART_MCR_DTR);
				neo_assert_modem_signals(ch);
			}
			ch->ch_tun.un_flags |= (UN_EMPTY);
		}

		memcpy_toio(&ch->ch_neo_uart->txrxburst, ch->ch_wqueue + tail, s);
		dgnc_sniff_nowait_nolock(ch, "UART WRITE", ch->ch_wqueue + tail, s);

		/* Add and flip queue if needed */
		tail = (tail + s) & WQUEUEMASK;
		n -= s;
		ch->ch_txcount += s;
		len_written += s;
	}

	/* Update the final tail */
	ch->ch_w_tail = tail & WQUEUEMASK;

	if (len_written > 0) {
		neo_pci_posting_flush(ch->ch_bd);
		ch->ch_flags &= ~(CH_TX_FIFO_EMPTY | CH_TX_FIFO_LWM);
	}

	spin_unlock_irqrestore(&ch->ch_lock, flags);
}


static void neo_parse_modem(struct channel_t *ch, uchar signals)
{
	uchar msignals = signals;

	if (!ch || ch->magic != DGNC_CHANNEL_MAGIC)
		return;

	/*
	 * Do altpin switching. Altpin switches DCD and DSR.
	 * This prolly breaks DSRPACE, so we should be more clever here.
	 */
	if (ch->ch_digi.digi_flags & DIGI_ALTPIN) {
		uchar mswap = msignals;

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

	/* Scrub off lower bits. They signify delta's, which I don't care about */
	msignals &= 0xf0;

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
}


/* Make the UART raise any of the output signals we want up */
static void neo_assert_modem_signals(struct channel_t *ch)
{
	uchar out;

	if (!ch || ch->magic != DGNC_CHANNEL_MAGIC)
		return;

	out = ch->ch_mostat;

	if (ch->ch_flags & CH_LOOPBACK)
		out |= UART_MCR_LOOP;

	writeb(out, &ch->ch_neo_uart->mcr);
	neo_pci_posting_flush(ch->ch_bd);

	/* Give time for the UART to actually raise/drop the signals */
	udelay(10);
}


static void neo_send_start_character(struct channel_t *ch)
{
	if (!ch || ch->magic != DGNC_CHANNEL_MAGIC)
		return;

	if (ch->ch_startc != _POSIX_VDISABLE) {
		ch->ch_xon_sends++;
		writeb(ch->ch_startc, &ch->ch_neo_uart->txrx);
		neo_pci_posting_flush(ch->ch_bd);
		udelay(10);
	}
}


static void neo_send_stop_character(struct channel_t *ch)
{
	if (!ch || ch->magic != DGNC_CHANNEL_MAGIC)
		return;

	if (ch->ch_stopc != _POSIX_VDISABLE) {
		ch->ch_xoff_sends++;
		writeb(ch->ch_stopc, &ch->ch_neo_uart->txrx);
		neo_pci_posting_flush(ch->ch_bd);
		udelay(10);
	}
}


/*
 * neo_uart_init
 */
static void neo_uart_init(struct channel_t *ch)
{

	writeb(0, &ch->ch_neo_uart->ier);
	writeb(0, &ch->ch_neo_uart->efr);
	writeb(UART_EFR_ECB, &ch->ch_neo_uart->efr);


	/* Clear out UART and FIFO */
	readb(&ch->ch_neo_uart->txrx);
	writeb((UART_FCR_ENABLE_FIFO|UART_FCR_CLEAR_RCVR|UART_FCR_CLEAR_XMIT), &ch->ch_neo_uart->isr_fcr);
	readb(&ch->ch_neo_uart->lsr);
	readb(&ch->ch_neo_uart->msr);

	ch->ch_flags |= CH_FIFO_ENABLED;

	/* Assert any signals we want up */
	writeb(ch->ch_mostat, &ch->ch_neo_uart->mcr);
	neo_pci_posting_flush(ch->ch_bd);
}


/*
 * Make the UART completely turn off.
 */
static void neo_uart_off(struct channel_t *ch)
{
	/* Turn off UART enhanced bits */
	writeb(0, &ch->ch_neo_uart->efr);

	/* Stop all interrupts from occurring. */
	writeb(0, &ch->ch_neo_uart->ier);
	neo_pci_posting_flush(ch->ch_bd);
}


static uint neo_get_uart_bytes_left(struct channel_t *ch)
{
	uchar left = 0;
	uchar lsr = readb(&ch->ch_neo_uart->lsr);

	/* We must cache the LSR as some of the bits get reset once read... */
	ch->ch_cached_lsr |= lsr;

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


/* Channel lock MUST be held by the calling function! */
static void neo_send_break(struct channel_t *ch, int msecs)
{
	/*
	 * If we receive a time of 0, this means turn off the break.
	 */
	if (msecs == 0) {
		if (ch->ch_flags & CH_BREAK_SENDING) {
			uchar temp = readb(&ch->ch_neo_uart->lcr);

			writeb((temp & ~UART_LCR_SBC), &ch->ch_neo_uart->lcr);
			neo_pci_posting_flush(ch->ch_bd);
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
		uchar temp = readb(&ch->ch_neo_uart->lcr);

		writeb((temp | UART_LCR_SBC), &ch->ch_neo_uart->lcr);
		neo_pci_posting_flush(ch->ch_bd);
		ch->ch_flags |= (CH_BREAK_SENDING);
	}
}


/*
 * neo_send_immediate_char.
 *
 * Sends a specific character as soon as possible to the UART,
 * jumping over any bytes that might be in the write queue.
 *
 * The channel lock MUST be held by the calling function.
 */
static void neo_send_immediate_char(struct channel_t *ch, unsigned char c)
{
	if (!ch || ch->magic != DGNC_CHANNEL_MAGIC)
		return;

	writeb(c, &ch->ch_neo_uart->txrx);
	neo_pci_posting_flush(ch->ch_bd);
}


static unsigned int neo_read_eeprom(unsigned char __iomem *base, unsigned int address)
{
	unsigned int enable;
	unsigned int bits;
	unsigned int databit;
	unsigned int val;

	/* enable chip select */
	writeb(NEO_EECS, base + NEO_EEREG);
	/* READ */
	enable = (address | 0x180);

	for (bits = 9; bits--; ) {
		databit = (enable & (1 << bits)) ? NEO_EEDI : 0;
		/* Set read address */
		writeb(databit | NEO_EECS, base + NEO_EEREG);
		writeb(databit | NEO_EECS | NEO_EECK, base + NEO_EEREG);
	}

	val = 0;

	for (bits = 17; bits--; ) {
		/* clock to EEPROM */
		writeb(NEO_EECS, base + NEO_EEREG);
		writeb(NEO_EECS | NEO_EECK, base + NEO_EEREG);
		val <<= 1;
		/* read EEPROM */
		if (readb(base + NEO_EEREG) & NEO_EEDO)
			val |= 1;
	}

	/* clock falling edge */
	writeb(NEO_EECS, base + NEO_EEREG);

	/* drop chip select */
	writeb(0x00, base + NEO_EEREG);

	return val;
}


static void neo_vpd(struct dgnc_board *brd)
{
	unsigned int i = 0;
	unsigned int a;

	if (!brd || brd->magic != DGNC_BOARD_MAGIC)
		return;

	if (!brd->re_map_membase)
		return;

	/* Store the VPD into our buffer */
	for (i = 0; i < NEO_VPD_IMAGESIZE; i++) {
		a = neo_read_eeprom(brd->re_map_membase, i);
		brd->vpd[i*2] = a & 0xff;
		brd->vpd[(i*2)+1] = (a >> 8) & 0xff;
	}

	if  (((brd->vpd[0x08] != 0x82)	   /* long resource name tag */
		&&  (brd->vpd[0x10] != 0x82))   /* long resource name tag (PCI-66 files)*/
		||  (brd->vpd[0x7F] != 0x78)) { /* small resource end tag */

		memset(brd->vpd, '\0', NEO_VPD_IMAGESIZE);
	} else {
		/* Search for the serial number */
		for (i = 0; i < NEO_VPD_IMAGEBYTES - 3; i++)
			if (brd->vpd[i] == 'S' && brd->vpd[i + 1] == 'N')
				strncpy(brd->serial_num, &(brd->vpd[i + 3]), 9);
	}
}
