/*
 *      Copyright (C) 1993-1996 Bas Laarhoven,
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
 * $Source: /homes/cvs/ftape-stacked/ftape/lowlevel/ftape-rw.c,v $
 * $Revision: 1.7 $
 * $Date: 1997/10/28 14:26:49 $
 *
 *      This file contains some common code for the segment read and
 *      segment write routines for the QIC-117 floppy-tape driver for
 *      Linux.
 */

#include <linux/string.h>
#include <linux/errno.h>

#include <linux/ftape.h>
#include <linux/qic117.h>
#include "../lowlevel/ftape-tracing.h"
#include "../lowlevel/ftape-rw.h"
#include "../lowlevel/fdc-io.h"
#include "../lowlevel/ftape-init.h"
#include "../lowlevel/ftape-io.h"
#include "../lowlevel/ftape-ctl.h"
#include "../lowlevel/ftape-read.h"
#include "../lowlevel/ftape-ecc.h"
#include "../lowlevel/ftape-bsm.h"

/*      Global vars.
 */
int ft_nr_buffers;
buffer_struct *ft_buffer[FT_MAX_NR_BUFFERS];
static volatile int ft_head;
static volatile int ft_tail;	/* not volatile but need same type as head */
int fdc_setup_error;
location_record ft_location = {-1, 0};
volatile int ftape_tape_running;

/*      Local vars.
 */
static int overrun_count_offset;
static int inhibit_correction;

/*  maxmimal allowed overshoot when fast seeking
 */
#define OVERSHOOT_LIMIT 10

/*      Increment cyclic buffer nr.
 */
buffer_struct *ftape_next_buffer(ft_buffer_queue_t pos)
{
	switch (pos) {
	case ft_queue_head:
		if (++ft_head >= ft_nr_buffers) {
			ft_head = 0;
		}
		return ft_buffer[ft_head];
	case ft_queue_tail:
		if (++ft_tail >= ft_nr_buffers) {
			ft_tail = 0;
		}
		return ft_buffer[ft_tail];
	default:
		return NULL;
	}
}
int ftape_buffer_id(ft_buffer_queue_t pos)
{
	switch(pos) {
	case ft_queue_head: return ft_head;
	case ft_queue_tail: return ft_tail;
	default: return -1;
	}
}
buffer_struct *ftape_get_buffer(ft_buffer_queue_t pos)
{
	switch(pos) {
	case ft_queue_head: return ft_buffer[ft_head];
	case ft_queue_tail: return ft_buffer[ft_tail];
	default: return NULL;
	}
}
void ftape_reset_buffer(void)
{
	ft_head = ft_tail = 0;
}

buffer_state_enum ftape_set_state(buffer_state_enum new_state)
{
	buffer_state_enum old_state = ft_driver_state;

	ft_driver_state = new_state;
	return old_state;
}
/*      Calculate Floppy Disk Controller and DMA parameters for a segment.
 *      head:   selects buffer struct in array.
 *      offset: number of physical sectors to skip (including bad ones).
 *      count:  number of physical sectors to handle (including bad ones).
 */
static int setup_segment(buffer_struct * buff, 
			 int segment_id,
			 unsigned int sector_offset, 
			 unsigned int sector_count, 
			 int retry)
{
	SectorMap offset_mask;
	SectorMap mask;
	TRACE_FUN(ft_t_any);

	buff->segment_id = segment_id;
	buff->sector_offset = sector_offset;
	buff->remaining = sector_count;
	buff->head = segment_id / ftape_segments_per_head;
	buff->cyl = (segment_id % ftape_segments_per_head) / ftape_segments_per_cylinder;
	buff->sect = (segment_id % ftape_segments_per_cylinder) * FT_SECTORS_PER_SEGMENT + 1;
	buff->deleted = 0;
	offset_mask = (1 << buff->sector_offset) - 1;
	mask = ftape_get_bad_sector_entry(segment_id) & offset_mask;
	while (mask) {
		if (mask & 1) {
			offset_mask >>= 1;	/* don't count bad sector */
		}
		mask >>= 1;
	}
	buff->data_offset = count_ones(offset_mask);	/* good sectors to skip */
	buff->ptr = buff->address + buff->data_offset * FT_SECTOR_SIZE;
	TRACE(ft_t_flow, "data offset = %d sectors", buff->data_offset);
	if (retry) {
		buff->soft_error_map &= offset_mask;	/* keep skipped part */
	} else {
		buff->hard_error_map = buff->soft_error_map = 0;
	}
	buff->bad_sector_map = ftape_get_bad_sector_entry(buff->segment_id);
	if (buff->bad_sector_map != 0) {
		TRACE(ft_t_noise, "segment: %d, bad sector map: %08lx",
			buff->segment_id, (long)buff->bad_sector_map);
	} else {
		TRACE(ft_t_flow, "segment: %d", buff->segment_id);
	}
	if (buff->sector_offset > 0) {
		buff->bad_sector_map >>= buff->sector_offset;
	}
	if (buff->sector_offset != 0 || buff->remaining != FT_SECTORS_PER_SEGMENT) {
		TRACE(ft_t_flow, "sector offset = %d, count = %d",
			buff->sector_offset, buff->remaining);
	}
	/*    Segments with 3 or less sectors are not written with valid
	 *    data because there is no space left for the ecc.  The
	 *    data written is whatever happens to be in the buffer.
	 *    Reading such a segment will return a zero byte-count.
	 *    To allow us to read/write segments with all bad sectors
	 *    we fake one readable sector in the segment. This
	 *    prevents having to handle these segments in a very
	 *    special way.  It is not important if the reading of this
	 *    bad sector fails or not (the data is ignored). It is
	 *    only read to keep the driver running.
	 *
	 *    The QIC-40/80 spec. has no information on how to handle
	 *    this case, so this is my interpretation.  
	 */
	if (buff->bad_sector_map == EMPTY_SEGMENT) {
		TRACE(ft_t_flow, "empty segment %d, fake first sector good",
		      buff->segment_id);
		if (buff->ptr != buff->address) {
			TRACE(ft_t_bug, "This is a bug: %p/%p",
			      buff->ptr, buff->address);
		}
		buff->bad_sector_map = FAKE_SEGMENT;
	}
	fdc_setup_error = 0;
	buff->next_segment = segment_id + 1;
	TRACE_EXIT 0;
}

/*      Calculate Floppy Disk Controller and DMA parameters for a new segment.
 */
int ftape_setup_new_segment(buffer_struct * buff, int segment_id, int skip)
{
	int result = 0;
	static int old_segment_id = -1;
	static buffer_state_enum old_ft_driver_state = idle;
	int retry = 0;
	unsigned offset = 0;
	int count = FT_SECTORS_PER_SEGMENT;
	TRACE_FUN(ft_t_flow);

	TRACE(ft_t_flow, "%s segment %d (old = %d)",
	      (ft_driver_state == reading || ft_driver_state == verifying) 
	      ? "reading" : "writing",
	      segment_id, old_segment_id);
	if (ft_driver_state != old_ft_driver_state) {	/* when verifying */
		old_segment_id = -1;
		old_ft_driver_state = ft_driver_state;
	}
	if (segment_id == old_segment_id) {
		++buff->retry;
		++ft_history.retries;
		TRACE(ft_t_flow, "setting up for retry nr %d", buff->retry);
		retry = 1;
		if (skip && buff->skip > 0) {	/* allow skip on retry */
			offset = buff->skip;
			count -= offset;
			TRACE(ft_t_flow, "skipping %d sectors", offset);
		}
	} else {
		buff->retry = 0;
		buff->skip = 0;
		old_segment_id = segment_id;
	}
	result = setup_segment(buff, segment_id, offset, count, retry);
	TRACE_EXIT result;
}

/*      Determine size of next cluster of good sectors.
 */
int ftape_calc_next_cluster(buffer_struct * buff)
{
	/* Skip bad sectors.
	 */
	while (buff->remaining > 0 && (buff->bad_sector_map & 1) != 0) {
		buff->bad_sector_map >>= 1;
		++buff->sector_offset;
		--buff->remaining;
	}
	/* Find next cluster of good sectors
	 */
	if (buff->bad_sector_map == 0) {	/* speed up */
		buff->sector_count = buff->remaining;
	} else {
		SectorMap map = buff->bad_sector_map;

		buff->sector_count = 0;
		while (buff->sector_count < buff->remaining && (map & 1) == 0) {
			++buff->sector_count;
			map >>= 1;
		}
	}
	return buff->sector_count;
}

/*  if just passed the last segment on a track, wait for BOT
 *  or EOT mark.
 */
int ftape_handle_logical_eot(void)
{
	TRACE_FUN(ft_t_flow);

	if (ft_runner_status == logical_eot) {
		int status;

		TRACE(ft_t_noise, "tape at logical EOT");
		TRACE_CATCH(ftape_ready_wait(ftape_timeout.seek, &status),);
		if ((status & (QIC_STATUS_AT_BOT | QIC_STATUS_AT_EOT)) == 0) {
			TRACE_ABORT(-EIO, ft_t_err, "eot/bot not reached");
		}
		ft_runner_status = end_of_tape;
	}
	if (ft_runner_status == end_of_tape) {
		TRACE(ft_t_noise, "runner stopped because of logical EOT");
		ft_runner_status = idle;
	}
	TRACE_EXIT 0;
}

static int check_bot_eot(int status)
{
	TRACE_FUN(ft_t_flow);

	if (status & (QIC_STATUS_AT_BOT | QIC_STATUS_AT_EOT)) {
		ft_location.bot = ((ft_location.track & 1) == 0 ?
				(status & QIC_STATUS_AT_BOT) != 0:
				(status & QIC_STATUS_AT_EOT) != 0);
		ft_location.eot = !ft_location.bot;
		ft_location.segment = (ft_location.track +
			(ft_location.bot ? 0 : 1)) * ft_segments_per_track - 1;
		ft_location.sector = -1;
		ft_location.known  = 1;
		TRACE(ft_t_flow, "tape at logical %s",
		      ft_location.bot ? "bot" : "eot");
		TRACE(ft_t_flow, "segment = %d", ft_location.segment);
	} else {
		ft_location.known = 0;
	}
	TRACE_EXIT ft_location.known;
}

/*      Read Id of first sector passing tape head.
 */
static int ftape_read_id(void)
{
	int status;
	__u8 out[2];
	TRACE_FUN(ft_t_any);

	/* Assume tape is running on entry, be able to handle
	 * situation where it stopped or is stopping.
	 */
	ft_location.known = 0;	/* default is location not known */
	out[0] = FDC_READID;
	out[1] = ft_drive_sel;
	TRACE_CATCH(fdc_command(out, 2),);
	switch (fdc_interrupt_wait(20 * FT_SECOND)) {
	case 0:
		if (fdc_sect == 0) {
			if (ftape_report_drive_status(&status) >= 0 &&
			    (status & QIC_STATUS_READY)) {
				ftape_tape_running = 0;
				TRACE(ft_t_flow, "tape has stopped");
				check_bot_eot(status);
			}
		} else {
			ft_location.known = 1;
			ft_location.segment = (ftape_segments_per_head
					       * fdc_head
					       + ftape_segments_per_cylinder
					       * fdc_cyl
					       + (fdc_sect - 1)
					       / FT_SECTORS_PER_SEGMENT);
			ft_location.sector = ((fdc_sect - 1)
					      % FT_SECTORS_PER_SEGMENT);
			ft_location.eot = ft_location.bot = 0;
		}
		break;
	case -ETIME:
		/*  Didn't find id on tape, must be near end: Wait
		 *  until stopped.
		 */
		if (ftape_ready_wait(FT_FOREVER, &status) >= 0) {
			ftape_tape_running = 0;
			TRACE(ft_t_flow, "tape has stopped");
			check_bot_eot(status);
		}
		break;
	default:
		/*  Interrupted or otherwise failing
		 *  fdc_interrupt_wait() 
		 */
		TRACE(ft_t_err, "fdc_interrupt_wait failed");
		break;
	}
	if (!ft_location.known) {
		TRACE_ABORT(-EIO, ft_t_flow, "no id found");
	}
	if (ft_location.sector == 0) {
		TRACE(ft_t_flow, "passing segment %d/%d",
		      ft_location.segment, ft_location.sector);
	} else {
		TRACE(ft_t_fdc_dma, "passing segment %d/%d",
		      ft_location.segment, ft_location.sector);
	}
	TRACE_EXIT 0;
}

static int logical_forward(void)
{
	ftape_tape_running = 1;
	return ftape_command(QIC_LOGICAL_FORWARD);
}

int ftape_stop_tape(int *pstatus)
{
	int retry = 0;
	int result;
	TRACE_FUN(ft_t_flow);

	do {
		result = ftape_command_wait(QIC_STOP_TAPE,
					    ftape_timeout.stop, pstatus);
		if (result == 0) {
			if ((*pstatus & QIC_STATUS_READY) == 0) {
				result = -EIO;
			} else {
				ftape_tape_running = 0;
			}
		}
	} while (result < 0 && ++retry <= 3);
	if (result < 0) {
		TRACE(ft_t_err, "failed ! (fatal)");
	}
	TRACE_EXIT result;
}

int ftape_dumb_stop(void)
{
	int result;
	int status;
	TRACE_FUN(ft_t_flow);

	/*  Abort current fdc operation if it's busy (probably read
	 *  or write operation pending) with a reset.
	 */
	if (fdc_ready_wait(100 /* usec */) < 0) {
		TRACE(ft_t_noise, "aborting fdc operation");
		fdc_reset();
	}
	/*  Reading id's after the last segment on a track may fail
	 *  but eventually the drive will become ready (logical eot).
	 */
	result = ftape_report_drive_status(&status);
	ft_location.known = 0;
	do {
		if (result == 0 && status & QIC_STATUS_READY) {
			/* Tape is not running any more.
			 */
			TRACE(ft_t_noise, "tape already halted");
			check_bot_eot(status);
			ftape_tape_running = 0;
		} else if (ftape_tape_running) {
			/*  Tape is (was) still moving.
			 */
#ifdef TESTING
			ftape_read_id();
#endif
			result = ftape_stop_tape(&status);
		} else {
			/*  Tape not yet ready but stopped.
			 */
			result = ftape_ready_wait(ftape_timeout.pause,&status);
		}
	} while (ftape_tape_running
		 && !(sigtestsetmask(&current->pending.signal, _NEVER_BLOCK)));
#ifndef TESTING
	ft_location.known = 0;
#endif
	if (ft_runner_status == aborting || ft_runner_status == do_abort) {
		ft_runner_status = idle;
	}
	TRACE_EXIT result;
}

/*      Wait until runner has finished tail buffer.
 *
 */
int ftape_wait_segment(buffer_state_enum state)
{
	int status;
	int result = 0;
	TRACE_FUN(ft_t_flow);

	while (ft_buffer[ft_tail]->status == state) {
		TRACE(ft_t_flow, "state: %d", ft_buffer[ft_tail]->status);
		/*  First buffer still being worked on, wait up to timeout.
		 *
		 *  Note: we check two times for being killed. 50
		 *  seconds are quite long. Note that
		 *  fdc_interrupt_wait() is not killable by any
		 *  means. ftape_read_segment() wants us to return
		 *  -EINTR in case of a signal.  
		 */
		FT_SIGNAL_EXIT(_DONT_BLOCK);
		result = fdc_interrupt_wait(50 * FT_SECOND);
		FT_SIGNAL_EXIT(_DONT_BLOCK);
		if (result < 0) {
			TRACE_ABORT(result,
				    ft_t_err, "fdc_interrupt_wait failed");
		}
		if (fdc_setup_error) {
			/* recover... FIXME */
			TRACE_ABORT(-EIO, ft_t_err, "setup error");
		}
	}
	if (ft_buffer[ft_tail]->status != error) {
		TRACE_EXIT 0;
	}
	TRACE_CATCH(ftape_report_drive_status(&status),);
	TRACE(ft_t_noise, "ftape_report_drive_status: 0x%02x", status);
	if ((status & QIC_STATUS_READY) && 
	    (status & QIC_STATUS_ERROR)) {
		unsigned int error;
		qic117_cmd_t command;
		
		/*  Report and clear error state.
		 *  In case the drive can't operate at the selected
		 *  rate, select the next lower data rate.
		 */
		ftape_report_error(&error, &command, 1);
		if (error == 31 && command == QIC_LOGICAL_FORWARD) {
			/* drive does not accept this data rate */
			if (ft_data_rate > 250) {
				TRACE(ft_t_info,
				      "Probable data rate conflict");
				TRACE(ft_t_info,
				      "Lowering data rate to %d Kbps",
				      ft_data_rate / 2);
				ftape_half_data_rate();
				if (ft_buffer[ft_tail]->retry > 0) {
					/* give it a chance */
					--ft_buffer[ft_tail]->retry;
				}
			} else {
				/* no rate is accepted... */
				TRACE(ft_t_err, "We're dead :(");
			}
		} else {
			TRACE(ft_t_err, "Unknown error");
		}
		TRACE_EXIT -EIO;   /* g.p. error */
	}
	TRACE_EXIT 0;
}

/* forward */ static int seek_forward(int segment_id, int fast);

static int fast_seek(int count, int reverse)
{
	int result = 0;
	int status;
	TRACE_FUN(ft_t_flow);

	if (count > 0) {
		/*  If positioned at begin or end of tape, fast seeking needs
		 *  special treatment.
		 *  Starting from logical bot needs a (slow) seek to the first
		 *  segment before the high speed seek. Most drives do this
		 *  automatically but some older don't, so we treat them
		 *  all the same.
		 *  Starting from logical eot is even more difficult because
		 *  we cannot (slow) reverse seek to the last segment.
		 *  TO BE IMPLEMENTED.
		 */
		inhibit_correction = 0;
		if (ft_location.known &&
		    ((ft_location.bot && !reverse) ||
		     (ft_location.eot && reverse))) {
			if (!reverse) {
				/*  (slow) skip to first segment on a track
				 */
				seek_forward(ft_location.track * ft_segments_per_track, 0);
				--count;
			} else {
				/*  When seeking backwards from
				 *  end-of-tape the number of erased
				 *  gaps found seems to be higher than
				 *  expected.  Therefor the drive must
				 *  skip some more segments than
				 *  calculated, but we don't know how
				 *  many.  Thus we will prevent the
				 *  re-calculation of offset and
				 *  overshoot when seeking backwards.
				 */
				inhibit_correction = 1;
				count += 3;	/* best guess */
			}
		}
	} else {
		TRACE(ft_t_flow, "warning: zero or negative count: %d", count);
	}
	if (count > 0) {
		int i;
		int nibbles = count > 255 ? 3 : 2;

		if (count > 4095) {
			TRACE(ft_t_noise, "skipping clipped at 4095 segment");
			count = 4095;
		}
		/* Issue this tape command first. */
		if (!reverse) {
			TRACE(ft_t_noise, "skipping %d segment(s)", count);
			result = ftape_command(nibbles == 3 ?
			   QIC_SKIP_EXTENDED_FORWARD : QIC_SKIP_FORWARD);
		} else {
			TRACE(ft_t_noise, "backing up %d segment(s)", count);
			result = ftape_command(nibbles == 3 ?
			   QIC_SKIP_EXTENDED_REVERSE : QIC_SKIP_REVERSE);
		}
		if (result < 0) {
			TRACE(ft_t_noise, "Skip command failed");
		} else {
			--count;	/* 0 means one gap etc. */
			for (i = 0; i < nibbles; ++i) {
				if (result >= 0) {
					result = ftape_parameter(count & 15);
					count /= 16;
				}
			}
			result = ftape_ready_wait(ftape_timeout.rewind, &status);
			if (result >= 0) {
				ftape_tape_running = 0;
			}
		}
	}
	TRACE_EXIT result;
}

static int validate(int id)
{
	/* Check to see if position found is off-track as reported
	 *  once.  Because all tracks in one direction lie next to
	 *  each other, if off-track the error will be approximately
	 *  2 * ft_segments_per_track.
	 */
	if (ft_location.track == -1) {
		return 1; /* unforseen situation, don't generate error */
	} else {
		/* Use margin of ft_segments_per_track on both sides
		 * because ftape needs some margin and the error we're
		 * looking for is much larger !
		 */
		int lo = (ft_location.track - 1) * ft_segments_per_track;
		int hi = (ft_location.track + 2) * ft_segments_per_track;

		return (id >= lo && id < hi);
	}
}

static int seek_forward(int segment_id, int fast)
{
	int failures = 0;
	int count;
	static int margin = 1;	/* fixed: stop this before target */
	static int overshoot = 1;
	static int min_count = 8;
	int expected = -1;
	int target = segment_id - margin;
	int fast_seeking;
	int prev_segment = ft_location.segment;
	TRACE_FUN(ft_t_flow);

	if (!ft_location.known) {
		TRACE_ABORT(-EIO, ft_t_err,
			    "fatal: cannot seek from unknown location");
	}
	if (!validate(segment_id)) {
		ftape_sleep(1 * FT_SECOND);
		ft_failure = 1;
		TRACE_ABORT(-EIO, ft_t_err,
			    "fatal: head off track (bad hardware?)");
	}
	TRACE(ft_t_noise, "from %d/%d to %d/0 - %d",
	      ft_location.segment, ft_location.sector,segment_id,margin);
	count = target - ft_location.segment - overshoot;
	fast_seeking = (fast &&
			count > (min_count + (ft_location.bot ? 1 : 0)));
	if (fast_seeking) {
		TRACE(ft_t_noise, "fast skipping %d segments", count);
		expected = segment_id - margin;
		fast_seek(count, 0);
	}
	if (!ftape_tape_running) {
		logical_forward();
	}
	while (ft_location.segment < segment_id) {
		/*  This requires at least one sector in a (bad) segment to
		 *  have a valid and readable sector id !
		 *  It looks like this is not guaranteed, so we must try
		 *  to find a way to skip an EMPTY_SEGMENT. !!! FIXME !!!
		 */
		if (ftape_read_id() < 0 || !ft_location.known ||
		    sigtestsetmask(&current->pending.signal, _DONT_BLOCK)) {
			ft_location.known = 0;
			if (!ftape_tape_running ||
			    ++failures > FT_SECTORS_PER_SEGMENT) {
				TRACE_ABORT(-EIO, ft_t_err,
					    "read_id failed completely");
			}
			FT_SIGNAL_EXIT(_DONT_BLOCK);
			TRACE(ft_t_flow, "read_id failed, retry (%d)",
			      failures);
			continue;
		}
		if (fast_seeking) {
			TRACE(ft_t_noise, "ended at %d/%d (%d,%d)",
			      ft_location.segment, ft_location.sector,
			      overshoot, inhibit_correction);
			if (!inhibit_correction &&
			    (ft_location.segment < expected ||
			     ft_location.segment > expected + margin)) {
				int error = ft_location.segment - expected;
				TRACE(ft_t_noise,
				      "adjusting overshoot from %d to %d",
				      overshoot, overshoot + error);
				overshoot += error;
				/*  All overshoots have the same
				 *  direction, so it should never
				 *  become negative, but who knows.
				 */
				if (overshoot < -5 ||
				    overshoot > OVERSHOOT_LIMIT) {
					if (overshoot < 0) {
						/* keep sane value */
						overshoot = -5;
					} else {
						/* keep sane value */
						overshoot = OVERSHOOT_LIMIT;
					}
					TRACE(ft_t_noise,
					      "clipped overshoot to %d",
					      overshoot);
				}
			}
			fast_seeking = 0;
		}
		if (ft_location.known) {
			if (ft_location.segment > prev_segment + 1) {
				TRACE(ft_t_noise,
				      "missed segment %d while skipping",
				      prev_segment + 1);
			}
			prev_segment = ft_location.segment;
		}
	}
	if (ft_location.segment > segment_id) {
		TRACE_ABORT(-EIO,
			    ft_t_noise, "failed: skip ended at segment %d/%d",
			    ft_location.segment, ft_location.sector);
	}
	TRACE_EXIT 0;
}

static int skip_reverse(int segment_id, int *pstatus)
{
	int failures = 0;
	static int overshoot = 1;
	static int min_rewind = 2;	/* 1 + overshoot */
	static const int margin = 1;	/* stop this before target */
	int expected = 0;
	int count = 1;
	int short_seek;
	int target = segment_id - margin;
	TRACE_FUN(ft_t_flow);

	if (ft_location.known && !validate(segment_id)) {
		ftape_sleep(1 * FT_SECOND);
		ft_failure = 1;
		TRACE_ABORT(-EIO, ft_t_err,
			    "fatal: head off track (bad hardware?)");
	}
	do {
		if (!ft_location.known) {
			TRACE(ft_t_warn, "warning: location not known");
		}
		TRACE(ft_t_noise, "from %d/%d to %d/0 - %d",
		      ft_location.segment, ft_location.sector,
		      segment_id, margin);
		/*  min_rewind == 1 + overshoot_when_doing_minimum_rewind
		 *  overshoot  == overshoot_when_doing_larger_rewind
		 *  Initially min_rewind == 1 + overshoot, optimization
		 *  of both values will be done separately.
		 *  overshoot and min_rewind can be negative as both are
		 *  sums of three components:
		 *  any_overshoot == rewind_overshoot - 
		 *                   stop_overshoot   -
		 *                   start_overshoot
		 */
		if (ft_location.segment - target - (min_rewind - 1) < 1) {
			short_seek = 1;
		} else {
			count = ft_location.segment - target - overshoot;
			short_seek = (count < 1);
		}
		if (short_seek) {
			count = 1;	/* do shortest rewind */
			expected = ft_location.segment - min_rewind;
			if (expected/ft_segments_per_track != ft_location.track) {
				expected = (ft_location.track * 
					    ft_segments_per_track);
			}
		} else {
			expected = target;
		}
		fast_seek(count, 1);
		logical_forward();
		if (ftape_read_id() < 0 || !ft_location.known ||
		    (sigtestsetmask(&current->pending.signal, _DONT_BLOCK))) {
			if ((!ftape_tape_running && !ft_location.known) ||
			    ++failures > FT_SECTORS_PER_SEGMENT) {
				TRACE_ABORT(-EIO, ft_t_err,
					    "read_id failed completely");
			}
			FT_SIGNAL_EXIT(_DONT_BLOCK);
			TRACE_CATCH(ftape_report_drive_status(pstatus),);
			TRACE(ft_t_noise, "ftape_read_id failed, retry (%d)",
			      failures);
			continue;
		}
		TRACE(ft_t_noise, "ended at %d/%d (%d,%d,%d)", 
		      ft_location.segment, ft_location.sector,
		      min_rewind, overshoot, inhibit_correction);
		if (!inhibit_correction &&
		    (ft_location.segment < expected ||
		     ft_location.segment > expected + margin)) {
			int error = expected - ft_location.segment;
			if (short_seek) {
				TRACE(ft_t_noise,
				      "adjusting min_rewind from %d to %d",
				      min_rewind, min_rewind + error);
				min_rewind += error;
				if (min_rewind < -5) {
					/* is this right ? FIXME ! */
					/* keep sane value */
					min_rewind = -5;
					TRACE(ft_t_noise, 
					      "clipped min_rewind to %d",
					      min_rewind);
				}
			} else {
				TRACE(ft_t_noise,
				      "adjusting overshoot from %d to %d",
				      overshoot, overshoot + error);
				overshoot += error;
				if (overshoot < -5 ||
				    overshoot > OVERSHOOT_LIMIT) {
					if (overshoot < 0) {
						/* keep sane value */
						overshoot = -5;
					} else {
						/* keep sane value */
						overshoot = OVERSHOOT_LIMIT;
					}
					TRACE(ft_t_noise,
					      "clipped overshoot to %d",
					      overshoot);
				}
			}
		}
	} while (ft_location.segment > segment_id);
	if (ft_location.known) {
		TRACE(ft_t_noise, "current location: %d/%d",
		      ft_location.segment, ft_location.sector);
	}
	TRACE_EXIT 0;
}

static int determine_position(void)
{
	int retry = 0;
	int status;
	int result;
	TRACE_FUN(ft_t_flow);

	if (!ftape_tape_running) {
		/*  This should only happen if tape is stopped by isr.
		 */
		TRACE(ft_t_flow, "waiting for tape stop");
		if (ftape_ready_wait(ftape_timeout.pause, &status) < 0) {
			TRACE(ft_t_flow, "drive still running (fatal)");
			ftape_tape_running = 1;	/* ? */
		}
	} else {
		ftape_report_drive_status(&status);
	}
	if (status & QIC_STATUS_READY) {
		/*  Drive must be ready to check error state !
		 */
		TRACE(ft_t_flow, "drive is ready");
		if (status & QIC_STATUS_ERROR) {
			unsigned int error;
			qic117_cmd_t command;

			/*  Report and clear error state, try to continue.
			 */
			TRACE(ft_t_flow, "error status set");
			ftape_report_error(&error, &command, 1);
			ftape_ready_wait(ftape_timeout.reset, &status);
			ftape_tape_running = 0;	/* ? */
		}
		if (check_bot_eot(status)) {
			if (ft_location.bot) {
				if ((status & QIC_STATUS_READY) == 0) {
					/* tape moving away from
					 * bot/eot, let's see if we
					 * can catch up with the first
					 * segment on this track.
					 */
				} else {
					TRACE(ft_t_flow,
					      "start tape from logical bot");
					logical_forward();	/* start moving */
				}
			} else {
				if ((status & QIC_STATUS_READY) == 0) {
					TRACE(ft_t_noise, "waiting for logical end of track");
					result = ftape_ready_wait(ftape_timeout.reset, &status);
					/* error handling needed ? */
				} else {
					TRACE(ft_t_noise,
					      "tape at logical end of track");
				}
			}
		} else {
			TRACE(ft_t_flow, "start tape");
			logical_forward();	/* start moving */
			ft_location.known = 0;	/* not cleared by logical forward ! */
		}
	}
	/* tape should be moving now, start reading id's
	 */
	while (!ft_location.known &&
	       retry++ < FT_SECTORS_PER_SEGMENT &&
	       (result = ftape_read_id()) < 0) {

		TRACE(ft_t_flow, "location unknown");

		/* exit on signal
		 */
		FT_SIGNAL_EXIT(_DONT_BLOCK);

		/*  read-id somehow failed, tape may
		 *  have reached end or some other
		 *  error happened.
		 */
		TRACE(ft_t_flow, "read-id failed");
		TRACE_CATCH(ftape_report_drive_status(&status),);
		TRACE(ft_t_err, "ftape_report_drive_status: 0x%02x", status);
		if (status & QIC_STATUS_READY) {
			ftape_tape_running = 0;
			TRACE(ft_t_noise, "tape stopped for unknown reason! "
			      "status = 0x%02x", status);
			if (status & QIC_STATUS_ERROR ||
			    !check_bot_eot(status)) {
				/* oops, tape stopped but not at end!
				 */
				TRACE_EXIT -EIO;
			}
		}
	}
	TRACE(ft_t_flow,
	      "tape is positioned at segment %d", ft_location.segment);
	TRACE_EXIT ft_location.known ? 0 : -EIO;
}

/*      Get the tape running and position it just before the
 *      requested segment.
 *      Seek tape-track and reposition as needed.
 */
int ftape_start_tape(int segment_id, int sector_offset)
{
	int track = segment_id / ft_segments_per_track;
	int result = -EIO;
	int status;
	static int last_segment = -1;
	static int bad_bus_timing = 0;
	/* number of segments passing the head between starting the tape
	 * and being able to access the first sector.
	 */
	static int start_offset = 1;
	int retry;
	TRACE_FUN(ft_t_flow);

	/* If sector_offset > 0, seek into wanted segment instead of
	 * into previous.
	 * This allows error recovery if a part of the segment is bad
	 * (erased) causing the tape drive to generate an index pulse
	 * thus causing a no-data error before the requested sector
	 * is reached.
	 */
	ftape_tape_running = 0;
	TRACE(ft_t_noise, "target segment: %d/%d%s", segment_id, sector_offset,
		ft_buffer[ft_head]->retry > 0 ? " retry" : "");
	if (ft_buffer[ft_head]->retry > 0) {	/* this is a retry */
		int dist = segment_id - last_segment;

		if ((int)ft_history.overrun_errors < overrun_count_offset) {
			overrun_count_offset = ft_history.overrun_errors;
		} else if (dist < 0 || dist > 50) {
			overrun_count_offset = ft_history.overrun_errors;
		} else if ((ft_history.overrun_errors -
			    overrun_count_offset) >= 8) {
			if (ftape_increase_threshold() >= 0) {
				--ft_buffer[ft_head]->retry;
				overrun_count_offset =
					ft_history.overrun_errors;
				TRACE(ft_t_warn, "increased threshold because "
				      "of excessive overrun errors");
			} else if (!bad_bus_timing && ft_data_rate >= 1000) {
				ftape_half_data_rate();
				--ft_buffer[ft_head]->retry;
				bad_bus_timing = 1;
				overrun_count_offset =
					ft_history.overrun_errors;
				TRACE(ft_t_warn, "reduced datarate because "
				      "of excessive overrun errors");
			}
		}
	}
	last_segment = segment_id;
	if (ft_location.track != track ||
	    (ftape_might_be_off_track && ft_buffer[ft_head]->retry== 0)) {
		/* current track unknown or not equal to destination
		 */
		ftape_ready_wait(ftape_timeout.seek, &status);
		ftape_seek_head_to_track(track);
		/* overrun_count_offset = ft_history.overrun_errors; */
	}
	result = -EIO;
	retry = 0;
	while (result < 0     &&
	       retry++ <= 5   &&
	       !ft_failure &&
	       !(sigtestsetmask(&current->pending.signal, _DONT_BLOCK))) {
		
		if (retry && start_offset < 5) {
			start_offset ++;
		}
		/*  Check if we are able to catch the requested
		 *  segment in time.
		 */
		if ((ft_location.known || (determine_position() == 0)) &&
		    ft_location.segment >=
		    (segment_id -
		     ((ftape_tape_running || ft_location.bot)
		      ? 0 : start_offset))) {
			/*  Too far ahead (in or past target segment).
			 */
			if (ftape_tape_running) {
				if ((result = ftape_stop_tape(&status)) < 0) {
					TRACE(ft_t_err,
					      "stop tape failed with code %d",
					      result);
					break;
				}
				TRACE(ft_t_noise, "tape stopped");
				ftape_tape_running = 0;
			}
			TRACE(ft_t_noise, "repositioning");
			++ft_history.rewinds;
			if (segment_id % ft_segments_per_track < start_offset){
				TRACE(ft_t_noise, "end of track condition\n"
				      KERN_INFO "segment_id        : %d\n"
				      KERN_INFO "ft_segments_per_track: %d\n"
				      KERN_INFO "start_offset      : %d",
				      segment_id, ft_segments_per_track, 
				      start_offset);
				      
				/*  If seeking to first segments on
				 *  track better do a complete rewind
				 *  to logical begin of track to get a
				 *  more steady tape motion.  
				 */
				result = ftape_command_wait(
					(ft_location.track & 1)
					? QIC_PHYSICAL_FORWARD
					: QIC_PHYSICAL_REVERSE,
					ftape_timeout.rewind, &status);
				check_bot_eot(status);	/* update location */
			} else {
				result= skip_reverse(segment_id - start_offset,
						     &status);
			}
		}
		if (!ft_location.known) {
			TRACE(ft_t_bug, "panic: location not known");
			result = -EIO;
			continue; /* while() will check for failure */
		}
		TRACE(ft_t_noise, "current segment: %d/%d",
		      ft_location.segment, ft_location.sector);
		/*  We're on the right track somewhere before the
		 *  wanted segment.  Start tape movement if needed and
		 *  skip to just before or inside the requested
		 *  segment. Keep tape running.  
		 */
		result = 0;
		if (ft_location.segment < 
		    (segment_id - ((ftape_tape_running || ft_location.bot)
				   ? 0 : start_offset))) {
			if (sector_offset > 0) {
				result = seek_forward(segment_id,
						      retry <= 3);
			} else {
				result = seek_forward(segment_id - 1,
						      retry <= 3);
			}
		}
		if (result == 0 &&
		    ft_location.segment !=
		    (segment_id - (sector_offset > 0 ? 0 : 1))) {
			result = -EIO;
		}
	}
	if (result < 0) {
		TRACE(ft_t_err, "failed to reposition");
	} else {
		ft_runner_status = running;
	}
	TRACE_EXIT result;
}
