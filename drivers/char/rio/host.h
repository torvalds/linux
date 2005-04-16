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

#ifdef SCCS_LABELS
#ifndef lint
static char *_host_h_sccs_ = "@(#)host.h	1.2";
#endif
#endif

/*
** the host structure - one per host card in the system.
*/

#define	MAX_EXTRA_UNITS	64

/*
**    Host data structure. This is used for the software equiv. of
**    the host.
*/
struct    Host
{
    uchar             	    Type;      /* RIO_EISA, RIO_MCA, ... */
    uchar             	    Ivec;      /* POLLED or ivec number */
    uchar             	    Mode;      /* Control stuff */
    uchar                   Slot;      /* Slot */
    volatile caddr_t        Caddr;     /* KV address of DPRAM */
    volatile struct DpRam  *CardP;     /* KV address of DPRAM, with overlay */
    paddr_t          	    PaddrP;    /* Phys. address of DPRAM */
    char                    Name[MAX_NAME_LEN];  /* The name of the host */
    uint            	    UniqueNum; /* host unique number */
    spinlock_t	            HostLock;  /* Lock structure for MPX */
    /*struct pci_devinfo    PciDevInfo; *//* PCI Bus/Device/Function stuff */
    /*struct lockb	    HostLock;  *//* Lock structure for MPX */
    uint                    WorkToBeDone; /* set to true each interrupt */
    uint                    InIntr;    /* Being serviced? */
    uint                    IntSrvDone;/* host's interrupt has been serviced */
    int			    (*Copy)( caddr_t, caddr_t, int ); /* copy func */
    struct timer_list timer;
    /*
    **               I M P O R T A N T !
    **
    ** The rest of this data structure is cleared to zero after
    ** a RIO_HOST_FOAD command.
    */
    
    ulong                   Flags;     /* Whats going down */
#define RC_WAITING            0
#define RC_STARTUP            1
#define RC_RUNNING            2
#define RC_STUFFED            3
#define RC_SOMETHING          4
#define RC_SOMETHING_NEW      5
#define RC_SOMETHING_ELSE     6
#define RC_READY              7
#define RUN_STATE             7
/*
** Boot mode applies to the way in which hosts in this system will
** boot RTAs
*/
#define RC_BOOT_ALL           0x8	/* Boot all RTAs attached */
#define RC_BOOT_OWN           0x10	/* Only boot RTAs bound to this system */
#define RC_BOOT_NONE          0x20	/* Don't boot any RTAs (slave mode) */

    struct Top		    Topology[LINKS_PER_UNIT]; /* one per link */
    struct Map              Mapping[MAX_RUP];     /* Mappings for host */
    struct PHB		    *PhbP;                /* Pointer to the PHB array */
    ushort           	    *PhbNumP;             /* Ptr to Number of PHB's */
    struct LPB 	            *LinkStrP ;           /* Link Structure Array */
    struct RUP       	    *RupP;                /* Sixteen real rups here */
    struct PARM_MAP  	    *ParmMapP;            /* points to the parmmap */
    uint                    ExtraUnits[MAX_EXTRA_UNITS]; /* unknown things */
    uint                    NumExtraBooted;       /* how many of the above */
    /*
    ** Twenty logical rups.
    ** The first sixteen are the real Rup entries (above), the last four
    ** are the link RUPs.
    */
    struct UnixRup	    UnixRups[MAX_RUP+LINKS_PER_UNIT];
	int				timeout_id;	/* For calling 100 ms delays */
	int				timeout_sem;/* For calling 100 ms delays */
    long locks; /* long req'd for set_bit --RR */
    char             	    ____end_marker____;
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

#define RIO_DUMBPARM 0x0860    /* what not to expect */

#endif
