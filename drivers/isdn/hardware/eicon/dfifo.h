
/*
 *
 Copyright (c) Eicon Networks, 2002.
 *
 This source file is supplied for the use with
 Eicon Networks range of DIVA Server Adapters.
 *
 Eicon File Revision :    2.1
 *
 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation; either version 2, or (at your option)
 any later version.
 *
 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY OF ANY KIND WHATSOEVER INCLUDING ANY
 implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 See the GNU General Public License for more details.
 *
 You should have received a copy of the GNU General Public License
 along with this program; if not, write to the Free Software
 Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */
#ifndef __DIVA_IDI_DFIFO_INC__
#define __DIVA_IDI_DFIFO_INC__
#define DIVA_DFIFO_CACHE_SZ   64 /* Used to isolate pipe from
				    rest of the world
				    should be divisible by 4
				 */
#define DIVA_DFIFO_RAW_SZ    (2512 * 8)
#define DIVA_DFIFO_DATA_SZ   68
#define DIVA_DFIFO_HDR_SZ    4
#define DIVA_DFIFO_SEGMENT_SZ  (DIVA_DFIFO_DATA_SZ + DIVA_DFIFO_HDR_SZ)
#define DIVA_DFIFO_SEGMENTS   ((DIVA_DFIFO_RAW_SZ) / (DIVA_DFIFO_SEGMENT_SZ) + 1)
#define DIVA_DFIFO_MEM_SZ (						\
		(DIVA_DFIFO_SEGMENT_SZ) * (DIVA_DFIFO_SEGMENTS) +	\
		(DIVA_DFIFO_CACHE_SZ) * 2				\
		)
#define DIVA_DFIFO_STEP DIVA_DFIFO_SEGMENT_SZ
/* -------------------------------------------------------------------------
   Block header layout is:
   byte[0] -> flags
   byte[1] -> length of data in block
   byte[2] -> reserved
   byte[4] -> reserved
   ------------------------------------------------------------------------- */
#define DIVA_DFIFO_WRAP   0x80 /* This is the last block in fifo   */
#define DIVA_DFIFO_READY  0x40 /* This block is ready for processing */
#define DIVA_DFIFO_LAST   0x20 /* This block is last in message      */
#define DIVA_DFIFO_AUTO   0x10 /* Don't look for 'ready', don't ack */
int diva_dfifo_create(void *start, int length);
#endif
