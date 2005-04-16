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
 * $Source: /homes/cvs/ftape-stacked/ftape/lowlevel/ftape-read.c,v $
 * $Revision: 1.6 $
 * $Date: 1997/10/21 14:39:22 $
 *
 *      This file contains the reading code
 *      for the QIC-117 floppy-tape driver for Linux.
 *
 */

#include <linux/string.h>
#include <linux/errno.h>
#include <linux/mm.h>

#include <linux/ftape.h>
#include <linux/qic117.h>
#include "../lowlevel/ftape-tracing.h"
#include "../lowlevel/ftape-read.h"
#include "../lowlevel/ftape-io.h"
#include "../lowlevel/ftape-ctl.h"
#include "../lowlevel/ftape-rw.h"
#include "../lowlevel/ftape-write.h"
#include "../lowlevel/ftape-ecc.h"
#include "../lowlevel/ftape-bsm.h"

/*      Global vars.
 */

/*      Local vars.
 */

void ftape_zap_read_buffers(void)
{
	int i;

	for (i = 0; i < ft_nr_buffers; ++i) {
/*  changed to "fit" with dynamic allocation of tape_buffer. --khp  */
		ft_buffer[i]->status = waiting;
		ft_buffer[i]->bytes = 0;
		ft_buffer[i]->skip = 0;
		ft_buffer[i]->retry = 0;
	}
/*	ftape_reset_buffer(); */
}

static SectorMap convert_sector_map(buffer_struct * buff)
{
	int i = 0;
	SectorMap bad_map = ftape_get_bad_sector_entry(buff->segment_id);
	SectorMap src_map = buff->soft_error_map | buff->hard_error_map;
	SectorMap dst_map = 0;
	TRACE_FUN(ft_t_any);

	if (bad_map || src_map) {
		TRACE(ft_t_flow, "bad_map = 0x%08lx", (long) bad_map);
		TRACE(ft_t_flow, "src_map = 0x%08lx", (long) src_map);
	}
	while (bad_map) {
		while ((bad_map & 1) == 0) {
			if (src_map & 1) {
				dst_map |= (1 << i);
			}
			src_map >>= 1;
			bad_map >>= 1;
			++i;
		}
		/* (bad_map & 1) == 1 */
		src_map >>= 1;
		bad_map >>= 1;
	}
	if (src_map) {
		dst_map |= (src_map << i);
	}
	if (dst_map) {
		TRACE(ft_t_flow, "dst_map = 0x%08lx", (long) dst_map);
	}
	TRACE_EXIT dst_map;
}

static int correct_and_copy_fraction(buffer_struct *buff, __u8 * destination,
				     int start, int size)
{
	struct memory_segment mseg;
	int result;
	SectorMap read_bad;
	TRACE_FUN(ft_t_any);

	mseg.read_bad = convert_sector_map(buff);
	mseg.marked_bad = 0;	/* not used... */
	mseg.blocks = buff->bytes / FT_SECTOR_SIZE;
	mseg.data = buff->address;
	/*    If there are no data sectors we can skip this segment.
	 */
	if (mseg.blocks <= 3) {
		TRACE_ABORT(0, ft_t_noise, "empty segment");
	}
	read_bad = mseg.read_bad;
	ft_history.crc_errors += count_ones(read_bad);
	result = ftape_ecc_correct_data(&mseg);
	if (read_bad != 0 || mseg.corrected != 0) {
		TRACE(ft_t_noise, "crc error map: 0x%08lx", (unsigned long)read_bad);
		TRACE(ft_t_noise, "corrected map: 0x%08lx", (unsigned long)mseg.corrected);
		ft_history.corrected += count_ones(mseg.corrected);
	}
	if (result == ECC_CORRECTED || result == ECC_OK) {
		if (result == ECC_CORRECTED) {
			TRACE(ft_t_info, "ecc corrected segment: %d", buff->segment_id);
		}
		if(start < 0) {
			start= 0;
		}
		if((start+size) > ((mseg.blocks - 3) * FT_SECTOR_SIZE)) {
			size = (mseg.blocks - 3) * FT_SECTOR_SIZE  - start;
		} 
		if (size < 0) {
			size= 0;
		}
		if(size > 0) {
			memcpy(destination + start, mseg.data + start, size);
		}
		if ((read_bad ^ mseg.corrected) & mseg.corrected) {
			/* sectors corrected without crc errors set */
			ft_history.crc_failures++;
		}
		TRACE_EXIT size; /* (mseg.blocks - 3) * FT_SECTOR_SIZE; */
	} else {
		ft_history.ecc_failures++;
		TRACE_ABORT(-EAGAIN,
			    ft_t_err, "ecc failure on segment %d",
			    buff->segment_id);
	}
	TRACE_EXIT 0;
}

/*      Read given segment into buffer at address.
 */
int ftape_read_segment_fraction(const int segment_id,
				void  *address, 
				const ft_read_mode_t read_mode,
				const int start,
				const int size)
{
	int result = 0;
	int retry  = 0;
	int bytes_read = 0;
	int read_done  = 0;
	TRACE_FUN(ft_t_flow);

	ft_history.used |= 1;
	TRACE(ft_t_data_flow, "segment_id = %d", segment_id);
	if (ft_driver_state != reading) {
		TRACE(ft_t_noise, "calling ftape_abort_operation");
		TRACE_CATCH(ftape_abort_operation(),);
		ftape_set_state(reading);
	}
	for(;;) {
		buffer_struct *tail;
		/*  Allow escape from this loop on signal !
		 */
		FT_SIGNAL_EXIT(_DONT_BLOCK);
		/*  Search all full buffers for the first matching the
		 *  wanted segment.  Clear other buffers on the fly.
		 */
		tail = ftape_get_buffer(ft_queue_tail);
		while (!read_done && tail->status == done) {
			/*  Allow escape from this loop on signal !
			 */
			FT_SIGNAL_EXIT(_DONT_BLOCK);
			if (tail->segment_id == segment_id) {
				/*  If out buffer is already full,
				 *  return its contents.  
				 */
				TRACE(ft_t_flow, "found segment in cache: %d",
				      segment_id);
				if (tail->deleted) {
					/*  Return a value that
					 *  read_header_segment
					 *  understands.  As this
					 *  should only occur when
					 *  searching for the header
					 *  segments it shouldn't be
					 *  misinterpreted elsewhere.
					 */
					TRACE_EXIT 0;
				}
				result = correct_and_copy_fraction(
					tail,
					address,
					start,
					size);
				TRACE(ft_t_flow, "segment contains (bytes): %d",
				      result);
				if (result < 0) {
					if (result != -EAGAIN) {
						TRACE_EXIT result;
					}
					/* keep read_done == 0, will
					 * trigger
					 * ftape_abort_operation
					 * because reading wrong
					 * segment.
					 */
					TRACE(ft_t_err, "ecc failed, retry");
					++retry;
				} else {
					read_done = 1;
					bytes_read = result;
				}
			} else {
				TRACE(ft_t_flow,"zapping segment in cache: %d",
				      tail->segment_id);
			}
			tail->status = waiting;
			tail = ftape_next_buffer(ft_queue_tail);
		}
		if (!read_done && tail->status == reading) {
			if (tail->segment_id == segment_id) {
				switch(ftape_wait_segment(reading)) {
				case 0:
					break;
				case -EINTR:
					TRACE_ABORT(-EINTR, ft_t_warn,
						    "interrupted by "
						    "non-blockable signal");
					break;
				default:
					TRACE(ft_t_noise,
					      "wait_segment failed");
					ftape_abort_operation();
					ftape_set_state(reading);
					break;
				}
			} else {
				/*  We're reading the wrong segment,
				 *  stop runner.
				 */
				TRACE(ft_t_noise, "reading wrong segment");
				ftape_abort_operation();
				ftape_set_state(reading);
			}
		}
		/*    should runner stop ?
		 */
		if (ft_runner_status == aborting) {
			buffer_struct *head = ftape_get_buffer(ft_queue_head);
			switch(head->status) {
			case error:
				ft_history.defects += 
					count_ones(head->hard_error_map);
			case reading:
				head->status = waiting;
				break;
			default:
				break;
			}
			TRACE_CATCH(ftape_dumb_stop(),);
		} else {
			/*  If just passed last segment on tape: wait
			 *  for BOT or EOT mark. Sets ft_runner_status to
			 *  idle if at lEOT and successful 
			 */
			TRACE_CATCH(ftape_handle_logical_eot(),);
		}
		/*    If we got a segment: quit, or else retry up to limit.
		 *
		 *    If segment to read is empty, do not start runner for it,
		 *    but wait for next read call.
		 */
		if (read_done ||
		    ftape_get_bad_sector_entry(segment_id) == EMPTY_SEGMENT ) {
			/* bytes_read = 0;  should still be zero */
			TRACE_EXIT bytes_read;

		}
		if (retry > FT_RETRIES_ON_ECC_ERROR) {
			ft_history.defects++;
			TRACE_ABORT(-ENODATA, ft_t_err,
				    "too many retries on ecc failure");
		}
		/*    Now at least one buffer is empty !
		 *    Restart runner & tape if needed.
		 */
		TRACE(ft_t_any, "head: %d, tail: %d, ft_runner_status: %d",
		      ftape_buffer_id(ft_queue_head),
		      ftape_buffer_id(ft_queue_tail),
		      ft_runner_status);
		TRACE(ft_t_any, "buffer[].status, [head]: %d, [tail]: %d",
		      ftape_get_buffer(ft_queue_head)->status,
		      ftape_get_buffer(ft_queue_tail)->status);
		tail = ftape_get_buffer(ft_queue_tail);
		if (tail->status == waiting) {
			buffer_struct *head = ftape_get_buffer(ft_queue_head);

			ftape_setup_new_segment(head, segment_id, -1);
			if (read_mode == FT_RD_SINGLE) {
				/* disable read-ahead */
				head->next_segment = 0;
			}
			ftape_calc_next_cluster(head);
			if (ft_runner_status == idle) {
				result = ftape_start_tape(segment_id,
							  head->sector_offset);
				if (result < 0) {
					TRACE_ABORT(result, ft_t_err, "Error: "
						    "segment %d unreachable",
						    segment_id);
				}
			}
			head->status = reading;
			fdc_setup_read_write(head, FDC_READ);
		}
	}
	/* not reached */
	TRACE_EXIT -EIO;
}

int ftape_read_header_segment(__u8 *address)
{
	int result;
	int header_segment;
	int first_failed = 0;
	int status;
	TRACE_FUN(ft_t_flow);

	ft_used_header_segment = -1;
	TRACE_CATCH(ftape_report_drive_status(&status),);
	TRACE(ft_t_flow, "reading...");
	/*  We're looking for the first header segment.
	 *  A header segment cannot contain bad sectors, therefor at the
	 *  tape start, segments with bad sectors are (according to QIC-40/80)
	 *  written with deleted data marks and must be skipped.
	 */
	memset(address, '\0', (FT_SECTORS_PER_SEGMENT - 3) * FT_SECTOR_SIZE); 
	result = 0;
#define HEADER_SEGMENT_BOUNDARY 68  /* why not 42? */
	for (header_segment = 0;
	     header_segment < HEADER_SEGMENT_BOUNDARY && result == 0;
	     ++header_segment) {
		/*  Set no read-ahead, the isr will force read-ahead whenever
		 *  it encounters deleted data !
		 */
		result = ftape_read_segment(header_segment,
					    address,
					    FT_RD_SINGLE);
		if (result < 0 && !first_failed) {
			TRACE(ft_t_err, "header segment damaged, trying backup");
			first_failed = 1;
			result = 0;	/* force read of next (backup) segment */
		}
	}
	if (result < 0 || header_segment >= HEADER_SEGMENT_BOUNDARY) {
		TRACE_ABORT(-EIO, ft_t_err,
			    "no readable header segment found");
	}
	TRACE_CATCH(ftape_abort_operation(),);
	ft_used_header_segment = header_segment;
	result = ftape_decode_header_segment(address);
 	TRACE_EXIT result;
}

int ftape_decode_header_segment(__u8 *address)
{
	unsigned int max_floppy_side;
	unsigned int max_floppy_track;
	unsigned int max_floppy_sector;
	unsigned int new_tape_len;
	TRACE_FUN(ft_t_flow);

	if (GET4(address, FT_SIGNATURE) == FT_D2G_MAGIC) {
		/* Ditto 2GB header segment. They encrypt the bad sector map.
		 * We decrypt it and store them in normal format.
		 * I hope this is correct.
		 */
		int i;
		TRACE(ft_t_warn,
		      "Found Ditto 2GB tape, "
		      "trying to decrypt bad sector map");
		for (i=256; i < 29 * FT_SECTOR_SIZE; i++) {
			address[i] = ~(address[i] - (i&0xff));
		}
		PUT4(address, 0,FT_HSEG_MAGIC);
	} else if (GET4(address, FT_SIGNATURE) != FT_HSEG_MAGIC) {
		TRACE_ABORT(-EIO, ft_t_err,
			    "wrong signature in header segment");
	}
	ft_format_code = (ft_format_type) address[FT_FMT_CODE];
	if (ft_format_code != fmt_big) {
		ft_header_segment_1   = GET2(address, FT_HSEG_1);
		ft_header_segment_2   = GET2(address, FT_HSEG_2);
		ft_first_data_segment = GET2(address, FT_FRST_SEG);
		ft_last_data_segment  = GET2(address, FT_LAST_SEG);
	} else {
		ft_header_segment_1   = GET4(address, FT_6_HSEG_1);
		ft_header_segment_2   = GET4(address, FT_6_HSEG_2);
		ft_first_data_segment = GET4(address, FT_6_FRST_SEG);
		ft_last_data_segment  = GET4(address, FT_6_LAST_SEG);
	}
	TRACE(ft_t_noise, "first data segment: %d", ft_first_data_segment);
	TRACE(ft_t_noise, "last  data segment: %d", ft_last_data_segment);
	TRACE(ft_t_noise, "header segments are %d and %d",
	      ft_header_segment_1, ft_header_segment_2);

	/*    Verify tape parameters...
	 *    QIC-40/80 spec:                 tape_parameters:
	 *
	 *    segments-per-track              segments_per_track
	 *    tracks-per-cartridge            tracks_per_tape
	 *    max-floppy-side                 (segments_per_track *
	 *                                    tracks_per_tape - 1) /
	 *                                    ftape_segments_per_head
	 *    max-floppy-track                ftape_segments_per_head /
	 *                                    ftape_segments_per_cylinder - 1
	 *    max-floppy-sector               ftape_segments_per_cylinder *
	 *                                    FT_SECTORS_PER_SEGMENT
	 */
	ft_segments_per_track = GET2(address, FT_SPT);
	ft_tracks_per_tape    = address[FT_TPC];
	max_floppy_side       = address[FT_FHM];
	max_floppy_track      = address[FT_FTM];
	max_floppy_sector     = address[FT_FSM];
	TRACE(ft_t_noise, "(fmt/spt/tpc/fhm/ftm/fsm) = %d/%d/%d/%d/%d/%d",
	      ft_format_code, ft_segments_per_track, ft_tracks_per_tape,
	      max_floppy_side, max_floppy_track, max_floppy_sector);
	new_tape_len = ftape_tape_len;
	switch (ft_format_code) {
	case fmt_425ft:
		new_tape_len = 425;
		break;
	case fmt_normal:
		if (ftape_tape_len == 0) {	/* otherwise 307 ft */
			new_tape_len = 205;
		}
		break;
	case fmt_1100ft:
		new_tape_len = 1100;
		break;
	case fmt_var:{
			int segments_per_1000_inch = 1;		/* non-zero default for switch */
			switch (ft_qic_std) {
			case QIC_TAPE_QIC40:
				segments_per_1000_inch = 332;
				break;
			case QIC_TAPE_QIC80:
				segments_per_1000_inch = 488;
				break;
			case QIC_TAPE_QIC3010:
				segments_per_1000_inch = 730;
				break;
			case QIC_TAPE_QIC3020:
				segments_per_1000_inch = 1430;
				break;
			}
			new_tape_len = (1000 * ft_segments_per_track +
					(segments_per_1000_inch - 1)) / segments_per_1000_inch;
			break;
		}
	case fmt_big:{
			int segments_per_1000_inch = 1;		/* non-zero default for switch */
			switch (ft_qic_std) {
			case QIC_TAPE_QIC40:
				segments_per_1000_inch = 332;
				break;
			case QIC_TAPE_QIC80:
				segments_per_1000_inch = 488;
				break;
			case QIC_TAPE_QIC3010:
				segments_per_1000_inch = 730;
				break;
			case QIC_TAPE_QIC3020:
				segments_per_1000_inch = 1430;
				break;
			default:
				TRACE_ABORT(-EIO, ft_t_bug,
			"%x QIC-standard with fmt-code %d, please report",
					    ft_qic_std, ft_format_code);
			}
			new_tape_len = ((1000 * ft_segments_per_track +
					 (segments_per_1000_inch - 1)) / 
					segments_per_1000_inch);
			break;
		}
	default:
		TRACE_ABORT(-EIO, ft_t_err,
			    "unknown tape format, please report !");
	}
	if (new_tape_len != ftape_tape_len) {
		ftape_tape_len = new_tape_len;
		TRACE(ft_t_info, "calculated tape length is %d ft",
		      ftape_tape_len);
		ftape_calc_timeouts(ft_qic_std, ft_data_rate, ftape_tape_len);
	}
	if (ft_segments_per_track == 0 && ft_tracks_per_tape == 0 &&
	    max_floppy_side == 0 && max_floppy_track == 0 &&
	    max_floppy_sector == 0) {
		/*  QIC-40 Rev E and earlier has no values in the header.
		 */
		ft_segments_per_track = 68;
		ft_tracks_per_tape = 20;
		max_floppy_side = 1;
		max_floppy_track = 169;
		max_floppy_sector = 128;
	}
	/*  This test will compensate for the wrong parameter on tapes
	 *  formatted by Conner software.
	 */
	if (ft_segments_per_track == 150 &&
	    ft_tracks_per_tape == 28 &&
	    max_floppy_side == 7 &&
	    max_floppy_track == 149 &&
	    max_floppy_sector == 128) {
TRACE(ft_t_info, "the famous CONNER bug: max_floppy_side off by one !");
		max_floppy_side = 6;
	}
	/*  These tests will compensate for the wrong parameter on tapes
	 *  formatted by ComByte Windows software.
	 *
	 *  First, for 205 foot tapes
	 */
	if (ft_segments_per_track == 100 &&
	    ft_tracks_per_tape == 28 &&
	    max_floppy_side == 9 &&
	    max_floppy_track == 149 &&
	    max_floppy_sector == 128) {
TRACE(ft_t_info, "the ComByte bug: max_floppy_side incorrect!");
		max_floppy_side = 4;
	}
	/* Next, for 307 foot tapes. */
	if (ft_segments_per_track == 150 &&
	    ft_tracks_per_tape == 28 &&
	    max_floppy_side == 9 &&
	    max_floppy_track == 149 &&
	    max_floppy_sector == 128) {
TRACE(ft_t_info, "the ComByte bug: max_floppy_side incorrect!");
		max_floppy_side = 6;
	}
	/*  This test will compensate for the wrong parameter on tapes
	 *  formatted by Colorado Windows software.
	 */
	if (ft_segments_per_track == 150 &&
	    ft_tracks_per_tape == 28 &&
	    max_floppy_side == 6 &&
	    max_floppy_track == 150 &&
	    max_floppy_sector == 128) {
TRACE(ft_t_info, "the famous Colorado bug: max_floppy_track off by one !");
		max_floppy_track = 149;
	}
	ftape_segments_per_head = ((max_floppy_sector/FT_SECTORS_PER_SEGMENT) *
				   (max_floppy_track + 1));
	/*  This test will compensate for some bug reported by Dima
	 *  Brodsky.  Seems to be a Colorado bug, either. (freebee
	 *  Imation tape shipped together with Colorado T3000
	 */
	if ((ft_format_code == fmt_var || ft_format_code == fmt_big) &&
	    ft_tracks_per_tape == 50 &&
	    max_floppy_side == 54 &&
	    max_floppy_track == 255 &&
	    max_floppy_sector == 128) {
TRACE(ft_t_info, "the famous ??? bug: max_floppy_track off by one !");
		max_floppy_track = 254;
	}
	/*
	 *    Verify drive_configuration with tape parameters
	 */
	if (ftape_segments_per_head == 0 || ftape_segments_per_cylinder == 0 ||
	  ((ft_segments_per_track * ft_tracks_per_tape - 1) / ftape_segments_per_head
	   != max_floppy_side) ||
	    (ftape_segments_per_head / ftape_segments_per_cylinder - 1 != max_floppy_track) ||
	(ftape_segments_per_cylinder * FT_SECTORS_PER_SEGMENT != max_floppy_sector)
#ifdef TESTING
	    || ((ft_format_code == fmt_var || ft_format_code == fmt_big) && 
		(max_floppy_track != 254 || max_floppy_sector != 128))
#endif
	   ) {
		char segperheadz = ftape_segments_per_head ? ' ' : '?';
		char segpercylz  = ftape_segments_per_cylinder ? ' ' : '?';
		TRACE(ft_t_err,"Tape parameters inconsistency, please report");
		TRACE(ft_t_err, "reported = %d/%d/%d/%d/%d/%d",
		      ft_format_code,
		      ft_segments_per_track,
		      ft_tracks_per_tape,
		      max_floppy_side,
		      max_floppy_track,
		      max_floppy_sector);
		TRACE(ft_t_err, "required = %d/%d/%d/%d%c/%d%c/%d",
		      ft_format_code,
		      ft_segments_per_track,
		      ft_tracks_per_tape,
		      ftape_segments_per_head ?
		      ((ft_segments_per_track * ft_tracks_per_tape -1) / 
		       ftape_segments_per_head ) :
			(ft_segments_per_track * ft_tracks_per_tape -1),
			segperheadz,
		      ftape_segments_per_cylinder ?
		      (ftape_segments_per_head / 
		       ftape_segments_per_cylinder - 1 ) :
			ftape_segments_per_head - 1,
			segpercylz,
		      (ftape_segments_per_cylinder * FT_SECTORS_PER_SEGMENT));
		TRACE_EXIT -EIO;
	}
	ftape_extract_bad_sector_map(address);
 	TRACE_EXIT 0;
}
