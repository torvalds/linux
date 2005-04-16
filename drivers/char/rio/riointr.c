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
**	Module		: riointr.c
**	SID		: 1.2
**	Last Modified	: 11/6/98 10:33:44
**	Retrieved	: 11/6/98 10:33:49
**
**  ident @(#)riointr.c	1.2
**
** -----------------------------------------------------------------------------
*/
#ifdef SCCS_LABELS
static char *_riointr_c_sccs_ = "@(#)riointr.c	1.2";
#endif


#include <linux/module.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/tty.h>
#include <asm/io.h>
#include <asm/system.h>
#include <asm/string.h>
#include <asm/semaphore.h>
#include <asm/uaccess.h>

#include <linux/termios.h>
#include <linux/serial.h>

#include <linux/generic_serial.h>

#include <linux/delay.h>

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


static void RIOReceive(struct rio_info *, struct Port *);


static char *firstchars (char *p, int nch)
{
  static char buf[2][128];
  static int t=0;
  t = ! t;
  memcpy (buf[t], p, nch);
  buf[t][nch] = 0;
  return buf[t];
}


#define	INCR( P, I )	((P) = (((P)+(I)) & p->RIOBufferMask))
/* Enable and start the transmission of packets */
void
RIOTxEnable(en)
char *		en;
{
  struct Port *	PortP;
  struct rio_info *p;
  struct tty_struct* tty;
  int c;
  struct PKT *	PacketP;
  unsigned long flags;

  PortP = (struct Port *)en; 
  p = (struct rio_info *)PortP->p;
  tty = PortP->gs.tty;


  rio_dprintk (RIO_DEBUG_INTR, "tx port %d: %d chars queued.\n", 
	      PortP->PortNum, PortP->gs.xmit_cnt);

  if (!PortP->gs.xmit_cnt) return;
  

  /* This routine is an order of magnitude simpler than the specialix
     version. One of the disadvantages is that this version will send
     an incomplete packet (usually 64 bytes instead of 72) once for
     every 4k worth of data. Let's just say that this won't influence
     performance significantly..... */

  rio_spin_lock_irqsave(&PortP->portSem, flags);

  while (can_add_transmit( &PacketP, PortP )) {
    c = PortP->gs.xmit_cnt;
    if (c > PKT_MAX_DATA_LEN) c = PKT_MAX_DATA_LEN;

    /* Don't copy past the end of the source buffer */
    if (c > SERIAL_XMIT_SIZE - PortP->gs.xmit_tail) 
      c = SERIAL_XMIT_SIZE - PortP->gs.xmit_tail;

    { int t;
    t = (c > 10)?10:c;
    
    rio_dprintk (RIO_DEBUG_INTR, "rio: tx port %d: copying %d chars: %s - %s\n", 
		 PortP->PortNum, c, 
		 firstchars (PortP->gs.xmit_buf + PortP->gs.xmit_tail      , t),
		 firstchars (PortP->gs.xmit_buf + PortP->gs.xmit_tail + c-t, t));
    }
    /* If for one reason or another, we can't copy more data, 
       we're done! */
    if (c == 0) break;

    rio_memcpy_toio (PortP->HostP->Caddr, (caddr_t)PacketP->data, 
		 PortP->gs.xmit_buf + PortP->gs.xmit_tail, c);
    /*    udelay (1); */

    writeb (c, &(PacketP->len));
    if (!( PortP->State & RIO_DELETED ) ) {
      add_transmit ( PortP );
      /*
      ** Count chars tx'd for port statistics reporting
      */
      if ( PortP->statsGather )
	PortP->txchars += c;
    }
    PortP->gs.xmit_tail = (PortP->gs.xmit_tail + c) & (SERIAL_XMIT_SIZE-1);
    PortP->gs.xmit_cnt -= c;
  }

  rio_spin_unlock_irqrestore(&PortP->portSem, flags);

  if (PortP->gs.xmit_cnt <= (PortP->gs.wakeup_chars + 2*PKT_MAX_DATA_LEN)) {
    rio_dprintk (RIO_DEBUG_INTR, "Waking up.... ldisc:%d (%d/%d)....",
		 (int)(PortP->gs.tty->flags & (1 << TTY_DO_WRITE_WAKEUP)),
		 PortP->gs.wakeup_chars, PortP->gs.xmit_cnt); 
    if ((PortP->gs.tty->flags & (1 << TTY_DO_WRITE_WAKEUP)) &&
	PortP->gs.tty->ldisc.write_wakeup)
      (PortP->gs.tty->ldisc.write_wakeup)(PortP->gs.tty);
    rio_dprintk (RIO_DEBUG_INTR, "(%d/%d)\n",
		PortP->gs.wakeup_chars, PortP->gs.xmit_cnt); 
    wake_up_interruptible(&PortP->gs.tty->write_wait);
  }

}


/*
** RIO Host Service routine. Does all the work traditionally associated with an
** interrupt.
*/
static int	RupIntr;
static int	RxIntr;
static int	TxIntr;
void
RIOServiceHost(p, HostP, From)
struct rio_info *	p;
struct Host *HostP;
int From; 
{
  rio_spin_lock (&HostP->HostLock);
  if ( (HostP->Flags & RUN_STATE) != RC_RUNNING ) { 
    static int t =0;
    rio_spin_unlock (&HostP->HostLock); 
    if ((t++ % 200) == 0)
      rio_dprintk (RIO_DEBUG_INTR, "Interrupt but host not running. flags=%x.\n", (int)HostP->Flags);
    return;
  }
  rio_spin_unlock (&HostP->HostLock); 

  if ( RWORD( HostP->ParmMapP->rup_intr ) ) {
    WWORD( HostP->ParmMapP->rup_intr , 0 );
    p->RIORupCount++;
    RupIntr++;
    rio_dprintk (RIO_DEBUG_INTR, "rio: RUP interrupt on host %d\n", HostP-p->RIOHosts);
    RIOPollHostCommands(p, HostP );
  }

  if ( RWORD( HostP->ParmMapP->rx_intr ) ) {
    int port;

    WWORD( HostP->ParmMapP->rx_intr , 0 );
    p->RIORxCount++;
    RxIntr++;

    rio_dprintk (RIO_DEBUG_INTR, "rio: RX interrupt on host %d\n", HostP-p->RIOHosts);
    /*
    ** Loop through every port. If the port is mapped into
    ** the system ( i.e. has /dev/ttyXXXX associated ) then it is
    ** worth checking. If the port isn't open, grab any packets
    ** hanging on its receive queue and stuff them on the free
    ** list; check for commands on the way.
    */
    for ( port=p->RIOFirstPortsBooted; 
	  port<p->RIOLastPortsBooted+PORTS_PER_RTA; port++ ) {
      struct Port *PortP = p->RIOPortp[port];
      struct tty_struct *ttyP;
      struct PKT *PacketP;
		
      /*
      ** not mapped in - most of the RIOPortp[] information
      ** has not been set up!
      ** Optimise: ports come in bundles of eight.
      */
      if ( !PortP->Mapped ) {
	port += 7;
	continue; /* with the next port */
      }

      /*
      ** If the host board isn't THIS host board, check the next one.
      ** optimise: ports come in bundles of eight.
      */
      if ( PortP->HostP != HostP ) {
	port += 7;
	continue;
      }

      /*
      ** Let us see - is the port open? If not, then don't service it.
      */
      if ( !( PortP->PortState & PORT_ISOPEN ) ) {
	continue;
      }

      /*
      ** find corresponding tty structure. The process of mapping
      ** the ports puts these here.
      */
      ttyP = PortP->gs.tty;

      /*
      ** Lock the port before we begin working on it.
      */
      rio_spin_lock(&PortP->portSem);

      /*
      ** Process received data if there is any.
      */
      if ( can_remove_receive( &PacketP, PortP ) )
	RIOReceive(p, PortP);

      /*
      ** If there is no data left to be read from the port, and
      ** it's handshake bit is set, then we must clear the handshake,
      ** so that that downstream RTA is re-enabled.
      */
      if ( !can_remove_receive( &PacketP, PortP ) && 
	   ( RWORD( PortP->PhbP->handshake )==PHB_HANDSHAKE_SET ) ) {
				/*
				** MAGIC! ( Basically, handshake the RX buffer, so that
				** the RTAs upstream can be re-enabled. )
				*/
	rio_dprintk (RIO_DEBUG_INTR, "Set RX handshake bit\n");
	WWORD( PortP->PhbP->handshake, 
	       PHB_HANDSHAKE_SET|PHB_HANDSHAKE_RESET );
      }
      rio_spin_unlock(&PortP->portSem);
    }
  }

  if ( RWORD( HostP->ParmMapP->tx_intr ) ) {
    int port;

    WWORD( HostP->ParmMapP->tx_intr , 0);

    p->RIOTxCount++;
    TxIntr++;
    rio_dprintk (RIO_DEBUG_INTR, "rio: TX interrupt on host %d\n", HostP-p->RIOHosts);

    /*
    ** Loop through every port.
    ** If the port is mapped into the system ( i.e. has /dev/ttyXXXX
    ** associated ) then it is worth checking.
    */
    for ( port=p->RIOFirstPortsBooted; 
	  port<p->RIOLastPortsBooted+PORTS_PER_RTA; port++ ) {
      struct Port *PortP = p->RIOPortp[port];
      struct tty_struct *ttyP;
      struct PKT *PacketP;

      /*
      ** not mapped in - most of the RIOPortp[] information
      ** has not been set up!
      */
      if ( !PortP->Mapped ) {
	port += 7;
	continue; /* with the next port */
      }

      /*
      ** If the host board isn't running, then its data structures
      ** are no use to us - continue quietly.
      */
      if ( PortP->HostP != HostP ) {
	port += 7;
	continue; /* with the next port */
      }

      /*
      ** Let us see - is the port open? If not, then don't service it.
      */
      if ( !( PortP->PortState & PORT_ISOPEN ) ) {
	continue;
      }

      rio_dprintk (RIO_DEBUG_INTR, "rio: Looking into port %d.\n", port);
      /*
      ** Lock the port before we begin working on it.
      */
      rio_spin_lock(&PortP->portSem);

      /*
      ** If we can't add anything to the transmit queue, then
      ** we need do none of this processing.
      */
      if ( !can_add_transmit( &PacketP, PortP ) ) {
	rio_dprintk (RIO_DEBUG_INTR, "Can't add to port, so skipping.\n");
	rio_spin_unlock(&PortP->portSem);
	continue;
      }

      /*
      ** find corresponding tty structure. The process of mapping
      ** the ports puts these here.
      */
      ttyP = PortP->gs.tty;
      /* If ttyP is NULL, the port is getting closed. Forget about it. */
      if (!ttyP) {
	rio_dprintk (RIO_DEBUG_INTR, "no tty, so skipping.\n");
	rio_spin_unlock(&PortP->portSem);
	continue;
      }
      /*
      ** If there is more room available we start up the transmit
      ** data process again. This can be direct I/O, if the cookmode
      ** is set to COOK_RAW or COOK_MEDIUM, or will be a call to the
      ** riotproc( T_OUTPUT ) if we are in COOK_WELL mode, to fetch
      ** characters via the line discipline. We must always call
      ** the line discipline,
      ** so that user input characters can be echoed correctly.
      **
      ** ++++ Update +++++
      ** With the advent of double buffering, we now see if
      ** TxBufferOut-In is non-zero. If so, then we copy a packet
      ** to the output place, and set it going. If this empties
      ** the buffer, then we must issue a wakeup( ) on OUT.
      ** If it frees space in the buffer then we must issue
      ** a wakeup( ) on IN.
      **
      ** ++++ Extra! Extra! If PortP->WflushFlag is set, then we
      ** have to send a WFLUSH command down the PHB, to mark the
      ** end point of a WFLUSH. We also need to clear out any
      ** data from the double buffer! ( note that WflushFlag is a
      ** *count* of the number of WFLUSH commands outstanding! )
      **
      ** ++++ And there's more!
      ** If an RTA is powered off, then on again, and rebooted,
      ** whilst it has ports open, then we need to re-open the ports.
      ** ( reasonable enough ). We can't do this when we spot the
      ** re-boot, in interrupt time, because the queue is probably
      ** full. So, when we come in here, we need to test if any
      ** ports are in this condition, and re-open the port before
      ** we try to send any more data to it. Now, the re-booted
      ** RTA will be discarding packets from the PHB until it
      ** receives this open packet, but don't worry tooo much
      ** about that. The one thing that is interesting is the
      ** combination of this effect and the WFLUSH effect!
      */
      /* For now don't handle RTA reboots. -- REW. 
	 Reenabled. Otherwise RTA reboots didn't work. Duh. -- REW */
      if ( PortP->MagicFlags ) {
#if 1
	if ( PortP->MagicFlags & MAGIC_REBOOT ) {
	  /*
	  ** well, the RTA has been rebooted, and there is room
	  ** on its queue to add the open packet that is required.
	  **
	  ** The messy part of this line is trying to decide if
	  ** we need to call the Param function as a tty or as
	  ** a modem.
	  ** DONT USE CLOCAL AS A TEST FOR THIS!
	  **
	  ** If we can't param the port, then move on to the
	  ** next port.
	  */
	  PortP->InUse = NOT_INUSE;

	  rio_spin_unlock(&PortP->portSem);
	  if ( RIOParam(PortP, OPEN, ((PortP->Cor2Copy & 
				       (COR2_RTSFLOW|COR2_CTSFLOW ) )== 
				      (COR2_RTSFLOW|COR2_CTSFLOW ) ) ? 
			TRUE : FALSE, DONT_SLEEP ) == RIO_FAIL ) {
	    continue; /* with next port */
	  }
	  rio_spin_lock(&PortP->portSem);
	  PortP->MagicFlags &= ~MAGIC_REBOOT;
	}
#endif

	/*
	** As mentioned above, this is a tacky hack to cope
	** with WFLUSH
	*/
	if ( PortP->WflushFlag ) {
	  rio_dprintk (RIO_DEBUG_INTR, "Want to WFLUSH mark this port\n");

	  if ( PortP->InUse )
	    rio_dprintk (RIO_DEBUG_INTR, "FAILS - PORT IS IN USE\n");
	}
				
	while ( PortP->WflushFlag &&
		can_add_transmit( &PacketP, PortP ) && 
		( PortP->InUse == NOT_INUSE ) ) {
	  int p;
	  struct PktCmd *PktCmdP;

	  rio_dprintk (RIO_DEBUG_INTR, "Add WFLUSH marker to data queue\n");
	  /*
	  ** make it look just like a WFLUSH command
	  */
	  PktCmdP = ( struct PktCmd * )&PacketP->data[0];

	  WBYTE( PktCmdP->Command , WFLUSH );

	  p =  PortP->HostPort % ( ushort )PORTS_PER_RTA;

	  /*
	  ** If second block of ports for 16 port RTA, add 8
	  ** to index 8-15.
	  */
	  if ( PortP->SecondBlock )
	    p += PORTS_PER_RTA;

	  WBYTE( PktCmdP->PhbNum, p );

	  /*
	  ** to make debuggery easier
	  */
	  WBYTE( PacketP->data[ 2], 'W'  );
	  WBYTE( PacketP->data[ 3], 'F'  );
	  WBYTE( PacketP->data[ 4], 'L'  );
	  WBYTE( PacketP->data[ 5], 'U'  );
	  WBYTE( PacketP->data[ 6], 'S'  );
	  WBYTE( PacketP->data[ 7], 'H'  );
	  WBYTE( PacketP->data[ 8], ' '  );
	  WBYTE( PacketP->data[ 9], '0'+PortP->WflushFlag );
	  WBYTE( PacketP->data[10], ' '  );
	  WBYTE( PacketP->data[11], ' '  );
	  WBYTE( PacketP->data[12], '\0' );

	  /*
	  ** its two bytes long!
	  */
	  WBYTE( PacketP->len , PKT_CMD_BIT | 2 );

	  /*
	  ** queue it!
	  */
	  if ( !( PortP->State & RIO_DELETED ) ) {
	    add_transmit( PortP );
	    /*
	    ** Count chars tx'd for port statistics reporting
	    */
	    if ( PortP->statsGather )
	      PortP->txchars += 2;
	  }

	  if ( --( PortP->WflushFlag ) == 0 ) {
	    PortP->MagicFlags &= ~MAGIC_FLUSH;
	  }

	  rio_dprintk (RIO_DEBUG_INTR, "Wflush count now stands at %d\n", 
		 PortP->WflushFlag);
	}
	if ( PortP->MagicFlags & MORE_OUTPUT_EYGOR ) {
	  if ( PortP->MagicFlags & MAGIC_FLUSH ) {
	    PortP->MagicFlags |= MORE_OUTPUT_EYGOR;
	  }
	  else {
	    if ( !can_add_transmit( &PacketP, PortP ) ) {
	      rio_spin_unlock(&PortP->portSem);
	      continue;
	    }
	    rio_spin_unlock(&PortP->portSem);
	    RIOTxEnable((char *)PortP);
	    rio_spin_lock(&PortP->portSem);
	    PortP->MagicFlags &= ~MORE_OUTPUT_EYGOR;
	  }
	}
      }


      /*
      ** If we can't add anything to the transmit queue, then
      ** we need do none of the remaining processing.
      */
      if (!can_add_transmit( &PacketP, PortP ) ) {
	rio_spin_unlock(&PortP->portSem);
	continue;
      }

      rio_spin_unlock(&PortP->portSem);
      RIOTxEnable((char *)PortP);
    }
  }
}

/*
** Routine for handling received data for clist drivers.
** NB: Called with the tty locked. The spl from the lockb( ) is passed.
** we return the ttySpl level that we re-locked at.
*/
static void
RIOReceive(p, PortP)
struct rio_info *	p;
struct Port *		PortP;
{
  struct tty_struct *TtyP;
  register ushort transCount;
  struct PKT *PacketP;
  register uint	DataCnt;
  uchar *	ptr;
  int copied =0;

  static int intCount, RxIntCnt;

  /*
  ** The receive data process is to remove packets from the
  ** PHB until there aren't any more or the current cblock
  ** is full. When this occurs, there will be some left over
  ** data in the packet, that we must do something with.
  ** As we haven't unhooked the packet from the read list
  ** yet, we can just leave the packet there, having first
  ** made a note of how far we got. This means that we need
  ** a pointer per port saying where we start taking the
  ** data from - this will normally be zero, but when we
  ** run out of space it will be set to the offset of the
  ** next byte to copy from the packet data area. The packet
  ** length field is decremented by the number of bytes that
  ** we succesfully removed from the packet. When this reaches
  ** zero, we reset the offset pointer to be zero, and free
  ** the packet from the front of the queue.
  */

  intCount++;

  TtyP = PortP->gs.tty;
  if (!TtyP) {
    rio_dprintk (RIO_DEBUG_INTR, "RIOReceive: tty is null. \n");
    return;
  }

  if (PortP->State & RIO_THROTTLE_RX) {
    rio_dprintk (RIO_DEBUG_INTR, "RIOReceive: Throttled. Can't handle more input.\n");
    return;
  }

  if ( PortP->State & RIO_DELETED )
    {
      while ( can_remove_receive( &PacketP, PortP ) )
	{
	  remove_receive( PortP );
	  put_free_end( PortP->HostP, PacketP );
	}
    }
  else
    {
      /*
      ** loop, just so long as:
      **   i ) there's some data ( i.e. can_remove_receive )
      **  ii ) we haven't been blocked
      ** iii ) there's somewhere to put the data
      **  iv ) we haven't outstayed our welcome
      */
      transCount = 1;
      while ( can_remove_receive(&PacketP, PortP)
	      && transCount)
	{
#ifdef STATS
	  PortP->Stat.RxIntCnt++;
#endif /* STATS */
	  RxIntCnt++;

	  /*
	  ** check that it is not a command!
	  */
	  if ( PacketP->len & PKT_CMD_BIT ) {
	    rio_dprintk (RIO_DEBUG_INTR, "RIO: unexpected command packet received on PHB\n");
	    /*	    rio_dprint(RIO_DEBUG_INTR, (" sysport   = %d\n", p->RIOPortp->PortNum)); */
	    rio_dprintk (RIO_DEBUG_INTR, " dest_unit = %d\n", PacketP->dest_unit);
	    rio_dprintk (RIO_DEBUG_INTR, " dest_port = %d\n", PacketP->dest_port);
	    rio_dprintk (RIO_DEBUG_INTR, " src_unit  = %d\n", PacketP->src_unit);
	    rio_dprintk (RIO_DEBUG_INTR, " src_port  = %d\n", PacketP->src_port);
	    rio_dprintk (RIO_DEBUG_INTR, " len	   = %d\n", PacketP->len);
	    rio_dprintk (RIO_DEBUG_INTR, " control   = %d\n", PacketP->control);
	    rio_dprintk (RIO_DEBUG_INTR, " csum	   = %d\n", PacketP->csum);
	    rio_dprintk (RIO_DEBUG_INTR, "	 data bytes: ");
	    for ( DataCnt=0; DataCnt<PKT_MAX_DATA_LEN; DataCnt++ )
	      rio_dprintk (RIO_DEBUG_INTR, "%d\n", PacketP->data[DataCnt]);
	    remove_receive( PortP );
	    put_free_end( PortP->HostP, PacketP );
	    continue; /* with next packet */
	  }

	  /*
	  ** How many characters can we move 'upstream' ?
	  **
	  ** Determine the minimum of the amount of data
	  ** available and the amount of space in which to
	  ** put it.
	  **
	  ** 1.	Get the packet length by masking 'len'
	  **	for only the length bits.
	  ** 2.	Available space is [buffer size] - [space used]
	  **
	  ** Transfer count is the minimum of packet length
	  ** and available space.
	  */
			
	  transCount = min_t(unsigned int, PacketP->len & PKT_LEN_MASK,
			   TTY_FLIPBUF_SIZE - TtyP->flip.count);
	  rio_dprintk (RIO_DEBUG_REC,  "port %d: Copy %d bytes\n", 
				      PortP->PortNum, transCount);
	  /*
	  ** To use the following 'kkprintfs' for debugging - change the '#undef'
	  ** to '#define', (this is the only place ___DEBUG_IT___ occurs in the
	  ** driver).
	  */
#undef ___DEBUG_IT___
#ifdef ___DEBUG_IT___
	  kkprintf("I:%d R:%d P:%d Q:%d C:%d F:%x ",
		   intCount,
		   RxIntCnt,
		   PortP->PortNum,
		   TtyP->rxqueue.count,
		   transCount,
		   TtyP->flags );
#endif
	  ptr = (uchar *) PacketP->data + PortP->RxDataStart;

	  rio_memcpy_fromio (TtyP->flip.char_buf_ptr, ptr, transCount);
	  memset(TtyP->flip.flag_buf_ptr, TTY_NORMAL, transCount);

#ifdef STATS
	  /*
	  ** keep a count for statistical purposes
	  */
	  PortP->Stat.RxCharCnt	+= transCount;
#endif
	  PortP->RxDataStart	+= transCount;
	  PacketP->len		-= transCount;
	  copied += transCount;
	  TtyP->flip.count += transCount;
	  TtyP->flip.char_buf_ptr += transCount;
	  TtyP->flip.flag_buf_ptr += transCount;


#ifdef ___DEBUG_IT___
	  kkprintf("T:%d L:%d\n", DataCnt, PacketP->len );
#endif

	  if ( PacketP->len == 0 )
	    {
				/*
				** If we have emptied the packet, then we can
				** free it, and reset the start pointer for
				** the next packet.
				*/
	      remove_receive( PortP );
	      put_free_end( PortP->HostP, PacketP );
	      PortP->RxDataStart = 0;
#ifdef STATS
				/*
				** more lies ( oops, I mean statistics )
				*/
	      PortP->Stat.RxPktCnt++;
#endif /* STATS */
	    }
	}
    }
  if (copied) {
    rio_dprintk (RIO_DEBUG_REC, "port %d: pushing tty flip buffer: %d total bytes copied.\n", PortP->PortNum, copied);
    tty_flip_buffer_push (TtyP);
  }

  return;
}

#ifdef FUTURE_RELEASE
/*
** The proc routine called by the line discipline to do the work for it.
** The proc routine works hand in hand with the interrupt routine.
*/
int
riotproc(p, tp, cmd, port)
struct rio_info *	p;
register struct ttystatics *tp;
int cmd;
int	port;
{
	register struct Port *PortP;
	int SysPort;
	struct PKT *PacketP;

	SysPort = port;	/* Believe me, it works. */

	if ( SysPort < 0 || SysPort >= RIO_PORTS ) {
		rio_dprintk (RIO_DEBUG_INTR, "Illegal port %d derived from TTY in riotproc()\n",SysPort);
		return 0;
	}
	PortP = p->RIOPortp[SysPort];

	if ((uint)PortP->PhbP < (uint)PortP->Caddr || 
			(uint)PortP->PhbP >= (uint)PortP->Caddr+SIXTY_FOUR_K ) {
		rio_dprintk (RIO_DEBUG_INTR, "RIO: NULL or BAD PhbP on sys port %d in proc routine\n",
							SysPort);
		rio_dprintk (RIO_DEBUG_INTR, "	 PortP = 0x%x\n",PortP);
		rio_dprintk (RIO_DEBUG_INTR, "	 PortP->PhbP = 0x%x\n",PortP->PhbP);
		rio_dprintk (RIO_DEBUG_INTR, "	 PortP->Caddr = 0x%x\n",PortP->PhbP);
		rio_dprintk (RIO_DEBUG_INTR, "	 PortP->HostPort = 0x%x\n",PortP->HostPort);
		return 0;
	}

	switch(cmd) {
		case T_WFLUSH:
			rio_dprintk (RIO_DEBUG_INTR, "T_WFLUSH\n");
			/*
			** Because of the spooky way the RIO works, we don't need
			** to issue a flush command on any of the SET*F commands,
			** as that causes trouble with getty and login, which issue
			** these commands to incur a READ flush, and rely on the fact
			** that the line discipline does a wait for drain for them.
			** As the rio doesn't wait for drain, the write flush would
			** destroy the Password: prompt. This isn't very friendly, so
			** here we only issue a WFLUSH command if we are in the interrupt
			** routine, or we aren't executing a SET*F command.
			*/
			if ( PortP->HostP->InIntr || !PortP->FlushCmdBodge ) {
				/*
				** form a wflush packet - 1 byte long, no data
				*/
				if ( PortP->State & RIO_DELETED ) {
					rio_dprintk (RIO_DEBUG_INTR, "WFLUSH on deleted RTA\n");
				}
				else {
					if ( RIOPreemptiveCmd(p, PortP, WFLUSH ) == RIO_FAIL ) {
						rio_dprintk (RIO_DEBUG_INTR, "T_WFLUSH Command failed\n");
					}
					else
						rio_dprintk (RIO_DEBUG_INTR, "T_WFLUSH Command\n");
				}
				/*
				** WFLUSH operation - flush the data!
				*/
				PortP->TxBufferIn = PortP->TxBufferOut = 0;
			}
			else {
				rio_dprintk (RIO_DEBUG_INTR, "T_WFLUSH Command ignored\n");
			}
			/*
			** sort out the line discipline
			*/
			if (PortP->CookMode == COOK_WELL)
				goto start;
			break;
	
		case T_RESUME:
			rio_dprintk (RIO_DEBUG_INTR, "T_RESUME\n");
			/*
			** send pre-emptive resume packet
			*/
			if ( PortP->State & RIO_DELETED ) {
				rio_dprintk (RIO_DEBUG_INTR, "RESUME on deleted RTA\n");
			}
			else {
				if ( RIOPreemptiveCmd(p, PortP, RESUME ) == RIO_FAIL ) {
					rio_dprintk (RIO_DEBUG_INTR, "T_RESUME Command failed\n");
				}
			}
			/*
			** and re-start the sender software!
			*/
			if (PortP->CookMode == COOK_WELL)
				goto start;
			break;
	
		case T_TIME:
			rio_dprintk (RIO_DEBUG_INTR, "T_TIME\n");
			/*
			** T_TIME is called when xDLY is set in oflags and
			** the line discipline timeout has expired. It's
			** function in life is to clear the TIMEOUT flag
			** and to re-start output to the port.
			*/
			/*
			** Fall through and re-start output
			*/
		case T_OUTPUT:
start:
			if ( PortP->MagicFlags & MAGIC_FLUSH ) {
				PortP->MagicFlags |= MORE_OUTPUT_EYGOR;
				return 0;
			}
			RIOTxEnable((char *)PortP);
			PortP->MagicFlags &= ~MORE_OUTPUT_EYGOR;
			/*rio_dprint(RIO_DEBUG_INTR, PortP,DBG_PROC,"T_OUTPUT finished\n");*/
			break;
	
		case T_SUSPEND:
			rio_dprintk (RIO_DEBUG_INTR, "T_SUSPEND\n");
			/*
			** send a suspend pre-emptive packet.
			*/
			if ( PortP->State & RIO_DELETED ) {
				rio_dprintk (RIO_DEBUG_INTR, "SUSPEND deleted RTA\n");
			}
			else {
				if ( RIOPreemptiveCmd(p, PortP, SUSPEND ) == RIO_FAIL ) {
					rio_dprintk (RIO_DEBUG_INTR, "T_SUSPEND Command failed\n");
				}
			}
			/*
			** done!
			*/
			break;
	
		case T_BLOCK:
			rio_dprintk (RIO_DEBUG_INTR, "T_BLOCK\n");
			break;
	
		case T_RFLUSH:
			rio_dprintk (RIO_DEBUG_INTR, "T_RFLUSH\n");
			if ( PortP->State & RIO_DELETED ) {
				rio_dprintk (RIO_DEBUG_INTR, "RFLUSH on deleted RTA\n");
				PortP->RxDataStart = 0;
			}
			else {
				if ( RIOPreemptiveCmd( p, PortP, RFLUSH ) == RIO_FAIL ) {
					rio_dprintk (RIO_DEBUG_INTR, "T_RFLUSH Command failed\n");
					return 0;
				}
				PortP->RxDataStart = 0;
				while ( can_remove_receive(&PacketP, PortP) ) {
					remove_receive(PortP);
					ShowPacket(DBG_PROC, PacketP );
					put_free_end(PortP->HostP, PacketP );
				}
				if ( PortP->PhbP->handshake == PHB_HANDSHAKE_SET ) {
					/*
					** MAGIC!
					*/
					rio_dprintk (RIO_DEBUG_INTR, "Set receive handshake bit\n");
					PortP->PhbP->handshake |= PHB_HANDSHAKE_RESET;
				}
			}
			break;
			/* FALLTHROUGH */
		case T_UNBLOCK:
			rio_dprintk (RIO_DEBUG_INTR, "T_UNBLOCK\n");
			/*
			** If there is any data to receive set a timeout to service it.
			*/
			RIOReceive(p, PortP);
			break;
	
		case T_BREAK:
			rio_dprintk (RIO_DEBUG_INTR, "T_BREAK\n");
			/*
			** Send a break command. For Sys V
			** this is a timed break, so we
			** send a SBREAK[time] packet
			*/
			/*
			** Build a BREAK command
			*/
			if ( PortP->State & RIO_DELETED ) {
				rio_dprintk (RIO_DEBUG_INTR, "BREAK on deleted RTA\n");
			}
			else {
				if (RIOShortCommand(PortP,SBREAK,2,
								p->RIOConf.BreakInterval)==RIO_FAIL) {
			   		rio_dprintk (RIO_DEBUG_INTR, "SBREAK RIOShortCommand failed\n");
				}
			}
	
			/*
			** done!
			*/
			break;
	
		case T_INPUT:
			rio_dprintk (RIO_DEBUG_INTR, "Proc T_INPUT called - I don't know what to do!\n");
			break;
		case T_PARM:
			rio_dprintk (RIO_DEBUG_INTR, "Proc T_PARM called - I don't know what to do!\n");
			break;
	
		case T_SWTCH:
			rio_dprintk (RIO_DEBUG_INTR, "Proc T_SWTCH called - I don't know what to do!\n");
			break;
	
		default:
			rio_dprintk (RIO_DEBUG_INTR, "Proc UNKNOWN command %d\n",cmd);
	}
	/*
	** T_OUTPUT returns without passing through this point!
	*/
	/*rio_dprint(RIO_DEBUG_INTR, PortP,DBG_PROC,"riotproc done\n");*/
	return(0);
}
#endif
