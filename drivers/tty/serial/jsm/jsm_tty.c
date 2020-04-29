// SPDX-License-Identifier: GPL-2.0+
/************************************************************************
 * Copyright 2003 Digi International (www.digi.com)
 *
 * Copyright (C) 2004 IBM Corporation. All rights reserved.
 *
 * Contact Information:
 * Scott H Kilau <Scott_Kilau@digi.com>
 * Ananda Venkatarman <mansarov@us.ibm.com>
 * Modifications:
 * 01/19/06:	changed jsm_input routine to use the dynamically allocated
 *		tty_buffer changes. Contributors: Scott Kilau and Ananda V.
 ***********************************************************************/
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/serial_reg.h>
#include <linux/delay.h>	/* For udelay */
#include <linux/pci.h>
#include <linux/slab.h>

#include "jsm.h"

static DECLARE_BITMAP(linemap, MAXLINES);

static void jsm_carrier(struct jsm_channel *ch);

static inline int jsm_get_mstat(struct jsm_channel *ch)
{
	unsigned char mstat;
	int result;

	jsm_dbg(IOCTL, &ch->ch_bd->pci_dev, "start\n");

	mstat = (ch->ch_mostat | ch->ch_mistat);

	result = 0;

	if (mstat & UART_MCR_DTR)
		result |= TIOCM_DTR;
	if (mstat & UART_MCR_RTS)
		result |= TIOCM_RTS;
	if (mstat & UART_MSR_CTS)
		result |= TIOCM_CTS;
	if (mstat & UART_MSR_DSR)
		result |= TIOCM_DSR;
	if (mstat & UART_MSR_RI)
		result |= TIOCM_RI;
	if (mstat & UART_MSR_DCD)
		result |= TIOCM_CD;

	jsm_dbg(IOCTL, &ch->ch_bd->pci_dev, "finish\n");
	return result;
}

static unsigned int jsm_tty_tx_empty(struct uart_port *port)
{
	return TIOCSER_TEMT;
}

/*
 * Return modem signals to ld.
 */
static unsigned int jsm_tty_get_mctrl(struct uart_port *port)
{
	int result;
	struct jsm_channel *channel =
		container_of(port, struct jsm_channel, uart_port);

	jsm_dbg(IOCTL, &channel->ch_bd->pci_dev, "start\n");

	result = jsm_get_mstat(channel);

	if (result < 0)
		return -ENXIO;

	jsm_dbg(IOCTL, &channel->ch_bd->pci_dev, "finish\n");

	return result;
}

/*
 * jsm_set_modem_info()
 *
 * Set modem signals, called by ld.
 */
static void jsm_tty_set_mctrl(struct uart_port *port, unsigned int mctrl)
{
	struct jsm_channel *channel =
		container_of(port, struct jsm_channel, uart_port);

	jsm_dbg(IOCTL, &channel->ch_bd->pci_dev, "start\n");

	if (mctrl & TIOCM_RTS)
		channel->ch_mostat |= UART_MCR_RTS;
	else
		channel->ch_mostat &= ~UART_MCR_RTS;

	if (mctrl & TIOCM_DTR)
		channel->ch_mostat |= UART_MCR_DTR;
	else
		channel->ch_mostat &= ~UART_MCR_DTR;

	channel->ch_bd->bd_ops->assert_modem_signals(channel);

	jsm_dbg(IOCTL, &channel->ch_bd->pci_dev, "finish\n");
	udelay(10);
}

/*
 * jsm_tty_write()
 *
 * Take data from the user or kernel and send it out to the FEP.
 * In here exists all the Transparent Print magic as well.
 */
static void jsm_tty_write(struct uart_port *port)
{
	struct jsm_channel *channel;

	channel = container_of(port, struct jsm_channel, uart_port);
	channel->ch_bd->bd_ops->copy_data_from_queue_to_uart(channel);
}

static void jsm_tty_start_tx(struct uart_port *port)
{
	struct jsm_channel *channel =
		container_of(port, struct jsm_channel, uart_port);

	jsm_dbg(IOCTL, &channel->ch_bd->pci_dev, "start\n");

	channel->ch_flags &= ~(CH_STOP);
	jsm_tty_write(port);

	jsm_dbg(IOCTL, &channel->ch_bd->pci_dev, "finish\n");
}

static void jsm_tty_stop_tx(struct uart_port *port)
{
	struct jsm_channel *channel =
		container_of(port, struct jsm_channel, uart_port);

	jsm_dbg(IOCTL, &channel->ch_bd->pci_dev, "start\n");

	channel->ch_flags |= (CH_STOP);

	jsm_dbg(IOCTL, &channel->ch_bd->pci_dev, "finish\n");
}

static void jsm_tty_send_xchar(struct uart_port *port, char ch)
{
	unsigned long lock_flags;
	struct jsm_channel *channel =
		container_of(port, struct jsm_channel, uart_port);
	struct ktermios *termios;

	spin_lock_irqsave(&port->lock, lock_flags);
	termios = &port->state->port.tty->termios;
	if (ch == termios->c_cc[VSTART])
		channel->ch_bd->bd_ops->send_start_character(channel);

	if (ch == termios->c_cc[VSTOP])
		channel->ch_bd->bd_ops->send_stop_character(channel);
	spin_unlock_irqrestore(&port->lock, lock_flags);
}

static void jsm_tty_stop_rx(struct uart_port *port)
{
	struct jsm_channel *channel =
		container_of(port, struct jsm_channel, uart_port);

	channel->ch_bd->bd_ops->disable_receiver(channel);
}

static void jsm_tty_break(struct uart_port *port, int break_state)
{
	unsigned long lock_flags;
	struct jsm_channel *channel =
		container_of(port, struct jsm_channel, uart_port);

	spin_lock_irqsave(&port->lock, lock_flags);
	if (break_state == -1)
		channel->ch_bd->bd_ops->send_break(channel);
	else
		channel->ch_bd->bd_ops->clear_break(channel);

	spin_unlock_irqrestore(&port->lock, lock_flags);
}

static int jsm_tty_open(struct uart_port *port)
{
	struct jsm_board *brd;
	struct jsm_channel *channel =
		container_of(port, struct jsm_channel, uart_port);
	struct ktermios *termios;

	/* Get board pointer from our array of majors we have allocated */
	brd = channel->ch_bd;

	/*
	 * Allocate channel buffers for read/write/error.
	 * Set flag, so we don't get trounced on.
	 */
	channel->ch_flags |= (CH_OPENING);

	/* Drop locks, as malloc with GFP_KERNEL can sleep */

	if (!channel->ch_rqueue) {
		channel->ch_rqueue = kzalloc(RQUEUESIZE, GFP_KERNEL);
		if (!channel->ch_rqueue) {
			jsm_dbg(INIT, &channel->ch_bd->pci_dev,
				"unable to allocate read queue buf\n");
			return -ENOMEM;
		}
	}
	if (!channel->ch_equeue) {
		channel->ch_equeue = kzalloc(EQUEUESIZE, GFP_KERNEL);
		if (!channel->ch_equeue) {
			jsm_dbg(INIT, &channel->ch_bd->pci_dev,
				"unable to allocate error queue buf\n");
			return -ENOMEM;
		}
	}

	channel->ch_flags &= ~(CH_OPENING);
	/*
	 * Initialize if neither terminal is open.
	 */
	jsm_dbg(OPEN, &channel->ch_bd->pci_dev,
		"jsm_open: initializing channel in open...\n");

	/*
	 * Flush input queues.
	 */
	channel->ch_r_head = channel->ch_r_tail = 0;
	channel->ch_e_head = channel->ch_e_tail = 0;

	brd->bd_ops->flush_uart_write(channel);
	brd->bd_ops->flush_uart_read(channel);

	channel->ch_flags = 0;
	channel->ch_cached_lsr = 0;
	channel->ch_stops_sent = 0;

	termios = &port->state->port.tty->termios;
	channel->ch_c_cflag	= termios->c_cflag;
	channel->ch_c_iflag	= termios->c_iflag;
	channel->ch_c_oflag	= termios->c_oflag;
	channel->ch_c_lflag	= termios->c_lflag;
	channel->ch_startc	= termios->c_cc[VSTART];
	channel->ch_stopc	= termios->c_cc[VSTOP];

	/* Tell UART to init itself */
	brd->bd_ops->uart_init(channel);

	/*
	 * Run param in case we changed anything
	 */
	brd->bd_ops->param(channel);

	jsm_carrier(channel);

	channel->ch_open_count++;

	jsm_dbg(OPEN, &channel->ch_bd->pci_dev, "finish\n");
	return 0;
}

static void jsm_tty_close(struct uart_port *port)
{
	struct jsm_board *bd;
	struct jsm_channel *channel =
		container_of(port, struct jsm_channel, uart_port);

	jsm_dbg(CLOSE, &channel->ch_bd->pci_dev, "start\n");

	bd = channel->ch_bd;

	channel->ch_flags &= ~(CH_STOPI);

	channel->ch_open_count--;

	/*
	 * If we have HUPCL set, lower DTR and RTS
	 */
	if (channel->ch_c_cflag & HUPCL) {
		jsm_dbg(CLOSE, &channel->ch_bd->pci_dev,
			"Close. HUPCL set, dropping DTR/RTS\n");

		/* Drop RTS/DTR */
		channel->ch_mostat &= ~(UART_MCR_DTR | UART_MCR_RTS);
		bd->bd_ops->assert_modem_signals(channel);
	}

	/* Turn off UART interrupts for this port */
	channel->ch_bd->bd_ops->uart_off(channel);

	jsm_dbg(CLOSE, &channel->ch_bd->pci_dev, "finish\n");
}

static void jsm_tty_set_termios(struct uart_port *port,
				 struct ktermios *termios,
				 struct ktermios *old_termios)
{
	unsigned long lock_flags;
	struct jsm_channel *channel =
		container_of(port, struct jsm_channel, uart_port);

	spin_lock_irqsave(&port->lock, lock_flags);
	channel->ch_c_cflag	= termios->c_cflag;
	channel->ch_c_iflag	= termios->c_iflag;
	channel->ch_c_oflag	= termios->c_oflag;
	channel->ch_c_lflag	= termios->c_lflag;
	channel->ch_startc	= termios->c_cc[VSTART];
	channel->ch_stopc	= termios->c_cc[VSTOP];

	channel->ch_bd->bd_ops->param(channel);
	jsm_carrier(channel);
	spin_unlock_irqrestore(&port->lock, lock_flags);
}

static const char *jsm_tty_type(struct uart_port *port)
{
	return "jsm";
}

static void jsm_tty_release_port(struct uart_port *port)
{
}

static int jsm_tty_request_port(struct uart_port *port)
{
	return 0;
}

static void jsm_config_port(struct uart_port *port, int flags)
{
	port->type = PORT_JSM;
}

static const struct uart_ops jsm_ops = {
	.tx_empty	= jsm_tty_tx_empty,
	.set_mctrl	= jsm_tty_set_mctrl,
	.get_mctrl	= jsm_tty_get_mctrl,
	.stop_tx	= jsm_tty_stop_tx,
	.start_tx	= jsm_tty_start_tx,
	.send_xchar	= jsm_tty_send_xchar,
	.stop_rx	= jsm_tty_stop_rx,
	.break_ctl	= jsm_tty_break,
	.startup	= jsm_tty_open,
	.shutdown	= jsm_tty_close,
	.set_termios	= jsm_tty_set_termios,
	.type		= jsm_tty_type,
	.release_port	= jsm_tty_release_port,
	.request_port	= jsm_tty_request_port,
	.config_port	= jsm_config_port,
};

/*
 * jsm_tty_init()
 *
 * Init the tty subsystem.  Called once per board after board has been
 * downloaded and init'ed.
 */
int jsm_tty_init(struct jsm_board *brd)
{
	int i;
	void __iomem *vaddr;
	struct jsm_channel *ch;

	if (!brd)
		return -ENXIO;

	jsm_dbg(INIT, &brd->pci_dev, "start\n");

	/*
	 * Initialize board structure elements.
	 */

	brd->nasync = brd->maxports;

	/*
	 * Allocate channel memory that might not have been allocated
	 * when the driver was first loaded.
	 */
	for (i = 0; i < brd->nasync; i++) {
		if (!brd->channels[i]) {

			/*
			 * Okay to malloc with GFP_KERNEL, we are not at
			 * interrupt context, and there are no locks held.
			 */
			brd->channels[i] = kzalloc(sizeof(struct jsm_channel), GFP_KERNEL);
			if (!brd->channels[i]) {
				jsm_dbg(CORE, &brd->pci_dev,
					"%s:%d Unable to allocate memory for channel struct\n",
					__FILE__, __LINE__);
			}
		}
	}

	ch = brd->channels[0];
	vaddr = brd->re_map_membase;

	/* Set up channel variables */
	for (i = 0; i < brd->nasync; i++, ch = brd->channels[i]) {

		if (!brd->channels[i])
			continue;

		spin_lock_init(&ch->ch_lock);

		if (brd->bd_uart_offset == 0x200)
			ch->ch_neo_uart =  vaddr + (brd->bd_uart_offset * i);
		else
			ch->ch_cls_uart =  vaddr + (brd->bd_uart_offset * i);

		ch->ch_bd = brd;
		ch->ch_portnum = i;

		/* .25 second delay */
		ch->ch_close_delay = 250;

		init_waitqueue_head(&ch->ch_flags_wait);
	}

	jsm_dbg(INIT, &brd->pci_dev, "finish\n");
	return 0;
}

int jsm_uart_port_init(struct jsm_board *brd)
{
	int i, rc;
	unsigned int line;

	if (!brd)
		return -ENXIO;

	jsm_dbg(INIT, &brd->pci_dev, "start\n");

	/*
	 * Initialize board structure elements.
	 */

	brd->nasync = brd->maxports;

	/* Set up channel variables */
	for (i = 0; i < brd->nasync; i++) {

		if (!brd->channels[i])
			continue;

		brd->channels[i]->uart_port.irq = brd->irq;
		brd->channels[i]->uart_port.uartclk = 14745600;
		brd->channels[i]->uart_port.type = PORT_JSM;
		brd->channels[i]->uart_port.iotype = UPIO_MEM;
		brd->channels[i]->uart_port.membase = brd->re_map_membase;
		brd->channels[i]->uart_port.fifosize = 16;
		brd->channels[i]->uart_port.ops = &jsm_ops;
		line = find_first_zero_bit(linemap, MAXLINES);
		if (line >= MAXLINES) {
			printk(KERN_INFO "jsm: linemap is full, added device failed\n");
			continue;
		} else
			set_bit(line, linemap);
		brd->channels[i]->uart_port.line = line;
		rc = uart_add_one_port(&jsm_uart_driver, &brd->channels[i]->uart_port);
		if (rc) {
			printk(KERN_INFO "jsm: Port %d failed. Aborting...\n", i);
			return rc;
		} else
			printk(KERN_INFO "jsm: Port %d added\n", i);
	}

	jsm_dbg(INIT, &brd->pci_dev, "finish\n");
	return 0;
}

int jsm_remove_uart_port(struct jsm_board *brd)
{
	int i;
	struct jsm_channel *ch;

	if (!brd)
		return -ENXIO;

	jsm_dbg(INIT, &brd->pci_dev, "start\n");

	/*
	 * Initialize board structure elements.
	 */

	brd->nasync = brd->maxports;

	/* Set up channel variables */
	for (i = 0; i < brd->nasync; i++) {

		if (!brd->channels[i])
			continue;

		ch = brd->channels[i];

		clear_bit(ch->uart_port.line, linemap);
		uart_remove_one_port(&jsm_uart_driver, &brd->channels[i]->uart_port);
	}

	jsm_dbg(INIT, &brd->pci_dev, "finish\n");
	return 0;
}

void jsm_input(struct jsm_channel *ch)
{
	struct jsm_board *bd;
	struct tty_struct *tp;
	struct tty_port *port;
	u32 rmask;
	u16 head;
	u16 tail;
	int data_len;
	unsigned long lock_flags;
	int len = 0;
	int s = 0;
	int i = 0;

	jsm_dbg(READ, &ch->ch_bd->pci_dev, "start\n");

	port = &ch->uart_port.state->port;
	tp = port->tty;

	bd = ch->ch_bd;
	if (!bd)
		return;

	spin_lock_irqsave(&ch->ch_lock, lock_flags);

	/*
	 *Figure the number of characters in the buffer.
	 *Exit immediately if none.
	 */

	rmask = RQUEUEMASK;

	head = ch->ch_r_head & rmask;
	tail = ch->ch_r_tail & rmask;

	data_len = (head - tail) & rmask;
	if (data_len == 0) {
		spin_unlock_irqrestore(&ch->ch_lock, lock_flags);
		return;
	}

	jsm_dbg(READ, &ch->ch_bd->pci_dev, "start\n");

	/*
	 *If the device is not open, or CREAD is off, flush
	 *input data and return immediately.
	 */
	if (!tp || !C_CREAD(tp)) {

		jsm_dbg(READ, &ch->ch_bd->pci_dev,
			"input. dropping %d bytes on port %d...\n",
			data_len, ch->ch_portnum);
		ch->ch_r_head = tail;

		/* Force queue flow control to be released, if needed */
		jsm_check_queue_flow_control(ch);

		spin_unlock_irqrestore(&ch->ch_lock, lock_flags);
		return;
	}

	/*
	 * If we are throttled, simply don't read any data.
	 */
	if (ch->ch_flags & CH_STOPI) {
		spin_unlock_irqrestore(&ch->ch_lock, lock_flags);
		jsm_dbg(READ, &ch->ch_bd->pci_dev,
			"Port %d throttled, not reading any data. head: %x tail: %x\n",
			ch->ch_portnum, head, tail);
		return;
	}

	jsm_dbg(READ, &ch->ch_bd->pci_dev, "start 2\n");

	len = tty_buffer_request_room(port, data_len);

	/*
	 * len now contains the most amount of data we can copy,
	 * bounded either by the flip buffer size or the amount
	 * of data the card actually has pending...
	 */
	while (len) {
		s = ((head >= tail) ? head : RQUEUESIZE) - tail;
		s = min(s, len);

		if (s <= 0)
			break;

			/*
			 * If conditions are such that ld needs to see all
			 * UART errors, we will have to walk each character
			 * and error byte and send them to the buffer one at
			 * a time.
			 */

		if (I_PARMRK(tp) || I_BRKINT(tp) || I_INPCK(tp)) {
			for (i = 0; i < s; i++) {
				/*
				 * Give the Linux ld the flags in the
				 * format it likes.
				 */
				if (*(ch->ch_equeue +tail +i) & UART_LSR_BI)
					tty_insert_flip_char(port, *(ch->ch_rqueue +tail +i),  TTY_BREAK);
				else if (*(ch->ch_equeue +tail +i) & UART_LSR_PE)
					tty_insert_flip_char(port, *(ch->ch_rqueue +tail +i), TTY_PARITY);
				else if (*(ch->ch_equeue +tail +i) & UART_LSR_FE)
					tty_insert_flip_char(port, *(ch->ch_rqueue +tail +i), TTY_FRAME);
				else
					tty_insert_flip_char(port, *(ch->ch_rqueue +tail +i), TTY_NORMAL);
			}
		} else {
			tty_insert_flip_string(port, ch->ch_rqueue + tail, s);
		}
		tail += s;
		len -= s;
		/* Flip queue if needed */
		tail &= rmask;
	}

	ch->ch_r_tail = tail & rmask;
	ch->ch_e_tail = tail & rmask;
	jsm_check_queue_flow_control(ch);
	spin_unlock_irqrestore(&ch->ch_lock, lock_flags);

	/* Tell the tty layer its okay to "eat" the data now */
	tty_flip_buffer_push(port);

	jsm_dbg(IOCTL, &ch->ch_bd->pci_dev, "finish\n");
}

static void jsm_carrier(struct jsm_channel *ch)
{
	struct jsm_board *bd;

	int virt_carrier = 0;
	int phys_carrier = 0;

	jsm_dbg(CARR, &ch->ch_bd->pci_dev, "start\n");

	bd = ch->ch_bd;
	if (!bd)
		return;

	if (ch->ch_mistat & UART_MSR_DCD) {
		jsm_dbg(CARR, &ch->ch_bd->pci_dev, "mistat: %x D_CD: %x\n",
			ch->ch_mistat, ch->ch_mistat & UART_MSR_DCD);
		phys_carrier = 1;
	}

	if (ch->ch_c_cflag & CLOCAL)
		virt_carrier = 1;

	jsm_dbg(CARR, &ch->ch_bd->pci_dev, "DCD: physical: %d virt: %d\n",
		phys_carrier, virt_carrier);

	/*
	 * Test for a VIRTUAL carrier transition to HIGH.
	 */
	if (((ch->ch_flags & CH_FCAR) == 0) && (virt_carrier == 1)) {

		/*
		 * When carrier rises, wake any threads waiting
		 * for carrier in the open routine.
		 */

		jsm_dbg(CARR, &ch->ch_bd->pci_dev, "carrier: virt DCD rose\n");

		if (waitqueue_active(&(ch->ch_flags_wait)))
			wake_up_interruptible(&ch->ch_flags_wait);
	}

	/*
	 * Test for a PHYSICAL carrier transition to HIGH.
	 */
	if (((ch->ch_flags & CH_CD) == 0) && (phys_carrier == 1)) {

		/*
		 * When carrier rises, wake any threads waiting
		 * for carrier in the open routine.
		 */

		jsm_dbg(CARR, &ch->ch_bd->pci_dev,
			"carrier: physical DCD rose\n");

		if (waitqueue_active(&(ch->ch_flags_wait)))
			wake_up_interruptible(&ch->ch_flags_wait);
	}

	/*
	 *  Test for a PHYSICAL transition to low, so long as we aren't
	 *  currently ignoring physical transitions (which is what "virtual
	 *  carrier" indicates).
	 *
	 *  The transition of the virtual carrier to low really doesn't
	 *  matter... it really only means "ignore carrier state", not
	 *  "make pretend that carrier is there".
	 */
	if ((virt_carrier == 0) && ((ch->ch_flags & CH_CD) != 0)
			&& (phys_carrier == 0)) {
		/*
		 *	When carrier drops:
		 *
		 *	Drop carrier on all open units.
		 *
		 *	Flush queues, waking up any task waiting in the
		 *	line discipline.
		 *
		 *	Send a hangup to the control terminal.
		 *
		 *	Enable all select calls.
		 */
		if (waitqueue_active(&(ch->ch_flags_wait)))
			wake_up_interruptible(&ch->ch_flags_wait);
	}

	/*
	 *  Make sure that our cached values reflect the current reality.
	 */
	if (virt_carrier == 1)
		ch->ch_flags |= CH_FCAR;
	else
		ch->ch_flags &= ~CH_FCAR;

	if (phys_carrier == 1)
		ch->ch_flags |= CH_CD;
	else
		ch->ch_flags &= ~CH_CD;
}


void jsm_check_queue_flow_control(struct jsm_channel *ch)
{
	struct board_ops *bd_ops = ch->ch_bd->bd_ops;
	int qleft;

	/* Store how much space we have left in the queue */
	if ((qleft = ch->ch_r_tail - ch->ch_r_head - 1) < 0)
		qleft += RQUEUEMASK + 1;

	/*
	 * Check to see if we should enforce flow control on our queue because
	 * the ld (or user) isn't reading data out of our queue fast enuf.
	 *
	 * NOTE: This is done based on what the current flow control of the
	 * port is set for.
	 *
	 * 1) HWFLOW (RTS) - Turn off the UART's Receive interrupt.
	 *	This will cause the UART's FIFO to back up, and force
	 *	the RTS signal to be dropped.
	 * 2) SWFLOW (IXOFF) - Keep trying to send a stop character to
	 *	the other side, in hopes it will stop sending data to us.
	 * 3) NONE - Nothing we can do.  We will simply drop any extra data
	 *	that gets sent into us when the queue fills up.
	 */
	if (qleft < 256) {
		/* HWFLOW */
		if (ch->ch_c_cflag & CRTSCTS) {
			if (!(ch->ch_flags & CH_RECEIVER_OFF)) {
				bd_ops->disable_receiver(ch);
				ch->ch_flags |= (CH_RECEIVER_OFF);
				jsm_dbg(READ, &ch->ch_bd->pci_dev,
					"Internal queue hit hilevel mark (%d)! Turning off interrupts\n",
					qleft);
			}
		}
		/* SWFLOW */
		else if (ch->ch_c_iflag & IXOFF) {
			if (ch->ch_stops_sent <= MAX_STOPS_SENT) {
				bd_ops->send_stop_character(ch);
				ch->ch_stops_sent++;
				jsm_dbg(READ, &ch->ch_bd->pci_dev,
					"Sending stop char! Times sent: %x\n",
					ch->ch_stops_sent);
			}
		}
	}

	/*
	 * Check to see if we should unenforce flow control because
	 * ld (or user) finally read enuf data out of our queue.
	 *
	 * NOTE: This is done based on what the current flow control of the
	 * port is set for.
	 *
	 * 1) HWFLOW (RTS) - Turn back on the UART's Receive interrupt.
	 *	This will cause the UART's FIFO to raise RTS back up,
	 *	which will allow the other side to start sending data again.
	 * 2) SWFLOW (IXOFF) - Send a start character to
	 *	the other side, so it will start sending data to us again.
	 * 3) NONE - Do nothing. Since we didn't do anything to turn off the
	 *	other side, we don't need to do anything now.
	 */
	if (qleft > (RQUEUESIZE / 2)) {
		/* HWFLOW */
		if (ch->ch_c_cflag & CRTSCTS) {
			if (ch->ch_flags & CH_RECEIVER_OFF) {
				bd_ops->enable_receiver(ch);
				ch->ch_flags &= ~(CH_RECEIVER_OFF);
				jsm_dbg(READ, &ch->ch_bd->pci_dev,
					"Internal queue hit lowlevel mark (%d)! Turning on interrupts\n",
					qleft);
			}
		}
		/* SWFLOW */
		else if (ch->ch_c_iflag & IXOFF && ch->ch_stops_sent) {
			ch->ch_stops_sent = 0;
			bd_ops->send_start_character(ch);
			jsm_dbg(READ, &ch->ch_bd->pci_dev,
				"Sending start char!\n");
		}
	}
}
