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
 * FEP5 based product lines.
 * 
 ************************************************************************
 *
 * $Id: dgap_tty.c,v 1.3 2011/06/23 12:11:31 markh Exp $
 */

#include <linux/kernel.h>
#include <linux/version.h>
#include <linux/sched.h>	/* For jiffies, task states */
#include <linux/interrupt.h>	/* For tasklet and interrupt structs/defines */
#include <linux/module.h>
#include <linux/ctype.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/serial_reg.h>
#include <linux/slab.h>
#include <linux/delay.h>	/* For udelay */
#include <asm/uaccess.h>	/* For copy_from_user/copy_to_user */
#include <asm/io.h>		/* For read[bwl]/write[bwl] */
#include <linux/pci.h>

#include "dgap_driver.h"
#include "dgap_tty.h"
#include "dgap_types.h"
#include "dgap_fep5.h"
#include "dgap_parse.h"
#include "dgap_conf.h"
#include "dgap_sysfs.h"

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,37)
#define init_MUTEX(sem)         sema_init(sem, 1)
#define DECLARE_MUTEX(name)     \
        struct semaphore name = __SEMAPHORE_INITIALIZER(name, 1)
#endif

/*
 * internal variables
 */
static struct board_t	*dgap_BoardsByMajor[256];
static uchar		*dgap_TmpWriteBuf = NULL;
static DECLARE_MUTEX(dgap_TmpWriteSem);

/*
 * Default transparent print information.
 */
static struct digi_t dgap_digi_init = {
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

static struct ktermios DgapDefaultTermios =
{
	.c_iflag =	(DEFAULT_IFLAGS),	/* iflags */
	.c_oflag =	(DEFAULT_OFLAGS),	/* oflags */
	.c_cflag =	(DEFAULT_CFLAGS),	/* cflags */
	.c_lflag =	(DEFAULT_LFLAGS),	/* lflags */
	.c_cc =		INIT_C_CC,
	.c_line = 	0,
};

/* Our function prototypes */
static int dgap_tty_open(struct tty_struct *tty, struct file *file);
static void dgap_tty_close(struct tty_struct *tty, struct file *file);
static int dgap_block_til_ready(struct tty_struct *tty, struct file *file, struct channel_t *ch);
static int dgap_tty_ioctl(struct tty_struct *tty, struct file *file, unsigned int cmd, unsigned long arg);
static int dgap_tty_digigeta(struct tty_struct *tty, struct digi_t __user *retinfo);
static int dgap_tty_digiseta(struct tty_struct *tty, struct digi_t __user *new_info);
static int dgap_tty_digigetedelay(struct tty_struct *tty, int __user *retinfo);
static int dgap_tty_digisetedelay(struct tty_struct *tty, int __user *new_info);
static int dgap_tty_write_room(struct tty_struct* tty);
static int dgap_tty_chars_in_buffer(struct tty_struct* tty);
static void dgap_tty_start(struct tty_struct *tty);
static void dgap_tty_stop(struct tty_struct *tty);
static void dgap_tty_throttle(struct tty_struct *tty);
static void dgap_tty_unthrottle(struct tty_struct *tty);
static void dgap_tty_flush_chars(struct tty_struct *tty);
static void dgap_tty_flush_buffer(struct tty_struct *tty);
static void dgap_tty_hangup(struct tty_struct *tty);
static int dgap_wait_for_drain(struct tty_struct *tty);
static int dgap_set_modem_info(struct tty_struct *tty, unsigned int command, unsigned int __user *value);
static int dgap_get_modem_info(struct channel_t *ch, unsigned int __user *value);
static int dgap_tty_digisetcustombaud(struct tty_struct *tty, int __user *new_info);
static int dgap_tty_digigetcustombaud(struct tty_struct *tty, int __user *retinfo);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,39)
static int dgap_tty_tiocmget(struct tty_struct *tty);
static int dgap_tty_tiocmset(struct tty_struct *tty, unsigned int set, unsigned int clear);
#else
static int dgap_tty_tiocmget(struct tty_struct *tty, struct file *file);
static int dgap_tty_tiocmset(struct tty_struct *tty, struct file *file, unsigned int set, unsigned int clear);
#endif
static int dgap_tty_send_break(struct tty_struct *tty, int msec);
static void dgap_tty_wait_until_sent(struct tty_struct *tty, int timeout);
static int dgap_tty_write(struct tty_struct *tty, const unsigned char *buf, int count);
static void dgap_tty_set_termios(struct tty_struct *tty, struct ktermios *old_termios);
static int dgap_tty_put_char(struct tty_struct *tty, unsigned char c);
static void dgap_tty_send_xchar(struct tty_struct *tty, char ch);

static const struct tty_operations dgap_tty_ops = {
	.open = dgap_tty_open,
	.close = dgap_tty_close,
	.write = dgap_tty_write,
	.write_room = dgap_tty_write_room,
	.flush_buffer = dgap_tty_flush_buffer,
	.chars_in_buffer = dgap_tty_chars_in_buffer,
	.flush_chars = dgap_tty_flush_chars,
	.ioctl = dgap_tty_ioctl,
	.set_termios = dgap_tty_set_termios,
	.stop = dgap_tty_stop, 
	.start = dgap_tty_start,
	.throttle = dgap_tty_throttle,
	.unthrottle = dgap_tty_unthrottle,
	.hangup = dgap_tty_hangup,
	.put_char = dgap_tty_put_char,
	.tiocmget = dgap_tty_tiocmget,
	.tiocmset = dgap_tty_tiocmset,
	.break_ctl = dgap_tty_send_break,
	.wait_until_sent = dgap_tty_wait_until_sent,
	.send_xchar = dgap_tty_send_xchar
};





/************************************************************************
 *                      
 * TTY Initialization/Cleanup Functions
 *      
 ************************************************************************/
         
/*
 * dgap_tty_preinit()
 *
 * Initialize any global tty related data before we download any boards.
 */
int dgap_tty_preinit(void)
{
	unsigned long flags;

	DGAP_LOCK(dgap_global_lock, flags);  

	/*
	 * Allocate a buffer for doing the copy from user space to
	 * kernel space in dgap_input().  We only use one buffer and
	 * control access to it with a semaphore.  If we are paging, we
	 * are already in trouble so one buffer won't hurt much anyway.
	 */
	dgap_TmpWriteBuf = kmalloc(WRITEBUFLEN, GFP_ATOMIC);

	if (!dgap_TmpWriteBuf) {
		DGAP_UNLOCK(dgap_global_lock, flags);
		DPR_INIT(("unable to allocate tmp write buf"));
		return (-ENOMEM);
	}
         
        DGAP_UNLOCK(dgap_global_lock, flags);
        return(0);
}


/*
 * dgap_tty_register()
 *
 * Init the tty subsystem for this board.
 */
int dgap_tty_register(struct board_t *brd)
{
	int rc = 0;

	DPR_INIT(("tty_register start"));

	brd->SerialDriver = alloc_tty_driver(MAXPORTS);

	snprintf(brd->SerialName, MAXTTYNAMELEN, "tty_dgap_%d_", brd->boardnum);
	brd->SerialDriver->name = brd->SerialName;
	brd->SerialDriver->name_base = 0;
	brd->SerialDriver->major = 0;
	brd->SerialDriver->minor_start = 0;
	brd->SerialDriver->type = TTY_DRIVER_TYPE_SERIAL; 
	brd->SerialDriver->subtype = SERIAL_TYPE_NORMAL;   
	brd->SerialDriver->init_termios = DgapDefaultTermios;
	brd->SerialDriver->driver_name = DRVSTR;
	brd->SerialDriver->flags = (TTY_DRIVER_REAL_RAW | TTY_DRIVER_DYNAMIC_DEV | TTY_DRIVER_HARDWARE_BREAK);

	/* The kernel wants space to store pointers to tty_structs */
	brd->SerialDriver->ttys = dgap_driver_kzmalloc(MAXPORTS * sizeof(struct tty_struct *), GFP_KERNEL);
	if (!brd->SerialDriver->ttys)
		return(-ENOMEM);

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,28)
	brd->SerialDriver->refcount = brd->TtyRefCnt;
#endif

	/*
	 * Entry points for driver.  Called by the kernel from
	 * tty_io.c and n_tty.c.
	 */
	tty_set_operations(brd->SerialDriver, &dgap_tty_ops);

	/*
	 * If we're doing transparent print, we have to do all of the above
	 * again, seperately so we don't get the LD confused about what major
	 * we are when we get into the dgap_tty_open() routine.
	 */
	brd->PrintDriver = alloc_tty_driver(MAXPORTS);

	snprintf(brd->PrintName, MAXTTYNAMELEN, "pr_dgap_%d_", brd->boardnum);
	brd->PrintDriver->name = brd->PrintName;
	brd->PrintDriver->name_base = 0;
	brd->PrintDriver->major = 0;
	brd->PrintDriver->minor_start = 0;
	brd->PrintDriver->type = TTY_DRIVER_TYPE_SERIAL;   
	brd->PrintDriver->subtype = SERIAL_TYPE_NORMAL;
	brd->PrintDriver->init_termios = DgapDefaultTermios;
	brd->PrintDriver->driver_name = DRVSTR;
	brd->PrintDriver->flags = (TTY_DRIVER_REAL_RAW | TTY_DRIVER_DYNAMIC_DEV | TTY_DRIVER_HARDWARE_BREAK);

	/* The kernel wants space to store pointers to tty_structs */
	brd->PrintDriver->ttys = dgap_driver_kzmalloc(MAXPORTS * sizeof(struct tty_struct *), GFP_KERNEL);
	if (!brd->PrintDriver->ttys)
		return(-ENOMEM);

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,28)
	brd->PrintDriver->refcount = brd->TtyRefCnt;
#endif

	/*
	 * Entry points for driver.  Called by the kernel from
	 * tty_io.c and n_tty.c.
	 */
	tty_set_operations(brd->PrintDriver, &dgap_tty_ops);

	if (!brd->dgap_Major_Serial_Registered) {
		/* Register tty devices */
		rc = tty_register_driver(brd->SerialDriver);
		if (rc < 0) {
			APR(("Can't register tty device (%d)\n", rc));
			return(rc);
		}
		brd->dgap_Major_Serial_Registered = TRUE;
		dgap_BoardsByMajor[brd->SerialDriver->major] = brd;
		brd->dgap_Serial_Major = brd->SerialDriver->major;
	}

	if (!brd->dgap_Major_TransparentPrint_Registered) {
		/* Register Transparent Print devices */
 		rc = tty_register_driver(brd->PrintDriver);
		if (rc < 0) {
			APR(("Can't register Transparent Print device (%d)\n", rc));
			return(rc);
		}
		brd->dgap_Major_TransparentPrint_Registered = TRUE;
		dgap_BoardsByMajor[brd->PrintDriver->major] = brd;
		brd->dgap_TransparentPrint_Major = brd->PrintDriver->major;
	}

	DPR_INIT(("DGAP REGISTER TTY: MAJORS: %d %d\n", brd->SerialDriver->major,
		brd->PrintDriver->major));

	return (rc);
}


/*
 * dgap_tty_init()
 *
 * Init the tty subsystem.  Called once per board after board has been
 * downloaded and init'ed.
 */
int dgap_tty_init(struct board_t *brd)
{
	int i;
	int tlw;
	uint true_count = 0;
	uchar *vaddr;
	uchar modem = 0;
	struct channel_t *ch;
	struct bs_t *bs;
	struct cm_t *cm;

	if (!brd)
		return (-ENXIO);

	DPR_INIT(("dgap_tty_init start\n"));

	/*
	 * Initialize board structure elements.
	 */

	vaddr = brd->re_map_membase;
	true_count = readw((vaddr + NCHAN));

	brd->nasync = dgap_config_get_number_of_ports(brd);

	if (!brd->nasync) {
		brd->nasync = brd->maxports;
	}

	if (brd->nasync > brd->maxports) {
		brd->nasync = brd->maxports;
	}

	if (true_count != brd->nasync) {
		if ((brd->type == PPCM) && (true_count == 64)) {
			APR(("***WARNING**** %s configured for %d ports, has %d ports.\nPlease make SURE the EBI cable running from the card\nto each EM module is plugged into EBI IN!\n",
				brd->name, brd->nasync, true_count));
		}
		else if ((brd->type == PPCM) && (true_count == 0)) {
			APR(("***WARNING**** %s configured for %d ports, has %d ports.\nPlease make SURE the EBI cable running from the card\nto each EM module is plugged into EBI IN!\n",
				brd->name, brd->nasync, true_count));
		}
		else {
			APR(("***WARNING**** %s configured for %d ports, has %d ports.\n",
				brd->name, brd->nasync, true_count));
		}

		brd->nasync = true_count;

		/* If no ports, don't bother going any further */
		if (!brd->nasync) {
			brd->state = BOARD_FAILED;
			brd->dpastatus = BD_NOFEP;
			return(-ENXIO);
		}
	}

	/*
	 * Allocate channel memory that might not have been allocated
	 * when the driver was first loaded.
	 */
	for (i = 0; i < brd->nasync; i++) {
		if (!brd->channels[i]) {
			brd->channels[i] = dgap_driver_kzmalloc(sizeof(struct channel_t), GFP_ATOMIC);
			if (!brd->channels[i]) {
				DPR_CORE(("%s:%d Unable to allocate memory for channel struct\n",
				    __FILE__, __LINE__));
			}
		}
	}

	ch = brd->channels[0];
	vaddr = brd->re_map_membase;

	bs = (struct bs_t *) ((ulong) vaddr + CHANBUF);
	cm = (struct cm_t *) ((ulong) vaddr + CMDBUF);

	brd->bd_bs = bs;

	/* Set up channel variables */
	for (i = 0; i < brd->nasync; i++, ch = brd->channels[i], bs++) {

		if (!brd->channels[i])
			continue;

		DGAP_SPINLOCK_INIT(ch->ch_lock);

		/* Store all our magic numbers */
		ch->magic = DGAP_CHANNEL_MAGIC;
		ch->ch_tun.magic = DGAP_UNIT_MAGIC;
		ch->ch_tun.un_type = DGAP_SERIAL;
		ch->ch_tun.un_ch = ch;
		ch->ch_tun.un_dev = i;

		ch->ch_pun.magic = DGAP_UNIT_MAGIC;
		ch->ch_pun.un_type = DGAP_PRINT;
		ch->ch_pun.un_ch = ch;
		ch->ch_pun.un_dev = i;

		ch->ch_vaddr = vaddr;
		ch->ch_bs = bs;
		ch->ch_cm = cm;
		ch->ch_bd = brd;
		ch->ch_portnum = i;
		ch->ch_digi = dgap_digi_init;

		/*
		 * Set up digi dsr and dcd bits based on altpin flag.
		 */
		if (dgap_config_get_altpin(brd)) {
			ch->ch_dsr	= DM_CD;
			ch->ch_cd	= DM_DSR;
			ch->ch_digi.digi_flags |= DIGI_ALTPIN;
		}
		else {
			ch->ch_cd	= DM_CD;
			ch->ch_dsr	= DM_DSR;
		}

		ch->ch_taddr = vaddr + ((ch->ch_bs->tx_seg) << 4);
		ch->ch_raddr = vaddr + ((ch->ch_bs->rx_seg) << 4);
		ch->ch_tx_win = 0;
		ch->ch_rx_win = 0;
		ch->ch_tsize = readw(&(ch->ch_bs->tx_max)) + 1;
		ch->ch_rsize = readw(&(ch->ch_bs->rx_max)) + 1;
		ch->ch_tstart = 0;
		ch->ch_rstart = 0;

		/* .25 second delay */
		ch->ch_close_delay = 250;

		/*
		 * Set queue water marks, interrupt mask,
		 * and general tty parameters. 
		 */
		ch->ch_tlw = tlw = ch->ch_tsize >= 2000 ? ((ch->ch_tsize * 5) / 8) : ch->ch_tsize / 2;

		dgap_cmdw(ch, STLOW, tlw, 0);

		dgap_cmdw(ch, SRLOW, ch->ch_rsize / 2, 0);

		dgap_cmdw(ch, SRHIGH, 7 * ch->ch_rsize / 8, 0);

		ch->ch_mistat = readb(&(ch->ch_bs->m_stat));

		init_waitqueue_head(&ch->ch_flags_wait);
		init_waitqueue_head(&ch->ch_tun.un_flags_wait);
		init_waitqueue_head(&ch->ch_pun.un_flags_wait);
		init_waitqueue_head(&ch->ch_sniff_wait);

		/* Turn on all modem interrupts for now */
		modem = (DM_CD | DM_DSR | DM_CTS | DM_RI);
		writeb(modem, &(ch->ch_bs->m_int));

		/*
		 * Set edelay to 0 if interrupts are turned on,
		 * otherwise set edelay to the usual 100.
		 */
		if (brd->intr_used)
			writew(0, &(ch->ch_bs->edelay));
		else
			writew(100, &(ch->ch_bs->edelay));
	
		writeb(1, &(ch->ch_bs->idata));
	}


	DPR_INIT(("dgap_tty_init finish\n"));

	return (0);
}


/*
 * dgap_tty_post_uninit()
 *
 * UnInitialize any global tty related data.
 */
void dgap_tty_post_uninit(void)
{
	if (dgap_TmpWriteBuf) {
		kfree(dgap_TmpWriteBuf);
		dgap_TmpWriteBuf = NULL;
	}
}


/*
 * dgap_tty_uninit()
 *
 * Uninitialize the TTY portion of this driver.  Free all memory and
 * resources. 
 */
void dgap_tty_uninit(struct board_t *brd)
{
	int i = 0;

	if (brd->dgap_Major_Serial_Registered) {
		dgap_BoardsByMajor[brd->SerialDriver->major] = NULL;
		brd->dgap_Serial_Major = 0;
		for (i = 0; i < brd->nasync; i++) {
			dgap_remove_tty_sysfs(brd->channels[i]->ch_tun.un_sysfs);
			tty_unregister_device(brd->SerialDriver, i);
		}
		tty_unregister_driver(brd->SerialDriver);
		if (brd->SerialDriver->ttys) {
			kfree(brd->SerialDriver->ttys);
			brd->SerialDriver->ttys = NULL;
		}
		put_tty_driver(brd->SerialDriver);
		brd->dgap_Major_Serial_Registered = FALSE;
	}

	if (brd->dgap_Major_TransparentPrint_Registered) {
		dgap_BoardsByMajor[brd->PrintDriver->major] = NULL;
		brd->dgap_TransparentPrint_Major = 0;
		for (i = 0; i < brd->nasync; i++) {
			dgap_remove_tty_sysfs(brd->channels[i]->ch_pun.un_sysfs);
			tty_unregister_device(brd->PrintDriver, i);
		}
		tty_unregister_driver(brd->PrintDriver);
		if (brd->PrintDriver->ttys) {
			kfree(brd->PrintDriver->ttys);
			brd->PrintDriver->ttys = NULL;
	        }
		put_tty_driver(brd->PrintDriver);
		brd->dgap_Major_TransparentPrint_Registered = FALSE;
	}
}


#define TMPBUFLEN (1024)

/*
 * dgap_sniff - Dump data out to the "sniff" buffer if the
 * proc sniff file is opened...
 */
static void dgap_sniff_nowait_nolock(struct channel_t *ch, uchar *text, uchar *buf, int len)
{
	struct timeval tv;
	int n;
	int r;
	int nbuf;
	int i;
	int tmpbuflen;
	char tmpbuf[TMPBUFLEN];
	char *p = tmpbuf;
	int too_much_data;

	/* Leave if sniff not open */
	if (!(ch->ch_sniff_flags & SNIFF_OPEN))
		return;

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
		while (nbuf > 0 && ch->ch_sniff_buf != 0) {
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
			if (n == 0) {
				return;
			}
	
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
}


/*=======================================================================
 *
 *      dgap_input - Process received data.
 * 
 *              ch      - Pointer to channel structure.
 * 
 *=======================================================================*/

void dgap_input(struct channel_t *ch)
{
	struct board_t *bd;
	struct bs_t	*bs;
	struct tty_struct *tp;
	struct tty_ldisc *ld;
	uint	rmask;
	uint	head;
	uint	tail;
	int	data_len;
	ulong	lock_flags;
	ulong   lock_flags2;
	int flip_len;
	int len = 0;
	int n = 0;
	uchar *buf;
	uchar tmpchar;
	int s = 0;

	if (!ch || ch->magic != DGAP_CHANNEL_MAGIC)
		return;

	tp = ch->ch_tun.un_tty;

	bs  = ch->ch_bs;
	if (!bs) {
		return;
	}

	bd = ch->ch_bd;
	if(!bd || bd->magic != DGAP_BOARD_MAGIC)
		return;

	DPR_READ(("dgap_input start\n"));

	DGAP_LOCK(bd->bd_lock, lock_flags);
	DGAP_LOCK(ch->ch_lock, lock_flags2);

	/* 
	 *      Figure the number of characters in the buffer.   
	 *      Exit immediately if none.
	 */

	rmask = ch->ch_rsize - 1;

	head = readw(&(bs->rx_head));
	head &= rmask;
	tail = readw(&(bs->rx_tail));
	tail &= rmask;

	data_len = (head - tail) & rmask;

	if (data_len == 0) {
		writeb(1, &(bs->idata));
		DGAP_UNLOCK(ch->ch_lock, lock_flags2);
		DGAP_UNLOCK(bd->bd_lock, lock_flags);
		DPR_READ(("No data on port %d\n", ch->ch_portnum));
		return;
	}

	/*
	 * If the device is not open, or CREAD is off, flush
	 * input data and return immediately.
	 */
	if ((bd->state != BOARD_READY) || !tp  || (tp->magic != TTY_MAGIC) ||
            !(ch->ch_tun.un_flags & UN_ISOPEN) || !(tp->termios->c_cflag & CREAD) ||
	    (ch->ch_tun.un_flags & UN_CLOSING)) {

		DPR_READ(("input. dropping %d bytes on port %d...\n", data_len, ch->ch_portnum));
		DPR_READ(("input. tp: %p tp->magic: %x MAGIC:%x ch flags: %x\n",
			tp, tp ? tp->magic : 0, TTY_MAGIC, ch->ch_tun.un_flags));
		writew(head, &(bs->rx_tail));
		writeb(1, &(bs->idata));
		DGAP_UNLOCK(ch->ch_lock, lock_flags2);
		DGAP_UNLOCK(bd->bd_lock, lock_flags);
		return;
	}

	/*
	 * If we are throttled, simply don't read any data.
	 */
	if (ch->ch_flags & CH_RXBLOCK) {
		writeb(1, &(bs->idata));
		DGAP_UNLOCK(ch->ch_lock, lock_flags2);
		DGAP_UNLOCK(bd->bd_lock, lock_flags);
		DPR_READ(("Port %d throttled, not reading any data. head: %x tail: %x\n",
			ch->ch_portnum, head, tail));
		return;
	}

	/*
	 *      Ignore oruns.
	 */
	tmpchar = readb(&(bs->orun));
	if (tmpchar) {
		ch->ch_err_overrun++;
		writeb(0, &(bs->orun));
	}

	DPR_READ(("dgap_input start 2\n"));

	/* Decide how much data we can send into the tty layer */
	if (dgap_rawreadok && tp->real_raw)
 		flip_len = MYFLIPLEN;
	else
 		flip_len = TTY_FLIPBUF_SIZE;

	/* Chop down the length, if needed */
	len = min(data_len, flip_len);
	len = min(len, (N_TTY_BUF_SIZE - 1) - tp->read_cnt);

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
			writew(head, &(bs->rx_tail));
			len = 0;
		}
	}

	if (len <= 0) {
		writeb(1, &(bs->idata));
		DGAP_UNLOCK(ch->ch_lock, lock_flags2);
		DGAP_UNLOCK(bd->bd_lock, lock_flags);
		DPR_READ(("dgap_input 1 - finish\n"));
		if (ld)
			tty_ldisc_deref(ld);
		return;
	}

	buf = ch->ch_bd->flipbuf;
	n = len;

	/*
	 * n now contains the most amount of data we can copy,
	 * bounded either by our buffer size or the amount
	 * of data the card actually has pending...
	 */
	while (n) {

		s = ((head >= tail) ? head : ch->ch_rsize) - tail;
		s = min(s, n);

		if (s <= 0)
			break;

		memcpy_fromio(buf, (char *) ch->ch_raddr + tail, s);
		dgap_sniff_nowait_nolock(ch, "USER READ", buf, s);

		tail += s;
		buf += s;

		n -= s;
		/* Flip queue if needed */
		tail &= rmask;
	}

	writew(tail, &(bs->rx_tail));
	writeb(1, &(bs->idata));
	ch->ch_rxcount += len;

	/*
	 * If we are completely raw, we don't need to go through a lot
	 * of the tty layers that exist.
	 * In this case, we take the shortest and fastest route we
	 * can to relay the data to the user.
	 *
	 * On the other hand, if we are not raw, we need to go through
	 * the tty layer, which has its API more well defined.
	 */
        if (dgap_rawreadok && tp->real_raw) {

		/* !!! WE *MUST* LET GO OF ALL LOCKS BEFORE CALLING RECEIVE BUF !!! */
		DGAP_UNLOCK(ch->ch_lock, lock_flags2);
		DGAP_UNLOCK(bd->bd_lock, lock_flags);

		DPR_READ(("dgap_input. %d real_raw len:%d calling receive_buf for buffer for board %d\n", 
			 __LINE__, len, ch->ch_bd->boardnum));
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,31)
		tp->ldisc->ops->receive_buf(tp, ch->ch_bd->flipbuf, NULL, len);
#else
		tp->ldisc.ops->receive_buf(tp, ch->ch_bd->flipbuf, NULL, len);
#endif
	}
	else {
		if (I_PARMRK(tp) || I_BRKINT(tp) || I_INPCK(tp)) {
			dgap_parity_scan(ch, ch->ch_bd->flipbuf, ch->ch_bd->flipflagbuf, &len);

			len = tty_buffer_request_room(tp->port, len);
			tty_insert_flip_string_flags(tp->port, ch->ch_bd->flipbuf,
				ch->ch_bd->flipflagbuf, len);
		}
		else {
			len = tty_buffer_request_room(tp->port, len);
			tty_insert_flip_string(tp->port, ch->ch_bd->flipbuf, len);
		}

		DGAP_UNLOCK(ch->ch_lock, lock_flags2);
		DGAP_UNLOCK(bd->bd_lock, lock_flags);

		/* Tell the tty layer its okay to "eat" the data now */
		tty_flip_buffer_push(tp->port);
	}

	if (ld)
		tty_ldisc_deref(ld);

	DPR_READ(("dgap_input - finish\n"));
}


/************************************************************************   
 * Determines when CARRIER changes state and takes appropriate
 * action. 
 ************************************************************************/
void dgap_carrier(struct channel_t *ch)
{
	struct board_t *bd;

        int virt_carrier = 0;
        int phys_carrier = 0;
 
	DPR_CARR(("dgap_carrier called...\n"));

	if (!ch || ch->magic != DGAP_CHANNEL_MAGIC)
		return;

	bd = ch->ch_bd;

	if (!bd || bd->magic != DGAP_BOARD_MAGIC)
		return;

	/* Make sure altpin is always set correctly */
	if (ch->ch_digi.digi_flags & DIGI_ALTPIN) {
		ch->ch_dsr      = DM_CD;
		ch->ch_cd       = DM_DSR;
	}
	else {
		ch->ch_dsr      = DM_DSR;
		ch->ch_cd       = DM_CD;
	}

	if (ch->ch_mistat & D_CD(ch)) {
		DPR_CARR(("mistat: %x  D_CD: %x\n", ch->ch_mistat, D_CD(ch)));
		phys_carrier = 1;
	}

	if (ch->ch_digi.digi_flags & DIGI_FORCEDCD) {
		virt_carrier = 1;
	}  

	if (ch->ch_c_cflag & CLOCAL) {
		virt_carrier = 1;
	}  


	DPR_CARR(("DCD: physical: %d virt: %d\n", phys_carrier, virt_carrier));

	/*
	 * Test for a VIRTUAL carrier transition to HIGH.
	 */
	if (((ch->ch_flags & CH_FCAR) == 0) && (virt_carrier == 1)) {

		/*
		 * When carrier rises, wake any threads waiting
		 * for carrier in the open routine.
		 */

		DPR_CARR(("carrier: virt DCD rose\n"));

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

		DPR_CARR(("carrier: physical DCD rose\n"));

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
	    (phys_carrier == 0)) 
	{

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

		if (ch->ch_tun.un_open_count > 0) {
			DPR_CARR(("Sending tty hangup\n"));
			tty_hangup(ch->ch_tun.un_tty);
		}

		if (ch->ch_pun.un_open_count > 0) { 
			DPR_CARR(("Sending pr hangup\n"));
			tty_hangup(ch->ch_pun.un_tty);
		}
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


/************************************************************************
 *      
 * TTY Entry points and helper functions
 *              
 ************************************************************************/

/*
 * dgap_tty_open()
 *
 */
static int dgap_tty_open(struct tty_struct *tty, struct file *file)
{
	struct board_t	*brd;
	struct channel_t *ch;
	struct un_t	*un;
	struct bs_t	*bs;
	uint		major = 0;
	uint		minor = 0;
	int		rc = 0;
	ulong		lock_flags;
	ulong		lock_flags2;
	u16		head;

	rc = 0;

	major = MAJOR(tty_devnum(tty));
	minor = MINOR(tty_devnum(tty));

	if (major > 255) {
		return -ENXIO;
	}

	/* Get board pointer from our array of majors we have allocated */
	brd = dgap_BoardsByMajor[major];
	if (!brd) {
		return -ENXIO;
	}

	/*
	 * If board is not yet up to a state of READY, go to
	 * sleep waiting for it to happen or they cancel the open.
	 */
	rc = wait_event_interruptible(brd->state_wait,
		(brd->state & BOARD_READY));

	if (rc) {
		return rc;
	}

	DGAP_LOCK(brd->bd_lock, lock_flags);

	/* The wait above should guarentee this cannot happen */
	if (brd->state != BOARD_READY) {
		DGAP_UNLOCK(brd->bd_lock, lock_flags);
		return -ENXIO;
	}

	/* If opened device is greater than our number of ports, bail. */
	if (MINOR(tty_devnum(tty)) > brd->nasync) {
		DGAP_UNLOCK(brd->bd_lock, lock_flags);
		return -ENXIO;
	}

	ch = brd->channels[minor];
	if (!ch) {
		DGAP_UNLOCK(brd->bd_lock, lock_flags);
		return -ENXIO;
	}

	/* Grab channel lock */
	DGAP_LOCK(ch->ch_lock, lock_flags2);

	/* Figure out our type */
	if (major == brd->dgap_Serial_Major) {
		un = &brd->channels[minor]->ch_tun;
		un->un_type = DGAP_SERIAL;
	}
	else if (major == brd->dgap_TransparentPrint_Major) {
		un = &brd->channels[minor]->ch_pun;
		un->un_type = DGAP_PRINT;
	}
	else {
		DGAP_UNLOCK(ch->ch_lock, lock_flags2);
		DGAP_UNLOCK(brd->bd_lock, lock_flags);
		DPR_OPEN(("%d Unknown TYPE!\n", __LINE__));
		return -ENXIO;
	}

	/* Store our unit into driver_data, so we always have it available. */
	tty->driver_data = un;

	DPR_OPEN(("Open called. MAJOR: %d MINOR:%d unit: %p NAME: %s\n",
		MAJOR(tty_devnum(tty)), MINOR(tty_devnum(tty)), un, brd->name));

	/*
	 * Error if channel info pointer is 0.
	 */
	if ((bs = ch->ch_bs) == 0) {
		DGAP_UNLOCK(ch->ch_lock, lock_flags2);
		DGAP_UNLOCK(brd->bd_lock, lock_flags);
		DPR_OPEN(("%d BS is 0!\n", __LINE__));
		return -ENXIO;
        }

	DPR_OPEN(("%d: tflag=%x  pflag=%x\n", __LINE__, ch->ch_tun.un_flags, ch->ch_pun.un_flags));

	/*
	 * Initialize tty's
	 */
	if (!(un->un_flags & UN_ISOPEN)) {
		/* Store important variables. */
		un->un_tty     = tty;

		/* Maybe do something here to the TTY struct as well? */
	}

	/*
	 * Initialize if neither terminal or printer is open.
	 */
	if (!((ch->ch_tun.un_flags | ch->ch_pun.un_flags) & UN_ISOPEN)) {

		DPR_OPEN(("dgap_open: initializing channel in open...\n"));

		ch->ch_mforce = 0;
		ch->ch_mval = 0;

		/*
		 * Flush input queue.
		 */
		head = readw(&(bs->rx_head));
		writew(head, &(bs->rx_tail));

		ch->ch_flags = 0;
		ch->pscan_state = 0;
		ch->pscan_savechar = 0;

		ch->ch_c_cflag   = tty->termios->c_cflag;
		ch->ch_c_iflag   = tty->termios->c_iflag;
		ch->ch_c_oflag   = tty->termios->c_oflag;
		ch->ch_c_lflag   = tty->termios->c_lflag;
		ch->ch_startc = tty->termios->c_cc[VSTART];
		ch->ch_stopc  = tty->termios->c_cc[VSTOP];

		/* TODO: flush our TTY struct here? */
	}

	dgap_carrier(ch);
	/*
	 * Run param in case we changed anything
	 */
	dgap_param(tty);

	/*                              
	 * follow protocol for opening port
	 */

	DGAP_UNLOCK(ch->ch_lock, lock_flags2);
	DGAP_UNLOCK(brd->bd_lock, lock_flags);

	rc = dgap_block_til_ready(tty, file, ch);

	if (!un->un_tty) {
		return -ENODEV;
	}

	if (rc) {
		DPR_OPEN(("dgap_tty_open returning after dgap_block_til_ready "
			"with %d\n", rc));
	}

	/* No going back now, increment our unit and channel counters */
	DGAP_LOCK(ch->ch_lock, lock_flags);
	ch->ch_open_count++;
	un->un_open_count++;
	un->un_flags |= (UN_ISOPEN);
	DGAP_UNLOCK(ch->ch_lock, lock_flags);

	DPR_OPEN(("dgap_tty_open finished\n"));
	return (rc);
}


/*   
 * dgap_block_til_ready()
 *
 * Wait for DCD, if needed.
 */
static int dgap_block_til_ready(struct tty_struct *tty, struct file *file, struct channel_t *ch)
{ 
	int retval = 0;
	struct un_t *un = NULL;
	ulong   lock_flags;
	uint	old_flags = 0;
	int sleep_on_un_flags = 0;

	if (!tty || tty->magic != TTY_MAGIC || !file || !ch || ch->magic != DGAP_CHANNEL_MAGIC) {
		return (-ENXIO);
	}

	un = tty->driver_data;
	if (!un || un->magic != DGAP_UNIT_MAGIC) {
		return (-ENXIO);
	}

	DPR_OPEN(("dgap_block_til_ready - before block.\n"));

	DGAP_LOCK(ch->ch_lock, lock_flags);

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

			if (file->f_flags & O_NONBLOCK) {
				break;
			}

			if (tty->flags & (1 << TTY_IO_ERROR)) {
				break;
			}

			if (ch->ch_flags & CH_CD) {
				DPR_OPEN(("%d: ch_flags: %x\n", __LINE__, ch->ch_flags));
				break;
			}

			if (ch->ch_flags & CH_FCAR) {
				DPR_OPEN(("%d: ch_flags: %x\n", __LINE__, ch->ch_flags));
				break;
			}
		}
		else {
			sleep_on_un_flags = 1;
		}

		/*
		 * If there is a signal pending, the user probably
		 * interrupted (ctrl-c) us.
		 * Leave loop with error set.
		 */
		if (signal_pending(current)) {
			DPR_OPEN(("%d: signal pending...\n", __LINE__));
			retval = -ERESTARTSYS;
			break;
		}

		DPR_OPEN(("dgap_block_til_ready - blocking.\n"));

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

		DGAP_UNLOCK(ch->ch_lock, lock_flags);

		DPR_OPEN(("Going to sleep on %s flags...\n",
			(sleep_on_un_flags ? "un" : "ch")));

		/*
		 * Wait for something in the flags to change from the current value.
		 */
		if (sleep_on_un_flags) {
			retval = wait_event_interruptible(un->un_flags_wait,
				(old_flags != (ch->ch_tun.un_flags | ch->ch_pun.un_flags)));
		}
		else {
			retval = wait_event_interruptible(ch->ch_flags_wait,
				(old_flags != ch->ch_flags));
		}

		DPR_OPEN(("After sleep... retval: %x\n", retval));

		/*
		 * We got woken up for some reason.
		 * Before looping around, grab our channel lock.
		 */
		DGAP_LOCK(ch->ch_lock, lock_flags);
	}

	ch->ch_wopen--;

	DGAP_UNLOCK(ch->ch_lock, lock_flags);

	DPR_OPEN(("dgap_block_til_ready - after blocking.\n"));

	if (retval) {
		DPR_OPEN(("dgap_block_til_ready - done. error. retval: %x\n", retval));
		return(retval);
	}

	DPR_OPEN(("dgap_block_til_ready - done no error. jiffies: %lu\n", jiffies));

	return(0);
}


/*
 * dgap_tty_hangup()
 *
 * Hangup the port.  Like a close, but don't wait for output to drain.
 */     
static void dgap_tty_hangup(struct tty_struct *tty)
{
	struct board_t	*bd;
	struct channel_t *ch;
	struct un_t	*un;

	if (!tty || tty->magic != TTY_MAGIC)
		return;

	un = tty->driver_data;
	if (!un || un->magic != DGAP_UNIT_MAGIC)
		return;

	ch = un->un_ch;
	if (!ch || ch->magic != DGAP_CHANNEL_MAGIC)
		return;

	bd = ch->ch_bd;
	if (!bd || bd->magic != DGAP_BOARD_MAGIC)
		return;

	DPR_CLOSE(("dgap_hangup called. ch->ch_open_count: %d un->un_open_count: %d\n",
		ch->ch_open_count, un->un_open_count));

	/* flush the transmit queues */
	dgap_tty_flush_buffer(tty);

	DPR_CLOSE(("dgap_hangup finished. ch->ch_open_count: %d un->un_open_count: %d\n",
		ch->ch_open_count, un->un_open_count));
}



/*
 * dgap_tty_close()
 *
 */
static void dgap_tty_close(struct tty_struct *tty, struct file *file)
{
	struct ktermios *ts;
	struct board_t *bd;
	struct channel_t *ch;
	struct un_t *un;
	ulong lock_flags;
	int rc = 0;

	if (!tty || tty->magic != TTY_MAGIC)
		return;

	un = tty->driver_data;
	if (!un || un->magic != DGAP_UNIT_MAGIC)
		return;

	ch = un->un_ch;
	if (!ch || ch->magic != DGAP_CHANNEL_MAGIC)
		return;

	bd = ch->ch_bd;
	if (!bd || bd->magic != DGAP_BOARD_MAGIC)
		return;

	ts = tty->termios;

	DPR_CLOSE(("Close called\n"));

	DGAP_LOCK(ch->ch_lock, lock_flags);

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

	if (--un->un_open_count < 0) {
		APR(("bad serial port open count of %d\n", un->un_open_count));
		un->un_open_count = 0;
	}

	ch->ch_open_count--;

	if (ch->ch_open_count && un->un_open_count) {
		DPR_CLOSE(("dgap_tty_close: not last close ch: %d un:%d\n",
			ch->ch_open_count, un->un_open_count));

		DGAP_UNLOCK(ch->ch_lock, lock_flags);
                return;
        }

	/* OK, its the last close on the unit */
	DPR_CLOSE(("dgap_tty_close - last close on unit procedures\n"));

	un->un_flags |= UN_CLOSING;

	tty->closing = 1;

	/*
	 * Only officially close channel if count is 0 and
         * DIGI_PRINTER bit is not set.
	 */
	if ((ch->ch_open_count == 0) && !(ch->ch_digi.digi_flags & DIGI_PRINTER)) {

		ch->ch_flags &= ~(CH_RXBLOCK);

		DGAP_UNLOCK(ch->ch_lock, lock_flags);

		/* wait for output to drain */
		/* This will also return if we take an interrupt */

		DPR_CLOSE(("Calling wait_for_drain\n"));
		rc = dgap_wait_for_drain(tty);
		DPR_CLOSE(("After calling wait_for_drain\n"));

		if (rc) {
			DPR_BASIC(("dgap_tty_close - bad return: %d ", rc));
		}

		dgap_tty_flush_buffer(tty);
		tty_ldisc_flush(tty);

		DGAP_LOCK(ch->ch_lock, lock_flags);

		tty->closing = 0;

		/*
		 * If we have HUPCL set, lower DTR and RTS
		 */
		if (ch->ch_c_cflag & HUPCL ) {
			DPR_CLOSE(("Close. HUPCL set, dropping DTR/RTS\n"));
			ch->ch_mostat &= ~(D_RTS(ch)|D_DTR(ch));
			dgap_cmdb( ch, SMODEM, 0, D_DTR(ch)|D_RTS(ch), 0 );

			/*
			 * Go to sleep to ensure RTS/DTR 
			 * have been dropped for modems to see it.
			 */
			if (ch->ch_close_delay) {
				DPR_CLOSE(("Close. Sleeping for RTS/DTR drop\n"));

				DGAP_UNLOCK(ch->ch_lock, lock_flags);
				dgap_ms_sleep(ch->ch_close_delay);
				DGAP_LOCK(ch->ch_lock, lock_flags);

				DPR_CLOSE(("Close. After sleeping for RTS/DTR drop\n"));
			}
		}

		ch->pscan_state = 0;
		ch->pscan_savechar = 0;
		ch->ch_baud_info = 0;

	}

	/*
	 * turn off print device when closing print device.
	 */
	if ((un->un_type == DGAP_PRINT)  && (ch->ch_flags & CH_PRON) ) {
		dgap_wmove(ch, ch->ch_digi.digi_offstr,
			(int) ch->ch_digi.digi_offlen);
		ch->ch_flags &= ~CH_PRON;
	}

	un->un_tty = NULL;
	un->un_flags &= ~(UN_ISOPEN | UN_CLOSING);
	tty->driver_data = NULL;

	DPR_CLOSE(("Close. Doing wakeups\n"));
	wake_up_interruptible(&ch->ch_flags_wait);
	wake_up_interruptible(&un->un_flags_wait);

	DGAP_UNLOCK(ch->ch_lock, lock_flags);
                
        DPR_BASIC(("dgap_tty_close - complete\n"));
}


/*
 * dgap_tty_chars_in_buffer()
 *
 * Return number of characters that have not been transmitted yet.
 *
 * This routine is used by the line discipline to determine if there
 * is data waiting to be transmitted/drained/flushed or not.
 */
static int dgap_tty_chars_in_buffer(struct tty_struct *tty)
{
	struct board_t *bd = NULL;
	struct channel_t *ch = NULL;
	struct un_t *un = NULL;
	struct bs_t *bs = NULL;
	uchar tbusy;
	uint chars = 0;
	u16 thead, ttail, tmask, chead, ctail;
	ulong   lock_flags = 0;
	ulong   lock_flags2 = 0;

	if (tty == NULL)
		return(0);

	un = tty->driver_data;
	if (!un || un->magic != DGAP_UNIT_MAGIC)
		return (0);

	ch = un->un_ch;
	if (!ch || ch->magic != DGAP_CHANNEL_MAGIC)
		return (0);

	bd = ch->ch_bd;
	if (!bd || bd->magic != DGAP_BOARD_MAGIC)
		return (0);

        bs = ch->ch_bs;
	if (!bs)
		return (0);

	DGAP_LOCK(bd->bd_lock, lock_flags);
	DGAP_LOCK(ch->ch_lock, lock_flags2);

	tmask = (ch->ch_tsize - 1);

	/* Get Transmit queue pointers */
	thead = readw(&(bs->tx_head)) & tmask;
	ttail = readw(&(bs->tx_tail)) & tmask;

	/* Get tbusy flag */
	tbusy = readb(&(bs->tbusy));

	/* Get Command queue pointers */
	chead = readw(&(ch->ch_cm->cm_head));
	ctail = readw(&(ch->ch_cm->cm_tail));

	DGAP_UNLOCK(ch->ch_lock, lock_flags2);
	DGAP_UNLOCK(bd->bd_lock, lock_flags);

	/*
	 * The only way we know for sure if there is no pending
	 * data left to be transferred, is if:
	 * 1) Transmit head and tail are equal (empty).
	 * 2) Command queue head and tail are equal (empty).
	 * 3) The "TBUSY" flag is 0. (Transmitter not busy).
 	 */

	if ((ttail == thead) && (tbusy == 0) && (chead == ctail)) {
		chars = 0;
	}
	else {
		if (thead >= ttail)
			chars = thead - ttail;
		else
			chars = thead - ttail + ch->ch_tsize;
		/*
		 * Fudge factor here.
		 * If chars is zero, we know that the command queue had
		 * something in it or tbusy was set.  Because we cannot
		 * be sure if there is still some data to be transmitted,
		 * lets lie, and tell ld we have 1 byte left.
		 */
		if (chars == 0) {
			/*
			 * If TBUSY is still set, and our tx buffers are empty,
			 * force the firmware to send me another wakeup after
			 * TBUSY has been cleared.
			 */
			if (tbusy != 0) {
				DGAP_LOCK(ch->ch_lock, lock_flags);
				un->un_flags |= UN_EMPTY;
				writeb(1, &(bs->iempty));
				DGAP_UNLOCK(ch->ch_lock, lock_flags);
			}
			chars = 1;
		}
	}

 	DPR_WRITE(("dgap_tty_chars_in_buffer. Port: %x - %d (head: %d tail: %d tsize: %d)\n", 
		ch->ch_portnum, chars, thead, ttail, ch->ch_tsize));
        return(chars);
}


static int dgap_wait_for_drain(struct tty_struct *tty)
{
	struct channel_t *ch;
	struct un_t *un;
	struct bs_t *bs;
	int ret = -EIO;
	uint count = 1;
	ulong   lock_flags = 0;

	if (!tty || tty->magic != TTY_MAGIC)
		return ret;

	un = tty->driver_data;
	if (!un || un->magic != DGAP_UNIT_MAGIC)
		return ret;

	ch = un->un_ch;
	if (!ch || ch->magic != DGAP_CHANNEL_MAGIC)
		return ret;

        bs = ch->ch_bs;
	if (!bs)
		return ret;

	ret = 0;

	DPR_DRAIN(("dgap_wait_for_drain start\n"));

	/* Loop until data is drained */
	while (count != 0) {

		count = dgap_tty_chars_in_buffer(tty);

		if (count == 0)
			break;

		/* Set flag waiting for drain */
		DGAP_LOCK(ch->ch_lock, lock_flags);
		un->un_flags |= UN_EMPTY;
		writeb(1, &(bs->iempty));
		DGAP_UNLOCK(ch->ch_lock, lock_flags);

		/* Go to sleep till we get woken up */
		ret = wait_event_interruptible(un->un_flags_wait, ((un->un_flags & UN_EMPTY) == 0));
		/* If ret is non-zero, user ctrl-c'ed us */
		if (ret) {
			break;
		}
	}

	DGAP_LOCK(ch->ch_lock, lock_flags);
	un->un_flags &= ~(UN_EMPTY);
	DGAP_UNLOCK(ch->ch_lock, lock_flags);

	DPR_DRAIN(("dgap_wait_for_drain finish\n"));
	return (ret);
}


/*               
 * dgap_maxcps_room
 *
 * Reduces bytes_available to the max number of characters
 * that can be sent currently given the maxcps value, and
 * returns the new bytes_available.  This only affects printer
 * output.
 */                     
static int dgap_maxcps_room(struct tty_struct *tty, int bytes_available)
{
	struct channel_t *ch = NULL;
	struct un_t *un = NULL;

	if (tty == NULL)
		return (bytes_available);

	un = tty->driver_data;
	if (!un || un->magic != DGAP_UNIT_MAGIC)
		return (bytes_available);

	ch = un->un_ch;
	if (!ch || ch->magic != DGAP_CHANNEL_MAGIC)
		return (bytes_available);

	/*
	 * If its not the Transparent print device, return
	 * the full data amount.
	 */
	if (un->un_type != DGAP_PRINT)
		return (bytes_available);

	if (ch->ch_digi.digi_maxcps > 0 && ch->ch_digi.digi_bufsize > 0 ) {
		int cps_limit = 0;
		unsigned long current_time = jiffies;
		unsigned long buffer_time = current_time +
			(HZ * ch->ch_digi.digi_bufsize) / ch->ch_digi.digi_maxcps;

		if (ch->ch_cpstime < current_time) {
			/* buffer is empty */
			ch->ch_cpstime = current_time;            /* reset ch_cpstime */
			cps_limit = ch->ch_digi.digi_bufsize;
		}
		else if (ch->ch_cpstime < buffer_time) {
			/* still room in the buffer */
			cps_limit = ((buffer_time - ch->ch_cpstime) * ch->ch_digi.digi_maxcps) / HZ;
		}
		else {
			/* no room in the buffer */
			cps_limit = 0; 
		}

		bytes_available = min(cps_limit, bytes_available);
	}

	return (bytes_available);
}


static inline void dgap_set_firmware_event(struct un_t *un, unsigned int event)
{
	struct channel_t *ch = NULL;
	struct bs_t *bs = NULL;

	if (!un || un->magic != DGAP_UNIT_MAGIC)
		return;
	ch = un->un_ch;
	if (!ch || ch->magic != DGAP_CHANNEL_MAGIC)
		return;
        bs = ch->ch_bs;
	if (!bs)
		return;

	if ((event & UN_LOW) != 0) {
		if ((un->un_flags & UN_LOW) == 0) {
			un->un_flags |= UN_LOW;
			writeb(1, &(bs->ilow));
		}
	}
	if ((event & UN_LOW) != 0) {
		if ((un->un_flags & UN_EMPTY) == 0) {
			un->un_flags |= UN_EMPTY;
			writeb(1, &(bs->iempty));
		}
	}
}


/*
 * dgap_tty_write_room()
 *
 * Return space available in Tx buffer
 */        
static int dgap_tty_write_room(struct tty_struct *tty)
{
	struct channel_t *ch = NULL;
	struct un_t *un = NULL;
	struct bs_t *bs = NULL;
	u16 head, tail, tmask;
	int ret = 0;
	ulong   lock_flags = 0;

	if (tty == NULL || dgap_TmpWriteBuf == NULL)
		return(0);

	un = tty->driver_data;
	if (!un || un->magic != DGAP_UNIT_MAGIC)
		return (0);

	ch = un->un_ch;
	if (!ch || ch->magic != DGAP_CHANNEL_MAGIC)
		return (0);

        bs = ch->ch_bs;
	if (!bs)
		return (0);

	DGAP_LOCK(ch->ch_lock, lock_flags);

	tmask = ch->ch_tsize - 1;
	head = readw(&(bs->tx_head)) & tmask;
	tail = readw(&(bs->tx_tail)) & tmask;

        if ((ret = tail - head - 1) < 0)
                ret += ch->ch_tsize;

	/* Limit printer to maxcps */
	ret = dgap_maxcps_room(tty, ret);

	/*
	 * If we are printer device, leave space for 
	 * possibly both the on and off strings.
	 */
	if (un->un_type == DGAP_PRINT) {
		if (!(ch->ch_flags & CH_PRON))
			ret -= ch->ch_digi.digi_onlen;
		ret -= ch->ch_digi.digi_offlen;
	}
	else {
		if (ch->ch_flags & CH_PRON)
			ret -= ch->ch_digi.digi_offlen;
	}

	if (ret < 0)
		ret = 0;

	/*
	 * Schedule FEP to wake us up if needed.
	 *
	 * TODO:  This might be overkill...
	 * Do we really need to schedule callbacks from the FEP
	 * in every case?  Can we get smarter based on ret?
	 */
	dgap_set_firmware_event(un, UN_LOW | UN_EMPTY);
	DGAP_UNLOCK(ch->ch_lock, lock_flags);
 
	DPR_WRITE(("dgap_tty_write_room - %d tail: %d head: %d\n", ret, tail, head));

        return(ret);
}


/*
 * dgap_tty_put_char()
 *
 * Put a character into ch->ch_buf
 *                              
 *      - used by the line discipline for OPOST processing
 */
static int dgap_tty_put_char(struct tty_struct *tty, unsigned char c)
{
	/*
	 * Simply call tty_write.
	 */
	DPR_WRITE(("dgap_tty_put_char called\n"));
	dgap_tty_write(tty, &c, 1);
	return 1;
}


/*
 * dgap_tty_write()
 *
 * Take data from the user or kernel and send it out to the FEP.
 * In here exists all the Transparent Print magic as well.
 */
static int dgap_tty_write(struct tty_struct *tty, const unsigned char *buf, int count)
{
	struct channel_t *ch = NULL;
	struct un_t *un = NULL;
	struct bs_t *bs = NULL;
	char *vaddr = NULL;
	u16 head, tail, tmask, remain;
	int bufcount = 0, n = 0;
	int orig_count = 0;
	ulong lock_flags;
	int from_user = 0;

	if (tty == NULL || dgap_TmpWriteBuf == NULL)
		return(0);

	un = tty->driver_data;
	if (!un || un->magic != DGAP_UNIT_MAGIC)
		return (0);

	ch = un->un_ch;
	if (!ch || ch->magic != DGAP_CHANNEL_MAGIC)
		return(0);

        bs = ch->ch_bs;
	if (!bs)
		return(0);

	if (!count)
		return(0);

	DPR_WRITE(("dgap_tty_write: Port: %x tty=%p user=%d len=%d\n",
		ch->ch_portnum, tty, from_user, count));

	/*
	 * Store original amount of characters passed in.
	 * This helps to figure out if we should ask the FEP
	 * to send us an event when it has more space available.
	 */
	orig_count = count;

	DGAP_LOCK(ch->ch_lock, lock_flags);

	/* Get our space available for the channel from the board */
	tmask = ch->ch_tsize - 1;
	head = readw(&(bs->tx_head)) & tmask;
	tail = readw(&(bs->tx_tail)) & tmask;

	if ((bufcount = tail - head - 1) < 0)
		bufcount += ch->ch_tsize;

	DPR_WRITE(("%d: bufcount: %x count: %x tail: %x head: %x tmask: %x\n",
		__LINE__, bufcount, count, tail, head, tmask));

	/*
	 * Limit printer output to maxcps overall, with bursts allowed
	 * up to bufsize characters.
	 */
	bufcount = dgap_maxcps_room(tty, bufcount);

	/*
	 * Take minimum of what the user wants to send, and the
	 * space available in the FEP buffer.
	 */
	count = min(count, bufcount);

	/*
	 * Bail if no space left.
	 */
	if (count <= 0) {
		dgap_set_firmware_event(un, UN_LOW | UN_EMPTY);
		DGAP_UNLOCK(ch->ch_lock, lock_flags);
		return(0);
	}

	/*
	 * Output the printer ON string, if we are in terminal mode, but
	 * need to be in printer mode.
	 */
	if ((un->un_type == DGAP_PRINT) && !(ch->ch_flags & CH_PRON)) {
		dgap_wmove(ch, ch->ch_digi.digi_onstr,
		    (int) ch->ch_digi.digi_onlen);
		head = readw(&(bs->tx_head)) & tmask;
		ch->ch_flags |= CH_PRON;
	}

	/*
	 * On the other hand, output the printer OFF string, if we are
	 * currently in printer mode, but need to output to the terminal.
	 */
	if ((un->un_type != DGAP_PRINT) && (ch->ch_flags & CH_PRON)) {
		dgap_wmove(ch, ch->ch_digi.digi_offstr,
			(int) ch->ch_digi.digi_offlen);
		head = readw(&(bs->tx_head)) & tmask;
		ch->ch_flags &= ~CH_PRON;
	}

	/*
	 * If there is nothing left to copy, or I can't handle any more data, leave.
	 */
	if (count <= 0) {
		dgap_set_firmware_event(un, UN_LOW | UN_EMPTY);
		DGAP_UNLOCK(ch->ch_lock, lock_flags);
		return(0);
	}

	if (from_user) {

		count = min(count, WRITEBUFLEN);

		DGAP_UNLOCK(ch->ch_lock, lock_flags);

		/*
		 * If data is coming from user space, copy it into a temporary
		 * buffer so we don't get swapped out while doing the copy to
		 * the board.
		 */
		/* we're allowed to block if it's from_user */
		if (down_interruptible(&dgap_TmpWriteSem)) {
			return (-EINTR);
		}

		if (copy_from_user(dgap_TmpWriteBuf, (const uchar __user *) buf, count)) {
			up(&dgap_TmpWriteSem);
			printk("Write: Copy from user failed!\n");
			return -EFAULT;
		}

		DGAP_LOCK(ch->ch_lock, lock_flags);

		buf = dgap_TmpWriteBuf;
	}

	n = count;

	/*
	 * If the write wraps over the top of the circular buffer,
	 * move the portion up to the wrap point, and reset the
	 * pointers to the bottom.
	 */
	remain = ch->ch_tstart + ch->ch_tsize - head;

	if (n >= remain) {
		n -= remain;
		vaddr = ch->ch_taddr + head;

		memcpy_toio(vaddr, (uchar *) buf, remain);
		dgap_sniff_nowait_nolock(ch, "USER WRITE", (uchar *) buf, remain);

		head = ch->ch_tstart;
		buf += remain;
	}

	if (n > 0) {

		/*
		 * Move rest of data.
		 */
		vaddr = ch->ch_taddr + head;
		remain = n;

		memcpy_toio(vaddr, (uchar *) buf, remain);
		dgap_sniff_nowait_nolock(ch, "USER WRITE", (uchar *) buf, remain);

		head += remain;

	}

	if (count) {
		ch->ch_txcount += count;
		head &= tmask;
		writew(head, &(bs->tx_head));
	}


	dgap_set_firmware_event(un, UN_LOW | UN_EMPTY);

	/*
	 * If this is the print device, and the
	 * printer is still on, we need to turn it
	 * off before going idle.  If the buffer is
	 * non-empty, wait until it goes empty.
	 * Otherwise turn it off right now.
	 */
	if ((un->un_type == DGAP_PRINT) && (ch->ch_flags & CH_PRON)) {
		tail = readw(&(bs->tx_tail)) & tmask;

		if (tail != head) {
			un->un_flags |= UN_EMPTY;
			writeb(1, &(bs->iempty));
		}
		else {
			dgap_wmove(ch, ch->ch_digi.digi_offstr,
				(int) ch->ch_digi.digi_offlen);
			head = readw(&(bs->tx_head)) & tmask;
			ch->ch_flags &= ~CH_PRON;
		}
	}

	/* Update printer buffer empty time. */
	if ((un->un_type == DGAP_PRINT) && (ch->ch_digi.digi_maxcps > 0)
	    && (ch->ch_digi.digi_bufsize > 0)) {
                ch->ch_cpstime += (HZ * count) / ch->ch_digi.digi_maxcps;
	}

	if (from_user) {
		DGAP_UNLOCK(ch->ch_lock, lock_flags);
		up(&dgap_TmpWriteSem);
	} 
	else {
		DGAP_UNLOCK(ch->ch_lock, lock_flags);
	}

	DPR_WRITE(("Write finished - Write %d bytes of %d.\n", count, orig_count));

	return (count);
}



/*
 * Return modem signals to ld.
 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,39)
static int dgap_tty_tiocmget(struct tty_struct *tty)
#else
static int dgap_tty_tiocmget(struct tty_struct *tty, struct file *file)
#endif
{
	struct channel_t *ch;
	struct un_t *un;
	int result = -EIO;
	uchar mstat = 0;
	ulong lock_flags;

	if (!tty || tty->magic != TTY_MAGIC)
		return result;

	un = tty->driver_data;
	if (!un || un->magic != DGAP_UNIT_MAGIC)
		return result;

	ch = un->un_ch;
	if (!ch || ch->magic != DGAP_CHANNEL_MAGIC)
		return result;

	DPR_IOCTL(("dgap_tty_tiocmget start\n"));

	DGAP_LOCK(ch->ch_lock, lock_flags);

	mstat = readb(&(ch->ch_bs->m_stat));
        /* Append any outbound signals that might be pending... */
        mstat |= ch->ch_mostat;

	DGAP_UNLOCK(ch->ch_lock, lock_flags);

	result = 0;

	if (mstat & D_DTR(ch))
		result |= TIOCM_DTR;
	if (mstat & D_RTS(ch))
		result |= TIOCM_RTS;
	if (mstat & D_CTS(ch))
		result |= TIOCM_CTS;
	if (mstat & D_DSR(ch))
		result |= TIOCM_DSR;
	if (mstat & D_RI(ch))
		result |= TIOCM_RI;
	if (mstat & D_CD(ch))
		result |= TIOCM_CD;

	DPR_IOCTL(("dgap_tty_tiocmget finish\n"));

	return result;
}


/*
 * dgap_tty_tiocmset()
 *
 * Set modem signals, called by ld.
 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,39)
static int dgap_tty_tiocmset(struct tty_struct *tty,
                unsigned int set, unsigned int clear)
#else
static int dgap_tty_tiocmset(struct tty_struct *tty, struct file *file,
		unsigned int set, unsigned int clear)
#endif
{
	struct board_t *bd;
	struct channel_t *ch;
	struct un_t *un;
	int ret = -EIO;
	ulong lock_flags;
	ulong lock_flags2;

	if (!tty || tty->magic != TTY_MAGIC)
		return ret;

	un = tty->driver_data;
	if (!un || un->magic != DGAP_UNIT_MAGIC)
		return ret;

	ch = un->un_ch;
	if (!ch || ch->magic != DGAP_CHANNEL_MAGIC)
		return ret;

	bd = ch->ch_bd;
	if (!bd || bd->magic != DGAP_BOARD_MAGIC)
		return ret;

	DPR_IOCTL(("dgap_tty_tiocmset start\n"));

	DGAP_LOCK(bd->bd_lock, lock_flags);
	DGAP_LOCK(ch->ch_lock, lock_flags2);

	if (set & TIOCM_RTS) {
		ch->ch_mforce |= D_RTS(ch);
		ch->ch_mval   |= D_RTS(ch);
        }         

	if (set & TIOCM_DTR) {
		ch->ch_mforce |= D_DTR(ch);
		ch->ch_mval   |= D_DTR(ch);
        }         

	if (clear & TIOCM_RTS) {
		ch->ch_mforce |= D_RTS(ch);
		ch->ch_mval   &= ~(D_RTS(ch));
        }

	if (clear & TIOCM_DTR) {
		ch->ch_mforce |= D_DTR(ch);
		ch->ch_mval   &= ~(D_DTR(ch));
        }

	dgap_param(tty);

	DGAP_UNLOCK(ch->ch_lock, lock_flags2);
	DGAP_UNLOCK(bd->bd_lock, lock_flags);

	DPR_IOCTL(("dgap_tty_tiocmset finish\n"));

	return (0);
}



/*
 * dgap_tty_send_break()
 *
 * Send a Break, called by ld.
 */
static int dgap_tty_send_break(struct tty_struct *tty, int msec)
{
	struct board_t *bd;
	struct channel_t *ch;
	struct un_t *un;
	int ret = -EIO;
	ulong lock_flags;
	ulong lock_flags2;

	if (!tty || tty->magic != TTY_MAGIC)
		return ret;

	un = tty->driver_data;
	if (!un || un->magic != DGAP_UNIT_MAGIC)
		return ret;

	ch = un->un_ch;
	if (!ch || ch->magic != DGAP_CHANNEL_MAGIC)
		return ret;

	bd = ch->ch_bd;
	if (!bd || bd->magic != DGAP_BOARD_MAGIC)
		return ret;

	switch (msec) {
	case -1:
		msec = 0xFFFF;
		break;
	case 0:
		msec = 1;
		break;
	default:
		msec /= 10;
		break;
	}

	DPR_IOCTL(("dgap_tty_send_break start 1.  %lx\n", jiffies));

	DGAP_LOCK(bd->bd_lock, lock_flags);
	DGAP_LOCK(ch->ch_lock, lock_flags2);
#if 0
	dgap_cmdw(ch, SBREAK, (u16) SBREAK_TIME, 0);
#endif
	dgap_cmdw(ch, SBREAK, (u16) msec, 0);

	DGAP_UNLOCK(ch->ch_lock, lock_flags2);
	DGAP_UNLOCK(bd->bd_lock, lock_flags);

	DPR_IOCTL(("dgap_tty_send_break finish\n"));

	return (0);
}




/*
 * dgap_tty_wait_until_sent()
 *
 * wait until data has been transmitted, called by ld.
 */
static void dgap_tty_wait_until_sent(struct tty_struct *tty, int timeout)
{
	int rc;
	rc = dgap_wait_for_drain(tty);
	if (rc) {
		DPR_IOCTL(("dgap_tty_ioctl - bad return: %d ", rc));
		return;
	}
	return;
}



/*
 * dgap_send_xchar()
 * 
 * send a high priority character, called by ld.
 */
static void dgap_tty_send_xchar(struct tty_struct *tty, char c)
{
	struct board_t *bd;
	struct channel_t *ch;
	struct un_t *un;
	ulong lock_flags;
	ulong lock_flags2;

	if (!tty || tty->magic != TTY_MAGIC)
		return;

	un = tty->driver_data;
	if (!un || un->magic != DGAP_UNIT_MAGIC)
		return;

	ch = un->un_ch;
	if (!ch || ch->magic != DGAP_CHANNEL_MAGIC)
		return;

	bd = ch->ch_bd;
	if (!bd || bd->magic != DGAP_BOARD_MAGIC)
		return;

	DPR_IOCTL(("dgap_tty_send_xchar start 1.  %lx\n", jiffies));

	DGAP_LOCK(bd->bd_lock, lock_flags);
	DGAP_LOCK(ch->ch_lock, lock_flags2);

	/*
	 * This is technically what we should do.
	 * However, the NIST tests specifically want
	 * to see each XON or XOFF character that it
	 * sends, so lets just send each character
	 * by hand...
	 */
#if 0
	if (c == STOP_CHAR(tty)) {
		dgap_cmdw(ch, RPAUSE, 0, 0);
	}
	else if (c == START_CHAR(tty)) {
		dgap_cmdw(ch, RRESUME, 0, 0);
	}
	else {
		dgap_wmove(ch, &c, 1);
	}
#else
	dgap_wmove(ch, &c, 1);
#endif

	DGAP_UNLOCK(ch->ch_lock, lock_flags2);
	DGAP_UNLOCK(bd->bd_lock, lock_flags);

	DPR_IOCTL(("dgap_tty_send_xchar finish\n"));

	return;
}




/*
 * Return modem signals to ld.
 */
static int dgap_get_modem_info(struct channel_t *ch, unsigned int __user *value)
{
	int result = 0;
	uchar mstat = 0;
	ulong lock_flags;
	int rc = 0;

	DPR_IOCTL(("dgap_get_modem_info start\n"));

	if (!ch || ch->magic != DGAP_CHANNEL_MAGIC)
		return(-ENXIO);

	DGAP_LOCK(ch->ch_lock, lock_flags);

	mstat = readb(&(ch->ch_bs->m_stat));
	/* Append any outbound signals that might be pending... */
	mstat |= ch->ch_mostat;

	DGAP_UNLOCK(ch->ch_lock, lock_flags);

	result = 0;

	if (mstat & D_DTR(ch))
		result |= TIOCM_DTR;
	if (mstat & D_RTS(ch))
		result |= TIOCM_RTS;
	if (mstat & D_CTS(ch))
		result |= TIOCM_CTS;
	if (mstat & D_DSR(ch))
		result |= TIOCM_DSR;
	if (mstat & D_RI(ch))
		result |= TIOCM_RI;
	if (mstat & D_CD(ch))
		result |= TIOCM_CD;

	rc = put_user(result, value);

	DPR_IOCTL(("dgap_get_modem_info finish\n"));
	return(rc);
}


/*
 * dgap_set_modem_info()
 *
 * Set modem signals, called by ld.
 */
static int dgap_set_modem_info(struct tty_struct *tty, unsigned int command, unsigned int __user *value)
{
	struct board_t *bd;
	struct channel_t *ch;
	struct un_t *un;
	int ret = -ENXIO;
	unsigned int arg = 0;
	ulong lock_flags;
	ulong lock_flags2;

	if (!tty || tty->magic != TTY_MAGIC)
		return ret;

	un = tty->driver_data;
	if (!un || un->magic != DGAP_UNIT_MAGIC)
		return ret;

	ch = un->un_ch;
	if (!ch || ch->magic != DGAP_CHANNEL_MAGIC)
		return ret;

	bd = ch->ch_bd;
	if (!bd || bd->magic != DGAP_BOARD_MAGIC)
		return ret;

	DPR_IOCTL(("dgap_set_modem_info() start\n"));

	ret = get_user(arg, value);
	if (ret) {
		DPR_IOCTL(("dgap_set_modem_info %d ret: %x. finished.\n", __LINE__, ret));
		return(ret);
	}

	DPR_IOCTL(("dgap_set_modem_info: command: %x arg: %x\n", command, arg));

	switch (command) {
	case TIOCMBIS:
		if (arg & TIOCM_RTS) {
			ch->ch_mforce |= D_RTS(ch);
			ch->ch_mval   |= D_RTS(ch);
        	}

		if (arg & TIOCM_DTR) {
			ch->ch_mforce |= D_DTR(ch);
			ch->ch_mval   |= D_DTR(ch);
        	}

		break;

	case TIOCMBIC:
		if (arg & TIOCM_RTS) {
			ch->ch_mforce |= D_RTS(ch);
			ch->ch_mval   &= ~(D_RTS(ch));
        	}

		if (arg & TIOCM_DTR) {
			ch->ch_mforce |= D_DTR(ch);
			ch->ch_mval   &= ~(D_DTR(ch));
        	}

		break;

        case TIOCMSET:
		ch->ch_mforce = D_DTR(ch)|D_RTS(ch);

		if (arg & TIOCM_RTS) {
			ch->ch_mval |= D_RTS(ch);
        	}
		else {
			ch->ch_mval &= ~(D_RTS(ch));
		}

		if (arg & TIOCM_DTR) {
			ch->ch_mval |= (D_DTR(ch));
        	}
		else {
			ch->ch_mval &= ~(D_DTR(ch));
		}

		break;

	default:
		return(-EINVAL);
	}

	DGAP_LOCK(bd->bd_lock, lock_flags);
	DGAP_LOCK(ch->ch_lock, lock_flags2);

	dgap_param(tty);

	DGAP_UNLOCK(ch->ch_lock, lock_flags2);
	DGAP_UNLOCK(bd->bd_lock, lock_flags);

	DPR_IOCTL(("dgap_set_modem_info finish\n"));

	return (0);
}


/*
 * dgap_tty_digigeta() 
 *
 * Ioctl to get the information for ditty.
 *
 *
 *
 */
static int dgap_tty_digigeta(struct tty_struct *tty, struct digi_t __user *retinfo)
{
	struct channel_t *ch;
	struct un_t *un;
	struct digi_t tmp;
	ulong lock_flags;

	if (!retinfo)
		return (-EFAULT);

	if (!tty || tty->magic != TTY_MAGIC)
		return (-EFAULT);

	un = tty->driver_data;
	if (!un || un->magic != DGAP_UNIT_MAGIC)
		return (-EFAULT);

	ch = un->un_ch;
	if (!ch || ch->magic != DGAP_CHANNEL_MAGIC)
		return (-EFAULT);

	memset(&tmp, 0, sizeof(tmp));

	DGAP_LOCK(ch->ch_lock, lock_flags);
	memcpy(&tmp, &ch->ch_digi, sizeof(tmp));
	DGAP_UNLOCK(ch->ch_lock, lock_flags);

	if (copy_to_user(retinfo, &tmp, sizeof(*retinfo)))
		return (-EFAULT);

	return (0);
}


/*
 * dgap_tty_digiseta() 
 *
 * Ioctl to set the information for ditty.
 *
 *
 *
 */
static int dgap_tty_digiseta(struct tty_struct *tty, struct digi_t __user *new_info)
{
	struct board_t *bd;
	struct channel_t *ch;
	struct un_t *un;
	struct digi_t new_digi;
	ulong   lock_flags = 0;
	unsigned long lock_flags2;

	DPR_IOCTL(("DIGI_SETA start\n"));

	if (!tty || tty->magic != TTY_MAGIC)
		return (-EFAULT);

	un = tty->driver_data;
	if (!un || un->magic != DGAP_UNIT_MAGIC)
		return (-EFAULT);

	ch = un->un_ch;
	if (!ch || ch->magic != DGAP_CHANNEL_MAGIC)
		return (-EFAULT);

	bd = ch->ch_bd;
	if (!bd || bd->magic != DGAP_BOARD_MAGIC)
		return (-EFAULT);

        if (copy_from_user(&new_digi, new_info, sizeof(struct digi_t))) {
		DPR_IOCTL(("DIGI_SETA failed copy_from_user\n"));
                return(-EFAULT);
	}

	DGAP_LOCK(bd->bd_lock, lock_flags);
	DGAP_LOCK(ch->ch_lock, lock_flags2);

	memcpy(&ch->ch_digi, &new_digi, sizeof(struct digi_t));

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

	dgap_param(tty);

	DGAP_UNLOCK(ch->ch_lock, lock_flags2);
	DGAP_UNLOCK(bd->bd_lock, lock_flags);

	DPR_IOCTL(("DIGI_SETA finish\n"));

	return(0);
}


/*
 * dgap_tty_digigetedelay() 
 *
 * Ioctl to get the current edelay setting.
 *
 *
 *
 */
static int dgap_tty_digigetedelay(struct tty_struct *tty, int __user *retinfo)
{
	struct channel_t *ch;
	struct un_t *un;
	int tmp;
	ulong lock_flags;

	if (!retinfo)
		return (-EFAULT);

	if (!tty || tty->magic != TTY_MAGIC)
		return (-EFAULT);

	un = tty->driver_data;
	if (!un || un->magic != DGAP_UNIT_MAGIC)
		return (-EFAULT);

	ch = un->un_ch;
	if (!ch || ch->magic != DGAP_CHANNEL_MAGIC)
		return (-EFAULT);

	memset(&tmp, 0, sizeof(tmp));

	DGAP_LOCK(ch->ch_lock, lock_flags);
	tmp = readw(&(ch->ch_bs->edelay));
	DGAP_UNLOCK(ch->ch_lock, lock_flags);

	if (copy_to_user(retinfo, &tmp, sizeof(*retinfo)))
		return (-EFAULT);

	return (0);
}


/*
 * dgap_tty_digisetedelay() 
 *
 * Ioctl to set the EDELAY setting
 *
 */
static int dgap_tty_digisetedelay(struct tty_struct *tty, int __user *new_info)
{
	struct board_t *bd;
	struct channel_t *ch;
	struct un_t *un;
	int new_digi;
	ulong lock_flags;
	ulong lock_flags2;

	DPR_IOCTL(("DIGI_SETA start\n"));

	if (!tty || tty->magic != TTY_MAGIC)
		return (-EFAULT);

	un = tty->driver_data;
	if (!un || un->magic != DGAP_UNIT_MAGIC)
		return (-EFAULT);

	ch = un->un_ch;
	if (!ch || ch->magic != DGAP_CHANNEL_MAGIC)
		return (-EFAULT);

	bd = ch->ch_bd;
	if (!bd || bd->magic != DGAP_BOARD_MAGIC)
		return (-EFAULT);

        if (copy_from_user(&new_digi, new_info, sizeof(int))) {
		DPR_IOCTL(("DIGI_SETEDELAY failed copy_from_user\n"));
                return(-EFAULT);
	}

	DGAP_LOCK(bd->bd_lock, lock_flags);
	DGAP_LOCK(ch->ch_lock, lock_flags2);

	writew((u16) new_digi, &(ch->ch_bs->edelay));

	dgap_param(tty);

	DGAP_UNLOCK(ch->ch_lock, lock_flags2);
	DGAP_UNLOCK(bd->bd_lock, lock_flags);

	DPR_IOCTL(("DIGI_SETA finish\n"));

	return(0);
}


/*
 * dgap_tty_digigetcustombaud()
 *
 * Ioctl to get the current custom baud rate setting.
 */
static int dgap_tty_digigetcustombaud(struct tty_struct *tty, int __user *retinfo)
{
	struct channel_t *ch;
	struct un_t *un;
	int tmp;
	ulong lock_flags;

	if (!retinfo)
		return (-EFAULT);

	if (!tty || tty->magic != TTY_MAGIC)
		return (-EFAULT);

	un = tty->driver_data;
	if (!un || un->magic != DGAP_UNIT_MAGIC)
		return (-EFAULT);

	ch = un->un_ch;
	if (!ch || ch->magic != DGAP_CHANNEL_MAGIC)
		return (-EFAULT);

	memset(&tmp, 0, sizeof(tmp));

	DGAP_LOCK(ch->ch_lock, lock_flags);
	tmp = dgap_get_custom_baud(ch);
	DGAP_UNLOCK(ch->ch_lock, lock_flags);

	DPR_IOCTL(("DIGI_GETCUSTOMBAUD. Returning %d\n", tmp));

	if (copy_to_user(retinfo, &tmp, sizeof(*retinfo)))
		return (-EFAULT);

	return (0);
}


/*
 * dgap_tty_digisetcustombaud() 
 *
 * Ioctl to set the custom baud rate setting
 */
static int dgap_tty_digisetcustombaud(struct tty_struct *tty, int __user *new_info)
{
	struct board_t *bd;
	struct channel_t *ch;
	struct un_t *un;
	uint new_rate;
	ulong lock_flags;
	ulong lock_flags2;

	DPR_IOCTL(("DIGI_SETCUSTOMBAUD start\n"));

	if (!tty || tty->magic != TTY_MAGIC)
		return (-EFAULT);

	un = tty->driver_data;
	if (!un || un->magic != DGAP_UNIT_MAGIC)
		return (-EFAULT);

	ch = un->un_ch;
	if (!ch || ch->magic != DGAP_CHANNEL_MAGIC)
		return (-EFAULT);

	bd = ch->ch_bd;
	if (!bd || bd->magic != DGAP_BOARD_MAGIC)
		return (-EFAULT);


	if (copy_from_user(&new_rate, new_info, sizeof(unsigned int))) {
		DPR_IOCTL(("DIGI_SETCUSTOMBAUD failed copy_from_user\n"));
		return(-EFAULT);
	}

	if (bd->bd_flags & BD_FEP5PLUS) {

		DPR_IOCTL(("DIGI_SETCUSTOMBAUD. Setting %d\n", new_rate));

		DGAP_LOCK(bd->bd_lock, lock_flags);
		DGAP_LOCK(ch->ch_lock, lock_flags2);

		ch->ch_custom_speed = new_rate;

		dgap_param(tty);

		DGAP_UNLOCK(ch->ch_lock, lock_flags2);
		DGAP_UNLOCK(bd->bd_lock, lock_flags);
	}

	DPR_IOCTL(("DIGI_SETCUSTOMBAUD finish\n"));

	return(0);
}


/*
 * dgap_set_termios()
 */
static void dgap_tty_set_termios(struct tty_struct *tty, struct ktermios *old_termios)
{
	struct board_t *bd;
	struct channel_t *ch;
	struct un_t *un;
	unsigned long lock_flags;
	unsigned long lock_flags2;

	if (!tty || tty->magic != TTY_MAGIC)
		return;

	un = tty->driver_data;
	if (!un || un->magic != DGAP_UNIT_MAGIC)
		return;

	ch = un->un_ch;
	if (!ch || ch->magic != DGAP_CHANNEL_MAGIC)
		return;

	bd = ch->ch_bd;
	if (!bd || bd->magic != DGAP_BOARD_MAGIC)
		return;

	DGAP_LOCK(bd->bd_lock, lock_flags);
	DGAP_LOCK(ch->ch_lock, lock_flags2);

	ch->ch_c_cflag   = tty->termios->c_cflag;
	ch->ch_c_iflag   = tty->termios->c_iflag;
	ch->ch_c_oflag   = tty->termios->c_oflag;
	ch->ch_c_lflag   = tty->termios->c_lflag;
	ch->ch_startc    = tty->termios->c_cc[VSTART];
	ch->ch_stopc     = tty->termios->c_cc[VSTOP];

	dgap_carrier(ch);
	dgap_param(tty);

	DGAP_UNLOCK(ch->ch_lock, lock_flags2);
	DGAP_UNLOCK(bd->bd_lock, lock_flags);
}


static void dgap_tty_throttle(struct tty_struct *tty)
{
	struct board_t *bd;
	struct channel_t *ch;
	struct un_t *un;
	ulong   lock_flags;
	ulong   lock_flags2;

	if (!tty || tty->magic != TTY_MAGIC)
		return;

	un = tty->driver_data;
	if (!un || un->magic != DGAP_UNIT_MAGIC)
		return;
        
        ch = un->un_ch;
        if (!ch || ch->magic != DGAP_CHANNEL_MAGIC)
                return;

	bd = ch->ch_bd;
	if (!bd || bd->magic != DGAP_BOARD_MAGIC)
		return;

	DPR_IOCTL(("dgap_tty_throttle start\n"));

	DGAP_LOCK(bd->bd_lock, lock_flags);
	DGAP_LOCK(ch->ch_lock, lock_flags2);

	ch->ch_flags |= (CH_RXBLOCK);
#if 1
	dgap_cmdw(ch, RPAUSE, 0, 0);
#endif

	DGAP_UNLOCK(ch->ch_lock, lock_flags2);
	DGAP_UNLOCK(bd->bd_lock, lock_flags);

	DPR_IOCTL(("dgap_tty_throttle finish\n"));
}


static void dgap_tty_unthrottle(struct tty_struct *tty)
{
	struct board_t *bd;
	struct channel_t *ch;
	struct un_t *un;
	ulong   lock_flags;
	ulong   lock_flags2;

	if (!tty || tty->magic != TTY_MAGIC)
		return;

	un = tty->driver_data;
	if (!un || un->magic != DGAP_UNIT_MAGIC)
		return;
        
        ch = un->un_ch;
        if (!ch || ch->magic != DGAP_CHANNEL_MAGIC)
                return;

	bd = ch->ch_bd;
	if (!bd || bd->magic != DGAP_BOARD_MAGIC)
		return;

	DPR_IOCTL(("dgap_tty_unthrottle start\n"));

	DGAP_LOCK(bd->bd_lock, lock_flags);
	DGAP_LOCK(ch->ch_lock, lock_flags2);

	ch->ch_flags &= ~(CH_RXBLOCK);

#if 1
	dgap_cmdw(ch, RRESUME, 0, 0);
#endif

	DGAP_UNLOCK(ch->ch_lock, lock_flags2);
	DGAP_UNLOCK(bd->bd_lock, lock_flags);

	DPR_IOCTL(("dgap_tty_unthrottle finish\n"));
}


static void dgap_tty_start(struct tty_struct *tty)
{
	struct board_t *bd;
	struct channel_t *ch;
	struct un_t *un;
	ulong   lock_flags;
	ulong   lock_flags2;

	if (!tty || tty->magic != TTY_MAGIC)
		return;

	un = tty->driver_data;
	if (!un || un->magic != DGAP_UNIT_MAGIC)
		return;
        
        ch = un->un_ch;
        if (!ch || ch->magic != DGAP_CHANNEL_MAGIC)
                return;

	bd = ch->ch_bd;
	if (!bd || bd->magic != DGAP_BOARD_MAGIC)
		return;

	DPR_IOCTL(("dgap_tty_start start\n"));

	DGAP_LOCK(bd->bd_lock, lock_flags);
	DGAP_LOCK(ch->ch_lock, lock_flags2);

	dgap_cmdw(ch, RESUMETX, 0, 0);

	DGAP_UNLOCK(ch->ch_lock, lock_flags2);
	DGAP_UNLOCK(bd->bd_lock, lock_flags);

	DPR_IOCTL(("dgap_tty_start finish\n"));
}


static void dgap_tty_stop(struct tty_struct *tty)
{
	struct board_t *bd;
	struct channel_t *ch;
	struct un_t *un;
	ulong   lock_flags;
	ulong   lock_flags2;

	if (!tty || tty->magic != TTY_MAGIC)
		return;

	un = tty->driver_data;
	if (!un || un->magic != DGAP_UNIT_MAGIC)
		return;
        
        ch = un->un_ch;
        if (!ch || ch->magic != DGAP_CHANNEL_MAGIC)
                return;

	bd = ch->ch_bd;
	if (!bd || bd->magic != DGAP_BOARD_MAGIC)
		return;

	DPR_IOCTL(("dgap_tty_stop start\n"));

	DGAP_LOCK(bd->bd_lock, lock_flags);
	DGAP_LOCK(ch->ch_lock, lock_flags2);

	dgap_cmdw(ch, PAUSETX, 0, 0);

	DGAP_UNLOCK(ch->ch_lock, lock_flags2);
	DGAP_UNLOCK(bd->bd_lock, lock_flags);

	DPR_IOCTL(("dgap_tty_stop finish\n"));
}


/* 
 * dgap_tty_flush_chars()
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
static void dgap_tty_flush_chars(struct tty_struct *tty)
{
	struct board_t *bd;
	struct channel_t *ch;
	struct un_t *un;
	ulong   lock_flags;
	ulong   lock_flags2;

	if (!tty || tty->magic != TTY_MAGIC)
		return;

	un = tty->driver_data;
	if (!un || un->magic != DGAP_UNIT_MAGIC)
		return;
        
        ch = un->un_ch;
        if (!ch || ch->magic != DGAP_CHANNEL_MAGIC)
                return;

	bd = ch->ch_bd;
	if (!bd || bd->magic != DGAP_BOARD_MAGIC)
		return;

	DPR_IOCTL(("dgap_tty_flush_chars start\n"));

	DGAP_LOCK(bd->bd_lock, lock_flags);
	DGAP_LOCK(ch->ch_lock, lock_flags2);

	/* TODO: Do something here */

	DGAP_UNLOCK(ch->ch_lock, lock_flags2);
	DGAP_UNLOCK(bd->bd_lock, lock_flags);

	DPR_IOCTL(("dgap_tty_flush_chars finish\n"));
}



/*
 * dgap_tty_flush_buffer()
 *              
 * Flush Tx buffer (make in == out)
 */
static void dgap_tty_flush_buffer(struct tty_struct *tty)
{
	struct board_t *bd;
	struct channel_t *ch;
	struct un_t *un;
	ulong   lock_flags;
	ulong   lock_flags2;
	u16	head = 0;

	if (!tty || tty->magic != TTY_MAGIC)
		return;

	un = tty->driver_data;
	if (!un || un->magic != DGAP_UNIT_MAGIC)
		return;
        
        ch = un->un_ch;
        if (!ch || ch->magic != DGAP_CHANNEL_MAGIC)
                return;

	bd = ch->ch_bd;
	if (!bd || bd->magic != DGAP_BOARD_MAGIC)
		return;

	DPR_IOCTL(("dgap_tty_flush_buffer on port: %d start\n", ch->ch_portnum));

	DGAP_LOCK(bd->bd_lock, lock_flags);
	DGAP_LOCK(ch->ch_lock, lock_flags2);

	ch->ch_flags &= ~CH_STOP;
	head = readw(&(ch->ch_bs->tx_head));
	dgap_cmdw(ch, FLUSHTX, (u16) head, 0);
	dgap_cmdw(ch, RESUMETX, 0, 0);
	if (ch->ch_tun.un_flags & (UN_LOW|UN_EMPTY)) {
		ch->ch_tun.un_flags &= ~(UN_LOW|UN_EMPTY);
		wake_up_interruptible(&ch->ch_tun.un_flags_wait);
	}
	if (ch->ch_pun.un_flags & (UN_LOW|UN_EMPTY)) {
		ch->ch_pun.un_flags &= ~(UN_LOW|UN_EMPTY);
		wake_up_interruptible(&ch->ch_pun.un_flags_wait);
	}

	DGAP_UNLOCK(ch->ch_lock, lock_flags2);
	DGAP_UNLOCK(bd->bd_lock, lock_flags);
	if (waitqueue_active(&tty->write_wait))
		wake_up_interruptible(&tty->write_wait);
	tty_wakeup(tty);

	DPR_IOCTL(("dgap_tty_flush_buffer finish\n"));
}



/*****************************************************************************
 *
 * The IOCTL function and all of its helpers
 *
 *****************************************************************************/
                        
/*
 * dgap_tty_ioctl()
 *
 * The usual assortment of ioctl's
 */
static int dgap_tty_ioctl(struct tty_struct *tty, struct file *file, unsigned int cmd,
		unsigned long arg)
{
	struct board_t *bd;
	struct channel_t *ch;
	struct un_t *un;
	int rc;
	u16	head = 0;
	ulong   lock_flags = 0;
	ulong   lock_flags2 = 0;
	void __user *uarg = (void __user *) arg;

	if (!tty || tty->magic != TTY_MAGIC)
		return (-ENODEV);

	un = tty->driver_data;
	if (!un || un->magic != DGAP_UNIT_MAGIC)
		return (-ENODEV);

	ch = un->un_ch;
	if (!ch || ch->magic != DGAP_CHANNEL_MAGIC)
		return (-ENODEV);

	bd = ch->ch_bd;
	if (!bd || bd->magic != DGAP_BOARD_MAGIC)
		return (-ENODEV);

	DPR_IOCTL(("dgap_tty_ioctl start on port %d - cmd %s (%x), arg %lx\n", 
		ch->ch_portnum, dgap_ioctl_name(cmd), cmd, arg));

	DGAP_LOCK(bd->bd_lock, lock_flags);
	DGAP_LOCK(ch->ch_lock, lock_flags2);

	if (un->un_open_count <= 0) {
		DPR_BASIC(("dgap_tty_ioctl - unit not open.\n"));
		DGAP_UNLOCK(ch->ch_lock, lock_flags2);
		DGAP_UNLOCK(bd->bd_lock, lock_flags);
		return(-EIO);
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
		DGAP_UNLOCK(ch->ch_lock, lock_flags2);
		DGAP_UNLOCK(bd->bd_lock, lock_flags);
		if (rc) {
			return(rc);
		}

		rc = dgap_wait_for_drain(tty);

		if (rc) {
			DPR_IOCTL(("dgap_tty_ioctl - bad return: %d ", rc));
			return(-EINTR);
		}

		DGAP_LOCK(bd->bd_lock, lock_flags);
		DGAP_LOCK(ch->ch_lock, lock_flags2);

		if(((cmd == TCSBRK) && (!arg)) || (cmd == TCSBRKP)) {
			dgap_cmdw(ch, SBREAK, (u16) SBREAK_TIME, 0);
		}

		DGAP_UNLOCK(ch->ch_lock, lock_flags2);
		DGAP_UNLOCK(bd->bd_lock, lock_flags);

		DPR_IOCTL(("dgap_tty_ioctl finish on port %d - cmd %s (%x), arg %lx\n", 
			ch->ch_portnum, dgap_ioctl_name(cmd), cmd, arg));

                return(0);


	case TCSBRKP:
 		/* support for POSIX tcsendbreak()

		 * According to POSIX.1 spec (7.2.2.1.2) breaks should be
		 * between 0.25 and 0.5 seconds so we'll ask for something
		 * in the middle: 0.375 seconds.
		 */
		rc = tty_check_change(tty);
		DGAP_UNLOCK(ch->ch_lock, lock_flags2);
		DGAP_UNLOCK(bd->bd_lock, lock_flags);
		if (rc) {
			return(rc);
		}

		rc = dgap_wait_for_drain(tty);
		if (rc) {
			DPR_IOCTL(("dgap_tty_ioctl - bad return: %d ", rc));
			return(-EINTR);
		}

		DGAP_LOCK(bd->bd_lock, lock_flags);
		DGAP_LOCK(ch->ch_lock, lock_flags2);

		dgap_cmdw(ch, SBREAK, (u16) SBREAK_TIME, 0);

		DGAP_UNLOCK(ch->ch_lock, lock_flags2);
		DGAP_UNLOCK(bd->bd_lock, lock_flags);

		DPR_IOCTL(("dgap_tty_ioctl finish on port %d - cmd %s (%x), arg %lx\n", 
			ch->ch_portnum, dgap_ioctl_name(cmd), cmd, arg));

		return(0);

        case TIOCSBRK:
		/*
		 * FEP5 doesn't support turning on a break unconditionally.
		 * The FEP5 device will stop sending a break automatically
		 * after the specified time value that was sent when turning on
		 * the break.
		 */
		rc = tty_check_change(tty);
		DGAP_UNLOCK(ch->ch_lock, lock_flags2);
		DGAP_UNLOCK(bd->bd_lock, lock_flags);
		if (rc) {
			return(rc);
		}

		rc = dgap_wait_for_drain(tty);
		if (rc) {
			DPR_IOCTL(("dgap_tty_ioctl - bad return: %d ", rc));
			return(-EINTR);
		}

		DGAP_LOCK(bd->bd_lock, lock_flags);
		DGAP_LOCK(ch->ch_lock, lock_flags2);

		dgap_cmdw(ch, SBREAK, (u16) SBREAK_TIME, 0);

		DGAP_UNLOCK(ch->ch_lock, lock_flags2);
		DGAP_UNLOCK(bd->bd_lock, lock_flags);

		DPR_IOCTL(("dgap_tty_ioctl finish on port %d - cmd %s (%x), arg %lx\n", 
			ch->ch_portnum, dgap_ioctl_name(cmd), cmd, arg));

		return 0;
                
        case TIOCCBRK:
		/*
		 * FEP5 doesn't support turning off a break unconditionally.
		 * The FEP5 device will stop sending a break automatically
		 * after the specified time value that was sent when turning on
		 * the break.
		 */
		DGAP_UNLOCK(ch->ch_lock, lock_flags2);
		DGAP_UNLOCK(bd->bd_lock, lock_flags);
		return 0;

	case TIOCGSOFTCAR:

		DGAP_UNLOCK(ch->ch_lock, lock_flags2);
		DGAP_UNLOCK(bd->bd_lock, lock_flags);

		rc = put_user(C_CLOCAL(tty) ? 1 : 0, (unsigned long __user *) arg);
		return(rc);

	case TIOCSSOFTCAR:
		DGAP_UNLOCK(ch->ch_lock, lock_flags2);
		DGAP_UNLOCK(bd->bd_lock, lock_flags);

		rc = get_user(arg, (unsigned long __user *) arg);
		if (rc)
			return(rc);

		DGAP_LOCK(bd->bd_lock, lock_flags);
		DGAP_LOCK(ch->ch_lock, lock_flags2);
		tty->termios->c_cflag = ((tty->termios->c_cflag & ~CLOCAL) | (arg ? CLOCAL : 0));
		dgap_param(tty);
		DGAP_UNLOCK(ch->ch_lock, lock_flags2);
		DGAP_UNLOCK(bd->bd_lock, lock_flags);

		return(0);
                        
	case TIOCMGET:
		DGAP_UNLOCK(ch->ch_lock, lock_flags2);
		DGAP_UNLOCK(bd->bd_lock, lock_flags);
                return(dgap_get_modem_info(ch, uarg));

	case TIOCMBIS:
	case TIOCMBIC:
	case TIOCMSET:
		DGAP_UNLOCK(ch->ch_lock, lock_flags2);
		DGAP_UNLOCK(bd->bd_lock, lock_flags);
		return(dgap_set_modem_info(tty, cmd, uarg));

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
			DGAP_UNLOCK(ch->ch_lock, lock_flags2);
			DGAP_UNLOCK(bd->bd_lock, lock_flags);
			return(rc);
		}

		if ((arg == TCIFLUSH) || (arg == TCIOFLUSH)) {
			if (!(un->un_type == DGAP_PRINT)) {
				head = readw(&(ch->ch_bs->rx_head));
				writew(head, &(ch->ch_bs->rx_tail));
				writeb(0, &(ch->ch_bs->orun));
			}
		}

		if ((arg == TCOFLUSH) || (arg == TCIOFLUSH)) {
			ch->ch_flags &= ~CH_STOP;
			head = readw(&(ch->ch_bs->tx_head));
			dgap_cmdw(ch, FLUSHTX, (u16) head, 0 );
			dgap_cmdw(ch, RESUMETX, 0, 0);
			if (ch->ch_tun.un_flags & (UN_LOW|UN_EMPTY)) {
				ch->ch_tun.un_flags &= ~(UN_LOW|UN_EMPTY);
				wake_up_interruptible(&ch->ch_tun.un_flags_wait);
			}
			if (ch->ch_pun.un_flags & (UN_LOW|UN_EMPTY)) {
				ch->ch_pun.un_flags &= ~(UN_LOW|UN_EMPTY);
				wake_up_interruptible(&ch->ch_pun.un_flags_wait);
			}
			if (waitqueue_active(&tty->write_wait))
				wake_up_interruptible(&tty->write_wait);

			/* Can't hold any locks when calling tty_wakeup! */
			DGAP_UNLOCK(ch->ch_lock, lock_flags2);
			DGAP_UNLOCK(bd->bd_lock, lock_flags);
			tty_wakeup(tty);
			DGAP_LOCK(bd->bd_lock, lock_flags);
			DGAP_LOCK(ch->ch_lock, lock_flags2);
		}                  

		/* pretend we didn't recognize this IOCTL */  
		DGAP_UNLOCK(ch->ch_lock, lock_flags2);
		DGAP_UNLOCK(bd->bd_lock, lock_flags);

		DPR_IOCTL(("dgap_tty_ioctl (LINE:%d) finish on port %d - cmd %s (%x), arg %lx\n", 
			__LINE__, ch->ch_portnum, dgap_ioctl_name(cmd), cmd, arg));

		return(-ENOIOCTLCMD);

#ifdef TIOCGETP
	case TIOCGETP:
#endif
	case TCGETS:
	case TCGETA:
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,31)
		if (tty->ldisc->ops->ioctl) {
#else
		if (tty->ldisc.ops->ioctl) {
#endif
			int retval = (-ENXIO);

			DGAP_UNLOCK(ch->ch_lock, lock_flags2);
			DGAP_UNLOCK(bd->bd_lock, lock_flags);

			if (tty->termios) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,31)
				retval = ((tty->ldisc->ops->ioctl) (tty, file, cmd, arg));
#else
				retval = ((tty->ldisc.ops->ioctl) (tty, file, cmd, arg));
#endif
			}

			DPR_IOCTL(("dgap_tty_ioctl (LINE:%d) finish on port %d - cmd %s (%x), arg %lx\n", 
				__LINE__, ch->ch_portnum, dgap_ioctl_name(cmd), cmd, arg));
			return(retval);
		}

		DGAP_UNLOCK(ch->ch_lock, lock_flags2);
		DGAP_UNLOCK(bd->bd_lock, lock_flags);
		DPR_IOCTL(("dgap_tty_ioctl (LINE:%d) finish on port %d - cmd %s (%x), arg %lx\n", 
			__LINE__, ch->ch_portnum, dgap_ioctl_name(cmd), cmd, arg));

		return(-ENOIOCTLCMD);

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
			head = readw(&(ch->ch_bs->rx_head));
			writew(head, &(ch->ch_bs->rx_tail));
		}

		/* now wait for all the output to drain */
		DGAP_UNLOCK(ch->ch_lock, lock_flags2);
		DGAP_UNLOCK(bd->bd_lock, lock_flags);
		rc = dgap_wait_for_drain(tty);
		if (rc) {
			DPR_IOCTL(("dgap_tty_ioctl - bad return: %d ", rc));
			return(-EINTR);
		}

		DPR_IOCTL(("dgap_tty_ioctl finish on port %d - cmd %s (%x), arg %lx\n", 
			ch->ch_portnum, dgap_ioctl_name(cmd), cmd, arg));

		/* pretend we didn't recognize this */
		return(-ENOIOCTLCMD);

	case TCSETAW:

		DGAP_UNLOCK(ch->ch_lock, lock_flags2);
		DGAP_UNLOCK(bd->bd_lock, lock_flags);
		rc = dgap_wait_for_drain(tty);
		if (rc) {
			DPR_IOCTL(("dgap_tty_ioctl - bad return: %d ", rc));
			return(-EINTR);
		}

		/* pretend we didn't recognize this */
		return(-ENOIOCTLCMD);  

	case TCXONC:
		/*
		 * The Linux Line Discipline (LD) would do this for us if we
		 * let it, but we have the special firmware options to do this
		 * the "right way" regardless of hardware or software flow
		 * control so we'll do it outselves instead of letting the LD
		 * do it.
		 */
		rc = tty_check_change(tty);
		if (rc) {
			DGAP_UNLOCK(ch->ch_lock, lock_flags2);
			DGAP_UNLOCK(bd->bd_lock, lock_flags);
			return(rc);
		}

		DPR_IOCTL(("dgap_ioctl - in TCXONC - %d\n", cmd));
		switch (arg) {

		case TCOON:
			DGAP_UNLOCK(ch->ch_lock, lock_flags2);
			DGAP_UNLOCK(bd->bd_lock, lock_flags);
			dgap_tty_start(tty);
			return(0);
		case TCOOFF:
			DGAP_UNLOCK(ch->ch_lock, lock_flags2);
			DGAP_UNLOCK(bd->bd_lock, lock_flags);
			dgap_tty_stop(tty);
			return(0);
		case TCION:
			DGAP_UNLOCK(ch->ch_lock, lock_flags2);
			DGAP_UNLOCK(bd->bd_lock, lock_flags);
			/* Make the ld do it */
			return(-ENOIOCTLCMD);
		case TCIOFF:
			DGAP_UNLOCK(ch->ch_lock, lock_flags2);
			DGAP_UNLOCK(bd->bd_lock, lock_flags);
			/* Make the ld do it */
			return(-ENOIOCTLCMD);
		default:
			DGAP_UNLOCK(ch->ch_lock, lock_flags2);
			DGAP_UNLOCK(bd->bd_lock, lock_flags);
			return(-EINVAL);
		}

		DGAP_UNLOCK(ch->ch_lock, lock_flags2);
		DGAP_UNLOCK(bd->bd_lock, lock_flags);
		return(-ENOIOCTLCMD);

	case DIGI_GETA:
		/* get information for ditty */
		DGAP_UNLOCK(ch->ch_lock, lock_flags2);
		DGAP_UNLOCK(bd->bd_lock, lock_flags);
		return(dgap_tty_digigeta(tty, uarg));

	case DIGI_SETAW:
	case DIGI_SETAF:

		/* set information for ditty */
		if (cmd == (DIGI_SETAW)) {

			DGAP_UNLOCK(ch->ch_lock, lock_flags2);
			DGAP_UNLOCK(bd->bd_lock, lock_flags);
			rc = dgap_wait_for_drain(tty);
			if (rc) {
				DPR_IOCTL(("dgap_tty_ioctl - bad return: %d ", rc));
				return(-EINTR);
			}
			DGAP_LOCK(bd->bd_lock, lock_flags);
			DGAP_LOCK(ch->ch_lock, lock_flags2);
		}
		else {
			tty_ldisc_flush(tty);
		}
		/* fall thru */

	case DIGI_SETA:
		DGAP_UNLOCK(ch->ch_lock, lock_flags2);
		DGAP_UNLOCK(bd->bd_lock, lock_flags);
		return(dgap_tty_digiseta(tty, uarg));

	case DIGI_GEDELAY:
		DGAP_UNLOCK(ch->ch_lock, lock_flags2);
		DGAP_UNLOCK(bd->bd_lock, lock_flags);
		return(dgap_tty_digigetedelay(tty, uarg));

	case DIGI_SEDELAY:
		DGAP_UNLOCK(ch->ch_lock, lock_flags2);
		DGAP_UNLOCK(bd->bd_lock, lock_flags);
		return(dgap_tty_digisetedelay(tty, uarg));

	case DIGI_GETCUSTOMBAUD:
		DGAP_UNLOCK(ch->ch_lock, lock_flags2);
		DGAP_UNLOCK(bd->bd_lock, lock_flags);
		return(dgap_tty_digigetcustombaud(tty, uarg));

	case DIGI_SETCUSTOMBAUD:
		DGAP_UNLOCK(ch->ch_lock, lock_flags2);
		DGAP_UNLOCK(bd->bd_lock, lock_flags);
		return(dgap_tty_digisetcustombaud(tty, uarg));

	case DIGI_RESET_PORT:
		dgap_firmware_reset_port(ch);
		dgap_param(tty);
		DGAP_UNLOCK(ch->ch_lock, lock_flags2);
		DGAP_UNLOCK(bd->bd_lock, lock_flags);
		return 0;

	default:
		DGAP_UNLOCK(ch->ch_lock, lock_flags2);
		DGAP_UNLOCK(bd->bd_lock, lock_flags);

		DPR_IOCTL(("dgap_tty_ioctl - in default\n"));
		DPR_IOCTL(("dgap_tty_ioctl end - cmd %s (%x), arg %lx\n", 
			dgap_ioctl_name(cmd), cmd, arg));

		return(-ENOIOCTLCMD);
	}

	DGAP_UNLOCK(ch->ch_lock, lock_flags2);
	DGAP_UNLOCK(bd->bd_lock, lock_flags);

	DPR_IOCTL(("dgap_tty_ioctl end - cmd %s (%x), arg %lx\n", 
		dgap_ioctl_name(cmd), cmd, arg));
                        
	return(0);
}
