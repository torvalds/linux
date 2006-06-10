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
**	Module		: host.h
**	SID		: 1.2
**	Last Modified	: 11/6/98 11:34:10
**	Retrieved	: 11/6/98 11:34:21
**
**  ident @(#)host.h	1.2
**
** -----------------------------------------------------------------------------
*/

#ifndef __rio_host_h__
#define __rio_host_h__

/*
** the host structure - one per host card in the system.
*/

#define	MAX_EXTRA_UNITS	64

/*
**    Host data structure. This is used for the software equiv. of
**    the host.
*/
struct Host {
	unsigned char Type;		/* RIO_EISA, RIO_MCA, ... */
	unsigned char Ivec;		/* POLLED or ivec number */
	unsigned char Mode;		/* Control stuff */
	unsigned char Slot;		/* Slot */
	caddr_t Caddr;			/* KV address of DPRAM */
	struct DpRam *CardP;		/* KV address of DPRAM, with overlay */
	unsigned long PaddrP;		/* Phys. address of DPRAM */
	char Name[MAX_NAME_LEN];	/* The name of the host */
	unsigned int UniqueNum;		/* host unique number */
	spinlock_t HostLock;	/* Lock structure for MPX */
	unsigned int WorkToBeDone;	/* set to true each interrupt */
	unsigned int InIntr;		/* Being serviced? */
	unsigned int IntSrvDone;	/* host's interrupt has been serviced */
	void (*Copy) (void *, void *, int);	/* copy func */
	struct timer_list timer;
	/*
	 **               I M P O R T A N T !
	 **
	 ** The rest of this data structure is cleared to zero after
	 ** a RIO_HOST_FOAD command.
	 */

	unsigned long Flags;			/* Whats going down */
#define RC_WAITING            0
#define RC_STARTUP            1
#define RC_RUNNING            2
#define RC_STUFFED            3
#define RC_READY              7
#define RUN_STATE             7
/*
** Boot mode applies to the way in which hosts in this system will
** boot RTAs
*/
#define RC_BOOT_ALL           0x8		/* Boot all RTAs attached */
#define RC_BOOT_OWN           0x10		/* Only boot RTAs bound to this system */
#define RC_BOOT_NONE          0x20		/* Don't boot any RTAs (slave mode) */

	struct Top Topology[LINKS_PER_UNIT];	/* one per link */
	struct Map Mapping[MAX_RUP];		/* Mappings for host */
	struct PHB *PhbP;			/* Pointer to the PHB array */
	unsigned short *PhbNumP;		/* Ptr to Number of PHB's */
	struct LPB *LinkStrP;			/* Link Structure Array */
	struct RUP *RupP;			/* Sixteen real rups here */
	struct PARM_MAP *ParmMapP;		/* points to the parmmap */
	unsigned int ExtraUnits[MAX_EXTRA_UNITS];	/* unknown things */
	unsigned int NumExtraBooted;		/* how many of the above */
	/*
	 ** Twenty logical rups.
	 ** The first sixteen are the real Rup entries (above), the last four
	 ** are the link RUPs.
	 */
	struct UnixRup UnixRups[MAX_RUP + LINKS_PER_UNIT];
	int timeout_id;				/* For calling 100 ms delays */
	int timeout_sem;			/* For calling 100 ms delays */
	long locks;				/* long req'd for set_bit --RR */
	char ____end_marker____;
};
#define Control      CardP->DpControl
#define SetInt       CardP->DpSetInt
#define ResetTpu     CardP->DpResetTpu
#define ResetInt     CardP->DpResetInt
#define Signature    CardP->DpSignature
#define Sram1        CardP->DpSram1
#define Sram2        CardP->DpSram2
#define Sram3        CardP->DpSram3
#define Scratch      CardP->DpScratch
#define __ParmMapR   CardP->DpParmMapR
#define SLX          CardP->DpSlx
#define Revision     CardP->DpRevision
#define Unique       CardP->DpUnique
#define Year         CardP->DpYear
#define Week         CardP->DpWeek

#define RIO_DUMBPARM 0x0860	/* what not to expect */

#endif
