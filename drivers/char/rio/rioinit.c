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

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <asm/io.h>
#include <asm/system.h>
#include <asm/string.h>
#include <asm/uaccess.h>

#include <linux/termios.h>
#include <linux/serial.h>

#include <linux/generic_serial.h>


#include "linux_compat.h"
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
#include "rio_linux.h"

int RIOPCIinit(struct rio_info *p, int Mode);

static int RIOScrub(int, u8 __iomem *, int);


/**
** RIOAssignAT :
**
** Fill out the fields in the p->RIOHosts structure now we know we know
** we have a board present.
**
** bits < 0 indicates 8 bit operation requested,
** bits > 0 indicates 16 bit operation.
*/

int RIOAssignAT(struct rio_info *p, int	Base, void __iomem *virtAddr, int mode)
{
	int		bits;
	struct DpRam __iomem *cardp = (struct DpRam __iomem *)virtAddr;

	if ((Base < ONE_MEG) || (mode & BYTE_ACCESS_MODE))
		bits = BYTE_OPERATION;
	else
		bits = WORD_OPERATION;

	/*
	** Board has passed its scrub test. Fill in all the
	** transient stuff.
	*/
	p->RIOHosts[p->RIONumHosts].Caddr	= virtAddr;
	p->RIOHosts[p->RIONumHosts].CardP	= virtAddr;

	/*
	** Revision 01 AT host cards don't support WORD operations,
	*/
	if (readb(&cardp->DpRevision) == 01)
		bits = BYTE_OPERATION;

	p->RIOHosts[p->RIONumHosts].Type = RIO_AT;
	p->RIOHosts[p->RIONumHosts].Copy = rio_copy_to_card;
											/* set this later */
	p->RIOHosts[p->RIONumHosts].Slot = -1;
	p->RIOHosts[p->RIONumHosts].Mode = SLOW_LINKS | SLOW_AT_BUS | bits;
	writeb(BOOT_FROM_RAM | EXTERNAL_BUS_OFF | p->RIOHosts[p->RIONumHosts].Mode | INTERRUPT_DISABLE ,
		&p->RIOHosts[p->RIONumHosts].Control);
	writeb(0xFF, &p->RIOHosts[p->RIONumHosts].ResetInt);
	writeb(BOOT_FROM_RAM | EXTERNAL_BUS_OFF | p->RIOHosts[p->RIONumHosts].Mode | INTERRUPT_DISABLE,
		&p->RIOHosts[p->RIONumHosts].Control);
	writeb(0xFF, &p->RIOHosts[p->RIONumHosts].ResetInt);
	p->RIOHosts[p->RIONumHosts].UniqueNum =
		((readb(&p->RIOHosts[p->RIONumHosts].Unique[0])&0xFF)<<0)|
		((readb(&p->RIOHosts[p->RIONumHosts].Unique[1])&0xFF)<<8)|
		((readb(&p->RIOHosts[p->RIONumHosts].Unique[2])&0xFF)<<16)|
		((readb(&p->RIOHosts[p->RIONumHosts].Unique[3])&0xFF)<<24);
	rio_dprintk (RIO_DEBUG_INIT, "RIO-init: Uniquenum 0x%x\n",p->RIOHosts[p->RIONumHosts].UniqueNum);

	p->RIONumHosts++;
	rio_dprintk (RIO_DEBUG_INIT, "RIO-init: Tests Passed at 0x%x\n", Base);
	return(1);
}

static	u8	val[] = {
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
int RIOBoardTest(unsigned long paddr, void __iomem *caddr, unsigned char type, int slot)
{
	struct DpRam __iomem *DpRam = caddr;
	void __iomem *ram[4];
	int  size[4];
	int  op, bank;
	int  nbanks;

	rio_dprintk (RIO_DEBUG_INIT, "RIO-init: Reset host type=%d, DpRam=%p, slot=%d\n",
			type, DpRam, slot);

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

	ram[0] = DpRam->DpSram1;
	ram[1] = DpRam->DpSram2;
	ram[2] = DpRam->DpSram3;
	nbanks = (type == RIO_PCI) ? 3 : 4;
	if (nbanks == 4)
		ram[3] = DpRam->DpScratch;


	if (nbanks == 3) {
		rio_dprintk (RIO_DEBUG_INIT, "RIO-init: Memory: %p(0x%x), %p(0x%x), %p(0x%x)\n",
				ram[0], size[0], ram[1], size[1], ram[2], size[2]);
	} else {
		rio_dprintk (RIO_DEBUG_INIT, "RIO-init: %p(0x%x), %p(0x%x), %p(0x%x), %p(0x%x)\n",
				ram[0], size[0], ram[1], size[1], ram[2], size[2], ram[3], size[3]);
	}

	/*
	** This scrub operation will test for crosstalk between
	** banks. TEST_END is a magic number, and relates to the offset
	** within the 'val' array used by Scrub.
	*/
	for (op=0; op<TEST_END; op++) {
		for (bank=0; bank<nbanks; bank++) {
			if (RIOScrub(op, ram[bank], size[bank]) == RIO_FAIL) {
				rio_dprintk (RIO_DEBUG_INIT, "RIO-init: RIOScrub band %d, op %d failed\n", 
							bank, op);
				return RIO_FAIL;
			}
		}
	}

	rio_dprintk (RIO_DEBUG_INIT, "Test completed\n");
	return 0;
}


/*
** Scrub an area of RAM.
** Define PRETEST and POSTTEST for a more thorough checking of the
** state of the memory.
** Call with op set to an index into the above 'val' array to determine
** which value will be written into memory.
** Call with op set to zero means that the RAM will not be read and checked
** before it is written.
** Call with op not zero and the RAM will be read and compared with val[op-1]
** to check that the data from the previous phase was retained.
*/

static int RIOScrub(int op, u8 __iomem *ram, int size)
{
	int off;
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
			if (readb(ram + off) != oldbyte) {
				rio_dprintk (RIO_DEBUG_INIT, "RIO-init: Byte Pre Check 1: BYTE at offset 0x%x should have been=%x, was=%x\n", off, oldbyte, readb(ram + off));
				return RIO_FAIL;
			}
		}
		for (off=0; off<size; off+=2) {
			if (readw(ram + off) != oldword) {
				rio_dprintk (RIO_DEBUG_INIT, "RIO-init: Word Pre Check: WORD at offset 0x%x should have been=%x, was=%x\n",off,oldword, readw(ram + off));
				rio_dprintk (RIO_DEBUG_INIT, "RIO-init: Word Pre Check: BYTE at offset 0x%x is %x BYTE at offset 0x%x is %x\n", off, readb(ram + off), off+1, readb(ram+off+1));
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
		if (op && (readb(ram + off) != oldbyte)) {
			rio_dprintk (RIO_DEBUG_INIT, "RIO-init: Byte Pre Check 2: BYTE at offset 0x%x should have been=%x, was=%x\n", off, oldbyte, readb(ram + off));
			return RIO_FAIL;
		}
		writeb(invbyte, ram + off);
		if (readb(ram + off) != invbyte) {
			rio_dprintk (RIO_DEBUG_INIT, "RIO-init: Byte Inv Check: BYTE at offset 0x%x should have been=%x, was=%x\n", off, invbyte, readb(ram + off));
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
		if (readw(ram + off) != invword) {
			rio_dprintk (RIO_DEBUG_INIT, "RIO-init: Word Inv Check: WORD at offset 0x%x should have been=%x, was=%x\n", off, invword, readw(ram + off));
			rio_dprintk (RIO_DEBUG_INIT, "RIO-init: Word Inv Check: BYTE at offset 0x%x is %x BYTE at offset 0x%x is %x\n", off, readb(ram + off), off+1, readb(ram+off+1));
			return RIO_FAIL;
		}

		writew(newword, ram + off);
		if ( readw(ram + off) != newword ) {
			rio_dprintk (RIO_DEBUG_INIT, "RIO-init: Post Word Check 1: WORD at offset 0x%x should have been=%x, was=%x\n", off, newword, readw(ram + off));
			rio_dprintk (RIO_DEBUG_INIT, "RIO-init: Post Word Check 1: BYTE at offset 0x%x is %x BYTE at offset 0x%x is %x\n", off, readb(ram + off), off+1, readb(ram + off + 1));
			return RIO_FAIL;
		}
	}

	/*
	** now run through the block of memory again, first in byte mode
	** then in word mode, and check that all the locations contain the
	** required test data.
	*/
	for (off=0; off<size; off++) {
		if (readb(ram + off) != newbyte) {
			rio_dprintk (RIO_DEBUG_INIT, "RIO-init: Post Byte Check: BYTE at offset 0x%x should have been=%x, was=%x\n", off, newbyte, readb(ram + off));
			return RIO_FAIL;
		}
	}

	for (off=0; off<size; off+=2) {
		if (readw(ram + off) != newword ) {
			rio_dprintk (RIO_DEBUG_INIT, "RIO-init: Post Word Check 2: WORD at offset 0x%x should have been=%x, was=%x\n", off, newword, readw(ram + off));
			rio_dprintk (RIO_DEBUG_INIT, "RIO-init: Post Word Check 2: BYTE at offset 0x%x is %x BYTE at offset 0x%x is %x\n", off, readb(ram + off), off+1, readb(ram + off + 1));
			return RIO_FAIL;
		}
	}

	/*
	** time to check out byte swapping errors
	*/
	swapword = invbyte | (newbyte << 8);

	for (off=0; off<size; off+=2) {
		writeb(invbyte, &ram[off]);
		writeb(newbyte, &ram[off+1]);
	}

	for ( off=0; off<size; off+=2 ) {
		if (readw(ram + off) != swapword) {
			rio_dprintk (RIO_DEBUG_INIT, "RIO-init: SwapWord Check 1: WORD at offset 0x%x should have been=%x, was=%x\n", off, swapword, readw(ram + off));
			rio_dprintk (RIO_DEBUG_INIT, "RIO-init: SwapWord Check 1: BYTE at offset 0x%x is %x BYTE at offset 0x%x is %x\n", off, readb(ram + off), off+1, readb(ram + off + 1));
			return RIO_FAIL;
		}
		writew(~swapword, ram + off);
	}

	for (off=0; off<size; off+=2) {
		if (readb(ram + off) != newbyte) {
			rio_dprintk (RIO_DEBUG_INIT, "RIO-init: SwapWord Check 2: BYTE at offset 0x%x should have been=%x, was=%x\n", off, newbyte, readb(ram + off));
			return RIO_FAIL;
		}
		if (readb(ram + off + 1) != invbyte) {
			rio_dprintk (RIO_DEBUG_INIT, "RIO-init: SwapWord Check 2: BYTE at offset 0x%x should have been=%x, was=%x\n", off+1, invbyte, readb(ram + off + 1));
			return RIO_FAIL;
		}
		writew(newword, ram + off);
	}
	return 0;
}


int RIODefaultName(struct rio_info *p, struct Host *HostP, unsigned int	UnitId)
{
	memcpy(HostP->Mapping[UnitId].Name, "UNKNOWN RTA X-XX", 17);
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

static struct rioVersion	stVersion;

struct rioVersion *RIOVersid(void)
{
    strlcpy(stVersion.version, "RIO driver for linux V1.0",
	    sizeof(stVersion.version));
    strlcpy(stVersion.buildDate, __DATE__,
	    sizeof(stVersion.buildDate));

    return &stVersion;
}

void RIOHostReset(unsigned int Type, struct DpRam __iomem *DpRamP, unsigned int Slot)
{
	/*
	** Reset the Tpu
	*/
	rio_dprintk (RIO_DEBUG_INIT,  "RIOHostReset: type 0x%x", Type);
	switch ( Type ) {
	case RIO_AT:
		rio_dprintk (RIO_DEBUG_INIT, " (RIO_AT)\n");
		writeb(BOOT_FROM_RAM | EXTERNAL_BUS_OFF | INTERRUPT_DISABLE | BYTE_OPERATION |
			SLOW_LINKS | SLOW_AT_BUS, &DpRamP->DpControl);
		writeb(0xFF, &DpRamP->DpResetTpu);
		udelay(3);
			rio_dprintk (RIO_DEBUG_INIT,  "RIOHostReset: Don't know if it worked. Try reset again\n");
		writeb(BOOT_FROM_RAM | EXTERNAL_BUS_OFF | INTERRUPT_DISABLE |
			BYTE_OPERATION | SLOW_LINKS | SLOW_AT_BUS, &DpRamP->DpControl);
		writeb(0xFF, &DpRamP->DpResetTpu);
		udelay(3);
		break;
	case RIO_PCI:
		rio_dprintk (RIO_DEBUG_INIT, " (RIO_PCI)\n");
		writeb(RIO_PCI_BOOT_FROM_RAM, &DpRamP->DpControl);
		writeb(0xFF, &DpRamP->DpResetInt);
		writeb(0xFF, &DpRamP->DpResetTpu);
		udelay(100);
		break;
	default:
		rio_dprintk (RIO_DEBUG_INIT, " (UNKNOWN)\n");
		break;
	}
	return;
}
