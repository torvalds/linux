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
**	Module		: rioctrl.c
**	SID		: 1.3
**	Last Modified	: 11/6/98 10:33:42
**	Retrieved	: 11/6/98 10:33:49
**
**  ident @(#)rioctrl.c	1.3
**
** -----------------------------------------------------------------------------
*/

#include <linux/module.h>
#include <linux/sched.h>
#include <linux/slab.h>
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


static struct LpbReq LpbReq;
static struct RupReq RupReq;
static struct PortReq PortReq;
static struct HostReq HostReq;	/* oh really?  global?  and no locking? */
static struct HostDpRam HostDpRam;
static struct DebugCtrl DebugCtrl;
static struct Map MapEnt;
static struct PortSetup PortSetup;
static struct DownLoad DownLoad;
static struct SendPack SendPack;
/* static struct StreamInfo	StreamInfo; */
/* static char modemtable[RIO_PORTS]; */
static struct SpecialRupCmd SpecialRupCmd;
static struct PortParams PortParams;
static struct portStats portStats;

static struct SubCmdStruct {
	ushort Host;
	ushort Rup;
	ushort Port;
	ushort Addr;
} SubCmd;

struct PortTty {
	uint port;
	struct ttystatics Tty;
};

static struct PortTty PortTty;
typedef struct ttystatics TERMIO;

/*
** This table is used when the config.rio downloads bin code to the
** driver. We index the table using the product code, 0-F, and call
** the function pointed to by the entry, passing the information
** about the boot.
** The RIOBootCodeUNKNOWN entry is there to politely tell the calling
** process to bog off.
*/
static int
 (*RIOBootTable[MAX_PRODUCT]) (struct rio_info *, struct DownLoad *) = {
					/* 0 */ RIOBootCodeHOST,
					/* Host Card */
					/* 1 */ RIOBootCodeRTA,
					/* RTA */
};

#define drv_makedev(maj, min) ((((uint) maj & 0xff) << 8) | ((uint) min & 0xff))

static int copy_from_io(void __user *to, void __iomem *from, size_t size)
{
	void *buf = kmalloc(size, GFP_KERNEL);
	int res = -ENOMEM;
	if (buf) {
		rio_memcpy_fromio(buf, from, size);
		res = copy_to_user(to, buf, size);
		kfree(buf);
	}
	return res;
}

int riocontrol(struct rio_info *p, dev_t dev, int cmd, unsigned long arg, int su)
{
	uint Host;		/* leave me unsigned! */
	uint port;		/* and me! */
	struct Host *HostP;
	ushort loop;
	int Entry;
	struct Port *PortP;
	struct PKT __iomem *PacketP;
	int retval = 0;
	unsigned long flags;
	void __user *argp = (void __user *)arg;

	func_enter();

	/* Confuse the compiler to think that we've initialized these */
	Host = 0;
	PortP = NULL;

	rio_dprintk(RIO_DEBUG_CTRL, "control ioctl cmd: 0x%x arg: %p\n", cmd, argp);

	switch (cmd) {
		/*
		 ** RIO_SET_TIMER
		 **
		 ** Change the value of the host card interrupt timer.
		 ** If the host card number is -1 then all host cards are changed
		 ** otherwise just the specified host card will be changed.
		 */
	case RIO_SET_TIMER:
		rio_dprintk(RIO_DEBUG_CTRL, "RIO_SET_TIMER to %ldms\n", arg);
		{
			int host, value;
			host = (arg >> 16) & 0x0000FFFF;
			value = arg & 0x0000ffff;
			if (host == -1) {
				for (host = 0; host < p->RIONumHosts; host++) {
					if (p->RIOHosts[host].Flags == RC_RUNNING) {
						writew(value, &p->RIOHosts[host].ParmMapP->timer);
					}
				}
			} else if (host >= p->RIONumHosts) {
				return -EINVAL;
			} else {
				if (p->RIOHosts[host].Flags == RC_RUNNING) {
					writew(value, &p->RIOHosts[host].ParmMapP->timer);
				}
			}
		}
		return 0;

	case RIO_FOAD_RTA:
		rio_dprintk(RIO_DEBUG_CTRL, "RIO_FOAD_RTA\n");
		return RIOCommandRta(p, arg, RIOFoadRta);

	case RIO_ZOMBIE_RTA:
		rio_dprintk(RIO_DEBUG_CTRL, "RIO_ZOMBIE_RTA\n");
		return RIOCommandRta(p, arg, RIOZombieRta);

	case RIO_IDENTIFY_RTA:
		rio_dprintk(RIO_DEBUG_CTRL, "RIO_IDENTIFY_RTA\n");
		return RIOIdentifyRta(p, argp);

	case RIO_KILL_NEIGHBOUR:
		rio_dprintk(RIO_DEBUG_CTRL, "RIO_KILL_NEIGHBOUR\n");
		return RIOKillNeighbour(p, argp);

	case SPECIAL_RUP_CMD:
		{
			struct CmdBlk *CmdBlkP;

			rio_dprintk(RIO_DEBUG_CTRL, "SPECIAL_RUP_CMD\n");
			if (copy_from_user(&SpecialRupCmd, argp, sizeof(SpecialRupCmd))) {
				rio_dprintk(RIO_DEBUG_CTRL, "SPECIAL_RUP_CMD copy failed\n");
				p->RIOError.Error = COPYIN_FAILED;
				return -EFAULT;
			}
			CmdBlkP = RIOGetCmdBlk();
			if (!CmdBlkP) {
				rio_dprintk(RIO_DEBUG_CTRL, "SPECIAL_RUP_CMD GetCmdBlk failed\n");
				return -ENXIO;
			}
			CmdBlkP->Packet = SpecialRupCmd.Packet;
			if (SpecialRupCmd.Host >= p->RIONumHosts)
				SpecialRupCmd.Host = 0;
			rio_dprintk(RIO_DEBUG_CTRL, "Queue special rup command for host %d rup %d\n", SpecialRupCmd.Host, SpecialRupCmd.RupNum);
			if (RIOQueueCmdBlk(&p->RIOHosts[SpecialRupCmd.Host], SpecialRupCmd.RupNum, CmdBlkP) == RIO_FAIL) {
				printk(KERN_WARNING "rio: FAILED TO QUEUE SPECIAL RUP COMMAND\n");
			}
			return 0;
		}

	case RIO_DEBUG_MEM:
		return -EPERM;

	case RIO_ALL_MODEM:
		rio_dprintk(RIO_DEBUG_CTRL, "RIO_ALL_MODEM\n");
		p->RIOError.Error = IOCTL_COMMAND_UNKNOWN;
		return -EINVAL;

	case RIO_GET_TABLE:
		/*
		 ** Read the routing table from the device driver to user space
		 */
		rio_dprintk(RIO_DEBUG_CTRL, "RIO_GET_TABLE\n");

		if ((retval = RIOApel(p)) != 0)
			return retval;

		if (copy_to_user(argp, p->RIOConnectTable, TOTAL_MAP_ENTRIES * sizeof(struct Map))) {
			rio_dprintk(RIO_DEBUG_CTRL, "RIO_GET_TABLE copy failed\n");
			p->RIOError.Error = COPYOUT_FAILED;
			return -EFAULT;
		}

		{
			int entry;
			rio_dprintk(RIO_DEBUG_CTRL, "*****\nMAP ENTRIES\n");
			for (entry = 0; entry < TOTAL_MAP_ENTRIES; entry++) {
				if ((p->RIOConnectTable[entry].ID == 0) && (p->RIOConnectTable[entry].HostUniqueNum == 0) && (p->RIOConnectTable[entry].RtaUniqueNum == 0))
					continue;

				rio_dprintk(RIO_DEBUG_CTRL, "Map entry %d.HostUniqueNum = 0x%x\n", entry, p->RIOConnectTable[entry].HostUniqueNum);
				rio_dprintk(RIO_DEBUG_CTRL, "Map entry %d.RtaUniqueNum = 0x%x\n", entry, p->RIOConnectTable[entry].RtaUniqueNum);
				rio_dprintk(RIO_DEBUG_CTRL, "Map entry %d.ID = 0x%x\n", entry, p->RIOConnectTable[entry].ID);
				rio_dprintk(RIO_DEBUG_CTRL, "Map entry %d.ID2 = 0x%x\n", entry, p->RIOConnectTable[entry].ID2);
				rio_dprintk(RIO_DEBUG_CTRL, "Map entry %d.Flags = 0x%x\n", entry, (int) p->RIOConnectTable[entry].Flags);
				rio_dprintk(RIO_DEBUG_CTRL, "Map entry %d.SysPort = 0x%x\n", entry, (int) p->RIOConnectTable[entry].SysPort);
				rio_dprintk(RIO_DEBUG_CTRL, "Map entry %d.Top[0].Unit = %x\n", entry, p->RIOConnectTable[entry].Topology[0].Unit);
				rio_dprintk(RIO_DEBUG_CTRL, "Map entry %d.Top[0].Link = %x\n", entry, p->RIOConnectTable[entry].Topology[0].Link);
				rio_dprintk(RIO_DEBUG_CTRL, "Map entry %d.Top[1].Unit = %x\n", entry, p->RIOConnectTable[entry].Topology[1].Unit);
				rio_dprintk(RIO_DEBUG_CTRL, "Map entry %d.Top[1].Link = %x\n", entry, p->RIOConnectTable[entry].Topology[1].Link);
				rio_dprintk(RIO_DEBUG_CTRL, "Map entry %d.Top[2].Unit = %x\n", entry, p->RIOConnectTable[entry].Topology[2].Unit);
				rio_dprintk(RIO_DEBUG_CTRL, "Map entry %d.Top[2].Link = %x\n", entry, p->RIOConnectTable[entry].Topology[2].Link);
				rio_dprintk(RIO_DEBUG_CTRL, "Map entry %d.Top[3].Unit = %x\n", entry, p->RIOConnectTable[entry].Topology[3].Unit);
				rio_dprintk(RIO_DEBUG_CTRL, "Map entry %d.Top[4].Link = %x\n", entry, p->RIOConnectTable[entry].Topology[3].Link);
				rio_dprintk(RIO_DEBUG_CTRL, "Map entry %d.Name = %s\n", entry, p->RIOConnectTable[entry].Name);
			}
			rio_dprintk(RIO_DEBUG_CTRL, "*****\nEND MAP ENTRIES\n");
		}
		p->RIOQuickCheck = NOT_CHANGED;	/* a table has been gotten */
		return 0;

	case RIO_PUT_TABLE:
		/*
		 ** Write the routing table to the device driver from user space
		 */
		rio_dprintk(RIO_DEBUG_CTRL, "RIO_PUT_TABLE\n");

		if (!su) {
			rio_dprintk(RIO_DEBUG_CTRL, "RIO_PUT_TABLE !Root\n");
			p->RIOError.Error = NOT_SUPER_USER;
			return -EPERM;
		}
		if (copy_from_user(&p->RIOConnectTable[0], argp, TOTAL_MAP_ENTRIES * sizeof(struct Map))) {
			rio_dprintk(RIO_DEBUG_CTRL, "RIO_PUT_TABLE copy failed\n");
			p->RIOError.Error = COPYIN_FAILED;
			return -EFAULT;
		}
/*
***********************************
				{
					int entry;
					rio_dprint(RIO_DEBUG_CTRL,  ("*****\nMAP ENTRIES\n") );
					for ( entry=0; entry<TOTAL_MAP_ENTRIES; entry++ )
					{
						rio_dprint(RIO_DEBUG_CTRL,  ("Map entry %d.HostUniqueNum = 0x%x\n", entry, p->RIOConnectTable[entry].HostUniqueNum ) );
						rio_dprint(RIO_DEBUG_CTRL,  ("Map entry %d.RtaUniqueNum = 0x%x\n", entry, p->RIOConnectTable[entry].RtaUniqueNum ) );
						rio_dprint(RIO_DEBUG_CTRL,  ("Map entry %d.ID = 0x%x\n", entry, p->RIOConnectTable[entry].ID ) );
						rio_dprint(RIO_DEBUG_CTRL,  ("Map entry %d.ID2 = 0x%x\n", entry, p->RIOConnectTable[entry].ID2 ) );
						rio_dprint(RIO_DEBUG_CTRL,  ("Map entry %d.Flags = 0x%x\n", entry, p->RIOConnectTable[entry].Flags ) );
						rio_dprint(RIO_DEBUG_CTRL,  ("Map entry %d.SysPort = 0x%x\n", entry, p->RIOConnectTable[entry].SysPort ) );
						rio_dprint(RIO_DEBUG_CTRL,  ("Map entry %d.Top[0].Unit = %b\n", entry, p->RIOConnectTable[entry].Topology[0].Unit ) );
						rio_dprint(RIO_DEBUG_CTRL,  ("Map entry %d.Top[0].Link = %b\n", entry, p->RIOConnectTable[entry].Topology[0].Link ) );
						rio_dprint(RIO_DEBUG_CTRL,  ("Map entry %d.Top[1].Unit = %b\n", entry, p->RIOConnectTable[entry].Topology[1].Unit ) );
						rio_dprint(RIO_DEBUG_CTRL,  ("Map entry %d.Top[1].Link = %b\n", entry, p->RIOConnectTable[entry].Topology[1].Link ) );
						rio_dprint(RIO_DEBUG_CTRL,  ("Map entry %d.Top[2].Unit = %b\n", entry, p->RIOConnectTable[entry].Topology[2].Unit ) );
						rio_dprint(RIO_DEBUG_CTRL,  ("Map entry %d.Top[2].Link = %b\n", entry, p->RIOConnectTable[entry].Topology[2].Link ) );
						rio_dprint(RIO_DEBUG_CTRL,  ("Map entry %d.Top[3].Unit = %b\n", entry, p->RIOConnectTable[entry].Topology[3].Unit ) );
						rio_dprint(RIO_DEBUG_CTRL,  ("Map entry %d.Top[4].Link = %b\n", entry, p->RIOConnectTable[entry].Topology[3].Link ) );
						rio_dprint(RIO_DEBUG_CTRL,  ("Map entry %d.Name = %s\n", entry, p->RIOConnectTable[entry].Name ) );
					}
					rio_dprint(RIO_DEBUG_CTRL,  ("*****\nEND MAP ENTRIES\n") );
				}
***********************************
*/
		return RIONewTable(p);

	case RIO_GET_BINDINGS:
		/*
		 ** Send bindings table, containing unique numbers of RTAs owned
		 ** by this system to user space
		 */
		rio_dprintk(RIO_DEBUG_CTRL, "RIO_GET_BINDINGS\n");

		if (!su) {
			rio_dprintk(RIO_DEBUG_CTRL, "RIO_GET_BINDINGS !Root\n");
			p->RIOError.Error = NOT_SUPER_USER;
			return -EPERM;
		}
		if (copy_to_user(argp, p->RIOBindTab, (sizeof(ulong) * MAX_RTA_BINDINGS))) {
			rio_dprintk(RIO_DEBUG_CTRL, "RIO_GET_BINDINGS copy failed\n");
			p->RIOError.Error = COPYOUT_FAILED;
			return -EFAULT;
		}
		return 0;

	case RIO_PUT_BINDINGS:
		/*
		 ** Receive a bindings table, containing unique numbers of RTAs owned
		 ** by this system
		 */
		rio_dprintk(RIO_DEBUG_CTRL, "RIO_PUT_BINDINGS\n");

		if (!su) {
			rio_dprintk(RIO_DEBUG_CTRL, "RIO_PUT_BINDINGS !Root\n");
			p->RIOError.Error = NOT_SUPER_USER;
			return -EPERM;
		}
		if (copy_from_user(&p->RIOBindTab[0], argp, (sizeof(ulong) * MAX_RTA_BINDINGS))) {
			rio_dprintk(RIO_DEBUG_CTRL, "RIO_PUT_BINDINGS copy failed\n");
			p->RIOError.Error = COPYIN_FAILED;
			return -EFAULT;
		}
		return 0;

	case RIO_BIND_RTA:
		{
			int EmptySlot = -1;
			/*
			 ** Bind this RTA to host, so that it will be booted by
			 ** host in 'boot owned RTAs' mode.
			 */
			rio_dprintk(RIO_DEBUG_CTRL, "RIO_BIND_RTA\n");

			if (!su) {
				rio_dprintk(RIO_DEBUG_CTRL, "RIO_BIND_RTA !Root\n");
				p->RIOError.Error = NOT_SUPER_USER;
				return -EPERM;
			}
			for (Entry = 0; Entry < MAX_RTA_BINDINGS; Entry++) {
				if ((EmptySlot == -1) && (p->RIOBindTab[Entry] == 0L))
					EmptySlot = Entry;
				else if (p->RIOBindTab[Entry] == arg) {
					/*
					 ** Already exists - delete
					 */
					p->RIOBindTab[Entry] = 0L;
					rio_dprintk(RIO_DEBUG_CTRL, "Removing Rta %ld from p->RIOBindTab\n", arg);
					return 0;
				}
			}
			/*
			 ** Dosen't exist - add
			 */
			if (EmptySlot != -1) {
				p->RIOBindTab[EmptySlot] = arg;
				rio_dprintk(RIO_DEBUG_CTRL, "Adding Rta %lx to p->RIOBindTab\n", arg);
			} else {
				rio_dprintk(RIO_DEBUG_CTRL, "p->RIOBindTab full! - Rta %lx not added\n", arg);
				return -ENOMEM;
			}
			return 0;
		}

	case RIO_RESUME:
		rio_dprintk(RIO_DEBUG_CTRL, "RIO_RESUME\n");
		port = arg;
		if ((port < 0) || (port > 511)) {
			rio_dprintk(RIO_DEBUG_CTRL, "RIO_RESUME: Bad port number %d\n", port);
			p->RIOError.Error = PORT_NUMBER_OUT_OF_RANGE;
			return -EINVAL;
		}
		PortP = p->RIOPortp[port];
		if (!PortP->Mapped) {
			rio_dprintk(RIO_DEBUG_CTRL, "RIO_RESUME: Port %d not mapped\n", port);
			p->RIOError.Error = PORT_NOT_MAPPED_INTO_SYSTEM;
			return -EINVAL;
		}
		if (!(PortP->State & (RIO_LOPEN | RIO_MOPEN))) {
			rio_dprintk(RIO_DEBUG_CTRL, "RIO_RESUME: Port %d not open\n", port);
			return -EINVAL;
		}

		rio_spin_lock_irqsave(&PortP->portSem, flags);
		if (RIOPreemptiveCmd(p, (p->RIOPortp[port]), RIOC_RESUME) ==
				RIO_FAIL) {
			rio_dprintk(RIO_DEBUG_CTRL, "RIO_RESUME failed\n");
			rio_spin_unlock_irqrestore(&PortP->portSem, flags);
			return -EBUSY;
		} else {
			rio_dprintk(RIO_DEBUG_CTRL, "RIO_RESUME: Port %d resumed\n", port);
			PortP->State |= RIO_BUSY;
		}
		rio_spin_unlock_irqrestore(&PortP->portSem, flags);
		return retval;

	case RIO_ASSIGN_RTA:
		rio_dprintk(RIO_DEBUG_CTRL, "RIO_ASSIGN_RTA\n");
		if (!su) {
			rio_dprintk(RIO_DEBUG_CTRL, "RIO_ASSIGN_RTA !Root\n");
			p->RIOError.Error = NOT_SUPER_USER;
			return -EPERM;
		}
		if (copy_from_user(&MapEnt, argp, sizeof(MapEnt))) {
			rio_dprintk(RIO_DEBUG_CTRL, "Copy from user space failed\n");
			p->RIOError.Error = COPYIN_FAILED;
			return -EFAULT;
		}
		return RIOAssignRta(p, &MapEnt);

	case RIO_CHANGE_NAME:
		rio_dprintk(RIO_DEBUG_CTRL, "RIO_CHANGE_NAME\n");
		if (!su) {
			rio_dprintk(RIO_DEBUG_CTRL, "RIO_CHANGE_NAME !Root\n");
			p->RIOError.Error = NOT_SUPER_USER;
			return -EPERM;
		}
		if (copy_from_user(&MapEnt, argp, sizeof(MapEnt))) {
			rio_dprintk(RIO_DEBUG_CTRL, "Copy from user space failed\n");
			p->RIOError.Error = COPYIN_FAILED;
			return -EFAULT;
		}
		return RIOChangeName(p, &MapEnt);

	case RIO_DELETE_RTA:
		rio_dprintk(RIO_DEBUG_CTRL, "RIO_DELETE_RTA\n");
		if (!su) {
			rio_dprintk(RIO_DEBUG_CTRL, "RIO_DELETE_RTA !Root\n");
			p->RIOError.Error = NOT_SUPER_USER;
			return -EPERM;
		}
		if (copy_from_user(&MapEnt, argp, sizeof(MapEnt))) {
			rio_dprintk(RIO_DEBUG_CTRL, "Copy from data space failed\n");
			p->RIOError.Error = COPYIN_FAILED;
			return -EFAULT;
		}
		return RIODeleteRta(p, &MapEnt);

	case RIO_QUICK_CHECK:
		if (copy_to_user(argp, &p->RIORtaDisCons, sizeof(unsigned int))) {
			p->RIOError.Error = COPYOUT_FAILED;
			return -EFAULT;
		}
		return 0;

	case RIO_LAST_ERROR:
		if (copy_to_user(argp, &p->RIOError, sizeof(struct Error)))
			return -EFAULT;
		return 0;

	case RIO_GET_LOG:
		rio_dprintk(RIO_DEBUG_CTRL, "RIO_GET_LOG\n");
		return -EINVAL;

	case RIO_GET_MODTYPE:
		if (copy_from_user(&port, argp, sizeof(unsigned int))) {
			p->RIOError.Error = COPYIN_FAILED;
			return -EFAULT;
		}
		rio_dprintk(RIO_DEBUG_CTRL, "Get module type for port %d\n", port);
		if (port < 0 || port > 511) {
			rio_dprintk(RIO_DEBUG_CTRL, "RIO_GET_MODTYPE: Bad port number %d\n", port);
			p->RIOError.Error = PORT_NUMBER_OUT_OF_RANGE;
			return -EINVAL;
		}
		PortP = (p->RIOPortp[port]);
		if (!PortP->Mapped) {
			rio_dprintk(RIO_DEBUG_CTRL, "RIO_GET_MODTYPE: Port %d not mapped\n", port);
			p->RIOError.Error = PORT_NOT_MAPPED_INTO_SYSTEM;
			return -EINVAL;
		}
		/*
		 ** Return module type of port
		 */
		port = PortP->HostP->UnixRups[PortP->RupNum].ModTypes;
		if (copy_to_user(argp, &port, sizeof(unsigned int))) {
			p->RIOError.Error = COPYOUT_FAILED;
			return -EFAULT;
		}
		return (0);
	case RIO_BLOCK_OPENS:
		rio_dprintk(RIO_DEBUG_CTRL, "Opens block until booted\n");
		for (Entry = 0; Entry < RIO_PORTS; Entry++) {
			rio_spin_lock_irqsave(&PortP->portSem, flags);
			p->RIOPortp[Entry]->WaitUntilBooted = 1;
			rio_spin_unlock_irqrestore(&PortP->portSem, flags);
		}
		return 0;

	case RIO_SETUP_PORTS:
		rio_dprintk(RIO_DEBUG_CTRL, "Setup ports\n");
		if (copy_from_user(&PortSetup, argp, sizeof(PortSetup))) {
			p->RIOError.Error = COPYIN_FAILED;
			rio_dprintk(RIO_DEBUG_CTRL, "EFAULT");
			return -EFAULT;
		}
		if (PortSetup.From > PortSetup.To || PortSetup.To >= RIO_PORTS) {
			p->RIOError.Error = PORT_NUMBER_OUT_OF_RANGE;
			rio_dprintk(RIO_DEBUG_CTRL, "ENXIO");
			return -ENXIO;
		}
		if (PortSetup.XpCps > p->RIOConf.MaxXpCps || PortSetup.XpCps < p->RIOConf.MinXpCps) {
			p->RIOError.Error = XPRINT_CPS_OUT_OF_RANGE;
			rio_dprintk(RIO_DEBUG_CTRL, "EINVAL");
			return -EINVAL;
		}
		if (!p->RIOPortp) {
			printk(KERN_ERR "rio: No p->RIOPortp array!\n");
			rio_dprintk(RIO_DEBUG_CTRL, "No p->RIOPortp array!\n");
			return -EIO;
		}
		rio_dprintk(RIO_DEBUG_CTRL, "entering loop (%d %d)!\n", PortSetup.From, PortSetup.To);
		for (loop = PortSetup.From; loop <= PortSetup.To; loop++) {
			rio_dprintk(RIO_DEBUG_CTRL, "in loop (%d)!\n", loop);
		}
		rio_dprintk(RIO_DEBUG_CTRL, "after loop (%d)!\n", loop);
		rio_dprintk(RIO_DEBUG_CTRL, "Retval:%x\n", retval);
		return retval;

	case RIO_GET_PORT_SETUP:
		rio_dprintk(RIO_DEBUG_CTRL, "Get port setup\n");
		if (copy_from_user(&PortSetup, argp, sizeof(PortSetup))) {
			p->RIOError.Error = COPYIN_FAILED;
			return -EFAULT;
		}
		if (PortSetup.From >= RIO_PORTS) {
			p->RIOError.Error = PORT_NUMBER_OUT_OF_RANGE;
			return -ENXIO;
		}

		port = PortSetup.To = PortSetup.From;
		PortSetup.IxAny = (p->RIOPortp[port]->Config & RIO_IXANY) ? 1 : 0;
		PortSetup.IxOn = (p->RIOPortp[port]->Config & RIO_IXON) ? 1 : 0;
		PortSetup.Drain = (p->RIOPortp[port]->Config & RIO_WAITDRAIN) ? 1 : 0;
		PortSetup.Store = p->RIOPortp[port]->Store;
		PortSetup.Lock = p->RIOPortp[port]->Lock;
		PortSetup.XpCps = p->RIOPortp[port]->Xprint.XpCps;
		memcpy(PortSetup.XpOn, p->RIOPortp[port]->Xprint.XpOn, MAX_XP_CTRL_LEN);
		memcpy(PortSetup.XpOff, p->RIOPortp[port]->Xprint.XpOff, MAX_XP_CTRL_LEN);
		PortSetup.XpOn[MAX_XP_CTRL_LEN - 1] = '\0';
		PortSetup.XpOff[MAX_XP_CTRL_LEN - 1] = '\0';

		if (copy_to_user(argp, &PortSetup, sizeof(PortSetup))) {
			p->RIOError.Error = COPYOUT_FAILED;
			return -EFAULT;
		}
		return retval;

	case RIO_GET_PORT_PARAMS:
		rio_dprintk(RIO_DEBUG_CTRL, "Get port params\n");
		if (copy_from_user(&PortParams, argp, sizeof(struct PortParams))) {
			p->RIOError.Error = COPYIN_FAILED;
			return -EFAULT;
		}
		if (PortParams.Port >= RIO_PORTS) {
			p->RIOError.Error = PORT_NUMBER_OUT_OF_RANGE;
			return -ENXIO;
		}
		PortP = (p->RIOPortp[PortParams.Port]);
		PortParams.Config = PortP->Config;
		PortParams.State = PortP->State;
		rio_dprintk(RIO_DEBUG_CTRL, "Port %d\n", PortParams.Port);

		if (copy_to_user(argp, &PortParams, sizeof(struct PortParams))) {
			p->RIOError.Error = COPYOUT_FAILED;
			return -EFAULT;
		}
		return retval;

	case RIO_GET_PORT_TTY:
		rio_dprintk(RIO_DEBUG_CTRL, "Get port tty\n");
		if (copy_from_user(&PortTty, argp, sizeof(struct PortTty))) {
			p->RIOError.Error = COPYIN_FAILED;
			return -EFAULT;
		}
		if (PortTty.port >= RIO_PORTS) {
			p->RIOError.Error = PORT_NUMBER_OUT_OF_RANGE;
			return -ENXIO;
		}

		rio_dprintk(RIO_DEBUG_CTRL, "Port %d\n", PortTty.port);
		PortP = (p->RIOPortp[PortTty.port]);
		if (copy_to_user(argp, &PortTty, sizeof(struct PortTty))) {
			p->RIOError.Error = COPYOUT_FAILED;
			return -EFAULT;
		}
		return retval;

	case RIO_SET_PORT_TTY:
		if (copy_from_user(&PortTty, argp, sizeof(struct PortTty))) {
			p->RIOError.Error = COPYIN_FAILED;
			return -EFAULT;
		}
		rio_dprintk(RIO_DEBUG_CTRL, "Set port %d tty\n", PortTty.port);
		if (PortTty.port >= (ushort) RIO_PORTS) {
			p->RIOError.Error = PORT_NUMBER_OUT_OF_RANGE;
			return -ENXIO;
		}
		PortP = (p->RIOPortp[PortTty.port]);
		RIOParam(PortP, RIOC_CONFIG, PortP->State & RIO_MODEM,
				OK_TO_SLEEP);
		return retval;

	case RIO_SET_PORT_PARAMS:
		rio_dprintk(RIO_DEBUG_CTRL, "Set port params\n");
		if (copy_from_user(&PortParams, argp, sizeof(PortParams))) {
			p->RIOError.Error = COPYIN_FAILED;
			return -EFAULT;
		}
		if (PortParams.Port >= (ushort) RIO_PORTS) {
			p->RIOError.Error = PORT_NUMBER_OUT_OF_RANGE;
			return -ENXIO;
		}
		PortP = (p->RIOPortp[PortParams.Port]);
		rio_spin_lock_irqsave(&PortP->portSem, flags);
		PortP->Config = PortParams.Config;
		rio_spin_unlock_irqrestore(&PortP->portSem, flags);
		return retval;

	case RIO_GET_PORT_STATS:
		rio_dprintk(RIO_DEBUG_CTRL, "RIO_GET_PORT_STATS\n");
		if (copy_from_user(&portStats, argp, sizeof(struct portStats))) {
			p->RIOError.Error = COPYIN_FAILED;
			return -EFAULT;
		}
		if (portStats.port < 0 || portStats.port >= RIO_PORTS) {
			p->RIOError.Error = PORT_NUMBER_OUT_OF_RANGE;
			return -ENXIO;
		}
		PortP = (p->RIOPortp[portStats.port]);
		portStats.gather = PortP->statsGather;
		portStats.txchars = PortP->txchars;
		portStats.rxchars = PortP->rxchars;
		portStats.opens = PortP->opens;
		portStats.closes = PortP->closes;
		portStats.ioctls = PortP->ioctls;
		if (copy_to_user(argp, &portStats, sizeof(struct portStats))) {
			p->RIOError.Error = COPYOUT_FAILED;
			return -EFAULT;
		}
		return retval;

	case RIO_RESET_PORT_STATS:
		port = arg;
		rio_dprintk(RIO_DEBUG_CTRL, "RIO_RESET_PORT_STATS\n");
		if (port >= RIO_PORTS) {
			p->RIOError.Error = PORT_NUMBER_OUT_OF_RANGE;
			return -ENXIO;
		}
		PortP = (p->RIOPortp[port]);
		rio_spin_lock_irqsave(&PortP->portSem, flags);
		PortP->txchars = 0;
		PortP->rxchars = 0;
		PortP->opens = 0;
		PortP->closes = 0;
		PortP->ioctls = 0;
		rio_spin_unlock_irqrestore(&PortP->portSem, flags);
		return retval;

	case RIO_GATHER_PORT_STATS:
		rio_dprintk(RIO_DEBUG_CTRL, "RIO_GATHER_PORT_STATS\n");
		if (copy_from_user(&portStats, argp, sizeof(struct portStats))) {
			p->RIOError.Error = COPYIN_FAILED;
			return -EFAULT;
		}
		if (portStats.port < 0 || portStats.port >= RIO_PORTS) {
			p->RIOError.Error = PORT_NUMBER_OUT_OF_RANGE;
			return -ENXIO;
		}
		PortP = (p->RIOPortp[portStats.port]);
		rio_spin_lock_irqsave(&PortP->portSem, flags);
		PortP->statsGather = portStats.gather;
		rio_spin_unlock_irqrestore(&PortP->portSem, flags);
		return retval;

	case RIO_READ_CONFIG:
		rio_dprintk(RIO_DEBUG_CTRL, "RIO_READ_CONFIG\n");
		if (copy_to_user(argp, &p->RIOConf, sizeof(struct Conf))) {
			p->RIOError.Error = COPYOUT_FAILED;
			return -EFAULT;
		}
		return retval;

	case RIO_SET_CONFIG:
		rio_dprintk(RIO_DEBUG_CTRL, "RIO_SET_CONFIG\n");
		if (!su) {
			p->RIOError.Error = NOT_SUPER_USER;
			return -EPERM;
		}
		if (copy_from_user(&p->RIOConf, argp, sizeof(struct Conf))) {
			p->RIOError.Error = COPYIN_FAILED;
			return -EFAULT;
		}
		/*
		 ** move a few value around
		 */
		for (Host = 0; Host < p->RIONumHosts; Host++)
			if ((p->RIOHosts[Host].Flags & RUN_STATE) == RC_RUNNING)
				writew(p->RIOConf.Timer, &p->RIOHosts[Host].ParmMapP->timer);
		return retval;

	case RIO_START_POLLER:
		rio_dprintk(RIO_DEBUG_CTRL, "RIO_START_POLLER\n");
		return -EINVAL;

	case RIO_STOP_POLLER:
		rio_dprintk(RIO_DEBUG_CTRL, "RIO_STOP_POLLER\n");
		if (!su) {
			p->RIOError.Error = NOT_SUPER_USER;
			return -EPERM;
		}
		p->RIOPolling = NOT_POLLING;
		return retval;

	case RIO_SETDEBUG:
	case RIO_GETDEBUG:
		rio_dprintk(RIO_DEBUG_CTRL, "RIO_SETDEBUG/RIO_GETDEBUG\n");
		if (copy_from_user(&DebugCtrl, argp, sizeof(DebugCtrl))) {
			p->RIOError.Error = COPYIN_FAILED;
			return -EFAULT;
		}
		if (DebugCtrl.SysPort == NO_PORT) {
			if (cmd == RIO_SETDEBUG) {
				if (!su) {
					p->RIOError.Error = NOT_SUPER_USER;
					return -EPERM;
				}
				p->rio_debug = DebugCtrl.Debug;
				p->RIODebugWait = DebugCtrl.Wait;
				rio_dprintk(RIO_DEBUG_CTRL, "Set global debug to 0x%x set wait to 0x%x\n", p->rio_debug, p->RIODebugWait);
			} else {
				rio_dprintk(RIO_DEBUG_CTRL, "Get global debug 0x%x wait 0x%x\n", p->rio_debug, p->RIODebugWait);
				DebugCtrl.Debug = p->rio_debug;
				DebugCtrl.Wait = p->RIODebugWait;
				if (copy_to_user(argp, &DebugCtrl, sizeof(DebugCtrl))) {
					rio_dprintk(RIO_DEBUG_CTRL, "RIO_SET/GET DEBUG: bad port number %d\n", DebugCtrl.SysPort);
					p->RIOError.Error = COPYOUT_FAILED;
					return -EFAULT;
				}
			}
		} else if (DebugCtrl.SysPort >= RIO_PORTS && DebugCtrl.SysPort != NO_PORT) {
			rio_dprintk(RIO_DEBUG_CTRL, "RIO_SET/GET DEBUG: bad port number %d\n", DebugCtrl.SysPort);
			p->RIOError.Error = PORT_NUMBER_OUT_OF_RANGE;
			return -ENXIO;
		} else if (cmd == RIO_SETDEBUG) {
			if (!su) {
				p->RIOError.Error = NOT_SUPER_USER;
				return -EPERM;
			}
			rio_spin_lock_irqsave(&PortP->portSem, flags);
			p->RIOPortp[DebugCtrl.SysPort]->Debug = DebugCtrl.Debug;
			rio_spin_unlock_irqrestore(&PortP->portSem, flags);
			rio_dprintk(RIO_DEBUG_CTRL, "RIO_SETDEBUG 0x%x\n", p->RIOPortp[DebugCtrl.SysPort]->Debug);
		} else {
			rio_dprintk(RIO_DEBUG_CTRL, "RIO_GETDEBUG 0x%x\n", p->RIOPortp[DebugCtrl.SysPort]->Debug);
			DebugCtrl.Debug = p->RIOPortp[DebugCtrl.SysPort]->Debug;
			if (copy_to_user(argp, &DebugCtrl, sizeof(DebugCtrl))) {
				rio_dprintk(RIO_DEBUG_CTRL, "RIO_GETDEBUG: Bad copy to user space\n");
				p->RIOError.Error = COPYOUT_FAILED;
				return -EFAULT;
			}
		}
		return retval;

	case RIO_VERSID:
		/*
		 ** Enquire about the release and version.
		 ** We return MAX_VERSION_LEN bytes, being a
		 ** textual null terminated string.
		 */
		rio_dprintk(RIO_DEBUG_CTRL, "RIO_VERSID\n");
		if (copy_to_user(argp, RIOVersid(), sizeof(struct rioVersion))) {
			rio_dprintk(RIO_DEBUG_CTRL, "RIO_VERSID: Bad copy to user space (host=%d)\n", Host);
			p->RIOError.Error = COPYOUT_FAILED;
			return -EFAULT;
		}
		return retval;

	case RIO_NUM_HOSTS:
		/*
		 ** Enquire as to the number of hosts located
		 ** at init time.
		 */
		rio_dprintk(RIO_DEBUG_CTRL, "RIO_NUM_HOSTS\n");
		if (copy_to_user(argp, &p->RIONumHosts, sizeof(p->RIONumHosts))) {
			rio_dprintk(RIO_DEBUG_CTRL, "RIO_NUM_HOSTS: Bad copy to user space\n");
			p->RIOError.Error = COPYOUT_FAILED;
			return -EFAULT;
		}
		return retval;

	case RIO_HOST_FOAD:
		/*
		 ** Kill host. This may not be in the final version...
		 */
		rio_dprintk(RIO_DEBUG_CTRL, "RIO_HOST_FOAD %ld\n", arg);
		if (!su) {
			rio_dprintk(RIO_DEBUG_CTRL, "RIO_HOST_FOAD: Not super user\n");
			p->RIOError.Error = NOT_SUPER_USER;
			return -EPERM;
		}
		p->RIOHalted = 1;
		p->RIOSystemUp = 0;

		for (Host = 0; Host < p->RIONumHosts; Host++) {
			(void) RIOBoardTest(p->RIOHosts[Host].PaddrP, p->RIOHosts[Host].Caddr, p->RIOHosts[Host].Type, p->RIOHosts[Host].Slot);
			memset(&p->RIOHosts[Host].Flags, 0, ((char *) &p->RIOHosts[Host].____end_marker____) - ((char *) &p->RIOHosts[Host].Flags));
			p->RIOHosts[Host].Flags = RC_WAITING;
		}
		RIOFoadWakeup(p);
		p->RIONumBootPkts = 0;
		p->RIOBooting = 0;
		printk("HEEEEELP!\n");

		for (loop = 0; loop < RIO_PORTS; loop++) {
			spin_lock_init(&p->RIOPortp[loop]->portSem);
			p->RIOPortp[loop]->InUse = NOT_INUSE;
		}

		p->RIOSystemUp = 0;
		return retval;

	case RIO_DOWNLOAD:
		rio_dprintk(RIO_DEBUG_CTRL, "RIO_DOWNLOAD\n");
		if (!su) {
			rio_dprintk(RIO_DEBUG_CTRL, "RIO_DOWNLOAD: Not super user\n");
			p->RIOError.Error = NOT_SUPER_USER;
			return -EPERM;
		}
		if (copy_from_user(&DownLoad, argp, sizeof(DownLoad))) {
			rio_dprintk(RIO_DEBUG_CTRL, "RIO_DOWNLOAD: Copy in from user space failed\n");
			p->RIOError.Error = COPYIN_FAILED;
			return -EFAULT;
		}
		rio_dprintk(RIO_DEBUG_CTRL, "Copied in download code for product code 0x%x\n", DownLoad.ProductCode);

		/*
		 ** It is important that the product code is an unsigned object!
		 */
		if (DownLoad.ProductCode >= MAX_PRODUCT) {
			rio_dprintk(RIO_DEBUG_CTRL, "RIO_DOWNLOAD: Bad product code %d passed\n", DownLoad.ProductCode);
			p->RIOError.Error = NO_SUCH_PRODUCT;
			return -ENXIO;
		}
		/*
		 ** do something!
		 */
		retval = (*(RIOBootTable[DownLoad.ProductCode])) (p, &DownLoad);
		/* <-- Panic */
		p->RIOHalted = 0;
		/*
		 ** and go back, content with a job well completed.
		 */
		return retval;

	case RIO_PARMS:
		{
			unsigned int host;

			if (copy_from_user(&host, argp, sizeof(host))) {
				rio_dprintk(RIO_DEBUG_CTRL, "RIO_HOST_REQ: Copy in from user space failed\n");
				p->RIOError.Error = COPYIN_FAILED;
				return -EFAULT;
			}
			/*
			 ** Fetch the parmmap
			 */
			rio_dprintk(RIO_DEBUG_CTRL, "RIO_PARMS\n");
			if (copy_from_io(argp, p->RIOHosts[host].ParmMapP, sizeof(PARM_MAP))) {
				p->RIOError.Error = COPYOUT_FAILED;
				rio_dprintk(RIO_DEBUG_CTRL, "RIO_PARMS: Copy out to user space failed\n");
				return -EFAULT;
			}
		}
		return retval;

	case RIO_HOST_REQ:
		rio_dprintk(RIO_DEBUG_CTRL, "RIO_HOST_REQ\n");
		if (copy_from_user(&HostReq, argp, sizeof(HostReq))) {
			rio_dprintk(RIO_DEBUG_CTRL, "RIO_HOST_REQ: Copy in from user space failed\n");
			p->RIOError.Error = COPYIN_FAILED;
			return -EFAULT;
		}
		if (HostReq.HostNum >= p->RIONumHosts) {
			p->RIOError.Error = HOST_NUMBER_OUT_OF_RANGE;
			rio_dprintk(RIO_DEBUG_CTRL, "RIO_HOST_REQ: Illegal host number %d\n", HostReq.HostNum);
			return -ENXIO;
		}
		rio_dprintk(RIO_DEBUG_CTRL, "Request for host %d\n", HostReq.HostNum);

		if (copy_to_user(HostReq.HostP, &p->RIOHosts[HostReq.HostNum], sizeof(struct Host))) {
			p->RIOError.Error = COPYOUT_FAILED;
			rio_dprintk(RIO_DEBUG_CTRL, "RIO_HOST_REQ: Bad copy to user space\n");
			return -EFAULT;
		}
		return retval;

	case RIO_HOST_DPRAM:
		rio_dprintk(RIO_DEBUG_CTRL, "Request for DPRAM\n");
		if (copy_from_user(&HostDpRam, argp, sizeof(HostDpRam))) {
			rio_dprintk(RIO_DEBUG_CTRL, "RIO_HOST_DPRAM: Copy in from user space failed\n");
			p->RIOError.Error = COPYIN_FAILED;
			return -EFAULT;
		}
		if (HostDpRam.HostNum >= p->RIONumHosts) {
			p->RIOError.Error = HOST_NUMBER_OUT_OF_RANGE;
			rio_dprintk(RIO_DEBUG_CTRL, "RIO_HOST_DPRAM: Illegal host number %d\n", HostDpRam.HostNum);
			return -ENXIO;
		}
		rio_dprintk(RIO_DEBUG_CTRL, "Request for host %d\n", HostDpRam.HostNum);

		if (p->RIOHosts[HostDpRam.HostNum].Type == RIO_PCI) {
			int off;
			/* It's hardware like this that really gets on my tits. */
			static unsigned char copy[sizeof(struct DpRam)];
			for (off = 0; off < sizeof(struct DpRam); off++)
				copy[off] = readb(p->RIOHosts[HostDpRam.HostNum].Caddr + off);
			if (copy_to_user(HostDpRam.DpRamP, copy, sizeof(struct DpRam))) {
				p->RIOError.Error = COPYOUT_FAILED;
				rio_dprintk(RIO_DEBUG_CTRL, "RIO_HOST_DPRAM: Bad copy to user space\n");
				return -EFAULT;
			}
		} else if (copy_from_io(HostDpRam.DpRamP, p->RIOHosts[HostDpRam.HostNum].Caddr, sizeof(struct DpRam))) {
			p->RIOError.Error = COPYOUT_FAILED;
			rio_dprintk(RIO_DEBUG_CTRL, "RIO_HOST_DPRAM: Bad copy to user space\n");
			return -EFAULT;
		}
		return retval;

	case RIO_SET_BUSY:
		rio_dprintk(RIO_DEBUG_CTRL, "RIO_SET_BUSY\n");
		if (arg > 511) {
			rio_dprintk(RIO_DEBUG_CTRL, "RIO_SET_BUSY: Bad port number %ld\n", arg);
			p->RIOError.Error = PORT_NUMBER_OUT_OF_RANGE;
			return -EINVAL;
		}
		rio_spin_lock_irqsave(&PortP->portSem, flags);
		p->RIOPortp[arg]->State |= RIO_BUSY;
		rio_spin_unlock_irqrestore(&PortP->portSem, flags);
		return retval;

	case RIO_HOST_PORT:
		/*
		 ** The daemon want port information
		 ** (probably for debug reasons)
		 */
		rio_dprintk(RIO_DEBUG_CTRL, "RIO_HOST_PORT\n");
		if (copy_from_user(&PortReq, argp, sizeof(PortReq))) {
			rio_dprintk(RIO_DEBUG_CTRL, "RIO_HOST_PORT: Copy in from user space failed\n");
			p->RIOError.Error = COPYIN_FAILED;
			return -EFAULT;
		}

		if (PortReq.SysPort >= RIO_PORTS) {	/* SysPort is unsigned */
			rio_dprintk(RIO_DEBUG_CTRL, "RIO_HOST_PORT: Illegal port number %d\n", PortReq.SysPort);
			p->RIOError.Error = PORT_NUMBER_OUT_OF_RANGE;
			return -ENXIO;
		}
		rio_dprintk(RIO_DEBUG_CTRL, "Request for port %d\n", PortReq.SysPort);
		if (copy_to_user(PortReq.PortP, p->RIOPortp[PortReq.SysPort], sizeof(struct Port))) {
			p->RIOError.Error = COPYOUT_FAILED;
			rio_dprintk(RIO_DEBUG_CTRL, "RIO_HOST_PORT: Bad copy to user space\n");
			return -EFAULT;
		}
		return retval;

	case RIO_HOST_RUP:
		/*
		 ** The daemon want rup information
		 ** (probably for debug reasons)
		 */
		rio_dprintk(RIO_DEBUG_CTRL, "RIO_HOST_RUP\n");
		if (copy_from_user(&RupReq, argp, sizeof(RupReq))) {
			rio_dprintk(RIO_DEBUG_CTRL, "RIO_HOST_RUP: Copy in from user space failed\n");
			p->RIOError.Error = COPYIN_FAILED;
			return -EFAULT;
		}
		if (RupReq.HostNum >= p->RIONumHosts) {	/* host is unsigned */
			rio_dprintk(RIO_DEBUG_CTRL, "RIO_HOST_RUP: Illegal host number %d\n", RupReq.HostNum);
			p->RIOError.Error = HOST_NUMBER_OUT_OF_RANGE;
			return -ENXIO;
		}
		if (RupReq.RupNum >= MAX_RUP + LINKS_PER_UNIT) {	/* eek! */
			rio_dprintk(RIO_DEBUG_CTRL, "RIO_HOST_RUP: Illegal rup number %d\n", RupReq.RupNum);
			p->RIOError.Error = RUP_NUMBER_OUT_OF_RANGE;
			return -EINVAL;
		}
		HostP = &p->RIOHosts[RupReq.HostNum];

		if ((HostP->Flags & RUN_STATE) != RC_RUNNING) {
			rio_dprintk(RIO_DEBUG_CTRL, "RIO_HOST_RUP: Host %d not running\n", RupReq.HostNum);
			p->RIOError.Error = HOST_NOT_RUNNING;
			return -EIO;
		}
		rio_dprintk(RIO_DEBUG_CTRL, "Request for rup %d from host %d\n", RupReq.RupNum, RupReq.HostNum);

		if (copy_from_io(RupReq.RupP, HostP->UnixRups[RupReq.RupNum].RupP, sizeof(struct RUP))) {
			p->RIOError.Error = COPYOUT_FAILED;
			rio_dprintk(RIO_DEBUG_CTRL, "RIO_HOST_RUP: Bad copy to user space\n");
			return -EFAULT;
		}
		return retval;

	case RIO_HOST_LPB:
		/*
		 ** The daemon want lpb information
		 ** (probably for debug reasons)
		 */
		rio_dprintk(RIO_DEBUG_CTRL, "RIO_HOST_LPB\n");
		if (copy_from_user(&LpbReq, argp, sizeof(LpbReq))) {
			rio_dprintk(RIO_DEBUG_CTRL, "RIO_HOST_LPB: Bad copy from user space\n");
			p->RIOError.Error = COPYIN_FAILED;
			return -EFAULT;
		}
		if (LpbReq.Host >= p->RIONumHosts) {	/* host is unsigned */
			rio_dprintk(RIO_DEBUG_CTRL, "RIO_HOST_LPB: Illegal host number %d\n", LpbReq.Host);
			p->RIOError.Error = HOST_NUMBER_OUT_OF_RANGE;
			return -ENXIO;
		}
		if (LpbReq.Link >= LINKS_PER_UNIT) {	/* eek! */
			rio_dprintk(RIO_DEBUG_CTRL, "RIO_HOST_LPB: Illegal link number %d\n", LpbReq.Link);
			p->RIOError.Error = LINK_NUMBER_OUT_OF_RANGE;
			return -EINVAL;
		}
		HostP = &p->RIOHosts[LpbReq.Host];

		if ((HostP->Flags & RUN_STATE) != RC_RUNNING) {
			rio_dprintk(RIO_DEBUG_CTRL, "RIO_HOST_LPB: Host %d not running\n", LpbReq.Host);
			p->RIOError.Error = HOST_NOT_RUNNING;
			return -EIO;
		}
		rio_dprintk(RIO_DEBUG_CTRL, "Request for lpb %d from host %d\n", LpbReq.Link, LpbReq.Host);

		if (copy_from_io(LpbReq.LpbP, &HostP->LinkStrP[LpbReq.Link], sizeof(struct LPB))) {
			rio_dprintk(RIO_DEBUG_CTRL, "RIO_HOST_LPB: Bad copy to user space\n");
			p->RIOError.Error = COPYOUT_FAILED;
			return -EFAULT;
		}
		return retval;

		/*
		 ** Here 3 IOCTL's that allow us to change the way in which
		 ** rio logs errors. send them just to syslog or send them
		 ** to both syslog and console or send them to just the console.
		 **
		 ** See RioStrBuf() in util.c for the other half.
		 */
	case RIO_SYSLOG_ONLY:
		p->RIOPrintLogState = PRINT_TO_LOG;	/* Just syslog */
		return 0;

	case RIO_SYSLOG_CONS:
		p->RIOPrintLogState = PRINT_TO_LOG_CONS;	/* syslog and console */
		return 0;

	case RIO_CONS_ONLY:
		p->RIOPrintLogState = PRINT_TO_CONS;	/* Just console */
		return 0;

	case RIO_SIGNALS_ON:
		if (p->RIOSignalProcess) {
			p->RIOError.Error = SIGNALS_ALREADY_SET;
			return -EBUSY;
		}
		/* FIXME: PID tracking */
		p->RIOSignalProcess = current->pid;
		p->RIOPrintDisabled = DONT_PRINT;
		return retval;

	case RIO_SIGNALS_OFF:
		if (p->RIOSignalProcess != current->pid) {
			p->RIOError.Error = NOT_RECEIVING_PROCESS;
			return -EPERM;
		}
		rio_dprintk(RIO_DEBUG_CTRL, "Clear signal process to zero\n");
		p->RIOSignalProcess = 0;
		return retval;

	case RIO_SET_BYTE_MODE:
		for (Host = 0; Host < p->RIONumHosts; Host++)
			if (p->RIOHosts[Host].Type == RIO_AT)
				p->RIOHosts[Host].Mode &= ~WORD_OPERATION;
		return retval;

	case RIO_SET_WORD_MODE:
		for (Host = 0; Host < p->RIONumHosts; Host++)
			if (p->RIOHosts[Host].Type == RIO_AT)
				p->RIOHosts[Host].Mode |= WORD_OPERATION;
		return retval;

	case RIO_SET_FAST_BUS:
		for (Host = 0; Host < p->RIONumHosts; Host++)
			if (p->RIOHosts[Host].Type == RIO_AT)
				p->RIOHosts[Host].Mode |= FAST_AT_BUS;
		return retval;

	case RIO_SET_SLOW_BUS:
		for (Host = 0; Host < p->RIONumHosts; Host++)
			if (p->RIOHosts[Host].Type == RIO_AT)
				p->RIOHosts[Host].Mode &= ~FAST_AT_BUS;
		return retval;

	case RIO_MAP_B50_TO_50:
	case RIO_MAP_B50_TO_57600:
	case RIO_MAP_B110_TO_110:
	case RIO_MAP_B110_TO_115200:
		rio_dprintk(RIO_DEBUG_CTRL, "Baud rate mapping\n");
		port = arg;
		if (port < 0 || port > 511) {
			rio_dprintk(RIO_DEBUG_CTRL, "Baud rate mapping: Bad port number %d\n", port);
			p->RIOError.Error = PORT_NUMBER_OUT_OF_RANGE;
			return -EINVAL;
		}
		rio_spin_lock_irqsave(&PortP->portSem, flags);
		switch (cmd) {
		case RIO_MAP_B50_TO_50:
			p->RIOPortp[port]->Config |= RIO_MAP_50_TO_50;
			break;
		case RIO_MAP_B50_TO_57600:
			p->RIOPortp[port]->Config &= ~RIO_MAP_50_TO_50;
			break;
		case RIO_MAP_B110_TO_110:
			p->RIOPortp[port]->Config |= RIO_MAP_110_TO_110;
			break;
		case RIO_MAP_B110_TO_115200:
			p->RIOPortp[port]->Config &= ~RIO_MAP_110_TO_110;
			break;
		}
		rio_spin_unlock_irqrestore(&PortP->portSem, flags);
		return retval;

	case RIO_STREAM_INFO:
		rio_dprintk(RIO_DEBUG_CTRL, "RIO_STREAM_INFO\n");
		return -EINVAL;

	case RIO_SEND_PACKET:
		rio_dprintk(RIO_DEBUG_CTRL, "RIO_SEND_PACKET\n");
		if (copy_from_user(&SendPack, argp, sizeof(SendPack))) {
			rio_dprintk(RIO_DEBUG_CTRL, "RIO_SEND_PACKET: Bad copy from user space\n");
			p->RIOError.Error = COPYIN_FAILED;
			return -EFAULT;
		}
		if (SendPack.PortNum >= 128) {
			p->RIOError.Error = PORT_NUMBER_OUT_OF_RANGE;
			return -ENXIO;
		}

		PortP = p->RIOPortp[SendPack.PortNum];
		rio_spin_lock_irqsave(&PortP->portSem, flags);

		if (!can_add_transmit(&PacketP, PortP)) {
			p->RIOError.Error = UNIT_IS_IN_USE;
			rio_spin_unlock_irqrestore(&PortP->portSem, flags);
			return -ENOSPC;
		}

		for (loop = 0; loop < (ushort) (SendPack.Len & 127); loop++)
			writeb(SendPack.Data[loop], &PacketP->data[loop]);

		writeb(SendPack.Len, &PacketP->len);

		add_transmit(PortP);
		/*
		 ** Count characters transmitted for port statistics reporting
		 */
		if (PortP->statsGather)
			PortP->txchars += (SendPack.Len & 127);
		rio_spin_unlock_irqrestore(&PortP->portSem, flags);
		return retval;

	case RIO_NO_MESG:
		if (su)
			p->RIONoMessage = 1;
		return su ? 0 : -EPERM;

	case RIO_MESG:
		if (su)
			p->RIONoMessage = 0;
		return su ? 0 : -EPERM;

	case RIO_WHAT_MESG:
		if (copy_to_user(argp, &p->RIONoMessage, sizeof(p->RIONoMessage))) {
			rio_dprintk(RIO_DEBUG_CTRL, "RIO_WHAT_MESG: Bad copy to user space\n");
			p->RIOError.Error = COPYOUT_FAILED;
			return -EFAULT;
		}
		return 0;

	case RIO_MEM_DUMP:
		if (copy_from_user(&SubCmd, argp, sizeof(struct SubCmdStruct))) {
			p->RIOError.Error = COPYIN_FAILED;
			return -EFAULT;
		}
		rio_dprintk(RIO_DEBUG_CTRL, "RIO_MEM_DUMP host %d rup %d addr %x\n", SubCmd.Host, SubCmd.Rup, SubCmd.Addr);

		if (SubCmd.Rup >= MAX_RUP + LINKS_PER_UNIT) {
			p->RIOError.Error = RUP_NUMBER_OUT_OF_RANGE;
			return -EINVAL;
		}

		if (SubCmd.Host >= p->RIONumHosts) {
			p->RIOError.Error = HOST_NUMBER_OUT_OF_RANGE;
			return -EINVAL;
		}

		port = p->RIOHosts[SubCmd.Host].UnixRups[SubCmd.Rup].BaseSysPort;

		PortP = p->RIOPortp[port];

		rio_spin_lock_irqsave(&PortP->portSem, flags);

		if (RIOPreemptiveCmd(p, PortP, RIOC_MEMDUMP) == RIO_FAIL) {
			rio_dprintk(RIO_DEBUG_CTRL, "RIO_MEM_DUMP failed\n");
			rio_spin_unlock_irqrestore(&PortP->portSem, flags);
			return -EBUSY;
		} else
			PortP->State |= RIO_BUSY;

		rio_spin_unlock_irqrestore(&PortP->portSem, flags);
		if (copy_to_user(argp, p->RIOMemDump, MEMDUMP_SIZE)) {
			rio_dprintk(RIO_DEBUG_CTRL, "RIO_MEM_DUMP copy failed\n");
			p->RIOError.Error = COPYOUT_FAILED;
			return -EFAULT;
		}
		return 0;

	case RIO_TICK:
		if (arg >= p->RIONumHosts)
			return -EINVAL;
		rio_dprintk(RIO_DEBUG_CTRL, "Set interrupt for host %ld\n", arg);
		writeb(0xFF, &p->RIOHosts[arg].SetInt);
		return 0;

	case RIO_TOCK:
		if (arg >= p->RIONumHosts)
			return -EINVAL;
		rio_dprintk(RIO_DEBUG_CTRL, "Clear interrupt for host %ld\n", arg);
		writeb(0xFF, &p->RIOHosts[arg].ResetInt);
		return 0;

	case RIO_READ_CHECK:
		/* Check reads for pkts with data[0] the same */
		p->RIOReadCheck = !p->RIOReadCheck;
		if (copy_to_user(argp, &p->RIOReadCheck, sizeof(unsigned int))) {
			p->RIOError.Error = COPYOUT_FAILED;
			return -EFAULT;
		}
		return 0;

	case RIO_READ_REGISTER:
		if (copy_from_user(&SubCmd, argp, sizeof(struct SubCmdStruct))) {
			p->RIOError.Error = COPYIN_FAILED;
			return -EFAULT;
		}
		rio_dprintk(RIO_DEBUG_CTRL, "RIO_READ_REGISTER host %d rup %d port %d reg %x\n", SubCmd.Host, SubCmd.Rup, SubCmd.Port, SubCmd.Addr);

		if (SubCmd.Port > 511) {
			rio_dprintk(RIO_DEBUG_CTRL, "Baud rate mapping: Bad port number %d\n", SubCmd.Port);
			p->RIOError.Error = PORT_NUMBER_OUT_OF_RANGE;
			return -EINVAL;
		}

		if (SubCmd.Rup >= MAX_RUP + LINKS_PER_UNIT) {
			p->RIOError.Error = RUP_NUMBER_OUT_OF_RANGE;
			return -EINVAL;
		}

		if (SubCmd.Host >= p->RIONumHosts) {
			p->RIOError.Error = HOST_NUMBER_OUT_OF_RANGE;
			return -EINVAL;
		}

		port = p->RIOHosts[SubCmd.Host].UnixRups[SubCmd.Rup].BaseSysPort + SubCmd.Port;
		PortP = p->RIOPortp[port];

		rio_spin_lock_irqsave(&PortP->portSem, flags);

		if (RIOPreemptiveCmd(p, PortP, RIOC_READ_REGISTER) ==
				RIO_FAIL) {
			rio_dprintk(RIO_DEBUG_CTRL, "RIO_READ_REGISTER failed\n");
			rio_spin_unlock_irqrestore(&PortP->portSem, flags);
			return -EBUSY;
		} else
			PortP->State |= RIO_BUSY;

		rio_spin_unlock_irqrestore(&PortP->portSem, flags);
		if (copy_to_user(argp, &p->CdRegister, sizeof(unsigned int))) {
			rio_dprintk(RIO_DEBUG_CTRL, "RIO_READ_REGISTER copy failed\n");
			p->RIOError.Error = COPYOUT_FAILED;
			return -EFAULT;
		}
		return 0;
		/*
		 ** rio_make_dev: given port number (0-511) ORed with port type
		 ** (RIO_DEV_DIRECT, RIO_DEV_MODEM, RIO_DEV_XPRINT) return dev_t
		 ** value to pass to mknod to create the correct device node.
		 */
	case RIO_MAKE_DEV:
		{
			unsigned int port = arg & RIO_MODEM_MASK;
			unsigned int ret;

			switch (arg & RIO_DEV_MASK) {
			case RIO_DEV_DIRECT:
				ret = drv_makedev(MAJOR(dev), port);
				rio_dprintk(RIO_DEBUG_CTRL, "Makedev direct 0x%x is 0x%x\n", port, ret);
				return ret;
			case RIO_DEV_MODEM:
				ret = drv_makedev(MAJOR(dev), (port | RIO_MODEM_BIT));
				rio_dprintk(RIO_DEBUG_CTRL, "Makedev modem 0x%x is 0x%x\n", port, ret);
				return ret;
			case RIO_DEV_XPRINT:
				ret = drv_makedev(MAJOR(dev), port);
				rio_dprintk(RIO_DEBUG_CTRL, "Makedev printer 0x%x is 0x%x\n", port, ret);
				return ret;
			}
			rio_dprintk(RIO_DEBUG_CTRL, "MAKE Device is called\n");
			return -EINVAL;
		}
		/*
		 ** rio_minor: given a dev_t from a stat() call, return
		 ** the port number (0-511) ORed with the port type
		 ** ( RIO_DEV_DIRECT, RIO_DEV_MODEM, RIO_DEV_XPRINT )
		 */
	case RIO_MINOR:
		{
			dev_t dv;
			int mino;
			unsigned long ret;

			dv = (dev_t) (arg);
			mino = RIO_UNMODEM(dv);

			if (RIO_ISMODEM(dv)) {
				rio_dprintk(RIO_DEBUG_CTRL, "Minor for device 0x%x: modem %d\n", dv, mino);
				ret = mino | RIO_DEV_MODEM;
			} else {
				rio_dprintk(RIO_DEBUG_CTRL, "Minor for device 0x%x: direct %d\n", dv, mino);
				ret = mino | RIO_DEV_DIRECT;
			}
			return ret;
		}
	}
	rio_dprintk(RIO_DEBUG_CTRL, "INVALID DAEMON IOCTL 0x%x\n", cmd);
	p->RIOError.Error = IOCTL_COMMAND_UNKNOWN;

	func_exit();
	return -EINVAL;
}

/*
** Pre-emptive commands go on RUPs and are only one byte long.
*/
int RIOPreemptiveCmd(struct rio_info *p, struct Port *PortP, u8 Cmd)
{
	struct CmdBlk *CmdBlkP;
	struct PktCmd_M *PktCmdP;
	int Ret;
	ushort rup;
	int port;

	if (PortP->State & RIO_DELETED) {
		rio_dprintk(RIO_DEBUG_CTRL, "Preemptive command to deleted RTA ignored\n");
		return RIO_FAIL;
	}

	if ((PortP->InUse == (typeof(PortP->InUse))-1) ||
			!(CmdBlkP = RIOGetCmdBlk())) {
		rio_dprintk(RIO_DEBUG_CTRL, "Cannot allocate command block "
			"for command %d on port %d\n", Cmd, PortP->PortNum);
		return RIO_FAIL;
	}

	rio_dprintk(RIO_DEBUG_CTRL, "Command blk %p - InUse now %d\n",
			CmdBlkP, PortP->InUse);

	PktCmdP = (struct PktCmd_M *)&CmdBlkP->Packet.data[0];

	CmdBlkP->Packet.src_unit = 0;
	if (PortP->SecondBlock)
		rup = PortP->ID2;
	else
		rup = PortP->RupNum;
	CmdBlkP->Packet.dest_unit = rup;
	CmdBlkP->Packet.src_port = COMMAND_RUP;
	CmdBlkP->Packet.dest_port = COMMAND_RUP;
	CmdBlkP->Packet.len = PKT_CMD_BIT | 2;
	CmdBlkP->PostFuncP = RIOUnUse;
	CmdBlkP->PostArg = (unsigned long) PortP;
	PktCmdP->Command = Cmd;
	port = PortP->HostPort % (ushort) PORTS_PER_RTA;
	/*
	 ** Index ports 8-15 for 2nd block of 16 port RTA.
	 */
	if (PortP->SecondBlock)
		port += (ushort) PORTS_PER_RTA;
	PktCmdP->PhbNum = port;

	switch (Cmd) {
	case RIOC_MEMDUMP:
		rio_dprintk(RIO_DEBUG_CTRL, "Queue MEMDUMP command blk %p "
				"(addr 0x%x)\n", CmdBlkP, (int) SubCmd.Addr);
		PktCmdP->SubCommand = RIOC_MEMDUMP;
		PktCmdP->SubAddr = SubCmd.Addr;
		break;
	case RIOC_FCLOSE:
		rio_dprintk(RIO_DEBUG_CTRL, "Queue FCLOSE command blk %p\n",
				CmdBlkP);
		break;
	case RIOC_READ_REGISTER:
		rio_dprintk(RIO_DEBUG_CTRL, "Queue READ_REGISTER (0x%x) "
				"command blk %p\n", (int) SubCmd.Addr, CmdBlkP);
		PktCmdP->SubCommand = RIOC_READ_REGISTER;
		PktCmdP->SubAddr = SubCmd.Addr;
		break;
	case RIOC_RESUME:
		rio_dprintk(RIO_DEBUG_CTRL, "Queue RESUME command blk %p\n",
				CmdBlkP);
		break;
	case RIOC_RFLUSH:
		rio_dprintk(RIO_DEBUG_CTRL, "Queue RFLUSH command blk %p\n",
				CmdBlkP);
		CmdBlkP->PostFuncP = RIORFlushEnable;
		break;
	case RIOC_SUSPEND:
		rio_dprintk(RIO_DEBUG_CTRL, "Queue SUSPEND command blk %p\n",
				CmdBlkP);
		break;

	case RIOC_MGET:
		rio_dprintk(RIO_DEBUG_CTRL, "Queue MGET command blk %p\n",
				CmdBlkP);
		break;

	case RIOC_MSET:
	case RIOC_MBIC:
	case RIOC_MBIS:
		CmdBlkP->Packet.data[4] = (char) PortP->ModemLines;
		rio_dprintk(RIO_DEBUG_CTRL, "Queue MSET/MBIC/MBIS command "
				"blk %p\n", CmdBlkP);
		break;

	case RIOC_WFLUSH:
		/*
		 ** If we have queued up the maximum number of Write flushes
		 ** allowed then we should not bother sending any more to the
		 ** RTA.
		 */
		if (PortP->WflushFlag == (typeof(PortP->WflushFlag))-1) {
			rio_dprintk(RIO_DEBUG_CTRL, "Trashed WFLUSH, "
					"WflushFlag about to wrap!");
			RIOFreeCmdBlk(CmdBlkP);
			return (RIO_FAIL);
		} else {
			rio_dprintk(RIO_DEBUG_CTRL, "Queue WFLUSH command "
					"blk %p\n", CmdBlkP);
			CmdBlkP->PostFuncP = RIOWFlushMark;
		}
		break;
	}

	PortP->InUse++;

	Ret = RIOQueueCmdBlk(PortP->HostP, rup, CmdBlkP);

	return Ret;
}
