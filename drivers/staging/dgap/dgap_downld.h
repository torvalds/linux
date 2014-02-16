/*
 * Copyright 2003 Digi International (www.digi.com)
 *      Scott H Kilau <Scott_Kilau at digi dot com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY, EXPRESS OR IMPLIED; without even the
 * implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 * PURPOSE.  See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * $Id: dgap_downld.h,v 1.1 2009/10/23 14:01:57 markh Exp $
 *
 *	NOTE: THIS IS A SHARED HEADER. DO NOT CHANGE CODING STYLE!!!
 *
 */

/*
** downld.h 
**  - describes the interface between the user level download process
**    and the concentrator download driver.
*/

#ifndef _DGAP_DOWNLD_H_
#define _DGAP_DOWNLD_H_


struct fepimg {
    int type;				/* board type */
    int	len;				/* length of image */
    char fepimage[1];			/* beginning of image */
};

struct downldio {
    unsigned int req_type;		/* FEP or concentrator */
    unsigned int bdid;			/* opaque board identifier */
    union {
	struct downld_t dl;		/* download structure */
	struct fepimg   fi;		/* fep/bios image structure */
    } image;
};

#define DIGI_DLREQ_GET	(('d'<<8) | 220)
#define DIGI_DLREQ_SET	(('d'<<8) | 221)

#define DIGI_DL_NUKE    (('d'<<8) | 222) /* Not really a dl request, but
					  dangerous enuff to not put in
					  digi.h */
/* Packed bits of intarg for DIGI_DL_NUKE */
#define DIGI_NUKE_RESET_ALL	 (1 << 31)
#define DIGI_NUKE_INHIBIT_POLLER (1 << 30)
#define DIGI_NUKE_BRD_NUMB        0x0f
	


#define	DLREQ_BIOS	0
#define	DLREQ_FEP	1
#define	DLREQ_CONC	2
#define	DLREQ_CONFIG	3
#define DLREQ_DEVCREATE 4

#endif
