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
#include <asm/semaphore.h>
#include <asm/uaccess.h>

#include <linux/termios.h>

#include <linux/serial.h>

#include <linux/generic_serial.h>


#include "linux_compat.h"
#include "rio_linux.h"
#include "typdef.h"
#include "pkt.h"
#include "daemon.h"
#include "rio.h"
#include "riospace.h"
#include "top.h"
#include "cmdpkt.h"
#include "map.h"
#include "riotypes.h"
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
#include "error.h"
#include "phb.h"
#include "link.h"
#include "cmdblk.h"
#include "route.h"
#include "control.h"
#include "cirrus.h"
#include "rioioctl.h"
#include "param.h"
#include "list.h"
#include "sam.h"

#if 0
static void ttyseth_pv(struct Port *, struct ttystatics *, 
				struct termios *sg, int);
#endif

static void RIOClearUp(struct Port *PortP);
int RIOShortCommand(struct rio_info *p, struct Port *PortP, 
			   int command, int len, int arg);

#if 0
static int RIOCookMode(struct ttystatics *);
#endif

extern int	conv_vb[];	/* now defined in ttymgr.c */
extern int	conv_bv[];	/* now defined in ttymgr.c */
 
/*
** 16.09.1998 ARG - Fix to build riotty.k.o for Modular Kernel Support
**
** ep.def.h is necessary for Modular Kernel Support
** DO NOT place any kernel 'extern's after this line
** or this source file will not build riotty.k.o
*/
#ifdef uLYNX
#include <ep.def.h>
#endif

#ifdef NEED_THIS2
static struct old_sgttyb 
default_sg = 
{ 
	B19200, B19200,				/* input and output speed */ 
	'H' - '@',					/* erase char */ 
	-1,							/* 2nd erase char */ 
	'U' - '@',					/* kill char */ 
	ECHO | CRMOD,				/* mode */ 
	'C' - '@',					/* interrupt character */ 
	'\\' - '@',					/* quit char */ 
	'Q' - '@',					/* start char */
	'S' - '@',					/* stop char */ 
	'D' - '@',					/* EOF */
	-1,							/* brk */
	(LCRTBS | LCRTERA | LCRTKIL | LCTLECH),	/* local mode word */ 
	'Z' - '@',					/* process stop */
	'Y' - '@',					/* delayed stop */
	'R' - '@',					/* reprint line */ 
	'O' - '@',					/* flush output */
	'W' - '@',					/* word erase */
	'V' - '@'					/* literal next char */
};
#endif


extern struct rio_info *p;


int
riotopen(struct tty_struct * tty, struct file * filp)
{
	register uint SysPort;
	int Modem;
	int repeat_this = 250;
	struct Port *PortP;		 /* pointer to the port structure */
	unsigned long flags;
	int retval = 0;

	func_enter ();

	/* Make sure driver_data is NULL in case the rio isn't booted jet. Else gs_close
	   is going to oops.
	*/
	tty->driver_data = NULL;
        
	SysPort = rio_minor(tty);
	Modem   = rio_ismodem(tty);

	if ( p->RIOFailed ) {
		rio_dprintk (RIO_DEBUG_TTY, "System initialisation failed\n");
		pseterr(ENXIO);
		func_exit ();
		return -ENXIO;
	}

	rio_dprintk (RIO_DEBUG_TTY, "port open SysPort %d (%s) (mapped:%d)\n",
	       SysPort,  Modem ? "Modem" : "tty",
				   p->RIOPortp[SysPort]->Mapped);

	/*
	** Validate that we have received a legitimate request.
	** Currently, just check that we are opening a port on
	** a host card that actually exists, and that the port
	** has been mapped onto a host.
	*/
	if (SysPort >= RIO_PORTS) {	/* out of range ? */
		rio_dprintk (RIO_DEBUG_TTY, "Illegal port number %d\n",SysPort);
		pseterr(ENXIO);
		func_exit();
		return -ENXIO;
	}

	/*
	** Grab pointer to the port stucture
	*/
	PortP = p->RIOPortp[SysPort];	/* Get control struc */
	rio_dprintk (RIO_DEBUG_TTY, "PortP: %p\n", PortP);
	if ( !PortP->Mapped ) {	/* we aren't mapped yet! */
		/*
		** The system doesn't know which RTA this port
		** corresponds to.
		*/
		rio_dprintk (RIO_DEBUG_TTY, "port not mapped into system\n");
		func_exit ();
		pseterr(ENXIO);
		return -ENXIO;
	}

	tty->driver_data = PortP;

	PortP->gs.tty = tty;
	PortP->gs.count++;

	rio_dprintk (RIO_DEBUG_TTY, "%d bytes in tx buffer\n",
				   PortP->gs.xmit_cnt);

	retval = gs_init_port (&PortP->gs);
	if (retval) {
		PortP->gs.count--;
		return -ENXIO;
	}
	/*
	** If the host hasn't been booted yet, then 
	** fail
	*/
	if ( (PortP->HostP->Flags & RUN_STATE) != RC_RUNNING ) {
		rio_dprintk (RIO_DEBUG_TTY, "Host not running\n");
		pseterr(ENXIO);
		func_exit ();
		return -ENXIO;
	}

	/*
	** If the RTA has not booted yet and the user has choosen to block
	** until the RTA is present then we must spin here waiting for
	** the RTA to boot.
	*/
#if 0
	if (!(PortP->HostP->Mapping[PortP->RupNum].Flags & RTA_BOOTED)) {
		if (PortP->WaitUntilBooted) {
			rio_dprintk (RIO_DEBUG_TTY, "Waiting for RTA to boot\n");
			do {
				if (RIODelay(PortP, HUNDRED_MS) == RIO_FAIL) {
					rio_dprintk (RIO_DEBUG_TTY, "RTA EINTR in delay \n");
					func_exit ();
					return -EINTR;
				}
				if (repeat_this -- <= 0) {
					rio_dprintk (RIO_DEBUG_TTY, "Waiting for RTA to boot timeout\n");
					RIOPreemptiveCmd(p, PortP, FCLOSE ); 
					pseterr(EINTR);
					func_exit ();
					return -EIO;
				}
			} while(!(PortP->HostP->Mapping[PortP->RupNum].Flags & RTA_BOOTED));
			rio_dprintk (RIO_DEBUG_TTY, "RTA has been booted\n");
		} else {
			rio_dprintk (RIO_DEBUG_TTY, "RTA never booted\n");
			pseterr(ENXIO);
			func_exit ();
			return 0;
		}
	}
#else
	/* I find the above code a bit hairy. I find the below code
           easier to read and shorter. Now, if it works too that would
	   be great... -- REW 
	*/
	rio_dprintk (RIO_DEBUG_TTY, "Checking if RTA has booted... \n");
	while (!(PortP->HostP->Mapping[PortP->RupNum].Flags & RTA_BOOTED)) {
	  if (!PortP->WaitUntilBooted) {
	    rio_dprintk (RIO_DEBUG_TTY, "RTA never booted\n");
	    func_exit ();
	    return -ENXIO;
	  }

	  /* Under Linux you'd normally use a wait instead of this
	     busy-waiting. I'll stick with the old implementation for
	     now. --REW 
	  */
	  if (RIODelay(PortP, HUNDRED_MS) == RIO_FAIL) {
	    rio_dprintk (RIO_DEBUG_TTY, "RTA_wait_for_boot: EINTR in delay \n");
	    func_exit ();
	    return -EINTR;
	  }
	  if (repeat_this -- <= 0) {
	    rio_dprintk (RIO_DEBUG_TTY, "Waiting for RTA to boot timeout\n");
	    func_exit ();
	    return -EIO;
	  }
	}
	rio_dprintk (RIO_DEBUG_TTY, "RTA has been booted\n");
#endif
#if 0
	tp =  PortP->TtyP;		/* get tty struct */
#endif
	rio_spin_lock_irqsave(&PortP->portSem, flags);
	if ( p->RIOHalted ) {
		goto bombout;
	}
#if 0
	retval = gs_init_port(&PortP->gs);
	if (retval){
		func_exit ();
		return retval;
	}
#endif

	/*
	** If the port is in the final throws of being closed,
	** we should wait here (politely), waiting
	** for it to finish, so that it doesn't close us!
	*/
	while ( (PortP->State & RIO_CLOSING) && !p->RIOHalted ) {
		rio_dprintk (RIO_DEBUG_TTY, "Waiting for RIO_CLOSING to go away\n");
		if (repeat_this -- <= 0) {
			rio_dprintk (RIO_DEBUG_TTY, "Waiting for not idle closed broken by signal\n");
			RIOPreemptiveCmd(p, PortP, FCLOSE ); 
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

	if ( !PortP->Mapped ) {
		rio_dprintk (RIO_DEBUG_TTY, "Port unmapped while closing!\n");
		rio_spin_unlock_irqrestore(&PortP->portSem, flags);
		retval = -ENXIO;
		func_exit ();
		return retval;
	}

	if ( p->RIOHalted ) {
		goto bombout;
	}

/*
** 15.10.1998 ARG - ESIL 0761 part fix
** RIO has it's own CTSFLOW and RTSFLOW flags in 'Config' in the port structure,
** we need to make sure that the flags are clear when the port is opened.
*/
	/* Uh? Suppose I turn these on and then another process opens
	   the port again? The flags get cleared! Not good. -- REW */
	if ( !(PortP->State & (RIO_LOPEN | RIO_MOPEN)) ) {
		PortP->Config &= ~(RIO_CTSFLOW|RIO_RTSFLOW);
	}

	if (!(PortP->firstOpen)) {	/* First time ? */
		rio_dprintk (RIO_DEBUG_TTY, "First open for this port\n");
	

		PortP->firstOpen++;
		PortP->CookMode = 0; /* XXX RIOCookMode(tp); */
		PortP->InUse = NOT_INUSE;

		/* Tentative fix for bug PR27. Didn't work. */
		/* PortP->gs.xmit_cnt = 0; */

		rio_spin_unlock_irqrestore(&PortP->portSem, flags);
#ifdef NEED_THIS
		ttyseth(PortP, tp, (struct old_sgttyb *)&default_sg);
#endif

		/* Someone explain to me why this delay/config is
                   here. If I read the docs correctly the "open"
                   command piggybacks the parameters immediately. 
		   -- REW */
		RIOParam(PortP,OPEN,Modem,OK_TO_SLEEP);		/* Open the port */
#if 0
		/* This delay of 1 second was annoying. I removed it. -- REW */
		RIODelay(PortP, HUNDRED_MS*10);
		RIOParam(PortP,CONFIG,Modem,OK_TO_SLEEP);	/* Config the port */
#endif
		rio_spin_lock_irqsave(&PortP->portSem, flags);

		/*
		** wait for the port to be not closed.
		*/
		while ( !(PortP->PortState & PORT_ISOPEN) && !p->RIOHalted ) {
			rio_dprintk (RIO_DEBUG_TTY, "Waiting for PORT_ISOPEN-currently %x\n",PortP->PortState);
/*
** 15.10.1998 ARG - ESIL 0759
** (Part) fix for port being trashed when opened whilst RTA "disconnected"
** Take out the limited wait - now wait for ever or until user
** bangs us out.
**
			if (repeat_this -- <= 0) {
				rio_dprint(RIO_DEBUG_TTY, ("Waiting for open to finish timed out.\n"));
				RIOPreemptiveCmd(p, PortP, FCLOSE ); 
				rio_spin_unlock_irqrestore(&PortP->portSem, flags);
				return -EINTR;
			}
**
*/
			rio_spin_unlock_irqrestore(&PortP->portSem, flags);
			if (RIODelay(PortP, HUNDRED_MS) == RIO_FAIL) {
				rio_dprintk (RIO_DEBUG_TTY, "Waiting for open to finish broken by signal\n");
				RIOPreemptiveCmd(p, PortP, FCLOSE );
				func_exit ();
				return -EINTR;
			}
			rio_spin_lock_irqsave(&PortP->portSem, flags);
		}

		if ( p->RIOHalted ) {
		  retval = -EIO;
bombout:
		  /* 			RIOClearUp( PortP ); */
			rio_spin_unlock_irqrestore(&PortP->portSem, flags);
			return retval;
		}
		rio_dprintk (RIO_DEBUG_TTY, "PORT_ISOPEN found\n");
	}

#ifdef MODEM_SUPPORT 
	if (Modem) {
		rio_dprintk (RIO_DEBUG_TTY, "Modem - test for carrier\n");
		/*
		** ACTION
		** insert test for carrier here. -- ???
		** I already see that test here. What's the deal? -- REW
		*/
		if ((PortP->gs.tty->termios->c_cflag & CLOCAL) || (PortP->ModemState & MSVR1_CD))
		{
			rio_dprintk (RIO_DEBUG_TTY, "open(%d) Modem carr on\n", SysPort);
			/*
			tp->tm.c_state |= CARR_ON;
			wakeup((caddr_t) &tp->tm.c_canq);
			*/
			PortP->State |= RIO_CARR_ON;
			wake_up_interruptible (&PortP->gs.open_wait);
		}
		else /* no carrier - wait for DCD */
		{
		  /*
			while (!(PortP->gs.tty->termios->c_state & CARR_ON) && 
			       !(filp->f_flags & O_NONBLOCK) && !p->RIOHalted )
		  */
			while (!(PortP->State & RIO_CARR_ON) && 
			       !(filp->f_flags & O_NONBLOCK) && !p->RIOHalted ) {

				rio_dprintk (RIO_DEBUG_TTY, "open(%d) sleeping for carr on\n",SysPort);
				/*
				PortP->gs.tty->termios->c_state |= WOPEN;
				*/
				PortP->State |= RIO_WOPEN;
				rio_spin_unlock_irqrestore(&PortP->portSem, flags);
				if (RIODelay (PortP, HUNDRED_MS) == RIO_FAIL)
#if 0
				if ( sleep((caddr_t)&tp->tm.c_canqo, TTIPRI|PCATCH))
#endif
				{
					/*
					** ACTION: verify that this is a good thing
					** to do here. -- ???
					** I think it's OK. -- REW
					*/
					rio_dprintk (RIO_DEBUG_TTY, "open(%d) sleeping for carr broken by signal\n",
					       SysPort);
					RIOPreemptiveCmd( p, PortP, FCLOSE );
					/*
					tp->tm.c_state &= ~WOPEN;
					*/
					PortP->State &= ~RIO_WOPEN;
					rio_spin_unlock_irqrestore(&PortP->portSem, flags);
					func_exit ();
					return -EINTR;
				}
			}
			PortP->State &= ~RIO_WOPEN;
		}
		if ( p->RIOHalted )
			goto bombout;
		rio_dprintk (RIO_DEBUG_TTY, "Setting RIO_MOPEN\n");
		PortP->State |= RIO_MOPEN;
	}
	else
#endif
	{
		/*
		** ACTION
		** Direct line open - force carrier (will probably mean
		** that sleeping Modem line fubar)
		*/
		PortP->State |= RIO_LOPEN;
	}

	if ( p->RIOHalted ) {
		goto bombout;
	}

	rio_dprintk (RIO_DEBUG_TTY, "high level open done\n");

#ifdef STATS
	PortP->Stat.OpenCnt++;
#endif
	/*
	** Count opens for port statistics reporting
	*/
	if (PortP->statsGather)
		PortP->opens++;

	rio_spin_unlock_irqrestore(&PortP->portSem, flags);
	rio_dprintk (RIO_DEBUG_TTY, "Returning from open\n");
	func_exit ();
	return 0;
}

/*
** RIOClose the port.
** The operating system thinks that this is last close for the device.
** As there are two interfaces to the port (Modem and tty), we need to
** check that both are closed before we close the device.
*/ 
int
riotclose(void  *ptr)
{
#if 0
	register uint SysPort = dev;
	struct ttystatics *tp;		/* pointer to our ttystruct */
#endif
	struct Port *PortP = ptr;	/* pointer to the port structure */
	int deleted = 0;
	int	try = -1; /* Disable the timeouts by setting them to -1 */
	int	repeat_this = -1; /* Congrats to those having 15 years of 
				     uptime! (You get to break the driver.) */
	unsigned long end_time;
	struct tty_struct * tty;
	unsigned long flags;
	int Modem;
	int rv = 0;
	
	rio_dprintk (RIO_DEBUG_TTY, "port close SysPort %d\n",PortP->PortNum);

	/* PortP = p->RIOPortp[SysPort]; */
	rio_dprintk (RIO_DEBUG_TTY, "Port is at address 0x%x\n",(int)PortP);
	/* tp = PortP->TtyP;*/			/* Get tty */
	tty = PortP->gs.tty;
	rio_dprintk (RIO_DEBUG_TTY, "TTY is at address 0x%x\n",(int)tty);

	if (PortP->gs.closing_wait) 
		end_time = jiffies + PortP->gs.closing_wait;
	else 
		end_time = jiffies + MAX_SCHEDULE_TIMEOUT;

	Modem = rio_ismodem(tty);
#if 0
	/* What F.CKING cache? Even then, a higly idle multiprocessor,
	   system with large caches this won't work . Better find out when 
	   this doesn't work asap, and fix the cause.  -- REW */
	
	RIODelay(PortP, HUNDRED_MS*10);	/* To flush the cache */
#endif
	rio_spin_lock_irqsave(&PortP->portSem, flags);

	/*
	** Setting this flag will make any process trying to open
	** this port block until we are complete closing it.
	*/
	PortP->State |= RIO_CLOSING;

	if ( (PortP->State & RIO_DELETED) ) {
		rio_dprintk (RIO_DEBUG_TTY, "Close on deleted RTA\n");
		deleted = 1;
	}
	
	if ( p->RIOHalted ) {
		RIOClearUp( PortP );
		rv = -EIO;
		goto close_end;
	}

	rio_dprintk (RIO_DEBUG_TTY, "Clear bits\n");
	/*
	** clear the open bits for this device
	*/
	PortP->State &= (Modem ? ~RIO_MOPEN : ~RIO_LOPEN);
	PortP->State &= ~RIO_CARR_ON;
	PortP->ModemState &= ~MSVR1_CD;
	/*
	** If the device was open as both a Modem and a tty line
	** then we need to wimp out here, as the port has not really
	** been finally closed (gee, whizz!) The test here uses the
	** bit for the OTHER mode of operation, to see if THAT is
	** still active!
	*/
	if ( (PortP->State & (RIO_LOPEN|RIO_MOPEN)) ) {
		/*
		** The port is still open for the other task -
		** return, pretending that we are still active.
		*/
		rio_dprintk (RIO_DEBUG_TTY, "Channel %d still open !\n",PortP->PortNum);
		PortP->State &= ~RIO_CLOSING;
		if (PortP->firstOpen)
			PortP->firstOpen--;
		rio_spin_unlock_irqrestore(&PortP->portSem, flags);
		return -EIO;
	}

	rio_dprintk (RIO_DEBUG_TTY, "Closing down - everything must go!\n");

	PortP->State &= ~RIO_DYNOROD;

	/*
	** This is where we wait for the port
	** to drain down before closing. Bye-bye....
	** (We never meant to do this)
	*/
	rio_dprintk (RIO_DEBUG_TTY, "Timeout 1 starts\n");

	if (!deleted)
	while ( (PortP->InUse != NOT_INUSE) && !p->RIOHalted && 
		(PortP->TxBufferIn != PortP->TxBufferOut) ) {
		cprintf("Need to flush the ttyport\n");
		if (repeat_this -- <= 0) {
			rv = -EINTR;
			rio_dprintk (RIO_DEBUG_TTY, "Waiting for not idle closed broken by signal\n");
			RIOPreemptiveCmd(p, PortP, FCLOSE);
			goto close_end;
		}
		rio_dprintk (RIO_DEBUG_TTY, "Calling timeout to flush in closing\n");
		rio_spin_unlock_irqrestore(&PortP->portSem, flags);
		if (RIODelay_ni(PortP, HUNDRED_MS*10) == RIO_FAIL) {
			rio_dprintk (RIO_DEBUG_TTY, "RTA EINTR in delay \n");
			rv = -EINTR;
			rio_spin_lock_irqsave(&PortP->portSem, flags);
			goto close_end;
		}
		rio_spin_lock_irqsave(&PortP->portSem, flags);
	}

	PortP->TxBufferIn = PortP->TxBufferOut = 0;
	repeat_this = 0xff;

	PortP->InUse = 0;
	if ( (PortP->State & (RIO_LOPEN|RIO_MOPEN)) ) {
		/*
		** The port has been re-opened for the other task -
		** return, pretending that we are still active.
		*/
		rio_dprintk (RIO_DEBUG_TTY, "Channel %d re-open!\n", PortP->PortNum);
		PortP->State &= ~RIO_CLOSING;
		rio_spin_unlock_irqrestore(&PortP->portSem, flags);
		if (PortP->firstOpen)
			PortP->firstOpen--;
		return -EIO;
	}

	if ( p->RIOHalted ) {
		RIOClearUp( PortP );
		goto close_end;
	}

	/* Can't call RIOShortCommand with the port locked. */
	rio_spin_unlock_irqrestore(&PortP->portSem, flags);

	if (RIOShortCommand(p, PortP, CLOSE, 1, 0) == RIO_FAIL) {
		RIOPreemptiveCmd(p, PortP, FCLOSE);
		goto close_end;
	}

	if (!deleted)
	  while (try && (PortP->PortState & PORT_ISOPEN)) {
	        try--;
		if (time_after (jiffies, end_time)) {
		  rio_dprintk (RIO_DEBUG_TTY, "Run out of tries - force the bugger shut!\n" );
		  RIOPreemptiveCmd(p, PortP,FCLOSE);
		  break;
		}
		rio_dprintk (RIO_DEBUG_TTY, "Close: PortState:ISOPEN is %d\n", 
					   PortP->PortState & PORT_ISOPEN);

		if ( p->RIOHalted ) {
			RIOClearUp( PortP );
			goto close_end;
		}
		if (RIODelay(PortP, HUNDRED_MS) == RIO_FAIL) {
			rio_dprintk (RIO_DEBUG_TTY, "RTA EINTR in delay \n");
			RIOPreemptiveCmd(p, PortP,FCLOSE);
			break;
		}
	}
	rio_spin_lock_irqsave(&PortP->portSem, flags);
	rio_dprintk (RIO_DEBUG_TTY, "Close: try was %d on completion\n", try );
 
	/* RIOPreemptiveCmd(p, PortP, FCLOSE); */

/*
** 15.10.1998 ARG - ESIL 0761 part fix
** RIO has it's own CTSFLOW and RTSFLOW flags in 'Config' in the port structure,** we need to make sure that the flags are clear when the port is opened.
*/
	PortP->Config &= ~(RIO_CTSFLOW|RIO_RTSFLOW);

#ifdef STATS
	PortP->Stat.CloseCnt++;
#endif
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
	PortP->State &= ~(RIO_CLOSING|RIO_DELETED);
	if (PortP->firstOpen)
		PortP->firstOpen--;
	rio_spin_unlock_irqrestore(&PortP->portSem, flags);
	rio_dprintk (RIO_DEBUG_TTY, "Return from close\n");
	return rv;
}


/*
** decide if we need to use the line discipline.
** This routine can return one of three values:
** COOK_RAW if no processing has to be done by the line discipline or the card
** COOK_WELL if the line discipline must be used to do the processing
** COOK_MEDIUM if the card can do all the processing necessary.
*/
#if 0
static int
RIOCookMode(struct ttystatics *tp)
{
	/*
	** We can't handle tm.c_mstate != 0 on SCO
	** We can't handle mapping
	** We can't handle non-ttwrite line disc.
	** We can't handle lflag XCASE
	** We can handle oflag OPOST & (OCRNL, ONLCR, TAB3)
	*/

#ifdef CHECK
	CheckTtyP( tp );
#endif
	if (!(tp->tm.c_oflag & OPOST))	/* No post processing */
		return COOK_RAW;	/* Raw mode o/p */

	if ( tp->tm.c_lflag & XCASE )
		return COOK_WELL;	/* Use line disc */

	if (tp->tm.c_oflag & ~(OPOST | ONLCR | OCRNL | TAB3 ) )
		return COOK_WELL;	/* Use line disc for strange modes */

	if ( tp->tm.c_oflag == OPOST )	/* If only OPOST is set, do RAW */
		return COOK_RAW;

	/*
	** So, we need to output process!
	*/
	return COOK_MEDIUM;
}
#endif

static void
RIOClearUp(PortP)
struct Port *PortP;
{
	rio_dprintk (RIO_DEBUG_TTY, "RIOHalted set\n");
	PortP->Config = 0;	  /* Direct semaphore */
	PortP->PortState = 0;
	PortP->firstOpen = 0;
	PortP->FlushCmdBodge = 0;
	PortP->ModemState = PortP->CookMode = 0;
	PortP->Mapped = 0;
	PortP->WflushFlag = 0;
	PortP->MagicFlags	= 0;
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
int RIOShortCommand(struct rio_info *p, struct Port *PortP,
		int command, int len, int arg)
{
	PKT *PacketP;
	int		retries = 20; /* at 10 per second -> 2 seconds */
	unsigned long flags;

	rio_dprintk (RIO_DEBUG_TTY, "entering shortcommand.\n");
#ifdef CHECK
	CheckPortP( PortP );
	if ( len < 1 || len > 2 )
		cprintf(("STUPID LENGTH %d\n",len));
#endif

	if ( PortP->State & RIO_DELETED ) {
		rio_dprintk (RIO_DEBUG_TTY, "Short command to deleted RTA ignored\n");
		return RIO_FAIL;
	}
	rio_spin_lock_irqsave(&PortP->portSem, flags);

	/*
	** If the port is in use for pre-emptive command, then wait for it to 
	** be free again.
	*/
	while ( (PortP->InUse != NOT_INUSE) && !p->RIOHalted ) {
		rio_dprintk (RIO_DEBUG_TTY, "Waiting for not in use (%d)\n", 
					   retries);
		rio_spin_unlock_irqrestore(&PortP->portSem, flags);
		if (retries-- <= 0) {
			return RIO_FAIL;
		}
		if (RIODelay_ni(PortP, HUNDRED_MS) == RIO_FAIL) {
			return RIO_FAIL;
		}
		rio_spin_lock_irqsave(&PortP->portSem, flags);
	}
	if ( PortP->State & RIO_DELETED ) {
		rio_dprintk (RIO_DEBUG_TTY, "Short command to deleted RTA ignored\n");
		rio_spin_unlock_irqrestore(&PortP->portSem, flags);
		return RIO_FAIL;
	}

	while ( !can_add_transmit(&PacketP,PortP) && !p->RIOHalted ) {
		rio_dprintk (RIO_DEBUG_TTY, "Waiting to add short command to queue (%d)\n", retries);
		rio_spin_unlock_irqrestore(&PortP->portSem, flags);
		if (retries-- <= 0) {
		  rio_dprintk (RIO_DEBUG_TTY, "out of tries. Failing\n");
			return RIO_FAIL;
		}
		if ( RIODelay_ni(PortP, HUNDRED_MS)==RIO_FAIL ) {
			return RIO_FAIL;
		}
		rio_spin_lock_irqsave(&PortP->portSem, flags);
	}

	if ( p->RIOHalted ) {
		rio_spin_unlock_irqrestore(&PortP->portSem, flags);
		return RIO_FAIL;
	}

	/*
	** set the command byte and the argument byte
	*/
	WBYTE(PacketP->data[0] , command);

	if ( len==2 )
		WBYTE(PacketP->data[1] , arg);

	/*
	** set the length of the packet and set the command bit.
	*/
	WBYTE(PacketP->len , PKT_CMD_BIT | len);

	add_transmit(PortP);
	/*
	** Count characters transmitted for port statistics reporting
	*/
	if (PortP->statsGather)
		PortP->txchars += len;

	rio_spin_unlock_irqrestore(&PortP->portSem, flags);
	return p->RIOHalted ? RIO_FAIL : ~RIO_FAIL;
}


#if 0
/*
** This is an ioctl interface. This is the twentieth century. You know what
** its all about.
*/
int
riotioctl(struct rio_info *p, struct tty_struct *tty, int cmd, caddr_t arg)
{
	register struct		Port *PortP;
	register struct		ttystatics *tp;
	int					current;
	int					ParamSemIncremented = 0;
	int					old_oflag, old_cflag, old_iflag, changed, oldcook;
	int					i;
	unsigned char		sio_regs[5];		/* Here be magic */
	short				vpix_cflag;
	short				divisor;
	int					baud;
	uint				SysPort = rio_minor(tty);
	int				Modem = rio_ismodem(tty);
	int					ioctl_processed;

	rio_dprintk (RIO_DEBUG_TTY, "port ioctl SysPort %d command 0x%x argument 0x%x %s\n",
			SysPort, cmd, arg, Modem?"Modem":"tty") ;

	if ( SysPort >= RIO_PORTS ) {
		rio_dprintk (RIO_DEBUG_TTY, "Bad port number %d\n", SysPort);
		return -ENXIO;
	}

	PortP = p->RIOPortp[SysPort];
	tp = PortP->TtyP;

	rio_spin_lock_irqsave(&PortP->portSem, flags);

#ifdef STATS
	PortP->Stat.IoctlCnt++;
#endif

	if ( PortP->State & RIO_DELETED ) {
		rio_spin_unlock_irqrestore(&PortP->portSem, flags);
		return -EIO;
	}


	if ( p->RIOHalted ) {
		RIOClearUp( PortP );
		rio_spin_unlock_irqrestore(&PortP->portSem, flags);
		return -EIO;
	}

	/*
	** Count ioctls for port statistics reporting
	*/
	if (PortP->statsGather)
		PortP->ioctls++;

	/*
	** Specialix RIO Ioctl calls
	*/
	switch (cmd) {

		case TCRIOTRIAD:
			if ( arg )
				PortP->State |= RIO_TRIAD_MODE;
			else
				PortP->State &= ~RIO_TRIAD_MODE;
			/*
			** Normally, when istrip is set on a port, a config is
			** sent to the RTA instructing the CD1400 to do the
			** stripping. In TRIAD mode, the interrupt receive routine
			** must do the stripping instead, since it has to detect
			** an 8 bit function key sequence. If istrip is set with
			** TRIAD mode on(off), and 8 bit data is being read by
			** the port, the user then turns TRIAD mode off(on), the RTA
			** must be reconfigured (not) to do the stripping.
			** Hence we call RIOParam here.
			*/
			rio_spin_unlock_irqrestore(&PortP->portSem, flags);
			RIOParam(PortP,CONFIG,Modem,OK_TO_SLEEP);	
			return 0;

		case TCRIOTSTATE:
			rio_dprintk (RIO_DEBUG_TTY, "tbusy/tstop monitoring %sabled\n",
		 		arg ? "en" : "dis");
			/* MonitorTstate = 0 ;*/
			rio_spin_unlock_irqrestore(&PortP->portSem, flags);
			RIOParam(PortP, CONFIG, Modem, OK_TO_SLEEP);
			return 0;

		case TCRIOSTATE: /* current state of Modem input pins */
			rio_dprintk (RIO_DEBUG_TTY, "TCRIOSTATE\n");
			if (RIOPreemptiveCmd(p, PortP, MGET) == RIO_FAIL)
				rio_dprintk (RIO_DEBUG_TTY, "TCRIOSTATE command failed\n");
			PortP->State |= RIO_BUSY;
			current = PortP->ModemState;
			if ( copyout((caddr_t)&current, (int)arg,
							sizeof(current))==COPYFAIL ) {
				rio_dprintk (RIO_DEBUG_TTY, "Copyout failed\n");
				rio_spin_unlock_irqrestore(&PortP->portSem, flags);
				pseterr(EFAULT);
			}
			rio_spin_unlock_irqrestore(&PortP->portSem, flags);
			return 0;

		case TCRIOMBIS:		/* Set modem lines */
		case TCRIOMBIC:		/* Clear modem lines */
			rio_dprintk (RIO_DEBUG_TTY, "TCRIOMBIS/TCRIOMBIC\n");
			if (cmd == TCRIOMBIS) {
				uint		state;
				state = (uint)arg;
				PortP->ModemState |= (ushort)state;
				PortP->ModemLines = (ulong) arg;
				if (RIOPreemptiveCmd(p, PortP, MBIS) == RIO_FAIL)
					rio_dprintk (RIO_DEBUG_TTY, 
					 "TCRIOMBIS command failed\n");
			}
			else {
				uint		state;

				state = (uint)arg;
				PortP->ModemState &= ~(ushort)state;
				PortP->ModemLines = (ulong) arg;
				if (RIOPreemptiveCmd(p, PortP, MBIC) == RIO_FAIL)
					rio_dprintk (RIO_DEBUG_TTY, "TCRIOMBIC command failed\n");
			}
			PortP->State |= RIO_BUSY;
			rio_spin_unlock_irqrestore(&PortP->portSem, flags);
			return 0;

		case TCRIOXPON: /* set Xprint ON string */
			rio_dprintk (RIO_DEBUG_TTY, "TCRIOXPON\n");
			if ( copyin((int)arg, (caddr_t)PortP->Xprint.XpOn,
						MAX_XP_CTRL_LEN)==COPYFAIL ) {
				rio_dprintk (RIO_DEBUG_TTY, "Copyin failed\n");
				PortP->Xprint.XpOn[0] = '\0';
				rio_spin_unlock_irqrestore(&PortP->portSem, flags);
				pseterr(EFAULT);
			}
			PortP->Xprint.XpOn[MAX_XP_CTRL_LEN-1] = '\0';
			PortP->Xprint.XpLen = strlen(PortP->Xprint.XpOn)+
												strlen(PortP->Xprint.XpOff);
			rio_spin_unlock_irqrestore(&PortP->portSem, flags);
			return 0;

		case TCRIOXPOFF: /* set Xprint OFF string */
			rio_dprintk (RIO_DEBUG_TTY, "TCRIOXPOFF\n");
			if ( copyin( (int)arg, (caddr_t)PortP->Xprint.XpOff,
						MAX_XP_CTRL_LEN)==COPYFAIL ) {
				rio_dprintk (RIO_DEBUG_TTY, "Copyin failed\n");
				PortP->Xprint.XpOff[0] = '\0';
				rio_spin_unlock_irqrestore(&PortP->portSem, flags);
				pseterr(EFAULT);
			}
			PortP->Xprint.XpOff[MAX_XP_CTRL_LEN-1] = '\0';
			PortP->Xprint.XpLen = strlen(PortP->Xprint.XpOn)+
										strlen(PortP->Xprint.XpOff);
			rio_spin_unlock_irqrestore(&PortP->portSem, flags);
			return 0;

		case TCRIOXPCPS: /* set Xprint CPS string */
			rio_dprintk (RIO_DEBUG_TTY, "TCRIOXPCPS\n");
			if ( (uint)arg > p->RIOConf.MaxXpCps || 
					(uint)arg < p->RIOConf.MinXpCps ) {
				rio_dprintk (RIO_DEBUG_TTY, "%d CPS out of range\n",arg);
				rio_spin_unlock_irqrestore(&PortP->portSem, flags);
				pseterr(EINVAL);
				return 0;
			}
			PortP->Xprint.XpCps = (uint)arg;
			rio_spin_unlock_irqrestore(&PortP->portSem, flags);
			return 0;

		case TCRIOXPRINT:
			rio_dprintk (RIO_DEBUG_TTY, "TCRIOXPRINT\n");
			if ( copyout((caddr_t)&PortP->Xprint, (int)arg,
					sizeof(struct Xprint))==COPYFAIL ) {
			        rio_spin_unlock_irqrestore(&PortP->portSem, flags);
				pseterr(EFAULT);
			}
			rio_spin_unlock_irqrestore(&PortP->portSem, flags);
			return 0;

		case TCRIOIXANYON:
			rio_dprintk (RIO_DEBUG_TTY, "TCRIOIXANYON\n");
			PortP->Config |= RIO_IXANY;
			rio_spin_unlock_irqrestore(&PortP->portSem, flags);
			return 0;

		case TCRIOIXANYOFF:
			rio_dprintk (RIO_DEBUG_TTY, "TCRIOIXANYOFF\n");
			PortP->Config &= ~RIO_IXANY;
			rio_spin_unlock_irqrestore(&PortP->portSem, flags);
			return 0;

		case TCRIOIXONON:
			rio_dprintk (RIO_DEBUG_TTY, "TCRIOIXONON\n");
			PortP->Config |= RIO_IXON;
			rio_spin_unlock_irqrestore(&PortP->portSem, flags);
			return 0;

		case TCRIOIXONOFF:
			rio_dprintk (RIO_DEBUG_TTY, "TCRIOIXONOFF\n");
			PortP->Config &= ~RIO_IXON;
			rio_spin_unlock_irqrestore(&PortP->portSem, flags);
			return 0;

/*
** 15.10.1998 ARG - ESIL 0761 part fix
** Added support for CTS and RTS flow control ioctls :
*/
		case TCRIOCTSFLOWEN:
			rio_dprintk (RIO_DEBUG_TTY, "TCRIOCTSFLOWEN\n");
			PortP->Config |= RIO_CTSFLOW;
			rio_spin_unlock_irqrestore(&PortP->portSem, flags);
			RIOParam(PortP,CONFIG,Modem,OK_TO_SLEEP);	
			return 0;

		case TCRIOCTSFLOWDIS:
			rio_dprintk (RIO_DEBUG_TTY, "TCRIOCTSFLOWDIS\n");
			PortP->Config &= ~RIO_CTSFLOW;
			rio_spin_unlock_irqrestore(&PortP->portSem, flags);
			RIOParam(PortP,CONFIG,Modem,OK_TO_SLEEP);	
			return 0;

		case TCRIORTSFLOWEN:
			rio_dprintk (RIO_DEBUG_TTY, "TCRIORTSFLOWEN\n");
			PortP->Config |= RIO_RTSFLOW;
			rio_spin_unlock_irqrestore(&PortP->portSem, flags);
			RIOParam(PortP,CONFIG,Modem,OK_TO_SLEEP);	
			return 0;

		case TCRIORTSFLOWDIS:
			rio_dprintk (RIO_DEBUG_TTY, "TCRIORTSFLOWDIS\n");
			PortP->Config &= ~RIO_RTSFLOW;
			rio_spin_unlock_irqrestore(&PortP->portSem, flags);
			RIOParam(PortP,CONFIG,Modem,OK_TO_SLEEP);	
			return 0;

/* end ESIL 0761 part fix */

	}


	/* Lynx IOCTLS */
	switch (cmd) {
		case TIOCSETP:
		case TIOCSETN:
		case OTIOCSETP:
		case OTIOCSETN:
			ioctl_processed++;
			ttyseth(PortP, tp, (struct old_sgttyb *)arg);
			break;
		case TCSETA:
		case TCSETAW:
		case TCSETAF:
			ioctl_processed++;
			rio_dprintk (RIO_DEBUG_TTY, "NON POSIX ioctl\n");
			ttyseth_pv(PortP, tp, (struct termios *)arg, 0);
			break;
		case TCSETAP:	/* posix tcsetattr() */
		case TCSETAWP:	/* posix tcsetattr() */
		case TCSETAFP:	/* posix tcsetattr() */
			rio_dprintk (RIO_DEBUG_TTY, "NON POSIX SYSV ioctl\n");
			ttyseth_pv(PortP, tp, (struct termios *)arg, 1);
			ioctl_processed++;
			break;
	}

	/*
	** If its any of the commands that require the port to be in the
	** non-busy state wait until all output has drained 
	*/
	if (!ioctl_processed)
	switch(cmd) {
		case TCSETAW:
		case TCSETAF:
		case TCSETA:
		case TCSBRK:
#define OLD_POSIX ('x' << 8)
#define OLD_POSIX_SETA (OLD_POSIX | 2)
#define OLD_POSIX_SETAW (OLD_POSIX | 3)
#define OLD_POSIX_SETAF (OLD_POSIX | 4)
#define NEW_POSIX (('i' << 24) | ('X' << 16))
#define NEW_POSIX_SETA (NEW_POSIX | 2)
#define NEW_POSIX_SETAW (NEW_POSIX | 3)
#define NEW_POSIX_SETAF (NEW_POSIX | 4)
		case OLD_POSIX_SETA:
		case OLD_POSIX_SETAW:
		case OLD_POSIX_SETAF:
		case NEW_POSIX_SETA:
		case NEW_POSIX_SETAW:
		case NEW_POSIX_SETAF:
#ifdef TIOCSETP
		case TIOCSETP:
#endif
		case TIOCSETD:
		case TIOCSETN:
			rio_dprintk (RIO_DEBUG_TTY, "wait for non-BUSY, semaphore set\n");
			/*
			** Wait for drain here, at least as far as the double buffer
			** being empty.
			*/
			/* XXX Does the above comment mean that this has
			   still to be implemented? -- REW */
			/* XXX Is the locking OK together with locking
                           in txenable? (Deadlock?) -- REW */
			
			RIOTxEnable((char *)PortP);
			break;
		default:
			break;
	}

	old_cflag = tp->tm.c_cflag;
	old_iflag = tp->tm.c_iflag;
	old_oflag = tp->tm.c_oflag;
	oldcook = PortP->CookMode;

	if ( p->RIOHalted ) {
		RIOClearUp( PortP );
		rio_spin_unlock_irqrestore(&PortP->portSem, flags);
		pseterr(EIO);
		return 0;
	}

	PortP->FlushCmdBodge = 0;

	/*
	** If the port is locked, and it is reconfigured, we want
	** to restore the state of the tty structure so the change is NOT
	** made.
	*/
	if (PortP->Lock) {
		tp->tm.c_iflag = PortP->StoredTty.iflag;
		tp->tm.c_oflag = PortP->StoredTty.oflag;
		tp->tm.c_cflag = PortP->StoredTty.cflag;
		tp->tm.c_lflag = PortP->StoredTty.lflag;
		tp->tm.c_line = PortP->StoredTty.line;
		for (i = 0; i < NCC + 1; i++)
			tp->tm.c_cc[i] = PortP->StoredTty.cc[i];
	}
	else {
		/*
		** If the port is set to store the parameters, and it is
		** reconfigured, we want to save the current tty struct so it
		** may be restored on the next open.
		*/
		if (PortP->Store) {
			PortP->StoredTty.iflag = tp->tm.c_iflag;
			PortP->StoredTty.oflag = tp->tm.c_oflag;
			PortP->StoredTty.cflag = tp->tm.c_cflag;
			PortP->StoredTty.lflag = tp->tm.c_lflag;
			PortP->StoredTty.line = tp->tm.c_line;
			for (i = 0; i < NCC + 1; i++)
				PortP->StoredTty.cc[i] = tp->tm.c_cc[i];
		}
	}

	changed = (tp->tm.c_cflag != old_cflag) ||
				(tp->tm.c_iflag != old_iflag) ||
				(tp->tm.c_oflag != old_oflag);

	PortP->CookMode = RIOCookMode(tp);	/* Set new cooking mode */

	rio_dprintk (RIO_DEBUG_TTY, "RIOIoctl changed %d newcook %d oldcook %d\n",
			changed,PortP->CookMode,oldcook);

#ifdef MODEM_SUPPORT
	/*
	** kludge to force CARR_ON if CLOCAL set
	*/
	if ((tp->tm.c_cflag & CLOCAL) || (PortP->ModemState & MSVR1_CD))	{
		tp->tm.c_state |= CARR_ON;
		wakeup ((caddr_t)&tp->tm.c_canq);
	}
#endif

	if ( p->RIOHalted ) {
		RIOClearUp( PortP );
		rio_spin_unlock_irqrestore(&PortP->portSem, flags);
		pseterr(EIO);
		return 0;
	}
	/*
	** Re-configure if modes or cooking have changed
	*/
	if (changed || oldcook != PortP->CookMode || (ioctl_processed)) {
		rio_spin_unlock_irqrestore(&PortP->portSem, flags);
		rio_dprintk (RIO_DEBUG_TTY, "Ioctl changing the PORT settings\n");
		RIOParam(PortP,CONFIG,Modem,OK_TO_SLEEP);	
		rio_spin_lock_irqsave(&PortP->portSem, flags);
	}

	if (p->RIOHalted) {
		rio_spin_unlock_irqrestore(&PortP->portSem, flags);
		RIOClearUp( PortP );
		pseterr(EIO);
		return 0;
	}
	rio_spin_unlock_irqrestore(&PortP->portSem, flags);
	return 0;
}

/*
	ttyseth -- set hardware dependent tty settings
*/
void
ttyseth(PortP, s, sg)
struct Port *		PortP;
struct ttystatics *		s;
struct old_sgttyb *sg;
{
	struct old_sgttyb *	tsg;
	struct termios *tp = &s->tm;

	tsg = &s->sg;

	if (sg->sg_flags & (EVENP|ODDP))  {
		tp->c_cflag &= PARENB;
		if (sg->sg_flags & EVENP) {
			if (sg->sg_flags & ODDP) {
				tp->c_cflag &= V_CS7;
				tp->c_cflag &= ~PARENB;
			}
			else {
				tp->c_cflag &= V_CS7;
				tp->c_cflag &= PARENB;
				tp->c_cflag &= PARODD;
			}
		}
		else if (sg->sg_flags & ODDP) {
			tp->c_cflag &= V_CS7;
			tp->c_cflag &= PARENB;
			tp->c_cflag &= PARODD;
		}
		else {
			tp->c_cflag &= V_CS7;
			tp->c_cflag &= PARENB;
		}
	}
/*
 * Use ispeed as the desired speed.  Most implementations don't handle 
 * separate input and output speeds very well. If the RIO handles this, 
 * I will have to use separate sets of flags to store them in the 
 * Port structure.
 */
	if ( !sg->sg_ospeed )
		sg->sg_ospeed = sg->sg_ispeed;
	else
		sg->sg_ispeed = sg->sg_ospeed;
	if (sg->sg_ispeed > V_EXTB ) 
		sg->sg_ispeed = V_EXTB;
	if (sg->sg_ispeed < V_B0)
		sg->sg_ispeed = V_B0;
	*tsg = *sg;
   tp->c_cflag = (tp->c_cflag & ~V_CBAUD) | conv_bv[(int)sg->sg_ispeed];
}

/*
	ttyseth_pv -- set hardware dependent tty settings using either the
			POSIX termios structure or the System V termio structure.
				sysv = 0 => (POSIX):	 struct termios *sg
				sysv != 0 => (System V): struct termio *sg
*/
static void
ttyseth_pv(PortP, s, sg, sysv)
struct Port *PortP;
struct ttystatics *s;
struct termios *sg;
int sysv;
{
    int speed;
    unsigned char csize;
    unsigned char cread;
    unsigned int lcr_flags;
    int ps;
 
    if (sysv) {
        /* sg points to a System V termio structure */
        csize = ((struct termio *)sg)->c_cflag & CSIZE;
        cread = ((struct termio *)sg)->c_cflag & CREAD;
        speed = conv_vb[((struct termio *)sg)->c_cflag & V_CBAUD];
    }
    else {
        /* sg points to a POSIX termios structure */
        csize = sg->c_cflag & CSIZE;
        cread = sg->c_cflag & CREAD;
        speed = conv_vb[sg->c_cflag & V_CBAUD];
    }
    if (s->sg.sg_ispeed != speed || s->sg.sg_ospeed != speed) {
        s->sg.sg_ispeed = speed;
        s->sg.sg_ospeed = speed;
        s->tm.c_cflag = (s->tm.c_cflag & ~V_CBAUD) |
                         conv_bv[(int)s->sg.sg_ispeed];
    }
}
#endif
