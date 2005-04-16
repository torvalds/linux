#ifndef _FTAPE_CTL_H
#define _FTAPE_CTL_H

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
 * $Source: /homes/cvs/ftape-stacked/ftape/lowlevel/ftape-ctl.h,v $
 * $Revision: 1.2 $
 * $Date: 1997/10/05 19:18:09 $
 *
 *      This file contains the non-standard IOCTL related definitions
 *      for the QIC-40/80/3010/3020 floppy-tape driver "ftape" for
 *      Linux.
 */

#include <linux/ioctl.h>
#include <linux/mtio.h>
#include <linux/ftape-vendors.h>

#include "../lowlevel/ftape-rw.h"
#include <linux/ftape-header-segment.h>

typedef struct {
	int used;		/* any reading or writing done */
	/* isr statistics */
	unsigned int id_am_errors;	/* id address mark not found */
	unsigned int id_crc_errors;	/* crc error in id address mark */
	unsigned int data_am_errors;	/* data address mark not found */
	unsigned int data_crc_errors;	/* crc error in data field */
	unsigned int overrun_errors;	/* fdc access timing problem */
	unsigned int no_data_errors;	/* sector not found */
	unsigned int retries;	/* number of tape retries */
	/* ecc statistics */
	unsigned int crc_errors;	/* crc error in data */
	unsigned int crc_failures;	/* bad data without crc error */
	unsigned int ecc_failures;	/* failed to correct */
	unsigned int corrected;	/* total sectors corrected */
	/* general statistics */
	unsigned int rewinds;	/* number of tape rewinds */
	unsigned int defects;	/* bad sectors due to media defects */
} history_record;

/* this structure contains * ALL * information that we want
 * our child modules to know about, but don't want them to
 * modify. 
 */
typedef struct {
	/*  vendor information */
	vendor_struct fti_drive_type;
	/*  data rates */
	unsigned int fti_used_data_rate;
	unsigned int fti_drive_max_rate;
	unsigned int fti_fdc_max_rate;
	/*  drive selection, either FTAPE_SEL_A/B/C/D */
	int fti_drive_sel;      
	/*  flags set after decode the drive and tape status   */
	unsigned int fti_formatted      :1;
	unsigned int fti_no_tape        :1;
	unsigned int fti_write_protected:1;
	unsigned int fti_new_tape       :1;
	/*  values of last queried drive/tape status and error */
	ft_drive_error  fti_last_error;
	ft_drive_status fti_last_status;
	/*  cartridge geometry */
	unsigned int fti_tracks_per_tape;
	unsigned int fti_segments_per_track;
	/*  location of header segments, etc. */
	int fti_used_header_segment;
	int fti_header_segment_1;
	int fti_header_segment_2;
	int fti_first_data_segment;
	int fti_last_data_segment;
	/*  the format code as stored in the header segment  */
	ft_format_type  fti_format_code;
	/*  the following is the sole reason for the ftape_set_status() call */
	unsigned int fti_qic_std;
	/*  is tape running? */
	volatile enum runner_status_enum fti_runner_status;
	/*  is tape reading/writing/verifying/formatting/deleting */
	buffer_state_enum fti_state;
	/*  flags fatal hardware error */
	unsigned int fti_failure:1;
	/*  history record */
	history_record fti_history;
} ftape_info;

/* vendor information */
#define ft_drive_type          ftape_status.fti_drive_type
/*  data rates */
#define ft_data_rate           ftape_status.fti_used_data_rate
#define ft_drive_max_rate      ftape_status.fti_drive_max_rate
#define ft_fdc_max_rate        ftape_status.fti_fdc_max_rate
/*  drive selection, either FTAPE_SEL_A/B/C/D */
#define ft_drive_sel           ftape_status.fti_drive_sel
/*  flags set after decode the drive and tape status   */
#define ft_formatted           ftape_status.fti_formatted
#define ft_no_tape             ftape_status.fti_no_tape
#define ft_write_protected     ftape_status.fti_write_protected
#define ft_new_tape            ftape_status.fti_new_tape
/*  values of last queried drive/tape status and error */
#define ft_last_error          ftape_status.fti_last_error
#define ft_last_status         ftape_status.fti_last_status
/*  cartridge geometry */
#define ft_tracks_per_tape     ftape_status.fti_tracks_per_tape
#define ft_segments_per_track  ftape_status.fti_segments_per_track
/*  the format code as stored in the header segment  */
#define ft_format_code         ftape_status.fti_format_code
/*  the qic status as returned by report drive configuration */
#define ft_qic_std             ftape_status.fti_qic_std
#define ft_used_header_segment ftape_status.fti_used_header_segment
#define ft_header_segment_1    ftape_status.fti_header_segment_1
#define ft_header_segment_2    ftape_status.fti_header_segment_2
#define ft_first_data_segment  ftape_status.fti_first_data_segment
#define ft_last_data_segment   ftape_status.fti_last_data_segment
/*  is tape running? */
#define ft_runner_status       ftape_status.fti_runner_status
/*  is tape reading/writing/verifying/formatting/deleting */
#define ft_driver_state        ftape_status.fti_state
/*  flags fatal hardware error */
#define ft_failure             ftape_status.fti_failure
/*  history record */
#define ft_history             ftape_status.fti_history

/*
 *      ftape-ctl.c defined global vars.
 */
extern ftape_info ftape_status;
extern int ftape_segments_per_head;
extern int ftape_segments_per_cylinder;
extern int ftape_init_drive_needed;

/*
 *      ftape-ctl.c defined global functions.
 */
extern int  ftape_mmap(struct vm_area_struct *vma);
extern int  ftape_enable(int drive_selection);
extern void ftape_disable(void);
extern int  ftape_seek_to_bot(void);
extern int  ftape_seek_to_eot(void);
extern int  ftape_abort_operation(void);
extern void ftape_calc_timeouts(unsigned int qic_std,
				 unsigned int data_rate,
				 unsigned int tape_len);
extern int  ftape_calibrate_data_rate(unsigned int qic_std);
extern const ftape_info *ftape_get_status(void);
#endif
