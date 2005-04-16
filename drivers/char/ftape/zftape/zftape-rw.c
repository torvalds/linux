/*
 *      Copyright (C) 1996, 1997 Claus-Justus Heine

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
 * $Source: /homes/cvs/ftape-stacked/ftape/zftape/zftape-rw.c,v $
 * $Revision: 1.2 $
 * $Date: 1997/10/05 19:19:08 $
 *
 *      This file contains some common code for the r/w code for
 *      zftape.
 */

#include <linux/config.h> /* for CONFIG_ZFT_DFLT_BLK_SZ */
#include <linux/errno.h>
#include <linux/mm.h>

#include <linux/zftape.h>
#include "../zftape/zftape-init.h"
#include "../zftape/zftape-eof.h"
#include "../zftape/zftape-ctl.h"
#include "../zftape/zftape-write.h"
#include "../zftape/zftape-read.h"
#include "../zftape/zftape-rw.h"
#include "../zftape/zftape-vtbl.h"

/*      Global vars.
 */

__u8 *zft_deblock_buf;
__u8 *zft_hseg_buf;
int zft_deblock_segment = -1;
zft_status_enum zft_io_state = zft_idle;
int zft_header_changed;
int zft_qic113; /* conform to old specs. and old zftape */
int zft_use_compression;
zft_position zft_pos = {
	-1, /* seg_pos */
	0,  /* seg_byte_pos */
	0,  /* tape_pos */
	0   /* volume_pos */
};
unsigned int zft_blk_sz = CONFIG_ZFT_DFLT_BLK_SZ;
__s64 zft_capacity;

unsigned int zft_written_segments;
int zft_label_changed;

/*      Local vars.
 */

unsigned int zft_get_seg_sz(unsigned int segment)
{
	int size;
	TRACE_FUN(ft_t_any);
	
	size = FT_SEGMENT_SIZE - 
		count_ones(ftape_get_bad_sector_entry(segment))*FT_SECTOR_SIZE;
	if (size > 0) {
		TRACE_EXIT (unsigned)size; 
	} else {
		TRACE_EXIT 0;
	}
}

/* ftape_set_flags(). Claus-Justus Heine, 1994/1995
 */
void zft_set_flags(unsigned minor_unit)
{     
	TRACE_FUN(ft_t_flow);
	
	zft_use_compression = zft_qic_mode = 0;
	switch (minor_unit & ZFT_MINOR_OP_MASK) {
	case (ZFT_Q80_MODE | ZFT_ZIP_MODE):
	case ZFT_ZIP_MODE:
		zft_use_compression = 1;
	case 0:
	case ZFT_Q80_MODE:
		zft_qic_mode = 1;
		if (zft_mt_compression) { /* override the default */
			zft_use_compression = 1;
		}
		break;
	case ZFT_RAW_MODE:
		TRACE(ft_t_noise, "switching to raw mode");
		break;
	default:
		TRACE(ft_t_warn, "Warning:\n"
		      KERN_INFO "Wrong combination of minor device bits.\n"
		      KERN_INFO "Switching to raw read-only mode.");
		zft_write_protected = 1;
		break;
	}
	TRACE_EXIT;
}

/* computes the segment and byte offset inside the segment
 * corresponding to tape_pos.
 *
 * tape_pos gives the offset in bytes from the beginning of the
 * ft_first_data_segment *seg_byte_pos is the offset in the current
 * segment in bytes
 *
 * Of, if this routine was called often one should cache the last data
 * pos it was called with, but actually this is only needed in
 * ftape_seek_block(), that is, almost never.
 */
int zft_calc_seg_byte_coord(int *seg_byte_pos, __s64 tape_pos)
{
	int segment;
	int seg_sz;
	TRACE_FUN(ft_t_flow);
	
	if (tape_pos == 0) {
		*seg_byte_pos = 0;
		segment = ft_first_data_segment;
	} else {
		seg_sz = 0;
		
		for (segment = ft_first_data_segment; 
		     ((tape_pos > 0) && (segment <= ft_last_data_segment));
		     segment++) {
			seg_sz = zft_get_seg_sz(segment); 
			tape_pos -= seg_sz;
		}
		if(tape_pos >= 0) {
			/* the case tape_pos > != 0 means that the
			 * argument tape_pos lies beyond the EOT.
			 */
			*seg_byte_pos= 0;
		} else { /* tape_pos < 0 */
			segment--;
			*seg_byte_pos= tape_pos + seg_sz;
		}
	}
	TRACE_EXIT(segment);
}

/* ftape_calc_tape_pos().
 *
 * computes the offset in bytes from the beginning of the
 * ft_first_data_segment inverse to ftape_calc_seg_byte_coord
 *
 * We should do some caching. But how:
 *
 * Each time the header segments are read in, this routine is called
 * with ft_tracks_per_tape*segments_per_track argumnet. So this should be
 * the time to reset the cache.
 *
 * Also, it might be in the future that the bad sector map gets
 * changed.  -> reset the cache
 */
static int seg_pos;
static __s64 tape_pos;

__s64 zft_get_capacity(void)
{
	seg_pos  = ft_first_data_segment;
	tape_pos = 0;

	while (seg_pos <= ft_last_data_segment) {
		tape_pos += zft_get_seg_sz(seg_pos ++);
	}
	return tape_pos;
}

__s64 zft_calc_tape_pos(int segment)
{
	int d1, d2, d3;
	TRACE_FUN(ft_t_any);
	
	if (segment > ft_last_data_segment) {
	        TRACE_EXIT zft_capacity;
	}
	if (segment < ft_first_data_segment) {
		TRACE_EXIT 0;
	}
	d2 = segment - seg_pos;
	if (-d2 > 10) {
		d1 = segment - ft_first_data_segment;
		if (-d2 > d1) {
			tape_pos = 0;
			seg_pos = ft_first_data_segment;
			d2 = d1;
		}
	}
	if (d2 > 10) {
		d3 = ft_last_data_segment - segment;
		if (d2 > d3) {
			tape_pos = zft_capacity;
			seg_pos  = ft_last_data_segment + 1;
			d2 = -d3;
		}
	}		
	if (d2 > 0) {
		while (seg_pos < segment) {
			tape_pos +=  zft_get_seg_sz(seg_pos++);
		}
	} else {
		while (seg_pos > segment) {
			tape_pos -=  zft_get_seg_sz(--seg_pos);
		}
	}
	TRACE(ft_t_noise, "new cached pos: %d", seg_pos);

	TRACE_EXIT tape_pos;
}

/* copy Z-label string to buffer, keeps track of the correct offset in
 * `buffer' 
 */
void zft_update_label(__u8 *buffer)
{ 
	TRACE_FUN(ft_t_flow);
	
	if (strncmp(&buffer[FT_LABEL], ZFTAPE_LABEL, 
		    sizeof(ZFTAPE_LABEL)-1) != 0) {
		TRACE(ft_t_info, "updating label from \"%s\" to \"%s\"",
		      &buffer[FT_LABEL], ZFTAPE_LABEL);
		strcpy(&buffer[FT_LABEL], ZFTAPE_LABEL);
		memset(&buffer[FT_LABEL] + sizeof(ZFTAPE_LABEL) - 1, ' ', 
		       FT_LABEL_SZ - sizeof(ZFTAPE_LABEL + 1));
		PUT4(buffer, FT_LABEL_DATE, 0);
		zft_label_changed = zft_header_changed = 1; /* changed */
	}
	TRACE_EXIT;
}

int zft_verify_write_segments(unsigned int segment, 
			      __u8 *data, size_t size,
			      __u8 *buffer)
{
	int result;
	__u8 *write_buf;
	__u8 *src_buf;
	int single;
	int seg_pos;
	int seg_sz;
	int remaining;
	ft_write_mode_t write_mode;
	TRACE_FUN(ft_t_flow);

	seg_pos   = segment;
	seg_sz    = zft_get_seg_sz(seg_pos);
	src_buf   = data;
	single    = size <= seg_sz;
	remaining = size;
	do {
		TRACE(ft_t_noise, "\n"
		      KERN_INFO "remaining: %d\n"
		      KERN_INFO "seg_sz   : %d\n"
		      KERN_INFO "segment  : %d",
		      remaining, seg_sz, seg_pos);
		if (remaining == seg_sz) {
			write_buf = src_buf;
			write_mode = single ? FT_WR_SINGLE : FT_WR_MULTI;
			remaining = 0;
		} else if (remaining > seg_sz) {
			write_buf = src_buf;
			write_mode = FT_WR_ASYNC; /* don't start tape */
			remaining -= seg_sz;
		} else { /* remaining < seg_sz */
			write_buf = buffer;
			memcpy(write_buf, src_buf, remaining);
			memset(&write_buf[remaining],'\0',seg_sz-remaining);
			write_mode = single ? FT_WR_SINGLE : FT_WR_MULTI;
			remaining = 0;
		}
		if ((result = ftape_write_segment(seg_pos, 
						  write_buf, 
						  write_mode)) != seg_sz) {
			TRACE(ft_t_err, "Error: "
			      "Couldn't write segment %d", seg_pos);
			TRACE_EXIT result < 0 ? result : -EIO; /* bail out */
		}
		zft_written_segments ++;
		seg_sz = zft_get_seg_sz(++seg_pos);
		src_buf += result;
	} while (remaining > 0);
	if (ftape_get_status()->fti_state == writing) {
		TRACE_CATCH(ftape_loop_until_writes_done(),);
		TRACE_CATCH(ftape_abort_operation(),);
		zft_prevent_flush();
	}
	seg_pos = segment;
	src_buf = data;
	remaining = size;
	do {
		TRACE_CATCH(result = ftape_read_segment(seg_pos, buffer, 
							single ? FT_RD_SINGLE
							: FT_RD_AHEAD),);
		if (memcmp(src_buf, buffer, 
			   remaining > result ? result : remaining) != 0) {
			TRACE_ABORT(-EIO, ft_t_err,
				    "Failed to verify written segment %d",
				    seg_pos);
		}
		remaining -= result;
		TRACE(ft_t_noise, "verify successful:\n"
		      KERN_INFO "segment  : %d\n"
		      KERN_INFO "segsize  : %d\n"
		      KERN_INFO "remaining: %d",
		      seg_pos, result, remaining);
		src_buf   += seg_sz;
		seg_pos++;
	} while (remaining > 0);
	TRACE_EXIT size;
}


/* zft_erase().  implemented compression-handling
 *
 * calculate the first data-segment when using/not using compression.
 *
 * update header-segment and compression-map-segment.
 */
int zft_erase(void)
{
	int result = 0;
	TRACE_FUN(ft_t_flow);
	
	if (!zft_header_read) {
		TRACE_CATCH(zft_vmalloc_once((void **)&zft_hseg_buf,
					     FT_SEGMENT_SIZE),);
		/* no need to read the vtbl and compression map */
		TRACE_CATCH(ftape_read_header_segment(zft_hseg_buf),);
		if ((zft_old_ftape = 
		     zft_ftape_validate_label(&zft_hseg_buf[FT_LABEL]))) {
			zft_ftape_extract_file_marks(zft_hseg_buf);
		}
		TRACE(ft_t_noise,
		      "ft_first_data_segment: %d, ft_last_data_segment: %d", 
		      ft_first_data_segment, ft_last_data_segment);
		zft_qic113 = (ft_format_code != fmt_normal &&
			      ft_format_code != fmt_1100ft &&
			      ft_format_code != fmt_425ft);
	}
	if (zft_old_ftape) {
		zft_clear_ftape_file_marks();
		zft_old_ftape = 0; /* no longer old ftape */
	}
	PUT2(zft_hseg_buf, FT_CMAP_START, 0);
	zft_volume_table_changed = 1;
	zft_capacity = zft_get_capacity();
	zft_init_vtbl();
	/* the rest must be done in ftape_update_header_segments 
	 */
	zft_header_read = 1;
	zft_header_changed = 1; /* force update of timestamp */
	result = zft_update_header_segments();

	ftape_abort_operation();

	zft_reset_position(&zft_pos);
	zft_set_flags (zft_unit);
	TRACE_EXIT result;
}

unsigned int zft_get_time(void) 
{
	unsigned int date = FT_TIME_STAMP(2097, 11, 30, 23, 59, 59); /* fun */
	return date;
}
