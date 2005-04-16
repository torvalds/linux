#ifndef _ZFTAPE_RW_H
#define _ZFTAPE_RW_H

/*
 * Copyright (C) 1996, 1997 Claus-Justus Heine.

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
 * $Source: /homes/cvs/ftape-stacked/ftape/zftape/zftape-rw.h,v $
 * $Revision: 1.2 $
 * $Date: 1997/10/05 19:19:09 $
 *
 *      This file contains the definitions for the read and write
 *      functions for the QIC-117 floppy-tape driver for Linux.
 *
 */

#include <linux/config.h> /* for CONFIG_ZFT_DFLT_BLK_SZ */
#include "../zftape/zftape-buffers.h"

#define SEGMENTS_PER_TAPE  (ft_segments_per_track * ft_tracks_per_tape)

/*  QIC-113 Rev. G says that `a maximum of 63488 raw bytes may be
 *  compressed into a single frame'.
 *  Maybe we should stick to 32kb to make it more `beautiful'
 */
#define ZFT_MAX_BLK_SZ           (62*1024) /* bytes */
#if !defined(CONFIG_ZFT_DFLT_BLK_SZ)
# define CONFIG_ZFT_DFLT_BLK_SZ   (10*1024) /* bytes, default of gnu tar */
#elif CONFIG_ZFT_DFLT_BLK_SZ == 0
# undef  CONFIG_ZFT_DFLT_BLK_SZ
# define CONFIG_ZFT_DFLT_BLK_SZ 1
#elif (CONFIG_ZFT_DFLT_BLK_SZ % 1024) != 0
# error CONFIG_ZFT_DFLT_BLK_SZ must be 1 or a multiple of 1024
#endif
/* The *optional* compression routines need some overhead per tape
 *  block for their purposes. Instead of asking the actual compression
 *  implementation how much it needs, we restrict this overhead to be
 *  maximal of ZFT_CMPT_OVERHEAD size. We need this for EOT
 *  conditions. The tape is assumed to be logical at EOT when the
 *  distance from the physical EOT is less than 
 *  one tape block + ZFT_CMPR_OVERHEAD 
 */
#define ZFT_CMPR_OVERHEAD 16        /* bytes */

typedef enum
{ 
	zft_idle = 0,
	zft_reading,
	zft_writing,
} zft_status_enum;

typedef struct               /*  all values measured in bytes */
{
	int   seg_pos;       /*  segment currently positioned at */
	int   seg_byte_pos;  /*  offset in current segment */ 
	__s64 tape_pos;      /*  real offset from BOT */
	__s64 volume_pos;    /*  pos. in uncompressed data stream in
			      *  current volume 
			      */
} zft_position; 

extern zft_position zft_pos;
extern __u8 *zft_deblock_buf;
extern __u8 *zft_hseg_buf;
extern int zft_deblock_segment;
extern zft_status_enum zft_io_state;
extern int zft_header_changed;
extern int zft_qic113; /* conform to old specs. and old zftape */
extern int zft_use_compression;
extern unsigned int zft_blk_sz;
extern __s64 zft_capacity;
extern unsigned int zft_written_segments;
extern int zft_label_changed;

/*  zftape-rw.c exported functions
 */
extern unsigned int zft_get_seg_sz(unsigned int segment);
extern void  zft_set_flags(unsigned int minor_unit);
extern int   zft_calc_seg_byte_coord(int *seg_byte_pos, __s64 tape_pos);
extern __s64 zft_calc_tape_pos(int segment);
extern __s64 zft_get_capacity(void);
extern void  zft_update_label(__u8 *buffer);
extern int   zft_erase(void);
extern int   zft_verify_write_segments(unsigned int segment, 
				       __u8 *data, size_t size, __u8 *buffer);
extern unsigned int zft_get_time(void);
#endif /* _ZFTAPE_RW_H */

