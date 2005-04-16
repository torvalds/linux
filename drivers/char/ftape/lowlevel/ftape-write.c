/*
 *      Copyright (C) 1993-1995 Bas Laarhoven,
 *                (C) 1996-1997 Claus-Justus Heine.

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
 * $Source: /homes/cvs/ftape-stacked/ftape/lowlevel/ftape-write.c,v $
 * $Revision: 1.3.4.1 $
 * $Date: 1997/11/14 18:07:04 $
 *
 *      This file contains the writing code
 *      for the QIC-117 floppy-tape driver for Linux.
 */

#include <linux/string.h>
#include <linux/errno.h>
#include <linux/mm.h>

#include <linux/ftape.h>
#include <linux/qic117.h>
#include "../lowlevel/ftape-tracing.h"
#include "../lowlevel/ftape-write.h"
#include "../lowlevel/ftape-read.h"
#include "../lowlevel/ftape-io.h"
#include "../lowlevel/ftape-ctl.h"
#include "../lowlevel/ftape-rw.h"
#include "../lowlevel/ftape-ecc.h"
#include "../lowlevel/ftape-bsm.h"
#include "../lowlevel/fdc-isr.h"

/*      Global vars.
 */

/*      Local vars.
 */
static int last_write_failed;

void ftape_zap_write_buffers(void)
{
	int i;

	for (i = 0; i < ft_nr_buffers; ++i) {
		ft_buffer[i]->status = done;
	}
	ftape_reset_buffer();
}

static int copy_and_gen_ecc(void *destination, 
			    const void *source,
			    const SectorMap bad_sector_map)
{
	int result;
	struct memory_segment mseg;
	int bads = count_ones(bad_sector_map);
	TRACE_FUN(ft_t_any);

	if (bads > 0) {
		TRACE(ft_t_noise, "bad sectors in map: %d", bads);
	}
	if (bads + 3 >= FT_SECTORS_PER_SEGMENT) {
		TRACE(ft_t_noise, "empty segment");
		mseg.blocks = 0; /* skip entire segment */
		result = 0;      /* nothing written */
	} else {
		mseg.blocks = FT_SECTORS_PER_SEGMENT - bads;
		mseg.data = destination;
		memcpy(mseg.data, source, (mseg.blocks - 3) * FT_SECTOR_SIZE);
		result = ftape_ecc_set_segment_parity(&mseg);
		if (result < 0) {
			TRACE(ft_t_err, "ecc_set_segment_parity failed");
		} else {
			result = (mseg.blocks - 3) * FT_SECTOR_SIZE;
		}
	}
	TRACE_EXIT result;
}


int ftape_start_writing(const ft_write_mode_t mode)
{
	buffer_struct *head = ftape_get_buffer(ft_queue_head);
	int segment_id = head->segment_id;
	int result;
	buffer_state_enum wanted_state = (mode == FT_WR_DELETE
					  ? deleting
					  : writing);
	TRACE_FUN(ft_t_flow);

	if ((ft_driver_state != wanted_state) || head->status != waiting) {
		TRACE_EXIT 0;
	}
	ftape_setup_new_segment(head, segment_id, 1);
	if (mode == FT_WR_SINGLE) {
		/* stop tape instead of pause */
		head->next_segment = 0;
	}
	ftape_calc_next_cluster(head); /* prepare */
	head->status = ft_driver_state; /* either writing or deleting */
	if (ft_runner_status == idle) {
		TRACE(ft_t_noise,
		      "starting runner for segment %d", segment_id);
		TRACE_CATCH(ftape_start_tape(segment_id,head->sector_offset),);
	} else {
		TRACE(ft_t_noise, "runner not idle, not starting tape");
	}
	/* go */
	result = fdc_setup_read_write(head, (mode == FT_WR_DELETE
					     ? FDC_WRITE_DELETED : FDC_WRITE));
	ftape_set_state(wanted_state); /* should not be necessary */
	TRACE_EXIT result;
}

/*  Wait until all data is actually written to tape.
 *  
 *  There is a problem: when the tape runs into logical EOT, then this
 *  failes. We need to restart the runner in this case.
 */
int ftape_loop_until_writes_done(void)
{
	buffer_struct *head;
	TRACE_FUN(ft_t_flow);

	while ((ft_driver_state == writing || ft_driver_state == deleting) && 
	       ftape_get_buffer(ft_queue_head)->status != done) {
		/* set the runner status to idle if at lEOT */
		TRACE_CATCH(ftape_handle_logical_eot(),	last_write_failed = 1);
		/* restart the tape if necessary */
		if (ft_runner_status == idle) {
			TRACE(ft_t_noise, "runner is idle, restarting");
			if (ft_driver_state == deleting) {
				TRACE_CATCH(ftape_start_writing(FT_WR_DELETE),
					    last_write_failed = 1);
			} else {
				TRACE_CATCH(ftape_start_writing(FT_WR_MULTI),
					    last_write_failed = 1);
			}
		}
		TRACE(ft_t_noise, "tail: %d, head: %d", 
		      ftape_buffer_id(ft_queue_tail),
		      ftape_buffer_id(ft_queue_head));
		TRACE_CATCH(fdc_interrupt_wait(5 * FT_SECOND),
			    last_write_failed = 1);
		head = ftape_get_buffer(ft_queue_head);
		if (head->status == error) {
			/* Allow escape from loop when signaled !
			 */
			FT_SIGNAL_EXIT(_DONT_BLOCK);
			if (head->hard_error_map != 0) {
				/*  Implement hard write error recovery here
				 */
			}
			/* retry this one */
			head->status = waiting;
			if (ft_runner_status == aborting) {
				ftape_dumb_stop();
			}
			if (ft_runner_status != idle) {
				TRACE_ABORT(-EIO, ft_t_err,
					    "unexpected state: "
					    "ft_runner_status != idle");
			}
			ftape_start_writing(ft_driver_state == deleting
					    ? FT_WR_MULTI : FT_WR_DELETE);
		}
		TRACE(ft_t_noise, "looping until writes done");
	}
	ftape_set_state(idle);
	TRACE_EXIT 0;
}

/*      Write given segment from buffer at address to tape.
 */
static int write_segment(const int segment_id,
			 const void *address, 
			 const ft_write_mode_t write_mode)
{
	int bytes_written = 0;
	buffer_struct *tail;
	buffer_state_enum wanted_state = (write_mode == FT_WR_DELETE
					  ? deleting : writing);
	TRACE_FUN(ft_t_flow);

	TRACE(ft_t_noise, "segment_id = %d", segment_id);
	if (ft_driver_state != wanted_state) {
		if (ft_driver_state == deleting ||
		    wanted_state == deleting) {
			TRACE_CATCH(ftape_loop_until_writes_done(),);
		}
		TRACE(ft_t_noise, "calling ftape_abort_operation");
		TRACE_CATCH(ftape_abort_operation(),);
		ftape_zap_write_buffers();
		ftape_set_state(wanted_state);
	}
	/*    if all buffers full we'll have to wait...
	 */
	ftape_wait_segment(wanted_state);
	tail = ftape_get_buffer(ft_queue_tail);
	switch(tail->status) {
	case done:
		ft_history.defects += count_ones(tail->hard_error_map);
		break;
	case waiting:
		/* this could happen with multiple EMPTY_SEGMENTs, but
		 * shouldn't happen any more as we re-start the runner even
		 * with an empty segment.
		 */
		bytes_written = -EAGAIN;
		break;
	case error:
		/*  setup for a retry
		 */
		tail->status = waiting;
		bytes_written = -EAGAIN; /* force retry */
		if (tail->hard_error_map != 0) {
			TRACE(ft_t_warn, 
			      "warning: %d hard error(s) in written segment",
			      count_ones(tail->hard_error_map));
			TRACE(ft_t_noise, "hard_error_map = 0x%08lx", 
			      (long)tail->hard_error_map);
			/*  Implement hard write error recovery here
			 */
		}
		break;
	default:
		TRACE_ABORT(-EIO, ft_t_err,
			    "wait for empty segment failed, tail status: %d",
			    tail->status);
	}
	/*    should runner stop ?
	 */
	if (ft_runner_status == aborting) {
		buffer_struct *head = ftape_get_buffer(ft_queue_head);
		if (head->status == wanted_state) {
			head->status = done; /* ???? */
		}
		/*  don't call abort_operation(), we don't want to zap
		 *  the dma buffers
		 */
		TRACE_CATCH(ftape_dumb_stop(),);
	} else {
		/*  If just passed last segment on tape: wait for BOT
		 *  or EOT mark. Sets ft_runner_status to idle if at lEOT
		 *  and successful 
		 */
		TRACE_CATCH(ftape_handle_logical_eot(),);
	}
	if (tail->status == done) {
		/* now at least one buffer is empty, fill it with our
		 * data.  skip bad sectors and generate ecc.
		 * copy_and_gen_ecc return nr of bytes written, range
		 * 0..29 Kb inclusive!  
		 *
		 * Empty segments are handled inside coyp_and_gen_ecc()
		 */
		if (write_mode != FT_WR_DELETE) {
			TRACE_CATCH(bytes_written = copy_and_gen_ecc(
				tail->address, address,
				ftape_get_bad_sector_entry(segment_id)),);
		}
		tail->segment_id = segment_id;
		tail->status = waiting;
		tail = ftape_next_buffer(ft_queue_tail);
	}
	/*  Start tape only if all buffers full or flush mode.
	 *  This will give higher probability of streaming.
	 */
	if (ft_runner_status != running && 
	    ((tail->status == waiting &&
	      ftape_get_buffer(ft_queue_head) == tail) ||
	     write_mode != FT_WR_ASYNC)) {
		TRACE_CATCH(ftape_start_writing(write_mode),);
	}
	TRACE_EXIT bytes_written;
}

/*  Write as much as fits from buffer to the given segment on tape
 *  and handle retries.
 *  Return the number of bytes written (>= 0), or:
 *      -EIO          write failed
 *      -EINTR        interrupted by signal
 *      -ENOSPC       device full
 */
int ftape_write_segment(const int segment_id,
			const void *buffer, 
			const ft_write_mode_t flush)
{
	int retry = 0;
	int result;
	TRACE_FUN(ft_t_flow);

	ft_history.used |= 2;
	if (segment_id >= ft_tracks_per_tape*ft_segments_per_track) {
		/* tape full */
		TRACE_ABORT(-ENOSPC, ft_t_err,
			    "invalid segment id: %d (max %d)", 
			    segment_id, 
			    ft_tracks_per_tape * ft_segments_per_track -1);
	}
	for (;;) {
		if ((result = write_segment(segment_id, buffer, flush)) >= 0) {
			if (result == 0) { /* empty segment */
				TRACE(ft_t_noise,
				      "empty segment, nothing written");
			}
			TRACE_EXIT result;
		}
		if (result == -EAGAIN) {
			if (++retry > 100) { /* give up */
				TRACE_ABORT(-EIO, ft_t_err,
				      "write failed, >100 retries in segment");
			}
			TRACE(ft_t_warn, "write error, retry %d (%d)",
			      retry,
			      ftape_get_buffer(ft_queue_tail)->segment_id);
		} else {
			TRACE_ABORT(result, ft_t_err,
				    "write_segment failed, error: %d", result);
		}
		/* Allow escape from loop when signaled !
		 */
		FT_SIGNAL_EXIT(_DONT_BLOCK);
	}
}
