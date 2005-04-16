/*
 *      Copyright (C) 1996-1997 Claus-Justus Heine

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
 * $Source: /homes/cvs/ftape-stacked/ftape/lowlevel/ftape_syms.c,v $
 * $Revision: 1.4 $
 * $Date: 1997/10/17 00:03:51 $
 *
 *      This file contains the symbols that the ftape low level
 *      part of the QIC-40/80/3010/3020 floppy-tape driver "ftape"
 *      exports to its high level clients
 */

#include <linux/config.h>
#include <linux/module.h>

#include <linux/ftape.h>
#include "../lowlevel/ftape-tracing.h"
#include "../lowlevel/ftape-init.h"
#include "../lowlevel/fdc-io.h"
#include "../lowlevel/ftape-read.h"
#include "../lowlevel/ftape-write.h"
#include "../lowlevel/ftape-io.h"
#include "../lowlevel/ftape-ctl.h"
#include "../lowlevel/ftape-rw.h"
#include "../lowlevel/ftape-bsm.h"
#include "../lowlevel/ftape-buffer.h"
#include "../lowlevel/ftape-format.h"

/* bad sector handling from ftape-bsm.c */
EXPORT_SYMBOL(ftape_get_bad_sector_entry);
EXPORT_SYMBOL(ftape_find_end_of_bsm_list);
/* from ftape-rw.c */
EXPORT_SYMBOL(ftape_set_state);
/* from ftape-ctl.c */
EXPORT_SYMBOL(ftape_seek_to_bot);
EXPORT_SYMBOL(ftape_seek_to_eot);
EXPORT_SYMBOL(ftape_abort_operation);
EXPORT_SYMBOL(ftape_get_status);
EXPORT_SYMBOL(ftape_enable);
EXPORT_SYMBOL(ftape_disable);
EXPORT_SYMBOL(ftape_mmap);
EXPORT_SYMBOL(ftape_calibrate_data_rate);
/* from ftape-io.c */
EXPORT_SYMBOL(ftape_reset_drive);
EXPORT_SYMBOL(ftape_command);
EXPORT_SYMBOL(ftape_parameter);
EXPORT_SYMBOL(ftape_ready_wait);
EXPORT_SYMBOL(ftape_report_operation);
EXPORT_SYMBOL(ftape_report_error);
/* from ftape-read.c */
EXPORT_SYMBOL(ftape_read_segment_fraction);
EXPORT_SYMBOL(ftape_zap_read_buffers);
EXPORT_SYMBOL(ftape_read_header_segment);
EXPORT_SYMBOL(ftape_decode_header_segment);
/* from ftape-write.c */
EXPORT_SYMBOL(ftape_write_segment);
EXPORT_SYMBOL(ftape_start_writing);
EXPORT_SYMBOL(ftape_loop_until_writes_done);
/* from ftape-buffer.h */
EXPORT_SYMBOL(ftape_set_nr_buffers);
/* from ftape-format.h */
EXPORT_SYMBOL(ftape_format_track);
EXPORT_SYMBOL(ftape_format_status);
EXPORT_SYMBOL(ftape_verify_segment);
/* from tracing.c */
#ifndef CONFIG_FT_NO_TRACE_AT_ALL
EXPORT_SYMBOL(ftape_tracing);
EXPORT_SYMBOL(ftape_function_nest_level);
EXPORT_SYMBOL(ftape_trace_call);
EXPORT_SYMBOL(ftape_trace_exit);
EXPORT_SYMBOL(ftape_trace_log);
#endif

