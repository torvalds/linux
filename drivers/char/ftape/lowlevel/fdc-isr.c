/*
 *      Copyright (C) 1994-1996 Bas Laarhoven,
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
 * $Source: /homes/cvs/ftape-stacked/ftape/lowlevel/fdc-isr.c,v $
 * $Revision: 1.9 $
 * $Date: 1997/10/17 23:01:53 $
 *
 *      This file contains the interrupt service routine and
 *      associated code for the QIC-40/80/3010/3020 floppy-tape driver
 *      "ftape" for Linux.
 */

#include <asm/io.h>
#include <asm/dma.h>

#define volatile		/* */

#include <linux/ftape.h>
#include <linux/qic117.h>
#include "../lowlevel/ftape-tracing.h"
#include "../lowlevel/fdc-isr.h"
#include "../lowlevel/fdc-io.h"
#include "../lowlevel/ftape-ctl.h"
#include "../lowlevel/ftape-rw.h"
#include "../lowlevel/ftape-io.h"
#include "../lowlevel/ftape-calibr.h"
#include "../lowlevel/ftape-bsm.h"

/*      Global vars.
 */
volatile int ft_expected_stray_interrupts;
volatile int ft_interrupt_seen;
volatile int ft_seek_completed;
volatile int ft_hide_interrupt;
/*      Local vars.
 */
typedef enum {
	no_error = 0, id_am_error = 0x01, id_crc_error = 0x02,
	data_am_error = 0x04, data_crc_error = 0x08,
	no_data_error = 0x10, overrun_error = 0x20,
} error_cause;
static int stop_read_ahead;


static void print_error_cause(int cause)
{
	TRACE_FUN(ft_t_any);

	switch (cause) {
	case no_data_error:
		TRACE(ft_t_noise, "no data error");
		break;
	case id_am_error:
		TRACE(ft_t_noise, "id am error");
		break;
	case id_crc_error:
		TRACE(ft_t_noise, "id crc error");
		break;
	case data_am_error:
		TRACE(ft_t_noise, "data am error");
		break;
	case data_crc_error:
		TRACE(ft_t_noise, "data crc error");
		break;
	case overrun_error:
		TRACE(ft_t_noise, "overrun error");
		break;
	default:;
	}
	TRACE_EXIT;
}

static char *fdc_mode_txt(fdc_mode_enum mode)
{
	switch (mode) {
	case fdc_idle:
		return "fdc_idle";
	case fdc_reading_data:
		return "fdc_reading_data";
	case fdc_seeking:
		return "fdc_seeking";
	case fdc_writing_data:
		return "fdc_writing_data";
	case fdc_reading_id:
		return "fdc_reading_id";
	case fdc_recalibrating:
		return "fdc_recalibrating";
	case fdc_formatting:
		return "fdc_formatting";
	case fdc_verifying:
		return "fdc_verifying";
	default:
		return "unknown";
	}
}

static inline error_cause decode_irq_cause(fdc_mode_enum mode, __u8 st[])
{
	error_cause cause = no_error;
	TRACE_FUN(ft_t_any);

	/*  Valid st[], decode cause of interrupt.
	 */
	switch (st[0] & ST0_INT_MASK) {
	case FDC_INT_NORMAL:
		TRACE(ft_t_fdc_dma,"normal completion: %s",fdc_mode_txt(mode));
		break;
	case FDC_INT_ABNORMAL:
		TRACE(ft_t_flow, "abnormal completion %s", fdc_mode_txt(mode));
		TRACE(ft_t_fdc_dma, "ST0: 0x%02x, ST1: 0x%02x, ST2: 0x%02x",
		      st[0], st[1], st[2]);
		TRACE(ft_t_fdc_dma,
		      "C: 0x%02x, H: 0x%02x, R: 0x%02x, N: 0x%02x",
		      st[3], st[4], st[5], st[6]);
		if (st[1] & 0x01) {
			if (st[2] & 0x01) {
				cause = data_am_error;
			} else {
				cause = id_am_error;
			}
		} else if (st[1] & 0x20) {
			if (st[2] & 0x20) {
				cause = data_crc_error;
			} else {
				cause = id_crc_error;
			}
		} else if (st[1] & 0x04) {
			cause = no_data_error;
		} else if (st[1] & 0x10) {
			cause = overrun_error;
		}
		print_error_cause(cause);
		break;
	case FDC_INT_INVALID:
		TRACE(ft_t_flow, "invalid completion %s", fdc_mode_txt(mode));
		break;
	case FDC_INT_READYCH:
		if (st[0] & ST0_SEEK_END) {
			TRACE(ft_t_flow, "drive poll completed");
		} else {
			TRACE(ft_t_flow, "ready change %s",fdc_mode_txt(mode));
		}
		break;
	default:
		break;
	}
	TRACE_EXIT cause;
}

static void update_history(error_cause cause)
{
	switch (cause) {
	case id_am_error:
		ft_history.id_am_errors++;
		break;
	case id_crc_error:
		ft_history.id_crc_errors++;
		break;
	case data_am_error:
		ft_history.data_am_errors++;
		break;
	case data_crc_error:
		ft_history.data_crc_errors++;
		break;
	case overrun_error:
		ft_history.overrun_errors++;
		break;
	case no_data_error:
		ft_history.no_data_errors++;
		break;
	default:;
	}
}

static void skip_bad_sector(buffer_struct * buff)
{
	TRACE_FUN(ft_t_any);

	/*  Mark sector as soft error and skip it
	 */
	if (buff->remaining > 0) {
		++buff->sector_offset;
		++buff->data_offset;
		--buff->remaining;
		buff->ptr += FT_SECTOR_SIZE;
		buff->bad_sector_map >>= 1;
	} else {
		/*  Hey, what is this????????????? C code: if we shift 
		 *  more than 31 bits, we get no shift. That's bad!!!!!!
		 */
		++buff->sector_offset;	/* hack for error maps */
		TRACE(ft_t_warn, "skipping last sector in segment");
	}
	TRACE_EXIT;
}

static void update_error_maps(buffer_struct * buff, unsigned int error_offset)
{
	int hard = 0;
	TRACE_FUN(ft_t_any);

	if (buff->retry < FT_SOFT_RETRIES) {
		buff->soft_error_map |= (1 << error_offset);
	} else {
		buff->hard_error_map |= (1 << error_offset);
		buff->soft_error_map &= ~buff->hard_error_map;
		buff->retry = -1;  /* will be set to 0 in setup_segment */
		hard = 1;
	}
	TRACE(ft_t_noise, "sector %d : %s error\n"
	      KERN_INFO "hard map: 0x%08lx\n"
	      KERN_INFO "soft map: 0x%08lx",
	      FT_SECTOR(error_offset), hard ? "hard" : "soft",
	      (long) buff->hard_error_map, (long) buff->soft_error_map);
	TRACE_EXIT;
}

static void print_progress(buffer_struct *buff, error_cause cause)
{
	TRACE_FUN(ft_t_any);

	switch (cause) {
	case no_error: 
		TRACE(ft_t_flow,"%d Sector(s) transferred", buff->sector_count);
		break;
	case no_data_error:
		TRACE(ft_t_flow, "Sector %d not found",
		      FT_SECTOR(buff->sector_offset));
		break;
	case overrun_error:
		/*  got an overrun error on the first byte, must be a
		 *  hardware problem
		 */
		TRACE(ft_t_bug,
		      "Unexpected error: failing DMA or FDC controller ?");
		break;
	case data_crc_error:
		TRACE(ft_t_flow, "Error in sector %d",
		      FT_SECTOR(buff->sector_offset - 1));
		break;
	case id_crc_error:
	case id_am_error:
	case data_am_error:
		TRACE(ft_t_flow, "Error in sector %d",
		      FT_SECTOR(buff->sector_offset));
		break;
	default:
		TRACE(ft_t_flow, "Unexpected error at sector %d",
		      FT_SECTOR(buff->sector_offset));
		break;
	}
	TRACE_EXIT;
}

/*
 *  Error cause:   Amount xferred:  Action:
 *
 *  id_am_error         0           mark bad and skip
 *  id_crc_error        0           mark bad and skip
 *  data_am_error       0           mark bad and skip
 *  data_crc_error    % 1024        mark bad and skip
 *  no_data_error       0           retry on write
 *                                  mark bad and skip on read
 *  overrun_error  [ 0..all-1 ]     mark bad and skip
 *  no_error           all          continue
 */

/*  the arg `sector' is returned by the fdc and tells us at which sector we
 *  are positioned at (relative to starting sector of segment)
 */
static void determine_verify_progress(buffer_struct *buff,
				      error_cause cause,
				      __u8 sector)
{
	TRACE_FUN(ft_t_any);

	if (cause == no_error && sector == 1) {
		buff->sector_offset = FT_SECTORS_PER_SEGMENT;
		buff->remaining     = 0;
		if (TRACE_LEVEL >= ft_t_flow) {
			print_progress(buff, cause);
		}
	} else {
		buff->sector_offset = sector - buff->sect;
		buff->remaining = FT_SECTORS_PER_SEGMENT - buff->sector_offset;
		TRACE(ft_t_noise, "%ssector offset: 0x%04x", 
		      (cause == no_error) ? "unexpected " : "",
		      buff->sector_offset);
		switch (cause) {
		case overrun_error:
			break;
#if 0
		case no_data_error:
			buff->retry = FT_SOFT_RETRIES;
			if (buff->hard_error_map    &&
			    buff->sector_offset > 1 &&
			    (buff->hard_error_map & 
			     (1 << (buff->sector_offset-2)))) {
				buff->retry --;
			}
			break;
#endif
		default:
			buff->retry = FT_SOFT_RETRIES;
			break;
		}
		if (TRACE_LEVEL >= ft_t_flow) {
			print_progress(buff, cause);
		}
		/*  Sector_offset points to the problem area Now adjust
		 *  sector_offset so it always points one past he failing
		 *  sector. I.e. skip the bad sector.
		 */
		++buff->sector_offset;
		--buff->remaining;
		update_error_maps(buff, buff->sector_offset - 1);
	}
	TRACE_EXIT;
}

static void determine_progress(buffer_struct *buff,
			       error_cause cause,
			       __u8 sector)
{
	unsigned int dma_residue;
	TRACE_FUN(ft_t_any);

	/*  Using less preferred order of disable_dma and
	 *  get_dma_residue because this seems to fail on at least one
	 *  system if reversed!
	 */
	dma_residue = get_dma_residue(fdc.dma);
	disable_dma(fdc.dma);
	if (cause != no_error || dma_residue != 0) {
		TRACE(ft_t_noise, "%sDMA residue: 0x%04x", 
		      (cause == no_error) ? "unexpected " : "",
		      dma_residue);
		/* adjust to actual value: */
		if (dma_residue == 0) {
			/* this happens sometimes with overrun errors.
			 * I don't know whether we could ignore the
			 * overrun error. Play save.
			 */
			buff->sector_count --;
		} else {
			buff->sector_count -= ((dma_residue + 
						(FT_SECTOR_SIZE - 1)) /
					       FT_SECTOR_SIZE);
		}
	}
	/*  Update var's influenced by the DMA operation.
	 */
	if (buff->sector_count > 0) {
		buff->sector_offset   += buff->sector_count;
		buff->data_offset     += buff->sector_count;
		buff->ptr             += (buff->sector_count *
					  FT_SECTOR_SIZE);
		buff->remaining       -= buff->sector_count;
		buff->bad_sector_map >>= buff->sector_count;
	}
	if (TRACE_LEVEL >= ft_t_flow) {
		print_progress(buff, cause);
	}
	if (cause != no_error) {
		if (buff->remaining == 0) {
			TRACE(ft_t_warn, "foo?\n"
			      KERN_INFO "count : %d\n"
			      KERN_INFO "offset: %d\n"
			      KERN_INFO "soft  : %08x\n"
			      KERN_INFO "hard  : %08x",
			      buff->sector_count,
			      buff->sector_offset,
			      buff->soft_error_map,
			      buff->hard_error_map);
		}
		/*  Sector_offset points to the problem area, except if we got
		 *  a data_crc_error. In that case it points one past the
		 *  failing sector.
		 *
		 *  Now adjust sector_offset so it always points one past he
		 *  failing sector. I.e. skip the bad sector.  
		 */
		if (cause != data_crc_error) {
			skip_bad_sector(buff);
		}
		update_error_maps(buff, buff->sector_offset - 1);
	}
	TRACE_EXIT;
}

static int calc_steps(int cmd)
{
	if (ftape_current_cylinder > cmd) {
		return ftape_current_cylinder - cmd;
	} else {
		return ftape_current_cylinder + cmd;
	}
}

static void pause_tape(int retry, int mode)
{
	int result;
	__u8 out[3] = {FDC_SEEK, ft_drive_sel, 0};
	TRACE_FUN(ft_t_any);

	/*  We'll use a raw seek command to get the tape to rewind and
	 *  stop for a retry.
	 */
	++ft_history.rewinds;
	if (qic117_cmds[ftape_current_command].non_intr) {
		TRACE(ft_t_warn, "motion command may be issued too soon");
	}
	if (retry && (mode == fdc_reading_data ||
		      mode == fdc_reading_id   ||
		      mode == fdc_verifying)) {
		ftape_current_command = QIC_MICRO_STEP_PAUSE;
		ftape_might_be_off_track = 1;
	} else {
		ftape_current_command = QIC_PAUSE;
	}
	out[2] = calc_steps(ftape_current_command);
	result = fdc_command(out, 3); /* issue QIC_117 command */
	ftape_current_cylinder = out[ 2];
	if (result < 0) {
		TRACE(ft_t_noise, "qic-pause failed, status = %d", result);
	} else {
		ft_location.known  = 0;
		ft_runner_status   = idle;
		ft_hide_interrupt     = 1;
		ftape_tape_running = 0;
	}
	TRACE_EXIT;
}

static void continue_xfer(buffer_struct *buff,
			  fdc_mode_enum mode, 
			  unsigned int skip)
{
	int write = 0;
 	TRACE_FUN(ft_t_any);

	if (mode == fdc_writing_data || mode == fdc_deleting) {
		write = 1;
	}
	/*  This part can be removed if it never happens
	 */
	if (skip > 0 &&
	    (ft_runner_status != running ||
	     (write && (buff->status != writing)) ||
	     (!write && (buff->status != reading && 
			 buff->status != verifying)))) {
		TRACE(ft_t_err, "unexpected runner/buffer state %d/%d",
		      ft_runner_status, buff->status);
		buff->status = error;
		/* finish this buffer: */
		(void)ftape_next_buffer(ft_queue_head);
		ft_runner_status = aborting;
		fdc_mode         = fdc_idle;
	} else if (buff->remaining > 0 && ftape_calc_next_cluster(buff) > 0) {
		/*  still sectors left in current segment, continue
		 *  with this segment
		 */
		if (fdc_setup_read_write(buff, mode) < 0) {
			/* failed, abort operation
			 */
			buff->bytes = buff->ptr - buff->address;
			buff->status = error;
			/* finish this buffer: */
			(void)ftape_next_buffer(ft_queue_head);
			ft_runner_status = aborting;
			fdc_mode         = fdc_idle;
		}
	} else {
		/* current segment completed
		 */
		unsigned int last_segment = buff->segment_id;
		int eot = ((last_segment + 1) % ft_segments_per_track) == 0;
		unsigned int next = buff->next_segment;	/* 0 means stop ! */

		buff->bytes = buff->ptr - buff->address;
		buff->status = done;
		buff = ftape_next_buffer(ft_queue_head);
		if (eot) {
			/*  finished last segment on current track,
			 *  can't continue
			 */
			ft_runner_status = logical_eot;
			fdc_mode         = fdc_idle;
			TRACE_EXIT;
		}
		if (next <= 0) {
			/*  don't continue with next segment
			 */
			TRACE(ft_t_noise, "no %s allowed, stopping tape",
			      (write) ? "write next" : "read ahead");
			pause_tape(0, mode);
			ft_runner_status = idle;  /*  not quite true until
						   *  next irq 
						   */
			TRACE_EXIT;
		}
		/*  continue with next segment
		 */
		if (buff->status != waiting) {
			TRACE(ft_t_noise, "all input buffers %s, pausing tape",
			      (write) ? "empty" : "full");
			pause_tape(0, mode);
			ft_runner_status = idle;  /*  not quite true until
						   *  next irq 
						   */
			TRACE_EXIT;
		}
		if (write && next != buff->segment_id) {
			TRACE(ft_t_noise, 
			      "segments out of order, aborting write");
			ft_runner_status = do_abort;
			fdc_mode         = fdc_idle;
			TRACE_EXIT;
		}
		ftape_setup_new_segment(buff, next, 0);
		if (stop_read_ahead) {
			buff->next_segment = 0;
			stop_read_ahead = 0;
		}
		if (ftape_calc_next_cluster(buff) == 0 ||
		    fdc_setup_read_write(buff, mode) != 0) {
			TRACE(ft_t_err, "couldn't start %s-ahead",
			      write ? "write" : "read");
			ft_runner_status = do_abort;
			fdc_mode         = fdc_idle;
		} else {
			/* keep on going */
			switch (ft_driver_state) {
			case   reading: buff->status = reading;   break;
			case verifying: buff->status = verifying; break;
			case   writing: buff->status = writing;   break;
			case  deleting: buff->status = deleting;  break;
			default:
				TRACE(ft_t_err, 
		      "BUG: ft_driver_state %d should be one out of "
		      "{reading, writing, verifying, deleting}",
				      ft_driver_state);
				buff->status = write ? writing : reading;
				break;
			}
		}
	}
	TRACE_EXIT;
}

static void retry_sector(buffer_struct *buff, 
			 int mode,
			 unsigned int skip)
{
	TRACE_FUN(ft_t_any);

	TRACE(ft_t_noise, "%s error, will retry",
	      (mode == fdc_writing_data || mode == fdc_deleting) ? "write" : "read");
	pause_tape(1, mode);
	ft_runner_status = aborting;
	buff->status     = error;
	buff->skip       = skip;
	TRACE_EXIT;
}

static unsigned int find_resume_point(buffer_struct *buff)
{
	int i = 0;
	SectorMap mask;
	SectorMap map;
	TRACE_FUN(ft_t_any);

	/*  This function is to be called after all variables have been
	 *  updated to point past the failing sector.
	 *  If there are any soft errors before the failing sector,
	 *  find the first soft error and return the sector offset.
	 *  Otherwise find the last hard error.
	 *  Note: there should always be at least one hard or soft error !
	 */
	if (buff->sector_offset < 1 || buff->sector_offset > 32) {
		TRACE(ft_t_bug, "BUG: sector_offset = %d",
		      buff->sector_offset);
		TRACE_EXIT 0;
	}
	if (buff->sector_offset >= 32) { /* C-limitation on shift ! */
		mask = 0xffffffff;
	} else {
		mask = (1 << buff->sector_offset) - 1;
	}
	map = buff->soft_error_map & mask;
	if (map) {
		while ((map & (1 << i)) == 0) {
			++i;
		}
		TRACE(ft_t_noise, "at sector %d", FT_SECTOR(i));
	} else {
		map = buff->hard_error_map & mask;
		i = buff->sector_offset - 1;
		if (map) {
			while ((map & (1 << i)) == 0) {
				--i;
			}
			TRACE(ft_t_noise, "after sector %d", FT_SECTOR(i));
			++i; /* first sector after last hard error */
		} else {
			TRACE(ft_t_bug, "BUG: no soft or hard errors");
		}
	}
	TRACE_EXIT i;
}

/*  check possible dma residue when formatting, update position record in
 *  buffer struct. This is, of course, modelled after determine_progress(), but
 *  we don't need to set up for retries because the format process cannot be
 *  interrupted (except at the end of the tape track).
 */
static int determine_fmt_progress(buffer_struct *buff, error_cause cause)
{
	unsigned int dma_residue;
	TRACE_FUN(ft_t_any);

	/*  Using less preferred order of disable_dma and
	 *  get_dma_residue because this seems to fail on at least one
	 *  system if reversed!
	 */
	dma_residue = get_dma_residue(fdc.dma);
	disable_dma(fdc.dma);
	if (cause != no_error || dma_residue != 0) {
		TRACE(ft_t_info, "DMA residue = 0x%04x", dma_residue);
		fdc_mode = fdc_idle;
		switch(cause) {
		case no_error:
			ft_runner_status = aborting;
			buff->status = idle;
			break;
		case overrun_error:
			/*  got an overrun error on the first byte, must be a
			 *  hardware problem 
			 */
			TRACE(ft_t_bug, 
			      "Unexpected error: failing DMA controller ?");
			ft_runner_status = do_abort;
			buff->status = error;
			break;
		default:
			TRACE(ft_t_noise, "Unexpected error at segment %d",
			      buff->segment_id);
			ft_runner_status = do_abort;
			buff->status = error;
			break;
		}
		TRACE_EXIT -EIO; /* can only retry entire track in format mode
				  */
	}
	/*  Update var's influenced by the DMA operation.
	 */
	buff->ptr             += FT_SECTORS_PER_SEGMENT * 4;
	buff->bytes           -= FT_SECTORS_PER_SEGMENT * 4;
	buff->remaining       -= FT_SECTORS_PER_SEGMENT;
	buff->segment_id ++; /* done with segment */
	TRACE_EXIT 0;
}

/*
 *  Continue formatting, switch buffers if there is no data left in
 *  current buffer. This is, of course, modelled after
 *  continue_xfer(), but we don't need to set up for retries because
 *  the format process cannot be interrupted (except at the end of the
 *  tape track).
 */
static void continue_formatting(buffer_struct *buff)
{
	TRACE_FUN(ft_t_any);

	if (buff->remaining <= 0) { /*  no space left in dma buffer */
		unsigned int next = buff->next_segment; 

		if (next == 0) { /* end of tape track */
			buff->status     = done;
			ft_runner_status = logical_eot;
			fdc_mode         = fdc_idle;
			TRACE(ft_t_noise, "Done formatting track %d",
			      ft_location.track);
			TRACE_EXIT;
		}
		/*
		 *  switch to next buffer!
		 */
		buff->status   = done;
		buff = ftape_next_buffer(ft_queue_head);

		if (buff->status != waiting  || next != buff->segment_id) {
			goto format_setup_error;
		}
	}
	if (fdc_setup_formatting(buff) < 0) {
		goto format_setup_error;
	}
	buff->status = formatting;
	TRACE(ft_t_fdc_dma, "Formatting segment %d on track %d",
	      buff->segment_id, ft_location.track);
	TRACE_EXIT;
 format_setup_error:
	ft_runner_status = do_abort;
	fdc_mode         = fdc_idle;
	buff->status     = error;
	TRACE(ft_t_err, "Error setting up for segment %d on track %d",
	      buff->segment_id, ft_location.track);
	TRACE_EXIT;

}

/*  this handles writing, read id, reading and formatting
 */
static void handle_fdc_busy(buffer_struct *buff)
{
	static int no_data_error_count;
	int retry = 0;
	error_cause cause;
	__u8 in[7];
	int skip;
	fdc_mode_enum fmode = fdc_mode;
	TRACE_FUN(ft_t_any);

	if (fdc_result(in, 7) < 0) { /* better get it fast ! */
		TRACE(ft_t_err, 
		      "Probably fatal error during FDC Result Phase\n"
		      KERN_INFO
		      "drive may hang until (power on) reset :-(");
		/*  what to do next ????
		 */
		TRACE_EXIT;
	}
	cause = decode_irq_cause(fdc_mode, in);
#ifdef TESTING
	{ int i;
	for (i = 0; i < (int)ft_nr_buffers; ++i)
		TRACE(ft_t_any, "buffer[%d] status: %d, segment_id: %d",
		      i, ft_buffer[i]->status, ft_buffer[i]->segment_id);
	}
#endif
	if (fmode == fdc_reading_data && ft_driver_state == verifying) {
		fmode = fdc_verifying;
	}
	switch (fmode) {
	case fdc_verifying:
		if (ft_runner_status == aborting ||
		    ft_runner_status == do_abort) {
			TRACE(ft_t_noise,"aborting %s",fdc_mode_txt(fdc_mode));
			break;
		}
		if (buff->retry > 0) {
			TRACE(ft_t_flow, "this is retry nr %d", buff->retry);
		}
		switch (cause) {
		case no_error:
			no_data_error_count = 0;
			determine_verify_progress(buff, cause, in[5]);
			if (in[2] & 0x40) {
				/*  This should not happen when verifying
				 */
				TRACE(ft_t_warn,
				      "deleted data in segment %d/%d",
				      buff->segment_id,
				      FT_SECTOR(buff->sector_offset - 1));
				buff->remaining = 0; /* abort transfer */
				buff->hard_error_map = EMPTY_SEGMENT;
				skip = 1;
			} else {
				skip = 0;
			}
			continue_xfer(buff, fdc_mode, skip);
			break;
		case no_data_error:
			no_data_error_count ++;
		case overrun_error:
			retry ++;
		case id_am_error:
		case id_crc_error:
		case data_am_error:
		case data_crc_error:
			determine_verify_progress(buff, cause, in[5]); 
			if (cause == no_data_error) {
				if (no_data_error_count >= 2) {
					TRACE(ft_t_warn,
					      "retrying because of successive "
					      "no data errors");
					no_data_error_count = 0;
				} else {
					retry --;
				}
			} else {
				no_data_error_count = 0;
			}
			if (retry) {
				skip = find_resume_point(buff);
			} else {
				skip = buff->sector_offset;
			}
			if (retry && skip < 32) {
				retry_sector(buff, fdc_mode, skip);
			} else {
				continue_xfer(buff, fdc_mode, skip);
			}
			update_history(cause);
			break;
		default:
			/*  Don't know why this could happen 
			 *  but find out.
			 */
			determine_verify_progress(buff, cause, in[5]);
			retry_sector(buff, fdc_mode, 0);
			TRACE(ft_t_err, "Error: unexpected error");
			break;
		}
		break;
	case fdc_reading_data:
#ifdef TESTING
		/* I'm sorry, but: NOBODY ever used this trace
		 * messages for ages. I guess that Bas was the last person
		 * that ever really used this (thank you, between the lines)
		 */
		if (cause == no_error) {
			TRACE(ft_t_flow,"reading segment %d",buff->segment_id);
		} else {
			TRACE(ft_t_noise, "error reading segment %d",
			      buff->segment_id);
			TRACE(ft_t_noise, "\n"
			      KERN_INFO
			     "IRQ:C: 0x%02x, H: 0x%02x, R: 0x%02x, N: 0x%02x\n"
			      KERN_INFO
			      "BUF:C: 0x%02x, H: 0x%02x, R: 0x%02x",
			      in[3], in[4], in[5], in[6],
			      buff->cyl, buff->head, buff->sect);
		}
#endif
		if (ft_runner_status == aborting ||
		    ft_runner_status == do_abort) {
			TRACE(ft_t_noise,"aborting %s",fdc_mode_txt(fdc_mode));
			break;
		}
		if (buff->bad_sector_map == FAKE_SEGMENT) {
			/* This condition occurs when reading a `fake'
			 * sector that's not accessible. Doesn't
			 * really matter as we would have ignored it
			 * anyway !
			 *
			 * Chance is that we're past the next segment
			 * now, so the next operation may fail and
			 * result in a retry.  
			 */
			buff->remaining = 0;	/* skip failing sector */
			/* buff->ptr       = buff->address; */
			/* fake success: */
			continue_xfer(buff, fdc_mode, 1);
			/*  trace calls are expensive: place them AFTER
			 *  the real stuff has been done.
			 *  
			 */
			TRACE(ft_t_noise, "skipping empty segment %d (read), size? %d",
			      buff->segment_id, buff->ptr - buff->address);
			TRACE_EXIT;
		}
		if (buff->retry > 0) {
			TRACE(ft_t_flow, "this is retry nr %d", buff->retry);
		}
		switch (cause) {
		case no_error:
			determine_progress(buff, cause, in[5]);
			if (in[2] & 0x40) {
				/*  Handle deleted data in header segments.
				 *  Skip segment and force read-ahead.
				 */
				TRACE(ft_t_warn,
				      "deleted data in segment %d/%d",
				      buff->segment_id,
				      FT_SECTOR(buff->sector_offset - 1));
				buff->deleted = 1;
				buff->remaining = 0;/*abort transfer */
				buff->soft_error_map |=
						(-1L << buff->sector_offset);
				if (buff->segment_id == 0) {
					/* stop on next segment */
					stop_read_ahead = 1;
				}
				/* force read-ahead: */
				buff->next_segment = 
					buff->segment_id + 1;
				skip = (FT_SECTORS_PER_SEGMENT - 
					buff->sector_offset);
			} else {
				skip = 0;
			}
			continue_xfer(buff, fdc_mode, skip);
			break;
		case no_data_error:
			/* Tape started too far ahead of or behind the
			 * right sector.  This may also happen in the
			 * middle of a segment !
			 *
			 * Handle no-data as soft error. If next
			 * sector fails too, a retry (with needed
			 * reposition) will follow.
			 */
			retry ++;
		case id_am_error:
		case id_crc_error:
		case data_am_error:
		case data_crc_error:
		case overrun_error:
			retry += (buff->soft_error_map != 0 ||
				  buff->hard_error_map != 0);
			determine_progress(buff, cause, in[5]); 
#if 1 || defined(TESTING)
			if (cause == overrun_error) retry ++;
#endif
			if (retry) {
				skip = find_resume_point(buff);
			} else {
				skip = buff->sector_offset;
			}
			/*  Try to resume with next sector on single
			 *  errors (let ecc correct it), but retry on
			 *  no_data (we'll be past the target when we
			 *  get here so we cannot retry) or on
			 *  multiple errors (reduce chance on ecc
			 *  failure).
			 */
			/*  cH: 23/02/97: if the last sector in the 
			 *  segment was a hard error, then there is 
			 *  no sense in a retry. This occasion seldom
			 *  occurs but ... @:³²¸`@%&§$
			 */
			if (retry && skip < 32) {
				retry_sector(buff, fdc_mode, skip);
			} else {
				continue_xfer(buff, fdc_mode, skip);
			}
			update_history(cause);
			break;
		default:
			/*  Don't know why this could happen 
			 *  but find out.
			 */
			determine_progress(buff, cause, in[5]);
			retry_sector(buff, fdc_mode, 0);
			TRACE(ft_t_err, "Error: unexpected error");
			break;
		}
		break;
	case fdc_reading_id:
		if (cause == no_error) {
			fdc_cyl = in[3];
			fdc_head = in[4];
			fdc_sect = in[5];
			TRACE(ft_t_fdc_dma,
			      "id read: C: 0x%02x, H: 0x%02x, R: 0x%02x",
			      fdc_cyl, fdc_head, fdc_sect);
		} else {	/* no valid information, use invalid sector */
			fdc_cyl = fdc_head = fdc_sect = 0;
			TRACE(ft_t_flow, "Didn't find valid sector Id");
		}
		fdc_mode = fdc_idle;
		break;
	case fdc_deleting:
	case fdc_writing_data:
#ifdef TESTING
		if (cause == no_error) {
			TRACE(ft_t_flow, "writing segment %d", buff->segment_id);
		} else {
			TRACE(ft_t_noise, "error writing segment %d",
			      buff->segment_id);
		}
#endif
		if (ft_runner_status == aborting ||
		    ft_runner_status == do_abort) {
			TRACE(ft_t_flow, "aborting %s",fdc_mode_txt(fdc_mode));
			break;
		}
		if (buff->retry > 0) {
			TRACE(ft_t_flow, "this is retry nr %d", buff->retry);
		}
		if (buff->bad_sector_map == FAKE_SEGMENT) {
			/* This condition occurs when trying to write to a
			 * `fake' sector that's not accessible. Doesn't really
			 * matter as it isn't used anyway ! Might be located
			 * at wrong segment, then we'll fail on the next
			 * segment.
			 */
			TRACE(ft_t_noise, "skipping empty segment (write)");
			buff->remaining = 0;	/* skip failing sector */
			/* fake success: */
			continue_xfer(buff, fdc_mode, 1);
			break;
		}
		switch (cause) {
		case no_error:
			determine_progress(buff, cause, in[5]);
			continue_xfer(buff, fdc_mode, 0);
			break;
		case no_data_error:
		case id_am_error:
		case id_crc_error:
		case data_am_error:
		case overrun_error:
			update_history(cause);
			determine_progress(buff, cause, in[5]);
			skip = find_resume_point(buff);
			retry_sector(buff, fdc_mode, skip);
			break;
		default:
			if (in[1] & 0x02) {
				TRACE(ft_t_err, "media not writable");
			} else {
				TRACE(ft_t_bug, "unforeseen write error");
			}
			fdc_mode = fdc_idle;
			break;
		}
		break; /* fdc_deleting || fdc_writing_data */
	case fdc_formatting:
		/*  The interrupt comes after formatting a segment. We then
		 *  have to set up QUICKLY for the next segment. But
		 *  afterwards, there is plenty of time.
		 */
		switch (cause) {
		case no_error:
			/*  would like to keep most of the formatting stuff
			 *  outside the isr code, but timing is too critical
			 */
			if (determine_fmt_progress(buff, cause) >= 0) {
				continue_formatting(buff);
			}
			break;
		case no_data_error:
		case id_am_error:
		case id_crc_error:
		case data_am_error:
		case overrun_error:
		default:
			determine_fmt_progress(buff, cause);
			update_history(cause);
			if (in[1] & 0x02) {
				TRACE(ft_t_err, "media not writable");
			} else {
				TRACE(ft_t_bug, "unforeseen write error");
			}
			break;
		} /* cause */
		break;
	default:
		TRACE(ft_t_warn, "Warning: unexpected irq during: %s",
		      fdc_mode_txt(fdc_mode));
		fdc_mode = fdc_idle;
		break;
	}
	TRACE_EXIT;
}

/*      FDC interrupt service routine.
 */
void fdc_isr(void)
{
	static int isr_active;
#ifdef TESTING
	unsigned int t0 = ftape_timestamp();
#endif
	TRACE_FUN(ft_t_any);

 	if (isr_active++) {
		--isr_active;
		TRACE(ft_t_bug, "BUG: nested interrupt, not good !");
		*fdc.hook = fdc_isr; /*  hook our handler into the fdc
				      *  code again 
				      */
		TRACE_EXIT;
	}
	sti();
	if (inb_p(fdc.msr) & FDC_BUSY) {	/*  Entering Result Phase */
		ft_hide_interrupt = 0;
		handle_fdc_busy(ftape_get_buffer(ft_queue_head));
		if (ft_runner_status == do_abort) {
			/*      cease operation, remember tape position
			 */
			TRACE(ft_t_flow, "runner aborting");
			ft_runner_status = aborting;
			++ft_expected_stray_interrupts;
		}
	} else { /* !FDC_BUSY */
		/*  clear interrupt, cause should be gotten by issuing
		 *  a Sense Interrupt Status command.
		 */
		if (fdc_mode == fdc_recalibrating || fdc_mode == fdc_seeking) {
			if (ft_hide_interrupt) {
				int st0;
				int pcn;

				if (fdc_sense_interrupt_status(&st0, &pcn) < 0)
					TRACE(ft_t_err,
					      "sense interrupt status failed");
				ftape_current_cylinder = pcn;
				TRACE(ft_t_flow, "handled hidden interrupt");
			}
			ft_seek_completed = 1;
			fdc_mode = fdc_idle;
		} else if (!waitqueue_active(&ftape_wait_intr)) {
			if (ft_expected_stray_interrupts == 0) {
				TRACE(ft_t_warn, "unexpected stray interrupt");
			} else {
				TRACE(ft_t_flow, "expected stray interrupt");
				--ft_expected_stray_interrupts;
			}
		} else {
			if (fdc_mode == fdc_reading_data ||
			    fdc_mode == fdc_verifying    ||
			    fdc_mode == fdc_writing_data ||
			    fdc_mode == fdc_deleting     ||
			    fdc_mode == fdc_formatting   ||
			    fdc_mode == fdc_reading_id) {
				if (inb_p(fdc.msr) & FDC_BUSY) {
					TRACE(ft_t_bug,
					"***** FDC failure, busy too late");
				} else {
					TRACE(ft_t_bug,
					      "***** FDC failure, no busy");
				}
			} else {
				TRACE(ft_t_fdc_dma, "awaited stray interrupt");
			}
		}
		ft_hide_interrupt = 0;
	}
	/*    Handle sleep code.
	 */
	if (!ft_hide_interrupt) {
		ft_interrupt_seen ++;
		if (waitqueue_active(&ftape_wait_intr)) {
			wake_up_interruptible(&ftape_wait_intr);
		}
	} else {
		TRACE(ft_t_flow, "hiding interrupt while %s", 
		      waitqueue_active(&ftape_wait_intr) ? "waiting":"active");
	}
#ifdef TESTING
	t0 = ftape_timediff(t0, ftape_timestamp());
	if (t0 >= 1000) {
		/* only tell us about long calls */
		TRACE(ft_t_noise, "isr() duration: %5d usec", t0);
	}
#endif
	*fdc.hook = fdc_isr;	/* hook our handler into the fdc code again */
	--isr_active;
	TRACE_EXIT;
}
