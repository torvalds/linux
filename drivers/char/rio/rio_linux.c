
/* rio_linux.c -- Linux driver for the Specialix RIO series cards. 
 *
 *
 *   (C) 1999 R.E.Wolff@BitWizard.nl
 *
 * Specialix pays for the development and support of this driver.
 * Please DO contact support@specialix.co.uk if you require
 * support. But please read the documentation (rio.txt) first.
 *
 *
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
 * $Log: rio.c,v $
 * Revision 1.1  1999/07/11 10:13:54  wolff
 * Initial revision
 *
 * */

#include <linux/module.h>
#include <linux/kdev_t.h>
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
#include <linux/slab.h>
#include <linux/miscdevice.h>
#include <linux/init.h>

#include <linux/generic_serial.h>
#include <asm/uaccess.h>

#include "linux_compat.h"
#include "pkt.h"
#include "daemon.h"
#include "rio.h"
#include "riospace.h"
#include "cmdpkt.h"
#include "map.h"
#include "rup.h"
#include "port.h"
#include "riodrvr.h"
#include "rioinfo.h"
#include "func.h"
#include "errors.h"
#include "pci.h"

#include "parmmap.h"
#include "unixrup.h"
#include "board.h"
#include "host.h"
#include "phb.h"
#include "link.h"
#include "cmdblk.h"
#include "route.h"
#include "cirrus.h"
#include "rioioctl.h"
#include "param.h"
#include "protsts.h"
#include "rioboard.h"


#include "rio_linux.h"

/* I don't think that this driver can handle more than 512 ports on
one machine.  Specialix specifies max 4 boards in one machine. I don't
know why. If you want to try anyway you'll have to increase the number
of boards in rio.h.  You'll have to allocate more majors if you need
more than 512 ports.... */

#ifndef RIO_NORMAL_MAJOR0
/* This allows overriding on the compiler commandline, or in a "major.h" 
   include or something like that */
#define RIO_NORMAL_MAJOR0  154
#define RIO_NORMAL_MAJOR1  156
#endif

#ifndef PCI_DEVICE_ID_SPECIALIX_SX_XIO_IO8
#define PCI_DEVICE_ID_SPECIALIX_SX_XIO_IO8 0x2000
#endif

#ifndef RIO_WINDOW_LEN
#define RIO_WINDOW_LEN 0x10000
#endif


/* Configurable options: 
   (Don't be too sure that it'll work if you toggle them) */

/* Am I paranoid or not ? ;-) */
#undef RIO_PARANOIA_CHECK


/* 20 -> 2000 per second. The card should rate-limit interrupts at 1000
   Hz, but it is user configurable. I don't recommend going above 1000
   Hz. The interrupt ratelimit might trigger if the interrupt is
   shared with a very active other device. 
   undef this if you want to disable the check....
*/
#define IRQ_RATE_LIMIT 200


/* These constants are derived from SCO Source */
static struct Conf
 RIOConf = {
	/* locator */ "RIO Config here",
					/* startuptime */ HZ * 2,
					/* how long to wait for card to run */
				/* slowcook */ 0,
				/* TRUE -> always use line disc. */
				/* intrpolltime */ 1,
				/* The frequency of OUR polls */
				/* breakinterval */ 25,
				/* x10 mS XXX: units seem to be 1ms not 10! -- REW */
				/* timer */ 10,
				/* mS */
	/* RtaLoadBase */ 0x7000,
	/* HostLoadBase */ 0x7C00,
				/* XpHz */ 5,
				/* number of Xprint hits per second */
				/* XpCps */ 120,
				/* Xprint characters per second */
				/* XpOn */ "\033d#",
				/* start Xprint for a wyse 60 */
				/* XpOff */ "\024",
				/* end Xprint for a wyse 60 */
				/* MaxXpCps */ 2000,
				/* highest Xprint speed */
				/* MinXpCps */ 10,
				/* slowest Xprint speed */
				/* SpinCmds */ 1,
				/* non-zero for mega fast boots */
					/* First Addr */ 0x0A0000,
					/* First address to look at */
					/* Last Addr */ 0xFF0000,
					/* Last address looked at */
				/* BufferSize */ 1024,
				/* Bytes per port of buffering */
				/* LowWater */ 256,
				/* how much data left before wakeup */
				/* LineLength */ 80,
				/* how wide is the console? */
				/* CmdTimeout */ HZ,
				/* how long a close command may take */
};




/* Function prototypes */

static void rio_disable_tx_interrupts(void *ptr);
static void rio_enable_tx_interrupts(void *ptr);
static void rio_disable_rx_interrupts(void *ptr);
static void rio_enable_rx_interrupts(void *ptr);
static int rio_get_CD(void *ptr);
static void rio_shutdown_port(void *ptr);
static int rio_set_real_termios(void *ptr);
static void rio_hungup(void *ptr);
static void rio_close(void *ptr);
static int rio_chars_in_buffer(void *ptr);
static int rio_fw_ioctl(struct inode *inode, struct file *filp, unsigned int cmd, unsigned long arg);
static int rio_init_drivers(void);

static void my_hd(void *addr, int len);

static struct tty_driver *rio_driver, *rio_driver2;

/* The name "p" is a bit non-descript. But that's what the rio-lynxos
sources use all over the place. */
struct rio_info *p;

int rio_debug;


/* You can have the driver poll your card. 
    - Set rio_poll to 1 to poll every timer tick (10ms on Intel). 
      This is used when the card cannot use an interrupt for some reason.
*/
static int rio_poll = 1;


/* These are the only open spaces in my computer. Yours may have more
   or less.... */
static int rio_probe_addrs[] = { 0xc0000, 0xd0000, 0xe0000 };

#define NR_RIO_ADDRS ARRAY_SIZE(rio_probe_addrs)


/* Set the mask to all-ones. This alas, only supports 32 interrupts. 
   Some architectures may need more. -- Changed to LONG to
   support up to 64 bits on 64bit architectures. -- REW 20/06/99 */
static long rio_irqmask = -1;

MODULE_AUTHOR("Rogier Wolff <R.E.Wolff@bitwizard.nl>, Patrick van de Lageweg <patrick@bitwizard.nl>");
MODULE_DESCRIPTION("RIO driver");
MODULE_LICENSE("GPL");
module_param(rio_poll, int, 0);
module_param(rio_debug, int, 0644);
module_param(rio_irqmask, long, 0);

static struct real_driver rio_real_driver = {
	rio_disable_tx_interrupts,
	rio_enable_tx_interrupts,
	rio_disable_rx_interrupts,
	rio_enable_rx_interrupts,
	rio_get_CD,
	rio_shutdown_port,
	rio_set_real_termios,
	rio_chars_in_buffer,
	rio_close,
	rio_hungup,
	NULL
};

/* 
 *  Firmware loader driver specific routines
 *
 */

static const struct file_operations rio_fw_fops = {
	.owner = THIS_MODULE,
	.ioctl = rio_fw_ioctl,
};

static struct miscdevice rio_fw_device = {
	RIOCTL_MISC_MINOR, "rioctl", &rio_fw_fops
};





#ifdef RIO_PARANOIA_CHECK

/* This doesn't work. Who's paranoid around here? Not me! */

static inline int rio_paranoia_check(struct rio_port const *port, char *name, const char *routine)
{

	static const char *badmagic = KERN_ERR "rio: Warning: bad rio port magic number for device %s in %s\n";
	static const char *badinfo = KERN_ERR "rio: Warning: null rio port for device %s in %s\n";

	if (!port) {
		printk(badinfo, name, routine);
		return 1;
	}
	if (port->magic != RIO_MAGIC) {
		printk(badmagic, name, routine);
		return 1;
	}

	return 0;
}
#else
#define rio_paranoia_check(a,b,c) 0
#endif


#ifdef DEBUG
static void my_hd(void *ad, int len)
{
	int i, j, ch;
	unsigned char *addr = ad;

	for (i = 0; i < len; i += 16) {
		rio_dprintk(RIO_DEBUG_PARAM, "%08lx ", (unsigned long) addr + i);
		for (j = 0; j < 16; j++) {
			rio_dprintk(RIO_DEBUG_PARAM, "%02x %s", addr[j + i], (j == 7) ? " " : "");
		}
		for (j = 0; j < 16; j++) {
			ch = addr[j + i];
			rio_dprintk(RIO_DEBUG_PARAM, "%c", (ch < 0x20) ? '.' : ((ch > 0x7f) ? '.' : ch));
		}
		rio_dprintk(RIO_DEBUG_PARAM, "\n");
	}
}
#else
#define my_hd(ad,len) do{/* nothing*/ } while (0)
#endif


/* Delay a number of jiffies, allowing a signal to interrupt */
int RIODelay(struct Port *PortP, int njiffies)
{
	func_enter();

	rio_dprintk(RIO_DEBUG_DELAY, "delaying %d jiffies\n", njiffies);
	msleep_interruptible(jiffies_to_msecs(njiffies));
	func_exit();

	if (signal_pending(current))
		return RIO_FAIL;
	else
		return !RIO_FAIL;
}


/* Delay a number of jiffies, disallowing a signal to interrupt */
int RIODelay_ni(struct Port *PortP, int njiffies)
{
	func_enter();

	rio_dprintk(RIO_DEBUG_DELAY, "delaying %d jiffies (ni)\n", njiffies);
	msleep(jiffies_to_msecs(njiffies));
	func_exit();
	return !RIO_FAIL;
}

void rio_copy_to_card(void *from, void __iomem *to, int len)
{
	rio_copy_toio(to, from, len);
}

int rio_minor(struct tty_struct *tty)
{
	return tty->index + (tty->driver == rio_driver) ? 0 : 256;
}

static int rio_set_real_termios(void *ptr)
{
	return RIOParam((struct Port *) ptr, CONFIG, 1, 1);
}


static void rio_reset_interrupt(struct Host *HostP)
{
	func_enter();

	switch (HostP->Type) {
	case RIO_AT:
	case RIO_MCA:
	case RIO_PCI:
		writeb(0xFF, &HostP->ResetInt);
	}

	func_exit();
}


static irqreturn_t rio_interrupt(int irq, void *ptr)
{
	struct Host *HostP;
	func_enter();

	HostP = ptr;			/* &p->RIOHosts[(long)ptr]; */
	rio_dprintk(RIO_DEBUG_IFLOW, "rio: enter rio_interrupt (%d/%d)\n", irq, HostP->Ivec);

	/* AAargh! The order in which to do these things is essential and
	   not trivial.

	   - hardware twiddling goes before "recursive". Otherwise when we
	   poll the card, and a recursive interrupt happens, we won't
	   ack the card, so it might keep on interrupting us. (especially
	   level sensitive interrupt systems like PCI).

	   - Rate limit goes before hardware twiddling. Otherwise we won't
	   catch a card that has gone bonkers.

	   - The "initialized" test goes after the hardware twiddling. Otherwise
	   the card will stick us in the interrupt routine again.

	   - The initialized test goes before recursive.
	 */

	rio_dprintk(RIO_DEBUG_IFLOW, "rio: We've have noticed the interrupt\n");
	if (HostP->Ivec == irq) {
		/* Tell the card we've noticed the interrupt. */
		rio_reset_interrupt(HostP);
	}

	if ((HostP->Flags & RUN_STATE) != RC_RUNNING)
		return IRQ_HANDLED;

	if (test_and_set_bit(RIO_BOARD_INTR_LOCK, &HostP->locks)) {
		printk(KERN_ERR "Recursive interrupt! (host %p/irq%d)\n", ptr, HostP->Ivec);
		return IRQ_HANDLED;
	}

	RIOServiceHost(p, HostP);

	rio_dprintk(RIO_DEBUG_IFLOW, "riointr() doing host %p type %d\n", ptr, HostP->Type);

	clear_bit(RIO_BOARD_INTR_LOCK, &HostP->locks);
	rio_dprintk(RIO_DEBUG_IFLOW, "rio: exit rio_interrupt (%d/%d)\n", irq, HostP->Ivec);
	func_exit();
	return IRQ_HANDLED;
}


static void rio_pollfunc(unsigned long data)
{
	func_enter();

	rio_interrupt(0, &p->RIOHosts[data]);
	mod_timer(&p->RIOHosts[data].timer, jiffies + rio_poll);

	func_exit();
}


/* ********************************************************************** *
 *                Here are the routines that actually                     *
 *              interface with the generic_serial driver                  *
 * ********************************************************************** */

/* Ehhm. I don't know how to fiddle with interrupts on the Specialix 
   cards. ....   Hmm. Ok I figured it out. You don't.  -- REW */

static void rio_disable_tx_interrupts(void *ptr)
{
	func_enter();

	/*  port->gs.flags &= ~GS_TX_INTEN; */

	func_exit();
}


static void rio_enable_tx_interrupts(void *ptr)
{
	struct Port *PortP = ptr;
	/* int hn; */

	func_enter();

	/* hn = PortP->HostP - p->RIOHosts;

	   rio_dprintk (RIO_DEBUG_TTY, "Pushing host %d\n", hn);
	   rio_interrupt (-1,(void *) hn, NULL); */

	RIOTxEnable((char *) PortP);

	/*
	 * In general we cannot count on "tx empty" interrupts, although
	 * the interrupt routine seems to be able to tell the difference.
	 */
	PortP->gs.flags &= ~GS_TX_INTEN;

	func_exit();
}


static void rio_disable_rx_interrupts(void *ptr)
{
	func_enter();
	func_exit();
}

static void rio_enable_rx_interrupts(void *ptr)
{
	/*  struct rio_port *port = ptr; */
	func_enter();
	func_exit();
}


/* Jeez. Isn't this simple?  */
static int rio_get_CD(void *ptr)
{
	struct Port *PortP = ptr;
	int rv;

	func_enter();
	rv = (PortP->ModemState & MSVR1_CD) != 0;

	rio_dprintk(RIO_DEBUG_INIT, "Getting CD status: %d\n", rv);

	func_exit();
	return rv;
}


/* Jeez. Isn't this simple? Actually, we can sync with the actual port
   by just pushing stuff into the queue going to the port... */
static int rio_chars_in_buffer(void *ptr)
{
	func_enter();

	func_exit();
	return 0;
}


/* Nothing special here... */
static void rio_shutdown_port(void *ptr)
{
	struct Port *PortP;

	func_enter();

	PortP = (struct Port *) ptr;
	PortP->gs.tty = NULL;
	func_exit();
}


/* I haven't the foggiest why the decrement use count has to happen
   here. The whole linux serial drivers stuff needs to be redesigned.
   My guess is that this is a hack to minimize the impact of a bug
   elsewhere. Thinking about it some more. (try it sometime) Try
   running minicom on a serial port that is driven by a modularized
   driver. Have the modem hangup. Then remove the driver module. Then
   exit minicom.  I expect an "oops".  -- REW */
static void rio_hungup(void *ptr)
{
	struct Port *PortP;

	func_enter();

	PortP = (struct Port *) ptr;
	PortP->gs.tty = NULL;

	func_exit();
}


/* The standard serial_close would become shorter if you'd wrap it like
   this. 
   rs_close (...){save_flags;cli;real_close();dec_use_count;restore_flags;}
 */
static void rio_close(void *ptr)
{
	struct Port *PortP;

	func_enter();

	PortP = (struct Port *) ptr;

	riotclose(ptr);

	if (PortP->gs.count) {
		printk(KERN_ERR "WARNING port count:%d\n", PortP->gs.count);
		PortP->gs.count = 0;
	}

	PortP->gs.tty = NULL;
	func_exit();
}



static int rio_fw_ioctl(struct inode *inode, struct file *filp, unsigned int cmd, unsigned long arg)
{
	int rc = 0;
	func_enter();

	/* The "dev" argument isn't used. */
	rc = riocontrol(p, 0, cmd, arg, capable(CAP_SYS_ADMIN));

	func_exit();
	return rc;
}

extern int RIOShortCommand(struct rio_info *p, struct Port *PortP, int command, int len, int arg);

static int rio_ioctl(struct tty_struct *tty, struct file *filp, unsigned int cmd, unsigned long arg)
{
	void __user *argp = (void __user *)arg;
	int rc;
	struct Port *PortP;
	int ival;

	func_enter();

	PortP = (struct Port *) tty->driver_data;

	rc = 0;
	switch (cmd) {
	case TIOCSSOFTCAR:
		if ((rc = get_user(ival, (unsigned __user *) argp)) == 0) {
			tty->termios->c_cflag = (tty->termios->c_cflag & ~CLOCAL) | (ival ? CLOCAL : 0);
		}
		break;
	case TIOCGSERIAL:
		rc = -EFAULT;
		if (access_ok(VERIFY_WRITE, argp, sizeof(struct serial_struct)))
			rc = gs_getserial(&PortP->gs, argp);
		break;
	case TCSBRK:
		if (PortP->State & RIO_DELETED) {
			rio_dprintk(RIO_DEBUG_TTY, "BREAK on deleted RTA\n");
			rc = -EIO;
		} else {
			if (RIOShortCommand(p, PortP, SBREAK, 2, 250) == RIO_FAIL) {
				rio_dprintk(RIO_DEBUG_INTR, "SBREAK RIOShortCommand failed\n");
				rc = -EIO;
			}
		}
		break;
	case TCSBRKP:
		if (PortP->State & RIO_DELETED) {
			rio_dprintk(RIO_DEBUG_TTY, "BREAK on deleted RTA\n");
			rc = -EIO;
		} else {
			int l;
			l = arg ? arg * 100 : 250;
			if (l > 255)
				l = 255;
			if (RIOShortCommand(p, PortP, SBREAK, 2, arg ? arg * 100 : 250) == RIO_FAIL) {
				rio_dprintk(RIO_DEBUG_INTR, "SBREAK RIOShortCommand failed\n");
				rc = -EIO;
			}
		}
		break;
	case TIOCSSERIAL:
		rc = -EFAULT;
		if (access_ok(VERIFY_READ, argp, sizeof(struct serial_struct)))
			rc = gs_setserial(&PortP->gs, argp);
		break;
	default:
		rc = -ENOIOCTLCMD;
		break;
	}
	func_exit();
	return rc;
}


/* The throttle/unthrottle scheme for the Specialix card is different
 * from other drivers and deserves some explanation. 
 * The Specialix hardware takes care of XON/XOFF
 * and CTS/RTS flow control itself.  This means that all we have to
 * do when signalled by the upper tty layer to throttle/unthrottle is
 * to make a note of it here.  When we come to read characters from the
 * rx buffers on the card (rio_receive_chars()) we look to see if the
 * upper layer can accept more (as noted here in rio_rx_throt[]). 
 * If it can't we simply don't remove chars from the cards buffer. 
 * When the tty layer can accept chars, we again note that here and when
 * rio_receive_chars() is called it will remove them from the cards buffer.
 * The card will notice that a ports buffer has drained below some low
 * water mark and will unflow control the line itself, using whatever
 * flow control scheme is in use for that port. -- Simon Allen
 */

static void rio_throttle(struct tty_struct *tty)
{
	struct Port *port = (struct Port *) tty->driver_data;

	func_enter();
	/* If the port is using any type of input flow
	 * control then throttle the port.
	 */

	if ((tty->termios->c_cflag & CRTSCTS) || (I_IXOFF(tty))) {
		port->State |= RIO_THROTTLE_RX;
	}

	func_exit();
}


static void rio_unthrottle(struct tty_struct *tty)
{
	struct Port *port = (struct Port *) tty->driver_data;

	func_enter();
	/* Always unthrottle even if flow control is not enabled on
	 * this port in case we disabled flow control while the port
	 * was throttled
	 */

	port->State &= ~RIO_THROTTLE_RX;

	func_exit();
	return;
}





/* ********************************************************************** *
 *                    Here are the initialization routines.               *
 * ********************************************************************** */


static struct vpd_prom *get_VPD_PROM(struct Host *hp)
{
	static struct vpd_prom vpdp;
	char *p;
	int i;

	func_enter();
	rio_dprintk(RIO_DEBUG_PROBE, "Going to verify vpd prom at %p.\n", hp->Caddr + RIO_VPD_ROM);

	p = (char *) &vpdp;
	for (i = 0; i < sizeof(struct vpd_prom); i++)
		*p++ = readb(hp->Caddr + RIO_VPD_ROM + i * 2);
	/* read_rio_byte (hp, RIO_VPD_ROM + i*2); */

	/* Terminate the identifier string.
	 *** requires one extra byte in struct vpd_prom *** */
	*p++ = 0;

	if (rio_debug & RIO_DEBUG_PROBE)
		my_hd((char *) &vpdp, 0x20);

	func_exit();

	return &vpdp;
}

static const struct tty_operations rio_ops = {
	.open = riotopen,
	.close = gs_close,
	.write = gs_write,
	.put_char = gs_put_char,
	.flush_chars = gs_flush_chars,
	.write_room = gs_write_room,
	.chars_in_buffer = gs_chars_in_buffer,
	.flush_buffer = gs_flush_buffer,
	.ioctl = rio_ioctl,
	.throttle = rio_throttle,
	.unthrottle = rio_unthrottle,
	.set_termios = gs_set_termios,
	.stop = gs_stop,
	.start = gs_start,
	.hangup = gs_hangup,
};

static int rio_init_drivers(void)
{
	int error = -ENOMEM;

	rio_driver = alloc_tty_driver(256);
	if (!rio_driver)
		goto out;
	rio_driver2 = alloc_tty_driver(256);
	if (!rio_driver2)
		goto out1;

	func_enter();

	rio_driver->owner = THIS_MODULE;
	rio_driver->driver_name = "specialix_rio";
	rio_driver->name = "ttySR";
	rio_driver->major = RIO_NORMAL_MAJOR0;
	rio_driver->type = TTY_DRIVER_TYPE_SERIAL;
	rio_driver->subtype = SERIAL_TYPE_NORMAL;
	rio_driver->init_termios = tty_std_termios;
	rio_driver->init_termios.c_cflag = B9600 | CS8 | CREAD | HUPCL | CLOCAL;
	rio_driver->flags = TTY_DRIVER_REAL_RAW;
	tty_set_operations(rio_driver, &rio_ops);

	rio_driver2->owner = THIS_MODULE;
	rio_driver2->driver_name = "specialix_rio";
	rio_driver2->name = "ttySR";
	rio_driver2->major = RIO_NORMAL_MAJOR1;
	rio_driver2->type = TTY_DRIVER_TYPE_SERIAL;
	rio_driver2->subtype = SERIAL_TYPE_NORMAL;
	rio_driver2->init_termios = tty_std_termios;
	rio_driver2->init_termios.c_cflag = B9600 | CS8 | CREAD | HUPCL | CLOCAL;
	rio_driver2->flags = TTY_DRIVER_REAL_RAW;
	tty_set_operations(rio_driver2, &rio_ops);

	rio_dprintk(RIO_DEBUG_INIT, "set_termios = %p\n", gs_set_termios);

	if ((error = tty_register_driver(rio_driver)))
		goto out2;
	if ((error = tty_register_driver(rio_driver2)))
		goto out3;
	func_exit();
	return 0;
      out3:
	tty_unregister_driver(rio_driver);
      out2:
	put_tty_driver(rio_driver2);
      out1:
	put_tty_driver(rio_driver);
      out:
	printk(KERN_ERR "rio: Couldn't register a rio driver, error = %d\n", error);
	return 1;
}


static void *ckmalloc(int size)
{
	void *p;

	p = kzalloc(size, GFP_KERNEL);
	return p;
}



static int rio_init_datastructures(void)
{
	int i;
	struct Port *port;
	func_enter();

	/* Many drivers statically allocate the maximum number of ports
	   There is no reason not to allocate them dynamically. Is there? -- REW */
	/* However, the RIO driver allows users to configure their first
	   RTA as the ports numbered 504-511. We therefore need to allocate
	   the whole range. :-(   -- REW */

#define RI_SZ   sizeof(struct rio_info)
#define HOST_SZ sizeof(struct Host)
#define PORT_SZ sizeof(struct Port *)
#define TMIO_SZ sizeof(struct termios *)
	rio_dprintk(RIO_DEBUG_INIT, "getting : %Zd %Zd %Zd %Zd %Zd bytes\n", RI_SZ, RIO_HOSTS * HOST_SZ, RIO_PORTS * PORT_SZ, RIO_PORTS * TMIO_SZ, RIO_PORTS * TMIO_SZ);

	if (!(p = ckmalloc(RI_SZ)))
		goto free0;
	if (!(p->RIOHosts = ckmalloc(RIO_HOSTS * HOST_SZ)))
		goto free1;
	if (!(p->RIOPortp = ckmalloc(RIO_PORTS * PORT_SZ)))
		goto free2;
	p->RIOConf = RIOConf;
	rio_dprintk(RIO_DEBUG_INIT, "Got : %p %p %p\n", p, p->RIOHosts, p->RIOPortp);

#if 1
	for (i = 0; i < RIO_PORTS; i++) {
		port = p->RIOPortp[i] = ckmalloc(sizeof(struct Port));
		if (!port) {
			goto free6;
		}
		rio_dprintk(RIO_DEBUG_INIT, "initing port %d (%d)\n", i, port->Mapped);
		port->PortNum = i;
		port->gs.magic = RIO_MAGIC;
		port->gs.close_delay = HZ / 2;
		port->gs.closing_wait = 30 * HZ;
		port->gs.rd = &rio_real_driver;
		spin_lock_init(&port->portSem);
		/*
		 * Initializing wait queue
		 */
		init_waitqueue_head(&port->gs.open_wait);
		init_waitqueue_head(&port->gs.close_wait);
	}
#else
	/* We could postpone initializing them to when they are configured. */
#endif



	if (rio_debug & RIO_DEBUG_INIT) {
		my_hd(&rio_real_driver, sizeof(rio_real_driver));
	}


	func_exit();
	return 0;

      free6:for (i--; i >= 0; i--)
		kfree(p->RIOPortp[i]);
/*free5:
 free4:
 free3:*/ kfree(p->RIOPortp);
      free2:kfree(p->RIOHosts);
      free1:
	rio_dprintk(RIO_DEBUG_INIT, "Not enough memory! %p %p %p\n", p, p->RIOHosts, p->RIOPortp);
	kfree(p);
      free0:
	return -ENOMEM;
}

static void __exit rio_release_drivers(void)
{
	func_enter();
	tty_unregister_driver(rio_driver2);
	tty_unregister_driver(rio_driver);
	put_tty_driver(rio_driver2);
	put_tty_driver(rio_driver);
	func_exit();
}


#ifdef CONFIG_PCI
 /* This was written for SX, but applies to RIO too...
    (including bugs....)

    There is another bit besides Bit 17. Turning that bit off
    (on boards shipped with the fix in the eeprom) results in a 
    hang on the next access to the card. 
  */

 /******************************************************** 
 * Setting bit 17 in the CNTRL register of the PLX 9050  * 
 * chip forces a retry on writes while a read is pending.*
 * This is to prevent the card locking up on Intel Xeon  *
 * multiprocessor systems with the NX chipset.    -- NV  *
 ********************************************************/

/* Newer cards are produced with this bit set from the configuration
   EEprom.  As the bit is read/write for the CPU, we can fix it here,
   if we detect that it isn't set correctly. -- REW */

static void fix_rio_pci(struct pci_dev *pdev)
{
	unsigned long hwbase;
	unsigned char __iomem *rebase;
	unsigned int t;

#define CNTRL_REG_OFFSET        0x50
#define CNTRL_REG_GOODVALUE     0x18260000

	hwbase = pci_resource_start(pdev, 0);
	rebase = ioremap(hwbase, 0x80);
	t = readl(rebase + CNTRL_REG_OFFSET);
	if (t != CNTRL_REG_GOODVALUE) {
		printk(KERN_DEBUG "rio: performing cntrl reg fix: %08x -> %08x\n", t, CNTRL_REG_GOODVALUE);
		writel(CNTRL_REG_GOODVALUE, rebase + CNTRL_REG_OFFSET);
	}
	iounmap(rebase);
}
#endif


static int __init rio_init(void)
{
	int found = 0;
	int i;
	struct Host *hp;
	int retval;
	struct vpd_prom *vpdp;
	int okboard;

#ifdef CONFIG_PCI
	struct pci_dev *pdev = NULL;
	unsigned short tshort;
#endif

	func_enter();
	rio_dprintk(RIO_DEBUG_INIT, "Initing rio module... (rio_debug=%d)\n", rio_debug);

	if (abs((long) (&rio_debug) - rio_debug) < 0x10000) {
		printk(KERN_WARNING "rio: rio_debug is an address, instead of a value. " "Assuming -1. Was %x/%p.\n", rio_debug, &rio_debug);
		rio_debug = -1;
	}

	if (misc_register(&rio_fw_device) < 0) {
		printk(KERN_ERR "RIO: Unable to register firmware loader driver.\n");
		return -EIO;
	}

	retval = rio_init_datastructures();
	if (retval < 0) {
		misc_deregister(&rio_fw_device);
		return retval;
	}
#ifdef CONFIG_PCI
	/* First look for the JET devices: */
	while ((pdev = pci_get_device(PCI_VENDOR_ID_SPECIALIX, PCI_DEVICE_ID_SPECIALIX_SX_XIO_IO8, pdev))) {
		u32 tint;

		if (pci_enable_device(pdev))
			continue;

		/* Specialix has a whole bunch of cards with
		   0x2000 as the device ID. They say its because
		   the standard requires it. Stupid standard. */
		/* It seems that reading a word doesn't work reliably on 2.0.
		   Also, reading a non-aligned dword doesn't work. So we read the
		   whole dword at 0x2c and extract the word at 0x2e (SUBSYSTEM_ID)
		   ourselves */
		pci_read_config_dword(pdev, 0x2c, &tint);
		tshort = (tint >> 16) & 0xffff;
		rio_dprintk(RIO_DEBUG_PROBE, "Got a specialix card: %x.\n", tint);
		if (tshort != 0x0100) {
			rio_dprintk(RIO_DEBUG_PROBE, "But it's not a RIO card (%d)...\n", tshort);
			continue;
		}
		rio_dprintk(RIO_DEBUG_PROBE, "cp1\n");

		hp = &p->RIOHosts[p->RIONumHosts];
		hp->PaddrP = pci_resource_start(pdev, 2);
		hp->Ivec = pdev->irq;
		if (((1 << hp->Ivec) & rio_irqmask) == 0)
			hp->Ivec = 0;
		hp->Caddr = ioremap(p->RIOHosts[p->RIONumHosts].PaddrP, RIO_WINDOW_LEN);
		hp->CardP = (struct DpRam __iomem *) hp->Caddr;
		hp->Type = RIO_PCI;
		hp->Copy = rio_copy_to_card;
		hp->Mode = RIO_PCI_BOOT_FROM_RAM;
		spin_lock_init(&hp->HostLock);
		rio_reset_interrupt(hp);
		rio_start_card_running(hp);

		rio_dprintk(RIO_DEBUG_PROBE, "Going to test it (%p/%p).\n", (void *) p->RIOHosts[p->RIONumHosts].PaddrP, p->RIOHosts[p->RIONumHosts].Caddr);
		if (RIOBoardTest(p->RIOHosts[p->RIONumHosts].PaddrP, p->RIOHosts[p->RIONumHosts].Caddr, RIO_PCI, 0) == 0) {
			rio_dprintk(RIO_DEBUG_INIT, "Done RIOBoardTest\n");
			writeb(0xFF, &p->RIOHosts[p->RIONumHosts].ResetInt);
			p->RIOHosts[p->RIONumHosts].UniqueNum =
			    ((readb(&p->RIOHosts[p->RIONumHosts].Unique[0]) & 0xFF) << 0) |
			    ((readb(&p->RIOHosts[p->RIONumHosts].Unique[1]) & 0xFF) << 8) | ((readb(&p->RIOHosts[p->RIONumHosts].Unique[2]) & 0xFF) << 16) | ((readb(&p->RIOHosts[p->RIONumHosts].Unique[3]) & 0xFF) << 24);
			rio_dprintk(RIO_DEBUG_PROBE, "Hmm Tested ok, uniqid = %x.\n", p->RIOHosts[p->RIONumHosts].UniqueNum);

			fix_rio_pci(pdev);

			p->RIOHosts[p->RIONumHosts].pdev = pdev;
			pci_dev_get(pdev);

			p->RIOLastPCISearch = 0;
			p->RIONumHosts++;
			found++;
		} else {
			iounmap(p->RIOHosts[p->RIONumHosts].Caddr);
			p->RIOHosts[p->RIONumHosts].Caddr = NULL;
		}
	}

	/* Then look for the older PCI card.... : */

	/* These older PCI cards have problems (only byte-mode access is
	   supported), which makes them a bit awkward to support.
	   They also have problems sharing interrupts. Be careful.
	   (The driver now refuses to share interrupts for these
	   cards. This should be sufficient).
	 */

	/* Then look for the older RIO/PCI devices: */
	while ((pdev = pci_get_device(PCI_VENDOR_ID_SPECIALIX, PCI_DEVICE_ID_SPECIALIX_RIO, pdev))) {
		if (pci_enable_device(pdev))
			continue;

#ifdef CONFIG_RIO_OLDPCI
		hp = &p->RIOHosts[p->RIONumHosts];
		hp->PaddrP = pci_resource_start(pdev, 0);
		hp->Ivec = pdev->irq;
		if (((1 << hp->Ivec) & rio_irqmask) == 0)
			hp->Ivec = 0;
		hp->Ivec |= 0x8000;	/* Mark as non-sharable */
		hp->Caddr = ioremap(p->RIOHosts[p->RIONumHosts].PaddrP, RIO_WINDOW_LEN);
		hp->CardP = (struct DpRam __iomem *) hp->Caddr;
		hp->Type = RIO_PCI;
		hp->Copy = rio_copy_to_card;
		hp->Mode = RIO_PCI_BOOT_FROM_RAM;
		spin_lock_init(&hp->HostLock);

		rio_dprintk(RIO_DEBUG_PROBE, "Ivec: %x\n", hp->Ivec);
		rio_dprintk(RIO_DEBUG_PROBE, "Mode: %x\n", hp->Mode);

		rio_reset_interrupt(hp);
		rio_start_card_running(hp);
		rio_dprintk(RIO_DEBUG_PROBE, "Going to test it (%p/%p).\n", (void *) p->RIOHosts[p->RIONumHosts].PaddrP, p->RIOHosts[p->RIONumHosts].Caddr);
		if (RIOBoardTest(p->RIOHosts[p->RIONumHosts].PaddrP, p->RIOHosts[p->RIONumHosts].Caddr, RIO_PCI, 0) == 0) {
			writeb(0xFF, &p->RIOHosts[p->RIONumHosts].ResetInt);
			p->RIOHosts[p->RIONumHosts].UniqueNum =
			    ((readb(&p->RIOHosts[p->RIONumHosts].Unique[0]) & 0xFF) << 0) |
			    ((readb(&p->RIOHosts[p->RIONumHosts].Unique[1]) & 0xFF) << 8) | ((readb(&p->RIOHosts[p->RIONumHosts].Unique[2]) & 0xFF) << 16) | ((readb(&p->RIOHosts[p->RIONumHosts].Unique[3]) & 0xFF) << 24);
			rio_dprintk(RIO_DEBUG_PROBE, "Hmm Tested ok, uniqid = %x.\n", p->RIOHosts[p->RIONumHosts].UniqueNum);

			p->RIOHosts[p->RIONumHosts].pdev = pdev;
			pci_dev_get(pdev);

			p->RIOLastPCISearch = 0;
			p->RIONumHosts++;
			found++;
		} else {
			iounmap(p->RIOHosts[p->RIONumHosts].Caddr);
			p->RIOHosts[p->RIONumHosts].Caddr = NULL;
		}
#else
		printk(KERN_ERR "Found an older RIO PCI card, but the driver is not " "compiled to support it.\n");
#endif
	}
#endif				/* PCI */

	/* Now probe for ISA cards... */
	for (i = 0; i < NR_RIO_ADDRS; i++) {
		hp = &p->RIOHosts[p->RIONumHosts];
		hp->PaddrP = rio_probe_addrs[i];
		/* There was something about the IRQs of these cards. 'Forget what.--REW */
		hp->Ivec = 0;
		hp->Caddr = ioremap(p->RIOHosts[p->RIONumHosts].PaddrP, RIO_WINDOW_LEN);
		hp->CardP = (struct DpRam __iomem *) hp->Caddr;
		hp->Type = RIO_AT;
		hp->Copy = rio_copy_to_card;	/* AT card PCI???? - PVDL
                                         * -- YES! this is now a normal copy. Only the
					 * old PCI card uses the special PCI copy.
					 * Moreover, the ISA card will work with the
					 * special PCI copy anyway. -- REW */
		hp->Mode = 0;
		spin_lock_init(&hp->HostLock);

		vpdp = get_VPD_PROM(hp);
		rio_dprintk(RIO_DEBUG_PROBE, "Got VPD ROM\n");
		okboard = 0;
		if ((strncmp(vpdp->identifier, RIO_ISA_IDENT, 16) == 0) || (strncmp(vpdp->identifier, RIO_ISA2_IDENT, 16) == 0) || (strncmp(vpdp->identifier, RIO_ISA3_IDENT, 16) == 0)) {
			/* Board is present... */
			if (RIOBoardTest(hp->PaddrP, hp->Caddr, RIO_AT, 0) == 0) {
				/* ... and feeling fine!!!! */
				rio_dprintk(RIO_DEBUG_PROBE, "Hmm Tested ok, uniqid = %x.\n", p->RIOHosts[p->RIONumHosts].UniqueNum);
				if (RIOAssignAT(p, hp->PaddrP, hp->Caddr, 0)) {
					rio_dprintk(RIO_DEBUG_PROBE, "Hmm Tested ok, host%d uniqid = %x.\n", p->RIONumHosts, p->RIOHosts[p->RIONumHosts - 1].UniqueNum);
					okboard++;
					found++;
				}
			}

			if (!okboard) {
				iounmap(hp->Caddr);
				hp->Caddr = NULL;
			}
		}
	}


	for (i = 0; i < p->RIONumHosts; i++) {
		hp = &p->RIOHosts[i];
		if (hp->Ivec) {
			int mode = IRQF_SHARED;
			if (hp->Ivec & 0x8000) {
				mode = 0;
				hp->Ivec &= 0x7fff;
			}
			rio_dprintk(RIO_DEBUG_INIT, "Requesting interrupt hp: %p rio_interrupt: %d Mode: %x\n", hp, hp->Ivec, hp->Mode);
			retval = request_irq(hp->Ivec, rio_interrupt, mode, "rio", hp);
			rio_dprintk(RIO_DEBUG_INIT, "Return value from request_irq: %d\n", retval);
			if (retval) {
				printk(KERN_ERR "rio: Cannot allocate irq %d.\n", hp->Ivec);
				hp->Ivec = 0;
			}
			rio_dprintk(RIO_DEBUG_INIT, "Got irq %d.\n", hp->Ivec);
			if (hp->Ivec != 0) {
				rio_dprintk(RIO_DEBUG_INIT, "Enabling interrupts on rio card.\n");
				hp->Mode |= RIO_PCI_INT_ENABLE;
			} else
				hp->Mode &= ~RIO_PCI_INT_ENABLE;
			rio_dprintk(RIO_DEBUG_INIT, "New Mode: %x\n", hp->Mode);
			rio_start_card_running(hp);
		}
		/* Init the timer "always" to make sure that it can safely be
		   deleted when we unload... */

		setup_timer(&hp->timer, rio_pollfunc, i);
		if (!hp->Ivec) {
			rio_dprintk(RIO_DEBUG_INIT, "Starting polling at %dj intervals.\n", rio_poll);
			mod_timer(&hp->timer, jiffies + rio_poll);
		}
	}

	if (found) {
		rio_dprintk(RIO_DEBUG_INIT, "rio: total of %d boards detected.\n", found);
		rio_init_drivers();
	} else {
		/* deregister the misc device we created earlier */
		misc_deregister(&rio_fw_device);
	}

	func_exit();
	return found ? 0 : -EIO;
}


static void __exit rio_exit(void)
{
	int i;
	struct Host *hp;

	func_enter();

	for (i = 0, hp = p->RIOHosts; i < p->RIONumHosts; i++, hp++) {
		RIOHostReset(hp->Type, hp->CardP, hp->Slot);
		if (hp->Ivec) {
			free_irq(hp->Ivec, hp);
			rio_dprintk(RIO_DEBUG_INIT, "freed irq %d.\n", hp->Ivec);
		}
		/* It is safe/allowed to del_timer a non-active timer */
		del_timer_sync(&hp->timer);
		if (hp->Caddr)
			iounmap(hp->Caddr);
		if (hp->Type == RIO_PCI)
			pci_dev_put(hp->pdev);
	}

	if (misc_deregister(&rio_fw_device) < 0) {
		printk(KERN_INFO "rio: couldn't deregister control-device\n");
	}


	rio_dprintk(RIO_DEBUG_CLEANUP, "Cleaning up drivers\n");

	rio_release_drivers();

	/* Release dynamically allocated memory */
	kfree(p->RIOPortp);
	kfree(p->RIOHosts);
	kfree(p);

	func_exit();
}

module_init(rio_init);
module_exit(rio_exit);
