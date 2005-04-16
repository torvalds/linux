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
 * $Source: /homes/cvs/ftape-stacked/ftape/zftape/zftape-read.c,v $
 * $Revision: 1.2 $
 * $Date: 1997/10/05 19:19:06 $
 *
 *      This file contains the high level reading code
 *      for the QIC-117 floppy-tape driver for Linux.
 */

#include <linux/errno.h>
#include <linux/mm.h>

#include <linux/zftape.h>

#include <asm/uaccess.h>

#include "../zftape/zftape-init.h"
#include "../zftape/zftape-eof.h"
#include "../zftape/zftape-ctl.h"
#include "../zftape/zftape-write.h"
#include "../zftape/zftape-read.h"
#include "../zftape/zftape-rw.h"
#include "../zftape/zftape-vtbl.h"

/*      Global vars.
 */
int zft_just_before_eof;

/*      Local vars.
 */
static int buf_len_rd;

void zft_zap_read_buffers(void)
{
	buf_len_rd = 0;
}

int zft_read_header_segments(void)      
{
	TRACE_FUN(ft_t_flow);

	zft_header_read = 0;
	TRACE_CATCH(zft_vmalloc_once(&zft_hseg_buf, FT_SEGMENT_SIZE),);
	TRACE_CATCH(ftape_read_header_segment(zft_hseg_buf),);
	TRACE(ft_t_info, "Segments written since first format: %d",
	      (int)GET4(zft_hseg_buf, FT_SEG_CNT));
	zft_qic113 = (ft_format_code != fmt_normal &&
		      ft_format_code != fmt_1100ft &&
		      ft_format_code != fmt_425ft);
	TRACE(ft_t_info, "ft_first_data_segment: %d, ft_last_data_segment: %d", 
	      ft_first_data_segment, ft_last_data_segment);
	zft_capacity = zft_get_capacity();
	zft_old_ftape = zft_ftape_validate_label(&zft_hseg_buf[FT_LABEL]);
	if (zft_old_ftape) {
		TRACE(ft_t_info, 
"Found old ftaped tape, emulating eof marks, entering read-only mode");
		zft_ftape_extract_file_marks(zft_hseg_buf);
		TRACE_CATCH(zft_fake_volume_headers(zft_eof_map, 
						    zft_nr_eof_marks),);
	} else {
		/* the specs say that the volume table must be
		 * initialized with zeroes during formatting, so it
		 * MUST be readable, i.e. contain vaid ECC
		 * information.  
		 */
		TRACE_CATCH(ftape_read_segment(ft_first_data_segment, 
					       zft_deblock_buf, 
					       FT_RD_SINGLE),);
		TRACE_CATCH(zft_extract_volume_headers(zft_deblock_buf),);
	}
	zft_header_read = 1;
	zft_set_flags(zft_unit);
	zft_reset_position(&zft_pos);
	TRACE_EXIT 0;
}

int zft_fetch_segment_fraction(const unsigned int segment, void *buffer,
			       const ft_read_mode_t read_mode,
			       const unsigned int start,
			       const unsigned int size)
{
	int seg_sz;
	TRACE_FUN(ft_t_flow);

	if (segment == zft_deblock_segment) {
		TRACE(ft_t_data_flow,
		      "re-using segment %d already in deblock buffer",
		      segment);
		seg_sz = zft_get_seg_sz(segment);
		if (start > seg_sz) {
			TRACE_ABORT(-EINVAL, ft_t_bug,
				    "trying to read beyond end of segment:\n"
				    KERN_INFO "seg_sz : %d\n"
				    KERN_INFO "start  : %d\n"
				    KERN_INFO "segment: %d",
				    seg_sz, start, segment);
		}
		if ((start + size) > seg_sz) {
			TRACE_EXIT seg_sz - start;
		}
		TRACE_EXIT size;
	}
	seg_sz = ftape_read_segment_fraction(segment, buffer, read_mode,
					     start, size);
	TRACE(ft_t_data_flow, "segment %d, result %d", segment, seg_sz);
	if ((int)seg_sz >= 0 && start == 0 && size == FT_SEGMENT_SIZE) {
		/*  this implicitly assumes that we are always called with
		 *  buffer == zft_deblock_buf 
		 */
		zft_deblock_segment = segment;
	} else {
		zft_deblock_segment = -1;
	}
	TRACE_EXIT seg_sz;
}

/*
 * out:
 *
 * int *read_cnt: the number of bytes we removed from the
 *                zft_deblock_buf (result)
 *
 * int *to_do   : the remaining size of the read-request. Is changed.
 *
 * in:
 *
 * char *buff      : buff is the address of the upper part of the user
 *                   buffer, that hasn't been filled with data yet.
 * int buf_pos_read: copy of buf_pos_rd
 * int buf_len_read: copy of buf_len_rd
 * char *zft_deblock_buf: ftape_zft_deblock_buf
 *
 * returns the amount of data actually copied to the user-buffer
 *
 * to_do MUST NOT SHRINK except to indicate an EOT. In this case to_do
 * has to be set to 0. We cannot return -ENOSPC, because we return the
 * amount of data actually * copied to the user-buffer
 */
static int zft_simple_read (int *read_cnt, 
			    __u8  __user *dst_buf, 
			    const int to_do, 
			    const __u8 *src_buf, 
			    const int seg_sz, 
			    const zft_position *pos,
			    const zft_volinfo *volume)
{
	TRACE_FUN(ft_t_flow);

	if (seg_sz - pos->seg_byte_pos < to_do) {
		*read_cnt = seg_sz - pos->seg_byte_pos;
	} else {
		*read_cnt = to_do;
	}
	if (copy_to_user(dst_buf, 
			 src_buf + pos->seg_byte_pos, *read_cnt) != 0) {
		TRACE_EXIT -EFAULT;
	}
	TRACE(ft_t_noise, "nr bytes just read: %d", *read_cnt);
	TRACE_EXIT *read_cnt;
}

/* req_len: gets clipped due to EOT of EOF.
 * req_clipped: is a flag indicating whether req_len was clipped or not
 * volume: contains information on current volume (blk_sz etc.)
 */
static int check_read_access(int *req_len, 
			     const zft_volinfo **volume,
			     int *req_clipped, 
			     const zft_position *pos)
{
	static __s64 remaining;
	static int eod;
	TRACE_FUN(ft_t_flow);
	
	if (zft_io_state != zft_reading) {
		if (zft_offline) { /* offline includes no_tape */
			TRACE_ABORT(-ENXIO, ft_t_warn,
				    "tape is offline or no cartridge");
		}
		if (!ft_formatted) {
			TRACE_ABORT(-EACCES,
				    ft_t_warn, "tape is not formatted");
		}
		/*  now enter defined state, read header segment if not
		 *  already done and flush write buffers
		 */
		TRACE_CATCH(zft_def_idle_state(),);
		zft_io_state = zft_reading;
		if (zft_tape_at_eod(pos)) {
			eod = 1;
			TRACE_EXIT 1;
		}
		eod = 0;
		*volume = zft_find_volume(pos->seg_pos);
		/* get the space left until EOF */
		remaining = zft_check_for_eof(*volume, pos);
		buf_len_rd = 0;
		TRACE(ft_t_noise, "remaining: " LL_X ", vol_no: %d",
		      LL(remaining), (*volume)->count);
	} else if (zft_tape_at_eod(pos)) {
		if (++eod > 2) {
			TRACE_EXIT -EIO; /* st.c also returns -EIO */
		} else {
			TRACE_EXIT 1;
		}
	}
	if ((*req_len % (*volume)->blk_sz) != 0) {
		/*  this message is informational only. The user gets the
		 *  proper return value
		 */
		TRACE_ABORT(-EINVAL, ft_t_info,
			    "req_len %d not a multiple of block size %d",
			    *req_len, (*volume)->blk_sz);
	}
	/* As GNU tar doesn't accept partial read counts when the
	 * multiple volume flag is set, we make sure to return the
	 * requested amount of data. Except, of course, at the end of
	 * the tape or file mark.  
	 */
	remaining -= *req_len;
	if (remaining <= 0) {
		TRACE(ft_t_noise, 
		      "clipped request from %d to %d.", 
		      *req_len, (int)(*req_len + remaining));
		*req_len += remaining;
		*req_clipped = 1;
	} else {
		*req_clipped = 0;
	}
	TRACE_EXIT 0;
}

/* this_segs_size: the current segment's size.
 * buff: the USER-SPACE buffer provided by the calling function.
 * req_len: how much data should be read at most.
 * volume: contains information on current volume (blk_sz etc.)
 */  
static int empty_deblock_buf(__u8 __user *usr_buf, const int req_len,
			     const __u8 *src_buf, const int seg_sz,
			     zft_position *pos,
			     const zft_volinfo *volume)
{
	int cnt;
	int result = 0;
	TRACE_FUN(ft_t_flow);

	TRACE(ft_t_data_flow, "this_segs_size: %d", seg_sz);
	if (zft_use_compression && volume->use_compression) {
		TRACE_CATCH(zft_cmpr_lock(1 /* try to load */),);
		TRACE_CATCH(result= (*zft_cmpr_ops->read)(&cnt,
							  usr_buf, req_len,
							  src_buf, seg_sz,
							  pos, volume),);
	} else {                                  
		TRACE_CATCH(result= zft_simple_read (&cnt,
						     usr_buf, req_len,
						     src_buf, seg_sz,
						     pos, volume),);
	}
	pos->volume_pos   += result;
        pos->tape_pos     += cnt;
	pos->seg_byte_pos += cnt;
	buf_len_rd        -= cnt; /* remaining bytes in buffer */
	TRACE(ft_t_data_flow, "buf_len_rd: %d, cnt: %d", buf_len_rd, cnt);
	if(pos->seg_byte_pos >= seg_sz) {
		pos->seg_pos++;
		pos->seg_byte_pos = 0;
	}
	TRACE(ft_t_data_flow, "bytes moved out of deblock-buffer: %d", cnt);
	TRACE_EXIT result;
}


/* note: we store the segment id of the segment that is inside the
 * deblock buffer. This spares a lot of ftape_read_segment()s when we
 * use small block-sizes. The block-size may be 1kb (SECTOR_SIZE). In
 * this case a MTFSR 28 maybe still inside the same segment.
 */
int _zft_read(char __user *buff, int req_len)
{
	int req_clipped;
	int result     = 0;
	int bytes_read = 0;
	static unsigned int seg_sz = 0;
	static const zft_volinfo *volume = NULL;
	TRACE_FUN(ft_t_flow);
	
	zft_resid = req_len;
	result = check_read_access(&req_len, &volume,
				   &req_clipped, &zft_pos);
	switch(result) {
	case 0: 
		break; /* nothing special */
	case 1: 
		TRACE(ft_t_noise, "EOD reached");
		TRACE_EXIT 0;   /* EOD */
	default:
		TRACE_ABORT(result, ft_t_noise,
			    "check_read_access() failed with result %d",
			    result);
		TRACE_EXIT result;
	}
	while (req_len > 0) { 
		/*  Allow escape from this loop on signal !
		 */
		FT_SIGNAL_EXIT(_DONT_BLOCK);
		/* buf_len_rd == 0 means that we need to read a new
		 * segment.
		 */
		if (buf_len_rd == 0) {
			while((result = zft_fetch_segment(zft_pos.seg_pos,
							  zft_deblock_buf,
							  FT_RD_AHEAD)) == 0) {
				zft_pos.seg_pos ++;
				zft_pos.seg_byte_pos = 0;
			}
			if (result < 0) {
				zft_resid -= bytes_read;
				TRACE_ABORT(result, ft_t_noise,
					    "zft_fetch_segment(): %d",
					    result);
			}
			seg_sz = result;
			buf_len_rd = seg_sz - zft_pos.seg_byte_pos;
		}
		TRACE_CATCH(result = empty_deblock_buf(buff, 
						       req_len,
						       zft_deblock_buf, 
						       seg_sz, 
						       &zft_pos,
						       volume),
			    zft_resid -= bytes_read);
		TRACE(ft_t_data_flow, "bytes just read: %d", result);
		bytes_read += result; /* what we got so far       */
		buff       += result; /* index in user-buffer     */
		req_len    -= result; /* what's left from req_len */
	} /* while (req_len  > 0) */
	if (req_clipped) {
		TRACE(ft_t_data_flow,
		      "maybe partial count because of eof mark");
		if (zft_just_before_eof && bytes_read == 0) {
			/* req_len was > 0, but user didn't get
			 * anything the user has read in the eof-mark 
			 */
			zft_move_past_eof(&zft_pos);
			ftape_abort_operation();
		} else {
			/* don't skip to the next file before the user
			 * tried to read a second time past EOF Just
			 * mark that we are at EOF and maybe decrement
			 * zft_seg_pos to stay in the same volume;
			 */
			zft_just_before_eof = 1;
			zft_position_before_eof(&zft_pos, volume);
			TRACE(ft_t_noise, "just before eof");
		}
	}
	zft_resid -= result; /* for MTSTATUS       */
	TRACE_EXIT bytes_read;
}
