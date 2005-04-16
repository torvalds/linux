/*
 *      Copyright (c) 1995-1997 Claus-Justus Heine 

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
 * $Source: /homes/cvs/ftape-stacked/ftape/zftape/zftape-vtbl.c,v $
 * $Revision: 1.7.6.1 $
 * $Date: 1997/11/24 13:48:31 $
 *
 *      This file defines a volume table as defined in various QIC
 *      standards.
 * 
 *      This is a minimal implementation, just allowing ordinary DOS
 *      :( prgrams to identify the cartridge as used.
 */

#include <linux/errno.h>
#include <linux/mm.h>
#include <linux/slab.h>

#include <linux/zftape.h>
#include "../zftape/zftape-init.h"
#include "../zftape/zftape-eof.h"
#include "../zftape/zftape-ctl.h"
#include "../zftape/zftape-write.h"
#include "../zftape/zftape-read.h"
#include "../zftape/zftape-rw.h"
#include "../zftape/zftape-vtbl.h"

#define ZFT_CMAP_HACK /* leave this defined to hide the compression map */

/*
 *  global variables 
 */
int zft_qic_mode   = 1; /* use the vtbl */
int zft_old_ftape; /* prevents old ftaped tapes to be overwritten */
int zft_volume_table_changed; /* for write_header_segments() */

/*
 *  private variables (only exported for inline functions)
 */
LIST_HEAD(zft_vtbl);

/*  We could also allocate these dynamically when extracting the volume table
 *  sizeof(zft_volinfo) is about 32 or something close to that
 */
static zft_volinfo  tape_vtbl;
static zft_volinfo  eot_vtbl;
static zft_volinfo *cur_vtbl;

static inline void zft_new_vtbl_entry(void)
{
	struct list_head *tmp = &zft_last_vtbl->node;
	zft_volinfo *new = zft_kmalloc(sizeof(zft_volinfo));

	list_add(&new->node, tmp);
	new->count = zft_eom_vtbl->count ++;
}

void zft_free_vtbl(void)
{
	for (;;) {
		struct list_head *tmp = zft_vtbl.prev;
		zft_volinfo *vtbl;

		if (tmp == &zft_vtbl)
			break;
		list_del(tmp);
		vtbl = list_entry(tmp, zft_volinfo, node);
		zft_kfree(vtbl, sizeof(zft_volinfo));
	}
	INIT_LIST_HEAD(&zft_vtbl);
	cur_vtbl = NULL;
}

/*  initialize vtbl, called by ftape_new_cartridge()
 */
void zft_init_vtbl(void)
{ 
	zft_volinfo *new;

	zft_free_vtbl();
	
	/*  Create the two dummy vtbl entries
	 */
	new = zft_kmalloc(sizeof(zft_volinfo));
	list_add(&new->node, &zft_vtbl);
	new = zft_kmalloc(sizeof(zft_volinfo));
	list_add(&new->node, &zft_vtbl);
	zft_head_vtbl->end_seg   = ft_first_data_segment;
	zft_head_vtbl->blk_sz    = zft_blk_sz;
	zft_head_vtbl->count     = -1;
	zft_eom_vtbl->start_seg  = ft_first_data_segment + 1;
	zft_eom_vtbl->end_seg    = ft_last_data_segment + 1;
	zft_eom_vtbl->blk_sz     = zft_blk_sz;
	zft_eom_vtbl->count      = 0;

	/*  Reset the pointer for zft_find_volume()
	 */
	cur_vtbl = zft_eom_vtbl;

	/* initialize the dummy vtbl entries for zft_qic_mode == 0
	 */
	eot_vtbl.start_seg       = ft_last_data_segment + 1;
	eot_vtbl.end_seg         = ft_last_data_segment + 1;
	eot_vtbl.blk_sz          = zft_blk_sz;
	eot_vtbl.count           = -1;
	tape_vtbl.start_seg = ft_first_data_segment;
	tape_vtbl.end_seg   = ft_last_data_segment;
	tape_vtbl.blk_sz    = zft_blk_sz;
	tape_vtbl.size      = zft_capacity;
	tape_vtbl.count     = 0;
}

/* check for a valid VTBL signature. 
 */
static int vtbl_signature_valid(__u8 signature[4])
{
	const char *vtbl_ids[] = VTBL_IDS; /* valid signatures */
	int j;
	
	for (j = 0; 
	     (j < NR_ITEMS(vtbl_ids)) && (memcmp(signature, vtbl_ids[j], 4) != 0);
	     j++);
	return j < NR_ITEMS(vtbl_ids);
}

/* We used to store the block-size of the volume in the volume-label,
 * using the keyword "blocksize". The blocksize written to the
 * volume-label is in bytes.
 *
 * We use this now only for compatibility with old zftape version. We
 * store the blocksize directly as binary number in the vendor
 * extension part of the volume entry.
 */
static int check_volume_label(const char *label, int *blk_sz)
{ 
	int valid_format;
	char *blocksize;
	TRACE_FUN(ft_t_flow);
	
	TRACE(ft_t_noise, "called with \"%s\" / \"%s\"", label, ZFT_VOL_NAME);
	if (strncmp(label, ZFT_VOL_NAME, strlen(ZFT_VOL_NAME)) != 0) {
		*blk_sz = 1; /* smallest block size that we allow */
		valid_format = 0;
	} else {
		TRACE(ft_t_noise, "got old style zftape vtbl entry");
		/* get the default blocksize */
		/* use the kernel strstr()   */
		blocksize= strstr(label, " blocksize ");
		if (blocksize) {
			blocksize += strlen(" blocksize ");
			for(*blk_sz= 0; 
			    *blocksize >= '0' && *blocksize <= '9'; 
			    blocksize++) {
				*blk_sz *= 10;
				*blk_sz += *blocksize - '0';
			}
			if (*blk_sz > ZFT_MAX_BLK_SZ) {
				*blk_sz= 1;
				valid_format= 0;
			} else {
				valid_format = 1;
			}
		} else {
			*blk_sz= 1;
			valid_format= 0;
		}
	}
	TRACE_EXIT valid_format;
}

/*   check for a zftape volume
 */
static int check_volume(__u8 *entry, zft_volinfo *volume)
{
	TRACE_FUN(ft_t_flow);
	
	if(strncmp(&entry[VTBL_EXT+EXT_ZFTAPE_SIG], ZFTAPE_SIG,
		   strlen(ZFTAPE_SIG)) == 0) {
		TRACE(ft_t_noise, "got new style zftape vtbl entry");
		volume->blk_sz = GET2(entry, VTBL_EXT+EXT_ZFTAPE_BLKSZ);
		volume->qic113 = entry[VTBL_EXT+EXT_ZFTAPE_QIC113];
		TRACE_EXIT 1;
	} else {
		TRACE_EXIT check_volume_label(&entry[VTBL_DESC], &volume->blk_sz);
	}
}


/* create zftape specific vtbl entry, the volume bounds are inserted
 * in the calling function, zft_create_volume_headers()
 */
static void create_zft_volume(__u8 *entry, zft_volinfo *vtbl)
{
	TRACE_FUN(ft_t_flow);

	memset(entry, 0, VTBL_SIZE);
	memcpy(&entry[VTBL_SIG], VTBL_ID, 4);
	sprintf(&entry[VTBL_DESC], ZFT_VOL_NAME" %03d", vtbl->count);
	entry[VTBL_FLAGS] = (VTBL_FL_NOT_VERIFIED | VTBL_FL_SEG_SPANNING);
	entry[VTBL_M_NO] = 1; /* multi_cartridge_count */
	strcpy(&entry[VTBL_EXT+EXT_ZFTAPE_SIG], ZFTAPE_SIG);
	PUT2(entry, VTBL_EXT+EXT_ZFTAPE_BLKSZ, vtbl->blk_sz);
	if (zft_qic113) {
		PUT8(entry, VTBL_DATA_SIZE, vtbl->size);
		entry[VTBL_CMPR] = VTBL_CMPR_UNREG; 
		if (vtbl->use_compression) { /* use compression: */
			entry[VTBL_CMPR] |= VTBL_CMPR_USED;
		}
		entry[VTBL_EXT+EXT_ZFTAPE_QIC113] = 1;
	} else {
		PUT4(entry, VTBL_DATA_SIZE, vtbl->size);
		entry[VTBL_K_CMPR] = VTBL_CMPR_UNREG; 
		if (vtbl->use_compression) { /* use compression: */
			entry[VTBL_K_CMPR] |= VTBL_CMPR_USED;
		}
	}
	if (ft_format_code == fmt_big) {
		/* SCSI like vtbl, store the number of used
		 * segments as 4 byte value 
		 */
		PUT4(entry, VTBL_SCSI_SEGS, vtbl->end_seg-vtbl->start_seg + 1);
	} else {
		/* normal, QIC-80MC like vtbl 
		 */
		PUT2(entry, VTBL_START, vtbl->start_seg);
		PUT2(entry, VTBL_END, vtbl->end_seg);
	}
	TRACE_EXIT;
}

/* this one creates the volume headers for each volume. It is assumed
 * that buffer already contains the old volume-table, so that vtbl
 * entries without the zft_volume flag set can savely be ignored.
 */
static void zft_create_volume_headers(__u8 *buffer)
{   
	__u8 *entry;
	struct list_head *tmp;
	zft_volinfo *vtbl;
	TRACE_FUN(ft_t_flow);
	
#ifdef ZFT_CMAP_HACK
	if((strncmp(&buffer[VTBL_EXT+EXT_ZFTAPE_SIG], ZFTAPE_SIG,
		    strlen(ZFTAPE_SIG)) == 0) && 
	   buffer[VTBL_EXT+EXT_ZFTAPE_CMAP] != 0) {
		TRACE(ft_t_noise, "deleting cmap volume");
		memmove(buffer, buffer + VTBL_SIZE,
			FT_SEGMENT_SIZE - VTBL_SIZE);
	}
#endif
	entry = buffer;
	for (tmp = zft_head_vtbl->node.next;
	     tmp != &zft_eom_vtbl->node;
	     tmp = tmp->next) {
		vtbl = list_entry(tmp, zft_volinfo, node);
		/* we now fill in the values only for newly created volumes.
		 */
		if (vtbl->new_volume) {
			create_zft_volume(entry, vtbl);
			vtbl->new_volume = 0; /* clear the flag */
		}
		
		DUMP_VOLINFO(ft_t_noise, &entry[VTBL_DESC], vtbl);
		entry += VTBL_SIZE;
	}
	memset(entry, 0, FT_SEGMENT_SIZE - zft_eom_vtbl->count * VTBL_SIZE);
	TRACE_EXIT;
}

/*  write volume table to tape. Calls zft_create_volume_headers()
 */
int zft_update_volume_table(unsigned int segment)
{
	int result = 0;
	__u8 *verify_buf = NULL;
	TRACE_FUN(ft_t_flow);

	TRACE_CATCH(result = ftape_read_segment(ft_first_data_segment, 
						zft_deblock_buf,
						FT_RD_SINGLE),);
	zft_create_volume_headers(zft_deblock_buf);
	TRACE(ft_t_noise, "writing volume table segment %d", segment);
	if (zft_vmalloc_once(&verify_buf, FT_SEGMENT_SIZE) == 0) {
		TRACE_CATCH(zft_verify_write_segments(segment, 
						      zft_deblock_buf, result,
						      verify_buf),
			    zft_vfree(&verify_buf, FT_SEGMENT_SIZE));
		zft_vfree(&verify_buf, FT_SEGMENT_SIZE);
	} else {
		TRACE_CATCH(ftape_write_segment(segment, zft_deblock_buf, 
						FT_WR_SINGLE),);
	}
	TRACE_EXIT 0;
}

/* non zftape volumes are handled in raw mode. Thus we need to
 * calculate the raw amount of data contained in those segments.  
 */
static void extract_alien_volume(__u8 *entry, zft_volinfo *vtbl)
{
	TRACE_FUN(ft_t_flow);

	vtbl->size  = (zft_calc_tape_pos(zft_last_vtbl->end_seg+1) -
		       zft_calc_tape_pos(zft_last_vtbl->start_seg));
	vtbl->use_compression = 0;
	vtbl->qic113 = zft_qic113;
	if (vtbl->qic113) {
		TRACE(ft_t_noise, 
		      "Fake alien volume's size from " LL_X " to " LL_X, 
		      LL(GET8(entry, VTBL_DATA_SIZE)), LL(vtbl->size));
	} else {
		TRACE(ft_t_noise,
		      "Fake alien volume's size from %d to " LL_X, 
		      (int)GET4(entry, VTBL_DATA_SIZE), LL(vtbl->size));
	}
	TRACE_EXIT;
}


/* extract an zftape specific volume
 */
static void extract_zft_volume(__u8 *entry, zft_volinfo *vtbl)
{
	TRACE_FUN(ft_t_flow);

	if (vtbl->qic113) {
		vtbl->size = GET8(entry, VTBL_DATA_SIZE);
		vtbl->use_compression = 
			(entry[VTBL_CMPR] & VTBL_CMPR_USED) != 0; 
	} else {
		vtbl->size = GET4(entry, VTBL_DATA_SIZE);
		if (entry[VTBL_K_CMPR] & VTBL_CMPR_UNREG) {
			vtbl->use_compression = 
				(entry[VTBL_K_CMPR] & VTBL_CMPR_USED) != 0;
		} else if (entry[VTBL_CMPR] & VTBL_CMPR_UNREG) {
			vtbl->use_compression = 
				(entry[VTBL_CMPR] & VTBL_CMPR_USED) != 0; 
		} else {
			TRACE(ft_t_warn, "Geeh! There is something wrong:\n"
			      KERN_INFO "QIC compression (Rev = K): %x\n"
			      KERN_INFO "QIC compression (Rev > K): %x",
			      entry[VTBL_K_CMPR], entry[VTBL_CMPR]);
		}
	}
	TRACE_EXIT;
}

/* extract the volume table from buffer. "buffer" must already contain
 * the vtbl-segment 
 */
int zft_extract_volume_headers(__u8 *buffer)
{                            
        __u8 *entry;
	TRACE_FUN(ft_t_flow);
	
	zft_init_vtbl();
	entry = buffer;
#ifdef ZFT_CMAP_HACK
	if ((strncmp(&entry[VTBL_EXT+EXT_ZFTAPE_SIG], ZFTAPE_SIG,
		     strlen(ZFTAPE_SIG)) == 0) &&
	    entry[VTBL_EXT+EXT_ZFTAPE_CMAP] != 0) {
		TRACE(ft_t_noise, "ignoring cmap volume");
		entry += VTBL_SIZE;
	} 
#endif
	/* the end of the vtbl is indicated by an invalid signature 
	 */
	while (vtbl_signature_valid(&entry[VTBL_SIG]) &&
	       (entry - buffer) < FT_SEGMENT_SIZE) {
		zft_new_vtbl_entry();
		if (ft_format_code == fmt_big) {
			/* SCSI like vtbl, stores only the number of
			 * segments used 
			 */
			unsigned int num_segments= GET4(entry, VTBL_SCSI_SEGS);
			zft_last_vtbl->start_seg = zft_eom_vtbl->start_seg;
			zft_last_vtbl->end_seg = 
				zft_last_vtbl->start_seg + num_segments - 1;
		} else {
			/* `normal', QIC-80 like vtbl 
			 */
			zft_last_vtbl->start_seg = GET2(entry, VTBL_START);
			zft_last_vtbl->end_seg   = GET2(entry, VTBL_END);
		}
		zft_eom_vtbl->start_seg  = zft_last_vtbl->end_seg + 1;
		/* check if we created this volume and get the
		 * blk_sz 
		 */
		zft_last_vtbl->zft_volume = check_volume(entry, zft_last_vtbl);
		if (zft_last_vtbl->zft_volume == 0) {
			extract_alien_volume(entry, zft_last_vtbl);
		} else {
			extract_zft_volume(entry, zft_last_vtbl);
		}
		DUMP_VOLINFO(ft_t_noise, &entry[VTBL_DESC], zft_last_vtbl);
		entry +=VTBL_SIZE;
	}
#if 0
/*
 *  undefine to test end of tape handling
 */
	zft_new_vtbl_entry();
	zft_last_vtbl->start_seg = zft_eom_vtbl->start_seg;
	zft_last_vtbl->end_seg   = ft_last_data_segment - 10;
	zft_last_vtbl->blk_sz          = zft_blk_sz;
	zft_last_vtbl->zft_volume      = 1;
	zft_last_vtbl->qic113          = zft_qic113;
	zft_last_vtbl->size = (zft_calc_tape_pos(zft_last_vtbl->end_seg+1)
			       - zft_calc_tape_pos(zft_last_vtbl->start_seg));
#endif
	TRACE_EXIT 0;
}

/* this functions translates the failed_sector_log, misused as
 * EOF-marker list, into a virtual volume table. The table mustn't be
 * written to tape, because this would occupy the first data segment,
 * which should be the volume table, but is actually the first segment
 * that is filled with data (when using standard ftape).  We assume,
 * that we get a non-empty failed_sector_log.
 */
int zft_fake_volume_headers (eof_mark_union *eof_map, int num_failed_sectors)
{
	unsigned int segment, sector;
	int have_eom = 0;
	int vol_no;
	TRACE_FUN(ft_t_flow);

	if ((num_failed_sectors >= 2) &&
	    (GET2(&eof_map[num_failed_sectors - 1].mark.segment, 0) 
	     == 
	     GET2(&eof_map[num_failed_sectors - 2].mark.segment, 0) + 1) &&
	    (GET2(&eof_map[num_failed_sectors - 1].mark.date, 0) == 1)) {
		/* this should be eom. We keep the remainder of the
		 * tape as another volume.
		 */
		have_eom = 1;
	}
	zft_init_vtbl();
	zft_eom_vtbl->start_seg = ft_first_data_segment;
	for(vol_no = 0; vol_no < num_failed_sectors - have_eom; vol_no ++) {
		zft_new_vtbl_entry();

		segment = GET2(&eof_map[vol_no].mark.segment, 0);
		sector  = GET2(&eof_map[vol_no].mark.date, 0);

		zft_last_vtbl->start_seg  = zft_eom_vtbl->start_seg;
		zft_last_vtbl->end_seg    = segment;
		zft_eom_vtbl->start_seg  = segment + 1;
		zft_last_vtbl->blk_sz     = 1;
		zft_last_vtbl->size       = 
			(zft_calc_tape_pos(zft_last_vtbl->end_seg)
			 - zft_calc_tape_pos(zft_last_vtbl->start_seg)
			 + (sector-1) * FT_SECTOR_SIZE);
		TRACE(ft_t_noise, 
		      "failed sector log: segment: %d, sector: %d", 
		      segment, sector);
		DUMP_VOLINFO(ft_t_noise, "Faked volume", zft_last_vtbl);
	}
	if (!have_eom) {
		zft_new_vtbl_entry();
		zft_last_vtbl->start_seg = zft_eom_vtbl->start_seg;
		zft_last_vtbl->end_seg   = ft_last_data_segment;
		zft_eom_vtbl->start_seg  = ft_last_data_segment + 1;
		zft_last_vtbl->size      = zft_capacity;
		zft_last_vtbl->size     -= zft_calc_tape_pos(zft_last_vtbl->start_seg);
		zft_last_vtbl->blk_sz    = 1;
		DUMP_VOLINFO(ft_t_noise, "Faked volume",zft_last_vtbl);
	}
	TRACE_EXIT 0;
}

/* update the internal volume table
 *
 * if before start of last volume: erase all following volumes if
 * inside a volume: set end of volume to infinity
 *
 * this function is intended to be called every time _ftape_write() is
 * called
 *
 * return: 0 if no new volume was created, 1 if a new volume was
 * created
 *
 * NOTE: we don't need to check for zft_mode as ftape_write() does
 * that already. This function gets never called without accessing
 * zftape via the *qft* devices 
 */

int zft_open_volume(zft_position *pos, int blk_sz, int use_compression)
{ 
	TRACE_FUN(ft_t_flow);
	
	if (!zft_qic_mode) {
		TRACE_EXIT 0;
	}
	if (zft_tape_at_lbot(pos)) {
		zft_init_vtbl();
		if(zft_old_ftape) {
			/* clear old ftape's eof marks */
			zft_clear_ftape_file_marks();
			zft_old_ftape = 0; /* no longer old ftape */
		}
		zft_reset_position(pos);
	}
	if (pos->seg_pos != zft_last_vtbl->end_seg + 1) {
		TRACE_ABORT(-EIO, ft_t_bug, 
		      "BUG: seg_pos: %d, zft_last_vtbl->end_seg: %d", 
		      pos->seg_pos, zft_last_vtbl->end_seg);
	}                            
	TRACE(ft_t_noise, "create new volume");
	if (zft_eom_vtbl->count >= ZFT_MAX_VOLUMES) {
		TRACE_ABORT(-ENOSPC, ft_t_err,
			    "Error: maxmimal number of volumes exhausted "
			    "(maxmimum is %d)", ZFT_MAX_VOLUMES);
	}
	zft_new_vtbl_entry();
	pos->volume_pos = pos->seg_byte_pos = 0;
	zft_last_vtbl->start_seg       = pos->seg_pos;
	zft_last_vtbl->end_seg         = ft_last_data_segment; /* infinity */
	zft_last_vtbl->blk_sz          = blk_sz;
	zft_last_vtbl->size            = zft_capacity;
	zft_last_vtbl->zft_volume      = 1;
	zft_last_vtbl->use_compression = use_compression;
	zft_last_vtbl->qic113          = zft_qic113;
	zft_last_vtbl->new_volume      = 1;
	zft_last_vtbl->open            = 1;
	zft_volume_table_changed = 1;
	zft_eom_vtbl->start_seg  = ft_last_data_segment + 1;
	TRACE_EXIT 0;
}

/*  perform mtfsf, mtbsf, not allowed without zft_qic_mode
 */
int zft_skip_volumes(int count, zft_position *pos)
{ 
	const zft_volinfo *vtbl;
	TRACE_FUN(ft_t_flow);
	
	TRACE(ft_t_noise, "count: %d", count);
	
	vtbl= zft_find_volume(pos->seg_pos);
	while (count > 0 && vtbl != zft_eom_vtbl) {
		vtbl = list_entry(vtbl->node.next, zft_volinfo, node);
		count --;
	}
	while (count < 0 && vtbl != zft_first_vtbl) {
		vtbl = list_entry(vtbl->node.prev, zft_volinfo, node);
		count ++;
	}
	pos->seg_pos        = vtbl->start_seg;
	pos->seg_byte_pos   = 0;
	pos->volume_pos     = 0;
	pos->tape_pos       = zft_calc_tape_pos(pos->seg_pos);
	zft_just_before_eof = vtbl->size == 0;
	if (zft_cmpr_ops) {
		(*zft_cmpr_ops->reset)();
	}
	zft_deblock_segment = -1; /* no need to keep cache */
	TRACE(ft_t_noise, "repositioning to:\n"
	      KERN_INFO "zft_seg_pos        : %d\n"
	      KERN_INFO "zft_seg_byte_pos   : %d\n"
	      KERN_INFO "zft_tape_pos       : " LL_X "\n"
	      KERN_INFO "zft_volume_pos     : " LL_X "\n"
	      KERN_INFO "file number        : %d",
	      pos->seg_pos, pos->seg_byte_pos, 
	      LL(pos->tape_pos), LL(pos->volume_pos), vtbl->count);
	zft_resid = count < 0 ? -count : count;
	TRACE_EXIT zft_resid ? -EINVAL : 0;
}

/* the following simply returns the raw data position of the EOM
 * marker, MTIOCSIZE ioctl 
 */
__s64 zft_get_eom_pos(void)
{
	if (zft_qic_mode) {
		return zft_calc_tape_pos(zft_eom_vtbl->start_seg);
	} else {
		/* there is only one volume in raw mode */
		return zft_capacity;
	}
}

/* skip to eom, used for MTEOM
 */
void zft_skip_to_eom(zft_position *pos)
{
	TRACE_FUN(ft_t_flow);
	pos->seg_pos      = zft_eom_vtbl->start_seg;
	pos->seg_byte_pos = 
		pos->volume_pos     = 
		zft_just_before_eof = 0;
	pos->tape_pos = zft_calc_tape_pos(pos->seg_pos);
	TRACE(ft_t_noise, "ftape positioned to segment %d, data pos " LL_X, 
	      pos->seg_pos, LL(pos->tape_pos));
	TRACE_EXIT;
}

/*  write an EOF-marker by setting zft_last_vtbl->end_seg to seg_pos.
 *  NOTE: this function assumes that zft_last_vtbl points to a valid
 *  vtbl entry
 *
 *  NOTE: this routine always positions before the EOF marker
 */
int zft_close_volume(zft_position *pos)
{
	TRACE_FUN(ft_t_any);

	if (zft_vtbl_empty || !zft_last_vtbl->open) { /* should not happen */
		TRACE(ft_t_noise, "There are no volumes to finish");
		TRACE_EXIT -EIO;
	}
	if (pos->seg_byte_pos == 0 && 
	    pos->seg_pos != zft_last_vtbl->start_seg) {
		pos->seg_pos --;
		pos->seg_byte_pos      = zft_get_seg_sz(pos->seg_pos);
	}
	zft_last_vtbl->end_seg   = pos->seg_pos;
	zft_last_vtbl->size      = pos->volume_pos;
	zft_volume_table_changed = 1;
	zft_just_before_eof      = 1;
	zft_eom_vtbl->start_seg  = zft_last_vtbl->end_seg + 1;
	zft_last_vtbl->open      = 0; /* closed */
	TRACE_EXIT 0;
}

/* write count file-marks at current position. 
 *
 *  The tape is positioned after the eof-marker, that is at byte 0 of
 *  the segment following the eof-marker
 *
 *  this function is only allowed in zft_qic_mode
 *
 *  Only allowed when tape is at BOT or EOD.
 */
int zft_weof(unsigned int count, zft_position *pos)
{
	
	TRACE_FUN(ft_t_flow);

	if (!count) { /* write zero EOF marks should be a real no-op */
		TRACE_EXIT 0;
	}
	zft_volume_table_changed = 1;
	if (zft_tape_at_lbot(pos)) {
		zft_init_vtbl();
		if(zft_old_ftape) {
			/* clear old ftape's eof marks */
			zft_clear_ftape_file_marks();
			zft_old_ftape = 0;    /* no longer old ftape */
		}
	}
	if (zft_last_vtbl->open) {
		zft_close_volume(pos);
		zft_move_past_eof(pos);
		count --;
	}
	/* now it's easy, just append eof-marks, that is empty
	 * volumes, to the end of the already recorded media.
	 */
	while (count > 0 && 
	       pos->seg_pos <= ft_last_data_segment && 
	       zft_eom_vtbl->count < ZFT_MAX_VOLUMES) {
		TRACE(ft_t_noise,
		      "Writing zero sized file at segment %d", pos->seg_pos);
		zft_new_vtbl_entry();
		zft_last_vtbl->start_seg       = pos->seg_pos;
		zft_last_vtbl->end_seg         = pos->seg_pos;
		zft_last_vtbl->size            = 0;
		zft_last_vtbl->blk_sz          = zft_blk_sz;
		zft_last_vtbl->zft_volume      = 1;
		zft_last_vtbl->use_compression = 0;
		pos->tape_pos += zft_get_seg_sz(pos->seg_pos);
		zft_eom_vtbl->start_seg = ++ pos->seg_pos;
		count --;
	} 
	if (count > 0) {
		/*  there are two possibilities: end of tape, or the
		 *  maximum number of files is exhausted.
		 */
		zft_resid = count;
		TRACE(ft_t_noise,"Number of marks NOT written: %d", zft_resid);
		if (zft_eom_vtbl->count == ZFT_MAX_VOLUMES) {
			TRACE_ABORT(-EINVAL, ft_t_warn,
				    "maximum allowed number of files "
				    "exhausted: %d", ZFT_MAX_VOLUMES);
		} else {
			TRACE_ABORT(-ENOSPC,
				    ft_t_noise, "reached end of tape");
		}
	}
	TRACE_EXIT 0;
}

const zft_volinfo *zft_find_volume(unsigned int seg_pos)
{
	TRACE_FUN(ft_t_flow);
	
	TRACE(ft_t_any, "called with seg_pos %d",seg_pos);
	if (!zft_qic_mode) {
		if (seg_pos > ft_last_data_segment) {
			TRACE_EXIT &eot_vtbl;
		}
		tape_vtbl.blk_sz =  zft_blk_sz;
		TRACE_EXIT &tape_vtbl;
	}
	if (seg_pos < zft_first_vtbl->start_seg) {
		TRACE_EXIT (cur_vtbl = zft_first_vtbl);
	}
	while (seg_pos > cur_vtbl->end_seg) {
		cur_vtbl = list_entry(cur_vtbl->node.next, zft_volinfo, node);
		TRACE(ft_t_noise, "%d - %d", cur_vtbl->start_seg, cur_vtbl->end_seg);
	}
	while (seg_pos < cur_vtbl->start_seg) {
		cur_vtbl = list_entry(cur_vtbl->node.prev, zft_volinfo, node);
		TRACE(ft_t_noise, "%d - %d", cur_vtbl->start_seg, cur_vtbl->end_seg);
	}
	if (seg_pos > cur_vtbl->end_seg || seg_pos < cur_vtbl->start_seg) {
		TRACE(ft_t_bug, "This cannot happen");
	}
	DUMP_VOLINFO(ft_t_noise, "", cur_vtbl);
	TRACE_EXIT cur_vtbl;
}

/* this function really assumes that we are just before eof
 */
void zft_move_past_eof(zft_position *pos)
{ 
	TRACE_FUN(ft_t_flow);

	TRACE(ft_t_noise, "old seg. pos: %d", pos->seg_pos);
	pos->tape_pos += zft_get_seg_sz(pos->seg_pos++) - pos->seg_byte_pos;
	pos->seg_byte_pos = 0;
	pos->volume_pos   = 0;
	if (zft_cmpr_ops) {
		(*zft_cmpr_ops->reset)();
	}
	zft_just_before_eof =  0;
	zft_deblock_segment = -1; /* no need to cache it anymore */
	TRACE(ft_t_noise, "new seg. pos: %d", pos->seg_pos);
	TRACE_EXIT;
}
