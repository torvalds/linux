/* SPDX-License-Identifier: GPL-2.0-or-later */
/******************************************************************************
 *
 *	(C)Copyright 1998,1999 SysKonnect,
 *	a business unit of Schneider & Koch & Co. Datensysteme GmbH.
 *
 *	The information in this file is provided "AS IS" without warranty.
 *
 ******************************************************************************/

#ifndef	_FDDI_
#define _FDDI_

struct fddi_addr {
	u_char	a[6] ;
} ;

#define GROUP_ADDR	0x80		/* MSB in a[0] */

struct fddi_mac {
	struct fddi_addr	mac_dest ;
	struct fddi_addr	mac_source ;
	u_char			mac_info[4478] ;
} ;

#define FDDI_MAC_SIZE	(12)
#define FDDI_RAW_MTU	(4500-5)	/* exl. Pr,SD, ED/FS */
#define FDDI_RAW	(4500)

/*
 * FC values
 */
#define FC_VOID		0x40		/* void frame */
#define FC_TOKEN	0x80		/* token */
#define FC_RES_TOKEN	0xc0		/* restricted token */
#define FC_SMT_INFO	0x41		/* SMT Info frame */
/*
 * FC_SMT_LAN_LOC && FC_SMT_LOC are SK specific !
 */
#define FC_SMT_LAN_LOC	0x42		/* local SMT Info frame */
#define FC_SMT_LOC	0x43		/* local SMT Info frame */
#define FC_SMT_NSA	0x4f		/* SMT NSA frame */
#define FC_MAC		0xc0		/* MAC frame */
#define FC_BEACON	0xc2		/* MAC beacon frame */
#define FC_CLAIM	0xc3		/* MAC claim frame */
#define FC_SYNC_LLC	0xd0		/* sync. LLC frame */
#define FC_ASYNC_LLC	0x50		/* async. LLC frame */
#define FC_SYNC_BIT	0x80		/* sync. bit in FC */

#define FC_LLC_PRIOR	0x07		/* priority bits */

#define BEACON_INFO	0		/* beacon type */
#define DBEACON_INFO	1		/* beacon type DIRECTED */


/*
 * indicator bits
 */
#define C_INDICATOR	(1<<0)
#define A_INDICATOR	(1<<1)
#define E_INDICATOR	(1<<2)
#define I_INDICATOR	(1<<6)		/* SK specific */ 
#define L_INDICATOR	(1<<7)		/* SK specific */

#endif	/* _FDDI_ */
