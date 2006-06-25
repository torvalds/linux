/*
 *      specialix.c  -- specialix IO8+ multiport serial driver.
 *
 *      Copyright (C) 1997  Roger Wolff (R.E.Wolff@BitWizard.nl)
 *      Copyright (C) 1994-1996  Dmitry Gorodchanin (pgmdsg@ibi.com)
 *
 *      Specialix pays for the development and support of this driver.
 *      Please DO contact io8-linux@specialix.co.uk if you require
 *      support. But please read the documentation (specialix.txt)
 *      first.
 *
 *      This driver was developped in the BitWizard linux device
 *      driver service. If you require a linux device driver for your
 *      product, please contact devices@BitWizard.nl for a quote.
 *
 *      This code is firmly based on the riscom/8 serial driver,
 *      written by Dmitry Gorodchanin. The specialix IO8+ card
 *      programming information was obtained from the CL-CD1865 Data
 *      Book, and Specialix document number 6200059: IO8+ Hardware
 *      Functional Specification.
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License as
 *      published by the Free Software Foundation; either version 2 of
 *      the License, or (at your option) any later version.
 *
 *      This program is distributed in the hope that it will be
 *      useful, but WITHOUT ANY WARRANTY; without even the implied
 *      warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 *      PURPOSE.  See the GNU General Public License for more details.
 *
 *      You should have received a copy of the GNU General Public
 *      License along with this program; if not, write to the Free
 *      Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139,
 *      USA.
 *
 * Revision history:
 *
 * Revision 1.0:  April 1st 1997.
 *                Initial release for alpha testing.
 * Revision 1.1:  April 14th 1997.
 *                Incorporated Richard Hudsons suggestions,
 *                removed some debugging printk's.
 * Revision 1.2:  April 15th 1997.
 *                Ported to 2.1.x kernels.
 * Revision 1.3:  April 17th 1997
 *                Backported to 2.0. (Compatibility macros).
 * Revision 1.4:  April 18th 1997
 *                Fixed DTR/RTS bug that caused the card to indicate
 *                "don't send data" to a modem after the password prompt.
 *                Fixed bug for premature (fake) interrupts.
 * Revision 1.5:  April 19th 1997
 *                fixed a minor typo in the header file, cleanup a little.
 *                performance warnings are now MAXed at once per minute.
 * Revision 1.6:  May 23 1997
 *                Changed the specialix=... format to include interrupt.
 * Revision 1.7:  May 27 1997
 *                Made many more debug printk's a compile time option.
 * Revision 1.8:  Jul 1  1997
 *                port to linux-2.1.43 kernel.
 * Revision 1.9:  Oct 9  1998
 *                Added stuff for the IO8+/PCI version.
 * Revision 1.10: Oct 22  1999 / Jan 21 2000.
 *                Added stuff for setserial.
 *                Nicolas Mailhot (Nicolas.Mailhot@email.enst.fr)
 *
 */

#define VERSION "1.11"


/*
 * There is a bunch of documentation about the card, jumpers, config
 * settings, restrictions, cables, device names and numbers in
 * Documentation/specialix.txt
 */

#include <linux/config.h>
#include <linux/module.h>

#include <asm/io.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/ioport.h>
#include <linux/interrupt.h>
#include <linux/errno.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/mm.h>
#include <linux/serial.h>
#include <linux/fcntl.h>
#include <linux/major.h>
#include <linux/delay.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <asm/uaccess.h>

#include "specialix_io8.h"
#include "cd1865.h"


/*
   This driver can spew a whole lot of debugging output at you. If you
   need maximum performance, you should disable the DEBUG define. To
   aid in debugging in the field, I'm leaving the compile-time debug
   features enabled, and disable them "runtime". That allows me to
   instruct people with problems to enable debugging without requiring
   them to recompile...
*/
#define DEBUG

static int sx_debug;
static int sx_rxfifo = SPECIALIX_RXFIFO;

#ifdef DEBUG
#define dprintk(f, str...) if (sx_debug & f) printk (str)
#else
#define dprintk(f, str...) /* nothing */
#endif

#define SX_DEBUG_FLOW    0x0001
#define SX_DEBUG_DATA    0x0002
#define SX_DEBUG_PROBE   0x0004
#define SX_DEBUG_CHAN    0x0008
#define SX_DEBUG_INIT    0x0010
#define SX_DEBUG_RX      0x0020
#define SX_DEBUG_TX      0x0040
#define SX_DEBUG_IRQ     0x0080
#define SX_DEBUG_OPEN    0x0100
#define SX_DEBUG_TERMIOS 0x0200
#define SX_DEBUG_SIGNALS 0x0400
#define SX_DEBUG_FIFO    0x0800


#define func_enter() dprintk (SX_DEBUG_FLOW, "io8: enter %s\n",__FUNCTION__)
#define func_exit()  dprintk (SX_DEBUG_FLOW, "io8: exit  %s\n", __FUNCTION__)

#define jiffies_from_ms(a) ((((a) * HZ)/1000)+1)


/* Configurable options: */

/* Am I paranoid or not ? ;-) */
#define SPECIALIX_PARANOIA_CHECK

/* Do I trust the IRQ from the card? (enabeling it doesn't seem to help)
   When the IRQ routine leaves the chip in a state that is keeps on
   requiring attention, the timer doesn't help either. */
#undef SPECIALIX_TIMER

#ifdef SPECIALIX_TIMER
static int sx_poll = HZ;
#endif



/*
 * The following defines are mostly for testing purposes. But if you need
 * some nice reporting in your syslog, you can define them also.
 */
#undef SX_REPORT_FIFO
#undef SX_REPORT_OVERRUN



#ifdef CONFIG_SPECIALIX_RTSCTS
#define SX_CRTSCTS(bla) 1
#else
#define SX_CRTSCTS(tty) C_CRTSCTS(tty)
#endif


/* Used to be outb (0xff, 0x80); */
#define short_pause() udelay (1)


#define SPECIALIX_LEGAL_FLAGS \
	(ASYNC_HUP_NOTIFY   | ASYNC_SAK          | ASYNC_SPLIT_TERMIOS   | \
	 ASYNC_SPD_HI       | ASYNC_SPEED_VHI    | ASYNC_SESSION_LOCKOUT | \
	 ASYNC_PGRP_LOCKOUT | ASYNC_CALLOUT_NOHUP)

#undef RS_EVENT_WRITE_WAKEUP
#define RS_EVENT_WRITE_WAKEUP	0

static struct tty_driver *specialix_driver;
static unsigned char * tmp_buf;

static unsigned long baud_table[] =  {
	0, 50, 75, 110, 134, 150, 200, 300, 600, 1200, 1800, 2400, 4800,
	9600, 19200, 38400, 57600, 115200, 0,
};

static struct specialix_board sx_board[SX_NBOARD] =  {
	{ 0, SX_IOBASE1,  9, },
	{ 0, SX_IOBASE2, 11, },
	{ 0, SX_IOBASE3, 12, },
	{ 0, SX_IOBASE4, 15, },
};

static struct specialix_port sx_port[SX_NBOARD * SX_NPORT];


#ifdef SPECIALIX_TIMER
static struct timer_list missed_irq_timer;
static irqreturn_t sx_interrupt(int irq, void * dev_id, struct pt_regs * regs);
#endif



static inline int sx_paranoia_check(struct specialix_port const * port,
				    char *name, const char *routine)
{
#ifdef SPECIALIX_PARANOIA_CHECK
	static const char *badmagic =
		KERN_ERR "sx: Warning: bad specialix port magic number for device %s in %s\n";
	static const char *badinfo =
		KERN_ERR "sx: Warning: null specialix port for device %s in %s\n";

	if (!port) {
		printk(badinfo, name, routine);
		return 1;
	}
	if (port->magic != SPECIALIX_MAGIC) {
		printk(badmagic, name, routine);
		return 1;
	}
#endif
	return 0;
}


/*
 *
 *  Service functions for specialix IO8+ driver.
 *
 */

/* Get board number from pointer */
static inline int board_No (struct specialix_board * bp)
{
	return bp - sx_board;
}


/* Get port number from pointer */
static inline int port_No (struct specialix_port const * port)
{
	return SX_PORT(port - sx_port);
}


/* Get pointer to board from pointer to port */
static inline struct specialix_board * port_Board(struct specialix_port const * port)
{
	return &sx_board[SX_BOARD(port - sx_port)];
}


/* Input Byte from CL CD186x register */
static inline unsigned char sx_in(struct specialix_board  * bp, unsigned short reg)
{
	bp->reg = reg | 0x80;
	outb (reg | 0x80, bp->base + SX_ADDR_REG);
	return inb  (bp->base + SX_DATA_REG);
}


/* Output Byte to CL CD186x register */
static inline void sx_out(struct specialix_board  * bp, unsigned short reg,
			  unsigned char val)
{
	bp->reg = reg | 0x80;
	outb (reg | 0x80, bp->base + SX_ADDR_REG);
	outb (val, bp->base + SX_DATA_REG);
}


/* Input Byte from CL CD186x register */
static inline unsigned char sx_in_off(struct specialix_board  * bp, unsigned short reg)
{
	bp->reg = reg;
	outb (reg, bp->base + SX_ADDR_REG);
	return inb  (bp->base + SX_DATA_REG);
}


/* Output Byte to CL CD186x register */
static inline void sx_out_off(struct specialix_board  * bp, unsigned short reg,
			  unsigned char val)
{
	bp->reg = reg;
	outb (reg, bp->base + SX_ADDR_REG);
	outb (val, bp->base + SX_DATA_REG);
}


/* Wait for Channel Command Register ready */
static inline void sx_wait_CCR(struct specialix_board  * bp)
{
	unsigned long delay, flags;
	unsigned char ccr;

	for (delay = SX_CCR_TIMEOUT; delay; delay--) {
		spin_lock_irqsave(&bp->lock, flags);
		ccr = sx_in(bp, CD186x_CCR);
		spin_unlock_irqrestore(&bp->lock, flags);
		if (!ccr)
			return;
		udelay (1);
	}

	printk(KERN_ERR "sx%d: Timeout waiting for CCR.\n", board_No(bp));
}


/* Wait for Channel Command Register ready */
static inline void sx_wait_CCR_off(struct specialix_board  * bp)
{
	unsigned long delay;
	unsigned char crr;
	unsigned long flags;

	for (delay = SX_CCR_TIMEOUT; delay; delay--) {
		spin_lock_irqsave(&bp->lock, flags);
		crr = sx_in_off(bp, CD186x_CCR);
		spin_unlock_irqrestore(&bp->lock, flags);
		if (!crr)
			return;
		udelay (1);
	}

	printk(KERN_ERR "sx%d: Timeout waiting for CCR.\n", board_No(bp));
}


/*
 *  specialix IO8+ IO range functions.
 */

static inline int sx_request_io_range(struct specialix_board * bp)
{
	return request_region(bp->base,
		bp->flags & SX_BOARD_IS_PCI ? SX_PCI_IO_SPACE : SX_IO_SPACE,
		"specialix IO8+") == NULL;
}


static inline void sx_release_io_range(struct specialix_board * bp)
{
	release_region(bp->base,
	               bp->flags&SX_BOARD_IS_PCI?SX_PCI_IO_SPACE:SX_IO_SPACE);
}


/* Must be called with enabled interrupts */
/* Ugly. Very ugly. Don't use this for anything else than initialization
   code */
static inline void sx_long_delay(unsigned long delay)
{
	unsigned long i;

	for (i = jiffies + delay; time_after(i, jiffies); ) ;
}



/* Set the IRQ using the RTS lines that run to the PAL on the board.... */
static int sx_set_irq ( struct specialix_board *bp)
{
	int virq;
	int i;
	unsigned long flags;

	if (bp->flags & SX_BOARD_IS_PCI)
		return 1;
	switch (bp->irq) {
	/* In the same order as in the docs... */
	case 15: virq = 0;break;
	case 12: virq = 1;break;
	case 11: virq = 2;break;
	case 9:  virq = 3;break;
	default: printk (KERN_ERR "Speclialix: cannot set irq to %d.\n", bp->irq);
	         return 0;
	}
	spin_lock_irqsave(&bp->lock, flags);
	for (i=0;i<2;i++) {
		sx_out(bp, CD186x_CAR, i);
		sx_out(bp, CD186x_MSVRTS, ((virq >> i) & 0x1)? MSVR_RTS:0);
	}
	spin_unlock_irqrestore(&bp->lock, flags);
	return 1;
}


/* Reset and setup CD186x chip */
static int sx_init_CD186x(struct specialix_board  * bp)
{
	unsigned long flags;
	int scaler;
	int rv = 1;

	func_enter();
	sx_wait_CCR_off(bp);			   /* Wait for CCR ready        */
	spin_lock_irqsave(&bp->lock, flags);
	sx_out_off(bp, CD186x_CCR, CCR_HARDRESET);      /* Reset CD186x chip          */
	spin_unlock_irqrestore(&bp->lock, flags);
	sx_long_delay(HZ/20);                      /* Delay 0.05 sec            */
	spin_lock_irqsave(&bp->lock, flags);
	sx_out_off(bp, CD186x_GIVR, SX_ID);             /* Set ID for this chip      */
	sx_out_off(bp, CD186x_GICR, 0);                 /* Clear all bits            */
	sx_out_off(bp, CD186x_PILR1, SX_ACK_MINT);      /* Prio for modem intr       */
	sx_out_off(bp, CD186x_PILR2, SX_ACK_TINT);      /* Prio for transmitter intr */
	sx_out_off(bp, CD186x_PILR3, SX_ACK_RINT);      /* Prio for receiver intr    */
	/* Set RegAckEn */
	sx_out_off(bp, CD186x_SRCR, sx_in (bp, CD186x_SRCR) | SRCR_REGACKEN);

	/* Setting up prescaler. We need 4 ticks per 1 ms */
	scaler =  SX_OSCFREQ/SPECIALIX_TPS;

	sx_out_off(bp, CD186x_PPRH, scaler >> 8);
	sx_out_off(bp, CD186x_PPRL, scaler & 0xff);
	spin_unlock_irqrestore(&bp->lock, flags);

	if (!sx_set_irq (bp)) {
		/* Figure out how to pass this along... */
		printk (KERN_ERR "Cannot set irq to %d.\n", bp->irq);
		rv = 0;
	}

	func_exit();
	return rv;
}


static int read_cross_byte (struct specialix_board *bp, int reg, int bit)
{
	int i;
	int t;
	unsigned long flags;

	spin_lock_irqsave(&bp->lock, flags);
	for (i=0, t=0;i<8;i++) {
		sx_out_off (bp, CD186x_CAR, i);
		if (sx_in_off (bp, reg) & bit)
			t |= 1 << i;
	}
	spin_unlock_irqrestore(&bp->lock, flags);

	return t;
}


#ifdef SPECIALIX_TIMER
void missed_irq (unsigned long data)
{
	unsigned char irq;
	unsigned long flags;
	struct specialix_board  *bp = (struct specialix_board *)data;

	spin_lock_irqsave(&bp->lock, flags);
	irq = sx_in ((struct specialix_board *)data, CD186x_SRSR) &
	                                            (SRSR_RREQint |
	                                             SRSR_TREQint |
	                                             SRSR_MREQint);
	spin_unlock_irqrestore(&bp->lock, flags);
	if (irq) {
		printk (KERN_INFO "Missed interrupt... Calling int from timer. \n");
		sx_interrupt (((struct specialix_board *)data)->irq,
		              (void*)data, NULL);
	}
	missed_irq_timer.expires = jiffies + sx_poll;
	add_timer (&missed_irq_timer);
}
#endif



/* Main probing routine, also sets irq. */
static int sx_probe(struct specialix_board *bp)
{
	unsigned char val1, val2;
#if 0
	int irqs = 0;
	int retries;
#endif
	int rev;
	int chip;

	func_enter();

	if (sx_request_io_range(bp)) {
		func_exit();
		return 1;
	}

	/* Are the I/O ports here ? */
	sx_out_off(bp, CD186x_PPRL, 0x5a);
	short_pause ();
	val1 = sx_in_off(bp, CD186x_PPRL);

	sx_out_off(bp, CD186x_PPRL, 0xa5);
	short_pause ();
	val2 = sx_in_off(bp, CD186x_PPRL);


	if ((val1 != 0x5a) || (val2 != 0xa5)) {
		printk(KERN_INFO "sx%d: specialix IO8+ Board at 0x%03x not found.\n",
		       board_No(bp), bp->base);
		sx_release_io_range(bp);
		func_exit();
		return 1;
	}

	/* Check the DSR lines that Specialix uses as board
	   identification */
	val1 = read_cross_byte (bp, CD186x_MSVR, MSVR_DSR);
	val2 = read_cross_byte (bp, CD186x_MSVR, MSVR_RTS);
	dprintk (SX_DEBUG_INIT, "sx%d: DSR lines are: %02x, rts lines are: %02x\n",
	        board_No(bp),  val1, val2);

	/* They managed to switch the bit order between the docs and
	   the IO8+ card. The new PCI card now conforms to old docs.
	   They changed the PCI docs to reflect the situation on the
	   old card. */
	val2 = (bp->flags & SX_BOARD_IS_PCI)?0x4d : 0xb2;
	if (val1 != val2) {
		printk(KERN_INFO "sx%d: specialix IO8+ ID %02x at 0x%03x not found (%02x).\n",
		       board_No(bp), val2, bp->base, val1);
		sx_release_io_range(bp);
		func_exit();
		return 1;
	}


#if 0
	/* It's time to find IRQ for this board */
	for (retries = 0; retries < 5 && irqs <= 0; retries++) {
		irqs = probe_irq_on();
		sx_init_CD186x(bp);	       		/* Reset CD186x chip       */
		sx_out(bp, CD186x_CAR, 2);               /* Select port 2          */
		sx_wait_CCR(bp);
		sx_out(bp, CD186x_CCR, CCR_TXEN);        /* Enable transmitter     */
		sx_out(bp, CD186x_IER, IER_TXRDY);       /* Enable tx empty intr   */
		sx_long_delay(HZ/20);
		irqs = probe_irq_off(irqs);

		dprintk (SX_DEBUG_INIT, "SRSR = %02x, ", sx_in(bp, CD186x_SRSR));
		dprintk (SX_DEBUG_INIT, "TRAR = %02x, ", sx_in(bp, CD186x_TRAR));
		dprintk (SX_DEBUG_INIT, "GIVR = %02x, ", sx_in(bp, CD186x_GIVR));
		dprintk (SX_DEBUG_INIT, "GICR = %02x, ", sx_in(bp, CD186x_GICR));
		dprintk (SX_DEBUG_INIT, "\n");

		/* Reset CD186x again      */
		if (!sx_init_CD186x(bp)) {
			/* Hmmm. This is dead code anyway. */
		}

		dprintk (SX_DEBUG_INIT "val1 = %02x, val2 = %02x, val3 = %02x.\n",
		        val1, val2, val3);

	}

#if 0
	if (irqs <= 0) {
		printk(KERN_ERR "sx%d: Can't find IRQ for specialix IO8+ board at 0x%03x.\n",
		       board_No(bp), bp->base);
		sx_release_io_range(bp);
		func_exit();
		return 1;
	}
#endif
	printk (KERN_INFO "Started with irq=%d, but now have irq=%d.\n", bp->irq, irqs);
	if (irqs > 0)
		bp->irq = irqs;
#endif
	/* Reset CD186x again  */
	if (!sx_init_CD186x(bp)) {
		sx_release_io_range(bp);
		func_exit();
		return 1;
	}

	sx_request_io_range(bp);
	bp->flags |= SX_BOARD_PRESENT;

	/* Chip           revcode   pkgtype
	                  GFRCR     SRCR bit 7
	   CD180 rev B    0x81      0
	   CD180 rev C    0x82      0
	   CD1864 rev A   0x82      1
	   CD1865 rev A   0x83      1  -- Do not use!!! Does not work.
	   CD1865 rev B   0x84      1
	 -- Thanks to Gwen Wang, Cirrus Logic.
	 */

	switch (sx_in_off(bp, CD186x_GFRCR)) {
	case 0x82:chip = 1864;rev='A';break;
	case 0x83:chip = 1865;rev='A';break;
	case 0x84:chip = 1865;rev='B';break;
	case 0x85:chip = 1865;rev='C';break; /* Does not exist at this time */
	default:chip=-1;rev='x';
	}

	dprintk (SX_DEBUG_INIT, " GFCR = 0x%02x\n", sx_in_off(bp, CD186x_GFRCR) );

#ifdef SPECIALIX_TIMER
	init_timer (&missed_irq_timer);
	missed_irq_timer.function = missed_irq;
	missed_irq_timer.data = (unsigned long) bp;
	missed_irq_timer.expires = jiffies + sx_poll;
	add_timer (&missed_irq_timer);
#endif

	printk(KERN_INFO"sx%d: specialix IO8+ board detected at 0x%03x, IRQ %d, CD%d Rev. %c.\n",
	       board_No(bp),
	       bp->base, bp->irq,
	       chip, rev);

	func_exit();
	return 0;
}

/*
 *
 *  Interrupt processing routines.
 * */

static inline void sx_mark_event(struct specialix_port * port, int event)
{
	func_enter();

	set_bit(event, &port->event);
	schedule_work(&port->tqueue);

	func_exit();
}


static inline struct specialix_port * sx_get_port(struct specialix_board * bp,
					       unsigned char const * what)
{
	unsigned char channel;
	struct specialix_port * port = NULL;

	channel = sx_in(bp, CD186x_GICR) >> GICR_CHAN_OFF;
	dprintk (SX_DEBUG_CHAN, "channel: %d\n", channel);
	if (channel < CD186x_NCH) {
		port = &sx_port[board_No(bp) * SX_NPORT + channel];
		dprintk (SX_DEBUG_CHAN, "port: %d %p flags: 0x%x\n",board_No(bp) * SX_NPORT + channel,  port, port->flags & ASYNC_INITIALIZED);

		if (port->flags & ASYNC_INITIALIZED) {
			dprintk (SX_DEBUG_CHAN, "port: %d %p\n", channel, port);
			func_exit();
			return port;
		}
	}
	printk(KERN_INFO "sx%d: %s interrupt from invalid port %d\n",
	       board_No(bp), what, channel);
	return NULL;
}


static inline void sx_receive_exc(struct specialix_board * bp)
{
	struct specialix_port *port;
	struct tty_struct *tty;
	unsigned char status;
	unsigned char ch, flag;

	func_enter();

	port = sx_get_port(bp, "Receive");
	if (!port) {
		dprintk (SX_DEBUG_RX, "Hmm, couldn't find port.\n");
		func_exit();
		return;
	}
	tty = port->tty;

	status = sx_in(bp, CD186x_RCSR);

	dprintk (SX_DEBUG_RX, "status: 0x%x\n", status);
	if (status & RCSR_OE) {
		port->overrun++;
		dprintk(SX_DEBUG_FIFO, "sx%d: port %d: Overrun. Total %ld overruns.\n",
		       board_No(bp), port_No(port), port->overrun);
	}
	status &= port->mark_mask;

	/* This flip buffer check needs to be below the reading of the
	   status register to reset the chip's IRQ.... */
	if (tty_buffer_request_room(tty, 1) == 0) {
		dprintk(SX_DEBUG_FIFO, "sx%d: port %d: Working around flip buffer overflow.\n",
		       board_No(bp), port_No(port));
		func_exit();
		return;
	}

	ch = sx_in(bp, CD186x_RDR);
	if (!status) {
		func_exit();
		return;
	}
	if (status & RCSR_TOUT) {
		printk(KERN_INFO "sx%d: port %d: Receiver timeout. Hardware problems ?\n",
		       board_No(bp), port_No(port));
		func_exit();
		return;

	} else if (status & RCSR_BREAK) {
		dprintk(SX_DEBUG_RX, "sx%d: port %d: Handling break...\n",
		       board_No(bp), port_No(port));
		flag = TTY_BREAK;
		if (port->flags & ASYNC_SAK)
			do_SAK(tty);

	} else if (status & RCSR_PE)
		flag = TTY_PARITY;

	else if (status & RCSR_FE)
		flag = TTY_FRAME;

	else if (status & RCSR_OE)
		flag = TTY_OVERRUN;

	else
		flag = TTY_NORMAL;

	if(tty_insert_flip_char(tty, ch, flag))
		tty_flip_buffer_push(tty);
	func_exit();
}


static inline void sx_receive(struct specialix_board * bp)
{
	struct specialix_port *port;
	struct tty_struct *tty;
	unsigned char count;

	func_enter();

	if (!(port = sx_get_port(bp, "Receive"))) {
		dprintk (SX_DEBUG_RX, "Hmm, couldn't find port.\n");
		func_exit();
		return;
	}
	tty = port->tty;

	count = sx_in(bp, CD186x_RDCR);
	dprintk (SX_DEBUG_RX, "port: %p: count: %d\n", port, count);
	port->hits[count > 8 ? 9 : count]++;

	tty_buffer_request_room(tty, count);

	while (count--)
		tty_insert_flip_char(tty, sx_in(bp, CD186x_RDR), TTY_NORMAL);
	tty_flip_buffer_push(tty);
	func_exit();
}


static inline void sx_transmit(struct specialix_board * bp)
{
	struct specialix_port *port;
	struct tty_struct *tty;
	unsigned char count;

	func_enter();
	if (!(port = sx_get_port(bp, "Transmit"))) {
		func_exit();
		return;
	}
	dprintk (SX_DEBUG_TX, "port: %p\n", port);
	tty = port->tty;

	if (port->IER & IER_TXEMPTY) {
		/* FIFO drained */
		sx_out(bp, CD186x_CAR, port_No(port));
		port->IER &= ~IER_TXEMPTY;
		sx_out(bp, CD186x_IER, port->IER);
		func_exit();
		return;
	}

	if ((port->xmit_cnt <= 0 && !port->break_length)
	    || tty->stopped || tty->hw_stopped) {
		sx_out(bp, CD186x_CAR, port_No(port));
		port->IER &= ~IER_TXRDY;
		sx_out(bp, CD186x_IER, port->IER);
		func_exit();
		return;
	}

	if (port->break_length) {
		if (port->break_length > 0) {
			if (port->COR2 & COR2_ETC) {
				sx_out(bp, CD186x_TDR, CD186x_C_ESC);
				sx_out(bp, CD186x_TDR, CD186x_C_SBRK);
				port->COR2 &= ~COR2_ETC;
			}
			count = min_t(int, port->break_length, 0xff);
			sx_out(bp, CD186x_TDR, CD186x_C_ESC);
			sx_out(bp, CD186x_TDR, CD186x_C_DELAY);
			sx_out(bp, CD186x_TDR, count);
			if (!(port->break_length -= count))
				port->break_length--;
		} else {
			sx_out(bp, CD186x_TDR, CD186x_C_ESC);
			sx_out(bp, CD186x_TDR, CD186x_C_EBRK);
			sx_out(bp, CD186x_COR2, port->COR2);
			sx_wait_CCR(bp);
			sx_out(bp, CD186x_CCR, CCR_CORCHG2);
			port->break_length = 0;
		}

		func_exit();
		return;
	}

	count = CD186x_NFIFO;
	do {
		sx_out(bp, CD186x_TDR, port->xmit_buf[port->xmit_tail++]);
		port->xmit_tail = port->xmit_tail & (SERIAL_XMIT_SIZE-1);
		if (--port->xmit_cnt <= 0)
			break;
	} while (--count > 0);

	if (port->xmit_cnt <= 0) {
		sx_out(bp, CD186x_CAR, port_No(port));
		port->IER &= ~IER_TXRDY;
		sx_out(bp, CD186x_IER, port->IER);
	}
	if (port->xmit_cnt <= port->wakeup_chars)
		sx_mark_event(port, RS_EVENT_WRITE_WAKEUP);

	func_exit();
}


static inline void sx_check_modem(struct specialix_board * bp)
{
	struct specialix_port *port;
	struct tty_struct *tty;
	unsigned char mcr;
	int msvr_cd;

	dprintk (SX_DEBUG_SIGNALS, "Modem intr. ");
	if (!(port = sx_get_port(bp, "Modem")))
		return;

	tty = port->tty;

	mcr = sx_in(bp, CD186x_MCR);
	printk ("mcr = %02x.\n", mcr);

	if ((mcr & MCR_CDCHG)) {
		dprintk (SX_DEBUG_SIGNALS, "CD just changed... ");
		msvr_cd = sx_in(bp, CD186x_MSVR) & MSVR_CD;
		if (msvr_cd) {
			dprintk (SX_DEBUG_SIGNALS, "Waking up guys in open.\n");
			wake_up_interruptible(&port->open_wait);
		} else {
			dprintk (SX_DEBUG_SIGNALS, "Sending HUP.\n");
			schedule_work(&port->tqueue_hangup);
		}
	}

#ifdef SPECIALIX_BRAIN_DAMAGED_CTS
	if (mcr & MCR_CTSCHG) {
		if (sx_in(bp, CD186x_MSVR) & MSVR_CTS) {
			tty->hw_stopped = 0;
			port->IER |= IER_TXRDY;
			if (port->xmit_cnt <= port->wakeup_chars)
				sx_mark_event(port, RS_EVENT_WRITE_WAKEUP);
		} else {
			tty->hw_stopped = 1;
			port->IER &= ~IER_TXRDY;
		}
		sx_out(bp, CD186x_IER, port->IER);
	}
	if (mcr & MCR_DSSXHG) {
		if (sx_in(bp, CD186x_MSVR) & MSVR_DSR) {
			tty->hw_stopped = 0;
			port->IER |= IER_TXRDY;
			if (port->xmit_cnt <= port->wakeup_chars)
				sx_mark_event(port, RS_EVENT_WRITE_WAKEUP);
		} else {
			tty->hw_stopped = 1;
			port->IER &= ~IER_TXRDY;
		}
		sx_out(bp, CD186x_IER, port->IER);
	}
#endif /* SPECIALIX_BRAIN_DAMAGED_CTS */

	/* Clear change bits */
	sx_out(bp, CD186x_MCR, 0);
}


/* The main interrupt processing routine */
static irqreturn_t sx_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	unsigned char status;
	unsigned char ack;
	struct specialix_board *bp;
	unsigned long loop = 0;
	int saved_reg;
	unsigned long flags;

	func_enter();

	bp = dev_id;
	spin_lock_irqsave(&bp->lock, flags);

	dprintk (SX_DEBUG_FLOW, "enter %s port %d room: %ld\n", __FUNCTION__, port_No(sx_get_port(bp, "INT")), SERIAL_XMIT_SIZE - sx_get_port(bp, "ITN")->xmit_cnt - 1);
	if (!bp || !(bp->flags & SX_BOARD_ACTIVE)) {
		dprintk (SX_DEBUG_IRQ, "sx: False interrupt. irq %d.\n", irq);
		spin_unlock_irqrestore(&bp->lock, flags);
		func_exit();
		return IRQ_NONE;
	}

	saved_reg = bp->reg;

	while ((++loop < 16) && (status = (sx_in(bp, CD186x_SRSR) &
	                                   (SRSR_RREQint |
		                            SRSR_TREQint |
	                                    SRSR_MREQint)))) {
		if (status & SRSR_RREQint) {
			ack = sx_in(bp, CD186x_RRAR);

			if (ack == (SX_ID | GIVR_IT_RCV))
				sx_receive(bp);
			else if (ack == (SX_ID | GIVR_IT_REXC))
				sx_receive_exc(bp);
			else
				printk(KERN_ERR "sx%d: status: 0x%x Bad receive ack 0x%02x.\n",
				       board_No(bp), status, ack);

		} else if (status & SRSR_TREQint) {
			ack = sx_in(bp, CD186x_TRAR);

			if (ack == (SX_ID | GIVR_IT_TX))
				sx_transmit(bp);
			else
				printk(KERN_ERR "sx%d: status: 0x%x Bad transmit ack 0x%02x. port: %d\n",
				       board_No(bp), status, ack, port_No (sx_get_port (bp, "Int")));
		} else if (status & SRSR_MREQint) {
			ack = sx_in(bp, CD186x_MRAR);

			if (ack == (SX_ID | GIVR_IT_MODEM))
				sx_check_modem(bp);
			else
				printk(KERN_ERR "sx%d: status: 0x%x Bad modem ack 0x%02x.\n",
				       board_No(bp), status, ack);

		}

		sx_out(bp, CD186x_EOIR, 0);   /* Mark end of interrupt */
	}
	bp->reg = saved_reg;
	outb (bp->reg, bp->base + SX_ADDR_REG);
	spin_unlock_irqrestore(&bp->lock, flags);
	func_exit();
	return IRQ_HANDLED;
}


/*
 *  Routines for open & close processing.
 */

static void turn_ints_off (struct specialix_board *bp)
{
	unsigned long flags;

	func_enter();
	if (bp->flags & SX_BOARD_IS_PCI) {
		/* This was intended for enabeling the interrupt on the
		 * PCI card. However it seems that it's already enabled
		 * and as PCI interrupts can be shared, there is no real
		 * reason to have to turn it off. */
	}

	spin_lock_irqsave(&bp->lock, flags);
	(void) sx_in_off (bp, 0); /* Turn off interrupts. */
	spin_unlock_irqrestore(&bp->lock, flags);

	func_exit();
}

static void turn_ints_on (struct specialix_board *bp)
{
	unsigned long flags;

	func_enter();

	if (bp->flags & SX_BOARD_IS_PCI) {
		/* play with the PCI chip. See comment above. */
	}
	spin_lock_irqsave(&bp->lock, flags);
	(void) sx_in (bp, 0); /* Turn ON interrupts. */
	spin_unlock_irqrestore(&bp->lock, flags);

	func_exit();
}


/* Called with disabled interrupts */
static inline int sx_setup_board(struct specialix_board * bp)
{
	int error;

	if (bp->flags & SX_BOARD_ACTIVE)
		return 0;

	if (bp->flags & SX_BOARD_IS_PCI)
		error = request_irq(bp->irq, sx_interrupt, SA_INTERRUPT | SA_SHIRQ, "specialix IO8+", bp);
	else
		error = request_irq(bp->irq, sx_interrupt, SA_INTERRUPT, "specialix IO8+", bp);

	if (error)
		return error;

	turn_ints_on (bp);
	bp->flags |= SX_BOARD_ACTIVE;

	return 0;
}


/* Called with disabled interrupts */
static inline void sx_shutdown_board(struct specialix_board *bp)
{
	func_enter();

	if (!(bp->flags & SX_BOARD_ACTIVE)) {
		func_exit();
		return;
	}

	bp->flags &= ~SX_BOARD_ACTIVE;

	dprintk (SX_DEBUG_IRQ, "Freeing IRQ%d for board %d.\n",
		 bp->irq, board_No (bp));
	free_irq(bp->irq, bp);

	turn_ints_off (bp);


	func_exit();
}


/*
 * Setting up port characteristics.
 * Must be called with disabled interrupts
 */
static void sx_change_speed(struct specialix_board *bp, struct specialix_port *port)
{
	struct tty_struct *tty;
	unsigned long baud;
	long tmp;
	unsigned char cor1 = 0, cor3 = 0;
	unsigned char mcor1 = 0, mcor2 = 0;
	static unsigned long again;
	unsigned long flags;

	func_enter();

	if (!(tty = port->tty) || !tty->termios) {
		func_exit();
		return;
	}

	port->IER  = 0;
	port->COR2 = 0;
	/* Select port on the board */
	spin_lock_irqsave(&bp->lock, flags);
	sx_out(bp, CD186x_CAR, port_No(port));

	/* The Specialix board doens't implement the RTS lines.
	   They are used to set the IRQ level. Don't touch them. */
	if (SX_CRTSCTS(tty))
		port->MSVR = MSVR_DTR | (sx_in(bp, CD186x_MSVR) & MSVR_RTS);
	else
		port->MSVR =  (sx_in(bp, CD186x_MSVR) & MSVR_RTS);
	spin_unlock_irqrestore(&bp->lock, flags);
	dprintk (SX_DEBUG_TERMIOS, "sx: got MSVR=%02x.\n", port->MSVR);
	baud = C_BAUD(tty);

	if (baud & CBAUDEX) {
		baud &= ~CBAUDEX;
		if (baud < 1 || baud > 2)
			port->tty->termios->c_cflag &= ~CBAUDEX;
		else
			baud += 15;
	}
	if (baud == 15) {
		if ((port->flags & ASYNC_SPD_MASK) == ASYNC_SPD_HI)
			baud ++;
		if ((port->flags & ASYNC_SPD_MASK) == ASYNC_SPD_VHI)
			baud += 2;
	}


	if (!baud_table[baud]) {
		/* Drop DTR & exit */
		dprintk (SX_DEBUG_TERMIOS, "Dropping DTR...  Hmm....\n");
		if (!SX_CRTSCTS (tty)) {
			port -> MSVR &= ~ MSVR_DTR;
			spin_lock_irqsave(&bp->lock, flags);
			sx_out(bp, CD186x_MSVR, port->MSVR );
			spin_unlock_irqrestore(&bp->lock, flags);
		}
		else
			dprintk (SX_DEBUG_TERMIOS, "Can't drop DTR: no DTR.\n");
		return;
	} else {
		/* Set DTR on */
		if (!SX_CRTSCTS (tty)) {
			port ->MSVR |= MSVR_DTR;
		}
	}

	/*
	 * Now we must calculate some speed depended things
	 */

	/* Set baud rate for port */
	tmp = port->custom_divisor ;
	if ( tmp )
		printk (KERN_INFO "sx%d: Using custom baud rate divisor %ld. \n"
		                  "This is an untested option, please be carefull.\n",
		                  port_No (port), tmp);
	else
		tmp = (((SX_OSCFREQ + baud_table[baud]/2) / baud_table[baud] +
		         CD186x_TPC/2) / CD186x_TPC);

	if ((tmp < 0x10) && time_before(again, jiffies)) {
		again = jiffies + HZ * 60;
		/* Page 48 of version 2.0 of the CL-CD1865 databook */
		if (tmp >= 12) {
			printk (KERN_INFO "sx%d: Baud rate divisor is %ld. \n"
			        "Performance degradation is possible.\n"
			        "Read specialix.txt for more info.\n",
			        port_No (port), tmp);
		} else {
			printk (KERN_INFO "sx%d: Baud rate divisor is %ld. \n"
			        "Warning: overstressing Cirrus chip. "
			        "This might not work.\n"
			        "Read specialix.txt for more info.\n",
			        port_No (port), tmp);
		}
	}
	spin_lock_irqsave(&bp->lock, flags);
	sx_out(bp, CD186x_RBPRH, (tmp >> 8) & 0xff);
	sx_out(bp, CD186x_TBPRH, (tmp >> 8) & 0xff);
	sx_out(bp, CD186x_RBPRL, tmp & 0xff);
	sx_out(bp, CD186x_TBPRL, tmp & 0xff);
	spin_unlock_irqrestore(&bp->lock, flags);
	if (port->custom_divisor) {
		baud = (SX_OSCFREQ + port->custom_divisor/2) / port->custom_divisor;
		baud = ( baud + 5 ) / 10;
	} else
		baud = (baud_table[baud] + 5) / 10;   /* Estimated CPS */

	/* Two timer ticks seems enough to wakeup something like SLIP driver */
	tmp = ((baud + HZ/2) / HZ) * 2 - CD186x_NFIFO;
	port->wakeup_chars = (tmp < 0) ? 0 : ((tmp >= SERIAL_XMIT_SIZE) ?
					      SERIAL_XMIT_SIZE - 1 : tmp);

	/* Receiver timeout will be transmission time for 1.5 chars */
	tmp = (SPECIALIX_TPS + SPECIALIX_TPS/2 + baud/2) / baud;
	tmp = (tmp > 0xff) ? 0xff : tmp;
	spin_lock_irqsave(&bp->lock, flags);
	sx_out(bp, CD186x_RTPR, tmp);
	spin_unlock_irqrestore(&bp->lock, flags);
	switch (C_CSIZE(tty)) {
	 case CS5:
		cor1 |= COR1_5BITS;
		break;
	 case CS6:
		cor1 |= COR1_6BITS;
		break;
	 case CS7:
		cor1 |= COR1_7BITS;
		break;
	 case CS8:
		cor1 |= COR1_8BITS;
		break;
	}

	if (C_CSTOPB(tty))
		cor1 |= COR1_2SB;

	cor1 |= COR1_IGNORE;
	if (C_PARENB(tty)) {
		cor1 |= COR1_NORMPAR;
		if (C_PARODD(tty))
			cor1 |= COR1_ODDP;
		if (I_INPCK(tty))
			cor1 &= ~COR1_IGNORE;
	}
	/* Set marking of some errors */
	port->mark_mask = RCSR_OE | RCSR_TOUT;
	if (I_INPCK(tty))
		port->mark_mask |= RCSR_FE | RCSR_PE;
	if (I_BRKINT(tty) || I_PARMRK(tty))
		port->mark_mask |= RCSR_BREAK;
	if (I_IGNPAR(tty))
		port->mark_mask &= ~(RCSR_FE | RCSR_PE);
	if (I_IGNBRK(tty)) {
		port->mark_mask &= ~RCSR_BREAK;
		if (I_IGNPAR(tty))
			/* Real raw mode. Ignore all */
			port->mark_mask &= ~RCSR_OE;
	}
	/* Enable Hardware Flow Control */
	if (C_CRTSCTS(tty)) {
#ifdef SPECIALIX_BRAIN_DAMAGED_CTS
		port->IER |= IER_DSR | IER_CTS;
		mcor1 |= MCOR1_DSRZD | MCOR1_CTSZD;
		mcor2 |= MCOR2_DSROD | MCOR2_CTSOD;
		spin_lock_irqsave(&bp->lock, flags);
		tty->hw_stopped = !(sx_in(bp, CD186x_MSVR) & (MSVR_CTS|MSVR_DSR));
		spin_unlock_irqrestore(&bp->lock, flags);
#else
		port->COR2 |= COR2_CTSAE;
#endif
	}
	/* Enable Software Flow Control. FIXME: I'm not sure about this */
	/* Some people reported that it works, but I still doubt it */
	if (I_IXON(tty)) {
		port->COR2 |= COR2_TXIBE;
		cor3 |= (COR3_FCT | COR3_SCDE);
		if (I_IXANY(tty))
			port->COR2 |= COR2_IXM;
		spin_lock_irqsave(&bp->lock, flags);
		sx_out(bp, CD186x_SCHR1, START_CHAR(tty));
		sx_out(bp, CD186x_SCHR2, STOP_CHAR(tty));
		sx_out(bp, CD186x_SCHR3, START_CHAR(tty));
		sx_out(bp, CD186x_SCHR4, STOP_CHAR(tty));
		spin_unlock_irqrestore(&bp->lock, flags);
	}
	if (!C_CLOCAL(tty)) {
		/* Enable CD check */
		port->IER |= IER_CD;
		mcor1 |= MCOR1_CDZD;
		mcor2 |= MCOR2_CDOD;
	}

	if (C_CREAD(tty))
		/* Enable receiver */
		port->IER |= IER_RXD;

	/* Set input FIFO size (1-8 bytes) */
	cor3 |= sx_rxfifo;
	/* Setting up CD186x channel registers */
	spin_lock_irqsave(&bp->lock, flags);
	sx_out(bp, CD186x_COR1, cor1);
	sx_out(bp, CD186x_COR2, port->COR2);
	sx_out(bp, CD186x_COR3, cor3);
	spin_unlock_irqrestore(&bp->lock, flags);
	/* Make CD186x know about registers change */
	sx_wait_CCR(bp);
	spin_lock_irqsave(&bp->lock, flags);
	sx_out(bp, CD186x_CCR, CCR_CORCHG1 | CCR_CORCHG2 | CCR_CORCHG3);
	/* Setting up modem option registers */
	dprintk (SX_DEBUG_TERMIOS, "Mcor1 = %02x, mcor2 = %02x.\n", mcor1, mcor2);
	sx_out(bp, CD186x_MCOR1, mcor1);
	sx_out(bp, CD186x_MCOR2, mcor2);
	spin_unlock_irqrestore(&bp->lock, flags);
	/* Enable CD186x transmitter & receiver */
	sx_wait_CCR(bp);
	spin_lock_irqsave(&bp->lock, flags);
	sx_out(bp, CD186x_CCR, CCR_TXEN | CCR_RXEN);
	/* Enable interrupts */
	sx_out(bp, CD186x_IER, port->IER);
	/* And finally set the modem lines... */
	sx_out(bp, CD186x_MSVR, port->MSVR);
	spin_unlock_irqrestore(&bp->lock, flags);

	func_exit();
}


/* Must be called with interrupts enabled */
static int sx_setup_port(struct specialix_board *bp, struct specialix_port *port)
{
	unsigned long flags;

	func_enter();

	if (port->flags & ASYNC_INITIALIZED) {
		func_exit();
		return 0;
	}

	if (!port->xmit_buf) {
		/* We may sleep in get_zeroed_page() */
		unsigned long tmp;

		if (!(tmp = get_zeroed_page(GFP_KERNEL))) {
			func_exit();
			return -ENOMEM;
		}

		if (port->xmit_buf) {
			free_page(tmp);
			func_exit();
			return -ERESTARTSYS;
		}
		port->xmit_buf = (unsigned char *) tmp;
	}

	spin_lock_irqsave(&port->lock, flags);

	if (port->tty)
		clear_bit(TTY_IO_ERROR, &port->tty->flags);

	port->xmit_cnt = port->xmit_head = port->xmit_tail = 0;
	sx_change_speed(bp, port);
	port->flags |= ASYNC_INITIALIZED;

	spin_unlock_irqrestore(&port->lock, flags);


	func_exit();
	return 0;
}


/* Must be called with interrupts disabled */
static void sx_shutdown_port(struct specialix_board *bp, struct specialix_port *port)
{
	struct tty_struct *tty;
	int i;
	unsigned long flags;

	func_enter();

	if (!(port->flags & ASYNC_INITIALIZED)) {
		func_exit();
		return;
	}

	if (sx_debug & SX_DEBUG_FIFO) {
		dprintk(SX_DEBUG_FIFO, "sx%d: port %d: %ld overruns, FIFO hits [ ",
			board_No(bp), port_No(port), port->overrun);
		for (i = 0; i < 10; i++) {
			dprintk(SX_DEBUG_FIFO, "%ld ", port->hits[i]);
		}
		dprintk(SX_DEBUG_FIFO, "].\n");
	}

	if (port->xmit_buf) {
		free_page((unsigned long) port->xmit_buf);
		port->xmit_buf = NULL;
	}

	/* Select port */
	spin_lock_irqsave(&bp->lock, flags);
	sx_out(bp, CD186x_CAR, port_No(port));

	if (!(tty = port->tty) || C_HUPCL(tty)) {
		/* Drop DTR */
		sx_out(bp, CD186x_MSVDTR, 0);
	}
	spin_unlock_irqrestore(&bp->lock, flags);
	/* Reset port */
	sx_wait_CCR(bp);
	spin_lock_irqsave(&bp->lock, flags);
	sx_out(bp, CD186x_CCR, CCR_SOFTRESET);
	/* Disable all interrupts from this port */
	port->IER = 0;
	sx_out(bp, CD186x_IER, port->IER);
	spin_unlock_irqrestore(&bp->lock, flags);
	if (tty)
		set_bit(TTY_IO_ERROR, &tty->flags);
	port->flags &= ~ASYNC_INITIALIZED;

	if (!bp->count)
		sx_shutdown_board(bp);
	func_exit();
}


static int block_til_ready(struct tty_struct *tty, struct file * filp,
                           struct specialix_port *port)
{
	DECLARE_WAITQUEUE(wait,  current);
	struct specialix_board *bp = port_Board(port);
	int    retval;
	int    do_clocal = 0;
	int    CD;
	unsigned long flags;

	func_enter();

	/*
	 * If the device is in the middle of being closed, then block
	 * until it's done, and then try again.
	 */
	if (tty_hung_up_p(filp) || port->flags & ASYNC_CLOSING) {
		interruptible_sleep_on(&port->close_wait);
		if (port->flags & ASYNC_HUP_NOTIFY) {
			func_exit();
			return -EAGAIN;
		} else {
			func_exit();
			return -ERESTARTSYS;
		}
	}

	/*
	 * If non-blocking mode is set, or the port is not enabled,
	 * then make the check up front and then exit.
	 */
	if ((filp->f_flags & O_NONBLOCK) ||
	    (tty->flags & (1 << TTY_IO_ERROR))) {
		port->flags |= ASYNC_NORMAL_ACTIVE;
		func_exit();
		return 0;
	}

	if (C_CLOCAL(tty))
		do_clocal = 1;

	/*
	 * Block waiting for the carrier detect and the line to become
	 * free (i.e., not in use by the callout).  While we are in
	 * this loop, info->count is dropped by one, so that
	 * rs_close() knows when to free things.  We restore it upon
	 * exit, either normal or abnormal.
	 */
	retval = 0;
	add_wait_queue(&port->open_wait, &wait);
	spin_lock_irqsave(&port->lock, flags);
	if (!tty_hung_up_p(filp)) {
		port->count--;
	}
	spin_unlock_irqrestore(&port->lock, flags);
	port->blocked_open++;
	while (1) {
		spin_lock_irqsave(&bp->lock, flags);
		sx_out(bp, CD186x_CAR, port_No(port));
		CD = sx_in(bp, CD186x_MSVR) & MSVR_CD;
		if (SX_CRTSCTS (tty)) {
			/* Activate RTS */
			port->MSVR |= MSVR_DTR;		/* WTF? */
			sx_out (bp, CD186x_MSVR, port->MSVR);
		} else {
			/* Activate DTR */
			port->MSVR |= MSVR_DTR;
			sx_out (bp, CD186x_MSVR, port->MSVR);
		}
		spin_unlock_irqrestore(&bp->lock, flags);
		set_current_state(TASK_INTERRUPTIBLE);
		if (tty_hung_up_p(filp) ||
		    !(port->flags & ASYNC_INITIALIZED)) {
			if (port->flags & ASYNC_HUP_NOTIFY)
				retval = -EAGAIN;
			else
				retval = -ERESTARTSYS;
			break;
		}
		if (!(port->flags & ASYNC_CLOSING) &&
		    (do_clocal || CD))
			break;
		if (signal_pending(current)) {
			retval = -ERESTARTSYS;
			break;
		}
		schedule();
	}

	set_current_state(TASK_RUNNING);
	remove_wait_queue(&port->open_wait, &wait);
	spin_lock_irqsave(&port->lock, flags);
	if (!tty_hung_up_p(filp)) {
		port->count++;
	}
	port->blocked_open--;
	spin_unlock_irqrestore(&port->lock, flags);
	if (retval) {
		func_exit();
		return retval;
	}

	port->flags |= ASYNC_NORMAL_ACTIVE;
	func_exit();
	return 0;
}


static int sx_open(struct tty_struct * tty, struct file * filp)
{
	int board;
	int error;
	struct specialix_port * port;
	struct specialix_board * bp;
	int i;
	unsigned long flags;

	func_enter();

	board = SX_BOARD(tty->index);

	if (board >= SX_NBOARD || !(sx_board[board].flags & SX_BOARD_PRESENT)) {
		func_exit();
		return -ENODEV;
	}

	bp = &sx_board[board];
	port = sx_port + board * SX_NPORT + SX_PORT(tty->index);
	port->overrun = 0;
	for (i = 0; i < 10; i++)
		port->hits[i]=0;

	dprintk (SX_DEBUG_OPEN, "Board = %d, bp = %p, port = %p, portno = %d.\n",
	        board, bp, port, SX_PORT(tty->index));

	if (sx_paranoia_check(port, tty->name, "sx_open")) {
		func_enter();
		return -ENODEV;
	}

	if ((error = sx_setup_board(bp))) {
		func_exit();
		return error;
	}

	spin_lock_irqsave(&bp->lock, flags);
	port->count++;
	bp->count++;
	tty->driver_data = port;
	port->tty = tty;
	spin_unlock_irqrestore(&bp->lock, flags);

	if ((error = sx_setup_port(bp, port))) {
		func_enter();
		return error;
	}

	if ((error = block_til_ready(tty, filp, port))) {
		func_enter();
		return error;
	}

	func_exit();
	return 0;
}


static void sx_close(struct tty_struct * tty, struct file * filp)
{
	struct specialix_port *port = (struct specialix_port *) tty->driver_data;
	struct specialix_board *bp;
	unsigned long flags;
	unsigned long timeout;

	func_enter();
	if (!port || sx_paranoia_check(port, tty->name, "close")) {
		func_exit();
		return;
	}
	spin_lock_irqsave(&port->lock, flags);

	if (tty_hung_up_p(filp)) {
		spin_unlock_irqrestore(&port->lock, flags);
		func_exit();
		return;
	}

	bp = port_Board(port);
	if ((tty->count == 1) && (port->count != 1)) {
		printk(KERN_ERR "sx%d: sx_close: bad port count;"
		       " tty->count is 1, port count is %d\n",
		       board_No(bp), port->count);
		port->count = 1;
	}

	if (port->count > 1) {
		port->count--;
		bp->count--;

		spin_unlock_irqrestore(&port->lock, flags);

		func_exit();
		return;
	}
	port->flags |= ASYNC_CLOSING;
	/*
	 * Now we wait for the transmit buffer to clear; and we notify
	 * the line discipline to only process XON/XOFF characters.
	 */
	tty->closing = 1;
	spin_unlock_irqrestore(&port->lock, flags);
	dprintk (SX_DEBUG_OPEN, "Closing\n");
	if (port->closing_wait != ASYNC_CLOSING_WAIT_NONE) {
		tty_wait_until_sent(tty, port->closing_wait);
	}
	/*
	 * At this point we stop accepting input.  To do this, we
	 * disable the receive line status interrupts, and tell the
	 * interrupt driver to stop checking the data ready bit in the
	 * line status register.
	 */
	dprintk (SX_DEBUG_OPEN, "Closed\n");
	port->IER &= ~IER_RXD;
	if (port->flags & ASYNC_INITIALIZED) {
		port->IER &= ~IER_TXRDY;
		port->IER |= IER_TXEMPTY;
		spin_lock_irqsave(&bp->lock, flags);
		sx_out(bp, CD186x_CAR, port_No(port));
		sx_out(bp, CD186x_IER, port->IER);
		spin_unlock_irqrestore(&bp->lock, flags);
		/*
		 * Before we drop DTR, make sure the UART transmitter
		 * has completely drained; this is especially
		 * important if there is a transmit FIFO!
		 */
		timeout = jiffies+HZ;
		while(port->IER & IER_TXEMPTY) {
			set_current_state (TASK_INTERRUPTIBLE);
			msleep_interruptible(jiffies_to_msecs(port->timeout));
			if (time_after(jiffies, timeout)) {
				printk (KERN_INFO "Timeout waiting for close\n");
				break;
			}
		}

	}

	if (--bp->count < 0) {
		printk(KERN_ERR "sx%d: sx_shutdown_port: bad board count: %d port: %d\n",
		       board_No(bp), bp->count, tty->index);
		bp->count = 0;
	}
	if (--port->count < 0) {
		printk(KERN_ERR "sx%d: sx_close: bad port count for tty%d: %d\n",
		       board_No(bp), port_No(port), port->count);
		port->count = 0;
	}

	sx_shutdown_port(bp, port);
	if (tty->driver->flush_buffer)
		tty->driver->flush_buffer(tty);
	tty_ldisc_flush(tty);
	spin_lock_irqsave(&port->lock, flags);
	tty->closing = 0;
	port->event = 0;
	port->tty = NULL;
	spin_unlock_irqrestore(&port->lock, flags);
	if (port->blocked_open) {
		if (port->close_delay) {
			msleep_interruptible(jiffies_to_msecs(port->close_delay));
		}
		wake_up_interruptible(&port->open_wait);
	}
	port->flags &= ~(ASYNC_NORMAL_ACTIVE|ASYNC_CLOSING);
	wake_up_interruptible(&port->close_wait);

	func_exit();
}


static int sx_write(struct tty_struct * tty,
                    const unsigned char *buf, int count)
{
	struct specialix_port *port = (struct specialix_port *)tty->driver_data;
	struct specialix_board *bp;
	int c, total = 0;
	unsigned long flags;

	func_enter();
	if (sx_paranoia_check(port, tty->name, "sx_write")) {
		func_exit();
		return 0;
	}

	bp = port_Board(port);

	if (!port->xmit_buf || !tmp_buf) {
		func_exit();
		return 0;
	}

	while (1) {
		spin_lock_irqsave(&port->lock, flags);
		c = min_t(int, count, min(SERIAL_XMIT_SIZE - port->xmit_cnt - 1,
				   SERIAL_XMIT_SIZE - port->xmit_head));
		if (c <= 0) {
			spin_unlock_irqrestore(&port->lock, flags);
			break;
		}
		memcpy(port->xmit_buf + port->xmit_head, buf, c);
		port->xmit_head = (port->xmit_head + c) & (SERIAL_XMIT_SIZE-1);
		port->xmit_cnt += c;
		spin_unlock_irqrestore(&port->lock, flags);

		buf += c;
		count -= c;
		total += c;
	}

	spin_lock_irqsave(&bp->lock, flags);
	if (port->xmit_cnt && !tty->stopped && !tty->hw_stopped &&
	    !(port->IER & IER_TXRDY)) {
		port->IER |= IER_TXRDY;
		sx_out(bp, CD186x_CAR, port_No(port));
		sx_out(bp, CD186x_IER, port->IER);
	}
	spin_unlock_irqrestore(&bp->lock, flags);
	func_exit();

	return total;
}


static void sx_put_char(struct tty_struct * tty, unsigned char ch)
{
	struct specialix_port *port = (struct specialix_port *)tty->driver_data;
	unsigned long flags;
	struct specialix_board  * bp;

	func_enter();

	if (sx_paranoia_check(port, tty->name, "sx_put_char")) {
		func_exit();
		return;
	}
	dprintk (SX_DEBUG_TX, "check tty: %p %p\n", tty, port->xmit_buf);
	if (!port->xmit_buf) {
		func_exit();
		return;
	}
	bp = port_Board(port);
	spin_lock_irqsave(&port->lock, flags);

	dprintk (SX_DEBUG_TX, "xmit_cnt: %d xmit_buf: %p\n", port->xmit_cnt, port->xmit_buf);
	if ((port->xmit_cnt >= SERIAL_XMIT_SIZE - 1) || (!port->xmit_buf)) {
		spin_unlock_irqrestore(&port->lock, flags);
		dprintk (SX_DEBUG_TX, "Exit size\n");
		func_exit();
		return;
	}
	dprintk (SX_DEBUG_TX, "Handle xmit: %p %p\n", port, port->xmit_buf);
	port->xmit_buf[port->xmit_head++] = ch;
	port->xmit_head &= SERIAL_XMIT_SIZE - 1;
	port->xmit_cnt++;
	spin_unlock_irqrestore(&port->lock, flags);

	func_exit();
}


static void sx_flush_chars(struct tty_struct * tty)
{
	struct specialix_port *port = (struct specialix_port *)tty->driver_data;
	unsigned long flags;
	struct specialix_board  * bp = port_Board(port);

	func_enter();

	if (sx_paranoia_check(port, tty->name, "sx_flush_chars")) {
		func_exit();
		return;
	}
	if (port->xmit_cnt <= 0 || tty->stopped || tty->hw_stopped ||
	    !port->xmit_buf) {
		func_exit();
		return;
	}
	spin_lock_irqsave(&bp->lock, flags);
	port->IER |= IER_TXRDY;
	sx_out(port_Board(port), CD186x_CAR, port_No(port));
	sx_out(port_Board(port), CD186x_IER, port->IER);
	spin_unlock_irqrestore(&bp->lock, flags);

	func_exit();
}


static int sx_write_room(struct tty_struct * tty)
{
	struct specialix_port *port = (struct specialix_port *)tty->driver_data;
	int	ret;

	func_enter();

	if (sx_paranoia_check(port, tty->name, "sx_write_room")) {
		func_exit();
		return 0;
	}

	ret = SERIAL_XMIT_SIZE - port->xmit_cnt - 1;
	if (ret < 0)
		ret = 0;

	func_exit();
	return ret;
}


static int sx_chars_in_buffer(struct tty_struct *tty)
{
	struct specialix_port *port = (struct specialix_port *)tty->driver_data;

	func_enter();

	if (sx_paranoia_check(port, tty->name, "sx_chars_in_buffer")) {
		func_exit();
		return 0;
	}
	func_exit();
	return port->xmit_cnt;
}


static void sx_flush_buffer(struct tty_struct *tty)
{
	struct specialix_port *port = (struct specialix_port *)tty->driver_data;
	unsigned long flags;
	struct specialix_board  * bp;

	func_enter();

	if (sx_paranoia_check(port, tty->name, "sx_flush_buffer")) {
		func_exit();
		return;
	}

	bp = port_Board(port);
	spin_lock_irqsave(&port->lock, flags);
	port->xmit_cnt = port->xmit_head = port->xmit_tail = 0;
	spin_unlock_irqrestore(&port->lock, flags);
	tty_wakeup(tty);

	func_exit();
}


static int sx_tiocmget(struct tty_struct *tty, struct file *file)
{
	struct specialix_port *port = (struct specialix_port *)tty->driver_data;
	struct specialix_board * bp;
	unsigned char status;
	unsigned int result;
	unsigned long flags;

	func_enter();

	if (sx_paranoia_check(port, tty->name, __FUNCTION__)) {
		func_exit();
		return -ENODEV;
	}

	bp = port_Board(port);
	spin_lock_irqsave (&bp->lock, flags);
	sx_out(bp, CD186x_CAR, port_No(port));
	status = sx_in(bp, CD186x_MSVR);
	spin_unlock_irqrestore(&bp->lock, flags);
	dprintk (SX_DEBUG_INIT, "Got msvr[%d] = %02x, car = %d.\n",
		port_No(port), status, sx_in (bp, CD186x_CAR));
	dprintk (SX_DEBUG_INIT, "sx_port = %p, port = %p\n", sx_port, port);
	if (SX_CRTSCTS(port->tty)) {
		result  = /*   (status & MSVR_RTS) ? */ TIOCM_DTR /* : 0) */
		          |   ((status & MSVR_DTR) ? TIOCM_RTS : 0)
		          |   ((status & MSVR_CD)  ? TIOCM_CAR : 0)
		          |/* ((status & MSVR_DSR) ? */ TIOCM_DSR /* : 0) */
		          |   ((status & MSVR_CTS) ? TIOCM_CTS : 0);
	} else {
		result  = /*   (status & MSVR_RTS) ? */ TIOCM_RTS /* : 0) */
		          |   ((status & MSVR_DTR) ? TIOCM_DTR : 0)
		          |   ((status & MSVR_CD)  ? TIOCM_CAR : 0)
		          |/* ((status & MSVR_DSR) ? */ TIOCM_DSR /* : 0) */
		          |   ((status & MSVR_CTS) ? TIOCM_CTS : 0);
	}

	func_exit();

	return result;
}


static int sx_tiocmset(struct tty_struct *tty, struct file *file,
		       unsigned int set, unsigned int clear)
{
	struct specialix_port *port = (struct specialix_port *)tty->driver_data;
	unsigned long flags;
	struct specialix_board *bp;

	func_enter();

	if (sx_paranoia_check(port, tty->name, __FUNCTION__)) {
		func_exit();
		return -ENODEV;
	}

	bp = port_Board(port);

	spin_lock_irqsave(&port->lock, flags);
   /*	if (set & TIOCM_RTS)
		port->MSVR |= MSVR_RTS; */
   /*   if (set & TIOCM_DTR)
		port->MSVR |= MSVR_DTR; */

	if (SX_CRTSCTS(port->tty)) {
		if (set & TIOCM_RTS)
			port->MSVR |= MSVR_DTR;
	} else {
		if (set & TIOCM_DTR)
			port->MSVR |= MSVR_DTR;
	}

  /*	if (clear & TIOCM_RTS)
		port->MSVR &= ~MSVR_RTS; */
  /*    if (clear & TIOCM_DTR)
		port->MSVR &= ~MSVR_DTR; */
	if (SX_CRTSCTS(port->tty)) {
		if (clear & TIOCM_RTS)
			port->MSVR &= ~MSVR_DTR;
	} else {
		if (clear & TIOCM_DTR)
			port->MSVR &= ~MSVR_DTR;
	}
	spin_lock_irqsave(&bp->lock, flags);
	sx_out(bp, CD186x_CAR, port_No(port));
	sx_out(bp, CD186x_MSVR, port->MSVR);
	spin_unlock_irqrestore(&bp->lock, flags);
	spin_unlock_irqrestore(&port->lock, flags);
	func_exit();
	return 0;
}


static inline void sx_send_break(struct specialix_port * port, unsigned long length)
{
	struct specialix_board *bp = port_Board(port);
	unsigned long flags;

	func_enter();

	spin_lock_irqsave (&port->lock, flags);
	port->break_length = SPECIALIX_TPS / HZ * length;
	port->COR2 |= COR2_ETC;
	port->IER  |= IER_TXRDY;
	spin_lock_irqsave(&bp->lock, flags);
	sx_out(bp, CD186x_CAR, port_No(port));
	sx_out(bp, CD186x_COR2, port->COR2);
	sx_out(bp, CD186x_IER, port->IER);
	spin_unlock_irqrestore(&bp->lock, flags);
	spin_unlock_irqrestore (&port->lock, flags);
	sx_wait_CCR(bp);
	spin_lock_irqsave(&bp->lock, flags);
	sx_out(bp, CD186x_CCR, CCR_CORCHG2);
	spin_unlock_irqrestore(&bp->lock, flags);
	sx_wait_CCR(bp);

	func_exit();
}


static inline int sx_set_serial_info(struct specialix_port * port,
                                     struct serial_struct __user * newinfo)
{
	struct serial_struct tmp;
	struct specialix_board *bp = port_Board(port);
	int change_speed;

	func_enter();
	/*
	if (!access_ok(VERIFY_READ, (void *) newinfo, sizeof(tmp))) {
		func_exit();
		return -EFAULT;
	}
	*/
	if (copy_from_user(&tmp, newinfo, sizeof(tmp))) {
		func_enter();
		return -EFAULT;
	}

#if 0
	if ((tmp.irq != bp->irq) ||
	    (tmp.port != bp->base) ||
	    (tmp.type != PORT_CIRRUS) ||
	    (tmp.baud_base != (SX_OSCFREQ + CD186x_TPC/2) / CD186x_TPC) ||
	    (tmp.custom_divisor != 0) ||
	    (tmp.xmit_fifo_size != CD186x_NFIFO) ||
	    (tmp.flags & ~SPECIALIX_LEGAL_FLAGS)) {
		func_exit();
		return -EINVAL;
	}
#endif

	change_speed = ((port->flags & ASYNC_SPD_MASK) !=
			(tmp.flags & ASYNC_SPD_MASK));
	change_speed |= (tmp.custom_divisor != port->custom_divisor);

	if (!capable(CAP_SYS_ADMIN)) {
		if ((tmp.close_delay != port->close_delay) ||
		    (tmp.closing_wait != port->closing_wait) ||
		    ((tmp.flags & ~ASYNC_USR_MASK) !=
		     (port->flags & ~ASYNC_USR_MASK))) {
			func_exit();
			return -EPERM;
		}
		port->flags = ((port->flags & ~ASYNC_USR_MASK) |
		                  (tmp.flags & ASYNC_USR_MASK));
		port->custom_divisor = tmp.custom_divisor;
	} else {
		port->flags = ((port->flags & ~ASYNC_FLAGS) |
		                  (tmp.flags & ASYNC_FLAGS));
		port->close_delay = tmp.close_delay;
		port->closing_wait = tmp.closing_wait;
		port->custom_divisor = tmp.custom_divisor;
	}
	if (change_speed) {
		sx_change_speed(bp, port);
	}
	func_exit();
	return 0;
}


static inline int sx_get_serial_info(struct specialix_port * port,
				     struct serial_struct __user *retinfo)
{
	struct serial_struct tmp;
	struct specialix_board *bp = port_Board(port);

	func_enter();

	/*
	if (!access_ok(VERIFY_WRITE, (void *) retinfo, sizeof(tmp)))
		return -EFAULT;
	*/

	memset(&tmp, 0, sizeof(tmp));
	tmp.type = PORT_CIRRUS;
	tmp.line = port - sx_port;
	tmp.port = bp->base;
	tmp.irq  = bp->irq;
	tmp.flags = port->flags;
	tmp.baud_base = (SX_OSCFREQ + CD186x_TPC/2) / CD186x_TPC;
	tmp.close_delay = port->close_delay * HZ/100;
	tmp.closing_wait = port->closing_wait * HZ/100;
	tmp.custom_divisor =  port->custom_divisor;
	tmp.xmit_fifo_size = CD186x_NFIFO;
	if (copy_to_user(retinfo, &tmp, sizeof(tmp))) {
		func_exit();
		return -EFAULT;
	}

	func_exit();
	return 0;
}


static int sx_ioctl(struct tty_struct * tty, struct file * filp,
                    unsigned int cmd, unsigned long arg)
{
	struct specialix_port *port = (struct specialix_port *)tty->driver_data;
	int retval;
	void __user *argp = (void __user *)arg;

	func_enter();

	if (sx_paranoia_check(port, tty->name, "sx_ioctl")) {
		func_exit();
		return -ENODEV;
	}

	switch (cmd) {
	 case TCSBRK:	/* SVID version: non-zero arg --> no break */
		retval = tty_check_change(tty);
		if (retval) {
			func_exit();
			return retval;
		}
		tty_wait_until_sent(tty, 0);
		if (!arg)
			sx_send_break(port, HZ/4);	/* 1/4 second */
		return 0;
	 case TCSBRKP:	/* support for POSIX tcsendbreak() */
		retval = tty_check_change(tty);
		if (retval) {
			func_exit();
			return retval;
		}
		tty_wait_until_sent(tty, 0);
		sx_send_break(port, arg ? arg*(HZ/10) : HZ/4);
		func_exit();
		return 0;
	 case TIOCGSOFTCAR:
		 if (put_user(C_CLOCAL(tty)?1:0, (unsigned long __user *)argp)) {
			 func_exit();
			 return -EFAULT;
		 }
		 func_exit();
		return 0;
	 case TIOCSSOFTCAR:
		 if (get_user(arg, (unsigned long __user *) argp)) {
			 func_exit();
			 return -EFAULT;
		 }
		tty->termios->c_cflag =
			((tty->termios->c_cflag & ~CLOCAL) |
			(arg ? CLOCAL : 0));
		func_exit();
		return 0;
	 case TIOCGSERIAL:
		 func_exit();
		return sx_get_serial_info(port, argp);
	 case TIOCSSERIAL:
		 func_exit();
		return sx_set_serial_info(port, argp);
	 default:
		 func_exit();
		return -ENOIOCTLCMD;
	}
	func_exit();
	return 0;
}


static void sx_throttle(struct tty_struct * tty)
{
	struct specialix_port *port = (struct specialix_port *)tty->driver_data;
	struct specialix_board *bp;
	unsigned long flags;

	func_enter();

	if (sx_paranoia_check(port, tty->name, "sx_throttle")) {
		func_exit();
		return;
	}

	bp = port_Board(port);

	/* Use DTR instead of RTS ! */
	if (SX_CRTSCTS (tty))
		port->MSVR &= ~MSVR_DTR;
	else {
		/* Auch!!! I think the system shouldn't call this then. */
		/* Or maybe we're supposed (allowed?) to do our side of hw
		   handshake anyway, even when hardware handshake is off.
		   When you see this in your logs, please report.... */
		printk (KERN_ERR "sx%d: Need to throttle, but can't (hardware hs is off)\n",
	                 port_No (port));
	}
	spin_lock_irqsave(&bp->lock, flags);
	sx_out(bp, CD186x_CAR, port_No(port));
	spin_unlock_irqrestore(&bp->lock, flags);
	if (I_IXOFF(tty)) {
		spin_unlock_irqrestore(&bp->lock, flags);
		sx_wait_CCR(bp);
		spin_lock_irqsave(&bp->lock, flags);
		sx_out(bp, CD186x_CCR, CCR_SSCH2);
		spin_unlock_irqrestore(&bp->lock, flags);
		sx_wait_CCR(bp);
	}
	spin_lock_irqsave(&bp->lock, flags);
	sx_out(bp, CD186x_MSVR, port->MSVR);
	spin_unlock_irqrestore(&bp->lock, flags);

	func_exit();
}


static void sx_unthrottle(struct tty_struct * tty)
{
	struct specialix_port *port = (struct specialix_port *)tty->driver_data;
	struct specialix_board *bp;
	unsigned long flags;

	func_enter();

	if (sx_paranoia_check(port, tty->name, "sx_unthrottle")) {
		func_exit();
		return;
	}

	bp = port_Board(port);

	spin_lock_irqsave(&port->lock, flags);
	/* XXXX Use DTR INSTEAD???? */
	if (SX_CRTSCTS(tty)) {
		port->MSVR |= MSVR_DTR;
	} /* Else clause: see remark in "sx_throttle"... */
	spin_lock_irqsave(&bp->lock, flags);
	sx_out(bp, CD186x_CAR, port_No(port));
	spin_unlock_irqrestore(&bp->lock, flags);
	if (I_IXOFF(tty)) {
		spin_unlock_irqrestore(&port->lock, flags);
		sx_wait_CCR(bp);
		spin_lock_irqsave(&bp->lock, flags);
		sx_out(bp, CD186x_CCR, CCR_SSCH1);
		spin_unlock_irqrestore(&bp->lock, flags);
		sx_wait_CCR(bp);
		spin_lock_irqsave(&port->lock, flags);
	}
	spin_lock_irqsave(&bp->lock, flags);
	sx_out(bp, CD186x_MSVR, port->MSVR);
	spin_unlock_irqrestore(&bp->lock, flags);
	spin_unlock_irqrestore(&port->lock, flags);

	func_exit();
}


static void sx_stop(struct tty_struct * tty)
{
	struct specialix_port *port = (struct specialix_port *)tty->driver_data;
	struct specialix_board *bp;
	unsigned long flags;

	func_enter();

	if (sx_paranoia_check(port, tty->name, "sx_stop")) {
		func_exit();
		return;
	}

	bp = port_Board(port);

	spin_lock_irqsave(&port->lock, flags);
	port->IER &= ~IER_TXRDY;
	spin_lock_irqsave(&bp->lock, flags);
	sx_out(bp, CD186x_CAR, port_No(port));
	sx_out(bp, CD186x_IER, port->IER);
	spin_unlock_irqrestore(&bp->lock, flags);
	spin_unlock_irqrestore(&port->lock, flags);

	func_exit();
}


static void sx_start(struct tty_struct * tty)
{
	struct specialix_port *port = (struct specialix_port *)tty->driver_data;
	struct specialix_board *bp;
	unsigned long flags;

	func_enter();

	if (sx_paranoia_check(port, tty->name, "sx_start")) {
		func_exit();
		return;
	}

	bp = port_Board(port);

	spin_lock_irqsave(&port->lock, flags);
	if (port->xmit_cnt && port->xmit_buf && !(port->IER & IER_TXRDY)) {
		port->IER |= IER_TXRDY;
		spin_lock_irqsave(&bp->lock, flags);
		sx_out(bp, CD186x_CAR, port_No(port));
		sx_out(bp, CD186x_IER, port->IER);
		spin_unlock_irqrestore(&bp->lock, flags);
	}
	spin_unlock_irqrestore(&port->lock, flags);

	func_exit();
}


/*
 * This routine is called from the work-queue when the interrupt
 * routine has signalled that a hangup has occurred.  The path of
 * hangup processing is:
 *
 * 	serial interrupt routine -> (workqueue) ->
 * 	do_sx_hangup() -> tty->hangup() -> sx_hangup()
 *
 */
static void do_sx_hangup(void *private_)
{
	struct specialix_port	*port = (struct specialix_port *) private_;
	struct tty_struct	*tty;

	func_enter();

	tty = port->tty;
	if (tty)
		tty_hangup(tty);	/* FIXME: module removal race here */

	func_exit();
}


static void sx_hangup(struct tty_struct * tty)
{
	struct specialix_port *port = (struct specialix_port *)tty->driver_data;
	struct specialix_board *bp;
	unsigned long flags;

	func_enter();

	if (sx_paranoia_check(port, tty->name, "sx_hangup")) {
		func_exit();
		return;
	}

	bp = port_Board(port);

	sx_shutdown_port(bp, port);
	spin_lock_irqsave(&port->lock, flags);
	port->event = 0;
	bp->count -= port->count;
	if (bp->count < 0) {
		printk(KERN_ERR "sx%d: sx_hangup: bad board count: %d port: %d\n",
			board_No(bp), bp->count, tty->index);
		bp->count = 0;
	}
	port->count = 0;
	port->flags &= ~ASYNC_NORMAL_ACTIVE;
	port->tty = NULL;
	spin_unlock_irqrestore(&port->lock, flags);
	wake_up_interruptible(&port->open_wait);

	func_exit();
}


static void sx_set_termios(struct tty_struct * tty, struct termios * old_termios)
{
	struct specialix_port *port = (struct specialix_port *)tty->driver_data;
	unsigned long flags;
	struct specialix_board  * bp;

	if (sx_paranoia_check(port, tty->name, "sx_set_termios"))
		return;

	if (tty->termios->c_cflag == old_termios->c_cflag &&
	    tty->termios->c_iflag == old_termios->c_iflag)
		return;

	bp = port_Board(port);
	spin_lock_irqsave(&port->lock, flags);
	sx_change_speed(port_Board(port), port);
	spin_unlock_irqrestore(&port->lock, flags);

	if ((old_termios->c_cflag & CRTSCTS) &&
	    !(tty->termios->c_cflag & CRTSCTS)) {
		tty->hw_stopped = 0;
		sx_start(tty);
	}
}


static void do_softint(void *private_)
{
	struct specialix_port	*port = (struct specialix_port *) private_;
	struct tty_struct	*tty;

	func_enter();

	if(!(tty = port->tty)) {
		func_exit();
		return;
	}

	if (test_and_clear_bit(RS_EVENT_WRITE_WAKEUP, &port->event)) {
 		tty_wakeup(tty);
		//wake_up_interruptible(&tty->write_wait);
	}

	func_exit();
}

static struct tty_operations sx_ops = {
	.open  = sx_open,
	.close = sx_close,
	.write = sx_write,
	.put_char = sx_put_char,
	.flush_chars = sx_flush_chars,
	.write_room = sx_write_room,
	.chars_in_buffer = sx_chars_in_buffer,
	.flush_buffer = sx_flush_buffer,
	.ioctl = sx_ioctl,
	.throttle = sx_throttle,
	.unthrottle = sx_unthrottle,
	.set_termios = sx_set_termios,
	.stop = sx_stop,
	.start = sx_start,
	.hangup = sx_hangup,
	.tiocmget = sx_tiocmget,
	.tiocmset = sx_tiocmset,
};

static int sx_init_drivers(void)
{
	int error;
	int i;

	func_enter();

	specialix_driver = alloc_tty_driver(SX_NBOARD * SX_NPORT);
	if (!specialix_driver) {
		printk(KERN_ERR "sx: Couldn't allocate tty_driver.\n");
		func_exit();
		return 1;
	}

	if (!(tmp_buf = (unsigned char *) get_zeroed_page(GFP_KERNEL))) {
		printk(KERN_ERR "sx: Couldn't get free page.\n");
		put_tty_driver(specialix_driver);
		func_exit();
		return 1;
	}
	specialix_driver->owner = THIS_MODULE;
	specialix_driver->name = "ttyW";
	specialix_driver->major = SPECIALIX_NORMAL_MAJOR;
	specialix_driver->type = TTY_DRIVER_TYPE_SERIAL;
	specialix_driver->subtype = SERIAL_TYPE_NORMAL;
	specialix_driver->init_termios = tty_std_termios;
	specialix_driver->init_termios.c_cflag =
		B9600 | CS8 | CREAD | HUPCL | CLOCAL;
	specialix_driver->flags = TTY_DRIVER_REAL_RAW;
	tty_set_operations(specialix_driver, &sx_ops);

	if ((error = tty_register_driver(specialix_driver))) {
		put_tty_driver(specialix_driver);
		free_page((unsigned long)tmp_buf);
		printk(KERN_ERR "sx: Couldn't register specialix IO8+ driver, error = %d\n",
		       error);
		func_exit();
		return 1;
	}
	memset(sx_port, 0, sizeof(sx_port));
	for (i = 0; i < SX_NPORT * SX_NBOARD; i++) {
		sx_port[i].magic = SPECIALIX_MAGIC;
		INIT_WORK(&sx_port[i].tqueue, do_softint, &sx_port[i]);
		INIT_WORK(&sx_port[i].tqueue_hangup, do_sx_hangup, &sx_port[i]);
		sx_port[i].close_delay = 50 * HZ/100;
		sx_port[i].closing_wait = 3000 * HZ/100;
		init_waitqueue_head(&sx_port[i].open_wait);
		init_waitqueue_head(&sx_port[i].close_wait);
		spin_lock_init(&sx_port[i].lock);
	}

	func_exit();
	return 0;
}

static void sx_release_drivers(void)
{
	func_enter();

	free_page((unsigned long)tmp_buf);
	tty_unregister_driver(specialix_driver);
	put_tty_driver(specialix_driver);
	func_exit();
}

/*
 * This routine must be called by kernel at boot time
 */
static int __init specialix_init(void)
{
	int i;
	int found = 0;

	func_enter();

	printk(KERN_INFO "sx: Specialix IO8+ driver v" VERSION ", (c) R.E.Wolff 1997/1998.\n");
	printk(KERN_INFO "sx: derived from work (c) D.Gorodchanin 1994-1996.\n");
#ifdef CONFIG_SPECIALIX_RTSCTS
	printk (KERN_INFO "sx: DTR/RTS pin is always RTS.\n");
#else
	printk (KERN_INFO "sx: DTR/RTS pin is RTS when CRTSCTS is on.\n");
#endif

	for (i = 0; i < SX_NBOARD; i++)
		sx_board[i].lock = SPIN_LOCK_UNLOCKED;

	if (sx_init_drivers()) {
		func_exit();
		return -EIO;
	}

	for (i = 0; i < SX_NBOARD; i++)
		if (sx_board[i].base && !sx_probe(&sx_board[i]))
			found++;

#ifdef CONFIG_PCI
	{
		struct pci_dev *pdev = NULL;

		i=0;
		while (i < SX_NBOARD) {
			if (sx_board[i].flags & SX_BOARD_PRESENT) {
				i++;
				continue;
			}
			pdev = pci_find_device (PCI_VENDOR_ID_SPECIALIX,
			                        PCI_DEVICE_ID_SPECIALIX_IO8,
			                        pdev);
			if (!pdev) break;

			if (pci_enable_device(pdev))
				continue;

			sx_board[i].irq = pdev->irq;

			sx_board[i].base = pci_resource_start (pdev, 2);

			sx_board[i].flags |= SX_BOARD_IS_PCI;
			if (!sx_probe(&sx_board[i]))
				found ++;
		}
	}
#endif

	if (!found) {
		sx_release_drivers();
		printk(KERN_INFO "sx: No specialix IO8+ boards detected.\n");
		func_exit();
		return -EIO;
	}

	func_exit();
	return 0;
}

static int iobase[SX_NBOARD]  = {0,};

static int irq [SX_NBOARD] = {0,};

module_param_array(iobase, int, NULL, 0);
module_param_array(irq, int, NULL, 0);
module_param(sx_debug, int, 0);
module_param(sx_rxfifo, int, 0);
#ifdef SPECIALIX_TIMER
module_param(sx_poll, int, 0);
#endif

/*
 * You can setup up to 4 boards.
 * by specifying "iobase=0xXXX,0xXXX ..." as insmod parameter.
 * You should specify the IRQs too in that case "irq=....,...".
 *
 * More than 4 boards in one computer is not possible, as the card can
 * only use 4 different interrupts.
 *
 */
static int __init specialix_init_module(void)
{
	int i;

	func_enter();

	if (iobase[0] || iobase[1] || iobase[2] || iobase[3]) {
		for(i = 0; i < SX_NBOARD; i++) {
			sx_board[i].base = iobase[i];
			sx_board[i].irq = irq[i];
			sx_board[i].count= 0;
		}
	}

	func_exit();

	return specialix_init();
}

static void __exit specialix_exit_module(void)
{
	int i;

	func_enter();

	sx_release_drivers();
	for (i = 0; i < SX_NBOARD; i++)
		if (sx_board[i].flags & SX_BOARD_PRESENT)
			sx_release_io_range(&sx_board[i]);
#ifdef SPECIALIX_TIMER
	del_timer (&missed_irq_timer);
#endif

	func_exit();
}

module_init(specialix_init_module);
module_exit(specialix_exit_module);

MODULE_LICENSE("GPL");
