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
#include <linux/tty_flip.h>
#include <asm/io.h>
#include <asm/system.h>
#include <asm/string.h>
#include <asm/uaccess.h>

#include <linux/termios.h>
#include <linux/serial.h>

#include <linux/generic_serial.h>

#include <linux/delay.h>

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


static void RIOReceive(struct rio_info *, struct Port *);


static char *firstchars(char *p, int nch)
{
	static char buf[2][128];
	static int t = 0;
	t = !t;
	memcpy(buf[t], p, nch);
	buf[t][nch] = 0;
	return buf[t];
}


#define	INCR( P, I )	((P) = (((P)+(I)) & p->RIOBufferMask))
/* Enable and start the transmission of packets */
void RIOTxEnable(char *en)
{
	struct Port *PortP;
	struct rio_info *p;
	struct tty_struct *tty;
	int c;
	struct PKT __iomem *PacketP;
	unsigned long flags;

	PortP = (struct Port *) en;
	p = (struct rio_info *) PortP->p;
	tty = PortP->gs.tty;


	rio_dprintk(RIO_DEBUG_INTR, "tx port %d: %d chars queued.\n", PortP->PortNum, PortP->gs.xmit_cnt);

	if (!PortP->gs.xmit_cnt)
		return;


	/* This routine is an order of magnitude simpler than the specialix
	   version. One of the disadvantages is that this version will send
	   an incomplete packet (usually 64 bytes instead of 72) once for
	   every 4k worth of data. Let's just say that this won't influence
	   performance significantly..... */

	rio_spin_lock_irqsave(&PortP->portSem, flags);

	while (can_add_transmit(&PacketP, PortP)) {
		c = PortP->gs.xmit_cnt;
		if (c > PKT_MAX_DATA_LEN)
			c = PKT_MAX_DATA_LEN;

		/* Don't copy past the end of the source buffer */
		if (c > SERIAL_XMIT_SIZE - PortP->gs.xmit_tail)
			c = SERIAL_XMIT_SIZE - PortP->gs.xmit_tail;

		{
			int t;
			t = (c > 10) ? 10 : c;

			rio_dprintk(RIO_DEBUG_INTR, "rio: tx port %d: copying %d chars: %s - %s\n", PortP->PortNum, c, firstchars(PortP->gs.xmit_buf + PortP->gs.xmit_tail, t), firstchars(PortP->gs.xmit_buf + PortP->gs.xmit_tail + c - t, t));
		}
		/* If for one reason or another, we can't copy more data,
		   we're done! */
		if (c == 0)
			break;

		rio_memcpy_toio(PortP->HostP->Caddr, PacketP->data, PortP->gs.xmit_buf + PortP->gs.xmit_tail, c);
		/*    udelay (1); */

		writeb(c, &(PacketP->len));
		if (!(PortP->State & RIO_DELETED)) {
			add_transmit(PortP);
			/*
			 ** Count chars tx'd for port statistics reporting
			 */
			if (PortP->statsGather)
				PortP->txchars += c;
		}
		PortP->gs.xmit_tail = (PortP->gs.xmit_tail + c) & (SERIAL_XMIT_SIZE - 1);
		PortP->gs.xmit_cnt -= c;
	}

	rio_spin_unlock_irqrestore(&PortP->portSem, flags);

	if (PortP->gs.xmit_cnt <= (PortP->gs.wakeup_chars + 2 * PKT_MAX_DATA_LEN))
		tty_wakeup(PortP->gs.tty);

}


/*
** RIO Host Service routine. Does all the work traditionally associated with an
** interrupt.
*/
static int RupIntr;
static int RxIntr;
static int TxIntr;

void RIOServiceHost(struct rio_info *p, struct Host *HostP)
{
	rio_spin_lock(&HostP->HostLock);
	if ((HostP->Flags & RUN_STATE) != RC_RUNNING) {
		static int t = 0;
		rio_spin_unlock(&HostP->HostLock);
		if ((t++ % 200) == 0)
			rio_dprintk(RIO_DEBUG_INTR, "Interrupt but host not running. flags=%x.\n", (int) HostP->Flags);
		return;
	}
	rio_spin_unlock(&HostP->HostLock);

	if (readw(&HostP->ParmMapP->rup_intr)) {
		writew(0, &HostP->ParmMapP->rup_intr);
		p->RIORupCount++;
		RupIntr++;
		rio_dprintk(RIO_DEBUG_INTR, "rio: RUP interrupt on host %Zd\n", HostP - p->RIOHosts);
		RIOPollHostCommands(p, HostP);
	}

	if (readw(&HostP->ParmMapP->rx_intr)) {
		int port;

		writew(0, &HostP->ParmMapP->rx_intr);
		p->RIORxCount++;
		RxIntr++;

		rio_dprintk(RIO_DEBUG_INTR, "rio: RX interrupt on host %Zd\n", HostP - p->RIOHosts);
		/*
		 ** Loop through every port. If the port is mapped into
		 ** the system ( i.e. has /dev/ttyXXXX associated ) then it is
		 ** worth checking. If the port isn't open, grab any packets
		 ** hanging on its receive queue and stuff them on the free
		 ** list; check for commands on the way.
		 */
		for (port = p->RIOFirstPortsBooted; port < p->RIOLastPortsBooted + PORTS_PER_RTA; port++) {
			struct Port *PortP = p->RIOPortp[port];
			struct tty_struct *ttyP;
			struct PKT __iomem *PacketP;

			/*
			 ** not mapped in - most of the RIOPortp[] information
			 ** has not been set up!
			 ** Optimise: ports come in bundles of eight.
			 */
			if (!PortP->Mapped) {
				port += 7;
				continue;	/* with the next port */
			}

			/*
			 ** If the host board isn't THIS host board, check the next one.
			 ** optimise: ports come in bundles of eight.
			 */
			if (PortP->HostP != HostP) {
				port += 7;
				continue;
			}

			/*
			 ** Let us see - is the port open? If not, then don't service it.
			 */
			if (!(PortP->PortState & PORT_ISOPEN)) {
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
			if (can_remove_receive(&PacketP, PortP))
				RIOReceive(p, PortP);

			/*
			 ** If there is no data left to be read from the port, and
			 ** it's handshake bit is set, then we must clear the handshake,
			 ** so that that downstream RTA is re-enabled.
			 */
			if (!can_remove_receive(&PacketP, PortP) && (readw(&PortP->PhbP->handshake) == PHB_HANDSHAKE_SET)) {
				/*
				 ** MAGIC! ( Basically, handshake the RX buffer, so that
				 ** the RTAs upstream can be re-enabled. )
				 */
				rio_dprintk(RIO_DEBUG_INTR, "Set RX handshake bit\n");
				writew(PHB_HANDSHAKE_SET | PHB_HANDSHAKE_RESET, &PortP->PhbP->handshake);
			}
			rio_spin_unlock(&PortP->portSem);
		}
	}

	if (readw(&HostP->ParmMapP->tx_intr)) {
		int port;

		writew(0, &HostP->ParmMapP->tx_intr);

		p->RIOTxCount++;
		TxIntr++;
		rio_dprintk(RIO_DEBUG_INTR, "rio: TX interrupt on host %Zd\n", HostP - p->RIOHosts);

		/*
		 ** Loop through every port.
		 ** If the port is mapped into the system ( i.e. has /dev/ttyXXXX
		 ** associated ) then it is worth checking.
		 */
		for (port = p->RIOFirstPortsBooted; port < p->RIOLastPortsBooted + PORTS_PER_RTA; port++) {
			struct Port *PortP = p->RIOPortp[port];
			struct tty_struct *ttyP;
			struct PKT __iomem *PacketP;

			/*
			 ** not mapped in - most of the RIOPortp[] information
			 ** has not been set up!
			 */
			if (!PortP->Mapped) {
				port += 7;
				continue;	/* with the next port */
			}

			/*
			 ** If the host board isn't running, then its data structures
			 ** are no use to us - continue quietly.
			 */
			if (PortP->HostP != HostP) {
				port += 7;
				continue;	/* with the next port */
			}

			/*
			 ** Let us see - is the port open? If not, then don't service it.
			 */
			if (!(PortP->PortState & PORT_ISOPEN)) {
				continue;
			}

			rio_dprintk(RIO_DEBUG_INTR, "rio: Looking into port %d.\n", port);
			/*
			 ** Lock the port before we begin working on it.
			 */
			rio_spin_lock(&PortP->portSem);

			/*
			 ** If we can't add anything to the transmit queue, then
			 ** we need do none of this processing.
			 */
			if (!can_add_transmit(&PacketP, PortP)) {
				rio_dprintk(RIO_DEBUG_INTR, "Can't add to port, so skipping.\n");
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
				rio_dprintk(RIO_DEBUG_INTR, "no tty, so skipping.\n");
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
			if (PortP->MagicFlags) {
				if (PortP->MagicFlags & MAGIC_REBOOT) {
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
					if (RIOParam(PortP, RIOC_OPEN, ((PortP->Cor2Copy & (RIOC_COR2_RTSFLOW | RIOC_COR2_CTSFLOW)) == (RIOC_COR2_RTSFLOW | RIOC_COR2_CTSFLOW)) ? 1 : 0, DONT_SLEEP) == RIO_FAIL)
						continue;	/* with next port */
					rio_spin_lock(&PortP->portSem);
					PortP->MagicFlags &= ~MAGIC_REBOOT;
				}

				/*
				 ** As mentioned above, this is a tacky hack to cope
				 ** with WFLUSH
				 */
				if (PortP->WflushFlag) {
					rio_dprintk(RIO_DEBUG_INTR, "Want to WFLUSH mark this port\n");

					if (PortP->InUse)
						rio_dprintk(RIO_DEBUG_INTR, "FAILS - PORT IS IN USE\n");
				}

				while (PortP->WflushFlag && can_add_transmit(&PacketP, PortP) && (PortP->InUse == NOT_INUSE)) {
					int p;
					struct PktCmd __iomem *PktCmdP;

					rio_dprintk(RIO_DEBUG_INTR, "Add WFLUSH marker to data queue\n");
					/*
					 ** make it look just like a WFLUSH command
					 */
					PktCmdP = (struct PktCmd __iomem *) &PacketP->data[0];

					writeb(RIOC_WFLUSH, &PktCmdP->Command);

					p = PortP->HostPort % (u16) PORTS_PER_RTA;

					/*
					 ** If second block of ports for 16 port RTA, add 8
					 ** to index 8-15.
					 */
					if (PortP->SecondBlock)
						p += PORTS_PER_RTA;

					writeb(p, &PktCmdP->PhbNum);

					/*
					 ** to make debuggery easier
					 */
					writeb('W', &PacketP->data[2]);
					writeb('F', &PacketP->data[3]);
					writeb('L', &PacketP->data[4]);
					writeb('U', &PacketP->data[5]);
					writeb('S', &PacketP->data[6]);
					writeb('H', &PacketP->data[7]);
					writeb(' ', &PacketP->data[8]);
					writeb('0' + PortP->WflushFlag, &PacketP->data[9]);
					writeb(' ', &PacketP->data[10]);
					writeb(' ', &PacketP->data[11]);
					writeb('\0', &PacketP->data[12]);

					/*
					 ** its two bytes long!
					 */
					writeb(PKT_CMD_BIT | 2, &PacketP->len);

					/*
					 ** queue it!
					 */
					if (!(PortP->State & RIO_DELETED)) {
						add_transmit(PortP);
						/*
						 ** Count chars tx'd for port statistics reporting
						 */
						if (PortP->statsGather)
							PortP->txchars += 2;
					}

					if (--(PortP->WflushFlag) == 0) {
						PortP->MagicFlags &= ~MAGIC_FLUSH;
					}

					rio_dprintk(RIO_DEBUG_INTR, "Wflush count now stands at %d\n", PortP->WflushFlag);
				}
				if (PortP->MagicFlags & MORE_OUTPUT_EYGOR) {
					if (PortP->MagicFlags & MAGIC_FLUSH) {
						PortP->MagicFlags |= MORE_OUTPUT_EYGOR;
					} else {
						if (!can_add_transmit(&PacketP, PortP)) {
							rio_spin_unlock(&PortP->portSem);
							continue;
						}
						rio_spin_unlock(&PortP->portSem);
						RIOTxEnable((char *) PortP);
						rio_spin_lock(&PortP->portSem);
						PortP->MagicFlags &= ~MORE_OUTPUT_EYGOR;
					}
				}
			}


			/*
			 ** If we can't add anything to the transmit queue, then
			 ** we need do none of the remaining processing.
			 */
			if (!can_add_transmit(&PacketP, PortP)) {
				rio_spin_unlock(&PortP->portSem);
				continue;
			}

			rio_spin_unlock(&PortP->portSem);
			RIOTxEnable((char *) PortP);
		}
	}
}

/*
** Routine for handling received data for tty drivers
*/
static void RIOReceive(struct rio_info *p, struct Port *PortP)
{
	struct tty_struct *TtyP;
	unsigned short transCount;
	struct PKT __iomem *PacketP;
	register unsigned int DataCnt;
	unsigned char __iomem *ptr;
	unsigned char *buf;
	int copied = 0;

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
	 ** we successfully removed from the packet. When this reaches
	 ** zero, we reset the offset pointer to be zero, and free
	 ** the packet from the front of the queue.
	 */

	intCount++;

	TtyP = PortP->gs.tty;
	if (!TtyP) {
		rio_dprintk(RIO_DEBUG_INTR, "RIOReceive: tty is null. \n");
		return;
	}

	if (PortP->State & RIO_THROTTLE_RX) {
		rio_dprintk(RIO_DEBUG_INTR, "RIOReceive: Throttled. Can't handle more input.\n");
		return;
	}

	if (PortP->State & RIO_DELETED) {
		while (can_remove_receive(&PacketP, PortP)) {
			remove_receive(PortP);
			put_free_end(PortP->HostP, PacketP);
		}
	} else {
		/*
		 ** loop, just so long as:
		 **   i ) there's some data ( i.e. can_remove_receive )
		 **  ii ) we haven't been blocked
		 ** iii ) there's somewhere to put the data
		 **  iv ) we haven't outstayed our welcome
		 */
		transCount = 1;
		while (can_remove_receive(&PacketP, PortP)
		       && transCount) {
			RxIntCnt++;

			/*
			 ** check that it is not a command!
			 */
			if (readb(&PacketP->len) & PKT_CMD_BIT) {
				rio_dprintk(RIO_DEBUG_INTR, "RIO: unexpected command packet received on PHB\n");
				/*      rio_dprint(RIO_DEBUG_INTR, (" sysport   = %d\n", p->RIOPortp->PortNum)); */
				rio_dprintk(RIO_DEBUG_INTR, " dest_unit = %d\n", readb(&PacketP->dest_unit));
				rio_dprintk(RIO_DEBUG_INTR, " dest_port = %d\n", readb(&PacketP->dest_port));
				rio_dprintk(RIO_DEBUG_INTR, " src_unit  = %d\n", readb(&PacketP->src_unit));
				rio_dprintk(RIO_DEBUG_INTR, " src_port  = %d\n", readb(&PacketP->src_port));
				rio_dprintk(RIO_DEBUG_INTR, " len	   = %d\n", readb(&PacketP->len));
				rio_dprintk(RIO_DEBUG_INTR, " control   = %d\n", readb(&PacketP->control));
				rio_dprintk(RIO_DEBUG_INTR, " csum	   = %d\n", readw(&PacketP->csum));
				rio_dprintk(RIO_DEBUG_INTR, "	 data bytes: ");
				for (DataCnt = 0; DataCnt < PKT_MAX_DATA_LEN; DataCnt++)
					rio_dprintk(RIO_DEBUG_INTR, "%d\n", readb(&PacketP->data[DataCnt]));
				remove_receive(PortP);
				put_free_end(PortP->HostP, PacketP);
				continue;	/* with next packet */
			}

			/*
			 ** How many characters can we move 'upstream' ?
			 **
			 ** Determine the minimum of the amount of data
			 ** available and the amount of space in which to
			 ** put it.
			 **
			 ** 1.        Get the packet length by masking 'len'
			 **   for only the length bits.
			 ** 2.        Available space is [buffer size] - [space used]
			 **
			 ** Transfer count is the minimum of packet length
			 ** and available space.
			 */

			transCount = tty_buffer_request_room(TtyP, readb(&PacketP->len) & PKT_LEN_MASK);
			rio_dprintk(RIO_DEBUG_REC, "port %d: Copy %d bytes\n", PortP->PortNum, transCount);
			/*
			 ** To use the following 'kkprintfs' for debugging - change the '#undef'
			 ** to '#define', (this is the only place ___DEBUG_IT___ occurs in the
			 ** driver).
			 */
			ptr = (unsigned char __iomem *) PacketP->data + PortP->RxDataStart;

			tty_prepare_flip_string(TtyP, &buf, transCount);
			rio_memcpy_fromio(buf, ptr, transCount);
			PortP->RxDataStart += transCount;
			writeb(readb(&PacketP->len)-transCount, &PacketP->len);
			copied += transCount;



			if (readb(&PacketP->len) == 0) {
				/*
				 ** If we have emptied the packet, then we can
				 ** free it, and reset the start pointer for
				 ** the next packet.
				 */
				remove_receive(PortP);
				put_free_end(PortP->HostP, PacketP);
				PortP->RxDataStart = 0;
			}
		}
	}
	if (copied) {
		rio_dprintk(RIO_DEBUG_REC, "port %d: pushing tty flip buffer: %d total bytes copied.\n", PortP->PortNum, copied);
		tty_flip_buffer_push(TtyP);
	}

	return;
}

