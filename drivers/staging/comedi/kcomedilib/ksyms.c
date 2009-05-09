/*
    comedi/kcomedilib/ksyms.c
    a comedlib interface for kernel modules

    COMEDI - Linux Control and Measurement Device Interface
    Copyright (C) 1997-2001 David A. Schleef <ds@schleef.org>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

*/

#ifndef EXPORT_SYMTAB
#define EXPORT_SYMTAB
#endif

#include "../comedi.h"
#include "../comedilib.h"
#include "../comedidev.h"

#include <linux/module.h>

#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/fcntl.h>
#include <linux/delay.h>
#include <linux/ioport.h>
#include <linux/mm.h>
#include <linux/slab.h>

/* functions specific to kcomedilib */

EXPORT_SYMBOL(comedi_register_callback);
EXPORT_SYMBOL(comedi_get_krange);
EXPORT_SYMBOL(comedi_get_buf_head_pos);
EXPORT_SYMBOL(comedi_set_user_int_count);
EXPORT_SYMBOL(comedi_map);
EXPORT_SYMBOL(comedi_unmap);

/* This list comes from user-space comedilib, to show which
 * functions are not ported yet. */

EXPORT_SYMBOL(comedi_open);
EXPORT_SYMBOL(comedi_close);

/* logging */
EXPORT_SYMBOL(comedi_loglevel);
EXPORT_SYMBOL(comedi_perror);
EXPORT_SYMBOL(comedi_strerror);
//EXPORT_SYMBOL(comedi_errno);
EXPORT_SYMBOL(comedi_fileno);

/* device queries */
EXPORT_SYMBOL(comedi_get_n_subdevices);
EXPORT_SYMBOL(comedi_get_version_code);
EXPORT_SYMBOL(comedi_get_driver_name);
EXPORT_SYMBOL(comedi_get_board_name);

/* subdevice queries */
EXPORT_SYMBOL(comedi_get_subdevice_type);
EXPORT_SYMBOL(comedi_find_subdevice_by_type);
EXPORT_SYMBOL(comedi_get_subdevice_flags);
EXPORT_SYMBOL(comedi_get_n_channels);
//EXPORT_SYMBOL(comedi_range_is_chan_specific);
//EXPORT_SYMBOL(comedi_maxdata_is_chan_specific);

/* channel queries */
EXPORT_SYMBOL(comedi_get_maxdata);
#ifdef KCOMEDILIB_DEPRECATED
EXPORT_SYMBOL(comedi_get_rangetype);
#endif
EXPORT_SYMBOL(comedi_get_n_ranges);
//EXPORT_SYMBOL(comedi_find_range);

/* buffer queries */
EXPORT_SYMBOL(comedi_get_buffer_size);
//EXPORT_SYMBOL(comedi_get_max_buffer_size);
//EXPORT_SYMBOL(comedi_set_buffer_size);
EXPORT_SYMBOL(comedi_get_buffer_contents);
EXPORT_SYMBOL(comedi_get_buffer_offset);

/* low-level stuff */
//EXPORT_SYMBOL(comedi_trigger);
//EXPORT_SYMBOL(comedi_do_insnlist);
EXPORT_SYMBOL(comedi_do_insn);
EXPORT_SYMBOL(comedi_lock);
EXPORT_SYMBOL(comedi_unlock);

/* physical units */
//EXPORT_SYMBOL(comedi_to_phys);
//EXPORT_SYMBOL(comedi_from_phys);

/* synchronous stuff */
EXPORT_SYMBOL(comedi_data_read);
EXPORT_SYMBOL(comedi_data_read_hint);
EXPORT_SYMBOL(comedi_data_read_delayed);
EXPORT_SYMBOL(comedi_data_write);
EXPORT_SYMBOL(comedi_dio_config);
EXPORT_SYMBOL(comedi_dio_read);
EXPORT_SYMBOL(comedi_dio_write);
EXPORT_SYMBOL(comedi_dio_bitfield);

/* slowly varying stuff */
//EXPORT_SYMBOL(comedi_sv_init);
//EXPORT_SYMBOL(comedi_sv_update);
//EXPORT_SYMBOL(comedi_sv_measure);

/* commands */
//EXPORT_SYMBOL(comedi_get_cmd_src_mask);
//EXPORT_SYMBOL(comedi_get_cmd_generic_timed);
EXPORT_SYMBOL(comedi_cancel);
EXPORT_SYMBOL(comedi_command);
EXPORT_SYMBOL(comedi_command_test);
EXPORT_SYMBOL(comedi_poll);

/* buffer configuration */
EXPORT_SYMBOL(comedi_mark_buffer_read);
EXPORT_SYMBOL(comedi_mark_buffer_written);

//EXPORT_SYMBOL(comedi_get_range);
EXPORT_SYMBOL(comedi_get_len_chanlist);

/* deprecated */
//EXPORT_SYMBOL(comedi_get_timer);
//EXPORT_SYMBOL(comedi_timed_1chan);

/* alpha */
//EXPORT_SYMBOL(comedi_set_global_oor_behavior);
