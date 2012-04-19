/************************************************************************
 * Copyright 2003 Digi International (www.digi.com)
 *
 * Copyright (C) 2004 IBM Corporation. All rights reserved.
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
 * Foundation, Inc., 59 * Temple Place - Suite 330, Boston,
 * MA  02111-1307, USA.
 *
 * Contact Information:
 * Scott H Kilau <Scott_Kilau@digi.com>
 * Wendy Xiong   <wendyx@us.ibm.com>
 *
 ***********************************************************************/
#include <linux/delay.h>	/* For udelay */
#include <linux/serial_reg.h>	/* For the various UART offsets */
#include <linux/tty.h>
#include <linux/pci.h>
#include <asm/io.h>

#include "jsm.h"		/* Driver main header file */

static u32 jsm_offset_table[8] = { 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80 };

/*
 * This function allows calls to ensure that all outstanding
 * PCI writes have been completed, by doing a PCI read against
 * a non-destructive, read-only location on the Neo card.
 *
 * In this case, we are reading the DVID (Read-only Device Identification)
 * value of the Neo card.
 */
static inline void neo_pci_posting_flush(struct jsm_board *bd)
{
      readb(bd->re_map_membase + 0x8D);
}

static void neo_set_cts_flow_control(struct jsm_channel *ch)
{
	u8 ier, efr;
	ier = readb(&ch->ch_neo_uart->ier);
	efr = readb(&ch->ch_neo_uart->efr);

	jsm_printk(PARAM, INFO, &ch->ch_bd->pci_dev, "Setting CTSFLOW\n");

	/* Turn on auto CTS flow control */
	ier |= (UART_17158_IER_CTSDSR);
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
}

static void neo_set_rts_flow_control(struct jsm_channel *ch)
{
	u8 ier, efr;
	ier = readb(&ch->ch_neo_uart->ier);
	efr = readb(&ch->ch_neo_uart->efr);

	jsm_printk(PARAM, INFO, &ch->ch_bd->pci_dev, "Setting RTSFLOW\n");

	/* Turn on auto RTS flow control */
	ier |= (UART_17158_IER_RTSDTR);
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

	writeb(56, &ch->ch_neo_uart->rfifo);
	ch->ch_r_tlevel = 56;

	writeb(ier, &ch->ch_neo_uart->ier);

	/*
	 * From the Neo UART spec sheet:
	 * The auto RTS/DTR function must be started by asserting
	 * RTS/DTR# output pin (MCR bit-0 or 1 to logic 1 after
	 * it is enabled.
	 */
	ch->ch_mostat |= (UART_MCR_RTS);
}


static void neo_set_ixon_flow_control(struct jsm_channel *ch)
{
	u8 ier, efr;
	ier = readb(&ch->ch_neo_uart->ier);
	efr = readb(&ch->ch_neo_uart->efr);

	jsm_printk(PARAM, INFO, &ch->ch_bd->pci_dev, "Setting IXON FLOW\n");

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
}

static void neo_set_ixoff_flow_control(struct jsm_channel *ch)
{
	u8 ier, efr;
	ier = readb(&ch->ch_neo_uart->ier);
	efr = readb(&ch->ch_neo_uart->efr);

	jsm_printk(PARAM, INFO, &ch->ch_bd->pci_dev, "Setting IXOFF FLOW\n");

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
}

static void neo_set_no_input_flow_control(struct jsm_channel *ch)
{
	u8 ier, efr;
	ier = readb(&ch->ch_neo_uart->ier);
	efr = readb(&ch->ch_neo_uart->efr);

	jsm_printk(PARAM, INFO, &ch->ch_bd->pci_dev, "Unsetting Input FLOW\n");

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
}

static void neo_set_no_output_flow_control(struct jsm_channel *ch)
{
	u8 ier, efr;
	ier = readb(&ch->ch_neo_uart->ier);
	efr = readb(&ch->ch_neo_uart->efr);

	jsm_printk(PARAM, INFO, &ch->ch_bd->pci_dev, "Unsetting Output FLOW\n");

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
}

static inline void neo_set_new_start_stop_chars(struct jsm_channel *ch)
{

	/* if hardware flow control is set, then skip this whole thing */
	if (ch->ch_c_cflag & CRTSCTS)
		return;

	jsm_printk(PARAM, INFO, &ch->ch_bd->pci_dev, "start\n");

	/* Tell UART what start/stop chars it should be looking for */
	writeb(ch->ch_startc, &ch->ch_neo_uart->xonchar1);
	writeb(0, &ch->ch_neo_uart->xonchar2);

	writeb(ch->ch_stopc, &ch->ch_neo_uart->xoffchar1);
	writeb(0, &ch->ch_neo_uart->xoffchar2);
}

static void neo_copy_data_from_uart_to_queue(struct jsm_channel *ch)
{
	int qleft = 0;
	u8 linestatus = 0;
	u8 error_mask = 0;
	int n = 0;
	int total = 0;
	u16 head;
	u16 tail;

	if (!ch)
		return;

	/* cache head and tail of queue */
	head = ch->ch_r_head & RQUEUEMASK;
	tail = ch->ch_r_tail & RQUEUEMASK;

	/* Get our cached LSR */
	linestatus = ch->ch_cached_lsr;
	ch->ch_cached_lsr = 0;

	/* Store how much space we have left in the queue */
	if ((qleft = tail - head - 1) < 0)
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
		n = min(((u32) total), (RQUEUESIZE - (u32) head));

		/*
		 * Cut down n even further if needed, this is to fix
		 * a problem with memcpy_fromio() with the Neo on the
		 * IBM pSeries platform.
		 * 15 bytes max appears to be the magic number.
		 */
		n = min((u32) n, (u32) 12);

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
		/*
		 * Since RX_FIFO_DATA_ERROR was 0, we are guaranteed
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
		if (linestatus & error_mask) {
			u8 discard;
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
			jsm_printk(READ, INFO, &ch->ch_bd->pci_dev,
				"Queue full, dropping DATA:%x LSR:%x\n",
				ch->ch_rqueue[tail], ch->ch_equeue[tail]);

			ch->ch_r_tail = tail = (tail + 1) & RQUEUEMASK;
			ch->ch_err_overrun++;
			qleft++;
		}

		memcpy_fromio(ch->ch_rqueue + head, &ch->ch_neo_uart->txrxburst, 1);
		ch->ch_equeue[head] = (u8) linestatus;

		jsm_printk(READ, INFO, &ch->ch_bd->pci_dev,
				"DATA/LSR pair: %x %x\n", ch->ch_rqueue[head], ch->ch_equeue[head]);

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
	jsm_input(ch);
}

static void neo_copy_data_from_queue_to_uart(struct jsm_channel *ch)
{
	u16 head;
	u16 tail;
	int n;
	int s;
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
	/*
	 * If FIFOs are disabled. Send data directly to txrx register
	 */
	if (!(ch->ch_flags & CH_FIFO_ENABLED)) {
		u8 lsrbits = readb(&ch->ch_neo_uart->lsr);

		ch->ch_cached_lsr |= lsrbits;
		if (ch->ch_cached_lsr & UART_LSR_THRE) {
			ch->ch_cached_lsr &= ~(UART_LSR_THRE);

			writeb(circ->buf[circ->tail], &ch->ch_neo_uart->txrx);
			jsm_printk(WRITE, INFO, &ch->ch_bd->pci_dev,
					"Tx data: %x\n", circ->buf[circ->head]);
			circ->tail = (circ->tail + 1) & (UART_XMIT_SIZE - 1);
			ch->ch_txcount++;
		}
		return;
	}

	/*
	 * We have to do it this way, because of the EXAR TXFIFO count bug.
	 */
	if (!(ch->ch_flags & (CH_TX_FIFO_EMPTY | CH_TX_FIFO_LWM)))
		return;

	n = UART_17158_TX_FIFOSIZE - ch->ch_t_tlevel;

	/* cache head and tail of queue */
	head = circ->head & (UART_XMIT_SIZE - 1);
	tail = circ->tail & (UART_XMIT_SIZE - 1);
	qlen = uart_circ_chars_pending(circ);

	/* Find minimum of the FIFO space, versus queue length */
	n = min(n, qlen);

	while (n > 0) {

		s = ((head >= tail) ? head : UART_XMIT_SIZE) - tail;
		s = min(s, n);

		if (s <= 0)
			break;

		memcpy_toio(&ch->ch_neo_uart->txrxburst, circ->buf + tail, s);
		/* Add and flip queue if needed */
		tail = (tail + s) & (UART_XMIT_SIZE - 1);
		n -= s;
		ch->ch_txcount += s;
		len_written += s;
	}

	/* Update the final tail */
	circ->tail = tail & (UART_XMIT_SIZE - 1);

	if (len_written >= ch->ch_t_tlevel)
		ch->ch_flags &= ~(CH_TX_FIFO_EMPTY | CH_TX_FIFO_LWM);

	if (uart_circ_empty(circ))
		uart_write_wakeup(&ch->uart_port);
}

static void neo_parse_modem(struct jsm_channel *ch, u8 signals)
{
	u8 msignals = signals;

	jsm_printk(MSIGS, INFO, &ch->ch_bd->pci_dev,
			"neo_parse_modem: port: %d msignals: %x\n", ch->ch_portnum, msignals);

	/* Scrub off lower bits. They signify delta's, which I don't care about */
	/* Keep DDCD and DDSR though */
	msignals &= 0xf8;

	if (msignals & UART_MSR_DDCD)
		uart_handle_dcd_change(&ch->uart_port, msignals & UART_MSR_DCD);
	if (msignals & UART_MSR_DDSR)
		uart_handle_cts_change(&ch->uart_port, msignals & UART_MSR_CTS);
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

	jsm_printk(MSIGS, INFO, &ch->ch_bd->pci_dev,
			"Port: %d DTR: %d RTS: %d CTS: %d DSR: %d " "RI: %d CD: %d\n",
		ch->ch_portnum,
		!!((ch->ch_mistat | ch->ch_mostat) & UART_MCR_DTR),
		!!((ch->ch_mistat | ch->ch_mostat) & UART_MCR_RTS),
		!!((ch->ch_mistat | ch->ch_mostat) & UART_MSR_CTS),
		!!((ch->ch_mistat | ch->ch_mostat) & UART_MSR_DSR),
		!!((ch->ch_mistat | ch->ch_mostat) & UART_MSR_RI),
		!!((ch->ch_mistat | ch->ch_mostat) & UART_MSR_DCD));
}

/* Make the UART raise any of the output signals we want up */
static void neo_assert_modem_signals(struct jsm_channel *ch)
{
	if (!ch)
		return;

	writeb(ch->ch_mostat, &ch->ch_neo_uart->mcr);

	/* flush write operation */
	neo_pci_posting_flush(ch->ch_bd);
}

/*
 * Flush the WRITE FIFO on the Neo.
 *
 * NOTE: Channel lock MUST be held before calling this function!
 */
static void neo_flush_uart_write(struct jsm_channel *ch)
{
	u8 tmp = 0;
	int i = 0;

	if (!ch)
		return;

	writeb((UART_FCR_ENABLE_FIFO | UART_FCR_CLEAR_XMIT), &ch->ch_neo_uart->isr_fcr);

	for (i = 0; i < 10; i++) {

		/* Check to see if the UART feels it completely flushed the FIFO. */
		tmp = readb(&ch->ch_neo_uart->isr_fcr);
		if (tmp & 4) {
			jsm_printk(IOCTL, INFO, &ch->ch_bd->pci_dev,
					"Still flushing TX UART... i: %d\n", i);
			udelay(10);
		}
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
static void neo_flush_uart_read(struct jsm_channel *ch)
{
	u8 tmp = 0;
	int i = 0;

	if (!ch)
		return;

	writeb((UART_FCR_ENABLE_FIFO | UART_FCR_CLEAR_RCVR), &ch->ch_neo_uart->isr_fcr);

	for (i = 0; i < 10; i++) {

		/* Check to see if the UART feels it completely flushed the FIFO. */
		tmp = readb(&ch->ch_neo_uart->isr_fcr);
		if (tmp & 2) {
			jsm_printk(IOCTL, INFO, &ch->ch_bd->pci_dev,
					"Still flushing RX UART... i: %d\n", i);
			udelay(10);
		}
		else
			break;
	}
}

/*
 * No locks are assumed to be held when calling this function.
 */
static void neo_clear_break(struct jsm_channel *ch, int force)
{
	unsigned long lock_flags;

	spin_lock_irqsave(&ch->ch_lock, lock_flags);

	/* Turn break off, and unset some variables */
	if (ch->ch_flags & CH_BREAK_SENDING) {
		u8 temp = readb(&ch->ch_neo_uart->lcr);
		writeb((temp & ~UART_LCR_SBC), &ch->ch_neo_uart->lcr);

		ch->ch_flags &= ~(CH_BREAK_SENDING);
		jsm_printk(IOCTL, INFO, &ch->ch_bd->pci_dev,
				"clear break Finishing UART_LCR_SBC! finished: %lx\n", jiffies);

		/* flush write operation */
		neo_pci_posting_flush(ch->ch_bd);
	}
	spin_unlock_irqrestore(&ch->ch_lock, lock_flags);
}

/*
 * Parse the ISR register.
 */
static inline void neo_parse_isr(struct jsm_board *brd, u32 port)
{
	struct jsm_channel *ch;
	u8 isr;
	u8 cause;
	unsigned long lock_flags;

	if (!brd)
		return;

	if (port > brd->maxports)
		return;

	ch = brd->channels[port];
	if (!ch)
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

		jsm_printk(INTR, INFO, &ch->ch_bd->pci_dev,
				"%s:%d isr: %x\n", __FILE__, __LINE__, isr);

		if (isr & (UART_17158_IIR_RDI_TIMEOUT | UART_IIR_RDI)) {
			/* Read data from uart -> queue */
			neo_copy_data_from_uart_to_queue(ch);

			/* Call our tty layer to enforce queue flow control if needed. */
			spin_lock_irqsave(&ch->ch_lock, lock_flags);
			jsm_check_queue_flow_control(ch);
			spin_unlock_irqrestore(&ch->ch_lock, lock_flags);
		}

		if (isr & UART_IIR_THRI) {
			/* Transfer data (if any) from Write Queue -> UART. */
			spin_lock_irqsave(&ch->ch_lock, lock_flags);
			ch->ch_flags |= (CH_TX_FIFO_EMPTY | CH_TX_FIFO_LWM);
			spin_unlock_irqrestore(&ch->ch_lock, lock_flags);
			neo_copy_data_from_queue_to_uart(ch);
		}

		if (isr & UART_17158_IIR_XONXOFF) {
			cause = readb(&ch->ch_neo_uart->xoffchar1);

			jsm_printk(INTR, INFO, &ch->ch_bd->pci_dev,
					"Port %d. Got ISR_XONXOFF: cause:%x\n", port, cause);

			/*
			 * Since the UART detected either an XON or
			 * XOFF match, we need to figure out which
			 * one it was, so we can suspend or resume data flow.
			 */
			spin_lock_irqsave(&ch->ch_lock, lock_flags);
			if (cause == UART_17158_XON_DETECT) {
				/* Is output stopped right now, if so, resume it */
				if (brd->channels[port]->ch_flags & CH_STOP) {
					ch->ch_flags &= ~(CH_STOP);
				}
				jsm_printk(INTR, INFO, &ch->ch_bd->pci_dev,
						"Port %d. XON detected in incoming data\n", port);
			}
			else if (cause == UART_17158_XOFF_DETECT) {
				if (!(brd->channels[port]->ch_flags & CH_STOP)) {
					ch->ch_flags |= CH_STOP;
					jsm_printk(INTR, INFO, &ch->ch_bd->pci_dev,
							"Setting CH_STOP\n");
				}
				jsm_printk(INTR, INFO, &ch->ch_bd->pci_dev,
						"Port: %d. XOFF detected in incoming data\n", port);
			}
			spin_unlock_irqrestore(&ch->ch_lock, lock_flags);
		}

		if (isr & UART_17158_IIR_HWFLOW_STATE_CHANGE) {
			/*
			 * If we get here, this means the hardware is doing auto flow control.
			 * Check to see whether RTS/DTR or CTS/DSR caused this interrupt.
			 */
			cause = readb(&ch->ch_neo_uart->mcr);

			/* Which pin is doing auto flow? RTS or DTR? */
			spin_lock_irqsave(&ch->ch_lock, lock_flags);
			if ((cause & 0x4) == 0) {
				if (cause & UART_MCR_RTS)
					ch->ch_mostat |= UART_MCR_RTS;
				else
					ch->ch_mostat &= ~(UART_MCR_RTS);
			} else {
				if (cause & UART_MCR_DTR)
					ch->ch_mostat |= UART_MCR_DTR;
				else
					ch->ch_mostat &= ~(UART_MCR_DTR);
			}
			spin_unlock_irqrestore(&ch->ch_lock, lock_flags);
		}

		/* Parse any modem signal changes */
		jsm_printk(INTR, INFO, &ch->ch_bd->pci_dev,
				"MOD_STAT: sending to parse_modem_sigs\n");
		neo_parse_modem(ch, readb(&ch->ch_neo_uart->msr));
	}
}

static inline void neo_parse_lsr(struct jsm_board *brd, u32 port)
{
	struct jsm_channel *ch;
	int linestatus;
	unsigned long lock_flags;

	if (!brd)
		return;

	if (port > brd->maxports)
		return;

	ch = brd->channels[port];
	if (!ch)
		return;

	linestatus = readb(&ch->ch_neo_uart->lsr);

	jsm_printk(INTR, INFO, &ch->ch_bd->pci_dev,
			"%s:%d port: %d linestatus: %x\n", __FILE__, __LINE__, port, linestatus);

	ch->ch_cached_lsr |= linestatus;

	if (ch->ch_cached_lsr & UART_LSR_DR) {
		/* Read data from uart -> queue */
		neo_copy_data_from_uart_to_queue(ch);
		spin_lock_irqsave(&ch->ch_lock, lock_flags);
		jsm_check_queue_flow_control(ch);
		spin_unlock_irqrestore(&ch->ch_lock, lock_flags);
	}

	/*
	 * This is a special flag. It indicates that at least 1
	 * RX error (parity, framing, or break) has happened.
	 * Mark this in our struct, which will tell me that I have
	 *to do the special RX+LSR read for this FIFO load.
	 */
	if (linestatus & UART_17158_RX_FIFO_DATA_ERROR)
		jsm_printk(INTR, DEBUG, &ch->ch_bd->pci_dev,
			"%s:%d Port: %d Got an RX error, need to parse LSR\n",
			__FILE__, __LINE__, port);

	/*
	 * The next 3 tests should *NOT* happen, as the above test
	 * should encapsulate all 3... At least, thats what Exar says.
	 */

	if (linestatus & UART_LSR_PE) {
		ch->ch_err_parity++;
		jsm_printk(INTR, DEBUG, &ch->ch_bd->pci_dev,
			"%s:%d Port: %d. PAR ERR!\n", __FILE__, __LINE__, port);
	}

	if (linestatus & UART_LSR_FE) {
		ch->ch_err_frame++;
		jsm_printk(INTR, DEBUG, &ch->ch_bd->pci_dev,
			"%s:%d Port: %d. FRM ERR!\n", __FILE__, __LINE__, port);
	}

	if (linestatus & UART_LSR_BI) {
		ch->ch_err_break++;
		jsm_printk(INTR, DEBUG, &ch->ch_bd->pci_dev,
			"%s:%d Port: %d. BRK INTR!\n", __FILE__, __LINE__, port);
	}

	if (linestatus & UART_LSR_OE) {
		/*
		 * Rx Oruns. Exar says that an orun will NOT corrupt
		 * the FIFO. It will just replace the holding register
		 * with this new data byte. So basically just ignore this.
		 * Probably we should eventually have an orun stat in our driver...
		 */
		ch->ch_err_overrun++;
		jsm_printk(INTR, DEBUG, &ch->ch_bd->pci_dev,
			"%s:%d Port: %d. Rx Overrun!\n", __FILE__, __LINE__, port);
	}

	if (linestatus & UART_LSR_THRE) {
		spin_lock_irqsave(&ch->ch_lock, lock_flags);
		ch->ch_flags |= (CH_TX_FIFO_EMPTY | CH_TX_FIFO_LWM);
		spin_unlock_irqrestore(&ch->ch_lock, lock_flags);

		/* Transfer data (if any) from Write Queue -> UART. */
		neo_copy_data_from_queue_to_uart(ch);
	}
	else if (linestatus & UART_17158_TX_AND_FIFO_CLR) {
		spin_lock_irqsave(&ch->ch_lock, lock_flags);
		ch->ch_flags |= (CH_TX_FIFO_EMPTY | CH_TX_FIFO_LWM);
		spin_unlock_irqrestore(&ch->ch_lock, lock_flags);

		/* Transfer data (if any) from Write Queue -> UART. */
		neo_copy_data_from_queue_to_uart(ch);
	}
}

/*
 * neo_param()
 * Send any/all changes to the line to the UART.
 */
static void neo_param(struct jsm_channel *ch)
{
	u8 lcr = 0;
	u8 uart_lcr, ier;
	u32 baud;
	int quot;
	struct jsm_board *bd;

	bd = ch->ch_bd;
	if (!bd)
		return;

	/*
	 * If baud rate is zero, flush queues, and set mval to drop DTR.
	 */
	if ((ch->ch_c_cflag & (CBAUD)) == 0) {
		ch->ch_r_head = ch->ch_r_tail = 0;
		ch->ch_e_head = ch->ch_e_tail = 0;

		neo_flush_uart_write(ch);
		neo_flush_uart_read(ch);

		ch->ch_flags |= (CH_BAUD0);
		ch->ch_mostat &= ~(UART_MCR_RTS | UART_MCR_DTR);
		neo_assert_modem_signals(ch);
		return;

	} else {
		int i;
		unsigned int cflag;
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

	ier = readb(&ch->ch_neo_uart->ier);
	uart_lcr = readb(&ch->ch_neo_uart->lcr);

	if (baud == 0)
		baud = 9600;

	quot = ch->ch_bd->bd_dividend / baud;

	if (quot != 0) {
		writeb(UART_LCR_DLAB, &ch->ch_neo_uart->lcr);
		writeb((quot & 0xff), &ch->ch_neo_uart->txrx);
		writeb((quot >> 8), &ch->ch_neo_uart->ier);
		writeb(lcr, &ch->ch_neo_uart->lcr);
	}

	if (uart_lcr != lcr)
		writeb(lcr, &ch->ch_neo_uart->lcr);

	if (ch->ch_c_cflag & CREAD)
		ier |= (UART_IER_RDI | UART_IER_RLSI);

	ier |= (UART_IER_THRI | UART_IER_MSI);

	writeb(ier, &ch->ch_neo_uart->ier);

	/* Set new start/stop chars */
	neo_set_new_start_stop_chars(ch);

	if (ch->ch_c_cflag & CRTSCTS)
		neo_set_cts_flow_control(ch);
	else if (ch->ch_c_iflag & IXON) {
		/* If start/stop is set to disable, then we should disable flow control */
		if ((ch->ch_startc == __DISABLED_CHAR) || (ch->ch_stopc == __DISABLED_CHAR))
			neo_set_no_output_flow_control(ch);
		else
			neo_set_ixon_flow_control(ch);
	}
	else
		neo_set_no_output_flow_control(ch);

	if (ch->ch_c_cflag & CRTSCTS)
		neo_set_rts_flow_control(ch);
	else if (ch->ch_c_iflag & IXOFF) {
		/* If start/stop is set to disable, then we should disable flow control */
		if ((ch->ch_startc == __DISABLED_CHAR) || (ch->ch_stopc == __DISABLED_CHAR))
			neo_set_no_input_flow_control(ch);
		else
			neo_set_ixoff_flow_control(ch);
	}
	else
		neo_set_no_input_flow_control(ch);
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
	return;
}

/*
 * jsm_neo_intr()
 *
 * Neo specific interrupt handler.
 */
static irqreturn_t neo_intr(int irq, void *voidbrd)
{
	struct jsm_board *brd = voidbrd;
	struct jsm_channel *ch;
	int port = 0;
	int type = 0;
	int current_port;
	u32 tmp;
	u32 uart_poll;
	unsigned long lock_flags;
	unsigned long lock_flags2;
	int outofloop_count = 0;

	/* Lock out the slow poller from running on this board. */
	spin_lock_irqsave(&brd->bd_intr_lock, lock_flags);

	/*
	 * Read in "extended" IRQ information from the 32bit Neo register.
	 * Bits 0-7: What port triggered the interrupt.
	 * Bits 8-31: Each 3bits indicate what type of interrupt occurred.
	 */
	uart_poll = readl(brd->re_map_membase + UART_17158_POLL_ADDR_OFFSET);

	jsm_printk(INTR, INFO, &brd->pci_dev,
		"%s:%d uart_poll: %x\n", __FILE__, __LINE__, uart_poll);

	if (!uart_poll) {
		jsm_printk(INTR, INFO, &brd->pci_dev,
			"Kernel interrupted to me, but no pending interrupts...\n");
		spin_unlock_irqrestore(&brd->bd_intr_lock, lock_flags);
		return IRQ_NONE;
	}

	/* At this point, we have at least SOMETHING to service, dig further... */

	current_port = 0;

	/* Loop on each port */
	while (((uart_poll & 0xff) != 0) && (outofloop_count < 0xff)){

		tmp = uart_poll;
		outofloop_count++;

		/* Check current port to see if it has interrupt pending */
		if ((tmp & jsm_offset_table[current_port]) != 0) {
			port = current_port;
			type = tmp >> (8 + (port * 3));
			type &= 0x7;
		} else {
			current_port++;
			continue;
		}

		jsm_printk(INTR, INFO, &brd->pci_dev,
		"%s:%d port: %x type: %x\n", __FILE__, __LINE__, port, type);

		/* Remove this port + type from uart_poll */
		uart_poll &= ~(jsm_offset_table[port]);

		if (!type) {
			/* If no type, just ignore it, and move onto next port */
			jsm_printk(INTR, ERR, &brd->pci_dev,
				"Interrupt with no type! port: %d\n", port);
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
			spin_lock_irqsave(&ch->ch_lock, lock_flags2);
			jsm_check_queue_flow_control(ch);
			spin_unlock_irqrestore(&ch->ch_lock, lock_flags2);

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
			jsm_printk(INTR, ERR, &brd->pci_dev,
				"%s:%d Unknown Interrupt type: %x\n", __FILE__, __LINE__, type);
			continue;
		}
	}

	spin_unlock_irqrestore(&brd->bd_intr_lock, lock_flags);

	jsm_printk(INTR, INFO, &brd->pci_dev, "finish.\n");
	return IRQ_HANDLED;
}

/*
 * Neo specific way of turning off the receiver.
 * Used as a way to enforce queue flow control when in
 * hardware flow control mode.
 */
static void neo_disable_receiver(struct jsm_channel *ch)
{
	u8 tmp = readb(&ch->ch_neo_uart->ier);
	tmp &= ~(UART_IER_RDI);
	writeb(tmp, &ch->ch_neo_uart->ier);

	/* flush write operation */
	neo_pci_posting_flush(ch->ch_bd);
}


/*
 * Neo specific way of turning on the receiver.
 * Used as a way to un-enforce queue flow control when in
 * hardware flow control mode.
 */
static void neo_enable_receiver(struct jsm_channel *ch)
{
	u8 tmp = readb(&ch->ch_neo_uart->ier);
	tmp |= (UART_IER_RDI);
	writeb(tmp, &ch->ch_neo_uart->ier);

	/* flush write operation */
	neo_pci_posting_flush(ch->ch_bd);
}

static void neo_send_start_character(struct jsm_channel *ch)
{
	if (!ch)
		return;

	if (ch->ch_startc != __DISABLED_CHAR) {
		ch->ch_xon_sends++;
		writeb(ch->ch_startc, &ch->ch_neo_uart->txrx);

		/* flush write operation */
		neo_pci_posting_flush(ch->ch_bd);
	}
}

static void neo_send_stop_character(struct jsm_channel *ch)
{
	if (!ch)
		return;

	if (ch->ch_stopc != __DISABLED_CHAR) {
		ch->ch_xoff_sends++;
		writeb(ch->ch_stopc, &ch->ch_neo_uart->txrx);

		/* flush write operation */
		neo_pci_posting_flush(ch->ch_bd);
	}
}

/*
 * neo_uart_init
 */
static void neo_uart_init(struct jsm_channel *ch)
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
}

/*
 * Make the UART completely turn off.
 */
static void neo_uart_off(struct jsm_channel *ch)
{
	/* Turn off UART enhanced bits */
	writeb(0, &ch->ch_neo_uart->efr);

	/* Stop all interrupts from occurring. */
	writeb(0, &ch->ch_neo_uart->ier);
}

static u32 neo_get_uart_bytes_left(struct jsm_channel *ch)
{
	u8 left = 0;
	u8 lsr = readb(&ch->ch_neo_uart->lsr);

	/* We must cache the LSR as some of the bits get reset once read... */
	ch->ch_cached_lsr |= lsr;

	/* Determine whether the Transmitter is empty or not */
	if (!(lsr & UART_LSR_TEMT))
		left = 1;
	else {
		ch->ch_flags |= (CH_TX_FIFO_EMPTY | CH_TX_FIFO_LWM);
		left = 0;
	}

	return left;
}

/* Channel lock MUST be held by the calling function! */
static void neo_send_break(struct jsm_channel *ch)
{
	/*
	 * Set the time we should stop sending the break.
	 * If we are already sending a break, toss away the existing
	 * time to stop, and use this new value instead.
	 */

	/* Tell the UART to start sending the break */
	if (!(ch->ch_flags & CH_BREAK_SENDING)) {
		u8 temp = readb(&ch->ch_neo_uart->lcr);
		writeb((temp | UART_LCR_SBC), &ch->ch_neo_uart->lcr);
		ch->ch_flags |= (CH_BREAK_SENDING);

		/* flush write operation */
		neo_pci_posting_flush(ch->ch_bd);
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
static void neo_send_immediate_char(struct jsm_channel *ch, unsigned char c)
{
	if (!ch)
		return;

	writeb(c, &ch->ch_neo_uart->txrx);

	/* flush write operation */
	neo_pci_posting_flush(ch->ch_bd);
}

struct board_ops jsm_neo_ops = {
	.intr				= neo_intr,
	.uart_init			= neo_uart_init,
	.uart_off			= neo_uart_off,
	.param				= neo_param,
	.assert_modem_signals		= neo_assert_modem_signals,
	.flush_uart_write		= neo_flush_uart_write,
	.flush_uart_read		= neo_flush_uart_read,
	.disable_receiver		= neo_disable_receiver,
	.enable_receiver		= neo_enable_receiver,
	.send_break			= neo_send_break,
	.clear_break			= neo_clear_break,
	.send_start_character		= neo_send_start_character,
	.send_stop_character		= neo_send_stop_character,
	.copy_data_from_queue_to_uart	= neo_copy_data_from_queue_to_uart,
	.get_uart_bytes_left		= neo_get_uart_bytes_left,
	.send_immediate_char		= neo_send_immediate_char
};
