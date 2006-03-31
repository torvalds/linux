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
**	Module		: riotable.c
**	SID		: 1.2
**	Last Modified	: 11/6/98 10:33:47
**	Retrieved	: 11/6/98 10:33:50
**
**  ident @(#)riotable.c	1.2
**
** -----------------------------------------------------------------------------
*/
#ifdef SCCS_LABELS
static char *_riotable_c_sccs_ = "@(#)riotable.c	1.2";
#endif

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
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

/*
** A configuration table has been loaded. It is now up to us
** to sort it out and use the information contained therein.
*/
int RIONewTable(struct rio_info *p)
{
	int Host, Host1, Host2, NameIsUnique, Entry, SubEnt;
	struct Map *MapP;
	struct Map *HostMapP;
	struct Host *HostP;

	char *cptr;

	/*
	 ** We have been sent a new table to install. We need to break
	 ** it down into little bits and spread it around a bit to see
	 ** what we have got.
	 */
	/*
	 ** Things to check:
	 ** (things marked 'xx' aren't checked any more!)
	 ** (1) That there are no booted Hosts/RTAs out there.
	 ** (2) That the names are properly formed
	 ** (3) That blank entries really are.
	 ** xx (4)      That hosts mentioned in the table actually exist. xx
	 ** (5) That the IDs are unique (per host).
	 ** (6) That host IDs are zero
	 ** (7) That port numbers are valid
	 ** (8) That port numbers aren't duplicated
	 ** (9) That names aren't duplicated
	 ** xx (10) That hosts that actually exist are mentioned in the table. xx
	 */
	rio_dprintk(RIO_DEBUG_TABLE, "RIONewTable: entering(1)\n");
	if (p->RIOSystemUp) {	/* (1) */
		p->RIOError.Error = HOST_HAS_ALREADY_BEEN_BOOTED;
		return -EBUSY;
	}

	p->RIOError.Error = NOTHING_WRONG_AT_ALL;
	p->RIOError.Entry = -1;
	p->RIOError.Other = -1;

	for (Entry = 0; Entry < TOTAL_MAP_ENTRIES; Entry++) {
		MapP = &p->RIOConnectTable[Entry];
		if ((MapP->Flags & RTA16_SECOND_SLOT) == 0) {
			rio_dprintk(RIO_DEBUG_TABLE, "RIONewTable: entering(2)\n");
			cptr = MapP->Name;	/* (2) */
			cptr[MAX_NAME_LEN - 1] = '\0';
			if (cptr[0] == '\0') {
				memcpy(MapP->Name, MapP->RtaUniqueNum ? "RTA	NN" : "HOST NN", 8);
				MapP->Name[5] = '0' + Entry / 10;
				MapP->Name[6] = '0' + Entry % 10;
			}
			while (*cptr) {
				if (*cptr < ' ' || *cptr > '~') {
					p->RIOError.Error = BAD_CHARACTER_IN_NAME;
					p->RIOError.Entry = Entry;
					return -ENXIO;
				}
				cptr++;
			}
		}

		/*
		 ** If the entry saved was a tentative entry then just forget
		 ** about it.
		 */
		if (MapP->Flags & SLOT_TENTATIVE) {
			MapP->HostUniqueNum = 0;
			MapP->RtaUniqueNum = 0;
			continue;
		}

		rio_dprintk(RIO_DEBUG_TABLE, "RIONewTable: entering(3)\n");
		if (!MapP->RtaUniqueNum && !MapP->HostUniqueNum) {	/* (3) */
			if (MapP->ID || MapP->SysPort || MapP->Flags) {
				rio_dprintk(RIO_DEBUG_TABLE, "%s pretending to be empty but isn't\n", MapP->Name);
				p->RIOError.Error = TABLE_ENTRY_ISNT_PROPERLY_NULL;
				p->RIOError.Entry = Entry;
				return -ENXIO;
			}
			rio_dprintk(RIO_DEBUG_TABLE, "!RIO: Daemon: test (3) passes\n");
			continue;
		}

		rio_dprintk(RIO_DEBUG_TABLE, "RIONewTable: entering(4)\n");
		for (Host = 0; Host < p->RIONumHosts; Host++) {	/* (4) */
			if (p->RIOHosts[Host].UniqueNum == MapP->HostUniqueNum) {
				HostP = &p->RIOHosts[Host];
				/*
				 ** having done the lookup, we don't really want to do
				 ** it again, so hang the host number in a safe place
				 */
				MapP->Topology[0].Unit = Host;
				break;
			}
		}

		if (Host >= p->RIONumHosts) {
			rio_dprintk(RIO_DEBUG_TABLE, "RTA %s has unknown host unique number 0x%x\n", MapP->Name, MapP->HostUniqueNum);
			MapP->HostUniqueNum = 0;
			/* MapP->RtaUniqueNum   = 0; */
			/* MapP->ID                     = 0; */
			/* MapP->Flags           = 0; */
			/* MapP->SysPort                 = 0; */
			/* MapP->Name[0]                 = 0; */
			continue;
		}

		rio_dprintk(RIO_DEBUG_TABLE, "RIONewTable: entering(5)\n");
		if (MapP->RtaUniqueNum) {	/* (5) */
			if (!MapP->ID) {
				rio_dprintk(RIO_DEBUG_TABLE, "RIO: RTA %s has been allocated an ID of zero!\n", MapP->Name);
				p->RIOError.Error = ZERO_RTA_ID;
				p->RIOError.Entry = Entry;
				return -ENXIO;
			}
			if (MapP->ID > MAX_RUP) {
				rio_dprintk(RIO_DEBUG_TABLE, "RIO: RTA %s has been allocated an invalid ID %d\n", MapP->Name, MapP->ID);
				p->RIOError.Error = ID_NUMBER_OUT_OF_RANGE;
				p->RIOError.Entry = Entry;
				return -ENXIO;
			}
			for (SubEnt = 0; SubEnt < Entry; SubEnt++) {
				if (MapP->HostUniqueNum == p->RIOConnectTable[SubEnt].HostUniqueNum && MapP->ID == p->RIOConnectTable[SubEnt].ID) {
					rio_dprintk(RIO_DEBUG_TABLE, "Dupl. ID number allocated to RTA %s and RTA %s\n", MapP->Name, p->RIOConnectTable[SubEnt].Name);
					p->RIOError.Error = DUPLICATED_RTA_ID;
					p->RIOError.Entry = Entry;
					p->RIOError.Other = SubEnt;
					return -ENXIO;
				}
				/*
				 ** If the RtaUniqueNum is the same, it may be looking at both
				 ** entries for a 16 port RTA, so check the ids
				 */
				if ((MapP->RtaUniqueNum == p->RIOConnectTable[SubEnt].RtaUniqueNum)
				    && (MapP->ID2 != p->RIOConnectTable[SubEnt].ID)) {
					rio_dprintk(RIO_DEBUG_TABLE, "RTA %s has duplicate unique number\n", MapP->Name);
					rio_dprintk(RIO_DEBUG_TABLE, "RTA %s has duplicate unique number\n", p->RIOConnectTable[SubEnt].Name);
					p->RIOError.Error = DUPLICATE_UNIQUE_NUMBER;
					p->RIOError.Entry = Entry;
					p->RIOError.Other = SubEnt;
					return -ENXIO;
				}
			}
			rio_dprintk(RIO_DEBUG_TABLE, "RIONewTable: entering(7a)\n");
			/* (7a) */
			if ((MapP->SysPort != NO_PORT) && (MapP->SysPort % PORTS_PER_RTA)) {
				rio_dprintk(RIO_DEBUG_TABLE, "TTY Port number %d-RTA %s is not a multiple of %d!\n", (int) MapP->SysPort, MapP->Name, PORTS_PER_RTA);
				p->RIOError.Error = TTY_NUMBER_OUT_OF_RANGE;
				p->RIOError.Entry = Entry;
				return -ENXIO;
			}
			rio_dprintk(RIO_DEBUG_TABLE, "RIONewTable: entering(7b)\n");
			/* (7b) */
			if ((MapP->SysPort != NO_PORT) && (MapP->SysPort >= RIO_PORTS)) {
				rio_dprintk(RIO_DEBUG_TABLE, "TTY Port number %d for RTA %s is too big\n", (int) MapP->SysPort, MapP->Name);
				p->RIOError.Error = TTY_NUMBER_OUT_OF_RANGE;
				p->RIOError.Entry = Entry;
				return -ENXIO;
			}
			for (SubEnt = 0; SubEnt < Entry; SubEnt++) {
				if (p->RIOConnectTable[SubEnt].Flags & RTA16_SECOND_SLOT)
					continue;
				if (p->RIOConnectTable[SubEnt].RtaUniqueNum) {
					rio_dprintk(RIO_DEBUG_TABLE, "RIONewTable: entering(8)\n");
					/* (8) */
					if ((MapP->SysPort != NO_PORT) && (MapP->SysPort == p->RIOConnectTable[SubEnt].SysPort)) {
						rio_dprintk(RIO_DEBUG_TABLE, "RTA %s:same TTY port # as RTA %s (%d)\n", MapP->Name, p->RIOConnectTable[SubEnt].Name, (int) MapP->SysPort);
						p->RIOError.Error = TTY_NUMBER_IN_USE;
						p->RIOError.Entry = Entry;
						p->RIOError.Other = SubEnt;
						return -ENXIO;
					}
					rio_dprintk(RIO_DEBUG_TABLE, "RIONewTable: entering(9)\n");
					if (strcmp(MapP->Name, p->RIOConnectTable[SubEnt].Name) == 0 && !(MapP->Flags & RTA16_SECOND_SLOT)) {	/* (9) */
						rio_dprintk(RIO_DEBUG_TABLE, "RTA name %s used twice\n", MapP->Name);
						p->RIOError.Error = NAME_USED_TWICE;
						p->RIOError.Entry = Entry;
						p->RIOError.Other = SubEnt;
						return -ENXIO;
					}
				}
			}
		} else {	/* (6) */
			rio_dprintk(RIO_DEBUG_TABLE, "RIONewTable: entering(6)\n");
			if (MapP->ID) {
				rio_dprintk(RIO_DEBUG_TABLE, "RIO:HOST %s has been allocated ID that isn't zero!\n", MapP->Name);
				p->RIOError.Error = HOST_ID_NOT_ZERO;
				p->RIOError.Entry = Entry;
				return -ENXIO;
			}
			if (MapP->SysPort != NO_PORT) {
				rio_dprintk(RIO_DEBUG_TABLE, "RIO: HOST %s has been allocated port numbers!\n", MapP->Name);
				p->RIOError.Error = HOST_SYSPORT_BAD;
				p->RIOError.Entry = Entry;
				return -ENXIO;
			}
		}
	}

	/*
	 ** wow! if we get here then it's a goody!
	 */

	/*
	 ** Zero the (old) entries for each host...
	 */
	for (Host = 0; Host < RIO_HOSTS; Host++) {
		for (Entry = 0; Entry < MAX_RUP; Entry++) {
			memset(&p->RIOHosts[Host].Mapping[Entry], 0, sizeof(struct Map));
		}
		memset(&p->RIOHosts[Host].Name[0], 0, sizeof(p->RIOHosts[Host].Name));
	}

	/*
	 ** Copy in the new table entries
	 */
	for (Entry = 0; Entry < TOTAL_MAP_ENTRIES; Entry++) {
		rio_dprintk(RIO_DEBUG_TABLE, "RIONewTable: Copy table for Host entry %d\n", Entry);
		MapP = &p->RIOConnectTable[Entry];

		/*
		 ** Now, if it is an empty slot ignore it!
		 */
		if (MapP->HostUniqueNum == 0)
			continue;

		/*
		 ** we saved the host number earlier, so grab it back
		 */
		HostP = &p->RIOHosts[MapP->Topology[0].Unit];

		/*
		 ** If it is a host, then we only need to fill in the name field.
		 */
		if (MapP->ID == 0) {
			rio_dprintk(RIO_DEBUG_TABLE, "Host entry found. Name %s\n", MapP->Name);
			memcpy(HostP->Name, MapP->Name, MAX_NAME_LEN);
			continue;
		}

		/*
		 ** Its an RTA entry, so fill in the host mapping entries for it
		 ** and the port mapping entries. Notice that entry zero is for
		 ** ID one.
		 */
		HostMapP = &HostP->Mapping[MapP->ID - 1];

		if (MapP->Flags & SLOT_IN_USE) {
			rio_dprintk(RIO_DEBUG_TABLE, "Rta entry found. Name %s\n", MapP->Name);
			/*
			 ** structure assign, then sort out the bits we shouldn't have done
			 */
			*HostMapP = *MapP;

			HostMapP->Flags = SLOT_IN_USE;
			if (MapP->Flags & RTA16_SECOND_SLOT)
				HostMapP->Flags |= RTA16_SECOND_SLOT;

			RIOReMapPorts(p, HostP, HostMapP);
		} else {
			rio_dprintk(RIO_DEBUG_TABLE, "TENTATIVE Rta entry found. Name %s\n", MapP->Name);
		}
	}

	for (Entry = 0; Entry < TOTAL_MAP_ENTRIES; Entry++) {
		p->RIOSavedTable[Entry] = p->RIOConnectTable[Entry];
	}

	for (Host = 0; Host < p->RIONumHosts; Host++) {
		for (SubEnt = 0; SubEnt < LINKS_PER_UNIT; SubEnt++) {
			p->RIOHosts[Host].Topology[SubEnt].Unit = ROUTE_DISCONNECT;
			p->RIOHosts[Host].Topology[SubEnt].Link = NO_LINK;
		}
		for (Entry = 0; Entry < MAX_RUP; Entry++) {
			for (SubEnt = 0; SubEnt < LINKS_PER_UNIT; SubEnt++) {
				p->RIOHosts[Host].Mapping[Entry].Topology[SubEnt].Unit = ROUTE_DISCONNECT;
				p->RIOHosts[Host].Mapping[Entry].Topology[SubEnt].Link = NO_LINK;
			}
		}
		if (!p->RIOHosts[Host].Name[0]) {
			memcpy(p->RIOHosts[Host].Name, "HOST 1", 7);
			p->RIOHosts[Host].Name[5] += Host;
		}
		/*
		 ** Check that default name assigned is unique.
		 */
		Host1 = Host;
		NameIsUnique = 0;
		while (!NameIsUnique) {
			NameIsUnique = 1;
			for (Host2 = 0; Host2 < p->RIONumHosts; Host2++) {
				if (Host2 == Host)
					continue;
				if (strcmp(p->RIOHosts[Host].Name, p->RIOHosts[Host2].Name)
				    == 0) {
					NameIsUnique = 0;
					Host1++;
					if (Host1 >= p->RIONumHosts)
						Host1 = 0;
					p->RIOHosts[Host].Name[5] = '1' + Host1;
				}
			}
		}
		/*
		 ** Rename host if name already used.
		 */
		if (Host1 != Host) {
			rio_dprintk(RIO_DEBUG_TABLE, "Default name %s already used\n", p->RIOHosts[Host].Name);
			memcpy(p->RIOHosts[Host].Name, "HOST 1", 7);
			p->RIOHosts[Host].Name[5] += Host1;
		}
		rio_dprintk(RIO_DEBUG_TABLE, "Assigning default name %s\n", p->RIOHosts[Host].Name);
	}
	return 0;
}

/*
** User process needs the config table - build it from first
** principles.
**
*	FIXME: SMP locking
*/
int RIOApel(struct rio_info *p)
{
	int Host;
	int link;
	int Rup;
	int Next = 0;
	struct Map *MapP;
	struct Host *HostP;
	unsigned long flags;

	rio_dprintk(RIO_DEBUG_TABLE, "Generating a table to return to config.rio\n");

	memset(&p->RIOConnectTable[0], 0, sizeof(struct Map) * TOTAL_MAP_ENTRIES);

	for (Host = 0; Host < RIO_HOSTS; Host++) {
		rio_dprintk(RIO_DEBUG_TABLE, "Processing host %d\n", Host);
		HostP = &p->RIOHosts[Host];
		rio_spin_lock_irqsave(&HostP->HostLock, flags);

		MapP = &p->RIOConnectTable[Next++];
		MapP->HostUniqueNum = HostP->UniqueNum;
		if ((HostP->Flags & RUN_STATE) != RC_RUNNING)
			continue;
		MapP->RtaUniqueNum = 0;
		MapP->ID = 0;
		MapP->Flags = SLOT_IN_USE;
		MapP->SysPort = NO_PORT;
		for (link = 0; link < LINKS_PER_UNIT; link++)
			MapP->Topology[link] = HostP->Topology[link];
		memcpy(MapP->Name, HostP->Name, MAX_NAME_LEN);
		for (Rup = 0; Rup < MAX_RUP; Rup++) {
			if (HostP->Mapping[Rup].Flags & (SLOT_IN_USE | SLOT_TENTATIVE)) {
				p->RIOConnectTable[Next] = HostP->Mapping[Rup];
				if (HostP->Mapping[Rup].Flags & SLOT_IN_USE)
					p->RIOConnectTable[Next].Flags |= SLOT_IN_USE;
				if (HostP->Mapping[Rup].Flags & SLOT_TENTATIVE)
					p->RIOConnectTable[Next].Flags |= SLOT_TENTATIVE;
				if (HostP->Mapping[Rup].Flags & RTA16_SECOND_SLOT)
					p->RIOConnectTable[Next].Flags |= RTA16_SECOND_SLOT;
				Next++;
			}
		}
		rio_spin_unlock_irqrestore(&HostP->HostLock, flags);
	}
	return 0;
}

/*
** config.rio has taken a dislike to one of the gross maps entries.
** if the entry is suitably inactive, then we can gob on it and remove
** it from the table.
*/
int RIODeleteRta(struct rio_info *p, struct Map *MapP)
{
	int host, entry, port, link;
	int SysPort;
	struct Host *HostP;
	struct Map *HostMapP;
	struct Port *PortP;
	int work_done = 0;
	unsigned long lock_flags, sem_flags;

	rio_dprintk(RIO_DEBUG_TABLE, "Delete entry on host %x, rta %x\n", MapP->HostUniqueNum, MapP->RtaUniqueNum);

	for (host = 0; host < p->RIONumHosts; host++) {
		HostP = &p->RIOHosts[host];

		rio_spin_lock_irqsave(&HostP->HostLock, lock_flags);

		if ((HostP->Flags & RUN_STATE) != RC_RUNNING) {
			rio_spin_unlock_irqrestore(&HostP->HostLock, lock_flags);
			continue;
		}

		for (entry = 0; entry < MAX_RUP; entry++) {
			if (MapP->RtaUniqueNum == HostP->Mapping[entry].RtaUniqueNum) {
				HostMapP = &HostP->Mapping[entry];
				rio_dprintk(RIO_DEBUG_TABLE, "Found entry offset %d on host %s\n", entry, HostP->Name);

				/*
				 ** Check all four links of the unit are disconnected
				 */
				for (link = 0; link < LINKS_PER_UNIT; link++) {
					if (HostMapP->Topology[link].Unit != ROUTE_DISCONNECT) {
						rio_dprintk(RIO_DEBUG_TABLE, "Entry is in use and cannot be deleted!\n");
						p->RIOError.Error = UNIT_IS_IN_USE;
						rio_spin_unlock_irqrestore(&HostP->HostLock, lock_flags);
						return -EBUSY;
					}
				}
				/*
				 ** Slot has been allocated, BUT not booted/routed/
				 ** connected/selected or anything else-ed
				 */
				SysPort = HostMapP->SysPort;

				if (SysPort != NO_PORT) {
					for (port = SysPort; port < SysPort + PORTS_PER_RTA; port++) {
						PortP = p->RIOPortp[port];
						rio_dprintk(RIO_DEBUG_TABLE, "Unmap port\n");

						rio_spin_lock_irqsave(&PortP->portSem, sem_flags);

						PortP->Mapped = 0;

						if (PortP->State & (RIO_MOPEN | RIO_LOPEN)) {

							rio_dprintk(RIO_DEBUG_TABLE, "Gob on port\n");
							PortP->TxBufferIn = PortP->TxBufferOut = 0;
							/* What should I do 
							   wakeup( &PortP->TxBufferIn );
							   wakeup( &PortP->TxBufferOut);
							 */
							PortP->InUse = NOT_INUSE;
							/* What should I do 
							   wakeup( &PortP->InUse );
							   signal(PortP->TtyP->t_pgrp,SIGKILL);
							   ttyflush(PortP->TtyP,(FREAD|FWRITE));
							 */
							PortP->State |= RIO_CLOSING | RIO_DELETED;
						}

						/*
						 ** For the second slot of a 16 port RTA, the
						 ** driver needs to reset the changes made to
						 ** the phb to port mappings in RIORouteRup.
						 */
						if (PortP->SecondBlock) {
							u16 dest_unit = HostMapP->ID;
							u16 dest_port = port - SysPort;
							u16 *TxPktP;
							struct PKT *Pkt;

							for (TxPktP = PortP->TxStart; TxPktP <= PortP->TxEnd; TxPktP++) {
								/*
								 ** *TxPktP is the pointer to the
								 ** transmit packet on the host card.
								 ** This needs to be translated into
								 ** a 32 bit pointer so it can be
								 ** accessed from the driver.
								 */
								Pkt = (struct PKT *) RIO_PTR(HostP->Caddr, readw(&*TxPktP));
								rio_dprintk(RIO_DEBUG_TABLE, "Tx packet (%x) destination: Old %x:%x New %x:%x\n", *TxPktP, Pkt->dest_unit, Pkt->dest_port, dest_unit, dest_port);
								writew(dest_unit, &Pkt->dest_unit);
								writew(dest_port, &Pkt->dest_port);
							}
							rio_dprintk(RIO_DEBUG_TABLE, "Port %d phb destination: Old %x:%x New %x:%x\n", port, PortP->PhbP->destination & 0xff, (PortP->PhbP->destination >> 8) & 0xff, dest_unit, dest_port);
							writew(dest_unit + (dest_port << 8), &PortP->PhbP->destination);
						}
						rio_spin_unlock_irqrestore(&PortP->portSem, sem_flags);
					}
				}
				rio_dprintk(RIO_DEBUG_TABLE, "Entry nulled.\n");
				memset(HostMapP, 0, sizeof(struct Map));
				work_done++;
			}
		}
		rio_spin_unlock_irqrestore(&HostP->HostLock, lock_flags);
	}

	/* XXXXX lock me up */
	for (entry = 0; entry < TOTAL_MAP_ENTRIES; entry++) {
		if (p->RIOSavedTable[entry].RtaUniqueNum == MapP->RtaUniqueNum) {
			memset(&p->RIOSavedTable[entry], 0, sizeof(struct Map));
			work_done++;
		}
		if (p->RIOConnectTable[entry].RtaUniqueNum == MapP->RtaUniqueNum) {
			memset(&p->RIOConnectTable[entry], 0, sizeof(struct Map));
			work_done++;
		}
	}
	if (work_done)
		return 0;

	rio_dprintk(RIO_DEBUG_TABLE, "Couldn't find entry to be deleted\n");
	p->RIOError.Error = COULDNT_FIND_ENTRY;
	return -ENXIO;
}

int RIOAssignRta(struct rio_info *p, struct Map *MapP)
{
	int host;
	struct Map *HostMapP;
	char *sptr;
	int link;


	rio_dprintk(RIO_DEBUG_TABLE, "Assign entry on host %x, rta %x, ID %d, Sysport %d\n", MapP->HostUniqueNum, MapP->RtaUniqueNum, MapP->ID, (int) MapP->SysPort);

	if ((MapP->ID != (u16) - 1) && ((int) MapP->ID < (int) 1 || (int) MapP->ID > MAX_RUP)) {
		rio_dprintk(RIO_DEBUG_TABLE, "Bad ID in map entry!\n");
		p->RIOError.Error = ID_NUMBER_OUT_OF_RANGE;
		return -EINVAL;
	}
	if (MapP->RtaUniqueNum == 0) {
		rio_dprintk(RIO_DEBUG_TABLE, "Rta Unique number zero!\n");
		p->RIOError.Error = RTA_UNIQUE_NUMBER_ZERO;
		return -EINVAL;
	}
	if ((MapP->SysPort != NO_PORT) && (MapP->SysPort % PORTS_PER_RTA)) {
		rio_dprintk(RIO_DEBUG_TABLE, "Port %d not multiple of %d!\n", (int) MapP->SysPort, PORTS_PER_RTA);
		p->RIOError.Error = TTY_NUMBER_OUT_OF_RANGE;
		return -EINVAL;
	}
	if ((MapP->SysPort != NO_PORT) && (MapP->SysPort >= RIO_PORTS)) {
		rio_dprintk(RIO_DEBUG_TABLE, "Port %d not valid!\n", (int) MapP->SysPort);
		p->RIOError.Error = TTY_NUMBER_OUT_OF_RANGE;
		return -EINVAL;
	}

	/*
	 ** Copy the name across to the map entry.
	 */
	MapP->Name[MAX_NAME_LEN - 1] = '\0';
	sptr = MapP->Name;
	while (*sptr) {
		if (*sptr < ' ' || *sptr > '~') {
			rio_dprintk(RIO_DEBUG_TABLE, "Name entry contains non-printing characters!\n");
			p->RIOError.Error = BAD_CHARACTER_IN_NAME;
			return -EINVAL;
		}
		sptr++;
	}

	for (host = 0; host < p->RIONumHosts; host++) {
		if (MapP->HostUniqueNum == p->RIOHosts[host].UniqueNum) {
			if ((p->RIOHosts[host].Flags & RUN_STATE) != RC_RUNNING) {
				p->RIOError.Error = HOST_NOT_RUNNING;
				return -ENXIO;
			}

			/*
			 ** Now we have a host we need to allocate an ID
			 ** if the entry does not already have one.
			 */
			if (MapP->ID == (u16) - 1) {
				int nNewID;

				rio_dprintk(RIO_DEBUG_TABLE, "Attempting to get a new ID for rta \"%s\"\n", MapP->Name);
				/*
				 ** The idea here is to allow RTA's to be assigned
				 ** before they actually appear on the network.
				 ** This allows the addition of RTA's without having
				 ** to plug them in.
				 ** What we do is:
				 **  - Find a free ID and allocate it to the RTA.
				 **  - If this map entry is the second half of a
				 **    16 port entry then find the other half and
				 **    make sure the 2 cross reference each other.
				 */
				if (RIOFindFreeID(p, &p->RIOHosts[host], &nNewID, NULL) != 0) {
					p->RIOError.Error = COULDNT_FIND_ENTRY;
					return -EBUSY;
				}
				MapP->ID = (u16) nNewID + 1;
				rio_dprintk(RIO_DEBUG_TABLE, "Allocated ID %d for this new RTA.\n", MapP->ID);
				HostMapP = &p->RIOHosts[host].Mapping[nNewID];
				HostMapP->RtaUniqueNum = MapP->RtaUniqueNum;
				HostMapP->HostUniqueNum = MapP->HostUniqueNum;
				HostMapP->ID = MapP->ID;
				for (link = 0; link < LINKS_PER_UNIT; link++) {
					HostMapP->Topology[link].Unit = ROUTE_DISCONNECT;
					HostMapP->Topology[link].Link = NO_LINK;
				}
				if (MapP->Flags & RTA16_SECOND_SLOT) {
					int unit;

					for (unit = 0; unit < MAX_RUP; unit++)
						if (p->RIOHosts[host].Mapping[unit].RtaUniqueNum == MapP->RtaUniqueNum)
							break;
					if (unit == MAX_RUP) {
						p->RIOError.Error = COULDNT_FIND_ENTRY;
						return -EBUSY;
					}
					HostMapP->Flags |= RTA16_SECOND_SLOT;
					HostMapP->ID2 = MapP->ID2 = p->RIOHosts[host].Mapping[unit].ID;
					p->RIOHosts[host].Mapping[unit].ID2 = MapP->ID;
					rio_dprintk(RIO_DEBUG_TABLE, "Cross referenced id %d to ID %d.\n", MapP->ID, p->RIOHosts[host].Mapping[unit].ID);
				}
			}

			HostMapP = &p->RIOHosts[host].Mapping[MapP->ID - 1];

			if (HostMapP->Flags & SLOT_IN_USE) {
				rio_dprintk(RIO_DEBUG_TABLE, "Map table slot for ID %d is already in use.\n", MapP->ID);
				p->RIOError.Error = ID_ALREADY_IN_USE;
				return -EBUSY;
			}

			/*
			 ** Assign the sys ports and the name, and mark the slot as
			 ** being in use.
			 */
			HostMapP->SysPort = MapP->SysPort;
			if ((MapP->Flags & RTA16_SECOND_SLOT) == 0)
				memcpy(HostMapP->Name, MapP->Name, MAX_NAME_LEN);
			HostMapP->Flags = SLOT_IN_USE | RTA_BOOTED;
#ifdef NEED_TO_FIX
			RIO_SV_BROADCAST(p->RIOHosts[host].svFlags[MapP->ID - 1]);
#endif
			if (MapP->Flags & RTA16_SECOND_SLOT)
				HostMapP->Flags |= RTA16_SECOND_SLOT;

			RIOReMapPorts(p, &p->RIOHosts[host], HostMapP);
			/*
			 ** Adjust 2nd block of 8 phbs
			 */
			if (MapP->Flags & RTA16_SECOND_SLOT)
				RIOFixPhbs(p, &p->RIOHosts[host], HostMapP->ID - 1);

			if (HostMapP->SysPort != NO_PORT) {
				if (HostMapP->SysPort < p->RIOFirstPortsBooted)
					p->RIOFirstPortsBooted = HostMapP->SysPort;
				if (HostMapP->SysPort > p->RIOLastPortsBooted)
					p->RIOLastPortsBooted = HostMapP->SysPort;
			}
			if (MapP->Flags & RTA16_SECOND_SLOT)
				rio_dprintk(RIO_DEBUG_TABLE, "Second map of RTA %s added to configuration\n", p->RIOHosts[host].Mapping[MapP->ID2 - 1].Name);
			else
				rio_dprintk(RIO_DEBUG_TABLE, "RTA %s added to configuration\n", MapP->Name);
			return 0;
		}
	}
	p->RIOError.Error = UNKNOWN_HOST_NUMBER;
	rio_dprintk(RIO_DEBUG_TABLE, "Unknown host %x\n", MapP->HostUniqueNum);
	return -ENXIO;
}


int RIOReMapPorts(struct rio_info *p, struct Host *HostP, struct Map *HostMapP)
{
	struct Port *PortP;
	unsigned int SubEnt;
	unsigned int HostPort;
	unsigned int SysPort;
	u16 RtaType;
	unsigned long flags;

	rio_dprintk(RIO_DEBUG_TABLE, "Mapping sysport %d to id %d\n", (int) HostMapP->SysPort, HostMapP->ID);

	/*
	 ** We need to tell the UnixRups which sysport the rup corresponds to
	 */
	HostP->UnixRups[HostMapP->ID - 1].BaseSysPort = HostMapP->SysPort;

	if (HostMapP->SysPort == NO_PORT)
		return (0);

	RtaType = GetUnitType(HostMapP->RtaUniqueNum);
	rio_dprintk(RIO_DEBUG_TABLE, "Mapping sysport %d-%d\n", (int) HostMapP->SysPort, (int) HostMapP->SysPort + PORTS_PER_RTA - 1);

	/*
	 ** now map each of its eight ports
	 */
	for (SubEnt = 0; SubEnt < PORTS_PER_RTA; SubEnt++) {
		rio_dprintk(RIO_DEBUG_TABLE, "subent = %d, HostMapP->SysPort = %d\n", SubEnt, (int) HostMapP->SysPort);
		SysPort = HostMapP->SysPort + SubEnt;	/* portnumber within system */
		/* portnumber on host */

		HostPort = (HostMapP->ID - 1) * PORTS_PER_RTA + SubEnt;

		rio_dprintk(RIO_DEBUG_TABLE, "c1 p = %p, p->rioPortp = %p\n", p, p->RIOPortp);
		PortP = p->RIOPortp[SysPort];
		rio_dprintk(RIO_DEBUG_TABLE, "Map port\n");

		/*
		 ** Point at all the real neat data structures
		 */
		rio_spin_lock_irqsave(&PortP->portSem, flags);
		PortP->HostP = HostP;
		PortP->Caddr = HostP->Caddr;

		/*
		 ** The PhbP cannot be filled in yet
		 ** unless the host has been booted
		 */
		if ((HostP->Flags & RUN_STATE) == RC_RUNNING) {
			struct PHB *PhbP = PortP->PhbP = &HostP->PhbP[HostPort];
			PortP->TxAdd = (u16 *) RIO_PTR(HostP->Caddr, readw(&PhbP->tx_add));
			PortP->TxStart = (u16 *) RIO_PTR(HostP->Caddr, readw(&PhbP->tx_start));
			PortP->TxEnd = (u16 *) RIO_PTR(HostP->Caddr, readw(&PhbP->tx_end));
			PortP->RxRemove = (u16 *) RIO_PTR(HostP->Caddr, readw(&PhbP->rx_remove));
			PortP->RxStart = (u16 *) RIO_PTR(HostP->Caddr, readw(&PhbP->rx_start));
			PortP->RxEnd = (u16 *) RIO_PTR(HostP->Caddr, readw(&PhbP->rx_end));
		} else
			PortP->PhbP = NULL;

		/*
		 ** port related flags
		 */
		PortP->HostPort = HostPort;
		/*
		 ** For each part of a 16 port RTA, RupNum is ID - 1.
		 */
		PortP->RupNum = HostMapP->ID - 1;
		if (HostMapP->Flags & RTA16_SECOND_SLOT) {
			PortP->ID2 = HostMapP->ID2 - 1;
			PortP->SecondBlock = 1;
		} else {
			PortP->ID2 = 0;
			PortP->SecondBlock = 0;
		}
		PortP->RtaUniqueNum = HostMapP->RtaUniqueNum;

		/*
		 ** If the port was already mapped then thats all we need to do.
		 */
		if (PortP->Mapped) {
			rio_spin_unlock_irqrestore(&PortP->portSem, flags);
			continue;
		} else
			HostMapP->Flags &= ~RTA_NEWBOOT;

		PortP->State = 0;
		PortP->Config = 0;
		/*
		 ** Check out the module type - if it is special (read only etc.)
		 ** then we need to set flags in the PortP->Config.
		 ** Note: For 16 port RTA, all ports are of the same type.
		 */
		if (RtaType == TYPE_RTA16) {
			PortP->Config |= p->RIOModuleTypes[HostP->UnixRups[HostMapP->ID - 1].ModTypes].Flags[SubEnt % PORTS_PER_MODULE];
		} else {
			if (SubEnt < PORTS_PER_MODULE)
				PortP->Config |= p->RIOModuleTypes[LONYBLE(HostP->UnixRups[HostMapP->ID - 1].ModTypes)].Flags[SubEnt % PORTS_PER_MODULE];
			else
				PortP->Config |= p->RIOModuleTypes[HINYBLE(HostP->UnixRups[HostMapP->ID - 1].ModTypes)].Flags[SubEnt % PORTS_PER_MODULE];
		}

		/*
		 ** more port related flags
		 */
		PortP->PortState = 0;
		PortP->ModemLines = 0;
		PortP->ModemState = 0;
		PortP->CookMode = COOK_WELL;
		PortP->ParamSem = 0;
		PortP->FlushCmdBodge = 0;
		PortP->WflushFlag = 0;
		PortP->MagicFlags = 0;
		PortP->Lock = 0;
		PortP->Store = 0;
		PortP->FirstOpen = 1;

		/*
		 ** Buffers 'n things
		 */
		PortP->RxDataStart = 0;
		PortP->Cor2Copy = 0;
		PortP->Name = &HostMapP->Name[0];
		PortP->statsGather = 0;
		PortP->txchars = 0;
		PortP->rxchars = 0;
		PortP->opens = 0;
		PortP->closes = 0;
		PortP->ioctls = 0;
		if (PortP->TxRingBuffer)
			memset(PortP->TxRingBuffer, 0, p->RIOBufferSize);
		else if (p->RIOBufferSize) {
			PortP->TxRingBuffer = kmalloc(p->RIOBufferSize, GFP_KERNEL);
			memset(PortP->TxRingBuffer, 0, p->RIOBufferSize);
		}
		PortP->TxBufferOut = 0;
		PortP->TxBufferIn = 0;
		PortP->Debug = 0;
		/*
		 ** LastRxTgl stores the state of the rx toggle bit for this
		 ** port, to be compared with the state of the next pkt received.
		 ** If the same, we have received the same rx pkt from the RTA
		 ** twice. Initialise to a value not equal to PHB_RX_TGL or 0.
		 */
		PortP->LastRxTgl = ~(u8) PHB_RX_TGL;

		/*
		 ** and mark the port as usable
		 */
		PortP->Mapped = 1;
		rio_spin_unlock_irqrestore(&PortP->portSem, flags);
	}
	if (HostMapP->SysPort < p->RIOFirstPortsMapped)
		p->RIOFirstPortsMapped = HostMapP->SysPort;
	if (HostMapP->SysPort > p->RIOLastPortsMapped)
		p->RIOLastPortsMapped = HostMapP->SysPort;

	return 0;
}

int RIOChangeName(struct rio_info *p, struct Map *MapP)
{
	int host;
	struct Map *HostMapP;
	char *sptr;

	rio_dprintk(RIO_DEBUG_TABLE, "Change name entry on host %x, rta %x, ID %d, Sysport %d\n", MapP->HostUniqueNum, MapP->RtaUniqueNum, MapP->ID, (int) MapP->SysPort);

	if (MapP->ID > MAX_RUP) {
		rio_dprintk(RIO_DEBUG_TABLE, "Bad ID in map entry!\n");
		p->RIOError.Error = ID_NUMBER_OUT_OF_RANGE;
		return -EINVAL;
	}

	MapP->Name[MAX_NAME_LEN - 1] = '\0';
	sptr = MapP->Name;

	while (*sptr) {
		if (*sptr < ' ' || *sptr > '~') {
			rio_dprintk(RIO_DEBUG_TABLE, "Name entry contains non-printing characters!\n");
			p->RIOError.Error = BAD_CHARACTER_IN_NAME;
			return -EINVAL;
		}
		sptr++;
	}

	for (host = 0; host < p->RIONumHosts; host++) {
		if (MapP->HostUniqueNum == p->RIOHosts[host].UniqueNum) {
			if ((p->RIOHosts[host].Flags & RUN_STATE) != RC_RUNNING) {
				p->RIOError.Error = HOST_NOT_RUNNING;
				return -ENXIO;
			}
			if (MapP->ID == 0) {
				memcpy(p->RIOHosts[host].Name, MapP->Name, MAX_NAME_LEN);
				return 0;
			}

			HostMapP = &p->RIOHosts[host].Mapping[MapP->ID - 1];

			if (HostMapP->RtaUniqueNum != MapP->RtaUniqueNum) {
				p->RIOError.Error = RTA_NUMBER_WRONG;
				return -ENXIO;
			}
			memcpy(HostMapP->Name, MapP->Name, MAX_NAME_LEN);
			return 0;
		}
	}
	p->RIOError.Error = UNKNOWN_HOST_NUMBER;
	rio_dprintk(RIO_DEBUG_TABLE, "Unknown host %x\n", MapP->HostUniqueNum);
	return -ENXIO;
}
