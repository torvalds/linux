#ifndef _FTAPE_BSM_H
#define _FTAPE_BSM_H

/*
 * Copyright (C) 1994-1996 Bas Laarhoven,
 *           (C) 1996-1997 Claus-Justus Heine.

 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation; either version 2, or (at your option)
 any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program; see the file COPYING.  If not, write to
 the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.

 *
 * $Source: /homes/cvs/ftape-stacked/ftape/lowlevel/ftape-bsm.h,v $
 * $Revision: 1.2 $
 * $Date: 1997/10/05 19:18:07 $
 *
 *      This file contains definitions for the bad sector map handling
 *      routines for the QIC-117 floppy-tape driver for Linux.
 */

#include <linux/ftape.h>
#include <linux/ftape-header-segment.h>

#define EMPTY_SEGMENT           (0xffffffff)
#define FAKE_SEGMENT            (0xfffffffe)

/*  maximum (format code 4) bad sector map size (bytes).
 */
#define BAD_SECTOR_MAP_SIZE     (29 * SECTOR_SIZE - 256)

/*  format code 4 bad sector entry, ftape uses this
 *  internally for all format codes
 */
typedef __u32 SectorMap;
/*  variable and 1100 ft bad sector map entry. These three bytes represent
 *  a single sector address measured from BOT. 
 */
typedef struct NewSectorMap {          
	__u8 bytes[3];
} SectorCount;


/*
 *      ftape-bsm.c defined global vars.
 */

/*
 *      ftape-bsm.c defined global functions.
 */
extern void update_bad_sector_map(__u8 * buffer);
extern void ftape_extract_bad_sector_map(__u8 * buffer);
extern SectorMap ftape_get_bad_sector_entry(int segment_id);
extern __u8 *ftape_find_end_of_bsm_list(__u8 * address);
extern void ftape_init_bsm(void);

#endif
