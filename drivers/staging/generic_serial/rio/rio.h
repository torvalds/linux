/*
** -----------------------------------------------------------------------------
**
**  Perle Specialix driver for Linux
**  Ported from existing RIO Driver for SCO sources.
 *
 *  (C) 1990 - 1998 Specialix International Ltd., Byfleet, Surrey, UK.
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
**	Module		: rio.h
**	SID		: 1.3
**	Last Modified	: 11/6/98 11:34:13
**	Retrieved	: 11/6/98 11:34:22
**
**  ident @(#)rio.h	1.3
**
** -----------------------------------------------------------------------------
*/

#ifndef	__rio_rio_h__
#define	__rio_rio_h__

/*
**	Maximum numbers of things
*/
#define	RIO_SLOTS	4	/* number of configuration slots */
#define	RIO_HOSTS	4	/* number of hosts that can be found */
#define	PORTS_PER_HOST	128	/* number of ports per host */
#define	LINKS_PER_UNIT	4	/* number of links from a host */
#define	RIO_PORTS	(PORTS_PER_HOST * RIO_HOSTS)	/* max. no. of ports */
#define	RTAS_PER_HOST	(MAX_RUP)	/* number of RTAs per host */
#define	PORTS_PER_RTA	(PORTS_PER_HOST/RTAS_PER_HOST)	/* ports on a rta */
#define	PORTS_PER_MODULE 4	/* number of ports on a plug-in module */
				/* number of modules on an RTA */
#define	MODULES_PER_RTA	 (PORTS_PER_RTA/PORTS_PER_MODULE)
#define MAX_PRODUCT	16	/* numbr of different product codes */
#define MAX_MODULE_TYPES 16	/* number of different types of module */

#define RIO_CONTROL_DEV	128	/* minor number of host/control device */
#define RIO_INVALID_MAJOR 0	/* test first host card's major no for validity */

/*
** number of RTAs that can be bound to a master
*/
#define MAX_RTA_BINDINGS (MAX_RUP * RIO_HOSTS)

/*
**	Unit types
*/
#define PC_RTA16	0x90000000
#define PC_RTA8		0xe0000000
#define TYPE_HOST	0
#define TYPE_RTA8	1
#define TYPE_RTA16	2

/*
**	Flag values returned by functions
*/

#define	RIO_FAIL	-1

/*
** SysPort value for something that hasn't any ports
*/
#define	NO_PORT	0xFFFFFFFF

/*
** Unit ID Of all hosts
*/
#define	HOST_ID	0

/*
** Break bytes into nybles
*/
#define	LONYBLE(X)	((X) & 0xF)
#define	HINYBLE(X)	(((X)>>4) & 0xF)

/*
** Flag values passed into some functions
*/
#define	DONT_SLEEP	0
#define	OK_TO_SLEEP	1

#define	DONT_PRINT	1
#define	DO_PRINT	0

#define PRINT_TO_LOG_CONS	0
#define PRINT_TO_CONS	1
#define PRINT_TO_LOG	2

/*
** Timeout has trouble with times of less than 3 ticks...
*/
#define	MIN_TIMEOUT	3

/*
**	Generally useful constants
*/

#define	HUNDRED_MS		((HZ/10)?(HZ/10):1)
#define	ONE_MEG			0x100000
#define	SIXTY_FOUR_K		0x10000

#define	RIO_AT_MEM_SIZE		SIXTY_FOUR_K
#define	RIO_EISA_MEM_SIZE	SIXTY_FOUR_K
#define	RIO_MCA_MEM_SIZE	SIXTY_FOUR_K

#define	COOK_WELL		0
#define	COOK_MEDIUM		1
#define	COOK_RAW		2

/*
**	Pointer manipulation stuff
**	RIO_PTR takes hostp->Caddr and the offset into the DP RAM area
**	and produces a UNIX caddr_t (pointer) to the object
**	RIO_OBJ takes hostp->Caddr and a UNIX pointer to an object and
**	returns the offset into the DP RAM area.
*/
#define	RIO_PTR(C,O) (((unsigned char __iomem *)(C))+(0xFFFF&(O)))
#define	RIO_OFF(C,O) ((unsigned char __iomem *)(O)-(unsigned char __iomem *)(C))

/*
**	How to convert from various different device number formats:
**	DEV is a dev number, as passed to open, close etc - NOT a minor
**	number!
**/

#define	RIO_MODEM_MASK		0x1FF
#define	RIO_MODEM_BIT		0x200
#define	RIO_UNMODEM(DEV)	(MINOR(DEV) & RIO_MODEM_MASK)
#define	RIO_ISMODEM(DEV)	(MINOR(DEV) & RIO_MODEM_BIT)
#define RIO_PORT(DEV,FIRST_MAJ)	( (MAJOR(DEV) - FIRST_MAJ) * PORTS_PER_HOST) \
					+ MINOR(DEV)
#define CSUM(pkt_ptr)  (((u16 *)(pkt_ptr))[0] + ((u16 *)(pkt_ptr))[1] + \
			((u16 *)(pkt_ptr))[2] + ((u16 *)(pkt_ptr))[3] + \
			((u16 *)(pkt_ptr))[4] + ((u16 *)(pkt_ptr))[5] + \
			((u16 *)(pkt_ptr))[6] + ((u16 *)(pkt_ptr))[7] + \
			((u16 *)(pkt_ptr))[8] + ((u16 *)(pkt_ptr))[9] )

#define	RIO_LINK_ENABLE	0x80FF	/* FF is a hack, mainly for Mips, to        */
			       /* prevent a really stupid race condition.  */

#define	NOT_INITIALISED	0
#define	INITIALISED	1

#define	NOT_POLLING	0
#define	POLLING		1

#define	NOT_CHANGED	0
#define	CHANGED		1

#define	NOT_INUSE	0

#define	DISCONNECT	0
#define	CONNECT		1

/* ------ Control Codes ------ */

#define	CONTROL		'^'
#define IFOAD		( CONTROL + 1 )
#define	IDENTIFY	( CONTROL + 2 )
#define	ZOMBIE		( CONTROL + 3 )
#define	UFOAD		( CONTROL + 4 )
#define IWAIT		( CONTROL + 5 )

#define	IFOAD_MAGIC	0xF0AD	/* of course */
#define	ZOMBIE_MAGIC	(~0xDEAD)	/* not dead -> zombie */
#define	UFOAD_MAGIC	0xD1E	/* kill-your-neighbour */
#define	IWAIT_MAGIC	0xB1DE	/* Bide your time */

/* ------ Error Codes ------ */

#define E_NO_ERROR                       ((ushort) 0)

/* ------ Free Lists ------ */

struct rio_free_list {
	u16 next;
	u16 prev;
};

/* NULL for card side linked lists */
#define	TPNULL	((ushort)(0x8000))
/* We can add another packet to a transmit queue if the packet pointer pointed
 * to by the TxAdd pointer has PKT_IN_USE clear in its address. */
#define PKT_IN_USE    0x1

/* ------ Topology ------ */

struct Top {
	u8 Unit;
	u8 Link;
};

#endif				/* __rio_h__ */
