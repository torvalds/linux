#ifndef _FTAPE_READ_H
#define _FTAPE_READ_H

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
 * $Source: /homes/cvs/ftape-stacked/ftape/lowlevel/ftape-read.h,v $
 * $Revision: 1.2 $
 * $Date: 1997/10/05 19:18:22 $
 *
 *      This file contains the definitions for the read functions
 *      for the QIC-117 floppy-tape driver for Linux.
 *
 */

/*      ftape-read.c defined global functions.
 */
typedef enum {
	FT_RD_SINGLE = 0,
	FT_RD_AHEAD  = 1,
} ft_read_mode_t;

extern int ftape_read_header_segment(__u8 *address);
extern int ftape_decode_header_segment(__u8 *address);
extern int ftape_read_segment_fraction(const int segment,
				       void  *address, 
				       const ft_read_mode_t read_mode,
				       const int start,
				       const int size);
#define ftape_read_segment(segment, address, read_mode)			\
	ftape_read_segment_fraction(segment, address, read_mode,	\
				    0, FT_SEGMENT_SIZE)
extern void ftape_zap_read_buffers(void);

#endif				/* _FTAPE_READ_H */
