#ifndef _ZFTAPE_EOF_H
#define _ZFTAPE_EOF_H

/*
 * Copyright (C) 1994-1995 Bas Laarhoven.
 * adaptaed for zftape 1996, 1997 by Claus Heine

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
 * $Source: /homes/cvs/ftape-stacked/ftape/zftape/zftape-eof.h,v $
 * $Revision: 1.2 $
 * $Date: 1997/10/05 19:19:03 $
 *
 *      Definitions and declarations for the end of file markers
 *      for the QIC-40/80 floppy-tape driver for Linux.
 */

#include <linux/ftape-header-segment.h>
#include "../zftape/zftape-buffers.h"
/*  failed sector log size (only used if format code != 4).
 */

typedef union {
	ft_fsl_entry mark;
	__u32 entry;
} eof_mark_union;
 
/*      ftape-eof.c defined global vars.
 */
extern int zft_nr_eof_marks;
extern eof_mark_union *zft_eof_map;

/*      ftape-eof.c defined global functions.
 */
extern void zft_ftape_extract_file_marks(__u8* address);
extern int  zft_ftape_validate_label(char* label);
extern void zft_clear_ftape_file_marks(void);

#endif
