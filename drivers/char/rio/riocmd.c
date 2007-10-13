/*
** -----------------------------------------------------------------------------
**
**  Perle Specialix driver for Linux
**  ported from the existing SCO driver source
**
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
**	Module		: riocmd.c
**	SID		: 1.2
**	Last Modified	: 11/6/98 10:33:41
**	Retrieved	: 11/6/98 10:33:49
**
**  ident @(#)riocmd.c	1.2
**
** -----------------------------------------------------------------------------
*/
#ifdef SCCS_LABELS
static char *_riocmd_c_sccs_ = "@(#)riocmd.c	1.2";
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


static struct IdentifyRta IdRta;
static struct KillNeighbour KillUnit;

int RIOFoadRta(struct Host *HostP, struct Map *MapP)
{
	struct CmdBlk *CmdBlkP;

	rio_dprintk(RIO_DEBUG_CMD, "FOAD RTA\n");

	CmdBlkP = RIOGetCmdBlk();

	if (!CmdBlkP) {
		rio_dprintk(RIO_DEBUG_CMD, "FOAD RTA: GetCmdBlk failed\n");
		return -ENXIO;
	}

	CmdBlkP->Packet.dest_unit = MapP->ID;
	CmdBlkP->Packet.dest_port = BOOT_RUP;
	CmdBlkP->Packet.src_unit = 0;
	CmdBlkP->Packet.src_port = BOOT_RUP;
	CmdBlkP->Packet.len = 0x84;
	CmdBlkP->Packet.data[0] = IFOAD;
	CmdBlkP->Packet.data[1] = 0;
	CmdBlkP->Packet.data[2] = IFOAD_MAGIC & 0xFF;
	CmdBlkP->Packet.data[3] = (IFOAD_MAGIC >> 8) & 0xFF;

	if (RIOQueueCmdBlk(HostP, MapP->ID - 1, CmdBlkP) == RIO_FAIL) {
		rio_dprintk(RIO_DEBUG_CMD, "FOAD RTA: Failed to queue foad command\n");
		return -EIO;
	}
	return 0;
}

int RIOZombieRta(struct Host *HostP, struct Map *MapP)
{
	struct CmdBlk *CmdBlkP;

	rio_dprintk(RIO_DEBUG_CMD, "ZOMBIE RTA\n");

	CmdBlkP = RIOGetCmdBlk();

	if (!CmdBlkP) {
		rio_dprintk(RIO_DEBUG_CMD, "ZOMBIE RTA: GetCmdBlk failed\n");
		return -ENXIO;
	}

	CmdBlkP->Packet.dest_unit = MapP->ID;
	CmdBlkP->Packet.dest_port = BOOT_RUP;
	CmdBlkP->Packet.src_unit = 0;
	CmdBlkP->Packet.src_port = BOOT_RUP;
	CmdBlkP->Packet.len = 0x84;
	CmdBlkP->Packet.data[0] = ZOMBIE;
	CmdBlkP->Packet.data[1] = 0;
	CmdBlkP->Packet.data[2] = ZOMBIE_MAGIC & 0xFF;
	CmdBlkP->Packet.data[3] = (ZOMBIE_MAGIC >> 8) & 0xFF;

	if (RIOQueueCmdBlk(HostP, MapP->ID - 1, CmdBlkP) == RIO_FAIL) {
		rio_dprintk(RIO_DEBUG_CMD, "ZOMBIE RTA: Failed to queue zombie command\n");
		return -EIO;
	}
	return 0;
}

int RIOCommandRta(struct rio_info *p, unsigned long RtaUnique, int (*func) (struct Host * HostP, struct Map * MapP))
{
	unsigned int Host;

	rio_dprintk(RIO_DEBUG_CMD, "Command RTA 0x%lx func %p\n", RtaUnique, func);

	if (!RtaUnique)
		return (0);

	for (Host = 0; Host < p->RIONumHosts; Host++) {
		unsigned int Rta;
		struct Host *HostP = &p->RIOHosts[Host];

		for (Rta = 0; Rta < RTAS_PER_HOST; Rta++) {
			struct Map *MapP = &HostP->Mapping[Rta];

			if (MapP->RtaUniqueNum == RtaUnique) {
				uint Link;

				/*
				 ** now, lets just check we have a route to it...
				 ** IF the routing stuff is working, then one of the
				 ** topology entries for this unit will have a legit
				 ** route *somewhere*. We care not where - if its got
				 ** any connections, we can get to it.
				 */
				for (Link = 0; Link < LINKS_PER_UNIT; Link++) {
					if (MapP->Topology[Link].Unit <= (u8) MAX_RUP) {
						/*
						 ** Its worth trying the operation...
						 */
						return (*func) (HostP, MapP);
					}
				}
			}
		}
	}
	return -ENXIO;
}


int RIOIdentifyRta(struct rio_info *p, void __user * arg)
{
	unsigned int Host;

	if (copy_from_user(&IdRta, arg, sizeof(IdRta))) {
		rio_dprintk(RIO_DEBUG_CMD, "RIO_IDENTIFY_RTA copy failed\n");
		p->RIOError.Error = COPYIN_FAILED;
		return -EFAULT;
	}

	for (Host = 0; Host < p->RIONumHosts; Host++) {
		unsigned int Rta;
		struct Host *HostP = &p->RIOHosts[Host];

		for (Rta = 0; Rta < RTAS_PER_HOST; Rta++) {
			struct Map *MapP = &HostP->Mapping[Rta];

			if (MapP->RtaUniqueNum == IdRta.RtaUnique) {
				uint Link;
				/*
				 ** now, lets just check we have a route to it...
				 ** IF the routing stuff is working, then one of the
				 ** topology entries for this unit will have a legit
				 ** route *somewhere*. We care not where - if its got
				 ** any connections, we can get to it.
				 */
				for (Link = 0; Link < LINKS_PER_UNIT; Link++) {
					if (MapP->Topology[Link].Unit <= (u8) MAX_RUP) {
						/*
						 ** Its worth trying the operation...
						 */
						struct CmdBlk *CmdBlkP;

						rio_dprintk(RIO_DEBUG_CMD, "IDENTIFY RTA\n");

						CmdBlkP = RIOGetCmdBlk();

						if (!CmdBlkP) {
							rio_dprintk(RIO_DEBUG_CMD, "IDENTIFY RTA: GetCmdBlk failed\n");
							return -ENXIO;
						}

						CmdBlkP->Packet.dest_unit = MapP->ID;
						CmdBlkP->Packet.dest_port = BOOT_RUP;
						CmdBlkP->Packet.src_unit = 0;
						CmdBlkP->Packet.src_port = BOOT_RUP;
						CmdBlkP->Packet.len = 0x84;
						CmdBlkP->Packet.data[0] = IDENTIFY;
						CmdBlkP->Packet.data[1] = 0;
						CmdBlkP->Packet.data[2] = IdRta.ID;

						if (RIOQueueCmdBlk(HostP, MapP->ID - 1, CmdBlkP) == RIO_FAIL) {
							rio_dprintk(RIO_DEBUG_CMD, "IDENTIFY RTA: Failed to queue command\n");
							return -EIO;
						}
						return 0;
					}
				}
			}
		}
	}
	return -ENOENT;
}


int RIOKillNeighbour(struct rio_info *p, void __user * arg)
{
	uint Host;
	uint ID;
	struct Host *HostP;
	struct CmdBlk *CmdBlkP;

	rio_dprintk(RIO_DEBUG_CMD, "KILL HOST NEIGHBOUR\n");

	if (copy_from_user(&KillUnit, arg, sizeof(KillUnit))) {
		rio_dprintk(RIO_DEBUG_CMD, "RIO_KILL_NEIGHBOUR copy failed\n");
		p->RIOError.Error = COPYIN_FAILED;
		return -EFAULT;
	}

	if (KillUnit.Link > 3)
		return -ENXIO;

	CmdBlkP = RIOGetCmdBlk();

	if (!CmdBlkP) {
		rio_dprintk(RIO_DEBUG_CMD, "UFOAD: GetCmdBlk failed\n");
		return -ENXIO;
	}

	CmdBlkP->Packet.dest_unit = 0;
	CmdBlkP->Packet.src_unit = 0;
	CmdBlkP->Packet.dest_port = BOOT_RUP;
	CmdBlkP->Packet.src_port = BOOT_RUP;
	CmdBlkP->Packet.len = 0x84;
	CmdBlkP->Packet.data[0] = UFOAD;
	CmdBlkP->Packet.data[1] = KillUnit.Link;
	CmdBlkP->Packet.data[2] = UFOAD_MAGIC & 0xFF;
	CmdBlkP->Packet.data[3] = (UFOAD_MAGIC >> 8) & 0xFF;

	for (Host = 0; Host < p->RIONumHosts; Host++) {
		ID = 0;
		HostP = &p->RIOHosts[Host];

		if (HostP->UniqueNum == KillUnit.UniqueNum) {
			if (RIOQueueCmdBlk(HostP, RTAS_PER_HOST + KillUnit.Link, CmdBlkP) == RIO_FAIL) {
				rio_dprintk(RIO_DEBUG_CMD, "UFOAD: Failed queue command\n");
				return -EIO;
			}
			return 0;
		}

		for (ID = 0; ID < RTAS_PER_HOST; ID++) {
			if (HostP->Mapping[ID].RtaUniqueNum == KillUnit.UniqueNum) {
				CmdBlkP->Packet.dest_unit = ID + 1;
				if (RIOQueueCmdBlk(HostP, ID, CmdBlkP) == RIO_FAIL) {
					rio_dprintk(RIO_DEBUG_CMD, "UFOAD: Failed queue command\n");
					return -EIO;
				}
				return 0;
			}
		}
	}
	RIOFreeCmdBlk(CmdBlkP);
	return -ENXIO;
}

int RIOSuspendBootRta(struct Host *HostP, int ID, int Link)
{
	struct CmdBlk *CmdBlkP;

	rio_dprintk(RIO_DEBUG_CMD, "SUSPEND BOOT ON RTA ID %d, link %c\n", ID, 'A' + Link);

	CmdBlkP = RIOGetCmdBlk();

	if (!CmdBlkP) {
		rio_dprintk(RIO_DEBUG_CMD, "SUSPEND BOOT ON RTA: GetCmdBlk failed\n");
		return -ENXIO;
	}

	CmdBlkP->Packet.dest_unit = ID;
	CmdBlkP->Packet.dest_port = BOOT_RUP;
	CmdBlkP->Packet.src_unit = 0;
	CmdBlkP->Packet.src_port = BOOT_RUP;
	CmdBlkP->Packet.len = 0x84;
	CmdBlkP->Packet.data[0] = IWAIT;
	CmdBlkP->Packet.data[1] = Link;
	CmdBlkP->Packet.data[2] = IWAIT_MAGIC & 0xFF;
	CmdBlkP->Packet.data[3] = (IWAIT_MAGIC >> 8) & 0xFF;

	if (RIOQueueCmdBlk(HostP, ID - 1, CmdBlkP) == RIO_FAIL) {
		rio_dprintk(RIO_DEBUG_CMD, "SUSPEND BOOT ON RTA: Failed to queue iwait command\n");
		return -EIO;
	}
	return 0;
}

int RIOFoadWakeup(struct rio_info *p)
{
	int port;
	struct Port *PortP;
	unsigned long flags;

	for (port = 0; port < RIO_PORTS; port++) {
		PortP = p->RIOPortp[port];

		rio_spin_lock_irqsave(&PortP->portSem, flags);
		PortP->Config = 0;
		PortP->State = 0;
		PortP->InUse = NOT_INUSE;
		PortP->PortState = 0;
		PortP->FlushCmdBodge = 0;
		PortP->ModemLines = 0;
		PortP->ModemState = 0;
		PortP->CookMode = 0;
		PortP->ParamSem = 0;
		PortP->Mapped = 0;
		PortP->WflushFlag = 0;
		PortP->MagicFlags = 0;
		PortP->RxDataStart = 0;
		PortP->TxBufferIn = 0;
		PortP->TxBufferOut = 0;
		rio_spin_unlock_irqrestore(&PortP->portSem, flags);
	}
	return (0);
}

/*
** Incoming command on the COMMAND_RUP to be processed.
*/
static int RIOCommandRup(struct rio_info *p, uint Rup, struct Host *HostP, struct PKT __iomem *PacketP)
{
	struct PktCmd __iomem *PktCmdP = (struct PktCmd __iomem *)PacketP->data;
	struct Port *PortP;
	struct UnixRup *UnixRupP;
	unsigned short SysPort;
	unsigned short ReportedModemStatus;
	unsigned short rup;
	unsigned short subCommand;
	unsigned long flags;

	func_enter();

	/*
	 ** 16 port RTA note:
	 ** Command rup packets coming from the RTA will have pkt->data[1] (which
	 ** translates to PktCmdP->PhbNum) set to the host port number for the
	 ** particular unit. To access the correct BaseSysPort for a 16 port RTA,
	 ** we can use PhbNum to get the rup number for the appropriate 8 port
	 ** block (for the first block, this should be equal to 'Rup').
	 */
	rup = readb(&PktCmdP->PhbNum) / (unsigned short) PORTS_PER_RTA;
	UnixRupP = &HostP->UnixRups[rup];
	SysPort = UnixRupP->BaseSysPort + (readb(&PktCmdP->PhbNum) % (unsigned short) PORTS_PER_RTA);
	rio_dprintk(RIO_DEBUG_CMD, "Command on rup %d, port %d\n", rup, SysPort);

	if (UnixRupP->BaseSysPort == NO_PORT) {
		rio_dprintk(RIO_DEBUG_CMD, "OBSCURE ERROR!\n");
		rio_dprintk(RIO_DEBUG_CMD, "Diagnostics follow. Please WRITE THESE DOWN and report them to Specialix Technical Support\n");
		rio_dprintk(RIO_DEBUG_CMD, "CONTROL information: Host number %Zd, name ``%s''\n", HostP - p->RIOHosts, HostP->Name);
		rio_dprintk(RIO_DEBUG_CMD, "CONTROL information: Rup number  0x%x\n", rup);

		if (Rup < (unsigned short) MAX_RUP) {
			rio_dprintk(RIO_DEBUG_CMD, "CONTROL information: This is the RUP for RTA ``%s''\n", HostP->Mapping[Rup].Name);
		} else
			rio_dprintk(RIO_DEBUG_CMD, "CONTROL information: This is the RUP for link ``%c'' of host ``%s''\n", ('A' + Rup - MAX_RUP), HostP->Name);

		rio_dprintk(RIO_DEBUG_CMD, "PACKET information: Destination 0x%x:0x%x\n", readb(&PacketP->dest_unit), readb(&PacketP->dest_port));
		rio_dprintk(RIO_DEBUG_CMD, "PACKET information: Source	  0x%x:0x%x\n", readb(&PacketP->src_unit), readb(&PacketP->src_port));
		rio_dprintk(RIO_DEBUG_CMD, "PACKET information: Length	  0x%x (%d)\n", readb(&PacketP->len), readb(&PacketP->len));
		rio_dprintk(RIO_DEBUG_CMD, "PACKET information: Control	 0x%x (%d)\n", readb(&PacketP->control), readb(&PacketP->control));
		rio_dprintk(RIO_DEBUG_CMD, "PACKET information: Check	   0x%x (%d)\n", readw(&PacketP->csum), readw(&PacketP->csum));
		rio_dprintk(RIO_DEBUG_CMD, "COMMAND information: Host Port Number 0x%x, " "Command Code 0x%x\n", readb(&PktCmdP->PhbNum), readb(&PktCmdP->Command));
		return 1;
	}
	PortP = p->RIOPortp[SysPort];
	rio_spin_lock_irqsave(&PortP->portSem, flags);
	switch (readb(&PktCmdP->Command)) {
	case BREAK_RECEIVED:
		rio_dprintk(RIO_DEBUG_CMD, "Received a break!\n");
		/* If the current line disc. is not multi-threading and
		   the current processor is not the default, reset rup_intr
		   and return 0 to ensure that the command packet is
		   not freed. */
		/* Call tmgr HANGUP HERE */
		/* Fix this later when every thing works !!!! RAMRAJ */
		gs_got_break(&PortP->gs);
		break;

	case COMPLETE:
		rio_dprintk(RIO_DEBUG_CMD, "Command complete on phb %d host %Zd\n", readb(&PktCmdP->PhbNum), HostP - p->RIOHosts);
		subCommand = 1;
		switch (readb(&PktCmdP->SubCommand)) {
		case MEMDUMP:
			rio_dprintk(RIO_DEBUG_CMD, "Memory dump cmd (0x%x) from addr 0x%x\n", readb(&PktCmdP->SubCommand), readw(&PktCmdP->SubAddr));
			break;
		case READ_REGISTER:
			rio_dprintk(RIO_DEBUG_CMD, "Read register (0x%x)\n", readw(&PktCmdP->SubAddr));
			p->CdRegister = (readb(&PktCmdP->ModemStatus) & MSVR1_HOST);
			break;
		default:
			subCommand = 0;
			break;
		}
		if (subCommand)
			break;
		rio_dprintk(RIO_DEBUG_CMD, "New status is 0x%x was 0x%x\n", readb(&PktCmdP->PortStatus), PortP->PortState);
		if (PortP->PortState != readb(&PktCmdP->PortStatus)) {
			rio_dprintk(RIO_DEBUG_CMD, "Mark status & wakeup\n");
			PortP->PortState = readb(&PktCmdP->PortStatus);
			/* What should we do here ...
			   wakeup( &PortP->PortState );
			 */
		} else
			rio_dprintk(RIO_DEBUG_CMD, "No change\n");

		/* FALLTHROUGH */
	case MODEM_STATUS:
		/*
		 ** Knock out the tbusy and tstop bits, as these are not relevant
		 ** to the check for modem status change (they're just there because
		 ** it's a convenient place to put them!).
		 */
		ReportedModemStatus = readb(&PktCmdP->ModemStatus);
		if ((PortP->ModemState & MSVR1_HOST) == (ReportedModemStatus & MSVR1_HOST)) {
			rio_dprintk(RIO_DEBUG_CMD, "Modem status unchanged 0x%x\n", PortP->ModemState);
			/*
			 ** Update ModemState just in case tbusy or tstop states have
			 ** changed.
			 */
			PortP->ModemState = ReportedModemStatus;
		} else {
			rio_dprintk(RIO_DEBUG_CMD, "Modem status change from 0x%x to 0x%x\n", PortP->ModemState, ReportedModemStatus);
			PortP->ModemState = ReportedModemStatus;
#ifdef MODEM_SUPPORT
			if (PortP->Mapped) {
				/***********************************************************\
				*************************************************************
				***													   ***
				***		  M O D E M   S T A T E   C H A N G E		  ***
				***													   ***
				*************************************************************
				\***********************************************************/
				/*
				 ** If the device is a modem, then check the modem
				 ** carrier.
				 */
				if (PortP->gs.tty == NULL)
					break;
				if (PortP->gs.tty->termios == NULL)
					break;

				if (!(PortP->gs.tty->termios->c_cflag & CLOCAL) && ((PortP->State & (RIO_MOPEN | RIO_WOPEN)))) {

					rio_dprintk(RIO_DEBUG_CMD, "Is there a Carrier?\n");
					/*
					 ** Is there a carrier?
					 */
					if (PortP->ModemState & MSVR1_CD) {
						/*
						 ** Has carrier just appeared?
						 */
						if (!(PortP->State & RIO_CARR_ON)) {
							rio_dprintk(RIO_DEBUG_CMD, "Carrier just came up.\n");
							PortP->State |= RIO_CARR_ON;
							/*
							 ** wakeup anyone in WOPEN
							 */
							if (PortP->State & (PORT_ISOPEN | RIO_WOPEN))
								wake_up_interruptible(&PortP->gs.open_wait);
						}
					} else {
						/*
						 ** Has carrier just dropped?
						 */
						if (PortP->State & RIO_CARR_ON) {
							if (PortP->State & (PORT_ISOPEN | RIO_WOPEN | RIO_MOPEN))
								tty_hangup(PortP->gs.tty);
							PortP->State &= ~RIO_CARR_ON;
							rio_dprintk(RIO_DEBUG_CMD, "Carrirer just went down\n");
						}
					}
				}
			}
#endif
		}
		break;

	default:
		rio_dprintk(RIO_DEBUG_CMD, "Unknown command %d on CMD_RUP of host %Zd\n", readb(&PktCmdP->Command), HostP - p->RIOHosts);
		break;
	}
	rio_spin_unlock_irqrestore(&PortP->portSem, flags);

	func_exit();

	return 1;
}

/*
** The command mechanism:
**	Each rup has a chain of commands associated with it.
**	This chain is maintained by routines in this file.
**	Periodically we are called and we run a quick check of all the
**	active chains to determine if there is a command to be executed,
**	and if the rup is ready to accept it.
**
*/

/*
** Allocate an empty command block.
*/
struct CmdBlk *RIOGetCmdBlk(void)
{
	struct CmdBlk *CmdBlkP;

	CmdBlkP = kzalloc(sizeof(struct CmdBlk), GFP_ATOMIC);
	return CmdBlkP;
}

/*
** Return a block to the head of the free list.
*/
void RIOFreeCmdBlk(struct CmdBlk *CmdBlkP)
{
	kfree(CmdBlkP);
}

/*
** attach a command block to the list of commands to be performed for
** a given rup.
*/
int RIOQueueCmdBlk(struct Host *HostP, uint Rup, struct CmdBlk *CmdBlkP)
{
	struct CmdBlk **Base;
	struct UnixRup *UnixRupP;
	unsigned long flags;

	if (Rup >= (unsigned short) (MAX_RUP + LINKS_PER_UNIT)) {
		rio_dprintk(RIO_DEBUG_CMD, "Illegal rup number %d in RIOQueueCmdBlk\n", Rup);
		RIOFreeCmdBlk(CmdBlkP);
		return RIO_FAIL;
	}

	UnixRupP = &HostP->UnixRups[Rup];

	rio_spin_lock_irqsave(&UnixRupP->RupLock, flags);

	/*
	 ** If the RUP is currently inactive, then put the request
	 ** straight on the RUP....
	 */
	if ((UnixRupP->CmdsWaitingP == NULL) && (UnixRupP->CmdPendingP == NULL) && (readw(&UnixRupP->RupP->txcontrol) == TX_RUP_INACTIVE) && (CmdBlkP->PreFuncP ? (*CmdBlkP->PreFuncP) (CmdBlkP->PreArg, CmdBlkP)
																	     : 1)) {
		rio_dprintk(RIO_DEBUG_CMD, "RUP inactive-placing command straight on. Cmd byte is 0x%x\n", CmdBlkP->Packet.data[0]);

		/*
		 ** Whammy! blat that pack!
		 */
		HostP->Copy(&CmdBlkP->Packet, RIO_PTR(HostP->Caddr, readw(&UnixRupP->RupP->txpkt)), sizeof(struct PKT));

		/*
		 ** place command packet on the pending position.
		 */
		UnixRupP->CmdPendingP = CmdBlkP;

		/*
		 ** set the command register
		 */
		writew(TX_PACKET_READY, &UnixRupP->RupP->txcontrol);

		rio_spin_unlock_irqrestore(&UnixRupP->RupLock, flags);

		return 0;
	}
	rio_dprintk(RIO_DEBUG_CMD, "RUP active - en-queing\n");

	if (UnixRupP->CmdsWaitingP != NULL)
		rio_dprintk(RIO_DEBUG_CMD, "Rup active - command waiting\n");
	if (UnixRupP->CmdPendingP != NULL)
		rio_dprintk(RIO_DEBUG_CMD, "Rup active - command pending\n");
	if (readw(&UnixRupP->RupP->txcontrol) != TX_RUP_INACTIVE)
		rio_dprintk(RIO_DEBUG_CMD, "Rup active - command rup not ready\n");

	Base = &UnixRupP->CmdsWaitingP;

	rio_dprintk(RIO_DEBUG_CMD, "First try to queue cmdblk %p at %p\n", CmdBlkP, Base);

	while (*Base) {
		rio_dprintk(RIO_DEBUG_CMD, "Command cmdblk %p here\n", *Base);
		Base = &((*Base)->NextP);
		rio_dprintk(RIO_DEBUG_CMD, "Now try to queue cmd cmdblk %p at %p\n", CmdBlkP, Base);
	}

	rio_dprintk(RIO_DEBUG_CMD, "Will queue cmdblk %p at %p\n", CmdBlkP, Base);

	*Base = CmdBlkP;

	CmdBlkP->NextP = NULL;

	rio_spin_unlock_irqrestore(&UnixRupP->RupLock, flags);

	return 0;
}

/*
** Here we go - if there is an empty rup, fill it!
** must be called at splrio() or higher.
*/
void RIOPollHostCommands(struct rio_info *p, struct Host *HostP)
{
	struct CmdBlk *CmdBlkP;
	struct UnixRup *UnixRupP;
	struct PKT __iomem *PacketP;
	unsigned short Rup;
	unsigned long flags;


	Rup = MAX_RUP + LINKS_PER_UNIT;

	do {			/* do this loop for each RUP */
		/*
		 ** locate the rup we are processing & lock it
		 */
		UnixRupP = &HostP->UnixRups[--Rup];

		spin_lock_irqsave(&UnixRupP->RupLock, flags);

		/*
		 ** First check for incoming commands:
		 */
		if (readw(&UnixRupP->RupP->rxcontrol) != RX_RUP_INACTIVE) {
			int FreeMe;

			PacketP = (struct PKT __iomem *) RIO_PTR(HostP->Caddr, readw(&UnixRupP->RupP->rxpkt));

			switch (readb(&PacketP->dest_port)) {
			case BOOT_RUP:
				rio_dprintk(RIO_DEBUG_CMD, "Incoming Boot %s packet '%x'\n", readb(&PacketP->len) & 0x80 ? "Command" : "Data", readb(&PacketP->data[0]));
				rio_spin_unlock_irqrestore(&UnixRupP->RupLock, flags);
				FreeMe = RIOBootRup(p, Rup, HostP, PacketP);
				rio_spin_lock_irqsave(&UnixRupP->RupLock, flags);
				break;

			case COMMAND_RUP:
				/*
				 ** Free the RUP lock as loss of carrier causes a
				 ** ttyflush which will (eventually) call another
				 ** routine that uses the RUP lock.
				 */
				rio_spin_unlock_irqrestore(&UnixRupP->RupLock, flags);
				FreeMe = RIOCommandRup(p, Rup, HostP, PacketP);
				if (readb(&PacketP->data[5]) == MEMDUMP) {
					rio_dprintk(RIO_DEBUG_CMD, "Memdump from 0x%x complete\n", readw(&(PacketP->data[6])));
					rio_memcpy_fromio(p->RIOMemDump, &(PacketP->data[8]), 32);
				}
				rio_spin_lock_irqsave(&UnixRupP->RupLock, flags);
				break;

			case ROUTE_RUP:
				rio_spin_unlock_irqrestore(&UnixRupP->RupLock, flags);
				FreeMe = RIORouteRup(p, Rup, HostP, PacketP);
				rio_spin_lock_irqsave(&UnixRupP->RupLock, flags);
				break;

			default:
				rio_dprintk(RIO_DEBUG_CMD, "Unknown RUP %d\n", readb(&PacketP->dest_port));
				FreeMe = 1;
				break;
			}

			if (FreeMe) {
				rio_dprintk(RIO_DEBUG_CMD, "Free processed incoming command packet\n");
				put_free_end(HostP, PacketP);

				writew(RX_RUP_INACTIVE, &UnixRupP->RupP->rxcontrol);

				if (readw(&UnixRupP->RupP->handshake) == PHB_HANDSHAKE_SET) {
					rio_dprintk(RIO_DEBUG_CMD, "Handshake rup %d\n", Rup);
					writew(PHB_HANDSHAKE_SET | PHB_HANDSHAKE_RESET, &UnixRupP->RupP->handshake);
				}
			}
		}

		/*
		 ** IF a command was running on the port,
		 ** and it has completed, then tidy it up.
		 */
		if ((CmdBlkP = UnixRupP->CmdPendingP) &&	/* ASSIGN! */
		    (readw(&UnixRupP->RupP->txcontrol) == TX_RUP_INACTIVE)) {
			/*
			 ** we are idle.
			 ** there is a command in pending.
			 ** Therefore, this command has finished.
			 ** So, wakeup whoever is waiting for it (and tell them
			 ** what happened).
			 */
			if (CmdBlkP->Packet.dest_port == BOOT_RUP)
				rio_dprintk(RIO_DEBUG_CMD, "Free Boot %s Command Block '%x'\n", CmdBlkP->Packet.len & 0x80 ? "Command" : "Data", CmdBlkP->Packet.data[0]);

			rio_dprintk(RIO_DEBUG_CMD, "Command %p completed\n", CmdBlkP);

			/*
			 ** Clear the Rup lock to prevent mutual exclusion.
			 */
			if (CmdBlkP->PostFuncP) {
				rio_spin_unlock_irqrestore(&UnixRupP->RupLock, flags);
				(*CmdBlkP->PostFuncP) (CmdBlkP->PostArg, CmdBlkP);
				rio_spin_lock_irqsave(&UnixRupP->RupLock, flags);
			}

			/*
			 ** ....clear the pending flag....
			 */
			UnixRupP->CmdPendingP = NULL;

			/*
			 ** ....and return the command block to the freelist.
			 */
			RIOFreeCmdBlk(CmdBlkP);
		}

		/*
		 ** If there is a command for this rup, and the rup
		 ** is idle, then process the command
		 */
		if ((CmdBlkP = UnixRupP->CmdsWaitingP) &&	/* ASSIGN! */
		    (UnixRupP->CmdPendingP == NULL) && (readw(&UnixRupP->RupP->txcontrol) == TX_RUP_INACTIVE)) {
			/*
			 ** if the pre-function is non-zero, call it.
			 ** If it returns RIO_FAIL then don't
			 ** send this command yet!
			 */
			if (!(CmdBlkP->PreFuncP ? (*CmdBlkP->PreFuncP) (CmdBlkP->PreArg, CmdBlkP) : 1)) {
				rio_dprintk(RIO_DEBUG_CMD, "Not ready to start command %p\n", CmdBlkP);
			} else {
				rio_dprintk(RIO_DEBUG_CMD, "Start new command %p Cmd byte is 0x%x\n", CmdBlkP, CmdBlkP->Packet.data[0]);
				/*
				 ** Whammy! blat that pack!
				 */
				HostP->Copy(&CmdBlkP->Packet, RIO_PTR(HostP->Caddr, readw(&UnixRupP->RupP->txpkt)), sizeof(struct PKT));

				/*
				 ** remove the command from the rup command queue...
				 */
				UnixRupP->CmdsWaitingP = CmdBlkP->NextP;

				/*
				 ** ...and place it on the pending position.
				 */
				UnixRupP->CmdPendingP = CmdBlkP;

				/*
				 ** set the command register
				 */
				writew(TX_PACKET_READY, &UnixRupP->RupP->txcontrol);

				/*
				 ** the command block will be freed
				 ** when the command has been processed.
				 */
			}
		}
		spin_unlock_irqrestore(&UnixRupP->RupLock, flags);
	} while (Rup);
}

int RIOWFlushMark(unsigned long iPortP, struct CmdBlk *CmdBlkP)
{
	struct Port *PortP = (struct Port *) iPortP;
	unsigned long flags;

	rio_spin_lock_irqsave(&PortP->portSem, flags);
	PortP->WflushFlag++;
	PortP->MagicFlags |= MAGIC_FLUSH;
	rio_spin_unlock_irqrestore(&PortP->portSem, flags);
	return RIOUnUse(iPortP, CmdBlkP);
}

int RIORFlushEnable(unsigned long iPortP, struct CmdBlk *CmdBlkP)
{
	struct Port *PortP = (struct Port *) iPortP;
	struct PKT __iomem *PacketP;
	unsigned long flags;

	rio_spin_lock_irqsave(&PortP->portSem, flags);

	while (can_remove_receive(&PacketP, PortP)) {
		remove_receive(PortP);
		put_free_end(PortP->HostP, PacketP);
	}

	if (readw(&PortP->PhbP->handshake) == PHB_HANDSHAKE_SET) {
		/*
		 ** MAGIC! (Basically, handshake the RX buffer, so that
		 ** the RTAs upstream can be re-enabled.)
		 */
		rio_dprintk(RIO_DEBUG_CMD, "Util: Set RX handshake bit\n");
		writew(PHB_HANDSHAKE_SET | PHB_HANDSHAKE_RESET, &PortP->PhbP->handshake);
	}
	rio_spin_unlock_irqrestore(&PortP->portSem, flags);
	return RIOUnUse(iPortP, CmdBlkP);
}

int RIOUnUse(unsigned long iPortP, struct CmdBlk *CmdBlkP)
{
	struct Port *PortP = (struct Port *) iPortP;
	unsigned long flags;

	rio_spin_lock_irqsave(&PortP->portSem, flags);

	rio_dprintk(RIO_DEBUG_CMD, "Decrement in use count for port\n");

	if (PortP->InUse) {
		if (--PortP->InUse != NOT_INUSE) {
			rio_spin_unlock_irqrestore(&PortP->portSem, flags);
			return 0;
		}
	}
	/*
	 ** While PortP->InUse is set (i.e. a preemptive command has been sent to
	 ** the RTA and is awaiting completion), any transmit data is prevented from
	 ** being transferred from the write queue into the transmit packets
	 ** (add_transmit) and no furthur transmit interrupt will be sent for that
	 ** data. The next interrupt will occur up to 500ms later (RIOIntr is called
	 ** twice a second as a saftey measure). This was the case when kermit was
	 ** used to send data into a RIO port. After each packet was sent, TCFLSH
	 ** was called to flush the read queue preemptively. PortP->InUse was
	 ** incremented, thereby blocking the 6 byte acknowledgement packet
	 ** transmitted back. This acknowledgment hung around for 500ms before
	 ** being sent, thus reducing input performance substantially!.
	 ** When PortP->InUse becomes NOT_INUSE, we must ensure that any data
	 ** hanging around in the transmit buffer is sent immediately.
	 */
	writew(1, &PortP->HostP->ParmMapP->tx_intr);
	/* What to do here ..
	   wakeup( (caddr_t)&(PortP->InUse) );
	 */
	rio_spin_unlock_irqrestore(&PortP->portSem, flags);
	return 0;
}

/*
** 
** How to use this file:
** 
** To send a command down a rup, you need to allocate a command block, fill
** in the packet information, fill in the command number, fill in the pre-
** and post- functions and arguments, and then add the command block to the
** queue of command blocks for the port in question. When the port is idle,
** then the pre-function will be called. If this returns RIO_FAIL then the
** command will be re-queued and tried again at a later date (probably in one
** clock tick). If the pre-function returns NOT RIO_FAIL, then the command
** packet will be queued on the RUP, and the txcontrol field set to the
** command number. When the txcontrol field has changed from being the
** command number, then the post-function will be called, with the argument
** specified earlier, a pointer to the command block, and the value of
** txcontrol.
** 
** To allocate a command block, call RIOGetCmdBlk(). This returns a pointer
** to the command block structure allocated, or NULL if there aren't any.
** The block will have been zeroed for you.
** 
** The structure has the following fields:
** 
** struct CmdBlk
** {
**	 struct CmdBlk *NextP;		  ** Pointer to next command block   **
**	 struct PKT	 Packet;		** A packet, to copy to the rup	**
**			int	 (*PreFuncP)();  ** The func to call to check if OK **
**			int	 PreArg;		** The arg for the func			**
**			int	 (*PostFuncP)(); ** The func to call when completed **
**			int	 PostArg;	   ** The arg for the func			**
** };
** 
** You need to fill in ALL fields EXCEPT NextP, which is used to link the
** blocks together either on the free list or on the Rup list.
** 
** Packet is an actual packet structure to be filled in with the packet
** information associated with the command. You need to fill in everything,
** as the command processor doesn't process the command packet in any way.
** 
** The PreFuncP is called before the packet is enqueued on the host rup.
** PreFuncP is called as (*PreFuncP)(PreArg, CmdBlkP);. PreFuncP must
** return !RIO_FAIL to have the packet queued on the rup, and RIO_FAIL
** if the packet is NOT to be queued.
** 
** The PostFuncP is called when the command has completed. It is called
** as (*PostFuncP)(PostArg, CmdBlkP, txcontrol);. PostFuncP is not expected
** to return a value. PostFuncP does NOT need to free the command block,
** as this happens automatically after PostFuncP returns.
** 
** Once the command block has been filled in, it is attached to the correct
** queue by calling RIOQueueCmdBlk( HostP, Rup, CmdBlkP ) where HostP is
** a pointer to the struct Host, Rup is the NUMBER of the rup (NOT a pointer
** to it!), and CmdBlkP is the pointer to the command block allocated using
** RIOGetCmdBlk().
** 
*/
