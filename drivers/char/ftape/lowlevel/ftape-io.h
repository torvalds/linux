#ifndef _FTAPE_IO_H
#define _FTAPE_IO_H

/*
 * Copyright (C) 1993-1996 Bas Laarhoven,
 *           (C) 1997      Claus-Justus Heine.

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
 * $Source: /homes/cvs/ftape-stacked/ftape/lowlevel/ftape-io.h,v $
 * $Revision: 1.2 $
 * $Date: 1997/10/05 19:18:18 $
 *
 *      This file contains definitions for the glue part of the
 *      QIC-40/80/3010/3020 floppy-tape driver "ftape" for Linux.
 */

#include <linux/qic117.h>
#include <linux/ftape-vendors.h>

typedef struct {
	unsigned int seek;
	unsigned int reset;
	unsigned int rewind;
	unsigned int head_seek;
	unsigned int stop;
	unsigned int pause;
} ft_timeout_table;

typedef enum {
	prehistoric, pre_qic117c, post_qic117b, post_qic117d 
} qic_model;

/*
 *      ftape-io.c defined global vars.
 */
extern ft_timeout_table ftape_timeout;
extern unsigned int ftape_tape_len;
extern volatile qic117_cmd_t ftape_current_command;
extern const struct qic117_command_table qic117_cmds[];
extern int ftape_might_be_off_track;

/*
 *      ftape-io.c defined global functions.
 */
extern void ftape_udelay(unsigned int usecs);
extern void  ftape_udelay_calibrate(void);
extern void ftape_sleep(unsigned int time);
extern void ftape_report_vendor_id(unsigned int *id);
extern int  ftape_command(qic117_cmd_t command);
extern int  ftape_command_wait(qic117_cmd_t command,
			       unsigned int timeout,
			       int *status);
extern int  ftape_parameter(unsigned int parameter);
extern int ftape_report_operation(int *status,
				  qic117_cmd_t  command,
				  int result_length);
extern int ftape_report_configuration(qic_model *model,
				      unsigned int *rate,
				      int *qic_std,
				      int *tape_len);
extern int ftape_report_drive_status(int *status);
extern int ftape_report_raw_drive_status(int *status);
extern int ftape_report_status(int *status);
extern int ftape_ready_wait(unsigned int timeout, int *status);
extern int ftape_seek_head_to_track(unsigned int track);
extern int ftape_set_data_rate(unsigned int new_rate, unsigned int qic_std);
extern int ftape_report_error(unsigned int *error,
			      qic117_cmd_t *command,
			      int report);
extern int ftape_reset_drive(void);
extern int ftape_put_drive_to_sleep(wake_up_types method);
extern int ftape_wakeup_drive(wake_up_types method);
extern int ftape_increase_threshold(void);
extern int ftape_half_data_rate(void);

#endif
