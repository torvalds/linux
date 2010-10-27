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
**	Module		: rioroute.c
**	SID		: 1.3
**	Last Modified	: 11/6/98 10:33:46
**	Retrieved	: 11/6/98 10:33:50
**
**  ident @(#)rioroute.c	1.3
**
** -----------------------------------------------------------------------------
*/

#include <linux/module.h>
#include <linux/errno.h>
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

static int RIOCheckIsolated(struct rio_info *, struct Host *, unsigned int);
static int RIOIsolate(struct rio_info *, struct Host *, unsigned int);
static int RIOCheck(struct Host *, unsigned int);
static void RIOConCon(struct rio_info *, struct Host *, unsigned int, unsigned int, unsigned int, unsigned int, int);


/*
** Incoming on the ROUTE_RUP
** I wrote this while I was tired. Forgive me.
*/
int RIORouteRup(struct rio_info *p, unsigned int Rup, struct Host *HostP, struct PKT __iomem * PacketP)
{
	struct PktCmd __iomem *PktCmdP = (struct PktCmd __iomem *) PacketP->data;
	struct PktCmd_M *PktReplyP;
	struct CmdBlk *CmdBlkP;
	struct Port *PortP;
	struct Map *MapP;
	struct Top *TopP;
	int ThisLink, ThisLinkMin, ThisLinkMax;
	int port;
	int Mod, Mod1, Mod2;
	unsigned short RtaType;
	unsigned int RtaUniq;
	unsigned int ThisUnit, ThisUnit2;	/* 2 ids to accommodate 16 port RTA */
	unsigned int OldUnit, NewUnit, OldLink, NewLink;
	char *MyType, *MyName;
	int Lies;
	unsigned long flags;

	/*
	 ** Is this unit telling us it's current link topology?
	 */
	if (readb(&PktCmdP->Command) == ROUTE_TOPOLOGY) {
		MapP = HostP->Mapping;

		/*
		 ** The packet can be sent either by the host or by an RTA.
		 ** If it comes from the host, then we need to fill in the
		 ** Topology array in the host structure. If it came in
		 ** from an RTA then we need to fill in the Mapping structure's
		 ** Topology array for the unit.
		 */
		if (Rup >= (unsigned short) MAX_RUP) {
			ThisUnit = HOST_ID;
			TopP = HostP->Topology;
			MyType = "Host";
			MyName = HostP->Name;
			ThisLinkMin = ThisLinkMax = Rup - MAX_RUP;
		} else {
			ThisUnit = Rup + 1;
			TopP = HostP->Mapping[Rup].Topology;
			MyType = "RTA";
			MyName = HostP->Mapping[Rup].Name;
			ThisLinkMin = 0;
			ThisLinkMax = LINKS_PER_UNIT - 1;
		}

		/*
		 ** Lies will not be tolerated.
		 ** If any pair of links claim to be connected to the same
		 ** place, then ignore this packet completely.
		 */
		Lies = 0;
		for (ThisLink = ThisLinkMin + 1; ThisLink <= ThisLinkMax; ThisLink++) {
			/*
			 ** it won't lie about network interconnect, total disconnects
			 ** and no-IDs. (or at least, it doesn't *matter* if it does)
			 */
			if (readb(&PktCmdP->RouteTopology[ThisLink].Unit) > (unsigned short) MAX_RUP)
				continue;

			for (NewLink = ThisLinkMin; NewLink < ThisLink; NewLink++) {
				if ((readb(&PktCmdP->RouteTopology[ThisLink].Unit) == readb(&PktCmdP->RouteTopology[NewLink].Unit)) && (readb(&PktCmdP->RouteTopology[ThisLink].Link) == readb(&PktCmdP->RouteTopology[NewLink].Link))) {
					Lies++;
				}
			}
		}

		if (Lies) {
			rio_dprintk(RIO_DEBUG_ROUTE, "LIES! DAMN LIES! %d LIES!\n", Lies);
			rio_dprintk(RIO_DEBUG_ROUTE, "%d:%c %d:%c %d:%c %d:%c\n",
				    readb(&PktCmdP->RouteTopology[0].Unit),
				    'A' + readb(&PktCmdP->RouteTopology[0].Link),
				    readb(&PktCmdP->RouteTopology[1].Unit),
				    'A' + readb(&PktCmdP->RouteTopology[1].Link), readb(&PktCmdP->RouteTopology[2].Unit), 'A' + readb(&PktCmdP->RouteTopology[2].Link), readb(&PktCmdP->RouteTopology[3].Unit), 'A' + readb(&PktCmdP->RouteTopology[3].Link));
			return 1;
		}

		/*
		 ** now, process each link.
		 */
		for (ThisLink = ThisLinkMin; ThisLink <= ThisLinkMax; ThisLink++) {
			/*
			 ** this is what it was connected to
			 */
			OldUnit = TopP[ThisLink].Unit;
			OldLink = TopP[ThisLink].Link;

			/*
			 ** this is what it is now connected to
			 */
			NewUnit = readb(&PktCmdP->RouteTopology[ThisLink].Unit);
			NewLink = readb(&PktCmdP->RouteTopology[ThisLink].Link);

			if (OldUnit != NewUnit || OldLink != NewLink) {
				/*
				 ** something has changed!
				 */

				if (NewUnit > MAX_RUP && NewUnit != ROUTE_DISCONNECT && NewUnit != ROUTE_NO_ID && NewUnit != ROUTE_INTERCONNECT) {
					rio_dprintk(RIO_DEBUG_ROUTE, "I have a link from %s %s to unit %d:%d - I don't like it.\n", MyType, MyName, NewUnit, NewLink);
				} else {
					/*
					 ** put the new values in
					 */
					TopP[ThisLink].Unit = NewUnit;
					TopP[ThisLink].Link = NewLink;

					RIOSetChange(p);

					if (OldUnit <= MAX_RUP) {
						/*
						 ** If something has become bust, then re-enable them messages
						 */
						if (!p->RIONoMessage)
							RIOConCon(p, HostP, ThisUnit, ThisLink, OldUnit, OldLink, DISCONNECT);
					}

					if ((NewUnit <= MAX_RUP) && !p->RIONoMessage)
						RIOConCon(p, HostP, ThisUnit, ThisLink, NewUnit, NewLink, CONNECT);

					if (NewUnit == ROUTE_NO_ID)
						rio_dprintk(RIO_DEBUG_ROUTE, "%s %s (%c) is connected to an unconfigured unit.\n", MyType, MyName, 'A' + ThisLink);

					if (NewUnit == ROUTE_INTERCONNECT) {
						if (!p->RIONoMessage)
							printk(KERN_DEBUG "rio: %s '%s' (%c) is connected to another network.\n", MyType, MyName, 'A' + ThisLink);
					}

					/*
					 ** perform an update for 'the other end', so that these messages
					 ** only appears once. Only disconnect the other end if it is pointing
					 ** at us!
					 */
					if (OldUnit == HOST_ID) {
						if (HostP->Topology[OldLink].Unit == ThisUnit && HostP->Topology[OldLink].Link == ThisLink) {
							rio_dprintk(RIO_DEBUG_ROUTE, "SETTING HOST (%c) TO DISCONNECTED!\n", OldLink + 'A');
							HostP->Topology[OldLink].Unit = ROUTE_DISCONNECT;
							HostP->Topology[OldLink].Link = NO_LINK;
						} else {
							rio_dprintk(RIO_DEBUG_ROUTE, "HOST(%c) WAS NOT CONNECTED TO %s (%c)!\n", OldLink + 'A', HostP->Mapping[ThisUnit - 1].Name, ThisLink + 'A');
						}
					} else if (OldUnit <= MAX_RUP) {
						if (HostP->Mapping[OldUnit - 1].Topology[OldLink].Unit == ThisUnit && HostP->Mapping[OldUnit - 1].Topology[OldLink].Link == ThisLink) {
							rio_dprintk(RIO_DEBUG_ROUTE, "SETTING RTA %s (%c) TO DISCONNECTED!\n", HostP->Mapping[OldUnit - 1].Name, OldLink + 'A');
							HostP->Mapping[OldUnit - 1].Topology[OldLink].Unit = ROUTE_DISCONNECT;
							HostP->Mapping[OldUnit - 1].Topology[OldLink].Link = NO_LINK;
						} else {
							rio_dprintk(RIO_DEBUG_ROUTE, "RTA %s (%c) WAS NOT CONNECTED TO %s (%c)\n", HostP->Mapping[OldUnit - 1].Name, OldLink + 'A', HostP->Mapping[ThisUnit - 1].Name, ThisLink + 'A');
						}
					}
					if (NewUnit == HOST_ID) {
						rio_dprintk(RIO_DEBUG_ROUTE, "MARKING HOST (%c) CONNECTED TO %s (%c)\n", NewLink + 'A', MyName, ThisLink + 'A');
						HostP->Topology[NewLink].Unit = ThisUnit;
						HostP->Topology[NewLink].Link = ThisLink;
					} else if (NewUnit <= MAX_RUP) {
						rio_dprintk(RIO_DEBUG_ROUTE, "MARKING RTA %s (%c) CONNECTED TO %s (%c)\n", HostP->Mapping[NewUnit - 1].Name, NewLink + 'A', MyName, ThisLink + 'A');
						HostP->Mapping[NewUnit - 1].Topology[NewLink].Unit = ThisUnit;
						HostP->Mapping[NewUnit - 1].Topology[NewLink].Link = ThisLink;
					}
				}
				RIOSetChange(p);
				RIOCheckIsolated(p, HostP, OldUnit);
			}
		}
		return 1;
	}

	/*
	 ** The only other command we recognise is a route_request command
	 */
	if (readb(&PktCmdP->Command) != ROUTE_REQUEST) {
		rio_dprintk(RIO_DEBUG_ROUTE, "Unknown command %d received on rup %d host %p ROUTE_RUP\n", readb(&PktCmdP->Command), Rup, HostP);
		return 1;
	}

	RtaUniq = (readb(&PktCmdP->UniqNum[0])) + (readb(&PktCmdP->UniqNum[1]) << 8) + (readb(&PktCmdP->UniqNum[2]) << 16) + (readb(&PktCmdP->UniqNum[3]) << 24);

	/*
	 ** Determine if 8 or 16 port RTA
	 */
	RtaType = GetUnitType(RtaUniq);

	rio_dprintk(RIO_DEBUG_ROUTE, "Received a request for an ID for serial number %x\n", RtaUniq);

	Mod = readb(&PktCmdP->ModuleTypes);
	Mod1 = LONYBLE(Mod);
	if (RtaType == TYPE_RTA16) {
		/*
		 ** Only one ident is set for a 16 port RTA. To make compatible
		 ** with 8 port, set 2nd ident in Mod2 to the same as Mod1.
		 */
		Mod2 = Mod1;
		rio_dprintk(RIO_DEBUG_ROUTE, "Backplane type is %s (all ports)\n", p->RIOModuleTypes[Mod1].Name);
	} else {
		Mod2 = HINYBLE(Mod);
		rio_dprintk(RIO_DEBUG_ROUTE, "Module types are %s (ports 0-3) and %s (ports 4-7)\n", p->RIOModuleTypes[Mod1].Name, p->RIOModuleTypes[Mod2].Name);
	}

	/*
	 ** try to unhook a command block from the command free list.
	 */
	if (!(CmdBlkP = RIOGetCmdBlk())) {
		rio_dprintk(RIO_DEBUG_ROUTE, "No command blocks to route RTA! come back later.\n");
		return 0;
	}

	/*
	 ** Fill in the default info on the command block
	 */
	CmdBlkP->Packet.dest_unit = Rup;
	CmdBlkP->Packet.dest_port = ROUTE_RUP;
	CmdBlkP->Packet.src_unit = HOST_ID;
	CmdBlkP->Packet.src_port = ROUTE_RUP;
	CmdBlkP->Packet.len = PKT_CMD_BIT | 1;
	CmdBlkP->PreFuncP = CmdBlkP->PostFuncP = NULL;
	PktReplyP = (struct PktCmd_M *) CmdBlkP->Packet.data;

	if (!RIOBootOk(p, HostP, RtaUniq)) {
		rio_dprintk(RIO_DEBUG_ROUTE, "RTA %x tried to get an ID, but does not belong - FOAD it!\n", RtaUniq);
		PktReplyP->Command = ROUTE_FOAD;
		memcpy(PktReplyP->CommandText, "RT_FOAD", 7);
		RIOQueueCmdBlk(HostP, Rup, CmdBlkP);
		return 1;
	}

	/*
	 ** Check to see if the RTA is configured for this host
	 */
	for (ThisUnit = 0; ThisUnit < MAX_RUP; ThisUnit++) {
		rio_dprintk(RIO_DEBUG_ROUTE, "Entry %d Flags=%s %s UniqueNum=0x%x\n",
			    ThisUnit, HostP->Mapping[ThisUnit].Flags & SLOT_IN_USE ? "Slot-In-Use" : "Not In Use", HostP->Mapping[ThisUnit].Flags & SLOT_TENTATIVE ? "Slot-Tentative" : "Not Tentative", HostP->Mapping[ThisUnit].RtaUniqueNum);

		/*
		 ** We have an entry for it.
		 */
		if ((HostP->Mapping[ThisUnit].Flags & (SLOT_IN_USE | SLOT_TENTATIVE)) && (HostP->Mapping[ThisUnit].RtaUniqueNum == RtaUniq)) {
			if (RtaType == TYPE_RTA16) {
				ThisUnit2 = HostP->Mapping[ThisUnit].ID2 - 1;
				rio_dprintk(RIO_DEBUG_ROUTE, "Found unit 0x%x at slots %d+%d\n", RtaUniq, ThisUnit, ThisUnit2);
			} else
				rio_dprintk(RIO_DEBUG_ROUTE, "Found unit 0x%x at slot %d\n", RtaUniq, ThisUnit);
			/*
			 ** If we have no knowledge of booting it, then the host has
			 ** been re-booted, and so we must kill the RTA, so that it
			 ** will be booted again (potentially with new bins)
			 ** and it will then re-ask for an ID, which we will service.
			 */
			if ((HostP->Mapping[ThisUnit].Flags & SLOT_IN_USE) && !(HostP->Mapping[ThisUnit].Flags & RTA_BOOTED)) {
				if (!(HostP->Mapping[ThisUnit].Flags & MSG_DONE)) {
					if (!p->RIONoMessage)
						printk(KERN_DEBUG "rio: RTA '%s' is being updated.\n", HostP->Mapping[ThisUnit].Name);
					HostP->Mapping[ThisUnit].Flags |= MSG_DONE;
				}
				PktReplyP->Command = ROUTE_FOAD;
				memcpy(PktReplyP->CommandText, "RT_FOAD", 7);
				RIOQueueCmdBlk(HostP, Rup, CmdBlkP);
				return 1;
			}

			/*
			 ** Send the ID (entry) to this RTA. The ID number is implicit as
			 ** the offset into the table. It is worth noting at this stage
			 ** that offset zero in the table contains the entries for the
			 ** RTA with ID 1!!!!
			 */
			PktReplyP->Command = ROUTE_ALLOCATE;
			PktReplyP->IDNum = ThisUnit + 1;
			if (RtaType == TYPE_RTA16) {
				if (HostP->Mapping[ThisUnit].Flags & SLOT_IN_USE)
					/*
					 ** Adjust the phb and tx pkt dest_units for 2nd block of 8
					 ** only if the RTA has ports associated (SLOT_IN_USE)
					 */
					RIOFixPhbs(p, HostP, ThisUnit2);
				PktReplyP->IDNum2 = ThisUnit2 + 1;
				rio_dprintk(RIO_DEBUG_ROUTE, "RTA '%s' has been allocated IDs %d+%d\n", HostP->Mapping[ThisUnit].Name, PktReplyP->IDNum, PktReplyP->IDNum2);
			} else {
				PktReplyP->IDNum2 = ROUTE_NO_ID;
				rio_dprintk(RIO_DEBUG_ROUTE, "RTA '%s' has been allocated ID %d\n", HostP->Mapping[ThisUnit].Name, PktReplyP->IDNum);
			}
			memcpy(PktReplyP->CommandText, "RT_ALLOCAT", 10);

			RIOQueueCmdBlk(HostP, Rup, CmdBlkP);

			/*
			 ** If this is a freshly booted RTA, then we need to re-open
			 ** the ports, if any where open, so that data may once more
			 ** flow around the system!
			 */
			if ((HostP->Mapping[ThisUnit].Flags & RTA_NEWBOOT) && (HostP->Mapping[ThisUnit].SysPort != NO_PORT)) {
				/*
				 ** look at the ports associated with this beast and
				 ** see if any where open. If they was, then re-open
				 ** them, using the info from the tty flags.
				 */
				for (port = 0; port < PORTS_PER_RTA; port++) {
					PortP = p->RIOPortp[port + HostP->Mapping[ThisUnit].SysPort];
					if (PortP->State & (RIO_MOPEN | RIO_LOPEN)) {
						rio_dprintk(RIO_DEBUG_ROUTE, "Re-opened this port\n");
						rio_spin_lock_irqsave(&PortP->portSem, flags);
						PortP->MagicFlags |= MAGIC_REBOOT;
						rio_spin_unlock_irqrestore(&PortP->portSem, flags);
					}
				}
				if (RtaType == TYPE_RTA16) {
					for (port = 0; port < PORTS_PER_RTA; port++) {
						PortP = p->RIOPortp[port + HostP->Mapping[ThisUnit2].SysPort];
						if (PortP->State & (RIO_MOPEN | RIO_LOPEN)) {
							rio_dprintk(RIO_DEBUG_ROUTE, "Re-opened this port\n");
							rio_spin_lock_irqsave(&PortP->portSem, flags);
							PortP->MagicFlags |= MAGIC_REBOOT;
							rio_spin_unlock_irqrestore(&PortP->portSem, flags);
						}
					}
				}
			}

			/*
			 ** keep a copy of the module types!
			 */
			HostP->UnixRups[ThisUnit].ModTypes = Mod;
			if (RtaType == TYPE_RTA16)
				HostP->UnixRups[ThisUnit2].ModTypes = Mod;

			/*
			 ** If either of the modules on this unit is read-only or write-only
			 ** or none-xprint, then we need to transfer that info over to the
			 ** relevant ports.
			 */
			if (HostP->Mapping[ThisUnit].SysPort != NO_PORT) {
				for (port = 0; port < PORTS_PER_MODULE; port++) {
					p->RIOPortp[port + HostP->Mapping[ThisUnit].SysPort]->Config &= ~RIO_NOMASK;
					p->RIOPortp[port + HostP->Mapping[ThisUnit].SysPort]->Config |= p->RIOModuleTypes[Mod1].Flags[port];
					p->RIOPortp[port + PORTS_PER_MODULE + HostP->Mapping[ThisUnit].SysPort]->Config &= ~RIO_NOMASK;
					p->RIOPortp[port + PORTS_PER_MODULE + HostP->Mapping[ThisUnit].SysPort]->Config |= p->RIOModuleTypes[Mod2].Flags[port];
				}
				if (RtaType == TYPE_RTA16) {
					for (port = 0; port < PORTS_PER_MODULE; port++) {
						p->RIOPortp[port + HostP->Mapping[ThisUnit2].SysPort]->Config &= ~RIO_NOMASK;
						p->RIOPortp[port + HostP->Mapping[ThisUnit2].SysPort]->Config |= p->RIOModuleTypes[Mod1].Flags[port];
						p->RIOPortp[port + PORTS_PER_MODULE + HostP->Mapping[ThisUnit2].SysPort]->Config &= ~RIO_NOMASK;
						p->RIOPortp[port + PORTS_PER_MODULE + HostP->Mapping[ThisUnit2].SysPort]->Config |= p->RIOModuleTypes[Mod2].Flags[port];
					}
				}
			}

			/*
			 ** Job done, get on with the interrupts!
			 */
			return 1;
		}
	}
	/*
	 ** There is no table entry for this RTA at all.
	 **
	 ** Lets check to see if we actually booted this unit - if not,
	 ** then we reset it and it will go round the loop of being booted
	 ** we can then worry about trying to fit it into the table.
	 */
	for (ThisUnit = 0; ThisUnit < HostP->NumExtraBooted; ThisUnit++)
		if (HostP->ExtraUnits[ThisUnit] == RtaUniq)
			break;
	if (ThisUnit == HostP->NumExtraBooted && ThisUnit != MAX_EXTRA_UNITS) {
		/*
		 ** if the unit wasn't in the table, and the table wasn't full, then
		 ** we reset the unit, because we didn't boot it.
		 ** However, if the table is full, it could be that we did boot
		 ** this unit, and so we won't reboot it, because it isn't really
		 ** all that disasterous to keep the old bins in most cases. This
		 ** is a rather tacky feature, but we are on the edge of reallity
		 ** here, because the implication is that someone has connected
		 ** 16+MAX_EXTRA_UNITS onto one host.
		 */
		static int UnknownMesgDone = 0;

		if (!UnknownMesgDone) {
			if (!p->RIONoMessage)
				printk(KERN_DEBUG "rio: One or more unknown RTAs are being updated.\n");
			UnknownMesgDone = 1;
		}

		PktReplyP->Command = ROUTE_FOAD;
		memcpy(PktReplyP->CommandText, "RT_FOAD", 7);
	} else {
		/*
		 ** we did boot it (as an extra), and there may now be a table
		 ** slot free (because of a delete), so we will try to make
		 ** a tentative entry for it, so that the configurator can see it
		 ** and fill in the details for us.
		 */
		if (RtaType == TYPE_RTA16) {
			if (RIOFindFreeID(p, HostP, &ThisUnit, &ThisUnit2) == 0) {
				RIODefaultName(p, HostP, ThisUnit);
				rio_fill_host_slot(ThisUnit, ThisUnit2, RtaUniq, HostP);
			}
		} else {
			if (RIOFindFreeID(p, HostP, &ThisUnit, NULL) == 0) {
				RIODefaultName(p, HostP, ThisUnit);
				rio_fill_host_slot(ThisUnit, 0, RtaUniq, HostP);
			}
		}
		PktReplyP->Command = ROUTE_USED;
		memcpy(PktReplyP->CommandText, "RT_USED", 7);
	}
	RIOQueueCmdBlk(HostP, Rup, CmdBlkP);
	return 1;
}


void RIOFixPhbs(struct rio_info *p, struct Host *HostP, unsigned int unit)
{
	unsigned short link, port;
	struct Port *PortP;
	unsigned long flags;
	int PortN = HostP->Mapping[unit].SysPort;

	rio_dprintk(RIO_DEBUG_ROUTE, "RIOFixPhbs unit %d sysport %d\n", unit, PortN);

	if (PortN != -1) {
		unsigned short dest_unit = HostP->Mapping[unit].ID2;

		/*
		 ** Get the link number used for the 1st 8 phbs on this unit.
		 */
		PortP = p->RIOPortp[HostP->Mapping[dest_unit - 1].SysPort];

		link = readw(&PortP->PhbP->link);

		for (port = 0; port < PORTS_PER_RTA; port++, PortN++) {
			unsigned short dest_port = port + 8;
			u16 __iomem *TxPktP;
			struct PKT __iomem *Pkt;

			PortP = p->RIOPortp[PortN];

			rio_spin_lock_irqsave(&PortP->portSem, flags);
			/*
			 ** If RTA is not powered on, the tx packets will be
			 ** unset, so go no further.
			 */
			if (!PortP->TxStart) {
				rio_dprintk(RIO_DEBUG_ROUTE, "Tx pkts not set up yet\n");
				rio_spin_unlock_irqrestore(&PortP->portSem, flags);
				break;
			}

			/*
			 ** For the second slot of a 16 port RTA, the driver needs to
			 ** sort out the phb to port mappings. The dest_unit for this
			 ** group of 8 phbs is set to the dest_unit of the accompanying
			 ** 8 port block. The dest_port of the second unit is set to
			 ** be in the range 8-15 (i.e. 8 is added). Thus, for a 16 port
			 ** RTA with IDs 5 and 6, traffic bound for port 6 of unit 6
			 ** (being the second map ID) will be sent to dest_unit 5, port
			 ** 14. When this RTA is deleted, dest_unit for ID 6 will be
			 ** restored, and the dest_port will be reduced by 8.
			 ** Transmit packets also have a destination field which needs
			 ** adjusting in the same manner.
			 ** Note that the unit/port bytes in 'dest' are swapped.
			 ** We also need to adjust the phb and rup link numbers for the
			 ** second block of 8 ttys.
			 */
			for (TxPktP = PortP->TxStart; TxPktP <= PortP->TxEnd; TxPktP++) {
				/*
				 ** *TxPktP is the pointer to the transmit packet on the host
				 ** card. This needs to be translated into a 32 bit pointer
				 ** so it can be accessed from the driver.
				 */
				Pkt = (struct PKT __iomem *) RIO_PTR(HostP->Caddr, readw(TxPktP));

				/*
				 ** If the packet is used, reset it.
				 */
				Pkt = (struct PKT __iomem *) ((unsigned long) Pkt & ~PKT_IN_USE);
				writeb(dest_unit, &Pkt->dest_unit);
				writeb(dest_port, &Pkt->dest_port);
			}
			rio_dprintk(RIO_DEBUG_ROUTE, "phb dest: Old %x:%x New %x:%x\n", readw(&PortP->PhbP->destination) & 0xff, (readw(&PortP->PhbP->destination) >> 8) & 0xff, dest_unit, dest_port);
			writew(dest_unit + (dest_port << 8), &PortP->PhbP->destination);
			writew(link, &PortP->PhbP->link);

			rio_spin_unlock_irqrestore(&PortP->portSem, flags);
		}
		/*
		 ** Now make sure the range of ports to be serviced includes
		 ** the 2nd 8 on this 16 port RTA.
		 */
		if (link > 3)
			return;
		if (((unit * 8) + 7) > readw(&HostP->LinkStrP[link].last_port)) {
			rio_dprintk(RIO_DEBUG_ROUTE, "last port on host link %d: %d\n", link, (unit * 8) + 7);
			writew((unit * 8) + 7, &HostP->LinkStrP[link].last_port);
		}
	}
}

/*
** Check to see if the new disconnection has isolated this unit.
** If it has, then invalidate all its link information, and tell
** the world about it. This is done to ensure that the configurator
** only gets up-to-date information about what is going on.
*/
static int RIOCheckIsolated(struct rio_info *p, struct Host *HostP, unsigned int UnitId)
{
	unsigned long flags;
	rio_spin_lock_irqsave(&HostP->HostLock, flags);

	if (RIOCheck(HostP, UnitId)) {
		rio_dprintk(RIO_DEBUG_ROUTE, "Unit %d is NOT isolated\n", UnitId);
		rio_spin_unlock_irqrestore(&HostP->HostLock, flags);
		return (0);
	}

	RIOIsolate(p, HostP, UnitId);
	RIOSetChange(p);
	rio_spin_unlock_irqrestore(&HostP->HostLock, flags);
	return 1;
}

/*
** Invalidate all the link interconnectivity of this unit, and of
** all the units attached to it. This will mean that the entire
** subnet will re-introduce itself.
*/
static int RIOIsolate(struct rio_info *p, struct Host *HostP, unsigned int UnitId)
{
	unsigned int link, unit;

	UnitId--;		/* this trick relies on the Unit Id being UNSIGNED! */

	if (UnitId >= MAX_RUP)	/* dontcha just lurv unsigned maths! */
		return (0);

	if (HostP->Mapping[UnitId].Flags & BEEN_HERE)
		return (0);

	HostP->Mapping[UnitId].Flags |= BEEN_HERE;

	if (p->RIOPrintDisabled == DO_PRINT)
		rio_dprintk(RIO_DEBUG_ROUTE, "RIOMesgIsolated %s", HostP->Mapping[UnitId].Name);

	for (link = 0; link < LINKS_PER_UNIT; link++) {
		unit = HostP->Mapping[UnitId].Topology[link].Unit;
		HostP->Mapping[UnitId].Topology[link].Unit = ROUTE_DISCONNECT;
		HostP->Mapping[UnitId].Topology[link].Link = NO_LINK;
		RIOIsolate(p, HostP, unit);
	}
	HostP->Mapping[UnitId].Flags &= ~BEEN_HERE;
	return 1;
}

static int RIOCheck(struct Host *HostP, unsigned int UnitId)
{
	unsigned char link;

/* 	rio_dprint(RIO_DEBUG_ROUTE, ("Check to see if unit %d has a route to the host\n",UnitId)); */
	rio_dprintk(RIO_DEBUG_ROUTE, "RIOCheck : UnitID = %d\n", UnitId);

	if (UnitId == HOST_ID) {
		/* rio_dprint(RIO_DEBUG_ROUTE, ("Unit %d is NOT isolated - it IS the host!\n", UnitId)); */
		return 1;
	}

	UnitId--;

	if (UnitId >= MAX_RUP) {
		/* rio_dprint(RIO_DEBUG_ROUTE, ("Unit %d - ignored.\n", UnitId)); */
		return 0;
	}

	for (link = 0; link < LINKS_PER_UNIT; link++) {
		if (HostP->Mapping[UnitId].Topology[link].Unit == HOST_ID) {
			/* rio_dprint(RIO_DEBUG_ROUTE, ("Unit %d is connected directly to host via link (%c).\n", 
			   UnitId, 'A'+link)); */
			return 1;
		}
	}

	if (HostP->Mapping[UnitId].Flags & BEEN_HERE) {
		/* rio_dprint(RIO_DEBUG_ROUTE, ("Been to Unit %d before - ignoring\n", UnitId)); */
		return 0;
	}

	HostP->Mapping[UnitId].Flags |= BEEN_HERE;

	for (link = 0; link < LINKS_PER_UNIT; link++) {
		/* rio_dprint(RIO_DEBUG_ROUTE, ("Unit %d check link (%c)\n", UnitId,'A'+link)); */
		if (RIOCheck(HostP, HostP->Mapping[UnitId].Topology[link].Unit)) {
			/* rio_dprint(RIO_DEBUG_ROUTE, ("Unit %d is connected to something that knows the host via link (%c)\n", UnitId,link+'A')); */
			HostP->Mapping[UnitId].Flags &= ~BEEN_HERE;
			return 1;
		}
	}

	HostP->Mapping[UnitId].Flags &= ~BEEN_HERE;

	/* rio_dprint(RIO_DEBUG_ROUTE, ("Unit %d DOESNT KNOW THE HOST!\n", UnitId)); */

	return 0;
}

/*
** Returns the type of unit (host, 16/8 port RTA)
*/

unsigned int GetUnitType(unsigned int Uniq)
{
	switch ((Uniq >> 28) & 0xf) {
	case RIO_AT:
	case RIO_MCA:
	case RIO_EISA:
	case RIO_PCI:
		rio_dprintk(RIO_DEBUG_ROUTE, "Unit type: Host\n");
		return (TYPE_HOST);
	case RIO_RTA_16:
		rio_dprintk(RIO_DEBUG_ROUTE, "Unit type: 16 port RTA\n");
		return (TYPE_RTA16);
	case RIO_RTA:
		rio_dprintk(RIO_DEBUG_ROUTE, "Unit type: 8 port RTA\n");
		return (TYPE_RTA8);
	default:
		rio_dprintk(RIO_DEBUG_ROUTE, "Unit type: Unrecognised\n");
		return (99);
	}
}

int RIOSetChange(struct rio_info *p)
{
	if (p->RIOQuickCheck != NOT_CHANGED)
		return (0);
	p->RIOQuickCheck = CHANGED;
	if (p->RIOSignalProcess) {
		rio_dprintk(RIO_DEBUG_ROUTE, "Send SIG-HUP");
		/*
		   psignal( RIOSignalProcess, SIGHUP );
		 */
	}
	return (0);
}

static void RIOConCon(struct rio_info *p,
		      struct Host *HostP,
		      unsigned int FromId,
		      unsigned int FromLink,
		      unsigned int ToId,
		      unsigned int ToLink,
		      int Change)
{
	char *FromName;
	char *FromType;
	char *ToName;
	char *ToType;
	unsigned int tp;

/*
** 15.10.1998 ARG - ESIL 0759
** (Part) fix for port being trashed when opened whilst RTA "disconnected"
**
** What's this doing in here anyway ?
** It was causing the port to be 'unmapped' if opened whilst RTA "disconnected"
**
** 09.12.1998 ARG - ESIL 0776 - part fix
** Okay, We've found out what this was all about now !
** Someone had botched this to use RIOHalted to indicated the number of RTAs
** 'disconnected'. The value in RIOHalted was then being used in the
** 'RIO_QUICK_CHECK' ioctl. A none zero value indicating that a least one RTA
** is 'disconnected'. The change was put in to satisfy a customer's needs.
** Having taken this bit of code out 'RIO_QUICK_CHECK' now no longer works for
** the customer.
**
    if (Change == CONNECT) {
		if (p->RIOHalted) p->RIOHalted --;
	 }
	 else {
		p->RIOHalted ++;
	 }
**
** So - we need to implement it slightly differently - a new member of the
** rio_info struct - RIORtaDisCons (RIO RTA connections) keeps track of RTA
** connections and disconnections. 
*/
	if (Change == CONNECT) {
		if (p->RIORtaDisCons)
			p->RIORtaDisCons--;
	} else {
		p->RIORtaDisCons++;
	}

	if (p->RIOPrintDisabled == DONT_PRINT)
		return;

	if (FromId > ToId) {
		tp = FromId;
		FromId = ToId;
		ToId = tp;
		tp = FromLink;
		FromLink = ToLink;
		ToLink = tp;
	}

	FromName = FromId ? HostP->Mapping[FromId - 1].Name : HostP->Name;
	FromType = FromId ? "RTA" : "HOST";
	ToName = ToId ? HostP->Mapping[ToId - 1].Name : HostP->Name;
	ToType = ToId ? "RTA" : "HOST";

	rio_dprintk(RIO_DEBUG_ROUTE, "Link between %s '%s' (%c) and %s '%s' (%c) %s.\n", FromType, FromName, 'A' + FromLink, ToType, ToName, 'A' + ToLink, (Change == CONNECT) ? "established" : "disconnected");
	printk(KERN_DEBUG "rio: Link between %s '%s' (%c) and %s '%s' (%c) %s.\n", FromType, FromName, 'A' + FromLink, ToType, ToName, 'A' + ToLink, (Change == CONNECT) ? "established" : "disconnected");
}

/*
** RIORemoveFromSavedTable :
**
** Delete and RTA entry from the saved table given to us
** by the configuration program.
*/
static int RIORemoveFromSavedTable(struct rio_info *p, struct Map *pMap)
{
	int entry;

	/*
	 ** We loop for all entries even after finding an entry and
	 ** zeroing it because we may have two entries to delete if
	 ** it's a 16 port RTA.
	 */
	for (entry = 0; entry < TOTAL_MAP_ENTRIES; entry++) {
		if (p->RIOSavedTable[entry].RtaUniqueNum == pMap->RtaUniqueNum) {
			memset(&p->RIOSavedTable[entry], 0, sizeof(struct Map));
		}
	}
	return 0;
}


/*
** RIOCheckDisconnected :
**
** Scan the unit links to and return zero if the unit is completely
** disconnected.
*/
static int RIOFreeDisconnected(struct rio_info *p, struct Host *HostP, int unit)
{
	int link;


	rio_dprintk(RIO_DEBUG_ROUTE, "RIOFreeDisconnect unit %d\n", unit);
	/*
	 ** If the slot is tentative and does not belong to the
	 ** second half of a 16 port RTA then scan to see if
	 ** is disconnected.
	 */
	for (link = 0; link < LINKS_PER_UNIT; link++) {
		if (HostP->Mapping[unit].Topology[link].Unit != ROUTE_DISCONNECT)
			break;
	}

	/*
	 ** If not all links are disconnected then we can forget about it.
	 */
	if (link < LINKS_PER_UNIT)
		return 1;

#ifdef NEED_TO_FIX_THIS
	/* Ok so all the links are disconnected. But we may have only just
	 ** made this slot tentative and not yet received a topology update.
	 ** Lets check how long ago we made it tentative.
	 */
	rio_dprintk(RIO_DEBUG_ROUTE, "Just about to check LBOLT on entry %d\n", unit);
	if (drv_getparm(LBOLT, (ulong_t *) & current_time))
		rio_dprintk(RIO_DEBUG_ROUTE, "drv_getparm(LBOLT,....) Failed.\n");

	elapse_time = current_time - TentTime[unit];
	rio_dprintk(RIO_DEBUG_ROUTE, "elapse %d = current %d - tent %d (%d usec)\n", elapse_time, current_time, TentTime[unit], drv_hztousec(elapse_time));
	if (drv_hztousec(elapse_time) < WAIT_TO_FINISH) {
		rio_dprintk(RIO_DEBUG_ROUTE, "Skipping slot %d, not timed out yet %d\n", unit, drv_hztousec(elapse_time));
		return 1;
	}
#endif

	/*
	 ** We have found an usable slot.
	 ** If it is half of a 16 port RTA then delete the other half.
	 */
	if (HostP->Mapping[unit].ID2 != 0) {
		int nOther = (HostP->Mapping[unit].ID2) - 1;

		rio_dprintk(RIO_DEBUG_ROUTE, "RioFreedis second slot %d.\n", nOther);
		memset(&HostP->Mapping[nOther], 0, sizeof(struct Map));
	}
	RIORemoveFromSavedTable(p, &HostP->Mapping[unit]);

	return 0;
}


/*
** RIOFindFreeID :
**
** This function scans the given host table for either one
** or two free unit ID's.
*/

int RIOFindFreeID(struct rio_info *p, struct Host *HostP, unsigned int * pID1, unsigned int * pID2)
{
	int unit, tempID;

	/*
	 ** Initialise the ID's to MAX_RUP.
	 ** We do this to make the loop for setting the ID's as simple as
	 ** possible.
	 */
	*pID1 = MAX_RUP;
	if (pID2 != NULL)
		*pID2 = MAX_RUP;

	/*
	 ** Scan all entries of the host mapping table for free slots.
	 ** We scan for free slots first and then if that is not successful
	 ** we start all over again looking for tentative slots we can re-use.
	 */
	for (unit = 0; unit < MAX_RUP; unit++) {
		rio_dprintk(RIO_DEBUG_ROUTE, "Scanning unit %d\n", unit);
		/*
		 ** If the flags are zero then the slot is empty.
		 */
		if (HostP->Mapping[unit].Flags == 0) {
			rio_dprintk(RIO_DEBUG_ROUTE, "      This slot is empty.\n");
			/*
			 ** If we haven't allocated the first ID then do it now.
			 */
			if (*pID1 == MAX_RUP) {
				rio_dprintk(RIO_DEBUG_ROUTE, "Make tentative entry for first unit %d\n", unit);
				*pID1 = unit;

				/*
				 ** If the second ID is not needed then we can return
				 ** now.
				 */
				if (pID2 == NULL)
					return 0;
			} else {
				/*
				 ** Allocate the second slot and return.
				 */
				rio_dprintk(RIO_DEBUG_ROUTE, "Make tentative entry for second unit %d\n", unit);
				*pID2 = unit;
				return 0;
			}
		}
	}

	/*
	 ** If we manage to come out of the free slot loop then we
	 ** need to start all over again looking for tentative slots
	 ** that we can re-use.
	 */
	rio_dprintk(RIO_DEBUG_ROUTE, "Starting to scan for tentative slots\n");
	for (unit = 0; unit < MAX_RUP; unit++) {
		if (((HostP->Mapping[unit].Flags & SLOT_TENTATIVE) || (HostP->Mapping[unit].Flags == 0)) && !(HostP->Mapping[unit].Flags & RTA16_SECOND_SLOT)) {
			rio_dprintk(RIO_DEBUG_ROUTE, "    Slot %d looks promising.\n", unit);

			if (unit == *pID1) {
				rio_dprintk(RIO_DEBUG_ROUTE, "    No it isn't, its the 1st half\n");
				continue;
			}

			/*
			 ** Slot is Tentative or Empty, but not a tentative second
			 ** slot of a 16 porter.
			 ** Attempt to free up this slot (and its parnter if
			 ** it is a 16 port slot. The second slot will become
			 ** empty after a call to RIOFreeDisconnected so thats why
			 ** we look for empty slots above  as well).
			 */
			if (HostP->Mapping[unit].Flags != 0)
				if (RIOFreeDisconnected(p, HostP, unit) != 0)
					continue;
			/*
			 ** If we haven't allocated the first ID then do it now.
			 */
			if (*pID1 == MAX_RUP) {
				rio_dprintk(RIO_DEBUG_ROUTE, "Grab tentative entry for first unit %d\n", unit);
				*pID1 = unit;

				/*
				 ** Clear out this slot now that we intend to use it.
				 */
				memset(&HostP->Mapping[unit], 0, sizeof(struct Map));

				/*
				 ** If the second ID is not needed then we can return
				 ** now.
				 */
				if (pID2 == NULL)
					return 0;
			} else {
				/*
				 ** Allocate the second slot and return.
				 */
				rio_dprintk(RIO_DEBUG_ROUTE, "Grab tentative/empty  entry for second unit %d\n", unit);
				*pID2 = unit;

				/*
				 ** Clear out this slot now that we intend to use it.
				 */
				memset(&HostP->Mapping[unit], 0, sizeof(struct Map));

				/* At this point under the right(wrong?) conditions
				 ** we may have a first unit ID being higher than the
				 ** second unit ID. This is a bad idea if we are about
				 ** to fill the slots with a 16 port RTA.
				 ** Better check and swap them over.
				 */

				if (*pID1 > *pID2) {
					rio_dprintk(RIO_DEBUG_ROUTE, "Swapping IDS %d %d\n", *pID1, *pID2);
					tempID = *pID1;
					*pID1 = *pID2;
					*pID2 = tempID;
				}
				return 0;
			}
		}
	}

	/*
	 ** If we manage to get to the end of the second loop then we
	 ** can give up and return a failure.
	 */
	return 1;
}


/*
** The link switch scenario.
**
** Rta Wun (A) is connected to Tuw (A).
** The tables are all up to date, and the system is OK.
**
** If Wun (A) is now moved to Wun (B) before Wun (A) can
** become disconnected, then the follow happens:
**
** Tuw (A) spots the change of unit:link at the other end
** of its link and Tuw sends a topology packet reflecting
** the change: Tuw (A) now disconnected from Wun (A), and
** this is closely followed by a packet indicating that 
** Tuw (A) is now connected to Wun (B).
**
** Wun (B) will spot that it has now become connected, and
** Wun will send a topology packet, which indicates that
** both Wun (A) and Wun (B) is connected to Tuw (A).
**
** Eventually Wun (A) realises that it is now disconnected
** and Wun will send out a topology packet indicating that
** Wun (A) is now disconnected.
*/
