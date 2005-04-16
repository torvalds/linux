/*

 
	Copyright (C) 1996  Digi International.
 
	For technical support please email digiLinux@dgii.com or
	call Digi tech support at (612) 912-3456

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

--------------------------------------------------------------------------- */
/* See README.epca for change history --DAT*/


#include <linux/config.h>
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

#ifdef CONFIG_PCI
#define ENABLE_PCI
#endif /* CONFIG_PCI */

#define putUser(arg1, arg2) put_user(arg1, (unsigned long __user *)arg2)
#define getUser(arg1, arg2) get_user(arg1, (unsigned __user *)arg2)

#ifdef ENABLE_PCI
#include <linux/pci.h>
#include "digiPCI.h"
#endif /* ENABLE_PCI */

#include "digi1.h"
#include "digiFep1.h"
#include "epca.h"
#include "epcaconfig.h"

#if BITS_PER_LONG != 32
#  error FIXME: this driver only works on 32-bit platforms
#endif

/* ---------------------- Begin defines ------------------------ */

#define VERSION            "1.3.0.1-LK"

/* This major needs to be submitted to Linux to join the majors list */

#define DIGIINFOMAJOR       35  /* For Digi specific ioctl */ 


#define MAXCARDS 7
#define epcaassert(x, msg)  if (!(x)) epca_error(__LINE__, msg)

#define PFX "epca: "

/* ----------------- Begin global definitions ------------------- */

static char mesg[100];
static int nbdevs, num_cards, liloconfig;
static int digi_poller_inhibited = 1 ;

static int setup_error_code;
static int invalid_lilo_config;

/* -----------------------------------------------------------------------
	MAXBOARDS is typically 12, but ISA and EISA cards are restricted to 
	7 below.
--------------------------------------------------------------------------*/
static struct board_info boards[MAXBOARDS];


/* ------------- Begin structures used for driver registeration ---------- */

static struct tty_driver *pc_driver;
static struct tty_driver *pc_info;

/* ------------------ Begin Digi specific structures -------------------- */

/* ------------------------------------------------------------------------
	digi_channels represents an array of structures that keep track of
	each channel of the Digi product.  Information such as transmit and
	receive pointers, termio data, and signal definitions (DTR, CTS, etc ...)
	are stored here.  This structure is NOT used to overlay the cards 
	physical channel structure.
-------------------------------------------------------------------------- */
  
static struct channel digi_channels[MAX_ALLOC];

/* ------------------------------------------------------------------------
	card_ptr is an array used to hold the address of the
	first channel structure of each card.  This array will hold
	the addresses of various channels located in digi_channels.
-------------------------------------------------------------------------- */
static struct channel *card_ptr[MAXCARDS];

static struct timer_list epca_timer;

/* ---------------------- Begin function prototypes --------------------- */

/* ----------------------------------------------------------------------
	Begin generic memory functions.  These functions will be alias
	(point at) more specific functions dependent on the board being
	configured.
----------------------------------------------------------------------- */
	
static inline void memwinon(struct board_info *b, unsigned int win);
static inline void memwinoff(struct board_info *b, unsigned int win);
static inline void globalwinon(struct channel *ch);
static inline void rxwinon(struct channel *ch);
static inline void txwinon(struct channel *ch);
static inline void memoff(struct channel *ch);
static inline void assertgwinon(struct channel *ch);
static inline void assertmemoff(struct channel *ch);

/* ---- Begin more 'specific' memory functions for cx_like products --- */

static inline void pcxem_memwinon(struct board_info *b, unsigned int win);
static inline void pcxem_memwinoff(struct board_info *b, unsigned int win);
static inline void pcxem_globalwinon(struct channel *ch);
static inline void pcxem_rxwinon(struct channel *ch);
static inline void pcxem_txwinon(struct channel *ch);
static inline void pcxem_memoff(struct channel *ch);

/* ------ Begin more 'specific' memory functions for the pcxe ------- */

static inline void pcxe_memwinon(struct board_info *b, unsigned int win);
static inline void pcxe_memwinoff(struct board_info *b, unsigned int win);
static inline void pcxe_globalwinon(struct channel *ch);
static inline void pcxe_rxwinon(struct channel *ch);
static inline void pcxe_txwinon(struct channel *ch);
static inline void pcxe_memoff(struct channel *ch);

/* ---- Begin more 'specific' memory functions for the pc64xe and pcxi ---- */
/* Note : pc64xe and pcxi share the same windowing routines */

static inline void pcxi_memwinon(struct board_info *b, unsigned int win);
static inline void pcxi_memwinoff(struct board_info *b, unsigned int win);
static inline void pcxi_globalwinon(struct channel *ch);
static inline void pcxi_rxwinon(struct channel *ch);
static inline void pcxi_txwinon(struct channel *ch);
static inline void pcxi_memoff(struct channel *ch);

/* - Begin 'specific' do nothing memory functions needed for some cards - */

static inline void dummy_memwinon(struct board_info *b, unsigned int win);
static inline void dummy_memwinoff(struct board_info *b, unsigned int win);
static inline void dummy_globalwinon(struct channel *ch);
static inline void dummy_rxwinon(struct channel *ch);
static inline void dummy_txwinon(struct channel *ch);
static inline void dummy_memoff(struct channel *ch);
static inline void dummy_assertgwinon(struct channel *ch);
static inline void dummy_assertmemoff(struct channel *ch);

/* ------------------- Begin declare functions ----------------------- */

static inline struct channel *verifyChannel(register struct tty_struct *);
static inline void pc_sched_event(struct channel *, int);
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
static void pc_set_termios(struct tty_struct *, struct termios *);
static void do_softint(void *);
static void pc_stop(struct tty_struct *);
static void pc_start(struct tty_struct *);
static void pc_throttle(struct tty_struct * tty);
static void pc_unthrottle(struct tty_struct *tty);
static void digi_send_break(struct channel *ch, int msec);
static void setup_empty_event(struct tty_struct *tty, struct channel *ch);
void epca_setup(char *, int *);
void console_print(const char *);

static int get_termio(struct tty_struct *, struct termio __user *);
static int pc_write(struct tty_struct *, const unsigned char *, int);
int pc_init(void);

#ifdef ENABLE_PCI
static int init_PCI(void);
#endif /* ENABLE_PCI */


/* ------------------------------------------------------------------
	Table of functions for each board to handle memory.  Mantaining 
	parallelism is a *very* good idea here.  The idea is for the 
	runtime code to blindly call these functions, not knowing/caring    
	about the underlying hardware.  This stuff should contain no
	conditionals; if more functionality is needed a different entry
	should be established.  These calls are the interface calls and 
	are the only functions that should be accessed.  Anyone caught
	making direct calls deserves what they get.
-------------------------------------------------------------------- */

static inline void memwinon(struct board_info *b, unsigned int win)
{
	(b->memwinon)(b, win);
}

static inline void memwinoff(struct board_info *b, unsigned int win)
{
	(b->memwinoff)(b, win);
}

static inline void globalwinon(struct channel *ch)
{
	(ch->board->globalwinon)(ch);
}

static inline void rxwinon(struct channel *ch)
{
	(ch->board->rxwinon)(ch);
}

static inline void txwinon(struct channel *ch)
{
	(ch->board->txwinon)(ch);
}

static inline void memoff(struct channel *ch)
{
	(ch->board->memoff)(ch);
}
static inline void assertgwinon(struct channel *ch)
{
	(ch->board->assertgwinon)(ch);
}

static inline void assertmemoff(struct channel *ch)
{
	(ch->board->assertmemoff)(ch);
}

/* ---------------------------------------------------------
	PCXEM windowing is the same as that used in the PCXR 
	and CX series cards.
------------------------------------------------------------ */

static inline void pcxem_memwinon(struct board_info *b, unsigned int win)
{
        outb_p(FEPWIN|win, (int)b->port + 1);
}

static inline void pcxem_memwinoff(struct board_info *b, unsigned int win)
{
	outb_p(0, (int)b->port + 1);
}

static inline void pcxem_globalwinon(struct channel *ch)
{
	outb_p( FEPWIN, (int)ch->board->port + 1);
}

static inline void pcxem_rxwinon(struct channel *ch)
{
	outb_p(ch->rxwin, (int)ch->board->port + 1);
}

static inline void pcxem_txwinon(struct channel *ch)
{
	outb_p(ch->txwin, (int)ch->board->port + 1);
}

static inline void pcxem_memoff(struct channel *ch)
{
	outb_p(0, (int)ch->board->port + 1);
}

/* ----------------- Begin pcxe memory window stuff ------------------ */

static inline void pcxe_memwinon(struct board_info *b, unsigned int win)
{
               outb_p(FEPWIN | win, (int)b->port + 1);
}

static inline void pcxe_memwinoff(struct board_info *b, unsigned int win)
{
	outb_p(inb((int)b->port) & ~FEPMEM,
	           (int)b->port + 1);
	outb_p(0, (int)b->port + 1);
}

static inline void pcxe_globalwinon(struct channel *ch)
{
	outb_p( FEPWIN, (int)ch->board->port + 1);
}

static inline void pcxe_rxwinon(struct channel *ch)
{
		outb_p(ch->rxwin, (int)ch->board->port + 1);
}

static inline void pcxe_txwinon(struct channel *ch)
{
		outb_p(ch->txwin, (int)ch->board->port + 1);
}

static inline void pcxe_memoff(struct channel *ch)
{
	outb_p(0, (int)ch->board->port);
	outb_p(0, (int)ch->board->port + 1);
}

/* ------------- Begin pc64xe and pcxi memory window stuff -------------- */

static inline void pcxi_memwinon(struct board_info *b, unsigned int win)
{
               outb_p(inb((int)b->port) | FEPMEM, (int)b->port);
}

static inline void pcxi_memwinoff(struct board_info *b, unsigned int win)
{
	outb_p(inb((int)b->port) & ~FEPMEM, (int)b->port);
}

static inline void pcxi_globalwinon(struct channel *ch)
{
	outb_p(FEPMEM, (int)ch->board->port);
}

static inline void pcxi_rxwinon(struct channel *ch)
{
		outb_p(FEPMEM, (int)ch->board->port);
}

static inline void pcxi_txwinon(struct channel *ch)
{
		outb_p(FEPMEM, (int)ch->board->port);
}

static inline void pcxi_memoff(struct channel *ch)
{
	outb_p(0, (int)ch->board->port);
}

static inline void pcxi_assertgwinon(struct channel *ch)
{
	epcaassert(inb((int)ch->board->port) & FEPMEM, "Global memory off");
}

static inline void pcxi_assertmemoff(struct channel *ch)
{
	epcaassert(!(inb((int)ch->board->port) & FEPMEM), "Memory on");
}


/* ----------------------------------------------------------------------
	Not all of the cards need specific memory windowing routines.  Some
	cards (Such as PCI) needs no windowing routines at all.  We provide
	these do nothing routines so that the same code base can be used.
	The driver will ALWAYS call a windowing routine if it thinks it needs
	to; regardless of the card.  However, dependent on the card the routine
	may or may not do anything.
---------------------------------------------------------------------------*/

static inline void dummy_memwinon(struct board_info *b, unsigned int win)
{
}

static inline void dummy_memwinoff(struct board_info *b, unsigned int win)
{
}

static inline void dummy_globalwinon(struct channel *ch)
{
}

static inline void dummy_rxwinon(struct channel *ch)
{
}

static inline void dummy_txwinon(struct channel *ch)
{
}

static inline void dummy_memoff(struct channel *ch)
{
}

static inline void dummy_assertgwinon(struct channel *ch)
{
}

static inline void dummy_assertmemoff(struct channel *ch)
{
}

/* ----------------- Begin verifyChannel function ----------------------- */
static inline struct channel *verifyChannel(register struct tty_struct *tty)
{ /* Begin verifyChannel */

	/* --------------------------------------------------------------------
		This routine basically provides a sanity check.  It insures that
		the channel returned is within the proper range of addresses as
		well as properly initialized.  If some bogus info gets passed in
		through tty->driver_data this should catch it.
	--------------------------------------------------------------------- */

	if (tty) 
	{ /* Begin if tty */

		register struct channel *ch = (struct channel *)tty->driver_data;

		if ((ch >= &digi_channels[0]) && (ch < &digi_channels[nbdevs])) 
		{
			if (ch->magic == EPCA_MAGIC)
				return ch;
		}

	} /* End if tty */

	/* Else return a NULL for invalid */
	return NULL;

} /* End verifyChannel */

/* ------------------ Begin pc_sched_event ------------------------- */

static inline void pc_sched_event(struct channel *ch, int event)
{ /* Begin pc_sched_event */


	/* ----------------------------------------------------------------------
		We call this to schedule interrupt processing on some event.  The 
		kernel sees our request and calls the related routine in OUR driver.
	-------------------------------------------------------------------------*/

	ch->event |= 1 << event;
	schedule_work(&ch->tqueue);


} /* End pc_sched_event */

/* ------------------ Begin epca_error ------------------------- */

static void epca_error(int line, char *msg)
{ /* Begin epca_error */

	printk(KERN_ERR "epca_error (Digi): line = %d %s\n",line,msg);
	return;

} /* End epca_error */

/* ------------------ Begin pc_close ------------------------- */
static void pc_close(struct tty_struct * tty, struct file * filp)
{ /* Begin pc_close */

	struct channel *ch;
	unsigned long flags;

	/* ---------------------------------------------------------
		verifyChannel returns the channel from the tty struct
		if it is valid.  This serves as a sanity check.
	------------------------------------------------------------- */

	if ((ch = verifyChannel(tty)) != NULL) 
	{ /* Begin if ch != NULL */

		save_flags(flags);
		cli();

		if (tty_hung_up_p(filp)) 
		{
			restore_flags(flags);
			return;
		}

		/* Check to see if the channel is open more than once */
		if (ch->count-- > 1) 
		{ /* Begin channel is open more than once */

			/* -------------------------------------------------------------
				Return without doing anything.  Someone might still be using
				the channel.
			---------------------------------------------------------------- */

			restore_flags(flags);
			return;
		} /* End channel is open more than once */

		/* Port open only once go ahead with shutdown & reset */

		if (ch->count < 0) 
		{
			ch->count = 0;
		}

		/* ---------------------------------------------------------------
			Let the rest of the driver know the channel is being closed.
			This becomes important if an open is attempted before close 
			is finished.
		------------------------------------------------------------------ */

		ch->asyncflags |= ASYNC_CLOSING;
	
		tty->closing = 1;

		if (ch->asyncflags & ASYNC_INITIALIZED) 
		{
			/* Setup an event to indicate when the transmit buffer empties */
			setup_empty_event(tty, ch);		
			tty_wait_until_sent(tty, 3000); /* 30 seconds timeout */
		}
	
		if (tty->driver->flush_buffer)
			tty->driver->flush_buffer(tty);

		tty_ldisc_flush(tty);
		shutdown(ch);
		tty->closing = 0;
		ch->event = 0;
		ch->tty = NULL;

		if (ch->blocked_open) 
		{ /* Begin if blocked_open */

			if (ch->close_delay) 
			{
				msleep_interruptible(jiffies_to_msecs(ch->close_delay));
			}

			wake_up_interruptible(&ch->open_wait);

		} /* End if blocked_open */

		ch->asyncflags &= ~(ASYNC_NORMAL_ACTIVE | ASYNC_INITIALIZED | 
		                      ASYNC_CLOSING);
		wake_up_interruptible(&ch->close_wait);


		restore_flags(flags);

	} /* End if ch != NULL */

} /* End pc_close */ 

/* ------------------ Begin shutdown  ------------------------- */

static void shutdown(struct channel *ch)
{ /* Begin shutdown */

	unsigned long flags;
	struct tty_struct *tty;
	volatile struct board_chan *bc;

	if (!(ch->asyncflags & ASYNC_INITIALIZED)) 
		return;

	save_flags(flags);
	cli();
	globalwinon(ch);

	bc = ch->brdchan;

	/* ------------------------------------------------------------------
		In order for an event to be generated on the receipt of data the
		idata flag must be set. Since we are shutting down, this is not 
		necessary clear this flag.
	--------------------------------------------------------------------- */ 

	if (bc)
		bc->idata = 0;

	tty = ch->tty;

	/* ----------------------------------------------------------------
	   If we're a modem control device and HUPCL is on, drop RTS & DTR.
 	------------------------------------------------------------------ */

	if (tty->termios->c_cflag & HUPCL) 
	{
		ch->omodem &= ~(ch->m_rts | ch->m_dtr);
		fepcmd(ch, SETMODEM, 0, ch->m_dtr | ch->m_rts, 10, 1);
	}

	memoff(ch);

	/* ------------------------------------------------------------------
		The channel has officialy been closed.  The next time it is opened
		it will have to reinitialized.  Set a flag to indicate this.
	---------------------------------------------------------------------- */

	/* Prevent future Digi programmed interrupts from coming active */

	ch->asyncflags &= ~ASYNC_INITIALIZED;
	restore_flags(flags);

} /* End shutdown */

/* ------------------ Begin pc_hangup  ------------------------- */

static void pc_hangup(struct tty_struct *tty)
{ /* Begin pc_hangup */

	struct channel *ch;
	
	/* ---------------------------------------------------------
		verifyChannel returns the channel from the tty struct
		if it is valid.  This serves as a sanity check.
	------------------------------------------------------------- */

	if ((ch = verifyChannel(tty)) != NULL) 
	{ /* Begin if ch != NULL */

		unsigned long flags;

		save_flags(flags);
		cli();
		if (tty->driver->flush_buffer)
			tty->driver->flush_buffer(tty);
		tty_ldisc_flush(tty);
		shutdown(ch);

		ch->tty   = NULL;
		ch->event = 0;
		ch->count = 0;
		restore_flags(flags);
		ch->asyncflags &= ~(ASYNC_NORMAL_ACTIVE | ASYNC_INITIALIZED);
		wake_up_interruptible(&ch->open_wait);

	} /* End if ch != NULL */

} /* End pc_hangup */

/* ------------------ Begin pc_write  ------------------------- */

static int pc_write(struct tty_struct * tty,
                    const unsigned char *buf, int bytesAvailable)
{ /* Begin pc_write */

	register unsigned int head, tail;
	register int dataLen;
	register int size;
	register int amountCopied;


	struct channel *ch;
	unsigned long flags;
	int remain;
	volatile struct board_chan *bc;


	/* ----------------------------------------------------------------
		pc_write is primarily called directly by the kernel routine
		tty_write (Though it can also be called by put_char) found in
		tty_io.c.  pc_write is passed a line discipline buffer where 
		the data to be written out is stored.  The line discipline 
		implementation itself is done at the kernel level and is not 
		brought into the driver.  
	------------------------------------------------------------------- */

	/* ---------------------------------------------------------
		verifyChannel returns the channel from the tty struct
		if it is valid.  This serves as a sanity check.
	------------------------------------------------------------- */

	if ((ch = verifyChannel(tty)) == NULL)
		return 0;

	/* Make a pointer to the channel data structure found on the board. */

	bc   = ch->brdchan;
	size = ch->txbufsize;

	amountCopied = 0;
	save_flags(flags);
	cli();

	globalwinon(ch);

	head = bc->tin & (size - 1);
	tail = bc->tout;

	if (tail != bc->tout)
		tail = bc->tout;
	tail &= (size - 1);

	/*	If head >= tail, head has not wrapped around. */ 
	if (head >= tail) 
	{ /* Begin head has not wrapped */

		/* ---------------------------------------------------------------
			remain (much like dataLen above) represents the total amount of
			space available on the card for data.  Here dataLen represents
			the space existing between the head pointer and the end of 
			buffer.  This is important because a memcpy cannot be told to
			automatically wrap around when it hits the buffer end.
		------------------------------------------------------------------ */ 

		dataLen = size - head;
		remain = size - (head - tail) - 1;

	} /* End head has not wrapped */
	else 
	{ /* Begin head has wrapped around */

		remain = tail - head - 1;
		dataLen = remain;

	} /* End head has wrapped around */

	/* -------------------------------------------------------------------
			Check the space on the card.  If we have more data than 
			space; reduce the amount of data to fit the space.
	---------------------------------------------------------------------- */

	bytesAvailable = min(remain, bytesAvailable);

	txwinon(ch);
	while (bytesAvailable > 0) 
	{ /* Begin while there is data to copy onto card */

		/* -----------------------------------------------------------------
			If head is not wrapped, the below will make sure the first 
			data copy fills to the end of card buffer.
		------------------------------------------------------------------- */

		dataLen = min(bytesAvailable, dataLen);
		memcpy(ch->txptr + head, buf, dataLen);
		buf += dataLen;
		head += dataLen;
		amountCopied += dataLen;
		bytesAvailable -= dataLen;

		if (head >= size) 
		{
			head = 0;
			dataLen = tail;
		}

	} /* End while there is data to copy onto card */

	ch->statusflags |= TXBUSY;
	globalwinon(ch);
	bc->tin = head;

	if ((ch->statusflags & LOWWAIT) == 0) 
	{
		ch->statusflags |= LOWWAIT;
		bc->ilow = 1;
	}
	memoff(ch);
	restore_flags(flags);

	return(amountCopied);

} /* End pc_write */

/* ------------------ Begin pc_put_char  ------------------------- */

static void pc_put_char(struct tty_struct *tty, unsigned char c)
{ /* Begin pc_put_char */

   
	pc_write(tty, &c, 1);
	return;

} /* End pc_put_char */

/* ------------------ Begin pc_write_room  ------------------------- */

static int pc_write_room(struct tty_struct *tty)
{ /* Begin pc_write_room */

	int remain;
	struct channel *ch;
	unsigned long flags;
	unsigned int head, tail;
	volatile struct board_chan *bc;

	remain = 0;

	/* ---------------------------------------------------------
		verifyChannel returns the channel from the tty struct
		if it is valid.  This serves as a sanity check.
	------------------------------------------------------------- */

	if ((ch = verifyChannel(tty)) != NULL) 
	{
		save_flags(flags);
		cli();
		globalwinon(ch);

		bc   = ch->brdchan;
		head = bc->tin & (ch->txbufsize - 1);
		tail = bc->tout;

		if (tail != bc->tout)
			tail = bc->tout;
		/* Wrap tail if necessary */
		tail &= (ch->txbufsize - 1);

		if ((remain = tail - head - 1) < 0 )
			remain += ch->txbufsize;

		if (remain && (ch->statusflags & LOWWAIT) == 0) 
		{
			ch->statusflags |= LOWWAIT;
			bc->ilow = 1;
		}
		memoff(ch);
		restore_flags(flags);
	}

	/* Return how much room is left on card */
	return remain;

} /* End pc_write_room */

/* ------------------ Begin pc_chars_in_buffer  ---------------------- */

static int pc_chars_in_buffer(struct tty_struct *tty)
{ /* Begin pc_chars_in_buffer */

	int chars;
	unsigned int ctail, head, tail;
	int remain;
	unsigned long flags;
	struct channel *ch;
	volatile struct board_chan *bc;


	/* ---------------------------------------------------------
		verifyChannel returns the channel from the tty struct
		if it is valid.  This serves as a sanity check.
	------------------------------------------------------------- */

	if ((ch = verifyChannel(tty)) == NULL)
		return(0);

	save_flags(flags);
	cli();
	globalwinon(ch);

	bc = ch->brdchan;
	tail = bc->tout;
	head = bc->tin;
	ctail = ch->mailbox->cout;

	if (tail == head && ch->mailbox->cin == ctail && bc->tbusy == 0)
		chars = 0;
	else 
	{ /* Begin if some space on the card has been used */

		head = bc->tin & (ch->txbufsize - 1);
		tail &= (ch->txbufsize - 1);

		/*  --------------------------------------------------------------
			The logic here is basically opposite of the above pc_write_room
			here we are finding the amount of bytes in the buffer filled.
			Not the amount of bytes empty.
		------------------------------------------------------------------- */

		if ((remain = tail - head - 1) < 0 )
			remain += ch->txbufsize;

		chars = (int)(ch->txbufsize - remain);

		/* -------------------------------------------------------------  
			Make it possible to wakeup anything waiting for output
			in tty_ioctl.c, etc.

			If not already set.  Setup an event to indicate when the
			transmit buffer empties 
		----------------------------------------------------------------- */

		if (!(ch->statusflags & EMPTYWAIT))
			setup_empty_event(tty,ch);

	} /* End if some space on the card has been used */

	memoff(ch);
	restore_flags(flags);

	/* Return number of characters residing on card. */
	return(chars);

} /* End pc_chars_in_buffer */

/* ------------------ Begin pc_flush_buffer  ---------------------- */

static void pc_flush_buffer(struct tty_struct *tty)
{ /* Begin pc_flush_buffer */

	unsigned int tail;
	unsigned long flags;
	struct channel *ch;
	volatile struct board_chan *bc;


	/* ---------------------------------------------------------
		verifyChannel returns the channel from the tty struct
		if it is valid.  This serves as a sanity check.
	------------------------------------------------------------- */

	if ((ch = verifyChannel(tty)) == NULL)
		return;

	save_flags(flags);
	cli();

	globalwinon(ch);

	bc   = ch->brdchan;
	tail = bc->tout;

	/* Have FEP move tout pointer; effectively flushing transmit buffer */

	fepcmd(ch, STOUT, (unsigned) tail, 0, 0, 0);

	memoff(ch);
	restore_flags(flags);

	wake_up_interruptible(&tty->write_wait);
	tty_wakeup(tty);

} /* End pc_flush_buffer */

/* ------------------ Begin pc_flush_chars  ---------------------- */

static void pc_flush_chars(struct tty_struct *tty)
{ /* Begin pc_flush_chars */

	struct channel * ch;

	/* ---------------------------------------------------------
		verifyChannel returns the channel from the tty struct
		if it is valid.  This serves as a sanity check.
	------------------------------------------------------------- */

	if ((ch = verifyChannel(tty)) != NULL) 
	{
		unsigned long flags;

		save_flags(flags);
		cli();

		/* ----------------------------------------------------------------
			If not already set and the transmitter is busy setup an event
			to indicate when the transmit empties.
		------------------------------------------------------------------- */

		if ((ch->statusflags & TXBUSY) && !(ch->statusflags & EMPTYWAIT))
			setup_empty_event(tty,ch);

		restore_flags(flags);
	}

} /* End pc_flush_chars */

/* ------------------ Begin block_til_ready  ---------------------- */

static int block_til_ready(struct tty_struct *tty, 
                           struct file *filp, struct channel *ch)
{ /* Begin block_til_ready */

	DECLARE_WAITQUEUE(wait,current);
	int	retval, do_clocal = 0;
	unsigned long flags;


	if (tty_hung_up_p(filp))
	{
		if (ch->asyncflags & ASYNC_HUP_NOTIFY)
			retval = -EAGAIN;
		else
			retval = -ERESTARTSYS;	
		return(retval);
	}

	/* ----------------------------------------------------------------- 
		If the device is in the middle of being closed, then block
		until it's done, and then try again.
	-------------------------------------------------------------------- */
	if (ch->asyncflags & ASYNC_CLOSING) 
	{
		interruptible_sleep_on(&ch->close_wait);

		if (ch->asyncflags & ASYNC_HUP_NOTIFY)
			return -EAGAIN;
		else
			return -ERESTARTSYS;
	}

	if (filp->f_flags & O_NONBLOCK) 
	{
		/* ----------------------------------------------------------------- 
	  	 If non-blocking mode is set, then make the check up front
	  	 and then exit.
		-------------------------------------------------------------------- */

		ch->asyncflags |= ASYNC_NORMAL_ACTIVE;

		return 0;
	}


	if (tty->termios->c_cflag & CLOCAL)
		do_clocal = 1;
	
   /* Block waiting for the carrier detect and the line to become free */
	
	retval = 0;
	add_wait_queue(&ch->open_wait, &wait);
	save_flags(flags);
	cli();


	/* We dec count so that pc_close will know when to free things */
	if (!tty_hung_up_p(filp))
		ch->count--;

	restore_flags(flags);

	ch->blocked_open++;

	while(1) 
	{ /* Begin forever while  */

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

		if (signal_pending(current)) 
		{
			retval = -ERESTARTSYS;
			break;
		}

		/* ---------------------------------------------------------------
			Allow someone else to be scheduled.  We will occasionally go
			through this loop until one of the above conditions change.
			The below schedule call will allow other processes to enter and
			prevent this loop from hogging the cpu.
		------------------------------------------------------------------ */
		schedule();

	} /* End forever while  */

	current->state = TASK_RUNNING;
	remove_wait_queue(&ch->open_wait, &wait);
	cli();
	if (!tty_hung_up_p(filp))
		ch->count++;
	restore_flags(flags);

	ch->blocked_open--;

	if (retval)
		return retval;

	ch->asyncflags |= ASYNC_NORMAL_ACTIVE;

	return 0;

} /* End block_til_ready */	

/* ------------------ Begin pc_open  ---------------------- */

static int pc_open(struct tty_struct *tty, struct file * filp)
{ /* Begin pc_open */

	struct channel *ch;
	unsigned long flags;
	int line, retval, boardnum;
	volatile struct board_chan *bc;
	volatile unsigned int head;

	line = tty->index;
	if (line < 0 || line >= nbdevs) 
	{
		printk(KERN_ERR "<Error> - pc_open : line out of range in pc_open\n");
		tty->driver_data = NULL;
		return(-ENODEV);
	}


	ch = &digi_channels[line];
	boardnum = ch->boardnum;

	/* Check status of board configured in system.  */

	/* -----------------------------------------------------------------
		I check to see if the epca_setup routine detected an user error.  
		It might be better to put this in pc_init, but for the moment it
		goes here.
	---------------------------------------------------------------------- */

	if (invalid_lilo_config)
	{
		if (setup_error_code & INVALID_BOARD_TYPE)
			printk(KERN_ERR "<Error> - pc_open: Invalid board type specified in LILO command\n");

		if (setup_error_code & INVALID_NUM_PORTS)
			printk(KERN_ERR "<Error> - pc_open: Invalid number of ports specified in LILO command\n");

		if (setup_error_code & INVALID_MEM_BASE)
			printk(KERN_ERR "<Error> - pc_open: Invalid board memory address specified in LILO command\n");

		if (setup_error_code & INVALID_PORT_BASE)
			printk(KERN_ERR "<Error> - pc_open: Invalid board port address specified in LILO command\n");

		if (setup_error_code & INVALID_BOARD_STATUS)
			printk(KERN_ERR "<Error> - pc_open: Invalid board status specified in LILO command\n");

		if (setup_error_code & INVALID_ALTPIN)
			printk(KERN_ERR "<Error> - pc_open: Invalid board altpin specified in LILO command\n");

		tty->driver_data = NULL;   /* Mark this device as 'down' */
		return(-ENODEV);
	}

	if ((boardnum >= num_cards) || (boards[boardnum].status == DISABLED)) 
	{
		tty->driver_data = NULL;   /* Mark this device as 'down' */
		return(-ENODEV);
	}
	
	if (( bc = ch->brdchan) == 0) 
	{
		tty->driver_data = NULL;
		return(-ENODEV);
	}

	/* ------------------------------------------------------------------
		Every time a channel is opened, increment a counter.  This is 
		necessary because we do not wish to flush and shutdown the channel
		until the last app holding the channel open, closes it.	 	
	--------------------------------------------------------------------- */

	ch->count++;

	/* ----------------------------------------------------------------
		Set a kernel structures pointer to our local channel 
		structure.  This way we can get to it when passed only
		a tty struct.
	------------------------------------------------------------------ */

	tty->driver_data = ch;
	
	/* ----------------------------------------------------------------
		If this is the first time the channel has been opened, initialize
		the tty->termios struct otherwise let pc_close handle it.
	-------------------------------------------------------------------- */

	save_flags(flags);
	cli();

	globalwinon(ch);
	ch->statusflags = 0;

	/* Save boards current modem status */
	ch->imodem = bc->mstat;

	/* ----------------------------------------------------------------
	   Set receive head and tail ptrs to each other.  This indicates
	   no data available to read.
	----------------------------------------------------------------- */
	head = bc->rin;
	bc->rout = head;

	/* Set the channels associated tty structure */
	ch->tty = tty;

	/* -----------------------------------------------------------------
		The below routine generally sets up parity, baud, flow control 
		issues, etc.... It effect both control flags and input flags.
	-------------------------------------------------------------------- */
	epcaparam(tty,ch);

	ch->asyncflags |= ASYNC_INITIALIZED;
	memoff(ch);

	restore_flags(flags);

	retval = block_til_ready(tty, filp, ch);
	if (retval)
	{
		return retval;
	}

	/* -------------------------------------------------------------
		Set this again in case a hangup set it to zero while this 
		open() was waiting for the line...
	--------------------------------------------------------------- */
	ch->tty = tty;

	save_flags(flags);
	cli();
	globalwinon(ch);

	/* Enable Digi Data events */
	bc->idata = 1;

	memoff(ch);
	restore_flags(flags);

	return 0;

} /* End pc_open */

#ifdef MODULE
static int __init epca_module_init(void)
{ /* Begin init_module */

	unsigned long	flags;

	save_flags(flags);
	cli();

	pc_init();

	restore_flags(flags);

	return(0);
}

module_init(epca_module_init);
#endif

#ifdef ENABLE_PCI
static struct pci_driver epca_driver;
#endif

#ifdef MODULE
/* -------------------- Begin cleanup_module  ---------------------- */

static void __exit epca_module_exit(void)
{

	int               count, crd;
	struct board_info *bd;
	struct channel    *ch;
	unsigned long     flags;

	del_timer_sync(&epca_timer);

	save_flags(flags);
	cli();

	if ((tty_unregister_driver(pc_driver)) ||  
	    (tty_unregister_driver(pc_info)))
	{
		printk(KERN_WARNING "<Error> - DIGI : cleanup_module failed to un-register tty driver\n");
		restore_flags(flags);
		return;
	}
	put_tty_driver(pc_driver);
	put_tty_driver(pc_info);

	for (crd = 0; crd < num_cards; crd++) 
	{ /* Begin for each card */

		bd = &boards[crd];

		if (!bd)
		{ /* Begin sanity check */
			printk(KERN_ERR "<Error> - Digi : cleanup_module failed\n");
			return;
		} /* End sanity check */

		ch = card_ptr[crd]; 

		for (count = 0; count < bd->numports; count++, ch++) 
		{ /* Begin for each port */

			if (ch) 
			{
				if (ch->tty)
					tty_hangup(ch->tty);
				kfree(ch->tmp_buf);
			}

		} /* End for each port */
	} /* End for each card */

#ifdef ENABLE_PCI
	pci_unregister_driver (&epca_driver);
#endif

	restore_flags(flags);

}
module_exit(epca_module_exit);
#endif /* MODULE */

static struct tty_operations pc_ops = {
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

/* ------------------ Begin pc_init  ---------------------- */

int __init pc_init(void)
{ /* Begin pc_init */

	/* ----------------------------------------------------------------
		pc_init is called by the operating system during boot up prior to
		any open calls being made.  In the older versions of Linux (Prior
		to 2.0.0) an entry is made into tty_io.c.  A pointer to the last
		memory location (from kernel space) used (kmem_start) is passed
		to pc_init.  It is pc_inits responsibility to modify this value 
		for any memory that the Digi driver might need and then return
		this value to the operating system.  For example if the driver
		wishes to allocate 1K of kernel memory, pc_init would return 
		(kmem_start + 1024).  This memory (Between kmem_start and kmem_start
		+ 1024) would then be available for use exclusively by the driver.  
		In this case our driver does not allocate any of this kernel 
		memory.
	------------------------------------------------------------------*/

	ulong flags;
	int crd;
	struct board_info *bd;
	unsigned char board_id = 0;

#ifdef ENABLE_PCI
	int pci_boards_found, pci_count;

	pci_count = 0;
#endif /* ENABLE_PCI */

	pc_driver = alloc_tty_driver(MAX_ALLOC);
	if (!pc_driver)
		return -ENOMEM;

	pc_info = alloc_tty_driver(MAX_ALLOC);
	if (!pc_info) {
		put_tty_driver(pc_driver);
		return -ENOMEM;
	}

	/* -----------------------------------------------------------------------
		If epca_setup has not been ran by LILO set num_cards to defaults; copy
		board structure defined by digiConfig into drivers board structure.
		Note : If LILO has ran epca_setup then epca_setup will handle defining
		num_cards as well as copying the data into the board structure.
	-------------------------------------------------------------------------- */
	if (!liloconfig)
	{ /* Begin driver has been configured via. epcaconfig */

		nbdevs = NBDEVS;
		num_cards = NUMCARDS;
		memcpy((void *)&boards, (void *)&static_boards,
		       (sizeof(struct board_info) * NUMCARDS));
	} /* End driver has been configured via. epcaconfig */

	/* -----------------------------------------------------------------
		Note : If lilo was used to configure the driver and the 
		ignore epcaconfig option was choosen (digiepca=2) then 
		nbdevs and num_cards will equal 0 at this point.  This is
		okay; PCI cards will still be picked up if detected.
	--------------------------------------------------------------------- */

	/*  -----------------------------------------------------------
		Set up interrupt, we will worry about memory allocation in
		post_fep_init. 
	--------------------------------------------------------------- */


	printk(KERN_INFO "DIGI epca driver version %s loaded.\n",VERSION);

#ifdef ENABLE_PCI

	/* ------------------------------------------------------------------
		NOTE : This code assumes that the number of ports found in 
		       the boards array is correct.  This could be wrong if
		       the card in question is PCI (And therefore has no ports 
		       entry in the boards structure.)  The rest of the 
		       information will be valid for PCI because the beginning
		       of pc_init scans for PCI and determines i/o and base
		       memory addresses.  I am not sure if it is possible to 
		       read the number of ports supported by the card prior to
		       it being booted (Since that is the state it is in when 
		       pc_init is run).  Because it is not possible to query the
		       number of supported ports until after the card has booted;
		       we are required to calculate the card_ptrs as the card is	 
		       is initialized (Inside post_fep_init).  The negative thing
		       about this approach is that digiDload's call to GET_INFO
		       will have a bad port value.  (Since this is called prior
		       to post_fep_init.)

	--------------------------------------------------------------------- */
  
	pci_boards_found = 0;
	if(num_cards < MAXBOARDS)
		pci_boards_found += init_PCI();
	num_cards += pci_boards_found;

#endif /* ENABLE_PCI */

	pc_driver->owner = THIS_MODULE;
	pc_driver->name = "ttyD"; 
	pc_driver->devfs_name = "tts/D";
	pc_driver->major = DIGI_MAJOR; 
	pc_driver->minor_start = 0;
	pc_driver->type = TTY_DRIVER_TYPE_SERIAL;
	pc_driver->subtype = SERIAL_TYPE_NORMAL;
	pc_driver->init_termios = tty_std_termios;
	pc_driver->init_termios.c_iflag = 0;
	pc_driver->init_termios.c_oflag = 0;
	pc_driver->init_termios.c_cflag = B9600 | CS8 | CREAD | CLOCAL | HUPCL;
	pc_driver->init_termios.c_lflag = 0;
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
	pc_info->flags = TTY_DRIVER_REAL_RAW;
	tty_set_operations(pc_info, &info_ops);


	save_flags(flags);
	cli();

	for (crd = 0; crd < num_cards; crd++) 
	{ /* Begin for each card */

		/*  ------------------------------------------------------------------
			This is where the appropriate memory handlers for the hardware is
			set.  Everything at runtime blindly jumps through these vectors.
		---------------------------------------------------------------------- */

		/* defined in epcaconfig.h */
		bd = &boards[crd];

		switch (bd->type)
		{ /* Begin switch on bd->type {board type} */
			case PCXEM:
			case EISAXEM:
				bd->memwinon     = pcxem_memwinon ;
				bd->memwinoff    = pcxem_memwinoff ;
				bd->globalwinon  = pcxem_globalwinon ;
				bd->txwinon      = pcxem_txwinon ;
				bd->rxwinon      = pcxem_rxwinon ;
				bd->memoff       = pcxem_memoff ;
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

		} /* End switch on bd->type */

		/* ---------------------------------------------------------------
			Some cards need a memory segment to be defined for use in 
			transmit and receive windowing operations.  These boards
			are listed in the below switch.  In the case of the XI the
			amount of memory on the board is variable so the memory_seg
			is also variable.  This code determines what they segment 
			should be.
		----------------------------------------------------------------- */

		switch (bd->type)
		{ /* Begin switch on bd->type {board type} */

			case PCXE:
			case PCXEVE:
			case PC64XE:
				bd->memory_seg = 0xf000;
			break;

			case PCXI:
				board_id = inb((int)bd->port);
				if ((board_id & 0x1) == 0x1) 
				{ /* Begin it's an XI card */ 

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

				} /* End it is an XI card */
				else
				{
					printk(KERN_ERR "<Error> - Board at 0x%x doesn't appear to be an XI\n",(int)bd->port);
				}
			break;

		} /* End switch on bd->type */

	} /* End for each card */

	if (tty_register_driver(pc_driver))
		panic("Couldn't register Digi PC/ driver");

	if (tty_register_driver(pc_info))
		panic("Couldn't register Digi PC/ info ");

	/* -------------------------------------------------------------------
	   Start up the poller to check for events on all enabled boards
	---------------------------------------------------------------------- */

	init_timer(&epca_timer);
	epca_timer.function = epcapoll;
	mod_timer(&epca_timer, jiffies + HZ/25);

	restore_flags(flags);

	return 0;

} /* End pc_init */

/* ------------------ Begin post_fep_init  ---------------------- */

static void post_fep_init(unsigned int crd)
{ /* Begin post_fep_init */

	int i;
	unchar *memaddr;
	volatile struct global_data *gd;
	struct board_info *bd;
	volatile struct board_chan *bc;
	struct channel *ch; 
	int shrinkmem = 0, lowwater ; 
 
	/*  -------------------------------------------------------------
		This call is made by the user via. the ioctl call DIGI_INIT.
		It is responsible for setting up all the card specific stuff.
	---------------------------------------------------------------- */
	bd = &boards[crd];

	/* -----------------------------------------------------------------
		If this is a PCI board, get the port info.  Remember PCI cards
		do not have entries into the epcaconfig.h file, so we can't get 
		the number of ports from it.  Unfortunetly, this means that anyone
		doing a DIGI_GETINFO before the board has booted will get an invalid
		number of ports returned (It should return 0).  Calls to DIGI_GETINFO
		after DIGI_INIT has been called will return the proper values. 
	------------------------------------------------------------------- */

	if (bd->type >= PCIXEM) /* If the board in question is PCI */
	{ /* Begin get PCI number of ports */

		/* --------------------------------------------------------------------
			Below we use XEMPORTS as a memory offset regardless of which PCI
			card it is.  This is because all of the supported PCI cards have
			the same memory offset for the channel data.  This will have to be
			changed if we ever develop a PCI/XE card.  NOTE : The FEP manual
			states that the port offset is 0xC22 as opposed to 0xC02.  This is
			only true for PC/XE, and PC/XI cards; not for the XEM, or CX series.
			On the PCI cards the number of ports is determined by reading a 
			ID PROM located in the box attached to the card.  The card can then
			determine the index the id to determine the number of ports available.
			(FYI - The id should be located at 0x1ac (And may use up to 4 bytes
			if the box in question is a XEM or CX)).  
		------------------------------------------------------------------------ */ 

		bd->numports = (unsigned short)*(unsigned char *)bus_to_virt((unsigned long)
                                                       (bd->re_map_membase + XEMPORTS));

		
		epcaassert(bd->numports <= 64,"PCI returned a invalid number of ports");
		nbdevs += (bd->numports);

	} /* End get PCI number of ports */

	if (crd != 0)
		card_ptr[crd] = card_ptr[crd-1] + boards[crd-1].numports;
	else
		card_ptr[crd] = &digi_channels[crd]; /* <- For card 0 only */

	ch = card_ptr[crd];


	epcaassert(ch <= &digi_channels[nbdevs - 1], "ch out of range");

	memaddr = (unchar *)bd->re_map_membase;

	/* 
	   The below command is necessary because newer kernels (2.1.x and
	   up) do not have a 1:1 virtual to physical mapping.  The below
	   call adjust for that.
	*/

	memaddr = (unsigned char *)bus_to_virt((unsigned long)memaddr);

	/* -----------------------------------------------------------------
		The below assignment will set bc to point at the BEGINING of
		the cards channel structures.  For 1 card there will be between
		8 and 64 of these structures.
	-------------------------------------------------------------------- */

	bc = (volatile struct board_chan *)((ulong)memaddr + CHANSTRUCT);

	/* -------------------------------------------------------------------
		The below assignment will set gd to point at the BEGINING of
		global memory address 0xc00.  The first data in that global
		memory actually starts at address 0xc1a.  The command in 
		pointer begins at 0xd10.
	---------------------------------------------------------------------- */

	gd = (volatile struct global_data *)((ulong)memaddr + GLOBAL);

	/* --------------------------------------------------------------------
		XEPORTS (address 0xc22) points at the number of channels the
		card supports. (For 64XE, XI, XEM, and XR use 0xc02)
	----------------------------------------------------------------------- */

	if (((bd->type == PCXEVE) | (bd->type == PCXE)) &&
	    (*(ushort *)((ulong)memaddr + XEPORTS) < 3))
		shrinkmem = 1;
	if (bd->type < PCIXEM)
		if (!request_region((int)bd->port, 4, board_desc[bd->type]))
			return;		

	memwinon(bd, 0);

	/*  --------------------------------------------------------------------
		Remember ch is the main drivers channels structure, while bc is 
	   the cards channel structure.
	------------------------------------------------------------------------ */

	/* For every port on the card do ..... */

	for (i = 0; i < bd->numports; i++, ch++, bc++) 
	{ /* Begin for each port */

		ch->brdchan        = bc;
		ch->mailbox        = gd; 
		INIT_WORK(&ch->tqueue, do_softint, ch);
		ch->board          = &boards[crd];

		switch (bd->type)
		{ /* Begin switch bd->type */

			/* ----------------------------------------------------------------
				Since some of the boards use different bitmaps for their
				control signals we cannot hard code these values and retain
				portability.  We virtualize this data here.
			------------------------------------------------------------------- */
			case EISAXEM:
			case PCXEM:
			case PCIXEM:
			case PCIXRJ:
			case PCIXR:
				ch->m_rts = 0x02 ;
				ch->m_dcd = 0x80 ; 
				ch->m_dsr = 0x20 ;
				ch->m_cts = 0x10 ;
				ch->m_ri  = 0x40 ;
				ch->m_dtr = 0x01 ;
				break;

			case PCXE:
			case PCXEVE:
			case PCXI:
			case PC64XE:
				ch->m_rts = 0x02 ;
				ch->m_dcd = 0x08 ; 
				ch->m_dsr = 0x10 ;
				ch->m_cts = 0x20 ;
				ch->m_ri  = 0x40 ;
				ch->m_dtr = 0x80 ;
				break;
	
		} /* End switch bd->type */

		if (boards[crd].altpin) 
		{
			ch->dsr = ch->m_dcd;
			ch->dcd = ch->m_dsr;
			ch->digiext.digi_flags |= DIGI_ALTPIN;
		}
		else 
		{ 
			ch->dcd = ch->m_dcd;
			ch->dsr = ch->m_dsr;
		}
	
		ch->boardnum   = crd;
		ch->channelnum = i;
		ch->magic      = EPCA_MAGIC;
		ch->tty        = NULL;

		if (shrinkmem) 
		{
			fepcmd(ch, SETBUFFER, 32, 0, 0, 0);
			shrinkmem = 0;
		}

		switch (bd->type)
		{ /* Begin switch bd->type */

			case PCIXEM:
			case PCIXRJ:
			case PCIXR:
				/* Cover all the 2MEG cards */
				ch->txptr = memaddr + (((bc->tseg) << 4) & 0x1fffff);
				ch->rxptr = memaddr + (((bc->rseg) << 4) & 0x1fffff);
				ch->txwin = FEPWIN | ((bc->tseg) >> 11);
				ch->rxwin = FEPWIN | ((bc->rseg) >> 11);
				break;

			case PCXEM:
			case EISAXEM:
				/* Cover all the 32K windowed cards */
				/* Mask equal to window size - 1 */
				ch->txptr = memaddr + (((bc->tseg) << 4) & 0x7fff);
				ch->rxptr = memaddr + (((bc->rseg) << 4) & 0x7fff);
				ch->txwin = FEPWIN | ((bc->tseg) >> 11);
				ch->rxwin = FEPWIN | ((bc->rseg) >> 11);
				break;

			case PCXEVE:
			case PCXE:
				ch->txptr = memaddr + (((bc->tseg - bd->memory_seg) << 4) & 0x1fff);
				ch->txwin = FEPWIN | ((bc->tseg - bd->memory_seg) >> 9);
				ch->rxptr = memaddr + (((bc->rseg - bd->memory_seg) << 4) & 0x1fff);
				ch->rxwin = FEPWIN | ((bc->rseg - bd->memory_seg) >>9 );
				break;

			case PCXI:
			case PC64XE:
				ch->txptr = memaddr + ((bc->tseg - bd->memory_seg) << 4);
				ch->rxptr = memaddr + ((bc->rseg - bd->memory_seg) << 4);
				ch->txwin = ch->rxwin = 0;
				break;

		} /* End switch bd->type */

		ch->txbufhead = 0;
		ch->txbufsize = bc->tmax + 1;
	
		ch->rxbufhead = 0;
		ch->rxbufsize = bc->rmax + 1;
	
		lowwater = ch->txbufsize >= 2000 ? 1024 : (ch->txbufsize / 2);

		/* Set transmitter low water mark */
		fepcmd(ch, STXLWATER, lowwater, 0, 10, 0);

		/* Set receiver low water mark */

		fepcmd(ch, SRXLWATER, (ch->rxbufsize / 4), 0, 10, 0);

		/* Set receiver high water mark */

		fepcmd(ch, SRXHWATER, (3 * ch->rxbufsize / 4), 0, 10, 0);

		bc->edelay = 100;
		bc->idata = 1;
	
		ch->startc  = bc->startc;
		ch->stopc   = bc->stopc;
		ch->startca = bc->startca;
		ch->stopca  = bc->stopca;
	
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
		ch->tmp_buf = kmalloc(ch->txbufsize,GFP_KERNEL);
		if (!(ch->tmp_buf))
		{
			printk(KERN_ERR "POST FEP INIT : kmalloc failed for port 0x%x\n",i);
			release_region((int)bd->port, 4);
			while(i-- > 0)
				kfree((ch--)->tmp_buf);
			return;
		}
		else 
			memset((void *)ch->tmp_buf,0,ch->txbufsize);
	} /* End for each port */

	printk(KERN_INFO 
	        "Digi PC/Xx Driver V%s:  %s I/O = 0x%lx Mem = 0x%lx Ports = %d\n", 
	        VERSION, board_desc[bd->type], (long)bd->port, (long)bd->membase, bd->numports);
	sprintf(mesg, 
	        "Digi PC/Xx Driver V%s:  %s I/O = 0x%lx Mem = 0x%lx Ports = %d\n", 
	        VERSION, board_desc[bd->type], (long)bd->port, (long)bd->membase, bd->numports);
	console_print(mesg);

	memwinoff(bd, 0);

} /* End post_fep_init */

/* --------------------- Begin epcapoll  ------------------------ */

static void epcapoll(unsigned long ignored)
{ /* Begin epcapoll */

	unsigned long flags;
	int crd;
	volatile unsigned int head, tail;
	struct channel *ch;
	struct board_info *bd;

	/* -------------------------------------------------------------------
		This routine is called upon every timer interrupt.  Even though
		the Digi series cards are capable of generating interrupts this 
		method of non-looping polling is more efficient.  This routine
		checks for card generated events (Such as receive data, are transmit
		buffer empty) and acts on those events.
	----------------------------------------------------------------------- */
	
	save_flags(flags);
	cli();

	for (crd = 0; crd < num_cards; crd++) 
	{ /* Begin for each card */

		bd = &boards[crd];
		ch = card_ptr[crd];

		if ((bd->status == DISABLED) || digi_poller_inhibited)
			continue; /* Begin loop next interation */

		/* -----------------------------------------------------------
			assertmemoff is not needed here; indeed it is an empty subroutine.
			It is being kept because future boards may need this as well as
			some legacy boards.
		---------------------------------------------------------------- */

		assertmemoff(ch);

		globalwinon(ch);

		/* ---------------------------------------------------------------
			In this case head and tail actually refer to the event queue not
			the transmit or receive queue.
		------------------------------------------------------------------- */

		head = ch->mailbox->ein;
		tail = ch->mailbox->eout;
		
		/* If head isn't equal to tail we have an event */

		if (head != tail)
			doevent(crd);

		memoff(ch);

	} /* End for each card */

	mod_timer(&epca_timer, jiffies + (HZ / 25));

	restore_flags(flags);
} /* End epcapoll */

/* --------------------- Begin doevent  ------------------------ */

static void doevent(int crd)
{ /* Begin doevent */

	volatile unchar *eventbuf;
	struct channel *ch, *chan0;
	static struct tty_struct *tty;
	volatile struct board_info *bd;
	volatile struct board_chan *bc;
	register volatile unsigned int tail, head;
	register int event, channel;
	register int mstat, lstat;

	/* -------------------------------------------------------------------
		This subroutine is called by epcapoll when an event is detected 
		in the event queue.  This routine responds to those events.
	--------------------------------------------------------------------- */

	bd = &boards[crd];

	chan0 = card_ptr[crd];
	epcaassert(chan0 <= &digi_channels[nbdevs - 1], "ch out of range");

	assertgwinon(chan0);

	while ((tail = chan0->mailbox->eout) != (head = chan0->mailbox->ein)) 
	{ /* Begin while something in event queue */

		assertgwinon(chan0);

		eventbuf = (volatile unchar *)bus_to_virt((ulong)(bd->re_map_membase + tail + ISTART));

		/* Get the channel the event occurred on */
		channel = eventbuf[0];

		/* Get the actual event code that occurred */
		event = eventbuf[1];

		/*  ----------------------------------------------------------------
			The two assignments below get the current modem status (mstat)
			and the previous modem status (lstat).  These are useful becuase
			an event could signal a change in modem signals itself.
		------------------------------------------------------------------- */

		mstat = eventbuf[2];
		lstat = eventbuf[3];

		ch = chan0 + channel;

		if ((unsigned)channel >= bd->numports || !ch) 
		{ 
			if (channel >= bd->numports)
				ch = chan0;
			bc = ch->brdchan;
			goto next;
		}

		if ((bc = ch->brdchan) == NULL)
			goto next;

		if (event & DATA_IND) 
		{ /* Begin DATA_IND */

			receive_data(ch);
			assertgwinon(ch);

		} /* End DATA_IND */
		/* else *//* Fix for DCD transition missed bug */
		if (event & MODEMCHG_IND) 
		{ /* Begin MODEMCHG_IND */

			/* A modem signal change has been indicated */

			ch->imodem = mstat;

			if (ch->asyncflags & ASYNC_CHECK_CD) 
			{
				if (mstat & ch->dcd)  /* We are now receiving dcd */
					wake_up_interruptible(&ch->open_wait);
				else
					pc_sched_event(ch, EPCA_EVENT_HANGUP); /* No dcd; hangup */
			}

		} /* End MODEMCHG_IND */

		tty = ch->tty;
		if (tty) 
		{ /* Begin if valid tty */

			if (event & BREAK_IND) 
			{ /* Begin if BREAK_IND */

				/* A break has been indicated */

				tty->flip.count++;
				*tty->flip.flag_buf_ptr++ = TTY_BREAK;

				*tty->flip.char_buf_ptr++ = 0;

				tty_schedule_flip(tty); 

			} /* End if BREAK_IND */
			else
			if (event & LOWTX_IND) 
			{ /* Begin LOWTX_IND */

				if (ch->statusflags & LOWWAIT) 
				{ /* Begin if LOWWAIT */

					ch->statusflags &= ~LOWWAIT;
					tty_wakeup(tty);
					wake_up_interruptible(&tty->write_wait);

				} /* End if LOWWAIT */

			} /* End LOWTX_IND */
			else
			if (event & EMPTYTX_IND) 
			{ /* Begin EMPTYTX_IND */

				/* This event is generated by setup_empty_event */

				ch->statusflags &= ~TXBUSY;
				if (ch->statusflags & EMPTYWAIT) 
				{ /* Begin if EMPTYWAIT */

					ch->statusflags &= ~EMPTYWAIT;
					tty_wakeup(tty);

					wake_up_interruptible(&tty->write_wait);

				} /* End if EMPTYWAIT */

			} /* End EMPTYTX_IND */

		} /* End if valid tty */


	next:
		globalwinon(ch);

		if (!bc)
			printk(KERN_ERR "<Error> - bc == NULL in doevent!\n");
		else 
			bc->idata = 1;

		chan0->mailbox->eout = (tail + 4) & (IMAX - ISTART - 4);
		globalwinon(chan0);

	} /* End while something in event queue */

} /* End doevent */

/* --------------------- Begin fepcmd  ------------------------ */

static void fepcmd(struct channel *ch, int cmd, int word_or_byte,
                   int byte2, int ncmds, int bytecmd)
{ /* Begin fepcmd */

	unchar *memaddr;
	unsigned int head, cmdTail, cmdStart, cmdMax;
	long count;
	int n;

	/* This is the routine in which commands may be passed to the card. */

	if (ch->board->status == DISABLED)
	{
		return;
	}

	assertgwinon(ch);

	/* Remember head (As well as max) is just an offset not a base addr */
	head = ch->mailbox->cin;

	/* cmdStart is a base address */
	cmdStart = ch->mailbox->cstart;

	/* ------------------------------------------------------------------
		We do the addition below because we do not want a max pointer 
		relative to cmdStart.  We want a max pointer that points at the 
		physical end of the command queue.
	-------------------------------------------------------------------- */

	cmdMax = (cmdStart + 4 + (ch->mailbox->cmax));

	memaddr = ch->board->re_map_membase;

	/* 
	   The below command is necessary because newer kernels (2.1.x and
	   up) do not have a 1:1 virtual to physical mapping.  The below
	   call adjust for that.
	*/

	memaddr = (unsigned char *)bus_to_virt((unsigned long)memaddr);

	if (head >= (cmdMax - cmdStart) || (head & 03)) 
	{
		printk(KERN_ERR "line %d: Out of range, cmd = %x, head = %x\n", __LINE__, 
              cmd, head);
		printk(KERN_ERR "line %d: Out of range, cmdMax = %x, cmdStart = %x\n", __LINE__, 
              cmdMax, cmdStart);
		return;
	}

	if (bytecmd) 
	{
		*(volatile unchar *)(memaddr + head + cmdStart + 0) = (unchar)cmd;

		*(volatile unchar *)(memaddr + head + cmdStart + 1) = (unchar)ch->channelnum;
		/* Below word_or_byte is bits to set */
		*(volatile unchar *)(memaddr + head + cmdStart + 2) = (unchar)word_or_byte;
		/* Below byte2 is bits to reset */
		*(volatile unchar *)(memaddr + head + cmdStart + 3) = (unchar)byte2;

	} 
	else 
	{
		*(volatile unchar *)(memaddr + head + cmdStart + 0) = (unchar)cmd;
		*(volatile unchar *)(memaddr + head + cmdStart + 1) = (unchar)ch->channelnum;
		*(volatile ushort*)(memaddr + head + cmdStart + 2) = (ushort)word_or_byte;
	}

	head = (head + 4) & (cmdMax - cmdStart - 4);
	ch->mailbox->cin = head;

	count = FEPTIMEOUT;

	for (;;) 
	{ /* Begin forever loop */

		count--;
		if (count == 0) 
		{
			printk(KERN_ERR "<Error> - Fep not responding in fepcmd()\n");
			return;
		}

		head = ch->mailbox->cin;
		cmdTail = ch->mailbox->cout;

		n = (head - cmdTail) & (cmdMax - cmdStart - 4);

		/* ----------------------------------------------------------
			Basically this will break when the FEP acknowledges the 
			command by incrementing cmdTail (Making it equal to head).
		------------------------------------------------------------- */

		if (n <= ncmds * (sizeof(short) * 4))
			break; /* Well nearly forever :-) */

	} /* End forever loop */

} /* End fepcmd */

/* ---------------------------------------------------------------------
	Digi products use fields in their channels structures that are very
	similar to the c_cflag and c_iflag fields typically found in UNIX
	termios structures.  The below three routines allow mappings 
	between these hardware "flags" and their respective Linux flags.
------------------------------------------------------------------------- */
 
/* --------------------- Begin termios2digi_h -------------------- */

static unsigned termios2digi_h(struct channel *ch, unsigned cflag)
{ /* Begin termios2digi_h */

	unsigned res = 0;

	if (cflag & CRTSCTS) 
	{
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

} /* End termios2digi_h */

/* --------------------- Begin termios2digi_i -------------------- */
static unsigned termios2digi_i(struct channel *ch, unsigned iflag)
{ /* Begin termios2digi_i */

	unsigned res = iflag & (IGNBRK | BRKINT | IGNPAR | PARMRK | 
	                        INPCK | ISTRIP|IXON|IXANY|IXOFF);
	
	if (ch->digiext.digi_flags & DIGI_AIXON)
		res |= IAIXON;
	return res;

} /* End termios2digi_i */

/* --------------------- Begin termios2digi_c -------------------- */

static unsigned termios2digi_c(struct channel *ch, unsigned cflag)
{ /* Begin termios2digi_c */

	unsigned res = 0;

#ifdef SPEED_HACK
	/* CL: HACK to force 115200 at 38400 and 57600 at 19200 Baud */
	if ((cflag & CBAUD)== B38400) cflag=cflag - B38400 + B115200;
	if ((cflag & CBAUD)== B19200) cflag=cflag - B19200 + B57600;
#endif /* SPEED_HACK */

	if (cflag & CBAUDEX)
	{ /* Begin detected CBAUDEX */

		ch->digiext.digi_flags |= DIGI_FAST;

		/* -------------------------------------------------------------
		   HUPCL bit is used by FEP to indicate fast baud
		   table is to be used.
		----------------------------------------------------------------- */

		res |= FEP_HUPCL;

	} /* End detected CBAUDEX */
	else ch->digiext.digi_flags &= ~DIGI_FAST; 

	/* -------------------------------------------------------------------
		CBAUD has bit position 0x1000 set these days to indicate Linux
		baud rate remap.  Digi hardware can't handle the bit assignment.
		(We use a different bit assignment for high speed.).  Clear this
		bit out.
	---------------------------------------------------------------------- */
	res |= cflag & ((CBAUD ^ CBAUDEX) | PARODD | PARENB | CSTOPB | CSIZE);

	/* -------------------------------------------------------------
		This gets a little confusing.  The Digi cards have their own
		representation of c_cflags controling baud rate.  For the most
		part this is identical to the Linux implementation.  However;
		Digi supports one rate (76800) that Linux doesn't.  This means 
		that the c_cflag entry that would normally mean 76800 for Digi
		actually means 115200 under Linux.  Without the below mapping,
		a stty 115200 would only drive the board at 76800.  Since 
		the rate 230400 is also found after 76800, the same problem afflicts	
		us when we choose a rate of 230400.  Without the below modificiation
		stty 230400 would actually give us 115200.

		There are two additional differences.  The Linux value for CLOCAL
		(0x800; 0004000) has no meaning to the Digi hardware.  Also in 
		later releases of Linux; the CBAUD define has CBAUDEX (0x1000;
		0010000) ored into it (CBAUD = 0x100f as opposed to 0xf). CBAUDEX
		should be checked for a screened out prior to termios2digi_c 
		returning.  Since CLOCAL isn't used by the board this can be
		ignored as long as the returned value is used only by Digi hardware. 
	----------------------------------------------------------------- */

	if (cflag & CBAUDEX)
	{
		/* -------------------------------------------------------------
			The below code is trying to guarantee that only baud rates
			115200 and 230400 are remapped.  We use exclusive or because
			the various baud rates share common bit positions and therefore
			can't be tested for easily.
		----------------------------------------------------------------- */

				
		if ((!((cflag & 0x7) ^ (B115200 & ~CBAUDEX))) || 
		    (!((cflag & 0x7) ^ (B230400 & ~CBAUDEX))))
		{
			res += 1;
		}
	}

	return res;

} /* End termios2digi_c */

/* --------------------- Begin epcaparam  ----------------------- */

static void epcaparam(struct tty_struct *tty, struct channel *ch)
{ /* Begin epcaparam */

	unsigned int cmdHead;
	struct termios *ts;
	volatile struct board_chan *bc;
	unsigned mval, hflow, cflag, iflag;

	bc = ch->brdchan;
	epcaassert(bc !=0, "bc out of range");

	assertgwinon(ch);

	ts = tty->termios;

	if ((ts->c_cflag & CBAUD) == 0) 
	{ /* Begin CBAUD detected */

		cmdHead = bc->rin;
		bc->rout = cmdHead;
		cmdHead = bc->tin;

		/* Changing baud in mid-stream transmission can be wonderful */
		/* ---------------------------------------------------------------
			Flush current transmit buffer by setting cmdTail pointer (tout)
			to cmdHead pointer (tin).  Hopefully the transmit buffer is empty.
		----------------------------------------------------------------- */

		fepcmd(ch, STOUT, (unsigned) cmdHead, 0, 0, 0);
		mval = 0;

	} /* End CBAUD detected */
	else 
	{ /* Begin CBAUD not detected */

		/* -------------------------------------------------------------------
			c_cflags have changed but that change had nothing to do with BAUD.
			Propagate the change to the card.
		---------------------------------------------------------------------- */ 

		cflag = termios2digi_c(ch, ts->c_cflag);

		if (cflag != ch->fepcflag) 
		{
			ch->fepcflag = cflag;
			/* Set baud rate, char size, stop bits, parity */
			fepcmd(ch, SETCTRLFLAGS, (unsigned) cflag, 0, 0, 0);
		}


		/* ----------------------------------------------------------------
			If the user has not forced CLOCAL and if the device is not a 
			CALLOUT device (Which is always CLOCAL) we set flags such that
			the driver will wait on carrier detect.
		------------------------------------------------------------------- */

		if (ts->c_cflag & CLOCAL)
		{ /* Begin it is a cud device or a ttyD device with CLOCAL on */
			ch->asyncflags &= ~ASYNC_CHECK_CD;
		} /* End it is a cud device or a ttyD device with CLOCAL on */
		else
		{ /* Begin it is a ttyD device */
			ch->asyncflags |= ASYNC_CHECK_CD;
		} /* End it is a ttyD device */

		mval = ch->m_dtr | ch->m_rts;

	} /* End CBAUD not detected */

	iflag = termios2digi_i(ch, ts->c_iflag);

	/* Check input mode flags */

	if (iflag != ch->fepiflag) 
	{
		ch->fepiflag = iflag;

		/* ---------------------------------------------------------------
			Command sets channels iflag structure on the board. Such things 
			as input soft flow control, handling of parity errors, and
			break handling are all set here.
		------------------------------------------------------------------- */

		/* break handling, parity handling, input stripping, flow control chars */
		fepcmd(ch, SETIFLAGS, (unsigned int) ch->fepiflag, 0, 0, 0);
	}

	/* ---------------------------------------------------------------
		Set the board mint value for this channel.  This will cause hardware
		events to be generated each time the DCD signal (Described in mint) 
		changes.	
	------------------------------------------------------------------- */
	bc->mint = ch->dcd;

	if ((ts->c_cflag & CLOCAL) || (ch->digiext.digi_flags & DIGI_FORCEDCD))
		if (ch->digiext.digi_flags & DIGI_FORCEDCD)
			bc->mint = 0;

	ch->imodem = bc->mstat;

	hflow = termios2digi_h(ch, ts->c_cflag);

	if (hflow != ch->hflow) 
	{
		ch->hflow = hflow;

		/* --------------------------------------------------------------
			Hard flow control has been selected but the board is not
			using it.  Activate hard flow control now.
		----------------------------------------------------------------- */

		fepcmd(ch, SETHFLOW, hflow, 0xff, 0, 1);
	}
	

	mval ^= ch->modemfake & (mval ^ ch->modem);

	if (ch->omodem ^ mval) 
	{
		ch->omodem = mval;

		/* --------------------------------------------------------------
			The below command sets the DTR and RTS mstat structure.  If
			hard flow control is NOT active these changes will drive the
			output of the actual DTR and RTS lines.  If hard flow control 
			is active, the changes will be saved in the mstat structure and
			only asserted when hard flow control is turned off. 
		----------------------------------------------------------------- */

		/* First reset DTR & RTS; then set them */
		fepcmd(ch, SETMODEM, 0, ((ch->m_dtr)|(ch->m_rts)), 0, 1);
		fepcmd(ch, SETMODEM, mval, 0, 0, 1);

	}

	if (ch->startc != ch->fepstartc || ch->stopc != ch->fepstopc) 
	{
		ch->fepstartc = ch->startc;
		ch->fepstopc = ch->stopc;

		/* ------------------------------------------------------------
			The XON / XOFF characters have changed; propagate these
			changes to the card.	
		--------------------------------------------------------------- */

		fepcmd(ch, SONOFFC, ch->fepstartc, ch->fepstopc, 0, 1);
	}

	if (ch->startca != ch->fepstartca || ch->stopca != ch->fepstopca) 
	{
		ch->fepstartca = ch->startca;
		ch->fepstopca = ch->stopca;

		/* ---------------------------------------------------------------
			Similar to the above, this time the auxilarly XON / XOFF 
			characters have changed; propagate these changes to the card.
		------------------------------------------------------------------ */

		fepcmd(ch, SAUXONOFFC, ch->fepstartca, ch->fepstopca, 0, 1);
	}

} /* End epcaparam */

/* --------------------- Begin receive_data  ----------------------- */

static void receive_data(struct channel *ch)
{ /* Begin receive_data */

	unchar *rptr;
	struct termios *ts = NULL;
	struct tty_struct *tty;
	volatile struct board_chan *bc;
	register int dataToRead, wrapgap, bytesAvailable;
	register unsigned int tail, head;
	unsigned int wrapmask;
	int rc;


	/* ---------------------------------------------------------------
		This routine is called by doint when a receive data event 
		has taken place.
	------------------------------------------------------------------- */

	globalwinon(ch);

	if (ch->statusflags & RXSTOPPED)
		return;

	tty = ch->tty;
	if (tty)
		ts = tty->termios;

	bc = ch->brdchan;

	if (!bc) 
	{
		printk(KERN_ERR "<Error> - bc is NULL in receive_data!\n");
		return;
	}

	wrapmask = ch->rxbufsize - 1;

	/* --------------------------------------------------------------------- 
		Get the head and tail pointers to the receiver queue.  Wrap the 
		head pointer if it has reached the end of the buffer.
	------------------------------------------------------------------------ */

	head = bc->rin;
	head &= wrapmask;
	tail = bc->rout & wrapmask;

	bytesAvailable = (head - tail) & wrapmask;

	if (bytesAvailable == 0)
		return;

	/* ------------------------------------------------------------------
	   If CREAD bit is off or device not open, set TX tail to head
	--------------------------------------------------------------------- */

	if (!tty || !ts || !(ts->c_cflag & CREAD)) 
	{
		bc->rout = head;
		return;
	}

	if (tty->flip.count == TTY_FLIPBUF_SIZE) 
		return;

	if (bc->orun) 
	{
		bc->orun = 0;
		printk(KERN_WARNING "overrun! DigiBoard device %s\n",tty->name);
	}

	rxwinon(ch);
	rptr = tty->flip.char_buf_ptr;
	rc = tty->flip.count;

	while (bytesAvailable > 0) 
	{ /* Begin while there is data on the card */

		wrapgap = (head >= tail) ? head - tail : ch->rxbufsize - tail;

		/* ---------------------------------------------------------------
			Even if head has wrapped around only report the amount of
			data to be equal to the size - tail.  Remember memcpy can't
			automaticly wrap around the receive buffer.
		----------------------------------------------------------------- */

		dataToRead = (wrapgap < bytesAvailable) ? wrapgap : bytesAvailable;

		/* --------------------------------------------------------------
		   Make sure we don't overflow the buffer
		----------------------------------------------------------------- */

		if ((rc + dataToRead) > TTY_FLIPBUF_SIZE)
			dataToRead = TTY_FLIPBUF_SIZE - rc;

		if (dataToRead == 0)
			break;

		/* ---------------------------------------------------------------
			Move data read from our card into the line disciplines buffer
			for translation if necessary.
		------------------------------------------------------------------ */

		if ((memcpy(rptr, ch->rxptr + tail, dataToRead)) != rptr)
			printk(KERN_ERR "<Error> - receive_data : memcpy failed\n");
			
		rc   += dataToRead;
		rptr += dataToRead;
		tail = (tail + dataToRead) & wrapmask;
		bytesAvailable -= dataToRead;

	} /* End while there is data on the card */


	tty->flip.count = rc;
	tty->flip.char_buf_ptr = rptr;
	globalwinon(ch);
	bc->rout = tail;

	/* Must be called with global data */
	tty_schedule_flip(ch->tty); 
	return;

} /* End receive_data */

static int info_ioctl(struct tty_struct *tty, struct file * file,
		    unsigned int cmd, unsigned long arg)
{
	switch (cmd) 
	{ /* Begin switch cmd */

		case DIGI_GETINFO:
		{ /* Begin case DIGI_GETINFO */

			struct digi_info di ;
			int brd;

			getUser(brd, (unsigned int __user *)arg);

			if ((brd < 0) || (brd >= num_cards) || (num_cards == 0))
				return (-ENODEV);

			memset(&di, 0, sizeof(di));

			di.board = brd ; 
			di.status = boards[brd].status;
			di.type = boards[brd].type ;
			di.numports = boards[brd].numports ;
			di.port = boards[brd].port ;
			di.membase = boards[brd].membase ;

			if (copy_to_user((void __user *)arg, &di, sizeof (di)))
				return -EFAULT;
			break;

		} /* End case DIGI_GETINFO */

		case DIGI_POLLER:
		{ /* Begin case DIGI_POLLER */

			int brd = arg & 0xff000000 >> 16 ; 
			unsigned char state = arg & 0xff ; 

			if ((brd < 0) || (brd >= num_cards))
			{
				printk(KERN_ERR "<Error> - DIGI POLLER : brd not valid!\n");
				return (-ENODEV);
			}

			digi_poller_inhibited = state ;
			break ; 

		} /* End case DIGI_POLLER */

		case DIGI_INIT:
		{ /* Begin case DIGI_INIT */

			/* ------------------------------------------------------------
				This call is made by the apps to complete the initilization
				of the board(s).  This routine is responsible for setting
				the card to its initial state and setting the drivers control
				fields to the sutianle settings for the card in question.
			---------------------------------------------------------------- */
		
			int crd ; 
			for (crd = 0; crd < num_cards; crd++) 
				post_fep_init (crd);

			break ; 

		} /* End case DIGI_INIT */


		default:
			return -ENOIOCTLCMD;

	} /* End switch cmd */
	return (0) ;
}
/* --------------------- Begin pc_ioctl  ----------------------- */

static int pc_tiocmget(struct tty_struct *tty, struct file *file)
{
	struct channel *ch = (struct channel *) tty->driver_data;
	volatile struct board_chan *bc;
	unsigned int mstat, mflag = 0;
	unsigned long flags;

	if (ch)
		bc = ch->brdchan;
	else
	{
		printk(KERN_ERR "<Error> - ch is NULL in pc_tiocmget!\n");
		return(-EINVAL);
	}

	save_flags(flags);
	cli();
	globalwinon(ch);
	mstat = bc->mstat;
	memoff(ch);
	restore_flags(flags);

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

	if (!ch) {
		printk(KERN_ERR "<Error> - ch is NULL in pc_tiocmset!\n");
		return(-EINVAL);
	}

	save_flags(flags);
	cli();
	/*
	 * I think this modemfake stuff is broken.  It doesn't
	 * correctly reflect the behaviour desired by the TIOCM*
	 * ioctls.  Therefore this is probably broken.
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

	/*  --------------------------------------------------------------
		The below routine generally sets up parity, baud, flow control
		issues, etc.... It effect both control flags and input flags.
	------------------------------------------------------------------ */

	epcaparam(tty,ch);
	memoff(ch);
	restore_flags(flags);
	return 0;
}

static int pc_ioctl(struct tty_struct *tty, struct file * file,
		    unsigned int cmd, unsigned long arg)
{ /* Begin pc_ioctl */

	digiflow_t dflow;
	int retval;
	unsigned long flags;
	unsigned int mflag, mstat;
	unsigned char startc, stopc;
	volatile struct board_chan *bc;
	struct channel *ch = (struct channel *) tty->driver_data;
	void __user *argp = (void __user *)arg;
	
	if (ch)
		bc = ch->brdchan;
	else 
	{
		printk(KERN_ERR "<Error> - ch is NULL in pc_ioctl!\n");
		return(-EINVAL);
	}

	save_flags(flags);

	/* -------------------------------------------------------------------
		For POSIX compliance we need to add more ioctls.  See tty_ioctl.c
		in /usr/src/linux/drivers/char for a good example.  In particular 
		think about adding TCSETAF, TCSETAW, TCSETA, TCSETSF, TCSETSW, TCSETS.
	---------------------------------------------------------------------- */

	switch (cmd) 
	{ /* Begin switch cmd */

		case TCGETS:
			if (copy_to_user(argp, 
					 tty->termios, sizeof(struct termios)))
				return -EFAULT;
			return(0);

		case TCGETA:
			return get_termio(tty, argp);

		case TCSBRK:	/* SVID version: non-zero arg --> no break */

			retval = tty_check_change(tty);
			if (retval)
				return retval;

			/* Setup an event to indicate when the transmit buffer empties */

			setup_empty_event(tty,ch);		
			tty_wait_until_sent(tty, 0);
			if (!arg)
				digi_send_break(ch, HZ/4);    /* 1/4 second */
			return 0;

		case TCSBRKP:	/* support for POSIX tcsendbreak() */

			retval = tty_check_change(tty);
			if (retval)
				return retval;

			/* Setup an event to indicate when the transmit buffer empties */

			setup_empty_event(tty,ch);		
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
			ch->omodem |= ch->m_dtr;
			cli();
			globalwinon(ch);
			fepcmd(ch, SETMODEM, ch->m_dtr, 0, 10, 1);
			memoff(ch);
			restore_flags(flags);
			break;

		case TIOCCDTR:
			ch->omodem &= ~ch->m_dtr;
			cli();
			globalwinon(ch);
			fepcmd(ch, SETMODEM, 0, ch->m_dtr, 10, 1);
			memoff(ch);
			restore_flags(flags);
			break;

		case DIGI_GETA:
			if (copy_to_user(argp, &ch->digiext, sizeof(digi_t)))
				return -EFAULT;
			break;

		case DIGI_SETAW:
		case DIGI_SETAF:
			if ((cmd) == (DIGI_SETAW)) 
			{
				/* Setup an event to indicate when the transmit buffer empties */

				setup_empty_event(tty,ch);		
				tty_wait_until_sent(tty, 0);
			}
			else 
			{
				/* ldisc lock already held in ioctl */
				if (tty->ldisc.flush_buffer)
					tty->ldisc.flush_buffer(tty);
			}

			/* Fall Thru */

		case DIGI_SETA:
			if (copy_from_user(&ch->digiext, argp, sizeof(digi_t)))
				return -EFAULT;
			
			if (ch->digiext.digi_flags & DIGI_ALTPIN) 
			{
				ch->dcd = ch->m_dsr;
				ch->dsr = ch->m_dcd;
			} 
			else 
			{
				ch->dcd = ch->m_dcd;
				ch->dsr = ch->m_dsr;
			}
		
			cli();
			globalwinon(ch);

			/* -----------------------------------------------------------------
				The below routine generally sets up parity, baud, flow control 
				issues, etc.... It effect both control flags and input flags.
			------------------------------------------------------------------- */

			epcaparam(tty,ch);
			memoff(ch);
			restore_flags(flags);
			break;

		case DIGI_GETFLOW:
		case DIGI_GETAFLOW:
			cli();	
			globalwinon(ch);
			if ((cmd) == (DIGI_GETFLOW)) 
			{
				dflow.startc = bc->startc;
				dflow.stopc = bc->stopc;
			}
			else 
			{
				dflow.startc = bc->startca;
				dflow.stopc = bc->stopca;
			}
			memoff(ch);
			restore_flags(flags);

			if (copy_to_user(argp, &dflow, sizeof(dflow)))
				return -EFAULT;
			break;

		case DIGI_SETAFLOW:
		case DIGI_SETFLOW:
			if ((cmd) == (DIGI_SETFLOW)) 
			{
				startc = ch->startc;
				stopc = ch->stopc;
			} 
			else 
			{
				startc = ch->startca;
				stopc = ch->stopca;
			}

			if (copy_from_user(&dflow, argp, sizeof(dflow)))
				return -EFAULT;

			if (dflow.startc != startc || dflow.stopc != stopc) 
			{ /* Begin  if setflow toggled */
				cli();
				globalwinon(ch);

				if ((cmd) == (DIGI_SETFLOW)) 
				{
					ch->fepstartc = ch->startc = dflow.startc;
					ch->fepstopc = ch->stopc = dflow.stopc;
					fepcmd(ch, SONOFFC, ch->fepstartc, ch->fepstopc, 0, 1);
				} 
				else 
				{
					ch->fepstartca = ch->startca = dflow.startc;
					ch->fepstopca  = ch->stopca = dflow.stopc;
					fepcmd(ch, SAUXONOFFC, ch->fepstartca, ch->fepstopca, 0, 1);
				}

				if	(ch->statusflags & TXSTOPPED)
					pc_start(tty);

				memoff(ch);
				restore_flags(flags);

			} /* End if setflow toggled */
			break;

		default:
			return -ENOIOCTLCMD;

	} /* End switch cmd */

	return 0;

} /* End pc_ioctl */

/* --------------------- Begin pc_set_termios  ----------------------- */

static void pc_set_termios(struct tty_struct *tty, struct termios *old_termios)
{ /* Begin pc_set_termios */

	struct channel *ch;
	unsigned long flags;

	/* ---------------------------------------------------------
		verifyChannel returns the channel from the tty struct
		if it is valid.  This serves as a sanity check.
	------------------------------------------------------------- */

	if ((ch = verifyChannel(tty)) != NULL) 
	{ /* Begin if channel valid */

		save_flags(flags);
		cli();
		globalwinon(ch);
		epcaparam(tty, ch);
		memoff(ch);

		if ((old_termios->c_cflag & CRTSCTS) &&
			 ((tty->termios->c_cflag & CRTSCTS) == 0))
			tty->hw_stopped = 0;

		if (!(old_termios->c_cflag & CLOCAL) &&
			 (tty->termios->c_cflag & CLOCAL))
			wake_up_interruptible(&ch->open_wait);

		restore_flags(flags);

	} /* End if channel valid */

} /* End pc_set_termios */

/* --------------------- Begin do_softint  ----------------------- */

static void do_softint(void *private_)
{ /* Begin do_softint */

	struct channel *ch = (struct channel *) private_;
	

	/* Called in response to a modem change event */

	if (ch && ch->magic == EPCA_MAGIC) 
	{ /* Begin EPCA_MAGIC */

		struct tty_struct *tty = ch->tty;

		if (tty && tty->driver_data) 
		{ 
			if (test_and_clear_bit(EPCA_EVENT_HANGUP, &ch->event)) 
			{ /* Begin if clear_bit */

				tty_hangup(tty);	/* FIXME: module removal race here - AKPM */
				wake_up_interruptible(&ch->open_wait);
				ch->asyncflags &= ~ASYNC_NORMAL_ACTIVE;

			} /* End if clear_bit */
		}

	} /* End EPCA_MAGIC */
} /* End do_softint */

/* ------------------------------------------------------------
	pc_stop and pc_start provide software flow control to the 
	routine and the pc_ioctl routine.
---------------------------------------------------------------- */

/* --------------------- Begin pc_stop  ----------------------- */

static void pc_stop(struct tty_struct *tty)
{ /* Begin pc_stop */

	struct channel *ch;
	unsigned long flags;

	/* ---------------------------------------------------------
		verifyChannel returns the channel from the tty struct
		if it is valid.  This serves as a sanity check.
	------------------------------------------------------------- */

	if ((ch = verifyChannel(tty)) != NULL) 
	{ /* Begin if valid channel */

		save_flags(flags); 
		cli();

		if ((ch->statusflags & TXSTOPPED) == 0) 
		{ /* Begin if transmit stop requested */

			globalwinon(ch);

			/* STOP transmitting now !! */

			fepcmd(ch, PAUSETX, 0, 0, 0, 0);

			ch->statusflags |= TXSTOPPED;
			memoff(ch);

		} /* End if transmit stop requested */

		restore_flags(flags);

	} /* End if valid channel */

} /* End pc_stop */

/* --------------------- Begin pc_start  ----------------------- */

static void pc_start(struct tty_struct *tty)
{ /* Begin pc_start */

	struct channel *ch;

	/* ---------------------------------------------------------
		verifyChannel returns the channel from the tty struct
		if it is valid.  This serves as a sanity check.
	------------------------------------------------------------- */

	if ((ch = verifyChannel(tty)) != NULL) 
	{ /* Begin if channel valid */

		unsigned long flags;

		save_flags(flags);
		cli();

		/* Just in case output was resumed because of a change in Digi-flow */
		if (ch->statusflags & TXSTOPPED) 
		{ /* Begin transmit resume requested */

			volatile struct board_chan *bc;

			globalwinon(ch);
			bc = ch->brdchan;
			if (ch->statusflags & LOWWAIT)
				bc->ilow = 1;

			/* Okay, you can start transmitting again... */

			fepcmd(ch, RESUMETX, 0, 0, 0, 0);

			ch->statusflags &= ~TXSTOPPED;
			memoff(ch);

		} /* End transmit resume requested */

		restore_flags(flags);

	} /* End if channel valid */

} /* End pc_start */

/* ------------------------------------------------------------------
	The below routines pc_throttle and pc_unthrottle are used 
	to slow (And resume) the receipt of data into the kernels
	receive buffers.  The exact occurrence of this depends on the
	size of the kernels receive buffer and what the 'watermarks'
	are set to for that buffer.  See the n_ttys.c file for more
	details. 
______________________________________________________________________ */
/* --------------------- Begin throttle  ----------------------- */

static void pc_throttle(struct tty_struct * tty)
{ /* Begin pc_throttle */

	struct channel *ch;
	unsigned long flags;

	/* ---------------------------------------------------------
		verifyChannel returns the channel from the tty struct
		if it is valid.  This serves as a sanity check.
	------------------------------------------------------------- */

	if ((ch = verifyChannel(tty)) != NULL) 
	{ /* Begin if channel valid */


		save_flags(flags);
		cli();

		if ((ch->statusflags & RXSTOPPED) == 0)
		{
			globalwinon(ch);
			fepcmd(ch, PAUSERX, 0, 0, 0, 0);

			ch->statusflags |= RXSTOPPED;
			memoff(ch);
		}
		restore_flags(flags);

	} /* End if channel valid */

} /* End pc_throttle */

/* --------------------- Begin unthrottle  ----------------------- */

static void pc_unthrottle(struct tty_struct *tty)
{ /* Begin pc_unthrottle */

	struct channel *ch;
	unsigned long flags;
	volatile struct board_chan *bc;


	/* ---------------------------------------------------------
		verifyChannel returns the channel from the tty struct
		if it is valid.  This serves as a sanity check.
	------------------------------------------------------------- */

	if ((ch = verifyChannel(tty)) != NULL) 
	{ /* Begin if channel valid */


		/* Just in case output was resumed because of a change in Digi-flow */
		save_flags(flags);
		cli();

		if (ch->statusflags & RXSTOPPED) 
		{

			globalwinon(ch);
			bc = ch->brdchan;
			fepcmd(ch, RESUMERX, 0, 0, 0, 0);

			ch->statusflags &= ~RXSTOPPED;
			memoff(ch);
		}
		restore_flags(flags);

	} /* End if channel valid */

} /* End pc_unthrottle */

/* --------------------- Begin digi_send_break  ----------------------- */

void digi_send_break(struct channel *ch, int msec)
{ /* Begin digi_send_break */

	unsigned long flags;

	save_flags(flags);
	cli();
	globalwinon(ch);

	/* -------------------------------------------------------------------- 
	   Maybe I should send an infinite break here, schedule() for
	   msec amount of time, and then stop the break.  This way,
	   the user can't screw up the FEP by causing digi_send_break()
	   to be called (i.e. via an ioctl()) more than once in msec amount 
	   of time.  Try this for now...
	------------------------------------------------------------------------ */

	fepcmd(ch, SENDBREAK, msec, 0, 10, 0);
	memoff(ch);

	restore_flags(flags);

} /* End digi_send_break */

/* --------------------- Begin setup_empty_event  ----------------------- */

static void setup_empty_event(struct tty_struct *tty, struct channel *ch)
{ /* Begin setup_empty_event */

	volatile struct board_chan *bc = ch->brdchan;
	unsigned long int flags;

	save_flags(flags);
	cli();
	globalwinon(ch);
	ch->statusflags |= EMPTYWAIT;
	
	/* ------------------------------------------------------------------
		When set the iempty flag request a event to be generated when the 
		transmit buffer is empty (If there is no BREAK in progress).
	--------------------------------------------------------------------- */

	bc->iempty = 1;
	memoff(ch);
	restore_flags(flags);

} /* End setup_empty_event */

/* --------------------- Begin get_termio ----------------------- */

static int get_termio(struct tty_struct * tty, struct termio __user * termio)
{ /* Begin get_termio */
	return kernel_termios_to_user_termio(termio, tty->termios);
} /* End get_termio */
/* ---------------------- Begin epca_setup  -------------------------- */
void epca_setup(char *str, int *ints)
{ /* Begin epca_setup */

	struct board_info board;
	int               index, loop, last;
	char              *temp, *t2;
	unsigned          len;

	/* ----------------------------------------------------------------------
		If this routine looks a little strange it is because it is only called
		if a LILO append command is given to boot the kernel with parameters.  
		In this way, we can provide the user a method of changing his board
		configuration without rebuilding the kernel.
	----------------------------------------------------------------------- */
	if (!liloconfig) 
		liloconfig = 1; 

	memset(&board, 0, sizeof(board));

	/* Assume the data is int first, later we can change it */
	/* I think that array position 0 of ints holds the number of args */
	for (last = 0, index = 1; index <= ints[0]; index++)
		switch(index)
		{ /* Begin parse switch */

			case 1:
				board.status = ints[index];
				
				/* ---------------------------------------------------------
					We check for 2 (As opposed to 1; because 2 is a flag
					instructing the driver to ignore epcaconfig.)  For this
					reason we check for 2.
				------------------------------------------------------------ */ 
				if (board.status == 2)
				{ /* Begin ignore epcaconfig as well as lilo cmd line */
					nbdevs = 0;
					num_cards = 0;
					return;
				} /* End ignore epcaconfig as well as lilo cmd line */
	
				if (board.status > 2)
				{
					printk(KERN_ERR "<Error> - epca_setup: Invalid board status 0x%x\n", board.status);
					invalid_lilo_config = 1;
					setup_error_code |= INVALID_BOARD_STATUS;
					return;
				}
				last = index;
				break;

			case 2:
				board.type = ints[index];
				if (board.type >= PCIXEM) 
				{
					printk(KERN_ERR "<Error> - epca_setup: Invalid board type 0x%x\n", board.type);
					invalid_lilo_config = 1;
					setup_error_code |= INVALID_BOARD_TYPE;
					return;
				}
				last = index;
				break;

			case 3:
				board.altpin = ints[index];
				if (board.altpin > 1)
				{
					printk(KERN_ERR "<Error> - epca_setup: Invalid board altpin 0x%x\n", board.altpin);
					invalid_lilo_config = 1;
					setup_error_code |= INVALID_ALTPIN;
					return;
				}
				last = index;
				break;

			case 4:
				board.numports = ints[index];
				if ((board.numports < 2) || (board.numports > 256))
				{
					printk(KERN_ERR "<Error> - epca_setup: Invalid board numports 0x%x\n", board.numports);
					invalid_lilo_config = 1;
					setup_error_code |= INVALID_NUM_PORTS;
					return;
				}
				nbdevs += board.numports;
				last = index;
				break;

			case 5:
				board.port = (unsigned char *)ints[index];
				if (ints[index] <= 0)
				{
					printk(KERN_ERR "<Error> - epca_setup: Invalid io port 0x%x\n", (unsigned int)board.port);
					invalid_lilo_config = 1;
					setup_error_code |= INVALID_PORT_BASE;
					return;
				}
				last = index;
				break;

			case 6:
				board.membase = (unsigned char *)ints[index];
				if (ints[index] <= 0)
				{
					printk(KERN_ERR "<Error> - epca_setup: Invalid memory base 0x%x\n",(unsigned int)board.membase);
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

	while (str && *str) 
	{ /* Begin while there is a string arg */

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

		switch(index)
		{
			case 1:
				len = strlen(str);
				if (strncmp("Disable", str, len) == 0) 
					board.status = 0;
				else
				if (strncmp("Enable", str, len) == 0)
					board.status = 1;
				else
				{
					printk(KERN_ERR "<Error> - epca_setup: Invalid status %s\n", str);
					invalid_lilo_config = 1;
					setup_error_code |= INVALID_BOARD_STATUS;
					return;
				}
				last = index;
				break;

			case 2:

				for(loop = 0; loop < EPCA_NUM_TYPES; loop++)
					if (strcmp(board_desc[loop], str) == 0)
						break;


				/* ---------------------------------------------------------------
					If the index incremented above refers to a legitamate board 
					type set it here. 
				------------------------------------------------------------------*/

				if (index < EPCA_NUM_TYPES) 
					board.type = loop;
				else
				{
					printk(KERN_ERR "<Error> - epca_setup: Invalid board type: %s\n", str);
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
				else
				if (strncmp("Enable", str, len) == 0)
					board.altpin = 1;
				else
				{
					printk(KERN_ERR "<Error> - epca_setup: Invalid altpin %s\n", str);
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

				if (*t2)
				{
					printk(KERN_ERR "<Error> - epca_setup: Invalid port count %s\n", str);
					invalid_lilo_config = 1;
					setup_error_code |= INVALID_NUM_PORTS;
					return;
				}

				/* ------------------------------------------------------------
					There is not a man page for simple_strtoul but the code can be 
					found in vsprintf.c.  The first argument is the string to 
					translate (To an unsigned long obviously),  the second argument
					can be the address of any character variable or a NULL.  If a
					variable is given, the end pointer of the string will be stored 
					in that variable; if a NULL is given the end pointer will 
					not be returned.  The last argument is the base to use.  If 
					a 0 is indicated, the routine will attempt to determine the 
					proper base by looking at the values prefix (A '0' for octal,
					a 'x' for hex, etc ...  If a value is given it will use that 
					value as the base. 
				---------------------------------------------------------------- */ 
				board.numports = simple_strtoul(str, NULL, 0);
				nbdevs += board.numports;
				last = index;
				break;

			case 5:
				t2 = str;
				while (isxdigit(*t2))
					t2++;

				if (*t2)
				{
					printk(KERN_ERR "<Error> - epca_setup: Invalid i/o address %s\n", str);
					invalid_lilo_config = 1;
					setup_error_code |= INVALID_PORT_BASE;
					return;
				}

				board.port = (unsigned char *)simple_strtoul(str, NULL, 16);
				last = index;
				break;

			case 6:
				t2 = str;
				while (isxdigit(*t2))
					t2++;

				if (*t2)
				{
					printk(KERN_ERR "<Error> - epca_setup: Invalid memory base %s\n",str);
					invalid_lilo_config = 1;
					setup_error_code |= INVALID_MEM_BASE;
					return;
				}

				board.membase = (unsigned char *)simple_strtoul(str, NULL, 16);
				last = index;
				break;

			default:
				printk(KERN_ERR "PC/Xx: Too many string parms\n");
				return;
		}
		str = temp;

	} /* End while there is a string arg */


	if (last < 6)  
	{
		printk(KERN_ERR "PC/Xx: Insufficient parms specified\n");
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

} /* End epca_setup */



#ifdef ENABLE_PCI
/* ------------------------ Begin init_PCI  --------------------------- */

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


static int __devinit epca_init_one (struct pci_dev *pdev,
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
	boards[board_idx].port =
		(unsigned char *)((char *) addr + PCI_IO_OFFSET);
	boards[board_idx].membase =
		(unsigned char *)((char *) addr);

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

	/* --------------------------------------------------------------
		I don't know what the below does, but the hardware guys say
		its required on everything except PLX (In this case XRJ).
	---------------------------------------------------------------- */
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
{ /* Begin init_PCI */
	memset (&epca_driver, 0, sizeof (epca_driver));
	epca_driver.name = "epca";
	epca_driver.id_table = epca_pci_tbl;
	epca_driver.probe = epca_init_one;

	return pci_register_driver(&epca_driver);
} /* End init_PCI */

#endif /* ENABLE_PCI */

MODULE_LICENSE("GPL");
