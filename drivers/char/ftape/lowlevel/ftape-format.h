#ifndef _FTAPE_FORMAT_H
#define _FTAPE_FORMAT_H

/*
 * Copyright (C) 1996-1997 Claus-Justus Heine.

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
 * $Source: /homes/cvs/ftape-stacked/ftape/lowlevel/ftape-format.h,v $
 * $Revision: 1.2 $
 * $Date: 1997/10/05 19:18:13 $
 *
 *      This file contains the low level definitions for the
 *      formatting support for the QIC-40/80/3010/3020 floppy-tape
 *      driver "ftape" for Linux.
 */

#ifdef __KERNEL__
extern int ftape_format_track(const unsigned int track, const __u8 gap3);
extern int ftape_format_status(unsigned int *segment_id);
extern int ftape_verify_segment(const unsigned int segment_id, SectorMap *bsm);
#endif /* __KERNEL__ */

#endif
