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
**	Module		: rioparam.c
**	SID		: 1.3
**	Last Modified	: 11/6/98 10:33:45
**	Retrieved	: 11/6/98 10:33:50
**
**  ident @(#)rioparam.c	1.3
**
** -----------------------------------------------------------------------------
*/

#ifdef SCCS_LABELS
static char *_rioparam_c_sccs_ = "@(#)rioparam.c	1.3";
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



/*
** The Scam, based on email from jeremyr@bugs.specialix.co.uk....
**
** To send a command on a particular port, you put a packet with the
** command bit set onto the port. The command bit is in the len field,
** and gets ORed in with the actual byte count.
**
** When you send a packet with the command bit set, then the first
** data byte ( data[0] ) is interpretted as the command to execute.
** It also governs what data structure overlay should accompany the packet.
** Commands are defined in cirrus/cirrus.h
**
** If you want the command to pre-emt data already on the queue for the
** port, set the pre-emptive bit in conjunction with the command bit.
** It is not defined what will happen if you set the preemptive bit
** on a packet that is NOT a command.
**
** Pre-emptive commands should be queued at the head of the queue using
** add_start(), whereas normal commands and data are enqueued using
** add_end().
**
** Most commands do not use the remaining bytes in the data array. The
** exceptions are OPEN MOPEN and CONFIG. (NB. As with the SI CONFIG and
** OPEN are currently analagous). With these three commands the following
** 11 data bytes are all used to pass config information such as baud rate etc.
** The fields are also defined in cirrus.h. Some contain straightforward
** information such as the transmit XON character. Two contain the transmit and
** receive baud rates respectively. For most baud rates there is a direct
** mapping between the rates defined in <sys/termio.h> and the byte in the
** packet. There are additional (non UNIX-standard) rates defined in
** /u/dos/rio/cirrus/h/brates.h.
**
** The rest of the data fields contain approximations to the Cirrus registers
** that are used to program number of bits etc. Each registers bit fields is
** defined in cirrus.h.
** 
** NB. Only use those bits that are defined as being driver specific
** or common to the RTA and the driver.
** 
** All commands going from RTA->Host will be dealt with by the Host code - you
** will never see them. As with the SI there will be three fields to look out
** for in each phb (not yet defined - needs defining a.s.a.p).
** 
** modem_status	- current state of handshake pins.
**
** port_status	 - current port status - equivalent to hi_stat for SI, indicates
** if port is IDLE_OPEN, IDLE_CLOSED etc.
**
** break_status	- bit X set if break has been received.
** 
** Happy hacking.
** 
*/

/* 
** RIOParam is used to open or configure a port. You pass it a PortP,
** which will have a tty struct attached to it. You also pass a command,
** either OPEN or CONFIG. The port's setup is taken from the t_ fields
** of the tty struct inside the PortP, and the port is either opened
** or re-configured. You must also tell RIOParam if the device is a modem
** device or not (i.e. top bit of minor number set or clear - take special
** care when deciding on this!).
** RIOParam neither flushes nor waits for drain, and is NOT preemptive.
**
** RIOParam assumes it will be called at splrio(), and also assumes
** that CookMode is set correctly in the port structure.
**
** NB. for MPX
**	tty lock must NOT have been previously acquired.
*/
int RIOParam(PortP, cmd, Modem, SleepFlag)
struct Port *PortP;
int cmd;
int Modem;
int SleepFlag;
{
	register struct tty_struct *TtyP;
	int retval;
	register struct phb_param *phb_param_ptr;
	PKT *PacketP;
	int res;
	uchar Cor1 = 0, Cor2 = 0, Cor4 = 0, Cor5 = 0;
	uchar TxXon = 0, TxXoff = 0, RxXon = 0, RxXoff = 0;
	uchar LNext = 0, TxBaud = 0, RxBaud = 0;
	int retries = 0xff;
	unsigned long flags;

	func_enter();

	TtyP = PortP->gs.tty;

	rio_dprintk(RIO_DEBUG_PARAM, "RIOParam: Port:%d cmd:%d Modem:%d SleepFlag:%d Mapped: %d, tty=%p\n", PortP->PortNum, cmd, Modem, SleepFlag, PortP->Mapped, TtyP);

	if (!TtyP) {
		rio_dprintk(RIO_DEBUG_PARAM, "Can't call rioparam with null tty.\n");

		func_exit();

		return RIO_FAIL;
	}
	rio_spin_lock_irqsave(&PortP->portSem, flags);

	if (cmd == OPEN) {
		/*
		 ** If the port is set to store or lock the parameters, and it is
		 ** paramed with OPEN, we want to restore the saved port termio, but
		 ** only if StoredTermio has been saved, i.e. NOT 1st open after reboot.
		 */
	}

	/*
	 ** wait for space
	 */
	while (!(res = can_add_transmit(&PacketP, PortP)) || (PortP->InUse != NOT_INUSE)) {
		if (retries-- <= 0) {
			break;
		}
		if (PortP->InUse != NOT_INUSE) {
			rio_dprintk(RIO_DEBUG_PARAM, "Port IN_USE for pre-emptive command\n");
		}

		if (!res) {
			rio_dprintk(RIO_DEBUG_PARAM, "Port has no space on transmit queue\n");
		}

		if (SleepFlag != OK_TO_SLEEP) {
			rio_spin_unlock_irqrestore(&PortP->portSem, flags);
			func_exit();

			return RIO_FAIL;
		}

		rio_dprintk(RIO_DEBUG_PARAM, "wait for can_add_transmit\n");
		rio_spin_unlock_irqrestore(&PortP->portSem, flags);
		retval = RIODelay(PortP, HUNDRED_MS);
		rio_spin_lock_irqsave(&PortP->portSem, flags);
		if (retval == RIO_FAIL) {
			rio_dprintk(RIO_DEBUG_PARAM, "wait for can_add_transmit broken by signal\n");
			rio_spin_unlock_irqrestore(&PortP->portSem, flags);
			pseterr(EINTR);
			func_exit();

			return RIO_FAIL;
		}
		if (PortP->State & RIO_DELETED) {
			rio_spin_unlock_irqrestore(&PortP->portSem, flags);
			func_exit();

			return RIO_SUCCESS;
		}
	}

	if (!res) {
		rio_spin_unlock_irqrestore(&PortP->portSem, flags);
		func_exit();

		return RIO_FAIL;
	}

	rio_dprintk(RIO_DEBUG_PARAM, "can_add_transmit() returns %x\n", res);
	rio_dprintk(RIO_DEBUG_PARAM, "Packet is 0x%x\n", (int) PacketP);

	phb_param_ptr = (struct phb_param *) PacketP->data;


	switch (TtyP->termios->c_cflag & CSIZE) {
	case CS5:
		{
			rio_dprintk(RIO_DEBUG_PARAM, "5 bit data\n");
			Cor1 |= COR1_5BITS;
			break;
		}
	case CS6:
		{
			rio_dprintk(RIO_DEBUG_PARAM, "6 bit data\n");
			Cor1 |= COR1_6BITS;
			break;
		}
	case CS7:
		{
			rio_dprintk(RIO_DEBUG_PARAM, "7 bit data\n");
			Cor1 |= COR1_7BITS;
			break;
		}
	case CS8:
		{
			rio_dprintk(RIO_DEBUG_PARAM, "8 bit data\n");
			Cor1 |= COR1_8BITS;
			break;
		}
	}

	if (TtyP->termios->c_cflag & CSTOPB) {
		rio_dprintk(RIO_DEBUG_PARAM, "2 stop bits\n");
		Cor1 |= COR1_2STOP;
	} else {
		rio_dprintk(RIO_DEBUG_PARAM, "1 stop bit\n");
		Cor1 |= COR1_1STOP;
	}

	if (TtyP->termios->c_cflag & PARENB) {
		rio_dprintk(RIO_DEBUG_PARAM, "Enable parity\n");
		Cor1 |= COR1_NORMAL;
	} else {
		rio_dprintk(RIO_DEBUG_PARAM, "Disable parity\n");
		Cor1 |= COR1_NOP;
	}
	if (TtyP->termios->c_cflag & PARODD) {
		rio_dprintk(RIO_DEBUG_PARAM, "Odd parity\n");
		Cor1 |= COR1_ODD;
	} else {
		rio_dprintk(RIO_DEBUG_PARAM, "Even parity\n");
		Cor1 |= COR1_EVEN;
	}

	/*
	 ** COR 2
	 */
	if (TtyP->termios->c_iflag & IXON) {
		rio_dprintk(RIO_DEBUG_PARAM, "Enable start/stop output control\n");
		Cor2 |= COR2_IXON;
	} else {
		if (PortP->Config & RIO_IXON) {
			rio_dprintk(RIO_DEBUG_PARAM, "Force enable start/stop output control\n");
			Cor2 |= COR2_IXON;
		} else
			rio_dprintk(RIO_DEBUG_PARAM, "IXON has been disabled.\n");
	}

	if (TtyP->termios->c_iflag & IXANY) {
		if (PortP->Config & RIO_IXANY) {
			rio_dprintk(RIO_DEBUG_PARAM, "Enable any key to restart output\n");
			Cor2 |= COR2_IXANY;
		} else
			rio_dprintk(RIO_DEBUG_PARAM, "IXANY has been disabled due to sanity reasons.\n");
	}

	if (TtyP->termios->c_iflag & IXOFF) {
		rio_dprintk(RIO_DEBUG_PARAM, "Enable start/stop input control 2\n");
		Cor2 |= COR2_IXOFF;
	}

	if (TtyP->termios->c_cflag & HUPCL) {
		rio_dprintk(RIO_DEBUG_PARAM, "Hangup on last close\n");
		Cor2 |= COR2_HUPCL;
	}

	if (C_CRTSCTS(TtyP)) {
		rio_dprintk(RIO_DEBUG_PARAM, "Rx hardware flow control enabled\n");
		Cor2 |= COR2_CTSFLOW;
		Cor2 |= COR2_RTSFLOW;
	} else {
		rio_dprintk(RIO_DEBUG_PARAM, "Rx hardware flow control disabled\n");
		Cor2 &= ~COR2_CTSFLOW;
		Cor2 &= ~COR2_RTSFLOW;
	}


	if (TtyP->termios->c_cflag & CLOCAL) {
		rio_dprintk(RIO_DEBUG_PARAM, "Local line\n");
	} else {
		rio_dprintk(RIO_DEBUG_PARAM, "Possible Modem line\n");
	}

	/*
	 ** COR 4 (there is no COR 3)
	 */
	if (TtyP->termios->c_iflag & IGNBRK) {
		rio_dprintk(RIO_DEBUG_PARAM, "Ignore break condition\n");
		Cor4 |= COR4_IGNBRK;
	}
	if (!(TtyP->termios->c_iflag & BRKINT)) {
		rio_dprintk(RIO_DEBUG_PARAM, "Break generates NULL condition\n");
		Cor4 |= COR4_NBRKINT;
	} else {
		rio_dprintk(RIO_DEBUG_PARAM, "Interrupt on	break condition\n");
	}

	if (TtyP->termios->c_iflag & INLCR) {
		rio_dprintk(RIO_DEBUG_PARAM, "Map newline to carriage return on input\n");
		Cor4 |= COR4_INLCR;
	}

	if (TtyP->termios->c_iflag & IGNCR) {
		rio_dprintk(RIO_DEBUG_PARAM, "Ignore carriage return on input\n");
		Cor4 |= COR4_IGNCR;
	}

	if (TtyP->termios->c_iflag & ICRNL) {
		rio_dprintk(RIO_DEBUG_PARAM, "Map carriage return to newline on input\n");
		Cor4 |= COR4_ICRNL;
	}
	if (TtyP->termios->c_iflag & IGNPAR) {
		rio_dprintk(RIO_DEBUG_PARAM, "Ignore characters with parity errors\n");
		Cor4 |= COR4_IGNPAR;
	}
	if (TtyP->termios->c_iflag & PARMRK) {
		rio_dprintk(RIO_DEBUG_PARAM, "Mark parity errors\n");
		Cor4 |= COR4_PARMRK;
	}

	/*
	 ** Set the RAISEMOD flag to ensure that the modem lines are raised
	 ** on reception of a config packet.
	 ** The download code handles the zero baud condition.
	 */
	Cor4 |= COR4_RAISEMOD;

	/*
	 ** COR 5
	 */

	Cor5 = COR5_CMOE;

	/*
	 ** Set to monitor tbusy/tstop (or not).
	 */

	if (PortP->MonitorTstate)
		Cor5 |= COR5_TSTATE_ON;
	else
		Cor5 |= COR5_TSTATE_OFF;

	/*
	 ** Could set LNE here if you wanted LNext processing. SVR4 will use it.
	 */
	if (TtyP->termios->c_iflag & ISTRIP) {
		rio_dprintk(RIO_DEBUG_PARAM, "Strip input characters\n");
		if (!(PortP->State & RIO_TRIAD_MODE)) {
			Cor5 |= COR5_ISTRIP;
		}
	}

	if (TtyP->termios->c_oflag & ONLCR) {
		rio_dprintk(RIO_DEBUG_PARAM, "Map newline to carriage-return, newline on output\n");
		if (PortP->CookMode == COOK_MEDIUM)
			Cor5 |= COR5_ONLCR;
	}
	if (TtyP->termios->c_oflag & OCRNL) {
		rio_dprintk(RIO_DEBUG_PARAM, "Map carriage return to newline on output\n");
		if (PortP->CookMode == COOK_MEDIUM)
			Cor5 |= COR5_OCRNL;
	}
	if ((TtyP->termios->c_oflag & TABDLY) == TAB3) {
		rio_dprintk(RIO_DEBUG_PARAM, "Tab delay 3 set\n");
		if (PortP->CookMode == COOK_MEDIUM)
			Cor5 |= COR5_TAB3;
	}

	/*
	 ** Flow control bytes.
	 */
	TxXon = TtyP->termios->c_cc[VSTART];
	TxXoff = TtyP->termios->c_cc[VSTOP];
	RxXon = TtyP->termios->c_cc[VSTART];
	RxXoff = TtyP->termios->c_cc[VSTOP];
	/*
	 ** LNEXT byte
	 */
	LNext = 0;

	/*
	 ** Baud rate bytes
	 */
	rio_dprintk(RIO_DEBUG_PARAM, "Mapping of rx/tx baud %x (%x)\n", TtyP->termios->c_cflag, CBAUD);

	switch (TtyP->termios->c_cflag & CBAUD) {
#define e(b) case B ## b : RxBaud = TxBaud = RIO_B ## b ;break
		e(50);
		e(75);
		e(110);
		e(134);
		e(150);
		e(200);
		e(300);
		e(600);
		e(1200);
		e(1800);
		e(2400);
		e(4800);
		e(9600);
		e(19200);
		e(38400);
		e(57600);
		e(115200);	/* e(230400);e(460800); e(921600);  */
	}

	/* XXX MIssing conversion table. XXX */
	/*       (TtyP->termios->c_cflag & V_CBAUD); */

	rio_dprintk(RIO_DEBUG_PARAM, "tx baud 0x%x, rx baud 0x%x\n", TxBaud, RxBaud);


	/*
	 ** Leftovers
	 */
	if (TtyP->termios->c_cflag & CREAD)
		rio_dprintk(RIO_DEBUG_PARAM, "Enable receiver\n");
#ifdef RCV1EN
	if (TtyP->termios->c_cflag & RCV1EN)
		rio_dprintk(RIO_DEBUG_PARAM, "RCV1EN (?)\n");
#endif
#ifdef XMT1EN
	if (TtyP->termios->c_cflag & XMT1EN)
		rio_dprintk(RIO_DEBUG_PARAM, "XMT1EN (?)\n");
#endif
	if (TtyP->termios->c_lflag & ISIG)
		rio_dprintk(RIO_DEBUG_PARAM, "Input character signal generating enabled\n");
	if (TtyP->termios->c_lflag & ICANON)
		rio_dprintk(RIO_DEBUG_PARAM, "Canonical input: erase and kill enabled\n");
	if (TtyP->termios->c_lflag & XCASE)
		rio_dprintk(RIO_DEBUG_PARAM, "Canonical upper/lower presentation\n");
	if (TtyP->termios->c_lflag & ECHO)
		rio_dprintk(RIO_DEBUG_PARAM, "Enable input echo\n");
	if (TtyP->termios->c_lflag & ECHOE)
		rio_dprintk(RIO_DEBUG_PARAM, "Enable echo erase\n");
	if (TtyP->termios->c_lflag & ECHOK)
		rio_dprintk(RIO_DEBUG_PARAM, "Enable echo kill\n");
	if (TtyP->termios->c_lflag & ECHONL)
		rio_dprintk(RIO_DEBUG_PARAM, "Enable echo newline\n");
	if (TtyP->termios->c_lflag & NOFLSH)
		rio_dprintk(RIO_DEBUG_PARAM, "Disable flush after interrupt or quit\n");
#ifdef TOSTOP
	if (TtyP->termios->c_lflag & TOSTOP)
		rio_dprintk(RIO_DEBUG_PARAM, "Send SIGTTOU for background output\n");
#endif
#ifdef XCLUDE
	if (TtyP->termios->c_lflag & XCLUDE)
		rio_dprintk(RIO_DEBUG_PARAM, "Exclusive use of this line\n");
#endif
	if (TtyP->termios->c_iflag & IUCLC)
		rio_dprintk(RIO_DEBUG_PARAM, "Map uppercase to lowercase on input\n");
	if (TtyP->termios->c_oflag & OPOST)
		rio_dprintk(RIO_DEBUG_PARAM, "Enable output post-processing\n");
	if (TtyP->termios->c_oflag & OLCUC)
		rio_dprintk(RIO_DEBUG_PARAM, "Map lowercase to uppercase on output\n");
	if (TtyP->termios->c_oflag & ONOCR)
		rio_dprintk(RIO_DEBUG_PARAM, "No carriage return output at column 0\n");
	if (TtyP->termios->c_oflag & ONLRET)
		rio_dprintk(RIO_DEBUG_PARAM, "Newline performs carriage return function\n");
	if (TtyP->termios->c_oflag & OFILL)
		rio_dprintk(RIO_DEBUG_PARAM, "Use fill characters for delay\n");
	if (TtyP->termios->c_oflag & OFDEL)
		rio_dprintk(RIO_DEBUG_PARAM, "Fill character is DEL\n");
	if (TtyP->termios->c_oflag & NLDLY)
		rio_dprintk(RIO_DEBUG_PARAM, "Newline delay set\n");
	if (TtyP->termios->c_oflag & CRDLY)
		rio_dprintk(RIO_DEBUG_PARAM, "Carriage return delay set\n");
	if (TtyP->termios->c_oflag & TABDLY)
		rio_dprintk(RIO_DEBUG_PARAM, "Tab delay set\n");
	/*
	 ** These things are kind of useful in a later life!
	 */
	PortP->Cor2Copy = Cor2;

	if (PortP->State & RIO_DELETED) {
		rio_spin_unlock_irqrestore(&PortP->portSem, flags);
		func_exit();

		return RIO_FAIL;
	}

	/*
	 ** Actually write the info into the packet to be sent
	 */
	WBYTE(phb_param_ptr->Cmd, cmd);
	WBYTE(phb_param_ptr->Cor1, Cor1);
	WBYTE(phb_param_ptr->Cor2, Cor2);
	WBYTE(phb_param_ptr->Cor4, Cor4);
	WBYTE(phb_param_ptr->Cor5, Cor5);
	WBYTE(phb_param_ptr->TxXon, TxXon);
	WBYTE(phb_param_ptr->RxXon, RxXon);
	WBYTE(phb_param_ptr->TxXoff, TxXoff);
	WBYTE(phb_param_ptr->RxXoff, RxXoff);
	WBYTE(phb_param_ptr->LNext, LNext);
	WBYTE(phb_param_ptr->TxBaud, TxBaud);
	WBYTE(phb_param_ptr->RxBaud, RxBaud);

	/*
	 ** Set the length/command field
	 */
	WBYTE(PacketP->len, 12 | PKT_CMD_BIT);

	/*
	 ** The packet is formed - now, whack it off
	 ** to its final destination:
	 */
	add_transmit(PortP);
	/*
	 ** Count characters transmitted for port statistics reporting
	 */
	if (PortP->statsGather)
		PortP->txchars += 12;

	rio_spin_unlock_irqrestore(&PortP->portSem, flags);

	rio_dprintk(RIO_DEBUG_PARAM, "add_transmit returned.\n");
	/*
	 ** job done.
	 */
	func_exit();

	return RIO_SUCCESS;
}


/*
** We can add another packet to a transmit queue if the packet pointer pointed
** to by the TxAdd pointer has PKT_IN_USE clear in its address.
*/
int can_add_transmit(PktP, PortP)
PKT **PktP;
struct Port *PortP;
{
	register PKT *tp;

	*PktP = tp = (PKT *) RIO_PTR(PortP->Caddr, RWORD(*PortP->TxAdd));

	return !((uint) tp & PKT_IN_USE);
}

/*
** To add a packet to the queue, you set the PKT_IN_USE bit in the address,
** and then move the TxAdd pointer along one position to point to the next
** packet pointer. You must wrap the pointer from the end back to the start.
*/
void add_transmit(PortP)
struct Port *PortP;
{
	if (RWORD(*PortP->TxAdd) & PKT_IN_USE) {
		rio_dprintk(RIO_DEBUG_PARAM, "add_transmit: Packet has been stolen!");
	}
	WWORD(*(ushort *) PortP->TxAdd, RWORD(*PortP->TxAdd) | PKT_IN_USE);
	PortP->TxAdd = (PortP->TxAdd == PortP->TxEnd) ? PortP->TxStart : PortP->TxAdd + 1;
	WWORD(PortP->PhbP->tx_add, RIO_OFF(PortP->Caddr, PortP->TxAdd));
}

/****************************************
 * Put a packet onto the end of the
 * free list
 ****************************************/
void put_free_end(HostP, PktP)
struct Host *HostP;
PKT *PktP;
{
	FREE_LIST *tmp_pointer;
	ushort old_end, new_end;
	unsigned long flags;

	rio_spin_lock_irqsave(&HostP->HostLock, flags);

	 /*************************************************
	* Put a packet back onto the back of the free list
	*
	************************************************/

	rio_dprintk(RIO_DEBUG_PFE, "put_free_end(PktP=%x)\n", (int) PktP);

	if ((old_end = RWORD(HostP->ParmMapP->free_list_end)) != TPNULL) {
		new_end = RIO_OFF(HostP->Caddr, PktP);
		tmp_pointer = (FREE_LIST *) RIO_PTR(HostP->Caddr, old_end);
		WWORD(tmp_pointer->next, new_end);
		WWORD(((FREE_LIST *) PktP)->prev, old_end);
		WWORD(((FREE_LIST *) PktP)->next, TPNULL);
		WWORD(HostP->ParmMapP->free_list_end, new_end);
	} else {		/* First packet on the free list this should never happen! */
		rio_dprintk(RIO_DEBUG_PFE, "put_free_end(): This should never happen\n");
		WWORD(HostP->ParmMapP->free_list_end, RIO_OFF(HostP->Caddr, PktP));
		tmp_pointer = (FREE_LIST *) PktP;
		WWORD(tmp_pointer->prev, TPNULL);
		WWORD(tmp_pointer->next, TPNULL);
	}
	rio_dprintk(RIO_DEBUG_CMD, "Before unlock: %p\n", &HostP->HostLock);
	rio_spin_unlock_irqrestore(&HostP->HostLock, flags);
}

/*
** can_remove_receive(PktP,P) returns non-zero if PKT_IN_USE is set
** for the next packet on the queue. It will also set PktP to point to the
** relevant packet, [having cleared the PKT_IN_USE bit]. If PKT_IN_USE is clear,
** then can_remove_receive() returns 0.
*/
int can_remove_receive(PktP, PortP)
PKT **PktP;
struct Port *PortP;
{
	if (RWORD(*PortP->RxRemove) & PKT_IN_USE) {
		*PktP = (PKT *) RIO_PTR(PortP->Caddr, RWORD(*PortP->RxRemove) & ~PKT_IN_USE);
		return 1;
	}
	return 0;
}

/*
** To remove a packet from the receive queue you clear its PKT_IN_USE bit,
** and then bump the pointers. Once the pointers get to the end, they must
** be wrapped back to the start.
*/
void remove_receive(PortP)
struct Port *PortP;
{
	WWORD(*PortP->RxRemove, RWORD(*PortP->RxRemove) & ~PKT_IN_USE);
	PortP->RxRemove = (PortP->RxRemove == PortP->RxEnd) ? PortP->RxStart : PortP->RxRemove + 1;
	WWORD(PortP->PhbP->rx_remove, RIO_OFF(PortP->Caddr, PortP->RxRemove));
}
