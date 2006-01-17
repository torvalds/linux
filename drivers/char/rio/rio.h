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

#ifdef SCCS_LABELS
static char *_rio_h_sccs_ = "@(#)rio.h	1.3";
#endif

/*
** 30.09.1998 ARG -
** Introduced driver version and host card type strings
*/
#define RIO_DRV_STR "Specialix RIO Driver"
#define RIO_AT_HOST_STR "ISA"
#define RIO_PCI_HOST_STR "PCI"


/*
** rio_info_store() commands (arbitary values) :
*/
#define RIO_INFO_PUT	0xA4B3C2D1
#define RIO_INFO_GET	0xF1E2D3C4


/*
** anything that I couldn't cram in somewhere else
*/
/*
#ifndef RIODEBUG
#define debug
#else
#define debug rioprint
#endif
*/


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
#define	RIO_SUCCESS	0
#define	COPYFAIL	-1	/* copy[in|out] failed */

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
#define	HALF_A_SECOND		((HZ)>>1)
#define	A_SECOND		(HZ)
#define	HUNDRED_HZ		((HZ/100)?(HZ/100):1)
#define	FIFTY_HZ		((HZ/50)?(HZ/50):1)
#define	TWENTY_HZ		((HZ/20)?(HZ/20):1)
#define	TEN_HZ			((HZ/10)?(HZ/10):1)
#define	FIVE_HZ			((HZ/5)?(HZ/5):1)
#define	HUNDRED_MS		TEN_HZ
#define	FIFTY_MS		TWENTY_HZ
#define	TWENTY_MS		FIFTY_HZ
#define	TEN_MS			HUNDRED_HZ
#define	TWO_SECONDS		((A_SECOND)*2)
#define	FIVE_SECONDS		((A_SECOND)*5)
#define	TEN_SECONDS		((A_SECOND)*10)
#define	FIFTEEN_SECONDS		((A_SECOND)*15)
#define	TWENTY_SECONDS		((A_SECOND)*20)
#define	HALF_A_MINUTE		(A_MINUTE>>1)
#define	A_MINUTE		(A_SECOND*60)
#define	FIVE_MINUTES		(A_MINUTE*5)
#define	QUARTER_HOUR		(A_MINUTE*15)
#define	HALF_HOUR		(A_MINUTE*30)
#define	HOUR			(A_MINUTE*60)

#define	SIXTEEN_MEG		0x1000000
#define	ONE_MEG			0x100000
#define	SIXTY_FOUR_K		0x10000

#define	RIO_AT_MEM_SIZE		SIXTY_FOUR_K
#define	RIO_EISA_MEM_SIZE	SIXTY_FOUR_K
#define	RIO_MCA_MEM_SIZE	SIXTY_FOUR_K

#define	POLL_VECTOR		0x100

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
#define	RIO_PTR(C,O) (((caddr_t)(C))+(0xFFFF&(O)))
#define	RIO_OFF(C,O) ((int)(O)-(int)(C))

/*
**	How to convert from various different device number formats:
**	DEV is a dev number, as passed to open, close etc - NOT a minor
**	number!
**
**	Note:	LynxOS only gives us 8 bits for the device minor number,
**		so all this crap here to deal with 'modem' bits etc. is
**		just a load of irrelevant old bunkum!
**		This however does not stop us needing to define a value
**		for RIO_MODEMOFFSET which is required by the 'riomkdev'
**		utility in the New Config Utilities suite.
*/
/* 0-511: direct 512-1023: modem */
#define	RIO_MODEMOFFSET		0x200	/* doesn't mean anything */
#define	RIO_MODEM_MASK		0x1FF
#define	RIO_MODEM_BIT		0x200
#define	RIO_UNMODEM(DEV)	(MINOR(DEV) & RIO_MODEM_MASK)
#define	RIO_ISMODEM(DEV)	(MINOR(DEV) & RIO_MODEM_BIT)
#define RIO_PORT(DEV,FIRST_MAJ)	( (MAJOR(DEV) - FIRST_MAJ) * PORTS_PER_HOST) \
					+ MINOR(DEV)

#define	splrio	spltty

#define	RIO_IPL	5
#define	RIO_PRI	(PZERO+10)
#define RIO_CLOSE_PRI	PZERO-1	/* uninterruptible sleeps for close */

typedef struct DbInf {
	uint Flag;
	char Name[8];
} DbInf;

#ifndef TRUE
#define	TRUE (1==1)
#endif
#ifndef FALSE
#define	FALSE	(!TRUE)
#endif

#define CSUM(pkt_ptr)  (((ushort *)(pkt_ptr))[0] + ((ushort *)(pkt_ptr))[1] + \
			((ushort *)(pkt_ptr))[2] + ((ushort *)(pkt_ptr))[3] + \
			((ushort *)(pkt_ptr))[4] + ((ushort *)(pkt_ptr))[5] + \
			((ushort *)(pkt_ptr))[6] + ((ushort *)(pkt_ptr))[7] + \
			((ushort *)(pkt_ptr))[8] + ((ushort *)(pkt_ptr))[9] )

/*
** This happy little macro copies SIZE bytes of data from FROM to TO
** quite well. SIZE must be a constant.
*/
#define CCOPY( FROM, TO, SIZE ) { *(struct s { char data[SIZE]; } *)(TO) = *(struct s *)(FROM); }

/*
** increment a buffer pointer modulo the size of the buffer...
*/
#define	BUMP( P, I )	((P) = (((P)+(I)) & RIOBufferMask))

#define INIT_PACKET( PK, PP ) \
{ \
	*((uint *)PK)    = PP->PacketInfo; \
}

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


/*
** Machine types - these must NOT overlap with product codes 0-15
*/
#define	RIO_MIPS_R3230	31
#define	RIO_MIPS_R4030	32

#define	RIO_IO_UNKNOWN	-2

#undef	MODERN
#define	ERROR( E )	do { u.u_error = E; return OPENFAIL } while ( 0 )

/* Defines for MPX line discipline routines */

#define DIST_LINESW_OPEN	0x01
#define DIST_LINESW_CLOSE	0x02
#define DIST_LINESW_READ	0x04
#define DIST_LINESW_WRITE	0x08
#define DIST_LINESW_IOCTL	0x10
#define DIST_LINESW_INPUT	0x20
#define DIST_LINESW_OUTPUT	0x40
#define DIST_LINESW_MDMINT	0x80

#endif				/* __rio_h__ */
