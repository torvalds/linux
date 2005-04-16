#ifndef _FTAPE_WRITE_H
#define _FTAPE_WRITE_H

/*
 * Copyright (C) 1994-1995 Bas Laarhoven,
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
 $Source: /homes/cvs/ftape-stacked/ftape/lowlevel/ftape-write.h,v $
 $Author: claus $
 *
 $Revision: 1.2 $
 $Date: 1997/10/05 19:18:30 $
 $State: Exp $
 *
 *      This file contains the definitions for the write functions
 *      for the QIC-117 floppy-tape driver for Linux.
 *
 */


/*      ftape-write.c defined global functions.
 */
typedef enum {
	FT_WR_ASYNC  = 0, /* start tape only when all buffers are full */
	FT_WR_MULTI  = 1, /* start tape, but don't necessarily stop */
	FT_WR_SINGLE = 2, /* write a single segment and stop afterwards */
	FT_WR_DELETE = 3  /* write deleted data marks */
} ft_write_mode_t;

extern int  ftape_start_writing(const ft_write_mode_t mode);
extern int  ftape_write_segment(const int segment,
				const void *address, 
				const ft_write_mode_t flushing);
extern void ftape_zap_write_buffers(void);
extern int  ftape_loop_until_writes_done(void);

#endif				/* _FTAPE_WRITE_H */

