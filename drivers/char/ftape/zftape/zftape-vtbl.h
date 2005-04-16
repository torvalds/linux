#ifndef _ZFTAPE_VTBL_H
#define _ZFTAPE_VTBL_H

/*
 *      Copyright (c) 1995-1997  Claus-Justus Heine

 This program is free software; you can redistribute it and/or
 modify it under the terms of the GNU General Public License as
 published by the Free Software Foundation; either version 2, or (at
 your option) any later version.
 
 This program is distributed in the hope that it will be useful, but
 WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 General Public License for more details.
 
 You should have received a copy of the GNU General Public License
 along with this program; see the file COPYING.  If not, write to
 the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139,
 USA.

 *
 * $Source: /homes/cvs/ftape-stacked/ftape/zftape/zftape-vtbl.h,v $
 * $Revision: 1.3 $
 * $Date: 1997/10/28 14:30:09 $
 *
 *      This file defines a volume table as defined in the QIC-80
 *      development standards.
 */

#include <linux/list.h>

#include "../lowlevel/ftape-tracing.h"

#include "../zftape/zftape-eof.h"
#include "../zftape/zftape-ctl.h"
#include "../zftape/zftape-rw.h"

#define VTBL_SIZE 128 /* bytes */

/* The following are offsets in the vtbl.  */
#define VTBL_SIG   0
#define VTBL_START 4
#define VTBL_END   6
#define VTBL_DESC  8
#define VTBL_DATE  52
#define VTBL_FLAGS 56
#define VTBL_FL_VENDOR_SPECIFIC (1<<0)
#define VTBL_FL_MUTLI_CARTRIDGE (1<<1)
#define VTBL_FL_NOT_VERIFIED    (1<<2)
#define VTBL_FL_REDIR_INHIBIT   (1<<3)
#define VTBL_FL_SEG_SPANNING    (1<<4)
#define VTBL_FL_DIRECTORY_LAST  (1<<5)
#define VTBL_FL_RESERVED_6      (1<<6)
#define VTBL_FL_RESERVED_7      (1<<7)
#define VTBL_M_NO  57
#define VTBL_EXT   58
#define EXT_ZFTAPE_SIG     0
#define EXT_ZFTAPE_BLKSZ  10
#define EXT_ZFTAPE_CMAP   12
#define EXT_ZFTAPE_QIC113 13
#define VTBL_PWD   84
#define VTBL_DIR_SIZE 92
#define VTBL_DATA_SIZE 96
#define VTBL_OS_VERSION 104
#define VTBL_SRC_DRIVE  106
#define VTBL_DEV        122
#define VTBL_RESERVED_1 123
#define VTBL_CMPR       124
#define VTBL_CMPR_UNREG 0x3f
#define VTBL_CMPR_USED  0x80
#define VTBL_FMT        125
#define VTBL_RESERVED_2 126
#define VTBL_RESERVED_3 127
/* compatibility with pre revision K */
#define VTBL_K_CMPR     120 

/*  the next is used by QIC-3020 tapes with format code 6 (>2^16
 *  segments) It is specified in QIC-113, Rev. G, Section 5 (SCSI
 *  volume table). The difference is simply, that we only store the
 *  number of segments used, not the starting segment.
 */
#define VTBL_SCSI_SEGS  4 /* is a 4 byte value */

/*  one vtbl is 128 bytes, that results in a maximum number of
 *  29*1024/128 = 232 volumes.
 */
#define ZFT_MAX_VOLUMES (FT_SEGMENT_SIZE/VTBL_SIZE)
#define VTBL_ID  "VTBL"
#define VTBL_IDS { VTBL_ID, "XTBL", "UTID", "EXVT" } /* other valid ids */
#define ZFT_VOL_NAME "zftape volume" /* volume label used by me */
#define ZFTAPE_SIG "LINUX ZFT"

/*  global variables
 */
typedef struct zft_internal_vtbl
{
	struct list_head node;
	int          count;
	unsigned int start_seg;         /* 32 bits are enough for now */
	unsigned int end_seg;           /* 32 bits are enough for now */
	__s64        size;              /* uncompressed size */
        unsigned int blk_sz;            /* block size for this volume */
	unsigned int zft_volume     :1; /* zftape created this volume */
	unsigned int use_compression:1; /* compressed volume  */
	unsigned int qic113         :1; /* layout of compressed block
					 * info and vtbl conforms to
					 * QIC-113, Rev. G 
					 */
	unsigned int new_volume     :1; /* it was created by us, this
					 * run.  this allows the
					 * fields that aren't really
					 * used by zftape to be filled
					 * in by some user level
					 * program.
					 */
	unsigned int open           :1; /* just in progress of being 
					 * written
					 */
} zft_volinfo;

extern struct list_head zft_vtbl;
#define zft_head_vtbl  list_entry(zft_vtbl.next, zft_volinfo, node)
#define zft_eom_vtbl   list_entry(zft_vtbl.prev, zft_volinfo, node)
#define zft_last_vtbl  list_entry(zft_eom_vtbl->node.prev, zft_volinfo, node)
#define zft_first_vtbl list_entry(zft_head_vtbl->node.next, zft_volinfo, node)
#define zft_vtbl_empty (zft_eom_vtbl->node.prev == &zft_head_vtbl->node)

#define DUMP_VOLINFO(level, desc, info)					\
{									\
	char tmp[21];							\
	strlcpy(tmp, desc, sizeof(tmp));				\
	TRACE(level, "Volume %d:\n"					\
	      KERN_INFO "description  : %s\n"				\
	      KERN_INFO "first segment: %d\n"				\
	      KERN_INFO "last  segment: %d\n"				\
	      KERN_INFO "size         : " LL_X "\n"			\
	      KERN_INFO "block size   : %d\n"				\
	      KERN_INFO "compression  : %d\n"				\
	      KERN_INFO "zftape volume: %d\n"				\
	      KERN_INFO "QIC-113 conf.: %d",				\
	      (info)->count, tmp, (info)->start_seg, (info)->end_seg,	\
	      LL((info)->size), (info)->blk_sz,				\
	      (info)->use_compression != 0, (info)->zft_volume != 0,	\
	      (info)->qic113 != 0);					\
}

extern int zft_qic_mode;
extern int zft_old_ftape;
extern int zft_volume_table_changed;

/* exported functions */
extern void  zft_init_vtbl             (void);
extern void  zft_free_vtbl             (void);
extern int   zft_extract_volume_headers(__u8 *buffer);
extern int   zft_update_volume_table   (unsigned int segment);
extern int   zft_open_volume           (zft_position *pos,
					int blk_sz, int use_compression);
extern int   zft_close_volume          (zft_position *pos);
extern const zft_volinfo *zft_find_volume(unsigned int seg_pos);
extern int   zft_skip_volumes          (int count, zft_position *pos);
extern __s64 zft_get_eom_pos           (void);
extern void  zft_skip_to_eom           (zft_position *pos);
extern int   zft_fake_volume_headers   (eof_mark_union *eof_map, 
					int num_failed_sectors);
extern int   zft_weof                  (unsigned int count, zft_position *pos);
extern void  zft_move_past_eof         (zft_position *pos);

static inline int   zft_tape_at_eod         (const zft_position *pos);
static inline int   zft_tape_at_lbot        (const zft_position *pos);
static inline void  zft_position_before_eof (zft_position *pos, 
					     const zft_volinfo *volume);
static inline __s64 zft_check_for_eof(const zft_volinfo *vtbl,
				      const zft_position *pos);

/* this function decrements the zft_seg_pos counter if we are right
 * at the beginning of a segment. This is to handle fsfm/bsfm -- we
 * need to position before the eof mark.  NOTE: zft_tape_pos is not
 * changed 
 */
static inline void zft_position_before_eof(zft_position *pos, 
					   const zft_volinfo *volume)
{ 
	TRACE_FUN(ft_t_flow);

	if (pos->seg_pos == volume->end_seg + 1 &&  pos->seg_byte_pos == 0) {
		pos->seg_pos --;
		pos->seg_byte_pos = zft_get_seg_sz(pos->seg_pos);
	}
	TRACE_EXIT;
}

/*  Mmmh. Is the position at the end of the last volume, that is right
 *  before the last EOF mark also logical an EOD condition?
 */
static inline int zft_tape_at_eod(const zft_position *pos)
{ 
	TRACE_FUN(ft_t_any);

	if (zft_qic_mode) {
		TRACE_EXIT (pos->seg_pos >= zft_eom_vtbl->start_seg ||
			    zft_last_vtbl->open);
	} else {
		TRACE_EXIT pos->seg_pos > ft_last_data_segment;
	}
}

static inline int zft_tape_at_lbot(const zft_position *pos)
{
	if (zft_qic_mode) {
		return (pos->seg_pos <= zft_first_vtbl->start_seg &&
			pos->volume_pos == 0);
	} else {
		return (pos->seg_pos <= ft_first_data_segment && 
			pos->volume_pos == 0);
	}
}

/* This one checks for EOF.  return remaing space (may be negative) 
 */
static inline __s64 zft_check_for_eof(const zft_volinfo *vtbl,
				      const zft_position *pos)
{     
	return (__s64)(vtbl->size - pos->volume_pos);
}

#endif /* _ZFTAPE_VTBL_H */
