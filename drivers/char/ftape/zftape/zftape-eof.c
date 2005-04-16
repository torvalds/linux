/*
 *   I use these routines just to decide when I have to fake a 
 *   volume-table to preserve compatibility to original ftape.
 */
/*
 *      Copyright (C) 1994-1995 Bas Laarhoven.
 *      
 *      Modified for zftape 1996, 1997 Claus Heine.

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

 * $Source: /homes/cvs/ftape-stacked/ftape/zftape/zftape-eof.c,v $
 * $Revision: 1.2 $
 * $Date: 1997/10/05 19:19:02 $
 *
 *      This file contains the eof mark handling code
 *      for the QIC-40/80 floppy-tape driver for Linux.
 */

#include <linux/string.h>
#include <linux/errno.h>

#include <linux/zftape.h>

#include "../zftape/zftape-init.h"
#include "../zftape/zftape-rw.h"
#include "../zftape/zftape-eof.h"

/*      Global vars.
 */

/* a copy of the failed sector log from the header segment.
 */
eof_mark_union *zft_eof_map;

/* number of eof marks (entries in bad sector log) on tape.
 */
int zft_nr_eof_marks = -1;


/*      Local vars.
 */

static char linux_tape_label[] = "Linux raw format V";
enum { 
	min_fmt_version = 1, max_fmt_version = 2 
};
static unsigned ftape_fmt_version = 0;


/* Ftape (mis)uses the bad sector log to record end-of-file marks.
 * Initially (when the tape is erased) all entries in the bad sector
 * log are added to the tape's bad sector map. The bad sector log then
 * is cleared.
 *
 * The bad sector log normally contains entries of the form: 
 * even 16-bit word: segment number of bad sector 
 * odd 16-bit word: encoded date
 * There can be a total of 448 entries (1792 bytes).
 *
 * My guess is that no program is using this bad sector log (the *
 * format seems useless as there is no indication of the bad sector
 * itself, only the segment) However, if any program does use the bad
 * sector log, the format used by ftape will let the program think
 * there are some bad sectors and no harm is done.
 *  
 * The eof mark entries that ftape stores in the bad sector log: even
 * 16-bit word: segment number of eof mark odd 16-bit word: sector
 * number of eof mark [1..32]
 *  
 * The zft_eof_map as maintained is a sorted list of eof mark entries.
 *
 *
 * The tape name field in the header segments is used to store a linux
 * tape identification string and a version number.  This way the tape
 * can be recognized as a Linux raw format tape when using tools under
 * other OS's.
 *
 * 'Wide' QIC tapes (format code 4) don't have a failed sector list
 * anymore. That space is used for the (longer) bad sector map that
 * now is a variable length list too.  We now store our end-of-file
 * marker list after the bad-sector-map on tape. The list is delimited
 * by a (__u32) 0 entry.
 */

int zft_ftape_validate_label(char *label)
{
	static char tmp_label[45];
	int result = 0;
	TRACE_FUN(ft_t_any);
	
	memcpy(tmp_label, label, FT_LABEL_SZ);
	tmp_label[FT_LABEL_SZ] = '\0';
	TRACE(ft_t_noise, "tape  label = `%s'", tmp_label);
	ftape_fmt_version = 0;
	if (memcmp(label, linux_tape_label, strlen(linux_tape_label)) == 0) {
		int pos = strlen(linux_tape_label);
		while (label[pos] >= '0' && label[pos] <= '9') {
			ftape_fmt_version *= 10;
			ftape_fmt_version = label[ pos++] - '0';
		}
		result = (ftape_fmt_version >= min_fmt_version &&
			  ftape_fmt_version <= max_fmt_version);
	}
	TRACE(ft_t_noise, "format version = %d", ftape_fmt_version);
	TRACE_EXIT result;
}

static __u8 * find_end_of_eof_list(__u8 * ptr, __u8 * limit)
{
	while (ptr + 3 < limit) {

		if (get_unaligned((__u32*)ptr)) {
			ptr += sizeof(__u32);
		} else {
			return ptr;
		}
	}
	return NULL;
}

void zft_ftape_extract_file_marks(__u8* address)
{
	int i;
	TRACE_FUN(ft_t_any);
	
	zft_eof_map = NULL;
	if (ft_format_code == fmt_var || ft_format_code == fmt_big) {
		__u8* end;
		__u8* start = ftape_find_end_of_bsm_list(address);

		zft_nr_eof_marks = 0;
		if (start) {
			start += 3; /* skip end of list mark */
			end = find_end_of_eof_list(start, 
						   address + FT_SEGMENT_SIZE);
			if (end && end - start <= FT_FSL_SIZE) {
				zft_nr_eof_marks = ((end - start) / 
						    sizeof(eof_mark_union));
				zft_eof_map = (eof_mark_union *)start;
			} else {
				TRACE(ft_t_err,
				      "EOF Mark List is too long or damaged!");
			}
		} else {
			TRACE(ft_t_err, 
			      "Bad Sector List is too long or damaged !");
		}
	} else {
		zft_eof_map = (eof_mark_union *)&address[FT_FSL];
		zft_nr_eof_marks = GET2(address, FT_FSL_CNT);
	}
	TRACE(ft_t_noise, "number of file marks: %d", zft_nr_eof_marks);
	if (ftape_fmt_version == 1) {
		TRACE(ft_t_info, "swapping version 1 fields");
		/* version 1 format uses swapped sector and segment
		 * fields, correct that !  
		 */
		for (i = 0; i < zft_nr_eof_marks; ++i) {
			__u16 tmp = GET2(&zft_eof_map[i].mark.segment,0);
			PUT2(&zft_eof_map[i].mark.segment, 0, 
			     GET2(&zft_eof_map[i].mark.date,0));
			PUT2(&zft_eof_map[i].mark.date, 0, tmp);
		}
	}
	for (i = 0; i < zft_nr_eof_marks; ++i) {
		TRACE(ft_t_noise, "eof mark: %5d/%2d",
			GET2(&zft_eof_map[i].mark.segment, 0), 
			GET2(&zft_eof_map[i].mark.date,0));
	}
	TRACE_EXIT;
}

void zft_clear_ftape_file_marks(void)
{
	TRACE_FUN(ft_t_flow);
	/*  Clear failed sector log: remove all tape marks. We
	 *  don't use old ftape-style EOF-marks.
	 */
	TRACE(ft_t_info, "Clearing old ftape's eof map");
	memset(zft_eof_map, 0, zft_nr_eof_marks * sizeof(__u32));
	zft_nr_eof_marks = 0;
	PUT2(zft_hseg_buf, FT_FSL_CNT, 0); /* nr of eof-marks */
	zft_header_changed = 1;
	zft_update_label(zft_hseg_buf);
	TRACE_EXIT;
}
