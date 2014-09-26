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
 */

/************************************************************************
 *
 * This file implements the tty driver functionality for the
 * Neo and ClassicBoard PCI based product lines.
 *
 ************************************************************************
 *
 */

#include <linux/kernel.h>
#include <linux/sched.h>	/* For jiffies, task states */
#include <linux/interrupt.h>	/* For tasklet and interrupt structs/defines */
#include <linux/module.h>
#include <linux/ctype.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/serial_reg.h>
#include <linux/slab.h>
#include <linux/delay.h>	/* For udelay */
#include <linux/uaccess.h>	/* For copy_from_user/copy_to_user */
#include <linux/pci.h>

#include "dgnc_driver.h"
#include "dgnc_tty.h"
#include "dgnc_types.h"
#include "dgnc_neo.h"
#include "dgnc_cls.h"
#include "dpacompat.h"
#include "dgnc_sysfs.h"
#include "dgnc_utils.h"

#define init_MUTEX(sem)	 sema_init(sem, 1)
#define DECLARE_MUTEX(name)     \
	struct semaphore name = __SEMAPHORE_INITIALIZER(name, 1)

/*
 * internal variables
 */
static struct dgnc_board	*dgnc_BoardsByMajor[256];
static uchar		*dgnc_TmpWriteBuf;
static DECLARE_MUTEX(dgnc_TmpWriteSem);

/*
 * Default transparent print information.
 */
static struct digi_t dgnc_digi_init = {
	.digi_flags =	DIGI_COOK,	/* Flags			*/
	.digi_maxcps =	100,		/* Max CPS			*/
	.digi_maxchar =	50,		/* Max chars in print queue	*/
	.digi_bufsize =	100,		/* Printer buffer size		*/
	.digi_onlen =	4,		/* size of printer on string	*/
	.digi_offlen =	4,		/* size of printer off string	*/
	.digi_onstr =	"\033[5i",	/* ANSI printer on string ]	*/
	.digi_offstr =	"\033[4i",	/* ANSI printer off string ]	*/
	.digi_term =	"ansi"		/* default terminal type	*/
};


/*
 * Define a local default termios struct. All ports will be created
 * with this termios initially.
 *
 * This defines a raw port at 9600 baud, 8 data bits, no parity,
 * 1 stop bit.
 */
static struct ktermios DgncDefaultTermios = {
	.c_iflag =	(DEFAULT_IFLAGS),	/* iflags */
	.c_oflag =	(DEFAULT_OFLAGS),	/* oflags */
	.c_cflag =	(DEFAULT_CFLAGS),	/* cflags */
	.c_lflag =	(DEFAULT_LFLAGS),	/* lflags */
	.c_cc =		INIT_C_CC,
	.c_line =	0,
};


/* Our function prototypes */
static int dgnc_tty_open(struct tty_struct *tty, struct file *file);
static void dgnc_tty_close(struct tty_struct *tty, struct file *file);
static int dgnc_block_til_ready(struct tty_struct *tty, struct file *file, struct channel_t *ch);
static int dgnc_tty_ioctl(struct tty_struct *tty, unsigned int cmd, unsigned long arg);
static int dgnc_tty_digigeta(struct tty_struct *tty, struct digi_t __user *retinfo);
static int dgnc_tty_digiseta(struct tty_struct *tty, struct digi_t __user *new_info);
static int dgnc_tty_write_room(struct tty_struct *tty);
static int dgnc_tty_put_char(struct tty_struct *tty, unsigned char c);
static int dgnc_tty_chars_in_buffer(struct tty_struct *tty);
static void dgnc_tty_start(struct tty_struct *tty);
static void dgnc_tty_stop(struct tty_struct *tty);
static void dgnc_tty_throttle(struct tty_struct *tty);
static void dgnc_tty_unthrottle(struct tty_struct *tty);
static void dgnc_tty_flush_chars(struct tty_struct *tty);
static void dgnc_tty_flush_buffer(struct tty_struct *tty);
static void dgnc_tty_hangup(struct tty_struct *tty);
static int dgnc_set_modem_info(struct tty_struct *tty, unsigned int command, unsigned int __user *value);
static int dgnc_get_modem_info(struct channel_t *ch, unsigned int __user *value);
static int dgnc_tty_tiocmget(struct tty_struct *tty);
static int dgnc_tty_tiocmset(struct tty_struct *tty, unsigned int set, unsigned int clear);
static int dgnc_tty_send_break(struct tty_struct *tty, int msec);
static void dgnc_tty_wait_until_sent(struct tty_struct *tty, int timeout);
static int dgnc_tty_write(struct tty_struct *tty, const unsigned char *buf, int count);
static void dgnc_tty_set_termios(struct tty_struct *tty, struct ktermios *old_termios);
static void dgnc_tty_send_xchar(struct tty_struct *tty, char ch);


static const struct tty_operations dgnc_tty_ops = {
	.open = dgnc_tty_open,
	.close = dgnc_tty_close,
	.write = dgnc_tty_write,
	.write_room = dgnc_tty_write_room,
	.flush_buffer = dgnc_tty_flush_buffer,
	.chars_in_buffer = dgnc_tty_chars_in_buffer,
	.flush_chars = dgnc_tty_flush_chars,
	.ioctl = dgnc_tty_ioctl,
	.set_termios = dgnc_tty_set_termios,
	.stop = dgnc_tty_stop,
	.start = dgnc_tty_start,
	.throttle = dgnc_tty_throttle,
	.unthrottle = dgnc_tty_unthrottle,
	.hangup = dgnc_tty_hangup,
	.put_char = dgnc_tty_put_char,
	.tiocmget = dgnc_tty_tiocmget,
	.tiocmset = dgnc_tty_tiocmset,
	.break_ctl = dgnc_tty_send_break,
	.wait_until_sent = dgnc_tty_wait_until_sent,
	.send_xchar = dgnc_tty_send_xchar
};

/************************************************************************
 *
 * TTY Initialization/Cleanup Functions
 *
 ************************************************************************/

/*
 * dgnc_tty_preinit()
 *
 * Initialize any global tty related data before we download any boards.
 */
int dgnc_tty_preinit(void)
{
	/*
	 * Allocate a buffer for doing the copy from user space to
	 * kernel space in dgnc_write().  We only use one buffer and
	 * control access to it with a semaphore.  If we are paging, we
	 * are already in trouble so one buffer won't hurt much anyway.
	 *
	 * We are okay to sleep in the malloc, as this routine
	 * is only called during module load, (not in interrupt context),
	 * and with no locks held.
	 */
	dgnc_TmpWriteBuf = kmalloc(WRITEBUFLEN, GFP_KERNEL);

	if (!dgnc_TmpWriteBuf)
		return -ENOMEM;

	return 0;
}


/*
 * dgnc_tty_register()
 *
 * Init the tty subsystem for this board.
 */
int dgnc_tty_register(struct dgnc_board *brd)
{
	int rc = 0;

	brd->SerialDriver.magic = TTY_DRIVER_MAGIC;

	snprintf(brd->SerialName, MAXTTYNAMELEN, "tty_dgnc_%d_", brd->boardnum);

	brd->SerialDriver.name = brd->SerialName;
	brd->SerialDriver.name_base = 0;
	brd->SerialDriver.major = 0;
	brd->SerialDriver.minor_start = 0;
	brd->SerialDriver.num = brd->maxports;
	brd->SerialDriver.type = TTY_DRIVER_TYPE_SERIAL;
	brd->SerialDriver.subtype = SERIAL_TYPE_NORMAL;
	brd->SerialDriver.init_termios = DgncDefaultTermios;
	brd->SerialDriver.driver_name = DRVSTR;
	brd->SerialDriver.flags = (TTY_DRIVER_REAL_RAW | TTY_DRIVER_DYNAMIC_DEV | TTY_DRIVER_HARDWARE_BREAK);

	/*
	 * The kernel wants space to store pointers to
	 * tty_struct's and termios's.
	 */
	brd->SerialDriver.ttys = kcalloc(brd->maxports, sizeof(*brd->SerialDriver.ttys), GFP_KERNEL);
	if (!brd->SerialDriver.ttys)
		return -ENOMEM;

	kref_init(&brd->SerialDriver.kref);
	brd->SerialDriver.termios = kcalloc(brd->maxports, sizeof(*brd->SerialDriver.termios), GFP_KERNEL);
	if (!brd->SerialDriver.termios)
		return -ENOMEM;

	/*
	 * Entry points for driver.  Called by the kernel from
	 * tty_io.c and n_tty.c.
	 */
	tty_set_operations(&brd->SerialDriver, &dgnc_tty_ops);

	if (!brd->dgnc_Major_Serial_Registered) {
		/* Register tty devices */
		rc = tty_register_driver(&brd->SerialDriver);
		if (rc < 0) {
			APR(("Can't register tty device (%d)\n", rc));
			return rc;
		}
		brd->dgnc_Major_Serial_Registered = TRUE;
	}

	/*
	 * If we're doing transparent print, we have to do all of the above
	 * again, separately so we don't get the LD confused about what major
	 * we are when we get into the dgnc_tty_open() routine.
	 */
	brd->PrintDriver.magic = TTY_DRIVER_MAGIC;
	snprintf(brd->PrintName, MAXTTYNAMELEN, "pr_dgnc_%d_", brd->boardnum);

	brd->PrintDriver.name = brd->PrintName;
	brd->PrintDriver.name_base = 0;
	brd->PrintDriver.major = brd->SerialDriver.major;
	brd->PrintDriver.minor_start = 0x80;
	brd->PrintDriver.num = brd->maxports;
	brd->PrintDriver.type = TTY_DRIVER_TYPE_SERIAL;
	brd->PrintDriver.subtype = SERIAL_TYPE_NORMAL;
	brd->PrintDriver.init_termios = DgncDefaultTermios;
	brd->PrintDriver.driver_name = DRVSTR;
	brd->PrintDriver.flags = (TTY_DRIVER_REAL_RAW | TTY_DRIVER_DYNAMIC_DEV | TTY_DRIVER_HARDWARE_BREAK);

	/*
	 * The kernel wants space to store pointers to
	 * tty_struct's and termios's.  Must be separated from
	 * the Serial Driver so we don't get confused
	 */
	brd->PrintDriver.ttys = kcalloc(brd->maxports, sizeof(*brd->PrintDriver.ttys), GFP_KERNEL);
	if (!brd->PrintDriver.ttys)
		return -ENOMEM;
	kref_init(&brd->PrintDriver.kref);
	brd->PrintDriver.termios = kcalloc(brd->maxports, sizeof(*brd->PrintDriver.termios), GFP_KERNEL);
	if (!brd->PrintDriver.termios)
		return -ENOMEM;

	/*
	 * Entry points for driver.  Called by the kernel from
	 * tty_io.c and n_tty.c.
	 */
	tty_set_operations(&brd->PrintDriver, &dgnc_tty_ops);

	if (!brd->dgnc_Major_TransparentPrint_Registered) {
		/* Register Transparent Print devices */
		rc = tty_register_driver(&brd->PrintDriver);
		if (rc < 0) {
			APR(("Can't register Transparent Print device (%d)\n", rc));
			return rc;
		}
		brd->dgnc_Major_TransparentPrint_Registered = TRUE;
	}

	dgnc_BoardsByMajor[brd->SerialDriver.major] = brd;
	brd->dgnc_Serial_Major = brd->SerialDriver.major;
	brd->dgnc_TransparentPrint_Major = brd->PrintDriver.major;

	return rc;
}


/*
 * dgnc_tty_init()
 *
 * Init the tty subsystem.  Called once per board after board has been
 * downloaded and init'ed.
 */
int dgnc_tty_init(struct dgnc_board *brd)
{
	int i;
	void __iomem *vaddr;
	struct channel_t *ch;

	if (!brd)
		return -ENXIO;

	/*
	 * Initialize board structure elements.
	 */

	vaddr = brd->re_map_membase;

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
			brd->channels[i] = kzalloc(sizeof(*brd->channels[i]), GFP_KERNEL);
		}
	}

	ch = brd->channels[0];
	vaddr = brd->re_map_membase;

	/* Set up channel variables */
	for (i = 0; i < brd->nasync; i++, ch = brd->channels[i]) {

		if (!brd->channels[i])
			continue;

		spin_lock_init(&ch->ch_lock);

		/* Store all our magic numbers */
		ch->magic = DGNC_CHANNEL_MAGIC;
		ch->ch_tun.magic = DGNC_UNIT_MAGIC;
		ch->ch_tun.un_ch = ch;
		ch->ch_tun.un_type = DGNC_SERIAL;
		ch->ch_tun.un_dev = i;

		ch->ch_pun.magic = DGNC_UNIT_MAGIC;
		ch->ch_pun.un_ch = ch;
		ch->ch_pun.un_type = DGNC_PRINT;
		ch->ch_pun.un_dev = i + 128;

		if (brd->bd_uart_offset == 0x200)
			ch->ch_neo_uart = vaddr + (brd->bd_uart_offset * i);
		else
			ch->ch_cls_uart = vaddr + (brd->bd_uart_offset * i);

		ch->ch_bd = brd;
		ch->ch_portnum = i;
		ch->ch_digi = dgnc_digi_init;

		/* .25 second delay */
		ch->ch_close_delay = 250;

		init_waitqueue_head(&ch->ch_flags_wait);
		init_waitqueue_head(&ch->ch_tun.un_flags_wait);
		init_waitqueue_head(&ch->ch_pun.un_flags_wait);
		init_waitqueue_head(&ch->ch_sniff_wait);

		{
			struct device *classp;
			classp = tty_register_device(&brd->SerialDriver, i,
				&(ch->ch_bd->pdev->dev));
			ch->ch_tun.un_sysfs = classp;
			dgnc_create_tty_sysfs(&ch->ch_tun, classp);

			classp = tty_register_device(&brd->PrintDriver, i,
				&(ch->ch_bd->pdev->dev));
			ch->ch_pun.un_sysfs = classp;
			dgnc_create_tty_sysfs(&ch->ch_pun, classp);
		}

	}

	return 0;
}


/*
 * dgnc_tty_post_uninit()
 *
 * UnInitialize any global tty related data.
 */
void dgnc_tty_post_uninit(void)
{
	kfree(dgnc_TmpWriteBuf);
	dgnc_TmpWriteBuf = NULL;
}


/*
 * dgnc_tty_uninit()
 *
 * Uninitialize the TTY portion of this driver.  Free all memory and
 * resources.
 */
void dgnc_tty_uninit(struct dgnc_board *brd)
{
	int i = 0;

	if (brd->dgnc_Major_Serial_Registered) {
		dgnc_BoardsByMajor[brd->SerialDriver.major] = NULL;
		brd->dgnc_Serial_Major = 0;
		for (i = 0; i < brd->nasync; i++) {
			dgnc_remove_tty_sysfs(brd->channels[i]->ch_tun.un_sysfs);
			tty_unregister_device(&brd->SerialDriver, i);
		}
		tty_unregister_driver(&brd->SerialDriver);
		brd->dgnc_Major_Serial_Registered = FALSE;
	}

	if (brd->dgnc_Major_TransparentPrint_Registered) {
		dgnc_BoardsByMajor[brd->PrintDriver.major] = NULL;
		brd->dgnc_TransparentPrint_Major = 0;
		for (i = 0; i < brd->nasync; i++) {
			dgnc_remove_tty_sysfs(brd->channels[i]->ch_pun.un_sysfs);
			tty_unregister_device(&brd->PrintDriver, i);
		}
		tty_unregister_driver(&brd->PrintDriver);
		brd->dgnc_Major_TransparentPrint_Registered = FALSE;
	}

	kfree(brd->SerialDriver.ttys);
	brd->SerialDriver.ttys = NULL;
	kfree(brd->PrintDriver.ttys);
	brd->PrintDriver.ttys = NULL;
}


#define TMPBUFLEN (1024)

/*
 * dgnc_sniff - Dump data out to the "sniff" buffer if the
 * proc sniff file is opened...
 */
void dgnc_sniff_nowait_nolock(struct channel_t *ch, uchar *text, uchar *buf, int len)
{
	struct timeval tv;
	int n;
	int r;
	int nbuf;
	int i;
	int tmpbuflen;
	char *tmpbuf;
	char *p;
	int too_much_data;

	tmpbuf = kzalloc(TMPBUFLEN, GFP_ATOMIC);
	if (!tmpbuf)
		return;
	p = tmpbuf;

	/* Leave if sniff not open */
	if (!(ch->ch_sniff_flags & SNIFF_OPEN))
		goto exit;

	do_gettimeofday(&tv);

	/* Create our header for data dump */
	p += sprintf(p, "<%ld %ld><%s><", tv.tv_sec, tv.tv_usec, text);
	tmpbuflen = p - tmpbuf;

	do {
		too_much_data = 0;

		for (i = 0; i < len && tmpbuflen < (TMPBUFLEN - 4); i++) {
			p += sprintf(p, "%02x ", *buf);
			buf++;
			tmpbuflen = p - tmpbuf;
		}

		if (tmpbuflen < (TMPBUFLEN - 4)) {
			if (i > 0)
				p += sprintf(p - 1, "%s\n", ">");
			else
				p += sprintf(p, "%s\n", ">");
		} else {
			too_much_data = 1;
			len -= i;
		}

		nbuf = strlen(tmpbuf);
		p = tmpbuf;

		/*
		 *  Loop while data remains.
		 */
		while (nbuf > 0 && ch->ch_sniff_buf) {
			/*
			 *  Determine the amount of available space left in the
			 *  buffer.  If there's none, wait until some appears.
			 */
			n = (ch->ch_sniff_out - ch->ch_sniff_in - 1) & SNIFF_MASK;

			/*
			 * If there is no space left to write to in our sniff buffer,
			 * we have no choice but to drop the data.
			 * We *cannot* sleep here waiting for space, because this
			 * function was probably called by the interrupt/timer routines!
			 */
			if (n == 0)
				goto exit;

			/*
			 * Copy as much data as will fit.
			 */

			if (n > nbuf)
				n = nbuf;

			r = SNIFF_MAX - ch->ch_sniff_in;

			if (r <= n) {
				memcpy(ch->ch_sniff_buf + ch->ch_sniff_in, p, r);

				n -= r;
				ch->ch_sniff_in = 0;
				p += r;
				nbuf -= r;
			}

			memcpy(ch->ch_sniff_buf + ch->ch_sniff_in, p, n);

			ch->ch_sniff_in += n;
			p += n;
			nbuf -= n;

			/*
			 *  Wakeup any thread waiting for data
			 */
			if (ch->ch_sniff_flags & SNIFF_WAIT_DATA) {
				ch->ch_sniff_flags &= ~SNIFF_WAIT_DATA;
				wake_up_interruptible(&ch->ch_sniff_wait);
			}
		}

		/*
		 * If the user sent us too much data to push into our tmpbuf,
		 * we need to keep looping around on all the data.
		 */
		if (too_much_data) {
			p = tmpbuf;
			tmpbuflen = 0;
		}

	} while (too_much_data);

exit:
	kfree(tmpbuf);
}


/*=======================================================================
 *
 *	dgnc_wmove - Write data to transmit queue.
 *
 *		ch	- Pointer to channel structure.
 *		buf	- Poiter to characters to be moved.
 *		n	- Number of characters to move.
 *
 *=======================================================================*/
static void dgnc_wmove(struct channel_t *ch, char *buf, uint n)
{
	int	remain;
	uint	head;

	if (!ch || ch->magic != DGNC_CHANNEL_MAGIC)
		return;

	head = ch->ch_w_head & WQUEUEMASK;

	/*
	 * If the write wraps over the top of the circular buffer,
	 * move the portion up to the wrap point, and reset the
	 * pointers to the bottom.
	 */
	remain = WQUEUESIZE - head;

	if (n >= remain) {
		n -= remain;
		memcpy(ch->ch_wqueue + head, buf, remain);
		head = 0;
		buf += remain;
	}

	if (n > 0) {
		/*
		 * Move rest of data.
		 */
		remain = n;
		memcpy(ch->ch_wqueue + head, buf, remain);
		head += remain;
	}

	head &= WQUEUEMASK;
	ch->ch_w_head = head;
}




/*=======================================================================
 *
 *      dgnc_input - Process received data.
 *
 *	      ch      - Pointer to channel structure.
 *
 *=======================================================================*/
void dgnc_input(struct channel_t *ch)
{
	struct dgnc_board *bd;
	struct tty_struct *tp;
	struct tty_ldisc *ld;
	uint	rmask;
	ushort	head;
	ushort	tail;
	int	data_len;
	unsigned long flags;
	int flip_len;
	int len = 0;
	int n = 0;
	int s = 0;
	int i = 0;

	if (!ch || ch->magic != DGNC_CHANNEL_MAGIC)
		return;

	tp = ch->ch_tun.un_tty;

	bd = ch->ch_bd;
	if (!bd || bd->magic != DGNC_BOARD_MAGIC)
		return;

	spin_lock_irqsave(&ch->ch_lock, flags);

	/*
	 *      Figure the number of characters in the buffer.
	 *      Exit immediately if none.
	 */
	rmask = RQUEUEMASK;
	head = ch->ch_r_head & rmask;
	tail = ch->ch_r_tail & rmask;
	data_len = (head - tail) & rmask;

	if (data_len == 0) {
		spin_unlock_irqrestore(&ch->ch_lock, flags);
		return;
	}

	/*
	 * If the device is not open, or CREAD is off,
	 * flush input data and return immediately.
	 */
	if (!tp || (tp->magic != TTY_MAGIC) || !(ch->ch_tun.un_flags & UN_ISOPEN) ||
	    !(tp->termios.c_cflag & CREAD) || (ch->ch_tun.un_flags & UN_CLOSING)) {

		ch->ch_r_head = tail;

		/* Force queue flow control to be released, if needed */
		dgnc_check_queue_flow_control(ch);

		spin_unlock_irqrestore(&ch->ch_lock, flags);
		return;
	}

	/*
	 * If we are throttled, simply don't read any data.
	 */
	if (ch->ch_flags & CH_FORCED_STOPI) {
		spin_unlock_irqrestore(&ch->ch_lock, flags);
		return;
	}

	flip_len = TTY_FLIPBUF_SIZE;

	/* Chop down the length, if needed */
	len = min(data_len, flip_len);
	len = min(len, (N_TTY_BUF_SIZE - 1));

	ld = tty_ldisc_ref(tp);

#ifdef TTY_DONT_FLIP
	/*
	 * If the DONT_FLIP flag is on, don't flush our buffer, and act
	 * like the ld doesn't have any space to put the data right now.
	 */
	if (test_bit(TTY_DONT_FLIP, &tp->flags))
		len = 0;
#endif

	/*
	 * If we were unable to get a reference to the ld,
	 * don't flush our buffer, and act like the ld doesn't
	 * have any space to put the data right now.
	 */
	if (!ld) {
		len = 0;
	} else {
		/*
		 * If ld doesn't have a pointer to a receive_buf function,
		 * flush the data, then act like the ld doesn't have any
		 * space to put the data right now.
		 */
		if (!ld->ops->receive_buf) {
			ch->ch_r_head = ch->ch_r_tail;
			len = 0;
		}
	}

	if (len <= 0) {
		spin_unlock_irqrestore(&ch->ch_lock, flags);
		if (ld)
			tty_ldisc_deref(ld);
		return;
	}

	/*
	 * The tty layer in the kernel has changed in 2.6.16+.
	 *
	 * The flip buffers in the tty structure are no longer exposed,
	 * and probably will be going away eventually.
	 *
	 * If we are completely raw, we don't need to go through a lot
	 * of the tty layers that exist.
	 * In this case, we take the shortest and fastest route we
	 * can to relay the data to the user.
	 *
	 * On the other hand, if we are not raw, we need to go through
	 * the new 2.6.16+ tty layer, which has its API more well defined.
	 */
	len = tty_buffer_request_room(tp->port, len);
	n = len;

	/*
	 * n now contains the most amount of data we can copy,
	 * bounded either by how much the Linux tty layer can handle,
	 * or the amount of data the card actually has pending...
	 */
	while (n) {
		s = ((head >= tail) ? head : RQUEUESIZE) - tail;
		s = min(s, n);

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
				if (*(ch->ch_equeue + tail + i) & UART_LSR_BI)
					tty_insert_flip_char(tp->port, *(ch->ch_rqueue + tail + i), TTY_BREAK);
				else if (*(ch->ch_equeue + tail + i) & UART_LSR_PE)
					tty_insert_flip_char(tp->port, *(ch->ch_rqueue + tail + i), TTY_PARITY);
				else if (*(ch->ch_equeue + tail + i) & UART_LSR_FE)
					tty_insert_flip_char(tp->port, *(ch->ch_rqueue + tail + i), TTY_FRAME);
				else
					tty_insert_flip_char(tp->port, *(ch->ch_rqueue + tail + i), TTY_NORMAL);
			}
		} else {
			tty_insert_flip_string(tp->port, ch->ch_rqueue + tail, s);
		}

		dgnc_sniff_nowait_nolock(ch, "USER READ", ch->ch_rqueue + tail, s);

		tail += s;
		n -= s;
		/* Flip queue if needed */
		tail &= rmask;
	}

	ch->ch_r_tail = tail & rmask;
	ch->ch_e_tail = tail & rmask;
	dgnc_check_queue_flow_control(ch);
	spin_unlock_irqrestore(&ch->ch_lock, flags);

	/* Tell the tty layer its okay to "eat" the data now */
	tty_flip_buffer_push(tp->port);

	if (ld)
		tty_ldisc_deref(ld);
}


/************************************************************************
 * Determines when CARRIER changes state and takes appropriate
 * action.
 ************************************************************************/
void dgnc_carrier(struct channel_t *ch)
{
	struct dgnc_board *bd;

	int virt_carrier = 0;
	int phys_carrier = 0;

	if (!ch || ch->magic != DGNC_CHANNEL_MAGIC)
		return;

	bd = ch->ch_bd;

	if (!bd || bd->magic != DGNC_BOARD_MAGIC)
		return;

	if (ch->ch_mistat & UART_MSR_DCD)
		phys_carrier = 1;

	if (ch->ch_digi.digi_flags & DIGI_FORCEDCD)
		virt_carrier = 1;

	if (ch->ch_c_cflag & CLOCAL)
		virt_carrier = 1;

	/*
	 * Test for a VIRTUAL carrier transition to HIGH.
	 */
	if (((ch->ch_flags & CH_FCAR) == 0) && (virt_carrier == 1)) {

		/*
		 * When carrier rises, wake any threads waiting
		 * for carrier in the open routine.
		 */

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
	if ((virt_carrier == 0) && ((ch->ch_flags & CH_CD) != 0) &&
	    (phys_carrier == 0)) {

		/*
		 *   When carrier drops:
		 *
		 *   Drop carrier on all open units.
		 *
		 *   Flush queues, waking up any task waiting in the
		 *   line discipline.
		 *
		 *   Send a hangup to the control terminal.
		 *
		 *   Enable all select calls.
		 */
		if (waitqueue_active(&(ch->ch_flags_wait)))
			wake_up_interruptible(&ch->ch_flags_wait);

		if (ch->ch_tun.un_open_count > 0)
			tty_hangup(ch->ch_tun.un_tty);

		if (ch->ch_pun.un_open_count > 0)
			tty_hangup(ch->ch_pun.un_tty);
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

/*
 *  Assign the custom baud rate to the channel structure
 */
static void dgnc_set_custom_speed(struct channel_t *ch, uint newrate)
{
	int testdiv;
	int testrate_high;
	int testrate_low;
	int deltahigh;
	int deltalow;

	if (newrate <= 0) {
		ch->ch_custom_speed = 0;
		return;
	}

	/*
	 *  Since the divisor is stored in a 16-bit integer, we make sure
	 *  we don't allow any rates smaller than a 16-bit integer would allow.
	 *  And of course, rates above the dividend won't fly.
	 */
	if (newrate && newrate < ((ch->ch_bd->bd_dividend / 0xFFFF) + 1))
		newrate = ((ch->ch_bd->bd_dividend / 0xFFFF) + 1);

	if (newrate && newrate > ch->ch_bd->bd_dividend)
		newrate = ch->ch_bd->bd_dividend;

	if (newrate > 0) {
		testdiv = ch->ch_bd->bd_dividend / newrate;

		/*
		 *  If we try to figure out what rate the board would use
		 *  with the test divisor, it will be either equal or higher
		 *  than the requested baud rate.  If we then determine the
		 *  rate with a divisor one higher, we will get the next lower
		 *  supported rate below the requested.
		 */
		testrate_high = ch->ch_bd->bd_dividend / testdiv;
		testrate_low  = ch->ch_bd->bd_dividend / (testdiv + 1);

		/*
		 *  If the rate for the requested divisor is correct, just
		 *  use it and be done.
		 */
		if (testrate_high != newrate) {
			/*
			 *  Otherwise, pick the rate that is closer (i.e. whichever rate
			 *  has a smaller delta).
			 */
			deltahigh = testrate_high - newrate;
			deltalow = newrate - testrate_low;

			if (deltahigh < deltalow)
				newrate = testrate_high;
			else
				newrate = testrate_low;
		}
	}

	ch->ch_custom_speed = newrate;
}


void dgnc_check_queue_flow_control(struct channel_t *ch)
{
	int qleft = 0;

	/* Store how much space we have left in the queue */
	qleft = ch->ch_r_tail - ch->ch_r_head - 1;
	if (qleft < 0)
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
		if (ch->ch_digi.digi_flags & CTSPACE || ch->ch_c_cflag & CRTSCTS) {
			if (!(ch->ch_flags & CH_RECEIVER_OFF)) {
				ch->ch_bd->bd_ops->disable_receiver(ch);
				ch->ch_flags |= (CH_RECEIVER_OFF);
			}
		}
		/* SWFLOW */
		else if (ch->ch_c_iflag & IXOFF) {
			if (ch->ch_stops_sent <= MAX_STOPS_SENT) {
				ch->ch_bd->bd_ops->send_stop_character(ch);
				ch->ch_stops_sent++;
			}
		}
		/* No FLOW */
		else {
			/* Empty... Can't do anything about the impending overflow... */
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
		if (ch->ch_digi.digi_flags & RTSPACE || ch->ch_c_cflag & CRTSCTS) {
			if (ch->ch_flags & CH_RECEIVER_OFF) {
				ch->ch_bd->bd_ops->enable_receiver(ch);
				ch->ch_flags &= ~(CH_RECEIVER_OFF);
			}
		}
		/* SWFLOW */
		else if (ch->ch_c_iflag & IXOFF && ch->ch_stops_sent) {
			ch->ch_stops_sent = 0;
			ch->ch_bd->bd_ops->send_start_character(ch);
		}
		/* No FLOW */
		else {
			/* Nothing needed. */
		}
	}
}


void dgnc_wakeup_writes(struct channel_t *ch)
{
	int qlen = 0;
	unsigned long flags;

	if (!ch || ch->magic != DGNC_CHANNEL_MAGIC)
		return;

	spin_lock_irqsave(&ch->ch_lock, flags);

	/*
	 * If channel now has space, wake up anyone waiting on the condition.
	 */
	qlen = ch->ch_w_head - ch->ch_w_tail;
	if (qlen < 0)
		qlen += WQUEUESIZE;

	if (qlen >= (WQUEUESIZE - 256)) {
		spin_unlock_irqrestore(&ch->ch_lock, flags);
		return;
	}

	if (ch->ch_tun.un_flags & UN_ISOPEN) {
		if ((ch->ch_tun.un_tty->flags & (1 << TTY_DO_WRITE_WAKEUP)) &&
			ch->ch_tun.un_tty->ldisc->ops->write_wakeup) {
			spin_unlock_irqrestore(&ch->ch_lock, flags);
			(ch->ch_tun.un_tty->ldisc->ops->write_wakeup)(ch->ch_tun.un_tty);
			spin_lock_irqsave(&ch->ch_lock, flags);
		}

		wake_up_interruptible(&ch->ch_tun.un_tty->write_wait);

		/*
		 * If unit is set to wait until empty, check to make sure
		 * the queue AND FIFO are both empty.
		 */
		if (ch->ch_tun.un_flags & UN_EMPTY) {
			if ((qlen == 0) && (ch->ch_bd->bd_ops->get_uart_bytes_left(ch) == 0)) {
				ch->ch_tun.un_flags &= ~(UN_EMPTY);

				/*
				 * If RTS Toggle mode is on, whenever
				 * the queue and UART is empty, keep RTS low.
				 */
				if (ch->ch_digi.digi_flags & DIGI_RTS_TOGGLE) {
					ch->ch_mostat &= ~(UART_MCR_RTS);
					ch->ch_bd->bd_ops->assert_modem_signals(ch);
				}

				/*
				 * If DTR Toggle mode is on, whenever
				 * the queue and UART is empty, keep DTR low.
				 */
				if (ch->ch_digi.digi_flags & DIGI_DTR_TOGGLE) {
					ch->ch_mostat &= ~(UART_MCR_DTR);
					ch->ch_bd->bd_ops->assert_modem_signals(ch);
				}
			}
		}

		wake_up_interruptible(&ch->ch_tun.un_flags_wait);
	}

	if (ch->ch_pun.un_flags & UN_ISOPEN) {
		if ((ch->ch_pun.un_tty->flags & (1 << TTY_DO_WRITE_WAKEUP)) &&
			ch->ch_pun.un_tty->ldisc->ops->write_wakeup) {
			spin_unlock_irqrestore(&ch->ch_lock, flags);
			(ch->ch_pun.un_tty->ldisc->ops->write_wakeup)(ch->ch_pun.un_tty);
			spin_lock_irqsave(&ch->ch_lock, flags);
		}

		wake_up_interruptible(&ch->ch_pun.un_tty->write_wait);

		/*
		 * If unit is set to wait until empty, check to make sure
		 * the queue AND FIFO are both empty.
		 */
		if (ch->ch_pun.un_flags & UN_EMPTY) {
			if ((qlen == 0) && (ch->ch_bd->bd_ops->get_uart_bytes_left(ch) == 0)) {
				ch->ch_pun.un_flags &= ~(UN_EMPTY);
			}
		}

		wake_up_interruptible(&ch->ch_pun.un_flags_wait);
	}

	spin_unlock_irqrestore(&ch->ch_lock, flags);
}



/************************************************************************
 *
 * TTY Entry points and helper functions
 *
 ************************************************************************/

/*
 * dgnc_tty_open()
 *
 */
static int dgnc_tty_open(struct tty_struct *tty, struct file *file)
{
	struct dgnc_board	*brd;
	struct channel_t *ch;
	struct un_t	*un;
	uint		major = 0;
	uint		minor = 0;
	int		rc = 0;
	unsigned long flags;

	rc = 0;

	major = MAJOR(tty_devnum(tty));
	minor = MINOR(tty_devnum(tty));

	if (major > 255)
		return -ENXIO;

	/* Get board pointer from our array of majors we have allocated */
	brd = dgnc_BoardsByMajor[major];
	if (!brd)
		return -ENXIO;

	/*
	 * If board is not yet up to a state of READY, go to
	 * sleep waiting for it to happen or they cancel the open.
	 */
	rc = wait_event_interruptible(brd->state_wait,
		(brd->state & BOARD_READY));

	if (rc)
		return rc;

	spin_lock_irqsave(&brd->bd_lock, flags);

	/* If opened device is greater than our number of ports, bail. */
	if (PORT_NUM(minor) > brd->nasync) {
		spin_unlock_irqrestore(&brd->bd_lock, flags);
		return -ENXIO;
	}

	ch = brd->channels[PORT_NUM(minor)];
	if (!ch) {
		spin_unlock_irqrestore(&brd->bd_lock, flags);
		return -ENXIO;
	}

	/* Drop board lock */
	spin_unlock_irqrestore(&brd->bd_lock, flags);

	/* Grab channel lock */
	spin_lock_irqsave(&ch->ch_lock, flags);

	/* Figure out our type */
	if (!IS_PRINT(minor)) {
		un = &brd->channels[PORT_NUM(minor)]->ch_tun;
		un->un_type = DGNC_SERIAL;
	} else if (IS_PRINT(minor)) {
		un = &brd->channels[PORT_NUM(minor)]->ch_pun;
		un->un_type = DGNC_PRINT;
	} else {
		spin_unlock_irqrestore(&ch->ch_lock, flags);
		return -ENXIO;
	}

	/*
	 * If the port is still in a previous open, and in a state
	 * where we simply cannot safely keep going, wait until the
	 * state clears.
	 */
	spin_unlock_irqrestore(&ch->ch_lock, flags);

	rc = wait_event_interruptible(ch->ch_flags_wait, ((ch->ch_flags & CH_OPENING) == 0));

	/* If ret is non-zero, user ctrl-c'ed us */
	if (rc)
		return -EINTR;

	/*
	 * If either unit is in the middle of the fragile part of close,
	 * we just cannot touch the channel safely.
	 * Go to sleep, knowing that when the channel can be
	 * touched safely, the close routine will signal the
	 * ch_flags_wait to wake us back up.
	 */
	rc = wait_event_interruptible(ch->ch_flags_wait,
		(((ch->ch_tun.un_flags | ch->ch_pun.un_flags) & UN_CLOSING) == 0));

	/* If ret is non-zero, user ctrl-c'ed us */
	if (rc)
		return -EINTR;

	spin_lock_irqsave(&ch->ch_lock, flags);


	/* Store our unit into driver_data, so we always have it available. */
	tty->driver_data = un;


	/*
	 * Initialize tty's
	 */
	if (!(un->un_flags & UN_ISOPEN)) {
		/* Store important variables. */
		un->un_tty     = tty;

		/* Maybe do something here to the TTY struct as well? */
	}


	/*
	 * Allocate channel buffers for read/write/error.
	 * Set flag, so we don't get trounced on.
	 */
	ch->ch_flags |= (CH_OPENING);

	/* Drop locks, as malloc with GFP_KERNEL can sleep */
	spin_unlock_irqrestore(&ch->ch_lock, flags);

	if (!ch->ch_rqueue)
		ch->ch_rqueue = kzalloc(RQUEUESIZE, GFP_KERNEL);
	if (!ch->ch_equeue)
		ch->ch_equeue = kzalloc(EQUEUESIZE, GFP_KERNEL);
	if (!ch->ch_wqueue)
		ch->ch_wqueue = kzalloc(WQUEUESIZE, GFP_KERNEL);

	spin_lock_irqsave(&ch->ch_lock, flags);

	ch->ch_flags &= ~(CH_OPENING);
	wake_up_interruptible(&ch->ch_flags_wait);

	/*
	 * Initialize if neither terminal or printer is open.
	 */
	if (!((ch->ch_tun.un_flags | ch->ch_pun.un_flags) & UN_ISOPEN)) {

		/*
		 * Flush input queues.
		 */
		ch->ch_r_head = 0;
		ch->ch_r_tail = 0;
		ch->ch_e_head = 0;
		ch->ch_e_tail = 0;
		ch->ch_w_head = 0;
		ch->ch_w_tail = 0;

		brd->bd_ops->flush_uart_write(ch);
		brd->bd_ops->flush_uart_read(ch);

		ch->ch_flags = 0;
		ch->ch_cached_lsr = 0;
		ch->ch_stop_sending_break = 0;
		ch->ch_stops_sent = 0;

		ch->ch_c_cflag   = tty->termios.c_cflag;
		ch->ch_c_iflag   = tty->termios.c_iflag;
		ch->ch_c_oflag   = tty->termios.c_oflag;
		ch->ch_c_lflag   = tty->termios.c_lflag;
		ch->ch_startc = tty->termios.c_cc[VSTART];
		ch->ch_stopc  = tty->termios.c_cc[VSTOP];

		/*
		 * Bring up RTS and DTR...
		 * Also handle RTS or DTR toggle if set.
		 */
		if (!(ch->ch_digi.digi_flags & DIGI_RTS_TOGGLE))
			ch->ch_mostat |= (UART_MCR_RTS);
		if (!(ch->ch_digi.digi_flags & DIGI_DTR_TOGGLE))
			ch->ch_mostat |= (UART_MCR_DTR);

		/* Tell UART to init itself */
		brd->bd_ops->uart_init(ch);
	}

	/*
	 * Run param in case we changed anything
	 */
	brd->bd_ops->param(tty);

	dgnc_carrier(ch);

	/*
	 * follow protocol for opening port
	 */

	spin_unlock_irqrestore(&ch->ch_lock, flags);

	rc = dgnc_block_til_ready(tty, file, ch);

	/* No going back now, increment our unit and channel counters */
	spin_lock_irqsave(&ch->ch_lock, flags);
	ch->ch_open_count++;
	un->un_open_count++;
	un->un_flags |= (UN_ISOPEN);
	spin_unlock_irqrestore(&ch->ch_lock, flags);

	return rc;
}


/*
 * dgnc_block_til_ready()
 *
 * Wait for DCD, if needed.
 */
static int dgnc_block_til_ready(struct tty_struct *tty, struct file *file, struct channel_t *ch)
{
	int retval = 0;
	struct un_t *un = NULL;
	unsigned long flags;
	uint	old_flags = 0;
	int	sleep_on_un_flags = 0;

	if (!tty || tty->magic != TTY_MAGIC || !file || !ch || ch->magic != DGNC_CHANNEL_MAGIC) {
		return -ENXIO;
	}

	un = tty->driver_data;
	if (!un || un->magic != DGNC_UNIT_MAGIC)
		return -ENXIO;

	spin_lock_irqsave(&ch->ch_lock, flags);

	ch->ch_wopen++;

	/* Loop forever */
	while (1) {

		sleep_on_un_flags = 0;

		/*
		 * If board has failed somehow during our sleep, bail with error.
		 */
		if (ch->ch_bd->state == BOARD_FAILED) {
			retval = -ENXIO;
			break;
		}

		/* If tty was hung up, break out of loop and set error. */
		if (tty_hung_up_p(file)) {
			retval = -EAGAIN;
			break;
		}

		/*
		 * If either unit is in the middle of the fragile part of close,
		 * we just cannot touch the channel safely.
		 * Go back to sleep, knowing that when the channel can be
		 * touched safely, the close routine will signal the
		 * ch_wait_flags to wake us back up.
		 */
		if (!((ch->ch_tun.un_flags | ch->ch_pun.un_flags) & UN_CLOSING)) {

			/*
			 * Our conditions to leave cleanly and happily:
			 * 1) NONBLOCKING on the tty is set.
			 * 2) CLOCAL is set.
			 * 3) DCD (fake or real) is active.
			 */

			if (file->f_flags & O_NONBLOCK)
				break;

			if (tty->flags & (1 << TTY_IO_ERROR)) {
				retval = -EIO;
				break;
			}

			if (ch->ch_flags & CH_CD)
				break;

			if (ch->ch_flags & CH_FCAR)
				break;
		} else {
			sleep_on_un_flags = 1;
		}

		/*
		 * If there is a signal pending, the user probably
		 * interrupted (ctrl-c) us.
		 * Leave loop with error set.
		 */
		if (signal_pending(current)) {
			retval = -ERESTARTSYS;
			break;
		}

		/*
		 * Store the flags before we let go of channel lock
		 */
		if (sleep_on_un_flags)
			old_flags = ch->ch_tun.un_flags | ch->ch_pun.un_flags;
		else
			old_flags = ch->ch_flags;

		/*
		 * Let go of channel lock before calling schedule.
		 * Our poller will get any FEP events and wake us up when DCD
		 * eventually goes active.
		 */

		spin_unlock_irqrestore(&ch->ch_lock, flags);

		/*
		 * Wait for something in the flags to change from the current value.
		 */
		if (sleep_on_un_flags)
			retval = wait_event_interruptible(un->un_flags_wait,
				(old_flags != (ch->ch_tun.un_flags | ch->ch_pun.un_flags)));
		else
			retval = wait_event_interruptible(ch->ch_flags_wait,
				(old_flags != ch->ch_flags));

		/*
		 * We got woken up for some reason.
		 * Before looping around, grab our channel lock.
		 */
		spin_lock_irqsave(&ch->ch_lock, flags);
	}

	ch->ch_wopen--;

	spin_unlock_irqrestore(&ch->ch_lock, flags);

	if (retval)
		return retval;

	return 0;
}


/*
 * dgnc_tty_hangup()
 *
 * Hangup the port.  Like a close, but don't wait for output to drain.
 */
static void dgnc_tty_hangup(struct tty_struct *tty)
{
	struct un_t	*un;

	if (!tty || tty->magic != TTY_MAGIC)
		return;

	un = tty->driver_data;
	if (!un || un->magic != DGNC_UNIT_MAGIC)
		return;

	/* flush the transmit queues */
	dgnc_tty_flush_buffer(tty);

}


/*
 * dgnc_tty_close()
 *
 */
static void dgnc_tty_close(struct tty_struct *tty, struct file *file)
{
	struct ktermios *ts;
	struct dgnc_board *bd;
	struct channel_t *ch;
	struct un_t *un;
	unsigned long flags;
	int rc = 0;

	if (!tty || tty->magic != TTY_MAGIC)
		return;

	un = tty->driver_data;
	if (!un || un->magic != DGNC_UNIT_MAGIC)
		return;

	ch = un->un_ch;
	if (!ch || ch->magic != DGNC_CHANNEL_MAGIC)
		return;

	bd = ch->ch_bd;
	if (!bd || bd->magic != DGNC_BOARD_MAGIC)
		return;

	ts = &tty->termios;

	spin_lock_irqsave(&ch->ch_lock, flags);

	/*
	 * Determine if this is the last close or not - and if we agree about
	 * which type of close it is with the Line Discipline
	 */
	if ((tty->count == 1) && (un->un_open_count != 1)) {
		/*
		 * Uh, oh.  tty->count is 1, which means that the tty
		 * structure will be freed.  un_open_count should always
		 * be one in these conditions.  If it's greater than
		 * one, we've got real problems, since it means the
		 * serial port won't be shutdown.
		 */
		APR(("tty->count is 1, un open count is %d\n", un->un_open_count));
		un->un_open_count = 1;
	}

	if (un->un_open_count)
		un->un_open_count--;
	else
		APR(("bad serial port open count of %d\n", un->un_open_count));

	ch->ch_open_count--;

	if (ch->ch_open_count && un->un_open_count) {
		spin_unlock_irqrestore(&ch->ch_lock, flags);
		return;
	}

	/* OK, its the last close on the unit */
	un->un_flags |= UN_CLOSING;

	tty->closing = 1;


	/*
	 * Only officially close channel if count is 0 and
	 * DIGI_PRINTER bit is not set.
	 */
	if ((ch->ch_open_count == 0) && !(ch->ch_digi.digi_flags & DIGI_PRINTER)) {

		ch->ch_flags &= ~(CH_STOPI | CH_FORCED_STOPI);

		/*
		 * turn off print device when closing print device.
		 */
		if ((un->un_type == DGNC_PRINT) && (ch->ch_flags & CH_PRON)) {
			dgnc_wmove(ch, ch->ch_digi.digi_offstr,
				(int) ch->ch_digi.digi_offlen);
			ch->ch_flags &= ~CH_PRON;
		}

		spin_unlock_irqrestore(&ch->ch_lock, flags);
		/* wait for output to drain */
		/* This will also return if we take an interrupt */

		rc = bd->bd_ops->drain(tty, 0);

		dgnc_tty_flush_buffer(tty);
		tty_ldisc_flush(tty);

		spin_lock_irqsave(&ch->ch_lock, flags);

		tty->closing = 0;

		/*
		 * If we have HUPCL set, lower DTR and RTS
		 */
		if (ch->ch_c_cflag & HUPCL) {

			/* Drop RTS/DTR */
			ch->ch_mostat &= ~(UART_MCR_DTR | UART_MCR_RTS);
			bd->bd_ops->assert_modem_signals(ch);

			/*
			 * Go to sleep to ensure RTS/DTR
			 * have been dropped for modems to see it.
			 */
			if (ch->ch_close_delay) {
				spin_unlock_irqrestore(&ch->ch_lock,
						       flags);
				dgnc_ms_sleep(ch->ch_close_delay);
				spin_lock_irqsave(&ch->ch_lock, flags);
			}
		}

		ch->ch_old_baud = 0;

		/* Turn off UART interrupts for this port */
		ch->ch_bd->bd_ops->uart_off(ch);
	} else {
		/*
		 * turn off print device when closing print device.
		 */
		if ((un->un_type == DGNC_PRINT) && (ch->ch_flags & CH_PRON)) {
			dgnc_wmove(ch, ch->ch_digi.digi_offstr,
				(int) ch->ch_digi.digi_offlen);
			ch->ch_flags &= ~CH_PRON;
		}
	}

	un->un_tty = NULL;
	un->un_flags &= ~(UN_ISOPEN | UN_CLOSING);

	wake_up_interruptible(&ch->ch_flags_wait);
	wake_up_interruptible(&un->un_flags_wait);

	spin_unlock_irqrestore(&ch->ch_lock, flags);
}


/*
 * dgnc_tty_chars_in_buffer()
 *
 * Return number of characters that have not been transmitted yet.
 *
 * This routine is used by the line discipline to determine if there
 * is data waiting to be transmitted/drained/flushed or not.
 */
static int dgnc_tty_chars_in_buffer(struct tty_struct *tty)
{
	struct channel_t *ch = NULL;
	struct un_t *un = NULL;
	ushort thead;
	ushort ttail;
	uint tmask;
	uint chars = 0;
	unsigned long flags;

	if (tty == NULL)
		return 0;

	un = tty->driver_data;
	if (!un || un->magic != DGNC_UNIT_MAGIC)
		return 0;

	ch = un->un_ch;
	if (!ch || ch->magic != DGNC_CHANNEL_MAGIC)
		return 0;

	spin_lock_irqsave(&ch->ch_lock, flags);

	tmask = WQUEUEMASK;
	thead = ch->ch_w_head & tmask;
	ttail = ch->ch_w_tail & tmask;

	spin_unlock_irqrestore(&ch->ch_lock, flags);

	if (ttail == thead) {
		chars = 0;
	} else {
		if (thead >= ttail)
			chars = thead - ttail;
		else
			chars = thead - ttail + WQUEUESIZE;
	}

	return chars;
}


/*
 * dgnc_maxcps_room
 *
 * Reduces bytes_available to the max number of characters
 * that can be sent currently given the maxcps value, and
 * returns the new bytes_available.  This only affects printer
 * output.
 */
static int dgnc_maxcps_room(struct tty_struct *tty, int bytes_available)
{
	struct channel_t *ch = NULL;
	struct un_t *un = NULL;

	if (!tty)
		return bytes_available;

	un = tty->driver_data;
	if (!un || un->magic != DGNC_UNIT_MAGIC)
		return bytes_available;

	ch = un->un_ch;
	if (!ch || ch->magic != DGNC_CHANNEL_MAGIC)
		return bytes_available;

	/*
	 * If its not the Transparent print device, return
	 * the full data amount.
	 */
	if (un->un_type != DGNC_PRINT)
		return bytes_available;

	if (ch->ch_digi.digi_maxcps > 0 && ch->ch_digi.digi_bufsize > 0) {
		int cps_limit = 0;
		unsigned long current_time = jiffies;
		unsigned long buffer_time = current_time +
			(HZ * ch->ch_digi.digi_bufsize) / ch->ch_digi.digi_maxcps;

		if (ch->ch_cpstime < current_time) {
			/* buffer is empty */
			ch->ch_cpstime = current_time;	    /* reset ch_cpstime */
			cps_limit = ch->ch_digi.digi_bufsize;
		} else if (ch->ch_cpstime < buffer_time) {
			/* still room in the buffer */
			cps_limit = ((buffer_time - ch->ch_cpstime) * ch->ch_digi.digi_maxcps) / HZ;
		} else {
			/* no room in the buffer */
			cps_limit = 0;
		}

		bytes_available = min(cps_limit, bytes_available);
	}

	return bytes_available;
}


/*
 * dgnc_tty_write_room()
 *
 * Return space available in Tx buffer
 */
static int dgnc_tty_write_room(struct tty_struct *tty)
{
	struct channel_t *ch = NULL;
	struct un_t *un = NULL;
	ushort head;
	ushort tail;
	ushort tmask;
	int ret = 0;
	unsigned long flags;

	if (tty == NULL || dgnc_TmpWriteBuf == NULL)
		return 0;

	un = tty->driver_data;
	if (!un || un->magic != DGNC_UNIT_MAGIC)
		return 0;

	ch = un->un_ch;
	if (!ch || ch->magic != DGNC_CHANNEL_MAGIC)
		return 0;

	spin_lock_irqsave(&ch->ch_lock, flags);

	tmask = WQUEUEMASK;
	head = (ch->ch_w_head) & tmask;
	tail = (ch->ch_w_tail) & tmask;

	ret = tail - head - 1;
	if (ret < 0)
		ret += WQUEUESIZE;

	/* Limit printer to maxcps */
	ret = dgnc_maxcps_room(tty, ret);

	/*
	 * If we are printer device, leave space for
	 * possibly both the on and off strings.
	 */
	if (un->un_type == DGNC_PRINT) {
		if (!(ch->ch_flags & CH_PRON))
			ret -= ch->ch_digi.digi_onlen;
		ret -= ch->ch_digi.digi_offlen;
	} else {
		if (ch->ch_flags & CH_PRON)
			ret -= ch->ch_digi.digi_offlen;
	}

	if (ret < 0)
		ret = 0;

	spin_unlock_irqrestore(&ch->ch_lock, flags);

	return ret;
}


/*
 * dgnc_tty_put_char()
 *
 * Put a character into ch->ch_buf
 *
 *      - used by the line discipline for OPOST processing
 */
static int dgnc_tty_put_char(struct tty_struct *tty, unsigned char c)
{
	/*
	 * Simply call tty_write.
	 */
	dgnc_tty_write(tty, &c, 1);
	return 1;
}


/*
 * dgnc_tty_write()
 *
 * Take data from the user or kernel and send it out to the FEP.
 * In here exists all the Transparent Print magic as well.
 */
static int dgnc_tty_write(struct tty_struct *tty,
		const unsigned char *buf, int count)
{
	struct channel_t *ch = NULL;
	struct un_t *un = NULL;
	int bufcount = 0, n = 0;
	int orig_count = 0;
	unsigned long flags;
	ushort head;
	ushort tail;
	ushort tmask;
	uint remain;
	int from_user = 0;

	if (tty == NULL || dgnc_TmpWriteBuf == NULL)
		return 0;

	un = tty->driver_data;
	if (!un || un->magic != DGNC_UNIT_MAGIC)
		return 0;

	ch = un->un_ch;
	if (!ch || ch->magic != DGNC_CHANNEL_MAGIC)
		return 0;

	if (!count)
		return 0;

	/*
	 * Store original amount of characters passed in.
	 * This helps to figure out if we should ask the FEP
	 * to send us an event when it has more space available.
	 */
	orig_count = count;

	spin_lock_irqsave(&ch->ch_lock, flags);

	/* Get our space available for the channel from the board */
	tmask = WQUEUEMASK;
	head = (ch->ch_w_head) & tmask;
	tail = (ch->ch_w_tail) & tmask;

	bufcount = tail - head - 1;
	if (bufcount < 0)
		bufcount += WQUEUESIZE;

	/*
	 * Limit printer output to maxcps overall, with bursts allowed
	 * up to bufsize characters.
	 */
	bufcount = dgnc_maxcps_room(tty, bufcount);

	/*
	 * Take minimum of what the user wants to send, and the
	 * space available in the FEP buffer.
	 */
	count = min(count, bufcount);

	/*
	 * Bail if no space left.
	 */
	if (count <= 0) {
		spin_unlock_irqrestore(&ch->ch_lock, flags);
		return 0;
	}

	/*
	 * Output the printer ON string, if we are in terminal mode, but
	 * need to be in printer mode.
	 */
	if ((un->un_type == DGNC_PRINT) && !(ch->ch_flags & CH_PRON)) {
		dgnc_wmove(ch, ch->ch_digi.digi_onstr,
		    (int) ch->ch_digi.digi_onlen);
		head = (ch->ch_w_head) & tmask;
		ch->ch_flags |= CH_PRON;
	}

	/*
	 * On the other hand, output the printer OFF string, if we are
	 * currently in printer mode, but need to output to the terminal.
	 */
	if ((un->un_type != DGNC_PRINT) && (ch->ch_flags & CH_PRON)) {
		dgnc_wmove(ch, ch->ch_digi.digi_offstr,
			(int) ch->ch_digi.digi_offlen);
		head = (ch->ch_w_head) & tmask;
		ch->ch_flags &= ~CH_PRON;
	}

	/*
	 * If there is nothing left to copy, or I can't handle any more data, leave.
	 */
	if (count <= 0) {
		spin_unlock_irqrestore(&ch->ch_lock, flags);
		return 0;
	}

	if (from_user) {

		count = min(count, WRITEBUFLEN);

		spin_unlock_irqrestore(&ch->ch_lock, flags);

		/*
		 * If data is coming from user space, copy it into a temporary
		 * buffer so we don't get swapped out while doing the copy to
		 * the board.
		 */
		/* we're allowed to block if it's from_user */
		if (down_interruptible(&dgnc_TmpWriteSem))
			return -EINTR;

		/*
		 * copy_from_user() returns the number
		 * of bytes that could *NOT* be copied.
		 */
		count -= copy_from_user(dgnc_TmpWriteBuf, (const uchar __user *) buf, count);

		if (!count) {
			up(&dgnc_TmpWriteSem);
			return -EFAULT;
		}

		spin_lock_irqsave(&ch->ch_lock, flags);

		buf = dgnc_TmpWriteBuf;

	}

	n = count;

	/*
	 * If the write wraps over the top of the circular buffer,
	 * move the portion up to the wrap point, and reset the
	 * pointers to the bottom.
	 */
	remain = WQUEUESIZE - head;

	if (n >= remain) {
		n -= remain;
		memcpy(ch->ch_wqueue + head, buf, remain);
		dgnc_sniff_nowait_nolock(ch, "USER WRITE", ch->ch_wqueue + head, remain);
		head = 0;
		buf += remain;
	}

	if (n > 0) {
		/*
		 * Move rest of data.
		 */
		remain = n;
		memcpy(ch->ch_wqueue + head, buf, remain);
		dgnc_sniff_nowait_nolock(ch, "USER WRITE", ch->ch_wqueue + head, remain);
		head += remain;
	}

	if (count) {
		head &= tmask;
		ch->ch_w_head = head;
	}

	/* Update printer buffer empty time. */
	if ((un->un_type == DGNC_PRINT) && (ch->ch_digi.digi_maxcps > 0)
	    && (ch->ch_digi.digi_bufsize > 0)) {
		ch->ch_cpstime += (HZ * count) / ch->ch_digi.digi_maxcps;
	}

	if (from_user) {
		spin_unlock_irqrestore(&ch->ch_lock, flags);
		up(&dgnc_TmpWriteSem);
	} else {
		spin_unlock_irqrestore(&ch->ch_lock, flags);
	}

	if (count) {
		/*
		 * Channel lock is grabbed and then released
		 * inside this routine.
		 */
		ch->ch_bd->bd_ops->copy_data_from_queue_to_uart(ch);
	}

	return count;
}


/*
 * Return modem signals to ld.
 */

static int dgnc_tty_tiocmget(struct tty_struct *tty)
{
	struct channel_t *ch;
	struct un_t *un;
	int result = -EIO;
	uchar mstat = 0;
	unsigned long flags;

	if (!tty || tty->magic != TTY_MAGIC)
		return result;

	un = tty->driver_data;
	if (!un || un->magic != DGNC_UNIT_MAGIC)
		return result;

	ch = un->un_ch;
	if (!ch || ch->magic != DGNC_CHANNEL_MAGIC)
		return result;

	spin_lock_irqsave(&ch->ch_lock, flags);

	mstat = (ch->ch_mostat | ch->ch_mistat);

	spin_unlock_irqrestore(&ch->ch_lock, flags);

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

	return result;
}


/*
 * dgnc_tty_tiocmset()
 *
 * Set modem signals, called by ld.
 */

static int dgnc_tty_tiocmset(struct tty_struct *tty,
		unsigned int set, unsigned int clear)
{
	struct dgnc_board *bd;
	struct channel_t *ch;
	struct un_t *un;
	int ret = -EIO;
	unsigned long flags;

	if (!tty || tty->magic != TTY_MAGIC)
		return ret;

	un = tty->driver_data;
	if (!un || un->magic != DGNC_UNIT_MAGIC)
		return ret;

	ch = un->un_ch;
	if (!ch || ch->magic != DGNC_CHANNEL_MAGIC)
		return ret;

	bd = ch->ch_bd;
	if (!bd || bd->magic != DGNC_BOARD_MAGIC)
		return ret;

	spin_lock_irqsave(&ch->ch_lock, flags);

	if (set & TIOCM_RTS)
		ch->ch_mostat |= UART_MCR_RTS;

	if (set & TIOCM_DTR)
		ch->ch_mostat |= UART_MCR_DTR;

	if (clear & TIOCM_RTS)
		ch->ch_mostat &= ~(UART_MCR_RTS);

	if (clear & TIOCM_DTR)
		ch->ch_mostat &= ~(UART_MCR_DTR);

	ch->ch_bd->bd_ops->assert_modem_signals(ch);

	spin_unlock_irqrestore(&ch->ch_lock, flags);

	return 0;
}


/*
 * dgnc_tty_send_break()
 *
 * Send a Break, called by ld.
 */
static int dgnc_tty_send_break(struct tty_struct *tty, int msec)
{
	struct dgnc_board *bd;
	struct channel_t *ch;
	struct un_t *un;
	int ret = -EIO;
	unsigned long flags;

	if (!tty || tty->magic != TTY_MAGIC)
		return ret;

	un = tty->driver_data;
	if (!un || un->magic != DGNC_UNIT_MAGIC)
		return ret;

	ch = un->un_ch;
	if (!ch || ch->magic != DGNC_CHANNEL_MAGIC)
		return ret;

	bd = ch->ch_bd;
	if (!bd || bd->magic != DGNC_BOARD_MAGIC)
		return ret;

	switch (msec) {
	case -1:
		msec = 0xFFFF;
		break;
	case 0:
		msec = 0;
		break;
	default:
		break;
	}

	spin_lock_irqsave(&ch->ch_lock, flags);

	ch->ch_bd->bd_ops->send_break(ch, msec);

	spin_unlock_irqrestore(&ch->ch_lock, flags);

	return 0;

}


/*
 * dgnc_tty_wait_until_sent()
 *
 * wait until data has been transmitted, called by ld.
 */
static void dgnc_tty_wait_until_sent(struct tty_struct *tty, int timeout)
{
	struct dgnc_board *bd;
	struct channel_t *ch;
	struct un_t *un;
	int rc;

	if (!tty || tty->magic != TTY_MAGIC)
		return;

	un = tty->driver_data;
	if (!un || un->magic != DGNC_UNIT_MAGIC)
		return;

	ch = un->un_ch;
	if (!ch || ch->magic != DGNC_CHANNEL_MAGIC)
		return;

	bd = ch->ch_bd;
	if (!bd || bd->magic != DGNC_BOARD_MAGIC)
		return;

	rc = bd->bd_ops->drain(tty, 0);

	return;
}


/*
 * dgnc_send_xchar()
 *
 * send a high priority character, called by ld.
 */
static void dgnc_tty_send_xchar(struct tty_struct *tty, char c)
{
	struct dgnc_board *bd;
	struct channel_t *ch;
	struct un_t *un;
	unsigned long flags;

	if (!tty || tty->magic != TTY_MAGIC)
		return;

	un = tty->driver_data;
	if (!un || un->magic != DGNC_UNIT_MAGIC)
		return;

	ch = un->un_ch;
	if (!ch || ch->magic != DGNC_CHANNEL_MAGIC)
		return;

	bd = ch->ch_bd;
	if (!bd || bd->magic != DGNC_BOARD_MAGIC)
		return;

	dev_dbg(tty->dev, "dgnc_tty_send_xchar start\n");

	spin_lock_irqsave(&ch->ch_lock, flags);
	bd->bd_ops->send_immediate_char(ch, c);
	spin_unlock_irqrestore(&ch->ch_lock, flags);

	dev_dbg(tty->dev, "dgnc_tty_send_xchar finish\n");
	return;
}




/*
 * Return modem signals to ld.
 */
static inline int dgnc_get_mstat(struct channel_t *ch)
{
	unsigned char mstat;
	int result = -EIO;
	unsigned long flags;

	if (!ch || ch->magic != DGNC_CHANNEL_MAGIC)
		return -ENXIO;

	spin_lock_irqsave(&ch->ch_lock, flags);

	mstat = (ch->ch_mostat | ch->ch_mistat);

	spin_unlock_irqrestore(&ch->ch_lock, flags);

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

	return result;
}



/*
 * Return modem signals to ld.
 */
static int dgnc_get_modem_info(struct channel_t *ch, unsigned int  __user *value)
{
	int result;

	if (!ch || ch->magic != DGNC_CHANNEL_MAGIC)
		return -ENXIO;

	result = dgnc_get_mstat(ch);

	if (result < 0)
		return -ENXIO;

	return put_user(result, value);
}


/*
 * dgnc_set_modem_info()
 *
 * Set modem signals, called by ld.
 */
static int dgnc_set_modem_info(struct tty_struct *tty, unsigned int command, unsigned int __user *value)
{
	struct dgnc_board *bd;
	struct channel_t *ch;
	struct un_t *un;
	int ret = -ENXIO;
	unsigned int arg = 0;
	unsigned long flags;

	if (!tty || tty->magic != TTY_MAGIC)
		return ret;

	un = tty->driver_data;
	if (!un || un->magic != DGNC_UNIT_MAGIC)
		return ret;

	ch = un->un_ch;
	if (!ch || ch->magic != DGNC_CHANNEL_MAGIC)
		return ret;

	bd = ch->ch_bd;
	if (!bd || bd->magic != DGNC_BOARD_MAGIC)
		return ret;

	ret = 0;

	ret = get_user(arg, value);
	if (ret)
		return ret;

	switch (command) {
	case TIOCMBIS:
		if (arg & TIOCM_RTS)
			ch->ch_mostat |= UART_MCR_RTS;

		if (arg & TIOCM_DTR)
			ch->ch_mostat |= UART_MCR_DTR;

		break;

	case TIOCMBIC:
		if (arg & TIOCM_RTS)
			ch->ch_mostat &= ~(UART_MCR_RTS);

		if (arg & TIOCM_DTR)
			ch->ch_mostat &= ~(UART_MCR_DTR);

		break;

	case TIOCMSET:

		if (arg & TIOCM_RTS)
			ch->ch_mostat |= UART_MCR_RTS;
		else
			ch->ch_mostat &= ~(UART_MCR_RTS);

		if (arg & TIOCM_DTR)
			ch->ch_mostat |= UART_MCR_DTR;
		else
			ch->ch_mostat &= ~(UART_MCR_DTR);

		break;

	default:
		return -EINVAL;
	}

	spin_lock_irqsave(&ch->ch_lock, flags);

	ch->ch_bd->bd_ops->assert_modem_signals(ch);

	spin_unlock_irqrestore(&ch->ch_lock, flags);

	return 0;
}


/*
 * dgnc_tty_digigeta()
 *
 * Ioctl to get the information for ditty.
 *
 *
 *
 */
static int dgnc_tty_digigeta(struct tty_struct *tty, struct digi_t __user *retinfo)
{
	struct channel_t *ch;
	struct un_t *un;
	struct digi_t tmp;
	unsigned long flags;

	if (!retinfo)
		return -EFAULT;

	if (!tty || tty->magic != TTY_MAGIC)
		return -EFAULT;

	un = tty->driver_data;
	if (!un || un->magic != DGNC_UNIT_MAGIC)
		return -EFAULT;

	ch = un->un_ch;
	if (!ch || ch->magic != DGNC_CHANNEL_MAGIC)
		return -EFAULT;

	memset(&tmp, 0, sizeof(tmp));

	spin_lock_irqsave(&ch->ch_lock, flags);
	memcpy(&tmp, &ch->ch_digi, sizeof(tmp));
	spin_unlock_irqrestore(&ch->ch_lock, flags);

	if (copy_to_user(retinfo, &tmp, sizeof(*retinfo)))
		return -EFAULT;

	return 0;
}


/*
 * dgnc_tty_digiseta()
 *
 * Ioctl to set the information for ditty.
 *
 *
 *
 */
static int dgnc_tty_digiseta(struct tty_struct *tty, struct digi_t __user *new_info)
{
	struct dgnc_board *bd;
	struct channel_t *ch;
	struct un_t *un;
	struct digi_t new_digi;
	unsigned long flags;

	if (!tty || tty->magic != TTY_MAGIC)
		return -EFAULT;

	un = tty->driver_data;
	if (!un || un->magic != DGNC_UNIT_MAGIC)
		return -EFAULT;

	ch = un->un_ch;
	if (!ch || ch->magic != DGNC_CHANNEL_MAGIC)
		return -EFAULT;

	bd = ch->ch_bd;
	if (!bd || bd->magic != DGNC_BOARD_MAGIC)
		return -EFAULT;

	if (copy_from_user(&new_digi, new_info, sizeof(new_digi)))
		return -EFAULT;

	spin_lock_irqsave(&ch->ch_lock, flags);

	/*
	 * Handle transistions to and from RTS Toggle.
	 */
	if (!(ch->ch_digi.digi_flags & DIGI_RTS_TOGGLE) && (new_digi.digi_flags & DIGI_RTS_TOGGLE))
		ch->ch_mostat &= ~(UART_MCR_RTS);
	if ((ch->ch_digi.digi_flags & DIGI_RTS_TOGGLE) && !(new_digi.digi_flags & DIGI_RTS_TOGGLE))
		ch->ch_mostat |= (UART_MCR_RTS);

	/*
	 * Handle transistions to and from DTR Toggle.
	 */
	if (!(ch->ch_digi.digi_flags & DIGI_DTR_TOGGLE) && (new_digi.digi_flags & DIGI_DTR_TOGGLE))
		ch->ch_mostat &= ~(UART_MCR_DTR);
	if ((ch->ch_digi.digi_flags & DIGI_DTR_TOGGLE) && !(new_digi.digi_flags & DIGI_DTR_TOGGLE))
		ch->ch_mostat |= (UART_MCR_DTR);

	memcpy(&ch->ch_digi, &new_digi, sizeof(new_digi));

	if (ch->ch_digi.digi_maxcps < 1)
		ch->ch_digi.digi_maxcps = 1;

	if (ch->ch_digi.digi_maxcps > 10000)
		ch->ch_digi.digi_maxcps = 10000;

	if (ch->ch_digi.digi_bufsize < 10)
		ch->ch_digi.digi_bufsize = 10;

	if (ch->ch_digi.digi_maxchar < 1)
		ch->ch_digi.digi_maxchar = 1;

	if (ch->ch_digi.digi_maxchar > ch->ch_digi.digi_bufsize)
		ch->ch_digi.digi_maxchar = ch->ch_digi.digi_bufsize;

	if (ch->ch_digi.digi_onlen > DIGI_PLEN)
		ch->ch_digi.digi_onlen = DIGI_PLEN;

	if (ch->ch_digi.digi_offlen > DIGI_PLEN)
		ch->ch_digi.digi_offlen = DIGI_PLEN;

	ch->ch_bd->bd_ops->param(tty);

	spin_unlock_irqrestore(&ch->ch_lock, flags);

	return 0;
}


/*
 * dgnc_set_termios()
 */
static void dgnc_tty_set_termios(struct tty_struct *tty, struct ktermios *old_termios)
{
	struct dgnc_board *bd;
	struct channel_t *ch;
	struct un_t *un;
	unsigned long flags;

	if (!tty || tty->magic != TTY_MAGIC)
		return;

	un = tty->driver_data;
	if (!un || un->magic != DGNC_UNIT_MAGIC)
		return;

	ch = un->un_ch;
	if (!ch || ch->magic != DGNC_CHANNEL_MAGIC)
		return;

	bd = ch->ch_bd;
	if (!bd || bd->magic != DGNC_BOARD_MAGIC)
		return;

	spin_lock_irqsave(&ch->ch_lock, flags);

	ch->ch_c_cflag   = tty->termios.c_cflag;
	ch->ch_c_iflag   = tty->termios.c_iflag;
	ch->ch_c_oflag   = tty->termios.c_oflag;
	ch->ch_c_lflag   = tty->termios.c_lflag;
	ch->ch_startc = tty->termios.c_cc[VSTART];
	ch->ch_stopc  = tty->termios.c_cc[VSTOP];

	ch->ch_bd->bd_ops->param(tty);
	dgnc_carrier(ch);

	spin_unlock_irqrestore(&ch->ch_lock, flags);
}


static void dgnc_tty_throttle(struct tty_struct *tty)
{
	struct channel_t *ch;
	struct un_t *un;
	unsigned long flags;

	if (!tty || tty->magic != TTY_MAGIC)
		return;

	un = tty->driver_data;
	if (!un || un->magic != DGNC_UNIT_MAGIC)
		return;

	ch = un->un_ch;
	if (!ch || ch->magic != DGNC_CHANNEL_MAGIC)
		return;

	spin_lock_irqsave(&ch->ch_lock, flags);

	ch->ch_flags |= (CH_FORCED_STOPI);

	spin_unlock_irqrestore(&ch->ch_lock, flags);
}


static void dgnc_tty_unthrottle(struct tty_struct *tty)
{
	struct channel_t *ch;
	struct un_t *un;
	unsigned long flags;

	if (!tty || tty->magic != TTY_MAGIC)
		return;

	un = tty->driver_data;
	if (!un || un->magic != DGNC_UNIT_MAGIC)
		return;

	ch = un->un_ch;
	if (!ch || ch->magic != DGNC_CHANNEL_MAGIC)
		return;

	spin_lock_irqsave(&ch->ch_lock, flags);

	ch->ch_flags &= ~(CH_FORCED_STOPI);

	spin_unlock_irqrestore(&ch->ch_lock, flags);
}


static void dgnc_tty_start(struct tty_struct *tty)
{
	struct dgnc_board *bd;
	struct channel_t *ch;
	struct un_t *un;
	unsigned long flags;

	if (!tty || tty->magic != TTY_MAGIC)
		return;

	un = tty->driver_data;
	if (!un || un->magic != DGNC_UNIT_MAGIC)
		return;

	ch = un->un_ch;
	if (!ch || ch->magic != DGNC_CHANNEL_MAGIC)
		return;

	bd = ch->ch_bd;
	if (!bd || bd->magic != DGNC_BOARD_MAGIC)
		return;

	spin_lock_irqsave(&ch->ch_lock, flags);

	ch->ch_flags &= ~(CH_FORCED_STOP);

	spin_unlock_irqrestore(&ch->ch_lock, flags);
}


static void dgnc_tty_stop(struct tty_struct *tty)
{
	struct dgnc_board *bd;
	struct channel_t *ch;
	struct un_t *un;
	unsigned long flags;

	if (!tty || tty->magic != TTY_MAGIC)
		return;

	un = tty->driver_data;
	if (!un || un->magic != DGNC_UNIT_MAGIC)
		return;

	ch = un->un_ch;
	if (!ch || ch->magic != DGNC_CHANNEL_MAGIC)
		return;

	bd = ch->ch_bd;
	if (!bd || bd->magic != DGNC_BOARD_MAGIC)
		return;

	spin_lock_irqsave(&ch->ch_lock, flags);

	ch->ch_flags |= (CH_FORCED_STOP);

	spin_unlock_irqrestore(&ch->ch_lock, flags);
}


/*
 * dgnc_tty_flush_chars()
 *
 * Flush the cook buffer
 *
 * Note to self, and any other poor souls who venture here:
 *
 * flush in this case DOES NOT mean dispose of the data.
 * instead, it means "stop buffering and send it if you
 * haven't already."  Just guess how I figured that out...   SRW 2-Jun-98
 *
 * It is also always called in interrupt context - JAR 8-Sept-99
 */
static void dgnc_tty_flush_chars(struct tty_struct *tty)
{
	struct dgnc_board *bd;
	struct channel_t *ch;
	struct un_t *un;
	unsigned long flags;

	if (!tty || tty->magic != TTY_MAGIC)
		return;

	un = tty->driver_data;
	if (!un || un->magic != DGNC_UNIT_MAGIC)
		return;

	ch = un->un_ch;
	if (!ch || ch->magic != DGNC_CHANNEL_MAGIC)
		return;

	bd = ch->ch_bd;
	if (!bd || bd->magic != DGNC_BOARD_MAGIC)
		return;

	spin_lock_irqsave(&ch->ch_lock, flags);

	/* Do something maybe here */

	spin_unlock_irqrestore(&ch->ch_lock, flags);
}



/*
 * dgnc_tty_flush_buffer()
 *
 * Flush Tx buffer (make in == out)
 */
static void dgnc_tty_flush_buffer(struct tty_struct *tty)
{
	struct channel_t *ch;
	struct un_t *un;
	unsigned long flags;

	if (!tty || tty->magic != TTY_MAGIC)
		return;

	un = tty->driver_data;
	if (!un || un->magic != DGNC_UNIT_MAGIC)
		return;

	ch = un->un_ch;
	if (!ch || ch->magic != DGNC_CHANNEL_MAGIC)
		return;

	spin_lock_irqsave(&ch->ch_lock, flags);

	ch->ch_flags &= ~CH_STOP;

	/* Flush our write queue */
	ch->ch_w_head = ch->ch_w_tail;

	/* Flush UARTs transmit FIFO */
	ch->ch_bd->bd_ops->flush_uart_write(ch);

	if (ch->ch_tun.un_flags & (UN_LOW|UN_EMPTY)) {
		ch->ch_tun.un_flags &= ~(UN_LOW|UN_EMPTY);
		wake_up_interruptible(&ch->ch_tun.un_flags_wait);
	}
	if (ch->ch_pun.un_flags & (UN_LOW|UN_EMPTY)) {
		ch->ch_pun.un_flags &= ~(UN_LOW|UN_EMPTY);
		wake_up_interruptible(&ch->ch_pun.un_flags_wait);
	}

	spin_unlock_irqrestore(&ch->ch_lock, flags);
}



/*****************************************************************************
 *
 * The IOCTL function and all of its helpers
 *
 *****************************************************************************/

/*
 * dgnc_tty_ioctl()
 *
 * The usual assortment of ioctl's
 */
static int dgnc_tty_ioctl(struct tty_struct *tty, unsigned int cmd,
		unsigned long arg)
{
	struct dgnc_board *bd;
	struct channel_t *ch;
	struct un_t *un;
	int rc;
	unsigned long flags;
	void __user *uarg = (void __user *) arg;

	if (!tty || tty->magic != TTY_MAGIC)
		return -ENODEV;

	un = tty->driver_data;
	if (!un || un->magic != DGNC_UNIT_MAGIC)
		return -ENODEV;

	ch = un->un_ch;
	if (!ch || ch->magic != DGNC_CHANNEL_MAGIC)
		return -ENODEV;

	bd = ch->ch_bd;
	if (!bd || bd->magic != DGNC_BOARD_MAGIC)
		return -ENODEV;

	spin_lock_irqsave(&ch->ch_lock, flags);

	if (un->un_open_count <= 0) {
		spin_unlock_irqrestore(&ch->ch_lock, flags);
		return -EIO;
	}

	switch (cmd) {

	/* Here are all the standard ioctl's that we MUST implement */

	case TCSBRK:
		/*
		 * TCSBRK is SVID version: non-zero arg --> no break
		 * this behaviour is exploited by tcdrain().
		 *
		 * According to POSIX.1 spec (7.2.2.1.2) breaks should be
		 * between 0.25 and 0.5 seconds so we'll ask for something
		 * in the middle: 0.375 seconds.
		 */
		rc = tty_check_change(tty);
		spin_unlock_irqrestore(&ch->ch_lock, flags);
		if (rc)
			return rc;

		rc = ch->ch_bd->bd_ops->drain(tty, 0);

		if (rc)
			return -EINTR;

		spin_lock_irqsave(&ch->ch_lock, flags);

		if (((cmd == TCSBRK) && (!arg)) || (cmd == TCSBRKP)) {
			ch->ch_bd->bd_ops->send_break(ch, 250);
		}

		spin_unlock_irqrestore(&ch->ch_lock, flags);

		return 0;


	case TCSBRKP:
		/* support for POSIX tcsendbreak()
		 * According to POSIX.1 spec (7.2.2.1.2) breaks should be
		 * between 0.25 and 0.5 seconds so we'll ask for something
		 * in the middle: 0.375 seconds.
		 */
		rc = tty_check_change(tty);
		spin_unlock_irqrestore(&ch->ch_lock, flags);
		if (rc)
			return rc;

		rc = ch->ch_bd->bd_ops->drain(tty, 0);
		if (rc)
			return -EINTR;

		spin_lock_irqsave(&ch->ch_lock, flags);

		ch->ch_bd->bd_ops->send_break(ch, 250);

		spin_unlock_irqrestore(&ch->ch_lock, flags);

		return 0;

	case TIOCSBRK:
		rc = tty_check_change(tty);
		spin_unlock_irqrestore(&ch->ch_lock, flags);
		if (rc)
			return rc;

		rc = ch->ch_bd->bd_ops->drain(tty, 0);
		if (rc)
			return -EINTR;

		spin_lock_irqsave(&ch->ch_lock, flags);

		ch->ch_bd->bd_ops->send_break(ch, 250);

		spin_unlock_irqrestore(&ch->ch_lock, flags);

		return 0;

	case TIOCCBRK:
		/* Do Nothing */
		spin_unlock_irqrestore(&ch->ch_lock, flags);
		return 0;

	case TIOCGSOFTCAR:

		spin_unlock_irqrestore(&ch->ch_lock, flags);

		rc = put_user(C_CLOCAL(tty) ? 1 : 0, (unsigned long __user *) arg);
		return rc;

	case TIOCSSOFTCAR:

		spin_unlock_irqrestore(&ch->ch_lock, flags);
		rc = get_user(arg, (unsigned long __user *) arg);
		if (rc)
			return rc;

		spin_lock_irqsave(&ch->ch_lock, flags);
		tty->termios.c_cflag = ((tty->termios.c_cflag & ~CLOCAL) | (arg ? CLOCAL : 0));
		ch->ch_bd->bd_ops->param(tty);
		spin_unlock_irqrestore(&ch->ch_lock, flags);

		return 0;

	case TIOCMGET:
		spin_unlock_irqrestore(&ch->ch_lock, flags);
		return dgnc_get_modem_info(ch, uarg);

	case TIOCMBIS:
	case TIOCMBIC:
	case TIOCMSET:
		spin_unlock_irqrestore(&ch->ch_lock, flags);
		return dgnc_set_modem_info(tty, cmd, uarg);

		/*
		 * Here are any additional ioctl's that we want to implement
		 */

	case TCFLSH:
		/*
		 * The linux tty driver doesn't have a flush
		 * input routine for the driver, assuming all backed
		 * up data is in the line disc. buffers.  However,
		 * we all know that's not the case.  Here, we
		 * act on the ioctl, but then lie and say we didn't
		 * so the line discipline will process the flush
		 * also.
		 */
		rc = tty_check_change(tty);
		if (rc) {
			spin_unlock_irqrestore(&ch->ch_lock, flags);
			return rc;
		}

		if ((arg == TCIFLUSH) || (arg == TCIOFLUSH)) {
			ch->ch_r_head = ch->ch_r_tail;
			ch->ch_bd->bd_ops->flush_uart_read(ch);
			/* Force queue flow control to be released, if needed */
			dgnc_check_queue_flow_control(ch);
		}

		if ((arg == TCOFLUSH) || (arg == TCIOFLUSH)) {
			if (!(un->un_type == DGNC_PRINT)) {
				ch->ch_w_head = ch->ch_w_tail;
				ch->ch_bd->bd_ops->flush_uart_write(ch);

				if (ch->ch_tun.un_flags & (UN_LOW|UN_EMPTY)) {
					ch->ch_tun.un_flags &= ~(UN_LOW|UN_EMPTY);
					wake_up_interruptible(&ch->ch_tun.un_flags_wait);
				}

				if (ch->ch_pun.un_flags & (UN_LOW|UN_EMPTY)) {
					ch->ch_pun.un_flags &= ~(UN_LOW|UN_EMPTY);
					wake_up_interruptible(&ch->ch_pun.un_flags_wait);
				}

			}
		}

		/* pretend we didn't recognize this IOCTL */
		spin_unlock_irqrestore(&ch->ch_lock, flags);
		return -ENOIOCTLCMD;
	case TCSETSF:
	case TCSETSW:
		/*
		 * The linux tty driver doesn't have a flush
		 * input routine for the driver, assuming all backed
		 * up data is in the line disc. buffers.  However,
		 * we all know that's not the case.  Here, we
		 * act on the ioctl, but then lie and say we didn't
		 * so the line discipline will process the flush
		 * also.
		 */
		if (cmd == TCSETSF) {
			/* flush rx */
			ch->ch_flags &= ~CH_STOP;
			ch->ch_r_head = ch->ch_r_tail;
			ch->ch_bd->bd_ops->flush_uart_read(ch);
			/* Force queue flow control to be released, if needed */
			dgnc_check_queue_flow_control(ch);
		}

		/* now wait for all the output to drain */
		spin_unlock_irqrestore(&ch->ch_lock, flags);
		rc = ch->ch_bd->bd_ops->drain(tty, 0);
		if (rc)
			return -EINTR;

		/* pretend we didn't recognize this */
		return -ENOIOCTLCMD;

	case TCSETAW:

		spin_unlock_irqrestore(&ch->ch_lock, flags);
		rc = ch->ch_bd->bd_ops->drain(tty, 0);
		if (rc)
			return -EINTR;

		/* pretend we didn't recognize this */
		return -ENOIOCTLCMD;

	case TCXONC:
		spin_unlock_irqrestore(&ch->ch_lock, flags);
		/* Make the ld do it */
		return -ENOIOCTLCMD;

	case DIGI_GETA:
		/* get information for ditty */
		spin_unlock_irqrestore(&ch->ch_lock, flags);
		return dgnc_tty_digigeta(tty, uarg);

	case DIGI_SETAW:
	case DIGI_SETAF:

		/* set information for ditty */
		if (cmd == (DIGI_SETAW)) {

			spin_unlock_irqrestore(&ch->ch_lock, flags);
			rc = ch->ch_bd->bd_ops->drain(tty, 0);

			if (rc)
				return -EINTR;

			spin_lock_irqsave(&ch->ch_lock, flags);
		} else {
			tty_ldisc_flush(tty);
		}
		/* fall thru */

	case DIGI_SETA:
		spin_unlock_irqrestore(&ch->ch_lock, flags);
		return dgnc_tty_digiseta(tty, uarg);

	case DIGI_LOOPBACK:
		{
			uint loopback = 0;
			/* Let go of locks when accessing user space, could sleep */
			spin_unlock_irqrestore(&ch->ch_lock, flags);
			rc = get_user(loopback, (unsigned int __user *) arg);
			if (rc)
				return rc;
			spin_lock_irqsave(&ch->ch_lock, flags);

			/* Enable/disable internal loopback for this port */
			if (loopback)
				ch->ch_flags |= CH_LOOPBACK;
			else
				ch->ch_flags &= ~(CH_LOOPBACK);

			ch->ch_bd->bd_ops->param(tty);
			spin_unlock_irqrestore(&ch->ch_lock, flags);
			return 0;
		}

	case DIGI_GETCUSTOMBAUD:
		spin_unlock_irqrestore(&ch->ch_lock, flags);
		rc = put_user(ch->ch_custom_speed, (unsigned int __user *) arg);
		return rc;

	case DIGI_SETCUSTOMBAUD:
	{
		int new_rate;
		/* Let go of locks when accessing user space, could sleep */
		spin_unlock_irqrestore(&ch->ch_lock, flags);
		rc = get_user(new_rate, (int __user *) arg);
		if (rc)
			return rc;
		spin_lock_irqsave(&ch->ch_lock, flags);
		dgnc_set_custom_speed(ch, new_rate);
		ch->ch_bd->bd_ops->param(tty);
		spin_unlock_irqrestore(&ch->ch_lock, flags);
		return 0;
	}

	/*
	 * This ioctl allows insertion of a character into the front
	 * of any pending data to be transmitted.
	 *
	 * This ioctl is to satify the "Send Character Immediate"
	 * call that the RealPort protocol spec requires.
	 */
	case DIGI_REALPORT_SENDIMMEDIATE:
	{
		unsigned char c;
		spin_unlock_irqrestore(&ch->ch_lock, flags);
		rc = get_user(c, (unsigned char __user *) arg);
		if (rc)
			return rc;
		spin_lock_irqsave(&ch->ch_lock, flags);
		ch->ch_bd->bd_ops->send_immediate_char(ch, c);
		spin_unlock_irqrestore(&ch->ch_lock, flags);
		return 0;
	}

	/*
	 * This ioctl returns all the current counts for the port.
	 *
	 * This ioctl is to satify the "Line Error Counters"
	 * call that the RealPort protocol spec requires.
	 */
	case DIGI_REALPORT_GETCOUNTERS:
	{
		struct digi_getcounter buf;

		buf.norun = ch->ch_err_overrun;
		buf.noflow = 0;  	/* The driver doesn't keep this stat */
		buf.nframe = ch->ch_err_frame;
		buf.nparity = ch->ch_err_parity;
		buf.nbreak = ch->ch_err_break;
		buf.rbytes = ch->ch_rxcount;
		buf.tbytes = ch->ch_txcount;

		spin_unlock_irqrestore(&ch->ch_lock, flags);

		if (copy_to_user(uarg, &buf, sizeof(buf)))
			return -EFAULT;

		return 0;
	}

	/*
	 * This ioctl returns all current events.
	 *
	 * This ioctl is to satify the "Event Reporting"
	 * call that the RealPort protocol spec requires.
	 */
	case DIGI_REALPORT_GETEVENTS:
	{
		unsigned int events = 0;

		/* NOTE: MORE EVENTS NEEDS TO BE ADDED HERE */
		if (ch->ch_flags & CH_BREAK_SENDING)
			events |= EV_TXB;
		if ((ch->ch_flags & CH_STOP) || (ch->ch_flags & CH_FORCED_STOP))
			events |= (EV_OPU | EV_OPS);

		if ((ch->ch_flags & CH_STOPI) || (ch->ch_flags & CH_FORCED_STOPI)) {
			events |= (EV_IPU | EV_IPS);
		}

		spin_unlock_irqrestore(&ch->ch_lock, flags);
		rc = put_user(events, (unsigned int __user *) arg);
		return rc;
	}

	/*
	 * This ioctl returns TOUT and TIN counters based
	 * upon the values passed in by the RealPort Server.
	 * It also passes back whether the UART Transmitter is
	 * empty as well.
	 */
	case DIGI_REALPORT_GETBUFFERS:
	{
		struct digi_getbuffer buf;
		int tdist;
		int count;

		spin_unlock_irqrestore(&ch->ch_lock, flags);

		/*
		 * Get data from user first.
		 */
		if (copy_from_user(&buf, uarg, sizeof(buf)))
			return -EFAULT;

		spin_lock_irqsave(&ch->ch_lock, flags);

		/*
		 * Figure out how much data is in our RX and TX queues.
		 */
		buf.rxbuf = (ch->ch_r_head - ch->ch_r_tail) & RQUEUEMASK;
		buf.txbuf = (ch->ch_w_head - ch->ch_w_tail) & WQUEUEMASK;

		/*
		 * Is the UART empty? Add that value to whats in our TX queue.
		 */
		count = buf.txbuf + ch->ch_bd->bd_ops->get_uart_bytes_left(ch);

		/*
		 * Figure out how much data the RealPort Server believes should
		 * be in our TX queue.
		 */
		tdist = (buf.tIn - buf.tOut) & 0xffff;

		/*
		 * If we have more data than the RealPort Server believes we
		 * should have, reduce our count to its amount.
		 *
		 * This count difference CAN happen because the Linux LD can
		 * insert more characters into our queue for OPOST processing
		 * that the RealPort Server doesn't know about.
		 */
		if (buf.txbuf > tdist)
			buf.txbuf = tdist;

		/*
		 * Report whether our queue and UART TX are completely empty.
		 */
		if (count)
			buf.txdone = 0;
		else
			buf.txdone = 1;

		spin_unlock_irqrestore(&ch->ch_lock, flags);

		if (copy_to_user(uarg, &buf, sizeof(buf)))
			return -EFAULT;

		return 0;
	}
	default:
		spin_unlock_irqrestore(&ch->ch_lock, flags);

		return -ENOIOCTLCMD;
	}
}
