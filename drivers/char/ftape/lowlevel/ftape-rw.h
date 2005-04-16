#ifndef _FTAPE_RW_H
#define _FTAPE_RW_H

/*
 * Copyright (C) 1993-1996 Bas Laarhoven,
 *           (C) 1996-1997 Claus-Justus Heine.

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
 * $Source: /homes/cvs/ftape-stacked/ftape/lowlevel/ftape-rw.h,v $
 * $Revision: 1.2 $
 * $Date: 1997/10/05 19:18:25 $
 *
 *      This file contains the definitions for the read and write
 *      functions for the QIC-117 floppy-tape driver for Linux.
 *
 * Claus-Justus Heine (1996/09/20): Add definition of format code 6
 * Claus-Justus Heine (1996/10/04): Changed GET/PUT macros to cast to (__u8 *)
 *
 */

#include "../lowlevel/fdc-io.h"
#include "../lowlevel/ftape-init.h"
#include "../lowlevel/ftape-bsm.h"

#include <asm/unaligned.h>

#define GET2(address, offset) get_unaligned((__u16*)((__u8 *)address + offset))
#define GET4(address, offset) get_unaligned((__u32*)((__u8 *)address + offset))
#define GET8(address, offset) get_unaligned((__u64*)((__u8 *)address + offset))
#define PUT2(address, offset , value) put_unaligned((value), (__u16*)((__u8 *)address + offset))
#define PUT4(address, offset , value) put_unaligned((value), (__u32*)((__u8 *)address + offset))
#define PUT8(address, offset , value) put_unaligned((value), (__u64*)((__u8 *)address + offset))

enum runner_status_enum {
	idle = 0,
	running,
	do_abort,
	aborting,
	logical_eot,
	end_of_tape,
};

typedef enum ft_buffer_queue {
	ft_queue_head = 0,
	ft_queue_tail = 1
} ft_buffer_queue_t;


typedef struct {
	int track;		/* tape head position */
	volatile int segment;	/* current segment */
	volatile int sector;	/* sector offset within current segment */
	volatile unsigned int bot;	/* logical begin of track */
	volatile unsigned int eot;	/* logical end of track */
	volatile unsigned int known;	/* validates bot, segment, sector */
} location_record;

/*      Count nr of 1's in pattern.
 */
static inline int count_ones(unsigned long mask)
{
	int bits;

	for (bits = 0; mask != 0; mask >>= 1) {
		if (mask & 1) {
			++bits;
		}
	}
	return bits;
}

#define FT_MAX_NR_BUFFERS 16 /* arbitrary value */
/*      ftape-rw.c defined global vars.
 */
extern buffer_struct *ft_buffer[FT_MAX_NR_BUFFERS];
extern int ft_nr_buffers;
extern location_record ft_location;
extern volatile int ftape_tape_running;

/*      ftape-rw.c defined global functions.
 */
extern int  ftape_setup_new_segment(buffer_struct * buff,
				    int segment_id,
				    int offset);
extern int  ftape_calc_next_cluster(buffer_struct * buff);
extern buffer_struct *ftape_next_buffer (ft_buffer_queue_t pos);
extern buffer_struct *ftape_get_buffer  (ft_buffer_queue_t pos);
extern int            ftape_buffer_id   (ft_buffer_queue_t pos);
extern void           ftape_reset_buffer(void);
extern void ftape_tape_parameters(__u8 drive_configuration);
extern int  ftape_wait_segment(buffer_state_enum state);
extern int  ftape_dumb_stop(void);
extern int  ftape_start_tape(int segment_id, int offset);
extern int  ftape_stop_tape(int *pstatus);
extern int  ftape_handle_logical_eot(void);
extern buffer_state_enum ftape_set_state(buffer_state_enum new_state);
#endif				/* _FTAPE_RW_H */
