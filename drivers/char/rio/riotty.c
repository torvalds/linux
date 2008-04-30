/*
** -----------------------------------------------------------------------------
**
**  Perle Specialix driver for Linux
**  Ported from existing RIO Driver for SCO sources.
 *
 *  (C) 1990 - 2000 Specialix International Ltd., Byfleet, Surrey, UK.
 *
 *      This program is free software; you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License as published by
 *      the Free Software Foundation; either version 2 of the License, or
 *      (at your option) any later version.
 *
 *      This program is distributed in the hope that it will be useful,
 *      but WITHOUT ANY WARRANTY; without even the implied warranty of
 *      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *      GNU General Public License for more details.
 *
 *      You should have received a copy of the GNU General Public License
 *      along with this program; if not, write to the Free Software
 *      Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
**
**	Module		: riotty.c
**	SID		: 1.3
**	Last Modified	: 11/6/98 10:33:47
**	Retrieved	: 11/6/98 10:33:50
**
**  ident @(#)riotty.c	1.3
**
** -----------------------------------------------------------------------------
*/
#ifdef SCCS_LABELS
static char *_riotty_c_sccs_ = "@(#)riotty.c	1.3";
#endif


#define __EXPLICIT_DEF_H__

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/tty.h>
#include <linux/string.h>
#include <asm/io.h>
#include <asm/system.h>
#include <asm/string.h>
#include <asm/uaccess.h>

#include <linux/termios.h>

#include <linux/serial.h>

#include <linux/generic_serial.h>


#include "linux_compat.h"
#include "rio_linux.h"
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

static void RIOClearUp(struct Port *PortP);

/* Below belongs in func.h */
int RIOShortCommand(struct rio_info *p, struct Port *PortP, int command, int len, int arg);


extern struct rio_info *p;


int riotopen(struct tty_struct *tty, struct file *filp)
{
	unsigned int SysPort;
	int repeat_this = 250;
	struct Port *PortP;	/* pointer to the port structure */
	unsigned long flags;
	int retval = 0;

	func_enter();

	/* Make sure driver_data is NULL in case the rio isn't booted jet. Else gs_close
	   is going to oops.
	 */
	tty->driver_data = NULL;

	SysPort = rio_minor(tty);

	if (p->RIOFailed) {
		rio_dprintk(RIO_DEBUG_TTY, "System initialisation failed\n");
		func_exit();
		return -ENXIO;
	}

	rio_dprintk(RIO_DEBUG_TTY, "port open SysPort %d (mapped:%d)\n", SysPort, p->RIOPortp[SysPort]->Mapped);

	/*
	 ** Validate that we have received a legitimate request.
	 ** Currently, just check that we are opening a port on
	 ** a host card that actually exists, and that the port
	 ** has been mapped onto a host.
	 */
	if (SysPort >= RIO_PORTS) {	/* out of range ? */
		rio_dprintk(RIO_DEBUG_TTY, "Illegal port number %d\n", SysPort);
		func_exit();
		return -ENXIO;
	}

	/*
	 ** Grab pointer to the port stucture
	 */
	PortP = p->RIOPortp[SysPort];	/* Get control struc */
	rio_dprintk(RIO_DEBUG_TTY, "PortP: %p\n", PortP);
	if (!PortP->Mapped) {	/* we aren't mapped yet! */
		/*
		 ** The system doesn't know which RTA this port
		 ** corresponds to.
		 */
		rio_dprintk(RIO_DEBUG_TTY, "port not mapped into system\n");
		func_exit();
		return -ENXIO;
	}

	tty->driver_data = PortP;

	PortP->gs.tty = tty;
	PortP->gs.count++;

	rio_dprintk(RIO_DEBUG_TTY, "%d bytes in tx buffer\n", PortP->gs.xmit_cnt);

	retval = gs_init_port(&PortP->gs);
	if (retval) {
		PortP->gs.count--;
		return -ENXIO;
	}
	/*
	 ** If the host hasn't been booted yet, then
	 ** fail
	 */
	if ((PortP->HostP->Flags & RUN_STATE) != RC_RUNNING) {
		rio_dprintk(RIO_DEBUG_TTY, "Host not running\n");
		func_exit();
		return -ENXIO;
	}

	/*
	 ** If the RTA has not booted yet and the user has choosen to block
	 ** until the RTA is present then we must spin here waiting for
	 ** the RTA to boot.
	 */
	/* I find the above code a bit hairy. I find the below code
	   easier to read and shorter. Now, if it works too that would
	   be great... -- REW 
	 */
	rio_dprintk(RIO_DEBUG_TTY, "Checking if RTA has booted... \n");
	while (!(PortP->HostP->Mapping[PortP->RupNum].Flags & RTA_BOOTED)) {
		if (!PortP->WaitUntilBooted) {
			rio_dprintk(RIO_DEBUG_TTY, "RTA never booted\n");
			func_exit();
			return -ENXIO;
		}

		/* Under Linux you'd normally use a wait instead of this
		   busy-waiting. I'll stick with the old implementation for
		   now. --REW
		 */
		if (RIODelay(PortP, HUNDRED_MS) == RIO_FAIL) {
			rio_dprintk(RIO_DEBUG_TTY, "RTA_wait_for_boot: EINTR in delay \n");
			func_exit();
			return -EINTR;
		}
		if (repeat_this-- <= 0) {
			rio_dprintk(RIO_DEBUG_TTY, "Waiting for RTA to boot timeout\n");
			func_exit();
			return -EIO;
		}
	}
	rio_dprintk(RIO_DEBUG_TTY, "RTA has been booted\n");
	rio_spin_lock_irqsave(&PortP->portSem, flags);
	if (p->RIOHalted) {
		goto bombout;
	}

	/*
	 ** If the port is in the final throws of being closed,
	 ** we should wait here (politely), waiting
	 ** for it to finish, so that it doesn't close us!
	 */
	while ((PortP->State & RIO_CLOSING) && !p->RIOHalted) {
		rio_dprintk(RIO_DEBUG_TTY, "Waiting for RIO_CLOSING to go away\n");
		if (repeat_this-- <= 0) {
			rio_dprintk(RIO_DEBUG_TTY, "Waiting for not idle closed broken by signal\n");
			RIOPreemptiveCmd(p, PortP, FCLOSE);
			retval = -EINTR;
			goto bombout;
		}
		rio_spin_unlock_irqrestore(&PortP->portSem, flags);
		if (RIODelay(PortP, HUNDRED_MS) == RIO_FAIL) {
			rio_spin_lock_irqsave(&PortP->portSem, flags);
			retval = -EINTR;
			goto bombout;
		}
		rio_spin_lock_irqsave(&PortP->portSem, flags);
	}

	if (!PortP->Mapped) {
		rio_dprintk(RIO_DEBUG_TTY, "Port unmapped while closing!\n");
		rio_spin_unlock_irqrestore(&PortP->portSem, flags);
		retval = -ENXIO;
		func_exit();
		return retval;
	}

	if (p->RIOHalted) {
		goto bombout;
	}

/*
** 15.10.1998 ARG - ESIL 0761 part fix
** RIO has it's own CTSFLOW and RTSFLOW flags in 'Config' in the port structure,
** we need to make sure that the flags are clear when the port is opened.
*/
	/* Uh? Suppose I turn these on and then another process opens
	   the port again? The flags get cleared! Not good. -- REW */
	if (!(PortP->State & (RIO_LOPEN | RIO_MOPEN))) {
		PortP->Config &= ~(RIO_CTSFLOW | RIO_RTSFLOW);
	}

	if (!(PortP->firstOpen)) {	/* First time ? */
		rio_dprintk(RIO_DEBUG_TTY, "First open for this port\n");


		PortP->firstOpen++;
		PortP->CookMode = 0;	/* XXX RIOCookMode(tp); */
		PortP->InUse = NOT_INUSE;

		/* Tentative fix for bug PR27. Didn't work. */
		/* PortP->gs.xmit_cnt = 0; */

		rio_spin_unlock_irqrestore(&PortP->portSem, flags);

		/* Someone explain to me why this delay/config is
		   here. If I read the docs correctly the "open"
		   command piggybacks the parameters immediately.
		   -- REW */
		RIOParam(PortP, OPEN, 1, OK_TO_SLEEP);	/* Open the port */
		rio_spin_lock_irqsave(&PortP->portSem, flags);

		/*
		 ** wait for the port to be not closed.
		 */
		while (!(PortP->PortState & PORT_ISOPEN) && !p->RIOHalted) {
			rio_dprintk(RIO_DEBUG_TTY, "Waiting for PORT_ISOPEN-currently %x\n", PortP->PortState);
			rio_spin_unlock_irqrestore(&PortP->portSem, flags);
			if (RIODelay(PortP, HUNDRED_MS) == RIO_FAIL) {
				rio_dprintk(RIO_DEBUG_TTY, "Waiting for open to finish broken by signal\n");
				RIOPreemptiveCmd(p, PortP, FCLOSE);
				func_exit();
				return -EINTR;
			}
			rio_spin_lock_irqsave(&PortP->portSem, flags);
		}

		if (p->RIOHalted) {
			retval = -EIO;
		      bombout:
			/*                    RIOClearUp( PortP ); */
			rio_spin_unlock_irqrestore(&PortP->portSem, flags);
			return retval;
		}
		rio_dprintk(RIO_DEBUG_TTY, "PORT_ISOPEN found\n");
	}
	rio_dprintk(RIO_DEBUG_TTY, "Modem - test for carrier\n");
	/*
	 ** ACTION
	 ** insert test for carrier here. -- ???
	 ** I already see that test here. What's the deal? -- REW
	 */
	if ((PortP->gs.tty->termios->c_cflag & CLOCAL) || (PortP->ModemState & MSVR1_CD)) {
		rio_dprintk(RIO_DEBUG_TTY, "open(%d) Modem carr on\n", SysPort);
		/*
		   tp->tm.c_state |= CARR_ON;
		   wakeup((caddr_t) &tp->tm.c_canq);
		 */
		PortP->State |= RIO_CARR_ON;
		wake_up_interruptible(&PortP->gs.open_wait);
	} else {	/* no carrier - wait for DCD */
			/*
		   while (!(PortP->gs.tty->termios->c_state & CARR_ON) &&
		   !(filp->f_flags & O_NONBLOCK) && !p->RIOHalted )
		 */
		while (!(PortP->State & RIO_CARR_ON) && !(filp->f_flags & O_NONBLOCK) && !p->RIOHalted) {
				rio_dprintk(RIO_DEBUG_TTY, "open(%d) sleeping for carr on\n", SysPort);
			/*
			   PortP->gs.tty->termios->c_state |= WOPEN;
			 */
			PortP->State |= RIO_WOPEN;
			rio_spin_unlock_irqrestore(&PortP->portSem, flags);
			if (RIODelay(PortP, HUNDRED_MS) == RIO_FAIL) {
				rio_spin_lock_irqsave(&PortP->portSem, flags);
				/*
				 ** ACTION: verify that this is a good thing
				 ** to do here. -- ???
				 ** I think it's OK. -- REW
				 */
				rio_dprintk(RIO_DEBUG_TTY, "open(%d) sleeping for carr broken by signal\n", SysPort);
				RIOPreemptiveCmd(p, PortP, FCLOSE);
				/*
				   tp->tm.c_state &= ~WOPEN;
				 */
				PortP->State &= ~RIO_WOPEN;
				rio_spin_unlock_irqrestore(&PortP->portSem, flags);
				func_exit();
				return -EINTR;
			}
			rio_spin_lock_irqsave(&PortP->portSem, flags);
		}
		PortP->State &= ~RIO_WOPEN;
	}
	if (p->RIOHalted)
		goto bombout;
	rio_dprintk(RIO_DEBUG_TTY, "Setting RIO_MOPEN\n");
	PortP->State |= RIO_MOPEN;

	if (p->RIOHalted)
		goto bombout;

	rio_dprintk(RIO_DEBUG_TTY, "high level open done\n");

	/*
	 ** Count opens for port statistics reporting
	 */
	if (PortP->statsGather)
		PortP->opens++;

	rio_spin_unlock_irqrestore(&PortP->portSem, flags);
	rio_dprintk(RIO_DEBUG_TTY, "Returning from open\n");
	func_exit();
	return 0;
}

/*
** RIOClose the port.
** The operating system thinks that this is last close for the device.
** As there are two interfaces to the port (Modem and tty), we need to
** check that both are closed before we close the device.
*/
int riotclose(void *ptr)
{
	struct Port *PortP = ptr;	/* pointer to the port structure */
	int deleted = 0;
	int try = -1;		/* Disable the timeouts by setting them to -1 */
	int repeat_this = -1;	/* Congrats to those having 15 years of
				   uptime! (You get to break the driver.) */
	unsigned long end_time;
	struct tty_struct *tty;
	unsigned long flags;
	int rv = 0;

	rio_dprintk(RIO_DEBUG_TTY, "port close SysPort %d\n", PortP->PortNum);

	/* PortP = p->RIOPortp[SysPort]; */
	rio_dprintk(RIO_DEBUG_TTY, "Port is at address %p\n", PortP);
	/* tp = PortP->TtyP; *//* Get tty */
	tty = PortP->gs.tty;
	rio_dprintk(RIO_DEBUG_TTY, "TTY is at address %p\n", tty);

	if (PortP->gs.closing_wait)
		end_time = jiffies + PortP->gs.closing_wait;
	else
		end_time = jiffies + MAX_SCHEDULE_TIMEOUT;

	rio_spin_lock_irqsave(&PortP->portSem, flags);

	/*
	 ** Setting this flag will make any process trying to open
	 ** this port block until we are complete closing it.
	 */
	PortP->State |= RIO_CLOSING;

	if ((PortP->State & RIO_DELETED)) {
		rio_dprintk(RIO_DEBUG_TTY, "Close on deleted RTA\n");
		deleted = 1;
	}

	if (p->RIOHalted) {
		RIOClearUp(PortP);
		rv = -EIO;
		goto close_end;
	}

	rio_dprintk(RIO_DEBUG_TTY, "Clear bits\n");
	/*
	 ** clear the open bits for this device
	 */
	PortP->State &= ~RIO_MOPEN;
	PortP->State &= ~RIO_CARR_ON;
	PortP->ModemState &= ~MSVR1_CD;
	/*
	 ** If the device was open as both a Modem and a tty line
	 ** then we need to wimp out here, as the port has not really
	 ** been finally closed (gee, whizz!) The test here uses the
	 ** bit for the OTHER mode of operation, to see if THAT is
	 ** still active!
	 */
	if ((PortP->State & (RIO_LOPEN | RIO_MOPEN))) {
		/*
		 ** The port is still open for the other task -
		 ** return, pretending that we are still active.
		 */
		rio_dprintk(RIO_DEBUG_TTY, "Channel %d still open !\n", PortP->PortNum);
		PortP->State &= ~RIO_CLOSING;
		if (PortP->firstOpen)
			PortP->firstOpen--;
		rio_spin_unlock_irqrestore(&PortP->portSem, flags);
		return -EIO;
	}

	rio_dprintk(RIO_DEBUG_TTY, "Closing down - everything must go!\n");

	PortP->State &= ~RIO_DYNOROD;

	/*
	 ** This is where we wait for the port
	 ** to drain down before closing. Bye-bye....
	 ** (We never meant to do this)
	 */
	rio_dprintk(RIO_DEBUG_TTY, "Timeout 1 starts\n");

	if (!deleted)
		while ((PortP->InUse != NOT_INUSE) && !p->RIOHalted && (PortP->TxBufferIn != PortP->TxBufferOut)) {
			if (repeat_this-- <= 0) {
				rv = -EINTR;
				rio_dprintk(RIO_DEBUG_TTY, "Waiting for not idle closed broken by signal\n");
				RIOPreemptiveCmd(p, PortP, FCLOSE);
				goto close_end;
			}
			rio_dprintk(RIO_DEBUG_TTY, "Calling timeout to flush in closing\n");
			rio_spin_unlock_irqrestore(&PortP->portSem, flags);
			if (RIODelay_ni(PortP, HUNDRED_MS * 10) == RIO_FAIL) {
				rio_dprintk(RIO_DEBUG_TTY, "RTA EINTR in delay \n");
				rv = -EINTR;
				rio_spin_lock_irqsave(&PortP->portSem, flags);
				goto close_end;
			}
			rio_spin_lock_irqsave(&PortP->portSem, flags);
		}

	PortP->TxBufferIn = PortP->TxBufferOut = 0;
	repeat_this = 0xff;

	PortP->InUse = 0;
	if ((PortP->State & (RIO_LOPEN | RIO_MOPEN))) {
		/*
		 ** The port has been re-opened for the other task -
		 ** return, pretending that we are still active.
		 */
		rio_dprintk(RIO_DEBUG_TTY, "Channel %d re-open!\n", PortP->PortNum);
		PortP->State &= ~RIO_CLOSING;
		rio_spin_unlock_irqrestore(&PortP->portSem, flags);
		if (PortP->firstOpen)
			PortP->firstOpen--;
		return -EIO;
	}

	if (p->RIOHalted) {
		RIOClearUp(PortP);
		goto close_end;
	}

	/* Can't call RIOShortCommand with the port locked. */
	rio_spin_unlock_irqrestore(&PortP->portSem, flags);

	if (RIOShortCommand(p, PortP, CLOSE, 1, 0) == RIO_FAIL) {
		RIOPreemptiveCmd(p, PortP, FCLOSE);
		rio_spin_lock_irqsave(&PortP->portSem, flags);
		goto close_end;
	}

	if (!deleted)
		while (try && (PortP->PortState & PORT_ISOPEN)) {
			try--;
			if (time_after(jiffies, end_time)) {
				rio_dprintk(RIO_DEBUG_TTY, "Run out of tries - force the bugger shut!\n");
				RIOPreemptiveCmd(p, PortP, FCLOSE);
				break;
			}
			rio_dprintk(RIO_DEBUG_TTY, "Close: PortState:ISOPEN is %d\n", PortP->PortState & PORT_ISOPEN);

			if (p->RIOHalted) {
				RIOClearUp(PortP);
				rio_spin_lock_irqsave(&PortP->portSem, flags);
				goto close_end;
			}
			if (RIODelay(PortP, HUNDRED_MS) == RIO_FAIL) {
				rio_dprintk(RIO_DEBUG_TTY, "RTA EINTR in delay \n");
				RIOPreemptiveCmd(p, PortP, FCLOSE);
				break;
			}
		}
	rio_spin_lock_irqsave(&PortP->portSem, flags);
	rio_dprintk(RIO_DEBUG_TTY, "Close: try was %d on completion\n", try);

	/* RIOPreemptiveCmd(p, PortP, FCLOSE); */

/*
** 15.10.1998 ARG - ESIL 0761 part fix
** RIO has it's own CTSFLOW and RTSFLOW flags in 'Config' in the port structure,** we need to make sure that the flags are clear when the port is opened.
*/
	PortP->Config &= ~(RIO_CTSFLOW | RIO_RTSFLOW);

	/*
	 ** Count opens for port statistics reporting
	 */
	if (PortP->statsGather)
		PortP->closes++;

close_end:
	/* XXX: Why would a "DELETED" flag be reset here? I'd have
	   thought that a "deleted" flag means that the port was
	   permanently gone, but here we can make it reappear by it
	   being in close during the "deletion".
	 */
	PortP->State &= ~(RIO_CLOSING | RIO_DELETED);
	if (PortP->firstOpen)
		PortP->firstOpen--;
	rio_spin_unlock_irqrestore(&PortP->portSem, flags);
	rio_dprintk(RIO_DEBUG_TTY, "Return from close\n");
	return rv;
}



static void RIOClearUp(struct Port *PortP)
{
	rio_dprintk(RIO_DEBUG_TTY, "RIOHalted set\n");
	PortP->Config = 0;	/* Direct semaphore */
	PortP->PortState = 0;
	PortP->firstOpen = 0;
	PortP->FlushCmdBodge = 0;
	PortP->ModemState = PortP->CookMode = 0;
	PortP->Mapped = 0;
	PortP->WflushFlag = 0;
	PortP->MagicFlags = 0;
	PortP->RxDataStart = 0;
	PortP->TxBufferIn = 0;
	PortP->TxBufferOut = 0;
}

/*
** Put a command onto a port.
** The PortPointer, command, length and arg are passed.
** The len is the length *inclusive* of the command byte,
** and so for a command that takes no data, len==1.
** The arg is a single byte, and is only used if len==2.
** Other values of len aren't allowed, and will cause
** a panic.
*/
int RIOShortCommand(struct rio_info *p, struct Port *PortP, int command, int len, int arg)
{
	struct PKT __iomem *PacketP;
	int retries = 20;	/* at 10 per second -> 2 seconds */
	unsigned long flags;

	rio_dprintk(RIO_DEBUG_TTY, "entering shortcommand.\n");

	if (PortP->State & RIO_DELETED) {
		rio_dprintk(RIO_DEBUG_TTY, "Short command to deleted RTA ignored\n");
		return RIO_FAIL;
	}
	rio_spin_lock_irqsave(&PortP->portSem, flags);

	/*
	 ** If the port is in use for pre-emptive command, then wait for it to
	 ** be free again.
	 */
	while ((PortP->InUse != NOT_INUSE) && !p->RIOHalted) {
		rio_dprintk(RIO_DEBUG_TTY, "Waiting for not in use (%d)\n", retries);
		rio_spin_unlock_irqrestore(&PortP->portSem, flags);
		if (retries-- <= 0) {
			return RIO_FAIL;
		}
		if (RIODelay_ni(PortP, HUNDRED_MS) == RIO_FAIL) {
			return RIO_FAIL;
		}
		rio_spin_lock_irqsave(&PortP->portSem, flags);
	}
	if (PortP->State & RIO_DELETED) {
		rio_dprintk(RIO_DEBUG_TTY, "Short command to deleted RTA ignored\n");
		rio_spin_unlock_irqrestore(&PortP->portSem, flags);
		return RIO_FAIL;
	}

	while (!can_add_transmit(&PacketP, PortP) && !p->RIOHalted) {
		rio_dprintk(RIO_DEBUG_TTY, "Waiting to add short command to queue (%d)\n", retries);
		rio_spin_unlock_irqrestore(&PortP->portSem, flags);
		if (retries-- <= 0) {
			rio_dprintk(RIO_DEBUG_TTY, "out of tries. Failing\n");
			return RIO_FAIL;
		}
		if (RIODelay_ni(PortP, HUNDRED_MS) == RIO_FAIL) {
			return RIO_FAIL;
		}
		rio_spin_lock_irqsave(&PortP->portSem, flags);
	}

	if (p->RIOHalted) {
		rio_spin_unlock_irqrestore(&PortP->portSem, flags);
		return RIO_FAIL;
	}

	/*
	 ** set the command byte and the argument byte
	 */
	writeb(command, &PacketP->data[0]);

	if (len == 2)
		writeb(arg, &PacketP->data[1]);

	/*
	 ** set the length of the packet and set the command bit.
	 */
	writeb(PKT_CMD_BIT | len, &PacketP->len);

	add_transmit(PortP);
	/*
	 ** Count characters transmitted for port statistics reporting
	 */
	if (PortP->statsGather)
		PortP->txchars += len;

	rio_spin_unlock_irqrestore(&PortP->portSem, flags);
	return p->RIOHalted ? RIO_FAIL : ~RIO_FAIL;
}


