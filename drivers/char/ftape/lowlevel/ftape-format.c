/*
 * Copyright (C) 1997 Claus-Justus Heine.

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
 * $Source: /homes/cvs/ftape-stacked/ftape/lowlevel/ftape-format.c,v $
 * $Revision: 1.2.4.1 $
 * $Date: 1997/11/14 16:05:39 $
 *
 *      This file contains the code to support formatting of floppy
 *      tape cartridges with the QIC-40/80/3010/3020 floppy-tape
 *      driver "ftape" for Linux.
 */
 
#include <linux/string.h>
#include <linux/errno.h>

#include <linux/ftape.h>
#include <linux/qic117.h>
#include "../lowlevel/ftape-tracing.h"
#include "../lowlevel/ftape-io.h"
#include "../lowlevel/ftape-ctl.h"
#include "../lowlevel/ftape-rw.h"
#include "../lowlevel/ftape-ecc.h"
#include "../lowlevel/ftape-bsm.h"
#include "../lowlevel/ftape-format.h"

#if defined(TESTING)
#define FT_FMT_SEGS_PER_BUF 50
#else
#define FT_FMT_SEGS_PER_BUF (FT_BUFF_SIZE/(4*FT_SECTORS_PER_SEGMENT))
#endif

static spinlock_t ftape_format_lock;

/*
 *  first segment of the new buffer
 */
static int switch_segment;

/*
 *  at most 256 segments fit into one 32 kb buffer.  Even TR-1 cartridges have
 *  more than this many segments per track, so better be careful.
 *
 *  buffer_struct *buff: buffer to store the formatting coordinates in
 *  int  start: starting segment for this buffer.
 *  int    spt: segments per track
 *
 *  Note: segment ids are relative to the start of the track here.
 */
static void setup_format_buffer(buffer_struct *buff, int start, int spt,
				__u8 gap3)
{
	int to_do = spt - start;
	TRACE_FUN(ft_t_flow);

	if (to_do > FT_FMT_SEGS_PER_BUF) {
		to_do = FT_FMT_SEGS_PER_BUF;
	}
	buff->ptr          = buff->address;
	buff->remaining    = to_do * FT_SECTORS_PER_SEGMENT; /* # sectors */
	buff->bytes        = buff->remaining * 4; /* need 4 bytes per sector */
	buff->gap3         = gap3;
	buff->segment_id   = start;
	buff->next_segment = start + to_do;
	if (buff->next_segment >= spt) {
		buff->next_segment = 0; /* 0 means: stop runner */
	}
	buff->status       = waiting; /* tells the isr that it can use
				       * this buffer
				       */
	TRACE_EXIT;
}


/*
 *  start formatting a new track.
 */
int ftape_format_track(const unsigned int track, const __u8 gap3)
{
	unsigned long flags;
	buffer_struct *tail, *head;
	int status;
	TRACE_FUN(ft_t_flow);

	TRACE_CATCH(ftape_ready_wait(ftape_timeout.pause, &status),);
	if (track & 1) {
		if (!(status & QIC_STATUS_AT_EOT)) {
			TRACE_CATCH(ftape_seek_to_eot(),);
		}
	} else {
		if (!(status & QIC_STATUS_AT_BOT)) {
			TRACE_CATCH(ftape_seek_to_bot(),);
		}
	}
	ftape_abort_operation(); /* this sets ft_head = ft_tail = 0 */
	ftape_set_state(formatting);

	TRACE(ft_t_noise,
	      "Formatting track %d, logical: from segment %d to %d",
	      track, track * ft_segments_per_track, 
	      (track + 1) * ft_segments_per_track - 1);
	
	/*
	 *  initialize the buffer switching protocol for this track
	 */
	head = ftape_get_buffer(ft_queue_head); /* tape isn't running yet */
	tail = ftape_get_buffer(ft_queue_tail); /* tape isn't running yet */
	switch_segment = 0;
	do {
		FT_SIGNAL_EXIT(_DONT_BLOCK);
		setup_format_buffer(tail, switch_segment,
				    ft_segments_per_track, gap3);
		switch_segment = tail->next_segment;
	} while ((switch_segment != 0) &&
		 ((tail = ftape_next_buffer(ft_queue_tail)) != head));
	/* go */
	head->status = formatting;
	TRACE_CATCH(ftape_seek_head_to_track(track),);
	TRACE_CATCH(ftape_command(QIC_LOGICAL_FORWARD),);
	spin_lock_irqsave(&ftape_format_lock, flags);
	TRACE_CATCH(fdc_setup_formatting(head), restore_flags(flags));
	spin_unlock_irqrestore(&ftape_format_lock, flags);
	TRACE_EXIT 0;
}

/*   return segment id of segment currently being formatted and do the
 *   buffer switching stuff.
 */
int ftape_format_status(unsigned int *segment_id)
{
	buffer_struct *tail = ftape_get_buffer(ft_queue_tail);
	int result;
	TRACE_FUN(ft_t_flow);

	while (switch_segment != 0 &&
	       ftape_get_buffer(ft_queue_head) != tail) {
		FT_SIGNAL_EXIT(_DONT_BLOCK);
		/*  need more buffers, first wait for empty buffer
		 */
		TRACE_CATCH(ftape_wait_segment(formatting),);
		/*  don't worry for gap3. If we ever hit this piece of code,
		 *  then all buffer already have the correct gap3 set!
		 */
		setup_format_buffer(tail, switch_segment,
				    ft_segments_per_track, tail->gap3);
		switch_segment = tail->next_segment;
		if (switch_segment != 0) {
			tail = ftape_next_buffer(ft_queue_tail);
		}
	}
	/*    should runner stop ?
	 */
	if (ft_runner_status == aborting || ft_runner_status == do_abort) {
		buffer_struct *head = ftape_get_buffer(ft_queue_head);
		TRACE(ft_t_warn, "Error formatting segment %d",
		      ftape_get_buffer(ft_queue_head)->segment_id);
		(void)ftape_abort_operation();
		TRACE_EXIT (head->status != error) ? -EAGAIN : -EIO;
	}
	/*
	 *  don't care if the timer expires, this is just kind of a
	 *  "select" operation that lets the calling process sleep
	 *  until something has happened
	 */
	if (fdc_interrupt_wait(5 * FT_SECOND) < 0) {
		TRACE(ft_t_noise, "End of track %d at segment %d",
		      ft_location.track,
		      ftape_get_buffer(ft_queue_head)->segment_id);
		result = 1;  /* end of track, unlock module */
	} else {
		result = 0;
	}
	/*
	 *  the calling process should use the seg id to determine
	 *  which parts of the dma buffers can be safely overwritten
	 *  with new data.
	 */
	*segment_id = ftape_get_buffer(ft_queue_head)->segment_id;
	/*
	 *  Internally we start counting segment ids from the start of
	 *  each track when formatting, but externally we keep them
	 *  relative to the start of the tape:
	 */
	*segment_id += ft_location.track * ft_segments_per_track;
	TRACE_EXIT result;
}

/*
 *  The segment id is relative to the start of the tape
 */
int ftape_verify_segment(const unsigned int segment_id, SectorMap *bsm)
{
	int result;
	int verify_done = 0;
	TRACE_FUN(ft_t_flow);

	TRACE(ft_t_noise, "Verifying segment %d", segment_id);

	if (ft_driver_state != verifying) {
		TRACE(ft_t_noise, "calling ftape_abort_operation");
		if (ftape_abort_operation() < 0) {
			TRACE(ft_t_err, "ftape_abort_operation failed");
			TRACE_EXIT -EIO;
		}
	}
	*bsm = 0x00000000;
	ftape_set_state(verifying);
	for (;;) {
		buffer_struct *tail;
		/*
		 *  Allow escape from this loop on signal
		 */
		FT_SIGNAL_EXIT(_DONT_BLOCK);
		/*
		 *  Search all full buffers for the first matching the
		 *  wanted segment.  Clear other buffers on the fly.
		 */
		tail = ftape_get_buffer(ft_queue_tail);
		while (!verify_done && tail->status == done) {
			/*
			 *  Allow escape from this loop on signal !
			 */
			FT_SIGNAL_EXIT(_DONT_BLOCK);
			if (tail->segment_id == segment_id) {
				/*  If out buffer is already full,
				 *  return its contents.  
				 */
				TRACE(ft_t_flow, "found segment in cache: %d",
				      segment_id);
				if ((tail->soft_error_map |
				     tail->hard_error_map) != 0) {
					TRACE(ft_t_info,"bsm[%d] = 0x%08lx",
					      segment_id,
					      (unsigned long)
					      (tail->soft_error_map |
					      tail->hard_error_map));
					*bsm = (tail->soft_error_map |
						tail->hard_error_map);
				}
				verify_done = 1;
			} else {
				TRACE(ft_t_flow,"zapping segment in cache: %d",
				      tail->segment_id);
			}
			tail->status = waiting;
			tail = ftape_next_buffer(ft_queue_tail);
		}
		if (!verify_done && tail->status == verifying) {
			if (tail->segment_id == segment_id) {
				switch(ftape_wait_segment(verifying)) {
				case 0:
					break;
				case -EINTR:
					TRACE_ABORT(-EINTR, ft_t_warn,
						    "interrupted by "
						    "non-blockable signal");
					break;
				default:
					ftape_abort_operation();
					ftape_set_state(verifying);
					/* be picky */
					TRACE_ABORT(-EIO, ft_t_warn,
						    "wait_segment failed");
				}
			} else {
				/*  We're reading the wrong segment,
				 *  stop runner.
				 */
				TRACE(ft_t_noise, "verifying wrong segment");
				ftape_abort_operation();
				ftape_set_state(verifying);
			}
		}
		/*    should runner stop ?
		 */
		if (ft_runner_status == aborting) {
			buffer_struct *head = ftape_get_buffer(ft_queue_head);
			if (head->status == error ||
			    head->status == verifying) {
				/* no data or overrun error */
				head->status = waiting;
			}
			TRACE_CATCH(ftape_dumb_stop(),);
		} else {
			/*  If just passed last segment on tape: wait
			 *  for BOT or EOT mark. Sets ft_runner_status to
			 *  idle if at lEOT and successful 
			 */
			TRACE_CATCH(ftape_handle_logical_eot(),);
		}
		if (verify_done) {
			TRACE_EXIT 0;
		}
		/*    Now at least one buffer is idle!
		 *    Restart runner & tape if needed.
		 */
		/*  We could optimize the following a little bit. We know that 
		 *  the bad sector map is empty.
		 */
		tail = ftape_get_buffer(ft_queue_tail);
		if (tail->status == waiting) {
			buffer_struct *head = ftape_get_buffer(ft_queue_head);

			ftape_setup_new_segment(head, segment_id, -1);
			ftape_calc_next_cluster(head);
			if (ft_runner_status == idle) {
				result = ftape_start_tape(segment_id,
							  head->sector_offset);
				switch(result) {
				case 0:
					break;
				case -ETIME:
				case -EINTR:
					TRACE_ABORT(result, ft_t_err, "Error: "
						    "segment %d unreachable",
						    segment_id);
					break;
				default:
					*bsm = EMPTY_SEGMENT;
					TRACE_EXIT 0;
					break;
				}
			}
			head->status = verifying;
			fdc_setup_read_write(head, FDC_VERIFY);
		}
	}
	/* not reached */
	TRACE_EXIT -EIO;
}
