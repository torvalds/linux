/*
 *      Copyright (C) 1996, 1997 Claus Heine

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
 * $Source: /homes/cvs/ftape-stacked/ftape/zftape/zftape-write.c,v $
 * $Revision: 1.3 $
 * $Date: 1997/11/06 00:50:29 $
 *
 *      This file contains the writing code
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

/*      Local vars.
 */
static int last_write_failed;
static int need_flush;

void zft_prevent_flush(void)
{
	need_flush = 0;
}

static int zft_write_header_segments(__u8* buffer)
{
	int header_1_ok = 0;
	int header_2_ok = 0;
	unsigned int time_stamp;
	TRACE_FUN(ft_t_noise);
	
	TRACE_CATCH(ftape_abort_operation(),);
	ftape_seek_to_bot();    /* prevents extra rewind */
	if (GET4(buffer, 0) != FT_HSEG_MAGIC) {
		TRACE_ABORT(-EIO, ft_t_err,
			    "wrong header signature found, aborting");
	} 
	/*   Be optimistic: */
	PUT4(buffer, FT_SEG_CNT,
	     zft_written_segments + GET4(buffer, FT_SEG_CNT) + 2);
	if ((time_stamp = zft_get_time()) != 0) {
		PUT4(buffer, FT_WR_DATE, time_stamp);
		if (zft_label_changed) {
			PUT4(buffer, FT_LABEL_DATE, time_stamp);
		}
	}
	TRACE(ft_t_noise,
	      "writing first header segment %d", ft_header_segment_1);
	header_1_ok = zft_verify_write_segments(ft_header_segment_1, 
						buffer, FT_SEGMENT_SIZE,
						zft_deblock_buf) >= 0;
	TRACE(ft_t_noise,
	      "writing second header segment %d", ft_header_segment_2);
	header_2_ok = zft_verify_write_segments(ft_header_segment_2, 
						buffer, FT_SEGMENT_SIZE,
						zft_deblock_buf) >= 0;
	if (!header_1_ok) {
		TRACE(ft_t_warn, "Warning: "
		      "update of first header segment failed");
	}
	if (!header_2_ok) {
		TRACE(ft_t_warn, "Warning: "
		      "update of second header segment failed");
	}
	if (!header_1_ok && !header_2_ok) {
		TRACE_ABORT(-EIO, ft_t_err, "Error: "
		      "update of both header segments failed.");
	}
	TRACE_EXIT 0;
}

int zft_update_header_segments(void)
{
	TRACE_FUN(ft_t_noise);
	
	/*  must NOT use zft_write_protected, as it also includes the
	 *  file access mode. But we also want to update when soft
	 *  write protection is enabled (O_RDONLY)
	 */
	if (ft_write_protected || zft_old_ftape) {
		TRACE_ABORT(0, ft_t_noise, "Tape set read-only: no update");
	} 
	if (!zft_header_read) {
		TRACE_ABORT(0, ft_t_noise, "Nothing to update");
	}
	if (!zft_header_changed) {
		zft_header_changed = zft_written_segments > 0;
	}
	if (!zft_header_changed && !zft_volume_table_changed) {
		TRACE_ABORT(0, ft_t_noise, "Nothing to update");
	}
	TRACE(ft_t_noise, "Updating header segments");
	if (ftape_get_status()->fti_state == writing) {
		TRACE_CATCH(ftape_loop_until_writes_done(),);
	}
	TRACE_CATCH(ftape_abort_operation(),);
	
	zft_deblock_segment = -1; /* invalidate the cache */
	if (zft_header_changed) {
		TRACE_CATCH(zft_write_header_segments(zft_hseg_buf),);
	}
	if (zft_volume_table_changed) {
		TRACE_CATCH(zft_update_volume_table(ft_first_data_segment),);
	}
	zft_header_changed =
		zft_volume_table_changed = 
		zft_label_changed        =
		zft_written_segments     = 0;
	TRACE_CATCH(ftape_abort_operation(),);
	ftape_seek_to_bot();
	TRACE_EXIT 0;
}

static int read_merge_buffer(int seg_pos, __u8 *buffer, int offset, int seg_sz)
{
	int result = 0;
	const ft_trace_t old_tracing = TRACE_LEVEL;
	TRACE_FUN(ft_t_flow);
	
	if (zft_qic_mode) {
		/*  writing in the middle of a volume is NOT allowed
		 *
		 */
		TRACE(ft_t_noise, "No need to read a segment");
		memset(buffer + offset, 0, seg_sz - offset);
		TRACE_EXIT 0;
	}
	TRACE(ft_t_any, "waiting");
	ftape_start_writing(FT_WR_MULTI);
	TRACE_CATCH(ftape_loop_until_writes_done(),);
	
	TRACE(ft_t_noise, "trying to read segment %d from offset %d",
	      seg_pos, offset);
	SET_TRACE_LEVEL(ft_t_bug);
	result = zft_fetch_segment_fraction(seg_pos, buffer, 
					    FT_RD_SINGLE,
					    offset, seg_sz - offset);
	SET_TRACE_LEVEL(old_tracing);
	if (result != (seg_sz - offset)) {
		TRACE(ft_t_noise, "Ignore error: read_segment() result: %d",
		      result);
		memset(buffer + offset, 0, seg_sz - offset);
	}
	TRACE_EXIT 0;
}

/* flush the write buffer to tape and write an eof-marker at the
 * current position if not in raw mode.  This function always
 * positions the tape before the eof-marker.  _ftape_close() should
 * then advance to the next segment.
 *
 * the parameter "finish_volume" describes whether to position before
 * or after the possibly created file-mark. We always position after
 * the file-mark when called from ftape_close() and a flush was needed
 * (that is ftape_write() was the last tape operation before calling
 * ftape_flush) But we always position before the file-mark when this
 * function get's called from outside ftape_close() 
 */
int zft_flush_buffers(void)
{
	int result;
	int data_remaining;
	int this_segs_size;
	TRACE_FUN(ft_t_flow);

	TRACE(ft_t_data_flow,
	      "entered, ftape_state = %d", ftape_get_status()->fti_state);
	if (ftape_get_status()->fti_state != writing && !need_flush) {
		TRACE_ABORT(0, ft_t_noise, "no need for flush");
	}
	zft_io_state = zft_idle; /*  triggers some initializations for the
				  *  read and write routines 
				  */
	if (last_write_failed) {
		ftape_abort_operation();
		TRACE_EXIT -EIO;
	}
	TRACE(ft_t_noise, "flushing write buffers");
	this_segs_size = zft_get_seg_sz(zft_pos.seg_pos);
	if (this_segs_size == zft_pos.seg_byte_pos) {
		zft_pos.seg_pos ++;
		data_remaining = zft_pos.seg_byte_pos = 0;
	} else {
		data_remaining = zft_pos.seg_byte_pos;
	}
	/* If there is any data not written to tape yet, append zero's
	 * up to the end of the sector (if using compression) or merge
	 * it with the data existing on the tape Then write the
	 * segment(s) to tape.
	 */
	TRACE(ft_t_noise, "Position:\n"
	      KERN_INFO "seg_pos  : %d\n"
	      KERN_INFO "byte pos : %d\n"
	      KERN_INFO "remaining: %d",
	      zft_pos.seg_pos, zft_pos.seg_byte_pos, data_remaining);
	if (data_remaining > 0) {
		do {
			this_segs_size = zft_get_seg_sz(zft_pos.seg_pos);
			if (this_segs_size > data_remaining) {
				TRACE_CATCH(read_merge_buffer(zft_pos.seg_pos,
							      zft_deblock_buf,
							      data_remaining,
							      this_segs_size),
					    last_write_failed = 1);
			}
			result = ftape_write_segment(zft_pos.seg_pos, 
						     zft_deblock_buf,
						     FT_WR_MULTI);
			if (result != this_segs_size) {
				TRACE(ft_t_err, "flush buffers failed");
				zft_pos.tape_pos    -= zft_pos.seg_byte_pos;
				zft_pos.seg_byte_pos = 0;

				last_write_failed = 1;
				TRACE_EXIT result;
			}
			zft_written_segments ++;
			TRACE(ft_t_data_flow,
			      "flush, moved out buffer: %d", result);
			/* need next segment for more data (empty segments?)
			 */
			if (result < data_remaining) { 
				if (result > 0) {       
					/* move remainder to buffer beginning 
					 */
					memmove(zft_deblock_buf, 
						zft_deblock_buf + result,
						FT_SEGMENT_SIZE - result);
				}
			} 
			data_remaining -= result;
			zft_pos.seg_pos ++;
		} while (data_remaining > 0);
		TRACE(ft_t_any, "result: %d", result);
		zft_deblock_segment = --zft_pos.seg_pos;
		if (data_remaining == 0) {  /* first byte next segment */
			zft_pos.seg_byte_pos = this_segs_size;
		} else { /* after data previous segment, data_remaining < 0 */
			zft_pos.seg_byte_pos = data_remaining + result;
		}
	} else {
		TRACE(ft_t_noise, "zft_deblock_buf empty");
		zft_pos.seg_pos --;
		zft_pos.seg_byte_pos = zft_get_seg_sz (zft_pos.seg_pos);
		ftape_start_writing(FT_WR_MULTI);
	}
	TRACE(ft_t_any, "waiting");
	if ((result = ftape_loop_until_writes_done()) < 0) {
		/* that's really bad. What to to with zft_tape_pos? 
		 */
		TRACE(ft_t_err, "flush buffers failed");
	}
	TRACE(ft_t_any, "zft_seg_pos: %d, zft_seg_byte_pos: %d",
	      zft_pos.seg_pos, zft_pos.seg_byte_pos);
	last_write_failed  =
		need_flush = 0;
	TRACE_EXIT result;
}

/* return-value: the number of bytes removed from the user-buffer
 *
 * out: 
 *      int *write_cnt: how much actually has been moved to the
 *                      zft_deblock_buf
 *      int req_len  : MUST NOT BE CHANGED, except at EOT, in 
 *                      which case it may be adjusted
 * in : 
 *      char *buff        : the user buffer
 *      int buf_pos_write : copy of buf_len_wr int
 *      this_segs_size    : the size in bytes of the actual segment
 *                          char
 *      *zft_deblock_buf   : zft_deblock_buf
 */
static int zft_simple_write(int *cnt,
			    __u8 *dst_buf, const int seg_sz,
			    const __u8 __user *src_buf, const int req_len, 
			    const zft_position *pos,const zft_volinfo *volume)
{
	int space_left;
	TRACE_FUN(ft_t_flow);

	/* volume->size holds the tape capacity while volume is open */
	if (pos->tape_pos + volume->blk_sz > volume->size) {
		TRACE_EXIT -ENOSPC;
	}
	/*  remaining space in this segment, NOT zft_deblock_buf
	 */
	space_left = seg_sz - pos->seg_byte_pos;
	*cnt = req_len < space_left ? req_len : space_left;
	if (copy_from_user(dst_buf + pos->seg_byte_pos, src_buf, *cnt) != 0) {
		TRACE_EXIT -EFAULT;
	}
	TRACE_EXIT *cnt;
}

static int check_write_access(int req_len,
			      const zft_volinfo **volume,
			      zft_position *pos,
			      const unsigned int blk_sz)
{
	int result;
	TRACE_FUN(ft_t_flow);

	if ((req_len % zft_blk_sz) != 0) {
		TRACE_ABORT(-EINVAL, ft_t_info,
			    "write-count %d must be multiple of block-size %d",
			    req_len, blk_sz);
	}
	if (zft_io_state == zft_writing) {
		/*  all other error conditions have been checked earlier
		 */
		TRACE_EXIT 0;
	}
	zft_io_state = zft_idle;
	TRACE_CATCH(zft_check_write_access(pos),);
	/*  If we haven't read the header segment yet, do it now.
	 *  This will verify the configuration, get the bad sector
	 *  table and read the volume table segment 
	 */
	if (!zft_header_read) {
		TRACE_CATCH(zft_read_header_segments(),);
	}
	/*  fine. Now the tape is either at BOT or at EOD,
	 *  Write start of volume now
	 */
	TRACE_CATCH(zft_open_volume(pos, blk_sz, zft_use_compression),);
	*volume = zft_find_volume(pos->seg_pos);
	DUMP_VOLINFO(ft_t_noise, "", *volume);
	zft_just_before_eof = 0;
	/* now merge with old data if necessary */
	if (!zft_qic_mode && pos->seg_byte_pos != 0){
		result = zft_fetch_segment(pos->seg_pos,
					   zft_deblock_buf,
					   FT_RD_SINGLE);
		if (result < 0) {
			if (result == -EINTR || result == -ENOSPC) {
				TRACE_EXIT result;
			}
			TRACE(ft_t_noise, 
			      "ftape_read_segment() result: %d. "
			      "This might be normal when using "
			      "a newly\nformatted tape", result);
			memset(zft_deblock_buf, '\0', pos->seg_byte_pos);
		}
	}
	zft_io_state = zft_writing;
	TRACE_EXIT 0;
}

static int fill_deblock_buf(__u8 *dst_buf, const int seg_sz,
			    zft_position *pos, const zft_volinfo *volume,
			    const char __user *usr_buf, const int req_len)
{
	int cnt = 0;
	int result = 0;
	TRACE_FUN(ft_t_flow);

	if (seg_sz == 0) {
		TRACE_ABORT(0, ft_t_data_flow, "empty segment");
	}
	TRACE(ft_t_data_flow, "\n"
	      KERN_INFO "remaining req_len: %d\n"
	      KERN_INFO "          buf_pos: %d", 
	      req_len, pos->seg_byte_pos);
	/* zft_deblock_buf will not contain a valid segment any longer */
	zft_deblock_segment = -1;
	if (zft_use_compression) {
		TRACE_CATCH(zft_cmpr_lock(1 /* try to load */),);
		TRACE_CATCH(result= (*zft_cmpr_ops->write)(&cnt,
							   dst_buf, seg_sz,
							   usr_buf, req_len,
							   pos, volume),);
	} else {
		TRACE_CATCH(result= zft_simple_write(&cnt,
						     dst_buf, seg_sz,
						     usr_buf, req_len,
						     pos, volume),);
	}
	pos->volume_pos   += result;
	pos->seg_byte_pos += cnt;
	pos->tape_pos     += cnt;
	TRACE(ft_t_data_flow, "\n"
	      KERN_INFO "removed from user-buffer : %d bytes.\n"
	      KERN_INFO "copied to zft_deblock_buf: %d bytes.\n"
	      KERN_INFO "zft_tape_pos             : " LL_X " bytes.",
	      result, cnt, LL(pos->tape_pos));
	TRACE_EXIT result;
}


/*  called by the kernel-interface routine "zft_write()"
 */
int _zft_write(const char __user *buff, int req_len)
{
	int result = 0;
	int written = 0;
	int write_cnt;
	int seg_sz;
	static const zft_volinfo *volume = NULL;
	TRACE_FUN(ft_t_flow);
	
	zft_resid         = req_len;	
	last_write_failed = 1; /* reset to 0 when successful */
	/* check if write is allowed 
	 */
	TRACE_CATCH(check_write_access(req_len, &volume,&zft_pos,zft_blk_sz),);
	while (req_len > 0) {
		/* Allow us to escape from this loop with a signal !
		 */
		FT_SIGNAL_EXIT(_DONT_BLOCK);
		seg_sz = zft_get_seg_sz(zft_pos.seg_pos);
		if ((write_cnt = fill_deblock_buf(zft_deblock_buf,
						  seg_sz,
						  &zft_pos,
						  volume,
						  buff,
						  req_len)) < 0) {
			zft_resid -= written;
			if (write_cnt == -ENOSPC) {
				/* leave the remainder to flush_buffers()
				 */
				TRACE(ft_t_info, "No space left on device");
				last_write_failed = 0;
				if (!need_flush) {
					need_flush = written > 0;
				}
				TRACE_EXIT written > 0 ? written : -ENOSPC;
			} else {
				TRACE_EXIT result;
			}
		}
		if (zft_pos.seg_byte_pos == seg_sz) {
			TRACE_CATCH(ftape_write_segment(zft_pos.seg_pos, 
							zft_deblock_buf,
							FT_WR_ASYNC),
				    zft_resid -= written);
			zft_written_segments ++;
			zft_pos.seg_byte_pos =  0;
			zft_deblock_segment  = zft_pos.seg_pos;
			++zft_pos.seg_pos;
		}
		written += write_cnt;
		buff    += write_cnt;
		req_len -= write_cnt;
	} /* while (req_len > 0) */
	TRACE(ft_t_data_flow, "remaining in blocking buffer: %d",
	       zft_pos.seg_byte_pos);
	TRACE(ft_t_data_flow, "just written bytes: %d", written);
	last_write_failed = 0;
	zft_resid -= written;
	need_flush = need_flush || written > 0;
	TRACE_EXIT written;               /* bytes written */
}
