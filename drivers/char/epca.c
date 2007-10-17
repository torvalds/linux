/*
	Copyright (C) 1996  Digi International.

	For technical support please email digiLinux@dgii.com or
	call Digi tech support at (612) 912-3456

	** This driver is no longer supported by Digi **

	Much of this design and code came from epca.c which was
	copyright (C) 1994, 1995 Troy De Jongh, and subsquently
	modified by David Nugent, Christoph Lameter, Mike McLagan.

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program; if not, write to the Free Software
	Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/
/* See README.epca for change history --DAT*/

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/serial.h>
#include <linux/delay.h>
#include <linux/ctype.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/slab.h>
#include <linux/ioport.h>
#include <linux/interrupt.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include <linux/spinlock.h>
#include <linux/pci.h>
#include "digiPCI.h"


#include "digi1.h"
#include "digiFep1.h"
#include "epca.h"
#include "epcaconfig.h"

#define VERSION            "1.3.0.1-LK2.6"

/* This major needs to be submitted to Linux to join the majors list */
#define DIGIINFOMAJOR       35  /* For Digi specific ioctl */


#define MAXCARDS 7
#define epcaassert(x, msg)  if (!(x)) epca_error(__LINE__, msg)

#define PFX "epca: "

static int nbdevs, num_cards, liloconfig;
static int digi_poller_inhibited = 1 ;

static int setup_error_code;
static int invalid_lilo_config;

/*
 * The ISA boards do window flipping into the same spaces so its only sane with
 * a single lock. It's still pretty efficient.
 */
static DEFINE_SPINLOCK(epca_lock);

/* MAXBOARDS is typically 12, but ISA and EISA cards are restricted to 7 below. */
static struct board_info boards[MAXBOARDS];

static struct tty_driver *pc_driver;
static struct tty_driver *pc_info;

/* ------------------ Begin Digi specific structures -------------------- */

/*
 * digi_channels represents an array of structures that keep track of each
 * channel of the Digi product. Information such as transmit and receive
 * pointers, termio data, and signal definitions (DTR, CTS, etc ...) are stored
 * here. This structure is NOT used to overlay the cards physical channel
 * structure.
 */
static struct channel digi_channels[MAX_ALLOC];

/*
 * card_ptr is an array used to hold the address of the first channel structure
 * of each card. This array will hold the addresses of various channels located
 * in digi_channels.
 */
static struct channel *card_ptr[MAXCARDS];

static struct timer_list epca_timer;

/*
 * Begin generic memory functions. These functions will be alias (point at)
 * more specific functions dependent on the board being configured.
 */
static void memwinon(struct board_info *b, unsigned int win);
static void memwinoff(struct board_info *b, unsigned int win);
static void globalwinon(struct channel *ch);
static void rxwinon(struct channel *ch);
static void txwinon(struct channel *ch);
static void memoff(struct channel *ch);
static void assertgwinon(struct channel *ch);
static void assertmemoff(struct channel *ch);

/* ---- Begin more 'specific' memory functions for cx_like products --- */

static void pcxem_memwinon(struct board_info *b, unsigned int win);
static void pcxem_memwinoff(struct board_info *b, unsigned int win);
static void pcxem_globalwinon(struct channel *ch);
static void pcxem_rxwinon(struct channel *ch);
static void pcxem_txwinon(struct channel *ch);
static void pcxem_memoff(struct channel *ch);

/* ------ Begin more 'specific' memory functions for the pcxe ------- */

static void pcxe_memwinon(struct board_info *b, unsigned int win);
static void pcxe_memwinoff(struct board_info *b, unsigned int win);
static void pcxe_globalwinon(struct channel *ch);
static void pcxe_rxwinon(struct channel *ch);
static void pcxe_txwinon(struct channel *ch);
static void pcxe_memoff(struct channel *ch);

/* ---- Begin more 'specific' memory functions for the pc64xe and pcxi ---- */
/* Note : pc64xe and pcxi share the same windowing routines */

static void pcxi_memwinon(struct board_info *b, unsigned int win);
static void pcxi_memwinoff(struct board_info *b, unsigned int win);
static void pcxi_globalwinon(struct channel *ch);
static void pcxi_rxwinon(struct channel *ch);
static void pcxi_txwinon(struct channel *ch);
static void pcxi_memoff(struct channel *ch);

/* - Begin 'specific' do nothing memory functions needed for some cards - */

static void dummy_memwinon(struct board_info *b, unsigned int win);
static void dummy_memwinoff(struct board_info *b, unsigned int win);
static void dummy_globalwinon(struct channel *ch);
static void dummy_rxwinon(struct channel *ch);
static void dummy_txwinon(struct channel *ch);
static void dummy_memoff(struct channel *ch);
static void dummy_assertgwinon(struct channel *ch);
static void dummy_assertmemoff(struct channel *ch);

static struct channel *verifyChannel(struct tty_struct *);
static void pc_sched_event(struct channel *, int);
static void epca_error(int, char *);
static void pc_close(struct tty_struct *, struct file *);
static void shutdown(struct channel *);
static void pc_hangup(struct tty_struct *);
static void pc_put_char(struct tty_struct *, unsigned char);
static int pc_write_room(struct tty_struct *);
static int pc_chars_in_buffer(struct tty_struct *);
static void pc_flush_buffer(struct tty_struct *);
static void pc_flush_chars(struct tty_struct *);
static int block_til_ready(struct tty_struct *, struct file *,
                           struct channel *);
static int pc_open(struct tty_struct *, struct file *);
static void post_fep_init(unsigned int crd);
static void epcapoll(unsigned long);
static void doevent(int);
static void fepcmd(struct channel *, int, int, int, int, int);
static unsigned termios2digi_h(struct channel *ch, unsigned);
static unsigned termios2digi_i(struct channel *ch, unsigned);
static unsigned termios2digi_c(struct channel *ch, unsigned);
static void epcaparam(struct tty_struct *, struct channel *);
static void receive_data(struct channel *);
static int pc_ioctl(struct tty_struct *, struct file *,
                    unsigned int, unsigned long);
static int info_ioctl(struct tty_struct *, struct file *,
                    unsigned int, unsigned long);
static void pc_set_termios(struct tty_struct *, struct ktermios *);
static void do_softint(struct work_struct *work);
static void pc_stop(struct tty_struct *);
static void pc_start(struct tty_struct *);
static void pc_throttle(struct tty_struct * tty);
static void pc_unthrottle(struct tty_struct *tty);
static void digi_send_break(struct channel *ch, int msec);
static void setup_empty_event(struct tty_struct *tty, struct channel *ch);
void epca_setup(char *, int *);

static int pc_write(struct tty_struct *, const unsigned char *, int);
static int pc_init(void);
static int init_PCI(void);

/*
 * Table of functions for each board to handle memory. Mantaining parallelism
 * is a *very* good idea here. The idea is for the runtime code to blindly call
 * these functions, not knowing/caring about the underlying hardware. This
 * stuff should contain no conditionals; if more functionality is needed a
 * different entry should be established. These calls are the interface calls
 * and are the only functions that should be accessed. Anyone caught making
 * direct calls deserves what they get.
 */
static void memwinon(struct board_info *b, unsigned int win)
{
	b->memwinon(b, win);
}

static void memwinoff(struct board_info *b, unsigned int win)
{
	b->memwinoff(b, win);
}

static void globalwinon(struct channel *ch)
{
	ch->board->globalwinon(ch);
}

static void rxwinon(struct channel *ch)
{
	ch->board->rxwinon(ch);
}

static void txwinon(struct channel *ch)
{
	ch->board->txwinon(ch);
}

static void memoff(struct channel *ch)
{
	ch->board->memoff(ch);
}
static void assertgwinon(struct channel *ch)
{
	ch->board->assertgwinon(ch);
}

static void assertmemoff(struct channel *ch)
{
	ch->board->assertmemoff(ch);
}

/* PCXEM windowing is the same as that used in the PCXR and CX series cards. */
static void pcxem_memwinon(struct board_info *b, unsigned int win)
{
        outb_p(FEPWIN|win, b->port + 1);
}

static void pcxem_memwinoff(struct board_info *b, unsigned int win)
{
	outb_p(0, b->port + 1);
}

static void pcxem_globalwinon(struct channel *ch)
{
	outb_p( FEPWIN, (int)ch->board->port + 1);
}

static void pcxem_rxwinon(struct channel *ch)
{
	outb_p(ch->rxwin, (int)ch->board->port + 1);
}

static void pcxem_txwinon(struct channel *ch)
{
	outb_p(ch->txwin, (int)ch->board->port + 1);
}

static void pcxem_memoff(struct channel *ch)
{
	outb_p(0, (int)ch->board->port + 1);
}

/* ----------------- Begin pcxe memory window stuff ------------------ */
static void pcxe_memwinon(struct board_info *b, unsigned int win)
{
	outb_p(FEPWIN | win, b->port + 1);
}

static void pcxe_memwinoff(struct board_info *b, unsigned int win)
{
	outb_p(inb(b->port) & ~FEPMEM, b->port + 1);
	outb_p(0, b->port + 1);
}

static void pcxe_globalwinon(struct channel *ch)
{
	outb_p(FEPWIN, (int)ch->board->port + 1);
}

static void pcxe_rxwinon(struct channel *ch)
{
	outb_p(ch->rxwin, (int)ch->board->port + 1);
}

static void pcxe_txwinon(struct channel *ch)
{
	outb_p(ch->txwin, (int)ch->board->port + 1);
}

static void pcxe_memoff(struct channel *ch)
{
	outb_p(0, (int)ch->board->port);
	outb_p(0, (int)ch->board->port + 1);
}

/* ------------- Begin pc64xe and pcxi memory window stuff -------------- */
static void pcxi_memwinon(struct board_info *b, unsigned int win)
{
	outb_p(inb(b->port) | FEPMEM, b->port);
}

static void pcxi_memwinoff(struct board_info *b, unsigned int win)
{
	outb_p(inb(b->port) & ~FEPMEM, b->port);
}

static void pcxi_globalwinon(struct channel *ch)
{
	outb_p(FEPMEM, ch->board->port);
}

static void pcxi_rxwinon(struct channel *ch)
{
	outb_p(FEPMEM, ch->board->port);
}

static void pcxi_txwinon(struct channel *ch)
{
	outb_p(FEPMEM, ch->board->port);
}

static void pcxi_memoff(struct channel *ch)
{
	outb_p(0, ch->board->port);
}

static void pcxi_assertgwinon(struct channel *ch)
{
	epcaassert(inb(ch->board->port) & FEPMEM, "Global memory off");
}

static void pcxi_assertmemoff(struct channel *ch)
{
	epcaassert(!(inb(ch->board->port) & FEPMEM), "Memory on");
}

/*
 * Not all of the cards need specific memory windowing routines. Some cards
 * (Such as PCI) needs no windowing routines at all. We provide these do
 * nothing routines so that the same code base can be used. The driver will
 * ALWAYS call a windowing routine if it thinks it needs to; regardless of the
 * card. However, dependent on the card the routine may or may not do anything.
 */
static void dummy_memwinon(struct board_info *b, unsigned int win)
{
}

static void dummy_memwinoff(struct board_info *b, unsigned int win)
{
}

static void dummy_globalwinon(struct channel *ch)
{
}

static void dummy_rxwinon(struct channel *ch)
{
}

static void dummy_txwinon(struct channel *ch)
{
}

static void dummy_memoff(struct channel *ch)
{
}

static void dummy_assertgwinon(struct channel *ch)
{
}

static void dummy_assertmemoff(struct channel *ch)
{
}

static struct channel *verifyChannel(struct tty_struct *tty)
{
	/*
	 * This routine basically provides a sanity check. It insures that the
	 * channel returned is within the proper range of addresses as well as
	 * properly initialized. If some bogus info gets passed in
	 * through tty->driver_data this should catch it.
	 */
	if (tty) {
		struct channel *ch = (struct channel *)tty->driver_data;
		if ((ch >= &digi_channels[0]) && (ch < &digi_channels[nbdevs])) {
			if (ch->magic == EPCA_MAGIC)
				return ch;
		}
	}
	return NULL;
}

static void pc_sched_event(struct channel *ch, int event)
{
	/*
	 * We call this to schedule interrupt processing on some event. The
	 * kernel sees our request and calls the related routine in OUR driver.
	 */
	ch->event |= 1 << event;
	schedule_work(&ch->tqueue);
}

static void epca_error(int line, char *msg)
{
	printk(KERN_ERR "epca_error (Digi): line = %d %s\n",line,msg);
}

static void pc_close(struct tty_struct *tty, struct file *filp)
{
	struct channel *ch;
	unsigned long flags;
	/*
	 * verifyChannel returns the channel from the tty struct if it is
	 * valid. This serves as a sanity check.
	 */
	if ((ch = verifyChannel(tty)) != NULL) {
		spin_lock_irqsave(&epca_lock, flags);
		if (tty_hung_up_p(filp)) {
			spin_unlock_irqrestore(&epca_lock, flags);
			return;
		}
		if (ch->count-- > 1)  {
			/* Begin channel is open more than once */
			/*
			 * Return without doing anything. Someone might still
			 * be using the channel.
			 */
			spin_unlock_irqrestore(&epca_lock, flags);
			return;
		}

		/* Port open only once go ahead with shutdown & reset */
		BUG_ON(ch->count < 0);

		/*
		 * Let the rest of the driver know the channel is being closed.
		 * This becomes important if an open is attempted before close
		 * is finished.
		 */
		ch->asyncflags |= ASYNC_CLOSING;
		tty->closing = 1;

		spin_unlock_irqrestore(&epca_lock, flags);

		if (ch->asyncflags & ASYNC_INITIALIZED)  {
			/* Setup an event to indicate when the transmit buffer empties */
			setup_empty_event(tty, ch);
			tty_wait_until_sent(tty, 3000); /* 30 seconds timeout */
		}
		if (tty->driver->flush_buffer)
			tty->driver->flush_buffer(tty);

		tty_ldisc_flush(tty);
		shutdown(ch);

		spin_lock_irqsave(&epca_lock, flags);
		tty->closing = 0;
		ch->event = 0;
		ch->tty = NULL;
		spin_unlock_irqrestore(&epca_lock, flags);

		if (ch->blocked_open) {
			if (ch->close_delay)
				msleep_interruptible(jiffies_to_msecs(ch->close_delay));
			wake_up_interruptible(&ch->open_wait);
		}
		ch->asyncflags &= ~(ASYNC_NORMAL_ACTIVE | ASYNC_INITIALIZED |
		                      ASYNC_CLOSING);
		wake_up_interruptible(&ch->close_wait);
	}
}

static void shutdown(struct channel *ch)
{
	unsigned long flags;
	struct tty_struct *tty;
	struct board_chan __iomem *bc;

	if (!(ch->asyncflags & ASYNC_INITIALIZED))
		return;

	spin_lock_irqsave(&epca_lock, flags);

	globalwinon(ch);
	bc = ch->brdchan;

	/*
	 * In order for an event to be generated on the receipt of data the
	 * idata flag must be set. Since we are shutting down, this is not
	 * necessary clear this flag.
	 */
	if (bc)
		writeb(0, &bc->idata);
	tty = ch->tty;

	/* If we're a modem control device and HUPCL is on, drop RTS & DTR. */
	if (tty->termios->c_cflag & HUPCL)  {
		ch->omodem &= ~(ch->m_rts | ch->m_dtr);
		fepcmd(ch, SETMODEM, 0, ch->m_dtr | ch->m_rts, 10, 1);
	}
	memoff(ch);

	/*
	 * The channel has officialy been closed. The next time it is opened it
	 * will have to reinitialized. Set a flag to indicate this.
	 */
	/* Prevent future Digi programmed interrupts from coming active */
	ch->asyncflags &= ~ASYNC_INITIALIZED;
	spin_unlock_irqrestore(&epca_lock, flags);
}

static void pc_hangup(struct tty_struct *tty)
{
	struct channel *ch;

	/*
	 * verifyChannel returns the channel from the tty struct if it is
	 * valid. This serves as a sanity check.
	 */
	if ((ch = verifyChannel(tty)) != NULL) {
		unsigned long flags;

		if (tty->driver->flush_buffer)
			tty->driver->flush_buffer(tty);
		tty_ldisc_flush(tty);
		shutdown(ch);

		spin_lock_irqsave(&epca_lock, flags);
		ch->tty   = NULL;
		ch->event = 0;
		ch->count = 0;
		ch->asyncflags &= ~(ASYNC_NORMAL_ACTIVE | ASYNC_INITIALIZED);
		spin_unlock_irqrestore(&epca_lock, flags);
		wake_up_interruptible(&ch->open_wait);
	}
}

static int pc_write(struct tty_struct *tty,
                    const unsigned char *buf, int bytesAvailable)
{
	unsigned int head, tail;
	int dataLen;
	int size;
	int amountCopied;
	struct channel *ch;
	unsigned long flags;
	int remain;
	struct board_chan __iomem *bc;

	/*
	 * pc_write is primarily called directly by the kernel routine
	 * tty_write (Though it can also be called by put_char) found in
	 * tty_io.c. pc_write is passed a line discipline buffer where the data
	 * to be written out is stored. The line discipline implementation
	 * itself is done at the kernel level and is not brought into the
	 * driver.
	 */

	/*
	 * verifyChannel returns the channel from the tty struct if it is
	 * valid. This serves as a sanity check.
	 */
	if ((ch = verifyChannel(tty)) == NULL)
		return 0;

	/* Make a pointer to the channel data structure found on the board. */
	bc   = ch->brdchan;
	size = ch->txbufsize;
	amountCopied = 0;

	spin_lock_irqsave(&epca_lock, flags);
	globalwinon(ch);

	head = readw(&bc->tin) & (size - 1);
	tail = readw(&bc->tout);

	if (tail != readw(&bc->tout))
		tail = readw(&bc->tout);
	tail &= (size - 1);

	if (head >= tail) {
		/* head has not wrapped */
		/*
		 * remain (much like dataLen above) represents the total amount
		 * of space available on the card for data. Here dataLen
		 * represents the space existing between the head pointer and
		 * the end of buffer. This is important because a memcpy cannot
		 * be told to automatically wrap around when it hits the buffer
		 * end.
		 */
		dataLen = size - head;
		remain = size - (head - tail) - 1;
	} else {
		/* head has wrapped around */
		remain = tail - head - 1;
		dataLen = remain;
	}
	/*
	 * Check the space on the card. If we have more data than space; reduce
	 * the amount of data to fit the space.
	 */
	bytesAvailable = min(remain, bytesAvailable);
	txwinon(ch);
	while (bytesAvailable > 0) {
		/* there is data to copy onto card */

		/*
		 * If head is not wrapped, the below will make sure the first
		 * data copy fills to the end of card buffer.
		 */
		dataLen = min(bytesAvailable, dataLen);
		memcpy_toio(ch->txptr + head, buf, dataLen);
		buf += dataLen;
		head += dataLen;
		amountCopied += dataLen;
		bytesAvailable -= dataLen;

		if (head >= size) {
			head = 0;
			dataLen = tail;
		}
	}
	ch->statusflags |= TXBUSY;
	globalwinon(ch);
	writew(head, &bc->tin);

	if ((ch->statusflags & LOWWAIT) == 0)  {
		ch->statusflags |= LOWWAIT;
		writeb(1, &bc->ilow);
	}
	memoff(ch);
	spin_unlock_irqrestore(&epca_lock, flags);
	return amountCopied;
}

static void pc_put_char(struct tty_struct *tty, unsigned char c)
{
	pc_write(tty, &c, 1);
}

static int pc_write_room(struct tty_struct *tty)
{
	int remain;
	struct channel *ch;
	unsigned long flags;
	unsigned int head, tail;
	struct board_chan __iomem *bc;

	remain = 0;

	/*
	 * verifyChannel returns the channel from the tty struct if it is
	 * valid. This serves as a sanity check.
	 */
	if ((ch = verifyChannel(tty)) != NULL)  {
		spin_lock_irqsave(&epca_lock, flags);
		globalwinon(ch);

		bc   = ch->brdchan;
		head = readw(&bc->tin) & (ch->txbufsize - 1);
		tail = readw(&bc->tout);

		if (tail != readw(&bc->tout))
			tail = readw(&bc->tout);
		/* Wrap tail if necessary */
		tail &= (ch->txbufsize - 1);

		if ((remain = tail - head - 1) < 0 )
			remain += ch->txbufsize;

		if (remain && (ch->statusflags & LOWWAIT) == 0) {
			ch->statusflags |= LOWWAIT;
			writeb(1, &bc->ilow);
		}
		memoff(ch);
		spin_unlock_irqrestore(&epca_lock, flags);
	}
	/* Return how much room is left on card */
	return remain;
}

static int pc_chars_in_buffer(struct tty_struct *tty)
{
	int chars;
	unsigned int ctail, head, tail;
	int remain;
	unsigned long flags;
	struct channel *ch;
	struct board_chan __iomem *bc;

	/*
	 * verifyChannel returns the channel from the tty struct if it is
	 * valid. This serves as a sanity check.
	 */
	if ((ch = verifyChannel(tty)) == NULL)
		return 0;

	spin_lock_irqsave(&epca_lock, flags);
	globalwinon(ch);

	bc = ch->brdchan;
	tail = readw(&bc->tout);
	head = readw(&bc->tin);
	ctail = readw(&ch->mailbox->cout);

	if (tail == head && readw(&ch->mailbox->cin) == ctail && readb(&bc->tbusy) == 0)
		chars = 0;
	else  { /* Begin if some space on the card has been used */
		head = readw(&bc->tin) & (ch->txbufsize - 1);
		tail &= (ch->txbufsize - 1);
		/*
		 * The logic here is basically opposite of the above
		 * pc_write_room here we are finding the amount of bytes in the
		 * buffer filled. Not the amount of bytes empty.
		 */
		if ((remain = tail - head - 1) < 0 )
			remain += ch->txbufsize;
		chars = (int)(ch->txbufsize - remain);
		/*
		 * Make it possible to wakeup anything waiting for output in
		 * tty_ioctl.c, etc.
		 *
		 * If not already set. Setup an event to indicate when the
		 * transmit buffer empties.
		 */
		if (!(ch->statusflags & EMPTYWAIT))
			setup_empty_event(tty,ch);
	} /* End if some space on the card has been used */
	memoff(ch);
	spin_unlock_irqrestore(&epca_lock, flags);
	/* Return number of characters residing on card. */
	return chars;
}

static void pc_flush_buffer(struct tty_struct *tty)
{
	unsigned int tail;
	unsigned long flags;
	struct channel *ch;
	struct board_chan __iomem *bc;
	/*
	 * verifyChannel returns the channel from the tty struct if it is
	 * valid. This serves as a sanity check.
	 */
	if ((ch = verifyChannel(tty)) == NULL)
		return;

	spin_lock_irqsave(&epca_lock, flags);
	globalwinon(ch);
	bc   = ch->brdchan;
	tail = readw(&bc->tout);
	/* Have FEP move tout pointer; effectively flushing transmit buffer */
	fepcmd(ch, STOUT, (unsigned) tail, 0, 0, 0);
	memoff(ch);
	spin_unlock_irqrestore(&epca_lock, flags);
	tty_wakeup(tty);
}

static void pc_flush_chars(struct tty_struct *tty)
{
	struct channel *ch;
	/*
	 * verifyChannel returns the channel from the tty struct if it is
	 * valid. This serves as a sanity check.
	 */
	if ((ch = verifyChannel(tty)) != NULL) {
		unsigned long flags;
		spin_lock_irqsave(&epca_lock, flags);
		/*
		 * If not already set and the transmitter is busy setup an
		 * event to indicate when the transmit empties.
		 */
		if ((ch->statusflags & TXBUSY) && !(ch->statusflags & EMPTYWAIT))
			setup_empty_event(tty,ch);
		spin_unlock_irqrestore(&epca_lock, flags);
	}
}

static int block_til_ready(struct tty_struct *tty,
                           struct file *filp, struct channel *ch)
{
	DECLARE_WAITQUEUE(wait,current);
	int retval, do_clocal = 0;
	unsigned long flags;

	if (tty_hung_up_p(filp)) {
		if (ch->asyncflags & ASYNC_HUP_NOTIFY)
			retval = -EAGAIN;
		else
			retval = -ERESTARTSYS;
		return retval;
	}

	/*
	 * If the device is in the middle of being closed, then block until
	 * it's done, and then try again.
	 */
	if (ch->asyncflags & ASYNC_CLOSING) {
		interruptible_sleep_on(&ch->close_wait);

		if (ch->asyncflags & ASYNC_HUP_NOTIFY)
			return -EAGAIN;
		else
			return -ERESTARTSYS;
	}

	if (filp->f_flags & O_NONBLOCK)  {
		/*
		 * If non-blocking mode is set, then make the check up front
		 * and then exit.
		 */
		ch->asyncflags |= ASYNC_NORMAL_ACTIVE;
		return 0;
	}
	if (tty->termios->c_cflag & CLOCAL)
		do_clocal = 1;
	/* Block waiting for the carrier detect and the line to become free */

	retval = 0;
	add_wait_queue(&ch->open_wait, &wait);

	spin_lock_irqsave(&epca_lock, flags);
	/* We dec count so that pc_close will know when to free things */
	if (!tty_hung_up_p(filp))
		ch->count--;
	ch->blocked_open++;
	while (1) {
		set_current_state(TASK_INTERRUPTIBLE);
		if (tty_hung_up_p(filp) ||
		    !(ch->asyncflags & ASYNC_INITIALIZED))
		{
			if (ch->asyncflags & ASYNC_HUP_NOTIFY)
				retval = -EAGAIN;
			else
				retval = -ERESTARTSYS;
			break;
		}
		if (!(ch->asyncflags & ASYNC_CLOSING) &&
			  (do_clocal || (ch->imodem & ch->dcd)))
			break;
		if (signal_pending(current)) {
			retval = -ERESTARTSYS;
			break;
		}
		spin_unlock_irqrestore(&epca_lock, flags);
		/*
		 * Allow someone else to be scheduled. We will occasionally go
		 * through this loop until one of the above conditions change.
		 * The below schedule call will allow other processes to enter
		 * and prevent this loop from hogging the cpu.
		 */
		schedule();
		spin_lock_irqsave(&epca_lock, flags);
	}

	__set_current_state(TASK_RUNNING);
	remove_wait_queue(&ch->open_wait, &wait);
	if (!tty_hung_up_p(filp))
		ch->count++;
	ch->blocked_open--;

	spin_unlock_irqrestore(&epca_lock, flags);

	if (retval)
		return retval;

	ch->asyncflags |= ASYNC_NORMAL_ACTIVE;
	return 0;
}

static int pc_open(struct tty_struct *tty, struct file * filp)
{
	struct channel *ch;
	unsigned long flags;
	int line, retval, boardnum;
	struct board_chan __iomem *bc;
	unsigned int head;

	line = tty->index;
	if (line < 0 || line >= nbdevs)
		return -ENODEV;

	ch = &digi_channels[line];
	boardnum = ch->boardnum;

	/* Check status of board configured in system.  */

	/*
	 * I check to see if the epca_setup routine detected an user error. It
	 * might be better to put this in pc_init, but for the moment it goes
	 * here.
	 */
	if (invalid_lilo_config) {
		if (setup_error_code & INVALID_BOARD_TYPE)
			printk(KERN_ERR "epca: pc_open: Invalid board type specified in kernel options.\n");
		if (setup_error_code & INVALID_NUM_PORTS)
			printk(KERN_ERR "epca: pc_open: Invalid number of ports specified in kernel options.\n");
		if (setup_error_code & INVALID_MEM_BASE)
			printk(KERN_ERR "epca: pc_open: Invalid board memory address specified in kernel options.\n");
		if (setup_error_code & INVALID_PORT_BASE)
			printk(KERN_ERR "epca; pc_open: Invalid board port address specified in kernel options.\n");
		if (setup_error_code & INVALID_BOARD_STATUS)
			printk(KERN_ERR "epca: pc_open: Invalid board status specified in kernel options.\n");
		if (setup_error_code & INVALID_ALTPIN)
			printk(KERN_ERR "epca: pc_open: Invalid board altpin specified in kernel options;\n");
		tty->driver_data = NULL;   /* Mark this device as 'down' */
		return -ENODEV;
	}
	if (boardnum >= num_cards || boards[boardnum].status == DISABLED)  {
		tty->driver_data = NULL;   /* Mark this device as 'down' */
		return(-ENODEV);
	}

	if ((bc = ch->brdchan) == 0) {
		tty->driver_data = NULL;
		return -ENODEV;
	}

	spin_lock_irqsave(&epca_lock, flags);
	/*
	 * Every time a channel is opened, increment a counter. This is
	 * necessary because we do not wish to flush and shutdown the channel
	 * until the last app holding the channel open, closes it.
	 */
	ch->count++;
	/*
	 * Set a kernel structures pointer to our local channel structure. This
	 * way we can get to it when passed only a tty struct.
	 */
	tty->driver_data = ch;
	/*
	 * If this is the first time the channel has been opened, initialize
	 * the tty->termios struct otherwise let pc_close handle it.
	 */
	globalwinon(ch);
	ch->statusflags = 0;

	/* Save boards current modem status */
	ch->imodem = readb(&bc->mstat);

	/*
	 * Set receive head and tail ptrs to each other. This indicates no data
	 * available to read.
	 */
	head = readw(&bc->rin);
	writew(head, &bc->rout);

	/* Set the channels associated tty structure */
	ch->tty = tty;

	/*
	 * The below routine generally sets up parity, baud, flow control
	 * issues, etc.... It effect both control flags and input flags.
	 */
	epcaparam(tty,ch);
	ch->asyncflags |= ASYNC_INITIALIZED;
	memoff(ch);
	spin_unlock_irqrestore(&epca_lock, flags);

	retval = block_til_ready(tty, filp, ch);
	if (retval)
		return retval;
	/*
	 * Set this again in case a hangup set it to zero while this open() was
	 * waiting for the line...
	 */
	spin_lock_irqsave(&epca_lock, flags);
	ch->tty = tty;
	globalwinon(ch);
	/* Enable Digi Data events */
	writeb(1, &bc->idata);
	memoff(ch);
	spin_unlock_irqrestore(&epca_lock, flags);
	return 0;
}

static int __init epca_module_init(void)
{
	return pc_init();
}
module_init(epca_module_init);

static struct pci_driver epca_driver;

static void __exit epca_module_exit(void)
{
	int               count, crd;
	struct board_info *bd;
	struct channel    *ch;

	del_timer_sync(&epca_timer);

	if (tty_unregister_driver(pc_driver) || tty_unregister_driver(pc_info))
	{
		printk(KERN_WARNING "epca: cleanup_module failed to un-register tty driver\n");
		return;
	}
	put_tty_driver(pc_driver);
	put_tty_driver(pc_info);

	for (crd = 0; crd < num_cards; crd++) {
		bd = &boards[crd];
		if (!bd) { /* sanity check */
			printk(KERN_ERR "<Error> - Digi : cleanup_module failed\n");
			return;
		}
		ch = card_ptr[crd];
		for (count = 0; count < bd->numports; count++, ch++) {
			if (ch && ch->tty)
				tty_hangup(ch->tty);
		}
	}
	pci_unregister_driver(&epca_driver);
}
module_exit(epca_module_exit);

static const struct tty_operations pc_ops = {
	.open = pc_open,
	.close = pc_close,
	.write = pc_write,
	.write_room = pc_write_room,
	.flush_buffer = pc_flush_buffer,
	.chars_in_buffer = pc_chars_in_buffer,
	.flush_chars = pc_flush_chars,
	.put_char = pc_put_char,
	.ioctl = pc_ioctl,
	.set_termios = pc_set_termios,
	.stop = pc_stop,
	.start = pc_start,
	.throttle = pc_throttle,
	.unthrottle = pc_unthrottle,
	.hangup = pc_hangup,
};

static int info_open(struct tty_struct *tty, struct file * filp)
{
	return 0;
}

static struct tty_operations info_ops = {
	.open = info_open,
	.ioctl = info_ioctl,
};

static int __init pc_init(void)
{
	int crd;
	struct board_info *bd;
	unsigned char board_id = 0;
	int err = -ENOMEM;

	int pci_boards_found, pci_count;

	pci_count = 0;

	pc_driver = alloc_tty_driver(MAX_ALLOC);
	if (!pc_driver)
		goto out1;

	pc_info = alloc_tty_driver(MAX_ALLOC);
	if (!pc_info)
		goto out2;

	/*
	 * If epca_setup has not been ran by LILO set num_cards to defaults;
	 * copy board structure defined by digiConfig into drivers board
	 * structure. Note : If LILO has ran epca_setup then epca_setup will
	 * handle defining num_cards as well as copying the data into the board
	 * structure.
	 */
	if (!liloconfig) {
		/* driver has been configured via. epcaconfig */
		nbdevs = NBDEVS;
		num_cards = NUMCARDS;
		memcpy(&boards, &static_boards,
		       sizeof(struct board_info) * NUMCARDS);
	}

	/*
	 * Note : If lilo was used to configure the driver and the ignore
	 * epcaconfig option was choosen (digiepca=2) then nbdevs and num_cards
	 * will equal 0 at this point. This is okay; PCI cards will still be
	 * picked up if detected.
	 */

	/*
	 * Set up interrupt, we will worry about memory allocation in
	 * post_fep_init.
	 */
	printk(KERN_INFO "DIGI epca driver version %s loaded.\n",VERSION);

	/*
	 * NOTE : This code assumes that the number of ports found in the
	 * boards array is correct. This could be wrong if the card in question
	 * is PCI (And therefore has no ports entry in the boards structure.)
	 * The rest of the information will be valid for PCI because the
	 * beginning of pc_init scans for PCI and determines i/o and base
	 * memory addresses. I am not sure if it is possible to read the number
	 * of ports supported by the card prior to it being booted (Since that
	 * is the state it is in when pc_init is run). Because it is not
	 * possible to query the number of supported ports until after the card
	 * has booted; we are required to calculate the card_ptrs as the card
	 * is initialized (Inside post_fep_init). The negative thing about this
	 * approach is that digiDload's call to GET_INFO will have a bad port
	 * value. (Since this is called prior to post_fep_init.)
	 */
	pci_boards_found = 0;
	if (num_cards < MAXBOARDS)
		pci_boards_found += init_PCI();
	num_cards += pci_boards_found;

	pc_driver->owner = THIS_MODULE;
	pc_driver->name = "ttyD";
	pc_driver->major = DIGI_MAJOR;
	pc_driver->minor_start = 0;
	pc_driver->type = TTY_DRIVER_TYPE_SERIAL;
	pc_driver->subtype = SERIAL_TYPE_NORMAL;
	pc_driver->init_termios = tty_std_termios;
	pc_driver->init_termios.c_iflag = 0;
	pc_driver->init_termios.c_oflag = 0;
	pc_driver->init_termios.c_cflag = B9600 | CS8 | CREAD | CLOCAL | HUPCL;
	pc_driver->init_termios.c_lflag = 0;
	pc_driver->init_termios.c_ispeed = 9600;
	pc_driver->init_termios.c_ospeed = 9600;
	pc_driver->flags = TTY_DRIVER_REAL_RAW;
	tty_set_operations(pc_driver, &pc_ops);

	pc_info->owner = THIS_MODULE;
	pc_info->name = "digi_ctl";
	pc_info->major = DIGIINFOMAJOR;
	pc_info->minor_start = 0;
	pc_info->type = TTY_DRIVER_TYPE_SERIAL;
	pc_info->subtype = SERIAL_TYPE_INFO;
	pc_info->init_termios = tty_std_termios;
	pc_info->init_termios.c_iflag = 0;
	pc_info->init_termios.c_oflag = 0;
	pc_info->init_termios.c_lflag = 0;
	pc_info->init_termios.c_cflag = B9600 | CS8 | CREAD | HUPCL;
	pc_info->init_termios.c_ispeed = 9600;
	pc_info->init_termios.c_ospeed = 9600;
	pc_info->flags = TTY_DRIVER_REAL_RAW;
	tty_set_operations(pc_info, &info_ops);


	for (crd = 0; crd < num_cards; crd++) {
		/*
		 * This is where the appropriate memory handlers for the
		 * hardware is set. Everything at runtime blindly jumps through
		 * these vectors.
		 */

		/* defined in epcaconfig.h */
		bd = &boards[crd];

		switch (bd->type) {
		case PCXEM:
		case EISAXEM:
			bd->memwinon     = pcxem_memwinon;
			bd->memwinoff    = pcxem_memwinoff;
			bd->globalwinon  = pcxem_globalwinon;
			bd->txwinon      = pcxem_txwinon;
			bd->rxwinon      = pcxem_rxwinon;
			bd->memoff       = pcxem_memoff;
			bd->assertgwinon = dummy_assertgwinon;
			bd->assertmemoff = dummy_assertmemoff;
			break;

		case PCIXEM:
		case PCIXRJ:
		case PCIXR:
			bd->memwinon     = dummy_memwinon;
			bd->memwinoff    = dummy_memwinoff;
			bd->globalwinon  = dummy_globalwinon;
			bd->txwinon      = dummy_txwinon;
			bd->rxwinon      = dummy_rxwinon;
			bd->memoff       = dummy_memoff;
			bd->assertgwinon = dummy_assertgwinon;
			bd->assertmemoff = dummy_assertmemoff;
			break;

		case PCXE:
		case PCXEVE:
			bd->memwinon     = pcxe_memwinon;
			bd->memwinoff    = pcxe_memwinoff;
			bd->globalwinon  = pcxe_globalwinon;
			bd->txwinon      = pcxe_txwinon;
			bd->rxwinon      = pcxe_rxwinon;
			bd->memoff       = pcxe_memoff;
			bd->assertgwinon = dummy_assertgwinon;
			bd->assertmemoff = dummy_assertmemoff;
			break;

		case PCXI:
		case PC64XE:
			bd->memwinon     = pcxi_memwinon;
			bd->memwinoff    = pcxi_memwinoff;
			bd->globalwinon  = pcxi_globalwinon;
			bd->txwinon      = pcxi_txwinon;
			bd->rxwinon      = pcxi_rxwinon;
			bd->memoff       = pcxi_memoff;
			bd->assertgwinon = pcxi_assertgwinon;
			bd->assertmemoff = pcxi_assertmemoff;
			break;

		default:
			break;
		}

		/*
		 * Some cards need a memory segment to be defined for use in
		 * transmit and receive windowing operations. These boards are
		 * listed in the below switch. In the case of the XI the amount
		 * of memory on the board is variable so the memory_seg is also
		 * variable. This code determines what they segment should be.
		 */
		switch (bd->type) {
		case PCXE:
		case PCXEVE:
		case PC64XE:
			bd->memory_seg = 0xf000;
			break;

		case PCXI:
			board_id = inb((int)bd->port);
			if ((board_id & 0x1) == 0x1) {
				/* it's an XI card */
				/* Is it a 64K board */
				if ((board_id & 0x30) == 0)
					bd->memory_seg = 0xf000;

				/* Is it a 128K board */
				if ((board_id & 0x30) == 0x10)
					bd->memory_seg = 0xe000;

				/* Is is a 256K board */
				if ((board_id & 0x30) == 0x20)
					bd->memory_seg = 0xc000;

				/* Is it a 512K board */
				if ((board_id & 0x30) == 0x30)
					bd->memory_seg = 0x8000;
			} else
				printk(KERN_ERR "epca: Board at 0x%x doesn't appear to be an XI\n",(int)bd->port);
			break;
		}
	}

	err = tty_register_driver(pc_driver);
	if (err) {
		printk(KERN_ERR "Couldn't register Digi PC/ driver");
		goto out3;
	}

	err = tty_register_driver(pc_info);
	if (err) {
		printk(KERN_ERR "Couldn't register Digi PC/ info ");
		goto out4;
	}

	/* Start up the poller to check for events on all enabled boards */
	init_timer(&epca_timer);
	epca_timer.function = epcapoll;
	mod_timer(&epca_timer, jiffies + HZ/25);
	return 0;

out4:
	tty_unregister_driver(pc_driver);
out3:
	put_tty_driver(pc_info);
out2:
	put_tty_driver(pc_driver);
out1:
	return err;
}

static void post_fep_init(unsigned int crd)
{
	int i;
	void __iomem *memaddr;
	struct global_data __iomem *gd;
	struct board_info *bd;
	struct board_chan __iomem *bc;
	struct channel *ch;
	int shrinkmem = 0, lowwater;

	/*
	 * This call is made by the user via. the ioctl call DIGI_INIT. It is
	 * responsible for setting up all the card specific stuff.
	 */
	bd = &boards[crd];

	/*
	 * If this is a PCI board, get the port info. Remember PCI cards do not
	 * have entries into the epcaconfig.h file, so we can't get the number
	 * of ports from it. Unfortunetly, this means that anyone doing a
	 * DIGI_GETINFO before the board has booted will get an invalid number
	 * of ports returned (It should return 0). Calls to DIGI_GETINFO after
	 * DIGI_INIT has been called will return the proper values.
	 */
	if (bd->type >= PCIXEM) { /* Begin get PCI number of ports */
		/*
		 * Below we use XEMPORTS as a memory offset regardless of which
		 * PCI card it is. This is because all of the supported PCI
		 * cards have the same memory offset for the channel data. This
		 * will have to be changed if we ever develop a PCI/XE card.
		 * NOTE : The FEP manual states that the port offset is 0xC22
		 * as opposed to 0xC02. This is only true for PC/XE, and PC/XI
		 * cards; not for the XEM, or CX series. On the PCI cards the
		 * number of ports is determined by reading a ID PROM located
		 * in the box attached to the card. The card can then determine
		 * the index the id to determine the number of ports available.
		 * (FYI - The id should be located at 0x1ac (And may use up to
		 * 4 bytes if the box in question is a XEM or CX)).
		 */
		/* PCI cards are already remapped at this point ISA are not */
		bd->numports = readw(bd->re_map_membase + XEMPORTS);
		epcaassert(bd->numports <= 64,"PCI returned a invalid number of ports");
		nbdevs += (bd->numports);
	} else {
		/* Fix up the mappings for ISA/EISA etc */
		/* FIXME: 64K - can we be smarter ? */
		bd->re_map_membase = ioremap(bd->membase, 0x10000);
	}

	if (crd != 0)
		card_ptr[crd] = card_ptr[crd-1] + boards[crd-1].numports;
	else
		card_ptr[crd] = &digi_channels[crd]; /* <- For card 0 only */

	ch = card_ptr[crd];
	epcaassert(ch <= &digi_channels[nbdevs - 1], "ch out of range");

	memaddr = bd->re_map_membase;

	/*
	 * The below assignment will set bc to point at the BEGINING of the
	 * cards channel structures. For 1 card there will be between 8 and 64
	 * of these structures.
	 */
	bc = memaddr + CHANSTRUCT;

	/*
	 * The below assignment will set gd to point at the BEGINING of global
	 * memory address 0xc00. The first data in that global memory actually
	 * starts at address 0xc1a. The command in pointer begins at 0xd10.
	 */
	gd = memaddr + GLOBAL;

	/*
	 * XEPORTS (address 0xc22) points at the number of channels the card
	 * supports. (For 64XE, XI, XEM, and XR use 0xc02)
	 */
	if ((bd->type == PCXEVE || bd->type == PCXE) && (readw(memaddr + XEPORTS) < 3))
		shrinkmem = 1;
	if (bd->type < PCIXEM)
		if (!request_region((int)bd->port, 4, board_desc[bd->type]))
			return;
	memwinon(bd, 0);

	/*
	 * Remember ch is the main drivers channels structure, while bc is the
	 * cards channel structure.
	 */
	for (i = 0; i < bd->numports; i++, ch++, bc++) {
		unsigned long flags;
		u16 tseg, rseg;

		ch->brdchan = bc;
		ch->mailbox = gd;
		INIT_WORK(&ch->tqueue, do_softint);
		ch->board = &boards[crd];

		spin_lock_irqsave(&epca_lock, flags);
		switch (bd->type) {
		/*
		 * Since some of the boards use different bitmaps for
		 * their control signals we cannot hard code these
		 * values and retain portability. We virtualize this
		 * data here.
		 */
		case EISAXEM:
		case PCXEM:
		case PCIXEM:
		case PCIXRJ:
		case PCIXR:
			ch->m_rts = 0x02;
			ch->m_dcd = 0x80;
			ch->m_dsr = 0x20;
			ch->m_cts = 0x10;
			ch->m_ri  = 0x40;
			ch->m_dtr = 0x01;
			break;

		case PCXE:
		case PCXEVE:
		case PCXI:
		case PC64XE:
			ch->m_rts = 0x02;
			ch->m_dcd = 0x08;
			ch->m_dsr = 0x10;
			ch->m_cts = 0x20;
			ch->m_ri  = 0x40;
			ch->m_dtr = 0x80;
			break;
		}

		if (boards[crd].altpin) {
			ch->dsr = ch->m_dcd;
			ch->dcd = ch->m_dsr;
			ch->digiext.digi_flags |= DIGI_ALTPIN;
		} else {
			ch->dcd = ch->m_dcd;
			ch->dsr = ch->m_dsr;
		}

		ch->boardnum   = crd;
		ch->channelnum = i;
		ch->magic      = EPCA_MAGIC;
		ch->tty        = NULL;

		if (shrinkmem) {
			fepcmd(ch, SETBUFFER, 32, 0, 0, 0);
			shrinkmem = 0;
		}

		tseg = readw(&bc->tseg);
		rseg = readw(&bc->rseg);

		switch (bd->type) {
		case PCIXEM:
		case PCIXRJ:
		case PCIXR:
			/* Cover all the 2MEG cards */
			ch->txptr = memaddr + ((tseg << 4) & 0x1fffff);
			ch->rxptr = memaddr + ((rseg << 4) & 0x1fffff);
			ch->txwin = FEPWIN | (tseg >> 11);
			ch->rxwin = FEPWIN | (rseg >> 11);
			break;

		case PCXEM:
		case EISAXEM:
			/* Cover all the 32K windowed cards */
			/* Mask equal to window size - 1 */
			ch->txptr = memaddr + ((tseg << 4) & 0x7fff);
			ch->rxptr = memaddr + ((rseg << 4) & 0x7fff);
			ch->txwin = FEPWIN | (tseg >> 11);
			ch->rxwin = FEPWIN | (rseg >> 11);
			break;

		case PCXEVE:
		case PCXE:
			ch->txptr = memaddr + (((tseg - bd->memory_seg) << 4) & 0x1fff);
			ch->txwin = FEPWIN | ((tseg - bd->memory_seg) >> 9);
			ch->rxptr = memaddr + (((rseg - bd->memory_seg) << 4) & 0x1fff);
			ch->rxwin = FEPWIN | ((rseg - bd->memory_seg) >>9 );
			break;

		case PCXI:
		case PC64XE:
			ch->txptr = memaddr + ((tseg - bd->memory_seg) << 4);
			ch->rxptr = memaddr + ((rseg - bd->memory_seg) << 4);
			ch->txwin = ch->rxwin = 0;
			break;
		}

		ch->txbufhead = 0;
		ch->txbufsize = readw(&bc->tmax) + 1;

		ch->rxbufhead = 0;
		ch->rxbufsize = readw(&bc->rmax) + 1;

		lowwater = ch->txbufsize >= 2000 ? 1024 : (ch->txbufsize / 2);

		/* Set transmitter low water mark */
		fepcmd(ch, STXLWATER, lowwater, 0, 10, 0);

		/* Set receiver low water mark */
		fepcmd(ch, SRXLWATER, (ch->rxbufsize / 4), 0, 10, 0);

		/* Set receiver high water mark */
		fepcmd(ch, SRXHWATER, (3 * ch->rxbufsize / 4), 0, 10, 0);

		writew(100, &bc->edelay);
		writeb(1, &bc->idata);

		ch->startc  = readb(&bc->startc);
		ch->stopc   = readb(&bc->stopc);
		ch->startca = readb(&bc->startca);
		ch->stopca  = readb(&bc->stopca);

		ch->fepcflag = 0;
		ch->fepiflag = 0;
		ch->fepoflag = 0;
		ch->fepstartc = 0;
		ch->fepstopc = 0;
		ch->fepstartca = 0;
		ch->fepstopca = 0;

		ch->close_delay = 50;
		ch->count = 0;
		ch->blocked_open = 0;
		init_waitqueue_head(&ch->open_wait);
		init_waitqueue_head(&ch->close_wait);

		spin_unlock_irqrestore(&epca_lock, flags);
	}

	printk(KERN_INFO
	        "Digi PC/Xx Driver V%s:  %s I/O = 0x%lx Mem = 0x%lx Ports = %d\n",
	        VERSION, board_desc[bd->type], (long)bd->port, (long)bd->membase, bd->numports);
	memwinoff(bd, 0);
}

static void epcapoll(unsigned long ignored)
{
	unsigned long flags;
	int crd;
	volatile unsigned int head, tail;
	struct channel *ch;
	struct board_info *bd;

	/*
	 * This routine is called upon every timer interrupt. Even though the
	 * Digi series cards are capable of generating interrupts this method
	 * of non-looping polling is more efficient. This routine checks for
	 * card generated events (Such as receive data, are transmit buffer
	 * empty) and acts on those events.
	 */
	for (crd = 0; crd < num_cards; crd++) {
		bd = &boards[crd];
		ch = card_ptr[crd];

		if ((bd->status == DISABLED) || digi_poller_inhibited)
			continue;

		/*
		 * assertmemoff is not needed here; indeed it is an empty
		 * subroutine. It is being kept because future boards may need
		 * this as well as some legacy boards.
		 */
		spin_lock_irqsave(&epca_lock, flags);

		assertmemoff(ch);

		globalwinon(ch);

		/*
		 * In this case head and tail actually refer to the event queue
		 * not the transmit or receive queue.
		 */
		head = readw(&ch->mailbox->ein);
		tail = readw(&ch->mailbox->eout);

		/* If head isn't equal to tail we have an event */
		if (head != tail)
			doevent(crd);
		memoff(ch);

		spin_unlock_irqrestore(&epca_lock, flags);
	} /* End for each card */
	mod_timer(&epca_timer, jiffies + (HZ / 25));
}

static void doevent(int crd)
{
	void __iomem *eventbuf;
	struct channel *ch, *chan0;
	static struct tty_struct *tty;
	struct board_info *bd;
	struct board_chan __iomem *bc;
	unsigned int tail, head;
	int event, channel;
	int mstat, lstat;

	/*
	 * This subroutine is called by epcapoll when an event is detected
	 * in the event queue. This routine responds to those events.
	 */
	bd = &boards[crd];

	chan0 = card_ptr[crd];
	epcaassert(chan0 <= &digi_channels[nbdevs - 1], "ch out of range");
	assertgwinon(chan0);
	while ((tail = readw(&chan0->mailbox->eout)) != (head = readw(&chan0->mailbox->ein))) { /* Begin while something in event queue */
		assertgwinon(chan0);
		eventbuf = bd->re_map_membase + tail + ISTART;
		/* Get the channel the event occurred on */
		channel = readb(eventbuf);
		/* Get the actual event code that occurred */
		event = readb(eventbuf + 1);
		/*
		 * The two assignments below get the current modem status
		 * (mstat) and the previous modem status (lstat). These are
		 * useful becuase an event could signal a change in modem
		 * signals itself.
		 */
		mstat = readb(eventbuf + 2);
		lstat = readb(eventbuf + 3);

		ch = chan0 + channel;
		if ((unsigned)channel >= bd->numports || !ch)  {
			if (channel >= bd->numports)
				ch = chan0;
			bc = ch->brdchan;
			goto next;
		}

		if ((bc = ch->brdchan) == NULL)
			goto next;

		if (event & DATA_IND)  { /* Begin DATA_IND */
			receive_data(ch);
			assertgwinon(ch);
		} /* End DATA_IND */
		/* else *//* Fix for DCD transition missed bug */
		if (event & MODEMCHG_IND) {
			/* A modem signal change has been indicated */
			ch->imodem = mstat;
			if (ch->asyncflags & ASYNC_CHECK_CD) {
				if (mstat & ch->dcd)  /* We are now receiving dcd */
					wake_up_interruptible(&ch->open_wait);
				else
					pc_sched_event(ch, EPCA_EVENT_HANGUP); /* No dcd; hangup */
			}
		}
		tty = ch->tty;
		if (tty) {
			if (event & BREAK_IND) {
				/* A break has been indicated */
				tty_insert_flip_char(tty, 0, TTY_BREAK);
				tty_schedule_flip(tty);
			} else if (event & LOWTX_IND)  {
				if (ch->statusflags & LOWWAIT) {
					ch->statusflags &= ~LOWWAIT;
					tty_wakeup(tty);
				}
			} else if (event & EMPTYTX_IND) {
				/* This event is generated by setup_empty_event */
				ch->statusflags &= ~TXBUSY;
				if (ch->statusflags & EMPTYWAIT) {
					ch->statusflags &= ~EMPTYWAIT;
					tty_wakeup(tty);
				}
			}
		}
	next:
		globalwinon(ch);
		BUG_ON(!bc);
		writew(1, &bc->idata);
		writew((tail + 4) & (IMAX - ISTART - 4), &chan0->mailbox->eout);
		globalwinon(chan0);
	} /* End while something in event queue */
}

static void fepcmd(struct channel *ch, int cmd, int word_or_byte,
                   int byte2, int ncmds, int bytecmd)
{
	unchar __iomem *memaddr;
	unsigned int head, cmdTail, cmdStart, cmdMax;
	long count;
	int n;

	/* This is the routine in which commands may be passed to the card. */

	if (ch->board->status == DISABLED)
		return;
	assertgwinon(ch);
	/* Remember head (As well as max) is just an offset not a base addr */
	head = readw(&ch->mailbox->cin);
	/* cmdStart is a base address */
	cmdStart = readw(&ch->mailbox->cstart);
	/*
	 * We do the addition below because we do not want a max pointer
	 * relative to cmdStart. We want a max pointer that points at the
	 * physical end of the command queue.
	 */
	cmdMax = (cmdStart + 4 + readw(&ch->mailbox->cmax));
	memaddr = ch->board->re_map_membase;

	if (head >= (cmdMax - cmdStart) || (head & 03))  {
		printk(KERN_ERR "line %d: Out of range, cmd = %x, head = %x\n", __LINE__,  cmd, head);
		printk(KERN_ERR "line %d: Out of range, cmdMax = %x, cmdStart = %x\n", __LINE__,  cmdMax, cmdStart);
		return;
	}
	if (bytecmd)  {
		writeb(cmd, memaddr + head + cmdStart + 0);
		writeb(ch->channelnum,  memaddr + head + cmdStart + 1);
		/* Below word_or_byte is bits to set */
		writeb(word_or_byte,  memaddr + head + cmdStart + 2);
		/* Below byte2 is bits to reset */
		writeb(byte2, memaddr + head + cmdStart + 3);
	}  else {
		writeb(cmd, memaddr + head + cmdStart + 0);
		writeb(ch->channelnum,  memaddr + head + cmdStart + 1);
		writeb(word_or_byte,  memaddr + head + cmdStart + 2);
	}
	head = (head + 4) & (cmdMax - cmdStart - 4);
	writew(head, &ch->mailbox->cin);
	count = FEPTIMEOUT;

	for (;;) {
		count--;
		if (count == 0)  {
			printk(KERN_ERR "<Error> - Fep not responding in fepcmd()\n");
			return;
		}
		head = readw(&ch->mailbox->cin);
		cmdTail = readw(&ch->mailbox->cout);
		n = (head - cmdTail) & (cmdMax - cmdStart - 4);
		/*
		 * Basically this will break when the FEP acknowledges the
		 * command by incrementing cmdTail (Making it equal to head).
		 */
		if (n <= ncmds * (sizeof(short) * 4))
			break;
	}
}

/*
 * Digi products use fields in their channels structures that are very similar
 * to the c_cflag and c_iflag fields typically found in UNIX termios
 * structures. The below three routines allow mappings between these hardware
 * "flags" and their respective Linux flags.
 */
static unsigned termios2digi_h(struct channel *ch, unsigned cflag)
{
	unsigned res = 0;

	if (cflag & CRTSCTS) {
		ch->digiext.digi_flags |= (RTSPACE | CTSPACE);
		res |= ((ch->m_cts) | (ch->m_rts));
	}

	if (ch->digiext.digi_flags & RTSPACE)
		res |= ch->m_rts;

	if (ch->digiext.digi_flags & DTRPACE)
		res |= ch->m_dtr;

	if (ch->digiext.digi_flags & CTSPACE)
		res |= ch->m_cts;

	if (ch->digiext.digi_flags & DSRPACE)
		res |= ch->dsr;

	if (ch->digiext.digi_flags & DCDPACE)
		res |= ch->dcd;

	if (res & (ch->m_rts))
		ch->digiext.digi_flags |= RTSPACE;

	if (res & (ch->m_cts))
		ch->digiext.digi_flags |= CTSPACE;

	return res;
}

static unsigned termios2digi_i(struct channel *ch, unsigned iflag)
{
	unsigned res = iflag & (IGNBRK | BRKINT | IGNPAR | PARMRK |
	                        INPCK | ISTRIP|IXON|IXANY|IXOFF);
	if (ch->digiext.digi_flags & DIGI_AIXON)
		res |= IAIXON;
	return res;
}

static unsigned termios2digi_c(struct channel *ch, unsigned cflag)
{
	unsigned res = 0;
	if (cflag & CBAUDEX) {
		ch->digiext.digi_flags |= DIGI_FAST;
		/*
		 * HUPCL bit is used by FEP to indicate fast baud table is to
		 * be used.
		 */
		res |= FEP_HUPCL;
	} else
		ch->digiext.digi_flags &= ~DIGI_FAST;
	/*
	 * CBAUD has bit position 0x1000 set these days to indicate Linux
	 * baud rate remap. Digi hardware can't handle the bit assignment.
	 * (We use a different bit assignment for high speed.). Clear this
	 * bit out.
	 */
	res |= cflag & ((CBAUD ^ CBAUDEX) | PARODD | PARENB | CSTOPB | CSIZE);
	/*
	 * This gets a little confusing. The Digi cards have their own
	 * representation of c_cflags controling baud rate. For the most part
	 * this is identical to the Linux implementation. However; Digi
	 * supports one rate (76800) that Linux doesn't. This means that the
	 * c_cflag entry that would normally mean 76800 for Digi actually means
	 * 115200 under Linux. Without the below mapping, a stty 115200 would
	 * only drive the board at 76800. Since the rate 230400 is also found
	 * after 76800, the same problem afflicts us when we choose a rate of
	 * 230400. Without the below modificiation stty 230400 would actually
	 * give us 115200.
	 *
	 * There are two additional differences. The Linux value for CLOCAL
	 * (0x800; 0004000) has no meaning to the Digi hardware. Also in later
	 * releases of Linux; the CBAUD define has CBAUDEX (0x1000; 0010000)
	 * ored into it (CBAUD = 0x100f as opposed to 0xf). CBAUDEX should be
	 * checked for a screened out prior to termios2digi_c returning. Since
	 * CLOCAL isn't used by the board this can be ignored as long as the
	 * returned value is used only by Digi hardware.
	 */
	if (cflag & CBAUDEX) {
		/*
		 * The below code is trying to guarantee that only baud rates
		 * 115200 and 230400 are remapped. We use exclusive or because
		 * the various baud rates share common bit positions and
		 * therefore can't be tested for easily.
		 */
		if ((!((cflag & 0x7) ^ (B115200 & ~CBAUDEX))) ||
		    (!((cflag & 0x7) ^ (B230400 & ~CBAUDEX))))
			res += 1;
	}
	return res;
}

/* Caller must hold the locks */
static void epcaparam(struct tty_struct *tty, struct channel *ch)
{
	unsigned int cmdHead;
	struct ktermios *ts;
	struct board_chan __iomem *bc;
	unsigned mval, hflow, cflag, iflag;

	bc = ch->brdchan;
	epcaassert(bc !=0, "bc out of range");

	assertgwinon(ch);
	ts = tty->termios;
	if ((ts->c_cflag & CBAUD) == 0)  { /* Begin CBAUD detected */
		cmdHead = readw(&bc->rin);
		writew(cmdHead, &bc->rout);
		cmdHead = readw(&bc->tin);
		/* Changing baud in mid-stream transmission can be wonderful */
		/*
		 * Flush current transmit buffer by setting cmdTail pointer
		 * (tout) to cmdHead pointer (tin). Hopefully the transmit
		 * buffer is empty.
		 */
		fepcmd(ch, STOUT, (unsigned) cmdHead, 0, 0, 0);
		mval = 0;
	} else { /* Begin CBAUD not detected */
		/*
		 * c_cflags have changed but that change had nothing to do with
		 * BAUD. Propagate the change to the card.
		 */
		cflag = termios2digi_c(ch, ts->c_cflag);
		if (cflag != ch->fepcflag)  {
			ch->fepcflag = cflag;
			/* Set baud rate, char size, stop bits, parity */
			fepcmd(ch, SETCTRLFLAGS, (unsigned) cflag, 0, 0, 0);
		}
		/*
		 * If the user has not forced CLOCAL and if the device is not a
		 * CALLOUT device (Which is always CLOCAL) we set flags such
		 * that the driver will wait on carrier detect.
		 */
		if (ts->c_cflag & CLOCAL)
			ch->asyncflags &= ~ASYNC_CHECK_CD;
		else
			ch->asyncflags |= ASYNC_CHECK_CD;
		mval = ch->m_dtr | ch->m_rts;
	} /* End CBAUD not detected */
	iflag = termios2digi_i(ch, ts->c_iflag);
	/* Check input mode flags */
	if (iflag != ch->fepiflag)  {
		ch->fepiflag = iflag;
		/*
		 * Command sets channels iflag structure on the board. Such
		 * things as input soft flow control, handling of parity
		 * errors, and break handling are all set here.
		 */
		/* break handling, parity handling, input stripping, flow control chars */
		fepcmd(ch, SETIFLAGS, (unsigned int) ch->fepiflag, 0, 0, 0);
	}
	/*
	 * Set the board mint value for this channel. This will cause hardware
	 * events to be generated each time the DCD signal (Described in mint)
	 * changes.
	 */
	writeb(ch->dcd, &bc->mint);
	if ((ts->c_cflag & CLOCAL) || (ch->digiext.digi_flags & DIGI_FORCEDCD))
		if (ch->digiext.digi_flags & DIGI_FORCEDCD)
			writeb(0, &bc->mint);
	ch->imodem = readb(&bc->mstat);
	hflow = termios2digi_h(ch, ts->c_cflag);
	if (hflow != ch->hflow)  {
		ch->hflow = hflow;
		/*
		 * Hard flow control has been selected but the board is not
		 * using it. Activate hard flow control now.
		 */
		fepcmd(ch, SETHFLOW, hflow, 0xff, 0, 1);
	}
	mval ^= ch->modemfake & (mval ^ ch->modem);

	if (ch->omodem ^ mval)  {
		ch->omodem = mval;
		/*
		 * The below command sets the DTR and RTS mstat structure. If
		 * hard flow control is NOT active these changes will drive the
		 * output of the actual DTR and RTS lines. If hard flow control
		 * is active, the changes will be saved in the mstat structure
		 * and only asserted when hard flow control is turned off.
		 */

		/* First reset DTR & RTS; then set them */
		fepcmd(ch, SETMODEM, 0, ((ch->m_dtr)|(ch->m_rts)), 0, 1);
		fepcmd(ch, SETMODEM, mval, 0, 0, 1);
	}
	if (ch->startc != ch->fepstartc || ch->stopc != ch->fepstopc)  {
		ch->fepstartc = ch->startc;
		ch->fepstopc = ch->stopc;
		/*
		 * The XON / XOFF characters have changed; propagate these
		 * changes to the card.
		 */
		fepcmd(ch, SONOFFC, ch->fepstartc, ch->fepstopc, 0, 1);
	}
	if (ch->startca != ch->fepstartca || ch->stopca != ch->fepstopca)  {
		ch->fepstartca = ch->startca;
		ch->fepstopca = ch->stopca;
		/*
		 * Similar to the above, this time the auxilarly XON / XOFF
		 * characters have changed; propagate these changes to the card.
		 */
		fepcmd(ch, SAUXONOFFC, ch->fepstartca, ch->fepstopca, 0, 1);
	}
}

/* Caller holds lock */
static void receive_data(struct channel *ch)
{
	unchar *rptr;
	struct ktermios *ts = NULL;
	struct tty_struct *tty;
	struct board_chan __iomem *bc;
	int dataToRead, wrapgap, bytesAvailable;
	unsigned int tail, head;
	unsigned int wrapmask;

	/*
	 * This routine is called by doint when a receive data event has taken
	 * place.
	 */
	globalwinon(ch);
	if (ch->statusflags & RXSTOPPED)
		return;
	tty = ch->tty;
	if (tty)
		ts = tty->termios;
	bc = ch->brdchan;
	BUG_ON(!bc);
	wrapmask = ch->rxbufsize - 1;

	/*
	 * Get the head and tail pointers to the receiver queue. Wrap the head
	 * pointer if it has reached the end of the buffer.
	 */
	head = readw(&bc->rin);
	head &= wrapmask;
	tail = readw(&bc->rout) & wrapmask;

	bytesAvailable = (head - tail) & wrapmask;
	if (bytesAvailable == 0)
		return;

	/* If CREAD bit is off or device not open, set TX tail to head */
	if (!tty || !ts || !(ts->c_cflag & CREAD))  {
		writew(head, &bc->rout);
		return;
	}

	if (tty_buffer_request_room(tty, bytesAvailable + 1) == 0)
		return;

	if (readb(&bc->orun)) {
		writeb(0, &bc->orun);
		printk(KERN_WARNING "epca; overrun! DigiBoard device %s\n",tty->name);
		tty_insert_flip_char(tty, 0, TTY_OVERRUN);
	}
	rxwinon(ch);
	while (bytesAvailable > 0)  { /* Begin while there is data on the card */
		wrapgap = (head >= tail) ? head - tail : ch->rxbufsize - tail;
		/*
		 * Even if head has wrapped around only report the amount of
		 * data to be equal to the size - tail. Remember memcpy can't
		 * automaticly wrap around the receive buffer.
		 */
		dataToRead = (wrapgap < bytesAvailable) ? wrapgap : bytesAvailable;
		/* Make sure we don't overflow the buffer */
		dataToRead = tty_prepare_flip_string(tty, &rptr, dataToRead);
		if (dataToRead == 0)
			break;
		/*
		 * Move data read from our card into the line disciplines
		 * buffer for translation if necessary.
		 */
		memcpy_fromio(rptr, ch->rxptr + tail, dataToRead);
		tail = (tail + dataToRead) & wrapmask;
		bytesAvailable -= dataToRead;
	} /* End while there is data on the card */
	globalwinon(ch);
	writew(tail, &bc->rout);
	/* Must be called with global data */
	tty_schedule_flip(ch->tty);
}

static int info_ioctl(struct tty_struct *tty, struct file *file,
		    unsigned int cmd, unsigned long arg)
{
	switch (cmd) {
	case DIGI_GETINFO:
		{
			struct digi_info di;
			int brd;

			if (get_user(brd, (unsigned int __user *)arg))
				return -EFAULT;
			if (brd < 0 || brd >= num_cards || num_cards == 0)
				return -ENODEV;

			memset(&di, 0, sizeof(di));

			di.board = brd;
			di.status = boards[brd].status;
			di.type = boards[brd].type ;
			di.numports = boards[brd].numports ;
			/* Legacy fixups - just move along nothing to see */
			di.port = (unsigned char *)boards[brd].port ;
			di.membase = (unsigned char *)boards[brd].membase ;

			if (copy_to_user((void __user *)arg, &di, sizeof(di)))
				return -EFAULT;
			break;

		}

	case DIGI_POLLER:
		{
			int brd = arg & 0xff000000 >> 16;
			unsigned char state = arg & 0xff;

			if (brd < 0 || brd >= num_cards) {
				printk(KERN_ERR "epca: DIGI POLLER : brd not valid!\n");
				return -ENODEV;
			}
			digi_poller_inhibited = state;
			break;
		}

	case DIGI_INIT:
		{
			/*
			 * This call is made by the apps to complete the
			 * initilization of the board(s). This routine is
			 * responsible for setting the card to its initial
			 * state and setting the drivers control fields to the
			 * sutianle settings for the card in question.
			 */
			int crd;
			for (crd = 0; crd < num_cards; crd++)
				post_fep_init(crd);
			break;
		}
	default:
		return -ENOTTY;
	}
	return 0;
}

static int pc_tiocmget(struct tty_struct *tty, struct file *file)
{
	struct channel *ch = (struct channel *) tty->driver_data;
	struct board_chan __iomem *bc;
	unsigned int mstat, mflag = 0;
	unsigned long flags;

	if (ch)
		bc = ch->brdchan;
	else
		return -EINVAL;

	spin_lock_irqsave(&epca_lock, flags);
	globalwinon(ch);
	mstat = readb(&bc->mstat);
	memoff(ch);
	spin_unlock_irqrestore(&epca_lock, flags);

	if (mstat & ch->m_dtr)
		mflag |= TIOCM_DTR;
	if (mstat & ch->m_rts)
		mflag |= TIOCM_RTS;
	if (mstat & ch->m_cts)
		mflag |= TIOCM_CTS;
	if (mstat & ch->dsr)
		mflag |= TIOCM_DSR;
	if (mstat & ch->m_ri)
		mflag |= TIOCM_RI;
	if (mstat & ch->dcd)
		mflag |= TIOCM_CD;
	return mflag;
}

static int pc_tiocmset(struct tty_struct *tty, struct file *file,
		       unsigned int set, unsigned int clear)
{
	struct channel *ch = (struct channel *) tty->driver_data;
	unsigned long flags;

	if (!ch)
		return -EINVAL;

	spin_lock_irqsave(&epca_lock, flags);
	/*
	 * I think this modemfake stuff is broken. It doesn't correctly reflect
	 * the behaviour desired by the TIOCM* ioctls. Therefore this is
	 * probably broken.
	 */
	if (set & TIOCM_RTS) {
		ch->modemfake |= ch->m_rts;
		ch->modem |= ch->m_rts;
	}
	if (set & TIOCM_DTR) {
		ch->modemfake |= ch->m_dtr;
		ch->modem |= ch->m_dtr;
	}
	if (clear & TIOCM_RTS) {
		ch->modemfake |= ch->m_rts;
		ch->modem &= ~ch->m_rts;
	}
	if (clear & TIOCM_DTR) {
		ch->modemfake |= ch->m_dtr;
		ch->modem &= ~ch->m_dtr;
	}
	globalwinon(ch);
	/*
	 * The below routine generally sets up parity, baud, flow control
	 * issues, etc.... It effect both control flags and input flags.
	 */
	epcaparam(tty,ch);
	memoff(ch);
	spin_unlock_irqrestore(&epca_lock, flags);
	return 0;
}

static int pc_ioctl(struct tty_struct *tty, struct file * file,
		    unsigned int cmd, unsigned long arg)
{
	digiflow_t dflow;
	int retval;
	unsigned long flags;
	unsigned int mflag, mstat;
	unsigned char startc, stopc;
	struct board_chan __iomem *bc;
	struct channel *ch = (struct channel *) tty->driver_data;
	void __user *argp = (void __user *)arg;

	if (ch)
		bc = ch->brdchan;
	else
		return -EINVAL;

	/*
	 * For POSIX compliance we need to add more ioctls. See tty_ioctl.c in
	 * /usr/src/linux/drivers/char for a good example. In particular think
	 * about adding TCSETAF, TCSETAW, TCSETA, TCSETSF, TCSETSW, TCSETS.
	 */
	switch (cmd) {
	case TCSBRK:	/* SVID version: non-zero arg --> no break */
		retval = tty_check_change(tty);
		if (retval)
			return retval;
		/* Setup an event to indicate when the transmit buffer empties */
		spin_lock_irqsave(&epca_lock, flags);
		setup_empty_event(tty,ch);
		spin_unlock_irqrestore(&epca_lock, flags);
		tty_wait_until_sent(tty, 0);
		if (!arg)
			digi_send_break(ch, HZ / 4);    /* 1/4 second */
		return 0;
	case TCSBRKP:	/* support for POSIX tcsendbreak() */
		retval = tty_check_change(tty);
		if (retval)
			return retval;

		/* Setup an event to indicate when the transmit buffer empties */
		spin_lock_irqsave(&epca_lock, flags);
		setup_empty_event(tty,ch);
		spin_unlock_irqrestore(&epca_lock, flags);
		tty_wait_until_sent(tty, 0);
		digi_send_break(ch, arg ? arg*(HZ/10) : HZ/4);
		return 0;
	case TIOCGSOFTCAR:
		if (put_user(C_CLOCAL(tty)?1:0, (unsigned long __user *)arg))
			return -EFAULT;
		return 0;
	case TIOCSSOFTCAR:
		{
			unsigned int value;

			if (get_user(value, (unsigned __user *)argp))
				return -EFAULT;
			tty->termios->c_cflag =
				((tty->termios->c_cflag & ~CLOCAL) |
				 (value ? CLOCAL : 0));
			return 0;
		}
	case TIOCMODG:
		mflag = pc_tiocmget(tty, file);
		if (put_user(mflag, (unsigned long __user *)argp))
			return -EFAULT;
		break;
	case TIOCMODS:
		if (get_user(mstat, (unsigned __user *)argp))
			return -EFAULT;
		return pc_tiocmset(tty, file, mstat, ~mstat);
	case TIOCSDTR:
		spin_lock_irqsave(&epca_lock, flags);
		ch->omodem |= ch->m_dtr;
		globalwinon(ch);
		fepcmd(ch, SETMODEM, ch->m_dtr, 0, 10, 1);
		memoff(ch);
		spin_unlock_irqrestore(&epca_lock, flags);
		break;

	case TIOCCDTR:
		spin_lock_irqsave(&epca_lock, flags);
		ch->omodem &= ~ch->m_dtr;
		globalwinon(ch);
		fepcmd(ch, SETMODEM, 0, ch->m_dtr, 10, 1);
		memoff(ch);
		spin_unlock_irqrestore(&epca_lock, flags);
		break;
	case DIGI_GETA:
		if (copy_to_user(argp, &ch->digiext, sizeof(digi_t)))
			return -EFAULT;
		break;
	case DIGI_SETAW:
	case DIGI_SETAF:
		if (cmd == DIGI_SETAW) {
			/* Setup an event to indicate when the transmit buffer empties */
			spin_lock_irqsave(&epca_lock, flags);
			setup_empty_event(tty,ch);
			spin_unlock_irqrestore(&epca_lock, flags);
			tty_wait_until_sent(tty, 0);
		} else {
			/* ldisc lock already held in ioctl */
			if (tty->ldisc.flush_buffer)
				tty->ldisc.flush_buffer(tty);
		}
		/* Fall Thru */
	case DIGI_SETA:
		if (copy_from_user(&ch->digiext, argp, sizeof(digi_t)))
			return -EFAULT;

		if (ch->digiext.digi_flags & DIGI_ALTPIN)  {
			ch->dcd = ch->m_dsr;
			ch->dsr = ch->m_dcd;
		} else {
			ch->dcd = ch->m_dcd;
			ch->dsr = ch->m_dsr;
			}

		spin_lock_irqsave(&epca_lock, flags);
		globalwinon(ch);

		/*
		 * The below routine generally sets up parity, baud, flow
		 * control issues, etc.... It effect both control flags and
		 * input flags.
		 */
		epcaparam(tty,ch);
		memoff(ch);
		spin_unlock_irqrestore(&epca_lock, flags);
		break;

	case DIGI_GETFLOW:
	case DIGI_GETAFLOW:
		spin_lock_irqsave(&epca_lock, flags);
		globalwinon(ch);
		if (cmd == DIGI_GETFLOW) {
			dflow.startc = readb(&bc->startc);
			dflow.stopc = readb(&bc->stopc);
		} else {
			dflow.startc = readb(&bc->startca);
			dflow.stopc = readb(&bc->stopca);
		}
		memoff(ch);
		spin_unlock_irqrestore(&epca_lock, flags);

		if (copy_to_user(argp, &dflow, sizeof(dflow)))
			return -EFAULT;
		break;

	case DIGI_SETAFLOW:
	case DIGI_SETFLOW:
		if (cmd == DIGI_SETFLOW) {
			startc = ch->startc;
			stopc = ch->stopc;
		} else {
			startc = ch->startca;
			stopc = ch->stopca;
		}

		if (copy_from_user(&dflow, argp, sizeof(dflow)))
			return -EFAULT;

		if (dflow.startc != startc || dflow.stopc != stopc) { /* Begin  if setflow toggled */
			spin_lock_irqsave(&epca_lock, flags);
			globalwinon(ch);

			if (cmd == DIGI_SETFLOW) {
				ch->fepstartc = ch->startc = dflow.startc;
				ch->fepstopc = ch->stopc = dflow.stopc;
				fepcmd(ch, SONOFFC, ch->fepstartc, ch->fepstopc, 0, 1);
			} else {
				ch->fepstartca = ch->startca = dflow.startc;
				ch->fepstopca  = ch->stopca = dflow.stopc;
				fepcmd(ch, SAUXONOFFC, ch->fepstartca, ch->fepstopca, 0, 1);
			}

			if (ch->statusflags & TXSTOPPED)
				pc_start(tty);

			memoff(ch);
			spin_unlock_irqrestore(&epca_lock, flags);
		} /* End if setflow toggled */
		break;
	default:
		return -ENOIOCTLCMD;
	}
	return 0;
}

static void pc_set_termios(struct tty_struct *tty, struct ktermios *old_termios)
{
	struct channel *ch;
	unsigned long flags;
	/*
	 * verifyChannel returns the channel from the tty struct if it is
	 * valid. This serves as a sanity check.
	 */
	if ((ch = verifyChannel(tty)) != NULL)  { /* Begin if channel valid */
		spin_lock_irqsave(&epca_lock, flags);
		globalwinon(ch);
		epcaparam(tty, ch);
		memoff(ch);
		spin_unlock_irqrestore(&epca_lock, flags);

		if ((old_termios->c_cflag & CRTSCTS) &&
			 ((tty->termios->c_cflag & CRTSCTS) == 0))
			tty->hw_stopped = 0;

		if (!(old_termios->c_cflag & CLOCAL) &&
			 (tty->termios->c_cflag & CLOCAL))
			wake_up_interruptible(&ch->open_wait);

	} /* End if channel valid */
}

static void do_softint(struct work_struct *work)
{
	struct channel *ch = container_of(work, struct channel, tqueue);
	/* Called in response to a modem change event */
	if (ch && ch->magic == EPCA_MAGIC) {
		struct tty_struct *tty = ch->tty;

		if (tty && tty->driver_data) {
			if (test_and_clear_bit(EPCA_EVENT_HANGUP, &ch->event)) {
				tty_hangup(tty);	/* FIXME: module removal race here - AKPM */
				wake_up_interruptible(&ch->open_wait);
				ch->asyncflags &= ~ASYNC_NORMAL_ACTIVE;
			}
		}
	}
}

/*
 * pc_stop and pc_start provide software flow control to the routine and the
 * pc_ioctl routine.
 */
static void pc_stop(struct tty_struct *tty)
{
	struct channel *ch;
	unsigned long flags;
	/*
	 * verifyChannel returns the channel from the tty struct if it is
	 * valid. This serves as a sanity check.
	 */
	if ((ch = verifyChannel(tty)) != NULL) {
		spin_lock_irqsave(&epca_lock, flags);
		if ((ch->statusflags & TXSTOPPED) == 0) { /* Begin if transmit stop requested */
			globalwinon(ch);
			/* STOP transmitting now !! */
			fepcmd(ch, PAUSETX, 0, 0, 0, 0);
			ch->statusflags |= TXSTOPPED;
			memoff(ch);
		} /* End if transmit stop requested */
		spin_unlock_irqrestore(&epca_lock, flags);
	}
}

static void pc_start(struct tty_struct *tty)
{
	struct channel *ch;
	/*
	 * verifyChannel returns the channel from the tty struct if it is
	 * valid. This serves as a sanity check.
	 */
	if ((ch = verifyChannel(tty)) != NULL) {
		unsigned long flags;
		spin_lock_irqsave(&epca_lock, flags);
		/* Just in case output was resumed because of a change in Digi-flow */
		if (ch->statusflags & TXSTOPPED)  { /* Begin transmit resume requested */
			struct board_chan __iomem *bc;
			globalwinon(ch);
			bc = ch->brdchan;
			if (ch->statusflags & LOWWAIT)
				writeb(1, &bc->ilow);
			/* Okay, you can start transmitting again... */
			fepcmd(ch, RESUMETX, 0, 0, 0, 0);
			ch->statusflags &= ~TXSTOPPED;
			memoff(ch);
		} /* End transmit resume requested */
		spin_unlock_irqrestore(&epca_lock, flags);
	}
}

/*
 * The below routines pc_throttle and pc_unthrottle are used to slow (And
 * resume) the receipt of data into the kernels receive buffers. The exact
 * occurrence of this depends on the size of the kernels receive buffer and
 * what the 'watermarks' are set to for that buffer. See the n_ttys.c file for
 * more details.
 */
static void pc_throttle(struct tty_struct *tty)
{
	struct channel *ch;
	unsigned long flags;
	/*
	 * verifyChannel returns the channel from the tty struct if it is
	 * valid. This serves as a sanity check.
	 */
	if ((ch = verifyChannel(tty)) != NULL) {
		spin_lock_irqsave(&epca_lock, flags);
		if ((ch->statusflags & RXSTOPPED) == 0) {
			globalwinon(ch);
			fepcmd(ch, PAUSERX, 0, 0, 0, 0);
			ch->statusflags |= RXSTOPPED;
			memoff(ch);
		}
		spin_unlock_irqrestore(&epca_lock, flags);
	}
}

static void pc_unthrottle(struct tty_struct *tty)
{
	struct channel *ch;
	unsigned long flags;
	/*
	 * verifyChannel returns the channel from the tty struct if it is
	 * valid. This serves as a sanity check.
	 */
	if ((ch = verifyChannel(tty)) != NULL) {
		/* Just in case output was resumed because of a change in Digi-flow */
		spin_lock_irqsave(&epca_lock, flags);
		if (ch->statusflags & RXSTOPPED) {
			globalwinon(ch);
			fepcmd(ch, RESUMERX, 0, 0, 0, 0);
			ch->statusflags &= ~RXSTOPPED;
			memoff(ch);
		}
		spin_unlock_irqrestore(&epca_lock, flags);
	}
}

void digi_send_break(struct channel *ch, int msec)
{
	unsigned long flags;

	spin_lock_irqsave(&epca_lock, flags);
	globalwinon(ch);
	/*
	 * Maybe I should send an infinite break here, schedule() for msec
	 * amount of time, and then stop the break. This way, the user can't
	 * screw up the FEP by causing digi_send_break() to be called (i.e. via
	 * an ioctl()) more than once in msec amount of time.
	 * Try this for now...
	 */
	fepcmd(ch, SENDBREAK, msec, 0, 10, 0);
	memoff(ch);
	spin_unlock_irqrestore(&epca_lock, flags);
}

/* Caller MUST hold the lock */
static void setup_empty_event(struct tty_struct *tty, struct channel *ch)
{
	struct board_chan __iomem *bc = ch->brdchan;

	globalwinon(ch);
	ch->statusflags |= EMPTYWAIT;
	/*
	 * When set the iempty flag request a event to be generated when the
	 * transmit buffer is empty (If there is no BREAK in progress).
	 */
	writeb(1, &bc->iempty);
	memoff(ch);
}

void epca_setup(char *str, int *ints)
{
	struct board_info board;
	int               index, loop, last;
	char              *temp, *t2;
	unsigned          len;

	/*
	 * If this routine looks a little strange it is because it is only
	 * called if a LILO append command is given to boot the kernel with
	 * parameters. In this way, we can provide the user a method of
	 * changing his board configuration without rebuilding the kernel.
	 */
	if (!liloconfig)
		liloconfig = 1;

	memset(&board, 0, sizeof(board));

	/* Assume the data is int first, later we can change it */
	/* I think that array position 0 of ints holds the number of args */
	for (last = 0, index = 1; index <= ints[0]; index++)
		switch (index) { /* Begin parse switch */
		case 1:
			board.status = ints[index];
			/*
			 * We check for 2 (As opposed to 1; because 2 is a flag
			 * instructing the driver to ignore epcaconfig.) For
			 * this reason we check for 2.
			 */
			if (board.status == 2) { /* Begin ignore epcaconfig as well as lilo cmd line */
				nbdevs = 0;
				num_cards = 0;
				return;
			} /* End ignore epcaconfig as well as lilo cmd line */

			if (board.status > 2) {
				printk(KERN_ERR "epca_setup: Invalid board status 0x%x\n", board.status);
				invalid_lilo_config = 1;
				setup_error_code |= INVALID_BOARD_STATUS;
				return;
			}
			last = index;
			break;
		case 2:
			board.type = ints[index];
			if (board.type >= PCIXEM)  {
				printk(KERN_ERR "epca_setup: Invalid board type 0x%x\n", board.type);
				invalid_lilo_config = 1;
				setup_error_code |= INVALID_BOARD_TYPE;
				return;
			}
			last = index;
			break;
		case 3:
			board.altpin = ints[index];
			if (board.altpin > 1) {
				printk(KERN_ERR "epca_setup: Invalid board altpin 0x%x\n", board.altpin);
				invalid_lilo_config = 1;
				setup_error_code |= INVALID_ALTPIN;
				return;
			}
			last = index;
			break;

		case 4:
			board.numports = ints[index];
			if (board.numports < 2 || board.numports > 256) {
				printk(KERN_ERR "epca_setup: Invalid board numports 0x%x\n", board.numports);
				invalid_lilo_config = 1;
				setup_error_code |= INVALID_NUM_PORTS;
				return;
			}
			nbdevs += board.numports;
			last = index;
			break;

		case 5:
			board.port = ints[index];
			if (ints[index] <= 0) {
				printk(KERN_ERR "epca_setup: Invalid io port 0x%x\n", (unsigned int)board.port);
				invalid_lilo_config = 1;
				setup_error_code |= INVALID_PORT_BASE;
				return;
			}
			last = index;
			break;

		case 6:
			board.membase = ints[index];
			if (ints[index] <= 0) {
				printk(KERN_ERR "epca_setup: Invalid memory base 0x%x\n",(unsigned int)board.membase);
				invalid_lilo_config = 1;
				setup_error_code |= INVALID_MEM_BASE;
				return;
			}
			last = index;
			break;

		default:
			printk(KERN_ERR "<Error> - epca_setup: Too many integer parms\n");
			return;

		} /* End parse switch */

	while (str && *str)  { /* Begin while there is a string arg */
		/* find the next comma or terminator */
		temp = str;
		/* While string is not null, and a comma hasn't been found */
		while (*temp && (*temp != ','))
			temp++;
		if (!*temp)
			temp = NULL;
		else
			*temp++ = 0;
		/* Set index to the number of args + 1 */
		index = last + 1;

		switch (index) {
		case 1:
			len = strlen(str);
			if (strncmp("Disable", str, len) == 0)
				board.status = 0;
			else if (strncmp("Enable", str, len) == 0)
				board.status = 1;
			else {
				printk(KERN_ERR "epca_setup: Invalid status %s\n", str);
				invalid_lilo_config = 1;
				setup_error_code |= INVALID_BOARD_STATUS;
				return;
			}
			last = index;
			break;

		case 2:
			for (loop = 0; loop < EPCA_NUM_TYPES; loop++)
				if (strcmp(board_desc[loop], str) == 0)
					break;
			/*
			 * If the index incremented above refers to a
			 * legitamate board type set it here.
			 */
			if (index < EPCA_NUM_TYPES)
				board.type = loop;
			else {
				printk(KERN_ERR "epca_setup: Invalid board type: %s\n", str);
				invalid_lilo_config = 1;
				setup_error_code |= INVALID_BOARD_TYPE;
				return;
			}
			last = index;
			break;

		case 3:
			len = strlen(str);
			if (strncmp("Disable", str, len) == 0)
				board.altpin = 0;
			else if (strncmp("Enable", str, len) == 0)
				board.altpin = 1;
			else {
				printk(KERN_ERR "epca_setup: Invalid altpin %s\n", str);
				invalid_lilo_config = 1;
				setup_error_code |= INVALID_ALTPIN;
				return;
			}
			last = index;
			break;

		case 4:
			t2 = str;
			while (isdigit(*t2))
				t2++;

			if (*t2) {
				printk(KERN_ERR "epca_setup: Invalid port count %s\n", str);
				invalid_lilo_config = 1;
				setup_error_code |= INVALID_NUM_PORTS;
				return;
			}

			/*
			 * There is not a man page for simple_strtoul but the
			 * code can be found in vsprintf.c. The first argument
			 * is the string to translate (To an unsigned long
			 * obviously), the second argument can be the address
			 * of any character variable or a NULL. If a variable
			 * is given, the end pointer of the string will be
			 * stored in that variable; if a NULL is given the end
			 * pointer will not be returned. The last argument is
			 * the base to use. If a 0 is indicated, the routine
			 * will attempt to determine the proper base by looking
			 * at the values prefix (A '0' for octal, a 'x' for
			 * hex, etc ... If a value is given it will use that
			 * value as the base.
			 */
			board.numports = simple_strtoul(str, NULL, 0);
			nbdevs += board.numports;
			last = index;
			break;

		case 5:
			t2 = str;
			while (isxdigit(*t2))
				t2++;

			if (*t2) {
				printk(KERN_ERR "epca_setup: Invalid i/o address %s\n", str);
				invalid_lilo_config = 1;
				setup_error_code |= INVALID_PORT_BASE;
				return;
			}

			board.port = simple_strtoul(str, NULL, 16);
			last = index;
			break;

		case 6:
			t2 = str;
			while (isxdigit(*t2))
				t2++;

			if (*t2) {
				printk(KERN_ERR "epca_setup: Invalid memory base %s\n",str);
				invalid_lilo_config = 1;
				setup_error_code |= INVALID_MEM_BASE;
				return;
			}
			board.membase = simple_strtoul(str, NULL, 16);
			last = index;
			break;
		default:
			printk(KERN_ERR "epca: Too many string parms\n");
			return;
		}
		str = temp;
	} /* End while there is a string arg */

	if (last < 6) {
		printk(KERN_ERR "epca: Insufficient parms specified\n");
		return;
	}

	/* I should REALLY validate the stuff here */
	/* Copies our local copy of board into boards */
	memcpy((void *)&boards[num_cards],(void *)&board, sizeof(board));
	/* Does this get called once per lilo arg are what ? */
	printk(KERN_INFO "PC/Xx: Added board %i, %s %i ports at 0x%4.4X base 0x%6.6X\n",
		num_cards, board_desc[board.type],
		board.numports, (int)board.port, (unsigned int) board.membase);
	num_cards++;
}

enum epic_board_types {
	brd_xr = 0,
	brd_xem,
	brd_cx,
	brd_xrj,
};

/* indexed directly by epic_board_types enum */
static struct {
	unsigned char board_type;
	unsigned bar_idx;		/* PCI base address region */
} epca_info_tbl[] = {
	{ PCIXR, 0, },
	{ PCIXEM, 0, },
	{ PCICX, 0, },
	{ PCIXRJ, 2, },
};

static int __devinit epca_init_one(struct pci_dev *pdev,
				 const struct pci_device_id *ent)
{
	static int board_num = -1;
	int board_idx, info_idx = ent->driver_data;
	unsigned long addr;

	if (pci_enable_device(pdev))
		return -EIO;

	board_num++;
	board_idx = board_num + num_cards;
	if (board_idx >= MAXBOARDS)
		goto err_out;

	addr = pci_resource_start (pdev, epca_info_tbl[info_idx].bar_idx);
	if (!addr) {
		printk (KERN_ERR PFX "PCI region #%d not available (size 0)\n",
			epca_info_tbl[info_idx].bar_idx);
		goto err_out;
	}

	boards[board_idx].status = ENABLED;
	boards[board_idx].type = epca_info_tbl[info_idx].board_type;
	boards[board_idx].numports = 0x0;
	boards[board_idx].port = addr + PCI_IO_OFFSET;
	boards[board_idx].membase = addr;

	if (!request_mem_region (addr + PCI_IO_OFFSET, 0x200000, "epca")) {
		printk (KERN_ERR PFX "resource 0x%x @ 0x%lx unavailable\n",
			0x200000, addr + PCI_IO_OFFSET);
		goto err_out;
	}

	boards[board_idx].re_map_port = ioremap(addr + PCI_IO_OFFSET, 0x200000);
	if (!boards[board_idx].re_map_port) {
		printk (KERN_ERR PFX "cannot map 0x%x @ 0x%lx\n",
			0x200000, addr + PCI_IO_OFFSET);
		goto err_out_free_pciio;
	}

	if (!request_mem_region (addr, 0x200000, "epca")) {
		printk (KERN_ERR PFX "resource 0x%x @ 0x%lx unavailable\n",
			0x200000, addr);
		goto err_out_free_iounmap;
	}

	boards[board_idx].re_map_membase = ioremap(addr, 0x200000);
	if (!boards[board_idx].re_map_membase) {
		printk (KERN_ERR PFX "cannot map 0x%x @ 0x%lx\n",
			0x200000, addr + PCI_IO_OFFSET);
		goto err_out_free_memregion;
	}

	/*
	 * I don't know what the below does, but the hardware guys say its
	 * required on everything except PLX (In this case XRJ).
	 */
	if (info_idx != brd_xrj) {
		pci_write_config_byte(pdev, 0x40, 0);
		pci_write_config_byte(pdev, 0x46, 0);
	}

	return 0;

err_out_free_memregion:
	release_mem_region (addr, 0x200000);
err_out_free_iounmap:
	iounmap (boards[board_idx].re_map_port);
err_out_free_pciio:
	release_mem_region (addr + PCI_IO_OFFSET, 0x200000);
err_out:
	return -ENODEV;
}


static struct pci_device_id epca_pci_tbl[] = {
	{ PCI_VENDOR_DIGI, PCI_DEVICE_XR, PCI_ANY_ID, PCI_ANY_ID, 0, 0, brd_xr },
	{ PCI_VENDOR_DIGI, PCI_DEVICE_XEM, PCI_ANY_ID, PCI_ANY_ID, 0, 0, brd_xem },
	{ PCI_VENDOR_DIGI, PCI_DEVICE_CX, PCI_ANY_ID, PCI_ANY_ID, 0, 0, brd_cx },
	{ PCI_VENDOR_DIGI, PCI_DEVICE_XRJ, PCI_ANY_ID, PCI_ANY_ID, 0, 0, brd_xrj },
	{ 0, }
};

MODULE_DEVICE_TABLE(pci, epca_pci_tbl);

int __init init_PCI (void)
{
	memset (&epca_driver, 0, sizeof (epca_driver));
	epca_driver.name = "epca";
	epca_driver.id_table = epca_pci_tbl;
	epca_driver.probe = epca_init_one;

	return pci_register_driver(&epca_driver);
}

MODULE_LICENSE("GPL");
