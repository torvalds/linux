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
**	Module		: rioinit.c
**	SID		: 1.3
**	Last Modified	: 11/6/98 10:33:43
**	Retrieved	: 11/6/98 10:33:49
**
**  ident @(#)rioinit.c	1.3
**
** -----------------------------------------------------------------------------
*/
#ifdef SCCS_LABELS
static char *_rioinit_c_sccs_ = "@(#)rioinit.c	1.3";
#endif

#include <linux/config.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <asm/io.h>
#include <asm/system.h>
#include <asm/string.h>
#include <asm/semaphore.h>
#include <asm/uaccess.h>

#include <linux/termios.h>
#include <linux/serial.h>

#include <linux/generic_serial.h>


#include "linux_compat.h"
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
#include "rio_linux.h"

#undef bcopy
#define bcopy rio_pcicopy

int RIOPCIinit(struct rio_info *p, int Mode);

#if 0
static void RIOAllocateInterrupts(struct rio_info *);
static int RIOReport(struct rio_info *);
static void RIOStopInterrupts(struct rio_info *, int, int);
#endif

static int RIOScrub(int, BYTE *, int);

#if 0
extern int	rio_intr();

/*
**	Init time code.
*/
void
rioinit( p, info )
struct rio_info		* p;
struct RioHostInfo	* info;
{
	/*
	** Multi-Host card support - taking the easy way out - sorry !
	** We allocate and set up the Host and Port structs when the
	** driver is called to 'install' the first host.
	** We check for this first 'call' by testing the RIOPortp pointer.
	*/
	if ( !p->RIOPortp )
	{
		rio_dprintk (RIO_DEBUG_INIT,  "Allocating and setting up driver data structures\n");

		RIOAllocDataStructs(p);		/* allocate host/port structs */
		RIOSetupDataStructs(p);		/* setup topology structs */
	}

	RIOInitHosts( p, info );	/* hunt down the hardware */

	RIOAllocateInterrupts(p);	/* allocate interrupts */
	RIOReport(p);			/* show what we found */
}

/*
** Initialise the Cards 
*/ 
void
RIOInitHosts(p, info)
struct rio_info		* p;
struct RioHostInfo	* info;
{
/*
** 15.10.1998 ARG - ESIL 0762 part fix
** If there is no ISA card definition - we always look for PCI cards.
** As we currently only support one host card this lets an ISA card
** definition take precedence over PLUG and PLAY.
** No ISA card - we are PLUG and PLAY with PCI.
*/

	/*
	** Note - for PCI both these will be zero, that's okay because
	** RIOPCIInit() fills them in if a card is found.
	*/
	p->RIOHosts[p->RIONumHosts].Ivec	= info->vector;
	p->RIOHosts[p->RIONumHosts].PaddrP	= info->location;

	/*
	** Check that we are able to accommodate another host
	*/
	if ( p->RIONumHosts >= RIO_HOSTS )
	{
		p->RIOFailed++;
		return;
	}

	if ( info->bus & ISA_BUS )
	{
		rio_dprintk (RIO_DEBUG_INIT,  "initialising card %d (ISA)\n", p->RIONumHosts);
		RIOISAinit(p, p->mode);
	}
	else
	{
		rio_dprintk (RIO_DEBUG_INIT,  "initialising card %d (PCI)\n", p->RIONumHosts);
		RIOPCIinit(p, RIO_PCI_DEFAULT_MODE);
	}

	rio_dprintk (RIO_DEBUG_INIT,  "Total hosts initialised so far : %d\n", p->RIONumHosts);


#ifdef FUTURE_RELEASE
	if (p->bus & EISA_BUS)
		/* EISA card */
		RIOEISAinit(p, RIO_EISA_DEFAULT_MODE);

	if (p->bus & MCA_BUS)
		/* MCA card */
		RIOMCAinit(p, RIO_MCA_DEFAULT_MODE);
#endif
}

/*
** go through memory for an AT host that we pass in the device info
** structure and initialise
*/
void
RIOISAinit(p, mode)
struct rio_info *	p;
int					mode;
{

  /* XXX Need to implement this. */
#if 0
	p->intr_tid = iointset(p->RIOHosts[p->RIONumHosts].Ivec,
					(int (*)())rio_intr, (char*)p->RIONumHosts);

	rio_dprintk (RIO_DEBUG_INIT,  "Set interrupt handler, intr_tid = 0x%x\n", p->intr_tid );

	if (RIODoAT(p, p->RIOHosts[p->RIONumHosts].PaddrP, mode)) {
		return;
	}
	else {
		rio_dprintk (RIO_DEBUG_INIT, "RIODoAT failed\n");
		p->RIOFailed++;
	}
#endif

}

/*
** RIODoAT :
**
** Map in a boards physical address, check that the board is there,
** test the board and if everything is okay assign the board an entry
** in the Rio Hosts structure.
*/
int
RIODoAT(p, Base, mode)
struct rio_info *	p;
int		Base;
int		mode;
{
#define	FOUND		1
#define NOT_FOUND	0

	caddr_t		cardAddr;

	/*
	** Check to see if we actually have a board at this physical address.
	*/
	if ((cardAddr = RIOCheckForATCard(Base)) != 0) {
		/*
		** Now test the board to see if it is working.
		*/
		if (RIOBoardTest(Base, cardAddr, RIO_AT, 0) == RIO_SUCCESS) {
			/*
			** Fill out a slot in the Rio host structure.
			*/
			if (RIOAssignAT(p, Base, cardAddr, mode)) {
				return(FOUND);
			}
		}
		RIOMapout(Base, RIO_AT_MEM_SIZE, cardAddr);
	}
	return(NOT_FOUND);
}

caddr_t
RIOCheckForATCard(Base)
int		Base;
{
	int				off;
	struct DpRam	*cardp;		/* (Points at the host) */
	caddr_t			virtAddr;
	unsigned char			RIOSigTab[24];
/*
** Table of values to search for as prom signature of a host card
*/
	strcpy(RIOSigTab, "JBJGPGGHINSMJPJR");

	/*
	** Hey! Yes, You reading this code! Yo, grab a load a this:
	**
	** IF the card is using WORD MODE rather than BYTE MODE
	** then it will occupy 128K of PHYSICAL memory area. So,
	** you might think that the following Mapin is wrong. Well,
	** it isn't, because the SECOND 64K of occupied space is an
	** EXACT COPY of the FIRST 64K. (good?), so, we need only
	** map it in in one 64K block.
	*/
	if (RIOMapin(Base, RIO_AT_MEM_SIZE, &virtAddr) == -1) {
		rio_dprintk (RIO_DEBUG_INIT, "RIO-init: Couldn't map the board in!\n");
		return((caddr_t)0);
	}

	/*
	** virtAddr points to the DP ram of the system.
	** We now cast this to a pointer to a RIO Host,
	** and have a rummage about in the PROM.
	*/
	cardp = (struct DpRam *)virtAddr;

	for (off=0; RIOSigTab[off]; off++) {
		if ((RBYTE(cardp->DpSignature[off]) & 0xFF) != RIOSigTab[off]) {
			/*
			** Signature mismatch - card not at this address
			*/
			RIOMapout(Base, RIO_AT_MEM_SIZE, virtAddr);
			rio_dprintk (RIO_DEBUG_INIT, "RIO-init: Couldn't match the signature 0x%x 0x%x!\n",
						(int)cardp, off);
			return((caddr_t)0);
		}
	}

	/*
	** If we get here then we must have found a valid board so return
	** its virtual address.
	*/
	return(virtAddr);
}
#endif

/**
** RIOAssignAT :
**
** Fill out the fields in the p->RIOHosts structure now we know we know
** we have a board present.
**
** bits < 0 indicates 8 bit operation requested,
** bits > 0 indicates 16 bit operation.
*/
int
RIOAssignAT(p, Base, virtAddr, mode)
struct rio_info *	p;
int		Base;
caddr_t	virtAddr;
int		mode;
{
	int		bits;
	struct DpRam *cardp = (struct DpRam *)virtAddr;

	if ((Base < ONE_MEG) || (mode & BYTE_ACCESS_MODE))
		bits = BYTE_OPERATION;
	else
		bits = WORD_OPERATION;

	/*
	** Board has passed its scrub test. Fill in all the
	** transient stuff.
	*/
	p->RIOHosts[p->RIONumHosts].Caddr	= virtAddr;
	p->RIOHosts[p->RIONumHosts].CardP	= (struct DpRam *)virtAddr;

	/*
	** Revision 01 AT host cards don't support WORD operations,
	*/
	if ( RBYTE(cardp->DpRevision) == 01 )
		bits = BYTE_OPERATION;

	p->RIOHosts[p->RIONumHosts].Type = RIO_AT;
	p->RIOHosts[p->RIONumHosts].Copy = bcopy;
											/* set this later */
	p->RIOHosts[p->RIONumHosts].Slot = -1;
	p->RIOHosts[p->RIONumHosts].Mode = SLOW_LINKS | SLOW_AT_BUS | bits;
	WBYTE(p->RIOHosts[p->RIONumHosts].Control, 
			BOOT_FROM_RAM | EXTERNAL_BUS_OFF | 
			p->RIOHosts[p->RIONumHosts].Mode | 
			INTERRUPT_DISABLE );
	WBYTE(p->RIOHosts[p->RIONumHosts].ResetInt,0xff);
	WBYTE(p->RIOHosts[p->RIONumHosts].Control,
			BOOT_FROM_RAM | EXTERNAL_BUS_OFF | 
			p->RIOHosts[p->RIONumHosts].Mode |
			INTERRUPT_DISABLE );
	WBYTE(p->RIOHosts[p->RIONumHosts].ResetInt,0xff);
	p->RIOHosts[p->RIONumHosts].UniqueNum =
		((RBYTE(p->RIOHosts[p->RIONumHosts].Unique[0])&0xFF)<<0)|
		((RBYTE(p->RIOHosts[p->RIONumHosts].Unique[1])&0xFF)<<8)|
		((RBYTE(p->RIOHosts[p->RIONumHosts].Unique[2])&0xFF)<<16)|
		((RBYTE(p->RIOHosts[p->RIONumHosts].Unique[3])&0xFF)<<24);
	rio_dprintk (RIO_DEBUG_INIT, "RIO-init: Uniquenum 0x%x\n",p->RIOHosts[p->RIONumHosts].UniqueNum);

	p->RIONumHosts++;
	rio_dprintk (RIO_DEBUG_INIT, "RIO-init: Tests Passed at 0x%x\n", Base);
	return(1);
}
#if 0
#ifdef FUTURE_RELEASE
int RIOMCAinit(int Mode)
{
	uchar SlotNumber;
	caddr_t Caddr;
	uint	Paddr;
	uint	Ivec;
	int	 Handle;
	int	 ret = 0;

	/*
	** Valid mode information for MCA cards
	** is only FAST LINKS
	*/
	Mode = (Mode & FAST_LINKS) ? McaTpFastLinks : McaTpSlowLinks;
	rio_dprintk (RIO_DEBUG_INIT, "RIOMCAinit(%d)\n",Mode);


	/*
	** Check out each of the slots
	*/
	for (SlotNumber = 0; SlotNumber < McaMaxSlots; SlotNumber++) {
	/*
	** Enable the slot we want to talk to
	*/
	outb( McaSlotSelect, SlotNumber | McaSlotEnable );

	/*
	** Read the ID word from the slot
	*/
	if (((inb(McaIdHigh)<< 8)|inb(McaIdLow)) == McaRIOId)
	{
		rio_dprintk (RIO_DEBUG_INIT, "Potential MCA card in slot %d\n", SlotNumber);

		/*
		** Card appears to be a RIO MCA card!
		*/
		RIOMachineType |= (1<<RIO_MCA);

		/*
		** Just check we haven't found too many wonderful objects
		*/
		if ( RIONumHosts >= RIO_HOSTS )
		{
		Rprintf(RIOMesgTooManyCards);
		return(ret);
		}

		/*
		** McaIrqEnable contains the interrupt vector, and a card
		** enable bit.
		*/
		Ivec = inb(McaIrqEnable);

		rio_dprintk (RIO_DEBUG_INIT, "Ivec is %x\n", Ivec);

		switch ( Ivec & McaIrqMask )
		{
		case McaIrq9:
		rio_dprintk (RIO_DEBUG_INIT, "IRQ9\n");
		break;
		case McaIrq3:
		rio_dprintk (RIO_DEBUG_INIT, "IRQ3\n");
		break;
		case McaIrq4:
		rio_dprintk (RIO_DEBUG_INIT, "IRQ4\n");
		break;
		case McaIrq7:
		rio_dprintk (RIO_DEBUG_INIT, "IRQ7\n");
		break;
		case McaIrq10:
		rio_dprintk (RIO_DEBUG_INIT, "IRQ10\n");
		break;
		case McaIrq11:
		rio_dprintk (RIO_DEBUG_INIT, "IRQ11\n");
		break;
		case McaIrq12:
		rio_dprintk (RIO_DEBUG_INIT, "IRQ12\n");
		break;
		case McaIrq15:
		rio_dprintk (RIO_DEBUG_INIT, "IRQ15\n");
		break;
		}

		/*
		** If the card enable bit isn't set, then set it!
		*/
		if ((Ivec & McaCardEnable) != McaCardEnable) {
			rio_dprintk (RIO_DEBUG_INIT, "McaCardEnable not set - setting!\n");
			outb(McaIrqEnable,Ivec|McaCardEnable);
		} else
			rio_dprintk (RIO_DEBUG_INIT, "McaCardEnable already set\n");

		/*
		** Convert the IRQ enable mask into something useful
		*/
		Ivec = RIOMcaToIvec[Ivec & McaIrqMask];

		/*
		** Find the physical address
		*/
		rio_dprintk (RIO_DEBUG_INIT, "inb(McaMemory) is %x\n", inb(McaMemory));
		Paddr = McaAddress(inb(McaMemory));

		rio_dprintk (RIO_DEBUG_INIT, "MCA card has Ivec %d Addr %x\n", Ivec, Paddr);

		if ( Paddr != 0 )
		{

		/*
		** Tell the memory mapper that we want to talk to it
		*/
		Handle = RIOMapin( Paddr, RIO_MCA_MEM_SIZE, &Caddr );

		if ( Handle == -1 ) {
			rio_dprintk (RIO_DEBUG_INIT, "Couldn't map %d bytes at %x\n", RIO_MCA_MEM_SIZE, Paddr;
			continue;
		}

		rio_dprintk (RIO_DEBUG_INIT, "Board mapped to vaddr 0x%x\n", Caddr);

		/*
		** And check that it is actually there!
		*/
		if ( RIOBoardTest( Paddr,Caddr,RIO_MCA,SlotNumber ) == RIO_SUCCESS )
		{
			rio_dprintk (RIO_DEBUG_INIT, "Board has passed test\n");
			rio_dprintk (RIO_DEBUG_INIT, "Slot %d. Type %d. Paddr 0x%x. Caddr 0x%x. Mode 0x%x.\n",
			                            SlotNumber, RIO_MCA, Paddr, Caddr, Mode);

			/*
			** Board has passed its scrub test. Fill in all the
			** transient stuff.
			*/
			p->RIOHosts[RIONumHosts].Slot	 = SlotNumber;
			p->RIOHosts[RIONumHosts].Ivec	 = Ivec;
			p->RIOHosts[RIONumHosts].Type	 = RIO_MCA;
			p->RIOHosts[RIONumHosts].Copy	 = bcopy;
			p->RIOHosts[RIONumHosts].PaddrP   = Paddr;
			p->RIOHosts[RIONumHosts].Caddr	= Caddr;
			p->RIOHosts[RIONumHosts].CardP	= (struct DpRam *)Caddr;
			p->RIOHosts[RIONumHosts].Mode	 = Mode;
			WBYTE(p->RIOHosts[p->RIONumHosts].ResetInt , 0xff);
			p->RIOHosts[RIONumHosts].UniqueNum =
			((RBYTE(p->RIOHosts[RIONumHosts].Unique[0])&0xFF)<<0)|
						((RBYTE(p->RIOHosts[RIONumHosts].Unique[1])&0xFF)<<8)|
			((RBYTE(p->RIOHosts[RIONumHosts].Unique[2])&0xFF)<<16)|
			((RBYTE(p->RIOHosts[RIONumHosts].Unique[3])&0xFF)<<24);
			RIONumHosts++;
			ret++;
		}
		else
		{
			/*
			** It failed the test, so ignore it.
			*/
			rio_dprintk (RIO_DEBUG_INIT, "TEST FAILED\n");
			RIOMapout(Paddr, RIO_MCA_MEM_SIZE, Caddr );
		}
		}
		else
		{
		rio_dprintk (RIO_DEBUG_INIT, "Slot %d - Paddr zero!\n", SlotNumber);
		}
	}
	else
	{
		rio_dprintk (RIO_DEBUG_INIT, "Slot %d NOT RIO\n", SlotNumber);
	}
	}
	/*
	** Now we have checked all the slots, turn off the MCA slot selector
	*/
	outb(McaSlotSelect,0);
	rio_dprintk (RIO_DEBUG_INIT, "Slot %d NOT RIO\n", SlotNumber);
	return ret;
}

int RIOEISAinit( int Mode )
{
	static int EISADone = 0;
	uint Paddr;
	int PollIntMixMsgDone = 0;
	caddr_t Caddr;
	ushort Ident;
	uchar EisaSlot;
	uchar Ivec;
	int ret = 0;

	/*
	** The only valid mode information for EISA hosts is fast or slow
	** links.
	*/
	Mode = (Mode & FAST_LINKS) ? EISA_TP_FAST_LINKS : EISA_TP_SLOW_LINKS;

	if ( EISADone )
	{
		rio_dprintk (RIO_DEBUG_INIT, "RIOEISAinit() - already done, return.\n");
		return(0);
	}

	EISADone++;

	rio_dprintk (RIO_DEBUG_INIT, "RIOEISAinit()\n");


	/*
	** First check all cards to see if ANY are set for polled mode operation.
	** If so, set ALL to polled.
	*/

	for ( EisaSlot=1; EisaSlot<=RIO_MAX_EISA_SLOTS; EisaSlot++ )
	{
	Ident = (INBZ(EisaSlot,EISA_PRODUCT_IDENT_HI)<<8) |
		 INBZ(EisaSlot,EISA_PRODUCT_IDENT_LO);

	if ( Ident == RIO_EISA_IDENT )
	{
		rio_dprintk (RIO_DEBUG_INIT, "Found Specialix product\n");

		if ( INBZ(EisaSlot,EISA_PRODUCT_NUMBER) != RIO_EISA_PRODUCT_CODE )
		{
		rio_dprintk (RIO_DEBUG_INIT, "Not Specialix RIO - Product number %x\n",
						INBZ(EisaSlot, EISA_PRODUCT_NUMBER));
		continue;  /* next slot */
		}
		/*
		** Its a Specialix RIO!
		*/
		rio_dprintk (RIO_DEBUG_INIT, "RIO Revision %d\n",
					INBZ(EisaSlot, EISA_REVISION_NUMBER));
		
		RIOMachineType |= (1<<RIO_EISA);

		/*
		** Just check we haven't found too many wonderful objects
		*/
		if ( RIONumHosts >= RIO_HOSTS )
		{
		Rprintf(RIOMesgTooManyCards);
		return 0;
		}

		/*
		** Ensure that the enable bit is set!
		*/
		OUTBZ( EisaSlot, EISA_ENABLE, RIO_EISA_ENABLE_BIT );

		/*
		** EISA_INTERRUPT_VEC contains the interrupt vector.
		*/
		Ivec = INBZ(EisaSlot,EISA_INTERRUPT_VEC);

#ifdef RIODEBUG
		switch ( Ivec & EISA_INTERRUPT_MASK )
		{
		case EISA_IRQ_3:
			rio_dprintk (RIO_DEBUG_INIT, "EISA IRQ 3\n");
		break;
		case EISA_IRQ_4:
			rio_dprintk (RIO_DEBUG_INIT, "EISA IRQ 4\n");
		break;
		case EISA_IRQ_5:
			rio_dprintk (RIO_DEBUG_INIT, "EISA IRQ 5\n");
		break;
		case EISA_IRQ_6:
			rio_dprintk (RIO_DEBUG_INIT, "EISA IRQ 6\n");
		break;
		case EISA_IRQ_7:
			rio_dprintk (RIO_DEBUG_INIT, "EISA IRQ 7\n");
		break;
		case EISA_IRQ_9:
			rio_dprintk (RIO_DEBUG_INIT, "EISA IRQ 9\n");
		break;
		case EISA_IRQ_10:
			rio_dprintk (RIO_DEBUG_INIT, "EISA IRQ 10\n");
		break;
		case EISA_IRQ_11:
			rio_dprintk (RIO_DEBUG_INIT, "EISA IRQ 11\n");
		break;
		case EISA_IRQ_12:
			rio_dprintk (RIO_DEBUG_INIT, "EISA IRQ 12\n");
		break;
		case EISA_IRQ_14:
			rio_dprintk (RIO_DEBUG_INIT, "EISA IRQ 14\n");
		break;
		case EISA_IRQ_15:
			rio_dprintk (RIO_DEBUG_INIT, "EISA IRQ 15\n");
		break;
		case EISA_POLLED:
			rio_dprintk (RIO_DEBUG_INIT, "EISA POLLED\n");
		break;
		default:
			rio_dprintk (RIO_DEBUG_INIT, NULL,DBG_INIT|DBG_FAIL,"Shagged interrupt number!\n");
		Ivec &= EISA_CONTROL_MASK;
		}
#endif

		if ( (Ivec & EISA_INTERRUPT_MASK) ==
		 EISA_POLLED )
		{
		RIOWillPoll = 1;
		break;		/* From EisaSlot loop */
		}
	}
	}

	/*
	** Do it all again now we know whether to change all cards to polled
	** mode or not
	*/

	for ( EisaSlot=1; EisaSlot<=RIO_MAX_EISA_SLOTS; EisaSlot++ )
	{
	Ident = (INBZ(EisaSlot,EISA_PRODUCT_IDENT_HI)<<8) |
		 INBZ(EisaSlot,EISA_PRODUCT_IDENT_LO);

	if ( Ident == RIO_EISA_IDENT )
	{
		if ( INBZ(EisaSlot,EISA_PRODUCT_NUMBER) != RIO_EISA_PRODUCT_CODE )
		continue;  /* next slot */

		/*
		** Its a Specialix RIO!
		*/
		
		/*
		** Ensure that the enable bit is set!
		*/
		OUTBZ( EisaSlot, EISA_ENABLE, RIO_EISA_ENABLE_BIT );

		/*
		** EISA_INTERRUPT_VEC contains the interrupt vector.
		*/
		Ivec = INBZ(EisaSlot,EISA_INTERRUPT_VEC);

		if ( RIOWillPoll )
		{
			/*
			** If we are going to operate in polled mode, but this
			** board is configured to be interrupt driven, display
			** the message explaining the situation to the punter,
			** assuming we haven't already done so.
			*/

			if ( !PollIntMixMsgDone &&
			 (Ivec & EISA_INTERRUPT_MASK) != EISA_POLLED )
			{
			Rprintf(RIOMesgAllPolled);
			PollIntMixMsgDone = 1;
			}

			/*
			** Ungraciously ignore whatever the board reports as its
			** interrupt vector...
			*/

			Ivec &= ~EISA_INTERRUPT_MASK;

			/*
			** ...and force it to dance to the poll tune.
			*/

			Ivec |= EISA_POLLED;
		}

		/*
		** Convert the IRQ enable mask into something useful (0-15)
		*/
		Ivec = RIOEisaToIvec(Ivec);

		rio_dprintk (RIO_DEBUG_INIT, "EISA host in slot %d has Ivec 0x%x\n",
		 EisaSlot, Ivec);

		/*
		** Find the physical address
		*/
		Paddr = (INBZ(EisaSlot,EISA_MEMORY_BASE_HI)<<24) |
				(INBZ(EisaSlot,EISA_MEMORY_BASE_LO)<<16);

		rio_dprintk (RIO_DEBUG_INIT, "EISA card has Ivec %d Addr %x\n", Ivec, Paddr);

		if ( Paddr == 0 )
		{
		rio_dprintk (RIO_DEBUG_INIT,
		 "Board in slot %d configured for address zero!\n", EisaSlot);
		continue;
		}

		/*
		** Tell the memory mapper that we want to talk to it
		*/
		rio_dprintk (RIO_DEBUG_INIT, "About to map EISA card \n");

		if (RIOMapin( Paddr, RIO_EISA_MEM_SIZE, &Caddr) == -1) {
		rio_dprintk (RIO_DEBUG_INIT, "Couldn't map %d bytes at %x\n",
							RIO_EISA_MEM_SIZE,Paddr);
		continue;
		}

		rio_dprintk (RIO_DEBUG_INIT, "Board mapped to vaddr 0x%x\n", Caddr);

		/*
		** And check that it is actually there!
		*/
		if ( RIOBoardTest( Paddr,Caddr,RIO_EISA,EisaSlot) == RIO_SUCCESS )
			{
		rio_dprintk (RIO_DEBUG_INIT, "Board has passed test\n");
		rio_dprintk (RIO_DEBUG_INIT, 
		"Slot %d. Ivec %d. Type %d. Paddr 0x%x. Caddr 0x%x. Mode 0x%x.\n",
			EisaSlot,Ivec,RIO_EISA,Paddr,Caddr,Mode);

		/*
		** Board has passed its scrub test. Fill in all the
		** transient stuff.
		*/
		p->RIOHosts[RIONumHosts].Slot	 = EisaSlot;
		p->RIOHosts[RIONumHosts].Ivec	 = Ivec;
		p->RIOHosts[RIONumHosts].Type	 = RIO_EISA;
		p->RIOHosts[RIONumHosts].Copy	 = bcopy;
				p->RIOHosts[RIONumHosts].PaddrP   = Paddr;
				p->RIOHosts[RIONumHosts].Caddr	= Caddr;
		p->RIOHosts[RIONumHosts].CardP	= (struct DpRam *)Caddr;
				p->RIOHosts[RIONumHosts].Mode	 = Mode;
		/*
		** because the EISA prom is mapped into IO space, we
		** need to copy the unqiue number into the memory area
		** that it would have occupied, so that the download
		** code can determine its ID and card type.
		*/
	 WBYTE(p->RIOHosts[RIONumHosts].Unique[0],INBZ(EisaSlot,EISA_UNIQUE_NUM_0));
	 WBYTE(p->RIOHosts[RIONumHosts].Unique[1],INBZ(EisaSlot,EISA_UNIQUE_NUM_1));
	 WBYTE(p->RIOHosts[RIONumHosts].Unique[2],INBZ(EisaSlot,EISA_UNIQUE_NUM_2));
	 WBYTE(p->RIOHosts[RIONumHosts].Unique[3],INBZ(EisaSlot,EISA_UNIQUE_NUM_3));
		p->RIOHosts[RIONumHosts].UniqueNum =
			((RBYTE(p->RIOHosts[RIONumHosts].Unique[0])&0xFF)<<0)|
						((RBYTE(p->RIOHosts[RIONumHosts].Unique[1])&0xFF)<<8)|
			((RBYTE(p->RIOHosts[RIONumHosts].Unique[2])&0xFF)<<16)|
			((RBYTE(p->RIOHosts[RIONumHosts].Unique[3])&0xFF)<<24);
		INBZ(EisaSlot,EISA_INTERRUPT_RESET);
				RIONumHosts++;
		ret++;
			}
		else
		{
		/*
		** It failed the test, so ignore it.
		*/
		rio_dprintk (RIO_DEBUG_INIT, "TEST FAILED\n");

		RIOMapout(Paddr, RIO_EISA_MEM_SIZE, Caddr );
		}
	}
	}
	if (RIOMachineType & RIO_EISA)
	return ret+1;
	return ret;
}
#endif


#ifndef linux

#define CONFIG_ADDRESS	0xcf8
#define CONFIG_DATA		0xcfc
#define FORWARD_REG		0xcfa


static int
read_config(int bus_number, int device_num, int r_number) 
{
	unsigned int cav;
	unsigned int val;

/*
   Build config_address_value:

      31        24 23        16 15      11 10  8 7        0 
      ------------------------------------------------------
      |1| 0000000 | bus_number | device # | 000 | register |
      ------------------------------------------------------
*/

	cav = r_number & 0xff;
	cav |= ((device_num & 0x1f) << 11);
	cav |= ((bus_number & 0xff) << 16);
	cav |= 0x80000000; /* Enable bit */
	outpd(CONFIG_ADDRESS,cav);
	val = inpd(CONFIG_DATA);
	outpd(CONFIG_ADDRESS,0);
	return val;
}

static
write_config(bus_number,device_num,r_number,val) 
{
	unsigned int cav;

/*
   Build config_address_value:

      31        24 23        16 15      11 10  8 7        0 
      ------------------------------------------------------
      |1| 0000000 | bus_number | device # | 000 | register |
      ------------------------------------------------------
*/

	cav = r_number & 0xff;
	cav |= ((device_num & 0x1f) << 11);
	cav |= ((bus_number & 0xff) << 16);
	cav |= 0x80000000; /* Enable bit */
	outpd(CONFIG_ADDRESS, cav);
	outpd(CONFIG_DATA, val);
	outpd(CONFIG_ADDRESS, 0);
	return val;
}
#else
/* XXX Implement these... */
static int
read_config(int bus_number, int device_num, int r_number) 
{
  return 0;
}

static int
write_config(int bus_number, int device_num, int r_number) 
{
  return 0;
}

#endif

int
RIOPCIinit(p, Mode)
struct rio_info	*p;
int 		Mode;
{
	#define MAX_PCI_SLOT		32
	#define RIO_PCI_JET_CARD	0x200011CB

	static int	slot;	/* count of machine's PCI slots searched so far */
	caddr_t		Caddr;	/* Virtual address of the current PCI host card. */
	unsigned char	Ivec;	/* interrupt vector for the current PCI host */
	unsigned long	Paddr;	/* Physical address for the current PCI host */
	int		Handle;	/* Handle to Virtual memory allocated for current PCI host */


	rio_dprintk (RIO_DEBUG_INIT,  "Search for a RIO PCI card - start at slot %d\n", slot);

	/*
	** Initialise the search status
	*/
	p->RIOLastPCISearch	= RIO_FAIL;

	while ( (slot < MAX_PCI_SLOT) & (p->RIOLastPCISearch != RIO_SUCCESS) )
	{
		rio_dprintk (RIO_DEBUG_INIT,  "Currently testing slot %d\n", slot);

		if (read_config(0,slot,0) == RIO_PCI_JET_CARD) {
			p->RIOHosts[p->RIONumHosts].Ivec = 0;
			Paddr = read_config(0,slot,0x18);
			Paddr = Paddr - (Paddr & 0x1); /* Mask off the io bit */

			if ( (Paddr == 0) || ((Paddr & 0xffff0000) == 0xffff0000) ) {
				rio_dprintk (RIO_DEBUG_INIT,  "Goofed up slot\n");	/* what! */
				slot++;
				continue;
			}

			p->RIOHosts[p->RIONumHosts].PaddrP = Paddr;
			Ivec = (read_config(0,slot,0x3c) & 0xff);

			rio_dprintk (RIO_DEBUG_INIT,  "PCI Host at 0x%x, Intr %d\n", (int)Paddr, Ivec);

			Handle = RIOMapin( Paddr, RIO_PCI_MEM_SIZE, &Caddr );
			if (Handle == -1) {
				rio_dprintk (RIO_DEBUG_INIT,  "Couldn't map %d bytes at 0x%x\n", RIO_PCI_MEM_SIZE, (int)Paddr);
				slot++;
				continue;
			}
			p->RIOHosts[p->RIONumHosts].Ivec = Ivec + 32;
			p->intr_tid = iointset(p->RIOHosts[p->RIONumHosts].Ivec,
						(int (*)())rio_intr, (char *)p->RIONumHosts);
			if (RIOBoardTest( Paddr, Caddr, RIO_PCI, 0 ) == RIO_SUCCESS) {
				rio_dprintk (RIO_DEBUG_INIT, ("Board has passed test\n");
				rio_dprintk (RIO_DEBUG_INIT, ("Paddr 0x%x. Caddr 0x%x. Mode 0x%x.\n", Paddr, Caddr, Mode);

				/*
				** Board has passed its scrub test. Fill in all the
				** transient stuff.
				*/
				p->RIOHosts[p->RIONumHosts].Slot	   = 0;
				p->RIOHosts[p->RIONumHosts].Ivec	   = Ivec + 32;
				p->RIOHosts[p->RIONumHosts].Type	   = RIO_PCI;
				p->RIOHosts[p->RIONumHosts].Copy	   = rio_pcicopy; 
				p->RIOHosts[p->RIONumHosts].PaddrP	   = Paddr;
				p->RIOHosts[p->RIONumHosts].Caddr	   = Caddr;
				p->RIOHosts[p->RIONumHosts].CardP	   = (struct DpRam *)Caddr;
				p->RIOHosts[p->RIONumHosts].Mode	   = Mode;

#if 0
				WBYTE(p->RIOHosts[p->RIONumHosts].Control, 
						BOOT_FROM_RAM | EXTERNAL_BUS_OFF | 
						p->RIOHosts[p->RIONumHosts].Mode | 
						INTERRUPT_DISABLE );
				WBYTE(p->RIOHosts[p->RIONumHosts].ResetInt,0xff);
				WBYTE(p->RIOHosts[p->RIONumHosts].Control,
						BOOT_FROM_RAM | EXTERNAL_BUS_OFF | 
						p->RIOHosts[p->RIONumHosts].Mode |
						INTERRUPT_DISABLE );
				WBYTE(p->RIOHosts[p->RIONumHosts].ResetInt,0xff);
#else
				WBYTE(p->RIOHosts[p->RIONumHosts].ResetInt, 0xff);
#endif
				p->RIOHosts[p->RIONumHosts].UniqueNum  =
					((RBYTE(p->RIOHosts[p->RIONumHosts].Unique[0])&0xFF)<<0)|
					((RBYTE(p->RIOHosts[p->RIONumHosts].Unique[1])&0xFF)<<8)|
					((RBYTE(p->RIOHosts[p->RIONumHosts].Unique[2])&0xFF)<<16)|
					((RBYTE(p->RIOHosts[p->RIONumHosts].Unique[3])&0xFF)<<24);

				rio_dprintk (RIO_DEBUG_INIT, "Unique no 0x%x.\n", 
				    p->RIOHosts[p->RIONumHosts].UniqueNum);

				p->RIOLastPCISearch = RIO_SUCCESS;
				p->RIONumHosts++;
			}
		}
		slot++;
	}

	if ( slot >= MAX_PCI_SLOT ) {
		rio_dprintk (RIO_DEBUG_INIT,  "All %d PCI slots have tested for RIO cards !!!\n",
			     MAX_PCI_SLOT);
	}


	/*
	** I don't think we want to do this anymore
	**

	if (!p->RIOLastPCISearch == RIO_FAIL ) {
		p->RIOFailed++;
	}

	**
	*/
}

#ifdef FUTURE_RELEASE
void riohalt( void )
{
	int host;
	for ( host=0; host<p->RIONumHosts; host++ )
	{
		rio_dprintk (RIO_DEBUG_INIT, "Stop host %d\n", host);
		(void)RIOBoardTest( p->RIOHosts[host].PaddrP, p->RIOHosts[host].Caddr, p->RIOHosts[host].Type,p->RIOHosts[host].Slot );
	}
}
#endif
#endif

static	uchar	val[] = {
#ifdef VERY_LONG_TEST
	  0x00, 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80,
	  0xa5, 0xff, 0x5a, 0x00, 0xff, 0xc9, 0x36, 
#endif
	  0xff, 0x00, 0x00 };

#define	TEST_END sizeof(val)

/*
** RAM test a board. 
** Nothing too complicated, just enough to check it out.
*/
int
RIOBoardTest(paddr, caddr, type, slot)
paddr_t	paddr;
caddr_t	caddr;
uchar	type;
int		slot;
{
	struct DpRam *DpRam = (struct DpRam *)caddr;
	char *ram[4];
	int  size[4];
	int  op, bank;
	int  nbanks;

	rio_dprintk (RIO_DEBUG_INIT, "RIO-init: Reset host type=%d, DpRam=0x%x, slot=%d\n",
			type,(int)DpRam, slot);

	RIOHostReset(type, DpRam, slot);

	/*
	** Scrub the memory. This comes in several banks:
	** DPsram1	- 7000h bytes
	** DPsram2	- 200h  bytes
	** DPsram3	- 7000h bytes
	** scratch	- 1000h bytes
	*/

	rio_dprintk (RIO_DEBUG_INIT, "RIO-init: Setup ram/size arrays\n");

	size[0] = DP_SRAM1_SIZE;
	size[1] = DP_SRAM2_SIZE;
	size[2] = DP_SRAM3_SIZE;
	size[3] = DP_SCRATCH_SIZE;

	ram[0] = (char *)&DpRam->DpSram1[0];
	ram[1] = (char *)&DpRam->DpSram2[0];
	ram[2] = (char *)&DpRam->DpSram3[0];
	nbanks = (type == RIO_PCI) ? 3 : 4;
	if (nbanks == 4)
		ram[3] = (char *)&DpRam->DpScratch[0];


	if (nbanks == 3) {
		rio_dprintk (RIO_DEBUG_INIT, "RIO-init: Memory: 0x%x(0x%x), 0x%x(0x%x), 0x%x(0x%x)\n",
				(int)ram[0], size[0], (int)ram[1], size[1], (int)ram[2], size[2]);
	} else {
		rio_dprintk (RIO_DEBUG_INIT, "RIO-init: 0x%x(0x%x), 0x%x(0x%x), 0x%x(0x%x), 0x%x(0x%x)\n",
			(int)ram[0], size[0], (int)ram[1], size[1], (int)ram[2], size[2], (int)ram[3], 
					size[3]);
	}

	/*
	** This scrub operation will test for crosstalk between
	** banks. TEST_END is a magic number, and relates to the offset
	** within the 'val' array used by Scrub.
	*/
	for (op=0; op<TEST_END; op++) {
		for (bank=0; bank<nbanks; bank++) {
			if (RIOScrub(op, (BYTE *)ram[bank], size[bank]) == RIO_FAIL) {
				rio_dprintk (RIO_DEBUG_INIT, "RIO-init: RIOScrub band %d, op %d failed\n", 
							bank, op);
				return RIO_FAIL;
			}
		}
	}

	rio_dprintk (RIO_DEBUG_INIT, "Test completed\n");
	return RIO_SUCCESS;
}


/*
** Scrub an area of RAM.
** Define PRETEST and POSTTEST for a more thorough checking of the
** state of the memory.
** Call with op set to an index into the above 'val' array to determine
** which value will be written into memory.
** Call with op set to zero means that the RAM will not be read and checked
** before it is written.
** Call with op not zero, and the RAM will be read and compated with val[op-1]
** to check that the data from the previous phase was retained.
*/
static int
RIOScrub(op, ram, size)
int		op;
BYTE *	ram;
int		size; 
{
	int				off;
	unsigned char	oldbyte;
	unsigned char	newbyte;
	unsigned char	invbyte;
	unsigned short	oldword;
	unsigned short	newword;
	unsigned short	invword;
	unsigned short	swapword;

	if (op) {
		oldbyte = val[op-1];
		oldword = oldbyte | (oldbyte<<8);
	} else
	  oldbyte = oldword = 0; /* Tell the compiler we've initilalized them. */
	newbyte = val[op];
	newword = newbyte | (newbyte<<8);
	invbyte = ~newbyte;
	invword = invbyte | (invbyte<<8);

	/*
	** Check that the RAM contains the value that should have been left there
	** by the previous test (not applicable for pass zero)
	*/
	if (op) {
		for (off=0; off<size; off++) {
			if (RBYTE(ram[off]) != oldbyte) {
				rio_dprintk (RIO_DEBUG_INIT, "RIO-init: Byte Pre Check 1: BYTE at offset 0x%x should have been=%x, was=%x\n", off, oldbyte, RBYTE(ram[off]));
				return RIO_FAIL;
			}
		}
		for (off=0; off<size; off+=2) {
			if (*(ushort *)&ram[off] != oldword) {
				rio_dprintk (RIO_DEBUG_INIT, "RIO-init: Word Pre Check: WORD at offset 0x%x should have been=%x, was=%x\n",off,oldword,*(ushort *)&ram[off]);
				rio_dprintk (RIO_DEBUG_INIT, "RIO-init: Word Pre Check: BYTE at offset 0x%x is %x BYTE at offset 0x%x is %x\n", off, RBYTE(ram[off]), off+1, RBYTE(ram[off+1]));
				return RIO_FAIL;
			}
		}
	}

	/*
	** Now write the INVERSE of the test data into every location, using
	** BYTE write operations, first checking before each byte is written
	** that the location contains the old value still, and checking after
	** the write that the location contains the data specified - this is
	** the BYTE read/write test.
	*/
	for (off=0; off<size; off++) {
		if (op && (RBYTE(ram[off]) != oldbyte)) {
			rio_dprintk (RIO_DEBUG_INIT, "RIO-init: Byte Pre Check 2: BYTE at offset 0x%x should have been=%x, was=%x\n", off, oldbyte, RBYTE(ram[off]));
			return RIO_FAIL;
		}
		WBYTE(ram[off],invbyte);
		if (RBYTE(ram[off]) != invbyte) {
			rio_dprintk (RIO_DEBUG_INIT, "RIO-init: Byte Inv Check: BYTE at offset 0x%x should have been=%x, was=%x\n", off, invbyte, RBYTE(ram[off]));
			return RIO_FAIL;
		}
	}

	/*
	** now, use WORD operations to write the test value into every location,
	** check as before that the location contains the previous test value
	** before overwriting, and that it contains the data value written
	** afterwards.
	** This is the WORD operation test.
	*/
	for (off=0; off<size; off+=2) {
		if (*(ushort *)&ram[off] != invword) {
			rio_dprintk (RIO_DEBUG_INIT, "RIO-init: Word Inv Check: WORD at offset 0x%x should have been=%x, was=%x\n", off, invword, *(ushort *)&ram[off]);
		rio_dprintk (RIO_DEBUG_INIT, "RIO-init: Word Inv Check: BYTE at offset 0x%x is %x BYTE at offset 0x%x is %x\n", off, RBYTE(ram[off]), off+1, RBYTE(ram[off+1]));
			return RIO_FAIL;
		}

		*(ushort *)&ram[off] = newword;
		if ( *(ushort *)&ram[off] != newword ) {
			rio_dprintk (RIO_DEBUG_INIT, "RIO-init: Post Word Check 1: WORD at offset 0x%x should have been=%x, was=%x\n", off, newword, *(ushort *)&ram[off]);
			rio_dprintk (RIO_DEBUG_INIT, "RIO-init: Post Word Check 1: BYTE at offset 0x%x is %x BYTE at offset 0x%x is %x\n", off, RBYTE(ram[off]), off+1, RBYTE(ram[off+1]));
			return RIO_FAIL;
		}
	}

	/*
	** now run through the block of memory again, first in byte mode
	** then in word mode, and check that all the locations contain the
	** required test data.
	*/
	for (off=0; off<size; off++) {
		if (RBYTE(ram[off]) != newbyte) {
			rio_dprintk (RIO_DEBUG_INIT, "RIO-init: Post Byte Check: BYTE at offset 0x%x should have been=%x, was=%x\n", off, newbyte, RBYTE(ram[off]));
			return RIO_FAIL;
		}
	}

	for (off=0; off<size; off+=2) {
		if ( *(ushort *)&ram[off] != newword ) {
			rio_dprintk (RIO_DEBUG_INIT, "RIO-init: Post Word Check 2: WORD at offset 0x%x should have been=%x, was=%x\n", off, newword, *(ushort *)&ram[off]);
			rio_dprintk (RIO_DEBUG_INIT, "RIO-init: Post Word Check 2: BYTE at offset 0x%x is %x BYTE at offset 0x%x is %x\n", off, RBYTE(ram[off]), off+1, RBYTE(ram[off+1]));
			return RIO_FAIL;
		}
	}

	/*
	** time to check out byte swapping errors
	*/
	swapword = invbyte | (newbyte << 8);

	for (off=0; off<size; off+=2) {
		WBYTE(ram[off],invbyte);
		WBYTE(ram[off+1],newbyte);
	}

	for ( off=0; off<size; off+=2 ) {
		if (*(ushort *)&ram[off] != swapword) {
			rio_dprintk (RIO_DEBUG_INIT, "RIO-init: SwapWord Check 1: WORD at offset 0x%x should have been=%x, was=%x\n", off, swapword, *((ushort *)&ram[off]));
			rio_dprintk (RIO_DEBUG_INIT, "RIO-init: SwapWord Check 1: BYTE at offset 0x%x is %x BYTE at offset 0x%x is %x\n", off, RBYTE(ram[off]), off+1, RBYTE(ram[off+1]));
			return RIO_FAIL;
		}
		*((ushort *)&ram[off]) = ~swapword;
	}

	for (off=0; off<size; off+=2) {
		if (RBYTE(ram[off]) != newbyte) {
			rio_dprintk (RIO_DEBUG_INIT, "RIO-init: SwapWord Check 2: BYTE at offset 0x%x should have been=%x, was=%x\n", off, newbyte, RBYTE(ram[off]));
			return RIO_FAIL;
		}
		if (RBYTE(ram[off+1]) != invbyte) {
			rio_dprintk (RIO_DEBUG_INIT, "RIO-init: SwapWord Check 2: BYTE at offset 0x%x should have been=%x, was=%x\n", off+1, invbyte, RBYTE(ram[off+1]));
			return RIO_FAIL;
		}
		*((ushort *)&ram[off]) = newword;
	}
	return RIO_SUCCESS;
}

/*
** try to ensure that every host is either in polled mode
** or is in interrupt mode. Only allow interrupt mode if
** all hosts can interrupt (why?)
** and force into polled mode if told to. Patch up the
** interrupt vector & salute The Queen when you've done.
*/
#if 0
static void
RIOAllocateInterrupts(p)
struct rio_info *	p;
{
	int Host;

	/*
	** Easy case - if we have been told to poll, then we poll.
	*/
	if (p->mode & POLLED_MODE) {
		RIOStopInterrupts(p, 0, 0);
		return;
	}

	/*
	** check - if any host has been set to polled mode, then all must be.
	*/
	for (Host=0; Host<p->RIONumHosts; Host++) {
		if ( (p->RIOHosts[Host].Type != RIO_AT) &&
				(p->RIOHosts[Host].Ivec == POLLED) ) {
			RIOStopInterrupts(p, 1, Host );
			return;
		}
	}
	for (Host=0; Host<p->RIONumHosts; Host++) {
		if (p->RIOHosts[Host].Type == RIO_AT) {
			if ( (p->RIOHosts[Host].Ivec - 32) == 0) {
				RIOStopInterrupts(p, 2, Host );
				return;
			}
		}
	}
}

/*
** something has decided that we can't be doing with these
** new-fangled interrupt thingies. Set everything up to just
** poll.
*/
static void
RIOStopInterrupts(p, Reason, Host)
struct rio_info *	p;
int	Reason;
int	Host; 
{
#ifdef FUTURE_RELEASE
	switch (Reason) {
		case 0:	/* forced into polling by rio_polled */
			break;
		case 1:	/* SCU has set 'Host' into polled mode */
			break;
		case 2:	/* there aren't enough interrupt vectors for 'Host' */
			break;
	}
#endif

	for (Host=0; Host<p->RIONumHosts; Host++ ) {
		struct Host *HostP = &p->RIOHosts[Host];

		switch (HostP->Type) {
			case RIO_AT:
				/*
				** The AT host has it's interrupts disabled by clearing the
				** int_enable bit.
				*/
				HostP->Mode &= ~INTERRUPT_ENABLE;
				HostP->Ivec = POLLED;
				break;
#ifdef FUTURE_RELEASE
			case RIO_EISA:
				/*
				** The EISA host has it's interrupts disabled by setting the
				** Ivec to zero
				*/
				HostP->Ivec = POLLED;
				break;
#endif
			case RIO_PCI:
				/*
				** The PCI host has it's interrupts disabled by clearing the
				** int_enable bit, like a regular host card.
				*/
				HostP->Mode &= ~RIO_PCI_INT_ENABLE;
				HostP->Ivec = POLLED;
				break;
#ifdef FUTURE_RELEASE
			case RIO_MCA:
				/*
				** There's always one, isn't there?
				** The MCA host card cannot have it's interrupts disabled.
				*/
				RIOPatchVec(HostP);
				break;
#endif
		}
	}
}

/*
** This function is called at init time to setup the data structures.
*/
void
RIOAllocDataStructs(p)
struct rio_info *	p;
{
	int	port,
		host,
		tm;

	p->RIOPortp = (struct Port *)sysbrk(RIO_PORTS * sizeof(struct Port));
	if (!p->RIOPortp) {
		rio_dprintk (RIO_DEBUG_INIT, "RIO-init: No memory for port structures\n");
		p->RIOFailed++;
		return;
	} 
	bzero( p->RIOPortp, sizeof(struct Port) * RIO_PORTS );
	rio_dprintk (RIO_DEBUG_INIT,  "RIO-init: allocated and cleared memory for port structs\n");
	rio_dprintk (RIO_DEBUG_INIT,  "First RIO port struct @0x%x, size=0x%x bytes\n",
	    (int)p->RIOPortp, sizeof(struct Port));

	for( port=0; port<RIO_PORTS; port++ ) {
		p->RIOPortp[port].PortNum = port;
		p->RIOPortp[port].TtyP = &p->channel[port];
		sreset (p->RIOPortp[port].InUse);	/* Let the first guy uses it */
		p->RIOPortp[port].portSem = -1;	/* Let the first guy takes it */
		p->RIOPortp[port].ParamSem = -1;	/* Let the first guy takes it */
		p->RIOPortp[port].timeout_id = 0;	/* Let the first guy takes it */
	}

	p->RIOHosts = (struct Host *)sysbrk(RIO_HOSTS * sizeof(struct Host));
	if (!p->RIOHosts) {
		rio_dprintk (RIO_DEBUG_INIT, "RIO-init: No memory for host structures\n");
		p->RIOFailed++;
		return;
	}
	bzero(p->RIOHosts, sizeof(struct Host)*RIO_HOSTS);
	rio_dprintk (RIO_DEBUG_INIT,  "RIO-init: allocated and cleared memory for host structs\n");
	rio_dprintk (RIO_DEBUG_INIT,  "First RIO host struct @0x%x, size=0x%x bytes\n",
	    (int)p->RIOHosts, sizeof(struct Host));

	for( host=0; host<RIO_HOSTS; host++ ) {
		spin_lock_init (&p->RIOHosts[host].HostLock);
		p->RIOHosts[host].timeout_id = 0; /* Let the first guy takes it */
	}
	/*
	** check that the buffer size is valid, round down to the next power of
	** two if necessary; if the result is zero, then, hey, no double buffers.
	*/
	for ( tm = 1; tm && tm <= p->RIOConf.BufferSize; tm <<= 1 )
		;
	tm >>= 1;
	p->RIOBufferSize = tm;
	p->RIOBufferMask = tm ? tm - 1 : 0;
}

/*
** this function gets called whenever the data structures need to be
** re-setup, for example, after a riohalt (why did I ever invent it?)
*/
void
RIOSetupDataStructs(p)
struct rio_info	* p;
{
	int host, entry, rup;

	for ( host=0; host<RIO_HOSTS; host++ ) {
		struct Host *HostP = &p->RIOHosts[host];
		for ( entry=0; entry<LINKS_PER_UNIT; entry++ ) {
			HostP->Topology[entry].Unit = ROUTE_DISCONNECT;
			HostP->Topology[entry].Link = NO_LINK;
		}
		bcopy("HOST X", HostP->Name, 7);
		HostP->Name[5] = '1'+host;
		for (rup=0; rup<(MAX_RUP + LINKS_PER_UNIT); rup++) {
			if (rup < MAX_RUP) {
				for (entry=0; entry<LINKS_PER_UNIT; entry++ ) {
					HostP->Mapping[rup].Topology[entry].Unit = ROUTE_DISCONNECT;
					HostP->Mapping[rup].Topology[entry].Link = NO_LINK;
				}
				RIODefaultName(p, HostP, rup);
			}
			spin_lock_init(&HostP->UnixRups[rup].RupLock);
		}
	}
}
#endif

int
RIODefaultName(p, HostP, UnitId)
struct rio_info *	p;
struct Host *	HostP;
uint			UnitId;
{
#ifdef CHECK
	CheckHost( Host );
	CheckUnitId( UnitId );
#endif
	bcopy("UNKNOWN RTA X-XX",HostP->Mapping[UnitId].Name,17);
	HostP->Mapping[UnitId].Name[12]='1'+(HostP-p->RIOHosts);
	if ((UnitId+1) > 9) {
		HostP->Mapping[UnitId].Name[14]='0'+((UnitId+1)/10);
		HostP->Mapping[UnitId].Name[15]='0'+((UnitId+1)%10);
	}
	else {
		HostP->Mapping[UnitId].Name[14]='1'+UnitId;
		HostP->Mapping[UnitId].Name[15]=0;
	}
	return 0;
}

#define RIO_RELEASE	"Linux"
#define RELEASE_ID	"1.0"

#if 0
static int
RIOReport(p)
struct rio_info *	p;
{
	char *	RIORelease = RIO_RELEASE;
	char *	RIORelID = RELEASE_ID;
	int		host;

	rio_dprintk (RIO_DEBUG_INIT, "RIO : Release: %s ID: %s\n", RIORelease, RIORelID);

	if ( p->RIONumHosts==0 ) {
		rio_dprintk (RIO_DEBUG_INIT, "\nNo Hosts configured\n");
		return(0);
	}

	for ( host=0; host < p->RIONumHosts; host++ ) {
		struct Host *HostP = &p->RIOHosts[host];
		switch ( HostP->Type ) {
			case RIO_AT:
				rio_dprintk (RIO_DEBUG_INIT, "AT BUS : found the card at 0x%x\n", HostP->PaddrP);
		}
	}
	return 0;
}
#endif

static struct rioVersion	stVersion;

struct rioVersion *
RIOVersid(void)
{
    strlcpy(stVersion.version, "RIO driver for linux V1.0",
	    sizeof(stVersion.version));
    strlcpy(stVersion.buildDate, __DATE__,
	    sizeof(stVersion.buildDate));

    return &stVersion;
}

#if 0
int
RIOMapin(paddr, size, vaddr)
paddr_t		paddr;
int			size;
caddr_t *	vaddr;
{
	*vaddr = (caddr_t)permap( (long)paddr, size);
	return ((int)*vaddr);
}

void
RIOMapout(paddr, size, vaddr)
paddr_t		paddr;
long		size;
caddr_t 	vaddr;
{
}
#endif


void
RIOHostReset(Type, DpRamP, Slot)
uint Type;
volatile struct DpRam *DpRamP;
uint Slot; 
{
	/*
	** Reset the Tpu
	*/
	rio_dprintk (RIO_DEBUG_INIT,  "RIOHostReset: type 0x%x", Type);
	switch ( Type ) {
		case RIO_AT:
			rio_dprintk (RIO_DEBUG_INIT, " (RIO_AT)\n");
			WBYTE(DpRamP->DpControl,  BOOT_FROM_RAM | EXTERNAL_BUS_OFF | 
					  INTERRUPT_DISABLE | BYTE_OPERATION |
					  SLOW_LINKS | SLOW_AT_BUS);
			WBYTE(DpRamP->DpResetTpu, 0xFF);
			udelay(3);

			rio_dprintk (RIO_DEBUG_INIT,  "RIOHostReset: Don't know if it worked. Try reset again\n");
			WBYTE(DpRamP->DpControl,  BOOT_FROM_RAM | EXTERNAL_BUS_OFF |
					  INTERRUPT_DISABLE | BYTE_OPERATION |
					  SLOW_LINKS | SLOW_AT_BUS);
			WBYTE(DpRamP->DpResetTpu, 0xFF);
			udelay(3);
			break;
#ifdef FUTURE_RELEASE
	case RIO_EISA:
	/*
	** Bet this doesn't work!
	*/
	OUTBZ( Slot, EISA_CONTROL_PORT,
		EISA_TP_RUN		| EISA_TP_BUS_DISABLE   |
		EISA_TP_SLOW_LINKS | EISA_TP_BOOT_FROM_RAM );
	OUTBZ( Slot, EISA_CONTROL_PORT,
		EISA_TP_RESET	  | EISA_TP_BUS_DISABLE   | 
		EISA_TP_SLOW_LINKS | EISA_TP_BOOT_FROM_RAM );
	suspend( 3 );
	OUTBZ( Slot, EISA_CONTROL_PORT,
		EISA_TP_RUN		| EISA_TP_BUS_DISABLE   | 
		EISA_TP_SLOW_LINKS | EISA_TP_BOOT_FROM_RAM );
	break;
	case RIO_MCA:
	WBYTE(DpRamP->DpControl  , McaTpBootFromRam | McaTpBusDisable );
	WBYTE(DpRamP->DpResetTpu , 0xFF );
	suspend( 3 );
	WBYTE(DpRamP->DpControl  , McaTpBootFromRam | McaTpBusDisable );
	WBYTE(DpRamP->DpResetTpu , 0xFF );
	suspend( 3 );
		break;
#endif
	case RIO_PCI:
		rio_dprintk (RIO_DEBUG_INIT, " (RIO_PCI)\n");
		DpRamP->DpControl  = RIO_PCI_BOOT_FROM_RAM;
		DpRamP->DpResetInt = 0xFF;
		DpRamP->DpResetTpu = 0xFF;
		udelay(100);
		/* for (i=0; i<6000; i++);  */
		/* suspend( 3 ); */
		break;
#ifdef FUTURE_RELEASE
	default:
	Rprintf(RIOMesgNoSupport,Type,DpRamP,Slot);
	return;
#endif

	default:
		rio_dprintk (RIO_DEBUG_INIT, " (UNKNOWN)\n");
		break;
	}
	return;
}
